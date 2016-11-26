#ifndef _TINYSRV_SSL_H
#define _TINYSRV_SSL_H

#include "project.h"

#define MAX_SEND_BUFFER_SIZE 1048576

#include <openssl/ssl.h>
#include <openssl/err.h>

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

#endif
