#ifndef _FORK_H
#define _FORK_H

extern volatile int terminated;

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
