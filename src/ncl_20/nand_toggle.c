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
/*! \file nand_toggle.c
 * @brief get nand parameter page and set timing
 *
 * \addtogroup ncl_20
 * \defgroup nand_toggle
 * \ingroup ncl_20
 *
 * @{
 */
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "nand_cfg.h"
#include "nand.h"
#include "nand_toggle.h"
#include "ndcu.h"

#if HAVE_TOGGLE_SUPPORT
/*! \cond PRIVATE */
#define __FILEID__ toggle
#include "trace.h"
/*! \endcond */


#define NAND_JEDEC_ID_ADDR	0x40	///< Nand toggle ID address
#define NAND_ID_TOGGLE_LENGTH	6	///< Nand toggle ID Length
#define NAND_PAGE_PARAM_LENGTH	512	///< Toggle nand parameter page length

init_data struct nand_page_param page_param;

#if DUMP_NAND_PARAM
/*!
 * @brief Dump parameter page content
 *
 * @return Not used
 */
init_code void dump_page_parameter(void)
{
	u32 i;

	ncb_toggle_trace(LOG_ERR, 0xd341, "Signature %c,%c,%c,%c\n", page_param.sig & 0xFF, (page_param.sig >> 8) & 0xFF, (page_param.sig >> 16) & 0xFF, (page_param.sig >> 24) & 0xFF);
	ncb_toggle_trace(LOG_ERR, 0x50ed, "Revision %d\n", page_param.revision);
	ncb_toggle_trace(LOG_ERR, 0x5724, "feature 0x%x\n", page_param.features);
	ncb_toggle_trace(LOG_ERR, 0x757a, "    %s support 16 bits bus\n", (page_param.features & BIT0) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xef2b, "    %s support multi-LUN\n", (page_param.features & BIT1) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x42a6, "    %s support non-sequential programing\n", (page_param.features & BIT2) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x593b, "    %s support MP prog & erase\n", (page_param.features & BIT3) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xc0aa, "    %s support MP read\n", (page_param.features & BIT4) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x2c0a, "    %s support sync DDR\n", (page_param.features & BIT5) ? "" : "NOT ");

	ncb_toggle_trace(LOG_ERR, 0xbd5b, "    %s support toggle DDR\n", (page_param.features & BIT6) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x3145, "    %s support external Vpp\n", (page_param.features & BIT7) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x25b5, "    %s support program page register clear enhancement\n", (page_param.features & BIT8) ? "" : "NOT ");

	ncb_toggle_trace(LOG_ERR, 0x0fe2, "optional cmd 0x%x %x %x\n", page_param.opt_cmd[0], page_param.opt_cmd[1], page_param.opt_cmd[2]);
	ncb_toggle_trace(LOG_ERR, 0xd10f, "    %s support cache program\n", (page_param.opt_cmd[0] & BIT0) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x6390, "    %s support cache read\n", (page_param.opt_cmd[0] & BIT1) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xf3e5, "    %s support set/get feature\n", (page_param.opt_cmd[0] & BIT2) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xd72a, "    %s support read status enhanced\n", (page_param.opt_cmd[0] & BIT3) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x0a64, "    %s support copyback\n", (page_param.opt_cmd[0] & BIT4) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x5ef6, "    %s support read unique ID\n", (page_param.opt_cmd[0] & BIT5) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xf915, "    %s support random data out\n", (page_param.opt_cmd[0] & BIT6) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x9b57, "    %s support MP copyback\n", (page_param.opt_cmd[0] & BIT7) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x3257, "    %s support small data move\n", (page_param.opt_cmd[1] & BIT0) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x40cf, "    %s support reset LUN\n", (page_param.opt_cmd[1] & BIT1) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x5ff4, "    %s support sync reset\n", (page_param.opt_cmd[1] & BIT2) ? "" : "NOT ");

	ncb_toggle_trace(LOG_ERR, 0x73af, "# copy %d\n", page_param.num_of_param_pages);

	ncb_toggle_trace(LOG_ERR, 0xc8a1, "Manufacturer:");
	for (i=0; i<12;i++) {
		ncb_toggle_trace(LOG_ERR, 0x2fad, "%c", page_param.manufacturer[i]);
	}
	ncb_toggle_trace(LOG_ERR, 0x4414, "\n");
	ncb_toggle_trace(LOG_ERR, 0x1ac3, "Device model:");
	for (i=0; i<20;i++) {
		ncb_toggle_trace(LOG_ERR, 0x1128, "%c", page_param.model[i]);
	}
	ncb_toggle_trace(LOG_ERR, 0xcd50, "\n");
	ncb_toggle_trace(LOG_ERR, 0x2e88, "JEDEC manufacturer ID %x\n", page_param.jedec_id[0]);

	ncb_toggle_trace(LOG_ERR, 0x94cf, "%d byte/page\n", page_param.byte_per_page);
	ncb_toggle_trace(LOG_ERR, 0x019e, "%d spare byte\n", page_param.spare_bytes_per_page);
	ncb_toggle_trace(LOG_ERR, 0x0ce8, "%d page/block\n", page_param.pages_per_block);
	ncb_toggle_trace(LOG_ERR, 0x11f7, "%d blk/lun\n", page_param.blocks_per_lun);
	ncb_toggle_trace(LOG_ERR, 0x6076, "%d lun\n", page_param.lun_count);
	ncb_toggle_trace(LOG_ERR, 0xb22a, "%d addr cycles\n", page_param.addr_cycles);
	ncb_toggle_trace(LOG_ERR, 0x21a5, "%d bit per cell\n", page_param.bits_per_cell);
	ncb_toggle_trace(LOG_ERR, 0x542a, "%d programs per page\n", page_param.programs_per_page);


	ncb_toggle_trace(LOG_ERR, 0x67a2, "%d mp addr\n", page_param.multi_plane_addr);
	ncb_toggle_trace(LOG_ERR, 0x7ac7, "%d mp op attribute\n", page_param.multi_plane_op_attr);
	ncb_toggle_trace(LOG_ERR, 0x3c1f, "    %s support read cache\n", (page_param.multi_plane_op_attr & BIT2) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xd63f, "    %s support program cache\n", (page_param.multi_plane_op_attr & BIT1) ? "" : "NOT ");
	if (page_param.multi_plane_op_attr & BIT0)
		ncb_toggle_trace(LOG_ERR, 0x4be5, "    no mp block addr restrictions\n");
	else
		ncb_toggle_trace(LOG_ERR, 0xfee6, "    has mp block addr restrictions\n");

	ncb_toggle_trace(LOG_ERR, 0xeb9a, "async sdr speed grade 0x%x \n", page_param.async_sdr_speed_grade);
	ncb_toggle_trace(LOG_ERR, 0x2f40, "toggle ddr speed grade 0x%x \n", page_param.toggle_ddr_speed_grade);
	ncb_toggle_trace(LOG_ERR, 0x3b10, "sync ddr speed grade 0x%x \n", page_param.sync_ddr_speed_grade);
	ncb_toggle_trace(LOG_ERR, 0x893a, "async sdr features 0x%x \n", page_param.async_sdr_features);
	ncb_toggle_trace(LOG_ERR, 0xb128, "toggle ddr features 0x%x \n", page_param.toggle_ddr_features);
	ncb_toggle_trace(LOG_ERR, 0x3e98, "sync ddr features 0x%x \n", page_param.sync_ddr_features);


	ncb_toggle_trace(LOG_ERR, 0xa7fc, "tPROG %dus \n", page_param.t_prog);
	ncb_toggle_trace(LOG_ERR, 0x11d9, "tERS %dus \n", page_param.t_bers);
	ncb_toggle_trace(LOG_ERR, 0x5061, "tR %dus \n", page_param.t_r);
	ncb_toggle_trace(LOG_ERR, 0x9b7d, "tR (MP) %dus \n", page_param.t_r_multi_plane);

	ncb_toggle_trace(LOG_ERR, 0xaf2f, "tCCS %dns \n", page_param.t_ccs);
	ncb_toggle_trace(LOG_ERR, 0x1a99, "I/O pin capacitance %d\n", page_param.io_pin_capacitance_typ);
	ncb_toggle_trace(LOG_ERR, 0x1716, "Input pin capacitance %d\n", page_param.input_pin_capacitance_typ);
	ncb_toggle_trace(LOG_ERR, 0x8788, "CK pin capacitance %d\n", page_param.clk_pin_capacitance_typ);
	ncb_toggle_trace(LOG_ERR, 0x86b0, "Drive strength 0x%x\n", page_param.driver_strength_support);
	ncb_toggle_trace(LOG_ERR, 0x736d, "    %s support 35ohm/50ohm drive strength\n", (page_param.driver_strength_support & BIT0) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0x0b93, "    %s support 25ohm drive strength\n", (page_param.driver_strength_support & BIT1) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xb1a5, "    %s support 18ohm/50ohm drive strength\n", (page_param.driver_strength_support & BIT2) ? "" : "NOT ");
	ncb_toggle_trace(LOG_ERR, 0xa214, "tADL %dns \n", page_param.t_adl);


	ncb_toggle_trace(LOG_ERR, 0x4528, "guaranteed good blocks %d\n", page_param.guaranteed_good_blocks);
	ncb_toggle_trace(LOG_ERR, 0x4d98, "guaranteed good blocks endurance %d\n", page_param.guaranteed_block_endurance);

	for (i = 0; i < 4; i++) {
		ncb_toggle_trace(LOG_ERR, 0x31dc, "ECC correctablity %d bits\n", page_param.ecc_info[i].ecc_bits);
		ncb_toggle_trace(LOG_ERR, 0x3521, "Codeword size %d\n", page_param.ecc_info[i].codeword_size);
		ncb_toggle_trace(LOG_ERR, 0xd15f, "Maximum average bad blocks per LUN %d\n", page_param.ecc_info[i].bb_per_lun);
		ncb_toggle_trace(LOG_ERR, 0xf6a7, "Block endurance %d\n", page_param.ecc_info[i].block_endurance);
	}
	ncb_toggle_trace(LOG_ERR, 0x13cf, "Vendor revision %d\n", page_param.vendor_rev_num);
	ncb_toggle_trace(LOG_ERR, 0x541c, "CRC 0x%x \n", page_param.crc);
}
#endif

extern void nal_get_first_dev(u32 *ch, u32 *dev);

/*!
 * @brief Read parameter page content from each device
 *
 * @return Not used
 */
init_code void static nand_read_page_parameter_from_dev(void)
{
	u32 ch, ce;
	nal_get_first_dev(&ch, &ce);
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 1,
		.reg1.b.ind_byte0 = 0xEC,
		.reg1.b.ind_byte1 = 0x40,
		.xfcnt = 0,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_close(&ctrl);

	//nand_wait_ready(ch, ce);
    nand_wait_ready_78h(ch,ce,0);
	//change read column to start to read data
	ndcu_ind_t ctrl_r = {
		.write = false,
//#if HAVE_SAMSUNG_SUPPORT
#if 0 // Sean Bics5 70h 00h
		.cmd_num = 0,
		.reg1.b.ind_byte0 = 0x00,
#else
		.cmd_num = 6,
		.reg1.b.ind_byte0 = 0x05,
		.reg1.b.ind_byte1 = 0x00,
		.reg1.b.ind_byte2 = 0x00,
		.reg1.b.ind_byte3 = 0x00,
		.reg2.b.ind_byte4 = 0x04,  //Sean_20220718 00->0x04 plane 2
		.reg2.b.ind_byte5 = 0x00,
		.reg2.b.ind_byte6 = 0xE0,
#endif
		.cle_mode = 1,
		.xfcnt = NAND_PAGE_PARAM_LENGTH,
		.buf = (u8 *)&page_param,
	};

	ndcu_open(&ctrl_r, ch, ce);
	ndcu_start(&ctrl_r);
	do {
		if (ndcu_xfer(&ctrl_r))
			break;
	} while (1);
	ndcu_close(&ctrl_r);
}

#if HAVE_SYSTEMC
/*!
 * @brief Read parameter page content from table
 *
 * @return Not used
 */
init_code static void nand_read_page_parameter_from_tbl(void)
{
	struct nand_page_param p = {
		.sig = 0x4453454a,
		.revision = 0x4,
		.features = 0x1da,
		.opt_cmd = {0x4d, 0x2, 0x0},
		.sec_cmd = 0x81,
		.num_of_param_pages = 0x10,
		.manufacturer = {
			0x54, 0x4f, 0x53, 0x48, 0x49, 0x42,
			0x41, 0x20, 0x20, 0x20, 0x20, 0x20
			},
		.model = {
			0x54, 0x48, 0x35, 0x38, 0x54, 0x46,
			0x54, 0x31, 0x54, 0x32, 0x33, 0x42,
			0x41, 0x38, 0x48, 0x20, 0x20, 0x20,
			0x20, 0x20
			},
		.jedec_id = {0x98, 0x0, 0x0, 0x0, 0x0, 0x0},
		.byte_per_page = 0x4000,
		.spare_bytes_per_page = 0x7a0,
		.pages_per_block = 0x300,
		.blocks_per_lun = 0xb8c,
		.lun_count = 0x2,
		.addr_cycles = 0x23,
		.bits_per_cell = 0x3,
		.programs_per_page = 0x1,
		.multi_plane_addr = 0x1,
		.multi_plane_op_attr = 0x7,
		.async_sdr_speed_grade = 0x0,
		.toggle_ddr_speed_grade = 0xff,
		.sync_ddr_speed_grade = 0x0,
		.async_sdr_features = 0x0,
		.toggle_ddr_features = 0x0,
		.sync_ddr_features = 0x0,
		.t_prog = 0x0,
		.t_bers = 0x0,
		.t_r = 0x0,
		.t_r_multi_plane = 0x0,
		.t_ccs = 0x0,
		.io_pin_capacitance_typ = 0x0,
		.input_pin_capacitance_typ = 0x0,
		.clk_pin_capacitance_typ = 0x0,
		.driver_strength_support = 0x3,
		.t_adl = 0x0,
		.guaranteed_good_blocks = 0x0,
		.guaranteed_block_endurance = 0x0,
		.ecc_info = {
			{.ecc_bits = 0x78, .codeword_size = 0xa, .bb_per_lun = 0x6c, .block_endurance = 0x0},
			{.ecc_bits = 0x0, .codeword_size = 0x0, .bb_per_lun = 0x0, .block_endurance = 0x0},
			{.ecc_bits = 0x0, .codeword_size = 0x0, .bb_per_lun = 0x0, .block_endurance = 0x0},
			{.ecc_bits = 0x0, .codeword_size = 0x0, .bb_per_lun = 0x0, .block_endurance = 0x0}
			},
		.vendor_rev_num = 0x0,
	};
	memcpy(&page_param, &p, sizeof(p));

	ndcu_set_tm(NDCU_IF_TGL1, 0);
}
#endif

/*!
 * @brief Read parameter page content
 *
 * @return Not used
 */
init_code void nand_read_page_parameter(void)
{
#ifndef HAVE_SYSTEMC
	nand_read_page_parameter_from_dev();
#else
	nand_read_page_parameter_from_tbl();
#endif
#if DUMP_NAND_PARAM
	dump_page_parameter();
#endif
	nand_info.fake_page_param = true;
#ifndef HAVE_VELOCE
	if (page_param.sig != 0x4453454A/*DSEJ*/) {
		ncb_toggle_trace(LOG_WARNING, 0xb3b9, "Signature wrong");
		return;
	}
#ifndef HAVE_SYSTEMC
	if (page_param.crc != page_param_crc16((u8*)&page_param, sizeof(struct nand_page_param) - 2)) {
#if HAVE_TSB_SUPPORT
		if (!nand_is_tsb_tsv())
#endif
		{
			ncb_toggle_trace(LOG_WARNING, 0x9c66, "CRC wrong %x", page_param_crc16((u8*)&page_param, sizeof(struct nand_page_param) - 2));
		}
	}
#endif
#endif
	u32 i;
	for (i = 0; i < sizeof(page_param.manufacturer); i++) {
		if (page_param.manufacturer[i] == ' ') {
			page_param.manufacturer[i] = 0;
			break;
		}
	}
	sys_assert(i < sizeof(page_param.manufacturer));
	ncb_onfi_trace(LOG_INFO, 0x977e, "Vendor: %s", page_param.manufacturer);
	page_param.manufacturer[i] = ' ';
	for (i = 0; i < sizeof(page_param.model); i++) {
		if (page_param.model[i] == ' ') {
			page_param.model[i] = 0;
			break;
		}
	}
	sys_assert(i < sizeof(page_param.model));
	ncb_onfi_trace(LOG_INFO, 0x8c2f, "Model: %s", page_param.model);
	page_param.model[i] = ' ';

	ncb_toggle_trace(LOG_INFO, 0x4618, "Jedec revision %x", page_param.revision);
	switch(page_param.bits_per_cell) {
	case 3:
		ncb_toggle_trace(LOG_INFO, 0x438f, "TLC");
		break;
	case 4:
		ncb_toggle_trace(LOG_INFO, 0x52a1, "QLC");
		break;
	default:
		ncb_toggle_trace(LOG_WARNING, 0x33de, "%dLC\n", page_param.bits_per_cell);
		break;
	}
	if (page_param.features & BIT6) {
		ncb_toggle_trace(LOG_INFO, 0xd541, "Support toggle DDR, speed %x", page_param.toggle_ddr_speed_grade);
	}

	nand_info.fake_page_param = false;
	nand_info.geo.nr_luns = page_param.lun_count;
#if HAVE_TSB_SUPPORT
	if (nand_is_tsb_tsv() && (nand_info.geo.nr_luns == 0x16)) {
		nand_info.geo.nr_luns = 16;
	}
#endif
	nand_info.geo.nr_planes = 1 << page_param.multi_plane_addr;
#if defined(TSB_XL_NAND)
	nand_info.geo.nr_planes = 2; // force to 2 planes for test, 16 planes originally
#endif
	nand_info.geo.nr_blocks = page_param.blocks_per_lun / nand_info.geo.nr_planes;
#if HAVE_TSB_SUPPORT && QLC_SUPPORT
	if (page_param.pages_per_block == 0x4080) {
		if ((nand_info.id[0] == 0x98) && \
		    (nand_info.id[1] == 0x77) && \
		    (nand_info.id[2] == 0x9D) && \
		    (nand_info.id[3] == 0xB3) && \
		    (nand_info.id[4] == 0x7A) && \
		    (nand_info.id[5] == 0xE3)) {
			page_param.pages_per_block = 0x600;
			// BiCS4 QLC tested 0x600 pages per block
			ncb_toggle_trace(LOG_ERR, 0x7c60, "BiCS4 QLC!!!\n");
		}
	}
#endif
	nand_info.geo.nr_pages = page_param.pages_per_block;
	nand_info.page_sz = page_param.byte_per_page;
	nand_info.spare_sz = page_param.spare_bytes_per_page;
#if defined(TSB_XL_NAND) && defined(FAST_MODE)
	// TSB XL nand, page size is 4KiB, when used under FAST mode, have to
	// fake page size in order to fill all physical channels
	nand_info.page_sz *= max_channel;
	nand_info.spare_sz *= max_channel;
#endif
	nand_info.bit_per_cell = page_param.bits_per_cell;
	nand_info.addr_cycles = page_param.addr_cycles;
#if HAVE_SAMSUNG_SUPPORT
	if (nand_is_K9AFGD8H0A() && page_param.toggle_ddr_speed_grade == 0x400) {
		// K9AFGD8H0A says support up to 533MT/s
		page_param.toggle_ddr_speed_grade = 0x1FF;
	}
#endif
	nand_info.max_speed = nand_speed(page_param.toggle_ddr_speed_grade);
#if HAVE_TSB_SUPPORT
	if (nand_is_bics4()) {
		// Application_Note_NAND_interface_setting_for_higher_speed_rev1.1.pdf by Wei Jiang
#if defined(M2_2A)
		nand_info.max_speed = 1066;
#elif defined(M2)
		if (nand_target_num() * nand_lun_num() >= 4) {
			nand_info.max_speed = 800;
		} else {
			nand_info.max_speed = 1066;// IO voltage should raise to 1.26V, otherwise 800MT/s
		}
#else
		nand_info.max_speed = 800;//adams
#endif
	}
#endif
}

/*!
 * @brief Set feature function
 *
 * @param[in] ch		channel
 * @param[in] ce		ce
 * @param[in] fa		set feature addr
 * @param[in] val		value
 *
 * @return Not used
 */
fast_code void set_feature(u8 ch, u8 ce, u8 fa, u32 val)
{
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 1,
		.reg1.b.ind_byte0 = 0xEF,
		.reg1.b.ind_byte1 = fa,
		.xfcnt = sizeof(val),
		.buf = (u8 *)&val,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);
}

fast_code void set_feature_D5h(u8 ch, u8 ce, u8 lun, u8 fa, u32 val)
{
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 2,
		.reg1.b.ind_byte0 = 0xD5,
		.reg1.b.ind_byte1 = lun,
		.reg1.b.ind_byte2 = fa,
		.xfcnt = sizeof(val),
		.buf = (u8 *)&val,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);
}

/*!
 * @brief Get feature function
 *
 * @param[in] ch		channel
 * @param[in] ce		ce
 * @param[in] fa		get feature addr
 *
 * @return value
 */
fast_code u32 get_feature(u8 ch, u8 ce, u8 fa)
{
	u32 data;
	ndcu_ind_t ctrl = {
		.write = false,
		.cmd_num = 1,
		.reg1.b.ind_byte0 = 0xEE,
		.reg1.b.ind_byte1 = fa,
		.xfcnt = sizeof(data),
		.buf = (u8 *)&data,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);

	return data;
}

fast_code u32 get_feature_D4h(u8 ch, u8 ce, u8 lun, u8 fa)
{
    u32 data = 0;  //Sean_init_buf_avoid_getfeature_fail_20220811 
	ndcu_ind_t ctrl = {
		.write = false,
		.cmd_num = 2,
		.reg1.b.ind_byte0 = 0xD4,
		.reg1.b.ind_byte1 = lun,
		.reg1.b.ind_byte2 = fa,
		.xfcnt = sizeof(data),
		.buf = (u8 *)&data,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);

	return data;
}

#endif
/*! @} */
