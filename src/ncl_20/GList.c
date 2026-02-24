#define INSTANTIATE_GLIST
#include "GList.h"
#undef  INSTANTIATE_GLIST
#include "fc_export.h"
#include "bf_mgr.h"
#include "ipc_api.h"
#include "cpu_msg.h"
#include "eccu.h"
#include "nand.h"
#include "die_que.h"
#include "ftl_core.h"
#include "ftl.h"
#include "ErrorHandle.h"

#include "types.h"
#include "dtag.h"
#include "l2p_mgr.h"
#include "addr_trans.h"
//#include "ncl.h"
#include "stdlib.h"
#include "srb.h"
#include "ddr_top_register.h"
#include "nvme_spec.h"
/*! \cond PRIVATE */
#define __FILEID__ glist
#include "trace.h"
/*! \endcond */


share_data epm_info_t*  shr_epm_info;
share_data volatile u8 shr_total_die;
share_data volatile u32 shr_program_fail_count;
share_data volatile u32 shr_erase_fail_count;
share_data volatile u32 shr_die_fail_count;

share_data volatile u64 shr_nand_bytes_written;
share_data volatile u32 shr_avg_erase;
#if 0//def ERRHANDLE_GLIST
share_data_zi bool  fgFail_Blk_Full;        // need define in sram
share_data_zi sGLTable *pGList;             // need define in Dram  // GL_mod, Paul_20201130
share_data_zi u16   wOpenBlk[MAX_OPEN_CNT]; // need define in Dram
share_data_zi u8    bErrOpCnt;
share_data_zi u8    bErrOpIdx;
share_data_zi bool  FTL_INIT_DONE;

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi sEH_Manage_Info sErrHandle_Info;
share_data_zi MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi u8 bFail_Blk_Cnt;             // need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];
//fast_data_zi u32 pg_cnt_in_xlc_spb;		///< pda number in XLC SPB
#endif

#if RAID_SUPPORT_UECC
share_data_zi volatile u32 nand_ecc_detection_cnt;
share_data_zi volatile u32 host_uecc_detection_cnt;
share_data_zi volatile u32 internal_uecc_detection_cnt;
share_data_zi volatile u32 uncorrectable_sector_count;
share_data_zi volatile u32 internal_rc_fail_cnt;
share_data_zi volatile u32 host_prog_fail_cnt;

share_data volatile SMART_uecc_info_t smart_uecc_info[smart_uecc_info_cnt];
share_data volatile u16 uecc_smart_ptr;

share_data_zi volatile bool  FTL_INIT_DONE;
#endif

extern u8 host_sec_bitz;
#ifdef ERRHANDLE_ECCT
share_data_zi volatile stECC_table *pECC_table;  //tony 20201030
share_data stECCT_ipc_t *vsc_ecct_data; //for vsc cmd use
share_data volatile u16 ecct_cnt;  //ecct cnt in sram  //tony 20201228
fast_data u8 evt_update_epm = 0xff;
//share_data u8 host_sec_bitz;
share_data_zi void *shr_dtag_meta;
share_data_zi void *shr_ddtag_meta;

share_data tEcctReg ecct_pl_info_cpu2[MAX_RC_REG_ECCT_CNT];
share_data tEcctReg ecct_pl_info_cpu4[MAX_RC_REG_ECCT_CNT];
share_data u8 ecct_pl_cnt2;
share_data u8 ecct_pl_cnt4;
share_data_zi lda_t *gc_lda_list;  //gc lda list from p2l
#endif
extern tencnet_smart_statistics_t *tx_smart_stat;

#if (CPU_ID == CPU_BE)
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
extern struct nvme_telemetry_host_initated_log *telemetry_host_data;

ddr_code void get_telemetry_ErrHandleVar_SMART_CPU_BE() //CPU_MSG_FW_ARD_HANDLER, handle FW ARD queue
{  
    telemetry_host_data->errorhandle.tel_program_fail_count = tx_smart_stat->program_fail_count;
    telemetry_host_data->errorhandle.tel_erase_fail_count = tx_smart_stat->erase_fail_count;
    telemetry_host_data->errorhandle.tel_reallocated_sector_count = tx_smart_stat->reallocated_sector_count;
    telemetry_host_data->errorhandle.tel_host_uecc_detection_cnt = tx_smart_stat->host_uecc_detection_cnt;
    telemetry_host_data->errorhandle.tel_uncorrectable_sector_count = tx_smart_stat->uncorrectable_sector_count;
    telemetry_host_data->errorhandle.tel_nand_ecc_detection_count = tx_smart_stat->nand_ecc_detection_count;
    telemetry_host_data->errorhandle.tel_nand_ecc_correction_count = tx_smart_stat->nand_ecc_correction_count;
    telemetry_host_data->errorhandle.tel_hcrc_error_count[0] = tx_smart_stat->hcrc_error_count[0];
    telemetry_host_data->errorhandle.tel_hcrc_error_count[1] = tx_smart_stat->hcrc_error_count[1];
    telemetry_host_data->errorhandle.tel_hcrc_error_count[2] = tx_smart_stat->hcrc_error_count[2];
    telemetry_host_data->errorhandle.tel_raid_recovery_fail_count = tx_smart_stat->raid_recovery_fail_count;

    cpu_msg_issue(CPU_FE - 1, CPU_MSG_GET_TELEMETRY_DATA_DONE, 0, 0);
}

ddr_code void __get_telemetry_ErrHandleVar_SMART_CPU_BE(volatile cpu_msg_req_t *req) //CPU_MSG_FW_ARD_HANDLER
{
    get_telemetry_ErrHandleVar_SMART_CPU_BE();
}

#endif
#endif

#ifdef ERRHANDLE_GLIST
#if (CPU_ID == CPU_BE)						//avoid CPU4 compile error at pg_cnt_in_xlc_spb because it define in CPU2
ddr_code void get_nand_byte_written(void)
{
    // u32 avg_ec, max_ec, min_ec, total_ec;
    // extern __attribute__((weak)) void get_avg_erase_cnt(u32 * avg_erase, u32 * max_erase, u32 *min_erase, u32 *total_ec);
    // get_avg_erase_cnt(&avg_ec, &max_ec, &min_ec, &total_ec);
    //shr_nand_bytes_written = (pg_cnt_in_xlc_spb*16*nand_block_num()*shr_avg_erase)>>ctz(0x8000); //Unit is 32MB

    //shr_nand_bytes_written = 0;	// DBG, SMARTVry
    //shr_nand_bytes_written += ((Nand_Written>>5) & 0xFFFFFFFF); //Unit is 32MB	// Update in pstream_supply directly
    //Nand_Written = 0;
    //ncl_glist_trace(LOG_INFO, 0, "shr_nand_bytes_written = %d", shr_nand_bytes_written);
}

ddr_code void Init_Nand_Written(u32 written)
{
    shr_nand_bytes_written = written;
}

ddr_code void ipc_Init_Nand_Written(volatile cpu_msg_req_t *req)
{
    u32 written = req->pl;
	Init_Nand_Written(written);
}

#endif

ddr_code void init_eh_mgr(void)	// DBG, PgFalVry
{
	memset(Fail_Blk_Info, INV_U8, sizeof(Fail_Blk_Info));
    bFail_Blk_Cnt 	= 0;
	fgFail_Blk_Full = false;

	memset((void *)rdOpBlkFal, INV_U8, sizeof(rdOpBlkFal));	// ISU, RdOpBlkHdl
}

ddr_code void ipc_init_eh_mgr(volatile cpu_msg_req_t *req)	// FET, PfmtHdlGL
{
	init_eh_mgr();
}


ddr_code void gl_build_table(void)
{
    // Keep it, assign pointer when power on.
    epm_glist_t *epm_glist_start = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable *)(&epm_glist_start->data[0]);

	if ((pGList->dGL_Tag != GLIST_TAG) || (pGList->bGL_Ver != GLIST_VER))
	{
		ncl_glist_trace(LOG_ERR, 0x4aa4, "[GL] Invalid tag[0x%x]", pGList->dGL_Tag);
		memset(pGList, 0, (GLIST_EPM_NEED_DTAG * DTAG_SZE));

		pGList->dGL_Tag = GLIST_TAG;
		pGList->bGL_Ver = GLIST_VER;
		pGList->bGL_VerInvert = ~(pGList->bGL_Ver);

		epm_update(GLIST_sign, (CPU_ID - 1));
	}

	init_eh_mgr();	// Power on should be ok for CPU4 to init directly, DBG, PgFalVry
}

ddr_code void gl_error_to_aer(u8 errType)	// DBG, PgFalVry
{
	extern __attribute__((weak)) void nvmet_evt_aer_in();

	u32 type;	
	switch(errType)
	{
		case GL_PROG_FAIL:
			type = 7;//prog fail
			break;
		case GL_ERASE_FAIL:
			type = 8;//erase fail
			break;
		default:
			return;
	}

	nvmet_evt_aer_in((2<<16)|type, 0);
}

ddr_code u16 wGListRegBlock(sGLEntry* errInfo)
{
    // This function will be called in ISR.c
    sGLEntry *pNewGLEntry;
    u32    dTotalUsedCnt;
    u32    dIdx, dIdxBot;
    bool   need_gc = true;
    bool   need_updtEPM = false; // FET, GL2BFalFlag
    bool   smart_upt = true;
#if UNC_MARK_BAD_VERIFY    
    bool   readverify = false;
#endif
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);


    // Step 1. Check if parameters overflow
    if (errInfo->bDie >= nand_info.geo.nr_luns * nand_info.geo.nr_channels * nand_info.geo.nr_targets)           //ERRTODO
    {
		ncl_glist_trace(LOG_ERR, 0xb97a, "[GL] Invalid D[%d]", errInfo->bDie);
        return GLERR_PARAM_OVERFLOW;
    }

    // Step 2. Check if GList is full.
	// TBC_EH, make sure invalid idx will not access illegal addr through GList[idx].
	// TBD_EH, GList entry not handled yet and GList cycle increased, may over write not handled entry.
    if (pGList->wGL_Mark_Cnt >= GL_TOTAL_ENTRY_CNT)    // GL_mod, Paul_20201130
    {
        pGList->dCycle                += 1;
        pGList->wGL_Mark_Cnt          = 0;
    }

    if (pGList->dCycle)
        dTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
    else
        dTotalUsedCnt = pGList->wGL_Mark_Cnt;


    // Step 3. Check if Block is registed in G-List, return function
    for (dIdx = 0; dIdx < dTotalUsedCnt; dIdx++)
    {
        dIdxBot = (dTotalUsedCnt - 1) - dIdx;           // 0 ~ (dTotalUsedCnt - 1)

        if ((pGList->GlistEntry[dIdxBot].bDie == errInfo->bDie) && (pGList->GlistEntry[dIdxBot].wPhyBlk == errInfo->wPhyBlk))
        {
            if (pGList->GlistEntry[dIdxBot].Mark_Bad == false)
            {
#if UNC_MARK_BAD_VERIFY
                if (pGList->GlistEntry[dIdxBot].ReadVerify == 1) // handle read verify
                {
                    readverify = true;
                    pNewGLEntry = &(pGList->GlistEntry[dIdxBot]);
                    memset(pNewGLEntry, 0xFF, sizeof(sGLEntry)); // remove old GList
                    ncl_cmd_trace(LOG_INFO, 0xb157, "[GL] Handle Read Verify Case, (LB/D/PB)[%d/%d/%d]", errInfo->wLBlk_Idx, errInfo->bDie, errInfo->wPhyBlk);
                    break;
                }
#endif                
                ncl_cmd_trace(LOG_ERR, 0xd4da, "[GL] Recorded, (LB/D/PB)[%d/%d/%d]", errInfo->wLBlk_Idx, errInfo->bDie, errInfo->wPhyBlk);

                #ifdef EH_ENABLE_2BIT_RETRY 
               	if (((errInfo->bError_Type == GL_RECV_2BREAD_FAIL) || (errInfo->bError_Type == GL_RECV_NO_ARD_ERROR)) && (pGList->GlistEntry[dIdxBot].RD_RecvFail == false)) // FET, GL2BFalFlag
           		{
               	    // Do not modify ErrType of GlistEntry[dIdxBot] to keep the original defect type.
               	    if(errInfo->bError_Type == GL_RECV_2BREAD_FAIL)
                    {
				        pGList->GlistEntry[dIdxBot].RD_2BFail = 1;
                        if(errInfo->Blank)
                        {
                            pGList->GlistEntry[dIdxBot].Blank = 1;
                            smart_upt = false;
                        }
                    }
                    
                    if(errInfo->DFU)
                        pGList->GlistEntry[dIdxBot].DFU = 1;

                    pGList->GlistEntry[dIdxBot].RD_RecvFail = 1;
					need_updtEPM = true;

                    #if RAID_SUPPORT_UECC      //smart info
                    if(smart_upt)
                    {
					    if(pGList->GlistEntry[dIdxBot].RD_Ftl)
					        tx_smart_stat->raid_recovery_fail_count++;
						else
					        tx_smart_stat->uncorrectable_sector_count++;
						ncl_cmd_trace(LOG_ERR, 0x3364, "[DBG]uecc SMART rc fail, host[%d] intr[%d]", tx_smart_stat->uncorrectable_sector_count, tx_smart_stat->raid_recovery_fail_count);
                    }
					#endif
				}
                #endif

                #if RAID_SUPPORT
                if((errInfo->bError_Type == GL_RECV_FAIL) && (pGList->GlistEntry[dIdxBot].RD_RecvFail == false))
                {
                    pGList->GlistEntry[dIdxBot].RD_RecvFail = 1;    // Kept for layer RAID. (die RAID recv failed should record another GList entry.)
                }
                #endif

                if (errInfo->CPU4_Flag)
                {
                    Err_Info_Rptr = ((Err_Info_Rptr + 1) & (Err_Info_size - 1));
                }

				if (need_updtEPM)    // FET, GL2BFalFlag
				{
                    extern volatile u8 plp_trigger;
                    extern u8 epm_Glist_updating;
                    
  				    if (plp_trigger != 0xEE && epm_Glist_updating == 0)
  				    {
  				        epm_Glist_updating = 1;  //Avoid updating epm glist too many times.
                        evt_set_cs(evt_update_epm, 0, 0, CS_TASK);
  				    }
			    }

                return GLERR_GBLOCK_EXIST;
            }
            else
            {
                ncl_cmd_trace(LOG_ERR, 0xed02, "[GL] NG? hit GL(Idx/LB/D/PB)[%d/%d/%d/%d]", dIdxBot, errInfo->wLBlk_Idx, errInfo->bDie, errInfo->wPhyBlk);
                return GLERR_GBLOCK_EXIST;
			    // Consider Defect BMP is not sync w/ GList mark bad field.
			    // 1. By manual.
				// 2. Power on loading issue w/ UNC.
				// Print log and let it register another entry again.
				//break;	// This break may cause record the same phyBlk even if this had not been handled.
            }
        }
    }


    // Step 4. fill new GList entry.
    errInfo->NeedGC   = ((errInfo->bError_Type == GL_ERASE_FAIL) || (errInfo->RD_Ftl) || (errInfo->wLBlk_Idx == 0)) ? false : true;
    errInfo->Mark_Bad = false;
#if UNC_MARK_BAD_VERIFY
    if (readverify)
        errInfo->ReadVerify = 2; // Second times hit this die
#endif
    pNewGLEntry = &(pGList->GlistEntry[pGList->wGL_Mark_Cnt]);
    *pNewGLEntry = *errInfo;

	if (errInfo->CPU4_Flag)
    {
        Err_Info_Rptr = ((Err_Info_Rptr + 1) & (Err_Info_size - 1));
    }

    #if RAID_SUPPORT_UECC       //smart info
    if((errInfo->bError_Type == GL_1BREAD_FAIL) || (errInfo->bError_Type == GL_GC_1BREAD_FAIL) || (errInfo->bError_Type == GL_P2L_1BREAD_FAIL))
    {
        #if 1
        tx_smart_stat->host_uecc_detection_cnt++;
        tx_smart_stat->nand_ecc_detection_count++;
        #else
        //nand_ecc_detection_cnt++;   // the blk count of entering RAID function
        smart_uecc_info[uecc_smart_ptr].lblk_idx = errInfo->wLBlk_Idx;
        smart_uecc_info[uecc_smart_ptr].err_type = uecc_detect;
        cpu_msg_issue(CPU_FTL - 1, CPU_MSG_UECC_FILL_SMART_INFO, 0, (u32)&smart_uecc_info[uecc_smart_ptr]);
        uecc_smart_ptr++;
        if(uecc_smart_ptr >= smart_uecc_info_cnt)
            uecc_smart_ptr = 0;
        #endif
		ncl_cmd_trace(LOG_ERR, 0x899b, "[DBG]uecc SMART detect, host[%d] intr[%d] ttl[%d]", tx_smart_stat->host_uecc_detection_cnt, internal_uecc_detection_cnt, tx_smart_stat->nand_ecc_detection_count);
    }
	else if(errInfo->bError_Type == GL_FTL_1BREAD_FAIL)
    {
        internal_uecc_detection_cnt++;
        tx_smart_stat->nand_ecc_detection_count++;
        ncl_cmd_trace(LOG_ERR, 0xc743, "[DBG]uecc SMART detect, host[%d] intr[%d] ttl[%d]", tx_smart_stat->host_uecc_detection_cnt, internal_uecc_detection_cnt, tx_smart_stat->nand_ecc_detection_count);
    }
	else if(errInfo->bError_Type == GL_RECV_1BREAD_FAIL)
    {
        if(errInfo->RD_Ftl)
            internal_uecc_detection_cnt++;
		else
			tx_smart_stat->host_uecc_detection_cnt++;
		tx_smart_stat->nand_ecc_detection_count++;
		ncl_cmd_trace(LOG_ERR, 0xb17b, "[DBG]uecc SMART detect, host[%d] intr[%d] ttl[%d]", tx_smart_stat->host_uecc_detection_cnt, internal_uecc_detection_cnt, tx_smart_stat->nand_ecc_detection_count);
	}
    else if (errInfo->bError_Type == GL_PROG_FAIL)
    {
    	gl_error_to_aer(GL_PROG_FAIL); // DBG, PgFalVry
    	tx_smart_stat->program_fail_count++;
	    //shr_program_fail_count++;
        host_prog_fail_cnt++;
    }
	else if (errInfo->bError_Type == GL_ERASE_FAIL)
	{
		gl_error_to_aer(GL_ERASE_FAIL);// DBG, PgFalVry
		tx_smart_stat->erase_fail_count++;
		//shr_erase_fail_count++;
	}
    #if 0//def EH_ENABLE_2BIT_RETRY 
   	else if((errInfo->bError_Type == GL_RECV_2BREAD_FAIL) || (errInfo->bError_Type == GL_RECV_NO_ARD_ERROR))// FET, GL2BFalFlag  //for ftl case
   	{
   	    if(errInfo->bError_Type == GL_RECV_2BREAD_FAIL)
	        errInfo->RD_2BFail = 1;
        
        errInfo->RD_RecvFail = 1;
		need_updtEPM = true;

	    if(errInfo->RD_Ftl)
        {   
            internal_uecc_detection_cnt++;
	        internal_rc_fail_cnt++;
        }
        else
        {
            host_uecc_detection_cnt++;
            uncorrectable_sector_count++;
        }
        nand_ecc_detection_cnt++;
		ncl_cmd_trace(LOG_ERR, 0xa8a3, "[DBG]uecc SMART rc detect:(hst/intr)[%d/%d], fail:(hst/intr)[%d/%d]",host_uecc_detection_cnt, host_uecc_detection_cnt, uncorrectable_sector_count, internal_rc_fail_cnt);
    }
    #endif
    #endif


    // Modify log format to match dump GList
    ncl_cmd_trace(LOG_ERR, 0x52dc, "[GL] RegBlk, [%d](Err/FTL_gcOp|LB/D/PB)[%d/0x%x|%d/%d/%d]", \
		pGList->wGL_Mark_Cnt, pNewGLEntry->bError_Type, pNewGLEntry->RD_Ftl << 1 | pNewGLEntry->gcOpen, \
		pNewGLEntry->wLBlk_Idx, pNewGLEntry->bDie, pNewGLEntry->wPhyBlk \
		);
    ncl_cmd_trace(LOG_ERR, 0x99ef, "[GL] _(Pg/Au|NdGC/MkBd/RcvFl/2BFl)[%d/%d|%d/%d/%d/%d]", \
		pNewGLEntry->wPage, pNewGLEntry->AU, pNewGLEntry->NeedGC, \
		pNewGLEntry->Mark_Bad, pNewGLEntry->RD_RecvFail, pNewGLEntry->RD_2BFail\
		);

    #ifdef ERR_PRINT_LOG
	ncl_cmd_trace(LOG_ERR, 0x431b, "[GL] _DBG, [LB|Cyc](%d|%d)", errInfo->wLBlk_Idx, pGList->dCycle);
    #endif

    pGList->wGL_Mark_Cnt++;
    
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)  
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    epm_ftl_data->glist_inc_cnt++;
#endif 

    __dmb();
 
    // Step 5. GList recorded, fill errInfo to handle spb.
	Fail_Blk_Info_Temp.all = 0;   

	if (errInfo->bError_Type == GL_ERASE_FAIL)
    {
        // Need not GC, to trigger mark bad directly.
        need_gc = false;
    }
	else	// Not erase failed, consider data on SPB.
	{
		if (errInfo->RD_Ftl)
		{
			need_gc = false;
			if (errInfo->bError_Type == GL_PROG_FAIL)
			{				
				Fail_Blk_Info_Temp.b.bakupPBT = 1;	// Parity_allocate_cnt <= 5 issue.
			}
		}
		else
		{
			//Fail_Blk_Info_Temp.b.bakupPBT = 1;	// Set after gc done, ISU, BakupPBTLat
		}
	}	

	// Handle Fail_Blk_Info is full in EH_SetFailBlk (ipc)
	// Clsdone flag is needless, let ErrHandle_Task handle if it is need to close blk.
	//Fail_Blk_Info_Temp.all          = 0;   

	Fail_Blk_Info_Temp.b.wBlkIdx    = errInfo->wLBlk_Idx;
	//Fail_Blk_Info_Temp.b.bType      = BlkType;
	//Fail_Blk_Info_Temp.b.bClsdone   = Clsdone;
	Fail_Blk_Info_Temp.b.need_gc    = need_gc;
	Fail_Blk_Info_Temp.b.gcOpen		= errInfo->gcOpen;	// ISU, GCOpClsBefSuspnd
		
	extern volatile u8 plp_trigger;
	// Handle by SPB, ISU, SPBErFalHdl // EH_TBC, non SPB, EPM_Blks
	// Skip trig handle defect before FTL init done. (eh_scan_glist_trig_handle handle it later)
	if ((plp_trigger != 0xEE) && (FTL_INIT_DONE))	// Handle by SPB, ISU, SPBErFalHdl // EH_TBC, non SPB, EPM_Blks
	{
		if (errInfo->bError_Type == GL_ERASE_FAIL)
		{	// handle in call back after spb erase done.
		}
		else
		{
			if (errInfo->RD_Ftl)	// ISU, HdlPBTAftForceCls
            {         
				//evt_set_cs(evt_force_pbt, true, DUMP_PBT_NORMAL_PARTIAL, CS_TASK);	// ISU, EHPerfImprove, Paul_20220221
				evt_set_cs(evt_force_pbt, true, DUMP_PBT_SHUTTLE, CS_TASK);
            }
            else
            {
                    EH_SetFailBlk(Fail_Blk_Info_Temp.all);	
            }
		    // Move to ErrHandle_Task, ISU, TSSP, PushGCRelsFalInfo
		    //epm_update(GLIST_sign, (CPU_ID-1));

			// Trigger save log in NAND
			flush_to_nand(EVT_MARK_GLIST);	// ISU, EHPerfImprove, Paul_20220221
		}
	}
		
    return GLERR_NO_ERROR;
}

ddr_code void ipc_wGListRegBlock(volatile cpu_msg_req_t *req)
{

    sGLEntry* errInfo = (sGLEntry*) req->pl;
    wGListRegBlock(errInfo);
}

// Call by CPU2: 
//	(1)spb erase_done (2)register GL (3)scan GL trig EH-ipc
// To avoid too many ipc from CPU2 -> CPU3.
// 	Consider case: physical blocks in an spb all are all marked in GList.
ddr_code void EH_SetFailBlk(u32 Tempall)
{
    u8 bIdx = 0, bfreeIdx = INV_U8;	// DBG, PgFalVry
	MK_FailBlk_Info failBlkInfo = {.all = Tempall};
    if (bFail_Blk_Cnt >= MAX_FAIL_BLK_CNT)
    {
        fgFail_Blk_Full = true;
        return;	// EH_TBD, notice caller insert Fail_Blk_Info failed.
    }

    // Search if spb is recorded, and update its flags.
    for (bIdx = 0; bIdx < MAX_FAIL_BLK_CNT; bIdx++)
    {
        if (Fail_Blk_Info[bIdx].b.wBlkIdx == failBlkInfo.b.wBlkIdx)
        {
            ncl_cmd_trace(LOG_ERR, 0xaa01, "[EH] FalBlkInfo recorded, idx[%d]=spb[%d]", bIdx, failBlkInfo.b.wBlkIdx);
            bfreeIdx = INV_U8;
            break;
        }
		else if (Fail_Blk_Info[bIdx].b.wBlkIdx == INV_U16)	// DBG, PgFalVry
		{
			if (bfreeIdx == INV_U8)
				bfreeIdx = bIdx;	
		}
    }

	if (bfreeIdx < MAX_FAIL_BLK_CNT)
	{
	    // Insert a new spb in FailBlkInfo array inorderly.
		Fail_Blk_Info[bfreeIdx].all = failBlkInfo.all;
	    
	    bFail_Blk_Cnt++;
	    ncl_cmd_trace(LOG_ERR, 0xac5a, "[EH] FalBlkInfo ins, (tot|idx/blk)[%d|%d/%d]", bFail_Blk_Cnt, bfreeIdx, failBlkInfo.b.wBlkIdx);
		__dmb();

	    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SET_ERR_TASK, 0, failBlkInfo.all);	// ISU, TSSP, PushGCRelsFalInfo
	}
}

//ddr_code void EH_DelFailBlk(u16 wBlk)	// ISU, BakupPBTLat
ddr_code void EH_DelFailBlk(MK_FailBlk_Info failBlkInfo)
{
	u16 wBlk = failBlkInfo.b.wBlkIdx;	// ISU, BakupPBTLat
	u8  bIdx = 0;

	#if 0
    u16 dTotalUsedCnt = 0;
    u16 dIdx = 0;
    u16 dIdxBot = 0;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);
    if (pGList->dCycle)
        dTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
    else
        dTotalUsedCnt = pGList->wGL_Mark_Cnt;
	#endif

    for (bIdx = 0; bIdx < MAX_FAIL_BLK_CNT; bIdx++)
    {
        if (Fail_Blk_Info[bIdx].b.wBlkIdx == wBlk)
        {
        	// FTL case only, user blk should not happen due to ErFal handle by SPB, DBG, PgFalVry
        	#if 0
		 	for (dIdx = 0; dIdx < dTotalUsedCnt; dIdx++)
		 	{
		     	dIdxBot = (dTotalUsedCnt - 1) - dIdx;   // 0 ~ (dTotalUsedCnt - 1)

				//ftl_apl_trace(LOG_ERR, 0, "[EH]dIdxBot is:%d,dTotalUsedCnt:%d \n", dIdxBot, dTotalUsedCnt);

				if ((pGList->GlistEntry[dIdxBot].wLBlk_Idx == wBlk) && (pGList->GlistEntry[dIdxBot].Mark_Bad == false))
                {
                    ncl_cmd_trace(LOG_ERR, 0xe91c, "[EH] non hdl GL, [FalBlkInfIdx/GLIdx/spb](%d/%d/%d)", bIdx, dIdxBot, wBlk);

					// ISU, TSSP, PushGCRelsFalInfo
					MK_FailBlk_Info failBlkInfo;
					failBlkInfo.all = 0;
					failBlkInfo.b.wBlkIdx = spb_id;
					failBlkInfo.b.need_gc = pGList->GlistEntry[dIdxBot].NeedGC;					
                    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SET_ERR_TASK, 0, failBlkInfo.all);
                    return;
                }
		 	}
			#endif

			// Whole SPB handled, check if need to backup PBT, FET, MakBadBakupPBT
			//if (Fail_Blk_Info[bIdx].b.bakupPBT)		// ISU, BakupPBTLat
			//{
			//	evt_set_cs(evt_force_pbt, true, true, CS_TASK);	// WA, parity_allocate_cnt == 5, for verify
			//}

			// DBG, PgFalVry
            Fail_Blk_Info[bIdx].all = INV_U32;	// Mark the slot free, avoid to adjust the elements idx, due to CPU3 is refering to it.
            bFail_Blk_Cnt--;	// Decrease cnt only when target found, ISU, TSSP, PushGCRelsFalInfo
            break;
        }
    }
    //bFail_Blk_Cnt--;	// ISU, TSSP, PushGCRelsFalInfo
	// ISU, BakupPBTLat
	if (failBlkInfo.b.bakupPBT)
	{
		evt_set_cs(evt_force_pbt, true, DUMP_PBT_SHUTTLE, CS_TASK);	// WA, parity_allocate_cnt == 5, for verify // ISU, EHPerfImprove
	}
    //ncl_cmd_trace(LOG_ERR, 0, "[EH] FalBlkInfo del, (tot|idx/blk)[%d|%d/%d]", bFail_Blk_Cnt, bIdx, wBlk);
    ncl_cmd_trace(LOG_ERR, 0x5833, "[EH] FalBlkInfo del, (tot|idx/blk|all)[%d|%d/%d|0x%x]", bFail_Blk_Cnt, bIdx, wBlk, failBlkInfo.all);
	__dmb();

    if ((bFail_Blk_Cnt < MAX_FAIL_BLK_CNT) && (fgFail_Blk_Full == true))	// ISU, TSSP, PushGCRelsFalInfo
    {
		// Scan GList if there is any entry had not be handled.
        // Occurrs when register GList but it doesn't exist in Fail_Blk_Info (full).
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SCAN_GLIST_TRG_HANDLE, 0, EH_BUILD_GL_TRIG_EHTASK);	// FET, PfmtHdlGL
		
        fgFail_Blk_Full = false;
    }
}


ddr_code void __EH_DelFailBlk(volatile cpu_msg_req_t *req)
{
    //u16 wBlk = (u16) req->pl;	// ISU, BakupPBTLat
    //EH_DelFailBlk(wBlk);
	MK_FailBlk_Info Temp =  {.all = req->pl};
	EH_DelFailBlk(Temp);
}

ddr_code void __EH_SetFailBlk(volatile cpu_msg_req_t *req)
{
    MK_FailBlk_Info Temp =  {.all = req->pl};
    EH_SetFailBlk(Temp.all);

}

ddr_code void Set_Fail_OP_Blk(u16 Blk)
{
	u32 i = 0;
	for(i = 0; i < MAX_OPEN_CNT; i++)
	{
		if(wOpenBlk[i] == Blk)
		{
			ncl_cmd_trace(LOG_ERR, 0xcd02, "Blk:%d in wOpenBlk array", Blk);
			return;
		}
	}
	ncl_cmd_trace(LOG_ERR, 0x314b, "[GL]Host_Open: 0x%x, GC_Open: 0x%x", ps_open[FTL_CORE_NRM], ps_open[FTL_CORE_GC]);
	ncl_cmd_trace(LOG_ERR, 0x9d54, "[GL]Blk:0x%x is Open blk", Blk);
	wOpenBlk[bErrOpCnt] = Blk;
	bErrOpCnt++;						//TBC_EH, >4
	sys_assert(bErrOpCnt <= MAX_OPEN_CNT);
	ncl_cmd_trace(LOG_ERR, 0x6816, "bErrOpCnt:%d", bErrOpCnt);
	__dmb();
}

ddr_code void __Set_Fail_OP_Blk(volatile cpu_msg_req_t *req)
{
    u16 wBlk = (u16) req->pl;
    Set_Fail_OP_Blk(wBlk);
}

ddr_code void Del_Err_OP_Cnt(u16 Blk)
{
    u16 bIdx         = 0;
    u16 bMoveIdx     = 0;
	bool notfind 	= true;
    for (bIdx = 0; bIdx < MAX_OPEN_CNT; bIdx++)
    {
        if (wOpenBlk[bIdx] == Blk)
        {
			notfind = false;
            for (bMoveIdx = bIdx; bMoveIdx < MAX_OPEN_CNT; bMoveIdx++)
            {
                if (bMoveIdx == MAX_OPEN_CNT - 1)
                {
                   wOpenBlk[bIdx] = 0xFFFF;
                    break;
                }
                wOpenBlk[bMoveIdx] = wOpenBlk[bMoveIdx + 1];
            }
			bErrOpCnt--;
            ncl_cmd_trace(LOG_ERR, 0xefe4, "[EH] OpBlk del, spb:%d index:%d, bErrOpCnt:%d", Blk, bIdx, bErrOpCnt);
            break;
        }
    }
	if (notfind)
	{
		ncl_cmd_trace(LOG_ERR, 0x712b, "[EH] Not find spb:%d", Blk);
	}
}

ddr_code void __Del_Err_OP_Cnt(volatile cpu_msg_req_t *req)
{
    u16 wBlk = (u16) req->pl;
    Del_Err_OP_Cnt(wBlk);
}


ddr_code void Read_GList_Header(u32 *Read_Glist)
{
    u16 bTotalUsedCnt   = 0;
    u16 bDefErCnt       = 0;
    u16 bDefWrCnt       = 0;
    u16 bDefRdCnt       = 0;
    u16 Idx             = 0;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);

    if (pGList->dCycle)
        bTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
    else
        bTotalUsedCnt = pGList->wGL_Mark_Cnt;

    Read_Glist[0]  = pGList->dGL_Tag;
    Read_Glist[1]  = 0x10;               //type
    Read_Glist[1] |= pGList->bGL_Ver << 8;
    Read_Glist[2]  = bTotalUsedCnt * 4;
    for (Idx = 0; Idx < bTotalUsedCnt; Idx++)
    {
        if (pGList->GlistEntry[Idx].bError_Type == GL_PROG_FAIL)
        {
            bDefWrCnt++;
        }
        else if (pGList->GlistEntry[Idx].bError_Type == GL_ERASE_FAIL)
        {
            bDefErCnt++;
        }
        else
        {
            bDefRdCnt++;
        }
    }
    Read_Glist[4]   = bDefErCnt;
    Read_Glist[4]  |= bDefWrCnt << 16;
    Read_Glist[5]   = bDefRdCnt;
}


ddr_code void Read_GList_PayLoad(u32 *Read_Glist)
{
    u16 bTotalUsedCnt   = 0;
    u16 Idx             = 0;
    //pGList = &EEPROM_data.nand.element.GList;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);

    if (pGList->dCycle)
        bTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
    else
        bTotalUsedCnt = pGList->wGL_Mark_Cnt;

    for (Idx = 0; Idx < bTotalUsedCnt; Idx++)
    {
        Read_Glist[Idx] |= pGList->GlistEntry[Idx].bError_Type;
        Read_Glist[Idx] |= pGList->GlistEntry[Idx].bDie << 8;
        Read_Glist[Idx] |= pGList->GlistEntry[Idx].wLBlk_Idx << 16;
    }

}

ddr_code void get_Die_fail_count()
{
    u16 totalUsedCnt   = 0;
    u16 Idx             = 0;
	u16 dieFailThrhld	= MAX_DIE_FAIL_CNT;	// DBG, SMARTVry (2)   
	u16 failcnt         = 0;
    u8 die              = 0;
    u8 totaldie         = 128;
    u8 diefailcnt       = 0;
    
    //pGList = &EEPROM_data.nand.element.GList;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);

	if (nand_info.geo.nr_blocks > 1000)	// DBG, SMARTVry (2)   
		dieFailThrhld = MAX_DIE_FAIL_CNT * 2;

    if (pGList->dCycle)
        totalUsedCnt = GL_TOTAL_ENTRY_CNT;
    else
        totalUsedCnt = pGList->wGL_Mark_Cnt;

    if ((!totalUsedCnt) && (pGList->dCycle == 0))
    {
        tx_smart_stat->die_fail_count = 0;
    }
    else
    {
        for (die = 0; die < totaldie; die++)
        {
            for (Idx = 0; Idx < totalUsedCnt; Idx++)
            {
                if ((pGList->GlistEntry[Idx].bError_Type == GL_ERASE_FAIL) || (pGList->GlistEntry[Idx].bError_Type == GL_PROG_FAIL))
                {
                    if (pGList->GlistEntry[Idx].bDie == die)
                    {
                        failcnt++;
                    }
                }

            }
            if (failcnt > dieFailThrhld)	// DBG, SMARTVry (2)   
            {
                diefailcnt++;
            }
            failcnt = 0;
        }
        if (diefailcnt > tx_smart_stat->die_fail_count)
        {
            sys_assert(diefailcnt <= totaldie);
            tx_smart_stat->die_fail_count = diefailcnt;
        }
    }
}

ddr_code void get_program_and_erase_fail_count()
{
    extern struct nand_info_t shr_nand_info;	// nand infomation
    shr_total_die = shr_nand_info.geo.nr_channels * shr_nand_info.geo.nr_targets * shr_nand_info.geo.nr_luns;
    u16 bTotalUsedCnt   = 0;
    u16 bDefWrCnt       = 0;
    u16 bDefErCnt       = 0;
    u16 Idx             = 0;

    //pGList = &EEPROM_data.nand.element.GList;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);

    if (pGList->dCycle)
        bTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
    else
        bTotalUsedCnt = pGList->wGL_Mark_Cnt;

    for (Idx = 0; Idx < bTotalUsedCnt; Idx++)
    {
        if (pGList->GlistEntry[Idx].bError_Type == GL_PROG_FAIL)
        {
            bDefWrCnt++;
        }
        if (pGList->GlistEntry[Idx].bError_Type == GL_ERASE_FAIL)
        {
            bDefErCnt++;
        }
    }
//////// Alan Huang for percentage
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	srb_t *srb = (srb_t *) SRAM_BASE;
	u32  WrPerc, ErPerc;

	if(epm_ftl_data->Phyblk_OP == 0)
	{
		WrPerc = ((1*100 + epm_ftl_data->Phyblk_OP) * FullCap_PhyblkCnt * (srb->cap_idx -1) - bDefWrCnt*100) / (FullCap_PhyblkCnt * (srb->cap_idx -1));
		ErPerc = ((1*100 + epm_ftl_data->Phyblk_OP) * FullCap_PhyblkCnt * (srb->cap_idx -1) - bDefErCnt*100) / (FullCap_PhyblkCnt * (srb->cap_idx -1));

		if(WrPerc >100)
			WrPerc = 100;

		if(ErPerc >100)
			ErPerc = 100;
	}
	else
	{
		WrPerc = ((1*100 + epm_ftl_data->Phyblk_OP) * FullCap_PhyblkCnt * (srb->cap_idx -1) - bDefWrCnt*100) / (FullCap_PhyblkCnt * (srb->cap_idx -1));
		ErPerc = ((1*100 + epm_ftl_data->Phyblk_OP) * FullCap_PhyblkCnt * (srb->cap_idx -1) - bDefErCnt*100) / (FullCap_PhyblkCnt * (srb->cap_idx -1));

		if(WrPerc >100)
			WrPerc = 100;

		if(ErPerc >100)
			ErPerc = 100;
	}
		//ncl_cmd_trace(LOG_ERR, 0, "WrPerc %d, ErPerc %d, OP %d, capIdx %d, phyCnt %d ",WrPerc, ErPerc, epm_ftl_data->Phyblk_OP, srb->cap_idx, FullCap_PhyblkCnt );
////////

    //shr_program_fail_count = bDefWrCnt;	// DBG, SMARTVry
    //shr_erase_fail_count = bDefErCnt;
	//#if (CPU_ID == CPU_BE)
	//get_nand_byte_written();	// Update in pstream_supply directly
	//#endif
	get_Die_fail_count();

    ncl_glist_trace(LOG_INFO, 0x89bf, "ScnGLFalCnt(Pg/Er)[%d/%d]", bDefWrCnt, bDefErCnt);

}

// fast_code void get_erase_fail_count(u32 *erase_fail_count)
// {
//     u16 bTotalUsedCnt   = 0;
//     u16 bDefErCnt       = 0;
//     u16 Idx             = 0;

//     //pGList = &EEPROM_data.nand.element.GList;
//     epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
//     pGList = (sGLTable*)(&epm_glist_start->data[0]);

//     if (pGList->dCycle)
//         bTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
//     else
//         bTotalUsedCnt = pGList->wGL_Mark_Cnt;

//     for (Idx = 0; Idx < bTotalUsedCnt; Idx++)
//     {
//         if (pGList->GlistEntry[Idx].bError_Type == GL_ERASE_FAIL)
//         {
//             bDefErCnt++;
//         }
//     }
//     erase_fail_count[0]   = bDefErCnt;

// }
#endif



//=============================================================================//
/*
 *                           ECC Table region
 */
//=============================================================================//
#ifdef ERRHANDLE_ECCT
#if (CPU_ID == CPU_BE)
ddr_code u16 wECC_Search_Idx(u32 lda, u16 left_margin, u16 right_matgin, u8 search_mode)
{
    u32 dErr_LDA;
    u16 wMid_idx;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);

    switch(search_mode){
        case ECC_BIN_SEARCH:  //1
        {
            if (pECC_table->ecc_table_cnt == 0) return 0xFFFF;

#ifdef While_break
			u64 start = get_tsc_64();
#endif
            while (left_margin <= right_matgin){
                wMid_idx = (left_margin + right_matgin) / 2;
                dErr_LDA = pECC_table->ecc_entry[wMid_idx].err_lda;

                if (dErr_LDA > lda)
                {
                    if (wMid_idx != 0) right_matgin = wMid_idx  - 1;
                    else break;
                }
                else if (dErr_LDA < lda)
                {
                    left_margin = wMid_idx + 1;
                }
                else if (dErr_LDA == lda)
                {
                    return wMid_idx;
                }

#ifdef While_break
				if(Chk_break(start,__FUNCTION__, __LINE__))
					break;
#endif
            }
            break;
        }
        case ECC_IDX_SEARCH:  //2
        {
            u16 wIdx;
            for (wIdx = left_margin; wIdx < right_matgin; wIdx++)
            {
                if (pECC_table->ecc_entry[wIdx].err_lda > lda)
                {
                    return wIdx;
                }
            }
            return pECC_table->ecc_table_cnt;
        }
        default:
            break;
    }

    return 0xFFFF;

}

ddr_code void ECC_Sorting_Table(void)
{
    u16 wIdx1, wIdx2;
    u32 derr_lda;
    u16 wbit_map, wpblk, wpage;
    u8  bau, breg_source, bdie;


    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);

    for (wIdx1 = 0; wIdx1 < pECC_table->ecc_table_cnt; wIdx1++)
    {
        for (wIdx2 = (wIdx1 + 1); wIdx2 < pECC_table->ecc_table_cnt ; wIdx2++)
        {
            if (pECC_table->ecc_entry[wIdx1].err_lda > pECC_table->ecc_entry[wIdx2].err_lda)
            {
                derr_lda    = pECC_table->ecc_entry[wIdx1].err_lda;
                wbit_map    = pECC_table->ecc_entry[wIdx1].bit_map;
                bdie        = pECC_table->ecc_entry[wIdx1].ErrInfor.die;
                wpblk       = pECC_table->ecc_entry[wIdx1].ErrInfor.pblk;
                wpage       = pECC_table->ecc_entry[wIdx1].ErrInfor.page;
                bau         = pECC_table->ecc_entry[wIdx1].ErrInfor.b.au;
                breg_source = pECC_table->ecc_entry[wIdx1].ErrInfor.b.reg_source;

                pECC_table->ecc_entry[wIdx1].err_lda               = pECC_table->ecc_entry[wIdx2].err_lda;
                pECC_table->ecc_entry[wIdx1].bit_map               = pECC_table->ecc_entry[wIdx2].bit_map;
                pECC_table->ecc_entry[wIdx1].ErrInfor.die          = pECC_table->ecc_entry[wIdx2].ErrInfor.die;
                pECC_table->ecc_entry[wIdx1].ErrInfor.pblk         = pECC_table->ecc_entry[wIdx2].ErrInfor.pblk;
                pECC_table->ecc_entry[wIdx1].ErrInfor.page         = pECC_table->ecc_entry[wIdx2].ErrInfor.page;
                pECC_table->ecc_entry[wIdx1].ErrInfor.b.au         = pECC_table->ecc_entry[wIdx2].ErrInfor.b.au;
                pECC_table->ecc_entry[wIdx1].ErrInfor.b.reg_source = pECC_table->ecc_entry[wIdx2].ErrInfor.b.reg_source;

                pECC_table->ecc_entry[wIdx2].err_lda               = derr_lda;
                pECC_table->ecc_entry[wIdx2].bit_map               = wbit_map;
                pECC_table->ecc_entry[wIdx2].ErrInfor.die          = bdie;
                pECC_table->ecc_entry[wIdx2].ErrInfor.pblk         = wpblk;
                pECC_table->ecc_entry[wIdx2].ErrInfor.page         = wpage;
                pECC_table->ecc_entry[wIdx2].ErrInfor.b.au         = bau;
                pECC_table->ecc_entry[wIdx2].ErrInfor.b.reg_source = breg_source;

            }
        }
    }
}

ddr_code void ECC_Dump_Table(void)
{
    u16 wIdx;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);

    if (pECC_table->ecc_table_cnt == 0)
    {
        ncl_glist_trace(LOG_ERR, 0x0843, "ECC Table is Empty\n");
        return;
    }

    ncl_glist_trace(LOG_ERR, 0x92ad, "ECC Table Count : %d\n", pECC_table->ecc_table_cnt);

    for (wIdx = 0; wIdx < pECC_table->ecc_table_cnt; wIdx++)
    {
        ncl_glist_trace(LOG_ERR, 0xdb17, "[ECC] Entry[%d] ErrLDA[0x%x] BitMap[0x%x] RegSource[%d]\n", wIdx, pECC_table->ecc_entry[wIdx].err_lda,
            pECC_table->ecc_entry[wIdx].bit_map, pECC_table->ecc_entry[wIdx].ErrInfor.b.reg_source);
    }

    // Show ECC Table Full Warning Message!
    if(pECC_table->ecc_table_cnt == MAX_ECC_TABLE_ENTRY)
    {
        ncl_glist_trace(LOG_ERR, 0x5414, "BuildECCTable, ECC Table FULL!\n");
    }

    wECC_Dump_Record_Table();
}

ddr_code u16 wECC_Build_Table(void)
{
    u16 wIdx;

    ncl_cmd_trace(LOG_ERR, 0x54c1, "Build ECC Table");
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);
    //_fgUpdateECC = mFALSE;

    // Step 1. Check ECC Tag, Initial ECC params if tag not existed
    if (pECC_table->ecc_table_tag != ECC_TBL_TAG || pECC_table->ecc_table_ver != ECC_TBL_VER)
    {
        ncl_cmd_trace(LOG_ERR, 0xd1cd, "ECC Table First Initialization");
        pECC_table->ecc_table_cnt = 0;
        pECC_table->ecc_table_tag = ECC_TBL_TAG;
        pECC_table->ecc_table_ver = ECC_TBL_VER;
        pECC_table->ecc_table_inver = ECC_VER_INVERT;
        //dUpdateFlag |= GLIST_AREA;
    }

    // Step 2. Error Protection for ECC Count Overflow
    if (pECC_table->ecc_table_cnt > MAX_ECC_TABLE_ENTRY)
    {
        ncl_cmd_trace(LOG_ERR, 0x8080, "ECC Table Count Overflow! ECCTableCount = %d", pECC_table->ecc_table_cnt);

        pECC_table->ecc_table_cnt = MAX_ECC_TABLE_ENTRY;
        epm_update(GLIST_sign,(CPU_ID-1));
    }

    // Step 3. Error Protection for disorder ECC Entry
    if (pECC_table->ecc_table_cnt >= 2)
    {
        for (wIdx = 0; wIdx < pECC_table->ecc_table_cnt - 1; wIdx++)
        {
            if (pECC_table->ecc_entry[wIdx].err_lda > pECC_table->ecc_entry[wIdx + 1].err_lda)
            {
                ECC_Sorting_Table();
                break;
            }
        }
    }

    ecct_cnt = pECC_table->ecc_table_cnt; //update ecct cnt in sram

    // Step 4. Dump ECC Table
    ECC_Dump_Table();

    //_dECCTableCnt = pECC_table->ecc_table_cnt;
    return 0;

}

ddr_code  void ECC_Table_Reset(void)
{
    u16 wIdx;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);

    ncl_cmd_trace(LOG_ERR, 0x5ae5, "Initial ECC Table");
    if (pECC_table->ecc_table_tag != ECC_TBL_TAG || pECC_table->ecc_table_ver != ECC_TBL_VER)
    {
        pECC_table->ecc_table_cnt = 0;
        pECC_table->ecc_table_tag = ECC_TBL_TAG;
        pECC_table->ecc_table_ver = ECC_TBL_VER;
        pECC_table->ecc_table_inver = ECC_VER_INVERT;
    }

    if (pECC_table->ecc_table_cnt > MAX_ECC_TABLE_ENTRY)
    {
        ncl_cmd_trace(LOG_ERR, 0x6eec, "ECC Table Count Overflow! ECCTableCount = %d", pECC_table->ecc_table_cnt);

        pECC_table->ecc_table_cnt = MAX_ECC_TABLE_ENTRY;
        //dUpdateFlag |= GLIST_AREA;
    }

    //if (pECC_table->ecc_table_cnt > 0)
    {
        //for (wIdx = 0; wIdx <= pECC_table->ecc_table_cnt - 1; wIdx++)
        for (wIdx = 0; wIdx <= MAX_ECC_TABLE_ENTRY - 1; wIdx++)
        {
            pECC_table->ecc_entry[wIdx].err_lda                = 0;
            pECC_table->ecc_entry[wIdx].bit_map                = 0;
            pECC_table->ecc_entry[wIdx].ErrInfor.die           = 0; //INV_U8;
            pECC_table->ecc_entry[wIdx].ErrInfor.pblk          = 0; //INV_U16;
            pECC_table->ecc_entry[wIdx].ErrInfor.page          = 0; //INV_U16;
            pECC_table->ecc_entry[wIdx].ErrInfor.all           = 0; //INV_U8;
            //pECC_table->ecc_entry[wIdx].ErrInfor.b.au          = INV_U8;
            //pECC_table->ecc_entry[wIdx].ErrInfor.b.reg_source  = INV_U8;
        }
        pECC_table->ecc_table_cnt = 0;
    }

    ecct_cnt = pECC_table->ecc_table_cnt; //update ecct cnt in sram

    //Request Update EEPROM
    //epm_update(GLIST_sign,(CPU_ID-1));
    evt_set_cs(evt_update_epm, 0, 0, CS_TASK);

}
slow_code pda_t LDA_to_PDA(u32 LDA)
{
// for cpu2 get pda
    u64 addr;
    u8 win = 0;
	pda_t pda = INV_U32;
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
	addr = (ddtag2off(shr_l2p_entry_start) | 0x40000000) + ((u64)LDA * 4);
	while (addr >= 0xC0000000)
	{
		addr -= 0x80000000;
		win++;
	}
	u8 old = ctrl0.b.cpu2_ddr_window_sel;
	ctrl0.b.cpu2_ddr_window_sel = win;
	isb();
	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
	pda = *((u32*)(u32)addr);
	ctrl0.b.cpu2_ddr_window_sel = old;
	writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
	isb();

	return pda;
}
//ddr_code u16 wECC_Register_Table(u32 lba, u32 total_len, u8 reg_sour, pda_t pda)

ddr_code void wECC_Record_Table(u64 lda, u16 bit_map, u8 reg_sour)
{
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    
    pGList = (sGLTable*)(&epm_glist_start->data[0]);
    if (pGList->wGL_ECC_Mark_Cnt >= GL_TOTAL_ECCT_ENTRY_CNT)
    {
        pGList->dECC_Cycle           += 1;
        pGList->wGL_ECC_Mark_Cnt      = 0;
    }

    pGList->GlistECCTEntry[pGList->wGL_ECC_Mark_Cnt].lda = lda;
    pGList->GlistECCTEntry[pGList->wGL_ECC_Mark_Cnt].bit_map = bit_map;
    pGList->GlistECCTEntry[pGList->wGL_ECC_Mark_Cnt++].reg_sou = reg_sour;
    __dmb();
    extern volatile u8 plp_trigger;
  	if (plp_trigger != 0xEE)
  	{
        extern u8 epm_Glist_updating;
        epm_Glist_updating = 1; //Avoid updating epm glist too many times.
        
  		evt_set_cs(evt_update_epm, 0, 0, CS_TASK);
  		//epm_update(SMART_sign, (CPU_ID - 1));
  	}
    flush_to_nand(EVT_MARK_GLIST);
}

ddr_code void wECC_Dump_Record_Table(void)
{
    u32 dTotalUsedCnt;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    ncl_glist_trace(LOG_ERR, 0xde81, "Dump ECC Record Table.");
    pGList = (sGLTable*)(&epm_glist_start->data[0]);

    if (pGList->dECC_Cycle)
        dTotalUsedCnt = GL_TOTAL_ECCT_ENTRY_CNT;
    else
        dTotalUsedCnt = pGList->wGL_ECC_Mark_Cnt;
    
    for (u32 i = 0 ; i < dTotalUsedCnt ; i++)
        ncl_glist_trace(LOG_ERR, 0x559d, "[ECC] Entry[%d] ErrLDA[0x%x] BitMap[0x%x] RegSource[%d]\n", i, pGList->GlistECCTEntry[i].lda,
            pGList->GlistECCTEntry[i].bit_map, pGList->GlistECCTEntry[i].reg_sou);

}

static ddr_code int dump_ecc_record_table(int argc, char *argv[])
{
    wECC_Dump_Record_Table();
    return 0;
}

static DEFINE_UART_CMD(dump_ecc_record_table, "dump_ecc_record_table", "",
		"", 0, 0, dump_ecc_record_table);

ddr_code u16 wECC_Register_Table(u64 lba, u32 total_len, u8 reg_sour)
{
    //fda_t fda = _pda2fda(pda);
    lda_t lda;
    pda_t pda;
    u16 wECC_idx = 0;
    u16 wIdx = 0;
	u32 ch_shift = 0, ce_shift = 0, lun_shift = 0, pl_shift = 0, LUN = 0, CH = 0, CE = 0;
    u8 bLBA_offset, bLBA_length;
	u8 numlba = 0;
	if(host_sec_bitz == 9)
		numlba = 8;
	else
		numlba = 1;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);

    //STEP1. Error protection
    if(total_len == 0) //|| lba == 0 || lba > wTotal_sector_cnt - 1) //Incorrect parameters
    {
        ncl_cmd_trace(LOG_ERR, 0x940c, "RegECCTable FAIL, Parameter Out of Range, LBA: 0x%x, Len: 0x%x", lba, total_len);
        return ECCERR_PARAM_OVERFLOW;
    }

    NEXT_LDA:
    if (pECC_table->ecc_table_cnt == MAX_ECC_TABLE_ENTRY) //ECC Table is Full
    {
        ncl_cmd_trace(LOG_ERR, 0x1bf2, "RegECCTable ECC Table Full");
        return ECCERR_ECCTABLE_FULL;
    }

    //STEP2. Get LDA, PDA & NAND info
	lda = lba  >> (LDA_SIZE_SHIFT - host_sec_bitz);
    pda = LDA_to_PDA(lda);
	ncl_cmd_trace(LOG_ALW, 0x9684, "[ECC]Reg lda 0x%x pda 0x%x", lda, pda);

	pl_shift = nand_info.pda_interleave_shift;				//2
	ch_shift = pl_shift + ctz(nand_info.geo.nr_planes); 	//3
	ce_shift = ch_shift + ctz(nand_info.geo.nr_channels);	//6
	lun_shift = ce_shift + ctz(nand_info.geo.nr_targets);	//9

	CH	= (pda >> ch_shift) & (nand_info.geo.nr_channels - 1);
	CE	= (pda >> ce_shift) & (nand_info.geo.nr_targets - 1);
	LUN = (pda >> lun_shift) & (nand_info.geo.nr_luns - 1);

    //STEP3. Search ECC table index by LAA. if search index = 0xFFFF, LAA is not existed in ECC table
    wECC_idx = wECC_Search_Idx(lda, 0, pECC_table->ecc_table_cnt - 1, ECC_BIN_SEARCH);

    //STEP4. Create and insert new entry
    if(wECC_idx == 0xFFFF)
    {
        wECC_idx = wECC_Search_Idx(lda, 0, pECC_table->ecc_table_cnt, ECC_IDX_SEARCH);

        if (wECC_idx != pECC_table->ecc_table_cnt)
        {
            for (wIdx = pECC_table->ecc_table_cnt; wIdx > wECC_idx; wIdx--)
            {
                pECC_table->ecc_entry[wIdx] = pECC_table->ecc_entry[wIdx-1];
            }
        }
        pECC_table->ecc_entry[wECC_idx].err_lda               = lda;
        pECC_table->ecc_entry[wECC_idx].bit_map               = 0;
        pECC_table->ecc_entry[wECC_idx].ErrInfor.die          = nand_info.geo.nr_channels * nand_info.geo.nr_targets * LUN + nand_info.geo.nr_channels * CE + CH;  //to_die_id(fda.ch, fda.ce, fda.lun);
        pECC_table->ecc_entry[wECC_idx].ErrInfor.pblk         = pda >> nand_info.pda_block_shift;  //(fda.blk * nand_info.geo.nr_planes) + fda.pl;
        pECC_table->ecc_entry[wECC_idx].ErrInfor.page         = ((pda >> nand_info.pda_page_shift) & (nand_info.pda_page_mask));  //fda.pg;
        pECC_table->ecc_entry[wECC_idx].ErrInfor.b.reg_source = reg_sour;
        pECC_table->ecc_entry[wECC_idx].ErrInfor.b.au         = pda & (DU_CNT_PER_PAGE - 1);  //fda.du & 0x7;      //au=du=4k per page
        pECC_table->ecc_table_cnt++;
        //_dECCTableCnt++;
    }

    ncl_cmd_trace(LOG_ERR, 0x1d46, "[ECC] RegPASS, LBA[0x%x] Len[0x%x]", lba, total_len);
    ncl_cmd_trace(LOG_ERR, 0xb9e1, "[ECC] Source[%d] DIE[%d] pBlk[0x%x] Page[%d] DU[%d]",
        pECC_table->ecc_entry[wECC_idx].ErrInfor.b.reg_source, pECC_table->ecc_entry[wECC_idx].ErrInfor.die, pECC_table->ecc_entry[wECC_idx].ErrInfor.pblk,
        pECC_table->ecc_entry[wECC_idx].ErrInfor.page, pECC_table->ecc_entry[wECC_idx].ErrInfor.b.au);

    //STEP5. Calculate Error Bitmap Offset
    bLBA_offset = lba & (numlba -  1); //(lba % (LDA_SIZE_SHIFT - HLBASZ));
    //bLBA_length = 0;

    //STEP6. Update Error Bitmap
    //while(bLBA_length++ < total_len)
    for(bLBA_length = 0; bLBA_length < total_len; bLBA_length++)
    {
        //pECC_table->ecc_entry[wECC_idx].bit_map |= BIT(bLBA_offset++);
        if(bLBA_offset == numlba)  // && (bLBA_length < total_len))
        {
            bLBA_offset = 0;
            //wECC_idx++;
            wECC_idx = 0;
            lba += bLBA_length;
            total_len -= bLBA_length;
            goto NEXT_LDA;
        }
        pECC_table->ecc_entry[wECC_idx].bit_map |= BIT(bLBA_offset++);
    }

    //STEP7. Error Protection for disorder ECC Entry
    if (pECC_table->ecc_table_cnt >= 2)
    {
        for (wIdx = 0; wIdx < pECC_table->ecc_table_cnt - 1; wIdx++)
        {
            if (pECC_table->ecc_entry[wIdx].err_lda > pECC_table->ecc_entry[wIdx + 1].err_lda)
            {
                ECC_Sorting_Table();
                break;
            }
        }
    }

    ecct_cnt = pECC_table->ecc_table_cnt; //update ecct cnt in sram

    //STEP8. Request Update EEPROM
    //epm_update(GLIST_sign,(CPU_ID-1));
    extern u8 epm_Glist_updating;
    epm_Glist_updating = 1;  //Avoid updating epm glist too many times.
    
    evt_set_cs(evt_update_epm, 0, 0, CS_TASK);

    //ncl_cmd_trace(LOG_ERR, 0, "[ECC] RegPASS, LBA[0x%x] Len[0x%x]", lba, total_len);
    //ncl_cmd_trace(LOG_ERR, 0, "[ECC] _Source[%d] DIE[%d] pBlk[0x%x] Page[%d] DU[%d]",
    //    pECC_table->ecc_entry[wECC_idx].ErrInfor.b.reg_source, pECC_table->ecc_entry[wECC_idx].ErrInfor.die, pECC_table->ecc_entry[wECC_idx].ErrInfor.pblk,
    //    pECC_table->ecc_entry[wECC_idx].ErrInfor.page, pECC_table->ecc_entry[wECC_idx].ErrInfor.b.au);

    return wECC_idx;

}

ddr_code void Register_ECCT_By_Raid_Recover(u64 lda, u16 bit_map, u8 reg_sour)
{
    u8 lba_idx = 0;
    u32 lba_len = 0;
    u64 lba;
	u8 num_lba=0;
	if(host_sec_bitz==9)
		num_lba=8;//joe  add sec size 20200818
	else
		num_lba=1;

    while(lba_idx < num_lba)
    {
        while((lba_idx < num_lba) && ((bit_map & BIT(lba_idx)) == 0))
        {
            lba_idx++;
        }

        lba = (lda * num_lba) + lba_idx;
        lba_len = 0;

        while((lba_idx < num_lba) && (bit_map & BIT(lba_idx)))
        {
            lba_idx++;
            lba_len++;
        }

        if(lba_len)
        {
            //wECC_Register_Table(lba, lba_len, reg_sour, pda);
            wECC_Register_Table(lba, lba_len, reg_sour);
        }
    }
}

ddr_code void __Register_ECCT_By_Raid_Recover(volatile cpu_msg_req_t *req)
{
    u64 LDA =  req->pl;
    u8 source = (u8)req->cmd.flags;
    Register_ECCT_By_Raid_Recover(LDA, ~(0x00), source);
}

ddr_code void __wECC_Record_Table(volatile cpu_msg_req_t *req)
{
    u64 LDA =  req->pl;
    u8 source = (u8)req->cmd.flags;
    wECC_Record_Table(LDA, ~(0x00), source);
}

ddr_code u16 wECC_UnRegister_Table(u64 lba, u32 total_len)
{
    u8 bUnRegOffsetStart, bUnRegOffsetEnd, bModify;
    u16 wEntryIdx, wPreEntryIdx, wIdx;
    u32 dLAAStart, dLAAEnd, dLAA;
	u8 numlba = 0;
	if(host_sec_bitz == 9)
		numlba = 8;
	else
		numlba = 1;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pECC_table = (stECC_table*)(&epm_glist_start->data[ECC_START_DATA_CNT]);


    // Return if no ECC Table
    if (pECC_table->ecc_table_cnt == 0)
    {
        return ECCERR_ECCTABLE_EMPTY;
    }
    // Error protection for incorrect paramters
    if (total_len == 0) //|| lba > CFG_TOTAL_SECTORS - 1)
    {
        ncl_cmd_trace(LOG_ERR, 0x5120, "UnRegECCTable Fail, Incorrect Parameters, LBA: 0x%x, Len: 0x%x", lba, total_len);
        return ECCERR_PARAM_OVERFLOW;
    }

    //while(!TestAndSetLockBit(EXT_BIT_12_MOD_ECC_TABLE));
    // Step 1. Convert LBA & Len to LAAStart & LAAEnd
    dLAAStart = lba >> (LDA_SIZE_SHIFT - host_sec_bitz);
    dLAAEnd = (lba + total_len - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz);

    // Record pre-entry index to speedup Search Process
    wPreEntryIdx = 0;
    bModify = false;
     bUnRegOffsetStart = 0xFF;
     bUnRegOffsetEnd = 0xFF;
    // Step 2. Unregister ECC Table
    #if 0
	u16 wBakBitmap;
	u8 bUnRegOffset;
    for (dLAA = dLAAStart; dLAA <= dLAAEnd; dLAA++)
    {
        bUnRegOffsetStart = 0;
        bUnRegOffsetEnd = numlba - 1;

        if (dLAA == dLAAStart)
        {
            bUnRegOffsetStart = lba & (numlba - 1);
        }

        if (dLAA == dLAAEnd)
        {
            bUnRegOffsetEnd = (lba + total_len - 1) & (numlba - 1);
        }

        wEntryIdx = wECC_Search_Idx(dLAA, wPreEntryIdx, pECC_table->ecc_table_cnt - 1, ECC_BIN_SEARCH);
        if (wEntryIdx == 0xFFFF) continue;

        wPreEntryIdx = wEntryIdx;

        bUnRegOffset = bUnRegOffsetStart;

        wBakBitmap = pECC_table->ecc_entry[wEntryIdx].bit_map;
        while (bUnRegOffset <= bUnRegOffsetEnd)
        {
            pECC_table->ecc_entry[wEntryIdx].bit_map &= ~ BIT(bUnRegOffset++);
        }

        if(wBakBitmap != pECC_table->ecc_entry[wEntryIdx].bit_map)
        {
            bModify = true;
        }

        if (pECC_table->ecc_entry[wEntryIdx].bit_map == 0)
        {
            pECC_table->ecc_table_cnt--;
            //_dECCTableCnt--;

            for (wIdx = wEntryIdx; wIdx < pECC_table->ecc_table_cnt; wIdx++)
            {
                pECC_table->ecc_entry[wIdx] = pECC_table->ecc_entry[wIdx+1];
            }

            pECC_table->ecc_entry[pECC_table->ecc_table_cnt].err_lda                = 0;
            pECC_table->ecc_entry[pECC_table->ecc_table_cnt].bit_map                = 0;
            pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.die           = 0; //INV_U8;
            pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.pblk          = 0; //INV_U16;
            pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.page          = 0; //INV_U16;
            pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.all           = 0; //INV_U8;
            //pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.b.au          = INV_U8;
            //pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.b.reg_source  = INV_U8;
        }
    }
    //while(!TestAndClearLockBit(EXT_BIT_12_MOD_ECC_TABLE));

    ecct_cnt = pECC_table->ecc_table_cnt; //update ecct cnt in sram
	#else
	if((pECC_table->ecc_entry[0].err_lda >  dLAAEnd)||(pECC_table->ecc_entry[pECC_table->ecc_table_cnt - 1].err_lda <  dLAAStart)){
		//ncl_cmd_trace(LOG_ERR, 0, "[ECC] UnRegECC not hit, LBA[0x%x] Len[0x%x]", lba, total_len);
		return ECCERR_PARAM_OVERFLOW;
	}
	wPreEntryIdx = INV_U16;
	wEntryIdx = 0;
	for(wIdx = 0;wIdx < pECC_table->ecc_table_cnt; wIdx++){
		dLAA = pECC_table->ecc_entry[wIdx].err_lda;
		if(dLAA < dLAAStart)
			continue;
		else if(dLAA > dLAAEnd)
			break;
		if(wPreEntryIdx == INV_U16){
			wPreEntryIdx = wIdx;
			bModify = true;
		}
		 if (dLAA == dLAAStart)
        {
            bUnRegOffsetStart = lba & (numlba - 1);
        }
        if (dLAA == dLAAEnd)
        {
            bUnRegOffsetEnd = (lba + total_len - 1) & (numlba - 1);
        }
		wEntryIdx++;
	}
	if(wEntryIdx){
		u16 bitmaps = 0;
		if((dLAAStart == dLAAEnd) && (bUnRegOffsetStart != 0xFF) && (bUnRegOffsetEnd != 0xFF)){
			bitmaps = ((1 << total_len) - 1) << bUnRegOffsetStart;
			pECC_table->ecc_entry[wPreEntryIdx].bit_map &= (~ bitmaps);
			if(pECC_table->ecc_entry[wPreEntryIdx].bit_map){
				wEntryIdx --;
			}
		}
		else{
			if(bUnRegOffsetStart != 0xFF){
				bitmaps = ((1 << (numlba -  bUnRegOffsetStart))- 1) << bUnRegOffsetStart;
				pECC_table->ecc_entry[wPreEntryIdx].bit_map &= (~ bitmaps);
				if(pECC_table->ecc_entry[wPreEntryIdx].bit_map){
					wEntryIdx --;
					wPreEntryIdx++;
				}
			}
			if(bUnRegOffsetEnd != 0xFF){
				bitmaps = (1 << (bUnRegOffsetEnd + 1)) - 1;
				pECC_table->ecc_entry[wPreEntryIdx +  wEntryIdx - 1].bit_map &= (~ bitmaps);
				if(pECC_table->ecc_entry[wPreEntryIdx +  wEntryIdx - 1].bit_map){
					wEntryIdx --;
				}
			}
		}
		if(wEntryIdx){
			sys_assert(pECC_table->ecc_table_cnt >= wEntryIdx);
			for(wIdx = wPreEntryIdx; wIdx < pECC_table->ecc_table_cnt; wIdx++){
				if((wIdx + wEntryIdx) < pECC_table->ecc_table_cnt){
					pECC_table->ecc_entry[wIdx] = pECC_table->ecc_entry[wIdx + wEntryIdx];
				}
				else{
					pECC_table->ecc_entry[wIdx].err_lda                = 0;
			        pECC_table->ecc_entry[wIdx].bit_map                = 0;
			        pECC_table->ecc_entry[wIdx].ErrInfor.die           = 0; //INV_U8;
			        pECC_table->ecc_entry[wIdx].ErrInfor.pblk          = 0; //INV_U16;
			        pECC_table->ecc_entry[wIdx].ErrInfor.page          = 0; //INV_U16;
			        pECC_table->ecc_entry[wIdx].ErrInfor.all           = 0; //INV_U8;
			        //pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.b.au          = INV_U8;
			        //pECC_table->ecc_entry[pECC_table->ecc_table_cnt].ErrInfor.b.reg_source  = INV_U8;
				}
			}
			pECC_table->ecc_table_cnt -=  wEntryIdx;
            ecct_cnt =pECC_table->ecc_table_cnt;
		}
	}
	#endif
    if(bModify)
    {
        ncl_cmd_trace(LOG_ERR, 0xfd7c, "[ECC] UnRegECC PASS, LBA[0x%x] Len[0x%x]", lba, total_len);
        //epm_update(GLIST_sign,(CPU_ID-1));
        extern u8 epm_Glist_updating;
        epm_Glist_updating = 1;  //Avoid updating epm glist too many times.
        
        evt_set_cs(evt_update_epm, 0, 0, CS_TASK);
    }

    return 0;
}


ddr_code void ECC_Table_Operation(stECCT_ipc_t *ecct_info)
{
    switch(ecct_info->type)
    {
        //lda_t lda;
        u64 lda;
        case VSC_ECC_reg:
            //wECC_Register_Table(ecct_info->lba, ecct_info->total_len, ECC_REG_VU, ecct_info->pda);
            wECC_Register_Table(ecct_info->lba, ecct_info->total_len, ecct_info->source);
            break;
        case VSC_ECC_unreg:
            wECC_UnRegister_Table(ecct_info->lba, ecct_info->total_len);
            break;
        case VSC_ECC_reset:
            ECC_Table_Reset();
            break;
        case VSC_ECC_rc_reg:
            if((ecct_info->source == ECC_REG_RECV) || (ecct_info->source == ECC_REG_WHCRC) || (ecct_info->source == ECC_REG_BLANK))
                lda = ecct_info->lba;
            else
                lda = (ecct_info->lba) >> (LDA_SIZE_SHIFT - host_sec_bitz);
            Register_ECCT_By_Raid_Recover(lda, ~(0x00), ecct_info->source);
            break;
        case VSC_ECC_dump_table:
            ECC_Dump_Table();
            break;
        default:
            ncl_cmd_trace(LOG_ERR, 0xb7a3, "[ECC] Operation not support");
            break;
    }

    if (vsc_ecct_data != NULL)
    {
        ncl_cmd_trace(LOG_ERR, 0x18fa, "vsc_ecct_data:0x%x", vsc_ecct_data);
        memset(vsc_ecct_data, 0, sizeof(stECCT_ipc_t));
        vsc_ecct_data = NULL;
    }
}

ddr_code void ipc_ECC_Table_Operation(volatile cpu_msg_req_t *req)
{
    stECCT_ipc_t* ecct_info = (stECCT_ipc_t*) req->pl;
    ECC_Table_Operation(ecct_info);
}

ddr_code void ipc_ECC_Build_Table(volatile cpu_msg_req_t *req)
{
    wECC_Build_Table();
}

#endif

slow_code void Reg_ECCT_Fill_Info(bm_pl_t *bm_pl, pda_t pda, u8 type, u8 source)
{
    u32 dtag_idx = 0;
    lda_t lda = 0;
    tEcctReg *ecct_info = NULL;
    u8 *ecct_ptr = NULL;

    if(type == GC_ECCT_TYPE)
    {
        dtag_idx = bm_pl->pl.du_ofst;
        dtag_idx = (DTAG_CNT_PER_GRP * bm_pl->pl.nvm_cmd_id) + dtag_idx;
        //dtag_idx = DTAG_GRP_IDX_TO_IDX(dtag_idx,  pl.pl.nvm_cmd_id);
        lda = gc_lda_list[dtag_idx];
        
        if (CPU_ID == CPU_BE)  //reg ecct
            Register_ECCT_By_Raid_Recover(lda, ~(0x00), source); 
        else
            cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_MARK, (u16)source, lda);                                       
    }
    else
    {
        if(CPU_ID == CPU_BE)
        {
            ecct_ptr = &ecct_pl_cnt2;
            ecct_info = &ecct_pl_info_cpu2[ecct_pl_cnt2];
        }
        else
        {
            ecct_ptr = &ecct_pl_cnt4;
            ecct_info = &ecct_pl_info_cpu4[ecct_pl_cnt4];
        }

        if(ecct_info->bm_pl.pl.dtag == 0)
        {        
            memcpy(&ecct_info->bm_pl, bm_pl, sizeof(bm_pl_t));
            ecct_info->bm_pl.pl.type_ctrl = 0;
        }
        else
        {
            ncl_cmd_trace(LOG_ERR, 0xfcd6, "[ECCT]ecct_pl info is busy");
            goto NEXT_PTR;
        }
        
        if(source == ECC_REG_WHCRC)
        {
            ecct_info->bm_pl.pl.type_ctrl = 0x3F; //for source use: 0x3F: ECC_REG_WHCRC, 0x38: ECC_REG_DFU, else: ECC_REG_RECV
        }
        else if(source == ECC_REG_DFU)
        {
            ecct_info->bm_pl.pl.type_ctrl = 0x38;
        }
        else if(source == ECC_REG_BLANK)
        {
            ecct_info->bm_pl.pl.type_ctrl = 0x3A; //for source use: 0x3A: ECC_REG_BLANK
        }

        ecct_info->type = type;  //host, gc or other     

        rc_reg_ecct(&ecct_info->bm_pl, ecct_info->type);

        NEXT_PTR:
        if(*ecct_ptr >= MAX_RC_REG_ECCT_CNT - 1)
            *ecct_ptr = 0;
        else
            (*ecct_ptr)++;

    }
    ncl_cmd_trace(LOG_ERR, 0x0ee4, "[ECCT] Fill Info, pda[0x%x] dtag[0x%x] type[%d]", pda, bm_pl->pl.dtag, type);
    return;
}

ddr_code void ecct_update_epm_event_triggr(u32 parm, u32 payload, u32 sts)
{
    //ncl_cmd_trace(LOG_INFO, 0, "[ECC] epm update event trigger");
    epm_update(GLIST_sign,(CPU_ID-1));
}

ddr_code void update_epm_init(void)
{
    //ncl_cmd_trace(LOG_INFO, 0, "[ECC] epm update event init");
    evt_register(ecct_update_epm_event_triggr, 0, &evt_update_epm);
}

#endif


