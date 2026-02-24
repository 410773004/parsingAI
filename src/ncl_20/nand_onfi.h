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
#if HAVE_ONFI_SUPPORT
/*! \brief The NAND param page */
// Refer to ONFI4.0 spec
struct nand_page_param {
	/* rev info and features block */
	/* 'O' 'N' 'F' 'I'  */
	__le32 sig;// 0-3
	__le16 revision;// 4-5
	__le16 features;// 6-7
	__le16 opt_cmd;// 8-9
	u8 jtg;// 10, ONFI-JEDEC JTG primary advanced command support
	u8 rsvd11;// 11
	__le16 ext_param_page_length;// 12-13
	u8 num_of_param_pages; // 14
	u8 rsvd15[17];// 15-31

	/* manufacturer information block */
	char manufacturer[12];// 32-43
	char model[20];// 44-63
	u8 jedec_id;// 64
	__le16 date_code;// 65-66
	u8 rsvd67[13];// 67-79

	/* memory organization block */
	__le32 byte_per_page;// 80-83
	__le16 spare_bytes_per_page;// 84-85
	__le32 data_bytes_per_ppage;//86-89, obsolete
	__le16 spare_bytes_per_ppage;// 90-91, obsolete
	__le32 pages_per_block;// 92-95
	__le32 blocks_per_lun;// 96-99
	u8 lun_count;// 100
	u8 addr_cycles;// 101
	u8 bits_per_cell;// 102
	__le16 bb_per_lun;// 103-104
	__le16 block_endurance;// 105-106
	u8 guaranteed_good_blocks;// 107
	__le16 guaranteed_block_endurance;// 108-109
	u8 programs_per_page;// 110
	u8 ppage_attr;// 111, obsolete
	u8 ecc_bits;// 112
	u8 interleaved_bits;// 113
	u8 interleaved_ops;// 114
	u8 ez_nand_support;// 115
	u8 rsvd116[12];// 116-127

	/* electrical parameter block */
	u8 io_pin_capacitance_max;// 128
	__le16 async_timing_mode;// 129-10
	__le16 program_cache_timing_mode;// 131-132
	__le16 t_prog;// 133-134
	__le16 t_bers;// 135-136
	__le16 t_r;// 137-138
	__le16 t_ccs;// 139-140
	u8 ddr_timing_mode;// 141 NV-DDR timing mode support
	u8 ddr2_timing_mode_l;// 142 NV-DDR2 timing mode support, pair with byte 162
	u8 ddr_ddr2_features;// 143
	__le16 clk_pin_capacitance_typ;// 144-145
	__le16 io_pin_capacitance_typ;// 146-147
	__le16 input_pin_capacitance_typ;// 148-149
	u8 input_pin_capacitance_max;// 150
	u8 driver_strength_support;// 151
	__le16 t_int_r;// 152-153
	__le16 t_adl;// 154-155
	__le16 t_r_ez;// 156-157 tR typical page read time for EZ NAND (us)
	u8 ddr23_features;// 158 NV-DDR2/3 features
	u8 ddr23_warmup_cycles;// 159 NV-DDR2/3 warmup cycles
	__le16 ddr3_timing_mode;// 160-161 NV-DDR3 timing mode support
	u8 ddr2_timing_mode_h;// 162 NV-DDR2 timing mode support, pair with byte 142
	u8 rsvd163;// 163

	/* vendor */
	__le16 vendor_revision;// 164-165
	u8 vendor[88];

	__le16 crc;
} PACKED;

extern struct nand_page_param page_param;
#endif
