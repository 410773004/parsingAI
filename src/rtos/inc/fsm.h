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

#pragma once

/*!
 * \file fsm.h
 * \brief This file contains definitions of final state machine
 * \addtogroup fsm
 * @{
 *
 * Final state machine is used to split long procedure to small step. It could
 * make code more readable and flexible.
 */

#include "types.h"
#include "stdlib.h"
#include "queue.h"

/*! @brief final state machine function result */
typedef enum {
	FSM_CONT,	///< continue state machine
	FSM_JMP,	///< jump to specific state
	FSM_PAUSE,	///< state machine pause, leave machine
	FSM_QUIT,	///< state machine over, leave machine
} fsm_res_t;

struct fsm_ctx;

/*! @brief define queue for final state machine context */
typedef struct fsm_head {
	QTAILQ_HEAD(fsm_qtail_head, fsm_ctx)
	head;
} fsm_head_t;

/*!
 * @brief state machine queue initialization
 *
 * @param fsm	final state machine queue
 *
 * @return 	none
 */
static inline void fsm_queue_init(fsm_head_t *fsm)
{
	QTAILQ_INIT(&fsm->head);
}

/*!
 * @brief final state machine state function
 *
 * @return	return state result, cont, jump, pause and quit.
 */
typedef fsm_res_t (*fsm_func_t)(struct fsm_ctx *);

/*! @brief type of state function */
typedef struct {
	char *name;		///< state function name
	fsm_func_t fun;	///< state function pointer
} fsm_funs_t;

/*! @brief type of final state machine */
typedef struct {
	char *name;		///< state machine name
	fsm_funs_t *fns;	///< state machine function array
	u16 max;	///< max state of final state machine
} fsm_state_t;

/*!
 * @brief parent context of final state machine context
 *
 * Final state machine context will be run in a final state machine, and it
 * could be parent context of a more complex context.
 */
typedef struct fsm_ctx {
	mem_type_t mtype;			///< memory type of context

	fsm_state_t *state;			///< state machine
	u16 state_cur;			///< current state

	int (*done)(void *);		///< completion callback
	void *done_priv;			///< caller context

	QTAILQ_ENTRY(fsm_ctx) node;		///< node of context queue
} fsm_ctx_t;

/*!
 * @brief create a fsm context
 *
 * @param msize		how many byte allocate
 * @param mtype		memory type
 *
 * @return		pointer to fsm context
 */
fsm_ctx_t *fsm_ctx_new(int msize, mem_type_t mtype);

/*!
 * @brief free fsm context
 *
 * @param ctx	pointer to fsm context
 *
 * @return	None
 */
void fsm_ctx_free(fsm_ctx_t *ctx);

/*!
 * @brief final state machine context initialization
 *
 * @param ctx		fsm context
 * @param state		final state machine
 * @param done		callback function
 * @param priv		caller context
 *
 * @return 	    	None
 */
void fsm_ctx_init(fsm_ctx_t *ctx, fsm_state_t *state, int (*done)(void*),
		void *done_priv);

/*!
 * @brief set current state of final state machine context
 *
 * @param ctx	fsm context
 * @param nr	which step will set to
 *
 * @return  	None
 */
void fsm_ctx_set(fsm_ctx_t *ctx, u16 nr);

/*!
 * @brief update final state machine context to next state
 *
 * @param ctx	fsm context
 *
 * @return  	None
 */
void fsm_ctx_next(fsm_ctx_t *ctx);

/*!
 * @brief execute final state machine context
 *
 * Run until state function return quit or pause.
 *
 * @param ctx	fsm context
 *
 * @return  	None
 */
void fsm_ctx_run(fsm_ctx_t *ctx);

/*!
 * @brief execute callback function
 *
 * @param ctx		fsm context
 *
 * @return		None
 */
void fsm_ctx_cmpl(fsm_ctx_t *ctx);

/*!
 * @brief insert final state machine context to head of context queue
 *
 * @param fsm		fsm head
 * @param ctx		fsm context
 *
 * @return		None
 */
void fsm_queue_insert_head(fsm_head_t *fsm, fsm_ctx_t *ctx);

/*!
 * @brief insert final state machine context to tail of context queue
 *
 * @param fsm		fsm head
 * @param ctx		fsm context
 *
 * @return		None
 */
void fsm_queue_insert_tail(fsm_head_t *fsm, fsm_ctx_t *ctx);

/*!
 * @brief remove first final state machine context from context queue head
 *
 * @param fsm		fsm queue object
 *
 * @return		first fsm context of queue, or NULL
 */
fsm_ctx_t *fsm_queue_remove_first(fsm_head_t *fsm);

/*!
 * @brief remove final state machine context context from tail of context queue
 *
 * @param fsm		fsm queue object
 *
 * @return		last fsm context of queue, or NULL
 */
fsm_ctx_t *fsm_queue_remove_last(fsm_head_t *fsm);

/*!
 * @brief run all context in context queue
 *
 * @param fsm		fsm queue
 *
 * @return 		none
 */
void fsm_queue_run(fsm_head_t *fsm);

/*! @} */
