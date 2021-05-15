#include "http.h"
#include "utils.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h> /* free(), malloc() */
#include <string.h> /* strcmp(), strncmp(), strndup(), strtok() */

static const char *http_method_getstr(int method) {
  switch ( method ) {
    case HTTP_METHOD_GET: return "GET";
    case HTTP_METHOD_HEAD: return "HEAD";
    case HTTP_METHOD_POST: return "POST";
    default: return "UNKNOWN";
  }
}

static const char *http_version_getstr(int version) {
  switch ( version ) {
    case HTTP_VERSION_10: return "HTTP/1.0";
    case HTTP_VERSION_11: return "HTTP/1.1";
    default: return "UNKNOWN";
  }
}

static const ps_http_status_t *http_header_get_status(int status_code) {

  int i;

  i = 0;
  while ( http_statuses[i].status_code ) {
    if ( http_statuses[i].status_code == status_code )
      return &http_statuses[i];
    i++;
  }
  return &err_http_status;
}

static unsigned int http_header_field_getindex(unsigned int key_index) {

  static unsigned int index_map[HTTP_HEADER_FIELDS];
  static int prepared = 0;

  unsigned int index;

  if ( !prepared ) {
    index = 0;
    while ( http_field_keys[index].key ) {
      index_map[http_field_keys[index].key_index] = index;
      index++;
    }
    prepared = 1;
  }

  return index_map[key_index];
}

static unsigned int http_header_parse_connection(const char *str, const unsigned int len) {

  char *end_str;
  unsigned int connection;

  connection = 0;

  end_str = (char *)str + len;
  while ( str < end_str ) {

    switch ( *str ) {

      case 'K':
      case 'k':
        if ( !strncasecmp(str, "keep-alive", 10) ) {
          DEBUG_PRINT("Connection: keep-alive.");
          connection |= HTTP_CONNECTION_KEEPALIVE;
          str += 10;
        }
        break;

      case 'C':
      case 'c':
        if ( !strncasecmp(str, "close", 5) ) {
          DEBUG_PRINT("Connection: close.");
          connection &= ~HTTP_CONNECTION_KEEPALIVE;
          str += 5;
        }
        break;

      case 'U':
      case 'u':
        if ( !strncasecmp(str, "upgrade", 5) ) {
          DEBUG_PRINT("Connection: upgrade.");
          connection |= HTTP_CONNECTION_UPGRADE;
          str += 7;
        }
        break;

      case ' ':
      case ',':
        str++;
        break;

      default:
        str = end_str;
    }

  }

  return connection;
}

int http_header_parse(ps_http_request_header_t *header, char *buffer, int *error) {

  char *str;
  char *tok, *line;
  unsigned int tok_length, line_length;

  *error = 400;
  line = buffer;

  /* Determine the length of the Request-Line (first line in HTTP request). */
  str = tu_strbtok(&line, &line_length, "\r\n");
  if ( line_length < 12 )
    return -1;

  /* Parse HTTP method. */
  tok = tu_strtok(&str, &tok_length, " \t");
  if ( tok_length == 0 )
    return -1;
  if ( !strncasecmp(tok, http_method_getstr(HTTP_METHOD_GET), tok_length) )
    header->method = HTTP_METHOD_GET;
  else if ( !strncasecmp(tok, http_method_getstr(HTTP_METHOD_HEAD), tok_length) )
    header->method = HTTP_METHOD_HEAD;
  else if ( !strncasecmp(tok, http_method_getstr(HTTP_METHOD_POST), tok_length) ) {
    header->method = HTTP_METHOD_POST;
    *error = 501;
    return -1;
  }
  else {
    header->method = HTTP_METHOD_UNKNOWN;
    *error = 501;
    return -1;
  }

  while ( *str == ' ' || *str == '\t' )
    str++;

  /* Parse HTTP Request-URI. */
  if ( *str != '/' )
    return -1;
  tok = tu_strtok(&str, &tok_length, " \t");
  header->filename = strndup(tok, tok_length);
  header->query = header->filename + tok_length;

  while ( *str == ' ' || *str == '\t' )
    str++;

  /* Parse HTTP version. */
  tok = tu_strbtok(&str, &tok_length, "\r\n");
  if ( tok_length != 8 )
    return -1;

  if ( *(tok + 5) != '1' )
    /* HTTP/2.0 is not yet supported. */
    return -1;

  switch ( *(tok + 7) ) {
    case '1':
      header->version = HTTP_VERSION_11;
      break;
    case '0':
      header->version = HTTP_VERSION_10;
      break;
    default:
      header->version = HTTP_VERSION_UNKNOWN;
      return -1;
  }

  header->header_start = line;

  /* Separate path and query. */
  tok = header->filename;
  while ( *tok ) {
    if ( *tok == '?' || *tok == '#' || *tok == ';' || *tok == '=' || *tok == ' ' ) {
      *tok = 0;
      header->query = tok + 1;
      break;
    }
    tok++;
  }

  /* Parse connection field. */
  for ( str = tu_strbtok(&line, &line_length, "\r\n"); line_length; str = tu_strbtok(&line, &line_length, "\r\n") ) {

    if ( strncasecmp(str, "Conn", 4) )
      continue;
    str += 10;
    line_length -= 10;

    while ( *str && *str != ':' && line_length > 0 ) {
      str++;
      line_length--;
    }

    if ( *str != ':' )
      continue;
    str++;
    line_length--;

    while ( *str == ' ' || *str == '\t' ) {
      str++;
      line_length--;
    }

    header->connection = http_header_parse_connection(str, line_length);

    break;
  }

  *error = 0;
  return 0;
}

char *http_header_getvalue(ps_http_request_header_t *header, const unsigned int key_index) {

  unsigned int index, line_length;
  char *line, *str;

  index = http_header_field_getindex(key_index);

  line = header->header_start;
  for ( str = tu_strbtok(&line, &line_length, "\r\n"); line_length; str = tu_strbtok(&line, &line_length, "\r\n") ) {

    if ( strncasecmp(str, http_field_keys[index].key, http_field_keys[index].short_key_length) )
      continue;
    str += strlen(http_field_keys[index].key);
    line_length -= strlen(http_field_keys[index].key);

    while ( *str && *str != ':' && line_length > 0 ) {
      str++;
      line_length--;
    }

    if ( *str != ':' )
      continue;
    str++;
    line_length--;

    while ( *str == ' ' || *str == '\t' ) {
      str++;
      line_length--;
    }

    return strndup(str, line_length);
  }

  return NULL;
}

int http_header_setvalue(ps_http_response_header_t *header, const unsigned int key_index, const char *value) {

  int index;

  if ( value == NULL )
    return -1;

  index = http_header_field_getindex(key_index);
  if ( index ) {
    if ( header->field[index] )
      free(header->field[index]);
    header->field[index] = strdup(value);
    return 0;
  }
  return -1;
}

int http_header_fill(ps_http_response_header_t *header, char *buffer, __attribute__((unused)) int buflen) {

  int index;
  int header_length;
  const ps_http_status_t *http_status;
  const char *sptr;

  sptr = http_version_getstr(header->version);

  strcpy(buffer, sptr);
  header_length = strlen(sptr);

  *(buffer + header_length) = ' ';
  header_length++;

  http_status = http_header_get_status(header->status_code);
  header_length += sprintf(buffer + header_length, "%d %s\r\n", http_status->status_code, http_status->status_msg);

  index = 0;
  while ( http_field_keys[index].key ) {
    if ( header->field[index] )
      header_length += sprintf(buffer + header_length, "%s: %s\r\n", http_field_keys[index].key, header->field[index]);
    index++;
  }
  strcpy(buffer + header_length, "\r\n");
  header_length += 2;

  return header_length;
}
