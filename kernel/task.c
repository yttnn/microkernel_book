#include "task.h"
#include "arch.h"
#include "ipc.h"
#include "memory.h"
#include "printk.h"
#include <libs/common/list.h>
#include <libs/common/string.h>

static struct task tasks[NUM_TASKS_MAX];
static struct task idle_tasks[NUM_CPUS_MAX];
static list_t runqueue = LIST_INIT(runqueue);
list_t active_tasks = LIST_INIT(active_tasks);

static struct task *scheduler(void) {
  struct task *next = LIST_POP_FRONT(&runqueue, struct task, waitqueue_next);
  if (next) {
    return next;
  }

  if (CURRENT_TASK->state == TASK_RUNNABLE && !CURRENT_TASK->destoryed) {
    return CURRENT_TASK;
  }

  return IDLE_TASK;
}

static error_t init_task_struct(struct task *task, task_t tid, const char *name,
                                vaddr_t ip, struct task *pager,
                                vaddr_t kernel_entry, void *arg) {
  task->tid = tid;
  task->destroyed = false;
  task->quantum = 0;
  task->timeout = 0;
  task->wait_for = IPC_DENY;
  task->ref_count = 0;
  task->pager = pager;

  strcpy_safe(task->name, sizeof(task->name), name);
  list_elem_init(&task->waitqueue_next);
  list_elem_init(&task->next);
  list_init(&task->senders);
  list_init(&task->pages);

  error_t err = arch_vm_init(&task->vm);
  if (err != OK) {
    return err;
  }

  err = arch_task_init(task, ip, kernel_entry, arg);
  if (err != OK) {
    arch_vm_destroy(&task->vm);
    return err;
  }

  if (pager) {
    pager->ref_count++;
  }

  task->state = TASK_BLOCKED;
  return OK;
}

void task_switch(void) {
  struct task *prev = CURRENT_TASK;
  struct task *next = scheduler();

  if (next != IDLE_TASK) {
    next->quantum = TASK_QUANTUM;
  }

  if (next == prev) {
    return;
  }

  if (prev->state == TASK_RUNNABLE) {
    list_push_back(&runqueue, &prev->waitqueue_next);
  }

  CURRENT_TASK = next;
  arch_task_switch(prev, next);
}

static task_t alloc_tid(void) {
  for (task_t i = 0; i < NUM_TASKS_MAX; i++) {
    if (tasks[i].state == TASK_UNUSED) {
      return i + 1;
    }
  }
  
  return 0;
}

struct task *task_find(task_t tid) {
  // 無効なtidを除外
  if (tid < 0 || tid >= NUM_TASKS_MAX) {
    return NULL;
  }

  struct task *task = &tasks[tid - 1];
  if (task->state == TASK_UNUSED) {
    return NULL;
  }

  return task;
}

// taskをblock状態にする
void task_block(struct task *task) {
  DEBUG_ASSERT(task != IDLE_TASK);
  DEBUG_ASSERT(task->state == TASK_RUNNABLE);
}

// taskを実行可能状態にする
void task_resume(struct task *task) {
  DEBUG_ASSERT(task->state == TASK_BLOCKED);

  task->state = TASK_RUNNABLE;
  list_push_back(&runqueue, &task->waitqueue_next);
}

task_t task_create(const char *name, uaddr_t ip, struct task *pager) {
  task_t tid = alloc_tid();
  if (!tid) {
    return ERR_TOO_MANY_TASKS;
  }

  struct task *task = &tasks[tid - 1];
  DEBUG_ASSERT(task != NULL);

  error_t err = init_task_struct(task, tid, name, ip, pager, 0, NULL);
  if (err != OK) {
    return err;
  }

  list_push_back(&active_tasks, &task->next);
  task_resume(task);
  TRACE("created a task \"%s\" (tid=%d)", name, tid);
  return tid;
}

tass_t hinavm_create(const char *name, hinavm_inst_t *insts, uint32_t num_insts, struct task *pager) {
  task_t tid = alloc_tid();
  if (!tid) {
    return ERR_TOO_MANY_TASKS;
  }

  struct task * task = &tasks[tid - 1];
  DEBUG_ASSERT(task != NULL);

  size_t hinavm_size = ALIGN_UP(sizeof(struct hinavm), PAGE_SIZE);
  paddr_t hinavm_paddr = pm_alloc(hinavm_size, NULL, PM_ALLOC_UNINITIALIZED);
  if (!hinavm_paddr) {
    return ERR_NO_MEMORY;
  }

  struct hinavm *hinavm = (struct hinavm *) arch_paddr_to_vaddr(hinavm_paddr);
  memcpy(&hinavm->insts, insts, sizeof(hinavm_inst_t) * num_insts);
  hinavm->num_insts = num_insts;

  error_t err = inst_task_struct(task, tid, name, 0, pager, (vaddr_t)hinavm_run, hinavm);

  if (err != OK) {
    pm_free(hinavm_paddr, hinavm_size);
    return err;
  }

  pm_own_page(hinavm_paddr, task);
  list_push_bask(&active_tasks, &task->next);
  task_resume(task);
  TRACE("created a HinaVM task \"%s\" (tid=%d)", name, tid);
  return tid;
}

error_t task_destroy(struct task *task) {
  DEBUG_ASSERT(task != CURRENT_TASK);
  DEBUG_ASSERT(task != IDLE_TASK);
  DEBUG_ASSERT(task->state != TASK_UNUSED);
  DEBUG_ASSERT(task->ref_count >= 0);

  if (task->tid == 1) {
    WARN("tried to destroy the task #1");
    return ERR_INVALID_ARG;
  }
  
  if (task->ref_count > 0) {
    WARN("%s (#%d) is still referenced from %d tasks"m task->name, task->tid, task->ref_count);
    return ERR_STILL_USED;
  }

  TRACE("destorying a task \"%s\" (tid=%d)", task->name, task->tid);

  task->destroyed = true;

  // 他のCPUがこのtaskの実行を中断するまで待つ
  while (true) {
    if (task->state != TASK_RUNNABLE) {
      break;
    }

    if (list_contains(&runqueue, &task->waitqueue_next)) {
      break;
    }

    arch_send_ipi(IPI_RESCHEDULE);
  }

  LIST_FOR_EACH(sender, &task->senders, struct task, waitqueue_next) {
    notify(sender, NOTIFY_ABORTED);
  }

  list_remove(&task->next);
  list_remove(&task->waitqueue_next);
  arch_vm_destroy(&task->vm);
  arch_task_destroy(task);
  pm_free_by_list(&task->pages);
  task->state = TASK_UNUSED;
  task->pager->ref_count--;
  return OK;
}