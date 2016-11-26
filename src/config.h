#ifndef _TINYSRV_CONFIG_H
#define _TINYSRV_CONFIG_H

#include "project.h"

enum ts_socket_option {
  DO_204 = 1,
  DO_CLOSE = 1 << 1,
  DO_REDIRECT = 1 << 2,
  DO_SSL = 1 << 3
};

struct ts_socket {
  int sockfd;
  char* ipaddr;
  char* port;
  unsigned int options;
  unsigned int serve_path_length;
  char *serve_path;
  char *cert_path;
  struct ts_socket *next;
};

typedef struct ts_socket ts_socket_t;

struct ts_configuration {
  char *user;
  char *pidfile;
  int do_foreground;
  int log_option;
  struct passwd *pw;
  ts_socket_t *sock;
};

typedef struct ts_configuration ts_configuration_t;

ts_configuration_t *ts_configuration_create(void);
int ts_configuration_parse(ts_configuration_t *, int, char **);
void ts_configuration_free(ts_configuration_t *);

#endif
