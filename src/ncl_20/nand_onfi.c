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
/*! \file nand_onfi.c
 * @brief get nand parameter page and set timing
 *
 * \addtogroup ncl_20
 * \defgroup nand_onfi
 * \ingroup ncl_20
 *
 * @{
 */
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "nand_cfg.h"
#include "nand.h"
#include "nand_onfi.h"
#include "ndcu.h"

#if HAVE_ONFI_SUPPORT
/*! \cond PRIVATE */
#define __FILEID__ onfi
#include "trace.h"
/*! \endcond */


#define NAND_ONFI_ID_ADDR	0x20	/// Nand ONFI ID address
#define NAND_ID_ONFI_LENGTH	4		/// Nand ONFI ID Length
#define NAND_PAGE_PARAM_LENGTH	256	/// ONFI nand parameter page length

/*! Nand set feature address */
#define NAND_FA_ONFI_MODE		0x01
#define NAND_FA_ONFI_DDR_CONFIG		0x02
#define NAND_FA_ONFI_DRV_STRGTH		0x10
#define NAND_FA_ONFI_VPP_CONFIG		0x30
#define NAND_FA_ONFI_FLAG_CHECK	0xDF
#define NAND_FA_AUTO_READ_CALIBRATION	0x96
#define NAND_FA_SOFT_INFO		0x97
#define NAND_FA_READ_OFFSET_SNAP_READ	0xEE

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
	ncb_onfi_trace(LOG_ERR, 0x74bd, "---------------------\nRevision Information and features block\n");
	ncb_onfi_trace(LOG_ERR, 0xae8d, "Signature %c,%c,%c,%c\n", page_param.sig & 0xFF, (page_param.sig >> 8) & 0xFF, (page_param.sig >> 16) & 0xFF, (page_param.sig >> 24) & 0xFF);
	ncb_onfi_trace(LOG_ERR, 0x9681, "Revision %d\n", page_param.revision);
	ncb_onfi_trace(LOG_ERR, 0xb6aa, "feature 0x%x\n", page_param.features);
	ncb_onfi_trace(LOG_ERR, 0xa622, "    %s support 16 bits bus\n", (page_param.features & BIT0) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x6fd7, "    %s support multi-LUN\n", (page_param.features & BIT1) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x188d, "    %s support non-sequential programing\n", (page_param.features & BIT2) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xcaaa, "    %s support interleave (MP) prog & erase\n", (page_param.features & BIT3) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xe29e, "    %s support odd-to-even copyback\n", (page_param.features & BIT4) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x8bc2, "    %s support NV-DDR\n", (page_param.features & BIT5) ? "" : "NOT ");

	ncb_onfi_trace(LOG_ERR, 0x13dc, "    %s support interleave (MP) read\n", (page_param.features & BIT6) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xc18a, "    %s support extended parameter page\n", (page_param.features & BIT7) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xa5ff, "    %s support program page register clear enhancement\n", (page_param.features & BIT8) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xf738, "    %s support EZ nand\n", (page_param.features & BIT9) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x107c, "    %s support NV-DDR2\n", (page_param.features & BIT10) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xea83, "    %s support volume address\n", (page_param.features & BIT11) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x2d71, "    %s support external Vpp\n", (page_param.features & BIT12) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x5acd, "    %s support NV-DDR3\n", (page_param.features & BIT13) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xa8bd, "    %s support ZQ calibration\n", (page_param.features & BIT14) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x2a15, "    %s support package electrical spec\n", (page_param.features & BIT15) ? "" : "NOT ");

	ncb_onfi_trace(LOG_ERR, 0xae41, "optional cmd 0x%x %x %x\n", page_param.opt_cmd);
	ncb_onfi_trace(LOG_ERR, 0x20d5, "    %s support cache program\n", (page_param.opt_cmd & BIT0) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x82ba, "    %s support cache read\n", (page_param.opt_cmd & BIT1) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x7adb, "    %s support set/get feature\n", (page_param.opt_cmd & BIT2) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x49ac, "    %s support read status enhanced\n", (page_param.opt_cmd & BIT3) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xe0de, "    %s support copyback\n", (page_param.opt_cmd & BIT4) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xac0c, "    %s support read unique ID\n", (page_param.opt_cmd & BIT5) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x6ef0, "    %s support change read column enhanced\n", (page_param.opt_cmd & BIT6) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x2b43, "    %s support change row address\n", (page_param.opt_cmd & BIT7) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xe211, "    %s support small data move\n", (page_param.opt_cmd & BIT8) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x433b, "    %s support reset LUN\n", (page_param.opt_cmd & BIT9) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x3537, "    %s support volume select\n", (page_param.opt_cmd & BIT10) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x4d85, "    %s support ODT configure\n", (page_param.opt_cmd & BIT11) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xed2d, "    %s support get/set feature by LUN\n", (page_param.opt_cmd & BIT12) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xb4c8, "    %s support ZQ calibration\n", (page_param.opt_cmd & BIT13) ? "" : "NOT ");

	ncb_onfi_trace(LOG_ERR, 0x1426, "onfi-jedec JTG cmd support 0x%x\n", page_param.jtg);
	ncb_onfi_trace(LOG_ERR, 0x14df, "    %s support Random data out\n", (page_param.jtg & BIT0) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xa03c, "    %s support MP program\n", (page_param.jtg & BIT1) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xc182, "    %s support MP copyback\n", (page_param.jtg & BIT2) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xb845, "    %s support MP erase\n", (page_param.jtg & BIT3) ? "" : "NOT ");

	ncb_onfi_trace(LOG_ERR, 0xafd0, "extended parameter page length %d\n", page_param.ext_param_page_length);
	ncb_onfi_trace(LOG_ERR, 0x2052, "# copy %d\n", page_param.num_of_param_pages);

	ncb_onfi_trace(LOG_ERR, 0x597b, "---------------------\nManufacturer information block\n");
	ncb_onfi_trace(LOG_ERR, 0xc527, "Manufacturer:");
	for (i=0; i<12;i++) {
		ncb_onfi_trace(LOG_ERR, 0x3cbc, "%c", page_param.manufacturer[i]);
	}
	ncb_onfi_trace(LOG_ERR, 0x56e5, "\n");
	ncb_onfi_trace(LOG_ERR, 0x7481, "Device model:");
	for (i=0; i<20;i++) {
		ncb_onfi_trace(LOG_ERR, 0x9d21, "%c", page_param.model[i]);
	}
	ncb_onfi_trace(LOG_ERR, 0x66e5, "\n");
	ncb_onfi_trace(LOG_ERR, 0xecfb, "JEDEC manufacturer ID %x\n", page_param.jedec_id);
	ncb_onfi_trace(LOG_ERR, 0x88be, "Date code %x\n", page_param.date_code);

	ncb_onfi_trace(LOG_ERR, 0x193a, "---------------------\nMemory organization block\n");
	ncb_onfi_trace(LOG_ERR, 0x49d5, "%d byte/page\n", page_param.byte_per_page);
	ncb_onfi_trace(LOG_ERR, 0xb683, "%d spare byte\n", page_param.spare_bytes_per_page);
	ncb_onfi_trace(LOG_ERR, 0x9556, "%d page/block\n", page_param.pages_per_block);
	ncb_onfi_trace(LOG_ERR, 0xc7ef, "%d blk/lun\n", page_param.blocks_per_lun);
	ncb_onfi_trace(LOG_ERR, 0x4028, "%d lun\n", page_param.lun_count);
	ncb_onfi_trace(LOG_ERR, 0x4a3f, "%d addr cycles\n", page_param.addr_cycles);
	ncb_onfi_trace(LOG_ERR, 0x7e59, "%d bit per cell\n", page_param.bits_per_cell);
	ncb_onfi_trace(LOG_ERR, 0xfce0, "Max bad blocks per LUN %d\n", page_param.bb_per_lun);
	ncb_onfi_trace(LOG_ERR, 0xf779, "Block endurance %d\n", page_param.block_endurance);
	ncb_onfi_trace(LOG_ERR, 0xaacd, "guaranteed good blocks at the beginning %d\n", page_param.guaranteed_good_blocks);
	ncb_onfi_trace(LOG_ERR, 0xa671, "guaranteed good blocks endurance %d\n", page_param.guaranteed_block_endurance);
	ncb_onfi_trace(LOG_ERR, 0x9905, "%d programs per page\n", page_param.programs_per_page);
	ncb_onfi_trace(LOG_ERR, 0x4272, "ECC correctablity %d bits\n", page_param.ecc_bits);

	ncb_onfi_trace(LOG_ERR, 0x0660, "# interleave addr bits %d\n", page_param.interleaved_bits);
	ncb_onfi_trace(LOG_ERR, 0x474c, "interleave operation attributes %x\n", page_param.interleaved_ops);
	ncb_onfi_trace(LOG_ERR, 0xecf8, "    %s support read cache\n", (page_param.interleaved_ops & BIT4) ? "" : "NOT ");
	if (page_param.interleaved_ops & BIT3)
		ncb_onfi_trace(LOG_ERR, 0x044e, "    no addr restrictions for cache operations\n");
	else
		ncb_onfi_trace(LOG_ERR, 0x056a, "    has addr restrictions for cache operations\n");
	ncb_onfi_trace(LOG_ERR, 0x3146, "    %s support program cache\n", (page_param.interleaved_ops & BIT2) ? "" : "NOT ");
	if (page_param.interleaved_ops & BIT1)
		ncb_onfi_trace(LOG_ERR, 0x9a71, "    no block addr restrictions\n");
	else
		ncb_onfi_trace(LOG_ERR, 0xeff2, "    has block addr restrictions\n");
	ncb_onfi_trace(LOG_ERR, 0x770e, "    support %s interleave\n", (page_param.interleaved_ops & BIT0) ? "" : "NOT ");

	if (page_param.features & BIT9) {
		ncb_onfi_trace(LOG_ERR, 0x4946, "EZ nand support %x\n", page_param.ez_nand_support);
		ncb_onfi_trace(LOG_ERR, 0x7fce, "    %s support en/dis auto retry\n", (page_param.ez_nand_support & BIT0) ? "" : "NOT ");
		ncb_onfi_trace(LOG_ERR, 0x91a3, "    %s support copyback for other planes & LUNs\n", (page_param.ez_nand_support & BIT1) ? "" : "NOT ");
		ncb_onfi_trace(LOG_ERR, 0xe52f, "    %s require copyback adjacency\n", (page_param.ez_nand_support & BIT2) ? "" : "NOT ");
	}

	ncb_onfi_trace(LOG_ERR, 0x74a9, "---------------------\nElectrical parameters block\n");
	ncb_onfi_trace(LOG_ERR, 0x5a6c, "I/O pad capacitance per CE\n", page_param.io_pin_capacitance_max);
	ncb_onfi_trace(LOG_ERR, 0xe61c, "async timing mode support 0x%x \n", page_param.async_timing_mode);
	ncb_onfi_trace(LOG_ERR, 0x19f7, "tPROG %dus \n", page_param.t_prog);
	ncb_onfi_trace(LOG_ERR, 0x35e0, "tERS %dus \n", page_param.t_bers);
	ncb_onfi_trace(LOG_ERR, 0xc9f1, "tR %dus \n", page_param.t_r);
	ncb_onfi_trace(LOG_ERR, 0xd365, "tCCS %dns \n", page_param.t_ccs);
	ncb_onfi_trace(LOG_ERR, 0x6788, "NV-DDR timing mode support 0x%x \n", page_param.ddr_timing_mode);
	ncb_onfi_trace(LOG_ERR, 0xe153, "NV-DDR2 timing mode support 0x%x \n", (page_param.ddr2_timing_mode_h << 8) + page_param.ddr2_timing_mode_l);
	ncb_onfi_trace(LOG_ERR, 0x782b, "NV-DDR3 timing mode support 0x%x \n", page_param.ddr3_timing_mode);
	ncb_onfi_trace(LOG_ERR, 0x4bbb, "NV-DDR/DDR2 features 0x%x \n", page_param.ddr_ddr2_features);
	ncb_onfi_trace(LOG_ERR, 0xadf7, "    tCAD value %s to use\n", (page_param.ddr_ddr2_features & BIT0) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xdad7, "    typical capacitance value %s preset\n", (page_param.ddr_ddr2_features & BIT1) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x6fb2, "    %s support CLK stop for data input\n", (page_param.ddr_ddr2_features & BIT2) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x7ff2, "    %s requires Vpp enablement sequence\n", (page_param.ddr_ddr2_features & BIT3) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xf4ec, "CK pin capacitance %d\n", page_param.clk_pin_capacitance_typ);
	ncb_onfi_trace(LOG_ERR, 0x0f8b, "I/O pin capacitance %d\n", page_param.io_pin_capacitance_typ);
	ncb_onfi_trace(LOG_ERR, 0x2f31, "Input pin capacitance %d\n", page_param.input_pin_capacitance_typ);
	ncb_onfi_trace(LOG_ERR, 0x0626, "Drive strength 0x%x\n", page_param.driver_strength_support);
	ncb_onfi_trace(LOG_ERR, 0x7743, "    %s support drive strength settings\n", (page_param.driver_strength_support & BIT0) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x7e58, "    %s support 25ohm drive strength\n", (page_param.driver_strength_support & BIT1) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xde43, "    %s support 18ohm/50ohm drive strength\n", (page_param.driver_strength_support & BIT2) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x0d8b, "tR (MP) %dus \n", page_param.t_int_r);
	ncb_onfi_trace(LOG_ERR, 0x614e, "tADL %dns \n", page_param.t_adl);
	if (page_param.features & BIT9) {
		ncb_onfi_trace(LOG_ERR, 0x3157, "tR for EZ nand %dus \n", page_param.t_r_ez);
	}
	ncb_onfi_trace(LOG_ERR, 0xda59, "NV-DDR2/3 features %x\n", page_param.ddr23_features);
	ncb_onfi_trace(LOG_ERR, 0x3232, "    %s support self-termination ODT\n", (page_param.ddr23_features & BIT0) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x8fbc, "    %s support matrix-termination ODT\n", (page_param.ddr23_features & BIT1) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x65ee, "    %s support 30 Ohms ODT\n", (page_param.ddr23_features & BIT2) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x2b1e, "    %s support differential RE_n\n", (page_param.ddr23_features & BIT3) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0x1f67, "    %s support differential DQS\n", (page_param.ddr23_features & BIT4) ? "" : "NOT ");
	ncb_onfi_trace(LOG_ERR, 0xc822, "    %s requires VREFQ for >=200MT/s\n", (page_param.ddr23_features & BIT5) ? "" : "NOT ");

	ncb_onfi_trace(LOG_ERR, 0xdd23, "NV-DDR2/3 warmup cycles 0x%x, input %d, output %d\n", page_param.ddr23_warmup_cycles, page_param.ddr23_warmup_cycles >> 4, page_param.ddr23_warmup_cycles & 0xF);

	ncb_onfi_trace(LOG_ERR, 0x7c13, "---------------------\nVendor block\n");
	ncb_onfi_trace(LOG_ERR, 0x9b30, "Vendor revision %d\n", page_param.vendor_revision);
	ncb_onfi_trace(LOG_ERR, 0x9d60, "CRC 0x%x \n", page_param.crc);

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
		.reg1.b.ind_byte1 = 0x00,
		.xfcnt = 0,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_close(&ctrl);

	nand_wait_ready(ch, ce);

	ndcu_ind_t ctrl_r = {
		.write = false,
		.cmd_num = 0,
		.reg1.b.ind_byte0 = 0x00,
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

#ifdef HAVE_SYSTEMC
/*!
 * @brief Read parameter page content from table
 *
 * @return Not used
 */
init_code static void nand_read_page_parameter_from_tbl(void)
{
	struct nand_page_param p = {
		.sig = 0x49464e4f,
		.revision = 0x3fe,
		.features = 0xfdfa,
		.opt_cmd = 0x3fff,
		.jtg = 0xf,
		.ext_param_page_length = 0x3,
		.num_of_param_pages = 0x3d,
		.manufacturer = {0x4d, 0x49, 0x43, 0x52, 0x4f, 0x4e, 0x20, 0x20,
			0x20, 0x20, 0x20, 0x20},
		.model = {0x4d, 0x54, 0x32, 0x39, 0x46, 0x33, 0x54, 0x30, 0x38,
			0x45, 0x55, 0x48, 0x42, 0x42, 0x4d, 0x34, 0x20, 0x20,
			0x20, 0x20},
		.jedec_id = 0x2c,
		.date_code = 0x0,
		.byte_per_page = 0x4000,
		.spare_bytes_per_page = 0x8a0,
		.data_bytes_per_ppage = 0x0,
		.spare_bytes_per_ppage = 0x0,
		.pages_per_block = 0x600,
		.blocks_per_lun = 0x890,
		.lun_count = 0x2,
		.addr_cycles = 0x23,
		.bits_per_cell = 0x3,
		.bb_per_lun = 0x94,
		.block_endurance = 0x20f,
		.guaranteed_good_blocks = 0x1,
		.guaranteed_block_endurance = 0x0,
		.programs_per_page = 0x1,
		.ppage_attr = 0x0,
		.ecc_bits = 0xff,
		.interleaved_bits = 0x2,
		.interleaved_ops = 0x1e,
		.ez_nand_support = 0x0,
		.io_pin_capacitance_max = 0x4,
		.async_timing_mode = 0x3f,
		.program_cache_timing_mode = 0x0,
		.t_prog = 0x157c,
		.t_bers = 0x7530,
		.t_r = 0xa9,
		.t_ccs = 0x190,
		.ddr_timing_mode = 0x3f,
		.ddr2_timing_mode_l = 0xff,
		.ddr_ddr2_features = 0x2,
		.clk_pin_capacitance_typ = 0x35,
		.io_pin_capacitance_typ = 0x20,
		.input_pin_capacitance_typ = 0x32,
		.input_pin_capacitance_max = 0x6,
		.driver_strength_support = 0x3,
		.t_int_r = 0xab,
		.t_adl = 0x96,
		.t_r_ez = 0x0,
		.ddr23_features = 0x1b,
		.ddr23_warmup_cycles = 0x44,
		.ddr3_timing_mode = 0x7f,
		.ddr2_timing_mode_h = 0x1,
		.vendor_revision = 0x2,
		.crc = 0x9D41,
	};
	memcpy(&page_param, &p, sizeof(p));
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
	if (page_param.sig != 0x49464E4F/*ONFI_PARAM_PAGE_SIGNATURE*/) {
		ncb_onfi_trace(LOG_ERR, 0xa958, "Signature wrong\n");
		return;
	}
#ifndef HAVE_SYSTEMC
	if (page_param.crc != page_param_crc16((u8*)&page_param, sizeof(struct nand_page_param) - 2)) {
		ncb_onfi_trace(LOG_ERR, 0x4d35, "CRC wrong %x\n", page_param_crc16((u8*)&page_param, sizeof(struct nand_page_param) - 2));
		sys_assert(0);
	}
#endif
	u32 i;
	for (i = 0; i < sizeof(page_param.manufacturer); i++) {
		if (page_param.manufacturer[i] == ' ') {
			page_param.manufacturer[i] = 0;
			break;
		}
	}
	sys_assert(i < sizeof(page_param.manufacturer));
	ncb_onfi_trace(LOG_INFO, 0x6254, "Vendor: %s", page_param.manufacturer);
	page_param.manufacturer[i] = ' ';
	for (i = 0; i < sizeof(page_param.model); i++) {
		if (page_param.model[i] == ' ') {
			page_param.model[i] = 0;
			break;
		}
#if HAVE_YMTC_SUPPORT
		if (page_param.model[i] == 0) {
			break;
		}
#endif
	}
	sys_assert(i < sizeof(page_param.model));
	ncb_onfi_trace(LOG_INFO, 0xe56e, "Model: %s", page_param.model);
	page_param.model[i] = ' ';

	switch(clz(page_param.revision)) {
	case 21:
		ncb_onfi_trace(LOG_INFO, 0x8fe4, "ONFI 4.1");
		break;
	case 22:
		ncb_onfi_trace(LOG_INFO, 0x8fe3, "ONFI 4.0");
		break;
	case 23:
		ncb_onfi_trace(LOG_INFO, 0xc534, "ONFI 3.2");
		break;
	default:
		ncb_onfi_trace(LOG_INFO, 0xe122, "ONFI ?.?, revision %x", page_param.revision);
		sys_assert(0);
		break;
	}
	switch(page_param.bits_per_cell) {
	case 3:
		ncb_onfi_trace(LOG_INFO, 0x02b8, "TLC");
		break;
	case 4:
		ncb_onfi_trace(LOG_INFO, 0xa443, "QLC");
		break;
	default:
		ncb_onfi_trace(LOG_WARNING, 0x733f, "%dLC", page_param.bits_per_cell);
		break;
	}
	nand_info.fake_page_param = false;
	nand_info.geo.nr_luns = page_param.lun_count;
#if HAVE_YMTC_SUPPORT
	if (nand_is_YMN09TC1B1DC6C()) {
		nand_info.geo.nr_luns = 1;
	}
#endif
	nand_info.geo.nr_planes = 1 << page_param.interleaved_bits;
	nand_info.geo.nr_blocks = page_param.blocks_per_lun / nand_info.geo.nr_planes;
	nand_info.geo.nr_pages = page_param.pages_per_block;
	nand_info.page_sz = page_param.byte_per_page;
	nand_info.spare_sz = page_param.spare_bytes_per_page;
	nand_info.bit_per_cell = page_param.bits_per_cell;
	nand_info.addr_cycles = page_param.addr_cycles;

	// Timing mode support print and record max timing mode
	ncb_onfi_trace(LOG_ERR, 0x877b, "Support DDR1/2/3 tm: ");
	if (page_param.features & BIT5) {
		u32 support = page_param.ddr_timing_mode;
		sys_assert(support != 0);
		sys_assert((support & (support + 1)) == 0);
		if (!nand_info.vcc_1p2v) {
			nand_info.max_tm = NDCU_IF_DDR | (31 - clz(support));
		}
		ncb_onfi_trace(LOG_ERR, 0xf157, "%d/", (31 - clz(support)));
	} else {
		ncb_onfi_trace(LOG_ERR, 0x905b, "-/");
	}

	if (page_param.features & BIT10) {
		u32 support = (page_param.ddr2_timing_mode_h << 8) + page_param.ddr2_timing_mode_l;
		sys_assert(support != 0);
		sys_assert((support & (support + 1)) == 0);
		if (!nand_info.vcc_1p2v) {
			nand_info.max_tm = NDCU_IF_DDR2 | (31 - clz(support));
		}
		ncb_onfi_trace(LOG_ERR, 0x89f6, "%d/", (31 - clz(support)));
	} else {
		ncb_onfi_trace(LOG_ERR, 0x2531, "-/");
	}
	if (page_param.features & BIT13) {
		u32 support = page_param.ddr3_timing_mode;
		if (support != 0) {
			sys_assert(support != 0);
			sys_assert((support & (support + 1)) == 0);
			if (nand_info.vcc_1p2v) {
				nand_info.max_tm = NDCU_IF_DDR3 | (31 - clz(support) + 3);
			}
			ncb_onfi_trace(LOG_ERR, 0x4d58, "%d\n", (31 - clz(support) + 3));
		} else {
			ncb_onfi_trace(LOG_ERR, 0x21ca, "-\n");
		}
	} else {
		ncb_onfi_trace(LOG_ERR, 0x32cb, "-\n");
	}
	if (nand_info.vcc_1p2v) {
		sys_assert(page_param.ddr3_timing_mode != 0);
#if HAVE_YMTC_SUPPORT
		if (page_param.ddr3_timing_mode == 0x1FFF) {
			page_param.ddr3_timing_mode >>= 3;
		}
#endif
		nand_info.max_speed = nand_speed((page_param.ddr3_timing_mode << 3) | 7);
#if HAVE_YMTC_SUPPORT
		if (page_param.model[11] == 'C') {
			switch (page_param.model[12]) {
			case '2':
				nand_info.max_speed = 666;
				break;
			case '3':
				nand_info.max_speed = 800;
				break;
			case '4':
				nand_info.max_speed = 1200;
				break;
			case '5':
				nand_info.max_speed = 1333;
				break;
			case '6':
				nand_info.max_speed = 1600;
				break;
			default:
				break;
			}
		}
#endif

#if HAVE_MICRON_SUPPORT
	if (nand_is_b27b()) {
		// Application_Note_NAND_interface_setting_for_higher_speed_rev1.1.pdf by Wei Jiang
#ifdef M2
		if (nand_target_num() * nand_lun_num() >= 4) {
			nand_info.max_speed = 1066;
		}
#else
		nand_info.max_speed = 666;
#endif
	}
#endif
	} else {
		nand_info.max_speed = nand_speed(page_param.ddr2_timing_mode_l + (page_param.ddr2_timing_mode_h << 8));
	}
#if WA_FORCE_SDR
	ncb_onfi_trace(LOG_ERR, 0x270e, "Workaround: for SDR tm 4\n");
	nand_info.max_tm = NDCU_IF_SDR | 4;
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
	ndcu_delay(tFEAT);
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
	ndcu_delay(tFEAT);

	return data;
}

/*!
 * @brief Set feature function(main)
 *
 * @param[in] addr		set/get feature addr
 * @param[in] val		value
 *
 * @return Not used
 */
void nand_set_feature(u32 addr, u32 value)
{
	u32 ch, ce;
	u32 output;

	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}
			set_feature(ch, ce, addr, value);
			output = get_feature(ch, ce, addr);
			if (output != value) {
				ncb_onfi_trace(LOG_WARNING, 0xcd85, "ch %d ce %d sf %b %x != %x", ch, ce, addr, value, output);
				// Feature address 0xA0-0xA2,0xA3-0xAC, get feature value != set feature value
				//sys_assert(0);
			}
		}
	}
}

/*!
 * @brief Set flash timing
 *
 * @return Not used
 */
void nand_set_timing(void)
{
	bool err = false;
	u32 ch, ce;
	u32 val, output;

#if HAVE_MICRON_SUPPORT
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}
#if defined(FPGA)
			set_feature(ch, ce, NAND_FA_ONFI_DDR_CONFIG, 0x36);// Enable 75ohm ODT, RE/DQS complement, disable Vref on FPGA
#else
			val = 0x37;
#if WARMUP_RD_CYCLES
			val |= (ctz(WARMUP_RD_CYCLES) + 1) << 8;
#endif
#if WARMUP_WR_CYCLES
			val |= (ctz(WARMUP_WR_CYCLES) + 1) << 12;
#endif
			set_feature(ch, ce, NAND_FA_ONFI_DDR_CONFIG, val);// Enable 75ohm ODT, RE/DQS complement and Vref
#endif
#ifdef M2
			set_feature(ch, ce, NAND_FA_ONFI_DRV_STRGTH, 0x2);// Enable 35 Ohms over drive strength
#else
			set_feature(ch, ce, NAND_FA_ONFI_DRV_STRGTH, 0x1);// Enable 25 Ohms over drive strength
#endif
#if ENABLE_VPP
			set_feature(ch, ce, NAND_FA_ONFI_VPP_CONFIG, 0x1);// Enable VPP
#endif
		}
	}
#endif

#if HAVE_YMTC_SUPPORT
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}
			//set_feature(ch, ce, NAND_FA_ONFI_DRV_STRGTH, 0x3);
#if ENABLE_VPP
			set_feature(ch, ce, NAND_FA_ONFI_VPP_CONFIG, 0x1);// Enable VPP
#endif
		}
	}
#endif

#if defined(HAVE_SYSTEMC) || defined(FPGA)
	/* nfx2 is 40Mhz in systemC */
	val = (nand_info.max_tm & 0xFFFFFFF0) | 0x1;
#else
	val = nand_info.max_tm;
#endif
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}
			set_feature(ch, ce, NAND_FA_ONFI_MODE, val | 0x40);// Enable 80h clear only selected plane page register
		}
	}

	if (!nand_is_1p2v()) {
		ndcu_set_tm(val & 0xF0, val & 0x0F);
	}

	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}
			output = get_feature(ch, ce, NAND_FA_ONFI_MODE);
			if (output != (val | 0x40)) {
				ncb_onfi_trace(LOG_ERR, 0xdad9, "ch %d ce %d ddr fail %x\n", ch, ce, output);
				err = true;
			}
		}
	}

#if HAVE_YMTC_SUPPORT
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}
			val = 0x6;
#if WARMUP_RD_CYCLES
			val |= (ctz(WARMUP_RD_CYCLES) + 1) << 8;
#endif
#if WARMUP_WR_CYCLES
			val |= (ctz(WARMUP_WR_CYCLES) + 1) << 12;
#endif
			set_feature(ch, ce, NAND_FA_ONFI_DDR_CONFIG, val);// Enable ODT, RE/DQS complement and Vref
		}
	}
#endif

	val = nand_info.max_tm;
	if (!err) {
		ncb_onfi_trace(LOG_ERR, 0xcdb7, "Set ");
		switch(val & 0xF0) {
		case NDCU_IF_SDR:
			ncb_onfi_trace(LOG_ERR, 0x364c, "SDR");
			break;
		case NDCU_IF_DDR:
			ncb_onfi_trace(LOG_ERR, 0x1434, "DDR");
			break;
		case NDCU_IF_DDR2:
			ncb_onfi_trace(LOG_ERR, 0x2ade, "DDR2");
			break;
		case NDCU_IF_DDR3:
			ncb_onfi_trace(LOG_ERR, 0x2adf, "DDR3");
			break;
		}

		ncb_onfi_trace(LOG_ERR, 0x8ff5, " tm %d pass\n", val & 0xF);
		nand_info.cur_tm = val;
	}
}
#endif
/*! @} */
