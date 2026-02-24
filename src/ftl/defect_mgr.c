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
/*! \file defect_mgr.c
 * @brief define defect table and how to load and usage
 *
 * \addtogroup defect_mgr
 *
 * @{
 * initialize defect table from MR or scan by FTL. Provide the interface for FTL
 * to get defect bitmap and count for a specific SPB.
 */
#include "ftlprecomp.h"
#include "sync_ncl_helper.h"
#include "ncl_exports.h"
#include "spb_log.h"
#include "system_log.h"
#include "spb_info.h"
#include "bitops.h"
#include "dma.h"
#include "ncl_exports.h"
#include "frb_log.h"
#include "srb.h"
#include "ftl_flash_geo.h"
#include "defect_mgr.h"
#include "console.h"
#include "ftl.h"
#include "srb.h"
#include "ipc_api.h"
#include "blk_pool.h"
#include "mpc.h"
#include "nvme_spec.h"
#ifdef SKIP_MODE 
#include "spb_mgr.h"
#include "spb_pool.h"
#endif
#if(degrade_mode == ENABLE)
#include "cmd_proc.h"
#endif
#ifdef TCG_NAND_BACKUP
#include "../tcg/tcg_nf_mid.h"
extern tcg_mid_info_t* tcg_mid_info;
#endif

/*! \cond PRIVATE */
#define __FILEID__ dfm
#include "trace.h"
/*! \endcond */

//#define REMAP_DUMP
#if defined(REMAP_DUMP)
#define remap_print(...)	printk(__VA_ARGS__)
#else
#define remap_print(...)
#endif

#define FTL_DEFECT_VERSION	0x1002	///< 2-byte version
#define PBLK_ROOT_IDX		(ftl_df_map->pblk_cnt - 1)	///< dedicate use last block in pool as root block
/* DEFECT TABLE VERSION HISTORY
 * 0 -> virgin design
 */

#define FTL_REMAP_SIG	'pmrf'		///< ftl remap table signature

/*! @brief define ftl defect block adjust data structure */
typedef struct def_adj_t {
	u32 signature;		///< remap table signature
	u32 rmp_count;		///< remap block count
	spb_id_t remap[1];	///< remap table
} def_adj_t;

#define FTL_DF_SIG	'Dltf'		///< FTL defect signature

/*! @brief define ftl defect map data structure, saved in nand */
typedef struct _ftl_defect_map_t {
	u32 signature;		///< FTL_DF_SIG
	u16 interleave;		///< interleave when first initialization
	u16 version;		///< defect table version
	u16 spb_cnt;		///< SPB count when first initialization
	u16 pblk_cnt;		///< physical block count
	u32 total_size;		///< total byte size of defect map
	union {
		struct {
			u32 mp_prune : 1;	///< enable multi-plane prune
			u32 remap : 1;		///< enable remap
			u32 pblk : 1;		///< if enable pblk
		} b;
		u32 all;
	} flags;

	phy_blk_t pblk[FTL_PBLK_MAX];		///< physical block table

	u8 defect_map[1];	///< defect bitmap
} ftl_defect_map_t;

#define GRWN_ERR_CNT		16 ///< number of grown defect error info

/*! @brief define grown defect error info structure, not ready yet */
typedef struct _grwn_info_t {
	QTAILQ_ENTRY(_grwn_info_t) link;
	u8 type;			///< error type
	pda_t pda;			///< error pda
} grwn_info_t;

typedef QTAILQ_HEAD(_grwn_err_info_list, _grwn_info_t) grwn_err_info_list_t;

fast_data_zi ftl_defect_map_t *ftl_df_map = NULL;		///< fake defect map for ftl
fast_data_zi u32 df_map_size;				///< defect bitmap size
fast_data_zi u32 df_width;					///< defect bitmap width of one SPB
share_data_zi ftl_remap_t ftl_remap;			///< initialized in defect_mgr.c
fast_data_zi def_adj_t *def_adj = NULL;			///< defect adjust
fast_data_zi u32 def_adj_size;				///< defect adjust table size
fast_data_zi grwn_err_info_list_t grwn_err_info_list;	///< grown defect error info list
fast_data_zi pool_t grwn_err_pool;				///< grown defect pool
fast_data_zi grwn_info_t grwn_base[GRWN_ERR_CNT];		///< grown defect error info base
extern ftl_stat_t ftl_stat;
share_data_zi volatile ftl_flags_t shr_ftl_flags;
extern u16 min_good_pl;

/*!
 * @brief return defect map from ftl defect map
 *
 * @return	return block defect bitmap
 */


/*!
 * @brief helper function to scan defect list in FTL, only called when SRB is missing
 *
 * @return	not used
 */
static void ftl_scan_defect(void);

/*!
 * @brief copy bytes list to dword list
 *
 * @param b		bytes list
 * @param dw		dwords list
 * @param byte_cnt	byte count
 *
 * @return		dword count after copied
 */
static u32 byte2dword(u8 *b, u32 *dw, u32 byte_cnt);

/*!
 * @brief helper function to load defect from MR
 *
 * @return		not used
 */
static void load_mr_defect(void);

/*!
 * @brief add a physical block into blk pool
 *
 * @param spb_id	spb_id of this block
 * @param iid		intreleave if of this block
 *
 * @return		not used
 */
static void ftl_add_pblk(u32 spb_id, u32 iid);

/*!
 * @brief Prune valid block if its brother block was defect to keep MP aligned
 *
 * @return		not used
 */
static void ftl_defect_prune_for_mp(void);

/*!
 * @brief scan useless physical block and add them into blk pool
 *
 * @param gblk_thr	good blocks threshold of spb, SPB whos valid count smaller then this will be added
 *
 * @return		not used
 */
static void ftl_defect_scan_pblk(u32 gblk_thr);

/*!
 * @brief build remap table
 *
 * @return		not used
 */
static void ftl_build_remap(void);

/*!
 * @brief check remap and rebuild remap_idx
 *
 * @return		not used
 */
static void defect_remap_rebuild(void);

/*!
 * @brief check remap and resume remap_idx
 *
 * @return		not used
 */
static void remap_resume(void);

init_code void defect_remap_rebuild(void)
{
	u32 i;
	u32 spb_cnt = get_total_spb_cnt();
	u32 interleave = get_interleave();
	u32 idx = 0;

	ftl_remap.interleave = interleave;
	ftl_remap.remap_cnt = def_adj->rmp_count;
	ftl_remap.remap_idx = share_malloc(sizeof(u16) * spb_cnt);

	sys_assert(ftl_remap.remap_idx);

	memset(ftl_remap.remap_idx, 0xff, sizeof(u16) * spb_cnt);
	for (i = 0; i < def_adj->rmp_count; i++) {
		ftl_remap.remap_idx[def_adj->remap[idx]] = i;
		idx += interleave;
	}
	ftl_remap.remap_tbl = def_adj->remap;
	ftl_apl_trace(LOG_ALW, 0xa0dc, "remap SPB cnt %d", def_adj->rmp_count);
}

fast_code void remap_resume(void)
{
	u32 i;
	u32 spb_cnt = get_total_spb_cnt();
	u32 interleave = get_interleave();
	u32 idx = 0;

	ftl_remap.interleave = interleave;

	sys_assert(ftl_remap.remap_idx);

	memset(ftl_remap.remap_idx, 0xff, sizeof(u16) * spb_cnt);
	for (i = 0; i < def_adj->rmp_count; i++) {
		ftl_remap.remap_idx[def_adj->remap[idx]] = i;
		idx += interleave;
	}
	ftl_remap.remap_tbl = def_adj->remap;
	ftl_apl_trace(LOG_ALW, 0xcce6, "remap SPB cnt %d %x %x", def_adj->rmp_count, interleave, ftl_remap.interleave);
}

init_code static inline int find_next_remap_can(bool no_first_blk, int off, ftl_defect_t *df, u32 target, u32 *can_bitmap)
{
	int i;
	int end = get_total_spb_cnt();
	u32 interleave = get_interleave();
	int can = -1;
	int start;

	if (no_first_blk)
		start = srb_reserved_spb_cnt;
	else
		start = off;

	for (i = start; i < end; i++) {
		if (i == off - 1)
			continue;		// self

		if (bitmap_check(can_bitmap, i) == 0)
			continue;		// don't a candidate

		spb_get_defect_map(i, df);
		if (df->df_cnt == 0) {
			bitmap_set(can_bitmap, i);
			continue;
		}

		if (df->df_cnt == interleave) {
			bitmap_set(can_bitmap, i);
			continue;
		}

		if (bitmap_check(df->df, target) == 0) {
			if (no_first_blk == false)
				return i;	// first candidate is best candidate

			if (can == -1)
				can = i;	// remember first candidate

			if (bitmap_check(df->df, 0))
				return i;	// this candidate without 1st block, it's best candidate
		}
	}
	if (can != -1)
		spb_get_defect_map(can, df);

	return can;
}

init_code void ftl_build_remap(void)
{
	int spb_cnt = get_total_spb_cnt();
	u32 interleave = get_interleave();
	ftl_defect_t remap_can_df;
	int i;
	u8 *defect_map = get_defect_map();
	u32 remap_idx;
	u32 partial_spb_cnt = 0;
	u32 defect_blk_cnt = 0;
	u32 remapped_blk_cnt = 0;
	u32 remapped_well = 0;
	u32 *remap_can_bmp = NULL;

	def_adj->signature = FTL_REMAP_SIG;

	remap_can_bmp = sys_malloc(FAST_DATA, occupied_by(spb_cnt, 32) * sizeof(u32));
	if (remap_can_bmp)
		memset(remap_can_bmp, 0, occupied_by(spb_cnt, 32) * sizeof(u32));

	ftl_apl_trace(LOG_ALW, 0x856b, "start REMAP ... %p", remap_can_bmp);

	for (i = srb_reserved_spb_cnt; i < spb_cnt; i++) {
		ftl_defect_t df;

		spb_get_defect_map(i, &df);
		if (df.df_cnt && df.df_cnt != interleave) {
			partial_spb_cnt++;
			defect_blk_cnt += df.df_cnt;

#ifdef SKIP_MODE
			// no need to set remap_can_bmp, because don't to do remap
#else
			if (remap_can_bmp)
				bitmap_set(remap_can_bmp, i);
#endif		

		}

		if (bitmap_check(df.df, 0) == 1)
			remap_print("remap can spb %d\n", i);

	}
	ftl_apl_trace(LOG_ALW, 0xa81d, "partial SPB %d, blk %d/%d",
			partial_spb_cnt, defect_blk_cnt, partial_spb_cnt * interleave);

	remap_idx = 0;
	for (i = srb_reserved_spb_cnt; i < spb_cnt; i++) {
		ftl_defect_t df;
		u32 idx;
		u32 remapped = 0;
		int remap_can = -1;

		spb_get_defect_map(i, &df);
		if (df.df_cnt == 0)
			continue;

		if (bitmap_check(df.df, 0))
			continue;

		remap_print("spb %d(%d) %x %x %x %x:", i, df.df_cnt, df.df[0], df.df[1], df.df[2], df.df[3]);
		for (idx = 0; idx < interleave; idx++) {
                  #ifdef SKIP_MODE
                     // no remap, so prt to myself
                  #else
			if (bitmap_check(df.df, idx) == 0) 
                  #endif
			{
				def_adj->remap[remap_idx + idx] = i;
				continue;
			}

			if (remap_can == -1)
				remap_can = find_next_remap_can(true, i + 1, &remap_can_df, idx, remap_can_bmp);

			while (remap_can != -1 && bitmap_check(remap_can_df.df, idx)) {
				remap_can = find_next_remap_can(true, remap_can + 1, &remap_can_df, idx, remap_can_bmp);
				if (remap_can == -1)
					break;
			}

			if (remap_can == -1) {
				remap_print("%d-N,", idx);
				def_adj->remap[remap_idx + idx] = INV_SPB_ID;
				continue;
			}

			remap_print("%d-%d,", idx, remap_can);
			remapped++;
			def_adj->remap[remap_idx + idx] = remap_can;
			df.df_cnt--;
			remapped_blk_cnt++;
			bitmap_reset(df.df, idx);
			bitmap_reset((u32*)defect_map, i * df_width + idx);
			bitmap_set(remap_can_df.df, idx);
			bitmap_set((u32*)defect_map, remap_can * df_width + idx);
		}
		remap_print(" (%d \n", remapped);
		if (df.df_cnt == 0) {
			remapped_well++;
		} else {
			remapped = 0;
			// restore remap due to no remapped well
			for (idx = 0; idx < interleave; idx++) {
				spb_id_t spb_id;

				spb_id = def_adj->remap[remap_idx + idx];
				if (spb_id == INV_SPB_ID)
					continue;

				if (spb_id == i)
					continue;

				bitmap_set((u32*)defect_map, i * df_width + idx);
				bitmap_reset((u32*)defect_map, spb_id * df_width + idx);
			}
		}

		if (remapped) {
			remap_idx += interleave;
			def_adj->rmp_count++;
		}
	}
	ftl_apl_trace(LOG_ALW, 0xeb38, "REMAP done ... total %d/%d remapped", remapped_well, def_adj->rmp_count);
	ftl_apl_trace(LOG_ALW, 0xa1f5, "total remapped blk %d %d/%d", remapped_blk_cnt,
			remapped_well * interleave,
			partial_spb_cnt * interleave - defect_blk_cnt);

	if (remap_can_bmp)
		sys_free(FAST_DATA, remap_can_bmp);

	defect_remap_rebuild();
}

init_code u32 defect_version_check(bool remap)
{
	u16 version = ftl_df_map->version;

	if (version != FTL_DEFECT_VERSION || ftl_df_map->flags.b.remap != remap) {
		ftl_apl_trace(LOG_ALW, 0x9638, "defect version %d - %d (%d)", version,
				FTL_DEFECT_VERSION, ftl_df_map->interleave);

		ftl_df_map->version = FTL_DEFECT_VERSION;
		return false;
	}

	if (remap && def_adj->signature != FTL_REMAP_SIG) {
		ftl_apl_trace(LOG_ALW, 0xe326, "remap sig error %x", def_adj->signature);
		return false;
	}
	return true;
}

fast_code void grwn_defect_rebuild(void)
{
	u32 i = 0;
	u32 j = 0;

	while (1) {
		u32 spb = get_grwn_defect_info(i);

		if (spb == ~0)
			return;

		u32 index = spb * df_width;
		u32 end = index + ftl_df_map->interleave;

		ftl_frb_trace(LOG_ALW, 0x1a8c, "rebuild grown defect %d", spb);

		for (j = index; j < end; j++)
			bitmap_set((u32*)get_defect_map(), j);

		i++;
	}
}

/*!
 * @brief get current defect block count
 *
 * @param total_blk_cnt		total defect block count
 *
 * @return			not used
 */
init_code void ftl_get_dft_blk_get(u32 *total_blk_cnt)
{
	u32 total_spb = ftl_df_map->spb_cnt;
	u32 total_dft_blk_cnt = 0;
	spb_id_t spb_id = 0;

	for (spb_id = 0; spb_id < total_spb; spb_id++)
		total_dft_blk_cnt += spb_get_defect_cnt(spb_id);

	*total_blk_cnt = total_dft_blk_cnt;
}

extern bool _fg_warm_boot;
extern u8 FTL_scandefect;  //AlanHuang
extern smart_statistics_t *smart_stat;
init_code void ftl_init_defect(bool *frb_valid)
{
	u32 spb_cnt = get_total_spb_cnt();
	u32 interleave = get_interleave();
	u32 table_size;
	u32 remap_size = 0;
	srb_t *srb = (srb_t *)SRB_HD_ADDR;
	bool remap = true;

	df_width = occupied_by(interleave, 8) << 3;
	table_size = spb_cnt * df_width / 8;
	df_map_size = offsetof(ftl_defect_map_t, defect_map) + table_size;

	u32 DtagCnt = occupied_by(df_map_size, DTAG_SZE);
	ftl_df_map = (void *) ddtag2mem(ddr_dtag_epm_register(DtagCnt));  //ftl_get_io_buf

	if (remap) {
		remap_size = spb_cnt * interleave * sizeof(spb_id_t);
		def_adj_size = offsetof(def_adj_t, remap) + remap_size;
                DtagCnt = occupied_by(def_adj_size, DTAG_SZE);
		def_adj = (void *) ddtag2mem(ddr_dtag_epm_register(DtagCnt));  // ftl_get_io_buf
	}

build:

#if EPM_OTF_Time
	if(_fg_warm_boot == false)
#endif
	{
	def_adj->rmp_count = 0;
	def_adj->signature = 0;

	ftl_df_map->flags.all = 0;
	ftl_df_map->flags.b.remap = remap;
	ftl_df_map->flags.b.mp_prune = false; //true
	ftl_df_map->flags.b.pblk = 1;
	ftl_df_map->interleave = interleave;
	ftl_df_map->signature = FTL_DF_SIG;
	ftl_df_map->spb_cnt = spb_cnt;
	ftl_df_map->pblk_cnt = 0;
	ftl_df_map->total_size = df_map_size;
	ftl_df_map->version = FTL_DEFECT_VERSION;
	}

	frb_log_type_init(FRB_TYPE_DEFECT, df_map_size, ftl_df_map, FRB_LOG_IDX_F_IO_BUF);
	if (remap)
		frb_log_type_init(FRB_TYPE_REMAP, def_adj_size, def_adj, FRB_LOG_IDX_F_IO_BUF);
    sys_assert_DQA(srb->srb_hdr.srb_signature == SRB_SIGNATURE);
	if (srb->srb_hdr.srb_signature != SRB_SIGNATURE) {
		if (*frb_valid == true) {
#if defined(L2CACHE)
			l2cache_mem_flush(ftl_df_map, df_map_size);
			l2cache_mem_flush(def_adj, def_adj_size);
#endif
			frb_log_type_load(FRB_TYPE_DEFECT, NULL);
			if (remap)
				frb_log_type_load(FRB_TYPE_REMAP, NULL);

			if (!defect_version_check(remap)) {
				*frb_valid = false;
				goto build;
			}
		} else {
			log_level_t old = (log_level_t) ipc_api_log_level_chg(LOG_ALW);
			u8 *defect_map = get_defect_map();

			memset(defect_map, 0, table_size);
			ftl_scan_defect();

			ipc_api_log_level_chg(old);
		}
	}
	else {
		u8 *defect_map = get_defect_map();

#if EPM_OTF_Time
		if(_fg_warm_boot == false)
#endif
		{
		memset(defect_map, 0, table_size);
		}

#if defined(L2CACHE)
		l2cache_mem_flush(ftl_df_map, df_map_size);
		l2cache_mem_flush(def_adj, def_adj_size);
#endif
		if (*frb_valid == true) {

#if EPM_OTF_Time
		if(_fg_warm_boot == false)
#endif
		{
			frb_log_type_load(FRB_TYPE_DEFECT, NULL);
			if (remap)
				frb_log_type_load(FRB_TYPE_REMAP, NULL);
		}

			if (!defect_version_check(remap)) {
				*frb_valid = false;
				goto build;
			}
		} else {
			#ifdef ERRHANDLE_VERIFY	// FET, RelsP2AndGL
			if (!FTL_scandefect)
			#endif
			{
	        	FTL_scandefect = 0;
				load_mr_defect();
			}

			// FTL_scandefect may be raised in load_mr_defect.
			if(FTL_scandefect == 1)  //AlanHuang
			{
				log_level_t old = (log_level_t) ipc_api_log_level_chg(LOG_ALW);
				//ftl_scan_defect();
				smart_stat->critical_warning.bits.device_reliability = 1;
				ipc_api_log_level_chg(old);

				#if(degrade_mode == ENABLE)
				extern none_access_mode_t noneaccess_mode_flags;
				//extern void cmd_disable_btn()

				noneaccess_mode_flags.b.defect_table_fail = 1;
				cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
				cpu_msg_issue(CPU_FE - 1, CPU_MSG_DISABLE_BTN, 0, false);
				//cmd_disable_btn(-1,1);
				#endif
			}

		}
	}

#ifdef TCG_NAND_BACKUP
	//extern bool bTcgTblErr;
	if(tcg_mid_info != NULL)
	{
		u32 i = 0;
		u8 *defect_map_for_tcg = get_defect_map();
		u32 tcg_cnt = TCG_SPB_ALLOCED * interleave / 8;
		defect_map_for_tcg += ((spb_cnt - TCG_SPB_ALLOCED) * interleave / 8);

		memset(tcg_mid_info->defect_map, 0xff, sizeof(tcg_mid_info->defect_map));
		memcpy32((void *)tcg_mid_info->defect_map, (const void *)defect_map_for_tcg, tcg_cnt);

		//set erased_blk 0 if def_map is 1 (bad blk)
		for(i=0; i<(sizeof(tcg_mid_info->defect_map)/sizeof(u32)); i++)
		{
			tcg_mid_info->blk_erased[i] &= (~tcg_mid_info->defect_map[i]);
		}
	}
#endif

	if (!(*frb_valid)) {
		if (ftl_df_map->flags.b.mp_prune)
			ftl_defect_prune_for_mp();
		if (ftl_df_map->flags.b.remap)
		{
			u8 *def_adj_map = (u8*) def_adj->remap;    //Aging defecttable overwrite, if disable remap need to modify

			memset(def_adj_map, 0, def_adj_size);

			ftl_build_remap();
		}

	#ifdef SKIP_MODE
		if (ftl_df_map->flags.b.pblk && ftl_df_map->pblk_cnt < FTL_PBLK_MAX)
			ftl_defect_scan_pblk(interleave); //interleave / 2  need to check
	#else
		if (ftl_df_map->flags.b.pblk && ftl_df_map->pblk_cnt < FTL_PBLK_MAX)
			ftl_defect_scan_pblk(interleave / 2); 
	#endif	

		reset_grwn_defect();
		frb_log_type_update(FRB_TYPE_REMAP);
		frb_log_type_update(FRB_TYPE_DEFECT);
		frb_log_type_update(FRB_TYPE_HEADER);
	} else {
		grwn_defect_rebuild();
		if (remap)
			defect_remap_rebuild();
	}

	if (ftl_df_map->pblk_cnt)
		blk_pool_init(ftl_df_map->pblk_cnt, ftl_df_map->pblk[PBLK_ROOT_IDX].pblk.pblk);

#ifdef SKIP_MODE	
	gl_pt_defect_tbl =  get_defect_map(); //init global defect tbl pointer
#endif	

	QTAILQ_INIT(&grwn_err_info_list);
	pool_init(&grwn_err_pool, (void*)&grwn_base[0], sizeof(grwn_base),
				sizeof(grwn_info_t), GRWN_ERR_CNT);
}

fast_code void defect_mgr_resume(void)
{
	def_adj->rmp_count = 0;
	def_adj->signature = 0;

	ftl_df_map->flags.all = 0;
	ftl_df_map->flags.b.remap = true;
	ftl_df_map->interleave = get_interleave();
	ftl_df_map->signature = FTL_DF_SIG;
	ftl_df_map->spb_cnt = get_total_spb_cnt();
	ftl_df_map->total_size = df_map_size;
	ftl_df_map->version = FTL_DEFECT_VERSION;

	frb_log_type_load(FRB_TYPE_DEFECT, NULL);
	frb_log_type_load(FRB_TYPE_REMAP, NULL);
	grwn_defect_rebuild();
	remap_resume();
	ncl_sync_reinit_remap();

	return;
}

fast_code void ins_grwn_err_info(u8 type, pda_t pda)
{
	grwn_info_t *gdef;

	QTAILQ_FOREACH(gdef, &grwn_err_info_list, link) {
		if (nal_pda_get_block_id(gdef->pda) == nal_pda_get_block_id(pda)) {
			ftl_apl_trace(LOG_ERR, 0xb2da, "grown defect multiple error %x %x", type, pda);
			return;
		}
	}

	gdef = (grwn_info_t *) pool_get_ex(&grwn_err_pool);
	if (!gdef) {
		ftl_apl_trace(LOG_ERR, 0xa481, "grown defect insert list fail %x %x", type, pda);
		return;
	}

	gdef->type = type;
	gdef->pda = pda;
	QTAILQ_INSERT_TAIL(&grwn_err_info_list, gdef, link);
}


fast_code void spb_get_defect_map(spb_id_t spb_id, ftl_defect_t *ftl_df)
{
	u32 df_byte = df_width >> 3;
	u32 index = spb_id * df_byte;
	u32 i;
	u32 dw_cnt;
	u8 *defect_map = get_defect_map();

	dw_cnt = byte2dword(&defect_map[index], ftl_df->df, df_byte);

	ftl_df->df_cnt = 0;
	for (i = 0; i < dw_cnt; i++)
		ftl_df->df_cnt += pop32(ftl_df->df[i]);
}

fast_code u32 spb_get_defect_cnt(spb_id_t spb_id)
{
	ftl_defect_t ftl_df;

	spb_get_defect_map(spb_id, &ftl_df);

	return ftl_df.df_cnt;
}

#ifdef SKIP_MODE //Alan Huang
fast_code void spb_check_defect_cnt(u16 spb_id)
{
	u32 interleave = get_interleave();
	u32 thr = interleave - min_good_pl;
	u32 df_cnt = spb_get_defect_cnt(spb_id);

	if (df_cnt > thr)
	{	
		FTL_BlockPopPushList(SPB_POOL_UNALLOC, spb_id, FTL_SORT_NONE);
		ftl_apl_trace(LOG_ALW, 0x2d9c, "spb %d dfcnt %d ", spb_id, df_cnt);	
		/*
		//spb_set_defect_blk(spb_id, ~0);
		if((spb_info_get(spb_id)->pool_id == SPB_POOL_QBT_FREE) || (spb_info_get(spb_id)->pool_id == SPB_POOL_QBT_ALLOC))
		{
			shr_ftl_flags.b.qbt_retire_need = 1;
			ftl_apl_trace(LOG_ALW, 0, "spb %d dfcnt %d ", spb_id, df_cnt);
			qbt_retire_handle(spb_id);
		}
		else
		{
			FTL_BlockPopPushList(SPB_POOL_UNALLOC, spb_id, FTL_SORT_NONE);
	        ftl_apl_trace(LOG_ALW, 0, "spb %d dfcnt %d ", spb_id, df_cnt);	
		}*/
	}
}
#endif

share_data volatile u32 GrowPhyDefectCnt; //for SMART GrowDef Use
/* if blk == ~0, which mean all blk */
fast_code u32 spb_set_defect_blk(spb_id_t spb_id, u32 blk)
{
	u32 cnt = 1;
	u32 index = spb_id * df_width;
	u32 end;
	u32 i;
	u32 df_inc = 0;
	grwn_info_t *gdef;
	u8 *defect_map = get_defect_map();

	ftl_apl_trace(LOG_ERR, 0xe262, "mark spb %d grown defect %x", spb_id, blk);

	if (blk == ~0) {
		cnt = ftl_df_map->interleave;
	} else {
		index += blk;
	}

	end = index + cnt;

	for (i = index; i < end; i++) {
		if (!bitmap_check((u32*)&defect_map[0], i)) {
			bitmap_set((u32*)defect_map, i);
			df_inc++;
		}
	}

	extern tencnet_smart_statistics_t *tx_smart_stat;
	tx_smart_stat->bad_block_failure_rate += cnt ; //for SMART GrowDef Use	

	QTAILQ_FOREACH(gdef, &grwn_err_info_list, link) {
		if (nal_pda_get_block_id(gdef->pda) == spb_id) {
			QTAILQ_REMOVE(&grwn_err_info_list, gdef, link);
			frb_log_update_grwn_def(gdef->type, gdef->pda);
			pool_put_ex(&grwn_err_pool, (void *)gdef);
			return df_inc;
		}
	}

	ftl_apl_trace(LOG_ERR, 0x1895, "search spb %d miss", spb_id);
	frb_log_update_grwn_def(GRWN_DEF_TYPE_OTHR_UNKN, nal_make_pda(spb_id, 0));
	return df_inc;
}

/* private function */
inline u8* get_defect_map(void)
{
	return ftl_df_map->defect_map;
}

init_code void ftl_scan_defect(void)
{
	//panic("not support");
#if 1  //AlanHuang
	u32 i;
	u32 spb_cnt = ftl_df_map->spb_cnt;
	u32 width = ftl_df_map->interleave;
	u8 *defect_map = get_defect_map();

	for (i = srb_reserved_spb_cnt; i < spb_cnt; i++) {
		bool ret;
		u32 j;
		struct info_param_t *info_list;
		u32 d = 0;
		u32 df[8];  //df[4]
		u32 index = df_width * i;

		ret = ncl_spb_sync_erase(i, NAL_PB_TYPE_XLC, &info_list);

		memset(df, 0, sizeof(df));
		ncl_spb_defect_scan(i, df);

		for (j = 0; j < width; j++) {
			if (((ret == false) && (info_list[j].status != ficu_err_good)) ||
					bitmap_check(df, j)) {

				bitmap_set((u32*)&defect_map[0], index + j);
				d++;
			}
		}

		if (ret == false)
			share_free(info_list);  //sys_free

		/*if (d != 0) {
			u32 byte_cnt = width >> 3;
			u32 dw_cnt;

			ftl_apl_trace(LOG_ALW, 0, "partial defect SPB %d,", i);
			dw_cnt = byte2dword(&defect_map[index >> 3], df, byte_cnt);

			for (j = 0; j < dw_cnt; j++)
				ftl_apl_trace(LOG_ALW, 0, " %x", df[j]);
		}*/
	}
#endif
}

fast_code u32 byte2dword(u8 *b, u32 *dw, u32 byte_cnt)
{
	u32 i = 0;
	u32 idx = 0;
	u32 dw_cnt = occupied_by(byte_cnt, sizeof(u32));

	for (i = 0; i < dw_cnt; i++) {
		u32 j;

		dw[i] = 0;

		for (j = 0; j < sizeof(u32); j++) {
			dw[i] |= b[idx] << (j << 3);
			idx++;
			if (idx >= byte_cnt) {
				return dw_cnt;
			}
		}
	}
	return dw_cnt;
}

init_code void trans_to_ftl_df(u32 *def, u8 *ftl_df_map)
{
	u32 i;
	u32 dw_cnt = occupied_by(get_interleave(), 32);
	u32 df_byte = df_width / 8;

	// disk def pl lun ce ch
	for (i = 0; i < get_total_spb_cnt(); i++) {
		u32 *bmp = &def[i * dw_cnt];
		// dword base to byte base, save more memory
		memcpy(&ftl_df_map[i * df_byte], bmp, df_byte);
	}
}

init_code void load_mr_defect(void)
{
	u8 *defect_map = get_defect_map();

#if Aging_defect
	srb_load_mr_defect(defect_map, sizeof(u8));
#else

        srb_t *srb = (srb_t *)SRB_HD_ADDR;

	if (srb->dftb_ftl_sz != 0)
		srb_load_mr_defect(defect_map, sizeof(u8));
	else
		panic("not support yet");
#endif
}

init_code void ftl_add_pblk(u32 spb_id, u32 iid)
{
	u32 idx = ftl_df_map->pblk_cnt;

	if (idx >= FTL_PBLK_MAX)
		return;

	ftl_df_map->pblk[idx].attr.all = 0;
	ftl_df_map->pblk[idx].pblk.spb_id = spb_id;
	ftl_df_map->pblk[idx].pblk.iid = iid;
	ftl_apl_trace(LOG_ALW, 0x9572, "pblk %d, %d-%d", idx, spb_id, iid);
	ftl_df_map->pblk_cnt++;
}

init_code void ftl_defect_prune_for_mp(void)
{
	u32 mp = get_mp();
	u32 spb_cnt = ftl_df_map->spb_cnt;
	u32 i;
	u8 mp_mask;
	u32 df_byte = df_width >> 3;
	u8 *defect_map = get_defect_map();

	sys_assert(mp < 8);
	mp_mask = (1 << mp) - 1;

	ftl_apl_trace(LOG_ALW, 0xb441, "mp mask %x", mp_mask);

	for (i = srb_reserved_spb_cnt; i < spb_cnt; i++) {
		u32 j;
		u8 od[32];
		u8 nd[32];
		u32 df[4];
		u32 idx = i * df_byte;
		bool print = false;
		u32 iid = 0;

		sys_assert(df_byte <= 32);
		memcpy(od, &defect_map[idx], df_byte);
		memset(nd, 0, df_byte);

		for (j = 0; j < df_byte; j++) {
			u32 k;

			for (k = 0; k < 8; k += mp) {
				u8 m = mp_mask << k;
				u8 d = m & od[j];

				if (d) {
					nd[j] |= m;
					print = true;
					if (d != m) {
						if (ftl_df_map->flags.b.pblk) {
							u32 drop_bmp = (~od[j]) & m;

							while (drop_bmp) {
								u32 z = ctz(drop_bmp);

								ftl_add_pblk(i, z + j * 8);
								drop_bmp &= ~(1 << z);
							}
						}

					}
				}

			}
			iid += 8;
		}

		if (print) {
			u32 dw_cnt;

			//ftl_apl_trace(LOG_ALW, 0, "spb %d, old defect", i);
			dw_cnt = byte2dword(od, df, df_byte);
			for (j = 0; j < dw_cnt; j++) {
				//ftl_apl_trace(LOG_ALW, 0, " %x", df[j]);
			}

			//ftl_apl_trace(LOG_ALW, 0, "        new defect");
			dw_cnt = byte2dword(nd, df, df_byte);
			for (j = 0; j < dw_cnt; j++) {
				//ftl_apl_trace(LOG_ALW, 0, " %x", df[j]);
			}

			memcpy(&defect_map[idx], nd, df_byte);
		}
	}
}

init_code void ftl_defect_scan_pblk(u32 gblk_thr)
{
	int spb_cnt = get_total_spb_cnt();
	u32 interleave = get_interleave();
	int i;
	u8 *defect_map = get_defect_map();

	if (gblk_thr != 0) {
		ftl_defect_scan_pblk(gblk_thr >> 1);
		if (ftl_df_map->pblk_cnt >= FTL_PBLK_MAX)
			return;
	} else {
		gblk_thr = 1;
	}

	ftl_apl_trace(LOG_ALW, 0x808b, "start find pblk ... %d", gblk_thr);

#ifdef While_break
	u64 start;
#endif

	for (i = srb_reserved_spb_cnt; i < spb_cnt; i++) {
		ftl_defect_t df;
		u32 gblk;

		spb_get_defect_map(i, &df);
		if (df.df_cnt == 0 || df.df_cnt == interleave)
			continue;

		gblk = interleave - df.df_cnt;
	#ifdef SKIP_MODE		
		if (ftl_df_map->pblk_cnt == FTL_PBLK_MAX)  //AlanHuang
			break;
	#else
		if (gblk > FTL_PBLK_MAX - ftl_df_map->pblk_cnt)
			break;		
	#endif

		if (gblk < gblk_thr) {
			u32 iid = find_first_zero_bit(df.df, interleave);

#ifdef While_break
	start = get_tsc_64();
#endif
			do {
				u32 idx = i * interleave;

				ftl_add_pblk(i, iid);

				sys_assert(bitmap_check((u32*)defect_map, idx + iid) == 0);
				bitmap_set((u32*)defect_map, idx + iid);
				iid = find_next_zero_bit(df.df, interleave, iid + 1);
#ifdef While_break
				if(Chk_break(start,__FUNCTION__, __LINE__))
					break;
#endif

			} while (iid < interleave && ftl_df_map->pblk_cnt < FTL_PBLK_MAX);
		}
	}

	if((ftl_df_map->pblk_cnt != FTL_PBLK_MAX) && (gblk_thr == interleave))
	{
		ftl_apl_trace(LOG_ALW, 0xe561, "Attention : pblk_cnt not enough FTL_PBLK_MAX, need to find again ");

		for (i = srb_reserved_spb_cnt; i < spb_cnt; i++) {
			ftl_defect_t df;

			spb_get_defect_map(i, &df);
			if (df.df_cnt == interleave)
				continue;

#ifdef SKIP_MODE		
			if (ftl_df_map->pblk_cnt == FTL_PBLK_MAX)  //AlanHuang
				break;
#else
			//if (gblk > FTL_PBLK_MAX - ftl_df_map->pblk_cnt)
				//break;		
#endif

				u32 iid = find_first_zero_bit(df.df, interleave);

	#ifdef While_break
			 start = get_tsc_64();
	#endif

				do {
					u32 idx = i * interleave;

					ftl_add_pblk(i, iid);

					sys_assert(bitmap_check((u32*)defect_map, idx + iid) == 0);
					bitmap_set((u32*)defect_map, idx + iid);
					iid = find_next_zero_bit(df.df, interleave, iid + 1);

	#ifdef While_break
					if(Chk_break(start,__FUNCTION__, __LINE__))
						break;
	#endif
				} while (iid < interleave && ftl_df_map->pblk_cnt < FTL_PBLK_MAX);
		}
	}
}

fast_code void update_pblk_tbl(void)
{
	blk_pool_copy(ftl_df_map->pblk);
	ftl_df_map->pblk_cnt = get_blk_pool_size() / sizeof(phy_blk_t);

	frb_log_type_update(FRB_TYPE_DEFECT);
}

init_code phy_blk_t *get_pblk_tbl(void)
{
	return ftl_df_map->pblk;
}

/*! @} */
