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
/*! \file dtag.c
 * @brief Rainier DTAG Manager Module
 *
 * \addtogroup btn
 * \defgroup dtag
 * \ingroup btn
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "btn_precomp.h"
#include "bf_mgr.h"
#include "assert.h"
#include "console.h"
#include "dtag.h"
#include "ddr.h"
#include "mpc.h"
#include "sect.h"

#include "fwconfig.h"

#include "ddr_info.h"
#include "srb.h"
#include "misc.h"

/*! \cond PRIVATE */
#define __FILEID__ dtag
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define DTAG_EVT_REQS_CNT	(256)		///< dtag number for event request

#define DTAG_URGT_CNT		(4)		///< dtag number for urgent requst


#ifdef GET_CFG	//20200822-Eddie
extern u8 *cfg_pos;  //This is position where loader already loaded
extern fw_config_set_t *fw_config_main;
#endif
#ifdef SAVE_DDR_CFG		//20200922-Eddie
	extern ddr_info_t* ddr_info_buf_in_ddr;
	extern fw_config_set_t* fw_config_main_in_ddr;
	extern ddr_info_t ddr_info_buf;
#endif
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
/*!
 * @brief dtag event request definition
 */
typedef struct {
	struct list_head entry;		///< list entry
	void *ctx;			///< caller context
	dtag_evt evt;			///< dtag event callback
} dtag_evt_req_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
fast_data_zi dtag_t _inv_dtag;				///< invalid dtag

fast_data_zi dtag_mgr_t _dtag_mgr[DTAG_T_MAX];		///< dtag manager
fast_data_zi unsigned long _dtag_mgr_sram_bmp[occupied_by(SRAM_IN_DTAG_CNT, BITS_PER_ULONG)];
fast_data_zi atomic_t _dtag_mgr_sram_refs[SRAM_IN_DTAG_CNT];

#if CPU_DTAG == CPU_ID
fast_data_zi unsigned long _dtag_mgr_ddr_bmp[occupied_by(DDR_DTAG_CNT, BITS_PER_ULONG)];
fast_data_zi atomic_t _dtag_mgr_ddr_refs[DDR_DTAG_CNT];
fast_data_zi u8 boot_dtag_recycle_cnt = 0;
#endif

#if defined(HMB_SUPPORT) && CPU_ID == 1
#define HMB_DTAG_CNT	((1024 * 1024) >> DTAG_SHF)
fast_data_zi unsigned long _dtag_mgr_hmb_bmp[occupied_by(HMB_DTAG_CNT,BITS_PER_ULONG)];
fast_data_zi atomic_t _dtag_mgr_hmb_refs[HMB_DTAG_CNT];
#endif

static fast_data_ni dtag_evt_req_t _dtag_evts[DTAG_EVT_REQS_CNT];	///< dtag event request buffer
static fast_data_ni pool_t dtag_evt_pool;				///< dtag event request pool
#if defined(MPC)
static fast_data_zi u32 remote_dtag_require[MPC][DTAG_T_MAX];
static fast_data_zi bool remote_dtag_require_evt_issued[DTAG_T_MAX] = {false, false, false};
#endif

slow_data char ldr_ver[5];
//for fill up
#if (CPU_ID == CPU_FE)
fast_data_ni dtag_pend pend_dtag[pending_dtag_cnt];
fast_data_ni dtag_pend_mgr_t _pend_dtag_mgr;
fast_data dtag_pend_mgr_t* pend_dtag_mgr = &_pend_dtag_mgr;
fast_data_ni u8 dtag_evt_trigger_cnt;
#endif
//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
#if 0	//SAVE_DDR_CFG
slow_code void memprint_(char *str, void *ptr, int mem_len)		//20200714-Eddie
{
	u32*pdata=NULL;
	pdata = ptr;
	data_tag_trace(LOG_ERR, 0x9082, "%s \n",*str);
    int c=0;
	for (c = 0; c < (mem_len/ sizeof(u32)); c++) {
	            if (1) {
	                    if ((c & 3) == 0) {
	                            data_tag_trace(LOG_ERR, 0xe046, "%x:", c << 2);
	                    }
	                    data_tag_trace(LOG_ERR, 0xe03c, "%x ", pdata[c]);
	                    if ((c & 3) == 3) {
	                            data_tag_trace(LOG_ERR, 0xd23b, "\n");
	                    }
	            }
	        }
}
#endif
fast_code void dtag_mgr_destroy(int type)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];

	if (!list_empty(&dtag_mgr->evt_list))
		data_tag_trace(LOG_ERR, 0xfe99, "list is not empty");

	if (dtag_mgr->evt_cnt != 0)
		data_tag_trace(LOG_ERR, 0x1914, "evt count is not zero");

	if (dtag_mgr->added != dtag_mgr->dtag_avail.data)
		data_tag_trace(LOG_ERR, 0x6bd7, "total is not equal to avail");

	sys_free(FAST_DATA, dtag_mgr->dtag_bitmap);
	sys_free(FAST_DATA, dtag_mgr->dtag_refs);
	memset(dtag_mgr, 0, sizeof(*dtag_mgr));
}

fast_code void dtag_mgr_init(int type, u32 total, u32 urgent_cnt)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	u32 i;
	u32 bmp_cnt = 0;

	_inv_dtag.dtag = DTAG_INV;

	if (type == DTAG_T_SRAM) {
		bmp_cnt = SRAM_IN_DTAG_CNT / BITS_PER_ULONG;
		dtag_mgr->dtag_bitmap = _dtag_mgr_sram_bmp;
		dtag_mgr->dtag_refs = _dtag_mgr_sram_refs;
		memset(dtag_mgr->dtag_refs, 0, sizeof(_dtag_mgr_sram_refs));
	} else if (type == DTAG_T_DDR) {
#if CPU_DTAG == CPU_ID
		bmp_cnt = occupied_by(DDR_DTAG_CNT , BITS_PER_ULONG);
		dtag_mgr->dtag_bitmap = _dtag_mgr_ddr_bmp;
		dtag_mgr->dtag_refs = _dtag_mgr_ddr_refs;
		memset(dtag_mgr->dtag_refs, 0, sizeof(_dtag_mgr_ddr_refs));
#else
		panic("wrong setting");
#endif
	} else {
#if defined(HMB_SUPPORT) && CPU_ID == 1
		sys_assert(total <= HMB_DTAG_CNT);
		bmp_cnt = HMB_DTAG_CNT / BITS_PER_ULONG;
		dtag_mgr->dtag_bitmap = _dtag_mgr_hmb_bmp;
		dtag_mgr->dtag_refs = _dtag_mgr_hmb_refs;
		memset(dtag_mgr->dtag_refs, 0, sizeof(_dtag_mgr_hmb_refs));
#else
		panic("wrong setting");
#endif
	}
	dtag_mgr->dtag_avail.data = 0;
	for (i = 0; i < bmp_cnt; i++)
		dtag_mgr->dtag_bitmap[i] = ~0;


	for (i = 0; i < MAX_DTAG_CALLER_NR; i++)
		dtag_mgr->callers_gc[i] = 0;

	dtag_mgr->total = total;
	dtag_mgr->added = 0;
	dtag_mgr->urgent_cnt = urgent_cnt;
	dtag_mgr->evt_cnt = 0;
	dtag_mgr->caller_cnt = 0;
	dtag_mgr->caller_idx = 0;
	dtag_mgr->need_gc_bmp = 0;
	dtag_mgr->gc_avail_bmp = 0;
	INIT_LIST_HEAD(&dtag_mgr->evt_list);
}

/*!
 * @brief private function to release dtag
 *
 * Reduce reference count of dtag, and only release dtag if reference count is
 * zero.
 *
 * @param dtag	dtag to be released
 *
 * @return	not used
 */
 extern volatile u8 plp_trigger;
extern bool ucache_flush_flag;
fast_code static void _dtag_put(int type, dtag_t dtag)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];

	if (dtag.b.dtag >= dtag_mgr->total) {
		data_tag_trace(LOG_ERR, 0xc8a5, "dtag:%d, all:%d", dtag.b.dtag, dtag.dtag);
		sys_assert(0);
	}
	if(dtag_mgr->dtag_refs[dtag.b.dtag].data == 0)
	{
		data_tag_trace(LOG_ERR, 0x8155, "dtag reference err,type %d dtag.all 0x%x refs %d", type, dtag.dtag, dtag_mgr->dtag_refs[dtag.b.dtag].data);
	}
	//sys_assert(dtag.b.dtag < dtag_mgr->total);
    if(!plp_trigger)
        sys_assert(dtag_mgr->dtag_refs[dtag.b.dtag].data);
    else if(plp_trigger && dtag_mgr->dtag_refs[dtag.b.dtag].data == 0)
        data_tag_trace(LOG_ERR, 0xcb4c, "dtag reference err, dtag %d refs %d", dtag.b.dtag, dtag_mgr->dtag_refs[dtag.b.dtag].data);
	atomic_dec(&dtag_mgr->dtag_refs[dtag.b.dtag]);
	if (dtag_mgr->dtag_refs[dtag.b.dtag].data > 0)
		return;

	dtag_mgr->dtag_avail.data++;
	clear_bit(dtag.b.dtag, dtag_mgr->dtag_bitmap);
}

/*!
 * @brief try to free dtag via gc callback of each dtag caller
 *
 * @param need_cnt	required count of dtag
 *
 * @return		not used
 */
fast_code static void dtag_gc(int type, u32 *need_cnt)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	u32 i;

	for (i = 0; i < dtag_mgr->caller_cnt; i++) {
		if (dtag_mgr->callers_gc[dtag_mgr->caller_idx])
			dtag_mgr->callers_gc[dtag_mgr->caller_idx](need_cnt);

		dtag_mgr->caller_idx++;
		if (dtag_mgr->caller_idx == dtag_mgr->caller_cnt)
			dtag_mgr->caller_idx = 0;

		if (*need_cnt == 0)
			break;
	}
}

/*!
 * @brief private function to allocate dtag
 *
 * @param mem	the dtag respective mem if any
 *
 * @return	the allocated dtag or _inv_dtag
 */
fast_code static dtag_t _dtag_get(int type, void **mem)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	dtag_t dtag;

	dtag.dtag = (u32) find_first_zero_bit(dtag_mgr->dtag_bitmap, dtag_mgr->total);
	if (dtag.dtag == dtag_mgr->total)
		return _inv_dtag;

	atomic_dec(&dtag_mgr->dtag_avail);
	atomic_inc(&dtag_mgr->dtag_refs[dtag.dtag]);
    if(dtag_mgr->dtag_refs[dtag.dtag].data != 1)
    {
       data_tag_trace(LOG_ERR, 0xd4f4, "dtag reference err type %d dtag.all 0x%x, data %d", type ,dtag.dtag, dtag_mgr->dtag_refs[dtag.dtag].data);
    }
	set_bit((int) dtag.dtag, dtag_mgr->dtag_bitmap);

	switch (type) {
	case DTAG_T_SRAM:
		if (mem)
			*mem = sdtag2mem(dtag.b.dtag);
		break;
	case DTAG_T_DDR:
		if (mem)
			*mem = (void *) ddtag2mem(dtag.dtag); // need 23 bits
		dtag.b.in_ddr = 1;
		break;
	case DTAG_T_HMB:
		if (mem)
			*mem = NULL; // no memory
		dtag.b.type = 1;
		break;
	};

	//data_tag_trace(LOG_DEBUG, 0, "%d: allocate Dtag(%x) avail(%d)", type,
		//	dtag.dtag, dtag_mgr->dtag_avail.data);

	return dtag;
}

fast_code dtag_t dtag_cont_get(int type, u32 required)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	int tag = 0;
	int off;
	int cnt;
	int i;
	dtag_t dtag;

	off = 0;
	cnt = 0;
	do {
		int next_tag = find_next_zero_bit(dtag_mgr->dtag_bitmap,
				dtag_mgr->total, off);

		if (next_tag == dtag_mgr->total) {
			u32 need_cnt = required;

			dtag_gc(type, &need_cnt);
			if (need_cnt == 0) {
				off = 0;
				cnt = 0;
				continue;
			} else {
				data_tag_trace(LOG_ERR, 0x291e, "%d: no suitable dtag for cont", type);
				return _inv_dtag;
			}
		}

		if (cnt == 0) {
			tag = next_tag;
			cnt++;
		} else if (next_tag != (tag + cnt)) {
			cnt = 1;
			tag = next_tag;
		} else {
			cnt++;
		}
		off = next_tag + 1;
	} while (cnt < (int)required);

	atomic_sub(cnt, &dtag_mgr->dtag_avail);
	for (i = 0; i < cnt; i++) {
		set_bit(tag + i, dtag_mgr->dtag_bitmap);
		atomic_inc(&dtag_mgr->dtag_refs[tag + i]);
	}
	//if plp trigger,dtag_cont_put log will not show
	data_tag_trace(LOG_INFO, 0x78e7, "%d: cont DTAG %d-%d", type, tag, required);
	dtag.dtag = (u32)tag;
#ifdef DDR
	if (type == DTAG_T_DDR)
		dtag.b.in_ddr = 1;
#endif
#ifdef HMB_DTAG
	if (type == DTAG_T_HMB)
		dtag.b.type = 1;
#endif

	return dtag;
}

fast_code void dtag_cont_put(int type, dtag_t dtag, u32 cnt)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	int i;
	int tag;
	u32 c = 0;

	tag = dtag.b.dtag;

	for (i = 0; i < (int) cnt; i++) {

		atomic_dec(&dtag_mgr->dtag_refs[tag + i]);
		if (dtag_mgr->dtag_refs[tag + i].data > 0)
			continue;

		if (!test_and_clear_bit(tag + i, dtag_mgr->dtag_bitmap))
			panic("error in release cont dtag");

		c++;
	}

	if (c == 0)
		return;

	sys_assert(c == cnt);
	atomic_add((int) cnt, &dtag_mgr->dtag_avail);
	if(!plp_trigger)
	{
		data_tag_trace(LOG_INFO, 0x20fa, "%d: recycle cont Dtag(%d)-%d avail(%d) evt(%d)",
				type, dtag.b.dtag, cnt, dtag_mgr->dtag_avail.data,
				dtag_mgr->evt_cnt);
	}
}

fast_code void dtag_ref_inc_bulk(int type, dtag_t *dtag, u32 cnt)
{
	u32 i;

	for (i = 0; i < cnt; i++) {
		type = get_dtag_type(dtag[i]);
		dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
		dtag_mgr->dtag_refs[dtag[i].b.dtag].data++;
	}
}

/*!
 * @brief private function to handle dtag event
 *
 * After dtag was released, handle queued dtag events.
 *
 * @return	not used
 */
 //#if(CPU_ID == 1)
 //extern bool In_Data_Xfer;
 //#endif
fast_code static void _dtag_evt_handle(int type)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	dtag_evt_req_t *ereq;
	dtag_evt evt;
	void *ctx;

redo:
	if ((dtag_mgr->evt_cnt == 0) || (dtag_mgr->dtag_avail.data <= dtag_mgr->urgent_cnt))
		return;
	//#if(CPU_ID == 1)
	//if(In_Data_Xfer)
	//	return;
	//#endif
	ereq = list_first_entry(&dtag_mgr->evt_list, dtag_evt_req_t, entry);
	/*lint -e(530) already initialized */
	list_del_init(&ereq->entry);

	evt = ereq->evt;
	ctx = ereq->ctx;

	dtag_mgr->evt_cnt--;
	pool_put_ex(&dtag_evt_pool, ereq);

	evt(ctx);

	goto redo;
}

fast_code void dtag_put(int type, dtag_t dtag)
{
	_dtag_put(type, dtag);

	/* push dtag event */
	_dtag_evt_handle(type);
}

fast_code dtag_t dtag_get(int type, void **mem)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	dtag_t dtag;
again:
	if (dtag_mgr->dtag_avail.data <= dtag_mgr->urgent_cnt)
		dtag.dtag = _inv_dtag.dtag;
	else
		dtag = _dtag_get(type, mem);

	if (dtag.dtag == _inv_dtag.dtag) {
		u32 need_cnt = 1;

		dtag_gc(type, &need_cnt);

		if (need_cnt == 0)
			goto again;

	}

	return dtag;
}

fast_code dtag_t dtag_get_urgt(int type, void **mem)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	dtag_t dtag;

again:
	if (dtag_mgr->dtag_avail.data == 0)
		dtag.dtag = _inv_dtag.dtag;
	else
		dtag = _dtag_get(type, mem);

	if (dtag.dtag == _inv_dtag.dtag) {
		u32 need_cnt = 1;

		dtag_gc(type, &need_cnt);

		if (need_cnt == 0)
			goto again;
	}

	return dtag;
}

fast_code u32 dtag_get_bulk(int type, u32 required, dtag_t *dtags)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	u32 alloc = 0;
	u32 total_need = required;
	int last_dtag = 0;
	bool gced = false;

again:
	while (required) {
		if (dtag_mgr->dtag_avail.data <= dtag_mgr->urgent_cnt)
			break;

		dtag_mgr->dtag_avail.data--;
		dtags[alloc].dtag = (u32) find_next_zero_bit(
				dtag_mgr->dtag_bitmap, dtag_mgr->total,
				last_dtag);

		/*data_tag_trace(LOG_DEBUG, 0, "%d: allocate Dtag(%d) avail(%d)",
				type, dtags[alloc].dtag, dtag_mgr->dtag_avail.data);*/

		last_dtag = (int) dtags[alloc].dtag;

		u32 mask = 1 << (last_dtag % 32);
		u32 *p = ((u32 *) dtag_mgr->dtag_bitmap) + (last_dtag >> 5);
		*p |= mask;

		dtag_mgr->dtag_refs[last_dtag].data++;
#ifdef DDR
		if (type == DTAG_T_DDR)
			dtags[alloc].b.in_ddr = 1;
#endif
#ifdef HMB_DTAG
		if (type == DTAG_T_HMB)
			dtags[alloc].b.type = 1;
#endif

		alloc++;
		required--;
	}

	if (alloc < total_need && !gced) {
		u32 need_cnt = required;

		gced = true;

		dtag_gc(type, &need_cnt);

		if (need_cnt < required) {
			last_dtag = 0;
			goto again;
		}
	}
	return alloc;
}

fast_code void dtag_put_bulk(int type, u32 recycled, dtag_t *dtags)
{
	u32 i;
	bool ts[DTAG_T_MAX] = { false, false, false };

	for (i  = 0; i < recycled; i++) {
		int t = type;
		dtag_t dtag = dtags[i];

		if (t == DTAG_T_MAX)
			t = get_dtag_type(dtags[i]);

		dtag_mgr_t *dtag_mgr = &_dtag_mgr[t];
		if (--dtag_mgr->dtag_refs[dtag.b.dtag].data > 0)
			continue;

		dtag_mgr->dtag_avail.data++;

		u32 mask = 1 << (dtag.b.dtag % 32);
		u32 *p = ((u32 *)dtag_mgr->dtag_bitmap) + (dtag.b.dtag >> 5);
		*p &= ~mask;

		ts[t] = true;
	}

	for (i = 0; i < DTAG_T_MAX; i++) {
		if (ts[i])
			_dtag_evt_handle(i);
	}
}

fast_code void dtag_add(int type, void *mem, u32 size)
{
	dtag_mgr_t *mgr = &_dtag_mgr[type];
	u32 tag;
	u32 _size = size;
	void *smem = mem;

	if (((u32) mem & DTAG_MSK) || (size & DTAG_MSK)) {
		data_tag_trace(LOG_ERR, 0x7337, "mem(0x%p) or size(0x%x) should be 4096 align", mem, size);
		panic("...");
	}

	while (_size != 0) {
		if (type == DTAG_T_DDR) {
			tag = mem2ddtag(mem);
		} else if (type == DTAG_T_HMB) {
			sys_assert(smem == NULL);
			tag = ((u32) mem) >> DTAG_SHF;
		} else {
			tag = mem2sdtag(mem);
		}
		clear_bit(tag, mgr->dtag_bitmap);
		mgr->dtag_refs[tag].data = 0;  /* initialize ref_count */
		_size -= DTAG_SZE;
		mem = ptr_inc(mem, DTAG_SZE);
		atomic_inc(&mgr->dtag_avail);
		mgr->added++;
	}

	data_tag_trace(LOG_INFO, 0x17af, "%d: mem(0x%p) size(0x%x) #Dtags(%d)", type,
			smem, size, mgr->dtag_avail.data);
}

init_code void dtag_blackout(int type, void *mem, u32 size)
{
	dtag_mgr_t *mgr = &_dtag_mgr[type];
	dtag_t dtag;
	u32 _size = size;
	void *smem = mem;

	sys_assert(((u32) mem & DTAG_MSK) == 0);
	sys_assert((size & DTAG_MSK) == 0);

	while (_size != 0) {
		dtag.b.dtag = sram_to_dma(mem) >> DTAG_SHF;
		if (test_and_set_bit(dtag.b.dtag, mgr->dtag_bitmap)) {
			data_tag_trace(LOG_ERR, 0x1db2, "%d: mem %p Dtag[%d] is taken",
					type, mem, dtag.b.dtag);
			panic("...");
		}
		_size -= DTAG_SZE;
		mem = ptr_inc(mem, DTAG_SZE);
		atomic_inc(&mgr->dtag_refs[dtag.b.dtag]);  /* add ref_count */
		atomic_dec(&mgr->dtag_avail);
	}

	data_tag_trace(LOG_INFO, 0xd84e, "%d: mem(0x%p) size(0x%x) Blacked out, left #Dtags(%d)",
			type, smem, size, mgr->dtag_avail.data);
}

fast_code void dtag_register_evt(int type, dtag_evt evt, void *ctx, bool head)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	dtag_evt_req_t *ereq = pool_get_ex(&dtag_evt_pool);

	if (ereq == NULL)
		panic("Dtag evt pool empty!\n");

	/*data_tag_trace(LOG_DEBUG, 0, "%d: ctx = %p, evt = %d",
			type, ctx, dtag_mgr->evt_cnt);*/

	/*lint -e(613) we already check ereq is null in line 445*/
	INIT_LIST_HEAD(&ereq->entry);
	ereq->ctx = ctx;
	ereq->evt = evt;

	if (head) {
		list_add(&ereq->entry, &dtag_mgr->evt_list);
	} else {
		list_add_tail(&ereq->entry, &dtag_mgr->evt_list);
	}

	dtag_mgr->evt_cnt++;
}

fast_code void dtag_remove_evt(int type, void *ctx)
{
	dtag_mgr_t *dtag_mgr = &_dtag_mgr[type];
	struct list_head *entry;
	struct list_head *entry2;

	list_for_each_safe(entry, entry2, &dtag_mgr->evt_list) {
		dtag_evt_req_t *ereq = container_of(entry, dtag_evt_req_t, entry);

		if (ereq->ctx == ctx) {
			list_del_init(&ereq->entry);
			data_tag_trace(LOG_DEBUG, 0x8598, "%d: dtag evt ctx %p removed", type, ctx);
			pool_put_ex(&dtag_evt_pool, (void*) ereq);
			dtag_mgr->evt_cnt--;
			return;
		}
	}
	data_tag_trace(LOG_WARNING, 0x5d7e, "%d: dtag evt ctx %p not found", type, ctx);
}

fast_code u32 dtag_get_evt_cnt(int type)
{
	return _dtag_mgr[type].evt_cnt;
}
#if (CPU_FE == CPU_ID)
//for fe admin cmd get urg dtag cnt
fast_data bool dtag_get_admin_avail(int type)
{
	bool flag = false;

	if(_dtag_mgr[type].dtag_avail.data > 1)//reseve 1 for fw_download
		flag = true;
	else
		data_tag_trace(LOG_WARNING, 0xf790, "ugr dtag not available for admin cmd");

	return flag;
}
#endif
fast_code bool dtag_get_nrm_avail(int type)
{
	return (_dtag_mgr[type].dtag_avail.data > _dtag_mgr[type].urgent_cnt);
}

fast_code void dtag_set_gc_avail(int type, u8 dtag_caller_id)
{
	_dtag_mgr[type].gc_avail_bmp |= (u8)((u32)1 << dtag_caller_id);
}

fast_code void dtag_clear_gc_avail(int type, u8 dtag_caller_id)
{
	_dtag_mgr[type].gc_avail_bmp &= ~(u8)((u32)1 << dtag_caller_id);
}

fast_code bool dtag_check_gc_avail(int type, u8 dtag_caller_id)
{
	bool gc_avail;

	gc_avail = (_dtag_mgr[type].gc_avail_bmp & ((u32)1 << dtag_caller_id)) ? true : false;

	return gc_avail;
}

fast_code u8 dtag_get_gc_avail(int type)
{
	return _dtag_mgr[type].gc_avail_bmp;
}

init_code u8 dtag_register_caller_gc(int type, dtag_gc_handler gc)
{
	u8 caller_id = _dtag_mgr[type].caller_cnt;

	_dtag_mgr[type].callers_gc[caller_id] = gc;

	_dtag_mgr[type].caller_cnt++;
	sys_assert(_dtag_mgr[type].caller_cnt < MAX_DTAG_CALLER_NR);

	return caller_id;
}

#if CPU_DTAG == CPU_ID
/*!
 * @brief recycle dtags blackout during boot
 *
 * in single CPU build, we just recycle init section
 * in multiple cpu build, we recycle all init section and
 * reserved dtag section if target cpu would like to
 * return them
 *
 * @return	not used
 */
fast_code void boot_dtags_recycle(u32 cpu)
{
	u32 max_recycle_cnt = 1;

	boot_dtag_recycle_cnt++;
#if defined(MPC)
	max_recycle_cnt = MPC;
	switch (cpu) {
	case 0:
		dtag_add(0, &__init_start_0, (u32) &__init_end_0 - (u32) &__init_start_0);
		if (cpu_init_bmp[0].b.dtag_ret)
			dtag_add(0, &__rsvd_dtag_section_start_0, (u32) &__rsvd_dtag_section_end_0 - (u32) &__rsvd_dtag_section_start_0);

		break;

	case 1:
		dtag_add(0, &__init_start_1, (u32) &__init_end_1 - (u32) &__init_start_1);
		if (cpu_init_bmp[1].b.dtag_ret)
			dtag_add(0, &__rsvd_dtag_section_start_1, (u32) &__rsvd_dtag_section_end_1 - (u32) &__rsvd_dtag_section_start_1);

		break;

	case 2:
		dtag_add(0, &__init_start_2, (u32) &__init_end_2 - (u32) &__init_start_2);
		if (cpu_init_bmp[2].b.dtag_ret || 2 >= MPC)
			dtag_add(0, &__rsvd_dtag_section_start_2, (u32) &__rsvd_dtag_section_end_2 - (u32) &__rsvd_dtag_section_start_2);

		break;

	case 3:
		dtag_add(0, &__init_start_3, (u32) &__init_end_3 - (u32) &__init_start_3);
		if (cpu_init_bmp[3].b.dtag_ret || 3 >= MPC)
			dtag_add(0, &__rsvd_dtag_section_start_3, (u32) &__rsvd_dtag_section_end_3 - (u32) &__rsvd_dtag_section_start_3);
		break;

	default:
		data_tag_trace(LOG_ERR, 0x5a35, "error cpu %d", cpu);
		sys_assert(false);
	}
#else
	#if !defined(RAMDISK_FULL)
	dtag_add(0, &__init_start, (u32) &__init_end - (u32) &__init_start);
	#endif
#endif
	if (boot_dtag_recycle_cnt == max_recycle_cnt)
		dtag_add(DTAG_T_SRAM, (void *) &__dtag_fwconfig_start, (u32) &__dtag_fwconfig_end - (u32) &__dtag_fwconfig_start);
}
#else
extern void __attribute__((weak, alias("__boot_dtags_recycle"))) boot_dtags_recycle(u32 cpu);

fast_code void __boot_dtags_recycle(u32 cpu)
{
	cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_DTAG_RECYCLE, 0, cpu);
}
#endif

#if defined(MPC)
fast_code void ipc_boot_dtags_recycle(volatile cpu_msg_req_t *req)
{
	u32 cpu = req->pl;

	boot_dtags_recycle(cpu);
}

fast_code void ipc_dtag_get_sync(volatile cpu_msg_req_t *req)
{
	volatile u32 *ret = (u32 *)req->pl;
	dtag_t dtag;

	dtag = dtag_get(req->cmd.flags, NULL);
	*ret = dtag.dtag;
	cpu_msg_sync_done(req->cmd.tx);
	__dmb();
}

fast_code void remote_dtag_evt(void *ctx)
{
	u32 i, j;

	for (j = 0; j < DTAG_T_MAX; j++) {
		remote_dtag_require_evt_issued[j] = false;
		for (i = 0; i < MPC; i++) {
			if (remote_dtag_require[i][j] == 0)
				continue;

			while (remote_dtag_require[i][j]) {
				dtag_t dtag = dtag_get(j, NULL);

				if (dtag.dtag != _inv_dtag.dtag) {
					cpu_msg_issue(i, CPU_MSG_DTAG_GET_ASYNC_DONE, 0, (u32) dtag.dtag);
					remote_dtag_require[i][j]--;
				} else {
					if (!remote_dtag_require_evt_issued[j]) {
						dtag_register_evt(j, remote_dtag_evt, NULL, true);
						remote_dtag_require_evt_issued[j] = true;
					}
					break;
				}
			}
		}
	}
}

fast_code void ipc_dtag_get_async(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = dtag_get(req->cmd.flags, NULL);

	if (dtag.dtag == _inv_dtag.dtag) {
		remote_dtag_require[req->cmd.tx][req->cmd.flags]++;
		if (!remote_dtag_require_evt_issued[req->cmd.flags]) {
			dtag_register_evt(req->cmd.flags, remote_dtag_evt, NULL, true);
			remote_dtag_require_evt_issued[req->cmd.flags] = true;
		}
	} else {
		cpu_msg_issue(req->cmd.tx, CPU_MSG_DTAG_GET_ASYNC_DONE, 0, (u32) dtag.dtag);
	}
}
#endif

/*!
 * @brief dtag resource initialization
 *
 * initialize SRAM dtag module, and blackout boot SRAM
 * initialize DDR here, consider to move it to RTOS
 *
 * in multiple CPU build, initialize DDR dtag here
 */
init_code void dtag_init(void)
{
	_inv_dtag.dtag = DTAG_INV;

	pool_init(&dtag_evt_pool, (void *)_dtag_evts,
		  sizeof(_dtag_evts), sizeof(dtag_evt_req_t),
		  DTAG_EVT_REQS_CNT);

#if defined(MPC)
	#if CPU_DTAG == CPU_ID
	dtag_mgr_init(DTAG_T_SRAM, SRAM_IN_DTAG_CNT, DTAG_URGT_CNT);
	dtag_add(DTAG_T_SRAM, &__dtag_mem_start,
		 (u32) &__dtag_mem_end - (u32) &__dtag_mem_start);

	// com buff in ROM should be always 4K
	dtag_blackout(DTAG_T_SRAM, &__dtag_fwconfig_start,
			(u32) &__dtag_fwconfig_end - (u32) &__dtag_fwconfig_start);

	dtag_blackout(DTAG_T_SRAM, &__init_start_0,
			(u32) &__init_end_0 - (u32) &__init_start_0);
	dtag_blackout(DTAG_T_SRAM, &__init_start_1,
			(u32) &__init_end_1 - (u32) &__init_start_1);
	dtag_blackout(DTAG_T_SRAM, &__init_start_2,
			(u32) &__init_end_2 - (u32) &__init_start_2);
	dtag_blackout(DTAG_T_SRAM, &__init_start_3,
			(u32) &__init_end_3 - (u32) &__init_start_3);

	local_item_done(dtag_ret);

	dtag_blackout(DTAG_T_SRAM, &__rsvd_dtag_section_start_0,
				(u32) &__rsvd_dtag_section_end_0 - (u32) &__rsvd_dtag_section_start_0);
	dtag_blackout(DTAG_T_SRAM, &__rsvd_dtag_section_start_1,
				(u32) &__rsvd_dtag_section_end_1 - (u32) &__rsvd_dtag_section_start_1);
	dtag_blackout(DTAG_T_SRAM, &__rsvd_dtag_section_start_2,
				(u32) &__rsvd_dtag_section_end_2 - (u32) &__rsvd_dtag_section_start_2);
	dtag_blackout(DTAG_T_SRAM, &__rsvd_dtag_section_start_3,
				(u32) &__rsvd_dtag_section_end_3 - (u32) &__rsvd_dtag_section_start_3);
	#if defined(RDISK)
	dtag_blackout(DTAG_T_SRAM, &__evlog_dtag_start,
			 (u32) &__evlog_dtag_end - (u32) &__evlog_dtag_start);
	#endif

	#if defined(MPC)
	dtag_blackout(DTAG_T_SRAM, &__dtag_stream_read_start,
				(u32) &__dtag_stream_read_end - (u32) &__dtag_stream_read_start);
	#endif

	#if defined(SEMI_WRITE_ENABLE) && defined(RDISK)
	dtag_blackout(DTAG_T_SRAM, &__dtag_stream_write_ex_start,
				(u32) &__dtag_stream_write_ex_end - (u32) &__dtag_stream_write_ex_start);
	dtag_blackout(DTAG_T_SRAM, &__dtag_stream_write_start,
				(u32) &__dtag_stream_write_end - (u32) &__dtag_stream_write_start);
	#endif
	cpu_msg_register(CPU_MSG_DTAG_RECYCLE, ipc_boot_dtags_recycle);
	cpu_msg_register(CPU_MSG_DTAG_GET_SYNC, ipc_dtag_get_sync);
	cpu_msg_register(CPU_MSG_DTAG_GET_ASYNC, ipc_dtag_get_async);

	u32 i, j;
	for (j = 0; j < DTAG_T_MAX; j++)
		for (i = 0; i < MPC; i++)
			remote_dtag_require[i][j] = 0;
	#else
    #if(CPU_ID == CPU_FE)
	dtag_mgr_init(DTAG_T_SRAM, SRAM_IN_DTAG_CNT, DTAG_URGT_CNT);
    #else
	dtag_mgr_init(DTAG_T_SRAM, SRAM_IN_DTAG_CNT, 0);
    #endif
	dtag_add(DTAG_T_SRAM, &___rsvd_dtag_section_start,
				(u32)&___rsvd_dtag_section_end - (u32)&___rsvd_dtag_section_start);
	#endif
#else
	dtag_mgr_init(DTAG_T_SRAM, SRAM_IN_DTAG_CNT, DTAG_URGT_CNT);
	dtag_add(DTAG_T_SRAM, &__dtag_mem_start,
		 (u32) &__dtag_mem_end - (u32) &__dtag_mem_start);

	dtag_blackout(DTAG_T_SRAM, &__init_start,
			(u32) &__init_end - (u32) &__init_start);

	dtag_blackout(DTAG_T_SRAM, &__dtag_fwconfig_start,
			(u32) &__dtag_fwconfig_end - (u32) &__dtag_fwconfig_start);
#endif

#if defined(DDR) && !defined(PROGRAMMER)
	if (CPU_ID == 1) {
		srb_t *srb = (srb_t *) SRAM_BASE;		//20201020-Eddie
		evlog_printk(LOG_ALW, "PGR Ver : %s , Loader Ver : %s\n",srb->pgr_ver,srb->ldr_ver);
		memcpy(ldr_ver,srb->ldr_ver,strlen(srb->ldr_ver));

		#ifdef GET_CFG	//20200822-Eddie
			fw_config_main = (fw_config_set_t*) cfg_pos;
		#endif
	#ifdef BYPASS_DDRINIT_WARMBOOT
		if (misc_is_warm_boot() == true){		//20201028-Eddie
			data_tag_trace(LOG_ERR, 0xab9d, "Bypass ddr init in warm_boot\n");
			ddr_init_bypass_warmboot();
		}else
	#endif
		{
		#ifdef MOVE_DDRINIT_2_LOADER
			data_tag_trace(LOG_ERR, 0xb279, "ddr_init_complement \n");
			ddr_init_complement();
		#else
			ddr_init();  //move to Loader
		#endif
		}
		#ifdef SAVE_DDR_CFG		//20201008-Eddie
			//memprint_("ddr_info_buf",&ddr_info_buf,320);
			u32 ddr_info_buf_ddtag = ddr_dtag_register(1);
			ddr_info_buf_in_ddr = (ddr_info_t*)ddtag2mem(ddr_info_buf_ddtag);
			memcpy((void*) ddr_info_buf_in_ddr,(void*)&ddr_info_buf,sizeof(ddr_info_t));
			if (ddr_info_buf_in_ddr->info_need_update){
				data_tag_trace(LOG_INFO, 0xe4e5, "[NP-2] ddr_info_buf_in_ddr->info_need_update");
			}
			u32 fw_config_main_ddtag = ddr_dtag_register(1);
			fw_config_main_in_ddr =  (fw_config_set_t*)ddtag2mem(fw_config_main_ddtag);
			memcpy((void*)fw_config_main_in_ddr,(void*)fw_config_main,sizeof(fw_config_set_t));
		#endif
#if defined(MPC)
		local_item_done(ddr_init);
#endif
	}
#if defined(RDISK)
	else {
		wait_remote_item_done_no_poll(ddr_init);
	}
#endif

#if !defined(RAMDISK_FULL)
extern u32 ddr_cache_cnt;
	if (CPU_DTAG == CPU_ID) {
		#ifdef RAMDISK
		u32 sz = DDR_DTAG_CNT * 2;
		#else
		u32 sz = ddr_cache_cnt;
		#endif
		dtag_mgr_init(DTAG_T_DDR, sz, 4);
		dtag_add(DTAG_T_DDR, (void *) ddtag2mem(DDR_IO_DTAG_START), sz << DTAG_SHF);
	}
#endif

#endif

#if defined(MPC) && (CPU_ID == CPU_FE)
	INIT_LIST_HEAD(&pend_dtag_mgr->entry1);
	INIT_LIST_HEAD(&pend_dtag_mgr->entry2);
	pend_dtag_mgr->free_cnt = pend_dtag_mgr->total = pending_dtag_cnt;

	for(u8 cnt=0;cnt<pending_dtag_cnt;cnt++)
	{
		pend_dtag[cnt].index = cnt;
		INIT_LIST_HEAD(&pend_dtag[cnt].pending);
		list_add_tail(&pend_dtag[cnt].pending, &pend_dtag_mgr->entry2);
	}

#endif
}

module_init(dtag_init, BTN_PRE);

/*! \cond PRIVATE */
/*lint -e(715) param is not used */
static ps_code int dtags_main(int argc, char *argv[])
{
	int i;

	for (i = 0; i < DTAG_T_MAX; i++) {
		data_tag_trace(LOG_ERR, 0xb900, "\n# of Dtags(%d/%d)\n", _dtag_mgr[i].dtag_avail.data, _dtag_mgr[i].added);
		data_tag_trace(LOG_ERR, 0xa67f, "# of Events(%d)", _dtag_mgr[i].evt_cnt);
	}
	return 0;
}


static DEFINE_UART_CMD(dtags, "dtags", "dtags", "dtags", 0, 0, dtags_main);
/*! \endcond */

/*! @} */
