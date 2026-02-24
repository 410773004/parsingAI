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
#if (_TCG_) // Jack Li

//-----------------------------------------------------------------------------
//  Codes
//-----------------------------------------------------------------------------



#else // Jack Li
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#define ___TCG_NF_HAL
#include <string.h>
#include "sect.h"
#include "ipc.h"
#include "customer.h"
#include "FeaturesDef.h"
#include "nvme_spec.h"
#include "nvmet.h"
#include "MemAlloc.h"
#include "SharedVars.h"
#include "ErrorCodes.h"
#include "Monitor.h"
#include "SysInfo.h"
#include "btn.h"
#include "misc.h"
#include "fio.h"
#include "eccu.h"
#include "tcgcommon.h"
#include "tcgtbl.h"
#include "tcg.h"
#include "tcg_sh_vars.h"
#include "tcg_nf_mid.h"
#include "tcg_nf_hal.h"

//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------
extern tcgNfHalParams_t     tcgNfHalParams;
extern tFIO_JOB             smFioJobBuf[MAX_FIO_JOB];

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------
int TCG_Rd1Pg(tTcgLogAddr readpnt, tTcgGrpDef grp);
int TCG_Wr1Pg(tTcgLogAddr writepnt, tTcgGrpDef grp, U32 Aux0, U32 Aux1);
int G4_RelpaceDefectBlock(U8 DftBlk);
int G5_RelpaceDefectBlock(U8 DftBlk);
int TCG_ErrHandling(tTcgGrpDef grp);
int TCG_ReadRetry(tTcgGrpDef grp, U16* errNativeBlkCnt);

// tPAA CnvAdrLog2Phy(tTcgLogAddr logadr, tTcgGrpDef grp);
U16 Tcg_G4FreeBlk(U16 *Vac);
U16 Tcg_G5FreeBlk(U16 *Vac);
void CnvAdrLog2Phy(tTcgLogAddr logadr, tTcgGrpDef grp, tPAA *mypaa);

void FillFragInPage(tPAA *pPaa);
void FillPaa(U16 ipage, tTcgLogAddr writepnt, tTcgGrpDef grp);
void FillAux(U16 ipage, U32 aux0, U32 aux1);

// static void TCG_ERR_HANDLE_ReadRetryTrkDone(U32 trkId, PVOID pStatus);

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
//-------- high speed variable -----------


//-------- low speed variable ------------
tcg_data ALIGNED(4) tTcgLogAddr TcgG4Pnt, TcgG5Pnt;
tcg_data ALIGNED(4) tTcgLogAddr TcgG4CurPnt, TcgG4NxtPnt;
tcg_data ALIGNED(4) tTcgLogAddr TcgG5CurPnt, TcgG5NxtPnt;
tcg_data ALIGNED(4) U32 TcgG4CurHistNo, TcgG4NxtHistNo;
tcg_data ALIGNED(4) U32 TcgG5CurHistNo, TcgG5NxtHistNo;
tcg_data ALIGNED(4) U8  TcgG4Defects,TcgG5Defects;

tcg_data U16 ValidBlks[32];
tcg_data U16 ValidCnt;
tcg_data U16 G4_FreeBlk = TCG_MBR_CELLS;
tcg_data U16 G5_FreeBlk = TCG_MBR_CELLS;

tcg_data U16 TcgMbrCellVac[TCG_MBR_CELLS];               // 256
tcg_data U32 TcgMbrCellValidMap[TCG_MBR_CELLS][PAGE_NUM_PER_BLOCK / 32];       // 4K
tcg_data U16 TcgTempMbrCellVac[TCG_MBR_CELLS];           // 256
tcg_data U32 TcgTempMbrCellValidMap[TCG_MBR_CELLS][PAGE_NUM_PER_BLOCK / 32];   // 4K

tcg_data ALIGNED(4) tPAA gTcgPaa[MAX_DEVICES_NUM_PER_ROW * PAA_NUM_PER_PAGE];  // 4 paa per page
tcg_data ALIGNED(4) tTcgAux gTcgAux[MAX_DEVICES_NUM_PER_ROW * PAA_NUM_PER_PAGE];  // 4 paa per page

tcg_data ALIGNED(4) tTcgRdRetry gTcgRdRetry;
tcg_data ALIGNED(4) tTcgLogAddr gNativePaa[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */];

tcg_data U8 *OnePgSzBuf = NULL;              // 16K
tcg_data ALIGNED(4) tTcgLogAddr WrPntBox[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */];

tcg_data ALIGNED(4) tMSG_HOST*  pHcmdMsg_cpy;
tcg_data ALIGNED(4) tFIO_JOB*   pTcgJob = NULL;
tcg_data U8* pRdLaunchBuf_bak;

#ifdef _ALEX_VER_OK
static tSEQ_APP_CALLBACK orgTcgErrTrkDoneCallback;
#endif
//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------
extern U32        FirstTx_ssdStartIdx_Record_flag;

extern tPAA       *DR_G4PaaBuf;
extern tPAA       *DR_G5PaaBuf;

//-----------------------------------------------------------------------------
//  Imported function proto-type without header include
//-----------------------------------------------------------------------------
extern void CACHE_Alloc_TCG(tFIO_JOB* pJob, void* pBuffer);
extern void Core_JobCompleteWait(U8 subIdx);
extern void Core_JobIssue(tFIO_JOB* pJob);
extern void Core_UpdateCoreDebugTable(U16 Blk, U8 ErrType, U16 sn);

//-----------------------------------------------------------------------------
//  Codes
//-----------------------------------------------------------------------------
///----------------------------------------
/// Read Launched
///----------------------------------------
tcg_code int tcg_hal_read_launched(U16 pages, U8 *pBuffer)
{
    U16 i;
    U16 pagecnts = pages;
    U16 paacnts;

    U16 DesTrackCnts    =  ((pagecnts * PAA_NUM_PER_PAGE) + (MAX_DESC_PER_TRK - 1)) / MAX_DESC_PER_TRK;

    //TCGPRN("%s() pages|%x DesTrackCnts|%x\n", __FUNCTION__, pages, DesTrackCnts);
    // TCG_DBG_P(2, 3, 0x820400, 2, DesTrackCnts);  //82 04 00, "[F]tcg_hal_read_launched, DesTrackCnts = [%04X]", 2
    Core_JobCompleteWait(0xC0);   // wait previous all job down.
    // pBuffer = (U8*)(CPU3_BTCM_SYS_BASE + 0x8000);
    pRdLaunchBuf_bak = pBuffer;
    for (i = 0; i < DesTrackCnts; i++)
    {
        paacnts = (pagecnts * PAA_NUM_PER_PAGE) > MAX_DESC_PER_TRK ? MAX_DESC_PER_TRK : (pagecnts * PAA_NUM_PER_PAGE);

        pTcgJob              = FIO_PopJob(FIO_JOB_POOL_RW_FREE, 0xd0);
        //pTcgJob->dtagList    = CACHE_WO_DTAG;
        pTcgJob->NCLcmd      = JOB_NCL_CMD_READ;
        pTcgJob->type        = CACHE_TCG;
        pTcgJob->flag        = FIO_F_TCG | TCG_FIO_F_MUTE | FIO_F_SLC;
        pTcgJob->nandcmdflag = FIO_F_READ_META | FIO_F_ONEBIT_RETRY_ONLY;
        pTcgJob->duFmt       = DU_FMT_USER_4K;
        // dtagCnt = 0 mean is it doesn't return the cache.
        pTcgJob->dtagCnt     = 0;
        // fill paacnt
        pTcgJob->paaCnt      = paacnts;
        // arrange Cache
        //TCGPRN("job_id|%x paacnts|%x\n", pTcgJob->job_id, paacnts);

        CACHE_Alloc_TCG(pTcgJob, (void*)(pBuffer + PAGE_NUM_PER_DESTRACK * CFG_UDATA_PER_PAGE * i));
        // fill PAA
        memcpy(pTcgJob->paa, &gTcgPaa[MAX_DESC_PER_TRK * i], sizeof(tPAA) * paacnts);

        #if 0  // dump PAA info
        {
            int x, j;
            //TCGPRN(">>>> Dump PAA :\n");
            DBG_P(0x01, 0x03, 0x730000 );  // >>>> Dump PAA :
            for(x = 0; x < paacnts; x+=4){
                //TCGPRN("%x => %x %x %x %x\n", x, gTcgPaa[x].all32, gTcgPaa[x+1].all32, gTcgPaa[x+2].all32, gTcgPaa[x+3].all32);
                DBG_P(0x6, 0x03, 0x730001, 4, x, 4, gTcgPaa[x].all32, 4, gTcgPaa[x+1].all32, 4, gTcgPaa[x+2].all32, 4, gTcgPaa[x+3].all32);  // %x => %x %x %x %x
            }
            for(j = 0; j < paacnts; j++){
                //TCGPRN("PAA :ch[%2x] ce[%2x] lun[%2x] blk[%4x] pln[%2x] frg[%2x] spg[%4x] pg[%4x]\n", gTcgPaa[j].b.ch, gTcgPaa[j].b.ce, gTcgPaa[j].b.lun, gTcgPaa[j].b.block, gTcgPaa[j].b.plane, gTcgPaa[j].b.frag, gTcgPaa[j].b.subpage, gTcgPaa[j].b.WLpage);
                DBG_P(0x9, 0x03, 0x730002, 1, gTcgPaa[j].b.ch, 1, gTcgPaa[j].b.ce, 1, gTcgPaa[j].b.lun, 2, gTcgPaa[j].b.block, 1, gTcgPaa[j].b.plane, 1, gTcgPaa[j].b.frag, 2, gTcgPaa[j].b.subpage, 2, gTcgPaa[j].b.WLpage);  // PAA :ch[%2x] ce[%2x] lun[%2x] blk[%4x] pln[%2x] frg[%2x] spg[%4x] pg[%4x]
            }
        }
        #endif

        Core_JobIssue(pTcgJob);
        Core_JobCompleteWait(0xC1);
        // get Aux
        memcpy(&gTcgAux[MAX_DESC_PER_TRK * i], pTcgJob->duMeta, sizeof(tTcgAux) * paacnts);

        if (pTcgJob->status & (FIO_S_UNC_ERR | FIO_S_BLANK_ERR | FIO_S_UXP_ERR)){
            if(TCG_FIO_F_MUTE != FIO_F_MUTE){
                TCG_ERR_PRN("tcg_hal_read_launched() fail, st|%x\n", pTcgJob->status);
                DBG_P(0x2, 0x03, 0x7F7F31, 4, pTcgJob->status);  // tcg_hal_read_launched() fail, st|%x
                // DBG_P(2, 3, 0x82040F, 2, pTcgJob->status); //82 04 0F, "!!!Error!!! ------ tcg_hal_read_launched Error status[%X] ------", 2
            }
            return zNG;
        }

        if(pagecnts > PAGE_NUM_PER_DESTRACK) pagecnts -= PAGE_NUM_PER_DESTRACK;

    }
    //TCG_PRINTF("zOK-%X---------------------------\n\n", pTcgJob->status);
    //TCGPRN("RD Job OK!\n");
    return zOK;
}


///----------------------------------------
/// Write Launched
///----------------------------------------
tcg_code int tcg_hal_write_launched(U16 pages, U8 *pBuffer)
{
    U16 i;
    U16 pagecnts = pages;
    U16 paacnts;

    U16 DesTrackCnts    =  ((pagecnts * PAA_NUM_PER_PAGE) + (MAX_DESC_PER_TRK - 1)) / MAX_DESC_PER_TRK;

    //TCGPRN("%s() %x\n", __FUNCTION__, DesTrackCnts);
    // TCG_DBG_P(2, 3, 0x820401, 2, DesTrackCnts);  //82 04 01, "[F]tcg_hal_write_launched, DesTrackCnts = [%04X]", 2
    Core_JobCompleteWait(0xC4);   // wait previous all job down.

    for(i = 0; i < DesTrackCnts; i++){
        paacnts = (pagecnts * PAA_NUM_PER_PAGE) > MAX_DESC_PER_TRK ? MAX_DESC_PER_TRK : (pagecnts * PAA_NUM_PER_PAGE);

        pTcgJob              = FIO_PopJob(FIO_JOB_POOL_RW_FREE, 0xd2);
        pTcgJob->NCLcmd      = JOB_NCL_CMD_WRITE;
        //pTcgJob->dtagList    = CACHE_WO_DTAG;
        pTcgJob->type        = CACHE_TCG;
        pTcgJob->header      = FTL_BLOCK_HEADER_SLC;
        pTcgJob->dtagCnt     = 0;
        pTcgJob->flag        = FIO_F_TCG | FIO_F_NO_FW_ERR_HANDLE | FIO_F_SLC;
        pTcgJob->nandcmdflag = FIO_F_WRITE_FIX_META;
        pTcgJob->duFmt       = DU_FMT_USER_4K;
        pTcgJob->paaCnt      = paacnts;

        memcpy(pTcgJob->duMeta, &gTcgAux[MAX_DESC_PER_TRK * i], sizeof(tTcgAux) * paacnts);
        CACHE_Alloc_TCG(pTcgJob, (void*)(pBuffer + PAGE_NUM_PER_DESTRACK * CFG_UDATA_PER_PAGE * i));

        memcpy(pTcgJob->paa, &gTcgPaa[MAX_DESC_PER_TRK * i], sizeof(tPAA) * paacnts);
        Core_JobIssue(pTcgJob);
        Core_JobCompleteWait(0xC5);

        if (pTcgJob->status & FIO_S_PROG_ERR){
            TCG_ERR_PRN("tcg_hal_write_launched() Error! , st|%x\n", pTcgJob->status);
            DBG_P(0x2, 0x03, 0x7F7F32, 4, pTcgJob->status);  // tcg_hal_write_launched() Error! , st|%x
            // DBG_P(2, 3, 0x820410, 2, pTcgJob->status); //82 04 10, "!!!Error!!! ------ tcg_hal_write_launched Error status[%X] ------", 2
            return zNG;
        }
    }
    //TCGPRN("==Wr OK==\n");
    // TCG_PRINTF("zOK----------------------------\n\n");
    return zOK;
}

///----------------------------------------
/// Erase Launched
///----------------------------------------
tcg_code int tcg_hal_erase_launched(U16 blkcnts)
{
    U32 i;

    //TCGPRN("%s()\n", __FUNCTION__);
    // TCG_DBG_P(1, 3, 0x820402);  //82 04 02, "[F]tcg_hal_erase_launched"
    Core_JobCompleteWait(0xC6);   // wait previous all job down.

    pTcgJob = FIO_PopJob(FIO_JOB_POOL_RW_FREE, 0xc4);
    pTcgJob->pCmdMsg = NULL;
    pTcgJob->NCLcmd  = JOB_NCL_CMD_ERASE;
    pTcgJob->type    = CACHE_TCG;
    pTcgJob->flag    = FIO_F_TCG | FIO_F_NO_FW_ERR_HANDLE | FIO_F_WAIT | FIO_F_SLC;
    pTcgJob->paaCnt  = blkcnts;
    for(i = 0; i < blkcnts; i++){
        pTcgJob->paa[i].all32 = gTcgPaa[i*PAA_NUM_PER_PAGE].all32;
    }

    Core_JobIssue(pTcgJob);
    Core_JobCompleteWait(0xC7);

    if (pTcgJob->status & FIO_S_ERASE_ERR){
        TCG_ERR_PRN("tcg_hal_erase_launched() Error! , st|%x\n", pTcgJob->status);
        DBG_P(0x2, 0x03, 0x7F7F33, 4, pTcgJob->status);  // tcg_hal_erase_launched() Error! , st|%x
        // DBG_P(2, 3, 0x820411, 2, pTcgJob->status); //82 04 11, "!!!Error!!! ------ tcg_hal_erase_launched Error status[%X] ------", 2
        return zNG;
    }
    return zOK;
}

///----------------------------------------
///  read row
///----------------------------------------
tcg_code int TCG_RdMulChan(tTcgGrpDef grp, tTcgLogAddr ReadPnt, U32 SkipChanMap)
{
    U16 i;
    U8 x, loops;
    tTcgLogAddr real_ReadPnt;

    // TCG_DBG_P(1, 3, 0x820403);  //82 04 03, "[F]TCG_RdMulChan"
    for(x=loops=0; x<DEVICES_NUM_PER_ROW; x++){
        if((x + ReadPnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
            if(!(SkipChanMap & mBIT(x))) loops++;
        }
    }
    if(loops == 0) return FALSE; //zNG;

    real_ReadPnt.all = ReadPnt.all;
    i = 0;
    for(x=0; x<DEVICES_NUM_PER_ROW; x++){
        if((x + ReadPnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
            if(!(SkipChanMap & mBIT(x))){
                real_ReadPnt.pc.cell = ReadPnt.pc.cell + x;
                FillPaa(i, real_ReadPnt, grp);
                ValidBlks[i] = real_ReadPnt.pc.cell;
                i++;
            }
        }
    }
    ValidCnt = i;

    return (tcg_hal_read_launched(loops, tcgTmpBuf));

}

///----------------------------------------
///  write row
///----------------------------------------
tcg_code int TCG_WrMulChan(tTcgGrpDef grp, tTcgLogAddr WritePnt, U32 SkipChanMap)
{
    U16 i;
    U8 x, loops;
    tTcgLogAddr real_WritePnt;

    // TCG_DBG_P(1, 3, 0x820404);  //82 04 04, "[F]TCG_WrMulChan"
    for(x=loops=0; x<DEVICES_NUM_PER_ROW; x++){
        if((x + WritePnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
            if(!(SkipChanMap & mBIT(x))) loops++;
        }
    }
    if(loops==0) return FALSE; //zNG;

    real_WritePnt.all = WritePnt.all;
    i=0;
    for(x=0; x<DEVICES_NUM_PER_ROW; x++){
        if((x + WritePnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
            if(!(SkipChanMap & mBIT(x))){
                real_WritePnt.pc.cell     =   WritePnt.pc.cell + x;
                FillPaa(i, real_WritePnt, grp);
                FillAux(i, real_WritePnt.all, real_WritePnt.all);
                ValidBlks[i] = real_WritePnt.pc.cell;
                i++;
            }
        }
    }
    ValidCnt = i;

    return (tcg_hal_write_launched(loops, tcgTmpBuf));
}


///----------------------------------------
///  erase row
///----------------------------------------
tcg_code int TCG_ErMulChan(tTcgGrpDef grp, tTcgLogAddr ErasePnt, U32 SkipChanMap)
{
    U16 i;
    U8 x, loops;
    tTcgLogAddr EraseBlkAddr;

    // DBG_P(1, 3, 0x820405);  //82 04 05, "[F]TCG_ErMulChan"
    for(x=loops=0; x<DEVICES_NUM_PER_ROW; x++){
        if((x + ErasePnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
            if(!(SkipChanMap & mBIT(x))) loops++;
        }
    }
    if(loops==0) return FALSE; //zNG;

    EraseBlkAddr.all = ErasePnt.all;
    i=0;
    for(x=0; x<DEVICES_NUM_PER_ROW; x++){
        if((x + ErasePnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
            if(!(SkipChanMap & mBIT(x))){  // It's not defected block
                EraseBlkAddr.pc.cell      =   ErasePnt.pc.cell + x;
                FillPaa(i, EraseBlkAddr, grp);
                ValidBlks[i] = EraseBlkAddr.pc.cell;
                if(grp > ZONE_DS){
                    tcgG5EraCnt[EraseBlkAddr.pc.cell]++;
                }else{
                    tcgG4EraCnt[EraseBlkAddr.pc.cell]++;
                }
                i++;

                #if 1  //Due to Seqencer Erase block is not clean in same CH,CE,LUN and different PLN in one trace.
                // if((i==2) || ((x + ErasePnt.pc.cell) == (TCG_MBR_CELLS-1))){    //two LAA in a Job
                    if(tcg_hal_erase_launched(i) == zNG){     // one LAA in a Job
                        //TCGPRN("erase fail ,blk|%x\n", EraseBlkAddr.pc.cell);
                        DBG_P(0x2, 0x03, 0x730006, 4, EraseBlkAddr.pc.cell);  // erase fail ,blk|%x
                        // DBG_P(2, 3, 0x820332, 2, EraseBlkAddr.pc.cell);  //82 03 32, "era DF = %X", 2
                        if(grp > ZONE_DS){
                            tcgG5Dft[EraseBlkAddr.pc.cell] = 0xDF;
                        }else{
                            tcgG4Dft[EraseBlkAddr.pc.cell] = 0xDF;
                        }
                    }
                    i = 0;
                // }
                #endif

            }
        }
    }
    ValidCnt = i;
#if 0  //Due to Seqencer Erase block is not clean in same CH,CE,LUN and different PLN in one trace.
    return (tcg_hal_erase_launched(loops));
#else
    return zOK;
#endif
}


#define PAGE_NUM_PER_STAGE      4
#define STAGE_NUM_PER_BLOCK     (PAGE_NUM_PER_BLOCK / PAGE_NUM_PER_STAGE)
#define ZONE51_MASK             0x5a4e3531   // "ZN51"
///----------------------------------------
///  read tcgZone51 block
///----------------------------------------
tcg_code int TCG_Zone51Rd1Pg(tPAA paa)
{
    //TCGPRN("TCG_Zone51Rd1Pg()\n");
    DBG_P(0x01, 0x03, 0x730007 );  // TCG_Zone51Rd1Pg()
    gTcgPaa[0].all32 = paa.all32;
    COPY2NEXT4(gTcgPaa, 0);
    FillFragInPage(&gTcgPaa[0]);
    return(tcg_hal_read_launched(1, OnePgSzBuf));
}
///----------------------------------------
///  read tcgZone51 block
///----------------------------------------
tcg_code int TCG_RdZone51(tPAA paa, void* buf)
{
    U32 pgCnt;

    // just read 1'st stage because every stage is same content.
    for(pgCnt = 0; pgCnt < PAGE_NUM_PER_STAGE; pgCnt++){
        paa.b.WLpage = pgCnt;
        gTcgPaa[pgCnt * PAA_NUM_PER_PAGE].all32 = paa.all32;
        COPY2NEXT4(gTcgPaa, pgCnt * PAA_NUM_PER_PAGE + 0);
        FillFragInPage(&gTcgPaa[pgCnt * PAA_NUM_PER_PAGE + 0]);
    }
    return(tcg_hal_read_launched(PAGE_NUM_PER_STAGE, buf));
}

///----------------------------------------
///  write tcgZone51 block
///----------------------------------------
tcg_code int TCG_WrZone51(tPAA paa, void* buf)
{
    U32 stageCnt;      // 4 pages per stage
    U32 pgCnt;

    for(stageCnt = 0; stageCnt < STAGE_NUM_PER_BLOCK; stageCnt++){
        for(pgCnt = 0; pgCnt < PAGE_NUM_PER_STAGE; pgCnt++){
            paa.b.WLpage = stageCnt * PAGE_NUM_PER_STAGE + pgCnt;
            gTcgPaa[pgCnt * PAA_NUM_PER_PAGE].all32 = paa.all32;
            COPY2NEXT4(gTcgPaa, pgCnt * PAA_NUM_PER_PAGE + 0);
            FillFragInPage(&gTcgPaa[pgCnt * PAA_NUM_PER_PAGE + 0]);
            FillAux(pgCnt * PAA_NUM_PER_PAGE + 0, paa.all32 /*Aux0*/ , TcgG4NxtPnt.all /*Aux1*/);
        }
        if(tcg_hal_write_launched(PAGE_NUM_PER_STAGE, buf) == zNG){
            return zNG;
        }
    }
    return zOK;
}

///----------------------------------------
///  erase tcgZone51 block
///----------------------------------------
tcg_code int TCG_EraseZone51(tPAA paa)
{
    paa.b.WLpage = 0;
    gTcgPaa[0].all32 = paa.all32;
    COPY2NEXT4(gTcgPaa, 0);
    FillFragInPage(&gTcgPaa[0]);
    //TCGPRN("TCG_EraseZone51() paa|%x\n", paa.all32);
    DBG_P(0x2, 0x03, 0x730008, 4, paa.all32);  // TCG_EraseZone51() paa|%x
    //TCGPRN("ch|%x ce|%x blk|%x lun|%x plane|%x\n",paa.b.ch, paa.b.ce, paa.b.block, paa.b.lun, paa.b.plane);
    DBG_P(0x6, 0x03, 0x730009, 4,paa.b.ch, 4, paa.b.ce, 4, paa.b.block, 4, paa.b.lun, 4, paa.b.plane);  // ch|%x ce|%x blk|%x lun|%x plane|%x

    return(tcg_hal_erase_launched(1));
}


///----------------------------------------
///  read 1 NG page
///----------------------------------------
tcg_code int TCG_Rd1RtyPg(tTcgLogAddr readpnt, tTcgGrpDef grp)
{
    // DBG_P(3, 3, 0x820406, 4, grp, 2, readpnt.all);  //82 04 06, "[F]TCG_Rd1RtyPg GRP[%X] RdLaa[%X]"
    //gNativePaa[0].all = readpnt.all;   // TCG_Rd1Pg no read retry currently.
    FillPaa(0, readpnt, grp);
    return (tcg_hal_read_launched(1, OnePgSzBuf));
}

///----------------------------------------
///  read 1 NG page with Adjust Voltage
///----------------------------------------
/*
int TCG_Rd1RtyPgWithAdjVoltage(tTcgLogAddr readpnt, U32 voltage, tTcgGrpDef grp)
{
    int i;
    // DBG_P(4, 3, 0x82041C, 4, grp, 4, voltage, 2, readpnt.all);  //82 04 1C, "[F]TCG_Rd1RtyPgWithAdjVoltage GRP[%X] Voltage[%08X] RdLaa[%X]", 4 4 2
    //gNativePaa[0].all = readpnt.all;   // TCG_Rd1Pg no read retry currently.
    FillPaa(0, readpnt, grp);
    for(i=PAA_NUM_PER_PAGE*2; i>0; i-=2){
        gTcgPaa[i-1].all32 = gTcgPaa[i-1].all32;
    }
    return (TCG_ReadRetryLaunched(2, OnePgSzBuf, voltage));  //
}
*/
///----------------------------------------
///  read 1 page
///----------------------------------------
tcg_code int TCG_Rd1Pg(tTcgLogAddr readpnt, tTcgGrpDef grp)
{
    U16 errNativeBlkCnt;

    //HERE(readpnt.all);
    #if 0
    // DBG_P(3, 3, 0x82041E, 4, grp, 2, readpnt.all);  //82 04 1E, "[F]TCG_Rd1Pg GRP[%08X] RdLaa[%04X]", 4 2
    #endif
    gNativePaa[0].all = readpnt.all;   // TCG_Rd1Pg no read retry currently.
    FillPaa(0, readpnt, grp);
    if(tcg_hal_read_launched(1, OnePgSzBuf) == zNG){
        if(TCG_ReadRetry(grp, &errNativeBlkCnt) == zNG){   // errNativeBlkCnt doesn't use in this TCG_Rd1Pg()
            return zNG;
        }
    }

    return zOK;
}

///----------------------------------------
///  write 1 page
///----------------------------------------
tcg_code int TCG_Wr1Pg(tTcgLogAddr writepnt, tTcgGrpDef grp, U32 Aux0, U32 Aux1)
{
    //HERE(writepnt.all);
    FillPaa(0, writepnt, grp);
    FillAux(0, Aux0, Aux1);
    return (tcg_hal_write_launched(1, OnePgSzBuf));
}

///----------------------------------------
/// group 4 read L2P
///----------------------------------------
tcg_code int TCG_G4RdL2p(void)
{
    tTcgLogAddr  readpnt;
    U8 ipage;
    U16 errNativeBlkCnt;

    //TCGPRN("TCG_G4RdL2p() TcgG4Pnt|%x\n", TcgG4Pnt.all);
    DBG_P(0x2, 0x03, 0x73000A, 4, TcgG4Pnt.all);  // TCG_G4RdL2p() TcgG4Pnt|%x
    // DBG_P(1, 3, 0x82040B);  //82 04 0B, "[F]TCG_G4RdL2p"

    readpnt.all=TcgG4Pnt.all;

    for(ipage=0; ipage<L2P_PAGE_CNT; ipage++){
        gNativePaa[ipage].all = readpnt.all;
        FillPaa(ipage, readpnt, ZONE_GRP4);
        readpnt.all += SLC_OFF(1);
    }

    if(tcg_hal_read_launched(L2P_PAGE_CNT, (U8 *)&(pG4->b.TcgG4Header)) == zNG){
        if(TCG_ReadRetry(ZONE_GRP4, &errNativeBlkCnt) == zNG){   // errNativeBlkCnt doesn't use in this TCG_G4RdL2p()
            return zNG;
        }
    }

    return zOK;
}

///----------------------------------------
/// group 4 write L2P
///----------------------------------------
tcg_code int TCG_G4WrL2p(void)
{
    tTcgLogAddr  writepnt;
    U8 ipage;
    int st;

    //TCGPRN("TCG_G4WrL2p() TcgG4NxtPnt|%x\n", TcgG4NxtPnt.all);
    DBG_P(0x2, 0x03, 0x73000B, 4, TcgG4NxtPnt.all);  // TCG_G4WrL2p() TcgG4NxtPnt|%x
    // DBG_P(1, 3, 0x82040C);  //82 04 0C, "[F]TCG_G4WrL2p"

    writepnt.all = TcgG4NxtPnt.all;
    FillPaa(0, writepnt, ZONE_GRP4);
    if((st = tcg_hal_erase_launched(1)) == zNG){
        tcgG4Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
    }
    tcgG4EraCnt[writepnt.pc.cell]++;
  #ifdef TCG_EEP_NOR
    TCG_NorEepWrite();
  #else
    SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
  #endif

    TcgG4Pnt.all        =   TcgG4NxtPnt.all;
    TcgG4CurPnt.all     =   TcgG4NxtPnt.all;
    TcgG4NxtPnt.all     +=  SLC_OFF(L2P_PAGE_CNT);

    if(st == zNG){
        return zNG;
    }

    TcgG4CurHistNo      =   TcgG4NxtHistNo;

    for(ipage=0; ipage<L2P_PAGE_CNT; ipage++){
        FillPaa(ipage, writepnt, ZONE_GRP4);
        FillAux(ipage, TCG_G4_TAG, TcgG4NxtHistNo);
        writepnt.all += SLC_OFF(1);
    }

    TcgG4NxtHistNo++;
    if(tcg_hal_write_launched(L2P_PAGE_CNT, (U8 *)&(pG4->b.TcgG4Header)) == zNG){
        //TCGPRN("ERROR! G4 L2P WR Fail.\n");
        DBG_P(0x01, 0x03, 0x73000C );  // ERROR! G4 L2P WR Fail.
        tcgG4Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
        return zNG;
    }

    if(tcg_hal_read_launched(L2P_PAGE_CNT, (U8 *)tcgTmpBuf + (TCG_GENERAL_BUF_SIZE - 0x4000*L2P_PAGE_CNT)) == zNG){
        //TCGPRN("ERROR! G4 L2P RD Fail.\n");
        DBG_P(0x01, 0x03, 0x73000D );  // ERROR! G4 L2P RD Fail.
        // DBG_P(1, 3, 0x82001B);   //82 00 1B, "!!!Error, G4 RD L2P fail afetr Wr L2P"
        tcgG4Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
        return zNG;
    }

    #if 0
    //TCGPRN("AUX0-1 %x %x %x %x - %x %x %x %x\n", gTcgAux[0].aux0, gTcgAux[0].aux1, gTcgAux[0].aux2, gTcgAux[0].aux3, gTcgAux[1].aux0, gTcgAux[1].aux1, gTcgAux[1].aux2, gTcgAux[1].aux3);
    DBG_P(0x9, 0x03, 0x73000E, 4, gTcgAux[0].aux0, 4, gTcgAux[0].aux1, 4, gTcgAux[0].aux2, 4, gTcgAux[0].aux3, 4, gTcgAux[1].aux0, 4, gTcgAux[1].aux1, 4, gTcgAux[1].aux2, 4, gTcgAux[1].aux3);  // AUX0-1 %x %x %x %x - %x %x %x %x
    //TCGPRN("AUX0-1 %x %x %x %x - %x %x %x %x\n", gTcgAux[2].aux0, gTcgAux[2].aux1, gTcgAux[2].aux2, gTcgAux[2].aux3, gTcgAux[3].aux0, gTcgAux[3].aux1, gTcgAux[3].aux2, gTcgAux[3].aux3);
    DBG_P(0x9, 0x03, 0x73000F, 4, gTcgAux[2].aux0, 4, gTcgAux[2].aux1, 4, gTcgAux[2].aux2, 4, gTcgAux[2].aux3, 4, gTcgAux[3].aux0, 4, gTcgAux[3].aux1, 4, gTcgAux[3].aux2, 4, gTcgAux[3].aux3);  // AUX0-1 %x %x %x %x - %x %x %x %x
    //TCGPRN("AUX0-1 %x %x %x %x - %x %x %x %x\n", gTcgAux[4].aux0, gTcgAux[4].aux1, gTcgAux[4].aux2, gTcgAux[4].aux3, gTcgAux[5].aux0, gTcgAux[5].aux1, gTcgAux[5].aux2, gTcgAux[5].aux3);
    DBG_P(0x9, 0x03, 0x730010, 4, gTcgAux[4].aux0, 4, gTcgAux[4].aux1, 4, gTcgAux[4].aux2, 4, gTcgAux[4].aux3, 4, gTcgAux[5].aux0, 4, gTcgAux[5].aux1, 4, gTcgAux[5].aux2, 4, gTcgAux[5].aux3);  // AUX0-1 %x %x %x %x - %x %x %x %x
    #endif

    if(memcmp((U8 *)&(pG4->b.TcgG4Header), (U8 *)tcgTmpBuf + (TCG_GENERAL_BUF_SIZE - 0x4000*L2P_PAGE_CNT), 0x4000*L2P_PAGE_CNT) != 0){
        TCG_ERR_PRN("ERROR! G4 L2P CMP Fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F36);  // ERROR! G4 L2P CMP Fail.
        #if 1
        {
            U32* p = (U32*)((U8*)&(pG4->b.TcgG4Header));
            U32* q = (U32*)((U8 *)tcgTmpBuf + (TCG_GENERAL_BUF_SIZE - 0x4000*L2P_PAGE_CNT));

            //TCGPRN("addr %x p = %x %x %x %x %x %x %x %x\n", (U32)p, *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
            DBG_P(0xa, 0x03, 0x730012, 4, (U32)p, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // addr %x p = %x %x %x %x %x %x %x %x
            //TCGPRN("addr %x q = %x %x %x %x %x %x %x %x\n", (U32)q, *(q+0), *(q+1), *(q+2), *(q+3), *(q+4), *(q+5), *(q+6), *(q+7));
            DBG_P(0xa, 0x03, 0x730013, 4, (U32)q, 4, *(q+0), 4, *(q+1), 4, *(q+2), 4, *(q+3), 4, *(q+4), 4, *(q+5), 4, *(q+6), 4, *(q+7));  // addr %x q = %x %x %x %x %x %x %x %x
            // DBG_P(2, 3, 0x820101, 4, p, 4, q); // temporary // alexcheck

            {
                U8* p = (U8*)((U8*)&(pG4->b.TcgG4Header));
                U8* q = (U8*)((U8 *)tcgTmpBuf /*+ (TCG_GENERAL_BUF_SIZE - 0x4000*L2P_PAGE_CNT)*/);
                while(*p == *q){
                    p++; q++;
                }
                p--, q--;
                p--, q--;
                //TCGPRN("%x, p = %x %x %x %x %x %x %x %x\n", (U32)p, *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
                DBG_P(0xa, 0x03, 0x730014, 4, (U32)p, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // %x, p = %x %x %x %x %x %x %x %x
                //TCGPRN("%x, q = %x %x %x %x %x %x %x %x\n", (U32)q, *(q+0), *(q+1), *(q+2), *(q+3), *(q+4), *(q+5), *(q+6), *(q+7));
                DBG_P(0xa, 0x03, 0x730015, 4, (U32)q, 4, *(q+0), 4, *(q+1), 4, *(q+2), 4, *(q+3), 4, *(q+4), 4, *(q+5), 4, *(q+6), 4, *(q+7));  // %x, q = %x %x %x %x %x %x %x %x
            }
        }
        #endif


        // DBG_P(1, 3, 0x82001C);  //82 00 1C, "!!!Error, G4 CMP L2P Error"
        tcgG4Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
        return zNG;
    }
    tcgL2pAssis.g4.tag = L2P_ASS_TAG;
    tcgL2pAssis.g4.cell_no = TcgG4NxtPnt.pc.cell;
    return zOK;
}

//----------------------------------------
// group 4 dummy read (pg->ce->ch->blk->lun)
//----------------------------------------
tcg_code int TCG_G4DmyRd(void)
{
    U8 ipage;
    tTcgLogAddr myRdPnt;

    // DBG_P(1, 3, 0x820430);  //82 04 30, "[F]TCG_G4DmyRd"
    for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
        myRdPnt.all = pG4->b.TcgMbrL2P[tcgNfHalParams.laas].all;
        gNativePaa[ipage].all = myRdPnt.all;
        FillPaa(ipage, myRdPnt, ZONE_GRP4);
    }
    if(tcg_hal_read_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf) == zNG){
        if(TCG_ErrHandling(ZONE_GRP4) == zNG){
            for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
                if((pG4->b.TcgMbrL2P[tcgNfHalParams.laas + ipage].blk) >= (TCG_MBR_CELLS)){  //blank?
                    memset((U8*)tcgNfHalParams.pBuf + (ipage * CFG_UDATA_PER_PAGE) , 0, CFG_UDATA_PER_PAGE);
                }
            }
            if (pTcgJob->status & (FIO_S_BLANK_ERR)){
                return zOK;
            }
            return zNG;
        }
    }

    return zOK;
}

//----------------------------------------
// group 4 read (pg->ce->ch->blk->lun)
//----------------------------------------
tcg_code int TCG_G4Rd(void)
{
    U16 ipage;
    tTcgLogAddr myRdPnt;

    // DBG_P(1, 3, 0x82040D);  //82 04 0D, "[F]TCG_G4Rd"
    //TCGPRN("TCG_G4Rd()\n");
    DBG_P(0x01, 0x03, 0x730016 );  // TCG_G4Rd()
    for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
        myRdPnt.all = pG4->b.TcgMbrL2P[tcgNfHalParams.laas + ipage].all;
        gNativePaa[ipage].all = myRdPnt.all;
        FillPaa(ipage, myRdPnt, ZONE_GRP4);
    }
    if(tcg_hal_read_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf) == zNG){
        if(TCG_ErrHandling(ZONE_GRP4) == zNG){
            for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
                if((pG4->b.TcgMbrL2P[tcgNfHalParams.laas + ipage].blk) >= (TCG_MBR_CELLS)){  //blank?
                    memset((U8*)tcgNfHalParams.pBuf + (ipage * CFG_UDATA_PER_PAGE) , 0, CFG_UDATA_PER_PAGE);
                }
            }
            if (pTcgJob->status & (FIO_S_BLANK_ERR)){
                return zOK;
            }
            return zNG;
        }
    }
    #if 0
    //TCGPRN("AUX0-1 %x %x %x %x - %x %x %x %x\n", gTcgAux[0].aux0, gTcgAux[0].aux1, gTcgAux[0].aux2, gTcgAux[0].aux3, gTcgAux[1].aux0, gTcgAux[1].aux1, gTcgAux[1].aux2, gTcgAux[1].aux3);
    DBG_P(0x9, 0x03, 0x730017, 4, gTcgAux[0].aux0, 4, gTcgAux[0].aux1, 4, gTcgAux[0].aux2, 4, gTcgAux[0].aux3, 4, gTcgAux[1].aux0, 4, gTcgAux[1].aux1, 4, gTcgAux[1].aux2, 4, gTcgAux[1].aux3);  // AUX0-1 %x %x %x %x - %x %x %x %x
    //TCGPRN("AUX0-1 %x %x %x %x - %x %x %x %x\n", gTcgAux[2].aux0, gTcgAux[2].aux1, gTcgAux[2].aux2, gTcgAux[2].aux3, gTcgAux[3].aux0, gTcgAux[3].aux1, gTcgAux[3].aux2, gTcgAux[3].aux3);
    DBG_P(0x9, 0x03, 0x730018, 4, gTcgAux[2].aux0, 4, gTcgAux[2].aux1, 4, gTcgAux[2].aux2, 4, gTcgAux[2].aux3, 4, gTcgAux[3].aux0, 4, gTcgAux[3].aux1, 4, gTcgAux[3].aux2, 4, gTcgAux[3].aux3);  // AUX0-1 %x %x %x %x - %x %x %x %x
    //TCGPRN("AUX0-1 %x %x %x %x - %x %x %x %x\n", gTcgAux[4].aux0, gTcgAux[4].aux1, gTcgAux[4].aux2, gTcgAux[4].aux3, gTcgAux[5].aux0, gTcgAux[5].aux1, gTcgAux[5].aux2, gTcgAux[5].aux3);
    DBG_P(0x9, 0x03, 0x730019, 4, gTcgAux[4].aux0, 4, gTcgAux[4].aux1, 4, gTcgAux[4].aux2, 4, gTcgAux[4].aux3, 4, gTcgAux[5].aux0, 4, gTcgAux[5].aux1, 4, gTcgAux[5].aux2, 4, gTcgAux[5].aux3);  // AUX0-1 %x %x %x %x - %x %x %x %x
    #endif
    return zOK;
}

//----------------------------------------
// group 4 write (pg->ce->ch->blk->lun)
//----------------------------------------
tcg_code int TCG_G4Wr(void)
{
    tTcgLogAddr  writepnt;
    U32     i;
    int     sts;

    // DBG_P(1, 3, 0x82040E);  //82 04 0E, "[F]TCG_G4Wr"
    //TCGPRN("TCG_G4Wr()\n");
    DBG_P(0x01, 0x03, 0x73001A );  // TCG_G4Wr()
    writepnt.all = 0xFFFFFFFF;
    for(i=0; i<tcgNfHalParams.laacnt; i++)
    {
        while((TcgG4NxtPnt.all == 0xFFFFFFFF) && (G4_FreeBlk > 0))
        {
            // build vac & validmap
            TcgMbrBuildGcTables();
            G4_FreeBlk = Tcg_G4FreeBlk(TcgMbrCellVac);
            // find a vac0 (excluding defect)
            TcgG4NxtPnt.pc.cell = TcgG4CurPnt.pc.cell;
            TcgG4NxtPnt.pc.page = 0;
            while(1){
                TcgG4NxtPnt.pc.cell++;
                if(TcgG4NxtPnt.pc.cell >= TCG_MBR_CELLS) TcgG4NxtPnt.pc.cell = 0;
                if( (tcgG4Dft[TcgG4NxtPnt.pc.cell] == 0)
                &&(TcgMbrCellVac[TcgG4NxtPnt.pc.cell] == 0) )
                {
                    break;
                }
            }
            // open block (write 5 pages)
            if(TCG_G4WrL2p() == zNG){
                TcgG4NxtPnt.all = 0xFFFFFFFF;
            #ifdef TCG_EEP_NOR
                TCG_NorEepWrite();
            #else
                SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
            #endif
            }

        }
        ASSERT(G4_FreeBlk != 0);

        pG4->b.TcgMbrL2P[tcgNfHalParams.laas + i].all = WrPntBox[i].all = writepnt.all = TcgG4CurPnt.all = TcgG4NxtPnt.all;
        tcgL2pAssis.g4.tag = L2P_ASS_TAG;
        tcgL2pAssis.g4.tbl_idx = TcgG4NxtPnt.pc.page - L2P_PAGE_CNT;
        tcgL2pAssis.g4.laa_no[tcgL2pAssis.g4.tbl_idx] = tcgNfHalParams.laas + i;
        TcgG4NxtPnt.all += SLC_OFF(1);
        if(TcgG4NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){
            TcgG4NxtPnt.all = 0xFFFFFFFF;
        }
    }

    for(i=0; i<tcgNfHalParams.laacnt; i++){
        FillPaa(i, WrPntBox[i], ZONE_GRP4);
        FillAux(i, tcgNfHalParams.laas + i, TcgG4CurHistNo);
    }

    sts = tcg_hal_write_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf);

    if(sts == zOK){
        if(G4_FreeBlk < TCG_GC_THRESHOLD){  // GC threshold = 3
            G4_GC();
        }
    }
    return sts;
}

///----------------------------------------
/// group 5 read L2P
///----------------------------------------
tcg_code int TCG_G5RdL2p(void)
{
    tTcgLogAddr  readpnt;
    U16 errNativeBlkCnt, ipage;

    // DBG_P(1, 3, 0x820407);  //82 04 07, "[F]TCG_G5RdL2p"
    //TCGPRN("TCG_G5RdL2p() %x\n", TcgG5Pnt.all);
    DBG_P(0x2, 0x03, 0x73001B, 4, TcgG5Pnt.all);  // TCG_G5RdL2p() %x
    readpnt.all=TcgG5Pnt.all;

    for(ipage=0; ipage<L2P_PAGE_CNT; ipage++){
        gNativePaa[ipage].all = readpnt.all;
        FillPaa(ipage, readpnt, ZONE_GRP5);
        readpnt.all += SLC_OFF(1);
    }

    if(tcg_hal_read_launched(L2P_PAGE_CNT, (U8 *)&(pG5->b.TcgG5Header)) == zNG){
        if(TCG_ReadRetry(ZONE_GRP5, &errNativeBlkCnt) == zNG){   // errNativeBlkCnt doesn't use in this TCG_G5RdL2p()
            return zNG;
        }
    }

    return zOK;
}

///----------------------------------------
/// group 5 write L2P
///----------------------------------------
tcg_code int TCG_G5WrL2p(void)
{
    tTcgLogAddr  writepnt;
    U16 ipage;
    int st;

    // DBG_P(1, 3, 0x820408);  //82 04 08, "[F]TCG_G5WrL2p"
    //TCGPRN("TCG_G5WrL2p() %x\n", TcgG5NxtPnt.all);
    DBG_P(0x2, 0x03, 0x73001C, 4, TcgG5NxtPnt.all);  // TCG_G5WrL2p() %x
    writepnt.all = TcgG5NxtPnt.all;
    FillPaa(0, writepnt, ZONE_GRP5);
    if((st = tcg_hal_erase_launched(1)) == zNG){
        tcgG5Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
    }
    tcgG5EraCnt[writepnt.pc.cell]++;
  #ifdef TCG_EEP_NOR
    TCG_NorEepWrite();
  #else
    SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
  #endif

    TcgG5Pnt.all        =   TcgG5NxtPnt.all;
    TcgG5CurPnt.all     =   TcgG5NxtPnt.all;
    TcgG5NxtPnt.all     +=  SLC_OFF(L2P_PAGE_CNT);

    if(st == zNG){
        return zNG;
    }

    TcgG5CurHistNo      =   TcgG5NxtHistNo;

    for(ipage=0; ipage<L2P_PAGE_CNT; ipage++){
        FillPaa(ipage, writepnt, ZONE_GRP5);
        FillAux(ipage, TCG_G5_TAG, TcgG5NxtHistNo);
        writepnt.all += SLC_OFF(1);
    }

    TcgG5NxtHistNo++;
    if(tcg_hal_write_launched(L2P_PAGE_CNT, (U8 *)&(pG5->b.TcgG5Header)) == zNG){
        TCG_ERR_PRN("ERROR! G5 L2P WR Fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F37);  // ERROR! G5 L2P WR Fail.
        tcgG5Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
        return zNG;
    }

    if(tcg_hal_read_launched(L2P_PAGE_CNT, (U8 *)tcgTmpBuf + (TCG_GENERAL_BUF_SIZE - 0x4000*L2P_PAGE_CNT)) == zNG){
        // DBG_P(1, 3, 0x82001D);   //82 00 1D, "!!!Error, G5 RD L2P fail afetr Wr L2P"
        TCG_ERR_PRN("ERROR! G5 L2P RD Fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F38);  // ERROR! G5 L2P RD Fail.
        tcgG5Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
        return zNG;
    }

    if(memcmp((U8 *)&(pG5->b.TcgG5Header), (U8 *)tcgTmpBuf + (TCG_GENERAL_BUF_SIZE - 0x4000*L2P_PAGE_CNT), 0x4000*L2P_PAGE_CNT) != 0){
        TCG_ERR_PRN("ERROR! G5 L2P CMP Fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F39);  // ERROR! G5 L2P CMP Fail.
        // DBG_P(1, 3, 0x82001E);  //82 00 1E, "!!!Error, G5 CMP L2P Error"
        tcgG5Dft[writepnt.pc.cell] = 0xDF;   //set defect flag
        return zNG;
    }

    tcgL2pAssis.g5.tag = L2P_ASS_TAG;
    tcgL2pAssis.g5.cell_no = TcgG5NxtPnt.pc.cell;
    return zOK;
}

//----------------------------------------
// group 5 dummy read (pg->ce->ch->blk->lun)
//----------------------------------------
tcg_code int TCG_G5DmyRd(void)
{
    U8 ipage;
    tTcgLogAddr myRdPnt;

    // DBG_P(1, 3, 0x820431);  //82 04 31, "[F]TCG_G5DmyRd"
    for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
        myRdPnt.all = pG5->b.TcgTempMbrL2P[tcgNfHalParams.laas].all;
        gNativePaa[ipage].all = myRdPnt.all;
        FillPaa(ipage, myRdPnt, ZONE_GRP5);
    }
    if(tcg_hal_read_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf) == zNG){
        if(TCG_ErrHandling(ZONE_GRP5) == zNG){
            for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
                if((pG5->b.TcgTempMbrL2P[tcgNfHalParams.laas + ipage].blk) >= (TCG_MBR_CELLS)){  //blank ?
                    memset((U8*)tcgNfHalParams.pBuf + (ipage * CFG_UDATA_PER_PAGE) , 0, CFG_UDATA_PER_PAGE);
                }
            }
            return zNG;
        }
    }
    return zOK;
}

//----------------------------------------
// group 5 read (pg->ce->ch->blk->lun)
//----------------------------------------
tcg_code int TCG_G5Rd(void)
{
    U16 ipage;
    tTcgLogAddr myRdPnt;

    // DBG_P(1, 3, 0x820409);  //82 04 09, "[F]TCG_G5Rd"
    //TCGPRN("TCG_G5Rd()\n");
    DBG_P(0x01, 0x03, 0x730020 );  // TCG_G5Rd()
    for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
        myRdPnt.all = pG5->b.TcgTempMbrL2P[tcgNfHalParams.laas + ipage].all;
        gNativePaa[ipage].all = myRdPnt.all;
        FillPaa(ipage, myRdPnt, ZONE_GRP5);
    }
    if(tcg_hal_read_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf) == zNG){
        if(TCG_ErrHandling(ZONE_GRP5) == zNG){
            for(ipage=0; ipage<tcgNfHalParams.laacnt; ipage++){
                if((pG5->b.TcgTempMbrL2P[tcgNfHalParams.laas + ipage].blk) >= (TCG_MBR_CELLS)){  //blank ?
                    memset((U8*)tcgNfHalParams.pBuf + (ipage * CFG_UDATA_PER_PAGE) , 0, CFG_UDATA_PER_PAGE);
                }
            }
            return zNG;
        }
    }
    return zOK;
}

//----------------------------------------
// group 5 write (pg->ce->ch->blk->lun)
//----------------------------------------
tcg_code int TCG_G5Wr(void)
{
    tTcgLogAddr  writepnt;
    U32     i;
    int     sts;

    // DBG_P(1, 3, 0x82040A);  //82 04 0A, "[F]TCG_G5Wr"
    //TCGPRN("TCG_G5Wr()\n");
    DBG_P(0x01, 0x03, 0x730021 );  // TCG_G5Wr()
    writepnt.all = 0xFFFFFFFF;
    for(i=0; i<tcgNfHalParams.laacnt; i++)
    {
        while((TcgG5NxtPnt.all == 0xFFFFFFFF) && (G5_FreeBlk > 0))
        {
            // build vac & validmap
            TcgTempMbrBuildGcTables();
            G5_FreeBlk = Tcg_G5FreeBlk(TcgTempMbrCellVac);

            // find a vac0 (excluding defect)
            TcgG5NxtPnt.pc.cell = TcgG5CurPnt.pc.cell;
            TcgG5NxtPnt.pc.page = 0;
            while(1){
                TcgG5NxtPnt.pc.cell++;
                if(TcgG5NxtPnt.pc.cell >= TCG_MBR_CELLS) TcgG5NxtPnt.pc.cell = 0;
                if( (tcgG5Dft[TcgG5NxtPnt.pc.cell] == 0)
                &&(TcgTempMbrCellVac[TcgG5NxtPnt.pc.cell] == 0) )
                {
                    break;
                }
            }
            // open block (write 3 pages)
            if(TCG_G5WrL2p() == zNG){
                TcgG5NxtPnt.all = 0xFFFFFFFF;
            #ifdef TCG_EEP_NOR
                TCG_NorEepWrite();
            #else
                SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
            #endif
            }

        }
        ASSERT(G5_FreeBlk != 0);

        pG5->b.TcgTempMbrL2P[tcgNfHalParams.laas + i].all = WrPntBox[i].all = writepnt.all = TcgG5CurPnt.all = TcgG5NxtPnt.all;
        tcgL2pAssis.g5.tag = L2P_ASS_TAG;
        tcgL2pAssis.g5.tbl_idx = TcgG5NxtPnt.pc.page - L2P_PAGE_CNT;
        tcgL2pAssis.g5.laa_no[tcgL2pAssis.g5.tbl_idx] = tcgNfHalParams.laas + i;
        TcgG5NxtPnt.all += SLC_OFF(1);
        if(TcgG5NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){
            TcgG5NxtPnt.all = 0xFFFFFFFF;
        }
    }

    for(i=0; i<tcgNfHalParams.laacnt; i++){
        FillPaa(i, WrPntBox[i], ZONE_GRP5);
        FillAux(i, tcgNfHalParams.laas + i, TcgG5CurHistNo);
    }

    sts = tcg_hal_write_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf);

    if(sts == zOK){
        if(G5_FreeBlk < TCG_GC_THRESHOLD){   // GC threshold = 3
            G5_GC();
        }
    }
    return sts;
}

//----------------------------------------
// Nor eeprom init
//----------------------------------------
tcg_code void TCG_NorEepInit(void)
{
#if 0
    U32 myTcgEepProgIdx, LastValidEepIdx;
    U32 HasWritten;
    U16 i;

    Core_JobCompleteWait(0xC8);     //HLD_FIO_WaitComplete();         // wait sequencer done
    tcgEepProgIdx = LastValidEepIdx = 0;
    HasWritten = DWORD_MASK;
    for (i = 0; i < SFH_TCG_COUNT; i++)
    {
        HalSflash_Copy((PVOID)(&myTcgEepProgIdx), (SFH_TCG_START + i) * SFLASH_SECTOR_SIZE, sizeof(U32));
        if (myTcgEepProgIdx != DWORD_MASK && myTcgEepProgIdx > tcgEepProgIdx)
        {
            tcgEepProgIdx = LastValidEepIdx = myTcgEepProgIdx;
        }
        HasWritten &= myTcgEepProgIdx;
    }

    if(HasWritten == DWORD_MASK)   //new NOR, first time ?
    {

        if(strcmp((char *)tcgDefectID, DEFECT_STRING) != 0){   // first time ? , if so, then init defect table
            MEM_SET((void *)tcgG4Dft, 0, sizeof(tcgG4Dft));   // force clear G4 defect table
            MEM_SET((void *)tcgG5Dft, 0, sizeof(tcgG5Dft));   // force clear G5 defect table
            //TCG_PRINTF(">>> Defect table is cleared.\n");  //soutb3(0x5C, 0xFE, 0x99);  //5C FE 99,    ">>> Defect table is cleared."
            // DBG_P(1, 3, 0x820413);  //82 04 13, "@@@ Defect table is cleared"
        }

        if(strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0){   // first time ? , if so, then init erased count table
            MEM_SET((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));   // force clear G4 erased count table
            MEM_SET((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));   // force clear G5 erased count table
            //TCG_PRINTF(">>> Erase count table is cleared.\n");  //soutb3(0x5C, 0xFE, 0x9A);  //5C FE 9A,    ">>> Erase count table is cleared."
            // DBG_P(1, 3, 0x820414);  //82 04 14, "@@@ Erase count table is cleared"
        }

        // ID : record defected ID in NOR
        if(strcmp((char *)tcgDefectID, DEFECT_STRING) != 0){   // first time ? , if so, then init defect table
            strcpy((char *)tcgDefectID,DEFECT_STRING);          // ID
        }
        // ID : record erased count ID in NOR
        if(strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0){   // first time ? , if so, then init erased count table
            strcpy((char *)tcgErasedCntID,ERASED_CNT_STRING);   // ID
        }

        TCG_NorEepWrite();
    }else{

        //////////
        U8  FoundLastTagCnt = 0;
        U32 FoundLastTag_Idx = 0xFFFFFFFF;
        for (i = 0; i < SFH_TCG_COUNT; i++){
            HalSflash_Copy((PVOID)(&tcgEepProgIdx), (SFH_TCG_START + i) * SFLASH_SECTOR_SIZE, sizeof(smSysInfo->d.MiscData.d.TCGUsed));
            if (smSysInfo->d.MiscData.d.TCGUsed.TCG_EE_LAST_USED_TAG == TCG_EE_LAST_USED_PATTEN){
                FoundLastTagCnt++;
                FoundLastTag_Idx = (U32)i;
            }
        }

        if(FoundLastTagCnt == 0){
            // DBG_P(1, 3, 0x820031); //82 00 31, "!!!Warn, need to write new tag."
            tcgEepProgIdx = LastValidEepIdx;
            TCG_NorEepRead();
            tcgEepProgIdx++;
            for(i=0; i<(SFH_TCG_COUNT); i++){
                TCG_NorEepWrite();
            }
        } else if((FoundLastTagCnt > 0) && (FoundLastTagCnt < SFH_TCG_COUNT)){  // POR , SPI writting interrupt
            // DBG_P(1, 3, 0x820032); //82 00 32, "!!!Error, TCG EE data is wrong when POR happened."
            HalSflash_Copy((PVOID)(&tcgEepProgIdx), (SFH_TCG_START + FoundLastTag_Idx) * SFLASH_SECTOR_SIZE, sizeof(smSysInfo->d.MiscData.d.TCGUsed));
            tcgEepProgIdx++;
            for(i=0; i<(SFH_TCG_COUNT); i++){
                TCG_NorEepWrite();
            }
        }
        else  //FoundLastTagCnt == SFH_TCG_COUNT
        {
            tcgEepProgIdx = LastValidEepIdx;
            //////////
            TCG_NorEepRead();
            //if reading data is wrong then read last
            if(tcgEepProgIdx != tcgEepProgIdx_tag){
                tcgEepProgIdx--;
                TCG_NorEepRead();
                if(tcgEepProgIdx != tcgEepProgIdx_tag){
                    // DBG_P(3, 3, 0x820016, 4, tcgEepProgIdx, 4, tcgEepProgIdx_tag); //82 00 16, "!!!Error, TCG EERPOM data is wrong.", 4 4
                    // TODO: reading data is wrong again.
                }else{
                    tcgEepProgIdx++;     //read OK then write
                    TCG_NorEepWrite();
                }
            }else{
                tcgEepProgIdx++;
            }
        }
    }
    // tcgEepProgIdx++;  //next write sector index
#endif
}

//----------------------------------------
// Nor eeprom read
//----------------------------------------
tcg_code void TCG_NorEepRead(void)
{
#if 0
    U32 NorSctNo;

    NorSctNo = (tcgEepProgIdx % SFH_TCG_COUNT);

    HalSflash_Copy((PVOID)(&tcgEepProgIdx), (SFH_TCG_START + NorSctNo) * SFLASH_SECTOR_SIZE, sizeof(smSysInfo->d.MiscData.d.TCGUsed));
#endif
}

//----------------------------------------
// Nor eeprom write
//----------------------------------------
tcg_code void TCG_NorEepWrite(void)
{

    SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);

#if 0
    U32 NorSctNo;
    U32 i;

    tcgEepProgIdx_tag = tcgEepProgIdx;
    NorSctNo = (tcgEepProgIdx % SFH_TCG_COUNT);
    smSysInfo->d.MiscData.d.TCGUsed.TCG_EE_LAST_USED_TAG = TCG_EE_LAST_USED_PATTEN;

    HalSflash_EraseSector(SFH_TCG_START + NorSctNo, 1, 3);
    HalSflash_ProgramU32((PVOID)(&tcgEepProgIdx), (SFH_TCG_START + NorSctNo) * SFLASH_SECTOR_SIZE, sizeof(smSysInfo->d.MiscData.d.TCGUsed));

    tcgEepProgIdx++;
    if(tcgEepProgIdx == DWORD_MASK){
        tcgEepProgIdx = 0;
        for(i=0; i<SFH_TCG_COUNT; i++){
            HalSflash_EraseSector(SFH_TCG_START + i, 1, 3);
            HalSflash_ProgramU32((PVOID)(&tcgEepProgIdx), (SFH_TCG_START + i) * SFLASH_SECTOR_SIZE, sizeof(smSysInfo->d.MiscData.d.TCGUsed));
            tcgEepProgIdx++;
        }

    }
#endif

}

#if 0 //cjdbg
//----------------------------------------
// Nor KEK Operation
//----------------------------------------
tcg_data U32 kekHistory;

tcg_code int TCG_NorKekChk(sTcgKekData_Nor *pdata)
{
    U32 CRC;
#if _SIGNPOLICY_ == SIGNPOLICY_DEBUG
    if ((pdata->header.tag      != TCG_DBG_TAG)
    || (pdata->header.version  != TCG_KEK_NOR_VER))
#else
    if ((pdata->header.tag      != TCG_TBL_ID)
    || (pdata->header.version  != TCG_KEK_NOR_VER))
#endif
    {
        return zNG;
    }

    // Check CRC
    CRC = MemCrcU32(DWORD_MASK, pdata, (sizeof(sTcgKekData_Nor) - sizeof(pdata->crc)));
    if (CRC != pdata->crc) return zNG;

    return zOK;
}

tcg_code int TCG_NorKekSync(U8 tcgNorSec)
{
    // DBG_P(1, 3, 0x820452);  //82 04 52, "[F]TCG_NorKekSync"
    // HalSflash_Copy((void*)&mTcgKekDataNor, (SFH_TCG_KEK_START + tcgNorSec) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor));

    if (TCG_NorKekChk((void*)&mTcgKekDataNor)) return zNG;

    SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);
#if 0
    for (U32 i = 0; i < SFH_TCG_KEK_COUNT; i++)
    {
        if (i == tcgNorSec) continue;

        if (HalSflash_EraseSector(SFH_TCG_KEK_START + i, 1, 3)) continue;
        if (HalSflash_Program((void*)&mTcgKekDataNor, (SFH_TCG_KEK_START + i) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor))) continue;
        if (HalSflash_Compare((void*)&mTcgKekDataNor, (SFH_TCG_KEK_START + i) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor))) continue;
    }
#endif
    return zOK;
}

//----------------------------------------
// Nor KEK read
//----------------------------------------
tcg_code int  TCG_NorKekRead(void)
{
#if 0
    U32 i;
    U32 maxHist         = 0;
    U32 minHist         = DWORD_MASK;
    U8  maxSecNo        = 0;
    U8  minSecNo        = 0;
    U8  validSecNo      = 0;
    U8  tcgNorValidCnt  = 0;

    sTcgKekData_Nor *ptmp;

    ptmp = (sTcgKekData_Nor *)tcgTmpBuf;
    memset(ptmp, 0, sizeof(sTcgKekData_Nor));

    for (i = 0; i < SFH_TCG_KEK_COUNT; i++)
    {
        HalSflash_Copy((PVOID)ptmp, (SFH_TCG_KEK_START + i) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor));

        // Check Header
        if (TCG_NorKekChk(ptmp)) continue;

        if (ptmp->header.history > maxHist)
        {
            maxHist = ptmp->header.history;
            maxSecNo = i;
        }
        if (ptmp->header.history < minHist)
        {
            minHist = ptmp->header.history;
            minSecNo = i;
        }

        tcgNorValidCnt++;
    }
    memset(ptmp, 0, sizeof(sTcgKekData_Nor));

    if (!tcgNorValidCnt)
    {
        // DBG_P(1, 3, 0x820453);  //82 04 53, "[F]TCG_NorKekRead Fail!!!"
        kekHistory = 0;
        return zNG;
    }

    validSecNo = ((maxHist - minHist) > 1) ? minSecNo : maxSecNo;

    if ((maxHist != minHist) || (tcgNorValidCnt != SFH_TCG_KEK_COUNT)) TCG_NorKekSync(validSecNo);

    HalSflash_Copy((void*)&mTcgKekDataNor, (SFH_TCG_KEK_START + validSecNo) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor));
    kekHistory = mTcgKekDataNor.header.history;

    // DBG_P(2, 3, 0x820450, 4, kekHistory);   //82 04 50, "[F]TCG_NorKekRead|H=%x", 4
#endif
    return zOK;
}

//----------------------------------------
// Nor KEK write
//----------------------------------------
tcg_code void TCG_NorKekWrite(void)
{
#if _SIGNPOLICY_==SIGNPOLICY_DEBUG
    mTcgKekDataNor.header.tag       = TCG_DBG_TAG;
#else
    mTcgKekDataNor.header.tag       = TCG_TBL_ID;
#endif
    mTcgKekDataNor.header.version   = TCG_KEK_NOR_VER;
    mTcgKekDataNor.header.history   = ++kekHistory;
    mTcgKekDataNor.crc = MemCrcU32(DWORD_MASK, (void*)&mTcgKekDataNor, (sizeof(sTcgKekData_Nor) - sizeof(mTcgKekDataNor.crc)));

    SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);
#if 0
    for (U32 i = 0; i < SFH_TCG_KEK_COUNT; i++)
    {
        if (HalSflash_EraseSector(SFH_TCG_KEK_START + i, 1, 3)) continue;
        if (HalSflash_Program((PVOID)(&mTcgKekDataNor), (SFH_TCG_KEK_START + i) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor))) continue;
        if (HalSflash_Compare((PVOID)(&mTcgKekDataNor), (SFH_TCG_KEK_START + i) * SFLASH_SECTOR_SIZE, sizeof(sTcgKekData_Nor))) continue;
    }
    // DBG_P(2, 3, 0x820451, 4, kekHistory);  //82 04 51, "[F]TCG_NorKekWrite|H=%x" 4
#endif
}

tcg_code void TCG_NorKekWriteDone(void)
{
    if (mTcgKekDataNor.kekInfoNew.tag == ROOTKEY_TAG)
    {
        memcpy(&(mTcgKekDataNor.kekInfoCur), &(mTcgKekDataNor.kekInfoNew), sizeof(sTcgKekInfo));
        MEM_SET(&(mTcgKekDataNor.kekInfoNew), 0, sizeof(sTcgKekInfo));

        TCG_NorKekWrite();
    }
    // DBG_P(1, 3, 0x820454);  //82 04 54, "[F]TCG_NorKekWriteDone"
}
#endif //cjdbg

//----------------------------------------
// group 4 build Vac table
//----------------------------------------
tcg_code void TcgMbrBuildGcTables(void)
{
    U16 laa;
    tTcgLogAddr  ptrk;

    memset(TcgMbrCellVac, 0, sizeof(TcgMbrCellVac));
    memset(TcgMbrCellValidMap, 0, sizeof(TcgMbrCellValidMap));
    for(laa=0; laa<TCG_LAST_USE_LAA /*G4G5_LAA_AMOUNT_LIMIT*/; laa++ ){
        ptrk.all = pG4->b.TcgMbrL2P[laa].all;
        if((ptrk.pc.cell) < (TCG_MBR_CELLS)){
            TcgMbrCellVac[ptrk.pc.cell]++;
            TcgMbrCellValidMap[ptrk.pc.cell][ptrk.pc.page/32] |= mBIT(ptrk.pc.page%32);
        }
    }
}

//----------------------------------------
// group 5 build Vac table
//----------------------------------------
tcg_code void TcgTempMbrBuildGcTables(void)
{
    U16 laa;
    tTcgLogAddr  ptrk;

    memset(TcgTempMbrCellVac, 0, sizeof(TcgTempMbrCellVac));
    memset(TcgTempMbrCellValidMap, 0, sizeof(TcgTempMbrCellValidMap));
    for(laa=0; laa<TCG_LAST_USE_LAA /*G4G5_LAA_AMOUNT_LIMIT*/; laa++ ){
        ptrk.all = pG5->b.TcgTempMbrL2P[laa].all;
        if((ptrk.pc.cell) < (TCG_MBR_CELLS)){
            TcgTempMbrCellVac[ptrk.pc.cell]++;
            TcgTempMbrCellValidMap[ptrk.pc.cell][ptrk.pc.page/32] |= mBIT(ptrk.pc.page%32);
        }
    }
}

//----------------------------------------
// group 4 GC
//----------------------------------------
tcg_code int G4_GC(void)
{
    tTcgLogAddr  writepnt, readpnt;
    U16     MinVac = PAGE_NUM_PER_BLOCK/SLC_OFF(1) - L2P_PAGE_CNT;
    U16     x,y;
    U16     gclaa;
    U32     i;
    // int     sts;

    writepnt.all = readpnt.all = 0xFFFFFFFF;
    for(i = 0; ; i++){
        MinVac = PAGE_NUM_PER_BLOCK / SLC_OFF(1) - L2P_PAGE_CNT;
        TcgMbrBuildGcTables();
        G4_FreeBlk = Tcg_G4FreeBlk(TcgMbrCellVac);
      #if 0
        // gc 1.decide gc block
        y = TcgG4CurPnt.pc.cell;
        for(x=0; ;x++){
            y++;
            if(y >= TCG_MBR_CELLS) y=0;
            if(y == TcgG4CurPnt.pc.cell) continue;
            if(tcgG4Dft[y]) continue;
            if(TcgMbrCellVac[y] < ((x/(PAGE_NUM_PER_BLOCK/4))+1)) break;
        }
      #else
        // gc 1.decide gc block
        y = 0xFFFF;
        for(x = 0; x < TCG_MBR_CELLS; x++){
            if(x == TcgG4CurPnt.pc.cell) continue;
            if(TcgMbrCellVac[x] == 0) continue;
            if(tcgG4Dft[x]) continue;
            if(TcgMbrCellVac[x] < MinVac){
                MinVac = TcgMbrCellVac[x];
                y = x;
            }
        }
      #endif
        //TCG_PRINTF("No=%2X Vac=%X, CurWrBlk%X, GCBlk%X\n",i, TcgMbrCellVac[y], TcgG4CurPnt.pc.cell, y);
        // DBG_P(5, 3, 0x820415, 4, i, 2, TcgMbrCellVac[y], 2, TcgG4CurPnt.pc.cell, 2, y);  //82 04 15, "G4 GC No=%2X Vac=%X, CurWrBlk%X, GCBlk%X", 4 2 2 2

        // gc 2.move all valid page from gc block to the open block
        if(TcgMbrCellVac[y] && (y < TCG_MBR_CELLS)){
            readpnt.pc.cell = y ;
            for(x = SLC_OFF(L2P_PAGE_CNT); x < PAGE_NUM_PER_BLOCK; x += SLC_OFF(1)){
                readpnt.pc.page = x;
                if(TcgMbrCellValidMap[y][x / 32] & (mBIT(x % 32))){
                    TCG_Rd1Pg(readpnt, ZONE_GRP4);

                    gclaa = gTcgAux[0].aux0;
                    if(gclaa >= G4G5_LAA_AMOUNT_LIMIT) continue; // if illegal laa

                    writepnt.all = TcgG4CurPnt.all = TcgG4NxtPnt.all;
                    TcgG4NxtPnt.all += SLC_OFF(1);

                    pG4->b.TcgMbrL2P[gclaa].all = writepnt.all;
                    TCG_Wr1Pg(writepnt, ZONE_GRP4, gclaa, TcgG4NxtHistNo);

                    if(TcgG4NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){   // this Free block is writed complete ?
                        TcgG4NxtPnt.all = 0xFFFFFFFF;
                        return zOK;                  // quit for(;;) loop
                    }
                }
            }
        }else{
            return zOK;
        }
    }

}

//----------------------------------------
// group 5 GC
//----------------------------------------

tcg_code int G5_GC(void)
{
    tTcgLogAddr  writepnt, readpnt;
    U16     MinVac = PAGE_NUM_PER_BLOCK/SLC_OFF(1) - L2P_PAGE_CNT;
    U16     x,y;
    U16     gclaa;
    U32     i;
    // int     sts;

    writepnt.all = readpnt.all = 0xFFFFFFFF;
    for(i = 0; ; i++){
        MinVac = PAGE_NUM_PER_BLOCK / SLC_OFF(1) - L2P_PAGE_CNT;
        TcgTempMbrBuildGcTables();
        G5_FreeBlk = Tcg_G5FreeBlk(TcgTempMbrCellVac);
      #if 0
        // gc 1.decide gc block
        y = TcgG5CurPnt.pc.cell;
        for(x=0; ;x++){
            y++;
            if(y >= TCG_MBR_CELLS) y=0;
            if(y == TcgG5CurPnt.pc.cell) continue;
            if(tcgG5Dft[y]) continue;
            if(TcgTempMbrCellVac[y] < ((x/(PAGE_NUM_PER_BLOCK/4))+1)) break;
        }
      #else
        // gc 1.decide gc block
        y = 0xFFFF;
        for(x = 0; x < TCG_MBR_CELLS; x++){
            if(x == TcgG5CurPnt.pc.cell) continue;
            if(TcgTempMbrCellVac[x] == 0) continue;
            if(tcgG5Dft[x]) continue;
            if(TcgTempMbrCellVac[x] < MinVac){
                MinVac = TcgTempMbrCellVac[x];
                y = x;
            }
        }
      #endif
        //TCG_PRINTF("No=%2X Vac=%X, CurWrBlk%X, GCBlk%X\n",i, TcgTempMbrCellVac[y], TcgG5CurPnt.pc.cell, y);
        // DBG_P(5, 3, 0x820416, 4, i, 2, TcgTempMbrCellVac[y], 2, TcgG5CurPnt.pc.cell, 2, y);  //82 04 16, "G5 GC No=%2X Vac=%X, CurWrBlk%X, GCBlk%X", 4 2 2 2

        // gc 2.move all valid page from gc block to the open block
        if(TcgTempMbrCellVac[y] && (y < TCG_MBR_CELLS)){
            readpnt.pc.cell = y ;
            for(x = SLC_OFF(L2P_PAGE_CNT); x < PAGE_NUM_PER_BLOCK; x += SLC_OFF(1)){
                readpnt.pc.page = x;
                if(TcgTempMbrCellValidMap[y][x / 32] & (mBIT(x % 32))){
                    TCG_Rd1Pg(readpnt, ZONE_GRP5);

                    gclaa = gTcgAux[0].aux0;
                    if(gclaa >= G4G5_LAA_AMOUNT_LIMIT) continue; // if illegal laa

                    writepnt.all = TcgG5CurPnt.all = TcgG5NxtPnt.all;
                    TcgG5NxtPnt.all += SLC_OFF(1);

                    pG5->b.TcgTempMbrL2P[gclaa].all = writepnt.all;
                    TCG_Wr1Pg(writepnt, ZONE_GRP5, gclaa, TcgG5NxtHistNo);

                    if(TcgG5NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){   // this Free block is writed complete ?
                        TcgG5NxtPnt.all = 0xFFFFFFFF;
                        return zOK;                  // quit for(;;) loop
                    }
                }
            }
        }else{
            return zOK;
        }
    }

}

//**************************************************************
// TCG error handling
//**************************************************************
//----------------------------------------
// G5 Replace defect block
//----------------------------------------
tcg_code int G4_RelpaceDefectBlock(U8 DftBlk)
{
    tTcgLogAddr  writepnt, readpnt;
    U16     x;
    U16     gclaa;
    U32     i;
    // U16     RdRtyCnt;

    HERE(NULL);
    // build vac & validmap
    TcgMbrBuildGcTables();
    // find a vac0 (excluding defect)
    TcgG4NxtPnt.pc.cell = DftBlk;
    TcgG4NxtPnt.pc.page = 0;
    for(i = 0; i < TCG_MBR_CELLS; i++){
        TcgG4NxtPnt.pc.cell++;
        if(TcgG4NxtPnt.pc.cell >= TCG_MBR_CELLS) TcgG4NxtPnt.pc.cell = 0;
        if( (tcgG4Dft[TcgG4NxtPnt.pc.cell] == 0)
        &&(TcgMbrCellVac[TcgG4NxtPnt.pc.cell] == 0) )
        {
            break;
        }
    }
    if(i >= TCG_MBR_CELLS)   // no anyone blank block was found ?
        return zNG;

    // open block (write 5 pages)
    TCG_G4WrL2p();

    //TCG_PRINTF("Vac=%X, CurWrBlk%X, DftBlk%X\n", TcgMbrCellVac[DftBlk], TcgG4CurPnt.pc.cell, DftBlk);
    // DBG_P(4, 3, 0x820417, 2, TcgMbrCellVac[DftBlk], 2, TcgG4CurPnt.pc.cell, 1, DftBlk);  //82 04 17, "G4 RDB Vac=%04X, CurWrBlk=%04X, DftBlk=%02X", 2 2 1

    writepnt.all = readpnt.all = 0xFFFFFFFF;

    // gc 2.move all valid page from gc block to the open block
    if(TcgMbrCellVac[DftBlk] && (DftBlk < TCG_MBR_CELLS)){
        readpnt.pc.cell = DftBlk;
        for(x=SLC_OFF(L2P_PAGE_CNT); x<PAGE_NUM_PER_BLOCK; x+=SLC_OFF(1)){
            readpnt.pc.page = x;
            if(TcgMbrCellValidMap[DftBlk][x/32]&(mBIT(x%32))){
                // ----- read -----
                FillPaa(0, readpnt, ZONE_GRP4);

                Core_JobCompleteWait(0xC9);   // wait previous all job down.
                //if(TCG_ReadRetryLaunched(1, OnePgSzBuf) == zNG){
                if(tcg_hal_read_launched(1, OnePgSzBuf) == zNG){
                    return zNG;
                }

                // ----- write -----
                gclaa = gTcgAux[0].aux0;
                if(gclaa >= G4G5_LAA_AMOUNT_LIMIT) continue; // if illegal laa

                writepnt.all = TcgG4CurPnt.all = TcgG4NxtPnt.all;
                TcgG4NxtPnt.all += SLC_OFF(1);

                // DBG_P(4, 3, 0x8204A2, 4, writepnt.all, 2, gclaa, 4, TcgG4NxtHistNo); //82 04 A2, "G4 writepnt.all[%08X] gclaa[%04X] TcgG4NxtHistNo[%08X]", 4 2 4
                pG4->b.TcgMbrL2P[gclaa].all = writepnt.all;
                TCG_Wr1Pg(writepnt, ZONE_GRP4, gclaa, TcgG4NxtHistNo);
            }
        }
        if(TcgG4NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){   // this Free block is writed complete ?
            TcgG4NxtPnt.all = 0xFFFFFFFF;
        }
    }else{
        return zNG;
    }
    return zOK;
}

//----------------------------------------
// G5 Replace defect block
//----------------------------------------
tcg_code int G5_RelpaceDefectBlock(U8 DftBlk)
{
    tTcgLogAddr  writepnt, readpnt;
    U16     x;
    U16     gclaa;
    U32     i;
    // U16     RdRtyCnt;

    HERE(NULL);
    // build vac & validmap
    TcgTempMbrBuildGcTables();
    // find a vac0 (excluding defect)
    TcgG5NxtPnt.pc.cell = DftBlk;
    TcgG5NxtPnt.pc.page = 0;
    for(i = 0; i < TCG_MBR_CELLS; i++){
        TcgG5NxtPnt.pc.cell++;
        if(TcgG5NxtPnt.pc.cell >= TCG_MBR_CELLS) TcgG5NxtPnt.pc.cell = 0;
        if( (tcgG5Dft[TcgG5NxtPnt.pc.cell] == 0)
        &&(TcgTempMbrCellVac[TcgG5NxtPnt.pc.cell] == 0) )
        {
            break;
        }
    }
    if(i >= TCG_MBR_CELLS)   // no anyone blank block was found ?
        return zNG;

    // open block (write 5 pages)
    TCG_G5WrL2p();

    //TCG_PRINTF("Vac=%X, CurWrBlk%X, DftBlk%X\n", TcgTempMbrCellVac[DftBlk], TcgG5CurPnt.pc.cell, DftBlk);
    // DBG_P(4, 3, 0x820418, 2, TcgTempMbrCellVac[DftBlk], 2, TcgG5CurPnt.pc.cell, 1, DftBlk);  //82 04 18, "G5 RDB Vac=%04X, CurWrBlk=%04X, DftBlk=%02X", 2 2 1

    writepnt.all = readpnt.all = 0xFFFFFFFF;

    // gc 2.move all valid page from gc block to the open block
    if(TcgTempMbrCellVac[DftBlk] && (DftBlk < TCG_MBR_CELLS)){
        readpnt.pc.cell = DftBlk;
        for(x=SLC_OFF(L2P_PAGE_CNT); x<PAGE_NUM_PER_BLOCK; x+=SLC_OFF(1)){
            readpnt.pc.page = x;
            if(TcgTempMbrCellValidMap[DftBlk][x/32]&(mBIT(x%32))){
                // ----- read -----
                FillPaa(0, readpnt, ZONE_GRP5);

                Core_JobCompleteWait(0xCA);   // wait previous all job down.
                // if(TCG_ReadRetryLaunched(1, OnePgSzBuf) == zNG){
                if(tcg_hal_read_launched(1, OnePgSzBuf) == zNG){
                    return zNG;
                }

                // ----- write -----
                gclaa = gTcgAux[0].aux0;
                if(gclaa >= G4G5_LAA_AMOUNT_LIMIT) continue; // if illegal laa

                writepnt.all = TcgG5CurPnt.all = TcgG5NxtPnt.all;
                TcgG5NxtPnt.all += SLC_OFF(1);

                // DBG_P(4, 3, 0x8204A3, 4, writepnt.all, 2, gclaa, 4, TcgG4NxtHistNo); //82 04 A3, "G4 writepnt.all[%08X] gclaa[%04X] TcgG4NxtHistNo[%08X]", 4 2 4
                pG5->b.TcgTempMbrL2P[gclaa].all = writepnt.all;
                TCG_Wr1Pg(writepnt, ZONE_GRP5, gclaa, TcgG5NxtHistNo);
            }
        }
        if(TcgG5NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){   // this Free block is writed complete ?
            TcgG5NxtPnt.all = 0xFFFFFFFF;
        }
    }else{
        return zNG;
    }
    return zOK;
}

//----------------------------------------
// TCG error handling
//----------------------------------------
tcg_code int TCG_ErrHandling(tTcgGrpDef grp)
{
    U16 errNativeBlkCnt = 0;
    tTcgLogAddr myRdPnt;
    U16 i;

    HERE(NULL);
    // DBG_P(1, 3, 0x82041F);  //82 04 1F, "TCG_ErrHandling"
    if(TCG_ReadRetry(grp, &errNativeBlkCnt) == zNG) return zNG;
    if(errNativeBlkCnt == 0) return zOK;    // if RetriedVoltage < 4 then don't GC.

    // DBG_P(2, 3, 0x820420, 2, errNativeBlkCnt);  //82 04 20, "=Replace= errNativeBlkCnt[%04X]", 2

    // move defect block away if read retry ok.
    for(i = 0; i < errNativeBlkCnt; i++){
        //tag a defect in defect table
        if(grp > ZONE_DS){
            if(G5_RelpaceDefectBlock(gTcgRdRetry.RdErrNativeBlk[i]) == zOK){   // replace defect block
                tcgG5Dft[ gTcgRdRetry.RdErrNativeBlk[i] ] = 0xDF;
            }else{
                return zNG;
            }
        }else{
            if(G4_RelpaceDefectBlock(gTcgRdRetry.RdErrNativeBlk[i]) == zOK){   // replace defect block
                tcgG4Dft[ gTcgRdRetry.RdErrNativeBlk[i] ] = 0xDF;
            }else{
                return zNG;
            }
        }
    }
  #ifdef TCG_EEP_NOR
    TCG_NorEepWrite();
  #else
    SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
  #endif

    // change read PAA if replace OK. then read again.
    if(grp > ZONE_DS){
        for(i=0; i<tcgNfHalParams.laacnt; i++){
            myRdPnt.all = pG5->b.TcgTempMbrL2P[tcgNfHalParams.laas + i].all;
            FillPaa(i, myRdPnt, ZONE_GRP5);
        }
    }else{
        for(i=0; i<tcgNfHalParams.laacnt; i++){
            myRdPnt.all = pG4->b.TcgMbrL2P[tcgNfHalParams.laas + i].all;
            FillPaa(i, myRdPnt, ZONE_GRP4);
        }
    }

    return(tcg_hal_read_launched(tcgNfHalParams.laacnt, tcgNfHalParams.pBuf));
}

//----------------------------------------
// TCG read retry
//----------------------------------------
tcg_code int TCG_ReadRetry(tTcgGrpDef grp, U16* errNativeBlkCnt)
{
    // tTcgLogAddr myRdPnt;
    U16 errPaaIdx;
    U16 errPaaCnt;
    //U16 errNativeBlkCnt;
    U16 i;
    // U16 RdRtyCnt;
    U8  ErrBlkTag[TCG_MBR_CELLS];
    // int j;

    HERE(NULL);

    *errNativeBlkCnt = 0;
    if (pTcgJob->status & (FIO_S_BLANK_ERR)){
        return zNG;
    }
    Core_JobCompleteWait(0xCB);   // wait previous all job down.
    // if(TCG_ReadRetryLaunched(pTcgJob->paaCnt/PAA_NUM_PER_PAGE, pTcgJob->pBuffer) != zOK){   // previos parameter for read retry
    if(tcg_hal_read_launched(pTcgJob->paaCnt/PAA_NUM_PER_PAGE, pRdLaunchBuf_bak) != zOK){   // previos parameter for read retry
        // DBG_P(2, 3, 0x820419);  //82 04 19, "Read Retry Fail!!!"
        return zNG;
    }

    gTcgRdRetry.PrevPaaCnt = pTcgJob->paaCnt;  // copy paaCnt
    memcpy((U8 *)gTcgRdRetry.PrevPaa, (U8 *)gTcgPaa, pTcgJob->paaCnt * sizeof(tPAA));     // copy previous paa
    memcpy((U8 *)gTcgRdRetry.RdErrMap, (U8 *)&smFioInfo.errPaaMap[pTcgJob->job_id][0], (pTcgJob->paaCnt + 31)/32 * sizeof(U32));    // get error paa bit map
    memset(ErrBlkTag, 0, sizeof(ErrBlkTag));

    memset((U8 *)&smFioInfo.errPaaMap[pTcgJob->job_id][0], 0, (pTcgJob->paaCnt + 31)/32 * sizeof(U32));    // clear error paa bit map
    smFioInfo.errPaaCnt[pTcgJob->job_id] = 0;

    errPaaIdx = 0;
    errPaaCnt = 0;

    // collect every read error page.
    for(errPaaIdx = 0; errPaaIdx < gTcgRdRetry.PrevPaaCnt; errPaaIdx += PAA_NUM_PER_PAGE){
        if( (gTcgRdRetry.RdErrMap[(errPaaIdx  ) >> FIO_ERR_PAA_BMP_SHIFT] & BIT((errPaaIdx  ) & FIO_ERR_PAA_BMP_MASK)) |
            (gTcgRdRetry.RdErrMap[(errPaaIdx+1) >> FIO_ERR_PAA_BMP_SHIFT] & BIT((errPaaIdx+1) & FIO_ERR_PAA_BMP_MASK)) |
            (gTcgRdRetry.RdErrMap[(errPaaIdx+2) >> FIO_ERR_PAA_BMP_SHIFT] & BIT((errPaaIdx+2) & FIO_ERR_PAA_BMP_MASK)) |
            (gTcgRdRetry.RdErrMap[(errPaaIdx+3) >> FIO_ERR_PAA_BMP_SHIFT] & BIT((errPaaIdx+3) & FIO_ERR_PAA_BMP_MASK)) )
        {
            gTcgRdRetry.RdErrNativePaa[errPaaCnt / PAA_NUM_PER_PAGE] = gNativePaa[errPaaIdx / PAA_NUM_PER_PAGE].all;  //get native error block
            ErrBlkTag[gNativePaa[errPaaIdx / PAA_NUM_PER_PAGE].pc.cell] = 0xDF;  // tag defet block

            errPaaCnt += PAA_NUM_PER_PAGE;
        }
    }

    //extract rd err native block and save it to array RdErrNativeBlk[]
    for(i = 0; i < sizeof(ErrBlkTag); i++){
        if(ErrBlkTag[i] == 0xDF){
            gTcgRdRetry.RdErrNativeBlk[*errNativeBlkCnt] = i;
            (*errNativeBlkCnt)++;
        }
    }

    // DBG_P(2, 3, 0x82041A, 2, *errNativeBlkCnt);  //82 04 1A, ">>Rd Retry OK, voltage[%04X]", 2
    return zOK;
}

//===================================================
//  U16 Tcg_G4FreeBlk(U16 *Vac)
//===================================================
tcg_code U16 Tcg_G4FreeBlk(U16 *Vac)
{
    U16 i;
    U16 FreeBlkCnt = 0;
    for(i = 0; i < TCG_MBR_CELLS; i++){
        if((!Vac[i]) && (!tcgG4Dft[i])){
            FreeBlkCnt++;
        }
    }
    return FreeBlkCnt;
}

//===================================================
//  U16 Tcg_G5FreeBlk(U16 *Vac)
//===================================================
tcg_code U16 Tcg_G5FreeBlk(U16 *Vac)
{
    U16 i;
    U16 FreeBlkCnt = 0;
    for(i = 0; i < TCG_MBR_CELLS; i++){
        if((!Vac[i]) && (!tcgG5Dft[i])){
            FreeBlkCnt++;
        }
    }
    return FreeBlkCnt;
}

//===================================================
// FillFragInPage()
//===================================================
tcg_code void FillFragInPage(tPAA *pPaa)
{
    pPaa->b.frag = 0;
    pPaa++;
    pPaa->b.frag = 1;
    pPaa++;
    pPaa->b.frag = 2;
    pPaa++;
    pPaa->b.frag = 3;
}

//===================================================
//  FillPaa()
//===================================================
tcg_code void FillPaa(U16 ipage, tTcgLogAddr writepnt, tTcgGrpDef grp)
{
    //gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].all = CnvAdrLog2Phy(writepnt, grp);
    CnvAdrLog2Phy(writepnt, grp, gTcgPaa+(ipage*PAA_NUM_PER_PAGE+0));
    COPY2NEXT4(gTcgPaa, ipage*PAA_NUM_PER_PAGE+0);
    FillFragInPage(&gTcgPaa[ipage*PAA_NUM_PER_PAGE+0]);

  #if 0
    TCG_PRINTF("LogAdr[%2X]=%4X , GRP=%2X, PAA :ch[%2x] ce[%2x] lun[%2x] blk[%4x] pln[%2x] frg[%2x] pg[%4x]\n", \
        ipage, writepnt.all, grp,
        gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.ch, gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.ce, \
        gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.lun, gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.block, \
        gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.plane, gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.frag, \
        gTcgPaa[ipage*PAA_NUM_PER_PAGE+0].b.page);
  #endif
}

//===================================================
//  FillAux()
//===================================================
tcg_code void FillAux(U16 ipage, U32 aux0, U32 aux1)
{
    // tPAA mypaa;
    // U16 blk;
    // U32 die;

    gTcgAux[ipage*PAA_NUM_PER_PAGE+0].aux0 = aux0;
    gTcgAux[ipage*PAA_NUM_PER_PAGE+0].aux1 = aux1;

    gTcgAux[ipage*PAA_NUM_PER_PAGE+0].aux2 = 0x23544347;  // "#TCG"  alexcheck, The future can be filled with useful information for this field.
    gTcgAux[ipage*PAA_NUM_PER_PAGE+0].aux3 = 0x00000000;  // Normal code NCL guy require AUX3 should be 0.
    COPY2NEXT4(gTcgAux, ipage*PAA_NUM_PER_PAGE+0);
}

//===================================================
//  CnvAdrLog2Phy()
//===================================================
tcg_code void CnvAdrLog2Phy(tTcgLogAddr logadr, tTcgGrpDef grp, tPAA *mypaa)
{
    if(grp > ZONE_DS){
        mypaa->all32   = DR_G5PaaBuf[logadr.pc.cell].all32;
    }else{
        mypaa->all32   = DR_G4PaaBuf[logadr.pc.cell].all32;
    }
    mypaa->b.WLpage    = logadr.pc.page;

    if ((logadr.pc.cell >= TCG_MBR_CELLS) || (logadr.pc.page >= PAGE_NUM_PER_BLOCK))
    {
        //82 04 80, "logadr[%08X] myPaa[%08X] WLpg[%04X] CE[%02X] CH[%02X] LUN[%02X] PLN[%02X] BLK[%04X]", 4 4 2 1 1 1 1 2
        // DBG_P(9, 3, 0x820480, 4, logadr.all, 4, mypaa->all32, 2, mypaa->b.WLpage, 1, mypaa->b.ce, 1, mypaa->b.ch, 1, mypaa->b.lun, 1, mypaa->b.plane, 2 , mypaa->b.block);
        // #if (DATA_ABNORMAL_SKIP_ASSERT == FALSE)
        // ASSERT(FALSE);
        // #else
        Core_UpdateCoreDebugTable(mypaa->b.block, TCG_ABNORMAL_PAA_CnvAdrLog2Phy, TcgG4NxtHistNo);
        // #endif
    }


}

#if TCG_TBL_HISTORY_DESTORY
//===================================================
//  TCG_G4HistoryTbl_Destroy()
//===================================================
tcg_code int TCG_G4HistoryTbl_Destroy(void)
{
    tTcgLogAddr erasepnt;
    int st;
    U16 i, LaaMax;

    erasepnt.all = 0;
    erasepnt.pc.cell = TcgG4CurPnt.pc.cell;
    do{
        erasepnt.pc.cell++;
        if(erasepnt.pc.cell >= TCG_MBR_CELLS) erasepnt.pc.cell = 0;
        if( (tcgG4Dft[erasepnt.pc.cell] == 0) && (TcgMbrCellVac[erasepnt.pc.cell] != 0) )  //
        {
            FillPaa(0, erasepnt, ZONE_GRP4);
            if((st = tcg_hal_erase_launched(1)) == zNG){
                tcgG4Dft[erasepnt.pc.cell] = 0xDF;   //set defect flag
            }
            tcgG4EraCnt[erasepnt.pc.cell]++;
          #ifdef TCG_EEP_NOR
            TCG_NorEepWrite();
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif

            LaaMax = TCG_MBR_CELLS * PAGE_NUM_PER_BLOCK;
            for(i=0; i<LaaMax; i++){
                if((pG4->b.TcgMbrL2P[i].blk) == (erasepnt.pc.cell)){
                    pG4->b.TcgMbrL2P[i].all = 0xFFFFFFFF;
                }
            }

        }

    }while(erasepnt.pc.cell != TcgG4CurPnt.pc.cell);
    return zOK;
}

//===================================================
//  TCG_G5HistoryTbl_Destroy()
//===================================================
tcg_code int TCG_G5HistoryTbl_Destroy(void)
{
    tTcgLogAddr erasepnt;
    int st;
    U16 i, LaaMax;

    erasepnt.all = 0;
    erasepnt.pc.cell = TcgG5CurPnt.pc.cell;
    do{
        erasepnt.pc.cell++;
        if(erasepnt.pc.cell >= TCG_MBR_CELLS) erasepnt.pc.cell = 0;
        if( (tcgG5Dft[erasepnt.pc.cell] == 0) && (TcgTempMbrCellVac[erasepnt.pc.cell] != 0) )  //
        {
            FillPaa(0, erasepnt, ZONE_GRP5);
            if((st = tcg_hal_erase_launched(1)) == zNG){
                tcgG5Dft[erasepnt.pc.cell] = 0xDF;   //set defect flag
            }
            tcgG5EraCnt[erasepnt.pc.cell]++;
          #ifdef TCG_EEP_NOR
            TCG_NorEepWrite();
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif

            LaaMax = TCG_MBR_CELLS * PAGE_NUM_PER_BLOCK;
            for(i=0; i<LaaMax; i++){
                if((pG5->b.TcgTempMbrL2P[i].blk) == (erasepnt.pc.cell)){
                    pG5->b.TcgTempMbrL2P[i].all = 0xFFFFFFFF;
                }
            }

        }

    }while(erasepnt.pc.cell != TcgG5CurPnt.pc.cell);
    return zOK;
}

#endif


//-------------------------------------------------------------------
// Function     : static void ERR_HANDLE_ReadRetryTrkDone(U32 trkId, PVOID pStatus)
// Description  : Error handler trake done
// Input        : N/A
// return       : N/A
//-------------------------------------------------------------------
#if 0
// this function will not be used
tcg_code static void TCG_ERR_HANDLE_ReadRetryTrkDone(U32 trkId, PVOID pStatus)
{
  #ifdef _ALEX_VER_OK
    tFIO_JOB* pJob = gFioInfo.tid2Job[trkId];

    pJob->status |= FIO_S_COMPLETE;

    gFioInfo.jobDoneCnt++;

    DESC_FIFO_UPDATE_R_PTR(pJob->jobDescCnt);
  #endif
}
#endif
//-------------------------------------------------------------------
// Function     : static void AGING_JobErrorHandle(tFIO_JOB* pJob)
// Description  :
// Input        : N/A
// return       : N/A
//-------------------------------------------------------------------
tcg_code void TCG_JobErrorHandle(tFIO_JOB* pJob)
{
  #ifdef _ALEX_VER_OK
    tPAA errPaa;
    U16  cnt;

    for (cnt = 0; cnt < (pJob->paaCnt); cnt++)
    {
        if (FIO_CHK_ERR_BMP(pJob->fid, cnt))
        {
            errPaa = pJob->paa[cnt];
            AGING_ErrBlk_Record(errPaa, pJob->cmd, pJob->status);
            FIO_CLR_ERR_BMP(pJob->fid, cnt);
        }
    }
  #endif
}

#if 0
//-------------------------------------------------------------------
// Function     : static void TCG_JobCompleteHandle(void)
// Description  :
// Input        : N/A
// return       : N/A
//-------------------------------------------------------------------
tcg_code void TCG_JobCompleteHandle(void)
{
    tFIO_JOB* pJob = NULL;

    if (IPC_MsgQFastPeek(cM2cComQueue))
    {
        pJob   = (tFIO_JOB*) IPC_GetMsgQ(cM2cComQueue);
        TCG_JobErrorHandle(pJob);

        gCbfCacheTrkDone[pJob->css](pJob);
        FIO_PushJob(FIO_JOB_POOL_RW_FREE, pJob);
        smFioInfo.jobDoneCnt++;
    }
}

//-------------------------------------------------------------------
// Function     : static void TCG_JobWaitComplete(void)
// Description  :
// Input        : N/A
// return       : N/A
//-------------------------------------------------------------------
tcg_code void TCG_JobWaitComplete(void)
{
    U32   timeout = 0;

    while (FIO_CHK_HAVE_BUSY_JOB())
    {
        TCG_JobCompleteHandle();

        timeout++;
        ASSERT(timeout < 0xF0000000);
    }
}
#endif // Jack Li

#endif

