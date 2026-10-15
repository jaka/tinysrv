/*
 * tinysrv
 * Copyright (C) 2016, jaka
 */

#include "connection.h"
#include "project.h"
#include "ssl.h"

#ifdef FORK
#include "fork.h"
#endif

#include <arpa/inet.h> /* inet_ntop */
#include <errno.h>
#include <fcntl.h>       /* open(), O_RDWR, F_TLOCK */
#include <grp.h>         /* initgroups() */
#include <netdb.h>       /* freeaddrinfo */
#include <netinet/tcp.h> /* SOL_TCP, TCP_NODELAY */
#include <pwd.h>         /* getpwnam() */
#include <signal.h>      /* sigaction(), sigemptyset(), struct sigaction */
#include <stdio.h>
#include <stdlib.h> /* EXIT_FAILURE */
#include <string.h> /* memset() */
#include <syslog.h> /* openlog(), syslog() */
#include <unistd.h> /* close(), daemon(), fork(), getuid(), setuid(), TEMP_FAILURE_RETRY */

volatile sig_atomic_t terminated;

static const struct {
  unsigned int flag;
  const char *name;
} ts_option_names[] = {
    {DO_204, "204"},
    {DO_REDIRECT, "REDIR"},
    {DO_SSL, "SSL"},
    {DO_CLOSE, "CLOSE"},
    {DO_FILES, "FILES"},
};

static char *ts_options_str(unsigned int options, char *buf, size_t buflen)
{
  unsigned int i;
  size_t len;

  if (!buflen) return buf;

  len = 0;
  buf[0] = 0;
  for (i = 0; i < sizeof(ts_option_names) / sizeof(ts_option_names[0]) && len + 1 < buflen; i++) {
    if (!(options & ts_option_names[i].flag)) continue;
    len += snprintf(buf + len, buflen - len, "%s%s", len ? "|" : "", ts_option_names[i].name);
  }
  if (!len) snprintf(buf, buflen, "none");

  return buf;
}

static int ts_bind(ts_socket_t *sock)
{
  struct addrinfo hints, *servinfo, *servinfo_base;
  struct sockaddr_in *ipv4;
  ts_socket_t *cur_sock;
  char optbuf[64];
  const char *port;
  int rv, sockfd, bound, one;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  /* Create sockets. */
  bound = 0;
  one = 1;
  for (cur_sock = sock; cur_sock; cur_sock = cur_sock->next) {

    hints.ai_flags = !cur_sock->ipaddr ? AI_PASSIVE : 0;
    port = cur_sock->port ? cur_sock->port : DEFAULT_PORT;

    servinfo_base = NULL;
    rv = getaddrinfo(cur_sock->ipaddr, port, &hints, &servinfo_base);
    if (rv) {
      syslog(LOG_ERR, "getaddrinfo: %s.", gai_strerror(rv));
      continue;
    }

    for (servinfo = servinfo_base; servinfo; servinfo = servinfo->ai_next) {
      if (!cur_sock->ipaddr) {
        cur_sock->ipaddr = malloc(INET_ADDRSTRLEN);
        if (!cur_sock->ipaddr) break;
        ipv4 = ((struct sockaddr_in *)servinfo->ai_addr);
        if (!inet_ntop(servinfo->ai_family, &(ipv4->sin_addr), cur_sock->ipaddr, INET_ADDRSTRLEN)) {
          free(cur_sock->ipaddr);
          cur_sock->ipaddr = NULL;
          continue;
        }
      }

      sockfd = socket(servinfo->ai_family, servinfo->ai_socktype | SOCK_NONBLOCK, servinfo->ai_protocol);
      if (sockfd < 0) {
        syslog(LOG_ERR, "socket: %m - %s:%s", cur_sock->ipaddr, port);
        continue;
      }

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)) ||
#ifdef SO_REUSEPORT
          setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(int)) ||
#endif
          setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &one, sizeof(int)) ||
          bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) ||
          listen(sockfd, TS_BACKLOG)) {
        close(sockfd);
        syslog(LOG_ERR, "Abort: %m - %s:%s", cur_sock->ipaddr, port);
        continue;
      }

      cur_sock->sockfd = sockfd;
      bound++;
      syslog(LOG_INFO, "Listening on address %s and port %s with options: %s.", cur_sock->ipaddr, port, ts_options_str(cur_sock->options, optbuf, sizeof(optbuf)));
      break;
    }

    freeaddrinfo(servinfo_base);
  }

  DEBUG_PRINT("Listening on %d sockets.", bound);
  return bound ? 0 : -1;
}

static int ts_listen(ts_socket_t *sock)
{
  int nfds, sockfd, select_rv;
  ts_socket_t *cur_sock, *ready;
  fd_set readfds, selectfds;

  nfds = 0;
  FD_ZERO(&readfds);
  for (cur_sock = sock; cur_sock; cur_sock = cur_sock->next) {
    if (cur_sock->sockfd < 0) continue;
    FD_SET(cur_sock->sockfd, &readfds);
    if (cur_sock->sockfd > nfds) nfds = cur_sock->sockfd;
  }

#ifdef FORK
  if (subprocess_death_fd >= 0) {
    FD_SET(subprocess_death_fd, &readfds);
    if (subprocess_death_fd > nfds) nfds = subprocess_death_fd;
  }
#endif

  nfds++;

  select_rv = 0;
  while (!terminated) {

    /* Call select() only if we have something more to process. */
    if (select_rv <= 0) {
      DEBUG_PRINT("Waiting for select.");

      selectfds = readfds;
      select_rv = select(nfds, &selectfds, NULL, NULL, NULL);

      if (select_rv < 0) {
        if (!terminated) {
          syslog(LOG_ERR, "Child select() returned error: %m.");
          exit(EXIT_FAILURE);
        }
      }
      else if (select_rv == 0) {
        /* Since we did not specify timeout, this should never happen. */
        syslog(LOG_WARNING, "Child select() returned zero.");
        continue;
      }
    }

#ifdef FORK
    if (subprocess_death_fd >= 0 && FD_ISSET(subprocess_death_fd, &selectfds)) {
      DEBUG_PRINT("Supervisor exited, shutting down worker.");
      terminated = 1;
      continue;
    }
#endif

    ready = NULL;
    for (cur_sock = sock; cur_sock; cur_sock = cur_sock->next) {
      if (cur_sock->sockfd < 0) continue;
      if (FD_ISSET(cur_sock->sockfd, &selectfds)) {
        ready = cur_sock;
        select_rv--;
        FD_CLR(cur_sock->sockfd, &selectfds);
        break;
      }
    }

    /* At this point select() should return socket handler, ie. ready != NULL. */
    if (!ready) {
      /* This is bad case since select() will never be called again. */
      syslog(LOG_WARNING, "Child select() returned a value of %d, but no file descriptors of interest are ready for read.", select_rv);
      /* Set select_rv = 0 so that select() will be called on the next loop iteration. */
      select_rv = 0;
      continue;
    }

    sockfd = accept(ready->sockfd, NULL, NULL);
    if (sockfd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* Client closed connection before we got a chance to accept it. */
        DEBUG_PRINT("Child accept(): %d", errno);
      }
      else syslog(LOG_WARNING, "Child accept() returned error: %m.");
      continue;
    }

    DEBUG_PRINT("Starting handling socket %d", sockfd);
    connection_new(ready, sockfd);
  }

  return 0;
}

int ts_main_loop(void *arg)
{
  ts_configuration_t *config = (ts_configuration_t *)arg;

  if (ts_bind(config->sock) < 0) {
    syslog(LOG_CRIT, "Cannot bind to ports!");
    return 1;
  }

  /* Change user, but not when we are already running as it. */
  if (config->pw != NULL && getuid() != config->pw->pw_uid) {
    if (initgroups(config->pw->pw_name, config->pw->pw_gid) < 0) {
      syslog(LOG_WARNING, "initgroups %s: %m", config->pw->pw_name);
      return 1;
    }
    if (setgid(config->pw->pw_gid) < 0) {
      syslog(LOG_WARNING, "setgid %d: %m", config->pw->pw_gid);
      return 1;
    }
    if (setuid(config->pw->pw_uid) < 0) {
      syslog(LOG_WARNING, "setuid %d: %m", config->pw->pw_uid);
      return 1;
    }
  }

  /* A user was requested, so refuse to run as anything else. */
  if (config->pw != NULL && getuid() != config->pw->pw_uid) {
    syslog(LOG_ERR, "Not running as user %s, exiting.", config->pw->pw_name);
    return 1;
  }

#ifdef USE_SSL
  /* SSL */
  SSL_library_init();
#endif /* USE_SSL */

  ts_listen(config->sock);

  syslog(LOG_INFO, "Received termination signal. Shutting down server.");

  return 0;
}

static void signal_handler(int signum)
{
  if (signum == SIGINT || signum == SIGTERM) terminated = 1;
}

int main(int argc, char **argv)
{
  int pidfd = -1, pid_locked = 0, ret = EXIT_FAILURE;
  char pid[16];
  uid_t uid;
  struct sigaction sa;
  ts_configuration_t *config = NULL;

  if (argc == 2 && !strcmp(argv[1], "-v")) {
    printf("tinysrv, built with:\n");
#ifdef DEBUG
    printf("* DEBUG\n");
#endif
#ifdef USE_SSL
    printf("* USE_SSL\n");
#endif
    return EXIT_SUCCESS;
  }

  /* Setup signal handler. */
  sa.sa_flags = 0;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
    fprintf(stderr, "ERROR: Signal handler could not be set!\n");
    goto cleanup;
  }

  /* Configuration. */
  terminated = 0;

  config = ts_configuration_create();
  if (!config) {
    fprintf(stderr, "ERROR: Cannot create configuration struct!\n");
    goto cleanup;
  }

  if (ts_configuration_parse(config, argc, argv) < 0) {
    fprintf(stderr, "ERROR: Invalid arguments!\n");
    goto cleanup;
  }

  if (!config->do_foreground && daemon(1, 0)) {
    fprintf(stderr, "ERROR: Failed to daemonize, exiting: %m.\n");
    goto cleanup;
  }

  openlog(PROGRAM_NAME, config->log_option, LOG_DAEMON);

  /* Open lockfile. */
  if (config->pidfile != NULL) {
    pidfd = open(config->pidfile, O_RDWR | O_CREAT, 0644);
    if (pidfd < 0) {
      syslog(LOG_ERR, "ERROR: Could not open PID file %s, exiting.", config->pidfile);
      goto cleanup;
    }
    /* Try to lock file. */
    if (lockf(pidfd, F_TLOCK, 0) < 0) {
      syslog(LOG_ERR, "ERROR: Could not lock PID file %s, exiting.", config->pidfile);
      goto cleanup;
    }
    pid_locked = 1;
  }

  if (pidfd >= 0) {
    int pid_length = snprintf(pid, sizeof(pid), "%d\n", getpid());
    if (ftruncate(pidfd, 0) < 0 || (pid_length > 0 && write(pidfd, pid, pid_length) != pid_length))
      syslog(LOG_WARNING, "Could not write PID file %s: %m", config->pidfile);
  }

  /* Handle things for changing user. */
  uid = 0;
  if (config->user) {
    uid = getuid();
    config->pw = getpwnam(config->user);
    if (!config->pw) {
      syslog(LOG_ERR, "Unknown user \"%s\", exiting.", config->user);
      goto cleanup;
    }
    else if (uid != config->pw->pw_uid && uid) {
      syslog(LOG_WARNING, "Root access is required to change user!");
      goto cleanup;
    }
  }

#ifdef FORK
  subprocess_init();
  subprocess_add(&ts_main_loop, (void *)config);
  subprocess_run();
  subprocess_quit();
#else
  ts_main_loop((void *)config);
#endif

  ret = EXIT_SUCCESS;

cleanup:
  if (pidfd >= 0) {
    if (pid_locked) unlink(config->pidfile);
    close(pidfd);
  }
  if (config) ts_configuration_free(config);
  DEBUG_PRINT("Exit.");
  closelog();
  return ret;
}
