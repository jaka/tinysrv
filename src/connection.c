#include "connection.h"
#include "mime.h"
#include "ssl.h"
#include "utils.h"

#include <arpa/inet.h>   /* recv(), send(), SOL_SOCKET */
#include <ctype.h>       /* tolower() */
#include <errno.h>       /* errno */
#include <fcntl.h>       /* O_RDONLY */
#include <netinet/tcp.h> /* TCP_CORK */
#include <stdio.h>
#include <stdlib.h>       /* free(), malloc() */
#include <string.h>       /* strcasestr() */
#include <sys/sendfile.h> /* sendfile() */
#include <sys/stat.h>     /* struct stat */
#include <time.h>         /* time() */
#include <unistd.h>       /* close() */

static const mime_t *get_mime(char *ext)
{
  unsigned int i;

  if (!ext || *ext != '.') return &no_mime;
  ext++;

  for (i = 0; default_mimes[i].ext; i++)
    if (!strncasecmp(ext, default_mimes[i].ext, default_mimes[i].ext_len) && !ext[default_mimes[i].ext_len])
      return &default_mimes[i];

  return &default_mimes[i];
}

/* Open `rel` beneath `base`, refusing to follow symlinks in any component. */
static int open_nofollow(const char *base, const char *rel, struct stat *st)
{
  char *path, *p, *slash;
  int dirfd, fd, next;

  dirfd = open(base, O_RDONLY | O_DIRECTORY);
  if (dirfd < 0) return -1;

  path = strdup(rel);
  if (!path) {
    close(dirfd);
    return -1;
  }

  fd = -1;
  p = path;
  while (*p) {
    while (*p == '/') p++;
    if (!*p) break;

    slash = strchr(p, '/');
    if (!slash) {
      /* O_NONBLOCK so a FIFO cannot block the worker before the regular-file check. */
      fd = openat(dirfd, p, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
      if (fd >= 0 && (fstat(fd, st) < 0 || (st->st_mode & S_IFMT) != S_IFREG)) {
        close(fd);
        fd = -1;
      }
      break;
    }

    *slash = 0;
    next = openat(dirfd, p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    close(dirfd);
    dirfd = next;
    if (dirfd < 0) break;
    p = slash + 1;
  }

  if (dirfd >= 0) close(dirfd);
  free(path);
  return fd;
}

static int handle_file(ps_connection_t *connection, ts_socket_t *sock, const char *filename)
{
  char file[MAX_PATH_LENGTH];
  char *hostname, *rel, *colon, *strp;
  int fd;
  struct stat st;

  if (!*(filename + 1)) return -1;

  hostname = http_header_getvalue(connection->request, HEADER_HOSTNAME);
  if (!hostname) return -1;
  if (strchr(hostname, '/') != NULL || *hostname == '.' || !*hostname) {
    free(hostname);
    return -1;
  }

  colon = strchr(hostname, ':');
  if (colon) *colon = 0;

  if (!*hostname) {
    free(hostname);
    return -1;
  }

  /* Host names are case-insensitive, so map them to directories in lower case. */
  for (strp = hostname; *strp; strp++) *strp = tolower((unsigned char)*strp);

  if (change_dots_to_underscore(hostname) < 0) {
    free(hostname);
    connection->response->status_code = 400;
    return -1;
  }

  if (sock->serve_path_length + strlen(hostname) + strlen(filename) + 1 > sizeof(file)) {
    free(hostname);
    return -1;
  }

  rel = file + sock->serve_path_length;
  if (sock->serve_path_length > 0) memcpy(file, sock->serve_path, sock->serve_path_length);
  strcpy(rel, hostname);
  strcat(rel, filename);

  free(hostname);

  DEBUG_PRINT("Requested local file: %s", file);

  /* Open the file beneath the serve path, refusing to follow symlinks. */
  fd = open_nofollow(sock->serve_path, rel, &st);
  if (fd < 0) {
    connection->response->status_code = 403;
    return -1;
  }

  connection->length = st.st_size;

  if (connection->request->method == HTTP_METHOD_GET) {
    connection->filefd = fd;
    connection->response->status_code = 200;
    return 0;
  }
  if (connection->request->method == HTTP_METHOD_HEAD) {
    close(fd);
    connection->response->status_code = 200;
    return 0;
  }

  close(fd);
  connection->response->status_code = 501;
  return -1;
}

static int handle_redirect(ps_connection_t *connection)
{
  char *query, *referer, *url, *p, *decoded_query;

  query = connection->request->query;
  if (!*query) return -1;

  DEBUG_PRINT("Query: %s.", query);

  if (!strcasestr(query, "=http") && !strcasestr(query, "\\x3dhttp") && !strcasestr(query, "%5Cx3dhttp"))
    return -1;

  /* Decoded string could be shorter, but +1 for termination. */
  decoded_query = malloc((strlen(query) + 1) * sizeof(char));
  if (!decoded_query) return -1;
  decode_url(decoded_query, query);

  /* Double decode */
  decode_url(decoded_query, decoded_query);

  url = strstr_last(decoded_query, "http://");
  if (!url) url = strstr_last(decoded_query, "https://");

  /* Reject control characters (e.g. CR/LF) to prevent header/response splitting. */
  for (p = url; p && *p; p++) {
    if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) {
      url = NULL;
      break;
    }
  }

  if (url) {
    referer = http_header_getvalue(connection->request, HEADER_REFERER);
    if (referer != NULL) {
      if (strstr(referer, url) && !strstr(referer, "adurl")) url = NULL;
      free(referer);
    }
  }

  if (url) {
    if (http_header_setvalue(connection->response, HEADER_LOCATION, url) < 0) url = NULL;
    else connection->response->status_code = 307;
  }

  free(decoded_query);

  return (url) ? 0 : -1;
}

static int handle_jsclose(ps_connection_t *connection, const mime_t *mime)
{
  char *accept;
  int cont;

  if (mime->ext)
    /* We return JSCLOSE if file extension is htm or html. */
    cont = !strcmp(mime->ext, "htm") || !strcmp(mime->ext, "html");
  else if (!mime->ext_len) {
    /* Or if Mimetype is unknown. */
    accept = http_header_getvalue(connection->request, HEADER_ACCEPT);
    cont = accept && strcmp(accept, "\x2A\x2F\x2A"); /*  * / *  */
    free(accept);
  }
  else
    cont = 0;

  if (!cont) return -1;

  if (http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, "text/html") < 0) return -1;

  connection->response->status_code = 200;
  connection->length = sizeof(content_jsclose) - 1;
  if (connection->request->method == HTTP_METHOD_GET) connection->str = content_jsclose;
  return 0;
}

static int serve(ps_connection_t *connection, ts_socket_t *sock, int *error)
{
  char *filename, *ext;
  const mime_t *mime;

  if (connection->request->method != HTTP_METHOD_GET && connection->request->method != HTTP_METHOD_HEAD) {
    *error = 501;
    return -1;
  }

  filename = connection->request->filename;
  if (!filename) {
    *error = 400;
    return -1;
  }

  ext = strrchr(filename, '.');
  mime = get_mime(ext);
  DEBUG_PRINT("Serve ext: %s", ext);
  DEBUG_PRINT("Serve mime: %s", mime->typestr);

  /* HTTP 204 No Content for Google generate_204 URLs. */
  if ((sock->options & DO_204) && (!strcasecmp(filename, "/generate_204") || !strcasecmp(filename, "/gen_204"))) {
    connection->response->status_code = 204;
    connection->length = 0;
    return 0;
  }

  /*
  Serve a real file. On failure handle_file() may leave a status (400, 403, 501) behind; by
  design it is not final: the redirect, JS close, 404 or placeholder below always replace it.
  */
  if (sock->serve_path_length > 0 && is_safe_filename(filename) && !handle_file(connection, sock, filename)) {
    if (http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, mime->typestr) < 0) {
      if (connection->filefd >= 0) close(connection->filefd);
      connection->filefd = -1;
      connection->length = 0;
      *error = 500;
      return -1;
    }
    return 0;
  }

  /* HTTP 307 Temporary Redirect for click counters (no content-type/body). */
  if ((sock->options & DO_REDIRECT) && !handle_redirect(connection)) return 0;

  /* Serve JS close script. */
  if ((sock->options & DO_CLOSE) && !handle_jsclose(connection, mime)) return 0;

  /* Serve files only: no empty/placeholder response. */
  if (sock->options & DO_FILES) {
    connection->response->status_code = 404;
    connection->length = 0;
    return 0;
  }

  /* Dummy file. */
  if (http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, mime->typestr) < 0) {
    *error = 500;
    return -1;
  }
  if (mime->response) {
    if (connection->request->method == HTTP_METHOD_GET) connection->str = mime->response;
    connection->length = mime->response_size;
  }
  else connection->length = 0;
  connection->response->status_code = 200;

  return 0;
}

/* I/O result meaning "not ready yet, wait for the socket and try again". */
#define IO_AGAIN -2

#ifdef USE_SSL
#define CLIENT_TLS(c) ((c)->sock->options & DO_SSL)
#else
#define CLIENT_TLS(c) 0
#endif

/* 64-bit even where long is 32-bit, which would overflow after 24.8 days of uptime. */
long long monotonic_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}

#ifdef USE_SSL
/* Map an SSL I/O result to bytes, 0 (closed), -1 (error) or IO_AGAIN, noting what to wait for. */
static int client_ssl_result(ts_client_t *c, int rv)
{
  if (rv > 0) return rv;

  switch (SSL_get_error(c->ssl.s, rv)) {
    case SSL_ERROR_WANT_READ:
      c->want_write = 0;
      return IO_AGAIN;
    case SSL_ERROR_WANT_WRITE:
      c->want_write = 1;
      return IO_AGAIN;
    case SSL_ERROR_ZERO_RETURN:
      return 0;
    default:
      return -1;
  }
}
#endif /* USE_SSL */

static int client_recv(ts_client_t *c, char *buf, int buflen)
{
  ssize_t rv;

#ifdef USE_SSL
  if (CLIENT_TLS(c)) {
    ERR_clear_error();
    return client_ssl_result(c, SSL_read(c->ssl.s, buf, buflen));
  }
#endif /* USE_SSL */

  rv = recv(c->fd, buf, buflen, 0);
  if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    c->want_write = 0;
    return IO_AGAIN;
  }
  return (int)rv;
}

static int client_send(ts_client_t *c, const char *buf, int buflen, int flags)
{
  ssize_t rv;

#ifdef USE_SSL
  if (CLIENT_TLS(c)) {
    ERR_clear_error();
    return client_ssl_result(c, SSL_write(c->ssl.s, buf, buflen));
  }
#endif /* USE_SSL */

  rv = send(c->fd, buf, buflen, MSG_NOSIGNAL | flags);
  if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    c->want_write = 1;
    return IO_AGAIN;
  }
  return (int)rv;
}

/* Release everything the connection holds and free its slot. */
static void client_close(ts_client_t *c)
{
  unsigned int index;

  if (c->connection.filefd >= 0) close(c->connection.filefd);
  c->connection.filefd = -1;

  free(c->request.filename);
  c->request.filename = NULL;
  for (index = 0; index < HTTP_HEADER_FIELDS; index++) {
    free(c->response.field[index]);
    c->response.field[index] = NULL;
  }

#ifdef USE_SSL
  if (CLIENT_TLS(c)) ts_ssl_session_close(&c->ssl);
#endif /* USE_SSL */

  close(c->fd);
  c->fd = -1;
  c->state = CS_FREE;
}

/*
Lingering close: closing a socket with unread data makes the kernel send RST, which can
destroy the response before the client has read it. Send FIN first, then discard whatever
the client still sends until it closes or the deadline passes.
*/
static void client_linger(ts_client_t *c)
{
  if (c->connection.filefd >= 0) close(c->connection.filefd);
  c->connection.filefd = -1;

#ifdef USE_SSL
  if (CLIENT_TLS(c)) {
    /*
    Send close_notify so the client sees a clean end of the TLS stream. One attempt only:
    we don't wait for the client's close_notify, and a full send buffer just skips it.
    */
    ERR_clear_error();
    SSL_shutdown(c->ssl.s);
    ts_ssl_session_close(&c->ssl);
  }
#endif /* USE_SSL */

  if (shutdown(c->fd, SHUT_WR)) {
    client_close(c);
    return;
  }

  c->state = CS_LINGER;
  c->want_write = 0;
  c->deadline = monotonic_ms() + LINGER_TIMEOUT_MS;
}

static void client_discard(ts_client_t *c)
{
  int i;
  ssize_t rv;

  /* Bounded, so a client flooding data cannot keep the loop here. */
  for (i = 0; i < 16; i++) {
    rv = recv(c->fd, c->buf, sizeof(c->buf), MSG_DONTWAIT);
    if (rv > 0) continue;
    if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
    client_close(c);
    return;
  }
}

static void client_write(ts_client_t *c)
{
  ps_connection_t *connection = &c->connection;
  long budget = WRITE_BUDGET;
  off_t offset, remaining;
  int rv;

  /* Response header, or the TLS alert on a plain socket. */
  while (!c->header_sent) {
    rv = client_send(c, c->buf + c->buf_pos, c->buf_len - c->buf_pos, connection->length > 0 ? MSG_MORE : 0);
    if (rv == IO_AGAIN) return;
    if (rv <= 0) {
      client_close(c);
      return;
    }
    c->buf_pos += rv;
    c->deadline = monotonic_ms() + SEND_TIMEOUT_MS;
    if (c->buf_pos == c->buf_len) {
      c->header_sent = 1;
      c->buf_pos = c->buf_len = 0;
    }
  }

  /* Body: a file or a constant buffer, a bounded amount per round so other connections get their turn. */
  if (connection->filefd >= 0 || connection->str) {
    while (c->body_sent < connection->length) {
      if (budget <= 0) {
        c->want_write = 1;
        return;
      }

      remaining = connection->length - c->body_sent;

      if (connection->filefd < 0) {
        /* Constant buffer: SSL_write retries need the same length, so always send the rest. */
        rv = client_send(c, connection->str + c->body_sent, (int)remaining, 0);
      }
#ifdef USE_SSL
      else if (CLIENT_TLS(c)) {
        /* Read the next chunk once the previous one is sent; SSL_write retries reuse it as is. */
        if (c->buf_pos == c->buf_len) {
          rv = (int)pread(connection->filefd, c->buf, remaining > (off_t)sizeof(c->buf) ? sizeof(c->buf) : (size_t)remaining, c->body_sent);
          if (rv <= 0) {
            /* Read error, or the file shrank since it was opened. */
            client_close(c);
            return;
          }
          c->buf_len = rv;
          c->buf_pos = 0;
        }
        rv = client_send(c, c->buf + c->buf_pos, c->buf_len - c->buf_pos, 0);
        if (rv > 0) c->buf_pos += rv;
      }
#endif /* USE_SSL */
      else {
        offset = c->body_sent;
        rv = (int)sendfile(c->fd, connection->filefd, &offset, (size_t)(remaining > budget ? budget : remaining));
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
          c->want_write = 1;
          rv = IO_AGAIN;
        }
      }

      if (rv == IO_AGAIN) return;
      if (rv <= 0) {
        /* Send error, or sendfile() found the file shorter than when it was opened. */
        client_close(c);
        return;
      }

      c->body_sent += rv;
      budget -= rv;
      c->deadline = monotonic_ms() + SEND_TIMEOUT_MS;
    }
  }

  client_linger(c);
}

/* Parse the request that has been read, prepare the response and start sending it. */
static void client_respond(ts_client_t *c)
{
  ps_connection_t *connection = &c->connection;
  int http_error, yes;
  unsigned int index;
  char content_length[21];

  DEBUG_PRINT("Received %d bytes on socket %d.", c->buf_len, c->fd);

  /*
  Cork only the response, so header and body go out in full segments; the shutdown at the end
  flushes the rest. Corking earlier would hold back the small TLS handshake messages for up to
  200 ms each.
  */
  yes = 1;
  setsockopt(c->fd, IPPROTO_TCP, TCP_CORK, (void *)&yes, sizeof(yes));

  if (!CLIENT_TLS(c) && c->buf[0] == 0x16) {
    /* TLS handshake on a plain socket: answer with an alert instead of an HTTP response. */
    memcpy(c->buf, content_noSSL, sizeof(content_noSSL) - 1);
    c->buf_len = sizeof(content_noSSL) - 1;
  }
  else {
    http_error = 0;
    /* Header fills the whole buffer without ending: too large to handle. */
    if (c->buf_len == sizeof(c->buf) - 1 && !strstr(c->buf, "\r\n\r\n")) {
      http_error = 400;
    }
    else {
      http_header_parse(&c->request, c->buf, &http_error);
    }

    c->response.version = HTTP_VERSION_10;
    http_header_setvalue(&c->response, HEADER_CONNECTION, "close");

    /* A POST body, like any other unread input, is discarded by the lingering close. */
    if (!http_error) serve(connection, c->sock, &http_error);

    /* Free request header structures. */
    free(c->request.filename);
    c->request.filename = NULL;

    if (http_error) c->response.status_code = http_error;

    if (connection->length > -1 && c->response.status_code != 204) {
      snprintf(content_length, sizeof(content_length), "%lld", (long long)connection->length);
      if (http_header_setvalue(&c->response, HEADER_CONTENT_LENGTH, content_length) < 0) {
        c->response.status_code = 500;
        connection->length = 0;
        connection->str = NULL;
        if (connection->filefd >= 0) {
          close(connection->filefd);
          connection->filefd = -1;
        }
      }
    }

    /* The request is no longer needed, so the buffer now holds the response header. */
    c->buf_len = http_header_fill(&c->response, c->buf, sizeof(c->buf));
    DEBUG_PRINT("Header length: %d.", c->buf_len);

    /* Free response header structures. */
    for (index = 0; index < HTTP_HEADER_FIELDS; index++) {
      free(c->response.field[index]);
      c->response.field[index] = NULL;
    }

    if (c->buf_len < 0) {
      client_close(c);
      return;
    }
  }

  c->state = CS_WRITE;
  c->want_write = 1;
  c->buf_pos = 0;
  c->header_sent = 0;
  c->body_sent = 0;
  c->deadline = monotonic_ms() + SEND_TIMEOUT_MS;
  client_write(c);
}

/*
Read until the end of the request header (empty line), the buffer is full or the peer stops
sending. A request may arrive in several TCP segments or TLS records. The buffer keeps room
for a terminating NUL.
*/
static void client_read(ts_client_t *c)
{
  int rv, scan;

  for (;;) {
    if (c->buf_len >= (int)sizeof(c->buf) - 1) {
      client_respond(c);
      return;
    }

    rv = client_recv(c, c->buf + c->buf_len, sizeof(c->buf) - 1 - c->buf_len);
    if (rv == IO_AGAIN) return;
    if (rv <= 0) {
      /* Peer closed or failed: answer what we have, if anything. */
      if (c->buf_len > 0) client_respond(c);
      else client_close(c);
      return;
    }

    /* Search only the new data, plus the tail of a terminator split across reads. */
    scan = c->buf_len > 3 ? c->buf_len - 3 : 0;
    c->buf_len += rv;
    c->buf[c->buf_len] = 0;

    /* TLS handshake on a plain socket, answered by client_respond(). */
    if ((!CLIENT_TLS(c) && c->buf[0] == 0x16) || strstr(c->buf + scan, "\r\n\r\n")) {
      client_respond(c);
      return;
    }
  }
}

static void client_start_read(ts_client_t *c)
{
  c->state = CS_READ;
  c->want_write = 0;
  c->deadline = monotonic_ms() + REQUEST_READ_TIMEOUT_MS;
  client_read(c);
}

#ifdef USE_SSL
static void client_handshake(ts_client_t *c)
{
  int rv;

  ERR_clear_error();
  rv = SSL_accept(c->ssl.s);
  if (rv == 1) {
    client_start_read(c);
    return;
  }
  if (client_ssl_result(c, rv) == IO_AGAIN) return;

  client_close(c);
}
#endif /* USE_SSL */

/* Take over an accepted, non-blocking socket into a free slot and start handling it. */
int connection_new(ts_client_t *c, ts_socket_t *sock, int fd)
{
  memset(c, 0, sizeof(*c));
  c->fd = fd;
  c->sock = sock;
  c->connection.str = NULL;
  c->connection.length = -1;
  c->connection.filefd = -1;
  c->connection.request = &c->request;
  c->connection.response = &c->response;

  DEBUG_PRINT("Handling socket %d.", fd);

#ifdef USE_SSL
  if (CLIENT_TLS(c)) {
    c->ssl.cert_path = sock->cert_path;
    if (ts_ssl_session_init(&c->ssl, fd) < 0) {
      client_close(c);
      return -1;
    }
    c->state = CS_HANDSHAKE;
    c->deadline = monotonic_ms() + REQUEST_READ_TIMEOUT_MS;
    client_handshake(c);
    return 0;
  }
#endif /* USE_SSL */

  client_start_read(c);
  return 0;
}

/* Add the connection's socket to the sets to wait on, and shorten the wait to its deadline. */
void connection_fdset(ts_client_t *c, fd_set *readfds, fd_set *writefds, int *maxfd, long *wait_ms)
{
  long long left;

  if (c->state == CS_FREE) return;

  FD_SET(c->fd, c->want_write ? writefds : readfds);
  if (c->fd > *maxfd) *maxfd = c->fd;

  left = c->deadline - monotonic_ms();
  if (left < 0) left = 0;
  if (left < *wait_ms) *wait_ms = left;
}

/* Advance the connection if its socket is ready, and end it once its stage runs out of time. */
void connection_process(ts_client_t *c, fd_set *readfds, fd_set *writefds)
{
  if (c->state == CS_FREE) return;

  if (FD_ISSET(c->fd, readfds) || FD_ISSET(c->fd, writefds)) {
    switch (c->state) {
#ifdef USE_SSL
      case CS_HANDSHAKE:
        client_handshake(c);
        break;
#endif /* USE_SSL */
      case CS_READ:
        client_read(c);
        break;
      case CS_WRITE:
        client_write(c);
        break;
      case CS_LINGER:
        client_discard(c);
        break;
      default:
        break;
    }
  }

  if (c->state != CS_FREE && monotonic_ms() >= c->deadline) {
    DEBUG_PRINT("Socket %d timed out in state %d.", c->fd, c->state);
    /* Out of time while reading: answer what has arrived (usually 400), as before. */
    if (c->state == CS_READ && c->buf_len > 0) client_respond(c);
    else client_close(c);
  }
}

void connection_abort(ts_client_t *c)
{
  if (c->state != CS_FREE) client_close(c);
}
