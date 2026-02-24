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

/*! \file mpc.h
 * @brief multiple CPU build environment, add definitions which are used between CPUs
 *
 * \addtogroup utils
 * \defgroup mpc
 * \ingroup mpc
 * @{
 */
#pragma once

#include "cpu_msg.h"
#include "fwconfig.h"

BUILD_BUG_ON(sizeof(fw_config_set_t) != 4096);

#define MAX_MPC				4	///< max CPU support in multiple CPU environment
#define SRAM_DTAG_CNT			(SRAM_SIZE >> 12)	///< SRAM DTAG count
#define MAX_DTAG_NUM			(DDR_DTAG_CNT)	///< MAX DTAG count, SRAM + DDR
#define MAX_SPBQ_PER_NS			3	///< FTL_CORE_MAX, max spb queue per namespace, normal + GC + PBT
#define MAX_SPB_COUNT 1662//990  // get_total_spb_cnt() BiCS5 4P 831, 2P 1662; BiCS4 3.84T 990, 7.68T 1958
//skip defect mode define
#define SKIP_MODE  // skip mode or replace mode switch
#define H_MODE_WRITE mDISABLE
#define hlba_8_bytes 1//1

#define LJ_Meta 1
#define LJ1_WUNC
#define Aging_defect 1
#define Dynamic_OP_En 1  //AlanCC
#define EPM_NOT_SAVE_Again 0
#define Panic_save_epm 1
#define While_break  //AlanCC , open this need to change VPD_blk_write() location from fast_code to ddr_code
#define EPM_OTF_Time  1 // WA_FW_UPDATE need DISABLE and WARMBOOT_FTL_HANDLE need mENABLE //for on_the_fly save time, donot load EPM, MPIN, FRB table in reboot.

// ----- added by Sunny -----
#define SPOR_FLOW         mENABLE
#define SPOR_CMD_EXP_BAND mENABLE  // expand spor ncl cmd
#define SPOR_CMD_ON_DDR   mDISABLE

#define SPOR_L2P_VC_CHK   mENABLE  // chk l2p vac
#define FTL_L2P_SEG_BMP   mDISABLE
#define SPOR_QBT_MAX_ONLY mDISABLE   // for test
#define SPOR_FLOW_PARTIAL mDISABLE  // currently SPOR partial function test
#define SPOR_READ_PREV_BLIST mDISABLE   // for test
#define SPOR_AUX_MODE_DBG  mDISABLE
#define SPOR_TRIM_DATA     mENABLE
#define SPOR_PLP_WL_CHK_COMPLETE  mDISABLE
#define SPOR_FTLINITDONE_SAVE_QBT  mENABLE
#define SPOR_FILL_DUMMY   mDISABLE


#define DEBUG_SPOR        mENABLE
#define SPOR_TIME_COST    mENABLE
#define SPOR_ASSERT       mDISABLE
#define SPOR_CHECK_TAG    mDISABLE  //add by Jay for modify pbt special case
#define SPOR_VAC_CMP      mDISABLE
#define PLP_FORCE_FLUSH_P2L	  mDISABLE  //force flush P2L to last WL when PLP

#define SPOR_DUMP_ERROR_INFO mENABLE //add by Jay , DBG for plp no done and vac error

#if(PLP_SUPPORT == 0)
#define SPOR_CHK_WL_CNT   (16)
#define SPOR_NON_PLP_LOG  mDISABLE

#define SPOR_STATE_INIT (0)
#define SPOR_STATE_NEED_BUILD (0x1)
#define SPOR_STATE_BUILD_DONE  (0xFF)

#define SPOR_NONPLP_GC_DONE  (0x0)
#define SPOR_NONPLP_GC_START (0x1)
#endif


#define IGNORE_PLP_NOT_DONE mENABLE	//ignore plp not done for SME customer

#if(PLP_SUPPORT == 1)
#define SPOR_VAC_EC_IN_PLP_EPM  mENABLE
#define PLP_SLC_BUFFER_ENABLE mENABLE //add by Jay , use blk 1 to write plp cache in SLC
#define FW_BUILD_VAC_ENABLE   mDISABLE

#else
#define SPOR_VAC_EC_IN_PLP_EPM  mDISABLE
#define PLP_SLC_BUFFER_ENABLE mDISABLE //nonplp
#define FW_BUILD_VAC_ENABLE   mENABLE //nonplp enable FW build vac to save spor time 
#endif

#define SLC_BUFFER_MAX_WL (420) 

#define POWER_ON_EPM_UPDATE_ENABLE    (1)
#define POWER_ON_EPM_UPDATE_START     (2)
#define POWER_ON_EPM_UPDATE_DONE   	  (3)
#define POWER_ON_EPM_UPDATE_DISABLE   (0)

#define EVLOG_TRIGGER_INIT 	(0)
#define EVLOG_TRIGGER_START (1)
#define EVLOG_TRIGGER_DONE 	(2)

#define SAVE_MODE_PLP_MAY_ERROR 	(1)
#define SAVE_MODE_PLP_TO_MANY_LOGS 	(2)
#define SAVE_MODE_FEEDBACK_CHK 	    (3)


#define SPOR_BYPASS_OPEN_BLK    mDISABLE
// ----- added by Sunny -----

#define PLP_SLC_BUFFER_BLK_ID (1)

#define PLP_NO_DONE_DEBUG mDISABLE
#define PLP_GC_SUSPEND mENABLE
#define GC_SUSPEND_FWDL 1	//20210308-Eddie
#define RAID_SUPPORT 1

#define RAID_SUPPORT_UECC 1 //bug need fix
#define XOR_CMPL_BY_PDONE 1
#define XOR_CMPL_BY_FDONE 1
#define XOR_CMPL_BY_CMPL_CTRL 0
#define SEMI_WAIT_FALSE 0
#define SHOW_PERFORMANCE_READ 0  // get host read performance MB/s
#define SHOW_PERFORMANCE_RLOG 0  // log show host read performance MB/s (impact latency)
#define SHOW_PERFORMANCE_WRITE 1 // get host write performance MB/s
#define SHOW_PERFORMANCE_WLOG 0  // log show host write performance MB/s (impact latency)
#define RD_NO_OPEN_BLK    mENABLE
#define MIN_EC_STRATEGY   mENABLE

#define OPEN_ROW 0
#define UART_L2P_SEARCH 0
#define STOP_BG_GC
#define WCMD_DROP_SEMI  // use cache to control QD1 performance, IOPS ~= 75K
#define WCMD_USE_IRQ  // write command use interrupt mode, for QD1 QOS < 100us
#define CO_SUPPORT_READ_REORDER
#define CO_SUPPORT_READ_AHEAD 1
#if (_TCG_)
#define TCG_WRITE_DATA_ENTRY_ABORT 1
#endif
#if (Synology_case)
#define SYNOLOGY_SETTINGS 1
#endif

#define CO_SUPPORT_PANIC_DEGRADED_MODE 1

#if ((Tencent_case) || (RD_VERIFY))
#define FIX_I2C_SDA_ALWAYS_LOW_ISSUE mENABLE
#endif

/*! @brief multiple CPU initialization bitmap, all bit should be initialized by only one CPU except start and and */
typedef union _mpc_init_bmp_t {
	u32 all;
	struct {
		u32 start : 1;		///< indicate this CPU already started
		u32 ddr_init : 1;		///< indicate DDR was initialized
		u32 nand_geo_init : 1;	///< indicate nand geometry was initialized
		u32 idx_meta_init : 1;	///< indicate index meta was initialized
		u32 tag_meta_init : 1;	///< indicate index meta was initialized
		u32 fcore_init : 1;	///< indicate ftl_core was initialized
		u32 be_init : 1;		///< indicate backend (NCB1) was initialized
		u32 be2_init : 1;		///< indicate backedn 2 (NCB2) was initialized
		u32 be_lite_init : 1;	///< indicate backend submission queue 2 (NCB1, Q2) was initialized
		u32 dtag_ret: 1;		///< indicate reserved DTAGs should be returned to main DTAG MGR (CPU1)
		u32 l2p_init : 1;		///< indicate l2p was initialized
		u32 l2p_para_init : 1;		///< indicate l2p parameter was initialized
		u32 btn_rst : 1;		///< indicate BTN was reset
		u32 btn_wcmd_rst : 1;		///< indicate BTN wcmd was reset
		u32 cache_init : 1;		///< indicate cache was initialized

		u32 warm_boot_ftl_handle : 1;
		u32 wait_btn_rst : 1;		///< indicate remote cpu to wait btn reset
		u32 wait_ns_restore: 1;

		u32 tcm_ecc : 1;
#ifdef TCG_NAND_BACKUP
		u32 tcg_nf_init : 1;
		u32 rsvd : 11;
#else
		u32 rsvd : 12;
#endif
		u32 end : 1;		///< indicate this CPU initialized
	} b;
} mpc_init_bmp_t;

extern volatile mpc_init_bmp_t cpu_init_bmp[MAX_MPC];
extern volatile u8 is_A1_SoC;

#define ddr_init_loc		(CPU_FE - 1)		///< definition of ddr initialization CPU
#define nand_geo_init_loc	(CPU_BE - 1)		///< definition of nand geometry initialization CPU
#define idx_meta_init_loc	(CPU_BE - 1)		///< definition of index meta initialization CPU
#define tag_meta_init_loc	(CPU_FE - 1)
#define be_init_loc		(CPU_BE - 1)		///< definition of backend initialization CPU
#define be2_init_loc		(CPU_BE2 - 1)		///< definition of backend 2 initialization CPU
#define be_lite_init_loc	(CPU_BE_LITE - 1)	///< definition of backend submission queue 2 initialization CPU
#define l2p_init_loc		(CPU_FTL - 1)		///< definition of l2p initialization CPU
#define l2p_para_init_loc	(CPU_FTL - 1)		///< definition of l2p parameter initialization CPU
#define btn_rst_loc		(0)			///< definition of BTN reset CPU
//#define btn_wcmd_rst					///< don't know here
#define fcore_init_loc		(CPU_BE - 1)
#define cache_init_loc		(CPU_FE - 1)
#define warm_boot_ftl_handle_loc (CPU_FTL - 1)
#define wait_ns_restore_loc (CPU_FE - 1)
#ifdef TCG_NAND_BACKUP
#define tcg_nf_init_loc	(CPU_BE_LITE - 1)	///< definition of TCG nf init
#endif

#define tcm_ecc_loc		(0)
#define _loc(x)			glue(x, _loc)

#define subitem(b, x)		b x

#define local_init_bmp			cpu_init_bmp[CPU_ID_0]
#define remote_init_bmp(x)		cpu_init_bmp[_loc(x)]
#define remote_init_bmp_id(id)		cpu_init_bmp[id]
#define remote_init_item(x)		remote_init_bmp(x) subitem(.b., x)
#define remote_init_item_ex(x, id)	remote_init_bmp_id(id) subitem(.b., x)

#define local_init_item(x)		local_init_bmp subitem(.b., x)

/*!
 * @brief api to set an item initialized in local
 *
 * @param x	item name
 */
#define local_item_done(x)		\
		local_init_item(x) = 1;

/*!
 * @brief api to wait an item initialized from remote, and open IPC handling
 *
 * @param x	item name
 */
#define wait_remote_item_done(x)			\
		while (remote_init_item(x) == 0) {	\
			cpu_msg_isr();			\
		}

/*!
 * @brief api to wait an item initialized from known remote CPU, and open ipc handling
 *
 * @param x	item name
 * @param loc	remote CPU index
 */
#define wait_remote_item_done_ex(x, loc)			\
		while (remote_init_item_ex(x, loc) == 0) {	\
			cpu_msg_isr();				\
		}

/*!
 * @brief api to wait an item initialized from remote, but no IPC handling
 *
 * @param x	item name
 */
#define wait_remote_item_done_no_poll(x)		\
		while (remote_init_item(x) == 0) {	\
			;				\
		}

/*!
 * @brief api to check if cpu was start
 *
 * @param x	CPU ID, 0's base
 *
 * @return	return 1 if CPU x was stared
 */
#define chk_cpu_start(x)	cpu_init_bmp[x].b.start

/*!
 * @brief namespace attribute for nvme front end
 */
typedef struct _ns_t {
	u64 cap;
	u32 seg_off;
	u32 seg_end;
	union {
		u32 all;
		struct {
			u32 du4k : 1;
			u32 host_meta : 1;
		} b;
	} flags;
} ns_t;

typedef union _ftl_flags_t {
	struct {
		u32 flush_tbl_all : 1;
		u32 spor : 1;
		u32 read_only : 1;
		u32 ftl_ready : 1;	///< ftl ready to exec cmd(recon done)
		u32 l2p_all_ready : 1;	///< all l2p load into ddr
		u32 gcing : 1;
		u32 flushing : 1;
		u32 boot_cmpl : 1;	///< power on reconstruction done
		u32 gc_stoped : 1;
		u32 qbt_retire_need : 1;
	} b;
	u32 all;
} ftl_flags_t;

typedef union _read_only_t{
    u16 all;
    struct{
        u16 esr_err : 1;
        u16 ts_block : 1;
        u16 spb_retire : 1;
		u16 no_free_blk : 1;
		u16 nand_feature_fail : 1;
		u16 gc_stop:1;
		u16 plp_not_done : 1; /// enter non-access mode
		u16 spor_user_build :1;	///	enter non-access mode
		u16 high_temp : 1;
    } b;
} read_only_t;

typedef union _none_access_mode_t{
    u16 all;
    struct{
        u16 defect_table_fail : 1;
        u16 tcg_key_table_fail : 1;
    } b;
} none_access_mode_t;

typedef struct
{
    u32  abortMedia;
    u32  abortExtra;
    u32  abortAxi;
    u32  hostReset;
    u32  cpuStatus[3];
} ResetHandle_t;


typedef struct range{
    u32 Length;
    u32 sLDA;
}range;

typedef struct Host_Trim_Data
{
    u32 Validtag;
    u32 Validcnt;
    u16 all;
    u16 valid_bmp;
    u32 nsid;//joe add 20200914  for trim transfer
    range Ranges[510];
}Host_Trim_Data;
BUILD_BUG_ON(sizeof(Host_Trim_Data) != 4096);



extern volatile ResetHandle_t smResetHandle;
/*!
 * @brief share TCM heap initialization
 *
 * @return	none
 */
void share_heap_init(void);

/*!
 * @brief share TCM heap memory allocation
 *
 * @param size	required size
 *
 * @return	allocated memory pointer or NULL
 */
void *share_malloc(u32 size);

/*!
 * @brief share TCM heap memory aligned allocation
 *
 * @param size	required size
 * @param aligned	aligned size
 *
 * @return	allocated memory pointer or NULL
 */
void* share_malloc_aligned(u32 size, u32 aligned);

/*!
 * @brief share TCM heap memory free
 *
 * @param ptr	share memory pointer to be free
 *
 * @return	not used
 */
void share_free(void *ptr);

/*!
 * @brief get ftl config in fwconfig
 *
 * @return	return ftl config pointer or NULL
 */
ftl_cfg_t *fw_config_get_ftl(void);

/*! @} */
