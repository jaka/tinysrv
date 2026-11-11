#ifndef _TINYSRV_CONNECTION_H
#define _TINYSRV_CONNECTION_H

#include "config.h"
#include "http.h"
#include "project.h"
#include "ssl.h"

#include <sys/select.h>
#include <sys/types.h>

struct ts_connection {
  const char *str;
  off_t length;
  int filefd;
  ps_http_request_header_t *request;
  ps_http_response_header_t *response;
};

typedef struct ts_connection ps_connection_t;

/* Stages of a connection, each advanced without blocking. */
typedef enum {
  CS_FREE = 0,  /* slot unused */
  CS_HANDSHAKE, /* TLS handshake */
  CS_READ,      /* reading the request header */
  CS_WRITE,     /* sending the response */
  CS_LINGER     /* response sent, discarding input until the client closes */
} ts_client_state;

struct ts_client {
  ts_client_state state;
  int fd;
  /* Wait for the socket to become writable instead of readable. */
  int want_write;
  /* Monotonic time in ms at which the current stage times out. */
  long deadline;
  ts_socket_t *sock;
  struct ts_ssl ssl;
  ps_http_request_header_t request;
  ps_http_response_header_t response;
  ps_connection_t connection;
  /* Request while reading; then the response header; then file chunks for TLS. */
  char buf[CHAR_BUF_SIZE];
  int buf_len;
  int buf_pos;
  int header_sent;
  off_t body_sent;
};

typedef struct ts_client ts_client_t;

static const char content_noSSL[] =
    "\x15"     /* Alert (21) */
    "\x03\x00" /* Version 3.0 */
    "\x00\x02" /* length 02 */
    "\x02"     /* fatal */
    "\x00"     /* 0 close notify, 0x28 Handshake failure 40, 0x31 TLS access denied 49 */
    "\x00";    /* string terminator (not part of actual response) */

static const char content_jsclose[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'/>"
    "<title></title><script type='text/javascript'>"
    "var a=window,b=document,c=b.referrer,z='.',D=function(u){var d=u.indexOf('://')>-1?u.split('/')[2]:u.split('/')[0];return d.indexOf(z)>-1?d:d.split(z)[0]};"
    "if(self==top){a.close();if(c&&c.length>0){var d=D(c),e=d.split(z).reverse();if(e.length>1){var f='u='+e.slice(0,2).reverse().join(z);b.cookie!=f&&(b.cookie=f,a.history.back())}}}"
    "</script></head></html>";

int connection_new(ts_client_t *, ts_socket_t *, int);
void connection_fdset(ts_client_t *, fd_set *, fd_set *, int *, long *);
void connection_process(ts_client_t *, fd_set *, fd_set *);
void connection_abort(ts_client_t *);

#endif
