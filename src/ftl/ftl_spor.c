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


//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "ftlprecomp.h"
#include "ftl_flash_geo.h"
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "idx_meta.h"
#include "sync_ncl_helper.h"
#include "l2p_mgr.h"
#include "ipc_api.h"
#include "l2cache.h"
#include "spb_mgr.h"
#include "ftl_remap.h"
#include "ftl_ns.h"
#include "mpc.h"
#include "ncl_cmd.h"
#include "fc_export.h"
#include "ftl_l2p.h"
#include "ftl_spor.h"
#include "ddr_top_register.h"
#include "../ncl_20/ncl_raid.h"
#include "../ncl_20/GList.h"
#include "../ncl_20/inc/eccu_reg_access.h"
#include "trim.h"
#include "frb_log.h"

/*! \cond PRIVATE */
#define __FILEID__ ftlspor
#include "trace.h"
/*! \endcond */

#if (SPOR_FLOW == mENABLE)
share_data_zi volatile u32 shr_p2l_grp_cnt;
share_data_zi volatile u32 shr_wl_per_p2l;
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
share_data_zi volatile bool delay_flush_spor_qbt;
#endif

#if(PLP_SUPPORT == 0)  
init_data bool ftl_spor_incomp_fag = false;
init_data u16 epm_first_open_wl    = 0xFFFF;
#define WL_DETECT mENABLE
#endif

#if(WL_DETECT == mENABLE)
slow_data struct ncl_cmd_t ncl_cmd_sf_1;
slow_data struct ncl_cmd_t ncl_cmd_ssr_1;
slow_data bm_pl_t bm_pl_ssr_1;
init_data u32 ftl_ssread_dtag;
slow_data_zi u32 ssread_meta_idx;			    ///< meta index of ssread meta
slow_data_zi struct du_meta_fmt *ssread_meta;   // ssread meta
#endif

// added by Sunny
#if (SPOR_CMD_EXP_BAND == mENABLE)
slow_data_zi u32 _spor_free_cmd_bmp;   // ncl free command bitmap for SPOR form submit
#else
slow_data_zi u16 _spor_free_cmd_bmp;   // ncl free command bitmap for SPOR form submit
#endif

slow_data_zi u32 ftl_spor_meta_idx;	   // ftl spor ddr meta index
slow_data_zi struct du_meta_fmt *ftl_spor_meta; // frl spor ddr meta addr

slow_data_ni struct ncl_cmd_t    *ftl_ncl_cmd;  // 80B
slow_data_zi struct info_param_t *ftl_info;     // 4B
slow_data_zi bm_pl_t             *ftl_bm_pl;    // 8B
slow_data_zi pda_t               *ftl_pda_list; // 4B
//slow_data_ni pda_t               l2p_old_pda = INV_U32;
init_data struct ftl_spor_p2l_pos_list_t *ftl_p2l_pos_list; // 8B
init_data struct ftl_spor_info_t *ftl_spor_info;   // ~= 1376B
init_data u8 *ftl_build_tbl_type;
init_data u32 *ftl_init_sn_tbl;

init_data u32  *ftl_chk_last_wl_bitmap;			// set spor dummy close blk open
init_data bool  spor_die_sch_wl = false;		// spor fill dummy for die sch
//init_data u64   spor_dummy_bit = 0;			// spor fill dummy for die sch
init_data u32   spor_dummy_bit[4] = {0};		// spor fill dummy for die sch

init_data u32  *ftl_special_plp_blk;			// plp save p2l to last group p2l position
init_data bool  spor_epm_use = false;
init_data u16  *ftl_blk_pbt_seg;
//init_data u16 plp_group = 0;

#if (SPOR_VAC_CMP == mENABLE)
init_data u32 *ftl_init_vc_tbl;  // for vac dbg, by Sunny 20210312
#endif

init_data struct ftl_blk_pool_t  *ftl_spor_blk_pool;  // 90B
init_data struct ftl_blk_node_t  *ftl_spor_blk_node;  // 5B

init_data struct ftl_scan_info_t *ftl_qbt_scan_info;  // 7B
init_data struct ftl_scan_info_t *ftl_pbt_scan_info;

init_data u8  *ftl_p2l_fail_grp_flag;
init_data u16 ftl_p2l_fail_cnt;
init_data u32 ftl_blk_aux_fail_cnt;
#if (SPOR_CHECK_TAG == mENABLE)
init_data lda_t pbt_chk_lda_buffer[4*12*3];//4---4plane,12----1plane du cnt,3---3die
#endif
init_data struct ftl_spor_p2l_t  ftl_host_p2l_info;
init_data struct ftl_spor_p2l_t  ftl_gc_p2l_info;
#if (SPOR_AUX_MODE_DBG == mENABLE)
init_data struct ftl_spor_p2l_t  ftl_dbg_p2l_info;
#endif

init_data static u32   usr_page_idx = 0, gc_page_idx = 0;
init_data static u32   adjust_gc_page_idx = 0;
init_data static u32   adjust_host_page_idx = 0;
init_data static u32   tmp_host_page_idx = INV_U32;
init_data static u32   usr_page_threshold = 0, gc_page_threshold = 0;

init_data u32 ftl_dummy_dtag;
init_data bool had_gc_data = false;
init_data bool had_host_data = false;
init_data bool pregc_update_all = false;
init_data bool replace_qbt = false;
share_data_zi epm_info_t*  shr_epm_info;
share_data_zi u8* gl_pt_defect_tbl;
extern u16 min_good_pl;
init_data spb_id_t pre_gc_blk   = INV_U16;
init_data spb_id_t pre_host_blk = INV_U16;

init_data spb_id_t last_pbt_blk = INV_U16;
init_data u16      last_pbt_seg = 0;
init_data bool     pbt_new_build = false;

init_data bool     spor_skip_raid_tag = false; // add by Jay for skip raid PLP_TAG
init_data bool     blk_list_need_check_flag = false;

init_data u32      record_spor_last_rec_blk_sn = 0;
slow_data bool	   SPOR_need_vac_rebuild = false;

extern u8 tcg_reserved_spb_cnt;
extern volatile bool shr_qbt_prog_err;

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
share_data_zi volatile u32 shr_spb_info_ddtag;
extern epm_info_t*  shr_epm_info;
#endif

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
init_data bool   need_trigger_slc_gc = false;
extern volatile u8 shr_slc_flush_state;
extern pda_t shr_plp_slc_start_pda;
extern pda_t shr_plp_slc_end_pda;
extern u32	 shr_plp_slc_need_gc;
init_data pda_t  slc_read_blank_pos = INV_PDA;
#endif

#if (PLP_SUPPORT == 0)
init_data u32*    ftl_spor_trimblkbitmap;
init_data bool   need_trigger_non_plp_gc = false;

init_data spb_id_t cur_blank_spb = INV_U16;
init_data u16      cur_blank_grp = INV_U16;
#endif

slow_data_zi u32 min_build_sn;


#if(degrade_mode == ENABLE)
extern smart_statistics_t *smart_stat;
#endif

init_data bool   skip_spor_build = false; //fqbt done , but gc no done, next spor just use fqbt build

#if defined(HMETA_SIZE)
extern enum du_fmt_t host_du_fmt; // Jamie 20210303
#endif
#define BLOCK_NO_BAD    (0)
#define BLOCK_P0_BAD    (1)
#define BLOCK_P1_BAD    (2)
#define BLOCK_ALL_BAD   (15)
#define BIT_CHECK(x, i) ((x>>i)&1)

#define NO_NEED_UPDATE          (0)
#define UPDATE_NEW_PTA_VAC      (1)
#define UPDATE_L2P_VAC          (2)
#define UPDATE_NEW_PTA_ONLY     (3)
#define UPDATE_OLD_PTA_VAC      (4)

#define NO_UPDATE_VAC          (0)
#define UPDATE_LAST_BLK_VAC    (1)
#define UPDATE_ALL_BLK_VAC     (2)
#define UPDATE_CHECK_POINT_VAC (3)

#define P2L_USR_ONLY            (BIT0)
#define P2L_GC_ONLY             (BIT1)
#define P2L_ALL                 (BIT2)

#if (WL_DETECT == mENABLE)
slow_code bool ssread(pda_t pda)
{
    struct info_param_t info;
    
    u32 *mem = NULL;
	dtag_t dtag = {.dtag = (ftl_ssread_dtag | DTAG_IN_DDR_MASK)};
	mem = (u32*)ddtag2mem(ftl_ssread_dtag);
	//dtag = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (mem == NULL) {
		ftl_apl_trace(LOG_WARNING, 0x53e7, "No dtag");
		return 0;
	}
    //bm_pl settings
    bm_pl_ssr_1.all = 0;
    bm_pl_ssr_1.pl.dtag = dtag.dtag;
    bm_pl_ssr_1.pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
    bm_pl_ssr_1.pl.nvm_cmd_id = ssread_meta_idx;

    //ncl_cmd settings
    ncl_cmd_ssr_1.op_code = NCL_CMD_OP_SSR_READ;
    ncl_cmd_ssr_1.op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    ncl_cmd_ssr_1.flags =  NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG | NCL_CMD_SYNC_FLAG;
    ncl_cmd_ssr_1.du_format_no = DU_FMT_USER_4K;
    ncl_cmd_ssr_1.addr_param.common_param.list_len = 1;
    ncl_cmd_ssr_1.addr_param.common_param.pda_list = &pda;
	ncl_cmd_ssr_1.addr_param.common_param.info_list = &info;
	ncl_cmd_ssr_1.addr_param.rw_raw_param.pda_list = ncl_cmd_ssr_1.addr_param.common_param.pda_list;

    //SSR setting and record tracking step
    ncl_cmd_ssr_1.read_level = SSR_GR;
    ncl_cmd_ssr_1.addr_param.rw_raw_param.column->column = SSR_GR;
    ncl_cmd_ssr_1.user_tag_list = &bm_pl_ssr_1;
    ncl_cmd_ssr_1.completion = NULL;

    ncl_cmd_submit(&ncl_cmd_ssr_1);
    u32 cellcnt = 0;

    cellcnt = ncl_cmd_ssr_1.raw_1bit_cnt;//Get 1 cnt from FICU SQ interrupt.

    if (cellcnt < 32488)
        return 1; // complete wl
    else
        return 0; // incomplete wl
}
#endif
//====================================
//========SPOR Functions Start========
//============by Sunny Lin============
//====================================

/*!
 * @brief ftl spor function
 * Translate CPDA info to PDA info
 *
 * @return	not used
 */
init_code pda_t ftl_cpda2pda(pda_t cpda, u8 data_mode)
{
	pda_t pda;
	u32 ch=0, ce=0, lun=0, plane=0, block=0, pg_in_wl=0, du=0, wl=0, page=0;

	// CPDA to ch, ce, lun, pl, blk, pg, du
    switch(data_mode)
    {
        case V_MODE:
            du = cpda & (DU_CNT_PER_PAGE - 1);
        	cpda >>= DU_CNT_SHIFT;
            plane = cpda & (shr_nand_info.geo.nr_planes- 1);
            cpda >>= ctz(shr_nand_info.geo.nr_planes);
            pg_in_wl = cpda % shr_nand_info.bit_per_cell;
            cpda /= shr_nand_info.bit_per_cell;
            ch = cpda % shr_nand_info.geo.nr_channels;
        	cpda /= shr_nand_info.geo.nr_channels;
            ce = cpda % shr_nand_info.geo.nr_targets;
        	cpda /= shr_nand_info.geo.nr_targets;
        	lun = cpda & (shr_nand_info.geo.nr_luns - 1);
        	cpda >>= ctz(shr_nand_info.geo.nr_luns);
            wl = cpda % (shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);
        	block = cpda/(shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);

            page = (wl*shr_nand_info.bit_per_cell) + pg_in_wl;
            break;

        case H_MODE:
            du = cpda & (DU_CNT_PER_PAGE - 1);
        	cpda >>= DU_CNT_SHIFT;
            ch = cpda % shr_nand_info.geo.nr_channels;
        	cpda /= shr_nand_info.geo.nr_channels;
            plane = cpda & (shr_nand_info.geo.nr_planes- 1);
            cpda >>= ctz(shr_nand_info.geo.nr_planes);
            pg_in_wl = cpda % shr_nand_info.bit_per_cell;
            cpda /= shr_nand_info.bit_per_cell;
            ce = cpda % shr_nand_info.geo.nr_targets;
        	cpda /= shr_nand_info.geo.nr_targets;
        	lun = cpda & (shr_nand_info.geo.nr_luns - 1);
        	cpda >>= ctz(shr_nand_info.geo.nr_luns);
            wl = cpda % (shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);
        	block = cpda/(shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);

            page = (wl*shr_nand_info.bit_per_cell) + pg_in_wl;
            break;
        default:
#if (SPOR_ASSERT == mENABLE)
            panic("no such mode!\n");
#else
            ftl_apl_trace(LOG_PANIC, 0x8cc8, "no such mode!");
#endif
            break;
    }

	// ch, ce, lun, pl, blk, pg, du to PDA
	pda = block << shr_nand_info.pda_block_shift;
	pda |= page << shr_nand_info.pda_page_shift;
	pda |= lun << shr_nand_info.pda_lun_shift;
    pda |= ce << shr_nand_info.pda_ce_shift;
    pda |= ch << shr_nand_info.pda_ch_shift;
	pda |= plane << shr_nand_info.pda_plane_shift;
	pda |= du << shr_nand_info.pda_du_shift;

	return pda;
}

/*!
 * @brief ftl spor function
 * Translate CPDA info to PDA info
 *
 * @return	not used
 */
init_code pda_t ftl_pda2cpda(pda_t pda, u8 data_mode)
{
	pda_t cpda = 0;
	u32 ch, ce, lun, plane, block, pg_in_wl, du, wl, page;

    // Split each fields
    block = (pda >> shr_nand_info.pda_block_shift) & shr_nand_info.pda_block_mask;
    page  = (pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask;
	lun   = (pda >> shr_nand_info.pda_lun_shift) & (shr_nand_info.geo.nr_luns - 1);
    ce    = (pda >> shr_nand_info.pda_ce_shift) & (shr_nand_info.geo.nr_targets - 1);
    ch    = (pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.geo.nr_channels - 1);
	plane = (pda >> shr_nand_info.pda_plane_shift) & (shr_nand_info.geo.nr_planes - 1);
	du    = (pda >> shr_nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);

    pg_in_wl = page % shr_nand_info.bit_per_cell;
    wl       = page / shr_nand_info.bit_per_cell;

    // Construct CPDA
    switch(data_mode)
    {
        case V_MODE:
            // Construct CPDA
        	cpda = block;
        	cpda *= (shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);
        	cpda += wl;
            cpda *= shr_nand_info.geo.nr_luns;
        	cpda += lun;
            cpda *= shr_nand_info.geo.nr_targets;
        	cpda += ce;
        	cpda *= shr_nand_info.geo.nr_channels;
        	cpda += ch;
            cpda *= shr_nand_info.bit_per_cell;
        	cpda += pg_in_wl;
        	cpda *= shr_nand_info.geo.nr_planes;
        	cpda += plane;
        	cpda *= DU_CNT_PER_PAGE;
        	cpda += du;
            break;

        case H_MODE:
            cpda = block;
        	cpda *= (shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);
        	cpda += wl;
            cpda *= shr_nand_info.geo.nr_luns;
        	cpda += lun;
            cpda *= shr_nand_info.geo.nr_targets;
        	cpda += ce;
            cpda *= shr_nand_info.bit_per_cell;
        	cpda += pg_in_wl;
        	cpda *= shr_nand_info.geo.nr_planes;
        	cpda += plane;
            cpda *= shr_nand_info.geo.nr_channels;
        	cpda += ch;
        	cpda *= DU_CNT_PER_PAGE;
        	cpda += du;
            break;
        default:
#if (SPOR_ASSERT == mENABLE)
            panic("no such mode!\n");
#else
            ftl_apl_trace(LOG_PANIC, 0x3f92, "no such mode!");
#endif
            break;
    }

	return cpda;
}


/*!
 * @brief ftl spor function
 * Fill spb id into cpda structure
 *
 * @return	not used
 */
init_code pda_t ftl_blk2cpda(u16 spb_id)
{
    pda_t cpda;
	cpda  = spb_id;
    cpda *= (shr_nand_info.geo.nr_pages*shr_nand_info.geo.nr_luns*shr_nand_info.geo.nr_targets*\
             shr_nand_info.geo.nr_channels*shr_nand_info.geo.nr_planes*DU_CNT_PER_PAGE);

	return cpda;
}


/*!
 * @brief ftl spor function
 * Fill spb id into pda structure
 *
 * @return	not used
 */
init_code pda_t ftl_set_blk2pda(pda_t pda, u16 spb_id)
{
	pda &= ~(shr_nand_info.pda_block_mask << shr_nand_info.pda_block_shift);
    pda |= ((spb_id&shr_nand_info.pda_block_mask) << shr_nand_info.pda_block_shift);
	return pda;
}

/*!
 * @brief ftl spor function
 * Fill ch into pda structure
 *
 * @return	not used
 */
init_code pda_t ftl_set_ch2pda(pda_t pda, u8 ch)
{
    u32 ch_mask = shr_nand_info.geo.nr_channels-1;
	pda &= ~(ch_mask << shr_nand_info.pda_ch_shift);
    pda |= ((ch&ch_mask) << shr_nand_info.pda_ch_shift);
	return pda;
}

/*!
 * @brief ftl spor function
 * Fill pn into pda structure
 *
 * @return	not used
 */
init_code pda_t ftl_set_pn2pda(pda_t pda, u8 pn)
{
    u32 plane_mask = shr_nand_info.geo.nr_planes-1;
	pda &= ~(plane_mask << shr_nand_info.pda_plane_shift);
    pda |= ((pn&plane_mask) << shr_nand_info.pda_plane_shift);
	return pda;
}

/*!
 * @brief ftl spor function
 * Fill pg into pda structure
 *
 * @return	not used
 */
init_code pda_t ftl_set_pg2pda(pda_t pda, u16 pg)
{
	pda &= ~(shr_nand_info.pda_page_mask << shr_nand_info.pda_page_shift);
    pda |= ((pg&shr_nand_info.pda_page_mask) << shr_nand_info.pda_page_shift);
	return pda;
}

/*!
 * @brief ftl spor function
 * Fill die into pda structure
 *
 * @return	not used
 */
init_code pda_t ftl_set_die2pda(pda_t pda, u16 die)
{
	pda    &= ~(shr_nand_info.pda_lun_mask << shr_nand_info.pda_ch_shift);
    pda    |= ((die&shr_nand_info.pda_lun_mask) << shr_nand_info.pda_ch_shift);
	return pda;
}


/*!
 * @brief ftl spor function
 * Fill du into pda structure
 *
 * @return	not used
 */
init_code pda_t ftl_set_du2pda(pda_t pda, u8 du)
{
	u32 du_mask = DU_CNT_PER_PAGE-1;
	pda &= ~(du_mask << shr_nand_info.pda_du_shift);
    pda |= ((du&du_mask) << shr_nand_info.pda_du_shift);
	return pda;
}


/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2ch(pda_t pda)
{
	return (pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.geo.nr_channels - 1);
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2ce(pda_t pda)
{
	return (pda >> shr_nand_info.pda_ce_shift) & (shr_nand_info.geo.nr_targets - 1);
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2lun(pda_t pda)
{
	return (pda >> shr_nand_info.pda_lun_shift) & (shr_nand_info.geo.nr_luns - 1);
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2plane(pda_t pda)
{
	u32 plane = (pda >> shr_nand_info.pda_plane_shift) & (shr_nand_info.geo.nr_planes - 1);
	return plane;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2page(pda_t pda)
{
	return (pda >> shr_nand_info.pda_page_shift) & (shr_nand_info.pda_page_mask);
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2du(pda_t pda)
{
	return (pda >> shr_nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2die(pda_t pda)
{
    return (pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.lun_num - 1);
}


/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
slow_code u32 ftl_pda2wl(pda_t pda)
{
	u32 wl = ((pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)/shr_nand_info.bit_per_cell;
	return wl;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_pda2grp(pda_t pda)
{
	u32 grp = ((pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)/shr_nand_info.bit_per_cell/shr_wl_per_p2l;
	return grp;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u16 ftl_spor_get_head_from_pool(u8 pool)
{
    return ftl_spor_blk_pool->head[pool];
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u16 ftl_spor_get_tail_from_pool(u8 pool)
{
    return ftl_spor_blk_pool->tail[pool];
}


/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u16 ftl_spor_get_blk_cnt_in_pool(u8 pool)
{
    return ftl_spor_blk_pool->blkCnt[pool];
}

init_code void ftl_reset_pbt_seg_info(void)
{
	last_pbt_blk = INV_U16;
	last_pbt_seg = 0;
}
init_code spb_id_t ftl_get_last_pbt_blk(void)
{
	return last_pbt_blk;
}
init_code u16 ftl_get_last_pbt_seg(void)
{
	return last_pbt_seg;
}



/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u16 ftl_spor_get_next_blk(u16 blk)
{
    return ftl_spor_blk_node[blk].nxt_blk;
}

init_code u16 ftl_spor_get_pre_blk(u16 blk)
{
    return ftl_spor_blk_node[blk].pre_blk;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_spor_blk_sn_sync(void)
{
	u16 spb_id;

	ftl_apl_trace(LOG_ALW, 0xdb45, "[IN] ftl_spor_blk_sn_sync");
	for(spb_id=0;spb_id<shr_nand_info.geo.nr_blocks;spb_id++)
    {
        spb_set_sn(spb_id, ftl_init_sn_tbl[spb_id]);
    }
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_spor_spb_desc_sync(void)
{
	ftl_apl_trace(LOG_ALW, 0x4757, "[IN] ftl_spor_spb_desc_sync");

	if(ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P || blk_list_need_check_flag)
	{
		//set nsid
		u16 loop = 0;
		u16 user_cnt = spb_get_free_cnt(SPB_POOL_USER);
		u16 user_idx = spb_mgr_get_head(SPB_POOL_USER);

		for(loop=0; loop<user_cnt; loop++)
		{
			spb_get_desc(user_idx)->ns_id = FTL_NS_ID_START;
			user_idx = spb_info_get(user_idx)->block;
		}

		u16 qbt_cnt = spb_get_free_cnt(SPB_POOL_QBT_ALLOC);
		u16 qbt_idx = spb_mgr_get_head(SPB_POOL_QBT_ALLOC);
		for(loop=0; loop<qbt_cnt; loop++)
		{
			spb_get_desc(qbt_idx)->ns_id = FTL_NS_ID_INTERNAL;
			qbt_idx = spb_info_get(qbt_idx)->block;
		}

		//set flag
		u16 blk_idx = 0;
		for(blk_idx=0; blk_idx<shr_nand_info.geo.nr_blocks; blk_idx++)
		{
			if((ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_PLP_USER) || (ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_PLP_GC))
			{
				if(ftl_spor_info->plpUSREndGrp == shr_p2l_grp_cnt){
					spb_set_flag(blk_idx, SPB_DESC_F_CLOSED);
				}
				else{
            		spb_set_flag(blk_idx, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE));
				}
			}

			if((ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_OPEN_USER) || (ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_OPEN_GC))
	        {
	            spb_set_flag(blk_idx, SPB_DESC_F_OPEN);
	        }

			if((ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_USER) || (ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_GC))
			{
				spb_set_flag(blk_idx, (SPB_DESC_F_BUSY|SPB_DESC_F_CLOSED));
			}

		}

	}
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_spor_set_blk_sn(u16 blk, u32 sn)
{
	ftl_init_sn_tbl[blk] = sn;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u32 ftl_spor_get_blk_sn(u16 blk)
{
	return ftl_init_sn_tbl[blk];
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_set_1bit_data_error_flag(u8 flag)
{
    ftl_spor_info->ftl_1bit_fail_need_fake_qbt = flag;
}
/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_get_1bit_data_error_flag(void)
{
    return ftl_spor_info->ftl_1bit_fail_need_fake_qbt;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_set_blist_data_error_flag(u8 flag)
{
    ftl_spor_info->blist_data_error = flag;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_get_blist_data_error_flag(void)
{
    return ftl_spor_info->blist_data_error;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_set_l2p_data_error_flag(u8 flag)
{
    ftl_spor_info->l2p_data_error = flag;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_get_l2p_data_error_flag(void)
{
    return ftl_spor_info->l2p_data_error;
}


/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_set_read_data_fail_flag(u8 flag)
{
    ftl_spor_info->read_data_fail  = flag;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_get_read_data_fail_flag(void)
{
    return ftl_spor_info->read_data_fail ;
}
init_code void ftl_set_qbt_fail_flag(void)
{
    ftl_spor_info->existQBT = mFALSE;
	ftl_spor_info->qbterr = mTRUE;
}

#if (SPOR_VAC_CMP == mENABLE)
init_code void ftl_vac_buffer_copy(void)
{
	u16    spb_id;
    u32    *vc, cnt;
    dtag_t dtag;
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);

    ftl_apl_trace(LOG_ALW, 0x1f68, "[IN] ftl_vac_buffer_copy");
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    for(spb_id = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; spb_id < shr_nand_info.geo.nr_blocks- tcg_reserved_spb_cnt; spb_id++)
#else
    for(spb_id = 1; spb_id < shr_nand_info.geo.nr_blocks - tcg_reserved_spb_cnt; spb_id++)
#endif
    {
        ftl_init_vc_tbl[spb_id] = vc[spb_id];
    }

    ftl_l2p_put_vcnt_buf(dtag, cnt, false);
}

init_code void ftl_vac_compare(void)
{
	u16    spb_id;
    u32    *vc, cnt;
    dtag_t dtag;
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
	spb_id_t error_blk = 0;
    ftl_apl_trace(LOG_ALW, 0x884f, "[IN] ftl_vac_compare");
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    for(spb_id = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; spb_id < shr_nand_info.geo.nr_blocks- tcg_reserved_spb_cnt; spb_id++)
#else
    for(spb_id = 1; spb_id < shr_nand_info.geo.nr_blocks - tcg_reserved_spb_cnt; spb_id++)
#endif
    {
        if(ftl_init_vc_tbl[spb_id] != vc[spb_id])
        {
            ftl_apl_trace(LOG_ALW, 0xfd61, "spb_id : %d, blist vac : 0x%x, rebuilt vac : 0x%x",\
                          spb_id, ftl_init_vc_tbl[spb_id], vc[spb_id]);
           error_blk = spb_id;
        }
    }
	if(error_blk)
	{
		disable_ltssm(1);
		sys_assert(0);
	}
	else
	{
		ftl_apl_trace(LOG_ALW, 0x293b, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<OK!!>>>>>>>>>>>>>>>>>>>>>>>>>");
	}
    ftl_l2p_put_vcnt_buf(dtag, cnt, false);
}
#endif


/*!
 * @brief ftl spor function
 * Sorting block after scanning aux
 *
 * @return	not used
 */

init_code void ftl_spor_pop_blk_from_pool(u16 blk, u8 pool)
{
    u16 prevSBlk = INV_U16;
    u16 currSBlk = INV_U16;

#if (SPOR_ASSERT == mENABLE)
    sys_assert(pool < SPB_POOL_MAX);
#else
    if(pool >= SPB_POOL_MAX || blk == INV_U16)
    {
        ftl_apl_trace(LOG_ALW, 0x0f5a, "pool >= SPB_POOL_MAX! blk:%d",blk);
        return;
    }
#endif

    if(ftl_spor_blk_node[blk].pool_id != pool)
    {
        ftl_apl_trace(LOG_ALW, 0x0ac5, "Cannot find block : %d in pool 0x%x!", blk, pool);
        return;
    }

    if(ftl_spor_blk_pool->blkCnt[pool])
    {
        if(blk == ftl_spor_blk_pool->head[pool] && blk == ftl_spor_blk_pool->tail[pool])
        {
            ftl_spor_blk_pool->head[pool] = ftl_spor_blk_pool->tail[pool] = INV_U16;
        }
        else if(blk == ftl_spor_blk_pool->head[pool])
        {
            ftl_spor_blk_pool->head[pool] = ftl_spor_blk_node[blk].nxt_blk;
        }
        else
        {
            prevSBlk = ftl_spor_blk_pool->head[pool];
            currSBlk = ftl_spor_blk_node[prevSBlk].nxt_blk;
            while((currSBlk != blk)&&(currSBlk != INV_U16))    // Still NEED search from the head of pool
            {
                if(currSBlk == ftl_spor_blk_pool->tail[pool])    // SBlk NOT Find
                {
                    ftl_apl_trace(LOG_ALW, 0x142e, "Cannot find block : 0x%x in pool 0x%x!", blk, pool);
                    //sys_assert(0);
                    return;
                }
                prevSBlk = currSBlk;
                currSBlk = ftl_spor_blk_node[currSBlk].nxt_blk;
            }

            if(currSBlk == INV_U16)
            {
                return;
            }

            ftl_spor_blk_node[prevSBlk].nxt_blk = ftl_spor_blk_node[currSBlk].nxt_blk;

            if(blk == ftl_spor_blk_pool->tail[pool])
            {
                ftl_spor_blk_pool->tail[pool] = prevSBlk;
            }
        }

        ftl_spor_blk_node[blk].nxt_blk = INV_U16;
        ftl_spor_blk_node[blk].pool_id = SPB_POOL_MAX;
        ftl_spor_blk_pool->blkCnt[pool]--;
    }
}

/*!
 * @brief ftl spor function
 * Sorting block after scanning aux
 *
 * @return	not used
 */

init_code void ftl_spor_sort_blk_sn(u16 blk, u8 pool)
{

    u16 prevSBlk, currSBlk;
    u32 comSN, curSN;

    if(ftl_spor_blk_node[blk].pool_id == pool)
    {
        return;
    }

    if(ftl_spor_blk_pool->blkCnt[pool] == 0)
    {
        ftl_spor_blk_pool->head[pool] = ftl_spor_blk_pool->tail[pool] = blk;
        ftl_spor_blk_node[blk].nxt_blk = ftl_spor_blk_node[blk].pre_blk = INV_U16;
    }
    else
    {
        prevSBlk = ftl_spor_blk_pool->head[pool];   // Sort from Head
        currSBlk = ftl_spor_blk_node[prevSBlk].nxt_blk;
        comSN    = ftl_spor_get_blk_sn(blk);

        if(comSN < ftl_spor_get_blk_sn(prevSBlk))   // Check Head SBlk's SN. if  so, add SBlk from Head
        {
            ftl_spor_blk_node[blk].nxt_blk = prevSBlk;
			ftl_spor_blk_node[blk].pre_blk = INV_U16;
			ftl_spor_blk_node[prevSBlk].pre_blk = blk;
            ftl_spor_blk_pool->head[pool] = blk;
        }
        else
        {
            // Search insert point
            while(1)
            {
                if(currSBlk == INV_U16) //last block
                {
                    break;
                }
                curSN = ftl_spor_get_blk_sn(currSBlk);
                if(comSN < curSN)
                {
                    break;
                }
                else if (currSBlk == ftl_spor_blk_pool->tail[pool])
                {
                    prevSBlk = ftl_spor_blk_pool->tail[pool];
                    break;
                }
                prevSBlk = currSBlk;
                currSBlk = ftl_spor_blk_node[currSBlk].nxt_blk;

            } // end while(1)

            // Insert SBlk
            if(prevSBlk == ftl_spor_blk_pool->tail[pool])  // Push from tail
            {
                ftl_spor_blk_node[ftl_spor_blk_pool->tail[pool]].nxt_blk = blk;
                ftl_spor_blk_node[blk].nxt_blk = INV_U16;
                ftl_spor_blk_node[blk].pre_blk = ftl_spor_blk_pool->tail[pool];
                ftl_spor_blk_pool->tail[pool] = blk;
            }
            else    // Insert point in middle
            {
                ftl_spor_blk_node[prevSBlk].nxt_blk = blk;
                ftl_spor_blk_node[blk].nxt_blk      = currSBlk;
                ftl_spor_blk_node[blk].pre_blk      = prevSBlk;
				if(currSBlk != INV_U16)
				{
					ftl_spor_blk_node[currSBlk].pre_blk  = blk;
				}
            }
        }
    }

    ftl_spor_blk_pool->blkCnt[pool]++;
    ftl_spor_blk_node[blk].pool_id = pool;
}


/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_is_host_data_exist(void)
{
    return ftl_spor_info->existHostData;
}


init_code u8 ftl_is_epm_vac_error(void)
{
    return ftl_spor_info->epm_vac_error;
}

init_code u8 ftl_is_spor_user_build(void)
{
    if(ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P)
    {
        return mTRUE;
    }

    return mFALSE;
}

init_code bool get_skip_spor_build(void)
{
	return skip_spor_build;
}

ddr_code void spor_clean_tag(void)
{
	skip_spor_build = false;
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)	
	need_trigger_slc_gc = false;
#endif
#if (PLP_SUPPORT == 0)	
	need_trigger_non_plp_gc = false;
#endif

}

//slc buffer / no plp 
init_code bool is_skip_spor_build(void)
{	
	skip_spor_build = false;
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)	
	//need_trigger_slc_gc = false;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	//3. if slc gc not done , and spor again , skip this spor build
	if(epm_ftl_data->plp_slc_gc_tag == FTL_PLP_SLC_NEED_GC_TAG &&	   //need GC
			ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P &&	   //QBT mode
			(ftl_spor_info->scan_sn > epm_ftl_data->plp_last_blk_sn || //QBT sn > slc max sn------this is fqbt
			epm_ftl_data->plp_last_blk_sn == INV_U32))    			   //slc max sn == INV_U32 ---- POR case + no host blk + slc write
	{
		ftl_apl_trace(LOG_ALW, 0x24e9, "[SLC] FQBT done,but gc no done skip spor build,qbt_sn:0x%x > blk_sn:0x%x",ftl_spor_info->scan_sn,epm_ftl_data->plp_last_blk_sn);
		//need_trigger_slc_gc = true;
		skip_spor_build = true;
		return mTRUE;
	}
#endif
#if (PLP_SUPPORT == 0)	
	need_trigger_non_plp_gc = false;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	//   if non-plp gc not done , and spor again , skip this spor build
	if(epm_ftl_data->non_plp_gc_tag == FTL_NON_PLP_NEED_GC_TAG &&	   //need GC
			ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P &&	   //QBT mode
			ftl_spor_info->scan_sn > epm_ftl_data->non_plp_last_blk_sn)    //QBT sn > slc max sn------this is fqbt
	{
		ftl_apl_trace(LOG_ALW, 0x510a, "[NONPLP] FQBT done,but gc no done skip spor build,qbt_sn:0x%x > blk_sn:0x%x",ftl_spor_info->scan_sn,epm_ftl_data->non_plp_last_blk_sn);
		need_trigger_non_plp_gc = true;
		skip_spor_build = true;
		return mTRUE;
	}
#endif

	return mFALSE;
}




#if (SPOR_QBT_MAX_ONLY == mENABLE)
init_code u8 ftl_is_qbt_blk_max(void)
{
    if((ftl_spor_info->existQBT == mTRUE) &&
       (ftl_spor_info->maxQBTSn > ftl_spor_info->maxHostDataBlkSn))
    {
        return mTRUE;
    }

    return mFALSE;
}
#endif

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
init_code bool ftl_get_trigger_slc_gc(void)
{
	return need_trigger_slc_gc;
}

init_code void ftl_set_trigger_slc_gc(bool type)
{
	need_trigger_slc_gc = type;
}


init_code void ftl_clean_slc_blk(void)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_sblk_erase(PLP_SLC_BUFFER_BLK_ID);
	epm_ftl_data->plp_pre_slc_wl = 0;
    epm_ftl_data->plp_slc_wl     = 0;
    //epm_ftl_data->plp_slc_times  = 0;
    epm_ftl_data->plp_slc_gc_tag = 0;    
    //epm_ftl_data->plp_slc_disable = 0;
    epm_ftl_data->slc_format_tag = 0;
    epm_ftl_data->slc_eraseing_tag = 0;
}



init_code u8 ftl_chk_slc_buffer_done(void)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_apl_trace(LOG_ALW, 0xd694, "[IN] ftl_chk_slc_buffer_done");

	//0. essr err , lock slc block
	if(epm_ftl_data->esr_lock_slc_block == true)
	{
		return mFALSE;
	}
	//1. if epm err , don't use slc buffer 
	if((epm_ftl_data->epm_sign != FTL_sign) || (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
	{
		if(epm_ftl_data->spor_tag == 0)//first power on
		{
			//not need disable slc buffer
			return mFALSE;
		}
		epm_ftl_data->plp_slc_disable = true;
		epm_ftl_data->plp_slc_gc_tag  = 0;//avoid fail
	 	ftl_apl_trace(LOG_ALW, 0x9f53, "[SLC] EPM ERR , disable slc buffer sign:%d tag:0x%x",epm_ftl_data->epm_sign,epm_ftl_data->spor_tag);
		#if (IGNORE_PLP_NOT_DONE == mENABLE)
		epm_ftl_data->plp_slc_disable = false;
		ftl_clean_slc_blk();
		#endif
		
		return mTRUE;
	}

	//2. if slc format flag not clear , need continue erase slc blk
	if(epm_ftl_data->slc_format_tag == SLC_ERASE_FORMAT_TAG)
	{
		//erase slc blk , update slc epm info
		ftl_clean_slc_blk();		
	 	ftl_apl_trace(LOG_ALW, 0x3e32, "[SLC] format not done , need erase");
		return mTRUE;
	}

	//3. chk slc buffer erase success(may abort by plp)
	if(epm_ftl_data->slc_eraseing_tag)
	{
		//slc erase done , but plp save epm fail(may no init done)
		ftl_clean_slc_blk();		
	 	ftl_apl_trace(LOG_ALW, 0xfb96, "[SLC] slc_erase_blk fail ,possible interruption");
		return mTRUE;
	}
	/*
	//3. if slc gc not done , and spor again , skip this spor build
	if(epm_ftl_data->plp_slc_gc_tag == FTL_PLP_SLC_NEED_GC_TAG && 	   //need GC
	   		ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P &&      //QBT mode
	   		ftl_spor_info->scan_sn > epm_ftl_data->plp_last_blk_sn)    //QBT sn > slc max sn------this is fqbt
	{
	 	ftl_apl_trace(LOG_ALW, 0, "[SLC] FQBT done,but gc no done skip spor build,qbt_sn:0x%x > blk_sn:0x%x",ftl_spor_info->scan_sn,epm_ftl_data->plp_last_blk_sn);
	 	need_trigger_slc_gc = true;
	 	skip_spor_build = true;
	 	return mFALSE;
	}
	*/
	 return mTRUE;

}

init_code void ftl_calculate_slc_build_range(void)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	pda_t pda_start = 0 , pda_end = 0;
	pda_start = ftl_set_blk2pda(pda_start,PLP_SLC_BUFFER_BLK_ID);
	pda_start = ftl_set_pg2pda(pda_start,epm_ftl_data->plp_pre_slc_wl);
	pda_end   = ftl_set_blk2pda(pda_end,PLP_SLC_BUFFER_BLK_ID);
	pda_end   = ftl_set_pg2pda(pda_end,epm_ftl_data->plp_slc_wl);
	pda_end--;

	//shr_slc_flush_state   = SLC_FLOW_FQBT_FLUSH;
	shr_plp_slc_start_pda = pda_start;
	shr_plp_slc_end_pda   = pda_end;
	shr_plp_slc_need_gc   = FTL_PLP_SLC_NEED_GC_TAG;  
	ftl_apl_trace(LOG_ALW, 0x90d6, "[SLC]gc info-> pda_s:0x%x pda_e:0x%x s_w:%d n_b_wl:%d",
		pda_start,pda_end,epm_ftl_data->plp_pre_slc_wl,epm_ftl_data->plp_slc_wl);
}

#endif




/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_need_p2l_rebuild(void)
{
    u8 spor_rebuild_flag = mFALSE;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
#endif
    // L2P/Blist read fail, change to FTL_USR_BUILD_L2P mode
    if(ftl_spor_info->l2p_data_error || ftl_spor_info->blist_data_error)
    {
        ftl_apl_trace(LOG_ALW, 0x418b, "L2P/Blist read fail, swith to FTL_USR_BUILD_L2P mode");
        ftl_spor_info->build_l2P_mode = FTL_USR_BUILD_L2P;
        ftl_spor_info->scan_sn = 0;
        spor_rebuild_flag = mTRUE;
    }

    if(ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P)
    {
        spor_rebuild_flag = mTRUE;
    }
    else if(ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P)
    {
        // Host block SN larger than QBT block SN
        if(ftl_spor_info->maxHostDataBlkSn > ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC)))
        {

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
            if((ftl_spor_info->load_blist_mode == FTL_QBT_LOAD_BLIST) && (epm_ftl_data->epm_vc_table[ftl_spor_info->maxHostDataBlk] == 0) && (ftl_spor_info->maxQBTSn+1 == ftl_spor_info->maxHostDataBlkSn))
#else
            //if((ftl_spor_info->load_blist_mode == FTL_QBT_LOAD_BLIST) && (ftl_spor_info->maxQBTSn+1 == ftl_spor_info->maxHostDataBlkSn))
			if(0)//nonplp can't judge max host blk invalid
#endif
        	{
				spor_rebuild_flag = mFALSE;
			}
			else
			{
				spor_rebuild_flag = mTRUE;
			}
        }
    }
    else if(ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P)
    {
        // ToDo : May need to add PBT force close case
        spor_rebuild_flag = mTRUE;
    }

    // need enter spor flow
    if(spor_rebuild_flag)
    {
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
        epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
        if((epm_ftl_data->epm_sign != FTL_sign) || (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
        {
			// vac table on epm is corrupted, check if need qbt/pbt build
			if ((ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P) || (ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P)) {
				// we can make sure blklist and l2p is ok at this point
				ftl_apl_trace(LOG_ALW, 0x642d, "EPM VAC read fail, swith to FTL_QBT/PBT_BUILD_L2P mode");
				ftl_spor_info->epm_vac_error = mTRUE;
			} else {
				sys_assert(ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P);
				ftl_apl_trace(LOG_ALW, 0xec2e, "EPM VAC read fail, swith to FTL_USR_BUILD_L2P mode");
				ftl_spor_info->scan_sn = 0;
				ftl_spor_info->epm_vac_error = mTRUE;
			}
        }
#endif
        return mTRUE;
    }

    return mFALSE;
}


init_code u16 ftl_choose_blist_blk()
{
	extern u16 ftl_get_spor_p2l_blk_head(u32 sn, u8 pool, u8 get_pre);
	//ftl_spor_info->secondDataBlk
	u16 usr_blk = INV_U16,gc_blk = INV_U16,cur_blk = INV_U16;
	usr_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->secondDataBlkSn, SPB_POOL_USER, mTRUE);
	gc_blk  = ftl_get_spor_p2l_blk_head(ftl_spor_info->secondDataBlkSn, SPB_POOL_GC, mTRUE);

	ftl_apl_trace(LOG_ALW, 0x2f7e,"Blist ---------- usr:%d gc:%d",usr_blk,gc_blk);

	if(usr_blk != INV_U16 && gc_blk != INV_U16)
	{
		if(ftl_init_sn_tbl[usr_blk] > ftl_init_sn_tbl[gc_blk])
			cur_blk = usr_blk;
		else
			cur_blk = gc_blk;

		return  cur_blk;
	}
	else if(usr_blk == INV_U16 && gc_blk != INV_U16)
	{
		return gc_blk;
	}
	else if(usr_blk != INV_U16 && gc_blk == INV_U16)
	{
		return usr_blk;
	}
	else
	{
		return INV_U16;
	}
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_spor_build_l2p_mode(void)
{
    ftl_apl_trace(LOG_ALW, 0x35e1, "QBT Exist : %d, PBT Exist : %d", ftl_spor_info->existQBT, ftl_spor_info->existPBT);

    ftl_apl_trace(LOG_ALW, 0x7970, "QBT Head idx : 0x%x, QBT Head SN : 0x%x ; PBT Head idx : 0x%x, PBT Head SN : 0x%x",\
           ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC),\
           ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC)),\
           ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC),\
           ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC)));


    // Currently only use QBT/USR L2P mode, by Sunny
#if (SPOR_FLOW_PARTIAL == mENABLE)
    if(ftl_spor_info->existQBT == mTRUE)
    {
        ftl_spor_info->build_l2P_mode = FTL_QBT_BUILD_L2P;
    }
    else
    {
        ftl_spor_info->build_l2P_mode = FTL_USR_BUILD_L2P;
    }
#else
    if((ftl_spor_info->existQBT == mTRUE)&&(ftl_spor_info->existPBT == mTRUE))
    {
        if(ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC)) >
           ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC)))
        {
            ftl_spor_info->build_l2P_mode = FTL_PBT_BUILD_L2P;
        }
        else
        {
            ftl_spor_info->build_l2P_mode = FTL_QBT_BUILD_L2P;
        }
    }
    else if(ftl_spor_info->existQBT == mTRUE)
    {
        ftl_spor_info->build_l2P_mode = FTL_QBT_BUILD_L2P;
    }
    else if(ftl_spor_info->existPBT == mTRUE)
    {
        ftl_spor_info->build_l2P_mode = FTL_PBT_BUILD_L2P;
    }
    else
    {
        ftl_spor_info->build_l2P_mode = FTL_USR_BUILD_L2P;
    }
#endif

    switch(ftl_spor_info->build_l2P_mode)
    {
        case FTL_QBT_BUILD_L2P:
            ftl_spor_info->scan_sn = ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC));
            ftl_apl_trace(LOG_ALW, 0xb020, "Load Build L2P mode [QBT]");
            break;

        case FTL_PBT_BUILD_L2P:
            ftl_spor_info->scan_sn = ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC));
            ftl_apl_trace(LOG_ALW, 0x2acd, "Load Build L2P mode [PBT]");
            break;

        case FTL_USR_BUILD_L2P:
            ftl_spor_info->scan_sn = 0;
            ftl_apl_trace(LOG_ALW, 0x9539, "Load Build L2P mode [USR]");
            break;
        default:
#if (SPOR_ASSERT == mENABLE)
            panic("no such build l2p mode!\n");
#else
            ftl_apl_trace(LOG_PANIC, 0xeefc, "no such build l2p mode!");
#endif
            break;
    }

    ftl_apl_trace(LOG_ALW, 0x33f0, "L2P scan_SN : 0x%x", ftl_spor_info->scan_sn);
	return ftl_spor_info->build_l2P_mode;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */

init_code u8 ftl_spor_load_blist_mode(u16 *blk_id, u8 fail_cnt)
{
    u8 mode = INV_U8;

	if(fail_cnt == 0)
    {
		if((ftl_spor_info->existQBT == mTRUE) &&
		   ((ftl_spor_info->maxQBTSn > ftl_spor_info->maxHostDataBlkSn) || get_skip_spor_build()) )
    	{
    		//if get_skip_spor_build() is true , don't use max usr blk load blist
			ftl_apl_trace(LOG_ALW, 0x0d7d, "Load BList mode [QBT] , skip_flag:%d",get_skip_spor_build());
			ftl_spor_info->load_blist_mode = FTL_QBT_LOAD_BLIST;
        	mode    = FTL_QBT_LOAD_BLIST;
        	*blk_id = ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC);
    	}
	    else
	    {
			// read max close blk's blist
			ftl_apl_trace(LOG_ALW, 0x07e9, "Load BList mode [USR/GC] blk:%d",ftl_spor_info->maxHostDataBlk);
			ftl_spor_info->load_blist_mode = FTL_USR_LOAD_BLIST;
			mode	= FTL_USR_LOAD_BLIST;
			*blk_id = ftl_spor_info->maxHostDataBlk;
	    }
	}
#if (PLP_SUPPORT == 1)
	else if(fail_cnt == 1)
	{
		if((ftl_spor_info->existQBT == mTRUE) && (ftl_spor_info->maxQBTSn > ftl_spor_info->secondDataBlkSn))
	    {
	        ftl_apl_trace(LOG_ALW, 0x8d63, "Reload BList mode [QBT]");
			ftl_spor_info->load_blist_mode = FTL_QBT_LOAD_BLIST;
        	mode    = FTL_QBT_LOAD_BLIST;
        	*blk_id = ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC);
	    }
		else
		{
			mode = FTL_LOAD_BLKIST_FAIL;
		}
	}
#else//nonplp blist may read fail,switch pre blk
	else if(fail_cnt == 1 || fail_cnt == 2)
	{
		/*	fail cnt = 1 --- max host blk read blist fail
		  	fail cnt = 2 --- second host blk read fail 
		*/
		u8 qbt_sn = 0;
		if((ftl_spor_info->existQBT == mTRUE))
			qbt_sn = ftl_spor_info->maxQBTSn;

		if(fail_cnt == 1)//read second blk
		{
			if(qbt_sn > ftl_spor_info->secondDataBlkSn)//use QBT
			{
				//use qbt build blist
				ftl_apl_trace(LOG_ALW, 0x328e, "Reload BList mode [QBT] fail_cnt:%d usr_blk:%d",fail_cnt,ftl_spor_info->secondDataBlk);
				ftl_spor_info->load_blist_mode = FTL_QBT_LOAD_BLIST;
	        	mode    = FTL_QBT_LOAD_BLIST;
	        	*blk_id = ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC);
			}
			else if(ftl_spor_info->secondDataBlk != INV_U16)//use second blk
			{
				// read second  blk blist
				ftl_apl_trace(LOG_ALW, 0x1fd9, "Load BList mode [USR/GC] fail 1 ,blk:%d",ftl_spor_info->secondDataBlk);
				ftl_spor_info->load_blist_mode = FTL_USR_LOAD_BLIST;
				mode	= FTL_USR_LOAD_BLIST;
				*blk_id = ftl_spor_info->secondDataBlk;
				blk_list_need_check_flag = true;
			}
			else //ftl_spor_info->secondDataBlkSn == 0 qbt sn == 0, return read blist fail
			{
				mode = FTL_LOAD_BLKIST_FAIL;
			}
		}
		else if(fail_cnt == 2)//read second blk fail
		{
			u16 new_blk = ftl_choose_blist_blk();
			u32 new_sn  = 0;
			if(new_blk != INV_U16)
				new_sn = ftl_init_sn_tbl[new_blk];
			if(qbt_sn > new_sn)//use QBT
			{
				//use qbt build blist
				ftl_apl_trace(LOG_ALW, 0x6841, "Reload BList mode [QBT] fail_cnt:%d usr_blk:%d",fail_cnt,new_blk);
				ftl_spor_info->load_blist_mode = FTL_QBT_LOAD_BLIST;
	        	mode    = FTL_QBT_LOAD_BLIST;
	        	*blk_id = ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC);
			}
			else if(new_blk != INV_U16)//use new blk
			{
				// read second  blk blist
				ftl_apl_trace(LOG_ALW, 0xd98b, "Load BList mode [USR/GC] fail 2 ,blk:%d",new_blk);
				ftl_spor_info->load_blist_mode = FTL_USR_LOAD_BLIST;
				mode	= FTL_USR_LOAD_BLIST;
				*blk_id = new_blk;
				blk_list_need_check_flag = true;
			}
			else //new blk sn == 0 qbt sn == 0, return read blist fail
			{
				mode = FTL_LOAD_BLKIST_FAIL;
			}
		}
	}
#endif
	else
	{
		mode = FTL_LOAD_BLKIST_FAIL;
	}

    ftl_apl_trace(LOG_ALW, 0x4663, "USR/GC : %d, SN : 0x%x ; QBT : %d, SN : 0x%x",\
                  ftl_spor_info->maxHostDataBlk, ftl_spor_info->maxHostDataBlkSn,\
                  ftl_spor_info->maxQBTBlk, ftl_spor_get_blk_sn(ftl_spor_info->maxQBTBlk));

	return mode;
}
extern u8 host_sec_bitz;

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_set_page_meta(u32 meta_idx, u8 ofst, u32 lda, u16 spb_id, u8 spb_type)
{
    u32 idx;
    u64 pg_sn;

    if(lda == SPOR_DUMMY_LDA) {pg_sn = INV_U64;}
    else                      {pg_sn = gFtlMgr.GlobalPageSN;}

    idx = (meta_idx-ftl_spor_meta_idx)+ofst;
    switch (ofst%DU_CNT_PER_PAGE)
	{
		case 0:
            ftl_spor_meta[idx].fmt1.page_sn_L = (pg_sn&0xFFFFFF);
            ftl_spor_meta[idx].fmt1.WUNC      = 0;
			break;
		case 1:
			ftl_spor_meta[idx].fmt2.page_sn_H = (pg_sn>>24)&0xFFFFFF;
            ftl_spor_meta[idx].fmt2.WUNC      = 0;
			break;
		case 2:
			ftl_spor_meta[idx].fmt3.blk_sn_L = (spb_get_sn(spb_id)&0xFFFF);
            ftl_spor_meta[idx].fmt3.blk_type = spb_type;
            ftl_spor_meta[idx].fmt3.WUNC     = 0;
			break;
		case 3:
			ftl_spor_meta[idx].fmt4.blk_sn_H = (spb_get_sn(spb_id)>>16)&0xFFFF;
            ftl_spor_meta[idx].fmt4.Debug    = 0xEA;
            ftl_spor_meta[idx].fmt4.WUNC     = 0;
			break;
        default:
#if (SPOR_ASSERT == mENABLE)
            panic("meta !=0~3\n");
#else
            ftl_apl_trace(LOG_PANIC, 0x3b5f, "meta !=0~3");
#endif
            break;
	}


    //ftl_spor_meta[idx].hlba = LDA_TO_LBA(lda);
    ftl_spor_meta[idx].hlba = (((u64)lda) << (LDA_SIZE_SHIFT - host_sec_bitz));
    ftl_spor_meta[idx].lda = lda;


    #if 0 // for dbg
    ftl_apl_trace(LOG_ALW, 0xd5b5, "LDA : 0x%x, FMT : 0x%x, HLBA : 0x%x",\
                  ftl_spor_meta[idx].lda, ftl_spor_meta[idx].fmt, ftl_spor_meta[idx].hlba);
    #endif
}


/*!
 * @brief ftl spor function
 * To check bad block are at which position
 *
 * @return	not used
 */
#ifdef SKIP_MODE
init_code u8 ftl_good_blk_in_pn_detect(u16 spb_id, u8 pn_ptr)
{
    u8 *df_ptr;

    df_ptr = ftl_get_spb_defect(spb_id);
    return ftl_get_defect_pl_pair(df_ptr, pn_ptr);
}
#endif
/*!
 * @brief ftl spor function
 * To determine if current pda is for P2L table
 *
 * @return	not used
 */
init_code void ftl_p2l_position_detect(u32 grp_id, u32 spb_id, u32 nsid, pda_t* p2l_pda, pda_t* pgsn_pda)
{
    u32 p2l_page = (grp_id + 1) * shr_nand_info.bit_per_cell * shr_wl_per_p2l;
    u32 p2l_die;
    u32 parity_die = shr_nand_info.lun_num - 1;
    bool last_p2l = false;
    u8 p2l_plane = 0;
    u8 pgsn_plane = 0;
    u32 pgsn_page;

#ifdef SKIP_MODE
		bool find_p2l_plane = false;
#else
		u32 plane_info;
#endif

    if ((grp_id + 1) * shr_nand_info.bit_per_cell * shr_wl_per_p2l >= get_page_per_block()) {
        p2l_die = shr_nand_info.lun_num - 1;
        p2l_page = get_page_per_block() - 1;
        last_p2l = true;
    } else
    {
        p2l_die = grp_id & ((shr_nand_info.lun_num >> 1) - 1);
    }

#ifdef While_break
	u64 start = get_tsc_64();
#endif

#ifdef SKIP_MODE
		u8* df_ptr =  ftl_get_spb_defect(spb_id);
#endif

    if(fcns_raid_enabled(nsid)) {
        //calculate parity die
        while (1) {
#ifdef SKIP_MODE
			if (BLOCK_ALL_BAD != ftl_get_defect_pl_pair(df_ptr, parity_die*get_mp()))
#else
			if (1)
#endif

			{
                break;
            }
#if (SPOR_ASSERT == mENABLE)
            sys_assert(parity_die != 0);
#else
            if(parity_die == 0)
            {
                ftl_apl_trace(LOG_PANIC, 0xb230, "parity_die == 0");
            }
#endif
            parity_die--;

#ifdef While_break
			if(Chk_break(start,__FUNCTION__, __LINE__))
				break;
#endif
        }
    }

#ifdef While_break
start = get_tsc_64();
#endif

    while (1) //check bad block
    {
        if(fcns_raid_enabled(nsid)) {
            if (p2l_die == parity_die
                && (shr_nand_info.geo.nr_planes - pop32(ftl_get_defect_pl_pair(df_ptr, parity_die*get_mp())) == 1)) {
                if (last_p2l == false)
                    p2l_die = (p2l_die + 1) & ((shr_nand_info.lun_num >> 1) - 1);
                else
                    p2l_die--;
                continue;
            }
        }

#ifdef SKIP_MODE
		//u32 plane_info = ftl_get_defect_pl_pair(df_ptr, p2l_die*get_mp());
		u32 pl;
		u32 bad_pln_cnt = 0;
		u32 good_pln_array[2] = {0};
		u32 good_pln_cnt = 0;
		u32 max_good_pln_cnt = 2;

#if RAID_SUPPORT
        bool skip_parity_plane = false;
        if(p2l_die == parity_die){
            skip_parity_plane = true;
            if(shr_nand_info.geo.nr_planes - pop32(ftl_get_defect_pl_pair(df_ptr, parity_die*get_mp())) == 2){
                max_good_pln_cnt = 1;
            }
        }
#endif
		if (last_p2l == false){
			for (pl = 0; pl < shr_nand_info.geo.nr_planes; pl++){
				u32 iid = (p2l_die*shr_nand_info.geo.nr_planes) + pl;
				u32 idx = iid >> 3;
				u32 off = iid & 7;
				if (((df_ptr[idx] >> off)&1)==0){ //good plane
#if RAID_SUPPORT
                    if(skip_parity_plane){
                        skip_parity_plane = false;
                        continue;
                    }
#endif
					good_pln_array[good_pln_cnt++] = pl;
					find_p2l_plane = true;
					if(good_pln_cnt == max_good_pln_cnt)
						break;
				}
				else
					bad_pln_cnt++;
			}
		}
		else{
			for (pl = shr_nand_info.geo.nr_planes; pl > 0; pl--){
				u32 iid = (p2l_die*shr_nand_info.geo.nr_planes) + (pl - 1);
				u32 idx = iid >> 3;
				u32 off = iid & 7;
				if (((df_ptr[idx] >> off)&1)==0){ //good plane
					good_pln_array[good_pln_cnt++] = (pl - 1);
					find_p2l_plane = true;
					if(good_pln_cnt == max_good_pln_cnt)
						break;
				}
				else
					bad_pln_cnt++;
			}
		}

		if (good_pln_cnt == 2){
			if(last_p2l){
				p2l_plane = good_pln_array[1];
				pgsn_plane = good_pln_array[0];
			}else{
				p2l_plane = good_pln_array[0];
				pgsn_plane = good_pln_array[1];
			}
		}
		else if(good_pln_cnt == 1)
			p2l_plane = pgsn_plane = good_pln_array[0];


		if (find_p2l_plane)
			break;

		if (bad_pln_cnt == shr_nand_info.geo.nr_planes){
			if (last_p2l == false)
				p2l_die = (p2l_die+1) & ((shr_nand_info.lun_num >> 1) - 1);
			else
				p2l_die--;
		}

#else
		plane_info = BLOCK_NO_BAD;
		if (BLOCK_ALL_BAD != plane_info) {
			if (plane_info == BLOCK_NO_BAD) {
				p2l_plane = 0;
				pgsn_plane = 1;
			}else if (plane_info == BLOCK_P0_BAD) {
				p2l_plane = pgsn_plane = 1;
			} else {
				p2l_plane = pgsn_plane = 0;
			}
			if (shr_nand_info.geo.nr_planes == 4) {  // adams 4P last P2L
				if (last_p2l) {
						p2l_plane = 2;  // nand_plane_num() - 2
				pgsn_plane = 3; // nand_plane_num() - 2 + 1
				}
			}
				break;
		}


		if (last_p2l == false)
			p2l_die = (p2l_die + 1) & ((shr_nand_info.lun_num >> 1) - 1);
		else
			p2l_die--;


#endif

        if (p2l_die == shr_nand_info.lun_num) {
            p2l_die = 0;
            sys_assert(0);
        }

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
    }

    if (pgsn_plane == p2l_plane) {
        if (last_p2l) {
            pgsn_page = p2l_page - 1;
        } else {
            pgsn_page = p2l_page + 1;
        }
    } else {
        pgsn_page = p2l_page;
    }


    //combine pda
    u32 interleave = p2l_die * get_mp() + p2l_plane;
    *p2l_pda = nal_make_pda(spb_id, (interleave<<shr_nand_info.pda_interleave_shift) + (p2l_page << shr_nand_info.pda_page_shift));
    interleave = p2l_die * get_mp() + pgsn_plane;
    *pgsn_pda = nal_make_pda(spb_id, (interleave<<shr_nand_info.pda_interleave_shift) + (pgsn_page << shr_nand_info.pda_page_shift));

#if 0
       ftl_apl_trace(LOG_ALW, 0x0238, "spor grp:%d, pda:0x%x,die:%d ,page:%d, plane:%d", grp_id, *p2l_pda, p2l_die, p2l_page,p2l_plane);
#endif
}


fast_code static inline u32 get_spor_ncl_cmd_idx(void)
{
	u8 _idx;
	if (_spor_free_cmd_bmp == 0)
    {
#if (SPOR_CMD_EXP_BAND == mENABLE)
        while (_spor_free_cmd_bmp != INV_U32)
#else
		while (_spor_free_cmd_bmp != INV_U16)
#endif
        {
            //ftl_apl_trace(LOG_ALW, 0, "free cmd form is not enough!");
			cpu_msg_isr();
        }
	}

	_idx = ctz(_spor_free_cmd_bmp);
#if (SPOR_ASSERT == mENABLE)
    sys_assert(_idx < FTL_SPOR_NCL_CMD_MAX);
#else
    if(_idx >= FTL_SPOR_NCL_CMD_MAX)
    {
        ftl_apl_trace(LOG_PANIC, 0xa285, "_idx >= FTL_SPOR_NCL_CMD_MAX");
    }
#endif

	_spor_free_cmd_bmp &= ~(1 << _idx);

	return _idx;
}

fast_code static inline void wait_spor_cmd_idle(void)
{
#if (SPOR_CMD_EXP_BAND == mENABLE)
        while (_spor_free_cmd_bmp != INV_U32)
#else
		while (_spor_free_cmd_bmp != INV_U16)
#endif
    {
        //ftl_apl_trace(LOG_ALW, 0, "free cmd not full yet!");
		cpu_msg_isr();
    }
}


fast_code static inline void check_spor_cmd_valid(void)
{
	if (_spor_free_cmd_bmp == 0)
    {
#if (SPOR_CMD_EXP_BAND == mENABLE)
        while (_spor_free_cmd_bmp != INV_U32)
#else
		while (_spor_free_cmd_bmp != INV_U16)
#endif
        {
            //ftl_apl_trace(LOG_ALW, 0, "free cmd form is not enough!");
			cpu_msg_isr();
        }
	}
}



/*!
 * @brief ftl spor function
 * Scan each block's first page
 *
 * @return	not used
 */
fast_code void ftl_send_read_ncl_form(u8 op_code, dtag_t *dtag, pda_t pda, u8 du_cnt,
                                      void (*completion)(struct ncl_cmd_t *))
{
    u8   du_idx;
    u16  cont_idx;
    //u16  cmd_idx;
    u8   cmd_idx;
    u32  meta_idx;

    cmd_idx = get_spor_ncl_cmd_idx();
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	bool slc = (pda2blk(pda) == PLP_SLC_BUFFER_BLK_ID)? true:false;
#endif
    //ftl_apl_trace(LOG_ALW, 0, "Current cmd_idx : 0x%x, _spor_free_cmd_bmp : 0x%x", cmd_idx, _spor_free_cmd_bmp);

    //  ----- fill in form info -----
    //cont_idx = cmd_idx * du_cnt;
    cont_idx = cmd_idx * DU_CNT_PER_PAGE;
    meta_idx = ftl_spor_meta_idx + cont_idx;

    memset(&ftl_info[cont_idx], 0, (du_cnt * sizeof(*ftl_info)));

    for(du_idx = 0; du_idx < du_cnt; du_idx++)
    {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    	if(slc)
        	ftl_info[cont_idx+du_idx].pb_type = NAL_PB_TYPE_SLC;
        else
#endif
        	ftl_info[cont_idx+du_idx].pb_type = NAL_PB_TYPE_XLC;

#ifdef SKIP_MODE
		ftl_pda_list[cont_idx+du_idx] = pda+du_idx;
#else
		ftl_pda_list[cont_idx+du_idx] = ftl_remap_pda(pda+du_idx);
#endif
        ftl_bm_pl[cont_idx+du_idx].pl.btag     = cmd_idx;
		ftl_bm_pl[cont_idx+du_idx].pl.du_ofst  = du_idx;
#if (SPOR_CMD_EXP_BAND == mENABLE)
        ftl_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
#else
        ftl_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
#endif
        ftl_bm_pl[cont_idx+du_idx].pl.nvm_cmd_id = meta_idx + du_idx;
        if((op_code == NCL_CMD_SPOR_P2L_READ) || (op_code == NCL_CMD_SPOR_BLIST_READ))
        {
            ftl_bm_pl[cont_idx+du_idx].pl.dtag = dtag->dtag + du_idx;
        }
        else
        {
            ftl_bm_pl[cont_idx+du_idx].pl.dtag = dtag->dtag;
        }
    }

    ftl_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &ftl_info[cont_idx];
    ftl_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &ftl_pda_list[cont_idx];
    ftl_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = du_cnt;

    ftl_ncl_cmd[cmd_idx].status        = 0;
    ftl_ncl_cmd[cmd_idx].op_code       = op_code;
#if defined(HMETA_SIZE)
    ftl_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG;
    ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_DATA_READ_PA_DTAG;
    ftl_ncl_cmd[cmd_idx].du_format_no  = host_du_fmt;
#else
    ftl_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
    ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    ftl_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
#endif
    ftl_ncl_cmd[cmd_idx].completion    = completion;
    ftl_ncl_cmd[cmd_idx].user_tag_list = &ftl_bm_pl[cont_idx];
    ftl_ncl_cmd[cmd_idx].caller_priv   = (void*)pda;

    ftl_ncl_cmd[cmd_idx].retry_step    = default_read;
#if (PLP_SUPPORT == 0)
    if(op_code == NCL_CMD_SPOR_SCAN_PG_AUX)
    {
    	ftl_ncl_cmd[cmd_idx].retry_step  = spor_retry_type;//non-plp retry
    }
#endif
	#if RAID_SUPPORT_UECC
	ftl_ncl_cmd[cmd_idx].uecc_type = NCL_UECC_SPOR_RD;
	#endif

    #if 0
    ftl_apl_trace(LOG_ALW, 0x94e4, "[FUNC] completion addr : 0x%x, ftl_bm_pl.pl.btag : 0x%x, ftl_bm_pl.pl.nvm_cmd_id : 0x%x",\
                  completion, ftl_bm_pl[cont_idx].pl.btag, ftl_bm_pl[cont_idx].pl.nvm_cmd_id);
    #endif
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(slc)
	{
		ftl_ncl_cmd[cmd_idx].flags &= ~(1<<NCL_CMD_XLC_PB_TYPE_FLAG);
		ftl_ncl_cmd[cmd_idx].flags |=  (1<<NCL_CMD_SLC_PB_TYPE_FLAG);
	}
#endif
    ncl_cmd_submit(&ftl_ncl_cmd[cmd_idx]);
}

/*!
 * @brief ftl spor function
 * Erase super block done
 *
 * @return	not used
 */
//slow_code void ftl_sblk_erase_done(struct ncl_cmd_t *ncl_cmd)
init_code void ftl_sblk_erase_done(struct ncl_cmd_t *ncl_cmd)
{
    u32 cmd_idx;
    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_sblk_erase_done");
    cmd_idx = (u32)ncl_cmd->caller_priv;
    _spor_free_cmd_bmp |= (1 << cmd_idx);
}


/*!
 * @brief ftl spor function
 * Erase super block
 *
 * @return	not used
 */
//init_code void ftl_sblk_erase(u16 spb_id)
slow_code void ftl_sblk_erase(u16 spb_id)
{
    u8  die_id = 0;
    u8  pn_pair, pn_cnt = 0;  
	u8  pln_idx;
	u32 pn_ptr = 0;  

    u16 cont_idx;  
    u32 cmd_idx;  
    pda_t pda = 0;  

    bool slc_blk = false;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(spb_id == PLP_SLC_BUFFER_BLK_ID)
		slc_blk = true;//slc buffer , don't increase EC cnt
	else
#endif
    if (spb_id < get_total_spb_cnt() - tcg_reserved_spb_cnt){
	    spb_info_get(spb_id)->erase_cnt++;	
    }	
    ftl_apl_trace(LOG_ALW, 0x9ef8, "[IN] ftl_sblk_erase , spb_id:%d slc:%d",spb_id,slc_blk);
    pda = ftl_set_blk2pda(pda, spb_id);  
    // ----- make ncl forms for all die -----
    while(die_id < FTL_DIE_CNT)
    {
#ifdef SKIP_MODE
		pn_ptr = die_id << FTL_PN_SHIFT;  
		pn_pair = ftl_good_blk_in_pn_detect(spb_id,pn_ptr);  
#else
		pn_pair = BLOCK_NO_BAD;  
#endif
		//ftl_apl_trace(LOG_ALW, 0, "spb_%d, die:%d, plane:%d", spb_id, die_id, pn_ptr);
		//ftl_apl_trace(LOG_ALW, 0, "pn_pair:%d", pn_pair);

		if(pn_pair > (1<<shr_nand_info.geo.nr_planes)-1)
		{
			panic("pn_pair case abnormal");
		}
		else if(pn_pair == (1<<shr_nand_info.geo.nr_planes)-1)
        {
        	die_id++;
            continue;
        }

		cmd_idx  = get_spor_ncl_cmd_idx();  
        cont_idx = cmd_idx * FTL_PN_CNT;  
        memset(&ftl_info[cont_idx], 0, (FTL_PN_CNT * sizeof(*ftl_info)));

		//fill up the pda_list
		pda = ftl_set_die2pda(pda, die_id);
		for (pln_idx = 0, pn_cnt = 0; pln_idx < shr_nand_info.geo.nr_planes; pln_idx++)
    	{
    		if(BIT_CHECK(pn_pair, pln_idx)) //defect plane
    		{
				//ftl_apl_trace(LOG_ALW, 0, "die:%d, plane:%d", die_id, pln_idx);
				continue;
    		}
    		else
    		{
				ftl_info[cont_idx + pn_cnt].pb_type = slc_blk ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
				ftl_pda_list[cont_idx+pn_cnt]	 = ftl_set_pn2pda(pda, pln_idx); 
				pn_cnt++;
    		}
        }
        ftl_ncl_cmd[cmd_idx].completion = ftl_sblk_erase_done;  
        ftl_ncl_cmd[cmd_idx].status  = 0;  
        ftl_ncl_cmd[cmd_idx].op_code = NCL_CMD_OP_ERASE;  
        ftl_ncl_cmd[cmd_idx].flags   = slc_blk ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG ;  
        ftl_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = pn_cnt;  
        ftl_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &ftl_pda_list[cont_idx];  
        ftl_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &ftl_info[cont_idx];  
        ftl_ncl_cmd[cmd_idx].user_tag_list = NULL;  
        ftl_ncl_cmd[cmd_idx].caller_priv = (void*)cmd_idx;  
        ftl_ncl_cmd[cmd_idx].du_format_no = DU_4K_DEFAULT_MODE;  
        ncl_cmd_submit(&ftl_ncl_cmd[cmd_idx]);  
        die_id++;  
    } // while(die_id < FTL_DIE_CNT)  
  
    // ----- wait all form done -----  
    wait_spor_cmd_idle();  
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done  
}


/*!
 * @brief ftl spor function
 * Init FTL variables
 *
 * @return	not used
 */
init_code void ftl_spor_resource_init(void)
{
    ftl_apl_trace(LOG_ALW, 0x9c83, "[IN] ftl_spor_resource_init");

#if (SPOR_CMD_EXP_BAND == mENABLE)
    _spor_free_cmd_bmp = INV_U32;
#else
    _spor_free_cmd_bmp = INV_U16;
#endif

#if (SPOR_CMD_EXP_BAND == mENABLE)
    #if (SPOR_CMD_ON_DDR == mENABLE)
    ftl_ncl_cmd  = (struct ncl_cmd_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_ncl_cmd)*FTL_SPOR_NCL_CMD_MAX), DTAG_SZE)));
    ftl_pda_list = (pda_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_pda_list)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT), DTAG_SZE)));
    ftl_bm_pl    = (bm_pl_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_bm_pl)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT), DTAG_SZE)));
    ftl_info     = (struct info_param_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_info)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT), DTAG_SZE)));
    #else
    ftl_ncl_cmd  = sys_malloc(SLOW_DATA, sizeof(*ftl_ncl_cmd)*FTL_SPOR_NCL_CMD_MAX);
    ftl_pda_list = sys_malloc(SLOW_DATA, sizeof(*ftl_pda_list)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT);
    ftl_bm_pl    = sys_malloc(SLOW_DATA, sizeof(*ftl_bm_pl)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT);
    ftl_info     = sys_malloc(SLOW_DATA, sizeof(*ftl_info)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT);
    #endif
#else
    ftl_ncl_cmd  = sys_malloc(SLOW_DATA, sizeof(*ftl_ncl_cmd)*FTL_SPOR_NCL_CMD_MAX);
    ftl_pda_list = sys_malloc(SLOW_DATA, sizeof(*ftl_pda_list)*FTL_SPOR_NCL_CMD_MAX*DU_CNT_PER_PAGE);
    ftl_bm_pl    = sys_malloc(SLOW_DATA, sizeof(*ftl_bm_pl)*FTL_SPOR_NCL_CMD_MAX*DU_CNT_PER_PAGE);
    ftl_info     = sys_malloc(SLOW_DATA, sizeof(*ftl_info)*FTL_SPOR_NCL_CMD_MAX*DU_CNT_PER_PAGE);
#endif

    #if 1
    // for share memory usage test
    ftl_apl_trace(LOG_ALW, 0x6d5c, "ncl_cmd addr : 0x%x, pda_list addr : 0x%x, bm_pl addr : 0x%x, info addr : 0x%x",\
                  ftl_ncl_cmd, ftl_pda_list, ftl_bm_pl, ftl_info);
	/*
    ftl_apl_trace(LOG_ALW, 0, "ncl_cmd size : 0x%x, pda_list size : 0x%x, bm_pl size : 0x%x, info size : 0x%x",\
                  (sizeof(*ftl_ncl_cmd)*FTL_SPOR_NCL_CMD_MAX),\
                  (sizeof(*ftl_pda_list)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT),\
                  (sizeof(*ftl_bm_pl)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT),\
                  (sizeof(*ftl_info)*FTL_SPOR_NCL_CMD_MAX*FTL_DU_PER_WL*FTL_PN_CNT));
	*/
    #endif

    sys_assert(ftl_ncl_cmd != NULL);
    sys_assert(ftl_pda_list != NULL);
    sys_assert(ftl_bm_pl != NULL);
    sys_assert(ftl_info != NULL);

    #if 0
    // for meta memory usage test
    ftl_apl_trace(LOG_ALW, 0x1704, "DRAM shared memory alloc count : %d", idx_meta_get_shr_alloc_cnt(DDR_IDX_META));
    ftl_apl_trace(LOG_ALW, 0xbaf0, "SRAM shared memory alloc count : %d", idx_meta_get_shr_alloc_cnt(SRAM_IDX_META));
    #endif

#if (SPOR_CMD_EXP_BAND == mENABLE)
    ftl_spor_meta = idx_meta_allocate((FTL_SPOR_NCL_CMD_MAX*DU_CNT_PER_PAGE), SRAM_IDX_META, &ftl_spor_meta_idx);
#else
    ftl_spor_meta = idx_meta_allocate((FTL_SPOR_NCL_CMD_MAX*DU_CNT_PER_PAGE), DDR_IDX_META, &ftl_spor_meta_idx);
#endif

    #if 0
    // for meta memory usage test
    ftl_apl_trace(LOG_ALW, 0x20ea, "DRAM shared memory alloc count : %d", idx_meta_get_shr_alloc_cnt(DDR_IDX_META));
    ftl_apl_trace(LOG_ALW, 0xd56f, "SRAM shared memory alloc count : %d", idx_meta_get_shr_alloc_cnt(SRAM_IDX_META));
    ftl_apl_trace(LOG_ALW, 0x230f, "spor meta addr : 0x%x, meta idx : 0x%x", ftl_spor_meta, ftl_spor_meta_idx);
    #endif

    ftl_p2l_pos_list = (struct ftl_spor_p2l_pos_list_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_p2l_pos_list)*shr_p2l_grp_cnt), DTAG_SZE)));
    memset(ftl_p2l_pos_list, 0xFF, sizeof(*ftl_p2l_pos_list)*shr_p2l_grp_cnt);

    ftl_spor_info = (struct ftl_spor_info_t*)ddtag2mem(ddr_dtag_register(occupied_by(sizeof(*ftl_spor_info), DTAG_SZE)));
    memset(ftl_spor_info, 0xFF, sizeof(*ftl_spor_info)); // ~= 1376B
    ftl_spor_info->epm_vac_error = mFALSE;
    ftl_spor_info->p2l_data_error   = mFALSE;
    ftl_spor_info->l2p_data_error   = mFALSE;
    ftl_spor_info->blist_data_error = mFALSE;
    ftl_spor_info->glist_eh_not_done = mFALSE;
	ftl_spor_info->ftl_1bit_fail_need_fake_qbt = mFALSE;

    ftl_build_tbl_type = (u8*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(u8)*shr_nand_info.geo.nr_blocks), DTAG_SZE)));
    memset(ftl_build_tbl_type, 0xFF, (sizeof(u8)*shr_nand_info.geo.nr_blocks)); // max : 1958

    ftl_init_sn_tbl = (u32*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(u32)*shr_nand_info.geo.nr_blocks), DTAG_SZE)));
    memset(ftl_init_sn_tbl, 0, (sizeof(u32)*shr_nand_info.geo.nr_blocks));  // max : 7832

    ftl_qbt_scan_info = (struct ftl_scan_info_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_qbt_scan_info)*FTL_MAX_QBT_CNT), DTAG_SZE)));
    memset(ftl_qbt_scan_info, 0xFF, (sizeof(*ftl_qbt_scan_info)*FTL_MAX_QBT_CNT));  // max : 14B

    ftl_pbt_scan_info = (struct ftl_scan_info_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_pbt_scan_info)*FTL_MAX_QBT_CNT), DTAG_SZE)));
    memset(ftl_pbt_scan_info, 0xFF, (sizeof(*ftl_pbt_scan_info)*FTL_MAX_QBT_CNT));  // max : 14B

    ftl_spor_blk_pool = (struct ftl_blk_pool_t*)ddtag2mem(ddr_dtag_register(occupied_by(sizeof(*ftl_spor_blk_pool), DTAG_SZE)));
    memset(ftl_spor_blk_pool, 0xFF, sizeof(*ftl_spor_blk_pool)); // 90B

    ftl_chk_last_wl_bitmap = (u32*)ddtag2mem(ddr_dtag_register(occupied_by((shr_nand_info.geo.nr_blocks/32+1)*sizeof(u32), DTAG_SZE)));
	memset(ftl_chk_last_wl_bitmap, 0, (shr_nand_info.geo.nr_blocks/32+1)*sizeof(u32));

	ftl_special_plp_blk = (u32*)ddtag2mem(ddr_dtag_register(occupied_by((shr_nand_info.geo.nr_blocks/32+1)*sizeof(u32), DTAG_SZE)));
	memset(ftl_special_plp_blk, 0, (shr_nand_info.geo.nr_blocks/32+1)*sizeof(u32));

	ftl_blk_pbt_seg = (u16*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(u16)*shr_nand_info.geo.nr_blocks), DTAG_SZE)));
    memset(ftl_blk_pbt_seg, 0xFF, (sizeof(u16)*shr_nand_info.geo.nr_blocks));
#if(PLP_SUPPORT == 0)
	ftl_spor_trimblkbitmap = (u32*)ddtag2mem(ddr_dtag_register(1));
	memset(ftl_spor_trimblkbitmap,0x0,256);//256B
#endif

    u8 i;
    for(i=0;i<SPB_POOL_MAX;i++)
    {
        ftl_spor_blk_pool->blkCnt[i] = 0;
    }

    ftl_spor_blk_node = (struct ftl_blk_node_t*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(*ftl_spor_blk_node)*shr_nand_info.geo.nr_blocks), DTAG_SZE)));
    memset(ftl_spor_blk_node, 0xFF, (sizeof(*ftl_spor_blk_node)*shr_nand_info.geo.nr_blocks));  // max : 9790B

    #if 0
    // for share memory usage test
    ftl_apl_trace(LOG_ALW, 0x567a, "ftl_spor_info addr : 0x%x, ftl_build_tbl_type addr : 0x%x",\
                  ftl_spor_info, ftl_build_tbl_type, ftl_init_sn_tbl);

    ftl_apl_trace(LOG_ALW, 0x0b7c, "ftl_qbt_scan_info addr : 0x%x, ftl_pbt_scan_info addr : 0x%x, ftl_spor_blk_pool addr : 0x%x, ftl_spor_blk_node addr : 0x%x",\
                  ftl_qbt_scan_info, ftl_pbt_scan_info, ftl_spor_blk_pool, ftl_spor_blk_node);
    #endif

    ftl_dummy_dtag = ddr_dtag_register(1);
    evlog_printk(LOG_ALW, "ftl_dummy_dtag %d", ftl_dummy_dtag);

    // Host/GC P2L buffer initial
    ftl_host_p2l_info.lda_dtag_start  = ddr_dtag_register(LDA_FULL_TABLE_DU_CNT);
    ftl_host_p2l_info.lda_buf         = (lda_t*)ddtag2mem(ftl_host_p2l_info.lda_dtag_start);
    ftl_host_p2l_info.pda_dtag_start  = ddr_dtag_register(PDA_FULL_TABLE_DU_CNT);
    ftl_host_p2l_info.pda_buf         = (pda_t*)ddtag2mem(ftl_host_p2l_info.pda_dtag_start);
    ftl_host_p2l_info.pgsn_dtag_start = ddr_dtag_register(PGSN_FULL_TABLE_DU_CNT);
    ftl_host_p2l_info.pgsn_buf        = (u64*)ddtag2mem(ftl_host_p2l_info.pgsn_dtag_start);
    memset(ftl_host_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
    memset(ftl_host_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
    memset(ftl_host_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);

    ftl_gc_p2l_info.lda_dtag_start  = ddr_dtag_register(LDA_FULL_TABLE_DU_CNT);
    ftl_gc_p2l_info.lda_buf         = (lda_t*)ddtag2mem(ftl_gc_p2l_info.lda_dtag_start);
    ftl_gc_p2l_info.pda_dtag_start  = ddr_dtag_register(PDA_FULL_TABLE_DU_CNT);
    ftl_gc_p2l_info.pda_buf         = (pda_t*)ddtag2mem(ftl_gc_p2l_info.pda_dtag_start);
    ftl_gc_p2l_info.pgsn_dtag_start = ddr_dtag_register(PGSN_FULL_TABLE_DU_CNT);
    ftl_gc_p2l_info.pgsn_buf        = (u64*)ddtag2mem(ftl_gc_p2l_info.pgsn_dtag_start);
    memset(ftl_gc_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
    memset(ftl_gc_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
    memset(ftl_gc_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);
#if (SPOR_CHECK_TAG == mENABLE)
	for(i=0; i<24; i++)
	{
		pbt_chk_lda_buffer[i] = INV_U32;
	}
#endif
    ftl_p2l_fail_grp_flag = (u8*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(u8)*shr_p2l_grp_cnt), DTAG_SZE)));
    memset(ftl_p2l_fail_grp_flag, 0, (sizeof(u8)*shr_p2l_grp_cnt));
    ftl_p2l_fail_cnt = 0;
    ftl_blk_aux_fail_cnt = 0;

#if (WL_DETECT == mENABLE)
    ftl_ssread_dtag = ddr_dtag_register(1);
    ssread_meta = idx_meta_allocate(1, DDR_IDX_META, &ssread_meta_idx);
#endif

    #if (SPOR_AUX_MODE_DBG == mENABLE)  // for p2l aux mode test, Sunny 20210506
    ftl_dbg_p2l_info.lda_dtag_start  = ddr_dtag_register(LDA_FULL_TABLE_DU_CNT);
    ftl_dbg_p2l_info.lda_buf         = (lda_t*)ddtag2mem(ftl_dbg_p2l_info.lda_dtag_start);
    ftl_dbg_p2l_info.pda_dtag_start  = ddr_dtag_register(PDA_FULL_TABLE_DU_CNT);
    ftl_dbg_p2l_info.pda_buf         = (pda_t*)ddtag2mem(ftl_dbg_p2l_info.pda_dtag_start);
    ftl_dbg_p2l_info.pgsn_dtag_start = ddr_dtag_register(PGSN_FULL_TABLE_DU_CNT);
    ftl_dbg_p2l_info.pgsn_buf        = (u64*)ddtag2mem(ftl_dbg_p2l_info.pgsn_dtag_start);
    memset(ftl_dbg_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
    memset(ftl_dbg_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
    memset(ftl_dbg_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);
    #endif

    #if 0
    ftl_apl_trace(LOG_ALW, 0x9a8c, "LDA_GRP_TABLE_DU_CNT : 0x%x, PDA_GRP_TABLE_DU_CNT : 0x%x, PGSN_GRP_TABLE_DU_CNT : 0x%x",\
                  LDA_GRP_TABLE_DU_CNT, PDA_GRP_TABLE_DU_CNT, PGSN_GRP_TABLE_DU_CNT);

    ftl_apl_trace(LOG_ALW, 0xac99, "LDA_GRP_TABLE_ENTRY : 0x%x, PDA_GRP_TABLE_ENTRY : 0x%x, PGSN_GRP_TABLE_ENTRY : 0x%x",\
                  LDA_GRP_TABLE_ENTRY, PDA_GRP_TABLE_ENTRY, PGSN_GRP_TABLE_ENTRY);
    #endif

    #if (SPOR_VAC_CMP == mENABLE)  // for vac dbg, by Sunny 20210312
    ftl_init_vc_tbl = (u32*)ddtag2mem(ddr_dtag_register(occupied_by((sizeof(u32)*shr_nand_info.geo.nr_blocks), DTAG_SZE)));
    memset(ftl_init_vc_tbl, 0, (sizeof(u32)*shr_nand_info.geo.nr_blocks));
    #endif
}

/*!
 * @brief ftl spor function
 * Recycle FTL varaibles memory space
 *
 * @return	not used
 */
init_code void ftl_spor_resource_recycle(void)
{
    ftl_apl_trace(LOG_ALW, 0x7f8c, "[IN] ftl_spor_resource_recycle");

    sys_free(SLOW_DATA, ftl_ncl_cmd);
    sys_free(SLOW_DATA, ftl_pda_list);
    sys_free(SLOW_DATA, ftl_bm_pl);
    sys_free(SLOW_DATA, ftl_info);

    //memset(ftl_spor_meta, 0, (DU_CNT_PER_PAGE*FTL_SPOR_NCL_CMD_MAX));
    //idx_meta_free((DU_CNT_PER_PAGE*FTL_SPOR_NCL_CMD_MAX), SRAM_IDX_META);
    #if 0
    sys_free(SLOW_DATA, ftl_p2l_pos_list);
    sys_free(SLOW_DATA, ftl_spor_info);
    sys_free(SLOW_DATA, ftl_build_tbl_type);
    sys_free(SLOW_DATA, ftl_init_sn_tbl);
    sys_free(SLOW_DATA, ftl_qbt_scan_info);
    sys_free(SLOW_DATA, ftl_pbt_scan_info);
    sys_free(SLOW_DATA, ftl_spor_blk_pool);
    sys_free(SLOW_DATA, ftl_spor_blk_node);
    #endif

    #if 0
    // for share memory usage test
    ftl_apl_trace(LOG_ALW, 0xf33d, "SRAM shared memory alloc count : %d", idx_meta_get_shr_alloc_cnt(SRAM_IDX_META));

    ftl_ncl_cmd  = share_malloc(sizeof(*ftl_ncl_cmd));
    ftl_apl_trace(LOG_ALW, 0xd03d, "test ncl_cmd addr : 0x%x", ftl_ncl_cmd);
    #endif
}


/*!
 * @brief ftl spor function
 * To check if any error happened during SPOR, and trigger save log
 *
 * @return	not used
 */
init_code bool ftl_spor_error_save_log(void)
{
	bool isSave = false;
    if(ftl_spor_info->l2p_data_error || ftl_spor_info->blist_data_error ||
       ftl_spor_info->epm_vac_error || ftl_spor_info->p2l_data_error)
    {
		ftl_apl_trace(LOG_ALW, 0x5a90, "SPOR save log %d %d %d %d", ftl_spor_info->l2p_data_error, ftl_spor_info->blist_data_error, ftl_spor_info->epm_vac_error, ftl_spor_info->p2l_data_error);
        flush_to_nand(EVT_SPOR_UNEXPECT_ERR);
        isSave = true;
    }
    return isSave;
}

/*!
 * @brief ftl spor function
 * check if any QBT block is valid
 *
 * @return	not used
 */
init_code void ftl_qbt_handle(void)
{
    u16 blk, nxt_blk;
    u8  idx, j, blk_err = mFALSE;

    ftl_apl_trace(LOG_ALW, 0x8ce2, "[IN] ftl_qbt_handle");

    ftl_spor_info->maxQBTBlk     = INV_U16;
    ftl_spor_info->maxQBTSn      = INV_U32;
    ftl_spor_info->existQBT      = mFALSE;
	ftl_spor_info->qbterr        = mFALSE;
    ftl_spor_info->scan_QBT_loop = 0;
    shr_qbt_loop = ftl_spor_info->scan_QBT_loop;

    #if 1
    //===============dump log====================
    for(idx = 0; idx < FTL_MAX_QBT_CNT; idx++)
    {
        ftl_apl_trace(LOG_ALW, 0x2aaa, "QBT Scan Loop : 0x%x, Blk : %d, SN : 0x%x, Type : 0x%x",\
               ftl_qbt_scan_info[idx].loop, ftl_qbt_scan_info[idx].blk_idx,\
               ftl_qbt_scan_info[idx].sn, ftl_build_tbl_type[ftl_qbt_scan_info[idx].blk_idx]);
    }

    ftl_apl_trace(LOG_ALW, 0x5c23, "QBT POOL Blk Cnt : 0x%x, QBT FREE POOL Blk Cnt : 0x%x",\
           ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC),\
           ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_FREE));
    //===============dump log====================
    #endif

    // ===== Sorting pair QBT blocks =====
    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC) == 0)
    {
        ftl_apl_trace(LOG_ALW, 0x720b, "No QBT block exist!");
        return;
    }

    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC) == FTL_MAX_QBT_CNT)
    {
        blk = ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC);

        // find scan info idx of smallest SN block first
        for(idx = 0; idx < FTL_MAX_QBT_CNT; idx++)
        {
            if(ftl_qbt_scan_info[idx].blk_idx == blk) {break;}
        }

        // push small SN pair block to free
        idx = (idx/FTL_QBT_BLOCK_CNT)*FTL_QBT_BLOCK_CNT;
        for(j = 0; j < FTL_QBT_BLOCK_CNT; j++)
        {
            ftl_spor_pop_blk_from_pool(ftl_qbt_scan_info[idx+j].blk_idx, SPB_POOL_QBT_ALLOC);
            ftl_spor_sort_blk_sn(ftl_qbt_scan_info[idx+j].blk_idx, SPB_POOL_QBT_FREE);
        }
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC) > FTL_QBT_BLOCK_CNT)
    {
        u8 blk_found = mFALSE;
        // find scan info idx of open block first
        for(idx = 0; idx < FTL_MAX_QBT_CNT; idx++)
        {
            if((ftl_qbt_scan_info[idx].loop==INV_U8) ||
               (ftl_build_tbl_type[ftl_qbt_scan_info[idx].blk_idx] == FTL_SN_TYPE_OPEN_QBT))
            {
                blk_found = mTRUE;
                break;
            }
        }

        if(!blk_found)
        {
            for(idx = 0; idx < FTL_MAX_QBT_CNT; idx++)
            {
                if(ftl_spor_blk_node[ftl_qbt_scan_info[idx].blk_idx].pool_id==SPB_POOL_QBT_FREE)
                {
                    break;
                }
            }
        }

        // push open pair to free
        idx = (idx/FTL_QBT_BLOCK_CNT)*FTL_QBT_BLOCK_CNT;
        for(j = 0; j < FTL_QBT_BLOCK_CNT; j++)
        {
            ftl_spor_pop_blk_from_pool(ftl_qbt_scan_info[idx+j].blk_idx, SPB_POOL_QBT_ALLOC);
            ftl_spor_sort_blk_sn(ftl_qbt_scan_info[idx+j].blk_idx, SPB_POOL_QBT_FREE);
        }
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC) == FTL_QBT_BLOCK_CNT)
    {
        // Do nothing
    }
    else
    {
        if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC) > 0)
        {
            blk = ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC);
            while(blk != INV_U16)
            {
                nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
                ftl_spor_pop_blk_from_pool(blk, SPB_POOL_QBT_ALLOC);
                ftl_spor_sort_blk_sn(blk, SPB_POOL_QBT_FREE);
                blk = nxt_blk;
            }
        }
    }

    // ===== check if index out of order =====
    if(FTL_QBT_BLOCK_CNT == 1)
    {
        ftl_spor_info->maxQBTBlk = ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC);
        ftl_spor_info->maxQBTSn  = ftl_spor_get_blk_sn(ftl_spor_info->maxQBTBlk);
        ftl_spor_info->existQBT  = mTRUE;

        for(idx=0; idx<FTL_MAX_QBT_CNT; idx++)
        {
            if(ftl_qbt_scan_info[idx].blk_idx == ftl_spor_info->maxQBTBlk) {break;}
        }

        ftl_spor_info->scan_QBT_loop = (idx+FTL_QBT_BLOCK_CNT)%FTL_MAX_QBT_CNT;
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC) == FTL_QBT_BLOCK_CNT)
    {
        blk = ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC);
        for(idx = 0; idx < FTL_MAX_QBT_CNT; idx++)
        {
            if(ftl_qbt_scan_info[idx].blk_idx == blk) {break;}
        }

        for(j = 0; j < ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC)-1; j++)
        {
            nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
            if((ftl_qbt_scan_info[(idx+j)+1].loop!=(ftl_qbt_scan_info[(idx+j)].loop+1)) ||
               ((ftl_qbt_scan_info[(idx+j)+1].loop/FTL_QBT_BLOCK_CNT)!=(ftl_qbt_scan_info[(idx+j)].loop/FTL_QBT_BLOCK_CNT)) ||
               (ftl_qbt_scan_info[(idx+j)+1].sn<=(ftl_qbt_scan_info[(idx+j)].sn)))
            {
                blk_err = mTRUE;
                break;
            }
            blk = nxt_blk;
        }

        if(blk_err)
        {
            ftl_apl_trace(LOG_ALW, 0x5b39, "QBT Handle error founded!");
            blk = ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC);
            while(blk != INV_U16)
            {
                nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
                ftl_spor_pop_blk_from_pool(blk, SPB_POOL_QBT_ALLOC);
                ftl_spor_sort_blk_sn(blk, SPB_POOL_QBT_FREE);
                blk = nxt_blk;
            }

            shr_qbt_loop = 0;
            return;
        }

        ftl_spor_info->maxQBTBlk = ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC);
        ftl_spor_info->maxQBTSn  = ftl_spor_get_blk_sn(ftl_spor_info->maxQBTBlk);
        ftl_spor_info->existQBT  = mTRUE;
        ftl_spor_info->scan_QBT_loop = (idx+FTL_QBT_BLOCK_CNT)%FTL_MAX_QBT_CNT;
    }

    shr_qbt_loop = ftl_spor_info->scan_QBT_loop;

    #if 1
    //===============dump log====================
    ftl_apl_trace(LOG_ALW, 0xf7c5, "Max QBT Blk Idx : %d, SN : 0x%x, Alloc Pool Cnt : %d",\
                  ftl_spor_info->maxQBTBlk, ftl_spor_info->maxQBTSn,\
                  ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC));

    blk = ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC);
    while(blk!=INV_U16)
    {
        nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
        ftl_apl_trace(LOG_ALW, 0x25cf, "QBT Blk Idx : %d, SN : 0x%x",\
                      blk, ftl_spor_get_blk_sn(blk));
        blk = nxt_blk;
    }

    ftl_apl_trace(LOG_ALW, 0xc864, "shr_qbt_loop : 0x%x", shr_qbt_loop);
    //===============dump log====================
    #endif
}


init_code void ftl_pbt_handle(void)
{
    u16 blk, nxt_blk;
    u8  idx, j, blk_err = mFALSE;

    ftl_apl_trace(LOG_ALW, 0xc5d8, "[IN] ftl_pbt_handle");

    ftl_spor_info->existPBT  = mFALSE;
    ftl_spor_info->maxPBTBlk = INV_U16;
    ftl_spor_info->maxPBTSn  = INV_U32;
    ftl_spor_info->scan_PBT_loop = 0;
    //shr_pbt_loop = ftl_spor_info->scan_PBT_loop;

    #if 1
    //===============dump log====================
    for(idx = 0; idx < FTL_MAX_PBT_CNT; idx++)
    {
        ftl_apl_trace(LOG_ALW, 0x8e11, "PBT Scan Loop : %d, Blk : %d, SN : 0x%x, Type : 0x%x",\
               ftl_pbt_scan_info[idx].loop, ftl_pbt_scan_info[idx].blk_idx,\
               ftl_pbt_scan_info[idx].sn, ftl_build_tbl_type[ftl_pbt_scan_info[idx].blk_idx]);
    }

    ftl_apl_trace(LOG_ALW, 0xa501, "PBT POOL Blk Cnt : 0x%x, PBT FREE POOL Blk Cnt : 0x%x",\
           ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC),\
           ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT));
    //===============dump log====================
    #endif


    // ===== Sorting pair PBT blocks =====
    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC) == 0)
    {
        ftl_apl_trace(LOG_ALW, 0x577c, "No PBT block exist!");
        return;
    }

    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC) == FTL_MAX_PBT_CNT)
    {
        blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC);

        // find scan info idx of smallest SN block first
        for(idx = 0; idx < FTL_MAX_PBT_CNT; idx++)
        {
            if(ftl_pbt_scan_info[idx].blk_idx == blk) {break;}
        }

        // push small SN pair block to free
        idx = (idx/FTL_PBT_BLOCK_CNT)*FTL_PBT_BLOCK_CNT;
        for(j = 0; j < FTL_PBT_BLOCK_CNT; j++)
        {
            ftl_spor_pop_blk_from_pool(ftl_pbt_scan_info[idx+j].blk_idx, SPB_POOL_PBT_ALLOC);
            ftl_spor_sort_blk_sn(ftl_pbt_scan_info[idx+j].blk_idx, SPB_POOL_PBT);
        }
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC) > FTL_PBT_BLOCK_CNT)
    {
        u8 blk_found = mFALSE;
        // find scan info idx of open block first
        for(idx = 0; idx < FTL_MAX_PBT_CNT; idx++)
        {
            if((ftl_pbt_scan_info[idx].loop==INV_U8) ||
               (ftl_build_tbl_type[ftl_pbt_scan_info[idx].blk_idx] == FTL_SN_TYPE_OPEN_PBT))
            {
                blk_found = mTRUE;
                break;
            }
        }

        if(!blk_found)
        {
            for(idx = 0; idx < FTL_MAX_PBT_CNT; idx++)
            {
                if(ftl_spor_blk_node[ftl_pbt_scan_info[idx].blk_idx].pool_id==SPB_POOL_PBT)
                {
                    break;
                }
            }
        }

        // push open pair to free
        idx = (idx/FTL_PBT_BLOCK_CNT)*FTL_PBT_BLOCK_CNT;
        for(j = 0; j < FTL_PBT_BLOCK_CNT; j++)
        {
            ftl_spor_pop_blk_from_pool(ftl_pbt_scan_info[idx+j].blk_idx, SPB_POOL_PBT_ALLOC);
            ftl_spor_sort_blk_sn(ftl_pbt_scan_info[idx+j].blk_idx, SPB_POOL_PBT);
        }
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC) == FTL_PBT_BLOCK_CNT)
    {
        // Do nothing
    }
    else
    {
        if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC) > 0)
        {
            blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC);
            while(blk != INV_U16)
            {
                nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
                ftl_spor_pop_blk_from_pool(blk, SPB_POOL_PBT_ALLOC);
                ftl_spor_sort_blk_sn(blk, SPB_POOL_PBT);
                blk = nxt_blk;
            }
        }
    }

    // ===== check if index out of order =====
    if(FTL_PBT_BLOCK_CNT == 1)
    {
        ftl_spor_info->maxPBTBlk = ftl_spor_get_tail_from_pool(SPB_POOL_PBT_ALLOC);
        ftl_spor_info->maxPBTSn  = ftl_spor_get_blk_sn(ftl_spor_info->maxPBTBlk);
        ftl_spor_info->existPBT  = mTRUE;

        for(idx=0; idx<FTL_MAX_PBT_CNT; idx++)
        {
            if(ftl_pbt_scan_info[idx].blk_idx == ftl_spor_info->maxPBTBlk) {break;}
        }

        //ftl_spor_info->scan_PBT_loop = (idx+FTL_PBT_BLOCK_CNT)%FTL_MAX_PBT_CNT;
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC) == FTL_PBT_BLOCK_CNT)
    {
        blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC);
        // find scan info idx of complete PBT block
        for(idx = 0; idx < FTL_MAX_PBT_CNT; idx++)
        {
            if(ftl_pbt_scan_info[idx].blk_idx == blk) {break;}
        }

        for(j = 0; j < ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC)-1; j++)
        {
            nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
            if((ftl_pbt_scan_info[(idx+j)+1].loop!=(ftl_pbt_scan_info[(idx+j)].loop+1)) ||
               ((ftl_pbt_scan_info[(idx+j)+1].loop/FTL_PBT_BLOCK_CNT)!=(ftl_pbt_scan_info[(idx+j)].loop/FTL_PBT_BLOCK_CNT)) ||
               (ftl_pbt_scan_info[(idx+j)+1].sn<=(ftl_pbt_scan_info[(idx+j)].sn)))
            {
                blk_err = mTRUE;
                break;
            }
            blk = nxt_blk;
        }

        if(blk_err)
        {
            ftl_apl_trace(LOG_ALW, 0xbb09, "PBT Handle error founded!");
            blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC);
            while(blk != INV_U16)
            {
                nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
                ftl_spor_pop_blk_from_pool(blk, SPB_POOL_PBT_ALLOC);
                ftl_spor_sort_blk_sn(blk, SPB_POOL_PBT);
                blk = nxt_blk;
            }

            //shr_pbt_loop = 0;
            return;
        }

        ftl_spor_info->maxPBTBlk = ftl_spor_get_tail_from_pool(SPB_POOL_PBT_ALLOC);
        ftl_spor_info->maxPBTSn  = ftl_spor_get_blk_sn(ftl_spor_info->maxPBTBlk);
        ftl_spor_info->existPBT  = mTRUE;
        //ftl_spor_info->scan_PBT_loop = (idx+FTL_PBT_BLOCK_CNT)%FTL_MAX_PBT_CNT;
    }

    //shr_pbt_loop = ftl_spor_info->scan_PBT_loop;

    #if 1 // added for dbg
    ftl_apl_trace(LOG_ALW, 0x1232, "Max PBT Blk Idx : %d, SN : 0x%x, Alloc Pool Cnt : %d",\
                  ftl_spor_info->maxPBTBlk, ftl_spor_info->maxPBTSn,\
                  ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC));

    blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC);
    while(blk!=INV_U16)
    {
        nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
        ftl_apl_trace(LOG_ALW, 0xc049, "PBT Blk Idx : %d, SN : 0x%x",\
                      blk, ftl_spor_get_blk_sn(blk));
        blk = nxt_blk;
    }

    //ftl_apl_trace(LOG_ALW, 0, "shr_pbt_loop : 0x%x", shr_pbt_loop);
    #endif

    // ToDo : push free PBT blk back to free pool and erase?
    // if not erase, may need to modify scan last page function to compare same loop blk SN

}


/*!
 * @brief ftl spor function
 * chk if block already in pool
 *
 * @return	not used
 */
init_code u8 ftl_chk_blk_in_pool(u16 pool_id, u16 spb_id)
{
    u16 blk = INV_U16;
    u16 blk_cnt = spb_get_free_cnt(pool_id);

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_chk_blk_in_pool");

#if 0//(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

    if(blk_cnt)
    {
        blk = spb_mgr_get_head(pool_id);
        for(; blk_cnt>0; blk_cnt--)
        {
            if(blk == spb_id)
            {
                return mTRUE;
            }
            blk = spb_info_tbl[blk].block;
        }
    }

#if 0//(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0xa1a9, "Function time cost : %d us", time_elapsed_in_us(time_start));
#endif

    return mFALSE;
}

init_code void ftl_spor_pbt_push_free(void)
{
    u16 blk, nxt_blk, blk_cnt;

    ftl_apl_trace(LOG_ALW, 0x3f94, "[IN] ftl_spor_pbt_push_free");
	FTL_NO_LOG = true;
    blk_cnt = ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT);

    if(blk_cnt)
    {
        //ftl_apl_trace(LOG_ALW, 0, "A1, cnt : 0x%x", blk_cnt);
        blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT);
        while(blk != INV_U16)
        {
            nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
            spb_set_sw_flags_zero(blk);
            spb_set_desc_flags_zero(blk);
            if(spb_info_tbl[blk].pool_id != SPB_POOL_FREE)
            {
               FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
            }

            blk = nxt_blk;
        }
    }
	//Prevent from flushing fake QBT with the just erased PBT //Howard
    blk_cnt = ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC);

    if(blk_cnt)
    {
        //ftl_apl_trace(LOG_ALW, 0, "A2, cnt : 0x%x", blk_cnt);
        blk = ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC);
        while(blk != INV_U16)
        {
            nxt_blk = ftl_spor_blk_node[blk].nxt_blk;
            spb_set_sw_flags_zero(blk);
            spb_set_desc_flags_zero(blk);
            if(spb_info_tbl[blk].pool_id != SPB_POOL_FREE)
            {
                FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
            }

            blk = nxt_blk;
        }
    }

	blk_cnt = spb_get_free_cnt(SPB_POOL_PBT);
	if(blk_cnt)
    {
        //ftl_apl_trace(LOG_ALW, 0, "A3, cnt : 0x%x", blk_cnt);
        blk = spb_mgr_get_head(SPB_POOL_PBT);
        while(blk != INV_U16)
        {
            nxt_blk = spb_info_tbl[blk].block;
            spb_set_sw_flags_zero(blk);
            spb_set_desc_flags_zero(blk);
            if(spb_info_tbl[blk].pool_id != SPB_POOL_FREE)
            {
                FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
            }

            blk = nxt_blk;
        }
    }

	blk_cnt = spb_get_free_cnt(SPB_POOL_PBT_ALLOC);
	if(blk_cnt)
    {
        //ftl_apl_trace(LOG_ALW, 0, "A4, cnt : 0x%x", blk_cnt);
        blk = spb_mgr_get_head(SPB_POOL_PBT_ALLOC);
        while(blk != INV_U16)
        {
            nxt_blk = spb_info_tbl[blk].block;
            spb_set_sw_flags_zero(blk);
            spb_set_desc_flags_zero(blk);
            if(spb_info_tbl[blk].pool_id != SPB_POOL_FREE)
            {
                FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
            }

            blk = nxt_blk;
        }
    }

	ftl_apl_trace(LOG_ALW, 0xc1c1, "[IN] ftl_spor_gcd_push_free");
	u32    *vc, cnt;
    dtag_t dtag;
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);

	blk_cnt = spb_get_free_cnt(SPB_POOL_GCD);

	if(blk_cnt)
    {
        //ftl_apl_trace(LOG_ALW, 0, "gcd, cnt : 0x%x", blk_cnt);
        blk = spb_mgr_get_head(SPB_POOL_GCD);
        while(blk != INV_U16)
        {
            nxt_blk = spb_info_tbl[blk].block;
            if(vc[blk] == 0)
            {
                spb_set_sw_flags_zero(blk);
                spb_set_desc_flags_zero(blk);
                if(ftl_chk_blk_in_pool(SPB_POOL_FREE, blk) == mFALSE)
                {
                    FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
                }
            }
            else
            {
                if((spb_info_tbl[blk].pool_id != SPB_POOL_USER)&&
                   (ftl_chk_blk_in_pool(SPB_POOL_USER, blk) == mFALSE))
                {
                    FTL_BlockPopPushList(SPB_POOL_USER, blk, FTL_SORT_NONE);
                }
            }

            blk = nxt_blk;
        }
    }

	ftl_l2p_put_vcnt_buf(dtag, cnt, false);
	FTL_NO_LOG = false;
}

/*!
 * @brief ftl spor function
 * Set max close block index
 *
 * @return	not used
 */
init_code u16 ftl_get_spor_p2l_blk_head(u32 sn, u8 pool, u8 get_pre)
{
    u16 blk = INV_U16, pre_blk = INV_U16;

    blk = ftl_spor_blk_pool->head[pool];
    pre_blk = blk;
    while (blk != INV_U16)
    {
        if ((ftl_spor_get_blk_sn(blk) == 0) || (ftl_spor_get_blk_sn(blk) < sn))
        {
            if(get_pre == mTRUE)
            {
                pre_blk = blk;
            }

            blk = ftl_spor_blk_node[blk].nxt_blk;
            continue;
        }
        else
        {
            break;
        }
    }

    if(get_pre == mTRUE)
    {
        return pre_blk;
    }
    else
    {
        return blk;
    }
}


/*!
 * @brief ftl spor function
 * Set max close block index
 *
 * @return	not used
 */
init_code void ftl_set_max_close_blk(u16 input_blk)
{
    if(input_blk == INV_U16){return;}

    if((ftl_spor_info->maxcloseBlk == INV_U16) ||
       (ftl_spor_get_blk_sn(input_blk)>ftl_spor_info->maxcloseBlkSn))
    {
        ftl_spor_info->maxcloseBlk   = input_blk;
        ftl_spor_info->maxcloseBlkSn = ftl_spor_get_blk_sn(input_blk);
    }
}


/*!
 * @brief ftl spor function
 * Set max open block index
 *
 * @return	not used
 */
init_code void ftl_set_max_open_blk(u16 input_blk)
{
    if(input_blk == INV_U16){return;}

    if((ftl_spor_info->maxopenBlk == INV_U16) ||
       (ftl_spor_get_blk_sn(input_blk)>ftl_spor_info->maxopenBlkSn))
    {
        ftl_spor_info->maxopenBlk   = input_blk;
        ftl_spor_info->maxopenBlkSn = ftl_spor_get_blk_sn(input_blk);
    }
}


/*!
 * @brief ftl spor function
 * Set max page sn
 *
 * @return	not used
 */
#if 0
init_code void ftl_cmp_last_pg_sn(void)
{
    if((ftl_spor_info->maxUSRPgSn != INV_U64) &&
       (ftl_spor_info->maxGCPgSn != INV_U64))
    {
        if(ftl_spor_info->maxUSRPgSn > ftl_spor_info->maxGCPgSn)
        {
            ftl_spor_info->maxPgSnBlk = ftl_spor_info->maxUSRBlk;
            ftl_spor_info->maxPgSn = ftl_spor_info->maxUSRPgSn;
        }
        else if(ftl_spor_info->maxUSRPgSn < ftl_spor_info->maxGCPgSn)
        {
            ftl_spor_info->maxPgSnBlk = ftl_spor_info->maxGCBlk;
            ftl_spor_info->maxPgSn = ftl_spor_info->maxGCPgSn;
        }
        else
        {
            #if 1
            ftl_apl_trace(LOG_ALW, 0x133e, "USR Blk : 0x%x, SN : 0x%x, PG SN : 0x%x ; GC Blk : 0x%x, SN : 0x%x, PG SN : 0x%x",\
                          ftl_spor_info->maxUSRBlk, ftl_spor_get_blk_sn(ftl_spor_info->maxUSRBlk), ftl_spor_info->maxUSRPgSn,\
                          ftl_spor_info->maxGCBlk, ftl_spor_get_blk_sn(ftl_spor_info->maxGCBlk), ftl_spor_info->maxGCPgSn);
            #endif
            sys_assert(0);
        }
    }
    else if(ftl_spor_info->maxUSRPgSn != INV_U64)
    {
        ftl_spor_info->maxPgSnBlk = ftl_spor_info->maxUSRBlk;
        ftl_spor_info->maxPgSn = ftl_spor_info->maxUSRPgSn;
    }
    else if(ftl_spor_info->maxGCPgSn != INV_U64)
    {
        ftl_spor_info->maxPgSnBlk = ftl_spor_info->maxGCBlk;
        ftl_spor_info->maxPgSn = ftl_spor_info->maxGCPgSn;
    }
}
#endif

/*!
 * @brief ftl spor function
 * Set max host/gc blk sn
 *
 * @return	not used
 */

init_code void ftl_cmp_last_blk_sn(void)
{
	u16 prehost = INV_U16;
	u16 pregc = INV_U16;
    if((ftl_spor_get_blk_cnt_in_pool(SPB_POOL_USER) != 0) &&
       (ftl_spor_get_blk_cnt_in_pool(SPB_POOL_GC) != 0))
    {
        if(ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_USER)) >
           ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_GC)))
        {
            ftl_spor_info->maxHostDataBlk   = ftl_spor_get_tail_from_pool(SPB_POOL_USER);
            ftl_spor_info->maxHostDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_USER));
			prehost = ftl_spor_blk_node[ftl_spor_info->maxHostDataBlk].pre_blk;
			pregc = ftl_spor_get_tail_from_pool(SPB_POOL_GC);
        }
        else if(ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_USER)) <
                ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_GC)))
        {
            ftl_spor_info->maxHostDataBlk   = ftl_spor_get_tail_from_pool(SPB_POOL_GC);
            ftl_spor_info->maxHostDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_GC));
			pregc = ftl_spor_blk_node[ftl_spor_info->maxHostDataBlk].pre_blk;
			prehost = ftl_spor_get_tail_from_pool(SPB_POOL_USER);
        }
        else
        {
            #if 0
            ftl_apl_trace(LOG_ALW, 0x9723, "USR Blk : 0x%x, SN : 0x%x ; GC Blk : 0x%x, SN : 0x%x",\
                          ftl_spor_get_tail_from_pool(SPB_POOL_USER), ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_USER)),\
                          ftl_spor_get_tail_from_pool(SPB_POOL_GC), ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_GC));
            #endif
#if (SPOR_ASSERT == mENABLE)
            sys_assert(0);
#else
            ftl_apl_trace(LOG_PANIC, 0xb252, "Blk SN is the same");
#endif
        }

		if((prehost == INV_U16) && (pregc != INV_U16))
		{
			ftl_spor_info->secondDataBlk = pregc;
		}
		else if((pregc == INV_U16) && (prehost != INV_U16))
		{
			ftl_spor_info->secondDataBlk = prehost;
		}
		else if((pregc != INV_U16) && (prehost != INV_U16))
		{
			ftl_spor_info->secondDataBlk = (ftl_spor_get_blk_sn(prehost)>ftl_spor_get_blk_sn(pregc))? prehost : pregc;
		}

		if(ftl_spor_info->secondDataBlk != INV_U16)
		{
			ftl_spor_info->secondDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_info->secondDataBlk);
		}
		ftl_apl_trace(LOG_INFO, 0xfdbb, "prehost %d pregc %d 2nd blk %d", prehost, pregc, ftl_spor_info->secondDataBlk);
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_USER) != 0)
    {
        ftl_spor_info->maxHostDataBlk   = ftl_spor_get_tail_from_pool(SPB_POOL_USER);
        ftl_spor_info->maxHostDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_USER));
		ftl_spor_info->secondDataBlk = ftl_spor_blk_node[ftl_spor_info->maxHostDataBlk].pre_blk;
		if(ftl_spor_info->secondDataBlk != INV_U16)
		{
			ftl_spor_info->secondDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_info->secondDataBlk);
		}
		ftl_apl_trace(LOG_INFO, 0xe10b, "prehost %d pregc %d 2nd blk %d", prehost, pregc, ftl_spor_info->secondDataBlk);
    }
    else if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_GC) != 0)
    {
        ftl_spor_info->maxHostDataBlk   = ftl_spor_get_tail_from_pool(SPB_POOL_GC);
        ftl_spor_info->maxHostDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_GC));
		ftl_spor_info->secondDataBlk = ftl_spor_blk_node[ftl_spor_info->maxHostDataBlk].pre_blk;
		if(ftl_spor_info->secondDataBlk != INV_U16)
		{
			ftl_spor_info->secondDataBlkSn = ftl_spor_get_blk_sn(ftl_spor_info->secondDataBlk);
		}
		ftl_apl_trace(LOG_INFO, 0xd487, "prehost %d pregc %d 2nd blk %d", prehost, pregc, ftl_spor_info->secondDataBlk);
    }
}

/*!
 * @brief ftl spor function
 * to decide if current scan blk is open blk
 *
 * @return	not used
 */
init_code void ftl_scan_du_blank_handle(u16 blk)
{
    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_scan_du_blank_handle");
    switch(ftl_build_tbl_type[blk])
    {
        case FTL_SN_TYPE_USER:
        case FTL_SN_TYPE_OPEN_USER:
            ftl_build_tbl_type[blk] = FTL_SN_TYPE_OPEN_USER;
			ftl_core_set_spb_open(blk);
            if(blk == ftl_spor_info->lastUSRBlk)
            {
                ftl_spor_info->openUSRBlk = ftl_spor_info->lastUSRBlk;
                ftl_spor_info->openUSRSn  = ftl_spor_info->lastUSRSn;
            }
            break;

        case FTL_SN_TYPE_GC:
        case FTL_SN_TYPE_OPEN_GC:
            ftl_build_tbl_type[blk] = FTL_SN_TYPE_OPEN_GC;
			ftl_core_set_spb_open(blk);
            if(blk == ftl_spor_info->lastGCBlk)
            {
                ftl_spor_info->openGCBlk = ftl_spor_info->lastGCBlk;
                ftl_spor_info->openGCSn  = ftl_spor_info->lastGCSn;
            }
            break;

        case FTL_SN_TYPE_QBT:
        case FTL_SN_TYPE_FQBT:
            ftl_build_tbl_type[blk] = FTL_SN_TYPE_OPEN_QBT;
            if((ftl_spor_info->openQBTSn == INV_U32) ||
               (ftl_spor_get_blk_sn(blk) > ftl_spor_info->openQBTSn))
            {
                ftl_spor_info->openQBTSn = ftl_spor_get_blk_sn(blk);
                ftl_spor_info->openQBTBlk = blk;
            }

            ftl_spor_pop_blk_from_pool(blk, SPB_POOL_QBT_ALLOC);
            ftl_spor_sort_blk_sn(blk, SPB_POOL_QBT_FREE);
            break;

        case FTL_SN_TYPE_PBT:
            ftl_build_tbl_type[blk] = FTL_SN_TYPE_OPEN_PBT;
            if((ftl_spor_info->openPBTSn == INV_U32) ||
               (ftl_spor_get_blk_sn(blk) > ftl_spor_info->openPBTSn))
            {
                ftl_spor_info->openPBTSn = ftl_spor_get_blk_sn(blk);
                ftl_spor_info->openPBTBlk = blk;
            }

            ftl_spor_pop_blk_from_pool(blk, SPB_POOL_PBT_ALLOC);
            ftl_spor_sort_blk_sn(blk, SPB_POOL_PBT);
            break;

        default:
            ftl_apl_trace(LOG_ALW, 0x9b00, "Current block NBT type incorrect - [0x%x]", ftl_build_tbl_type[blk]);
#if (SPOR_ASSERT == mENABLE)
            sys_assert(0);
#endif
            break;
    }
}

/*!
 * @brief ftl spor function
 * Sorting scan first page aux info
 *
 * @return	not used
 */
init_code void ftl_scan_first_pg_sorting(struct info_param_t *info_list, u32 cmd_idx, u32 meta_idx, u16 blk_idx, u8 length)
{
    //u64   pg_sn = 0;
    u16   pbt_seg = INV_U16 ;//,pbt_blk = INV_U16;
    u32   blk_sn = 0;
    u8    blk_type;
    u8    idx = 0, i;
    lda_t lda;
    u8    blank_flag = mFALSE, uecc_flag = mFALSE, meta_crc_flag = mFALSE;

	if(blk_idx > get_total_spb_cnt())
	{
		return;
	}
    // Work around for blank trk error will have uecc error again, Sunny 20201109
    #if 1
    if(ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_BLANK)
    {
        return;
    }
    #endif


    // ============ Error handle area ============
    for(i=0; i<length; i++)
    {
        if(info_list[i].status == cur_du_erase) // Blank
        {
            blank_flag = mTRUE;
        }
        else if(info_list[i].status >= cur_du_ucerr) // UECC
        {
            uecc_flag = mTRUE;
        }
        else if(info_list[i].status == cur_du_dfu_err) // meta CRC error
        {
            meta_crc_flag = mTRUE;
        }
    }

    if(blank_flag) // Blank
    {
        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_BLANK;
        return;
    }
    else if(uecc_flag || meta_crc_flag) // UECC or meta CRC
    {
        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_OPEN_UNKOWN;
        ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_UNALLOC);
        return;
    }
    // ============ Error handle area ============

    lda = ftl_spor_meta[meta_idx].lda;

    //pg_sn  = (((u64)(ftl_spor_meta[meta_idx].fmt1.page_sn_L)) & 0xFFFFFF);
    //pg_sn |= ((((u64)(ftl_spor_meta[meta_idx+1].fmt2.page_sn_H)) & 0xFFFFFF) << 24);

	//if((ftl_spor_meta[meta_idx+1].fmt2.page_sn_H) & (1<<23))
	if(lda == FTL_BLIST_TAG)
	{
		pbt_seg = (u16)(ftl_spor_meta[meta_idx].fmt1.page_sn_L);
		ftl_blk_pbt_seg[blk_idx] = pbt_seg;
		//pbt_blk = (u16)(ftl_spor_meta[meta_idx+1].fmt2.page_sn_H);
	}

    blk_sn = ftl_spor_meta[meta_idx+2].fmt3.blk_sn_L;
    blk_sn |= ftl_spor_meta[meta_idx+3].fmt4.blk_sn_H << 16;
    //spb_set_sn(blk_idx, blk_sn);
    ftl_spor_set_blk_sn(blk_idx, blk_sn);

    blk_type = ftl_spor_meta[meta_idx+2].fmt3.blk_type;

    //ftl_apl_trace(LOG_ALW, 0, "Blk Idx:%d, B_T:0x%x , blk_sn:0x%x pbt_seg:%d LAA:0x%x", blk_idx, blk_type,blk_sn,pbt_seg,lda);

    if (blk_type == FTL_BLK_TYPE_HOST) // Host
    {
        // USR block
        ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_USER);
        if((ftl_spor_info->lastUSRSn == INV_U32) ||
           (ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->lastUSRSn))
        {
            ftl_spor_info->lastUSRSn  = ftl_spor_get_blk_sn(blk_idx);
            ftl_spor_info->lastUSRBlk = blk_idx;
        }

        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_USER;
    }
    else if (blk_type == FTL_BLK_TYPE_GC)  // GC
    {
        // GC block
        ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_GC);
        if((ftl_spor_info->lastGCSn == INV_U32) ||
           (ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->lastGCSn))
        {
            ftl_spor_info->lastGCSn  = ftl_spor_get_blk_sn(blk_idx);
            ftl_spor_info->lastGCBlk = blk_idx;
        }

        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_GC;
    }
    else if((blk_type == FTL_BLK_TYPE_FTL) && ((lda & ~FTL_TABLE_TAG_MASK) == FTL_QBT_TABLE_TAG))
    {
        // QBT block
        idx = (lda & FTL_TABLE_TAG_MASK);
        if((ftl_qbt_scan_info[idx].loop == INV_U8) ||
           (ftl_qbt_scan_info[idx].sn < ftl_spor_get_blk_sn(blk_idx)))
        {
            if(ftl_qbt_scan_info[idx].blk_idx != INV_U16)
            {
                ftl_spor_pop_blk_from_pool(ftl_qbt_scan_info[idx].blk_idx, SPB_POOL_QBT_ALLOC);
                ftl_spor_sort_blk_sn(ftl_qbt_scan_info[idx].blk_idx, SPB_POOL_QBT_FREE);
            }
            ftl_qbt_scan_info[idx].blk_idx = blk_idx;
            ftl_qbt_scan_info[idx].sn      = ftl_spor_get_blk_sn(blk_idx);
            ftl_qbt_scan_info[idx].loop    = idx;
            ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_QBT_ALLOC);
        }
        else
        {
            ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_QBT_FREE);
        }
        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_QBT;
    }
    else if((blk_type == FTL_BLK_TYPE_FTL) && ((lda & ~FTL_TABLE_TAG_MASK) == FTL_FQBT_TABLE_TAG))
    {
        // FQBT block
        idx = (lda & FTL_TABLE_TAG_MASK);
        if((ftl_qbt_scan_info[idx].loop == INV_U8) ||
           (ftl_qbt_scan_info[idx].sn < ftl_spor_get_blk_sn(blk_idx)))
        {
            if(ftl_qbt_scan_info[idx].blk_idx != INV_U16)
            {
                ftl_spor_pop_blk_from_pool(ftl_qbt_scan_info[idx].blk_idx, SPB_POOL_QBT_ALLOC);
                ftl_spor_sort_blk_sn(ftl_qbt_scan_info[idx].blk_idx, SPB_POOL_QBT_FREE);
            }
            ftl_qbt_scan_info[idx].blk_idx = blk_idx;
            ftl_qbt_scan_info[idx].sn      = ftl_spor_get_blk_sn(blk_idx);
            ftl_qbt_scan_info[idx].loop    = idx;
            ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_QBT_ALLOC);
        }
        else
        {
            ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_QBT_FREE);
        }
        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_FQBT;
    }
    else if((blk_type == FTL_BLK_TYPE_FTL) && ((lda & ~FTL_TABLE_TAG_MASK) == FTL_PBT_TABLE_TAG))
    {
        // PBT block
        idx = (lda & FTL_TABLE_TAG_MASK);
        if((ftl_pbt_scan_info[idx].loop == INV_U8) ||
           (ftl_pbt_scan_info[idx].sn < ftl_spor_get_blk_sn(blk_idx)))
        {
            if(ftl_pbt_scan_info[idx].blk_idx != INV_U16)
            {
                ftl_spor_pop_blk_from_pool(ftl_pbt_scan_info[idx].blk_idx, SPB_POOL_PBT_ALLOC);
                ftl_spor_sort_blk_sn(ftl_pbt_scan_info[idx].blk_idx, SPB_POOL_PBT);
            }
            ftl_pbt_scan_info[idx].blk_idx = blk_idx;
            ftl_pbt_scan_info[idx].sn      = ftl_spor_get_blk_sn(blk_idx);
            ftl_pbt_scan_info[idx].loop    = idx;
            ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_PBT_ALLOC);
        }
        else
        {
            ftl_spor_sort_blk_sn(blk_idx, SPB_POOL_PBT);
        }
        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_PBT;
    }
    else
    {
        // ===== if AUX type invalid =====
        ftl_spor_set_blk_sn(blk_idx, 0);

        ftl_apl_trace(LOG_ALW, 0x611b, "Aux data invalid!");

        ftl_apl_trace(LOG_ALW, 0x6ce2, "Current block index : 0x%x", blk_idx);

        ftl_apl_trace(LOG_ALW, 0xe155, "FTL META SN : 0x%x, FTL META Header : 0x%x, FTL META LAA : 0x%x",\
                      ftl_spor_get_blk_sn(blk_idx), blk_type, lda);
        return;
    }

    if((ftl_spor_info->lastBlkSn == INV_U32) ||
       (ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->lastBlkSn))
    {
        ftl_spor_info->lastBlkSn = ftl_spor_get_blk_sn(blk_idx);
        ftl_spor_info->lastBlk   = blk_idx;
    }
}

/*!
 * @brief ftl spor function
 * Sorting scan last page aux info
 *
 * @return	not used
 */

init_code void ftl_scan_last_pg_sorting(struct info_param_t *info_list, u32 cmd_idx, u32 meta_idx, u16 blk_idx, u8 length)
{
    //u64   pg_sn;
    u8    i;
    u8    blank_flag = mFALSE, uecc_flag = mFALSE, meta_crc_flag = mFALSE;

    //pg_sn    = ftl_spor_meta[meta_idx].fmt1.page_sn_L;
    //pg_sn   |= ftl_spor_meta[meta_idx+1].fmt2.page_sn_H << 24;


    // ============ Error handle area ============
    for(i=0; i<length; i++)
    {
        if(info_list[i].status == cur_du_erase) // Blank
        {
            blank_flag = mTRUE;
        }
        else if(info_list[i].status >= cur_du_ucerr) // UECC
        {
            uecc_flag = mTRUE;
        }
        else if(info_list[i].status == cur_du_dfu_err) // meta CRC error
        {
            meta_crc_flag = mTRUE;
        }
    }

    if(blank_flag) // Blank
    {
        ftl_scan_du_blank_handle(blk_idx);
        return;
    }
    else if(uecc_flag || meta_crc_flag) // UECC or meta CRC
    {
        ftl_apl_trace(LOG_INFO, 0x3115, "[LP] spb : 0x%x, UECC/CRC err", blk_idx);
        // ToDo : connect with retry flow, Sunny
        // Work around for blank trk error will have uecc error again, Sunny 20201109
        #if 0
        if((ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_OPEN_USER) ||
           (ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_OPEN_GC) ||
           (ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_OPEN_QBT) ||
           (ftl_build_tbl_type[blk_idx] == FTL_SN_TYPE_OPEN_PBT))
        {
            return;
        }
        #endif
    }
    // ============ Error handle area ============

	if(ftl_spor_meta[meta_idx].lda == SPOR_DUMMY_LDA)
	{
		u8 index   = blk_idx >> 5;
		u8 offset  = blk_idx & 0x1f;
		ftl_chk_last_wl_bitmap[index] |= 1<<offset;
		ftl_apl_trace(LOG_ALW, 0x52f3, "blk:%d  close but spor dummy ,bitmap:0x%x",blk_idx,ftl_chk_last_wl_bitmap[index]);
	}
#if(PLP_FORCE_FLUSH_P2L == mENABLE)
	else if(ftl_spor_meta[meta_idx].lda == DUMMY_PLP_LDA)
	{
		ftl_scan_du_blank_handle(blk_idx);
		switch(ftl_build_tbl_type[blk_idx])
		{
		    case FTL_SN_TYPE_OPEN_USER:
		        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_PLP_USER;
		        break;
		    case FTL_SN_TYPE_OPEN_GC:
		        ftl_build_tbl_type[blk_idx] = FTL_SN_TYPE_PLP_GC;
		        break;
		    default:
		    break;
		  }
		  ftl_apl_trace(LOG_ALW, 0xe636, "special plp blk:%d,type:%d",blk_idx,ftl_build_tbl_type[blk_idx]);
		u8 index   = blk_idx >> 5;
		u8 offset  = blk_idx & 0x1f;
		ftl_special_plp_blk[index] |= 1<<offset;	
        return;
	}
#endif

    switch(ftl_build_tbl_type[blk_idx])
    {
        case FTL_SN_TYPE_USER:
            if (ftl_spor_info->maxUSRSn == INV_U32)
            {
                ftl_spor_info->maxUSRSn   = ftl_spor_get_blk_sn(blk_idx);
                ftl_spor_info->maxUSRBlk  = blk_idx;
                //ftl_spor_info->maxUSRPgSn = pg_sn;
            }
            else if(ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->maxUSRSn)
            {
                ftl_spor_info->secUSRSn   = ftl_spor_info->maxUSRSn;
                ftl_spor_info->secUSRBlk  = ftl_spor_info->maxUSRBlk;
                ftl_spor_info->maxUSRSn   = ftl_spor_get_blk_sn(blk_idx);
                ftl_spor_info->maxUSRBlk  = blk_idx;
                //ftl_spor_info->maxUSRPgSn = pg_sn;
            }
            break;

        case FTL_SN_TYPE_GC:
            if (ftl_spor_info->maxGCSn == INV_U32)
            {
                ftl_spor_info->maxGCSn   = ftl_spor_get_blk_sn(blk_idx);
                ftl_spor_info->maxGCBlk  = blk_idx;
                //ftl_spor_info->maxGCPgSn = pg_sn;
            }
            else if(ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->maxGCSn)
            {
                ftl_spor_info->secGCSn   = ftl_spor_info->maxGCSn;
                ftl_spor_info->secGCBlk  = ftl_spor_info->maxGCBlk;
                ftl_spor_info->maxGCSn   = ftl_spor_get_blk_sn(blk_idx);
                ftl_spor_info->maxGCBlk  = blk_idx;
                //ftl_spor_info->maxGCPgSn = pg_sn;
            }
            break;

        case FTL_SN_TYPE_QBT:
        case FTL_SN_TYPE_FQBT:
            if((ftl_spor_info->maxQBTSn == INV_U32) ||
               (ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->maxQBTSn))
            {
                ftl_spor_info->maxQBTSn  = ftl_spor_get_blk_sn(blk_idx);
                ftl_spor_info->maxQBTBlk = blk_idx;
            }
            break;

        case FTL_SN_TYPE_PBT:
            if ((ftl_spor_info->maxPBTSn == INV_U32) ||
                (ftl_spor_get_blk_sn(blk_idx) > ftl_spor_info->maxPBTSn))
            {
                ftl_spor_info->maxPBTSn  = ftl_spor_get_blk_sn(blk_idx);
                ftl_spor_info->maxPBTBlk = blk_idx;
            }
            break;

         default:
            ftl_apl_trace(LOG_ALW, 0xe24f, "Current block index : 0x%x", blk_idx);
            ftl_apl_trace(LOG_ALW, 0xf75a, "Blk NBT type is not as expected - [0x%x]", ftl_build_tbl_type[blk_idx]);
#if (SPOR_ASSERT == mENABLE)
            sys_assert(0);
#endif
            break;
    }
}


/*!
 * @brief ftl spor function
 * Handle scan du done result
 *
 * @return	not used
 */
init_code void ftl_scan_blk_pg_done(struct ncl_cmd_t *ncl_cmd)
{
    u32 meta_idx;
	u16 cmd_idx, spb_id;
    pda_t pda;
    u8 length, status;
    struct info_param_t *info_list;
    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_scan_blk_pg_done");

    pda      = (pda_t)ncl_cmd->caller_priv;
    spb_id   = pda2blk(pda);
    cmd_idx  = ncl_cmd->user_tag_list[0].pl.btag;
    meta_idx = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
    length   = ncl_cmd->addr_param.common_param.list_len;
    status   = ncl_cmd->addr_param.common_param.info_list[0].status;
    info_list = ncl_cmd->addr_param.common_param.info_list;

    #if 0 // for dbg
    ftl_apl_trace(LOG_ALW, 0xdd93, "Current block index : 0x%x", spb_id);
    ftl_apl_trace(LOG_ALW, 0x937c, "Cmd index : 0x%x, Meta index : 0x%x", cmd_idx, meta_idx);
    #endif

#if NCL_FW_RETRY
    if((ncl_cmd->status) && (status != ficu_err_du_erased))
    {
        extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
        if(ncl_cmd->retry_step == default_read)
        {
    	    //ftl_apl_trace(LOG_INFO, 0, "scan_blk_rd_err_handling, ncl_cmd: 0x%x, length: %d, cmd_idx: %d", ncl_cmd, length, cmd_idx);
            rd_err_handling(ncl_cmd);
            return;
        }
        else if(ncl_cmd->retry_step != raid_recover_fail)
        {
            u8 i;
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && ftl_uc_pda_chk(ncl_cmd) && (rced == false))
            //if (fcns_raid_enabled(nsid) && (rced == false))
            {
                for (i = 0; i < length; i++)
                {
        		    if (info_list[i].status > ficu_err_du_ovrlmt)
                    {
                        rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
                        raid_correct_push(rc_req);
                        return;
                    }
                }
            }
        }
    }
#endif

    meta_idx -= ftl_spor_meta_idx;
    switch (ncl_cmd->op_code)
    {
        case NCL_CMD_SPOR_SCAN_FIRST_PG:
        {
            ftl_scan_first_pg_sorting(ncl_cmd->addr_param.common_param.info_list, cmd_idx, meta_idx, spb_id, length);
            break;
        }

        case NCL_CMD_SPOR_SCAN_LAST_PG:
        {
            ftl_scan_last_pg_sorting(ncl_cmd->addr_param.common_param.info_list, cmd_idx, meta_idx, spb_id, length);
            break;
        }

        default:
#if (SPOR_ASSERT == mENABLE)
            sys_assert(0);
#endif
            break;
    }

    _spor_free_cmd_bmp |= (1 << cmd_idx);
}


/*!
 * @brief ftl spor function
 * Scan each block's first page aux data
 *
 * @return	not used
 */
init_code void ftl_scan_blk_first_page(void)
{
    pda_t pda = 0;
    u8  pn_pair, pn = 0;
    u16 pn_ptr = 0, spb_id;
    u8  switch_die = mFALSE, scan_qbt_done = mFALSE;
    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};
	u8 pln_idx;
    ftl_apl_trace(LOG_ALW, 0x0fad, "[IN] ftl_scan_blk_first_page");


    SCAN_START:
    if(!scan_qbt_done)
    {
        spb_id = spb_mgr_get_head(SPB_POOL_QBT_FREE);
    }
    else
    {
        spb_id = spb_mgr_get_head(SPB_POOL_FREE);
    }

    // ----- start to submit ncl form -----
	while (spb_id != INV_U16)
    {
        // ----- H Mode calculation -----
        if(switch_die)
        {
            switch_die = mFALSE;
            pn_ptr += FTL_PN_CNT; //go to next die
            if(pn_ptr == FTL_TOT_PN_CNT)
            {
                // ===== Error Handle =====
                ftl_apl_trace(LOG_ALW, 0xafb2, "Current spb is bad! Idx : 0x%x", spb_id);
                ftl_spor_sort_blk_sn(spb_id, SPB_POOL_UNALLOC);
                ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_BAD;
                spb_id = spb_info_tbl[spb_id].block;
                continue;
            }
        }
        else
        {	//find first good plane from die 0
            pda = 0;
            pda = ftl_set_blk2pda(pda, spb_id);
            pn_ptr = 0;
        }

#ifdef SKIP_MODE
		pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
		pn_pair = BLOCK_NO_BAD;
#endif
		if(pn_pair > (1<<shr_nand_info.geo.nr_planes)-1)
        {
#if (SPOR_ASSERT == mENABLE)
            panic("pn_pair case abnormal");
#else
            ftl_apl_trace(LOG_PANIC, 0xa040, "pn_pair case abnormal");
#endif
        }
		//find first good plane
		for (pln_idx = 0; pln_idx < shr_nand_info.geo.nr_planes; pln_idx++)
		{
			if (BIT_CHECK(pn_pair, pln_idx)){
				//ftl_apl_trace(LOG_ALW, 0, "[L2P]BIT_CHECK(pln_type, pln_idx) %d", BIT_CHECK(pn_pair, pln_idx));
				if(pln_idx == shr_nand_info.geo.nr_planes - 1)
					switch_die = mTRUE;
				else
					continue;
			}
			else{
				pn = pln_idx; // plane pln_idx is good
				break;
			}
		}
		// all plane is bad
        if(pn_pair == (1<<shr_nand_info.geo.nr_planes)-1 && switch_die)
        {
            continue;
        }

        pda = ftl_set_die2pda(pda, (pn_ptr>>FTL_PN_SHIFT));
        pda = ftl_set_pn2pda(pda, pn);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_SCAN_FIRST_PG, &dtag, pda, DU_CNT_PER_PAGE, ftl_scan_blk_pg_done);
		//scan next spb
        spb_id = spb_info_tbl[spb_id].block;
	}

    if(!scan_qbt_done)
    {
        scan_qbt_done = mTRUE;
        goto SCAN_START;
    }

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
}



/*!
 * @brief ftl spor function
 * Scan each block's last page aux data
 *
 * @return	not used
 */
init_code void ftl_scan_last_wl(u8 pool_id, u32 nsid)
{
	pda_t pda = 0;
	u16   spb_id, die_id = 0, pg = (get_page_per_block()-1);
	u8	  pn_pair, pn = 0;
	u8	  switch_ch = mFALSE;
	u8 pln_idx;
#ifdef SKIP_MODE
	u8	  pn_ptr;
#endif


    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_scan_last_wl");

    // ----- start to submit ncl form -----
    spb_id = ftl_spor_get_head_from_pool(pool_id);
	while(spb_id != INV_U16)
    {
        // ----- H Mode calculation -----
        if(switch_ch)
        {
            die_id--; // go to previous die
            if(die_id == 0)
            {
                ftl_apl_trace(LOG_ALW, 0x8e3a, "Current spb is bad! Idx : 0x%x", spb_id);
                ftl_spor_sort_blk_sn(spb_id, SPB_POOL_UNALLOC);
            }
            switch_ch = mFALSE;
        }
        else
        {	//find first good plane from last page, last die
            pda = 0;
            pda = ftl_set_blk2pda(pda, spb_id);
            pda = ftl_set_pg2pda(pda, pg);
            die_id = (FTL_DIE_CNT-1);//jump to first plane of previous die
        }

#ifdef SKIP_MODE
		pn_ptr = die_id * get_mp();
		pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
		pn_pair = BLOCK_NO_BAD;
#endif
		if(pn_pair > (1<<shr_nand_info.geo.nr_planes)-1)
        {
#if (SPOR_ASSERT == mENABLE)
			panic("pn_pair case abnormal");
#else
			ftl_apl_trace(LOG_PANIC, 0xf8f6, "pn_pair case abnormal");
#endif
		}
		//find first good plane from last plane
		for (pln_idx = shr_nand_info.geo.nr_planes; pln_idx > 0; pln_idx--)
		{
			if (BIT_CHECK(pn_pair, (pln_idx-1))){
				//ftl_apl_trace(LOG_ALW, 0, "[L2P]BIT_CHECK(pln_type, pln_idx) %d", BIT_CHECK(pn_pair, pln_idx));
				if(pln_idx-1 == 0)
					switch_ch = mTRUE;
				else
					continue;
			}
			else{
				pn = pln_idx-1; // plane pln_idx is good
				break;
			}
		}
		// all plane is bad
        if(pn_pair == (1<<shr_nand_info.geo.nr_planes)-1 && switch_ch)
        {
            continue;
        }

        pda = ftl_set_pn2pda(pda, pn);
        pda = ftl_set_die2pda(pda, die_id);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_SCAN_LAST_PG, &dtag, pda, DU_CNT_PER_PAGE, ftl_scan_blk_pg_done);
        spb_id = ftl_spor_blk_node[spb_id].nxt_blk;
	}

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
}

/*!
 * @brief ftl spor function
 * Scan each block's last page aux data
 *
 * @return	not used
 */
init_code void ftl_scan_blk_last_page(void)
{
    u32 nsid = 0;

    ftl_apl_trace(LOG_ALW, 0x0ea6, "[IN] ftl_scan_blk_last_page");

    // ----- start to submit ncl form -----
    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_USER))
    {
        ftl_scan_last_wl(SPB_POOL_USER, nsid);
        //ftl_scan_last_wl_p2l(SPB_POOL_USER, nsid);
    }

    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_GC))
    {
        ftl_scan_last_wl(SPB_POOL_GC, nsid);
        //ftl_scan_last_wl_p2l(SPB_POOL_GC, nsid);
    }

    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_QBT_ALLOC))
    {
        ftl_scan_last_wl(SPB_POOL_QBT_ALLOC, nsid);
    }

    if(ftl_spor_get_blk_cnt_in_pool(SPB_POOL_PBT_ALLOC))
    {
        ftl_scan_last_wl(SPB_POOL_PBT_ALLOC, nsid);
    }
}

init_code void ftl_show_scan_info(void)
{
#if 1
    u32 spb_id = 0;

    while(spb_id < get_total_spb_cnt())
    {
        if(spb_id + 4 < get_total_spb_cnt()){
            ftl_apl_trace(LOG_INFO, 0x189f, "blk : %d, sn : 0x%x 0x%x 0x%x 0x%x 0x%x",
                spb_id, ftl_init_sn_tbl[spb_id], ftl_init_sn_tbl[spb_id+1], ftl_init_sn_tbl[spb_id+2], ftl_init_sn_tbl[spb_id+3], ftl_init_sn_tbl[spb_id+4]);
        }else{
            ftl_apl_trace(LOG_INFO, 0xca3d, "blk : %d, sn : 0x%x",spb_id, ftl_init_sn_tbl[spb_id]);
        }

        if (spb_id + 4 < get_total_spb_cnt())
            spb_id+=5;
        else
            spb_id++;
    }
#else
	u32 spb_id = 0;
	u32 type_blk_id[3]={0,0,0};
	while(spb_id < get_total_spb_cnt())
	{
		if(spb_id + 2 < get_total_spb_cnt())
		{
			type_blk_id[0] = spb_id +	  ftl_build_tbl_type[spb_id]   * 100000;
			type_blk_id[1] = spb_id + 1 + ftl_build_tbl_type[spb_id+1] * 100000;
			type_blk_id[2] = spb_id + 2 + ftl_build_tbl_type[spb_id+2] * 100000;
			ftl_apl_trace(LOG_ALW, 0xc986, "blk:%u,sn:0x%x blk:%u,sn:0x%x blk:%u,sn:0x%x",
				type_blk_id[0],ftl_init_sn_tbl[spb_id],
				type_blk_id[1],ftl_init_sn_tbl[spb_id+1],
				type_blk_id[2],ftl_init_sn_tbl[spb_id+2]);
		}
		else
		{
			type_blk_id[0] = spb_id +	  ftl_build_tbl_type[spb_id]   * 100000;
			ftl_apl_trace(LOG_ALW, 0x11a9, "blk:%u,sn:0x%x",type_blk_id, ftl_init_sn_tbl[spb_id]);
		}

		if (spb_id + 2 < get_total_spb_cnt())
			spb_id+=3;
		else
			spb_id++;
	}
#endif

}

/*!
 * @brief ftl spor function
 * Scan each block's SN and blk type
 *
 * @return	not used
 */
init_code void ftl_scan_all_blk_info(void)
{
#if(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

    ftl_apl_trace(LOG_ALW, 0xc41f, "[IN] ftl_scan_all_blk_info");
    // Read all block first page
    ftl_scan_blk_first_page();

    // Read last page to see if max SN block is open block
    ftl_scan_blk_last_page();

	ftl_show_scan_info();
#if(SPOR_TIME_COST == mENABLE)
		ftl_apl_trace(LOG_ALW, 0xf013, "scan info time cost : %d us", time_elapsed_in_us(time_start));
#endif
}

/*!
 * @brief if glist cnt increased during last runtime, need to check glist final blk SN with PBT SN
 *
 *
 * @return not used
 */
#ifdef ERRHANDLE_GLIST
share_data_zi sGLTable *pGList;
ddr_code void ftl_check_glist_last_blk(void)
{
    u16 wIdx, wDumpIdx, wTotalDumpIdx;
    u16 idx;
    u32 max_blk_sn = INV_U32, cur_blk_sn = 0;

    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    epm_glist_t *epm_glist_start = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable *)(&epm_glist_start->data[0]);

    ftl_spor_info->glist_eh_not_done = mFALSE;

	ftl_apl_trace(LOG_ALW, 0x9791, "glist inc cnt : 0x%x ", epm_ftl_data->glist_inc_cnt);

    if((pGList->dGL_Tag != GLIST_TAG) || (pGList->bGL_Ver != GLIST_VER)) {return;}

    if (pGList->dCycle)
    {
    	wTotalDumpIdx = (GL_TOTAL_ENTRY_CNT + pGList->wGL_Mark_Cnt);
    }
    else
    {
    	wTotalDumpIdx = pGList->wGL_Mark_Cnt;
    }

    // glist has no increased, no need to check
    if((wTotalDumpIdx == 0) || (epm_ftl_data->glist_inc_cnt == 0))
    {
        return;
    }

    wIdx = wTotalDumpIdx-1;
    for (idx = 0; idx < epm_ftl_data->glist_inc_cnt; idx++)
    {
        if(wIdx >= GL_TOTAL_ENTRY_CNT)
        {
            wDumpIdx = wIdx - GL_TOTAL_ENTRY_CNT;
        }
        else
        {
            wDumpIdx = wIdx;
        }

    	if(((pGList->GlistEntry[wDumpIdx].bError_Type == GL_1BREAD_FAIL) ||
           (pGList->GlistEntry[wDumpIdx].bError_Type == GL_PROG_FAIL)) && (pGList->GlistEntry[wDumpIdx].RD_Ftl != 1))
        {
            cur_blk_sn = (pGList->GlistEntry[wDumpIdx].dLBlk_SN == 0) ? ftl_spor_get_blk_sn(pGList->GlistEntry[wDumpIdx].wLBlk_Idx) : pGList->GlistEntry[wDumpIdx].dLBlk_SN;
            if((max_blk_sn == INV_U32) || (max_blk_sn < cur_blk_sn))
            {
                max_blk_sn = cur_blk_sn;
            }
        }

        wIdx--;
    }

    // not found any valid entry
    if(max_blk_sn == INV_U32)
    {
        return;
    }

    if((ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P) &&
       (ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_PBT_ALLOC)) < max_blk_sn))
    {
        ftl_spor_info->glist_eh_not_done = mTRUE;
        ftl_apl_trace(LOG_ALW, 0x775c, "PBT case, blk sn : 0x%x ", max_blk_sn);
    }
    else if((ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P) &&
            (ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC)) < max_blk_sn))
    {
        ftl_spor_info->glist_eh_not_done = mTRUE;
        ftl_apl_trace(LOG_ALW, 0x719c, "QBT case, blk sn : 0x%x ", max_blk_sn);
    }
}
#endif


/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
slow_code void ftl_scan_info_handle(void)
{
    ftl_apl_trace(LOG_ALW, 0x53e4, "[IN] ftl_scan_info_handle");

    // Sorting blk/pg SN
    ftl_cmp_last_blk_sn();

    ftl_set_max_close_blk(ftl_spor_info->maxUSRBlk);
    ftl_set_max_close_blk(ftl_spor_info->maxGCBlk);
    ftl_set_max_close_blk(ftl_spor_info->maxQBTBlk);
    ftl_set_max_close_blk(ftl_spor_info->maxPBTBlk);

    ftl_set_max_open_blk(ftl_spor_info->openUSRBlk);
    ftl_set_max_open_blk(ftl_spor_info->openGCBlk);
    ftl_set_max_open_blk(ftl_spor_info->openQBTBlk);
    ftl_set_max_open_blk(ftl_spor_info->openPBTBlk);

    #if 1
    // ===== dbg log =====
    ftl_apl_trace(LOG_ALW, 0x537d, "[SPOR] MaxUSRBlk[%d] MaxUSRSN[0x%x], MaxGCBlk[%d] MaxGCSN[0x%x]",\
                  ftl_spor_info->maxUSRBlk, ftl_spor_info->maxUSRSn, ftl_spor_info->maxGCBlk, ftl_spor_info->maxGCSn);

	ftl_apl_trace(LOG_ALW, 0x4677, "[SPOR] MaxPBTBlk[%d] MaxPBTSN[0x%x], MaxQBTBlk[%d] MaxQBTSN[0x%x], MaxBlk[%d] MaxSN[0x%x]",\
                  ftl_spor_info->maxPBTBlk, ftl_spor_info->maxPBTSn, ftl_spor_info->maxQBTBlk, ftl_spor_info->maxQBTSn,\
                  ftl_spor_info->maxcloseBlk, ftl_spor_info->maxcloseBlkSn);

    #if 0
    ftl_apl_trace(LOG_ALW, 0x85ad, "[SPOR] MaxUSRBlk PG SN[0x%x], MaxGCBlk PG SN[0x%x], MaxPgSnBlk[0x%x]",\
                  ftl_spor_info->maxUSRPgSn, ftl_spor_info->maxGCPgSn, ftl_spor_info->maxPgSnBlk);
    #endif

    ftl_apl_trace(LOG_ALW, 0x6b72, "[SPOR] OpenUSRBlk[%d] OpenUSRSN[0x%x], OpenGCBlk[%d] OpenGCSN[0x%x]",\
                  ftl_spor_info->openUSRBlk, ftl_spor_info->openUSRSn, ftl_spor_info->openGCBlk, ftl_spor_info->openGCSn);

	ftl_apl_trace(LOG_ALW, 0x4e79, "[SPOR] OpenPBTBlk[%d] OpenPBTSN[0x%x], OpenQBTBlk[%d] OpenQBTSN[0x%x], MaxOpenBlk[%d] MaxOpenSN[0x%x]",\
                  ftl_spor_info->openPBTBlk, ftl_spor_info->openPBTSn, ftl_spor_info->openQBTBlk, ftl_spor_info->openQBTSn,\
                  ftl_spor_info->maxopenBlk, ftl_spor_info->maxopenBlkSn);
    #endif

    if((ftl_spor_info->maxUSRBlk != INV_U16) || (ftl_spor_info->maxGCBlk != INV_U16) ||
       (ftl_spor_info->openUSRBlk != INV_U16) || (ftl_spor_info->openGCBlk != INV_U16))
    {
        ftl_spor_info->existHostData = mTRUE;
    }
    else
    {
        ftl_spor_info->existHostData = mFALSE;
        ftl_apl_trace(LOG_ALW, 0x8463, "No Host Data exist!!");
    }

    gFtlMgr.SerialNumber = (ftl_spor_info->lastBlkSn == INV_U32) ? 0:ftl_spor_info->lastBlkSn;

    ftl_qbt_handle();
#if (SPOR_FLOW_PARTIAL == mDISABLE)
    ftl_pbt_handle();
#endif
}

/*!
 * @brief ftl spor function
 *		  find last pbt
 *
 * @return	not used
 */
init_code void ftl_last_pbt_seg_handle(void)
{
	ftl_apl_trace(LOG_ALW, 0xc306, "[IN] ftl_last_pbt_seg_handle");

	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
#if (PLP_SUPPORT == 1)
	if((epm_ftl_data->epm_sign != FTL_sign) ||
       (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
   {
		//epm err ,no use 2 group build
		last_pbt_blk = INV_U16;
		last_pbt_seg = 0;
		return;
   }
   else 
#endif
   if(epm_ftl_data->pbt_force_flush_flag != 0)
   {
		//force pbt no finish , pbt loop idx may err , no use
		last_pbt_blk = INV_U16;
		last_pbt_seg = 0;
		ftl_apl_trace(LOG_ALW, 0xac1f, "[ERR] runtime force pbt no done!! mode:%d",epm_ftl_data->pbt_force_flush_flag);
		return;
   }

	if((ftl_spor_info->maxHostDataBlk != INV_U16) 
	   && (ftl_spor_info->openPBTBlk != INV_U16)
	   && (ftl_spor_info->openPBTSn < ftl_spor_info->maxHostDataBlkSn) 
	   && (ftl_spor_info->openPBTSn > ftl_spor_info->maxPBTSn))
	{
		if(ftl_blk_pbt_seg[ftl_spor_info->maxHostDataBlk] > 40*shr_nand_info.interleave)
		{
			last_pbt_blk = ftl_spor_info->openPBTBlk;
			last_pbt_seg = ftl_blk_pbt_seg[ftl_spor_info->maxHostDataBlk] - 4*shr_nand_info.interleave;
			ftl_apl_trace(LOG_ALW, 0xc91c, "maxHostBLK:%d seg:%d maxOpenPBT:%d last_pbt_seg:%d",
				ftl_spor_info->maxHostDataBlk,ftl_blk_pbt_seg[ftl_spor_info->maxHostDataBlk],ftl_spor_info->openPBTBlk,last_pbt_seg);
		}
		else
		{
			last_pbt_blk = INV_U16;
			last_pbt_seg = 0;
			ftl_apl_trace(LOG_ALW, 0x586d, "no use maxPBT:%d host seg:%d",ftl_spor_info->openPBTBlk,ftl_blk_pbt_seg[ftl_spor_info->maxHostDataBlk]);
		}
	}
}

#if(PLP_SUPPORT == 0)
init_code void ftl_epm_non_plp_info_sorting(u16 sort_cnt, u16 type)
{
	//type 0 host , 1 gc

	ftl_apl_trace(LOG_ALW, 0xcbca, "[IN] epm_non_plp_info,first_wl:%d sort_cnt:%d type:%d",epm_first_open_wl,sort_cnt,type);
	if(sort_cnt >= SPOR_CHK_WL_CNT)
	{
		ftl_apl_trace(LOG_ALW, 0x1a04, "may err skip it");
		return;
	}
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    u16 *open_wl_tpye = NULL;
    u32 *die_bit_type = NULL;
    if(type == 0)
    {
		open_wl_tpye = epm_ftl_data->host_open_wl;
		die_bit_type = epm_ftl_data->host_die_bit;
    }
    else if(type == 1)
    {
		open_wl_tpye = epm_ftl_data->gc_open_wl;
		die_bit_type = epm_ftl_data->gc_die_bit;
    }
    else
    {
		//panic("type err");
		return;
    }
    u8 i,cnt;
    for(cnt = 0; cnt < sort_cnt; cnt++)
    {
		for(i = SPOR_CHK_WL_CNT - 1; i > 0; i--)
		{
			open_wl_tpye[i] = open_wl_tpye[i-1];
			die_bit_type[i] = die_bit_type[i-1];
		}
		open_wl_tpye[0] = open_wl_tpye[1] - 1;
		die_bit_type[0] = 0x0;	//init
		ftl_apl_trace(LOG_ALW, 0x3369, "open_wl_tpye[0]:%d [1]:%d [2]:%d b[1]:0x%x b[2]:0x%x",open_wl_tpye[0],open_wl_tpye[1],open_wl_tpye[2],die_bit_type[1],die_bit_type[2]);
    }   
}

fast_code void ftl_set_non_plp_info(u16 wl_idx , u16 die_idx , pda_t pda , bool type , u8 method)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    u16 *open_wl_tpye = NULL;
    u32 *die_bit_type = NULL;
    u16 wl_bit_idx    = 0;
    if(type == 0)
    {
		open_wl_tpye = epm_ftl_data->host_open_wl;
		die_bit_type = epm_ftl_data->host_die_bit;
    }
    else if(type == 1)
    {
		open_wl_tpye = epm_ftl_data->gc_open_wl;
		die_bit_type = epm_ftl_data->gc_die_bit;
    }
    else
    {
    	//if enter user build , all open blk will enter aux build , 
    	//but only max user blk and gc blk will set this type!! don't panic
		//panic("type err");
		return;
    }

    //---------------set epm wl and die---------------------
	if(ftl_spor_incomp_fag == false)//record first open wl
	{
		ftl_spor_incomp_fag = true;
		epm_first_open_wl   = wl_idx;
		open_wl_tpye[0] 	= wl_idx;
		ftl_apl_trace(LOG_INFO, 0xf3a0,"set nonplp blk first wl:%d type:%d",epm_first_open_wl,type);
	} 
	if(wl_idx < epm_first_open_wl)//if cur_wl_idx < first open wl,need sort epm buffer
	{
		ftl_apl_trace(LOG_INFO, 0x3809, "[NONPLP] Type:%d cur_wl_id %d first_open_wl %d",type,wl_idx,epm_first_open_wl);
		ftl_epm_non_plp_info_sorting(epm_first_open_wl - wl_idx , type);
		epm_first_open_wl = wl_idx;
	}
	if((wl_idx >= epm_first_open_wl) && (wl_idx < epm_first_open_wl + SPOR_CHK_WL_CNT))
    {
        wl_bit_idx = wl_idx - epm_first_open_wl;
       	open_wl_tpye[wl_bit_idx] = wl_idx;
        die_bit_type[wl_bit_idx] |= (1 << die_idx);
        #if (SPOR_NON_PLP_LOG == mENABLE)
        ftl_apl_trace(LOG_ALW, 0xb7c6, "[EPMINFO] Type:%d cur_pda:0x%x wl:%d die:%d bitmap:0x%x method:%d",
        	type,pda,wl_idx,die_idx,die_bit_type[wl_bit_idx],method);
        #endif
    }
	else
	{
		ftl_apl_trace(LOG_INFO, 0xfee4, "[NONPLP]  Type:%d ERR!!!!!!! wl_idx:%d first_wl:%d method:%d",type,wl_idx,epm_first_open_wl,method);
	}


}

init_code void ftl_set_non_plp_blk(bool open_flag ,spb_id_t spb_id ,u16 first_aux_grp ,u16 grp_threshold)
{
	if(ftl_spor_blk_node[spb_id].nxt_blk != INV_U16)
	{
		ftl_apl_trace(LOG_INFO, 0x1b92, "[NONPLP] spb_id:%d is not last blk , blk_type:%d, next:%d",
			spb_id,ftl_build_tbl_type[spb_id],ftl_spor_blk_node[spb_id].nxt_blk);
			return;
	}
	epm_FTL_t *epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(open_flag) //open blk	
	{
		if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER)&&(epm_ftl_data->host_open_blk[0] == INV_U16))
		{
			epm_ftl_data->host_open_blk[0] = spb_id;
			epm_ftl_data->host_aux_group   = first_aux_grp;
			//ftl_apl_trace(LOG_INFO, 0, "[NONPLP] aux host open spb:%d first_aux_grp:%d",spb_id,first_aux_grp);
		}
		else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC)&&(epm_ftl_data->gc_open_blk[0] == INV_U16))
		{
			epm_ftl_data->gc_open_blk[0] = spb_id;
			epm_ftl_data->gc_aux_group	 = first_aux_grp;
			//ftl_apl_trace(LOG_INFO, 0, "[NONPLP] aux gc   open spb:%d first_aux_grp:%d",spb_id,first_aux_grp);
		}  
	}
	else		  //close blk(may fill dummy or last wl is incomplete wl)
	{
		if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_USER)&&(epm_ftl_data->host_open_blk[0] == INV_U16))
		{
			ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_OPEN_USER;
			epm_ftl_data->host_open_blk[0] = spb_id;
			epm_ftl_data->host_aux_group   = first_aux_grp;
			ftl_spor_info->openUSREndGrp = grp_threshold;//need set threshold
			//ftl_apl_trace(LOG_INFO, 0, "[NONPLP] aux host spb:%d first_aux_grp:%d",spb_id,first_aux_grp);
		}
		else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_GC)&&(epm_ftl_data->gc_open_blk[0] == INV_U16))
		{
			ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_OPEN_GC;
			epm_ftl_data->gc_open_blk[0] = spb_id;
			epm_ftl_data->gc_aux_group	 = first_aux_grp;
			ftl_spor_info->openGCEndGrp = grp_threshold;
			//ftl_apl_trace(LOG_INFO, 0, "[NONPLP] aux gc   spb:%d first_aux_grp:%d",spb_id,first_aux_grp);
		}
	}

	ftl_apl_trace(LOG_INFO, 0xf10b, "[NONPLP] set blk:%d first_aux_grp:%d open_flag:%d grp_threshold:%d",
							spb_id,first_aux_grp,open_flag,grp_threshold);
}
#endif

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
ddr_code void ftl_qbt_slc_mode_dbg(void)
{
	epm_FTL_t *epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    u32    *vc, cnt;
    dtag_t dtag;
	dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
	//memcpy(vc, epm_ftl_data->epm_vc_table, shr_nand_info.geo.nr_blocks*sizeof(u32));
	ftl_apl_trace(LOG_ALW, 0xe497," [SLC] QBT + SLC case , last blk sn:0x%x QBT sn:0x%x",epm_ftl_data->plp_last_blk_sn,ftl_spor_info->scan_sn);
	ftl_apl_trace(LOG_ALW, 0x02ed," QBT SLC vac :0x%x epm SLC vac :0x%x",vc[PLP_SLC_BUFFER_BLK_ID],epm_ftl_data->epm_vc_table[PLP_SLC_BUFFER_BLK_ID]);
	ftl_apl_trace(LOG_ALW, 0x3482," MAX user SN:0x%x blk:%d",ftl_spor_info->maxHostDataBlkSn,ftl_spor_info->maxHostDataBlk);
	ftl_l2p_put_vcnt_buf(dtag, cnt, true);
}


init_code void ftl_aux_build_slc_l2p_done(struct ncl_cmd_t *ncl_cmd) 
{ 

    u32 meta_idx; 
    pda_t pda; 
    u8 length; 
    u16 cmd_idx; 
    struct info_param_t *info_list;

    info_list = ncl_cmd->addr_param.common_param.info_list;
    meta_idx = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id; 
	meta_idx -= ftl_spor_meta_idx; 
    pda      = (pda_t)ncl_cmd->caller_priv; 
    length   = ncl_cmd->addr_param.common_param.list_len; 
    cmd_idx  = ncl_cmd->user_tag_list[0].pl.btag; 

    u8 i; 
    //pda_t pda_read; 

	u32 lda_buf_idx = 0;
	u32 pda_buf_idx = 0;
	lda_t *lda_buf_start = NULL;
	pda_t *pda_buf_start = NULL;

	lda_buf_idx = (pda&FTL_PDA_DU_MSK);
	pda_buf_idx = (lda_buf_idx / DU_CNT_PER_PAGE);
	lda_buf_start = ftl_host_p2l_info.lda_buf;
	pda_buf_start = ftl_host_p2l_info.pda_buf;
    sys_assert(pda_buf_idx < PDA_FULL_TABLE_ENTRY);

	// ============ Error handle area ============
	u8	du_is_blank = mFALSE;
    #if NCL_FW_RETRY
		if(ncl_cmd->status)
		{
			// change blank flag assign mechanism, Sunny 20210901

			for(i=0; i < length; i++)
			{
				if(info_list[i].status == ficu_err_du_erased)
				{
					du_is_blank = mTRUE;
				}
			}

			if(du_is_blank)
			{
				goto START_AUX_EXTRACT;
			}

			extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
			if(ncl_cmd->retry_step == default_read)
			{											  
				rd_err_handling(ncl_cmd);
				return;
			}
			else if(ncl_cmd->retry_step == spor_retry_type)
			{
				rd_err_handling(ncl_cmd);
				return;
				//aux build retry handle
			}

			if(ncl_cmd->retry_step != raid_recover_fail && slc_read_blank_pos == INV_PDA)
			{
				u8 enter_raid = mTRUE;
				u32 nsid = INT_NS_ID;
				bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
				if (fcns_raid_enabled(nsid) && ftl_uc_pda_chk(ncl_cmd) && (rced == false) && enter_raid)
				//if (fcns_raid_enabled(nsid) && (rced == false) && enter_raid)
				{
					for (i = 0; i < length; i++)
					{
						if (info_list[i].status > ficu_err_du_ovrlmt)
						{
							rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
							raid_correct_push(rc_req);
							return;
						}
					}
				}
			}
		}
	#endif
	// ============ Error handle area ============	
START_AUX_EXTRACT:

    if(du_is_blank)
    {
        slc_read_blank_pos = min(slc_read_blank_pos,pda);
    	ftl_apl_trace(LOG_ERR, 0x839c,"slc incomplete wl,position:0x%x",pda);
        pda_buf_start[pda_buf_idx]  = INV_PDA;
    }
	else
	{

		for(i=0; i < length; i++) 
		{
			if((info_list[i].status == cur_du_good) || (info_list[i].status == cur_du_partial_err))
			{
				lda_buf_start[lda_buf_idx + i] = ftl_spor_meta[meta_idx+i].lda;
				/*
				pda_read = pda + i; 
				//pda_meta = ftl_spor_meta[meta_idx+i].fmt; 
				if(ftl_spor_meta[meta_idx+i].lda != DUMMY_LDA)
				{
					ftl_apl_trace(LOG_ALW, 0x276d,"pda_read:0x%x pda_buf_idx:%d, lda:0x%x lda_buf_idx:%d",
						pda_read,pda_buf_idx,ftl_spor_meta[meta_idx+i].lda,lda_buf_idx+i); 
				}
				*/
			}
			else
			{
				lda_buf_start[lda_buf_idx + i] = INV_LDA;
			}
		}
	    pda_buf_start[pda_buf_idx] = pda;
	}

    _spor_free_cmd_bmp |= (1 << cmd_idx); 
} 

ddr_code  void ftl_aux_build_slc_l2p(void) 
{ 
	// aux read slc cache buffer 
    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)}; 
    //epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag); 

    pda_t pda_start = shr_plp_slc_start_pda, pda_end = shr_plp_slc_end_pda; 
	pda_t pda_cur = pda_start; 
	sys_assert(pda_end > pda_start);

	memset((lda_t*)ddtag2mem(ftl_host_p2l_info.lda_dtag_start), 0xFF, LDA_FULL_TABLE_SIZE);
	memset((pda_t*)ddtag2mem(ftl_host_p2l_info.pda_dtag_start), 0xFF, PDA_FULL_TABLE_SIZE);
	//memset((u64*)ddtag2mem(pgsn_dtag_start),  0xFF, PGSN_FULL_TABLE_SIZE); 

#if 1/*---------------------------skip raid + defect------------------------------*/
	bool raid_support = fcns_raid_enabled(INT_NS_ID);
    u16 parity_die_idx = FTL_DIE_CNT;
    u16 parity_die_pln_idx = FTL_PN_CNT;
#ifdef SKIP_MODE
	u32 die_pn_idx;
	u8	*df_ptr;
	u32 idx, off;
#endif
	u8  pn_pair;
	u8	pn_ptr = 0;
	spb_id_t spb_id = PLP_SLC_BUFFER_BLK_ID;

	if(raid_support){
		parity_die_idx = FTL_DIE_CNT - 1;
		while(parity_die_idx >= 0)
		{
#ifdef SKIP_MODE
		pn_ptr = parity_die_idx << FTL_PN_SHIFT;
		pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
		pn_pair = BLOCK_NO_BAD;
#endif

			if (pn_pair == BLOCK_ALL_BAD) {
				parity_die_idx--;
			} else {
				bool ret = false;
				for (u8 pl = 0 ; pl < shr_nand_info.geo.nr_planes; pl++){
					if (((pn_pair >> pl)&1)==0){ //good plane
						parity_die_pln_idx = pl;
						ret = true;
							break;
					}
				}
				sys_assert(ret);
				break;
			}
		}
	}
#ifdef SKIP_MODE
	df_ptr = ftl_get_spb_defect(spb_id);
#endif

#endif/*---------------------------skip raid + defect------------------------------*/

    while(pda_cur < pda_end) 
    { 
#ifdef SKIP_MODE
		die_pn_idx = (ftl_pda2die(pda_cur) << FTL_PN_SHIFT) | ftl_pda2plane(pda_cur);
		idx = die_pn_idx >> 3;
		off = die_pn_idx & (7);
		if((ftl_pda2die(pda_cur) == parity_die_idx && ftl_pda2plane(pda_cur) == parity_die_pln_idx) ||
			(((df_ptr[idx] >> off)&1)!=0))
		{
			pda_cur += DU_CNT_PER_PAGE; 
			continue;
		}	
#endif
		ftl_send_read_ncl_form(NCL_CMD_SPOR_SCAN_PG_AUX, &dtag, pda_cur, DU_CNT_PER_PAGE, ftl_aux_build_slc_l2p_done); 

		pda_cur += DU_CNT_PER_PAGE; 
    } 
    wait_spor_cmd_idle(); 

	ftl_apl_trace(LOG_ALW, 0x92bc," pda_start:0x%x wl:%d ,pda_end:0x%x wl:%d",pda_start,ftl_pda2page(pda_start),pda_end,ftl_pda2page(pda_end)); 
} 
#endif

/*!
 * @brief ftl spor function
 * Scan block aux data to rebuild P2L table, cmd done handle
 *
 * @return	not used
 */
fast_code void ftl_page_aux_build_p2l_done(struct ncl_cmd_t *ncl_cmd)
{
    u32 meta_idx, cmd_idx;
    u8  i, length;
    struct info_param_t *info_list;
    pda_t pda, cpda, cpda_ofst;
    u64   page_sn = 0;
    lda_t *lda_buf_start = NULL;
    pda_t *pda_buf_start = NULL;
    u64   *pgsn_buf_start = NULL;
    u32   pgsn_buf_idx, lda_buf_idx;
    u16   grp_idx = 0, grp_ofst = 0;
    u16   spb_id;
    u8  du_is_blank = mFALSE;

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_page_aux_build_p2l_done");

    cmd_idx   = ncl_cmd->user_tag_list[0].pl.btag;
    meta_idx  = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
    info_list = ncl_cmd->addr_param.common_param.info_list;
    length    = ncl_cmd->addr_param.common_param.list_len;
    pda       = (pda_t)ncl_cmd->caller_priv;

#if(PLP_SUPPORT == 0)
    u16   wl_id,die_id/*,plane_id*/;
    epm_FTL_t* epm_ftl_data;
    //u16  wl_bit_idx = 0;
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

    wl_id     = ftl_pda2wl(pda);
    die_id    = ftl_pda2die(pda);
    //plane_id  = ftl_pda2plane(pda);
#endif
    spb_id    = pda2blk(pda);
    cpda      = ftl_pda2cpda(pda, V_MODE);
    meta_idx -= ftl_spor_meta_idx;

    lda_buf_idx = (pda&FTL_PDA_DU_MSK);
    cpda_ofst   = (cpda % FTL_DU_PER_TLC_SBLOCK);
    grp_idx      = cpda_ofst/FTL_DU_PER_GRP;
    grp_ofst     = (cpda_ofst%FTL_DU_PER_GRP)>>2;  // divided by DU cnt per page
    pgsn_buf_idx = (grp_idx*PGSN_GRP_TABLE_ENTRY) + grp_ofst;

    // ============ Error handle area ============
    #if NCL_FW_RETRY
    if(ncl_cmd->status)
    {
        // change blank flag assign mechanism, Sunny 20210901

        for(i=0; i < length; i++)
        {
            if(info_list[i].status == ficu_err_du_erased)
            {
                du_is_blank = mTRUE;
            }
        }

        if(du_is_blank)
        {
            goto START_AUX_EXTRACT;
        }

     	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
        if(ncl_cmd->retry_step == default_read)
        {
            rd_err_handling(ncl_cmd);
            return;
        }
        else if(ncl_cmd->retry_step == spor_retry_type)
        {
            rd_err_handling(ncl_cmd);
            return;
			//aux build retry handle
        }
#if(PLP_SUPPORT == 0)
        else if(ncl_cmd->retry_step == retry_end)
        {
			//ftl_apl_trace(LOG_ERR, 0, "[NONPLP]SPOR retry fail mark pda");
			 if(epm_ftl_data->host_open_blk[0] == spb_id)
            {
                for(i=0; i < length; i++)
                {
                    if((info_list[i].status != ficu_err_good) && (info_list[i].status != ficu_err_du_erased))
                    {
						ftl_set_non_plp_info(wl_id, die_id, pda, 0 , 1);//host
						break;
                    }
                }  
            }
            else if(epm_ftl_data->gc_open_blk[0] == spb_id)
            {
                //epm_ftl_data->openblk[1] = spb_id;
                for(i=0; i < length; i++)
                {
                    if((info_list[i].status != ficu_err_good) && (info_list[i].status != ficu_err_du_erased))
                    {

						ftl_set_non_plp_info(wl_id, die_id, pda, 1 , 1);//gc
						break;
                    }
                }       
            }

           goto START_AUX_EXTRACT;
        }
#else
        else if(ncl_cmd->retry_step != raid_recover_fail)
        {
		    u8 enter_raid = mTRUE;
		    u32 nsid = INT_NS_ID;
		    bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
		    if (fcns_raid_enabled(nsid) && ftl_uc_pda_chk(ncl_cmd) && (rced == false) && enter_raid)
		    //if (fcns_raid_enabled(nsid) && (rced == false) && enter_raid)
		    {
		    	for (i = 0; i < length; i++)
		        {
		    	    if (info_list[i].status > ficu_err_du_ovrlmt)
		            {
		                rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
		                raid_correct_push(rc_req);
		                return;
		            }
		        }
		    }
        }
#endif//#if(PLP_SUPPORT == 0)
    }
    #endif


START_AUX_EXTRACT:



  #if (SPOR_AUX_MODE_DBG == mENABLE)
    lda_buf_start  = ftl_dbg_p2l_info.lda_buf;
    pgsn_buf_start = ftl_dbg_p2l_info.pgsn_buf;
    pda_buf_start  = ftl_dbg_p2l_info.pda_buf;
  #else
    if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER))
    {
        lda_buf_start  = ftl_host_p2l_info.lda_buf;
        pgsn_buf_start = ftl_host_p2l_info.pgsn_buf;
        pda_buf_start  = ftl_host_p2l_info.pda_buf;
    }
    else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
    {
        lda_buf_start  = ftl_gc_p2l_info.lda_buf;
        pgsn_buf_start = ftl_gc_p2l_info.pgsn_buf;
        pda_buf_start  = ftl_gc_p2l_info.pda_buf;
    }
  #endif

    #if 0
    ftl_apl_trace(LOG_ALW, 0xaa84, "cpda_ofst : 0x%x, grp_idx : %d, grp_ofst : %d, pgsn_buf_idx :0x%x",\
                  cpda_ofst, grp_idx, grp_ofst, pgsn_buf_idx);
    #endif

    // add blank handle, for pg sn abnormal solution, Sunny 20210901
    if(du_is_blank)
    {
        pgsn_buf_start[pgsn_buf_idx] = INV_U64;
        pda_buf_start[pgsn_buf_idx]  = INV_U32;
    }
    else
    {
        // check if any LDA aux read fail
        for(i=0; i < length; i++)
        {
            if((info_list[i].status == cur_du_good) || (info_list[i].status == cur_du_partial_err))
            {
                lda_buf_start[lda_buf_idx+i] = ftl_spor_meta[meta_idx+i].lda;
            }
            else
            {
                lda_buf_start[lda_buf_idx+i] = INV_U32;
                ftl_blk_aux_fail_cnt++;
            }
        }

        // if page sn meta not good
        if(((info_list[0].status == cur_du_good) || (info_list[0].status == cur_du_partial_err)) &&
           ((info_list[1].status == cur_du_good) || (info_list[1].status == cur_du_partial_err)))
        {
            page_sn  = (((u64)(ftl_spor_meta[meta_idx].fmt1.page_sn_L)) & 0xFFFFFF);
            page_sn |= ((((u64)(ftl_spor_meta[meta_idx+1].fmt2.page_sn_H)) & 0xFFFFFF) << 24) ;
            pgsn_buf_start[pgsn_buf_idx] = page_sn;
            pda_buf_start[pgsn_buf_idx]  = pda;
            // added for dbg
            #if 0
            if((ftl_spor_meta[meta_idx].fmt1.page_sn_L == 0xFFFFFF) ||
               ((ftl_spor_meta[meta_idx+1].fmt2.page_sn_H << 24) == 0xFFFFFF) )
            {
                ftl_apl_trace(LOG_ALW, 0x78b6, "Aux PG SN abnormal : 0x%x-%x ",\
                              (ftl_spor_meta[meta_idx].fmt1.page_sn_L),\
                              (ftl_spor_meta[meta_idx+1].fmt2.page_sn_H << 24));
            }
            #endif
        }
        else
        {
            pgsn_buf_start[pgsn_buf_idx] = INV_U64;
            pda_buf_start[pgsn_buf_idx]  = INV_U32;
        }
    }

    _spor_free_cmd_bmp |= (1 << cmd_idx);
}

/*!
 * @brief ftl spor function
 * Scan block aux data to rebuild P2L table
 *
 * @return	not used
 */
init_code void ftl_page_aux_build_p2l(u16 spb_id, u16 grp_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start)
{
#ifdef WL_DETECT
    u32 incomplet_wl_bitmap[8] = {0};
    u32 bmap_idx, bmap_off;
    u32 grp_pda;
    u32 grp_wl;
    u32 cur_grp_pl_idx = 0;
    u16 grp_first_wl , cur_wl_idx;
#endif
    lda_t *lda_buf_start = NULL;
    pda_t *pda_buf_start = NULL;
    u64   *pgsn_buf_start = NULL;

    u8  pn_pair;
	u8	pn_ptr = 0;
	pda_t pda_start = 0, pda, cpda, cpda_end;
    bool raid_support = fcns_raid_enabled(INT_NS_ID);
    u16 parity_die_idx = FTL_DIE_CNT;
    u16 parity_die_pln_idx = FTL_PN_CNT;
#ifdef SKIP_MODE
	u32 die_pn_idx;
	u8	*df_ptr;
	u32 idx, off;
#endif

    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};

    ftl_apl_trace(LOG_ALW, 0x7637, "[IN] ftl_page_aux_build_p2l");

    lda_buf_start  = (lda_t*)ddtag2mem(lda_dtag_start);
    pda_buf_start  = (pda_t*)ddtag2mem(pda_dtag_start);
    pgsn_buf_start = (u64*)ddtag2mem(pgsn_dtag_start);

    memset(&lda_buf_start[grp_id*LDA_GRP_TABLE_ENTRY],   0xFF, LDA_GRP_TABLE_SIZE);
    memset(&pda_buf_start[grp_id*PDA_GRP_TABLE_ENTRY],   0xFF, PDA_GRP_TABLE_SIZE);
    memset(&pgsn_buf_start[grp_id*PGSN_GRP_TABLE_ENTRY], 0xFF, PGSN_GRP_TABLE_SIZE);

    pda_start = ftl_set_blk2pda(pda_start, spb_id);
    pda_start = ftl_set_pg2pda(pda_start, (grp_id*shr_wl_per_p2l*FTL_PG_IN_WL_CNT));

    cpda     = ftl_pda2cpda(pda_start, V_MODE);
    cpda_end = cpda + FTL_DU_PER_GRP;

    #if 0
    ftl_apl_trace(LOG_ALW, 0x0cd8, "pda_start : 0x%x, cpda start : 0x%x, cpda end : 0x%x",pda_start, cpda, cpda_end);
    #endif

    // -----find which die write raid parity first-----
    //calculate parity die
    if(raid_support){
        parity_die_idx = FTL_DIE_CNT - 1;
        while(parity_die_idx >= 0)
        {
#ifdef SKIP_MODE
		pn_ptr = parity_die_idx << FTL_PN_SHIFT;
		pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
		pn_pair = BLOCK_NO_BAD;
#endif

            if (pn_pair == BLOCK_ALL_BAD) {
                parity_die_idx--;
            } else {
                bool ret = false;
                for (u8 pl = 0 ; pl < shr_nand_info.geo.nr_planes; pl++){
                    if (((pn_pair >> pl)&1)==0){ //good plane
                        parity_die_pln_idx = pl;
                        ret = true;
                            break;
                    }
                }
                sys_assert(ret);
                break;
            }
        }
    }
    // -----start to send read cmd-----
#ifdef SKIP_MODE
		df_ptr = ftl_get_spb_defect(spb_id);
#endif
#if (WL_DETECT == mENABLE)
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	u16  wl_idx,die_idx;
	bool type = INV_U8;
    if(epm_ftl_data->host_open_blk[0] == spb_id)
    {
		type = 0;
    }
    else if(epm_ftl_data->gc_open_blk[0] == spb_id)
    {
		type = 1;
    }

    // detect incomplete WL position first
    pda = pda_start;
    grp_first_wl = ftl_pda2wl(pda);
	for (grp_wl = 0; grp_wl < shr_wl_per_p2l; grp_wl++) //pre grp
	{
    	for(die_pn_idx = 0; die_pn_idx < FTL_TOT_PN_CNT ; die_pn_idx++) //pre wl
    	{
			pda = ftl_set_pn2pda(pda,  (die_pn_idx%FTL_PN_CNT));
			pda = ftl_set_die2pda(pda, (die_pn_idx>>FTL_PN_SHIFT));

			idx = die_pn_idx >> 3;
			off = die_pn_idx & (7);
			if((ftl_pda2die(pda) == parity_die_idx && ftl_pda2plane(pda) == parity_die_pln_idx) ||
		   (((df_ptr[idx] >> off)&1)!=0))
			{
			    continue;
			}

			grp_pda = pda + (grp_wl * shr_nand_info.bit_per_cell << shr_nand_info.pda_page_shift);
            if(!ssread(grp_pda))
            {
            	cur_grp_pl_idx = die_pn_idx + grp_wl * FTL_TOT_PN_CNT;
                // is incomplete wl
                bmap_idx = cur_grp_pl_idx >> 5;
            	bmap_off = cur_grp_pl_idx & (31);
                incomplet_wl_bitmap[bmap_idx] |= (1<<bmap_off);

                wl_idx  = ftl_pda2wl(grp_pda);
                die_idx = ftl_pda2die(grp_pda);
                #if (SPOR_NON_PLP_LOG == mENABLE)
                ftl_apl_trace(LOG_ALW, 0x9193, "[Inc-WL] grp_pda:0x%x cur_grp_pl_idx:%d die:%d wl:%d",
                              grp_pda,cur_grp_pl_idx, die_idx,wl_idx);
                #endif
				ftl_set_non_plp_info(wl_idx, die_idx,grp_pda,type,0);
            }
    	}
	}

    #if 0
    for(die_pn_idx = 0; die_pn_idx < FTL_TOT_PN_CNT ; die_pn_idx++)
    {
        pda = ftl_set_pn2pda(pda,  (die_pn_idx%FTL_PN_CNT));
        pda = ftl_set_die2pda(pda, (die_pn_idx>>FTL_PN_SHIFT));

        idx = die_pn_idx >> 3;
     	off = die_pn_idx & (7);
      	if((ftl_pda2die(pda) == parity_die_idx) ||
           (((df_ptr[idx] >> off)&1)!=0))
        {
            continue;
        }
        for (grp_wl = 0; grp_wl < shr_wl_per_p2l; grp_wl++)
        {
            grp_pda = pda + (grp_wl * shr_nand_info.bit_per_cell << shr_nand_info.pda_page_shift);
            if(!ssread(grp_pda))
            {
                // is incomplete wl
                bmap_idx = die_pn_idx >> 5;
            	bmap_off = die_pn_idx & (31);
                incomplet_wl_bitmap[bmap_idx] |= (1<<bmap_off);
                ftl_apl_trace(LOG_ALW, 0x3163, "[Inc-WL] die_pn : 0x%x, bmap_idx : 0x%x,bmap_off : 0x%x",\
                              die_pn_idx, bmap_idx, bmap_off);
            }
        }
    }
    #endif
#endif
    while(cpda < cpda_end)
    {
        pda = ftl_cpda2pda(cpda, V_MODE);
#ifdef SKIP_MODE
				die_pn_idx = (ftl_pda2die(pda) << FTL_PN_SHIFT) | ftl_pda2plane(pda);
				idx = die_pn_idx >> 3;
				off = die_pn_idx & (7);
#ifdef WL_DETECT
				cur_wl_idx = ftl_pda2wl(pda);
				cur_grp_pl_idx = die_pn_idx + (cur_wl_idx - grp_first_wl) * FTL_TOT_PN_CNT;
                bmap_idx = cur_grp_pl_idx >> 5;
                bmap_off = cur_grp_pl_idx & (31);
      	        if((ftl_pda2die(pda) == parity_die_idx && ftl_pda2plane(pda) == parity_die_pln_idx) ||
                    (((df_ptr[idx] >> off)&1)!=0) ||
                    (((incomplet_wl_bitmap[bmap_idx] >> bmap_off)&1)!=0))

#else
				if((ftl_pda2die(pda) == parity_die_idx && ftl_pda2plane(pda) == parity_die_pln_idx) ||
					(((df_ptr[idx] >> off)&1)!=0))
#endif
				{
					// is bad block skip to next page entry
					cpda+=DU_CNT_PER_PAGE;
					continue;
				}
#endif

        #if 0
        ftl_apl_trace(LOG_ALW, 0x6036, "pda : 0x%x, cpda : 0x%x", pda, cpda);
        #endif

        ftl_send_read_ncl_form(NCL_CMD_SPOR_SCAN_PG_AUX, &dtag, pda, DU_CNT_PER_PAGE, ftl_page_aux_build_p2l_done);
        cpda+=DU_CNT_PER_PAGE;
    }

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());
}




/*!
 * @brief ftl spor function
 * dbg useage, to see print P2L table content
 *
 * @return	not used
 */
init_code void ftl_print_p2l_info(u16 spb_id, u32 nsid, u16 wl_start, u16 wl_cnt)
{
#if 0

    #if 1
    u32 pg_buf_idx = 0;
    pda_t *pda_buf = NULL;
    u64   *pgsn_buf = NULL;
    pda_t  pda;
    u64   pg_sn;
    #endif

    u32 lda_buf_idx = 0;
    lda_t *lda_buf = NULL;
    lda_t lda;

    u32   idx_start;
    u32   threshold;

    ftl_apl_trace(LOG_ALW, 0x7fb9, "[IN] ftl_print_p2l_info");
    //ftl_apl_trace(LOG_ALW, 0, "Blk idx : 0x%x, type : 0x%x", blk_idx, blk_type);
  #if 0
    lda_buf  = ftl_dbg_p2l_info.lda_buf;
    //pgsn_buf = ftl_dbg_p2l_info.pgsn_buf;
    //pda_buf  = ftl_dbg_p2l_info.pda_buf;
  #else
    u8  blk_type = 0;
    if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER))
    {
        blk_type = FTL_BLK_TYPE_HOST;
    }
    else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
    {
        blk_type = FTL_BLK_TYPE_GC;
    }

    switch (blk_type)
    {
        case FTL_BLK_TYPE_HOST:
        {
            lda_buf  = ftl_host_p2l_info.lda_buf;
            pgsn_buf = ftl_host_p2l_info.pgsn_buf;
            pda_buf  = ftl_host_p2l_info.pda_buf;
            break;
        }

        case FTL_BLK_TYPE_GC:
        {
            lda_buf  = ftl_gc_p2l_info.lda_buf;
            pgsn_buf = ftl_gc_p2l_info.pgsn_buf;
            pda_buf  = ftl_gc_p2l_info.pda_buf;
            break;
        }

        default:
            ftl_apl_trace(LOG_PANIC, 0xb1e2, "blk type incorrect");
            break;
    }
  #endif

    //========================================
    idx_start = wl_start * LDA_GRP_TABLE_ENTRY;
    threshold = idx_start + (LDA_GRP_TABLE_ENTRY*wl_cnt);
    for(lda_buf_idx = idx_start; lda_buf_idx < threshold; lda_buf_idx++)
    {
       lda = lda_buf[lda_buf_idx];
       ftl_apl_trace(LOG_ALW, 0x4012, "lda_buf[0x%x] : 0x%x",\
                     lda_buf_idx, lda);
    } // for(lda_buf_idx = 0; lda_buf_idx < threshold; lda_buf_idx++)

    #if 1
    idx_start = wl_start * PGSN_GRP_TABLE_ENTRY;
    threshold = idx_start + (PGSN_GRP_TABLE_ENTRY*wl_cnt);
    for(pg_buf_idx = idx_start; pg_buf_idx < threshold; pg_buf_idx++)
    {
        pg_sn = pgsn_buf[pg_buf_idx];
        pda   = pda_buf[pg_buf_idx];
        ftl_apl_trace(LOG_ALW, 0x7431, "P2L table - Pg idx : 0x%x, PDA : 0x%x, PGSN : 0x%x-%x",\
                      pg_buf_idx, pda, (pg_sn>>32), (pg_sn&INV_U32));
    } // for(pg_buf_idx = 0; pg_buf_idx < threshold; pg_buf_idx++)
    #endif
    //================================================
#endif
}
/*!
 * @brief ftl spor function
 * dbg useage, to see print P2L table content
 *
 * @return	not used
 */

init_code void ftl_p2l_aux_comapare(u16 spb_id, u16 wl_start, u16 wl_cnt)
{
#if (SPOR_AUX_MODE_DBG == mENABLE)
    #if 1
    pda_t *pda_buf_start = NULL;
    u64   *pgsn_buf_start = NULL;
    u32 pg_buf_idx = 0;
    pda_t  pda;
    u64   pg_sn;
    #endif

    #if 1
    u32    lda_buf_idx = 0;
    lda_t *lda_buf_start = NULL;
    lda_t  lda;
    #endif

    u8 blk_type = 0;
    u32 threshold;
    u32 idx_start;

    ftl_apl_trace(LOG_ALW, 0xdea0, "[IN] ftl_p2l_aux_comapare, wl idx : 0x%x", wl_start);

    if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER))
    {
        blk_type = FTL_BLK_TYPE_HOST;
    }
    else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
    {
        blk_type = FTL_BLK_TYPE_GC;
    }

    switch (blk_type)
    {
        case FTL_BLK_TYPE_HOST:
        {
            lda_buf_start  = ftl_host_p2l_info.lda_buf;
            pgsn_buf_start = ftl_host_p2l_info.pgsn_buf;
            pda_buf_start  = ftl_host_p2l_info.pda_buf;
            break;
        }

        case FTL_BLK_TYPE_GC:
        {
            lda_buf_start  = ftl_gc_p2l_info.lda_buf;
            pgsn_buf_start = ftl_gc_p2l_info.pgsn_buf;
            pda_buf_start  = ftl_gc_p2l_info.pda_buf;
            break;
        }

        default:
            ftl_apl_trace(LOG_PANIC, 0x4326, "blk type incorrect");
            break;
    }


    #if 1
    // check LDA buf
    ftl_apl_trace(LOG_ALW, 0x3e25, "comapare lda buffer start");

    idx_start = wl_start * LDA_GRP_TABLE_ENTRY;
    threshold = idx_start + (LDA_GRP_TABLE_ENTRY*wl_cnt);
    for(lda_buf_idx = idx_start; lda_buf_idx < threshold; lda_buf_idx++)
    {
        lda = lda_buf_start[lda_buf_idx];
        if((lda_buf_start[lda_buf_idx] < _max_capacity) ||
           (ftl_dbg_p2l_info.lda_buf[lda_buf_idx] < _max_capacity))
        {
            if(lda_buf_start[lda_buf_idx] != lda)
            {
                ftl_apl_trace(LOG_ALW, 0xdbd3, "LDA not matched! lda_buf_idx: 0x%x, p2l_buf : 0x%x, dbg_buf : 0x%x",\
                              lda_buf_idx, lda, ftl_dbg_p2l_info.lda_buf[lda_buf_idx]);
            }
        }
    }
    #endif


    #if 1
    // check pgsn buf & pda buf
    ftl_apl_trace(LOG_ALW, 0xf02b, "comapare pda&pgsn buffer start");

    idx_start = wl_start * PGSN_GRP_TABLE_ENTRY;
    threshold = idx_start + (PGSN_GRP_TABLE_ENTRY*wl_cnt);
    for(pg_buf_idx = idx_start;pg_buf_idx < threshold;pg_buf_idx++)
    {
        pda = pda_buf_start[pg_buf_idx];
        pg_sn = pgsn_buf_start[pg_buf_idx];

        if(ftl_dbg_p2l_info.pda_buf[pg_buf_idx] != pda)
        {
            ftl_apl_trace(LOG_ALW, 0x2b0d, "PDA not matched! pg_buf_idx: 0x%x, pda_buf : 0x%x, dbg_buf : 0x%x",\
                          pg_buf_idx, pda, ftl_dbg_p2l_info.pda_buf[pg_buf_idx]);
        }

        if(ftl_dbg_p2l_info.pgsn_buf[pg_buf_idx] != pg_sn)
        {
            ftl_apl_trace(LOG_ALW, 0xfcb6, "PGSN not matched! pg_buf_idx: 0x%x, pgsn_buf : 0x%x-%x, dbg_buf : 0x%x-%x",\
                          pg_buf_idx, (pg_sn>>32), (pg_sn&INV_U32),\
                          (ftl_dbg_p2l_info.pgsn_buf[pg_buf_idx]>>32), (ftl_dbg_p2l_info.pgsn_buf[pg_buf_idx]&INV_U32));
        }

        if((ftl_dbg_p2l_info.pda_buf[pg_buf_idx] == INV_U32) &&
           (pda == INV_U32))
        {
            ftl_apl_trace(LOG_ALW, 0x88b2, "stopped buffer pg_buf_idx : 0x%x", pg_buf_idx);
            break;
        }
    }
    #endif

#endif
}

ddr_code void ftl_vc_tbl_print(void)
{
#if 1
    u8  fail_flag = mFALSE;
    u16 spb_id;
    u32 cnt;
    u32 *vc;
    dtag_t dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);

    ftl_apl_trace(LOG_ALW, 0x9436, "[IN] ftl_vc_tbl_print");

    spb_id = 0;

    while(spb_id < get_total_spb_cnt())
    {
        if(spb_id + 4 < get_total_spb_cnt()){
            ftl_apl_trace(LOG_ALW, 0xe2dc, "Spb : %d, Vac : 0x%x 0x%x 0x%x 0x%x 0x%x",
                spb_id, vc[spb_id], vc[spb_id+1], vc[spb_id+2], vc[spb_id+3], vc[spb_id+4]);
        }else{
            ftl_apl_trace(LOG_ALW, 0x927f, "Spb : %d, Vac : 0x%x",spb_id, vc[spb_id]);
        }

		if ((vc[spb_id] > FTL_DU_PER_TLC_SBLOCK) || 
            ((spb_id + 4 < get_total_spb_cnt()) && ((vc[spb_id+1] > FTL_DU_PER_TLC_SBLOCK) 
                || (vc[spb_id+2] > FTL_DU_PER_TLC_SBLOCK) || (vc[spb_id+3] > FTL_DU_PER_TLC_SBLOCK) 
                || (vc[spb_id+4] > FTL_DU_PER_TLC_SBLOCK))))
        {
            fail_flag = mTRUE;
            ftl_apl_trace(LOG_ALW, 0x6dcc, "Vac abnormal!!");
        }
        if (spb_id + 4 < get_total_spb_cnt())
            spb_id+=5;
        else
            spb_id++;
    }
    //ftl_apl_trace(LOG_ALW, 0, "Spb : 0x%x, Vac : 0x%x", spb_id, vc[spb_id]);

    ftl_l2p_put_vcnt_buf(dtag, cnt, false);

    if(fail_flag)
    {
        sys_assert(0);
    }

#endif
}



ddr_code void ftl_l2p_print(void)
{
#if 1
    u32* l2p_base = (u32*)(ddtag2off(shr_l2p_entry_start) | 0x40000000);
    ftl_apl_trace(LOG_ALW, 0x7949, "max Laa: 0x%x, l2p(0~4): 0x%x 0x%x 0x%x 0x%x 0x%x",
        _max_capacity, *l2p_base, *(l2p_base+1), *(l2p_base+2), *(l2p_base+3), *(l2p_base+4));
#else
    //u8  win = 0;
    u32 laa = 0;
    u64 addr_base;
    u64 addr;
    u64 l2p_base = ddtag2off(shr_l2p_entry_start);

    ftl_apl_trace(LOG_ALW, 0x08be, "[IN] ftl_l2p_print, max Laa : 0x%x", _max_capacity);


    addr_base = (l2p_base | 0x40000000);

    while(laa < 5)
    {
        addr = addr_base + ((u64)laa * sizeof(lda_t));
        ftl_apl_trace(LOG_ALW, 0xd803, "LAA : 0x%x, L2P PDA : 0x%x", laa, *((u32*)(u32)addr));
        laa++;
    }

    #if 0
    laa = _max_capacity - 10;
    while(laa < _max_capacity)
    {
        addr = addr_base + ((u64)laa * sizeof(lda_t));
        win = 0;
        while (addr >= 0xC0000000) {
    		addr -= 0x80000000;
    		win++;
    	}

        mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
    	u8 old = ctrl0.b.cpu3_ddr_window_sel;
    	ctrl0.b.cpu3_ddr_window_sel = win;
    	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        ftl_apl_trace(LOG_ALW, 0x6417, "LAA : 0x%x, L2P PDA : 0x%x", laa, *((u32*)(u32)addr));
        ctrl0.b.cpu3_ddr_window_sel = old;
    	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        laa++;
    }
    #endif
#endif
}

ddr_code void ftl_l2p_print_lda0(void)
{
	u32 laa = 0;
	u64 addr_base;
	u64 addr;
	u64 l2p_base = ddtag2off(shr_l2p_entry_start);
	addr_base = (l2p_base | 0x40000000);
    addr = addr_base + ((u64)laa * sizeof(lda_t));
    ftl_apl_trace(LOG_ALW, 0x1420, "LAA : 0x%x, L2P PDA : 0x%x", laa, *((u32*)(u32)addr));
}

init_code void ftl_p2l_vac_chk(u16 spb_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start)
{
#if 0
    lda_t *lda_buf_start  = NULL;
    //u32   vac_cnt = 0;
    u8 idx;

    ftl_apl_trace(LOG_ALW, 0x009a, "[IN] ftl_p2l_vac_chk");

    lda_buf_start  = (lda_t*)ddtag2mem(lda_dtag_start);

    #if 1
    pda_t pda;
    u32   pgsn_buf_idx = 0;
    u32   wl_vac_cnt = 0;
    pda_t *pda_buf_start  = NULL;
    //u64   *pgsn_buf_start = NULL;

    pda_buf_start  = (pda_t*)ddtag2mem(pda_dtag_start);
    //pgsn_buf_start = (u64*)ddtag2mem(pgsn_dtag_start);
    //==================================
    while(pgsn_buf_idx < PGSN_FULL_TABLE_ENTRY)
    {
        pda = pda_buf_start[pgsn_buf_idx];
        for(idx=0; idx<DU_CNT_PER_PAGE; idx++)
        {
            if(lda_buf_start[(pda&FTL_PDA_DU_MSK)] < _max_capacity)
            {
                wl_vac_cnt++;
            }
            pda++;
        }

        pgsn_buf_idx++;
        if(pda_buf_start[pgsn_buf_idx] == INV_U32)
        {
            pgsn_buf_idx = ((pgsn_buf_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY;
        }
    }

    ftl_apl_trace(LOG_ALW, 0x15b2, "spb id : 0x%x, p2l total wl vac cnt : 0x%x", spb_id, wl_vac_cnt);
    #endif
    //==================================

    #if 0
    for(idx = 0;idx < LDA_FULL_TABLE_ENTRY;idx++)
    {
        if(lda_buf_start[idx] < _max_capacity)
        {
            vac_cnt++;
        }
    }

    ftl_apl_trace(LOG_ALW, 0xd442, "spb id : 0x%x, p2l total lda vac cnt : 0x%x", spb_id, vac_cnt);
    #endif



#endif
}

/*!
 * @brief ftl spor function
 * read p2l table done
 *
 * @return	not used
 */
init_code u8 ftl_spor_p2l_fail_chk(u16 grp_idx)
{
    if((ftl_p2l_fail_grp_flag[grp_idx] == mFALSE) &&
       (grp_idx < shr_p2l_grp_cnt))
    {
        ftl_p2l_fail_grp_flag[grp_idx] = mTRUE;
        ftl_p2l_fail_cnt++;
        return mTRUE;
    }

    return mFALSE;
}

/*!
 * @brief ftl spor function
 * read p2l table done
 *
 * @return	not used
 */
#if(PLP_SUPPORT == 1)  
fast_code void ftl_read_p2l_from_nand_done(struct ncl_cmd_t *ncl_cmd)
#else
init_code void ftl_read_p2l_from_nand_done(struct ncl_cmd_t *ncl_cmd)
#endif
{
    u8  blank_flag = mFALSE, uecc_flag = mFALSE, meta_crc_flag = mFALSE;
    u8  i, length;
    u8  cmd_idx;
    u16 grp_idx;
    pda_t pda;
    struct info_param_t *info_list;

    pda       = (pda_t)ncl_cmd->caller_priv;
    cmd_idx   = ncl_cmd->user_tag_list[0].pl.btag;
    info_list = ncl_cmd->addr_param.common_param.info_list;
    length    = ncl_cmd->addr_param.common_param.list_len;

    // ToDo : current ncl cmd error interrupt might have problem, by Tony
    // ============ Error handle area ============

    #if NCL_FW_RETRY
    //if((ncl_cmd->op_code != NCL_CMD_SPOR_SCAN_BLANK_POS) &&
    //   (ncl_cmd->op_code != NCL_CMD_SPOR_P2L_READ_POS))
    if(ncl_cmd->status)
    {
        u8  du_is_blank = mTRUE;
        for(i=0; i < length; i++)
        {
            if(info_list[i].status != ficu_err_du_erased)
            {
                du_is_blank = mFALSE;
            }
        }

        if(du_is_blank)
        {
            goto EXTRACT_P2L_INFO;
        }


        extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
        if (ncl_cmd->retry_step == default_read)
        {
        	if(spor_skip_raid_tag)
        	{
				ftl_apl_trace(LOG_ALW, 0xf94e, "scan blank pos,skip retry!! pda:0x%x,wl:%d die:%d",pda,ftl_pda2wl(pda),ftl_pda2die(pda));
				goto EXTRACT_P2L_INFO;
        	}
            rd_err_handling(ncl_cmd);
            return;
        }
        else if(ncl_cmd->retry_step != raid_recover_fail)
        {
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && ftl_uc_pda_chk(ncl_cmd) && (rced == false))
            //if (fcns_raid_enabled(nsid) && (rced == false))
            {
            	for (i = 0; i < length; i++)
                {
        		    if (info_list[i].status > ficu_err_du_ovrlmt)
                    {
                        rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
                        raid_correct_push(rc_req);
                        return;
                    }
                }
            }
        }
    }
    #endif


EXTRACT_P2L_INFO:

    for(i=0; i<length; i++)
    {
        if(info_list[i].status == cur_du_erase) // Blank
        {
            blank_flag = mTRUE;
        }
        else if(info_list[i].status >= cur_du_ucerr) // UECC
        {
            uecc_flag = mTRUE;
        }
        else if(info_list[i].status == cur_du_dfu_err) // meta CRC error
        {
            meta_crc_flag = mTRUE;
        }
    }
    // ============ Error handle area ============

    if((blank_flag == mTRUE) || (uecc_flag == mTRUE) || (meta_crc_flag == mTRUE))
    {

	    if((ncl_cmd->op_code == NCL_CMD_SPOR_P2L_READ) &&
	       ((pda == ftl_spor_info->last_grp_lda_buf_pda) ||
	       (pda == ftl_spor_info->last_grp_pgsn_buf_pda) ||
	       (pda == ftl_spor_info->last_grp_pda_buf_pda)))
        {
            grp_idx = (shr_p2l_grp_cnt-1);
        }
        else
        {
            grp_idx = ftl_pda2grp(pda)-1;
        }

        if(blank_flag == mTRUE)
        {
            // record first blank pos
            u16 pg, open_pg;
            ftl_spor_info->read_data_blank = mTRUE;

            open_pg = ftl_pda2page(ftl_spor_info->blank_pda);
            pg      = ftl_pda2page(pda);
            if((ftl_spor_info->blank_pda == INV_U32) ||
               (open_pg > pg))
            {
                ftl_spor_info->blank_pda = pda;
            }
            	//add by Jay for spor fill dummy
			if((spor_die_sch_wl == true) && (ftl_pda2du(pda) == 0))
			{
				u16 die_id;
				u32 *spor_bitmap = spor_dummy_bit;
				die_id = ftl_pda2die(pda);
				set_bit(die_id, spor_bitmap);
				//*spor_bitmap |= BIT(die_id);
			}
        }
        else
        {
            ftl_spor_info->read_data_fail = mTRUE;

        }
#if (PLP_SUPPORT == 0)
		if(pda2blk(pda) != cur_blank_spb)
		{
			cur_blank_spb = pda2blk(pda);
			cur_blank_grp = grp_idx;
		}
		else
		{
			cur_blank_grp = min(cur_blank_grp,grp_idx);
		}
#endif

        ftl_spor_p2l_fail_chk(grp_idx);
    }
    else
    {
        #if (SPOR_PLP_WL_CHK_COMPLETE == mENABLE)
        if(ncl_cmd->op_code == NCL_CMD_SPOR_P2L_READ_POS)
        #endif
        {
            u32 meta_idx;
            u16 spb_id;
            lda_t lda;

            spb_id = pda2blk(pda);
            meta_idx = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
            meta_idx -= ftl_spor_meta_idx;
            lda      = ftl_spor_meta[meta_idx].lda;

            //ftl_apl_trace(LOG_ALW, 0, "[p2l_done] pda : 0x%x, lda : 0x%x", pda, lda);
            // if current blk is PLP blk
			if(((lda == FTL_PLP_TAG)||((lda & ~FTL_PLP_GROUP_MASK) == FTL_PLP_FORCE_TAG))  && 
               ((ftl_build_tbl_type[spb_id] != FTL_SN_TYPE_PLP_USER) &&
               (ftl_build_tbl_type[spb_id] != FTL_SN_TYPE_PLP_GC)))
            {

            	if(spor_skip_raid_tag) //add by Jay 2023.07.24
            	{
					ftl_apl_trace(LOG_INFO, 0x4557, "raid PLP_TAG skip set blk type pda:0x%x ",pda); 
            	}
            	else
            	{
					switch(ftl_build_tbl_type[spb_id])
				   {
					   case FTL_SN_TYPE_USER:
					   case FTL_SN_TYPE_OPEN_USER:
						   ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_PLP_USER;
						   break;

					   case FTL_SN_TYPE_GC:
					   case FTL_SN_TYPE_OPEN_GC:
						   ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_PLP_GC;
						   break;

					   default:
#if (SPOR_ASSERT == mENABLE)
						   panic("incorrect ftl sn type!\n");
#else
						   ftl_apl_trace(LOG_PANIC, 0xed4c, "incorrect ftl sn type!");
#endif
						   break;
				   }

            	}

            }

            u16 cur_pg, prev_pg;
            if((lda == P2L_LDA) || (lda == FTL_PLP_TAG))
            {
                // record last P2L pos, for fill dummy not completed case
                prev_pg = ftl_pda2page(ftl_spor_info->p2l_pda);
                cur_pg  = ftl_pda2page(pda);
                if((ftl_spor_info->p2l_pda == INV_U32) ||
                   (cur_pg > prev_pg))
                {
                    ftl_spor_info->p2l_pda = pda;
                }

            }
            else if((lda & ~FTL_PLP_GROUP_MASK) == FTL_PLP_FORCE_TAG) 
			{
				ftl_spor_info->plp_force_p2l_pda = pda; 
				//ftl_apl_trace(LOG_INFO, 0, "read plp force p2l success pda:0x%x lda:0x%x group:%d",pda,lda,lda&FTL_PLP_GROUP_MASK); 
			}
            else if(lda == SPOR_DUMMY_LDA)
            {
                // record last dummy pos
                prev_pg = ftl_pda2page(ftl_spor_info->last_dummy_pda);
                cur_pg  = ftl_pda2page(pda);
                if((ftl_spor_info->last_dummy_pda == INV_U32) ||
                   (cur_pg > prev_pg))
                {
                    ftl_spor_info->last_dummy_pda = pda;
                }

                // record first dummy pos
                prev_pg = ftl_pda2page(ftl_spor_info->first_dummy_pda);
                cur_pg  = ftl_pda2page(pda);
                if((ftl_spor_info->first_dummy_pda == INV_U32) ||
                   (cur_pg < prev_pg))
                {
                    ftl_spor_info->first_dummy_pda = pda;
                }
            }
        }
    }

    _spor_free_cmd_bmp |= (1 << cmd_idx);
}

/*!
 * @brief ftl spor function
 * read p2l table from NAND
 *
 * @return	not used
 */
init_code void ftl_read_p2l_from_nand(u16 spb_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start, u32 nsid)
{
    pda_t  lda_buf_pda;
    pda_t  pgsn_buf_pda;
    pda_t  pda_buf_pda;
    u16    grp_idx;
    dtag_t dtag;

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_read_p2l_from_nand");

    for(grp_idx = 0; grp_idx < shr_p2l_grp_cnt; grp_idx++)
    {
        ftl_p2l_position_detect(grp_idx, spb_id, nsid, &lda_buf_pda, &pgsn_buf_pda);
        pda_buf_pda = pgsn_buf_pda + PGSN_GRP_TABLE_DU_CNT;

        if(grp_idx == (shr_p2l_grp_cnt-1))
        {
            ftl_spor_info->last_grp_lda_buf_pda  = lda_buf_pda;
            ftl_spor_info->last_grp_pgsn_buf_pda = pgsn_buf_pda;
            ftl_spor_info->last_grp_pda_buf_pda  = pda_buf_pda;
        }

        dtag.dtag = ((lda_dtag_start+(grp_idx*LDA_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, lda_buf_pda, LDA_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

        dtag.dtag = ((pgsn_dtag_start+(grp_idx*PGSN_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pgsn_buf_pda, PGSN_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

        dtag.dtag = ((pda_dtag_start+(grp_idx*PDA_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pda_buf_pda, PDA_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

        #if 0 // for dbg
        ftl_apl_trace(LOG_ALW, 0x8d3b, "wl_idx : 0x%x, lda_buf_pda : 0x%x, pgsn_buf_pda : 0x%x, pda_buf_pda : 0x%x",\
                      wl_idx, lda_buf_pda, pgsn_buf_pda, pda_buf_pda);
        #endif

        if(ftl_spor_info->read_data_blank) {break;}
	}
    // ----- wait all form done -----
    wait_spor_cmd_idle();

    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
}

/*!
 * @brief ftl spor function
 * scan first blank pos for open/plp blk
 *
 * @return	not used
 */
init_code void ftl_scan_first_blank_p2l_pos(u16 spb_id, u32 nsid)
{
    pda_t  lda_buf_pda;
    pda_t  pgsn_buf_pda;
    pda_t  pda_buf_pda;
    u16    grp_idx;
    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};

    ftl_apl_trace(LOG_ALW, 0x648b, "[IN] ftl_scan_first_blank_p2l_pos");
    ftl_spor_info->read_data_fail = mFALSE;

    for(grp_idx = 0; grp_idx < shr_p2l_grp_cnt; grp_idx++)
    {
        ftl_p2l_position_detect(grp_idx, spb_id, nsid, &lda_buf_pda, &pgsn_buf_pda);
        pda_buf_pda = pgsn_buf_pda + PGSN_GRP_TABLE_DU_CNT;

        if(grp_idx == (shr_p2l_grp_cnt-1))
        {
            ftl_spor_info->last_grp_lda_buf_pda  = lda_buf_pda;
            ftl_spor_info->last_grp_pgsn_buf_pda = pgsn_buf_pda;
            ftl_spor_info->last_grp_pda_buf_pda  = pda_buf_pda;
        }

        ftl_p2l_pos_list[grp_idx].lda_pda  = lda_buf_pda;
        ftl_p2l_pos_list[grp_idx].pgsn_pda = pgsn_buf_pda;
        //ftl_apl_trace(LOG_ALW, 0, "Current grp_idx : %d, p2l_pda : 0x%x, pgsn_pda : 0x%x", grp_idx, lda_buf_pda, pgsn_buf_pda);

        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ_POS, &dtag, lda_buf_pda,  LDA_GRP_TABLE_DU_CNT,  ftl_read_p2l_from_nand_done);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ_POS, &dtag, pgsn_buf_pda, PGSN_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ_POS, &dtag, pda_buf_pda,  PDA_GRP_TABLE_DU_CNT,  ftl_read_p2l_from_nand_done);

        // stop send ncl form, as reach first blank position
        if(ftl_spor_info->blank_pda != INV_U32) {break;}
	}

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());

    ftl_spor_info->read_data_fail = mFALSE;
}


/*!
 * @brief ftl spor function
 * read p2l table from NAND
 *
 * @return	not used
 */
init_code void ftl_read_p2l_from_nand_with_list(u16 spb_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start, u32 nsid, u16 grp_threshold)
{
    pda_t  lda_buf_pda;
    pda_t  pgsn_buf_pda;
    pda_t  pda_buf_pda;
    u16    grp_idx;
    dtag_t dtag;

    //ftl_apl_trace(LOG_ALW, 0, "[IN]ftl_read_p2l_from_nand_with_list");

    // notice : wl(n) p2l table is place at wl(n+1); there is no p2l table written in wl(0)

    ftl_apl_trace(LOG_ALW, 0xd485, "p2l_list grp_threshold : %d", grp_threshold);
#if (PLP_SUPPORT == 0)
    if(grp_threshold <= 1) {return;}
    grp_threshold--;
#else
    if(grp_threshold == 0) {return;}
#endif
    for(grp_idx = 0; grp_idx < grp_threshold; grp_idx++)
    {
        lda_buf_pda  = ftl_p2l_pos_list[grp_idx].lda_pda;
        pgsn_buf_pda = ftl_p2l_pos_list[grp_idx].pgsn_pda;

        //ftl_apl_trace(LOG_ALW, 0, "Current grp_idx : %d, p2l_pda : 0x%x, pgsn_pda : 0x%x", grp_idx, p2l_pda, pgsn_pda);

        dtag.dtag = ((lda_dtag_start+(grp_idx*LDA_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, lda_buf_pda,  LDA_GRP_TABLE_DU_CNT,  ftl_read_p2l_from_nand_done);

        dtag.dtag = ((pgsn_dtag_start+(grp_idx*PGSN_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pgsn_buf_pda, PGSN_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

        pda_buf_pda = pgsn_buf_pda + PGSN_GRP_TABLE_DU_CNT;
        dtag.dtag = ((pda_dtag_start+(grp_idx*PDA_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
        ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pda_buf_pda, PDA_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

        //if(ftl_spor_info->read_data_fail) {break;}
	}

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
}

ddr_code bool ftl_plp_read_last_p2l(u16 spb_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start, u32 nsid, u16 special_grp)
{
    pda_t  lda_buf_pda;
    pda_t  pgsn_buf_pda;
    pda_t  pda_buf_pda;
    //u16    grp_idx;
    dtag_t dtag;
	ftl_spor_info->plp_force_p2l_pda = INV_U32;
    ftl_apl_trace(LOG_ALW, 0xb872, "plp special_grp : %d", special_grp);
	//plp_group = special_grp;
    //if(special_grp == 0) {return 0;}


	ftl_p2l_position_detect(shr_p2l_grp_cnt-1, spb_id, nsid, &lda_buf_pda, &pgsn_buf_pda);
    pda_buf_pda = pgsn_buf_pda + PGSN_GRP_TABLE_DU_CNT;

	ftl_apl_trace(LOG_ALW, 0x4205, "last p2l position lda_buf_pda 0x%x,pgsn_buf_pda 0x%x", lda_buf_pda,pgsn_buf_pda);

    ftl_spor_info->last_grp_lda_buf_pda  = lda_buf_pda;
    ftl_spor_info->last_grp_pgsn_buf_pda = pgsn_buf_pda;
    ftl_spor_info->last_grp_pda_buf_pda  = pda_buf_pda;

        //lda_buf_pda  = ftl_p2l_pos_list[grp_idx].lda_pda;
        //pgsn_buf_pda = ftl_p2l_pos_list[grp_idx].pgsn_pda;

  	//ftl_apl_trace(LOG_ALW, 0, "Current grp_idx : %d, p2l_pda : 0x%x, pgsn_pda : 0x%x", grp_idx, p2l_pda, pgsn_pda);

    dtag.dtag = ((lda_dtag_start+(special_grp*LDA_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
    ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, lda_buf_pda,  LDA_GRP_TABLE_DU_CNT,  ftl_read_p2l_from_nand_done);

    dtag.dtag = ((pgsn_dtag_start+(special_grp*PGSN_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
    ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pgsn_buf_pda, PGSN_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

    pda_buf_pda = pgsn_buf_pda + PGSN_GRP_TABLE_DU_CNT;
    dtag.dtag = ((pda_dtag_start+(special_grp*PDA_GRP_TABLE_DU_CNT)) | DTAG_IN_DDR_MASK);
    ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pda_buf_pda, PDA_GRP_TABLE_DU_CNT, ftl_read_p2l_from_nand_done);

        //if(ftl_spor_info->read_data_fail) {break;}


    // ----- wait all form done -----
    wait_spor_cmd_idle();
    if((ftl_spor_info->read_data_fail == mTRUE) || (ftl_spor_info->plp_force_p2l_pda == INV_U32))
    	return  1;
    else
    	return 0;
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
}


/*!
 * @brief ftl spor function
 * fill dummy layer
 *
 * @return	not used
 */
init_code void ftl_func_write_done(struct ncl_cmd_t *ncl_cmd)
{
    u16 cmd_idx;
    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_fill_dummy_done");
    cmd_idx  = ncl_cmd->user_tag_list[0].pl.btag;
    _spor_free_cmd_bmp |= (1 << cmd_idx);
}


slow_code void ftl_fqbt_write_done(struct ncl_cmd_t *ncl_cmd)
{
    u16 cmd_idx;
    cmd_idx  = ncl_cmd->user_tag_list[0].pl.btag;
    _spor_free_cmd_bmp |= (1 << cmd_idx);

    u8 i;
    if (ncl_cmd->status)
    {
        for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++) {
            if (ncl_cmd->addr_param.common_param.info_list[i].status != ficu_err_encoding)
            {
                ftl_apl_trace(LOG_ALW, 0xfb95, "pda: 0x%x err, status: %u", ncl_cmd->addr_param.common_param.pda_list[i], ncl_cmd->addr_param.common_param.info_list[i].status);
                shr_qbt_prog_err = true;
            }
        }
    }
}


/*!
 * @brief ftl spor function
 * fill dummy layer
 *
 * @return	not used
 */
init_code void ftl_fill_dummy_layer(pda_t pda_start, u32 dummy_du_cnt, u8 wl_align)
{
	u8	 spb_type = 0xFF;
	u8	 pn_pair, du_cnt = 0, plane_op = 0;
	u16  du_idx, cont_idx, die_id, i;
#ifdef SKIP_MODE
	u32  pn_ptr = 0;
#endif
	u16  cmd_idx, wl = 0;
	u32  meta_idx;
	pda_t cpda_idx = 0, cpda_next = 0, cpda_end, cpda_start, pda = 0;
	u16  spb_id = pda2blk(pda_start);

	u8  pln_idx;	 
	pda_t cpda_plane_first = 0; 
	u8  page_cmd_idx ;  

    u8 parity_die_pln_idx = shr_nand_info.geo.nr_planes;
    u32 parity_die = FTL_DIE_CNT;
    bool raid_support = fcns_raid_enabled(INT_NS_ID);
    if(raid_support){
        parity_die = FTL_DIE_CNT - 1;
        while(parity_die >= 0)
        {
#ifdef SKIP_MODE
            pn_ptr = parity_die<<FTL_PN_SHIFT;
            pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
            pn_pair = BLOCK_NO_BAD;
#endif
            if (pn_pair == BLOCK_ALL_BAD) {
                parity_die--;
            } else {
                bool ret = false;
                for (u8 pl = 0; pl < shr_nand_info.geo.nr_planes; pl++){
                    if (((pn_pair >> pl)&1)==0){ //good plane
                        parity_die_pln_idx = pl;
                        ret = true;
                        break;
                    }
                }
                sys_assert(ret);    
                break;
            }
        }
    }

    ftl_apl_trace(LOG_ALW, 0x5d14, "[IN] ftl_fill_dummy_layer spb %u", spb_id);

    // ===== Start fill dummy from next wl =====

    memset((u8*)ddtag2mem(ftl_dummy_dtag), 0xFF, DTAG_SZE);

    if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER))
    {
        spb_type = FTL_BLK_TYPE_HOST;
    }
    else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC) ||
            (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
    {
        spb_type = FTL_BLK_TYPE_GC;
    }


    if(wl_align)
    {
        wl = ftl_pda2wl(pda_start);
        pda = ftl_set_blk2pda(pda, spb_id);
        pda = ftl_set_pg2pda(pda, (wl*FTL_PG_IN_WL_CNT));
    }
    else
    {
        wl = ftl_pda2wl(pda_start);
        pda = pda_start;
    }

    ftl_apl_trace(LOG_ALW, 0x1add, "dummy start wl : %u, dummy_wl_cnt : %u", wl, dummy_du_cnt/FTL_DU_PER_SWL);

    cpda_start = ftl_pda2cpda(pda, V_MODE);
    //cpda_end   = cpda_start + FTL_DU_PER_LAYER;
    cpda_end   = cpda_start + dummy_du_cnt;

    if(cpda_end>((FTL_DU_PER_TLC_SBLOCK*spb_id)+FTL_DU_PER_TLC_SBLOCK)) 
    {cpda_end = (FTL_DU_PER_TLC_SBLOCK*spb_id)+FTL_DU_PER_TLC_SBLOCK;}
	pda_t pda_end = ftl_cpda2pda(cpda_end, V_MODE)-1;
    ftl_apl_trace(LOG_ALW, 0x51ef, "pda : 0x%x wl:%d pda_end:0x%x  wl:%d, cpda_start : 0x%x, cpda_end : 0x%x ",
    			pda,ftl_pda2wl(pda),pda_end,ftl_pda2wl(pda_end), cpda_start, cpda_end);
    ftl_apl_trace(LOG_ALW, 0xe198, "pda die id : 0x%x, plane : 0x%x, pg : 0x%x, du : 0x%x",\
                  ftl_pda2die(pda), ftl_pda2plane(pda), ftl_pda2page(pda), ftl_pda2du(pda));

    // ===== set 1 page meta first, all page meta in dummy is the same =====
    meta_idx = ftl_spor_meta_idx;
    for(i = 0; i < DU_CNT_PER_PAGE; i++)
    {
        ftl_set_page_meta(meta_idx, i, SPOR_DUMMY_LDA, spb_id, spb_type);
    }

    die_id = ftl_pda2die(pda);
    cpda_idx = cpda_start;
    while(cpda_idx<cpda_end)
    {
        while(die_id < FTL_DIE_CNT)
        {
#ifdef SKIP_MODE
			pn_ptr = die_id << FTL_PN_SHIFT;
			pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
			pn_pair = BLOCK_NO_BAD;
#endif

            cpda_next = cpda_idx + FTL_DU_PER_WL*FTL_PN_CNT;
            plane_op = FTL_PN_CNT; 

            if((die_id == parity_die) && raid_support){
                pn_pair |= 1 << parity_die_pln_idx; // skip parity plane
            }

			if(pn_pair == BLOCK_ALL_BAD)
			{
				die_id++;
                cpda_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
				ftl_apl_trace(LOG_ALW, 0x3142, "die:%d plane all bad",die_id); 
				continue;
			}

            // ===== prepare 2PN WL ncl cmd resource =====
            cmd_idx  = get_spor_ncl_cmd_idx();
            cont_idx = cmd_idx*FTL_DU_PER_WL*FTL_PN_CNT;
            memset(&ftl_info[cont_idx], 0, (FTL_DU_PER_WL*FTL_PN_CNT*sizeof(*ftl_info)));
             if(pn_pair == BLOCK_NO_BAD)
             {
                 for(i=0;i<FTL_PN_CNT*FTL_PG_IN_WL_CNT;i++)
                 {
                     ftl_pda_list[cont_idx+i]     = ftl_cpda2pda(cpda_idx, V_MODE);
                     ftl_info[cont_idx+i].pb_type = NAL_PB_TYPE_XLC;
                     cpda_idx+=DU_CNT_PER_PAGE;
                 }
             }
            else
            {
            	//ftl_apl_trace(LOG_ALW, 0, "fill dummy pn_pair = 0x%x",pn_pair); 
            	page_cmd_idx = 0; 
            	cpda_plane_first = cpda_idx; 
				for(pln_idx = 0;pln_idx < FTL_PN_CNT;pln_idx++) 
				{ 
					if(BIT_CHECK(pn_pair, pln_idx)) 
					{ 
						plane_op--; 
					} 
					else 
					{ 
						for(i=0;i<FTL_PG_IN_WL_CNT;i++,page_cmd_idx++) 
		                { 
		                    ftl_pda_list[cont_idx+page_cmd_idx]     = ftl_cpda2pda(cpda_idx, V_MODE); 
		                    ftl_info[cont_idx+page_cmd_idx].pb_type = NAL_PB_TYPE_XLC; 
		                    cpda_idx+=(DU_CNT_PER_PAGE<<FTL_PN_SHIFT); 
		                    //ftl_apl_trace(LOG_ALW, 0, "fill dummy pda :0x%x,die:%d,plane:%d",ftl_pda_list[cont_idx+page_cmd_idx],die_id,ftl_pda2plane(ftl_pda_list[cont_idx+page_cmd_idx])); 
		                } 
					} 
					cpda_plane_first +=DU_CNT_PER_PAGE; 
					cpda_idx = cpda_plane_first; 
				}
            }

            du_cnt = plane_op*FTL_DU_PER_WL;
            for(du_idx = 0; du_idx < du_cnt; du_idx++)
            {
                ftl_bm_pl[cont_idx+du_idx].pl.btag       = cmd_idx;
               	ftl_bm_pl[cont_idx+du_idx].pl.du_ofst    = du_idx;
               	ftl_bm_pl[cont_idx+du_idx].pl.nvm_cmd_id = meta_idx + (du_idx%DU_CNT_PER_PAGE);
    #if (SPOR_CMD_EXP_BAND == mENABLE)
                ftl_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
    #else
                ftl_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
    #endif
                ftl_bm_pl[cont_idx+du_idx].pl.dtag       = (ftl_dummy_dtag | DTAG_IN_DDR_MASK);
            } // for(du_idx = 0; du_idx < du_cnt; du_idx++)

            ftl_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &ftl_info[cont_idx];
            ftl_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &ftl_pda_list[cont_idx];
            ftl_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = plane_op*FTL_PG_IN_WL_CNT;
            ftl_ncl_cmd[cmd_idx].status        = 0;
            ftl_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
            ftl_ncl_cmd[cmd_idx].op_code       = NCL_CMD_OP_WRITE;
#if defined(HMETA_SIZE)
            ftl_ncl_cmd[cmd_idx].op_type = NCL_CMD_PROGRAM_DATA;
            ftl_ncl_cmd[cmd_idx].du_format_no = host_du_fmt;
#else
            ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_PROGRAM_TABLE;
            ftl_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
#endif
            ftl_ncl_cmd[cmd_idx].completion    = ftl_func_write_done;
            ftl_ncl_cmd[cmd_idx].user_tag_list = &ftl_bm_pl[cont_idx];
            ftl_ncl_cmd[cmd_idx].caller_priv   = NULL;
            ncl_cmd_submit(&ftl_ncl_cmd[cmd_idx]);
            // ===== prepare 2PN WL ncl cmd resource =====
            die_id++;
            cpda_idx = cpda_next;
            //ftl_apl_trace(LOG_ALW, 0, "cpda_idx : 0x%x, cpda_end : 0x%x", cpda_idx, cpda_end);

            if(cpda_idx>=cpda_end){break;}
        } // while(die_id < FTL_DIE_CNT)

        die_id = 0;
    } // while(cpda_start<cpda_end)

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
}

/*!
 * @brief ftl spor function
 * fill dummy for die sch
 *
 * @return	not used
 */
init_code bool ftl_fill_dummy_for_die_sch(spb_id_t spb_id , u16 wl_idx )
{
	//spor_fill_next_wl = false;
	bool need_fill_next_wl = false;
	u8	spb_type = 0xFF;
	u8	pn_pair, du_cnt = 0, plane_op = 0;
	u16	du_idx, cont_idx, die_id, i; 

	u16	cmd_idx = 0;
	u32	meta_idx;
	pda_t cpda_idx = 0, pda = 0; 
#ifdef SKIP_MODE
	u32	pn_ptr = 0; 
	u8  pln_idx;	   
	pda_t cpda_plane_first = 0;  
	u8  page_cmd_idx ; 
#endif

    u8 parity_die_pln_idx = shr_nand_info.geo.nr_planes;
    u32 parity_die = FTL_DIE_CNT;
    bool raid_support = fcns_raid_enabled(INT_NS_ID);
    if(raid_support){
        parity_die = FTL_DIE_CNT - 1;
        while(parity_die >= 0)
        {
#ifdef SKIP_MODE
            pn_ptr = parity_die<<FTL_PN_SHIFT;
            pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
            pn_pair = BLOCK_NO_BAD;
#endif
            if (pn_pair == BLOCK_ALL_BAD) {
                parity_die--;
            } else {
                bool ret = false;
                for (u8 pl = 0; pl < shr_nand_info.geo.nr_planes; pl++){
                    if (((pn_pair >> pl)&1)==0){ //good plane
                        parity_die_pln_idx = pl;
                        ret = true;
                        break;
                    }
                }
                sys_assert(ret);
                break;
            }
        }
    }

	//u16	spb_id = pda2blk(pda_start);

	ftl_apl_trace(LOG_ALW, 0x3c0f, "spb %u,wl_idx:%d,bitmap:0x%x%x,die_cnt:%d",\
							spb_id,wl_idx,spor_dummy_bit[1],spor_dummy_bit[0],FTL_DIE_CNT); 

	// ===== Start fill dummy from next wl =====

	memset((u8*)ddtag2mem(ftl_dummy_dtag), 0xFF, DTAG_SZE);

	if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER) ||
	  (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER))
	{
	   spb_type = FTL_BLK_TYPE_HOST;
	}
	else if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC) ||
		   (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
	{
	   spb_type = FTL_BLK_TYPE_GC;
	}

	pda = ftl_set_blk2pda(pda, spb_id); 
	pda = ftl_set_pg2pda(pda, (wl_idx*FTL_PG_IN_WL_CNT));

	meta_idx = ftl_spor_meta_idx;
    for(i = 0; i < DU_CNT_PER_PAGE; i++)
    {
        ftl_set_page_meta(meta_idx, i, SPOR_DUMMY_LDA, spb_id, spb_type);
    }

	u32* spor_bitmap = spor_dummy_bit;

	for(die_id = 0;die_id < FTL_DIE_CNT;die_id++)
	{
		u64 die_bit = test_bit(die_id,spor_bitmap); 
#ifdef SKIP_MODE
		pn_ptr = die_id<<FTL_PN_SHIFT; 
	    pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr); 
#else
		pn_pair = BLOCK_NO_BAD; 
#endif

        if((die_id == parity_die) && raid_support){
            pn_pair |= 1 << parity_die_pln_idx; // skip parity plane
        }

	    if(pn_pair == BLOCK_ALL_BAD)
		{
			//die_id++;
            //cpda_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
            ftl_apl_trace(LOG_ALW, 0xf038, "plane all bad at die : 0x%x!!", die_id);
            continue;
		}

		if(die_bit == 0 )   
		{
			//if(pn_pair == BLOCK_NO_BAD)	//blank die is no defect 
				need_fill_next_wl = true;
			continue;
		}
		pda = ftl_set_die2pda(pda, die_id);
	    cpda_idx = ftl_pda2cpda(pda, V_MODE); 

	    plane_op = FTL_PN_CNT;  
		//ftl_apl_trace(LOG_ALW, 0, "die_sch_fill_dummy die:%d pda:0x%x",die_id,pda); 
	    // ===== prepare 2PN WL ncl cmd resource =====
	    cmd_idx  = get_spor_ncl_cmd_idx();
	    cont_idx = cmd_idx*FTL_DU_PER_WL*FTL_PN_CNT; 
	    memset(&ftl_info[cont_idx], 0, (FTL_DU_PER_WL*FTL_PN_CNT*sizeof(*ftl_info))); 
	    if(pn_pair == BLOCK_NO_BAD)
	    {
	        for(i=0;i<FTL_PN_CNT*FTL_PG_IN_WL_CNT;i++) 
	        {
	            ftl_pda_list[cont_idx+i]     = ftl_cpda2pda(cpda_idx, V_MODE);
	            ftl_info[cont_idx+i].pb_type = NAL_PB_TYPE_XLC;
	            cpda_idx+=DU_CNT_PER_PAGE;
	        }
	    }
#ifdef SKIP_MODE
	    else 
        { 
        	//ftl_apl_trace(LOG_ALW, 0, "die sch pn_pair = 0x%x",pn_pair); 
        	page_cmd_idx = 0;  
        	cpda_plane_first = cpda_idx; 
			for(pln_idx = 0;pln_idx < FTL_PN_CNT;pln_idx++) 
			{ 
				if(BIT_CHECK(pn_pair, pln_idx)) 
				{ 
					//cpda_idx += DU_CNT_PER_PAGE; 
					plane_op--; 
					//continue; 
				} 
				else 
				{ 
					for(i=0;i<FTL_PG_IN_WL_CNT;i++,page_cmd_idx++)  
	                { 
	                    ftl_pda_list[cont_idx+page_cmd_idx]     = ftl_cpda2pda(cpda_idx, V_MODE);  
	                    ftl_info[cont_idx+page_cmd_idx].pb_type = NAL_PB_TYPE_XLC;  
	                    cpda_idx+=(DU_CNT_PER_PAGE<<FTL_PN_SHIFT);  
	                    //ftl_apl_trace(LOG_ALW, 0, "die sch fill dummy pda :0x%x,plane:%d",ftl_pda_list[cont_idx+page_cmd_idx],ftl_pda2plane(ftl_pda_list[cont_idx+page_cmd_idx])); 
	                } 
				} 
				cpda_plane_first +=DU_CNT_PER_PAGE; 
				cpda_idx = cpda_plane_first; 
			} 
			//du_cmt_idx = 0; 
        }  
#endif

	    du_cnt = plane_op*FTL_DU_PER_WL;
	    for(du_idx = 0; du_idx < du_cnt; du_idx++)
	    {
	        ftl_bm_pl[cont_idx+du_idx].pl.btag       = cmd_idx;
	       	ftl_bm_pl[cont_idx+du_idx].pl.du_ofst    = du_idx;
	       	ftl_bm_pl[cont_idx+du_idx].pl.nvm_cmd_id = meta_idx + (du_idx%DU_CNT_PER_PAGE);
#if (SPOR_CMD_EXP_BAND == mENABLE)
	        ftl_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
#else
	        ftl_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
#endif
	        ftl_bm_pl[cont_idx+du_idx].pl.dtag       = (ftl_dummy_dtag | DTAG_IN_DDR_MASK);
	    } // for(du_idx = 0; du_idx < du_cnt; du_idx++)

	    ftl_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &ftl_info[cont_idx];
	    ftl_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &ftl_pda_list[cont_idx];
	    ftl_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = plane_op*FTL_PG_IN_WL_CNT;
	    ftl_ncl_cmd[cmd_idx].status        = 0;
	    ftl_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
	    ftl_ncl_cmd[cmd_idx].op_code       = NCL_CMD_OP_WRITE;
#if defined(HMETA_SIZE)
	    ftl_ncl_cmd[cmd_idx].op_type = NCL_CMD_PROGRAM_DATA;
	    ftl_ncl_cmd[cmd_idx].du_format_no = host_du_fmt;
#else
	    ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_PROGRAM_TABLE;
	    ftl_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
#endif
	    ftl_ncl_cmd[cmd_idx].completion    = ftl_func_write_done;
	    ftl_ncl_cmd[cmd_idx].user_tag_list = &ftl_bm_pl[cont_idx];
	    ftl_ncl_cmd[cmd_idx].caller_priv   = NULL;
	    ncl_cmd_submit(&ftl_ncl_cmd[cmd_idx]);
	}
	// ----- wait all form done -----
	wait_spor_cmd_idle();
	spor_dummy_bit[0] = 0;
	spor_dummy_bit[1] = 0;
	spor_dummy_bit[2] = 0;
	spor_dummy_bit[3] = 0;
	return need_fill_next_wl;

}



/*!
 * @brief ftl spor function
 * update vac table
 *
 * @return	not used
 */
ddr_code void ftl_vac_mode_handle(u16 new_blk, u16 old_blk, pda_t new_pda, pda_t old_pda, u32 *vc, u8 update_vc_mode)
{
    if(new_blk != old_blk)
    {
        if(update_vc_mode == UPDATE_ALL_BLK_VAC)
        {
            vc[new_blk]++;

            #if 0  // no need to compare SN, due to GC update first then Host update
            if(ftl_spor_get_blk_sn(new_blk) > ftl_spor_get_blk_sn(old_blk))
            #endif
            {
                if(vc[old_blk]==0)
                {
                    ftl_apl_trace(LOG_ALW, 0x38e5, "Update Vac equal zero(2) - new Blk : 0x%x; old Blk : 0x%x", new_blk, old_blk);
#if (SPOR_ASSERT == mENABLE)
                    sys_assert(0);
#endif
                }
                else
                {
                    vc[old_blk]--;
                }
            }
        }
        else if(update_vc_mode == UPDATE_CHECK_POINT_VAC)
        {
            if(ftl_spor_get_blk_sn(new_blk) > ftl_spor_get_blk_sn(old_blk))
            {

                vc[new_blk]++;
                if(vc[old_blk]==0)
                {
                    ftl_apl_trace(LOG_ALW, 0x2c5d, "Update Vac equal zero(3) - new Blk : 0x%x; old Blk : 0x%x", new_blk, old_blk);
#if (SPOR_ASSERT == mENABLE)
                    sys_assert(0);
#endif
                }
                else
                {
                    vc[old_blk]--;
                }
            }
        }

        if(0 == vc[old_blk])
        {
            if((spb_info_tbl[old_blk].pool_id != SPB_POOL_GC)&&
               (spb_info_tbl[old_blk].pool_id != SPB_POOL_FREE))
            {
                FTL_BlockPopPushList(SPB_POOL_FREE, old_blk, FTL_SORT_BY_EC);
                spb_set_sw_flags_zero(old_blk);
                spb_set_desc_flags_zero(old_blk);
            }
        }

    }
    else
    {
        // if(new_blk == old_blk)
        pda_t new_cpda, old_cpda;
        new_cpda = ftl_pda2cpda(new_pda, V_MODE);
        old_cpda = ftl_pda2cpda(old_pda, V_MODE);

        if(new_cpda <= old_cpda)
        {
            vc[new_blk]++;
        }
    }
}


/*!
 * @brief ftl spor function
 * update vac table
 *
 * @return	not used
 */
//fast_code void ftl_update_vac(pda_t new_pda, pda_t old_pda, u32 *vc, u8 update_vc_mode)
ddr_code void ftl_update_vac(pda_t new_pda, pda_t old_pda, u32 *vc, u8 update_vc_mode)
{
    u16   old_blk, new_blk;

    //if(update_vc_mode == NO_UPDATE_VAC) {return;}

    old_blk = pda2blk(old_pda);
    new_blk = pda2blk(new_pda);

    if(old_pda == INV_LDA)
    {
        vc[new_blk]++;
    }
    else
    {
        if(ftl_build_tbl_type[old_blk] == FTL_SN_TYPE_BLANK)
        {
            // old block is completely blank
            if(vc[old_blk])
            {
                ftl_apl_trace(LOG_ALW, 0x6fca, "Empty Block : 0x%x, VAC : 0x%x", old_blk, vc[old_blk]);
                //ftl_vac_mode_handle(new_blk, old_blk, new_pda, old_pda, vc, update_vc_mode);
                vc[old_blk] = 0;
            }

            vc[new_blk]++;
        }
        else
        {
            // old pda has valid entry
            ftl_vac_mode_handle(new_blk, old_blk, new_pda, old_pda, vc, update_vc_mode);
        }
    }

    if(vc[new_blk] > FTL_DU_PER_TLC_SBLOCK)
    {
        ftl_apl_trace(LOG_ALW, 0x653a, "Update Vac over threshold - new Blk : 0x%x, new PDA : 0x%x; old Blk : 0x%x, old PDA : 0x%x", new_blk ,new_pda, old_blk, old_pda);
#if (SPOR_ASSERT == mENABLE)
        sys_assert(0);
#endif
    }
}


/*!
 * @brief ftl spor function
 * update P2L entries to L2P
 *
 * @return	not used
 */
fast_code void ftl_update_l2p_table(lda_t lda, pda_t new_pda, pda_t *old_pda, u32 l2p_addr)
{
    lda_t *l2p_entry = (lda_t*)l2p_addr;

    *old_pda = *l2p_entry;
    *l2p_entry = new_pda;
}
#if (PLP_SUPPORT == 0)
/*!
 * @brief ftl spor function
 * update recheck L2P[NON-PLP]
 *
 * @return	not used
 */
fast_code void ftl_recheck_l2p_table(lda_t lda, pda_t new_pda, pda_t *old_pda, u32 l2p_addr)
{
    lda_t *l2p_entry = (lda_t*)l2p_addr;

    *old_pda = *l2p_entry;
    if(*old_pda == new_pda)
    	*l2p_entry = INV_U32;
}
#endif
/*! 
 * @brief ftl spor function
 *  update P2L entries to L2P(need recheck)
 *
 * @return	not used
 */
fast_code bool ftl_chk_update_l2p_table(lda_t lda, pda_t new_pda, pda_t *old_pda, u32 l2p_addr,u8 mode)
{
    lda_t *l2p_entry = (lda_t*)l2p_addr; 

    *old_pda = *l2p_entry; 

	if(*old_pda != INV_U32) 
	{  
		//spb_id_t new_blk = pda2blk(new_pda); 
		spb_id_t old_blk = pda2blk(*old_pda); 
		if((mode == P2L_GC_ONLY) && (old_blk == pre_host_blk))   //gc cover host----pbt special case 
		{ 
			//purpose  : avoid gc blk cover host blk(PBT special case) 
			//principle: when build open gc in pbt case,need to compare pre host blk data 

			return true; 

		} 
	} 

    *l2p_entry = new_pda; 
    return false; 
}



#if (SPOR_TRIM_DATA == mENABLE)
extern u8* TrimBlkBitamp;
/*!
 * @brief ftl spor function
 * read trim data from specified pda
 *
 * @return	not used
 */
init_code void ftl_read_trim_pda(pda_t pda)
{
    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_read_trim_pda, PDA : 0x%x", pda);
    ftl_send_read_ncl_form(NCL_CMD_SPOR_P2L_READ, &dtag, pda, 1, ftl_read_p2l_from_nand_done);
    wait_spor_cmd_idle();
}


/*!
 * @brief ftl spor function
 * update trim info to L2P
 *
 * @return	not used
 */
fast_code u32 ftl_update_invalid_entry(u64 addr_start, u64 byte_length)
{
    u32 done_length;
    u32 addr_ofst = (u32)addr_start&0x1F;
    addr_start -= (u64)addr_ofst;

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_update_invalid_entry");
    if((32 - (u64)addr_ofst)<=byte_length){
        done_length = (32 - addr_ofst);
    }else{
        done_length = (u32)byte_length;
    }

    #if 1
    //ftl_apl_trace(LOG_ALW, 0, "addr_start after : 0x%x-%x", (addr_start>>32), addr_start);
    {
        u8  win = 0;
        while (addr_start >= 0xC0000000)
    {
            addr_start -= 0x80000000;
        win++;
    }
        u32 DDRPriority = readl((void *)(0xc0068014));
        writel(0x4400,(void *)(0xc0068014));//set FW ddr access highest priority
        isb();
        mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
        u8 old_win = ctrl0.b.cpu3_ddr_window_sel;
        if(win != old_win){
    ctrl0.b.cpu3_ddr_window_sel = win;
            writel(ctrl0.all,(void *)(DDR_TOP_BASE + MC_CTRL_REG0));
    // current byte length won't exceed 65536(0x10000)

        isb();
        }
        memset((void *)((u32)addr_start+addr_ofst), 0xFF, done_length);
        if(win != old_win){
        ctrl0.b.cpu3_ddr_window_sel = old_win;
        writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
        isb();

    }
        writel(DDRPriority,(void *)(0xc0068014));
    isb();
    }
    #else
    {
        void * L2P_bak = NULL;
        L2P_bak = sys_malloc_aligned(FAST_DATA,32,32);
        sys_assert(L2P_bak);
        sync_dpe_copy(addr_start, (u32)L2P_bak, 32);
        memset((void *)((u32)L2P_bak+addr_ofst), 0xFF, done_length);
        sync_dpe_copy((u32)L2P_bak, addr_start, 32);
        sys_free_aligned(FAST_DATA, (void *) L2P_bak);
    }
    #endif
    return done_length;
}

fast_code void ftl_update_trim_l2p_table(lda_t lda, u32 length)
{
    u32 byte_length;
    u64 addr_base, addr_start;
    u64 done_length = 0;
    addr_base = ddtag2mem(shr_l2p_entry_start);
    if((lda >= _max_capacity) || (length == 0) || ((lda+length)>_max_capacity)) {return;}

    // increase speed for clear L2P
    addr_start  = addr_base + ((u64)lda * sizeof(lda_t));
    byte_length = (length*sizeof(lda_t));

    // if(length!=1024)
    //     lxp_mgr_trace(LOG_ALW, 0, " lda %u, num %u, lba %x_%x, num %x_%x",
    //         lda, length, ((u64)lda)>>29, ((u64)lda)<<3, ((u64)length)>>29, ((u64)length)<<3);

    //ftl_apl_trace(LOG_ALW, 0, "addr_start : 0x%x-%x, byte length : %d", (addr_start>>32), addr_start, byte_length);
    //------------------------------------------------------
    if(((addr_start & 0x1F) != 0)||(byte_length<32))
    {
        // if addr_start is not 32 byte aligned
        done_length = (u64)ftl_update_invalid_entry(addr_start, byte_length);
        byte_length -= done_length;

        addr_start += done_length;
        if(byte_length == 0){
            // if aligned addr_start is larger than addr_end
            return;
        }
    }
    sys_assert((addr_start&0x1F)==0);
    // altered addr_start & byte_length
    if((byte_length >> 5) != 0)
    {
        // if byte_length is not 32 byte aligned
        done_length = byte_length&0xFFFFFFFFFFFFFFE0;
        bm_scrub_ddr((addr_start-DDR_BASE), done_length, 0xffffffff);
            // byte_length >= 32
        addr_start += done_length;
        byte_length -= done_length;
        if(byte_length == 0){
            return;
            // byte_length < 32
        }
    }
    sys_assert((addr_start&0x1F)==0);
    sys_assert(byte_length<32);

    // addr_start is not aligned, and need to clean pre not aligned part
    done_length = (u64)ftl_update_invalid_entry(addr_start, byte_length);
    byte_length -= done_length;
    sys_assert(byte_length==0);
}

/*!
 * @brief ftl spor function
 * update trim info to L2P
 *
 * @return	not used
 */
fast_data u32 update_trim_cnt = 0;
fast_code void ftl_update_trim_info(void)
{
    u32   trim_set_cnt, set_idx;
    u32   length;
    lda_t slda, elda;
    Host_Trim_Data * trim_data = NULL;
#if(SPOR_TIME_COST == mENABLE)
    //u64 time_start = get_tsc_64();
#endif

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_update_trim_info");
    trim_data = sys_malloc_aligned(FAST_DATA, DTAG_SZE, 32);
    sys_assert(trim_data);
    sync_dpe_copy((u64) ddtag2mem(ftl_dummy_dtag), (u32) trim_data, DTAG_SZE);
    if(trim_data->Validtag != 0x12345678)
    {
        ftl_apl_trace(LOG_ALW, 0x31b4, "trim tag invalid : 0x%x", trim_data->Validtag);
        return;
    }

    trim_set_cnt  = trim_data->Validcnt;

    // if(trim_data->Ranges[0].Length!=1024)
    //     ftl_apl_trace(LOG_ALW, 0, "trim_set_cnt : %d lda 0x%x length %u",
    //         trim_set_cnt, trim_data->Ranges[0].sLDA, trim_data->Ranges[0].Length);

    // increase speed for clear L2P
    update_trim_cnt++;
    if((update_trim_cnt&0x7FF) == 0){
        ftl_apl_trace(LOG_ALW, 0xef0e, "LDA start : 0x%x, length : %d,update_trim_cnt:0x%x", trim_data->Ranges[0].sLDA, trim_data->Ranges[0].Length,update_trim_cnt);
    }
    for(set_idx = 0; set_idx < trim_set_cnt; set_idx++)
    {
        //ftl_apl_trace(LOG_ALW, 0, "LDA start : 0x%x, length : %d", lda, length);
        length = trim_data->Ranges[set_idx].Length;
        slda = trim_data->Ranges[set_idx].sLDA;
        elda = slda + length ;
        // extern Trim_Info TrimInfo;
        //if(!trim_data->all)
        //	ftl_apl_trace(LOG_ALW, 0x05d4, "LDA start : 0x%x, length : %d", slda, length);

        if(trim_data->all){
            ftl_update_trim_l2p_table(slda, length);
        }else{
            if(slda & (BIT(10)-1)){
                u32 len = min((BIT(10)-(slda & (BIT(10)-1))), length);
                ftl_update_trim_l2p_table(slda, len);
            }
            if(elda & (BIT(10)-1) && (!(slda & (BIT(10)-1)) || (slda>>10 != elda>>10))){
                ftl_update_trim_l2p_table((elda - (elda & (BIT(10)-1))), (elda & (BIT(10)-1)));
            }
        }
    }

    sys_free_aligned(FAST_DATA, (void *) trim_data);


#if(SPOR_TIME_COST == mENABLE)
    //ftl_apl_trace(LOG_ALW, 0, "Function time cost : %d us", time_elapsed_in_us(time_start));
#endif

}
#endif

/*! 
 * @brief ftl spor function 
 * P2L build table 
 * 
 * @return	not used 
 */ 
fast_code void ftl_p2l_skip_bad_update(u16 spb_id, u8 blk_type, u32 *pgsn_buf_idx, u8 mode, u32 *vc) 
{ 
    u8    update_vc_mode = NO_UPDATE_VAC; 
    u32   threshold = 0; 
    u8    i, win, old_win; 
    pda_t new_pda = 0, old_pda = 0; 
    lda_t lda; 
    u32   loop_idx; 
    u32   page_sn_loop = 0; 
    lda_t *lda_buf_start = NULL; 
    pda_t *pda_buf_start = NULL; 
    u64   *pgsn_buf_start = NULL; 
#if (SPOR_TRIM_DATA == mENABLE) 
    u8    read_trim_data = mFALSE; 
    pda_t trim_data_pda  = INV_U32; 
#endif 

    u64 addr; 
    u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000); 
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))}; 

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_p2l_skip_bad_update"); 
	bool special_case = false; 
    if((ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P) && 
       (ftl_spor_info->l2p_data_error || ftl_spor_info->blist_data_error || 
        ftl_spor_info->epm_vac_error)) 
    { 
        // if L2P/Blist read fail, or using USR build. 
        update_vc_mode = UPDATE_ALL_BLK_VAC; 
		ftl_apl_trace(LOG_ALW, 0x5769, "update_vc_mode:%d vc:0x%x  need recheck l2p table!!!!!" ,update_vc_mode,vc[spb_id]);	//no use 			 
    } 

    // ===== decide blk type ===== 
    switch (blk_type) 
    { 
        case FTL_BLK_TYPE_HOST: 
        { 
            lda_buf_start  = ftl_host_p2l_info.lda_buf; 
            pgsn_buf_start = ftl_host_p2l_info.pgsn_buf; 
            pda_buf_start  = ftl_host_p2l_info.pda_buf; 
            break; 
        } 

        case FTL_BLK_TYPE_GC: 
        { 
            lda_buf_start  = ftl_gc_p2l_info.lda_buf; 
            pgsn_buf_start = ftl_gc_p2l_info.pgsn_buf; 
            pda_buf_start  = ftl_gc_p2l_info.pda_buf; 
            break; 
        } 

        default: 
#if (SPOR_ASSERT == mENABLE) 
            panic("no such type!\n"); 
#else 
            ftl_apl_trace(LOG_PANIC, 0xc4c6, "no such type!"); 
#endif 
            break; 
    } 

    // ===== decide update threshold ===== 
    switch(mode) 
    { 
        case P2L_USR_ONLY: 
        { 
            threshold = usr_page_threshold; 
            break; 
        } 

        case P2L_GC_ONLY: 
        { 
            threshold = gc_page_threshold; 
            break; 
        } 

        default: 
#if (SPOR_ASSERT == mENABLE) 
            panic("no such mode!\n"); 
#else 
            ftl_apl_trace(LOG_PANIC, 0x2e5c, "no such mode!"); 
#endif 
            break; 
    } 

	if((spb_id == pre_gc_blk) && (pre_host_blk != INV_U16)) 
	//if(mode == P2L_GC_ONLY) 
	{ 
		if(ftl_init_sn_tbl[pre_host_blk] < record_spor_last_rec_blk_sn) 
		{ 
			ftl_apl_trace(LOG_ALW, 0x3080, "pre_host_blk_sn:0x%x < spor_last_rec_blk_sn:0x%x ,no need update",ftl_init_sn_tbl[pre_host_blk],record_spor_last_rec_blk_sn); 
		} 
		else 
		{ 
			special_case = true; 
			ftl_apl_trace(LOG_ALW, 0x212b, "[PBT_special_case]gc blk need recheck l2p,  pre_gc:%d pre_host:%d",pre_gc_blk,pre_host_blk); 
		} 
	} 

    // traverse super wl page sn table 
    loop_idx = (*pgsn_buf_idx); 

	if(special_case == false)//normal case 
	{ 

		while(loop_idx < threshold) 
	    { 
	        new_pda = pda_buf_start[loop_idx]; 

	        for(i=0; i<DU_CNT_PER_PAGE; i++) 
	        { 
	            lda = lda_buf_start[(new_pda&FTL_PDA_DU_MSK)]; 
			    if((lda < _max_capacity) ) 
	            { 
	                //addr = addr_base + ((u64)lda * sizeof(lda_t)); 
	                addr = addr_base + ((u64)lda << 2); 
	                win  = 0; 
	                while (addr >= 0xC0000000) { 
	                	addr -= 0x80000000; 
	                	win++; 
	                } 
 					if(win == 0)//no need change win , save time
 					{
	                	ftl_update_l2p_table(lda, new_pda, &old_pda, (u32)addr); 
 					}
 					else
 					{
						old_win = ctrl0.b.cpu3_ddr_window_sel; 
						ctrl0.b.cpu3_ddr_window_sel = win; 
						isb(); 
						writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
						ftl_update_l2p_table(lda, new_pda, &old_pda, (u32)addr); 
						ctrl0.b.cpu3_ddr_window_sel = old_win; 
						writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
						isb(); 

 					}

	                if((old_pda == 0)||(new_pda == 0)) 
	                { 
	                    ftl_apl_trace(LOG_ALW, 0x84a3, "PDA Zero -new PDA : 0x%x old PDA : 0x%x",new_pda, old_pda); 
#if (SPOR_ASSERT == mENABLE) 
	                    sys_assert(0); 
#endif 
	                } 

	                //if(update_vc_mode != NO_UPDATE_VAC) 
	                //{ 
	                    //ftl_update_vac(new_pda, old_pda, vc, update_vc_mode); 
	                //} 
	                page_sn_loop = loop_idx; 
	            } 
#if (SPOR_TRIM_DATA == mENABLE) 
	            else if(lda == TRIM_LDA) 
	            { 
	                trim_data_pda  = new_pda; 
	                read_trim_data = mTRUE; 
	                page_sn_loop = loop_idx; 
	            } 
#endif 
	            new_pda++; 
	        } // end for(i=0; i<DU_CNT_PER_PAGE; i++) 

#if (SPOR_TRIM_DATA == mENABLE) 
	        if(read_trim_data) 
	        { 
	            ftl_read_trim_pda(trim_data_pda); 
	            ftl_update_trim_info(); 
	            read_trim_data = mFALSE; 
	        } 
#endif 

	        (*pgsn_buf_idx)++; 
	        loop_idx++; 
	        // reach buffer end, change to next WL buffer addr 
	        if(pda_buf_start[loop_idx] == INV_U32) 
	        { 
	            //ftl_apl_trace(LOG_ALW, 0, "[Before] pgsn_buf_idx : 0x%x, loop_idx : 0x%x", *pgsn_buf_idx, loop_idx); 
	            (*pgsn_buf_idx) = (((*pgsn_buf_idx) + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY; 
	            loop_idx        = ((loop_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY; 
	            if(pda_buf_start[loop_idx] == INV_U32) 
				{ 
					lda = lda_buf_start[(pda_buf_start[loop_idx]&FTL_PDA_DU_MSK)]; 
					ftl_apl_trace(LOG_ALW, 0x6605, "skip update>>loop_idx:%d threshold:%d ",loop_idx,threshold); 
					(*pgsn_buf_idx) = threshold; 
					loop_idx = threshold; 
					//return; 
				} 
	            //ftl_apl_trace(LOG_ALW, 0, "[After] pgsn_buf_idx : 0x%x, loop_idx : 0x%x", *pgsn_buf_idx, loop_idx); 
	        } 
	    } 

	} 
	else //gc blk recheck l2p table --add by Jay, separate code for save time
	{ 
		bool hit_lda = false; 
		u8  print_cnt = 30; 
		while(loop_idx < threshold) 
		{ 
	        new_pda = pda_buf_start[loop_idx]; 

	        for(i=0; i<DU_CNT_PER_PAGE; i++) 
	        { 
	            lda = lda_buf_start[(new_pda&FTL_PDA_DU_MSK)]; 
			    if((lda < _max_capacity) ) 
	            { 
	                //addr = addr_base + ((u64)lda * sizeof(lda_t)); 
	                addr = addr_base + ((u64)lda << 2);  
	                win  = 0; 
	                while (addr >= 0xC0000000) { 
	                	addr -= 0x80000000; 
	                	win++;  
	                }	 
 					if(win == 0)//no need change win , save time
 					{
						hit_lda = ftl_chk_update_l2p_table(lda, new_pda, &old_pda, (u32)addr ,mode); 
 					}
 					else
 					{
		                old_win = ctrl0.b.cpu3_ddr_window_sel; 
		                ctrl0.b.cpu3_ddr_window_sel = win; 
		                isb(); 
		                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 

						hit_lda = ftl_chk_update_l2p_table(lda, new_pda, &old_pda, (u32)addr ,mode); 

		                ctrl0.b.cpu3_ddr_window_sel = old_win; 
		                writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
		                isb(); 
 					}

	                page_sn_loop = loop_idx; 
					if((print_cnt > 0) && (hit_lda == true)) 
					{ 
						print_cnt--;
						ftl_apl_trace(LOG_ALW, 0xfa50, "hit lda:0x%x  gc pda:0x%x usr pda:0x%x", lda,new_pda,old_pda); 
					} 
					if((new_pda == 0) || (old_pda == 0))//error 
					{ 
						ftl_apl_trace(LOG_ALW, 0x9da4, "PDA Zero -new PDA : 0x%x old PDA : 0x%x",new_pda, old_pda); 
#if (SPOR_ASSERT == mENABLE) 
				         sys_assert(0); 
#endif 
					} 

	            } 
	            new_pda++; 
	        } // end for(i=0; i<DU_CNT_PER_PAGE; i++) 

 	        (*pgsn_buf_idx)++; 
	        loop_idx++; 
	        if(pda_buf_start[loop_idx] == INV_U32) 
	        { 
	            (*pgsn_buf_idx) = (((*pgsn_buf_idx) + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY; 
	            loop_idx        = ((loop_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY; 
	            if(pda_buf_start[loop_idx] == INV_U32) 
				{ 
					lda = lda_buf_start[(pda_buf_start[loop_idx]&FTL_PDA_DU_MSK)]; 
					ftl_apl_trace(LOG_ALW, 0xc8fe, "skip update>>loop_idx:%d threshold:%d ",loop_idx,threshold); 
					(*pgsn_buf_idx) = threshold; 
					loop_idx = threshold; 
				} 
	        } 
	    } 
	} 

    	if((pgsn_buf_start[page_sn_loop] != INV_U64) && (pgsn_buf_start[page_sn_loop] != 0xFFFFFFFFFFFF)) 
        { 
            gFtlMgr.GlobalPageSN = (pgsn_buf_start[page_sn_loop]>gFtlMgr.GlobalPageSN)? pgsn_buf_start[page_sn_loop] : gFtlMgr.GlobalPageSN; 
        }	 
		ftl_apl_trace(LOG_ALW, 0xefab, "trim_pda:0x%x page_sn_loop:%d page sn:0x%x%x threshold:%d", 
						trim_data_pda , page_sn_loop ,(u32)(pgsn_buf_start[page_sn_loop]>>32),(u32)pgsn_buf_start[page_sn_loop],threshold); 
} 


#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
init_code void ftl_slc_build_l2p(void)
{
	u32 loop_idx  = 0;//pda buf start
	u32 threshold = 0;//pda buf end
    lda_t *lda_buf_start = NULL; 
    pda_t *pda_buf_start = NULL; 
	lda_t lda = INV_LDA;
    pda_t new_pda = 0, old_pda = 0; 
	u32 i;

	if(slc_read_blank_pos != INV_PDA)
	{
		pda_t pda_temp = slc_read_blank_pos;
		pda_temp = ftl_set_pn2pda(pda_temp, 0);
		pda_temp = ftl_set_du2pda(pda_temp, 0);
		shr_plp_slc_end_pda = pda_temp;
		ftl_apl_trace(LOG_WARNING, 0x9021,"[SLC] adjust slc build range end_pda:0x%x blank_pos:0x%x",pda_temp,slc_read_blank_pos);
	}
    loop_idx  = (shr_plp_slc_start_pda&FTL_PDA_DU_MSK) / DU_CNT_PER_PAGE;
    threshold = (shr_plp_slc_end_pda&FTL_PDA_DU_MSK) / DU_CNT_PER_PAGE;
	lda_buf_start  = ftl_host_p2l_info.lda_buf; 
	pda_buf_start  = ftl_host_p2l_info.pda_buf; 

    sys_assert(threshold < PDA_FULL_TABLE_ENTRY);

	ftl_apl_trace(LOG_INFO, 0x9a96,"slc build start,loop_idx:%d threshold:%d cnt:%d",loop_idx,threshold,threshold-loop_idx);

	u8  win, old_win; 
	u64 addr; 
    u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000); 
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))}; 

#if (SPOR_TRIM_DATA == mENABLE) 
	u8	  read_trim_data = mFALSE; 
	pda_t trim_data_pda  = INV_U32; 
#endif 


	while(loop_idx < threshold)
	{
		new_pda = pda_buf_start[loop_idx];
		if(new_pda == INV_U32)
		{
			loop_idx++;
			continue;
		}
		sys_assert(pda2blk(new_pda) == PLP_SLC_BUFFER_BLK_ID);
		for(i = 0; i<DU_CNT_PER_PAGE; i++)
		{
			lda = lda_buf_start[(new_pda&FTL_PDA_DU_MSK)];
			if(lda < _max_capacity)
			{
				addr = addr_base + ((u64)lda << 2); 
	            win  = 0; 
	            while (addr >= 0xC0000000) { 
	            	addr -= 0x80000000; 
	            	win++; 
	            } 
				if(win == 0)//no need change win , save time
				{
	        		ftl_update_l2p_table(lda, new_pda, &old_pda, (u32)addr); 
				}
				else
				{
					old_win = ctrl0.b.cpu3_ddr_window_sel; 
					ctrl0.b.cpu3_ddr_window_sel = win; 
					isb(); 
					writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
					ftl_update_l2p_table(lda, new_pda, &old_pda, (u32)addr); 
					ctrl0.b.cpu3_ddr_window_sel = old_win; 
					writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
					isb(); 
				}
				//ftl_apl_trace(LOG_INFO, 0x0816,"pda:0x%x old_pda:0x%x o_blk:%d lda:0x%x loop_idx:%d",
				//	new_pda,old_pda,pda2blk(old_pda),lda,loop_idx);
			}
#if (SPOR_TRIM_DATA == mENABLE) 
            else if(lda == TRIM_LDA) 
            { 
                trim_data_pda  = new_pda; 
                read_trim_data = mTRUE; 
            } 
#endif 
			new_pda++;
		}

#if (SPOR_TRIM_DATA == mENABLE) 
		if(read_trim_data) 
		{ 
			ftl_read_trim_pda(trim_data_pda); 
			ftl_update_trim_info(); 
			read_trim_data = mFALSE; 
		} 
#endif

		loop_idx++;
	}
	ftl_apl_trace(LOG_INFO, 0x7a4a,"slc build done,loop_idx:%d threshold:%d trim_data_pda:0x%x",loop_idx,threshold,trim_data_pda);
}
#endif

/*!
 * @brief ftl spor function
 * read p2l table from NAND
 *
 * @return	not used
 */
#define  USR_P2L_END (1)
#define  GC_P2L_END  (2)


init_code u8 ftl_cross_p2l_update_l2p(u16 usr_blk, u16 gc_blk, u8 mode)
{
    u32    *vc, cnt;
    dtag_t dtag;
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
#if(PLP_SUPPORT == 1)      
	bool  is_skip_update = false;
#endif 

    // ===== initial update index =====
    if((usr_page_idx==0) && (usr_blk != INV_U16))
    {
        if((ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P) &&
           (ftl_spor_info->l2p_data_error || ftl_spor_info->blist_data_error ||
            ftl_spor_info->epm_vac_error))
        {
            ftl_apl_trace(LOG_ALW, 0x7a1e, "usr blk : %d, blist vac : 0x%x", usr_blk, vc[usr_blk]);
            vc[usr_blk] = 0;
#if(PLP_SUPPORT == 1)              
            is_skip_update = true;
#endif
        }

        if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_PLP_USER)
        {
            usr_page_threshold = ftl_spor_info->plpUSREndGrp*PGSN_GRP_TABLE_ENTRY;
            ftl_apl_trace(LOG_ALW, 0xf613, "usr blk : %d, grp threshold : %d", usr_blk, ftl_spor_info->plpUSREndGrp);
        }
        else if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_OPEN_USER)
        {
            usr_page_threshold = ftl_spor_info->openUSREndGrp*PGSN_GRP_TABLE_ENTRY;
        }
        else
        {
            usr_page_threshold = PGSN_FULL_TABLE_ENTRY;
        }

		if((had_host_data) && (mode == P2L_USR_ONLY))
		{
			usr_page_idx = adjust_host_page_idx;
			ftl_apl_trace(LOG_ALW, 0x97d5, "[SPOR]adj_h_pg_idx:%d",usr_page_idx);
			had_host_data = false;
        }
#if (SPOR_TRIM_DATA == ENABLE)
        if((ftl_spor_blk_node[usr_blk].nxt_blk == INV_U16) || test_bit(usr_blk, TrimBlkBitamp))
#else
        if((ftl_spor_blk_node[usr_blk].nxt_blk == INV_U16))
#endif
        {
#if(PLP_SUPPORT == 1)          
			//last usr blk
			is_skip_update = true;
#endif
        }
    }

    if((gc_page_idx==0) && (gc_blk != INV_U16))
    {
        if((ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P) &&
           (ftl_spor_info->l2p_data_error || ftl_spor_info->blist_data_error ||
            ftl_spor_info->epm_vac_error))
        {
            ftl_apl_trace(LOG_ALW, 0x0ac1, "gc blk : %d, blist vac : 0x%x", gc_blk, vc[gc_blk]);
            vc[gc_blk] = 0;
#if(PLP_SUPPORT == 1)              
            is_skip_update = true;
#endif
        }

        if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_PLP_GC)
        {
            gc_page_threshold = ftl_spor_info->plpGCEndGrp*PGSN_GRP_TABLE_ENTRY;
            ftl_apl_trace(LOG_ALW, 0xbb23, "gc blk : %d, grp threshold : %d", gc_blk, ftl_spor_info->plpGCEndGrp);
        }
        else if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_OPEN_GC)
        {
            gc_page_threshold = ftl_spor_info->openGCEndGrp*PGSN_GRP_TABLE_ENTRY;
        }
        else
        {
            gc_page_threshold = PGSN_FULL_TABLE_ENTRY;
        }

		if((had_gc_data) && (mode == P2L_GC_ONLY))
		{
			gc_page_idx = adjust_gc_page_idx;
			ftl_apl_trace(LOG_ALW, 0xe138, "[SPOR]adj_gc_pg_idx:%d",gc_page_idx);
			had_gc_data = false;
		}

#if (SPOR_TRIM_DATA == ENABLE)
        if((ftl_spor_blk_node[gc_blk].nxt_blk == INV_U16) || test_bit(gc_blk, TrimBlkBitamp))
#else
        if((ftl_spor_blk_node[gc_blk].nxt_blk == INV_U16))
#endif
        {
#if(PLP_SUPPORT == 1)          
			//last gc blk
			is_skip_update = true;
#endif
        }
    }


    // ===== different update mode =====
    switch(mode)
    {
        case P2L_USR_ONLY:
        {
#if(PLP_SUPPORT == 1)          
        	if((vc[usr_blk] != 0) || is_skip_update)
            	ftl_p2l_skip_bad_update(usr_blk, FTL_BLK_TYPE_HOST, &usr_page_idx, P2L_USR_ONLY, vc);
            else
            	usr_page_idx = usr_page_threshold;//force update done
#else
            ftl_p2l_skip_bad_update(usr_blk, FTL_BLK_TYPE_HOST, &usr_page_idx, P2L_USR_ONLY, vc);
#endif
            goto UPDATE_END;
            break;
        }

        case P2L_GC_ONLY:
        {  
#if(PLP_SUPPORT == 1)          
			if((vc[gc_blk] != 0)  || is_skip_update)
            	ftl_p2l_skip_bad_update(gc_blk, FTL_BLK_TYPE_GC, &gc_page_idx, P2L_GC_ONLY, vc);
            else
            	gc_page_idx = gc_page_threshold;//force update done
#else
            ftl_p2l_skip_bad_update(gc_blk, FTL_BLK_TYPE_GC, &gc_page_idx, P2L_GC_ONLY, vc);
#endif
            goto UPDATE_END;
            break;
        }

        default:
#if (SPOR_ASSERT == mENABLE)
            panic("no such mode!\n");
#else
            ftl_apl_trace(LOG_PANIC, 0xbfbd, "no such mode!");
#endif
    }

    UPDATE_END:
    // ===== du index reached threshold =====
    if((usr_page_idx>=usr_page_threshold) && (usr_blk != INV_U16))
    {
#if (SPOR_TRIM_DATA == ENABLE)
        ftl_apl_trace(LOG_ALW, 0x3143, "usr P2L_done blk:%d, vac:0x%x, p_id:0x%x , flag:0x%x, isTrimBlk 0x%x",
            usr_blk, vc[usr_blk], spb_info_tbl[usr_blk].pool_id,spb_get_flag(usr_blk),
            test_bit(usr_blk, TrimBlkBitamp));
#else
        ftl_apl_trace(LOG_ALW, 0x2d31, "usr P2L_done blk:%d, vac:0x%x, p_id:0x%x , flag:0x%x", usr_blk, vc[usr_blk], spb_info_tbl[usr_blk].pool_id,spb_get_flag(usr_blk));
#endif

        #if 0   // dbg log
        u32 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000);
        ftl_apl_trace(LOG_ALW, 0x5348, "L2P[0] : 0x%x", *((u32*)(u32)addr_base));
        #endif

        if(vc[usr_blk])
        {
            if(ftl_blk_aux_fail_cnt)
            {
                ftl_apl_trace(LOG_ALW, 0xd589, "Aux fail cnt : %d", ftl_blk_aux_fail_cnt);
                //vc[usr_blk]-= ftl_blk_aux_fail_cnt;
                ftl_blk_aux_fail_cnt = 0;
            }

            if((spb_info_tbl[usr_blk].pool_id != SPB_POOL_USER) &&
               (ftl_chk_blk_in_pool(SPB_POOL_USER, usr_blk) == mFALSE))
            {
                FTL_BlockPopPushList(SPB_POOL_USER, usr_blk, FTL_SORT_BY_SN);
            }
        }

        if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_PLP_USER)
        {
            ftl_spor_info->plpUSRBlk = usr_blk;
			if(ftl_spor_info->plpUSREndGrp == shr_p2l_grp_cnt){
				spb_set_flag(usr_blk, SPB_DESC_F_CLOSED);
			}
			else{
            spb_set_flag(usr_blk, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE));
			}
        }
        else if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_OPEN_USER)
        {
            spb_set_flag(usr_blk, SPB_DESC_F_OPEN);
        }

        usr_page_threshold = 0;
        usr_page_idx = 0;
        ftl_l2p_put_vcnt_buf(dtag, cnt, true);
        return USR_P2L_END;
    }
    else if((gc_page_idx>=gc_page_threshold) && (gc_blk != INV_U16))
    {        
#if (SPOR_TRIM_DATA == ENABLE)
        ftl_apl_trace(LOG_ALW, 0x6f35, "gc P2L_done blk:%d, vac:0x%x, p_id:0x%x, flag:0x%x, isTrimBlk 0x%x",
            gc_blk, vc[gc_blk], spb_info_tbl[gc_blk].pool_id,spb_get_flag(gc_blk),
            test_bit(gc_blk, TrimBlkBitamp));
#else
        ftl_apl_trace(LOG_ALW, 0x640c, "gc P2L_done blk:%d, vac:0x%x, p_id:0x%x, flag:0x%x", gc_blk, vc[gc_blk], spb_info_tbl[gc_blk].pool_id,spb_get_flag(gc_blk));
#endif

        #if 0   // dbg log
        u32 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000);
        ftl_apl_trace(LOG_ALW, 0xd036, "L2P[0] : 0x%x", *((u32*)(u32)addr_base));
        #endif

        if(vc[gc_blk])
        {
            if(ftl_blk_aux_fail_cnt)
            {
                ftl_apl_trace(LOG_ALW, 0x89c9, "Aux fail cnt : %d", ftl_blk_aux_fail_cnt);
                //vc[gc_blk]-= ftl_blk_aux_fail_cnt;
                ftl_blk_aux_fail_cnt = 0;
            }

            if((spb_info_tbl[gc_blk].pool_id != SPB_POOL_USER) &&
               (ftl_chk_blk_in_pool(SPB_POOL_USER, gc_blk) == mFALSE))
            {
                FTL_BlockPopPushList(SPB_POOL_USER, gc_blk, FTL_SORT_BY_SN);
            }
        }

        if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_PLP_GC)
        {
            ftl_spor_info->plpGCBlk = gc_blk;
			if(ftl_spor_info->plpGCEndGrp == shr_p2l_grp_cnt){
				spb_set_flag(gc_blk, SPB_DESC_F_CLOSED);
			}
			else{
            spb_set_flag(gc_blk, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE));
			}
        }
        else if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_OPEN_GC)
        {
            spb_set_flag(gc_blk, SPB_DESC_F_OPEN);
        }

        gc_page_threshold = 0;
        gc_page_idx = 0;
        ftl_l2p_put_vcnt_buf(dtag, cnt, true);
        return GC_P2L_END;
    }

    return 0;
}

/*!
 * @brief ftl spor function
 * decide which mode to update P2L
 *
 * @return	not used
 */
ddr_code u8 ftl_blk_update_order(u16 usr_blk, u16 gc_blk)
{
    u8 mode = 0;

    if((usr_blk != INV_U16)&&(gc_blk != INV_U16))
    {
        mode = P2L_ALL;
    }
    else if((usr_blk != INV_U16)&&(gc_blk == INV_U16))
    {
        mode = P2L_USR_ONLY;
    }
    else if((usr_blk == INV_U16)&&(gc_blk != INV_U16))
    {
        mode = P2L_GC_ONLY;
    }

#if (SPOR_ASSERT == mENABLE)
    sys_assert(mode!=0);
#endif

    return mode;
}



/*!
 * @brief ftl spor function
 * read blist/vc tbl table from Host block
 *
 * @return	not used
 */
init_code bool ftl_scan_blank_pos(pda_t pda_start, u16 wl_threshold, u8 wl_align)
{
	spor_skip_raid_tag = true;
	u16    wl_idx;
	u32    die_pn_idx;
#ifdef SKIP_MODE
	u8	   *df_ptr;
	u32    idx, off;
#endif

    pda_t  pda = 0;
    dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};
    bool wl_cmd_send;
    pda_t blank_pda_bk = ftl_spor_info->blank_pda;

    ftl_apl_trace(LOG_ALW, 0x35f7, "[IN] ftl_scan_blank_pos");

    ftl_spor_info->blank_pda = INV_U32;

    ftl_apl_trace(LOG_ALW, 0x66b6, "[Before] pda : 0x%x, Die : %d, PN : %d, WL : %d, PG in WL : %d, DU :%d",\
                  pda_start, ftl_pda2die(pda_start), ftl_pda2plane(pda_start),\
                  ftl_pda2wl(pda_start), (ftl_pda2page(pda_start)%FTL_PG_IN_WL_CNT), ftl_pda2du(pda_start));


#ifdef SKIP_MODE
		df_ptr = ftl_get_spb_defect(pda2blk(pda_start));
#endif

    // start from next physical WL, original WL may not program complete
    pda = ftl_set_du2pda(pda_start, 0);
    pda = ftl_set_pg2pda(pda, ftl_pda2wl(pda)*FTL_PG_IN_WL_CNT);

    ftl_apl_trace(LOG_ALW, 0x16f2, "[After] pda : 0x%x, Die : %d, PN : %d, WL : %d, PG in WL : %d, DU : %d",\
                  pda, ftl_pda2die(pda), ftl_pda2plane(pda),\
                  ftl_pda2wl(pda), (ftl_pda2page(pda)%FTL_PG_IN_WL_CNT), ftl_pda2du(pda));


    wl_idx = ftl_pda2wl(pda);
    die_pn_idx = ftl_pda2die(pda)*ftl_pda2plane(pda);
    while(wl_idx < wl_threshold)
    {
        wl_cmd_send = false;
        while(die_pn_idx < FTL_TOT_PN_CNT)
        {
#ifdef SKIP_MODE
			idx = die_pn_idx >> 3;
			off = die_pn_idx & (7);
			if(((df_ptr[idx] >> off)&1)==0)
#endif

            {
                wl_cmd_send = true;
                // is not bad block
                pda = ftl_set_pn2pda(pda,  (die_pn_idx%FTL_PN_CNT));
                pda = ftl_set_die2pda(pda, (die_pn_idx>>FTL_PN_SHIFT));
                pda = ftl_set_pg2pda(pda,   wl_idx*FTL_PG_IN_WL_CNT);

                //ftl_apl_trace(LOG_ALW, 0, "pda 0x%x, wl %u die %u plane %u", pda, ftl_pda2wl(pda), ftl_pda2die(pda),die_pn_idx%FTL_PN_CNT);
                ftl_send_read_ncl_form(NCL_CMD_SPOR_SCAN_BLANK_POS, &dtag, pda, DU_CNT_PER_PAGE, ftl_read_p2l_from_nand_done);
            }

            if(wl_align && wl_cmd_send)
                break;

            die_pn_idx++;

            if(ftl_spor_info->blank_pda != INV_U32) {break;}
        }

        die_pn_idx = 0;
        wl_idx++;

        if(ftl_spor_info->blank_pda != INV_U32) {break;}
    }

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
	spor_skip_raid_tag = false;
    if(ftl_spor_info->blank_pda == INV_U32){
        ftl_apl_trace(LOG_ALW, 0x09bf, "first page no blank blank_pda : 0x%x -> 0x%x", ftl_spor_info->blank_pda, blank_pda_bk);
        ftl_spor_info->blank_pda = blank_pda_bk;
        return false;
    }

    ftl_apl_trace(LOG_ALW, 0x082d, "real blank pda pos : 0x%x", ftl_spor_info->blank_pda);
    return true;
}

/*!
 * @brief ftl spor function
 * scan blank wl for die schduler
 *
 * @return	not used
 */

init_code void ftl_scan_wl_blank_pos(spb_id_t spb_id,u16 wl_id )
{
	spor_die_sch_wl = true;
	spor_skip_raid_tag = true;
	dtag_t dtag = {.dtag = (ftl_dummy_dtag | DTAG_IN_DDR_MASK)};
	//u8     *df_ptr;
	//u32    idx, off;
	//u32    die_pn_idx =0;
	pda_t  pda = 0;
	u8 die_id,pn_pair;
	u32 pn_ptr;

	pda = ftl_set_blk2pda(pda,spb_id);
	pda = ftl_set_du2pda(pda , 0);
	//pda = ftl_set_die2pda(pda, 0);
	pda = ftl_set_pn2pda(pda, 0);
    pda = ftl_set_pg2pda(pda, wl_id*FTL_PG_IN_WL_CNT);
	//df_ptr = ftl_get_spb_defect(spb_id);

	for(die_id = 0 ;die_id < FTL_DIE_CNT;die_id++)
	{
#ifdef SKIP_MODE
		pn_ptr = die_id << FTL_PN_SHIFT;
		pn_pair = ftl_good_blk_in_pn_detect(spb_id, pn_ptr);
#else
		pn_pair = BLOCK_ALL_BAD;
#endif
		//log
		if(pn_pair != BLOCK_NO_BAD)
			ftl_apl_trace(LOG_ALW, 0xe3ac, "die:%d pn_pair:0x%x",die_id,pn_pair);

		if(pn_pair == BLOCK_ALL_BAD) 
		{
			//ftl_apl_trace(LOG_ALW, 0, "die:%d BLOCK_ALL_BAD",die_id);
		}
		else
		{
			u8 pln_idx;
			for(pln_idx=0;pln_idx<FTL_PN_CNT;pln_idx++)
			{
				if(BIT_CHECK(pn_pair, pln_idx)==0)
				{
					pda = ftl_set_die2pda(pda,die_id);
					pda = ftl_set_pn2pda(pda,pln_idx);
					ftl_send_read_ncl_form(NCL_CMD_SPOR_SCAN_BLANK_POS, &dtag, pda, DU_CNT_PER_PAGE, ftl_read_p2l_from_nand_done);
					break;
				}
			}
		}
	}


	// ----- wait all form done -----
	wait_spor_cmd_idle();  

	spor_skip_raid_tag = false;
	spor_die_sch_wl = false;

}



/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
init_code u8 ftl_is_last_wl_p2l(u16 spb_id, u32 nsid, pda_t pda)
{
    pda_t lda_buf_pda, pgsn_buf_pda, pda_buf_pda;

    ftl_p2l_position_detect((shr_p2l_grp_cnt-1), spb_id, nsid, &lda_buf_pda, &pgsn_buf_pda);
    pda_buf_pda = pgsn_buf_pda + PGSN_GRP_TABLE_DU_CNT;

    if((pda == lda_buf_pda) || (pda == pgsn_buf_pda) || (pda == pda_buf_pda))
    {
        return mTRUE;
    }

    return mFALSE;
}

/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
#if(PLP_SUPPORT == 1)  
fast_code void ftl_aux_pgsn_buffer_sorting(u32 pgsn_dtag_start, u32 pda_dtag_start, u16 grp_id)
{
    pda_t *pda_buf_start = NULL;
    u64   *pgsn_buf_start = NULL;
    u32    idx, first_inv_idx = 0;
    u8     inv_idx_record = mFALSE;
    u32    threshold;

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_aux_pgsn_buffer_sorting");

    pda_buf_start  = (pda_t*)ddtag2mem(pda_dtag_start);
    pgsn_buf_start = (u64*)ddtag2mem(pgsn_dtag_start);

    idx = grp_id*PGSN_GRP_TABLE_ENTRY;
    threshold = idx+PGSN_GRP_TABLE_ENTRY;

    while(idx < threshold)
    {
        if((inv_idx_record == mFALSE) && (pgsn_buf_start[idx] == INV_U64))
        {
            // record first invalid entry index
            first_inv_idx  = idx;
            inv_idx_record = mTRUE;
        }
        else if((inv_idx_record == mTRUE) && (pgsn_buf_start[idx] != INV_U64))
        {
            // found first non-invalid entry after invalid entry index
            pda_buf_start[first_inv_idx]  = pda_buf_start[idx];
            pgsn_buf_start[first_inv_idx] = pgsn_buf_start[idx];
            pda_buf_start[idx]  = INV_U32;
            pgsn_buf_start[idx] = INV_U64;
            idx = first_inv_idx;
            inv_idx_record = mFALSE;
        }

        idx++;
    }
}
#else  //nonplp
/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
init_code void ftl_aux_pgsn_buffer_sorting(u16 spb_id, u16 grp_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start)
{
    pda_t *pda_buf_start = NULL;
    u64   *pgsn_buf_start = NULL;
    pda_t *lda_buf_start = NULL;
    u32    idx, first_inv_idx = 0;
    u8     inv_idx_record = mFALSE;
    u32    threshold;

    u8 wl_idx_aux,i,pg_idx,d_idx;
    u32 buf_idx = 0/*,lda_buf_idx = 0*/; 

    //u32 grp_duidx = 0;
    u16 grp_ofset = 0;
    u16 pg_per_swl=0 ,/*du_per_upswl=0, du_plane_list=0,*/ pg_per_die=0;
    epm_FTL_t* epm_ftl_data;
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

    u8 win, old_win;
    pda_t old_pda = 0; 
    u64 addr; 
    u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000); 
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))}; 

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_aux_pgsn_buffer_sorting");
    lda_buf_start  = (pda_t*)ddtag2mem(lda_dtag_start);
    pda_buf_start  = (pda_t*)ddtag2mem(pda_dtag_start);
    pgsn_buf_start = (u64*)ddtag2mem(pgsn_dtag_start);

    //grp_duidx = grp_id*FTL_DU_PER_GRP;
    idx = grp_id*PGSN_GRP_TABLE_ENTRY;
    threshold = idx+PGSN_GRP_TABLE_ENTRY;

    pg_per_swl = FTL_DU_PER_SWL/DU_CNT_PER_PAGE;
    pg_per_die = FTL_PN_CNT*FTL_PG_IN_WL_CNT;
    //du_plane_list = FTL_PN_CNT*DU_CNT_PER_PAGE; //up/mid/top die have du cnt 2T 4pl*4du 
    //du_per_upswl = FTL_DU_PER_SWL/FTL_PG_IN_WL_CNT;//up/mid/top swl have du cnt 2T 32die*4pl*4du 


	u16 *open_wl_tpye = NULL;
    u32 *die_bit_type = NULL;
    u32 new_pda = INV_PDA;
    lda_t lda 	= INV_LDA;
    if(epm_ftl_data->host_open_blk[0] == spb_id)
    {
		open_wl_tpye = epm_ftl_data->host_open_wl;
		die_bit_type = epm_ftl_data->host_die_bit;
    }
    else if(epm_ftl_data->gc_open_blk[0] == spb_id)
    {
		open_wl_tpye = epm_ftl_data->gc_open_wl;
		die_bit_type = epm_ftl_data->gc_die_bit;
    }
 	else
 	{
		goto SKIP_RECHK_L2P;
 	}

     for(wl_idx_aux=0;wl_idx_aux<SPOR_CHK_WL_CNT;wl_idx_aux++)//chk wl cnt
     {
         if(open_wl_tpye[wl_idx_aux]/shr_wl_per_p2l == grp_id)
         {
             grp_ofset = open_wl_tpye[wl_idx_aux]%shr_wl_per_p2l;//wl
             for(i=0;i<FTL_DIE_CNT;i++)//die cnt pre wl
             {
                 if((die_bit_type[wl_idx_aux] >> i)&0x1)//check die bit
                 {
                    for(pg_idx=0; pg_idx<FTL_PN_CNT*FTL_PG_IN_WL_CNT;pg_idx++)//page cnt per die
                     {     
                         buf_idx = idx+grp_ofset*pg_per_swl+pg_per_die*i+pg_idx;
                         //ftl_apl_trace(LOG_ALW, 0, "idx %d die %d  pg %d pda 0x%x", buf_idx, i, pg_idx, pda_buf_start[buf_idx]);
						 if(pda_buf_start[buf_idx] != INV_U32)
						 {
						 	new_pda = pda_buf_start[buf_idx];
		                     for(d_idx=0;d_idx<DU_CNT_PER_PAGE;d_idx++)//du cnt pre page
		                     {
		                     	lda = lda_buf_start[(new_pda&FTL_PDA_DU_MSK)];
		                         if(lda < _max_capacity)
		                         {                                 
		                             addr = addr_base + ((u64)lda << 2); 
		                             win  = 0; 
		                             while (addr >= 0xC0000000) { 
		                                 addr -= 0x80000000; 
		                                 win++; 
		                             } 

		                             old_win = ctrl0.b.cpu3_ddr_window_sel; 
		                             ctrl0.b.cpu3_ddr_window_sel = win; 
		                             isb(); 
		                             writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
		                             ftl_recheck_l2p_table(lda, new_pda, &old_pda, (u32)addr); 
		                             ctrl0.b.cpu3_ddr_window_sel = old_win; 
		                             writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
		                             isb(); 
		                             ftl_apl_trace(LOG_ALW, 0x09d2, "[NONPLP] LDA 0x%x new_pda:0x%x old_pda:0x%x old_blk:%d",lda,new_pda,old_pda,pda2blk(old_pda));
		                         }
		                         new_pda++;
		                         //lda_buf_start[lda_buf_idx]  = INV_U32;    //grp ,swl , up/mid/top swl ,                          
		                     }
						 }                  
                         pda_buf_start[buf_idx]  = INV_U32;//grp swl die pg
                         pgsn_buf_start[buf_idx] = INV_U64;
                     }
                 }
             }
         }
     }

    #if 0
     if(epm_ftl_data->gc_open_blk[0] == spb_id)  //open gc blk
     {    
         for(wl_idx_aux=0;wl_idx_aux<SPOR_CHK_WL_CNT;wl_idx_aux++)
         {
             if(epm_ftl_data->gc_open_wl[wl_idx_aux]/shr_wl_per_p2l == grp_id)
             {
                 grp_ofset = epm_ftl_data->gc_open_wl[wl_idx_aux]%shr_wl_per_p2l;//wl
                 for(i=0;i<FTL_DIE_CNT;i++)
                 {
                     if((epm_ftl_data->gc_die_bit[wl_idx_aux] >> i)&0x1)//check die bit
                     {
                        u8 pg_up_mid_idx = 0,pl_list = 0;
                        for(pg_idx=0; pg_idx<FTL_PN_CNT*FTL_PG_IN_WL_CNT;pg_idx++)
                         {     
                             buf_idx = idx+grp_ofset*pg_per_swl+pg_per_die*i+pg_idx;
                             //ftl_apl_trace(LOG_ALW, 0, "idx %d die %d  pg %d pda 0x%x", buf_idx, i, pg_idx, pda_buf_start[buf_idx]);
                             pda_buf_start[buf_idx]  = INV_U32;
                             pgsn_buf_start[buf_idx] = INV_U64;
                             if((pg_idx % FTL_PN_CNT == 0)&&(pg_idx != 0))
                             {
                                 pg_up_mid_idx++;
                                 pl_list = 0;
                             }                    
                             for(d_idx=0;d_idx<DU_CNT_PER_PAGE;d_idx++)//
                             {
                                 lda_buf_idx = grp_duidx+grp_ofset*FTL_DU_PER_SWL+du_per_upswl*pg_up_mid_idx+du_plane_list*i+pl_list*DU_CNT_PER_PAGE+d_idx; 
                                 //ftl_apl_trace(LOG_ALW, 0, "idx %d  lda 0x%x  ii %d", lda_buf_idx, lda_buf_start[lda_buf_idx] , pg_up_mid_idx);
                                 if(lda_buf_start[lda_buf_idx] < _max_capacity)
                                 {                                 
                                     addr = addr_base + ((u64)lda_buf_start[lda_buf_idx] << 2); 
                                     win  = 0; 
                                     while (addr >= 0xC0000000) { 
                                         addr -= 0x80000000; 
                                         win++; 
                                     } 

                                     old_win = ctrl0.b.cpu3_ddr_window_sel; 
                                     ctrl0.b.cpu3_ddr_window_sel = win; 
                                     isb(); 
                                     writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
                                     ftl_update_l2p_table(lda_buf_start[lda_buf_idx], INV_LDA, &old_pda, (u32)addr); 
                                     ctrl0.b.cpu3_ddr_window_sel = old_win; 
                                     writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0)); 
                                     isb(); 
                                     ftl_apl_trace(LOG_ALW, 0x1929, "[NONPLP] gc LDA 0x%x -new PDA: 0x%x old PDA: 0x%x",lda_buf_start[lda_buf_idx],INV_LDA, old_pda);
                                 }
                                 lda_buf_start[lda_buf_idx]  = INV_U32;                                
                             }
                             pl_list++;
                         }
                     }
                 }
             }
         }
     }
	#endif
SKIP_RECHK_L2P:

    while(idx < threshold)
    {
        if((inv_idx_record == mFALSE) && (pgsn_buf_start[idx] == INV_U64))
        {
            // record first invalid entry index
            first_inv_idx  = idx;
            inv_idx_record = mTRUE;
        }
        else if((inv_idx_record == mTRUE) && (pgsn_buf_start[idx] != INV_U64))
        {
            // found first non-invalid entry after invalid entry index
            pda_buf_start[first_inv_idx]  = pda_buf_start[idx];
            pgsn_buf_start[first_inv_idx] = pgsn_buf_start[idx];
            pda_buf_start[idx]  = INV_U32;
            pgsn_buf_start[idx] = INV_U64;
            idx = first_inv_idx;
            inv_idx_record = mFALSE;
        }

        idx++;
    }
}
#endif

/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
init_code void ftl_wl_p2l_table_rebuild(u16 spb_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start, u32 nsid)
{
    u16 grp_idx;

    ftl_apl_trace(LOG_ALW, 0x5b4c, "[IN] ftl_wl_p2l_table_rebuild, p2l_fail_cnt : %d ", ftl_p2l_fail_cnt);

    ftl_blk_aux_fail_cnt = 0;
    for(grp_idx = 0; grp_idx < shr_p2l_grp_cnt; grp_idx++)
    {
        if(ftl_p2l_fail_grp_flag[grp_idx])
        {
            // ToDo : if it is open WL, don't enter raid recover

            ftl_apl_trace(LOG_ALW, 0x6911, "fail grp_idx:%d wl_S:%d wl_E:%d", grp_idx,grp_idx * shr_wl_per_p2l,grp_idx * shr_wl_per_p2l + shr_wl_per_p2l-1);
            ftl_page_aux_build_p2l(spb_id, grp_idx, lda_dtag_start, pgsn_dtag_start, pda_dtag_start);
#if(PLP_SUPPORT == 1)              
            ftl_aux_pgsn_buffer_sorting(pgsn_dtag_start, pda_dtag_start, grp_idx);
#else
            ftl_aux_pgsn_buffer_sorting(spb_id,grp_idx,lda_dtag_start, pgsn_dtag_start, pda_dtag_start);
#endif
        }
    }
}

#if (SPOR_PLP_WL_CHK_COMPLETE == mENABLE)
/*!
 * @brief ftl spor function
 * find non-defect first pda
 *
 * @return	not used
 */
#ifdef SKIP_MODE
init_code pda_t ftl_skip_defect_pda(pda_t pda)
{
    u8    *df_ptr;
    u32   idx, off;
    u16   die_pn_idx;

    df_ptr = ftl_get_spb_defect(pda2blk(pda));
    for(die_pn_idx = 0; die_pn_idx < FTL_TOT_PN_CNT; die_pn_idx++)
    {
        //ftl_apl_trace(LOG_ALW, 0, "[Cur] die_pn_idx : %d", die_pn_idx);
        idx = die_pn_idx >> 3;
     	off = die_pn_idx & (7);
      	if(((df_ptr[idx] >> off)&1)==0)
        {
            // is not bad block
            pda = ftl_set_pn2pda(pda,  (die_pn_idx%FTL_PN_CNT));
            pda = ftl_set_die2pda(pda, (die_pn_idx>>FTL_PN_SHIFT));
            break;
        }
    }

    return pda;
}
#endif


/*!
 * @brief ftl spor function
 * find non-defect first pda
 *
 * @return	not used
 */
init_code u8 ftl_ckh_plp_wl_complete(pda_t pda)
{
    u16 wl_idx = ftl_pda2wl(pda);

    ftl_apl_trace(LOG_ALW, 0xf592, "[IN] ftl_ckh_plp_wl_complete");

    // check current wl last page

    ftl_scan_blank_pos(pda, (wl_idx+1),false);
    if(ftl_spor_info->blank_pda != INV_U32)
    {
        // wl not complete
        return mFALSE;
    }

    // check next wl first page
    wl_idx++;
    pda = ftl_set_die2pda(pda, 0);
    pda = ftl_set_pn2pda(pda, 0);
    pda = ftl_set_pg2pda(pda, (wl_idx*FTL_PG_IN_WL_CNT));
#ifdef SKIP_MODE
    pda = ftl_skip_defect_pda(pda);
#endif
    ftl_scan_blank_pos(pda, (wl_idx+1),false);
    if((pda != ftl_spor_info->blank_pda) &&
       (pda != ftl_spor_info->first_dummy_pda))
    {
        // wl not complete
        return mFALSE;
    }

    return mTRUE;
}
#endif//(SPOR_PLP_WL_CHK_COMPLETE == mENABLE)

/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
init_code void ftl_extract_p2l_table(u16 spb_id, u32 lda_dtag_start, u32 pgsn_dtag_start, u32 pda_dtag_start, u32 nsid)
{
    u16 grp_threshold = 0, grp_idx, blank_grp_idx = 0,blank_wl_idx = 0, p2l_grp_idx = 0;
	u16 dummy_wl_idx = 0, p2l_wl_idx = 0;
    u8  dummy_detected = mFALSE;
    u8  fill_dummy = mTRUE;
    u8  open_flag = mFALSE;

    ftl_spor_info->read_data_fail  = mFALSE;
    ftl_spor_info->read_data_blank = mFALSE;
    ftl_spor_info->first_dummy_pda = INV_U32;
    ftl_spor_info->last_dummy_pda  = INV_U32;
    ftl_spor_info->blank_pda       = INV_U32;
    ftl_spor_info->p2l_pda         = INV_U32;
#if (PLP_SUPPORT == 0)    
	//epm_FTL_t *epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	u16 first_aux_grp = INV_U16;
	cur_blank_grp = INV_U16;
	cur_blank_spb = INV_U16;

    ftl_spor_incomp_fag  = false;
    epm_first_open_wl    = 0xFFFF;
#endif    

    if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_USER) ||
       (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_GC))
    {
        // ===== close block handle =====
        memset(ftl_p2l_fail_grp_flag, 0, (sizeof(u8)*shr_p2l_grp_cnt));
        ftl_p2l_fail_cnt = 0;
        ftl_read_p2l_from_nand(spb_id, lda_dtag_start, pgsn_dtag_start, pda_dtag_start, nsid);
		spb_set_flag(spb_id,SPB_DESC_F_CLOSED);
        // if current close block is also PLP block
        if(ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER)
        {
            if(ftl_spor_info->last_dummy_pda != INV_U32)
            {
                // if close block has p2l is filled with dummy
                grp_idx = ftl_pda2grp(ftl_spor_info->last_dummy_pda);
                if((grp_idx == (shr_p2l_grp_cnt-1)) &&
                   ftl_is_last_wl_p2l(spb_id, nsid, ftl_spor_info->last_dummy_pda))
                {
                   ftl_spor_info->plpUSREndGrp = shr_p2l_grp_cnt - 1;
                }
                else
                {
                   ftl_spor_info->plpUSREndGrp = grp_idx - 1;
                }
            }
            else
            {
                ftl_spor_info->plpUSREndGrp = shr_p2l_grp_cnt;
            }
        }
        else if(ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC)
        {
            if(ftl_spor_info->last_dummy_pda != INV_U32)
            {
                // if close block has p2l is filled with dummy
                grp_idx = ftl_pda2grp(ftl_spor_info->last_dummy_pda);
                if((grp_idx == (shr_p2l_grp_cnt-1)) &&
                   ftl_is_last_wl_p2l(spb_id, nsid, ftl_spor_info->last_dummy_pda))
                {
                   ftl_spor_info->plpGCEndGrp = shr_p2l_grp_cnt - 1;
                }
                else
                {
                   ftl_spor_info->plpGCEndGrp = grp_idx - 1;
                }
            }
            else
            {
                ftl_spor_info->plpGCEndGrp = shr_p2l_grp_cnt;
            }
        }

        //if close blk fill with spor dummy(open blk->close blk)  
		//the last two group need aux build(because it's p2l is dummy) 
		bool auxbuild = false;
		if(ftl_spor_info->first_dummy_pda != INV_U32)
		{
			//special case: close blk(fill spor dummy) need aux build
			p2l_grp_idx   = (ftl_spor_info->p2l_pda == INV_U32) ? 0 : ftl_pda2grp(ftl_spor_info->p2l_pda);         
			grp_threshold = p2l_grp_idx;
			grp_threshold = (ftl_spor_p2l_fail_chk(p2l_grp_idx) == mTRUE) ? (grp_threshold+1) : grp_threshold;
            grp_threshold = (ftl_spor_p2l_fail_chk(p2l_grp_idx+1) == mTRUE) ? (grp_threshold+1) : grp_threshold;
      		ftl_apl_trace(LOG_ALW, 0x5bc7, "close blk aux build>> p2l_pda:0x%x,first_dummy_pda:0x%x,last_dummy_pda:0x%x",\
         		ftl_spor_info->p2l_pda,ftl_spor_info->first_dummy_pda,ftl_spor_info->last_dummy_pda);
        	auxbuild = true;
#if (PLP_SUPPORT == 0)
			first_aux_grp = p2l_grp_idx;
            if(p2l_grp_idx > 0)
            {
            	first_aux_grp--;
                ftl_spor_p2l_fail_chk(first_aux_grp);//nonplp
            }
            ftl_set_non_plp_blk(false,  spb_id, first_aux_grp, grp_threshold);
#endif   
			//spb_set_flag(spb_id, SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE); //set blk open

		}
		// read data fail   
        if(ftl_spor_info->read_data_fail || 
           ftl_spor_info->read_data_blank) 
        { 
            ftl_spor_info->p2l_data_error = mTRUE; 
            auxbuild = true;
#if (PLP_SUPPORT == 0)
			if(first_aux_grp != INV_U16) //spor dummy blk,but last wl incomplete
			{
				//do nothing
			}
			else if(cur_blank_spb == spb_id)
			{
				first_aux_grp = cur_blank_grp;
				//ftl_spor_p2l_fail_chk(first_aux_grp);
            	ftl_set_non_plp_blk(false,  spb_id, first_aux_grp, shr_p2l_grp_cnt);
            	ftl_apl_trace(LOG_ALW, 0xaf58, "close blk need aux build!! first_aux_grp:%d ",first_aux_grp);
			}
#endif

        } 

        if(auxbuild)
        {
            ftl_wl_p2l_table_rebuild(spb_id, lda_dtag_start, pgsn_dtag_start, pda_dtag_start, nsid);

			spb_clear_flag(spb_id,SPB_DESC_F_CLOSED);
            spb_set_flag(spb_id, SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE);

		}
    }
    else
    {
        // ===== open/PLP block handle =====
#if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
		u8 index   = spb_id >> 5;
		u8 offset  = spb_id & 0x1f;
		bool plp_fcore_flush_p2l =(ftl_special_plp_blk[index] >> offset) & 1;
#endif
        memset((lda_t*)ddtag2mem(lda_dtag_start), 0xFF, LDA_FULL_TABLE_SIZE);
        memset((pda_t*)ddtag2mem(pda_dtag_start), 0xFF, PDA_FULL_TABLE_SIZE);
        memset((u64*)ddtag2mem(pgsn_dtag_start),  0xFF, PGSN_FULL_TABLE_SIZE);

        ftl_scan_first_blank_p2l_pos(spb_id, nsid);

		p2l_grp_idx = (ftl_spor_info->p2l_pda == INV_U32) ? 0 : ftl_pda2grp(ftl_spor_info->p2l_pda);
        p2l_wl_idx = (ftl_spor_info->p2l_pda == INV_U32) ? 0 : ftl_pda2wl(ftl_spor_info->p2l_pda);

        // added for blk has PLP tag but last WL incomplete protection
        #if (SPOR_PLP_WL_CHK_COMPLETE == mENABLE)
        if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER) ||
           (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC))
        {
            if(!ftl_ckh_plp_wl_complete(ftl_spor_info->p2l_pda))
            {
                ftl_apl_trace(LOG_ALW, 0x9a56, "P2L not comp");
                if(ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_USER)
                {
                    ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_OPEN_USER;
                }
                else if(ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_PLP_GC)
                {
                    ftl_build_tbl_type[spb_id] = FTL_SN_TYPE_OPEN_GC;
                }
                else
                {
                    ftl_apl_trace(LOG_ALW, 0x1aa1, "Err SPB type : 0x%x",\
                                  ftl_build_tbl_type[spb_id]);
                }
            }
        }
        #endif

		blank_grp_idx = (ftl_spor_info->blank_pda == INV_U32) ? 0 : ftl_pda2grp(ftl_spor_info->blank_pda);
		blank_wl_idx  = (ftl_spor_info->blank_pda == INV_U32) ? 0 : ftl_pda2wl(ftl_spor_info->blank_pda);

		ftl_apl_trace(LOG_ALW, 0xe924, "spb_id %u ftl_build_tbl_type[spb_id] %u, shr_wl_per_p2l %u",spb_id, ftl_build_tbl_type[spb_id], shr_wl_per_p2l);

        ftl_apl_trace(LOG_ALW, 0x3913, "last P2L pda : 0x%x, p2l_grp_idx : %u ,p2l_wl_idx: %u",ftl_spor_info->p2l_pda, p2l_grp_idx,p2l_wl_idx);

        // "first P2L blank pda"
        ftl_apl_trace(LOG_ALW, 0x6170, "first WL blank pda : 0x%x, blank_grp_idx : %u , blank_wl_idx : %u",ftl_spor_info->blank_pda, blank_grp_idx,blank_wl_idx);

        ftl_apl_trace(LOG_ALW, 0xd88a, "first Dummy pda : 0x%x, grp idx : %u",ftl_spor_info->first_dummy_pda, ftl_pda2grp(ftl_spor_info->first_dummy_pda));

        ftl_apl_trace(LOG_ALW, 0xdf0e, "last Dummy pda : 0x%x, grp idx : %u",ftl_spor_info->last_dummy_pda, ftl_pda2grp(ftl_spor_info->last_dummy_pda));


        // ===== to check the first blank and first dummy pda pos =====
        if(ftl_spor_info->blank_pda == INV_U32)
        {
        	// if last WL's P2L table already written in NAND
            ftl_spor_info->blank_pda = (FTL_DU_PER_TLC_SBLOCK-1);
            ftl_spor_info->blank_pda = ftl_set_blk2pda(ftl_spor_info->blank_pda, spb_id);
            grp_threshold = shr_p2l_grp_cnt;
        #if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
            ftl_apl_trace(LOG_ALW, 0x93f4, "open block, but block full  plp_focre_tag:%d",plp_fcore_flush_p2l);
            if(plp_fcore_flush_p2l) 
				fill_dummy   = mTRUE;
            else
				fill_dummy   = mFALSE; 
        #endif  
        }
        else
        {
            if(ftl_spor_info->last_dummy_pda != INV_U32)
            {
                // if already has dummy in blk, check if dummy has filled 1 layer(4WL)
                //dummy_grp_idx = ftl_pda2grp(ftl_spor_info->last_dummy_pda);
                dummy_wl_idx = ftl_pda2wl(ftl_spor_info->last_dummy_pda);
                dummy_detected = mTRUE;
                if((dummy_wl_idx - p2l_wl_idx) >= FTL_WL_PER_LAYER )
                {
                    fill_dummy = mFALSE;
                    ftl_apl_trace(LOG_ALW, 0xb2d8, "open block fill spor dummy,no need fill again"); 
                }
            }
			//may bug?   --Jay mark
            grp_threshold = p2l_grp_idx;
        } // end if(ftl_spor_info->blank_pda == INV_U32)

        if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER) ||
           (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
	    {
	        open_flag = mTRUE;
		}

#if (SPOR_FILL_DUMMY == mDISABLE)
		fill_dummy = mFALSE;//PJ1 Jira38,GC retry incomplete WL enter timeout
#endif

        if(!fill_dummy) {goto READ_P2L;}

        // ===== to see if dummy already filled last time =====
        if(!dummy_detected)
        {
            // ===== first time fill dummy layer =====
            if(!open_flag)
            {
                // PLP blk fill dummy
                 // start to fill dummy from open wl p2l pda    
   				pda_t p2l_blank_pda = ftl_spor_info->blank_pda;
                if(shr_wl_per_p2l>1){
                	ftl_spor_info->last_dummy_pda = INV_U32;
                    pda_t pda_tmp = 0;

                    if(ftl_spor_info->p2l_pda == INV_PDA)
                    {
						pda_tmp = ftl_set_blk2pda(0, spb_id);
                    }
                    else
                    {
						pda_tmp = ftl_spor_info->p2l_pda;
                    }

                    ftl_scan_blank_pos(pda_tmp, ftl_pda2wl(ftl_spor_info->blank_pda) + 1, true);
                    ftl_apl_trace(LOG_ALW, 0xa941, "p2l_blank_pda:0x%x,wl:%d, real balnk pda:0x%x,wl:%d",
                        p2l_blank_pda, ftl_pda2wl(p2l_blank_pda), ftl_spor_info->blank_pda,ftl_pda2wl(ftl_spor_info->blank_pda));                   
                }
                p2l_blank_pda = ftl_spor_info->blank_pda;
				u16 wl_fill_count = FTL_WL_PER_LAYER;
				#if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)			
				u16 wl_fill_start = ftl_pda2wl(ftl_spor_info->blank_pda);
				bool ret = true;
				if((wl_fill_start + wl_fill_count) >= (FTL_WL_CNT - 1))
				{
					pda_t pda_last_wl = 0;
					pda_last_wl = ftl_set_blk2pda(pda_last_wl, spb_id); 
					pda_last_wl = ftl_set_pg2pda(pda_last_wl, ((FTL_WL_CNT -1)*FTL_PG_IN_WL_CNT));
					ret = ftl_scan_blank_pos(pda_last_wl,FTL_WL_CNT,true);
					if(ret == false) //last wl has fill dummy!!!
						wl_fill_count = FTL_WL_CNT - 1 - wl_fill_start;
				}
				ftl_apl_trace(LOG_ALW, 0x765e, "wl_fill_start:%d,wl_fill_count:%d,end_wl:%d,FTL_WL_CNT:%d,ret:%d",
				wl_fill_start,wl_fill_count,wl_fill_start+wl_fill_count,FTL_WL_CNT,ret);

				if(wl_fill_count == 0)
					ftl_apl_trace(LOG_ALW, 0xb00d, "skip fill dummy to last wl");
				else
				#endif
					ftl_fill_dummy_layer(p2l_blank_pda, wl_fill_count*FTL_DU_PER_SWL, mFALSE); 
            }
            else
            {
                // open blk fill dummy
                // start to fill dummy from open wl p2l pda
				pda_t pda_tmp =0;
				if(shr_wl_per_p2l>1){ 
					//ftl_apl_trace(LOG_ALW, 0, "blank p2l>>ftl_spor_info->blank_pda 0x%x", ftl_spor_info->blank_pda);
					//ftl_spor_info->blank_pda = blank p2l
					if(ftl_spor_info->p2l_pda == INV_PDA) 
						pda_tmp = ftl_set_blk2pda(0, spb_id); 
					else 
						pda_tmp = ftl_spor_info->p2l_pda; 

                    ftl_scan_blank_pos(pda_tmp, ftl_pda2wl(ftl_spor_info->blank_pda) + 1, true);
					//ftl_spor_info->blank_pda = blank wl
				} 

#if (PLP_SUPPORT == 0) //nonplp               
                u16 wl_fill_first  = (p2l_grp_idx == 0) ? 0: ((p2l_grp_idx - 1)*shr_wl_per_p2l);
#else
                u16 wl_fill_first  = (ftl_pda2wl(ftl_spor_info->blank_pda) == 0) ? 0 : (ftl_pda2wl(ftl_spor_info->blank_pda) - 1);
#endif
				u16 wl_fill_start  = 0,wl_blank_true = 0;

				bool sts = false;
				wl_fill_start = wl_fill_first;
				//----------------die sch fill dummy----------
				do{
					ftl_scan_wl_blank_pos(spb_id,wl_fill_start);
					sts = ftl_fill_dummy_for_die_sch(spb_id, wl_fill_start);
					wl_fill_start++;
					if(wl_fill_start == FTL_WL_CNT)
					{
						break;
					}
				}while(sts == true );
				//----------------die sch fill dummy---------- 

				wl_blank_true = wl_fill_start;
				ftl_apl_trace(LOG_ALW, 0x4a05, "wl_blank:%d,fill next wl:%d , blank_p2l_wl:%d,wl_fill_first %d",wl_blank_true-1,wl_blank_true,blank_wl_idx,wl_fill_first);  
				pda_t pda_continue = 0;
				pda_continue = ftl_set_blk2pda(pda_continue, spb_id); 
				pda_continue = ftl_set_pg2pda(pda_continue, (wl_blank_true*FTL_PG_IN_WL_CNT));
				ftl_fill_dummy_layer(pda_continue, FTL_DU_PER_SWL*(FTL_WL_PER_LAYER-1), mFALSE);//1wl die sch  3wl spor dummy
				#if 0 
				// spor fill one spor dummy p2l for gc aux build------no longer need  
				pda_t p2l_pda, pgsn_pda; 
				if((wl_blank_true > blank_wl_idx) || (wl_blank_true == FTL_WL_CNT)) //last wl
				{
					ftl_apl_trace(LOG_ALW, 0x8347, "p2l  fill finish");
				}
				else
				{				
					pda_tmp = 0;
					pda_tmp = ftl_set_blk2pda(pda_tmp, spb_id);
					pda_tmp = ftl_set_pg2pda(pda_tmp, ((wl_blank_true)*FTL_PG_IN_WL_CNT));
					ftl_p2l_position_detect(p2l_grp_idx, spb_id, nsid, &p2l_pda, &pgsn_pda);

					ftl_apl_trace(LOG_ALW, 0xb0c9, "pda_tmp:0x%x wl_blank_true:%d blank p2l wl:%d ",pda_tmp,wl_blank_true,ftl_pda2wl(p2l_pda));

					ftl_fill_dummy_layer(pda_tmp, ((ftl_pda2wl(p2l_pda) - wl_blank_true + 1) * FTL_DU_PER_SWL), mFALSE); 
				} 
				#endif 
            }
        }
        else
        {
            // ===== already have some dummy in blk ======
        #if 0//LJ1 dummy type        
			pda_t pda_scan_start = 0;
			u32 dummy_du_cnt;
            ftl_apl_trace(LOG_ALW, 0xe31b, "Continue fill dummy");
            ftl_scan_blank_pos(ftl_spor_info->last_dummy_pda, FTL_WL_CNT,false);
			pda_scan_start = ftl_spor_info->blank_pda;
            dummy_du_cnt = (pda_scan_start-ftl_spor_info->p2l_pda)&FTL_PDA_DU_MSK;
            dummy_du_cnt = (dummy_du_cnt/FTL_DU_PER_WL)*FTL_DU_PER_WL;

            ftl_fill_dummy_layer(pda_scan_start, dummy_du_cnt, mFALSE);
        #else//PJ1 fix for second spor incomplete wl
			ftl_apl_trace(LOG_ALW, 0x76d5, "Continue fill dummy");
			bool sts = false;
			u16 wl_fill_start = ftl_pda2wl(ftl_spor_info->last_dummy_pda);
			u16 wl_fill_temp = wl_fill_start;
			//wl_fill_start = wl_fill_first;
			do{
				ftl_scan_wl_blank_pos(spb_id,wl_fill_start);
				sts = ftl_fill_dummy_for_die_sch(spb_id, wl_fill_start);
				wl_fill_start++;
				if(wl_fill_start == FTL_WL_CNT)
				{
					break;
				}
			}while(sts == true );
			u16 wl_fill_cnt = wl_fill_start - wl_fill_temp;
			u16 dummy_du_cnt = (wl_fill_cnt >= FTL_WL_PER_LAYER)? 0:(FTL_WL_PER_LAYER-wl_fill_cnt)*FTL_DU_PER_SWL;
			pda_t pda_fill_start = 0;
			pda_fill_start = ftl_set_blk2pda(pda_fill_start, spb_id);
        	pda_fill_start = ftl_set_pg2pda(pda_fill_start, (wl_fill_start*FTL_PG_IN_WL_CNT));
        	if(dummy_du_cnt != 0)
				ftl_fill_dummy_layer(pda_fill_start, dummy_du_cnt, mFALSE);
        #endif

        }

READ_P2L:

        memset(ftl_p2l_fail_grp_flag, 0, (sizeof(u8)*shr_p2l_grp_cnt));
        ftl_p2l_fail_cnt = 0;
        ftl_read_p2l_from_nand_with_list(spb_id, lda_dtag_start, pgsn_dtag_start, pda_dtag_start, nsid, grp_threshold);

        if((ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_USER) || //open blk
           (ftl_build_tbl_type[spb_id] == FTL_SN_TYPE_OPEN_GC))
        {
            // ===== open block aux build =====  
#if (PLP_SUPPORT == 0)
            first_aux_grp = p2l_grp_idx;
            if(p2l_grp_idx > 0)
            {
                first_aux_grp--;
                ftl_spor_p2l_fail_chk(first_aux_grp);//nonplp
            }
			ftl_set_non_plp_blk(true, spb_id,  first_aux_grp, grp_threshold);                                           
#endif          
            grp_threshold = (ftl_spor_p2l_fail_chk(p2l_grp_idx) == mTRUE) ? (grp_threshold+1) : grp_threshold;
            grp_threshold = (ftl_spor_p2l_fail_chk(p2l_grp_idx+1) == mTRUE) ? (grp_threshold+1) : grp_threshold;
            grp_threshold = min(grp_threshold,shr_p2l_grp_cnt);
            //wl_threshold = (p2l_wl_idx+2);

            ftl_wl_p2l_table_rebuild(spb_id, lda_dtag_start, pgsn_dtag_start, pda_dtag_start, nsid);
#if (PLP_SUPPORT == 0)
            ftl_apl_trace(LOG_ALW, 0x9e30, "open blk aux build done,p2l_grp-1:%d,grp_threshold:%d,blk type:%d",p2l_grp_idx-1,grp_threshold,ftl_build_tbl_type[spb_id]);
#else
            ftl_apl_trace(LOG_ALW, 0xcae2, "open blk aux build done,p2l_grp:%d,grp_threshold:%d,blk type:%d",p2l_grp_idx,grp_threshold,ftl_build_tbl_type[spb_id]);
#endif            
            //ftl_apl_trace(LOG_ALW, 0, "PLP fail, open block detected! p2l grp_threshold : %d", grp_threshold);
			/*
			u32    *vc, cnt;
			dtag_t dtag;
    		dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
			if(vc[spb_id] != 0)
			{
				extern bool is_epm_vac_err;//for smart
				is_epm_vac_err = true;
				ftl_apl_trace(LOG_ALW, 0, "Aux mode vac != 0 into RO");
			}
			ftl_l2p_put_vcnt_buf(dtag, cnt, false);
            ftl_spor_info->p2l_data_error = mTRUE;
            */
        }
        #if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
		else if((grp_threshold != shr_p2l_grp_cnt) && plp_fcore_flush_p2l)//plp blk
		{
			//read last p2l 

			bool ret = ftl_plp_read_last_p2l(spb_id, lda_dtag_start, pgsn_dtag_start, pda_dtag_start, nsid, p2l_grp_idx);
			if(ret)
			{
				//error
				grp_threshold = (ftl_spor_p2l_fail_chk(p2l_grp_idx) == mTRUE) ? (grp_threshold+1) : grp_threshold;
				ftl_wl_p2l_table_rebuild(spb_id, lda_dtag_start, pgsn_dtag_start, pda_dtag_start, nsid);		   
				ftl_apl_trace(LOG_ALW, 0x192f, "plp flush p2l error, blk aux build ,p2l_grp:%d,blk type:%d",grp_threshold,ftl_build_tbl_type[spb_id]);

			}
			else
			{
				//success update blank_grp buffer
				spb_set_flag(spb_id, SPB_DESC_F_PLP_LAST_P2L);
				grp_threshold++;
				//ftl_apl_trace(LOG_ALW, 0, "[pre]grp_threshold:%d,plp handle group:%d",grp_threshold,blank_grp_idx-1);
			}
		}
        #endif

        switch(ftl_build_tbl_type[spb_id])
        {
            case FTL_SN_TYPE_PLP_USER:
            {
                ftl_spor_info->plpUSREndGrp = grp_threshold;
                break;
            }

            case FTL_SN_TYPE_OPEN_USER:
            {
                ftl_spor_info->openUSREndGrp = grp_threshold;
                break;
            }

            case FTL_SN_TYPE_PLP_GC:
            {
                ftl_spor_info->plpGCEndGrp = grp_threshold;
                break;
            }

            case FTL_SN_TYPE_OPEN_GC:
            {
                ftl_spor_info->openGCEndGrp = grp_threshold;
                break;
            }

            default:
#if (SPOR_ASSERT == mENABLE)
                panic("impossible build tbl type!\n");
#else
                ftl_apl_trace(LOG_PANIC, 0x3a05, "impossible build tbl type!");
#endif
                break;
        }
    } // end if (ftl_build_tbl_type[spb_id])

    #if (SPOR_AUX_MODE_DBG == mENABLE) // for aux build test
    {
        u16 grp;
        for(grp = 0; grp<shr_p2l_grp_cnt; grp++)
        {
            ftl_apl_trace(LOG_ALW, 0xd362, "Current grp : 0x%x", grp);
            ftl_page_aux_build_p2l(spb_id, grp, ftl_dbg_p2l_info.lda_dtag_start, ftl_dbg_p2l_info.pgsn_dtag_start, ftl_dbg_p2l_info.pda_dtag_start);
            ftl_aux_pgsn_buffer_sorting(ftl_dbg_p2l_info.pgsn_dtag_start, ftl_dbg_p2l_info.pda_dtag_start, grp);
            ftl_p2l_aux_comapare(spb_id, grp, 1);
        }

        //ftl_p2l_aux_comapare(usr_blk, 1, 1);
        //ftl_print_p2l_info(usr_blk, nsid, 1, 1);
    }
    #endif
}
#if (SPOR_CHECK_TAG == mENABLE)
fast_code bool Chk_preh_blk_build_s(u32 adj_pgsn_idx, u16 adj_die, u16 chk_preh_blk, u16 gc_blk)
{
    u64 init_time_start = get_tsc_64();
	bool tmp_chk = false;
	bool need_chk_again = false;
	u8 die_cnt = 0;
    dtag_t dtag;
	u32 *bmp = NULL;
    u32 bmp_hit_cnt = 0;
    lda_t lda;
	dtag = dtag_get(DTAG_T_SRAM, (void*)&bmp);
    memset(bmp, 0, DTAG_SZE);
    u32 bmp_shft = 13;
    while((_max_capacity>>bmp_shft) > DTAG_SZE){
        bmp_shft ++;
    }
    ftl_apl_trace(LOG_ALW, 0xed3e, "_max_capacity %d, _max_capacity>>bmp_shft %u, bmp_shft %u",
        _max_capacity, (_max_capacity>>bmp_shft), bmp_shft);

    u8 vaild_lda_count = 0;//skip dummy lda
	//chk host
	u8  i, j, pn_pair = 0;
	pda_t chk_pda = 0;
	//gc
#ifdef SKIP_MODE
	u32 gc_pn_ptr = 0;
#endif
	u32 gc_adj_pgsn_idx = adj_pgsn_idx;

	for(die_cnt=0; die_cnt<3; die_cnt++)
	{
		if(adj_die == FTL_DIE_CNT)
		{
			adj_die  = 0;
		}

		if(ftl_gc_p2l_info.pda_buf[adj_pgsn_idx] == INV_U32)
        {
            adj_pgsn_idx = ((adj_pgsn_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY;
			adj_die = 0;
		}

		ftl_apl_trace(LOG_ALW, 0x915d, "chk gc die %d",adj_die);
		u8 die_page_cnt = 0;
		u8 pln_idx;
#ifdef SKIP_MODE
		gc_pn_ptr = adj_die << FTL_PN_SHIFT;
		pn_pair = ftl_good_blk_in_pn_detect(gc_blk, gc_pn_ptr);
#else
		pn_pair = BLOCK_NO_BAD;
#endif
		for(pln_idx=0;pln_idx<FTL_PN_CNT;pln_idx++)
		{
			if(BIT_CHECK(pn_pair, pln_idx) == 0) 
			{ 
				die_page_cnt += 3;
			} 
		}

		for(j=0; j<die_page_cnt; j++)
		{
			chk_pda = ftl_gc_p2l_info.pda_buf[adj_pgsn_idx];
			for(i=0; i<DU_CNT_PER_PAGE; i++)
			{
			    if(ftl_gc_p2l_info.lda_buf[(chk_pda&FTL_PDA_DU_MSK)] <= _max_capacity){
                    lda = ftl_gc_p2l_info.lda_buf[(chk_pda&FTL_PDA_DU_MSK)];
    				pbt_chk_lda_buffer[vaild_lda_count++] = lda;
                    set_bit(lda>>bmp_shft, bmp);
    				ftl_apl_trace(LOG_ALW, 0xbc74, "[SPOR]chk_lda:0x%x,pda:0x%x,wl:%d", lda,chk_pda,ftl_pda2wl(chk_pda));
                }
                chk_pda ++;
			}
			adj_pgsn_idx++;
		}
		adj_die++;
		//ftl_apl_trace(LOG_ALW, 0, "vaild_lda_count:%d  dummy count:%d",vaild_lda_count,24-vaild_lda_count);

	}
	u32 chk_pgsn_grp_idx = 0;
	u32 chk_pgsn_idx = 0;
	u32 chk_pgsn_idx_thr = 0;
	u16 die_id = 0;
#ifdef SKIP_MODE
	u32 pn_ptr = 0;
#endif
	u8  pn_idx;
	while(chk_pgsn_idx < PGSN_FULL_TABLE_ENTRY)
	{
		if(chk_pgsn_idx >= chk_pgsn_idx_thr)
		{
			chk_pgsn_grp_idx = chk_pgsn_idx;
#ifdef SKIP_MODE
			pn_ptr = die_id << FTL_PN_SHIFT;
	        pn_pair = ftl_good_blk_in_pn_detect(chk_preh_blk, pn_ptr);
#else
	        pn_pair = BLOCK_NO_BAD;
#endif
			if(pn_pair == BLOCK_ALL_BAD)
			{
				die_id++;
		        continue;
			}

			chk_pgsn_idx_thr = chk_pgsn_idx;
			for(pn_idx=0;pn_idx<FTL_PN_CNT;pn_idx++)
			{
				if(BIT_CHECK(pn_pair, pn_idx) == 0) 
				{ 
					chk_pgsn_idx_thr += 3;
				} 
			}	

			die_id++;
	        if(die_id == FTL_DIE_CNT)
	        {
	        	die_id  = 0;
	        }
		}

		chk_pda = ftl_host_p2l_info.pda_buf[chk_pgsn_idx];
		for(i=0; i<DU_CNT_PER_PAGE; i++)
		{
            lda = ftl_host_p2l_info.lda_buf[(chk_pda&FTL_PDA_DU_MSK)];
		    if((lda <= _max_capacity) && test_bit(lda>>bmp_shft, bmp)){
                bmp_hit_cnt ++;
                for(j=0; j<vaild_lda_count; j++)
                {
                    if(pbt_chk_lda_buffer[j] == lda)
                    {
                        ftl_apl_trace(LOG_ALW, 0x9814, "[FTL]spe_case : host_pgsn_idx %d  host_pgsn_grp_idx %d", chk_pgsn_idx, chk_pgsn_grp_idx);
                        ftl_apl_trace(LOG_ALW, 0xe7c1, "host_pgsn 0x%x-%x adjust_host_pgsn 0x%x-%x",(u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx] >> 32), (u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx]), (u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_grp_idx] >> 32), (u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_grp_idx]));
                        ftl_apl_trace(LOG_ALW, 0x248a, "Hit chk lda %d : 0x%x", j, pbt_chk_lda_buffer[j]);
                        adjust_host_page_idx = chk_pgsn_grp_idx;
                        had_host_data = true;
                        need_chk_again = true;
                        goto SPE_CASE_END;
                    }
                }
            }
            chk_pda++;
        }

		if((ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx] > ftl_gc_p2l_info.pgsn_buf[gc_adj_pgsn_idx]) && (ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx] != INV_U64) && (tmp_chk == false))
		{
			//if use , need consider if blist page 0 use to pbt seg!!!!!!
			ftl_apl_trace(LOG_ALW, 0xd8eb, "host_pgsn_idx %d  host_pgsn_grp_idx %d", chk_pgsn_idx, chk_pgsn_grp_idx);
			ftl_apl_trace(LOG_ALW, 0xdab6, "host_pgsn 0x%x-%x tmp_host_pgsn 0x%x-%x",(u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx] >> 32), (u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx]), (u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_grp_idx] >> 32), (u32)(ftl_host_p2l_info.pgsn_buf[chk_pgsn_grp_idx]));
			tmp_host_page_idx = chk_pgsn_grp_idx;
			had_host_data = true;
			tmp_chk = true;
			//need_chk_again = true;
		}

		chk_pgsn_idx++;

		if(ftl_host_p2l_info.pda_buf[chk_pgsn_idx] == INV_U32)
        {
            chk_pgsn_idx = ((chk_pgsn_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY;
			die_id = 0;
		}
	}
SPE_CASE_END:

    dtag_put(DTAG_T_SRAM, dtag);
	ftl_apl_trace(LOG_ALW, 0x3c83, "bmp_hit_cnt %u, time cost : %d us", bmp_hit_cnt, time_elapsed_in_us(init_time_start));
	return need_chk_again;
}

fast_code bool Chk_gc_blk_build_s(u64 pgsn_thr, u16 chk_blk, u16 *adj_die)
{
	bool need_chk_again = false;
	u32 chk_pgsn_grp_idx = 0;
	u32 chk_pgsn_idx = 0;  
	u32 chk_pgsn_idx_thr = 0;
	u8  pn_pair = 0;
#ifdef SKIP_MODE
	u32 pn_ptr = 0;
#endif
	u16 die_id = 0;
	u8  pn_idx;
	while(chk_pgsn_idx < PGSN_FULL_TABLE_ENTRY)
	{
		if(chk_pgsn_idx >= chk_pgsn_idx_thr)
		{
			chk_pgsn_grp_idx = chk_pgsn_idx;
			//pn_ptr = die_id * FTL_PN_CNT;
			*adj_die = die_id;
#ifdef SKIP_MODE
			pn_ptr = die_id << FTL_PN_SHIFT;
	        pn_pair = ftl_good_blk_in_pn_detect(chk_blk, pn_ptr);
#else
			pn_pair = BLOCK_NO_BAD;
#endif
			if(pn_pair == BLOCK_ALL_BAD)
			{
				die_id++;
		        continue;
			}

			chk_pgsn_idx_thr = chk_pgsn_idx;
			for(pn_idx=0;pn_idx<FTL_PN_CNT;pn_idx++)
			{
				if(BIT_CHECK(pn_pair, pn_idx) == 0) 
				{ 
					chk_pgsn_idx_thr += 3;
				} 
			}	

			die_id++;

			if(die_id == FTL_DIE_CNT)
			{
				die_id = 0;
			}
		}

		if((ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] > pgsn_thr) && ((ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] != INV_U64) && (ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] != 0xFFFFFFFFFFFF)))
		{
			/*
			u8 die_page_cnt;
			if(pn_pair == BLOCK_NO_BAD){
				die_page_cnt = 6;
			}
			else if((pn_pair == BLOCK_P0_BAD) || (pn_pair == BLOCK_P1_BAD)){
				die_page_cnt = 3;
			}
			else{
				die_page_cnt = 0;
				sys_assert(0);
			}
			*/
			adjust_gc_page_idx = chk_pgsn_grp_idx;
			ftl_apl_trace(LOG_ALW, 0xe9b5, "[SPOR]pda:0x%x wl:%d  adj_die %d gc_pgsn_idx %d  gc_pgsn_grp_idx %d ",
				ftl_gc_p2l_info.pda_buf[chk_pgsn_idx],ftl_pda2wl(ftl_gc_p2l_info.pda_buf[chk_pgsn_idx]),*adj_die,chk_pgsn_idx,chk_pgsn_grp_idx);
			ftl_apl_trace(LOG_ALW, 0x06b7, "[SPOR]gc_pgsn 0x%x%x aj_gc_pgsn 0x%x%x PGSN_GRP_TABLE_ENTRY:%d",
				(u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] >> 32), (u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx]), (u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_grp_idx] >> 32), (u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_grp_idx]),PGSN_GRP_TABLE_ENTRY);
			/*
			for(j=0; j<die_page_cnt; j++)
			{
				chk_pda = ftl_gc_p2l_info.pda_buf[chk_pgsn_grp_idx];
				for(i=0; i<DU_CNT_PER_PAGE; i++)
				{
					pbt_chk_lda_buffer[j*DU_CNT_PER_PAGE + i] = ftl_gc_p2l_info.lda_buf[(chk_pda&FTL_PDA_DU_MSK)];
					chk_pda++;
					ftl_apl_trace(LOG_ALW, 0, "[SPOR]chk_lda:0x%x", pbt_chk_lda_buffer[j*DU_CNT_PER_PAGE + i]);
				}
				chk_pgsn_grp_idx++;
			}
			*/
			need_chk_again = true;
			had_gc_data = true;
			goto SPE_GC_CASE;
		}
		chk_pgsn_idx++;
		if(ftl_gc_p2l_info.pda_buf[chk_pgsn_idx] == INV_U32)
        {
            chk_pgsn_idx = ((chk_pgsn_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY;
			die_id = 0;
		}
	}
SPE_GC_CASE:
	return need_chk_again;
}
#endif

/*
 about plane raid , it has two situation:
 1. spor read p2l , this can read invaild lda(in lda dtag) , then in func ftl_p2l_skip_bad_update() can check this
 2. spor aux build, this will skip read raid plane ,so func Chk_gc_blk_build_s need skip this plane

 Extreme situation raid die just has raid plane (3 defect plane and 1 raid plane)
 Now should consider if we also need skip raid plane 

 1.spor read p2l , raid plane has invaild lda , so no need skip raid plane
 2.spor aux build, when aux build , spor will skip read this plane , this situation cannnot occur

*/
fast_code bool Chk_gc_blk_build_s(u64 pgsn_thr, u16 chk_blk, u16 *adj_die)
{
	bool need_chk_again = false;
	u32 chk_pgsn_die_idx = 0;  //die fiset page sn idx
	u32 chk_pgsn_idx = 0;      //hit gc page sn > host page sn idx
	u16 pre_page = 0;

	u16 die_id = 0;
	u16 group_id = 0;
	pda_t hit_pda = 0;
	u16 wl_id  = 0;
	pda_t pda_die_start = 0;
	u16 page_id = 0;
	u16 plane_id = 0;

	u8  pn_pair = 0;
#ifdef SKIP_MODE
	u32 pn_ptr = 0;
#endif
		/*-----------------skip raid plane---------------*/
	bool raid_support = fcns_raid_enabled(INT_NS_ID);
	u16 parity_die_idx = FTL_DIE_CNT;
	u16 parity_die_pln_idx = FTL_PN_CNT;
	bool skip_raid = true;	//if parity die only has plane raid->3 plane is defect , 1 plane raid
	if(raid_support){
		parity_die_idx = FTL_DIE_CNT - 1;
		while(parity_die_idx >= 0)
		{
#ifdef SKIP_MODE
		pn_ptr = parity_die_idx << FTL_PN_SHIFT;
		pn_pair = ftl_good_blk_in_pn_detect(chk_blk, pn_ptr);
#else
		pn_pair = BLOCK_NO_BAD;
#endif

			if (pn_pair == BLOCK_ALL_BAD) {
				parity_die_idx--;
			} else {
				bool ret = false;
				for (u8 pl = 0 ; pl < shr_nand_info.geo.nr_planes; pl++){
					if (((pn_pair >> pl)&1)==0){ //good plane
						parity_die_pln_idx = pl;
						ret = true;
						skip_raid = (shr_nand_info.geo.nr_planes - pop32(pn_pair) == 1)? false : true;
							break;
					}
				}
				sys_assert(ret);
				break;
			}
		}
	}
	/*-----------------skip raid plane---------------*/


	while(chk_pgsn_idx < PGSN_FULL_TABLE_ENTRY)
	{
		if((ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] > pgsn_thr) && ((ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] != INV_U64) && (ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] != 0xFFFFFFFFFFFF)))
		{

			//------------------find cur die finst page idx---------------------

			hit_pda =  ftl_gc_p2l_info.pda_buf[chk_pgsn_idx];
			die_id  =  ftl_pda2die(hit_pda);
			wl_id   =  ftl_pda2wl(hit_pda);
			page_id = wl_id * shr_nand_info.bit_per_cell; // 

			/*--------construct first page pda to check----------*/
			pda_die_start = ftl_set_blk2pda(pda_die_start, chk_blk);
			pda_die_start = ftl_set_pg2pda(pda_die_start, page_id);
			pda_die_start = ftl_set_die2pda(pda_die_start, die_id);


#ifdef SKIP_MODE
			pn_ptr = die_id << FTL_PN_SHIFT;
			pn_pair = ftl_good_blk_in_pn_detect(chk_blk, pn_ptr);
#else
			pn_pair = BLOCK_NO_BAD;
#endif

			for(plane_id=0;plane_id<FTL_PN_CNT;plane_id++)
			{
				if(BIT_CHECK(pn_pair, plane_id) == 0) 
				{ 
					if(skip_raid && (die_id == parity_die_idx) && (plane_id == parity_die_pln_idx))
					{
						//hit raid plane , skip it
						ftl_apl_trace(LOG_ALW, 0x0d87, "[SPOR]gc hit raid plane:%d die:%d check next",parity_die_pln_idx,parity_die_idx);
					}
					else
					{
						pda_die_start = ftl_set_pn2pda(pda_die_start,plane_id);
						break;
					}

				} 
			}
			/*--------construct first page pda to check----------*/

			chk_pgsn_die_idx = chk_pgsn_idx;
			if(hit_pda < pda_die_start) // read p2l raid, and first page(raid plane) sn > host sn
			{
				ftl_apl_trace(LOG_ALW, 0x549f, "[SPOR]hit_pda:0x%x pl:%d < pda_die_start:0x%x pl:%d",
					hit_pda,ftl_pda2plane(hit_pda),pda_die_start,ftl_pda2plane(pda_die_start));
			}
			else
			{
				if(chk_pgsn_die_idx == 0)
					sys_assert(ftl_gc_p2l_info.pda_buf[chk_pgsn_die_idx] == pda_die_start);// debug 
				for(pre_page = 0; pre_page < shr_nand_info.geo.nr_planes*shr_nand_info.bit_per_cell;pre_page++)
				{
					if(ftl_gc_p2l_info.pda_buf[chk_pgsn_die_idx] == pda_die_start)
					{
						break;
					}
					chk_pgsn_die_idx--;
					if(chk_pgsn_die_idx == 0 )
							break;
				}
				sys_assert(ftl_gc_p2l_info.pda_buf[chk_pgsn_die_idx] == pda_die_start);// debug 

			}


			adjust_gc_page_idx = chk_pgsn_die_idx;
			*adj_die = die_id; 
			//------------------find cur die finst page idx---------------------

			//-----------------------log------------------------
			ftl_apl_trace(LOG_ALW, 0x4888, "[SPOR]hit_pda:0x%x die_pda:0x%x pre_page:%d die_id:%d wl:%d",
									hit_pda,ftl_gc_p2l_info.pda_buf[chk_pgsn_die_idx],pre_page,die_id,wl_id);
			ftl_apl_trace(LOG_ALW, 0x27db, "[SPOR]chk_pgsn_idx:%d chk_pgsn_die_idx:%d group_id:%d plane_id:%d",
									chk_pgsn_idx,chk_pgsn_die_idx,group_id,plane_id);
			ftl_apl_trace(LOG_ALW, 0x1fe7, "[SPOR]gc_pgsn 0x%x%x aj_gc_pgsn 0x%x%x PGSN_GRP_TABLE_ENTRY:%d",
				(u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx] >> 32), (u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_idx]),
				(u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_die_idx] >> 32), (u32)(ftl_gc_p2l_info.pgsn_buf[chk_pgsn_die_idx]),PGSN_GRP_TABLE_ENTRY);

			//-----------------------log------------------------
			need_chk_again = true;
			had_gc_data = true;
			goto SPE_GC_CASE;
		}
		chk_pgsn_idx++;
		if(ftl_gc_p2l_info.pda_buf[chk_pgsn_idx] == INV_U32)
        {
            chk_pgsn_idx = ((chk_pgsn_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY;
			group_id++;
		}
	}
SPE_GC_CASE:
	return need_chk_again;
}




init_code u64 Chk_h_blk_build_s(u16 chk_blk)
{
	u64 h_last_pgsn = INV_U64;
	//u32 chk_pgsn_grp_idx = 0;
	u32 chk_pgsn_idx = 0;
	u32 chk_pgsn_idx_thr = 0;
	u8  pn_pair = 0;
#ifdef SKIP_MODE
	u32 pn_ptr = 0;
#endif
	u16 die_id = 0;
	u8 pn_idx;
	while(chk_pgsn_idx < PGSN_FULL_TABLE_ENTRY)
	{
		if(chk_pgsn_idx >= chk_pgsn_idx_thr)
		{
#ifdef SKIP_MODE
			pn_ptr = die_id * FTL_PN_CNT;
	        pn_pair = ftl_good_blk_in_pn_detect(chk_blk, pn_ptr);
#else
			pn_pair = BLOCK_NO_BAD;
#endif
			if(pn_pair == BLOCK_ALL_BAD)
			{
				die_id++;
		        continue;
			}

			chk_pgsn_idx_thr = chk_pgsn_idx;
			for(pn_idx=0;pn_idx<FTL_PN_CNT;pn_idx++)
			{
				if(BIT_CHECK(pn_pair, pn_idx) == 0) 
				{ 
					chk_pgsn_idx_thr += 3;
				} 
			}	
			die_id++;
			if(die_id == FTL_DIE_CNT)
			{
				die_id = 0;
			}
		}
		h_last_pgsn = ftl_host_p2l_info.pgsn_buf[chk_pgsn_idx];
		chk_pgsn_idx++;

		if(ftl_host_p2l_info.pda_buf[chk_pgsn_idx] == INV_U32)
        {
            chk_pgsn_idx = ((chk_pgsn_idx + PGSN_GRP_TABLE_ENTRY)/PGSN_GRP_TABLE_ENTRY)*PGSN_GRP_TABLE_ENTRY;
			die_id = 0;
		}
	}

	return h_last_pgsn;
}

init_code void PBT_special_case(u16 *gc_blk, u16 *usr_blk, u32 spor_last_rec_blk_sn)
{
	bool sts = false;
	u16 adj_die = 0;
	u16 adjust_gc_blk = INV_U16;
	u16 adjust_host_blk = INV_U16;
	u64 host_start_pgsn = ftl_host_p2l_info.pgsn_buf[1];//page sn 0 is blist pbt seg
	ftl_apl_trace(LOG_ALW, 0x2997, "[SPOR]h_s_pgsn 0x%x%x qbt_sn:0x%x",(u32)(host_start_pgsn >> 32), (u32)host_start_pgsn ,spor_last_rec_blk_sn);

	//step 1 : chk gc start point
	sts = Chk_gc_blk_build_s(host_start_pgsn, *gc_blk, &adj_die);

	//step 2 : if need to chk pre host with gc blk 3 die
			//Don't chk here , when open gc build recheck l2p --- Jay
	if(sts){
		adjust_host_blk = ftl_spor_blk_node[*usr_blk].pre_blk;

		if((adjust_host_blk != INV_U16) && (ftl_spor_get_blk_sn(adjust_host_blk) < spor_last_rec_blk_sn) && (spor_last_rec_blk_sn != INV_U32))
	    {
	 		ftl_apl_trace(LOG_ALW, 0x5e8a, "pre_host_blk_sn : 0x%x < rec_blk_sn : 0x%x",ftl_spor_get_blk_sn(adjust_host_blk),spor_last_rec_blk_sn);
			sts = true;
			adjust_host_page_idx = 0;
	    }
		else if(adjust_host_blk != INV_U16)
		{
			ftl_apl_trace(LOG_ALW, 0x624d, "pre_host_blk : %d, sn : 0x%x", adjust_host_blk, ftl_spor_get_blk_sn(adjust_host_blk));
			pre_host_blk = adjust_host_blk; 
			sts = false; 
			/* 
			ftl_extract_p2l_table(adjust_host_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, 1); 
			sts = Chk_preh_blk_build_s(adjust_gc_page_idx, adj_die, adjust_host_blk, *gc_blk); 
			 
			if(sts == true) 
			{ 
				*usr_blk = adjust_host_blk; 
			} 
			*/ 
		}
		else // only one host, pre host == INV_U16
		{
			sts = true;
		}
	}

	//step 3 : if step 1 start point not in this gc blk, search next gc blk(this gc blk is too old)
	if(!had_gc_data)
	{
		//adjust_gc_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_GC, mFALSE);
		adjust_gc_blk = ftl_spor_blk_node[*gc_blk].nxt_blk;
		if(adjust_gc_blk != INV_U16)
	    {
	    	memset(ftl_gc_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
		    memset(ftl_gc_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
		    memset(ftl_gc_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);
	        ftl_apl_trace(LOG_ALW, 0x4054, "skip chk_gc_blk : %d, sn : 0x%x", adjust_gc_blk, ftl_spor_get_blk_sn(adjust_gc_blk));
	        ftl_extract_p2l_table(adjust_gc_blk, ftl_gc_p2l_info.lda_dtag_start, ftl_gc_p2l_info.pgsn_dtag_start, ftl_gc_p2l_info.pda_dtag_start, 1);
	    }
		*gc_blk = adjust_gc_blk;
	}

	//step 4 : if step 2 start point not in this host blk, search next host blk or update the last wl
	if(sts == false)
	{
		if((had_host_data == true) && (tmp_host_page_idx != INV_U32))  // no use
		{
			adjust_host_page_idx = tmp_host_page_idx;
			*usr_blk = adjust_host_blk;
			ftl_apl_trace(LOG_ALW, 0x3ead, "adj_host_page_idx 0x%x",adjust_host_page_idx);
		}
		//else if(ftl_spor_get_blk_sn(adjust_host_blk) < gFtlMgr.PrevTableSN)
		else if(pbt_new_build == false) //pbt_new_build flag make sure current usr blk sn > pbt sn
		{
			adjust_host_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_USER, mTRUE);
			if((adjust_host_blk != INV_U16) && (ftl_spor_get_blk_sn(adjust_host_blk) < spor_last_rec_blk_sn) && (spor_last_rec_blk_sn != INV_U32))
	    	{
	 	  		ftl_apl_trace(LOG_ALW, 0xe9c3, "pre_host_blk_sn : 0x%x < rec_blk_sn : 0x%x",ftl_spor_get_blk_sn(adjust_host_blk),spor_last_rec_blk_sn);
		  		adjust_host_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_USER, mFALSE);
	    	}
			if(adjust_host_blk != INV_U16)
	    	{
		    	memset(ftl_host_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
			    memset(ftl_host_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
			    memset(ftl_host_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);
		        ftl_apl_trace(LOG_ALW, 0x33ed, "chk_h_blk : %d, sn : 0x%x", adjust_host_blk, ftl_spor_get_blk_sn(adjust_host_blk));
		        ftl_extract_p2l_table(adjust_host_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, 1);
		    }
			if(ftl_build_tbl_type[adjust_host_blk] == FTL_SN_TYPE_USER)
			{
				if(ftl_spor_get_blk_sn(adjust_host_blk) < gFtlMgr.PrevTableSN)
				{
					u64 h_last_pgsn = INV_U64;
					h_last_pgsn = Chk_h_blk_build_s(adjust_host_blk);
					//page sn 0 is blist pbt seg
					if((ftl_gc_p2l_info.pgsn_buf[1] > h_last_pgsn) && (h_last_pgsn != INV_U64))
					{
						//(this usr blk is too old)
						adjust_host_page_idx = 0;
						ftl_apl_trace(LOG_ALW, 0x6b7e, "pre_host_blk_sn < PrevSN : 0x%x", gFtlMgr.PrevTableSN);

						adjust_host_blk = ftl_spor_blk_node[adjust_host_blk].nxt_blk;
						if(adjust_host_blk != INV_U16)
						{
							memset(ftl_host_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
			    			memset(ftl_host_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
			    			memset(ftl_host_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);
		        			ftl_apl_trace(LOG_ALW, 0x98c2, "chk_host_blk : %d, sn : 0x%x", adjust_host_blk, ftl_spor_get_blk_sn(adjust_host_blk));
		        			ftl_extract_p2l_table(adjust_host_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, 1);
						}
					}
				}
		    }
			*usr_blk = adjust_host_blk;
		}
	}
#if (SPOR_TRIM_DATA == mENABLE) 
	//step 5 : chk  pre_host_blk need build trim info!!!
	if(pre_host_blk != INV_U16 )
	{
#if(PLP_SUPPORT == 1)
	if(*usr_blk != pre_host_blk  && test_bit(pre_host_blk, TrimBlkBitamp))
#else
	if(*usr_blk != pre_host_blk  && test_bit(pre_host_blk, ftl_spor_trimblkbitmap))
#endif
		{
			if((ftl_spor_get_blk_sn(pre_host_blk) < spor_last_rec_blk_sn) && (spor_last_rec_blk_sn != INV_U32))
			{
				ftl_apl_trace(LOG_ALW, 0x1bcf, "pre_sn:0x%x < qbt_sn:0x%x no need update again!!",
					ftl_spor_get_blk_sn(pre_host_blk), spor_last_rec_blk_sn);
				return;
			}
			else if(ftl_spor_get_blk_sn(pre_host_blk) < gFtlMgr.PrevTableSN)
			{
				SPOR_need_vac_rebuild = true;
				ftl_apl_trace(LOG_ALW, 0x16bd, "[Warning] pre_sn:0x%x < gFtlMgr.PrevTableSN:0x%x",
					ftl_spor_get_blk_sn(pre_host_blk), gFtlMgr.PrevTableSN);
			}
			ftl_apl_trace(LOG_ALW, 0xebec, "usr:%d -> pre_usr:%d , build trim info!!!!",*usr_blk, pre_host_blk);
			*usr_blk = pre_host_blk;
			adjust_host_page_idx = 0;
			memset(ftl_host_p2l_info.lda_buf,  0xFF, LDA_FULL_TABLE_SIZE);
			memset(ftl_host_p2l_info.pda_buf,  0xFF, PDA_FULL_TABLE_SIZE);
			memset(ftl_host_p2l_info.pgsn_buf, 0xFF, PGSN_FULL_TABLE_SIZE);
			ftl_extract_p2l_table(pre_host_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, 1);
		}
	}
#endif

}

init_code bool is_need_l2p_build_vac(void)
{
#if 0
	// force return true, just for debug
	return true;
#else
	u16 panic_flag = false,shuttle_flag = false;
#if (Panic_save_epm && SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    panic_flag = epm_ftl_data->panic_build_vac;

	u32 shuttle_sn = epm_ftl_data->max_shuttle_gc_blk_sn;
	if(shuttle_sn >= gFtlMgr.PrevTableSN)
	{
		shuttle_flag = true;
		ftl_apl_trace(LOG_ALW, 0xc0d2," shuttle blk %d sn 0x%x > PreTableSN 0x%x ",epm_ftl_data->max_shuttle_gc_blk,shuttle_sn,gFtlMgr.PrevTableSN);
	}
#endif

	extern bool SPOR_need_vac_rebuild;
	if(SPOR_need_vac_rebuild)
	{
		extern int l2p_build_vac(int argc, char * argv [ ]);
		l2p_build_vac(1,NULL);
		SPOR_need_vac_rebuild = false;
		flush_to_nand(EVT_VAC_REBUILD);
		return false;//avoid build vac twice
	}

	if (ftl_spor_info->epm_vac_error || ftl_spor_info->glist_eh_not_done || (ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P) || panic_flag || shuttle_flag) {
        ftl_apl_trace(LOG_ALW, 0x6a05, "[FTL] %d %d %d %d %d", 
        	ftl_spor_info->epm_vac_error, ftl_spor_info->glist_eh_not_done, ftl_spor_info->build_l2P_mode,panic_flag,shuttle_flag);
		return true;
	}
	return false;
#endif
}
#if 0
ddr_code void ftl_l2p_build_vac(void)
{
    u64 addr;
    u8  win = 0, old_win = 0;
    lda_t lda;
    //u16   spb_id;
    u32   vc_tbl_dtag_cnt;
    u32   *vc;
    u32   *temp_vc_table;

	u32 cached_cap = _max_capacity;
	dtag_t vac_dtag;


	bool win_change = false;
	pda_t curr_pda;

	u64 start = get_tsc_64(), end;

	ftl_apl_trace(LOG_ALW, 0x73eb, "start l2p build vac:0x%x-%x", start>>32, start&0xFFFFFFFF);

    u64 addr_base = (ddtag2off(shr_l2p_entry_start) | 0x40000000);
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};

    temp_vc_table = sys_malloc(SLOW_DATA, sizeof(u32)*shr_nand_info.geo.nr_blocks);
	sys_assert(temp_vc_table);
	memset(temp_vc_table, 0, (sizeof(u32)*shr_nand_info.geo.nr_blocks));

	// save old win
	old_win = ctrl0.b.cpu3_ddr_window_sel;
	win = 0;

	// don't add any uart log during building vac table
    for(lda = 0; lda < cached_cap; lda++)
    {
		if (lda > 0) {
			addr += 4;
			if (addr >= 0xC0000000) {
				addr -= 0x80000000;
				win++;
				win_change = true;
			}
			if (win_change) {
				ctrl0.b.cpu3_ddr_window_sel = win;
				writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
				win_change = false;
			}
		} else {
			addr = addr_base;
			while (addr >= 0xC0000000) {
				addr -= 0x80000000;
				win++;
			}
			ctrl0.b.cpu3_ddr_window_sel = win;
        	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
		}

		curr_pda = *((u32*)(u32)addr);
        if (curr_pda != INV_U32)
        {
            temp_vc_table[pda2blk(curr_pda)]++;
        }
    }

	// revert old win
	ctrl0.b.cpu3_ddr_window_sel = old_win;
	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));

	#if 0
	// compare l2p vac with epm vac, just used for debug
    ftl_apl_trace(LOG_ALW, 0x0e99, "checking epm vac && rebuild vac");

    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    if (ftl_spor_info->epm_vac_error)
    {
        ftl_apl_trace(LOG_ALW, 0xf84a, "EPM FTL data incorrect!!");
    } else {
		for (spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
		{
			if (epm_ftl_data->epm_vc_table[spb_id] != temp_vc_table[spb_id])
			{
				ftl_apl_trace(LOG_INFO, 0x4055, "VAC mismatch, block = 0x%x, epm vac = %d, L2P vac = %d",
							   spb_id, epm_ftl_data->epm_vc_table[spb_id], temp_vc_table[spb_id]);
			}
		}
	}
	#endif

	#if 0 // added for dbg, Sunny 20210903
    // Compare ftl vc table with L2P vc, just used for uart debug, runtime not open
    dtag_t dtag = ftl_l2p_get_vcnt_buf(&vc_tbl_dtag_cnt, (void **)&vc);
    u16    spb_id;
    for (spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
    {
        if (vc[spb_id] != temp_vc_table[spb_id])
        {
            //fail_flag = mTRUE;
            ftl_apl_trace(LOG_INFO, 0xea14, "VAC mismatch, block = 0x%x, blist vac = %d, L2P vac = %d",
                           spb_id, vc[spb_id], temp_vc_table[spb_id]);
        }
    }

    ftl_l2p_put_vcnt_buf(dtag, vc_tbl_dtag_cnt, false);
	#endif

	#if 1
    // saved vac table into hw
    vac_dtag = ftl_l2p_get_vcnt_buf(&vc_tbl_dtag_cnt, (void **)&vc);
    memcpy(vc, temp_vc_table, shr_nand_info.geo.nr_blocks*sizeof(u32));
    ftl_l2p_put_vcnt_buf(vac_dtag, vc_tbl_dtag_cnt, true);
	#endif


	end = get_tsc_64();
	ftl_apl_trace(LOG_ALW, 0x5d32, "l2p build vac end:0x%x-%x", end>>32, end&0xFFFFFFFF);

    sys_free(SLOW_DATA, temp_vc_table);
    return;
}
#endif

init_code u32 ftl_double_chk_spor_preTableSN(void)
{
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    u32 epm_record_sn = epm_ftl_data->record_PrevTableSN;
    u32 blist_record_sn = gFtlMgr.PrevTableSN;
    u32 ret_sn = blist_record_sn;
    ftl_apl_trace(LOG_ALW, 0x61fc, "Chk PrevTableSN Blist : 0x%x epm : 0x%x ",blist_record_sn ,epm_record_sn);
	if( epm_record_sn == 0 || epm_record_sn == INV_U16)
	{
		ret_sn = blist_record_sn;
	}
	//avoid special gc force PBT , but Blist has not update due to no new open block.
	else if( (epm_record_sn > blist_record_sn) && ( epm_record_sn - blist_record_sn < QBT_BLK_CNT * PBT_RATIO * 3) )
	{
		ret_sn = epm_record_sn;
		//SPOR_need_vac_rebuild = true;
    	ftl_apl_trace(LOG_ALW, 0x1114, "[Warning] use epm PrevTableSN : 0x%x " ,epm_record_sn);
	}
	else
	{
		ret_sn = blist_record_sn;
	}
	
	return ret_sn;
}
/*!
 * @brief ftl spor function
 * dbg useage, to see USR/GC scan block list
 *
 * @return	not used
 */
//init_code void ftl_print_spor_list_info(void)
ddr_code void ftl_print_spor_list_info(void)
{
#if 1
    mUINT_16 blk;
    mUINT_16 idx = 0;
	u16 print_blk_cnt = 0; 
    ftl_apl_trace(LOG_ALW, 0x9bce, "SPOR USR");
    blk = ftl_spor_blk_pool->head[SPB_POOL_USER];
    for(idx = 0 ; idx < ftl_spor_blk_pool->blkCnt[SPB_POOL_USER] ; idx++)
    {
    	print_blk_cnt++;
		if((ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P) && (ftl_spor_blk_pool->blkCnt[SPB_POOL_USER]-print_blk_cnt > 20))
		{
			//do nothing
		}
		else
		{
			ftl_apl_trace(LOG_ALW, 0xd191, "USR Blk:%d SN:0x%x B_T:%d pbt_s:%d",\
                      blk, spb_get_sn(blk), ftl_build_tbl_type[blk],ftl_blk_pbt_seg[blk]);
		}
        blk = ftl_spor_blk_node[blk].nxt_blk;
    }

	print_blk_cnt = 0;
    ftl_apl_trace(LOG_ALW, 0xbd5c, "SPOR GC");
    blk = ftl_spor_blk_pool->head[SPB_POOL_GC];
    for(idx = 0 ; idx < ftl_spor_blk_pool->blkCnt[SPB_POOL_GC] ; idx++)
    {
    	print_blk_cnt++;
    	if((ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P) && (ftl_spor_blk_pool->blkCnt[SPB_POOL_GC]-print_blk_cnt > 20))
		{
			//do nothing
		}
   		else
   		{
			ftl_apl_trace(LOG_ALW, 0x7f3e, "GC Blk:%d SN:0x%x B_T:%d pbt_s:%d",\
							  blk, spb_get_sn(blk), ftl_build_tbl_type[blk],ftl_blk_pbt_seg[blk]);
   		}
        blk = ftl_spor_blk_node[blk].nxt_blk;
    }
#endif
}

init_code u16 ftl_adjust_pbt_build_sn(u8 pool)
{
	if(ftl_get_last_pbt_blk() == INV_U16) 
	{
		ftl_apl_trace(LOG_INFO, 0xc14e, "no open pbt use!!"); 
		return INV_U16; 
	}
	mUINT_16 blk,pre_blk;
    //mUINT_16 idx = 0;
    blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, pool, mFALSE);//get blk after pbt blk
    if(blk == INV_U16)
	{
		return INV_U16;
	}
    //blk = ftl_spor_blk_pool->tail[pool];
	//pre_blk = ftl_spor_blk_node[blk].pre_blk;
	//bool is_find = false;
	pre_blk = INV_U16;

    while(ftl_init_sn_tbl[blk] < ftl_init_sn_tbl[last_pbt_blk])
    {
		if(ftl_blk_pbt_seg[blk] > ftl_get_last_pbt_seg())
		{
			//hit: pre blk seg < pbt seg < blk seg
			break;
		}
		pre_blk = blk;
		blk = ftl_spor_blk_node[blk].nxt_blk;
		if(blk == INV_U16)
			break;
		//ftl_apl_trace(LOG_INFO, 0x11ea,"[DBG] blk:%d seg:%d sn:0x%x <> nxt:%d seg:%d sn:0x%x",
		//	pre_blk,ftl_blk_pbt_seg[pre_blk],ftl_init_sn_tbl[pre_blk],blk,ftl_blk_pbt_seg[blk],ftl_init_sn_tbl[blk]);
    }

	/*
		Purpose: find one blk seg < pbt seg , and scan sn < blk sn < open PBT sn
		Starting from scan sn(close PBT sn),check cur blk seg > pbt seg ,then return pre blk. 
		three situaction:
		1.first blk seg > pbt seg , but  pre blk is INV_U16 , just return INV_U16.
		2.find  blk seg > pbt seg , then pre blk seg < pbt seg , choose this pre blk.
		3.can't find blk seg > pbt seg , but pre blk seg  < pbt seg , we also can use this pre blk.
	*/

	if(pre_blk != INV_U16)
	{
		if(pool == SPB_POOL_USER)
			pbt_new_build = true; //to skip PBT_special_case chk if user blk is too old. 
		ftl_apl_trace(LOG_ALW, 0x0bbe, "pool:%d blk:%d seg:%d sn:0x%x cur_pbt_seg:%d open_pbt_sn:0x%x",
			pool,pre_blk,ftl_blk_pbt_seg[pre_blk],ftl_init_sn_tbl[pre_blk],ftl_get_last_pbt_seg(),ftl_init_sn_tbl[last_pbt_blk]);

	}

    return pre_blk;
}

#if(PLP_SUPPORT == 0)

init_data struct ncl_cmd_t ncl_cmd_ur;
init_data struct info_param_t info_ur[16];
init_data struct raw_column_list sf_addr[16];
init_data pda_t pda_list_BE[16];
init_data pda_t pda_list_BE_LITE[16];

ddr_code void ftl_reset_read_shfit_value(void)
{
    u8 be_cpu_cnt = 2; 
    u8 lun_per_be_cpu = (shr_nand_info.lun_num/be_cpu_cnt);

    u8 j = 0;
    u8 k = 0;

    for(u8 i = 0; i < shr_nand_info.lun_num; i++)
    {
        pda_t make_pda = nal_make_pda(0, i << shr_nand_info.pda_ch_shift);

        if(((make_pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.geo.nr_channels - 1)) < (shr_nand_info.geo.nr_channels/be_cpu_cnt))
        {
            pda_list_BE[k] = make_pda;
            k++;
        }
        else
        {
            pda_list_BE_LITE[j] = make_pda;
            j++;
        }
    }

    u8 feture_addr[2] = {0x89, 0x8A};   
    u8 fa = 0;
    pda_t *pda_list_ur = NULL;


    for(u8 be_cpu = 0; be_cpu < be_cpu_cnt; be_cpu++)
        {
            if(be_cpu == 0)
                pda_list_ur = pda_list_BE;
            else
                pda_list_ur = pda_list_BE_LITE;

            for(u8 fa_num = 0; fa_num < 2; fa_num++)
            {
                for(u8 plane = 0; plane < shr_nand_info.geo.nr_planes; plane++)
                {
                     switch(plane){
                        case 0:
                            fa = feture_addr[fa_num];
                            break;
                        case 1:
                            fa = (feture_addr[fa_num] - 4);
                            break;
                        case 2:
                            fa = (feture_addr[fa_num] +0x20);
                            break;
                        case 3:
                            fa = (feture_addr[fa_num] +0x20-4);
                            break;
                        default:
                            sys_assert(0);
                        break;
                    }

                 	ncl_cmd_ur.op_code = NCL_CMD_SET_GET_FEATURE;
                    ncl_cmd_ur.flags = 0;
                    ncl_cmd_ur.flags |= (NCL_CMD_FEATURE_LUN_FLAG | NCL_CMD_SYNC_FLAG);

                    ncl_cmd_ur.status = 0;
                    ncl_cmd_ur.completion = NULL;

                    ncl_cmd_ur.addr_param.rw_raw_param.list_len = lun_per_be_cpu;
                    ncl_cmd_ur.addr_param.rw_raw_param.info_list = info_ur;
                    ncl_cmd_ur.addr_param.rw_raw_param.pda_list = pda_list_ur;

                    ncl_cmd_ur.addr_param.rw_raw_param.column = sf_addr;
                    ncl_cmd_ur.sf_val = 0x0;

                    for(u8 i = 0; i < lun_per_be_cpu; i++)
                    {
                        sf_addr[i].column = fa;
                    }

                    //ftl_apl_trace(LOG_ALW, 0, "ncl_cmd = 0x%p, info_ur = 0x%p,pda_list_ur = 0x%p,sf_addr = 0x%p",
                      //  &ncl_cmd_ur,info_ur,pda_list_ur,sf_addr);

                    ncl_cmd_submit(&ncl_cmd_ur);
                }
            }
        }
}
#endif
/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
init_code void ftl_p2l_build_table(void)
{
    u8  status = 0xFF;
    u32 nsid = 1; // for Host/GC blk
    u16 usr_blk = INV_U16, gc_blk = INV_U16;
    u32 spor_last_rec_blk_sn = 0;
    u16 build_blk_cnt = 0;
#if(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

    ftl_apl_trace(LOG_ALW, 0x76bf, "[IN] ftl_p2l_build_table");

    ftl_spor_info->p2l_data_error = mFALSE;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    if((epm_ftl_data->epm_sign == FTL_sign) && (epm_ftl_data->spor_tag == FTL_EPM_SPOR_TAG))
    {
        spor_last_rec_blk_sn = epm_ftl_data->spor_last_rec_blk_sn;
    }
    else 
#endif        
    if(gFtlMgr.LastQbtSN != INV_U32)
    {
        spor_last_rec_blk_sn = gFtlMgr.LastQbtSN;
    }

	record_spor_last_rec_blk_sn = spor_last_rec_blk_sn;
	SPOR_need_vac_rebuild = false;
    // for dbg spor list
    ftl_print_spor_list_info();
    
    switch(ftl_spor_info->build_l2P_mode)
    {
		case FTL_PBT_BUILD_L2P:
		
		gFtlMgr.PrevTableSN = ftl_double_chk_spor_preTableSN();
 			//select user
		usr_blk = ftl_adjust_pbt_build_sn(SPB_POOL_USER);
		if(usr_blk == INV_U16)	
		{
			usr_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_USER, mTRUE);
		}
		if((usr_blk != INV_U16) && (ftl_spor_get_blk_sn(usr_blk) < spor_last_rec_blk_sn) && (spor_last_rec_blk_sn != INV_U32))
		{
			ftl_apl_trace(LOG_ALW, 0x2f27, "pre_u_sn 0x%x < last_rec_b_sn 0x%x",ftl_spor_get_blk_sn(usr_blk), spor_last_rec_blk_sn);
			//usr_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_USER, mFALSE);
			usr_blk = ftl_spor_blk_node[usr_blk].nxt_blk;
			if(usr_blk != INV_U16)
			{
				if(ftl_spor_get_blk_sn(usr_blk) > spor_last_rec_blk_sn)
				{
					pregc_update_all = true;
					ftl_apl_trace(LOG_ALW, 0x2b47, "pre gc update all case");
				}
			}
		}

		if(usr_blk != INV_U16 && ftl_init_sn_tbl[usr_blk] < gFtlMgr.PrevTableSN )
		{
			SPOR_need_vac_rebuild = true;
			ftl_apl_trace(LOG_ALW, 0x00a6, "[Warning] cur usr_blk:%d sn:0x%x < PrevTableSN:0x%x",
				usr_blk,ftl_init_sn_tbl[usr_blk],gFtlMgr.PrevTableSN);
		}
		
		gc_blk  = ftl_adjust_pbt_build_sn(SPB_POOL_GC);	//select gc
		if(gc_blk == INV_U16)
		{
			gc_blk	= ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_GC,   mTRUE);
		}
 		if((gc_blk != INV_U16) && (ftl_spor_get_blk_sn(gc_blk) < spor_last_rec_blk_sn) && (spor_last_rec_blk_sn != INV_U32))
		{
			ftl_apl_trace(LOG_ALW, 0x78a1, "pre_g_sn 0x%x < last_rec_b_sn 0x%x",ftl_spor_get_blk_sn(gc_blk), spor_last_rec_blk_sn);
			//gc_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_GC, mFALSE);
			gc_blk = ftl_spor_blk_node[gc_blk].nxt_blk;
		}
		if((usr_blk == INV_U16) && (gc_blk != INV_U16))
		{
			pregc_update_all = true;
			ftl_apl_trace(LOG_ALW, 0x7c85, "PBT no usr blk pre gc update all case");
		}



		ftl_apl_trace(LOG_ALW, 0x1549, "PBT first chk usr:%d gc:%d scan_sn:0x%x PrevTableSN:0x%x",usr_blk, gc_blk,ftl_spor_info->scan_sn,gFtlMgr.PrevTableSN);



            break;

        case FTL_QBT_BUILD_L2P:
        case FTL_USR_BUILD_L2P:
            gc_blk  = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_GC,   mFALSE);
            usr_blk = ftl_get_spor_p2l_blk_head(ftl_spor_info->scan_sn, SPB_POOL_USER, mFALSE);
			ftl_apl_trace(LOG_ALW, 0xaa29, "QBT/USR start usr : %d gc :%d",usr_blk, gc_blk);
            break;

        default:
#if (SPOR_ASSERT == mENABLE)
            panic("no such build l2p mode!\n");
#else
            ftl_apl_trace(LOG_PANIC, 0x6dec, "no such build l2p mode!");
#endif
            break;
    }


    if(usr_blk != INV_U16)
    {
        ftl_apl_trace(LOG_ALW, 0xa0b1, "USR Blk : %d, SN : 0x%x", usr_blk, ftl_spor_get_blk_sn(usr_blk));
        ftl_extract_p2l_table(usr_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, nsid);

		if((usr_blk == gFtlMgr.last_host_blk) && (ftl_spor_info->load_blist_mode == FTL_QBT_LOAD_BLIST))
		{
			u8 chk_pool;
			chk_pool = ftl_chk_blk_in_pool(SPB_POOL_FREE, usr_blk);
			ftl_apl_trace(LOG_ALW, 0x787c, "[FTL]QBT Blist case %d", chk_pool);
		}
		else if((usr_blk == gFtlMgr.last_host_blk) && (ftl_chk_blk_in_pool(SPB_POOL_USER, usr_blk) == mFALSE))
        {
            FTL_BlockPopPushList(SPB_POOL_USER, usr_blk, FTL_SORT_BY_SN);
        }

		if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_PLP_USER)
        {
            ftl_spor_info->plpUSRBlk = usr_blk;
			if(ftl_spor_info->plpUSREndGrp == shr_p2l_grp_cnt){
				spb_set_flag(usr_blk, SPB_DESC_F_CLOSED);
			}
			else{
            spb_set_flag(usr_blk, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE));
			}
        }
        else if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_OPEN_USER)
        {
            spb_set_flag(usr_blk, SPB_DESC_F_OPEN);
        }
    }

    if(gc_blk != INV_U16)
    {
        ftl_apl_trace(LOG_ALW, 0x0232, "GC Blk : %d, SN : 0x%x", gc_blk, ftl_spor_get_blk_sn(gc_blk));
        ftl_extract_p2l_table(gc_blk, ftl_gc_p2l_info.lda_dtag_start, ftl_gc_p2l_info.pgsn_dtag_start, ftl_gc_p2l_info.pda_dtag_start, nsid);

		if((gc_blk == gFtlMgr.last_gc_blk) && (ftl_spor_info->load_blist_mode == FTL_QBT_LOAD_BLIST))
		{
			u8 chk_pool;
			chk_pool = ftl_chk_blk_in_pool(SPB_POOL_FREE, gc_blk);
			ftl_apl_trace(LOG_ALW, 0x3862, "[FTL]QBT Blist case %d", chk_pool);
		}
		else if((gc_blk == gFtlMgr.last_gc_blk) && (ftl_chk_blk_in_pool(SPB_POOL_USER, gc_blk) == mFALSE))
        {
            FTL_BlockPopPushList(SPB_POOL_USER, gc_blk, FTL_SORT_BY_SN);
        }

		if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_PLP_GC)
        {
            ftl_spor_info->plpGCBlk = gc_blk;
			if(ftl_spor_info->plpGCEndGrp == shr_p2l_grp_cnt){
				spb_set_flag(gc_blk, SPB_DESC_F_CLOSED);
			}
			else{
            spb_set_flag(gc_blk, (SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE));
			}
        }
        else if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_OPEN_GC)
        {
            spb_set_flag(gc_blk, SPB_DESC_F_OPEN);
        }
    }

#if (SPOR_TRIM_DATA == mENABLE) 
		//avoid TrimBlkBitmap no init
		epm_trim_t* epm_trim_data = (epm_trim_t*)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		TrimBlkBitamp = (u8 *) epm_trim_data->TrimBlkBitamp;
#endif
	// adjust gc blk start update position add by Curry
										 //----modify by Jay
	if((ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P) && (gc_blk != INV_U16 && usr_blk != INV_U16))
	{
		if(pregc_update_all == false)
		{
			pre_gc_blk = gc_blk;
			PBT_special_case(&gc_blk, &usr_blk, spor_last_rec_blk_sn);
		}
		ftl_apl_trace(LOG_ALW, 0x58bc, "PBT case final chk usr : %d gc : %d", usr_blk, gc_blk);
	}

	if(usr_blk != INV_U16 && gc_blk != INV_U16)
		min_build_sn = min(ftl_spor_get_blk_sn(gc_blk),ftl_spor_get_blk_sn(usr_blk));
	else if(usr_blk != INV_U16)
		min_build_sn = ftl_spor_get_blk_sn(usr_blk);
	else
		min_build_sn = ftl_spor_get_blk_sn(gc_blk);

	while(gc_blk!=INV_U16)
    {
    	build_blk_cnt++;
        /*
        compare 2 blk p2l in a function
        if 1 blk has reach end of line, break out function and switch next blk
        return value need to know which block reach the end
        */
		/*
        #if(SPOR_BYPASS_OPEN_BLK == mENABLE)
		if(gc_blk!=INV_U16)
		{
			if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_OPEN_GC)
		    {
		    	ftl_apl_trace(LOG_ALW, 0, "GC OPEN blk");
		        status = GC_P2L_END;
		        goto EXTRACT_NEXT_GC_P2L;
		    }
		}
        #endif
        */

        status = ftl_cross_p2l_update_l2p(usr_blk, gc_blk, P2L_GC_ONLY);
        ftl_apl_trace(LOG_ALW, 0xe0e5, "gFtlMgr.GlobalPageSN : 0x%x%x", (u32)(gFtlMgr.GlobalPageSN >> 32), (u32)(gFtlMgr.GlobalPageSN));
#if (SPOR_ASSERT == mENABLE)
        sys_assert(status!=0);
#endif
        ftl_apl_trace(LOG_ALW, 0xef97, "USR Blk : %d, GC Blk : %d, P2L Status : 0x%x", usr_blk, gc_blk, status);
        #if 0 //(SPOR_BYPASS_OPEN_BLK == mENABLE)
EXTRACT_NEXT_GC_P2L:
        #endif
        if((status == GC_P2L_END)&&(gc_blk != INV_U16))
        {
            gc_blk = ftl_spor_blk_node[gc_blk].nxt_blk;
#if (SPOR_ASSERT == mENABLE)
            sys_assert(gc_blk != 0);
#endif
            if(gc_blk != INV_U16)
            {
                ftl_apl_trace(LOG_ALW, 0xcd01, "GC Blk : %d, SN : 0x%x", gc_blk, ftl_spor_get_blk_sn(gc_blk));
                ftl_extract_p2l_table(gc_blk, ftl_gc_p2l_info.lda_dtag_start, ftl_gc_p2l_info.pgsn_dtag_start, ftl_gc_p2l_info.pda_dtag_start, nsid);
                if((gc_blk == gFtlMgr.last_gc_blk) &&
                   (ftl_chk_blk_in_pool(SPB_POOL_USER, gc_blk) == mFALSE))
                {
                    FTL_BlockPopPushList(SPB_POOL_USER, gc_blk, FTL_SORT_BY_SN);
                }
            }
        }
    }

	while(usr_blk!=INV_U16)
    {
    	build_blk_cnt++;
        /*
        compare 2 blk p2l in a function
        if 1 blk has reach end of line, break out function and switch next blk
        return value need to know which block reach the end
        */
		/*
        #if(SPOR_BYPASS_OPEN_BLK == mENABLE)
		if(usr_blk!=INV_U16)
		{
		    if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_OPEN_USER)
		    {
		    	ftl_apl_trace(LOG_ALW, 0, "USR OPEN blk");
		        status = USR_P2L_END;
		        goto EXTRACT_NEXT_USR_P2L;
		    }
		}
        #endif
		*/
		status = ftl_cross_p2l_update_l2p(usr_blk, gc_blk, P2L_USR_ONLY);
        ftl_apl_trace(LOG_ALW, 0xf746, "gFtlMgr.GlobalPageSN : 0x%x%x", (u32)(gFtlMgr.GlobalPageSN >> 32), (u32)(gFtlMgr.GlobalPageSN));
#if (SPOR_ASSERT == mENABLE)
        sys_assert(status!=0);
#endif
        ftl_apl_trace(LOG_ALW, 0x44df, "USR Blk : %d, GC Blk : %d, P2L Status : 0x%x", usr_blk, gc_blk, status);

        #if 0 //(SPOR_BYPASS_OPEN_BLK == mENABLE)
EXTRACT_NEXT_USR_P2L:
        #endif
        if((status == USR_P2L_END)&&(usr_blk != INV_U16))
        {
            usr_blk = ftl_spor_blk_node[usr_blk].nxt_blk;
#if (SPOR_ASSERT == mENABLE)
            sys_assert(usr_blk != 0);
#endif
            if(usr_blk != INV_U16)
            {
                ftl_apl_trace(LOG_ALW, 0x68d8, "USR Blk : %d, SN : 0x%x", usr_blk, ftl_spor_get_blk_sn(usr_blk));
                ftl_extract_p2l_table(usr_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, nsid);
                if((usr_blk == gFtlMgr.last_host_blk) &&
                   (ftl_chk_blk_in_pool(SPB_POOL_USER, usr_blk) == mFALSE))
                {
                    FTL_BlockPopPushList(SPB_POOL_USER, usr_blk, FTL_SORT_BY_SN);
                }
            }
        }
    }
	/*
    while((usr_blk!=INV_U16) || (gc_blk!=INV_U16))
    {

        //compare 2 blk p2l in a function
        //if 1 blk has reach end of line, break out function and switch next blk
        //return value need to know which block reach the end


        #if 1  // added to bypass open block rebuild, need to delete later,  Sunny 20210303
			if(usr_blk!=INV_U16)
			{
		        if(ftl_build_tbl_type[usr_blk] == FTL_SN_TYPE_OPEN_USER)
		        {
		        	ftl_apl_trace(LOG_ALW, 0, "USR OPEN blk");
		            status = USR_P2L_END;
		            goto EXTRACT_NEXT_P2L;
		        }
			}
			if(gc_blk!=INV_U16)
			{
				if(ftl_build_tbl_type[gc_blk] == FTL_SN_TYPE_OPEN_GC)
		        {
		        	ftl_apl_trace(LOG_ALW, 0, "GC OPEN blk");
		            status = GC_P2L_END;
		            goto EXTRACT_NEXT_P2L;
		        }
			}
        #endif

        mode = ftl_blk_update_order(usr_blk, gc_blk);
        ftl_apl_trace(LOG_ALW, 0, "Cross P2L Update Mode : 0x%x", mode);

        status = ftl_cross_p2l_update_l2p(usr_blk, gc_blk, P2L_GC_ONLY);
		status = ftl_cross_p2l_update_l2p(usr_blk, gc_blk, P2L_USR_ONLY);
        //ftl_apl_trace(LOG_ALW, 0, "gFtlMgr.GlobalPageSN : 0x%x", gFtlMgr.GlobalPageSN);
#if (SPOR_ASSERT == mENABLE)
        sys_assert(status!=0);
#endif
        ftl_apl_trace(LOG_ALW, 0, "USR Blk : 0x%x, GC Blk : 0x%x, P2L Status : 0x%x", usr_blk, gc_blk, status);

        #if 1  // added to bypass open block rebuild, need to delete later,  Sunny 20210303
EXTRACT_NEXT_P2L:
        #endif
        if((status == USR_P2L_END)&&(usr_blk != INV_U16))
        {
            usr_blk = ftl_spor_blk_node[usr_blk].nxt_blk;
#if (SPOR_ASSERT == mENABLE)
            sys_assert(usr_blk != 0);
#endif
            if(usr_blk != INV_U16)
            {
                ftl_apl_trace(LOG_ALW, 0, "USR Blk : 0x%x, SN : 0x%x", usr_blk, ftl_spor_get_blk_sn(usr_blk));
                ftl_extract_p2l_table(usr_blk, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, nsid);
                if(usr_blk == gFtlMgr.last_host_blk)
                {
                    FTL_BlockPushList(SPB_POOL_USER, usr_blk, FTL_SORT_BY_SN);
                }
            }
        }
        else if((status == GC_P2L_END)&&(gc_blk != INV_U16))
        {
            gc_blk = ftl_spor_blk_node[gc_blk].nxt_blk;
#if (SPOR_ASSERT == mENABLE)
            sys_assert(gc_blk != 0);
#endif
            if(gc_blk != INV_U16)
            {
                ftl_apl_trace(LOG_ALW, 0, "GC Blk : 0x%x, SN : 0x%x", gc_blk, ftl_spor_get_blk_sn(gc_blk));
                ftl_extract_p2l_table(gc_blk, ftl_gc_p2l_info.lda_dtag_start, ftl_gc_p2l_info.pgsn_dtag_start, ftl_gc_p2l_info.pda_dtag_start, nsid);
                if(gc_blk == gFtlMgr.last_gc_blk)
                {
                    FTL_BlockPushList(SPB_POOL_USER, gc_blk, FTL_SORT_BY_SN);
                }
            }
        }
    } // while((usr_blk!=INV_U16) || (gc_blk!=INV_U16))
	*/
	#if(PLP_SUPPORT == 0)
	ftl_reset_read_shfit_value();
	#endif
    ftl_apl_trace(LOG_ALW, 0xe7c3, "[SPOR] PlpUSRBlk[%d] PlpGCBlk[%d]", ftl_spor_info->plpUSRBlk, ftl_spor_info->plpGCBlk);

#if(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0xe010, "Function time cost : %d us build_blk_cnt:%d", time_elapsed_in_us(time_start),build_blk_cnt);
#endif
}

/*!
 * @brief ftl spor function
 * read blist/vc tbl table from Host block done
 *
 * @return	not used
 */
init_code void ftl_read_misc_data_from_host_done(struct ncl_cmd_t *ncl_cmd)
{
    u32 cmd_idx;
    u8  blank_flag = mFALSE, uecc_flag = mFALSE, meta_crc_flag = mFALSE;
    u8  i, length;
    struct info_param_t *info_list;

    cmd_idx   = ncl_cmd->user_tag_list[0].pl.btag;
    info_list = ncl_cmd->addr_param.common_param.info_list;
    length    = ncl_cmd->addr_param.common_param.list_len;

    // ToDo : current ncl cmd error interrupt might have problem, by Tony
    // ============ Error handle area ============
    #if NCL_FW_RETRY
    if(ncl_cmd->status)
    {
        extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
        if(ncl_cmd->retry_step == default_read)
        {
    	    //ftl_apl_trace(LOG_ALW, 0, "ftl_read_misc_data_handling, ncl_cmd: 0x%x", ncl_cmd);
            rd_err_handling(ncl_cmd);
            return;
        }
        else if(ncl_cmd->retry_step != raid_recover_fail)
        {
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && ftl_uc_pda_chk(ncl_cmd) && (rced == false))
            //if (fcns_raid_enabled(nsid) && (rced == false))
            {
            	for (i = 0; i < length; i++)
                {
        		    if (info_list[i].status > ficu_err_du_ovrlmt)
                    {
                        rc_req_t* rc_req = l2p_rc_req_prepare(ncl_cmd);
                        raid_correct_push(rc_req);
                        return;
                    }
                }
            }
        }
    }
    #endif

    for(i=0; i<length; i++)
    {
        if(info_list[i].status == cur_du_erase) // Blank
        {
            blank_flag = mTRUE;
        }
        else if(info_list[i].status >= cur_du_ucerr) // UECC
        {
            uecc_flag = mTRUE;
        }
        else if(info_list[i].status == cur_du_dfu_err) // meta CRC error
        {
            meta_crc_flag = mTRUE;
        }
    }
    // ============ Error handle area ============

    if(blank_flag || uecc_flag || meta_crc_flag)
    {
        ftl_spor_info->read_data_fail = mTRUE;
    }

    _spor_free_cmd_bmp |= (1 << cmd_idx);
}

/*!
 * @brief ftl spor function
 * read blist/vc tbl table from Host block
 *
 * @return	not used
 */
slow_code bool ftl_read_misc_data_from_host(u16 spb_id)
{
    pda_t  pda = 0, cpda = 0;
    u8     i, du_cnt = 0;
    u8      blist_du_idx = 0;
    u8     remain_du_cnt = 0;

    u16    cont_idx;
    u16    cmd_idx;
    u32    meta_idx;
#ifdef SKIP_MODE
    u32    die_pn_idx;
    u8     *df_ptr;
    u32    idx, off;
#endif

#if(PLP_SUPPORT == 1)
	u8 	vc_du_idx = 0;
	u32 vc_du_needed;
	dtag_t dtag_vc_start;
#else//nonplp
	u8  trim_du_idx = 0;
	u32 trim_du_needed = 1;
	dtag_t dtag_trim_start;
#endif

    ftl_apl_trace(LOG_ALW, 0xec45, "[IN] ftl_read_misc_data_from_host, blk id : %d", spb_id);

    ftl_spor_info->read_data_fail = mFALSE;

    // ===== send ncl form to read nand data =====
#if(PLP_SUPPORT == 1)
    dtag_vc_start = ftl_l2p_get_vcnt_buf(&vc_du_needed, NULL);
    ftl_apl_trace(LOG_ALW, 0x9cac, "vac du cnt : 0x%x, blist du cnt : 0x%x", vc_du_needed, shr_blklistbuffer_need);
    remain_du_cnt = vc_du_needed + shr_blklistbuffer_need;
#else
	dtag_trim_start = dtag_cont_get(DTAG_T_SRAM, trim_du_needed);
    ftl_apl_trace(LOG_ALW, 0x5c69, "trim table du cnt : 0x%x, blist du cnt : 0x%x", trim_du_needed, shr_blklistbuffer_need);
    remain_du_cnt = trim_du_needed + shr_blklistbuffer_need;
#endif



    cpda = ftl_blk2cpda(spb_id);
#ifdef SKIP_MODE
    df_ptr = ftl_get_spb_defect(spb_id);
#endif
    while(remain_du_cnt>0)
    {
#ifdef SKIP_MODE
#ifdef While_break
		u64 start = get_tsc_64();
#endif
#endif
        //-----Chk next pda pos-----
        while(1)
        {
            pda = ftl_cpda2pda(cpda, V_MODE);
#ifdef SKIP_MODE
            die_pn_idx = (ftl_pda2die(pda) << FTL_PN_SHIFT) | ftl_pda2plane(pda);
            idx = die_pn_idx >> 3;
         	off = die_pn_idx & (7);
          	if(((df_ptr[idx] >> off)&1)!=0)
            {

#ifdef While_break
				if(Chk_break(start,__FUNCTION__, __LINE__))
					break;
#endif
                // is bad block skip to next page entry
                cpda+=DU_CNT_PER_PAGE;
                continue;
            }
#endif
            break;
        }
        //-----Chk next pda pos-----

        cmd_idx = get_spor_ncl_cmd_idx();
        cont_idx = cmd_idx * DU_CNT_PER_PAGE;
        meta_idx = ftl_spor_meta_idx + cont_idx;
        memset(&ftl_info[cont_idx], 0, (DU_CNT_PER_PAGE * sizeof(*ftl_info)));

        du_cnt = (remain_du_cnt>DU_CNT_PER_PAGE)?DU_CNT_PER_PAGE:remain_du_cnt;
        for(i = 0; i < du_cnt; i++)
        {
            ftl_info[cont_idx+i].pb_type       = NAL_PB_TYPE_XLC;
            ftl_pda_list[cont_idx+i]           = pda+i;
            ftl_bm_pl[cont_idx+i].pl.btag      = cmd_idx;
    		ftl_bm_pl[cont_idx+i].pl.du_ofst   = i;
    #if (SPOR_CMD_EXP_BAND == mENABLE)
            ftl_bm_pl[cont_idx+i].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
    #else
            ftl_bm_pl[cont_idx+i].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
    #endif
            ftl_bm_pl[cont_idx+i].pl.nvm_cmd_id = meta_idx + i;
#if(PLP_SUPPORT == 1)
            if(vc_du_idx < vc_du_needed)
            {
                ftl_bm_pl[cont_idx+i].pl.dtag   = (dtag_vc_start.dtag+vc_du_idx);
                vc_du_idx++;
                //ftl_apl_trace(LOG_ALW, 0, "vc_du_idx : 0x%x, pda : 0x%x", vc_du_idx, pda);
            }
#else
			if(trim_du_idx < trim_du_needed)
			{
				ftl_bm_pl[cont_idx+i].pl.dtag	= (dtag_trim_start.dtag+trim_du_idx);
				trim_du_idx++;
			}
#endif
            else if(blist_du_idx < shr_blklistbuffer_need)
            {
                ftl_bm_pl[cont_idx+i].pl.dtag   = ((shr_blklistbuffer_start[0]+blist_du_idx) | DTAG_IN_DDR_MASK);
                blist_du_idx++;
                //ftl_apl_trace(LOG_ALW, 0, "blist_du_idx : 0x%x, pda : 0x%x", blist_du_idx, pda);
            }
        }

        ftl_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &ftl_info[cont_idx];
        ftl_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &ftl_pda_list[cont_idx];
        ftl_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = du_cnt;
        ftl_ncl_cmd[cmd_idx].status        = 0;

        ftl_ncl_cmd[cmd_idx].op_code       = NCL_CMD_SPOR_BLIST_READ;
#if defined(HMETA_SIZE)
        ftl_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG;
        ftl_ncl_cmd[cmd_idx].du_format_no  = host_du_fmt;
        ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_DATA_READ_PA_DTAG;
#else
        ftl_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
        ftl_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
        ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#endif
        ftl_ncl_cmd[cmd_idx].completion    = ftl_read_misc_data_from_host_done;
        ftl_ncl_cmd[cmd_idx].user_tag_list = &ftl_bm_pl[cont_idx];
        ftl_ncl_cmd[cmd_idx].caller_priv   = (void*)pda;
        ftl_ncl_cmd[cmd_idx].retry_step    = 0;

		#if RAID_SUPPORT_UECC
		ftl_ncl_cmd[cmd_idx].uecc_type = NCL_UECC_SPOR_RD;
		#endif

        ncl_cmd_submit(&ftl_ncl_cmd[cmd_idx]);

        remain_du_cnt-=du_cnt;
        //ftl_apl_trace(LOG_ALW, 0, "remain_du_cnt : 0x%x", remain_du_cnt);
        if((remain_du_cnt == 0) || ftl_spor_info->read_data_fail) {break;}
        cpda+=DU_CNT_PER_PAGE;
    }

    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done
#if(PLP_SUPPORT == 1)
	if(ftl_spor_info->read_data_fail)
	{
		ftl_apl_trace(LOG_ALW, 0x35aa, "Blist/VAC read fail");
		ftl_spor_info->read_data_fail = mFALSE;
		ftl_l2p_put_vcnt_buf(dtag_vc_start, vc_du_needed, false);
		return mFALSE;
	}
	else
	{
		ftl_l2p_put_vcnt_buf(dtag_vc_start, vc_du_needed, true);
		return mTRUE;
	}
#else
	if(ftl_spor_info->read_data_fail)
	{
		ftl_apl_trace(LOG_ALW, 0x83e0, "Blist/trim table read fail");
		ftl_spor_info->read_data_fail = mFALSE;
		dtag_cont_put(DTAG_T_SRAM, dtag_trim_start, trim_du_needed);
		return mFALSE;
	}
	else
	{
		//extern u8* TrimBlkBitamp;
		memcpy(ftl_spor_trimblkbitmap, dtag2mem(dtag_trim_start), 256);		
		dtag_cont_put(DTAG_T_SRAM, dtag_trim_start, trim_du_needed);
		/*
		for(u8 i = 0;i < 64 ;i++)
		{
			ftl_apl_trace(LOG_ALW, 0, "idx:%d bitmap:0x%x",i,ftl_spor_trimblkbitmap[i]);
		}
		*/
		return mTRUE;
	}
#endif


}

fast_code void ftl_spor_get_raid_source(ftl_spor_raid_info_t *raid_info)
{
    u32 i;
    u32 raid_id = (TTL_RAID_ID - 3);
    u32 bank_id = (RAID_BANK_NUM - 1);

    sys_assert(FTL_PN_CNT <= RAID_BUF_PER_BANK);

    for (i = 0; i < FTL_PG_IN_WL_CNT; i++) {
        raid_info->raid_id[i] = raid_id;
        raid_info->bank_id[i] = bank_id;
        raid_id --;
        bank_id --;
    }
}

/*!
 * @brief ftl spor function
 * flush l2p tables and block list
 *
 * @return	not used
 */

typedef struct _ftl_hns_t {
	ftl_ns_t *ns;		///< parent ftl namespace object

	l2p_ele_t l2p_ele;	///< l2p element, describe l2p of this namespace
} ftl_hns_t;

extern ftl_hns_t _ftl_hns[FTL_NS_ID_END];	///< ftl host namespace objects

//init_code void ftl_flush_qbt(u8 spb_cnt, u16 *spb_id)
slow_code bool ftl_flush_qbt(u8 spb_cnt, u16 *spb_id)
{
	struct ncl_cmd_t *qbt_ncl_cmd;  // 80B
	qbt_ncl_cmd  = sys_malloc(FAST_DATA, sizeof(*qbt_ncl_cmd)*FTL_SPOR_NCL_CMD_MAX);
	memset(&qbt_ncl_cmd[0], 0, sizeof(*qbt_ncl_cmd)*FTL_SPOR_NCL_CMD_MAX);

    u8   loop, plane_op = 0;
    u8   du_idx, du_cnt;
    u32  cmd_idx, cont_idx, bm_pl_idx;
    u32  meta_idx;
    u32  l2p_du_idx = 0, blist_du_idx = 0, vc_tbl_du_idx = 0;
    u32  total_du_idx;
    pda_t cpda_idx = 0, cpda_next = 0;
    pda_t cpda_tmp = 0; // for dbg
    u32   vc_dtag_cnt_needed;
    #if 0
    u32   vc_tmp_dtag_cnt_needed;
    u32   l2p_dtag_cnt_needed;
    u32   seg_size = DU_CNT_PER_PAGE*DTAG_SZE*FTL_PG_IN_WL_CNT;
    #endif

	u8	i, die_id;
	u8	pn_pair;
#ifdef SKIP_MODE
	u32 pn_ptr = 0;
#endif

    //u32 l2p_du_cnt = shr_l2p_entry_need;
    u32 l2p_du_cnt = (_ftl_hns[1].l2p_ele.seg_end*NR_L2PP_IN_SEG); // for dynamic OP

    dtag_t vc_dtag_start = ftl_l2p_get_vcnt_buf(&vc_dtag_cnt_needed, NULL);


    ftl_apl_trace(LOG_ALW, 0xd869, "[IN] ftl_flush_qbt spb_cnt %d l2p_du_cnt %d", spb_cnt, l2p_du_cnt);

    #if 0 // dbg_log
    ftl_apl_trace(LOG_ALW, 0xcd53, "[original] l2g du cnt : 0x%x,vc du cnt : 0x%x", shr_l2p_entry_need, vc_dtag_cnt_needed);

    //vc_tmp_dtag_cnt_needed = (occupied_by(vc_dtag_cnt_needed*DTAG_SZE, seg_size))*DU_CNT_PER_PAGE*FTL_PG_IN_WL_CNT;
    l2p_dtag_cnt_needed = (occupied_by(shr_l2p_entry_need*DTAG_SZE, seg_size))*DU_CNT_PER_PAGE*FTL_PG_IN_WL_CNT;

    ftl_apl_trace(LOG_ALW, 0xb6ae, "[seglized] l2g du cnt : 0x%x, vc du cnt : 0x%x, blist du cnt : 0x%x",\
                  l2p_dtag_cnt_needed, vc_dtag_cnt_needed, shr_blklistbuffer_need);
    #endif

    // get raid source
    ftl_spor_raid_info_t raid_info;
    u32 parity_die = FTL_DIE_CNT;
    bool parity_user_done = true;
    u8   parity_pn_pair = 0;
    bool parity_die_user_data = true;
    u8 parity_die_pln_idx = shr_nand_info.geo.nr_planes;
    pda_t cpda_idx_backup = INV_PDA;
    u8 raid_pg_idx;
    bool raid_support = fcns_raid_enabled(INT_NS_ID);

    if(raid_support){
        ftl_spor_get_raid_source(&raid_info);
    }

    meta_idx = ftl_spor_meta_idx;

    for(loop = 0; loop<spb_cnt; loop++)
    {
        ftl_apl_trace(LOG_ALW, 0xf5b7, "spb id : %u, shr_qbt_loop : 0x%x raid_support %d",
						spb_id[loop], shr_qbt_loop, raid_support);


        // ===== set 1 page meta first, all page meta in QBT blk is the same =====
        for(du_idx = 0; du_idx < DU_CNT_PER_PAGE; du_idx++)
        {
            ftl_set_page_meta(meta_idx, du_idx, (FTL_FQBT_TABLE_TAG|shr_qbt_loop), spb_id[loop], FTL_CORE_QBT);
        }

        shr_qbt_loop++;
        if(shr_qbt_loop == FTL_MAX_QBT_CNT)
        {
            shr_qbt_loop = 0;
        }
        // ===== init info per blk =====
        total_du_idx = 0;
        cpda_idx = ftl_blk2cpda(spb_id[loop]);

        if(raid_support){
            parity_die = FTL_DIE_CNT - 1;

            while(parity_die >= 0)
            {
#ifdef SKIP_MODE
				pn_ptr = parity_die<<FTL_PN_SHIFT;
				pn_pair = ftl_good_blk_in_pn_detect(spb_id[loop], pn_ptr);
#else
				pn_pair = BLOCK_NO_BAD;
#endif
                if (pn_pair == BLOCK_ALL_BAD) {
                    parity_die--;
                } else {
                    parity_pn_pair = pn_pair;
                    if(shr_nand_info.geo.nr_planes - pop32(parity_pn_pair) == 1)
                        parity_die_user_data = false;

                    bool ret = false;
                    for (u8 pl = 0; pl < shr_nand_info.geo.nr_planes; pl++){
                        if (((pn_pair >> pl)&1)==0){ //good plane
                            parity_die_pln_idx = pl;
                            ret = true;
                                break;
                        }
                    }
                    sys_assert(ret);
                    break;
                }
            }
        }
		//u32 l2p_ddtag_idx = 0;
        while(total_du_idx!=FTL_DU_PER_TLC_SBLOCK)
        {
            #if 0 // for dbg
            pda_tmp = ftl_cpda2pda(cpda_idx, V_MODE);
            ftl_apl_trace(LOG_ALW, 0x61d5, "QBT blk : 0x%x, WL idx : 0x%x, cpda_idx : 0x%x", spb_id[loop], ftl_pda2wl(pda_tmp), cpda_idx);
            #endif

            die_id = 0;
            if(raid_support){
                parity_user_done = !parity_die_user_data;
            }
			// for each wl
            while(die_id < FTL_DIE_CNT)
            {
                if(raid_support){
                    // skip parity plane
                    if((die_id == parity_die) && (!parity_user_done)){
                        cpda_idx_backup = cpda_idx;
                    }
                }
#ifdef SKIP_MODE
				pn_ptr = die_id<<FTL_PN_SHIFT;
				pn_pair = ftl_good_blk_in_pn_detect(spb_id[loop], pn_ptr);
#else
				pn_pair = BLOCK_NO_BAD;
#endif
                if((die_id == parity_die) && raid_support){
                    if(parity_user_done){
                        pn_pair = (~(1 << parity_die_pln_idx)) & ((1<<shr_nand_info.geo.nr_planes)-1); // just write parity plane
                    }else{
                        pn_pair |= 1 << parity_die_pln_idx; // skip parity plane
					}
                }
				if(pn_pair > (1<<shr_nand_info.geo.nr_planes)-1)
				{
					panic("pn_pair case abnormal");
				}
				else if(pn_pair == BLOCK_ALL_BAD)
        		{
        			die_id++;
					total_du_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
                    cpda_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
           			continue;
        		}

                cpda_next = cpda_idx + (FTL_DU_PER_WL<<FTL_PN_SHIFT);

                plane_op = FTL_PN_CNT;

                cpda_tmp = cpda_idx;
                if(((die_id == parity_die && parity_user_done) || (die_id == 0)) && raid_support){
                    wait_spor_cmd_idle(); // wait xor/pout done
                }
                cmd_idx  = get_spor_ncl_cmd_idx();
                //ftl_apl_trace(LOG_ALW, 0, "_spor_free_cmd_bmp : 0x%x", _spor_free_cmd_bmp)
                //cont_idx = cmd_idx*FTL_DU_PER_WL*FTL_PN_CNT; //by 4*3*pln_num
                //memset(&ftl_info[cont_idx], 0, (FTL_DU_PER_WL*FTL_PN_CNT*sizeof(*ftl_info)));

                cont_idx = cmd_idx*FTL_PG_IN_WL_CNT*FTL_PN_CNT; // 3*pln_num
                bm_pl_idx = cmd_idx*DU_CNT_PER_PAGE*FTL_PG_IN_WL_CNT*FTL_PN_CNT;   // 4*3*pln_num

                memset(&ftl_info[cont_idx], 0, (FTL_PG_IN_WL_CNT*FTL_PN_CNT*sizeof(*ftl_info)));
				//memset(&ftl_pda_list[cont_idx], 0, (FTL_PG_IN_WL_CNT*FTL_PN_CNT*sizeof(*ftl_pda_list)));

				//memset(&ftl_bm_pl[cont_idx], 0, (FTL_DU_PER_WL*FTL_PN_CNT*sizeof(*ftl_bm_pl)));

                #if 0 // for dbg
                ftl_apl_trace(LOG_ALW, 0xcc92, "Current PDA : 0x%x", pda);
                ftl_apl_trace(LOG_ALW, 0x8232, "CH : 0x%x, CE : 0x%x, LUN : 0x%x, PN : 0x%x, Page : 0x%x, DU : 0x%x",\
                              ftl_pda2ch(pda), ftl_pda2ce(pda), ftl_pda2lun(pda),\
                              ftl_pda2plane(pda), ftl_pda2page(pda), ftl_pda2du(pda));
                #endif

				u8 page_cmt_idx = 0;
				for(i = 0; i < FTL_PG_IN_WL_CNT * FTL_PN_CNT; i++)
				{
					if(BIT_CHECK(pn_pair, i % FTL_PN_CNT) == 0)
					{
						ftl_pda_list[cont_idx+page_cmt_idx]     = ftl_cpda2pda(cpda_idx, V_MODE);
                        //ftl_apl_trace(LOG_ALW, 0, "Current PDA : 0x%x", ftl_cpda2pda(cpda_idx, V_MODE));
                        ftl_info[cont_idx+page_cmt_idx].pb_type = NAL_PB_TYPE_XLC;
                        if(raid_support)
                        {
							raid_pg_idx = ftl_pda2page(ftl_pda_list[cont_idx+page_cmt_idx]) % FTL_PG_IN_WL_CNT;
							ftl_info[cont_idx+page_cmt_idx].raid_id = raid_info.raid_id[raid_pg_idx];
							ftl_info[cont_idx+page_cmt_idx].bank_id = raid_info.bank_id[raid_pg_idx];
                       	}
                       	page_cmt_idx++;
					}
 					cpda_idx += DU_CNT_PER_PAGE;
				}
				plane_op = page_cmt_idx / FTL_PG_IN_WL_CNT;

                du_cnt = plane_op*FTL_DU_PER_WL;
                for(du_idx = 0; du_idx < du_cnt; du_idx++)
                {
                	ftl_bm_pl[bm_pl_idx+du_idx].all        = 0;
                    ftl_bm_pl[bm_pl_idx+du_idx].pl.btag       = cmd_idx;
                	ftl_bm_pl[bm_pl_idx+du_idx].pl.du_ofst    = du_idx;
                	ftl_bm_pl[bm_pl_idx+du_idx].pl.nvm_cmd_id = meta_idx + (du_idx%DU_CNT_PER_PAGE);
#if (SPOR_CMD_EXP_BAND == mENABLE)
                	ftl_bm_pl[bm_pl_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
#else
                	ftl_bm_pl[bm_pl_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
#endif

                    // -----------------------------------------------------------------------
                    if(die_id == parity_die && raid_support && parity_user_done)
                    {
                        ftl_bm_pl[cont_idx+du_idx].pl.dtag = WVTAG_ID;
                    }
                    else if(l2p_du_idx < l2p_du_cnt) // L2P data
                    {
                        ftl_bm_pl[bm_pl_idx+du_idx].pl.dtag  = (shr_l2p_entry_start+l2p_du_idx) | DTAG_IN_DDR_MASK;
                        #if 1 // for dbg
                        if((l2p_du_idx == 0)  || (l2p_du_idx == (l2p_du_cnt-1)))
                        {
                            ftl_apl_trace(LOG_ALW, 0x98ec, "L2P du %d done PDA: 0x%x dtag 0x%x",
								l2p_du_idx,
								ftl_cpda2pda(cpda_tmp, V_MODE),
								ftl_bm_pl[bm_pl_idx+du_idx].pl.dtag);
                        }
                        #endif
                        l2p_du_idx++;
                    }
                    else if(vc_tbl_du_idx < vc_dtag_cnt_needed) // VC table
                    {
                        ftl_bm_pl[bm_pl_idx+du_idx].pl.dtag   = (vc_dtag_start.dtag + vc_tbl_du_idx);
                        #if 1 // for dbg
                        //if(vc_tbl_du_idx == vc_tmp_dtag_cnt_needed)
                        {
                            ftl_apl_trace(LOG_ALW, 0x79bc, "VC du done PDA : 0x%x dtag 0x%x", ftl_cpda2pda(cpda_tmp, V_MODE), ftl_bm_pl[cont_idx+du_idx].pl.dtag);
                        }
                        #endif
                        vc_tbl_du_idx++;
                    }
                    else if(blist_du_idx < shr_blklistbuffer_need) // Blk info data
                    {
                        ftl_bm_pl[bm_pl_idx+du_idx].pl.dtag   = (shr_blklistbuffer_start[0]+blist_du_idx) | DTAG_IN_DDR_MASK;
                        #if 1 // for dbg
                        //if(blist_du_idx == shr_blklistbuffer_need)
                        {
                            ftl_apl_trace(LOG_ALW, 0xfd54, "Blist du done PDA : 0x%x dtag 0x%x", ftl_cpda2pda(cpda_tmp, V_MODE), ftl_bm_pl[cont_idx+du_idx].pl.dtag);
                        }
                        #endif
                        blist_du_idx++;
                    }
					else // dummy data
                    {
                        ftl_bm_pl[bm_pl_idx+du_idx].pl.dtag = WVTAG_ID;
                    }
                    cpda_tmp++;
                    // -----------------------------------------------------------------------
                } // for(du_idx = 0; du_idx < du_cnt; du_idx++)
                qbt_ncl_cmd[cmd_idx].flags = 0;
                if(raid_support){
                    if(die_id == parity_die && parity_user_done){
                        qbt_ncl_cmd[cmd_idx].flags = NCL_CMD_RAID_POUT_FLAG_SET;
                    }else{
                        qbt_ncl_cmd[cmd_idx].flags = NCL_CMD_RAID_XOR_FLAG_SET;
                    }
                }
                qbt_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &ftl_info[cont_idx];
                qbt_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &ftl_pda_list[cont_idx];
                qbt_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = plane_op*FTL_PG_IN_WL_CNT;
                qbt_ncl_cmd[cmd_idx].status        = 0;
                qbt_ncl_cmd[cmd_idx].flags         |= NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
                qbt_ncl_cmd[cmd_idx].op_code       = NCL_CMD_OP_WRITE;
#if defined(HMETA_SIZE)
                qbt_ncl_cmd[cmd_idx].op_type       = NCL_CMD_PROGRAM_DATA;
                qbt_ncl_cmd[cmd_idx].du_format_no  = host_du_fmt;
#else
                qbt_ncl_cmd[cmd_idx].op_type       = NCL_CMD_PROGRAM_TABLE;
                qbt_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
#endif
                qbt_ncl_cmd[cmd_idx].completion    = ftl_fqbt_write_done;
                qbt_ncl_cmd[cmd_idx].user_tag_list = &ftl_bm_pl[bm_pl_idx];
                qbt_ncl_cmd[cmd_idx].caller_priv   = NULL;

                ncl_cmd_submit(&qbt_ncl_cmd[cmd_idx]);

				// finish l2p table flush (wl) & return to parity die to write
                if(raid_support && (die_id == FTL_DIE_CNT - 1) && (!parity_user_done)){
                    // jump to parity plane after write wl done if parity die is not last die
                    die_id = parity_die; // return to parity die
                    cpda_idx = cpda_idx_backup; // cpda of parity die
                    cpda_idx_backup = cpda_next; // cpda of next wl
                    // total_du_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
                    parity_user_done = true;
                    sys_assert(cpda_idx != INV_PDA);
				// finish writing parity die
                }else if(raid_support && (die_id == parity_die) && (parity_user_done)){
                    cpda_idx = cpda_idx_backup; // cpda of next wl
                    die_id = FTL_DIE_CNT;
                    total_du_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
                }else{
                    total_du_idx += (FTL_DU_PER_WL*FTL_PN_CNT);
                    cpda_idx = cpda_next;
                    die_id++;
                }
            } // while(die_pn_idx<FTL_TOT_PN_CNT)

            if (shr_qbt_prog_err)
            {
                break;
            }

        } // while(total_du_idx!=FTL_DU_PER_TLC_SBLOCK)
    } // for(loop = 0;loop < spb_cnt; loop++)
    // ----- wait all form done -----
    wait_spor_cmd_idle();
    //sys_assert(IS_NCL_IDLE());  // check if ncl commands are done

    ftl_l2p_put_vcnt_buf(vc_dtag_start, vc_dtag_cnt_needed, false);
	sys_free(FAST_DATA, qbt_ncl_cmd);

    if (shr_qbt_prog_err)
    {
        ftl_apl_trace(LOG_ALW, 0x8f79, "nand err, rewrite fqbt.");
        shr_qbt_prog_err = false;
        return false;
    }
    else
    {
        return true;
    }
}



/*!
 * @brief ftl spor function
 * check all blk vac, to decide if need to push to free or user pool
 *
 * @return	not used
 */
init_code void ftl_sort_blk_in_pool(u16 pool_id)
{
    u16    blk, nxt_blk;
    u32    *vc, cnt;
    dtag_t dtag;
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
	FTL_NO_LOG = true;
    if(ftl_spor_blk_pool->blkCnt[pool_id])
    {
        blk = ftl_spor_blk_pool->head[pool_id];
        while(blk != INV_U16)
        {
            nxt_blk = ftl_spor_blk_node[blk].nxt_blk;

			if(spb_info_tbl[blk].pool_id == SPB_POOL_QBT_FREE)
			{
				ftl_apl_trace(LOG_ALW, 0x44bf, "qbt blk %d", blk);
			}
            else if(vc[blk] == 0)
            {
                spb_set_sw_flags_zero(blk);
                spb_set_desc_flags_zero(blk);
                if(ftl_chk_blk_in_pool(SPB_POOL_FREE, blk) == mFALSE)
                {
                    FTL_BlockPopPushList(SPB_POOL_FREE, blk, FTL_SORT_BY_EC);
                }
            }
            else
            {
                if((spb_info_tbl[blk].pool_id != SPB_POOL_USER)&&
                   (ftl_chk_blk_in_pool(SPB_POOL_USER, blk) == mFALSE))
                {
                	if((ftl_build_tbl_type[blk] == FTL_SN_TYPE_USER) || (ftl_build_tbl_type[blk] == FTL_SN_TYPE_GC))
                	{
                		spb_set_desc_flags_zero(blk);
						spb_set_flag(blk, (SPB_DESC_F_BUSY|SPB_DESC_F_CLOSED));
					}
                    FTL_BlockPopPushList(SPB_POOL_USER, blk, FTL_SORT_NONE);
                }
            }

            blk = nxt_blk;
        }
    }

    ftl_l2p_put_vcnt_buf(dtag, cnt, false);
	FTL_NO_LOG = false;
}

/*!
 * @brief ftl spor function
 * if blk last page fill spor dummy,set this blk flag open
 *
 * @return	not used
 */
init_code void ftl_set_spor_dummy_blk_flag(void)
{
	ftl_apl_trace(LOG_ALW, 0xf57a, "[IN] ftl_set_spor_dummy_blk_flag");
	spb_id_t blk_idx;

	u32 index ;
	u8  offest ;
	for(index = 0; index < (shr_nand_info.geo.nr_blocks/32+1);index++)
	{
		u32 idx = ftl_chk_last_wl_bitmap[index];
		for(offest = 0 ;offest < 32;offest++)
		{
			u32 dummy_bit = idx & BIT(offest);
			//ftl_apl_trace(LOG_ALW, 0, "blkid : %d bit:0x%x",(index << 5) + offest,dummy_bit);
			if(dummy_bit)
			{
				blk_idx = (index << 5) + offest;
				ftl_apl_trace(LOG_ALW, 0xebcd, "spor dummy close blk:%d",blk_idx);
				spb_set_flag(blk_idx,SPB_DESC_F_OPEN|SPB_DESC_F_NO_NEED_CLOSE);
			}
		}
	}
	//u8 index   = blk_idx >> 5;
	//u8 offest  = blk_idx & 0x1f;
	//occupied_by(shr_nand_info.geo.nr_blocks, sizeof(u32))
}



/*!
 * @brief ftl spor function
 * check all blk vac, to decide if need to push to free or user pool
 *
 * @return	not used
 */
init_code void ftl_check_all_blk_status(void)
{
    ftl_apl_trace(LOG_ALW, 0xacf5, "[IN] ftl_check_all_blk_status");

    ftl_sort_blk_in_pool(SPB_POOL_USER);
    ftl_sort_blk_in_pool(SPB_POOL_GC);
}

init_code void ftl_free_vac_zero_blk(u16 pool_id, u16 sortRule)
{
    u16    blk, nxt_blk;
    u32    *vc, cnt;
    dtag_t dtag;
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);

    ftl_apl_trace(LOG_ALW, 0xba2c, "[IN] ftl_free_vac_zero_blk");
	FTL_NO_LOG = true;
    if(spb_get_free_cnt(pool_id))
    {
        blk = spb_mgr_get_head(pool_id);
        while(blk != INV_U16)
        {
            nxt_blk = spb_info_tbl[blk].block;
            if(vc[blk] == 0)
            {
                spb_set_sw_flags_zero(blk);
                spb_set_desc_flags_zero(blk);
                if(ftl_chk_blk_in_pool(SPB_POOL_FREE, blk) == mFALSE)
                {
                    FTL_BlockPopPushList(SPB_POOL_FREE, blk, sortRule);
                }
            }
            blk = nxt_blk;
        }
    }
    ftl_l2p_put_vcnt_buf(dtag, cnt, false);
	u16 blk_start = 0;
	u16 blk_end = get_total_spb_cnt();
	u16 blk_idx;
	for (blk_idx = blk_start; blk_idx < blk_end; blk_idx++) {
        pool_id = spb_info_get(blk_idx)->pool_id;
		if(pool_id == SPB_POOL_FREE)
		{
			FTL_BlockPopPushList(SPB_POOL_FREE, blk_idx, FTL_SORT_BY_EC);
		}
	}
	FTL_NO_LOG = false;
}
init_code void user_build_setRO(void)
{
	if((ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P)&&((ftl_spor_info->existPBT||ftl_spor_info->existQBT||ftl_spor_info->qbterr))){
		extern bool is_epm_vac_err;//for smart
		extern read_only_t read_only_flags;
		is_epm_vac_err = true;
		read_only_flags.b.spor_user_build = true;
		#if(degrade_mode == ENABLE)
		if(is_epm_vac_err){
			smart_stat->critical_warning.bits.device_reliability = 1;
			ftl_apl_trace(LOG_ALW, 0x7f18, "Set critical warning bit[2] because SPOR user build");
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
			//extern void cmd_disable_btn();
			//cmd_disable_btn(-1,1);
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_DISABLE_BTN, 0, false);
		}

		#else
		ftl_apl_trace(LOG_ALW, 0x8c8c, "User build set into RO");
		#endif
	}
}

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
/*!
 * @brief ftl spor function
 * copy epm vc to ftl vc table
 *
 * @return	not used
 */
init_code void ftl_restore_vac_from_epm(void)
{
    u32    *vc, cnt;
    dtag_t dtag;

    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

    ftl_apl_trace(LOG_ALW, 0x7b79, "[IN] ftl_restore_vac_from_epm");

    if((epm_ftl_data->epm_sign != FTL_sign) ||
       (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
    {
        ftl_apl_trace(LOG_ALW, 0x31c3, "EPM FTL data incorrect, VAC abort!!");
		extern bool is_epm_vac_err;//for smart
		is_epm_vac_err = true;
		#if (IGNORE_PLP_NOT_DONE == mENABLE)
		is_epm_vac_err = false;
		#endif
		//if(epm_ftl_data->plp_slc_gc_tag == FTL_PLP_SLC_NEED_GC_TAG)

        return;
    }

    // vc table
    dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
    memcpy(vc, epm_ftl_data->epm_vc_table, shr_nand_info.geo.nr_blocks*sizeof(u32));
    ftl_l2p_put_vcnt_buf(dtag, cnt, true);

    #if 0
    // for dbg
    for(spb_id = 0; spb_id <= 10; spb_id++)
    {
            ftl_apl_trace(LOG_ALW, 0x90d1, "[EPM] Spb ID : 0x%x, VAC : 0x%x",\
                          spb_id, epm_ftl_data->epm_vc_table[spb_id]);
    }
    #endif
	//epm_ftl_data->spor_tag = INV_U32;
	//jira2 ,don't clean spor_tag in spor flow to avoid epm update without plp protection ,Jay 23/10/18
}
    // ===== ec table =====
init_code void ftl_restore_ec_from_epm(void)
{
    u16  spb_id;

    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

    ftl_apl_trace(LOG_ALW, 0xb334, "[IN] ftl_restore_ec_from_epm");

	if(epm_ftl_data->epm_sign != FTL_sign)
    {
        ftl_apl_trace(LOG_ALW, 0x5c3c, "EPM FTL data incorrect, EC abort!!");
        return;
    }

    // ===== ec table =====
    for(spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
    {
		if(epm_ftl_data->epm_ec_table[spb_id] > spb_info_tbl[spb_id].erase_cnt)
    	{
			spb_info_tbl[spb_id].erase_cnt = epm_ftl_data->epm_ec_table[spb_id];
		}
    }

}


/*!
 * @brief ftl spor function
 * clear epm vc table
 *
 * @return	not used
 */
init_code void ftl_clear_vac_ec_in_epm(void)
{
    //u16  spb_id;
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

    ftl_apl_trace(LOG_ALW, 0x744a, "[IN] ftl_clear_vac_ec_in_epm");

    memset(epm_ftl_data->epm_vc_table, 0, shr_nand_info.geo.nr_blocks*sizeof(u32));
}
#endif

#if(PLP_SUPPORT == 0) 
ddr_code void ftl_chk_non_plp_init(void)
{
	epm_FTL_t* epm_ftl_data;
	epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(ftl_spor_info->existHostData == mFALSE || epm_ftl_data->host_open_blk[0] == 0 || epm_ftl_data->gc_open_blk[0] == 0)
	{
		for(u8 i_idx = 0;i_idx<2;i_idx++)
		{
			epm_ftl_data->host_open_blk[i_idx] = INV_U16;
			epm_ftl_data->gc_open_blk[i_idx]   = INV_U16;
		}
		for(u8 i=0;i<SPOR_CHK_WL_CNT;i++)
		{
			epm_ftl_data->host_open_wl[i] = INV_U16;
			epm_ftl_data->host_die_bit[i] = 0;
			epm_ftl_data->gc_open_wl[i]   = INV_U16;
			epm_ftl_data->gc_die_bit[i]   = 0;			  
		}
		epm_ftl_data->host_aux_group	  = INV_U16;
		epm_ftl_data->gc_aux_group		  = INV_U16;

		epm_ftl_data->non_plp_gc_tag	  = 0;
		epm_ftl_data->non_plp_last_blk_sn = INV_U32;
		ftl_apl_trace(LOG_ALW, 0x1802, "[IN] ftl_chk_non_plp_init");
		epm_update(FTL_sign,(CPU_ID-1));
	}
}

init_code void ftl_update_non_plp_info(void)
{
	bool hit_open_blk = false;
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(epm_ftl_data->host_open_blk[0] != 0xFFFF) 
   {	
		hit_open_blk = true;
		FTL_BlockPopPushList(SPB_POOL_USER, epm_ftl_data->host_open_blk[0], FTL_SORT_BY_SN); 
   }
   if(epm_ftl_data->gc_open_blk[0] != 0xFFFF)
   {
		hit_open_blk = true;
		FTL_BlockPopPushList(SPB_POOL_USER, epm_ftl_data->gc_open_blk[0], FTL_SORT_BY_SN); 
   }
   if(hit_open_blk)
   {
	   epm_ftl_data->non_plp_gc_tag = FTL_NON_PLP_NEED_GC_TAG;
	   epm_ftl_data->non_plp_last_blk_sn = ftl_spor_info->lastBlkSn;	   
   	ftl_apl_trace(LOG_ALW, 0x8505, "non_plp_last_blk_sn:0x%x",epm_ftl_data->non_plp_last_blk_sn); 
   }
   //force push two open blk in usr pool,avoid open pbt/qbt blk use them 
   //when trigger non-plp gc,gc flow will push them in free pool		
   epm_update(FTL_sign,(CPU_ID-1)); 

}

ddr_code void ftl_show_non_plp_info(void)
{
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    if(epm_ftl_data->host_open_blk[0] != INV_U16)
        { 
            ftl_apl_trace(LOG_ALW, 0x68bc, "host blk %d ",epm_ftl_data->host_open_blk[0]);  
            for(u16 i = 0; i<SPOR_CHK_WL_CNT - 1; i++)  
            {  
                if(epm_ftl_data->host_open_wl[i] != INV_U16)
                { 
                    ftl_apl_trace(LOG_ALW, 0x90d7, "idx:%d host wl :%d die 0x%x",i,epm_ftl_data->host_open_wl[i],epm_ftl_data->host_die_bit[i]);  
                } 
            } 
        } 
        if(epm_ftl_data->gc_open_blk[0] != INV_U16)
        { 
            ftl_apl_trace(LOG_ALW, 0x68bf, "gc blk %d ",epm_ftl_data->gc_open_blk[0]);  
            for(u16 i = 0; i<SPOR_CHK_WL_CNT - 1; i++)  
            {  
                if(epm_ftl_data->gc_open_wl[i] != INV_U16)
                { 
                    ftl_apl_trace(LOG_ALW, 0x6e06, "gc wl :%d die 0x%x",epm_ftl_data->gc_open_wl[i],epm_ftl_data->gc_die_bit[i]);          
                } 
           } 
        }  
}
init_code bool is_need_trigger_non_plp_gc(void)
{
	//bool temp = need_trigger_slc_gc;
	//need_trigger_slc_gc = false;
	return need_trigger_non_plp_gc;
}

#endif


#if (PLP_SUPPORT == 1 && SPOR_DUMP_ERROR_INFO == mENABLE)
ddr_code void ftl_plp_no_done_dbg(void)
{
	if(ftl_spor_info->maxopenBlk == INV_U16)
		return;
	else if(ftl_spor_info->epm_vac_error == mFALSE)
		return;

	spb_id_t spb;
	if(ftl_spor_info->openUSRSn > ftl_spor_info->scan_sn)
	{
		spb = ftl_spor_info->openUSRBlk;
	}
	else
	{
		spb = ftl_spor_info->maxopenBlk;
	}
	u16 wl_idx = 0;
	u16 last_data_wl = FTL_WL_CNT;
	u16 first_data_die;
	u16 first_blank_die;
	while(wl_idx < FTL_WL_CNT)
	{
		ftl_scan_wl_blank_pos( spb, wl_idx);
		first_data_die = find_first_zero_bit(spor_dummy_bit, FTL_DIE_CNT);
		if(first_data_die == FTL_DIE_CNT)
		{
			//whole wl blank
			last_data_wl = wl_idx - 1;
		}
		else
		{
			first_blank_die = find_first_bit(spor_dummy_bit, FTL_DIE_CNT);
			if(first_blank_die != FTL_DIE_CNT)
			{
				ftl_apl_trace(LOG_ERR, 0x9e37,"[ERROR]spb : %d wl :%d bitmap : 0x%x%x (bit1 is blank)",spb,wl_idx,spor_dummy_bit[1],spor_dummy_bit[0]);
			}
		}

		if(last_data_wl != FTL_WL_CNT)
		{
			ftl_apl_trace(LOG_ALW, 0xfe8b,"last data wl : %d last blank wl : %d",last_data_wl,wl_idx);
			break;
		}
		wl_idx++;
		spor_dummy_bit[0] = 0;
		spor_dummy_bit[1] = 0;
		spor_dummy_bit[2] = 0;
		spor_dummy_bit[3] = 0;
	}//check if super wl complete

	//dump last grp P2L
    lda_t *lda_buf_start = NULL; 
    pda_t *pda_buf_start = NULL; 
	if(spb == ftl_spor_info->openUSRBlk)
	{
		ftl_extract_p2l_table(spb, ftl_host_p2l_info.lda_dtag_start, ftl_host_p2l_info.pgsn_dtag_start, ftl_host_p2l_info.pda_dtag_start, 1);   
		lda_buf_start = ftl_host_p2l_info.lda_buf;
		pda_buf_start = ftl_host_p2l_info.pda_buf; 
	}
	else
	{
		ftl_extract_p2l_table(spb, ftl_gc_p2l_info.lda_dtag_start, ftl_gc_p2l_info.pgsn_dtag_start, ftl_gc_p2l_info.pda_dtag_start, 1);
		lda_buf_start = ftl_gc_p2l_info.lda_buf;
		pda_buf_start = ftl_gc_p2l_info.pda_buf; 
	}

	u16 grp_idx = last_data_wl / shr_wl_per_p2l;
	u16 grp_oft;
	//lda_buf_start = (lda_buf_start + grp_idx * LDA_GRP_TABLE_ENTRY);
	pda_buf_start = (pda_buf_start + grp_idx * PDA_GRP_TABLE_ENTRY);

	pda_t pda;
	u32 lda_buf_idx;
	for(grp_oft = 0; grp_oft < PGSN_GRP_TABLE_ENTRY; grp_oft++)
	{
		pda = pda_buf_start[grp_oft];

		if(pda == INV_PDA)
			break;

		lda_buf_idx = pda & FTL_PDA_DU_MSK;
		ftl_apl_trace(LOG_ALW, 0xbea5,"[openGrp] pda:0x%x lda:0x%x 0x%x 0x%x 0x%x",
			pda,lda_buf_start[lda_buf_idx],lda_buf_start[lda_buf_idx+1],lda_buf_start[lda_buf_idx+2],lda_buf_start[lda_buf_idx+3]);

	}

}

ddr_code void ftl_vac_error_dbg(void)
{
	if(ftl_spor_info->epm_vac_error == mTRUE)
		return;
	//check if vac over range

	u32 max_vac_cnt = get_du_cnt_in_native_spb();
	u16 spb_id;
	u32 cnt;
    u32 *vc;
    dtag_t dtag = ftl_l2p_get_vcnt_buf(&cnt, (void **)&vc);
    bool fail_tag = mFALSE;
	for(spb_id=0; spb_id<get_total_spb_cnt(); spb_id++)
	{
		if(vc[spb_id] > max_vac_cnt)
		{
			fail_tag = mTRUE;
			ftl_apl_trace(LOG_ERR, 0x1e2b,"[ERR] spb_id : %d , vac : 0x%x , Type : %d , SN : 0x%x",
				spb_id,vc[spb_id],ftl_build_tbl_type[spb_id],ftl_init_sn_tbl[spb_id]);
		}
	}
	dtag_cont_put(DTAG_T_SRAM, dtag,cnt);  
	if(fail_tag)
	{
		extern int l2p_build_vac(int argc, char *argv[]);
		l2p_build_vac(1,NULL);
	}

}
#endif

/*!
 * @brief ftl spor function
 * ftl_chk_qbt_ect
 * to check if current qbt blk ec cnt is over 5K, if yes replace it
 * @return	spb id
 */
#if 0 
ddr_code u16 ftl_chk_qbt_ec(u16 spb_id)
{
    // if current qbt blk ec over 5K, replace it
    if(spb_info_get(spb_id)->erase_cnt >= 5000)
    {
        u32 bad_blk_threshold = get_interleave() - min_good_pl; //QBT need at least 220 good phy blk
		u16 cand_spb_id;
		u32 defect_cnt;
#ifdef SKIP_MODE
		u8* df_ptr;
		u16 i;
#endif
		u16 loop, pool_cnt;

        ftl_apl_trace(LOG_ALW, 0x56e3, "B-QBT[%d] EC[%d]", spb_id, spb_info_get(spb_id)->erase_cnt);

        // find new QBT blk from free pool
        pool_cnt = spb_get_free_cnt(SPB_POOL_FREE);
		cand_spb_id = spb_mgr_get_head(SPB_POOL_FREE);
        for(loop = 0; loop < pool_cnt ; loop++)
        {
            if(cand_spb_id == INV_U16){
				ftl_apl_trace(LOG_ALW, 0x6396, "No free blk return origin qbt %d",spb_id);
				return spb_id;
			}
            defect_cnt = 0;
#ifdef SKIP_MODE
			df_ptr = ftl_get_spb_defect(cand_spb_id);
			// chk defect cnt
			for(i = 0; i < shr_nand_info.interleave; i++)
			{
				if(ftl_defect_check(df_ptr,i)) {defect_cnt++;}
			}
#endif

            if((defect_cnt < bad_blk_threshold) &&
               (spb_info_get(cand_spb_id)->erase_cnt < 5000))
            {
                // defect & ec is less than threshold, new QBT blk found
                ftl_apl_trace(LOG_ALW, 0x896c, "N-QBT[%d] EC[%d]", cand_spb_id, spb_info_get(cand_spb_id)->erase_cnt);
				u8 grp = 0;
#ifdef SKIP_MODE
				u32 TargBit;
				u8 *ftl_df = &gl_pt_defect_tbl[0];
#endif

				epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

				for(grp=0; grp<2; grp++)
				{
					if(spb_id == QBT_GRP_HANDLE[grp].head_qbt_idx)
					{
						QBT_GRP_HANDLE[grp].head_qbt_idx = cand_spb_id;
						if(grp == 0)
						{
							epm_ftl_data->qbt_grp1_head = cand_spb_id;
						}
						else if(grp == 1)
						{
							epm_ftl_data->qbt_grp2_head = cand_spb_id;
						}
					}
					if(spb_id == QBT_GRP_HANDLE[grp].tail_qbt_idx)
					{
						QBT_GRP_HANDLE[grp].tail_qbt_idx = cand_spb_id;
						if(grp == 0)
						{
							epm_ftl_data->qbt_grp1_tail = cand_spb_id;
						}
						else if(grp == 1)
						{
							epm_ftl_data->qbt_grp2_tail = cand_spb_id;
						}
					}
				}
				ftl_sblk_erase(spb_id); // push to unalloc need to erase
        		FTL_BlockPushList(SPB_POOL_UNALLOC, spb_id, FTL_SORT_NONE);

#ifdef SKIP_MODE
				TargBit = spb_id*shr_nand_info.interleave;
				for(i=0; i<128; i++)
				{
					bitmap_set((u32*)ftl_df, TargBit+i);
				}
#endif

				replace_qbt = true;
				ftl_sblk_erase(cand_spb_id); // replace qbt need to erase
				FTL_BlockPopList(SPB_POOL_FREE,cand_spb_id);
                return cand_spb_id;
            }
            else
            {
                // candidate defect cnt over threshold, push back to free pool
                ftl_apl_trace(LOG_ALW, 0xadc6, "C-QBT[%d], d_cnt[%d], EC[%d]", cand_spb_id, defect_cnt, spb_info_get(cand_spb_id)->erase_cnt);
                //FTL_BlockPushList(SPB_POOL_FREE, cand_spb_id, FTL_SORT_BY_EC);
            }
			cand_spb_id = spb_info_get(cand_spb_id)->block;
        }

        ftl_apl_trace(LOG_ALW, 0x09d6, "No good blk for QBT");
    }

    return spb_id;
}
#endif
/*!
 * @brief ftl spor function
 * save fake qbt
 *
 * @return	not used
 */
ddr_code void ftl_save_fake_qbt(void)
{
    u8  i;
    u16 *spb_list;
    u16 spb_id;

    ftl_apl_trace(LOG_ALW, 0x26af, "[IN] ftl_save_fake_qbt");

#if(SPOR_TIME_COST == mENABLE)
    u64 time_start = get_tsc_64();
#endif

#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
   if((delay_flush_spor_qbt == false)/*&&(ftl_spor_info->ftl_1bit_fail_need_fake_qbt == false)*/){ 
		gFtlMgr.SerialNumber = (ftl_spor_info->lastBlkSn == INV_U32) ? 0:ftl_spor_info->lastBlkSn; 
		gFtlMgr.last_host_blk = INV_U16; 
		gFtlMgr.last_gc_blk	 = INV_U16; 
		delay_flush_spor_qbt =true; 
		#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
		if(ftl_get_trigger_slc_gc())
		{
			ftl_set_trigger_slc_gc(false);
			shr_slc_flush_state = SLC_FLOW_FQBT_FLUSH;
		}
		#endif 
		ftl_apl_trace(LOG_ALW, 0x0f04, "spor delay flush qbt, gFtlMgr.SerialNumber :0x%x",gFtlMgr.SerialNumber); 
	} 
	return;
#endif

   {
    u8 cur_loop = shr_qbt_loop;
    spb_list = sys_malloc(SLOW_DATA, sizeof(u16)*FTL_QBT_BLOCK_CNT);

rewrite:
	/*Push SPB from SPB_POOL_FREE to SPB_POOL_QBT_FREE*/
	for (i = 0; i < FTL_QBT_BLOCK_CNT; i++)
	{
		spb_id = FTL_BlockPopHead(SPB_POOL_FREE);
		sys_assert(spb_id != INV_U16);
		FTL_BlockPushList(SPB_POOL_QBT_FREE, spb_id, FTL_SORT_NONE);
	}

    if(spb_get_free_cnt(SPB_POOL_QBT_ALLOC) == FTL_QBT_BLOCK_CNT)
	{
		spb_PurgePool2Free(SPB_POOL_QBT_ALLOC, FTL_SORT_BY_EC); 
	}

	//Erase QBT
    for(i = 0; i < FTL_QBT_BLOCK_CNT; i++)
    {
        spb_list[i] = FTL_BlockPopHead(SPB_POOL_QBT_FREE);
		sys_assert(spb_list[i] != INV_U16);
        //spb_list[i] = ftl_chk_qbt_ec(spb_list[i]);
        gFtlMgr.SerialNumber++;
        spb_set_sn(spb_list[i], gFtlMgr.SerialNumber);
        ftl_apl_trace(LOG_ALW, 0xe357, "QBT block : %u, SN : 0x%x", spb_list[i], spb_get_sn(spb_list[i]));
		ftl_sblk_erase(spb_list[i]);
        FTL_BlockPushList(SPB_POOL_QBT_ALLOC, spb_list[i], FTL_SORT_NONE);
    }

    gFtlMgr.LastQbtSN = gFtlMgr.SerialNumber;

#if(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0x0184, "erase blk time cost : %d us", time_elapsed_in_us(time_start));
#endif


    FTL_CopyFtlBlkDataToBuffer(0);
#if(SPOR_TIME_COST == mENABLE)
    u64 time_flush = get_tsc_64();
#endif
    ftl_flush_qbt(FTL_QBT_BLOCK_CNT, spb_list);
    if (ftl_flush_qbt(FTL_QBT_BLOCK_CNT, spb_list) == false)
    {   
        shr_qbt_loop = cur_loop;
        goto rewrite;
    }

#if(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0x6842, "flush qbt time cost : %d us", time_elapsed_in_us(time_flush));
#endif

	if(replace_qbt == true)
	{
		frb_log_type_update(FRB_TYPE_DEFECT);
		replace_qbt = false;
	}

    sys_free(SLOW_DATA, spb_list);
    if(ftl_spor_info->ftl_1bit_fail_need_fake_qbt == true){        
        ftl_spor_info->ftl_1bit_fail_need_fake_qbt = false;
    }

#if(SPOR_TIME_COST == mENABLE)
    ftl_apl_trace(LOG_ALW, 0x848d, "Function time cost : %d us", time_elapsed_in_us(time_start));
#endif
}

}

#endif

/*! @} */
