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


#include "mod.h"
#include "sect.h"
#include "stdio.h"
#include "io.h"
#include "console.h"
#include "top_revision.h"
#include "rainier_soc.h"
#include "mpc.h"
#if !defined(ENABLE_SOUT)
#include "trace-fmtstr.h"
#endif
#include "mpc.h"
#define __FILEID__ version
#include "trace.h"
#include "nvme/inc/nvme_cfg.h"

/*lint -save -e750 */
#define TOP_VER_MAJOR	3
#define TOP_VER_MINOR	1
#define TOP_VER_OEM	5
#define TOP_VER_BUILD	2

#define STR(x)	#x

#define MOD_VER_STRING_BUILD(str, major, minor, oem, build)	\
	str"r"STR(major)"."STR(minor)"."STR(oem)"."STR(build)" "

#define TOP_REVISION	MOD_VER_STRING_BUILD(	\
	"top, ", TOP_VER_MAJOR, TOP_VER_MINOR, TOP_VER_OEM, TOP_VER_BUILD)

#define PROJECT "LJ1"


#ifndef GIT_RTOS_REVISION
# define GIT_RTOS_REVISION ""
#endif

#ifndef GIT_TOP_REVISION
# define GIT_TOP_REVISION 	TOP_REVISION
#endif

#ifndef VERSION
#define VERSION FR
#endif

#ifndef GIT_FE_REVISION
# define GIT_FE_REVISION ""
#endif

#ifndef GIT_FTL_REVISION
# define GIT_FTL_REVISION ""
#endif

#ifndef GIT_BE_REVISION
# define GIT_BE_REVISION ""
#endif

fast_data_zi u32 soc_svn_id;
#if defined(FPGA)
fast_data_zi soc_cfg_reg1_t soc_cfg_reg1;
fast_data_zi soc_cfg_reg2_t soc_cfg_reg2;
#endif

void *get_fw_version()
{
	return VERSION;
}

/*line -restore */
static ps_code void version_show(void)
{
	evlog_printk(LOG_ALW, "%s(SSSTC) FW %s %s%s%s%s%s(%s %s), Id(0x%x), CPU%d",
		PROJECT, VERSION, GIT_TOP_REVISION, GIT_RTOS_REVISION, GIT_FE_REVISION,
		GIT_BE_REVISION, GIT_FTL_REVISION, __DATE__, __TIME__, soc_svn_id, CPU_ID);

	evlog_printk(LOG_ALW, "enabled option:");
	evlog_printk(LOG_ALW, " "
#if defined(RAWDISK)
	       " rawdisk"
#elif defined(RDISK)
	       " rdisk"
#elif defined(PROGRAMMER)
	       " vdisk"
#elif defined(RAMDISK)
	       " ramdisk"
#if defined(RAMDISK_FULL)
		"(full)"
#endif
#endif
#ifdef RAMDISK_L2P
	       " l2p"
#endif
#if defined(DDR)
	       " ddr"
#if defined(LPDDR4)
		"(LPDDR4)"
#elif defined(DDR4)
		"(DDR4)"
#else
		"()"
#endif
#endif
#if defined(ENABLE_INLINE_ECC)
               " iecc"
#endif
#if defined(ENABLE_PARALLEL_ECC)
               " pecc"
#endif
#ifdef PERF_BUILD
	       " perf"
#endif
#ifdef FPGA
	       " fpga"
#endif
#ifdef HAVE_A0
	       " a0"
#endif
#ifdef HAVE_T0
	       " t0"
#endif
#ifdef PI_SUPPORT
	       " pi"
#endif
#ifdef USE_8K_DU
	       " du(8k)"
#else
	       " du(4k)"
#endif
#ifdef USE_TSB_NAND
	       " tsb"
#endif
#ifdef USE_MU_NAND
	       " micron"
#endif
#ifdef USE_UNIC_NAND
	       " unic"
#endif
#ifdef USE_YMTC_NAND
	       " ymtc"
#endif
#ifdef USE_HYNX_NAND
	       " hynx"
#endif
#ifdef USE_SNDK_NAND
	       " sndk"
#endif
#ifdef USE_SS_NAND
	       " ss"
#endif
#ifdef USE_512B_HOST_SECTOR_SIZE
	       " sec512"
#else
	       " sec4k"
#endif
	);
	evlog_printk(LOG_ALW, " "
#ifdef DISABLE_HS_CRC_SUPPORT
	       " no_hcrc"
#else
	       " hcrc"
#endif
#ifdef HMB_SUPPORT
               " hmb"
#endif
#ifdef ENABLE_SRAM_ECC
               " sram_ecc"
#endif
#ifdef NCL_STRESS
               " ncl_stress"
#endif
#ifdef FAST_MODE
	       " fast_mode"
#endif
#ifdef TSB_XL_NAND
	       " tsb_xl_nand"
#endif
#ifdef USE_CRYPTO_HW
	       " security"
#ifdef USE_SM4_ALGO
	       " sm4"
#else
	       " aes"
#endif
#endif
#ifdef SEMI_WRITE_ENABLE
	       " semi-write"
#endif
#ifdef BTN_STREAM_BUF_ONLY
	       " interbuf-only"
#endif
#ifdef SRIOV_SUPPORT
	       " sriov"
#else
	       " no_sriov"
#endif
#ifdef ENABLE_L2CACHE
	       " l2cache"
#endif
#ifdef ENABLE_VPD
	       " vpd"
#endif
#if RAID_SUPPORT
	       " raid"
#endif
#ifdef LJ_Meta
	       " LJ_Meta"
#endif
#ifdef M2_0305
	       " m2_0305"
#endif
#ifdef M2_2A
	       " m2_2a"
#endif
#ifdef skip_mode
	       " skip mode ftl"
#endif

#ifdef hlba_8_bytes
	       " hlba 8 bytes"
#endif
#ifdef U2_0504
	       " u2_0504"
#endif
#ifdef U2_LJ
	       " u2_lj"
#endif
	    );

#if defined(FPGA)
	soc_cfg_reg1_t reg1 = {.all = soc_cfg_reg1.all};
	soc_cfg_reg2_t reg2 = {.all = soc_cfg_reg2.all};

	/* http://10.10.0.17/issues/2974 */
	evlog_printk(LOG_ALW, "cfg1: %x, CPU1:%d, CPU2:%d, CPU3:%d, CPU4:%d",
	       reg1.all, reg1.b.cpu1_exist, reg1.b.cpu2_exist,reg1.b.cpu3_exist,reg1.b.cpu4_exist);
	evlog_printk(LOG_ALW, "CPU5:%d, CPU6:%d, CPU7:%d, CPU8:%d DDR:%s, L2C %d, NCB %d",
	       reg1.b.cpu5_exist,reg1.b.cpu6_exist,reg1.b.cpu7_exist,reg1.b.cpu8_exist,
	       ((reg1.b.ddr != 0) ? ( (reg1.b.ddr ==1) ? "40 BIT" : "72 BIT" ) : "No"), reg1.b.l2c, reg1.b.ncb);

	evlog_printk(LOG_ALW, "cfg2: %x, NVM %d, SRV %d, HMTBL %d, AES %d, CMB %d",
	       reg2.all, reg2.b.nvm_en, reg2.b.sriov, reg2.b.hmb_tbl,
	       reg2.b.aes, reg2.b.nvm_cmb);
	evlog_printk(LOG_ALW, "L2P %d, HWS %d, DPE %d, CYT %d, MAC %d, HMBDTG %d\n",
	       reg2.b.btn_l2p, reg2.b.btn_hw_srch,
	       reg2.b.btn_dpe, reg2.b.btn_crypto, reg2.b.pcie_mac,
	       reg2.b.hmb_dtag);
#endif
#if !defined(ENABLE_SOUT)
	evlog_printk(LOG_ALW, "fmtstr crc: %x\n", EVID_FMT_CRC32);
#endif
}

#ifndef HAVE_VELOCE
module_init(version_show, RTOS_VER);
#endif

static ps_code int version_main(int argc, char *argv[])
{
	version_show();
	return 0;
}

static DEFINE_UART_CMD(fw_version, "fw_version", "fw_version", "fw_version", 0, 0, version_main);
