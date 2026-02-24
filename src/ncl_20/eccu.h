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

#ifndef _ECCU_H_
#define _ECCU_H_

#include "inc/ncb_eccu_register.h"

/// DU format type
enum du_fmt_t {
	DU_FMT_MR_4K = 0,
	DU_FMT_RAW_4K,
	DU_FMT_USER_4K,
	DU_FMT_USER_4K_CMF1,
#if defined(HMETA_SIZE)
	DU_FMT_USER_4K_HMETA,
#endif
    DU_FMT_USER_4K_PATROL_READ,  //tony 20210108
#if ONFI_DCC_TRAINING
	DU_FMT_RAW_512B,
	DU_FMT_RAW_4588B,
#endif
	DU_FMT_COUNT,
};

enum cmf_idx_t {
    LDPC_CMF_MR,
    LDPC_CMF_61_512,
    LDPC_CMF_61_4K,
    LDPC_CMF_62,
    LDPC_CMF_291,
    LDPC_CMF_MAX,
};

///  DU format Structure
struct du_format_t {
	///  Fix format
	eccu_du_fmt_reg0_t fmt0;
	eccu_du_fmt_reg1_t fmt1;

	///  Calc
	eccu_du_fmt_reg2_t fmt2;
	eccu_du_fmt_reg3_t fmt3;
	eccu_du_fmt_reg4_t fmt4;
	eccu_du_fmt_reg5_t fmt5;
	eccu_du_fmt_reg6_t fmt6;

	///  DU property
	u16 encoded_ecc_du_sz;
	u16 encoded_ecc_du_sz_2align;
};

struct fdma_cfg_group {
	u32 addr;
	eccu_fdma_cfg_xfsz_reg_t size;
	eccu_fdma_cfg_reg_t cfg;
	eccu_fdma_cfg_reg_t rdy;
};

/// FDMA config type
enum fdma_op{
	ROM_ENC_CMF_DL = 0,	// ROM encoder CMF download
	ROM_DEC_CMF_DL,		// ROM decoder CMF download
	ROM_SCR_SEED_LUT_DL,// ROM srambler seed LUT download
	SCR_SEED_LUT_DL,	// Srambler seed LUT download
	FDMA_ENC_CMF1_DL,	// ROM encoder CMF1 download
	FDMA_DEC_CMF1_DL,	// ROM decoder CMF1 download
	FDMA_ENC_CMF62_DL,
	FDMA_DEC_CMF62_DL,
	FDMA_ENC_CMF291_DL,
	FDMA_DEC_CMF291_DL,
	FDMA_ENC_CMFFFFF_DL,
	FDMA_ENC_CMF2_DL,	// ROM encoder CMF2 download
	FDMA_DEC_CMF2_DL,	// ROM decoder CMF2 download
	FDMA_OP_MAX,
};

#define FDMA_DTAG_QUEUE_DROP			2

#define FDMA_CONF_NORMAL_MODE			0
#define FDMA_CONF_CMF_MODE				1
#define FDMA_CONF_ROM_CMF_MODE			2
#define FDMA_CONF_SRC_LUT_MODE			5
#define FDMA_CONF_ROM_SRC_LUT_MODE		6

#define	FDMA_CONF_ENC				0
#define	FDMA_CONF_DEC				1

#define	FDMA_CONF_READ				1
#define	FDMA_CONF_WRITE				0

#if USE_8K_DU
#define NAND_DU_SIZE			8192
#else
#define NAND_DU_SIZE			4096
#endif

#if defined(TSB_XL_NAND) && !FAST_MODE
#define NAND_PAGE_SIZE		4096
#elif USE_8K_PAGE
#define NAND_PAGE_SIZE		8192
#else
#define NAND_PAGE_SIZE		16384
#endif

#define DU_CNT_PER_PAGE	    (NAND_PAGE_SIZE / NAND_DU_SIZE)
#define DU_CNT_PER_PAGE_SHIFT   (2)

#define DU_2K_MR_MODE	DU_FMT_MR_4K
#define DU_4K_DEFAULT_MODE DU_FMT_USER_4K
#define DU_CNT_SHIFT		ctz(DU_CNT_PER_PAGE)

#define DEC_MODE_MDEC		0
#define DEC_MODE_PDEC		1
#define DEC_MODE_MDEC_AND_PDEC	2

#define ARD_MODE_MDEC		0
#define ARD_MODE_PDEC		1

#define ERD_MODE_MDEC		0
#define ERD_MODE_PDEC		1

/*! DU format table for NCB */
extern struct du_format_t du_fmt_tbl[];

/*! Get encoded DU format size */
static inline u32 get_encoded_du_size(void) {
	return du_fmt_tbl[DU_FMT_USER_4K].encoded_ecc_du_sz_2align;
}

/*!
 * @brief Get the ECCU decoder error type
 *
 * @return enum value(error type no)
 */
 enum ncb_err eccu_dec_err_get(u16* cnt1); //cnt1 for vth tracking
/*!
 * @brief Get the ECCU encoder error type
 *
 * @return enum value(error type no)
 */
enum ncb_err eccu_enc_err_get(void);
/*!
 * @brief Resume the ECCU decoder
 *
 * @return Not used
 */
void resume_decoder(void);
/*!
 * @brief Resume the ECCU encoder
 *
 * @return Not used
 */
void resume_encoder(void);
/*!
 * @brief Reset ECCU HW status to default value
 *
 * @return Not used
 */
void eccu_hw_reset(void);
/*!
 * @brief ECCU module initialization
 *
 * @return Not used
 */
void eccu_init(void);
/*!
 * @brief Switch DU format type
 *
 * @param[in] idx		DU fomrat index
 *
 * @return Not used
 */
void eccu_switch_du_fmt(enum du_fmt_t idx);

// Jamie 
void eccu_switch_cmf(enum cmf_idx_t idx);

void eccu_dufmt_switch(enum cmf_idx_t idx);
/*!
 * @brief Judge now CMF is ROM or not
 *
 * @return value(true or false)
 */
bool eccu_cmf_is_rom(void);
/*!
 * @brief ECCU FDMA config selection
 *
 * @param[in] idx		FDMA config index
 *
 * @return Not used
 */
void eccu_fdma_cfg(enum fdma_op idx, bool power_down); //CMF Reload workaround _GENE_20201119
/*!
 * @brief Get encoder CMF size
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF size)
 */
u32 get_enc_cmf_size(u32 idx);
/*!
 * @brief Get encoder CMF buffer
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF code array)
 */
u32* get_enc_cmf_buf(u32 idx);
/*!
 * @brief Get decoder CMF size
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF size)
 */
u32 get_dec_cmf_size(u32 idx);
/*!
 * @brief Get decoder CMF buffer
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF code array)
 */
u32* get_dec_cmf_buf(u32 idx);
/*!
 * @brief Get scramble seed size
 *
 * @return value(size)
 */
u32 get_scr_seed_size(void);
/*!
 * @brief Get scramble seed buffer
 *
 * @return value(seed buffer array)
 */
u32* get_scr_seed_buf(void);
/*!
 * @brief Set ECCU decoder mode
 *
 * @param[in] otf		decoder mode no
 *
 * @return Not used
 */
void eccu_set_dec_mode(int otf);
/*!
 * @brief Set ECCU ARD decoder mode
 *
 * @param[in] dec		ARD decoder mode no
 *
 * @return Not used
 */
void eccu_set_ard_dec(u32 dec);
/*!
 * @brief Configure DU format register 0/1/2
 *
 * @param[in] idx		DU fomrat index
 *
 * @return Not used
 */
void eccu_du_fmt_cfg(int idx);

/*!
 * @brief Configure DU format overlimit threshold
 *
 * @param[in] idx		DU fomrat index
 * @param[in] thd		threshold
 *
 * @return Not used
 */
void eccu_cfg_ovrlmt_thd(int idx, u32 thd);

/*!
 * @brief SW initialize DU format info, should be used after eccu_du_fmt_cfg
 *
 * @param[in] fmt_id		DU fomrat ID
 *
 * @return Not used
 */
void eccu_du_fmt_init(int fmt_id);
void eccu_fast_mode(bool enable);
/*!
 * @brief Restore CMF & SCR LUT during PMU resume
 *
 * @return Not used
 */
void pmu_eccu_cmf_restore(void);

/*!
 * @brief Set partial DU detect config
 *
 * @param[in] enable		Partial DU detect feature enable.
 * @param[in] loc_start		Start location for bits to be checked. (In dword)
 *
 * @return Not used
 */
void eccu_partial_du_cfg(bool enable, u32 loc_start);

/*!
 * @brief Set partial DU detect bit mask
 *
 * @param[in] bit_mask		Bit mask for all 32 bits to be checked.
 *
 * @return Not used
 */
void eccu_partial_du_mask(u32 bit_mask);

extern eccu_conf_reg_t eccu_cfg;
#endif
