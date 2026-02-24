//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2018 Innogrit Corporation
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

#include "types.h"
#include "stdio.h"
#include "assert.h"
#include "security_api.h"
#include "string.h"
#include "bf_mgr.h"
#include "sect.h"
#include "console.h"
#include "mod.h"
#include "trng.h"
/*! \cond PRIVATE */
#define __FILEID__ sectest
#include "trace.h"
#if defined(USE_CRYPTO_HW)
extern void crypto_hw_cfg_low_power(u8 disable);

#define SECURITY_FULL_TEST                      1
#define CRYPTO_SEC_SUB_SECURE_MODE	0x00000001
#define CRYPTO_SEC_SUB_TEST_MODE	       0x00000002

typedef union {
	u32 all;
	struct {
		u32 randst_check:1;
		u32 sm2_check:1;
		u32 sm3_check:1;
		u32 sm4_check:1;
		u32 sha256_check:1;
		u32 rsvd:26;
	} b;
} sec_status_t;

sec_status_t sec_status = {0};

#if SECURITY_FULL_TEST
enum {
	AES_256B_KWP_NO_SECURE = 0,
	AES_256B_KUWP_NO_SECURE,
	AES_256B_RAW_ENCRYPT,
	AES_256B_RAW_DECRYPT,
	AES_256B_KWP_SECURE_NO_CFP,
	AES_256B_KUWP_SECURE_NO_CFP,
	AES_256B_KWP_SECURE_CFP,
	AES_256B_KUWP_SECURE_CFP,
	SM4_128B_KWP_NO_SECURE,
	SM4_128B_KUWP_NO_SECURE,
	SM4_128B_RAW_ENCRYPT,
	SM4_128B_RAW_DECRYPT,
	SM4_128B_KWP_SECURE_NO_CFP,
	SM4_128B_KUWP_SECURE_NO_CFP,
	SM4_128B_KWP_SECURE_CFP,
	SM4_128B_KUWP_SECURE_CFP,
	AES_SM4_MAX_TEST_VECTOR,
};

/* Test vector #1 - AES: KEK 256 bit. Key Wrap/Unwrap. Ref: NIST doc. */
fast_data u8 kek_256b[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			          0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			          0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			          0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

/* Test vector #1 - AES: p_MEK_in 256 bit. Key Wrap/Unwrap. Ref: NIST doc. */
fast_data u8 mek_256b_key_p[32] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		                        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
				        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

/* Test vector #1 - AES: c_MEK_out 320 bit. Key Wrap/Unwrap. Ref: NIST doc. */
fast_data u8 mek_320b_key_c[40] = {0x28, 0xC9, 0xF4, 0x04, 0xC4, 0xB8, 0x10, 0xF4,
	                                0xCB, 0xCC, 0xB3, 0x5C, 0xFB, 0x87, 0xF8, 0x26,
			                0x3F, 0x57, 0x86, 0xE2, 0xD8, 0x0E, 0xD3, 0x26,
			                0xCB, 0xC7, 0xF0, 0xE7, 0x1A, 0x99, 0xF4, 0x3B,
			                0xFB, 0x98, 0x8B, 0x9B, 0x7A, 0x02, 0xDD, 0x21};

/* SM4 Data: Moyang's email. Byte order must be reversed to get expected result. */
/* Test vector #6: SM4: KEK 256 bit. Key Wrap/Unwrap. Ref: Moyang's email */
fast_data u8 kek_256b_kwp1_sm4[32] = {0x12, 0x15, 0x35, 0x24, 0xc0, 0x89, 0x5e, 0x81,
	                                   0x84, 0x84, 0xd6, 0x09, 0xb1, 0xf0, 0x56, 0x63,
				           0x06, 0xb9, 0x7b, 0x0d, 0x46, 0xdf, 0x99, 0x8d,
					   0xb2, 0xc2, 0x84, 0x65, 0x89, 0x37, 0x52, 0x12};

/* Test vector #6: SM4: p_MEK_in 128 bit. Key Wrap/Unwrap. Ref: Moyang's email */
fast_data u8 mek_128b_kwp1_sm4_p1[16] = {0xcb, 0x20, 0x3e, 0x96, 0x89, 0x83, 0xb8, 0x13,
	                                      0x86, 0xbc, 0x38, 0x0d, 0xa9, 0xa7, 0xd6, 0x53};

/* Test vector #6: SM4: c_MEK_in 192 bit. Key Wrap/Unwrap. Ref: Moyang's email */
/*                 c_MEK_out from test without reversing kek and p_MEK_in data */
fast_data u8 mek_192b_kwp1_sm4_c2[24] = {0x61, 0x15, 0x39, 0xBA, 0x51, 0xC6, 0x5C, 0xD4,
	                                      0x12, 0x70, 0x15, 0xFA, 0x23, 0x0F, 0x18, 0x3E,
                                              0x84, 0xC1, 0xAE, 0xA4, 0xEF, 0x43, 0xBE, 0x74};

/* Data to test AES ECB RAW encrypt/decrypt */
/* Test vector #5: AES: KEK 128 bit. Raw encrypt/decrypt. Ref: tiny-AES-c */
fast_data u8 kek_128b_ecb[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
	                              0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f,  0x3c};

/* Test vector #5: AES: p_MEK_in 128 bit. Raw encrypt/decrypt. Ref: tiny-AES-c */
fast_data u8 mek_128b_ecb_p1[16] = {0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
	                                 0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef,  0x97};

/* Test vector #5: AES: c_MEK_in 128 bit. Raw encrypt/decrypt. Ref: tiny-AES-c */
fast_data u8 mek_128b_ecb_c1[16] = {0x22, 0x49, 0xa2, 0x63, 0x8c, 0x6f, 0x1c, 0x75,
	                                 0x5a, 0x84, 0xf9, 0x68, 0x1a, 0x9f, 0x08, 0xc1};

/* Data to test RAW SM4 encrypt/decrypt */
/* Test vector #4: SM4: KEK 128 bit. Raw encrypt/decrypt. Ref: Moyang's code */
fast_data u8 kek_128b_sm4[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	                              0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

/* Test vector #4: SM4: p_MEK_in 128 bit. Raw encrypt/decrypt. Ref: Moyang's code */
fast_data u8 mek_128b_sm4_p1[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	                                 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};

/* Test vector #4: SM4: c_MEK_in 128 bit. Raw encrypt/decrypt. Ref: Moyang's code */
fast_data u8 mek_128b_sm4_c1[16] = {0x68, 0x1e, 0xdf, 0x34, 0xd2, 0x06, 0x96, 0x5e,
	                                 0x86, 0xb3, 0xe9, 0x4f, 0x53, 0x6e, 0x42, 0x46};

/* Data used to test secure/non-secure mode: SM4 key wrap/unwrap - from Moyang (reversed bytes) */
/* Test vector #7: SM4: KEK 256 bit. Key Wrap/Unwrap. Ref: Moyang's email */
fast_data u8 kek_256b_rev_kwp1_sm4[32] = {0x12, 0x52, 0x37, 0x89, 0x65, 0x84, 0xc2, 0xb2,
				               0x8d, 0x99, 0xdf, 0x46, 0x0d, 0x7b, 0xb9, 0x06,
					       0x63, 0x56, 0xf0, 0xb1, 0x09, 0xd6, 0x84, 0x84,
					       0x81, 0x5e, 0x89, 0xc0, 0x24, 0x35, 0x15, 0x12};

/* Test vector #7: SM4: p_MEK_in 128 bit. Key Wrap/Unwrap. Ref: Moyang's email */
fast_data u8 mek_128b_rev_kwp1_sm4_p1[16] = {0x53, 0xd6, 0xa7, 0xa9, 0x0d, 0x38, 0xbc, 0x86,
						  0x13, 0xb8, 0x83, 0x89, 0x96, 0x3e, 0x20, 0xcb};

/* Test vector #7: SM4: c_MEK_out 192 bit. Key Wrap/Unwrap. Ref: Moyang's email */
fast_data u8 mek_192b_rev_kwp1_sm4_c2[24] ={0x1e, 0x37, 0xb3, 0x3e, 0x3a, 0xbd, 0xe2, 0x31,
						 0x8e, 0x45, 0x1b, 0x1f, 0xb5, 0xd0, 0x6f, 0x9b,
						 0xf5, 0x7c, 0xf0, 0x71, 0x10, 0x3a, 0x68, 0x38};

/*!
 * @brief Function to test security subsystem "Key Wrap/Unwrap" for the
 * following test case:
 *
 * 	AES KWP NO SECURE
 *	AES KUWP NO SECURE
 * 	AES RAW ENCRYPT
 * 	AES RAW DECRYPT
 * 	AES KWP SECURE NO CFP
 * 	AES KUWP SECURE NO CFP
 * 	AES KWP SECURE CFP
 * 	AES KUWP SECURE CFP
 *
 * 	SM4 KWP NO SECURE
 *	SM4 KUWP NO SECURE
 * 	SM4 RAW ENCRYPT
 * 	SM4 RAW DECRYPT
 * 	SM4 KWP SECURE NO CFP
 * 	SM4 KUWP SECURE NO CFP
 * 	SM4 KWP SECURE CFP
 * 	SM4 KUWP SECURE CFP
 *
 * @param - [in] - None.
 * @param - [out] - None.
 *
 * @return  None.
 */
ddr_code void test_ss_key_wp_uwp(u8 test_vector)//joe slow->ddr 20201124
{
	 /*
	 *	data_in could be kek, pmek, cmek, ptext, ctext
	 *	data_out could be cmek, pmek, ctext, ptext
	 */
	void* data_in;
	void *data_out;
	void *key_in;
	dtag_t dtag[3];
	kwp_cfg_t kwp_cfg;
	enum spr_reg_idx spr_idx;

	dtag[0] = dtag_get(DTAG_T_SRAM, &data_in);
	dtag[1] = dtag_get(DTAG_T_SRAM, &data_out);
	dtag[2] = dtag_get(DTAG_T_SRAM, &key_in);
	sys_assert(dtag[0].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dtag[1].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dtag[2].b.dtag != _inv_dtag.b.dtag);

	/* Initialize key wrap context */
	memset((void *)&kwp_cfg, 0x00, sizeof(kwp_cfg_t));

	switch (test_vector) {
		/* AES */
	case AES_256B_KWP_NO_SECURE:
		bm_clear_secure_mode();
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_256b_key_p, 32);
		memset(data_out, 0x00, 40);
		break;
	case AES_256B_KUWP_NO_SECURE:
		bm_clear_secure_mode();
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_320b_key_c, 40);
		memset(data_out, 0x00, 32);
		break;
	case AES_256B_RAW_ENCRYPT:
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_128b_ecb, 16);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_128b_ecb_p1, 16);
		memset(data_out, 0x00, 32);
		break;
	case AES_256B_RAW_DECRYPT:
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_128b_ecb, 16);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_128b_ecb_c1, 16);
		memset(data_out, 0x00, 32);
		break;
	case AES_256B_KWP_SECURE_NO_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		spr_idx = SS_SPR_REG1;
		memcpy(data_in, mek_256b_key_p, 32);
		sec_ss_spr_prgm(spr_idx, data_in);
		memset(data_out, 0x00, 40);
		break;
	case AES_256B_KUWP_SECURE_NO_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_320b_key_c, 40);
		memset(data_out, 0x00, 40);
		break;
	case AES_256B_KWP_SECURE_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		spr_idx = SS_SPR_REG1;
		memcpy(data_in, mek_256b_key_p, 32);
		sec_ss_spr_prgm(spr_idx, data_in);
		memset(data_out, 0x00, 40);
		break;
	case AES_256B_KUWP_SECURE_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_320b_key_c, 40);
		memset(data_out, 0x00, 40);
		break;

		/* SM4 128B */
	case SM4_128B_KWP_NO_SECURE:
		bm_clear_secure_mode();
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b_rev_kwp1_sm4, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_128b_rev_kwp1_sm4_p1, 16);
		memset(data_out, 0x00, 40);
		break;
	case SM4_128B_KUWP_NO_SECURE:
		bm_clear_secure_mode();
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b_rev_kwp1_sm4, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_192b_rev_kwp1_sm4_c2, 24);
		memset(data_out, 0x00, 32);
		break;
	case SM4_128B_RAW_ENCRYPT:
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_128b_sm4, 16);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_128b_sm4_p1, 16);
		memset(data_out, 0x00, 32);
		break;
	case SM4_128B_RAW_DECRYPT:
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_128b_sm4, 16);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_128b_sm4_c1, 16);
		memset(data_out, 0x00, 32);
		break;
	case SM4_128B_KWP_SECURE_NO_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b_rev_kwp1_sm4, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		spr_idx = SS_SPR_REG1;
		memcpy(data_in, mek_128b_rev_kwp1_sm4_p1, 16);
		sec_ss_spr_prgm(spr_idx, data_in);
		memset(data_out, 0x00, 40);
		break;
	case SM4_128B_KUWP_SECURE_NO_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b_rev_kwp1_sm4, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_192b_rev_kwp1_sm4_c2, 24);
		memset(data_out, 0x00, 40);
		break;
	case SM4_128B_KWP_SECURE_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b_rev_kwp1_sm4, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		spr_idx = SS_SPR_REG1;
		memcpy(data_in, mek_128b_rev_kwp1_sm4_p1, 16);
		sec_ss_spr_prgm(spr_idx, data_in);
		memset(data_out, 0x00, 40);
		break;
	case SM4_128B_KUWP_SECURE_CFP:
		/* set secure and test modes */
		bm_set_secure_mode(CRYPTO_SEC_SUB_TEST_MODE |
					 CRYPTO_SEC_SUB_SECURE_MODE);
		spr_idx = SS_SPR_REG0;
		memcpy(key_in, kek_256b_rev_kwp1_sm4, 32);
		sec_ss_spr_prgm(spr_idx, key_in);
		memcpy(data_in, mek_192b_rev_kwp1_sm4_c2, 24);
		memset(data_out, 0x00, 40);
		break;
	default:
		nvme_apl_trace(LOG_INFO, 0xcc68, "..... NO VALID TEST VECTOR SELECTED");
		break;
	}

	/* setup key wrap/unwrap configuration parameters */
	switch (test_vector) {
		/* AES 256B */
	case AES_256B_KWP_NO_SECURE:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case AES_256B_KUWP_NO_SECURE:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case AES_256B_RAW_ENCRYPT:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 0; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_RAW_ENCR; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case AES_256B_RAW_DECRYPT:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 0; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_RAW_DECR; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case AES_256B_KWP_SECURE_NO_CFP:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		break;
	case AES_256B_KUWP_SECURE_NO_CFP:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		kwp_cfg.sec_ena_uwp_rslt_spr_idx = SS_SPR_REG2; /* secure enable unwrap result SPR idx */
		kwp_cfg.sec_ena_uwp_rslt_mek_idx = 0; /* secure enable unwrap result MEK idx */
		crypto_hw_cfg_low_power(1);
		break;
	case AES_256B_KWP_SECURE_CFP:
		if (bm_fetch_ss_cfp(1)) {
			kwp_cfg.cfp_en = 1; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0xcd5b, "CFP ..... ENABLED");
		} else {
			kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0x5f2f, "CFP ..... DISABLED");
		}
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		break;
	case AES_256B_KUWP_SECURE_CFP:
		if (bm_fetch_ss_cfp(1)) {
			kwp_cfg.cfp_en = 1; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0x4ad1, "CFP ..... ENABLED");
		} else {
			kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0xeefb, "CFP ..... DISABLED");
		}
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		kwp_cfg.sec_ena_uwp_rslt_spr_idx = SS_SPR_REG2; /* secure enable unwrap result SPR idx */
		kwp_cfg.sec_ena_uwp_rslt_mek_idx = 0; /* secure enable unwrap result MEK idx */
		crypto_hw_cfg_low_power(1);
		break;

		/* SM4 128B */
	case SM4_128B_KWP_NO_SECURE:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case SM4_128B_KUWP_NO_SECURE:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case SM4_128B_RAW_ENCRYPT:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 0; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_RAW_ENCR; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case SM4_128B_RAW_DECRYPT:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 0; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_RAW_DECR; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		break;
	case SM4_128B_KWP_SECURE_NO_CFP:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		break;
	case SM4_128B_KUWP_SECURE_NO_CFP:
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		kwp_cfg.sec_ena_uwp_rslt_spr_idx = SS_SPR_REG2; /* secure enable unwrap result SPR idx */
		kwp_cfg.sec_ena_uwp_rslt_mek_idx = 0; /* secure enable unwrap result MEK idx */
		crypto_hw_cfg_low_power(1);
		break;
	case SM4_128B_KWP_SECURE_CFP:
		if (bm_fetch_ss_cfp(1)) {
			kwp_cfg.cfp_en = 1; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0xcdce, "CFP ..... ENABLED");
		} else {
			kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0x5828, "CFP ..... DISABLED");
		}
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		break;
	case SM4_128B_KUWP_SECURE_CFP:
		if (bm_fetch_ss_cfp(1)) {
			kwp_cfg.cfp_en = 1; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0x3593, "CFP ..... ENABLED");
		} else {
			kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
			nvme_apl_trace(LOG_INFO, 0xa3c7, "CFP ..... DISABLED");
		}
		kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		kwp_cfg.sec_mode_en = 1; /* secure mode enable(1)/disable(0) */
		kwp_cfg.sec_ena_uwp_rslt_spr_idx = SS_SPR_REG2; /* secure enable unwrap result SPR idx */
		kwp_cfg.sec_ena_uwp_rslt_mek_idx = 0; /* secure enable unwrap result MEK idx */
		crypto_hw_cfg_low_power(1);
		break;
	default:
		nvme_apl_trace(LOG_INFO, 0xa7b7, "..... NO VALID TEST VECTOR SELECTED");
		break;
	}

	/* Call key wrap/unwrap API */
	sec_ss_key_wp_uwp(data_in, data_out, &kwp_cfg);

	/* Validate the result */
	switch (test_vector) {
		/* AES 258 */
	case AES_256B_KWP_NO_SECURE:
		if (memcmp(mek_320b_key_c, data_out, 40) == 0) {
			nvme_apl_trace(LOG_INFO, 0x52e2, "AES_256B_KWP_NO_SECURE ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x6fa6, "AES_256B_KWP_NO_SECURE ..... FAIL");
		}
		break;
	case AES_256B_KUWP_NO_SECURE:
		if (memcmp(mek_256b_key_p, data_out, 32) == 0) {
			nvme_apl_trace(LOG_INFO, 0x6008, "AES_256B_KUWP_NO_SECURE ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x5fa8, "AES_256B_KUWP_NO_SECURE ..... FAIL");
		}
		break;
	case AES_256B_RAW_ENCRYPT:
		if (memcmp(mek_128b_ecb_c1, data_out, 16) == 0) {
			nvme_apl_trace(LOG_INFO, 0x9a68, "AES_256B_RAW_ENCRYPT ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x9756, "AES_256B_RAW_ENCRYPT ..... FAIL");
		}
		break;
	case AES_256B_RAW_DECRYPT:
		if (memcmp(mek_128b_ecb_p1, data_out, 16) == 0) {
			nvme_apl_trace(LOG_INFO, 0x9066, "AES_256B_RAW_DECRYPT ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x20da, "AES_256B_RAW_DECRYPT ..... FAIL");
		}
		break;
	case AES_256B_KWP_SECURE_NO_CFP:
		if (memcmp(mek_320b_key_c, data_out, 40) == 0) {
			nvme_apl_trace(LOG_INFO, 0x89a0, "AES_256B_KWP_SECURE_NO_CFP ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0xc9bf, "AES_256B_KWP_SECURE_NO_CFP ..... FAIL");
		}
		break;
	case AES_256B_KUWP_SECURE_NO_CFP:
		if (memcmp(mek_256b_key_p, data_out, 32) == 0) {
			nvme_apl_trace(LOG_INFO, 0x38b7, "AES_256B_KUWP_SECURE_NO_CFP ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x3860, "AES_256B_KUWP_SECURE_NO_CFP ..... FAIL");
		}
		crypto_hw_cfg_low_power(0);
		break;
	case AES_256B_KWP_SECURE_CFP:
		nvme_apl_trace(LOG_INFO, 0xb0fa, "AES_256B_KWP_SECURE_CFP ..... PASS");
		break;
	case AES_256B_KUWP_SECURE_CFP:
		nvme_apl_trace(LOG_INFO, 0xf750, "AES_256B_KUWP_SECURE_CFP ..... PASS");
		crypto_hw_cfg_low_power(0);
		break;

		/* SM4 128 */
	case SM4_128B_KWP_NO_SECURE:
		if (memcmp(mek_192b_rev_kwp1_sm4_c2, data_out, 24) == 0) {
			nvme_apl_trace(LOG_INFO, 0xa619, "SM4_128B_KWP_NO_SECURE ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0xcedf, "SM4_128B_KWP_NO_SECURE ..... FAIL");
		}
		break;
	case SM4_128B_KUWP_NO_SECURE:
		if (memcmp(mek_128b_rev_kwp1_sm4_p1, data_out, 16) == 0) {
			nvme_apl_trace(LOG_INFO, 0xad69, "SM4_128B_KUWP_NO_SECURE ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0xf5e8, "SM4_128B_KUWP_NO_SECURE ..... FAIL");
		}
		break;
	case SM4_128B_RAW_ENCRYPT:
		if (memcmp(mek_128b_sm4_c1, data_out, 16) == 0) {
			nvme_apl_trace(LOG_INFO, 0x9e14, "SM4_128B_RAW_ENCRYPT ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x82b0, "SM4_128B_RAW_ENCRYPT ..... FAIL");
		}
		break;
	case SM4_128B_RAW_DECRYPT:
		if (memcmp(mek_128b_sm4_p1, data_out, 16) == 0) {
			nvme_apl_trace(LOG_INFO, 0x58d5, "SM4_128B_RAW_DECRYPT ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0xe82f, "SM4_128B_RAW_DECRYPT ..... FAIL");
		}
		break;
	case SM4_128B_KWP_SECURE_NO_CFP:
		if (memcmp(mek_192b_rev_kwp1_sm4_c2, data_out, 24) == 0) {
			nvme_apl_trace(LOG_INFO, 0x06e9, "SM4_128B_KWP_SECURE_NO_CFP ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0x5cf9, "SM4_128B_KWP_SECURE_NO_CFP ..... FAIL");
		}
		break;
	case SM4_128B_KUWP_SECURE_NO_CFP:
		if (memcmp(mek_128b_rev_kwp1_sm4_p1, data_out, 16) == 0) {
			nvme_apl_trace(LOG_INFO, 0x1e3e, "SM4_128B_KUWP_SECURE_NO_CFP ..... PASS");
		} else {
			nvme_apl_trace(LOG_INFO, 0xade0, "SM4_128B_KUWP_SECURE_NO_CFP ..... FAIL");
		}
		crypto_hw_cfg_low_power(0);
		break;
	case SM4_128B_KWP_SECURE_CFP:
		nvme_apl_trace(LOG_INFO, 0x6422, "SM4_128B_KWP_SECURE_CFP ..... PASS");
		break;
	case SM4_128B_KUWP_SECURE_CFP:
		nvme_apl_trace(LOG_INFO, 0x0328, "SM4_128B_KUWP_SECURE_CFP ..... PASS");
		crypto_hw_cfg_low_power(0);
		break;
	default:
		nvme_apl_trace(LOG_INFO, 0xca2e, "..... NO VALID TEST VECTOR SELECTED");
		break;
	}

	/* Release dtags */
	dtag_put(DTAG_T_SRAM, dtag[0]);
	dtag_put(DTAG_T_SRAM, dtag[1]);
	dtag_put(DTAG_T_SRAM, dtag[2]);
}
#endif
u32 randstream_check(void)
{
	u8 ones_array[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
	u8 halfbyte;
	u32 sample_bits = 20000;   /*  Page 44 of SPEC SECURITY REQUIREMENTS  FOR  CRYPTOGRAPHIC MODULES*/
	u32 poker_fi[16];    /* 0~f */
	u32 i, j;
	u32 monobit_sum = 0;
	u32 poker_sum = 0,quotient,remainder;
	u32 status = 0;
	u32 valid_dword = (sample_bits >> 5);
	u32 max_loop = (sample_bits >> 11);

	void* sec_buf_c;
	dtag_t dtag;
	dtag =  dtag_get(DTAG_T_SRAM, &sec_buf_c);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);

	u32 *prandom = (u32 *)sec_buf_c;
	u32 runs0[7], runs1[7];
	u32 long_runs = 0;
	u32 current, previous, firstbit_flag = 0;
	u32 bits[2] = { 0,0 };
	if (sample_bits > 32768) {
		nvme_apl_trace(LOG_ERR, 0xbc4e, "bits number overflow\n");
		return status;
	}
	if (sample_bits % 2048)  /*get 2048 bit each time */
		max_loop += 1;

	memset(prandom, 0, sizeof(sec_buf_c));/* clear all the buffer */
	/*clear  all the member */
	for (i = 0; i < 16; i++)
		poker_fi[i] = 0;	/* clear for poker test  */
	for (i = 0; i < 7; i++) {
		runs0[i] = 0;		/* clear for runs test 	*/
		runs1[i] = 0;		/* clear for runs test 	*/
	}
	for (i = 0; i < max_loop; ++i) {
		trng_gen_random(prandom + i * 64, 64);
	}

	for (i = 0; i < valid_dword; i++) {
		for (j = 0; j < 8; j++) {
			halfbyte = (prandom[i] >> (j << 2)) & 0xf;
			poker_fi[halfbyte]++;  /*store for poker calculator */
			monobit_sum += ones_array[halfbyte];
		}
	}
	/* monobit test */
	status = (monobit_sum > MONIBIT_MIN) &(monobit_sum < MONIBIT_MAX);
	/* poker test */
	for (i = 0; i < 16; i++)
		poker_sum += poker_fi[i] * poker_fi[i];
	quotient = poker_sum / 5000;
	remainder = poker_sum % 5000;
	poker_sum = 1600 * quotient + 1600 * remainder / 5000 - 5000 * 100;
	status &= (poker_sum > POKER_MIN) & (poker_sum < POKER_MAX);
	/* the runs test */
	for (i = 0; i < valid_dword; i++) {
		for (j = 0; j < 32; j++) {
			current = (prandom[i] >> j) & 0x1;
			if (firstbit_flag == 0) {
				firstbit_flag = 1;   /*get the first bit already*/
				previous = current;
			}else {
				if (current^previous) { /* the different bit */
					long_runs = max(long_runs, bits[previous]);
					if (bits[previous] > 6)
						bits[previous] = 6; /* length of run */
					if (previous == 0)
						runs0[bits[previous]]++;
					else
						runs1[bits[previous]]++;
					previous = current;     /*update new flag*/
					bits[0] = 0;
					bits[1] = 0;
				}
			}
			bits[current]++;
		}
	}
	long_runs = max(long_runs, bits[current]);   /* the last bit */
	if (bits[current] > 6)
		bits[current] = 6;
	if (current == 0)
		runs0[bits[current]]++;
	else
		runs1[bits[current]]++;
	status &= (runs0[1] > RUNS_SEQ1_MIN) & (runs0[1] < RUNS_SEQ1_MAX);
	status &= (runs0[2] > RUNS_SEQ2_MIN) & (runs0[2] < RUNS_SEQ2_MAX);
	status &= (runs0[3] > RUNS_SEQ3_MIN) & (runs0[3] < RUNS_SEQ3_MAX);
	status &= (runs0[4] > RUNS_SEQ4_MIN) & (runs0[4] < RUNS_SEQ4_MAX);
	status &= (runs0[5] > RUNS_SEQ5_MIN) & (runs0[5] < RUNS_SEQ5_MAX);
	status &= (runs0[6] > RUNS_SEQ6_MIN) & (runs0[6] < RUNS_SEQ6_MAX);
	status &= (runs1[1] > RUNS_SEQ1_MIN) & (runs1[1] < RUNS_SEQ1_MAX);
	status &= (runs1[2] > RUNS_SEQ2_MIN) & (runs1[2] < RUNS_SEQ2_MAX);
	status &= (runs1[3] > RUNS_SEQ3_MIN) & (runs1[3] < RUNS_SEQ3_MAX);
	status &= (runs1[4] > RUNS_SEQ4_MIN) & (runs1[4] < RUNS_SEQ4_MAX);
	status &= (runs1[5] > RUNS_SEQ5_MIN) & (runs1[5] < RUNS_SEQ5_MAX);
	status &= (runs1[6] > RUNS_SEQ6_MIN) & (runs1[6] < RUNS_SEQ6_MAX);
	/* The long runs test */
	status &= (long_runs < LONG_RUNS);
	dtag_put(DTAG_T_SRAM, dtag);
	return status;
}

void security_selftest(void)
{
	sec_status.all = 0;
#if !defined(FPGA)
	/* Trng selftest */
	{
		u32 loops = 0;
		for (loops = 0; loops < 3;++loops) {
			sec_status.b.randst_check = randstream_check();
			if (sec_status.b.randst_check == 1) {
				loops = 3;
			}
		}
	}
#endif //FPGA
	/* SM2 get Public/Private keys selftest */
	{
		u8 pri[32] = {0xb8, 0xc5, 0xf7, 0x4d, 0xef, 0x81, 0xfb, 0x42,
			0x1a, 0xb5, 0x60, 0x28, 0x69, 0x93, 0x93, 0x88,
			0x95, 0x9f, 0xd3, 0xc6, 0x8a, 0xe3, 0x36, 0x3f,
			0xb1, 0x44, 0x21, 0x7b, 0x8f, 0x20, 0x45, 0x39};
		u8 pub_e[64] = {0x20, 0x50, 0xf3, 0x56, 0xf3, 0x8f, 0xb0, 0x6b,
			0x07, 0xfc, 0x33, 0x18, 0xad, 0x9f, 0x17, 0x72,
			0xc6, 0xc5, 0x4b, 0x1e, 0x16, 0x7d, 0xdd, 0x50,
			0xa1, 0x21, 0x54, 0x1e, 0x31, 0xdf, 0xf9, 0x09,
			0x13, 0xad, 0xa9, 0x2d, 0x07, 0xf6, 0x32, 0x66,
			0x4a, 0x08, 0x5e, 0xf3, 0xfb, 0x05, 0xed, 0x0a,
			0x60, 0xaa, 0xc1, 0x8c, 0x71, 0xea, 0xc6, 0x2d,
			0xa5, 0x75, 0x67, 0xe2, 0x0c, 0x49, 0xea, 0xcc};
		u8 pub_ans[64];
		memset(pub_ans, 0, sizeof(pub_ans));
		/* calculate pub key */
		sec_sm2_get_pub_key(pri, pub_ans);
		sec_status.b.sm2_check = (memcmp(pub_ans, pub_e, 64) == 0);
	}
	/* SM2 sign pretreatment test */
	{
		u8 user_id[16] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
			0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
		u32 pri_key[32] = {0x4DF7C5B8, 0x42FB81EF, 0x2860B51A, 0x88939369,
			0xC6D39F95, 0x3F36E38A, 0x7B2144B1, 0x3945208F};
		u8 msg[14] = {0x6D, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x20,
			0x64, 0x69, 0x67, 0x65, 0x73, 0x74};
		u8 e[32] = {0xF0, 0xB4, 0x3E, 0x94, 0xBA, 0x45, 0xAC, 0xCA,
			0xAC, 0xE6, 0x92, 0xED, 0x53, 0x43, 0x82, 0xEB,
			0x17, 0xE6, 0xAB, 0x5A, 0x19, 0xCE, 0x7B, 0x31,
			0xF4, 0x48, 0x6F, 0xDF, 0xC0, 0xD2, 0x86, 0x40};
		u8 e_ans[32];
		memset(e_ans, 0, sizeof(e_ans));
		sec_sm2_sign_pretreatment(msg, sizeof(msg), user_id, sizeof(user_id), (u8 *)pri_key, (u8 *)e_ans);
		sec_status.b.sm2_check &= (memcmp(e, e_ans, 32) == 0);
	}
	/* SM2 sign and verify selftest */
	{
		u32 official_pri[8] = {0x4DF7C5B8, 0x42FB81EF, 0x2860B51A, 0x88939369,
			 0xC6D39F95, 0x3F36E38A, 0x7B2144B1, 0x3945208F};
		u32 official_e[8] = {0xC0D28640, 0xF4486FDF, 0x19CE7B31, 0x17E6AB5A,
			 0x534382EB, 0xACE692ED, 0xBA45ACCA, 0xF0B43E94};
		u32 official_pub[16] = {0x56F35020, 0x6BB08FF3, 0x1833FC07, 0x72179FAD,
			0x1E4BC5C6, 0x50DD7D16, 0x1E5421A1, 0x09F9DF31,
			0x2DA9AD13, 0x6632F607, 0xF35E084A, 0x0AED05FB,
			0x8CC1AA60, 0x2DC6EA71, 0xE26775A5, 0xCCEA490C};
		u32 sign[16] = {0xEEE720B3, 0x43AC7EAC, 0x27D5B741, 0x5944DA38,
			0xE1BB81A1, 0x0EEAC513, 0x48D2C463, 0xF5A03B06,
			0x85BBC1AA, 0x840B69C4, 0x1F7F42D4, 0xBB9038FD,
			0x0D421CA1, 0x763182BC, 0xDF212FD8, 0xB1B6AA29};
		u32 random_sign[32] = {0xEAC1BC21, 0x6D54B80D, 0x3CDBE4CE, 0xEF3CC1FA, 0xD9C02DCC,
			0x16680F3A, 0xD506861A, 0x59276E27};
		u32 official_pub_ans[16];
		u32 sign_ans[16];
		u32 official_e_backup[8];
		memcpy(official_e_backup, official_e, sizeof(official_e_backup));
		memset(official_pub_ans, 0, sizeof(official_pub_ans));
		memset(sign_ans, 0, sizeof(sign_ans));
		sec_sm2_get_pub_key((u8 *)official_pri, (u8 *)official_pub_ans);
		sec_status.b.sm2_check  &= (memcmp(official_pub, official_pub_ans, 64) == 0);
		sec_status.b.sm2_check  &=  sec_sm2_verify((u8 *)official_pub, (u8 *)official_e_backup, (u8 *)sign);
		sec_sm2_sign((u8 *)official_e, 32, (u8 *)random_sign, (u8 *)official_pri, (u8 *)sign_ans);
		sec_status.b.sm2_check &= (memcmp(sign, sign_ans, 64) == 0);
		sec_status.b.sm2_check &= sec_sm2_verify((u8 *)official_pub,(u8 *)official_e_backup, (u8 *)sign_ans);
	}
	/* SM2 encrypte/decrypte test */
	{
		u8 sm2_pmsg[19] = {0x65, 0x6E, 0x63, 0x72, 0x79, 0x70,  0x74, 0x69,
			0x6F, 0x6E, 0x20, 0x73, 0x74, 0x61, 0x6E, 0x64,
			0x61, 0x72, 0x64};
		u32 sm2_msg_len = sizeof(sm2_pmsg);
		u8 sm2_pri_key[32] = {0xb8, 0xc5, 0xf7, 0x4d, 0xef, 0x81, 0xfb, 0x42,
			0x1a, 0xb5, 0x60, 0x28, 0x69, 0x93, 0x93, 0x88,
			0x95, 0x9f, 0xd3, 0xc6, 0x8a, 0xe3, 0x36, 0x3f,
			0xb1, 0x44, 0x21, 0x7b, 0x8f, 0x20, 0x45, 0x39};
		u8 sm2_random[32] = {0x21, 0xbc, 0xc1, 0xea, 0x0d, 0xb8, 0x54, 0x6d,
			0xce, 0xe4, 0xdb, 0x3c, 0xfa, 0xc1, 0x3c, 0xef,
			0xcc, 0x2d, 0xc0, 0xd9, 0x3a, 0x0f, 0x68, 0x16,
			0x1a, 0x86, 0x06, 0xd5, 0x27, 0x6e, 0x27, 0x59};
		u8 sm2_cmsg[115] = {
			0x04, 0xEB, 0xFC, 0x71, 0x8E, 0x8D, 0x17, 0x98,
			0x62, 0x04, 0x32, 0x26, 0x8E, 0x77, 0xFE, 0xB6,
			0x41, 0x5E, 0x2E, 0xDE, 0x0E, 0x07, 0x3C, 0x0F,
			0x4F, 0x64, 0x0E, 0xCD, 0x2E, 0x14, 0x9A, 0x73,
			0xE8, 0x58, 0xF9, 0xD8, 0x1E, 0x54, 0x30, 0xA5,
			0x7B, 0x36, 0xDA, 0xAB, 0x8F, 0x95, 0x0A, 0x3C,
			0x64, 0xE6, 0xEE, 0x6A, 0x63, 0x09, 0x4D, 0x99,
			0x28, 0x3A, 0xFF, 0x76, 0x7E, 0x12, 0x4D, 0xF0,
			0x59, 0x98, 0x3C, 0x18, 0xF8, 0x09, 0xE2, 0x62,
			0x92, 0x3C, 0x53, 0xAE, 0xC2, 0x95, 0xD3, 0x03,
			0x83, 0xB5, 0x4E, 0x39, 0xD6, 0x09, 0xD1, 0x60,
			0xAF, 0xCB, 0x19, 0x08, 0xD0, 0xBD, 0x87, 0x66,
			0x21, 0x88, 0x6C, 0xA9, 0x89, 0xCA, 0x9C, 0x7D,
			0x58, 0x08, 0x73, 0x07, 0xCA, 0x93, 0x09, 0x2D,
			0x65, 0x1E, 0xFA};
		u8 sm2_pmsg_ans[19];
		u8 sm2_cmsg_ans[64 + 19 + 32];
		u8 sm2_pub[64];
		memset(sm2_pmsg_ans, 0, sizeof(sm2_pmsg_ans));
		sec_status.b.sm2_check &= sec_sm2_decrypt(sm2_cmsg,sm2_msg_len + 64 + 32, sm2_pri_key, sm2_pmsg_ans);
		sec_status.b.sm2_check &=  (memcmp(sm2_pmsg_ans, sm2_pmsg, sm2_msg_len) == 0);

		memset(sm2_cmsg_ans, 0, sizeof(sm2_cmsg_ans));
		sec_sm2_get_pub_key(sm2_pri_key, sm2_pub);
		sec_sm2_encrypt(sm2_pmsg, sm2_msg_len,sm2_pub, sm2_random, sm2_cmsg_ans);
		sec_status.b.sm2_check &=  (memcmp(sm2_cmsg_ans,sm2_cmsg,sizeof(sm2_cmsg_ans)) == 0);

		memset(sm2_pmsg_ans, 0, sizeof(sm2_pmsg_ans));
		sec_status.b.sm2_check &=  sec_sm2_decrypt(sm2_cmsg, sm2_msg_len + 32 + 64,sm2_pri_key, sm2_pmsg_ans);
		sec_status.b.sm2_check &=  (memcmp(sm2_pmsg_ans, sm2_pmsg, sm2_msg_len) == 0);
	};
	/* SM3 and SHA3-256 selftest */
	{
		u8 msg_in[] = "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd";
		u8 sm3_msg_out[32] = {0xde, 0xbe, 0x9f, 0xf9, 0x22, 0x75, 0xb8, 0xa1,
			0x38, 0x60, 0x48, 0x89, 0xc1, 0x8e, 0x5a, 0x4d,
			0x6f, 0xdb, 0x70, 0xe5, 0x38, 0x7e, 0x57, 0x65,
			0x29, 0x3d, 0xcb, 0xa3, 0x9c, 0x0c, 0x57, 0x32,};
		u8 sha3_256_msg_out[32] = {0x32, 0x99, 0x3a, 0x3b, 0xa3, 0x7e, 0x59, 0x3c,
			0x63, 0xf8, 0xc3, 0xe0, 0x68, 0xdf, 0x5b, 0x3e,
			0xe8, 0xa2, 0xc5, 0x70, 0x3f, 0x54, 0x0a, 0x0b,
			0x5c, 0x1d, 0xbf, 0x72, 0xd4, 0xf2, 0x5f, 0x72};
		u8 msg_out_s[32];
		u32 len = sizeof(msg_in) - 1;
		memset(msg_out_s, 0, sizeof(msg_out_s));
		sec_gen_sm3_hash(msg_in, len, msg_out_s);
		sec_status.b.sm3_check = (memcmp(msg_out_s, sm3_msg_out, 32) == 0);
		memset(msg_out_s, 0, sizeof(msg_out_s));
		sec_gen_sha3_256_hash(msg_in, len, msg_out_s );
		sec_status.b.sha256_check =  (memcmp(msg_out_s, sha3_256_msg_out, 32) == 0);
	}
	/* SM4 encryption/decryption test */
	{
		u8 official_mek[16] = {0x15, 0x17, 0xBB, 0x06, 0xF2, 0x72, 0x19, 0xDA,
			0xE4, 0x90, 0x22, 0xDD, 0xC4, 0x7A, 0x06, 0x8D};
		u8 official_pmsg[16] = {0xE4, 0xC9, 0x49, 0x6A, 0x95, 0x1A, 0x6B, 0x09,
			0xED, 0xBD, 0xC8, 0x64, 0xC7, 0xAD, 0xBD, 0x74};
		u8 official_cmsg[16] = {0x07, 0x9B, 0x3B, 0xA8, 0x0D, 0x9A, 0x36, 0x7A,
			0x7C, 0xD7, 0x47, 0xDE, 0x3A, 0x28, 0xE5, 0x74};
		u8 mek[16];
		u8 official_pmsg_ans[16];
		u8 official_cmsg_ans[16];
		memcpy(mek, official_mek, 16);
		/* decryption */
		sec_sm4_ende(false, mek, 16, official_cmsg, 16, official_pmsg_ans);
		sec_status.b.sm4_check = (memcmp(official_pmsg_ans, official_pmsg, 16) == 0);
		/* encryption */
		sec_sm4_ende(true, official_mek, 16, official_pmsg, 16, official_cmsg_ans);
		sec_status.b.sm4_check &= (memcmp(official_cmsg_ans, official_cmsg, 16) == 0);
		/* decryption */
		sec_sm4_ende(false, mek, 16, official_cmsg, 16, official_pmsg_ans);
		sec_status.b.sm4_check &= (memcmp(official_pmsg_ans, official_pmsg, 16) == 0);
	}

#if SECURITY_FULL_TEST
	u32 i;
	for (i = AES_256B_KWP_NO_SECURE; i < AES_SM4_MAX_TEST_VECTOR; i++)
		test_ss_key_wp_uwp(i);
#endif

	nvme_apl_trace(LOG_INFO, 0xf72c, "Security Result(SHA3-256 SM4/3/2 TRNG) : 0x%x\n", sec_status.all);
}

void security_test_init(void)
{
	sec_status.all = 0;
}

static ps_code int security_test_main(int argc, char *argv[])
{
	security_selftest();
	// extern int sec_rsa_calc_test(void);
	// sec_rsa_calc_test();
	return 0;
}
static DEFINE_UART_CMD(sec_test, "sec_test", "sec_test", "sec_test", 0, 0, security_test_main);
#endif //USE_CRYPTO_HW
