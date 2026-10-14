#include "http.h"
#include "utils.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h> /* free(), malloc() */
#include <string.h> /* strcmp(), strncmp(), strndup(), strtok() */

static const char *http_version_getstr(int version)
{
  switch (version) {
    case HTTP_VERSION_10:
      return "HTTP/1.0";
    case HTTP_VERSION_11:
      return "HTTP/1.1";
    default:
      return "UNKNOWN";
  }
}

static const ps_http_status_t *http_header_get_status(int status_code)
{
  unsigned int i;

  for (i = 0; http_statuses[i].status_code; i++) {
    if (http_statuses[i].status_code == status_code) return &http_statuses[i];
  }
  return &err_http_status;
}

static unsigned int http_header_parse_connection(const char *str, const unsigned int len)
{
  char *end_str;
  unsigned int connection = 0;

  end_str = (char *)str + len;
  while (str < end_str) {

    switch (*str) {

      case 'K':
      case 'k':
        if (!strncasecmp(str, "keep-alive", 10)) {
          DEBUG_PRINT("Connection: keep-alive.");
          connection |= HTTP_CONNECTION_KEEPALIVE;
          str += 10;
        }
        else str++;
        break;

      case 'C':
      case 'c':
        if (!strncasecmp(str, "close", 5)) {
          DEBUG_PRINT("Connection: close.");
          connection &= ~HTTP_CONNECTION_KEEPALIVE;
          str += 5;
        }
        else str++;
        break;

      case 'U':
      case 'u':
        if (!strncasecmp(str, "upgrade", 7)) {
          DEBUG_PRINT("Connection: upgrade.");
          connection |= HTTP_CONNECTION_UPGRADE;
          str += 7;
        }
        else str++;
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

static char *http_header_find(char *header_start, const char *key, unsigned int key_length, unsigned int *value_length)
{
  char *line, *str;
  unsigned int line_length;

  line = header_start;
  for (str = tu_strbtok(&line, &line_length, "\r\n"); line_length; str = tu_strbtok(&line, &line_length, "\r\n")) {

    if (strncasecmp(str, key, key_length)) continue;
    str += key_length;
    line_length -= key_length;

    if (*str != ':') continue;
    str++;
    line_length--;

    while (*str == ' ' || *str == '\t') {
      str++;
      line_length--;
    }

    if (value_length) *value_length = line_length;
    return str;
  }

  return NULL;
}

int http_header_parse(ps_http_request_header_t *header, char *buffer, int *error)
{
  char *str;
  char *tok, *line = buffer;
  unsigned int tok_length, line_length;

  *error = 400;

  /* Determine the length of the Request-Line (first line in HTTP request). */
  str = tu_strbtok(&line, &line_length, "\r\n");
  if (line_length < 12) return -1;

  /* Parse HTTP method. */
  tok = tu_strtok(&str, &tok_length, " \t");
  DEBUG_PRINT("Got method: %.*s", tok_length, tok);
  if (!tok_length) return -1;
  if (!strncasecmp(tok, "GET", tok_length))
    header->method = HTTP_METHOD_GET;
  else if (!strncasecmp(tok, "HEAD", tok_length))
    header->method = HTTP_METHOD_HEAD;
  else if (!strncasecmp(tok, "POST", tok_length)) {
    header->method = HTTP_METHOD_POST;
    *error = 501;
    return -1;
  }
  else {
    header->method = HTTP_METHOD_UNKNOWN;
    *error = 501;
    return -1;
  }

  while (*str == ' ' || *str == '\t') str++;

  /* Parse HTTP Request-URI. */
  if (*str != '/') return -1;
  tok = tu_strtok(&str, &tok_length, " \t");
  header->filename = strndup(tok, tok_length);
  if (!header->filename) return -1;
  header->query = header->filename + tok_length;
  DEBUG_PRINT("Got filename: %s", header->filename);
  DEBUG_PRINT("Got query: %s", header->query);

  while (*str == ' ' || *str == '\t') str++;

  /* Parse HTTP version. */
  tok = tu_strbtok(&str, &tok_length, "\r\n");
  DEBUG_PRINT("Got HTTP version: %.*s", tok_length, tok);
  if (tok_length != 8) return -1;

  if (*(tok + 5) != '1')
    /* HTTP/2.0 is not yet supported. */
    return -1;

  switch (*(tok + 7)) {
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
  for (tok = header->filename; *tok; tok++) {
    if (*tok == '?' || *tok == '#' || *tok == ';' || *tok == '=' || *tok == ' ') {
      *tok = 0;
      header->query = tok + 1;
      break;
    }
  }

  /* Parse connection field. */
  str = http_header_find(line, "Connection", 10, &line_length);
  if (str) header->connection = http_header_parse_connection(str, line_length);

  *error = 0;
  return 0;
}

char *http_header_getvalue(ps_http_request_header_t *header, const unsigned int key_index)
{
  unsigned int value_length;
  char *value;

  value = http_header_find(header->header_start, http_field_keys[key_index].key, http_field_keys[key_index].key_length, &value_length);
  if (!value) return NULL;

  return strndup(value, value_length);
}

int http_header_setvalue(ps_http_response_header_t *header, const unsigned int key_index, const char *value)
{
  if (!value) return -1;
  /* Never allow CR/LF in a header value (header/response splitting). */
  if (strpbrk(value, "\r\n")) return -1;

  if (key_index >= HTTP_HEADER_FIELDS) return -1;

  free(header->field[key_index]);
  header->field[key_index] = strdup(value);
  return header->field[key_index] ? 0 : -1;
}

int http_header_fill(ps_http_response_header_t *header, char *buffer, int buflen)
{
  int index, len, offset, remaining;
  const ps_http_status_t *http_status;

  if (!buffer || buflen <= 0) return -1;

  offset = 0;
  remaining = buflen;

  len = snprintf(buffer + offset, remaining, "%s ", http_version_getstr(header->version));
  if (len < 0 || len >= remaining) return -1;
  offset += len;
  remaining -= len;

  http_status = http_header_get_status(header->status_code);
  len = snprintf(buffer + offset, remaining, "%d %s\r\n", http_status->status_code, http_status->status_msg);
  if (len < 0 || len >= remaining) return -1;
  offset += len;
  remaining -= len;

  for (index = 0; http_field_keys[index].key; index++) {
    if (!header->field[index]) continue;
    len = snprintf(buffer + offset, remaining, "%s: %s\r\n", http_field_keys[index].key, header->field[index]);
    if (len < 0 || len >= remaining) return -1;
    offset += len;
    remaining -= len;
  }

  len = snprintf(buffer + offset, remaining, "\r\n");
  if (len < 0 || len >= remaining) return -1;
  offset += len;

  return offset;
}
