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

#include "tk.h"
#include "task.h"
//#include "atomic.h"
#include "stdio.h"
#include "event.h"
#include "pic.h"

#define __FILEID__ task
#include "trace.h"

fast_data task_t *task_cur;
static fast_data u32 tid;

init_code void task_init(task_t *ts, stack_t *stack, int stsize)
{
	stack_t *sp;

	if (((u32)ts) & 0x3) {
		panic("task must be align 4byte");
	}

	/* it is the currently running task */
	task_cur = ts;
	tid = 0;

	/* mark it as using system stack */
	ts->flags.data = F_MAIN;
	ts->name = "sys";

	/* task is only one in the list */
	ts->next = ts;
	ts->count = 0;
	ts->tid = tid++;
	ts->size = stsize;
	ts->stack = stack;

	/* fill the stack with guardword */
	sp = task_getsp();
	while (sp > stack) {
		sp--;
		*sp = STACKWORD;
	}

	/* set address for stack check */
	ts->guard = sp;
	rtos_core_trace(LOG_DEBUG, 0x53b0, "ts=%x,%d, stack=%x, size=%d",
			ts, ts->tid, stack, stsize);

}

int task_new(task_t *ts, stack_t *stack, int size,
	     int (*start)(int), char *name, int arg)
{
	stack_t *sp;		/* pointer to task stack area for fill */

	if (((u32)ts) & 0x3) {
		panic("task must be align 4byte");
	}

	ts->size = size;
	ts->stack = stack;

	/* set it up to run */
	sp = task_frame(ts, start, arg);

	/* fill stack with guardword */
	ts->fp = sp;
	sp--;
	while (sp > ts->stack) {
		sp--;
		*sp = STACKWORD;
	}

	ts->guard = sp;

	/* schedule the task to run */
	ts->flags.data = F_AWAKE;
	/* fit it in after task_cur */
	ts->next = task_cur->next;
	task_cur->next = ts;
	ts->name = name;
	ts->count = 0;
	ts->tid = tid++;
	rtos_core_trace(LOG_DEBUG, 0x4f9e, "ts=%x,%d, stack=%x, size=%d",
			ts, ts->tid, stack, size);

	return 0;
}

static void task_del(task_t *ts);
fast_data_zi static task_t *task_died;

fast_code void task_block(void)
{
	/* the next task to run */
	task_t *ts = task_cur;
	task_t *hts = ts;

	/* check if the guard word still intact */
	if (*(ts->guard) != STACKWORD) {
		panic("task_block stack");
		return;
	}

	/* find the next entry ready to run task */
	do {
		ts = ts->next;
		/* see if a sleeping task is ready to wake */
		if (ts->waketick && ts->waketick < cticks) {
			/* clear the tick flag */
			ts->waketick = 0;
			/* go run this task */
			break;
		}
#ifdef CPU_IDLE
	if (ts->flags.data & F_AWAKE)
		break;

	if (hts == ts)
		CPU_IDLE;

#endif
	} while ((ts->flags.data & F_AWAKE) == 0);

	/* clear wake flag before it runs */
	atomic_andnot(F_AWAKE, &ts->flags);

	/* run the next task */
	task_switch(ts);

	if (task_died) {
		/* delete the dead task */
		task_del(task_died);
		/* clear pointer/flag */
		task_died = NULL;
	}
}

void task_exit(void)
{
	/* kill current task, save in task_died */
	task_kill(task_cur);
	/* this should delete task_die */
	task_block();
	/* should never return from block */
	panic("task_exit");
}

void task_kill(task_t *task_to_die)
{
	task_t *ts;

	/* Is another task in the process of dying */
	if (task_died) {
		/* this should allow it to finish */
		task_yield();
		if (task_died)
			panic("task_kill die");
	}

	for (ts = task_cur; ts->next != task_to_die; ts = ts->next);

	/* now patch around task_to_die */
	ts->next = task_to_die->next;

	/* If I'm killing myself, die the next time I block */
	if (task_cur == task_to_die)
		task_died = task_cur;
	else
		task_del(task_to_die);
}

/* must call from task context */
static void task_del(task_t *ts)
{
	/* don't delete running task! */
	if (task_cur == ts)
		panic("task_del");

	/* free stack if it's not system stack */
	if ((ts->flags.data & F_MAIN) != 0) {
#ifdef NO_YET
		ts->free(ts->stack);
#endif
	}
#ifdef NO_YET
	ts->free(ts);
#endif
}

#ifndef NDEBUG
/*lint -esym(551, task_wakeups) used as counter for dbg */
static fast_data u32 task_wakeups;
#endif

fast_code void task_sleep(u32 ticks)
{
	rtos_core_trace(LOG_DEBUG, 0xd51b, "ts=%x,%d, ticks=%d, count=%d",
			task_cur, task_cur->tid, ticks, task_cur->count);
	/* put task to sleep */
	atomic_andnot(F_AWAKE, &task_cur->flags);
	/* set wakeup time */
	task_cur->waketick = cticks + ticks;
	task_block();
#ifndef NDEBUG
	task_wakeups++;
#endif
	task_cur->count++;
	rtos_core_trace(LOG_DEBUG, 0xb798, "ts=%x,%d, ticks=%d, count=%d",
			task_cur, task_cur->tid, ticks, task_cur->count);
}

fast_code void task_ev_block(void *event)
{
	u32 oe;

	rtos_core_trace(LOG_DEBUG, 0x7673, "ts=%x,%d, count=%d, event=%p",
			task_cur, task_cur->tid, task_cur->count, event);
	/* put task to sleep */
	atomic_andnot(F_AWAKE, &task_cur->flags);
	/* set wake event */
	oe = xchg(event, &task_cur->event);
#ifndef NDEBUG
	if (oe != 0)
		panic("task_ev_block");
#endif
	/* wait for task_event_wake() */
	task_block();
#ifndef NDEBUG
	task_wakeups++;
#endif
	task_cur->count++;
	rtos_core_trace(LOG_DEBUG, 0x4ecf, "ts=%x,%d, count=%d, event=%p",
			task_cur, task_cur->tid, task_cur->count, event);
}

fast_code void task_wake(task_t *ts)
{
	atomic_or(F_AWAKE, &ts->flags);
#ifndef NDEBUG
	task_wakeups++;
#endif
	ts->count++;
}

fast_code void task_ev_wake(void *event)
{
	task_t *ts;

	/* loop though task list - skip task_cur */
	for (ts = task_cur->next; ts != task_cur; ts = ts->next) {
		u32 oe;

		rtos_core_trace(LOG_DEBUG, 0xb5e9, "ts=%x,%d, event=%p,%p",
				ts, ts->tid, ts->event, event);
		if (ts->event != event)
			continue;
		/* clear event */
		oe = xchg(NULL, &ts->event);
#ifndef NDEBUG
		if (oe != (u32) event)
			panic("task_ev_wake");
#endif
		/* wake the task */
		atomic_or(F_AWAKE, &ts->flags);
	}
	rtos_core_trace(LOG_DEBUG, 0xd511, "ts=%x,%d, count=%d", ts, ts->tid,
			ts->count);
}
