//============================================================================//
/*
 *  Header files
 */
//============================================================================//
#ifndef     _GLIST_H_INC
  #define   _GLIST_H_INC

#include "types.h"
#include "cpu_msg.h"
#include "sect.h"
#include "epm.h"
#include "mpc.h"
#ifdef   INSTANTIATE_GLIST
#define EXTERN 
#else
#define EXTERN extern
#endif



//============================================================================//
/*
 *  Variable/ feature definition
 */
//============================================================================//
#define ERRHANDLE_GLIST
//#define ERR_PRINT_LOG
//#define ERR_PROGRAM_FAIL
//#define ERR_GC_PRO_FAIL
//#define FINST_ERR_DBG    							// DBG_EH, mark for finst error, Paul_20201214
#define ERRHANDLE_VERIFY							// VSC for Tencent, FET, RelsP2AndGL
#define EH_FORCE_CLOSE_SPB 						// ISU, LJ1-337, PgFalClsNotDone (1)
//#define EH_GCOPEN_HALT_DATA_IN					// For EH_FORCE_CLOSE_SPB, tmp define for debug, Skip suspend gc, directly bypass gc data in ftl_core_gc_data_in, ISU, LJ1-337, PgFalClsNotDone (1)
//#define UNC_MARK_BAD_VERIFY

#define GL_1BREAD_FAIL              1				//retry fail
#define GL_2BREAD_FAIL              2				//2B retry fail
#define GL_BLANK_READ_FAIL          3				//read blank not retry
#define GL_OVER_LIMIT               4				//ECC over limit
#define GL_MCRC_ERROR               5				//after LDPC confirm

#define GL_GC_1BREAD_FAIL           6
#define GL_GC_2BREAD_FAIL           7
#define GL_GC_BLANK_READ_FAIL       8
#define GL_GC_OVER_LIMIT            9
#define GL_GC_MCRC_ERROR            10

#define GL_FTL_1BREAD_FAIL          11
#define GL_FTL_2BREAD_FAIL          12
#define GL_FTL_BLANK_READ_FAIL      13
#define GL_FTL_OVER_LIMIT           14
#define GL_FTL_MCRC_ERROR           15

#define GL_RECV_FAIL                16
#define GL_RECV_1BREAD_FAIL         17
#define GL_RECV_2BREAD_FAIL         18
#define GL_RECV_BLANK_READ_FAIL     19
#define GL_RECV_OVER_LIMIT          20
#define GL_RECV_MCRC_ERROR          21
#define GL_RECV_NO_ARD_ERROR        22

#define GL_PROG_FAIL                23
#define GL_ERASE_FAIL               24
#define GL_MANUAL_ERROR             25		    // UART test
#define GL_UNKNOWN_ERROR            26

#define GL_P2L_1BREAD_FAIL          27
#define GL_P2L_2BREAD_FAIL          28
#define GL_P2L_BLANK_READ_FAIL      29
#define GL_P2L_OVER_LIMIT           30
#define GL_P2L_MCRC_ERROR           31

#define GLERR_NO_ERROR              0
#define GLERR_PARAM_OVERFLOW        1
#define GLERR_NOT_FIND              2


#define GL_GLIST_TAG                0x54534C47  // 'GLST'
#define GLIST_AREA                  0x00000010
#define GLIST_TAG                   0x42544c47  // "GLTB"
#define GLIST_VER			        0x01

#define GLIST_EPM_NEED_DTAG         16			// GLIST entry cnt * single entry size = 64k
#define GL_TOTAL_ENTRY_CNT          1555
#define GL_TOTAL_ECCT_ENTRY_CNT     500

typedef struct
{
    // LBlk_Info
    u32 dLBlk_SN;
    u32 dLBlk_flags;                //Flg-SWFlg
    u32 dLBlk_POH;
    u32 dLBlk_EC;
    u32 dLBlk_RdCnt;
    u16 wLBlk_Idx;
    
    // tAnaly_Info
    u16 wPhyBlk;
    u16 wPage;
    u8  bError_Type;
    u8  bTemper;
    u8  bDie;
    
    u8  AU              : 3;
    u8  RD_RecvFail     : 1;		//same phy blk for layer raid
    u8  RD_2BFail       : 1;		//2B fail
    u8	RD_Ftl			: 1;        //FTL block
    u8  CPU4_Flag       : 1;        //mark cpu4 errinfo
    u8  RD_uaFlag       : 1;        //unaligned data
    
    //tHandle_Info
    u8 NeedGC   : 1;
    u8 Mark_Bad : 1;
    u8 Open_blk : 1;
    u8 Blank    : 1;    
    u8 DFU      : 1;   
	u8 gcOpen   : 1;                // Notice GC type open block, ISU, GCOpClsBefSuspnd			
	u8 Recv     : 1;
    u8 ReadVerify:2;
    
    u8 bResvd    :7;                // 4-Byte aligned, need to fix in future
    
}sGLEntry;

typedef struct
{
    u64 lda;
    u16 bit_map;
    u8  reg_sou;
    u8  rev[5];
}sGLECCTEntry;

typedef struct
{
    u32 dGL_Tag;					            // "GLIST"
    u8  bGL_Ver;					            // 0001
    u8  bGL_VerInvert;				            // FFFE
    u16 wGL_Mark_Cnt;
    u32 dCycle;
    u32 dGL_CheckSum;
    u32 dGL_CrossTempCnt;
    u16 wGL_ECC_Mark_Cnt;
    u16 dECC_Cycle;

    sGLEntry GlistEntry[GL_TOTAL_ENTRY_CNT];    // 36B * 1555
    sGLECCTEntry GlistECCTEntry[GL_TOTAL_ECCT_ENTRY_CNT]; // 16B * 500
    u32 rsvd[((GLIST_EPM_NEED_DTAG * DTAG_SZE - 4 - 1 - 1 - 2 - 4 - 4 - 4 - 2 - 2 - 55980 - 8000) + 3) / 4];
}sGLTable;

typedef struct
{
    u32 dGL_Entry_Cnt;
    u32 dECCT_Entry_Cnt;
    
}sEH_Manage_Info;

#define MAX_FAIL_BLK_CNT            10

//GList Status
#define GLERR_NO_ERROR              0
#define GLERR_PARAM_OVERFLOW        1
//#define GLERR_GLIST_FULL            2
#define GLERR_GLIST_EMPTY           3
#define GLERR_GBLOCK_EXIST          4
//#define GLERR_GBLOCK_NOT_EXIST      5
//#define GLERR_GLIST_TAG_NOT_FOUND   6
//#define GLERR_GLIST_VER_NOT_FOUND   7
//#define GLERR_GLIST_NO_FREESIZE     8
//#define GLERR_GLIST_READ_FIFO_ERROR 9
//#define GLERR_GLIST_NOT_SUPPORTED   10
//#define GLERR_PWR_BLOCK             11
//#define GLERR_BLANK_ERROR_BLCOK     12

//Mark Fail blk info
typedef union
{
    u32 all;
    
    struct
    {
        u16 wBlkIdx;
        u8  bType;
        u8  bClsdone: 1;			// No use
        u8  need_gc : 1;			// USR pre-act for mark bad.
        u8  bGCing  : 1;            // No use
        u8  bakupPBT: 1;			// FET, MakBadBakupPBT
        u8  markBad : 1;			// ISU, TSSP, PushGCRelsFalInfo				
        u8  gcOpen  : 1;			// ISU, GCOpClsBefSuspnd
		u8  resvd   : 2;
    }b;
}MK_FailBlk_Info;


#define MAX_OPEN_CNT 2048

#define EH_ONLINE_VERIFY                                                    //For White box test

#define FullCap_PhyblkCnt  798*256 // 798 SPB for 4T Capa.
#define CHK_GLENTRY_VALID(x) (pGList->GlistEntry[x].wPhyBlk != 0xFFFF)


#define PHY_BLK_SIZE  18   //18M
#define MAX_DIE_FAIL_CNT  200	// DBG, SMARTVry (2)   



//============================================================================//
/*
 *  Variable/ function dclaration
 */
//============================================================================//
extern u16 wGListRegBlock(sGLEntry* errInfo);
extern void ipc_wGListRegBlock(volatile cpu_msg_req_t *req);
//extern void EH_DelFailBlk(u16 wBlk);	// ISU, BakupPBTLat
extern void EH_DelFailBlk(MK_FailBlk_Info failBlkInfo);
extern void EH_SetFailBlk(u32 Tempall);
extern void __EH_DelFailBlk(volatile cpu_msg_req_t *req);

extern void __EH_SetFailBlk(volatile cpu_msg_req_t *req);

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
extern void __get_telemetry_ErrHandleVar_SMART_CPU_BE(volatile cpu_msg_req_t *req);
#endif

extern void Set_Fail_OP_Blk(u16 Blk);
extern void __Set_Fail_OP_Blk(volatile cpu_msg_req_t *req);
extern void Del_Err_OP_Cnt(u16 Blk);
extern void __Del_Err_OP_Cnt(volatile cpu_msg_req_t *req);

extern void Read_GList_Header(u32 *Read_Glist);
extern void Read_GList_PayLoad(u32 *Read_Glist);
extern void gl_build_table(void);
extern void ipc_Init_Nand_Written(volatile cpu_msg_req_t *req);

// extern void get_program_and_erase_fail_count(u32 *program_fail_count);
// extern void get_erase_fail_count(u32 *erase_fail_count);
// extern void get_nand_byte_written(u32* write_count);


//============================================================================//
/*
 * UECC SMART
 */
//============================================================================//
#if RAID_SUPPORT_UECC  
#define smart_uecc_info_cnt 100

enum uecc_type_t 
{
	NCL_UECC_NORMAL_RD = 0,
    NCL_UECC_SPOR_RD, //Not included QBT, PBT
	NCL_UECC_L2P_RD,
	NCL_UECC_AUX_RD,
	NCL_UECC_RC_HOST,
	NCL_UECC_RC_FTL,
	NCL_UECC_RC_SPOR,
    NCL_UECC_RC_L2P,
	NCL_UECC_RC_MAX,
};

enum
{
  uecc_detect = 0,
  uecc_fail,
  uecc_correct,
  uecc_max,
};

typedef struct
{
    u16 lblk_idx;
    u16 err_type;
        
}SMART_uecc_info_t;
#endif
//============================================================================//
/*
 * ECC Table Region
 */
//============================================================================//
#define ERRHANDLE_ECCT

#define MAX_ECC_TABLE_ENTRY 20000   //Max entry cnt = 65535
#define ECC_START_DATA_CNT  (GLIST_EPM_NEED_DTAG * DTAG_SZE)/4  //ECCT start addr in glist of emp 
#define MAX_RC_REG_ECCT_CNT 24
//extern volatile stECC_table *pECC_table;

//--------------ECCT error------------------//
#define ECCERR_ECCTABLE_EMPTY    1
#define ECCERR_PARAM_OVERFLOW    2
#define ECCERR_ECCTABLE_FULL     3
#define ECCERR_UNSUPPORT_RANGE   4

//-----------ECC table idx search-----------//
#define ECC_BIN_SEARCH           1
#define ECC_IDX_SEARCH           2

//----------------ECCT tag------------------//
#define ECC_TBL_TAG       0x42434345      // 'ECCB'
#define ECC_TBL_VER       0x01 
#define ECC_VER_INVERT    0xFE

//------------ECCT record source------------//
#define ECC_REG_VU          0
#define ECC_REG_WHCRC       1
#define ECC_REG_RECV        2
#define ECC_REG_DFU         3
#define ECC_REG_BLANK       4
//#define ECC_REG_MPECC       4    // 2 bit only for record source
//#define ECC_REG_WHCRC       5

//----------ECCT operation function---------//
#define VSC_ECC_reg         0
#define VSC_ECC_unreg       1
#define VSC_ECC_reset       2
#define VSC_ECC_rc_reg      3 
#define VSC_ECC_dump_table  4   //test

//----------RC ECCT register type-----------//
#define HOST_ECCT_TYPE      0
#define GC_ECCT_TYPE        1
#define OTHER_ECCT_TYPE     2
//------------------------------------------//

#define ECC_LDA_POISON_BIT	0x80000000	// Use highest bit to notice this LDA is poisoned, DBG, LJ1-252, PgFalCmdTimeout

typedef struct
{
    bm_pl_t bm_pl;
    u8 type;   //host or gc type
}tEcctReg;

typedef struct
{
    u32 err_lda;
    u16 bit_map;  //lba bit map in lda
    //u8  reserved;
    struct 
    {
        u8  die;
        u16 pblk;
        u16 page;
        //u32 pda;
        union
        {
            u8 all;
            struct
            {
                u8 au:         3;
                u8 reg_source: 3;
                u8 reserved:   2;
            } b;
        };
    } ErrInfor;
}stEccEntry;

typedef struct
{
    u32 ecc_table_tag;                                   
    u8  ecc_table_ver;           //version
    u8  ecc_table_inver;
    u16 ecc_table_cnt;
    u32 ecc_table_chksum;        //checksum for ECCTable                                        
    //stEccEntry ecc_entry[ecc_table_cnt];
    stEccEntry ecc_entry[MAX_ECC_TABLE_ENTRY];
}stECC_table;


typedef struct
{
    u64   lba;
    u32   total_len;
    u32   source;
    u32   type;
}stECCT_ipc_t;


void ECC_Sorting_Table(void);
void wECC_Dump_Record_Table(void);
void ECC_Dump_Table(void);
u16  wECC_Build_Table(void);
u16  wECC_Search_Idx(u32 lda, u16 left_margin, u16 right_matgin, u8 search_mode);
void ECC_Table_Reset(void);
pda_t LDA_to_PDA(u32 LDA);
//u16  wECC_Register_Table(u32 lba, u32 total_len, u8 reg_sour, pda_t pda);
u16  wECC_Register_Table(u64 lba, u32 total_len, u8 reg_sour);
void Register_ECCT_By_Raid_Recover(u64 lda, u16 bit_map, u8 reg_sour);
void wECC_Record_Table(u64 lda, u16 bit_map, u8 reg_sour);
u16  wECC_UnRegister_Table(u64 lba, u32 total_len);
void ECC_Table_Operation(stECCT_ipc_t *ecct_info);
void ipc_ECC_Table_Operation(volatile cpu_msg_req_t *req);
void ipc_ECC_Build_Table(volatile cpu_msg_req_t *req);
void ecct_update_epm_event_triggr(u32 parm, u32 payload, u32 sts);
void update_epm_init(void);
void __Register_ECCT_By_Raid_Recover(volatile cpu_msg_req_t *req);
void __wECC_Record_Table(volatile cpu_msg_req_t *req);
void ipc_init_eh_mgr(volatile cpu_msg_req_t *req);	// FET, PfmtHdlGL
void Reg_ECCT_Fill_Info(bm_pl_t *bm_pl, pda_t pda, u8 type, u8 source);
#endif

//============================================================================//
/*
 * Error Handle Modify Notice
 *     FET: feature
 *     ISU: issue/ bid
 *     DBG: debug
 *     VRY: verify
 */
//============================================================================//
// === had NOT been verified ===
// FET, GL2BFalFlag, Enable RD_2BFail in GList entry to notice 2BRR failed, Paul_20210407
// FET, RelsP2AndGL, Drop P2 list and Glist (grown defect) for ErrHandle verify (release VSC for Tx), Paul_20210413
// DBG, LJ1-252, PgFalCmdTimeout, In ncl_cmd_find_pda_index, access overflow index(0~23) from info_list(8 only), Paul_20210428
// FET, MakBadBakupPBT, Backup PBT to make sure marked bad SPB will not be mis-used again on next power cycle, Pis, Paul_20210506
// FET, VSCRegGL, Release register GList VSC cmd for verify SMART NAND related value, Paul_20210507
// DBG, SMARTVry, verify backend SMART value, Lucas/ Paul_20210508
//		(1) Update shr_nand_bytes_written when power down to save it in EPM, Paul_20210615
//		(2) Modify threshold of judging fail die, MAX_DIE_FAIL_CNT to 200/ 400 for 4T/ 8T drive, Paul_20210908
// ISU, SPBErFalHdl, handle SPB to mark bad after whole SPB erase done, Paul_20210514
// DBG, PgFalVry, check prog failed error handle flow, Paul_20210525
//		(1) Get pda from pda_list when getting info for register glist. 1-p op, no matter pl 0/1, will polled error status at ficu_err_finst_status[0] only.
//		(2) Init errInfo get from Err_Info_Cpu4_Addr in CPU4.
//		(3) Handle Vac is zero during error handle flow, Paul_20210701
// ISU, LJ1-242, (1) In GC open block prog failed handle flow, handle the popped block for GC to continue the GC flow later, Paul_20210602
//				 (2) GC pool block read failed, need not push it back to GC again.
// FET, PfmtHdlGL, after preformat, directly mark bad non-handled entry in GList, Paul_20210702
// ISU, LJ1-195, PgFalReWrMisComp, Prog failed and following re-write may cause incorrect cache order, Maksin/ Paul_20210719
// ISU, LJ1-337, PgFalClsNotDone, 
//		(1) Force close prog failed spb w/ two WLs only, Pis/ Paul_20210727, Paul_20210909
//      (2) Confirm PBT close open WL (by fill dummy) completely before reset related vars in ftl_set_force_dump_pbt, Easy/ Paul_20210729
// FET, EHDbgMsg, Add error handle related debug information, Paul_20210730
// ISU, PBTNotOpen, PBT cannot open due to failed on parity die request w/o ftl flag, Pis/ Paul_20210826
// FET, DumpGLLog, Dump GList table before save log, Paul_20210908
//		LJ1-426, Tony_20211223

// FET, ReduceEHCode, Remove useless code in EH/ GL flow, Paul_20210915
// ISU, TSSP, PushGCRelsFalInfo, Release failed spb from Fail_Blk_Info when pushing it to gc pool, Paul_20210915
// ISU, BakupPBTLat, backup PBT later for OpBlk EH performance in ISU, TSSP, PushGCRelsFalInfo, Paul_20211110
// ISU, GC_mod_for_EH, for case, page 0 prog failed and close 2 WLs only, gc cannot get correct P2L, Tania_20211118 
// ISU, EH_GC_not_done, 
//	(1) Avoid force close spb earlier than fill dummy done // ISU, EH_GC_not_done (1), Paul_20211125
//	(2) For gc open spb case, fix load P2L cnt incompletely. Maksin_Lucas_20211125
//  (3) Fix bug, raise dummy_done flag when all dummy req done, // ISU, EH_GC_not_done (3), Paul_20220209
// ISU, GCRdFalClrWeak, Consider case: gc read failed and set weak w/o increase shuttle cnt, Maksin/ Paul_20211128
// ISU, SetGCSuspFlag, apply gc suspend by gc_action instead ftl_core_gc_action, Paul_20211201
// ISU, HdlPBTAftForceCls, PBT die 127 may hit enc err 41, Paul_20211210
// ISU, QD1 performance, Move shr_gc_rel_du_cnt += shr_host_write_cnt from the 1st P2L to the last one. Adams_20211216
// ISU, FolwVacCntZeroHdl, handle case for vac == 0 later and trig error handle, Paul_20211223
// ISU, GCRdFalClrWeak
// ISU, GCIdlePgFal, GC idle fill dummy prog fail cannot suspend, Tania_20220107
// ISU, GCOpClsBefSuspnd, GCOpBlk err handling, ps_open[FTL_CORE_GC] changed before GC suspend, Paul_20220112
// FET, RmUselessVar, remove useless var in EH, Paul_20220119
// ISU, EHPerfImprove, Shuttle GC and force dump PBT speed control, Tania_20220215
//	(1) Force close PBT for PBT prog failed // ISU, EHPerfImprove (1), Paul_20220302
//  (2) Speed up force dump PBT to prohibit SPOR user build // ISU, EHPerfImprove (2), Paul_20220302
// ISU, AddSMART normalized value should not be negative // ISU, EHSmartNorValAdj, Paul_20220426
// ISU, FTL spb need not GC before mark bad // ISU, FTLSpbNotNedGC, Paul_20220517
// ISU, Erase failed handling by spb and meet retired criterion // ISU, ErFalRetired, Paul_20220518
// ISU, RdOpBlk force close handling // ISU, RdOpBlkHdl, Paul_20220704
