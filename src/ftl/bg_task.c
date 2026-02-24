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
/*! \file bg_task.c
 * @brief exec background tasks(GC/WL/Flush)
 *
 * \addtogroup ftl
 *
 * @{
 */

#include "types.h"
#include "sect.h"
#include "stdlib.h"
#include "string.h"
#include "cpu_msg.h"
#include "ftl_export.h"
#include "ftl_ns.h"
#include "spb_mgr.h"
#include "gc.h"

#define __FILEID__ bgt
#include "trace.h"

enum {
	BG_TASK_PRI_GC = 0,
	BG_TASK_PRI_WL,
	BG_TASK_PRI_MAX,
};	///< background task priority

#define FTL_BG_GC_THR			3		///< gc_end + this value
#define FTL_BG_GC_VCNT_THR_RATIO	10		///< 1/10

typedef struct _bg_task_mgr_t {
	u8 task_pri;	///< current bg task priority
	u8 nsid;	///< current bg task namespace
	u16 gc_ttl;	///< total gc spb count in this bg round
	u16 wl_ttl;	///< total wl spb count in this bg round
	gc_action_t gc_abort;	///< gc abort request

	union {
		struct {
			u32 aborting : 1;	///< bg task is aborting
			u32 stop : 1;
		} b;
		u32 all;
	} flags;
} bg_task_mgr_t;

typedef bool (*bg_task_func)(void);

/*!
 * @brief	exec bg gc
 *
 * @return	true if find gc candidate and start gc
 */
static bool bg_task_gc(void);

/*!
 * @brief	exec bg wl
 *
 * @return	true if find wl candidate and start gc
 */
static bool bg_task_wl(void);

share_data_zi volatile ftl_flags_t shr_ftl_flags;
share_data_zi volatile read_only_t read_only_flags;


fast_data_ni bg_task_mgr_t bg_task;
fast_data static bg_task_func _bg_task_func[BG_TASK_PRI_MAX] = {
	bg_task_gc,
	bg_task_wl,
};


static void bg_task_exec(void);

fast_code void bg_gc_done(void *ctx)
{
	if (bg_task.flags.b.stop) {
		bg_task.flags.b.stop = 0;
		ftl_apl_trace(LOG_INFO, 0x0413, "bg task stop");
		cpu_msg_issue(CPU_FE - 1, CPU_MSG_BG_TASK_DONE, 0, false);
		return;
	}

	u32 free_du_cnt = (u32)ctx;
	if (free_du_cnt != ~0)
		bg_task_exec();
}

fast_code bool bg_task_gc(void)
{
	u32 nsid;
	u32 pool_id;
	spb_id_t spb = INV_SPB_ID;
	ftl_ns_t *ns;
	ftl_ns_pool_t *ns_pool;

	sys_assert(shr_ftl_flags.b.gcing == false);

	for (; bg_task.nsid < FTL_NS_ID_END; bg_task.nsid++) {
		if (bg_task.nsid == FTL_NS_ID_INTERNAL)
			continue;

		nsid = bg_task.nsid;
		ns = ftl_ns_get(nsid);

		// always GC SLC pool first, if not pSLC or pXLC
		pool_id = SPB_POOL_FREE;

		ns_pool = &ns->pools[pool_id];

		// TODO: bgc threshold need optimization
		if ((ns_pool->quota - ns_pool->allocated) <= (ns_pool->gc_thr.end + FTL_BG_GC_THR)) {
			ftl_apl_trace(LOG_ERR, 0xe0d3, "tzu here is bg gc it will be remove in next version %d %d\n",ns_pool->quota, ns_pool->allocated);
			spb = spb_mgr_find_gc_candidate(nsid, SPB_POOL_USER, SPB_GC_CAN_POLICY_MIN_VC);
			sys_assert(spb != INV_SPB_ID);
		}

		if (spb != INV_SPB_ID) {

			ftl_apl_trace(LOG_INFO, 0xcf9e, "bg task gc start %d", spb);
			bg_task.gc_ttl++;
			gc_start(spb, nsid, min_vac, bg_gc_done);
			return true;
		}
	}


	//reset nsid for next task
	bg_task.nsid = FTL_NS_ID_START;
	//dec useless print
	if (bg_task.gc_ttl)
		ftl_apl_trace(LOG_INFO, 0x9199, "bg task gc done %d", bg_task.gc_ttl);

	return false;
}

fast_code bool bg_task_wl(void)
{
	u32 pool_id;
	spb_id_t spb;
	//ftl_ns_t *ns;

	sys_assert(shr_ftl_flags.b.gcing == false);

	for (; bg_task.nsid < FTL_NS_ID_END; bg_task.nsid++) {

		pool_id = SPB_POOL_FREE;

		spb = spb_mgr_find_gc_candidate(bg_task.nsid, pool_id, SPB_GC_CAN_POLICY_WL);
		if (spb != INV_SPB_ID) {
			bg_task.wl_ttl++;
			ftl_apl_trace(LOG_INFO, 0x5d3a, "bg task wl start %d", spb);
			gc_start(spb, bg_task.nsid, INV_U32, bg_gc_done);
			return true;
		}
	}

	//reset nsid for next task
	bg_task.nsid = FTL_NS_ID_START;
	//dec useless print
	if (bg_task.wl_ttl)
		ftl_apl_trace(LOG_INFO, 0x5fed, "bg task wl done %d", bg_task.wl_ttl);

	return false;
}

fast_code void bg_task_exec(void)
{
	bg_task_func task_func;

	for (; bg_task.task_pri < BG_TASK_PRI_MAX; bg_task.task_pri++) {
		task_func = _bg_task_func[bg_task.task_pri];
		if (task_func())
			return;
	}

	//return true to FE means all bg task done, then FE enter APST
	cpu_msg_issue(CPU_FE - 1, CPU_MSG_BG_TASK_DONE, 0, true);
}

fast_code void ipc_bg_task_exec(volatile cpu_msg_req_t *req)
{
	u32 task_state = req->pl;

	if (read_only_flags.all) {
		// don't exec any bg task when system is read only
		cpu_msg_issue(CPU_FE - 1, CPU_MSG_BG_TASK_DONE, 0, true);
		return;
	}

	if (task_state == BG_TASK_NEW) {
		bg_task.gc_ttl = 0;
		bg_task.wl_ttl = 0;
		bg_task.flags.b.stop = false;	// re-start
		bg_task.nsid = FTL_NS_ID_START;
		bg_task.task_pri = BG_TASK_PRI_GC;
	}

	bg_task_exec();
}

fast_code void bg_gc_abort_done(gc_action_t* action)
{
	bg_task.flags.b.aborting = false;
}

fast_code void ipc_bg_task_abort(volatile cpu_msg_req_t *req)
{
	if (gc_busy() == false)
		return;

	if (bg_task.flags.b.aborting)
		return;

	ftl_apl_trace(LOG_INFO, 0x9a28, "abort bg task %d", bg_task.task_pri);

	bg_task.gc_abort.act = GC_ACT_ABORT;
	bg_task.gc_abort.caller = &bg_task;
	bg_task.gc_abort.cmpl = bg_gc_abort_done;

	bg_task.flags.b.aborting = true;
	bg_task.flags.b.stop = true;
	gc_action(&bg_task.gc_abort);
}

fast_code void bg_task_resume(void)
{
	memset(&bg_task, 0, sizeof(bg_task_mgr_t));
}

init_code void bg_task_init(void)
{
	bg_task_resume();
	cpu_msg_register(CPU_MSG_BG_TASK_EXEC, ipc_bg_task_exec);
	cpu_msg_register(CPU_MSG_BG_TASK_ABORT, ipc_bg_task_abort);
}

