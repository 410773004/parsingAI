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

/*! \file btn_helper.h
 * @brief helper function for btn in dispatcher
 *
 * \addtogroup dispatcher
 * \defgroup btn_helper
 * \ingroup dispatcher
 * @{
 * help to insert write read free dtag for btn, currently ramdisk/rawdisk/rdisk use this helper
 */
#pragma once
#include "nvme_spec.h"

/*! \cond PRIVATE */
#pragma push_macro("__FILEID__")
#undef __FILEID__
#define __FILEID__ btnh
/*! \endcond PRIVATE */

#ifdef FORCE_IO_SRAM
#define WR_DTAG_TYPE	DTAG_T_SRAM
#elif FORCE_IO_DDR
#define WR_DTAG_TYPE	DTAG_T_DDR
#else

#ifdef DDR
#define WR_DTAG_TYPE	DTAG_T_DDR
#else
#define WR_DTAG_TYPE	DTAG_T_SRAM	///< normal dtag type used in dispatcher
#endif
#endif

#ifdef MERGE_WA
#define MERGE_DST_TYPE	DTAG_T_SRAM
#else
#define MERGE_DST_TYPE	WR_DTAG_TYPE
#endif

#if defined(RDISK)
#define FLOW_CTRL			///< only rdisk support flow control
#endif

/*!
 * @brief definition of btn command list, use btn_cmd_ex->next_btag for next
 */
typedef struct _bcmd_list_t {
	u16 head;		///< list head
	u16 tail;		///< list tail
} bcmd_list_t;

#define BM_PL_QUE_SZ	(SRAM_IN_DTAG_CNT + 1)	///< use the largest SRAM DTAG COUNT

/*!
 * @brief bm payload queue, used when IO was pended
 */
typedef struct _bm_pl_que_t {
	bm_pl_t que[BM_PL_QUE_SZ];	///< queue buffer
	u16 wptr;				///< write pointer, bm payload was pended
	u16 rptr;				///< bm payload was resumed
	dtag_t dst;				///< destination dtag, INV_DTAG if not allocated yet
} bm_pl_que_t;

fast_data UNUSED static u32 _ua_dtag;			///< unaligned destination dtag buffer, we only need one
fast_data int _free_otf_wd = 0;		///< free dtag count fw pushed to btn for write, and btn didn't return fw
fast_data int _wr_credit = 0;		///< how many dtags need to be pushed to btn for write
fast_data static u32 _free_otf_rd = 0;		///< free dtag count fw pushed to btn for read, and btn didn't return fw
fast_data static u32 _rd_credit = 0;		///< how many dtags need to be pushed to btn for read
fast_data static bool _wr_evt = false;		///< set true if fw already register dtag event for write
fast_data static bool _rd_evt = false;		///< set true if fw already register dtag event for read
fast_data static u32 *dtag_otf_bmp;		///< which dtag was pushed to btn and not return yet, will be removed in A0
extern volatile int _fc_credit; 	///< flow control credit, control how many du could be pushed to btn for write
fast_data UNUSED static bcmd_list_t bcmd_pending = { .head = 0xffff, .tail = 0xffff };	///< pending list for read command

//joe add sec size 20200817
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200817
/*!
 * @brief calculate DU count of request
 *
 * Calculate du count of lba range
 *
 * @param req	request to be executed.
 *
 * @return	DU count of req
 */
fast_code UNUSED static u32 calc_du_cnt(u64 slba, u16 nlb)
{
	int required = 1;
	u8 num_lba=0;
	if(host_sec_bitz==9)
	num_lba=8;//joe add sec size 20200817
	else
	num_lba=1;
	u8 lba_mask=num_lba-1;
    u32 lba_size = nlb;
    if (nlb == 0) {
        lba_size = 0x10000;
    }
	//int lba_ofst = LBA_OFST_LDA(slba);//joe add sec size 20200817
	int lba_ofst=slba &lba_mask;
	//int len = NR_LBA_PER_LDA - lba_ofst;//joe add sec size 20200817
	int len = num_lba- lba_ofst;

	int xfer = lba_size < len ? lba_size : len;

	lba_size -= xfer;
	//required += occupied_by(nlb, NR_LBA_PER_LDA);//joe add sec size 20200817
	required += occupied_by(lba_size, num_lba);

	return required;
}

/*!
 * @brief allocate free dtag for write request, and push to free queue
 *
 * @param dtag_type	dtag type
 * @param required	required count
 *
 * @return		free dtag count pushed
 */
fast_code static int ins_free_dtag_for_wr(int dtag_type, int required)
{
	int pushed = 0;
	int total = required;
	dtag_t dtags[33];	// 128K unit

	do {
		int got;
		u32 cnt = min(required, 32);

		got = (int) dtag_get_bulk(dtag_type, cnt, dtags);

		if (got > 0) {
#if defined(RAWDISK)
			u32 i = 0;
			do {
				// already ignore high bits
				set_bit(dtags[i].b.dtag, dtag_otf_bmp);
			} while (++i < got);
#endif
			bm_free_wr_load(dtags, got);
			_free_otf_wd += got;
			required -= got;
			pushed += got;
		} else {
			break;
		}
	} while (pushed < total);
	return pushed;
}

/*!
 * @brief insert free dtag for read
 *
 * @param dtag_type	dtag type
 * @param required	how many dtags are required
 *
 * @return		how many dtags are pushed
 */
fast_code static u32 ins_free_dtag_for_rd(int dtag_type, u32 required)
{
	u32 pushed = 0;
	u32 total = required;
	dtag_t dtags[33];	// 128K unit

	do {
		u32 got;
		u32 cnt = min(required, 33);

		if (_free_otf_rd >= 128)
			break;

		got = dtag_get_bulk(dtag_type, cnt, dtags);

		if (got > 0) {
			bm_free_rd_load(dtags, got);
			_free_otf_rd += got;
			required -= got;
			pushed += got;
		} else {
			break;
		}
	} while (pushed < total);
	return pushed;
}

/*!
 * @brief reload free write dtags
 *
 * Event handler function for write dtags reload.
 *
 * @param ctx		if not null, it's called by event handler, unset _wr_evt
 *
 * @return		not used
 */
fast_code UNUSED static void reload_free_dtag(void *ctx)
{
	int required;
	int allocated;

#if defined(FLOW_CTRL)
	if (_fc_credit <= 0)
		return;

	required = min(_wr_credit, _fc_credit);
#else
	required = _wr_credit;
#endif
	if (ctx)
		_wr_evt = false;

	if (required <= 0)
		return;

	_wr_credit -= required;
	allocated = ins_free_dtag_for_wr(WR_DTAG_TYPE, required);
#if defined(FLOW_CTRL)
	_fc_credit -= allocated;
#endif

	if (allocated < required) {
		_wr_credit += required - allocated;

		if (_wr_evt == false) {
			_wr_evt = true;
			dtag_register_evt(WR_DTAG_TYPE, reload_free_dtag, &_wr_evt, true);
		}
	}
}

/*!
 * @brief reload free read dtags
 *
 * Event handler function for read dtags reload.
 *
 * @param ctx		if not null, it's called from dtag event handler, unset _rd_evt
 *
 * @return		not used
 */
fast_code static void reload_free_read_dtag(void *ctx)
{
	u32 required = _rd_credit;
	u32 allocated;

	if (ctx)
		_rd_evt = false;

	if (required == 0)
		return;

	_rd_credit = 0;
	allocated = ins_free_dtag_for_rd(WR_DTAG_TYPE, required);

	if (allocated < required) {
		_rd_credit += required - allocated;

		if (_rd_evt == false && _free_otf_rd < 128) {
			_rd_evt = true;
			dtag_register_evt(WR_DTAG_TYPE, reload_free_read_dtag, &_rd_evt, true);
		}
	}
}

/*!
 * @brief load free dtag for write, caller by write command handler, increase _wr_credit only here
 *
 * @param required	how many dtags are required
 *
 * @return		not used
 */
UNUSED fast_code static void load_free_dtag(u32 required)
{
#if CPU_DTAG != CPU_ID && defined(MPC)
	cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_WR_CREDIT, 0, required);
#else
	int allocated;

	#if defined(FLOW_CTRL)
	if (!_fc_credit) {
		allocated = 0;
		goto fc_exit;
	}
	#endif

	if (_wr_credit < 0) {
		int req = (int) required;
		_wr_credit += req;
		if (_wr_credit <= 0)
			return;

		required = (u32) _wr_credit;
		_wr_credit = 0;
	}

	allocated = ins_free_dtag_for_wr(WR_DTAG_TYPE, (int) required);

	#if defined(FLOW_CTRL)
fc_exit:
	_fc_credit -= allocated;
	#endif
	if (allocated < required) {
		_wr_credit += (required - allocated);
		if (_wr_evt == false) {
			_wr_evt = true;
			dtag_register_evt(WR_DTAG_TYPE, reload_free_dtag, &_wr_evt, false);
		}
	}
#endif
}

/*!
 * @brief load free dtag for read, caller by read command handler, increase _rd_credit only here
 *
 * @param required	how many dtags are required
 *
 * @return		not used
 */
UNUSED fast_code static void load_free_read_dtag(u32 required)
{
	u32 allocated;

	allocated = ins_free_dtag_for_rd(WR_DTAG_TYPE, required);

	if (allocated < required) {
		_rd_credit += (required - allocated);
		if (_rd_evt == false) {
			_rd_evt = true;
			dtag_register_evt(WR_DTAG_TYPE, reload_free_read_dtag, &_rd_evt, false);
		}
	}
}

/*!
 * @brief api to get lba count in unaligned head part, use for partial write data entry
 *
 * @param off	lba offset in du, 0~7
 * @param nlb	original command lb count
 *
 * @return	how many lba in first du
 */
fast_code static inline u8 get_ua_head_cnt(u8 off, u16 nlb)
{
	//u8 cnt = NR_LBA_PER_LDA - off;
	u8 cnt=0;
	if(host_sec_bitz==9)
	cnt = 8 - off;//joe add sec size 20200817
	else
	cnt=1-off;
	// consider head and tail are the same DU
	return min(cnt, nlb);
}

/*!
 * @brief api to get lba count in unaligned tail part, use for partial write data entry
 *
 * @param slba		start lba of original command
 * @param nlb		original command lb count
 *
 * @return		how many lba in last du
 */
fast_code static inline u8 get_ua_tail_cnt(u64 slba, u16 nlb)
{
	u64 elba = slba + nlb - 1;
	//u8 cnt = elba & NR_LBA_PER_LDA_MASK;
	u8 num_lba_mask=0;
	if(host_sec_bitz==9)//joe add sec size 20200820
	num_lba_mask=8 - 1;
	else
	num_lba_mask=1- 1;	
	u8 cnt = elba & (num_lba_mask);//joe add sec size 20200817

	return cnt + 1;
}

/*!
 * @brief insert btn command a btn command list
 *
 * @param btag		command tag
 * @param list		btn command list
 *
 * @return		not used
 */
fast_code static inline void bcmd_list_ins(u16 btag, bcmd_list_t *list)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);

	bcmd_ex->next_btag = 0xffff;
	if (list->head == 0xffff) {
		list->head = btag;
		list->tail = btag;
	} else {
		bcmd_ex = btag2bcmd_ex(list->tail);
		sys_assert(bcmd_ex->next_btag == 0xffff);
		bcmd_ex->next_btag = btag;
		list->tail = btag;
	}
}

/*!
 * @brief api to return head of btn command list
 *
 * @return	command tag of head of list
 */
fast_code static inline u16 bcmd_list_head(bcmd_list_t *list)
{
	return list->head;
}

/*!
 * @brief api to pop head of btn command list
 *
 * @param list		btn command list
 *
 * @return		command tag of head of btn command list
 */
fast_code static inline u16 bcmd_list_pop_head(bcmd_list_t *list)
{
	u16 ret = list->head;

	if (ret != 0xffff) {
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(ret);

		list->head = bcmd_ex->next_btag;
		if (list->head == 0xffff)
			list->tail = 0xffff;

	}
	return ret;
}

/*!
 * @brief api to check if btn command list was empty
 *
 * @return	return true if empty
 */
fast_code static inline bool bcmd_list_empty(bcmd_list_t *list)
{
	return (list->head == 0xffff && list->tail == 0xffff);
}

/*!
 * @brief api to move a btn command list to another list
 *
 * @param dst		destination list
 * @param src		source list
 *
 * @return		not used
 */
fast_code static inline void bcmd_list_move(bcmd_list_t *dst, bcmd_list_t *src)
{
	*dst = *src;
	src->tail = src->head = 0xffff;
}

/*!
 * @brief write error event handler function
 *
 * While write error happened, need to pop the error data entry in write error
 * linked list.
 *
 * @param param		not used
 * @param payload	not used
 * @param count		number of data entry to be recycled
 *
 * @return		not used
 */
fast_code static void UNUSED
wd_err_updt(u32 param, u32 btag, u32 count)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);

	_free_otf_wd -= (int) count;
	if (bcmd_ex->du_xfer_left) {
		disp_apl_trace(LOG_ERR, 0xaaa9, "btag %d abort %d, left %d", btag, count, bcmd_ex->du_xfer_left);
		_wr_credit -= (int) bcmd_ex->du_xfer_left;
		bcmd_ex->du_xfer_left = 0;
	}
	disp_apl_trace(LOG_ERR, 0x8209, "--- otf %d cre %d", _free_otf_wd, _wr_credit);
	//sys_assert(_free_otf_wd + _wr_credit == 0);
	// todo: reload free write ?
	// todo: resume IO MGR req
}

/*!
 * @brief reset a on-the-fly dtag from dtag_otf_bmp
 *
 * when pcie reset, some dtag may not be returned by btn, use this bitmap, fw could
 * find those missing free dtags and reset them
 *
 * @param bm_pl		write data entry to be reset
 *
 * @return		not used
 */
fast_code UNUSED static void wd_otf_uptd(bm_pl_t *bm_pl)
{
	u32 dtagid = bm_pl->pl.dtag & (~DTAG_IN_DDR_MASK);
	int ret;
#if DDR
	sys_assert(dtagid < DDR_DTAG_CNT);
#else
	sys_assert(dtagid < SRAM_IN_DTAG_CNT);
#endif
	ret = test_and_clear_bit(dtagid, dtag_otf_bmp);
	sys_assert(ret);
}

/*!
 * @brief API to insert bm payload to queue
 *
 * @param que		queue
 * @param bm_pl		bm payload to be inserted
 *
 * @return		not used
 */
UNUSED fast_code static void bm_pl_que_ins(bm_pl_que_t *que, bm_pl_t *bm_pl)
{
	que->que[que->wptr] = *bm_pl;
	que->wptr++;
	if (que->wptr >= BM_PL_QUE_SZ) {
		que->wptr = 0;
	}
	sys_assert(que->wptr != que->rptr);
}

/*! \cond PRIVATE */
#pragma pop_macro("__FILEID__")
/*! \endcond PRIVATE */
/*! @} */
