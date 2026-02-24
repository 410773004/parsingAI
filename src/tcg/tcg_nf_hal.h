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
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------
#define L2P_PAGE_CNT                3

#define MAX_DESC_PER_TRK            64  // tcgTempBuf == 256K , 4K * 64 = 256K

#define MAX_DEVICES_NUM_PER_ROW     (8 * 4)   // 8 ch, 4 ce
// #define DEVICES_NUM_PER_ROW         (gNfInfo_CPU2.numChannels * gNfInfo_CPU2.devicesPerChannel)
#define DEVICES_NUM_PER_ROW         (smNandInfo.geo.nr_channels * smNandInfo.geo.nr_targets)
// #define BLKS_NUM_PER_SUBLK          (gNfInfo_CPU2.planesPerLun * gNfInfo_CPU2.lunsPerDevice * DEVICES_NUM_PER_ROW)
#define BLKS_NUM_PER_SUBLK          (smNandInfo.geo.nr_planes * smNandInfo.geo.nr_luns * DEVICES_NUM_PER_ROW)
#define G4_SUBLK_NO                 0
#define G5_SUBLK_NO                 (TCG_MBR_CELLS / BLKS_NUM_PER_SUBLK)

#define PAA_NUM_PER_PAGE            4
#define PAA_NUM_PER_ROW             (DEVICES_NUM_PER_ROW * PAA_NUM_PER_PAGE)     // DEVICES_NUM_PER_ROW * 4
#define PAGE_NUM_PER_DESTRACK       (MAX_DESC_PER_TRK / PAA_NUM_PER_PAGE)

#define MAX_PAGE_PER_TRK            ((MAX_DESC_PER_TRK / PAA_NUM_PER_PAGE) > 32 ? 32 : (MAX_DESC_PER_TRK / PAA_NUM_PER_PAGE))


#define TCG_G4_TAG                  0x00434734
#define TCG_G5_TAG                  0x00434735


// #define PAGE_NUM_PER_BLOCK          (NF_PAGES_PER_BLOCK - 1) /* gNfInfo_CPU2.pagesPerBlock //this is 384 not 256 at TLC */
// #define PAGE_NUM_PER_BLOCK          smNandInfo.geo.nr_pages
#define PAGE_NUM_PER_BLOCK          384

#define TCG_GC_THRESHOLD            3

//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define VAR_SFT_R_BITS(var, shtbits)    (var >> shtbits)
#define BITS_MASK(b)        (~(U32_MAX << b))
#define BITS_CAP(a, b)      (a & BITS_MASK(b))
#define BITS2VAR(a, b, c)   (a = BITS_CAP(b, c))

#define SLC_OFF(a)          (a) //(a << 1)    // SLC offset

#define COPY2NEXT4(a, b)    a[b+3] = a[b+2] = a[b+1] = a[b+0]

#ifdef _ALEXDEBUG_PAA
#define dumpPaa(pagecnts)   { \
                                int i,j; \
                                for(i=0; i <pagecnts * PAA_NUM_PER_PAGE; i+=PAA_NUM_PER_PAGE){ \
                                    for(j=0; j<PAA_NUM_PER_PAGE; j++){ \
                                        TCG_PRINTF("PAA :ch[%2x] ce[%2x] lun[%2x] blk[%4x] pln[%2x] frg[%2x] pg[%4x]\n", gTcgPaa[i+j].b.ch, gTcgPaa[i+j].b.ce, gTcgPaa[i+j].b.lun, gTcgPaa[i+j].b.block, gTcgPaa[i+j].b.plane, gTcgPaa[i+j].b.frag, gTcgPaa[i+j].b.page);\
                                    } \
                                } \
                            }
#else
#define dumpPaa(pagecnts)   {}
#endif


//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
/// @TcgAddr info

typedef struct
{
    U32 aux0;
    U32 aux1;
    U32 aux2;
    U32 aux3;
}tTcgAux;


typedef struct
{
    U16     PrevPaaCnt;   // Previous Paa Cnt
    tPAA PrevPaa[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */ * PAA_NUM_PER_PAGE];  // Previous Paa, 4 paa per page
    U32     RdErrMap[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */ * PAA_NUM_PER_PAGE / 32];  // read error bits mapping
    U16     RdErrNativePaa[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */];
    U8      RdErrNativeBlk[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */];
}tTcgRdRetry;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
extern tTcgAux      gTcgAux[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */ * PAA_NUM_PER_PAGE];
extern tTcgLogAddr  WrPntBox[MAX_DEVICES_NUM_PER_ROW  /* MAX_PAGE_PER_TRK */];
extern tTcgLogAddr  TcgG4Pnt, TcgG5Pnt;
extern tTcgLogAddr  TcgG4CurPnt, TcgG4NxtPnt;
extern tTcgLogAddr  TcgG5CurPnt, TcgG5NxtPnt;
extern U32          TcgG4CurHistNo, TcgG4NxtHistNo;
extern U32          TcgG5CurHistNo, TcgG5NxtHistNo;
extern U8           TcgG4Defects, TcgG5Defects;
extern U16          ValidBlks[32];
extern U16          ValidCnt;
extern tMSG_HOST    *pHcmdMsg_cpy;
extern U16          G4_FreeBlk;
extern U16          G5_FreeBlk;

//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Imported function proto-type without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------
int tcg_hal_read_launched(U16 pagecnts, U8 *pBuffer);
int tcg_hal_write_launched(U16 pagecnts, U8 *pBuffer);
int tcg_hal_erase_launched(U16 blkcnts);
int TCG_G4Wr(void);
int TCG_G4WrL2p(void);
int TCG_G5Wr(void);
int TCG_G5WrL2p(void);

//-----------------------------------------------------------------------------
//  Public function proto-type definitions:
//-----------------------------------------------------------------------------
int  TCG_G4Rd(void);
int  TCG_G5Rd(void);
int  TCG_G4DmyRd(void);
int  TCG_G5DmyRd(void);
int  TCG_Rd1Pg(tTcgLogAddr readpnt, tTcgGrpDef grp);
int  TCG_Wr1Pg(tTcgLogAddr writepnt, tTcgGrpDef grp, U32 Aux0, U32 Aux1);
int  TCG_G4RdL2p(void);
int  TCG_G5RdL2p(void);
int  TCG_RdMulChan(tTcgGrpDef grp, tTcgLogAddr ReadPnt, U32 SkipChanMap);
int  TCG_WrMulChan(tTcgGrpDef grp, tTcgLogAddr WritePnt, U32 SkipChanMap);
int  TCG_ErMulChan(tTcgGrpDef grp, tTcgLogAddr ErasePnt, U32 SkipChanMap);
void TcgMbrBuildGcTables(void);
void TcgTempMbrBuildGcTables(void);
int  G4_GC(void);
int  G5_GC(void);
void TCG_NorEepInit(void);
void TCG_NorEepRead(void);
void TCG_NorEepWrite(void);
int  TCG_G4HistoryTbl_Destroy(void);
int  TCG_G5HistoryTbl_Destroy(void);
void TCG_JobErrorHandle(tFIO_JOB* pJob);
void TCG_JobCompleteHandle(void);
int TCG_Zone51Rd1Pg(tPAA paa);
int TCG_RdZone51(tPAA paa, void* buf);
int TCG_WrZone51(tPAA paa, void* buf);
int TCG_EraseZone51(tPAA paa);
//void TCG_JobWaitComplete(void);
#if 0
int  TcgInit_NorKek(void);
int  TCG_NorKekSync(U8 TcgNorSec);
int  TCG_NorKekRead(void);
void TCG_NorKekWrite(void);
void TCG_NorKekWriteDone(void);
#endif
