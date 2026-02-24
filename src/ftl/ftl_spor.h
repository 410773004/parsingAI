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
/*! \file ftl_spor.h
 * @brief define ftl l2p manager, define l2p memory usage and l2p engine operations
 *
 * \addtogroup ftl
 * \defgroup ftl_l2p
 * \ingroup ftl
 * @{
 *
 */
#pragma once

#include "types.h"
#include "dtag.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl.h"
#include "mpc.h"
#include "spb_mgr.h"
#include "spb_pool.h"
#include "fc_export.h"
//#include "ftl_ns.h"

/*!
 *====================
 *@brief FTL parameter
 *====================
 */

#define V_MODE   1
#define H_MODE   2

#if (SPOR_CMD_EXP_BAND == mENABLE)
#define FTL_SPOR_NCL_CMD_MAX  32
#else
#define FTL_SPOR_NCL_CMD_MAX  16  // 8CH * 2
#endif

#define FTL_DU_PER_SPAGE        (get_du_cnt_in_native_spb()/get_page_per_block())
#define FTL_DU_PER_SWL          (FTL_DU_PER_SPAGE*get_nr_bits_per_cell())
#define FTL_DU_PER_GRP          (FTL_DU_PER_SWL*shr_wl_per_p2l)
#define FTL_DU_PER_TLC_SBLOCK   (get_du_cnt_in_native_spb())
#define FTL_DU_PER_LAYER        (FTL_DU_PER_SWL*4)
#define FTL_DU_PER_WL           (DU_CNT_PER_PAGE*get_nr_bits_per_cell())
#define FTL_WL_PER_LAYER        4

#define FTL_PG_IN_BLK_CNT       (get_page_per_block())
#define FTL_PG_IN_WL_CNT        (get_nr_bits_per_cell())
#define FTL_PN_CNT              (get_mp())
#define FTL_CH_CNT              (get_ch())
#define FTL_WL_CNT              (get_page_per_block()/get_nr_bits_per_cell())
#define FTL_DIE_CNT             (get_lun_per_spb())
#define FTL_TOT_PN_CNT          (FTL_DIE_CNT*FTL_PN_CNT)

#define FTL_PN_SHIFT            ctz(FTL_PN_CNT)
#define FTL_PDA_DU_MSK          ((1<<shr_nand_info.pda_block_shift)-1)

//---------------------------------------------------------------------
// V-Mode program
//---------------------------------------------------------------------
#define FTL_NCL_WRITE_DIE_DU_CNT    (get_mp()*get_nr_bits_per_cell()*DU_CNT_PER_PAGE)
#define FTL_NCL_WRITE_PN_DU_CNT     (FTL_NCL_WRITE_DIE_DU_CNT/get_mp())

//---------------------------------------------------------------------
#define CPDA_CH_MULTIPLIER      (get_mp()*get_nr_bits_per_cell()*DU_CNT_PER_PAGE)
#define CPDA_WL_MULTIPLIER      (get_ch()*get_ce()*get_lun()*get_mp()*DU_CNT_PER_PAGE)
#define CPDA_LUN_MULTIPLIER     (get_mp()*DU_CNT_PER_PAGE)
#define CPDA_SBLK_DU_MASK       (FTL_DU_PER_TLC_SBLOCK-1)

#define LDA_GRP_TABLE_DU_CNT   3
#define LDA_GRP_TABLE_SIZE     (DTAG_SZE*LDA_GRP_TABLE_DU_CNT)
#define LDA_FULL_TABLE_SIZE    (LDA_GRP_TABLE_SIZE*shr_p2l_grp_cnt)
#define LDA_FULL_TABLE_DU_CNT  (LDA_GRP_TABLE_DU_CNT*shr_p2l_grp_cnt)
#define LDA_GRP_TABLE_ENTRY    (LDA_GRP_TABLE_SIZE/sizeof(lda_t))
#define LDA_FULL_TABLE_ENTRY   (LDA_FULL_TABLE_SIZE/sizeof(lda_t))

#define PDA_GRP_TABLE_DU_CNT   1
#define PDA_GRP_TABLE_SIZE     (DTAG_SZE*PDA_GRP_TABLE_DU_CNT)
#define PDA_FULL_TABLE_SIZE    (PDA_GRP_TABLE_SIZE*shr_p2l_grp_cnt)
#define PDA_FULL_TABLE_DU_CNT  (PDA_GRP_TABLE_DU_CNT*shr_p2l_grp_cnt)

#define PDA_GRP_TABLE_ENTRY    (PDA_GRP_TABLE_SIZE/sizeof(pda_t))
#define PDA_FULL_TABLE_ENTRY   (PDA_FULL_TABLE_SIZE/sizeof(pda_t))

#define PGSN_GRP_TABLE_DU_CNT  2
#define PGSN_GRP_TABLE_SIZE    (DTAG_SZE*PGSN_GRP_TABLE_DU_CNT)
#define PGSN_FULL_TABLE_SIZE   (PGSN_GRP_TABLE_SIZE*shr_p2l_grp_cnt)
#define PGSN_FULL_TABLE_DU_CNT (PGSN_GRP_TABLE_DU_CNT*shr_p2l_grp_cnt)

#define PGSN_GRP_TABLE_ENTRY   (PGSN_GRP_TABLE_SIZE/sizeof(u64))
#define PGSN_FULL_TABLE_ENTRY  (PGSN_FULL_TABLE_SIZE/sizeof(u64))



#define FTL_GET_DIE_NUM(lun,ce,ch)  (((lun) * FTL_DIE_CNT / get_lun()) + ((ce) << get_ch_shift()) + (ch))

extern bool replace_qbt;

/*!
 *==========================
 *@brief FTL enum definition
 *==========================
 */

enum
{
    FTL_SCAN_NO_HOST_DATA = 0,
    FTL_SCAN_UPDATE_P2L_ONLY,
    FTL_SCAN_UPDATE_ALL,
    FTL_SCAN_MAX
};


enum
{
    FTL_BLK_TYPE_HOST = 0,
    FTL_BLK_TYPE_GC,
    FTL_BLK_TYPE_FTL,
    FTL_BLK_TYPE_MAX
};



typedef enum
{
    FTL_SN_TYPE_BLANK=0,
    FTL_SN_TYPE_USER,
    FTL_SN_TYPE_GC,
    FTL_SN_TYPE_PBT,
    FTL_SN_TYPE_QBT,
    FTL_SN_TYPE_FQBT,
    FTL_SN_TYPE_OPEN_USER, // 6
    FTL_SN_TYPE_OPEN_GC,   // 7
    FTL_SN_TYPE_OPEN_QBT,
    FTL_SN_TYPE_OPEN_PBT,
    FTL_SN_TYPE_OPEN_UNKOWN,
    FTL_SN_TYPE_PLP_USER,  // 11
    FTL_SN_TYPE_PLP_GC,    // 12
    FTL_SN_TYPE_MCRC_ERR,
    FTL_SN_TYPE_SCAN_MISS,
    FTL_SN_TYPE_BAD,
    FTL_SN_TYPE_MAX   // 16
} ftl_block_sn_type_t;


/*!
 *
 *@brief FTL build L2P mode
 *
 */
enum
{
    FTL_QBT_BUILD_L2P,
    FTL_PBT_BUILD_L2P,
    FTL_USR_BUILD_L2P,
    FTL_BUILD_L2P_MAX
};

/*!
 *
 *@brief FTL build Blist mode
 *
 */
enum
{
    FTL_QBT_LOAD_BLIST,
    FTL_USR_LOAD_BLIST,
    FTL_LOAD_BLKIST_FAIL,
    FTL_LOAD_BLIST_MAX
};



/*!
 *
 *@brief FTL struct definition
 *
 */
struct ftl_scan_info_t
{
    u32             sn;
    u16             blk_idx;
    u8              loop;
};


struct ftl_blk_pool_t
{
    u16             head[SPB_POOL_MAX];
    u16             tail[SPB_POOL_MAX];
    u16             blkCnt[SPB_POOL_MAX];
};


struct ftl_blk_node_t
{
    u16             pre_blk;
    u16             nxt_blk;
    u8              pool_id;
};

typedef struct _ftl_epm_inv_info_t
{
    u16             inv_blk;
    u16             inv_cnt;
    u32             inv_pda[2048];
}ftl_epm_inv_info_t;

struct ftl_spor_info_t
{
    u32  lastBlkSn;
    u16  lastBlk;

    u32  maxUSRSn;
    u16  maxUSRBlk;
    u32  lastUSRSn;
    u16  lastUSRBlk;
    u32  secUSRSn;
    u16  secUSRBlk;

    u32  maxGCSn;
    u16  maxGCBlk;
    u32  lastGCSn;
    u16  lastGCBlk;
    u32  secGCSn;
    u16  secGCBlk;

    u16  maxHostDataBlk;
    u32  maxHostDataBlkSn;

    u32  maxPBTSn;
    u16  maxPBTBlk;
    u32  secPBTSn;
    u16  secPBTBlk;
    u32  minPBTSn;
    u16  minPBTBlk;

    u32  maxQBTSn;
    u16  maxQBTBlk;

    u32  maxcloseBlkSn;
    u16  maxcloseBlk;

    u32  openUSRSn;
    u16  openUSRBlk;

    u32  openGCSn;
    u16  openGCBlk;

    u32  openPBTSn;
    u16  openPBTBlk;

    u32  openQBTSn;
    u16  openQBTBlk;

    u32  maxopenBlkSn;
    u16  maxopenBlk;

    u16  plpGCBlk;
    u16  plpUSRBlk;

    pda_t  plpGCEndPda;
    pda_t  plpUSREndPda;

    u16  plpGCEndGrp;
    u16  plpUSREndGrp;

    pda_t  openGCEndPda;
    pda_t  openUSREndPda;

    u16  openGCEndGrp;
    u16  openUSREndGrp;

	volatile pda_t	  plp_force_p2l_pda;
    volatile pda_t    blist_pda;
    volatile pda_t    p2l_pda;
    volatile pda_t    blank_pda;
    volatile pda_t    last_dummy_pda;
    volatile pda_t    first_dummy_pda;

    volatile pda_t    last_grp_lda_buf_pda;
    volatile pda_t    last_grp_pgsn_buf_pda;
    volatile pda_t    last_grp_pda_buf_pda;

    u32  scan_sn;
	u32  scan_sn_host;
    u32  scan_sn_gc;
    u8   build_l2P_mode;
    u8   read_data_fail;
    u8   read_data_blank;
    u8   blist_data_error;
    u8   l2p_data_error;
    u8   p2l_data_error;
    u8   existHostData;
    u8   existPBT;
    u8   existQBT;
    u8   scan_QBT_loop;
    u8   scan_PBT_loop;
    u8   epm_vac_error;
    u8   glist_eh_not_done;
	u8   load_blist_mode;
	u8   ftl_1bit_fail_need_fake_qbt;		
	u8   qbterr;
	u16  secondDataBlk;
    u32  secondDataBlkSn;
}; // 174 byte


struct ftl_spor_p2l_t {
	u32   pgsn_dtag_start;
	u32   lda_dtag_start;
    u32   pda_dtag_start;
	u64   *pgsn_buf;         // SN of each P2L table. [FTL_WL_CNT]
    lda_t *lda_buf;          // store all LDA
    pda_t *pda_buf;          // store all PDA
};


struct ftl_spor_p2l_pos_list_t {
	u32   lda_pda;
	u32   pgsn_pda;
    u32   pda_pda;
};


typedef struct _ftl_spor_raid_info_t {
		u8 raid_id[XLC]; 	///< raid id of each page
		u8 bank_id[XLC]; 	///< bank id of each page
}ftl_spor_raid_info_t;

/*!
 *=========================
 *@brief FTL spor functions
 *=========================
 */

/*!
 * @brief init/alocate memmory for spor variables
 *
 * @param   not used
 *
 * @return	not used
 */
void ftl_spor_resource_init(void);

/*!
 * @brief free memmory for spor variables
 *
 * @param   not used
 *
 * @return	not used
 */
void ftl_spor_resource_recycle(void);

/*!
 * @brief scan all block's aux data and sorting
 *
 * @param   not used
 *
 * @return	not used
 */
void ftl_scan_all_blk_info(void);

void ftl_scan_info_handle(void);

void ftl_last_pbt_seg_handle(void); //add by Jay

/*!
 * @brief ftl spor function
 * P2L build table
 *
 * @return	not used
 */
void ftl_p2l_build_table(void);

/*!
 * @brief ftl spor function
 * Fill dummy layer
 *
 * @return	not used
 */
//void ftl_fill_dummy_layer(void);

/*!
 * @brief ftl spor function
 * Save Fake QBT
 *
 * @return	not used
 */
void ftl_save_fake_qbt(void);

bool ftl_read_misc_data_from_host(u16 spb_id);

u32 ftl_scan_block_list_pos(u16 spb_id);

#if(SPOR_QBT_MAX_ONLY == mENABLE)
u8 ftl_is_qbt_blk_max(void);
#endif

void ftl_spor_pbt_push_free(void);

u8 ftl_spor_build_l2p_mode(void);

u8 ftl_spor_load_blist_mode(u16 *blk_id, u8 fail_cnt);

u16 ftl_spor_get_head_from_pool(u8 pool);

u16 ftl_spor_get_tail_from_pool(u8 pool);

void ftl_reset_pbt_seg_info(void);

spb_id_t ftl_get_last_pbt_blk(void);

u16 ftl_get_last_pbt_seg(void);

u16 ftl_spor_get_next_blk(u16 blk);

u16 ftl_spor_get_pre_blk(u16 blk);

u32 ftl_spor_get_blk_sn(u16 blk);


u8 ftl_is_host_data_exist(void);

u8 ftl_need_p2l_rebuild(void);

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
u8 ftl_chk_slc_buffer_done(void);
bool ftl_get_trigger_slc_gc(void);
void ftl_set_trigger_slc_gc(bool type);
void ftl_calculate_slc_build_range(void);
void ftl_aux_build_slc_l2p(void);
void ftl_aux_build_slc_l2p_done(struct ncl_cmd_t *ncl_cmd);
void ftl_qbt_slc_mode_dbg(void);
void ftl_slc_build_l2p(void);
#endif

#if (PLP_SUPPORT == 0) 	
bool is_need_trigger_non_plp_gc(void);
void ftl_chk_non_plp_init(void);
void ftl_update_non_plp_info(void);
void ftl_show_non_plp_info(void);
#endif


void ftl_set_blist_data_error_flag(u8 flag);

u8 ftl_get_blist_data_error_flag(void);

void ftl_set_l2p_data_error_flag(u8 flag);

u8 ftl_get_l2p_data_error_flag(void);

void ftl_set_read_data_fail_flag(u8 flag);

u8 ftl_get_read_data_fail_flag(void);
void ftl_set_qbt_fail_flag(void);

void ftl_set_1bit_data_error_flag(u8 flag);

u8 ftl_get_1bit_data_error_flag(void);

void ftl_spor_spb_desc_sync(void);

void ftl_spor_blk_sn_sync(void);

void ftl_l2p_print(void);

void ftl_vc_tbl_print(void);
void ftl_check_all_blk_status(void);

void ftl_free_vac_zero_blk(u16 pool_id, u16 sortRule);

#if (SPOR_VAC_CMP == mENABLE)
void ftl_vac_buffer_copy(void);

void ftl_vac_compare(void);
#endif
void user_build_setRO(void);

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
void ftl_restore_vac_from_epm(void);
void ftl_restore_ec_from_epm(void);
void ftl_clear_vac_ec_in_epm(void);
#endif

u8 ftl_is_epm_vac_error(void);
bool is_need_l2p_build_vac(void);
void ftl_l2p_build_vac(void);
u8 ftl_is_spor_user_build(void);

bool is_skip_spor_build(void);
bool get_skip_spor_build(void);
void spor_clean_tag(void);

bool ftl_spor_error_save_log(void);

void ftl_sblk_erase(u16 spb_id);

void ftl_set_spor_dummy_blk_flag(void);
#ifdef ERRHANDLE_GLIST
void ftl_check_glist_last_blk(void);
#endif

pda_t ftl_set_blk2pda(pda_t pda, u16 spb_id);
pda_t ftl_set_pg2pda(pda_t pda, u16 pg);

#if (PLP_SUPPORT == 1 && SPOR_DUMP_ERROR_INFO == mENABLE)
void ftl_plp_no_done_dbg(void);
void ftl_vac_error_dbg(void);
#endif

/*! @} */
