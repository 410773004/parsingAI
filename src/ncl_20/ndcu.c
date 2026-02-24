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
/*! \file ndcu.c
 * @brief ndcu PHY configure
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * NDCU APIs
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "ncl.h"
#include "ndcu.h"
#include "nand.h"
#include "ndphy.h"
#include "nand_define.h"

/*! \cond PRIVATE */
#define __FILEID__ ndcu
#include "trace.h"
/*! \endcond */

norm_ps_data u32 nf_clk = 100;		///< NF clock frequency in MT/s unit

fast_code void delay(void)
{
	/* TODO: we should merge this to rtos layer.  */
#if !defined(HAVE_SYSTEMC) && !defined(HAVE_VELOCE)
	vu32 i;
	for (i = 0; i < 10000; i++) {
	}
#endif
}

/*!
 * @brief Delay in micro second
 *
 * @param us	micro second
 *
 * @return	not used
 */
fast_code void delay_us(u32 us)
{
	u32 t1, t2;
	t1 = get_tsc_lo();
	while(1) {
		t2 = get_tsc_lo();
#if FPGA
		if (((t2 - t1) / (SYS_CLK / 1000000)) > us) {
			return;
		}
#else
		extern int cur_cpu_clk_freq;
		if ((t2 - t1) > cur_cpu_clk_freq * us) {
			return;
		}
#endif
	}
}

/*!
 * @brief NDCU HW reset
 *
 * @return	not used
 */
norm_ps_code void ndcu_hw_rst(void)
{
	nf_ctrl_reg0_t reg0;
	nf_ind_reg0_t reg;

	ncb_ndcu_trace(LOG_DEBUG, 0x9a5e, "ndcu hw reset");
	reg0.all = ndcu_readl(NF_CTRL_REG0);
	reg0.b.nf_op_mode = 0;
	reg0.b.nf_tim_mode = 0;
	reg0.b.nf_reset = 1;
	ndcu_writel(reg0.all, NF_CTRL_REG0);
	delay();
	reg0.b.nf_reset = 0;
	ndcu_writel(reg0.all, NF_CTRL_REG0);
	delay();
	reg.all = ndcu_readl(NF_IND_REG0);
	reg.b.ind_fifo_reset = 1;
	reg.b.ind_ddr_reset = 1;
	ndcu_writel(reg.all, NF_IND_REG0);
	delay();
	reg.b.ind_fifo_reset = 0;
	reg.b.ind_ddr_reset = 0;
	ndcu_writel(reg.all, NF_IND_REG0);
	delay();
}

/* for onfi, we assume ndcu and nand in same mode */
fast_data_zi static ndcu_if_mode ndcu_intf_mode;

typedef struct {
	u16 nf_max_ddr :1;
	u16 nf_tim_mode:4;
	u16 nf_op_mode :4;
	u16 re2qs_sync:8;
	u16 re2qs_async:8;
} intf_map_t;

norm_ps_data static intf_map_t inf_map[] = {
	[NDCU_IF_SDR>>4]  = { .nf_max_ddr = 0, .nf_tim_mode = 0, .nf_op_mode = 0, .re2qs_sync = 4, .re2qs_async = 4, },
	[NDCU_IF_DDR>>4]  = { .nf_max_ddr = 1, .nf_tim_mode = 6, .nf_op_mode = 2, .re2qs_sync = 4, .re2qs_async = 10, }, // Jamie 20201130 for distribution read
	[NDCU_IF_DDR2>>4] = { .nf_max_ddr = 1, .nf_tim_mode = 7, .nf_op_mode = 2, .re2qs_sync = 4, .re2qs_async = 10, },
	[NDCU_IF_DDR3>>4] = { .nf_max_ddr = 1, .nf_tim_mode = 7, .nf_op_mode = 2, .re2qs_sync = 4, .re2qs_async = 10, },
	[NDCU_IF_TGL1>>4] = { .nf_max_ddr = 1, .nf_tim_mode = 5, .nf_op_mode = 0, .re2qs_sync = 4, .re2qs_async = 4, },
	[NDCU_IF_TGL2>>4] = { .nf_max_ddr = 1, .nf_tim_mode = 5, .nf_op_mode = 0, .re2qs_sync = 8, .re2qs_async = 8, }, // Jamie 20201130 for distribution read
};

/*!
 * @brief NDCU set interface timing mode
 *
 * @param intf	interface
 * @param tm	timing mode
 *
 * @return	not used
 */
fast_code void ndcu_set_tm(ndcu_if_mode intf, u32 tm)
{
	ndcu_intf_mode = intf;

	intf_map_t * map = &inf_map[intf>>4];

	// The reset avoids Get Feature or Read Status hang easily in DDR mode
	ndcu_hw_rst();

	nf_ctrl_reg0_t reg;
	reg.all = ndcu_readl(NF_CTRL_REG0);
	reg.b.nf_tim_mode = map->nf_tim_mode;
	reg.b.nf_op_mode  = map->nf_op_mode;
	reg.b.nf_max_ddr  = map->nf_max_ddr;
	ndcu_writel(reg.all, NF_CTRL_REG0);

	int ch;
	for (ch = 0; ch < max_channel; ch++)
		ndphy_set_tm(ch, intf);
	ndphy_set_re2qs(map->re2qs_sync, map->re2qs_async);

	nf_ind_reg5_t ind_reg5;
	ind_reg5.all = ndcu_readl(NF_IND_REG5);
	switch (intf) {
	case NDCU_IF_TGL1:
	case NDCU_IF_TGL2:
		ind_reg5.b.ind_t1 = 1;
		ind_reg5.b.ind_t2 = 1;
		break;
	case NDCU_IF_SDR:
	default:
		ind_reg5.b.ind_t1 = 9;
		ind_reg5.b.ind_t2 = 9;
 		break;
	}
	ndcu_writel(ind_reg5.all, NF_IND_REG5);

	/* TODO: DLL clock change with AISC ready */
	/* TODO: check the flow with Jie */

        reg.all = ndcu_readl(NF_CTRL_REG0);
        reg.b.nf_reset = 1;
        ndcu_writel(reg.all, NF_CTRL_REG0);
        reg.b.nf_reset = 0;
        ndcu_writel(reg.all, NF_CTRL_REG0);

	return;
}

/*!
 * @brief Page parameter CRC calculation
 *
 * @param p	page parameter pointer
 * @param len	Length of page parameter (crc excluded)
 *
 * @return	not used
 */
fast_code u16 page_param_crc16(u8 const *p, u32 len)
{
	int i;
	u16 crc;
	const u16 base_crc = 0x4F4E;

	crc = base_crc;
	while (len--) {
		crc ^= *p++ << 8;
		for (i = 0; i < 8; i++)
			crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
	}

	return crc;
}

/*!
 * @brief Nand write protection enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
norm_ps_code void ndcu_write_protection(bool enable)
{
	int ch = 0;
	cmdphy_wp_ctrl_reg_t reg;

	reg.all = ndphy_readl(ch, CMDPHY_WP_CTRL_REG);
	reg.b.cmdphy_wp_ctrl = 1;
	reg.b.cmdphy_wp_val = !enable;
	ndphy_writel(ch, reg.all, CMDPHY_WP_CTRL_REG);
}

/*!
 * @brief NCB ndphy and ndcu reset
 *
 * @return	not used
 */
fast_code void ncb_hw_reset(void)
{
	ndphy_hw_reset();
	ndcu_hw_rst();
}

/*!
 * @brief Set tR, tERASE
 *
 * @param tR	tR in 0.1us
 * @param tPROG	tPROG array[4] pointer in us
 * @param tBERS	tERASE in us
 *
 * @return	not used
 */
fast_code void ndcu_set_nand_tBUSY(u32 tR, u32 tBERS, u32* tPROG)
{
	nf_wcnt_reg0_t erase_wcnt_reg;
	nf_wcnt_reg1_t read_wcnt_reg;
	nf_wcnt_reg3_t prog_wcnt_reg;
	nf_tim_ctrl_reg0_t nf_tim_ctrl_reg0;
	u32 max_tR;
	u32 max_tBERS;
	u32 read_div;
	u32 wcnt;
	u32 max_wcnt = 0x3F;
        nf_pcnt_reg0_t pcnt1_2;// _GENE_20200629 7bit
        nf_pcnt_reg1_t pcnt3_4; // _GENE_20200629 7bit
	///< Always use option1
	nf_tim_ctrl_reg0.all = ndcu_readl(NF_TIM_CTRL_REG0);
	nf_tim_ctrl_reg0.b.nf_timing_sel_ch0 = 0x0;
	ndcu_writel(nf_tim_ctrl_reg0.all, NF_TIM_CTRL_REG0);

	///< Calculate suitable read wcnt div
	read_div = 0;
	/*
		For NF_DIV3 (0x0F4, nf_wcnt_reg1)
		0h: divide the original NFX1_CLK by 2^6, and then use it as the polling frequency
		1h: divide the original NFX1_CLK by 2^7, and then use it as the polling frequency
		2h: divide the original NFX1_CLK by 2^8, and then use it as the polling frequency
		3h: divide the original NFX1_CLK by 2^9, and then use it as the polling frequency
	*/
	max_tR = 63 * (1 << 6) * 10 / (nf_clk / 2);
	/*
		For NF_DIV1 (0x0F0, nf_wcnt_reg0)
		0h: divide the original NFX1_CLK by 2^11, and then use it as the polling frequency
		1h: divide the original NFX1_CLK by 2^12, and then use it as the polling frequency
		2h: divide the original NFX1_CLK by 2^13, and then use it as the polling frequency
		3h: divide the original NFX1_CLK by 2^14, and then use it as the polling frequency
	*/
	max_tBERS = 63 * (1 << 14) / (nf_clk / 2);
	while ((tR > max_tR) && (read_div < 3)) {
		read_div++;
		max_tR *= 2;
	}

	///< Calculate read wcnt
	wcnt = tR * max_wcnt / max_tR;
	read_wcnt_reg.all = ndcu_readl(NF_WCNT_REG1);
	read_wcnt_reg.b.nf_wcnt32 = wcnt;
	read_wcnt_reg.b.nf_div3 = read_div;
	//ncb_ndcu_trace(LOG_ERR, 0, "tR %dus, @ %dclk read div %d wcnt 0x%x\n", tR, nf_clk, read_div, wcnt);
	ndcu_writel(read_wcnt_reg.all, NF_WCNT_REG1);

	///< Calculate erase wcnt
	wcnt = tBERS * max_wcnt / max_tBERS;
	erase_wcnt_reg.all = ndcu_readl(NF_WCNT_REG0);
	erase_wcnt_reg.b.nf_wcnt12 = wcnt;
        erase_wcnt_reg.b.nf_div1 = 0;
	ndcu_writel(erase_wcnt_reg.all, NF_WCNT_REG0);
	//ncb_ndcu_trace(LOG_ERR, 0, "tERASE %dus, @ %dclk erase wcnt 0x%x\n", tBERS, nf_clk, wcnt);


	///< Calculate program wcnt
	u32 max_tPROG1, max_tPROG2;
        u32 max_tPROG3, max_tPROG4;
	u32 div;
	max_tPROG1 = max_tPROG2 = 63 * (1 << 10) / (nf_clk / 2);
	prog_wcnt_reg.all = ndcu_readl(NF_WCNT_REG3);

        div = 3;
	max_tPROG1 = 63 * (1 << (10+div)) / (nf_clk / 2);//_GENE_20200629
        max_tPROG2 = 63 * (1 << (12)) / (nf_clk / 2);//_GENE_20200629
        max_tPROG3 = 63 * (1 << (13)) / (nf_clk / 2);//_GENE_20200629
        max_tPROG4 = 63 * (1 << (14)) / (nf_clk / 2);//_GENE_20200629
        prog_wcnt_reg.all = ndcu_readl(NF_WCNT_REG3);


	wcnt = tPROG[0] * max_wcnt / max_tPROG1;
	prog_wcnt_reg.b.nf_wcnt71 = (wcnt > max_wcnt) ? max_wcnt : wcnt;
	wcnt = tPROG[1] * max_wcnt / max_tPROG2;
	prog_wcnt_reg.b.nf_wcnt72 = (wcnt > max_wcnt) ? max_wcnt : wcnt;
        prog_wcnt_reg.b.nf_div7 = div;
	div = 3;
	wcnt = tPROG[2] * max_wcnt / max_tPROG3;
	prog_wcnt_reg.b.nf_wcnt81 = (wcnt > max_wcnt) ? max_wcnt : wcnt;
	wcnt = tPROG[3] * max_wcnt / max_tPROG4;
	prog_wcnt_reg.b.nf_wcnt82 = (wcnt > max_wcnt) ? max_wcnt : wcnt;
        prog_wcnt_reg.b.nf_div8 = div;
	ndcu_writel(prog_wcnt_reg.all, NF_WCNT_REG3);

         //_GENE_20200629
        pcnt1_2.all = ndcu_readl(NF_PCNT_REG0);
        pcnt1_2.b.nf_pcnt12 = 0;
        ndcu_writel(pcnt1_2.all,NF_PCNT_REG0);

        pcnt3_4.all = ndcu_readl(NF_PCNT_REG1);
        pcnt3_4.b.nf_pcnt31 = 0x20;
        pcnt3_4.b.nf_pcnt32 = 0x20;
        ndcu_writel(pcnt3_4.all,NF_PCNT_REG1);
	//ncb_ndcu_trace(LOG_ERR, 0, "tPROG %dus, @ %dclk read wcnt reg 0x%x\n", tPROG[0], nf_clk, prog_wcnt_reg.all);
}

/*!
 * @brief Set read status poll interval (1st wcnt and later pcnt)
 *
 * @return	not used
 */
fast_code void ndcu_set_poll_delay(void)
{
	nf_wcnt_reg0_t wcnt0;
	nf_wcnt_reg1_t wcnt1;
	nf_wcnt_reg2_t wcnt2;
	nf_wcnt_reg3_t wcnt3;
	nf_pcnt_reg0_t pcnt0;
	nf_pcnt_reg1_t pcnt1;

	// Temporarily set to minimum
	wcnt0.all = ndcu_readl(NF_WCNT_REG0);
	wcnt0.all = 0;
	ndcu_writel(wcnt0.all, NF_WCNT_REG0);

	wcnt1.all = ndcu_readl(NF_WCNT_REG1);
	wcnt1.all = 0;
	ndcu_writel(wcnt1.all, NF_WCNT_REG1);

	wcnt2.all = ndcu_readl(NF_WCNT_REG2);
	wcnt2.all = 0;
	ndcu_writel(wcnt2.all, NF_WCNT_REG2);

	wcnt3.all = ndcu_readl(NF_WCNT_REG3);
	wcnt3.all = 0;
	ndcu_writel(wcnt3.all, NF_WCNT_REG3);

	pcnt0.all = ndcu_readl(NF_PCNT_REG0);
	pcnt0.all = 0;
	ndcu_writel(pcnt0.all, NF_PCNT_REG0);

	pcnt1.all = ndcu_readl(NF_PCNT_REG1);
	pcnt1.all = 0xA0;
	ndcu_writel(pcnt1.all, NF_PCNT_REG1);

#if !FPGA
#if HAVE_TSB_SUPPORT || HAVE_MICRON_SUPPORT
	u32 tPROG[4] = {0};	// In total we can have 4 different tPROG settings
#endif

	///< tPROG[0] for SLC program, [1] for almost 0 tPROG, [2] for XLC program
#if TSB_XL_NAND
	tPROG[0] = tPROG[1] = tPROG[2] = tPROG[3] = 72;
	ndcu_set_nand_tBUSY(47, 1770, tPROG);		///< 4.77us tR, 1.77ms tBERS
#elif HAVE_TSB_SUPPORT
	u32 max_wdelay = ((nand_interleave_num() / nand_channel_num() - nand_plane_num()) * nand_whole_page_size() / nf_clk);	///< Data xfer time of other LUNS of this channel
	tPROG[1] = 0;//slc cache prg
	tPROG[2] = 0;// slc rand prg & tlc low/md
	if (tPROG[2] > XLC * max_wdelay) {
		tPROG[2] = XLC * max_wdelay;
	}
	if (nand_is_bics3()) {
		tPROG[0] = tPROG[3] = 242;
		ndcu_set_nand_tBUSY(510, 4920, tPROG);	///< 51us tR, 4.92ms tBERS
	} else if (nand_is_tsb_tsv()) {
		tPROG[0] = tPROG[3] = 162;
		ndcu_set_nand_tBUSY(430, 6770, tPROG);	///< 43us tR, 6.77ms tBERS
	} else if (nand_is_bics4_800mts() || nand_is_bics4_16DP()) {
		tPROG[0] = 162; //slc prg
		tPROG[1] = 0; //slc cache prg
		tPROG[2] = 0; //slc rand prg & tlc low/md
    tPROG[3] = 2000;// tlc up
		ndcu_set_nand_tBUSY(320, 4568, tPROG);	///< 32us tR, 4.568ms/3.937ms TLC/SLC tBERS
	} else if (nand_is_bics4_TH58LJT2V24BB8N()) {
		ndcu_set_nand_tBUSY(280, 7150, tPROG);	///< 28us tR, 8.99ms/7.15ms TLC/SLC tBERS
	} else if (nand_is_bics4_HDR()) {
		tPROG[0] = tPROG[3] = 162;
		ndcu_set_nand_tBUSY(320, 4568, tPROG);	///< 32us tR, 4.568ms/3.937ms TLC/SLC tBERS		/* code */
	}
    else {//8dp 4T
		tPROG[0] = 162; //slc prg
		tPROG[1] = 0; //slc cache prg
		tPROG[2] = 0; //slc rand prg & tlc low/md
                tPROG[3] = 2000;// tlc up
		ndcu_set_nand_tBUSY(240, 4568, tPROG);	///< 32us tR, 4.568ms/3.937ms TLC/SLC tBERS
    }
#elif HAVE_MICRON_SUPPORT
	u32 max_wdelay = ((nand_interleave_num() / nand_channel_num() - nand_plane_num()) * nand_whole_page_size() / nf_clk);	///< Data xfer time of other LUNS of this channel
	tPROG[0] = 0;
	tPROG[2] = 0;
	if (tPROG[2] > (XLC - 1) * max_wdelay) {
		tPROG[2] = (XLC - 1) * max_wdelay;
	}
	if (nand_is_b27b()) {
		tPROG[1] = tPROG[3] = 0;
		ndcu_set_nand_tBUSY(409, 3700, tPROG);	///< 40.9us tR, 3.70ms tBERS
	}
#endif
#endif
}

fast_code void set_busy_time(u32 ndcmd_fmt, u32 op_type, u32 wcnt_sel,
				u32 tbusy)
{
	nf_ncmd_fmt_ptr_t reg_ptr;
	nf_ncmd_fmt_reg1_t wcnt_sel_reg;
	nf_wcnt_enh_reg0_t wcnt_reg;
	u32 max_tbusy;
	u32 wcnt, max_wcnt = 0xFF;
	u32 div = 0;
	u32 div_sel = 0, max_div_sel = 0;

	reg_ptr.all = ndcu_readl(NF_NCMD_FMT_PTR);
	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = ndcmd_fmt;
	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);

	switch (op_type) {
	case FINST_TYPE_ERASE:
		div = 11;
		max_div_sel = 3;
		break;
	case FINST_TYPE_PROG:
		div = 10;
		max_div_sel = 3;
		break;
	case FINST_TYPE_READ:
		div = 6;
		max_div_sel = 0;
		break;
	}

	for (div_sel = 0; div_sel < 4; div_sel++) {
		max_tbusy = max_wcnt * (1 << div) / (nf_clk / 2);
		if (max_tbusy >= tbusy) {
			break;
		} else {
			if (div_sel == max_div_sel) {
				break;
			} else if (div_sel == 0) {
				div += 2;
			} else {
				div += 1;
			}
		}

	}

	wcnt_sel_reg.all = ndcu_readl(NF_NCMD_FMT_REG1);
	wcnt_sel_reg.b.pcnt_sel = 3;
	wcnt_sel_reg.b.wcnt_sel = wcnt_sel;
	wcnt_sel_reg.b.freq_div_sel = div_sel;
	ndcu_writel(wcnt_sel_reg.all, NF_NCMD_FMT_REG1);

	if (tbusy > max_tbusy) {
		wcnt = max_wcnt;
	} else {
		wcnt = tbusy * max_wcnt / max_tbusy;
	}

	wcnt_reg.all = ndcu_readl(NF_WCNT_ENH_REG0 + (wcnt_sel >> 2) * 4);
	wcnt_reg.all &= ~(0xFF << (8 * (wcnt_sel & 3)));
	wcnt_reg.all |= wcnt << (8 * (wcnt_sel & 3));
	ndcu_writel(wcnt_reg.all, NF_WCNT_ENH_REG0 + (wcnt_sel >> 2) * 4);

  //adams try Pcnt 16DP 8T 512Gb
  /*if (nand_is_bics4_800mts() || nand_is_bics4_TH58TFT2T23BA8J()) {
  wcnt_reg.all = ndcu_readl(NF_PCNT_ENH_REG0 + (0 >> 2) * 4);
  wcnt_reg.all = 0x40000000;
  ndcu_writel(wcnt_reg.all, NF_PCNT_ENH_REG0 + (0 >> 2) * 4);
  }*/
}

/*!
 * @brief Nand command format configure
 *
 * @return	not used
 */
norm_ps_code void ndcu_ndcmd_format_cfg(void)
{
	int i;
	nf_ncmd_fmt_ptr_t reg_ptr;
	reg_ptr.all = 0;

	for (i = 1; i < FINST_NAND_FMT_MAX; i++) {
		reg_ptr.b.nf_ncmd_fmt_cfg_ptr = i;
		ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
		ndcu_writel(ncmd_fmt_array[i].fmt.all, NF_NCMD_FMT_REG0);
		ndcu_writel(ncmd_fmt_array[i].poll.all, NF_NCMD_FMT_REG1);
	}
}

/*!
 * @brief NDCU registers init
 *
 * @return	not used
 */
init_code void ndcu_hw_init(void)
{
	u32 *src;
	u32 *dest;

#if HAVE_TSB_SUPPORT
	extern void ndcu_ndcmd_format_adjust(void);
	ndcu_ndcmd_format_adjust();
#endif

	// Copy nand command format
	ndcu_ndcmd_format_cfg();

	// Copy nand command bytes
	src = (u32 *)&ncb_ndcu_cmd_regs;
	dest = (u32 *)(NDCU_REG_ADDR + NF_RCMD_REG00);

	while (dest <= (u32 *)(NDCU_REG_ADDR + NF_PRECMD_REG21))
		*dest++ = *src++;

	// Copy pass/fail and ready/busy setting
	src = (u32 *)&ncb_ndcu_rdy_fail_regs;
	dest = (u32 *)(NDCU_REG_ADDR + NF_FAIL_REG8);
	while (dest <= (u32 *)(NDCU_REG_ADDR + NF_FAIL_REG15 + 4))
		*dest++ = *src++;
	u32 reg;
	int i;
	for (i = 0; i < 8; i++) {
		reg = ndcu_readl(NF_FAIL_REG8 + (i * 4));
		if (i < 5) {
			ndcu_writel(reg, NF_FAIL_REG0 + (i * 4));
		} else {
			ndcu_writel(reg, NF_FAIL_REG5 + ((i - 5) * 4));
		}
	}

	ndcu_writel(ncb_ndcu_rdy_fail_regs.nf_rdy_reg0.all, NF_RDY_REG0);

	nf_lun_ctrl_reg0_t lun_ctrl;
	lun_ctrl.all = 0;
	lun_ctrl.b.lun_addr_sel_ch0 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch1 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch2 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch3 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch4 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch5 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch6 = nand_row_lun_shift() - 16;
	lun_ctrl.b.lun_addr_sel_ch7 = nand_row_lun_shift() - 16;
	ndcu_writel(lun_ctrl.all, NF_LUN_CTRL_REG0);

	nf_mp_enh_ctrl_reg0_t aipr_ctrl_reg0;
	aipr_ctrl_reg0.all = ndcu_readl(NF_MP_ENH_CTRL_REG0);
	aipr_ctrl_reg0.b.mp_addr_sel_ch0 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch1 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch2 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch3 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch4 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch5 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch6 = nand_row_plane_shift() - 8;
	aipr_ctrl_reg0.b.mp_addr_sel_ch7 = nand_row_plane_shift() - 8;
	ndcu_writel(aipr_ctrl_reg0.all, NF_MP_ENH_CTRL_REG0);

	nf_mp_enh_ctrl_reg1_t aipr_ctrl_reg1;
	aipr_ctrl_reg1.all = ndcu_readl(NF_MP_ENH_CTRL_REG1);
	aipr_ctrl_reg1.b.mp_num_max = ctz(nand_plane_num());
	ndcu_writel(aipr_ctrl_reg1.all, NF_MP_ENH_CTRL_REG1);

	nf_rdy_err_loc_reg0_t rdy_err_loc;
	rdy_err_loc.all = 0;
	rdy_err_loc.b.nf_rdy_range_sel = RDY_BIT_OFFSET;
	rdy_err_loc.b.nf_err_range_sel = FAIL_BIT_OFFSET;
	ndcu_writel(rdy_err_loc.all, NF_RDY_ERR_LOC_REG0);
	ndcu_writel(nf_fail_reg_sp.all, NF_FAIL_REG_SP);
        //_GENE_20200714
        nf_scmd_type_reg0_t scmd_type;
        scmd_type.all = ndcu_readl(NF_SCMD_TYPE_REG0);
        //if (nand_lun_num() == 1) {
        if (0) {  //All use 78h polling to report plane status //Sean_230421
            scmd_type.b.nf_scmd_type = 0; // all 70h
            ndcu_writel(0x70707070, NF_SCMD_REG0);  
            if(nand_support_aipr()){
                scmd_type.b.nf_scmd_type |= 3 <<(7*2);
				ndcu_writel(0x78707070, NF_SCMD_REG1);  
    		} else {
    			ndcu_writel(0x70707070, NF_SCMD_REG1);
            }
        }
        else
        {
            #if HAVE_MICRON_SUPPORT  || HAVE_UNIC_SUPPORT
                ndcu_writel(NAND_READ_STATUS_71 * 0x01010101, NF_SCMD_REG0 );
                ndcu_writel(NAND_READ_STATUS_71 * 0x01010101, NF_SCMD_REG1 );
                scmd_type.b.nf_scmd_type = 0x5555; //all use 71h
            #endif
            #if HAVE_YMTC_SUPPORT
                ndcu_writel(NAND_READ_STATUS_78 * 0x01010101, NF_SCMD_REG0 );
                ndcu_writel(NAND_READ_STATUS_78 * 0x01010101, NF_SCMD_REG1 );
                scmd_type.b.nf_scmd_type  = 0xFFFF; //all use 78h
            #endif
            #if HAVE_TSB_SUPPORT
                scmd_type.b.nf_scmd_type  = 0xFFFF; //all use 78h
                ndcu_writel(NAND_READ_STATUS_78 * 0x01010101, NF_SCMD_REG0 );
                ndcu_writel(NAND_READ_STATUS_78 * 0x01010101, NF_SCMD_REG1 );
            #endif
        }
        #if HAVE_HYNIX_SUPPORT || HAVE_SANDISK_SUPPORT || HAVE_SAMSUNG_SUPPORT
            sys_assert(0);
        #endif
        ndcu_writel(scmd_type.all, NF_SCMD_TYPE_REG0);

	nf_pdwn_ctrl_reg0_t pdwn_ctrl_reg;
	pdwn_ctrl_reg.all = ndcu_readl(NF_PDWN_CTRL_REG0);
#if HAVE_TSB_SUPPORT
	pdwn_ctrl_reg.b.nf_rdst_mp_poll_dis = 0;//
	pdwn_ctrl_reg.b.nf_mp_poll_on_fail = 0;

//_GENE_20200714
	//pdwn_ctrl_reg.b.nf_mlun_sel_en = 1;  
	//pdwn_ctrl_reg.b.nf_mlun_mode = 0;
	pdwn_ctrl_reg.b.nf_mlun_sel_en = 0;  //disable polling brfore 05_E0
	pdwn_ctrl_reg.b.nf_mlun_mode = 1;
    pdwn_ctrl_reg.b.nf_scmd_enh_en = 1;

#else
	pdwn_ctrl_reg.b.nf_rdst_mp_poll_dis = 1;
#endif

#if HAVE_YMTC_SUPPORT
	if (nand_lun_num() != 1) {
		pdwn_ctrl_reg.b.nf_rdst_mp_poll_dis = 0;
		pdwn_ctrl_reg.b.nf_mp_poll_on_fail = 0;
	}
#endif
#if defined(FAST_MODE)
	pdwn_ctrl_reg.b.nf_progxfer_hpri_en = 0;
#endif
#if HAVE_SAMSUNG_SUPPORT
	pdwn_ctrl_reg.b.nf_prog_ext_en = 1;
#endif

#if WCNT_ENHANCE
	pdwn_ctrl_reg.b.nf_pwcnt_enh_en = 1;
#endif
	ndcu_writel(pdwn_ctrl_reg.all, NF_PDWN_CTRL_REG0);
#if HAVE_SAMSUNG_SUPPORT
	ndcu_writel(0x21131211,NF_PSTCMD_REG00);
	ndcu_writel(0x32312322,NF_PSTCMD_REG01);
#endif

	nf_susp_reg0_t susp_reg;
	susp_reg.all = ndcu_readl(NF_SUSP_REG0);
	susp_reg.b.nf_susp_p2e = 1;
	susp_reg.b.nf_susp_en = 1;
	susp_reg.b.nf_susp_rd_ooo_en = 1;
	susp_reg.b.nf_susp_set_feat_en = 1;

#if HAVE_MICRON_SUPPORT || HAVE_UNIC_SUPPORT
	susp_reg.b.nf_susp_eaddr_mode = ~BIT0 & BIT1;// Suspend w/ addr, resume w/o addr
	susp_reg.b.nf_susp_paddr_mode = 0;// Suspend w/ addr, resume w/ addr
#elif HAVE_SANDISK_SUPPORT || HAVE_TSB_SUPPORT
	susp_reg.b.nf_resm_cmd_mode = 1;// Resume with erase command
	susp_reg.b.nf_susp_eaddr_mode = 0;// Suspend w/ addr, resume w addr
	susp_reg.b.nf_susp_paddr_mode = BIT0 | BIT1;// Suspend w/o addr, resume w/o addr
#endif
	susp_reg.b.nf_susp_prefix_mode = 2;// Disable prefix
	susp_reg.all |= BIT13;// Workaround for #5302
	ndcu_writel(susp_reg.all, NF_SUSP_REG0);
	ndcu_writel(0, NF_SUSP_REG1);

	if ((nand_info.addr_cycles & 0xF) == 4) {
		nf_ctrl_reg0_t reg;
		reg.all = ndcu_readl(NF_CTRL_REG0);
		reg.b.nf_ext_row_en = 1;
		ndcu_writel(reg.all, NF_CTRL_REG0);
	}
}

/*!
 * @brief Enable register control mode
 *
 * @return	not used
 */
fast_code void ndcu_en_reg_control_mode(void)
{
	nf_ctrl_reg0_t reg;

	reg.all = ndcu_readl(NF_CTRL_REG0);
	if (reg.b.nf_tim_mode >= 6) {
		reg.b.nf_op_mode = 2;
	} else {
		reg.b.nf_op_mode = 0;
	}
	ndcu_writel(reg.all, NF_CTRL_REG0);
}

/*!
 * @brief Disable register control mode
 *
 * @return	not used
 */
fast_code void ndcu_dis_reg_control_mode(void)
{
	nf_ctrl_reg0_t reg;

	reg.all = ndcu_readl(NF_CTRL_REG0);
	reg.b.nf_op_mode = 1;

	ndcu_writel(reg.all, NF_CTRL_REG0);
}

/*!
 * @brief Register control mode adjustment for DDR mode
 * refer to NDCU_register_control_AppNote for detail
 *
 * @param ctrl	operation parameters
 * @param cmd	cmd byte
 *
 * @return	0
 */
#if HAVE_ONFI_SUPPORT
fast_code static void ndcu_ddr_ctrl(ndcu_ind_t *ctrl, u8 cmd)
{
	if (ndcu_intf_mode == NDCU_IF_SDR || ctrl->xfcnt <= 1) {
		ctrl->ind_ddr_en = 0;
		ctrl->ind_xfcnt_db_en = 0;
		return;
	}

	switch (cmd) {
	case 0x90: /* read id */
	case 0x70: case 0x71: case 0x78: /* read status */
	case 0xEE: /* read feature */
		ctrl->ind_xfcnt_db_en = 1;
		break;

	case 0xEF: /* set feature */
		ctrl->ind_xfcnt_db_en = 1;
		break;

	case 0xEC: /* read parameter */
	case 0x00: case 0x31: case 0x3F: /* read */
	case 0x80: case 0x81: case 0x84: case 0x13: /* prog */
		ctrl->ind_xfcnt_db_en = 0;
		break;
	}
	ctrl->ind_ddr_en = 1;
}
#endif

#if HAVE_TOGGLE_SUPPORT
fast_code static void ndcu_ddr_ctrl(ndcu_ind_t *ctrl, u8 cmd)
{
	switch (cmd) {
	case 0x90: /* read id */
	case 0x70: case 0x71: case 0x78: /* read status */
#if HAVE_HYNIX_SUPPORT
	case 0x37: /* Get parameter */
#endif
#if HAVE_TSB_SUPPORT
	case 0xB0: /* TSB BiCS4 read detected steps */
#endif
	case 0xEE: /* read feature */
	case 0xD4:
		ctrl->ind_ddr_en = 0;
		if (ndcu_intf_mode == NDCU_IF_SDR) {
			ctrl->ind_xfcnt_db_en = 0;
		} else {
			ctrl->ind_xfcnt_db_en = 1;
		}
		break;

#if HAVE_HYNIX_SUPPORT
	case 0x36: /* Set parameter */
#endif
#if HAVE_TSB_SUPPORT
	case 0x55: /* TSB BiCS4 parameter set */
#endif
	case 0xEF: /* set feature */
	case 0xD5:
		if (ndcu_intf_mode == NDCU_IF_SDR) {
			ctrl->ind_ddr_en = 0;
			ctrl->ind_xfcnt_db_en = 0;
		} else {
			ctrl->ind_ddr_en = 1;
			ctrl->ind_xfcnt_db_en = 1;
		}
		break;

	case 0xEC: /* read parameter */
	case 0x00: case 0x3F: /* read */
	case 0x05: /* Random Data Output */
	case 0x80: /* prog */
	case 0x85: /* Random Data Input */
		ctrl->ind_ddr_en = 1;
		ctrl->ind_xfcnt_db_en = 0;
		break;
	}
}
#endif

/*!
 * @brief Register control mode operation open
 *
 * @param ctrl	operation parameters
 * @param ch	channel
 * @param ce	target
 *
 * @return	0
 */
fast_code int ndcu_open(ndcu_ind_t *ctrl, int ch, int ce)
{
	/* make sure the state machine in idle state */
	nf_ind_reg0_t reg0 = { .all = ndcu_readl(NF_IND_REG0), };
	sys_assert(reg0.b.ind_start == 0);

	/* XXX: assume op_mode, tim_mode are correct set */

	ndcu_ddr_ctrl(ctrl, ctrl->reg1.b.ind_byte0);

	/* 1. cmd/add register */
	ndcu_writel(ctrl->reg1.all, NF_IND_REG1);
	ndcu_writel(ctrl->reg2.all, NF_IND_REG2);

	/* 2. xfer cnt and ind_ddr_en, please ref to
	 * - NDCU_register_control_AppNote
	 */
	nf_ind_reg3_t reg3 = { .all = ndcu_readl(NF_IND_REG3), };
	reg3.b.ind_xfcnt = ctrl->xfcnt << ctrl->ind_xfcnt_db_en;
	ndcu_writel(reg3.all, NF_IND_REG3);

	/* 3. reset fifo */
	reg0.all = ndcu_readl(NF_IND_REG0);
	reg0.b.ind_fifo_reset = 0;
	ndcu_writel(reg0.all, NF_IND_REG0);
	reg0.b.ind_fifo_reset = 1;
	ndcu_writel(reg0.all, NF_IND_REG0);
	reg0.b.ind_fifo_reset = 0;
	ndcu_writel(reg0.all, NF_IND_REG0);

	/* 4. select device, rmb, cmd num */
	reg0.all = ndcu_readl(NF_IND_REG0);
	reg0.b.ind_ch_id   = ch;
	reg0.b.ind_dev_id  = ce;

	reg0.b.ind_rwb     = ctrl->write == true && ctrl->xfcnt != 0 ? 0 : 1;
	reg0.b.ind_cmd_num = ctrl->cmd_num;
	reg0.b.ind_cle_mode= ctrl->cle_mode;
	reg0.b.ind_rdy_ctrl= 0;
	reg0.b.ind_start   = 0;
	reg0.b.ind_ddr_en  = ctrl->ind_ddr_en;
	reg0.b.ind_wp_en   = 0;
	ndcu_writel(reg0.all, NF_IND_REG0);

	/* zero internal data */
	ctrl->rdy_ctrl = false;
	ctrl->cnt = 0;

	return 0;
}

/*!
 * @brief Register control mode operation start
 *
 * @param ctrl	operation parameters
 *
 * @return	0
 */
fast_code int ndcu_start(ndcu_ind_t *ctrl)
{
	nf_ind_reg0_t reg0 = { .all = ndcu_readl(NF_IND_REG0), };
	reg0.b.ind_start = 1;
	ndcu_writel(reg0.all, NF_IND_REG0);

	return 0;
}

/*!
 * @brief Register control mode operation data transfer
 *
 * @param ctrl	operation parameters
 *
 * @return	data transfer complete
 */
fast_code int ndcu_xfer(ndcu_ind_t *ctrl)
{
	nf_ind_reg0_t reg0;

	if (ctrl->xfcnt != 0 && ctrl->cnt == 0 &&
	    ctrl->write == false && ctrl->rdy_ctrl == false) {
		reg0.all = ndcu_readl(NF_IND_REG0);
		reg0.b.ind_rdy_ctrl = 1;
		ndcu_writel(reg0.all, NF_IND_REG0);
		ctrl->rdy_ctrl = true;
	}
	reg0.all = ndcu_readl(NF_IND_REG0);
	if (!reg0.b.ind_fifo_rdy)
		return 0;

	nf_ind_reg4_t data;
	int xfcnt = (ctrl->ind_xfcnt_db_en && ctrl->ind_ddr_en) ?  2 : 4;
	int len = min(ctrl->xfcnt - ctrl->cnt, xfcnt);

	if (ctrl->write == false) {
		data.all = ndcu_readl(NF_IND_REG4);
		if ((ctrl->ind_xfcnt_db_en == 0) || (ctrl->ind_xfcnt_db_en == 1 && ctrl->ind_ddr_en == 0)) {
			switch (len) {
			case 4:	ctrl->buf[ctrl->cnt+3] = data.b.ind_fdata3;
			case 3: ctrl->buf[ctrl->cnt+2] = data.b.ind_fdata2;
			case 2: ctrl->buf[ctrl->cnt+1] = data.b.ind_fdata1;
			case 1: ctrl->buf[ctrl->cnt+0] = data.b.ind_fdata0;
			}
		} else {
			switch (len) {
			case 2: ctrl->buf[ctrl->cnt+1] = data.b.ind_fdata3;
			case 1: ctrl->buf[ctrl->cnt+0] = data.b.ind_fdata0;
			}
		}
	} else {
		data.all = 0;
		if (ctrl->ind_xfcnt_db_en == 0) {
			switch (len) {
			case 4:	data.b.ind_fdata3 = ctrl->buf[ctrl->cnt+3];
			case 3:	data.b.ind_fdata2 = ctrl->buf[ctrl->cnt+2];
			case 2:	data.b.ind_fdata1 = ctrl->buf[ctrl->cnt+1];
			case 1:	data.b.ind_fdata0 = ctrl->buf[ctrl->cnt+0];
			}
		} else {
			switch (len) {
			case 2:	data.b.ind_fdata3 = ctrl->buf[ctrl->cnt+1];
				data.b.ind_fdata2 = ctrl->buf[ctrl->cnt+1];
				data.b.ind_fdata1 = ctrl->buf[ctrl->cnt+0];
				data.b.ind_fdata0 = ctrl->buf[ctrl->cnt+0];
				break;
			case 1:	data.b.ind_fdata1 = ctrl->buf[ctrl->cnt+0];
				data.b.ind_fdata0 = ctrl->buf[ctrl->cnt+0];
				break;
			}
		}
		ndcu_writel(data.all, NF_IND_REG4);
	}
	ctrl->cnt += len;

	return ctrl->cnt == ctrl->xfcnt;
}

/*!
 * @brief Register control mode operation end
 *
 * @param ctrl	operation parameters
 *
 * @return	0
 */
fast_code int ndcu_close(ndcu_ind_t *ctrl)
{
	nf_ind_reg0_t reg0;

	if (ctrl->cnt == ctrl->xfcnt && ctrl->write == true) {
		reg0.all = ndcu_readl(NF_IND_REG0);
		reg0.b.ind_rdy_ctrl = 1;
		ndcu_writel(reg0.all, NF_IND_REG0);
	}

	/* waiting for the state machine back to idle */
	do {
		reg0.all = ndcu_readl(NF_IND_REG0);
	} while (reg0.b.ind_start);

	return 0;
}

#if defined(FAST_MODE)
/*!
 * @brief Fast access mode enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
fast_code void ndcu_fast_mode(bool enable)
{
	nf_fac_ctrl_reg0_t reg0;

	reg0.all = ndcu_readl(NF_FAC_CTRL_REG0);
	reg0.b.nf_fac_mode_en = enable;
	ndcu_writel(reg0.all, NF_FAC_CTRL_REG0);
}

/*!
 * @brief Fast access mode initialization
 *
 * @param en	enable
 *
 * @return	not used
 */
fast_code void ndcu_fast_mode_init(u8 nr_ch)
{
	if ((nr_ch != 2) && (nr_ch != 4) && (nr_ch != 8)) {
		ncb_eccu_trace(LOG_INFO, 0x8a24, "FAST mode can't support (%d) physical channels", nr_ch);
		sys_assert(0);
	}

	nf_fac_ctrl_reg0_t reg0;
	nf_ctrl_reg0_t nf_ctrl_reg0;

	reg0.all = ndcu_readl(NF_FAC_CTRL_REG0);
	reg0.b.nf_fac_mode_en = 1;
	reg0.b.nf_fac_op_mode = (4 - ctz(nr_ch));
	ndcu_writel(reg0.all, NF_FAC_CTRL_REG0);

        nf_ctrl_reg0.all = ndcu_readl(NF_CTRL_REG0);
	nf_ctrl_reg0.b.nf_rd_ooo_dis = 1; // workaround for multiple CE fast mode, #3186
	ndcu_writel(nf_ctrl_reg0.all, NF_CTRL_REG0);
	ncb_eccu_trace(LOG_INFO, 0x51d1, "FAST mode enabled for NDCU, (%d) physical channels", nr_ch);
}
#endif

/*!
 * @brief NDCU high speed timing parameters setting
 *
 * @return	not used
 */
norm_ps_code void ndcu_set_high_freq(void)
{
	nf_sync_tim_reg0_t sync_reg0;
	nf_sync_tim_reg1_t sync_reg1;
	nf_sync_tim_reg2_t sync_reg2;
	nf_gen_tim_reg0_t gen_reg0;
	nf_gen_tim_reg1_t gen_reg1;

	sync_reg2.all = ndcu_readl(NF_SYNC_TIM_REG2);

	//ncb_ndcu_trace(LOG_INFO, 0, "Set type %x to %d MHz", ncb_flash_type, nf_clk);

	switch (nand_info.cur_tm & 0xF0) {
	case NDCU_IF_SDR:
		return;

	case NDCU_IF_TGL1:
	case NDCU_IF_TGL2:
	//case NDCU_IF_TGL3:
		sync_reg0.all = ndcu_readl(NF_SYNC_TIM_REG0);
		sync_reg1.all = ndcu_readl(NF_SYNC_TIM_REG1);
		sync_reg2.all = ndcu_readl(NF_SYNC_TIM_REG2);
            	gen_reg0.all = ndcu_readl(NF_GEN_TIM_REG0);
		gen_reg1.all = ndcu_readl(NF_GEN_TIM_REG1);
#if FPGA
		sync_reg1.b.t5_sync1 = 12;
		sync_reg0.b.t3_sync1 = 6;
		sync_reg0.b.t4_sync1 = 6;
#else
		sync_reg0.b.t3_sync1 = 4;
		sync_reg0.b.t4_sync1 = 4;
		switch(nf_clk) {
		case 666:
			sync_reg1.b.t5_sync1 = 10;
			sync_reg2.b.twhr = 0x2A;
			break;
		case 800:
			sync_reg0.b.t3_sync1 = 4;
			sync_reg0.b.t4_sync1 = 4;
			sync_reg1.b.t5_sync1 = 10;
			sync_reg1.b.tadl_sync1 = 0x06; //_GENE_20200929 more margin
			sync_reg2.b.twhr = 0x2D; //_GENE_20200929 more margin
      gen_reg1.b.nf_trpre = 0xF; // Jamie
			gen_reg1.b.nf_trpsth = 0xF; // Jamie 20200925 Maximum tRPSTH
			
                        /*
                        
                        gen_reg1.b.nf_twpsth = 0x0A; //_GENE_20200709
                        gen_reg0.b.nf_twb = 0 ; //_GENE_20200709
                        gen_reg1.b.nf_trpst = 0x0A; //_GENE_20200709
                        gen_reg1.b.nf_twpre = 0x0A; //_GENE_20200709
                        */
			break;
		case 1066:
			sync_reg0.b.t3_sync1 = 5;
			sync_reg0.b.t4_sync1 = 5;
			sync_reg1.b.t5_sync1 = 12;
			sync_reg2.b.twhr = 0x2A;
			break;

		case 1200:

			#if 0
			sync_reg0.b.t3_sync1 = 7;
			sync_reg0.b.t4_sync1 = 7;
			sync_reg1.b.t5_sync1 = 4;
			sync_reg2.b.twhr = 0x28;
			sync_reg1.b.tadl_sync1 = 6;
			gen_reg0.b.nf_ddr2_wcnt_ext = 1;
			gen_reg1.b.nf_twpsth = 12;
			gen_reg1.b.nf_trpre = 9;
			gen_reg1.b.nf_trpst = 11;
			gen_reg1.b.nf_trpsth = 14;
			gen_reg1.b.nf_twpst = 12;
			gen_reg0.b.nf_trhw = 3;
			ndphy_set_re2qs_gate_dly(10);
			ndphy_set_qs_gate_dis(0);
			ndphy_set_re2qs_dly(9);
			break;
			#else   //sean_tune
			sync_reg0.b.t3_sync1 = 4;
			sync_reg0.b.t4_sync1 = 4; ///3
			sync_reg1.b.t5_sync1 = 2;
			sync_reg2.b.twhr = 0x24; ///0x10
			sync_reg1.b.tadl_sync1 = 9; ///6
			gen_reg0.b.nf_ddr2_wcnt_ext = 1;
			gen_reg1.b.nf_twpsth = 7;
			gen_reg1.b.nf_trpre = 5;
			gen_reg1.b.nf_trpst = 8; ///6
			gen_reg1.b.nf_trpsth = 14; ///7
			gen_reg1.b.nf_twpst = 7;
			gen_reg0.b.nf_trhw = 3;
			ndphy_set_re2qs_gate_dly(5);
			ndphy_set_qs_gate_dis(0);
			ndphy_set_re2qs_dly(8);
			break;
			#endif

		default:
			sync_reg1.b.t5_sync1 = 12;
			sync_reg2.b.twhr = 0x3E;
			break;
		}
#endif
		ndcu_writel(sync_reg0.all, NF_SYNC_TIM_REG0);
		ndcu_writel(sync_reg1.all, NF_SYNC_TIM_REG1);
		ndcu_writel(sync_reg2.all, NF_SYNC_TIM_REG2);
		ndcu_writel(gen_reg0.all, NF_GEN_TIM_REG0);
		ndcu_writel(gen_reg1.all, NF_GEN_TIM_REG1);
		break;

	case NDCU_IF_DDR3:
	case NDCU_IF_DDR2:
		sync_reg0.all = ndcu_readl(NF_SYNC_TIM_REG0);
		sync_reg1.all = ndcu_readl(NF_SYNC_TIM_REG1);
		gen_reg0.all = ndcu_readl(NF_GEN_TIM_REG0);
		gen_reg1.all = ndcu_readl(NF_GEN_TIM_REG1);
		switch(nf_clk) {
		case 400:
			sync_reg0.b.t3_sync1 = 4;
			sync_reg0.b.t4_sync1 = 4;
			sync_reg1.b.t5_sync1 = 4;
			break;
		case 533:
			sync_reg0.b.t3_sync1 = 4;
			sync_reg0.b.t4_sync1 = 4;
			sync_reg1.b.t5_sync1 = 6;
			break;
		case 666:
			sync_reg0.b.t3_sync1 = 5;
			sync_reg0.b.t4_sync1 = 5;
			sync_reg1.b.t5_sync1 = 12;
			gen_reg1.b.nf_trpst = 14;
			break;
		case 1066:
			sync_reg0.b.t3_sync1 = 5;
			sync_reg0.b.t4_sync1 = 5;
			sync_reg1.b.t5_sync1 = 12;
			gen_reg1.b.nf_trpst = 7;
			break;
		case 1200:
#if 1
			// HW provided value
			sync_reg0.b.t3_sync1 = 5;
			sync_reg0.b.t4_sync1 = 5;
			sync_reg1.b.t5_sync1 = 6;
			sync_reg2.b.twhr = 2;
			gen_reg1.b.nf_trpre = 4;
			gen_reg1.b.nf_twpsth = 12;
			gen_reg0.b.nf_ddr2_wcnt_ext = 1;
			gen_reg1.b.nf_trpst = 14;
#endif
#if 0
			// Aggressive value
			sync_reg0.b.t3_sync1 = 3;
			sync_reg0.b.t4_sync1 = 3;
			sync_reg1.b.t5_sync1 = 4;
			sync_reg2.b.twhr = 0x11;
			gen_reg1.b.nf_trpst = 8;
#endif
			break;
		case 1600:
			sync_reg0.b.t3_sync1 = 6;
			sync_reg0.b.t4_sync1 = 6;
			sync_reg1.b.t5_sync1 = 10;
			sync_reg2.b.twhr = 2;
			gen_reg1.b.nf_trpre = 4;
			gen_reg1.b.nf_twpsth = 12;
			gen_reg0.b.nf_ddr2_wcnt_ext = 1;
			gen_reg1.b.nf_trpst = 14;
			break;
		default:
			sync_reg1.b.t5_sync1 = 12;
			sync_reg0.b.t3_sync1 = 6;
			sync_reg0.b.t4_sync1 = 6;
			break;
		}
		ndcu_writel(sync_reg0.all, NF_SYNC_TIM_REG0);
		ndcu_writel(sync_reg1.all, NF_SYNC_TIM_REG1);
		ndcu_writel(sync_reg2.all, NF_SYNC_TIM_REG2);
		ndcu_writel(gen_reg0.all, NF_GEN_TIM_REG0);
		ndcu_writel(gen_reg1.all, NF_GEN_TIM_REG1);
		break;

	default:
		break;
	}

	ndphy_locking_mode();
}

/*!
 * @brief NDCU NF clock switch
 *
 * @param freq	New NF clock frequency
 *
 * @return	not used
 */
fast_code void ndcu_nf_clk_switch(u32 freq)
{
	nf_clk = freq;
	nf_clk_init(nf_clk);
	if (nf_clk < 533) {
		ndphy_set_re2qs(4, 4);
	} else {
		ndphy_set_re2qs(10, 10);
	}
	ndcu_set_poll_delay();
}

/*!
 * @brief NDCU initialization (including nand initialization)
 *
 * @return	not used
 */
init_code void ndcu_init(void)
{
	ncb_hw_reset();
	ndphy_hw_init();

	nf_clk_init(100);
	ndphy_set_re2qs(4, 4);
	ndphy_enable_odt(false);

	nand_info.vcc_1p2v = false;
	nand_init();


#if (OTF_TIME_REDUCE == ENABLE)
	if (!misc_is_warm_boot())
#endif
	{
		ndphy_hw_reset();
	}

#if !defined(FPGA)
	//nf_clk = nand_info.max_speed;
	nf_clk = 533;
#if HAVE_TSB_SUPPORT
	//if (nand_is_bics4_TH58LJT1V24BA8H() && (nf_clk == 400)) {
	//	nf_clk = 666;
	//}
#ifdef U2
	nf_clk = 1200; /* Max. stable speed for U.2 */ //adams
#endif
#elif HAVE_YMTC_SUPPORT
	if ((nand_info.max_speed == 800) && !nand_is_1p2v()) {
		nf_clk = 666;
	}
#endif
#if defined(RDISK) && defined(M2_0305)
	nf_clk = 533;
	ncb_ndcu_trace(LOG_ALW, 0xc541, "Force %dMT/s for RDISK", nf_clk);
#endif
	//if (nf_clk > 100) {
	//	nand_set_high_speed();
	//}
	nf_clk_init(nf_clk);
	if (nf_clk <= 533) {
		ndphy_set_re2qs(8, 8);
	} else {
		ndphy_set_re2qs(10, 10);
	}
#ifdef U2
	ndphy_set_re2qs(8, 8); // DLL calib would fail with 10
#endif
#endif

	if (nf_clk > 100) {
		ndcu_set_high_freq();
		ndphy_set_differential_mode(true);
		ndphy_enable_odt(true);
#if (OTF_TIME_REDUCE == ENABLE)
		if (!misc_is_warm_boot())
#endif
		{
			nand_set_high_speed();
		}
	}
	ndcu_set_poll_delay();
	ndcu_hw_init();
#if WCNT_ENHANCE
	extern void ndcu_nand_cmd_format_init(void);
	ndcu_nand_cmd_format_init();
#endif
	ndcu_dis_reg_control_mode();
#if defined(FAST_MODE)
	ndcu_fast_mode_init(max_channel);
#endif

	nf_warmup_ctrl_reg0_t warm_up_reg;
	warm_up_reg.all = ndcu_readl(NF_WARMUP_CTRL_REG0);
#if defined(WARMUP_RD_CYCLES)
	sys_assert(((1 << WARMUP_RD_CYCLES) & (BIT1 | BIT2 | BIT4)) != 0);
	warm_up_reg.b.nf_rd_warmup_en = 1;
#if HAVE_MICRON_SUPPORT || HAVE_YMTC_SUPPORT
	warm_up_reg.b.nf_get_feat_warmup_en = 1;
#endif
	warm_up_reg.b.nf_rd_warmup_sz = WARMUP_RD_CYCLES;
#endif
#if defined(WARMUP_WR_CYCLES)
	sys_assert(((1 << WARMUP_WR_CYCLES) & (BIT1 | BIT2 | BIT4)) != 0);
	warm_up_reg.b.nf_wr_warmup_en = 1;
#if HAVE_MICRON_SUPPORT || HAVE_YMTC_SUPPORT
	warm_up_reg.b.nf_set_feat_warmup_en = 1;
#endif
	warm_up_reg.b.nf_wr_warmup_sz = WARMUP_WR_CYCLES;
#endif
	ndcu_writel(warm_up_reg.all, NF_WARMUP_CTRL_REG0);

	if (nf_clk == nand_info.max_speed) {
		ncb_ndcu_trace(LOG_INFO, 0x7555, "NDCU init done %dMT/s", nf_clk);
	} else if (nf_clk > nand_info.max_speed) {
		ncb_ndcu_trace(LOG_INFO, 0x3d0c, "NDCU init done %dMT/s over %dMT/s", nf_clk, nand_info.max_speed);
	} else {
		ncb_ndcu_trace(LOG_INFO, 0xc1f3, "NDCU init done %dMT/s under %dMT/s", nf_clk, nand_info.max_speed);
	}
}

/*! @} */
