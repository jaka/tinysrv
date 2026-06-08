#include "fork.h"
#include "project.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h> /* EXIT_FAILURE */
#include <sys/prctl.h>
#include <sys/wait.h> /* waitpid() */
#include <syslog.h>   /* openlog(), syslog() */
#include <unistd.h>   /* fork() */

#include "debug.h"

struct fork_process_s *process_list;

void subprocess_init(void)
{
  process_list = NULL;
}

void subprocess_quit(void)
{
  struct fork_process_s *cur_process;

  while (process_list) {
    cur_process = process_list;
    process_list = cur_process->next;
    if (cur_process->pid > 0) kill(cur_process->pid, SIGTERM);
    free(cur_process);
  }
}

int subprocess_add(int (*fn)(void *), void *arg)
{
  struct fork_process_s *new_process = malloc(sizeof(struct fork_process_s));
  if (!new_process) return -1;

  new_process->pid = 0;
  new_process->fn = fn;
  new_process->arg = arg;

  new_process->next = process_list;
  process_list = new_process;

  return 0;
}

static void subprocess_signal_handler(int signum)
{
  if (signum == SIGTERM) {
    syslog(LOG_INFO, "Received termination signal. Shutting down subprocess.");
    terminated = 1;
  }
}

static int subprocess_setup_signal_handler(void)
{
  struct sigaction sa;

  sa.sa_handler = subprocess_signal_handler;
  sigemptyset(&sa.sa_mask);

  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGSEGV, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
    syslog(LOG_WARNING, "Signal handler could not be set: %m.");
    return -1;
  }

  return 0;
}

void subprocess_run(void)
{
  struct fork_process_s *cur_process;
  int status;
  pid_t new_pid, pid;

  for (;;) {

    for (cur_process = process_list; cur_process; cur_process = cur_process->next) {

      if (cur_process->pid != 0) continue;

      /* Start subprocess. */
      new_pid = fork();
      if (!new_pid) {
        signal(SIGINT, SIG_IGN);
        if (subprocess_setup_signal_handler() < 0) exit(EXIT_FAILURE);
        prctl(PR_SET_NAME, "worker", 0, 0, 0);
        exit(cur_process->fn(cur_process->arg));
      }
      else if (new_pid > 0) {
        cur_process->pid = new_pid;
        syslog(LOG_ERR, "Created new subprocess with PID %ld.", (long)new_pid);
      }
      else {
        syslog(LOG_INFO, "Could not create a subprocess.");
        terminated = 1;
      }
    }

    /* Wait for any subprocesses to exit. */
#ifndef WAIT_ANY
#define WAIT_ANY -1
#endif
    pid = waitpid(WAIT_ANY, &status, 0);

    if (pid > 0) {
      if (WIFEXITED(status)) {
        syslog(LOG_INFO, "PID %ld exited with code %d", (long)pid, WEXITSTATUS(status));
        if (WEXITSTATUS(status) == EXIT_FAILURE) terminated = 1;
      }
      else if (WIFSIGNALED(status)) {
        syslog(LOG_INFO, "PID %ld terminated by signal %d", (long)pid, WTERMSIG(status));
      }
    }
    else if (pid == -1) {
      if (errno == EINTR && terminated) {
        DEBUG_PRINT("SIGINT requested shutdown.");
      }
      else terminated = 1;
    }

    if (terminated) {
      DEBUG_PRINT("Kill all running subprocesses.");
      for (cur_process = process_list; cur_process; cur_process = cur_process->next)
        if (cur_process->pid > 0) {
          kill(cur_process->pid, SIGTERM);
          cur_process->pid = 0;
        }
      /* Reap remaining children */
      while ((pid = waitpid(-1, NULL, 0)) > 0);
      break;
    }
  }
}
