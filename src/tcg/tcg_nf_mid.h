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

#include "rainier_soc.h"
#include "./inc/ficu_reg_access.h"
#include "bf_mgr.h"
#include "ncl.h"

//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------
#define TCG_SMBR_LAA_START          0x0000  // size = 0x2000  pages  = 0x8000000 bytes(128MB)
#define TCG_SMBR_LAA_END            0x1FFF  // size = 0x2000  pages  = 0x8000000 bytes(128MB)

#define TCG_G1_LAA0                 0x2000  // 1 laa = 16K for G1
#define TCG_G2_LAA0                 0x2001  // 2 laa = 32K for G2
#define TCG_G2_LAA1                 0x2002  //
#define TCG_G3_LAA0                 0x2003  // 2 laa = 32K for G3
#define TCG_G3_LAA1                 0x2004  //

#define TCG_DS_LAA_START            0x2005  // size = 0x280  pages  = 0xA00000 bytes(10MB)
#define TCG_DS_LAA_END              0x228C
#define TCG_DUMMY_LAA               0x228D  // this is for  TcgTempMbrClean()
#define TCG_DS_DUMMY_LAA            0x228E  // this is for  TcgTempDSClean()

#define TCG_G1_DEFAULT_LAA0         0x228F
#define TCG_G2_DEFAULT_LAA0         0x2290
#define TCG_G2_DEFAULT_LAA1         0x2291
#define TCG_G3_DEFAULT_LAA0         0x2292
#define TCG_G3_DEFAULT_LAA1         0x2293
#define TCG_LAST_USE_LAA            (TCG_G3_DEFAULT_LAA1 + 1)
#define TCG_TOTAL_UESD_LAA          (TCG_LAST_USE_LAA + TCG_LAST_USE_LAA)

#define TCG_G5_LAA_BASE             TCG_LAST_USE_LAA
#define TCG_ALL_LAA_CNT				(TCG_G5_LAA_BASE + TCG_LAST_USE_LAA)

#if 1
#define TCG_SPB_ALLOCED             ((shr_nand_info.interleave<32)?6:3)
#define TCG_SPB_ID_END              (shr_nand_info.geo.nr_blocks - 1)
#define TCG_SPB_ID_START            (shr_nand_info.geo.nr_blocks - TCG_SPB_ALLOCED)
#else
#define TCG_SPB_ID_START            828
#define TCG_SPB_ID_END              830  // maybe defect half or all
#define TCG_SPB_ALLOCED             (TCG_SPB_ID_END - TCG_SPB_ID_START + 1)
#endif

#define TCG_GC_THRESHOLD             5
#define TCG_GC_DONE_THRESHOLD       20
#define TCG_BAD_BLK_THRESHOLD       58

#define TCG_INIT_TAG                0x2DEF2DEF  // init 2 DEFault already
#define TCG_PFMT_TAG                0x544D4650  // Preformatting (in progress)
#define TCG_TAG            			0x54434740  //"TCG@"
#define TCG_L2P_TAG         		0x4C325041  // "L2PA"
#define NONTCG_TAG                  0x58544347  //"XTCG"

#define DU_CNT_PER_TCG_FLUSH        8           //du flush

#define TCG_BE_BUF_SIZE             0x14000     // 80KB

//#define TCG_CMT_L2P_UPDT

typedef struct _tcg_io_res_t
{
	struct ncl_cmd_t ncl_cmd;                             ///< NCL command resource
	struct info_param_t info_list[DU_CNT_PER_TCG_FLUSH];  ///< information list for NCL command
	bm_pl_t bm_pl_list[DU_CNT_PER_TCG_FLUSH];
} tcg_io_res_t;

typedef struct
{
	enum ncl_cmd_op_t  op;
	u8                 sync;
	u8                 updt_l2p_wr;
    u16                laas;
    u16                laacnt;
	u32                result;
	u32                trnsctn_sts;
	union
	{
		// for write,    Dtag -> buffer -> NAND
    	bm_pl_t* bm_pl;
		
		// for read,     NAND -> buffer -> target
		void*    target;
		
	}mem;
} PACKED tcg_nf_params_t;

typedef struct
{
	//u32 init_tag;
	u32 history_no;
	u16 vac[384];           // SPB allocated: 2.5(alloc: 3), Block cnt in 1 SPB: 32/64/128 (480/1T/2T)
	u32 blk_erased[12];     // 2.5(alloc: 3) * (32/64/128) = (96/192/384) bits = (3/6/12) in double word
	u32 defect_map[12];
	u32 l2p_tbl[8852+8852]; // 8852  *2 *4 = 70,816 = 4.x*16k
	u32 trsac_bitmap[277];  // 8852  /32   = 276.x
} PACKED tcg_mid_info_t;

typedef union
{
	struct {
		u32 tag;
		u32 laa;
		u32 hlba_l;
		u32 hlba_h;
		u32 rsvd[META_SIZE/sizeof(u32)-1-1-1-1];
	}tcg_data;

	struct {
		u32 tag;
		u32 history_no;
		u32 rsvd[META_SIZE/sizeof(u32)-1-1];
	}tcg_l2p;
	
	u32 all[META_SIZE/sizeof(u32)];
} PACKED tcg_du_meta_fmt_t;

typedef struct {
	u64 slda;
	u32 du_cnt;
	u16 nvm_cmd_id;
	u16 btag;
} PACKED mbr_rd_payload_t;

extern void tcg_nf_Start(tcg_nf_params_t *param);

/*
#else // Jack Li

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------
// #define TCG_SMBR_LAA_START          0x0000  // size = 0x2000  pages  = 0x8000000 bytes(128MB)
// #define TCG_SMBR_LAA_END            0x1FFF  // size = 0x2000  pages  = 0x8000000 bytes(128MB)

// #define TCG_G1_LAA0                 0x2000  // 1 laa = 16K for G1
// #define TCG_G2_LAA0                 0x2001  // 2 laa = 32K for G2
// #define TCG_G2_LAA1                 0x2002  //
// #define TCG_G3_LAA0                 0x2003  // 2 laa = 32K for G3
// #define TCG_G3_LAA1                 0x2004  //

// #define TCG_DS_LAA_START            0x2005  // size = 0x280  pages  = 0xA00000 bytes(10MB)
// #define TCG_DS_LAA_END              0x228C
// #define TCG_DUMMY_LAA               0x228D  // this is for  TcgTempMbrClean()
// #define TCG_DS_DUMMY_LAA            0x228E  // this is for  TcgTempDSClean()

// #define TCG_G1_DEFAULT_LAA0         0x228F
// #define TCG_G2_DEFAULT_LAA0         0x2290
// #define TCG_G2_DEFAULT_LAA1         0x2291
// #define TCG_G3_DEFAULT_LAA0         0x2292
// #define TCG_G3_DEFAULT_LAA1         0x2293
// #define TCG_LAST_USE_LAA            TCG_G3_DEFAULT_LAA1 + 1

#define ZONE51_BLANK_TAG                0x424C4E4B
//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#ifdef alexcheck
#define HAL_MRE_SecCopy(a, b, c, d, e)  {}
#endif
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef enum{
    ZONE_GRP1,
    ZONE_GRP2,
    ZONE_GRP3,
    ZONE_GRP4,
    ZONE_SMBR,
    ZONE_DS,
    ZONE_TGRP1,
    ZONE_TGRP2,
    ZONE_TGRP3,
    ZONE_GRP5,
    ZONE_TDS,
    ZONE_TSMBR,
    ZONE_LAST
}tTcgGrpDef;


typedef struct
{
    U16             laas;
    U16             laae;
    U16             laacnt;
    PVOID           pBuf;
    U32             aux0;
    U32             aux1;
    tTcgGrpDef      grp;
} tcgNfHalParams_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------

extern tcgLaa_t             *pMBR_TEMPL2PTBL;
extern tcgLaa_t             *pDS_TEMPL2PTBL;
extern U32                  TCG_FIO_F_MUTE;
// extern sTcgKekData_Nor      mTcgKekDataNor;

//-----------------------------------------------------------------------------
//  Imported function proto-type without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------
#ifdef __TCG_NF_MID
int  tcg_nf_Start(req_t*);
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
static int  tcg_nf_G4FTL(req_t*);           // 0x1D
static int  tcg_nf_G5FTL(req_t*);           // 0x1E
static int  tcg_nf_InitCache(req_t*);       // 0x1F
static int  tcg_nf_ClrCache(req_t*);        // 0x20
static int  tcg_nf_NorEepInit(req_t*);      // 0x21
static int  tcg_nf_NorEepRd(req_t*);        // 0x22
static int  tcg_nf_NorEepWr(req_t*);        // 0x23
static int  tcg_nf_NfCpuInit(req_t*);       // 0x24
static int  tcg_nf_syncZone51Media(req_t*); // 0x25
static int  tcg_secure_boot_enable(req_t*); // 0x26
static int  tcg_nf_tbl_recovery(req_t*);    // 0x27
static int  tcg_nf_tbl_update(req_t*);      // 0x28
static int  tcg_nf_G3Wr_syncZone51(req_t*); // 0x29
#endif

int G4RdRetry(void);
int G5RdRetry(void);

//-----------------------------------------------------------------------------
//  Public function proto-type definitions:
//-----------------------------------------------------------------------------
void tcg_nf_mid_params_set(U16 laas, U16 laae, PVOID Pbuf);

int save_l2pTblAssis(void);
int  tcgCpu4_oneTimeInit(InitBootMode_t initMode);
int  tcgCpu4_init(bool mRst, bool bClearCache);

void tcg_nf_sync_clear(void);
void tcg_nf_post_sync_request(void);
void tcg_nf_post_sync_response(void);
int  Init_Zero_Pattern_Cache(void);


U32  IsG1Blank(void);
U32  IsG2Blank(void);
U32  IsG3Blank(void);
U32  IsDSBlank(void);
U32  IsG1G2G3DSAllBlank(void);
U32  IsG1G2G3OneOfBlank(void);
int  TCG_G4_NewTable(req_t*);
int  TCG_G5_NewTable(req_t*);

int  TCG_BuildTable(req_t*);

void TcgGetKEK(void);
void CPU4_changeKey(U8 rangeNo);
void CPU4_chg_cbc_tbl_key(void);
void CPU4_chg_ebc_key_key(void);
void CPU4_chg_cbc_fwImage_key(void);
int  Build_DefectRemovedPaaMappingTable(void);

int BackwardSearch_FTL(U32 tgtPg, tTcgLogAddr curPosIdx, tTcgGrpDef grp);
int chkDefaultTblPattern_cpu4(req_t*);
int chk_tbl_pattern(req_t*);
int update_zone51_buffer(void);
int sync_zone51_media(void);
int new_zone51(void);
int check_zone51_blank();

void dump_G4_erased_count(void);
void dump_G5_erased_count(void);
extern void DumpTcgKeyInfo(void);
extern void DumpRangeInfo(void);

#ifdef __TCG_NF_MID
//-----------------------------------------------------------------------------
//  inline function (inline, inline, inline)
//-----------------------------------------------------------------------------
extern tcgNfHalParams_t tcgNfHalParams;
extern void tcg_nf_mid_params_set(U16 laas, U16 laae, PVOID Pbuf);
#endif

extern int hal_crypto(U8 *des, U8 *src, U8 *kek, bool cryptoMode, U32 srcLen);
*/
#endif // Jack Li
