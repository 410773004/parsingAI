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

/*! \file cbf.h
 * @brief circle buffer implementation
 *
 * \addtogroup rtos
 * \defgroup circle buffer
 * \ingroup rtos
 *
 * {@
 */

/*! @brief previous index of circle buffer */
#define prev_cbf_idx(now, _max)		((now > 0) ? (now - 1) : (_max - 1))

/*! @brief next index of circle buffer */
#define next_cbf_idx(now, _max)		(((now + 1) >= _max) ? (0) : (now + 1))

/*! @brief previous index of circle buffer for size is 2's power */
#define prev_cbf_idx_2n(now, _max)	((now - 1) & (_max - 1))

/*! @brief next index of circle buffer for size is 2's power */
#define next_cbf_idx_2n(now, _max)	((now + 1) & (_max - 1))

/*! @brief get circle buffer 2 point distance a->b, return -1 for equal */
#define cbf_distance(a, b, size)	((b >= a) ? (b - a - 1) : (size - a + b - 1))
#define cbf_depth(rptr, wptr, size)	((wptr >= rptr) ? (wptr - rptr) : (size - rptr + wptr))

/*! @brief define circle buffer header */
typedef struct _cbf_t {
	u32 size;
	volatile u32 wptr;
	volatile u32 rptr;
} cbf_t;

#define CBF(type, sz) 	\
struct {			\
	u32 size;		\
	volatile u32 wptr;	\
	volatile u32 rptr;	\
	type buf[sz];		\
}

/*!
 * @brief initialize circle buffer
 *
 * @param cbf8		circle buffer pointer
 *
 * @return		not used
 */
#define CBF_INIT(cbf) do {	\
	(cbf)->wptr = 0;		\
	(cbf)->rptr = 0;		\
	(cbf)->size = sizeof((cbf)->buf) / sizeof((cbf)->buf[0]);	\
} while (0)

/*!
 * @brief check if circle buffer was empty or not
 *
 * @param cbf8		circle buffer
 *
 * @return		return true if circle buffer was empty
 */
#define CBF_EMPTY(cbf)		(((cbf)->wptr == (cbf)->rptr) ? true : false)

#define CBF_MAKE_EMPTY(cbf)	((cbf)->wptr = (cbf)->rptr)
#define CBF_MAKE_DIRTY(cbf)	((cbf)->wptr = (cbf)->rptr + 1)
/*!
 * @brief insert entry into circle buffer
 *
 * @param cbf8		circle buffer
 * @param ent		entry to be inserted
 *
 * @return		return true if inserted
 */
#define CBF_INS(cbf, ret, ent) do {				\
	u32 next = next_cbf_idx_2n((cbf)->wptr, (cbf)->size);	\
	if ((cbf)->rptr == next) {				\
		ret = false;					\
		break;						\
	}							\
	(cbf)->buf[(cbf)->wptr] = (ent);			\
	dmb();\
	(cbf)->wptr = next;					\
	ret = true;						\
} while (0)

#define CBF_GET_ENT(cbf, ent) do {							\
	ent = &((cbf)->buf[(cbf)->wptr]);						\
} while (0)

#define CBF_MOVE_WPTR(cbf) do {								\
	u32 next = next_cbf_idx_2n((cbf)->wptr, (cbf)->size);	\
	__dmb();												\
	(cbf)->wptr = next;										\
} while (0)

/*!
 * @brief insert entry into circle buffer
 *
 * @param cbf8		circle buffer
 * @param ent		entry to be inserted
 *
 * @return		return true if inserted
 */
#define CBF_INS_GC(cbf, ret, ent) do {				\
	u32 next = next_cbf_idx((cbf)->wptr, (cbf)->size);	\
	if ((cbf)->rptr == next) {				\
		ret = false;					\
		break;						\
	}							\
	(cbf)->buf[(cbf)->wptr] = (ent);			\
	dmb();\
	(cbf)->wptr = next; 				\
	ret = true; 					\
} while (0)

/*! @brief get current circle buffer available insert size */
#define cbf_avail_ins_sz(sz, cbf) do {			\
	volatile u32 wptr = (cbf)->wptr;		\
	volatile u32 rptr = (cbf)->rptr;		\
	sz = cbf_distance(wptr, rptr, (cbf)->size);	\
} while (0)

/*!
 * @brief insert entry list into circle buffer
 *
 * @param cbf8		circle buffer
 * @param ins_cnt	return inserted count
 * @param ent		entry list
 * @param cnt		list length
 */
#define CBF_INS_LIST(cbf, ins_cnt, ent, cnt) do {		\
	u32 avail;						\
	cbf_avail_ins_sz(avail, cbf);				\
	u32 c = min(avail, cnt);				\
	u32 n = (cbf)->wptr;					\
	ins_cnt = 0;						\
	if (c == 0)						\
		break;						\
	do {							\
		(cbf)->buf[n] = (ent)[ins_cnt];			\
		n = next_cbf_idx_2n(n, (cbf)->size);		\
		ins_cnt++;					\
	} while (--c > 0);					\
	dmb();\
	(cbf)->wptr = n;					\
} while (0)

/*!
 * @brief fetch one entry in circle buffer
 *
 * @param cbf8		circle buffer
 *
 * @return		return first entry in circle buffer
 */
#define CBF_FETCH(cbf, ret) do {					\
	sys_assert(CBF_EMPTY(cbf) == false);				\
	ret = (cbf)->buf[(cbf)->rptr];					\
	(cbf)->rptr = next_cbf_idx_2n((cbf)->rptr, (cbf)->size);	\
} while (0)

/*!
 * @brief fetch one entry in circle buffer for gc free dtag
 *
 * @param cbf8		circle buffer
 *
 * @return		return first entry in circle buffer
 */
#define CBF_FETCH_GC(cbf, ret) do {					\
	sys_assert(CBF_EMPTY(cbf) == false);				\
	ret = (cbf)->buf[(cbf)->rptr];					\
	(cbf)->rptr = next_cbf_idx((cbf)->rptr, (cbf)->size);	\
} while (0)


/*!
 * @brief circle buffer fetch list, return entey point but not updated rptr
 *
 * @param cbf		circle buffer
 * @param cnt		count array, we may have two entry pointers
 * @param p		entry pointer array
 * @param pcnt		entry pointer count
 */
#define CBF_FETCH_LIST(cbf, cnt, p, pcnt) do {				\
	sys_assert(CBF_EMPTY(cbf) == false);				\
	u32 r = (cbf)->rptr;						\
	u32 w = (cbf)->wptr;						\
	pcnt = 0;							\
	if (r > w) {							\
		cnt[pcnt] = (cbf)->size - r;				\
		p[pcnt] = &(cbf)->buf[r];				\
		pcnt++;							\
		r = 0;							\
	}								\
	if (r < w) {							\
		cnt[pcnt] = w - r;					\
		p[pcnt] = &(cbf)->buf[r];				\
		pcnt++;							\
	}								\
} while (0)

#define CBF_FETCH_LIST_DONE(cbf, cnt) do {				\
	u32 r = (cbf)->rptr;						\
	r += cnt;							\
	if (r >= (cbf)->size)						\
		r -= (cbf)->size;					\
	(cbf)->rptr = r;						\
} while (0)



/*!
 * @brief get head entry in circle buffer
 *
 * @param cbf8		circle buffer
 *
 * @return		return first entry in circle buffer
 */
#define CBF_HEAD(cbf, ret) do {					\
	sys_assert(CBF_EMPTY(cbf) == false);				\
	ret = (cbf)->buf[(cbf)->rptr];					\
} while (0)

/*!
 * @brief remove head entry in circle buffer
 *
 * @param cbf8		circle buffer
 *
 * @return		none
 */
#define CBF_REMOVE_HEAD(cbf) do {					\
	sys_assert(CBF_EMPTY(cbf) == false);				\
	(cbf)->rptr = next_cbf_idx_2n((cbf)->rptr, (cbf)->size);	\
} while (0)

/*! @} */
