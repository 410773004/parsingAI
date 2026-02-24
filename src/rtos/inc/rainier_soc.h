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

/*!
 * \file rainier_soc.h
 * @brief define rainier memory address and size
 *
 * \addtogroup system
 * @{
 */

#pragma once
#define ATCM_BASE    0x00000000
#define ATCM_SIZE    0x00018000

#define BTCM_BASE    0x00080000
#define BTCM_SIZE    0x00030000

#define BTCM_SH_BASE 0x000B0000
#define BTCM_SH_SIZE 0x00020000

#define L2CRAM_BASE  0x10020000
#define L2CRAM_SIZE  0x00020000  //128KB

#define SRAM_BASE    0x20000000
#define SRAM_SIZE    0x00200000	// 1024K in vc709 fpga
#define ALIGNED_SRAM_SIZE	0x00200000

#define RAID_BASE    0x20200000
#define RAID_SIZE    0x00086000

#if CPU_BE2 == CPU_ID
#define SPRM_OFF     0x40000
#define NCB_OFF      0xc0000
#else
#define SPRM_OFF     0x0
#define NCB_OFF      0x0
#endif

#define SPRM_BASE_ADJ    (0x20300000 + SPRM_OFF)
#define SPRM_BASE    0x20300000
#define SPRM_SIZE    0x00028000 /* 128kbyte + 32kbyte */

#define DDR_BASE     0x40000000
#define DDR_SIZE     0x80000000

#define DDR_SH_BASE     0x42800000
#define DDR_SH_SIZE     0x00200000

#define CPU1_ATCM_BASE      0x00100000
#define CPU1_ATCM_SIZE      ATCM_SIZE

#define CPU1_BTCM_BASE      0x00120000
#define CPU1_BTCM_SIZE      BTCM_SIZE

#define CPU2_ATCM_BASE      0x00180000
#define CPU2_ATCM_SIZE      ATCM_SIZE

#define CPU2_BTCM_BASE      0x001A0000
#define CPU2_BTCM_SIZE      BTCM_SIZE

#define CPU3_ATCM_BASE      0x00200000
#define CPU3_ATCM_SIZE      ATCM_SIZE

#define CPU3_BTCM_BASE      0x00220000
#define CPU3_BTCM_SIZE      BTCM_SIZE

#define CPU4_ATCM_BASE      0x00280000
#define CPU4_ATCM_SIZE      ATCM_SIZE

#define CPU4_BTCM_BASE      0x002A0000
#define CPU4_BTCM_SIZE      BTCM_SIZE

#define ROM_BASE	0x10000000
#define NCB_BASE	(0xC0000000 + NCB_OFF)
#define BM_BASE 	0xC0010000
#define PCIE_CORE_BASE	0xC0020000
#define NVME_BASE	0xC0030000
#define NVME_VF_BASE	0xC0033000
#define MISC_BASE	0xC0040000
#define PCIE_WRAP_BASE	0xC0043000 ///< PCIe wrapper base address
#define UART_BASE	0xC0050000
#define MC0_BASE	0xC0060000
#define DFI_BASE	0xC0064000
#define DDR_TOP_BASE	0xc0068000
#define BTN_ME_BASE	0xC00D0000
#define IPC_BASE	0xC00F0000
#define SPIN_LOCK_BASE	0xC00F1000
#define VIC_BASE	0xC0200000
#define TIMER_BASE	0xC0201000
#define CPU_CONF_BASE	0xC0204000
#define TRNG_BASE	0xC0040400 ///< TRNG registers base address
#define OTP_BASE	0xC0042000 ///< OTP register base

#ifdef HAVE_VELOCE
	#define SYS_CLK (200 * 1000 * 1000) // freq for current Veloce
#else
	#if defined(FPGA)
		#define SYS_CLK (16 * 1000 * 1000) // freq for current vc709 bitfile
	#else
		#define SYS_CLK _SYS_CLK
	#endif
#endif

extern u32       soc_svn_id;

#if defined(FPGA)
/* http://10.10.0.17/issues/2974 */
typedef union {
	u32 all;
	struct {
		u32 cpu1_exist:1;
		u32 cpu2_exist:1;
		u32 cpu3_exist:1;
		u32 cpu4_exist:1;
		u32 cpu5_exist:1;
		u32 cpu6_exist:1;
		u32 cpu7_exist:1;
		u32 cpu8_exist:1;
		u32 M0_exist:1;
		u32 rsvd0:3;
		u32 ddr:2;
		u32 l2c:1;
		u32 ncb:1;

		u32 rsvd1:16;
	} b;
} soc_cfg_reg1_t;

typedef union {
	u32 all;
	struct {
		u32 rsvd:20;

		u32 hmb_dtag:1;
		u32 pcie_mac:2;
		u32 btn_crypto:1;

		u32 btn_dpe:1;
		u32 btn_hw_srch:1;
		u32 btn_l2p:1;
		u32 nvm_cmb:1;
		u32 aes:1;
		u32 hmb_tbl:1;
		u32 sriov:1;
		u32 nvm_en:1;
	} b;
} soc_cfg_reg2_t;

extern soc_cfg_reg1_t soc_cfg_reg1;
extern soc_cfg_reg2_t soc_cfg_reg2;

#endif

/*! @} */
