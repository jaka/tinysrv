#ifndef _TINYSRV_SSL_H
#define _TINYSRV_SSL_H

#include "project.h"

#ifdef USE_SSL

#define MAX_SEND_BUFFER_SIZE 1048576

#include <openssl/err.h>
#include <openssl/ssl.h>

struct ts_ssl {
  SSL *s;
  SSL_CTX *context;
  SSL_CTX *subcontext;
  const char *servername;
  const char *cert_path;
};

int ts_ssl_session_init(struct ts_ssl *, int);
int ts_ssl_session_close(struct ts_ssl *);
int ts_ssl_sendfile(SSL *, int, int);

#else

struct ts_ssl {
  void *s;
  void *context;
  void *subcontext;
  const char *servername;
  const char *cert_path;
};

#endif /* USE_SSL */

#endif
