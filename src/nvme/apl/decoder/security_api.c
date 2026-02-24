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

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "assert.h"
#include "itrng_reg.h"
#include "sect.h"
#include "misc.h"
#include "string.h"
#include "task.h"
#include "endian.h"
#include "security_api.h"
#include "stdlib.h"
#include "crypto.h"
#include "bigdigits.h"
#include "dpe_export.h"
#include "trng.h"

/*! \cond PRIVATE */
#define __FILEID__ sec_apl
#include "trace.h"
/*! \endcond */

#define SEC_RSA_TEST 0
#if SEC_RSA_TEST
dpe_sec_calc_ctx_t dpe_sec_ctx;

/*!
 * @brief calculate n0inv(nprime0)
 *
 * @param n[in]: digits
 * @param ndigits: the length in digits
 *
 * @return nprime0 = (-n[0] ^ (-1)) mod (2 ^ 32)
 */
unsigned int sec_rsa_calc_nprime0(unsigned int *n, unsigned int ndigits)
{
	u32 w = 32;
	u32 *w2; ///< w2 = 2 ^ 32
	u32 *inv; ///< inv = n^(-1) mod w2
	u32 *zeros;
	u32 *nprime0;
	u32 *work_buffers[3];
	u32 *work_buffer;
	u32 i;
	dtag_t dtags[3];
	sys_assert(ndigits <= MAX_DIGITS_DW_CNT);
	sys_assert(3 == dtag_get_bulk(DTAG_T_SRAM, 3, dtags));
	for (i = 0; i < 3; i++)
		work_buffers[i] = (u32 *)sdtag2mem(dtags[i].b.dtag);

	w2 = work_buffers[0];
	inv = w2 + ndigits;
	zeros  = inv + ndigits;
	nprime0 = zeros + ndigits;
	work_buffer = nprime0 + ndigits;
	/* clear w2 buffer */
	mpSetZero(w2, ndigits);
	/* calcuate 2 ^ 32 */
	mpSetBit(w2, ndigits, w, 1);
	/* calcuate inv */
	mpModInv(inv, n, w2, ndigits, &work_buffers[1]);
	/* clear zeros buffer */
	mpSetZero(zeros, ndigits);
	/* calcuate nprime0(n0inv) = -1 / n mod (2 ^ 32) */
	mpModSub(nprime0, zeros, inv, w2, ndigits, work_buffer);
	dtag_put_bulk(DTAG_T_SRAM, 3, dtags);
	return nprime0[0];
}

/*!
 * @brief calculate r, rr value from n
 *
 * @param n[in]: digits, please make the buffer length meet the requirement of each mode
 *               for example, if mode is RSA4096, the buffer lengthe should be 512B. if
 *               the length of n is less than the length of buffer, please cleare the upper bytes
 *               with all 0s
 * @param r[out]: r = 2 ^ ELEN mod n, please make sure the buffer length meet the requirement of each mode
 * @param rr[out]: rr =  r * r mod n, please make sure the buffer length meet the requirement of each mode
 * @param mode:
 *             native RSA4096 working mode, NLEN = 4096
 *             native RSA3072 working mode, NLEN = 3072
 *             native RSA2048 working mode, NLEN = 2048
 *             native RSA1024 working mode, NLEN = 1024
 *
 * @return None
 */
void sec_rsa_calc_r_rr(unsigned int *n, unsigned int *r, unsigned int *rr, enum rsa_mode_ctrl mode)
{
	/*
	* native RSA4096 working mode, NLEN = 4096
	* native RSA3072 working mode, NLEN = 3072
	* native RSA2048 working mode, NLEN = 2048
	* native RSA1024 working mode, NLEN = 1024
	*/
	u32 ndigits = 0;
	u32 nlen = 0;
	switch (mode) {
	case SS_RSA_4096_MODE:
		ndigits = 4096 / 32;
		nlen = 4096;
		break;
	case SS_RSA_3072_MODE:
		ndigits = 3072 / 32;
		nlen = 3072;
		break;
	case SS_RSA_2048_MODE:
		ndigits = 2048 / 32;
		nlen = 2048;
		break;
	case SS_RSA_1024_MODE:
		ndigits = 1024 / 32;
		nlen = 1024;
		break;
	default:
		sys_assert(0);
	}
	u32 nn = ndigits * 2;
	u32 *work_buffer;
	dtag_t dtag1 = dtag_get(DTAG_T_SRAM, NULL);
	sys_assert(dtag1.b.dtag != _inv_dtag.b.dtag);
	dtag_t dtag2 = dtag_get(DTAG_T_SRAM, (void **)&work_buffer);
	sys_assert(dtag2.b.dtag != _inv_dtag.b.dtag);
	u32 *base = sdtag2mem(dtag1.b.dtag);
	u32 *exp = base + nn;
	/* calcuate r = 2 ^ NLEN mode n */
	mpSetDigit(base, 2, nn);
	mpSetDigit(exp, nlen, nn);
	memset(work_buffer, 0 , DTAG_SZE);
	mpModExp(r, base, exp, n, ndigits, work_buffer);
	/* calcuate r = r * r mode n */
	mpSetDigit(base, 2, nn);
	mpSetDigit(exp, 2 * nlen, nn);
	memset(work_buffer, 0 , DTAG_SZE);
	mpModExp(rr, base, exp, n, ndigits, work_buffer);
	dtag_put(DTAG_T_SRAM, dtag1);
	dtag_put(DTAG_T_SRAM, dtag2);
}

static int dpe_rsa_calc_cmpl(void* ctx, dpe_rst_t* rst)
{
	dpe_sec_calc_ctx_t *rsa_calc_ctx = (dpe_sec_calc_ctx_t *)ctx;
	dpe_sec_rsa_param_t *rsa_param = rsa_calc_ctx->data.rsa.param;
	memcpy(rsa_calc_ctx->data.rsa.result, rsa_param->result, rsa_calc_ctx->data.rsa.result_byte_len);
	rsa_calc_ctx->dpe_done_flag = 1;
	return 0;
}

/*!
 * @brief RSA calculation
 *  this function will submit RSA calc request to HW DPE function
 *
 * @param n[in]: digits, n = p * q
 * @param ndigits: the length of n in digits
 * @param DOrE[in]: the d value that from private key(d = e ^ (-1) mod (p - 1)(q - 1), n)
 *                  the e valuse that from publick key(e, n)
 * @param DOrEdigits: the lengthe of D or E in digits
 * @param msg: the cipher text or plain text
 * @param msgdigits: the message length in digits, maximum 512B
 * @param result: the plain text or cipher text
 * @param mode:
 *              0 - native RSA4096 working mode
 *              1 - native RSA3072 working mode
 *              2 - native RSA2048 working mode
 *              3 - native RSA1024 working mode
 *              as with each mode name, the maximum length of data it can calcuate are different.
 *              please make sure the input data bit length is less than the maximum bit length of the selected mode
 *              for example, RSA1024 mode means it will only calculate 1024 bit input data even the input data bit length is
 *              greater than 1024 bit.
 *              if the input parameter n's bit length is 1024, you can select mode: 0, 1, 2, 3, but 3 mode is the fastest
 *              if the input parameter n's bit length is 4096, you can only select mode 0.
 *
 * @return 0 - pass
 *
 * Notes:
 *	1. For an output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 */
int sec_rsa_calc(unsigned int *n, unsigned int ndigits, unsigned int *DOrE,
	unsigned int DOrEdigits, unsigned int *msg, unsigned int msgdigits,
	unsigned int *result, enum rsa_mode_ctrl mode)
{
	dpe_sec_rsa_param_t *dpe_sec_rsa;
	u32 nbytes;
	dtag_t dtag;
	dtag = dtag_get(DTAG_T_SRAM, (void **)&dpe_sec_rsa);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	nbytes = ndigits * sizeof(u32);
	/* must clear the buffer to zeros */
	memset((void *)dpe_sec_rsa, 0, DTAG_SZE);
	memcpy(dpe_sec_rsa->n, n, nbytes);
	memcpy(dpe_sec_rsa->DOrE, DOrE, DOrEdigits * sizeof(u32));
	memcpy(dpe_sec_rsa->msg, msg, msgdigits * sizeof(u32));
	sec_rsa_calc_r_rr(dpe_sec_rsa->n, dpe_sec_rsa->r, dpe_sec_rsa->rr, mode);
	dpe_sec_ctx.data.rsa.param = dpe_sec_rsa;
	dpe_sec_ctx.data.rsa.result = result;
	dpe_sec_ctx.data.rsa.result_byte_len = nbytes;
	bm_rsa_calc((void *)dpe_sec_rsa, dpe_sec_rsa->result,
		sec_rsa_calc_nprime0(n, ndigits), dpe_rsa_calc_cmpl, &dpe_sec_ctx, mode);
	dpe_sec_ctx.dpe_done_flag = 0;
	do {
		dpe_isr();
	} while (dpe_sec_ctx.dpe_done_flag == 0);
	dpe_sec_ctx.dpe_done_flag = 0;
	return 0;
}
#endif /* SEC_RSA_TEST */

#if defined(USE_CRYPTO_HW)
dpe_sec_calc_ctx_t dpe_sec_ctx;

static inline u32 trng_readl(u32 reg)
{
	return readl((void *)(TRNG_BASE + reg));
}

static inline void trng_writel(u32 data, u32 reg)
{
	writel(data, (void *)(TRNG_BASE + reg));
}
static bool is_all_zero(u8 *buffer, u32 len)
{
	u32 i;
	for (i = 0; i < len; i++) {
		if (buffer[i] != 0)
			break;
	}
	return (i == len);
}

static void sec_wait_bm_done(void)
{
	dpe_sec_ctx.dpe_done_flag = 0;
	do {
		dpe_isr();
	} while (dpe_sec_ctx.dpe_done_flag == 0);
	dpe_sec_ctx.dpe_done_flag = 0;
}

/*!
 * @brief Function to generate random number
 *
 *        This function will generate either 16 or 32 bytes random number
 *        and return it in the buffer provided. The size of the buffer
 *        must be suffcient enough to hold the random number. If the
 *        rand_len is 16, 16 bytes random number will be generated.
 *        Otherwise, this will generate 32 bytes random number.
 *
 * @param rnd_buf   pointer to buffer to hold 16/32 bytes random number.
 * @param rand_len  length of random number. if this is 16, 16 bytes else
 *		    32 bytes.
 *
 * @return  none
 */
void sec_gen_rand_number(u8 *buf, u8 rand_len)
{
	trng_gen_random((u32 *)buf, rand_len / 4);
}

/*!
 * @brief DPE SM2 calculation completion function.
 *
 * @param ctx - [in] completion context.
 * @param rst - [in] completion result.
 *
 * @return  0 - PASS
 *	    1 - FAIL
 */
static int dpe_sm2_cal_cmpl(void* ctx, dpe_rst_t* rst)
{
	dpe_sec_calc_ctx_t *sm2_ctx = (dpe_sec_calc_ctx_t *)ctx;
	u8 *result = (u8 *)sm2_ctx->data.sm2.result;
	u8 *sram = (u8 *)sm2_ctx->data.sm2.sram;
	enum sm2_func_sel func_sel = sm2_ctx->data.sm2.func_sel;
	switch (func_sel) {
	case SM2_FUNC_SEL_KG:
		/* Copy public key to caller memory */
		memcpy(sm2_ctx->data.sm2.pub_key_px_py, result, 64);
		/* Copy rand_num_k to caller memory */
		memcpy(sm2_ctx->data.sm2.rand_num_k, sram + 64, 32);
		break;
	case SM2_FUNC_SEL_SN:
		/* Copy signature to caller memory */
		memcpy(sm2_ctx->data.sm2.sign_r_s, result, 64);
		break;
	case SM2_FUNC_SEL_VF:
		break;
	case SM2_FUNC_SEL_LP:
		/* Copy public key to caller memory */
		memcpy(sm2_ctx->data.sm2.x2_y2, result, 64);
		break;
	default:
		break;
	}

	/* set dpe done flag */
	sm2_ctx->dpe_done_flag = 1;
	dtag_put(DTAG_T_SRAM, sm2_ctx->dtag[0]);
	dtag_put(DTAG_T_SRAM, sm2_ctx->dtag[1]);
	return 0;
}

/*!
 * @brief This function geneates an ECC public key pair (px, py) for a given
 * private key.
 *
 *
 * @param - pri_key [in] pointer to buffer to 32 bytes of private key.
 * @param - pub_key [out] pointer to buffer to 64 bytes of public key pair (px || py).
 *			px and py are concatenated to form 64 bytes array.
 *
 * @return  none
 *
 * Notes:
 *	1. For an output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 */
void sec_sm2_get_pub_key(unsigned char *pri_key, unsigned char *pub_key)
{
	void *mem;
	void *result;
	dtag_t dtag[2];

	dtag[0] = dtag_get(DTAG_T_SRAM, &mem);
	dtag[1] = dtag_get(DTAG_T_SRAM, &result);
	sys_assert(dtag[0].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dtag[1].b.dtag != _inv_dtag.b.dtag);

	memset(mem, 0, DTAG_SZE);
	/* initialize context */
	dpe_sec_ctx.dtag[0] = dtag[0];
	dpe_sec_ctx.dtag[1] = dtag[1];

	memcpy( (u8 *)mem + 64, pri_key, 32);
	dpe_sec_ctx.data.sm2.rand_num_k = (u8 *)pri_key;
	dpe_sec_ctx.data.sm2.pub_key_px_py = (u8 *)pub_key;
	dpe_sec_ctx.data.sm2.sram = (void *)mem;
	dpe_sec_ctx.data.sm2.result= (void *)result;
	dpe_sec_ctx.data.sm2.func_sel = SM2_FUNC_SEL_KG;
	dpe_sec_ctx.dpe_done_flag = 0;
	bm_sm2_cal((void*)mem, result, SM2_FUNC_SEL_KG, dpe_sm2_cal_cmpl, &dpe_sec_ctx);

	/* Wait for the completion of SM2 request */
	sec_wait_bm_done();
	dpe_sec_ctx.dpe_done_flag = 0;
}

/*!
 * @brief This function performs pretreatment for SM2 sign operation
 *
 * @param sign_msg [in], pointer to sign message
 * @param msg_len [in], sign message length
 * @param user_id [in], pointer to User ID
 * @param id_len [in], User ID length
 * @param pri_key [in], pointer to private key
 * @param e [out], pointer to pretreatment result
 *
 * @return 0 - Failed, beacuse of the the length of message and User ID are too long
 *         1 - Pass
 */
int sec_sm2_sign_pretreatment(u8 *sign_msg, u32 msg_len, u8 *user_id, u32 id_len,
	u8 *pri_key, u8 *e)
{
	int i;
	u8 pub_key[64];
	dtag_t dtag;
	u8 *hash_msg;
	/* MSB                         ...                     LSB */
	u8 a[] = {0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC};

	u8 b[] = {0x28, 0xE9, 0xFA, 0x9E, 0x9D, 0x9F, 0x5E, 0x34,
		0x4D, 0x5A, 0x9E, 0x4B, 0xCF, 0x65, 0x09, 0xA7,
		0xF3, 0x97, 0x89, 0xF5, 0x15, 0xAB, 0x8F, 0x92,
		0xDD, 0xBC, 0xBD, 0x41, 0x4D, 0x94, 0x0E, 0x93};

	u8 xg[] = {0x32, 0xC4, 0xAE, 0x2C, 0x1F, 0x19, 0x81, 0x19,
		0x5F, 0x99, 0x04, 0x46, 0x6A, 0x39, 0xC9, 0x94,
		0x8F, 0xE3, 0x0B, 0xBF, 0xF2, 0x66, 0x0B, 0xE1,
		0x71, 0x5A, 0x45, 0x89, 0x33, 0x4C, 0x74, 0xC7};

	u8 yg[] = {0xBC, 0x37, 0x36, 0xA2, 0xF4, 0xF6, 0x77, 0x9C,
		0x59, 0xBD, 0xCE, 0xE3, 0x6B, 0x69, 0x21, 0x53,
		0xD0, 0xA9, 0x87, 0x7C, 0xC6, 0x2A, 0x47, 0x40,
		0x02, 0xDF, 0x32, 0xE5, 0x21, 0x39, 0xF0, 0xA0};

	u16 entl = id_len * 8;    ///<ID bits
	u8 za[32];
	u32 len;
	len = id_len + sizeof(a) + sizeof(b) + sizeof(xg) + sizeof(yg) + sizeof(pub_key);
	if (len > DTAG_SZE) {
		sys_assert(0);
		return 0;
	}
	sec_sm2_get_pub_key(pri_key, pub_key);

	dtag = dtag_get(DTAG_T_SRAM, (void **)&hash_msg);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	memset(hash_msg, 0x00, DTAG_SZE);
	len = 0;
	hash_msg[0] = (entl >> 8);
	hash_msg[1] = (entl & 0xFF);
	len += 2;
	memcpy(hash_msg + len, user_id, id_len);
	len += id_len;
	memcpy(hash_msg + len, a, sizeof(a));
	len += sizeof(a);
	memcpy(hash_msg + len, b, sizeof(b));
	len += sizeof(b);
	memcpy(hash_msg + len, xg, sizeof(xg));
	len += sizeof(xg);
	memcpy(hash_msg + len, yg, sizeof(yg));
	len += sizeof(yg);
	for (i = 31; i >= 0; i--)
		hash_msg[len++] = pub_key[i];
	for (i = 63; i >= 32; i--)
		hash_msg[len++] = pub_key[i];
	sec_gen_sm3_hash(hash_msg, len, za);
	memset(hash_msg, 0x00, DTAG_SZE);
	/* e = Hash( Za || M ) */
	memcpy(hash_msg, za, 32);
	memcpy(hash_msg + 32, sign_msg, msg_len);
	sec_gen_sm3_hash(hash_msg, 32 + msg_len, e);
	dtag_put(DTAG_T_SRAM, dtag);
	return 1;
}

/*!
 * @brief  This function performs a SM2 signature generation.
 *
 * @param - e [in] pointer to 32 byte of hash digest E.
 * @param - elen the length of the hash digest E, should be 32B
 * @param - rand_num [in], pointer to 32 bytes random number.
 * @param - pri_key [in] pointer to 32 byte of private key.
 * @param - sign [out], pointer to 32 bytes signature.
 *
 * @return  0 - FAIL.
 *	    1 - PASS
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 *
 */
int sec_sm2_sign(unsigned char *e, u32 elen, unsigned char *rand_num,
	unsigned char *pri_key, unsigned char *sign)
{
	void *mem;
	void *result;
	dtag_t dtag[2];
	u8 i;
	u8 *data;
	enum sm2_func_sel func_sel = SM2_FUNC_SEL_SN;

	if (elen != 32)
		return 0;

	u32 Gn[8] = {0x39D54123, 0x53BBF409, 0x21C6052B, 0x7203DF6B,
		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE};
	/* Check private key */
	for (i = 7; i >= 0 ; i--) {
		if (Gn[i] < *((u32 *)pri_key + i)) {
			return 0;
		} else {
			break;
		}
	}

	/* Use SM2 KG function select to calculate public key */
	dtag[0] = dtag_get(DTAG_T_SRAM, &mem);
	dtag[1] = dtag_get(DTAG_T_SRAM, &result);
	sys_assert(dtag[0].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dtag[1].b.dtag != _inv_dtag.b.dtag);
	memset(mem, 0, DTAG_SZE);


	/* initialize context */
	dpe_sec_ctx.dtag[0] = dtag[0];
	dpe_sec_ctx.dtag[1] = dtag[1];
	data = (u8 *)mem;

	/* Fill Hash value - E */
	memcpy(data, e, 32);
	/* Fill Private key - D */
	memcpy(data + 32, pri_key, 32);
	/* Fill Random number - K */
	memcpy(data + 64, rand_num, 32);
	/* Fill dpe context */
	dpe_sec_ctx.data.sm2.hash_e = (u8 *)e;
	dpe_sec_ctx.data.sm2.rand_num_k = (u8 *)rand_num;
	dpe_sec_ctx.data.sm2.pri_key_d = (u8 *)pri_key;
	dpe_sec_ctx.data.sm2.sign_r_s = (u8 *)sign;
	dpe_sec_ctx.data.sm2.sram = (void *)data;
	dpe_sec_ctx.data.sm2.result= result;
	dpe_sec_ctx.data.sm2.func_sel = func_sel;
	dpe_sec_ctx.dpe_done_flag = 0;
	bm_sm2_cal((void*)data, result, func_sel, dpe_sm2_cal_cmpl, &dpe_sec_ctx);

	/* Wait for the completion of SM2 request */
	sec_wait_bm_done();

	/* successful */
	return 1;
}

/*!
 * @brief This function performs a SM2 verification operation.
 *
 * @param - pub_key [in] pointer to 64 byte of public key pair (px, py)
 * @param - hash [in] pointer to 32 byte of hash digest.
 * @param - sign [in], pointer to 64 bytes signature R and S parts.
 *
 * @return  1 - PASS.
 *	    0 - FAIL
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 *
 */
int sec_sm2_verify(unsigned char *pub_key, unsigned char *hash, unsigned char *sign)
{
	void *mem;
	void *result;
	dtag_t dtag[2];
	u8 *data;
	enum sm2_func_sel func_sel = SM2_FUNC_SEL_VF;

	/* Use SM2 KG function select to calculate public key */
	dtag[0] = dtag_get(DTAG_T_SRAM, &mem);
	dtag[1] = dtag_get(DTAG_T_SRAM, &result);
	memset(mem, 0, DTAG_SZE);

	/* initialize context */
	dpe_sec_ctx.dtag[0] = dtag[0];
	dpe_sec_ctx.dtag[1] = dtag[1];
	sys_assert(dtag[0].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dtag[1].b.dtag != _inv_dtag.b.dtag);

	data = (u8 *)mem;
	/* Fill Hash value - E */
	memcpy(data, hash, 32);
	/* Fill Private key - D */
	memcpy(data + 32, sign, 64);
	/* Fill Random number - K */
	memcpy(data + 96, pub_key, 64);
	/* Fill dpe context */
	dpe_sec_ctx.data.sm2.hash_e = (u8 *)hash;
	dpe_sec_ctx.data.sm2.pub_key_px_py = (u8 *)pub_key;
	dpe_sec_ctx.data.sm2.sign_r_s = (u8 *)sign;
	dpe_sec_ctx.data.sm2.sram = (void *)data;
	dpe_sec_ctx.data.sm2.result= result;
	dpe_sec_ctx.data.sm2.func_sel = func_sel;
	dpe_sec_ctx.dpe_done_flag = 0x0;
	bm_sm2_cal((void*)data, result, func_sel, dpe_sm2_cal_cmpl, &dpe_sec_ctx);

	/* Wait for the completion of SM2 request */
	sec_wait_bm_done();
	dpe_sec_ctx.dpe_done_flag = 0x0;

	return (is_all_zero(result,64));
}

/*!
 * @brief This function performs a SM2 LP operation.
 *
 * @param - lp [in] pointer to 32 byte of lp. This could be a random number.
 * @param - pub_key_lp [in] pointer to 64 byte of public key pair (px, py)
 * @param - lp_x2y2 [out] pointer to 64 byte of x2, y2 pair. This is an external
 *			  point on elliptic curve.
 *
 * @return  1 - PASS.
 *	    0 - FAIL
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 *
 */
int sec_sm2_lp(unsigned char *lp, unsigned char *pub_key_lp, unsigned char *lp_x2y2)
{
	void *mem;
	void *result;
	dtag_t dtag[2];
	u8 *data;
	enum sm2_func_sel func_sel = SM2_FUNC_SEL_LP;

	/* Use SM2 KG function select to calculate public key */
	dtag[0] = dtag_get(DTAG_T_SRAM, &mem);
	dtag[1] = dtag_get(DTAG_T_SRAM, &result);
	sys_assert(dtag[0].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dtag[1].b.dtag != _inv_dtag.b.dtag);
	memset(mem, 0, DTAG_SZE);

	/* initialize context */
	dpe_sec_ctx.dtag[0] = dtag[0];
	dpe_sec_ctx.dtag[1] = dtag[1];
	data = (u8 *)mem;

	/* Fill LP value */
	memcpy(data + 64, lp, 32);
	/* Fill Public key LP - px, py */
	memcpy(data + 96, pub_key_lp, 64);
	/* Fill dpe context */
	dpe_sec_ctx.data.sm2.rand_num_lp = (u8 *)lp;
	dpe_sec_ctx.data.sm2.pub_key_px_py = (u8 *)pub_key_lp;
	dpe_sec_ctx.data.sm2.x2_y2 = (u8 *)lp_x2y2;
	dpe_sec_ctx.data.sm2.sram = (void *)data;
	dpe_sec_ctx.data.sm2.result= result;
	dpe_sec_ctx.data.sm2.func_sel = func_sel;
	dpe_sec_ctx.dpe_done_flag = 0;
	bm_sm2_cal((void*)data, result, func_sel, dpe_sm2_cal_cmpl, &dpe_sec_ctx);

	/* Wait for the completion of SM2 request */
	sec_wait_bm_done();
	dpe_sec_ctx.dpe_done_flag = 0;
	/* successful */
	return 1;
}

/*!
 * @brief DPE Security Subsystem RN generator completion function.
 *
 * @param ctx - [in] completion context.
 * @param rst - [in] completion result.
 *
 * @return  0 - PASS.
 */
static int sec_ss_rn_gen_cmpl(void* ctx, dpe_rst_t* rst)
{
	dpe_sec_calc_ctx_t *rn_gen_ctx = (dpe_sec_calc_ctx_t *)ctx;
	rn_gen_ctx->dpe_done_flag = 1;
	return 0;
}

/*!
 * @brief DPE Security Subsystem SPR Program completion function.
 *
 * @param ctx - [in] completion context.
 * @param rst - [in] completion result.
 *
 * @return  0 - PASS.
 */
static int sec_ss_spr_prgm_cmpl(void* ctx, dpe_rst_t* rst)
{
	dpe_sec_calc_ctx_t *spr_prgm_ctx = (dpe_sec_calc_ctx_t *)ctx;

	dtag_put(DTAG_T_SRAM, spr_prgm_ctx->dtag[0]);
	spr_prgm_ctx->dpe_done_flag = 1;
	return 0;
}

/*!
 * @brief DPE SM3/SHA3-256 calculation completion function.
 *
 * @param ctx - [in] completion context.
 * @param rst - [in] completion result.
 *
 * @return  0 - PASS
 *	    1 - FAIL
 */
int sec_gen_sha3_sm3_cmpl(void* ctx, dpe_rst_t* rst)
{
	dpe_sec_calc_ctx_t *sha3_sm3_ctx = (dpe_sec_calc_ctx_t *)ctx;
	memcpy(sha3_sm3_ctx->data.sha3sm3.hash, dtag2mem(sha3_sm3_ctx->dtag[1]), 32);
	sys_assert(rst->error != -1);
	sha3_sm3_ctx->dpe_done_flag = 1;
	return 0;
}

/*!
 * @brief This is a wrapper function to perform SM3 or SHA3-256 hash operation.
 *
 * @param - src [in] pointer to input message.
 * @param - len [in] length of input message.
 * @param - result [out], pointer to 32 bytes SM3 hash digest.
 * @param - sha3_sm3 [in] True for SM3, False for SHA3-256.
 *
 * @return  None.
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 *
 */
static ddr_code void sec_gen_sha3_sm3_hash(void *src, u32 len, void *result, bool sha3_sm3)
{
	void *msg, *digest;
	u32 i;
	u32 cnt, calc_size;
	dpe_sec_ctx.dtag[0] = dtag_get_urgt(DTAG_T_SRAM, (void **)&msg);
	dpe_sec_ctx.dtag[1] = dtag_get_urgt(DTAG_T_SRAM, (void **)&digest);
	sys_assert(dpe_sec_ctx.dtag[0].b.dtag != _inv_dtag.b.dtag);
	sys_assert(dpe_sec_ctx.dtag[1].b.dtag != _inv_dtag.b.dtag);
	cnt = occupied_by(len, DTAG_SZE);
	//may be the massage length is zero that we didn't support
	if (cnt == 0) {
		memset(result, 0xFF, 32);
		return;
	}
	dpe_sec_ctx.data.sha3sm3.hash = result;
	for (i = 0; i < cnt; i++) {
		calc_size = min(len, DTAG_SZE);
		len -= calc_size;
		memcpy(msg, src + i * calc_size, calc_size);
		dpe_sec_ctx.dpe_done_flag = 0;
		bm_sha3_sm3_calc(msg, digest, sha3_sm3,calc_size,sec_gen_sha3_sm3_cmpl, &dpe_sec_ctx);
		sec_wait_bm_done();
	}
	dtag_put(DTAG_T_SRAM, dpe_sec_ctx.dtag[0]);
	dtag_put(DTAG_T_SRAM, dpe_sec_ctx.dtag[1]);
	dpe_sec_ctx.dpe_done_flag = 0;
}

/*!
 * @brief This function performs a SM3 hash operation.
 *
 * @param - in_msg [in] pointer to input message.
 * @param - msg_len [in] length of input message.
 * @param - hash [out], pointer to 32 bytes SM3 hash digest.
 *
 * @return  None.
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 *
 */
fast_code void sec_gen_sm3_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash)
{
	/* call common hash function */
	sec_gen_sha3_sm3_hash(in_msg, msg_len, hash, HASH_SM3);
}

/*!
 * @brief This function performs a SHA3-256 hash operation.
 *
 * @param - in_msg [in] pointer to input message.
 * @param - msg_len [in] length of input message.
 * @param - hash [out], pointer to 32 bytes SM3 hash digest.
 *
 * @return  None.
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 *
 */
ddr_code void sec_gen_sha3_256_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash)
{
	/* call common hash function */
	sec_gen_sha3_sm3_hash(in_msg, msg_len, hash, HASH_SHA3_256);
}
/*!
 * @brief This function used to generate C1 for SM2 encryption
 *
 * @param random [in], pointer to random number
 * @param c1 [out], pointer to C1, 64 bytes
 *
 * @return none
 */
void _sec_sm2_encrypt_gen_c1(u8 *random, u8 *c1)
{
	sec_sm2_get_pub_key(random, c1);
}

/*!
 * @brief This is Key Derivation Function(KDF)
 *
 * @param x2y2 [in], pointer to Key
 * @param klen [in], the length of the Key in bits
 * @param K [out], pointer to result
 *
 * @return none
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 */
void sec_sm2_kdf(u8 *x2y2, u32 klen, u8 *K)
{
	u32 v = 32 * 8;
	u8 digest[32];
	u8 z[68];
	int i, pos = 0;
	u32 ct;
	u32 cnt, residue;
	ct = 0x00000001;  ///< counter
	cnt = ((klen + v - 1) / v);
	residue = (klen % v);
	/* Z = x2y2 || ct */
	for (i = 31; i >= 0; i--)
		z[pos++] = *(x2y2 + i);

	for (i = 63; i >= 32; i--)
		z[pos++] = *(x2y2 + i);

	for (i = 0; i < cnt; i++) {
		cpu_to_be32(ct++, &z[64]);
		sec_gen_sm3_hash(z, 68, digest);
		if (i == cnt - 1) {
			memcpy(K + i * 32, digest, residue == 0 ? 32 : residue / 8);
		} else {
			memcpy(K + i * 32, digest, 32);
		}
	}
}

/*!
 * @brief This function used to generate C2 for SM2 encryption
 *
 * @param x2ye [in], pointer to Key
 * @param klen [in], the length of the key in bits
 * @param msg [in], pointer to message
 * @param c2 [out], pointer to C2
 *
 * @return true - Pass
 *         false - Fail
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 */
bool _sec_sm2_encrypt_gen_c2(u8 *x2y2, u32 klen, u8 *msg, u8 *c2)
{
	u8 *t;
	u32 i;
	u32 dcnt = occupied_by(klen/8, DTAG_SZE);
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, dcnt);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	t = (u8 *)dtag2mem(dtag);
	sec_sm2_kdf(x2y2, klen, t);
	if (is_all_zero(t, klen/8)){
		return false;
	}
	for (i = 0; i < klen/8; i++){
		c2[i] = t[i] ^ msg[i];
	}
	dtag_cont_put(DTAG_T_SRAM, dtag, dcnt);
	return true;
}

/*!
 * @brief This function used to generate C3 for SM2 encryption
 *
 * @param msg [in], pointer to messge
 * @param msg_len [in], the length of the message
 * @param x2y2 [in], pointer to Key
 * @param c3 [out], pointer to C3
 *
 * @return None
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 */
void _sec_sm2_encrypt_gen_c3(u8 *msg, u32 msg_len, u8 *x2y2, u8 *c3)
{
	u8 *c3_hash;
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, occupied_by(msg_len, DTAG_SZE));
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	c3_hash = dtag2mem(dtag);
	int i, pos = 0;
	for (i = 31; i >= 0; i--)
		c3_hash[pos++] = *(x2y2 + i);
	for (i = 0; i < msg_len; i++)
		c3_hash[pos++] = *(msg + i);
	for (i = 63; i >= 32; i--)
		c3_hash[pos++] = *(x2y2 + i);
	sec_gen_sm3_hash(c3_hash, 64 + msg_len, c3);
	dtag_cont_put(DTAG_T_SRAM, dtag, occupied_by(msg_len, DTAG_SZE));
}

/*!
 * @brief This function performs an SM2 encryption of a given plain message
 *
 * @param - pmsg [in], pointer to plain message.
 * @param - pmsg_len [in], length of plain message
 * @param - pub_key [in] pointer to 64 bytes of public key pair (px || py).
 *			px and py are concatenated to form 64 bytes array.
 * @param - random [in] pointer to 32 bytes of random number.
 * @param - cmsg [out], pointer to cipher message.
 *
 * @return  0 - PASS.
 *	    3 - FAIL (public key is zero)
 *	    4 - FAIL (x2 || y2 is zero)
 *	    5 - FAIL (calcuate C2 failed)
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *		    byte stream LSB is 00 and MSB is 1F
 */
int sec_sm2_encrypt(u8 *pmsg, u32 pmsg_len, u8 *pub_key,
	u8 *random, u8 *cmsg)
{
	u8 c1[64];
	u8 x2y2[64];
	u8 *c2;
	u8 c3[32];
	u32 pos = 0;
	int i;
	sys_assert(pmsg_len + 64 + 32 <= DTAG_SZE); ///< maybe you can comment this assert, if you want to encrypte more message
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&c2);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);

	/* Step2: C1=[k]G=(x1, y1) */
	_sec_sm2_encrypt_gen_c1(random, c1);
	/* Step3: Calculte Curve points S=[h]Pb, h==1 */
	if (is_all_zero(c1, 16))
		return 3;
	/* Step4: Calculte [k]Pb=(x2, y2) */
	sec_sm2_lp(random, pub_key, x2y2);
	if (is_all_zero(x2y2, 64))
		return 4;

	/* Step6: Calculte C2=M^t */
	if (!_sec_sm2_encrypt_gen_c2(x2y2, pmsg_len * 8, pmsg, c2))
		return 5;

	/* Step7: Calculate C3=Hash(x2||M||y2) */
	_sec_sm2_encrypt_gen_c3(pmsg, pmsg_len, x2y2, c3);
	for (i = 31; i >= 0; i--)
		cmsg[pos++] = c1[i];
	for (i = 63; i >= 32; i--)
		cmsg[pos++] = c1[i];
	memcpy(cmsg + 64, c3, 32);
	memcpy(cmsg + 64 + 32, c2, pmsg_len);
	dtag_put(DTAG_T_SRAM, dtag);
	return 0;
}

/*!
 * @brief This function performs an SM2 decryption of a given cipher message
 *
 * @param - pri_key [in], pointer to private key.
 * @param - cmsg [in], pointer to cipher message.
 * @param - pmsg [out], pointer to plain message.
 * @param - cmsg_len [in], length of cipher message
 *
 * @return	0 - FAIL.
 *		1 - PASS.
 *
 * Notes:
 *	1. For an input/output byte stream of B[0]B[1]B[2]B3]....B[28]B[29]B[30]B[31]
 *		The LSB is B[0] and MSB is B[31]
 *		Ex: In 000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F
 *			byte stream LSB is 00 and MSB is 1F
 *	3. cmsg format = (C1 || C3 || C2)
 */
ddr_code int sec_sm2_decrypt(u8 *cmsg, u32 cmsg_len, u8 *pri_key, u8 *pmsg)//joe slow->ddr 20201124
{
	u8 c1[64];
	u8 x2y2[64];
	int i, pos = 0;
	u8 *t, *c2, *mm;
	u32 pmsg_len = cmsg_len - 64 - 32;
	dtag_t dtag[4];
	u8 u[32];
	u8 *c3_hash;
	int res = 1;
	u32 Gn[8] = {0x39D54123, 0x53BBF409, 0x21C6052B, 0x7203DF6B,
		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE};

	/* Check cipher message length */
	if (cmsg_len <= 96 || cmsg_len > DTAG_SZE){
		sys_assert(0);
		return 0;
	}
	for (i = 0; i < 4; i++) {
		dtag[i] = dtag_get(DTAG_T_SRAM, NULL);
		sys_assert(dtag[i].b.dtag != _inv_dtag.b.dtag);
	}

	/* Check private key */
	for (i = 7; i >= 0 ; i--) {
		if (Gn[i] < *((u32 *)pri_key + i)) {
			sys_assert(0);
			return 0;
		} else {
			break;
		}
	}
	for (i = 31; i >= 0; i--)
		c1[pos++] = cmsg[i];
	for (i = 63; i >= 32; i--)
		c1[pos++] = cmsg[i];
	t = (u8 *)dtag2mem(dtag[0]);
	sec_sm2_lp(pri_key, c1, x2y2);
	sec_sm2_kdf(x2y2, (pmsg_len) * 8, t);

	c2 = (u8 *)dtag2mem(dtag[1]);
	memcpy(c2, cmsg + 64 + 32, pmsg_len);
	mm = (u8 *)dtag2mem(dtag[2]);
	for (i = 0; i < pmsg_len; i++)
		mm[i + 32] = c2[i] ^ t[i];
	memcpy(pmsg, mm + 32, pmsg_len);
	c3_hash = (u8 *)dtag2mem(dtag[3]);
	pos = 0;
	for (i = 31; i >= 0; i--)
		c3_hash[pos++] = *(x2y2 + i);
	for (i = 0; i < pmsg_len; i++)
		c3_hash[pos++] = *(pmsg + i);
	for (i = 63; i >= 32; i--)
		c3_hash[pos++] = *(x2y2 + i);
	sec_gen_sm3_hash(c3_hash, 64 + pmsg_len, u);
	for (i = 0; i < 32; i++) {
		if (u[i] != cmsg[i + 64]) {
			res = 0;
			sys_assert(0);
			break;
		}
	}
	for (i = 0; i < 4; i++)
		dtag_put(DTAG_T_SRAM, dtag[i]);
	return res;
}

/*!
 * @brief This function generate a random usig security subsytem and
 * write it to SPRx register.
 *
 * @param - spr_idx [in], SPR register index.
 *
 * @return  None
 */
ddr_code void sec_ss_rn_gen(enum spr_reg_idx spr_idx)
{
       dpe_sec_ctx.dpe_done_flag = 0;
	bm_ss_rn_gen(spr_idx, sec_ss_rn_gen_cmpl, &dpe_sec_ctx);

	/* Wait for the completion of Random Number Generate request */
	sec_wait_bm_done();
}

/*!
 * @brief This function program SPRx register with data from memory
 *
 * @param - spr_idx [in], SPR register index.
 * @param - mem [in], pointer to SRAM memory.
 *
 * @return  None
 *
 * Note: The SPR register size is 32 bytes. So the size of data in mem must
 *       be 32 bytes. If the caller need only 16 bytes, the remaining area
 *       of mem must be appended with zero or any unused data.
 */
ddr_code void sec_ss_spr_prgm(enum spr_reg_idx spr_idx, void *mem)
{
	void *data_in;
	dtag_t dtag;

	dtag = dtag_get_urgt(DTAG_T_SRAM, &data_in);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	memcpy(data_in, mem, 32);

	dpe_sec_ctx.dtag[0] = dtag;
	dpe_sec_ctx.dpe_done_flag = 0;
	bm_ss_spr_prgm(data_in, spr_idx, sec_ss_spr_prgm_cmpl, &dpe_sec_ctx);

	/* Wait for the completion of SPR Program request */
	sec_wait_bm_done();
	dpe_sec_ctx.dpe_done_flag = 0;
}

/*!
 * @brief This is key wrap unwrap completion function
 *
 * @param - ctx [in], Pointer to DPE context.
 * @param - rst [in], pointer to result.
 *
 * @return	0 - pass
 *
 */
static int sec_ss_key_wp_uwp_cmpl(void* ctx, dpe_rst_t* rst)
{
	dpe_sec_calc_ctx_t *kwp_ctx = (dpe_sec_calc_ctx_t *)ctx;
	kwp_ctx->dpe_done_flag = 1;
	return 0;
}

/*!
 * @brief This function is an API to program DPE to perfrom key wrap/unwrap.
 *
 * The key wrap/unwrap is done under various conditions.
 * AES-256. SM4-128B
 * Key size: 128bit or 256 bit
 * data in length: 128bit or 256 bit
 * secure or non-secure mode
 * chip finger print: enabled or disabled
 * Operation mode: key wrap, key unwrap, raw encrypt, raw decrypt
 * The key wrap/unwrap configuration will be set in kwp_cfg structure.
 *
 * @param - src [in], Pointer to input data. Depending on the operation mode,
 *			this could be plain or cipher text data.
 * @param - result[in], Pointer to output data. Depending on the operation mode,
 *			this could be plain or cipher text data.
 * @param - kwp_cfg [in], pointer to key wrap/unwrap configuration.
 *
 * @return  None
 *
 * Note: Depending on the operation mode, the plain data (plain key)
 *       could be written to SRAM, SPRx register or MEK SRAM at a
 *       specific index (0 to 63). In AES-XTS mode, one pair of indices
 *       are used to encrypt/decrypt data. Those sets are (0, 1), (2, 3)
 *       .. (62, 63). This forms 32 pairs. If the keys are just one set of
 *       16 or 32 bit (ex: SM4, AES-ECB) then only one of indices are used.
 *       Those are 0, 2, 4, .., 62
 * Warnning:  the sram bus is 256-bit wide, which is 32-byte, so the base address
 *            of src and result must be 32-byte aligned. for key-wrap, the result
 *            is 320-bit, and DPE will write 512bit, which is 64Byte. for secure mode,
 *            it will just write back all 0s. therefore, after key-wrap,
 *            the 64B of result-address will be written with 0s
 */
slow_code void sec_ss_key_wp_uwp(void *src, void *result, kwp_cfg_t *kwp_cfg)
{

	/* setup context */
	dpe_sec_ctx.data.kwp.data_in = src;
	dpe_sec_ctx.data.kwp.data_out = result;
	dpe_sec_ctx.dpe_done_flag = 0x00;
	bm_ss_key_wp_uwp(src, result, sec_ss_key_wp_uwp_cmpl, &dpe_sec_ctx, kwp_cfg);

	/* Wait for the completion of DPE key wrap/unwrap request */
	sec_wait_bm_done();
	dpe_sec_ctx.dpe_done_flag = 0x00;
}

/*!
* @brief This function is an API to program DPE to perfrom SM4 encryption/decryption.
*
* @param encr [in], True for data encryption, False for data decryption.
* @param mek [in], pointer to MEK.
* @param mek_len [in], MEK length must equal to 16 byte.
* @param msg [in], pointer to input message(plain or cipher).
* @param msg_len [in], input message length
* @param result [out], pointer to encrypte/decrypte result
*
* @return 0 - Fail
*		  1 - Pass
*/
int sec_sm4_ende(bool encr, u8 *mek, u32 mek_len, u8 *msg,
	u32 msg_len, void *result)
{
	kwp_cfg_t kwp_cfg;
	u8 spr_idx = SS_SPR_REG0;
	u32 i;
	if (msg_len < 16 || msg_len % 16 || mek_len != 16) {
		return 0;
	}
	/* Initialize key wrap context */
	memset((void *)&kwp_cfg, 0x00, sizeof(kwp_cfg_t));
	u8 *data_in, *data_out;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&data_in);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	data_out = data_in + 1024;
	kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
	kwp_cfg.aes_sm4_mode = 1; /* AES (0) or SM4 (1) mode */
	kwp_cfg.din_size = 0; /* din_size_select 0:128 bit, 1:256 bit */
	kwp_cfg.kek_size = 0; /* kek_size_select b'00:128 bit, b'10:256 bit */
	kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
	kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
	kwp_cfg.wrap_op = (encr ? SS_RAW_ENCR : SS_RAW_DECR); /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */

	sec_ss_spr_prgm(spr_idx, mek);
	for (i = 0; i < msg_len / 16; i++) {
		memcpy(data_in, msg + i * 16, 16);
		sec_ss_key_wp_uwp(data_in, data_out, &kwp_cfg);
		memcpy(result + i * 16, data_out, 16);
	}
	dtag_put(DTAG_T_SRAM, dtag);
	return 1;
}

#define WP_UMP_SM4_PLAIN_KEY_LEN 16
#define WP_UMP_SM4_CIPHER_KEY_LEN 24
#define WP_UMP_SM2_PLAIN_KEY_LEN 32
#define WP_UMP_SM2_CIPHER_KEY_LEN 40

#define WP_UMP_MEM_SHIFT 1024

void sec_cfg_kwp(kwp_cfg_t *kwp_cfg, u8 choice, bool sm4)
{
	kwp_cfg->cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
	kwp_cfg->aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
	kwp_cfg->din_size = (sm4) ? 0 : 1; /* din_size_select 0:128 bit, 1:256 bit */
	kwp_cfg->kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
	kwp_cfg->din_spr_idx = 1; /* SPR register index for data input 0 to 3 */
	kwp_cfg->kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
	kwp_cfg->wrap_op = choice; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
}

u32 sec_gen_kek_hash_msg(u8 *random, bool rerandom,
	u8 *seed, u8 idx, u8 *chip_id, u8 *result, char *str)
{
	u32 len = 0;
	if (rerandom)
		sec_gen_rand_number(random, 32);
	memcpy(result + len, random, 32);
	len += 32;
	memcpy(result + len, seed, 32);
	len += 32;
	result[len++] = idx;
	if (chip_id) {
		memcpy(result + len, chip_id, 32);
		len += 32;
	}
	memcpy(result + len, str, strlen(str));
	len += strlen(str);
	return len;
}
#endif //USE_CRYPTO_HW

#if SEC_RSA_TEST

/* Native 1024 mode, calculate bit length is 1024 bits */
char c_n1024_str[] = "3bb970d656c3fbe8d8f0557761e1805386ede4f412a9085c98bb6c97cdca4ed0c885fa6955e22ed14a480bd7ea419666d41367510582dc39d72ddb18110fd5f690ca43af75fc651e4c95dd53290affbf79f0591b3e8eeb25a86f6bc685ff4cfee2bb2d74ffdab8185bd11788e0e0e144b08ea39d9e26201bf1636e7be6699b5c";
char n_n1024_str[] = "a556e304d97ae434bdf2d8ce3e097d63cb363e7ae7259ed697926a98a6c8bf5516dcc0f66b677d1c843c279a169b1a6cdc823aad8166880088703bc48b6ea0598fc4babc43de8ce66e5e73e4e1ee5642062f5872221681ce064502ffd728f4af8e165ec62256cfc741b2b06304566b94c6c7d8e9fab8a970f96004efbe1c7899";
char d_n1024_str[] = "4c4f7c7864601a8e7f0d9f241ca1eb1a5dca4438b973d327f72fe26dd6d2ce760a8d455e0a2fc39701f4610c0a6ef881034fcc501456b4ec8dbda56e18912cfe88ec6863f478eadb5d6482986ed6249b860b0d4f3bd47ed8554ccae1112216fc015fdaf7a80e5d7afddff86aefab3869bfe8f09d3593d2be1cad54c4b222dea5";
char m_n1024_str[] = "7e0e31d2d37e9a469eb18b212b6dc2974fc88290d80f8000e8c106379ee94b691fa896cb85ee2d968e33d89690735309c0a54e60ee12ef73bb32da7aadd2ff8755673e1f6e432bb47ccc5347dda057ae6f90c2c4f8b7be56832fec5a7058bdd7855e94f1a5344ca2e2e8f0c02f8d69905f82cf8d3a493b83b747fa5caac4bf6a";
char r_n1024_str[] = "5aa91cfb26851bcb420d2731c1f6829c34c9c18518da6129686d9567593740aae9233f09949882e37bc3d865e964e593237dc5527e9977ff778fc43b74915fa6703b4543bc21731991a18c1b1e11a9bdf9d0a78ddde97e31f9bafd0028d70b5071e9a139dda93038be4d4f9cfba9946b393827160547568f069ffb1041e38767";
char rr_n1024_str[] = "951bd9a69d0da1afd7ff864c1e4fb2142ded0d3942e9d8a1db9c3c6c5ef0b9d49f9753e80482b56bc51cc7e605a75d5cd7cda3afba72f9c75ba30897a3e59f25d2e134eb87cb7e743ff1e780c6b321399463ce0217da1d51fa9e0d2923ccab4072622bf1865e3bb0bdabab5513c3feb30c93b5a7c578e6c67636a70eaf599d23";
u32 n0prime_n1024 = 0xd539a457;
u32 e_n1024 = 0xd;

/* Native 2048 mode, calculate bit length is 2048 bits */
char c_n2048_str[] = "407647b7f93a13a273194b739a3fb096c38aaba0d85ccf2a983709ac31b4d5fea8b99277d267e409a1022946b5aef87a88f969ca11c3814df1e6c6f62590790ebd9c61c86611b52acdbbc1130c6d8914b250e093ccdba10dc707c233ce1c2bf515973f86eb3a4e763c85876e9f3b13ad48d63c135b14dbcc9562ae6a654a0b60f9d18b3ac9bac505ace8acd7f62a29d75d4bbfaeac0149bde5b1eea46e5d06244cf3928b5f98fb0443d21aeeb8d0b1878aac698cb31b89324490a724ce842fe09fd17fefafa115ce7287094f1848c316e23005c5cd7270a168b30caf50273652af4cfc0a0a0df75128ebd6f01a9b1652cdd2c92ba5e6f3b3a6dfdeaee49d853f";
char n_n2048_str[] = "808f46bcedc6b7de82d460f9baf1491413d0f858d0225195715241216732fe25ea12f2b05b472627e424fbd2bf9ef0ded33519f75e5497b5909aca90d3c6febbf260b23b4f892bf0c48f19ce7ed45274ca140ffcdf187f9db0cc778ce9e6b1b469e0506c1de27811f499609457017af51e6973df4fbefe008345a5f8fe81bece9996211be5da0d2e319111675562699072bded77c787a842c2f521d222521f6445f0985d66d8dba0cc3995d61fe7236d24f040c14f67811315ecf861d373308403660b94e254195db0588e299ab159933b78669ca2c27485efcee82a39922ab5d60c10ff9a1bb8812f62fe08951230e9a035733d7d0873405c8b831981ec362f";
char d_n2048_str[] = "6cc800c73f59608133a0034976cc2a24ae4e5bfc61581da5d60a85e12ff0122014d4f4bc9c0120492381c1638e72cbd03c8f64bd9e9658fc17e5705329599c77a5b4480acd254c90a651b35ff529cf9de610f9d5f7d9a70f46d465287725aa0ecfbdcde5545d2a8558d08ccc499eca80a3944e5a7e8dea9d3a5edfec321717d2bae8b366181f1cd3d3fc9fbf619627ee04f6d8967589288c2ac7b6d4edfff11f0aa3ff15806d6942784e6f3fd198b5ad829377c61c68ca8b6524ea4c951a585a30e19963a05afe64777c20787162f5efdd8a2bcc88aa6900bf8b3765314b2b272c907179e4ca8664f5716500c51bdff324419e6b77b6d934b2423c7d528030dd";
char m_n2048_str[] = "3fa91b685fa6f0246903b25dd54d360809641400d182219a75e8a7346dc0c565c2680edc2716c66ea825b223bff40a7044ce0fa8d4d288c1fd582b3f456801060f51bb38360fbc36c9cc4baa995963e3385760d580b8b751741d630caf0d06890c522ad65f6dff587860bb625211175e5c9ae4c041da05a8454e987372df443948718a1c03bcfaacd187c322abbbbf121187cc52dca551bfe4162b03119c1867682c5b6a91fbd01e88f3683eb8c7374ead722f8aa328e0923a0b7d71477088762cea64e44994b9120f7dd6ccaff576b5b0c6f1b94864e169b214db620ac7234ad950449d8c57b4010fc5603d9a42a4eb3979d91672e3013932c3e78663db6c82";
char r_n2048_str[] = "7f70b943123948217d2b9f06450eb6ebec2f07a72fddae6a8eadbede98cd01da15ed0d4fa4b8d9d81bdb042d40610f212ccae608a1ab684a6f65356f2c3901440d9f4dc4b076d40f3b70e631812bad8b35ebf00320e780624f33887316194e4b961faf93e21d87ee0b669f6ba8fe850ae1968c20b04101ff7cba5a07017e41316669dee41a25f2d1ce6eee98aa9d966f8d421288387857bd3d0ade2dddade09bba0f67a29927245f33c66a29e018dc92db0fbf3eb0987eecea13079e2c8ccf7bfc99f46b1dabe6a24fa771d6654ea66cc48799635d3d8b7a103117d5c66dd54a29f3ef0065e4477ed09d01f76aedcf165fca8cc282f78cbfa3747ce67e13c9d1";
char rr_n2048_str[] = "227c6540f5fb3e2ec907fbec4efb81b08bd5e232159442c6fa385b3c4b60942f8e2fd3defdc8fbb754d11f7003d57fd8f4615ac429f87d099d9f88be78423c2f6b29f97bd84198f8ea8c1b563121e76a29f8807812db3c43f84e7edd6bf2d7bf7c00718ac36cd21f3ff2988ec92a72ae2f4190e5d1bff05b16b3d8e0a019265e1a8b013bb96e4dea5dbcf4a89dd76a39ababa661bbc0488fe9ad30b80fd05f14ae93d1ec6bbc1688ea4769d8b0d9940e30ab598b10a2b61bd1375169c3a4c8b1a45689eb32e8e5b202f5f4a7ef28d46f0cb0dd0a561f54a4805dcd108159a019a20add1182eaf5b0438a521374c60a15a23a76266f130da36fe06525f6acde26";
u32 n0prime_n2048 = 0xad92f31;
u32 e_n2048 = 0xd;

/* Native 2048 mode, calculate bit length is 2048 bits, special case: e = 0, used to test if HW can calcuate m ^ 0 */
u32 e_s1_n2048 = 0x0;
/* Native 2048 mode, calculate bit length is 2048 bits, special case: e = 1, used to test if HW can calcuate m ^ 1 */
u32 e_s2_n2048 = 0x1;

/* calcuate bit length: 2048 in native 4096 mode */
char c_2048_str[] = "407647b7f93a13a273194b739a3fb096c38aaba0d85ccf2a983709ac31b4d5fea8b99277d267e409a1022946b5aef87a88f969ca11c3814df1e6c6f62590790ebd9c61c86611b52acdbbc1130c6d8914b250e093ccdba10dc707c233ce1c2bf515973f86eb3a4e763c85876e9f3b13ad48d63c135b14dbcc9562ae6a654a0b60f9d18b3ac9bac505ace8acd7f62a29d75d4bbfaeac0149bde5b1eea46e5d06244cf3928b5f98fb0443d21aeeb8d0b1878aac698cb31b89324490a724ce842fe09fd17fefafa115ce7287094f1848c316e23005c5cd7270a168b30caf50273652af4cfc0a0a0df75128ebd6f01a9b1652cdd2c92ba5e6f3b3a6dfdeaee49d853f";
char n_2048_str[] = "808f46bcedc6b7de82d460f9baf1491413d0f858d0225195715241216732fe25ea12f2b05b472627e424fbd2bf9ef0ded33519f75e5497b5909aca90d3c6febbf260b23b4f892bf0c48f19ce7ed45274ca140ffcdf187f9db0cc778ce9e6b1b469e0506c1de27811f499609457017af51e6973df4fbefe008345a5f8fe81bece9996211be5da0d2e319111675562699072bded77c787a842c2f521d222521f6445f0985d66d8dba0cc3995d61fe7236d24f040c14f67811315ecf861d373308403660b94e254195db0588e299ab159933b78669ca2c27485efcee82a39922ab5d60c10ff9a1bb8812f62fe08951230e9a035733d7d0873405c8b831981ec362f";
char d_2048_str[] = "6cc800c73f59608133a0034976cc2a24ae4e5bfc61581da5d60a85e12ff0122014d4f4bc9c0120492381c1638e72cbd03c8f64bd9e9658fc17e5705329599c77a5b4480acd254c90a651b35ff529cf9de610f9d5f7d9a70f46d465287725aa0ecfbdcde5545d2a8558d08ccc499eca80a3944e5a7e8dea9d3a5edfec321717d2bae8b366181f1cd3d3fc9fbf619627ee04f6d8967589288c2ac7b6d4edfff11f0aa3ff15806d6942784e6f3fd198b5ad829377c61c68ca8b6524ea4c951a585a30e19963a05afe64777c20787162f5efdd8a2bcc88aa6900bf8b3765314b2b272c907179e4ca8664f5716500c51bdff324419e6b77b6d934b2423c7d528030dd";
char m_2048_str[] = "3fa91b685fa6f0246903b25dd54d360809641400d182219a75e8a7346dc0c565c2680edc2716c66ea825b223bff40a7044ce0fa8d4d288c1fd582b3f456801060f51bb38360fbc36c9cc4baa995963e3385760d580b8b751741d630caf0d06890c522ad65f6dff587860bb625211175e5c9ae4c041da05a8454e987372df443948718a1c03bcfaacd187c322abbbbf121187cc52dca551bfe4162b03119c1867682c5b6a91fbd01e88f3683eb8c7374ead722f8aa328e0923a0b7d71477088762cea64e44994b9120f7dd6ccaff576b5b0c6f1b94864e169b214db620ac7234ad950449d8c57b4010fc5603d9a42a4eb3979d91672e3013932c3e78663db6c82";
char r_2048_str[] = "227c6540f5fb3e2ec907fbec4efb81b08bd5e232159442c6fa385b3c4b60942f8e2fd3defdc8fbb754d11f7003d57fd8f4615ac429f87d099d9f88be78423c2f6b29f97bd84198f8ea8c1b563121e76a29f8807812db3c43f84e7edd6bf2d7bf7c00718ac36cd21f3ff2988ec92a72ae2f4190e5d1bff05b16b3d8e0a019265e1a8b013bb96e4dea5dbcf4a89dd76a39ababa661bbc0488fe9ad30b80fd05f14ae93d1ec6bbc1688ea4769d8b0d9940e30ab598b10a2b61bd1375169c3a4c8b1a45689eb32e8e5b202f5f4a7ef28d46f0cb0dd0a561f54a4805dcd108159a019a20add1182eaf5b0438a521374c60a15a23a76266f130da36fe06525f6acde26";
char rr_2048_str[] = "394bf1204f41dcb017e5c6b8ac5d082c0f99884a233f02dabbf171d32a74c0bdec25ddd3ac731047555b0caeb13d49c6b5ab0ad601fe1f3e3e48ef5c4d9f7559b2a24d09042404f343528c657377f35e4e1821a9a54b957534636f2f5e50dfd8e82f0965d1b41afe31ab039d5c15c264fb1de115b15e4c7cee0c13e78ca9af49f5062f1fcdc5ac5e97018355c0ebc30d510c3a25e71a548a4c23149ba3ce4847442ecec66b77bc74e043b3ed2bf8d0687d9f83f30a08f0d37b425d181a8a9ff6f95d5a7e25e22aebc3b11794475c20ecbcd457843a23f51d650334d6709790faf30f3bceeb57f35275858d4d99473739decbbac4feace62054dbc725cc13cd5e";
u32 n0prime_2048 = 0xad92f31;
u32 e_2048 = 0xd;

/* Native 3072 mode, calculate bit length is 3072 bits */
char c_n3072_str[] = "4dc3d40f6731dc8d27057344af3aeef836cbab13d688190b5f0ef086b7468495be43fda54b2aba50d6e94c550bfac854a004dce2dd33cbd203789ce03ec3945ffd0f562088fe5a00201a9c68a1f2d8bc7926689728242000d31ad8ab9d4fcc50adcf1b51d3595660b504f3db5970220f96f40ae3db03ea3c2822ca5e01ac69b3ff2ff52bc01b661a547838fa86caa275aa0dc8d0b9e450f44befe81283d94821f17d7551bf24586045011818e71fbf90948f1569ac52a3a563080a5756c2c0721f9f3d6a0900d537660122921f7173384232a1fc280102b2cf8649bec97f5d3e8bc6ef935d750a5700cdcdd074be82a65d4fd6bd8e0e41ee267b218cfe34d417da7b225869041ab9647ae929b05355209140fd0e542d393bd3da4cd2be3fa1967da4dfe567ceadb9d31f59c2cc113862a3169be49e1f8f6a8983ac31183ecb24b4e211102d443af4e6ea7eeeb6a9a74b4f4bef013ca1a5428a60d2fdc532c5ec662ce85b759ad33fc487dca04df3f7cf5c8e82e4fe0962bb3ea04f5f6b523e48";
char n_n3072_str[] = "8e004dd953214472bfd7e63e0e36a7e2a6e51e701bbe42fb68e99850abebea09337ec74efcfb05f9b35a67634049993f012aec6c215904c6f74e81079a696b69b65268ab9f25e2ff9e910805bf99b2562f5d9a5600d24518c24e5dabe1f76dd436e81bfd928a45cf668604abb632959f1ab1583571738f8846f66b35affdb42d31e02e1fd29bc56ef105d60b2992b451e9082564adffaed7b3f15b9b8630ccf8ddb1424a76be2141f89a511634b8718c36fb140a666a12161b5f67448d7db0d7abe1e52a3ad40918676e19f56180b916362b7c1f1b8b50e8abd9493bacffe8861ccbb3d425f49683be5a78559ad934dab7c18bab1801440f6426c4934b5e64f8d08931bb2378f3df1ce5b052f0c8522728696449057aff41273e4e2c55b7801168d9100216204f29a1944f85a007604a9b10ffa9c4e797b2b883cc06c232ae6b0d60b6f1483be20b46908a572147bad76deb5bece7994dbaab182870e3c45d7ad5354b4037f7fd8cddfda818c5346ebf10374a88d64d110776ca939eaa3aea59";
char d_n3072_str[] = "5762a60f959e5181b122664d9297c9c68e16d7a77388c6c2192d4a0a4269cb195ac42be1d6c1dc4ae4866702002d4a9ced06b8e014858cc935ba2804adcab8410dbc8f2e8952644e88f6c9dc272381701d25e8d276a8c80f3c7efe9128984396492c5ffe81902af5c8f002dfd2954861e90aac6fa8471d402bab55aae274bda5a889f4ffe411170931dc34f32d469659ca7b2ab41c4e92fae4e34c10f01e07fb9c1e502dd2eb282898fc80ab3422bc07844bbd8fd66b51b55f82de3dd7ac46666115205ac667221b3579d771abe37ebb296e675eaa7b4c2000290e8e9dfa2eb270a3ab357398e1fa7704f39fdbacd1f86c9162086815c156cc582f5c5b5478bc61a6aed8417057067716de0c7ef488f46b0e72838e56a859227efb2c2c8a5a63b561afe7ff65d2c871c7f0458fe984738ec305dd96a55404529e7c658f31fc687d93a953cb7938f6800baf762f77cd5f1aaa5bb04e35aa6e265a4b64121f43614aba230416e54b4867db8ea008671f0a58f71bdd5cd8eb8ae0e8b6732c6aaa65";
char m_n3072_str[] = "90ce2286f49324fb47abdc67b0f5a47831722dffe35ca31a86a6514ca5bb553702f47ea7cafc961acd87bb6c3f77e1b5155c32dc6d692f1949bcd22b3d5de16b26ede33d4f9cf723f62fbee1fd5fa0710d7805326715c168332dd169f9181ca2546797f559c606249c6d85ac2e4ace658dfe72848bf6320b96b9b13837493ad7aafecf0543d860ed8ea984c89e3da4bcc2594ad74a8368fb664ef6fb906b04e0f97fe65648d447454c418b2dddc6dbe8359d56124462d18dc08019990d92fb33943767eea23c765cec6d58310a997cca35f88d381530faabc62c3beb248b9fcc51e285e5221ce5c3170ed1b2f87dec4e76fae9da04ba84f5d4c36c9d15e76934318c7ed6e990aae5ac4454751b27ac107ee103715c4ba4f103becf9b05ab3c469a2e3f04fe1e6c23871229642e03434f415092d7b2ed6c6ebdfd27dab37b9ebc6d04ea8b8eb653161f12a46c95cd13ddde460078882102c5987792405ea69b1008ffa0ec43a7584e4ab52c2a4ce68d500b68f4c600cad125951fa74a72eb554";
char r_n3072_str[] = "71ffb226acdebb8d402819c1f1c9581d591ae18fe441bd04971667af541415f6cc8138b10304fa064ca5989cbfb666c0fed51393dea6fb3908b17ef86596949649ad975460da1d00616ef7fa40664da9d0a265a9ff2dbae73db1a2541e08922bc917e4026d75ba309979fb5449cd6a60e54ea7ca8e8c7077b90994ca50024bd2ce1fd1e02d643a910efa29f4d66d4bae16f7da9b520051284c0ea46479cf3307224ebdb58941debe0765aee9cb478e73c904ebf59995ede9e4a098bb72824f28541e1ad5c52bf6e79891e60a9e7f46e9c9d483e0e474af175426b6c453001779e3344c2bda0b697c41a587aa6526cb25483e7454e7febbf09bd93b6cb4a19b072f76ce44dc870c20e31a4fad0f37add8d7969bb6fa8500bed8c1b1d3aa487fee9726effde9dfb0d65e6bb07a5ff89fb564ef00563b18684d477c33f93dcd5194f29f490eb7c41df4b96f75a8deb845289214a4131866b24554e7d78f1c3ba2852acab4bfc8080273220257e73acb9140efc8b57729b2eef889356c6155c515a7";
char rr_n3072_str[] = "7322a0be69a1e5ef9c6d6bd52a80572fb726fd1b3f4cf5431525f90d980379c8b2275c33b59492c9a19d47a65858c1dda9f6d4bbaf58e228312f0998ed9dd4ba84ed2a4e0a4ff320fae4645b3f2fb009087eff736bdd0caccb49c7cda0a12797cc7479bed4fde87bc58c3dec64054eeac268c5fcd60c06b819ded2e44b3af09412d9f474144f5bceded9a8625a68d29ca5a35631b33956c6bbc3e06cde222d28e33bae7263ed43b06d66a15b6dc451950254013a1508c365082c38e5d2a209321b738f88549ac980c6660f2ff354853bf0044cd99487bd1ef7eb80f6896101f2005c5af7cf496ad54147c0cacaf9a0ce351f6f727b1ff575a95d58c00f07197353d839fa8f89677bbb835d9179a6bb3ae8d4f5277032edc9ce266d4240e37e9c21fab76b92b2a921db0469507772539e98afb1101e7e2dbb45d8080fb7b165589ea2b81b1fcf6335e0a0d50dc756ca8d2ad9e8b1e9b66c181d0ee23fc7f82f3122dd6e5f73414357d620b4aecb4d79276ff06c91667cc23e0e4631f7c54092bc";
u32 n0prime_n3072 = 0x95a4217;
u32 e_n3072 = 0xd;

/* Native 4096 mode, calculate bit length is 4096 bits */
char c_n4096_str[] = "aaafeb11cc2ac9f2393feda3ef82507d02b0cd54437be23104840b25b66d9e2811abd913ed6aa63ae910fe024fe46dd80f395218c252aee263de2c8b106967e3ed36f800acfdcd4c6388e58a4e507dd2b08dc88befb3089c806d429904587851c7982a1d8ba0942641838428d8a1bc8e09b45ddb3615b3d5c394115201cff3cb422121e7dff96547bfbf30dc1225eba314f6330af49af49505a83d5a1f17487adcbff9df511646bf0ce38b92acca1913e379b33c5a6e7f139865ffeacbd8e8cc5b17a9bf1b56bd281467f0236e19216a073aa1dbcb32b5031f7eb40d6494550ccec8556f3beb9e3c5f937e3de4a207d1ab3d930ad7f236be4231980a9ad04ab30f27fab4c186899db959d0cb4a771307b49d24f217edfd1c2321b78b97427320f99fddf8fc702a5b1ca1c8193802aadb438806896bd4a62cc33e5849ba61b139e97b35cc60f06e1caad5d2e07186efdc8a64a7def1fb297de288bf1e938238a105d9ba9fec81380dfc27b0e16ab4fe670667f2b9a51122b150cb218a283c5f7192d2c66ae19f95ef42cbe23a035dd49dd6975c3f0505add9a07281070300d141ce368ac34b473133a259d4460a03b6e0028e608baa8476791059ee0392e6bcbb3ab71b3b926374b7062b182647f2be900dd1fa6f097dba90720b0f793914a939ecc4b629104cd0b35558703e6c9a6f37733c58c0c09b4e5934e559c49eb269f";
char n_n4096_str[] = "8a3420aa3f6deb4adb703c1511957ce4838294ede2908633d3a80c56553af82acec5f4da57ecabc23d8d54b4335998088e1b8c6f263b5321e4b1da2632d209d3f733d697cf6abf5e77ffffc4e01614caef6dd9c0d74419821513e6a93c69e6a37b501ffcf3607af439e99cd28b39b89d63101740dd914f945e8fdf83dda5b50b8dd313ca5e1cb42b3ea8a57a946a8c3d623ad4cabf8f75a8fdde9112cf0248e34aa0e5e6b56f345ce960ec4c4913061dec6572f34794f4e436b9e8ad7ede4fa9e34df650f0a240b00ad93f1072c55540f1fbcc20eb47d793ecfec0cf71509303f332cb9b613590be220bcf9e8c6ca2176bdb722597f9f5981fcf6089501719ba043ad82d2ee3773aba76ad59821d4ccf29617aaa6e0b2ab337c35454c0353a8492b989851e6f5d6902b638c5b9b121fc1734f210b14eb2020ee3aa2ef3b037799590f30cf1b752e1371807f93f0552c6e58744d75cc64cbdb50f159200acd4f26b4f1dd2b246440d5194db318980044cc0653162dff518a662e494a82f9d922fd2bfd34a750f181cc47a0788faeaac5303b86885791a8a51c0d1614d4545113e1b350b083fa00a669b7190d493b7e2244394753095391757b809927589eeffa3ccc547ec2d7c550d3797d15b63a059d00ab5d7f4b9b935afd0ccc227d37a9e77d3ec7fe972f6ab2a7287ed352caa4315849e659bdb2aa2f3df4eec89f761c3ef";
char d_n4096_str[] = "74f107f28470b366b9adbcaf5da5dfd50cd0f42bbfb55ddd158e31d2e5a80d108793cf2ee7efa506d19ef8e73f246cf38befecfb9680f792d534075b66142fb35b047a80746e0463a09d89a6963a119805abb840b625ee32fe2488190bbc11eccacda4e9ba3df1e2586335ed3abaafe7a297762331a2570763b4d0d20a512309c6c64bd29e670e9abedd7854076dd9203f6cdb707ab4774039462bfc3901eee7b54d114d0facdd89b1c82a67ef1018de3e2e74f53c91bb8606ec13a67f0ae0f2367d0b7f90894a6d930697d2d7448336f423d41bdac6a2b83eb02cfe4c307c65cdc884e5efcadcefa6a7884b145beb9d11d8ae63d39f4ba60417f81e1881f450a64158007f70597b88be96017bf9124cd73fe8fbfd6d7e41c363912d7596f6f8b16ca0484dd6651db73a4d8629decb4dc32b022f5db98c4a60074ee03ac9fb3c5c75d3295eb74d0f95a520e507385b695f948b615d4fb516b0eb0a980783cf255433a739bf7d5300431ed34d411ce1f09b3238b046a1c835b5b705a66e7326ff3f59ac82ac901ffdb07bc0640b07acf94c042fa6842a28b96c92d961de87a586c5cd0058543ee931febd2ef04ea737998942a846219fa5cc1d886e373ce3813459aca07fab3c0ea6f7729656bfeb667b6fc7598e5813014c6dc87c4b5b54d58ce4a137d9b2df4f2c70d1a5ff18912e9590acd0883d352c35b0ad2f83af9ccd35";
char m_n4096_str[] = "7f818601fe28f4cef32dd774ca36893d5da201cacd14546a7aa458efb948c9b5c3d4507d18917e6ff8ee0963051a615fad4a6a264e278ab853ce3a7fa82162dd024a51aa9850f4eda24762a570efdb9c8a830c370be0f084044dd7114e9d56111821218ce32d7e16259c5fc738d5450a62672ebeaccac8a089252aa49695538f97092f06edf05946dbe6aa752c46f50be0cdc5f9f5f2fa5435c1ce62f1ac84e805a3d79edd2e24230af80deb7569c1a2b8af596fa1f8eb1e7ea97ae90207fcadfb63702b06009d84fec7412e44902033cada8471c4ad7e4909b41833f5da563d02d507b7adf92034ac4421eeeebea113da5f70b498e6a51389b55022d7af7a2a0001d467342cd712c18bf396ae6bd06c459d090fc0f4103df769339dbd0548169675a08d571ea5f130d673aa4c74e5777ad95bbe4e6ee203d7c58aa7fc79d6e707cb261b6c72733324404116d3120b6c58640e2342b0d4c681da1d799236d9209bb7c39f577dd0cc40a834ef36c301f81e63c9c8bc0cfb231e5942653a0ddbd782be8d8b9e4c21abf3461ba151018108ccc438ecf7f84a72f028c757adb2562de0132bcab8575965e9a8a93e2c6a906f200d1c02703aa1f39865f08cea3923c48d120cdbd0529e5b409a1e8815e95c8ea60f5c1841e95f4f6276795c52f8cf91fe3387468fcce570c453754c0c12d9c53285bd5e6e4bff9fadfe39c0aac70bb9";
char r_n4096_str[] = "75cbdf55c09214b5248fc3eaee6a831b7c7d6b121d6f79cc2c57f3a9aac507d5313a0b25a813543dc272ab4bcca667f771e47390d9c4acde1b4e25d9cd2df62c08cc2968309540a18800003b1fe9eb351092263f28bbe67deaec1956c396195c84afe0030c9f850bc616632d74c647629cefe8bf226eb06ba170207c225a4af4722cec35a1e34bd4c1575a856b9573c29dc52b3540708a5702216eed30fdb71cb55f1a194a90cba3169f13b3b6ecf9e2139a8d0cb86b0b1bc94617528121b0561cb209af0f5dbf4ff526c0ef8d3aaabf0e0433df14b8286c13013f308eaf6cfc0ccd34649eca6f41ddf4306173935de894248dda68060a67e0309f76afe8e645fbc527d2d11c88c5458952a67de2b330d69e855591f4d54cc83cabab3fcac57b6d46767ae190a296fd49c73a464ede03e8cb0def4eb14dfdf11c55d10c4fc8866a6f0cf30e48ad1ec8e7f806c0faad391a78bb28a339b3424af0ea6dff532b0d94b0e22d4db9bbf2ae6b24ce767ffbb33f9ace9d200ae7599d1b6b57d0626dd02d402cb58af0e7e33b85f877051553acfc47977a86e575ae3f2e9eb2babaeec1e4caf4f7c05ff599648e6f2b6c481ddbbc6b8acf6ac6e8a847f66d8a7611005c333ab813d283aaf2c8682ea49c5fa62ff54a280b4646ca502f333dd82c8561882c1380168d0954d58d7812cad355bcea7b619a6424d55d0c20b11376089e3c11";
char rr_n4096_str[] = "11693e58f1b18909a0b8a7337453b5f0b22e1b988e4a71170072ebc6d9bb6821342c8246908a27f6bb1b82809c814b92554b4d55dc5a109d9923082af996df30e85195f35128b6b2bd77ac129fa3a0d4c03cb83d8089da6af0fda148f81ee954e81ad0ea81e56825026e71e8b448c1569c295c83f5f0b06698dd1b087631cda9092f75f1173252c49ee27068167912972c61b52d3501e9b1e018b1188c88fc8c752ab30d96a9b15e869703fe054bbb82f89434bac81bdb85ab578fa17efca6748d4c39c5ee85d183b3374e2a24f3f5713f9c16fc2fe072422ca58986520315403acb53aebb0df08fa31d9a613d133845eaf37d88c3aafaf45efba8b6870af6fd5712c28224b59d711f2d78d97ebd6ed175456a01e5740a2558631ecbd5e3cb3cd48d6948e2531804a6b4b0df36259deff94fd154785fbabc7550fa941db46899bd754e0fd55a808b6639a790d9c3c9640b9986cc324d3dc2425e0a67d36c8e51adb4d6718b4b60973d512f4024c26ecaadea05defefb63429c46e63e3a88cf6a352ab7c20f985293f3244ca6c8fdb0d3d52ab11c2f283d9ba9c71fb5c60969c7e44066002db27e34dd65d26d7c93f721cfbd90e5e31e6557887fefdf65c5ded89bf24f45c24670f7600aa6ef7921cb67117862d0b2812400aee0f12d927e110c43096cfe39dd35c3233fbf04fdad898161dd68b1413d6fc74fc2e4acd4ab3b57";
u32 n0prime_n4096 = 0x1f7634f1;
u32 e_n4096 = 0xd;

int sec_rsa_calc_test(void)
{
	u32 ndigits;
	void *work_buffer;
	dtag_t dtags[3];
 	u32 get_cnt = dtag_get_bulk(DTAG_T_SRAM, 3, dtags);
	sys_assert(get_cnt == 3);
	u32 *n, *c, *d, *m, *r, *rr, *r_ans, *rr_ans;
	n = (u32 *)sdtag2mem(dtags[0].b.dtag);
	c = n + 512 / 4;
	d = c + 512 / 4;
	m = d + 512 / 4;

	r = (u32 *)sdtag2mem(dtags[1].b.dtag);
	rr = r + 512 / 4;
	r_ans = rr + 512 / 4;
	rr_ans = r_ans + 512 / 4;

	work_buffer = sdtag2mem(dtags[2].b.dtag);

	u32 result[4096 / 32];
	///< native 1024 mode
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 1024 / 32;
		sys_assert(ndigits == mpConvFromHex(c, ndigits, c_n1024_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_n1024_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(d, ndigits, d_n1024_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_n1024_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(r_ans, ndigits, r_n1024_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(rr_ans, ndigits, rr_n1024_str, work_buffer));
		sys_assert(n0prime_n1024 == sec_rsa_calc_nprime0(n, ndigits));
		sec_rsa_calc_r_rr(n, r, rr, SS_RSA_1024_MODE);
		sys_assert(0 == memcmp(r, r_ans, ndigits * 4));
		sys_assert(0 == memcmp(rr, rr_ans, ndigits * 4));
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_n1024, 1, m, ndigits, result, SS_RSA_1024_MODE);
		sys_assert(0 == memcmp(result, c, ndigits * 4));
		/* decryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, d, ndigits, c, ndigits, result, SS_RSA_1024_MODE);
		sys_assert(0 == memcmp(result, m, ndigits * 4));
		nvme_apl_trace(LOG_ERR, 0xfd4a, "RSA native 1024 mode test passed!\n");
	}
	///< native 2048 mode
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 2048 / 32;
		sys_assert(ndigits == mpConvFromHex(c, ndigits, c_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(d, ndigits, d_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(r_ans, ndigits, r_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(rr_ans, ndigits, rr_n2048_str, work_buffer));
		sys_assert(n0prime_n2048 == sec_rsa_calc_nprime0(n, ndigits));
		sec_rsa_calc_r_rr(n, r, rr, SS_RSA_2048_MODE);
		sys_assert(0 == memcmp(r, r_ans, ndigits * 4));
		sys_assert(0 == memcmp(rr, rr_ans, ndigits * 4));
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_n2048, 1, m, ndigits, result, SS_RSA_2048_MODE);
		sys_assert(0 == memcmp(result, c, ndigits * 4));
		/* decryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, d, ndigits, c, ndigits, result, SS_RSA_2048_MODE);
		sys_assert(0 == memcmp(result, m, ndigits * 4));
		nvme_apl_trace(LOG_ERR, 0x831a, "RSA native 2048 mode test passed!\n");
	}
	///< native 2048 mode, special case: e = 0;
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 2048 / 32;
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_n2048_str, work_buffer));
		/* e = 0, so c = m ^ e = 1 */
		mpSetDigit(c, 1, ndigits);
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_s1_n2048, 1, m, ndigits, result, SS_RSA_2048_MODE);
		sys_assert(0 == memcmp(result, c, ndigits * 4));
		nvme_apl_trace(LOG_ERR, 0xb943, "RSA native 2048 mode, special case: e = 0 passed!\n");
	}
	///< native 2048 mode, special case: e = 1;
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 2048 / 32;
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_n2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_n2048_str, work_buffer));
		/* e = 1, so c = m ^ e = m */
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_s2_n2048, 1, m, ndigits, result, SS_RSA_2048_MODE);
		sys_assert(0 == memcmp(result, m, ndigits * 4));
		nvme_apl_trace(LOG_ERR, 0xf751, "RSA native 2048 mode, special case: e = 1 passed!\n");
	}
	///< native 3072 mode
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 3072 / 32;
		sys_assert(ndigits == mpConvFromHex(c, ndigits, c_n3072_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_n3072_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(d, ndigits, d_n3072_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_n3072_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(r_ans, ndigits, r_n3072_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(rr_ans, ndigits, rr_n3072_str, work_buffer));
		sys_assert(n0prime_n3072 == sec_rsa_calc_nprime0(n, ndigits));
		sec_rsa_calc_r_rr(n, r, rr, SS_RSA_3072_MODE);
		sys_assert(0 == memcmp(r, r_ans, ndigits * 4));
		sys_assert(0 == memcmp(rr, rr_ans, ndigits * 4));
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_n3072, 1, m, ndigits, result, SS_RSA_3072_MODE);
		sys_assert(0 == memcmp(result, c, ndigits * 4));
		/* decryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, d, ndigits, c, ndigits, result, SS_RSA_3072_MODE);
		sys_assert(0 == memcmp(result, m, ndigits * 4));
		nvme_apl_trace(LOG_ERR, 0x6ec8, "RSA native 3072 mode test passed!\n");
	}
	///< native 4096 mode
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 4096 / 32;
		sys_assert(ndigits == mpConvFromHex(c, ndigits, c_n4096_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_n4096_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(d, ndigits, d_n4096_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_n4096_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(r_ans, ndigits, r_n4096_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(rr_ans, ndigits, rr_n4096_str, work_buffer));
		sys_assert(n0prime_n4096 == sec_rsa_calc_nprime0(n, ndigits));
		sec_rsa_calc_r_rr(n, r, rr, SS_RSA_4096_MODE);
		sys_assert(0 == memcmp(r, r_ans, ndigits * 4));
		sys_assert(0 == memcmp(rr, rr_ans, ndigits * 4));
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_n4096, 1, m, ndigits, result, SS_RSA_4096_MODE);
		sys_assert(0 == memcmp(result, c, ndigits * 4));
		/* decryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, d, ndigits, c, ndigits, result, SS_RSA_4096_MODE);
		sys_assert(0 == memcmp(result, m, ndigits * 4));
		nvme_apl_trace(LOG_ERR, 0xdf69, "RSA native 4096 mode test passed!\n");
	};
	///< calcuate bit length: 2048 in native 4096 mode
	{
		memset(n, 0, DTAG_SZE);
		memset(r, 0, DTAG_SZE);
		ndigits = 2048 / 32;
		sys_assert(ndigits == mpConvFromHex(c, ndigits, c_2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(n, ndigits, n_2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(d, ndigits, d_2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(m, ndigits, m_2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(r_ans, ndigits, r_2048_str, work_buffer));
		sys_assert(ndigits == mpConvFromHex(rr_ans, ndigits, rr_2048_str, work_buffer));
		sys_assert(n0prime_2048 == sec_rsa_calc_nprime0(n, ndigits));
		sec_rsa_calc_r_rr(n, r, rr, SS_RSA_4096_MODE);
		sys_assert(0 == memcmp(r, r_ans, ndigits * 4));
		sys_assert(0 == memcmp(rr, rr_ans, ndigits * 4));
		/* encryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, &e_n4096, 1, m, ndigits, result, SS_RSA_4096_MODE);
		sys_assert(0 == memcmp(result, c, ndigits * 4));
		/* decryption */
		memset(result, 0, sizeof(result));
		sec_rsa_calc(n, ndigits, d, ndigits, c, ndigits, result, SS_RSA_4096_MODE);
		nvme_apl_trace(LOG_ERR, 0xa46d, "RSA calcuate 2048 bit length in native 4096 mode test passed!\n");
	}
	nvme_apl_trace(LOG_ERR, 0xe511, "RSA Test Done\n");
	dtag_put_bulk(DTAG_T_SRAM, get_cnt, dtags);
	return 0;
}
#endif /* SEC_RSA_TEST */
