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
/*! \file system_log.c
 * @brief define system log data structure and method
 *
 * \addtogroup log
 * \defgroup system_log
 * \ingroup log
 * @{
 *
 * System log is used to save critical system information of disk. Each module
 * could define some log by itself, and register system log descriptor to
 * system log. System log has flush interface to flush data.
 *
 */
#include "ftlprecomp.h"
#include "system_log.h"
#include "spb_mgr.h"
#include "sync_ncl_helper.h"
#include "ncl_cmd.h"
#include "log.h"
#include "ncl_helper.h"
#include "log_flush.h"
#include "ftl_flash_geo.h"
#include "frb_log.h"
#include "ftl_ns.h"
#include "idx_meta.h"
#include "l2cache.h"

/*! \cond PRIVATE */
#define __FILEID__ syslog
#include "trace.h"
/*! \endcond */

#define MAX_SYS_LOG_PG_CNT	48		///< max system log page count

#define DU_CNT_PER_SYS_FLUSH	32
#define PG_CNT_PER_SYS_FLUSH	du2page(DU_CNT_PER_SYS_FLUSH)

/*! @brief system log descriptor list type */
typedef QSIMPLEQ_HEAD(_sld_list_t, _sys_log_desc_t) sld_list_t;

/*! @brief system log descriptor flush context list type */
typedef QSIMPLEQ_HEAD(_sld_flush_list_t, _sld_flush_t) sld_flush_list_t;

#define SYS_LOG_HEADER_VER	0x1	///< system log header version

/*! @brief definition of system log header */
typedef struct _sys_log_header_t {
	u32 version;		///< version number of system log
	u32 log_page_cnt;	///< total system log page count
	pda_t l2pt[MAX_SYS_LOG_PG_CNT];	///< L2P table of system log
} sys_log_header_t;

#define SYS_LOG_HEADER_PAGE_ID	(0xFF - 1)	///< system log header page id

/*! @brief definition of du 2 meta of system log page */
typedef struct _sys_log_du2_meta_t {
	u32 seed;		///< software seed
	pda_t last_header_pos;	///< last header position
	u32 rsvd[6];
} sys_log_du2_meta_t;

/*! @brief definition of meta of system log page */
typedef struct _sys_log_meta_t {
	log_du0_meta_t meta0;		///< du 0
	log_du1_meta_t meta1;		///< du 1
#if DU_CNT_PER_PAGE == 4
	sys_log_du2_meta_t meta2;	///< du 2
	log_du3_meta_t meta3;		///< du 3
#endif
} sys_log_meta_t;
BUILD_BUG_ON(sizeof(sys_log_meta_t) != PAGE_META_SZ);

/*! @brief definition of system log flush resource */
typedef struct _sys_log_io_res_t {
	pda_t *pda;		///< pointer of PDA list buffer
	bm_pl_t *bm_pl_list;	///< pointer of BM payload buffer
	struct info_param_t *info_list;	///< information list for NCL command
	sys_log_meta_t *meta;		///< pointer of meta list buffer
	struct ncl_cmd_t *ncl_cmd;	///< NCL command resource
	u32 meta_idx;			///< meta index of meta buffer
} sys_log_io_res_t;

#define SYS_LOG_F_FLUSHING		0x01	///< system log flag: flushing
#define SYS_LOG_F_FLUSH_ALL		0x02	///< system log flag: flush all
#define SYS_LOG_F_VIR_BOOT		0x04	///< system log flag: virgin boot
#define SYS_LOG_F_FLUSH_REPEAT		0x08	///< system log flag: repeat to flush all dirty page

/*! @brief definition of system log */
typedef struct _sys_log_t {
	log_t log;			///< basic log context
	sld_list_t desc_list;		///< list of all system log descriptor
	sld_flush_list_t flushing_list;	///< list of flushing system log descriptor flush contexts
	sld_flush_list_t waiting_list;	///< list of waiting system log descriptor flush contexts
	sld_flush_list_t cached_flush_list;	///< list of queued system log descriptor flush contexts
	void *base;			///< base pointer of system log
	sys_log_header_t *header;	///< header pointer of system log

	pda_t header_pos;		///< PDA of last system log header page

	u8 flush_io_cnt;		///< how many system log flushing IOs
	u8 page_after_header;		///< total size of system log in byte
	u8 page_size_shift;		///< NAND page size shift
	u8 flags;			///< SYSTEM_LOG_F_xxx

	u32 total_size;			///< total size of system log

	u64 flush_pg_bmp;		///< queued system log page to be flushed
	u64 repeat_bmp;
	u16 repeat_idx;
	u16 repeat_total;

	dtag_t *dtag;			///< dtag buffer list of system log base pointer

	u32 cache_on;			///< count of sys_log_cache_on called
} sys_log_t;

/*!
 * @brief Initialize system log flush IO resource
 *
 * Allocate PDA/dtag/meta buffer for flushing
 *
 * @param io_res	io_res to be initialized
 *
 * @return		none
 */
//static void sys_log_io_res_init(sys_log_io_res_t *io_res); // Curry

/*!
 * @brief Default callback function to initialize a system log descriptor to all 0xFF
 *
 * @param desc		descriptor to be initialized
 *
 * @return		none
 */
static void sys_log_desc_default(sys_log_desc_t *desc);

/*!
 * @brief Flush all pending system log pages in _sys_log.flush_pg_bmp
 *
 * @return	none
 */
static void sys_log_pages_flush(void);

/*!
 * @brief Setup meta for system log page
 *
 * @param meta		page meta buffer to be setup
 * @param header_pos	PDA of last header page
 * @param flush_id	current flush ID
 * @param page_id	system log page ID
 * @param flush_idx	flush page index in a single flush command
 * @param flush_cnt	flush page count in a single flush command
 * @param backup	backup block
 * @param seed		start seed
 *
 * @return		none
 */
static void sys_log_page_meta_setup(sys_log_meta_t *meta,
		pda_t header_pos, u32 flush_id, u32 page_id, u32 flush_idx,
		u32 flush_cnt, pblk_t backup, u32 seed);

/*!
 * @brief Callback function for system log flush command completion
 *
 * After flush command done, update LUT in system log header buffer, and
 * completed queued flush context.
 *
 * @param ncl_cmd	pointer to completed NCL command
 *
 * @return		none
 */
//static void sys_log_flush_done(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief Complete all queued system log flush context
 *
 * @param ctx_list	system log flush context list of this flush command
 *
 * @return		none
 */
static void sys_log_run_cmpl(sld_flush_list_t *ctx_list);

/*!
 * @brief System log reconstruction with two system log SPB
 *
 * @param pblk		system log block array from FTL root block
 * @param log_recon	log reconstruction parameter
 *
 * @return		Return FTL_ERR_OK, if reconstruct successfully
 *
 * @note
 * 1. decide which SPB is latest
 * 2. decide which block is latest
 * 3. decide which page is latest
 * 4. from the latest programmed page, scan all valid system log page back
 */
//static ftl_err_t sys_log_reconstruction(pblk_t pblk[4],
//		log_recon_t *log_recon);

/*!
 * @brief Callback function to restore an old L2P table from latest page
 *
 * If header page is valid, the L2P table will be restore, otherwise INV_PDA
 * will be returned, and log will be reconstructed from latest block page 0.
 *
 * @param log_recon	log reconstruction parameter
 * @param meta		log meta of latest page
 * @param latest	PDA of latest page
 *
 * @return		Return PDA of header page or INV_PDA
 */
static pda_t sys_log_l2pt_read(log_recon_t *log_recon, log_meta_t *meta,
	pda_t latest);

/*!
 * @brief To flush system log header page
 *
 * @return	none
 */
static void sys_log_flush_flush_header(void);

/*!
 * @brief Callback function of header page programmed done
 *
 * @param ncl_cmd	NCL command to flush system log header page
 *
 * @return		none
 */
static void sys_log_header_flush_done(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief Read all system log page back according LUT
 *
 * Because dtag 4K alignment limitation, there will be data copy in recovery
 *
 * @param spb		return SPB ID in log after reconstruction
 * @param log_recon	log reconstruction parameter
 *
 * @return		none
 */
static void sys_log_recovery(log_recon_t *log_recon);

fast_data static sys_log_header_t _sys_header;
fast_data static sys_log_t _sys_log = {
	LOG_INIT(&_sys_log.log),
	.desc_list = QSIMPLEQ_HEAD_INITIALIZER(_sys_log.desc_list),
	.flushing_list = QSIMPLEQ_HEAD_INITIALIZER(_sys_log.flushing_list),
	.waiting_list = QSIMPLEQ_HEAD_INITIALIZER(_sys_log.waiting_list),
	.cached_flush_list = QSIMPLEQ_HEAD_INITIALIZER(_sys_log.cached_flush_list),
	.base = NULL, .header = NULL,
	.header_pos = INV_PDA,
	.flush_io_cnt = 0, .page_after_header = 0,
	.page_size_shift = 0,
	.flags = 0,
	.total_size = 0,
	.flush_pg_bmp = 0,
	.repeat_bmp = 0,
	.repeat_idx = 0,
	.repeat_total = 0,
	.dtag = NULL,
	.cache_on = 0
};	///< system log entity

fast_data_zi static sys_log_io_res_t _sys_log_io_res[2];	///< system log io resource

init_code void sys_log_init(void)
{
	/* the second half of SPB is for backup */
	log_init(&_sys_log.log, 0, get_slc_page_per_block(), LOG_F_ATOMIC | LOG_F_BACKUP);

	_sys_log.page_size_shift = ctz(NAND_PAGE_SIZE);
	_sys_log.header_pos = INV_PDA;
}

init_code void sys_log_desc_register(sys_log_desc_t *desc, u32 ele_cnt,
		u32 ele_size, sld_default_t init_default)
{
	u32 page_cnt;

	desc->element_cnt = ele_cnt;
	desc->element_size = ele_size;
	desc->total_size = ele_cnt * ele_size;
	desc->total_size = round_up_by_2_power(desc->total_size, 32);

	page_cnt = occupied_by(desc->total_size, NAND_PAGE_SIZE);
	desc->log_page_idx = _sys_log.log.log_page_cnt;
	_sys_log.log.log_page_cnt += page_cnt;
	desc->log_page_cnt = page_cnt;

	_sys_log.total_size += desc->total_size;

	desc->init_default = sys_log_desc_default;
	if (init_default != NULL)
		desc->init_default = init_default;

	QSIMPLEQ_INSERT_TAIL(&_sys_log.desc_list, desc, link);
}

init_code ftl_err_t sys_log_start(pblk_t pblk[4])
{
	u32 i;
	u8 *p;
	u32 size;
	u32 phy_page_cnt;
	dtag_t *phy_dtag_list;
	sys_log_desc_t *desc;
	u32 offset;
	ftl_err_t ret;
	u32 vld_dtag_cnt;

	_sys_log.base = ftl_get_io_buf(_sys_log.total_size);

	_sys_log.header = &_sys_header;

	size = sizeof(dtag_t) * page2du(_sys_log.log.log_page_cnt);
	_sys_log.dtag = sys_malloc(SLOW_DATA, size);// Curry 20210312

	phy_page_cnt = occupied_by(_sys_log.total_size, NAND_PAGE_SIZE);
	/* add dummy page */
	phy_page_cnt++;

	size = sizeof(dtag_t) * page2du(phy_page_cnt);
	phy_dtag_list = sys_malloc(FAST_DATA, size);

	/*
	 * add one page for dummy page
	 * phy_dtag_list is dtag list to describe system log base pointer
	 */
	vld_dtag_cnt = ftl_sram_sz_in_dtag(_sys_log.total_size);
	ftl_sram_to_dtag_list(_sys_log.base, phy_dtag_list, vld_dtag_cnt);

	/* For DTAG simulation */
	for (i = vld_dtag_cnt; i < page2du(phy_page_cnt); i++)
		phy_dtag_list[i].dtag = phy_dtag_list[0].dtag;

	/*
	 * setup system log page dtag list
	 *
	 * two system log pages may share the same phy_dtag_list[]
	 */
	p = (u8*) _sys_log.base;
	offset = 0;
	QSIMPLEQ_FOREACH(desc, &_sys_log.desc_list, link) {
		u32 idx = offset >> (_sys_log.page_size_shift - DU_CNT_SHIFT);

		desc->ptr = (void*) (p);

		memcpy(&_sys_log.dtag[page2du(desc->log_page_idx)],
				&phy_dtag_list[idx],
				page2du(desc->log_page_cnt) * sizeof(dtag_t));

		offset += desc->total_size;
		p += desc->total_size;
	}

	sys_free(FAST_DATA, phy_dtag_list);
	/* allocate flush resource */
	//sys_log_io_res_init(&_sys_log_io_res[0]);
	//sys_log_io_res_init(&_sys_log_io_res[1]);

	// todo: determine virgin
	if (1)//(pblk[0].spb_id == INV_SPB_ID && pblk[1].spb_id == INV_SPB_ID)
	{
		u8 log_page_cnt = _sys_log.log.log_page_cnt;

		/* must both be invalid SPB, initialize to default */
		QSIMPLEQ_FOREACH(desc, &_sys_log.desc_list, link) {
			desc->init_default(desc);
		}

		_sys_log.header->log_page_cnt = log_page_cnt;
		_sys_log.header->version = SYS_LOG_HEADER_VER;
		memset(_sys_log.header->l2pt, 0xFF,
				sizeof(_sys_log.header->l2pt));

		/* queue a flush all for virgin boot */
		_sys_log.flush_pg_bmp = (1ULL << log_page_cnt) - 1;
		_sys_log.flags |= SYS_LOG_F_VIR_BOOT;
		//blk_pool_get_pblk(&_sys_log.log.cur[0]); // Curry
		//blk_pool_get_pblk(&_sys_log.log.cur[1]);
		_sys_log.log.next_page = 0;

		ret = FTL_ERR_VIRGIN;
	}
	else {
		log_recon_t log_recon;
		u32 i;

		log_recon_init(&log_recon, _sys_log.header->l2pt,
				sys_log_l2pt_read, SYSTEM_LOG_SIG);

		for (i = 0; i < 4; i++) {
			if (pblk[i].spb_id == INV_SPB_ID)
				continue;

			log_recon.pblk[log_recon.pblk_cnt++] = pblk[i];
		}

		/* reconstruction for latest LUT */
		//ret = sys_log_reconstruction(pblk, &log_recon);

		/* according LUT to read all system log page back */
		if (ret == FTL_ERR_OK) {
			sys_log_recovery(&log_recon);
		} else {
			// error handling
		}

		log_recon_rel(&log_recon);

		sys_log_dump();
	}

	return ret;
}

fast_code void sys_log_resume(void)
{
	log_recon_t log_recon;

	log_recon_init(&log_recon, _sys_log.header->l2pt,
			NULL, SYSTEM_LOG_SIG);

	/* according LUT to read all system log page back */
	sys_log_recovery(&log_recon);

	log_recon_rel(&log_recon);

	sys_log_dump();
}

fast_code void sys_log_desc_flush(sys_log_desc_t *desc, u32 ele,
	sld_flush_t *ctx)
{
	u64 bitmap;

	if (ele == ~0) {
		/* queue all system log pages of this descriptor */
		if (desc)
			bitmap = ((1ULL << desc->log_page_cnt) - 1) << desc->log_page_idx;
		else
			bitmap = (1ULL<< _sys_log.log.log_page_cnt) - 1;
	} else {
		u32 offset = ele * desc->element_size;
		u32 page;

		page = offset >> _sys_log.page_size_shift;
		bitmap = 1ULL << (page + desc->log_page_idx);

		offset += desc->element_size - 1;
		page = offset >> _sys_log.page_size_shift;

		/* only flush changed system log pages */
		bitmap |= 1ULL << (page + desc->log_page_idx);
	}

	if (ctx) {
		QSIMPLEQ_INSERT_TAIL(&_sys_log.cached_flush_list, ctx, link);
	}
	_sys_log.flush_pg_bmp |= bitmap;

	/* if cache on, wait for cache off */
	if (_sys_log.cache_on || (_sys_log.flags & SYS_LOG_F_FLUSHING))
		return;

	sys_log_pages_flush();
}

fast_code void sys_log_cache_on()
{
	_sys_log.cache_on++;
}

fast_code void sys_log_cache_off(void)
{
	_sys_log.cache_on--;

	if (_sys_log.cache_on == 0 && _sys_log.flush_pg_bmp
		&& !(_sys_log.flags & SYS_LOG_F_FLUSHING)) {
		sys_log_pages_flush();
	}
}

fast_code void sys_log_flush_wait(sld_flush_t *flush)
{
	QSIMPLEQ_INSERT_TAIL(&_sys_log.waiting_list, flush, link);
}

fast_code bool is_sys_log_flushing(void)
{
	return (_sys_log.flags & SYS_LOG_F_FLUSHING) ? true : false;
}

init_code void recon_flush_done(sld_flush_t *sld_flush)
{
//	task_ev_wake(sld_flush);
}

init_code void sys_log_critical_chk(void)
{
/*	sld_flush_t sld_flush;

	if ((_sys_log.log.curr != INV_SPB_ID) &&
			(_sys_log_appl.flags & SPB_APPL_F_TRIGGERED)) {

		ftl_syslog_trace(LOG_ALW, 0, "system log critical gc");

		_sys_log.flush_pg_bmp = (1 << _sys_log.log.log_page_cnt) - 1;
		_sys_log.log.cur_blk = _sys_log.log.blk_cnt;
		_sys_log.log.next_page = _sys_log.log.nr_page_per_blk;

		sys_log_pages_flush();

		sld_flush.caller = NULL;
		sld_flush.cmpl = recon_flush_done;
		sys_log_flush_wait(&sld_flush);

		recon_task_ev_block(&sld_flush);
	}*/
}

fast_code void sys_log_dump(void)
{
	ftl_syslog_trace(LOG_ALW, 0x6992, "system log, %x(%x), next %x(%x), next page %d",
			_sys_log.log.cur[0].pblk, _sys_log.log.cur[1].pblk,
			_sys_log.log.next[0].pblk, _sys_log.log.next[1].pblk,
			_sys_log.log.next_page);
}

init_code pda_t sys_log_l2pt_read(log_recon_t *log_recon, log_meta_t *meta,
	pda_t latest)
{
#if DU_CNT_PER_PAGE == 4
	sys_log_meta_t *sys_log_meta = (sys_log_meta_t*) meta;
	pda_t header = sys_log_meta->meta2.last_header_pos;
	u32 l2pt_size;

	l2pt_size = sizeof(pda_t) * _sys_log.log.log_page_cnt;

	if (header != INV_PDA) {
		nal_status_t ret;

		ret = ncl_read_one_page(header, log_recon->data, log_recon->meta_idx);

		if (ficu_du_data_good(ret)) {
			sys_log_header_t *log_header;

			log_header = (sys_log_header_t*) log_recon->data;
			memcpy(log_recon->l2pt, log_header->l2pt, l2pt_size);
			_sys_log.header_pos = header;
		}
	} else {
		memset(log_recon->l2pt, 0xff, l2pt_size);
	}

	return header;
#else
	return INV_PDA;
#endif
}
/*
init_code ftl_err_t sys_log_reconstruction(pblk_t pblk[4],
		log_recon_t *log_recon)
{
	bool ret;

	log_blk_decide(&_sys_log.log, log_recon);

	ret = log_reconstruction(&_sys_log.log, log_recon);

	sys_assert(ret == true);

	return FTL_ERR_OK;
}
*/
fast_code void sys_log_recovery(log_recon_t *log_recon)
{
	u32 i;
	sys_log_desc_t *desc;
	pda_t *l2pt = log_recon->l2pt;
	void *data;
	u8 *p;
	u32 next_log_page_skip = 0;

	data = log_recon->data;
	p = (u8*) _sys_log.base;

	QSIMPLEQ_FOREACH(desc, &_sys_log.desc_list, link) {
		u32 total_size = desc->total_size;

		desc->ptr = (void*) p;

		for (i = 0; i < desc->log_page_cnt; i++) {
			u32 copy_size;
			void *addr;
			nal_status_t ret;
			u32 meta_idx;
			pda_t pda;

			copy_size = NAND_PAGE_SIZE - next_log_page_skip;
			copy_size = min(total_size, copy_size);

			addr = (copy_size < NAND_PAGE_SIZE) ? data : p;

			if ((u32)addr >= DDR_BASE)
				meta_idx = ddr_dummy_meta_idx;
			else
				meta_idx = log_recon->meta_idx;


			pda = l2pt[desc->log_page_idx + i];

			ret = ncl_read_one_page(pda, addr, meta_idx);

			if (ret == ficu_err_du_uc) {
				// try backup
			}

			if (copy_size < NAND_PAGE_SIZE) {
				memcpy(p, ((u8*) addr) + next_log_page_skip,
					copy_size);
			}
			p += copy_size;
			total_size -= copy_size;
			next_log_page_skip += copy_size;
			next_log_page_skip &= NAND_DU_SIZE - 1;
		}

		sys_assert(total_size == 0);
	}
}

/*
init_code void sys_log_io_res_init(sys_log_io_res_t *io_res)
{
	io_res->bm_pl_list = share_malloc(sizeof(bm_pl_t) * DU_CNT_PER_SYS_FLUSH);

	io_res->pda = share_malloc(sizeof(pda_t) * PG_CNT_PER_SYS_FLUSH);

	io_res->info_list = share_malloc(sizeof(struct info_param_t) * DU_CNT_PER_SYS_FLUSH);

	io_res->meta = idx_meta_allocate(DU_CNT_PER_SYS_FLUSH, DDR_IDX_META, &io_res->meta_idx);
	io_res->ncl_cmd = share_malloc(sizeof(struct ncl_cmd_t));
	memset(io_res->info_list, 0, sizeof(struct info_param_t) * DU_CNT_PER_SYS_FLUSH);
}
*/
init_code void sys_log_desc_default(sys_log_desc_t *desc)
{
	memset(desc->ptr, 0xFF, desc->total_size);
}
/*
fast_code void sys_log_cont_flush(void)
{
	u64 bitmap = _sys_log.repeat_bmp;
	u32 flush_idx = _sys_log.repeat_idx;
	u32 flush_cnt = _sys_log.repeat_total;
	u32 cnt = 0;
	u32 i;

	while (bitmap && (cnt < PG_CNT_PER_SYS_FLUSH)) {
		u32 index = ctz64(bitmap);
		bm_pl_t *bm_pl0;
		bm_pl_t *bm_pl1;
		u32 meta_idx0;
		u32 meta_idx1;

		bitmap &= ~(1ULL << index);

		ftl_syslog_trace(LOG_DEBUG, 0, "[%d/%d]%x %x, f %x",
				cnt + flush_idx, flush_cnt,
				_sys_log_io_res[0].pda[cnt],
				_sys_log_io_res[1].pda[cnt],
				_sys_log.log.flush_id - 1);

		bm_pl0 = &_sys_log_io_res[0].bm_pl_list[page2du(cnt)];
		bm_pl1 = &_sys_log_io_res[1].bm_pl_list[page2du(cnt)];

		meta_idx0 = page2du(cnt) + _sys_log_io_res[0].meta_idx;
		meta_idx1 = page2du(cnt) + _sys_log_io_res[1].meta_idx;
		for (i = 0; i < DU_CNT_PER_PAGE; i++) {
			u32 dtag_id;

			dtag_id = _sys_log.dtag[page2du(index) + i].dtag;
			ftl_wr_bm_pl_setup(bm_pl0, dtag_id);
			ftl_prea_idx_meta_setup(bm_pl0, i);
			bm_pl0->pl.du_ofst = page2du(cnt) + i;

			bm_pl1->all = bm_pl0->all;

			bm_pl0->pl.nvm_cmd_id += meta_idx0;
			bm_pl1->pl.nvm_cmd_id += meta_idx1;

			bm_pl0++;
			bm_pl1++;
		}

		// meta setup
		sys_log_page_meta_setup(&_sys_log_io_res[0].meta[cnt],
			_sys_log.header_pos, _sys_log.log.flush_id - 1, index,
			cnt + flush_idx, flush_cnt, _sys_log.log.cur[1],
			_sys_log_io_res[0].pda[cnt]);

		_sys_log_io_res[1].meta[cnt] =
			_sys_log_io_res[0].meta[cnt];


#if defined(ENABLE_L2CACHE)
		u64 addr = _sys_log.dtag[page2du(index)].b.dtag << DTAG_SHF;
		l2cache_flush(addr, DTAG_SZE * DU_CNT_PER_PAGE);
		l2cache_mem_flush((void *) &_sys_log_io_res[0].meta[cnt], sizeof(sys_log_meta_t));
		l2cache_mem_flush((void *) &_sys_log_io_res[1].meta[cnt], sizeof(sys_log_meta_t));
#endif
		cnt++;
	}
	log_page_allocate(&_sys_log.log, _sys_log_io_res[0].pda,
			_sys_log_io_res[1].pda, cnt);

	sys_assert(_sys_log.flush_io_cnt == 0);
	if (bitmap == 0) {
		sys_assert(cnt + flush_idx == flush_cnt);
		_sys_log.repeat_bmp = 0;
	} else {
		_sys_log.repeat_bmp = bitmap;
	}
	_sys_log.repeat_idx = cnt + flush_idx;

	i = 0;
	do {
		u32 flags;
		struct ncl_cmd_t *ncl_cmd;

		ncl_cmd = _sys_log_io_res[i].ncl_cmd;

		flags = NCL_CMD_SLC_PB_TYPE_FLAG;

		ncl_cmd_setup_addr_common(&ncl_cmd->addr_param.common_param,
			_sys_log_io_res[i].pda, cnt,
			_sys_log_io_res[i].info_list);

		ncl_cmd_prog_setup_helper(ncl_cmd,
				NCL_CMD_PROGRAM_TABLE,
				flags,
				_sys_log_io_res[i].bm_pl_list,
				sys_log_flush_done,
				&_sys_log_io_res[i]);

		_sys_log.flush_io_cnt++;

		ncl_cmd_submit(ncl_cmd);
	} while (++i < 2);
}
*/
fast_code void sys_log_pages_flush(void)
{
	u64 bitmap;
	u32 flush_cnt;

	sys_assert((_sys_log.flags & SYS_LOG_F_FLUSHING) == 0);

	sys_assert(_sys_log.log.cur[0].spb_id != INV_SPB_ID);

	if (_sys_log.log.flags & LOG_F_FLUSH_ALL) {
		_sys_log.log.flags &= ~LOG_F_FLUSH_ALL;
		_sys_log.flush_pg_bmp = (1ULL << _sys_log.log.log_page_cnt) - 1;
	}

	flush_cnt = pop64(_sys_log.flush_pg_bmp);
	flush_cnt = log_block_switch_check(&_sys_log.log, flush_cnt);

	if (flush_cnt == 0) {
		ftl_syslog_trace(LOG_INFO, 0x7e9d, "system log pend");
		return;
	} else if (flush_cnt == _sys_log.log.log_page_cnt) {
		_sys_log.flags |= SYS_LOG_F_FLUSH_ALL;
		bitmap = (1ULL << _sys_log.log.log_page_cnt) - 1;
		if (_sys_log.log.flags & LOG_F_SWITCHED) {
			_sys_log.header_pos = INV_PDA;
			_sys_log.page_after_header = 0;
		}
	} else {
		bitmap = _sys_log.flush_pg_bmp;
	}

	_sys_log.flush_pg_bmp = 0;

	_sys_log.repeat_bmp = bitmap;
	_sys_log.repeat_idx = 0;
	_sys_log.repeat_total = flush_cnt;


	/* NCL submit may complete previous command, and trigger sys log flush
	 * if log flush is cache flush, it will set bmp and pend to cache flush list because flushing flag set.
	 * clear flush bmp before new flush set, to avoid new cache flush miss processing */

	QSIMPLEQ_MOVE(&_sys_log.flushing_list, &_sys_log.cached_flush_list);
	_sys_log.flags |= SYS_LOG_F_FLUSHING;
	_sys_log.log.flush_id++; // ++ first, sys_log_cont_flush will use flush_id - 1
	//sys_log_cont_flush();
	if (_sys_log.repeat_bmp != 0)
		_sys_log.flags |= SYS_LOG_F_FLUSH_REPEAT;
}

inline void sys_log_page_meta_setup(sys_log_meta_t *meta,
	pda_t header_pos, u32 flush_id, u32 page_id, u32 flush_idx,
	u32 flush_cnt, pblk_t backup, u32 seed)
{
	log_meta_setup((log_meta_t*)meta, SYSTEM_LOG_SIG, flush_id, backup,
		flush_idx, flush_cnt, page_id, seed);
#if DU_CNT_PER_PAGE == 4
	meta->meta2.last_header_pos = header_pos;
#endif
}
/*
fast_code void sys_log_flush_done(struct ncl_cmd_t *ncl_cmd)
{
	sys_log_io_res_t *io_res;
	u32 cnt;
	u32 i;

	if (ncl_cmd->status != 0) {
		// todo: error handling
		ftl_log_trace(LOG_ERR, 0, "sys log flush err %x, %d",
				ncl_cmd->addr_param.common_param.pda_list[0],
				ncl_cmd->addr_param.common_param.list_len);
	}

	sys_assert(_sys_log.flush_io_cnt);
	_sys_log.flush_io_cnt--;
	io_res = (sys_log_io_res_t*) ncl_cmd->caller_priv;

	if ((io_res - &_sys_log_io_res[0]) == 0) {
		cnt = ncl_cmd->addr_param.common_param.list_len;

		_sys_log.page_after_header += cnt;
		for (i = 0; i < cnt; i++) {
			u8 page_id = io_res->meta[i].meta1.log_page_id;

			// update map
			sys_assert(page_id < MAX_SYS_LOG_PG_CNT);
			_sys_log.header->l2pt[page_id] = io_res->pda[i];
		}
	}

	if (_sys_log.flush_io_cnt == 0) {
		//there're still dirty page left
		if (_sys_log.flags & SYS_LOG_F_FLUSH_REPEAT) {
			if (_sys_log.repeat_bmp == 0) {
				sys_assert(_sys_log.repeat_idx == _sys_log.repeat_total);
				_sys_log.flags &= ~SYS_LOG_F_FLUSH_REPEAT;
			} else {
				sys_log_cont_flush();
				return;
			}
		}

		// normal and backup command were both completed
		sys_log_run_cmpl(&_sys_log.flushing_list);

		_sys_log.flags &= ~(SYS_LOG_F_FLUSHING | SYS_LOG_F_FLUSH_ALL);

		log_flush_done(&_sys_log.log);
#if DU_CNT_PER_PAGE == 4
		sys_log_flush_flush_header();
#endif

		sys_log_cache_on();
		sys_log_run_cmpl(&_sys_log.waiting_list);
		sys_log_cache_off();
	}
}
*/
fast_code __attribute__((unused))
void sys_log_flush_flush_header(void)
{
	u32 thr = _sys_log.log.nr_page_per_blk >> 1;	// temp solution
	dtag_t dtag;
	void *mem;
	u32 i;
	u32 log_page_cnt;
	sys_log_header_t *header;

	if (_sys_log.page_after_header < thr ||
		_sys_log.log.next_page == _sys_log.log.nr_page_per_blk) {
		return;
	}
	_sys_log.page_after_header = 0;
	_sys_log.flags |= SYS_LOG_F_FLUSHING;

	log_page_cnt = _sys_log.log.log_page_cnt;
	dtag = dtag_get_urgt(DTAG_T_SRAM, &mem);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
	header = (sys_log_header_t*) mem;
	memcpy(header, _sys_log.header, sizeof(sys_log_header_t));

	log_page_allocate(&_sys_log.log, _sys_log_io_res[0].pda,
				_sys_log_io_res[1].pda, 1);

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		ftl_wr_bm_pl_setup(&_sys_log_io_res[0].bm_pl_list[i], dtag.dtag);
		ftl_wr_bm_pl_setup(&_sys_log_io_res[1].bm_pl_list[i], dtag.dtag);
	}

	sys_log_page_meta_setup(&_sys_log_io_res[0].meta[0],
		_sys_log_io_res[0].pda[0], _sys_log.log.flush_id,
		log_page_cnt, 0, 1, _sys_log.log.cur[1], _sys_log_io_res[0].pda[0]);

	_sys_log_io_res[1].meta[0] = _sys_log_io_res[0].meta[0];

#ifdef MONITER_WRITE
    extern bool update_stat_wr(pda_t *pda, u32 count, u8 type, u8 tag);
#endif
	i = 0;
	sys_assert(_sys_log.flush_io_cnt == 0);
	do {
		struct ncl_cmd_t *ncl_cmd = _sys_log_io_res[i].ncl_cmd;
		u32 flags;

		ncl_cmd_setup_addr_common(&ncl_cmd->addr_param.common_param,
			_sys_log_io_res[i].pda, 1,
			_sys_log_io_res[i].info_list);

		flags = NCL_CMD_SLC_PB_TYPE_FLAG;

		ncl_cmd_prog_setup_helper(ncl_cmd, NCL_CMD_PROGRAM_TABLE,
				flags,
				_sys_log_io_res[i].bm_pl_list,
				sys_log_header_flush_done,
				&_sys_log_io_res[i]);

		_sys_log.flush_io_cnt++;

#ifdef MONITER_WRITE
        if (update_stat_wr(_sys_log_io_res[i].pda, 1, WR_TYPE_SYS_LOG, 5))
            ncl_cmd->flags |= NCL_CMD_OPEN_BLK_PROG_END_FLAG;
#endif
		ncl_cmd_submit(ncl_cmd);
	} while (++i < 2);

	ftl_log_trace(LOG_DEBUG, 0x18ca, "sys log flush header at %x",
		_sys_log_io_res[0].pda[0]);
}

fast_code void sys_log_header_flush_done(struct ncl_cmd_t *ncl_cmd)
{
	sys_log_io_res_t *io_res;

	sys_assert(_sys_log.flush_io_cnt);
	_sys_log.flush_io_cnt--;
	io_res = (sys_log_io_res_t*) ncl_cmd->caller_priv;
	if ((io_res - &_sys_log_io_res[0]) == 0)
		_sys_log.header_pos = io_res->pda[0];

	/* normal and backup command were both completed */
	if (_sys_log.flush_io_cnt == 0) {
		dtag_t dtag;

		dtag.dtag = io_res->bm_pl_list[0].pl.dtag;
		dtag_put(DTAG_T_SRAM, dtag);

		_sys_log.flags &= ~(SYS_LOG_F_FLUSHING);
		log_flush_done(&_sys_log.log);

		sys_log_cache_on();
		sys_log_run_cmpl(&_sys_log.waiting_list);
		sys_log_cache_off();
	}
}

fast_code void sys_log_run_cmpl(sld_flush_list_t *ctx_list)
{
	sld_flush_list_t local;

	QSIMPLEQ_INIT(&local);
	QSIMPLEQ_MOVE(&local, ctx_list);
	while (!QSIMPLEQ_EMPTY(&local)) {
		sld_flush_t *ctx = QSIMPLEQ_FIRST(&local);

		QSIMPLEQ_REMOVE_HEAD(&local, link);
		ctx->cmpl(ctx);
	}
}

/*! @} */
