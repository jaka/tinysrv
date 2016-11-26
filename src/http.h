#ifndef _TINYSRV_HTTP_H
#define _TINYSRV_HTTP_H

#include "project.h"
#include <stddef.h>

#define HEADER_HOSTNAME_STR "Host"
#define HEADER_ACCEPT_STR "Accept"
#define HEADER_REFERER_STR "Referer"
#define HEADER_CONNECTION_STR "Connection"
#define HEADER_CONTENT_TYPE_STR "Content-type"
#define HEADER_CONTENT_LENGTH_STR "Content-length"
#define HEADER_LOCATION_STR "Location"

typedef enum {
  HEADER_HOSTNAME,
  HEADER_ACCEPT,
  HEADER_REFERER,
  HEADER_CONNECTION,
  HEADER_CONTENT_TYPE,
  HEADER_CONTENT_LENGTH,
  HEADER_LOCATION
} http_field_key_index;

struct http_field_key {
  http_field_key_index key_index;
  char *key;
  int short_key_length;
};

typedef struct http_field_key http_field_key_t;

static const http_field_key_t http_field_keys[] = {
  { HEADER_HOSTNAME, HEADER_HOSTNAME_STR, 1 },
  { HEADER_ACCEPT, HEADER_ACCEPT_STR, 1 },
  { HEADER_REFERER, HEADER_REFERER_STR, 1 },
  { HEADER_CONTENT_TYPE, HEADER_CONTENT_TYPE_STR, 9 },
  { HEADER_CONTENT_LENGTH, HEADER_CONTENT_LENGTH_STR, 9 },
  { HEADER_CONNECTION, HEADER_CONNECTION_STR, 4 },
  { HEADER_LOCATION, HEADER_LOCATION_STR, 1 },
  { 0, NULL, 0 }
};

#define HTTP_HEADER_FIELDS (sizeof(http_field_keys) / sizeof(http_field_key_t))
#define HTTP_HEADER_KEY_LENGTH 16

typedef enum {
  HTTP_METHOD_UNKNOWN,
  HTTP_METHOD_GET,
  HTTP_METHOD_POST,
  HTTP_METHOD_HEAD
} http_method;

typedef enum {
  HTTP_VERSION_UNKNOWN,
  HTTP_VERSION_10,
  HTTP_VERSION_11
} http_version;

typedef enum {
  HTTP_CONNECTION_KEEPALIVE = 1,
  HTTP_CONNECTION_UPGRADE = 1 << 2
} http_connection;

struct ps_http_request_header {
  http_method method;
  http_version version;
  int connection;
  char *filename;
  char *query;
  /* HTTP header fields. */
  char *header_start;
};

typedef struct ps_http_request_header ps_http_request_header_t;

struct ps_http_response_header {
  http_version version;
  /* HTTP status code. */
  int status_code;
  char *field[HTTP_HEADER_FIELDS];
};

typedef struct ps_http_response_header ps_http_response_header_t;

struct ps_http_status {
  int status_code;
  const char *status_msg;
};

typedef struct ps_http_header ps_http_header_t;
typedef struct ps_http_status ps_http_status_t;

static const ps_http_status_t err_http_status = { 500, "Internal Server Error" };

static const ps_http_status_t http_statuses[] = {
  { 200, "OK" },
  { 204, "No Content" },
  { 307, "Temporary Redirect" },
  { 400, "Bad Request" },
  { 501, "Method Not Implemented" },
  { 0, NULL }
};

int http_header_parse(ps_http_request_header_t *, char *, int *);
char *http_header_getvalue(ps_http_request_header_t *, const unsigned int);

int http_header_setvalue(ps_http_response_header_t *header, const unsigned int key_index, const char *value);
int http_header_fill(ps_http_response_header_t *header, char *buffer, int buflen);

#endif
