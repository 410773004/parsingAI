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
/*! \file nand.c
 * @brief nand initialization and driver files
 *
 * \addtogroup ncl_20
 * \defgroup nand
 * \ingroup ncl_20
 *
 * @{
 */
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "eccu_reg_access.h"
#include "ncb_eccu_register.h"
#include "nand_cfg.h"
#include "ncl.h"
#include "ndcu.h"
#include "ndphy.h"
#include "ficu.h"
#include "nand.h"
/*! \cond PRIVATE */
#define __FILEID__ nand
#include "trace.h"
#include "srb.h"
/*! \endcond */

/*! Compact page parameter info for devices that do not have parameter page */
struct compact_page_param {
	u8 id[6];
	u8 id_len;
	u8 nr_luns;
	u8 nr_planes;
	u8 bit_per_cell;
	u8 addr_cycles;
	u8 max_tm;
	u16 speed_grade;
	u16 nr_blocks;
	u16 nr_pages;
	u16 page_sz;
	u16 spare_sz;
};

init_data struct compact_page_param support_devices[] = {
#if HAVE_TSB_SUPPORT
#if QLC_SUPPORT
	{
		.id = {0x98, 0x7E, 0x1E, 0xB3, 0x7E, 0xF2},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x1FF,//to be determined
		.bit_per_cell = 4,
		.addr_cycles = 0x23,
		.nr_luns = 1,//4, shasta 4LUN * 8 plane seems not work
		.nr_planes = 2,//8
		.nr_blocks = 1687,
		.nr_pages = 0x400,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
#else
	{
		// BiCS5
		.id = {0x98, 0x3E, 0x98, 0x03, 0x76, 0xE4},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x1FFF,//1200MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 2,
		.nr_blocks = 0x67E,// 3324/2
		.nr_pages = 0x540,//1344
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		.id = {0x98, 0x3E, 0xA8, 0x03, 0x7A, 0xE4}, //Sean_20220720_TH58LKT1Y45BA8C
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x1FFF,//1200MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
        .nr_planes = 4,
        .nr_blocks = 0x33F,  //831
		.nr_pages = 0x540,//1344
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		.id = {0x98, 0x48, 0xA9, 0x03, 0x7E, 0xE4}, //Sean_20230630_TH58LKT2Y45BA8H
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x1FFF,//1200MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 2,
        .nr_planes = 4,
        .nr_blocks = 0x33F,  //831
		.nr_pages = 0x540,//1344
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		// BiCS4 that support 800MT/s, copy from and modify based on BiCS4 (98h 48h 99h B3h 7Ah E3h)
		.id = {0x98, 0x49, 0x9A, 0xB3, 0x7E, 0xE3},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x7FF,//800MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 4,//adams BiCS4 16DP 512Gb
		.nr_planes = 2,
		.nr_blocks = 0x7A6,
		.nr_pages = 0x480,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		// BiCS4 that support 800MT/s, copy from and modify based on BiCS4 (98h 48h 99h B3h 7Ah E3h)
		.id = {0x98, 0x48, 0x9A, 0xB3, 0x7E, 0xE3},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x7FF,//800MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 4,
		.nr_planes = 2,
		.nr_blocks = 990,//adams BiCS4 16DP 256Gb
		.nr_pages = 0x480,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		// BiCS4 that support 800MT/s, copy from and modify based on BiCS4 (98h 48h 99h B3h 7Ah E3h)
		.id = {0x98, 0x3E, 0x99, 0xB3, 0x7A, 0xE3},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x7FF,//800MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 2,
		.nr_planes = 2,
		.nr_blocks = 990,//adams BiCS4 256Gb 8DP EVB
		.nr_pages = 0x480,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		// BiCS4 that support 800MT/s, copy from and modify based on BiCS4 (98h 48h 99h B3h 7Ah E3h)
		.id = {0x98, 0x48, 0x99, 0xB3, 0x7A, 0xE3},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x7FF,//800MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 2,
		.nr_planes = 2,
		.nr_blocks = 1958,//0x7A6, //
		.nr_pages = 0x480,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		//HDR BICS4
		.id = {0x98, 0x3E, 0x99, 0xB3, 0xFA, 0xE3},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0xFFF,//1066MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 2,
		.nr_planes = 2,
		.nr_blocks = 0x3CE,
		.nr_pages = 0x480,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
	{
		// TSV nand
		.id = {0x98, 0x19, 0xAA, 0xB3, 0x7E, 0xF1},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL2 | 0,
		.speed_grade = 0x7FF,//800MT/s
		.bit_per_cell = 3,
		.addr_cycles = 0x24,
		.nr_luns = 2,
		.nr_planes = 4,
		.nr_blocks = 0x7AC,
		.nr_pages = 0x240,
		.page_sz = 16384,
		.spare_sz = 0x7A0,
	},
#endif
#endif
#if HAVE_UNIC_SUPPORT
#if QLC_SUPPORT
	{
		.id = {0x89, 0xD4, 0x0C, 0x32, 0xAA},
		.id_len = 5,
		.max_tm = NDCU_IF_DDR2 | 0,
		.speed_grade = 0x0,//to be determined
		.bit_per_cell = 4,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 4,
		.nr_blocks = 736,
		.nr_pages = 0xC00,
		.page_sz = 16384,
		.spare_sz = 2208,//0x8A0,
	},
#else
	{
		.id = {0x89, 0xc4, 0x89, 0x32, 0xA1},
		.id_len = 5,
		.max_tm = NDCU_IF_DDR2 | 0,
		.speed_grade = 0x0,//to be determined
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 2,
		.nr_planes = 2,
		.nr_blocks = 504,
		.nr_pages = 0x900,
		.page_sz = 16384,
		.spare_sz = 2208,//0x8A0,
	},
	{
		.id = {0x89, 0xA4, 0x08, 0x32, 0xA1},
		.id_len = 5,
		.max_tm = NDCU_IF_DDR2 | 0,
		.speed_grade = 0x0,//to be determined
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 2,
		.nr_blocks = 504,
		.nr_pages = 0x900,
		.page_sz = 16384,
		.spare_sz = 2208,//0x8A0,
	},
	{
		.id = {0x89, 0xC4, 0x08, 0x32, 0xA6},
		.id_len = 5,
		.max_tm = NDCU_IF_DDR2 | 0,
		.speed_grade = 0x7FF,//800MT/s, Micron B17 spec only support 533/666 for DDR2/DDR3, but DLL calibration pass on 800MT/s DDR2
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 4,
		.nr_blocks = 504,
		.nr_pages = 0x900,
		.page_sz = 16384,
		.spare_sz = 2208,//0x8A0,
	},

	{ ///< Micron B27B
		.id = {0x89, 0xC3, 0x08, 0x32, 0xE6},
		.id_len = 5,
		.max_tm = NDCU_IF_DDR2 | 0,
		.speed_grade = 0x1FF,
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 4,
		.nr_blocks = 338, //338 blocks per plane
		.nr_pages = 0xD80,
		.page_sz = 16384,
		.spare_sz = 2208,//0x8A0,
	},

#endif
#endif
#if HAVE_HYNIX_SUPPORT
	{
		.id = {0xAD, 0x5E, 0x28, 0x22, 0x10, 0x90},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL1 | 0,
		.speed_grade = 0x3FF,//to be determined
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 4,
		.nr_blocks = 635,
		.nr_pages = 1728,
		.page_sz = 8192,
		.spare_sz = 1024,
	},
	{
		.id = {0xAD, 0x89, 0x28, 0x53, 0x00, 0xB0},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL1 | 0,
		.speed_grade = 0x3FF,//to be determined
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 4,
		.nr_blocks = 1366 + 72,
		.nr_pages = 1536,
		.page_sz = 16384,
		.spare_sz = 2048,
	},
#endif
#if HAVE_SANDISK_SUPPORT
	{
		.id = {0x45, 0x3E, 0x98, 0xB3, 0x76, 0x72},
		.id_len = 6,
		.max_tm = NDCU_IF_TGL1 | 0,
		.speed_grade = 0x1FF,
		.bit_per_cell = 3,
		.addr_cycles = 0x23,
		.nr_luns = 1,
		.nr_planes = 2,
		.nr_blocks = 723,
		.nr_pages = 768,
		.page_sz = 16384,
		.spare_sz = 1952,//0x7A0
	},
#endif
};


struct nand_codename_t {
	u8 id[8];
	u8 id_len;
	char name[10];
};

slow_data struct nand_codename_t nand_codename[] =
{
	{{0x2C, 0xD4, 0x89, 0x32, 0xA6}, 5, "B17A"},
	{{0x89, 0xD4, 0x89, 0x32, 0xA6}, 5, "IntelB17A"},
	{{0x2C, 0xC3, 0x08, 0x32, 0xE6}, 5, "B27B"},
	{{0x2C, 0xD3, 0x89, 0x32, 0xE6}, 5, "B27B"},
	{{0x98, 0x3C, 0x98, 0xB3, 0x76, 0xF2}, 6, "BiCS3"},
	{{0x98, 0x3E, 0x98, 0xB3, 0x76, 0xE3}, 6, "BiCS4"},
	{{0x98, 0x49, 0x9A, 0xB3, 0x7E, 0xE3}, 6, "BiCS4 800"},
	{{0xAD, 0x89, 0x28, 0x53, 0x00, 0xB0}, 6, "3DV6"},
	{{0x45, 0x3E, 0x98, 0xB3, 0x76, 0x72}, 6, "BiCS3"},
	{{0x9B, 0xC4, 0x28, 0x49, 0x20}, 5, "X2-9060"},// 128 layer
	{{0x9B, 0xC3, 0x48, 0x25, 0x10}, 5, "X1-9050"},// 64 layer
};

/*! Set default max channel number */
norm_ps_data u32 max_channel = MAX_CHANNEL;
/*! Set default max target(ce) number */
norm_ps_data u32 max_target = MAX_TARGET;
norm_ps_data u32 max_lun = MAX_LUN;
//norm_ps_data struct nand_info_t nand_info;
fast_data_zi struct nand_info_t nand_info;  // ok adams
fast_data_zi u8 ce_exist_bmp[MAX_CHANNEL] = {0};
norm_ps_data u32 channel_l2p_mapping = 0x76543210;
norm_ps_data u32 channel_p2l_mapping = 0xFFFFFFFF;
#if TACOMA12
norm_ps_data u32 channel_l2p_mapping_hi = 0xFEDCBA98;
norm_ps_data u32 channel_p2l_mapping_hi = 0xFFFFFFFF;
#endif
/*!
 * @brief Wait indirect mode done
 *
 * @return Not used
 */
fast_code void ndcu_wait_cmd_end(void)
{
	nf_ind_reg0_t reg0;
	do {
		reg0.all = ndcu_readl(NF_IND_REG0);
	} while(reg0.b.ind_start);
}

#define POLL_READY_MAX_LOOP 1000

/*!
 * @brief Change flash lun
 *
 * @param[in] ch		flash channel
 * @param[in] ce		flash ce
 *
 * @return Not used
 */
fast_code void nand_lun0_select(u8 ch, u8 ce)
{
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 3,
		.reg1.b.ind_byte0 = 0x78,
		.reg1.b.ind_byte1 = 0x00,
		.reg1.b.ind_byte2 = 0x00,
		.reg1.b.ind_byte3 = 0x00,
		.xfcnt = 0,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_close(&ctrl);
}

/*!
 * @brief Read flash status
 *
 * @param[in] ch		flash channel
 * @param[in] ce		flash ce
 * @param[in] cmd		flash cmd
 *
 * @return value(flash status)
 */
fast_code u8 nand_read_sts(u8 ch, u8 ce, u8 cmd)
{
	u8 sts;

	ndcu_ind_t ctrl = {
		.write = false,
		.cmd_num = 0,
		.reg1.b.ind_byte0 = cmd,
		.xfcnt = 1,
		.buf = &sts,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_delay(tWHR);
	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);

	return sts;
}

/*!
 * @brief Wait flash ready
 *
 * @param[in] ch		flash channel
 * @param[in] ce		flash ce
 *
 * @return value(flash status)
 */
fast_code u8 nand_wait_ready(u8 ch, u8 ce)
{
	int loop = POLL_READY_MAX_LOOP;

	u8 sts;
	do {
		sts = nand_read_sts(ch, ce, 0x70);
		if (sts == NO_DEV_SIGNAL) {
			break;
		}
		if (sts & BIT6) {
			if (sts & BIT0) {
				ncb_nand_trace(LOG_WARNING, 0xf601, "SR fail %b", sts);
			}
			break;
		}
	} while (loop--);

	return sts;
}

/*!
 * @brief Reset all flash
 *
 * @return Not used
 */
fast_code void nand_reset_all(void)
{
	u32 ch, ce;
#if HAVE_SANDISK_SUPPORT
	// Sandisk nand 0xFF will not reset set feature value, need 0x89
	ncb_nand_trace(LOG_DEBUG, 0x700f, "Reset set feature value");
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			ndcu_ind_t ctrl = {
				.write = true,
				.cmd_num = 0,
				.reg1.b.ind_byte0 = 0x89,
				.xfcnt = 0,
			};
			ndcu_open(&ctrl, ch, ce);
			ndcu_start(&ctrl);
			ndcu_close(&ctrl);
		}
	}


	// Poll all nand reset ready
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			nand_wait_ready(ch, ce);
		}
	}
#endif
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			ndcu_ind_t ctrl = {
				.write = true,
				.cmd_num = 0,
				.reg1.b.ind_byte0 = 0xFF,
				.xfcnt = 0,
			};
			ndcu_open(&ctrl, ch, ce);
			ndcu_start(&ctrl);
			ndcu_close(&ctrl);
		}
	}

	ncb_nand_trace(LOG_DEBUG, 0x95ec, "Reset all nand");
    mdelay(5);//adams
	// Poll all nand reset ready
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			nand_wait_ready(ch, ce);
		}
	}
}

// 20210709 Jamie, 78h read status
fast_code u8 nand_read_sts_78h(u8 ch, u8 ce, u8 lun)
{
	u8 sts;
    u32 row;
    row = row_assemble(lun, 0, 0, 0);
    //printk("lun %d row 0x%x\n", lun, row);
	ndcu_ind_t ctrl = {
		.write = false,
		.cmd_num = 3,
		.reg1.b.ind_byte0 = 0x78,
		.reg1.b.ind_byte1 = row & 0xFF,
		.reg1.b.ind_byte2 = (row >> 8) & 0xFF,
		.reg1.b.ind_byte3 = (row >> 16) & 0xFF,
		.xfcnt = 1,
		.buf = &sts,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_delay(tWHR);
	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);

	return sts;
}


// 20210709 Jamie, 78h polling
fast_code u8 nand_wait_ready_78h(u8 ch, u8 ce, u8 lun)
{
	int loop = POLL_READY_MAX_LOOP;
	u8 sts;
	do {
		sts = nand_read_sts_78h(ch, ce, lun);
		if (sts == NO_DEV_SIGNAL) {
			break;
		}
		if (sts & BIT6) {
			if (sts & BIT0) {
				ncb_nand_trace(LOG_WARNING, 0xeb49, "SR fail %b", sts);
			}
			break;
		}
	} while (loop--);

	return sts;
}

fast_code void nand_reset_FD(u8 ch, u8 ce, u8 lun)
{
    nand_wait_ready_78h(ch, ce, lun);
    
    ndcu_ind_t ctrl = {
        .write = true,
        .cmd_num = 0,
        .reg1.b.ind_byte0 = 0xFD,
        .xfcnt = 0,
    };
    ndcu_open(&ctrl, ch, ce);
    ndcu_start(&ctrl);
    ndcu_close(&ctrl);
    ndcu_delay(5);

    //nand_wait_ready_78h(ch, ce, lun);
}

#if BYPASS_NAND_DETECTION
/*!
 * @brief Set a default nand flash type
 *
 * @return Not used
 */
fast_code void nand_detect_fake(void)
{
#if HAVE_ONFI_SUPPORT
	nand_info.device_class = ONFI_NAND;
	nand_info.id[0] = 0x2C;// B17A nand
	nand_info.id[1] = 0xC4;
	nand_info.id[2] = 0x08;
	nand_info.id[3] = 0x32;
	nand_info.id[4] = 0xA6;
	nand_info.id[5] = 0x01;
#endif
#if HAVE_TOGGLE_SUPPORT
	nand_info.device_class = TOGGLE_NAND;
	nand_info.id[0] = 0x98;// BiCS3
	nand_info.id[1] = 0x3E;
	nand_info.id[2] = 0x99;
	nand_info.id[3] = 0xB3;
	nand_info.id[4] = 0x7A;
	nand_info.id[5] = 0xF2;
#endif
	nand_info.nr_id_bytes = 6;
	nand_info.geo.nr_channels = max_channel;
	nand_info.geo.nr_targets = max_target;
	ncb_nand_trace(LOG_ERR, 0xe78e, "Fake read ID ch%d ce%d:", max_channel, max_target);
	ncb_nand_trace(LOG_ERR, 0x5644, "    %b %b %b %b %b %b", nand_info.id[0], nand_info.id[1], nand_info.id[2], nand_info.id[3], nand_info.id[4], nand_info.id[5]);
	ncb_nand_trace(LOG_INFO, 0x32c1, "CH %d CE %d", nand_info.geo.nr_channels, nand_info.geo.nr_targets);
}
#endif

/*!
 * @brief Detect nand flash type
 *
 * @return Not used
 */
fast_code void nand_detect(void)
{
	u32 ch;
	bool support;
	u32 nr_id_bytes = NAND_ID_LENGTH;
	u32 ch_remap = 0, ce_remap;
	u32 ch_cnt, ce_cnt;
	bool ch_exist;
	bool ce_stop = false;
	bool ce_consective = true;
	bool first_ce = true;


	nand_info.geo.nr_channels = 0;
	ch_cnt = 0;
	for (ch = 0; ch < max_channel; ch++) {
		u32 ce;
		ch_exist = false;
		ce_stop = false;
		ce_remap = 0;
		ce_cnt = 0;
		for (ce = 0; ce < max_target; ce++) {
			support = true;
			u8 id[NAND_ID_LENGTH];
			u32 i;
			ndcu_ind_t ctrl = {
				.write = false,
				.cmd_num = 1,
				.reg1.b.ind_byte0 = 0x90,
				.reg1.b.ind_byte1 = NAND_MANU_ID_ADDR,
				.xfcnt = sizeof(id),
				.buf = id,
			};

			ndcu_open(&ctrl, ch, ce);
			ndcu_start(&ctrl);
			ndcu_delay(tWHR);
			do {
				if (ndcu_xfer(&ctrl))
					break;
			} while (1);
			ndcu_close(&ctrl);

#if HAVE_ONFI_SUPPORT
			if (id[1] == NAND_ID_VENDOR) {
				if (first_ce) {
					ncb_nand_trace(LOG_ERR, 0x4857, "IO volitage is 1.2V\n");
				}
				for (i = 0; i < NAND_ID_LENGTH - 1; i++) {
					id[i] = id[i + 1];
				}
				id[NAND_ID_LENGTH - 1] = 0;
				// 1.2V default to NVDDR3 mode
				nand_info.def_tm = NDCU_IF_DDR3 | 0;
				nand_info.vcc_1p2v = true;
			}
#endif
			if (first_ce) {
				ncb_nand_trace(LOG_ERR, 0xc237, "Read ID ch%d ce%d:", ch, ce);
				ncb_nand_trace(LOG_ERR, 0x2840, "    %b %b %b %b %b %b", id[0], id[1], id[2], id[3], id[4], id[5]);

				for (i = 0; i < sizeof(nand_codename) / sizeof(nand_codename[0]); i++) {
					if (memcmp(id, nand_codename[i].id, nand_codename[i].id_len) == 0) {
						ncb_nand_trace(LOG_ERR, 0x27ef, " (%s)", nand_codename[i].name);
						break;
					}
				}
				ncb_nand_trace(LOG_ERR, 0xe06f, "\n");

				if (id[0] != NAND_ID_VENDOR) {
					ncb_nand_trace(LOG_ERR, 0x5c5e, "Read ID ch%d ce%d:", ch, ce);
					ncb_nand_trace(LOG_ERR, 0xaad7, "    %b %b %b %b %b %b", id[0], id[1], id[2], id[3], id[4], id[5]);
					ncb_nand_trace(LOG_WARNING, 0xeedb, "expect ID %b", NAND_ID_VENDOR);
				}
#if HAVE_VELOCE
				sys_assert(id[0] == NAND_ID_VELOCE || id[0] == NAND_ID_VENDOR);
#else
				sys_assert(id[0] == NAND_ID_VENDOR);
#endif
				switch (id[0]) {
#ifdef HAVE_VELOCE
				case NAND_ID_VELOCE:
#endif
				case NAND_ID_TOSHIBA:
					ncb_nand_trace(LOG_DEBUG, 0xd0ce, "TSB Toggle nand");
					support = true;
					nand_info.device_class = TOGGLE_NAND;
					break;
				case NAND_ID_HYNIX:
					ncb_nand_trace(LOG_DEBUG, 0x12b1, "Hynix Toggle nand");
					support = true;
					nand_info.device_class = TOGGLE_NAND;
					break;
				case NAND_ID_SANDISK:
					ncb_nand_trace(LOG_DEBUG, 0x36f8, "Sandisk Toggle nand");
					support = true;
					nand_info.device_class = TOGGLE_NAND;
					break;
				case NAND_ID_SAMSUNG:
					ncb_nand_trace(LOG_DEBUG, 0xec68, "Samsung Toggle nand");
					support = true;
					nand_info.device_class = TOGGLE_NAND;
					break;
				case NAND_ID_MICRON:
					ncb_nand_trace(LOG_DEBUG, 0x5816, "Micron ONFI nand");
					support = true;
					nand_info.device_class = ONFI_NAND;
					break;
				case NAND_ID_YMTC:
					ncb_nand_trace(LOG_DEBUG, 0x549b, "Ymtc ONFI nand");
					support = true;
					nand_info.device_class = ONFI_NAND;
					break;
				case NAND_ID_UNIC:
					ncb_nand_trace(LOG_DEBUG, 0xbf23, "Unic ONFI nand");
					support = true;
					nand_info.device_class = ONFI_NAND;
					break;
				default:
					ncb_nand_trace(LOG_DEBUG, 0xf80a, "Nand vendor 0x%x not support", id[0]);
					support = false;
					sys_assert(0);
					break;
				}
			}
			if (first_ce) {
				memcpy(nand_info.id, id, nr_id_bytes);
				nand_info.nr_id_bytes = nr_id_bytes;
				first_ce = false;
				ncb_nand_trace(LOG_ERR, 0xaea9, "#");
			} else {
				if (memcmp(nand_info.id, id, nr_id_bytes) != 0) {
					if (id[0] != NO_DEV_SIGNAL) {
						ncb_nand_trace(LOG_ERR, 0xae74, "x");
					} else {
						ncb_nand_trace(LOG_ERR, 0xae4d, "_");
					}
					support = false;
				} else {
					ncb_nand_trace(LOG_ERR, 0x8ca4, "#");
				}
			}
			if (!support) {
				ce_stop = true;
				continue;
			} else {
				if (ce_stop) {
					ce_consective = false;
				}
			}
			ce_exist_bmp[ch] |= 1 << ce;
			ch_exist = true;
			ce_remap |= ce << (ce_cnt << 2);
			ce_cnt++;
		}
		ncb_nand_trace(LOG_ERR, 0xc801, "\n");
		if (ch_exist) {
			if (nand_info.geo.nr_channels == 0){
				nand_info.geo.nr_targets = ce_cnt;
			} else {
				if (nand_info.geo.nr_targets != ce_cnt) {
					ncb_nand_trace(LOG_WARNING, 0x7296, "CE cnt diff");
					sys_assert(0);
				}
			}
			nand_info.geo.nr_channels++;
			ch_remap |= ch << (ch_cnt << 2);
			ch_cnt++;
			if (!ce_consective) {// CE not consecutive, update CE remapping
				ndcu_writel(ce_remap, NF_CE_REMAP_REG0 + (ch << 2));
				ncb_nand_trace(LOG_INFO, 0x0c9f, "CE remap %x", ce_remap);
			}
		} else {
			// Make sure non-exist logic channel remaps to non-exist physical channel
			ch_remap |= ch << (((max_channel - 1) - (ch - ch_cnt)) << 2);
		}
	}


	if (max_channel < 8) {
		// Make sure non-exist logic channel remaps to non-exist physical channel
		ch_remap |= 0x77777777 << (max_channel * 4);
	}
	ncb_nand_trace(LOG_INFO, 0x76ea, "CH remap %x", ch_remap);

	ficu_channel_remap(ch_remap);
	if (!ce_consective) {// CE not consecutive, enable CE remapping
		nf_ddr_data_dly_ctrl_reg0_t remap_en;
		remap_en.all = ndcu_readl(NF_DDR_DATA_DLY_CTRL_REG0);
		remap_en.b.nf_ce_remap_en = 1;
		ndcu_writel(remap_en.all, NF_DDR_DATA_DLY_CTRL_REG0);
	}

	ncb_nand_trace(LOG_INFO, 0x3ed4, "CH %d CE %d", nand_info.geo.nr_channels, nand_info.geo.nr_targets);
#if defined(FAST_MODE)
	nand_info.geo.nr_channels = 1;
	ncb_nand_trace(LOG_INFO, 0x543e, "FAST mode, (%d) virtual channel", nand_info.geo.nr_channels);
#endif

#if HAVE_TSB_SUPPORT
	if (nand_is_bics4() || nand_is_bics5()) {
		nand_info.vcc_1p2v = true;
	} else if (nand_is_bics3()) {
		nand_info.vcc_1p2v = false;
	} else {
		sys_assert(0);
	}
#endif

#if defined(RDISK) && HAVE_MICRON_SUPPORT
#if defined(MU_B27B)
	if (!nand_is_b27b()) {
		sys_assert(0);
	}
#else
	if (nand_is_b27b()) {
		sys_assert(0);
	}
#endif
#endif
}

/*!
 * @brief Judge if nand is support 1.2v
 *
 * @return value(true or false)
 */
slow_code bool nand_is_1p2v(void)
{
	return nand_info.vcc_1p2v;
}

/*!
 * @brief Get nand user page size(NOT include spare area)
 *
 * @return value(user page size)
 */
fast_code u16 nand_page_user_size(void)
{
	return nand_info.page_sz;
}

/*!
 * @brief Get nand whole page size(include spare area)
 *
 * @return value(whole page size)
 */
fast_code u16 nand_whole_page_size(void)
{
	return nand_info.page_sz + nand_info.spare_sz;
}

/*!
 * @brief Get nand interleave number
 *
 * @return value(interleave number)
 */
fast_code u32 nand_interleave_num(void)
{
	return nand_info.interleave;
}

/*!
 * @brief Get nand channel number
 *
 * @return value(channel number)
 */
fast_code u8 nand_channel_num(void)
{
	return nand_info.geo.nr_channels;
}

/*!
 * @brief Get nand target(ce) number
 *
 * @return value(target(ce) number)
 */
fast_code u8 nand_target_num(void)
{
	return nand_info.geo.nr_targets;
}

/*!
 * @brief Get nand lun number
 *
 * @return value(lun number)
 */
fast_code u8 nand_lun_num(void)
{
	return nand_info.geo.nr_luns;
}

/*!
 * @brief Get nand plane number
 *
 * @return value(plane number)
 */
fast_code u8 nand_plane_num(void)
{
	return nand_info.geo.nr_planes;
}

/*!
 * @brief Get nand block number
 *
 * @return value(block number)
 */
fast_code u32 nand_block_num(void)
{
	return nand_info.geo.nr_blocks;
}

/*!
 * @brief Get nand page spare area size
 *
 * @return value(page spare area size)
 */
fast_code u32 nand_spare_size(void)
{
	return nand_info.spare_sz;
}

/*!
 * @brief Get nand page number
 *
 * @return value(page number)
 */
fast_code u32 nand_page_num(void)
{
	return nand_info.geo.nr_pages;
}

/*!
 * @brief Get nand SLC page number
 *
 * @return value(SLC page number)
 */
fast_code u32 nand_page_num_slc(void)
{
	return nand_info.geo.nr_pages / nand_info.bit_per_cell;
}

/*!
 * @brief Get nand bit per cell type
 *
 * @return value(bit per cell type)
 */
fast_code u32 nand_bits_per_cell(void)
{
	return nand_info.bit_per_cell;
}

/*!
 * @brief Get nand PDA block shift
 *
 * @return value(PDA block shift)
 */
fast_code u32 nand_pda_block_shift(void)
{
	return nand_info.pda_block_shift;
}

/*!
 * @brief Get nand row address lun shift number
 *
 * @return value(lun shift number)
 */
fast_code u8 nand_row_lun_shift(void)
{
	return nand_info.row_lun_shift;
}

/*!
 * @brief Get nand row address block shift number
 *
 * @return value(block shift number)
 */
fast_code u8 nand_row_blk_shift(void)
{
	return nand_info.row_block_shift;
}

/*!
 * @brief Get nand row address plane shift number
 *
 * @return value(plane shift number)
 */
fast_code u8 nand_row_plane_shift(void)
{
	return nand_info.row_pl_shift;
}

/*!
 * @brief Get nand row address page shift number
 *
 * @return value(page shift number)
 */
fast_code u8 nand_row_page_shift(void)
{
	return nand_info.row_page_shift;
}

/*!
 * @brief Check if ce exists
 *
 * @param[in] ch		flash channel
 * @param[in] ce		flash ce
 *
 * @return value(true or false)
 */
fast_code bool nand_target_exist(u32 ch, u32 ce)
{
	if (ce_exist_bmp[ch] & (1 << ce)) {
		return true;
	} else {
		return false;
	}
}

/*!
 * @brief Get the flash channel number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(channel value)
 */
fast_code u32 pda2ch(pda_t pda)
{
	return (pda >> nand_info.pda_ch_shift) & (nand_info.geo.nr_channels - 1);
}

/*!
 * @brief Get the flash ce number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(ce value)
 */
fast_code u32 pda2ce(pda_t pda)
{
	return (pda >> nand_info.pda_ce_shift) & (nand_info.geo.nr_targets - 1);
}

fast_code u32 pda2die(pda_t pda)
{
    return (pda >> nand_info.pda_ch_shift) & (nand_info.lun_num - 1);
}


/*!
 * @brief Get the flash lun number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(lun value)
 */
fast_code u32 pda2lun(pda_t pda)
{
	return (pda >> nand_info.pda_lun_shift) & (nand_info.geo.nr_luns - 1);
}

/*!
 * @brief Get the flash page number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(page value)
 */
fast_code u32 pda2page(pda_t pda)
{
	return (pda >> nand_info.pda_page_shift) & (nand_info.pda_page_mask);
}

/*!
 * @brief Get the flash du number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(page value)
 */
fast_code u32 pda2du(pda_t pda)
{
	return (pda >> nand_info.pda_du_shift) & ((1<<nand_info.pda_du_shift) - 1);
}

/*!
 * @brief Get the CH, CE, LUN combined ID in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(combined ID)
 */
fast_code u32 pda2lun_id(pda_t pda)
{
	// Assume LUN CE CH PL or CE LUN CH PL
	return (pda >> nand_info.pda_ch_shift) & nand_info.pda_lun_mask;
}

/*!
 * @brief Get the flash row address in PDA
 *
 * @param[in] pda			PDA
 * @param[in] pb_type		PDA's PB type
 *
 * @return value(row address)
 */
fast_code u32 pda2row(pda_t pda, enum nal_pb_type pb_type)
{
	u32 lun, plane, block, page;

	page = (pda >> nand_info.pda_page_shift) & nand_info.pda_page_mask;
	block = (pda >> nand_info.pda_block_shift) & nand_info.pda_block_mask;
	plane = (pda >> nand_info.pda_plane_shift) & (nand_info.geo.nr_planes - 1);
	lun = (pda >> nand_info.pda_lun_shift) & (nand_info.geo.nr_luns- 1);

#if ROW_WL_ADDR
	// Wordline address instead of page address in NAND row address
	if (pb_type == NAL_PB_TYPE_XLC) {
		page /= nand_info.bit_per_cell;
	}
#endif
	return (page << nand_info.row_page_shift) | \
	(plane << nand_info.row_pl_shift) | \
	(block << nand_info.row_block_shift) | \
	(lun << nand_info.row_lun_shift);
}

fast_code u32 pda2ActDiePlane_Row(pda_t pda)
{
	return (pda >> nand_info.pda_page_shift) & nand_info.pda_pageblock_mask;
}


#include "eccu.h"

/*!
 * @brief Transfer PDA to RDA
 *
 * @param[in] pda			PDA
 * @param[in] pb_type		PDA's PB type
 * @param[in] rda			point to a RDA address
 *
 * @return Not used
 */
fast_code void nal_pda_to_rda(pda_t pda, enum nal_pb_type pb_type, rda_t *rda)
{
	if (unlikely(!is_normal_pda(pda)))
		sys_assert(0);

	rda->row = pda2row(pda, pb_type);
	rda->ch = pda2ch(pda);
	rda->dev = pda2ce(pda);
	rda->pb_type = pb_type;
	rda->du_off = (pda >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
}

/*!
 * @brief Transfer RDA to PDA
 *
 * @param[in] rda			point to a RDA address
 *
 * @return value(PDA)
 */

fast_code pda_t nal_rda_to_pda(rda_t *rda)
{
	pda_t pda;
	u32 blk, pg, lun, pl;

	pg = (rda->row >> nand_info.row_page_shift) & ((1 << nand_info.row_pl_shift) - 1);
	blk = (rda->row >> nand_info.row_block_shift) & nand_info.pda_block_mask;
	lun = (rda->row >> nand_info.row_lun_shift) & (nand_info.geo.nr_luns - 1);
	pl = (rda->row >> nand_info.row_pl_shift) & (nand_info.geo.nr_planes - 1);

#if ROW_WL_ADDR
	if (rda->pb_type != NAL_PB_TYPE_SLC) {
		pg = pg * XLC + (rda->pb_type - NAL_PB_TYPE_XLC);
	}
#endif

	pda = pg << nand_info.pda_page_shift;
	pda |= blk << nand_info.pda_block_shift;
	pda |= lun << nand_info.pda_lun_shift;
	pda |= pl << nand_info.pda_plane_shift;
	pda |= rda->ch << nand_info.pda_ch_shift;
	pda |= rda->dev << nand_info.pda_ce_shift;
	pda |= rda->du_off << nand_info.pda_du_shift;

	return pda;
}

fast_code u32 nand_phy_ch(u32 log_ch)
{
#if TACOMA12
	if (log_ch >= 8) {
		return (channel_l2p_mapping_hi >> ((log_ch - 8) * 4)) & 0xF;
	} else
#endif
	{
		return (channel_l2p_mapping >> (log_ch * 4)) & 0xF;
	}
}
fast_code u32 nand_phy_ce(u32 log_ch, u32 log_ce)
{
	nf_ce_remap_reg0_t reg;
#if   TACOMAX
	reg.all = ndcu_readl(NF_CE_REMAP_REG0 + log_ch * 4);
#endif
	return (reg.all >> (log_ce * 4)) & 0xF;
}
fast_code u32 nand_log_ch(u32 phy_ch)
{
#if TACOMA12
	if (phy_ch >= 8) {
		return (channel_p2l_mapping_hi >> ((phy_ch - 8) * 4)) & 0xF;
	} else
#endif
	{
		return (channel_p2l_mapping >> (phy_ch * 4)) & 0xF;
	}
}
fast_code u32 nand_log_ce(u32 phy_ch, u32 phy_ce)
{
	u32 log_ce = 0;
//#if   TACOMAX
#if 1
	nf_ce_remap_reg0_t reg =  {
		.all = ndcu_readl(NF_CE_REMAP_REG0 + (phy_ch * 4)),
	};
#endif
	while (reg.all) {
		if ((reg.all & 0xF) == phy_ce) {
			return log_ce;
		}
		reg.all >>= 4;
		++log_ce;
	}
	sys_assert(0);
	return phy_ce;
}
/*!
 * @brief Get the nand block number in PDA
 *
 * @param[in] pda			PDA
 *
 * @return value(flash block number)
 */
fast_code u32 pda2blk(pda_t pda)
{
	u32 block = (pda >> nand_info.pda_block_shift) & nand_info.pda_block_mask;
	return block;
}

fast_code u32 pda2plane(pda_t pda)
{
	u32 plane = (pda >> nand_info.pda_plane_shift) & (nand_info.geo.nr_planes - 1);
	return plane;
}

/*!
 * @brief Get PDA offset in a super block
 *
 * @param[in] pda			PDA
 *
 * @return value(PDA offset)
 */
fast_code u32 nal_pda_offset_in_spb(pda_t pda)
{
	return pda & ((1U << nand_info.pda_block_shift) - 1);
}

/*!
 * @brief Get the nand column address in PDA
 *
 * @param[in] pda			PDA
 *
 * @return value(flash column address)
 */
fast_code u32 pda2column(pda_t pda)
{
	u32 du;
	du = (pda >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
	return du * get_encoded_du_size();
}

/*!
 * @brief Get the nand page index in PDA
 *
 * @param[in] pda			PDA
 *
 * @return value(page index)
 */
fast_code u8 pda2pg_idx(pda_t pda)
{
	u32 page;
	page = (pda >> nand_info.pda_page_shift) & nand_info.pda_page_mask;
#if QLC_SUPPORT
	return page & 0x3;
#else
#if HAVE_SAMSUNG_SUPPORT
	if (page < 8) {
		return (page % 2) + 1;
	} else if (page < 0x2F0) {
		return (page - 8) % 3;
	} else if (page < 0x2F8) {
		return ((page - 0x2F0) % 2) + 1;
	} else {
		return 2;
	}
#else
	return page % 3;
#endif
#endif
}

/*!
 * @brief Get the nand block index in PDA
 *
 * @param[in] pda			PDA
 *
 * @return value(block index)
 */
fast_code u32 nal_pda_get_block_id(pda_t pda)
{
	return pda >> nand_info.pda_block_shift;
}

/*!
 * @brief Get the PDA number
 *
 * @param[in] spb_id		SPB ID
 * @param[in] index			index
 *
 * @return value(PDA)
 */
fast_code pda_t nal_make_pda(u32 spb_id, u32 index)
{
	pda_t pda = spb_id << nand_info.pda_block_shift;

	pda += index;
	return pda;
}

/*!
 * @brief Get the row address
 *
 * @param[in] lun		flash lun
 * @param[in] pl		flash plane
 * @param[in] block		flash block
 * @param[in] page		flash page
 *
 * @return value(row address)
 */
fast_code u32 row_assemble(u32 lun, u32 pl, u32 block, u32 page)
{
	u32 row;
	row = (lun << nand_info.row_lun_shift) | \
		(pl << nand_info.row_pl_shift) | \
		(block << nand_info.row_block_shift) | \
		(page << nand_info.row_page_shift);
	return row;
}

/*!
 * @brief Get the PDA number
 *
 * @param[in] ch		flash ch
 * @param[in] ce		flash ce
 * @param[in] row		flash row addr
 * @param[in] col		flash col addr
 *
 * @return value(PDA)
 */
fast_code pda_t pda_assemble(u32 ch, u32 ce, u32 row, u32 col)
{
	u32 lun, plane, block, page, du;
	pda_t pda;
	u32 row_page_mask;

	row_page_mask = (1 << (nand_info.row_pl_shift - nand_info.row_page_shift)) - 1;

	page = (row >> nand_info.row_page_shift) & row_page_mask;
	plane = (row >> nand_info.row_pl_shift) & (nand_info.geo.nr_planes - 1);
	block = (row >> nand_info.row_block_shift) & nand_info.pda_block_mask;
	lun = row >> nand_info.row_lun_shift;
	du = col / (NAND_DU_SIZE+1);// Currently support 4KB du
	//ncb_nand_trace(LOG_INFO, 0, "ch %d, ce %d, lun %d, pl %d, block %d, page %d, du %d (col 0x%x)", ch, ce, lun, plane, block, page, du, col);
	pda = (lun << nand_info.pda_lun_shift) | \
		(ce << nand_info.pda_ce_shift) | \
		(ch << nand_info.pda_ch_shift) | \
		(plane << nand_info.pda_plane_shift) | \
		(block << nand_info.pda_block_shift) | \
		(page << nand_info.pda_page_shift) | \
		(du << nand_info.pda_du_shift);
	return pda;
}
fast_code pda_t pda_assemble_xlc(u32 ch, u32 ce, u32 row, u32 col, u8 prefix)
{
	u32 lun, plane, block, page, du;
	pda_t pda;
	u32 row_page_mask;

	row_page_mask = (1 << (nand_info.row_pl_shift - nand_info.row_page_shift)) - 1;

	page = (row >> nand_info.row_page_shift) & row_page_mask;
	plane = (row >> nand_info.row_pl_shift) & (nand_info.geo.nr_planes - 1);
	block = (row >> nand_info.row_block_shift) & nand_info.pda_block_mask;
	lun = row >> nand_info.row_lun_shift;
	du = col / NAND_DU_SIZE;// Currently support 4KB du
	//ncb_nand_trace(LOG_INFO, 0, "ch %d, ce %d, lun %d, pl %d, block %d, page %d, du %d (col 0x%x)", ch, ce, lun, plane, block, page, du, col);
	page = page * XLC + prefix - 1;

	pda = (lun << nand_info.pda_lun_shift) | \
		(ce << nand_info.pda_ce_shift) | \
		(ch << nand_info.pda_ch_shift) | \
		(plane << nand_info.pda_plane_shift) | \
		(block << nand_info.pda_block_shift) | \
		(page << nand_info.pda_page_shift) | \
		(du << nand_info.pda_du_shift);
	return pda;
}

/*!
 * @brief Initialization nand information value
 *
 * @return Not used
 */
fast_code void nand_info_init(void)
{
	nand_info.pda_du_shift = 0;
	nand_info.pda_interleave_shift = DU_CNT_SHIFT;
	nand_info.pda_plane_shift = nand_info.pda_interleave_shift;
	nand_info.pda_ch_shift = nand_info.pda_plane_shift + ctz(nand_info.geo.nr_planes);
	nand_info.pda_ce_shift = nand_info.pda_ch_shift + ctz(nand_info.geo.nr_channels);
	nand_info.pda_lun_shift = nand_info.pda_ce_shift + ctz(nand_info.geo.nr_targets);

	nand_info.lun_num = nand_info.geo.nr_channels * nand_info.geo.nr_targets * nand_info.geo.nr_luns;
	nand_info.pda_lun_mask = nand_info.lun_num - 1;
	nand_info.interleave = nand_info.geo.nr_channels * nand_info.geo.nr_targets * nand_info.geo.nr_luns * nand_info.geo.nr_planes;
	nand_info.pda_page_shift = nand_info.pda_interleave_shift + ctz(nand_info.interleave);
	u32 page_width = ctz(get_next_power_of_two(nand_info.geo.nr_pages));
	nand_info.pda_page_mask = (1 << page_width) - 1;
	nand_info.pda_block_shift = nand_info.pda_page_shift + page_width;
	u32 block_width = ctz(get_next_power_of_two(nand_info.geo.nr_blocks));
	nand_info.pda_block_mask = (1 << block_width) - 1;
    nand_info.pda_pageblock_mask = (1 << (block_width + page_width)) - 1;
	
	nand_info.cpda_blk_pg_shift = DU_CNT_SHIFT + ctz(nand_info.geo.nr_planes) +\
		ctz(nand_info.geo.nr_luns) + ctz(nand_info.geo.nr_targets) + ctz(nand_info.geo.nr_channels);

	nand_info.row_page_shift = 0;
#if ROW_WL_ADDR
	// WL address
	page_width = ctz(get_next_power_of_two(nand_info.geo.nr_pages / nand_info.bit_per_cell));
	nand_info.row_pl_shift = nand_info.row_page_shift + page_width;
	nand_info.row_block_shift = nand_info.row_pl_shift + ctz(nand_info.geo.nr_planes);
	nand_info.row_lun_shift = nand_info.row_block_shift + block_width;
#else
	// Page address
	nand_info.row_pl_shift = nand_info.row_page_shift + page_width;
	nand_info.row_block_shift = nand_info.row_pl_shift + ctz(nand_info.geo.nr_planes);
	nand_info.row_lun_shift = nand_info.row_block_shift + block_width;
#endif

	ncb_nand_trace(LOG_ERR, 0xb536, "page size (%d+%d), ", nand_info.page_sz, nand_info.spare_sz);
	ncb_nand_trace(LOG_ERR, 0x0f97, "%dblocks*%dpages, ", nand_info.geo.nr_blocks, nand_info.geo.nr_pages);
	ncb_nand_trace(LOG_ERR, 0x2033, "%dCH*%dCE*%dLUN*%dPL, ", nand_info.geo.nr_channels, nand_info.geo.nr_targets, nand_info.geo.nr_luns, nand_info.geo.nr_planes);
	u32 nand_size;
	nand_size = nand_info.page_sz >> 10;// KB
	nand_size *= nand_info.geo.nr_pages;
	nand_size *= nand_info.interleave;
	nand_size >>= 10;
	nand_size *= nand_info.geo.nr_blocks;
	nand_size >>= 10;
	ncb_nand_trace(LOG_ERR, 0xbd58, "NAND capacity %d GB\n", nand_size);
	sys_assert(nand_info.bit_per_cell == XLC);

	srb_t *srb = (srb_t *) SRAM_BASE;
	ncb_nand_trace(LOG_ERR, 0x3ae8, "Drive CAP. Idx : %d (0:512, 1:1T, 2:2T, 3:4T)\n",srb->cap_idx);

#ifdef CAP_IDX	//20200831
	cap_idx = srb->cap_idx;
#endif

	nand_info.flags = 0;
#if HAVE_TSB_SUPPORT
	if (nand_is_bics4() || nand_is_bics5()) {
		nand_info.flags |= NAND_FLAG_TSB_AIPR;
	}
#endif
}

/*!
 * @brief Judge if nand support AIPR
 *
 * @return value(true or false)
 */
fast_code bool nand_support_aipr(void)
{
#if HAVE_TSB_SUPPORT
	if (nand_info.flags & NAND_FLAG_TSB_AIPR) {
		return true;
	}
#endif
	return false;
}

/*!
 * @brief Get the nand speed MT
 *
 * @param[in] speed_grade		speed_grade value
 *
 * @return value(MT number)
 */
init_code u16 nand_speed(u16 speed_grade)
{
	switch(speed_grade) {
	case 0xFF:
		return 400;
		break;
	case 0x1FF:
		return 533;
		break;
	case 0x3FF:
		return 666;
		break;
	case 0x7FF:
		return 800;
		break;
	case 0xFFF:
		return 1066;
		break;
	case 0x1FFF:
		return 1200;
		break;
	case 0:
		// Please find out speed grade of this nand
	default:
		break;
	}
	sys_assert(0);
	return 0;
}

/*!
 * @brief Check if the device nand ID is already in flash support list
 *
 * @return Not used
 */
init_code bool nand_scan_support_list(void)
{
	u32 i;

	for (i = 0; i < sizeof(support_devices) / sizeof(support_devices[0]); i++) {
		if (!memcmp(nand_info.id, support_devices[i].id, support_devices[i].id_len)) {
			nand_info.nr_id_bytes = support_devices[i].id_len;
			nand_info.bit_per_cell = support_devices[i].bit_per_cell;
			nand_info.addr_cycles = support_devices[i].addr_cycles;
			nand_info.max_tm = support_devices[i].max_tm;
			nand_info.geo.nr_luns = support_devices[i].nr_luns;
			nand_info.geo.nr_planes = support_devices[i].nr_planes;
			nand_info.geo.nr_blocks = support_devices[i].nr_blocks;
			nand_info.geo.nr_pages = support_devices[i].nr_pages;
			nand_info.page_sz = support_devices[i].page_sz;
			nand_info.spare_sz = support_devices[i].spare_sz;
			nand_info.max_speed = nand_speed(support_devices[i].speed_grade);
#if XLC == 2
			ncb_nand_trace(LOG_INFO, 0xce36, "Fake param page (MLC)");
#elif XLC == 3
			ncb_nand_trace(LOG_INFO, 0xb8fd, "Fake param page (TLC)");
#elif XLC == 4
			ncb_nand_trace(LOG_INFO, 0x849d, "Fake param page (QLC)");
#endif
			return true;
		}
	}
	sys_assert(0);
	return false;
}

// Debug purpose only, will be removed in final formal FW
init_code void check_board_wp(void)
{
	u8 status;
	u32 ch, ce;
#ifdef FAST_MODE
	for (ch = 0; ch < max_channel; ch++) {
#else
	for (ch = 0; ch < nand_info.geo.nr_channels; ch++) {
#endif
		for (ce = 0; ce < nand_info.geo.nr_targets; ce++) {
			status = nand_read_sts(ch, ce, 0x70);
			if ((status & BIT7) == 0) {
				ncb_nand_trace(LOG_ERR, 0xbbb0, "ch %d ce %d wp!!!\n", ch, ce);
			}
		}
	}
}

/*!
 * @brief Nand switch NF clock
 *
 * @param freq	New frequency
 *
 * @return	not used
 */
fast_code void nand_clk_switch(u32 freq)
{
	u32 nf_clk_old;

	extern u32 nf_clk;
	nf_clk_old = nf_clk;

	ndcu_nf_clk_switch(freq);

	nand_vendor_clk_switch(nf_clk_old, nf_clk);

	///< Redo DLL calibration
	extern void ncl_dll_calibration_enhance(void);
	ncl_dll_calibration_enhance();
}

// Nand initialize commands
/*!
 * @brief Do the nand module initialization
 *
 * @return Not used
 */
init_code void nand_init(void)
{
	ndcu_set_tm(NDCU_IF_SDR, 0);
	nand_info.cur_tm = NDCU_IF_SDR | 0;

#if BYPASS_NAND_DETECTION
	nand_detect_fake();
#else

	// Nand initialization
	// Reset all CH*CE and wait busy
#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
		nand_reset_all();
	}

#if HAVE_TSB_SUPPORT
rescan:
#endif
	// Scan all CH*CE ID, and decide ONFI/Toggle
	nand_detect();
	ndcu_set_tm(nand_info.def_tm, 0);
	nand_info.cur_tm = nand_info.def_tm;

	// Debug check NAND not write protect
#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
		check_board_wp();
	}

#if HAVE_ONFI_SUPPORT
	nand_info.cur_tm = nand_info.def_tm;
	ndcu_set_tm(nand_info.def_tm & 0xF0, nand_info.def_tm & 0xF);
#endif

#if HAVE_TSB_SUPPORT
	// Toshiba 2 LUN device sometimes cannot read parameter page correctly
	// Debug found LUN 1 is accessed before warm reset, then ECh-40h will output wrong data.
	// After adding LUN0 select, read parameter page always correct
	nand_lun0_select(0, 0);
#endif
#endif

	// Read parameter page and update NAND info
#if HAVE_TOGGLE_SUPPORT
	sys_assert(nand_info.device_class == TOGGLE_NAND);
	nand_info.max_tm = NDCU_IF_TGL2 | 0;  //Sean_0419
	nand_info.cur_tm = NDCU_IF_DEFAULT | 0;
	nand_info.def_tm = NDCU_IF_DEFAULT | 0;
	ndcu_set_tm(NDCU_IF_DEFAULT, 0);
#if HAVE_TSB_SUPPORT
	extern void nand_tsb_tsv_intf_cfg(void);
	if (nand_is_tsb_tsv() && (nand_target_num() == 1)) {
		nand_tsb_tsv_intf_cfg();
		max_target = MAX_TARGET;
		ndcu_set_tm(NDCU_IF_SDR, 0);
		goto rescan;
	}
#endif
#endif

#if HAVE_ONFI_SUPPORT
	sys_assert(nand_info.device_class == ONFI_NAND);
#endif
	nand_read_page_parameter();
	if (nand_info.fake_page_param) {
		nand_scan_support_list();
	}
#if USE_TSB_NAND
	if (nand_is_bics5() && (nand_info.max_speed == 400)) {
		nand_info.max_speed = 1200;
	}
#endif
	nand_info_init();
#if HAVE_HYNIX_SUPPORT || HAVE_TSB_SUPPORT
#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
		nand_specific_init();
	}
#endif

#if !BYPASS_NAND_DETECTION
#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
		nand_set_timing();
	}
#endif
}

// Nand reinitialize commands
/*!
 * @brief Do the nand module reinitialization
 *
 * @return Not used
 */
fast_code void nand_reinit(void)
{
		ncb_nand_trace(LOG_ERR, 0x1255, "nand reinit!\n");
		ndphy_hw_init();

		nf_clk_init(100);
		ndcu_set_tm(NDCU_IF_SDR, 0);
		nand_reset_all();

		ndcu_set_tm(nand_info.def_tm, 0);

#if HAVE_TSB_SUPPORT
		nand_specific_init();
#endif

		nand_set_timing();
}
/*! @} */
