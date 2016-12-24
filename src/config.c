#include "config.h"

#include <stdlib.h> /* free(), malloc() */
#include <string.h>
#include <syslog.h>

static void ts_socket_load_defaults(ts_socket_t *sock) {

  sock->ipaddr = NULL;
  sock->port = NULL;
  sock->options = DO_204 | DO_REDIRECT;
  sock->serve_path_length = 0;
  sock->cert_path = NULL;
}

static ts_socket_t *ts_socket_new(void) {

  ts_socket_t *sock;

  sock = malloc(sizeof(ts_socket_t));
  if ( !sock )
    return NULL;
  ts_socket_load_defaults(sock);
  return sock;
}

static int ts_socket_free(ts_socket_t *sock) {

  if ( !sock )
    return -1;
  if ( sock->ipaddr )
    free(sock->ipaddr);
  if ( sock->port )
    free(sock->port);
  if ( sock->serve_path_length > 0 && sock->serve_path )
    free(sock->serve_path);
  if ( sock->cert_path )
    free(sock->cert_path);
  free(sock);
  return 0;
}

ts_configuration_t *ts_configuration_create(void) {

  ts_configuration_t *config;

  config = malloc(sizeof(ts_configuration_t));
  if ( config == NULL )
    return NULL;

  config->user = NULL;
  config->pidfile = NULL;
  config->do_foreground = 0;
  config->log_option = LOG_PID | LOG_CONS;
  config->sock = NULL;
  config->pw = NULL;

  return config;
}

int ts_configuration_parse(ts_configuration_t *config, int argc, char **argv) {

  int i, error;
  ts_socket_t *cur_socket;

  cur_socket = ts_socket_new();

  /* Process command line arguments. */
  error = 0;
  for ( i = 1; i < argc && error == 0; i++ ) {

    if ( argv[i][0] == '-' ) {

      /* Handle arguments that don't require a subsequent argument. */
      switch ( argv[i][1] ) {

        case '2':
          /* Disable HTTP 204 reply to generate_204 URLs. */
          cur_socket->options &= ~DO_204; continue;

        case 'c':
          /* Return javascript window close script instead of plain response. */
          cur_socket->options |= DO_CLOSE; continue;

        case 'f':
          /* Stay in foreground - don't daemonize. */
          config->do_foreground = 1; config->log_option |= LOG_PERROR; continue;

        case 'R':
          /* Disable redirect to encoded path in tracker links. */
          cur_socket->options &= ~DO_REDIRECT; continue;

        /* No default because we want to move on to the next section and process further. */
      }

      /* Handle arguments that require a subsequent argument. */
      if ( (i + 1) < argc ) {

        /* Increase i to access argument. */
        switch ( argv[i++][1] ) {

          case 'k':
            cur_socket->options |= DO_SSL;
            if ( cur_socket->port )
              free(cur_socket->port);
            cur_socket->port = strdup(argv[i]);
            continue;

          case 'p':
            cur_socket->options &= ~DO_SSL;
            if ( cur_socket->port )
              free(cur_socket->port);
            cur_socket->port = strdup(argv[i]);
            continue;

          case 'P':
            if ( config->pidfile )
              free(config->pidfile);
            config->pidfile = strdup(argv[i]);
            continue;

          case 'u':
            if ( config->user )
              free(config->user);
            config->user = strdup(argv[i]);
            continue;

          case 'S':
            cur_socket->serve_path = strdup(argv[i]);
            /* TODO: Clean and check path. */
            cur_socket->serve_path_length = strlen(cur_socket->serve_path);
            if ( *(cur_socket->serve_path + cur_socket->serve_path_length - 1) != '/' ) {
              free(cur_socket->serve_path);
              error = 1;
            }
            continue;

          case 'C':
            if ( cur_socket->cert_path )
              free(cur_socket->cert_path);
            cur_socket->cert_path = strdup(argv[i]);
            continue;

          default:
            error = 1;
            continue;
        }

      }
      else {
        error = 1;
      }
    }
    else {

      /* Store IP to config_node. */
      if ( cur_socket->ipaddr )
        free(cur_socket->ipaddr);
      cur_socket->ipaddr = strdup(argv[i]);

      if ( cur_socket->port == NULL ) {
        return -1;
      }

      if ( cur_socket->cert_path == NULL )
        cur_socket->options &= ~DO_SSL;

      /* Update global configuration list */
      cur_socket->next = config->sock;
      config->sock = cur_socket;

      /* Create new configuration. */
      cur_socket = ts_socket_new();

    }
  }

  if ( error ) {
    ts_socket_free(cur_socket);
    return -1;
  }

  if ( config->sock == NULL ) {
    if ( cur_socket->port == NULL )
      cur_socket->port = strdup(DEFAULT_PORT);
    cur_socket->next = NULL;
    config->sock = cur_socket;
  }
  else if ( config->sock != cur_socket ) {
    ts_socket_free(cur_socket);
  }

  return 0;
}

void ts_configuration_free(ts_configuration_t *config) {

  ts_socket_t *cur_socket, *next_socket;

  if ( config != NULL ) {
    cur_socket = config->sock;
    while ( cur_socket ) {
      next_socket = cur_socket->next;
      ts_socket_free(cur_socket);
      cur_socket = next_socket;
    }
    if ( config->user != NULL )
      free(config->user);
    if ( config->pidfile != NULL )
      free(config->pidfile);
    free(config);
  }
}
