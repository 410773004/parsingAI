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

#pragma once
#include "dpe_export.h"

#define DPE_CPY_SRC_IS_DTCM	0x3
#define DPE_CPY_DST_IS_DTCM	0x2
#define DPE_CPY_DST_IS_ATCM	0x1

/*!
 * @brief Data Process Engine routine types
 */
enum {
	DPE_CMP_FM1 = 0x1,	///< Compare between 2 dtags
	DPE_CMP_FM2 = 0x2,	///< Compare between memory range
	DPE_MERGE = 0x3,	///< Partial write merge between dtags
	DPE_COPY = 0x4,		///< Data Copy from source to dest
	DPE_SEARCH = 0x5,	///< Data/HMB Search
	DPE_FILL = 0x6,		///< Pattern Fill out
	DPE_SHA3_SM3_CAL = 0x8,	///< SHA3/SM3 calculate
	DPE_SM2_CAL = 0x9,	///< SM2 calculate
	DPE_SEC_RND_NUMBER = 0xA,	///< Security Subsystem Random Number Generator
	DPE_SEC_SPR_PROG = 0xB,	///< Security Subsystem SPR register program
	DPE_SEC_KWP_WRAP = 0xC, ///< Security Subsystem key wrap/unwrap process
	DPE_SEC_RSA_CAL = 0xD, ///< Security Subsystem to do the RSA calculation
	DPE_SEC_GEN_CRYPTO = 0xE, ///< Security Subsystem general data encryption/decryption process
};

/*!
 * @brief Data Process request / response entry
 */
typedef struct {
	union {
		u32 all;
		struct {
			u32 bm_tag:12;	/*! BM command tag */
			u32 param:12;	/*! parameter, depend on cmd_code */
			u32 cmd_code:4;	/*! DP engine command code */
			u32 cmd_opt:4;	/*! cmd option, depend on cmd_code */
		} req;

		struct {
			u32 rsp_tag:12;	/*! response command tag */
			u32 rsp_misc:12;/*! depend on cmd_code */
			u32 rsp_code:4;	/*! depend on cmd_code */
			u32 rsp_sts:4;	/*! command status */
		} rsp;
	} hdr;

	u32 payload[3];			/*! command payload, depend on cmd_code */
} dpe_entry_t;

typedef union dpe_cp_hdr_t {
	struct {
		u32 bm_tag : 12;		///< command tag
		u32 ddr_win_1st : 4;		///< ddr offset high bits [35:32]
		u32 ddr_win_2nd : 4;		///< ddr offset high bits [35:32]
		u32 rsvd20 : 4;
		u32 cmd_code : 4;		///< command code
		u32 src_ddr : 1;		///< if source address was DDR
		u32 dst_ddr : 1;		///< if destination address was DDR
		u32 dtcm_sel : 2;       ///<select DTCM for data copy
	} b;
	u32 all;
} dpe_cp_hdr_t;

typedef struct {
	/* command DW 0 */
	union {
		u32 all;
		struct {
			u32 bm_tag :12;             ///< BTN command tag
			u32 sm2_replace_K :1;       ///< replace K with SPR0
			u32 result_to_spr_en :2;    ///< store the result to SPR1, SPR2
			u32 rsvd0 :9;
			u32 cmd_code :4;            ///< command code
			u32 func_sel :3;            ///< SM2 function select: SM2_FUNC_SEL_XX
			u32 rsvd1 :1;
		} b;
	} dw0;
	/* command DW 1 */
	u32 source;                 ///< source address
	/* command DW 2 */
	u32 result;                 ///< result address
	/* command DW 3 */
	u32 dw3;
} dpe_sm2_req_t;

/*!
 * @brief DPE hash command entry structure
 */
typedef struct _dpe_hash_entry_t {
	union {
		u32 all;
		struct {
			u32 bm_tag :12;        ///< DPE command tag
			u32 rsvd0 :10;
			u32 type :2;           ///< DPE hash type for SHA family: DPE_HASH_TYPE_SHAX_XXX
			u32 cmd_code :4;       ///< DPE engine command code
			u32 algo :1;           ///< DPE_HASH_ALGO_XXX
			u32 start :1;          ///< false means the first message slice
			u32 end :1;            ///< true means the last message slice
			u32 rsvd1 :1;

		} b;
	} dw0;

	u32 msg_addr;  ///< input message memory address
	u32 rst_addr;  ///< digest memory address
	u32 count;     ///< message length of this DPE request
} dpe_hash_req_t;

typedef struct dpe_generic_crypto_cmd {
	/* command DW 0 */
	u32 bm_tag :12;              ///< BTN command tag
	u32 source_ddr_window :4;    ///< this is DDR window for the source address if bit[28] = 1
	u32 result_ddr_window :4;    ///< this is DDR window for the result address if bit[29] = 1
	u32 rsvd0 :4;                ///< Reserved field
	u32 cmd_code :4;             ///< DPE command code: DPE_SEC_GEN_CRYPTO
	u32 source_ddr :1;           ///< if 1, the source address points to DDR; if 0, the source address points to SRAM
	u32 result_ddr :1;           ///< if 1, the result address points to DDR; if 0, the result address points to SRAM
	u32 rsvd1 :2;                ///< Reserved field

	/* command DW 1 */
	u32 source;                  ///< starting SRAM/DDR address for source data
	/* command DW 2 */
	u32 result;                  ///< starting SRAM/DDR address for result data
	/* command DW 3 */
	u32 data_units :16;          ///< the total process data length, in the unit of 32B
	u32 spr_index_for_key1 :3;   ///< the SPR register index for Key1
	u32 spr_index_for_key2 :3;   ///< the SPR register index for Key2
	u32 key_size_select :2;      ///< key size selection, b00: 128bit, b10: 256bit, DPE_GENERIC_CRYPTO_KEY_XXX
	u32 algo :2;                 ///< algorithms selection, 0: ECB, 1: XTS, 2: CBC, DPE_GENERIC_CRYPTO_ALGO_XXX
	u32 mode :1;                 ///< 0: AES mode, 1: SM4 mode, DPE_GENERIC_CRYPTO_MODE_XXX
	u32 operation :1;            ///< 0: data encryption, 1: data decryption
	u32 spr_index_for_iv :3;     ///< SPR register index for IV
	u32 part_selection_of_iv :1; ///< 0: select[127:0] of the IV, 1: select[255:128] of the IV
} dpe_generic_crypto_cmd_t;

