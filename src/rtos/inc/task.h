//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#ifndef _TASK_H_
#define _TASK_H_

#include "sect.h"
#include "atomic.h"

/*! \file task.h
 * @brief This file contians definitions for multi-task rtos.
 *
 * \addtogroup task
 * @{
 * Details.
 */

/*!
 * @brief pattern written into stack area
 */
#define STACKWORD 0x53544143

/*!
 * @brief type of data in task stacks
 */
typedef u32 stack_t;

/*!
 * @brief type of data in task
 *
 * if you change this, be sure to change all the assembly which depends on it.
 */
typedef struct task {
	struct task *next;     ///< pointer to next task
	stack_t     *fp;       ///< task's current frame ptr
	u32     size;     ///< stack size
	stack_t     *stack;    ///< base of task's stack
#define F_AWAKE 0x0000001  ///< task is ready to run
#define F_MAIN  0x0000002  ///< task is using system stack
	atomic_t     flags;    ///< flag set if task is scheduled

	char        *name;     ///< the task's name
	u32     count;    ///< number of wakeups
	stack_t     *guard;    ///< pointer to lowest stack
	void        *event;    ///< event to wake blocked task
	u32     waketick; ///< tick to wakeup sleeping task
	u32     tid;      ///< task id
#ifdef NO_YET
	void  *(free)(void *p);///< pointer to cleanup mem resource
#endif
} __attribute__ ((packed)) __attribute__ ((aligned (4))) task_t;

/*! @brief currently running task */
extern task_t *task_cur;

/** count to task switches */
/** extern u32 task_wakeups; */

/*!
 * @brief Initializes the RTOS and creates the first task.
 *
 * Initialize the tasking system. Create the first task, and set its stack
 * pointer to the main program stack. The first task always uses the main
 * system startup stack for compatability with compiler provided "startup" files.
 * The circular list of tasks * contains only the original task, so originally
 * set its next task pointer to point to itself.
 * The @ts address and @stsize of the first task's stack is passed to task_init as parameters.
 * The first task's default name is "sys".
 * The first task's stack is actully on the system stack.
 *
 * @param ts		pointer to task struct
 * @param stack		pointer to stack
 * @param stsize	the size of stack for this task
 *
 * @return 		This routine returns to the caller with 0, or error it returns error-no.
 */
void task_init(task_t *ts, stack_t *stack, int stsize);

/*!
 * @brief create a new task
 *
 * Create a new task with stsize byte of stack, and place it in the circular
 * list of tasks prev_tk. Awaken it so that it will run, and set it up so that
 * it does run, it will start running routine start. This routine does not
 * affect the execution of currently running task.
 * Note, must call from thread context
 *
 * @param ts		pointer to task data struct
 * @param stack		pointer to stack for this task
 * @param stsize	size of stack for this task
 * @param start		where the new task starts execution
 * @param name		the task's name as a string
 * @param arg		argument to the task
 *
 * @return		returns to caller with 0, or error it returns error-no
 */
int task_new(task_t *ts,
		stack_t *stack,
		int stsize,
		int (*start)(int),
		char *name,

		int arg
		);

/*!
 * @brief block the currently running task
 *
 * Block the currently running task and run the next task in the circular list
 * of task that is awake. Before returning, see if any cleanup has to be done
 * for another task.
 * Note, must call from thread context
 *
 * @return	None
 */
void task_block(void);

/*!
 * @brief destroy the current task.
 *
 * destroy the current task. Accomplished by setting a flag indicating that an
 * exit has occured and then entring the scheduler loop. when task_block()
 * returns for some other task and finds this flag set, it deallocateds the
 * task which exited.
 * Note, must call from thread context
 *
 * @return	None
 */
void task_exit(void);

/*!
 * @brief sets the task for death
 *
 * If the @ts_to_kill is the current task, it dies as soon as it blocks.
 * If the @ts_to_kill is not current task, it is killed immediately.
 * Note, must call from thread context
 *
 * @param ts_to_kill	pointer to the task for death
 *
 * @return		None
 */
void task_kill(task_t *ts_to_kill);

/*!
 * @brief force the task ts to the run state.
 *
 * The @ts_to_wakeup task does not run immediately, instead, it is put into a run
 * state so that it runs the next time the scheduler comes around.
 * Note, can call from isr or thread context
 *
 * @param ts_to_wakeup	pointer to task to be wake
 *
 * @return		None
 */
void task_wake(task_t *ts_to_wakeup);

/*!
 * @brief puts the current task to sleep for ticks number of cticks.
 *
 * task_sleep() calls task_block(), causing an immediate task switch.
 * Note, must call from thread context
 *
 * @param ticks		ticks number
 *
 * @return		None
 */
void task_sleep(u32 ticks);

/*!
 * @brief puts the current task into non-running state (sleep)
 *
 * Until an event occurs, The event is a 32-bit value.
 * Note, must call from thread context
 *
 * @param event		that can wakeup the task
 *
 * @return		None
 */
void task_ev_block(void *event);

/*!
 * @brief wake up the task by using event
 *
 * Walk through the TCBs, comparing the @event parameter to the @event
 * entry in the TCB structure. If the TCB's @event equals the parameter
 * @event then the task is marked as runnable.
 * Note, may call from irq
 *
 * @param event		pointer to event
 *
 * @return		None
 */
void task_ev_wake(void *event);

/*!
 * @brief create task frame
 *
 * @return	None
 */
stack_t *task_frame(task_t *ts, int (*)(int), int);

/*!
 * @brief run the next task
 *
 * @return	None
 */
void task_switch(task_t *ts);

/*!
 * @brief get current stack pointer
 *
 * @return	None
 */
stack_t *task_getsp(void);

/*!
 * @brief the yield() macro
 *
 * Note, must call from thread context
 */
#define task_yield() do { task_wake(task_cur); task_block(); } while (0)

/*! @} */
#endif
