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
#include "fsm.h"
#include "stdlib.h"
#include "stdio.h"
#include "sect.h"

/*! \cond PRIVATE */
#define __FILEID__ fsm
#include "trace.h"
/*! \endcond */

fast_code fsm_ctx_t *fsm_ctx_new(int msize, mem_type_t mtype)
{
	fsm_ctx_t *ctx = sys_malloc(mtype, msize);

	if (ctx)
		ctx->mtype = mtype;

	return ctx;
}

fast_code void fsm_ctx_init(fsm_ctx_t *ctx, fsm_state_t *state, int (*done)(void *),
		void *priv)
{
	rtos_fsm_trace(LOG_DEBUG, 0x4b28, "ctx %p, state %p", ctx, state);
	ctx->state = state;
	ctx->state_cur = 0;
	ctx->done = done;
	ctx->done_priv = priv;
}

fast_code void fsm_ctx_set(fsm_ctx_t *ctx, u16 nr)
{
	ctx->state_cur = nr;
}

fast_code void fsm_ctx_next(fsm_ctx_t *ctx)
{
	ctx->state_cur++;
}

fast_code void fsm_ctx_free(fsm_ctx_t *ctx)
{
	sys_free(ctx->mtype, ctx);
}

static fast_code fsm_res_t _fsm_ctx_run(fsm_ctx_t *ctx)
{
	fsm_res_t res;
	fsm_state_t *arr;
	fsm_funs_t *fns;

#ifndef NDEBUG
	u16 ostate = ctx->state_cur;

	if (ctx->state_cur > ctx->state->max)
		panic("wrong state step");
#endif
	arr = ctx->state;
	fns = &arr->fns[ctx->state_cur];

	rtos_fsm_trace(LOG_DEBUG, 0x5d49, "ctx %p, state %s(%d)", ctx, fns->name,
			ctx->state_cur);
	res = fns->fun(ctx);
	rtos_fsm_trace(LOG_DEBUG, 0x85d5, "ctx %p, state %s(%d), res %d", ctx,
			fns->name, ctx->state_cur, res);

	switch (res) {
	case FSM_CONT:
		ctx->state_cur++;
		break;

	case FSM_JMP:
#ifndef NDEBUG
		if (ostate == ctx->state_cur)
			panic("fsm: wrong jmp");
#endif
		break;

	case FSM_PAUSE:
	case FSM_QUIT:
		break;

	default:
		panic("fsm: fns return un-expect value");
		break;
	}

	return res;
}

fast_code void fsm_ctx_run(fsm_ctx_t *ctx)
{
	do {
		fsm_res_t res = _fsm_ctx_run(ctx);
		if (res == FSM_PAUSE || res == FSM_QUIT)
			break;
	} while (1); /*lint !e506 suppress 'Constant value Boolean'*/
}

fast_code void fsm_ctx_cmpl(fsm_ctx_t *ctx)
{
	(void) (ctx->done)(ctx);
}

fast_code void fsm_queue_insert_head(fsm_head_t *fsm, fsm_ctx_t *ctx)
{
	rtos_fsm_trace(LOG_DEBUG, 0x4bf0, "fsm %p, ctx %p", fsm, ctx);
	QTAILQ_INSERT_HEAD(&fsm->head, ctx, node);
}

fast_code void fsm_queue_insert_tail(fsm_head_t *fsm, fsm_ctx_t *ctx)
{
	rtos_fsm_trace(LOG_DEBUG, 0x18fd, "fsm %p, ctx %p", fsm, ctx);
	QTAILQ_INSERT_TAIL(&fsm->head, ctx, node);
}

fast_code fsm_ctx_t *fsm_queue_remove_first(fsm_head_t *fsm)
{
	fsm_ctx_t *ctx = QTAILQ_FIRST(&fsm->head);

	if (ctx)
		QTAILQ_REMOVE(&fsm->head, ctx, node);

	rtos_fsm_trace(LOG_DEBUG, 0xae3e, "fsm %p, ctx %p", fsm, ctx);
	return ctx;
}

fast_code fsm_ctx_t *fsm_queue_remove_last(fsm_head_t *fsm)
{
	fsm_ctx_t *ctx = QTAILQ_LAST(&fsm->head, fsm_qtail_head);

	if (ctx)
		QTAILQ_REMOVE(&fsm->head, ctx, node);

	rtos_fsm_trace(LOG_DEBUG, 0x4810, "fsm %p, ctx %p", fsm, ctx);
	return ctx;
}

fast_data void fsm_queue_run(fsm_head_t *fsm)
{
	fsm_ctx_t *ctx;
	fsm_head_t local;

	rtos_fsm_trace(LOG_DEBUG, 0xcadc, "fsm %p", fsm);
	fsm_queue_init(&local);
	while ((ctx = fsm_queue_remove_first(fsm)) != NULL)
		fsm_queue_insert_tail(&local, ctx);

	while ((ctx = fsm_queue_remove_first(&local)) != NULL)
		fsm_ctx_run(ctx);
}
