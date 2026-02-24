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
/*! \file frb_log.c
 * @brief define operation of ftl root block
 *
 * \addtogroup frb
 *
 * @{
 * define FTL root block interface function and initialization
 */
#include "ftlprecomp.h"
#include "bitops.h"
#include "frb_log.h"
#include "sync_ncl_helper.h"
#include "ncl_cmd.h"
#include "log.h"
#include "console.h"
#include "misc.h"
#include "defect_mgr.h"
#include "ftl_flash_geo.h"
#include "ncl_helper.h"
#include "ftl_meta.h"
#include "ipc_api.h"
#include "idx_meta.h"
#include "l2cache.h"
#if FRB_remap_enable
#include "srb.h"
#include "epm.h"
#include "nand.h"
#endif
#include "fc_export.h"

#ifdef TCG_NAND_BACKUP
#include "../tcg/tcg_nf_mid.h"
extern tcg_mid_info_t* tcg_mid_info;
#endif

#define __FILEID__ frb
#include "trace.h"

#define FRB_LOG_SIG		'brF'//'LbrF' AlanCC for wunc	///< ftl root block signature
#define FRB_HEADER_SIG		'DaeH'	///< frb header page signature
#define FRB_GDEF_SIG		'FedG'	///< frb grown defect signature

#define FRB_LOG_VER		4	///< frb log version

#if defined(SHRINK_CAP)
#define FTL_BUILD		0x02000002	///< ftl build number
#else
#define FTL_BUILD		0x02100005	///< ftl build number
#endif

#if defined(PURE_XLC)
#undef FTL_BUILD
#if PURE_XLC == 1
#define FTL_BUILD		0x12345678	///< XLC
#elif PURE_XLC == 3
#define FTL_BUILD		0x87654321	///< SLC
#else
#error "defined error"
#endif
#endif

#define GRWN_DEF_VERSION	0x01		///< 8-bit version
#define GROWN_DEF_MAX_NUM	64		///< max grown defect number

/*!
 * @brief grown defect table definition
 */
typedef struct _frb_grwn_def_t {
	u32 sig;			///< signature
	u8 version;			///< version of grown defect
	u8 count;			///< count of grown defect in the table
	u16 history;			///< historical counter
	u8 type[GROWN_DEF_MAX_NUM];	///< grown defect info
	pda_t pda[GROWN_DEF_MAX_NUM];	///< grown defect pda

	u32 max_user_spare_cnt;		///< max user spare_cnt
} frb_grwn_def_t;

/*!
 * @brief frb table context definition
 */
typedef struct _frb_hdr_tbl_t {
	u32 FTL_Current_Capacity;	///< Current capacity, todo review this
	union{
		 struct{
                u32 QBT_TAG : 1;// Curry QBT Tag
		 }b;
		 u32   all;
	} FTL_Tag_in_FRB;
	u32 QBT_SN;
	u16 QBT_blk_idx;
	u8 customer_info[110];		///< reserved for customer to add new fields
} frb_hdr_tbl_t;
BUILD_BUG_ON(sizeof(frb_hdr_tbl_t) != 124);

/*!
 * @brief frb log flags definition
 */
#define FRB_LOG_F_ERASE	 	0x00000001	///< block need to erase before use
#define FRB_LOG_F_BUSY	 	0x00000002	///< FRB update now busy
#define FRB_LOG_F_SWITCH_TRIG	0x00000004	///< FRB switch block trigger

/*!
 * @brief ftl boot option definition
 */
#define FRB_OPT_F_VIRGIN	0x00000001	///< force virgin boot
#define FRB_OPT_F_DROP_GDEF	0x00000002	///< drop grown defect
#define FRB_OPT_F_DROP_P2GL 0x00000004	///< drop P2 list and Glist (grown defect), FET, RelsP2AndGL // DBG, PgFalVry

typedef struct _frb_op {
	u16 type;		///< FRB_TYPE_xxxx
	u16 pg;			///< now operating page
} frb_op_t;

typedef struct _frb_pg_idx {
	u16 page;		///< physical page index
	u8  pg_cnt;		///< number of page
	u8  flags;		///< FRB_LOG_IDX_F_xxx
	u32 size;		///< data structure size in frb log pages
	void *mem;		///< operating memory location
} frb_log_type_t;

/*!
 * @brief pb log context definition
 */
typedef struct _frb_log_t {
	u32 sig;				///< header page signature
	u32 version;				///< frb log version
	u32 ftl_build;				///< FTL build number
	u32 ftl_boot_opt;			///< FRB_OPT_F_xxx
	spb_id_t spb;				///< spb id of pb belongs to
	pb_id_t cur_blk;			///< current using block
	//pb_id_t next_blk;			///< next block to be used
	u8 test_bit;
	u32 flush_id;				///< current flush serial number
	u8 flags;				///< FRB_LOG_F_xxx
	u8 pending;				///< FRB_PEND_xxx
	u8 rsvd[2];				///< reserved field, default is 0
	u32 rsvd0;				///< reserved field, default is 0;
	u16 nr_page_per_blk;			///< number of page in log block
	u16 next_page;				///< next page to be program
	frb_log_type_t frb_logs[NR_FRB_TYPE];	///< frb log type
} frb_log_t;
BUILD_BUG_ON(sizeof(frb_log_t) > 132);	///< lock frb_log_t, can't add new fields

/*!
 * @brief frb log context definition, will be flushed to nand
 */
typedef struct _frb_header_t {
	frb_log_t log;		///< frb log info
	frb_hdr_tbl_t tbl;	///< ptr to frb table
	// add new big context here
	frb_grwn_def_t grwn_def;///< grown defect information
} frb_header_t;

typedef struct _frb_du_rsv_meta_t {
	u32 seed;		///< seed
	u32 rsvd[7];		///< reserved
} frb_du_rsv_meta_t;

typedef struct _frb_du0_meta_t {
	u32 seed;		///< seed
	u32 signature;		///< signature of log
	u32 flush_id;		///< flush id
	u8 type;		///< FRB_TYPE_xxxx
	u8 pg_idx;		///< pg_idx
	u8 rsvd[18];		///< reserved
} frb_du0_meta_t;

typedef struct _frb_meta_t {
	frb_du0_meta_t		meta0;	///< meta of du0
	frb_du_rsv_meta_t	meta1;	///< rsv
	frb_du_rsv_meta_t	meta2;	///< rsv
	frb_du_rsv_meta_t	meta3;	///< rsv
} frb_meta_t;
BUILD_BUG_ON(sizeof(frb_meta_t) != PAGE_META_SZ);

/*!
 * @brief frb log io resource definition
 */
typedef struct _frb_log_io_res_t {
	bm_pl_t *bm_pl_list;
	struct info_param_t *info_list;
	pda_t *pda;
	struct ncl_cmd_t *ncl_cmd;
	frb_meta_t *meta[MAX_IDX_META];
	frb_op_t op;
	void *ctx;
	completion_t cmpl;			///< only one resource, use it carefully
	u32 meta_idx[MAX_IDX_META];		///< meta index of meta buffer
} frb_log_io_res_t;
#ifdef ERRHANDLE_GLIST
share_data_zi volatile bool profail; // TBD_EH, Temp use for ASSERT Prog failed after mark defect.
#endif
fast_data_zi static frb_log_io_res_t _frb_log_io_res;	///< ftl root block io resource

slow_data static frb_header_t _frb_log = {
		{INV_SPB_ID, 0, 0, 0, 0, 0, 0},
};	///< frb header page data structure

slow_data bool QBT_TAG = false;
slow_data u16  QBT_BLK_IDX;

share_data_zi volatile u8 wb_frb_lock_done;		//20210325-Eddie
share_data volatile bool _fg_warm_boot;
/*!
 * @brief get next program page pda of given pblog and increase page ptr
 *
 * @param log		ptr to target pblog
 * @param pda_list	ptr to output pda list
 *
 * @return		None
 */
static void pblog_page_allocate(frb_log_t *log, pda_t *pda_list);

/*!
 * @brief erase one single pb of FRB
 *
 * @param pb		target pb in FRB
 * @param cmpl		callback function
 *
 * @return		None
 */
static void frb_pb_erase(pb_id_t pb, void (*cmpl)(struct ncl_cmd_t *));

/*!
 * @brief frb flush done function
 *
 * @param ncl_cmd	pointer to completed NCL command
 *
 * @return		None
 */
static void frb_log_flush_done(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief Flush FRB page to NAND
 *
 * @param type		flush type
 * @param pg_idx	page index in frb log type
 *
 * @return		None
 */
static void _frb_log_flush(u32 type, u32 pg_idx, u32 sync);


/*!
 * @brief FRB flush operation
 *
 * @param type		flush type
 * @param pg_idx	page index in frb log type
 *
 * @return		None
 */
static void frb_log_flush(u32 type, u32 pg_idx, u32 sync);

/*!
 * @brief get frb flush id of given pda
 *
 * @param pda		target pda to read
 * @param fid		return flush id
 *
 * @return		return ncl status of read
 */
static nal_status_t get_frb_pb_fid(pda_t pda, u32 *fid);

/*!
 * @brief check current block of FRB
 *
 * This function will get fid from current block and next block.
 * Larger fid belongs to newer block. May switch block to right order.
 *
 * @return		true for current block found, false for fail
 */
static bool chk_frb_curr_blk(void);


/*!
 * @brief find correct next page and update output array
 *
 * This function loop over current block to get last valid page and fid
 * Will read latest log and update output array
 *
 * @return		return false if anything wrong
 */
static bool frb_log_recovery(void);

/*!
 * @brief scan frb log block for all frb log type
 *
 * @param last_fid	last valid fid from frb log block
 *
 * @return 		next writable page index
 */
static u32 frb_log_scan(u32 *last_fid);

/*!
 * @brief rebuild frb log
 *
 * Check current block id. If current block can be identified,
 * find next program page idx and update output array
 *
 * @return		FRB valid return true
 */
static bool frb_log_reconstruction(void);

/*!
 * @brief initialize frb io resource
 *
 * @return		None
 */
static void frb_log_io_res_init(frb_log_io_res_t *io_res);

/*!
 * @brief frb flush complete function
 *
 * @param ncl_cmd	NCL command of frb flush command
 *
 * @return		None
 */
static void frb_log_flush_cmpl(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief dump function for frb
 *
 * @param frb_header	frb pointer
 *
 * @return		none
 */
static void frb_dump(frb_header_t *frb_header);

fast_code void pblog_page_allocate(frb_log_t *log, pda_t *pda_list)
{
	pda_list[0] = blk_page_make_pda(log->spb, log->cur_blk,
			log->next_page);

	log->next_page++;
}

fast_code u32 epm_rda_2_pb(rda_t rda)
{
	/*row_t row = rda.row;
	u16 ch = rda.ch;
	u16 dev = rda.dev;
	u16 lun = row >> shr_nand_info.row_lun_shift;
	row = (row & ((1U << shr_nand_info.row_lun_shift) - 1));

	row = (row & ((1U << shr_nand_info.row_block_shift) - 1));
	u16 pln = row >> shr_nand_info.row_pl_shift;

	u32 dpos = pln+ (ch << FTL_CH_SHF) + (lun << FTL_LUN_SHF) + (dev << FTL_CE_SHF);

	return dpos;*/
	return 0;
}

fast_code void frb_pb_erase(pb_id_t pb, void (*cmpl)(struct ncl_cmd_t *))
{
	u32 flags;

	flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	if (cmpl == NULL) {
		flags |= NCL_CMD_SYNC_FLAG;
	}

	_frb_log_io_res.pda[0] = blk_page_make_pda(_frb_log.log.spb, pb, 0);

	ncl_cmd_setup_helper(_frb_log_io_res.ncl_cmd, flags,
			NCL_CMD_OP_ERASE, NULL, cmpl, NULL);

	ncl_cmd_setup_addr_common(
			&_frb_log_io_res.ncl_cmd->addr_param.common_param,
			&_frb_log_io_res.pda[0], 1, _frb_log_io_res.info_list);

	ncl_cmd_submit(_frb_log_io_res.ncl_cmd);

#if FRB_remap_enable
	if (_frb_log_io_res.ncl_cmd->status != 0)
	{
		ftl_frb_trace(LOG_ALW, 0x34e9, "FRB erase fail pda[0x%x] status=0x%x", _frb_log_io_res.pda[0], _frb_log_io_res.ncl_cmd->status);

		extern epm_info_t* shr_epm_info;
		epm_remap_tbl_t *epm_remap_tbl;
		epm_remap_tbl = (epm_remap_tbl_t *)ddtag2mem(shr_epm_info->epm_remap_tbl_info_ddtag);

		srb_t *srb = (srb_t *) SRAM_BASE;
		u32 blk0 = epm_rda_2_pb(srb->ftlb_pri_pos); //129 : ch0 lun1 pl1 ce0
		u32 blk1 = epm_rda_2_pb(srb->ftlb_sec_pos);	//145 : ch0 lun1 pl1 ce1

		ftl_frb_trace(LOG_ALW, 0xcf83, "FRB remap blk0 %d, blk1 %d, pb %d, sour0 0x%x, sour1 0x%x",
					  blk0, blk1, pb, epm_remap_tbl->frb_remap_source[0],
					  epm_remap_tbl->frb_remap_source[1]);

		if ( (pb == blk0) && (epm_remap_tbl->frb_remap_source[0] != 0xFFFFFFFF) ) //129 : ch0 lun1 pl1 ce0
		{
			_frb_log_io_res.pda[0] = epm_remap_tbl->frb_remap_source[0];
		}
		else if((pb == blk1) && (epm_remap_tbl->frb_remap_source[1] != 0xFFFFFFFF))  //145 : ch0 lun1 pl1 ce1
		{
			_frb_log_io_res.pda[0] = epm_remap_tbl->frb_remap_source[1];
		}
		else
		{
		        ftl_frb_trace(LOG_ALW, 0xadd6, "FRB No remap blk use");
				return;
		}

			ncl_cmd_setup_helper(_frb_log_io_res.ncl_cmd, flags,
									NCL_CMD_OP_ERASE, NULL, cmpl, NULL);

			ncl_cmd_setup_addr_common(
				&_frb_log_io_res.ncl_cmd->addr_param.common_param,
				&_frb_log_io_res.pda[0], 1, _frb_log_io_res.info_list);

			ncl_cmd_submit(_frb_log_io_res.ncl_cmd);

			if (_frb_log_io_res.ncl_cmd->status != 0)
			{
				ftl_frb_trace(LOG_ALW, 0xa5cd, " FRB_remap blk erase fail pda=0x%x status=0x%x", _frb_log_io_res.pda[0], _frb_log_io_res.ncl_cmd->status);
				if(pb == blk0)
				{
				epm_remap_tbl->frb_remap_source[0] = 0xFFFFFFFF;
				}
				else if (pb == blk1)
				{
				epm_remap_tbl->frb_remap_source[1] = 0xFFFFFFFF;
				}
			}
			else
			{
				if(pb == blk0)
				{
					pb = (epm_remap_tbl->frb_remap_source[0] >> 2); // 161
					epm_remap_tbl->frb_remap[0] = epm_remap_tbl->frb_remap_source[0];
					epm_remap_tbl->frb_remap_source[0] = 0xFFFFFFFF;
			        ftl_frb_trace(LOG_ALW, 0xc7a7, "FRB_remap blk success pb = 0x%x remap0 = 0x%x", pb, epm_remap_tbl->frb_remap[0] );
				}
				else if (pb == blk1)
				{
					pb = (epm_remap_tbl->frb_remap_source[1] >> 2);  // 177
					epm_remap_tbl->frb_remap[1] = epm_remap_tbl->frb_remap_source[1];
					epm_remap_tbl->frb_remap_source[1] = 0xFFFFFFFF;
			        ftl_frb_trace(LOG_ALW, 0x1422, "FRB_remap blk success pb = 0x%x remap1 = 0x%x", pb, epm_remap_tbl->frb_remap[1] );
				}
			}

			epm_remap_tbl_flush(&epm_remap_tbl->remap_last_pda);
		ftl_frb_trace(LOG_ALW, 0x21d4, "FRB frb_pb_erase done");
	}
#endif

	ftl_frb_trace(LOG_ALW, 0xe173, "frb erase pda %x id %d cmpl %x", _frb_log_io_res.pda[0], pb, cmpl);
}

fast_code void frb_log_flush_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	/* todo: error handling */
#if FRB_remap_enable	
	if ((_frb_log_io_res.ncl_cmd->status != 0) && (ncl_cmd->op_code == NCL_CMD_OP_ERASE))
	{
		ftl_frb_trace(LOG_ALW, 0x28ea, "FRB erase fail pda[0x%x] status=0x%x", _frb_log_io_res.pda[0], _frb_log_io_res.ncl_cmd->status);

		extern epm_info_t* shr_epm_info;
		epm_remap_tbl_t *epm_remap_tbl;		
		epm_remap_tbl = (epm_remap_tbl_t *)ddtag2mem(shr_epm_info->epm_remap_tbl_info_ddtag);		

		u32 flags;
		flags = NCL_CMD_SLC_PB_TYPE_FLAG;
		flags |= NCL_CMD_SYNC_FLAG;

		u16 phyblk = ncl_cmd->addr_param.common_param.pda_list[0]>>2 ;	
		srb_t *srb = (srb_t *) SRAM_BASE;	

		u32 blk0 = epm_rda_2_pb(srb->ftlb_pri_pos);
		u32 blk1 = epm_rda_2_pb(srb->ftlb_sec_pos);

		ftl_frb_trace(LOG_ALW, 0xc262, "FRB remap blk0 %d, blk1 %d, phyblk %d, sour0 0x%x, sour1 0x%x",
                                  blk0, blk1, phyblk, epm_remap_tbl->frb_remap_source[0], 
                                  epm_remap_tbl->frb_remap_source[1]);
		
		if ( (phyblk == blk0) && (epm_remap_tbl->frb_remap_source[0] != 0xFFFFFFFF) ) //129 : ch0 lun1 pl1 ce0
		{
			_frb_log_io_res.pda[0] = epm_remap_tbl->frb_remap_source[0];
		}
		else if((phyblk == blk1) && (epm_remap_tbl->frb_remap_source[1] != 0xFFFFFFFF))  //145 : ch0 lun1 pl1 ce1
		{
			_frb_log_io_res.pda[0] = epm_remap_tbl->frb_remap_source[1];		
		}
		else
		{
		     ftl_frb_trace(LOG_ALW, 0x7c25, "FRB No remap blk use");		
		     goto out;
		}	

		ncl_cmd_setup_helper(_frb_log_io_res.ncl_cmd, flags,
							 NCL_CMD_OP_ERASE, NULL, NULL, NULL);

		ncl_cmd_setup_addr_common(
			&_frb_log_io_res.ncl_cmd->addr_param.common_param,
			&_frb_log_io_res.pda[0], 1, _frb_log_io_res.info_list);

		ncl_cmd_submit(_frb_log_io_res.ncl_cmd);					

		if (_frb_log_io_res.ncl_cmd->status != 0)
		{
			ftl_frb_trace(LOG_ALW, 0x1361, "FRB_remap blk erase fail pda=0x%x status=0x%x", _frb_log_io_res.pda[0], _frb_log_io_res.ncl_cmd->status);
			if(phyblk == blk0)  //129 : ch0 lun1 pl1 ce0 
			{	
				epm_remap_tbl->frb_remap_source[0] = 0xFFFFFFFF;
			}
			else if (phyblk == blk1)  //145 : ch0 lun1 pl1 ce1
			{
				epm_remap_tbl->frb_remap_source[1] = 0xFFFFFFFF;				
			}	
		}
		else
		{
			if(phyblk == blk0)  //129 : ch0 lun1 pl1 ce0 
			{				
				_frb_log.log.next_blk = (epm_remap_tbl->frb_remap_source[0] >> 2); // 161 //pda2plane(remap_pda)+ (pda2ch(remap_pda) << FTL_CH_SHF) + (pda2lun(remap_pda) << FTL_LUN_SHF) + (pda2ce(remap_pda) << FTL_CE_SHF);
				epm_remap_tbl->frb_remap[0] = epm_remap_tbl->frb_remap_source[0];
				epm_remap_tbl->frb_remap_source[0] = 0xFFFFFFFF;
			    ftl_frb_trace(LOG_ALW, 0x53c5, "FRB_remap blk success nextblk=0x%x remap0=0x%x", _frb_log.log.next_blk, epm_remap_tbl->frb_remap[0] );
			}
			else if (phyblk == blk1)  //145 : ch0 lun1 pl1 ce1
			{
				_frb_log.log.next_blk = (epm_remap_tbl->frb_remap_source[1] >> 2);  // 177 //pda2plane(remap_pda)+ (pda2ch(remap_pda) << FTL_CH_SHF) + (pda2lun(remap_pda) << FTL_LUN_SHF) + (pda2ce(remap_pda) << FTL_CE_SHF);
				epm_remap_tbl->frb_remap[1] = epm_remap_tbl->frb_remap_source[1];
				epm_remap_tbl->frb_remap_source[1] = 0xFFFFFFFF;	
			    ftl_frb_trace(LOG_ALW, 0xb43f, "FRB_remap blk success nextblk = 0x%x remap1 = 0x%x", _frb_log.log.next_blk, epm_remap_tbl->frb_remap[1] );
			}
		}

		epm_remap_tbl_flush(&epm_remap_tbl->remap_last_pda);
		ftl_frb_trace(LOG_ALW, 0x6475, "FRB frb_log_flush_cmpl done");	
	}	
#endif

//out:
	_frb_log.log.flags &= ~ FRB_LOG_F_BUSY;

	if (_frb_log.log.flags & FRB_LOG_F_SWITCH_TRIG) {
		_frb_log.log.pending |= FRB_PEND_ALL;
		_frb_log.log.flags &= ~FRB_LOG_F_SWITCH_TRIG;
	}
	if (_frb_log.log.pending) {
		u32 type = find_first_bit(&_frb_log.log.pending, NR_FRB_TYPE);

		frb_log_flush(type, 0, 0);
		return;
	}

	#if(WA_FW_UPDATE == ENABLE)
		if (_fg_warm_boot == true)
			wb_frb_lock_done = 1;
	#endif		

	if (_frb_log_io_res.cmpl) {
		completion_t cmpl = _frb_log_io_res.cmpl;
		void *ctx = _frb_log_io_res.ctx;

		_frb_log_io_res.cmpl = NULL;
		_frb_log_io_res.ctx = NULL;
		cmpl(ctx);
	}
}


fast_code void frb_log_flush_done(struct ncl_cmd_t *ncl_cmd)
{
	u32 pg_idx = _frb_log_io_res.op.pg + 1;
	u32 type = _frb_log_io_res.op.type;
	frb_log_type_t *log_type = &_frb_log.log.frb_logs[type];

	if (!(log_type->flags & FRB_LOG_IDX_F_IO_BUF)) {
		dtag_t dtag;

		dtag.dtag = _frb_log_io_res.bm_pl_list[0].pl.dtag;
		dtag_put(DTAG_T_SRAM, dtag);
	}

	if (_frb_log.log.flags & FRB_LOG_F_SWITCH_TRIG) {
		frb_pb_erase(_frb_log.log.cur_blk, frb_log_flush_cmpl);
	} else if (pg_idx != _frb_log.log.frb_logs[type].pg_cnt) {
		_frb_log_flush(type, pg_idx, 0);
	} else {
		ftl_frb_trace(LOG_ERR, 0x6b57, "[FTL]frb_log_flush_done pg %d\n", pg_idx);

		frb_log_flush_cmpl(NULL);	
#ifdef ERRHANDLE_GLIST
		if (profail)    // TBD_EH, Temp use for ASSERT Prog failed after mark defect.
		{
			sys_assert(0);//TBD if rewrite timeout fix will del
		}
#endif
	}
}


fast_code void frb_log_flush(u32 type, u32 pg_idx, u32 sync)
{
	if (_frb_log.log.flags & FRB_LOG_F_BUSY) {
		ftl_frb_trace(LOG_INFO, 0xc13e, "frb log flush pend %d with %x",
				type, _frb_log.log.pending);
		_frb_log.log.pending |= (1 << type);
		return;
	}

	_frb_log.log.flags |= FRB_LOG_F_BUSY;
	_frb_log.log.pending &= ~(1 << type);

	_frb_log_flush(type, pg_idx, sync);
}

fast_code void _frb_log_flush(u32 type, u32 pg_idx, u32 sync)
{
	dtag_t dtag;
	void *mem;
	u8 i;
	//pb_id_t temp;
	struct ncl_cmd_t *ncl_cmd;
	u32 meta_type = SRAM_IDX_META;
	frb_meta_t *meta;

	if (_frb_log.log.frb_logs[type].flags & FRB_LOG_IDX_F_IO_BUF) {
		void *buf = _frb_log.log.frb_logs[type].mem;

		buf = ptr_inc(buf, NAND_PAGE_SIZE * pg_idx);
		dtag = mem2dtag(buf);
		if (dtag.b.in_ddr)
			meta_type = DDR_IDX_META;

		for (i = 0; i < NR_FRB_LOG_DU_CNT; i++) {
			ftl_wr_bm_pl_setup(&_frb_log_io_res.bm_pl_list[i], dtag.dtag + i);
			ftl_prea_idx_meta_setup(&_frb_log_io_res.bm_pl_list[i],
					_frb_log_io_res.meta_idx[meta_type] + i);
		}
	} else {
		sys_assert(_frb_log.log.frb_logs[type].size <= DTAG_SZE);
		dtag = dtag_get_urgt(DTAG_T_SRAM, &mem);
		sys_assert(dtag.dtag != _inv_dtag.dtag);

		void *buf = _frb_log.log.frb_logs[type].mem;
		u32 size = _frb_log.log.frb_logs[type].size;

		memcpy(mem, buf, size);
		if ((DTAG_SZE - size) && type == FRB_TYPE_HEADER)
			memset(ptr_inc(mem, size), 0xff, DTAG_SZE - size);

		for (i = 0; i < NR_FRB_LOG_DU_CNT; i++) {
			ftl_wr_bm_pl_setup(&_frb_log_io_res.bm_pl_list[i], dtag.dtag);
			ftl_prea_idx_meta_setup(&_frb_log_io_res.bm_pl_list[i],
					_frb_log_io_res.meta_idx[meta_type] + i);
		}
	}

	if (_frb_log.log.next_page == (_frb_log.log.nr_page_per_blk - 1)) {
		_frb_log.log.flags |= FRB_LOG_F_SWITCH_TRIG;
	}

	if (_frb_log.log.next_page == _frb_log.log.nr_page_per_blk) {
		/*
		temp = _frb_log.log.next_blk;
		_frb_log.log.next_blk = _frb_log.log.cur_blk;
		_frb_log.log.cur_blk = temp;
		*/
		_frb_log.log.next_page = 0;

		ftl_frb_trace(LOG_INFO, 0x30f6, "frb log switch cur %d",
				_frb_log.log.cur_blk);
	}

	/* check if rebuild set erase before use */
	if (_frb_log.log.flags & FRB_LOG_F_ERASE) {
		frb_pb_erase(_frb_log.log.cur_blk, NULL);
		_frb_log.log.flags &= ~FRB_LOG_F_ERASE;
	}

	if (pg_idx == 0)
		_frb_log.log.frb_logs[type].page = _frb_log.log.next_page;

	pblog_page_allocate(&_frb_log.log, &_frb_log_io_res.pda[0]);

	_frb_log_io_res.op.type = type;
	_frb_log_io_res.op.pg = pg_idx;
	meta = _frb_log_io_res.meta[meta_type];
	meta->meta0.seed = meta_seed_setup(_frb_log_io_res.pda[0]);
	meta->meta0.signature = FRB_LOG_SIG;
	meta->meta0.flush_id = _frb_log.log.flush_id;
	meta->meta0.type = type;
	meta->meta0.pg_idx = pg_idx;

	meta->meta1.seed = meta_seed_setup(_frb_log_io_res.pda[0] + 1);
	meta->meta2.seed = meta_seed_setup(_frb_log_io_res.pda[0] + 2);
	meta->meta3.seed = meta_seed_setup(_frb_log_io_res.pda[0] + 3);

#if defined(ENABLE_L2CACHE)
	if (meta_type == DDR_IDX_META) {
		u64 addr = dtag.b.dtag << DTAG_SHF;
		l2cache_flush(addr, DTAG_SZE * NR_FRB_LOG_DU_CNT);
		l2cache_mem_flush((void *)meta, sizeof(*meta));
	}
#endif

	memset(_frb_log_io_res.info_list, 0, sizeof(struct info_param_t) * NR_FRB_LOG_DU_CNT);

	ncl_cmd = _frb_log_io_res.ncl_cmd;

	ncl_cmd_setup_addr_common(&ncl_cmd->addr_param.common_param,
		&_frb_log_io_res.pda[0], 1, _frb_log_io_res.info_list);

	u32 flag = (sync) ? NCL_CMD_SYNC_FLAG : 0;

	ncl_cmd_prog_setup_helper(ncl_cmd, NCL_CMD_PROGRAM_TABLE,
			(NCL_CMD_SLC_PB_TYPE_FLAG | flag),
			_frb_log_io_res.bm_pl_list,
			frb_log_flush_done, &_frb_log_io_res);

	_frb_log.log.flush_id++;

	ncl_cmd_submit(ncl_cmd);

	ftl_frb_trace(LOG_ALW, 0xd45c, "frb log flush op %d pg %d at %x, id %d",
			type, pg_idx, _frb_log_io_res.pda[0],
			_frb_log.log.flush_id - 1);
}


init_code nal_status_t get_frb_pb_fid(pda_t pda, u32 *fid)
{
	dtag_t dtag;
	void *mem;
	log_level_t old = (log_level_t) ipc_api_log_level_chg(LOG_ERR);
    bm_pl_t *bm_pl_list;
	pda_t *pda_list;
	struct info_param_t *info_list;
	ncl_page_res_t *page_res;
	//ftl_frb_trace(LOG_ERR, 0, "get_frb_pb_fid");
	page_res = share_malloc(sizeof(*page_res));
	bm_pl_list = page_res->bm_pl;
	pda_list = page_res->pda;
	info_list = page_res->info;

	dtag = dtag_get(DTAG_T_SRAM, &mem);
	ftl_bm_pl_setup(&bm_pl_list[0], dtag);
	ftl_prea_idx_meta_setup(&bm_pl_list[0], _frb_log_io_res.meta_idx[SRAM_IDX_META]);

	info_list[0].status = ficu_err_good;
	info_list[0].xlc.slc_idx = 0;
	info_list[0].pb_type = NAL_PB_TYPE_SLC;
    pda_list[0] = pda;
    ncl_read_dus(pda_list, 1, bm_pl_list,
			info_list, NAL_PB_TYPE_SLC, false, false);

	dtag_put(DTAG_T_SRAM, dtag);
	ipc_api_log_level_chg(old);
    share_free(page_res);

	ftl_frb_trace(LOG_DEBUG, 0x8095, "frb %x, st %d sig %x t %d", pda,
			info_list[0].status,
			_frb_log_io_res.meta[SRAM_IDX_META]->meta0.signature,
			_frb_log_io_res.meta[SRAM_IDX_META]->meta0.type);

	if (ficu_du_data_good(info_list[0].status))
	{
		if (_frb_log_io_res.meta[SRAM_IDX_META]->meta0.signature == FRB_LOG_SIG) {
			*fid = _frb_log_io_res.meta[SRAM_IDX_META]->meta0.flush_id;
			return info_list[0].status;
		}
	}

	*fid = 0;
	//ftl_frb_trace(LOG_ERR, 0, "fid 0\n");
	return info_list[0].status;
}

init_code bool chk_frb_curr_blk(void)
{
	pda_t pda;
	u32 fid[1];
	//pb_id_t temp;

	/* get fid from current block and next block */
	pda = blk_page_make_pda(_frb_log.log.spb, _frb_log.log.cur_blk, 0);
	get_frb_pb_fid(pda, &fid[0]);

	//pda = blk_page_make_pda(_frb_log.log.spb, _frb_log.log.next_blk, 0);
	//get_frb_pb_fid(pda, &fid[1]);

	/* error when both empty, initial flush id and return false. when mr ready, should never happen */
	if (!fid[0]) {

		//sys_assert(0); //when mr ready, the valid frb should always be found

		_frb_log.log.flags |= FRB_LOG_F_ERASE;
		_frb_log.log.flush_id = 1;

		ftl_frb_trace(LOG_ALW, 0xc223, "frb empty cur %d",
				_frb_log.log.cur_blk);
		return false;
	}

	/* switch block in right order */
	/*
	if (fid[0] > fid[1]) {
		_frb_log.log.flush_id = fid[0];
	}
	else {
		_frb_log.log.flush_id = fid[1];
		temp = _frb_log.log.next_blk;
		_frb_log.log.next_blk = _frb_log.log.cur_blk;
		_frb_log.log.cur_blk = temp;
	}
	*/
	ftl_frb_trace(LOG_ALW, 0x0bca, "frb chk cur %d",
			_frb_log.log.cur_blk);

	/* return true for current block found */
	return true;
}


init_code bool frb_log_recovery(void)
{
	u32 last_fid;
	frb_header_t *frb_header;
	dtag_t dtag;
	u32 dtag_cnt;

	/* scan whole block for all type */
	_frb_log.log.next_page = frb_log_scan(&last_fid);
	_frb_log.log.flush_id = last_fid + 1;

	ftl_frb_trace(LOG_INFO, 0xe5b2, "frb head pg %d, next pg %d, fid %d",
			_frb_log.log.frb_logs[FRB_TYPE_HEADER].page,
			_frb_log.log.next_page, _frb_log.log.flush_id);

	/* current block full, raise flag to notify erase before use next cur_blk */
	if (_frb_log.log.next_page == _frb_log.log.nr_page_per_blk) {
		_frb_log.log.flags |= FRB_LOG_F_ERASE;
		_frb_log.log.flags |= FRB_LOG_F_SWITCH_TRIG;
	}

	/* read out frb log and update output array */
	dtag_cnt = _frb_log.log.frb_logs[FRB_TYPE_HEADER].pg_cnt * NR_FRB_LOG_DU_CNT;
	dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
	frb_header = dtag2mem(dtag);

	frb_log_type_load(FRB_TYPE_HEADER, frb_header);
	//ftl_frb_trace(LOG_ALW, 0, "frb test bit:%d", frb_header->log.test_bit);

	if (ftl_boot_lvl == FTL_BOOT_VIRGIN_DROP_FRB) {
		ftl_frb_trace(LOG_ALW, 0x3f1d, "force virgin boot to drop FRB");
		goto erase;
	}

	/* TODO: error handling */
	if (frb_header->log.sig != FRB_HEADER_SIG) {
		ftl_frb_trace(LOG_ERR, 0x76ee, "dtag %x, sig %x, fid %x, type %d",
				dtag.dtag, _frb_log_io_res.meta[SRAM_IDX_META]->meta0.signature,
					_frb_log_io_res.meta[SRAM_IDX_META]->meta0.flush_id,
					_frb_log_io_res.meta[SRAM_IDX_META]->meta0.type);
		goto erase;
	}

	if (frb_header->log.version != FRB_LOG_VER) {
		ftl_frb_trace(LOG_ERR, 0x3dc6, "ver: %d -> %d, plz upgrade",
				frb_header->log.version, FRB_LOG_VER);
erase:
		frb_pb_erase(_frb_log.log.cur_blk, NULL);
		//frb_pb_erase(_frb_log.log.next_blk, NULL);
		_frb_log.log.flush_id = 1;
		_frb_log.log.next_page = 0;
		ftl_boot_lvl = max(ftl_boot_lvl, FTL_BOOT_VIRGIN_DROP_FRB);
		ftl_frb_trace(LOG_ALW, 0xf66c, "Force erase pb %d",
				_frb_log.log.cur_blk);
		dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);
		return false;
	}

	if (frb_header->log.ftl_build != FTL_BUILD) {
		ftl_frb_trace(LOG_ALW, 0x70ce, "FTL build mismatch %x <-> %x, virgin boot",
				frb_header->log.ftl_build, FTL_BUILD);

		ftl_boot_lvl = max(ftl_boot_lvl, FTL_BOOT_VIRGIN_DROP_ALL_PBLK);
	}

	_frb_log.log.ftl_boot_opt = frb_header->log.ftl_boot_opt;

	memcpy(&_frb_log.tbl, &frb_header->tbl, sizeof(frb_hdr_tbl_t));
	memcpy(&_frb_log.grwn_def, &frb_header->grwn_def, sizeof(frb_grwn_def_t));

	if (frb_header->log.ftl_build != FTL_BUILD) {
		_frb_log.grwn_def.max_user_spare_cnt = 0xFFFFFFFF;
		_frb_log.tbl.FTL_Current_Capacity = ~0;
	}

	if (frb_header->grwn_def.sig != FRB_GDEF_SIG) {
		ftl_frb_trace(LOG_ALW, 0xa48a, "grown defect info restart");
		reset_grwn_defect();
	}

	// support FRB upgrade from no this field
	if (_frb_log.tbl.FTL_Current_Capacity == 0xFFFFFFFF) {
		ftl_frb_trace(LOG_ALW, 0x652a, "frb tbl upgraded Cap");
		_frb_log.tbl.FTL_Current_Capacity = ftl_flash_geo.disk_size_in_gb;
	}

	dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);
	frb_dump(&_frb_log);
	return true;
}


init_code u32 frb_log_scan(u32 *last_fid)
{
	u32 i;
	u32 _last_fid = 0;
	u8 rewrite = 0;

	for (i = 0; i < _frb_log.log.nr_page_per_blk; i++) {
		pda_t pda;
		u32 fid;
		u32 type;
		u32 pg_idx;
		nal_status_t sts;

		pda = blk_page_make_pda(_frb_log.log.spb, _frb_log.log.cur_blk, i);
		sts = get_frb_pb_fid(pda, &fid);
		type = _frb_log_io_res.meta[SRAM_IDX_META]->meta0.type;
		pg_idx = _frb_log_io_res.meta[SRAM_IDX_META]->meta0.pg_idx;

		if (fid) {
			if (pg_idx == 0) {
				_frb_log.log.frb_logs[type].page = i;
			}
			_last_fid = fid;
		}

		if((sts == ficu_err_du_uc)&& (rewrite == 0))
		{
			ftl_frb_trace(LOG_ALW, 0x4d16, " Attention : frb fid %d, pg_idx %d, type %d ",fid, pg_idx, type);
			rewrite = 1;
		}

		//if ((sts == ficu_err_du_erased) || (sts == ficu_err_du_uc) || (sts == ficu_err_1bit_retry_err)) {
		if (sts == ficu_err_du_erased) {
			/* currently, no error handling considered, 0 implies last valid over */
			break;
		}
	}
	*last_fid = _last_fid;

	if(rewrite == 1)
	{
		i = _frb_log.log.nr_page_per_blk ;	  //rewrite FRB
	}

	return i;
}

init_code bool frb_log_reconstruction(void)
{
	if (chk_frb_curr_blk()) {
		bool ret;

		_frb_log.log.flags &= ~FRB_LOG_F_ERASE;
		ret = frb_log_recovery();
		if (ret == false)
			return false;

		return true;
	}
	return false;
}

init_code void frb_log_io_res_init(frb_log_io_res_t *io_res)
{
	ncl_page_res_t *res;

	io_res->meta[SRAM_IDX_META] = idx_meta_allocate(DU_CNT_PER_PAGE, SRAM_IDX_META, &io_res->meta_idx[SRAM_IDX_META]);
	io_res->meta[DDR_IDX_META] = idx_meta_allocate(DU_CNT_PER_PAGE, DDR_IDX_META, &io_res->meta_idx[DDR_IDX_META]);

	res = share_malloc(sizeof(*res));
	io_res->ctx = NULL;
	io_res->cmpl = NULL;
	io_res->bm_pl_list = res->bm_pl;
	io_res->info_list = res->info;
	io_res->pda = res->pda;
	io_res->ncl_cmd = &res->ncl_cmd;
	memset(io_res->info_list, 0, sizeof(struct info_param_t) * NR_FRB_LOG_DU_CNT);

#ifdef LJ1_WUNC
	u8 i;
	for (i = 0; i < MAX_IDX_META; i ++) {
	memset(io_res->meta[i], 0, sizeof(frb_meta_t) * NR_FRB_LOG_DU_CNT);
	}
#endif
}

init_code void frb_log_res_init(void)
{
	memset(&_frb_log.tbl, 0xFF, sizeof(frb_hdr_tbl_t));

	frb_log_io_res_init(&_frb_log_io_res);
}

init_code bool frb_log_start(void)
{
	bool ret = frb_log_reconstruction();

	if (_frb_log.log.ftl_boot_opt & FRB_OPT_F_VIRGIN) {
		ftl_frb_trace(LOG_ALW, 0xe2a8, "ftl force virgin boot");
		ftl_boot_lvl = max(ftl_boot_lvl, FTL_BOOT_VIRGIN_DROP_SYS_LOG);

		_frb_log.log.ftl_boot_opt &= ~FRB_OPT_F_VIRGIN;
	}

	if (_frb_log.log.ftl_boot_opt & FRB_OPT_F_DROP_GDEF) {
		ftl_frb_trace(LOG_ALW, 0x834e, "ftl drop grown defect");
		ret = false;
		_frb_log.log.ftl_boot_opt &= ~FRB_OPT_F_DROP_GDEF;
	}

	#ifdef ERRHANDLE_VERIFY	// FET, RelsP2AndGL
	extern u8 FTL_scandefect;
	if (_frb_log.log.ftl_boot_opt & FRB_OPT_F_DROP_P2GL) {
		ftl_frb_trace(LOG_ALW, 0xfcea, "ftl drop P2GL");
		ret = false;
		FTL_scandefect = true;	// Force skip load P1/ P2 in SRB.
		_frb_log.log.ftl_boot_opt &= ~FRB_OPT_F_DROP_P2GL;
	}
	#endif

	return ret;
}

fast_code u32 get_grwn_defect_info(u32 ofst)
{
	if (ofst < _frb_log.grwn_def.count)
		return nal_pda_get_block_id(_frb_log.grwn_def.pda[ofst]);
	else
		return ~0;
}

init_code void reset_grwn_defect(void)
{
	u32 i;

	_frb_log.grwn_def.sig = FRB_GDEF_SIG;
	_frb_log.grwn_def.version = GRWN_DEF_VERSION;
	_frb_log.grwn_def.count = 0;
	_frb_log.grwn_def.history = 0;

	for (i = 0; i < GROWN_DEF_MAX_NUM; i ++) {
		_frb_log.grwn_def.type[i] = 0xFF;
		_frb_log.grwn_def.pda[i] = 0xFFFFFFFF;
	}
	_frb_log.grwn_def.max_user_spare_cnt = 0xFFFFFFFF;
}

init_code void frb_log_init(u32 blk_id0, u32 spb_id)
{
	if (sizeof(frb_header_t) > DTAG_SZE)
		panic("frb header oversize");

	_frb_log.log.sig = FRB_HEADER_SIG;
	_frb_log.log.version = FRB_LOG_VER;
	_frb_log.log.ftl_build = FTL_BUILD;
	_frb_log.log.nr_page_per_blk = get_slc_page_per_block();
	_frb_log.log.flags = FRB_LOG_F_ERASE;
	_frb_log.log.spb = spb_id;
	_frb_log.log.cur_blk = (pb_id_t) blk_id0;
	//_frb_log.log.next_blk = (pb_id_t) blk_id1;
	//_frb_log.log.test_bit = 99;
	_frb_log.log.next_page = 0;
	_frb_log.log.flush_id = 1;
	_frb_log.log.rsvd[0] = _frb_log.log.rsvd[1] = 0;
	_frb_log.log.rsvd0 = 0;
	_frb_log.log.pending = 0;

#if FRB_remap_enable
	extern epm_info_t* shr_epm_info;
	epm_remap_tbl_t *epm_remap_tbl;

	epm_remap_tbl = (epm_remap_tbl_t *)ddtag2mem(shr_epm_info->epm_remap_tbl_info_ddtag);

	if( epm_remap_tbl->frb_remap[0] != 0xFFFFFFFF ) 
	{
		_frb_log.log.cur_blk = (pb_id_t)(epm_remap_tbl->frb_remap[0]>>2);
		ftl_frb_trace(LOG_ALW, 0x1d88, "cur_blk %d , frb_remap[0] 0x%x", _frb_log.log.cur_blk, epm_remap_tbl->frb_remap[0]);
	}	

	if(epm_remap_tbl->frb_remap[1] != 0xFFFFFFFF) 
	{
		_frb_log.log.next_blk = (pb_id_t)(epm_remap_tbl->frb_remap[1]>>2);
		ftl_frb_trace(LOG_ALW, 0x7a46, "next_blk %d , frb_remap[1] 0x%x", _frb_log.log.next_blk, epm_remap_tbl->frb_remap[1]);		
	}	
#endif	

	reset_grwn_defect();

	//sys_assert(_frb_log.log.cur_blk != _frb_log.log.next_blk);

	memset(&_frb_log.tbl, 0xff, sizeof(_frb_log.tbl));
	memset(&_frb_log.log.frb_logs, 0x00, sizeof(frb_log_type_t) * NR_FRB_TYPE);

	//need initial value in case frb invalid
	_frb_log.tbl.FTL_Current_Capacity = get_disk_size_in_gb();
	_frb_log.tbl.FTL_Tag_in_FRB.all = 0;// 20200918 Curry initial frb ftl flag

	frb_log_type_init(FRB_TYPE_HEADER, sizeof(_frb_log), &_frb_log, 0);
}


fast_code void frb_log_type_load(u32 type, void *buf)
{
	dtag_t dtag;
	pda_t pda;
	void *mem;
	u32 loop;
	u32 pg_cnt;
	u32 pg;
	u32 size;

	pg_cnt = _frb_log.log.frb_logs[type].pg_cnt;
	size = _frb_log.log.frb_logs[type].size;
	pg = _frb_log.log.frb_logs[type].page;
	if (buf == NULL) {
		buf = _frb_log.log.frb_logs[type].mem;
	}

	dtag = dtag_cont_get(DTAG_T_SRAM, NR_FRB_LOG_DU_CNT);
	mem = dtag2mem(dtag);

	for (loop = 0; loop < pg_cnt; loop++) {
		nal_status_t sts;
		u32 cp_sz = min(NAND_PAGE_SIZE, size);

		pda = blk_page_make_pda(_frb_log.log.spb,
				_frb_log.log.cur_blk, pg);

		sts = ncl_read_one_page(pda, mem, _frb_log_io_res.meta_idx[SRAM_IDX_META]);
		if(sts != 0)
		{
			ftl_frb_trace(LOG_ALW, 0xd655, "[frb_log] Read one page sts = %d, type %d, pda 0x%x", sts, type, pda);
		}

        sys_assert(ficu_du_data_good(sts));
		if (_frb_log_io_res.meta[SRAM_IDX_META]->meta0.type != type) {
			goto end;
		}
		sys_assert(_frb_log_io_res.meta[SRAM_IDX_META]->meta0.pg_idx == loop);

		memcpy(buf, mem, cp_sz);
		buf = (void*) ptr_inc(buf, cp_sz);
		size -= cp_sz;
		pg++;
	}
end:
	dtag_cont_put(DTAG_T_SRAM, dtag, NR_FRB_LOG_DU_CNT);
}

init_code void frb_log_type_init(u32 type, u32 size, void *mem, u8 flags)
{
	u32 pg_cnt = occupied_by(size, NAND_PAGE_SIZE);

	_frb_log.log.frb_logs[type].mem = mem;
	_frb_log.log.frb_logs[type].size = size;
	_frb_log.log.frb_logs[type].pg_cnt = pg_cnt;
	_frb_log.log.frb_logs[type].flags |= flags;
}

fast_code void frb_log_update_grwn_def(u8 type, pda_t pda)
{
	u8 prt = (_frb_log.grwn_def.history & (GROWN_DEF_MAX_NUM - 1));

	if (_frb_log.grwn_def.count == GROWN_DEF_MAX_NUM) {
		ftl_frb_trace(LOG_ERR, 0x081f, "grown defect number max, kick %x %x",
				_frb_log.grwn_def.pda[prt], _frb_log.grwn_def.type[prt]);
	} else {
		_frb_log.grwn_def.count++;
	}

	_frb_log.grwn_def.type[prt] = type;
	_frb_log.grwn_def.pda[prt] = pda;
	_frb_log.grwn_def.history++;
	frb_log_flush(FRB_TYPE_HEADER, 0, 0);
}

init_code void frb_log_set_spare_cnt(u32 spare)
{
	if (_frb_log.grwn_def.max_user_spare_cnt == 0xFFFFFFFF) {
		_frb_log.grwn_def.max_user_spare_cnt = spare;
		ftl_frb_trace(LOG_ALW, 0xbe32, "set spare %d", _frb_log.grwn_def.max_user_spare_cnt);
	}
}

fast_code u32 frb_log_get_spare_cnt(void)
{
	return _frb_log.grwn_def.max_user_spare_cnt;
}

fast_code void frb_log_set_cb(void *ctx, completion_t cmpl)
{
	_frb_log_io_res.ctx = ctx;
	_frb_log_io_res.cmpl = cmpl;
}

#ifdef TCG_NAND_BACKUP
slow_code void tcg_def_map_updt(void)
{
	if(tcg_mid_info != NULL)
	{
		u32 i = 0;
		u32 spb_cnt = get_total_spb_cnt();
		u32 interleave = get_interleave();
		u8 *defect_map_for_tcg = get_defect_map();
		u32 tcg_cnt = TCG_SPB_ALLOCED * interleave / 8;

		defect_map_for_tcg += ((spb_cnt - TCG_SPB_ALLOCED) * interleave / 8);

		//memset(tcg_mid_info->defect_map, 0xff, sizeof(tcg_mid_info->defect_map));
		memcpy32((void *)tcg_mid_info->defect_map, (const void *)defect_map_for_tcg, tcg_cnt);

		//set erased_blk 0 if def_map is 1 (bad blk)
		for(i=0; i<(sizeof(tcg_mid_info->defect_map)/sizeof(u32)); i++)
		{
			tcg_mid_info->blk_erased[i] &= (~tcg_mid_info->defect_map[i]);
		}
	}
}
#endif

fast_code void frb_log_type_update(u32 type)
{
	frb_log_flush(type, 0, 0);
#ifdef TCG_NAND_BACKUP
	tcg_def_map_updt();
#endif
}

fast_code void frb_dump(frb_header_t *frb_header)
{
	ftl_frb_trace(LOG_ALW, 0x12f1, "cur %d fid %d f %x",
			frb_header->log.cur_blk,
			frb_header->log.flush_id, frb_header->log.flags);

	ftl_frb_trace(LOG_ALW, 0xf6cf, "frb ver %d, build %x",
			frb_header->log.version,
			frb_header->log.ftl_build);

	ftl_frb_trace(LOG_ALW, 0x9231, "next pg %d/%d", frb_header->log.next_page,
			frb_header->log.nr_page_per_blk);

	ftl_frb_trace(LOG_ALW, 0xff4c, "grown defect version %d count %d history %d",
				frb_header->grwn_def.version,
				frb_header->grwn_def.count,
				frb_header->grwn_def.history);

	ftl_frb_trace(LOG_ALW, 0xdf13, "ftl boot option %x", _frb_log.log.ftl_boot_opt);
}
fast_code void frb_erase_callby_vu(void)
{
	frb_dump(&_frb_log);
	if ((_frb_log.log.cur_blk == 0)) {
		ftl_frb_trace(LOG_ALW, 0x2827, "erase frb can not work SRB MR block");
	} else {
		frb_pb_erase(_frb_log.log.cur_blk, NULL);
		//frb_pb_erase(_frb_log.log.next_blk, NULL);
		ftl_frb_trace(LOG_ALW, 0x28fd, "erase frb erase done, sys reset");
	}
}


/*! \cond PRIVATE */
static ddr_code int frb_erase_main(int argc, char *argv[])
{
	frb_dump(&_frb_log);
	ftl_frb_trace(LOG_ALW, 0x8639, "erase frb %d", _frb_log.log.cur_blk);
	if ((_frb_log.log.cur_blk == 0)) {
		ftl_frb_trace(LOG_ALW, 0x9e2b, "erase frb can not work SRB MR block");
		return -1;
	}
	frb_pb_erase(_frb_log.log.cur_blk, NULL);
	//frb_pb_erase(_frb_log.log.next_blk, NULL);
	ftl_frb_trace(LOG_ALW, 0x7231, "erase frb erase done, sys reset");

//	ncl_warmboot_suspend();
	//sys_reset();
	return 0;
}


static fast_code void frb_header_update_cmpl(void *ctx)
{
	ftl_frb_trace(LOG_ALW, 0xd899, "frb header updated");
	mdelay(10);

//	ncl_warmboot_suspend();
	//sys_reset();
}

static fast_code int frb_boot_virgin(int argc, char *argv[])
{
	frb_dump(&_frb_log);
	ftl_frb_trace(LOG_ALW, 0x34b6, "force ftl virgin");

	_frb_log.log.ftl_boot_opt |= FRB_OPT_F_VIRGIN;
	_frb_log_io_res.cmpl = frb_header_update_cmpl;

	frb_log_flush(FRB_TYPE_HEADER, 0, 0);

	return 0;
}

static fast_code int frb_drop_grwn(int argc, char *argv[])
{
	frb_dump(&_frb_log);
	ftl_frb_trace(LOG_ALW, 0x8e59, "frb drop grown defect");

	_frb_log.log.ftl_boot_opt |= FRB_OPT_F_DROP_GDEF;
	_frb_log_io_res.cmpl = frb_header_update_cmpl;

	frb_log_flush(FRB_TYPE_HEADER, 0, 0);

	return 0;
}

#ifdef ERRHANDLE_VERIFY	// FET, RelsP2AndGL
ddr_code int frb_drop_P2GL(void)
{
	frb_dump(&_frb_log);
	ftl_frb_trace(LOG_ALW, 0xffdf, "frb drop P2List GList defect");

	_frb_log.log.ftl_boot_opt |= FRB_OPT_F_DROP_P2GL;
	_frb_log_io_res.cmpl = NULL;

	frb_log_flush(FRB_TYPE_HEADER, 0, 0);

	return 0;
}
#endif

u32 Get_FTL_Capacity(void)
{
	//ftl_frb_trace(LOG_ERR, 0, "FTL_Cap= %d\n", _frb_log.tbl.FTL_Current_Capacity);
	return _frb_log.tbl.FTL_Current_Capacity;
}

slow_code void frb_log_set_qbt_info(u32 TAG, u16 blk_id, u32 blk_sn)
{
    _frb_log.tbl.FTL_Tag_in_FRB.all = TAG;
	_frb_log.tbl.QBT_blk_idx = blk_id;
	_frb_log.tbl.QBT_SN = blk_sn;
}

init_code u32 frb_log_get_ftl_tag(void)
{
    return _frb_log.tbl.FTL_Tag_in_FRB.all;
}
init_code u16 frb_log_get_qbt_blk(void)
{
    return _frb_log.tbl.QBT_blk_idx;
}
init_code u32 frb_log_get_qbt_sn(void)
{
    return _frb_log.tbl.QBT_SN;
}

#if defined(SHRINK_CAP)
init_code void set_ftl_capacity(u32 capacity)
{
	_frb_log.tbl.FTL_Current_Capacity = capacity;
}
#endif

void Reset_FTL_Capacity(u32 vendor_capacity)
{
	if (_frb_log.log.next_page != 0) {
		frb_pb_erase(_frb_log.log.cur_blk, NULL);
		//frb_pb_erase(_frb_log.log.next_blk, NULL);
		//u32 temp = _frb_log.log.next_blk;
		//_frb_log.log.next_blk = _frb_log.log.cur_blk;
		//_frb_log.log.cur_blk = temp;
		_frb_log.log.next_page = 0;
	}

	_frb_log.tbl.FTL_Current_Capacity = vendor_capacity;
	frb_log_flush(FRB_TYPE_HEADER, 0, 1);
	frb_log_flush(FRB_TYPE_DEFECT, 0, 1);
}



static fast_code int frb_add_defect_main(int argc, char *argv[])
{
	u32 spb_id = atoi(argv[1]);

	if (spb_id >= get_total_spb_cnt()) {
		ftl_frb_trace(LOG_ERR, 0x059a, " spb %d in not valid \n", spb_id);
		return 0;
	}

	ins_grwn_err_info(GRWN_DEF_TYPE_OTHR_MANUL, nal_make_pda(spb_id, 0));
	spb_set_defect_blk(spb_id, ~0);

	return 0;
}

static fast_code int grwn_defect_dump(int argc, char *argv[])
{
	u32 i;

	ftl_frb_trace(LOG_ALW, 0x2fd1, "grown defect version %d count %d history %d",
				_frb_log.grwn_def.version,
				_frb_log.grwn_def.count,
				_frb_log.grwn_def.history);
	for (i = 0; i < _frb_log.grwn_def.count; i++) {
		u32 pda = _frb_log.grwn_def.pda[i];
		u32 spb = nal_pda_get_block_id(pda);
		ftl_frb_trace(LOG_ALW, 0x6129, "grown defect %d, type %d, spb %d, pda %x",
				i, _frb_log.grwn_def.type[i], spb, pda);
	}

	return 0;
}

static DEFINE_UART_CMD(frb_erase, "frb_erase", "erase frb",
		"erase ftl root block to virgin boot", 0, 0, frb_erase_main);
static DEFINE_UART_CMD(ftl_virgin, "ftl_virgin", "virgin boot",
		"force ftl virgin boot without earse frb", 0, 0, frb_boot_virgin);
static DEFINE_UART_CMD(ftl_drop_grwn, "ftl_drop_grwn", "virgin drop grown defect",
		"virgin drop grown defect", 0, 0, frb_drop_grwn);
static DEFINE_UART_CMD(add_df, "add_df", "add defect spb",
		"add_df [spb_id] : set [spb_id] as defect",
		1, 1, frb_add_defect_main);
static DEFINE_UART_CMD(dump_grwn, "dump_grwn", "dump grown defect info",
		"dump grown defect info", 0, 0, grwn_defect_dump);
/*! \endcond */
/*! @} */
