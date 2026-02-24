//-----------------------------------------------------------------------------
//		 Copyright(c) 2016-2019 Innogrit Corporation
//			     All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#pragma once

#define IMAGE_CONFIG 0x464E4F43	/* CONF */
#define FWCFG_LENGTH 4096
#define VERSION_MAJ 1
#define VERSION_MIN 2
#define BOARD_CFG_VER 1
#define NVME_CFG_VER 1
#define FTL_CFG_VER 3
#define NCL_CFG_VER 1

typedef struct {
	unsigned int signature;
	unsigned int entry_point;
	unsigned short section_num;
	unsigned short image_dus;
	unsigned int section_csum;
	char project[8];
	unsigned int version_major;
	unsigned int version_minor;
	unsigned int header_csum;
	char reserved[476];
} header_cfg_t;

typedef struct {
	unsigned int version;
	char board_id;
	char odt;
	char cpu_clk;
	char ddr_board;
	char ddr_clk;
	char ddr_size;
	char ddr_vendor;
	//char rsvd1[117];
	char ddr_info[320];
    char dram_train_result[256];
	char reserved[437];
} board_cfg_t;

typedef struct {
	unsigned int version;
	char pmu_support;
	char reserved[507];
} nvme_cfg_t;

typedef struct {
	unsigned int version;
	unsigned int op;
	unsigned int tbl_op;
	unsigned short tbw;
	unsigned short burst_wr_mb;
	unsigned short slc_ep;
	unsigned short native_ep;
	unsigned short user_spare;
	char read_only_spare;
	char wa;
	unsigned int nat_rd_ret_thr;
	unsigned int slc_rd_ret_thr;
	unsigned int max_wio_size;
	char gc_retention_chk;
	char alloc_retention_chk;
	char avail_spare_thr;
	char reserved[473];
} ftl_cfg_t;

typedef struct {
	unsigned int version;
	char eccu_clk;
	char nand_clk;
	char nand_dll_ch[16];
	char nand_ds_ch[16];
	char reserved[1498];//[2010];
} ncl_cfg_t;

typedef struct _fw_config_set_t {
	header_cfg_t header;
	board_cfg_t board;
	nvme_cfg_t nvme;
	ftl_cfg_t ftl;
	ncl_cfg_t ncl;
} fw_config_set_t;
