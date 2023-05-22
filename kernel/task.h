#pragma once
#include "arch.h"
#include "hinavm.h"
#include "interrupt.h"
#include <libs/common/list.h>
#include <libs/common/message.h>
#include <libs/common/types.h>

#define TASK_QUANTUM (20 * (TICK_HZ / 1000))

#define IDLE_TASK (arch_cpuvar_get()->idle_task)
#define CURRENT_TASK (arch_cpuvar_get()->current_task)

#define TASK_UNUSED 0
#define TASK_RUNNABLE 1
#define TASK_BLOCKED 2

struct task {
  struct arch_task arch;
  struct arch_vm vm;
  task_t tid;
  char name[TASK_NAME_LEN];
  int state;
  bool destroyed;
  struct task *pager;
  unsigned timeout;
  int ref_count;
  unsigned quantum;
  list_elem_t waitqueue_next;
  list_elem_t next;
  list_t senders;
  task_t wait_for;
  list_t pages;
  notifications_t notifications;
  struct message m;
};

extern list_t active_tasks;

struct task *task_find(task_t tid);
task_t task_create(const char *name, uaddr_t ip, struct task *pager);
task_t hinavm_create(const char *name, hinavm_inst_t *insts, uint32_t num_insts, struct task *pager);
error_t task_destroy(struct task *task);
__noreturn void task_exit(int exception);
void task_resume(struct task *task);
void task_block(struct task *task);
void task_switch(void);
void task_dump(void);
void task_init_percpu(void);
