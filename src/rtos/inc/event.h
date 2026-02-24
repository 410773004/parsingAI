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

#ifndef _EVENT_H_
#define _EVENT_H_

#define EVT_DELAY_ARRAY		//fix evt deay panic. shengbin yang 2023/8/23

/*!
 * \file event.h
 * @brief This file contain definitions of event api.
 * \addtogroup event
 * @{
 */

/*!
 * @brief type of event handler callback function
 *
 * @param parm		data pass from evt_register function
 * @param payload	data pass from evt_set function
 * @param sts		data pass from evt_set function
 *
 * @return		None
 */
typedef void (*evt_handler_t)(u32 parm, u32 payload, u32 sts);

/*!
 * @brief event register function
 *
 * @param handler 	callback function
 * @param parm 		initialized private data for callback function
 * @param cookie 	event subsystem will assign a cookie of this event
 *
 * @return		Return to caller with 0, or error return -1.
 */
int evt_register(evt_handler_t handler, u32 parm, u8 *cookie);

/*! @brief type of event trigger */
typedef enum {
	CS_NOW,		///< event handled immediately
	CS_TASK,	///< event handled by event task
} evt_cs_t;

/*!
 * @brief event trigger
 *
 * @param cookie	event cookie to be triggered
 * @param payload	event payload, second input of event handler
 * @param sts		event status, third input of event handler
 * @param cs		trigger type, CS_NOW or CS_TASK
 *
 * @return		return 0 if triggered successfully, or -1
 */
int evt_set_cs(u8 cookie, u32 payload, u32 sts, evt_cs_t cs);

/*!
 * @brief event trigger and executed immediately
 *
 * @param cookie	event cookie to be triggered
 * @param r0		second input of event handler
 * @param r1		third input of event handler
 *
 * @return 		none
 */
void evt_set_imt(u8 cookie, u32 r0, u32 r1);

/*!
 * @brief extra event trigger and executed immediately
 *
 * @param cookie	event cookie to be triggered
 *
 * @return 		none
 */
void evt_ex_exec(u8 cookie);

/*!
 * @brief it's not supported, yet
 *
 * @param cookie	event cookie to be triggered
 * @param payload	payload of event
 * @param sts		status of status
 * @param jiffies_delay	delay jiffies number
 *
 * @return
 */
int evt_set_delay(u8 cookie, u32 payload, u32 sts,
		  u32 jiffies_delay);

/*!
 * @brief execute event handler if there is any
 *
 * @return 	true if execute	one event handler, otherwise false
 */

bool evt_task_process_one(void);

/*!
 * @brief check if any event was set
 *
 * @return	true if any event was set
 */
bool evt_task_in(void);

/*!
 * @brief check if the same event was set or not
 *
 * @return	true if any event was set
 */
bool evt_task_check(u8 cookie);

/*!
 * @brief cancel event
 *
 * @param cookie 	event to be cancelled
 *
 * @return		not used
 */
void evt_task_cancel(u8 cookie);

/*!
 * @brief API to force awake evt_task without event trigger
 *
 * @return	none
 */
void evt_task_awake(void);

/*!
 * @brief event initialization
 *
 * @return 	none
 */
void sys_event_init(void);

/*!
 * @brief register urgent event handler
 *
 * @param handler		handler function
 * @param param			1st parameter
 * @param priority		pre-assigned priority
 *
 * @return			return -1 if priority error
 */
int urg_evt_register(evt_handler_t handler, u32 param, u8 priority);

/*!
 * @brief check if urgent event triggered
 *
 * @return	true if yes
 */
bool urg_evt_chk(void);

/*!
 * @brief according priority handle urgent event
 *
 * @return	not used
 */
void urg_evt_task_process(void);

/*!
 * @brief urgent event trigger
 *
 * @param priority	priority
 * @param r0		2nd parameter
 * @param r1		3rd parameter
 *
 * @return		return false if handler was not registered
 */
bool urg_evt_set(u8 priority, u32 r0, u32 r1);

/*!
 * @brief register assert event handler
 *
 * @param handler		handler function
 * @param param			1st parameter
 * @param priority		pre-assigned priority
 *
 * @return			return -1 if priority error
 */
int assert_evt_register(evt_handler_t handler, u32 param, u8 priority);

/*!
 * @brief check if assert event triggered
 *
 * @return	true if yes
 */
bool assert_evt_chk(void);

/*!
 * @brief according priority handle assert event
 *
 * @return	not used
 */
void assert_evt_task_process(void);

/*!
 * @brief assert event trigger
 *
 * @param priority	priority
 * @param r0		2nd parameter
 * @param r1		3rd parameter
 *
 * @return		return false if handler was not registered
 */
bool assert_evt_set(u8 priority, u32 r0, u32 r1);

extern volatile u32 smCPUxAssert;

#if (CPU_ID == CPU_FE)
extern u8 evt_degradedMode;
#endif

#endif
