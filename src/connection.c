#include "connection.h"
#include "mime.h"
#include "utils.h"
#include "ssl.h"

#include <arpa/inet.h>	/* recv(), send(), SOL_SOCKET */
#include <errno.h> /* errno */
#include <fcntl.h> /* O_RDONLY */
#include <netinet/tcp.h> /* TCP_CORK */
#include <stdio.h>
#include <stdlib.h> /* free(), malloc() */
#include <string.h> /* strcasestr() */
#include <sys/sendfile.h> /* sendfile() */
#include <sys/stat.h> /* struct stat */
#include <unistd.h> /* close() */

static const mime_t *get_mime(char *ext) {

  const mime_t *ar_mimes;
  unsigned int i;

  if ( ext && *ext == '.' ) {

    /* Skip the dot and compare only characters. */
    ext++;

    /* Load first mime from array. */
    i = 0;
    ar_mimes = &default_mimes[0];

    while ( ar_mimes[i].ext ) {
      if ( !strncasecmp(ext, ar_mimes[i].ext, ar_mimes[i].ext_len) ) {
        return &ar_mimes[i];
      }
      i++;
    }

    /* Return last mime: SEND_UNK_EXT. */
    return &ar_mimes[i];

  }

  /* Return no mime: SENT_NO_EXT. */
  return &no_mime;
}

static int handle_file(ps_connection_t *connection, ts_socket_t *sock, const char *filename) {

  char file[MAX_PATH_LENGTH];
  char *hostname;
  unsigned int hostname_length, filename_length, length;
  int fd;
  struct stat st;

  if ( *(filename + 1) == 0 )
    return -1;

  hostname = http_header_getvalue(connection->request, HEADER_HOSTNAME);
  if ( hostname == NULL )
    return -1;
  if ( strchr(hostname, '/' ) != NULL || *hostname == '.' || *hostname == 0 ) {
    free(hostname);
    return -1;
  }

  hostname_length = find_delimiter(hostname, NULL, ":");
  if ( hostname_length > 0 ) {
    hostname[hostname_length] = 0;
    hostname_length--;
  }
  else
    hostname_length = strlen(hostname);

  if ( change_dots_to_underscore(hostname) < 0 ) {
    free(hostname);
    connection->response->status_code = 400;
    return -1;
  }

  filename_length = strlen(filename);

  length = 1; /* NUL terminator */
  if ( sock->serve_path_length > 0 ) {
    length += sock->serve_path_length;
    if ( *(sock->serve_path + sock->serve_path_length - 1) != '/' )
      length++;
  }
  length += hostname_length;
  length += filename_length;

  if ( length > sizeof(file) )
    return -1;
  file[0] = 0;

  /* Concatenate file. */
  if ( sock->serve_path_length > 0 ) {
    strncpy(file, sock->serve_path, sock->serve_path_length);
    *(file + sock->serve_path_length) = 0;
    if ( *(sock->serve_path + sock->serve_path_length - 1) != '/' )
      strcat(file, "/");
  }
  strcat(file, hostname);
  strcat(file, filename);

  free(hostname);

  DEBUG_PRINT("Requested local file: %s", file);

  /* Check if file exists and it is a regular file. */
  fd = 0;
  if ( stat(file, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG ) {
    connection->length = st.st_size;
    fd = open(file, O_RDONLY);
  }

  if ( fd ) {
    connection->response->status_code = 200;
    switch ( connection->request->method ) {
      case HTTP_METHOD_GET:
        connection->filefd = fd;
        return 0;
      case HTTP_METHOD_HEAD:
        close(fd);
        return 0;
      default:
        close(fd);
    }
    connection->response->status_code = 501;
    return -1;
  }
  else
    connection->response->status_code = 403;

  return -1;
}

static int handle_redirect(ps_connection_t *connection) {

  char *query, *referer, *url;
  char *decoded_query;

  query = connection->request->query;
  if ( *query == 0 )
    return -1;

  DEBUG_PRINT("Query: %s.", query);

  if ( !strcasestr(query, "=http") && !strcasestr(query, "\x3dhttp") && !strcasestr(query, "%5Cx3dhttp") )
    return -1;

   /* Decoded string could be shorter, but +1 for termination. */
  decoded_query = malloc((strlen(query) + 1) * sizeof(char));
  decode_url(decoded_query, query);

  /* Double decode */
  decode_url(decoded_query, decoded_query);

  url = strstr_last(decoded_query, "http://");
  if ( url == NULL ) {
    url = strstr_last(decoded_query, "https://");
  }

  if ( url ) {
    referer = http_header_getvalue(connection->request, HEADER_REFERER);
    if ( referer != NULL ) {
      if ( strstr(referer, url) && !strstr(referer, "adurl") )
        url = NULL;
      free(referer);
    }
  }

  if ( url ) {
    http_header_setvalue(connection->response, HEADER_LOCATION, url);
    connection->response->status_code = 307;
  }

  free(decoded_query);

  return ( url ) ? 0 : -1;
}

static int handle_jsclose(ps_connection_t *connection, const mime_t *mime) {

  int cont;
  char *accept;

  cont = 0;

  /* We return JSCLOSE if file extension is htm. */
  if ( mime->ext && !strcmp(mime->ext, "htm") )
    cont = 1;
  /* Or if Mimetype is unknown. */
  else if ( mime->ext_len == 0 ) {
    accept = http_header_getvalue(connection->request, HEADER_ACCEPT);
    if ( accept != NULL ) {
      if ( strcmp(accept, "\x2A\x2F\x2A") ) /*  * / *  */
        cont = 1;
      free(accept);
    }
  }

  if ( cont == 0 )
    return -1;

  http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, "text/html");
  connection->response->status_code = 200;
  connection->length = sizeof(content_jsclose) - 1;
  if ( connection->request->method == HTTP_METHOD_GET )
    connection->str = content_jsclose;
  return 0;
}

static int serve(ps_connection_t *connection, ts_socket_t *sock, int *error) {

  char *filename, *ext;
  const mime_t *mime;

  if ( connection->request->method != HTTP_METHOD_GET && connection->request->method != HTTP_METHOD_HEAD ) {
    *error = 501;
    return -1;
  }

  filename = connection->request->filename;
  if ( filename == NULL ) {
    *error = 400;
    return -1;
  }

  ext = strrchr(filename, '.');
  mime = get_mime(ext);

  http_header_setvalue(connection->response, HEADER_CONTENT_TYPE, mime->typestr);

  if ( sock->serve_path_length > 0 && is_safe_filename(filename) ) {
    if ( !handle_file(connection, sock, filename) )
      return 0;
  }

  if ( sock->options & DO_204 ) {
    /* HTTP 204 No Content for Google generate_204 URLs. */
    if ( !strcasecmp(filename, "/generate_204") || !strcasecmp(filename, "/gen_204") ) {
      connection->response->status_code = 204;
      return 0;
    }
  }

  if ( sock->options & DO_REDIRECT ) {
    /* HTTP 307 Temporary Redirect for click counters. */
    if ( !handle_redirect(connection) )
      return 0;
  }

  if ( sock->options & DO_CLOSE ) {
    if ( !handle_jsclose(connection, mime) )
      return 0;
  }

  /* Dummy file. */
  if ( mime->response ) {
    if ( connection->request->method == HTTP_METHOD_GET )
      connection->str = mime->response;
    connection->length = mime->response_size;
  }
  else {
    connection->length = 0;
  }
  connection->response->status_code = 200;

  return 0;
}

static int connection_read(ts_socket_t *sock, struct ts_ssl *ssl, int fd, char *buf, int buflen) {

#ifdef USE_SSL
  int rv;

  if ( sock->options & DO_SSL ) {

    ssl->cert_path = sock->cert_path;

    rv = ts_ssl_session_init(ssl, fd);
    if ( rv > 0 )
      return SSL_read(ssl->s, buf, buflen);
    else
      return -1;

  }
#endif /* USE_SSL */

  return recv(fd, buf, buflen, 0);
}

static int connection_write(ts_socket_t *sock, struct ts_ssl *ssl, int fd, ps_connection_t *connection) {

  char response_buffer[CHAR_BUF_SIZE];
  int response_buffer_size;
  int size;
  char content_length[8];
  off_t offset;
  int flags;

  if ( connection->length > -1 ) {
    sprintf(content_length, "%d", connection->length);
    http_header_setvalue(connection->response, HEADER_CONTENT_LENGTH, content_length);
  }

  flags = MSG_NOSIGNAL;
  if ( connection->length > 0 )
    flags |= MSG_MORE;

  /* Output header. */
  response_buffer_size = http_header_fill(connection->response, response_buffer, sizeof(response_buffer) - 1);
  DEBUG_PRINT("Header length: %d.", response_buffer_size);
  if ( response_buffer_size < 0 )
    return -1;

#ifdef USE_SSL
  if ( sock->options & DO_SSL )
    size = SSL_write(ssl->s, response_buffer, response_buffer_size);
  else
#endif /* USE_SSL */
    size = send(fd, response_buffer, response_buffer_size, flags);

  if ( connection->filefd ) {

#ifdef USE_SSL
    if ( sock->options & DO_SSL ) {
      ts_ssl_sendfile(ssl->s, connection->filefd, connection->length);
    }
    else
#endif /* USE_SSL */
    {
      /* Output file hander content. */
      offset = 0;
      size += sendfile(fd, connection->filefd, &offset, connection->length);
    }

    close(connection->filefd);
  }
  else if ( connection->str ) {
#ifdef USE_SSL
    /* Output constant buffer. */
    if ( sock->options & DO_SSL )
      size += SSL_write(ssl->s, connection->str, connection->length);
    else
#endif /* USE_SSL */
      size += send(fd, connection->str, connection->length, MSG_NOSIGNAL);
  }

  return size;
}

int connection_new(ts_socket_t *sock, int fd) {

  int yes;
  int http_error;
  unsigned int index;
  struct timeval timeout;
  ps_http_request_header_t request_header;
  ps_http_response_header_t response_header;
  char request_buffer[CHAR_BUF_SIZE];
  int request_buffer_size;
  struct ts_ssl ssl;
  ps_connection_t connection;

  /*
  The socket is connected, but we need to perform a check for incoming data.
  Since we're using blocking checks, we first want to set a timeout.
  */
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  if ( setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval)) < 0 ) {
    return -1;
  }

  yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_CORK, (void *)&yes, sizeof(yes));

  /* Create new connection. */
  connection.str = NULL;
  connection.length = -1;
  connection.filefd = 0;
  connection.request = &request_header;
  connection.response = &response_header;

  DEBUG_PRINT("Reading from socket %d.", fd);

  request_buffer_size = connection_read(sock, &ssl, fd, request_buffer, sizeof(request_buffer) - 1);
  if ( request_buffer_size < 0 && errno != 0 ) {
     DEBUG_PRINT("Received errno: %d", errno);
  }

  if ( request_buffer_size > 0 ) {

    DEBUG_PRINT("Received %d bytes.", request_buffer_size);

    if ( request_buffer[0] == 0x16 ) {
      send(fd, content_noSSL, sizeof(content_noSSL) - 1, MSG_NOSIGNAL);
    }
    else {
      request_buffer[request_buffer_size] = 0;

      http_error = 0;
      request_header.filename = NULL;
      http_header_parse(&request_header, request_buffer, &http_error);

      if ( request_header.method == HTTP_METHOD_POST ) {
        /* Socket may still be opened for reading, so read any data that is still waiting for us. */
        do {
#ifdef USE_SSL
          if ( sock->options & DO_SSL )
            request_buffer_size = SSL_read(ssl.s, request_buffer, sizeof(request_buffer) - 1);
          else
#endif /* USE_SSL */
            request_buffer_size = recv(fd, request_buffer, sizeof(request_buffer) - 1, 0);
        } while ( request_buffer_size > 0 );
      }

      response_header.version = HTTP_VERSION_10;
      memset(&response_header.field, 0, sizeof(response_header.field));
      http_header_setvalue(&response_header, HEADER_CONNECTION, "close");

      if ( http_error == 0 )
        serve(&connection, sock, &http_error);

      /* Free request header structures. */
      if ( request_header.filename )
        free(request_header.filename);

      if ( http_error != 0 )
        response_header.status_code = http_error;

      connection_write(sock, &ssl, fd, &connection);

      /* Free response header structures. */
      for ( index = 0; index < HTTP_HEADER_FIELDS; index++ )
        if ( response_header.field[index] )
          free(response_header.field[index]);

    }
  }

#ifdef USE_SSL
  if ( sock->options & DO_SSL ) {
    DEBUG_PRINT("Closing down ssl");
    ts_ssl_session_close(&ssl);
  }
#endif

  /* Close the connection. */
  shutdown(fd, SHUT_RDWR);
  close(fd);

  return 0;
}
