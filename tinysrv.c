#include "project.h"
#include "connection.h"
#include "ssl.h"

#ifdef FORK
#include "fork.h"
#endif

#include <arpa/inet.h> /* inet_ntop */
#include <errno.h>
#include <fcntl.h> /* F_SETFL, F_GETFL, fcntl() */
#include <netdb.h> /* freeaddrinfo */
#include <netinet/tcp.h> /* SOL_TCP, TCP_NODELAY */
#include <pwd.h> /* getpwnam() */
#include <signal.h> /* sigaction(), sigemptyset(), struct sigaction */
#include <stdio.h>
#include <stdlib.h> /* EXIT_FAILURE */
#include <string.h> /* memset() */
#include <syslog.h> /* openlog(), syslog() */
#include <unistd.h> /* close(), daemon(), fork(), getuid(), setuid(), TEMP_FAILURE_RETRY */

volatile int terminated;

static int ts_bind(ts_socket_t *sock) {

  struct addrinfo hints, *servinfo;
  struct sockaddr_in *ipv4;
  ts_socket_t *cur_sock;
  int rv, sockfd, yes;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  /* Create sockets. */
  yes = 1;
  for ( cur_sock = sock; cur_sock; cur_sock = cur_sock->next ) {

    if ( cur_sock->ipaddr == NULL ) {
      hints.ai_flags = AI_PASSIVE;
    }

    rv = getaddrinfo(cur_sock->ipaddr, cur_sock->port, &hints, &servinfo);
    if ( rv != 0 ) {
      syslog(LOG_ERR, "getaddrinfo: %s.", gai_strerror(rv));
      return -1;
    }

    if ( cur_sock->ipaddr == NULL ) {
      cur_sock->ipaddr = malloc(INET6_ADDRSTRLEN * sizeof(char));
      ipv4 = ((struct sockaddr_in *)servinfo->ai_addr);
      inet_ntop(servinfo->ai_family, &(ipv4->sin_addr), cur_sock->ipaddr, INET6_ADDRSTRLEN);
    }

    if ( ((sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 1) ||
         (setsockopt(sockfd, SOL_SOCKET, TS_SOL_SOCKET, &yes, sizeof(int))) ||
         (setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &yes, sizeof(int))) ||
         (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen)) ||
         (listen(sockfd, TS_BACKLOG)) ||
         (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK)) ) {
      syslog(LOG_ERR, "Abort: %m - %s:%s", cur_sock->ipaddr, cur_sock->port);
      exit(EXIT_FAILURE);
    }

    cur_sock->sockfd = sockfd;

    freeaddrinfo(servinfo);

    syslog(LOG_INFO, "Listening on address %s and port %s with options: %d.", cur_sock->ipaddr, cur_sock->port, cur_sock->options);
  }

  return 0;
}

static int ts_listen(ts_socket_t *sock) {

  int nfds;
  int sockfd;
  int select_rv;
  ts_socket_t *cur_sock;
  fd_set readfds, selectfds;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;

  nfds = 0;
  FD_ZERO(&readfds);
  for ( cur_sock = sock; cur_sock; cur_sock = cur_sock->next ) {
    FD_SET(cur_sock->sockfd, &readfds);
    if ( cur_sock->sockfd > nfds )
      nfds = cur_sock->sockfd;
  }

  nfds++;

  select_rv = 0;
  while ( !terminated ) {

    /* Call select() only if we have something more to process. */
    if ( select_rv <= 0 ) {

      DEBUG_PRINT("Waiting for select.");

      selectfds = readfds;
      select_rv = select(nfds, &selectfds, NULL, NULL, NULL);

      if ( select_rv < 0 ) {
        if ( !terminated ) {
          syslog(LOG_ERR, "Child select() returned error: %m.");
          exit(EXIT_FAILURE);
        }
      }
      else if ( select_rv == 0 ) {
        /* Since we did not specify timeout, this should never happen. */
        syslog(LOG_WARNING, "Child select() returned zero.");
        continue;
      }

    }

    sockfd = 0;
    for ( cur_sock = sock; cur_sock; cur_sock = cur_sock->next ) {
      if ( FD_ISSET(cur_sock->sockfd, &selectfds) ) {
        sockfd = cur_sock->sockfd;
        select_rv--;
        break;
      }
    }

    /* At this point select() should return socket handler, ie. cur_fd != 0. */
    if ( !sockfd ) {
      /* This is bad case since select() will never be called again. */
      syslog(LOG_WARNING, "Child select() returned a value of %d, but no file descriptors of interest are ready for read.", select_rv);
      /* Set select_rv = 0 so that select() will be called on the next loop iteration. */
      select_rv = 0;
      continue;
    }

    sin_size = sizeof(struct sockaddr_storage);
    sockfd = accept(cur_sock->sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if ( sockfd < 0 ) {
      if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
        /* Client closed connection before we got a chance to accept it. */
        DEBUG_PRINT("Child accept(): %d", errno);
      }
      else {
        syslog(LOG_WARNING, "Child accept() returned error: %m.");
      }
      continue;
    }

    DEBUG_PRINT("Starting handling socket %d", sockfd);
    connection_new(cur_sock, sockfd);
  }

  return 0;
}

int ts_main_loop(void *arg) {

  ts_configuration_t *config;

  config = (ts_configuration_t *)arg;

  if ( ts_bind(config->sock) < 0 ) {
    syslog(LOG_CRIT, "Cannot bind to ports!");
    return 1;
  }

  /* SSL */
  SSL_library_init();

  /* Change user. */
  if ( config->pw != NULL && setuid(config->pw->pw_uid) ) {
    syslog(LOG_WARNING, "setuid %d: %m", config->pw->pw_uid);
    return 1;
  }

  ts_listen(config->sock);

  return 0;
}

static void signal_handler(int signum) {

  if ( signum == SIGINT || signum == SIGTERM ) {

    syslog(LOG_INFO, "Received termination signal. Shutting down server.");
    terminated = 1;

    if ( signum == SIGTERM )
      signal(SIGTERM, SIG_IGN);
    else if ( signum == SIGINT )
      signal(SIGINT, SIG_IGN);
  }

}

int main(int argc, char **argv) {

  int pidfd;
  char pid[11];
  uid_t uid;
  struct sigaction sa;
  ts_configuration_t *config;

  /* Setup signal handler. */
  sa.sa_flags = 0;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  if ( sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0 ) {
    fprintf(stderr, "ERROR: Signal handler could not be set!\n");
    exit(EXIT_FAILURE);
  }

  /* Configuration. */
  terminated = 0;

  config = ts_configuration_create();
  if ( config == NULL ) {
    fprintf(stderr, "ERROR: Cannot create configuration struct!\n");
    exit(EXIT_FAILURE);
  }

  if ( ts_configuration_parse(config, argc, argv) < 0 ) {
    fprintf(stderr, "ERROR: Invalid arguments!\n");
    exit(EXIT_FAILURE);
  }

  if ( !config->do_foreground && daemon(1, 0) ) {
    fprintf(stderr, "ERROR: Failed to daemonize, exiting: %m.\n");
    exit(EXIT_FAILURE);
  }

  openlog(PROGRAM_NAME, config->log_option, LOG_DAEMON);

  /* Open lockfile. */
  pidfd = 0;
  if ( config->pidfile != NULL ) {
    pidfd = open(config->pidfile, O_RDWR | O_CREAT, 0644);
    if ( pidfd < 0 ) {
      syslog(LOG_ERR, "ERROR: Could not open PID file %s, exiting.", config->pidfile);
      exit(EXIT_FAILURE);
    }
    /* Try to lock file. */
    if ( lockf(pidfd, F_TLOCK, 0) < 0 ) {
      syslog(LOG_ERR, "ERROR: Could not lock PID file %s, exiting.", config->pidfile);
      exit(EXIT_FAILURE);
    }
  }

  if ( pidfd > 0 ) {
    sprintf(pid, "%d\n", getpid());
    write(pidfd, pid, strlen(pid));
  }

  /* Handle things for changing user. */
  uid = 0;
  if ( config->user != NULL ) {
    uid = getuid();
    config->pw = getpwnam(config->user);
    if ( config->pw == NULL ) {
      syslog(LOG_WARNING, "Unknown user \"%s\"", config->user);
    }
    else if ( uid != config->pw->pw_uid && uid != 0 ) {
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

  if ( pidfd > 0 ) {
    close(pidfd);
    unlink(config->pidfile);
  }

  ts_configuration_free(config);

  DEBUG_PRINT("Exit.");
  return 0;
}
