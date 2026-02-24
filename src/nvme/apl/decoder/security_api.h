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
#pragma once

#ifdef  __cplusplus
extern "C" {
#endif
#include "types.h"
#include "bf_mgr.h"

typedef struct _dpe_sec_calc_ctx_t
{
	dtag_t dtag[3];     // extra buffer for sha3/sm3 B0 verify
	u32 dtag_cnt;
	u32 size;
	u32 dpe_done_flag;
	union {
		struct {
			u32 cnt;
			u8 *hash;
			void* sram;
			void* result;
		} sha3sm3;
		struct {
			u8 *rand_num_k;
			u8 *pub_key_px_py;
			u8 *hash_e;
			u8 *sign_r_s;
			u8 *rand_num_lp;
			u8 *x2_y2;
			u8 *pri_key_d;
			void* sram;
			void* result;
			enum sm2_func_sel func_sel;
		} sm2;
		struct {
			void *data_in;
			void *data_out;
		} kwp;
		struct {
			void *param;
			void *result;
			u32 result_byte_len;
		} rsa;
	} data;
} dpe_sec_calc_ctx_t;

typedef struct {
	u32 n[128];          ///< 521B, public parameter: modulus
	u32 DOrE[128];       ///< 512B, private(d, n) or public(e, n) key
	u32 r[128];          ///< 512B, r = ((2^4096) mod n)
	u32 rr[128];         ///< 512B, rr = (r * r) mod n
	u32 msg[128];        ///< 512B, the cipher text or plain text, depend on DOrE
	u32 result[128];     ///< 512B, result buffer
} dpe_sec_rsa_param_t;

#define TRNG_CFG_VERSION	0
#define TRNG_VERSION_0 	0
#define TRNG_VERSION_1		1

#if (TRNG_CFG_VERSION == TRNG_VERSION_0)
#define MONIBIT_MIN			9654
#define MONIBIT_MAX 		10346
#define POKER_MIN			103
#define POKER_MAX			5740
#define RUNS_SEQ1_MIN		2267
#define RUNS_SEQ1_MAX  	2733
#define RUNS_SEQ2_MIN   	1079
#define RUNS_SEQ2_MAX  	1421
#define RUNS_SEQ3_MIN		504
#define RUNS_SEQ3_MAX 		748
#define RUNS_SEQ4_MIN   	223
#define RUNS_SEQ4_MAX  	402
#define RUNS_SEQ5_MIN   	90
#define RUNS_SEQ5_MAX  	223
#define RUNS_SEQ6_MIN   	90
#define RUNS_SEQ6_MAX  	223
#define LONG_RUNS       		34
#else
#define MONIBIT_MIN			9725
#define MONIBIT_MAX 	 	10275
#define POKER_MIN			216
#define POKER_MAX			4617
#define RUNS_SEQ1_MIN    	2343
#define RUNS_SEQ1_MAX   	2657
#define RUNS_SEQ2_MIN	 	1135
#define RUNS_SEQ2_MAX   	1365
#define RUNS_SEQ3_MIN	 	542
#define RUNS_SEQ3_MAX   	708
#define RUNS_SEQ4_MIN    	251
#define RUNS_SEQ4_MAX   	373
#define RUNS_SEQ5_MIN    	111
#define RUNS_SEQ5_MAX   	201
#define RUNS_SEQ6_MIN    	111
#define RUNS_SEQ6_MAX		201
#define LONG_RUNS       		26
#endif
extern void sec_gen_rand_number(u8 *buf, u8 rand_len);
extern void sec_sm2_get_pub_key(unsigned char *pri_key, unsigned char *pub_key);
extern int sec_sm2_encrypt(u8 *pmsg, u32 pmsg_len, u8 *pub_key,u8 *random, u8 *cmsg);
extern int sec_sm2_decrypt(u8 *cmsg, u32 cmsg_len, u8 *pri_key, u8 *pmsg);
extern int sec_sm2_sign(unsigned char *e, u32 elen, unsigned char *rand_num, unsigned char *pri_key, unsigned char *sign);
extern int sec_sm2_verify(unsigned char *pub_key, unsigned char *hash, unsigned char *sign);
extern void sec_gen_sm3_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash);
extern void sec_gen_sha3_256_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash);
extern int sec_sm4_ende(bool encr, u8 *mek, u32 mek_len, u8 *msg,u32 msg_len, void *result);
extern int sec_sm2_sign_pretreatment(u8 *sign_msg, u32 msg_len, u8 *user_id, u32 id_len,u8 *pri_key, u8 *e);
extern void sec_cfg_kwp(kwp_cfg_t *kwp_cfg, u8 choice, bool sm4);
extern u32  sec_gen_kek_hash_msg(u8 *random, bool rerandom,u8 *seed, u8 idx, u8 *chip_id, u8 *result, char *str);
extern void sec_ss_spr_prgm(enum spr_reg_idx spr_idx, void *mem);
extern void sec_ss_key_wp_uwp(void *src, void *result, kwp_cfg_t *kwp_cfg);
extern void bm_isr_slow(void);
extern void bm_ss_rn_gen(enum spr_reg_idx spr_idx, dpe_cb_t callback, void *ctx);
extern void bm_ss_spr_prgm(void* mem, enum spr_reg_idx spr_idx, dpe_cb_t callback, void *ctx);
extern void bm_ss_key_wp_uwp(void* mem, void *result,dpe_cb_t callback, void *ctx, kwp_cfg_t *kwp_cfg);

#ifdef  __cplusplus
}
#endif
