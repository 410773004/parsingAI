//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#define INSTANTIATE_ERRHANDLE
#include "ErrorHandle.h"
#undef  INSTANTIATE_ERRHANDLE
#include "ftlprecomp.h"
#include "defect_mgr.h"
#include "spb_mgr.h"
#include "gc.h"
#include "frb_log.h"
#include "console.h"
#include "ipc_api.h"    // for spb_weak_retire_t, Paul_20201202

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
#include "nvme_spec.h"
extern struct nvme_telemetry_host_initated_log *telemetry_host_data;
#endif
/*! \cond PRIVATE */
#define __FILEID__ errhandle
#include "trace.h"
/*! \endcond */

share_data epm_info_t*  shr_epm_info;
share_data_zi u8   FW_ARD_CPU_ID;

ddr_data bool ard_cs_flag = 0;  //FW ARD critical section flag
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
share_data_zi u8 bFail_Blk_Cnt;                               //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];
share_data_zi bool profail; // TBD_EH, Temp use for ASSERT Prog failed after mark defect.

#endif

#ifdef ERRHANDLE_GLIST

extern void ftl_spb_weak_retire(spb_weak_retire_t weak_retire);   // Paul_20201202
extern volatile u32 GrowPhyDefectCnt; //for SMART GrowDef Use

fast_data_zi volatile bool bypass_verify = false;

#if (Synology_case)
share_data_zi synology_smart_statistics_t *synology_smart_stat;
#endif

ddr_code bool EH_GLMarkBad(u16 wLBlk)
{
	u8 *defect_map = get_defect_map();
	u32 dTotalUsedCnt, dIdx, dIdxBot, dTargBit, dChkBit;
	u16 wTargblk;
    u8  bTargDie;
    u8  bTargPL;
	bool blFindBlk = false;

	if (pGList->dCycle)
    	dTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
	else
    	dTotalUsedCnt = pGList->wGL_Mark_Cnt;

    // Handle whole spb mark bad, should not break this for loop early.
 	for (dIdx = 0; dIdx < dTotalUsedCnt; dIdx++)
 	{
     	dIdxBot = (dTotalUsedCnt - 1) - dIdx;   // 0 ~ (dTotalUsedCnt - 1)
#if UNC_MARK_BAD_VERIFY
        if (bypass_verify && pGList->GlistEntry[dIdxBot].ReadVerify == 1 &&
            pGList->GlistEntry[dIdxBot].bError_Type != GL_PROG_FAIL &&
            pGList->GlistEntry[dIdxBot].bError_Type != GL_ERASE_FAIL) // bypass read verify scan case
            continue;
#endif				
		if ((pGList->GlistEntry[dIdxBot].wLBlk_Idx == wLBlk) && (pGList->GlistEntry[dIdxBot].Mark_Bad == false))
    	{
			blFindBlk = true;

        	wTargblk = pGList->GlistEntry[dIdxBot].wLBlk_Idx;
        	bTargDie = pGList->GlistEntry[dIdxBot].bDie;
        	bTargPL  = pGList->GlistEntry[dIdxBot].wPhyBlk & (shr_nand_info.geo.nr_planes - 1);

        	// Fill info for NAND analysis.
        	pGList->GlistEntry[dIdxBot].dLBlk_EC	= spb_info_get(wTargblk)->erase_cnt;
        	pGList->GlistEntry[dIdxBot].dLBlk_POH   = spb_info_get(wTargblk)->poh;    // May be a little different from 1st scene.
        	pGList->GlistEntry[dIdxBot].dLBlk_RdCnt	= spb_get_desc(wTargblk)->rd_cnt; // May be a little different from 1st scene.
        	pGList->GlistEntry[dIdxBot].dLBlk_SN	= spb_get_desc(wTargblk)->sn;
        	pGList->GlistEntry[dIdxBot].dLBlk_flags = spb_get_flag(wTargblk) << 16 | spb_get_sw_flag(wTargblk);	

            pGList->GlistEntry[dIdxBot].NeedGC      = false;
#if UNC_MARK_BAD_VERIFY
            if (pGList->GlistEntry[dIdxBot].bError_Type != GL_PROG_FAIL && pGList->GlistEntry[dIdxBot].bError_Type != GL_ERASE_FAIL &&
                pGList->GlistEntry[dIdxBot].ReadVerify == 0) // First time read verify
            {
                pGList->GlistEntry[dIdxBot].ReadVerify = 1;
                __dmb();
                ftl_apl_trace(LOG_INFO, 0x2ef2, "[EH] Read Verify, GL(LB/D/Pl)[%d/%d/%d]", wTargblk, bTargDie, bTargPL);
                continue;
            }
#endif                
            pGList->GlistEntry[dIdxBot].Mark_Bad    = true;
			__dmb();
#if (Synology_case)
            synology_smart_stat->total_later_bad_block_cnt++;
            ftl_apl_trace(LOG_ERR, 0xceb8, "[EH] total_later_bad_block_cnt[%d]", synology_smart_stat->total_later_bad_block_cnt);
#endif
			extern u32 df_width;
        	dTargBit = wTargblk * df_width + bTargDie * shr_nand_info.geo.nr_planes + bTargPL;
			dChkBit  = bitmap_check((u32*)&defect_map[0], dTargBit);

            ftl_apl_trace(LOG_ERR, 0xedf0, "[EH] mark bad, GL(Idx/LB/D/Pl)[%d/%d/%d/%d]", dIdxBot, wTargblk, bTargDie, bTargPL);

			if (!dChkBit)
        	{
            	bitmap_set((u32*)defect_map, dTargBit); // mark bad table

				// for dbg
				dChkBit = bitmap_check((u32*)&defect_map[0], dTargBit);
			}
        	else
        	{
       			// for dbg
            	ftl_apl_trace(LOG_ERR, 0x3fff, "[EH] NG, hit BMP[0x%x]", dChkBit);

				// TBD_EH, workaround for FTL blocks, Paul_20201116
				#ifndef	EH_MARK_BAD_MANUALLY
				if (!pGList->GlistEntry[dIdxBot].RD_Ftl)
					sys_assert(0);
				#endif
        	}
    	}
 	}

	#ifdef SKIP_MODE		
	if (blFindBlk)		// FET, MarkBadManully
		spb_check_defect_cnt(wLBlk);
    	// TBD_EH, check SPB cnt meet user capacity or not
    #endif	

	return blFindBlk;
}

ddr_code void ErrHandle_Task(MK_FailBlk_Info failBlkInfo)	// ISU, TSSP, PushGCRelsFalInfo
{
    u16 wLBlk = 0;
    bool blFindBlk = false;

    wLBlk = failBlkInfo.b.wBlkIdx;
	ftl_apl_trace(LOG_ERR, 0xa57e, "[EH] task, tot[%d] Hdl_spb[%d]", bFail_Blk_Cnt, wLBlk);

    // TBC_EH
	// 1. SYS blocks
	// 2. FTL blocks
	// 3. Retired blocks    

    if (failBlkInfo.b.need_gc == false)
	{
		// FET, ReduceEHCode
		blFindBlk = EH_GLMarkBad(wLBlk);		

	 	if (!blFindBlk)
		{
		    ftl_apl_trace(LOG_ERR, 0xacd2, "[EH] NG? GL not found");

			//SPB_DESC_F_WEAK may be set by patrol read due to over limit.
		    //sys_assert(0);   //not find in glist
		}
	 	else
	 	{
			// 1. Save GList and defect BMP after whole GList scanned and updated.
			//  Consider case: mark part of LBlk bad and power off.
			// 2. Save GList should be completed before defect BMP due to mark GList again is permitted but mark defect BMP again doesn't.
			//  Consider case: mark defect BMP and power off, power on may scan not handled GList and access defect block again.

            extern volatile u8 plp_trigger;
            if (plp_trigger != 0xEE)
            {
                epm_update(GLIST_sign, (CPU_ID-1));
				#ifndef EHVRY_INJ_BY_ENTRY_DIE_FAIL		// FET, EHPerfImpact
                frb_log_type_update(FRB_TYPE_DEFECT);	// VRY, for rebase on new FW base
				#endif
            }

            //#if skip_mode				
			//spb_check_defect_cnt(wLBlk);	// FET, MarkBadManully
            // TBD_EH, check SPB cnt meet user capacity or not
            //#endif	
		}

		// Clear record in FailBlkInfo array.
        //cpu_msg_issue(CPU_BE - 1, CPU_MSG_DEL_FAILBLK, 0, (u32)wLBlk);	// Handled for power on scan GList, ISU, TSSP, PushGCRelsFalInfo // ISU, BakupPBTLat
        cpu_msg_issue(CPU_BE - 1, CPU_MSG_DEL_FAILBLK, 0, failBlkInfo.all);

	 	ftl_apl_trace(LOG_ERR, 0xd85c, "[EH] hdled, LB[%d]", wLBlk);
	}
    else
    {
        // Need not handle GCing due to ErrHandle_Task will be waked up only when it was needed.              
        u32 nsid = spb_get_nsid(wLBlk);
		ftl_apl_trace(LOG_ERR, 0xbdbd, "[EH] trig cls/ gc, spb[%d] nsid[%d]", wLBlk, nsid);

		// Trigger close + gc/ gc in set_spb_weak_retire()
    	spb_weak_retire_t weak_retire;
        weak_retire.b.spb_id = wLBlk;
        weak_retire.b.type = SPB_WEAK_HNDL_THRSHLD;

        if (weak_retire.b.spb_id == ps_open[FTL_CORE_GC])			// Wait for GC suspend done and then do closing GC open.
        {
            if((gc_suspend_done_flag) || (gc_busy() == false)) 		// ISU, GCIdlePgFal
            {
                ftl_apl_trace(LOG_ERR, 0xc93c, "[EH] GC OpBlk, gc_suspend_done[%d]", gc_suspend_done_flag);
                ftl_spb_weak_retire(weak_retire);  
                gc_suspend_done_flag = false;
            }
        }
        else
        {  
        	// Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
        	if (wLBlk != ps_open[FTL_CORE_NRM] && wLBlk != ps_open[FTL_CORE_GC])	
        	{	
        		// Release FailBlkInfo when push to GC, ISU, TSSP, PushGCRelsFalInfo	
				cpu_msg_issue(CPU_BE - 1, CPU_MSG_DEL_FAILBLK, 0, failBlkInfo.all);	
			}

            ftl_spb_weak_retire(weak_retire);       
        }
		
		ftl_apl_trace(LOG_ERR, 0xc220, "[EH] task, chked FalBlkInfo.spb[%d].(Cd/Gi/NdGC)[%d/%d/%d] gc_suspend_done[%d]", \
    		failBlkInfo.b.wBlkIdx, failBlkInfo.b.bClsdone, failBlkInfo.b.bGCing, failBlkInfo.b.need_gc, gc_suspend_done_flag);
    }		
}

ddr_code void __ErrHandle_Task(volatile cpu_msg_req_t *req)
{
    MK_FailBlk_Info failBlkInfo;	// ISU, TSSP, PushGCRelsFalInfo
	failBlkInfo.all = req->pl;
    ErrHandle_Task(failBlkInfo);
}

ddr_code void fw_ard_handler(u32 fwArdParams) //CPU_MSG_FW_ARD_HANDLER, handle FW ARD queue
{  
    u8 CpuIdAndChId = fwArdParams & 0x00FF;  //cpu_id and ch_id
    u8 op = (fwArdParams & 0xFF00) >> 8;     //Operation code

    extern ard_que_t fw_ard_que;

    if(op == 1)  //Insert cpu_id and ch_id to the FW ARD queue
    {
        bool ret;
        CBF_INS(&fw_ard_que, ret, CpuIdAndChId);
        sys_assert(ret == true);
        //ftl_apl_trace(LOG_ALW, 0, "fw_ard_handler, Insert CpuIdAndChId = 0x%x", CpuIdAndChId);  //dbg log
    }
    else if(op == 2)  //Remove head that have completed fw ard, make ard_cs_flag to 0, end CS section.
    {   
        CBF_REMOVE_HEAD(&fw_ard_que);
        ard_cs_flag = 0;
        //ftl_apl_trace(LOG_ALW, 0, "fw_ard_handler, fw ard done");  //dbg log
    }
    else
    {
		ftl_apl_trace(LOG_ALW, 0x761e, "wrong op");
        sys_assert(0);  //wrong op
    }

    if(ard_cs_flag == 0 && (!CBF_EMPTY(&fw_ard_que)))  //Go to do fw ard
    {
        ard_cs_flag = 1;

        CBF_HEAD(&fw_ard_que, CpuIdAndChId);  //get cpu_id and ch_id from FW ARD  queue.
        u8 cpu_id = (CpuIdAndChId & 0x00F0) >> 4;
        u8 ch_id = CpuIdAndChId & 0x000F;
        FW_ARD_CPU_ID = cpu_id;

        if(cpu_id == CPU_BE)  //issue cpu2
        {
            //ftl_apl_trace(LOG_ALW, 0, "fw_ard_handler,CPU3 call CPU2, ch_id = %d",ch_id);  //dbg log
            cpu_msg_issue(CPU_BE - 1, CPU_MSG_READ_RETRY_HANDLING, 0, (u32)(ch_id));
        }
        else if(cpu_id == CPU_BE_LITE) //issue cpu4
        {
            //ftl_apl_trace(LOG_ALW, 0, "fw_ard_handler,CPU3 call CPU4, ch_id = %d",ch_id);  //dbg log
            cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_READ_RETRY_HANDLING, 0, (u32)(ch_id));
        }
        else
        {
            ftl_apl_trace(LOG_ALW, 0x8012, "wrong cpu_id");
            sys_assert(0);  //wrong cpu_id
        }
    }
    else if(ard_cs_flag == 1 && (!CBF_EMPTY(&fw_ard_que)))
    {
        ftl_apl_trace(LOG_ALW, 0xa9e4, "fw_ard_handler, fw ard busy");  //dbg log
    }
    else
    {
        ftl_apl_trace(LOG_ALW, 0x0e5f, "fw_ard_handler, fw ard all done");  //dbg log
    }

}

ddr_code void __fw_ard_handler(volatile cpu_msg_req_t *req) //CPU_MSG_FW_ARD_HANDLER
{
    fw_ard_handler(req->pl);
}

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
ddr_code void get_telemetry_ErrHandleVar_CPU_FTL() //CPU_MSG_FW_ARD_HANDLER, handle FW ARD queue
{  
    telemetry_host_data->errorhandle.tel_ard_cs_flag = (u8)ard_cs_flag;
    telemetry_host_data->errorhandle.tel_bFail_Blk_Cnt = bFail_Blk_Cnt;

    cpu_msg_issue(CPU_FE - 1, CPU_MSG_GET_TELEMETRY_DATA_DONE, 0, 0);
}

ddr_code void __get_telemetry_ErrHandleVar_CPU_FTL(volatile cpu_msg_req_t *req) //CPU_MSG_FW_ARD_HANDLER
{
    get_telemetry_ErrHandleVar_CPU_FTL();
}
#endif

extern bool _fg_warm_boot;
// Move to CPU 3 due to CPU 2 core size issue.
ddr_code void eh_dump_glist(void)
{
    u16 wIdx;
    u16 wStar_Idx;
    //u16 wErrStat;
    u16 wDumpIdx;
    u16 wTotalDumpIdx;

    //wErrStat = GLERR_NO_ERROR;

    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);

    ftl_apl_trace(LOG_ERR, 0x63d8, "[GL] (Tag/Ver/InvVer/ChkSum)[0x%x/%x/%x/%x]-(Cnt/Cyc)[%d/%d]", \
        pGList->dGL_Tag, pGList->bGL_Ver, pGList->bGL_VerInvert, pGList->dGL_CheckSum, pGList->wGL_Mark_Cnt, pGList->dCycle);
    ftl_apl_trace(LOG_ERR, 0x8f34, "[GL] (CrossTempCnt)(SPBCnt)[%d]", pGList->dGL_CrossTempCnt);

	if (pGList->dCycle)
	{
		wStar_Idx = pGList->wGL_Mark_Cnt;
		wTotalDumpIdx = (GL_TOTAL_ENTRY_CNT + pGList->wGL_Mark_Cnt);
	}
	else
	{
		wStar_Idx = 0;
		wTotalDumpIdx = pGList->wGL_Mark_Cnt;
	}

    for (wIdx = wStar_Idx; wIdx < wTotalDumpIdx; wIdx++)
    {
        if(wIdx >= GL_TOTAL_ENTRY_CNT)
            wDumpIdx = wIdx - GL_TOTAL_ENTRY_CNT;
        else
            wDumpIdx = wIdx;

#if (OTF_TIME_REDUCE == ENABLE)
	if(_fg_warm_boot == false)
#endif		
	{            
        ftl_apl_trace(LOG_ERR, 0x3111, "[%d](Err/NdGC/MkBd/RcvFl/2BFl)[%d/%d/%d/%d/%d]", 
		 	wDumpIdx, pGList->GlistEntry[wDumpIdx].bError_Type, pGList->GlistEntry[wDumpIdx].NeedGC, 
		 	pGList->GlistEntry[wDumpIdx].Mark_Bad, pGList->GlistEntry[wDumpIdx].RD_RecvFail, pGList->GlistEntry[wDumpIdx].RD_2BFail
		 	);
		ftl_apl_trace(LOG_ERR, 0x7db8, "---- (LB/D/PB/Pg/Au)[%d/%d/%d/%d/%d]", 
		 	pGList->GlistEntry[wDumpIdx].wLBlk_Idx, pGList->GlistEntry[wDumpIdx].bDie, 
		 	pGList->GlistEntry[wDumpIdx].wPhyBlk, pGList->GlistEntry[wDumpIdx].wPage, pGList->GlistEntry[wDumpIdx].AU		 	
		 	);	
		ftl_apl_trace(LOG_ERR, 0xb6b6, "---- (Open/Blank/DFU/FTL/unAli/gcOp)[%d/%d/%d/%d/%d/%d]", 
		 	pGList->GlistEntry[wDumpIdx].Open_blk, pGList->GlistEntry[wDumpIdx].Blank, pGList->GlistEntry[wDumpIdx].DFU, 
		 	pGList->GlistEntry[wDumpIdx].RD_Ftl, pGList->GlistEntry[wDumpIdx].RD_uaFlag, pGList->GlistEntry[wDumpIdx].gcOpen
		 	);		
        ftl_apl_trace(LOG_ERR, 0xd178, "---- (SN/POH/EC/RC/Flg-SWFlg/Tem)[%d/%d/%d/%d/0x%x/%d]", 
		 	pGList->GlistEntry[wDumpIdx].dLBlk_SN, pGList->GlistEntry[wDumpIdx].dLBlk_POH, 
		 	pGList->GlistEntry[wDumpIdx].dLBlk_EC, pGList->GlistEntry[wDumpIdx].dLBlk_RdCnt, pGList->GlistEntry[wDumpIdx].dLBlk_flags, 
		 	pGList->GlistEntry[wDumpIdx].bTemper);
        ftl_apl_trace(LOG_ERR, 0x7bc1, "---- (ReadVerify)[%d]", pGList->GlistEntry[wDumpIdx].ReadVerify);
    }
	}
    //return wErrStat;
}

ddr_code void ipc_eh_dump_glist(volatile cpu_msg_req_t *req)	// FET, DumpGLLog
{
    eh_dump_glist();
}

ddr_code void eh_scan_glist_trig_handle(tEHBuildGLCond tCond)
{        
    u16 wTotalUsedCnt;
    u16 wIdx;
	bool blSaveEPMAtLast = false;	// FET, PfmtHdlGL

    MK_FailBlk_Info Fail_Blk_Info_Temp;         //CPU2 use this global variable so define a local variable for cpu3
    // Step 1. Point to GList table addr.
    //epm_glist_t *epm_glist_start = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    //pGList = (sGLTable *)(&epm_glist_start->data[0]);

	// Dump GList only when power on, ISU, TSSP, PushGCRelsFalInfo
	//if (!FTL_INIT_DONE)
	//    eh_dump_glist();

    // Step 2. Valid GList, scan GList and trigger ErrHandle_Task if there is any un-handled (mark bad) entry.
    // TBD_EH
	// 1. SMART infor
	// 2. Non-handled entries > MAX_FAIL_BLK_CNT
	if (pGList->dGL_Tag == GLIST_TAG)
    {
		// 1. Used by Fail_Blk_Info to handle array full case -- EH_BUILD_GL_TRIG_EHTASK
        // 2. Handle after preformat -- EH_BUILD_GL_USER_ERASED

        if (tCond > EH_BUILD_GL_FTL_NOT_RDY)
        {        
            if (pGList->dCycle)
            {    
                wTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
            }    
            else
            {    
                wTotalUsedCnt = pGList->wGL_Mark_Cnt;
            }    

            bypass_verify = true;
            for (wIdx = 0; wIdx < wTotalUsedCnt; wIdx++)
            {
                // Handle w/ mark GList sequence.        
                if (pGList->GlistEntry[wIdx].Mark_Bad == false)
                {   
#if UNC_MARK_BAD_VERIFY
                    if (pGList->GlistEntry[wIdx].ReadVerify == 1) // skip first time read verify case
                        continue;
#endif                    
                	// FET, PfmtHdlGL
					switch (tCond)
					{
					case EH_BUILD_GL_USER_ERASED:
						{
						blSaveEPMAtLast = true;
						EH_GLMarkBad(pGList->GlistEntry[wIdx].wLBlk_Idx);
						}	
						break;		
					case EH_BUILD_GL_TRIG_EHTASK:
						{
						u32 nsid;
	                    spb_set_flag( pGList->GlistEntry[wIdx].wLBlk_Idx, SPB_DESC_F_NO_NEED_CLOSE);
	                	nsid = spb_get_nsid(pGList->GlistEntry[wIdx].wLBlk_Idx);

	                    // Trigger ErrHandle_Task to handle it.    
	                	Fail_Blk_Info_Temp.all       = 0;   
	                	Fail_Blk_Info_Temp.b.wBlkIdx = pGList->GlistEntry[wIdx].wLBlk_Idx;
	                	//Fail_Blk_Info_Temp.b.need_gc = (nsid == 0) ? false : pGList->GlistEntry[wIdx].NeedGC;	// ISU, FTLSpbNotNedGC
	                	Fail_Blk_Info_Temp.b.need_gc = ((nsid == 0) || (nsid == 2)) ? false : pGList->GlistEntry[wIdx].NeedGC;
						if (Fail_Blk_Info_Temp.b.need_gc)	// Avoid SPB_DESC_F_GCED abort error handle, ISU, TSSP, PushGCRelsFalInfo
							spb_clear_flag(pGList->GlistEntry[wIdx].wLBlk_Idx, SPB_DESC_F_GCED);

	                    //cpu_msg_issue(CPU_BE - 1, CPU_MSG_SET_FAILBLK, 0, Fail_Blk_Info_Temp.all);	// ISU, TSSP, PushGCRelsFalInfo
						ErrHandle_Task(Fail_Blk_Info_Temp);
						}
						break;
					default:
						break;
					}	
                }    
            }  

            bypass_verify = false;
			// FET, PfmtHdlGL
			if (tCond == EH_BUILD_GL_USER_ERASED)
			{
				// Reset by CPU3 after preformat.
				//memset(Fail_Blk_Info, INV_U8, sizeof(Fail_Blk_Info));
    			//bFail_Blk_Cnt 	= 0;
				//fgFail_Blk_Full = false;	
				cpu_msg_issue(CPU_BE - 1, CPU_MSG_INIT_EH_MGR, 0, 0);

				extern volatile u8 plp_trigger;
                if (plp_trigger != 0xEE)
                {
                	if (blSaveEPMAtLast)
                	{
                    	epm_update(GLIST_sign, (CPU_ID-1));
						frb_log_type_update(FRB_TYPE_DEFECT);	// FET, MarkBadManully
		            }
                }	
			}
        }
    }
}
ddr_code void __eh_scan_glist_trig_handle(volatile cpu_msg_req_t *req)
{
	// FET, PfmtHdlGL
	u32 build_GL_cond = req->pl;

    //eh_scan_glist_trig_handle(EH_BUILD_GL_TRIG_EHTASK);
    eh_scan_glist_trig_handle(build_GL_cond);
}
ddr_code void eh_clear_glist(void)
{
    //u32 *pGlistDtag = (u32*)ddtag2mem(shr_epm_info->epm_glist.ddtag); // GL_mod, Paul_20201130
	//memset(pGlistDtag, 0, (GLIST_EPM_NEED_DTAG * DTAG_SZE));

	epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	pGList = (sGLTable*)(&epm_glist_start->data[0]);
	memset(pGList, 0, (GLIST_EPM_NEED_DTAG * DTAG_SZE));

	pGList->dGL_Tag = GLIST_TAG;
	pGList->bGL_Ver = GLIST_VER;
	pGList->bGL_VerInvert = ~(pGList->bGL_Ver);

	epm_update(GLIST_sign, (CPU_ID-1));

    return;
}

ddr_code void eh_dump_gl_defect_map()
{
	u32 dTargBit;
	u32 dChkBit;
    epm_glist_t* epm_glist_start = (epm_glist_t*)ddtag2mem(shr_epm_info->epm_glist.ddtag);
    pGList = (sGLTable*)(&epm_glist_start->data[0]);
	u8 *defect_map = get_defect_map();
	u16 GLTotalCnt = 0;
	u16 idx;

	if (pGList->dCycle)
	{
		GLTotalCnt = GL_TOTAL_ENTRY_CNT;
	}
	else
	{
		GLTotalCnt = pGList->wGL_Mark_Cnt;
	}

	for (idx = 0; idx < GLTotalCnt; idx++)
	{
		extern u32 df_width;
		dTargBit	= (pGList->GlistEntry[idx].wLBlk_Idx * df_width) + (pGList->GlistEntry[idx].bDie * shr_nand_info.geo.nr_planes) + (pGList->GlistEntry[idx].wPhyBlk & (shr_nand_info.geo.nr_planes - 1));
		dChkBit = bitmap_check((u32*)&defect_map[0], dTargBit);
		ftl_apl_trace(LOG_ERR, 0x7031, "dChkBit:%x", dChkBit);

		#ifdef EHVRY_REL_GL
		bitmap_reset((u32*)&defect_map[0], dTargBit);
		dChkBit = bitmap_check((u32*)&defect_map[0], dTargBit);
		ftl_apl_trace(LOG_ERR, 0x1e83, "Aft, dChkBit:0x%x", dChkBit);
		#endif
	}

	#ifdef EHVRY_REL_GL
	frb_log_type_update(FRB_TYPE_DEFECT);
	#endif

	// For verify, release P1, ISU, SPBErFalHdl
	#if 0	// ISU, TSSP, PushGCRelsFalInfo
	{
		u32 spb_cnt = shr_nand_info.geo.nr_blocks;
		u32 interleave = shr_nand_info.interleave;
		u32 table_size;
		u32 df_width;

		df_width = occupied_by(interleave, 8) << 3;
		table_size = spb_cnt * df_width / 8;

		memset(defect_map, 0, table_size);	
	}
	#endif

	return;
}

ddr_code void eh_dump_fail_blk_info(void)
{
    u8 bIdx = 0;

	ftl_apl_trace(LOG_ALW, 0x2445, "[EH] fail blk info cnt [%d]", bFail_Blk_Cnt);

    for (bIdx = 0; bIdx < MAX_FAIL_BLK_CNT; bIdx++)	// DBG, PgFalVry
    {        
        ftl_apl_trace(LOG_ALW, 0x5700, "[%d] (LB/Tp)[%d/%d]-(ClsDone/NeedGc/Gcing)[%d/%d/%d]", \
            bIdx, Fail_Blk_Info[bIdx].b.wBlkIdx, Fail_Blk_Info[bIdx].b.bType, \
            Fail_Blk_Info[bIdx].b.bClsdone, Fail_Blk_Info[bIdx].b.need_gc, Fail_Blk_Info[bIdx].b.bGCing);  
    }
}

static ddr_code int GList_main(int argc, char *argv[])
{
	u8 mode = strtol(argv[1], (void *)0, 0);

	ftl_apl_trace(LOG_ALW, 0xc6ec, "M[0x%x]", mode);

	eh_dump_glist();

	switch (mode)
	{
	    #if 1
	    case 0xB:   // Register GList and mark bad manully, Paul_20201207
	    {    
	        sGLEntry *errInfo = &errInfo4;

	        errInfo->RD_Ftl     = true; // Let it trigger mark bad directly.
	        errInfo->bError_Type= GL_MANUAL_ERROR;
	        errInfo->wPhyBlk    = strtol(argv[2], (void *)0, 0);
            errInfo->bDie       = strtol(argv[3], (void *)0, 0);
            errInfo->wLBlk_Idx  = errInfo->wPhyBlk / 2;	     

	        ftl_apl_trace(LOG_INFO, 0xf998, "[EH] GLReg ipc"); 
		    cpu_msg_issue(CPU_BE - 1, CPU_MSG_REG_GLIST, 0, (u32)errInfo);
	    } 
	        break;
	    #endif

	    case 0xC:
	        eh_clear_glist();
	        eh_dump_glist();
	        break;

	    case 0xE:
            eh_dump_fail_blk_info();
            break;

	    case 0xF:
	        eh_dump_gl_defect_map();	
			#ifdef EHVRY_REL_GL
			eh_clear_glist();
			#endif
	        break;             

	    default:
	        break;
	}

	ftl_apl_trace(LOG_ERR, 0x4c3a, "GL Main Ed\n");

	return 0;
}

static DEFINE_UART_CMD(dump_glist, "glist", "dmp + mod=0xC: clr/ 0xF: defMap",
		"GL/ EH op", 0, 3, GList_main);

ddr_code void ipc_eh_clear_glist(volatile cpu_msg_req_t *req)   // Paul_20210105
{
    eh_clear_glist();
}

#endif

static ddr_code int manual_markbad(int argc, char *argv[])
{
	u32 blk = strtol(argv[1], (void *)0, 0);
	u8 plane = strtol(argv[2], (void *)0, 0);

	u8 *defect_map = get_defect_map();
	u32 dTargBit, dChkBit;
	u16 wTargblk = blk;
    u8  bTargDie = 0;
    u8  bTargPL = plane;

	extern u32 df_width;
	dTargBit = wTargblk * df_width + bTargDie * shr_nand_info.geo.nr_planes + bTargPL;
	dChkBit  = bitmap_check((u32*)&defect_map[0], dTargBit);

    ftl_apl_trace(LOG_INFO, 0x67f2, "[EH] mark bad, GL(LB/D/Pl)[%d/%d/%d/%d]", wTargblk, bTargDie, bTargPL);

	if (!dChkBit)
	{	
    	bitmap_set((u32*)defect_map, dTargBit); // mark bad table
		ftl_apl_trace(LOG_INFO, 0xd281, "mark bad successful");

		u32 index = (wTargblk * df_width) >> 3;
		u8* ftl_df_ptr = (defect_map + index);
		u32 interleave = bTargDie * shr_nand_info.geo.nr_planes + bTargPL;
		u32 idx = interleave >> 3;
		u32 off = interleave & (7);
		ftl_df_ptr[idx] |= 1 << off;

		// for dbg
		dChkBit = bitmap_check((u32*)&defect_map[0], dTargBit);

		ftl_apl_trace(LOG_INFO, 0xf986, "[EH] mark bad plane:%d result1:%d result2:%d", interleave, dChkBit, (ftl_df_ptr[idx] & (1 << off)));
	}
	else
	{			// for dbg
    	ftl_apl_trace(LOG_INFO, 0xdca5, "already mark bad");
	}

	u8* spb_defect_map = (gl_pt_defect_tbl + ((wTargblk * df_width) >> 3));
	u8 i;
	for(i=0;i<(shr_nand_info.interleave/8);i++)
		ftl_apl_trace(LOG_INFO, 0x0647, "spb_%d[%d]:%d", wTargblk, i, spb_defect_map[i]);

	epm_update(GLIST_sign, (CPU_ID-1));
	frb_log_type_update(FRB_TYPE_DEFECT);	// VRY, for rebase on new FW base

	return 0;
}

static DEFINE_UART_CMD(manual_markbad, "markbad", "Enter blk id and plane id",
		"blk", 2, 2, manual_markbad);

