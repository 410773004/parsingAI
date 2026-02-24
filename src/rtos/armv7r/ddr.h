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

/*! \file ddr.h
 * @brief ddr initialization
 *
 * \addtogroup rtos
 * \defgroup ddr
 * \ingroup rtos
 *
 * {@
 */
#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#if defined(LOADER)//defined(PROGRAMMER)
#else
#include "bf_mgr.h"
#endif
#include "../ncl_20/nand_cfg.h"
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#if defined(MPC) && defined(RAWDISK)
#define DDR_DTAG_CNT		256	///< can't afford so much DTAG due, need lots of ncl command to flush
#elif defined(FORCE_IO_SRAM)
#define DDR_DTAG_CNT		256
#elif defined(RAMDISK)
#define DDR_DTAG_CNT		512
#elif defined(RAWDISK)
#define DDR_DTAG_CNT		512
#else
// #ifndef NS_MANAGE
// #define DDR_DTAG_CNT		(1536)  //3072  // adams_SeqW
// #else
#define DDR_DTAG_CNT		(3072)  //3072  // adams_SeqW
// #endif
#endif
BUILD_BUG_ON(DDR_DTAG_CNT%32 != 0);
#define L2P_DDTAG_ALLOC	//20201029-Eddie-l2p Tbl fixed in DRAM from end

#define OFFSET_DDR_DTAG   //20201106-Eddie-Leave 100 MB DDR space dor code

// #define L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie

#define EPM_DDTAG_ALLOC	//20210527-Eddie

#ifdef EPM_DDTAG_ALLOC
#define EPM_ddr_capacity		4
#endif

#ifdef L2PnTRIM_DDTAG_ALLOC
#define TRIM_ddr_8T_capacity		256	//8T : TRIM bitmap = 256 MB, actual : 224MB
#define TRIM_ddr_4T_capacity		(TRIM_ddr_8T_capacity/2)	// 4T : TRIM bitmap = 128 MB, actual : 112MB
#endif

#ifdef L2P_DDTAG_ALLOC
#define L2P_ddr_8T_capacity		7172	//8T : 7172 MB
#define L2P_ddr_4T_capacity		(L2P_ddr_8T_capacity/2)	// 4T : 3586 MB
#define L2P_ddr_2T_capacity		(L2P_ddr_8T_capacity/4)	// 4T : 1789 MB
#define L2P_ddr_1T_capacity		(L2P_ddr_8T_capacity/8)	// 4T : 897 MB
#define L2P_ddr_512G_capacity		(L2P_ddr_8T_capacity/16)	// 4T : 449 MB
//#define L2P_FROM_DDREND
#endif

#define DDR_DTAG_CNT_2T 		3072
#define DDR_DTAG_CNT_1T 		1536


#define MAX_DDR_GC_DTAG_CNT	(64 * 4 * 4 * 4) ///< MAX_DIE * MAX_PLANE * QLC * DU_CNT_PER_PAGE
#define MOVE_DDRINIT_2_LOADER		//20200727-Eddie
#define BYPASS_DDRINIT_WARMBOOT
#define DDR_IO_DTAG_START	0
#define DDR_RD_RECOVERY_0	(DDR_DTAG_CNT)
#define DDR_RD_RECOVERY_1	(DDR_RD_RECOVERY_0 + 1)

#define DDR_PA_RD_DTAG_START    (DDR_DTAG_CNT + 32)
#define DDR_PA_RD_DTAG_CNT   (PARD_DIE_AIPR*XLC)
#define DDR_GC_DTAG_START	(DDR_PA_RD_DTAG_START + DDR_PA_RD_DTAG_CNT)

#define DDR_BLIST_MAX_CNT   14
#define DDR_RD_REWRITE_START (DDR_BLIST_DTAG_START + DDR_BLIST_MAX_CNT + 32)  //(DDR_GC_DTAG_START + 4096 + 32)
#define DDR_RD_REWRITE_TTL_CNT    48
//#define DDR_RD_REWRITE_START  ((DDR_GC_DTAG_START + FCORE_GC_DTAG_CNT) - DDR_RD_REWRITE_TTL_CNT)  //(DDR_GC_DTAG_START + 4096 + 32)
#define DDR_TRIM_RANGE_START (DDR_RD_REWRITE_START + DDR_RD_REWRITE_TTL_CNT + 1)  //(DDR_GC_DTAG_START + 4096 + 32)
#define DDR_TRIM_RANGE_DTAG_CNT    63

#define DDR_WUNC_DDUMMY_DTAG_START (DDR_TRIM_RANGE_START + DDR_TRIM_RANGE_DTAG_CNT + 32)
#define DDR_WUNC_DDUMMY_DTAG_CNT   (1)

#define DDR_RD_RECOVERY_EX_START (DDR_WUNC_DDUMMY_DTAG_START + DDR_WUNC_DDUMMY_DTAG_CNT + 32)
#define DDR_RD_RECOVERY_EX_CNT (8 * 8)  //8CH * 8DU

#define DDR_RA_DTAG_START   (DDR_RD_RECOVERY_EX_START + DDR_RD_RECOVERY_EX_CNT + 1)
#define DDR_RA_RD_DTAG_CNT   384//TODO ra.h/ra.c need the same, revise later

#define DDR_REFRESH_TIME_TEMPERATURE_THRESHOLD 70+273
#define DDR_REFRESH_TIME_7p8_us 0xC3
#define DDR_REFRESH_TIME_3p9_us 0x62
#define Invalid_Temperature 0x80 +273

extern volatile u32 max_ddr_dtag_cnt;
#define MAX_DDR_DTAG_CNT	(max_ddr_dtag_cnt)

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef enum {
	DDR_TYPE_DDR4 = 0,
	DDR_TYPE_LPDDR4
} ddr_cfg_type_t;

typedef enum {
	DDR_SPEED_800 = 0,
	DDR_SPEED_1600,
	DDR_SPEED_2400,
	DDR_SPEED_2666,
	DDR_SPEED_3200,
} ddr_cfg_speed_t;

typedef enum {
	DDR_SIZE_256M = 0,
	DDR_SIZE_512M,
	DDR_SIZE_1024M,
	DDR_SIZE_2048M,
	DDR_SIZE_4096M,
	DDR_SIZE_8192M,
	DDR_SIZE_16384M,	//20200720-Eddie
} ddr_cfg_size_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/*!
 * @brief get ddr capacity
 *
 * @return	ddr capacity in bytes
 */
u64 ddr_get_capapcity(void);

/*!
 * @brief initialize ddr, start ddr training and create ddr dtag pool
 *
 * @return	not used
 */
void ddr_init(void);

#ifdef BYPASS_DDRINIT_WARMBOOT
void ddr_init_bypass_warmboot(void);
#endif

void ddr_init_complement(void);
/*!
 * @brief register ddr dtag interface
 *
 * @param priority	number of dtag
 *
 * @return		start dtag / ~0 for allocate fail
 */
u32 ddr_dtag_register(u32 dtag_num);

/*!
 * @brief lock ddr dtag register
 *
 * @return		not used
 */
void ddr_dtag_register_lock(void);

//#define DRAM_Error_injection

#if defined(DRAM_Error_injection)
void Error_injection_1bit(void);
void Error_injection_2bit(void);
#endif
/*!
 * @brief api to set ddr cpu window, without mpu, cpu windows only 1GB
 *
 * @param ddr_seg	ddr segment id
 *
 * @return		not used
 */
 #ifdef EPM_DDTAG_ALLOC	//20210527-Eddie
 void ddr_dtag_epm_register_lock(void);
 u32 ddr_dtag_epm_register(u32 dtag_num);
 #endif
 #ifdef L2PnTRIM_DDTAG_ALLOC		//20201207-Eddie
 void ddr_dtag_trim_register_lock(void);
 u32 ddr_dtag_trim_register(u32 dtag_num);
 #endif
 #ifdef L2P_DDTAG_ALLOC
 u32 ddr_dtag_l2p_register(u32 dtag_num);
 void ddr_dtag_l2p_register_lock(void);
 #endif
void ddr_setup_window(u32 ddr_seg);

/*!
 * @brief get ddr info buffer
 *
 * @return	ddr pointer of ddr info buffer
 */
void* get_ddr_info_buf(void);

/*!
 * @brief is ddr training done
 *
 * @return	true if ddr training done
 */
bool is_ddr_training_done(void);

/*!
 * @brief set ddr bkup fwconfig done
 *
 * @return	not used
 */
void set_ddr_info_bkup_fwconfig_done(void);
void ddr_modify_refresh_time(u16 current_temperature);

//20200720-Eddie
void memprint(char *str, void *ptr, int mem_len);
#define GET_CFG	//20200822-Eddie

#ifdef GET_CFG	//20200822-Eddie
extern void ddr_set_capacity(u64 size);
extern volatile u64 ddr_capacity;
#endif

#if defined(PROGRAMMER)
/*!
 * @brief train DDR in programmer
 */
void ddr_train(ddr_cfg_type_t type, ddr_cfg_speed_t, ddr_cfg_size_t);
#endif



/*! @} */

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
u32 ddr_dtag_telemetry_register(u32 dtag_num);
#endif

