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

//=============================================================================
//
/*! \file
 * @brief rainier dpe module
 *
 * \addtogroup btn
 * \defgroup dpe
 * \ingroup btn
 * @{
 * Implementation of data process engine
 */
//=============================================================================

#pragma once
#include "dtag.h"

#define MAX_SEARCH_IO_CNT	16		///< concurrent DP search command count


#define DPE_ALIGN_SIZE        (32)
#define DPE_ALIGN_MASK        (DPE_ALIGN_SIZE - 1)
#define DPE_HASH_CAL_MAX_SIZE (1024 * 64 - DPE_ALIGN_SIZE)

typedef enum {
	DPE_HASH_ALGO_SHA = 0,  ///< DPE hash algorithm: SHA2/3
	DPE_HASH_ALGO_SM3 = 1,  ///< DPE hash algorithm: SM3
} dpe_hash_algo_t;

typedef enum {
	DPE_HASH_TYPE_SHA3_256 = 0, ///< DPE hash SHA3 256
	DPE_HASH_TYPE_SHA3_384 = 1, ///< DPE hash SHA3 384
	DPE_HASH_TYPE_SHA3_512 = 2, ///< DPE hash SHA3 512
	DPE_HASH_TYPE_SHA2_256 = 3, ///< DPE hash SHA2 256
	DPE_HASH_TYPE_MAX,
} dpe_hash_type_t;

#define HASH_DIGEST_LEN_SM3      (32)  ///< SM3 hash digest length
#define HASH_DIGEST_LEN_SHA3_256 (32)  ///< SHA3-256 hash digest length
#define HASH_DIGEST_LEN_SHA3_384 (48)  ///< SHA3-384 hash digest length
#define HASH_DIGEST_LEN_SHA3_512 (64)  ///< SHA3-512 hash digest length
#define HASH_DIGEST_LEN_SHA2_256 (32)  ///< SHA2-256 hash digest length
#define HASH_DIGEST_LEN_SHA2_384 (48)  ///< SHA2-384 hash digest length
#define HASH_DIGEST_LEN_SHA2_512 (64)  ///< SHA2-512 hash digest length
#define HASH_DIGEST_LEN_MAX   (HASH_DIGEST_LEN_SHA3_512)

#define DPE_GENERIC_CRYPTO_DATA_UNIT_SHIFT      (5) ///< DPE generic crypto command unit length shift

#define DPE_GENERIC_CRYPTO_KEY_128 (0) ///< DPE generic crypto command key is 128bits
#define DPE_GENERIC_CRYPTO_KEY_256 (2) ///< DPE generic crypto command key is 256bits

#define DPE_GENERIC_CRYPTO_MODE_AES (0) ///< DPE generic crypto command mode is AES
#define DPE_GENERIC_CRYPTO_MODE_SM4 (1) ///< DPE generic crypto command mode is SM4

#define DPE_GENERIC_CRYPTO_ALGO_ECB (0) ///< DPE generic crypto command algorithm is ECB
#define DPE_GENERIC_CRYPTO_ALGO_XTS (1) ///< DPE generic crypto command algorithm is XTS
#define DPE_GENERIC_CRYPTO_ALGO_CBC (2) ///< DPE generic crypto command algorithm is CBC

#define DPE_GENERIC_CRYPTO_IV_LOW_128  (0)  ///< DPE generic crypto command IV selection is LOW 128 bits
#define DPE_GENERIC_CRYPTO_IV_HIGH_128 (1) ///< DPE generic crypto command IV selection is HIGH 128 bits

#define DPE_GENERIC_CRYPTO_OP_ENCRYPTION (0) ///< DPE generic crypto command operation: encryption
#define DPE_GENERIC_CRYPTO_OP_DECRYPTION (1) ///< DPE generic crypto command operation: decryption

/*!
 * @brief DPE RSA mode - key bit length
 */
enum rsa_mode_ctrl {
	SS_RSA_4096_MODE = 0,///< SS_RSA_4096_MODE
	SS_RSA_3072_MODE = 1,///< SS_RSA_3072_MODE
	SS_RSA_2048_MODE = 2,///< SS_RSA_2048_MODE
	SS_RSA_1024_MODE = 3,///< SS_RSA_1024_MODE
};

/*!
 * @brief SM2 function select enumerate
 */
enum sm2_func_sel {
	SM2_FUNC_SEL_VF = 0,    ///< SM2 Verify
	SM2_FUNC_SEL_LP,        ///< SM2 Get LP
	SM2_FUNC_SEL_KG,        ///< SM2 Generation
	SM2_FUNC_SEL_SN,        ///< SM2 Signature
};

/*!
 * @brief DPE key wrap/unwrap and security subsystem program configuration
 * parameters.
 */
typedef struct _kwp_cfg_t {
	u8 cfp_en;		///< Chip Finger Print Enable
	u8 aes_sm4_mode;	///< AES or SM4 mode
	u8 din_size;	///< Size of input data 128 or 256 bit
	u8 kek_size;	///< Size of KEK. 128 or 256 bit
	u8 din_spr_idx;	///< SPR index of input data
	u8 kek_spr_idx;	///< SPR index of KEK
	u8 wrap_op;	///< Wrap/Unwrap operation RAW ENC/DEC, KWP/KUWP
	u8 sec_ena_uwp_rslt_spr_idx; ///< Secure mode enable unwrap result SPR idx
	u8 sec_ena_uwp_rslt_mek_idx; ///< Secure mode enable unwrap result MEK idx
	u8 sec_mode_en;	///< Secure mode enable
	u8 test_mode_en;	///< Test mode enabled
} kwp_cfg_t;

/*!
 * @brief SPR register index
 */
enum spr_reg_idx {
	SS_SPR_REG0 = 0,	///< SPR register 0
	SS_SPR_REG1,		///< SPR register 1
	SS_SPR_REG2,		///< SPR register 2
	SS_SPR_REG3,		///< SPR register 3
};

/*!
 * @brief Key Wrap encrypt control
 */
enum key_wp_enc_ctrl {
	SS_RAW_ENCR = 0x4,	///< Raw Encryption
	SS_RAW_DECR = 0x8,	///< Raw Decryption
	SS_KEY_WRAP = 0x5,	///< Key Wrap
	SS_KEY_UWRAP = 0xA,	///< Key UnWrap
};

/*!
 * @brief data process engine result
 */
typedef struct _dpe_rst_t {
	int error;		/*! command error or not */
	union {
		struct {
			u16 idx;		///< data index
			u16 cmp_err_cnt;	///< compare error cnt: max 15
			u32 err_loc;		///< in 32 bytes, [4:0] is zero
		} cmp_fmt1;
		struct {
			u16 cmp_err_cnt;	///< compare error cnt: max 4095
			u32 err_loc;		///< in 32 bytes, [4:0] is zero
		} cmp_fmt2;
		struct {
			u16 search_hit;		///< search hit cnt: max 4095
			u16 rsp_buf_id;		///< response buffer id
		} search;
		struct {
			u8 func_sel;		///< refer to enum sm2_func_sel
			union {
				struct {
					u16 ver_result : 1;
					u16 finish_state : 1;
					u16 rsvd_2 : 2;
					u16 state : 2;
					u16 rsvd_6 : 2;
					u16 abn_sig : 8;
				} sts;
				u16 all;
			} cal_sts;
		} sm2;
		struct {
			u8 trng_chk_flag;
		} trng;
	};
} dpe_rst_t;

/*!
 * @brief data process engine callback function type
 */
typedef int (*dpe_cb_t)(void *ctx, dpe_rst_t *rst);

/*!
 * @brief data process engine search result format
 */
typedef union _dpe_search_loc_t {
	u16 all;
	struct {
		u16 sram_pos : 12;	///< sram word position, sram word is 8 dwords
		u16 dw_pos : 4;		///< dword position in sram word, 0~7
	} b;
} dpe_search_loc_t;

/*! @brief header of bm_scrub_ddr from other cpu */
typedef struct _ddr_scrub_ipc_hdl_t {
	u64 start;			///< ddr start offset
	u64 len;			///< scrub length
	u32 pat;			///< scrub pattern
} ddr_scrub_ipc_hdl_t;

typedef struct _bm_copy_ipc_hdl_t {
	u64 src;
	u64 dst;
	u32 len;
	void* ctx;
	dpe_cb_t cb;
	u8 tx;
} bm_copy_ipc_hdl_t;

/*! @brief header of bm_scrub_ddr from other cpu */
typedef struct _sha3_sm3_calc_ipc_hdl_t {
	void *mem;
	void *result;
	u32 count;
	u32 cur_count;
	dpe_cb_t callback;
	void *ctx;
	bool sm3;
	bool first;
	u8 tx;
} sha3_sm3_calc_ipc_hdl_t;

/*!
 * @brief merge two dtags, it's for partial write
 *
 * Build Data Process engine command to merge two dtag according parameters.
 *
 * @param src		source dtag
 * @param dst		destination dtag
 * @param lba_ofst	lba offset of source dtag to copy from
 * @param nsec		lba sectors to be copied
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern bool bm_data_merge(dtag_t src, dtag_t dst, u8 lba_ofst, u8 nsec, dpe_cb_t callback, void *ctx);

/*!
 * @brief copy data from memory to memory
 *
 * Build Data Process engine command to copy data from source to destination.
 *
 * @param src		source memory address
 * @param dst		destination memory address
 * @param nbytes	number of bytes
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_data_copy(u64 src, u64 dst, u32 nbytes, dpe_cb_t callback, void *ctx);

/*!
 * @brief compare data between two dtags, it's for compare command
 *
 * Build Data Process engine command to compare data between two dtags.
 *
 * @param src		source dtag
 * @param dst		destination dtag
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_data_compare_dtag(dtag_t src, dtag_t dst, dpe_cb_t callback, void *ctx);

/*!
 * @brief compare data between two memory region
 *
 * Build Data Process engine command to compare data between two memory regions.
 *
 * @note limited DDR address in 1G
 *
 * @param mem1		source memory address
 * @param mem2		destination memory address
 * @param len		memory length
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_data_compare_mem(void *mem1, void *mem2, u32 len, dpe_cb_t callback, void *ctx);

/*!
 * @brief setup search result buffer in search result table
 *
 * Data Process engine allow 16 entries to be setup, which mean, Data Process
 * engine support 16 search commands at the same time.
 * If full, return ~0.
 *
 * @param res	result buffer
 *
 * @return	Return search result index or ~0.
 */
extern u32 bm_dpe_setup_search_result_buf(void *res);

/*!
 * @brief search data pattern in memory
 *
 * Build Data Process engine command to search pattern in memory
 *
 * @param mem		memory to be searched
 * @param nbytes	memory length
 * @param mask		search mask
 * @param pat		search pattern
 * @param callback	callback function when command done
 * @param result_id	result id from bm_dpe_setup_search_result_buf
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_data_search(void *mem, u32 nbytes, u32 mask, u32 pat, dpe_cb_t callback, u32 result_id, void *ctx);

/*!
 * @brief fill pattern in memory
 *
 * Build Data Process engine command to fill pattern into memory
 *
 * @param mem		memory to be filled
 * @param nbytes	memory length
 * @param pat		fill pattern
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_data_fill(void *mem, u32 nbytes, u32 pat, dpe_cb_t callback, void *ctx);

/*!
 * @brief fill pattern in DDR memory
 *
 * Build Data Process engine command to fill pattern into DDR memory
 *
 * @param start		start offset address in DDR,
 * @param len		length to be scrubbed
 * @param pat		fill pattern
 *
 * @return		not used
 */
extern void bm_scrub_ddr(u64 start, u64 len, u32 pat);

/*!
 * @brief do SHA3/SM3 calculation
 *
 * Build Data Process engine command for SHA3/SM3 algorithm calculation
 *
 * @param mem		SRAM buffer to store data message
 * @param result	SRAM buffer for store calculated result
 * @param sm3		true for SM3 mode, false for SHA3 mode
 * @param count		How many bytes of data message to be calculated
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_sha3_sm3_calc(void *mem, void *result, bool sm3, u32 count, dpe_cb_t callback, void *ctx);

/*!
 * @brief do SHA3/SM3 calculation part
 *
 * Build part Data Process engine command for SHA3/SM3 algorithm calculation
 *
 * @param mem		SRAM buffer to store data message
 * @param result		SRAM buffer for store calculated result
 * @param sm3		true for SM3 mode, false for SHA3 mode
 * @param count		How many bytes of data message to be calculated
 * @param cur_count	current bytes of data message to be calculated
 * @param callback	callback function when command done
 * @param ctx			caller context
 * @param first		true if this is the first command
 *
 * @return		not used
 */
extern void bm_sha3_sm3_calc_part(void *mem, void *result, bool sm3, u32 count, u32 cur_count, dpe_cb_t callback, void *ctx, bool first);

/*!
 * @wait current dpe data process done
 *
 * @return		not used
 */
extern void bm_wait_dpe_data_process_done(void);

/*!
 * @brief do SM2 calculation
 *
 * Build Data Process engine command for SM2 algorithm calculation
 *
 * @param mem		SRAM buffer to store data message
 * @param result	SRAM buffer for store calculated result
 * @param byte_cnt	How many bytes to be calculated.
 * @param func_sel	SM2 function select, SM2_FUNC_SEL_VF/LP/KG/SN
 * @param callback	callback function when command done
 * @param ctx		caller context
 *
 * @return		not used
 */
extern void bm_sm2_cal(void *mem, void *result, enum sm2_func_sel func_sel, dpe_cb_t callback, void *ctx);

/*!
 * @brief handle DPE command done interrupt
 *
 * @return	None
 */
void dpe_isr(void);

/*!
 * @brief initialize Data Process engine
 *
 * @return	None
 */
void dpe_init(void);

/*!
 * @brief do RSA calculation
 *
 * @param src the pointer to the source buffer(n || d || r || rr || c), 32B aligned
 * @param result the pointer to the result buffer, the length should be 512B, 32B aligned
 * @param callback  call back function pointer
 * @param ctx  pointer of caller context
 * @param mode 0 - SS_RSA_4096_MODE
 *             1 - SS_RSA_3072_MODE
 *             2 - SS_RSA_2048_MODE
 *             3 - SS_RSA_1024_MODE
 *
 * @return None
 */
void bm_rsa_calc(void *src, void *result, unsigned int nprime0, dpe_cb_t callback, void *ctx, enum rsa_mode_ctrl mode);

/*!
 * @brief initialize dpe when need
 *
 * @return	not used
 */
void dpe_early_init(void);

extern void sync_dpe_reset(void);
extern void sync_dpe_copy(u64 src, u64 dst, u32 size);

/*! @} */
