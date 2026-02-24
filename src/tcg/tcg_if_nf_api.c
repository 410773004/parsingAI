/*
//-----------------------------------------------------------------------------
//       Copyright(c) 2019-2020 Solid State Storage Technology Corporation.
//                         All Rights reserved.
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from SSSTC.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from SSSTC.
//-----------------------------------------------------------------------------
*/
#ifdef TCG_NAND_BACKUP // Jack Li

#include "sect.h"
#include "bf_mgr.h"
#include "ncl.h"
#include "dtag.h"
#include "eccu.h"
#include "tcg_nf_mid.h"
#include "tcgcommon.h"
#include "tcgtbl.h"
#include "tcg_if_nf_api.h"
#include "tcg.h"
#include "tcg_sh_vars.h"
#include "epm.h"
#include "console.h"
#include "evlog.h"

#define __FILEID__ tcgapi
#include "trace.h"

extern volatile u8 host_sec_bitz;
extern epm_info_t* shr_epm_info;
extern tcg_mid_info_t* tcg_mid_info;
extern volatile u8 plp_trigger;

fast_data_zi volatile tcg_nf_params_t tcg_nf_params;

//Error Handling when Read fail causes TCG table error
ddr_code void tcg_tbl_err_handling(void)
{
	bTcgTblErr = true;

	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	epm_aes_data->tcg_err_flag &= BIT2;

	epm_update(AES_sign, (CPU_ID - 1));
	
	extern void tcg_if_onetime_init(bool bootup, bool buf_to_dram);
	tcg_if_onetime_init(false, false);
}

//Read default table from NAND first (to vars G1)
ddr_code u8 tcg_nf_G1RdDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	if(bTcgTblErr)
		return true;

	tG1 *tmpBuf_tbl = (tG1 *)tcgTmpBuf;
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0;
	tcg_nf_params.laacnt = 1;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0x5b58, "[TCG] Read G1 def in G4 fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xe4ea, "[TCG] G4  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
		tcg_api_trace(LOG_INFO, 0x1c8c, "[TCG] G4 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0xf470, "[TCG] G4 End: 0x%x", tmpBuf_tbl->b.mEndTag);
		
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 1;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	
		if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
			|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
		{
			tcg_api_trace(LOG_INFO, 0x7c2e, "[TCG] Read G1 def in G5 fail !! result: %d", tcg_nf_params.result);
			tcg_api_trace(LOG_INFO, 0xd245, "[TCG] G5  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
			tcg_api_trace(LOG_INFO, 0x6947, "[TCG] G5 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
			tcg_api_trace(LOG_INFO, 0xcdf7, "[TCG] G5 End: 0x%x", tmpBuf_tbl->b.mEndTag);

			//bTcgTblErr = true;
			tcg_tbl_err_handling();
			
			return true;
		}
	}
	
	memcpy32((void *)pG1, (const void *)tcgTmpBuf, sizeof(tG1));
	
	return false;
}

//Read default table from NAND first (to vars G2)
ddr_code u8 tcg_nf_G2RdDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	if(bTcgTblErr)
		return true;

	tG2 *tmpBuf_tbl = (tG2 *)tcgTmpBuf;
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G2_DEFAULT_LAA0;
	tcg_nf_params.laacnt = 2;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0x206e, "[TCG] Read G2 def in G4 fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xe776, "[TCG] G4  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
		tcg_api_trace(LOG_INFO, 0x5e4e, "[TCG] G4 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x55c3, "[TCG] G4 End: 0x%x", tmpBuf_tbl->b.mEndTag);
		
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = TCG_G2_DEFAULT_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 2;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	
		if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
			|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
		{
			tcg_api_trace(LOG_INFO, 0xa5ab, "[TCG] Read G2 def in G5 fail !! result: %d", tcg_nf_params.result);
			tcg_api_trace(LOG_INFO, 0xe3c9, "[TCG] G5  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
			tcg_api_trace(LOG_INFO, 0x1d6b, "[TCG] G5 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
			tcg_api_trace(LOG_INFO, 0x148c, "[TCG] G5 End: 0x%x", tmpBuf_tbl->b.mEndTag);
			
			//bTcgTblErr = true;
			tcg_tbl_err_handling();
			
			return true;
		}
	}
	
	memcpy32((void *)pG2 , (const void *)tcgTmpBuf, sizeof(tG2));
	
	return false;
}

//Read default table from NAND first (to vars G3)
ddr_code u8 tcg_nf_G3RdDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	if(bTcgTblErr)
		return true;

	tG3 *tmpBuf_tbl = (tG3 *)tcgTmpBuf;
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G3_DEFAULT_LAA0;
	tcg_nf_params.laacnt = 2;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0xace7, "[TCG] Read G3 def in G4 fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xa2de, "[TCG] G4  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
		tcg_api_trace(LOG_INFO, 0xe4a6, "[TCG] G4 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x0c2a, "[TCG] G4 End: 0x%x", tmpBuf_tbl->b.mEndTag);
		
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = TCG_G3_DEFAULT_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 2;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	
		if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
			|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
		{
			tcg_api_trace(LOG_INFO, 0xc818, "[TCG] Read G3 def in G5 fail !! result: %d", tcg_nf_params.result);
			tcg_api_trace(LOG_INFO, 0xf98c, "[TCG] G5  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
			tcg_api_trace(LOG_INFO, 0x7873, "[TCG] G5 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
			tcg_api_trace(LOG_INFO, 0xc2e1, "[TCG] G5 End: 0x%x", tmpBuf_tbl->b.mEndTag);
			
			//bTcgTblErr = true;
			tcg_tbl_err_handling();
			
			return true;
		}
	}

	memcpy32((void *)pG3, (const void *)tcgTmpBuf, sizeof(tG3));
	
	return false;
}

//Read default table from NAND first (to vars G1, G2, and G3)
ddr_code u8 tcg_nf_G4RdDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	if(bTcgTblErr)
		return true;

	tG1 *tmpBuf_tbl_G1 = (tG1 *)tcgTmpBuf;
	tG2 *tmpBuf_tbl_G2 = (tG2 *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE));
	tG3 *tmpBuf_tbl_G3 = (tG3 *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE));
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0;
	tcg_nf_params.laacnt = 5;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl_G1->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl_G1->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G1->b.mEndTag != TCG_END_TAG)
		|| (tmpBuf_tbl_G2->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl_G2->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G2->b.mEndTag != TCG_END_TAG)
		|| (tmpBuf_tbl_G3->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl_G3->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G3->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0x1fb8, "[TCG] Read G4 def fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xe160, "[TCG] G1 ID: 0x%x, ver. 0x%x, End: 0x%x", tmpBuf_tbl_G1->b.mTcgTblInfo.ID, tmpBuf_tbl_G1->b.mTcgTblInfo.ver, tmpBuf_tbl_G1->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0xd0ab, "[TCG] G2 ID: 0x%x, ver. 0x%x, End: 0x%x", tmpBuf_tbl_G2->b.mTcgTblInfo.ID, tmpBuf_tbl_G2->b.mTcgTblInfo.ver, tmpBuf_tbl_G2->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x08c9, "[TCG] G3 ID: 0x%x, ver. 0x%x, End: 0x%x", tmpBuf_tbl_G3->b.mTcgTblInfo.ID, tmpBuf_tbl_G3->b.mTcgTblInfo.ver, tmpBuf_tbl_G3->b.mEndTag);
		
		if(tcg_nf_G5RdDefault())
		{
			//bTcgTblErr = true;
			tcg_tbl_err_handling();
			
			return true;
		}
	}

	memcpy32((void *)pG1, (const void *)tcgTmpBuf                                                                                                                     , sizeof(tG1));
	memcpy32((void *)pG2, (const void *)tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE)                                                           , sizeof(tG2));
	memcpy32((void *)pG3, (const void *)tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE), sizeof(tG3));

	return false;
}

ddr_code u8 tcg_nf_G5RdDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	if(bTcgTblErr)
		return true;

	tG1 *tmpBuf_tbl_G1 = (tG1 *)tcgTmpBuf;
	tG2 *tmpBuf_tbl_G2 = (tG2 *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE));
	tG3 *tmpBuf_tbl_G3 = (tG3 *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE));
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0 + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = 5;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl_G1->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl_G1->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G1->b.mEndTag != TCG_END_TAG)
		|| (tmpBuf_tbl_G2->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl_G2->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G2->b.mEndTag != TCG_END_TAG)
		|| (tmpBuf_tbl_G3->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl_G3->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G3->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0x41cb, "[TCG] Read G5 def fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xf668, "[TCG] G1 ID: 0x%x, ver. 0x%x, End: 0x%x", tmpBuf_tbl_G1->b.mTcgTblInfo.ID, tmpBuf_tbl_G1->b.mTcgTblInfo.ver, tmpBuf_tbl_G1->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x5066, "[TCG] G2 ID: 0x%x, ver. 0x%x, End: 0x%x", tmpBuf_tbl_G2->b.mTcgTblInfo.ID, tmpBuf_tbl_G2->b.mTcgTblInfo.ver, tmpBuf_tbl_G2->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x4157, "[TCG] G3 ID: 0x%x, ver. 0x%x, End: 0x%x", tmpBuf_tbl_G3->b.mTcgTblInfo.ID, tmpBuf_tbl_G3->b.mTcgTblInfo.ver, tmpBuf_tbl_G3->b.mEndTag);
		
		return true;
	}
	
	return false;
}

// Read def tbl from NAND & write to corresponding LAA
ddr_code u8 tcg_nf_G4WrDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;
	
	// need sycn G1~G3 and G1d~G3d in G4
	if(bTcgTblErr)
	{
		memcpy(tcgTmpBuf                                                                                                                     , (const void *)pG1, sizeof(tG1));
		memcpy(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE)                                                           , (const void *)pG2, sizeof(tG2));
		memcpy(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE), (const void *)pG3, sizeof(tG3));

		tG1 *pt1 = (tG1 *)tcgTmpBuf;
		tG2 *pt2 = (tG2 *)(tcgTmpBuf+NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE));
		tG3 *pt3 = (tG3 *)(tcgTmpBuf+NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE)+NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE));
		tcg_api_trace(LOG_INFO, 0x0639, "[TCG] for write default G4 table check");
		tcg_api_trace(LOG_INFO, 0xfc19, "[TCG] Buf G1 ver.0x%x", pt1->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x6092, "[TCG] Buf G1 end.0x%x", pt1->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x3d56, "[TCG] Buf G2 ver.0x%x", pt2->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0xced2, "[TCG] Buf G2 end.0x%x", pt2->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x5f21, "[TCG] Buf G3 ver.0x%x", pt3->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x2a35, "[TCG] Buf G3 end.0x%x", pt3->b.mEndTag);

		tcg_api_trace(LOG_INFO, 0xd68b, "[TCG] G1 ver.0x%x", pG1->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x77af, "[TCG] G1 end.0x%x", pG1->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x2d67, "[TCG] G2 ver.0x%x", pG2->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x294a, "[TCG] G2 end.0x%x", pG2->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x2df2, "[TCG] G3 ver.0x%x", pG3->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0xeb72, "[TCG] G3 end.0x%x", pG3->b.mEndTag);

		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_WRITE;
		tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0;
		tcg_nf_params.laacnt = 5;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}
	else
	{
		//tcg_nf_G4RdDefault();
	}

	tG1 *pt1 = (tG1 *)tcgTmpBuf;
	tG2 *pt2 = (tG2 *)(tcgTmpBuf+NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE));
	tG3 *pt3 = (tG3 *)(tcgTmpBuf+NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE)+NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE));
	tcg_api_trace(LOG_INFO, 0x3a46, "[TCG] for write normal G4 table check");
	tcg_api_trace(LOG_INFO, 0x508c, "[TCG] Buf G1 ver.0x%x", pt1->b.mTcgTblInfo.ver);
	tcg_api_trace(LOG_INFO, 0x3849, "[TCG] Buf G1 end.0x%x", pt1->b.mEndTag);
	tcg_api_trace(LOG_INFO, 0x6083, "[TCG] Buf G2 ver.0x%x", pt2->b.mTcgTblInfo.ver);
	tcg_api_trace(LOG_INFO, 0x71af, "[TCG] Buf G2 end.0x%x", pt2->b.mEndTag);
	tcg_api_trace(LOG_INFO, 0xe2c4, "[TCG] Buf G3 ver.0x%x", pt3->b.mTcgTblInfo.ver);
	tcg_api_trace(LOG_INFO, 0x5a42, "[TCG] Buf G3 end.0x%x", pt3->b.mEndTag);

	tcg_api_trace(LOG_INFO, 0xe384, "[TCG] G1 ver.0x%x", pG1->b.mTcgTblInfo.ver);
	tcg_api_trace(LOG_INFO, 0x782e, "[TCG] G1 end.0x%x", pG1->b.mEndTag);
	tcg_api_trace(LOG_INFO, 0x126c, "[TCG] G2 ver.0x%x", pG2->b.mTcgTblInfo.ver);
	tcg_api_trace(LOG_INFO, 0xa11e, "[TCG] G2 end.0x%x", pG2->b.mEndTag);
	tcg_api_trace(LOG_INFO, 0x6dea, "[TCG] G3 ver.0x%x", pG3->b.mTcgTblInfo.ver);
	tcg_api_trace(LOG_INFO, 0x0f53, "[TCG] G3 end.0x%x", pG3->b.mEndTag);

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = TCG_G1_LAA0;
	tcg_nf_params.laacnt = 5;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	tcg_nf_G5WrDefault();
	
	bTcgTblErr = false;
	
	return false;
}

ddr_code u8 tcg_nf_G5WrDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(bTcgTblErr)
	{
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_WRITE;
		tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 5;
		
		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = TCG_G1_LAA0 + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = 5;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);	
	
	return false;
}


ddr_code u8 tcg_nf_G1Rd(bool G4_only, bool G5_only) // both True is inhibittied
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	tG1 *tmpBuf_tbl = (tG1 *)tcgTmpBuf;

	if(G5_only)
		goto Read_G5_for_G1;
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G1_LAA0;
	tcg_nf_params.laacnt = 1;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0x3dd7, "[TCG] Read G1 in G4 fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xec65, "[TCG] G1 in G4  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
		tcg_api_trace(LOG_INFO, 0xf634, "[TCG] G1 in G4 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x9a0a, "[TCG] G1 in G4 End: 0x%x", tmpBuf_tbl->b.mEndTag);

		if(G4_only)
		{
			tcg_tbl_err_handling();
			return true;
		}
		
Read_G5_for_G1:
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = TCG_G1_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 1;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);

		if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
			|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
		{
			tcg_api_trace(LOG_INFO, 0x570b, "[TCG] Read G1 in G5 fail !! result: %d", tcg_nf_params.result);
			tcg_api_trace(LOG_INFO, 0x47cb, "[TCG] G1 in G5  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
			tcg_api_trace(LOG_INFO, 0xfab6, "[TCG] G1 in G5 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
			tcg_api_trace(LOG_INFO, 0x930b, "[TCG] G1 in G5 End: 0x%x", tmpBuf_tbl->b.mEndTag);
			
			tcg_tbl_err_handling();
			return true;
		}
	}

	memcpy32((void *)pG1, (const void *)tcgTmpBuf, sizeof(tG1));
	
	return false;
}

ddr_code u8 tcg_nf_G2Rd(bool G4_only, bool G5_only) // both True is inhibittied
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	tG2 *tmpBuf_tbl = (tG2 *)tcgTmpBuf;

	if(G5_only)
		goto Read_G5_for_G2;
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G2_LAA0;
	tcg_nf_params.laacnt = 2;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0xe4ef, "[TCG] Read G2 in G4 fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0xaf29, "[TCG] G2 in G4  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
		tcg_api_trace(LOG_INFO, 0x9b6d, "[TCG] G2 in G4 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0xbb37, "[TCG] G2 in G4 End: 0x%x", tmpBuf_tbl->b.mEndTag);

		if(G4_only)
		{
			tcg_tbl_err_handling();
			return true;
		}
		
Read_G5_for_G2:
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = TCG_G2_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 2;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);

		if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
			|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
		{
			tcg_api_trace(LOG_INFO, 0xcb18, "[TCG] Read G2 in G5 fail !! result: %d", tcg_nf_params.result);
			tcg_api_trace(LOG_INFO, 0xdf33, "[TCG] G2 in G5  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
			tcg_api_trace(LOG_INFO, 0xcf66, "[TCG] G2 in G5 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
			tcg_api_trace(LOG_INFO, 0x2ae2, "[TCG] G2 in G5 End: 0x%x", tmpBuf_tbl->b.mEndTag);
			
			tcg_tbl_err_handling();
			return true;
		}
	}

	memcpy32((void *)pG2, (const void *)tcgTmpBuf, sizeof(tG2));

	return false;
}

ddr_code u8 tcg_nf_G3Rd(bool G4_only, bool G5_only) // both True is inhibittied
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;

	tG3 *tmpBuf_tbl = (tG3 *)tcgTmpBuf;

	if(G5_only)
		goto Read_G5_for_G3;
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = TCG_G3_LAA0;
	tcg_nf_params.laacnt = 2;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
		|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
	{
		tcg_api_trace(LOG_INFO, 0x950a, "[TCG] Read G3 in G4 fail !! result: %d", tcg_nf_params.result);
		tcg_api_trace(LOG_INFO, 0x0984, "[TCG] G3 in G4  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
		tcg_api_trace(LOG_INFO, 0x766e, "[TCG] G3 in G4 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x1a44, "[TCG] G3 in G4 End: 0x%x", tmpBuf_tbl->b.mEndTag);

		if(G4_only)
		{
			tcg_tbl_err_handling();
			return true;
		}
		
Read_G5_for_G3:
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = TCG_G3_LAA0 + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = 2;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);

		if((tcg_nf_params.result) || (tmpBuf_tbl->b.mTcgTblInfo.ID != TCG_TBL_ID)
			|| (tmpBuf_tbl->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) || (tmpBuf_tbl->b.mEndTag != TCG_END_TAG))
		{
			tcg_api_trace(LOG_INFO, 0x7f3f, "[TCG] Read G3 in G5 fail !! result: %d", tcg_nf_params.result);
			tcg_api_trace(LOG_INFO, 0x35fb, "[TCG] G3 in G5  ID: 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ID);
			tcg_api_trace(LOG_INFO, 0x7af0, "[TCG] G3 in G5 ver. 0x%x", tmpBuf_tbl->b.mTcgTblInfo.ver);
			tcg_api_trace(LOG_INFO, 0x1345, "[TCG] G3 in G5 End: 0x%x", tmpBuf_tbl->b.mEndTag);
			
			tcg_tbl_err_handling();
			return true;
		}
	}
	
	memcpy32((void *)pG3, (const void *)tcgTmpBuf, sizeof(tG3));

	return false;
}

ddr_code u8 tcg_nf_G1Wr(bool G5_only)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;
	memcpy32(tcgTmpBuf, (const void *)pG1, sizeof(tG1));

	if(!G5_only)
	{
		// G4 //
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_WRITE;
		tcg_nf_params.laas   = TCG_G1_LAA0;
		tcg_nf_params.laacnt = 1;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}

	// G5 //
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = TCG_G1_LAA0 + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = 1;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);
	
	return false;
}

ddr_code u8 tcg_nf_G2Wr(bool G5_only)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;
	memcpy32(tcgTmpBuf, (const void *)pG2, sizeof(tG2));

	if(!G5_only)
	{
		// G4 //
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_WRITE;
		tcg_nf_params.laas   = TCG_G2_LAA0;
		tcg_nf_params.laacnt = 2;
	
		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}

	// G5 //
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = TCG_G2_LAA0 + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = 2;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);
	
	return false;
}

ddr_code u8 tcg_nf_G3Wr(bool G5_only)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;
	memcpy32(tcgTmpBuf, (const void *)pG3, sizeof(tG3));

	if(!G5_only)
	{
		// G4 //
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_WRITE;
		tcg_nf_params.laas   = TCG_G3_LAA0;
		tcg_nf_params.laacnt = 2;
	
		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}

	// G5 //
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = TCG_G3_LAA0 + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = 2;
	
	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	extern bool bKeyChanged;
	if(bKeyChanged)
		flush_to_nand(EVT_CHANGE_KEY);

	return false;
}

ddr_code u8 tcg_nf_SMBRRd(u64 slba, u16 du_cnt, bool from_io)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;
	u16 start_laa = slba >> (DTAG_SHF + DU_CNT_PER_PAGE_SHIFT - host_sec_bitz);
	u64 mask = (1 << (DTAG_SHF - host_sec_bitz)) - 1;
	u16 end_laa = ((slba & (~mask)) + (du_cnt << (DTAG_SHF - host_sec_bitz)) - 1) >> (DTAG_SHF + DU_CNT_PER_PAGE_SHIFT - host_sec_bitz);

	tcg_api_trace(LOG_INFO, 0x8eaf, "[TCG] tcg_nf_SMBRRd() : start_laa|0x%x - end_laa|0x%x", start_laa, end_laa);

	if(tcgTmpBuf==NULL)
		return true;

	u16 slaa_first, slaa_second;
	if(from_io)
	{
		slaa_first  = TCG_SMBR_LAA_START + start_laa;
		slaa_second = TCG_SMBR_LAA_START + TCG_LAST_USE_LAA + start_laa;
	}
	else
	{
		slaa_first  = TCG_SMBR_LAA_START + TCG_LAST_USE_LAA + start_laa;
		slaa_second = TCG_SMBR_LAA_START + start_laa;
	}
	
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = slaa_first;
	tcg_nf_params.laacnt = end_laa - start_laa + 1;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) && (mSessionManager.TransactionState == TRNSCTN_IDLE))
	{
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = slaa_second;
		tcg_nf_params.laacnt = end_laa - start_laa + 1;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}
	
	if(tcg_nf_params.result)
	{
		tcg_tbl_err_handling();
		return true;
	}
	
	return false;
}

ddr_code u8 tcg_nf_SMBRWr(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;
	
	void *ptcg_ipc;

	if(tcgTmpBuf==NULL)
		return true;

	if(mSessionManager.TransactionState == TRNSCTN_ACTIVE)
	{
		for(u16 pgsn = 0; pgsn < laacnt; pgsn++)
			tcg_mid_info->trsac_bitmap[(laas+pgsn)/32] |= (1 << ((laas+pgsn)%32));
	}
	
	// G5 //
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = laas + TCG_SMBR_LAA_START + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = laacnt;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);
	
	return false;
}

ddr_code u8 tcg_nf_SMBRCommit(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = 0xFF;
	tcg_nf_params.laas   = laas + TCG_SMBR_LAA_START;
	tcg_nf_params.laacnt = laacnt;

	tcg_nf_params.trnsctn_sts = mSessionManager.TransactionState;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if(tcg_nf_params.result)
	{
		tcg_tbl_err_handling();
		return true;
	}
	
	return false;
}

ddr_code u8 tcg_nf_SMBRAbort(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = 0xFE;
	tcg_nf_params.laas   = laas + TCG_SMBR_LAA_START;
	tcg_nf_params.laacnt = laacnt;

	tcg_nf_params.trnsctn_sts = mSessionManager.TransactionState;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if(tcg_nf_params.result)
	{
		tcg_tbl_err_handling();
		return true;
	}
	
	return false;
}

ddr_code u8 tcg_nf_DSRd(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	if(tcgTmpBuf==NULL)
		return true;

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_READ;
	tcg_nf_params.laas   = laas + TCG_DS_LAA_START + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = laacnt;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if((tcg_nf_params.result) && (mSessionManager.TransactionState == TRNSCTN_IDLE))
	{
		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_READ;
		tcg_nf_params.laas   = laas + TCG_LAST_USE_LAA;
		tcg_nf_params.laacnt = laacnt;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}
	
	if(tcg_nf_params.result)
	{
		tcg_tbl_err_handling();
		return true;
	}
	
	return false;
}

ddr_code u8 tcg_nf_DSWr(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	if(tcgTmpBuf==NULL)
		return true;

	// G5 //
	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = NCL_CMD_OP_WRITE;
	tcg_nf_params.laas   = laas + TCG_DS_LAA_START + TCG_LAST_USE_LAA;
	tcg_nf_params.laacnt = laacnt;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);
	
	return false;
}

ddr_code u8 tcg_nf_DSCommit(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = 0xFF;
	tcg_nf_params.laas   = laas + TCG_DS_LAA_START;
	tcg_nf_params.laacnt = laacnt;

	tcg_nf_params.trnsctn_sts = TRNSCTN_NAN;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if(tcg_nf_params.result)
	{
		tcg_tbl_err_handling();
		return true;
	}
	
	return false;
}

ddr_code u8 tcg_nf_DSAbort(u16 laas, u16 laacnt)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = true;
	tcg_nf_params.op     = 0xFE;
	tcg_nf_params.laas   = laas + TCG_DS_LAA_START;
	tcg_nf_params.laacnt = laacnt;

	tcg_nf_params.trnsctn_sts = TRNSCTN_NAN;

	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	if(tcg_nf_params.result)
	{
		tcg_tbl_err_handling();
		return true;
	}
	
	return false;
}

ddr_code u8 tcg_nf_allErase(u8 sync)
{
	if(plp_trigger)
		return true;

	void *ptcg_ipc;

	tcg_nf_params.result = 0;
	tcg_nf_params.sync   = sync;
	tcg_nf_params.op     = NCL_CMD_OP_ERASE;
	
	ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
	tcg_nf_Start(ptcg_ipc);

	bTcgTblErr = true;
	
	return false;
}

static ddr_code int dump_tcg_tbl_info_main(int argc, char *argv[])
{
	tcg_mid_trace(LOG_ALW, 0x34a1, "[TCG] G1 ID: 0x%x, ver: 0x%x, end: 0x%x", pG1->b.mTcgTblInfo.ID, pG1->b.mTcgTblInfo.ver, pG1->b.mEndTag);
	tcg_mid_trace(LOG_ALW, 0xc1da, "[TCG] G2 ID: 0x%x, ver: 0x%x, end: 0x%x", pG2->b.mTcgTblInfo.ID, pG2->b.mTcgTblInfo.ver, pG2->b.mEndTag);
	tcg_mid_trace(LOG_ALW, 0x354b, "[TCG] G3 ID: 0x%x, ver: 0x%x, end: 0x%x", pG3->b.mTcgTblInfo.ID, pG3->b.mTcgTblInfo.ver, pG3->b.mEndTag);
	
	tcg_mid_trace(LOG_ALW, 0x8ab1, "[TCG] sts: %d, RdLck: 0x%x, WrLck: 0x%x", mTcgStatus, mReadLockedStatus, mWriteLockedStatus);
	
	return 0;
}

static DEFINE_UART_CMD(dump_tcg_tbl_info, "dump_tcg_tbl_info", "dump_tcg_tbl_info", "dump_tcg_tbl_info", 0, 0, dump_tcg_tbl_info_main);

/*
static int  tcg_nf_G1RdDefault(req_t*);     // 0x00
static int  tcg_nf_G2RdDefault(req_t*);     // 0x01
static int  tcg_nf_G3RdDefault(req_t*);     // 0x02
static int  tcg_nf_G4RdDefault(req_t*);     // 0x03
static int  tcg_nf_G5RdDefault(req_t*);     // 0x0x04
static int  tcg_nf_G4WrDefault(req_t*);     // 0x05
static int  tcg_nf_G5WrDefault(req_t*);     // 0x06
static int  tcg_nf_G4BuildDefect(req_t*);   // 0x07
static int  tcg_nf_G5BuildDefect(req_t*);   // 0x08
static int  tcg_nf_G1Rd(req_t*);            // 0x09
static int  tcg_nf_G1Wr(req_t*);            // 0x0A
static int  tcg_nf_G2Rd(req_t*);            // 0x0B
static int  tcg_nf_G2Wr(req_t*);            // 0x0C
static int  tcg_nf_G3Rd(req_t*);            // 0x0D
static int  tcg_nf_G3Wr(req_t*);            // 0x0E
static int  tcg_nf_G4DmyRd(req_t*);         // 0x0F
static int  tcg_nf_G4DmyWr(req_t*);         // 0x10
static int  tcg_nf_G5DmyRd(req_t*);         // 0x11
static int  tcg_nf_G5DmyWr(req_t*);         // 0x12
static int  tcg_nf_SMBRRd(req_t*);          // 0x13
static int  tcg_nf_SMBRWr(req_t*);          // 0x14
static int  tcg_nf_SMBRCommit(req_t*);      // 0x15
static int  tcg_nf_SMBRClear(req_t*);       // 0x16
static int  tcg_nf_TSMBRClear(req_t*);      // 0x17
static int  tcg_nf_DSRd(req_t*);            // 0x18
static int  tcg_nf_DSWr(req_t*);            // 0x19
static int  tcg_nf_DSCommit(req_t*);        // 0x1A
static int  tcg_nf_DSClear(req_t*);         // 0x1B
static int  tcg_nf_TDSClear(req_t*);        // 0x1C
*/

#endif // Jack Li
