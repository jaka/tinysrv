#ifdef USE_SSL

#include "ssl.h"
#include "utils.h"

#include <ctype.h> /* tolower() */
#include <string.h>
#include <sys/mman.h>
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

int ts_ssl_sendfile(SSL *s, int filefd, off_t filesize)
{
  unsigned char *buf;
  off_t written, remaining_size;
  int len, rv;

  if (filesize <= 0) return 0;

  buf = mmap(0, (size_t)filesize, PROT_READ, MAP_PRIVATE, filefd, 0);
  if (buf == MAP_FAILED) return -1;

  written = 0;
  for (remaining_size = filesize; remaining_size > 0;) {
    len = remaining_size > MAX_SEND_BUFFER_SIZE ? MAX_SEND_BUFFER_SIZE : (int)remaining_size;

    rv = SSL_write(s, buf + written, len);
    if (rv <= 0) break;

    written += rv;
    remaining_size -= rv;
  }

  munmap(buf, (size_t)filesize);

  return remaining_size > 0 ? -1 : (int)written;
}

int ts_ssl_session_init(struct ts_ssl *ssl, int fd)
{
  ssl->context = SSL_CTX_new(TLS_server_method());
  if (!ssl->context) return -1;

  SSL_CTX_set_min_proto_version(ssl->context, TLS1_2_VERSION);
  SSL_CTX_set_options(ssl->context, SSL_MODE_RELEASE_BUFFERS | SSL_OP_NO_COMPRESSION);
  SSL_CTX_set_tlsext_servername_callback(ssl->context, ts_ssl_servername_cb);
  SSL_CTX_set_tlsext_servername_arg(ssl->context, ssl);

  ssl->s = SSL_new(ssl->context);
  if (!ssl->s) {
    SSL_CTX_free(ssl->context);
    return -1;
  }

  SSL_set_fd(ssl->s, fd);

  ssl->subcontext = NULL;
  return SSL_accept(ssl->s);
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
