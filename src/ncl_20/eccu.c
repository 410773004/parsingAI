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
/*! \file eccu.c
 * @brief ECCU initialization and driver file
 *
 * \addtogroup ncl_20
 * \defgroup eccu
 * \ingroup ncl_20
 *
 * @{
 */
#include "eccu_reg_access.h"
#include "ncb_eccu_register.h"
#include "ndcu.h"
#include "eccu.h"
#include "ncl_err.h"
#include "cmf.h"
#include "ncl.h"
#include "nand.h"
#include "mpc.h"
#include "nand_tsb.h"
#include "eccu_pdec_register.h"
#include "ncl_cmd.h"
/*! \cond PRIVATE */
#define __FILEID__ eccu
#include "trace.h"
#include "../nvme/inc/nvme_spec.h"
/*! \endcond */
#if defined(USE_512B_HOST_SECTOR_SIZE)
#define HOST_SECTOR_SIZE	512			///< Host transfer sector size
#else
#define HOST_SECTOR_SIZE	4096		///< Host transfer sector size
#endif
#ifdef FAST_MODE
#  define ECCU_CORE_SEL		1/*BCH*/	///< ECCU mode selection , 1 for BCH, 0 for LDPC
#  define BCH_T			70 // Make sure encoded page size is no bigger than physical page size.
#else
#  define ECCU_CORE_SEL		0/*LDPC*/	///< ECCU mode selection , 1 for BCH, 0 for LDPC
#  define BCH_T			0
#endif
#define ECCU_OVRLMT_THD		300			///< ECCU over limit threshold
#ifdef History_read
#define ECCU_PATROL_READ_THD    160 //200//140     //ECCU over limit threshold for patral read  
#else
#define ECCU_PATROL_READ_THD    300 //200//140     //ECCU over limit threshold for patral read  
#endif

fast_data_zi bool cur_rom_cmf = false;
extern u32 fcmd_outstanding_cnt;
extern enum du_fmt_t host_du_fmt;

norm_ps_data eccu_conf_reg_t eccu_cfg;

/*! List all DU format for NCB */
norm_ps_data struct du_format_t du_fmt_tbl[DU_FMT_COUNT] = {
	/*! 2KB MR format, fixed value */
	{
		.fmt0 = {
			.b.host_sector_sz	= 4096,
			.b.du2host_ratio	= NAND_DU_SIZE / 4096,
			.b.hlba_mode		= 0,
			.b.meta_sz		= META_SIZE,
			.b.hlba_sz_20		= 0,
		},
		.fmt1 = {
#if defined(FPGA)  ///<Current Bitfile only support one CMF
			.b.ecc_cmf_sel		= 0,
#else
			.b.ecc_cmf_sel		= 1,
#endif //FPGA
			.b.cw_num_du		= (NAND_DU_SIZE / 4096),
			.b.meta_crc_en		= 1,
			.b.du_crc_6b_en		= 0,
			.b.du_crc_en		= 1,
			.b.ecc_bypass		= 0,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
			.b.meta_scr_en		= 1,
			.b.user_scr_en		= 1,
			.b.scr_lut_en		= 0,
			.b.scr_seed_sel		= 0,
			.b.ecc_core_sel		= ECCU_CORE_SEL,
			.b.bch_t		= BCH_T,
		},
	},

	/*! 4KB raw data format */
	{
		.fmt0 = {
			.b.host_sector_sz	= HOST_SECTOR_SIZE,
			.b.du2host_ratio	= NAND_DU_SIZE / HOST_SECTOR_SIZE,
			.b.hlba_mode		= 0,
			.b.meta_sz		= 0,
			.b.hlba_sz_20		= 0,
		},
		.fmt1 = {
			.b.ecc_cmf_sel		= 0,
			.b.meta_crc_en		= 0,
			.b.du_crc_6b_en		= 0,
			.b.du_crc_en		= 0,
			.b.ecc_bypass		= 0x3,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
			.b.meta_scr_en		= 0,
			.b.user_scr_en		= 0,
			.b.cw_num_du		= (NAND_DU_SIZE / 4096),
			.b.scr_lut_en		= 0,
			.b.scr_seed_sel		= 0,
			.b.ecc_core_sel		= ECCU_CORE_SEL,
			.b.bch_t		= BCH_T,
		},
	},

	/*! Default 4KB DU user and meta format */
	{
		.fmt0 = {
			.b.host_sector_sz	= HOST_SECTOR_SIZE,
			.b.du2host_ratio	= NAND_DU_SIZE / HOST_SECTOR_SIZE,
#if defined(DISABLE_HS_CRC_SUPPORT)
			.b.hlba_mode		= 1,
#else
			.b.hlba_mode		= 0,
#endif
			.b.meta_sz		= META_SIZE,
			.b.hlba_sz_20	        = 1, // 1:4b
		},
		.fmt1 = {
			.b.ecc_cmf_sel		= 0,
			.b.meta_crc_en		= 1,
			.b.du_crc_6b_en		= 0,
			.b.du_crc_en		= 1,
			.b.ecc_bypass		= 0,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
#if DEBUG
			.b.meta_scr_en		= 0,
			.b.user_scr_en		= 0,
#else
			.b.meta_scr_en		= 1,
			.b.user_scr_en		= 1,
#endif
			.b.cw_num_du		= (NAND_DU_SIZE / 4096),
			.b.scr_lut_en	        = 1,
			// Don't change to 1, see ticket #3978
#ifdef LJ_Meta
			.b.scr_seed_sel		= 1,// meta as seed
#else
			.b.scr_seed_sel		= 2,// meta as seed
#endif
			.b.ecc_core_sel		= ECCU_CORE_SEL,
			.b.bch_t		= BCH_T,
		},
	},

	/*! 4KB DU user and meta format use 2nd CMF */
	{
		.fmt0 = {
#if defined(HMETA_SIZE)
            .b.host_sector_sz   = 512 + HMETA_SIZE,
#else
			.b.host_sector_sz	= 512,
#endif
			.b.du2host_ratio	= NAND_DU_SIZE / 512,
#if defined(DISABLE_HS_CRC_SUPPORT)
			.b.hlba_mode		= 1,
#else
			.b.hlba_mode		= 0,
#endif
			.b.meta_sz		= META_SIZE,
			.b.hlba_sz_20	        = 1, // 1:4b
		},
		.fmt1 = {
			.b.ecc_cmf_sel		= 1,
			.b.meta_crc_en		= 1,
			.b.du_crc_6b_en		= 0,
			.b.du_crc_en		= 1,
			.b.ecc_bypass		= 0,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
#if DEBUG
			.b.meta_scr_en		= 0,
			.b.user_scr_en		= 0,
#else
			.b.meta_scr_en		= 1,
			.b.user_scr_en		= 1,
#endif
			.b.cw_num_du		= (NAND_DU_SIZE / 4096),
			.b.scr_lut_en	        = 1,
#ifdef LJ_Meta
			.b.scr_seed_sel		= 1,// Meta as seed
#else
			.b.scr_seed_sel		= 2,// Meta as seed
#endif
			.b.ecc_core_sel		= ECCU_CORE_SEL,
			.b.bch_t		= BCH_T,
		},
	},

#if defined(HMETA_SIZE)
	/*! 4KB DU user and meta format + PI */
	{
		.fmt0 = {
			.b.host_sector_sz	= HOST_SECTOR_SIZE + HMETA_SIZE,
			.b.du2host_ratio	= NAND_DU_SIZE / HOST_SECTOR_SIZE,
#if defined(DISABLE_HS_CRC_SUPPORT)
			.b.hlba_mode		= 1,
#else
			.b.hlba_mode		= 0,
#endif
			.b.meta_sz		= META_SIZE,
			.b.hlba_sz_20	= 0,
		},
		.fmt1 = {
			.b.ecc_cmf_sel		= 1,
            .b.meta_crc_en      = 1, // Jamie 20210111
			.b.cw_num_du		= (NAND_DU_SIZE / 4096),
			.b.du_crc_6b_en		= 0,
			.b.du_crc_en		= 1,
			.b.ecc_bypass		= 0,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
			.b.meta_scr_en		= 1,
			.b.user_scr_en		= 1,
			.b.scr_lut_en		= 1,
#ifdef LJ_Meta
			.b.scr_seed_sel		= 1,// Meta as seed
#else
			.b.scr_seed_sel		= 2,// Meta as seed
#endif
			.b.ecc_core_sel		= ECCU_CORE_SEL,
			.b.bch_t		= BCH_T,
			.b.pi_sz		= (HMETA_SIZE / 8),
		},
	},
#endif

	/*! Default 4KB DU user and meta format for patrol read */
	{
		.fmt0 = {
			.b.host_sector_sz	= HOST_SECTOR_SIZE,
			.b.du2host_ratio	= NAND_DU_SIZE / HOST_SECTOR_SIZE,
#if defined(DISABLE_HS_CRC_SUPPORT)
			.b.hlba_mode		= 1,
#else
			.b.hlba_mode		= 0,
#endif
			.b.meta_sz		= META_SIZE,
			.b.hlba_sz_20	        = 1, // 1:4b
		},
		.fmt1 = {
			.b.ecc_cmf_sel		= 0,
			.b.meta_crc_en		= 1,
			.b.du_crc_6b_en		= 0,
			.b.du_crc_en		= 1,
			.b.ecc_bypass		= 0,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
#if DEBUG
			.b.meta_scr_en		= 0,
			.b.user_scr_en		= 0,
#else
			.b.meta_scr_en		= 1,
			.b.user_scr_en		= 1,
#endif
			.b.cw_num_du		= (NAND_DU_SIZE / 4096),
			.b.scr_lut_en	        = 1,
			// Don't change to 1, see ticket #3978
#ifdef LJ_Meta
			.b.scr_seed_sel		= 1,// meta as seed  
#else
			.b.scr_seed_sel		= 2,// meta as seed
#endif
			.b.ecc_core_sel		= ECCU_CORE_SEL,
			.b.bch_t		= BCH_T,
		},
	},
#if ONFI_DCC_TRAINING
	{
		.fmt0 = {
			.b.host_sector_sz	= 512,//don't know why the minimum size is 128,here just want to get 32 Bytes
			.b.du2host_ratio	= 1,
			.b.meta_sz		= 0,
		},
		.fmt1 = {
			.b.cw_num_du		= 1,
			.b.meta_crc_en		= 0,
			.b.du_crc_en		= 0,
			.b.ecc_bypass		= 0x3,
			.b.ecc_inv_en		= 0,
			.b.du_crc_inv_en	= 0,
			.b.meta_scr_en		= 0,
			.b.user_scr_en		= 0,
#if   TACOMAX
			.b.scr_lut_en		= 0,
			.b.ecc_cmf_sel		= 0,
			.b.scr_seed_sel 	= 0,
			.b.ecc_core_sel 	= ECCU_CORE_SEL,
#endif
		},
	},
	{
			.fmt0 = {
				.b.host_sector_sz	= 4588,//4588 bytes
				.b.du2host_ratio	= 1,
				.b.meta_sz		= 0,
			},
			.fmt1 = {
				.b.cw_num_du		= 1,
				.b.meta_crc_en		= 0,
				.b.du_crc_en		= 0,
				.b.ecc_bypass		= 0x3,
				.b.ecc_inv_en		= 0,
				.b.du_crc_inv_en	= 0,
				.b.meta_scr_en		= 0,
				.b.user_scr_en		= 0,
#if   TACOMAX
				.b.scr_lut_en		= 0,
				.b.ecc_cmf_sel		= 0,
				.b.scr_seed_sel 	= 0,
				.b.ecc_core_sel 	= ECCU_CORE_SEL,
#endif
			},
	}
#endif

};

/*! List all FDMA config */
norm_ps_data struct fdma_cfg_group fdma_cfg_table[FDMA_OP_MAX] = {
	/*! 4KB DU encoder ROM CMF */
	{
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_ROM_CMF_MODE,
			.b.fdma_conf_enc_dec_sel = FDMA_CONF_ENC,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_enc_init = 1,
			// Workaround, ROM CMF should be encrypted version, and bypass set to 0, but it's unencrypted version at 7920.bit
#if defined(FPGA)  ///<Current Bitfile only support one CMF
			.b.fdma_conf_cmf_sel = 0,
#else
			.b.fdma_conf_cmf_sel = 1,
#endif //
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_enc_rdy = 1,

		},
	},
	/*! 4KB DU decoder ROM CMF */
	{
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_ROM_CMF_MODE,
			.b.fdma_conf_enc_dec_sel = FDMA_CONF_DEC,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_dec_init = 1,
			// Workaround, ROM CMF should be encrypted version, and bypass set to 0, but it's unencrypted version at 7920.bit
#if defined(FPGA)  ///<Current Bitfile only support one CMF
			.b.fdma_conf_cmf_sel = 0,
#else
			.b.fdma_conf_cmf_sel = 1,
#endif //FPGA
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_dec_rdy = 1,
		},
	},
	/*! ROM scrambler seed LUT */
	{
		.size = {
			.b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
		},
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_ROM_SRC_LUT_MODE,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_scr_lut_init = 1,
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_scr_lut_rdy = 1,
		},
	},
	/*! Scrambler seed LUT */
	{
		.addr = (u32)seed_lut_buf,
		.size = {
			.b.fdma_conf_xfer_sz = sizeof(seed_lut_buf)/sizeof(u32),
			.b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
		},
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_SRC_LUT_MODE,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_scr_lut_init = 1,
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_scr_lut_rdy = 1,
		},
	},
	/*! 4KB DU encoder CMF1 */
	{
		.addr = (u32)cmf_enc_code1,
		.size = {
			.b.fdma_conf_xfer_sz = sizeof(cmf_enc_code1)/sizeof(u32),
			.b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
		},
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
			.b.fdma_conf_enc_dec_sel = FDMA_CONF_ENC,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_enc_init = 1,
			.b.fdma_conf_cmf_sel = 0,
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_enc_rdy = 1,

		},
	},
	/*! 4KB DU decoder CMF1 */
	{
		.addr = (u32)cmf_dec_code1,
		.size = {
			.b.fdma_conf_xfer_sz = sizeof(cmf_dec_code1)/sizeof(u32),
			.b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
		},
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
			.b.fdma_conf_enc_dec_sel = FDMA_CONF_DEC,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_dec_init = 1,
			.b.fdma_conf_cmf_sel = 0,
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_dec_rdy = 1,
		},
	},
#if defined(HMETA_SIZE)
    /*! 4KB DU encoder CMF62 */
    {
        .addr = (u32)_cmf_enc_code62,
        .size = {
            .b.fdma_conf_xfer_sz = sizeof(_cmf_enc_code62)/sizeof(u32),
            .b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
        },
        .cfg = {
            .b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
            .b.fdma_conf_enc_dec_sel = FDMA_CONF_ENC,
            .b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
            .b.fdma_conf_enc_init = 1,
            .b.fdma_conf_cmf_sel = 1,
        },
        .rdy = {
            .b.fdma_conf_xfer_rd_finish = 1,
            .b.fdma_conf_enc_rdy = 1,
    
        },
    },
    /*! 4KB DU decoder CMF62 */
    {
        .addr = (u32)_cmf_dec_code62,
        .size = {
            .b.fdma_conf_xfer_sz = sizeof(_cmf_dec_code62)/sizeof(u32),
            .b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
        },
        .cfg = {
            .b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
            .b.fdma_conf_enc_dec_sel = FDMA_CONF_DEC,
            .b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
            .b.fdma_conf_dec_init = 1,
            .b.fdma_conf_cmf_sel = 1,
        },
        .rdy = {
            .b.fdma_conf_xfer_rd_finish = 1,
            .b.fdma_conf_dec_rdy = 1,
        },
    },
    /*! 4KB DU encoder CMF291 */
    {
        .addr = (u32)_cmf_enc_code291,
        .size = {
            .b.fdma_conf_xfer_sz = sizeof(_cmf_enc_code291)/sizeof(u32),
            .b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
        },
        .cfg = {
            .b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
            .b.fdma_conf_enc_dec_sel = FDMA_CONF_ENC,
            .b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
            .b.fdma_conf_enc_init = 1,
            .b.fdma_conf_cmf_sel = 1,
        },
        .rdy = {
            .b.fdma_conf_xfer_rd_finish = 1,
            .b.fdma_conf_enc_rdy = 1,

        },
    },
    /*! 4KB DU decoder CMF291 */
    {
        .addr = (u32)_cmf_dec_code291,
        .size = {
            .b.fdma_conf_xfer_sz = sizeof(_cmf_dec_code291)/sizeof(u32),
            .b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
        },
        .cfg = {
            .b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
            .b.fdma_conf_enc_dec_sel = FDMA_CONF_DEC,
            .b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
            .b.fdma_conf_dec_init = 1,
            .b.fdma_conf_cmf_sel = 1,
        },
        .rdy = {
            .b.fdma_conf_xfer_rd_finish = 1,
            .b.fdma_conf_dec_rdy = 1,
        },
    },
    /*! 4KB DU encoder CMF FFFF */
    {
        .addr = (u32)_cmf_enc_codeffff,
        .size = {
            .b.fdma_conf_xfer_sz = sizeof(_cmf_enc_codeffff)/sizeof(u32),
            .b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
        },
        .cfg = {
            .b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
            .b.fdma_conf_enc_dec_sel = FDMA_CONF_ENC,
            .b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
            .b.fdma_conf_enc_init = 1,
            .b.fdma_conf_cmf_sel = 1,
        },
        .rdy = {
            .b.fdma_conf_xfer_rd_finish = 1,
            .b.fdma_conf_enc_rdy = 1,
    
        },
    },
#endif
#if ENABLE_2_CMF
	/*! 4KB DU encoder CMF2 */
	{
		.addr = (u32)cmf_enc_code2,
		.size = {
			.b.fdma_conf_xfer_sz = sizeof(cmf_enc_code2)/sizeof(u32),
			.b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
		},
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
			.b.fdma_conf_enc_dec_sel = FDMA_CONF_ENC,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_enc_init = 1,
			.b.fdma_conf_cmf_sel = 1,
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_enc_rdy = 1,

		},
	},
	/*! 4KB DU decoder CMF2 */
	{
		.addr = (u32)cmf_dec_code2,
		.size = {
			.b.fdma_conf_xfer_sz = sizeof(cmf_dec_code2)/sizeof(u32),
			.b.fdma_conf_rels_type = FDMA_DTAG_QUEUE_DROP,
		},
		.cfg = {
			.b.fdma_conf_mode = FDMA_CONF_CMF_MODE,
			.b.fdma_conf_enc_dec_sel = FDMA_CONF_DEC,
			.b.fdma_conf_rd_wrb = FDMA_CONF_WRITE,
			.b.fdma_conf_dec_init = 1,
			.b.fdma_conf_cmf_sel = 1,
		},
		.rdy = {
			.b.fdma_conf_xfer_rd_finish = 1,
			.b.fdma_conf_dec_rdy = 1,
		},
	},
#endif
};

/*!
 * @brief ECCU FDMA transfer function
 *
 * @param[in] xfer		point to a FDMA transfer config type
 *
 * @return Not used
 */
norm_ps_code void eccu_fdma_xfer(struct fdma_cfg_group* xfer,bool power_down) //CMF Reload workaround _GENE_20201119
{
	eccu_fdma_cfg_reg_t cfg_reg;
	eccu_writel(xfer->addr, ECCU_FDMA_CFG_ADDR_REG);
	eccu_writel(xfer->size.all, ECCU_FDMA_CFG_XFSZ_REG);

    if(power_down)
    {
        cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
        ncb_eccu_trace(LOG_INFO, 0xefc9, "CMF switch protect dec sel:%d, mode:%d->%d", cfg_reg.b.fdma_conf_enc_dec_sel, cfg_reg.b.fdma_conf_mode, xfer->cfg.b.fdma_conf_mode);
        pll1_power(true);
    }
	eccu_writel(xfer->cfg.all & ~(FDMA_CONF_DEC_INIT_MASK | FDMA_CONF_ENC_INIT_MASK | FDMA_CONF_SCR_LUT_INIT_MASK), ECCU_FDMA_CFG_REG);

    if (power_down) {
        pll1_power(false); //turn on PLL1 for ECCU/NF CLK after switch FDMA conf mode.
    }
    // Wait for a few CPU clock cycles.
	ndcu_delay(1);
	eccu_writel(xfer->cfg.all, ECCU_FDMA_CFG_REG);
	
#ifdef While_break
	u64 start = get_tsc_64();
#endif	
	// Wait for a few CPU clock cycles to finish synchronization of ?�FDMA_CONF_XFER_RD_FINISH??and ?�FDMA_CONF_ENC_RDY??	ndcu_delay(1);
	do {
		cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
		
#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
	} while ((cfg_reg.all & xfer->rdy.all) != xfer->rdy.all);
}

/*!
 * @brief ECCU FDMA config selection
 *
 * @param[in] idx		FDMA config index
 *
 * @return Not used
 */
norm_ps_code void eccu_fdma_cfg(enum fdma_op idx,bool power_down) //norm_ps_code
{
	eccu_fdma_xfer(&fdma_cfg_table[idx],power_down);

	switch (fdma_cfg_table[idx].cfg.b.fdma_conf_mode) {
	case FDMA_CONF_ROM_SRC_LUT_MODE:
	case FDMA_CONF_SRC_LUT_MODE:
		break;
	case FDMA_CONF_CMF_MODE:
	case FDMA_CONF_ROM_CMF_MODE:
		break;
	default:
		sys_assert(0);
		break;
	}
}

/*!
 * @brief Restore CMF & SCR LUT during PMU resume
 *
 * @return Not used
 */
norm_ps_code void pmu_eccu_cmf_restore(void)
{
    eccu_fdma_cfg_reg_t cfg_reg;
	eccu_fdma_cfg(ROM_SCR_SEED_LUT_DL,false);
	eccu_fdma_cfg(ROM_ENC_CMF_DL,false); // Download 4KB ROM CMF ENC
	eccu_fdma_cfg(ROM_DEC_CMF_DL,false);

    eccu_fdma_cfg(FDMA_ENC_CMF1_DL,true); // Download 4KB FDMA CMF ENC
	eccu_fdma_cfg(FDMA_DEC_CMF1_DL,false);
/*
#if ENABLE_2_CMF
	if (eccu_cfg.b.cmf_1) {
		eccu_fdma_cfg(FDMA_ENC_CMF2_DL);
		eccu_fdma_cfg(FDMA_DEC_CMF2_DL);
	}
#endif*/
    cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
    ncb_eccu_trace(LOG_INFO, 0x6fe0, "CMF switch protect dec sel:%d, mode:%d->%d\n", cfg_reg.b.fdma_conf_enc_dec_sel, cfg_reg.b.fdma_conf_mode, FDMA_CONF_NORMAL_MODE);

    pll1_power(true); //turn off PLL1
	cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
	cfg_reg.b.fdma_conf_mode = FDMA_CONF_NORMAL_MODE;
	eccu_writel(cfg_reg.all, ECCU_FDMA_CFG_REG);
    pll1_power(false); //turn on PLL1

}

/*!
 * @brief Reset ECCU HW status to default value
 *
 * @return Not used
 */
norm_ps_code void eccu_hw_reset(void)
{
	fdma_ctrl_reg0_t fdma_ctrl;
	fdma_ctrl.all = eccu_readl(FDMA_CTRL_REG0);
	fdma_ctrl.b.fdma_reset = 1;
	eccu_writel(fdma_ctrl.all, FDMA_CTRL_REG0);
	fdma_ctrl.b.fdma_reset = 0;
	eccu_writel(fdma_ctrl.all, FDMA_CTRL_REG0);
	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.eccu_soft_reset = 1;
	eccu_ctrl.b.dec_ard_halt_on_err = 0;
	eccu_ctrl.b.dec_halt_on_err = 0;
	eccu_ctrl.b.enc_halt_on_err = 0;
	eccu_ctrl.b.dec_err_rpt_mode = 1;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
	eccu_ctrl.b.eccu_soft_reset = 0;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
}

/*!
 * @brief Initialization ECCU module
 *
 * @return Not used
 */
init_code void eccu_hw_init(u8 otf_sel)
{
	// FDMA mode settings
	fdma_ctrl_reg0_t reg0;
	reg0.all = eccu_readl(FDMA_CTRL_REG0);
	reg0.b.fdma_du_sz_sel = (NAND_DU_SIZE == 4096) ? 0 : 1;
#if defined(DISABLE_HS_CRC_SUPPORT)
	reg0.b.fdma_scrc_en = 0;
#else
	reg0.b.fdma_scrc_en = 1;
#endif
	eccu_writel(reg0.all, FDMA_CTRL_REG0);

	// OTF decoder settings
	dec_top_ctrs_reg0_t dtop_ctr;
	dtop_ctr.all = dec_top_readl(DEC_TOP_CTRS_REG0);
	//  PDEC followed by MDEC (if MDEC fails)
	dtop_ctr.b.eccu_otf_dec_mode = otf_sel;
	dtop_ctr.b.eccu_mdec_dis = 0;
	dec_top_writel(dtop_ctr.all, DEC_TOP_CTRS_REG0);

	// ARD decoder settings
	dec_top_ctrs_reg1_t dtop_reg1;
	dtop_reg1.all = dec_top_readl(DEC_TOP_CTRS_REG1);
#if 0
    if (eccu_cfg.b.pdec) {
		dtop_reg1.b.eccu_ard_dec_mode = ARD_MODE_PDEC;// PDEC only
	} else {
		dtop_reg1.b.eccu_ard_dec_mode = ARD_MODE_MDEC;// MDEC only
	}
#else
    #if defined(EH_ENABLE_2BIT_RETRY)
    dtop_reg1.b.eccu_ard_dec_mode = ARD_MODE_PDEC;
	dtop_reg1.b.eccu_ard_data_fmt = 1;// does not support soft bit //0 = 1HD, 1 = 1HD+1SD, 2 = 1HD+1SD+1SD, It's safe to switch ARD mode when there is no outstanding IO
    #else
    dtop_reg1.b.eccu_ard_dec_mode = ARD_MODE_MDEC; // MDEC only //tony 20200818
	dtop_reg1.b.eccu_ard_data_fmt = 0;// does not support soft bit //0 = 1HD, 1 = 1HD+1SD, 2 = 1HD+1SD+1SD, It's safe to switch ARD mode when there is no outstanding IO
    #endif
#endif
	dtop_reg1.b.eccu_ard_max_num_ch = 6; //tony 20200907  //sylas 20230912 IG SDK code settings
	dec_top_writel(dtop_reg1.all, DEC_TOP_CTRS_REG1);

#ifdef EH_ENABLE_2BIT_RETRY
    // Must enable halt on error, otherwise ARD will hang
	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.dec_halt_on_err = 1;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
#endif

#if NCL_HAVE_ERD
	// ERD decoder settings
	extern void ncl_erd_hw_init(void);
	ncl_erd_hw_init();
#endif

	// HLBA settings
	eccu_ctrl_reg5_t eccu_ctrl_reg5;
	eccu_ctrl_reg5.all = eccu_readl(ECCU_CTRL_REG5);
	eccu_ctrl_reg5.b.hlba_src = 0; // 0: meta, 1: hlba fifo (not supported yet)
#if defined(DISABLE_HS_CRC_SUPPORT)
	eccu_ctrl_reg5.b.hlba_loc_start = 0;
#else
	eccu_ctrl_reg5.b.hlba_loc_start = offsetof(struct du_meta_fmt, hlba);
#endif
	eccu_ctrl_reg5.b.hlba_sz = 1; // 1: 4 bytes, 2: 8 bytes
#if DU_PADDING
	eccu_ctrl_reg5.b.enc_force_phy_pad_en = 1; // Enable DU padding
	eccu_ctrl_reg5.b.dec_force_phy_pad_en = 1;
#endif
	eccu_writel(eccu_ctrl_reg5.all,ECCU_CTRL_REG5);

#if 0
	mdec_ctrs_reg4_t mdec_reg4;
	mdec_reg4.all = mdec_readl(MDEC_CTRS_REG4);
	//  clean MDEC error
	mdec_reg4.b.eccu_mdec_force_cw_fail = 0x0;
	mdec_writel(mdec_reg4.all, MDEC_CTRS_REG4);
#endif

#ifdef LJ1_WUNC
	u32 loc = offsetof(struct du_meta_fmt, wunc.WUNC);
	u32 mask = 0xFF;

	eccu_partial_du_cfg(true, loc / sizeof(u32)); // dword location
	eccu_partial_du_mask(mask << ((loc % 4)  * 8));
#endif

    //NCL_FW_ARD 
    //USE FW_ARD
    eccu_ctrl_reg1_t eccu_reg;
	eccu_reg.all = eccu_readl(ECCU_CTRL_REG1);
	eccu_reg.b.dec_fw_ard_en = 1;
	eccu_writel(eccu_reg.all, ECCU_CTRL_REG1);
}

/*!
 * @brief Configure DU format register 0/1/2
 *
 * @param[in] idx		DU fomrat index
 *
 * @return Not used
 */
norm_ps_code void eccu_du_fmt_cfg(int idx)
{
	struct du_format_t *fmt;
	eccu_du_fmt_sel_reg_t fmt_sel;

	// Configure HW setting
	fmt_sel.all = eccu_readl(ECCU_DU_FMT_SEL_REG);
	fmt_sel.b.du_fmt_cfg_idx = idx;
	eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);

	fmt = &du_fmt_tbl[idx];

	eccu_writel(fmt->fmt0.all, ECCU_DU_FMT_REG0);
	eccu_writel(fmt->fmt1.all, ECCU_DU_FMT_REG1);
    
    if (idx == DU_FMT_USER_4K_PATROL_READ) {
	    eccu_writel(ECCU_PATROL_READ_THD, ECCU_DU_FMT_REG3);
    } else {
        eccu_writel(ECCU_OVRLMT_THD, ECCU_DU_FMT_REG3);
    }
}

/*!
 * @brief Configure DU format overlimit threshold
 *
 * @param[in] idx		DU fomrat index
 * @param[in] thd		threshold
 *
 * @return Not used
 */
norm_ps_code void eccu_cfg_ovrlmt_thd(int idx, u32 thd)
{
	eccu_du_fmt_sel_reg_t fmt_sel;
	eccu_du_fmt_reg3_t fmt_reg;

	fmt_sel.all = eccu_readl(ECCU_DU_FMT_SEL_REG);
	fmt_sel.b.du_fmt_cfg_idx = idx;
	eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);

	fmt_reg.all = eccu_readl(ECCU_DU_FMT_REG3);
	fmt_reg.b.cw_err_ovrlmt_thd = thd;
	eccu_writel(fmt_reg.all, ECCU_DU_FMT_REG3);
}

/*!
 * @brief SW initialize DU format info, should be used after eccu_du_fmt_cfg
 *
 * @param[in] fmt_id		DU fomrat ID
 *
 * @return Not used
 */
ddr_code void eccu_du_fmt_init(int fmt_id)
{
	struct du_format_t *fmt;
	u16 usr_sz, ecc_sz, par_sz;


	fmt = &du_fmt_tbl[fmt_id];

	fmt->fmt4.all = eccu_readl(ECCU_DU_FMT_REG4);
	fmt->fmt5.all = eccu_readl(ECCU_DU_FMT_REG5);

	usr_sz = fmt->fmt0.b.meta_sz;
	if (fmt->fmt1.b.meta_crc_en) {
		usr_sz += 4;
	}
	usr_sz += fmt->fmt0.b.host_sector_sz * fmt->fmt0.b.du2host_ratio;
	eccu_ctrl_reg5_t ctrl_reg;
	ctrl_reg.all = eccu_readl(ECCU_CTRL_REG5);
	if (ctrl_reg.b.hlba_src) {
		switch (fmt->fmt0.b.hlba_mode) {
		case 0:
			break;
		case 1:
			usr_sz += fmt->fmt0.b.hlba_sz_20*4;
			break;
		case 2:
			usr_sz += fmt->fmt0.b.hlba_sz_20 * 4 * fmt->fmt0.b.du2host_ratio;
			break;
		default:
			sys_assert(0);
			break;
		}
	}
	if (fmt->fmt1.b.du_crc_en) {
		if (fmt->fmt1.b.du_crc_6b_en) {
			usr_sz += 6;
		} else {
			usr_sz += 4;
		}
	}

	eccu_fdma_cfg_reg_t fdma_reg;
	fdma_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
	fdma_reg.b.fdma_conf_cmf_sel = fmt->fmt1.b.ecc_cmf_sel;
	eccu_writel(fdma_reg.all, ECCU_FDMA_CFG_REG);
	fmt->fmt6.all = eccu_readl(ECCU_DU_FMT_REG6);
	par_sz = fmt->fmt6.b.ecc_max_par_sz;
	if (fmt->fmt1.b.ecc_bypass == 0x3) {
		par_sz = 0;
	} else if (!par_sz) {
		par_sz = 256;//Encoder 03 nand_get_cmf_parity_size();
	}
	ecc_sz = par_sz * fmt->fmt1.b.cw_num_du;
	fmt->encoded_ecc_du_sz = usr_sz + ecc_sz;
	fmt->encoded_ecc_du_sz_2align =
		round_up_by_2_power(fmt->encoded_ecc_du_sz, 2);

	ncb_eccu_trace(LOG_INFO, 0x0811, "DU fmt %d size 0x%x", fmt_id, fmt->encoded_ecc_du_sz_2align);
	if (fmt->encoded_ecc_du_sz_2align * DU_CNT_PER_PAGE > nand_whole_page_size()) {
		if ((fmt_id != 0) && (fmt_id != 3)) {
			ncb_eccu_trace(LOG_INFO, 0xac35, "DU %d sz error, pg size: %x", fmt_id,
			nand_whole_page_size());
			//sys_assert(0);
		}
	}
	fmt->fmt2.all = eccu_readl(ECCU_DU_FMT_REG2);
#if DU_PADDING
	if (fmt_id >= DU_4K_DEFAULT_MODE) {
		fmt->fmt2.b.phy_pad_sz = (nand_whole_page_size() / DU_CNT_PER_PAGE) - fmt->encoded_ecc_du_sz;
	} else {
		fmt->fmt2.b.phy_pad_sz = 0;
	}
#else
	fmt->fmt2.b.phy_pad_sz = 0;
#endif
	fmt->fmt2.b.erase_det_thd = 0xFF;		// Temp set maximum
	eccu_writel(fmt->fmt2.all, ECCU_DU_FMT_REG2);
    ncb_eccu_trace(LOG_INFO, 0xd0f2, "Init End");
}

/*!
 * @brief Switch DU format type
 *
 * @param[in] idx		DU fomrat index
 *
 * @return Not used
 */
fast_code void eccu_switch_du_fmt(enum du_fmt_t idx)
{
	if (cur_rom_cmf ^ (idx == DU_FMT_MR_4K)) {// Check CMF mismatch
		cur_rom_cmf = !cur_rom_cmf;
        #ifndef NORELOAD_CMF
		if (cur_rom_cmf) {// Load ROM 2KB DU CMF
#if !HAS_ROM
			sys_assert(0);
#endif
			eccu_fdma_cfg(ROM_ENC_CMF_DL,false);
			eccu_fdma_cfg(ROM_DEC_CMF_DL,false);
			ficu_set_ard_du_size(du_fmt_tbl[DU_FMT_MR_4K].encoded_ecc_du_sz_2align);
		} else {// Load FDMA 4KB DU CMF
			eccu_fdma_cfg(FDMA_ENC_CMF1_DL,false);
			eccu_fdma_cfg(FDMA_DEC_CMF1_DL,false);
			ficu_set_ard_du_size(du_fmt_tbl[DU_FMT_USER_4K].encoded_ecc_du_sz_2align);
		}

        pll1_power(true);
		eccu_fdma_cfg_reg_t cfg_reg;
		cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
		cfg_reg.b.fdma_conf_mode = FDMA_CONF_NORMAL_MODE;
		eccu_writel(cfg_reg.all, ECCU_FDMA_CFG_REG);
        pll1_power(false);

		ficu_mode_disable();
		ficu_mode_enable();
        #endif
	}
}


#if defined(HMETA_SIZE)
ddr_code enum cmf_idx_t get_cur_cmf(void)
{
	eccu_du_fmt_sel_reg_t fmt_sel;
    eccu_du_fmt_reg5_t reg5;
    enum cmf_idx_t cur_cmf_idx;

	fmt_sel.all = eccu_readl(ECCU_DU_FMT_SEL_REG);
	fmt_sel.b.du_fmt_cfg_idx = DU_FMT_MR_4K;
	eccu_writel(fmt_sel.all, ECCU_DU_FMT_SEL_REG);
    
    reg5.all = eccu_readl(ECCU_DU_FMT_REG5);
    switch (reg5.b.eccu_tot_xfcnt) {
        case 0x1288:
            cur_cmf_idx = LDPC_CMF_MR;
            break;
        case 0x11a8:
            cur_cmf_idx = LDPC_CMF_62;
            break;
        case 0x11d8:
            cur_cmf_idx = LDPC_CMF_291;
            break;
        default:
            // sholud not be here
            cur_cmf_idx = LDPC_CMF_MAX;
            break;
    }
    return cur_cmf_idx;
}

ddr_code void eccu_switch_cmf(enum cmf_idx_t idx)
{	
	ncb_eccu_trace(LOG_INFO, 0xaf48, " cmf idx = %d", idx);

	if ((idx == LDPC_CMF_61_512) || (idx == LDPC_CMF_61_4K))
		host_du_fmt = DU_FMT_USER_4K;
	else if ((idx == LDPC_CMF_62) || (idx == LDPC_CMF_291))
		host_du_fmt = DU_FMT_USER_4K_HMETA;

    enum cmf_idx_t cur_cmf_idx = get_cur_cmf();
    if (cur_cmf_idx == idx) {
        ncb_eccu_trace(LOG_INFO, 0xb65b, "same as cur cmf %d", cur_cmf_idx);
        return;
    }

	while(!IS_NCL_IDLE())
	{
		ficu_done_wait()
	}

    switch (idx) {
        case LDPC_CMF_MR:
            eccu_fdma_cfg(ROM_ENC_CMF_DL,false);
            eccu_fdma_cfg(ROM_DEC_CMF_DL,false);
            break;
        case LDPC_CMF_61_512:
        case LDPC_CMF_61_4K:
        #if 0
            eccu_fdma_cfg(FDMA_ENC_CMF1_DL,false);
            eccu_fdma_cfg(FDMA_DEC_CMF1_DL,false);
            break;
        #else   // Keep Code 61 in CMF1,
            return;
        #endif
        case LDPC_CMF_62:
            eccu_fdma_cfg(FDMA_ENC_CMF62_DL,false);
            eccu_fdma_cfg(FDMA_DEC_CMF62_DL,false);
            break;
        case LDPC_CMF_291:
            eccu_fdma_cfg(FDMA_ENC_CMFFFFF_DL,false);
            eccu_fdma_cfg(FDMA_ENC_CMF291_DL,false);
            eccu_fdma_cfg(FDMA_DEC_CMF291_DL,false);
            break;
        default:
            return;
    }
    
    pll1_power(true);
    eccu_fdma_cfg_reg_t cfg_reg;
    cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
    cfg_reg.b.fdma_conf_mode = FDMA_CONF_NORMAL_MODE;
    eccu_writel(cfg_reg.all, ECCU_FDMA_CFG_REG);
    pll1_power(false);
    
    ficu_mode_disable();
    ficu_mode_enable();
}

ddr_code void eccu_dufmt_switch(enum cmf_idx_t idx) {
	u8 fmt_idx;
	ncb_eccu_trace(LOG_INFO, 0x4b04, " dufmt idx = %d", idx);

	while(!IS_NCL_IDLE())
	{
		cpu_msg_isr();
		ficu_done_wait()
	}

    enum cmf_idx_t cur_cmf_idx = get_cur_cmf();
    if (cur_cmf_idx == idx) {
        ncb_eccu_trace(LOG_INFO, 0x030b, "same as cur cmf %d", cur_cmf_idx);
        return;
    }
    
    switch (idx) {
        case LDPC_CMF_MR:
            return;
        case LDPC_CMF_61_512:
            fmt_idx = DU_FMT_USER_4K;

            // for patrol read
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.host_sector_sz = 512;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.du2host_ratio	= NAND_DU_SIZE / 512;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.pi_sz = 0;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.ecc_cmf_sel = 0;
 
            du_fmt_tbl[DU_FMT_USER_4K].fmt0.b.host_sector_sz = 512;
            du_fmt_tbl[DU_FMT_USER_4K].fmt0.b.du2host_ratio	= NAND_DU_SIZE / 512;
            du_fmt_tbl[DU_FMT_USER_4K].fmt1.b.pi_sz = 0;
            break;
        case LDPC_CMF_61_4K:
            fmt_idx = DU_FMT_USER_4K;

            // for patrol read
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.host_sector_sz = 4096;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.du2host_ratio	= NAND_DU_SIZE / 4096;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.pi_sz = 0;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.ecc_cmf_sel = 0;
            
            du_fmt_tbl[DU_FMT_USER_4K].fmt0.b.host_sector_sz = 4096;
            du_fmt_tbl[DU_FMT_USER_4K].fmt0.b.du2host_ratio	= NAND_DU_SIZE / 4096;
            du_fmt_tbl[DU_FMT_USER_4K].fmt1.b.pi_sz = 0;
            break;
        case LDPC_CMF_62:
            fmt_idx = DU_FMT_USER_4K_HMETA;
            
            // for patrol read
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.host_sector_sz = 512 + 8;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.du2host_ratio	= NAND_DU_SIZE / 512;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.pi_sz = 1;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.ecc_cmf_sel = 1;
            
            du_fmt_tbl[DU_FMT_USER_4K_HMETA].fmt0.b.host_sector_sz = 512 + 8;
            du_fmt_tbl[DU_FMT_USER_4K_HMETA].fmt0.b.du2host_ratio = NAND_DU_SIZE / 512;
            du_fmt_tbl[DU_FMT_USER_4K_HMETA].fmt1.b.pi_sz = 1;
            break;
        case LDPC_CMF_291:
            fmt_idx = DU_FMT_USER_4K_HMETA;
            
            // for patrol read
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.host_sector_sz = 4096 + 8;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt0.b.du2host_ratio	= NAND_DU_SIZE / 4096;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.pi_sz = 1;
            du_fmt_tbl[DU_FMT_USER_4K_PATROL_READ].fmt1.b.ecc_cmf_sel = 1;

            du_fmt_tbl[DU_FMT_USER_4K_HMETA].fmt0.b.host_sector_sz = 4096 + 8;
            du_fmt_tbl[DU_FMT_USER_4K_HMETA].fmt0.b.du2host_ratio = NAND_DU_SIZE / 4096;
            du_fmt_tbl[DU_FMT_USER_4K_HMETA].fmt1.b.pi_sz = 1;
            break;
        default:
            return;
    }

    eccu_du_fmt_cfg(DU_FMT_USER_4K_PATROL_READ);
    eccu_du_fmt_init(DU_FMT_USER_4K_PATROL_READ);
    eccu_du_fmt_cfg(fmt_idx);
    eccu_du_fmt_init(fmt_idx);
	ncb_eccu_trace(LOG_INFO, 0x871a, "eccu_dufmt_switch");
}
#endif

/*!
 * @brief Judge now CMF is ROM or not
 *
 * @return value(true or false)
 */
fast_code bool eccu_cmf_is_rom(void)
{
	return cur_rom_cmf;
}

#if defined(FAST_MODE)
fast_code void eccu_fast_mode(bool enable)
{
	eccu_ctrl_reg1_t reg1;

	reg1.all = eccu_readl(ECCU_CTRL_REG1);
	reg1.b.eccu_fast_access_en = enable;
	eccu_writel(reg1.all, ECCU_CTRL_REG1);
}

init_code void eccu_fast_mode_init(u8 nr_ch)
{
	if ((nr_ch != 2) && (nr_ch != 4) && (nr_ch != 8)) {
		ncb_eccu_trace(LOG_INFO, 0x61c3, "FAST mode can't support (%d) physical channels", nr_ch);
		sys_assert(0);
	}

	eccu_ctrl_reg1_t reg1;

	reg1.all = eccu_readl(ECCU_CTRL_REG1);
	reg1.b.eccu_fast_access_en = 1;
	reg1.b.eccu_fast_access_mode = ctz(nr_ch);
	eccu_writel(reg1.all, ECCU_CTRL_REG1);
	ncb_eccu_trace(LOG_INFO, 0x5004, "FAST mode enabled for ECCU, (%d) physical channels", nr_ch);
}
#endif

#if defined(HMB_DTAG)
init_code void eccu_hmb_en(void)
{
	eccu_ctrl_reg1_t reg1 = {.all = eccu_readl(ECCU_CTRL_REG1),};
	reg1.b.eccu_hmb_en = 1;
	eccu_writel(reg1.all, ECCU_CTRL_REG1);
}
#endif

/*!
 * @brief ECCU config type selection(0/1/2)
 *
 * @return value(type)
 */
init_code u8 eccu_conf_sel(void)
{
	u8 otf_sel = DEC_MODE_MDEC_AND_PDEC;
	eccu_conf_reg_t cfg = { .all = eccu_readl(ECCU_CONF_REG), };

	// ncb_eccu_trace(LOG_ERR, 0, "cfg %x: ch/ce %d/%d, mdec0/1 %d/%d, pdec %d, ldpc %d, "
	//        "bch %d, cmf0/1 %d/%d, ncb0/1 %d/%d\n",
	//        cfg.all, cfg.b.ch_num, cfg.b.ce_num,
	//        cfg.b.mdec_0, cfg.b.mdec_1, cfg.b.pdec, cfg.b.ldpc_enc,
	//        cfg.b.bch, cfg.b.cmf_0, cfg.b.cmf_1,
	//        cfg.b.ncb_0, cfg.b.ncb_1);

	eccu_cfg.all = cfg.all;
#if defined(FAST_MODE)
	sys_assert(cfg.b.bch);
#else
	if (cfg.b.ldpc_enc && cfg.b.pdec == 0) {
		otf_sel = DEC_MODE_MDEC;
	}
#endif
	return otf_sel;
}

/*!
 * @brief ECCU module initialization
 *
 * @return Not used
 */
init_code void eccu_init(void)
{
	int cmf_nr = 1;
	u8 otf_sel = eccu_conf_sel();
	eccu_hw_reset();
	eccu_hw_init(otf_sel);
	sys_assert(DU_CNT_PER_PAGE * NAND_DU_SIZE < nand_whole_page_size());

/*
#if HAS_ROM
	// Download 4KB ROM CMF
	eccu_fdma_cfg(ROM_ENC_CMF_DL);
	eccu_fdma_cfg(ROM_DEC_CMF_DL);
	eccu_fdma_cfg(ROM_SCR_SEED_LUT_DL);

#endif

	//  Download 4KB FDMA CMF
	eccu_fdma_cfg(FDMA_ENC_CMF1_DL);
	eccu_fdma_cfg(FDMA_DEC_CMF1_DL);
#if ENABLE_2_CMF
	if (eccu_cfg.b.cmf_1) {
		eccu_fdma_cfg(FDMA_ENC_CMF2_DL);
		eccu_fdma_cfg(FDMA_DEC_CMF2_DL);
		cmf_nr ++;
	}
#endif
*/
    pmu_eccu_cmf_restore();
	// Initialize 4KB DU format
	eccu_du_fmt_cfg(DU_FMT_MR_4K);
	eccu_du_fmt_init(DU_FMT_MR_4K);
    
    //enable raw 1 bit cnt for vth tracking
    eccu_ctrl_reg5_t eccu_ctrl_reg5;
	eccu_ctrl_reg5.all = eccu_readl(ECCU_CTRL_REG5);    
    eccu_ctrl_reg5.all &= 0xFFF0FFFF;
    eccu_ctrl_reg5.all |= 0x20000; //DEC_TAG_NUM_MAX
	eccu_writel(eccu_ctrl_reg5.all,ECCU_CTRL_REG5);
    
    dec_01_accu_reg0_t dec_01_accu_reg0;
    dec_01_accu_reg0.all = dec_top_readl(DEC_01_ACCU_REG0);
    dec_01_accu_reg0.all |= 0x104; //DEC_DU_01_ACCUM_EN & DEC_01_ACCU_SEL
    dec_top_writel(dec_01_accu_reg0.all, DEC_01_ACCU_REG0);
    //end for enable raw 1 bit cnt

	int idx;

	// Skip 2K MR becuase CMF doesn't match which is for 4K.
	for (idx = DU_FMT_MR_4K+1; idx < DU_FMT_COUNT; idx++) {
		eccu_du_fmt_cfg(idx);
		// Update SW setting
		if(idx == DU_FMT_USER_4K_PATROL_READ)
        {
		    eccu_cfg_ovrlmt_thd(idx, ECCU_PATROL_READ_THD);
        }
        eccu_du_fmt_init(idx);
	}

	// Download scrambler seed LUT
	eccu_fdma_cfg(SCR_SEED_LUT_DL,false);
	eccu_fdma_cfg_reg_t cfg_reg;
	cfg_reg.all = eccu_readl(ECCU_FDMA_CFG_REG);
	cfg_reg.b.fdma_conf_mode = FDMA_CONF_NORMAL_MODE;
	eccu_writel(cfg_reg.all, ECCU_FDMA_CFG_REG);

#if defined(FAST_MODE)
	eccu_fast_mode_init(max_channel);
#endif

#if defined(HMB_DTAG)
	eccu_hmb_en();
#endif

#if DEBUG
	dec_top_ctrs_reg0_t reg0;
	dec_top_ctrs_reg1_t reg1;
	dec_top_ctrs_reg2_t reg2;

	reg0.all = dec_top_readl(DEC_TOP_CTRS_REG0);
	reg1.all = dec_top_readl(DEC_TOP_CTRS_REG1);
	reg2.all = dec_top_readl(DEC_TOP_CTRS_REG2);
	ncb_eccu_trace(LOG_DEBUG, 0xd1a7, "ECCU Nrml/ARD/ERD: %s/%s/%s" \
				, (u32)((reg0.b.eccu_otf_dec_mode == 0) ? "M" : ((reg0.b.eccu_otf_dec_mode == 1) ? "P" : "(M+P)"))\
				, (u32)(reg1.b.eccu_ard_dec_mode ? "P" : "M")\
				, (u32)(reg2.b.eccu_erd_dec_sel ? "P" : "M"));
	
#endif

	// 20210514 Jamie, modify pdec max iteration from 64 -> 48
	pdec_ctrs_reg3_t pdec_reg;
	pdec_reg.all = pdec_readl(PDEC_CTRS_REG3);
	pdec_reg.b.eccu_pdec_max_iter_cnt = 48;
	pdec_writel(pdec_reg.all, PDEC_CTRS_REG3);

	ncb_eccu_trace(LOG_INFO, 0x0ae7, "ECCU init done(%d CMF)", cmf_nr);
}

/*!
 * @brief Get the ECCU decoder error type
 *
 * @return enum value(error type no)
 */
fast_code enum ncb_err eccu_dec_err_get(u16* cnt1) //cnt1 for vth tracking
{
	eccu_dec_status_reg0_t dec_status;
	dec_status.all = eccu_readl(ECCU_DEC_STATUS_REG0);
    //ncb_eccu_trace(LOG_NCL_ERR, 0, "[DBG] eccu_dec_status_reg0: 0x%x", dec_status.all);   //tony 20201215

	if (dec_status.b.cur_du_erase) {
		ncb_eccu_trace(LOG_DEBUG, 0x2c7e, "rd erased err");
		return cur_du_erase;
	} else if (dec_status.b.cur_du_ucerr) {
		ncb_eccu_trace(LOG_DEBUG, 0xa687, "rd decoding err");
         (*cnt1)=(u16)dec_status.b.cur_du_accum_raw_01_cnt; //raw 1 bit cnt for vth tracking
		return cur_du_ucerr;
    } else if (dec_status.b.cur_du_ppu_err_48) {
        ncb_eccu_trace(LOG_DEBUG, 0xfeaa, "rd ppu err");
        return cur_du_ppu_err;
	} else if (dec_status.b.cur_du_dfu_err_48) {
		ncb_eccu_trace(LOG_DEBUG, 0xf7e0, "rd dfu err");
         (*cnt1)=(u16)dec_status.b.cur_du_accum_raw_01_cnt; //raw 1 bit cnt for vth tracking
		return cur_du_dfu_err;
	} else if (dec_status.b.cur_du_raid_err_48) {
		ncb_eccu_trace(LOG_DEBUG, 0x18ed, "rd raid err");
		return cur_du_raid_err;
	} else if (dec_status.b.cur_du_partial_du_det_err) {
		ncb_eccu_trace(LOG_DEBUG, 0x8464, "rd partial err");
		return cur_du_partial_err;
	} else if (dec_status.b.cur_du_ovrlmt_err) {
		ncb_eccu_trace(LOG_DEBUG, 0x27b2, "rd ovlmt err");
		return cur_du_ovrlmt_err;
	} else {
		ncb_eccu_trace(LOG_NCL_ERR, 0xf2f6, "rd ncb err %x", dec_status.all);
		return unknown_err;
	}
}

/*!
 * @brief Get the ECCU encoder error type
 *
 * @return enum value(error type no)
 */
fast_code enum ncb_err eccu_enc_err_get(void)
{
	eccu_enc_status_reg0_t enc_status;
	enc_status.all = eccu_readl(ECCU_ENC_STATUS_REG0);
	ncb_eccu_trace(LOG_NCL_ERR, 0x3a86, "enc err %x", enc_status.all);

	if (enc_status.b.cur_du_enc_err) {
		//ncb_eccu_trace(LOG_DEBUG, 0, "wr encoder err");
		return cur_du_enc_err;
	} else if (enc_status.b.cur_du_ppu_err) {
	
        extern tencnet_smart_statistics_t *tx_smart_stat; ///< controller Tencent SMART statistics
        tx_smart_stat->hcrc_error_count[1]++;
		ncb_eccu_trace(LOG_DEBUG, 0xc5a2, "wr ppu err");
		return cur_du_ppu_err;
	} else if (enc_status.b.cur_du_dfu_err) {
		ncb_eccu_trace(LOG_DEBUG, 0x5879, "wr dfu err");
		return cur_du_dfu_err;
	} else if (enc_status.b.cur_du_raid_err) { 
		ncb_eccu_trace(LOG_DEBUG, 0x49f7, "wr raid err");
		return cur_du_raid_err; 
	} else {
		sys_assert(0);
		return unknown_err;
	}
}

/*!
 * @brief Resume the ECCU encoder
 *
 * @return Not used
 */
fast_code void resume_encoder(void)
{
	eccu_enc_status_reg0_t enc_status;
	enc_status.all = 0;
	enc_status.b.eccu_enc_resume = 1;
	eccu_writel(enc_status.all, ECCU_ENC_STATUS_REG0);
}

/*!
 * @brief Resume the ECCU decoder
 *
 * @return Not used
 */
fast_code void resume_decoder(void)
{
	eccu_dec_status_reg0_t dec_status;
	dec_status.all = eccu_readl(ECCU_DEC_STATUS_REG0);
	dec_status.b.eccu_dec_resume = 1;
	eccu_writel(dec_status.all, ECCU_DEC_STATUS_REG0);
}

/*!
 * @brief Set ECCU decoder mode
 *
 * @param[in] otf		decoder mode no
 *
 * @return Not used
 */
fast_code void eccu_set_dec_mode(int otf)
{
	dec_top_ctrs_reg0_t dtop_ctr;
	mdec_ctrs_reg4_t mdec_reg4;

	mdec_reg4.all = mdec_readl(MDEC_CTRS_REG4);
	// clean MDEC error
	mdec_reg4.b.eccu_mdec_force_cw_fail = 0x0;
	mdec_writel(mdec_reg4.all, MDEC_CTRS_REG4);

	dtop_ctr.all = dec_top_readl(DEC_TOP_CTRS_REG0);
	// PDEC followed by MDEC (if MDEC fails)
	dtop_ctr.b.eccu_otf_dec_mode = otf;
	dec_top_writel(dtop_ctr.all, DEC_TOP_CTRS_REG0);
}

/*!
 * @brief Set ECCU ARD decoder mode
 *
 * @param[in] dec		ARD decoder mode no
 *
 * @return Not used
 */
fast_code void eccu_set_ard_dec(u32 dec)
{
	dec_top_ctrs_reg1_t reg1;

	reg1.all = dec_top_readl(DEC_TOP_CTRS_REG1);
	reg1.b.eccu_ard_dec_mode = dec;

	dec_top_writel(reg1.all, DEC_TOP_CTRS_REG1);
}

/*!
 * @brief Set ECCU ERD decoder mode
 *
 * @param[in] dec		ERD decoder mode no
 *
 * @return Not used
 */
fast_code void eccu_set_erd_dec(u32 dec)
{
	dec_top_ctrs_reg2_t reg2;

	reg2.all = dec_top_readl(DEC_TOP_CTRS_REG2);
	reg2.b.eccu_erd_dec_sel = dec;

	dec_top_writel(reg2.all, DEC_TOP_CTRS_REG2);
}

/*!
 * @brief Get ECCU binding decoder CMF
 *
 * @param[in] size		CMF array size
 *
 * @return value(CMF code array)
 */
norm_ps_code u32 *eccu_get_binding_dec_cmf(u32 *size)
{
	*size = sizeof(cmf_dec_code1);
	return cmf_dec_code1;
}

/*!
 * @brief Get ECCU binding encoder CMF
 *
 * @param[in] size		CMF array size
 *
 * @return value(CMF code array)
 */
norm_ps_code u32 *eccu_get_binding_enc_cmf(u32 *size)
{
	*size = sizeof(cmf_enc_code1);
	return cmf_enc_code1;
}

/*!
 * @brief Set partial DU detect config
 *
 * @param[in] enable		Partial DU detect feature enable.
 * @param[in] loc_start		Start location for bits to be checked. (In dword)
 *
 * @return Not used
 */
norm_ps_code void eccu_partial_du_cfg(bool enable, u32 loc_start)
{
	eccu_partial_du_detect_reg0_t cfg_reg;
	cfg_reg.all = eccu_readl(ECCU_PARTIAL_DU_DETECT_REG0);
	cfg_reg.b.partial_du_detect_en = enable ? 1 : 0;
	cfg_reg.b.partial_du_detect_loc_start = loc_start;
	eccu_writel(cfg_reg.all, ECCU_PARTIAL_DU_DETECT_REG0);
}

/*!
 * @brief Set partial DU detect bit mask
 *
 * @param[in] bit_mask		Bit mask for all 32 bits to be checked.
 *
 * @return Not used
 */
norm_ps_code void eccu_partial_du_mask(u32 bit_mask)
{
	eccu_partial_du_detect_reg1_t mask_reg;
	mask_reg.b.partial_du_detect_bit_mask = bit_mask;
	eccu_writel(mask_reg.all, ECCU_PARTIAL_DU_DETECT_REG1);
}

#if 1 // Only used for validation

/*!
 * @brief Get encoder CMF size
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF size)
 */
fast_code u32 get_enc_cmf_size(u32 idx)
{
	if (idx == 0) {
		return sizeof(cmf_enc_code1);
	} else if (idx == 1) {
		return sizeof(cmf_enc_code2);
	}
	return 0;
}

/*!
 * @brief Get encoder CMF buffer
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF code array)
 */
fast_code u32* get_enc_cmf_buf(u32 idx)
{
	if (idx == 0) {
		return cmf_enc_code1;
	} else if (idx == 1) {
		return cmf_enc_code2;
	}
	return 0;
}

/*!
 * @brief Get decoder CMF size
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF size)
 */
fast_code u32 get_dec_cmf_size(u32 idx)
{
	if (idx == 0) {
		return sizeof(cmf_dec_code1);
	} else if (idx == 1) {
		return sizeof(cmf_dec_code2);
	}
	return 0;
}

/*!
 * @brief Get decoder CMF buffer
 *
 * @param[in] idx		CMF type index
 *
 * @return value(CMF code array)
 */
fast_code u32* get_dec_cmf_buf(u32 idx)
{
	if (idx == 0) {
		return cmf_dec_code1;
	} else if (idx == 1) {
		return cmf_dec_code2;
	}
	return 0;
}

/*!
 * @brief Get scramble seed size
 *
 * @return value(size)
 */
fast_code u32 get_scr_seed_size(void)
{
	return sizeof(seed_lut_buf);
}

/*!
 * @brief Get scramble seed buffer
 *
 * @return value(seed buffer array)
 */
fast_code u32* get_scr_seed_buf(void)
{
	return seed_lut_buf;
}

#endif
/*! @} */
