#include "connection.h"
#include "mime.h"
#include "ssl.h"
#include "utils.h"

#include <arpa/inet.h>   /* recv(), send(), SOL_SOCKET */
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
      fd = openat(dirfd, p, O_RDONLY | O_NOFOLLOW);
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
  char *hostname, *rel, *port;
  int fd;
  struct stat st;

  if (!*(filename + 1)) return -1;

  hostname = http_header_getvalue(connection->request, HEADER_HOSTNAME);
  if (!hostname) return -1;
  if (strchr(hostname, '/') != NULL || *hostname == '.' || !*hostname) {
    free(hostname);
    return -1;
  }

  port = strchr(hostname, ':');
  if (port) *port = 0;

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
  char *query, *referer, *url, *p;
  char *decoded_query;

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
    http_header_setvalue(connection->response, HEADER_LOCATION, url);
    connection->response->status_code = 307;
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

  http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, "text/html");
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

  http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, mime->typestr);

  if (sock->serve_path_length > 0 && is_safe_filename(filename) && !handle_file(connection, sock, filename)) return 0;

  /* HTTP 307 Temporary Redirect for click counters. */
  if ((sock->options & DO_REDIRECT) && !handle_redirect(connection)) return 0;

  if ((sock->options & DO_CLOSE) && !handle_jsclose(connection, mime)) return 0;

  /* Dummy file. */
  if (mime->response) {
    if (connection->request->method == HTTP_METHOD_GET) connection->str = mime->response;
    connection->length = mime->response_size;
  }
  else connection->length = 0;
  connection->response->status_code = 200;

  return 0;
}

static int connection_recv(ts_socket_t *sock, struct ts_ssl *ssl, int fd, char *buf, int buflen)
{
#ifdef USE_SSL
  if (sock->options & DO_SSL) return SSL_read(ssl->s, buf, buflen);
#else
  (void)sock;
  (void)ssl;
#endif /* USE_SSL */

  return recv(fd, buf, buflen, 0);
}

static int connection_read(ts_socket_t *sock, struct ts_ssl *ssl, int fd, char *buf, int buflen)
{
#ifdef USE_SSL
  if (sock->options & DO_SSL) {
    ssl->cert_path = sock->cert_path;
    if (ts_ssl_session_init(ssl, fd) <= 0) return -1;
  }
#endif /* USE_SSL */

  return connection_recv(sock, ssl, fd, buf, buflen);
}

static int connection_write(ts_socket_t *sock, struct ts_ssl *ssl, int fd, ps_connection_t *connection)
{
  char response_buffer[CHAR_BUF_SIZE];
  int response_buffer_size;
  int size;
  char content_length[21];
  off_t offset;
  int flags;

#ifndef USE_SSL
  (void)sock;
  (void)ssl;
#endif /* USE_SSL */

  if (connection->length > -1) {
    snprintf(content_length, sizeof(content_length), "%lld", (long long)connection->length);
    http_header_setvalue(connection->response, HEADER_CONTENT_LENGTH, content_length);
  }

  flags = MSG_NOSIGNAL;
  if (connection->length > 0) flags |= MSG_MORE;

  /* Output header. */
  response_buffer_size = http_header_fill(connection->response, response_buffer, sizeof(response_buffer));
  DEBUG_PRINT("Header length: %d.", response_buffer_size);
  if (response_buffer_size < 0) {
    if (connection->filefd >= 0) close(connection->filefd);
    return -1;
  }

#ifdef USE_SSL
  if (sock->options & DO_SSL)
    size = SSL_write(ssl->s, response_buffer, response_buffer_size);
  else
#endif /* USE_SSL */
    size = send(fd, response_buffer, response_buffer_size, flags);

  if (size < 0) {
    if (connection->filefd >= 0) close(connection->filefd);
    return -1;
  }

  if (connection->filefd >= 0) {

#ifdef USE_SSL
    if (sock->options & DO_SSL) {
      ts_ssl_sendfile(ssl->s, connection->filefd, connection->length);
    }
    else
#endif /* USE_SSL */
    {
      /* Output file hander content. */
      offset = 0;
      size += sendfile(fd, connection->filefd, &offset, (size_t)connection->length);
    }

    close(connection->filefd);
  }
  else if (connection->str) {
#ifdef USE_SSL
    /* Output constant buffer. */
    if (sock->options & DO_SSL)
      size += SSL_write(ssl->s, connection->str, (int)connection->length);
    else
#endif /* USE_SSL */
      size += send(fd, connection->str, (size_t)connection->length, MSG_NOSIGNAL);
  }

  return size;
}

static void connection_drain(ts_socket_t *sock, struct ts_ssl *ssl, int fd, char *buf, int buflen)
{
  time_t deadline = time(NULL) + POST_DRAIN_TIMEOUT;
  int size;

  do {
    size = connection_recv(sock, ssl, fd, buf, buflen);
  } while (size > 0 && time(NULL) < deadline);
}

int connection_new(ts_socket_t *sock, int fd)
{
  int yes;
  int http_error;
  unsigned int index;
  struct timeval timeout;
  ps_http_request_header_t request_header;
  ps_http_response_header_t response_header;
  char request_buffer[CHAR_BUF_SIZE];
  int request_buffer_size;
  struct ts_ssl ssl = {0};
  ps_connection_t connection;

  /*
  The socket is connected, but we need to perform a check for incoming data.
  Since we're using blocking checks, we first want to set a timeout.
  */
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval)) < 0) {
    close(fd);
    return -1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(struct timeval)) < 0) {
    close(fd);
    return -1;
  }

  yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_CORK, (void *)&yes, sizeof(yes));

  /* Create new connection. */
  memset(&request_header, 0, sizeof(request_header));
  memset(&response_header, 0, sizeof(response_header));
  connection.str = NULL;
  connection.length = -1;
  connection.filefd = -1;
  connection.request = &request_header;
  connection.response = &response_header;

  DEBUG_PRINT("Reading from socket %d.", fd);

  request_buffer_size = connection_read(sock, &ssl, fd, request_buffer, sizeof(request_buffer) - 1);
  if (request_buffer_size < 0 && errno != 0) {
    DEBUG_PRINT("Received errno: %d", errno);
  }

  if (request_buffer_size > 0) {

    DEBUG_PRINT("Received %d bytes.", request_buffer_size);

    if (request_buffer[0] == 0x16) {
      send(fd, content_noSSL, sizeof(content_noSSL) - 1, MSG_NOSIGNAL);
    }
    else {
      request_buffer[request_buffer_size] = 0;

      http_error = 0;
      http_header_parse(&request_header, request_buffer, &http_error);

      if (request_header.method == HTTP_METHOD_POST) {
        /*
        Socket may still be opened for reading, so read any data that is still waiting for us.
        Bound the drain by wall-clock time so a client trickling bytes cannot hold the
        single worker indefinitely.
        */
        connection_drain(sock, &ssl, fd, request_buffer, sizeof(request_buffer) - 1);
      }

      response_header.version = HTTP_VERSION_10;
      http_header_setvalue(&response_header, HEADER_CONNECTION, "close");

      if (!http_error) serve(&connection, sock, &http_error);

      /* Free request header structures. */
      if (request_header.filename) free(request_header.filename);

      if (http_error) response_header.status_code = http_error;

      connection_write(sock, &ssl, fd, &connection);

      /* Free response header structures. */
      for (index = 0; index < HTTP_HEADER_FIELDS; index++)
        if (response_header.field[index]) free(response_header.field[index]);
    }
  }

#ifdef USE_SSL
  if (sock->options & DO_SSL) {
    DEBUG_PRINT("Closing down ssl");
    ts_ssl_session_close(&ssl);
  }
#endif

  /* Close the connection. */
  shutdown(fd, SHUT_RDWR);
  close(fd);

  return 0;
}
