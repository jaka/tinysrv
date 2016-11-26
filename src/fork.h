#ifndef _FORK_H
#define _FORK_H

extern volatile int terminated;

struct fork_process_s {
  int pid;
  int (*fn)(void *);
  void *arg;
  struct fork_process_s *next;
};

struct fork_process_s *process_list;

int subprocess_init(void);
int subprocess_quit(void);
int subprocess_add(int (*)(void *), void *);
int subprocess_run(void);

#endif
