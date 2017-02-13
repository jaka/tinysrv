#ifdef USE_SSL

#include "ssl.h"
#include "utils.h"

#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h> /* struct stat */

static int ts_ssl_loadcert(SSL *s, struct ts_ssl *ssl, const char *file) {

  ssl->subcontext = SSL_CTX_new(TLSv1_2_server_method());
  if ( ssl->subcontext == NULL )
    return SSL_TLSEXT_ERR_ALERT_FATAL;

  SSL_CTX_set_options(ssl->subcontext, SSL_OP_SINGLE_DH_USE);
  if ( SSL_CTX_use_certificate_file(ssl->subcontext, file, SSL_FILETYPE_PEM) <= 0 ||
       SSL_CTX_use_PrivateKey_file(ssl->subcontext, file, SSL_FILETYPE_PEM) <= 0 ) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  SSL_set_SSL_CTX(s, ssl->subcontext);
  return SSL_TLSEXT_ERR_OK;
}

static int ts_ssl_servername_cb(SSL *s, int *ad, void *arg) {

  struct ts_ssl *ssl;
  char file[MAX_PATH_LENGTH];
  int dot_count;
  char *filename;
  char *pem_filename;
  struct stat st;
  int rv;

  ssl = (struct ts_ssl *)arg;
  rv = SSL_TLSEXT_ERR_OK;

  /* Get servername from SSL request and save it. */
  ssl->servername = (char *)SSL_get_servername(s, TLSEXT_NAMETYPE_host_name);

  DEBUG_PRINT("SSL request for hostname: %s.", ssl->servername);

  /* Allocate memory for servername and transform it. */
  filename = strdup(ssl->servername);
  dot_count = change_dots_to_underscore(filename);
  if ( dot_count < 0 ) {
    rv = SSL_TLSEXT_ERR_ALERT_FATAL;
    goto free_all;
  }

  pem_filename = filename;

  /* Check certificate. */
  ts_concatenate_path_filename(file, sizeof(file), ssl->cert_path, pem_filename);
  DEBUG_PRINT("Certificate file: %s", file);
  if ( stat(file, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG ) {
    rv = ts_ssl_loadcert(s, ssl, file);
    goto free_all;
  }

  /* Check wildcard certificate. */
  if ( dot_count > 1 && *pem_filename != '_' ) {
    pem_filename++;
    while ( *pem_filename && *pem_filename != '_' )
      pem_filename++;
    *(--pem_filename) = '+';
    ts_concatenate_path_filename(file, sizeof(file), ssl->cert_path, pem_filename);
    DEBUG_PRINT("Certificate file: %s", file);
    if ( stat(file, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG ) {
      rv = ts_ssl_loadcert(s, ssl, file);
    }
  }

free_all:
  free(filename);
  return rv;
}

int ts_ssl_sendfile(SSL *s, int filefd, int filesize) {

  unsigned char *p, *buf;
  int remaining_size, len;
  int rv;

  buf = mmap(0, (size_t)filesize, PROT_READ, MAP_PRIVATE, filefd, 0);

  if ( buf != (unsigned char *)-1 ) {

    p = buf;
	remaining_size = filesize;

    while ( remaining_size > 0 ) {

      if ( remaining_size > MAX_SEND_BUFFER_SIZE )
        len = MAX_SEND_BUFFER_SIZE;
      else
        len = remaining_size;

      rv = SSL_write(s, p, len);
      if ( rv != len )
        break;

      p += rv;
      remaining_size -= rv;

    }
    munmap(buf, (size_t)filesize);
  }

  return 0;
}

int ts_ssl_session_init(struct ts_ssl *ssl, int fd) {

  ssl->context = SSL_CTX_new(TLSv1_2_server_method());

  if ( ssl->context == NULL )
    return -1;

  SSL_CTX_set_options(ssl->context, SSL_MODE_RELEASE_BUFFERS | SSL_OP_NO_COMPRESSION);
  SSL_CTX_set_tlsext_servername_callback(ssl->context, ts_ssl_servername_cb);
  SSL_CTX_set_tlsext_servername_arg(ssl->context, ssl);

  ssl->s = SSL_new(ssl->context);
  if ( ssl->s == NULL ) {
    SSL_CTX_free(ssl->context);
    return -1;
  }

  SSL_set_fd(ssl->s, fd);

  ssl->subcontext = NULL;
  return SSL_accept(ssl->s);
}

int ts_ssl_session_close(struct ts_ssl *ssl) {

  SSL_set_shutdown(ssl->s, SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);

  SSL_free(ssl->s);
  if ( ssl->subcontext != NULL )
    SSL_CTX_free(ssl->subcontext);
  SSL_CTX_free(ssl->context);
  return 0;
}

#endif /* USE_SSL */
