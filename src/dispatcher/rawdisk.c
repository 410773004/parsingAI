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
/*! \file rawdisk.c
 * @brief Rawdisk support
 *
 * \addtogroup dispatcher
 *
 * \defgroup rawdisk
 * \ingroup dispatcher
 * @{
 *
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
#include "cbf.h"

/*! \cond PRIVATE */
#define __FILEID__ rawdisk
#include "trace.h"
/*! \endcond */

#include "btn_helper.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

#if !defined(ENABLE_L2CACHE)
#define DEBUG_5372
#endif

#ifdef DEBUG_5372
u32 __off, __cnt;
#endif

#define NR_DU_PER_CMD	33	///< du count per cmd

#define PADDING_BTAG		511		///< definition of padding btag,  (1<<9) - 1
#define PADDING_LDA		0xFFFFFFFF	///< definition of padding lda
#define UNMAPPING_PDA		0xFFFFFFFF	///< definition of unmapped pda

#ifdef DDR
	#if RAWDISK_L2P
		#define RAWDISK_LDA_CNT		(524288)			///< 2GB capacity
	#else
		#define RAWDISK_LDA_CNT		(16384)				///< 64MB capacity
	#endif
	#define RAWDISK_DTAG_TYPE		DTAG_T_DDR
	#define RAWDISK_DTAG_CNT		DDR_DTAG_CNT
#else
	#if RAWDISK_L2P
		#define RAWDISK_LDA_CNT		(524288)			///< 2GB capacity
	#else
		#define RAWDISK_LDA_CNT		(16384)				///< 64MB capacity
	#endif
	#define RAWDISK_DTAG_TYPE		DTAG_T_SRAM
	#define RAWDISK_DTAG_CNT		SRAM_IN_DTAG_CNT
#endif

#ifdef HMB_DTAG
#define RAWDISK_STREAMING_MODE_DTAG_CNT		32
#else
#define RAWDISK_STREAMING_MODE_DTAG_CNT		(MAX_CHANNEL * 4)	///< 4 dtags per channel for streaming read
#endif

#if defined(SEMI_WRITE_ENABLE) && defined(DDR)
#define RAWDISK_SEMI_WRITE_DDR 			true
#else
#define RAWDISK_SEMI_WRITE_DDR 			false
#endif
#define RAWDISK_SEMI_WRITE_DTAG_CNT			32
dtag_t dtag_res[RAWDISK_STREAMING_MODE_DTAG_CNT];

#if defined(HMB_DTAG) || defined(HAVE_VELOCE) || defined(ENABLE_L2CACHE)
#define INTL_INS	0
#else
#define INTL_INS	0	///< insert background internal io
#endif

#if INTL_INS && defined(DDR)
#define INTL_DTAG_TYPE	DTAG_T_DDR	///< use DDR as internal read write dtag
#else
#define INTL_DTAG_TYPE	DTAG_T_SRAM	///< use SRAM as internal read write dtag
#endif

#define MAX_NCL_DATA_LEN	64
#define MAX_NCL_DAT_CNT		32

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

/*!
 * @brief definition of NCL data structure, will serve one sub read request, max 128K
 */
typedef struct {
	pda_t pda[NR_DU_PER_CMD];			///< pda list, 1st element to cast, keep the order
	lda_t lda[NR_DU_PER_CMD];			///< lda list
	struct info_param_t info[NR_DU_PER_CMD];	///< NCL required information list
	bm_pl_t bm_pl[NR_DU_PER_CMD];		///< user tag list
	int count;					///< list length

	// sub req
	lda_t slda;					///< start LBA of this sub read request
	int ndu;					///< du number of this sub read request

	int ofst;					///< du offset in origin read request
	int id;						///< index
	u64 hit_bmp;					///< already handled DU bitmap
	u32 hit_cnt;					///< already handled count
	u64 l2p_oft_hit_bmp;				///< l2p on-the-fly hit bit map, still in l2p update queue
} ncl_dat_t;

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
 * @brief definition of reget dtag for read in evt
 */
typedef struct _reget_dtag_t {
	int btag;		///< request
	ncl_dat_t *dat;		///< ncl data
	u16 got;		///< already got
	u16 required;		///< required dtags count
} reget_dtag_t;

/*!
 * @brief Read type of definitions
 */
enum rd_type_t {
	RD_T_STREAMING,		///< host read in streaming mode
	RD_T_DYNAMIC,		///< not used anymore
};

/*!
 * @brief Write type of definitions
 */
enum wr_type_t {
	WR_T_CACHE_ON,		///< write cache enabled
	WR_T_CACHE_OFF,		///< write cache disabled
};

/*!
 * @brief BM for write qid definitions, not used now
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
slow_data meta_t _dummy_meta[64] ALIGNED(32);			///< index mode meta, use for read dummy
slow_data meta_t *dummy_meta = &_dummy_meta[0];
slow_data meta_t dtag_meta[SRAM_IN_DTAG_CNT] ALIGNED(32);	///< dtag mode meta for sram, use for write
#ifdef DDR
fast_data meta_t *ddtag_meta = NULL;				///< dtag meta meta for ddr, use for write
#endif
static fast_data ncl_dat_t *wr_pl_cur = NULL;			///< current write payload
CBF(bm_pl_t, 1024) bm_pl_buffer;
/* SPB and its write pointer */
static fast_data u16 cur_spb = 0;				///< current SPB
fast_data u16 nxt_spb;						///< next SPB
static fast_data u32 cur_spb_pda_wptr = 0;			///< current SPB write pointer of pda

static fast_data u16 spb_total;					///< total SPB number
static fast_data u32 width_nominal;	///< interleave, #CH *#DIE * #PLN
static fast_data u8  width_in_dws;	///< hook to Dword
static fast_code u8  pda_cnt_multi_pln;	///< multi-plane pda number
static fast_data u32 pda_cnt_in_slc_spb;	///< pda number in SLC SPB
static fast_data enum spb_state *spb_states;	///< SPB states, to be erase or erased

static fast_data bm_pl_t wb_unmap_pl[MAX_NCL_DATA_LEN];	///< write buffer payload for unmapped lda and write cache hit
static fast_data s32 lda_in_wb_unmap_cnt = 0;		///< number of unmap lda in write buffer payload
static fast_data u64 lda_in_wb_unmap_bitmap = 0;	///< bitmap of unmap lda in write buffer payload

static fast_data u32 defect_bitmap_size;	///< defect bitmap size
static fast_data u32 *defect_bitmap;		///< defect bitmap

fast_data pda_t *l2p;					///< lda to pda array.

fast_data enum rd_type_t rrd_type = RD_T_STREAMING;	///< BM read type
fast_data enum wr_type_t rwr_type = WR_T_CACHE_ON;	///< BM write type

fast_data u32 *valid_cnt;				///< valid count table

static fast_data struct timer_list flush_timer;		///< flush timer

static fast_data u16 du_format = DU_4K_DEFAULT_MODE;	///< data unit format, default is 4K mode

static fast_data struct ncl_cmd_t _ncl_cmd[MAX_NCL_DAT_CNT];	///< NCL cmds
static fast_data ncl_dat_t _ncl_data[MAX_NCL_DAT_CNT];		///< NCL data
#define nid2cmd(nid)		(&_ncl_cmd[nid])		///< get ncl cmd from id in ncl data

static fast_data pool_t ncl_dat_pool;				///< NCL data pool

// The below is for TLC mode
#if defined(TSB_XL_NAND) || defined(HAVE_VELOCE)
static fast_data bool rawdisk_tlc_mode = false;		///< XL nand only has slc mode
#else
static fast_data bool rawdisk_tlc_mode = true;		///< indicate rawdisk is in tlc mode or not
#endif
fast_data u16 end_slc_spb;			///< final slc spb
fast_data u16 width_nominal_for_pda;		///< pda width nominal
fast_data u8  pda_cnt_multi_pln_per_wl;		///< pda number per word line in multi plane
fast_data u16 start_spb;			///< start spb
fast_data u16 start_nxt_spb;			///< next spb
fast_data u16 write_pda_per_il = 0;		///< write pda per interleave
fast_data u32 pda_cnt_in_xlc_spb;		///< pda number in XLC SPB

fast_data dtag_t dst;			///< partial write merge destionation dtag
fast_data bm_pl_que_t par_pl_q;		///< payload queue for partial write data entry
fast_data u32 par_handling = 0;		///< if partial write entry was handling, current rawdisk only support 1
fast_data par_ncl_cmd_t _par_ncl_cmd;	///< ncl command resource for partial write data entry

ps_data dtag_t aurl_dtag[RAWDISK_STREAMING_MODE_DTAG_CNT];	///< streaming dtag array
ps_data dtag_t semi_dtag[RAWDISK_SEMI_WRITE_DTAG_CNT];
#if RAWDISK_L2P
#include "rawdisk_l2p.h"
#elif HMB_DTAG
#include "rawdisk_hmb_dtag.h"
#endif

fast_data bool enable_rawdisk_defect_scan = false;	///< force rawdisk defect scan
#if INTL_INS
static fast_data dtag_t intl_sdtag;			///< internal io start dtag
static fast_data struct ncl_cmd_t intl_ncl_cmd;		///< ncl cmd for internal io
static fast_data ncl_dat_t intl_wr_pl;			///< pl for internal io
static fast_data u32 intl_cur_spb_pda_wptr = ~0;	///< internal io write pointer
static fast_data u16 intl_cur_spb;			///< internal io spb
#endif

extern u8 evt_dtag_ins;
extern u8 evt_reset_disk;				///< reset disk event when pcie reset
fast_data u8 evt_io_req_resume = 0xFF;

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
extern void ncl_backend_only_perf_test(void);

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
static fast_code void dat_pool_put(pool_t *pl, void *p);
static fast_code void rawdisk_resume_write_req(void);
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
		if ((--bcmd_ex->du_xfer_left == 0) && (bcmd_ex->flags.b.wr_err == 0)) {	//< normal data path
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
fast_code static void rawdisk_erase_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	u32 spb_id = (u32) ncl_cmd->caller_priv;
	pda_t *pda = (pda_t *) ncl_cmd->addr_param.common_param.pda_list;

	sys_free(FAST_DATA, pda);
	spb_states[spb_id] = SPB_ST_ERASED;

	sys_free(FAST_DATA, ncl_cmd);
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
fast_code static void rawdisk_write_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	ncl_dat_t *dat;
	dat = ncl_cmd->caller_priv;

	if (rwr_type == WR_T_CACHE_OFF)
		rawdisk_req_wr_cmpl(dat->bm_pl, dat->count * DU_CNT_PER_PAGE);

#if defined(SEMI_WRITE_ENABLE)
	if (RAWDISK_SEMI_WRITE_DDR) {
		u32 i;

		for (i = 0; i < dat->count << DU_CNT_SHIFT; i++) {
			bm_pl_t *bm_pl = &dat->bm_pl[i];
			u32 type = bm_pl->pl.type_ctrl & BTN_NCL_QID_TYPE_MASK;
			dtag_t dtag;

			if (bm_pl->pl.dtag == WVTAG_ID)
				continue;

			if (type == BTN_NCB_QID_TYPE_CTRL_DDR_COPY_SEMI_STREAM)
				dtag.dtag = DTAG_IN_DDR_MASK | bm_pl->pl.nvm_cmd_id; // semi-ddr dtag was backup at nvm_cmd_id
			else
				dtag.dtag = bm_pl->pl.dtag;

			dtag_put_ex(dtag);
		}
	}
	else {
		u32 i;
		for (i = 0; i < dat->count << DU_CNT_SHIFT; i ++) {
			bm_pl_t *bm_pl = &dat->bm_pl[i];
			u32 type = bm_pl->pl.type_ctrl & BTN_NCL_QID_TYPE_MASK;
			dtag_t dtag;

			if (bm_pl->pl.dtag == WVTAG_ID)
				continue;

			if (type == BTN_NCB_QID_TYPE_CTRL_DROP) {
				dtag.dtag = bm_pl->pl.dtag;
				dtag_put_ex(dtag);
			}
		}
	}
#endif

#ifdef HMB_DTAG
	u32 i;

	for (i = 0; i < dat->count * DU_CNT_PER_PAGE; i++) {
		dtag_t dtag;

		dtag.dtag = ncl_cmd->user_tag_list[i].pl.dtag;
		dtag_put(DTAG_T_HMB, dtag);
		disp_apl_trace(LOG_DEBUG, 0xa6a7, "prog done: %x", dtag.dtag);
	}
#endif
	dat_pool_put(&ncl_dat_pool, dat);
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
	dtag_t dtag = { .dtag = bm_pl->pl.dtag};
	int ret;

	sys_assert(_free_otf_wd);
	_free_otf_wd--;
	ret = test_and_clear_bit(dtag.b.dtag, dtag_otf_bmp);
	sys_assert(ret);
	dtag_put(RAWDISK_DTAG_TYPE, dtag);
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
			disp_apl_trace(LOG_ERR, 0xe596, "sts %d, l/p %x,%x PDA(0x%x), SPB %d",
				       s, dat->lda[i], dat->pda[i], l2p[dat->lda[i]],
				       nal_pda_get_block_id(l2p[dat->lda[i]]));

			disp_apl_trace(LOG_ERR, 0x8912, "btag %d, ofst %d ret error",
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
#ifdef HMB_DTAG
		pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_HMB;
#else
		// must be streaming mode
		pl->pl.dtag = 0; // streaming mode: it's meta index
		pl->pl.type_ctrl = META_SRAM_IDX;
#endif

		dat->lda[j] = lda;
		dat->pda[j] = l2p[lda];
		disp_apl_trace(LOG_DEBUG, 0xcca8, "lda(0x%x) -> pda(0x%x), nid %x, btag %x, type %x dtag %x",
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
		dat->ndu = min(bcmd_ex->ndu - bcmd_ex->read_ofst, NR_DU_PER_CMD);
#if defined(HMB_DTAG)
		hmb_dtag_sub_rd_req_exec(btag, dat);
#elif defined(RAWDISK_L2P)
		l2p_sub_rd_req_exec(btag, dat);
#else
		sub_rd_req_exec(btag, dat);
#endif
		bcmd_ex->read_ofst += dat->ndu;

		if (bcmd_ex->read_ofst == bcmd_ex->ndu)
			bcmd_list_pop_head(&bcmd_pending);
	}
}

static fast_code void rawdisk_io_req_resume(u32 p0 , u32 fs_ca, u32 _req)
{
	rawdisk_resume_write_req();
	rawdisk_resume_read_req();
}

/*!
 * @brief make sure the rest of request will be handled when put resource back to pool.
 *
 * @return	not used
 */
static fast_code void dat_pool_put(pool_t *pl, void *p)
{
	pool_put_ex(pl, p);
	evt_set_cs(evt_io_req_resume, 0, 0, CS_TASK);
}
/*!
 * @brief callback function of ncl read command
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

#ifdef HMB_DTAG
	// todo: read error parse
	rawdisk_copy_rd_dtags(ncl_cmd);
#endif

	dat = ncl_cmd->caller_priv;

	// sub req done
	dat_pool_put(&ncl_dat_pool, dat);
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
fast_code int
rawdisk_erase(u32 spb_id, enum nal_pb_type type, bool sync,
		void (*completion)(struct ncl_cmd_t *))
{
	int i;
	int ret = 0;
	struct ncl_cmd_t *ncl_cmd = sys_malloc(FAST_DATA, sizeof(*ncl_cmd));
	u32 sz;
	sys_assert(ncl_cmd != NULL);
	sz = width_nominal * (sizeof(struct info_param_t) + sizeof(pda_t));

	pda_t *pda = sys_malloc(FAST_DATA, sz);
	struct info_param_t *info_list = ptr_inc(pda, sizeof(pda_t) * width_nominal);
	sys_assert(pda);

	for (i = 0; i < width_nominal; i++) {
		pda[i] = ftl_remap_pda(nal_make_pda(spb_id, i * DU_CNT_PER_PAGE));
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
	sys_assert(width_nominal <= width_nominal);

	ncl_cmd_submit(ncl_cmd);

	if (sync) {
		disp_apl_trace(LOG_DEBUG, 0xaa45, "spb %d erase done", spb_id);
		if (ncl_cmd->status != 0)
			ret = 1;
		spb_states[spb_id] = SPB_ST_ERASED;
		sys_free(FAST_DATA, pda);
		sys_free(FAST_DATA, ncl_cmd);
	}

	return ret;
}

/*!
 * @brief rawdisk write function
 *
 * Issue NCL write command
 *
 * @param dat		NCL information for write command
 * @param pl		BM payload for write command
 * @param sync		issue command in synchronous (true) or asynchronous (false) mode
 * @param op_type	ncl command op type
 *
 * @return		not used
 */
static fast_code void
rawdisk_write(ncl_dat_t *dat, bm_pl_t *pl, bool sync, enum ncl_op_type_t op_type)
{
	struct ncl_cmd_t *ncl_cmd;

	ncl_cmd = nid2cmd(dat->id);
	sys_assert(ncl_cmd != NULL);

	ncl_cmd->completion = sync ? NULL : rawdisk_write_cmpl;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->flags = (sync ? NCL_CMD_SYNC_FLAG : 0) |
			((!rawdisk_tlc_mode) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG);

	ncl_cmd->op_type = op_type;
	ncl_cmd->user_tag_list = pl;
	ncl_cmd->caller_priv = dat;
	ncl_cmd->addr_param.common_param.list_len = dat->count;
	ncl_cmd->addr_param.common_param.pda_list = dat->pda;
	ncl_cmd->addr_param.common_param.info_list = dat->info;

	ncl_cmd->du_format_no = du_format;
	sys_assert(dat->count <= MAX_NCL_DATA_LEN);
	ncl_cmd_submit(ncl_cmd);

	if (sync) {
		if (ncl_cmd->status == NCL_CMD_ERROR_STATUS)
			return;
	}
}

/*!
 * @brief rawdisk read function
 *
 * Issue NCL read command
 *
 * @param dat		NCL information for read command
 * @param sync		set command is synchronous (true) or asynchronous (false)
 * @param op_type	refer to ncl_op_type_t
 *
 * @return		Return read success or not
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

#if defined(USE_MU_NAND)
	ncl_cmd->flags |= NCL_CMD_FLAGCHECK_FLAG;
#endif

	disp_apl_trace(LOG_DEBUG, 0x310b, "op_type = %d, streaming(%d)", op_type,
				op_type == NCL_CMD_FW_DATA_READ_STREAMING);
	ncl_cmd->op_type = op_type;

	ncl_cmd->du_format_no = du_format;
	ncl_cmd->caller_priv = dat;

	ncl_cmd->completion = sync ? NULL : rawdisk_read_cmpl;
	ncl_cmd->user_tag_list = bm_pl;
    #if NCL_FW_RETRY
	ncl_cmd->retry_step = default_read;
    #endif

	ncl_cmd->addr_param.common_param.list_len = dat->count;
	ncl_cmd->addr_param.common_param.pda_list = dat->pda;
	ncl_cmd->addr_param.common_param.info_list = dat->info;
	sys_assert(dat->count <= MAX_NCL_DATA_LEN);
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
	disp_apl_trace(LOG_INFO, 0xe521, "cur_spb(%d), nxt_spb(%d)", cur_spb, nxt_spb);
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
 * @param pl	write data entries
 *
 * @return	not used
 */
static fast_code void rawdisk_write_prep_for_mu_tlc(ncl_dat_t *dat, bm_pl_t *pl)
{
	int i = 0, j = 0;
	pda_t pda;
	bool wl_atomic = false;
	u32 pg;
	u32 pda_idx;
#if RAWDISK_L2P
	l2p_otf_t *l2p_otf = sys_malloc(FAST_DATA, sizeof(l2p_otf_t));
	sys_assert(l2p_otf);
	l2p_otf->cnt = 0;
#endif
	static u32 last_pg = ~0;

	pg = cur_spb_pda_wptr / width_nominal_for_pda;
	if (pg != last_pg) {
		disp_apl_trace(LOG_ERR, 0xc860, "%d\n", pg);
		last_pg = pg;
	}

#if defined(MU_B27B)
	wl_atomic = mu_get_xlc_cwl(pg) == 1 ? false : true;
#else
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
#endif
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
#if !RAWDISK_L2P //normal rawdisk
			if (l2p[dat->lda[i]] != UNMAPPING_PDA)
				valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]--;
			pda = nal_make_pda(cur_spb, pda_idx);
			l2p[dat->lda[i]] = pda;
			valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]++;
#elif RAWDISK_L2P //rawdisk_l2p
			pda = nal_make_pda(cur_spb, pda_idx);

			l2p_otf->lda[l2p_otf->cnt] = dat->lda[i];
			l2p_otf->pda[l2p_otf->cnt] = pda;
			l2p_otf->cnt++;

			valid_cnt[nal_pda_get_block_id(pda)]++;
#endif
		} else {
			pda = UNMAPPING_PDA;
		}

		dtag_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, pda_idx));
#ifdef HMB_DTAG
		pl->pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_HMB;
#endif

		++pda_idx;
		disp_apl_trace(LOG_DEBUG, 0xe5e1, "Index(%d): lda(0x%x) -> pda(0x%x) Dtag(%d)", i, dat->lda[i], l2p[dat->lda[i]], pl->pl.dtag);
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

#if RAWDISK_L2P
	rawdisk_l2p_updt(l2p_otf);
	list_add_tail(&l2p_otf->entry, &l2p_otf_list);
#endif
}
#else
/*!
 * @brief prepare metadata, page resource for write for TLC
 *
 * Always use multi-plane write to keep performance, and update table here.
 *
 * @param dat	NCL data structure
 * @param pl	write data entries
 * @param meta	metadata buffer
 *
 * @return	not used
 */
static fast_code void rawdisk_write_prep_for_tsb_tlc(ncl_dat_t *dat, bm_pl_t *pl)
{
	int i = 0, j = 0;
	u8 idx = 0;
	u32 pda_idx;
#if RAWDISK_L2P
	l2p_otf_t *l2p_otf = sys_malloc(FAST_DATA, sizeof(l2p_otf_t));
	sys_assert(l2p_otf);
	l2p_otf->cnt = 0;
#endif

	sys_assert(dat->count == pda_cnt_multi_pln);

	/* flexible configurable cur_spb */
	if (spb_states[cur_spb] != SPB_ST_ERASED)
		rawdisk_erase(cur_spb, NAL_PB_TYPE_XLC, true, NULL);

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
#if !RAWDISK_L2P //normal rawdisk
		if (dat->lda[i] != PADDING_LDA) {
			if (l2p[dat->lda[i]] != UNMAPPING_PDA)
				valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]--;
			l2p[dat->lda[i]] = nal_make_pda(cur_spb, pda_idx);
			valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]++;
		}
#elif RAWDISK_L2P //rawdisk_l2p
		if (dat->lda[i] != PADDING_LDA) {
			pda_t pda = nal_make_pda(cur_spb, pda_idx);

			l2p_otf->lda[l2p_otf->cnt] = dat->lda[i];
			l2p_otf->pda[l2p_otf->cnt] = pda;
			l2p_otf->cnt++;

			valid_cnt[nal_pda_get_block_id(pda)]++;
		}
#endif
		pl->pl.type_ctrl = 0;
		dtag_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, cur_spb_pda_wptr));
#ifdef HMB_DTAG
		pl->pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_HMB;
#endif

		++pda_idx;
		disp_apl_trace(LOG_DEBUG, 0x0261, "lda(0x%x) -> pda(0x%x) Dtag(%d)", dat->lda[i], l2p[dat->lda[i]], pl->pl.dtag);
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

#if RAWDISK_L2P
	rawdisk_l2p_updt(l2p_otf);
	list_add_tail(&l2p_otf->entry, &l2p_otf_list);
#endif
}
#endif

/*!
 * @brief prepare metadata, page resource for write
 *
 * Always use multi-plane write to keep performance, and update table here.
 *
 * @param dat	NCL data structure
 * @param pl	write data entries
 *
 * @return	not used
 */
static fast_code void rawdisk_write_prep(ncl_dat_t *dat, bm_pl_t *pl)
{
	int i;
	int j = 0;
	pda_t pda;
#if RAWDISK_L2P
	l2p_otf_t *l2p_otf = sys_malloc(FAST_DATA, sizeof(l2p_otf_t));
	sys_assert(l2p_otf);
	l2p_otf->cnt = 0;
#endif
	/* XXX: always full page write */
	sys_assert((dat->count & (DU_CNT_PER_PAGE - 1)) == 0);
	for (i = 0; i < dat->count; i++, pl++) {
		if (cur_spb_pda_wptr >= pda_cnt_in_slc_spb) {
			cur_spb_pda_wptr = 0;
			rawdisk_pickup_spb();
		}

		/* flexible configurable cur_spb */
		if (spb_states[cur_spb] != SPB_ST_ERASED)
			rawdisk_erase(cur_spb, NAL_PB_TYPE_SLC, true, NULL);

		if ((i & (DU_CNT_PER_PAGE - 1)) == 0) {
			dat->pda[j] = nal_make_pda(cur_spb, cur_spb_pda_wptr);
			dat->info[j].pb_type = NAL_PB_TYPE_SLC;
			dat->info[j].xlc.slc_idx = 0;
			j++;
		}

		/* update L2P mapping */
		if (dat->lda[i] != PADDING_LDA) {
#if !RAWDISK_L2P //normal rawdisk
			if (l2p[dat->lda[i]] != UNMAPPING_PDA)
				valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]--;
			pda = nal_make_pda(cur_spb, cur_spb_pda_wptr);
			l2p[dat->lda[i]] = pda;
			valid_cnt[nal_pda_get_block_id(l2p[dat->lda[i]])]++;
#elif RAWDISK_L2P //rawdisk_l2p
			pda = nal_make_pda(cur_spb, cur_spb_pda_wptr);

			l2p_otf->lda[l2p_otf->cnt] = dat->lda[i];
			l2p_otf->pda[l2p_otf->cnt] = pda;
			l2p_otf->cnt++;

			valid_cnt[nal_pda_get_block_id(pda)]++;
#endif
		} else {
			pda = UNMAPPING_PDA;
		}

#if defined(SEMI_WRITE_ENABLE)
		if (pl->pl.dtag != WVTAG_ID) {
			if ((pl->pl.type_ctrl & BTN_SEMI_MODE_MASK) == BTN_SEMI_STREAMING_MODE) {
				pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM;
			} else if ((pl->pl.type_ctrl & BTN_SEMI_MODE_MASK) == BTN_SEMI_DDRCOPY_STREAMING_MODE) {
				pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DDR_COPY_SEMI_STREAM;
				pl->pl.nvm_cmd_id = pl->semi_ddr_pl.semi_ddr; // use nvme_cmd_id to backup semi-ddr dtag
				pl->semi_ddr_pl.semi_ddr = 0;
			} else {
				pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP; // only for 512B case
			}
		}
#else
		pl->pl.type_ctrl = 0;
#endif

		dtag_meta_w_setup(pl, dat->lda[i], nal_make_pda(cur_spb, cur_spb_pda_wptr));
#ifdef HMB_DTAG
		pl->pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_HMB;
#endif

		cur_spb_pda_wptr++;
		disp_apl_trace(LOG_DEBUG, 0xce25, "Index(%d): lda(0x%x) -> pda(0x%x) Dtag(%d)", i, dat->lda[i], l2p[dat->lda[i]], pl->pl.dtag);
	}

	/* correct dat count due to PDA -> Page */
	dat->count = j;

#if RAWDISK_L2P
	rawdisk_l2p_updt(l2p_otf);
	list_add_tail(&l2p_otf->entry, &l2p_otf_list);
#endif
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
	if (wr_pl_cur == NULL)
		return;

	disp_apl_trace(LOG_INFO, 0xb488, "%s", (const char *) data);

	if (wr_pl_cur->count < pda_cnt_multi_pln) {
		u32 i = 0;
		u32 ofst = wr_pl_cur->count;
		u32 padding_cnt = pda_cnt_multi_pln - ofst;

		for (i = 0; i < padding_cnt; i++) {
			wr_pl_cur->lda[ofst + i] = PADDING_LDA;

			wr_pl_cur->bm_pl[ofst + i].pl.btag = PADDING_BTAG;
			wr_pl_cur->bm_pl[ofst + i].pl.dtag = WVTAG_ID;
		}

		wr_pl_cur->count += padding_cnt;
	}
	if (rawdisk_tlc_mode == false) {
		rawdisk_write_prep(wr_pl_cur, wr_pl_cur->bm_pl);
	} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
		rawdisk_write_prep_for_mu_tlc(wr_pl_cur, wr_pl_cur->bm_pl);
#else
		rawdisk_write_prep_for_tsb_tlc(wr_pl_cur, wr_pl_cur->bm_pl);
#endif
	}
#ifdef HMB_DTAG
	enum ncl_op_type_t op_type = NCL_CMD_HOST_WRITE_HMB_DTAG;
#else
	enum ncl_op_type_t op_type = NCL_CMD_PROGRAM_HOST;
#endif
	rawdisk_write(wr_pl_cur, wr_pl_cur->bm_pl, false, op_type);

	wr_pl_cur = 0;
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
	btn_cmd_t *bcmd;
	lda_t lda;

	if (wr_pl_cur == NULL) {
		wr_pl_cur = pool_get_ex(&ncl_dat_pool);
		if (wr_pl_cur == NULL) {
			bool ret;
			CBF_INS(&bm_pl_buffer, ret, *bm_pl);
			sys_assert(ret == true);
			return;
		}
		wr_pl_cur->count = 0;
	}

	while (!CBF_EMPTY(&bm_pl_buffer)) {
		bm_pl_t bmp;
		CBF_FETCH(&bm_pl_buffer, bmp);
		bcmd = btag2bcmd(bmp.pl.btag);
		lda = LBA_TO_LDA(bcmd_get_slba(bcmd));
		lda += bmp.pl.du_ofst;
		wr_pl_cur->lda[wr_pl_cur->count] = lda;
		wr_pl_cur->bm_pl[wr_pl_cur->count] = bmp;
		wr_pl_cur->count++;

		if (rwr_type == WR_T_CACHE_ON)
			rawdisk_req_wr_cmpl(&bmp, 1);
		else
			mod_timer(&flush_timer, jiffies + 2*HZ/10);
		if (wr_pl_cur->count == pda_cnt_multi_pln) {
			break;
		}
	}

	if (wr_pl_cur->count == pda_cnt_multi_pln) {
		bool ret;
		CBF_INS(&bm_pl_buffer, ret, *bm_pl);
		sys_assert(ret == true);
	} else {
		bcmd = btag2bcmd(bm_pl->pl.btag);
		lda = LBA_TO_LDA(bcmd_get_slba(bcmd));
		lda += bm_pl->pl.du_ofst;
		wr_pl_cur->lda[wr_pl_cur->count] = lda;
		wr_pl_cur->bm_pl[wr_pl_cur->count] = *bm_pl;
		wr_pl_cur->count++;

		if (rwr_type == WR_T_CACHE_ON)
			rawdisk_req_wr_cmpl(bm_pl, 1);
		else
			mod_timer(&flush_timer, jiffies + 2*HZ/10);
	}


	if (wr_pl_cur->count == pda_cnt_multi_pln) {
		/* issue the write */
		if (!rawdisk_tlc_mode) {
			rawdisk_write_prep(wr_pl_cur, wr_pl_cur->bm_pl);
		} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
			rawdisk_write_prep_for_mu_tlc(wr_pl_cur, wr_pl_cur->bm_pl);
#else
			rawdisk_write_prep_for_tsb_tlc(wr_pl_cur, wr_pl_cur->bm_pl);
#endif
		}
#ifdef HMB_DTAG
		enum ncl_op_type_t op_type = NCL_CMD_HOST_WRITE_HMB_DTAG;
#else
		enum ncl_op_type_t op_type = NCL_CMD_PROGRAM_HOST;
#endif
		rawdisk_write(wr_pl_cur, wr_pl_cur->bm_pl, false, op_type);
		/* for the rest if any */
		wr_pl_cur = NULL;
	}
}

fast_code static void rawdisk_resume_write_req(void)
{
	if(!CBF_EMPTY(&bm_pl_buffer)){
		if(wr_pl_cur == NULL){
			wr_pl_cur = pool_get_ex(&ncl_dat_pool);
			if (wr_pl_cur == NULL) {
				return;
			}
			wr_pl_cur->count = 0;
		}
	}
	while(!CBF_EMPTY(&bm_pl_buffer)){
		bm_pl_t bmp;
		CBF_FETCH(&bm_pl_buffer, bmp);
		btn_cmd_t *bcmd = btag2bcmd(bmp.pl.btag);
		lda_t lda = LBA_TO_LDA(bcmd_get_slba(bcmd));
		lda += bmp.pl.du_ofst;
		wr_pl_cur->lda[wr_pl_cur->count] = lda;
		wr_pl_cur->bm_pl[wr_pl_cur->count] = bmp;
		wr_pl_cur->count++;

		if (rwr_type == WR_T_CACHE_ON)
			rawdisk_req_wr_cmpl(&bmp, 1);
		else
			mod_timer(&flush_timer, jiffies + 2*HZ/10);

		if(wr_pl_cur->count == pda_cnt_multi_pln){
			break;
		}
	}

	if (!wr_pl_cur)
		return;

	if(wr_pl_cur->count == pda_cnt_multi_pln){
		/* issue the write */
		if (!rawdisk_tlc_mode) {
			rawdisk_write_prep(wr_pl_cur, wr_pl_cur->bm_pl);
		} else {
	#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
			rawdisk_write_prep_for_mu_tlc(wr_pl_cur, wr_pl_cur->bm_pl);
	#else
			rawdisk_write_prep_for_tsb_tlc(wr_pl_cur, wr_pl_cur->bm_pl);
	#endif
		}
	#ifdef HMB_DTAG
		enum ncl_op_type_t op_type = NCL_CMD_HOST_WRITE_HMB_DTAG;
	#else
		enum ncl_op_type_t op_type = NCL_CMD_PROGRAM_HOST;
	#endif
		rawdisk_write(wr_pl_cur, wr_pl_cur->bm_pl, false, op_type);
		wr_pl_cur = NULL;
	}

}

/*!
 * @brief insert a write data entry into current write payload
 *
 * @param bm_pl		bm payload of write data entry
 *
 * @return		not used
 */
static fast_code void rawdisk_nrm_wd_updt(bm_pl_t *bm_pl)
{
#if defined(HMB_DTAG)
	if (rwr_type == WR_T_CACHE_ON)
		rawdisk_write_hmb(bm_pl, 1);
#else
	_rawdisk_nrm_wd_updt(bm_pl);
#endif
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
#ifdef DEBUG_5372
	u32 i;
	for (i = 0; i < NR_LBA_PER_LDA; i++) {
		u32 j;
		u32 *d;
		u32 *s;
		bool err = false;

		d = ptr_inc(dtag2mem(dst), i * LBA_SIZE);
		if (i >= __off && i < __off + __cnt) {
			s = ptr_inc(dtag2mem(par), i * LBA_SIZE);
		} else {
			continue;
		}

		for (j = 0; j < LBA_SIZE / sizeof(u32); j++) {
			if (s[j] != d[j]) {
				disp_apl_trace(LOG_ERR, 0xeea5, "[%d][%d] src %x dst %x\n", i, j, s[j], d[j]);
				err = true;
			}
		}
		if (err) {
			btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
			lda_t lda = LBA_TO_LDA(bcmd_get_slba(bcmd));

			lda += bm_pl->pl.du_ofst;
			disp_apl_trace(LOG_ERR, 0x6cde, "merge Error lda %x \n", lda);
		}
	}
#endif

#if defined(SEMI_WRITE_ENABLE) &&  defined(DDR)
	bm_pl->pl.type_ctrl = 0;	/// clear semi information
	dtag_put(RAWDISK_DTAG_TYPE, par);
#elif defined(SEMI_WRITE_ENABLE)
	bm_pl->pl.type_ctrl = 0;	/// clear semi information
	bm_free_semi_write_load(&par, 1, 0);
#else
	dtag_put(RAWDISK_DTAG_TYPE, par);
#endif
	bm_pl->pl.dtag = dst.dtag;

#ifdef HMB_DTAG
	rawdisk_write_hmb(bm_pl, 1);
#else
	_rawdisk_nrm_wd_updt(bm_pl);
#endif

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

	dtag_put_ex(dtag);
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

		disp_apl_trace(LOG_DEBUG, 0xa8aa, "%d: merge %d head %d", bm_pl->pl.btag, (u32) slba, cnt);
		sys_assert(cnt < NR_LBA_PER_LDA);
	} else {
		off = 0;
		cnt = get_ua_tail_cnt(slba, bcmd->dw3.b.xfer_lba_num);
		disp_apl_trace(LOG_DEBUG, 0x5552, "%d: merge %d tail %d", bm_pl->pl.btag, (u32) slba, cnt);
		sys_assert(cnt < NR_LBA_PER_LDA);
	}
	//sys_assert(dst.b.in_ddr == 0);
#ifdef DEBUG_5372
	__off = off;
	__cnt = cnt;
#endif
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
	if (ncl_cmd->status != 0)
		panic("read error");
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
    #if NCL_FW_RETRY
	ncl_cmd->retry_step = default_read;
    #endif

	_par_ncl_cmd.bm_pl.all = 0;
	_par_ncl_cmd.bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_NRM;
	if (dtag.b.in_ddr)
		_par_ncl_cmd.bm_pl.pl.type_ctrl |= META_DDR_DTAG;

	_par_ncl_cmd.bm_pl.pl.dtag = dtag.dtag;
	_par_ncl_cmd.pda = pda;

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

next:
	if (par_pl_q.rptr == par_pl_q.wptr)
		return;

	if (par_handling != 0)
		return;

	par_handling = 1;
	bm_pl = &par_pl_q.que[par_pl_q.rptr];

#if defined(SEMI_WRITE_ENABLE) && defined(DDR)
	u32 type = bm_pl->pl.type_ctrl & BTN_SEMI_MODE_MASK;
	if (type == BTN_SEMI_DDRCOPY_STREAMING_MODE) {
		u32 ddtag;
		dtag_t sdtag;
		//disp_apl_trace(LOG_ERR, 0, "partial semi %08x\n", bm_pl->pl.dtag);
		ddtag = DTAG_IN_DDR_MASK | bm_pl->semi_ddr_pl.semi_ddr;
		sdtag.dtag = bm_pl->semi_ddr_pl.semi_sram;
		bm_free_semi_write_load(&sdtag, 1, 0);
		bm_pl->pl.dtag = ddtag;
		bm_pl->pl.type_ctrl &= ~BTN_SEMI_MODE_MASK;
	}
#endif
	bcmd = btag2bcmd(bm_pl->pl.btag);

	lda_in_wb_unmap_cnt = 0;
	lda_in_wb_unmap_bitmap = 0;
	slba = bcmd_get_slba(bcmd);
	lda = LBA_TO_LDA(slba) + bm_pl->pl.du_ofst;

	do {
#ifdef HMB_DTAG
		// check SRAM
		rawdisk_hmb_otf_collision(lda, 1, bm_pl->pl.btag, 0);
		if (lda_in_wb_unmap_cnt)
			break;
#endif
		if (wr_pl_cur)
			rawdisk_collision(lda, 1, bm_pl->pl.btag, 0);
	} while (0);

	dst = dtag_get_urgt(MERGE_DST_TYPE, &mem);
	sys_assert(dst.dtag != _inv_dtag.dtag);

	if (lda_in_wb_unmap_cnt) {
		dtag_t src = { .dtag = wb_unmap_pl[0].pl.dtag };

		do {
#ifdef HMB_DTAG
			if (src.b.type == 1) {
				bm_pl_t pl;
				u16 hmb_off;

				pl.pl.dtag = dst.dtag;
				par_hmb_dtag = dst;
				hmb_off = src.b.dtag;
				par_handling = 2;
				bm_hmb_req(&pl, hmb_off, true);
				merge = false;
				break;
			}
#endif
			//sys_assert(dst.b.in_ddr == 0);
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
#ifdef RAWDISK_L2P
		if (rawdisk_l2p_par_srch(lda) == 0) {
			// no PDA, still searching
			return;
		}

		// found
		pda = par_dat.pda[0];
#else
		pda = l2p[lda];
#endif
		if (pda == INV_PDA) {
			dtag_put(MERGE_DST_TYPE, dst);
			_rawdisk_nrm_wd_updt(bm_pl);
			par_handling = 0;
			par_pl_q.rptr++;
			if (par_pl_q.rptr >= BM_PL_QUE_SZ)
				par_pl_q.rptr = 0;
			goto next;
			//bm_data_fill(mem, DTAG_SZE, 0xFFFFFFFF, NULL, NULL);
			//merge = true;
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
 * @brief partial write data entry handler
 *
 * Receive partial write data entries, need to read full dtag back and merge it
 *
 * @param bm_pl		partial write data entry
 *
 * @return		not used
 */
static fast_code void
rawdisk_par_wd_updt(bm_pl_t *bm_pl)
{
	disp_apl_trace(LOG_DEBUG, 0xaab3, "par in b %d o %d d %d",
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
		btn_cmd_t *bcmd = btag2bcmd(bm_pl->pl.btag);
		btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_pl->pl.btag);

#if defined(SEMI_WRITE_ENABLE)
		if (RAWDISK_SEMI_WRITE_DDR == false) {
			volatile btn_cmd_ex_flag_t *flags = &bcmd_ex->flags;
			while (flags->b.bcmd_init == 0)
				btn_rw_cmd_in();
		}
#endif

		sys_assert(bcmd->dw0.b.nvm_cmd_id == bm_pl->pl.nvm_cmd_id);
		if (bm_pl->pl.type_ctrl <= BTN_NVM_TYPE_CTRL_CMP)
			wd_otf_uptd(bm_pl);

		if (bcmd_ex->flags.b.wr_err) {
			dtag_t dtag = { .dtag = bm_pl->pl.dtag };

			dtag_put_ex(dtag);
			--bcmd_ex->du_xfer_left;
			continue;
		}

		switch (bm_pl->pl.type_ctrl & 0x7) {
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
	reload_free_dtag(NULL);
}

/*!
 * @brief common free queue updated event handler function
 *
 * common free queue for NVMe read transfer or NCL write done, recycle dtags.
 *
 * @param param		not used
 * @param payload	should be dtag list
 * @param count		length of dtag list
 *
 * @return		not used
 */
#ifndef HMB_DTAG
static fast_code void
rawdisk_com_free_updt(u32 param, u32 payload, u32 count)
{
	dtag_put_bulk(DTAG_T_MAX, count, (dtag_t *) payload);
}
#endif
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

static inline void rawdisk_fw_ddat_upt(btn_rd_de_t *de)
{
	sys_assert(dst.dtag = ((bm_pl_t*) de)->pl.dtag);
	par_handling = 5;
	rawdisk_merge(&par_pl_q.que[par_pl_q.rptr], dst);
}

fast_code static void rawdisk_rd_updt(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32*) payload;
	u32 i;

	for (i = 0; i < count; i++) {
		btn_rd_de_t *de = dma_to_btcm(addr[i]);

		if (de->b.type == BTN_NCB_TYPE_CTRL_DYN) {
			rawdisk_hst_rd_updt(de);
		} else if (de->b.type == BTN_NCB_TYPE_CTRL_PREASSIGN) {
			rawdisk_fw_ddat_upt(de);
		} else {
			panic("stop");
		}
	}

}

/*!
 * @brief power state change event handler function
 *
 * @param param		not used
 * @param ps		power status
 * @param count		not used
 *
 * @return		not used
 */
UNUSED static slow_code void
rawdisk_power_state_change(u32 param, u32 ps, u32 count)
{
	if (3 == ps) {
		sys_assert(bcmd_list_empty(&bcmd_pending) == true);
		sys_sleep(SLEEP_MODE_PS3_L12);
	} else if (4 == ps) {
		sys_assert(bcmd_list_empty(&bcmd_pending) == true);
		sys_sleep(SLEEP_MODE_PS4);
	} else {
		sys_sleep_cancel();
	}

	disp_apl_trace(LOG_ALW, 0x30f8, "change power state [%d] complete\n", ps);
	return;
}

static fast_code void rawdisk_collision(lda_t slda, u32 ndu, u32 btag, u32 ofst)
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
			disp_apl_trace(LOG_DEBUG, 0xfc23, "lda(0x%x) is re-written, discard it", *lda);
			continue;
		}

		disp_apl_trace(LOG_DEBUG, 0x159a, "lda(0x%x) is in write buffer", *lda);

		dtag_t dtag;

		dtag.dtag = pl->pl.dtag;
#if defined(SEMI_WRITE_ENABLE)
		if (RAWDISK_SEMI_WRITE_DDR) {
			if ((pl->pl.type_ctrl & BTN_SEMI_MODE_MASK) == BTN_SEMI_DDRCOPY_STREAMING_MODE)
				dtag.dtag = DTAG_IN_DDR_MASK | pl->semi_ddr_pl.semi_ddr;
		}
#endif

#ifdef HMB_DTAG
		dtag_ref_inc(DTAG_T_HMB, dtag);
#else
		dtag_ref_inc_ex(dtag);
#endif

		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.btag = btag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.dtag = dtag.dtag;
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

		disp_apl_trace(LOG_DEBUG, 0x1daa, "lda(0x%x) is in unmapping area", lda);

		btn_cmd_t *bcmd = btag2bcmd(btag);

		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.btag = btag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.dtag = RVTAG_ID;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.nvm_cmd_id  = bcmd->dw0.b.nvm_cmd_id;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.du_ofst = i + ofst;
		set_bit(i, (void *) &lda_in_wb_unmap_bitmap);
		lda_in_wb_unmap_cnt++;
	}
}

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

	disp_apl_trace(LOG_INFO, 0x8891, "rawdisk format: PI %s", (pi_enable ? "en" : "dis"));
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

		for (i = 0; i < wr_pl_cur->count; i++) {
			dtag_t dtag;

			dtag.dtag = wr_pl_cur->bm_pl[i].pl.dtag;
			dtag_put_ex(dtag);
		}
//		sys_free(FAST_DATA, wr_pl_cur);
		pool_put_ex(&ncl_dat_pool, wr_pl_cur);
		wr_pl_cur = NULL;
	}

	// we should set host DU format to PI DU format, but backend not support yet
#if defined(HMETA_SIZE)
	du_format = (HMETA_SIZE > 0) ? DU_FMT_USER_4K_HMETA : DU_4K_DEFAULT_MODE;
#endif
}

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
			dat->ndu = min(bcmd_ex->ndu - bcmd_ex->read_ofst, NR_DU_PER_CMD);
#if defined(HMB_DTAG)
			hmb_dtag_sub_rd_req_exec(btag, dat);
#elif defined(RAWDISK_L2P)
			l2p_sub_rd_req_exec(btag, dat);
#else
			sub_rd_req_exec(btag, dat);
#endif
			bcmd_ex->read_ofst += dat->ndu;
		}
pend:
		if (bcmd_ex->read_ofst != bcmd_ex->ndu)
			bcmd_list_ins(btag, &bcmd_pending);
		break;
	case NVM_WRITE:
		/* reload for Free Write */
#if defined(SEMI_WRITE_ENABLE)
		if (RAWDISK_SEMI_WRITE_DDR) {
#else
		if (1) {
#endif
			// normal path and DDR semi-write
			load_free_dtag(bcmd_ex->ndu);
		} else {
			// SRAM semi-write
		}

		bcmd_ex->du_xfer_left += (short)bcmd_ex->ndu;
		if (bcmd_ex->du_xfer_left == 0 && bcmd_ex->flags.b.wr_err == 0)
			btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
		disp_apl_trace(LOG_DEBUG, 0x666a, "(W) btag(%d) NDU(%d)", btag, bcmd_ex->ndu);
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
	case REQ_T_FORMAT:
		rawdisk_format(req->op_fields.format.meta_enable);
		req->completion(req);
		break;
	case REQ_T_FLUSH:
		req->completion(req);
		break;
	default:
		sys_assert(0);
		break;
	}

	return true;
}

/*** those code for Jtag only ***/
static row_t nda2row(u32 pgn, u8 pln, u16 blk, u8 lun)
{
	return (pgn << nand_row_page_shift()) | (pln << nand_row_plane_shift()) | (blk << nand_row_blk_shift()) | (lun << nand_row_lun_shift());
}

extern void nal_get_first_dev(u32 *ch, u32 *dev);
extern int ncl_access_mr(rda_t *rda_list, enum ncl_cmd_op_t op,
			  bm_pl_t *dtag, u32 count);

/*!
 * @brief search SRB
 *
 * @param mem	should be MR defect table buffer
 * @param sz	size of MR defect table
 *
 * @return	return ture if found
 */
static init_code bool rawdisk_srb_scan(void *mem, u32 sz)
{
	u32 ch, dev;
	u32 pln;
	u32 blkno;
	srb_t *srb;
	bool retval = true;

	nal_get_first_dev(&ch, &dev);

	dtag_t dtag = dtag_get(DTAG_T_SRAM ,(void *)&srb);

	if (dtag.dtag == _inv_dtag.dtag)
		sys_assert(0);

	memset(dtag2mem(dtag), 0, DTAG_SZE);

	bm_pl_t pl = {
		.pl = {
			.dtag = dtag.b.dtag,
			.du_ofst = 0,
			.btag = 0,
			.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
		}
	};

	rda_t srb_hdr_rda = {
		.ch = ch,
		.dev = dev,
		.du_off = 0,
		.pb_type = NAL_PB_TYPE_SLC,
	};

	/* Scan SRB accordingly */
	for (blkno = 0; blkno < SRB_BLKNO; blkno++) {
		for (pln = 0; pln < nand_plane_num(); pln++) {
			srb_hdr_rda.row = nda2row(0, pln, blkno, 0); /* always use LUN0 */
			disp_apl_trace(LOG_ALW, 0x16d3, "SRB hdr scan Row(0x%x)@CH/CE(%d/%d)", srb_hdr_rda.row, srb_hdr_rda.ch, srb_hdr_rda.dev);
			if (ncl_access_mr(&srb_hdr_rda, NCL_CMD_OP_READ, &pl, 1) != 0)
				continue;

			u32 sig_hi = (srb->srb_hdr.srb_signature >> 16) >> 16;
			u32 sig_lo = srb->srb_hdr.srb_signature;

			disp_apl_trace(LOG_ERR, 0xf297, "SRB Signature -> 0x%x%x", sig_hi, sig_lo);

			if (srb->srb_hdr.srb_signature == SRB_SIGNATURE &&
					(srb->srb_hdr.srb_csum == crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum))))
			{
				disp_apl_trace(LOG_ALW, 0x8d31, "SRB hdr founded");

				upgrade_fw.fw_dw_buffer_rda = srb->fwb_buf_pos;
				upgrade_fw.fwb_pri_rda = srb->fwb_pri_pos;
				upgrade_fw.fwb_sec_rda = srb->fwb_sec_pos;
				if (srb_load_fbbt((dft_btmp_t *)mem, sz, srb->dftb_pos, NR_DUS_SLICE(srb->dftb_sz)) == false) {
					disp_apl_trace(LOG_ALW, 0x5079, "Trying mirror fbbt ...");
					if (srb_load_fbbt((dft_btmp_t *)mem, sz, srb->dftb_m_pos, NR_DUS_SLICE(srb->dftb_sz)) == false) {
						disp_apl_trace(LOG_ALW, 0xc476, "SRB fbbt load fail");
						retval = false;
					}
				}
				goto out;
			}
		}
	}

	retval = false;
out:
	dtag_put(DTAG_T_SRAM, dtag);

	return retval;
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
	u32 mem_sz;
	dft_btmp_t *mem;

	// compact defect bitmap
	defect_bitmap_size = spb_total * width_in_dws * sizeof(u32);
	defect_bitmap = sys_malloc(SLOW_DATA, defect_bitmap_size);
	sys_assert(defect_bitmap);
	memset(defect_bitmap, 0, defect_bitmap_size);

	// MR defect bitmap
	mem_sz = spb_total * sizeof(dft_btmp_t);
	mem = sys_malloc(SLOW_DATA, mem_sz);
	sys_assert(mem);

	/* If MR is not in memory, retrieve from media */
	srb_t *srb = (srb_t *) SRAM_BASE;
	//memset(srb, 0, sizeof(srb_t));
	if ((srb->srb_hdr.srb_signature != SRB_SIGNATURE) ||
		(srb->srb_hdr.srb_csum != crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum)))) {
		/* Scan SRB block */
		if (rawdisk_srb_scan(mem, mem_sz) == false) {
			disp_apl_trace(LOG_ERR, 0x6138, "Use Programmer for fbbt build first!\n");
			sys_assert(0);
		}
	} else {
		upgrade_fw.fw_dw_buffer_rda = srb->fwb_buf_pos;
		upgrade_fw.fwb_pri_rda = srb->fwb_pri_pos;
		upgrade_fw.fwb_sec_rda = srb->fwb_sec_pos;
		if (srb_load_fbbt((dft_btmp_t *)mem, mem_sz, srb->dftb_pos, NR_DUS_SLICE(srb->dftb_sz)) == false) {
			disp_apl_trace(LOG_ALW, 0xd1d1, "Trying mirror fbbt ...");
			if (srb_load_fbbt((dft_btmp_t *)mem, mem_sz, srb->dftb_m_pos, NR_DUS_SLICE(srb->dftb_sz)) == false) {
				disp_apl_trace(LOG_ALW, 0xbac5, "Defect in MR read fail 2");
				sys_assert(0);
			}
		}
	}
	disp_apl_trace(LOG_ALW, 0x0ba9, "SRB fbbt loaded");

	fbbt_trans_layout(mem, (u8 *)defect_bitmap, NULL, spb_total, width_in_dws * sizeof(u32));
	/* To accelerate bad block check */

	sys_free(SLOW_DATA, mem);
}

/*!
 * @brief rawdisk suspend function
 *
 * @param mode	sleep mode
 *
 * @return	always true
 */
ps_code bool rawdisk_suspend(enum sleep_mode_t mode)
{
#ifdef HMB_DTAG
	bm_free_hmb_pop();
#else
	u32 i;
	dtag_t dtag;

	/* for debug */
	for (i = 0; i < RAWDISK_STREAMING_MODE_DTAG_CNT; i++) {
		dtag = bm_pop_dtag_llist(FREE_AURL_RD_DTAG_LLIST);

		if (dtag.dtag == _inv_dtag.dtag) {
			//disp_apl_trace(LOG_ERR, 0, "pop %d\n", i);
			break;
		}
		dtag_put(DTAG_T_SRAM, dtag);
	}
	//dtag = bm_pop_dtag_llist(FREE_AURL_RD_DTAG_LLIST);
	//sys_assert(dtag.dtag == _inv_dtag.dtag);
#endif
	return true;
}

/*!
 * @brief rawdisk resume function, push streaming dtag again
 *
 * @param mode	sleep mode
 *
 * @return	not used
 */
ps_code void rawdisk_resume(enum sleep_mode_t mode)
{
#ifdef HMB_DTAG
	dtag_get_bulk(RAWDISK_DTAG_TYPE, RAWDISK_STREAMING_MODE_DTAG_CNT, dtag_res);
	bm_free_hmb_load();
#else
	#if !defined(BTN_STREAM_BUF_ONLY) // strean + auto release
	dtag_t dtag_res[RAWDISK_STREAMING_MODE_DTAG_CNT];
	u32 alloc = dtag_get_bulk(DTAG_T_SRAM, RAWDISK_STREAMING_MODE_DTAG_CNT, dtag_res);

	bm_free_aurl_load(dtag_res, alloc);
	#endif
#endif
}

#if INTL_INS
/*! use to simulate real case */
static void intl_ins_read(struct ncl_cmd_t *ncl_cmd);
static fast_code void intl_ins_write(struct ncl_cmd_t *ncl_cmd)
{
	/* do internal write to intl_cur_spb */
	int i;
	void *mem = dtag2mem(intl_sdtag);

	if (ncl_cmd) {
		u32 spb_id = (u32) ncl_cmd->caller_priv;
		spb_states[spb_id] = SPB_ST_ERASED;
		sys_free(FAST_DATA, ncl_cmd);
	}

	if (intl_cur_spb_pda_wptr >= pda_cnt_in_slc_spb) {
		valid_cnt[intl_cur_spb] = 0;
		intl_cur_spb_pda_wptr = 0;
		intl_cur_spb = nxt_spb;
		valid_cnt[intl_cur_spb] = ~0;
		nxt_spb = rawdisk_find_next_candidate_spb(intl_cur_spb);
		disp_apl_trace(LOG_INFO, 0x30dd, "intl_cur_spb(%d), nxt_spb(%d)", intl_cur_spb, nxt_spb);
		rawdisk_erase(nxt_spb, ((!rawdisk_tlc_mode) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC),
			false, rawdisk_erase_cmpl);
	}

	/* flexible configurable cur_spb */
	if (spb_states[intl_cur_spb] != SPB_ST_ERASED) {
		rawdisk_erase(intl_cur_spb, NAL_PB_TYPE_SLC, false, intl_ins_write);
		return;
	}

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		u32 *dw = ptr_inc(mem, i * NAND_DU_SIZE);

		intl_wr_pl.pda[i] = nal_make_pda(intl_cur_spb, intl_cur_spb_pda_wptr);
		*dw = intl_wr_pl.pda[i];

		intl_wr_pl.info[i].pb_type = NAL_PB_TYPE_SLC;
		intl_wr_pl.info[i].xlc.slc_idx = 0;

		intl_wr_pl.bm_pl[i].all = 0;
		intl_wr_pl.bm_pl[i].pl.dtag = intl_sdtag.dtag + i;
		intl_wr_pl.bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		dtag_meta_w_setup(&intl_wr_pl.bm_pl[i], intl_wr_pl.pda[i], intl_wr_pl.pda[i]);

		intl_cur_spb_pda_wptr++;
	}
#if 0
	l2cache_flush(intl_sdtag.b.dtag << DTAG_SHF, DU_CNT_PER_PAGE << DTAG_SHF);
	l2cache_mem_flush(&ddtag_meta[intl_sdtag.b.dtag], sizeof(meta_t) * DU_CNT_PER_PAGE);
#endif

	intl_ncl_cmd.completion = intl_ins_read;
	intl_ncl_cmd.status = 0;
	intl_ncl_cmd.op_code = NCL_CMD_OP_WRITE;
	intl_ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;

	intl_ncl_cmd.op_type = NCL_CMD_PROGRAM_TABLE;
	intl_ncl_cmd.user_tag_list = intl_wr_pl.bm_pl;

	intl_ncl_cmd.addr_param.common_param.list_len = 1;
	intl_ncl_cmd.addr_param.common_param.pda_list = intl_wr_pl.pda;
	intl_ncl_cmd.addr_param.common_param.info_list = intl_wr_pl.info;

	intl_ncl_cmd.du_format_no = DU_FMT_USER_4K;
	ncl_cmd_submit(&intl_ncl_cmd);
}

static fast_code void intl_ins_read_done(struct ncl_cmd_t *ncl_cmd)
{
	/* compare internal read data*/
	u32 i;
	void *mem = dtag2mem(intl_sdtag);

#if 0
	l2cache_invalidate(intl_sdtag.b.dtag << DTAG_SHF, DU_CNT_PER_PAGE << DTAG_SHF);
	l2cache_mem_invalidate(&ddtag_meta[intl_sdtag.b.dtag], sizeof(meta_t) * DU_CNT_PER_PAGE);
#endif

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		u32 *dw = ptr_inc(mem, i * NAND_DU_SIZE);
		pda_t pda = ncl_cmd->addr_param.common_param.pda_list[i];
		meta_t *meta;

#if defined(DDR)
		meta = ddtag_meta;
#else
		meta = dtag_meta;
#endif
		if (meta[intl_sdtag.b.dtag + i].hlba != (pda * NR_LBA_PER_LDA) ||
				meta[intl_sdtag.b.dtag + i].lda != pda ||
				meta[intl_sdtag.b.dtag + i].sn != 0x12345678 ||
				*dw != pda) {
			disp_apl_trace(LOG_ERR, 0xf826, "pda %x dw %x meta %x %x %x",
					pda, *dw, meta[intl_sdtag.b.dtag + i].hlba,
					meta[intl_sdtag.b.dtag + i].lda,
					meta[intl_sdtag.b.dtag + i].sn);
			panic("Oops");
		}

	}

	intl_ins_write(NULL);
}

static fast_code void intl_ins_read(struct ncl_cmd_t *ncl_cmd)
{
	/* do internal read to intl_cur_spb */
	u32 i;
	void *mem = dtag2mem(intl_sdtag);

#if defined(DDR)
	memset(&ddtag_meta[intl_sdtag.b.dtag], 0xaa, sizeof(meta_t) * DU_CNT_PER_PAGE);
#else
	memset(&dtag_meta[intl_sdtag.b.dtag], 0xaa, sizeof(meta_t) * DU_CNT_PER_PAGE);
#endif
	intl_ncl_cmd.status = 0;
	intl_ncl_cmd.op_code = NCL_CMD_OP_READ;
	intl_ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
	intl_ncl_cmd.op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    #if NCL_FW_RETRY
	intl_ncl_cmd.retry_step = default_read;
    #endif

	intl_ncl_cmd.du_format_no = DU_FMT_USER_4K;
	intl_ncl_cmd.caller_priv = NULL;
	intl_ncl_cmd.completion = intl_ins_read_done;

	intl_cur_spb_pda_wptr -= DU_CNT_PER_PAGE;
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		u32 *dw = ptr_inc(mem, i * NAND_DU_SIZE);

		*dw = 0xdeadbeef; // reset buffer before read
		intl_wr_pl.pda[i] = nal_make_pda(intl_cur_spb, intl_cur_spb_pda_wptr);
		intl_wr_pl.info[i].pb_type = NAL_PB_TYPE_SLC;
		intl_wr_pl.info[i].xlc.slc_idx = 0;

		intl_wr_pl.bm_pl[i].all = 0;
		intl_wr_pl.bm_pl[i].pl.dtag = intl_sdtag.dtag + i;

		dtag_meta_r_setup(&intl_wr_pl.bm_pl[i]);
		intl_wr_pl.bm_pl[i].pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
		intl_cur_spb_pda_wptr++;
	}
#if 0
	l2cache_flush(intl_sdtag.b.dtag << DTAG_SHF, DU_CNT_PER_PAGE << DTAG_SHF);
	l2cache_mem_flush(&ddtag_meta[intl_sdtag.b.dtag], sizeof(meta_t) * DU_CNT_PER_PAGE);
#endif

	intl_ncl_cmd.user_tag_list = intl_wr_pl.bm_pl;
	intl_ncl_cmd.addr_param.common_param.list_len = DU_CNT_PER_PAGE;
	intl_ncl_cmd.addr_param.common_param.pda_list = intl_wr_pl.pda;
	intl_ncl_cmd.addr_param.common_param.info_list = intl_wr_pl.info;
	ncl_cmd_submit(&intl_ncl_cmd);
}
#endif

/*!
 * @brief scan SRB and read srb header to 0x20000000
 *
 * @return	not used
 */
init_code void misc_modules_init_base_on_srb(void)
{
	srb_t *srb = (srb_t *) SRB_HD_ADDR;
	dtag_t srb_dtag;

	srb_dtag = _inv_dtag;
	if (srb->srb_hdr.srb_signature != SRB_SIGNATURE) {
		sys_assert(ncl_enter_mr_mode() != false);
		sys_assert(srb_scan_and_load(&srb_dtag) != false);

		srb = (srb_t *)dtag2mem(srb_dtag);
	}

	fwdl_init(srb);
	//srb_sus_init(srb);

	if (srb_dtag.dtag != _inv_dtag.dtag) {
		memcpy((void *)SRB_HD_ADDR, dtag2mem(srb_dtag), sizeof(srb_t));
		dtag_put(DTAG_T_SRAM ,srb_dtag);
		ncl_leave_mr_mode();
	}
}

slow_code void rawdisk_dtag_ins(u32 param, u32 p1, u32 p2)
{
	dtag_t dtag = dtag_get_urgt(RAWDISK_DTAG_TYPE, NULL);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	set_bit(dtag.b.dtag, dtag_otf_bmp);
	bm_free_wr_load(&dtag, 1);

	_free_otf_wd ++;
	_wr_credit --;
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

#if !defined(BTN_STREAM_BUF_ONLY)
		bm_free_aurl_load(aurl_dtag, RAWDISK_STREAMING_MODE_DTAG_CNT);
#endif

		disp_apl_trace(LOG_ALW, 0xf78d, "WR: otf free %d cred %d", _free_otf_wd, _wr_credit);
		disp_apl_trace(LOG_ALW, 0x9efd, "RD: otf free %d cred %d", _free_otf_rd, _rd_credit);

		// todo consider DDR dtag
		for (i = 0; i < RAWDISK_DTAG_CNT; i++) {
			if (test_bit(i, dtag_otf_bmp)) {
				disp_apl_trace(LOG_ALW, 0x9bdd, "[%d]-> %d alive", cnt, i);
				dtag.dtag = i;
				cnt++;
				sys_assert(0);
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

	for (spb_id = 0; spb_id < spb_total; spb_id++) {
		rawdisk_erase(spb_id, NAL_PB_TYPE_XLC, true, NULL);
		ncl_spb_defect_scan(spb_id, (u32 *) (defect_bitmap + spb_id * width_in_dws));
	}
}

fast_code static void rawdisk_fw_download(u32 p0, u32 _req, u32 not_used)
{
	req_t *req = (req_t *) _req;
	dtag_t *dtags = (dtag_t *) req->req_prp.mem;
	u32 count = req->req_prp.mem_sz;
	fwdl_req_t fwdl_req;
	bool ret;
	fwdl_req.field.download.dtags = dtags;
	fwdl_req.field.download.count = count;

	ret = fwdl_download(&fwdl_req);
	sys_assert(ret == true);

	dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, (dtag_t *)req->req_prp.mem);
	sys_free(SLOW_DATA, req->req_prp.mem);
	sys_free(SLOW_DATA, req->req_prp.prp);
	evt_set_imt(evt_cmd_done, (u32) req, 0);
}

fast_code static void rawdisk_fw_commit(u32 p0 , u32 fs_ca, u32 _req)
{
	u32 fs = fs_ca >> 16;
	u32 ca = fs_ca & 0xFFFF;
	fwdl_req_t req;
	bool ret;

	req.field.commit.ca = ca;
	req.field.commit.slot = fs;

	ret = fwdl_commit(&req);
	sys_assert(ret == true);
	if (req.status != ~0)
		evt_set_imt(evt_cmd_done, _req, req.status);
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
	nvme_ctrl_attr_t ctrl_attr;
	UNUSED u32 alloc;

	width_nominal = nand_plane_num() * nand_lun_num() *
				nal_get_channels() * nal_get_targets();
	sys_assert(width_nominal == nal_get_interleave());
	spb_total = nand_block_num();

	if (width_nominal <= 32)
		width_in_dws = 1;
	else if (width_nominal <= 64)
		width_in_dws = 2;
	else if (width_nominal <= 128)
		width_in_dws = 4;
	else
		width_in_dws = 8;

	/* assuming 3:1 but it depends on NAND finally, slc should be related to #WL */
	pda_cnt_in_xlc_spb = DU_CNT_PER_PAGE * nand_page_num() * width_nominal;
	pda_cnt_in_slc_spb = pda_cnt_in_xlc_spb / nand_bits_per_cell();  /* no u64 __aeabi_uldivmod, so u32 */

	if (rawdisk_tlc_mode) {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
		pda_cnt_multi_pln = DU_CNT_PER_PAGE * nand_plane_num() * 2;
#elif USE_TSB_NAND | USE_HYNX_NAND | USE_SNDK_NAND
		pda_cnt_multi_pln = DU_CNT_PER_PAGE * nand_plane_num() * nand_bits_per_cell();
#if QLC_SUPPORT
		// QLC not support yet
		sys_assert(0);
#endif
#else
		sys_assert(0);
#endif
	} else {
		pda_cnt_multi_pln = DU_CNT_PER_PAGE * nand_plane_num();
	}
	pda_cnt_multi_pln_per_wl = DU_CNT_PER_PAGE * nand_plane_num();
	width_nominal_for_pda = ((u16)width_nominal << DU_CNT_SHIFT);

	par_pl_q.wptr = par_pl_q.rptr = 0;
	sys_assert(pda_cnt_multi_pln <= NR_DU_PER_CMD);

	spb_states = sys_malloc(FAST_DATA, spb_total);
	sys_assert(spb_states != NULL);
#ifdef HMB_DTAG
	evt_register(rawdisk_hmb_dtag_com_free, 0, &evt_com_free_upt);
#else
	evt_register(rawdisk_com_free_updt, 0, &evt_com_free_upt);
#endif
	evt_register(rawdisk_wd_updt, 0, &evt_wd_grp0_nrm_par_upt);
	evt_register(rawdisk_rd_updt, 0, &evt_rd_ent_upt);
	evt_register(wd_err_updt, 0, &evt_wd_err_upt);
	//evt_register(rawdisk_power_state_change, 0, &evt_change_ps);
	evt_register(rawdisk_reset_disk, 0, &evt_reset_disk);
	evt_register(rawdisk_dtag_ins, 0, &evt_dtag_ins);
	evt_register(rawdisk_fw_download, 0, &evt_fw_dwnld);
	evt_register(rawdisk_fw_commit, 0, &evt_fw_commit);
	evt_register(rawdisk_io_req_resume, 0, &evt_io_req_resume);
#ifdef HMB_DTAG
	evt_register(rawdisk_hmb_rd_updt, 0, &evt_hmb_rd_upt);

	par_hmb_dtag = _inv_dtag;
	to_hmb_otf_que.rptr = to_hmb_otf_que.wptr = 0;
	from_hmb_wait_que.rptr = from_hmb_wait_que.wptr = 0;

	bm_free_hmb_load();
#endif

	ncl_set_meta_base(dummy_meta, META_IDX_SRAM_BASE);
	ncl_set_meta_base(dtag_meta, META_DTAG_SRAM_BASE);

#ifdef HMB_DTAG
	ncl_set_meta_base(dummy_meta, META_IDX_HBM_BASE);
	ncl_set_meta_base(dtag_meta, META_DTAG_HMB_BASE);
#endif

#ifdef DDR
	ncl_set_meta_base(dummy_meta, META_IDX_DDR_BASE);
	u32 cnt = occupied_by(sizeof(struct du_meta_fmt) * DDR_DTAG_CNT, DTAG_SZE);
	u32 ddr_meta_off = ddr_dtag_register(cnt);
	ddtag_meta = (struct du_meta_fmt *) ddtag2mem(ddr_meta_off);
	ncl_set_meta_base(ddtag_meta, META_DTAG_DDR_BASE);
#endif

#if !RAWDISK_L2P
	/* prepare unmapping Dtag */
	l2p = sys_malloc(SLOW_DATA, sizeof(pda_t) * RAWDISK_LDA_CNT);
	sys_assert(l2p != NULL);
	memset(l2p, UNMAPPING_PDA, sizeof(pda_t) * RAWDISK_LDA_CNT);
#endif

	// Scan defect table instead of using the result from SRB
	if (enable_rawdisk_defect_scan) {
		rawdisk_defect_scan();
	} else {
		misc_modules_init_base_on_srb();
		ncl_enter_mr_mode();
		rawdisk_fbbt_trans_bbt();
		ncl_leave_mr_mode();
	}

	int i = 0;

	valid_cnt = sys_malloc(SLOW_DATA, sizeof(u32) * spb_total);
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
		disp_apl_trace(LOG_INFO, 0xb54a, "cur_spb(%d), nxt_spb(%d)", cur_spb, nxt_spb);
		if (cur_spb <= 1) {
			disp_apl_trace(LOG_ERR, 0x7374, "No good SPB, check defect table!");
			sys_assert(0);
		}
	}

	INIT_LIST_HEAD(&flush_timer.entry);
	flush_timer.function = rawdisk_idle_flush;
	flush_timer.data = "idle_flush";

#ifdef HMB_DTAG
	alloc = dtag_get_bulk(DTAG_T_SRAM, RAWDISK_STREAMING_MODE_DTAG_CNT, dtag_res);
	sys_assert(alloc == RAWDISK_STREAMING_MODE_DTAG_CNT);
	bm_free_aurl_load(dtag_res, alloc);
	fw_stream_init(dtag_res, RAWDISK_STREAMING_MODE_DTAG_CNT);
#else
	if (rrd_type == RD_T_STREAMING) {
		#if !defined(BTN_STREAM_BUF_ONLY)
		disp_apl_trace(LOG_ALW, 0x0da4, "alloc Streaming Read #Dtags(%d)", RAWDISK_STREAMING_MODE_DTAG_CNT);
		alloc = dtag_get_bulk(DTAG_T_SRAM, RAWDISK_STREAMING_MODE_DTAG_CNT, aurl_dtag);
		sys_assert(alloc == RAWDISK_STREAMING_MODE_DTAG_CNT);
		bm_free_aurl_load(aurl_dtag, RAWDISK_STREAMING_MODE_DTAG_CNT);
		#endif
	}
#endif

#if defined(SEMI_WRITE_ENABLE)
	btn_semi_write_ctrl(RAWDISK_SEMI_WRITE_DDR);
	alloc = dtag_get_bulk(DTAG_T_SRAM, RAWDISK_SEMI_WRITE_DTAG_CNT, semi_dtag);
	sys_assert(alloc == RAWDISK_SEMI_WRITE_DTAG_CNT);
	bm_free_semi_write_load(semi_dtag, RAWDISK_SEMI_WRITE_DTAG_CNT, 0);
	disp_apl_trace(LOG_INFO, 0x8176, "Rawdisk SEMI ddr(%d)", RAWDISK_SEMI_WRITE_DDR);
#endif

	btn_callbacks_t callbacks = {
			.hst_strm_rd_err = rawdisk_bm_read_error,
			.write_err = rawdisk_err_wde_handle
	};
	btn_callback_register(&callbacks);

#if RAWDISK_L2P
#ifndef DDR
	ddr_init();
#endif
	rawdisk_l2p_setup();
#endif
	btn_cmd_hook(bcmd_exec, NULL);


	ctrl_attr.all = 0;
	nvmet_set_ctrlr_attrs(&ctrl_attr);

	nvme_ns_attr_t attr;
	memset((void*)&attr, 0, sizeof(attr));

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

	pmu_register_handler(SUSPEND_COOKIE_FTL, rawdisk_suspend,
				RESUME_COOKIE_FTL, rawdisk_resume);


#if INTL_INS
	/* prepared for internal io resource */
	intl_sdtag = dtag_cont_get(INTL_DTAG_TYPE, DU_CNT_PER_PAGE);
	sys_assert(intl_sdtag.dtag != _inv_dtag.dtag);
	intl_ins_write(NULL);
#endif

	for (i = 0; i < MAX_NCL_DAT_CNT; i++)
		_ncl_data[i].id = i;

	pool_init(&ncl_dat_pool, (void*) &_ncl_data[0], sizeof(_ncl_data),
			sizeof(_ncl_data[0]), MAX_NCL_DAT_CNT);
	CBF_INIT(&bm_pl_buffer);
	u32 sz;
#ifdef DDR
	sz = occupied_by(DDR_DTAG_CNT, 32) * sizeof(u32);
#else
	sz = occupied_by(SRAM_IN_DTAG_CNT, 32) * sizeof(u32);
#endif
	dtag_otf_bmp = sys_malloc(FAST_DATA, sz);
	sys_assert(dtag_otf_bmp);
	memset(dtag_otf_bmp, 0, sz);

	nvmet_restore_feat(NULL);

	disp_apl_trace(LOG_INFO, 0xeb8e, "Rawdisk init done. DTAG T: %d, MERGE %d", RAWDISK_DTAG_TYPE, MERGE_DST_TYPE);
#if 0
	ncl_backend_only_perf_test();
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
	disp_apl_trace(LOG_ERR, 0x9016, "\n# of Dtags in write buffer: %d", wr_pl_cur ? wr_pl_cur->count : 0);
	return 0;
}
static DEFINE_UART_CMD(rawdisk, "rawdisk", "rawdisk", "rawdisk: help misc rawdisk information", 0, 0, rawdisk_main);

slow_code int tlc_mode_console(int argc, char *argv[])
{
	bool old = rawdisk_tlc_mode;

	if (rwr_type == WR_T_CACHE_OFF) {
		disp_apl_trace(LOG_ERR, 0x254a, "\nTLC mode doesn't work for cache off\n");
		return 0;
	}

	if (argc > 1)
		rawdisk_tlc_mode = (bool) atoi(argv[1]);
	else
		rawdisk_tlc_mode = true;

	disp_apl_trace(LOG_ERR, 0x6ead, "\nUsing TLC %d -> %d\n", old, rawdisk_tlc_mode);

	// clean mapping
	if (old != rawdisk_tlc_mode) {
		int i;

		disp_apl_trace(LOG_ERR, 0x792a, "Clean mapping\n");

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
			disp_apl_trace(LOG_INFO, 0x253a, "wr_pl_cur %x, count:%d", wr_pl_cur, wr_pl_cur->count);
			for (i = 0; i < wr_pl_cur->count; i++) {
				dtag_t dtag;

				dtag.dtag = wr_pl_cur->bm_pl[i].pl.dtag;
				dtag_put(RAWDISK_DTAG_TYPE, dtag);
			}
//			sys_free(SLOW_DATA, wr_pl_cur);
			pool_put_ex(&ncl_dat_pool, wr_pl_cur);
			wr_pl_cur = NULL;
		}
		lda_in_wb_unmap_cnt = 0;
		lda_in_wb_unmap_bitmap = 0;
		write_pda_per_il = 0;

		disp_apl_trace(LOG_ERR, 0x3433, "Update write width\n");
		pda_cnt_multi_pln_per_wl = DU_CNT_PER_PAGE * nand_plane_num();
		width_nominal_for_pda = ((u16)width_nominal << DU_CNT_SHIFT);
		if (!rawdisk_tlc_mode) {
			pda_cnt_multi_pln = pda_cnt_multi_pln_per_wl;
		} else {
#if USE_MU_NAND | USE_UNIC_NAND | USE_YMTC_NAND
			pda_cnt_multi_pln = pda_cnt_multi_pln_per_wl * 2;
#elif USE_TSB_NAND | USE_HYNX_NAND | USE_SNDK_NAND
			pda_cnt_multi_pln = pda_cnt_multi_pln_per_wl * nand_bits_per_cell();
#else
			sys_assert(0);
#endif
		}
		sys_assert(pda_cnt_multi_pln <= NR_DU_PER_CMD);
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
			disp_apl_trace(LOG_ERR, 0x95ea, "\nWrite Cache: ENABLED");
		else
			disp_apl_trace(LOG_ERR, 0x774f, "\nWrite Cache: DISABLED");
		return 0;
	}

	set_wr_cache_val = atoi(argv[1]);
	if (set_wr_cache_val == 0)
		rwr_type = WR_T_CACHE_OFF;
	else if (set_wr_cache_val == 1)
		rwr_type = WR_T_CACHE_ON;
	else {
		disp_apl_trace(LOG_ERR, 0x7e54, "\nIncorrect input: %d", set_wr_cache_val);
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
static DEFINE_UART_CMD(set_wr_cache, "set_wr_cache",
	"change write cache to enable/disable",
	"syntax: set_wr_cache n [0=disable, 1=enable]",
		0, 1, set_wr_cache_main);

/*! \endcond */
/*! @} */
