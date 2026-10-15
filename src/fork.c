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

#ifndef WAIT_ANY
#define WAIT_ANY -1
#endif

struct fork_process_s *process_list;
int subprocess_death_fd = -1;

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
  if (signum == SIGTERM) terminated = 1;
}

static int subprocess_setup_signal_handler(void)
{
  struct sigaction sa;

  sa.sa_handler = subprocess_signal_handler;
  sigemptyset(&sa.sa_mask);

  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGTERM, &sa, NULL) < 0) {
    syslog(LOG_WARNING, "Signal handler could not be set: %m.");
    return -1;
  }

  return 0;
}

static struct fork_process_s *subprocess_find(pid_t pid)
{
  struct fork_process_s *cur_process;

  for (cur_process = process_list; cur_process; cur_process = cur_process->next) {
    if (cur_process->pid == pid) return cur_process;
  }

  return NULL;
}

void subprocess_run(void)
{
  struct fork_process_s *cur_process;
  int status, death_pipe[2];
  pid_t new_pid, pid;

  if (pipe(death_pipe) < 0) {
    syslog(LOG_WARNING, "pipe: %m.");
    death_pipe[0] = death_pipe[1] = -1;
  }
  subprocess_death_fd = death_pipe[0];

  for (;;) {

    for (cur_process = process_list; cur_process; cur_process = cur_process->next) {

      if (cur_process->pid != 0) continue;

      /* Start subprocess. */
      new_pid = fork();
      if (!new_pid) {
        if (death_pipe[1] >= 0) close(death_pipe[1]);
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
    pid = waitpid(WAIT_ANY, &status, 0);

    if (pid > 0) {
      /* Allow the dead subprocess to be started again. */
      cur_process = subprocess_find(pid);
      if (cur_process) cur_process->pid = 0;

      if (WIFEXITED(status)) {
        syslog(LOG_INFO, "PID %ld exited with code %d", (long)pid, WEXITSTATUS(status));
        if (WEXITSTATUS(status) == EXIT_FAILURE) terminated = 1;
      }
      else if (WIFSIGNALED(status)) {
        syslog(LOG_INFO, "PID %ld terminated by signal %d", (long)pid, WTERMSIG(status));
      }

      /* Throttle respawns to avoid a tight crash loop. */
      if (!terminated) sleep(1);
    }
    else if (pid == -1) {
      if (errno == EINTR && terminated) {
        DEBUG_PRINT("SIGINT requested shutdown.");
      }
      else terminated = 1;
    }

    if (terminated) {
      DEBUG_PRINT("Kill all running subprocesses.");
      for (cur_process = process_list; cur_process; cur_process = cur_process->next) {
        if (cur_process->pid > 0) {
          kill(cur_process->pid, SIGTERM);
          cur_process->pid = 0;
        }
      }
      /* Reap remaining children */
      while ((pid = waitpid(-1, NULL, 0)) > 0);
      if (death_pipe[0] >= 0) close(death_pipe[0]);
      if (death_pipe[1] >= 0) close(death_pipe[1]);
      break;
    }
  }
}
