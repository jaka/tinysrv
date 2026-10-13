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
#include <fcntl.h>       /* F_SETFL, F_GETFL, fcntl() */
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

static int ts_bind(ts_socket_t *sock)
{
  struct addrinfo hints, *servinfo, *servinfo_base;
  struct sockaddr_in *ipv4;
  ts_socket_t *cur_sock;
  int flags, rv, sockfd, success, yes;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  /* Create sockets. */
  success = 0;
  yes = 1;
  for (cur_sock = sock; cur_sock; cur_sock = cur_sock->next) {

    hints.ai_flags = !cur_sock->ipaddr ? AI_PASSIVE : 0;

    servinfo_base = NULL;
    rv = getaddrinfo(cur_sock->ipaddr, cur_sock->port, &hints, &servinfo_base);
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

      sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
      if (sockfd < 0) {
        syslog(LOG_ERR, "socket: %m - %s:%s", cur_sock->ipaddr, cur_sock->port);
        continue;
      }

      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ||
#ifdef SO_REUSEPORT
          setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int)) ||
#endif
          setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &yes, sizeof(int)) ||
          bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) ||
          listen(sockfd, TS_BACKLOG)) {
        close(sockfd);
        syslog(LOG_ERR, "Abort: %m - %s:%s", cur_sock->ipaddr, cur_sock->port);
        continue;
      }

      flags = fcntl(sockfd, F_GETFL, 0);
      if (flags < 0) {
        close(sockfd);
        continue;
      }

      if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(sockfd);
        continue;
      }

      cur_sock->sockfd = sockfd;
      success++;
      syslog(LOG_INFO, "Listening on address %s and port %s with options: %d.", cur_sock->ipaddr, cur_sock->port, cur_sock->options);
      break;
    }

    freeaddrinfo(servinfo_base);
  }

  DEBUG_PRINT("Listening on %d sockets.", success);
  return success ? 0 : -1;
}

static int ts_listen(ts_socket_t *sock)
{
  int nfds;
  int sockfd;
  int select_rv;
  ts_socket_t *cur_sock;
  fd_set readfds, selectfds;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;

  nfds = 0;
  FD_ZERO(&readfds);
  for (cur_sock = sock; cur_sock; cur_sock = cur_sock->next) {
    if (cur_sock->sockfd < 0) continue;
    FD_SET(cur_sock->sockfd, &readfds);
    if (cur_sock->sockfd > nfds) nfds = cur_sock->sockfd;
  }

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

    sockfd = 0;
    for (cur_sock = sock; cur_sock; cur_sock = cur_sock->next) {
      if (cur_sock->sockfd < 0) continue;
      if (FD_ISSET(cur_sock->sockfd, &selectfds)) {
        sockfd = cur_sock->sockfd;
        select_rv--;
        FD_CLR(cur_sock->sockfd, &selectfds);
        break;
      }
    }

    /* At this point select() should return socket handler, ie. cur_fd != 0. */
    if (!sockfd) {
      /* This is bad case since select() will never be called again. */
      syslog(LOG_WARNING, "Child select() returned a value of %d, but no file descriptors of interest are ready for read.", select_rv);
      /* Set select_rv = 0 so that select() will be called on the next loop iteration. */
      select_rv = 0;
      continue;
    }

    sin_size = sizeof(struct sockaddr_storage);
    sockfd = accept(cur_sock->sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if (sockfd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* Client closed connection before we got a chance to accept it. */
        DEBUG_PRINT("Child accept(): %d", errno);
      }
      else syslog(LOG_WARNING, "Child accept() returned error: %m.");
      continue;
    }

    DEBUG_PRINT("Starting handling socket %d", sockfd);
    connection_new(cur_sock, sockfd);
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

  /* Change user. */
  if (config->pw != NULL) {
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
  int pidfd;
  char pid[16];
  uid_t uid;
  struct sigaction sa;
  ts_configuration_t *config;

  if (argc == 2 && !strcmp(argv[1], "-v")) {
    printf("tinysrv, built with:\n");
#if DEBUG
    printf("* DEBUG\n");
#endif
#if USE_SSL
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
    exit(EXIT_FAILURE);
  }

  /* Configuration. */
  terminated = 0;

  config = ts_configuration_create();
  if (!config) {
    fprintf(stderr, "ERROR: Cannot create configuration struct!\n");
    exit(EXIT_FAILURE);
  }

  if (ts_configuration_parse(config, argc, argv) < 0) {
    fprintf(stderr, "ERROR: Invalid arguments!\n");
    ts_configuration_free(config);
    exit(EXIT_FAILURE);
  }

  if (!config->do_foreground && daemon(1, 0)) {
    fprintf(stderr, "ERROR: Failed to daemonize, exiting: %m.\n");
    exit(EXIT_FAILURE);
  }

  openlog(PROGRAM_NAME, config->log_option, LOG_DAEMON);

  /* Open lockfile. */
  pidfd = -1;
  if (config->pidfile != NULL) {
    pidfd = open(config->pidfile, O_RDWR | O_CREAT, 0644);
    if (pidfd < 0) {
      syslog(LOG_ERR, "ERROR: Could not open PID file %s, exiting.", config->pidfile);
      exit(EXIT_FAILURE);
    }
    /* Try to lock file. */
    if (lockf(pidfd, F_TLOCK, 0) < 0) {
      syslog(LOG_ERR, "ERROR: Could not lock PID file %s, exiting.", config->pidfile);
      exit(EXIT_FAILURE);
    }
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
      exit(EXIT_FAILURE);
    }
    else if (uid != config->pw->pw_uid && uid) {
      syslog(LOG_WARNING, "Root access is required to change user!");
      exit(EXIT_FAILURE);
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

  if (pidfd >= 0) {
    close(pidfd);
    unlink(config->pidfile);
  }

  ts_configuration_free(config);

  DEBUG_PRINT("Exit.");
  return 0;
}
