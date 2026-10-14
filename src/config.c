#include "config.h"

#include <stdlib.h> /* free(), malloc() */
#include <string.h>
#include <syslog.h>

static void ts_socket_load_defaults(ts_socket_t *sock)
{
  sock->sockfd = -1;
  sock->ipaddr = NULL;
  sock->port = NULL;
  sock->options = DO_204 | DO_REDIRECT;
  sock->serve_path_length = 0;
  sock->serve_path = NULL;
  sock->cert_path = NULL;
  sock->next = NULL;
}

static ts_socket_t *ts_socket_new(void)
{
  ts_socket_t *sock;

  sock = malloc(sizeof(ts_socket_t));
  if (!sock) return NULL;
  ts_socket_load_defaults(sock);
  return sock;
}

static int set_string(char **dst, const char *src)
{
  char *copy = strdup(src);
  if (!copy) return -1;
  if (*dst) free(*dst);
  *dst = copy;
  return 0;
}

static int ts_socket_free(ts_socket_t *sock)
{
  if (!sock) return -1;
  if (sock->ipaddr) free(sock->ipaddr);
  if (sock->port) free(sock->port);
  if (sock->serve_path) {
    sock->serve_path_length = 0;
    free(sock->serve_path);
  }
  if (sock->cert_path) free(sock->cert_path);
  free(sock);
  sock = 0;
  return 0;
}

ts_configuration_t *ts_configuration_create(void)
{
  ts_configuration_t *config;

  config = malloc(sizeof(ts_configuration_t));
  if (!config) return NULL;

  config->user = NULL;
  config->pidfile = NULL;
  config->do_foreground = 0;
  config->log_option = LOG_PID | LOG_CONS;
  config->sock = NULL;
  config->pw = NULL;

  return config;
}

int ts_configuration_parse(ts_configuration_t *config, int argc, char **argv)
{
  int i, error;
  ts_socket_t *cur_socket;

  cur_socket = ts_socket_new();
  if (!cur_socket) return -1;

  /* Process command line arguments. */
  error = 0;
  for (i = 1; i < argc && !error; i++) {

    if (argv[i][0] == '-') {

      /* Handle arguments that don't require a subsequent argument. */
      switch (argv[i][1]) {

        case '2':
          /* Disable HTTP 204 reply to generate_204 URLs. */
          cur_socket->options &= ~DO_204;
          continue;

        case 'c':
          /* Return javascript window close script instead of plain response. */
          cur_socket->options |= DO_CLOSE;
          continue;

        case 'f':
          /* Stay in foreground - don't daemonize. */
          config->do_foreground = 1;
          config->log_option |= LOG_PERROR;
          continue;

        case 'R':
          /* Disable redirect to encoded path in tracker links. */
          cur_socket->options &= ~DO_REDIRECT;
          continue;

        case 'F':
          /* Serve only files: 404 instead of an empty placeholder response. */
          cur_socket->options |= DO_FILES;
          continue;

          /* No default because we want to move on to the next section and process further. */
      }

      /* Handle arguments that require a subsequent argument. */
      if ((i + 1) < argc) {
        /* Increase i to access argument. */
        switch (argv[i++][1]) {

          case 'k':
            cur_socket->options |= DO_SSL;
            if (set_string(&cur_socket->port, argv[i]) < 0) error = 1;
            continue;

          case 'p':
            cur_socket->options &= ~DO_SSL;
            if (set_string(&cur_socket->port, argv[i]) < 0) error = 1;
            continue;

          case 'P':
            if (set_string(&config->pidfile, argv[i]) < 0) error = 1;
            continue;

          case 'u':
            if (set_string(&config->user, argv[i]) < 0) error = 1;
            continue;

          case 'S':
            if (set_string(&cur_socket->serve_path, argv[i]) < 0) {
              error = 1;
              continue;
            }
            /* TODO: Clean and check path. */
            cur_socket->serve_path_length = strlen(cur_socket->serve_path);
            if (!cur_socket->serve_path_length || *(cur_socket->serve_path + cur_socket->serve_path_length - 1) != '/') {
              cur_socket->serve_path_length = 0;
              free(cur_socket->serve_path);
              cur_socket->serve_path = NULL;
              error = 1;
            }
            continue;

          case 'C':
            if (set_string(&cur_socket->cert_path, argv[i]) < 0) error = 1;
            continue;

          default:
            error = 1;
            continue;
        }
      }
      else error = 1;
    }
    else {
      /* Store IP to config_node. */
      if (set_string(&cur_socket->ipaddr, argv[i]) < 0) error = 1;

      if (!cur_socket->port) {
        error = 1;
        continue;
      }
      if (!cur_socket->cert_path) cur_socket->options &= ~DO_SSL;

      /* Update global configuration list */
      cur_socket->next = config->sock;
      config->sock = cur_socket;

      /* Create new configuration. */
      cur_socket = ts_socket_new();
      if (!cur_socket) {
        error = 1;
        continue;
      }
    }
  }

  if (error) {
    ts_socket_free(cur_socket);
    return -1;
  }

  if (!config->sock) {
    if (!cur_socket->port) cur_socket->port = strdup(DEFAULT_PORT);
    cur_socket->next = NULL;
    config->sock = cur_socket;
  }
  else if (config->sock != cur_socket) ts_socket_free(cur_socket);

  return 0;
}

void ts_configuration_free(ts_configuration_t *config)
{
  ts_socket_t *cur_socket, *next_socket;

  if (!config) return;

  cur_socket = config->sock;
  while (cur_socket) {
    next_socket = cur_socket->next;
    ts_socket_free(cur_socket);
    cur_socket = next_socket;
  }
  if (config->user != NULL) free(config->user);
  if (config->pidfile != NULL) free(config->pidfile);
  free(config);
}
