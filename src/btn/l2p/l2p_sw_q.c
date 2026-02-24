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

/*! \file
 * @brief rainier l2p engine software command queue
 *
 * \addtogroup btn
 * \defgroup l2pe
 * \ingroup btn
 * @{
 */

#include "btn_precomp.h"
#include "dtag.h"
#include "btn.h"
#include "l2p_mgr.h"
#include "l2p_sw_q.h"
#include "event.h"

#define __FILEID__ swq
#include "trace.h"

typedef struct _l2p_sw_q_t {
	void *que_buf;		///< pointer to queue buffer
	u16 wptr;		///< write pointer
	u16 rptr;		///< read pointer
	u16 ent_sz;		///< size of one entry
	u16 que_sz;		///< size of the queue

	l2p_que_t *que;		///< pointer to the corresponding hw queue
} l2p_sw_q_t;

fast_data u8 evt_swq_issue = 0xff;	///< handle event for sw queue trigger
fast_data_zi l2p_sw_q_t *_swq[10];		///< sw queue pointer array
fast_data_zi u8 swq_cnt = 0;		///< number of sw queue
fast_data u16 srhq_avail_cnt = SWQ_SZ_SRCH;

fast_code bool l2p_sw_que_full_check(u32 swq_id){
	bool ret = false;
	l2p_sw_q_t *swq = _swq[swq_id];
	u16 nwptr = (swq->wptr + 1) & (swq->que_sz - 1);

	if (nwptr == swq->rptr){
		lxp_mgr_trace(LOG_ERR, 0x03bf, "sw_que_full");
		ret = true;	
	}
	return ret;
}

fast_code void *l2p_sw_que_get_next_req(u32 swq_id)
{
	l2p_sw_q_t *swq = _swq[swq_id];
	u16 nwptr = (swq->wptr + 1) & (swq->que_sz - 1);
	void *ptr;

	if (nwptr == swq->rptr)
		return NULL;

	ptr = ptr_inc(swq->que_buf, swq->ent_sz * swq->wptr);
	return ptr;
}

fast_code void l2p_sw_que_submit(u32 swq_id)
{
	l2p_sw_q_t *swq = _swq[swq_id];

	swq->wptr = (swq->wptr + 1) & (swq->que_sz - 1);
	evt_set_cs(evt_swq_issue, 0, 0, CS_TASK);
}

fast_code bool l2p_sw_queue_issue(u32 swq_id)
{
	l2p_sw_q_t *swq = _swq[swq_id];
	hdr_reg_t *reg;
	int idx;
	int next;
	void *req;
	void *sw_req;

	while (swq->rptr != swq->wptr) {
		next = _req_submit(swq->que, &idx, false);
		if (next == -1)
			break;

		reg = &swq->que->reg;

		req = reg->base;
		req = ptr_inc(req, idx * swq->ent_sz);

		sw_req = ptr_inc(swq->que_buf, swq->rptr * swq->ent_sz);
		memcpy(req, sw_req, swq->ent_sz);

		reg->entry_pnter.b.wptr = (u16) next;

		writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
		swq->rptr++;
		srhq_avail_cnt++;
		if (swq->rptr >= swq->que_sz)
			swq->rptr = 0;
	}

	return (swq->rptr == swq->wptr) ? false : true;
}

/*!
 * @brief handle event for sw queue issue
 *
 * @param param0	not used
 * @param param1	not used
 * @param param2	not used
 *
 * @return		not used
 */
fast_code void l2p_sw_que_issue_evt(u32 param0, u32 param1, u32 param2)
{
	u32 i;
	bool wait = false;

	for (i = 0; i < swq_cnt; i++) {
		wait |= l2p_sw_queue_issue(i);
	}

	if (wait) {
		evt_set_cs(evt_swq_issue, 0, 0, CS_TASK);
	}
}

ps_code void l2p_sw_que_resume(u32 ent_sz, u32 que_sz, l2p_que_t *que)
{
	l2p_sw_q_t *swq;
	u32 swq_id = que->swq_id;

	sys_assert(is_power_of_two(que_sz));
	sys_assert(swq_id < swq_cnt);
	swq = _swq[swq_id];
	sys_assert(swq);

	swq->que_buf = (void *) &swq[1];
	swq->wptr = swq->rptr = 0;
	swq->ent_sz = ent_sz;
	swq->que_sz = que_sz;
	swq->que = que;

	return;
}

init_code u32 l2p_sw_que_init(u32 ent_sz, u32 que_sz, l2p_que_t *que)
{
	l2p_sw_q_t *swq;
	u32 sz = ent_sz * que_sz;
	u32 swq_id;

	sys_assert(is_power_of_two(que_sz));

	swq = (l2p_sw_q_t *) sys_malloc(SLOW_DATA, sz + sizeof(l2p_sw_q_t));
	sys_assert(swq);

	swq->que_buf = (void *) &swq[1];
	swq->wptr = swq->rptr = 0;
	swq->ent_sz = ent_sz;
	swq->que_sz = que_sz;
	swq->que = que;

	_swq[swq_cnt] = swq;
	swq_id = swq_cnt;
	swq_cnt++;

	if (evt_swq_issue == 0xff) {
		evt_register(l2p_sw_que_issue_evt, 0, &evt_swq_issue);
	}
	return swq_id;
}

fast_code bool l2p_sw_que_empty(u32 swq_id)
{
	l2p_sw_q_t *swq = _swq[swq_id];

	return (swq->wptr == swq->rptr) ? true : false;
}

/*! @} */
