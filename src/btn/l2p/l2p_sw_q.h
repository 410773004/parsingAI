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

#include "ddr.h"

#define SWQ_SZ_UPDT	(256 << 1)	///< size of update sw queue
#define SWQ_SZ_SRCH	(1024)		///< size of search sw queue
extern u16 srhq_avail_cnt;


/****************************************************************************
 * Request Submit Functions
 ****************************************************************************/

/*!
 * @brief get available request slot and wptr value
 *
 * @param que		pointer to the queue
 * @param cur		pointer to current request slot
 * @param wait		true for wait until the queue has available request slot
 *
 * @return		next wptr
 */
static inline int _req_submit(volatile l2p_que_t *que, int *cur, int wait)
{
	u16 rptr;
	u16 wptr_nxt;
	volatile hdr_reg_t *reg = &que->reg;

	do {
		rptr = que->ptr;
		wptr_nxt = (reg->entry_pnter.b.wptr + 1) &
				reg->entry_dbase.b.max_sz;

		if (rptr != wptr_nxt) {
			/* still have slot */
			break;
		}

		if (!wait) {
			/* return Busy state */
			return -1;
		}
	} while (1);
	*cur = reg->entry_pnter.b.wptr;
	return wptr_nxt;
}

/*!
 * @brief initial the queue
 *
 * @param ent_sz	size of the entry
 * @param que_sz	size of the queue
 * @param que		pointer to the queue
 *
 * @return		corresponding sw queue id
 */
u32 l2p_sw_que_init(u32 ent_sz, u32 que_sz, l2p_que_t *que);

/*!
 * @brief resume the queue
 *
 * @param ent_sz	size of the entry
 * @param que_sz	size of the queue
 * @param que		pointer to the queue
 *
 * @return		corresponding sw queue id
 */
void l2p_sw_que_resume(u32 ent_sz, u32 que_sz, l2p_que_t *que);


/*!
 * @brief get next available request pointer
 *
 * @param swq_id	sw queue id
 *
 * @return		next request pointer
 */
void *l2p_sw_que_get_next_req(u32 swq_id);

/*!
 * @brief set queue issue event
 *
 * @param swq_id	sw queue id
 *
 * @return		not used
 */
void l2p_sw_que_submit(u32 swq_id);

/*!
 * @brief check if queue empty
 *
 * check rptr wptr mismatch or not
 *
 * @param swq_id	sw queue id
 *
 * @return		true if empty
 */
bool l2p_sw_que_empty(u32 swq_id);

/*!
 * @brief issue sw queue
 *
 * @param swq_id	sw queue id
 *
 * @return		true if still have pending request
 */
bool l2p_sw_queue_issue(u32 swq_id);

/*!
 * @brief check queue condition and issue if not empty
 *
 * @param swq_id	sw queue id
 *
 * @return		true if still have pending request
 */
static inline bool l2p_swq_check(u32 swq_id)
{
#if defined(FPGA)
	return false;
#endif
	if (!l2p_sw_que_empty(swq_id)) {
		if (l2p_sw_queue_issue(swq_id)) {
			// swq is not empty
			return true;
		}
	}
	return false;
}

/*! @} */

