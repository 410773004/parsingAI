//============================================================================//
/*
 *  Header files
 */
//============================================================================//
#pragma once
#include "types.h"
#include "GList.h"
#include "../fcore/export/fc_export.h"					// ISU, RdOpBlkHdl
#include "export/ftltype.h"



//============================================================================//
/*
 *  Variable/ function definition
 */
//============================================================================//
typedef enum {
    EH_BUILD_GL_FTL_NOT_RDY = 0,    // Init and dump glist only.
    EH_BUILD_GL_USER_ERASED,        // Mark bad for not handled entry directly.
    EH_BUILD_GL_TRIG_EHTASK,        // Scan NOT handled entry which is not recorded in Fail_Blk_Info either and handle it.
    EH_BUILD_GL_MAX
} tEHBuildGLCond;



//============================================================================//
/*
 *  Variable/ function dclaration
 */
//============================================================================//	
// FET, PfmtHdlGL
extern volatile bool  fgFail_Blk_Full;        // need define in sram
extern sGLTable *pGList;             // need define in Dram  // GL_mod, Paul_20201130

extern volatile u16*  wOpenBlk; // need define in Dram

extern volatile u8    bErrOpCnt;
extern volatile u8    bErrOpIdx;

extern volatile bool  FTL_INIT_DONE;

extern sGLEntry errInfo2;
extern sGLEntry errInfo4;
extern u32 cpu4_glist_dtag;

extern sGLEntry* Err_Info_Cpu4_Addr;
extern volatile u32 Err_Info_Wptr;
extern volatile u32 Err_Info_Rptr;
extern volatile u32 Err_Info_size;

extern volatile u64 Nand_Written;

extern volatile sEH_Manage_Info sErrHandle_Info;  
extern volatile MK_FailBlk_Info Fail_Blk_Info_Temp;
extern volatile u8 bFail_Blk_Cnt;                       // need define in sram
extern MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];
extern volatile spb_id_t rdOpBlkFal[FTL_CORE_GC + 1];	// need define in sram, refered in ftl_core_done, ISU, RdOpBlkHdl
extern bool profail; 									// TBD_EH, Temp use for ASSERT Prog failed after mark defect.

extern u32	EPM_LBLK; 
extern bool gc_pro_fail;
extern bool gc_suspend_done_flag;
//extern bool Ftl_Pg_fail;	// FET, RmUselessVar
//extern u16 pbt_Pg_fail_blk;
//extern u16 Pg_fail_blk;

//============================================================================//
/*  
 *  CCCCCC    PPPPPP    U    U    333333
 *  C  		  P	   P    U    U         3
 *  C         PPPPPP    U    U    333333  Following functions used by CPU3 only.
 *  C         P         U    U         3
 *  CCCCCC    P         UUUUUU    333333
 */ 
//============================================================================//
extern void ErrHandle_Task(MK_FailBlk_Info failBlkInfo);	// ISU, TSSP, PushGCRelsFalInfo
extern void __ErrHandle_Task(volatile cpu_msg_req_t *req);
extern void fw_ard_handler(u32 fwArdParams);
extern void __fw_ard_handler(volatile cpu_msg_req_t *req);

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
extern void __get_telemetry_ErrHandleVar_CPU_FTL(volatile cpu_msg_req_t *req);
#endif

extern void eh_clear_glist(void);
extern void eh_dump_glist(void);
extern void eh_scan_glist_trig_handle(tEHBuildGLCond tCond);
extern void ipc_eh_clear_glist(volatile cpu_msg_req_t *req);   // Paul_20210105
extern void ipc_eh_dump_glist(volatile cpu_msg_req_t *req);	// FET, DumpGLLog
extern void __eh_scan_glist_trig_handle(volatile cpu_msg_req_t *req);

