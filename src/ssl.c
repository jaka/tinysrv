#ifdef USE_SSL

#include "ssl.h"
#include "utils.h"

#include <ctype.h> /* tolower() */
#include <string.h>
#include <sys/stat.h> /* struct stat */

static int ts_ssl_loadcert(SSL *s, struct ts_ssl *ssl, const char *file)
{
  ssl->subcontext = SSL_CTX_new(TLS_server_method());
  if (!ssl->subcontext) return SSL_TLSEXT_ERR_ALERT_FATAL;

  SSL_CTX_set_options(ssl->subcontext, SSL_OP_SINGLE_DH_USE);
  if (SSL_CTX_use_certificate_file(ssl->subcontext, file, SSL_FILETYPE_PEM) <= 0 ||
      SSL_CTX_use_PrivateKey_file(ssl->subcontext, file, SSL_FILETYPE_PEM) <= 0) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  SSL_set_SSL_CTX(s, ssl->subcontext);
  return SSL_TLSEXT_ERR_OK;
}

static int ts_ssl_trycert(SSL *s, struct ts_ssl *ssl, char *file, int buflen, const char *name, int *found)
{
  struct stat st;

  *found = 0;
  if (ts_concatenate_path_filename(file, buflen, ssl->cert_path, name) < 0) return SSL_TLSEXT_ERR_ALERT_FATAL;

  DEBUG_PRINT("Certificate file: %s", file);
  if (stat(file, &st) || (st.st_mode & S_IFMT) != S_IFREG) return SSL_TLSEXT_ERR_OK;

  *found = 1;
  return ts_ssl_loadcert(s, ssl, file);
}

static int ts_ssl_servername_cb(SSL *s, int *ad, void *arg)
{
  struct ts_ssl *ssl;
  char file[MAX_PATH_LENGTH];
  int dot_count, found, rv;
  char *filename, *pem_filename, *p;

  (void)ad;
  ssl = (struct ts_ssl *)arg;

  /* Get servername from SSL request and save it. */
  ssl->servername = (char *)SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);
  if (!ssl->servername) return SSL_TLSEXT_ERR_ALERT_FATAL;

  DEBUG_PRINT("SSL request for hostname: %s.", ssl->servername);

  /* Allocate memory for servername and transform it. */
  filename = strdup(ssl->servername);
  if (!filename) return SSL_TLSEXT_ERR_ALERT_FATAL;

  /* SNI host names are case-insensitive, so match lower-case certificate files. */
  for (p = filename; *p; p++) *p = tolower((unsigned char)*p);

  dot_count = change_dots_to_underscore(filename);
  if (dot_count < 0) {
    free(filename);
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  pem_filename = filename;

  /* Check certificate. */
  rv = ts_ssl_trycert(s, ssl, file, sizeof(file) - 1, pem_filename, &found);
  if (found || rv != SSL_TLSEXT_ERR_OK) goto free_all;

  /* Check wildcard certificate. */
  if (dot_count > 1 && *pem_filename != '_') {
    pem_filename++;
    while (*pem_filename && *pem_filename != '_') pem_filename++;
    *(--pem_filename) = '+';
    rv = ts_ssl_trycert(s, ssl, file, sizeof(file) - 1, pem_filename, &found);
  }

free_all:
  free(filename);
  return rv;
}

/*
Prepare a TLS session on a non-blocking socket. The handshake itself (SSL_accept) is driven
by the caller as the socket becomes ready. Returns 0 on success, -1 on failure.
*/
int ts_ssl_session_init(struct ts_ssl *ssl, int fd)
{
  ssl->context = SSL_CTX_new(TLS_server_method());
  if (!ssl->context) return -1;

  SSL_CTX_set_min_proto_version(ssl->context, TLS1_2_VERSION);
  SSL_CTX_set_options(ssl->context, SSL_OP_NO_COMPRESSION);
  SSL_CTX_set_tlsext_servername_callback(ssl->context, ts_ssl_servername_cb);
  SSL_CTX_set_tlsext_servername_arg(ssl->context, ssl);

  /* On failure ssl->context is released by ts_ssl_session_close(). */
  ssl->s = SSL_new(ssl->context);
  if (!ssl->s) return -1;

  if (!SSL_set_fd(ssl->s, fd)) return -1;

  ssl->subcontext = NULL;
  return 0;
}

int ts_ssl_session_close(struct ts_ssl *ssl)
{
  if (!ssl) return -1;

  if (ssl->s) {
    SSL_set_shutdown(ssl->s, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
    SSL_free(ssl->s);
    ssl->s = NULL;
  }
  if (ssl->subcontext != NULL) {
    SSL_CTX_free(ssl->subcontext);
    ssl->subcontext = NULL;
  }
  if (ssl->context != NULL) {
    SSL_CTX_free(ssl->context);
    ssl->context = NULL;
  }
  return 0;
}

#endif /* USE_SSL */
