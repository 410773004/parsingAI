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
#include "srb.h"
#if epm_enable
#if CPU_ID != CPU_BE
//#if 1
#include "mpc.h"
#include "ncl_exports.h"
#include "ipc_api.h"
#include "eccu.h"
#include "nand.h"
#include "ddr.h"
#include "console.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "idx_meta.h"
#include "read_retry.h"
#define __FILEID__ epm
#include "trace.h"
#include "ncl_err.h"
#include "GList.h"
#include "ErrorHandle.h"
#include "fc_export.h"
#include "misc.h"

extern u64 sys_time;

typedef struct _epm_io_res_t
{
	struct ncl_cmd_t ncl_cmd; ///< NCL command resource
#if epm_remap_enable
	pda_t noremap_pda_list[DU_CNT_PER_EPM_FLUSH]; ///< pointer of PDA list buffer
#endif
	pda_t pda_list[DU_CNT_PER_EPM_FLUSH];				 ///< pointer of PDA list buffer  // die du num
	bm_pl_t bm_pl_list[DU_CNT_PER_EPM_FLUSH];			 ///< pointer of BM payload buffer
	struct info_param_t info_list[DU_CNT_PER_EPM_FLUSH]; ///< information list for NCL command
} epm_io_res_t;
extern volatile u8 plp_trigger;
extern volatile u8 plp_epm_update_done;
u8 epm_plp_flag;
u8 epm_back_counter;
u32 plp_page_num;
#ifdef ERRHANDLE_GLIST
share_data_zi volatile bool fgFail_Blk_Full;		  // need define in sram
share_data_zi sGLTable *pGList;			  // need define in Dram  // GL_mod, Paul_20201130
share_data_zi volatile u16* wOpenBlk; // need define in Dram
share_data_zi volatile u8 bErrOpCnt;
share_data_zi volatile u8 bErrOpIdx;

share_data_zi volatile bool FTL_INIT_DONE;

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi volatile sEH_Manage_Info sErrHandle_Info;
share_data_zi volatile MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi volatile u8 bFail_Blk_Cnt; //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];

share_data_zi volatile u8 wb_epm_lock_done;		//20210317-Eddie
share_data volatile bool _fg_warm_boot;
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
extern volatile u8 shr_slc_flush_state;
extern volatile bool slc_call_epm_update;
ddr_data u8 slc_epm_back_counter = 0;
#endif

extern volatile u8 power_on_update_epm_flag;
ddr_data u8 power_on_epm_back_counter = 0;

#endif
u8 epm_debug_log = 0;
u8 GLIST_sign_flag = 0;  //Indicates that this epm update for Glist sign.
epm_io_res_t epm_io_cmd;
epm_io_res_t epm_io_header_cmd[2];
u8 epm_header_idx = 0;
#if epm_uart_enable
u8 spor_flag = 0; //test spor
#endif
#if epm_remap_enable
u8 epm_remap_flag = 0; //test remap
epm_remap_tbl_t *epm_remap_tbl;
#endif
share_data epm_info_t *shr_epm_info; //epm.c init
share_data volatile bool OTF_WARM_BOOT_FLAG;

fast_data u8 evt_call_epm_update = 0xFF;

u32 EpmMetaIdx; //epm metadata
void *EpmMeta;

ddr_code void epm_evt_update(u32 p0, u32 p1, u32 type)
{
	if(type >= FTL_sign && type < EPM_PLP_end)
		epm_update(type, (CPU_ID - 1));
}


ddr_code void epm_init()
{
	epm_back_counter = 0;
	epm_plp_flag = false;
    
    extern u8 epm_Glist_updating;
    epm_Glist_updating = 0;
	GLIST_sign_flag = 0;
    
	epm_io_header_cmd[0].ncl_cmd.completion = NULL;
	epm_io_header_cmd[1].ncl_cmd.completion = NULL;
#if epm_debug
	epm_debug_log = 1;
	ncl_cmd_trace(LOG_ALW, 0xcf73, "epm debug enable");
#else
	epm_debug_log = 0;
	ncl_cmd_trace(LOG_ALW, 0x5706, "epm debug disable");
#endif

#if EPM_NOT_SAVE_Again	
    extern u8 EPM_NorShutdown;	
	EPM_NorShutdown = 0;
#endif	

	srb_t *srb = (srb_t *)SRAM_BASE;
	ncl_cmd_trace(LOG_ALW, 0xba1f, "epm_init");
	u32 i = 0;
	//init meta space
	EpmMeta = idx_meta_allocate(1, DDR_IDX_META, &EpmMetaIdx);
	//allocate dram space to shr_epm_info first
	//u32 shr_epm_info_ddtag = ddr_dtag_register(1);
	u32 shr_epm_info_ddtag = ddr_dtag_epm_register(1);
	shr_epm_info = (epm_info_t *)ddtag2mem(shr_epm_info_ddtag);
	//test no programmer , set pos value
	force_set_srb_rda();
	//emp header
	sys_assert((sizeof(epm_header_t) % (16 * 1024)) == 0);
	shr_epm_info->epm_header.ddtag_cnt = occupied_by(sizeof(epm_header_t), DTAG_SZE);
	//shr_epm_info->epm_header.ddtag = ddr_dtag_register(shr_epm_info->epm_header.ddtag_cnt);
	shr_epm_info->epm_header.ddtag = ddr_dtag_epm_register(shr_epm_info->epm_header.ddtag_cnt);

#if EPM_OTF_Time		
	if(_fg_warm_boot == false)			
#endif		
	{	
	//ftl
	sys_assert((sizeof(epm_FTL_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_ftl.ddtag_cnt = occupied_by(sizeof(epm_FTL_t), DTAG_SZE);
	//glist
	sys_assert((sizeof(epm_glist_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_glist.ddtag_cnt = occupied_by(sizeof(epm_glist_t), DTAG_SZE);
	//smart
	sys_assert((sizeof(epm_smart_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_smart.ddtag_cnt = occupied_by(sizeof(epm_smart_t), DTAG_SZE);
	//namespace
	sys_assert((sizeof(epm_namespace_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_namespace.ddtag_cnt = occupied_by(sizeof(epm_namespace_t), DTAG_SZE);
	//aes
	sys_assert((sizeof(epm_aes_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_aes.ddtag_cnt = occupied_by(sizeof(epm_aes_t), DTAG_SZE);
	//trim
	sys_assert((sizeof(epm_trim_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_trim.ddtag_cnt = occupied_by(sizeof(epm_trim_t), DTAG_SZE);
	//journal
	sys_assert((sizeof(epm_journal_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_journal.ddtag_cnt = occupied_by(sizeof(epm_journal_t), DTAG_SZE);
	//epm_misc
	/*sys_assert((sizeof(epm_misc_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_misc.ddtag_cnt = occupied_by(sizeof(epm_misc_t), DTAG_SZE);*/
	//epm_error_warn_data
	sys_assert((sizeof(epm_error_warn_t) % (32 * 1024)) == 0);
	shr_epm_info->epm_error_warn_data.ddtag_cnt = occupied_by(sizeof(epm_error_warn_t), DTAG_SZE);
	//epm_tcg_info
	//sys_assert((sizeof(epm_tcg_info_t) % (32 * 1024)) == 0);
	//shr_epm_info->epm_tcg_info.ddtag_cnt = occupied_by(sizeof(epm_tcg_info_t), DTAG_SZE);
	}	

	plp_page_num = (shr_epm_info->epm_ftl.ddtag_cnt + shr_epm_info->epm_smart.ddtag_cnt + shr_epm_info->epm_trim.ddtag_cnt + shr_epm_info->epm_error_warn_data.ddtag_cnt) / (EPM_BLK_CNT * DU_CNT_PER_PAGE);

	//rebuild_dtag
	//shr_epm_info->rebuild_ddtag = ddr_dtag_register(1);
	shr_epm_info->rebuild_ddtag = ddr_dtag_epm_register(1);
#if epm_remap_enable
	sys_assert((sizeof(epm_remap_tbl_t) % (16 * 1024)) == 0);
	u8 epm_remap_ddtag_cnt = occupied_by(sizeof(epm_remap_tbl_t), DTAG_SZE);
	//shr_epm_info->epm_remap_tbl_info_ddtag = ddr_dtag_register(epm_remap_ddtag_cnt); //remap ddr space
	shr_epm_info->epm_remap_tbl_info_ddtag = ddr_dtag_epm_register(epm_remap_ddtag_cnt);
	epm_remap_tbl = (epm_remap_tbl_t *)ddtag2mem(shr_epm_info->epm_remap_tbl_info_ddtag);
	
#if EPM_OTF_Time		
	if(_fg_warm_boot == false)			
#endif		
	{
	epm_init_remap(); //remap init or load
	}
#endif

#if EPM_OTF_Time		
	if(_fg_warm_boot == true)			
	{			
		for (i = FTL_sign; i < EPM_sign_end; i++)
		{
			epm_init_pos(i);
		}
	}
	else
#endif		
	{	
	latest_epm_data_t latest_epm_data;
	latest_epm_data_t latest_epm_data_mirror;
	for (i = FTL_sign; i < EPM_sign_end; i++)
	{
		epm_init_pos(i);
		latest_epm_data.latest_epm_sn[i] = 0;
		latest_epm_data.latest_epm_data_pda[i] = invalid_epm;
		latest_epm_data_mirror.latest_epm_sn[i] = 0;
		latest_epm_data_mirror.latest_epm_data_pda[i] = invalid_epm;
	}
	latest_epm_data.empty_pda = invalid_epm;
	latest_epm_data_mirror.empty_pda = invalid_epm;
	//scan epm header, load data or first init
	//if read error
	//scan header mirror?
	pda_t latest_epm_header_pda = invalid_epm;
	u32 max_epm_data_sn = 0;
	u32 max_epm_data_mirror_sn = 0;

	u8 head_valid_status = chk_epm_header_valid();

	//if(head_valid_status != 0xFF)
	//{
	//if ((head_valid_status == ALL_FLUSH) || (head_valid_status == ONLY_MASTER))
	if (head_valid_status == ONLY_MASTER)
		latest_epm_header_pda = scan_the_latest_epm_header();

	max_epm_data_sn = scan_the_latest_epm_data(&latest_epm_data, ONLY_MASTER);
	max_epm_data_mirror_sn = scan_the_latest_epm_data(&latest_epm_data_mirror, ONLY_MIRROR);

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x0dad, "latest_epm_header_pda[0x%x], max_sn[%d] max_mirror_sn[%d]", latest_epm_header_pda, max_epm_data_sn, max_epm_data_mirror_sn);
	//}

	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);

	if ((latest_epm_header_pda == invalid_epm) && (max_epm_data_sn == 0) && (max_epm_data_mirror_sn == 0))
	{
		ncl_cmd_trace(LOG_ALW, 0xa1e4, "header invalid and epm blk no data, init all");
		u32 *epm_header_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_header.ddtag);
		u32 *epm_ftl_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		u32 *epm_glist_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		u32 *epm_smart_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		u32 *epm_ns_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
		u32 *epm_aes_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		u32 *epm_trim_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		u32 *epm_journal_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
		//u32 *epm_misc_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
		u32 *epm_error_warn_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
		//u32 *epm_tcg_info_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_tcg_info.ddtag);
		
		memset(epm_header_ptr, 0x00, (shr_epm_info->epm_header.ddtag_cnt * DTAG_SZE));
		memset(epm_ftl_ptr, 0, (shr_epm_info->epm_ftl.ddtag_cnt * DTAG_SZE));
		memset(epm_glist_ptr, 0, (shr_epm_info->epm_glist.ddtag_cnt * DTAG_SZE));
		memset(epm_smart_ptr, 0, (shr_epm_info->epm_smart.ddtag_cnt * DTAG_SZE));
		memset(epm_ns_ptr, 0, (shr_epm_info->epm_namespace.ddtag_cnt * DTAG_SZE));
		memset(epm_aes_ptr, 0, (shr_epm_info->epm_aes.ddtag_cnt * DTAG_SZE));
		memset(epm_trim_ptr, 0, (shr_epm_info->epm_trim.ddtag_cnt * DTAG_SZE));
		memset(epm_journal_ptr, 0, (shr_epm_info->epm_journal.ddtag_cnt * DTAG_SZE));
		//memset(epm_misc_ptr, 0, (shr_epm_info->epm_misc.ddtag_cnt * DTAG_SZE));
		memset(epm_error_warn_ptr, 0, (shr_epm_info->epm_error_warn_data.ddtag_cnt * DTAG_SZE));
		//memset(epm_tcg_info_ptr, 0, (shr_epm_info->epm_tcg_info.ddtag_cnt * DTAG_SZE));
		
		//erase all
		for (i = 0; i < EPM_TAG_end; i++)
		{
			epm_erase(i);
		}
        
		epm_header_data->EPM_Head_tag = epm_head_tag;
		epm_header_data->EPM_SN = 0;

		epm_header_data->epm_header_last_pda = nal_rda_to_pda(&srb->epm_header_pos[0]);
		epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
		epm_header_data->valid_tag = epm_valid_tag;
		epm_header_data->epm_header_mirror_mask = nal_rda_to_pda(&srb->epm_header_pos[0]) ^ nal_rda_to_pda(&srb->epm_header_mir_pos[0]);
		epm_header_data->epm_mirror_mask = nal_rda_to_pda(&srb->epm_pos[0]) ^ nal_rda_to_pda(&srb->epm_pos_mir[0]);

		epm_flush_all(ALL_FLUSH, NULL);
		epm_header_update();
	}
	else
	{
		bool epm_header_valid = false;
		u8 reflush = ALL_FLUSH;
		if (latest_epm_header_pda == invalid_epm)
		{
			ncl_cmd_trace(LOG_ALW, 0x8c42, "epm_header invalid, epm blk valid");
			epm_header_valid = false;
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0x5300, "epm_header valid pda=0x%x", latest_epm_header_pda);
			epm_header_valid = epm_header_load(latest_epm_header_pda);
			//if read fail, retry and read mirror////
			if (epm_header_valid == false)
			{
				ncl_cmd_trace(LOG_ALW, 0x5f1b, "load latest epm_header fail");
				//panic("\n");
				/*
				ncl_cmd_trace(LOG_ALW, 0, "load latest epm_header fail, load mirror pda=0x%x", latest_epm_header_pda | epm_header_data->epm_header_mirror_mask);
				epm_header_valid = epm_header_load(latest_epm_header_pda | epm_header_data->epm_header_mirror_mask);

				if (epm_header_valid == false)
				{
					ncl_cmd_trace(LOG_ALW, 0, "load mirror latest epm_header fail, panic");
					panic("\n");
				}
				*/
			}
		}

		if ((epm_header_valid == true) && (epm_header_data->EPM_SN == max_epm_data_sn) && (epm_header_data->EPM_SN == max_epm_data_mirror_sn))
		{
			if (head_valid_status == ONLY_MASTER)
			{
				ncl_cmd_trace(LOG_ALW, 0x8f95, "only master");
				//reflush = ONLY_HEADER;
			}
			else
			{
				ncl_cmd_trace(LOG_ALW, 0x9e4b, "normal case");
			}

			if (epm_header_data->epm_last_pda != latest_epm_data.empty_pda)
			{
				ncl_cmd_trace(LOG_ALW, 0xb5b0, "epm_last_pda!=latest_empty_pda[0x%x]", latest_epm_data.empty_pda);
				epm_header_data->epm_last_pda = latest_epm_data.empty_pda;
				//panic("\n");     //history for debug
				reflush = rebuild_epm_header(latest_epm_data, latest_epm_data_mirror); //spor
			}
		}
		else if((epm_header_valid == true) && (epm_header_data->EPM_SN == max_epm_data_sn) && (epm_header_data->EPM_SN == ++max_epm_data_mirror_sn))
		{
			ncl_cmd_trace(LOG_ALW, 0x8ee5, "case for plp page not enough, max_sn=%d max_mirror_sn=%d", max_epm_data_sn, max_epm_data_mirror_sn);
			reflush = ONLY_MASTER;
		}
		else if((max_epm_data_sn != 0) || (max_epm_data_mirror_sn != 0)) // as long as have one EPM data, header and another EPM can rebuild  
		{
			ncl_cmd_trace(LOG_ALW, 0x7de6, "rebuild_epm_header, max_sn=%d max_mirror_sn=%d", max_epm_data_sn, max_epm_data_mirror_sn);
			reflush = rebuild_epm_header(latest_epm_data, latest_epm_data_mirror);
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0x83c9, "No this case");
			panic("\n");
		}

		epm_read_all();

		if (reflush)
		{
			if (reflush < ONLY_HEADER)
			{
				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				pda_t only_mirror_pda = (nal_rda_to_pda(&srb->epm_pos[0])) | epm_header_data->epm_mirror_mask;

				if (reflush == ONLY_MIRROR)
				{
					ncl_cmd_trace(LOG_ALW, 0x03ab, "epm blk erase and reflush");
					epm_erase(EPM_DATA_TAG);
					epm_flush_all(ONLY_MASTER, NULL);
					epm_erase(EPM_DATA_MIRROR_TAG);
					epm_flush_all(ONLY_MIRROR, &only_mirror_pda);
				}
				else if (reflush == ONLY_MASTER)
				{
					ncl_cmd_trace(LOG_ALW, 0x0de4, "epm blk mirror erase and reflush");
					epm_erase(EPM_DATA_MIRROR_TAG);
					epm_flush_all(ONLY_MIRROR, &only_mirror_pda);
					epm_erase(EPM_DATA_TAG);
					epm_flush_all(ONLY_MASTER, NULL);
				}
				else
				{
					ncl_cmd_trace(LOG_ALW, 0xa2db, "error case");
					panic("error case\n");
				}
			}

			epm_header_data->epm_header_last_pda = nal_rda_to_pda(&srb->epm_header_pos[0]);
			//pda_t only_mirror_head_pda = (nal_rda_to_pda(&srb->epm_header_pos[0])) | epm_header_data->epm_header_mirror_mask;
			if (reflush == ONLY_HEADER)
			{
				ncl_cmd_trace(LOG_ALW, 0x5aeb, "erase mirror head and reflush-ONLY_HEADER");
				//epm_erase(EPM_HEADER_MIRROR_TAG);
				//epm_header_flush(ONLY_MIRROR, only_mirror_head_pda, false);
				epm_erase(EPM_HEADER_TAG);
				epm_header_flush(ONLY_MASTER, 0, false);
			}
			else
			{
				//reflush header
				epm_erase(EPM_HEADER_TAG);
				epm_header_flush(ONLY_MASTER, 0, true);
				//epm_erase(EPM_HEADER_MIRROR_TAG);
				//epm_header_flush(ONLY_MIRROR, only_mirror_head_pda, false);
			}
		}
	}
}

	evt_register(epm_evt_update, 0, &evt_call_epm_update);
	//epm_update(ERROR_WARN_sign, (CPU_ID - 1));
	dump_error_warn_info();
	ncl_cmd_trace(LOG_ALW, 0xd51a, "epm_init done");
	
#ifdef ERRHANDLE_GLIST
    gl_build_table();
    
#else
	epm_glist_t *epm_glist_start = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	pGList = (sGLTable *)(&epm_glist_start->data[0]);

	if (pGList->dGL_Tag == GLIST_TAG)
	{

	}
	else
	{
		ncl_cmd_trace(LOG_ALW, 0xd51e, "[GL] invalid tag[0x%x]", pGList->dGL_Tag); // GL_mod, Paul_20201130
		//u32 *pGlistDtag = (u32*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		//memset(pGlistDtag, 0, (GLIST_EPM_NEED_DTAG * DTAG_SZE));

		memset(pGList, 0, (GLIST_EPM_NEED_DTAG * DTAG_SZE));

		pGList->dGL_Tag = GLIST_TAG;
		pGList->bGL_Ver = GLIST_VER;
		pGList->bGL_VerInvert = ~(pGList->bGL_Ver);

		epm_update(GLIST_sign, (CPU_ID - 1));
	}
	
#endif
}

ddr_code void dump_error_warn_info()
{
	epm_error_warn_t* epm_error_warn_data = (epm_error_warn_t*)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);

	u16 idx;
	
	ncl_cmd_trace(LOG_ALW, 0x2d66,"cur save idx : %d need init : %d",epm_error_warn_data->cur_save_idx,epm_error_warn_data->need_init);
	for(idx = 0; idx <= 1; idx++)
	{	
		if(epm_error_warn_data->cur_save_type[idx] == 0)
			continue;
		ncl_cmd_trace(LOG_ALW, 0x7bfe,"[error_warn] ---------------index : %d-------------",idx);
		//------------basic info-------------
		ncl_cmd_trace(LOG_ALW, 0x04e0," save_type : %d, cpu_id : %d, pc_cnt : 0x%x, temperature :%d",
			epm_error_warn_data->cur_save_type[idx],epm_error_warn_data->cur_update_cpu_id[idx],epm_error_warn_data->cur_power_cycle_cnt[idx],epm_error_warn_data->cur_temperature[idx]);
		//-------------plp info--------------
		ncl_cmd_trace(LOG_ALW, 0xeeb5," CPU1_plp_step:%d, CPU2_plp_step:%d, cpu1_gpio_lr:0x%x",
			epm_error_warn_data->record_CPU1_plp_step[idx],epm_error_warn_data->record_CPU2_plp_step[idx],epm_error_warn_data->record_cpu1_gpio_lr[idx]);
		ncl_cmd_trace(LOG_ALW, 0x3272," host_open_die : %d, host_next_die : %d",epm_error_warn_data->record_host_open_die[idx],epm_error_warn_data->record_host_next_die[idx]);
		//------------write info-------------
		ncl_cmd_trace(LOG_ALW, 0x374f," is_host_idle : %d, is_gcing : %d, cache_handle_cnt : %d",
			epm_error_warn_data->is_host_idle[idx],epm_error_warn_data->is_gcing[idx],epm_error_warn_data->cache_handle_cnt[idx]);
		ncl_cmd_trace(LOG_ALW, 0x8f66," FICU_start : 0x%x, FICU_end : 0x%x",epm_error_warn_data->FICU_start[idx],epm_error_warn_data->FICU_done[idx]);
		//------------gc info---------------
		ncl_cmd_trace(LOG_ALW, 0xd0d3," cur_global_gc_mode : %d cur_cpu_feedback : 0x%x",epm_error_warn_data->cur_global_gc_mode[idx],epm_error_warn_data->cur_cpu_feedback[idx]);

	}
	
}

void force_set_srb_rda()
{
	srb_t *srb = (srb_t *)SRAM_BASE;
	//epm header pos
	srb->epm_header_pos[0].row = 0x00000200;
	srb->epm_header_pos[0].ch = 0x5;
	srb->epm_header_pos[0].dev = 0;
	srb->epm_header_pos[0].du_off = 0;
	srb->epm_header_pos[0].pb_type = 0;
	srb->epm_header_pos[1].row = 0x00000000;
	srb->epm_header_pos[1].ch = 0x0;
	srb->epm_header_pos[1].dev = 0;
	srb->epm_header_pos[1].du_off = 0;
	srb->epm_header_pos[1].pb_type = 0;

	srb->epm_header_mir_pos[0].row = 0x00000000;
	srb->epm_header_mir_pos[0].ch = 0;
	srb->epm_header_mir_pos[0].dev = 0;
	srb->epm_header_mir_pos[0].du_off = 0;
	srb->epm_header_mir_pos[0].pb_type = 0;
	srb->epm_header_mir_pos[1].row = 0x00000000;
	srb->epm_header_mir_pos[1].ch = 0;
	srb->epm_header_mir_pos[1].dev = 0;
	srb->epm_header_mir_pos[1].du_off = 0;
	srb->epm_header_mir_pos[1].pb_type = 0;
	//epm pos
	srb->epm_pos[0].row = 0x00000000;
	srb->epm_pos[0].ch = 0x6;
	srb->epm_pos[0].dev = 0;
	srb->epm_pos[0].du_off = 0;
	srb->epm_pos[0].pb_type = 0;
	srb->epm_pos[1].row = 0x00000200;
	srb->epm_pos[1].ch = 0x6;
	srb->epm_pos[1].dev = 0;
	srb->epm_pos[1].du_off = 0;
	srb->epm_pos[1].pb_type = 0;
	srb->epm_pos[2].row = 0x00000000;
	srb->epm_pos[2].ch = 0x0;
	srb->epm_pos[2].dev = 0;
	srb->epm_pos[2].du_off = 0;
	srb->epm_pos[2].pb_type = 0;
	srb->epm_pos[3].row = 0x00000000;
	srb->epm_pos[3].ch = 0x0;
	srb->epm_pos[3].dev = 0;
	srb->epm_pos[3].du_off = 0;
	srb->epm_pos[3].pb_type = 0;
	srb->epm_pos[4].row = 0x00000000;
	srb->epm_pos[4].ch = 0x0;
	srb->epm_pos[4].dev = 0;
	srb->epm_pos[4].du_off = 0;
	srb->epm_pos[4].pb_type = 0;
	srb->epm_pos[5].row = 0x00000000;
	srb->epm_pos[5].ch = 0x0;
	srb->epm_pos[5].dev = 0;
	srb->epm_pos[5].du_off = 0;
	srb->epm_pos[5].pb_type = 0;
	srb->epm_pos[6].row = 0x00000000;
	srb->epm_pos[6].ch = 0x0;
	srb->epm_pos[6].dev = 0;
	srb->epm_pos[6].du_off = 0;
	srb->epm_pos[6].pb_type = 0;
	srb->epm_pos[7].row = 0x00000000;
	srb->epm_pos[7].ch = 0x0;
	srb->epm_pos[7].dev = 0;
	srb->epm_pos[7].du_off = 0;
	srb->epm_pos[7].pb_type = 0;
	// 4T & 8T mirror in same phkblk
	ncl_cmd_trace(LOG_ALW, 0xcaf3, "force_set_srb_rda");
	srb->epm_pos_mir[0].row = 0x00000000;
	srb->epm_pos_mir[0].ch = 0x7;
	srb->epm_pos_mir[0].dev = 0;
	srb->epm_pos_mir[0].du_off = 0;
	srb->epm_pos_mir[0].pb_type = 0;
	srb->epm_pos_mir[1].row = 0x00000200;
	srb->epm_pos_mir[1].ch = 0x7;
	srb->epm_pos_mir[1].dev = 0;
	srb->epm_pos_mir[1].du_off = 0;
	srb->epm_pos_mir[1].pb_type = 0;
	srb->epm_pos_mir[2].row = 0x00000000;
	srb->epm_pos_mir[2].ch = 0x0;
	srb->epm_pos_mir[2].dev = 0;
	srb->epm_pos_mir[2].du_off = 0;
	srb->epm_pos_mir[2].pb_type = 0;
	srb->epm_pos_mir[3].row = 0x00000000;
	srb->epm_pos_mir[3].ch = 0x0;
	srb->epm_pos_mir[3].dev = 0;
	srb->epm_pos_mir[3].du_off = 0;
	srb->epm_pos_mir[3].pb_type = 0;
	srb->epm_pos_mir[4].row = 0x00000000;
	srb->epm_pos_mir[4].ch = 0x0;
	srb->epm_pos_mir[4].dev = 0;
	srb->epm_pos_mir[4].du_off = 0;
	srb->epm_pos_mir[4].pb_type = 0;
	srb->epm_pos_mir[5].row = 0x00000000;
	srb->epm_pos_mir[5].ch = 0x0;
	srb->epm_pos_mir[5].dev = 0;
	srb->epm_pos_mir[5].du_off = 0;
	srb->epm_pos_mir[5].pb_type = 0;
	srb->epm_pos_mir[6].row = 0x00000000;
	srb->epm_pos_mir[6].ch = 0x0;
	srb->epm_pos_mir[6].dev = 0;
	srb->epm_pos_mir[6].du_off = 0;
	srb->epm_pos_mir[6].pb_type = 0;
	srb->epm_pos_mir[7].row = 0x00000000;
	srb->epm_pos_mir[7].ch = 0x0;
	srb->epm_pos_mir[7].dev = 0;
	srb->epm_pos_mir[7].du_off = 0;
	srb->epm_pos_mir[7].pb_type = 0;
}

void epm_init_pos(u32 epm_sign)
{
	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);
	epm_pos->ddtag = ddr_dtag_epm_register(epm_pos->ddtag_cnt); 
#if epm_spin_lock_enable
	u32 i = 0;
	epm_pos->busy_lock = 0;
	epm_pos->cur_key = 1;
	epm_pos->alloc_key = 1;
	for (i = 0; i < 4; i++)
	{
		epm_pos->key_num[i] = 0;
		epm_pos->set_ddr_done[i] = 1;
	}
#endif
}

epm_sub_header_t get_epm_newest_pda(u32 epm_sign)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0xdde5, "get_epm_newest_pda epm_sing=%d", epm_sign);
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	epm_sub_header_t epm_pda;
	switch (epm_sign)
	{
	case FTL_sign:
		epm_pda = epm_header_data->epm_ftl_header;
		break;
	case GLIST_sign:
		epm_pda = epm_header_data->epm_glist_header;
		break;
	case SMART_sign:
		epm_pda = epm_header_data->epm_smart_header;
		break;
	case NAMESPACE_sign:
		epm_pda = epm_header_data->epm_namespace_header;
		break;
	case AES_sign:
		epm_pda = epm_header_data->epm_aes_header;
		break;
	case TRIM_sign:
		epm_pda = epm_header_data->epm_trim_header;
		break;
	case JOURNAL_sign:
		epm_pda = epm_header_data->epm_journal_header;
		break;
	/*case MISC_sign:
		epm_pda = epm_header_data->epm_misc_header;
		break;*/
	case ERROR_WARN_sign:
		epm_pda = epm_header_data->epm_nvme_data_header;
		break;
	//case TCG_INFO_sign:
		//epm_pda = epm_header_data->epm_tcg_info_header;
		//break;
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:		
		ncl_cmd_trace(LOG_ALW, 0x711b, "get_epm_newest_pda ppwercycle case");
		break;
	default:
		panic("no this case\n");
	}
	return epm_pda;
}

extern volatile u8 eccu_during_change;
u32 epm_ncl_cmd(pda_t *pda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count, int du_format, int stripe_id)
{
	int i;
	epm_io_cmd.ncl_cmd.addr_param.common_param.info_list = epm_io_cmd.info_list;
	epm_io_cmd.ncl_cmd.addr_param.common_param.list_len = count;
	epm_io_cmd.ncl_cmd.addr_param.common_param.pda_list = pda_list;

	memset(epm_io_cmd.info_list, 0, sizeof(struct info_param_t) * count);

	for (i = 0; i < count; i++)
		epm_io_cmd.info_list[i].pb_type = NAL_PB_TYPE_SLC;

	epm_io_cmd.ncl_cmd.caller_priv = NULL;
	epm_io_cmd.ncl_cmd.completion = NULL;
	epm_io_cmd.ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;
	epm_io_cmd.ncl_cmd.op_code = op;
    if(op == NCL_CMD_OP_READ){
        epm_io_cmd.ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
        #if RAID_SUPPORT_UECC
	    epm_io_cmd.ncl_cmd.uecc_type = NCL_UECC_NORMAL_RD;
        #endif
    }else{
        epm_io_cmd.ncl_cmd.op_type = INT_TABLE_WRITE_PRE_ASSIGN;
    }
	struct du_meta_fmt *Meta = (struct du_meta_fmt *)EpmMeta;
	memset(Meta, 0, sizeof(struct du_meta_fmt));
	Meta->fmt4.Debug = 0x45;
	epm_io_cmd.ncl_cmd.user_tag_list = bm_pl;
	epm_io_cmd.ncl_cmd.du_format_no = DU_FMT_USER_4K;


#ifdef NCL_HAVE_reARD
ToDoRetry:
#endif
	epm_io_cmd.ncl_cmd.status = 0;

	u8 dump_flag;
    dump_flag = 0;
	while(eccu_during_change == true)
	{
			if(dump_flag == 0)
			{
					ncl_cmd_trace(LOG_DEBUG, 0xff41, "epm in while");
					dump_flag |= BIT0;
			}

			if(ncl_busy[NRM_SQ_IDX] == true)
			{
					dump_flag |= BIT1;
					ficu_done_wait();
			}
	};

    if(dump_flag)
    {
        ncl_cmd_trace(LOG_DEBUG, 0x40ce, "epm out while %x",dump_flag);
    }


	ncl_cmd_submit(&epm_io_cmd.ncl_cmd);
	if (op == NCL_CMD_OP_READ)
	{
		i = 0;
		nal_status_t ret;
		ret = ficu_err_good;
		if (epm_io_cmd.ncl_cmd.status != 0)
		{
			do
			{
#ifdef NCL_HAVE_reARD
				if ((epm_io_cmd.info_list[i].status == ficu_err_nard) && (epm_io_cmd.ncl_cmd.re_ard_flag == true))
				{
					ncl_cmd_trace(LOG_ERR, 0x65d8, "[ERR] EPM not do ARD");
					ret = epm_io_cmd.info_list[i].status;
					epm_io_cmd.ncl_cmd.re_ard_flag = false;
					goto ToDoRetry;
					//return ret;
				}
#endif
				if (epm_io_cmd.info_list[i].status > ret)
				{
					ret = epm_io_cmd.info_list[i].status;
				}
			} while (++i < count);
		}
		return ret;
	}
#if epm_remap_enable && epm_uart_enable
	if (epm_remap_flag && op == NCL_CMD_OP_ERASE)
	{
		ncl_cmd_trace(LOG_ALW, 0x78df, "test remap force set erase fail");
		epm_io_cmd.ncl_cmd.status = 1;
		epm_io_cmd.info_list[1].status = 1;
		epm_io_cmd.info_list[2].status = 1;
		epm_io_cmd.info_list[3].status = 1;
		epm_io_cmd.info_list[4].status = 1;
		epm_io_cmd.info_list[6].status = 1;
		epm_io_cmd.info_list[7].status = 1;
		epm_remap_flag = 0;
	}
#endif
	return epm_io_cmd.ncl_cmd.status;
}

u32 epm_header_ncl_cmd(pda_t *pda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count, int du_format)
{
	int i;
	epm_io_header_cmd[epm_header_idx].ncl_cmd.addr_param.common_param.info_list = epm_io_header_cmd[epm_header_idx].info_list;
	epm_io_header_cmd[epm_header_idx].ncl_cmd.addr_param.common_param.list_len = count;
	epm_io_header_cmd[epm_header_idx].ncl_cmd.addr_param.common_param.pda_list = pda_list;

	memset(epm_io_header_cmd[epm_header_idx].info_list, 0, sizeof(struct info_param_t) * count);

	for (i = 0; i < count; i++)
		epm_io_header_cmd[epm_header_idx].info_list[i].pb_type = NAL_PB_TYPE_SLC;

	epm_io_header_cmd[epm_header_idx].ncl_cmd.caller_priv = NULL;
	if(epm_plp_flag)
	{
		epm_back_counter++;
		epm_io_header_cmd[epm_header_idx].ncl_cmd.completion = epm_flush_done;
	}
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
	else if(shr_slc_flush_state == SLC_FLOW_START_ERASE)
	{
		slc_epm_back_counter++;
		epm_io_header_cmd[epm_header_idx].ncl_cmd.completion = slc_epm_flush_done;
	}
#endif
	else if(power_on_update_epm_flag == POWER_ON_EPM_UPDATE_START)
	{
		power_on_epm_back_counter++;
		epm_io_header_cmd[epm_header_idx].ncl_cmd.completion = power_on_epm_flush_done;
		
	}
    else if(GLIST_sign_flag == 1)
    {
        GLIST_sign_flag = 0;

        extern u8 epm_Glist_updating;
        epm_Glist_updating = 0;  //The epm Glist has been updaed. Reset epm_Glist_updating = 0.
    }
    else
    {
		epm_io_header_cmd[epm_header_idx].ncl_cmd.completion = NULL;
    }
	epm_io_header_cmd[epm_header_idx].ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;
	epm_io_header_cmd[epm_header_idx].ncl_cmd.op_code = op;
    if(op == NCL_CMD_OP_READ){
        epm_io_header_cmd[epm_header_idx].ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
        #if RAID_SUPPORT_UECC
	    epm_io_header_cmd[epm_header_idx].ncl_cmd.uecc_type = NCL_UECC_NORMAL_RD;
        #endif
    }else{
        epm_io_header_cmd[epm_header_idx].ncl_cmd.op_type = INT_TABLE_WRITE_PRE_ASSIGN;
    }
	struct du_meta_fmt *Meta = (struct du_meta_fmt *)EpmMeta;
	memset(Meta, 0, sizeof(struct du_meta_fmt));
	Meta->fmt4.Debug = 0x45;
	epm_io_header_cmd[epm_header_idx].ncl_cmd.user_tag_list = bm_pl;
	epm_io_header_cmd[epm_header_idx].ncl_cmd.du_format_no = DU_FMT_USER_4K;


#ifdef NCL_HAVE_reARD
ToDoRetry:
#endif
	epm_io_header_cmd[epm_header_idx].ncl_cmd.status = 0;

	u8 dump_flag;
    dump_flag = 0;
	while(eccu_during_change == true)
	{
			if(dump_flag == 0)
			{
					ncl_cmd_trace(LOG_DEBUG, 0x50f8, "epm in while");
					dump_flag |= BIT0;
			}

			if(ncl_busy[NRM_SQ_IDX] == true)
			{
					dump_flag |= BIT1;
					ficu_done_wait();
			}
	};

    if(dump_flag)
    {
        ncl_cmd_trace(LOG_DEBUG, 0x9155, "epm out while %x",dump_flag);
    }


	ncl_cmd_submit(&epm_io_header_cmd[epm_header_idx].ncl_cmd);
	
	if (op == NCL_CMD_OP_READ)
	{
		i = 0;
		nal_status_t ret;
		ret = ficu_err_good;
		if (epm_io_header_cmd[epm_header_idx].ncl_cmd.status != 0)
		{
			do
			{
#ifdef NCL_HAVE_reARD
				if ((epm_io_header_cmd[epm_header_idx].info_list[i].status == ficu_err_nard) && (epm_io_header_cmd[epm_header_idx].ncl_cmd.re_ard_flag == true))
				{
					ncl_cmd_trace(LOG_ERR, 0x315b, "[ERR] EPM not do ARD");
					ret = epm_io_header_cmd[epm_header_idx].info_list[i].status;
					epm_io_header_cmd[epm_header_idx].ncl_cmd.re_ard_flag = false;
					goto ToDoRetry;
					//return ret;
				}
#endif
				if (epm_io_header_cmd[epm_header_idx].info_list[i].status > ret)
				{
					ret = epm_io_cmd.info_list[i].status;
				}
			} while (++i < count);
		}
		return ret;
	}
#if epm_remap_enable && epm_uart_enable
	if (epm_remap_flag && op == NCL_CMD_OP_ERASE)
	{
		ncl_cmd_trace(LOG_ALW, 0x11de, "test remap force set erase fail");
		epm_io_header_cmd[epm_header_idx].ncl_cmd.status = 1;
		epm_io_header_cmd[epm_header_idx].info_list[1].status = 1;
		epm_io_header_cmd[epm_header_idx].info_list[2].status = 1;
		epm_io_header_cmd[epm_header_idx].info_list[3].status = 1;
		epm_io_header_cmd[epm_header_idx].info_list[4].status = 1;
		epm_io_header_cmd[epm_header_idx].info_list[6].status = 1;
		epm_io_header_cmd[epm_header_idx].info_list[7].status = 1;
		epm_remap_flag = 0;
	}
#endif
	return epm_io_header_cmd[epm_header_idx].ncl_cmd.status;
}

#if epm_remap_enable
void epm_remap_get_pda(pda_t *pda_base, pda_t *pda_list, u8 count)
{
	u8 i = 0;
	for (i = 0; i < count; i++)
		pda_list[i] = (*pda_base) + i;
	*pda_base += (nal_get_interleave() << DU_CNT_SHIFT); //add page
}
void epm_remap_tbl_flush(pda_t *pda_base)
{
	ncl_cmd_trace(LOG_ALW, 0x5f64, "epm_remap_tbl_flush");
	u32 i = 0, status = 0;
	u8 mirror_erase = 0;
	epm_remap_tbl->epm_remap_sn++;
	if (pda2page(*pda_base) >= nand_page_num_slc())
	{
		ncl_cmd_trace(LOG_ALW, 0x6376, "blk not enough change blk pda[0x%x]", *pda_base);
		chk_pda(*pda_base);
		/*if (pda2plane(*pda_base) == 0)   //Endion  blk[0]:master, blk[1]:mirror
			*pda_base = epm_remap_tbl->remap_tbl_blk[1];
		else
			*pda_base = epm_remap_tbl->remap_tbl_blk[0];*/
		*pda_base = epm_remap_tbl->remap_tbl_blk[0];
		mirror_erase = 1; //mirror also need to erase before write
		status = epm_ncl_cmd(pda_base, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x7543, "remap erase error");
	}
	epm_remap_get_pda(pda_base, epm_io_cmd.pda_list, 1);
	epm_remap_tbl->remap_last_pda = *pda_base ;
	ncl_cmd_trace(LOG_ALW, 0x2162, "get_remap_pda[0x%x]", epm_io_cmd.pda_list[0]);
	chk_pda(epm_io_cmd.pda_list[0]);
	for (i = 0; i < DU_CNT_PER_PAGE; i++)
	{
		epm_io_cmd.bm_pl_list[i].pl.dtag = (DTAG_IN_DDR_MASK | (shr_epm_info->epm_remap_tbl_info_ddtag + (i)));
		epm_io_cmd.bm_pl_list[i].pl.du_ofst = i;
		epm_io_cmd.bm_pl_list[i].pl.btag = 0;
		epm_io_cmd.bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
		epm_io_cmd.bm_pl_list[i].pl.nvm_cmd_id = EpmMetaIdx;
	}
	epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE, 0);

	//write mirror
	epm_io_cmd.pda_list[0] |= epm_remap_tbl->remap_tbl_mirror_mask;
	ncl_cmd_trace(LOG_ALW, 0xe691, "get_mir_remap_pda[0x%x]", epm_io_cmd.pda_list[0]);
	if (mirror_erase == 1)
	{
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0xa6d4, "mirror remap erase error");
	}

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0xd544, "get mirror pda0=0x%x", epm_io_cmd.pda_list[0]);

	epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE, 0);
}

u32 scan_the_latest_epm_ramap(pda_t *the_latest_pda, pda_t *pda_base)
{
	pda_t remap_start_pda = 0;
	u32 epm_remap_sn = 0, the_latest_sn = 0, valid_tag = 0;
	nal_status_t sts = 0;
	dtag_t dtag_temp;
	u32 dtag_cnt = dtag_get_bulk(DTAG_T_SRAM, 1, &dtag_temp); // need to use sram!!!!! improtant!!!
	u32 *data = dtag2mem(dtag_temp);
	bm_pl_t pl;
	//pl.pl.dtag = (DTAG_IN_DDR_MASK | shr_epm_info->rebuild_ddtag);
	pl.pl.dtag = dtag_temp.b.dtag;
	pl.pl.du_ofst = 0;
	pl.pl.btag = 0;
	pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
	pl.pl.nvm_cmd_id = EpmMetaIdx;
	while (1)
	{
		if (pda2page(*pda_base) >= nand_page_num_slc())
		{
			ncl_cmd_trace(LOG_ALW, 0xfc02, "blk not enough change blk pda[0x%x]", *pda_base);
			chk_pda(*pda_base);
			break;
		}
		sts = epm_ncl_cmd(pda_base, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
		//ncl_cmd_trace(LOG_ERR, 0, "read remap head pda[0x%x]\n",*pda_base);
		//chk_pda(*pda_base);
		if (sts == ficu_err_du_erased)
		{
			ncl_cmd_trace(LOG_ALW, 0xb567, "load epm remap empty sts=%d", sts);
			break;
		}
		else if (sts == 0)
		{
			if (*data != epm_head_tag)
			{
				ncl_cmd_trace(LOG_ALW, 0xe6c2, "remap tag fail");
				break;
			}
			epm_remap_sn = *(data + 1);
			remap_start_pda = *pda_base;
			epm_remap_get_pda(pda_base, epm_io_cmd.pda_list, DU_CNT_PER_PAGE);
			sts = epm_ncl_cmd(&epm_io_cmd.pda_list[(DU_CNT_PER_PAGE - 1)], NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
			//ncl_cmd_trace(LOG_ERR, 0, "epm_io_cmd.pda_list[%d]=0x%x\n",(DU_CNT_PER_PAGE-1),epm_io_cmd.pda_list[(DU_CNT_PER_PAGE-1)]);
			//chk_pda(epm_io_cmd.pda_list[(DU_CNT_PER_PAGE-1)]);
			if (sts == ficu_err_du_erased)
			{
				ncl_cmd_trace(LOG_ALW, 0x1d9f, "load epm remap head valid tail empty");
				break;
			}
			else if (sts == 0)
			{
				valid_tag = *(data + ((4096 / 4) - 1));
				if (valid_tag == epm_valid_tag)
				{
					the_latest_sn = epm_remap_sn;	   //scan out SN
					*the_latest_pda = remap_start_pda; //scan out pda
				}
			}
			else
			{
				ncl_cmd_trace(LOG_ALW, 0xccb2, "valid_tag invalid");
				break;
			}
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0x64ea, "load epm remap read error sts=%d", sts);
			break;
		}
	}
	if (the_latest_sn != 0)
	{
		while (1)
		{
			if (pda2page(*pda_base) >= nand_page_num_slc())
			{
				ncl_cmd_trace(LOG_ALW, 0x4b58, "scan empty page end page>slc page page[%d]", pda2page(*pda_base));
				chk_pda(*pda_base);
				break;
			}
			sts = epm_ncl_cmd(pda_base, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
			if (sts == ficu_err_du_erased)
			{
				ncl_cmd_trace(LOG_ALW, 0xef86, "find empty pda");
				//chk_pda(*pda_base);
				break;
			}
			epm_remap_get_pda(pda_base, epm_io_cmd.pda_list, DU_CNT_PER_PAGE);
		}
	}
	dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, &dtag_temp);
	return the_latest_sn; // 0 is not found
}
bool epm_remap_tbl_load()
{
	ncl_cmd_trace(LOG_ALW, 0xb3c7, "epm_remap_tbl_load");
	u32 remap_blk1_max_sn = 0, remap_blk2_max_sn = 0, i = 0;
	pda_t the_latest_blk1_pda = 0xFFFFFFFF, the_latest_blk2_pda = 0xFFFFFFFF;
	pda_t remap_blk1_pda = epm_remap_tbl->remap_tbl_blk[0];
	pda_t remap_blk2_pda = epm_remap_tbl->remap_tbl_blk[1];
	pda_t the_latest_remap_pda = 0;
	ncl_cmd_trace(LOG_ALW, 0x9c42, "remap_blk1_pda=0x%x remap_blk2_pda=0x%x", remap_blk1_pda, remap_blk2_pda);
	remap_blk1_max_sn = scan_the_latest_epm_ramap(&the_latest_blk1_pda, &remap_blk1_pda);
	remap_blk2_max_sn = scan_the_latest_epm_ramap(&the_latest_blk2_pda, &remap_blk2_pda);
	ncl_cmd_trace(LOG_ALW, 0x06a2, "remap_blk1_max_sn=%d remap_blk2_max_sn=%d", remap_blk1_max_sn, remap_blk2_max_sn);
	ncl_cmd_trace(LOG_ALW, 0x6630, "the_latest_blk1_pda=0x%x the_latest_blk2_pda=0x%x", the_latest_blk1_pda, the_latest_blk2_pda);
	ncl_cmd_trace(LOG_ALW, 0x996e, "remap_blk1_pda=0x%x remap_blk2_pda=0x%x", remap_blk1_pda, remap_blk2_pda);

	if ((remap_blk1_max_sn == 0) && (remap_blk2_max_sn == 0))
	{
		return false;
	}
	/*else
	{
		if (remap_blk1_max_sn > remap_blk2_max_sn)
		{
			the_latest_remap_pda = the_latest_blk1_pda;
		}
		else if (remap_blk1_max_sn < remap_blk2_max_sn)
		{
			the_latest_remap_pda = the_latest_blk2_pda;
		}
		else
		{
			ncl_cmd_trace(LOG_ERR, 0, "no this case\n");
			return false;
		}
	}*/
	the_latest_remap_pda = the_latest_blk1_pda;
	ncl_cmd_trace(LOG_ALW, 0xf2f4, "the_latest_remap_pda[0x%x]", the_latest_remap_pda);
	chk_pda(the_latest_remap_pda);
	epm_remap_get_pda(&the_latest_remap_pda, epm_io_cmd.pda_list, DU_CNT_PER_PAGE);
	ncl_cmd_trace(LOG_ALW, 0x1ddd, "epm_io_cmd.pda_list[0x%x]", epm_io_cmd.pda_list[0]);
	chk_pda(epm_io_cmd.pda_list[0]);
	for (i = 0; i < DU_CNT_PER_PAGE; i++)
	{
		epm_io_cmd.bm_pl_list[i].pl.dtag = (DTAG_IN_DDR_MASK | (shr_epm_info->epm_remap_tbl_info_ddtag + (i)));
		epm_io_cmd.bm_pl_list[i].pl.du_ofst = i;
		epm_io_cmd.bm_pl_list[i].pl.btag = 0;
		epm_io_cmd.bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
		epm_io_cmd.bm_pl_list[i].pl.nvm_cmd_id = EpmMetaIdx;
	}
	nal_status_t sts = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_READ, epm_io_cmd.bm_pl_list, 4, DU_4K_DEFAULT_MODE, 0);

	if (sts == 0)
	{
		return true;
	}
	else
	{
		ncl_cmd_trace(LOG_ALW, 0xb21e, "load master epm remap read error sts=%d", sts);

		epm_io_cmd.pda_list[0] |= epm_remap_tbl->remap_tbl_mirror_mask; //read mirror

		sts = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_READ, epm_io_cmd.bm_pl_list, 4, DU_4K_DEFAULT_MODE, 0);
		if (sts == 0)
		{
			return true;
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0xd17f, "load mirror epm remap read error sts=%d", sts);
			return false;
		}
	}
}
void srch_remap(pda_t *epm_remap, pda_t *remap_src)
{
	//ncl_cmd_trace(LOG_ERR, 0, "srch_remap\n");
	u8 i = 0;
	u32 status = 0;
	for (i = 0; i < EPM_RM_BLK; i++)
	{
		if (remap_src[i] != 0xFFFFFFFF)
		{
			status = epm_ncl_cmd(&remap_src[i], NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
			if (status != 0)
			{
				ncl_cmd_trace(LOG_ALW, 0x5858, "srch_remap blk erase fail pda=0x%x status=0x%x", remap_src[i], status);
				remap_src[i] = 0xFFFFFFFF;
			}
			else
			{
				*epm_remap = remap_src[i];
				remap_src[i] = 0xFFFFFFFF;
				ncl_cmd_trace(LOG_ERR, 0x9cfc, "remap blk erase success epm_remap=0x%x\n",*epm_remap);
				chk_pda(*epm_remap);
				break;
			}
		}
	}
	epm_remap_tbl_flush(&epm_remap_tbl->remap_last_pda);
}
void chk_epm_pda_remap(pda_t *pda_list, pda_t *noremap_pda_list, u8 count)
{
	u8 i = 0, remap_idx = 0, du = 0, ch = 0;
	u16 pg = 0;

	ch = pda2ch(pda_list[i]);
	//printk("chk_epm_pda_remap ln=%d\n",ln);
	for (i = 0; i < count; i++)
	{
		noremap_pda_list[i] = pda_list[i];
		//remap_idx = pda2plane(pda_list[i]) + (pda2ch(pda_list[i]) - 6) * 2 + pda2ce(pda_list[i]) * 4;
		remap_idx = pda2plane(pda_list[i]);
		//printk("remap_idx=%d, pda_list[i] 0x%x, i %d \n",remap_idx, pda_list[i], i);

		if (ch == 6)
		{
			if (epm_remap_tbl->epm_remap[remap_idx] != 0xFFFFFFFF)
			{
				//printk("epm_remap[%d]=0x%x\n",remap_idx,epm_remap_tbl->epm_remap[remap_idx]);
				du = (pda_list[i] >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
				pg = pda2page(pda_list[i]);
				pda_list[i] = epm_remap_tbl->epm_remap[remap_idx] | (du << nand_info.pda_du_shift) | (pg << nand_info.pda_page_shift);
				//printk(" Endion Andy1 pda_list[i] 0x%x, i %d, du %d, pg %d \n",pda_list[i], i, du, pg);
				//ncl_cmd_trace(LOG_ALW, 0, "sucessful");
			}
		}
		else if (ch == 7)
		{
			if (epm_remap_tbl->epm_mirror_remap[remap_idx] != 0xFFFFFFFF)
			{
				//printk("epm_remap[%d]=0x%x\n",remap_idx,epm_remap_tbl->epm_remap[remap_idx]);
				du = (pda_list[i] >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
				pg = pda2page(pda_list[i]);
				pda_list[i] = epm_remap_tbl->epm_mirror_remap[remap_idx] | (du << nand_info.pda_du_shift) | (pg << nand_info.pda_page_shift);
				//printk(" Endion Andy2 pda_list[i] 0x%x, i %d, du %d, pg %d \n",pda_list[i], i, du, pg);
			}
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0xc24c, "Attention !!! no this case");
		}
	}
}
void epm_init_remap()
{
	ncl_cmd_trace(LOG_ALW, 0x0737, "epm_init_remap");
	u32 i = 0, status = 0;
	//u32 j = 0;
	//u8 pl = 0, ch = 2, ce = 2, lun = 0;
	//pda_t pda = 0;
	rda_t remap_tbl_rda[2];
	remap_tbl_rda[0].row = 0x00000400; //0x00000000
	remap_tbl_rda[0].ch = 0x4;		   //0x2
	remap_tbl_rda[0].dev = 0;		   //4
	remap_tbl_rda[0].du_off = 0;
	remap_tbl_rda[0].pb_type = 0;
	remap_tbl_rda[1].row = 0x00000600;
	remap_tbl_rda[1].ch = 0x4; //0x2
	remap_tbl_rda[1].dev = 0;  //4
	remap_tbl_rda[1].du_off = 0;
	remap_tbl_rda[1].pb_type = 0;
	for (i = 0; i < 2; i++)
	{
		epm_remap_tbl->remap_tbl_blk[i] = nal_rda_to_pda(&remap_tbl_rda[i]);
		chk_pda(epm_remap_tbl->remap_tbl_blk[i]);
	}
	epm_remap_tbl->remap_last_pda = epm_remap_tbl->remap_tbl_blk[0];
	epm_remap_tbl->remap_tbl_mirror_mask = (epm_remap_tbl->remap_tbl_blk[0]) ^ (epm_remap_tbl->remap_tbl_blk[1]); //1 << nand_info.pda_plane_shift;  //   nand_info.pda_lun_shift
	if (epm_remap_tbl_load())																					  //true is valid
	{
#if FRB_remap_enable	
		ncl_cmd_trace(LOG_ALW, 0xb9db, "EPMRemap rmap0 0x%x, rmap1 0x%x, Sour0 0x%x, Sour1 0x%x",epm_remap_tbl->frb_remap[0], epm_remap_tbl->frb_remap[1], epm_remap_tbl->frb_remap_source[0], epm_remap_tbl->frb_remap_source[1]);	
	
	if( ((epm_remap_tbl->frb_remap[0] == 0x284) || (epm_remap_tbl->frb_remap[0] == 0xFFFFFFFF )) &&
		((epm_remap_tbl->frb_remap[1] == 0x2C4) || (epm_remap_tbl->frb_remap[1] == 0xFFFFFFFF )) &&
		((epm_remap_tbl->frb_remap_source[0] == 0x284) || (epm_remap_tbl->frb_remap_source[0] == 0xFFFFFFFF )) &&
		((epm_remap_tbl->frb_remap_source[1] == 0x2C4) || (epm_remap_tbl->frb_remap_source[1] == 0xFFFFFFFF )) )		
	{
		ncl_cmd_trace(LOG_ALW, 0xfa2b, "load reamp tbl, frb remap normal case");
	}
	else
	{
		i = 0;
                lun = 1;
		ch = 0;
		pl = 1;
		pda = 0;
		
		for (ce = 2; ce < 4; ce++)
		{
			pda = epm_gen_pda(0, pl, ch, ce, lun, 0, 0);
			epm_remap_tbl->frb_remap_source[i++] = pda;
		}

		for (i = 0; i < 2; i++)
		{
			epm_remap_tbl->frb_remap[i] = 0xFFFFFFFF;
		}
		ncl_cmd_trace(LOG_ALW, 0x2660, "load reamp tbl, frb remap abnormal case");
		epm_remap_tbl_flush(&epm_remap_tbl->remap_last_pda);	
	}
#endif		
		ncl_cmd_trace(LOG_ALW, 0xd2c1, "load_epm_remap success");
	}
	else
	{
		ncl_cmd_trace(LOG_ALW, 0x75fe, "load invalid init epm_remap");
		status = epm_ncl_cmd(&epm_remap_tbl->remap_tbl_blk[0], NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x9c2f, "master erase error");

		status = epm_ncl_cmd(&epm_remap_tbl->remap_tbl_blk[1], NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x5b1c, "mirror erase error");

		memset(epm_remap_tbl, 0, sizeof(epm_remap_tbl_t));
		
		for (i = 0; i < 2; i++)
		{
			epm_remap_tbl->remap_tbl_blk[i] = nal_rda_to_pda(&remap_tbl_rda[i]);
			chk_pda(epm_remap_tbl->remap_tbl_blk[i]);
		}
		epm_remap_tbl->remap_last_pda = epm_remap_tbl->remap_tbl_blk[0];
		epm_remap_tbl->remap_tbl_mirror_mask = (epm_remap_tbl->remap_tbl_blk[0]) ^ (epm_remap_tbl->remap_tbl_blk[1]); 
		
		epm_remap_tbl->epm_remap_sn = 0;
		epm_remap_tbl->EPM_remap_tag = epm_head_tag;
		epm_remap_tbl->valid_tag = epm_valid_tag;
		//
		//i = 0;
		//j = 0;
		/*for (lun = 0; lun < 2; lun++)
		{
			pda = 0;
			for (ch = 3; ch < 4; ch++)	   //for (ch = 2; ch < 4; ch++)
				for (ce = 2; ce < 6; ce++) //for (ce = 2; ce < 4; ce++)
					for (pl = 0; pl < 2; pl++)
					{
						pda = epm_gen_pda(0, pl, ch, ce, lun, 0, 0);
						if (lun == 0)
						{
							//epm_remap_tbl->epm_mirror_remap_source[j++] = pda;
							epm_remap_tbl->epm_remap_source[i++] = pda;
						}
						else
						{
							//epm_remap_tbl->epm_remap_source[i++] = pda;
							epm_remap_tbl->epm_mirror_remap_source[j++] = pda;
						}
					}
		}*/

		epm_remap_tbl->epm_remap_source[0] = epm_gen_pda(0, 2, 5, 0, 0, 0, 0);  //epm_gen_pda(0, pl, ch, ce, lun, 0, 0)
		epm_remap_tbl->epm_remap_source[1] = epm_gen_pda(0, 3, 5, 0, 0, 0, 0);
		epm_remap_tbl->epm_remap_source[2] = epm_gen_pda(0, 2, 6, 0, 0, 0, 0);
		epm_remap_tbl->epm_mirror_remap_source[0] = epm_gen_pda(0, 3, 6, 0, 0, 0, 0);
		epm_remap_tbl->epm_mirror_remap_source[1] = epm_gen_pda(0, 2, 7, 0, 0, 0, 0);
		epm_remap_tbl->epm_mirror_remap_source[2] = epm_gen_pda(0, 3, 7, 0, 0, 0, 0);
		
		for (i = 0; i < 8; i++)
		{
			epm_remap_tbl->epm_remap[i] = 0xFFFFFFFF;
			epm_remap_tbl->epm_mirror_remap[i] = 0xFFFFFFFF;
		}
		
#if FRB_remap_enable
		i = 0;
                lun = 1;
		ch = 0;
		pl = 1;
		pda = 0;
		
		for (ce = 2; ce < 4; ce++)
		{
			pda = epm_gen_pda(0, pl, ch, ce, lun, 0, 0);
			epm_remap_tbl->frb_remap_source[i++] = pda;
		}

		for (i = 0; i < 2; i++)
		{
			epm_remap_tbl->frb_remap[i] = 0xFFFFFFFF;
		}
#endif		
		epm_remap_tbl_flush(&epm_remap_tbl->remap_last_pda);
	}
}
#endif

u8 rebuild_epm_header(latest_epm_data_t latest_epm_data, latest_epm_data_t latest_epm_mirror_data)
{
	srb_t *srb = (srb_t *)SRAM_BASE;
	u32 i = 1;
	pda_t latest_start_pda;
	bool master_all_flush_valid = true;
	bool mirror_all_flush_valid = true;
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	epm_header_data->EPM_Head_tag = epm_head_tag;
	epm_header_data->valid_tag = epm_valid_tag;
	epm_header_data->epm_header_mirror_mask = nal_rda_to_pda(&srb->epm_header_pos[0]) ^ nal_rda_to_pda(&srb->epm_header_mir_pos[0]);
	epm_header_data->epm_mirror_mask = nal_rda_to_pda(&srb->epm_pos[0]) ^ nal_rda_to_pda(&srb->epm_pos_mir[0]);
	u32 master_max_sn = 0;
	u32 mirror_max_sn = 0;
	for (i = FTL_sign; i < EPM_sign_end; i++)
	{
		if (latest_epm_data.latest_epm_sn[i] > master_max_sn)
			master_max_sn = latest_epm_data.latest_epm_sn[i];
		if (latest_epm_mirror_data.latest_epm_sn[i] > mirror_max_sn)
			mirror_max_sn = latest_epm_mirror_data.latest_epm_sn[i];

		if (latest_epm_data.latest_epm_sn[i] >= latest_epm_mirror_data.latest_epm_sn[i])
		{
			latest_start_pda = latest_epm_data.latest_epm_data_pda[i];
		}
		else
		{
			latest_start_pda = latest_epm_mirror_data.latest_epm_data_pda[i];
		}

		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0x63a7, "master epm_sign %d last_pda 0x%x SN %d", i, latest_epm_data.latest_epm_data_pda[i], latest_epm_data.latest_epm_sn[i]);
			chk_pda(latest_epm_data.latest_epm_data_pda[i]);
			ncl_cmd_trace(LOG_ALW, 0xcf98, "mirror epm_sign %d last_pda 0x%x SN %d", i, latest_epm_mirror_data.latest_epm_data_pda[i], latest_epm_mirror_data.latest_epm_sn[i]);
			chk_pda(latest_epm_mirror_data.latest_epm_data_pda[i]);
		}

		if ((latest_epm_data.latest_epm_sn[i] == 0) && (latest_epm_data.latest_epm_data_pda[i] == invalid_epm))
		{
			master_all_flush_valid = false;
		}
		if ((latest_epm_mirror_data.latest_epm_sn[i] == 0) && (latest_epm_mirror_data.latest_epm_data_pda[i] == invalid_epm))
		{
			mirror_all_flush_valid = false;
		}

		set_epm_header(i, latest_start_pda, invalid_epm);
		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0xe3c4, "epm_sign[%d],latest_start_pda[0x%x]", i, latest_start_pda);
			chk_pda(latest_start_pda);
		}
	}

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x244b, "master_max_sn=%d mirror_max_sn=%d", master_max_sn, mirror_max_sn);

	/*pda_t epm_head_empty_pda = srh_empty_epm_header_pda(nal_rda_to_pda(&srb->epm_header_pos[0]));
	epm_header_data-> epm_header_last_pda = epm_head_empty_pda;
	epm_header_data-> epm_last_pda = latest_epm_data.empty_pda;*/

	if ((master_all_flush_valid == true) && (mirror_all_flush_valid == true))
	{
		if (master_max_sn >= mirror_max_sn)
		{
			epm_header_data->EPM_SN = master_max_sn;
			ncl_cmd_trace(LOG_ALW, 0xb17a, "master and mirror all flush-ONLY_MASTER");
			return ONLY_MASTER;
		}
		else
		{
			epm_header_data->EPM_SN = mirror_max_sn;
			ncl_cmd_trace(LOG_ALW, 0xbfcd, "master and mirror all flush-ONLY_MIRROR");
			return ONLY_MIRROR;
		}
	}
	else
	{
		if (master_all_flush_valid == true)
		{
			epm_header_data->EPM_SN = master_max_sn;
			ncl_cmd_trace(LOG_ALW, 0x1622, "only master all flush-ONLY_MASTER");
			return ONLY_MASTER;
		}
		else if (mirror_all_flush_valid == true)
		{
			epm_header_data->EPM_SN = mirror_max_sn;
			ncl_cmd_trace(LOG_ALW, 0x0446, "only mirror all flush-ONLY_MIRROR");
			return ONLY_MIRROR;
		}
		else
		{
			epm_header_data->EPM_SN = master_max_sn;
			ncl_cmd_trace(LOG_ALW, 0x4dd0, "master and mirror both no flush-ONLY_MASTER");
			ncl_cmd_trace(LOG_ALW, 0xa7e5, "should not have this case!!!"); // if plp maybe?
			return ONLY_MASTER;
		}
	}
}

u32 scan_the_latest_epm_data(latest_epm_data_t *latest_epm_data, u8 mode)
{
	ncl_cmd_trace(LOG_ALW, 0x910c, "scan_the_latest_epm_data mode=%d", mode);
	srb_t *srb = (srb_t *)SRAM_BASE;
	u32 the_latest_sn = 0;
	pda_t pda_base = 0;

	if (mode == ONLY_MASTER){
		chk_pda(nal_rda_to_pda(&srb->epm_pos[0]));
		pda_base = nal_rda_to_pda(&srb->epm_pos[0]);}
	else{
		chk_pda(nal_rda_to_pda(&srb->epm_pos_mir[0]));
		pda_base = nal_rda_to_pda(&srb->epm_pos_mir[0]);}

	//u32* data = (u32*)ddtag2mem(shr_epm_info->rebuild_ddtag);
	dtag_t dtag_temp;
	u32 dtag_cnt = dtag_get_bulk(DTAG_T_SRAM, 1, &dtag_temp); // need to use sram!!!!! improtant!!!
	u32 *data = dtag2mem(dtag_temp);
	//
	epm_pos_t *epm_pos = NULL;
	u32 total_mp_du_cnt = EPM_BLK_CNT * DU_CNT_PER_PAGE;
	nal_status_t sts = 0;

	u32 epm_sign = 0, epm_sn = 0, epm_dtag_pg_cnt = 0, mp_du = 0, valid_tag = 0;
	pda_t epm_start_pda = 0;

	bm_pl_t pl;
	//pl.pl.dtag = (DTAG_IN_DDR_MASK | shr_epm_info->rebuild_ddtag);
	pl.pl.dtag = dtag_temp.b.dtag;
	//
	pl.pl.du_ofst = 0;
	pl.pl.btag = 0;
	pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
	pl.pl.nvm_cmd_id = EpmMetaIdx;

	while (1)
	{
		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0x45d0, "read pda_base = 0x%x", pda_base);
			chk_pda(pda_base);
		}

		if (pda2page(pda_base) >= nand_page_num_slc())
		{
			ncl_cmd_trace(LOG_ALW, 0x82d6, "scan end page>slc page page[%d]", pda2page(pda_base));
			break;
		}

#if epm_remap_enable
		chk_epm_pda_remap(&pda_base, &epm_io_cmd.noremap_pda_list[0], 1);
#endif
		sts = epm_ncl_cmd(&pda_base, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
		//printk("Endion chk_epm_pda_remap pda_base 0x%x  \n", pda_base);
#if epm_remap_enable
		pda_base = epm_io_cmd.noremap_pda_list[0];
#endif
		if (sts == ficu_err_du_erased) //first init or scab end
		{
			//printk("Endion ficu_err_du_erased pda_base 0x%x  \n", pda_base);
			break;
		}
		else if (sts == 0)
		{
			//ncl_cmd_trace(LOG_ERR, 0, "read success scan_pda=0x%x\n",pda_base);
			epm_sign = *data;
			if (epm_sign >= EPM_sign_end)
			{
				ncl_cmd_trace(LOG_ALW, 0x526b, "first epm_sign fail");
				break;
			}

			epm_sn = *(data + 1);
			epm_start_pda = pda_base;
			epm_pos = get_epm_pos_ptr(epm_sign);
			epm_dtag_pg_cnt = epm_pos->ddtag_cnt / total_mp_du_cnt;
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0x835d, "epm_sign[%d] epm_sn[0x%x] epm_dtag_pg_cnt[%d]", epm_sign, epm_sn, epm_dtag_pg_cnt);
			for (mp_du = 0; mp_du < epm_dtag_pg_cnt; mp_du++)
			{
				get_rd_epm_pda(&pda_base, epm_io_cmd.pda_list, total_mp_du_cnt);
			}
#if epm_remap_enable //remap
			chk_epm_pda_remap(&epm_io_cmd.pda_list[total_mp_du_cnt - 1], &epm_io_cmd.noremap_pda_list[0], 1);
#endif
			sts = epm_ncl_cmd(&epm_io_cmd.pda_list[total_mp_du_cnt - 1], NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);

#if epm_remap_enable
			epm_io_cmd.pda_list[total_mp_du_cnt - 1] = epm_io_cmd.noremap_pda_list[0];
#endif
			if (sts == ficu_err_du_erased) //first init
			{
				ncl_cmd_trace(LOG_ALW, 0xce97, "epm_sign[%d] head valid,tail erase read empty scan_pda[0x%x]", epm_sn, pda_base);
				break;
			}
			else if (sts == 0)
			{
				valid_tag = *(data + ((4096 / 4) - 1));

				if (valid_tag == epm_valid_tag)
				{
					the_latest_sn = epm_sn;
					latest_epm_data->latest_epm_sn[epm_sign] = epm_sn;
					latest_epm_data->latest_epm_data_pda[epm_sign] = epm_start_pda;
				}
				else
				{
					ncl_cmd_trace(LOG_ALW, 0x6893, "valid_tag invalid");
					break;
				}
			}
			else
			{
				ncl_cmd_trace(LOG_ALW, 0xada4, "epm_sign[%d] tail read pda[0x%x] fail, status[0x%x]", epm_sn, pda_base, sts);
				break;
			}
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0x19d2, "read head pda[0x%x] fail, status[0x%x]", pda_base, sts);
			break;
		}
	}

	if (the_latest_sn != 0)
	{
		while (1)
		{
			if (pda2page(pda_base) >= nand_page_num_slc())
			{
				ncl_cmd_trace(LOG_ALW, 0x9635, "scan empty page end page>slc page page[%d]", pda2page(pda_base));
				if (epm_debug_log)
					chk_pda(pda_base);
				latest_epm_data->empty_pda = pda_base; //no any empty
				break;
			}
#if epm_remap_enable //remap
			chk_epm_pda_remap(&pda_base, &epm_io_cmd.noremap_pda_list[0], 1);
#endif
			sts = epm_ncl_cmd(&pda_base, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);

#if epm_remap_enable
			pda_base = epm_io_cmd.noremap_pda_list[0];
#endif
			if (sts == ficu_err_du_erased)
			{
				latest_epm_data->empty_pda = pda_base;
				if (epm_debug_log)
				{
					ncl_cmd_trace(LOG_ALW, 0xd300, "epm_last_empty_pda[0x%x]", latest_epm_data->empty_pda);
					chk_pda(latest_epm_data->empty_pda);
				}
				break;
			}
			get_rd_epm_pda(&pda_base, epm_io_cmd.pda_list, total_mp_du_cnt);
		}
	}

	dtag_put_bulk(DTAG_T_SRAM, dtag_cnt, &dtag_temp);
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x46f2, "end the_latest_sn=%d", the_latest_sn);
	return the_latest_sn; //0 is not found
}

pda_t scan_the_latest_epm_header()
{
	ncl_cmd_trace(LOG_ALW, 0xf736, "scan_the_latest_epm_header");

	srb_t *srb = (srb_t *)SRAM_BASE;
	pda_t the_latest_pda = 0xFFFFFFFF;
	pda_t pda_base = nal_rda_to_pda(&srb->epm_header_pos[0]);
	u32 start_pg = 0;
	u32 end_pg = nand_page_num_slc(); //384
	u32 scan_pg = 0;

	bm_pl_t pl;
	pl.pl.dtag = (DTAG_IN_DDR_MASK | shr_epm_info->rebuild_ddtag);
	pl.pl.du_ofst = 0;
	pl.pl.btag = 0;
	pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
	pl.pl.nvm_cmd_id = EpmMetaIdx;
	while (start_pg != end_pg)
	{
		scan_pg = (start_pg + end_pg) / 2;
		pda_t scan_pda = pda_base + ((scan_pg * nal_get_interleave()) << DU_CNT_SHIFT);

		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0x8b35, "scan_pg=%d", scan_pg);
			ncl_cmd_trace(LOG_ALW, 0x6280, "scan_pda=%d", scan_pda);
			chk_pda(scan_pda);
		}

		if (start_pg == scan_pg && scan_pg != 0)
		{
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0xb1ab, "start_pg == scan_pg");
			break;
		}
		nal_status_t sts = epm_ncl_cmd(&scan_pda, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
		if (sts == ficu_err_du_erased)
		{
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0xba1c, "read empty scan_pda=0x%x", scan_pda);
			end_pg = scan_pg;
			if (scan_pg == 0)
			{
				if (epm_debug_log)
					ncl_cmd_trace(LOG_ALW, 0x262d, "no valid");
				scan_pg = 0xFFFFFFFF;
				break;
			}
		}
		else if (sts == 0)
		{
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0xad08, "read success scan_pda=0x%x", scan_pda);
			start_pg = scan_pg;
			if (scan_pg == 0)
				break;
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0x3b38, "read pda=0x%x fail status=0x%x", scan_pda, sts);
			//panic("search latest header read fail not handle");
			return invalid_epm;
		}
	}

	if (scan_pg != 0xFFFFFFFF)
	{
		//check plane 1
		pda_t pda_p0 = pda_base + ((scan_pg * nal_get_interleave()) << DU_CNT_SHIFT);
		/*
		pda_t pda_p1 = (pda_p0) | (1 << nand_info.pda_plane_shift);
		nal_status_t sts = epm_ncl_cmd(&pda_p1, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
		if (sts == ficu_err_du_erased)
		{
			the_latest_pda = pda_p0;
		}
		else if (sts == 0)
		{
			the_latest_pda = pda_p1;
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0, "read p1 pda=0x%x fail status=0x%x", pda_p1, sts);
			ncl_cmd_trace(LOG_ALW, 0, "search latest header read pl 1 fail not handle");
		}
		*/
		the_latest_pda = pda_p0;
		if (epm_debug_log)
			ncl_cmd_trace(LOG_ALW, 0xeec9, "search end scan_pg=%d, the_latest_pda=0x%x", scan_pg, the_latest_pda);
	}
	return the_latest_pda; //0xFFFFFFFF is not found
}

bool epm_header_load(pda_t pda_base)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x844f, "epm_header_load");
	u32 pg = 0, i = 0;
	for (pg = 0; pg < (shr_epm_info->epm_header.ddtag_cnt / DU_CNT_PER_PAGE); pg++)
	{
		get_rd_epm_header_pda(&pda_base, epm_io_cmd.pda_list, DU_CNT_PER_PAGE);
		for (i = 0; i < DU_CNT_PER_PAGE; i++)
		{
			epm_io_cmd.bm_pl_list[i].pl.dtag = (DTAG_IN_DDR_MASK | (shr_epm_info->epm_header.ddtag + (pg * DU_CNT_PER_PAGE) + (i)));
			epm_io_cmd.bm_pl_list[i].pl.du_ofst = i;
			epm_io_cmd.bm_pl_list[i].pl.btag = 0;
			epm_io_cmd.bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
			epm_io_cmd.bm_pl_list[i].pl.nvm_cmd_id = EpmMetaIdx;
			nal_status_t sts = epm_ncl_cmd(&epm_io_cmd.pda_list[i], NCL_CMD_OP_READ, &epm_io_cmd.bm_pl_list[i], 1, DU_4K_DEFAULT_MODE, 0);

			if (sts != ficu_err_good)
			{
				//if (sts == ficu_err_nard)
				//{
				//	ncl_cmd_trace(LOG_ERR, 0, "[ERR] EPM header not do ARD first");
				//	sts = epm_ncl_cmd(&epm_io_cmd.pda_list[i], NCL_CMD_OP_READ, &epm_io_cmd.bm_pl_list[i], 1, DU_4K_DEFAULT_MODE, 0);
				//	if (sts == ficu_err_good)
				//		continue;
				//}
				//ncl_cmd_trace(LOG_ALW, 0, "load epm header read error sts=%d", sts);
				if (epm_debug_log)
					ncl_cmd_trace(LOG_ALW, 0x90b7, "load epm header read error sts=%d", sts);
				return false;
			}
		}
	}

	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	if (epm_header_data->valid_tag == epm_valid_tag)
	{
		return true;
	}
	else
	{
		if (epm_debug_log)
			ncl_cmd_trace(LOG_ALW, 0xf4c9, "load epm header valid_tag invalid");
		return false;
	}
}

u8 chk_epm_header_valid()
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x37d2, "chk_epm_header_valid");
	srb_t *srb = (srb_t *)SRAM_BASE;
	chk_pda(nal_rda_to_pda(&srb->epm_header_pos[0]));
	bool master_head = epm_header_load(nal_rda_to_pda(&srb->epm_header_pos[0]));
	//bool mirror_head = epm_header_load(nal_rda_to_pda(&srb->epm_header_mir_pos[0]));

	u32 *epm_header_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	memset(epm_header_ptr, 0x00, (shr_epm_info->epm_header.ddtag_cnt * DTAG_SZE));

	if (master_head)
	{
		ncl_cmd_trace(LOG_ALW, 0xc54f, "master and mirror valid");
		return ONLY_MASTER;
	}
	/*
	if (master_head && mirror_head)
	{
		ncl_cmd_trace(LOG_ALW, 0, "master and mirror valid");
		return ALL_FLUSH;
	}
	else if (master_head && !(mirror_head))
	{
		ncl_cmd_trace(LOG_ALW, 0, "master valid and mirror invalid");
		return ONLY_MASTER;
	}
	else if ((!master_head) && mirror_head)
	{
		ncl_cmd_trace(LOG_ALW, 0, "master invalid and mirror valid");
		return ONLY_MIRROR;
	}
	*/
	else
	{
		ncl_cmd_trace(LOG_ALW, 0x7672, "master invalid and mirror invalid");
		return 0xFF;
	}
}

void epm_header_flush(u8 flush_mode, pda_t only_mirror_pda, bool sn_update)
{
	//ncl_cmd_trace(LOG_ALW, 0, "epm_header_flush");
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	pda_t *pda_base;
	u32 pg = 0, i = 0;
	if (sn_update)
		epm_header_data->EPM_SN++;

	if (flush_mode == ONLY_MIRROR)
	{
		if (epm_debug_log)
			ncl_cmd_trace(LOG_ALW, 0x16b3, "ONLY_MIRROR");
		pda_base = &only_mirror_pda;
	}
	else
	{
		pda_base = &(epm_header_data->epm_header_last_pda);
	}

	if(epm_io_header_cmd[epm_header_idx].ncl_cmd.completion != NULL)
	{
		epm_header_idx ^= 1;
		//ncl_cmd_trace(LOG_ALW, 0x617c, "[EPM] header ncl cmd index change :%d", epm_header_idx);
	}

	for (pg = 0; pg < (shr_epm_info->epm_header.ddtag_cnt / DU_CNT_PER_PAGE); pg++)
	{
		get_wr_epm_header_pda(pda_base, epm_io_header_cmd[epm_header_idx].pda_list);

		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0xd64b, "get pda0=0x%x", epm_io_header_cmd[epm_header_idx].pda_list[0]);
			chk_pda(epm_io_header_cmd[epm_header_idx].pda_list[0]);
		}

		for (i = 0; i < DU_CNT_PER_PAGE; i++)
		{
			epm_io_header_cmd[epm_header_idx].bm_pl_list[i].pl.dtag = (DTAG_IN_DDR_MASK | (shr_epm_info->epm_header.ddtag + (pg * DU_CNT_PER_PAGE) + (i)));
			epm_io_header_cmd[epm_header_idx].bm_pl_list[i].pl.du_ofst = i;
			epm_io_header_cmd[epm_header_idx].bm_pl_list[i].pl.btag = 0;
			epm_io_header_cmd[epm_header_idx].bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
			epm_io_header_cmd[epm_header_idx].bm_pl_list[i].pl.nvm_cmd_id = EpmMetaIdx;
		}
		epm_header_ncl_cmd(epm_io_header_cmd[epm_header_idx].pda_list, NCL_CMD_OP_WRITE, epm_io_header_cmd[epm_header_idx].bm_pl_list, 1, DU_4K_DEFAULT_MODE);

		if (flush_mode == ALL_FLUSH)
		{
			/*
			//write mirror
			epm_io_cmd.pda_list[0] |= epm_header_data->epm_header_mirror_mask;
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0, "get mirror pda0=0x%x", epm_io_cmd.pda_list[0]);

			epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE, 0);
			*/
			ncl_cmd_trace(LOG_ERR, 0x6a46, "all flush fail");
			panic("\n");
		}
	}
}

ddr_code void power_on_epm_flush_done()
{
	power_on_epm_back_counter--;
	if(power_on_epm_back_counter == 0)
	{
		ncl_cmd_trace(LOG_ERR, 0x1c6e, "power on epm call back");
		power_on_update_epm_flag = POWER_ON_EPM_UPDATE_DONE;
	}

}

#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
void slc_epm_flush_done()
{
	slc_epm_back_counter--;
	if(slc_epm_back_counter == 0)
	{
		ncl_cmd_trace(LOG_ALW, 0x8a1e, "slc epm call back");
		slc_call_epm_update = false;
	}

}
#endif
void epm_flush_done()
{
	epm_back_counter--;
	if(epm_back_counter == 0)
	{
		ncl_cmd_trace(LOG_ERR, 0x1ee4, "epm call back");
		plp_epm_update_done = true;
		epm_plp_flag = false;
	}
}

void epm_flush_done_for_glist()  //The callback function for GLIST_sign_flag.
{
    extern u8 epm_Glist_updating;
    
	if(epm_Glist_updating == 1)
	{
		epm_Glist_updating = 0;  //The epm Glist has been updaed. Reset epm_Glist_updating = 0.
	}
}

void epm_header_update()
{
	//if block full case
	if (!plp_trigger)
		ncl_cmd_trace(LOG_ALW, 0xabf5, "epm_header_update");
	srb_t *srb = (srb_t *)SRAM_BASE;
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);

	u32 need_pg_num = (shr_epm_info->epm_header.ddtag_cnt * 2) / DU_CNT_PER_PAGE;
	if(!epm_plp_flag)
		need_pg_num++;
	u32 remain_pg = nand_page_num_slc() - pda2page(epm_header_data->epm_header_last_pda);

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x3bb9, "need_pg_num = %d , remain_pg = %d", need_pg_num, remain_pg);

	if (remain_pg < need_pg_num)
	{
		ncl_cmd_trace(LOG_ALW, 0x9e86, "epm_header space not enough erase and reflush");
		if(epm_plp_flag)
			panic("\n");
		epm_erase(EPM_HEADER_TAG);
		epm_header_data->epm_header_last_pda = nal_rda_to_pda(&srb->epm_header_pos[0]);
		pda_t only_mirror_pda = (nal_rda_to_pda(&srb->epm_header_pos[0])) | epm_header_data->epm_header_mirror_mask;
		epm_header_flush(ONLY_MASTER, 0, true);
		//if seccess
		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0xab7e, "only mirror pda=0x%x", only_mirror_pda);
			chk_pda(only_mirror_pda);
		}
		//epm_erase(EPM_HEADER_MIRROR_TAG);
		//epm_header_flush(ONLY_MIRROR, only_mirror_pda, false);
	}
	else
	{
		epm_header_flush(ONLY_MASTER, 0, true);
	}
	if (!plp_trigger)
		ncl_cmd_trace(LOG_ALW, 0xd5f9, "epm_header_update done");
    //plp_epm_update_done = true;
}


void set_header(epm_header_t *epm_type_header, epm_header_t *epm_header)
{
	epm_type_header->EPM_Head_tag = epm_header->EPM_Head_tag;
	epm_type_header->EPM_SN = epm_header->EPM_SN;
	epm_type_header->epm_ftl_header = epm_header->epm_ftl_header;
	epm_type_header->epm_glist_header = epm_header->epm_glist_header;
	epm_type_header->epm_smart_header = epm_header->epm_smart_header;
	epm_type_header->epm_namespace_header = epm_header->epm_namespace_header;
	epm_type_header->epm_aes_header = epm_header->epm_aes_header;
	epm_type_header->epm_trim_header = epm_header->epm_trim_header;
	epm_type_header->epm_journal_header = epm_header->epm_journal_header;
	//epm_type_header->epm_misc_header = epm_header->epm_misc_header;
	epm_type_header->epm_nvme_data_header = epm_header->epm_nvme_data_header;
	epm_type_header->epm_header_last_pda = epm_header->epm_header_last_pda;
	epm_type_header->epm_last_pda = epm_header->epm_last_pda;
	epm_type_header->epm_header_mirror_mask = epm_header->epm_header_mirror_mask;
	epm_type_header->epm_mirror_mask = epm_header->epm_mirror_mask;
	epm_type_header->valid_tag = epm_header->valid_tag;

}


void set_epm_data(u32 epm_sign)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0xee99, "set_epm_data");

	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	switch (epm_sign)
	{
	case FTL_sign:
	{
		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		epm_ftl_data->epm_sign = FTL_sign;
		epm_ftl_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&epm_ftl_data->header, epm_header_data);
	}
	break;
	case GLIST_sign:
	{
		epm_glist_t *epm_glist_data = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		epm_glist_data->epm_sign = GLIST_sign;
		epm_glist_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&epm_glist_data->header, epm_header_data);
	}
	break;
	case SMART_sign:
	{
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		epm_smart_data->epm_sign = SMART_sign;
		epm_smart_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&epm_smart_data->header, epm_header_data);
	}
	break;
	case NAMESPACE_sign:
	{
		epm_namespace_t *epm_namespace_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
		epm_namespace_data->epm_sign = NAMESPACE_sign;
		epm_namespace_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&epm_namespace_data->header, epm_header_data);
	}
	break;
	case AES_sign:
	{
		epm_aes_t *epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		epm_aes_data->epm_sign = AES_sign;
		epm_aes_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&epm_aes_data->header, epm_header_data);
	}
	break;
	case TRIM_sign:
	{
		epm_trim_t *epm_trim_data = (epm_trim_t *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		epm_trim_data->epm_sign = TRIM_sign;
		epm_trim_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&epm_trim_data->header, epm_header_data);
	}
	break;
	case JOURNAL_sign:
	{
		epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
		epm_journal_data->epm_sign = JOURNAL_sign;
		epm_journal_data->EPM_SN = epm_header_data->EPM_SN + 1;
		epm_journal_data->header = *epm_header_data;
	}
	break;
	/*case MISC_sign:
	{
		epm_misc_t *epm_misc_data = (epm_misc_t *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
		epm_misc_data->epm_sign = MISC_sign;
		epm_misc_data->EPM_SN = epm_header_data->EPM_SN + 1;
		epm_misc_data->header = *epm_header_data;
	}
	break;*/
	case ERROR_WARN_sign:
	{
		epm_error_warn_t *_epm_nvme_data = (epm_error_warn_t *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
		_epm_nvme_data->epm_sign = ERROR_WARN_sign;
		_epm_nvme_data->EPM_SN = epm_header_data->EPM_SN + 1;
		set_header(&_epm_nvme_data->header, epm_header_data);
	}
	break;
	/*case TCG_INFO_sign:
	{
		epm_tcg_info_t *epm_tcg_info = (epm_tcg_info_t *)ddtag2mem(shr_epm_info->epm_tcg_info.ddtag);
		epm_tcg_info->epm_sign = TCG_INFO_sign;
		epm_tcg_info->EPM_SN = epm_header_data->EPM_SN + 1;
		epm_tcg_info->header = *epm_header_data;
	}
	break;*/
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:		
		ncl_cmd_trace(LOG_ALW, 0xbe1d, "set_epm_data powercycle case");
		break;
	default:
		panic("no this case\n");
	}
}

void set_epm_header(u32 epm_sign, pda_t pda_header, pda_t pda_tail)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x7459, "set_epm_header");
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	epm_sub_header_t epm_sub_header;
	epm_sub_header.epm_header_tag = epm_sign;
	epm_sub_header.epm_newest_head = pda_header;
	epm_sub_header.epm_newest_tail = pda_tail;
	epm_sub_header.valid_tag = epm_valid_tag;

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x26fb, "epm_header_tag=0x%x, epm_newest_head=0x%x, epm_newest_tail=0x%x", epm_sub_header.epm_header_tag, epm_sub_header.epm_newest_head, epm_sub_header.epm_newest_tail);

	switch (epm_sign)
	{
	case FTL_sign:
		epm_header_data->epm_ftl_header = epm_sub_header;
		break;
	case GLIST_sign:
		epm_header_data->epm_glist_header = epm_sub_header;
		break;
	case SMART_sign:
		epm_header_data->epm_smart_header = epm_sub_header;
		break;
	case NAMESPACE_sign:
		epm_header_data->epm_namespace_header = epm_sub_header;
		break;
	case AES_sign:
		epm_header_data->epm_aes_header = epm_sub_header;
		break;
	case TRIM_sign:
		epm_header_data->epm_trim_header = epm_sub_header;
		break;
	case JOURNAL_sign:
		epm_header_data->epm_journal_header = epm_sub_header;
		break;
	/*case MISC_sign:
		epm_header_data->epm_misc_header = epm_sub_header;
		break;*/
	case ERROR_WARN_sign:
		epm_header_data->epm_nvme_data_header = epm_sub_header;
		break;
	/*case TCG_INFO_sign:
		epm_header_data->epm_tcg_info_header = epm_sub_header;
		break;*/
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:		
		ncl_cmd_trace(LOG_ALW, 0xb860, "set_epm_header powercycle case");
		break;
	default:
		panic("no this case\n");
	}
}

u32 Chk_EPMData_Tag(u32 epm_sign)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x49ed, "Chk_EPMData_Tag");
	u32 Tag = 0;

	switch (epm_sign)
	{
	case FTL_sign:
	{
		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		Tag = epm_ftl_data->header.valid_tag;
	}
	break;
	case GLIST_sign:
	{
		epm_glist_t *epm_glist_data = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		Tag = epm_glist_data->header.valid_tag;
	}
	break;
	case SMART_sign:
	{
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		Tag = epm_smart_data->header.valid_tag;
	}
	break;
	case NAMESPACE_sign:
	{
		epm_namespace_t *epm_namespace_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
		Tag = epm_namespace_data->header.valid_tag;
	}
	break;
	case AES_sign:
	{
		epm_aes_t *epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		Tag = epm_aes_data->header.valid_tag;
	}
	break;
	case TRIM_sign:
	{
		epm_trim_t *epm_trim_data = (epm_trim_t *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		Tag = epm_trim_data->header.valid_tag;
	}
	break;
	/*case MISC_sign:
	{
		epm_misc_t *epm_misc_data = (epm_misc_t *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
		Tag = epm_misc_data->header.valid_tag;
	}
	break;*/
    case JOURNAL_sign:
	{
		epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
		Tag = epm_journal_data->header.valid_tag;
	}
	break;
	case ERROR_WARN_sign:
	{
		epm_error_warn_t *_epm_nvme_data = (epm_error_warn_t *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
		Tag = _epm_nvme_data->header.valid_tag;
	}
    break;
	/*case TCG_INFO_sign:
	{
		epm_tcg_info_t *epm_tcg_info = (epm_tcg_info_t *)ddtag2mem(shr_epm_info->epm_tcg_info.ddtag);
		Tag = epm_tcg_info->header.valid_tag;
	}
	break;*/
	
	default:
		panic("no this case\n");
	}

	return Tag;
}

void epm_read_all()
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0xe098, "epm_read_all");
	u32 i = 1;

	for (i = FTL_sign; i < EPM_sign_end; i++)
	{
		bool master_data = epm_read(i, ONLY_MASTER);
		if(i == JOURNAL_sign)
		{
			epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
			ncl_cmd_trace(LOG_ALW, 0x9d86, "EPM OFFSET:%d",epm_journal_data->journal_offset);
		}
		//if read fail  read mirror
		if (master_data == false)
		{
			ncl_cmd_trace(LOG_ALW, 0xd533, "epm load master data fail %d", i);
			master_data = epm_read(i, ONLY_MIRROR);

			if (master_data == false)
			{
				ncl_cmd_trace(LOG_ALW, 0x35a0, "epm load mirror data fail %d", i);
				panic("\n");
			}
		}
	}
}

bool epm_read(u32 epm_sign, u8 read_mode)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x2aa6, "epm_read");
	pda_t pda_base;
	epm_sub_header_t epm_pda = get_epm_newest_pda(epm_sign);
	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);

	if (epm_debug_log)
	{
		ncl_cmd_trace(LOG_ALW, 0xbced, "epm_header_tag[0x%x],head_pda[0x%x],dtag[0x%x],dcnt[%d]", epm_pda.epm_header_tag, epm_pda.epm_newest_head, epm_pos->ddtag, epm_pos->ddtag_cnt);
	}

	if (read_mode == ONLY_MIRROR)
	{
		if (epm_debug_log)
			ncl_cmd_trace(LOG_ALW, 0xb8b5, "ONLY_MIRROR");
		pda_base = epm_pda.epm_newest_head | epm_header_data->epm_mirror_mask;
	}
	else
	{
		pda_base = epm_pda.epm_newest_head;
	}

	u32 total_mp_du_cnt = EPM_BLK_CNT * DU_CNT_PER_PAGE;
	u32 mp_du = 0, i = 0;

	for (mp_du = 0; mp_du < (epm_pos->ddtag_cnt / total_mp_du_cnt); mp_du++)
	{
		get_rd_epm_pda(&pda_base, epm_io_cmd.pda_list, total_mp_du_cnt);

#if epm_remap_enable //remap
		chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], total_mp_du_cnt);
		if (epm_debug_log)
		{
			u32 k = 0;
			ncl_cmd_trace(LOG_ALW, 0xfde7, "ncl_submit");
			for (k = 0; k < (PG_CNT_PER_EPM_FLUSH); k++)
			{
				ncl_cmd_trace(LOG_ALW, 0xdf06, "pda_list : ");
				chk_pda(epm_io_cmd.pda_list[k]);
				ncl_cmd_trace(LOG_ALW, 0x8d48, "pda_no_remap_list : ");
				chk_pda(epm_io_cmd.noremap_pda_list[k]);
			}
		}
#endif
		for (i = 0; i < total_mp_du_cnt; i++)
		{
			epm_io_cmd.bm_pl_list[i].pl.dtag = (DTAG_IN_DDR_MASK | (epm_pos->ddtag + (mp_du * total_mp_du_cnt) + (i)));
			epm_io_cmd.bm_pl_list[i].pl.du_ofst = i;
			epm_io_cmd.bm_pl_list[i].pl.btag = 0;
			epm_io_cmd.bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
			epm_io_cmd.bm_pl_list[i].pl.nvm_cmd_id = EpmMetaIdx;
			nal_status_t sts = epm_ncl_cmd(&epm_io_cmd.pda_list[i], NCL_CMD_OP_READ, &epm_io_cmd.bm_pl_list[i], 1, DU_4K_DEFAULT_MODE, 0);
			if (sts == 0)
			{
			}
			else
			{
				ncl_cmd_trace(LOG_ALW, 0xfc79, "read pda[0x%x] fail status[%d]", epm_io_cmd.pda_list[i], sts);
				chk_pda(epm_io_cmd.pda_list[i]);
				return false;
			}
		}
	}

	//need to check data, then judge true or false
	if (Chk_EPMData_Tag(epm_sign) == epm_valid_tag)
	{
		ncl_cmd_trace(LOG_ALW, 0x5e47, "epm load epm data OK sign %d ", epm_sign);
		return true;
	}
	else
	{
		ncl_cmd_trace(LOG_ALW, 0xe34b, "epm load epm data valid_tag invalid sign %d", epm_sign);
		return false;
	}
}

#if epm_spin_lock_enable
void unlock_epm_ddr(u32 epm_sign, u32 cpu_id)
{
	if (epm_sign > EPM_sign_end)
	{
		ncl_cmd_trace(LOG_ALW, 0x9c4e, "unlock_epm_ddr epm_sign[%d]>EPM_sign_end", epm_sign);
		return;
	}
	ncl_cmd_trace(LOG_ALW, 0xc9fd, "unlock_epm_ddr epm_sign[%d] cpu_id[%d]", epm_sign, cpu_id);
	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);
	//sys_assert((epm_pos->busy_lock == 1));
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x04bb, "cur_key=%d finish", epm_pos->cur_key);
	if (epm_pos->busy_lock == 1)
	{
		epm_pos->cur_key++;
		if (epm_pos->cur_key > max_key_num)
			epm_pos->cur_key = 1;
		epm_pos->busy_lock = 0;
	}
}

void get_epm_access_key(u32 cpu_id, u32 epm_sign)
{
	if (epm_sign > EPM_sign_end)
	{
		ncl_cmd_trace(LOG_ALW, 0xcda4, "get_epm_access_key epm_sign[%d]>EPM_sign_end", epm_sign);
		return;
	}
	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x3588, "get_epm_access_key cpu_id[%d[,epm_sign[%d] getkey[%d]", cpu_id, epm_sign, epm_pos->alloc_key);
	epm_pos->key_num[cpu_id] = epm_pos->alloc_key++;
	if (epm_pos->alloc_key > max_key_num)
		epm_pos->alloc_key = 1;
}

void recovery_all_lock(u8 *lock)
{
	ncl_cmd_trace(LOG_ALW, 0x7202, "recovery_all_lock\n");
	u32 i = 0;
	for (i = FTL_sign; i < EPM_sign_end; i++)
	{
		epm_pos_t *epm_pos = get_epm_pos_ptr(i);
		epm_pos->busy_lock = lock[i];
	}
}

void wait_ddr_set_done_and_force_all_lock(u8 *lock)
{
	ncl_cmd_trace(LOG_ALW, 0xe5d2, "wait_ddr_set_done_and_force_all_lock\n");
	u32 i = 0;
	for (i = FTL_sign; i < EPM_sign_end; i++)
	{
		epm_pos_t *epm_pos = get_epm_pos_ptr(i);

		while ((epm_pos->set_ddr_done[0] == 0) || (epm_pos->set_ddr_done[1] == 0 || (epm_pos->set_ddr_done[2] == 0) || (epm_pos->set_ddr_done[3] == 0)))
		{
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0xad87, "sign=%d,0:%d 1:%d 2:%d 3:%d\n", i, epm_pos->set_ddr_done[0], epm_pos->set_ddr_done[1], epm_pos->set_ddr_done[2], epm_pos->set_ddr_done[3]);
		};
		lock[i] = epm_pos->busy_lock;
		epm_pos->busy_lock = 1; //force lock
	}
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x2d4a, "all ddr set done and all lock\n");
}
#endif
void epm_flush_all(u8 flush_mode, pda_t *only_mirror_pda) //no log
{
	epm_flush_fast(0xFF, flush_mode, only_mirror_pda);
}
void epm_flush_fast(u8 epm_sign_bit, u8 flush_mode, pda_t *only_mirror_pda)
{
	epm_pos_t *epm_pos = NULL;
	u32 sign = 1, i = 0, mp_du = 0;
	u32 total_mp_du_cnt = EPM_BLK_CNT * DU_CNT_PER_PAGE;
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	pda_t *pda_base;
	u32 send_pg_cnt = 0;
	pda_t pda_header = 0;
	pda_t pda_tail = 0;
	u32 k = 0;

	if (flush_mode == ONLY_MIRROR)
	{
		if (epm_debug_log)
			ncl_cmd_trace(LOG_ALW, 0x6c5f, "ONLY_MIRROR");
		pda_base = only_mirror_pda;
	}
	else
	{
		pda_base = &(epm_header_data->epm_last_pda);
	}
	//ncl_cmd_trace(LOG_ALW, 0, "Endion CYC1 pda_base 0x%x", *pda_base);
	for (sign = 1; sign < EPM_sign_end; sign++)
	{
		if (epm_sign_bit & 0x1)
		{
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0x554d, "epm_sign[%d] epm_flush_fast", sign);

			epm_pos = get_epm_pos_ptr(sign);

			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0x5a5e, "epm_sign[%d],ddtag[0x%x],dcnt[%d]", sign, epm_pos->ddtag, epm_pos->ddtag_cnt);

			set_epm_data(sign);
			for (mp_du = 0; mp_du < (epm_pos->ddtag_cnt / total_mp_du_cnt); mp_du++)
			{
				get_wr_epm_pda(pda_base, &epm_io_cmd.pda_list[send_pg_cnt]);

				if (mp_du == 0)
					pda_header = epm_io_cmd.pda_list[send_pg_cnt];

				if (mp_du == ((epm_pos->ddtag_cnt / total_mp_du_cnt) - 1))
					pda_tail = epm_io_cmd.pda_list[send_pg_cnt + (EPM_BLK_CNT-1)];

				if (epm_debug_log)
					ncl_cmd_trace(LOG_ALW, 0x1e7e, "get pda0[0x%x],pda1[0x%x]", epm_io_cmd.pda_list[send_pg_cnt], epm_io_cmd.pda_list[send_pg_cnt + 1]);

				for (i = 0; i < total_mp_du_cnt; i++)
				{
					epm_io_cmd.bm_pl_list[(send_pg_cnt * DU_CNT_PER_PAGE) + i].all = 0;
					epm_io_cmd.bm_pl_list[(send_pg_cnt * DU_CNT_PER_PAGE) + i].pl.dtag = (DTAG_IN_DDR_MASK | (epm_pos->ddtag + (mp_du * total_mp_du_cnt) + (i)));
					epm_io_cmd.bm_pl_list[(send_pg_cnt * DU_CNT_PER_PAGE) + i].pl.du_ofst = i;
					epm_io_cmd.bm_pl_list[(send_pg_cnt * DU_CNT_PER_PAGE) + i].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
					epm_io_cmd.bm_pl_list[(send_pg_cnt * DU_CNT_PER_PAGE) + i].pl.nvm_cmd_id = EpmMetaIdx;
				}

				send_pg_cnt += EPM_BLK_CNT;
				if (send_pg_cnt == PG_CNT_PER_EPM_FLUSH)
				{
#if epm_remap_enable //remap
					chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], PG_CNT_PER_EPM_FLUSH);
#endif
					if (epm_debug_log)
					{
						ncl_cmd_trace(LOG_ALW, 0x88a7, "ncl_submit");
						for (k = 0; k < (PG_CNT_PER_EPM_FLUSH); k++)
						{
							ncl_cmd_trace(LOG_ALW, 0xabd9, "pda_list : ");
							chk_pda(epm_io_cmd.pda_list[k]);
#if epm_remap_enable
							ncl_cmd_trace(LOG_ALW, 0xe746, "pda_no_remap_list : ");
							chk_pda(epm_io_cmd.noremap_pda_list[k]);
#endif
						}
						//for(k=0;k<DU_CNT_PER_EPM_FLUSH;k++)
						//{
						//	ncl_cmd_trace(LOG_ALW, 0, "pl_dtag[%d]=0x%x",k,epm_io_cmd.bm_pl_list[k].pl.dtag);
						//}
					}
					//chk_pda(epm_io_cmd.pda_list[0]);
					epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, send_pg_cnt, DU_4K_DEFAULT_MODE, 0);

					if (flush_mode == ALL_FLUSH)
					{
						//write mirror
						for (i = 0; i < PG_CNT_PER_EPM_FLUSH; i++)
						{
#if epm_remap_enable
							epm_io_cmd.pda_list[i] = epm_io_cmd.noremap_pda_list[i] | epm_header_data->epm_mirror_mask;
							//chk_pda(epm_io_cmd.pda_list[0]);
#else
							epm_io_cmd.pda_list[i] |= epm_header_data->epm_mirror_mask;
#endif
						}

#if epm_remap_enable
						chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], PG_CNT_PER_EPM_FLUSH);
						//chk_pda(epm_io_cmd.pda_list[0]);

#endif
						if (epm_debug_log)
							ncl_cmd_trace(LOG_ALW, 0x241f, "get mirror pda0=0x%x", epm_io_cmd.pda_list[0]);

						epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, send_pg_cnt, DU_4K_DEFAULT_MODE, 0);
					}
					send_pg_cnt = 0;
				}
			}

			if (flush_mode != ONLY_MIRROR)
			{
				if (epm_debug_log)
					ncl_cmd_trace(LOG_ALW, 0xa56d, "set_epm_header pda_header[0x%x] pda_tail[0x%x]", pda_header, pda_tail);
				set_epm_header(sign, pda_header, pda_tail);
			}
		}
		epm_sign_bit >>= 1;
	}

/*	if (send_pg_cnt != 0)
	{
#if epm_remap_enable //remap
		chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], send_pg_cnt);
#endif
		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0, "remain send_pg_cnt=%d", send_pg_cnt);
			ncl_cmd_trace(LOG_ALW, 0, "ncl_submit");
			for (k = 0; k < (send_pg_cnt); k++)
			{
				ncl_cmd_trace(LOG_ALW, 0, "pda_list : ");
				chk_pda(epm_io_cmd.pda_list[k]);
#if epm_remap_enable
				ncl_cmd_trace(LOG_ALW, 0, "pda_no_remap_list : ");
				chk_pda(epm_io_cmd.noremap_pda_list[k]);
#endif
			}
		}
		epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, send_pg_cnt, DU_4K_DEFAULT_MODE, 0);

		if (flush_mode == ALL_FLUSH)
		{
			//write mirror
			for (i = 0; i < send_pg_cnt; i++)
			{
#if epm_remap_enable
				epm_io_cmd.pda_list[i] = epm_io_cmd.noremap_pda_list[i] | epm_header_data->epm_mirror_mask;
#else
				epm_io_cmd.pda_list[i] |= epm_header_data->epm_mirror_mask;
#endif
			}

#if epm_remap_enable
			chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], PG_CNT_PER_EPM_FLUSH);
#endif
			if (epm_debug_log)
				ncl_cmd_trace(LOG_ALW, 0, "get mirror pda0=0x%x", epm_io_cmd.pda_list[0]);

			epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, send_pg_cnt, DU_4K_DEFAULT_MODE, 0);
		}
		send_pg_cnt = 0;
	}*/
}
void epm_powercycle_update(u32 epm_sign)
{
	if (!plp_trigger)
		ncl_cmd_trace(LOG_ALW, 0x68b6, "epm_powercycle_update sign : %d", epm_sign);
	u8  sign = 1, epm_sign_bit = 0, chk_epm_sign_bit = 0;
	srb_t *srb = (srb_t *)SRAM_BASE;
	u32 need_pg_num = 0;
	epm_pos_t *epm_pos;
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	pda_t last_pda = epm_header_data->epm_last_pda;
	u32 remain_pg = 1 * 1 * (nand_page_num_slc() - pda2page(last_pda));
	switch (epm_sign)
	{
	case EPM_PLP1:
	{
		epm_sign_bit = 0x25;
		chk_epm_sign_bit = epm_sign_bit;
		for (sign = 1; sign < EPM_sign_end; sign++)
		{
			if (chk_epm_sign_bit & 0x1)
			{
				epm_pos = get_epm_pos_ptr(sign);
				need_pg_num += epm_pos->ddtag_cnt / (EPM_BLK_CNT * DU_CNT_PER_PAGE);
			}
			chk_epm_sign_bit >>= 1;
		}
		ncl_cmd_trace(LOG_ALW, 0x9cf5, "plp need_pg_num=%d remain_pg=%d", need_pg_num, remain_pg);
		if (remain_pg < need_pg_num)
		{
			if(plp_epm_update_done)//update done , don't call epm plp update again
				return;
			else
			{
				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				epm_erase(EPM_DATA_TAG);
				epm_flush_all(ONLY_MASTER, NULL);
			}
			//panic(" no this case for epm plp");
		}
		else
		{
			epm_flush_fast(epm_sign_bit, ALL_FLUSH, NULL);
		}
	}
	break;
	case EPM_PLP2:
	case EPM_POR:	
	case EPM_NON_PLP:
	{
		if (!plp_trigger)
			ncl_cmd_trace(LOG_ALW, 0x9d74, "EPM_Powercycle");
		//epm_set_bit(epm_sign_bit,SMART_sign);
		//epm_set_bit(epm_sign_bit,TRIM_sign);
		if (epm_sign == EPM_NON_PLP)
			epm_sign_bit = 0x5;
		else
			epm_sign_bit = 0x25; //0xFF
				
		chk_epm_sign_bit = epm_sign_bit;
		for (sign = 1; sign < EPM_sign_end; sign++)
		{
			if (chk_epm_sign_bit & 0x1)
			{
				if (epm_debug_log)
					ncl_cmd_trace(LOG_ALW, 0x86de, "epm_sign[%d] EPM_PLP", sign);
				epm_pos = get_epm_pos_ptr(sign);
				need_pg_num += epm_pos->ddtag_cnt / (EPM_BLK_CNT * DU_CNT_PER_PAGE);
			}
			chk_epm_sign_bit >>= 1;
		}
		if (!plp_trigger)
			ncl_cmd_trace(LOG_ALW, 0x259a, "need_pg_num=%d remain_pg=%d, plp_pg_num:%d", need_pg_num, remain_pg, plp_page_num);
		if (remain_pg < (need_pg_num + plp_page_num))
		{
			ncl_cmd_trace(LOG_ALW, 0xd0ee, "epm_data space not enough erase and reflush sign:[%d]", epm_sign);
			if((epm_sign == EPM_POR) || (epm_sign == EPM_NON_PLP))
			{
				if (!plp_trigger)
					ncl_cmd_trace(LOG_ALW, 0xa88a, "EPM_POR");
				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				pda_t only_mirror_pda = (nal_rda_to_pda(&srb->epm_pos[0])) | epm_header_data->epm_mirror_mask;
				epm_erase(EPM_DATA_TAG);
				epm_flush_all(ONLY_MASTER, NULL);
				epm_erase(EPM_DATA_MIRROR_TAG);
				epm_flush_all(ONLY_MIRROR, &only_mirror_pda);
			}
			else
			{
				ncl_cmd_trace(LOG_ALW, 0xca3b, "EPM_PLP2");
				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				epm_erase(EPM_DATA_TAG);
				epm_flush_all(ONLY_MASTER, NULL);
			}
		}
		else
		{
			epm_flush_fast(epm_sign_bit, ALL_FLUSH, NULL);
		}
	}
	break;
	case EPM_PLP_TEST1:
	{
		ncl_cmd_trace(LOG_ALW, 0x9bde, "EPM_PLP_TEST1");
		epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
		pda_t only_mirror_pda = (nal_rda_to_pda(&srb->epm_pos[0])) | epm_header_data->epm_mirror_mask;
		epm_erase(EPM_DATA_TAG);
		epm_flush_all(ONLY_MASTER, NULL);
		//if flush success
		epm_erase(EPM_DATA_MIRROR_TAG);
		epm_flush_all(ONLY_MIRROR, &only_mirror_pda);
	}
	break;
	case EPM_PLP_TEST2:
	{
		ncl_cmd_trace(LOG_ALW, 0xbe45, "EPM_PLP_TEST2");
		epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
		epm_erase(EPM_DATA_TAG);
		epm_flush_all(ONLY_MASTER, NULL);
	}
	break;
	default:
		ncl_cmd_trace(LOG_ALW, 0x26ec, "error plp num");
		break;
	}
	if (!plp_trigger)
		ncl_cmd_trace(LOG_ALW, 0x707b, "epm_powercycle_update done wb_fg : %x",_fg_warm_boot);
	if(epm_sign == EPM_PLP1)
		epm_plp_flag = true;
	epm_header_update();
#if(WARMBOOT_FTL_HANDLE == mENABLE)	
	if (_fg_warm_boot == true)
		wb_epm_lock_done = 1;	//20210317-Eddie-unlock cpu1 when epm update done
#endif
}
void epm_update(u32 epm_sign, u32 cpu_id)
{
	if ((epm_sign > EPM_sign_end) && (epm_sign < EPM_PLP_end))
	{
		if (OTF_WARM_BOOT_FLAG == true){	//20210520-Eddie CA3 ==> OTF Updates ==> delete refesh read cpu4
			extern struct timer_list refresh_read_timer;
			del_timer(&refresh_read_timer);
			OTF_WARM_BOOT_FLAG = false;
		}
		if (!plp_trigger)
			ncl_cmd_trace(LOG_ALW, 0xa9f9, "Powercycle epm_update epm_sign[%d] cpu_id[%d]", epm_sign, cpu_id);
		epm_powercycle_update(epm_sign);
		return;
	}
	ncl_cmd_trace(LOG_ALW, 0x90a0, "epm_update epm_sign[%d] cpu_id[%d]", epm_sign, cpu_id);
	
	if(plp_trigger && epm_sign != ERROR_WARN_sign)
	{
		ncl_cmd_trace(LOG_WARNING, 0xf9b7, "[warn]plp case, cancel epm update request, epm_done:%d",plp_epm_update_done);
		return;//force return
	}
		
	//if block full case
	//u8 start_ch;
	pda_t last_pda;
	srb_t *srb = (srb_t *)SRAM_BASE;
	u8 epm_sign_bit = 0;

	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);
	u32 need_pg_num = epm_pos->ddtag_cnt / (EPM_BLK_CNT * DU_CNT_PER_PAGE);
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
#if epm_spin_lock_enable
	//double check
	if (epm_sign > EPM_sign_end)
	{
		ncl_cmd_trace(LOG_ALW, 0x9aea, "get_epm_access_key epm_sign[%d]>EPM_sign_end", epm_sign);
	}
	else
	{
		sys_assert(cpu_id != (CPU_BE_LITE - 1)); // cpu4 can't update : avoid dead lock
		if (epm_pos->busy_lock == 0)
		{
			ncl_cmd_trace(LOG_ALW, 0xda0b, "panic please set spin lock before epm_flush");
			sys_assert(0);
		}
	}
#endif
	last_pda = epm_header_data->epm_last_pda;
	//start_ch = 6;

	u32 remain_pg = 1 * 1 * (nand_page_num_slc() - pda2page(last_pda));

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0xd5dc, "need_pg_num[%d],remain_pg[%d]", need_pg_num, remain_pg);

	if (remain_pg < (need_pg_num + plp_page_num))  // reserve page for plp
	{
		ncl_cmd_trace(LOG_ALW, 0x361a, "epm_data space not enough erase and reflush");
		epm_erase(EPM_DATA_TAG);
		epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
		pda_t only_mirror_pda = (nal_rda_to_pda(&srb->epm_pos[0])) | epm_header_data->epm_mirror_mask;
#if epm_spin_lock_enable
		u8 record_lock[EPM_sign_end];
		wait_ddr_set_done_and_force_all_lock(&record_lock[0]);
#endif
		epm_flush_all(ONLY_MASTER, NULL);
		//if flush success
		if (epm_debug_log)
		{
			ncl_cmd_trace(LOG_ALW, 0x749e, "only_mirror_pda=0x%x", only_mirror_pda);
			chk_pda(only_mirror_pda);
		}
		epm_erase(EPM_DATA_MIRROR_TAG);
		epm_flush_all(ONLY_MIRROR, &only_mirror_pda);
#if epm_spin_lock_enable
		recovery_all_lock(&record_lock[0]);
#endif
	}
	else
	{
		epm_sign_bit = 0;
		epm_set_bit(epm_sign_bit, epm_sign);
		epm_flush_fast(epm_sign_bit, ALL_FLUSH, NULL);
		//epm_flush(epm_pos,epm_sign,ALL_FLUSH,NULL);
	}
#if epm_spin_lock_enable
	unlock_epm_ddr(epm_sign, cpu_id);
#endif
	ncl_cmd_trace(LOG_ALW, 0xf1d2, "epm_data_update done");

    if(epm_sign == GLIST_sign)
    {
        GLIST_sign_flag = 1;  //When this flag == 1, assign the callback function to epm_header_ncl_cmd.
    }
	epm_header_update();
}

void journal_update(u16 evt_reason_id, u32 use_0)
{
	epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
	struct journal_tag *ptr = (struct journal_tag*)(((u32*)(void*)epm_journal_data)+4);
	u32 log_poh = (poh + (jiffies / 36000));

	(ptr+(epm_journal_data->journal_offset))->event_num = (epm_journal_data->journal_offset);
	(ptr+(epm_journal_data->journal_offset))->system_time_hi = (u32)(sys_time>>32);
	(ptr+(epm_journal_data->journal_offset))->system_time_lo = (u32)(sys_time);
	(ptr+(epm_journal_data->journal_offset))->event_id = evt_reason_id;
	(ptr+(epm_journal_data->journal_offset))->log_poh = log_poh;
	(ptr+(epm_journal_data->journal_offset))->pc_cnt = pc_cnt;
	(ptr+(epm_journal_data->journal_offset))->use_0 = use_0;

	if(epm_journal_data->journal_offset < 127)
	{
		epm_journal_data->journal_offset += 1;
	}
	else
	{
		epm_journal_data->journal_offset = 0;
	}

	epm_update(JOURNAL_sign,(CPU_ID-1));
}

void epm_erase(u32 area_tag)
{
	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0x051e, "epm_erase");
#if epm_remap_enable
	u8 remap_idx = 0, i = 0;
#endif
	srb_t *srb = (srb_t *)SRAM_BASE;
	u8 count = 0;
	u32 status = 0;
	switch (area_tag)
	{
	case EPM_HEADER_TAG:
		for (count = 0; count < 1; count++)
			epm_io_cmd.pda_list[count] = nal_rda_to_pda(&srb->epm_header_pos[count]);


		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x3bec, "erase fail area[%d] status=0x%x", area_tag, status);
		break;
	/*
	case EPM_HEADER_MIRROR_TAG:
		for (count = 0; count < 2; count++)
			epm_io_cmd.pda_list[count] = nal_rda_to_pda(&srb->epm_header_mir_pos[count]);
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, 2, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0, "erase fail area[%d] status=0x%x", area_tag, status);
		break;
	*/
	case EPM_DATA_TAG:
		for (count = 0; count < EPM_BLK_CNT; count++)
			epm_io_cmd.pda_list[count] = nal_rda_to_pda(&srb->epm_pos[count]);
#if epm_remap_enable
		//remap
		chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], EPM_BLK_CNT);
		//
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, EPM_BLK_CNT, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
		{
			ncl_cmd_trace(LOG_ALW, 0xf3a2, "erase fail area[%d] status=0x%x", area_tag, status);
			for (i = 0; i < EPM_BLK_CNT; i++)
			{
				//ncl_cmd_trace(LOG_ERR, 0, "pda 0x%x, err %d\n", epm_io_cmd.pda_list[i], epm_io_cmd.info_list[i].status);
				if (epm_io_cmd.info_list[i].status != 0)
				{
					//remap_idx = pda2plane(epm_io_cmd.noremap_pda_list[i]) + (pda2ch(epm_io_cmd.noremap_pda_list[i]) - 6) * 2 + pda2ce(epm_io_cmd.noremap_pda_list[i]) * 4;
					remap_idx = pda2plane(epm_io_cmd.noremap_pda_list[i]);
					//ncl_cmd_trace(LOG_ERR, 0, "remap_idx=%d\n",remap_idx);
					srch_remap(&epm_remap_tbl->epm_remap[remap_idx], epm_remap_tbl->epm_remap_source);
				}
			}
		}
#else
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, EPM_BLK_CNT, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x1ac5, "erase fail area[%d] status=0x%x", area_tag, status);
#endif
		break;
	case EPM_DATA_MIRROR_TAG:
		for (count = 0; count < EPM_BLK_CNT; count++)
			epm_io_cmd.pda_list[count] = nal_rda_to_pda(&srb->epm_pos_mir[count]);
#if epm_remap_enable
		//remap
		chk_epm_pda_remap(&epm_io_cmd.pda_list[0], &epm_io_cmd.noremap_pda_list[0], EPM_BLK_CNT);
		//
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, EPM_BLK_CNT, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
		{
			ncl_cmd_trace(LOG_ALW, 0x2b3e, "mirror erase fail area[%d] status=0x%x", area_tag, status);
			for (i = 0; i < EPM_BLK_CNT; i++)
			{
				if (epm_io_cmd.info_list[i].status != 0)
				{
					//remap_idx = pda2plane(epm_io_cmd.noremap_pda_list[i]) + (pda2ch(epm_io_cmd.noremap_pda_list[i]) - 6) * 2 + pda2ce(epm_io_cmd.noremap_pda_list[i]) * 4;
					remap_idx = pda2plane(epm_io_cmd.noremap_pda_list[i]);
					//printk("remap_idx=%d\n",remap_idx);
					srch_remap(&epm_remap_tbl->epm_mirror_remap[remap_idx], epm_remap_tbl->epm_mirror_remap_source);
				}
			}
		}
#else
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, EPM_BLK_CNT, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x7928, "erase fail area[%d] status=0x%x", area_tag, status);
#endif
		break;
	default:
		ncl_cmd_trace(LOG_ALW, 0x54a9, "no this case");
		panic("no this case\n");
	}
}

void get_next_epm_pda(pda_t *pda_base)
{
	*pda_base += (nal_get_interleave() << DU_CNT_SHIFT); //add page
}

void get_wr_epm_pda(pda_t *pda_base, pda_t *pda_list)
{
	for(int i=0; i<EPM_BLK_CNT;i++)
	{
		pda_list[i] = ((*pda_base) | (i << nand_info.pda_plane_shift));
		//chk_pda(pda_list[i]);
	}
	if (epm_debug_log)
	{
		ncl_cmd_trace(LOG_ALW, 0x6ef4, "get_wr_epm_pda pl0 pl1 :");
		chk_pda(pda_list[0]);
		chk_pda(pda_list[1]);
	}
	get_next_epm_pda(pda_base);
}

void get_rd_epm_pda(pda_t *pda_base, pda_t *pda_list, u32 cnt)
{
	u32 i;
	for (i = 0; i < cnt; i++)
	{
		pda_list[i] = (*pda_base) + i;
	}
	get_next_epm_pda(pda_base);
}

void get_next_epm_header_pda(pda_t *pda_base)
{
	/*
	if (pda2plane((*pda_base)) == 0)
	{
		*pda_base = ((*pda_base) | (1 << nand_info.pda_plane_shift));
	}
	else if (pda2plane((*pda_base)) == 1)
	{
		*pda_base = (*pda_base) & (~(1 << nand_info.pda_plane_shift));
		*pda_base += (nal_get_interleave() << DU_CNT_SHIFT); //add page
	}
	*/
	*pda_base += (nal_get_interleave() << DU_CNT_SHIFT); //add page
}

void get_wr_epm_header_pda(pda_t *pda_base, pda_t *pda_list)
{
	pda_list[0] = *pda_base;

	if (epm_debug_log)
	{
		ncl_cmd_trace(LOG_ALW, 0xdea3, "get_wr_epm_header_pda : ");
		chk_pda(pda_list[0]);
	}
	get_next_epm_header_pda(pda_base);
}

void get_rd_epm_header_pda(pda_t *pda_base, pda_t *pda_list, u32 cnt)
{
	u32 i;
	for (i = 0; i < cnt; i++)
	{
		pda_list[i] = (*pda_base) + i;
	}
	get_next_epm_header_pda(pda_base);
}
pda_t epm_gen_pda(u32 du, u8 pl, u8 ch, u8 ce, u8 lun, u32 pg, u32 blk)
{
	// ch, ce, lun, pl, blk, pg, du to PDA
	pda_t pda = 0;
	pda = blk << nand_info.pda_block_shift;
	pda |= pg << nand_info.pda_page_shift;
	pda |= ch << nand_info.pda_ch_shift;
	pda |= ce << nand_info.pda_ce_shift;
	pda |= lun << nand_info.pda_lun_shift;
	pda |= pl << nand_info.pda_plane_shift;
	pda |= du << nand_info.pda_du_shift;
	return pda;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//test and debug////////////////////////////////////////////////////////////////////////////////////////////////////
void chk_pda(pda_t pda)
{
	ncl_cmd_trace(LOG_ALW, 0xe929, "pda[0x%x] pl[%d],ch[%d],ce[%d],lun[%d],pg[%d]",
				  pda, pda2plane(pda), pda2ch(pda), pda2ce(pda), pda2lun(pda), pda2page(pda));
}

fast_code int reset_epm_all(int argc, char *argv[])
{
	srb_t *srb = (srb_t *)SRAM_BASE;
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	
	u32 *epm_header_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	u32 *epm_ftl_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	u32 *epm_glist_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	u32 *epm_smart_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	u32 *epm_ns_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
	u32 *epm_aes_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	u32 *epm_trim_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
	u32 *epm_journal_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
	//u32 *epm_misc_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
	u32 *epm_nvme_data_ptr = (u32 *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);

	memset(epm_header_ptr, 0x00, (shr_epm_info->epm_header.ddtag_cnt * DTAG_SZE));
	memset(epm_ftl_ptr, 0, (shr_epm_info->epm_ftl.ddtag_cnt * DTAG_SZE));
	memset(epm_glist_ptr, 0, (shr_epm_info->epm_glist.ddtag_cnt * DTAG_SZE));
	memset(epm_smart_ptr, 0, (shr_epm_info->epm_smart.ddtag_cnt * DTAG_SZE));
	memset(epm_ns_ptr, 0, (shr_epm_info->epm_namespace.ddtag_cnt * DTAG_SZE));
	memset(epm_aes_ptr, 0, (shr_epm_info->epm_aes.ddtag_cnt * DTAG_SZE));
	memset(epm_trim_ptr, 0, (shr_epm_info->epm_trim.ddtag_cnt * DTAG_SZE));
	memset(epm_journal_ptr, 0, (shr_epm_info->epm_journal.ddtag_cnt * DTAG_SZE));
	//memset(epm_misc_ptr, 0, (shr_epm_info->epm_misc.ddtag_cnt * DTAG_SZE));
	memset(epm_nvme_data_ptr, 0, (shr_epm_info->epm_error_warn_data.ddtag_cnt * DTAG_SZE));

	//erase all
	for (u8 i = 0; i < EPM_TAG_end; i++)
	{
		epm_erase(i);
	}
        
	epm_header_data->EPM_Head_tag = epm_head_tag;
	epm_header_data->EPM_SN = 0;

	epm_header_data->epm_header_last_pda = nal_rda_to_pda(&srb->epm_header_pos[0]);
	epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
	epm_header_data->valid_tag = epm_valid_tag;
	epm_header_data->epm_header_mirror_mask = nal_rda_to_pda(&srb->epm_header_pos[0]) ^ nal_rda_to_pda(&srb->epm_header_mir_pos[0]);
	epm_header_data->epm_mirror_mask = nal_rda_to_pda(&srb->epm_pos[0]) ^ nal_rda_to_pda(&srb->epm_pos_mir[0]);

	epm_flush_all(ALL_FLUSH, NULL);
	epm_header_update();
	
	ncl_cmd_trace(LOG_ALW, 0x7abc, "EPM data reset done!!!");
	return 0;
}

static DEFINE_UART_CMD(reset_epm_ddr, "reset_epm_ddr", "reset_epm_ddr", "reset_epm_ddr", 0, 0, reset_epm_all);

#if epm_uart_enable
fast_code int set_epm_ddr_space(int argc, char *argv[])
{
	u32 epm_sign = strtol(argv[1], (void *)0, 0);
	u32 data = strtol(argv[2], (void *)0, 0);
	ncl_cmd_trace(LOG_ALW, 0x770b, "set_epm_ddr_space epm_sign=%d data=%d", epm_sign, data);
	u32 i = 0;
	switch (epm_sign)
	{
	case FTL_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xb15c, "case FTL_sign");
		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xd470, "epm_ftl_data addr=0x%x", epm_ftl_data);
		for (i = 0; i < (262144 / 4); i++)
		{
			epm_ftl_data->data[i] = (data * 0x10000000) + (FTL_sign * 0x100000) + i;
		}
	}
	break;
	case GLIST_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x576f, "case GLIST_sign");
		epm_glist_t *epm_glist_data = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xde4a, "epm_glist_data addr=0x%x", epm_glist_data);
		for (i = 0; i < (622592 / 4); i++)
		{
			epm_glist_data->data[i] = (data * 0x10000000) + (GLIST_sign * 0x100000) + i;
		}
	}
	break;
	case SMART_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x090d, "case SMART_sign");
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x7e65, "epm_smart_data addr=0x%x", epm_smart_data);
		for (i = 0; i < 64; i++)
		{
			epm_smart_data->smart_save[i] = (data * 0x10000000) + (SMART_sign * 0x100000) + i;
		}
		for (i = 0; i < ((16384 - 4 - 4 - 256 - 156 - 4) / 4); i++)
		{
			epm_smart_data->data[i] = (data * 0x10000000) + (SMART_sign * 0x100000) + i + 63;
		}
		for (i = 0; i < 40; i++)
		{
			epm_smart_data->feature_save[i] = (data * 0x10000000) + (SMART_sign * 0x100000) + i + 64 + 3989;
		}
	}
	break;
	case NAMESPACE_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xb8ab, "case NAMESPACE_sign");
		epm_namespace_t *epm_namespace_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x3c71, "epm_namespace_data addr=0x%x", epm_namespace_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_namespace_data->data[i] = (data * 0x10000000) + (NAMESPACE_sign * 0x100000) + i;
		}
	}
	break;
	case AES_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xc7b0, "case AES_sign");
		epm_aes_t *epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x0698, "epm_aes_data addr=0x%x", epm_aes_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_aes_data->data[i] = (data * 0x10000000) + (AES_sign * 0x100000) + i;
		}
	}
	break;
	case TRIM_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xee51, "case Trim_sign");
		epm_trim_t *epm_trim_data = (epm_trim_t *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x07a7, "epm_trim_data addr=0x%x", epm_trim_data);
		for (i = 0; i < 4097; i++)
		{
			epm_trim_data->last_page[i] = (data * 0x10000000) + (TRIM_sign * 0x100000) + i;
		}
		for (i = 0; i < 15263; i++)
		{
			epm_trim_data->R2P[i] = (data * 0x10000000) + (TRIM_sign * 0x100000) + i + 4096;
		}
		for (i = 0; i < 256; i++)
		{
			epm_trim_data->info[i] = (data * 0x10000000) + (TRIM_sign * 0x100000) + i + 4096 + 15262;
		}
		for (i = 0; i < 515; i++)
		{
			epm_trim_data->host_data[i] = (data * 0x10000000) + (TRIM_sign * 0x100000) + i + 4096 + 15262 + 256;
		}
		for (i = 0; i < 347; i++)
		{
			epm_trim_data->RESERVED[i] = (data * 0x10000000) + (TRIM_sign * 0x100000) + i + 4096 + 15262 + 256 + 514;
		}
	}
	break;
	case JOURNAL_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x0d7c, "case JOURNAL_sign");
		epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x1131, "epm_journal_data addr=0x%x", epm_journal_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_journal_data->data[i] = (data * 0x10000000) + (JOURNAL_sign * 0x100000) + i;
		}
	}
	break;
	/*case MISC_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xe63c, "case MISC_sign");
		epm_misc_t *epm_misc_data = (epm_misc_t *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xb16e, "epm_misc_data addr=0x%x", epm_misc_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_misc_data->data[i] = (data * 0x10000000) + (MISC_sign * 0x100000) + i;
		}
	}
	break;*/
	case ERROR_WARN_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x7503, "case ERROR_WARN_sign");
		epm_error_warn_t *_epm_nvme_data = (epm_error_warn_t *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x8308, "_epm_nvme_data addr=0x%x", _epm_nvme_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			_epm_nvme_data->data[i] = (data * 0x10000000) + (ERROR_WARN_sign * 0x100000) + i;
		}
	}
	break;
	/*
	case TCG_INFO_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0, "case TCG_INFO_sign");
		epm_tcg_info_t *epm_tcg_info = (epm_tcg_info_t *)ddtag2mem(shr_epm_info->epm_tcg_info.ddtag);
		ncl_cmd_trace(LOG_ALW, 0, "epm_tcg_info addr=0x%x", epm_tcg_info);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_tcg_info->data[i] = (data * 0x10000000) + (TCG_INFO_sign * 0x100000) + i;
		}
	}
	break;
	*/
	
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:		
		ncl_cmd_trace(LOG_ALW, 0x13eb, "set_epm_ddr_space powercycle case");
		break;
	default:
		panic("no this case\n");
	}
	ncl_cmd_trace(LOG_ALW, 0x7fa1, "mem set end");
	return 0;
}

static DEFINE_UART_CMD(set_epm_mem, "set_epm_mem", "set_epm_mem", "set_epm_mem", 2, 2, set_epm_ddr_space);

fast_code int clear_epm_ddr_space(int argc, char *argv[])
{
	u32 epm_sign = strtol(argv[1], (void *)0, 0);
	ncl_cmd_trace(LOG_ALW, 0xab13, "clear_epm_ddr_space epm_sign = %d", epm_sign);
	u32 i = 0;
	switch (epm_sign)
	{
	case FTL_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x015f, "case FTL_sign");
		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xd582, "epm_ftl_data addr=0x%x", epm_ftl_data);
		for (i = 0; i < (262144 / 4); i++)
		{
			epm_ftl_data->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	case GLIST_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x7122, "case GLIST_sign");
		epm_glist_t *epm_glist_data = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xb554, "epm_glist_data addr=0x%x", epm_glist_data);
		for (i = 0; i < (622592 / 4); i++)
		{
			epm_glist_data->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	case SMART_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x8cf7, "case SMART_sign");
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x5383, "epm_smart_data addr=0x%x", epm_smart_data);
		for (i = 0; i < 64; i++)
		{
			epm_smart_data->smart_save[i] = 0xFFFFFFFF;
		}
		for (i = 0; i < ((16384 - 4 - 4 - 256 - 156 - 4) / 4); i++)
		{
			epm_smart_data->data[i] = 0xFFFFFFFF;
		}
		for (i = 0; i < 40; i++)
		{
			epm_smart_data->feature_save[i] = 0xFFFFFFFF;
		}
	}
	break;
	case NAMESPACE_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x3d9e, "case NAMESPACE_sign");
		epm_namespace_t *epm_namespace_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xaec9, "epm_namespace_data addr=0x%x", epm_namespace_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_namespace_data->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	case AES_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xa14e, "case AES_sign");
		epm_aes_t *epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xa9d5, "epm_aes_data addr=0x%x", epm_aes_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_aes_data->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	case TRIM_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xc582, "case TRIM_sign");
		epm_trim_t *epm_trim_data = (epm_trim_t *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xf917, "epm_trim_data addr=0x%x", epm_trim_data);
		for (i = 0; i < 4097; i++)
		{
			epm_trim_data->last_page[i] = 0xFFFFFFFF;
		}
		for (i = 0; i < 15263; i++)
		{
			epm_trim_data->R2P[i] = 0xFFFFFFFF;
		}
		for (i = 0; i < 256; i++)
		{
			epm_trim_data->info[i] = 0xFFFFFFFF;
		}
		for (i = 0; i < 515; i++)
		{
			epm_trim_data->host_data[i] = 0xFFFFFFFF;
		}
		for (i = 0; i < 347; i++)
		{
			epm_trim_data->RESERVED[i] = 0xFFFFFFFF;
		}
	}
	break;
	case MISC_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x1f94, "case MISC_sign");
		epm_misc_t *epm_misc_data = (epm_misc_t *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x7316, "epm_misc_data addr=0x%x", epm_misc_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_misc_data->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	case ERROR_WARN_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xbd27, "case ERROR_WARN_sign");
		epm_error_warn_t *_epm_nvme_data = (epm_error_warn_t *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xef1e, "_epm_nvme_data addr=0x%x", _epm_nvme_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			_epm_nvme_data->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	/*
	case TCG_INFO_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0, "case TCG_INFO_sign");
		epm_tcg_info_t *epm_tcg_info = (epm_tcg_info_t *)ddtag2mem(shr_epm_info->epm_tcg_info.ddtag);
		ncl_cmd_trace(LOG_ALW, 0, "epm_tcg_info addr=0x%x", epm_tcg_info);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			epm_tcg_info->data[i] = 0xFFFFFFFF;
		}
	}
	break;
	*/
	
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:		
		ncl_cmd_trace(LOG_ALW, 0x2feb, "clear_epm_ddr_space powercycle case");
		break;
	default:
		panic("no this case\n");
	}

	ncl_cmd_trace(LOG_ALW, 0x11d2, "mem clr end");
	return 0;
}

static DEFINE_UART_CMD(clr_epm_mem, "clr_epm_mem", "clr_epm_mem", "clr_epm_mem", 1, 1, clear_epm_ddr_space);

fast_code int flush_epm_head_func(int argc, char *argv[])
{
	epm_header_update();
	return 0;
}

static DEFINE_UART_CMD(flush_epm_head, "flush_epm_head", "flush_epm_head", "flush_epm_head", 0, 0, flush_epm_head_func);

fast_code int erase_area_epm_func(int argc, char *argv[])
{
	u32 erase_area = 0;
	erase_area = strtol(argv[1], (void *)0, 10);
	ncl_cmd_trace(LOG_ALW, 0x6d84, "erase_area=%d", erase_area);
	epm_erase(erase_area);
	return 0;
}

static DEFINE_UART_CMD(erase_area_epm, "erase_area_epm", "erase_area_epm", "erase_area_epm", 1, 1, erase_area_epm_func);

fast_code int read_epm_func(int argc, char *argv[])
{
	u32 sign = 0;
	sign = strtol(argv[1], (void *)0, 10);
	ncl_cmd_trace(LOG_ALW, 0xc2cf, "sign=%d", sign);

	bool master_data = epm_read(sign, ONLY_MASTER);
	//if read fail  read mirror
	if (master_data == false)
	{
		ncl_cmd_trace(LOG_ALW, 0x891d, "epm load master data fail sign : %d", sign);
		master_data = epm_read(sign, ONLY_MIRROR);

		if (master_data == false)
		{
			ncl_cmd_trace(LOG_ALW, 0xed9a, "epm load mirror data fail sign : %d", sign);
		}
	}
	return 0;
}

static DEFINE_UART_CMD(read_epm, "read_epm", "read_epm", "read_epm", 1, 1, read_epm_func);

fast_code int spor_flag_func(int argc, char *argv[])
{
	if (spor_flag)
	{
		spor_flag = 0;
		ncl_cmd_trace(LOG_ALW, 0x5ff7, "spor_flag disable");
	}
	else
	{
		spor_flag = 1;
		ncl_cmd_trace(LOG_ALW, 0xc31e, "spor_flag enable");
	}
	return 0;
}
static DEFINE_UART_CMD(spor_flag_enable, "spor_flag_enable", "spor_flag_enable", "spor_flag_enable", 0, 0, spor_flag_func);

#endif

ddr_code int dump_epm_ddr_space(int argc, char *argv[])
{
	u32 epm_sign = strtol(argv[1], (void *)0, 0);
	ncl_cmd_trace(LOG_ALW, 0xe062, "dump_epm_ddr_space epm_sign = %d", epm_sign);
	u32 i = 0;
	switch (epm_sign)
	{
	case FTL_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xcdae, "case FTL_sign");
		epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x2150, "epm_ftl_data addr=0x%x", epm_ftl_data);
		for (i = 0; i < (16); i++)
		{
			ncl_cmd_trace(LOG_ALW, 0xa38b, "epm_ftl_data->data[%d]=[0x%x]", i, epm_ftl_data->data[i]);
		}
		ncl_cmd_trace(LOG_ALW, 0x7cfe, "EPM_SN = %d ", epm_ftl_data->EPM_SN);

#ifdef Dynamic_OP_En
		ncl_cmd_trace(LOG_ALW, 0x4f1b, " OPValue %d, OPFlag %d, Capa_OP14 %d ", epm_ftl_data->OPValue, epm_ftl_data->OPFlag, epm_ftl_data->Capa_OP14);
		ncl_cmd_trace(LOG_ALW, 0xe6ed, " OP_LBA 0x%x 0x%x, OP_CAP 0x%x ", epm_ftl_data->OP_LBA_H, epm_ftl_data->OP_LBA_L, epm_ftl_data->OP_CAP);		
#endif
		ncl_cmd_trace(LOG_ALW, 0x425d, " BadSPBCnt %d, Phyblk_OP %d, PhyOP %d ", epm_ftl_data->BadSPBCnt, epm_ftl_data->Phyblk_OP, epm_ftl_data->PhyOP);
	}
	break;
	case GLIST_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xb811, "case GLIST_sign");
		epm_glist_t *epm_glist_data = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x43e5, "epm_glist_data addr=0x%x", epm_glist_data);
		for (i = 0; i < (622592 / 4); i++)
		{
			if ((i < 100) || (i > ((622592 / 4) - 100)))
				ncl_cmd_trace(LOG_ALW, 0xe603, "glist data[%d]=[0x%x]", i, epm_glist_data->data[i]);
			//ncl_cmd_trace(LOG_ERR, 0, "glist data[%d]=[0x%x][0x%x]\n",i,epm_glist_data->data[i],epm_glist_data->data[i+1]);
			//i++;
		}
		ncl_cmd_trace(LOG_ALW, 0x9fb2, "EPM_SN = %d ", epm_glist_data->EPM_SN);
	}
	break;
	case SMART_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x79b0, "case SMART_sign");
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xfd5d, "epm_smart_data addr=0x%x", epm_smart_data);
		ncl_cmd_trace(LOG_ALW, 0xb4ee, "EPM_SN = %d ", epm_smart_data->EPM_SN);
		for (i = 0; i < 64; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0x3f6b, "epm_smart_data->smart_save[%d]=[0x%x]", i, epm_smart_data->smart_save[i]);
		}
		ncl_cmd_trace(LOG_ALW, 0xa5be, "dcc_training_fail_cnt = %d ", epm_smart_data->dcc_training_fail_cnt);
		for (i = 0; i < 128; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0xab18, "epm_smart_data->ex_smart_save[%d]=[0x%x]", i, epm_smart_data->ex_smart_save[i]);
		}
		// for (i = 0; i < 3857; i++)
		// {
		// 	if ((i < 100) || (i > (3862 - 100)))
		// 		ncl_cmd_trace(LOG_ALW, 0, "epm_smart_data[%d]=[0x%x]", i, epm_smart_data->data[i]);
		// }
		for (i = 0; i < 5; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0x7bec, "epm_smart_data->error_log[%d]=[0x%x]", i, epm_smart_data->error_log[i]);
		}
		for (i = 0; i < 40; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0xc428, "epm_smart_data->feature_save[%d]=[0x%x]", i, epm_smart_data->feature_save[i]);
		}
	}
	break;
	case NAMESPACE_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x8199, "case NAMESPACE_sign");
		epm_namespace_t *epm_namespace_data = (epm_namespace_t *)ddtag2mem(shr_epm_info->epm_namespace.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x9418, "epm_namespace_data addr=0x%x", epm_namespace_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			if ((i < 3500) || (i > (((16384 - 4 - 4) / 4) - 100)))
				ncl_cmd_trace(LOG_ALW, 0xdebf, "epm_namespace_data[%d]=[0x%x]", i, epm_namespace_data->data[i]);
			//ncl_cmd_trace(LOG_ERR, 0, "epm_namespace_data[%d]=[0x%x][0x%x]\n",i,epm_namespace_data->data[i],epm_namespace_data->data[i+1]);
			//i++;
		}
		ncl_cmd_trace(LOG_ALW, 0x2866, "EPM_SN = %d ", epm_namespace_data->EPM_SN);
	}
	break;
	case AES_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x9319, "case AES_sign\n");
		epm_aes_t *epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x365e, "epm_aes_data addr=0x%x", epm_aes_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			if ((i < 100) || (i > (((16384 - 4 - 4) / 4) - 100)))
				ncl_cmd_trace(LOG_ALW, 0x6afa, "epm_aes_data[%d]=[0x%x]", i, epm_aes_data->data[i]);
			//ncl_cmd_trace(LOG_ERR, 0, "epm_aes_data[%d]=[0x%x][0x%x]\n",i,epm_aes_data->data[i],epm_aes_data->data[i+1]);
			//i++;
		}
		ncl_cmd_trace(LOG_ALW, 0x8c3b, "EPM_SN = %d ", epm_aes_data->EPM_SN);
	}
	break;
	case TRIM_sign:
	{
		ncl_cmd_trace(LOG_ERR, 0x6a0d, "case TRIM_sign\n");
		epm_trim_t *epm_trim_data = (epm_trim_t *)ddtag2mem(shr_epm_info->epm_trim.ddtag);
		ncl_cmd_trace(LOG_ERR, 0x4b87, "epm_trim_data addr=0x%x\n", epm_trim_data);
		for (i = 0; i < sizeof(epm_trim_data->info) / sizeof(epm_trim_data->info[0]); i++)
		{
			ncl_cmd_trace(LOG_ERR, 0x1b43, "epm_trim_data->info[%d]=[0x%x]\n", i, epm_trim_data->info[i]);
		}
		for (i = 0; i < sizeof(epm_trim_data->TrimTable) / sizeof(epm_trim_data->TrimTable[0]); i++)
		{
			ncl_cmd_trace(LOG_ERR, 0xefb0, "epm_trim_data->TrimTable[%d]=[0x%x]\n", i, epm_trim_data->TrimTable[i]);
		}
		for (i = 0; i < sizeof(epm_trim_data->RESERVED) / sizeof(epm_trim_data->RESERVED[0]); i++)
		{
			ncl_cmd_trace(LOG_ERR, 0xfd14, "epm_trim_data->RESERVED[%d]=[0x%x]\n", i, epm_trim_data->RESERVED[i]);
		}
		ncl_cmd_trace(LOG_ALW, 0x89de, "EPM_SN = %d ", epm_trim_data->EPM_SN);
	}
	break;
	case JOURNAL_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0x2286, "case JOURNAL_sign");
		epm_journal_t *epm_journal_data = (epm_journal_t *)ddtag2mem(shr_epm_info->epm_journal.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x7bc2, "epm_journal_data addr=0x%x", epm_journal_data);
		/*for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			if ((i < 100) || (i > (((16384 - 4 - 4) / 4) - 100)))
				ncl_cmd_trace(LOG_ALW, "epm_journal_data[%d]=[0x%x]", i, epm_journal_data->journal_offset[i]);
			//ncl_cmd_trace(LOG_ERR, "_epm_nvme_data[%d]=[0x%x][0x%x]\n",i,_epm_nvme_data->data[i],_epm_nvme_data->data[i+1]);
			//i++;
		}*/
		ncl_cmd_trace(LOG_ALW, 0x1f59, "EPM_SN = %d ", epm_journal_data->EPM_SN);
	}
	break;
	/*case MISC_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xb32a, "case MISC_sign");
		epm_misc_t *epm_misc_data = (epm_misc_t *)ddtag2mem(shr_epm_info->epm_misc.ddtag);
		ncl_cmd_trace(LOG_ALW, 0x3de7, "epm_misc_data addr=0x%x", epm_misc_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			if ((i < 100) || (i > (((16384 - 4 - 4) / 4) - 100)))
				ncl_cmd_trace(LOG_ALW, 0xd7bb, "epm_misc_data[%d]=[0x%x]", i, epm_misc_data->data[i]);
			//ncl_cmd_trace(LOG_ERR, 0, "epm_misc_data[%d]=[0x%x][0x%x]\n",i,epm_misc_data->data[i],epm_misc_data->data[i+1]);
			//i++;
		}
		ncl_cmd_trace(LOG_ALW, 0x1f59, "EPM_SN = %d ", epm_misc_data->EPM_SN);
	}
	break;*/
	case ERROR_WARN_sign:
	{
		ncl_cmd_trace(LOG_ALW, 0xeae3, "case ERROR_WARN_sign");
		epm_error_warn_t *_epm_nvme_data = (epm_error_warn_t *)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
		ncl_cmd_trace(LOG_ALW, 0xf370, "_epm_nvme_data addr=0x%x", _epm_nvme_data);
		for (i = 0; i < ((16384 - 4 - 4) / 4); i++)
		{
			if ((i < 100) || (i > (((16384 - 4 - 4) / 4) - 100)))
				ncl_cmd_trace(LOG_ALW, 0xf585, "_epm_nvme_data[%d]=[0x%x]", i, _epm_nvme_data->data[i]);
			//ncl_cmd_trace(LOG_ERR, 0, "_epm_nvme_data[%d]=[0x%x][0x%x]\n",i,_epm_nvme_data->data[i],_epm_nvme_data->data[i+1]);
			//i++;
		}
		ncl_cmd_trace(LOG_ALW, 0x6b63, "EPM_SN = %d ", _epm_nvme_data->EPM_SN);
	}
	break;
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:		
		ncl_cmd_trace(LOG_ALW, 0x5f0d, "dump_epm_ddr_space powercycle case");
		break;
	default:
		panic("no this case\n");
	}
	ncl_cmd_trace(LOG_ALW, 0x78a5, "dump end");
	return 0;
}

static DEFINE_UART_CMD(dump_epm_mem, "dump_epm_mem", "dump_epm_mem", "dump_epm_mem", 1, 1, dump_epm_ddr_space);

ddr_code void dump_epm_header_data()
{
	epm_header_t *epm_header_data = (epm_header_t *)ddtag2mem(shr_epm_info->epm_header.ddtag);
	ncl_cmd_trace(LOG_ALW, 0x95cf, "EPM_Head_tag=0x%x", epm_header_data->EPM_Head_tag);
	ncl_cmd_trace(LOG_ALW, 0x5304, "EPM_SN=0x%x", epm_header_data->EPM_SN);
	ncl_cmd_trace(LOG_ALW, 0xe548, "epm_ftl_header.epm_header_tag=0x%x", epm_header_data->epm_ftl_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0x33bb, "epm_ftl_header.epm_newest_head=0x%x", epm_header_data->epm_ftl_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x711f, "->epm_ftl_header.epm_newest_tail=0x%x", epm_header_data->epm_ftl_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0xa1af, "->epm_ftl_header.valid_tag=0x%x", epm_header_data->epm_ftl_header.valid_tag);
	ncl_cmd_trace(LOG_ALW, 0xeba4, "->epm_glist_header.epm_header_tag=0x%x", epm_header_data->epm_glist_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0xb69b, "->epm_glist_header.epm_newest_head=0x%x", epm_header_data->epm_glist_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x1acb, "->epm_glist_header.epm_newest_tail=0x%x", epm_header_data->epm_glist_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0x95c2, "->epm_glist_header.valid_tag=0x%x", epm_header_data->epm_glist_header.valid_tag);
	ncl_cmd_trace(LOG_ALW, 0x5cde, "->epm_smart_header.epm_header_tag=0x%x", epm_header_data->epm_smart_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0xcb28, "->epm_smart_header.epm_newest_head=0x%x", epm_header_data->epm_smart_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x50d8, "->epm_smart_header.epm_newest_tail=0x%x", epm_header_data->epm_smart_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0xb982, "->epm_smart_header.valid_tag=0x%x", epm_header_data->epm_smart_header.valid_tag);
	ncl_cmd_trace(LOG_ALW, 0x4c10, "->epm_namespace_header.epm_header_tag=0x%x", epm_header_data->epm_namespace_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0xabc5, "->epm_namespace_header.epm_newest_head=0x%x", epm_header_data->epm_namespace_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x830c, "->epm_namespace_header.epm_newest_tail=0x%x", epm_header_data->epm_namespace_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0x7cc1, "->epm_namespace_header.valid_tag=0x%x", epm_header_data->epm_namespace_header.valid_tag);
	ncl_cmd_trace(LOG_ALW, 0xb95b, "->epm_aes_header.epm_header_tag=0x%x", epm_header_data->epm_aes_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0x001a, "->epm_aes_header.epm_newest_head=0x%x", epm_header_data->epm_aes_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x1e28, "->epm_aes_header.epm_newest_tail=0x%x", epm_header_data->epm_aes_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0x87c2, "->epm_aes_header.valid_tag=0x%x", epm_header_data->epm_aes_header.valid_tag);
	ncl_cmd_trace(LOG_ALW, 0x12ff, "->epm_trim_header.epm_header_tag=0x%x", epm_header_data->epm_trim_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0x92b9, "->epm_trim_header.epm_newest_head=0x%x", epm_header_data->epm_trim_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x0e94, "->epm_trim_header.epm_newest_tail=0x%x", epm_header_data->epm_trim_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0xbd56, "->epm_trim_header.valid_tag=0x%x", epm_header_data->epm_trim_header.valid_tag);
	ncl_cmd_trace(LOG_ALW, 0xc930, "->epm_journal_header.epm_header_tag=0x%x", epm_header_data->epm_journal_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0xb429, "->epm_journal_header.epm_newest_head=0x%x", epm_header_data->epm_journal_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0xc06d, "->epm_journal_header.epm_newest_tail=0x%x", epm_header_data->epm_journal_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0xac65, "->epm_journal_header.valid_tag=0x%x", epm_header_data->epm_journal_header.valid_tag);
	/*ncl_cmd_trace(LOG_ALW, 0x108c, "->epm_misc_header.epm_header_tag=0x%x", epm_header_data->epm_misc_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0x30c3, "->epm_misc_header.epm_newest_head=0x%x", epm_header_data->epm_misc_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0x392a, "->epm_misc_header.epm_newest_tail=0x%x", epm_header_data->epm_misc_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0xc7c8, "->epm_misc_header.valid_tag=0x%x", epm_header_data->epm_misc_header.valid_tag);*/
	ncl_cmd_trace(LOG_ALW, 0xcfb1, "->epm_nvme_data_header.epm_header_tag=0x%x", epm_header_data->epm_nvme_data_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0xab6e, "->epm_nvme_data_header.epm_newest_head=0x%x", epm_header_data->epm_nvme_data_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0xb226, "->epm_nvme_data_header.epm_newest_tail=0x%x", epm_header_data->epm_nvme_data_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0x2bcb, "->epm_nvme_data_header.valid_tag=0x%x", epm_header_data->epm_nvme_data_header.valid_tag);
	/*ncl_cmd_trace(LOG_ALW, 0, "->epm_tcg_info_header.epm_header_tag=0x%x", epm_header_data->epm_tcg_info_header.epm_header_tag);
	ncl_cmd_trace(LOG_ALW, 0, "->epm_tcg_info_header.epm_newest_head=0x%x", epm_header_data->epm_tcg_info_header.epm_newest_head);
	ncl_cmd_trace(LOG_ALW, 0, "->epm_tcg_info_header.epm_newest_tail=0x%x", epm_header_data->epm_tcg_info_header.epm_newest_tail);
	ncl_cmd_trace(LOG_ALW, 0, "->epm_tcg_info_header.valid_tag=0x%x", epm_header_data->epm_tcg_info_header.valid_tag);*/
	ncl_cmd_trace(LOG_ALW, 0x5dbd, "->epm_header_last_pda=0x%x", epm_header_data->epm_header_last_pda);
	ncl_cmd_trace(LOG_ALW, 0xa4ff, "->epm_last_pda=0x%x", epm_header_data->epm_last_pda);
	ncl_cmd_trace(LOG_ALW, 0x30f3, "->epm_header_mirror_mask=0x%x", epm_header_data->epm_header_mirror_mask);
	ncl_cmd_trace(LOG_ALW, 0x39ab, "->epm_mirror_mask=0x%x", epm_header_data->epm_mirror_mask);
	ncl_cmd_trace(LOG_ALW, 0xb55a, "->valid_tag=0x%x", epm_header_data->valid_tag);
}

ddr_code int dump_epm_header_func(int argc, char *argv[])
{
	dump_epm_header_data();
	return 0;
}

static DEFINE_UART_CMD(dump_epm_head, "dump_epm_head", "dump_epm_head", "dump_epm_head", 0, 0, dump_epm_header_func);

#if epm_remap_enable
ddr_code int epm_remap_func(int argc, char *argv[])
{
	u8 i = 0;
	u32 test_mode = strtol(argv[1], (void *)0, 0);
	ncl_cmd_trace(LOG_ALW, 0xa7f6, "epm_remap_func");
	switch (test_mode)
	{
	case 1:
	{
		ncl_cmd_trace(LOG_ALW, 0xaf4f, "epm_remap_flag");
		if (epm_remap_flag)
		{
			epm_remap_flag = 0;
			ncl_cmd_trace(LOG_ALW, 0x8eb4, "epm_remap_flag disable");
		}
		else
		{
			epm_remap_flag = 1;
			ncl_cmd_trace(LOG_ALW, 0x1af9, "epm_remap_flag enable");
		}
	}
	break;
	case 2:
	{
		ncl_cmd_trace(LOG_ALW, 0x5cfe, "epm_init_remap");
		epm_init_remap();
	}
	break;
	case 3:
	{
		ncl_cmd_trace(LOG_ALW, 0x021f, "epm_remap_tbl_flush");
		epm_remap_tbl_flush(&epm_remap_tbl->remap_last_pda);
	}
	break;
	case 4:
	{
		ncl_cmd_trace(LOG_ALW, 0x3e94, "epm_remap_tbl_load");
		pda_t remap_last_pda = epm_remap_tbl->remap_tbl_blk[0];
		epm_remap_tbl_load(&remap_last_pda);
	}
	break;
	case 5:
	{
		ncl_cmd_trace(LOG_ALW, 0xba8f, "srch_remap");
		srch_remap(&epm_remap_tbl->epm_remap[0], epm_remap_tbl->epm_remap_source);
		srch_remap(&epm_remap_tbl->epm_mirror_remap[0], epm_remap_tbl->epm_mirror_remap_source);
	}
	break;
	case 6:
	{
		ncl_cmd_trace(LOG_ALW, 0xb679, "dump_remap");
		ncl_cmd_trace(LOG_ALW, 0xc593, "EPM_remap_tag=0x%x", epm_remap_tbl->EPM_remap_tag);
		ncl_cmd_trace(LOG_ALW, 0xe44a, "epm_remap_sn=0x%x", epm_remap_tbl->epm_remap_sn);
		ncl_cmd_trace(LOG_ALW, 0x5483, "remap_last_pda=0x%x", epm_remap_tbl->remap_last_pda);
		ncl_cmd_trace(LOG_ALW, 0x3258, "remap_tbl_blk[0]=0x%x", epm_remap_tbl->remap_tbl_blk[0]);
		ncl_cmd_trace(LOG_ALW, 0x3f88, "remap_tbl_blk[1]=0x%x", epm_remap_tbl->remap_tbl_blk[1]);
		ncl_cmd_trace(LOG_ALW, 0x01c6, "remap_tbl_mirror_mask=0x%x", epm_remap_tbl->remap_tbl_mirror_mask);
		for (i = 0; i < 8; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0xb038, "epm_remap[%d]=0x%x", i, epm_remap_tbl->epm_remap[i]);
			chk_pda(epm_remap_tbl->epm_remap[i]);
		}
		for (i = 0; i < 8; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0xc1e8, "epm_remap_source[%d]=0x%x", i, epm_remap_tbl->epm_remap_source[i]);
			chk_pda(epm_remap_tbl->epm_remap_source[i]);
		}
		for (i = 0; i < 8; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0x1047, "epm_mirror_remap[%d]=0x%x", i, epm_remap_tbl->epm_mirror_remap[i]);
			chk_pda(epm_remap_tbl->epm_mirror_remap[i]);
		}
		for (i = 0; i < 8; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0xd546, "epm_mirror_remap_source[%d]=0x%x", i, epm_remap_tbl->epm_mirror_remap_source[i]);
			chk_pda(epm_remap_tbl->epm_mirror_remap_source[i]);
		}
#if FRB_remap_enable		
		for (i = 0; i < 2; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0x8ef4, "FRB_remap[%d]=0x%x", i, epm_remap_tbl->frb_remap[i]);
			chk_pda(epm_remap_tbl->frb_remap[i]);
		}
		for (i = 0; i < 2; i++)
		{
			ncl_cmd_trace(LOG_ALW, 0x9177, "FRB_remap_source[%d]=0x%x", i, epm_remap_tbl->frb_remap_source[i]);
			chk_pda(epm_remap_tbl->frb_remap_source[i]);
		}
#endif		
		ncl_cmd_trace(LOG_ALW, 0xe103, "valid_tag=0x%x", epm_remap_tbl->valid_tag);
		ncl_cmd_trace(LOG_ALW, 0xa8ab, "epm_remap_tbl addr : 0x%x ",epm_remap_tbl);
	}
	break;
	default:
		ncl_cmd_trace(LOG_ALW, 0x54b9, "no this case test_mode[%d]", test_mode);
		ncl_cmd_trace(LOG_ALW, 0xec86, "1:epm_remap_flag");
		ncl_cmd_trace(LOG_ALW, 0x9dcf, "2:epm_init_remap");
		ncl_cmd_trace(LOG_ALW, 0x663a, "3:epm_remap_tbl_flush");
		ncl_cmd_trace(LOG_ALW, 0xebd5, "4:epm_remap_tbl_load");
		ncl_cmd_trace(LOG_ALW, 0x3cb3, "5:srch_remap");
		ncl_cmd_trace(LOG_ALW, 0x65ad, "6:dump_remap");
		break;
	}
	return 0;
}
static DEFINE_UART_CMD(epm_remap_uart, "epm_remap_uart", "epm_remap_uart", "epm_remap_uart", 1, 1, epm_remap_func);
#endif
/*fast_code int plp_test_func(int argc, char *argv[])
{
	ncl_cmd_trace(LOG_ALW, 0, "flush_all_test_func");
	u8 plp_num = strtol(argv[1], (void *)0, 10);
	srb_t *srb = (srb_t *) SRAM_BASE;
	u8 epm_sign_bit=0;
	epm_header_t* epm_header_data = (epm_header_t*)ddtag2mem(shr_epm_info->epm_header.ddtag);
	pda_t only_mirror_pda = 0;
	ncl_cmd_trace(LOG_ERR, 0, "plp_num=%d\n",plp_num);
	u32 plp_t1=0,plp_t2=0,time=0;
	extern int cur_cpu_clk_freq;
	if(plp_num>0)
	{
		switch(plp_num)
		{
			case 1:
				ncl_cmd_trace(LOG_ERR, 0, "plp case 1 : \n");
	
				plp_t1 = get_tsc_lo();

				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				only_mirror_pda = (nal_rda_to_pda(&srb->epm_pos[0])) | epm_header_data->epm_mirror_mask;
				epm_erase(EPM_DATA_TAG);
				epm_sign_bit = 0xFF;
				epm_flush_fast(epm_sign_bit,ONLY_MASTER,NULL);
				epm_erase(EPM_DATA_MIRROR_TAG);
				epm_sign_bit = 0xFF;
				epm_flush_fast(epm_sign_bit,ONLY_MIRROR,&only_mirror_pda);
				epm_header_update();
	
				plp_t2 = get_tsc_lo();
				time = (plp_t2 - plp_t1) * 10 / cur_cpu_clk_freq;
				ncl_cmd_trace(LOG_ERR, 0, "\nt1=0x%x t2=0x%x\n",plp_t1,plp_t2);
				ncl_cmd_trace(LOG_ERR, 0, "\ntime : %d.%dus | %d ms\n",time /10, time % 10, (time /10/1000) );
				break;
			case 2:
				ncl_cmd_trace(LOG_ERR, 0, "plp case 2 : \n");
				plp_t1 = get_tsc_lo();
				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				epm_erase(EPM_DATA_TAG);
				epm_sign_bit = 0xFF;
				epm_flush_fast(epm_sign_bit,ONLY_MASTER,NULL);	
				epm_header_update();
				plp_t2 = get_tsc_lo();
				time = (plp_t2 - plp_t1) * 10 / cur_cpu_clk_freq;
				ncl_cmd_trace(LOG_ERR, 0, "\nt1=0x%x t2=0x%x\n",plp_t1,plp_t2);
				ncl_cmd_trace(LOG_ERR, 0, "\ntime : %d.%dus | %d ms\n",time /10, time % 10, (time /10/1000) );
				break;
			case 3:
				ncl_cmd_trace(LOG_ERR, 0, "plp case 3 : \n");
				plp_t1 = get_tsc_lo();
				epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
				epm_erase(EPM_DATA_TAG);
				epm_sign_bit = 36;
				epm_flush_fast(epm_sign_bit,ONLY_MASTER,NULL);	
				epm_header_update();
				plp_t2 = get_tsc_lo();
				time = (plp_t2 - plp_t1) * 10 / cur_cpu_clk_freq;
				ncl_cmd_trace(LOG_ERR, 0, "\nt1=0x%x t2=0x%x\n",plp_t1,plp_t2);
				ncl_cmd_trace(LOG_ERR, 0, "\ntime : %d.%dus | %d ms\n",time /10, time % 10, (time /10/1000) );
				break;
			case 4:
				ncl_cmd_trace(LOG_ERR, 0, "plp case 4 : \n");
				plp_t1 = get_tsc_lo();
				epm_sign_bit = 0xFF;
				epm_flush_fast(epm_sign_bit,ONLY_MASTER,NULL);	
				epm_header_update();	
				plp_t2 = get_tsc_lo();
				time = (plp_t2 - plp_t1) * 10 / cur_cpu_clk_freq;
				ncl_cmd_trace(LOG_ERR, 0, "\nt1=0x%x t2=0x%x\n",plp_t1,plp_t2);
				ncl_cmd_trace(LOG_ERR, 0, "\ntime : %d.%dus | %d ms\n",time /10, time % 10, (time /10/1000) );
				break;
			case 5:
				ncl_cmd_trace(LOG_ERR, 0, "plp case 5 : \n");
				plp_t1 = get_tsc_lo();
				epm_sign_bit = 36;
				epm_flush_fast(epm_sign_bit,ONLY_MASTER,NULL);	
				epm_header_update();
				plp_t2 = get_tsc_lo();
				time = (plp_t2 - plp_t1) * 10 / cur_cpu_clk_freq;
				ncl_cmd_trace(LOG_ERR, 0, "\nt1=0x%x t2=0x%x\n",plp_t1,plp_t2);
				ncl_cmd_trace(LOG_ERR, 0, "\ntime : %d.%dus | %d ms\n",time /10, time % 10, (time /10/1000) );
				break;
			default:
				ncl_cmd_trace(LOG_ERR, 0, "error plp num\n");	
				break;
		}
	}
	return 0;
}
static DEFINE_UART_CMD(plp_test, "plp_test", "plp_test", "plp_test", 1, 1, plp_test_func);*/

ddr_code int epm_chkpda_func(int argc, char *argv[])
{
	pda_t pda = strtol(argv[1], (void *)0, 0);
	ncl_cmd_trace(LOG_ERR, 0x7e9e, "pda=0x%x\n", pda);
	chk_pda(pda);
	return 0;
}

static DEFINE_UART_CMD(epm_chkpda, "epm_chkpda", "epm_chkpda", "epm_chkpda", 1, 1, epm_chkpda_func);

ddr_code int epm_debug_func(int argc, char *argv[])
{
	if (epm_debug_log)
	{
		epm_debug_log = 0;
		ncl_cmd_trace(LOG_ALW, 0xb090, "epm debug disable");
	}
	else
	{
		epm_debug_log = 1;
		ncl_cmd_trace(LOG_ALW, 0x78f9, "epm debug enable");
	}
	return 0;
}

static DEFINE_UART_CMD(epm_debug_enable, "epm_debug_enable", "epm_debug_enable", "epm_debug_enable", 0, 0, epm_debug_func);

fast_code int erase_epm_func(int argc, char *argv[])
{
	u32 i = 0;
	for (i = 0; i < EPM_TAG_end; i++)
	{
		epm_erase(i);
	}
	ncl_cmd_trace(LOG_ALW, 0xb24a, "epm erase done");
	return 0;
}

static DEFINE_UART_CMD(erase_epm, "erase_epm", "erase_epm", "erase_epm", 0, 0, erase_epm_func);

ddr_code int epm_show_warn_info_main(int argc, char *argv[])
{
	dump_error_warn_info();
	extern volatile bool cpu_feedback[];
	ncl_cmd_trace(LOG_ALW, 0xdb43, "CPU4 timer trigger %d %d %d %d",cpu_feedback[0],cpu_feedback[1],cpu_feedback[2],cpu_feedback[3]);
	return 0;
}

static DEFINE_UART_CMD(epm_warn, "epm_warn", "epm_warn", "epm_warn", 0, 0, epm_show_warn_info_main);


#if 0//defined(USE_CRYPTO_HW)
static init_code int CPU4_crypt5(int argc, char *argv[])
{
	crypto_change_mode_range(2, 1, 1, 0);
	return 0;
}

static DEFINE_UART_CMD(CPU4_crypt5, "CPU4_crypt5", "CPU4_crypt5",
					   "CPU4_crypt5", 0, 0, CPU4_crypt5);
#endif
#if defined(USE_CRYPTO_HW)

// JackLi
ddr_code int epm_sntz_clear_main(int argc, char *argv[])
{
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	
	ncl_cmd_trace(LOG_ERR, 0x165f, "Ori EPM for sntz");
	ncl_cmd_trace(LOG_ERR, 0x177f, "Ori EPM for sntz: 0x%x, 0x%x_%x", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, (u32)(epm_smart_data->sanitizeInfo.sanitize_log_page>>32), (u32)epm_smart_data->sanitizeInfo.sanitize_log_page);

	epm_smart_data->sanitizeInfo.sanitize_Tag = 0;
	epm_smart_data->sanitizeInfo.fwSanitizeProcessStates = 0;
	epm_smart_data->sanitizeInfo.sanitize_log_page = 0;
	
	ncl_cmd_trace(LOG_ERR, 0xe5b5, "Clear EPM for sanitize in SMART!");
	ncl_cmd_trace(LOG_ERR, 0xd6e1, "Cur EPM for sntz: 0x%x, 0x%x %x", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, (u32)(epm_smart_data->sanitizeInfo.sanitize_log_page>>32), (u32)epm_smart_data->sanitizeInfo.sanitize_log_page);
	return 0;
}

static DEFINE_UART_CMD(epm_sntz_clear, "epm_sntz_clear", "epm_sntz_clear", "epm_sntz_clear", 0, 0, epm_sntz_clear_main);

ddr_code int epm_sntz_set_sts_main(int argc, char *argv[])
{
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	ncl_cmd_trace(LOG_ERR, 0xbba9, "Ori EPM for sntz");
	ncl_cmd_trace(LOG_ERR, 0xe893, "Ori EPM for sntz: 0x%x, 0x%x %x", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, (u32)(epm_smart_data->sanitizeInfo.sanitize_log_page>>32), (u32)epm_smart_data->sanitizeInfo.sanitize_log_page);

	u64 sts = strtoull(argv[1], (void *)0, 0);
	epm_smart_data->sanitizeInfo.sanitize_log_page &= (0xFFFFFFFFFFF8FFFF);
	epm_smart_data->sanitizeInfo.sanitize_log_page |= (sts<<16);
	
	ncl_cmd_trace(LOG_ERR, 0x0464, "Cur EPM for sntz: 0x%x, 0x%x %x", epm_smart_data->sanitizeInfo.fwSanitizeProcessStates, (u32)(epm_smart_data->sanitizeInfo.sanitize_log_page>>32), (u32)epm_smart_data->sanitizeInfo.sanitize_log_page);
	return 0;
}

static DEFINE_UART_CMD(sntz_set_sts, "sntz_set_sts", "sntz_set_sts [stsVal]", "set sts in log page for sntz", 1, 1, epm_sntz_set_sts_main);

#endif
#endif
#endif
