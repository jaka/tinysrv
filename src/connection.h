#ifndef _TINYSRV_CONNECTION_H
#define _TINYSRV_CONNECTION_H

#include "project.h"
#include "config.h"
#include "http.h"

struct ts_connection {
  const char *str;
  int length;
  int filefd;
  ps_http_request_header_t *request;
  ps_http_response_header_t *response;
};

typedef struct ts_connection ps_connection_t;

static const char content_noSSL[] =
  "\x15" /* Alert (21) */
  "\x03\x00" /* Version 3.0 */
  "\x00\x02" /* length 02 */
  "\x02" /* fatal */
  "\x00" /* 0 close notify, 0x28 Handshake failure 40, 0x31 TLS access denied 49 */
  "\x00"; /* string terminator (not part of actual response) */

static const char content_jsclose[] =
  "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
  "<title></title><script type='text/javascript'>"
  "if(self==top)window.close();"
  "</script></head></html>";

int connection_new(ts_socket_t *, int);

#endif
