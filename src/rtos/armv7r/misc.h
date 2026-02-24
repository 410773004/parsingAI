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

/*! \file misc.h
 * @brief misc for timer, and misc
 *
 * \addtogroup rtos
 * \defgroup core misc
 * \ingroup rtos
 *
 * {@
 */

#pragma once

#include "irq.h"
#include "list.h"
#include "io.h"
#include "smbus.h"
#include "rainier_soc.h"

#define   GPIO_PLP_STRPG_SHIFT                   (4)    //GPIO 4
#define   GPIO_PLP_DETECT_SHIFT                  (3)    //GPIO 3
#define   GPIO_POWER_DISABLE_SHIFT               (0)    //GPIO 0

#define   GPIO_PLA_SHIFT                         (12)   //GPIO 12
#define   GPIO_PLN_SHIFT                         (1)     //GPIO 1
#define   GPIO_PLP_DEBUG_SHIFT                   (15)    //GPIO 15

#define PLP_ENA_ENABLE    (1)

#define PLP_ENA_DISABLE   (0)


#if defined(FPGA)
#define CPU_CLK			SYS_CLK
#else
#define CPU_CLK			(800 * 1000 * 1000)	///< default clock is 666Mhz
#endif

#define TIMER0_CYCLE_PER_TICK   (CPU_CLK / HZ)

#define CYCLE_PER_MS            (CPU_CLK / 1000)	///< number of cpu cycles in 1ms
#define CYCLE_PER_US            (CYCLE_PER_MS / 1000)	///< number of cpu cycles in us

#define WDT_TIMER_MS		(2000)				///< watchdog timer feed interval in ms
#define WDT_TIMER_CLK		(CYCLE_PER_MS * WDT_TIMER_MS)	///< watchdog timer feed interval in cpu cycles

#define POWER_ISR         mDISABLE

#if CPU_ID == 1
#define MASKED_SYS_INT_REG	C1_MASKED_SYS_INT
#define SYS_INT_MASK_REG	C1_SYS_INT_MASK
#define SYS_VID_WDT		SYS_VID_WDT_1
#elif CPU_ID == 2
#define MASKED_SYS_INT_REG	C2_MASKED_SYS_INT
#define SYS_INT_MASK_REG	C2_SYS_INT_MASK
#define SYS_VID_WDT		SYS_VID_WDT_2
#elif CPU_ID == 3
#define MASKED_SYS_INT_REG	C3_MASKED_SYS_INT
#define SYS_INT_MASK_REG	C3_SYS_INT_MASK
#define SYS_VID_WDT		SYS_VID_WDT_3
#elif CPU_ID == 4
#define MASKED_SYS_INT_REG	C4_MASKED_SYS_INT
#define SYS_INT_MASK_REG	C4_SYS_INT_MASK
#define SYS_VID_WDT		SYS_VID_WDT_4
#else
#error "wrong CPU setting"
#endif

#if defined(FPGA)
#define CPU_CLK			SYS_CLK
#else
#define CPU_CLK			(800 * 1000 * 1000)	///< default clock is 666Mhz
#endif

#if defined(USE_MU_NAND) || defined(USE_TSB_NAND) || defined(USE_SNDK_NAND)
#define USE_NAND_TEMP	0 // disable temporarily, wait for NCL	implement
#else
#define USE_NAND_TEMP	0
#endif

#if USE_NAND_TEMP
#define TS_DEFAULT_CRITICAL	95
#define TS_DEFAULT_WARNING	(TS_DEFAULT_CRITICAL - 20)	///< ts warning: 75
#define TS_DEFAULT_TMT0	(TS_DEFAULT_CRITICAL - 25)	///< default tmt0: 70
#define TS_DEFAULT_TMT1	(TS_DEFAULT_CRITICAL - 20)	///< default tmt1: 75
#define TS_DEFAULT_TMT2	(TS_DEFAULT_CRITICAL - 10)	///< default tmt2: 85
#define TS_DEFAULT_UNDER	(0)
#else
//#define TS_DEFAULT_CRITICAL	115
#define TS_DEFAULT_CRITICAL	85 //refer to GTP spec,if USE_NAND_TEMP modified to 1 please let suda know

#ifdef Xfusion_case
	#define TS_DEFAULT_WARNING	(TS_DEFAULT_CRITICAL - 7)	///< ts warning: 78
	#define TS_DEFAULT_TMT0	(TS_DEFAULT_CRITICAL - 7)	///< default tmt0: 78
	#define TS_DEFAULT_TMT1	(TS_DEFAULT_CRITICAL - 7)	///< default tmt1: 78
	#define TS_DEFAULT_TMT2	(TS_DEFAULT_CRITICAL - 2)		///< default tmt2: 83
#elif defined(iEi_case)
	#define TS_DEFAULT_WARNING	(TS_DEFAULT_CRITICAL - 10)	///< ts warning: 75
	#define TS_DEFAULT_TMT0	(TS_DEFAULT_CRITICAL - 10)	///< default tmt0: 75
	#define TS_DEFAULT_TMT1	(TS_DEFAULT_CRITICAL - 10)	///< default tmt1: 75
	#define TS_DEFAULT_TMT2	(TS_DEFAULT_CRITICAL - 5)		///< default tmt2: 80
#elif (Smart_Modular_case) && (PLP_SUPPORT == 0)
	#define TS_DEFAULT_WARNING	(TS_DEFAULT_CRITICAL)	///< ts warning: 85
	#define TS_DEFAULT_TMT0	(TS_DEFAULT_CRITICAL - 15)	///< default tmt0: 70
	#define TS_DEFAULT_TMT1	(TS_DEFAULT_CRITICAL - 15)	///< default tmt1: 70
	#define TS_DEFAULT_TMT2	(TS_DEFAULT_CRITICAL - 5)		///< default tmt2: 80
#else
	#define TS_DEFAULT_WARNING	(TS_DEFAULT_CRITICAL - 15)	///< ts warning: 70
	#define TS_DEFAULT_TMT0	(TS_DEFAULT_CRITICAL - 15)	///< default tmt0: 70
	#define TS_DEFAULT_TMT1	(TS_DEFAULT_CRITICAL - 15)	///< default tmt1: 70
	#define TS_DEFAULT_TMT2	(TS_DEFAULT_CRITICAL - 5)		///< default tmt2: 80
#endif

#define TS_DEFAULT_UNDER	(0)

#endif

#define TS_MIN_TMT	TS_DEFAULT_UNDER
#define TS_MAX_TMT	TS_DEFAULT_CRITICAL

#define AGING2NORMAL_DDRBYPASS	//20201123-Eddie

#define CA_1_SIGNATURE     0x20434131  /* CA1 */	//20201225-Eddie
#define CA_2_SIGNATURE     0x20434132  /* CA2 */	//20210426-Eddie
#define CA_3_SIGNATURE     0x20434133  /* CA3 */
//20210226-Eddie-Use Sticky reg 12 to set CPU locked flag
#define STTS12_CPU_LOCK_CLR     0x00000000  //CPU lock flag CLEARED
#define STTS12_CPU_LOCK     0x00000001  //CPU lock flag,BIT0
#define STTS11_PARD_STOP     0x00000002  //Patrol Read Stop flag,BIT1
#define STTS11_REFR_STOP     0x00000004  //Refresh Read Stop flag,BIT2
#define STTS11_BGREAD_STOP     0x00000008  //Patrol & Refresh Read Stop flag,BIT3

#define GET_PCIE_ERR DISABLE
#define CORR_ERR_INT ENABLE
#define RXERR_IRQ_RETRAIN       // Jack 2022.04.28
//#define RXERR_LINKUP_RETRAIN    // Jack 2022.04.28 //close 2023.09.01
//#define LANE_LOSS_RETRAIN
#define LOG_PCIE_ERR 0
#define xMPS128
//#define LONG_CPL_TO		//close 2023.09.01
#define CPL_TO_ERR_INJECT
#define PCIE_PHY_IG_SDK //add IG SDK PCIe phy code from ssstc_hs_04020J34.zip

//#if (CUST_CODE == 0) // RD Version
//#define PERST_MODE_0          // PERST_MODE_0  HW_RESET_PCIE_MAC     3/25 Richard
//#endif

#define PLP_TEST 1
#define LOG_PLP 1

#ifdef MDOT2_SUPPORT
#define MAX_TEMP_SENSOR 1 //Only one temp sensor, WillWu 2023.6.5
#else
#define MAX_TEMP_SENSOR 2
#endif

#define SENSOR_IN_SMART 1
#if (MAX_TEMP_SENSOR == 1) //Only one temp sensor, WillWu 2023.6.5
#define SenAllFail 1
#endif
#if (MAX_TEMP_SENSOR == 2)
#define SenAllFail 3
#endif
#if (MAX_TEMP_SENSOR == 3)
#define SenAllFail 7
#endif
#define WB_IDLE_MODE  ENABLE	//20210426-Eddie
#define UART_STARTWB	DISABLE
#define WB_WO_RESET	ENABLE

#define OTF_MEASURE_TIME
#define OTF_TIME_REDUCE	ENABLE

#if (Tencent_case)
#define MAX_AVG_EC 7000
#else
#define MAX_AVG_EC 10000
#endif

#define MRR_EQUAL_MPS DISABLE	// fix PJ1_78. Z590-A M2_slot 2 + Gen2 + nSPOR fail.

#define OVER_PROVISION (14)
#define LDR_Mode 2
#define MAIN_Mode 0
enum {
	TS_SEC_NORMAL,
	TS_SEC_WARNING,
	TS_SEC_CRITICAL,
	TS_SEC_MAX,
};

enum {
	TS_THROTTLE_NONE,		///< max performance gear
	TS_THROTTLE_LIGHT,	///< slow down CPU
	TS_THROTTLE_HEAVY,	///< slow down CPU and NF
	TS_THROTTLE_BLOCK,	///< block IO due to high or low temperature
	TS_THROTTLE_MAX,
};

typedef struct{
    u32 receiver_err_cnt;
    u32 bad_tlp_cnt;
    u32 bad_dllp_cnt;
    u32 replay_num_rollover_cnt;
    u32 replay_timer_timeout_cnt;
    u32 advisory_non_fatal_error_cnt;
    u32 corrected_internal_error_cnt;
    u32 header_log_overflow_cnt;
}corr_err_cnt_t;

typedef struct{
    u32 undefined_cnt;
    u32 data_link_protocol_err_cnt;
    u32 surprise_down_err_cnt;
    u32 poisoned_tlp_received_cnt;
    u32 flow_control_protocol_err_cnt;
    u32 complement_timeout_cnt;
    u32 completer_abort_cnt;
    u32 unexpected_complement_cnt;
    u32 receiver_overflow_cnt;
    u32 malformed_tlp_cnt;
    u32 ercr_err_cnt;
    u32 unsupported_request_err_cnt;
    u32 acs_violation_cnt;
    u32 uncorrectable_internal_err_cnt;
    u32 mc_blocked_tlp_cnt;
    u32 atomicop_egress_blocked_cnt;
    u32 tlp_prefix_blocked_err_cnt;
    u32 poisoned_tlp_egress_blocked_cnt;
}uncorr_err_cnt_t;


typedef struct _ts_tmt_t {
	u16 sec;	///< current temperature section
	u16 gear;	///< current throttle gear
	int tmt0;	///< temperature of tmt0
	int tmt1;	///< temperature of tmt1
	int tmt2;	///< temperature of tmt2
	int warning;	///< temperature of warning
	int critical;	///< temperature of critical

	int cur_ts;	///< currrent temperature
	u32 sec_start;	///< current temperature section start time
	u32 sec_time[TS_SEC_MAX];	///< time of each section

	u32 tmt_start;	///< current tmt level start time
	u32 tmt_cnt[2];	///< tmt switch count
	u32 tmt_time[2];	///< time of each tmt level
} ts_tmt_t;


#define HZ          10             /* 100ms */

#define PCIE_RST			0
#define PCIE_RST_LINK_DOWN		1
#define PCIE_RST_FLR			2
#define NVME_RST_CC_CLR			3
#define NVME_RST_CC_EN			4
#define NVME_RST_SHUTDOWN		5
#define NVME_RST_SUBSYSTEM		6
#define PCIE_NVME_RST_COUNT		8

#define PCIE_GEN1               1
#define PCIE_GEN2               2
#define PCIE_GEN3               3
#define PCIE_GEN4               4

#define PCIE_X1                 1
#define PCIE_X2                 2
#define PCIE_X4                 4

//ltssm code
#define PCIE_LTSSM_S_DETECT_QUIET 0x00
#define PCIE_LTSSM_S_DETECT_ACT 0x01
#define PCIE_LTSSM_S_POLL_ACTIVE 0x02
#define PCIE_LTSSM_S_POLL_COMPLIANCE 0x03
#define PCIE_LTSSM_S_POLL_CONFIG 0x04
#define PCIE_LTSSM_S_PRE_DETECT_QUIET 0x05
#define PCIE_LTSSM_S_DETECT_WAIT 0x06
#define PCIE_LTSSM_S_CFG_LINKWD_START 0x07
#define PCIE_LTSSM_S_CFG_LINKWD_ACEPT 0x08
#define PCIE_LTSSM_S_CFG_LANENUM_WAI 0x09
#define PCIE_LTSSM_S_CFG_LANENUM_ACEPT 0x0A
#define PCIE_LTSSM_S_CFG_COMPLETE 0x0B
#define PCIE_LTSSM_S_CFG_IDLE 0x0C
#define PCIE_LTSSM_S_RCVRY_LOCK 0x0D
#define PCIE_LTSSM_S_RCVRY_SPEED 0x0E
#define PCIE_LTSSM_S_RCVRY_RCVRCFG 0x0F
#define PCIE_LTSSM_S_RCVRY_IDLE 0x10
#define PCIE_LTSSM_S_L0 0x11
#define PCIE_LTSSM_S_L0S 0x12
#define PCIE_LTSSM_S_L123_SEND_EIDLE 0x13
#define PCIE_LTSSM_S_L1_IDLE 0x14
#define PCIE_LTSSM_S_L2_IDLE 0x15
#define PCIE_LTSSM_S_L2_WAKE 0x16
#define PCIE_LTSSM_S_DISABLED_ENTRY 0x17
#define PCIE_LTSSM_S_DISABLED_IDLE 0x18
#define PCIE_LTSSM_S_DISABLED 0x19
#define PCIE_LTSSM_S_LPBK_ENTRY 0x1A
#define PCIE_LTSSM_S_LPBK_ACTIVE 0x1B
#define PCIE_LTSSM_S_LPBK_EXIT 0x1C
#define PCIE_LTSSM_S_LPBK_EXIT_TIMEOUT 0x1D
#define PCIE_LTSSM_S_HOT_RESET_ENTRY 0x1E
#define PCIE_LTSSM_S_HOT_RESET 0x1F
#define PCIE_LTSSM_S_RCVRY_EQ0 0x20
#define PCIE_LTSSM_S_RCVRY_EQ1 0x21
#define PCIE_LTSSM_S_RCVRY_EQ2 0x22
#define PCIE_LTSSM_S_RCVRY_EQ3 0x23


//#define MEASURE_TIME

extern u32 jiffies;		///< global 100ms ticks
extern u8 evt_perst_hook;	///< pcie reset event
extern u64 tsc64_base;		///< record of tick before pmu
extern volatile u32 _SYS_CLK;		///< current system clock
extern u8 evt_vol_mon_hook;	///< voltage monitor event
extern volatile bool ts_io_block;
extern ts_tmt_t ts_tmt;
#ifdef OTF_MEASURE_TIME
extern u32 baseTick;
extern vu32 recMsec[12];
extern u64 global_time_start[20];
#endif
extern u32 gPcieRstRedirected;
extern volatile u32 gResetFlag;
extern u8 evt_wr_pending_abort;
extern bool gPerstInCmd;
extern bool gPlpDetectInIrqDisableMode;
extern bool gFormatInProgress;
extern u8 evt_assert_rst;
extern bool ctrl_fatal_state;
/*!
 * @brief definition of timer
 */
struct timer_list {
	struct list_head entry;		///< list entry
	u32 expires;			///< default expires time if added
	void (*function)(void *data);	///< handler when expires reached
	void *data;			///< common context
};

/* reset */
/* Don't change the sequence */
enum reset_type {
	RESET_CHIP = 0,
	RESET_NCB,
	RESET_NVME,
	RESET_BM,
	RESET_CPU,
	RESET_PCIE,
	RESET_PCIE_PHY,
	RESET_POWER_DOMAIN_0,
	RESET_PCIE_PHY_REGS,
	RESET_PERI,
	RESET_AXI,
	RESET_PCIE_MAC,
	RESET_PCIE_WRAPPER,
	RESET_MC,
	RESET_L2CACHE,
	RESET_CPU_MSG,
};

/// @brief Reset type
typedef enum
{
    cNvmeControllerResetClr = 0,    ///< NVMe controller reset (CC.EN 1 to 0 or NSSR = "NVMe")
    cNvmeControllerResetSet,        ///< NVMe controller reset (CC.EN 0 to 1 or NSSR = "NVMe")
    cNvmeSubsystemReset,            ///< NVMe sub-system reset (NSSR = "NVMe")
    cNvmeFlrPfReset,                ///< NVMe FLR Physical Function reset
    cNVMeLinkReqRstNot,             ///< PCIe Link Req Rst Not interrupt
    cNvmePCIeReset,                 ///< PCIe reset
    cNvmeShutDown,					//
    cWarmReset,
} NvmeReset_t;

/// @brief NVMe Command Common DW 0
typedef union
{
    u32 all;///< All bits.
    struct
    {
        u32 OPC:8;         ///< Opcode DW0 bits[7:0]
        u32 FUSE:2;        ///< Fused Operation (00=normal, 01=fused 1st command, 10=fused last command, 11=reserved) DW0 bits[9:8]
        u32 reserved0:4;   ///< Reserved DW0 bits[13:10]
        u32 PSDT:2;        ///< PRP or SGL for Data Transfer(PSDT) DW0 bits[15:14]
        u32 CID:16;        ///< Command Identifier DW0 bits[31:16]
    } b;
} NvmeCommandDw0_t;

// 2022/8/1  Keep VU flag value when warm boot occur
typedef struct
{
	union
	{
		u8 all;
		struct
		{
			u8 dump_log_flag   :1;
			u8 dump_log_en	   :1;
			u8 vsc_on          :1;
			u8 reserved 	   :5;
		}b;
	};
}cache_VU_FLAG;

// #if CO_SUPPORT_DEVICE_SELF_TEST
#define ShortDSTTime   120    // Short Device Self-test Time
#if(Xfusion_case)
#define ExtenDSTTime   1200    // Extended Device Self-test Time for Xfusion (EDSTT) 240726-Max modify
#elif(Tencent_case)
#define ExtenDSTTime   1200    // Extended Device Self-test Time for Tencent (EDSTT) 240909-Shengbin modify
#elif (Synology_case)
#define ExtenDSTTime   3600    // Extended Device Self-test Time for Synology (EDSTT) 251029-Leslie modify
#else
#define ExtenDSTTime   7200    // Extended Device Self-test Time (EDSTT): 251029-Leslie modify
#endif
extern u8 evt_dev_self_test;
extern u8 evt_dev_self_test_delay;

extern u32    gDeviceSelfTest;
extern u8     gCurrDSTOperation;
extern u8     gCurrDSTOperationNSID;
extern u8     gCurrDSTCompletion;
extern u64    gCurrDSTTime;
extern u64    gLastDSTTime;
extern u8     gCurrDSTOperationImmed;
extern u8*    gDSTScratchBuffer;
extern u8*    gDSTScratchBuffer;


enum
{
    cDST_INIT                       = 0x0,
    cDST_SHORT_RAM_CHECK            = 0x1,
    cDST_SHORT_SMART_CHECK,
    cDST_SHORT_VOLATILE_MEMORY,
    cDST_SHORT_METADATA_VALIDATION,
    cDST_SHORT_NVM_INTEGRITY,
    cDST_EXTEN_DATA_INTEGRITY,
    cDST_EXTEN_MEDIA_CHECK,
    cDST_EXTEN_DRIVE_LIFE,
    cDST_EXTEN_SMART_CHECK,
    cDST_pass_patrol,
    cDST_DONE                       = 0xFF
};
typedef enum
{
    cDST_SHORT    = 1,
    cDST_EXTENDED = 2,
    cDST_ABORT    = 0x0F,
} Self_test_Code_t;

typedef enum
{
    cDSTCompleted           = 0,  // Operation completed without error
    cDSTAbort               = 1,  // Operation was aborted by a Device Self-test command
    cDSTAbortReset          = 2,  // Operation was aborted by a Controller Level Reset
    cDSTAbortNamespace      = 3,  // Operation was aborted due to a removal of a namespace from the namespace inventory
    cDSTAbortFormat         = 4,  // Operation was aborted due to the processing of a Format NVM command
    cDSTFatalErr            = 5,  // A fatal error or unknown test error occurred while the controller was executing the device self-test operation and the operation did not complete
    cDSTCompletedFail       = 6,  // Operation completed with a segment that failed and the segment that failed is not known
    cDSTCompletedSegmentNum = 7,  // Operation completed with one or more failed segments and the first segment that failed is indicated in the Segment Number field
    cDSTAbortSanitize       = 9,  // Operation was aborted by a sanitize cmd
    cDSTEntryNotUsed        = 0x0F,  //Entry not used
} Self_test_Result_t;


extern void DST_ProcessCenter(u32 p0, u32 p1, u32 p2);
extern void DST_ProcessCenter_delay(u32 p0, u32 p1, u32 p2);
extern void NvmeDeviceSelfTestInit(void);
extern void DST_Completion(u8 Result);
extern void DSTOperationImmediate(void);
extern void HandleSaveDSTLog(void);
extern void DST_Operation(u8 stc, u32 nsid);

// #endif


/*!
 * @brief reset asic module
 *
 * @param reset_type	module to be reset
 *
 * @return		not used
 */
extern void misc_reset(enum reset_type);

/*!
 * @brief setup divsisor of uart according system clock
 *
 * @param sys_clk	system clock
 *
 * @return		not used
 */
extern void set_uart_divisor(u32 sys_clk);

/*!
 * @brief setup internal thermal sensor
 *
 * @return      return not used
 */
extern void ts_setup(void);

/*!
 * @brief init thermal fw parameters
 *
 * @return      return not used
 */
extern void ts_start(void);

/*!
 * @brief return absolute temperature getting from tmp102 sensor
 *
 * @return      absolute temperature
 */
extern u16 misc_get_temp(void);

/*!
 * @brief return current temperature from internal sensor
 *
 * @return      absolute temperature
 */
extern u32 ts_get(void);

/*!
 * @brief get current temperature statistics
 *
 * @param sec_time      total time of normal/warning/critical
 * @param tmt_tran_cnt  thermal management transition count
 * @param tmt_time      total time of thermal management
 *
 * @return              none
 */
extern void ts_sec_time_get(u32 sec_time[3], u32 tmt_tran_cnt[2], u32 tmt_time[2]);

extern void ts_warn_cri_tmt_setup(u32 tmt_waring, u32 tmt_critical);

/*!
 * @brief setup thermal management temperature 1 and 2
 *
 * @param tmt1  thermal management temprature 1
 * @param tmt2  thermal management temprature 2
 *
 * @return      none
 */
extern void ts_tmt_setup(u32 tmt1, u32 tmt2);

/*!
 * @brief dump gpio information
 *
 * @return	not used
 */
extern void gpio_dump(void);

/*!
 * @brief check if nand voltage is 1.2v or not
 *
 * @return	return true if 1.2 was applied
 */
extern bool gpio_io_voltage_is_1_2v(void);

/*!
 * @brief check if this timer is already in pending list
 *
 * @param timer		timer to be checked
 *
 * @return		return true if it was pended
 */
extern bool timer_is_pending(const struct timer_list *timer);

/*! @brief add timer to pending list
 *
 * @note Timers with an ->expires field in the past will be executed in the next timer tick
 *
 * @param timer		timer to be added
 *
 * @return		not used
 */
extern void add_timer(struct timer_list *timer);

/*!
 * @brief delete timer if it was in pending list
 *
 * @param timer		timer to be deleted
 *
 * @return		return 1 if it was in pending list and deleted
 */
extern int del_timer(struct timer_list *timer);

/*!
 * @brief disable shared timer irq
 *
 * @return		not used
 */
extern void share_timer_irq_disable(void);

extern u32 cpu_clk_init(int freq);
extern u32 nf_clk_init(int freq);

/*!
 * @brief not ready yet
 *
 * @param bool
 *
 * @return		not used
 */
extern bool warm_boot_on(void);

/*!
 * @brief not ready yet
 *
 * @param bool
 *
 * @return		not used
 */
extern void warm_boot_enable(bool);

/*!
 * @brief not ready yet
 *
 * @param bool
 *
 * @return		not used
 */
extern void warm_boot(void);
/*!
 * @brief modify a timer's timeout
 *
 * mod_timer is a more efficient way to update the expire field of an active timer
 * (if the timer is inactive it will be activated)
 * mod_timer(timer, expires) is equivalent to del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * @param timer		the timer to be modified
 * @param expires	new timeout in jiffies
 *
 * @return		Return whether it has modified a pending timer or not.
 * 			(ie. mod_timer() of an inactive timer returns 0,
 * 			mod_timer() of an active timer returns 1.)
 */
extern int mod_timer(struct timer_list *timer, u64 expires);

/*!
 * @brief get lower part of cpu tick
 *
 * @return	return lower part of cpu tick
 */
extern u32 get_tsc_lo(void);

/*!
 * @brief get higher part of cpu tick
 *
 * @return	return higher part of cpu tick
 */
extern u32 get_tsc_hi(void);

/*!
 * @brief get CPU tick, one tick per CPU cycle
 *
 * @note it's CPU private register
 *
 * @return	current cpu tick
 */
extern u64 get_tsc_64(void);
extern u32 get_tsc_ms(void);
/*!
 * @brief get sys tick, one tick per system cycle
 *
 * @return	current system tick
 */
extern u64 get_st1_64(void);
extern void gpio_init(void);
extern bool gpio_get_value(u32 gpio_ofst);
extern void gpio_set_gpio15(u32 value);
extern void disable_ltssm(u32 ms);
/*
static inline void sys_reset(void)
{
	misc_reset(RESET_CHIP);
}
*/
/*!
 * @brief set system time(unit ms,error range is 100ms.)
 *
 * @return	not used
 */
extern void set_cur_sys_time(u64 time);


/*!
 * @brief get current system time(unit ms)
 *
 * @return	system time value.
 */
extern u64 get_cur_sys_time(void);


/*!
 * @brief delay function for us microsecond
 *
 * @param us	delay us microsecond
 *
 * @return	not used
 */
extern void udelay(u32 us);

/*!
 * @brief delay function for us millisecond
 *
 * @param us	delay us millisecond
 *
 * @return	not used
 */
extern void mdelay(u32 ms);

extern void mdelay_plp(u32 ms);

/*!
 * @brief count time elapsed since timestamp in us
 *
 * @param timestamp	last timestamp
 *
 * @return		time elapsed since timestamp in us
 */
extern u32 time_elapsed_in_us(u64 timestamp);

/*!
 * @brief count time elapsed since timestamp in ms
 *
 * @param timestamp	last timestamp
 *
 * @return		time elapsed since timestamp in ms
 */
extern u32 time_elapsed_in_ms(u64 timestamp);

/*!
 * @brief return how many jiffies elapsed
 *
 * @param timestamp	last stamp in jiffies
 *
 * @return		return how many jiffies elapsed
 */
extern u32 time_elapsed_in_jiffies(u32 timestamp);

/*!
 * @brief abort nvme hw controller
 *
 * @param reset		abort nvme hw if true
 *
 * @return		not used
 */
extern void misc_nvm_ctrl_reset(bool reset);

/*!
 * @brief set warm boot flag, and save some data in sticky registers, and use them after warm boot
 *
 * @param data		data list to be saved
 * @param count		list length
 *
 * @return		not used
 */
extern void misc_set_warm_boot_flag(u32 *data, u32 count);

/*!
 * @brief set xloader boot signature, reuse warm boot sticky register 8
 *
 * @param img_pos	firmware image position for xloader
 *
 * @return		not used
 */
extern void misc_set_xloader_boot(u32 img_pos);

/*!
 * @brief clear warm boot flag, clear data in sticky registers, called after warm boot
 *
 * @return		not used
 */
extern void misc_clear_warm_boot_flag(void);

/*!
 * @brief check sticky register if warm boot signature
 *
 * @return	return true if warm boot
 */
extern bool misc_is_warm_boot(void);

/*!
 * @brief read saved data in sticky registers after warm boot
 *
 * @param data		data buffer
 * @param count		list length
 *
 * @return		not used
 */
extern void misc_get_warm_boot_data(u32 *data, u32 count);

/*!
 * @brief set fw wait reset flag in sticky register, trigger warm boot after reset
 *
 * @return		not used
 */
extern void misc_set_fw_wait_reset_flag(void);

/*!
 * @brief check if fw run for reset signal
 *
 * @return		true if fw wait reset flag in sticky register
 */
extern bool misc_is_fw_run_reset(void);

/*!
 * @brief set fw run reset flag in sticky register, trigger warm boot after reset
 *
 * @return		not used
 */
extern void misc_set_fw_run_reset_flag(void);

/*!
 * @brief check if fw wait for reset signal
 *
 * @return		true if fw wait reset flag in sticky register
 */
extern bool misc_is_fw_wait_reset(void);

/*!
 * @brief initialization of pcie
 *
 * @return	not used
 */
extern void pcie_init(void);
extern void pll1_power(bool off);//CMF Workaround _GENE_20201119
/*!
 * @brief check if pcie clk is enabled, if host was connected
 *
 * @return	return true if host was connected
 */
extern bool is_pcie_clk_enable(void);

/*!
 * @brief enable pcie request exit L1, phy will keep in L0
 *
 * @return	not used
 */
extern void enable_pcie_req_exit_l1(void);

/*!
 * @brief disable pcie request exit L1
 *
 * @return	not used
 */
extern void disable_pcie_req_exit_l1(void);

/*!
 * @brief get PCIe transfer max payload size
 *
 * @return	PCIe max payload size
 * 		0 is 128 bytes
 * 		1 is 256 bytes
 * 		2 is 512 bytes
 */
extern u32 get_pcie_xfer_mps(void);

/*!
 * @brief get PCIe transfer max read request size
 *
 * @return	PCIe max read request size
 * 		0 is 128 bytes
 * 		1 is 256 bytes
 *		2 is 256 bytes
 */
extern u32 get_pcie_xfer_mrr(void);

/*!
 * @brief set pcie transfer busy to pcie, keep in L0 to avoid enter l1 too aggressive
 *
 * @param busy	set transfer busy if true
 *
 * @return	not used
 */
extern void pcie_set_xfer_busy(bool busy);

/*!
 * @brief clear all masked pcie interrupt
 *
 * @return	not used
 */
extern void pcie_isr_clear(void);

/*!
 * @brief enable ltssm to link pcie
 *
 * @return	not used
 */
extern void pcie_link_enable(void);

/*!
 * @brief set max msi x interrupt count for physical function
 *
 * @param msi_x_count	count of msix interrupt
 *
 * @return		not used
 */
extern void pcie_set_pf_max_msi_x(u32 msi_x_count);

/*!
 * @brief halt remote CPU
 *
 * @param cpu_id	cpu id to be halt, 0 base
 * @param halt		true to halt, false to let it go
 *
 * @return		not used
 */
extern void cpu_halt(int cpu_id, bool halt);

/*!
 * @brief reset remote cpu
 *
 * @param cpu_id	cpu id to be reset, 0 base
 *
 * @return		not used
 */
extern void cpu_reset(int cpu_id);

/*!
 * @brief pcie link idle check
 *
 * PCIE_PIPE_STATUS
 * Bit [3:0] pcie_core_clk_active
 * Check these 4-bit to know if PCIe Core CLK is active. Only valid before LTSSM is set to 1.
 * 0: PCIe Core CLK is active / 1: PCIe Core CLK is NOT active.
 *
 * Bit [7:4] rx_electrical_idle Lane3~Lane0, Rx Electrical Idle
 * 1: Rx Electrical idle. / 0: Rx Not electrical idle
 *
 * Bit [19:16] active_lane. Lane3~Lane0 active status after LTSSM_EN is set to 1.
 * 1: Active lane / 0: Not active lane
 *
 * @return		not used
 */
extern u32 pcie_link_idle(void);
extern u32 pcie_phy_in_l1(void);

/*!
 * @brief initialize RAID internal sram buffer
 *
 * @return		not used
 */
extern void raid_sram_init(void);

extern void pcie_phy_leq_dfe();
extern void pcie_phy_cali_results();

/*!
 * @brief pcie pipe check, only print trace
 *
 * @param cmp_mask	return cmp_mask
 *
 * @return		return cmp_ra4 in pipe
 */
extern u32 pcie_pipe_check(u32 *cmp_mask);

extern bool Chk_break(u64 time, const char *func, int line);
#if (Baidu_case)
extern void pcie_cfg_ack_freq(void);
#endif

/*!
 * @brief change the cpu clock setting
 *
 * @return		not used
 */
//extern void pmu_clk_chg(enum cpu_clk_t idx, enum eccu_clk_lv eccu_lv);

/*!
 * @brief lock other CPU in ATCM code section loop
 *
 * @param btn_reset	if btn was reset after lock
 * @param wait		sync wait lock done
 *
 * @return		true if other cpu locked
 */
extern bool rtos_lock_other_cpu(bool btn_reset, bool wait);

/*!
 * @brief unlock other CPU
 */
extern void rtos_unlock_other_cpu(void);

/*!
 * @brief check if all other cpu locked
 *
 * @return		true if other cpu locked
 */
extern bool rtos_chk_other_cpu_locked(void);

extern void HalOtpValueConfirm(void);

extern u32 AplPollingResetEvent(void);

extern void start_warm_boot_timer(void *data);
#ifdef AGING2NORMAL_DDRBYPASS	//20201123-Eddie
extern void misc_set_aging2normal_ddr_bypass_flag(void);
extern bool misc_is_aging2normal_ddr_bypass(void);
#endif
//20210426-Eddie
extern bool misc_is_CA1_active(void);
extern bool misc_is_CA3_active(void);
extern void misc_set_warm_boot_ca_mode(u32 active_type);
#ifdef MEASURE_TIME
extern u32 Misc_GetElapsedTimeMsec(u32 timestamp);
#endif
#if 1
extern void misc_set_STOP_BGREAD(void);
extern bool misc_is_STOP_BGREAD(void);
extern void misc_clear_STOP_BGREAD(void);
extern void misc_set_startwb_flag(u32 delay);
extern bool misc_is_startwb(void);
extern u32 misc_get_startwb(void);
extern void misc_clear_startwb(void);
#else
extern void misc_set_STOP_PARD(void);
extern bool misc_is_STOP_PARD(void);
extern void misc_clear_STOP_PARD(void);
extern void misc_set_STOP_REFR(void);
extern bool misc_is_STOP_REFR(void);
extern void misc_clear_STOP_REFR(void);
#endif
void misc_set_otp_deep_stdby_mode(void);
void misc_chk_otp_deep_stdby_mode(void);
extern void Assert_timer_isr(void);
extern void Assert_nvmet_isr(void);
void rdisk_pln_format_sanitize(u32 p0, u32 p1, u32 p2);
void pln_evt(void);
extern void pln_flush(void);
extern void btn_de_wr_disable(void);

/*! @} */
