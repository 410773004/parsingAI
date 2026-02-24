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
 * @brief Rawdisk support, multiple cpu version
 *
 * \addtogroup dispatcher
 * \defgroup rawdisk
 * \ingroup dispatcher
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "btn_export.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "queue.h"
#include "misc.h"
#include "ncl_exports.h"
#include "console.h"
#include "crc32.h"
#include "srb.h"
#include "fw_download.h"
#include "rainier_soc.h"
#include "pmu.h"
#include "ddr.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "helper.h"
#include "ipc_api.h"
#include "rawdisk_mpc.h"
#include "cbf.h"

/*! \cond PRIVATE */
#define __FILEID__ rawfe
#include "trace.h"
/*! \endcond */

#include "btn_helper.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define EACH_DIE_PAGE_CNT	(4 * 3)					///< page count per die, up to 4 PL * 3WL
#define EACH_DIE_DTAG_CNT	(EACH_DIE_PAGE_CNT * DU_CNT_PER_PAGE)	///< dtag count per die

#define PADDING_BTAG		511			///< definition of padding ctag,  (1<<9) - 1
#define PADDING_LDA		0xFFFFFFFF		///< definition of padding lda
#define UNMAPPING_PDA		0xFFFFFFFF		///< definition of unmapped pda

#define RAWDISK_LDA_CNT		(16384)			///< 64MB capacity

/*! @brief for rainier, put erase/write command resource in sram */
#define RAW_IO_CNT		128	///< rawdisk mpc large read and write command resource count

#define RAWDISK_STREAMING_MODE_DTAG_CNT		32

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

/*!
 * @brief definition of metadata structure
 */
typedef struct du_meta_fmt meta_t;

/*!
 * @brief SPB state definitions
 */
enum spb_state {
	SPB_ST_TO_ERASE,	///< need to be erased
	SPB_ST_ERASED,		///< SPB is erased
};

/*!
 * @brief Write type of definitions
 */
enum wr_type_t {
	WR_T_CACHE_ON,		///< write cache enabled
	WR_T_CACHE_OFF,		///< write cache disabled
};

/*!
 * @brief BM for write qid definitions
 */
enum wr_qid_t {
	WR_QID_COM_FREE,	///< program destination queue is common free queue
	WR_QID_PDONE		///< program destination queue is pdone queue
};

/*!
 * @brief NCL read command resource for partial write
 */
typedef struct _par_ncl_cmd_t {
	struct ncl_cmd_t ncl_cmd;	///< ncl command
	bm_pl_t bm_pl;			///< bm_pl for ncl command
	pda_t pda;			///< pda list buffer for ncl command
	struct info_param_t info;	///< info list buffer for ncl command
} par_ncl_cmd_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
share_data struct nand_info_t shr_nand_info;		///< nand info in shared memory
share_data volatile void *shr_dummy_meta;			///< share index meta pointer to BE cpu
share_data void *shr_dtag_meta;				///< share dtag meta pointer to BE cpu
share_data volatile pda_t btag2pda[BTN_CMD_CNT];			///< fast read resource, pass pda to BE cpu by btag
share_data volatile u16 nvm_cmd_id[BTN_CMD_CNT];			///< fast read resource, pass nvm_cmd_id to BE cpu by btag

share_data volatile struct ncl_cmd_t _ncl_cmd[RAW_IO_CNT];	///< NCL cmds
share_data volatile ncl_dat_t _ncl_data[RAW_IO_CNT];		///< NCL data

slow_data meta_t _dummy_meta[NR_IDX_META] ALIGNED(32);		///< index mode meta, use for read dummy
slow_data meta_t *dummy_meta = &_dummy_meta[0];
slow_data meta_t dtag_meta[SRAM_IN_DTAG_CNT] ALIGNED(32);	///< dtag mode meta for sram, use for write
#ifdef DDR
fast_data meta_t *ddtag_meta = NULL;		///< dtag meta meta for ddr, use for write
#endif
static fast_data ncl_dat_t *wr_pl_cur = NULL;		///< current write payload

/* SPB and its write pointer */
static fast_data u16 cur_spb = 0;		///< current SPB
static fast_data u16 nxt_spb;			///< next SPB
static fast_data u32 cur_spb_pda_wptr = 0;	///< current SPB write pointer of pda

static fast_data u16 spb_total;		///< total SPB number
static fast_data u32  width_nominal;	///< interleave, #CH *#DIE * #PLN
static fast_data u8  width_in_dws;	///< hook to Dword
static fast_code u8  pda_cnt_multi_pln;	///< multi-plane pda number
static fast_data u32 pda_cnt_in_slc_spb;	///< pda number in SLC SPB
static fast_data enum spb_state *spb_states;	///< SPB states, to be erase or erased

static fast_data bm_pl_t wb_unmap_pl[MAX_NCL_DATA_LEN];	///< write buffer payload
static fast_data s32 lda_in_wb_unmap_cnt = 0;		///< number of unmap lda in write buffer payload
static fast_data u64 lda_in_wb_unmap_bitmap = 0;	///< bitmap of unmap lda in write buffer payload

static fast_data u32 defect_bitmap_size;	///< defect bitmap size
static fast_data u32 *defect_bitmap;		///< defect bitmap

fast_data pda_t *l2p;					///< lda to pda array.
share_data volatile pda_t *shr_l2p;				///< share l2p pointer

share_data volatile enum rd_type_t rrd_type = RD_T_STREAMING;	///< BM read type
fast_data enum wr_type_t rwr_type = WR_T_CACHE_ON;	///< BM write type
fast_data enum wr_qid_t wr_qid = WR_QID_COM_FREE;	///< BM write queue ID

fast_data u32 *valid_cnt;				///< valid count

static fast_data struct timer_list flush_timer;		///< flush timer

static fast_data u16 du_format = DU_4K_DEFAULT_MODE;	///< data unit format, default is 4K mode

#define nid2cmd(nid)		(&_ncl_cmd[nid])	///< get ncl cmd from id in ncl data

static fast_data pool_t ncl_dat_pool;			///< NCL data pool

// The below is for TLC mode
share_data volatile bool rawdisk_tlc_mode = true;		///< indicate rawdisk is in tlc mode or not
fast_data u16 end_slc_spb;				///< final slc spb
fast_data u16 width_nominal_for_pda;			///< pda width nominal
fast_data u8  pda_cnt_multi_pln_per_wl;			///< pda number per word line in multi plane
fast_data u16 start_spb;				///< start spb
fast_data u16 start_nxt_spb;				///< next spb
fast_data u16 write_pda_per_il = 0;			///< write pda per interleave
fast_data u32 pda_cnt_in_xlc_spb;			///< pda number in XLC SPB

fast_data dtag_t dst;			///< partial write merge destionation dtag
fast_data bm_pl_que_t par_pl_q;		///< payload queue for partial write data entry
fast_data u32 par_handling = 0;		///< if partial write entry was handling, current rawdisk only support 1
fast_data par_ncl_cmd_t _par_ncl_cmd;	///< ncl command resource for partial write data entry

share_data volatile bool enable_rawdisk_defect_scan = false;

extern u8 evt_reset_disk;				///< reset disk event when pcie reset

static fast_data u32 last_wr_cnt = 0, wr_cnt = 0;

typedef CBF(bm_pl_t, RAWDISK_DTAG_CNT) wde_queue_t;	///< queue type to buffer write data entrty if ncl data resource was not enough

fast_data wde_queue_t wde_queue;			///< write data entry buffer queue
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
/*!
 * @brief check data is in write cache or not
 *
 * If data was in write cache (wr_pl_cur), we could return data from sram
 * instead of reading from nand. from write cache.
 *
 * @param slda		start lda
 * @param ndu		number of du
 * @param btag		btn command tag
 * @param ofst		du offset in origin command
 *
 * @return		collision count
 */
static void rawdisk_collision(lda_t slda, u32 ndu, u32 btag, u32 ofst);

/*!
 * @brief lookup l2p and setup pda and bm payload list for read
 *
 * @param btag	btn command tag
 * @param dat	ncl data for read command
 *
 * @return 	not used
 */
static void rawdisk_read_prep(int btag, ncl_dat_t *dat);

/*!
 * @brief issue ncl read command
 *
 * @param dat		ncl data
 * @param sync		true for read synchronously
 * @param op_type	read op type, refer to ncl_op_type_t
 *
 * @return		return command status when sync == true
 */
static int rawdisk_read(ncl_dat_t *dat, bool sync, enum ncl_op_type_t op_type);


/*!
 * @brief handler of partial bm payload queue
 *
 * do read-modify-write
 * if target in write cache: dpe copy -> dpe merge
 * if target in nand: ncl read -> dpe merge
 * if target is not exist: dpe fill -> dpe merge
 *
 * @return		not used
 */
static void rawdisk_par_q_handle(void);

/*!
 * @brief handle sub read request, a sub read request is 128K
 *
 * @param btag		btn command tag
 * @param dat		ncl data
 *
 * @return		not used
 */
static void sub_rd_req_exec(int btag, ncl_dat_t *dat);

/*!
 * @brief resume all pending read/write request, should be called when ncl data return
 *
 * @return		not used
 */
static void rawdisk_resume_req(void);
static void rawdisk_write_prep(ncl_dat_t *dat);
static void rawdisk_write_prep_for_tsb_tlc(ncl_dat_t *dat);
static void rawdisk_write(ncl_dat_t *dat, bool sync, enum ncl_op_type_t op_type);


fast_code void *rawdisk_malloc(u32 size)
{
	void *ret;

	ret = sys_malloc(FAST_DATA, size);
	if (ret == NULL)
		ret = share_malloc(size);

	sys_assert(ret);
	return ret;
}

fast_code void rawdisk_mfree(void *p)
{
	u32 addr = (u32) p;

	if (addr >= BTCM_SH_BASE)
		share_free(p);
	else
		sys_free(FAST_DATA, p);
}

/*!
 * @brief complete write request
 *
 * Parse BM payload list to reduce transfer count of request, when dtag transfer
 * count of request was reduced to zero, complete request by calling completion
 * request.
 *
 * @param pl		BM payload list
 * @param count		length of BM payload list
 *
 * @return		not used
 */
static fast_code void
rawdisk_req_wr_cmpl(bm_pl_t *pl, int count)
{
	int i = 0;

	for (i = 0; i < count; i++, pl++) {
		if (pl->pl.btag == PADDING_BTAG)
			continue;

		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(pl->pl.btag);

		/* Both Xfer Done & Cmd Proc Done, then complete after NRM done
		   for Cache On and NCB done for Cache Off */
		if (--bcmd_ex->du_xfer_left == 0) {
			btn_cmd_rels_push(btag2bcmd(pl->pl.btag), RLS_T_WRITE_CQ);
		}
	}
}

/*!
 * @brief NCL erase command callback function
 *
 * While NCL erase command completed, free NCL command and resources.
 *
 * @param ncl_cmd	NCL erase command
 *
 * @return		not used
 */
static void rawdisk_erase_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	u32 spb_id = (u32) ncl_cmd->caller_priv;
	pda_t *pda = (pda_t *) ncl_cmd->addr_param.common_param.pda_list;

	rawdisk_mfree(pda);
	spb_states[spb_id] = SPB_ST_ERASED;

	rawdisk_mfree(ncl_cmd);
}

/*!
 * @brief NCL write command callback function
 *
 * While NCL write command is completed, free resources.
 *
 * @param ncl_cmd	NCL write command
 *
 * @return		not used
 */
static void rawdisk_write_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	ncl_dat_t *dat = (ncl_dat_t *) ncl_cmd->caller_priv;

	if (rwr_type == WR_T_CACHE_OFF)
		rawdisk_req_wr_cmpl(dat->bm_pl, dat->count * DU_CNT_PER_PAGE);

	pool_put_ex(&ncl_dat_pool, dat);
	rawdisk_resume_req();
}

/*!
 * @brief handler of bm read error data entry
 *
 * @param bm_pl		error data entry
 *
 * @return		not used
 */
fast_code static void rawdisk_bm_read_error(bm_pl_t *bm_pl)
{
	dtag_t dtag;

	dtag.dtag = bm_pl->pl.dtag;

	bm_free_aurl_return(&dtag, 1);
}

fast_code static void rawdisk_err_wde_handle(bm_pl_t *bm_pl)
{
	dtag_t dtag = { .dtag = bm_pl->pl.dtag };

#if CPU_DTAG == CPU_ID
	dtag_put(RAWDISK_DTAG_TYPE, dtag);
#else
	cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_DTAG_PUT, 0, dtag.dtag);
#endif
}

/*!
 * @brief check NCL command status to set error to requests
 *
 * While read error is happened, check DU status of NCL command and setup error
 * to requests.
 *
 * @param ncl_cmd	NCL read command
 *
 * @return		not used
 */
slow_code static void rawdisk_read_error(struct ncl_cmd_t *ncl_cmd)
{
	u32 i = 0;
	ncl_dat_t *dat = ncl_cmd->caller_priv;
	bm_pl_t *pl = ncl_cmd->user_tag_list;

	do {
		u32 s;

		s = ncl_cmd->addr_param.common_param.info_list[i].status;
		if (!ficu_du_data_good(s)) {
			disp_apl_trace(LOG_ERR, 0xa112, "sts %d, l/p %x,%x PDA(0x%x), SPB %d",
				       s, dat->lda[i], dat->pda[i], l2p[dat->lda[i]],
				       valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]);

			disp_apl_trace(LOG_ERR, 0x867e, "btag %d, ofst %d ret error",
					pl->pl.btag, pl->pl.du_ofst);

			bm_err_commit(pl->pl.du_ofst, pl->pl.btag);
		}
		i++;
		pl++;
	} while (i < ncl_cmd->addr_param.common_param.list_len);
}

fast_code static void rawdisk_read_prep(int btag, ncl_dat_t *dat)
{
	int i;
	int j = 0;
	u16 ndu = dat->ndu;
	lda_t lda = dat->slda;
	bm_pl_t *pl;
	btn_cmd_t *bcmd = btag2bcmd(btag);

	pl = dat->bm_pl;
	for (i = 0; i < ndu; i++, lda++) {
		if (test_bit(i, &dat->hit_bmp))
			continue;

		pl->pl.nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
		pl->pl.btag = btag;
		pl->pl.du_ofst = i + dat->ofst;

		// must be streaming mode
		pl->pl.dtag = READ_DUMMY_IDX; // streaming mode: it's meta index
		pl->pl.type_ctrl = META_SRAM_IDX;

		dat->lda[j] = lda;
		dat->pda[j] = l2p[lda];
		disp_apl_trace(LOG_DEBUG, 0x7ded, "lda(0x%x) -> pda(0x%x), nid %x, btag %x, type %x dtag %x",
			       lda, dat->pda[j], pl->pl.nvm_cmd_id, btag, pl->pl.type_ctrl, pl->pl.dtag);

		if (!rawdisk_tlc_mode) {
			dat->info[j].pb_type = NAL_PB_TYPE_SLC;
			dat->info[j].xlc.slc_idx = 0;
		} else {
			dat->info[j].pb_type = NAL_PB_TYPE_XLC;
			dat->info[j].xlc.slc_idx = nal_get_tlc_pg_idx_in_wl(dat->pda[j]);
		}

		pl++;
		j++;
	}

	dat->count = j;
}

/*!
 * @brief check if any pending read command in pending list, and handle it
 *
 * @return	not used
 */
fast_code static void rawdisk_resume_read_req(void)
{
	while (!bcmd_list_empty(&bcmd_pending)) {
		ncl_dat_t *dat = pool_get_ex(&ncl_dat_pool);
		btn_cmd_ex_t *bcmd_ex;
		if (dat == NULL)
			return;

		u16 btag = bcmd_list_head(&bcmd_pending);
		sys_assert(btag != 0xffff);

		bcmd_ex = btag2bcmd_ex(btag);
		dat->ofst = bcmd_ex->read_ofst;
		dat->slda = LBA_TO_LDA(bcmd_get_slba(btag2bcmd(btag))) + dat->ofst;
		dat->ndu = min(bcmd_ex->ndu - bcmd_ex->read_ofst, MAX_NCL_DATA_LEN);
		sub_rd_req_exec(btag, dat);
		bcmd_ex->read_ofst += dat->ndu;

		if (bcmd_ex->read_ofst == bcmd_ex->ndu)
			bcmd_list_pop_head(&bcmd_pending);
	}
}

/*!
 * @brief resume pending write data entry
 *
 * @return	not used
 */
fast_code static void rawdisk_resume_write_req(void)
{
	while (!CBF_EMPTY(&wde_queue)) {
		if (wr_pl_cur == NULL) {
			wr_pl_cur = pool_get_ex(&ncl_dat_pool);
			if (wr_pl_cur == NULL)
				break;
			wr_pl_cur->count = 0;
		}
		bm_pl_t *bm_pl = &wde_queue.buf[wde_queue.rptr];
		btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
		lda_t lda;

		lda = LBA_TO_LDA(bcmd_get_slba(bcmd));
		lda += bm_pl->pl.du_ofst;
		wr_pl_cur->lda[wr_pl_cur->count] = lda;
		wr_pl_cur->bm_pl[wr_pl_cur->count] = *bm_pl;
		wr_pl_cur->count++;

		if (rwr_type == WR_T_CACHE_ON)
			rawdisk_req_wr_cmpl(bm_pl, 1);
		else
			mod_timer(&flush_timer, jiffies + 2*HZ/10);

		if (wr_pl_cur->count == pda_cnt_multi_pln) {
			/* issue the write */
			if (!rawdisk_tlc_mode) {
				rawdisk_write_prep(wr_pl_cur);
			} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
				rawdisk_write_prep_for_mu_tlc(wr_pl_cur);
#else
				rawdisk_write_prep_for_tsb_tlc(wr_pl_cur);
#endif
			}

			enum ncl_op_type_t op_type = NCL_CMD_PROGRAM_HOST;

			rawdisk_write(wr_pl_cur, false, op_type);

			/* for the rest if any */
			wr_pl_cur = NULL;
		}
		wde_queue.rptr = next_cbf_idx(wde_queue.rptr, wde_queue.size);
	}
}

static fast_code void rawdisk_resume_req(void)
{
	bool write = false;

	if (write) {
		rawdisk_resume_write_req();
		rawdisk_resume_read_req();
	} else {
		rawdisk_resume_read_req();
		rawdisk_resume_write_req();
	}
	write = !write;

}

/*!
 * @brief callback function of read multiple DU command
 *
 * @param ncl_cmd	NCL read command
 *
 * @return		not used
 */
static fast_code void rawdisk_read_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	ncl_dat_t *dat;

	if (ncl_cmd->status & NCL_CMD_ERROR_STATUS) {
		if (ncl_cmd->op_type == NCL_CMD_HOST_READ_DA_DTAG ||
			ncl_cmd->op_type == NCL_CMD_FW_DATA_READ_STREAMING) {
			/* only handle host error */
			rawdisk_read_error(ncl_cmd);
		}
	}

	dat = ncl_cmd->caller_priv;

	// sub req done
	pool_put_ex(&ncl_dat_pool, dat);
	rawdisk_resume_req();
}

/*!
 * @brief rawdisk erase function
 *
 * Issue NCL erase command to erase SPB.
 *
 * @param spb_id	target SPB to be erased
 * @param type		PB (physical block) type, XLC or SLC
 * @param sync		issue command in synchronous (true) or asynchronous mode (false)
 * @param completion	callback function while erase done
 *
 * @return		Return erase success or not
 */
fast_code int rawdisk_erase(u32 spb_id, enum nal_pb_type type, bool sync,
		void (*completion)(struct ncl_cmd_t *))
{
	int i;
	int ret = 0;
	struct ncl_cmd_t *ncl_cmd = rawdisk_malloc(sizeof(*ncl_cmd));
	u32 sz;

	sz = width_nominal * (sizeof(struct info_param_t) + sizeof(pda_t));

	pda_t *pda = rawdisk_malloc(sz);
	struct info_param_t *info_list = ptr_inc(pda, sizeof(pda_t) * width_nominal);

	for (i = 0; i < width_nominal; i++) {
		pda[i] = nal_make_pda(spb_id, i * DU_CNT_PER_PAGE);
		info_list[i].pb_type = type;
		info_list[i].xlc.slc_idx = 0;
	}

	ncl_cmd->completion = sync ? NULL : completion;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = sync ? NCL_CMD_SYNC_FLAG : 0;
	if (type == NAL_PB_TYPE_XLC) {
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	} else {
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	}
	ncl_cmd->addr_param.common_param.list_len = width_nominal;
	ncl_cmd->addr_param.common_param.pda_list = pda;
	ncl_cmd->addr_param.common_param.info_list = info_list;
	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = (void *)spb_id;
	ncl_cmd->du_format_no = du_format;

	ncl_cmd_submit(ncl_cmd);

	if (sync) {
		disp_apl_trace(LOG_DEBUG, 0x69d2, "spb %d erase done", spb_id);
		if (ncl_cmd->status != 0)
			ret = 1;
		spb_states[spb_id] = SPB_ST_ERASED;
		rawdisk_mfree(pda);
		rawdisk_mfree(ncl_cmd);
	}

	return ret;
}

/*!
 * @brief rawdisk write function
 *
 * Issue NCL write command
 *
 * @param dat		NCL information for write command
 * @param sync		issue command in synchronous (true) or asynchronous (false) mode
 * @param op_type	ncl command op type
 *
 * @return		not used
 */
static fast_code void
rawdisk_write(ncl_dat_t *dat, bool sync, enum ncl_op_type_t op_type)
{
	struct ncl_cmd_t *ncl_cmd;

	ncl_cmd = nid2cmd(dat->id);

	ncl_cmd->caller_priv = dat;
	ncl_cmd->completion = sync ? NULL : rawdisk_write_cmpl;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->flags = (sync ? NCL_CMD_SYNC_FLAG : 0) |
			((!rawdisk_tlc_mode) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG);

	ncl_cmd->op_type = op_type;
	ncl_cmd->user_tag_list = dat->bm_pl;

	ncl_cmd->addr_param.common_param.list_len = dat->count;
	ncl_cmd->addr_param.common_param.pda_list = dat->pda;
	ncl_cmd->addr_param.common_param.info_list = dat->info;

	ncl_cmd->du_format_no = du_format;
	ncl_cmd_submit(ncl_cmd);

	if (sync)
		pool_put_ex(&ncl_dat_pool, dat);
}

/*!
 * @brief rawdisk read function
 *
 * Issue NCB read command
 *
 * @param dat		NCL information for read command
 * @param sync		set command is synchronous (true) or asynchronous (false)
 * @param op_type	BM dtag process mode.
 *
 * @return		Return NCB read Success or not
 */
static fast_code int
rawdisk_read(ncl_dat_t *dat, bool sync, enum ncl_op_type_t op_type)
{
	int retval = 0;
	struct ncl_cmd_t *ncl_cmd;
	bm_pl_t *bm_pl = dat->bm_pl;

	ncl_cmd = nid2cmd(dat->id);

	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->flags = (sync ? NCL_CMD_SYNC_FLAG : 0) |
		((!rawdisk_tlc_mode) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG) |
		NCL_CMD_TAG_EXT_FLAG;

	disp_apl_trace(LOG_DEBUG, 0x0886, "op_type = %d, streaming(%d)", op_type,
				op_type == NCL_CMD_FW_DATA_READ_STREAMING);
	ncl_cmd->op_type = op_type;

	ncl_cmd->du_format_no = du_format;
	ncl_cmd->caller_priv = dat;

	ncl_cmd->completion = sync ? NULL : rawdisk_read_cmpl;
	ncl_cmd->user_tag_list = bm_pl;

	ncl_cmd->addr_param.common_param.list_len = dat->count;
	ncl_cmd->addr_param.common_param.pda_list = dat->pda;
	ncl_cmd->addr_param.common_param.info_list = dat->info;
	ncl_cmd_submit(ncl_cmd);

	if (sync) {
		if (ncl_cmd->status == NCL_CMD_ERROR_STATUS)
			retval = -1;
	}

	return retval;
}

/*!
 * @brief check SPB is defect or not
 *
 * @param spb_id	SPB to be checked.
 *
 * @return		true if SPB is defect, false if SPB is good to use
 */
static fast_code bool rawdisk_spb_is_defect_spb(u32 spb_id)
{
	int i;
	u32 *dft = defect_bitmap + spb_id * width_in_dws;

	for (i = 0; i < width_in_dws; i++)  {
		if (*(dft + i) != 0x0)
			return true;
	}
	return false;
}

/*!
 * @brief prepare next candidate SPB
 *
 * Prepare SPB resource to be written. Will also check candidate SPB is defect
 * or not.
 *
 * @param spd_id	current SPB id
 *
 * @return		Candidate suitable SPB id
 */
static fast_code u32 rawdisk_find_next_candidate_spb(u32 spb_id)
{
	spb_id++;

	for (; spb_id < spb_total; spb_id++) {
		if ((rawdisk_spb_is_defect_spb(spb_id) == false)
				&& (valid_cnt[spb_id] == 0)) {
			spb_states[spb_id] = SPB_ST_TO_ERASE;
			return spb_id;
		}
	}

	/* Wrap Around */
	for (spb_id = 1; spb_id < spb_total; spb_id++) {
		if ((rawdisk_spb_is_defect_spb(spb_id) == false)
				&& (valid_cnt[spb_id] == 0)) {
			spb_states[spb_id] = SPB_ST_TO_ERASE;
			return spb_id;
		}
	}

	/* TODO: if you hit here, we'd add GC */

	sys_assert(0);
	return -1;
}

/*!
 * @brief prepare SPB resource to be written
 *
 * @return	not used
 */
static fast_code void rawdisk_pickup_spb(void)
{
	cur_spb = nxt_spb;
	nxt_spb = rawdisk_find_next_candidate_spb(cur_spb);
	disp_apl_trace(LOG_INFO, 0x8aa2, "cur_spb(%d), nxt_spb(%d)", cur_spb, nxt_spb);
	sys_assert(cur_spb != nxt_spb);
	rawdisk_erase(nxt_spb, ((!rawdisk_tlc_mode) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC),
		false, rawdisk_erase_cmpl);
}

#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
/*!
 * @brief prepare metadata, page resource for write for Micron TLC
 *
 * Always use multi-plane write to keep performance, and update table here.
 *
 * @param dat	NCL data structure
 *
 * @return	not used
 */
static fast_code void rawdisk_write_prep_for_mu_tlc(ncl_dat_t *dat)
{
	int i = 0, j = 0;
	pda_t pda;
	bool wl_atomic = false;
	u32 pg;
	u32 pda_idx;
	bm_pl_t *pl = dat->bm_pl;

	pg = cur_spb_pda_wptr / width_nominal_for_pda;
	if (pg < 12) {
	} else if (pg < 36) {
		if ((pg & BIT0) == 0) {
			wl_atomic = true;
		}
	} else if (pg < 60) {
	} else if (pg < 2220) {
		if (((pg - 60) % 3) == 0) {
			wl_atomic = true;
		}
	} else if (pg < 2268) {
		if ((pg & BIT0) == 0) {
			wl_atomic = true;
		}
	} else if (pg < 2292) {
		if ((pg & BIT0) == 0) {
			wl_atomic = true;
		}
	}
	/* flexible configurable cur_spb */
	if (spb_states[cur_spb] != SPB_ST_ERASED) {
		rawdisk_erase(cur_spb, NAL_PB_TYPE_XLC, true, NULL);
	}

	pda_idx = cur_spb_pda_wptr;
	/* XXX: always full page write */
	sys_assert((dat->count & (DU_CNT_PER_PAGE - 1)) == 0);
	for (i = 0; i < dat->count; i++, pl++) {
		if (wl_atomic && (i == pda_cnt_multi_pln_per_wl)) {
			pda_idx = cur_spb_pda_wptr + width_nominal_for_pda;
		}
		if ((i & (DU_CNT_PER_PAGE - 1)) == 0) {
			dat->pda[j] = nal_make_pda(cur_spb, pda_idx);
			dat->info[j].pb_type = NAL_PB_TYPE_XLC;
			dat->info[j].xlc.slc_idx = 0;
			j++;
		}

		/* update L2P mapping */
		if (dat->lda[i] != PADDING_LDA) {

			if (l2p[dat->lda[i]] != UNMAPPING_PDA)
				valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]--;
			pda = nal_make_pda(cur_spb, pda_idx);
			l2p[dat->lda[i]] = pda;
			valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]++;

		} else {
			pda = UNMAPPING_PDA;
		}

		dtag_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, pda_idx));

		++pda_idx;
		disp_apl_trace(LOG_DEBUG, 0x551f, "Index(%d): lda(0x%x) -> pda(0x%x) Dtag(%d)", i, dat->lda[i], l2p[dat->lda[i]], pl->pl.dtag);
	}

	if (wl_atomic) {
		cur_spb_pda_wptr += pda_cnt_multi_pln_per_wl;
		write_pda_per_il += pda_cnt_multi_pln_per_wl;
		if (write_pda_per_il == width_nominal_for_pda) {
			write_pda_per_il = 0;
			cur_spb_pda_wptr += width_nominal_for_pda;
		}
	} else {
		cur_spb_pda_wptr += dat->count;
	}

	if (cur_spb_pda_wptr >= pda_cnt_in_xlc_spb) {
		sys_assert(cur_spb_pda_wptr == pda_cnt_in_xlc_spb);
		cur_spb_pda_wptr = 0;
		rawdisk_pickup_spb();
	}
	/* correct dat count due to PDA -> Page */
	dat->count = j;
}
#else
/*!
 * @brief prepare metadata, page resource for write for TLC
 *
 * Always use multi-plane write to keep performance, and update table here.
 *
 * @param dat	NCL data structure
 *
 * @return	not used
 */
static fast_code void rawdisk_write_prep_for_tsb_tlc(ncl_dat_t *dat)
{
	int i = 0, j = 0;
	u8 idx = 0;
	u32 pda_idx;
	u32 wr_dummy_idx = WRITE_DUMMY_IDX;
	bm_pl_t *pl = dat->bm_pl;
	sys_assert(dat->count == pda_cnt_multi_pln);

	/* flexible configurable cur_spb */
	if (spb_states[cur_spb] != SPB_ST_ERASED) {
		rawdisk_erase(cur_spb, NAL_PB_TYPE_XLC, true, NULL);
	}

	pda_idx = cur_spb_pda_wptr;
	for (i = 0; i < pda_cnt_multi_pln; i++, pl++) {
		if (i == pda_cnt_multi_pln_per_wl) {
			idx = 1;
			pda_idx = cur_spb_pda_wptr + width_nominal_for_pda;
		} else if (i == (pda_cnt_multi_pln_per_wl << 1)) {
			idx = 2;
			pda_idx = cur_spb_pda_wptr + (width_nominal_for_pda << 1);
		}

		if ((i & (DU_CNT_PER_PAGE - 1)) == 0) {
			dat->pda[j] = nal_make_pda(cur_spb, pda_idx);
			dat->info[j].pb_type = NAL_PB_TYPE_XLC;
			dat->info[j].xlc.slc_idx = idx;
			j++;
		}

		/* update L2P mapping */
		if (dat->lda[i] != PADDING_LDA) {
			if (l2p[dat->lda[i]] != UNMAPPING_PDA)
				valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]--;
			l2p[dat->lda[i]] = nal_make_pda(cur_spb, pda_idx);
			valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]++;
		}

		pl->pl.type_ctrl = 0;
		if  (pl->pl.dtag != WVTAG_ID) {
			dtag_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, cur_spb_pda_wptr));
		} else {
			idx_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, cur_spb_pda_wptr), wr_dummy_idx++);
			pl->pl.type_ctrl |= DTAG_QID_DROP;
		}

		++pda_idx;
		disp_apl_trace(LOG_DEBUG, 0xffb8, "lda(0x%x) -> pda(0x%x) Dtag(%d)", dat->lda[i], l2p[dat->lda[i]], pl->pl.dtag);
	}
	cur_spb_pda_wptr += pda_cnt_multi_pln_per_wl;
	write_pda_per_il += pda_cnt_multi_pln_per_wl;

	if (write_pda_per_il == width_nominal_for_pda) {
		write_pda_per_il = 0;
		cur_spb_pda_wptr += (width_nominal_for_pda << 1);
	}

	if (cur_spb_pda_wptr >= pda_cnt_in_xlc_spb) {
		sys_assert(cur_spb_pda_wptr == pda_cnt_in_xlc_spb);
		cur_spb_pda_wptr = 0;
		rawdisk_pickup_spb();
	}

	/* correct dat count due to PDA -> Page */
	dat->count = j;
}
#endif

/*!
 * @brief prepare metadata, page resource for write
 *
 * Always use multi-plane write to keep performance, and update table here.
 *
 * @param dat	NCL data structure
 *
 * @return	not used
 */
static fast_code void rawdisk_write_prep(ncl_dat_t *dat)
{
	int i;
	int j = 0;
	pda_t pda;
	u32 wr_dummy_idx = WRITE_DUMMY_IDX;
	bm_pl_t *pl = dat->bm_pl;

	/* XXX: always full page write */
	sys_assert((dat->count & (DU_CNT_PER_PAGE - 1)) == 0);
	for (i = 0; i < dat->count; i++, pl++) {
		if (cur_spb_pda_wptr >= pda_cnt_in_slc_spb) {
			cur_spb_pda_wptr = 0;
			rawdisk_pickup_spb();
		}

		/* flexible configurable cur_spb */
		if (spb_states[cur_spb] != SPB_ST_ERASED) {
			rawdisk_erase(cur_spb, NAL_PB_TYPE_SLC, true, NULL);
		}

		if ((i & (DU_CNT_PER_PAGE - 1)) == 0) {
			dat->pda[j] = nal_make_pda(cur_spb, cur_spb_pda_wptr);
			dat->info[j].pb_type = NAL_PB_TYPE_SLC;
			dat->info[j].xlc.slc_idx = 0;
			j++;
		}

		/* update L2P mapping */
		if (dat->lda[i] != PADDING_LDA) {
			if (l2p[dat->lda[i]] != UNMAPPING_PDA)
				valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]--;
			pda = nal_make_pda(cur_spb, cur_spb_pda_wptr);
			l2p[dat->lda[i]] = pda;
			valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]++;
		} else {
			pda = UNMAPPING_PDA;
		}

		pl->pl.type_ctrl = 0;

		if (pl->pl.dtag != WVTAG_ID) {
			dtag_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, cur_spb_pda_wptr));
		} else {
			idx_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, cur_spb_pda_wptr), wr_dummy_idx++);
			pl->pl.type_ctrl |= DTAG_QID_DROP;
		}

		cur_spb_pda_wptr++;
		disp_apl_trace(LOG_DEBUG, 0x0fd2, "Index(%d): lda(0x%x) -> pda(0x%x) Dtag(%d)", i, dat->lda[i], l2p[dat->lda[i]], pl->pl.dtag);
	}

	/* correct dat count due to PDA -> Page */
	dat->count = j;
}

/*!
 * @brief Rawdisk flush data to NAND while idle for a while.
 *
 * @param data	caller string
 *
 * @return	not used
 */
static fast_code void rawdisk_idle_flush(void *data)
{
	/* check each 3s to force flush  if idle */
	if ((wr_pl_cur == NULL) || (last_wr_cnt == 0) ||
			(last_wr_cnt != wr_cnt)) {
		last_wr_cnt = wr_cnt;
		mod_timer(&flush_timer, jiffies + 3 * HZ);
		return;
	}

	disp_apl_trace(LOG_ALW, 0xecb4, "%s (%d/%d)", (const char *) data, wr_pl_cur->count, pda_cnt_multi_pln);

	if (wr_pl_cur->count < pda_cnt_multi_pln) {
		u32 i = 0;
		bm_pl_t *pl = wr_pl_cur->bm_pl;
		u32 ofst = wr_pl_cur->count;
		u32 padding_cnt = pda_cnt_multi_pln - ofst;

		for (i = 0; i < padding_cnt; i++) {
			wr_pl_cur->lda[ofst + i] = PADDING_LDA;

			pl[ofst + i].pl.btag = PADDING_BTAG;
			pl[ofst + i].pl.dtag = WVTAG_ID;

			/* we don't care du_ofst since it's to COM_FREE queue */
		}

		wr_pl_cur->count += padding_cnt;
	}
	if (rawdisk_tlc_mode == false) {
		rawdisk_write_prep(wr_pl_cur);
	} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
		rawdisk_write_prep_for_mu_tlc(wr_pl_cur);
#else
		rawdisk_write_prep_for_tsb_tlc(wr_pl_cur);
#endif
	}

	enum ncl_op_type_t op_type = NCL_CMD_PROGRAM_HOST;

	rawdisk_write(wr_pl_cur, false, op_type);

	wr_pl_cur = 0;
	mod_timer(&flush_timer, jiffies + 3 * HZ);
}

/*!
 * @brief normal write queue updated event handler function
 *
 * Receive normal write data entries, and accumulate them to wr_pl_cur, once
 * accumulated count meets minimal flush count(pda_cnt_multi_pln), setup NCL
 * command to write to NAND.
 *
 * @param bm_pl		BM payload
 *
 * @return		not used
 */
static fast_code void _rawdisk_nrm_wd_updt(bm_pl_t *bm_pl)
{
	btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
	lda_t lda;
	bool ret;

	if (!CBF_EMPTY(&wde_queue)) {
		CBF_INS(&wde_queue, ret, *bm_pl);
		return;
	}

	if (wr_pl_cur == NULL) {
		wr_pl_cur = pool_get_ex(&ncl_dat_pool);
		if (wr_pl_cur == NULL) {
			CBF_INS(&wde_queue, ret, *bm_pl);
			sys_assert(ret == true);
			return;
		}
		wr_pl_cur->count = 0;
	}

	lda = LBA_TO_LDA(bcmd_get_slba(bcmd));
	lda += bm_pl->pl.du_ofst;
	wr_pl_cur->lda[wr_pl_cur->count] = lda;
	wr_pl_cur->bm_pl[wr_pl_cur->count] = *bm_pl;
	wr_pl_cur->count++;

	if (rwr_type == WR_T_CACHE_ON)
		rawdisk_req_wr_cmpl(bm_pl, 1);
	else
		mod_timer(&flush_timer, jiffies + 2*HZ/10);

	if (wr_pl_cur->count == pda_cnt_multi_pln) {
		/* issue the write */
		if (!rawdisk_tlc_mode) {
			rawdisk_write_prep(wr_pl_cur);
		} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
			rawdisk_write_prep_for_mu_tlc(wr_pl_cur);
#else
			rawdisk_write_prep_for_tsb_tlc(wr_pl_cur);
#endif
		}

		enum ncl_op_type_t op_type = NCL_CMD_PROGRAM_HOST;

		rawdisk_write(wr_pl_cur, false, op_type);

		/* for the rest if any */
		wr_pl_cur = NULL;
	}
}

/*!
 * @brief normal write queue updated event handler function
 *
 * Receive normal write data entries, and accumulate them to wr_pl_cur, once
 * accumulated count meets minimal flush count(pda_cnt_multi_pln), setup NCL
 * command to write to NAND.
 *
 * @param bm_pl		BM payload
 *
 * @return		not used
 */
fast_code static void rawdisk_nrm_wd_updt(bm_pl_t *bm_pl)
{
	_rawdisk_nrm_wd_updt(bm_pl);
}

/*!
 * @brief DPE merge command done, the last step of read-modify-write
 *
 * @param ctx	caller context, should be partial write data entry
 * @param rst	dpe result
 *
 * @return	not used
 */
fast_code static int rawdisk_merge_done(void *ctx, dpe_rst_t *rst)
{
	bm_pl_t *bm_pl = (bm_pl_t *) ctx;
	dtag_t par;

	sys_assert(&par_pl_q.que[par_pl_q.rptr] == bm_pl);

	par.dtag = bm_pl->pl.dtag;
	sys_assert(par_pl_q.dst.dtag == DTAG_INV);
	par_pl_q.dst = par;

	bm_pl->pl.dtag = dst.dtag;

	_rawdisk_nrm_wd_updt(bm_pl);

	par_handling = 0;
	par_pl_q.rptr++;
	if (par_pl_q.rptr >= BM_PL_QUE_SZ)
		par_pl_q.rptr = 0;

	rawdisk_par_q_handle();
	return 0;
}

/*!
 * @brief the 1st step of read-modify-write if cache hit
 *
 * if read-modify-write target is in write cache, we will copy it to
 * a destination dtag and merge partial write data entry to destination
 *
 * @param ctx	caller context, should be source dtag (in write cache)
 * @param rst	dpe result
 *
 * @return	not used
 */
fast_code static int rawdisk_copy_done(void *ctx, dpe_rst_t *rst)
{
	dtag_t dtag = { .dtag = (u32) ctx };

	if (dtag.b.in_ddr == 0)
		cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_DTAG_PUT, 0, dtag.dtag);
	else
		dtag_put(RAWDISK_DTAG_TYPE, dtag);
	return 0;
}

/*!
 * @brief the major step of read-modify-write, merge 2 dtag
 *
 * use dpe engine to merge partial dtag to destination dtag
 *
 * @param bm_pl		partial write data entry
 * @param dst		destination dtag, may be copied from write cache or read from nand
 *
 * @return		not used
 */
fast_code static void rawdisk_merge(bm_pl_t *bm_pl, dtag_t dst)
{
	btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
	dtag_t src = { .dtag = bm_pl->pl.dtag };
	u8 off;
	u8 cnt;
	u64 slba = bcmd_get_slba(bcmd);

	if (bm_pl->pl.du_ofst == 0) {
		// head
		off = LBA_OFST_LDA(slba);
		cnt = get_ua_head_cnt(off, bcmd->dw3.b.xfer_lba_num);

		disp_apl_trace(LOG_DEBUG, 0xacab, "%d: merge %d head %d", bm_pl->pl.btag, (u32) slba, cnt);
		sys_assert(cnt < NR_LBA_PER_LDA);
	} else {
		off = 0;
		cnt = get_ua_tail_cnt(slba, bcmd->dw3.b.xfer_lba_num);
		disp_apl_trace(LOG_DEBUG, 0xac9e, "%d: merge %d tail %d", bm_pl->pl.btag, (u32) slba, cnt);
		sys_assert(cnt < NR_LBA_PER_LDA);
	}
	sys_assert(dst.b.in_ddr == 0);
	bm_data_merge(src, dst, off, cnt, rawdisk_merge_done, bm_pl);
}

/*!
 * @brief ncl read command done of read-modify-write
 *
 * @param ncl_cmd	read command of read-modify-write
 *
 * @return		not used
 */
fast_code static void rawdisk_par_read_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	dtag_t dst;
	bm_pl_t *bm_pl;

	if (ncl_cmd->status != 0)
		panic("read error");

	dst.dtag = ncl_cmd->user_tag_list[0].pl.dtag;
	bm_pl = (bm_pl_t *) ncl_cmd->caller_priv;
	par_handling = 5;

	// collision check again ?
	rawdisk_merge(bm_pl, dst);
}

/*!
 * @brief read step of read-modify-write, if target in nand
 *
 * we use pre-assign read
 *
 * @param pda		target pda
 * @param ctx		caller context
 * @param dtag		dtag of destination
 *
 * @return		not used
 */
fast_code static void rawdisk_par_read(pda_t pda, void *ctx, dtag_t dtag)
{
	struct ncl_cmd_t *ncl_cmd = &_par_ncl_cmd.ncl_cmd;

	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->flags = ((!rawdisk_tlc_mode) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG)
			| NCL_CMD_TAG_EXT_FLAG;

	ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;

	ncl_cmd->du_format_no = du_format;
	ncl_cmd->caller_priv = ctx;

	ncl_cmd->completion = rawdisk_par_read_cmpl;
	ncl_cmd->user_tag_list = &_par_ncl_cmd.bm_pl;

	_par_ncl_cmd.bm_pl.all = 0;
	_par_ncl_cmd.bm_pl.pl.dtag = dtag.dtag;
	_par_ncl_cmd.pda = pda;
	_par_ncl_cmd.bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;

	if (!rawdisk_tlc_mode) {
		_par_ncl_cmd.info.pb_type = NAL_PB_TYPE_SLC;
		_par_ncl_cmd.info.xlc.slc_idx = 0;
	} else {
		_par_ncl_cmd.info.pb_type = NAL_PB_TYPE_XLC;
		_par_ncl_cmd.info.xlc.slc_idx = nal_get_tlc_pg_idx_in_wl(pda);
	}

	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = &_par_ncl_cmd.pda;
	ncl_cmd->addr_param.common_param.info_list = &_par_ncl_cmd.info;
	ncl_cmd_submit(ncl_cmd);
}

fast_code static void rawdisk_par_q_handle(void)
{
	bm_pl_t *bm_pl;
	lda_t lda;
	bool merge;
	void *mem;
	btn_cmd_t *bcmd;
	u64 slba;

	if (par_pl_q.rptr == par_pl_q.wptr)
		return;

	if (par_handling != 0)
		return;

	par_handling = 1;
	sys_assert(par_pl_q.dst.dtag != DTAG_INV);

	bm_pl = &par_pl_q.que[par_pl_q.rptr];
	bcmd = btag2bcmd(bm_pl->pl.btag);

	lda_in_wb_unmap_cnt = 0;
	lda_in_wb_unmap_bitmap = 0;
	slba = bcmd_get_slba(bcmd);
	lda = LBA_TO_LDA(slba) + bm_pl->pl.du_ofst;

	do {
		if (wr_pl_cur)
			rawdisk_collision(lda, 1, bm_pl->pl.btag, 0);
	} while (0);

	dst = par_pl_q.dst;
	sys_assert(dst.dtag != _inv_dtag.dtag);
	par_pl_q.dst.dtag = DTAG_INV;
	mem = dtag2mem(dst);

	if (lda_in_wb_unmap_cnt) {
		dtag_t src = { .dtag = wb_unmap_pl[0].pl.dtag };

		do {
			sys_assert(dst.b.in_ddr == 0);
#if defined(USE_8K_DU)
			bm_data_merge(src, dst, 0, NR_LBA_PER_LDA / 2, NULL, NULL);
			bm_data_merge(src, dst, NR_LBA_PER_LDA / 2, NR_LBA_PER_LDA / 2, rawdisk_copy_done, (void *)src.dtag);
#else
			bm_data_merge(src, dst, 0, NR_LBA_PER_LDA, rawdisk_copy_done, (void *)src.dtag);
#endif
			merge = true;
		} while (0);
	} else {
		pda_t pda;

		pda = l2p[lda];

		if (pda == INV_PDA) {
			bm_data_fill(mem, DTAG_SZE, 0xFFFFFFFF, NULL, NULL);
			merge = true;
		} else {
			par_handling = 3;
			rawdisk_par_read(pda, bm_pl, dst);
			merge = false;
		}
	}

	if (merge) {
		par_handling = 4;
		rawdisk_merge(bm_pl, dst);
	}
}

/*!
 * @brief partial write queue updated event handler function
 *
 * Receive partial write data entries, need to read full dtag back and merge it
 *
 * @param param		not used
 * @param payload	should be BM payload list
 * @param count		length of BM payload list
 *
 * @return		not used
 */
static fast_code void
rawdisk_par_wd_updt(bm_pl_t *bm_pl)
{
	disp_apl_trace(LOG_DEBUG, 0xeb4d, "par in b %d o %d d %d",
				bm_pl->pl.btag, bm_pl->pl.du_ofst, bm_pl->pl.dtag);
	bm_pl_que_ins(&par_pl_q, bm_pl);

	rawdisk_par_q_handle();
}

/*!
 * @brief event handler of btn write data group 0
 *
 * @param param		not used
 * @param payload	addr list of write data entries
 * @param count		list length
 *
 * @return		not used
 */
fast_code static void rawdisk_wd_updt(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32 *) payload;
	u32 i;

	_free_otf_wd -= (int) count;

	for (i = 0; i < count; i++) {
		bm_pl_t *bm_pl = (bm_pl_t *) dma_to_btcm(addr[i]);

		switch (bm_pl->pl.type_ctrl) {
		case BTN_NVM_TYPE_CTRL_NRM:
			rawdisk_nrm_wd_updt(bm_pl);
			break;
		case BTN_NVM_TYPE_CTRL_PAR:
			rawdisk_par_wd_updt(bm_pl);
			break;
		default:
			panic("not support");
			break;
		}
	}
	//reload_free_dtag(NULL);
}

/*!
 * @brief host read queue updated event handler function, only used in HMB dtag
 *
 * @param de		host read done data entry
 *
 * @return		not used
 */
static inline void rawdisk_hst_rd_updt(btn_rd_de_t *de)
{
	_free_otf_rd -= 1;
	bm_radj_push_rel((bm_pl_t *) de, 1);
	//if (_free_otf_rd < 16) {
		reload_free_read_dtag(NULL);
	//}
}

fast_code static void rawdisk_rd_updt(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32*) payload;
	u32 i;

	for (i = 0; i < count; i++) {
		btn_rd_de_t *de = dma_to_btcm(addr[i]);

		if (de->b.type == BTN_NCB_TYPE_CTRL_DYN) {
			rawdisk_hst_rd_updt(de);
		} else {
			panic("stop");
		}
	}

}

/*!
 * @brief check data is in write cache or not
 *
 * If data was in write cache (wr_pl_cur), we could return data from sram
 * instead of reading from nand. from write cache.
 *
 * @param slda		start lda
 * @param ndu		number of du
 * @param btag		btn command tag
 * @param ofst		du offset in origin command
 *
 * @return		collision count
 */
fast_code static void rawdisk_collision(lda_t slda, u32 ndu, u32 btag, u32 ofst)
{
	int i = 0;
	int lda_cnt = wr_pl_cur->count;
	lda_t *lda = &wr_pl_cur->lda[lda_cnt - 1];
	bm_pl_t *pl = &wr_pl_cur->bm_pl[lda_cnt - 1];
	lda_t elda = slda + ndu - 1;

	/* there could be re-written lda, so search reverse */
	for (i = 0; i < lda_cnt; i++, lda--, pl--) {
		u32 lda_ofst;

		if (*lda < slda || *lda > elda)
			continue;

		lda_ofst = *lda - slda;
		if (test_bit(lda_ofst, &lda_in_wb_unmap_bitmap)) {
			disp_apl_trace(LOG_DEBUG, 0x0fc4, "lda(0x%x) is re-written, discard it", *lda);
			continue;
		}

		disp_apl_trace(LOG_DEBUG, 0xf876, "lda(0x%x) is in write buffer", *lda);

		dtag_t dtag = {
			.dtag = pl->pl.dtag,
		};

		if (dtag.b.in_ddr == 0)
			cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_ADD_REF, 0, dtag.dtag);
		else
			dtag_ref_inc(RAWDISK_DTAG_TYPE, dtag);

		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.btag = btag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.dtag = pl->pl.dtag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.du_ofst = lda_ofst + ofst;

		set_bit(lda_ofst, (void *) &lda_in_wb_unmap_bitmap);
		lda_in_wb_unmap_cnt++;
	}
}

/*!
 * @brief check lookup table, use RVTAG for unmapped area
 *
 * @param slda	start lda
 * @param ndu	number of du
 * @param btag	btn command tag
 * @param ofst	du offset start of start lda in origin read request
 *
 * @return	not used
 */
static fast_code void rawdisk_unmap_pda(lda_t slda, u32 ndu, u32 btag, u32 ofst)
{
	int i = 0;
	lda_t lda = slda;

	for (i = 0; i < ndu; i++, lda++) {
		if ((l2p[lda] != UNMAPPING_PDA) ||
			test_bit(i, &lda_in_wb_unmap_bitmap))
			continue;

		disp_apl_trace(LOG_DEBUG, 0x84cb, "lda(0x%x) is in unmapping area", lda);

		btn_cmd_t *bcmd = btag2bcmd(btag);

		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.btag = btag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.dtag = RVTAG_ID;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.nvm_cmd_id  = bcmd->dw0.b.nvm_cmd_id;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.du_ofst = i + ofst;
		set_bit(i, (void *) &lda_in_wb_unmap_bitmap);
		lda_in_wb_unmap_cnt++;
	}
}

#if defined(PI_SUPPORT)
/*!
 * @brief Rawdisk format function, not ready yet
 *
 * Reset lookup table, discard all data in write cache, and setup PI setting
 * for NCL.
 *
 * @param pi_enable	PI (protection info) enable or not
 *
 * @return		not used
 */
static slow_code void rawdisk_format(bool pi_enable)
{
	u32 i;

	disp_apl_trace(LOG_INFO, 0x858c, "rawdisk format: PI %s", (pi_enable ? "en" : "dis"));
	/*! clean map */
	for (i = 0; i < RAWDISK_LDA_CNT; i++) {
		if (l2p[i] != UNMAPPING_PDA) {
			pda_t pda = l2p[i];

			l2p[i] = UNMAPPING_PDA;
			valid_cnt[nal_pda_get_block_id(pda)]--;
		}
	}

	/*! discard write cache */
	if (wr_pl_cur) {
		u32 i;

		for (i = 0; i < wr_pl_cur->dat.count; i++) {
			dtag_t dtag;

			dtag.dtag = wr_pl_cur->pl[i].pl.dtag;
			dtag_put(dtag);
		}
		sys_free(SLOW_DATA, wr_pl_cur->meta);
		sys_free(SLOW_DATA, wr_pl_cur);
		wr_pl_cur = NULL;
	}

	// we should set host DU format to PI DU format, but backend not support yet
	du_format = pi_enable ? DU_4K_PI_MODE : DU_4K_DEFAULT_MODE;
	ncl_pi_control(pi_enable);
}
#endif

fast_code __attribute__((unused)) void sub_rd_req_exec(int btag, ncl_dat_t *dat)
{
	lda_in_wb_unmap_cnt = 0;
	lda_in_wb_unmap_bitmap = 0;

	if (wr_pl_cur != 0)
		rawdisk_collision(dat->slda, dat->ndu, btag, dat->ofst);

	rawdisk_unmap_pda(dat->slda, dat->ndu, btag, dat->ofst);

	if (lda_in_wb_unmap_cnt)
		bm_radj_push_rel(wb_unmap_pl, lda_in_wb_unmap_cnt);

	sys_assert(lda_in_wb_unmap_cnt <= dat->ndu);

	/* all data in write buffer or unmap area */
	if (lda_in_wb_unmap_cnt == dat->ndu) {
		pool_put_ex(&ncl_dat_pool, dat);
		return;
	}

	enum ncl_op_type_t op_type;
	dat->hit_bmp = lda_in_wb_unmap_bitmap;
	dat->hit_cnt = lda_in_wb_unmap_cnt;

	// lookup + unmap + ncl prepare
	rawdisk_read_prep(btag, dat);

#if defined(FAST_MODE)
	op_type = NCL_CMD_FDMA_FAST_READ_MODE;
#else
	if (rrd_type == RD_T_STREAMING) {
		op_type = NCL_CMD_FW_DATA_READ_STREAMING;
	} else {
		op_type = NCL_CMD_HOST_READ_DA_DTAG;
		load_free_read_dtag(dat->count);
	}
#endif

	rawdisk_read(dat, false, op_type);
}

/*!
 * @brief 4k fast read command handler, for short call stack
 *
 * @param bcmd	btn command
 * @param btag	command tag
 *
 * @return	return true if handled
 */
fast_code bool bcmd_fast_read(btn_cmd_t *bcmd, int btag)
{
	if (!bcmd_list_empty(&bcmd_pending))
		return false;

	if (wr_pl_cur != NULL)
		return false;

	lda_t lda = bcmd_get_slba(bcmd) >> NR_LBA_PER_LDA_SHIFT;

	if (l2p[lda] == UNMAPPING_PDA) {
		dtag_t dtag = { .dtag = RVTAG_ID};
		bm_rd_dtag_commit(0, btag, dtag);
		return true;
	} else {
		btag2pda[btag] = l2p[lda];
		nvm_cmd_id[btag] = bcmd->dw0.b.nvm_cmd_id;
		rawdisk_4k_read(btag);
		return true;
	}
}

/*!
 * @brief btn io command handler
 *
 * here, we only handle read/write command, other command type
 * is handled in req_exec
 *
 * @param bcmd	btn command
 * @param btag	btn command tag
 *
 * @return	not used
 */
fast_code void bcmd_exec(btn_cmd_t *bcmd, int btag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	u64 slba;
	ncl_dat_t *dat;

	slba = bcmd_get_slba(bcmd);
	bcmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);

	switch (bcmd->dw0.b.cmd_type) {
	case NVM_READ:
	case PRI_READ:
		bcmd_ex->read_ofst = 0;
		if (!bcmd_list_empty(&bcmd_pending))
			goto pend;

		while (bcmd_ex->read_ofst < bcmd_ex->ndu) {
			dat = pool_get_ex(&ncl_dat_pool);
			if (dat == NULL)
				break;

			dat->ofst = bcmd_ex->read_ofst;
			dat->slda = LBA_TO_LDA(slba) + dat->ofst;
			dat->ndu = min(bcmd_ex->ndu - bcmd_ex->read_ofst, MAX_NCL_DATA_LEN);

			sub_rd_req_exec(btag, dat);

			bcmd_ex->read_ofst += dat->ndu;
		}
pend:
		if (bcmd_ex->read_ofst != bcmd_ex->ndu)
			bcmd_list_ins(btag, &bcmd_pending);
		break;
	case NVM_WRITE:
		/* reload for Free Write */
		load_free_dtag(bcmd_ex->ndu);
		wr_cnt++;
		bcmd_ex->du_xfer_left = bcmd_ex->ndu;
		disp_apl_trace(LOG_DEBUG, 0x140c, "(W) btag(%d) NDU(%d)", btag, bcmd_ex->ndu);
		break;
	case IO_MGR:
		btn_iom_cmd_rels(bcmd);
		break;
	default:
		panic("stop");
		break;
	}
}

/*!
 * @brief request execution function, for non-io request
 *
 * @param req	request to be executed.
 *
 * @return	not used
 */
fast_code bool req_exec(req_t *req)
{
	switch (req->opcode) {
	case REQ_T_READ:
	case REQ_T_WRITE:
		panic("not support");
		break;
#if defined(PI_SUPPORT)
	case REQ_T_FORMAT:
		rawdisk_format(req->op_fields.format.pi_enable);
		break;
#endif
	case REQ_T_FLUSH:
		req->completion(req);
		break;
	default:
		sys_assert(0);
		break;
	}

	return true;
}

/*!
 * @brief read defect table from MR and translate it to smaller size
 *
 * defect table in MR is huge, 16 dwords per each SPB,
 * we read it back and translate it smaller size
 *
 * @return	not used
 */
static init_code void rawdisk_fbbt_trans_bbt(void)
{
	// compact defect bitmap
	defect_bitmap_size = spb_total * width_in_dws * sizeof(u32);
#if defined(DDR)
	u32 ddtag_id = ddr_dtag_register(occupied_by(defect_bitmap_size, DTAG_SZE));

	defect_bitmap = ptr_inc((void *) DDR_BASE, ddtag_id * DTAG_SZE);
#else
	defect_bitmap = sys_malloc(SLOW_DATA, defect_bitmap_size);
#endif
	sys_assert(defect_bitmap);
	memset(defect_bitmap, 0, defect_bitmap_size);

	/* If MR is not in memory, retrieve from media */
	srb_t *srb = (srb_t *) SRAM_BASE;

	if ((srb->srb_hdr.srb_signature != SRB_SIGNATURE) ||
		(srb->srb_hdr.srb_csum != crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum)))) {
		/* Scan SRB block */
		disp_apl_trace(LOG_ERR, 0xea84, "Use Programmer for fbbt build first!\n");
		sys_assert(0);
	} else {
		srb_load_mr_defect((u8 *)defect_bitmap, sizeof(u32));
	}
	disp_apl_trace(LOG_ALW, 0xf12b, "SRB fbbt loaded");
}

/*!
 * @brief reset disk when perst coming, not finished yet
 *
 * @param p0	not used
 * @param p1	dtag to be free if p2 == 2
 * @param p2	reset mode, 0 to cancel write credit, 1 for return dtag, 2 is reset disk
 *
 * @return	not used
 */
slow_code void rawdisk_reset_disk(u32 p0, u32 p1, u32 p2)
{
	if (p2 == 0) {
		_wr_credit = 0;
	} else if (p2 == 1) {
		// move to btn_callbacks_t->write_err
	} else if (p2 == 2) {
		u32 i;
		u32 cnt = 0;
		dtag_t dtag;
		int ret;

		disp_apl_trace(LOG_ALW, 0x4605, "WR: otf free %d cred %d", _free_otf_wd, _wr_credit);
		disp_apl_trace(LOG_ALW, 0x7080, "RD: otf free %d cred %d", _free_otf_rd, _rd_credit);

		// todo consider DDR dtag
		for (i = 0; i < RAWDISK_DTAG_CNT; i++) {
			if (test_bit(i, dtag_otf_bmp)) {
				disp_apl_trace(LOG_ALW, 0xfdbe, "[%d]-> %d alive", cnt, i);
				dtag.dtag = i;
				cnt++;
			}
		}
		if (RAWDISK_DTAG_TYPE == DTAG_T_DDR)
			dtag.b.in_ddr = 1;

		sys_assert(cnt <= 1);
		if (cnt) {
			_free_otf_wd--;
			ret = test_and_clear_bit(dtag.b.dtag, dtag_otf_bmp);
			sys_assert(ret);
			dtag_put(RAWDISK_DTAG_TYPE, dtag);
		}
	}
}

/*!
 * @brief do defect scan, use it if no srb
 *
 * @return	not used
 */
static void fast_code rawdisk_defect_scan(void)
{
	u32 spb_id;

	defect_bitmap_size = spb_total * width_in_dws * sizeof(u32);

	defect_bitmap = sys_malloc(SLOW_DATA, defect_bitmap_size);

	sys_assert(defect_bitmap);
	memset(defect_bitmap, 0, defect_bitmap_size);

	for (spb_id = 2; spb_id < spb_total; spb_id++) {
		rawdisk_erase(spb_id, NAL_PB_TYPE_XLC, true, NULL);
		ncl_spb_defect_scan(spb_id, (u32 *) (defect_bitmap + spb_id * width_in_dws));
	}
}

/*!
 * @brief rawdisk allocate streaming read/write
 *
 * @return		not used
 */
fast_code void rawdisk_alloc_streaming_rw_dtags(void)
{
	UNUSED dtag_t dtag_res[RAWDISK_STREAMING_MODE_DTAG_CNT];
	UNUSED u32 start;
	UNUSED u32 end;
	UNUSED u32 sz;
	UNUSED u32 cnt;
	UNUSED u32 i;

#if !defined(BTN_STREAM_BUF_ONLY)
	start = (u32)&__dtag_stream_read_start;
	end = (u32)&__dtag_stream_read_end;
	sz = end - start;
	cnt = sz / DTAG_SZE;
	disp_apl_trace(LOG_INFO, 0x2db1, "alloc Streaming Read #Dtags(%d) start %x end %x", cnt, start, end);
	sys_assert(cnt == RAWDISK_STREAMING_MODE_DTAG_CNT);
	sys_assert((start & 0xFFF) == 0);
	sys_assert((end & 0xFFF) == 0);

	for (i = 0; i < cnt; i++)
		dtag_res[i] = mem2dtag((void*) start + i * DTAG_SZE);

	bm_free_aurl_load(dtag_res, cnt);
#endif

#if defined(SEMI_WRITE_ENABLE)
	start = (u32)&__dtag_stream_write_start;
	end = (u32)&__dtag_stream_write_end;
	sz = end - start;
	cnt = sz / DTAG_SZE;
	disp_apl_trace(LOG_INFO, 0x66bd, "alloc Streaming Write #Dtags(%d) start %x end %x", cnt, start, end);
	sys_assert(cnt == RDISK_STREAMING_MODE_DTAG_CNT);
	sys_assert((start & 0xFFF) == 0);
	sys_assert((end & 0xFFF) == 0);

	for (i = 0; i < cnt; i++)
		dtag_res[i] = mem2dtag((void*) start + i * DTAG_SZE);

	btn_semi_write_ctrl(true);
	bm_free_semi_write_load(dtag_res, cnt, 0);
#else
	disp_apl_trace(LOG_INFO, 0xc607, "disable Streaming Write");
#endif
}

fast_code void fwdl_op_done(fwdl_req_t *fwdl_req)
{
	req_t *_req = (req_t *) fwdl_req->ctx;
	fwdl_req_op_t op = fwdl_req->op;
	u32 status = fwdl_req->status;

	sys_free(SLOW_DATA, fwdl_req);

	if (op == FWDL_DOWNLOAD) {
		status = 0;
		bm_wait_dpe_data_process_done();
		dtag_put_bulk(DTAG_T_SRAM, _req->req_prp.mem_sz, (dtag_t *)_req->req_prp.mem);
		sys_free(SLOW_DATA, _req->req_prp.mem);
		sys_free(SLOW_DATA, _req->req_prp.prp);
	} else if (op == FWDL_COMMIT) {

	}
	evt_set_imt(evt_cmd_done, (u32)_req, status);
}

fast_code void ipc_fwdl_op_done(volatile cpu_msg_req_t *req)
{
	fwdl_req_t *fwdl_req = (fwdl_req_t *) req->pl;

	fwdl_op_done(fwdl_req);
}

fast_code static void rawdisk_fw_download(u32 p0, u32 _req, u32 not_used)
{
	req_t *req = (req_t *) _req;
	dtag_t *dtags = (dtag_t *) req->req_prp.mem;
	u32 count = req->req_prp.mem_sz;
	fwdl_req_t *fwdl_req = sys_malloc(SLOW_DATA, sizeof(fwdl_req_t));
	sys_assert(fwdl_req);

	fwdl_req->op = FWDL_DOWNLOAD;
	fwdl_req->ctx = (void *) req;
	fwdl_req->field.download.count = count;
	fwdl_req->field.download.dtags = dtags;

	bool ret = fwdl_download(fwdl_req);

	if (ret == true)
		fwdl_op_done(fwdl_req);
}

fast_code static void rawdisk_fw_commit(u32 p0 , u32 fs_ca, u32 _req)
{
	u32 fs = fs_ca >> 16;
	u32 ca = fs_ca & 0xFFFF;
	bool ret;
	req_t *req = (req_t *) _req;
	fwdl_req_t *fwdl_req = sys_malloc(SLOW_DATA, sizeof(fwdl_req_t));
	sys_assert(fwdl_req);

	fwdl_req->field.commit.ca = ca;
	fwdl_req->field.commit.slot = fs;
	fwdl_req->ctx = (void *) req;
	fwdl_req->op = FWDL_COMMIT;

	ret = fwdl_commit(fwdl_req);
	if (ret == true)
		fwdl_op_done(fwdl_req);
}

/*!
 * @brief Rawdisk initialization
 *
 * Setup rawdisk parameters. Register all buffer manager event handler
 * functions.
 *
 * @return	not used
 */
static void init_code rawdisk_init(void)
{
	shr_dummy_meta = dummy_meta;
	shr_dtag_meta = dtag_meta;
	local_item_done(ddr_init);
	wait_remote_item_done(be_init);

	width_nominal = nal_get_interleave();
	spb_total = shr_nand_info.geo.nr_blocks;

	if (width_nominal <= 32)
		width_in_dws = 1;
	else if (width_nominal <= 64)
		width_in_dws = 2;
	else if (width_nominal <= 128)
		width_in_dws = 4;
	else
		width_in_dws = 8;

	/* assuming 3:1 but it depends on NAND finally, slc should be related to #WL */
	pda_cnt_in_xlc_spb = DU_CNT_PER_PAGE * shr_nand_info.geo.nr_pages * width_nominal;
	pda_cnt_in_slc_spb = pda_cnt_in_xlc_spb / shr_nand_info.bit_per_cell;  /* no u64 __aeabi_uldivmod, so u32 */

	if (rawdisk_tlc_mode) {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
		pda_cnt_multi_pln = DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes * 2;
#elif USE_TSB_NAND | USE_HYNX_NAND | USE_SNDK_NAND
		pda_cnt_multi_pln = DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes * shr_nand_info.bit_per_cell;
#if QLC_SUPPORT
		// QLC not support yet
		sys_assert(0);
#endif
#else
		sys_assert(0);
#endif
	} else {
		pda_cnt_multi_pln = DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes;
	}
	pda_cnt_multi_pln_per_wl = DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes;
	width_nominal_for_pda = ((u16)width_nominal << DU_CNT_SHIFT);

	par_pl_q.wptr = par_pl_q.rptr = 0;
	_ua_dtag = DTAG_INV;
	ipc_api_remote_dtag_get(&_ua_dtag, true, false);
	sys_assert(_ua_dtag != DTAG_INV);
	par_pl_q.dst.dtag = _ua_dtag;

	sys_assert(pda_cnt_multi_pln <= EACH_DIE_DTAG_CNT);

	spb_states = sys_malloc(FAST_DATA, spb_total);
	sys_assert(spb_states != NULL);

	evt_register(rawdisk_wd_updt, 0, &evt_wd_grp0_nrm_par_upt);
	evt_register(rawdisk_rd_updt, 0, &evt_rd_ent_upt);
	evt_register(wd_err_updt, 0, &evt_wd_err_upt);
	evt_register(rawdisk_reset_disk, 0, &evt_reset_disk);
	evt_register(rawdisk_fw_download, 0, &evt_fw_dwnld);
	evt_register(rawdisk_fw_commit, 0, &evt_fw_commit);

	rawdisk_alloc_streaming_rw_dtags();

	/* prepare unmapping Dtag */
	l2p = sys_malloc(FAST_DATA, sizeof(pda_t) * RAWDISK_LDA_CNT);
	sys_assert(l2p != NULL);
	memset(l2p, UNMAPPING_PDA, sizeof(pda_t) * RAWDISK_LDA_CNT);
	shr_l2p = tcm_local_to_share(l2p);

	// Scan defect table instead of using the result from SRB
	if (enable_rawdisk_defect_scan) {
		rawdisk_defect_scan();
	} else {
		rawdisk_fbbt_trans_bbt();
	}

	int i = 0;

	valid_cnt = sys_malloc(FAST_DATA, sizeof(u32) * spb_total);
	sys_assert(valid_cnt);
	for (i = 2; i < spb_total; i++) {
		spb_states[i] = SPB_ST_TO_ERASE;
		valid_cnt[i] = 0;
	}

	/* find the first good SPB to use */
	{
		u32 spb_id;

		for (spb_id = 2; spb_id < spb_total; spb_id++) {
			if (rawdisk_spb_is_defect_spb(spb_id) == false) {
				cur_spb = spb_id;
				for (++spb_id; spb_id < spb_total; spb_id++) {
					if (rawdisk_spb_is_defect_spb(spb_id) == false) {
						nxt_spb = spb_id;
						break;
					}
				}
				break;
			}
		}
		start_spb = cur_spb;
		start_nxt_spb = nxt_spb;
		disp_apl_trace(LOG_INFO, 0x0dac, "cur_spb(%d), nxt_spb(%d)", cur_spb, nxt_spb);
		if (cur_spb <= 1) {
			disp_apl_trace(LOG_ERR, 0x5a01, "No good SPB, check defect table!");
			sys_assert(0);
		}
	}

	INIT_LIST_HEAD(&flush_timer.entry);
	flush_timer.function = rawdisk_idle_flush;
	flush_timer.data = "idle_flush";
	mod_timer(&flush_timer, jiffies + 3 * HZ);

	btn_callbacks_t callbacks = {
			.hst_strm_rd_err = rawdisk_bm_read_error,
			.write_err = rawdisk_err_wde_handle
	};
	btn_callback_register(&callbacks);

	btn_cmd_hook(bcmd_exec, bcmd_fast_read);
	nvme_ctrl_attr_t ctrl_attr;

	ctrl_attr.all = 0;

	nvme_ns_attr_t attr;
	memset((void*)&attr, 0, sizeof(attr));
	nvmet_set_ctrlr_attrs(&ctrl_attr);

	nvme_ns_attr_t *p_ns = &attr;
	p_ns->hw_attr.nsid = 1;
	p_ns->hw_attr.pad_pat_sel = 1;
	p_ns->fw_attr.support_pit_cnt = 0;

	p_ns->fw_attr.support_lbaf_cnt = 1;

	p_ns->fw_attr.type = NSID_TYPE_ACTIVE;
	p_ns->fw_attr.ncap = LDA_TO_LBA(RAWDISK_LDA_CNT);
	p_ns->fw_attr.nsz = LDA_TO_LBA(RAWDISK_LDA_CNT);
	p_ns->hw_attr.lb_cnt = LDA_TO_LBA(RAWDISK_LDA_CNT);

	nvmet_set_ns_attrs(p_ns, true);

	ipc_api_init();
	cpu_msg_register(CPU_MSG_FWDL_OP_DONE, ipc_fwdl_op_done);

	CBF_INIT(&wde_queue);
	for (i = 0; i < RAW_IO_CNT; i++)
		_ncl_data[i].id = i;

	pool_init(&ncl_dat_pool, (void*) &_ncl_data[0], sizeof(_ncl_data),
			sizeof(_ncl_data[0]), RAW_IO_CNT);

	u32 sz;
#ifdef DDR
	sz = occupied_by(DDR_DTAG_CNT, 32) * sizeof(u32);
#else
	sz = occupied_by(SRAM_IN_DTAG_CNT, 32) * sizeof(u32);
#endif
	dtag_otf_bmp = sys_malloc(FAST_DATA, sz);
	sys_assert(dtag_otf_bmp);
	memset(dtag_otf_bmp, 0, sz);

#ifdef DDR
	sz = occupied_by(DDR_DTAG_CNT * sizeof(meta_t), DTAG_SZE);
	ddtag_meta = (meta_t *) (ddr_dtag_register(sz) * DTAG_SZE + DDR_BASE);
#endif

	nvmet_restore_feat(NULL);

	disp_apl_trace(LOG_INFO, 0x2e19, "Rawdisk init done. DTAG T: %d", RAWDISK_DTAG_TYPE);

	share_heap_init();

#if defined(FPGA) //no partial_chip?
	//rawdisk_selftest();	// this func currupts dtag_refs
#endif
}

module_init(rawdisk_init, DISP_APL);

/*!
 * @brief (DEBUG)Rawdisk UART debug function.
 *
 * @param argc	not used
 * @param argv	not used
 *
 * @return	always return 0
 */
static ps_code int rawdisk_main(int argc, char *argv[])
{
	disp_apl_trace(LOG_ERR, 0x70cc, "\n# of Dtags in write buffer: %d", wr_pl_cur ? wr_pl_cur->count : 0);
	return 0;
}
static DEFINE_UART_CMD(rawdisk, "rawdisk", "rawdisk", "rawdisk: help misc rawdisk information", 0, 0, rawdisk_main);

/*!
 * @brief (DEBUG)Rawdisk write queue id to sting
 *
 * @param qid	write queue id
 *
 * @return	String description for queue id
 */
static ps_code const char *wr_qid_str(enum wr_qid_t qid)
{
	switch (qid) {
	case WR_QID_COM_FREE:
		return "COM_FREE";
	case WR_QID_PDONE:
		return "PDONE";
	default:
		break;
	}

	return "unknown";
}

/*!
 * @brief (DEBUG)Rawdisk Write queue information debug.
 *
 * @param argc	number of argument
 * @param argv	argument list
 *
 * @return	return -1 for wrong input arguments, or 0 for successfully
 */
static ps_code int wr_qid_main(int argc, char *argv[])
{
	if (atoi(argv[1]) > 1)
		return -1;
	disp_apl_trace(LOG_ERR, 0xe3d4, "\nWrite Qid %s -> %s)", wr_qid_str(wr_qid),
			wr_qid_str((enum wr_qid_t) atoi(argv[1])));
	wr_qid = (enum wr_qid_t) atoi(argv[1]);

	return 0;
}

slow_code int tlc_mode_console(int argc, char *argv[])
{
	bool old = rawdisk_tlc_mode;

	if (rwr_type == WR_T_CACHE_OFF) {
		disp_apl_trace(LOG_ERR, 0x5f5a, "\nTLC mode doesn't work for cache off\n");
		return 0;
	}

	if (argc > 1)
		rawdisk_tlc_mode = (bool) atoi(argv[1]);
	else
		rawdisk_tlc_mode = true;

	disp_apl_trace(LOG_ERR, 0x2316, "\nUsing TLC %d -> %d\n", old, rawdisk_tlc_mode);

	// clean mapping
	if (old != rawdisk_tlc_mode) {
		int i;

		disp_apl_trace(LOG_ERR, 0x99ca, "Clean mapping\n");

		cur_spb_pda_wptr = 0;
		cur_spb = start_spb;
		nxt_spb = start_nxt_spb;
		for (i = 2; i < spb_total; i++) {
			spb_states[i] = SPB_ST_TO_ERASE;
			valid_cnt[i] = 0;
		}
		rawdisk_erase(cur_spb, ((!rawdisk_tlc_mode) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC),
			true, NULL);
		rawdisk_erase(nxt_spb, ((!rawdisk_tlc_mode) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC),
			true, NULL);

		memset(l2p, UNMAPPING_PDA, sizeof(pda_t) * RAWDISK_LDA_CNT);

		/*! discard write cache */
		if (wr_pl_cur) {
			u32 i;

			for (i = 0; i < wr_pl_cur->count; i++) {
				dtag_t dtag;

				dtag.dtag = wr_pl_cur->bm_pl[i].pl.dtag;
				dtag_put(RAWDISK_DTAG_TYPE, dtag);
			}
			pool_put_ex(&ncl_dat_pool, wr_pl_cur);
			wr_pl_cur = NULL;
		}
		lda_in_wb_unmap_cnt = 0;
		lda_in_wb_unmap_bitmap = 0;
		write_pda_per_il = 0;

		disp_apl_trace(LOG_ERR, 0x36e6, "Update write width\n");
		pda_cnt_multi_pln_per_wl = DU_CNT_PER_PAGE * shr_nand_info.geo.nr_planes;
		width_nominal_for_pda = ((u16)width_nominal << DU_CNT_SHIFT);
		if (!rawdisk_tlc_mode) {
			pda_cnt_multi_pln = pda_cnt_multi_pln_per_wl;
		} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
			pda_cnt_multi_pln = pda_cnt_multi_pln_per_wl * 2;
#elif USE_TSB_NAND | USE_HYNX_NAND | USE_SNDK_NAND
			pda_cnt_multi_pln = pda_cnt_multi_pln_per_wl * shr_nand_info.bit_per_cell;
#else
			sys_assert(0);
#endif
		}
		sys_assert(pda_cnt_multi_pln <= EACH_DIE_DTAG_CNT);
		sys_assert(pda_cnt_multi_pln <= 64); //meta array size
	}

	return 0;
}

/*!
 * @brief Dynamically enable/disable write cache
 *
 * @param argc	number of argument
 * @param argv	argument list
 *
 * @return	return -1 for wrong input arguments, or 0 for successfully
 */
static ps_code int set_wr_cache_main(int argc, char *argv[])
{
	u32 set_wr_cache_val;

	if (argc == 1) {
		if (rwr_type == WR_T_CACHE_ON)
			disp_apl_trace(LOG_ERR, 0x752e, "\nWrite Cache: ENABLED");
		else
			disp_apl_trace(LOG_ERR, 0x1ca9, "\nWrite Cache: DISABLED");
		return 0;
	}

	set_wr_cache_val =  atoi(argv[1]);
	if (set_wr_cache_val == 0)
		rwr_type = WR_T_CACHE_OFF;
	else if (set_wr_cache_val == 1)
		rwr_type = WR_T_CACHE_ON;
	else {
		disp_apl_trace(LOG_ERR, 0xc386, "\nIncorrect input: %d", set_wr_cache_val);
		return -1;
	}

	return 0;
}

#if defined(USE_TSB_NAND)
/*! \cond PRIVATE */
static DEFINE_UART_CMD(tlc_mode, "tlc_mode", "enter tlc_mode for E2E", "tlc_mode- 0: slc 1: tlc(default)", 0, 1, tlc_mode_console);
/*! \endcond */
#endif

/*! \cond PRIVATE */
static DEFINE_UART_CMD(wr_qid, "wr_qid", "wr_qid- 0: Com_Free 1: FULL", "wr_qid- 0: Com_Free 1: Full", 1, 1, wr_qid_main);
/*! \endcond */

/*! \cond PRIVATE */
static DEFINE_UART_CMD(set_wr_cache, "set_wr_cache",
	"change write cache to enable/disable",
	"syntax: set_wr_cache n [0=disable, 1=enable]",
		0, 1, set_wr_cache_main);
/*! \endcond */
/*! @} */
