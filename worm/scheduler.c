#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;

  // 'S': Sleep, 'W': Wait, 'B': Block, 'R': Ready, 'E': Exited
  char state;

  // store when this task should wake up if this task is of state 'S'
  size_t wake_up_time;

  // store which task is being waited if the current task if of state 'W'
  task_t wait_task;

  // store the user input if the current task if of state 'B'
  int user_input;

} task_info_t;

int current_task = 0;          //< The handle of the currently-executing task
int num_tasks = 1;             //< The number of tasks created so far
task_info_t tasks[MAX_TASKS];  //< Information for every task

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functiosn in this file.
 */
void scheduler_init() {
  // Initialize the state of the scheduler to 'R': ready
  tasks[current_task].state = 'R';
}

void switch_context() {
  // store the current task to current_task_temp and update current_task to find the next available
  // one to run
  int current_task_temp = current_task;

  while (TRUE) {
    current_task = (current_task + 1) % num_tasks;
    // If Exited, skip it
    if (tasks[current_task].state == 'E') continue;
    // If Ready, switch to it
    else if (tasks[current_task].state == 'R')
      break;
    // If sleep, check whether sleep time is over
    else if (tasks[current_task].state == 'S') {
      if (time_ms() >= tasks[current_task].wake_up_time) {
        tasks[current_task].state = 'R';
        break;
      } else
        continue;
    }

    // If waiting, check whether the task being waited for is finished
    else if (tasks[current_task].state == 'W') {
      if ((tasks[tasks[current_task].wait_task].state == 'R') ||
          (tasks[tasks[current_task].wait_task].state == 'E')) {
        tasks[current_task].state = 'R';
        break;
      } else
        continue;
    }

    // If blocked, check whether there is a user input now. If so, store it.
    else if (tasks[current_task].state == 'B') {
      int user_input_new = getch();
      if (user_input_new != ERR) {
        tasks[current_task].state = 'R';
        tasks[current_task].user_input = user_input_new;
        break;
      } else
        continue;
    }
  }

  if (swapcontext(&(tasks[current_task_temp].context), &(tasks[current_task].context)) < 0) {
    perror("swapcontect failed!\n");
    exit(2);
  }
}
/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  // Set the state to 'E' and switch to a new task.
  tasks[current_task].state = 'E';
  switch_context();
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;

  // Set the task handle to this index, since task_t is just an int
  *handle = index;

  // We're going to make two contexts: one to run the task, and one that runs at the end of the task
  // so we can clean up. Start with the second

  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);

  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;

  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);

  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);

  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;

  // Now set the uc_link field, which sets things up so our task will go to the exit context when
  // the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;

  // Initialize the state of each new task to Ready
  tasks[index].state = 'R';

  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  // set the state of this task to wait, store which task is being waited, and change to a new task.
  tasks[current_task].state = 'W';
  tasks[current_task].wait_task = handle;
  switch_context();
}

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.scheduler_init
 *
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  // set the state of this task to sleep, store the wake up time, and change to a new task.
  tasks[current_task].wake_up_time = time_ms() + ms;
  tasks[current_task].state = 'S';
  switch_context();
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  //  if there is user input, return it.
  // Otherwise, set the state of this task to block, change to a new task, and return the user input
  // after this task is switched back in the future..
  int ch_input = getch();
  if (ch_input != ERR) {
    tasks[current_task].state = 'R';
    tasks[current_task].user_input = ch_input;
    return ch_input;
  } else {
    tasks[current_task].state = 'B';
    switch_context();
  }
  return tasks[current_task].user_input;
}
