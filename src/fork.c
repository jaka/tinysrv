#include "fork.h"
#include "project.h"
#include "debug.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h> /* EXIT_FAILURE */
#include <sys/wait.h> /* waitpid() */
#include <syslog.h> /* openlog(), syslog() */
#include <unistd.h> /* fork() */

struct fork_process_s *process_list;

int subprocess_init(void) {

  process_list = NULL;
  return 0;
}

int subprocess_quit(void) {

  struct fork_process_s *cur_process;

  while ( process_list ) {
    cur_process = process_list;
    process_list = cur_process->next;
    if ( cur_process->pid > 0 ) {
      kill(cur_process->pid, SIGTERM);
    }
    free(cur_process);
  }
  return 0;
}

int subprocess_add(int (*fn)(void *), void *arg) {

  struct fork_process_s *new_process;

  new_process = malloc(sizeof(struct fork_process_s));
  if ( new_process == NULL )
    return -1;

  new_process->pid = 0;
  new_process->fn = fn;
  new_process->arg = arg;

  new_process->next = process_list;
  process_list = new_process;

  return 0;
}

static void subprocess_signal_handler(int signum) {

  if ( signum == SIGTERM ) {
    syslog(LOG_INFO, "Received termination signal. Shutting down subprocess.");
    terminated = 1;
  }
}

static int subprocess_setup_signal_handler(void) {

  struct sigaction sa;

  sa.sa_flags = 0;
  sa.sa_handler = subprocess_signal_handler;
  sigemptyset(&sa.sa_mask);

  sa.sa_flags = SA_RESTART;
  if ( sigaction(SIGSEGV, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0 ) {
    syslog(LOG_WARNING, "Signal handler could not be set: %m.");
    return -1;
  }

  return 0;
}

int subprocess_run(void) {

  struct fork_process_s *cur_process;
  int new_pid, status;
  pid_t pid;

  for (;;) {

    for ( cur_process = process_list; cur_process; cur_process = cur_process->next ) {

      if ( cur_process->pid != 0 )
        continue;

      /* Start subprocess. */
      new_pid = fork();
      if ( new_pid == 0 ) {

        signal(SIGINT, SIG_IGN);
        if ( subprocess_setup_signal_handler() < 0 ) {
          exit(EXIT_FAILURE);
        }

        cur_process->fn(cur_process->arg);

        exit(EXIT_SUCCESS);

      }
      else if ( new_pid > 0 ) {
        cur_process->pid = new_pid;
        syslog(LOG_INFO, "Created new subprocess with PID %ld.", (long)new_pid);
      }

    }

    /* Wait for any subprocesses to exit. */
#ifndef WAIT_ANY
 #define WAIT_ANY -1
#endif
    pid = waitpid(WAIT_ANY, &status, 0);

    syslog(LOG_INFO, "Subprocess with PID %ld exited with status 0x%04x.", (long)pid, status);

    if ( pid == -1 || ( WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE ) ) {
        terminated = 1;
    }
    else {
      for ( cur_process = process_list; cur_process; cur_process = cur_process->next )
        if ( cur_process->pid == pid ) {
          cur_process->pid = 0;
          DEBUG_PRINT("Subprocess scheduled for restart.");
        }
    }

    if ( terminated ) {
      DEBUG_PRINT("Kill all running subprocesses.");
      for ( cur_process = process_list; cur_process; cur_process = cur_process->next )
        if ( cur_process->pid > 0 ) {
          kill(cur_process->pid, SIGTERM);
          cur_process->pid = 0;
        }
      break;
    }

  }

  return 0;
}
