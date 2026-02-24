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

//=============================================================================
//
/*! \file bg_training.c
 * @brief trainning background state
 *
 * \addtogroup dispatcher
 *
 * @{
 */

#include "types.h"
#include "misc.h"
#include "sect.h"
#include "string.h"
#include "cpu_msg.h"
#include "ftl_export.h"
#include "console.h"
#include "req.h"
#include "nvme_apl.h"

#define __FILEID__ bgr
#include "trace.h"

enum {
	BG_ST_UNKNOWN,	///< background state: unknown
	BG_ST_BUSY,	///< background state: io running
	BG_ST_IDLE,	///< background state: idle train done, may start bg task
	BG_ST_DONE,	///< background state: idle flush done
	BG_ST_STOP,	///< background state: stop, keep timer and wait for enable again
	BG_ST_MAX,
};

#define DEFAULT_BG_PERIOD	2	///< training bg every 2 seconds by default
#define DEFAULT_BG_IDLE_THR	3	///< bg training threshold

typedef struct _bg_training_t {
	u8 idle_cnt;	///<  idle counter, switch background state when reach threshold
	u8 idle_thr;	///< when idle count reach this valid, bg training success
	u8 period;	///< bg training period
	u8 bg_state;	///< bg state
	struct timer_list timer;

	union {
		struct {
			u32 task_execing : 1;	///< bg task is executing
			u32 idle_flushing : 1;	///< idle flushing
		} b;
		u32 all;
	} flags;
} bg_training_t;

extern volatile ftl_flags_t shr_ftl_flags;
fast_data_zi bg_training_t bg_training;
fast_data_zi req_t _idle_flush_req;

extern u64 get_req_statistics(void);

ddr_code bool is_system_idle(void)
{
	//rcv host cmd when training
	if (get_req_statistics())
		return false;

	if (nvmet_cmd_pending())
		return false;

	//gc or flush triggered by bg, system is still idle
	if (bg_training.flags.b.task_execing || bg_training.flags.b.idle_flushing)
		return true;

	//gc and flush not triggered by bg, system is busy
	if (shr_ftl_flags.b.gcing || shr_ftl_flags.b.flushing)
		return false;

	return true;
}

ddr_code bool bg_st_switch(u8 new_state)
{
	if (new_state != bg_training.bg_state) {
		disp_apl_trace(LOG_INFO, 0x046d, "bg state %d->%d", bg_training.bg_state, new_state);
		bg_training.bg_state = new_state;
		return true;
	}

	return false;
}

ddr_code void bg_training_timer(void *data)
{
	if (bg_training.bg_state == BG_ST_STOP || shr_ftl_flags.b.ftl_ready == 0) {
		mod_timer(&bg_training.timer, jiffies + bg_training.period * HZ);
		return;
	}

	if (is_system_idle()) {
		bg_training.idle_cnt++;
	} else {
		bg_training.idle_cnt = 0;
		bg_st_switch(BG_ST_BUSY);

		/*
		* abort running task to guarantee performance,
		* but abort here is not very in time, optimization is necessary?
		*/
		if (bg_training.flags.b.task_execing)
			cpu_msg_issue(CPU_FTL - 1, CPU_MSG_BG_TASK_ABORT, 0, 0);
	}

	if (bg_training.idle_cnt >= bg_training.idle_thr && bg_training.bg_state != BG_ST_DONE) {
		bg_training.idle_cnt = 0;
		bg_st_switch(BG_ST_IDLE);

		if (bg_training.flags.b.task_execing == false && bg_training.flags.b.idle_flushing == false) {
			bg_training.flags.b.task_execing = true;
			//busy->idle, exec bg task from the start
			cpu_msg_issue(CPU_FTL - 1, CPU_MSG_BG_TASK_EXEC, 0, BG_TASK_NEW);
		}
	}

	mod_timer(&bg_training.timer, jiffies + bg_training.period * HZ);
}

fast_code bool idle_flush_done(req_t *req)
{
	bg_training.flags.b.idle_flushing = false;

	if (bg_training.bg_state != BG_ST_IDLE)
		return 0;

	// enter apst
	bg_st_switch(BG_ST_DONE);

	return 0;
}

fast_code void ipc_bg_task_done(volatile cpu_msg_req_t *req)
{
	bg_training.flags.b.task_execing = false;
	if (bg_training.bg_state != BG_ST_IDLE)
		return;

	//check system idle again before flush
	if (is_system_idle() == false) {
		bg_training.idle_cnt = 0;
		bg_st_switch(BG_ST_BUSY);
		mod_timer(&bg_training.timer, jiffies + bg_training.period * HZ);
		return;
	}

	sys_assert(bg_training.flags.b.idle_flushing == false);
	bg_training.flags.b.idle_flushing = true;
	_idle_flush_req.completion = idle_flush_done;
	_idle_flush_req.req_from = REQ_Q_BG;
	_idle_flush_req.opcode = REQ_T_FLUSH;
	_idle_flush_req.op_fields.flush.shutdown = true;
	_idle_flush_req.req_id = 0xff;

	extern bool req_exec(req_t *req);
	req_exec(&_idle_flush_req);
}

ddr_code void bg_enable(void)
{
	if (bg_training.bg_state == BG_ST_STOP)
		bg_st_switch(BG_ST_UNKNOWN);
}

ddr_code void bg_disable(void)
{
	bg_st_switch(BG_ST_STOP);
}

ddr_code bool bg_disable_enhance(void)
{
	bool stop = bg_st_switch(BG_ST_STOP);
	//abort running task when non-stop -> stop
	if (stop && bg_training.flags.b.task_execing)
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_BG_TASK_ABORT, 0, 0);

	return bg_training.flags.b.task_execing ? false : true;
}

ddr_code void bg_training_init(void)
{
	memset(&bg_training, 0, sizeof(bg_training_t));

	bg_training.period = DEFAULT_BG_PERIOD;
	bg_training.idle_thr = DEFAULT_BG_IDLE_THR;

	INIT_LIST_HEAD(&bg_training.timer.entry);
	bg_training.timer.function = bg_training_timer;
	//mod_timer(&bg_training.timer, jiffies + bg_training.period * HZ);

	cpu_msg_register(CPU_MSG_BG_TASK_DONE, ipc_bg_task_done);
}

ddr_code int bg_enable_main(int argc, char *argv[])
{
	u32 type = strtol(argv[1], (void *)0, 10);

	if (type == 0)
		bg_enable();
	else if (type == 1)
		bg_disable();
	else
		bg_disable_enhance();

	return 0;
}

static DEFINE_UART_CMD(bg_enable, "bg_enable", "bg_enable",
		"enable or disable bg", 1, 1, bg_enable_main);
