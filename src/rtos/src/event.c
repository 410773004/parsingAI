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

#include "types.h"
#include "event.h"
#include "bitops.h"
#include "stdio.h"
#include "sect.h"
#include "mod.h"
#include "task.h"
#include "stdlib.h"
#include "pic.h"
//#include "io.h"
#include "assert.h"
#include "misc.h"
#include "ddr.h"
#include "nvme_apl.h"
/*! \cond PRIVATE */
#define __FILEID__ event
#include "trace.h"
/*! \endcond */

/**
 * \file event.c
 * @brief This file contain definitions of event api.
 * \addtogroup event
 * @{
 *
 * Event is an service interface between modules, a module could use event to
 * pass payload to another module to handle. Event could hide implementation
 * to provide or export some services. Event could be executed immediately or
 * waited for scheduling of event task.
 */

/*! @brief max event count is 40 */
enum {
	MAX_EVTS = 50,
	MAX_URG_EVTS = 8,
	MAX_ASSERT_EVTS = 4
};

/*! @brief event data structure */
struct evt_data {
	evt_handler_t handler;		///< event handler
	u32 param, r0, r1;		///< event initialized param, event parameters
};

struct evt_delay {
	u32 r0;
	u32 r1;
	u8 cookie;
	struct timer_list timer;
};

static fast_data_zi struct evt_data eds_array[MAX_EVTS];		///< event array
static fast_data_zi struct evt_data eds_array_urg[MAX_URG_EVTS];	///< urgent event array
static fast_data_zi struct evt_data eds_array_assert[MAX_ASSERT_EVTS];	///< assert event array
static fast_data_zi u8 eds_index = 0;					///< next index for event register cookie
static fast_data_zi int evt_process_one_nr = 0;
#ifdef EVT_DELAY_ARRAY
static fast_data struct evt_delay evt_delay_arr[3]; //0: shutdown; 		1:CC_EN clr; 		2:warmboot
static fast_data u8 ebt_delay_arr_bitmap;			//bit 0: shutdown; 	bit 1:CC_EN clr;	bit 2:warmboot
#endif
#if (CPU_ID == CPU_BE_LITE)
fast_data u8 evt_gc_release_dtag_chk= 0xFF;
#endif
ddr_sh_data u32 evt_delay_dtag_start;
#if (CPU_ID == CPU_BE)
fast_data u8 evt_gc_issue_load_p2l = 0xFF;
#endif
init_code int evt_register(evt_handler_t handler, u32 param, u8 *cookie)
{
	rtos_evt_trace(LOG_DEBUG, 0xc0a9, "handler=%x, param=%x, index=%d",
		  handler, param, eds_index);

	if (eds_index >= MAX_EVTS)
        sys_assert(0);
		//return -1;

	struct evt_data *eds = &eds_array[eds_index];
	eds->handler = handler;
	eds->param = param;

	*cookie = eds_index;

	eds_index ++;
	return 0;
}

init_code int urg_evt_register(evt_handler_t handler, u32 param, u8 priority)
{
	if (priority >= MAX_URG_EVTS)
		return -1;

	struct evt_data *eds = &eds_array_urg[priority];
	sys_assert(eds->handler == NULL);

	eds->handler = handler;
	return 0;
}

init_code int assert_evt_register(evt_handler_t handler, u32 param, u8 priority)
{
	if (priority >= MAX_ASSERT_EVTS)
		return -1;

	struct evt_data *eds = &eds_array_assert[priority];
	sys_assert(eds->handler == NULL);

	eds->handler = handler;
	return 0;
}

static fast_data_zi unsigned long evt_bitmap[(MAX_EVTS/8/sizeof(unsigned long))+1];	///< triggered event bitmap
static fast_data_zi unsigned long urg_evt_bitmap[(MAX_URG_EVTS/8/sizeof(unsigned long))+1];
static fast_data_zi unsigned long assert_evt_bitmap[(MAX_ASSERT_EVTS/8/sizeof(unsigned long))+1];
static fast_data_zi task_t task_evt;		///< event task

bool fast_code urg_evt_set(u8 priority, u32 r0, u32 r1)
{
	struct evt_data *eds = &eds_array_urg[priority];

	if (eds->handler == NULL)
		return false;

	eds->r0 = r0;
	eds->r1 = r1;
	test_and_set_bit(priority, urg_evt_bitmap);
	return true;
}

bool fast_code urg_evt_chk(void)
{
	if (urg_evt_bitmap[0] == 0)
		return false;
	return true;
}

void fast_code urg_evt_task_process(void)
{
	int nr = find_first_bit(urg_evt_bitmap, MAX_URG_EVTS);

	while (nr != MAX_URG_EVTS) {
		struct evt_data *eds = &eds_array_urg[nr];
		if (test_and_clear_bit(nr, urg_evt_bitmap)) {
			if (eds->handler)
				eds->handler(eds->param, eds->r0, eds->r1);
		}
		nr = (nr + 1) & (MAX_URG_EVTS - 1);
		nr = find_next_bit(urg_evt_bitmap, MAX_URG_EVTS, nr);
	}
}

//----------------------------Degraded Mode-------------------------------------
bool ddr_code assert_evt_set(u8 priority, u32 r0, u32 r1)
{
	struct evt_data *eds = &eds_array_assert[priority];

	if (eds->handler == NULL)
		return false;

	eds->r0 = r0;
	eds->r1 = r1;
	test_and_set_bit(priority, assert_evt_bitmap);
	return true;
}

bool fast_code assert_evt_chk(void)
{
	#if CPU_ID == 1
	if (smCPUxAssert)
	{
		assert_evt_set(evt_degradedMode, 0, 0);
	}
	#endif
	if (assert_evt_bitmap[0] == 0)
		return false;
	return true;
}

void ddr_code assert_evt_task_process(void)
{
	int nr = find_first_bit(assert_evt_bitmap, MAX_ASSERT_EVTS);

	while (nr != MAX_ASSERT_EVTS) {
		struct evt_data *eds = &eds_array_assert[nr];
		if (test_and_clear_bit(nr, assert_evt_bitmap)) {
			if (eds->handler)
				eds->handler(eds->param, eds->r0, eds->r1);
		}
		nr = (nr + 1) & (MAX_ASSERT_EVTS - 1);
		nr = find_next_bit(assert_evt_bitmap, MAX_ASSERT_EVTS, nr);
	}
}

//----------------------------Degraded Mode-------------------------------------

void fast_code evt_set_imt(u8 cookie, u32 r0, u32 r1)
{
	struct evt_data *eds = &eds_array[cookie];
	if (eds->handler) {
		eds->handler(eds->param, r0, r1);
	}
}

int fast_code evt_set_cs(u8 cookie, u32 r0, u32 r1, evt_cs_t cs)
{
#ifndef NDEBUG
	if (cookie > eds_index) {
		rtos_evt_trace(LOG_INFO, 0x3178, "cookie error %d/%d",
			       cookie, eds_index);
	}
#endif
	if (cs == CS_TASK && test_and_set_bit(cookie, evt_bitmap))
		return -1;

	struct evt_data *eds = &eds_array[cookie];

	if (cs == CS_NOW) {
		if (eds->handler) {
			eds->handler(eds->param, r0, r1);
		}
	} else {
		eds->r0 = r0;
		eds->r1 = r1;
		task_wake(&task_evt);
	}

	return 0;
}

fast_code void evt_task_awake(void)
{
	task_wake(&task_evt);
}

fast_code void evt_delay_resume(void *data)
{
	struct evt_delay *evt_delay = (struct evt_delay *) data;
	u8 cookie = evt_delay->cookie;
	u32 r0 = evt_delay->r0;
	u32 r1 = evt_delay->r1;
	
#ifdef EVT_DELAY_ARRAY
#if defined(SRIOV_SUPPORT)
	extern void shutdown2000(u8 fid);
	extern void shutdown(u8 fid);
#else
	extern void shutdown2000(void);
	extern void shutdown(void);
#endif
	u8 i = 0; //0: shutdown; 1:CC_EN clr; 2:warmboot
	 /*
	 *"evt_fw_dwnld" & "evt_fw_commit" :alloc in ddr, no need to free
	 *define EVT_DELAY_ARRAY : "evt_shutdown" : alloc in evt_delay_arr, no need to free
	 * clear ebt_delay_arr_bitmap 
	 */
	if ((cookie == evt_fw_dwnld) || (cookie == evt_fw_commit)){
		//alloc in ddr, no need to free
	}
	else
	{
		if((void *)shutdown2000 == (void*)r0){
			i = 0;
		}else if((void *)shutdown == (void*)r0){
			i = 1;
		}else{
			i = 2;
		}
		ebt_delay_arr_bitmap &= ~(BIT(i));
	}
#else
	if ((cookie == evt_fw_dwnld) || (cookie == evt_fw_commit)){
		//alloc in ddr, no need to free
	}
	else{
		sys_free(PS_DATA, data);
	}
#endif

	rtos_evt_trace(LOG_WARNING, 0x4747, "evt act %d %d", cookie);
	evt_set_cs(cookie, r0, r1, CS_TASK);
}
typedef struct evt_delay _evt_delay;
ps_code int evt_set_delay(u8 cookie, u32 r0, u32 r1, u32 jiffies_delay)
{
	struct evt_delay *evt_delay;
	
#ifdef EVT_DELAY_ARRAY
#if defined(SRIOV_SUPPORT)
	extern void shutdown2000(u8 fid);
	extern void shutdown(u8 fid);
#else
	extern void shutdown2000(void);
	extern void shutdown(void);
#endif
	u8 i = 0; //0: shutdown; 1:CC_EN clr; 2:warmboot
#endif
#if 1
	if ((cookie == evt_fw_dwnld) || (cookie == evt_fw_commit)){
		evt_delay = (_evt_delay *)ddtag2mem(evt_delay_dtag_start);
		//evlog_printk(LOG_ALW,"FWDL COMMIT evt_delay size %x",sizeof(struct evt_delay));
		}
	else
#ifdef EVT_DELAY_ARRAY
	{
		if((void *)shutdown2000 == (void*)r0){
			i = 0;
		}else if((void *)shutdown == (void*)r0){
			i = 1;
		}else{
			i = 2;
		}
		evt_delay = &evt_delay_arr[i];
	}
#else
	evt_delay = sys_malloc(PS_DATA, sizeof(struct evt_delay));
#endif
#endif
#ifndef EVT_DELAY_ARRAY
	sys_assert(evt_delay);
#endif
	/*lint --e{613} evt_delay is not null from here*/
	evt_delay->cookie = cookie;
	evt_delay->r0 = r0;
	evt_delay->r1 = r1;
#ifdef EVT_DELAY_ARRAY
	rtos_evt_trace(LOG_WARNING, 0xc516, "evt delay %d %d evt_delay_arr=%d", cookie, jiffies_delay,i);
#else
	rtos_evt_trace(LOG_WARNING, 0x7015, "evt delay %d %d", cookie, jiffies_delay);
#endif

	evt_delay->timer.data = evt_delay;
#ifdef EVT_DELAY_ARRAY
	if(!(ebt_delay_arr_bitmap & BIT(i)))
	{
		INIT_LIST_HEAD(&evt_delay->timer.entry);
	}
	ebt_delay_arr_bitmap |= BIT(i);
#else
	INIT_LIST_HEAD(&evt_delay->timer.entry);
#endif
	evt_delay->timer.function = evt_delay_resume;

	mod_timer(&evt_delay->timer, jiffies + jiffies_delay*HZ/10);
	return 0;
}

/*!
 * @brief event scheduler of event task entry
 *
 * According offset to execute event handler
 *
 * @param offset	event id (cookie)
 *
 * @return		next triggered event id
 */
static int fast_code evt_task_process(int offset)
{
	int nr = offset == -1 ? find_first_bit(evt_bitmap, MAX_EVTS) : offset;

	while (nr != MAX_EVTS) {
		struct evt_data *eds = &eds_array[nr];
		if (test_and_clear_bit(nr, evt_bitmap)) {
			if (eds->handler)
				eds->handler(eds->param, eds->r0, eds->r1);
		}
		nr = (nr + 1) & (MAX_EVTS - 1);
		nr = find_next_bit(evt_bitmap, MAX_EVTS, nr);
	}

	return find_first_bit(evt_bitmap, MAX_EVTS);
}

bool fast_code evt_task_in(void)
{
	return (find_first_bit(evt_bitmap, MAX_EVTS) == MAX_EVTS) ? false : true;
}

/*!
 * @brief event scheduler of event task entry
 *
 * Execute event handler if there is any
 *
 * @param none
 *
 * @return	true if execute	one event handler, otherwise false
 */
bool fast_code evt_task_process_one(void)
{
	evt_process_one_nr = find_next_bit(evt_bitmap, MAX_EVTS, evt_process_one_nr);
	if (evt_process_one_nr == MAX_EVTS)
		evt_process_one_nr = find_next_bit(evt_bitmap, MAX_EVTS, 0);

	if (evt_process_one_nr == MAX_EVTS) {
		evt_process_one_nr = 0;
		return false;
	}

	struct evt_data *eds = &eds_array[evt_process_one_nr];
	if (test_and_clear_bit(evt_process_one_nr, evt_bitmap)) {
		if (eds->handler)
			eds->handler(eds->param, eds->r0, eds->r1);
		evt_process_one_nr += 1;
		if (evt_process_one_nr == MAX_EVTS)
			evt_process_one_nr = 0;
	}
	return true;
}

/*!
 * @brief event task entry
 *
 * Event task entry, while loop to execute triggered event, if no event
 * triggered, task will be blocked or yield.
 *
 * @param unused	not used
 *
 * @return 		never return
 */
static int fast_code evt_task(int unsued)
{
	int nr = -1;
	int loop = 10000;

	do {
		int block = 1;
		nr = evt_task_process(nr);
		loop --;
		if (nr != MAX_EVTS && loop != 0)
			continue;
		if (nr != MAX_EVTS && loop == 0) {
			rtos_evt_trace(LOG_ERR, 0xdeea, "evt_task spun for 10000 iterations, nr %d", nr);
			panic("stop");
			block = 0;
		}
		nr = -1;
		loop = 10000;

		if (block)
			task_block();
		else
			task_yield();
	} while (1); /*lint !e506 */

	return 0; /*lint !e527 suppress 'Unreachable code'*/
}

init_code void sys_event_init(void)
{
	int stack_size = 4096;
	stack_t *stack = sys_malloc(FAST_DATA, stack_size);
	task_new(&task_evt, stack, stack_size, evt_task, "evt", 0);
}

bool fast_code evt_task_check(u8 cookie)
{
	if (cookie != 0xFF)
		return ((BIT(cookie) & (evt_bitmap[(cookie/8/sizeof(unsigned long))])) > 0 ) ? true : false;
	else
		return false;
}

void fast_code evt_task_cancel(u8 cookie)
{
	if (cookie != 0xFF)
		clear_bit(cookie, evt_bitmap);
}

/*! @} */
