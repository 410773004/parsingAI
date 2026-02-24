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

#ifndef _FINSTR_H_
#define _FINSTR_H_

#include "inc/ficu_reg_access.h"
#include "inc/ncb_ficu_register.h"
#include "nand_cfg.h"

enum {
	FINST_TYPE_READ		= 0x2,
	FINST_TYPE_PROG		= 0x1,
	FINST_TYPE_ERASE	= 0x0,
	FINST_TYPE_READ_STS	= 0x3,
	FINST_TYPE_READ_DATA	= FINST_TYPE_READ | BIT2,
};

#define ERD_DEC_START	BIT0
#define ERD_INC_READ	BIT1
#define ERD_IPI_ENABLE	BIT2

/*! \brief Xfer Count Setting */
enum finstr_xfcnt_t {
	FINST_XFER_ZERO 	= 0x0,	///< Zero
	FINST_XFER_1B,			///< 1B
	FINST_XFER_2B,			///< 2B
	FINST_XFER_4B,			///< 4B
	FINST_XFER_8B,			///< 8B
	FINST_XFER_16B, 		///< 16B
	FINST_XFER_PAGE_PAD,	///< Full page write with padding
	FINST_XFER_ONE_DU,		///< NAND_DU_SIZE
	FINST_XFER_ONE_PAGE, 		///< PAGE_SIZE
	FINST_XFER_TWO_PAGES, 		///< Two pages
	FINST_XFER_MAX_LUT_COUNT,	///< Max transfer count, don't greater than 30
	FINST_XFER_AUTO		= 0x1F,
};

extern bool finst_info_in_sram;

enum {
	/*! ADDR NUM */
	NAND_FMT_ADDR_NUM_NO_ADR	= 0x0,
	NAND_FMT_ADDR_NUM_1B_COL	= 0x1,
	NAND_FMT_ADDR_NUM_2B_COL	= 0x2,
	NAND_FMT_ADDR_NUM_3B_ROW	= 0x3,
	NAND_FMT_ADDR_NUM_4B_ROW	= 0x4,
	NAND_FMT_ADDR_NUM_5B_COL_ROW	= 0x5,
	NAND_FMT_ADDR_NUM_6B_COL_ROW	= 0x6,
	NAND_FMT_ADDR_NUM_HW		= 0x7,

	/*! ADDR_4B_EN */
	NAND_FMT_4B_DIS			= 0x0,
	NAND_FMT_4B_EN			= 0x1,

	/*! PREFIX IDX */
	NAND_FMT_PREFIX_NO		= 0x0,
	NAND_FMT_PREFIX_CMD0		= 0x1,
	NAND_FMT_PREFIX_CMD1		= 0x2,
	NAND_FMT_PREFIX_CMD2		= 0x3,
	NAND_FMT_PREFIX_CMD3		= 0x4,

	NAND_FMT_PREFIX_CMD4		= 0x5,
	NAND_FMT_PREFIX_CMD5		= 0x6,
	NAND_FMT_PREFIX_CMD6		= 0x7,
	NAND_FMT_PREFIX_CMD7		= 0x8,

	NAND_FMT_PREFIX_MODE_FIRST_PLN	= 0x1,
	NAND_FMT_PREFIX_MODE_ALL_PLN	= 0x0,


	/*! RD_SCMD */
	///  use SCMD0 as the command byte (default), RDY_BIT_CHK0/FAIL_BIT_CHK0/ FAIL_BIT_CHK01/02/03/04 (for MP)
	NAND_FMT_RD_SCMD_SBYTE0		= 0,
	///use SCMD1 as the command byte, RDY_BIT_CHK1/FAIL_BIT_CHK1/ FAIL_BIT_CHK11/12/13/14 (for MP)
	NAND_FMT_RD_SCMD_SBYTE1		= 1,
	///  use SCMD2 as the command byte for LUN0, RDY_BIT_CHK2/FAIL_BIT_CHK2/ FAIL_BIT_CHK21/22/23/24 (for MP)
	NAND_FMT_RD_SCMD_SBYTE2		= 2,
	///  use SCMD3 as the command byte for LUN1, RDY_BIT_CHK3/FAIL_BIT_CHK3/ FAIL_BIT_CHK31/32/33/34 (for MP)
	NAND_FMT_RD_SCMD_SBYTE3		= 3,
	///  use the command byte {F+N},  RDY_BIT_CHK4/FAIL_BIT_CHK4/ FAIL_BIT_CHK41/42/43/44 (for MP)
	NAND_FMT_RD_SCMD_FN	 	= 4,
	NAND_FMT_RD_SCMD_SBYTE4		= 4,
	///use SCMD5 as the command byte. RDY_BIT_CHK1/FAIL_BIT_CHK1
	NAND_FMT_RD_SCMD_SBYTE5 	= 5,
	///use SCMD6 as the command byte. RDY_BIT_CHK2/FAIL_BIT_CHK2
	NAND_FMT_RD_SCMD_SBYTE6 	= 6,
	///use SCMD7 as the command byte. RDY_BIT_CHK3/FAIL_BIT_CHK3
	NAND_FMT_RD_SCMD_SBYTE7 	= 7,


	/*! CMD MP */
	NAND_FMT_NON_FIRST_PL_IDX_0	= 0x0,
	NAND_FMT_NON_FIRST_PL_IDX_1	= 0x1,
	NAND_FMT_NON_FIRST_PL_IDX_2	= 0x2,
	NAND_FMT_NON_FIRST_PL_IDX_3	= 0x3,
	NAND_FMT_NON_LAST_PL_IDX_0	= (0x0 << 2),
	NAND_FMT_NON_LAST_PL_IDX_1	= (0x1 << 2),
	NAND_FMT_NON_LAST_PL_IDX_2	= (0x2 << 2),
	NAND_FMT_NON_LAST_PL_IDX_3	= (0x3 << 2),


	/*! CMD EXT */
	NAND_FMT_1ST_CMD_NO		= (0),
	NAND_FMT_1ST_CMD_IDX_0		= (1),
	NAND_FMT_1ST_CMD_IDX_1		= (2),
	NAND_FMT_1ST_CMD_IDX_2		= (3),
	NAND_FMT_1ST_CMD_IDX_3		= (4),
	NAND_FMT_1ST_CMD_IDX_4		= (5),
	NAND_FMT_1ST_CMD_IDX_5		= (6),
	NAND_FMT_1ST_CMD_IDX_6		= (7),

	NAND_FMT_2ND_CMD_NO		= (0 << 3),
	NAND_FMT_2ND_CMD_IDX_0		= (1 << 3),
	NAND_FMT_2ND_CMD_IDX_1		= (2 << 3),
	NAND_FMT_2ND_CMD_IDX_2		= (3 << 3),
	NAND_FMT_2ND_CMD_IDX_3		= (4 << 3),
	NAND_FMT_2ND_CMD_IDX_4		= (5 << 3),
	NAND_FMT_2ND_CMD_IDX_5		= (6 << 3),
	NAND_FMT_2ND_CMD_IDX_6		= (7 << 3),


	NAND_FMT_READ_CRC_00		= (0 << 6),///< use 00h defined in R_DUMY_CMDx
	NAND_FMT_READ_CRC_06_E0		= (1 << 6),///< read buffer + full address
	NAND_FMT_READ_CRC_05_E0		= (2 << 6),///< read buffer + col_address
	NAND_FMT_READ_CRC_00_05_E0	= (3 << 6),///< read buffer
	NAND_FMT_READ_CRC_EXT_00	= (1 << 8),///< extra 00h before xfer data (also define a register to have the same function)

	NAND_FMT_DATA_ADDR_NORMAL	= (0 << 6),///< Normal
	NAND_FMT_DATA_ADDR_ENH_ROW	= (1 << 6),///< Enhance + row
	NAND_FMT_DATA_ADDR_ENH_FULL	= (2 << 6),///< Enhance + full addr
	NAND_FMT_DATA_ADDR_RSVD		= (3 << 6),///< Reserved

	NAND_FMT_ERASE_JEDEC		= (3 << 6),///< JEDEC 60h-60h-D0h

	NAND_FMT_EXTRA_PROG_POSTFIX	= (3 << 6),///< extra program confirm for Samsung

	NAND_FMT_POLL_NO_WAIT		= (0 << 2),
	NAND_FMT_POLL_WAIT		= (1 << 2),
	NAND_FMT_SEL_WCNT0		= 0,
	NAND_FMT_SEL_WCNT1		= 1,
	NAND_FMT_SEL_WCNT2		= 2,
	NAND_FMT_SEL_WCNT3		= 3,
};

#define READ_STATUS_EHN_MODE	BIT3
#define VSC_NO_POLL	0
#define VSC_CMD1	BIT0
#define VSC_CMD2	BIT1
/*!  Operation Table for NCL command */
/*!
 * @brief Flash instruction format
 */
struct finstr_format {
	union {
		u32 all;
		struct {
			/* 0: 3 */
			u32 fins_vld:1;
			u32 fins:1;
			u32 lins:1;
			u32 susp_prct:1;

			/* 4: 7 */
			u32 poll_dis:1;
			u32 susp_en:1;
			u32 mlun_en:1;
			u32 mp_row_offset_en:1;

			/* 8: 15 */
			u32 ndcmd_fmt_sel:7;
#define erd_dec_mask	ndcmd_fmt_sel
			u32 vsc_en:1;

			/* 16: 23 */
			u32 mp_num:4;
			u32 finst_type:3;
			u32 fins_fuse:1;

			/* 24: 31 */
			u32 mp_enh:1;
			u32 rsvd0:1;
			u32 pg_idx:2;
			u32 xlc_en:1;
			u32 raw_addr_en:1;
			u32 nvcmd_tag_ext:1;
			u32 no_eccu_path:1;
		} b;
	} dw0;

	union {
		u32 all;
		struct {
			u32 fcmd_id:12;
#define erd_id		fcmd_id
// FCMD_ID[7:0] ERD_ID
			u32 rsvd0:4;
			u32 du_dtag_ptr:10;
			u32 finst_info_loc:1;// Finst info in SRAM(1) or DTCM(0)
			u32 xfcnt_sel:5;
		} b;
	} dw1;

	union {
		u32 all;
		struct {
			/* 0: 7 */
			u32 btn_op_type:5;
			u32 ard_schem_sel:3;
#define erd_cmd		ard_schem_sel
/*
http://10.10.0.17/issues/1376
ARD_TEMPL_SEL0 ERD_DEC_START when ERD_EN = 1
ARD_TEMPL_SEL1 ERD_INC_RD when ERD_EN = 1 ERD_DEC_START and ERD_INC_RD cant both be high
ARD_TEMPL_SEL2 IPI_ENABLE {VSC_EN, NDCMD_FMT_SEL[6:0]} ERD_DEC_MASK when ERD_EN = 1 ERD_DEC_START=1
*/
			/* 8: 15 */
			u32 du_fmt_sel:5;
			u32 du_num:3;

			/* 16: 23 */
			u32 hc:1;
			u32 ext_llr_value_en:1;
			u32 host_trx_dis:1;
			u32 scrc_en:1;
			u32 meta_strip:1;
#define phy_page_pad_en	meta_strip
			u32 raid_sram_bank_id:3;

			/* 24: 31 */
			u32 raid_cmd:5;
			u32 force_retry_en:1;
			u32 sd_mode:1;
			u32 erd_en:1;
		} b;
	} dw2;

	union {
		u32 all;
		struct {
			u32 raid_id0:6;
			u32 raid_id0_sram_bank_id:2;

			u32 raid_id1:6;
			u32 raid_id1_sram_bank_id:2;

			u32 raid_id2:6;
			u32 raid_id2_sram_bank_id:2;

			u32 raid_id3:6;
			u32 raid_id3_sram_bank_id:2;
		} b;
	} dw3;
};

/*!
 * @brief NCB VSC format
 */
struct ncb_vsc_format {
	union {
		u32 all;
		struct {
			u32 cmd1:8;
			u32 cmd2:8;

			u32 rd_scmd:4;
			u32 rdy_chk:4;
			u32 fail_chk:4;
			u32 poll_fuse:1;
			u32 rsvd:3;
		} b;
	} dw0;

	union {
		u32 all;
		struct {
			u32 addr_num:3;
			u32 addr_4b_en:1;
			u32 wcnt_sel:4;
			u32 pcnt_sel:2;
			u32 cmd_enh:5;
			u32 zero_xfer:1;

			u32 pre_cmd1:4;
			u32 pre_cmd2:4;
			u32 pre_cmd3:4;
			u32 prefix_mode:2;
			//u32 rd_buff_only:1;
		    u32 vsc_rd_buff_only:1;
			u32 ce_fuse:1;
		} b;
	} dw1;
};

/*!
 * @brief FDA dtag format
 */
struct fda_dtag_format {
	// DW0: FDA header info in FDA format
	union {
		u32 hdr;
		struct {
			u32	fda_col		: 16;
			u32	ch_id		: 4;
			u32	dev_id		: 4;
			u32	vol_sel		: 4;
			u32	mp_num		: 4;
		};
	};

	// DW1: FDA row or PDA info
	union {
		u32	row;		// Row address in FDA format (single plane or mp_row_offset_en=1)
		u32*	row_list;	// Row address in FDA format (multiple plane and mp_row_offset_en=0)
		pda_t	pda;		// PDA address in PDA format (single plane or mp_row_offset_en=1)
		pda_t*	pda_list;	// PDA address in PDA format (multiple plane and mp_row_offset_en=0)
	};

	// DW2 & DW3: Dtag info
	union {
		bm_pl_t dtag;		// Single DTAG usage
		u32 dtag_ptr;		// Multiple DTAG usage
	};
};

union ext_llr_info {
	u32 bit0_soft_pl0 : 4;
	u32 bit1_soft_pl0 : 4;
	u32 bit0_soft_pl1 : 4;
	u32 bit1_soft_pl1 : 4;
	u32 bit0_soft_pl2 : 4;
	u32 bit1_soft_pl2 : 4;
	u32 bit0_soft_pl3 : 4;
	u32 bit1_soft_pl3 : 4;
};

/*!
 * @brief Extra flash instruction format
 */
struct extra_finst_info {
	struct {
		u32 op_cost	: 14;
		u32 rsvd	: 10;
		u32 dev_id	: 4;
		u32 ch_id	: 4;
	};
	union ext_llr_info* ext_llr_ptr;
};

/*!
 * @brief FDMA Meta Format inherit from Shasta
 */
struct meta_descriptor {
	u32 du_cnt		: 8;
	u32 offset		: 24;
};

/*!
 * @brief FDMA Meta Format that hold 16B meta
 */
struct meta_entry {
	u32 meta[META_SIZE/sizeof(u32)];
	//u32 padding;
	//u32* meta_addr;
};

#define SRAM_FINST_INFO_CNT	1024
#define DTCM_FINST_INFO_CNT	256
#define SRAM_META_DTAG_CNT	SRAM_IN_DTAG_CNT
#define SRAM_META_IDX_CNT	1024

struct sram_finst_info_t {
	struct fda_dtag_format	fda_dtag_list[SRAM_FINST_INFO_CNT];
};

struct dtcm_finst_info_t {
	struct fda_dtag_format	fda_dtag_list[DTCM_FINST_INFO_CNT];
};

static inline void ficu_submit_finstr(u32 cnt)
{
#if NCL_USE_DTCM_SQ
#if DEBUG
	extern void ficu_finst_dump(void);
	ficu_finst_dump();
#endif
	extern void ficu_update_sq_wrptr(void);
	ficu_update_sq_wrptr();
#else

#if DEBUG
	extern void ficu_finst_dump(void);
	ficu_finst_dump();
	extern u8 ficu_sq_select;
	switch (ficu_sq_select) {
	case 0:
		ficu_writel(cnt, FICU_FSPM_NORM_FINST_QUE0_CNT_REG);
		break;
	case 1:
		ficu_writel(cnt, FICU_FSPM_HIGH_FINST_QUE0_CNT_REG);
		break;
	case 2:
		ficu_writel(cnt, FICU_FSPM_URGENT_FINST_QUE0_CNT_REG);
		break;
	case 3:
		ficu_writel(cnt, FICU_FSPM_NORM_FINST_QUE1_CNT_REG);
		break;
	case 4:
		ficu_writel(cnt, FICU_FSPM_HIGH_FINST_QUE1_CNT_REG);
		break;
	case 5:
		ficu_writel(cnt, FICU_FSPM_URGENT_FINST_QUE1_CNT_REG);
		break;
	}
#else

#if CPU_ID == CPU_BE || CPU_ID == CPU_BE2
	ficu_writel(cnt, FICU_FSPM_NORM_FINST_QUE0_CNT_REG);
#else
	ficu_writel(cnt, FICU_FSPM_NORM_FINST_QUE1_CNT_REG);
#endif

#endif

#endif
}
#endif
