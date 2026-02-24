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

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "inc/ncb_ndcu_register.h"
#include "nand_cfg.h"
#include "misc.h"

typedef struct {		///< NDCU registers settings related to nand command
    /* 0x0060 */ nf_rcmd_reg00_t nf_rcmd_reg00;
    /* 0x0064 */ nf_rcmd_reg01_t nf_rcmd_reg01;
    /* 0x0068 */ nf_rcmd_reg10_t nf_rcmd_reg10;
    /* 0x006c */ nf_rcmd_reg11_t nf_rcmd_reg11;
    /* 0x0070 */ nf_rcmd_mp_reg00_t nf_rcmd_mp_reg00;
    /* 0x0074 */ nf_rcmd_mp_reg01_t nf_rcmd_mp_reg01;
    /* 0x0078 */ nf_rcmd_mp_reg10_t nf_rcmd_mp_reg10;
    /* 0x007c */ nf_rcmd_mp_reg11_t nf_rcmd_mp_reg11;
    /* 0x0080 */ nf_rxcmd_reg0_t nf_rxcmd_reg0;
    /* 0x0084 */ nf_rxcmd_reg1_t nf_rxcmd_reg1;
    /* 0x0088 */ nf_rxcmd_mp_reg0_t nf_rxcmd_mp_reg0;
    /* 0x008c */ nf_rxcmd_mp_reg1_t nf_rxcmd_mp_reg1;
    /* 0x0090 */ nf_rdcmd_dummy_t nf_rdcmd_dummy;
    /* 0x0094 */ nf_pcmd_reg00_t nf_pcmd_reg00;
    /* 0x0098 */ nf_pcmd_reg01_t nf_pcmd_reg01;
    /* 0x009c */ nf_pcmd_reg10_t nf_pcmd_reg10;
    /* 0x00a0 */ nf_pcmd_reg11_t nf_pcmd_reg11;
    /* 0x00a4 */ nf_pcmd_mp_reg00_t nf_pcmd_mp_reg00;
    /* 0x00a8 */ nf_pcmd_mp_reg01_t nf_pcmd_mp_reg01;
    /* 0x00ac */ nf_pcmd_mp_reg10_t nf_pcmd_mp_reg10;
    /* 0x00b0 */ nf_pcmd_mp_reg11_t nf_pcmd_mp_reg11;
    /* 0x00b4 */ nf_ecmd_reg0_t nf_ecmd_reg0;
    /* 0x00b8 */ nf_ecmd_reg1_t nf_ecmd_reg1;
    /* 0x00bc */ nf_ecmd_mp_reg0_t nf_ecmd_mp_reg0;
    /* 0x00c0 */ nf_ecmd_mp_reg1_t nf_ecmd_mp_reg1;
    /* 0x00c4 */ nf_ecmd_jdc0_t nf_ecmd_jdc0;
    /* 0x00c8 */ nf_scmd_reg0_t nf_scmd_reg0;
    /* 0x00cc */ nf_scmd_reg1_t nf_scmd_reg1;
    /* 0x00d0 */ nf_rstcmd_reg0_t nf_rstcmd_reg0;
    /* 0x00d4 */ nf_rstcmd_reg1_t nf_rstcmd_reg1;
    /* 0x00d8 */ nf_precmd_reg00_t nf_precmd_reg00;
    /* 0x00dc */ nf_precmd_reg01_t nf_precmd_reg01;
    /* 0x00e0 */ nf_precmd_reg10_t nf_precmd_reg10;
    /* 0x00e4 */ nf_precmd_reg11_t nf_precmd_reg11;
    /* 0x00e8 */ nf_precmd_reg20_t nf_precmd_reg20;
    /* 0x00ec */ nf_precmd_reg21_t nf_precmd_reg21;
} ncb_ndcu_cmd_regs_t;

typedef struct {		///< NDCU Ready/busy and pass/fail registers settings
    /* 0x0108 */ nf_fail_reg0_t nf_fail_reg0;
    /* 0x010c */ nf_fail_reg1_t nf_fail_reg1;
    /* 0x0110 */ nf_fail_reg2_t nf_fail_reg2;
    /* 0x0114 */ nf_fail_reg3_t nf_fail_reg3;
    /* 0x0118 */ nf_fail_reg4_t nf_fail_reg4;
    /* 0x0180 */ nf_fail_reg5_t nf_fail_reg5;
    /* 0x0184 */ nf_fail_reg6_t nf_fail_reg6;
    /* 0x0188 */ nf_fail_reg7_t nf_fail_reg7;
    /* 0x011c */ nf_rdy_reg0_t nf_rdy_reg0;
} ncb_ndcu_rdy_fail_regs_t;

typedef struct {		///< NDCU postfix registers settings (Samsung requires)
    /* 0x01f0 */ nf_pstcmd_reg00_t nf_pstcmd_reg00;
    /* 0x01f4 */ nf_pstcmd_reg01_t nf_pstcmd_reg01;
} ncb_ndcu_pstcmd_regs_t;

typedef struct {
	nf_ncmd_fmt_reg0_t	fmt;
	nf_ncmd_fmt_reg1_t	poll;
} ncmd_fmt_t;

// Must define these arrays for each vendor
extern ncb_ndcu_cmd_regs_t ncb_ndcu_cmd_regs;
extern ncb_ndcu_rdy_fail_regs_t ncb_ndcu_rdy_fail_regs;
extern nf_fail_reg_sp_t nf_fail_reg_sp;
extern ncmd_fmt_t ncmd_fmt_array[];

/*!
 * @brief NDCU initialization (including nand initialization)
 *
 * @return	not used
 */
void ndcu_init(void);

/*!
 * @brief Nand write protection enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
void ndcu_write_protection(bool enable);

/*!
 * @brief Enable register control mode
 *
 * @return	not used
 */
void ndcu_en_reg_control_mode(void);

/*!
 * @brief Disable register control mode
 *
 * @return	not used
 */
void ndcu_dis_reg_control_mode(void);

/*!
 * @brief Page parameter CRC calculation
 *
 * @param p	page parameter pointer
 * @param len	Length of page parameter (crc excluded)
 *
 * @return	not used
 */
u16 page_param_crc16(u8 const *p, u32 len);

/*!
 * @brief NDCU HW reset
 *
 * @return	not used
 */
void ndcu_hw_rst(void);

/*!
 * @brief NDCU registers init
 *
 * @return	not used
 */
void ndcu_hw_init(void);

/*!
 * @brief NDCU set interface timing mode
 *
 * @param intf	interface
 * @param tm	timing mode
 *
 * @return	not used
 */
void ndcu_set_tm(ndcu_if_mode intf, u32 tm);

/*!
 * @brief Nand command format configure
 *
 * @return	not used
 */
void ndcu_ndcmd_format_cfg(void);

/* in-direct access mode api */
typedef struct {		///< Register control mode operation parameter
	/* caller fill data */
	bool write;        /* 0: data from nand device, 1 data to nand device */
	u8 cmd_num, cle_mode;
	nf_ind_reg1_t reg1;
	nf_ind_reg2_t reg2;
	u16 xfcnt; /* byte */
	u8 *buf;

	/* calle's internal data */
	bool rdy_ctrl;
	int ind_ddr_en, ind_xfcnt_db_en;
	u16 cnt;  /* byte */
} ndcu_ind_t;

/*!
 * @brief Register control mode operation open
 *
 * @param ctrl	operation parameters
 * @param ch	channel
 * @param ce	target
 *
 * @return	0
 */
int ndcu_open (ndcu_ind_t *ctrl, int ch, int ce);

/*!
 * @brief Register control mode operation start
 *
 * @param ctrl	operation parameters
 *
 * @return	0
 */
int ndcu_start(ndcu_ind_t *ctrl);

/*!
 * @brief Register control mode operation data transfer
 *
 * @param ctrl	operation parameters
 *
 * @return	data transfer complete
 */
int ndcu_xfer (ndcu_ind_t *ctrl);

/*!
 * @brief Register control mode operation end
 *
 * @param ctrl	operation parameters
 *
 * @return	0
 */
int ndcu_close(ndcu_ind_t *ctrl);

/*!
 * @brief Set read status poll interval (1st wcnt and later pcnt)
 *
 * @return	not used
 */
void ndcu_set_poll_delay(void);

/*!
 * @brief Fast access mode enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
void ndcu_fast_mode(bool enable);

void delay(void);
void delay_us(u32 us);
#ifdef HAVE_SYSTEMC
static inline void ndcu_delay(u32 us) { };
#else
static inline void ndcu_delay(u32 us)
{
	mdelay(us);
}
#endif

enum { /* B16A, TLC Array Characteristics */
	tR    = 120, /* 120us */
	tFEAT = 1,   /* 1us   */
	tWHR  = 0,   /* 80ns  */
};

/*!
 * @brief Get low/middle/upper page type from nand command format
 * Used in error handling to know low, middle or upper page occurs error.
 * Because toggle nand low, middle, upper page has same row address
 *
 * @param ndcmd_fmt_idx	nand command format index
 *
 * @return	low, middle or upper page
 */
u8 ndcu_get_xlc_page_index(int ndcmd_fmt_idx);

/*!
 * @brief NDCU NF clock switch
 *
 * @param freq	New NF clock frequency
 *
 * @return	not used
 */
void ndcu_nf_clk_switch(u32 freq);
