#ifndef _FORK_H
#define _FORK_H

#include <signal.h>

extern volatile sig_atomic_t terminated;
extern int subprocess_death_fd;

struct fork_process_s {
  int pid;
  int (*fn)(void *);
  void *arg;
  struct fork_process_s *next;
};

void subprocess_init(void);
void subprocess_quit(void);
void subprocess_run(void);
int subprocess_add(int (*)(void *), void *);

#endif
