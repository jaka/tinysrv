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
  int tok_length, line_length;

  *error = 400;

  /* Determine the length of the Request-Line (first line in HTTP request). */
  line_length = find_delimiter(buffer, &line, "\r\n");
  if ( line_length < 12 )
    return -1;

  /* Parse HTTP method. */
  tok_length = find_delimiter(buffer, &str, " ");

  if ( tok_length == 0 )
    return -1;
  if ( !strncasecmp(buffer, http_method_getstr(HTTP_METHOD_GET), tok_length) )
    header->method = HTTP_METHOD_GET;
  else if ( !strncasecmp(buffer, http_method_getstr(HTTP_METHOD_HEAD), tok_length) )
    header->method = HTTP_METHOD_HEAD;
  else if ( !strncasecmp(buffer, http_method_getstr(HTTP_METHOD_POST), tok_length) ) {
    header->method = HTTP_METHOD_POST;
    *error = 501;
    return -1;
  }
  else {
    header->method = HTTP_METHOD_UNKNOWN;
    *error = 501;
    return -1;
  }

  /* Parse HTTP path. */
  while ( str && *str == ' ' )
    str++;
  if ( *str != '/' )
    return -1;

  tok = str;
  tok_length = find_delimiter(NULL, &str, " ");
  header->filename = strndup(tok, tok_length);
  header->query = header->filename + tok_length;

  tok = header->filename;
  while ( *tok ) {
    if ( *tok == '?' || *tok == '#' || *tok == ';' || *tok == '=' || *tok == ' ' ) {
      *tok = 0;
      header->query = tok + 1;
      break;
    }
    tok++;
  }

  /* Parse HTTP version. */
  tok = str;
  tok_length = find_delimiter(NULL, &str, "\r\n");
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

  /* Parse connection field. */
  for ( line_length = find_delimiter(line, &str, "\r\n"); line_length; line = str, line_length = find_delimiter(NULL, &str, "\r\n") ) {

    tok = line;
    if ( strncasecmp(tok, "Conn", 4) )
      continue;

    if ( *(tok + 10) != ':' )
      continue;

    tok += 11;
    line_length -= 11;

    header->connection = http_header_parse_connection(tok, line_length);

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
  for ( line_length = find_delimiter(line, &str, "\r\n"); line_length; line = str, line_length = find_delimiter(NULL, &str, "\r\n") ) {

    if ( strncasecmp(line, http_field_keys[index].key, http_field_keys[index].short_key_length) )
      continue;

    line += strlen(http_field_keys[index].key);
    line_length -= strlen(http_field_keys[index].key);

    if ( *line != ':' )
      continue;
    line++;
    line_length--;

    while ( line && *line == ' ' ) {
      line++;
      line_length--;
    }

    return strndup(line, line_length);
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

int http_header_fill(ps_http_response_header_t *header, char *buffer, int buflen) {

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
