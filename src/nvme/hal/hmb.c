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
/*! \file
 * @brief Rainier HMB Module
 *
 * \addtogroup hal
 * \defgroup HMB
 * \ingroup hal
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "assert.h"
#include "io.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "event.h"
#include "bf_mgr.h"
#include "nvme_spec.h"
#include "pf_reg.h"
#include "hmb.h"
#include "hmb_cmd.h"
#include "hal_nvme.h"
#include "req.h"
#include "cmd_proc.h"
#include "port_ctrl.h"

/*! \cond PRIVATE */
#define __FILEID__ hmb
#include "trace.h"
/*! \endcond */

#if (HOST_NVME_FEATURE_HMB == FEATURE_SUPPORTED)

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#if defined(TFW_CTRL_HMB) || defined(TFW_CTRL_NS_HMB)
#define AUTO_LOOKUP_ENABLE 1
#else
#define AUTO_LOOKUP_ENABLE 0 ///< macro to control auto lookup feature enable
#endif

#define HMB_ENCRYPTION_ENABLE 1	  ///< enable HMB encryption
#define HMB_REQ_BUF_LOC FAST_DATA ///< macro to control request buffer location

#define HMB_RESP_BUF_LOC FAST_DATA		///< response buffer location
#define HMB_AUTO_RESP_BUF_LOC FAST_DATA ///< auto lookup reponse buffer location
#define MB_SFT (20)						///< megabytes shift
#define HMB_CMD_DW_SIZE (4)				///< HMB command dword size

#define HMB_TU_CNT(mb) ((mb << MB_SFT) >> 12) ///< HMB table unit count in page size

#define CRC_SZ_PER_TU (32)	  ///< crc size per table unit, crc16 per 512 bytes
#define CRC_SZ_PER_TU_SFT (5) ///< crc size per table unit shit

#define HMB_META_ENT_SZ (8)				  ///< HMB meta entry size
#define MIN_SEG_SZ_SFT (MB_SFT)			  ///< minimal HMB segment shift, 1MB
#define MIN_SEG_SZ (1 << MIN_SEG_SZ_SFT)  ///< minimal HMB segment size
#define HMB_TBL_SZ_ALIGN(x) (x & (4 - 1)) ///< HMB table size should be 4MB aligned

#define ring_distanace(s, e, n) \
	(((e) >= (s)) ? ((e) - (s)) : (((n) - (s)) + (e))) ///< distance between s and e

#define next_ring_index(s, n) \
	((((s) + 1) == n) ? 0 : ((s) + 1)) ///< next ring buffer index

#define HMB_MEM_SET_SHIFT (13)					  ///< unit to set host memory shift
#define HMB_MEM_SET_SIZE (1 << HMB_MEM_SET_SHIFT) ///< unit to set host memory, used to clear meta segment

#define HMB_UNMAP_CHK(pda) ((pda == ~0) ? UNMAP_PDA : pda) ///< check if PDA was unmap

#define HMB_REG_DW_OFF(x) ((x - HMB_NAND_TBL_CTRL) >> 2) ///< get dword index in backup register array

#define hmb_reg_ps_restore(reg) \
	hmb_writel(ps_hmb_regs[HMB_REG_DW_OFF(reg)], reg) ///< restore hmb register by backup regsiter

#define hmb_backup_reg(reg) (ps_hmb_regs[HMB_REG_DW_OFF(reg)]) ///< get backup register value

#define hmb_reg_resume_chk(reg) \
	((hmb_readl(reg) != hmb_backup_reg(reg)) ? false : true) ///< check if hmb register was equal to backup register

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
/*!
 * @brief HMB queue structure for command and response
 */
typedef struct _hmb_queue_t
{
	void *queue; ///< queue buffer
	u16 wptr;	 ///< current write pointer
	u16 cnt;	 ///< queue size, how many elements in queue
	u32 qid;	 ///< queue id, equal to namespace id
} hmb_queue_t;

/*!
 * @brief HMB context when initialization
 */
typedef struct _hmb_ctx_t
{
	struct nvme_host_memory_desc_entry *list;  ///< host memory segments list from host
	struct nvme_host_memory_desc_entry *ilist; ///< internal host memory segments list
	u32 list_cnt;							   ///< count of HMB segments
	u32 page_bits;							   ///< nvme page bits
	req_t *req;								   ///< set feature request
} hmb_ctx_t;

/*!
 * @brief context to issue reset HMB segment in host
 */
typedef struct _hmb_mem_reset_t
{
	void *zero;					   ///< zero buffer to reset meta segment
	u32 buf_size;				   ///< zero buffer size
	u32 list_idx;				   ///< current zero segment index in list
	u32 iter;					   ///< current zero iteration in one segment
	u32 seg_size;				   ///< segment remain size to be zero
	hmb_ctx_t *ctx;				   ///< hmb initialization context
	void (*cmpl)(struct _req_t *); ///< hmb memory reset completion callback function
} hmb_mem_reset_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//----------------------------------------------------------------------------
static fast_data hmb_queue_t _hmb_cmd;		 ///< HMB command queue
static fast_data hmb_queue_t _hmb_resp;		 ///< HMB response queue
static fast_data hmb_queue_t _hmb_auto_resp; ///< HMB auto lookup reponse queue
#ifdef RAMDISK_2NS
static fast_data hmb_queue_t _hmb_resp_ns0;		 ///< HMB response queue
static fast_data hmb_queue_t _hmb_auto_resp_ns0; ///< HMB auto lookup reponse queue
#endif
static fast_data hmb_cmd_cb_t _hmb_cbs[HMB_CMD_CNT - 1]; ///< HMB command callback context
static fast_data hmb_auto_lkp_rst_t *_hmb_auto_lkp_rst;	 ///< HMB auto lookup command result buffer
static fast_data u32 sfa[HMB_AUTO_CMD_CNT];				 ///< pda buffer for 4K RR auto lookup result
static fast_data u32 _hmb_cmd_otf;						 ///< running HMB commands
static fast_data u32 _evt_hmb_detach = ~0;				 ///< ctag of HMB returning request (set feature)
static fast_data u16 _hmb_meta_seg;						 ///< HMB meta segment ID
static fast_data u16 hmb_buf_seg_end;					 ///< HMB dtag buffer segment end
static fast_data u16 _hmb_dmeta_seg;					 ///< HMB dtag meta segment ID
static fast_data u16 _hmb_ecc_seg;						 ///< HMB dtag ECC segment ID
static fast_data u16 _hmb_crc_seg;						 ///< HMB CRC segment ID
static fast_data dtag_t zero_tag;						 ///< dtag to clear meta segment
fast_data pool_t hmb_cbs_pool;							 ///< HMB callback context pool

fast_data u8 wait_for_auto_lkp = 0;	   ///< how many IOs are waiting for auto lookup result
fast_data u8 evt_auto_lkp_done = 0xFF; ///< auto lookup done event
fast_data u8 free_cb_cnt;			   ///< free hmb callback context count
fast_data u32 free_resp_cnt;		   ///< free hmb response count
fast_data u32 tbl_cnt;				   ///< number of table
fast_data bool hmb_enabled = false;	   ///< enable HMB or not

extern u8 evt_hmb_cmd_done;		 ///< HMB command done event
extern u8 evt_hmb_auto_cmd_done; ///< HMB auto lookup command done event
extern u8 evt_cmd_done;			 ///< return hmb admin code by this event

fast_data u8 *ps_hmb_sz_tbl = NULL;	   ///< power safe hmb segment size table
fast_data u64 *ps_hmb_addr_tbl = NULL; ///< power safe hmb segment address table
fast_data u32 ps_hmb_tbl_cnt;		   ///< power safe hmb segment count
fast_data u32 *ps_hmb_regs = NULL;	   ///< power safe hmb register

//joe add sec size 20200817
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200817

extern req_t *cmd_proc_ctag2req(int ctag);
extern req_t *rawdisk_ctag2req(int ctag);
extern fast_data u32 cmdproc_vac;

#define HMB_BASE (NVME_BASE)
#define HMB_DTAG_NUM (1024)

//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
/*!
 * @brief interface to write HMB register
 *
 * @param val	value to be written
 * @param reg	register index to be written
 *
 * @return	None
 */
static inline void hmb_writel(u32 val, u32 reg)
{
	writel(val, ((void *)(HMB_BASE + reg)));
}

/*!
 * @brief interface to write 2 HMB register
 *
 * @param val	value to be written
 * @param reg	register index to be written
 *
 * @return	None
 */
static inline void hmb_writell(u64 val, u32 reg)
{
	u32 v;

	v = val & 0xffffffff;
	hmb_writel(v, reg);

	v = (val >> 32);
	hmb_writel(v, reg + 4);
}

/*!
 * @brief clear _hmb_cmd_otf and wait_for_auto_lkp
 *
 * @return	None
 */
void clear_hmb_cmd_otf()
{
	_hmb_cmd_otf = 0;
	wait_for_auto_lkp = 0;
}

/*!
 * @brief interface to read HMB register
 *
 * @param reg	register index to be read
 *
 * @return	current value in register
 */
static inline u32 hmb_readl(u32 reg)
{
	return readl(((void *)(HMB_BASE + reg)));
}

/*!
 * @brief setup HMB queue and initialize HMB hardware register
 *
 * Allocate memory for HMB queue, and assigned queue buffer pointer to hardware
 * register and initialize hardware read/write pointer.
 *
 * @param queue		HMB queue pointer
 * @param cmd_cnt	how many element in queue
 * @param cmd_base_addr	HMB queue base register index
 * @param type		memory type of HMB queue buffer
 *
 * @return		None
 */
static fast_code void hmb_hal_queue_setup(hmb_queue_t *queue, u32 cmd_cnt,
										  u32 cmd_base_addr, mem_type_t type, u32 qid)
{
	u32 val;
	u32 queue_byte_sz;

	queue->qid = qid;
	queue_byte_sz = HMB_CMD_DW_SIZE * cmd_cnt * sizeof(u32);

	queue->queue = sys_malloc_aligned(type, queue_byte_sz, 32);
	sys_assert(queue->queue);

	/* for debug */
	memset(queue->queue, 0xff, queue_byte_sz);
	if (type == SLOW_DATA)
	{
		val = sram_to_dma(queue->queue);
	}
	else
	{
		val = btcm_to_dma(queue->queue);
		val |= FW_NTBL_ACCESS_RSP0_X_DTCM_MASK;
	}

	hmb_writel(val, cmd_base_addr);
	/* the value of pointer to wrap around, 12bit */
	hmb_writel(cmd_cnt - 1, cmd_base_addr + 4);
	/* clear access pointer */
	hmb_writel(0, cmd_base_addr + 8);

	queue->cnt = cmd_cnt;
	queue->wptr = 0;
}

/*!
 * @brief add write pointer of HMB queue
 *
 * Set HMB hardware write pointer by add cnt to trigger hardware.
 *
 * @param queue		HMB queue pointer
 * @param reg_addr	HMB hardware read/write pointer register index
 * @param cnt		increment of write pointer
 *
 * @return		None
 */
static inline void advance_queue_ptr(hmb_queue_t *queue, u32 reg_addr, u32 cnt)
{
	fw_ntbl_access_req_pointers_t ptr;

	queue->wptr += cnt;

	if (queue->wptr >= queue->cnt)
		queue->wptr -= queue->cnt;

	ptr.b.fw_ntbl_access_req_wptr = queue->wptr;
	hmb_writel(ptr.all, reg_addr);
}

/*!
 * @brief get next write pointer position
 *
 * Check if there are commands free to be used.
 *
 * @param queue		queue_buffer
 * @param reg_addr	HMB hardware  read/write pointer register index
 * @param pos		offset of next write pointer, must >= 0
 *
 * @return		Return next write pointer
 */
static fast_code u32 get_next_queue_ptr(hmb_queue_t *queue,
										u32 reg_addr, u32 pos)
{
	fw_ntbl_access_req_pointers_t ptr;
	u32 end_ptr;

	ptr.all = hmb_readl(reg_addr);

	end_ptr = ptr.b.fw_ntbl_access_req_rptr - 1;
	if (end_ptr < queue->wptr)
		end_ptr += queue->cnt;

	if (queue->wptr + pos == end_ptr)
		return ~0;

	if ((queue->wptr + pos) >= queue->cnt)
		return queue->wptr + pos - queue->cnt;

	return queue->wptr + pos;
}

/*!
 * @brief release a hmb queue buffer
 *
 * @param queue		HMB queue pointer
 * @param type		memory type of HMB queue buffer
 *
 * @return		None
 */
static fast_code void hmb_hal_queue_destory(hmb_queue_t *queue, mem_type_t type)
{
	sys_free_aligned(type, queue->queue);
}

/*!
 * @brief find a suitable segment for target size
 *
 * Search list to find a segment which has minimal size for target size, and
 * avoid to find CRC segment and META segment.
 *
 * @param list		host memory descriptor list
 * @param list_cnt	list length
 * @param tar_sz	target size to be found, unit: page size
 * @param page_bits	controller page size shift
 *
 * @return		Return index of suitable segment, or ~0
 */
static fast_code u32 hmb_find_suit_seg(
	struct nvme_host_memory_desc_entry *list, u32 list_cnt,
	u32 tar_sz, u32 page_bits)
{
	u32 i;
	u32 ret = ~0;
	u32 min_size = ~0;

	/* tar_sz = (N << page_bits) bytes, skip buf seg */
	for (i = hmb_buf_seg_end; i < list_cnt; i++)
	{
		if (i == _hmb_crc_seg || i == _hmb_meta_seg || i == _hmb_dmeta_seg || i == _hmb_ecc_seg)
			continue;

		if (list[i].bsize == tar_sz)
		{
			ret = i;
			break;
		}

		if (list[i].bsize > tar_sz)
		{
			if (min_size > list[i].bsize)
			{
				min_size = list[i].bsize;
				ret = i;
			}
		}
	}

	sys_assert(ret != ~0);
	// split entry
	if (list[ret].bsize > tar_sz)
	{
		u32 si = list_cnt - 4;
		u32 hi;
		u32 lo;

		do
		{
			if (list[si].bsize == 0)
				break;
		} while (++si < list_cnt);

		list[si] = list[ret];
		list[si].bsize = tar_sz;

		lo = list[si].badd;
		hi = list[si].badd >> 32;
		hmb_hal_trace(LOG_INFO, 0x4038, "new seg[%d]: addr %x%x %d",
					  si, hi, lo, list[si].bsize);

		list[ret].badd += tar_sz << page_bits;
		lo = list[ret].badd;
		hi = list[ret].badd >> 32;
		list[ret].bsize -= tar_sz;
		hmb_hal_trace(LOG_INFO, 0x669e, "new seg[%d]: addr %x%x %d",
					  ret, hi, lo, list[ret].bsize);

		ret = si;
	}
	return ret;
}

/*!
 * @brief enable HMB commands
 *
 * Initialize HMB command queue and response data structure and hardware.
 *
 * @param cmd_cnt	how many commands of HMB command pool
 *
 * @return		None
 */
static fast_code void hmb_cmd_enable(u32 cmd_cnt)
{
	fw_ntbl_access_ctrl_sts_t ctrl;

	hmb_hal_queue_setup(&_hmb_cmd, cmd_cnt, FW_NTBL_ACCESS_REQ_SRAM_BASE,
						HMB_REQ_BUF_LOC, 0); //request fw_ntbl_access_req_sram_base 0x2234
	/* there may be one command which has four completion entry */
	hmb_hal_queue_setup(&_hmb_resp, HMB_RESP_CNT, FW_NTBL_ACCESS_RES0_SRAM_BASE,
						HMB_RESP_BUF_LOC, 1); //response fw_ntbl_access_res0_sram_base 0x223C

#ifdef RAMDISK_2NS
	hmb_hal_queue_setup(&_hmb_resp_ns0, HMB_RESP_CNT, FW_NTBL_ACCESS_RES1_SRAM_BASE,
						HMB_RESP_BUF_LOC, 0);
#endif

	pool_init(&hmb_cbs_pool, (void *)&_hmb_cbs[0], sizeof(_hmb_cbs),
			  sizeof(hmb_cmd_cb_t), HMB_CMD_CNT - 1);
	free_cb_cnt = HMB_CMD_CNT - 1;
	free_resp_cnt = HMB_RESP_CNT - 4;

	/* enable */
	ctrl.all = 0;
	ctrl.b.fw_ntbl_access_en = 1;
	ctrl.b.fwhmb_rsp0_dtcm_en = (HMB_RESP_BUF_LOC == FAST_DATA);
#ifdef RAMDISK_2NS
	ctrl.b.fwhmb_rsp1_dtcm_en = (HMB_RESP_BUF_LOC == FAST_DATA);
#endif

	_hmb_cmd_otf = 0;

	hmb_writel(ctrl.all, FW_NTBL_ACCESS_CTRL_STS);
}

#if AUTO_LOOKUP_ENABLE
/*!
 * @brief enable auto lookup command
 *
 * Initialize auto lookup response queue, the queue size is 4 * cmd_cnt, since
 * one command may have four responses at most.
 * Prefetch count could be used to lookup more results than host command
 * requested.
 *
 * @param cmd_cnt		host command count
 * @param prefetch_cnt		enable prefetch for auto lookup feature
 *
 * @return			None
 */
static fast_code void hmb_auto_cmd_enable(u32 cmd_cnt,
										  u32 prefetch_cnt)
{
	hmb_ntbl_auto_lkup_ctrl_sts_t ctrl;
	u32 size;

#ifdef RAMDISK_2NS
	hmb_hal_queue_setup(&_hmb_auto_resp_ns0, HMB_AUTO_RESP_CNT,
						AUTO_LKUP_RES1_SRAM_BASE, HMB_AUTO_RESP_BUF_LOC, 0);
#endif
	hmb_hal_queue_setup(&_hmb_auto_resp, HMB_AUTO_RESP_CNT,
						AUTO_LKUP_RES0_SRAM_BASE, HMB_AUTO_RESP_BUF_LOC, 1);

	ctrl.all = 0;
	ctrl.b.hmb_ntbl_autolkup_enable = 1;
	ctrl.b.auto_lkup_prefetch_enable = prefetch_cnt ? 1 : 0;
	ctrl.b.max_prefecth_npa_num = prefetch_cnt;
	ctrl.b.read_cmd_ntbl_lkup_en = 1;
	ctrl.b.par_wr_cmd_ntbl_lkup_en = 0;
	ctrl.b.alkup_cmd_tag_sel = 1;
	ctrl.b.auto_lkup_rsp0_dtcm_en = (HMB_AUTO_RESP_BUF_LOC == FAST_DATA) ? 1 : 0;
#ifdef RAMDISK_2NS
	ctrl.b.auto_lkup_rsp1_dtcm_en = (HMB_AUTO_RESP_BUF_LOC == FAST_DATA) ? 1 : 0;
#endif

	size = sizeof(hmb_auto_lkp_rst_t) * cmd_cnt;
	_hmb_auto_lkp_rst = sys_malloc(FAST_DATA, size);
	sys_assert(_hmb_auto_lkp_rst);

	hmb_writel(ctrl.all, HMB_NTBL_AUTO_LKUP_CTRL_STS);
}
#endif

/*!
 * @brief disable HMB commands
 *
 * Release HMB command and response buffer and reset hardware.
 *
 * @return	None
 */
static fast_code void hmb_cmd_disable(void)
{
	hmb_writel(0, FW_NTBL_ACCESS_CTRL_STS);
	hmb_writel(0, FW_NTBL_ACCESS_REQ_SRAM_BASE);
	hmb_writel(0, FW_NTBL_ACCESS_REQ_POINTERS);

	hmb_hal_queue_destory(&_hmb_cmd, HMB_REQ_BUF_LOC);
	hmb_hal_queue_destory(&_hmb_resp, HMB_RESP_BUF_LOC);
}

/*!
 * @brief Disable HMB auto lookup commands
 *
 * Release HMB auto lookup response buffer and reset hardware.
 *
 * @return	None
 */
#if AUTO_LOOKUP_ENABLE == 1
static fast_code void hmb_auto_cmd_disable(void)
{
	hmb_writel(0, HMB_NTBL_AUTO_LKUP_CTRL_STS);
	hmb_writel(0, AUTO_LKUP_RES0_SRAM_BASE);
	hmb_writel(0, AUTO_LOOKUP_RES0_PTRS);

	sys_free(FAST_DATA, _hmb_auto_lkp_rst);
	hmb_hal_queue_destory(&_hmb_auto_resp, HMB_AUTO_RESP_BUF_LOC);
}
#endif

/*!
 * @brief general HMB command completion handler
 *
 * Copy results of HMB response to HMB command callback context, and call
 * completion of caller to return upper layer.
 *
 * @param resp		response of HMB command
 * @param ent		HMB responses of command if response entry count != 1
 *
 * @return		None
 */
#include "misc.h"
static fast_code void hmb_cmd_done(hmb_cmd_resp_t *resp,
								   hmb_cmd_resp_entry_t *ent[3])
{
	hmb_cmd_cb_t *cb;

	cb = &_hmb_cbs[resp->hdr.cmd_tag];
	cb->error = false;

	// add debug
	if ((jiffies - cb->tid) > 30)
	{
		hmb_hal_trace(LOG_INFO, 0x83cc, "Hmb comand timeout");
	}

	hmb_hal_trace(LOG_DEBUG, 0xfe14, "hmb cmd %d done, resp_code:%d",
				  resp->hdr.cmd_tag, resp->hdr.resp_code);

	if (resp->hdr.hmb_crc_err == 1)
	{
		hmb_hal_trace(LOG_ERR, 0x39de, "hmb crc error");
		cb->error = true;
		goto end;
	}

	switch (resp->hdr.resp_code)
	{
	case HMB_CMD_TABLE_READ:
	{
		if (resp->hdr.status == 1)
		{
			hmb_tbl_read_resp_t *rresp;

			rresp = (hmb_tbl_read_resp_t *)resp;
			cb->tid = rresp->transaction_id;
		}
		else
		{
			cb->error = true;
		}
		break;
	}
	case HMB_CMD_TABLE_WRITE:
	{

		hmb_tbl_write_resp_t *wresp;

		wresp = (hmb_tbl_write_resp_t *)resp;
		cb->tid = wresp->transaction_id;
		break;
	}
	case HMB_CMD_LOOKUP_FMT1:
	{
		hmb_lkp_fmt1_resp_t *l1r;
		u32 vbits;
		u32 *rst;

		l1r = (hmb_lkp_fmt1_resp_t *)resp;
		cb->tid = l1r->transaction_id;
		vbits = l1r->hdr.status;
		cb->rst.lkp_fmt1.vbits = vbits;
		rst = cb->rst.lkp_fmt1.pda;
		rst[0] = (vbits & BIT(0)) ? HMB_UNMAP_CHK(l1r->p[0]) : HMB_LKP_MISS;
		rst[1] = (vbits & BIT(1)) ? HMB_UNMAP_CHK(l1r->p[1]) : HMB_LKP_MISS;
		break;
	}
	case HMB_CMD_LOOKUP_FMT2:
	{
		hmb_lkp_fmt2_resp_t *l2r;
		u32 cnt;
		u32 i;
		u32 loop;
		u32 *rst;

		l2r = (hmb_lkp_fmt2_resp_t *)resp;
		cnt = l2r->hdr.status;
		cb->rst.lkp_fmt2.cnt = cnt;
		cb->tid = l2r->transaction_id;
		rst = cb->rst.lkp_fmt2.pda;
		if (cnt == 0)
		{
			/* lookup fail */
			for (i = 0; i < cb->rst.lkp_fmt2.lookup_cnt; i++)
			{
				*rst = HMB_LKP_MISS;
				rst++;
			}
			break;
		}

		loop = min(cnt, 2);
		for (i = 0; i < loop; i++)
		{
			*rst = l2r->p[i];
			rst++;
		}
		cnt -= loop;
		for (i = 0; i < l2r->hdr.resp_ent_cnt - 1; i++)
		{
			u32 j;

			loop = min(4, cnt);
			sys_assert(loop != 0);
			for (j = 0; j < loop; j++)
			{
				*rst = HMB_UNMAP_CHK(ent[i]->payload[j]);
				rst++;
			}
			cnt -= loop;
		}
		sys_assert(cnt == 0);
		break;
	}
	case HMB_CMD_LOOKUP_REPLACE_FMT1:
	{
		hmb_lkp_rep_fmt1_resp_t *lr1r;

		lr1r = (hmb_lkp_rep_fmt1_resp_t *)resp;
		cb->rst.lkp_rep_fmt1.vbits = lr1r->hdr.status;
		cb->tid = lr1r->transaction_id;
		cb->rst.lkp_rep_fmt1.old_pda = (lr1r->hdr.status) ? lr1r->pda : HMB_LKP_MISS;
		break;
	}
	case HMB_CMD_LOOKUP_REPLACE_FMT2:
	{
		hmb_lkp_rep_fmt2_resp_t *lr2r;
		u32 i;
		u32 cnt;
		u32 *rst;
		u32 m;

		lr2r = (hmb_lkp_rep_fmt2_resp_t *)resp;

		cb->tid = lr2r->transaction_id;
		cnt = lr2r->hdr.status;
		cb->rst.lkp_rep_fmt2.cnt = cnt;

		if (cnt == 0)
			break;

		rst = cb->rst.lkp_rep_fmt2.pda;
		m = 2;
		m = min(m, cnt);
		for (i = 0; i < m; i++)
		{
			*rst = lr2r->pda[i];
			rst++;
		}
		cnt -= m;
		if (lr2r->hdr.resp_ent_cnt > 1)
		{
			sys_assert(cnt < 4 && cnt > 0);

			for (i = 0; i < cnt; i++)
			{
				*rst = ent[0]->payload[i];
				rst++;
			}
		}
		break;
	}
	default:
		panic("invalid op code");
		break;
	}
end:
	cb->cmpl(cb);
	_hmb_cmd_otf--;
}

/*!
 * @brief handle HMB response queue
 *
 * Check HMB hardware response read/write pointer and handle responses. If not
 * called from isr, after all responses handled, enable HMB response ISR.
 *
 * @param isr		1 if called from isr
 *
 * @return		None
 */
static fast_code void hmb_cmd_done_handler(u32 isr, u32 qid)
{
	fw_ntbl_access_res0_pointers_t ptr;
	u32 i;
	hmb_cmd_resp_t *res_base;
	u32 res_packet_cnt;
	u32 reg_ptr[] = {FW_NTBL_ACCESS_RES1_POINTERS, FW_NTBL_ACCESS_RES0_POINTERS};
#ifdef RAMDISK_2NS
	hmb_queue_t *que[] = {&_hmb_resp_ns0, &_hmb_resp};
#else
	hmb_queue_t *que[] = {NULL, &_hmb_resp};
	sys_assert(qid == 1);
#endif
	ptr.all = hmb_readl(reg_ptr[qid]);

	res_packet_cnt = ring_distanace(ptr.b.fw_access_rsp0_rptr,
									ptr.b.fw_access_rsp0_wptr,
									HMB_RESP_CNT);
	if (res_packet_cnt == 0)
	{
		hmb_hal_trace(LOG_DEBUG, 0x8ff7, "no new hmb resp %d-%d",
					  ptr.b.fw_access_rsp0_rptr,
					  ptr.b.fw_access_rsp0_wptr);
		return;
	}
	i = ptr.b.fw_access_rsp0_rptr;
	res_base = (hmb_cmd_resp_t *)que[qid]->queue;

	while (res_packet_cnt)
	{
		hmb_cmd_resp_t *res;
		hmb_cmd_resp_entry_t *ent[3] = {NULL, NULL, NULL};
		u32 j;

		res = &res_base[i];

		if (res_packet_cnt < res->hdr.resp_ent_cnt)
			break; /* this response not completed */

		i = next_ring_index(i, HMB_RESP_CNT);

		for (j = 1; j < res->hdr.resp_ent_cnt; j++)
		{
			ent[j - 1] = (hmb_cmd_resp_entry_t *)&res_base[i];
			i = next_ring_index(i, HMB_RESP_CNT);
		}

		hmb_cmd_done(res, ent);

		res_packet_cnt -= res->hdr.resp_ent_cnt;
	}

	ptr.b.fw_access_rsp0_rptr = i;

	hmb_writel(ptr.all, reg_ptr[qid]);
}

/*!
 * @brief dump auto lookup response header
 *
 * @param hdr	header of auto lookup response
 *
 * @return	None
 */
static fast_code __attribute__((unused)) void hmb_auto_lkp_rsp_dump(
	hmb_auto_lkp_resp_t *rsp, int nsid, u32 idx)
{
	hmb_hal_trace(LOG_ALW, 0x6fde, "[%d] nsid %d sq %d fid %d ent %d",
				  idx, nsid, rsp->hdr.sq_id, rsp->hdr.cmd_tag,
				  rsp->hdr.resp_ent_cnt);
	hmb_hal_trace(LOG_ALW, 0xb10f, " valid_cnt %d, transaction_id %d, pda %x pda %x", rsp->hdr.valid_cnt,
				  rsp->transaction_id, rsp->p[0], rsp->p[1]);
}
static fast_code __attribute__((unused)) void hmb_auto_lkp_rsp_dat_dump(
	hmb_auto_lkp_resp_t *rsp, int nsid, u32 idx)
{
	hmb_hal_trace(LOG_ALW, 0x5d75, "pda %x pda %x pda %x pda %x", rsp->hdr.cmd_tag,
				  rsp->transaction_id, rsp->p[0], rsp->p[1]);
}

static fast_code void hmb_discard_auto_lookup_result(auto_lookup_res0_ptrs_t ptr)
{
	u32 idx;
	u32 i;
	hmb_auto_lkp_resp_t *res_base;
	hmb_auto_lkp_resp_t *res;

	res_base = (hmb_auto_lkp_resp_t *)_hmb_auto_resp.queue;
	idx = ptr.b.alkup_rsp0_rptr;

	res = &res_base[idx];
	hal_nvmet_cmd_set_dis_req_id(res->hdr.cmd_tag);

	for (i = 0; i < res->hdr.resp_ent_cnt; i++)
		idx = next_ring_index(idx, HMB_AUTO_RESP_CNT);

	hmb_hal_trace(LOG_DEBUG, 0xa9f8, "hmb auto resp discard %d->%d",
				  ptr.b.alkup_rsp0_rptr, idx);

	ptr.b.alkup_rsp0_rptr = idx;
	hmb_writel(ptr.all, AUTO_LOOKUP_RES0_PTRS);
}

/*!
 * @brief handle auto lookup response queue
 *
 * Check HMB hardware auto lookup response read/write pointer and
 * handle auto lookup responses. If not called from isr, after all responses
 * handled, enable HMB response ISR
 *
 * @return		None
 */
static fast_code void hmb_auto_cmd_done_handler(u32 qid)
{
	auto_lookup_res0_ptrs_t ptr;
	u32 res_packet_cnt;
	u8 num_lba = 0;
	if (host_sec_bitz == 9) //joe add sec size 20200818
		num_lba = 8;
	else
		num_lba = 1;
#if defined(TFW_CTRL_HMB) || defined(TFW_CTRL_NS_HMB)
	u32 dump_ctl = 0;
#endif
	hmb_auto_lkp_resp_t *res_base;
	u32 reg_ptr[] = {AUTO_LOOKUP_RES1_PTRS, AUTO_LOOKUP_RES0_PTRS};
#ifdef RAMDISK_2NS
	hmb_queue_t *que[] = {&_hmb_auto_resp_ns0, &_hmb_auto_resp};
#else
	hmb_queue_t *que[] = {NULL, &_hmb_auto_resp};
	sys_assert(qid == 1);
#endif
	u32 i;

	ptr.all = hmb_readl(reg_ptr[qid]);

	if (hmb_enabled == false)
	{
		hmb_hal_trace(LOG_ERR, 0xceab, "auto lkp isr ptr %x, but not inited",
					  ptr.all);
		return;
	}

	res_packet_cnt = ring_distanace(ptr.b.alkup_rsp0_rptr,
									ptr.b.alkup_rsp0_wptr,
									HMB_AUTO_RESP_CNT);

	res_base = (hmb_auto_lkp_resp_t *)que[qid]->queue;
	i = ptr.b.alkup_rsp0_rptr;

#if defined(TFW_CTRL_HMB) || defined(TFW_CTRL_NS_HMB)
	hmb_hal_trace(LOG_ERR, 0x035b, "res_packet_cnt %x", res_packet_cnt);
#endif

	while (res_packet_cnt)
	{
		hmb_auto_lkp_resp_t *res;
		hmb_cmd_resp_entry_t *ent;
		u32 k;
		u32 cnt;
		extern req_t *_reqs;
		hmb_auto_lkp_rst_t *rst;
		u32 req_id;
		u32 *list;
		req_t *req;

		res = &res_base[i];
#if defined(TFW_CTRL_HMB) || defined(TFW_CTRL_NS_HMB)
		{
			if (dump_ctl == 0)
			{
				sys_assert(res->hdr.valid_cnt <= 14);
				hmb_auto_lkp_rsp_dump(res, qid, i);
				dump_ctl = 1;
			}
			else
			{
				hmb_auto_lkp_rsp_dat_dump(res, qid, i);
			}
			res_packet_cnt--;
			i = next_ring_index(i, HMB_AUTO_RESP_CNT);
			continue;
		}
#endif

		k = 0;
		req_id = hal_nvmet_cmd_get_req_id(res->hdr.cmd_tag);
		if (req_id == HCMD_REQID_INV || res_packet_cnt < res->hdr.resp_ent_cnt)
		{
			if (req_id == HCMD_REQID_INV && res_packet_cnt == (HMB_AUTO_RESP_CNT - 1))
			{
				hmb_discard_auto_lookup_result(ptr);
				return;
			}
			/*
			if (req_id == HCMD_REQID_INV && cmdproc_vac == MAX_CTAG) {
				u32 val;

				val = hmb_readl(NVME_INT_MASK0);
				// debug code
				if (val & IO_CMD_FETCHED_MASK) {
					hmb_hal_trace(LOG_INFO, 0, "kickoff for waiting cmd, int %x",
							val);
					hal_nvmet_kickoff();
				}
			}*/

			/* this command is not handled, yet */
			hmb_hal_trace(LOG_DEBUG, 0xd3cb, "cmd is not handled, yet %d/%d",
						  i, ptr.b.alkup_rsp0_wptr);
		pend:
			ptr.b.alkup_rsp0_rptr = i;
			hmb_writel(ptr.all, AUTO_LOOKUP_RES0_PTRS);
			return;
		}

		/* only pend auto lookup if auto lookup queue is not full */
		if (free_cb_cnt < 4 && res_packet_cnt < (HMB_AUTO_RESP_CNT - 1))
			goto pend;

		i = next_ring_index(i, HMB_AUTO_RESP_CNT);

		req = &_reqs[req_id];
		if (res->hdr.hmb_crc_err == 1)
		{
			u32 j;

			hmb_hal_trace(LOG_ERR, 0xda20, "hmb crc error");
			req->op_fields.read.cnt = 0;
			for (j = 1; j < res->hdr.resp_ent_cnt; j++)
			{
				i = next_ring_index(i, HMB_AUTO_RESP_CNT);
			}
			goto issue;
		}

		if (res->hdr.valid_cnt == 1 && res->hdr.hmb_crc_err == 0)
		{
			u32 lba = req->lba.srage.slba;

			//if (req->lba.srage.nlb == NR_LBA_PER_LDA &&//joe add sec size 20200817
			//(lba & NR_LBA_PER_LDA_MASK) == 0) {
			if (req->lba.srage.nlb == num_lba &&
				(lba & (num_lba - 1)) == 0)
			{
				extern void nvmet_core_submit(req_t * req);

				sfa[req_id] = HMB_UNMAP_CHK(res->p[0]);
				req->op_fields.read.auto_rst = &sfa[req_id];
				req->op_fields.read.tid = res->transaction_id;
				req->op_fields.read.cnt = 1;

				nvmet_core_submit(req);
				res_packet_cnt -= res->hdr.resp_ent_cnt;
				wait_for_auto_lkp--;
				continue;
			}
		}

		rst = &_hmb_auto_lkp_rst[req_id];
		req->op_fields.read.auto_rst = rst->pda;
		req->op_fields.read.cnt = res->hdr.valid_cnt;
		req->op_fields.read.tid = res->transaction_id;

		cnt = res->hdr.valid_cnt;
		list = req->op_fields.read.auto_rst;
		if (cnt >= 1)
		{
			u32 c = min(cnt, (u32)2);
			u32 j;

			for (j = 0; j < c; j++)
			{
				list[k] = HMB_UNMAP_CHK(res->p[j]);
				k++;
			}
			cnt -= c;

			for (j = 1; j < res->hdr.resp_ent_cnt; j++)
			{
				u32 l;

				ent = (hmb_cmd_resp_entry_t *)&res_base[i];
				c = min(cnt, (u32)4);
				for (l = 0; l < c; l++)
				{
					list[k] = HMB_UNMAP_CHK(ent->payload[l]);
					k++;
				}
				cnt -= c;

				i = next_ring_index(i, HMB_AUTO_RESP_CNT);
			}
			sys_assert(cnt == 0);
		}

	issue:
		evt_set_imt(evt_auto_lkp_done, (u32)req, 0);

		res_packet_cnt -= res->hdr.resp_ent_cnt;
		wait_for_auto_lkp--;
	}

	ptr.b.alkup_rsp0_rptr = ptr.b.alkup_rsp0_wptr;

	hmb_writel(ptr.all, reg_ptr[qid]);
}

/*!
 * @brief event handler for HMB isr
 *
 * Event was triggered when HMB command done.
 * If HMB was detached by host, we must wait for all HMB commands finished.
 * Here, we will check if detach was happened, and complete detach if all HMB
 * command was finished.
 *
 * @param r0		not used
 * @param r1		not used
 * @param isr		1 if called from isr, (CS_NOW)
 *
 * @return		None
 */
static fast_code void hmb_evt_cmd_done(u32 r0, u32 r1, u32 isr)
{
#ifdef RAMDISK_2NS
	hmb_cmd_done_handler(isr, 0);
#endif
	hmb_cmd_done_handler(isr, 1);
	if (_evt_hmb_detach != ~0)
	{
		if (hmb_detach(_evt_hmb_detach))
		{
			UNUSED u16 ctag = _evt_hmb_detach;
			_evt_hmb_detach = ~0;
#if defined(RAWDISK)
			//req_t *req = rawdisk_ctag2req(ctag); todo
			req_t *req = NULL; // todo
			panic("todo");
#else
			req_t *req = NULL; // todo
			panic("todo");
#endif
			evt_set_imt(evt_cmd_done, (u32)req, 0);
		}
	}
}

/*!
 * @brief event handler for HMB isr
 *
 * Event was triggered when auto lookup command done.
 * If HMB was detached by host, we must wait for all HMB commands finished.
 * Here, we will check if detach was happened, and complete detach if all HMB
 * command was finished.
 *
 * @param r0		not used
 * @param r1		not used
 * @param isr		1 if called from isr, (CS_NOW)
 *
 * @return		None
 */
static fast_code void hmb_evt_auto_cmd_done(u32 r0, u32 r1, u32 isr)
{
#ifdef RAMDISK_2NS
	hmb_auto_cmd_done_handler(0);
#endif
	hmb_auto_cmd_done_handler(1);
	if (_evt_hmb_detach != ~0)
	{
		if (hmb_detach(_evt_hmb_detach))
		{
			u16 ctag = _evt_hmb_detach;

			_evt_hmb_detach = ~0;
			evt_set_imt(evt_cmd_done, ctag, 0);
		}
	}
}

/*!
 * @brief check if there was HMB command which is not finished yet
 *
 * @return	Return true if HMB command was finished
 */
static inline bool hmb_is_idle(void)
{
	if (_hmb_cmd_otf || wait_for_auto_lkp)
		return false;

	return true;
}

fast_code void hmb_cmd_submit(u32 cmd_cnt)
{
	_hmb_cmd_otf++;
	advance_queue_ptr(&_hmb_cmd, FW_NTBL_ACCESS_REQ_POINTERS, cmd_cnt);
}

fast_code hmb_cmd_t *hmb_get_next_cmd(hmb_cmd_cb_t *cb, u32 pos)
{
	hmb_cmd_t *cmd;
	u32 ptr;

	ptr = get_next_queue_ptr(&_hmb_cmd, FW_NTBL_ACCESS_REQ_POINTERS, pos);

	if (ptr != ~0)
	{
		cmd = (hmb_cmd_t *)_hmb_cmd.queue;
		cmd += ptr;
		cmd->dw0.cmd_id = cb - &_hmb_cbs[0];
	}
	else
	{
		cmd = NULL;
	}
	return cmd;
}

fast_code u32 hmb_get_hw_tid(void)
{
	return hmb_readl(HMB_NTBL_UPDT_TRANSACTION_ID);
}

#if defined(HMB_ENCRYPTION_ENABLE)
/*!
 * @brief initialize hmb encryption key
 *
 * @return	none
 */
static fast_code void hmb_encry_set_key(void)
{
	hmb_data_encryption_key0_t key;
	u32 reg = HMB_DATA_ENCRYPTION_KEY0;

	key.all = 0;
	do
	{
		hmb_writel(key.all, reg);
		key.all++;
		reg += 4;
	} while (reg <= HMB_DATA_ENCRYPTION_KEY7);
}

/*!
 * @brief dump HMB encryption key
 *
 * @return	none
 */
static fast_code void hmb_encry_get_key(void)
{
	u32 data;
	u32 reg = HMB_DATA_ENCRYPTION_KEY0;

	do
	{
		data = hmb_readl(reg);
		hmb_hal_trace(LOG_DEBUG, 0xe11f, "reg %x data %x\n", reg, data);
		reg += 4;
	} while (reg <= HMB_DATA_ENCRYPTION_KEY7);
}
#endif

fast_code bool hmb_detach(u32 ctag)
{
	sys_assert(hmb_enabled == true);
	/* handle all command */
	if (hmb_is_idle())
	{
		hmb_cmd_disable();
#if AUTO_LOOKUP_ENABLE == 1
		hmb_auto_cmd_disable();
#endif
		sys_free(FAST_DATA, ps_hmb_addr_tbl);
		sys_free(FAST_DATA, ps_hmb_sz_tbl);

		hmb_writel(0, HMB_NTBL_META_CTRL);
		hmb_writel(HMB_NTBL_SIZE_FETCH_DONE_MASK, HMB_NAND_TBL_CTRL);
		hmb_enabled = false;

		hmb_hal_trace(LOG_INFO, 0x49d6, "hmb detached");
		dtag_mgr_destroy(DTAG_T_HMB);
		return true;
	}

	sys_assert(ctag != ~0);
	_evt_hmb_detach = ctag;
	return false;
}

static void _hmb_mem_set(hmb_mem_reset_t *reset);

/*!
 * @brief HMB table initialization
 *
 * @param ctx		hmb initialization context
 *
 * @return		None
 */
fast_code void hmb_tbl_init(hmb_ctx_t *ctx)
{
	u32 i;
	hmb_ntbl_meta_ctrl_t meta_ctrl;
	hmb_nand_tbl_ctrl_t ctrl;
	hmb_tbl_ctrl_t hmb_buf_ctrl;
	u64 *addr_tbl;
	u64 *addr_buf;

	u8 *size_tbl;
	u8 *size_buf;

	u32 hmb_tbl_mb = 0;
	u32 hmb_buf_mb = 0;
	u32 tbl_seg_cnt = 0;
	u32 buf_seg_cnt = 0;
	u32 min_seg = MIN_SEG_SZ >> ctx->page_bits;

	sys_assert(ctx->list_cnt <= 512);
	sys_assert(hmb_enabled == false);
	/* init hmb buffer/table */
	addr_buf = sys_malloc_aligned(SLOW_DATA, sizeof(u64) * hmb_buf_seg_end, 128);
	addr_tbl = sys_malloc_aligned(SLOW_DATA, sizeof(u64) * (ctx->list_cnt - hmb_buf_seg_end), 128);

	size_buf = sys_malloc_aligned(SLOW_DATA, sizeof(u8) * hmb_buf_seg_end, 128);
	size_tbl = sys_malloc_aligned(SLOW_DATA, sizeof(u8) * (ctx->list_cnt - hmb_buf_seg_end), 128);

	sys_assert(addr_buf);
	sys_assert(addr_tbl);
	sys_assert(size_buf);
	sys_assert(size_tbl);

	for (i = 0; i < ctx->list_cnt; i++)
	{
		u32 hi;
		u32 lo;

		if (i == _hmb_crc_seg || i == _hmb_meta_seg || i == _hmb_dmeta_seg || i == _hmb_ecc_seg)
			continue; /* skip crc/meta seg */

		if (ctx->ilist[i].bsize < min_seg)
			continue;

		if (i < hmb_buf_seg_end)
		{
			addr_buf[buf_seg_cnt] = ctx->ilist[i].badd;
			size_buf[buf_seg_cnt] = ctx->ilist[i].bsize >> (MB_SFT - ctx->page_bits);
			sys_assert(size_buf[buf_seg_cnt] != 0);
			hi = ctx->ilist[i].badd >> 32;
			lo = ctx->ilist[i].badd & 0xffffffff;

			hmb_buf_mb += size_buf[buf_seg_cnt];

			hmb_hal_trace(LOG_INFO, 0x79ed, "BUF Seg[%d]: %x%x size %d MB",
						  buf_seg_cnt, hi, lo, size_buf[buf_seg_cnt]);
			buf_seg_cnt++;
		}
		else
		{
			addr_tbl[tbl_seg_cnt] = ctx->ilist[i].badd;
			size_tbl[tbl_seg_cnt] = ctx->ilist[i].bsize >> (MB_SFT - ctx->page_bits);

			sys_assert(size_tbl[tbl_seg_cnt] != 0);

			hi = ctx->ilist[i].badd >> 32;
			lo = ctx->ilist[i].badd & 0xffffffff;

			hmb_tbl_mb += size_tbl[tbl_seg_cnt];

			hmb_hal_trace(LOG_INFO, 0x7a4f, "TBL Seg[%d]: %x%x size %d MB",
						  tbl_seg_cnt, hi, lo, size_tbl[tbl_seg_cnt]);
			tbl_seg_cnt++;
		}
	}

	/* HMB table size must be 4MB aligned */
	while (HMB_TBL_SZ_ALIGN(hmb_tbl_mb))
	{
		size_tbl[tbl_seg_cnt - 1]--;
		hmb_tbl_mb--;
		hmb_hal_trace(LOG_INFO, 0x2bdc, "TBL seg[%d] %d MB", tbl_seg_cnt - 1,
					  size_tbl[tbl_seg_cnt - 1]);
		if (size_tbl[tbl_seg_cnt - 1] == 0)
			tbl_seg_cnt--;
	}

	hmb_hal_trace(LOG_ALW, 0x12fe, "HMB table %d MB", hmb_tbl_mb);
	hmb_hal_trace(LOG_ALW, 0x654d, "HMB buffer %d MB", hmb_buf_mb);

	// just use 1M HMB dtag, resource is not enough
	u32 min_sz = min(hmb_buf_mb, 1);
	dtag_mgr_init(DTAG_T_HMB, min_sz << 8, 0);
	dtag_add(DTAG_T_HMB, 0, min_sz << 20);
#ifdef RAMDISK_2NS
	u32 off = hmb_tbl_mb / 4; // unit in 4MB

	off = (off / 2); // each namespace has half HMB
	hmb_ntbl_ns_ctrl_sts_t ns_hmb_sts = {
		.b.hmb_multi_ns_en = 1,
		.b.ns0_noncache_mode = 0, // need to be verified later
		//.b.ns1_host_sector_size = (HLBASZ == 9) ? 0 : 1,
		.b.ns1_host_sector_size = (host_sec_bitz == 9) ? 0 : 1,		
		.b.ns1_ntbl_data_unit_size = (DTAG_SHF == 12) ? 0 : 1,
		.b.hmb_ns_offset = off,
		.b.ns0_unexp_lda_size = 1,
		.b.ns0_unexp_way_updt = 1};

	hmb_hal_trace(LOG_ALW, 0x2578, "ns0 %d MB, ns1 %d MB", off * 4, hmb_tbl_mb - off * 4);
	hmb_writel(ns_hmb_sts.all, HMB_NTBL_NS_CTRL_STS);
#endif

	/* init hmb buffer to hw, addr_tbl and size tbl is sram location */
	sys_assert(buf_seg_cnt < 64);
	hmb_writel(sram_to_dma(addr_buf), HMB_SEG_ADDR_BASE);
	hmb_writel(sram_to_dma(size_buf), HMB_SEG_SIZE_BASE);
	hmb_writell(ctx->ilist[_hmb_dmeta_seg].badd, HMB_META_BASE_L);
	hmb_writell(ctx->ilist[_hmb_ecc_seg].badd, HMB_ECC_BASE_L);
	hmb_buf_ctrl.all = 0;
	hmb_buf_ctrl.b.hmb_du_crc_en = 1;
#if !defined(DISABLE_HS_CRC_SUPPORT)
	hmb_buf_ctrl.b.hmb_hcrc_en = 1;
#endif
	hmb_buf_ctrl.b.hmb_ecc_en = 1;
	hmb_buf_ctrl.b.hmb_scramble_en = 1;
	hmb_buf_ctrl.b.hmb_seg_num = buf_seg_cnt;
	hmb_buf_ctrl.b.hmb_tbl_fetch_en = 1;
	hmb_writel(hmb_buf_ctrl.all, HMB_TBL_CTRL);
	// todo: queue setup ??
	do
	{
		hmb_buf_ctrl.all = hmb_readl(HMB_TBL_CTRL);
		if (hmb_buf_ctrl.b.hmb_tbl_fetch_done)
			break;
	} while (1);
	sys_free_aligned(SLOW_DATA, size_buf);
	sys_free_aligned(SLOW_DATA, addr_buf);

	/* init hmb tbl to hw */
	ps_hmb_tbl_cnt = tbl_seg_cnt;
	ps_hmb_addr_tbl = sys_malloc(FAST_DATA, sizeof(u64) * (tbl_seg_cnt + 2));
	sys_assert(ps_hmb_addr_tbl);
	ps_hmb_sz_tbl = sys_malloc(FAST_DATA, sizeof(u8) * (tbl_seg_cnt + 2));
	sys_assert(ps_hmb_sz_tbl);

	memcpy(ps_hmb_addr_tbl, addr_tbl, sizeof(u64) * tbl_seg_cnt);
	memcpy(ps_hmb_sz_tbl, size_tbl, sizeof(u8) * tbl_seg_cnt);

	ps_hmb_addr_tbl[tbl_seg_cnt] = ctx->ilist[_hmb_crc_seg].badd;
	ps_hmb_addr_tbl[tbl_seg_cnt + 1] = ctx->ilist[_hmb_meta_seg].badd;

	ps_hmb_sz_tbl[tbl_seg_cnt] = ctx->ilist[_hmb_crc_seg].bsize >> (MB_SFT - ctx->page_bits);
	ps_hmb_sz_tbl[tbl_seg_cnt + 1] = ctx->ilist[_hmb_meta_seg].bsize >> (MB_SFT - ctx->page_bits);

	hmb_writel(sram_to_dma(addr_tbl), HMB_NTBL_SADDR_SRAM_BASE);
	hmb_writel(sram_to_dma(size_tbl), HMB_NTBL_SSIZE_SRAM_BASE);

	hmb_writell(ctx->ilist[_hmb_crc_seg].badd, HMB_NTBL_CRC_HADDR_LOW);
	hmb_writell(ctx->ilist[_hmb_meta_seg].badd, HMB_NTBL_META_HADDR_LOW);

	meta_ctrl.all = 0;
	hmb_writel(meta_ctrl.all, HMB_NTBL_META_CTRL);

	ctrl.all = 0;
	ctrl.b.hmb_nand_tbl_enable = 1;
	ctrl.b.hmb_ntbl_crc_en = 1;
	ctrl.b.hmb_ntbl_seg_num = tbl_seg_cnt;
	ctrl.b.hmb_ntbl_total_size = hmb_tbl_mb;
	//ctrl.b.host_sector_size = (HLBASZ == 12) ? 1 : 0;
	ctrl.b.host_sector_size = (host_sec_bitz == 12) ? 1 : 0;	
	ctrl.b.ntbl_data_unit_size = (DTAG_SHF == 13) ? 1 : 0;
#if defined(HMB_ENCRYPTION_ENABLE)
	hmb_encry_get_key();
	hmb_encry_set_key();
	ctrl.b.hmb_encrypt_en = 1;
#endif

	hmb_writel(ctrl.all, HMB_NAND_TBL_CTRL);

	/* assign ntbl access cmd buffer */
	hmb_cmd_enable(HMB_CMD_CNT);
#if AUTO_LOOKUP_ENABLE
	hmb_auto_cmd_enable(HMB_AUTO_CMD_CNT, 0);
#endif

	do
	{
		ctrl.all = hmb_readl(HMB_NAND_TBL_CTRL);
		if (ctrl.b.hmb_ntbl_size_fetch_done)
			break;
	} while (1);

	sys_free_aligned(SLOW_DATA, size_tbl);
	sys_free_aligned(SLOW_DATA, addr_tbl);

	if (evt_hmb_cmd_done == 0xFF)
		evt_register(hmb_evt_cmd_done, 0, &evt_hmb_cmd_done);
	if (evt_hmb_auto_cmd_done == 0xFF)
		evt_register(hmb_evt_auto_cmd_done, 0, &evt_hmb_auto_cmd_done);

	hmb_enabled = true;
	tbl_cnt = HMB_TU_CNT(hmb_tbl_mb);

	evt_set_imt(evt_cmd_done, (u32)ctx->req, 0);

	sys_free(FAST_DATA, ctx->list);
	sys_free(SLOW_DATA, ctx->ilist);
	sys_free(SLOW_DATA, ctx);
}

/*!
 * @brief callback for HMB memory set
 *
 * @param _reset	context to issue reset meta segment in host
 *
 * @return		None
 */
static fast_code void hmb_mem_set_cb(void *_reset)
{
	hmb_mem_reset_t *reset = (hmb_mem_reset_t *)_reset;

	if (reset->seg_size)
	{
		_hmb_mem_set(reset);
	}
	else
	{
		hmb_hal_trace(LOG_INFO, 0x140e, "hmb %d seg clear done", reset->list_idx);
		reset->list_idx++;
		if (reset->list_idx < reset->ctx->list_cnt)
		{
			reset->iter = 0;
			reset->seg_size = reset->ctx->list[reset->list_idx].bsize << reset->ctx->page_bits;
			//nvme_hal_trace(LOG_ERR, 0, "hmb [%d] size %d\n", reset->list_idx, reset->seg_size);
			_hmb_mem_set(reset);
		}
		else
		{
			dtag_t dtag;
			u32 i;
			hmb_ctx_t *ctx;
			void (*cmpl)(req_t *);

			dtag = mem2dtag(reset->zero);
			for (i = 0; i < (reset->buf_size >> DTAG_SHF); i++)
			{
				dtag_put(DTAG_T_SRAM, dtag);
				dtag.dtag++;
			}

			ctx = reset->ctx;
			cmpl = reset->cmpl;
			sys_free(SLOW_DATA, reset);
			if (cmpl == NULL)
			{
				hmb_tbl_init(ctx);
			}
			else
			{
				dtag_t dtag;
				req_t *req;

				dtag = mem2dtag((void *)ctx->list);
				dtag_put(DTAG_T_SRAM, dtag);
				req = ctx->req;
				sys_free(SLOW_DATA, ctx);

				hmb_hal_trace(LOG_INFO, 0xcfae, "req %p %p\n", req, cmpl);
				cmpl(req);
			}
		}
	}
}

static fast_code void _hmb_mem_set(hmb_mem_reset_t *reset)
{
	u32 size;
	u64 addr;

	/* clear meta seg via DMA */
	size = min(reset->seg_size, reset->buf_size);
	addr = reset->ctx->list[reset->list_idx].badd +
		   (reset->iter * reset->buf_size);
	hal_nvmet_data_xfer(addr, reset->zero, size, WRITE, reset,
						hmb_mem_set_cb);
	//nvme_hal_trace(LOG_ERR, 0, "hmb mem_set(%d-%d)-%x%x %d\n", reset->list_idx,
	//		reset->iter, addr, size);

	reset->seg_size -= size;
	reset->iter++;
}

/*!
 * @brief HMB momery initialization
 *
 * @param ctx			HMB initialization context
 * @param zero_buf		zero buffer to reset meta segment
 * @param buf_size		size of zero buffer
 *
 * @return			None
 */
static fast_code void hmb_mem_set(hmb_ctx_t *ctx, void *zero_buf,
								  u32 buf_size)
{
	hmb_mem_reset_t *reset;

	reset = sys_malloc(SLOW_DATA, sizeof(hmb_mem_reset_t));
	reset->ctx = ctx;
	reset->zero = zero_buf;
	reset->buf_size = buf_size;
	reset->list_idx = 0;
	reset->iter = 0;
	reset->seg_size = ctx->list[0].bsize << ctx->page_bits;
	reset->cmpl = NULL;
	//nvme_hal_trace(LOG_ERR, 0, "total seg %d\n", ctx->list_cnt);
	//nvme_hal_trace(LOG_ERR, 0, "hmb [%d] size %d\n", reset->list_idx, reset->seg_size);
	_hmb_mem_set(reset);
}

fast_code void hmb_mem_reset(req_t *req, void (*callback)(req_t *), u32 page_bits)
{
	hmb_ctx_t *ctx;
	u32 i;
	void *buf;
	hmb_mem_reset_t *reset;
	u32 buf_size;
	dtag_t dtag;

	sys_assert(ps_hmb_addr_tbl && ps_hmb_sz_tbl);

	ctx = sys_malloc(SLOW_DATA, sizeof(hmb_ctx_t));
	sys_assert(ctx);

	dtag = dtag_get(DTAG_T_SRAM, &buf);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	ctx->page_bits = page_bits;
	ctx->list = (struct nvme_host_memory_desc_entry *)buf;

	// +2: meta and crc
	for (i = 0; i < ps_hmb_tbl_cnt + 2; i++)
	{
		u32 size;

		ctx->list[i].badd = ps_hmb_addr_tbl[i];
		size = ps_hmb_sz_tbl[i];
		ctx->list[i].bsize = (size << MB_SFT) >> ctx->page_bits;
	}
	ctx->list_cnt = ps_hmb_tbl_cnt + 2;
	ctx->req = req;

	reset = sys_malloc(SLOW_DATA, sizeof(hmb_mem_reset_t));
	sys_assert(reset);

	zero_tag = dtag_cont_get(DTAG_T_SRAM, HMB_MEM_SET_SIZE >> DTAG_SHF);
	if (zero_tag.dtag == _inv_dtag.dtag)
	{
		buf_size = DTAG_SZE;
		zero_tag = dtag_get_urgt(DTAG_T_SRAM, NULL);
		sys_assert(zero_tag.dtag != _inv_dtag.dtag);
	}
	else
	{
		buf_size = HMB_MEM_SET_SIZE;
	}
	memset(dtag2mem(zero_tag), 0, buf_size);

	reset->ctx = ctx;
	reset->zero = dtag2mem(zero_tag);
	reset->buf_size = buf_size;
	reset->list_idx = 0;
	reset->iter = 0;
	reset->seg_size = ctx->list[0].bsize << ctx->page_bits;
	reset->cmpl = callback;
	_hmb_mem_set(reset);
}

fast_code bool hal_wait_hmb_auto_lookup(struct nvme_cmd *cmd)
{
#if AUTO_LOOKUP_ENABLE == 1
	if (hmb_enabled)
	{
		if (hal_nvmet_cmd_get_req_id_by_hcmd(cmd) < HMB_AUTO_CMD_CNT)
		{
			/* todo: partial write should return true */
			wait_for_auto_lkp++;
			return true;
		}
	}
#endif
	return false;
}

static fast_code u32 hmb_dtag_get_md_entry_sz(void)
{
	// TODO: up to du size/host type/16MD enable
	return 84;
}

static fast_code u32 hmb_dtag_get_ecc_entry_sz(void)
{
#if defined(USE_8K_DU)
	return 0x200;
#endif
	return 0x100;
}

/*!
 * @brief HMB pre-initialization
 *
 * @param ctx		HMB initialization context
 *
 * @return		None
 */
static fast_code void hmb_pre_init(hmb_ctx_t *ctx)
{
	u32 i;
	u32 size;
	u32 hmb_tbl_mb = 0;
	u32 hmb_buf_mb = 0;
	u32 buf_size;

	size = sizeof(struct nvme_host_memory_desc_entry) * (ctx->list_cnt + 5);
	ctx->ilist = sys_malloc(SLOW_DATA, size);
	sys_assert(ctx->ilist);

	size = sizeof(struct nvme_host_memory_desc_entry) * ctx->list_cnt;
	memcpy(ctx->ilist, ctx->list, size);
	ctx->ilist[ctx->list_cnt].bsize = 0;
	ctx->ilist[ctx->list_cnt + 1].bsize = 0;
	ctx->ilist[ctx->list_cnt + 2].bsize = 0;
	ctx->ilist[ctx->list_cnt + 3].bsize = 0;
	ctx->ilist[ctx->list_cnt + 4].bsize = 0;

	for (i = 0; i < ctx->list_cnt; i++)
	{
		u32 mb;
		u32 hi;
		u32 lo;

		mb = ctx->list[i].bsize >> (MB_SFT - ctx->page_bits);
		if (hmb_buf_mb < (HMB_DTAG_NUM >> 8))
		{
			hmb_buf_mb += mb; // todo: split remain space
			hmb_buf_seg_end++;
		}
		else
		{
			hmb_tbl_mb += mb;
		}

		hi = ctx->list[i].badd >> 32;
		lo = ctx->list[i].badd & 0xffffffff;

		hmb_hal_trace(LOG_INFO, 0xb7ec, "Seg[%d]: %x%x size %d MB",
					  i, hi, lo, mb);
	}

	_hmb_meta_seg = _hmb_crc_seg = _hmb_dmeta_seg = _hmb_ecc_seg = 0xFFFF;

	/* table unit count * crc_size */
	size = HMB_TU_CNT(hmb_tbl_mb) << CRC_SZ_PER_TU_SFT;
	size = occupied_by(size, MIN_SEG_SZ) * MIN_SEG_SZ;
	_hmb_crc_seg = hmb_find_suit_seg(ctx->ilist, ctx->list_cnt + 5,
									 size >> ctx->page_bits, ctx->page_bits);
	hmb_hal_trace(LOG_ALW, 0xd53d, "HMB CRC Seg: %d, %d b", _hmb_crc_seg, size);

	size = (HMB_TU_CNT(hmb_tbl_mb) >> 2) * HMB_META_ENT_SZ;
	size = occupied_by(size, MIN_SEG_SZ) * MIN_SEG_SZ;
	_hmb_meta_seg = hmb_find_suit_seg(ctx->ilist, ctx->list_cnt + 5,
									  size >> ctx->page_bits, ctx->page_bits);
	hmb_hal_trace(LOG_ALW, 0x1a2a, "HMB META Seg: %d, %d b", _hmb_meta_seg,
				  size);
	/* alloc hmb buffer meta seg */
	size = HMB_TU_CNT(hmb_buf_mb) * hmb_dtag_get_md_entry_sz();
	size = occupied_by(size, MIN_SEG_SZ) * MIN_SEG_SZ;
	_hmb_dmeta_seg = hmb_find_suit_seg(ctx->ilist, ctx->list_cnt + 5,
									   size >> ctx->page_bits, ctx->page_bits);
	hmb_hal_trace(LOG_ALW, 0xb200, "HMB DTAG META Seg: %d, %d b", _hmb_dmeta_seg,
				  size);

	size = HMB_TU_CNT(hmb_buf_mb) * hmb_dtag_get_ecc_entry_sz();
	size = occupied_by(size, MIN_SEG_SZ) * MIN_SEG_SZ;
	_hmb_ecc_seg = hmb_find_suit_seg(ctx->ilist, ctx->list_cnt + 5,
									 size >> ctx->page_bits, ctx->page_bits);
	hmb_hal_trace(LOG_ALW, 0xad1a, "HMB DTAG ECC Seg: %d, %d b", _hmb_ecc_seg,
				  size);

	/* clear meta seg via DMA */
	zero_tag = dtag_cont_get(DTAG_T_SRAM, HMB_MEM_SET_SIZE >> DTAG_SHF);
	if (zero_tag.dtag == _inv_dtag.dtag)
	{
		buf_size = DTAG_SZE;
		zero_tag = dtag_get_urgt(DTAG_T_SRAM, NULL);
		sys_assert(zero_tag.dtag != _inv_dtag.dtag);
	}
	else
	{
		buf_size = HMB_MEM_SET_SIZE;
	}
	memset(dtag2mem(zero_tag), 0, HMB_MEM_SET_SIZE);

	hmb_mem_set(ctx, dtag2mem(zero_tag), buf_size);
}

fast_code void hmb_attach(struct nvme_host_memory_desc_entry *hmb_desc_list,
						  u32 cnt, u32 page_bits, req_t *req)
{
	hmb_ctx_t *ctx;
	if (hmb_enabled)
		hmb_detach(~0);

	ctx = sys_malloc(SLOW_DATA, sizeof(hmb_ctx_t));
	ctx->list = hmb_desc_list;
	ctx->list_cnt = cnt;
	ctx->page_bits = page_bits;
	ctx->req = req;

	hmb_pre_init(ctx);
	return;
}

fast_code u32 hmb_get_cb_idx(struct _hmb_cmd_cb_t *cb)
{
	return cb - &_hmb_cbs[0];
}

ps_code void hmb_suspend(void)
{
	/*u32 reg_sz = EXP_ROM_SRAM_BASE - HMB_NAND_TBL_CTRL;
	u32 reg_cnt = reg_sz >> 2;
	u32 reg;
	u32 i;

	if (!hmb_enabled)
		return;

	ps_hmb_regs = sys_malloc(FAST_DATA, reg_sz);
	reg = HMB_NAND_TBL_CTRL;
	for (i = 0; i < reg_cnt; i++) {
		ps_hmb_regs[i] = hmb_readl(reg);
		reg += 4;
	}*/
}

ps_code void hmb_resume(bool abort)
{
	u64 *addr_tbl;
	u8 *size_tbl;
	u32 size;
	hmb_nand_tbl_ctrl_t ctrl;

	if (!hmb_enabled)
		return;

	if (abort)
	{
		sys_free(FAST_DATA, ps_hmb_regs);
		return;
	}

	// tbl init
	size = sizeof(u64) * ps_hmb_tbl_cnt;
	addr_tbl = sys_malloc_aligned(SLOW_DATA, size, 128);
	sys_assert(addr_tbl);
	memcpy(addr_tbl, ps_hmb_addr_tbl, size);

	size = sizeof(u8) * ps_hmb_tbl_cnt;
	size_tbl = sys_malloc_aligned(SLOW_DATA, size, 128);
	sys_assert(size_tbl);
	memcpy(size_tbl, ps_hmb_sz_tbl, size);

	hmb_writel(0, HMB_NAND_TBL_CTRL);
	//nvme_hal_trace(LOG_ERR, 0, "resume %x, clear %x\n", temp, hmb_readl(HMB_NAND_TBL_CTRL));

	hmb_writel(sram_to_dma(addr_tbl), HMB_NTBL_SADDR_SRAM_BASE);
	hmb_writel(sram_to_dma(size_tbl), HMB_NTBL_SSIZE_SRAM_BASE);

	sys_assert(hmb_reg_resume_chk(HMB_NTBL_CRC_HADDR_HIGH));
	sys_assert(hmb_reg_resume_chk(HMB_NTBL_CRC_HADDR_LOW));
	sys_assert(hmb_reg_resume_chk(HMB_NTBL_META_HADDR_HIGH));
	sys_assert(hmb_reg_resume_chk(HMB_NTBL_META_HADDR_LOW));
	sys_assert(hmb_reg_resume_chk(HMB_NTBL_META_CTRL));

#if defined(HMB_ENCRYPTION_ENABLE)
	u32 reg;

	reg = HMB_DATA_ENCRYPTION_KEY0;
	do
	{
		sys_assert(hmb_reg_resume_chk(reg));
		reg += 4;
	} while (reg <= HMB_DATA_ENCRYPTION_KEY7);
#endif

	hmb_writel(HMB_XID_UPDT_EN_MASK, HMB_NAND_TBL_CTRL);
	hmb_reg_ps_restore(HMB_NTBL_UPDT_TRANSACTION_ID);

	ctrl.all = hmb_backup_reg(HMB_NAND_TBL_CTRL);
	ctrl.b.hmb_nand_tbl_enable = 1;

	//nvme_hal_trace(LOG_ERR, 0, "re-enable %x\n", ctrl.all);
	hmb_writel(ctrl.all, HMB_NAND_TBL_CTRL);

	/* re-init pointer */
	_hmb_cmd.wptr = 0;
	sys_assert(hmb_reg_resume_chk(FW_NTBL_ACCESS_REQ_SRAM_BASE));
	hmb_writel(0, FW_NTBL_ACCESS_REQ_POINTERS);

	sys_assert(hmb_reg_resume_chk(FW_NTBL_ACCESS_RES0_SRAM_BASE));
	hmb_writel(0, FW_NTBL_ACCESS_RES0_POINTERS);

	// response full bit was set after resume
	//sys_assert(hmb_reg_resume_chk(FW_NTBL_ACCESS_CTRL_STS));
	hmb_reg_ps_restore(FW_NTBL_ACCESS_CTRL_STS);

#if AUTO_LOOKUP_ENABLE
	sys_assert(hmb_reg_resume_chk(AUTO_LKUP_RES0_SRAM_BASE));
	hmb_writel(0, AUTO_LOOKUP_RES0_PTRS);
	// response full bit was set after resume
	//sys_assert(hmb_reg_resume_chk(HMB_NTBL_AUTO_LKUP_CTRL_STS));
	hmb_reg_ps_restore(HMB_NTBL_AUTO_LKUP_CTRL_STS);
#endif

	do
	{
		ctrl.all = hmb_readl(HMB_NAND_TBL_CTRL);
		if (ctrl.b.hmb_ntbl_size_fetch_done)
			break;
	} while (1);

	sys_free_aligned(SLOW_DATA, size_tbl);
	sys_free_aligned(SLOW_DATA, addr_tbl);

	sys_free(FAST_DATA, ps_hmb_regs);

	// reg check
	sys_assert(hmb_reg_resume_chk(HMB_NTBL_UPDT_TRANSACTION_ID));
}
#endif

/*! @} */
