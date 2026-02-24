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
/*! \file pcie.c
 * @brief pcie related code
 *
 * \addtogroup rtos
 * \defgroup pcie
 * \ingroup rtos
 *
 * {@
 */
//=============================================================================

#include "types.h"
#include "stdio.h"
#include "assert.h"
#include "string.h"
#include "rainier_soc.h"
#include "sect.h"
#include "misc_register.h"
#include "pcie_wrapper_register.h"
#include "pcie_core_register.h"
#include "misc.h"
#include "event.h"
#include "console.h"
#include "../../nvme/inc/hal_nvme.h"

#if defined(RDISK)
#define RXLM_TEST 1
#define RXLM_SOUT 0
#else
#define RXLM_TEST 1
#define RXLM_SOUT 0
#endif


#define SHORT_CHANNEL
#define IG_MODIFIED_PHY
#if (Synology_case)
#define FORCE_GEN3
#endif
//#define ASPM_REG_SETTING // wait IG reply if it is necessary

/*! \cond PRIVATE */
#define __FILEID__ pcie
#include "trace.h"
/*! \endcond */

#define PCIE_REF_CLK_CHK    300000    /// Extern PCIe Ref. Clock/PRESET-deassert wait from 30s
#define ltr_support



fast_data_zi corr_err_cnt_t corr_err_cnt;
slow_data u8 pcie_abnormal_event = 0;  // 0: No event occur. 1: PCIe not 4 x 4 first.  2:  CTO timeout.  3: suddent link down event. Richard added in 2024/5/23.

#if GET_PCIE_ERR
fast_data_zi struct timer_list corr_err_cnter_timer;
fast_data_zi struct timer_list dump_corr_timer;

fast_data_zi uncorr_err_cnt_t uncorr_err_cnt;
fast_data_zi u32 corr_error_cnt = 0;
fast_data_zi u32 uncorr_error_cnt = 0;
fast_data_zi u32 bad_tlp_start_time = 0;
share_data u8 volatile pcie_perst_flag;     // perst_assert flag, only of AER counter now. Richard 4/27
fast_data u8 AER_POLLING_TIMER_SET = 0;
fast_data u8 is_bad_tlp_masked = 0;

void corr_uncorr_err_counter(void *data);
void dump_pcie_corr_err(void *data);

#endif

fast_data u32 lane_leq[4]; //LEQ
fast_data s32 lane_dfe[4]; //DFE
fast_data u16 shr_pll_kr; //PLL_BAND
fast_data u32 lane_cdr[4]; //CDR
fast_data s8 lane_rc[4]; //DAC
fast_data_zi u8 no_clk=0;
fast_data_zi u8 no_perst=0;
#if(CORR_ERR_INT == ENABLE)
fast_data_zi u8 rx_error;
share_data_zi volatile u32 RxErr_cnt;
#ifdef RXERR_IRQ_RETRAIN
share_data_zi volatile u8 retrain_cnt;
#define MAX_RETRAIN_CNT 20
#endif
#endif
fast_data_zi u32 transpend_cnt = 0;
fast_data_zi bool err_timer_flag = false;
fast_data_zi u32 loop_cnt;


extern bool cc_en_set;
extern tencnet_smart_statistics_t *tx_smart_stat; ///< controller Tencent SMART statistics
extern volatile u8 plp_trigger;
//fast_data_zi u8 suddent_link_down = 0;

#define PCIE_PHY_LOG		0      /// Enable PCIe phy log for debug
u32 link_retry_cnt = 0;
enum pcie_rx_lane_margin_sts_t
{
	RLM_TOO_MANY_ERR = 0,
	RLM_NOT_STARTED,
	RLM_IN_PROGRESS,
	RLM_NAK,
};

/**
* VPD template //joe 20200918 cv test
**/
typedef union
{
	u32 all;
	struct
	{
		u32 vpd_cap_id : 8;
		u32 nxt_cap_ptr : 8;
		u32 vpd_addr : 15;
		u32 vpd_flag : 1;
	} b;
} pcie_vpd_config_reg;

typedef union
{
	u32 all;
	struct
	{
		u32 vpd_data : 32;
	} b;
} pcie_vpd_data;

fast_data u8 evt_perst_hook = 2;

fast_data struct timer_list pcie_link_timer;
fast_data struct timer_list	pcie_abnormal_timer;
#ifdef RXERR_IRQ_RETRAIN
fast_data struct timer_list pcie_retrain_timer;
#endif
#if PCIE_PHY_LOG
slow_data struct timer_list pcie_phy_timer; /// PCIE phy timer to record phy error

slow_data u32 pcie_reg_phy_rcvry;
slow_data u32 pcie_phy_err_cnt = 0;
slow_data u32 pcie_recovery_cnt = 0;
#endif

fast_data u32 lane_chk_times = 0;	  //lane chk timer cnt
fast_data u32 lane_chk_bitmap = 0;	//lane chk type; Bit[0xF]:force lane1, Bit[0x10]:0 lane
fast_data bool pcie_timer_enable = 0; // For lane detection WA
fast_data u8 pcie_force_lane2 = 0;
fast_data u8 pcie_lane2_to_1 = 0;
fast_data u8 pcie_force_lane4 = 0;
fast_data u8 pcie_lane1_cnt = 0;
fast_data u32 lane_flag[16];
fast_data u64 pcie_time;
#define FORCE_LANE_2_CNT 30
#define PCIE_LANE_CHK_TIME	4000   /// 4ms
#define PCIE_RE_CHK_LANE	1		///1us
u32 timer_count_down(u32 us, void (*isr)(void));

extern void hal_nvmet_abort_xfer(bool abort);
extern void misc_nvm_ctrl_reset(bool reset);

/*!
 * @brief PCIe phy setting for gen3 rx bandwidth
 *
 * need to be called before ltssm in link_enable and preset function
 *
 * @return	not used
 */
void pcie_gen3_bandwidth(void);

/*!
 * @brief PCIe rx lane margin parameter settings.
 *
 * need to be called before ltssm in link_enable and preset function
 *
 * @return	not used
 */

void pcie_lane_margin_setting(void);

#ifdef PCIE_PHY_IG_SDK
/*
 * @brief PCIe Phy parameter to update RX squelch
 *
 * Adjust phy RX squelch for all lane
 *
 * @return	not used
 */
void pcie_phy_squelch_update(void);

/*
 * @brief PCIe Phy paramert for cdr relock whil1 1 bit error
 *
 * 0x01C= 0x0000_0000 (defaule0)
 * XCFG_DEC_ERRNUM_1	[13]
 * XCFG_DEC_ERRNUM_0	[12]
 * 00: 7 continues error
 * 01: 5 continues error
 * 10: 3 continues error
 * 11: 1 continues error

 * @return	not used
 */
void pcie_phy_cdr_relock(void);

/*
 * @brief PCIe Phy update DFE limit from 64 to 32
 *
 * Update DFE adaptive Algorithm settings from 64 to 32
 *
 * DFE Limit: The maximum range for the DFE search.
 * Set DFE algorithm adaption to a proper range. (64 -> 32)
 * Based on the circuit working range to set the DFE limit to improve the convergence.
 *
 * @return	not used
 */
void pcie_phy_update_dfe_lmt(void);

/*
 * @brief PCIe Phy pll lock time from 30 us to 100 us
 *
 * @return	not used
 */
void pcie_phy_pll_lock(void);

/*
 * @brief PCIe Phy enhance cdr lock flow
 *
 *
 *
 */
void pcie_phy_cdr_lock_enhance(void);

/*
 * @brief PCIe Phy keep TX common mode electrical on.
 *
 *  Keep TX common mode electrical on when PCIe in L1.1
 *
 */
void pcie_phy_l11_tx_commode_on(void);

/*
 * @brief PCIe phy setting to improve the capability of avoid refclk noise
 *
 * Phy setting to improve phy clk to avoid refclk noise
 */
void pcie_phy_refclksq_setting(void);

/*
 * @brief improve pcie phy rx lane margin
 *
 * Phy setting to adjust the lane margin center position.
 */
void pcie_phy_rlm_improve(void);

/*
 * @brief improve the capability of tx detect rx
 *
 * Phy setting to improve the capability of tx detect rx
 */
void pcie_phy_txdetrx_setting(void);
#endif

void pcie_link_width_config(u32 width);

void pcie_link_retrain(void);

void ig_pcie_link_retrain(u32 retrain_target_speed, u32 retrain_time_delay);
void pcie_speed_change(u32 speed);

#if defined(FORCE_GEN1) || defined(FORCE_GEN2) || defined(FORCE_GEN3)
void pcie_change_speed(void);
#endif
/*!
 * @brief PCIe core register related settings
 *
 * @return	not used
 */

void pcie_core_setting(void); //joe add test 20201007


#ifdef IG_MODIFIED_PHY
void pcie_gen34_preset(void);
void pcie_phy_leq_train(void);
void pcie_phy_cdr_setting(void);
#endif

//void pcie_tap1_setting();

/*!
 * @brief PCIe TDR settings
 *
 * Adjust phy TDR(Tx detect Rx) settings
 *
 * @return	not used
 */
void pcie_tdr_setting(void);
/*!
 * @brief PCIe Phy parameter to improve TX signal
 *
 * Adjust phy Tx swing 1010mV
 *
 * @return	not used
 */
void pcie_tx_update(void);
/*!
 * @brief PCIe Phy parameter to PLL bandwidth setting
 *
 * Adjust phy for gen3 & gen4 PLL fail
 *
 * @return	not used
 */
void pcie_pll_bw_update(void);
#ifdef SHORT_CHANNEL
/*!
 * @brief PCIe Phy parameter to improve short channel
 *
 * Adjust phy TEQ STEP/LEQ BW/EQ_bot lim/EQ_ac_offset/RX ODT setting
 *
 * @return	not used
 */
void pcie_short_channel_improve(void);
#endif

fast_code void pcie_dbglog(void);
static ddr_code void pcie_save_log(void *data);

static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}

static inline void misc_writel(u32 data, u32 reg)
{
	writel(data, (void *)(MISC_BASE + reg));
}

static inline u32 pcie_wrap_readl(u32 reg)
{
	return readl((void *)(PCIE_WRAP_BASE + reg));
}

static inline void pcie_wrap_writel(u32 data, u32 reg)
{
	writel(data, (void *)(PCIE_WRAP_BASE + reg));
}

/*!
 * @brief registers READ access
 *
 * @param reg  which register to access
 *
 * @return register value
 */
ddr_code void link_loss_flush_to_nand()
{
    pcie_dbglog();
    #if(CORR_ERR_INT == ENABLE)
    if(no_clk || no_perst || rx_error)
    #else
    if(no_clk || no_perst)
    #endif
    {
        if(no_clk)
        {
            tx_smart_stat->no_clk++;
        }
        if(no_perst)
        {
            tx_smart_stat->no_perst++;
        }
    #if(CORR_ERR_INT == ENABLE)
        if(rx_error)
        {
            rx_error = 0;
            tx_smart_stat->corr_err_cnt++;
        }
    #endif
        rtos_core_trace(LOG_ERR, 0xed0e, "No clk or perst");

        if(!plp_trigger)
            flush_to_nand(EVT_LINK_LOSS);
    }
}
static inline u32 pcie_core_readl(u32 reg)
{
#if 0//PLP_TEST == 1
	if(plp_trigger){
		rtos_core_trace(LOG_ERR, 0x273f, "doing plp, cant read pcie configuration");
	}
#endif
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return 0;
	}
#endif
	return readl((void *)(PCIE_CORE_BASE + reg));
}


/*!
 * @brief registers WRITE access
 *
 * @param val	the value to update
 * @param reg	which register to access
 *
 * @return		None
 */
static inline void pcie_core_writel(u32 data, u32 reg)
{
#if 0//PLP_TEST == 1
	if(plp_trigger){
		rtos_core_trace(LOG_ERR, 0x461f, "doing plp, cant write pcie configuration");
	}
#endif
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return;
	}
#endif
	writel(data, (void *)(PCIE_CORE_BASE + reg));
}

#if defined(M2_2A) || defined(U2_LJ)
/*!
 * @brief configure pcie phy register
 *
 * @param addr	register address
 * @param val   value to set
 *
 * @return	not used
 */
fast_code static void pcie_phy_cfg(u32 addr, u32 val)
{
	phy_data_t phy_data;
	phy_address_t phy_addr = {
		.all = 0,
	};

	phy_addr.b.phy_reg_address = addr;
	phy_data.b.phy_reg_data = val;
	misc_writel(phy_addr.all, PHY_ADDRESS);
	misc_writel(phy_data.all, PHY_DATA);
}
#endif


static ddr_code void pcie_save_log(void *data)
{
	#if(PLP_SUPPORT == 1)
	if(!plp_trigger || (gpio_get_value(GPIO_PLP_DETECT_SHIFT)))
	#endif
	{
		pcie_core_status2_t link_sts;
		link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);

		// pcie_abnormal_event  0: No event occur. 1: PCIe not 4 x 4 first.  2:  CTO timeout.  3: suddent link down event.
		if(pcie_abnormal_event == 1 && ((link_sts.b.neg_link_speed != PCIE_GEN4) || (link_sts.b.neg_link_width != PCIE_X4)))
		{
			pcie_abnormal_event = 0; 
			flush_to_nand(EVT_PCIE_STS_ERR);	//PCIe not four by four first, and still not four by four.
		}
		else if(pcie_abnormal_event>1)  // CTO timeout or link down occur.
		{
			pcie_abnormal_event = 0;
			flush_to_nand(EVT_PCIE_ABNORMAL);
		}
	}
	del_timer(&pcie_abnormal_timer);
}

fast_code void pcie_disable_base_address(void)
{

	/*
	1.    Set RC00430B0 = 0x8000_0000          //enable cs2 access
	2-0   Set RC0020030 = 0x0                  //disable expansion rom support
	2-1   Set RC0020020 = 0x0                  //disable bar4
	3.    Set RC00430B0 = 0x0                  //disable cs2 access
	*/

	r_axi_dbi_h16b_t axi_dbi;
	exp_rom_base_addr_reg_t pcie_exp_rom;
	u32 bar4;

	axi_dbi.all = pcie_wrap_readl(R_AXI_DBI_H16B);
	axi_dbi.b.r_axi_dbi_h16b = 0x8000;
	pcie_wrap_writel(axi_dbi.all, R_AXI_DBI_H16B);

	pcie_exp_rom.all = pcie_core_readl(EXP_ROM_BASE_ADDR_REG);
	pcie_exp_rom.all = 0;
	pcie_core_writel(pcie_exp_rom.all, EXP_ROM_BASE_ADDR_REG);

	bar4 = 0;
	pcie_core_writel(bar4, BAR4_REG);

	axi_dbi.all = pcie_wrap_readl(R_AXI_DBI_H16B);
	axi_dbi.b.r_axi_dbi_h16b = 0;
	pcie_wrap_writel(axi_dbi.all, R_AXI_DBI_H16B);
}

/*!
 * VPD(Vital Product Data)
 *
 * Ref: PCIe base spec rev.4.0 ver.1.0 6.28
 */
#ifdef ENABLE_VPD
/*!
 * VPD Configuration(Control and Capabilities) Reg
 */
#define VPD_CONFIG_REG		0xc00200d0
#define VPD_SZ			20
static u32 prod_name_sz = 5;
static u32 ro_sz = 8;

/*!
 * @brief Vital Product Data
 *
 * This is one time configuration.
 *
 * TODO: Fill in product data, now is fake data for test
 *
 */
static u32 vpd[VPD_SZ] =
{
	// Product Name
	0x49001182, // "I"
	0x474f4e4e, // "NNOG"
	0x5b544952, // "RIT["
	0x6e696152, // "Rain"
	0x5d726569, // "ier]"
	// RO
	0x50001d90, // PN: 12345678
	0x3231084e,
	0x36353433,
	0x4e533837, // SN: ABCDEFGH
	0x43424108,
	0x47464544,
	0x04565248, // RV
	0x00000011, // checksum: 0x11, 8-bit 2's complement from offset 0
	// RW
	0x56001591, // V1: abcdefg
	0x62610731,
	0x66656463,
	0x03315967, // Y1: N/A
	0x52412f4e, // RW
	0x00000257,
	// End
	0x00000078,
};

/*!
 * @brief handle VPD R/W from host
 *
 * @param not used
 *
 * @return not used
 */
static ddr_code void vpd_handler(void)
{
	u32 vpd_config = readl((void *)VPD_CONFIG_REG);
	u32 offset = ((vpd_config & ~BIT31) >> 16);
	u32 idx = offset >> 2;
	bool is_read = ((vpd_config & BIT31) == 0);

	if (idx > VPD_SZ - 1)
	{
		rtos_core_trace(LOG_ERR, 0xa183, "VPD access beyond VPD area");
		sys_assert(0);
	}

	if (is_read)
	{
		writel(vpd[idx], (void *)(VPD_CONFIG_REG + 4));
		writel(vpd_config | BIT31, (void *)VPD_CONFIG_REG);
	}
	else
	{
		if (idx > VPD_SZ - 2 || idx < prod_name_sz + ro_sz)
		{
			rtos_core_trace(LOG_ERR, 0x352a, "VPD write beyond RW area");
			sys_assert(0);
		}

		vpd[idx] = readl((void *)(VPD_CONFIG_REG + 4));
		writel(vpd_config & ~BIT31, (void *)VPD_CONFIG_REG);
	}
}
#endif // ENABLE_VPD

fast_code void pcie_intr_mask_init(void)
{
	u32 mask = ~0;

	/* only CPU 1 control PCIe */
	sys_assert(CPU_ID == 1);

	/* unmask link up & down interrupts */
	mask &= ~SMLH_LINK_UP_MASK;
	mask &= ~SMLH_LINK_DOWN_MASK;
	mask &= ~CFG_MEM_SPACE_DIS_MASK;
	mask &= ~CFG_MEM_SPACE_EN_MASK;
	mask &= ~SMLH_REQ_RST_NOT_MASK;
	#if(CORR_ERR_INT == ENABLE)
	//mask &= ~CFG_SEND_COR_ERR_MASK;
	#endif
	//mask &= ~CFG_SEND_NF_ERR_MASK;
	//mask &= ~CFG_SEND_F_ERR_MASK;

	if (gPcieRstRedirected == true)
	{
		#if defined(SRIOV_SUPPORT)
		mask &= ~CFG_FLR_VF_ACTIVE_MASK;
		#endif
		mask &= ~CFG_VPD_INT_MASK;
		mask &= ~CFG_FLR_PF_ACTIVE_MASK;
		//mask &= ~CFG_SEND_NF_ERR_MASK;
		///mask &= ~CFG_SEND_F_ERR_MASK;
		mask &= ~SLV_RD_CPL_TIMEOUT_MASK;
		//mask &= ~LINK_REQ_RST_NOT_MASK; // 3.1.7.4
	}

#define DEBUG_PCIE_ISR
#ifdef DEBUG_PCIE_ISR
	//mask &= 0;
#endif
	pcie_wrap_writel(mask, PCIE_INTR_MASK0);
}

fast_code u32 pcie_pipe_check(u32 *cmp_mask)
{
	u32 r3c = pcie_wrap_readl(PCIE_PIPE_STATUS);
	u32 cmp_ra4;
	u32 _cmp_mask;

	r3c = (r3c >> 16) & 0x000F;
	if (r3c == 0x1) { //x1
		cmp_ra4  = 0x40004;
		_cmp_mask = 0xF000F;
	} else if (r3c == 0x3) { //x2
		cmp_ra4  = 0x40044;
		_cmp_mask = 0xF00FF;
	} else { //x4
		cmp_ra4  = 0x44444;
		_cmp_mask = 0xFFFFF;
	}
	rtos_core_trace(LOG_ALW, 0x7fc6, "r3c %x, cmp_ra4 %x", r3c, cmp_ra4);
	if (cmp_mask)
		*cmp_mask = _cmp_mask;
	return cmp_ra4;
}

/*! Perst_n pin in active low status, so need to wait until change to high
 *
 * @param not used
 *
 * @return not used
 */
fast_code void pcie_wait_clear_perst_n(void)
{
	unm_sys_int_t unm_sys_int;
	reset_ctrl_t reset_ctrl;
#if(PLP_SUPPORT == 1)
	gpio_int_t gpio_int_status;
#endif
	//u32 count = 0;
	u32 timeout = 10000; // add 1s timeout, base SDK_J34 2024/1/8 shengbin yang.
	do
	{
		reset_ctrl.all = misc_readl(RESET_CTRL);
		//count++;
		//rtos_core_trace(LOG_INFO, 0, "albert1 wait for dead\n");
#if(PLP_SUPPORT == 1)
		gpio_int_status.all = misc_readl(GPIO_INT);

		if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
		{
			rtos_core_trace(LOG_INFO, 0xf804, "[PLP] IN IRQ Disable mode");
			gPlpDetectInIrqDisableMode = true;
			break;
		}
#endif
		timeout--;
		udelay(100);

	} while ((!(reset_ctrl.b.perst_n && reset_ctrl.b.int_perst_n))&&(timeout != 0));
	if(timeout == 0)
	{
		rtos_core_trace(LOG_ERR, 0xbd70, "wait perst_n time > 1s, perst_n(%d),int_perst_n(%d)",reset_ctrl.b.perst_n, reset_ctrl.b.int_perst_n);
	}

	/* If there is PERSTn happened, PERSTn takes ~10us delay
	 * from PERSTn Pin(RC004000C[31]) to PERSTn Interrupt bit(RC0040000[2:1]).
	 * So, adding 20us delay to make sure the interrupt bit can be clear correctly.
	 */
	udelay(20);

	//clear Misc assert /deassert
	unm_sys_int.all = misc_readl(UNM_SYS_INT);
	unm_sys_int.b.perst_asserted = 1;
	unm_sys_int.b.perst_deaserted = 1;
	misc_writel(unm_sys_int.all, UNM_SYS_INT);
}

fast_code void flr_post_handler(u32 reason)
{
	// just done FLR reset
	rtos_core_trace(LOG_ERR, 0x2442, "flr reset done");

	pcie_wrap_writel(1, PCIE_FLR_DNE_SET);
}
///Andy add for Tencent server issue 2020/10/06
fast_code static void pcie_core_enable_ro_write(bool enable)
{
	misc_control_1_off_t ctrl_reg = {.all = pcie_core_readl(MISC_CONTROL_1_OFF)};

	ctrl_reg.b.dbi_ro_wr_en = enable;
	pcie_core_writel(ctrl_reg.all, MISC_CONTROL_1_OFF); // #define MISC_CONTROL_1_OFF                       0x000008bc
}
/*
fast_code void pcie_header_reset(void)
{
	device_id_vendor_id_reg_t did_vid;
	//misc_control_1_off_t pcie_misc_ctrl;

	//pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	//pcie_misc_ctrl.b.dbi_ro_wr_en = 1;
	//pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);
	//to do more
	did_vid.all = pcie_core_readl(DEVICE_ID_VENDOR_ID_REG);
	did_vid.b.pci_type0_vendor_id = VID;
	did_vid.b.pci_type0_device_id = DID;
	pcie_core_writel(did_vid.all, DEVICE_ID_VENDOR_ID_REG);

	did_vid.all = pcie_core_readl(DEVICE_ID_VENDOR_ID_REG);
	rtos_core_trace(LOG_INFO, 0, "pcie vid %x, pcie did %x\n",
					did_vid.b.pci_type0_vendor_id, did_vid.b.pci_type0_device_id);

	//pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	//pcie_misc_ctrl.b.dbi_ro_wr_en = 0;
	//pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);
}
*/
///Andy add for Tencent server issue 2020/10/06
fast_code static UNUSED void pcie_disable_unuse_cfg(void)
{
	pci_lane_margin_ext_id_ver_next_reg_t LaneMarCap = {.all = pcie_core_readl(PCI_LANE_MARGIN_EXT_ID_VER_NEXT_REG)};

    /*correctable_err_mask_reg_t correctable_err_mask;
    correctable_err_mask.all = pcie_core_readl(CORRECTABLE_ERROR_MASK_REG);
    correctable_err_mask.all |= (~0); //only open bad tlp err isr*/

    /*device_control_device_status_t device_control_device_status = {.all = pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS)};
    device_control_device_status.all |= 0x07;
    rtos_core_trace(LOG_INFO, 0, "device_control_device_status %x ",device_control_device_status.all);*/
    /*u32 root_cmd = pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS);
    root_cmd |= 0x07;
    rtos_core_trace(LOG_INFO, 0, "root_cmd %x ",root_cmd);*/
	link_capabilities_reg_t PCIe_LINK_CAP = {.all = pcie_core_readl(LINK_CAPABILITIES_REG)};
	L1SubstateCapabili00_t l1SubstateCapabili = {.all = pcie_core_readl(L1SUB_CAPABILITY_REG)};

#ifndef ltr_support
	LaneMarCap.b.next_cap_off = PCI_DATA_LINK_FEATURE_EXT_CAP; //disable sriov , point to <unkown extended capability>
#else
	LaneMarCap.b.next_cap_off = PCIE_LTR_Capability_Register;
	pcie_ltr_support_head ltrs = {.all = pcie_core_readl(PCIE_LTR_Capability_Register)};
	ltrs.b.next = PCI_DATA_LINK_FEATURE_EXT_CAP;
/*
	pci_device_serial_number_ext_id_ver_next_reg_t device = {.all = pcie_core_readl(PCI_DEVICE_SERIAL_NUMBER_EXT_ID_VER_NEXT_REG)};
	device.b.next_cap_off = PCIE_LTR_Capability_Register;
	device.b.cap_version = 1;
	device.b.pci_ext_cap_id = 3;

	u32 _1stDW = pcie_core_readl(PCI_DEVICE_SERIAL_NUMBER_1ST_DW);
	_1stDW = 0x93382500;
	u32 _2scDW = pcie_core_readl(PCI_DEVICE_SERIAL_NUMBER_2SC_DW);
	_2scDW = 0x1842000;
*/
#endif


	subsystem_id_subsystem_vendor_id_reg_t sub_did_vid = {.all = pcie_core_readl(SUBSYSTEM_ID_SUBSYSTEM_VENDOR_ID_REG)};
	sub_did_vid.b.subsys_vendor_id = SSVID;
	sub_did_vid.b.subsys_dev_id = SSDID;

	pci_msix_cap_id_next_ctrl_reg_t MsixCap = {.all = pcie_core_readl(PCI_MSIX_CAP_ID_NEXT_CTRL_REG)};
	MsixCap.b.pci_msix_cap_next_offset = 0; 				   //disable VPD  , for pciecv_test
#if defined(SRIOV_SUPPORT)
	MsixCap.b.pci_msix_table_size = SRIOV_PF_VF_Q_PER_CTRLR;
#elif !defined(NVME_SHASTA_MODE_ENABLE)
	MsixCap.b.pci_msix_table_size = NVMET_RESOURCES_FLEXIBLE_TOTAL - 1 - 1;//admin queue and 0 base
#endif

	device_id_vendor_id_reg_t did_vid = {.all = pcie_core_readl(DEVICE_ID_VENDOR_ID_REG)};
	did_vid.b.pci_type0_vendor_id = VID;
	did_vid.b.pci_type0_device_id = DID;

	PCIe_LINK_CAP.b.pcie_cap_active_state_link_pm_support = 0;

	/* PCIe Spec 4.0r1.0 (5.4.1.1 L0s ASPM Stat)
	 * When a component does not advertise that it supports L0s, as indicated by its ASPM Support field
	 * value being 00b or 10b, it is recommended that the component?�s L0s Exit Latency field return a
	 * value of 111b, indicating the maximum latency range.
	 */
	PCIe_LINK_CAP.b.pcie_cap_l0s_exit_latency = 7;  // add shengbin yang.2023/8/31

	// L1 Substate Capability
	{
		// Broadcast to host L1 Sub State capability
		l1SubstateCapabili.b.L1PM_SUPT      = 0;    //L1 sub state support
		l1SubstateCapabili.b.PCIPM_L11_SUPT = 0;    //PCI-PM L1.1 support
		l1SubstateCapabili.b.PCIPM_L12_SUPT = 0;    //PCI-PM L1.2 support
		l1SubstateCapabili.b.ASPM_L11_SUPT  = 0;    //ASPM L1.1 support
		l1SubstateCapabili.b.ASPM_L12_SUPT  = 0;    //ASPM L1.2 support
	}

	pcie_core_enable_ro_write(true);
	//pcie_header_reset();

	#ifdef LONG_CPL_TO
	device_control2_device_status2_reg_t dc2ds = {.all = pcie_core_readl(DEVICE_CONTROL2_DEVICE_STATUS2_REG)};
	//dc2ds.b.pcie_cap_cpl_timeout_value = 0xD;// 5.4~8.2sec for Rainier
	dc2ds.b.pcie_cap_cpl_timeout_value = 0x9;// 260~900msec for Rainier
	pcie_core_writel(dc2ds.all, DEVICE_CONTROL2_DEVICE_STATUS2_REG);
	#endif
#ifdef ltr_support
	pcie_core_writel(ltrs.all, PCIE_LTR_Capability_Register);

/*
	pcie_core_writel(device.all, PCI_DEVICE_SERIAL_NUMBER_EXT_ID_VER_NEXT_REG);
	pcie_core_writel(_1stDW, PCI_DEVICE_SERIAL_NUMBER_1ST_DW);
	pcie_core_writel(_2scDW, PCI_DEVICE_SERIAL_NUMBER_2SC_DW);
*/
#endif
	//pcie_core_writel(correctable_err_mask.all, CORRECTABLE_ERROR_MASK_REG);
	/*pcie_core_writel(device_control_device_status.all, DEVICE_CONTROL_DEVICE_STATUS);

	device_control_device_status.all = pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS);
	rtos_core_trace(LOG_INFO, 0, "device_control_device_status.all %x ",device_control_device_status.all);*/
	/*if(cc_en_set == false)
	{
		pcie_core_writel(correctable_err_mask.all, CORRECTABLE_ERROR_MASK_REG);
		correctable_err_mask.all = pcie_core_readl(CORRECTABLE_ERROR_MASK_REG);
		rtos_core_trace(LOG_INFO, 0, "cc en not set and correctable_err_mask%x ",correctable_err_mask.all);
	}*/
/*
	pcie_core_writel(PM_header.all, PM_CAP_ID_NXT_PTR_REG);
	pcie_core_writel(sub_did_vid.all, SUBSYSTEM_ID_SUBSYSTEM_VENDOR_ID_REG);
*/
	pcie_core_writel(sub_did_vid.all, SUBSYSTEM_ID_SUBSYSTEM_VENDOR_ID_REG);

	pcie_core_writel(did_vid.all, DEVICE_ID_VENDOR_ID_REG);
	did_vid.all = pcie_core_readl(DEVICE_ID_VENDOR_ID_REG);
	//rtos_core_trace(LOG_INFO, 0, "pcie vid %x, pcie did %x\n",
	//				did_vid.b.pci_type0_vendor_id, did_vid.b.pci_type0_device_id);

	// JackLi for PCBasher
	bar0_reg_t bar0_reg = {.all = pcie_core_readl(BAR0_REG)};
	bar0_reg.b.bar0_start_1 = 0;
	pcie_core_writel(bar0_reg.all, BAR0_REG);
	//pcie_bar0_starth_t bar0u = {.all = pcie_wrap_readl(PCIE_BAR0_STARTH)};
	//pcie_bar0_startl_t bar0l = {.all = pcie_wrap_readl(PCIE_BAR0_STARTL)};
	//rtos_core_trace(LOG_INFO, 0, "Chk PCIe BAR0: %x%x ", bar0u.all, bar0l.all);

	pcie_core_writel(MsixCap.all, PCI_MSIX_CAP_ID_NEXT_CTRL_REG);
	MsixCap.all = pcie_core_readl(PCI_MSIX_CAP_ID_NEXT_CTRL_REG);
	//rtos_core_trace(LOG_INFO, 0, "msi_x table size %x\n",
	//				MsixCap.b.pci_msix_table_size);

	pcie_core_writel(LaneMarCap.all, PCI_LANE_MARGIN_EXT_ID_VER_NEXT_REG);

	pcie_core_writel(PCIe_LINK_CAP.all, LINK_CAPABILITIES_REG);

	pcie_core_writel(l1SubstateCapabili.all, L1SUB_CAPABILITY_REG);

	//pcie_core_writel(device_cap2.all, DEVICE_CAPABILITIES2_REG);
	//device_cap2.all = pcie_core_readl(DEVICE_CAPABILITIES2_REG);
	//rtos_core_trace(LOG_INFO, 0, "LTRS %x\n",
					//device_cap2.b.pcie_cap_ltr_supp);

#if 0 //LINK_MAX_SPEED_Gen3 == TRUE
	link_capabilities_reg_t LinkCap = { .all = pcie_core_readl(LINK_CAPABILITIES_REG)};
	link_control2_link_status2_reg_t link_sts = {.all = pcie_core_readl(LINK_CONTROL2_LINK_STATUS2_REG)};
	LinkCap.b.pcie_cap_max_link_speed = 3;
	link_sts.b.pcie_cap_target_link_speed = 3;
	pcie_core_writel(LinkCap.all, LINK_CAPABILITIES_REG);
	pcie_core_writel(link_sts.all, LINK_CONTROL2_LINK_STATUS2_REG);
#endif
	/* configure aux power detect */
	misc_ana_ctrl_t misc_ana;
	misc_ana.all = misc_readl(MISC_ANA_CTRL);
	misc_ana.b.aux_pwr_det = 0;
	misc_writel(misc_ana.all, MISC_ANA_CTRL);
	//--------------------------------------
	pcie_core_enable_ro_write(false);
}

fast_code void pcie_cfg_lane_number(u32 number)
{
	misc_control_1_off_t pcie_misc_ctrl;

	pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	pcie_misc_ctrl.b.dbi_ro_wr_en = 1;
	pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);
	link_capabilities_reg_t link_cap;
	link_cap.all = pcie_core_readl(LINK_CAPABILITIES_REG);
	link_cap.b.pcie_cap_max_link_width = number;
	pcie_core_writel(link_cap.all, LINK_CAPABILITIES_REG);

	pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	pcie_misc_ctrl.b.dbi_ro_wr_en = 0;
	pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);
}

fast_code void pcie_force_2lane(void)
{
#if 0
	test_ctrl_t test_ctrl;
	test_ctrl.all = readl((void *)0xc0040134);
	test_ctrl.b.force_pcie_lane_width = 1;
	test_ctrl.b.pcie_lane_width = 1; // force 2 lane
	writel(test_ctrl.all, (void *)0xc0040134);
#else

	u32 r130 = readl((void *)0xc0040130);
	r130 = r130 >> 16;
	r130 |= BIT16;
	r130 &= ~(BIT7 | BIT8); //GPIO7/8 - 00b X1, 01b X2, 10b X4
	r130 |= BIT7;
	writel(r130, (void *)0xc004003c);

	//pcie_cfg_lane_number(2);
#endif
}

fast_code void pcie_force_1lane(void)
{
#if 0
	test_ctrl_t test_ctrl;
	test_ctrl.all = readl((void *)0xc0040134);
	test_ctrl.b.force_pcie_lane_width = 1;
	test_ctrl.b.pcie_lane_width = 0; // force 1 lane
	writel(test_ctrl.all, (void *)0xc0040134);
#else
	u32 r130 = readl((void *)0xc0040130);
	r130 = r130 >> 16;
	r130 |= BIT16;
	r130 &= ~(BIT7 | BIT8);
	writel(r130, (void *)0xc004003c);
	//pcie_cfg_lane_number(1);
#endif
}

fast_code void pcie_force_4lane(void)
{
#if 0
	test_ctrl_t test_ctrl;
	test_ctrl.all = readl((void *)0xc0040134);
	test_ctrl.b.force_pcie_lane_width = 1;
	test_ctrl.b.pcie_lane_width = 2; // force 4 lane
	writel(test_ctrl.all, (void *)0xc0040134);
#else
	u32 r130 = readl((void *)0xc0040130);
	r130 = r130 >> 16;
	r130 |= BIT16;
	r130 &= ~(BIT7 | BIT8); //GPIO7/8 - 00b X1, 01b X2, 10b X4

	r130 |= BIT8;
	writel(r130, (void *)0xc004003c);

	//pcie_cfg_lane_number(4);
#endif
}

fast_code void pcie_lane_chk(void)
{
	pcie_core_status_t core_sts;
	pcie_pipe_status_t pipe_sts;
	pcie_control_reg_t pcie_ctrl;
	u32 tmp_cnt=0;
	
	lane_chk_times++;
	if(lane_chk_times == 1)
	{
		pcie_time = get_tsc_64();
	}
	if(pcie_timer_enable == 1)
	{
		pcie_timer_enable = 0;
		share_timer_irq_disable();
	}
#if(PLP_SUPPORT == 1)
	gpio_int_t gpio_int_status;
	gpio_int_status.all = misc_readl(GPIO_INT);
	if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger)
		return ;
#endif
	core_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS);
	pcie_ctrl.all = pcie_wrap_readl(PCIE_CONTROL_REG);
	pipe_sts.all = 0;
	
	if ((core_sts.b.smlh_ltssm_state == 0x11))
	{
		pcie_force_4lane();
		return;
	}
	tmp_cnt = lane_chk_times / 1000; // times ≈ us; tmp_cnt = times/1000 ≈ ms
	if((pcie_force_lane4==0)&&(tmp_cnt > 500))
	{
		pcie_force_lane4 = 1;
		pcie_force_4lane();
		pcie_timer_enable = 1;
		timer_count_down(PCIE_LANE_CHK_TIME, pcie_lane_chk);
		return;
	}
	tmp_cnt = tmp_cnt / 1000; // tmp_cnt ≈ us; tmp_cnt/1000 ≈ s
#if (Tencent_case)
	if(tmp_cnt >= 120)
#else
	if(tmp_cnt >= 10)
#endif
	{
		pcie_force_4lane();
#if (Tencent_case)
		rtos_core_trace(LOG_ALW, 0xce06,"time > 120s, lane_chk_bitmap(0x%x),pcie_force_lane2(%d),lane2_to_1(%d),lane_1(%d)",lane_chk_bitmap,pcie_force_lane2,pcie_lane2_to_1,pcie_lane1_cnt);
#else
		rtos_core_trace(LOG_ALW, 0x61a7,"time > 10s, lane_chk_bitmap(0x%x),pcie_force_lane2(%d),lane2_to_1(%d),lane_1(%d)",lane_chk_bitmap,pcie_force_lane2,pcie_lane2_to_1,pcie_lane1_cnt);
#endif
		mod_timer(&pcie_abnormal_timer, jiffies + 10);
		pcie_dbglog();
		return;
	}


	if (core_sts.b.smlh_ltssm_state == 0x2 || core_sts.b.smlh_ltssm_state == 0x3) {
		pipe_sts.all = pcie_wrap_readl(PCIE_PIPE_STATUS);

		u32 lane = (pipe_sts.all >> 4) & 0xF;		
		//rtos_core_trace(LOG_ALW, 0xd1e7, "force to #lane 0x%x", lane);
		//printk("force to #lane 0x%x,\n", lane);
		if ((lane & 0xF) == 0xF) {  // Keep timer
			lane_chk_bitmap |= BIT(0x10);
			pcie_timer_enable = 1;
			//timer_count_down(PCIE_LANE_CHK_TIME, pcie_lane_chk);
			lane_chk_times += PCIE_RE_CHK_LANE;
			timer_count_down(PCIE_RE_CHK_LANE, pcie_lane_chk);
			return;
		} else {
			u32 fixed_lane = 0;
			u32 lane_stable_cnt = 0;
			// 1. CHeck if pipe status is true 0


			do {
				pipe_sts.all = pcie_wrap_readl(PCIE_PIPE_STATUS);
				fixed_lane = (pipe_sts.all >> 4) & 0xF;
#if 1
				if (fixed_lane == lane) {
					lane_stable_cnt++;
				}else {
					pcie_timer_enable = 1;
					lane_chk_times += PCIE_RE_CHK_LANE;
					timer_count_down(PCIE_RE_CHK_LANE, pcie_lane_chk);
					return;
				}
				if (lane_stable_cnt == 16) {
					//rtos_core_trace(LOG_ALW, "lane stable, continue");
					break;
				}
#endif
			} while(1);

			lane_chk_bitmap |= BIT(lane);
			pcie_timer_enable = 1;
			lane_chk_times += PCIE_RE_CHK_LANE;
			timer_count_down(PCIE_RE_CHK_LANE, pcie_lane_chk);
			if (((lane & 0xF) == 0xC) || ((lane & 0xF) == 0x8) || ((lane & 0xF) == 0x4)) {
				//lane0/Lane1 active, but lane2/lane3 has term
				if(pcie_force_lane2++ > FORCE_LANE_2_CNT)
				{
					lane_chk_bitmap |= BIT(0xF);
					pcie_force_lane2=0;
					pcie_force_1lane();
					pcie_lane2_to_1++;
					pcie_lane1_cnt++;
					return;
				}
				pcie_force_2lane();
				//rtos_core_trace(LOG_ALW, 0x9a2e, "enable ltssm x2");
			} else if (((lane & 0xF) == 0xE) || ((lane & 0xF) == 0x6) || ((lane & 0xF) == 0xA)|| ((lane & 0xF) == 0x2)) {
				// lane0 active, but lane1/lane2/lane3 has term
				pcie_force_1lane();
				pcie_lane1_cnt++;
				//pcie_term_wa_enabled = 1;
				//rtos_core_trace(LOG_ALW, 0x9a2f, "enable ltssm x1");
				/* Lane reversal case */
			} else if (((lane & 0xF) == 0x3) || ((lane & 0xF) == 0x1)) {
				/*if((lane_flag[0] < lane_flag[3] ? (lane_flag[3] - lane_flag[0]):(lane_flag[0] - lane_flag[3])) < 50){
					pcie_force_1lane();
					return;
				}*/
				if(pcie_force_lane2++ > FORCE_LANE_2_CNT)
				{
					lane_chk_bitmap |= BIT(0xF);
					pcie_force_lane2=0;
					pcie_lane2_to_1++;
					pcie_force_1lane();
					pcie_lane1_cnt++;
					return;
				}
				// lane2/Lane3 active, but lane0/lane1 has term
				pcie_ctrl.all = pcie_wrap_readl(PCIE_CONTROL_REG);
				pcie_ctrl.b.tx_lane_flip_en = 1;
				pcie_ctrl.b.rx_lane_flip_en = 1;
				pcie_wrap_writel(pcie_ctrl.all, PCIE_CONTROL_REG);
				pcie_force_2lane();
				//rtos_core_trace(LOG_ALW, 0xa21c, "enable ltssm rvs x2");
				//printk( "enable ltssm rvs x2 \n");
			} else if (((lane & 0xF) == 0x7) || ((lane & 0xF) == 0x5) ){
				// lane3 active, but lane0/lane1/lane2 has term
				pcie_ctrl.all = pcie_wrap_readl(PCIE_CONTROL_REG);
				pcie_ctrl.b.tx_lane_flip_en = 1;
				pcie_ctrl.b.rx_lane_flip_en = 1;
				pcie_wrap_writel(pcie_ctrl.all, PCIE_CONTROL_REG);
				pcie_lane1_cnt++;
				pcie_force_1lane();
				//pcie_term_wa_enabled = 1;
				//rtos_core_trace(LOG_ALW, 0xa21f, "enable ltssm rvs x1");
			}
		

		}
	}
	else
	{
		pcie_timer_enable = 1;
		lane_chk_times += PCIE_RE_CHK_LANE;
		timer_count_down(PCIE_RE_CHK_LANE, pcie_lane_chk);
	}

}


/*!
 * @brief use 25MHz timer to do timer count down, now this api is only used to warm boot check
 *        please don't call irq_enable in irq mode, or it will cause nest irq
 * @param us		timerout value in us, if us is 0, request elapsed time
 * @param isr		isr to check something
 *
 * @return		return elapsed time after calculating timer value * prescale
 */
fast_code u32 timer_count_down(u32 us, void (*isr)(void))
{
	if (us) {
		shared_timer0_value_t val;
		shared_timer0_ctrl_t ctrl;
		u32 e = 1;
		u32 f = us;

		while (f >= 65536) {
			f >>= 1;
			e <<= 1;
		}

		ctrl.all = 0;
		misc_writel(ctrl.all, SHARED_TIMER0_CTRL);
		val.b.shr_timer0_cur_val = f;
		val.b.shr_timer0_rel_val = f;
		misc_writel(val.all, SHARED_TIMER0_VALUE);
		ctrl.b.shr_timer0_mode = 0;
		ctrl.b.shr_timer0_load = 0;
		ctrl.b.shr_timer0_prescale = (25 - 1) * e;	/// 1 * e us
		ctrl.b.shr_timer0_start = 1;
		if (isr) {
			ctrl.b.shr_timer0_int_en = 1;
			sirq_register(SYS_VID_ST, isr, false);
			misc_sys_isr_enable(SYS_VID_ST);
			/* please don't call irq_enable in irq mode, or it will cause nest irq */
		}

		misc_writel(ctrl.all, SHARED_TIMER0_CTRL);
		return (f * e);
	} else {
		shared_timer0_value_t val;
		shared_timer0_ctrl_t ctrl;
		u32 e;

		val.all = misc_readl(SHARED_TIMER0_VALUE);
		ctrl.all = misc_readl(SHARED_TIMER0_CTRL);
		e = ctrl.b.shr_timer0_prescale / 24;
		return (val.b.shr_timer0_rel_val - val.b.shr_timer0_cur_val) * e;
	}
}

fast_code void pcie_rst_post(u32 reason)
{

	pcie_unmsk_intr_t pcie_int;

	pcie_intr_mask_init();

	pcie_control_reg_t pcie_ctrlr = {
		.all = pcie_wrap_readl(PCIE_CONTROL_REG),
	};

	del_timer(&pcie_link_timer); // delete timer for dfe and leq to avoid re-entry
#ifdef RXERR_IRQ_RETRAIN
	del_timer(&pcie_retrain_timer);	
#endif
	extern void nvmet_pcie_config_resume(void);
	nvmet_pcie_config_resume();
	pcie_wait_clear_perst_n();

	if (gPlpDetectInIrqDisableMode)
	{
		gPlpDetectInIrqDisableMode = false;
		return;
	}

	//pcie_tdr_setting();
#ifdef SHORT_CHANNEL
	pcie_short_channel_improve();
#endif

#ifdef IG_MODIFIED_PHY
	//pcie_tdr_setting();
#ifndef PCIE_PHY_IG_SDK
	pcie_tx_update();
#endif
	pcie_pll_bw_update();
#ifdef PCIE_PHY_IG_SDK
	pcie_tx_update();
#endif
	//pcie_gen3_bandwidth();
	pcie_phy_leq_train();
	pcie_phy_cdr_setting();
#ifdef PCIE_PHY_IG_SDK
	pcie_phy_squelch_update();
	pcie_phy_cdr_relock();
	pcie_phy_pll_lock();
	pcie_phy_cdr_lock_enhance();
	pcie_phy_l11_tx_commode_on();
	pcie_phy_update_dfe_lmt();
	pcie_phy_refclksq_setting();
	pcie_phy_rlm_improve();
	pcie_phy_txdetrx_setting();
#endif

	misc_reset(RESET_PCIE_PHY);

	#ifdef PCIE_TDR_LOOSE
	pcie_tdr_setting_loose();
	#endif

	#if !defined(FPGA)
	if (is_pcie_clk_enable())
	{ // add inno patch for eq problem 20201007
		pcie_lane_margin_setting();
		pcie_core_setting(); //add inno patch 20201007
		pcie_gen34_preset();
		pcie_disable_unuse_cfg();
		pcie_disable_base_address();
#if GET_PCIE_ERR
		pcie_perst_flag = 0;
#endif
	}
	#endif

	//rtos_core_trace(LOG_ERR, 0xfc54, "IG PCIE WORKING");
#else
	//pcie_tdr_setting();
	pcie_tx_update();
	pcie_pll_bw_update();
	pcie_gen3_bandwidth();

	#if !defined(FPGA)
		if (is_pcie_clk_enable())
		{ // add inno patch for eq problem 20201007
			pcie_lane_margin_setting();
			pcie_core_setting(); //add inno patch 20201007
			pcie_disable_unuse_cfg();
			pcie_disable_base_address();
#if GET_PCIE_ERR
		pcie_perst_flag = 0;
#endif
		}
	#endif
#endif

	//pcie_wait_clear_perst_n();
#ifdef ASPM_REG_SETTING
#if defined(MPC)
	//pcie_ctrlr.b.app_xfer_pending = 1;
	pcie_ctrlr.b.app_l1sub_disable = 1;
#endif
#endif
	/* Enable PCIe log */
	pcie_fsm_log_sel_t pcie_log = {
		 .all = pcie_wrap_readl(PCIE_FSM_LOG_SEL),
	};
	pcie_log.b.pcie_log_en = 1;
	pcie_wrap_writel(pcie_log.all, PCIE_FSM_LOG_SEL);

	/* Clear request reset pcie isr before ltssm_enable*/
	pcie_int.all = pcie_wrap_readl(PCIE_UNMSK_INTR);
	pcie_int.b.smlh_req_rst_not = 1;
	pcie_wrap_writel(pcie_int.all, PCIE_UNMSK_INTR);
#if defined(FORCE_GEN1) || defined(FORCE_GEN2) || defined(FORCE_GEN3)
	pcie_change_speed();
#endif

	pcie_ctrlr.b.app_ltssm_enable = 1;
	pcie_ctrlr.b.auto_ltssm_clr_en = 1;
	pcie_ctrlr.b.app_margining_ready = 1;		  /* Enable Margin Ready bit for host tool rx lane margin test*/
	pcie_ctrlr.b.cfg_10bits_tag_comp_support = 1; // joe for CV test 20200918
	pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);
	rtos_core_trace(LOG_ERR, 0xcaf1, "%s: LTSSM_EN(%d),clk_loop_cnt(%d)", __FUNCTION__, pcie_ctrlr.b.app_ltssm_enable,loop_cnt);

	lane_chk_times = 0;
	lane_chk_bitmap=0;
	pcie_force_lane2=0;
	pcie_lane2_to_1=0;
	pcie_lane1_cnt=0;
	//lane_chk_err_ltssm_en = 0;
	pcie_timer_enable = 1;
	pcie_force_lane4=0;
	pcie_force_4lane();
	timer_count_down(PCIE_LANE_CHK_TIME, pcie_lane_chk);

#if PCIE_PHY_LOG
	mod_timer(&pcie_phy_timer, jiffies + HZ);
#endif
}

#if defined(FORCE_GEN1) || defined(FORCE_GEN2) || defined(FORCE_GEN3)
fast_code void pcie_change_speed(void)
{
	u32 pcie_write_ro;
	u8 speed;

	#ifdef FORCE_GEN1
		speed = BIT0;
	#elif defined(FORCE_GEN2)
		speed = BIT1;
	#else // FORCE_GEN3
		speed = (BIT0 | BIT1);
	#endif

	pcie_write_ro = readl((void *)0xC00208BC);
	pcie_write_ro |= BIT(0);
	writel(pcie_write_ro, (void *)0xC00208BC);

	u32 link_speed;
	u32 link_target;
	link_speed = readl((void *)0xc002007C);
	link_speed &= 0xFFFFFFF0;
	link_speed |= speed;
	writel(link_speed, (void *)0xc002007C);

	link_target = readl((void *)0xc00200A0);
	link_target &= 0xFFFFFFF0;
	link_target |= speed;
	writel(link_target, (void *)0xc00200A0);
	printk("force gen%d.\n", speed);

	pcie_write_ro &= ~BIT(0);
	writel(pcie_write_ro, (void *)0xC00208BC);
}
#endif

fast_code static u32 pcie_phy_sts(u32 addr)
{
	u32 data;
	phy_address_t phy_addr = {
		.all = 0,
	};

	phy_addr.b.phy_reg_address = addr;
	misc_writel(phy_addr.all, PHY_ADDRESS);
	data = misc_readl(PHY_DATA);

	return data;
}

ddr_code void pcie_phy_leq_dfe()//joe slow->ddr 20201124
{
	u32 r204 = 0;
	u32 r208 = 0;
	u32 r220 = 0;
	u32 r224 = 0;
	u32 r23c = 0;
	u32 r240 = 0;
	u32 r258 = 0;
	u32 r25c = 0;
	u32 lane0_leq;
	u32 lane1_leq;
	u32 lane2_leq;
	u32 lane3_leq;

	u32 lane0_dfe;
	u32 lane1_dfe;
	u32 lane2_dfe;
	u32 lane3_dfe;

	r204 = pcie_phy_sts(0x204);
	r208 = pcie_phy_sts(0x208);
	r220 = pcie_phy_sts(0x220);
	r224 = pcie_phy_sts(0x224);
	r23c = pcie_phy_sts(0x23c);
	r240 = pcie_phy_sts(0x240);
	r258 = pcie_phy_sts(0x258);
	r25c = pcie_phy_sts(0x25c);

	lane0_leq = (r204 & (BIT4 | BIT5 | BIT6 | BIT7 | BIT8)) >> 4;
	lane1_leq = (r220 & (BIT4 | BIT5 | BIT6 | BIT7 | BIT8)) >> 4;
	lane2_leq = (r23c & (BIT4 | BIT5 | BIT6 | BIT7 | BIT8)) >> 4;
	lane3_leq = (r258 & (BIT4 | BIT5 | BIT6 | BIT7 | BIT8)) >> 4;

	lane_leq[0] = lane0_leq;
	lane_leq[1] = lane1_leq;
	lane_leq[2] = lane2_leq;
	lane_leq[3] = lane3_leq;

	u32 dfe_mask = (BIT25 | BIT26 | BIT27 | BIT28 | BIT29 | BIT30 | BIT31);

	lane0_dfe = ((r204 & dfe_mask) >> 25) | ((r208 & BIT0) << 7);
	lane1_dfe = ((r220 & dfe_mask) >> 25) | ((r224 & BIT0) << 7);
	lane2_dfe = ((r23c & dfe_mask) >> 25) | ((r240 & BIT0) << 7);
	lane3_dfe = ((r258 & dfe_mask) >> 25) | ((r25c & BIT0) << 7);

	lane_dfe[0] = lane0_dfe;
	lane_dfe[1] = lane1_dfe;
	lane_dfe[2] = lane2_dfe;
	lane_dfe[3] = lane3_dfe;

	if ((r208 & BIT0) == 1)
	{
		lane0_dfe = ~lane0_dfe + 1;
	}
	if ((r224 & BIT0) == 1)
	{
		lane1_dfe = ~lane1_dfe + 1;
	}
	if ((r240 & BIT0) == 1)
	{
		lane2_dfe = ~lane2_dfe + 1;
	}
	if ((r25c & BIT0) == 1)
	{
		lane3_dfe = ~lane3_dfe + 1;
	}

	rtos_core_trace(LOG_ALW, 0x013a, "LEQ criteria : Delta LEQ < 4");
	rtos_core_trace(LOG_ALW, 0xbce4, " LEQ 0[%d] 1[%d] 2[%d] 3[%d]",
					lane0_leq, lane1_leq, lane2_leq, lane3_leq);

	rtos_core_trace(LOG_ALW, 0x08d4, "DFE criteria : Delta DFE < 25");
	if (((r208 & BIT0) == 1) && ((r208 & BIT0) == 1) &&
		((r208 & BIT0) == 1) && ((r208 & BIT0) == 1))
	{
		rtos_core_trace(LOG_ALW, 0x0758, " DFE 0[-%d] 1[-%d] 2[-%d] 3[-%d]",
						(u8)lane0_dfe, (u8)lane1_dfe, (u8)lane2_dfe, (u8)lane3_dfe);
	}
	else
	{
		rtos_core_trace(LOG_ALW, 0xfa56, " DFE 0[%d] 1[%d] 2[%d] 3[%d]",
						(u8)lane0_dfe, (u8)lane1_dfe, (u8)lane2_dfe, (u8)lane3_dfe);
	}
}

ddr_code void pcie_phy_cali_results()//joe slow->ddr 20201124
{
	u32 r218;
	u32 r234;
	u32 r250;
	u32 r26c;
	u32 r274;
	u32 tr_mask;
	u32 imp_mask;

	u8 lane0_rc;
	u8 lane1_rc;
	u8 lane2_rc;
	u8 lane3_rc;
	u16 pll_kr; // pll calibration result

	u32 r200 = 0;
	u32 r204 = 0;
	u32 r21c = 0;
	u32 r220 = 0;
	u32 r238 = 0;
	u32 r23c = 0;
	u32 r254 = 0;
	u32 r258 = 0;

	u32 lane0_cdr;
	u32 lane1_cdr;
	u32 lane2_cdr;
	u32 lane3_cdr;

	r218 = pcie_phy_sts(0x218);
	r234 = pcie_phy_sts(0x234);
	r250 = pcie_phy_sts(0x250);
	r26c = pcie_phy_sts(0x26c);
	r274 = pcie_phy_sts(0x274);

	tr_mask = (BIT7 | BIT8 | BIT9 | BIT10 | BIT11 | BIT12 | BIT13 | BIT14);
	imp_mask = (BIT4 | BIT5 | BIT6 | BIT7 | BIT8 | BIT9 | BIT10 | BIT11 | BIT12);

	lane0_rc = (r218 & tr_mask) >> 7;
	lane1_rc = (r234 & tr_mask) >> 7;
	lane2_rc = (r250 & tr_mask) >> 7;
	lane3_rc = (r26c & tr_mask) >> 7;

	pll_kr = (r274 & imp_mask) >> 4;
	shr_pll_kr = pll_kr;

	r200 = pcie_phy_sts(0x200);
	r204 = pcie_phy_sts(0x204);
	r21c = pcie_phy_sts(0x21c);
	r220 = pcie_phy_sts(0x220);
	r238 = pcie_phy_sts(0x238);
	r23c = pcie_phy_sts(0x23c);
	r254 = pcie_phy_sts(0x254);
	r258 = pcie_phy_sts(0x258);

	lane0_cdr = (r204 & (BIT0 | BIT1 | BIT2 | BIT3)) << 5;
	lane0_cdr |= (r200 >> 27);

	lane1_cdr = (r220 & (BIT0 | BIT1 | BIT2 | BIT3)) << 5;
	lane1_cdr |= (r21c >> 27);

	lane2_cdr = (r23c & (BIT0 | BIT1 | BIT2 | BIT3)) << 5;
	lane2_cdr |= (r238 >> 27);

	lane3_cdr = (r258 & (BIT0 | BIT1 | BIT2 | BIT3)) << 5;
	lane3_cdr |= (r254 >> 27);

	lane_cdr[0] = lane0_cdr;
	lane_cdr[1] = lane1_cdr;
	lane_cdr[2] = lane2_cdr;
	lane_cdr[3] = lane3_cdr;

	rtos_core_trace(LOG_ALW, 0x89d5, "PLL_BAND criteria : 150 < PLL_BAND < 400");
	rtos_core_trace(LOG_ALW, 0x1782, " PLL_BAND [%d]", pll_kr);

	rtos_core_trace(LOG_ALW, 0x4267, "CDR_BAND criteria : 80 < CDR_BAND < 300");
	rtos_core_trace(LOG_ALW, 0x801a, " CDR_BAND 0[%d] 1[%d] 2[%d] 3[%d]",
					lane0_cdr, lane1_cdr, lane2_cdr, lane3_cdr);

	rtos_core_trace(LOG_ALW, 0x77cf, "DAC criteria : -110 < DAC < 110");
	if (((r218 & BIT14) >> 14) == 1)
	{
		lane_rc[0] = lane0_rc;
		lane0_rc = (~lane0_rc) + 1;
		rtos_core_trace(LOG_ALW, 0x9b84, " DAC 0[-%d]", lane0_rc);
	}
	else
	{
		lane_rc[0] = lane0_rc;
		rtos_core_trace(LOG_ALW, 0x6077, " DAC 0[%d]", lane0_rc);
	}

	if (((r234 & BIT14) >> 14) == 1)
	{
		lane_rc[1] = lane1_rc;
		lane1_rc = (~lane1_rc) + 1;
		rtos_core_trace(LOG_ALW, 0x1a9c, " DAC 1[-%d]", lane1_rc);
	}
	else
	{
		lane_rc[1] = lane1_rc;
		rtos_core_trace(LOG_ALW, 0x8a8c, " DAC 1[%d]", lane1_rc);
	}

	if (((r250 & BIT14) >> 14) == 1)
	{
		lane_rc[2] = lane2_rc;
		lane2_rc = (~lane2_rc) + 1;
		rtos_core_trace(LOG_ALW, 0xe1fc, " DAC 2[-%d]", lane2_rc);
	}
	else
	{
		lane_rc[2] = lane2_rc;
		rtos_core_trace(LOG_ALW, 0xe560, " DAC 2[%d]", lane2_rc);
	}

	if (((r26c & BIT14) >> 14) == 1)
	{
		lane_rc[3] = lane3_rc;
		lane3_rc = ~lane3_rc + 1;
		rtos_core_trace(LOG_ALW, 0xb973, " DAC 3[-%d]", lane3_rc);
	}
	else
	{
		lane_rc[3] = lane3_rc;
		rtos_core_trace(LOG_ALW, 0x8271, " DAC 3[%d]", lane3_rc);
	}
}

static ddr_code void pcie_link_timer_handling(void *data)
{
	#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return;
	}
	#endif

	pcie_core_status2_t link_sts;

	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);

	rtos_core_trace(LOG_ALW, 0xeb47, "PCIe gen %d x %d", link_sts.b.neg_link_speed,
					link_sts.b.neg_link_width);

	pcie_phy_leq_dfe();
	pcie_phy_cali_results();

	rtos_core_trace(LOG_ALW, 0x2006, "No clk: %d, No perst: %d, CE Cnt: %d", tx_smart_stat->no_clk,tx_smart_stat->no_perst,tx_smart_stat->corr_err_cnt);

	if (link_sts.b.neg_link_speed != PCIE_GEN4)
	{
		HalOtpValueConfirm();
	}

	pcie_dbglog();

	if ((link_sts.b.neg_link_speed != PCIE_GEN4) || (link_sts.b.neg_link_width != PCIE_X4))
	{
		// Give one more chance to determine save log or not, since some of platforms will first link to gen 1.  Richard Lu 2024/5/22.
		pcie_abnormal_event = 1;	// 0: No event occur. 1: PCIe not 4 x 4 first.  2:  CTO timeout.  3: suddent link down event.

		#if(PLP_SUPPORT == 1)
		if(!plp_trigger || (gpio_get_value(GPIO_PLP_DETECT_SHIFT)))
		#endif
		{
			mod_timer(&pcie_abnormal_timer, jiffies + 40*HZ/10);// Delay save log operation. Delay 4 second.
		}
	}

	del_timer(&pcie_link_timer);

#ifdef RXERR_LINKUP_RETRAIN
	u32 target_speed = 3;
	u32 retrain_delay = 0;
	pcie_dbglog();
	ig_pcie_link_retrain(target_speed, retrain_delay);
	pcie_dbglog();
#endif

	// Enable IRQ
#if(CORR_ERR_INT == ENABLE)
	u32 mask;
	mask = pcie_wrap_readl(PCIE_INTR_MASK0);
	mask &= ~CFG_SEND_COR_ERR_MASK;
	pcie_wrap_writel(mask, PCIE_INTR_MASK0);
	rtos_core_trace(LOG_ERR, 0xcf43, "Enable RxErr interrupt");
#endif

}

#ifdef RXERR_IRQ_RETRAIN
static fast_code void pcie_retrain_timer_handling(void *data)
{
	u32 target_speed = 3;
	u32 retrain_delay = 0;
	pcie_dbglog();
	ig_pcie_link_retrain(target_speed, retrain_delay);
	pcie_dbglog();

	retrain_cnt++;
	rtos_core_trace(LOG_ERR, 0x40f4, "Already Retrained %d time(s) in current Powercycle", retrain_cnt);

	del_timer(&pcie_retrain_timer);
	// Enable IRQ
	u32 mask;
	mask = pcie_wrap_readl(PCIE_INTR_MASK0);
	mask &= ~CFG_SEND_COR_ERR_MASK;
	pcie_wrap_writel(mask, PCIE_INTR_MASK0);
	rtos_core_trace(LOG_ERR, 0x7041, "Enable RxErr interrupt");
}
#endif

#if PCIE_PHY_LOG
static slow_code void pcie_phy_timer_handler(void *data)
{
	pcie_rxsts_l0_rcvry_log0_t l0_rcvry;

	l0_rcvry.all = pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG0);

	rtos_core_trace(LOG_DEBUG, 0xa5f5, "phy_rcvry %x", pcie_reg_phy_rcvry);
	if (pcie_reg_phy_rcvry != l0_rcvry.all) {
		pcie_reg_phy_rcvry = l0_rcvry.all;

		if (pcie_reg_phy_rcvry & 0xFFFF0000) {
			pcie_phy_err_cnt++;
		}

		if (pcie_reg_phy_rcvry & BIT(14)) {
			pcie_recovery_cnt++;
		}
	}

	mod_timer(&pcie_phy_timer, jiffies + HZ);
}
#endif

/*
fast_code void pcie_err_clr(void)
{
    pcie_core_enable_ro_write(true);
    pcie_core_writel(0xffffffff, CORRECTABLE_ERROR_STATUS_REG);

    device_control_device_status_t pcie_device_sts;
    pcie_device_sts.all= pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS) |
                         PCIE_CAP_CORR_ERR_DETECTED_MASK;

    pcie_core_writel(pcie_device_sts.all, DEVICE_CONTROL_DEVICE_STATUS);
    pcie_core_enable_ro_write(false);
}
*/
// TODO The function should be revised in the feature
#ifdef LONG_CPL_TO
static fast_code void set_cpl_to_val(void)
{
	device_control2_device_status2_reg_t dc2ds = {.all = pcie_core_readl(DEVICE_CONTROL2_DEVICE_STATUS2_REG)};
	if(dc2ds.b.pcie_cap_cpl_timeout_value != 9)
	{
		//dc2ds.b.pcie_cap_cpl_timeout_value = 0xD;// 5.4~8.2sec for Rainier
		dc2ds.b.pcie_cap_cpl_timeout_value = 0x9;// 260~900msec for Rainier
		pcie_core_enable_ro_write(true);
		pcie_core_writel(dc2ds.all, DEVICE_CONTROL2_DEVICE_STATUS2_REG);
		pcie_core_enable_ro_write(false);
	}
	rtos_core_trace(LOG_ALW, 0xf74f, "device_statu2 :0x%x", pcie_core_readl(DEVICE_CONTROL2_DEVICE_STATUS2_REG));
}
#endif
#if (Baidu_case)
fast_code void pcie_cfg_ack_freq(void)
{
	ack_f_aspm_ctrl_off_t ack_freq_reg = {.all = pcie_core_readl(ACK_F_ASPM_CTRL_OFF)};
	pcie_core_status2_t link_sts;

	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	if(link_sts.b.neg_link_speed < PCIE_GEN4)
	{
		ack_freq_reg.b.ack_freq = 1;
	}
	else
	{
		ack_freq_reg.b.ack_freq = 0;
	}
	pcie_core_writel(ack_freq_reg.all, ACK_F_ASPM_CTRL_OFF);
}
#endif

fast_code static void pcie_bootup_isr(void)
{
    //log_isr = LOG_IRQ_DO;
    //pcie_unmsk_intr_t unmsk_intr;
    pcie_maskd_intr0_t maskd_intr0;

    maskd_intr0.all = pcie_wrap_readl(PCIE_MASKD_INTR0);

    // 2021-11-23
    //benson add for RX Error debugging
    rtos_core_trace(LOG_ERR, 0x6f3f, " pcie int. status: 0x%x", maskd_intr0.all);

    pcie_dbglog();
    #if(CORR_ERR_INT == ENABLE)
    if(maskd_intr0.all & CFG_SEND_COR_ERR_MASK)
    {
        //pcie_dbglog();
        rx_error = 1;
        //correctable_err_status_reg_t correctable_err_status = {.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG)};
        //rtos_core_trace(LOG_ERR, 0, " CE status: %x",correctable_err_status.all);
    }
    #endif

    if (maskd_intr0.all & SMLH_REQ_RST_NOT_MASK)
    {
        pcie_control_reg_t pcie_ctrlr = {
            .all = pcie_wrap_readl(PCIE_CONTROL_REG),
        };
		/*
		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			share_timer_irq_disable();
		}
		*/
        rtos_core_trace(LOG_ERR, 0x3720, "[PCIe] LinkReqRstNot");

        // if the ltssm still enable, we do nothing.
        if (pcie_ctrlr.b.app_ltssm_enable == 1)
        {
            rtos_core_trace(LOG_ERR, 0x58a2, "[PCIe] Link still enable");
            goto end;
        }

        //pcie_dbglog();

        // configure NVMe HW to disable SQE_fetching / CQE_returning
        misc_nvm_ctrl_reset(true);

        // configure cmd_proc to abort DMA xfer
        // continue to fetch DTAG from BTN and directly release DTAG to BTN
        hal_nvmet_abort_xfer(true);

        // trigger main reset task before release PCIe link training
        if (evt_perst_hook != 0xff)
        {
            urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST_LINK_DOWN);
            if (gPcieRstRedirected == false)
            {
                extern void urg_evt_task_process();
                urg_evt_task_process();

                //pcie_dbglog();
            }
        }
    }

    if (maskd_intr0.all & SMLH_LINK_DOWN_MASK)
    {
		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			lane_chk_bitmap=0;
			share_timer_irq_disable();
		}
		lane_chk_times=0;
		pcie_force_4lane();

        rtos_core_trace(LOG_ERR, 0xc587, "PCIe Link-");
        //pcie_dbglog();
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log0 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG0));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log1 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG1));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log2 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG2));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log3 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG3));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log4 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG4));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log5 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG5));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log6 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG6));
        // rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log7 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG7));
    }

    if (maskd_intr0.all & SMLH_LINK_UP_MASK)
    {
    	
		if(lane_chk_times > 0)
			rtos_core_trace(LOG_ALW, 0x48f7,"lane chk %llu ms, cnt(%d),lane_chk_bitmap(0x%x),lane2_1(%d)lane1(%d)", time_elapsed_in_ms(pcie_time),lane_chk_times,lane_chk_bitmap,pcie_lane2_to_1,pcie_lane1_cnt);

		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			lane_chk_bitmap=0;
			share_timer_irq_disable();
		}
		lane_chk_times=0;
		pcie_force_4lane();

        rtos_core_trace(LOG_ERR, 0xc585, "PCIe Link+");
        pcie_link_retrain();
        mod_timer(&pcie_link_timer, jiffies + 5*HZ/10);
    }

    if (maskd_intr0.all & CFG_MEM_SPACE_DIS_MASK)
    {
        rtos_core_trace(LOG_ERR, 0x1a35, "PCIe CFG MEM SPACE disable");
    }

    if (maskd_intr0.all & CFG_MEM_SPACE_EN_MASK)
    {
        pcie_bar0_starth_t bar0u = {
            .all = pcie_wrap_readl(PCIE_BAR0_STARTH),
        };
        pcie_bar0_startl_t bar0l = {
            .all = pcie_wrap_readl(PCIE_BAR0_STARTL),
        };

        //pcie_err_clr();

        rtos_core_trace(LOG_ERR, 0x6a01, "PCIe CFG MEM SPACE enable BAR0 %x%x ", bar0u.all, bar0l.all);
    }

end:
    /* Clear all interrupts */
    //unmsk_intr.all = pcie_wrap_readl(PCIE_UNMSK_INTR);
    //pcie_dbglog();
    pcie_wrap_writel(maskd_intr0.all, PCIE_UNMSK_INTR);
    //log_isr = LOG_IRQ_DOWN;
}


#if GET_PCIE_ERR
/*
ddr_code static void pcie_corr_err(correctable_err_status_reg_t corr_err_status)
{

    //rtos_core_trace(LOG_ERR, 0, " correctable_err_status 0x%x",corr_err_status.all);
    if(corr_err_status.b.bad_tlp){
        if (((bad_tlp_start_time < get_tsc_lo()) && (get_tsc_lo() - bad_tlp_start_time > (CYCLE_PER_MS*100))) || \
            ((bad_tlp_start_time > get_tsc_lo()) && (INV_U32 - bad_tlp_start_time + get_tsc_lo() >  \
            (CYCLE_PER_MS*100))) || bad_tlp_start_time == 0)
        {
            tx_smart_stat->pcie_correctable_error_count[0]+=1;
            corr_err_cnt.bad_tlp_cnt++;
            bad_tlp_start_time = get_tsc_lo();
            rtos_core_trace(LOG_PCIE_ERR, 0, " bad tlp occurs, cnt %d",corr_err_cnt.bad_tlp_cnt);
        }
    }
    if(corr_err_status.b.receiver_error){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.receiver_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " receiver_err occurs, cnt %d",corr_err_cnt.receiver_err_cnt);
    }
    if(corr_err_status.b.bad_dllp){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.bad_dllp_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " bad dllp occurs, cnt %d",corr_err_cnt.bad_dllp_cnt);
    }
    if(corr_err_status.b.replay_num_rollover){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.replay_num_rollover_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " replay_num_rollover occurs, cnt %d",corr_err_cnt.replay_num_rollover_cnt);
    }
    if(corr_err_status.b.replay_timer_timeout){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.replay_timer_timeout_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " replay_timer_timeout occurs, cnt %d",corr_err_cnt.replay_timer_timeout_cnt);
    }
    if(corr_err_status.b.advisory_non_fatal_error){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.advisory_non_fatal_error_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " advisory_non_fatal_error occurs, cnt %d",corr_err_cnt.advisory_non_fatal_error_cnt);
    }
    if(corr_err_status.b.corrected_internal_error){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.corrected_internal_error_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " corrected_internal_error occurs, cnt %d",corr_err_cnt.corrected_internal_error_cnt);
    }
    if(corr_err_status.b.header_log_overflow){
        tx_smart_stat->pcie_correctable_error_count[0]+=1;
        corr_err_cnt.header_log_overflow_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " header_log_overflow occurs, cnt %d",corr_err_cnt.header_log_overflow_cnt);
    }
    //pcie_core_enable_ro_write(true);
    //pcie_core_writel(corr_err_status.all, CORRECTABLE_ERROR_STATUS_REG);
    //pcie_core_enable_ro_write(false);
    //corr_err_status.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG);
    //rtos_core_trace(LOG_ERR, 0, " correctable_err_status 0x%x",corr_err_status.all);

}*/
/*
ddr_code static void pcie_uncorr_err(uncorrectable_err_status_reg_t uncorr_err_status)
{

    rtos_core_trace(LOG_PCIE_ERR, 0, " uncorrectable_err_status 0x%x", uncorr_err_status.all);
    if(uncorr_err_status.b.undefined){
        uncorr_err_cnt.undefined_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " undefined err occurs, cnt %d",uncorr_err_cnt.undefined_cnt);
    }
    if(uncorr_err_status.b.data_link_protocol_err){
        uncorr_err_cnt.data_link_protocol_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " data_link_protocol_err err occurs, cnt %d",uncorr_err_cnt.data_link_protocol_err_cnt);
    }
    if(uncorr_err_status.b.surprise_down_err){
        uncorr_err_cnt.surprise_down_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " surprise_down_err err occurs, cnt %d",uncorr_err_cnt.surprise_down_err_cnt);
    }
    if(uncorr_err_status.b.poisoned_tlp_received){
        uncorr_err_cnt.poisoned_tlp_received_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " poisoned_tlp_received err occurs, cnt %d",uncorr_err_cnt.poisoned_tlp_received_cnt);
    }
    if(uncorr_err_status.b.flow_control_protocol_err){
        uncorr_err_cnt.flow_control_protocol_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " flow_control_protocol_err err occurs, cnt %d",uncorr_err_cnt.flow_control_protocol_err_cnt);
    }
    if(uncorr_err_status.b.complement_timeout){
        uncorr_err_cnt.complement_timeout_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " complement_timeout err occurs, cnt %d",uncorr_err_cnt.complement_timeout_cnt);
    }
    if(uncorr_err_status.b.completer_abort){
        uncorr_err_cnt.completer_abort_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " completer_abort err occurs, cnt %d",uncorr_err_cnt.completer_abort_cnt);
    }
    if(uncorr_err_status.b.unexpected_complement){
        uncorr_err_cnt.unexpected_complement_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " unexpected_complement err occurs, cnt %d",uncorr_err_cnt.unexpected_complement_cnt);
    }
    if(uncorr_err_status.b.receiver_overflow){
        uncorr_err_cnt.receiver_overflow_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " receiver_overflow err occurs, cnt %d",uncorr_err_cnt.receiver_overflow_cnt);
    }
    if(uncorr_err_status.b.malformed_tlp){
        uncorr_err_cnt.malformed_tlp_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " malformed_tlp_cnt err occurs, cnt %d",uncorr_err_cnt.malformed_tlp_cnt);
    }
    if(uncorr_err_status.b.ercr_err){
        uncorr_err_cnt.ercr_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " ercr_err err occurs, cnt %d",uncorr_err_cnt.ercr_err_cnt);
    }
    if(uncorr_err_status.b.unsupported_request_err){
        uncorr_err_cnt.unsupported_request_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " unsupported_request_err err occurs, cnt %d",uncorr_err_cnt.unsupported_request_err_cnt);
    }
    if(uncorr_err_status.b.acs_violation){
        uncorr_err_cnt.acs_violation_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " acs_violation err occurs, cnt %d",uncorr_err_cnt.acs_violation_cnt);
    }
    if(uncorr_err_status.b.uncorrectable_internal_err){
        uncorr_err_cnt.uncorrectable_internal_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " uncorrectable_internal_err err occurs, cnt %d",uncorr_err_cnt.uncorrectable_internal_err_cnt);
    }
    if(uncorr_err_status.b.mc_blocked_tlp){
        uncorr_err_cnt.mc_blocked_tlp_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " mc_blocked_tlp err occurs, cnt %d",uncorr_err_cnt.mc_blocked_tlp_cnt);
    }
    if(uncorr_err_status.b.atomicop_egress_blocked){
        uncorr_err_cnt.atomicop_egress_blocked_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " atomicop_egress_blocked err occurs, cnt %d",uncorr_err_cnt.atomicop_egress_blocked_cnt);
    }
    if(uncorr_err_status.b.tlp_prefix_blocked_err){
        uncorr_err_cnt.tlp_prefix_blocked_err_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " tlp_prefix_blocked_err err occurs, cnt %d",uncorr_err_cnt.tlp_prefix_blocked_err_cnt);
    }
    if(uncorr_err_status.b.poisoned_tlp_egress_blocked){
        uncorr_err_cnt.poisoned_tlp_egress_blocked_cnt++;
        rtos_core_trace(LOG_PCIE_ERR, 0, " poisoned_tlp_egress_blocked err occurs, cnt %d",uncorr_err_cnt.poisoned_tlp_egress_blocked_cnt);
    }
}
*/
fast_code void disable_corr_uncorr_isr(void)
{
    u32 mask = pcie_wrap_readl(PCIE_INTR_MASK0);
    mask |= CFG_SEND_COR_ERR_MASK;
    mask |= CFG_SEND_NF_ERR_MASK;
    mask |= CFG_SEND_F_ERR_MASK;
    pcie_wrap_writel(mask, PCIE_INTR_MASK0);
    mask = pcie_wrap_readl(PCIE_INTR_MASK0);
    rtos_core_trace(LOG_INFO, 0x81b2, " cc en reset , disable AER err intr, PCIE_INTR_MASK0 0x%x ",mask);
}

fast_code void enable_corr_uncorr_isr(void)
{

	/*Reset PCIe AER Reg err intr*/
	u32 mask = 0;
	mask |= CFG_SEND_COR_ERR_MASK;
	mask |= CFG_SEND_NF_ERR_MASK;
	mask |= CFG_SEND_F_ERR_MASK;
	pcie_wrap_writel(mask, PCIE_UNMSK_INTR);

	/*Decide whether to report Corr Error to host or not*/
	device_control_device_status_t device_control_device_status;
	device_control_device_status.all = pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS);

#if GET_PCIE_ERR
	if ( 0 == device_control_device_status.b.pcie_cap_corr_err_report_en && !AER_POLLING_TIMER_SET)
	{
		corr_err_cnter_timer.function = corr_uncorr_err_counter;
		corr_err_cnter_timer.data = NULL;
		mod_timer(&corr_err_cnter_timer, jiffies + 1);
		dump_corr_timer.function = dump_pcie_corr_err;
		dump_corr_timer.data = NULL;
		mod_timer(&dump_corr_timer, jiffies + 1*HZ/10);
		AER_POLLING_TIMER_SET = 1;
		rtos_core_trace(LOG_INFO, 0x4fbd, " AER TIMER MODE ENABLE");
	}
	else if(AER_POLLING_TIMER_SET == 1 && 1 == device_control_device_status.b.pcie_cap_corr_err_report_en)
	{
		del_timer(&corr_err_cnter_timer);
		del_timer(&dump_corr_timer);
		AER_POLLING_TIMER_SET = 0;
		rtos_core_trace(LOG_INFO, 0x6a74, " AER TIMER MODE ENABLE first, DISABLE now");
	}
#endif
	mask = pcie_wrap_readl(PCIE_INTR_MASK0);
	if(device_control_device_status.all & PCIE_CAP_CORR_ERR_REPORT_EN_MASK)
		mask &= ~CFG_SEND_COR_ERR_MASK;

	if(device_control_device_status.all & PCIE_CAP_NON_FATAL_ERR_REPORT_EN_MASK)
		mask &= ~CFG_SEND_NF_ERR_MASK;

	if(device_control_device_status.all & PCIE_CAP_FATAL_ERR_REPORT_EN_MASK)
		mask &= ~CFG_SEND_F_ERR_MASK;

	pcie_wrap_writel(mask, PCIE_INTR_MASK0);
	mask = pcie_wrap_readl( PCIE_INTR_MASK0);
	correctable_err_mask_reg_t correctable_err_mask = {.all = pcie_core_readl(CORRECTABLE_ERROR_MASK_REG)};
#if GET_PCIE_ERR
	is_bad_tlp_masked = (u8) correctable_err_mask.b.bad_tlp;
#endif
	uncorrectable_err_mask_reg_t uncorrectable_err_mask = {.all = pcie_core_readl(UNCORRECTABLE_ERROR_MASK_REG)};
	rtos_core_trace(LOG_INFO, 0x612d, " cc en set , device_control status 0x%x, PCIE_INTR_MASK0 0x%x corr_mask 0x%x uncorr_mask 0x%x ",device_control_device_status.all, mask,correctable_err_mask.all,uncorrectable_err_mask.all);
}
#endif

// Static -> Non Static for using it in main function	2023/8/09 Richard
ddr_code void pcie_dbglog(void)
{
    //u32 r58;
    //u32 r5c;
    //u32 r60;

    //r58 = readl((void *)0xc0043058);
    //r5c = readl((void *)0xc004305c);
    //r60 = readl((void *)0xc0043060);
#if(PLP_SUPPORT == 1)
	if(!(gpio_get_value(GPIO_PLP_DETECT_SHIFT)) || plp_trigger)
	{
		return;
	}
#endif


    rtos_core_trace(LOG_INFO, 0xab1e, "R3018:%x, R3058:%x, R305C:%x, R3060:%x,",
          readl((void *)0xC0043018), readl((void *)0xc0043058), readl((void *)0xc004305c), readl((void *)0xc0043060));

    rtos_core_trace(LOG_INFO, 0xd488, "R3018:%x, R3058:%x, R305C:%x, R3060:%x,",
        readl((void *)0xC0043018), readl((void *)0xc0043058), readl((void *)0xc004305c), readl((void *)0xc0043060));

    rtos_core_trace(LOG_INFO, 0x6cdf, "R301C:%x, R303C:%x, R00A4:%x, R30B4:%x, R3004:%x,",
                    readl((void *)0xC004301C), readl((void *)0xC004303C),readl((void *)0xC00400A4), readl((void *)0xC00430B4),readl((void *)0xC0043004));

    rtos_core_trace(LOG_INFO, 0xc3aa, "R3064:%x, R3080:%x, R3000:%x, R00A8:%x, R0094:%x,",
                    readl((void *)0xc0043064), readl((void *)0xc0043080), readl((void *)0xc0043000), readl((void *)0xc00400a8),readl((void *)0xc0040094));

    rtos_core_trace(LOG_INFO, 0x2188, "R0000:%x, R3004:%x, R300C:%x,", readl((void *)0xc0040000), readl((void *)0xc0043004), readl((void *)0xc004300C));
}

extern struct nvme_registers nvmet_regs;	///< nvme register
extern u64 sys_time;					//system time

#if(CORR_ERR_INT == ENABLE)
ddr_code static void HandlePcieCorrError(void)
{
	#ifdef RXERR_IRQ_RETRAIN
	// Disable IRQ
	u32 mask;
	mask = pcie_wrap_readl(PCIE_INTR_MASK0);
	mask |= CFG_SEND_COR_ERR_MASK;
	pcie_wrap_writel(mask, PCIE_INTR_MASK0);
	rtos_core_trace(LOG_ERR, 0x9f7d, "Disable RxErr interrupt");
	#endif
	//pcie_dbglog();
	//correctable_err_status_reg_t correctable_err_status = {.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG)};
	//rtos_core_trace(LOG_ERR, 0, " CE status: %x", correctable_err_status.all);
	tx_smart_stat->corr_err_cnt++;
	rtos_core_trace(LOG_ERR, 0x076c, "Rx Err happens, corr_err_cnt: %d \n", tx_smart_stat->corr_err_cnt);
	RxErr_cnt++;
	rtos_core_trace(LOG_ERR, 0x0047, "Rx Err happens %d times in current Powercycle \n", RxErr_cnt);
	//flush_to_nand(EVT_LINK_LOSS);
	//mod_timer(&pcie_abnormal_timer, jiffies + 1*HZ/10);

	#ifdef RXERR_IRQ_RETRAIN
	if(retrain_cnt < MAX_RETRAIN_CNT)
	{
		mod_timer(&pcie_retrain_timer, jiffies + 20*HZ/10);
	}
	#endif
}
#endif

ddr_code static void HandlePcieCplTo(void)
{
	transpend_cnt++;
	rtos_core_trace(LOG_ERR, 0x0c69, " CPL TO, transpend count %d ", transpend_cnt);
	//pcie_dbglog();
#if(PLP_SUPPORT == 1)
	if((gpio_get_value(GPIO_PLP_DETECT_SHIFT) == true) && (plp_trigger != 0xEE))
	{
		//flush_to_nand(EVT_PCIE_CPL_TO);
		pcie_abnormal_event = 2;	// 0: No event occur. 1: PCIe not 4 x 4 first.  2:  CTO timeout.  3: suddent link down event.
		mod_timer(&pcie_abnormal_timer, jiffies + 10*HZ/10);//avoid save log in plp
	}
#endif
	/*
	u32 r58;
	u32 r5c;
	u32 r60;
	r58 = readl((void *)0xc0043058);
	r5c = readl((void *)0xc004305c);
	r60 = readl((void *)0xc0043060);
	rtos_core_trace(LOG_ERR, 0, "[CPL]R3018 %x, R58 %x, R5C %x, R60 %x \n", pcie_wrap_readl(PCIE_CORE_STATUS), r58, r5c, r60);
	rtos_core_trace(LOG_ERR, 0, "[CPL]R30B4 %x, RC0043000 %x, RC004000C %x \n", pcie_wrap_readl(PCIE_CORE_STATUS2),
			readl((void *)0xc0043000), readl((void *)0xc004000C));
	*/
}

ddr_code static void HandlePcieLinkDown(void)
{
	//pcie_dbglog();
#if(PLP_SUPPORT == 1)
	if(cc_en_set && (gpio_get_value(GPIO_PLP_DETECT_SHIFT) == true) && (plp_trigger != 0xEE))
#else
    if(cc_en_set)
#endif
	{
		//suddent_link_down = 1;
		pcie_abnormal_event = 3;	// 0: No event occur. 1: PCIe not 4 x 4 first.  2:  CTO timeout.  3: suddent link down event.
		//flush_to_nand(EVT_PCIE_LK_DOWN);
		mod_timer(&pcie_abnormal_timer, jiffies + 10*HZ/10);//avoid save log in plp
	}
	// Reason that PCIe link enters recovery, Jack 20220504
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log0 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG0));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log1 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG1));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log2 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG2));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log3 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG3));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log4 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG4));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log5 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG5));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log6 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG6));
	// rtos_core_trace(LOG_ALW, 0, "PCIe link Recovery Log7 :0x%x", pcie_wrap_readl(PCIE_RXSTS_L0_RCVRY_LOG7));
}

fast_code static void pcie_isr(void)
{
	//log_isr = LOG_IRQ_DO;
	//pcie_unmsk_intr_t unmsk_intr;
	pcie_maskd_intr0_t maskd_intr0;

	maskd_intr0.all = pcie_wrap_readl(PCIE_MASKD_INTR0);

	//pcie_intr_mask0_t intr_mask0;
	//intr_mask0.all = pcie_wrap_readl(PCIE_INTR_MASK0);
	//rtos_core_trace(LOG_INFO, 0, " pcie isr intr_mask0 0x%x ",intr_mask0.all);

	// if (LINK_REQ_RST_NOT_MASK & maskd_intr0.all) {
	// 	rtos_core_trace(LOG_ALW, 0, "\033[91mPCIe hot reset[0m");
	// }//3.1.7.4 pcie

	// 2021-11-23
	//benson add for RX Error debugging
	rtos_core_trace(LOG_ALW, 0x69d0, "pcie int. status: 0x%x, timestamp of cpu%d: 0x%w%x", maskd_intr0.all, CPU_ID, (u32)(sys_time >> 32), (u32)(sys_time));
	//pcie_dbglog();
	//rtos_core_trace(LOG_ALW, 0, "timestamp of cpu%d: 0x%w%x", CPU_ID, (u32)(sys_time >> 32), (u32)(sys_time));
	#if(CORR_ERR_INT == ENABLE)
	if(maskd_intr0.all & CFG_SEND_COR_ERR_MASK)
	{
		HandlePcieCorrError();
	}
	#endif

#if GET_PCIE_ERR
	if(maskd_intr0.all & CFG_SEND_COR_ERR_MASK)
	{
		//rtos_core_trace(LOG_ERR, 0, " correctable err ");
		pcie_dbglog();
		volatile pcie_core_status_t core_status;
		core_status.all = pcie_wrap_readl(PCIE_CORE_STATUS);
		if( !is_bad_tlp_masked && !plp_trigger && !pcie_perst_flag && core_status.b.smlh_ltssm_state == 0x11)
		{
			corr_error_cnt++;
			//rtos_core_trace(LOG_PCIE_ERR, 0, "PCIe isr start to read AER register correctable error register");
			correctable_err_status_reg_t correctable_err_status = {.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG)};
			//correctable_err_mask_reg_t correctable_err_mask = {.all = pcie_core_readl(CORRECTABLE_ERROR_MASK_REG)};
			//rtos_core_trace(LOG_PCIE_ERR, 0, " correctable_err_status 0x%x correctable err count %d",correctable_err_status.all, corr_error_cnt);
			//correctable_err_status.all &= (~correctable_err_mask.all);

			if(correctable_err_status.b.bad_tlp)
			{
				if (((bad_tlp_start_time < get_tsc_lo()) && (get_tsc_lo() - bad_tlp_start_time > (CYCLE_PER_MS*100))) || \
					((bad_tlp_start_time > get_tsc_lo()) && (INV_U32 - bad_tlp_start_time + get_tsc_lo() >  \
					(CYCLE_PER_MS*100))) || bad_tlp_start_time == 0)
				{
					tx_smart_stat->pcie_correctable_error_count[0]+=1;
					corr_err_cnt.bad_tlp_cnt++;
					bad_tlp_start_time = get_tsc_lo();
					//rtos_core_trace(LOG_PCIE_ERR, 0, " bad tlp occurs, cnt %d",corr_err_cnt.bad_tlp_cnt);
				}
			}
			//pcie_corr_err(correctable_err_status);
		}
	}

	if((maskd_intr0.all & CFG_SEND_NF_ERR_MASK) || (maskd_intr0.all & CFG_SEND_F_ERR_MASK))
	{
		pcie_dbglog();
		/*
		//rtos_core_trace(LOG_ERR, 0, " uncorrectable err ");
			if(!plp_trigger && !pcie_perst_flag)
			{
					uncorr_error_cnt++;
					//rtos_core_trace(LOG_PCIE_ERR, 0, "PCIe isr start to read AER register uncorrectable error register");
					uncorrectable_err_status_reg_t uncorrectable_err_status = {.all = pcie_core_readl(UNCORRECTABLE_ERROR_STATUS_REG)};
					uncorrectable_err_mask_reg_t uncorrectable_err_mask = {.all = pcie_core_readl(UNCORRECTABLE_ERROR_MASK_REG)};
					//rtos_core_trace(LOG_PCIE_ERR, 0, " uncorrectable_err_status 0x%x uncorr_error_cnt %d",uncorrectable_err_status.all,uncorr_error_cnt);
					uncorrectable_err_status.all &= (~uncorrectable_err_mask.all);
					pcie_uncorr_err(uncorrectable_err_status);
			}
		*/
	}
#endif

	if(maskd_intr0.all & SLV_RD_CPL_TIMEOUT_MASK)
	{
		pcie_dbglog();
		HandlePcieCplTo();
	}

	if (maskd_intr0.all & SMLH_REQ_RST_NOT_MASK)
	{
		pcie_control_reg_t pcie_ctrlr = {
			.all = pcie_wrap_readl(PCIE_CONTROL_REG),
		};
		
		//if(lane_chk_times > 0)
			//rtos_core_trace(LOG_ALW, 0x3c5c,"lane chk %llu ms, cnt(%d),lane_chk_bitmap(0x%x),lane2_1(%d)lane1(%d)", time_elapsed_in_ms(pcie_time),lane_chk_times,lane_chk_bitmap,pcie_lane2_to_1,pcie_lane1_cnt);
		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			lane_chk_bitmap=0;
			share_timer_irq_disable();
		}
		lane_chk_times=0;
		pcie_force_4lane();

		rtos_core_trace(LOG_ALW, 0x2f2f, "PCIE REQ_RST_NOT");
		// if the ltssm still enable, we do nothing.
		if (pcie_ctrlr.b.app_ltssm_enable == 1)
		{
			rtos_core_trace(LOG_ALW, 0x2d40, "LTSSM Still Enable : ltssm_enable=%d", pcie_ctrlr.b.app_ltssm_enable);
			goto end;
		}

		if(gResetFlag & BIT(cNvmeShutDown))
		{
			gResetFlag &= ~BIT(cNvmeShutDown);
		}
		gResetFlag |= BIT(cNVMeLinkReqRstNot);

		// configure NVMe HW to disable SQE_fetching / CQE_returning
		misc_nvm_ctrl_reset(true);

		// configure cmd_proc to abort DMA xfer
		// continue to fetch DTAG from BTN and directly release DTAG to BTN
		hal_nvmet_abort_xfer(true);

		// trigger main reset task before release PCIe link training
		#if CO_SUPPORT_PANIC_DEGRADED_MODE
		if (smCPUxAssert)
		{
			urg_evt_set(evt_assert_rst, (u32)pcie_rst_post, PCIE_RST_LINK_DOWN);
		}
		else
		#endif
		{
			if (evt_perst_hook != 0xff)
				//urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST);
				urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST_LINK_DOWN); //joe add cvtest 20200918
		}

	}

	if (maskd_intr0.all & CFG_FLR_PF_ACTIVE_MASK)
	{
		pcie_dbglog();
		extern void nvmet_chk_flr(void);
		rtos_core_trace(LOG_ERR, 0xda4f, "[RSET] FLR");

		// Clear Shutdown bit when FLR trigger. 12/14 Richard modify for PCBasher
		if(gResetFlag & BIT(cNvmeShutDown))
		{
			gResetFlag &= ~BIT(cNvmeShutDown);
		}
		gResetFlag |= BIT(cNvmeFlrPfReset);

		// configure NVMe HW to disable SQE_fetching / CQE_returning
		misc_nvm_ctrl_reset(true);

		// configure cmd_proc to abort DMA xfer
		// continue to fetch DTAG from BTN and directly release DTAG to BTN
		hal_nvmet_abort_xfer(true);

		// Handle NVMe FLR first
		nvmet_chk_flr();

		#if CO_SUPPORT_PANIC_DEGRADED_MODE
		if (smCPUxAssert)
		{
			urg_evt_set(evt_assert_rst, (u32)flr_post_handler, PCIE_RST_FLR);
		}
		else
		#endif
		{
			urg_evt_set(evt_perst_hook, (u32)flr_post_handler, PCIE_RST_FLR);
		}

	}

	if (maskd_intr0.all & SMLH_LINK_DOWN_MASK)
	{
		pcie_dbglog();
		
		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			lane_chk_bitmap=0;
			share_timer_irq_disable();
		}
		lane_chk_times=0;
		pcie_force_4lane();

		// configure NVMe HW to disable SQE_fetching / CQE_returning
		misc_nvm_ctrl_reset(true);

		// configure cmd_proc to abort DMA xfer
		// continue to fetch DTAG from BTN and directly release DTAG to BTN
		hal_nvmet_abort_xfer(true);

		pcie_wrap_writel(1, PCIE_APP_REQ_FLS);
		rtos_core_trace(LOG_ERR, 0x741d, "PCIe Link-");
		HandlePcieLinkDown();
	}

	if (maskd_intr0.all & SMLH_LINK_UP_MASK)
	{
		pcie_dbglog();
		if(lane_chk_times > 0)
			rtos_core_trace(LOG_ALW, 0x7f3a,"lane chk %llu ms, cnt(%d),lane_chk_bitmap(0x%x),lane2_1(%d)lane1(%d)", time_elapsed_in_ms(pcie_time),lane_chk_times,lane_chk_bitmap,pcie_lane2_to_1,pcie_lane1_cnt);
		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			lane_chk_bitmap=0;
			share_timer_irq_disable();
		}
		lane_chk_times=0;
		pcie_force_4lane();

		rtos_core_trace(LOG_ERR, 0x7423, "PCIe Link+");
		#ifdef LONG_CPL_TO
		set_cpl_to_val();
		#endif
		//pcie_link_retrain();
		extern bool save_nor_flag;
		save_nor_flag = true;
		mod_timer(&pcie_link_timer, jiffies + 5*HZ/10);
	}

	if (maskd_intr0.all & CFG_MEM_SPACE_DIS_MASK)
	{
		pcie_dbglog();
		rtos_core_trace(LOG_ERR, 0x94e1, "PCIe CFG MEM SPACE disable");
		err_timer_flag = false;
	}

	if (maskd_intr0.all & CFG_MEM_SPACE_EN_MASK)
	{
		pcie_dbglog();
		pcie_bar0_starth_t bar0u = {
			.all = pcie_wrap_readl(PCIE_BAR0_STARTH),
		};
		pcie_bar0_startl_t bar0l = {
			.all = pcie_wrap_readl(PCIE_BAR0_STARTL),
		};

		rtos_core_trace(LOG_ERR, 0x9f7f, "PCIe CFG MEM SPACE enable BAR0 %x%x ", bar0u.all, bar0l.all);
		err_timer_flag = true;

		//pcie_err_clr();

		/*if(err_timer_flag == false){
			err_timer_flag = true;
			mod_timer(&corr_err_cnt_timer, jiffies + 1);
		}*/
	}

	if (maskd_intr0.all & CFG_VPD_INT_MASK) {
		vpd_base_t vpd_cfg;
		data_reg_t vpd_data;
		pcie_dbglog();

		pcie_set_xfer_busy(true);
		vpd_cfg.all = pcie_core_readl(VPD_BASE);
		if (vpd_cfg.b.vpd_flag == 0) {
			if (vpd_cfg.b.vpd_address == 0) {
				vpd_data.b.rsvd_0 = 0x41000482;    // example data for cv test
				pcie_core_writel(vpd_data.all, DATA_REG);
			} else {
				vpd_data.b.rsvd_0 = 0x78444342;    // example data for cv test
				pcie_core_writel(vpd_data.all, DATA_REG);
			}
		}
		vpd_cfg.b.vpd_flag ^= 0x1;
		pcie_core_writel(vpd_cfg.all, VPD_BASE);
		pcie_set_xfer_busy(false);
		rtos_core_trace(LOG_ALW, 0x768e, "PCIE VPD vpd_cfg %x ", vpd_cfg.all);
	}

#ifdef ENABLE_VPD
	if (maskd_intr0.all & CFG_VPD_INT_MASK)
		vpd_handler();
#endif
end:
	/* Clear all interrupts */
	//unmsk_intr.all = pcie_wrap_readl(PCIE_UNMSK_INTR);
	pcie_wrap_writel(maskd_intr0.all, PCIE_UNMSK_INTR);
	//log_isr = LOG_IRQ_DOWN;
	//evlog_printk(LOG_ALW, "timestamp of cpu%d: 0x%w%x", CPU_ID, (u32)(sys_time >> 32), (u32)(sys_time));
}

static ddr_code void perst_assert_handler(void)
{
#if GET_PCIE_ERR
		pcie_perst_flag = 1;
#else
	//log_isr = LOG_IRQ_DO;
	pcie_maskd_intr0_t maskd_intr0;

	maskd_intr0.all = pcie_wrap_readl(PCIE_MASKD_INTR0);

	rtos_core_trace(LOG_ERR, 0x80de, "PA_pcie_isr: 0x%x\n", maskd_intr0.all);
	//log_isr = LOG_IRQ_DOWN;
#endif
}

static fast_code void preset_n_isr(void)
{
#if GET_PCIE_ERR
		pcie_perst_flag = 0;
#endif
	//log_isr = LOG_IRQ_DO;
	if (!(gResetFlag & BIT(cNvmePCIeReset)))
	{
			// Clear Shutdown bit when PCIe reset trigger. 12/14 Richard modify for PCBasher
		if(gResetFlag & BIT(cNvmeShutDown))
		{
			gResetFlag &= ~BIT(cNvmeShutDown);
		}
		gResetFlag |= BIT(cNvmePCIeReset);
		
		if(pcie_timer_enable == 1)
		{
			pcie_timer_enable=0;
			lane_chk_times=0;
			lane_chk_bitmap=0;
			share_timer_irq_disable();
			//rtos_core_trace(LOG_ALW, "resotre 4 lane when link+");
		}

		// configure NVMe HW to disable SQE_fetching / CQE_returning
		misc_nvm_ctrl_reset(true);

		// configure cmd_proc to abort DMA xfer
		// continue to fetch DTAG from BTN and directly release DTAG to BTN
		hal_nvmet_abort_xfer(true);

		// trigger main reset task before release PCIe link training
		rtos_core_trace(LOG_ERR, 0xa526, "[RSET] PCIeReset");
		#if CO_SUPPORT_PANIC_DEGRADED_MODE
		if (smCPUxAssert)
		{
			urg_evt_set(evt_assert_rst, (u32)pcie_rst_post, PCIE_RST);
		}
		else
		#endif
		{
			urg_evt_set(evt_perst_hook, (u32)pcie_rst_post, PCIE_RST);
		}

		if (gPcieRstRedirected == false)
		{
			extern void urg_evt_task_process();
			urg_evt_task_process();
		}
	}
	else
	{
		rtos_core_trace(LOG_ERR, 0x6754, "Ignore glitch...");
	}
	//log_isr = LOG_IRQ_REST;

}
#if 0
/*!
 * @brief set pcie phy register
 *
 * This is one time configuration.
 *
 * @return	not used
 */
UNUSED static fast_code void pcie_phy_step(void)
{
	//TAP1 TARGET=12 / TAPN1 TARGET=24 / TAP1 RATIO = 0.5
	pcie_phy_cfg(0x6C, 0x00180CCB);

	//### TAPN1 RATIO ==> 2X
	pcie_phy_cfg(0x78, 0x00400000);
	//## TEQ_DIFF RATIO
	pcie_phy_cfg(0x68, 0x4005E000);
	//## TAP0 BW
	pcie_phy_cfg(0x20, 0xC0006801);
	//## TAP1/TAP2 BW
	pcie_phy_cfg(0x30, 0x20978FB1);
	//## TMV RATIO / VGA BW
	pcie_phy_cfg(0x70, 0x181E1E90);
	//## Set TDR Strength
	pcie_phy_cfg(0x108, 0x00010800);
	pcie_phy_cfg(0x118, 0x00108000);
	pcie_phy_cfg(0x128, 0x00400000);
	pcie_phy_cfg(0x138, 0x00400000);

	//## CDR Ppath BW  G3G4G2G1
	pcie_phy_cfg(0x168, 0x00440000);
	//## CDR L0 Ipath Downsample
	pcie_phy_cfg(0x10C, 0x00000003);

	//## EQ_CAL_TIME
	pcie_phy_cfg(0x3C, 0x00007700);
	//## EQ_ac_offset
	pcie_phy_cfg(0x64, 0x78090307);
	//## EQ_bot lim = 3
	pcie_phy_cfg(0x5C, 0x0000001C);
	//##VGA Target [23:16] increase for G4
	pcie_phy_cfg(0x74, 0x80483419);
	//##TAP1 BOT LIM
	pcie_phy_cfg(0x24, 0x007f7e30);
	//##Front-End bandwidth Summer G4
	pcie_phy_cfg(0x170, 0x082066e0);
	//##LEQ G4 Bandwidth
	pcie_phy_cfg(0x174, 0xce3667F0);
	//##VGA G4 Bandwidth
	pcie_phy_cfg(0x178, 0x0067f008);
	//##I path Current G4
	pcie_phy_cfg(0x164, 0x22220000);
	//##LPF Kv gain 0x0a012db0 (new) / 0x0a012d90 (old) + INSTG VCM increase
	pcie_phy_cfg(0x104, 0x0a0125B0);
}
#endif

#if defined(M2_2A) || defined(U2_LJ) //joe add U2_LJ
/*!
 * @brief set pcie phy BOT settings
 *
 * This parameter is for PCIe tap1 settings for M2_2A board.
 * This is one time configuration.
 *
 * @return	not used
 */
//fast_code void pcie_tap1_setting()
//{
//	//## TAP1_BOT = 0
//	pcie_phy_cfg(0x24, 0x007F7E30);
//}
#endif

fast_code void pcie_gen3_bandwidth(void)
{
	pcie_phy_cfg(0x174, 0x7E3666E0);
	pcie_phy_cfg(0x16C, 0x670083F8);
}

fast_code void pcie_isr_clear(void)
{
	pcie_unmsk_intr_t mask_sts;
	mask_sts.all = pcie_wrap_readl(PCIE_MASKD_INTR0);
	pcie_wrap_writel(mask_sts.all, PCIE_UNMSK_INTR);
}

fast_code void pcie_lane_margin_setting(void)
{
	//Rx Lane Margin Parameter Settings
	if(!plp_trigger)
			writel(0x20603210, (void *)0xC0020B80);
	if(!plp_trigger)
		writel(0x17031f1f, (void *)0xC0020B84);
}

fast_code void pcie_core_setting(void) //add inno eq problem 20201007
{
	if(!plp_trigger)
		writel(0x00002c00, (void *)0xC0020890);
	if(!plp_trigger)
		writel(0x01002c00, (void *)0xC0020890);
}
#ifdef IG_MODIFIED_PHY
fast_code void pcie_gen34_preset(void)
{
	if(!plp_trigger)
		writel(0x01002C00, (void *)0xC0020890);
	if(!plp_trigger)
		writel(0x04004071, (void *)0xC00208A8);
}
#endif

#ifdef SHORT_CHANNEL
fast_code void pcie_short_channel_improve(void)
{
	//## TEQ STEP
#ifndef PCIE_PHY_IG_SDK
	pcie_phy_cfg(0x68, 0x0005e000);
#else
	pcie_phy_cfg(0x68, 0x0005E0F0);
#endif

	//## LEQ BW
	//Benson 2021-10-29
	//modify this value for IEC issue(pcie speed up fail during link training)
	//improve Gen3 LEQ bandwidth
	//pcie_phy_cfg(0x174, 0x7D3665D0);
	pcie_phy_cfg(0x174, 0xBD3565D0);// 36 -> 35 for Gen2 LEQ BW and 7->B for Gen3 LEQ BW

	//### EQ_bot lim = 3, BGCAL_OFF[30], bot= 0, 0x00000005, LEQ TRAINING BW = 4(1D, 1C)
	pcie_phy_cfg(0x5C, 0x0000001B);

	//### EQ_ac_offset: G3/G4 offset = [15:8]
	pcie_phy_cfg(0x64, 0x78090207);

	//## RX ODT
	pcie_phy_cfg(0x100, 0x0A6C043C);
	pcie_phy_cfg(0x110, 0xA6C043C0);
	pcie_phy_cfg(0x120, 0x6C043C00);
	pcie_phy_cfg(0x130, 0xC043C000);
}
#endif
fast_code void pcie_tdr_setting(void)
{
	u32 r108;
	u32 r118;
	u32 r128;
	u32 r138;

	r108 = pcie_phy_sts(0x108);
	r108 &= ~(BIT16 | BIT17);
	r108 |= (BIT16 | BIT17);
	pcie_phy_cfg(0x108, r108);

	r118 = pcie_phy_sts(0x118);
	r118 &= ~(BIT20 | BIT21);
	r118 |= (BIT20 | BIT21);
	pcie_phy_cfg(0x118, r118);

	r128 = pcie_phy_sts(0x128);
	r128 &= ~(BIT24 | BIT25);
	r128 |= (BIT24 | BIT25);
	pcie_phy_cfg(0x128, r128);

	r138 = pcie_phy_sts(0x138);
	r138 &= ~(BIT28 | BIT29);
	r138 |= (BIT28 | BIT29);
	pcie_phy_cfg(0x138, r138);
}

fast_code void pcie_pll_bw_update(void)
{
	pcie_phy_cfg(0x190, 0x15558BA4);
}

#ifdef IG_MODIFIED_PHY
fast_code void pcie_phy_leq_train(void)
{
	pcie_phy_cfg(0x24, 0x007FFE30);
	pcie_phy_cfg(0x2C, 0x2040FE00);
	pcie_phy_cfg(0x74, 0x804CF419);
}
fast_code void pcie_phy_cdr_setting(void)
{
	pcie_phy_cfg(0x104, 0x0A012590);
}

#endif

#ifdef PCIE_PHY_IG_SDK
fast_code void pcie_phy_squelch_update(void)
{
	u32 r104 = pcie_phy_sts(0x104);
	r104 &= ~(BIT26 | BIT25 | BIT24);
	r104 |= (BIT26);
	pcie_phy_cfg(0x104, r104);

	u32 r114 = pcie_phy_sts(0x114);
	r114 &= ~(BIT30 | BIT29 | BIT28);
	r114 |= (BIT30);
	pcie_phy_cfg(0x114, r114);

	u32 r128 = pcie_phy_sts(0x128);
	r128 &= ~(BIT2 | BIT1 | BIT0);
	r128 |= (BIT2);
	pcie_phy_cfg(0x128, r128);

	u32 r138 = pcie_phy_sts(0x138);
	r138 &= ~(BIT6 | BIT5 | BIT4);
	r138 |= (BIT6);
	pcie_phy_cfg(0x138, r138);
}

fast_code void pcie_phy_cdr_relock(void)
{
	u32 r1c = pcie_phy_sts(0x1c);

	r1c &= ~(BIT13 | BIT12);
	r1c |= (BIT13 | BIT12);
	r1c |= (BIT23); 	// When CDR lock data fail, relock the refclk
	rtos_core_trace(LOG_DEBUG, 0x0c23, "cdr relock r1c:%x\n", r1c);
	pcie_phy_cfg(0x1c, r1c);
}

fast_code void pcie_phy_update_dfe_lmt(void)
{
	u32 r2c;
	r2c = pcie_phy_sts(0x2c);
	r2c &= ~(BIT23 | BIT22 | BIT21 | BIT20);
	r2c |= (BIT21);
	pcie_phy_cfg(0x2c, r2c);
}

fast_code void pcie_phy_pll_lock(void)
{
	u32 r3c = pcie_phy_sts(0x3c);
	r3c &= ~(BIT30 | BIT29 | BIT28);
	r3c |= (BIT30 | BIT28);
	pcie_phy_cfg(0x3c, r3c);

}

fast_code void pcie_phy_l11_tx_commode_on(void)
{
	u32 r60;
	r60 = pcie_phy_sts(0x60);
	r60 |= BIT28;
	pcie_phy_cfg(0x60, r60);
}

fast_code void pcie_phy_cdr_lock_enhance(void)
{
	pcie_phy_cfg(0x40, 0x000000f0);
}

fast_code void pcie_phy_refclksq_setting(void)
{
	u32 r140;
	r140 = pcie_phy_sts(0x140);
	r140 &= ~(BIT30 | BIT29);
	r140 |= BIT29;
	pcie_phy_cfg(0x140, r140);
}


fast_code void pcie_phy_rlm_improve(void)
{
	u32 r10;
	r10 = pcie_phy_sts(0x10);
	r10 &= ~(BIT5 | BIT4);
	r10 |= BIT5;
	pcie_phy_cfg(0x10, r10);
}

fast_code void pcie_phy_txdetrx_setting(void)
{
	u32 r108;
	u32 r118;
	u32 r12C;
	u32 r13C;

	r108 = pcie_phy_sts(0x108);
	r108 |= BIT25;
	pcie_phy_cfg(0x108, r108);

	r118 = pcie_phy_sts(0x118);
	r118 |= BIT29;
	pcie_phy_cfg(0x118, r118);

	r12C = pcie_phy_sts(0x12C);
	r12C |= BIT1;
	pcie_phy_cfg(0x12C, r12C);

	r13C = pcie_phy_sts(0x13C);
	r13C |= BIT5;
	pcie_phy_cfg(0x13C, r13C);

}
#endif

fast_code void pcie_tx_update(void)
{
	u32 r108;
	u32 r118;
	u32 r128;
	u32 r138;
	u32 r140;
	u32 r144;

	r108 = pcie_phy_sts(0x108);
	r108 &= ~(BIT11 | BIT10 | BIT9 | BIT8);
	r108 |= (BIT11 | BIT9); // b'1010
	pcie_phy_cfg(0x108, r108);

	r118 = pcie_phy_sts(0x118);
	r118 &= ~(BIT15 | BIT14 | BIT13 | BIT12);
	r118 |= (BIT15 | BIT13); // b'1010
	pcie_phy_cfg(0x118, r118);

	r128 = pcie_phy_sts(0x128);
	r128 &= ~(BIT19 | BIT18 | BIT17 | BIT16);
	r128 |= (BIT19 | BIT17); // b'1010
	pcie_phy_cfg(0x128, r128);


	r138 = pcie_phy_sts(0x138);
	r138 &= ~(BIT23 | BIT22 | BIT21 | BIT20);
	r138 |= (BIT23 | BIT21);  // b'1010
	pcie_phy_cfg(0x138, r138);

	r140 = pcie_phy_sts(0x140);
	r140 &= ~(BIT25);
	pcie_phy_cfg(0x140, r140);

	r144 = pcie_phy_sts(0x144);
	r144 &= ~(BIT7 | BIT6);
	r144 |= (BIT6);
	pcie_phy_cfg(0x144, r144);

}

fast_code void pcie_set_xfer_busy(bool busy)
{
	pcie_control_reg_t pcie_ctrlr;

	pcie_ctrlr.all = pcie_wrap_readl(PCIE_CONTROL_REG);
	pcie_ctrlr.b.app_xfer_pending = busy;
	pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);
}

fast_code void pcie_speed_change(u32 speed)
{

	misc_control_1_off_t pcie_misc_ctrl;

	pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	pcie_misc_ctrl.b.dbi_ro_wr_en = 1;
	pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);

	link_control2_link_status2_reg_t link_sts;
	link_sts.all = pcie_core_readl(LINK_CONTROL2_LINK_STATUS2_REG);
	link_sts.b.pcie_cap_target_link_speed = speed;
	pcie_core_writel(link_sts.all, LINK_CONTROL2_LINK_STATUS2_REG);

	pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	pcie_misc_ctrl.b.dbi_ro_wr_en = 0;
	pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);

	u32 r80c = pcie_core_readl(PCIE_CORE_R80C_REG);
	r80c |= BIT17;
	pcie_core_writel(r80c, PCIE_CORE_R80C_REG);
}

fast_code void ig_pcie_link_retrain(u32 retrain_target_speed, u32 retrain_time_delay)
{
	if(plp_trigger)
		return;

	pcie_core_status2_t core_sts2 = {
		.all = pcie_wrap_readl(PCIE_CORE_STATUS2),
	};
	u32 ori_speed = core_sts2.b.neg_link_speed;
	if (ori_speed != 4)
	{
		return;
	}
	rtos_core_trace(LOG_ALW, 0xa70e, "link retrain, (cur) %d to (tar) %d", ori_speed, retrain_target_speed);
	//rtos_core_trace(LOG_ALW, 0, "change speed to gen %d ", retrain_target_speed);
	pcie_speed_change(retrain_target_speed);

	u32 cnt = 0;
	while (1) {

		if(plp_trigger)
			return;

		mdelay(1);
		cnt++;

		if(cnt % 5 == 0)
		{
			core_sts2.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
			if (core_sts2.b.neg_link_speed == (retrain_target_speed))
			{
				break;
			}
			else
			{
				if (cnt >= 100) {
					rtos_core_trace(LOG_ALW, 0x3816, "down speed fail, new speed:%x, target:%x", core_sts2.b.neg_link_speed, (retrain_target_speed));
					break;
				}
			}
		}
	}
	retrain_target_speed = 3;

	if (retrain_time_delay != 0)
	{
		rtos_core_trace(LOG_ALW, 0xf058, "delay %d msec", retrain_time_delay);
		mdelay(retrain_time_delay);
		retrain_time_delay = 0;
	}

	rtos_core_trace(LOG_ALW, 0x5e01, "change speed to gen %d ", ori_speed);
	pcie_speed_change(ori_speed);
}

fast_code void pcie_link_enable(void)
{
	fw_stts_1_t fw_stts_1 = {
		.all = misc_readl(FW_STTS_1),
	};

	fw_stts_1.all += 1;
	misc_writel(fw_stts_1.all, FW_STTS_1);

	if (fw_stts_1.all != 1)
	{
		return;
	}

	reset_ctrl_t reset_ctrl = {
		.all = misc_readl(RESET_CTRL),
	};

	/* Need to wait perst_n pin and internal perst_n to be de-asserted */
	if (reset_ctrl.b.perst_n && reset_ctrl.b.int_perst_n)
	{
	enable:
		misc_writel(0xF, UNM_SYS_INT);

		/*  Reset PCIe MAC or not when perst_n asserts */
#ifdef PERST_MODE_0
		reset_ctrl.b.perst_mode = 0;
#else
		reset_ctrl.b.perst_mode = 1;
#endif
		reset_ctrl.b.clr_ltssm_perst = 1;
		misc_writel(reset_ctrl.all, RESET_CTRL);

		//pcie_phy_step();
#ifdef SHORT_CHANNEL
		pcie_short_channel_improve();
#endif
//#if defined(M2_2A) || defined(U2_LJ) //joe add LJ 20200915
//		pcie_tap1_setting();
//#endif

#ifdef IG_MODIFIED_PHY
		//pcie_tdr_setting();
#ifndef PCIE_PHY_IG_SDK
		pcie_tx_update();
#endif
		pcie_pll_bw_update();
#ifdef PCIE_PHY_IG_SDK
		pcie_tx_update();
#endif
		//pcie_gen3_bandwidth();
		pcie_phy_leq_train();
		pcie_phy_cdr_setting();
#ifdef PCIE_PHY_IG_SDK
		pcie_phy_squelch_update();
		pcie_phy_cdr_relock();
		pcie_phy_pll_lock();
		pcie_phy_cdr_lock_enhance();
		pcie_phy_l11_tx_commode_on();
		pcie_phy_update_dfe_lmt();
		pcie_phy_refclksq_setting();
		pcie_phy_rlm_improve();
		pcie_phy_txdetrx_setting();
#endif

		misc_reset(RESET_PCIE_PHY);

		#ifdef PCIE_TDR_LOOSE
		pcie_tdr_setting_loose();
		#endif
		if (is_pcie_clk_enable())
		{
			pcie_lane_margin_setting();
			pcie_core_setting();
			pcie_gen34_preset();
			pcie_disable_unuse_cfg();
			pcie_disable_base_address();
#if GET_PCIE_ERR
		pcie_perst_flag = 0;
#endif
		}
		//rtos_core_trace(LOG_ERR, 0xbad9, "IG PCIE WORKING");
#else
		//pcie_tdr_setting();
		pcie_tx_update();
		pcie_pll_bw_update();
		pcie_gen3_bandwidth();

		if (is_pcie_clk_enable())
		{
			pcie_lane_margin_setting();
			pcie_core_setting();
			///Andy add for Tencent server issue 2020/10/06
			pcie_disable_unuse_cfg();
			pcie_disable_base_address();
#if GET_PCIE_ERR
			pcie_perst_flag = 0;
#endif

		}
#endif
		pcie_wait_clear_perst_n();

		if (gPlpDetectInIrqDisableMode) //[PCBaher] PLP01
		{
			gPlpDetectInIrqDisableMode = false;
			return;
		}

		/* Enable PCIe log */
		pcie_fsm_log_sel_t pcie_log = {
			.all = pcie_wrap_readl(PCIE_FSM_LOG_SEL),
		};
		pcie_log.b.pcie_log_en = 1;
		pcie_wrap_writel(pcie_log.all, PCIE_FSM_LOG_SEL);

		pcie_control_reg_t pcie_ctrlr = {
			.all = pcie_wrap_readl(PCIE_CONTROL_REG),
		};

#ifdef ASPM_REG_SETTING
#if defined(MPC)
		if (is_pcie_clk_enable())
		{
			//pcie_ctrlr.b.app_xfer_pending = 1;
			pcie_ctrlr.b.app_l1sub_disable = 1;
		}
#endif
#endif

#if defined(FORCE_GEN1) || defined(FORCE_GEN2) || defined(FORCE_GEN3)
		pcie_change_speed();
#endif
		pcie_ctrlr.b.app_ltssm_enable = 1;								/* Enable Link training */
		pcie_ctrlr.b.auto_ltssm_clr_en = 1;								/* clear ltssm while request reset happened */
		pcie_ctrlr.b.app_margining_ready = 1;							/* Enable Margin Ready bit for host tool rx lane margin test*/
		pcie_ctrlr.b.cfg_10bits_tag_comp_support = 1; /* for CV test */ // joe add cv 20200918
		pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);

		rtos_core_trace(LOG_ALW, 0x7db1, "1st PCIe_Link_EN (%d),clk_loop_cnt(%d)", pcie_ctrlr.b.app_ltssm_enable,loop_cnt);

		lane_chk_times = 0;
		lane_chk_bitmap=0;
		pcie_force_lane2=0;
		pcie_lane2_to_1=0;
		pcie_lane1_cnt=0;
		pcie_force_lane4=0;
		//lane_chk_err_ltssm_en = 0;
		pcie_timer_enable = 1;
		pcie_force_4lane();
		timer_count_down(PCIE_LANE_CHK_TIME, pcie_lane_chk);

#if PCIE_PHY_LOG
		mod_timer(&pcie_phy_timer, jiffies + HZ);
#endif

	}
	else
	{
		u32 cnt = 1;
		/* Need to wait perst_n pin and internal perst_n to be de-asserted */
		while (!(reset_ctrl.b.perst_n && reset_ctrl.b.int_perst_n))
		{
			if (cnt >= PCIE_REF_CLK_CHK)
			{
				no_perst = 1;
				rtos_core_trace(LOG_ALW, 0x7f88, "5 min no PERST");
				break;
			}
			cnt++;
			mdelay(1);
			reset_ctrl.all = misc_readl(RESET_CTRL);
		}

		goto enable;
	}
}

//-----------------------------------------------------------------------------
/**
    One-time initialization for PCIe Interrupt\n

    @return
**/
//-----------------------------------------------------------------------------
fast_code void HalIrq_InitPcieInterrupt(void)
{
	if (gPcieRstRedirected == false)
	{
		sirq_register(SYS_VID_PCIE_INT0, pcie_bootup_isr, false);
		misc_sys_isr_enable(SYS_VID_PCIE_INT0);
	}
	else
	{
		sirq_register(SYS_VID_PCIE_INT0, pcie_isr, false);
		misc_sys_isr_enable(SYS_VID_PCIE_INT0);
	}

	sirq_register(SYS_VID_PRESET_DEASSERT, preset_n_isr, false);
	misc_sys_isr_enable(SYS_VID_PRESET_DEASSERT);

	sirq_register(SYS_VID_PRESET_ASEERT, perst_assert_handler, false);
#if GET_PCIE_ERR
	misc_sys_isr_enable(SYS_VID_PRESET_ASEERT);
#endif
	//misc_sys_isr_enable(SYS_VID_PRESET_ASEERT);

	#if !defined(PROGRAMMER)
	pcie_intr_mask_init();
	#endif
}

init_code void pcie_init(void)
{
	pcie_unmsk_intr_t unmask_sts;

	/* clear any spurious interrupts */
	unmask_sts.all = pcie_wrap_readl(PCIE_UNMSK_INTR);
	pcie_wrap_writel(unmask_sts.all, PCIE_UNMSK_INTR);

	/* register and enable pcie interrupt */
	misc_nvm_ctrl_reset(false);

	HalIrq_InitPcieInterrupt();
	#if GET_PCIE_ERR
	pcie_perst_flag = 1;
	#endif
	INIT_LIST_HEAD(&pcie_link_timer.entry);
	pcie_link_timer.function = pcie_link_timer_handling;
	pcie_link_timer.data = "pcie_link_timer";

	#ifdef RXERR_IRQ_RETRAIN
	INIT_LIST_HEAD(&pcie_retrain_timer.entry);
	pcie_retrain_timer.function = pcie_retrain_timer_handling;
	pcie_retrain_timer.data = "pcie_retrain_timer";
	#endif

	INIT_LIST_HEAD(&pcie_abnormal_timer.entry);
	pcie_abnormal_timer.function = pcie_save_log;
	pcie_abnormal_timer.data = "pcie_abnormal_timer";
/*
#if GET_PCIE_ERR
	corr_err_cnter_timer.function = corr_uncorr_err_counter;
	corr_err_cnter_timer.data = NULL;
	mod_timer(&corr_err_cnter_timer, jiffies + 1);
	dump_corr_timer.function = dump_pcie_corr_err;
	dump_corr_timer.data = NULL;
	mod_timer(&dump_corr_timer, jiffies + 1*HZ/10);
#endif
*/
	//pcie_header_reset();

#if PCIE_PHY_LOG
	INIT_LIST_HEAD(&pcie_phy_timer.entry);
	pcie_phy_timer.function = pcie_phy_timer_handler;
	pcie_phy_timer.data = "pcie_phy_sts_timer";
#endif
}
/*!
 * @brief PCIe TDR settings
 *
 * Adjust phy TDR(Tx detect Rx) settings
 * @2'b01 : default
 * @2'b11 : strict
 * @return  not used
 */
#ifdef PCIE_TDR_LOOSE
void pcie_tdr_setting_loose(void);

fast_code void pcie_tdr_setting_loose(void)
{
    u32 r108;
    u32 r118;
    u32 r128;
    u32 r138;

    //"Reset PCIe TDR to loose (00b)"
    r108 = pcie_phy_sts(0x108);
    r108 &= ~(BIT16 | BIT17);
    pcie_phy_cfg(0x108, r108);

    r118 = pcie_phy_sts(0x118);
    r118 &= ~(BIT20 | BIT21);
    pcie_phy_cfg(0x118, r118);

    r128 = pcie_phy_sts(0x128);
    r128 &= ~(BIT24 | BIT25);
    pcie_phy_cfg(0x128, r128);

    r138 = pcie_phy_sts(0x138);
    r138 &= ~(BIT28 | BIT29);
    pcie_phy_cfg(0x138, r138);
}
#endif
/*!
 * @brief check pcie clock is enable or not
 *
 * @return  true if there is pcie clk
 */
fast_code bool is_pcie_clk_enable(void)
{
	pcie_powr_status_t pcie_pwr_sts;
	pcie_pipe_status_t pcie_pipe_sts;
	loop_cnt = 1;
#if(PLP_SUPPORT == 1)
	gpio_int_t gpio_int_status;
#endif
	do
	{
		pcie_pipe_sts.all = pcie_wrap_readl(PCIE_PIPE_STATUS);
#if(PLP_SUPPORT == 1)
		gpio_int_status.all = misc_readl(GPIO_INT);
		if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger)
			return false;
#endif
		mdelay(1);
		if (loop_cnt >= PCIE_REF_CLK_CHK)
		{
			no_clk = 1;
			rtos_core_trace(LOG_WARNING, 0x9a27, "5 min no clk");
			break;
		}
		//rtos_core_trace(LOG_DEBUG, 0, "LOOP COUNT IS %d",loop_cnt);
		loop_cnt++;
	} while ((pcie_pipe_sts.all & 0xF) != 0);
	//rtos_core_trace(LOG_INFO, 0xf129, "loop_cnt (%d)",loop_cnt);

	pcie_pwr_sts.all = pcie_wrap_readl(PCIE_POWR_STATUS);
	if ((pcie_pipe_sts.all & 0xF) != 0)
	{
		rtos_core_trace(LOG_WARNING, 0xf047, "pcie_pipe_sts not rdy %x, mac_pmu_cs %d, loop_cnt %d\n",
						pcie_pipe_sts.all, pcie_pwr_sts.b.mac_pmu_cs, loop_cnt);
		return false;
	}

	return true;
}

/*!
 * @brief check pcie msi is enable or not
 *
 * @return  true if msi is enable
 */
fast_code bool is_pcie_msi_enable(void)
{
	pci_msi_cap_id_next_ctrl_reg_t pcie_msi;

	pcie_msi.all = pcie_core_readl(PCI_MSI_CAP_ID_NEXT_CTRL_REG);

	if (!pcie_msi.b.pci_msi_enable)
		return false;

	return true;
}

fast_code u32 get_pcie_msi_multiple_message_capable(void)
{

	pci_msi_cap_id_next_ctrl_reg_t pcie_msi;

	pcie_msi.all = pcie_core_readl(PCI_MSI_CAP_ID_NEXT_CTRL_REG);

	return pcie_msi.b.pci_msi_multiple_msg_cap;
}

fast_code u32 get_pcie_msi_multiple_message_enable(void)
{
	pci_msi_cap_id_next_ctrl_reg_t pcie_msi;

	pcie_msi.all = pcie_core_readl(PCI_MSI_CAP_ID_NEXT_CTRL_REG);

	return pcie_msi.b.pci_msi_multiple_msg_en;
}

/*!
 * @brief check pcie msi-x is enable or not
 *
 * @return  true if msi-x is enable
 */
fast_code bool is_pcie_msi_x_enable(void)
{
	pci_msix_cap_id_next_ctrl_reg_t pcie_msi_x;

	pcie_msi_x.all = pcie_core_readl(PCI_MSIX_CAP_ID_NEXT_CTRL_REG);

	if (!pcie_msi_x.b.pci_msix_enable)
		return false;

	return true;
}

fast_code u32 get_pcie_msi_x_table_size(void)
{
	pci_msix_cap_id_next_ctrl_reg_t pcie_msi_x;

	pcie_msi_x.all = pcie_core_readl(PCI_MSIX_CAP_ID_NEXT_CTRL_REG);

	return pcie_msi_x.b.pci_msix_table_size;
}
/*
fast_code void pcie_set_pf_max_msi_x(u32 msi_x_count)
{
	//misc_control_1_off_t pcie_misc_ctrl;

	//pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	//pcie_misc_ctrl.b.dbi_ro_wr_en = 1;
	//pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);

	pci_msix_cap_id_next_ctrl_reg_t pcie_msix_cap;
	pcie_msix_cap.all = pcie_core_readl(PCI_MSIX_CAP_ID_NEXT_CTRL_REG);
	pcie_msix_cap.b.pci_msix_table_size = msi_x_count;
	pcie_core_writel(pcie_msix_cap.all, PCI_MSIX_CAP_ID_NEXT_CTRL_REG);

	pcie_msix_cap.all = pcie_core_readl(PCI_MSIX_CAP_ID_NEXT_CTRL_REG);
	//rtos_core_trace(LOG_INFO, 0, "msi_x table size %x\n",
					//pcie_msix_cap.b.pci_msix_table_size);

	//pcie_misc_ctrl.all = pcie_core_readl(MISC_CONTROL_1_OFF);
	//pcie_misc_ctrl.b.dbi_ro_wr_en = 0;
	//pcie_core_writel(pcie_misc_ctrl.all, MISC_CONTROL_1_OFF);
}
*/
ddr_code void enable_pcie_req_exit_l1(void)
{
	pcie_control_reg_t pcie_ctrlr;

	pcie_ctrlr.all = pcie_wrap_readl(PCIE_CONTROL_REG);
	pcie_ctrlr.b.app_req_exit_l1 = 1;
	pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);
}

/*!
 * @brief disable pcie requst exit L1
 *
 * return	None
 */
ddr_code void disable_pcie_req_exit_l1(void)
{
	pcie_control_reg_t pcie_ctrlr;

	pcie_ctrlr.all = pcie_wrap_readl(PCIE_CONTROL_REG);
	pcie_ctrlr.b.app_req_exit_l1 = 0;
	pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);
}

fast_code u32 get_pcie_xfer_mps(void)
{
	pcie_cfg_mps_mrr_t pcie_cfg;

	pcie_cfg.all = pcie_wrap_readl(PCIE_CFG_MPS_MRR);
	return pcie_cfg.b.pcie_cfg_mps;
}

fast_code u32 get_pcie_xfer_mrr(void)
{
	pcie_cfg_mps_mrr_t pcie_cfg;

	pcie_cfg.all = pcie_wrap_readl(PCIE_CFG_MPS_MRR);
	return pcie_cfg.b.pcie_cfg_mrr;
}

ddr_code u32 pcie_link_idle(void)
{
	return !!(pcie_wrap_readl(PCIE_PIPE_STATUS) & BIT4);
}

ddr_code u32 pcie_phy_in_l1(void)
{
	pcie_powr_status_t pcie_stt = {
		.all = pcie_wrap_readl(PCIE_POWR_STATUS),
	};

	return pcie_stt.b.pm_linkst_in_l1_1c;
}

ddr_code void disable_ltssm(u32 ms)
{
	pcie_control_reg_t pcie_ctrlr = {
		.all = pcie_wrap_readl(PCIE_CONTROL_REG),
	};
	pcie_ctrlr.b.app_ltssm_enable = 0;
	pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);
	u64 start_time = get_tsc_64();
	while(time_elapsed_in_ms(start_time) < ms);
	rtos_core_trace(LOG_INFO, 0xd2ba, "disable ltssm %d", ms);
}

ddr_code void enable_ltssm(u32 ms)
{
    pcie_control_reg_t pcie_ctrlr = {
        .all = pcie_wrap_readl(PCIE_CONTROL_REG),
    };
    pcie_ctrlr.b.app_ltssm_enable = 1;
    pcie_wrap_writel(pcie_ctrlr.all, PCIE_CONTROL_REG);
}

#if CPU_ID == 1

static ps_code int pcie_link_main(int argc, char *argv[])
{
	int pcie_link;
	int UP = 0x01;
	int DOWN = 0x00;

#if defined(FPGA)
	/* Pcie Link up/down with Bit 11 - Assert perst_n to PCIe MAC */
	u32 pcie_phy_up = 0xfffff7ff;
	u32 pcie_phy_down = 0x00000800;
#else
	/* Pcie Link up/down with Bit 6 - Reset PCIe PHY */
	u32 pcie_phy_up = 0xffffffbf;
	u32 pcie_phy_down = 0x00000040;
#endif
	u32 rst_ctrl = misc_readl(RESET_CTRL);

	if (argc == 1)
	{
		rtos_core_trace(LOG_ERR, 0xb372, "\nreset_ctrl reg (0x%x)", rst_ctrl);
		return 0;
	}

	pcie_link = atoi(argv[1]);

	if (pcie_link == UP)
	{
		/* pcie phy up - set bit 11 or 6 to b'0 */
		rst_ctrl &= pcie_phy_up;
		misc_writel(rst_ctrl, RESET_CTRL);
	}
	else if (pcie_link == DOWN)
	{
		/* pcie phy down - set bit 11 or 6 to b'1 */
		rst_ctrl |= pcie_phy_down;
		misc_writel(rst_ctrl, RESET_CTRL);
	}
	else
	{
		/* invalid argument */
		rtos_core_trace(LOG_ERR, 0xfc40, "\npcie_link: Error - wrong parameter(%d)", pcie_link);
		return -1;
	}

	return 0;
}

/*!
 * @brief function test pcie rx lane margin
 *
 * @param hdir	 - timing direction (0 for right, 1 for left)
 * @param offset - timing offset (1, 2, 3 etc.)
 * @param vdir	 - voltage direction (0 for up, 1 for down)
 * @param voffset - voltage offset (1, 2, 3, etc.)
 * @param err_cnt - maximum allowed error count
 * @param lane_num - pcie lane num (now support only lane 0)
 *
 * @return
 *	2:  pass
 *	0, 1, 3: different types of errors in a test status
 */
ddr_code u32 pcie_rlm_test(u32 hdir, u32 offset, u32 vdir, u32 voffset, u32 err_cnt, u32 lane_num)
{
	u32 ret = 0;
	u32 voffset_mask = 0x00ffffff; /* voltage offset mask */
	u32 vori;
	u32 cvop; /* current voltage origin point. vori = cvop >> 7 */
	u32 voffset_final = 0;
	u32 vo_val;			 /* voltage offset value */
	u32 final_vo_val;	 /* final voltage offset value  = (vo_val & voffset_mask) | voffset_final */
	u32 eclr_offset = 0; /* error count limit register offset 0x0, 0x4, 0x8, 0xC */
	u32 new_voffset = 0;
	u32 retry_cnt = 0;

redo_test:
#if RXLM_SOUT == 1
	//rtos_core_trace(LOG_INFO, 0, "===================================================");
	rtos_core_trace(LOG_INFO, 0x33ad, "====== Start to do Timing and Voltage Margin ======");
	//rtos_core_trace(LOG_INFO, 0, "===================================================");
	mdelay(3);

	if (hdir == 0)
	{
		rtos_core_trace(LOG_INFO, 0xfc6d, "Timing Direction = Right");
	}
	else
	{
		rtos_core_trace(LOG_INFO, 0x7c82, "Timing Direction = Left");
	}

	rtos_core_trace(LOG_INFO, 0xa83a, "Timing Offset = %xh ", offset);

	if (vdir == 0)
	{
		rtos_core_trace(LOG_INFO, 0x8400, "Voltage Direction = Up");
	}
	else
	{
		rtos_core_trace(LOG_INFO, 0x28e1, "Voltage Direction = Down");
	}

	rtos_core_trace(LOG_INFO, 0x07cb, "Voltage Offset = %xh", voffset);
#endif

	//-------------------------------
	// Phy Initialization
	//-------------------------------
	// Activate Force Approach
#if RXLM_SOUT == 1
	mdelay(3);
#endif

	if (lane_num == 0)
	{
		/* Lane0 */
		misc_writel(0x08C, PHY_ADDRESS);
		misc_writel(0x000, PHY_DATA);
		misc_writel(0x200, PHY_DATA);
		voffset_mask = 0x00ffffff;
		eclr_offset = 0x0;
	}
	else if (lane_num == 1)
	{
		/* Lane1 */
		misc_writel(0x0A4, PHY_ADDRESS);
		misc_writel(0x00000, PHY_DATA);
		misc_writel(0x20000, PHY_DATA);
		voffset_mask = 0x00ffffff;
		eclr_offset = 0x4;
	}
	else if (lane_num == 2)
	{
		/* Lane2 */
		misc_writel(0x0BC, PHY_ADDRESS);
		misc_writel(0x0000000, PHY_DATA);
		misc_writel(0x2000000, PHY_DATA);
		voffset_mask = 0xffff00ff;
		eclr_offset = 0x8;
	}
	else if (lane_num == 3)
	{
		/* Lane3 */
		misc_writel(0x0D8, PHY_ADDRESS);
		misc_writel(0x0, PHY_DATA);
		misc_writel(0x2, PHY_DATA);
		voffset_mask = 0xff00ffff;
		eclr_offset = 0xC;
	}

	if (vdir == 1)
	{
		//change to 2's complement and cast to 8 bits.
		new_voffset = (~voffset + 1) ;
	} else {
		new_voffset = voffset;
	}

	/* Read Lane0, 1, 2 or 3 current voltage origin point. */
	if (lane_num == 0)
	{
		misc_writel(0x214, PHY_ADDRESS);
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_INFO, 0xfc75, "r214 %x", misc_readl(PHY_DATA));
#endif
	}
	else if (lane_num == 1)
	{
		misc_writel(0x230, PHY_ADDRESS);
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_INFO, 0x6b03, "r230 %x", misc_readl(PHY_DATA));
#endif
	}
	else if (lane_num == 2)
	{
		misc_writel(0x24C, PHY_ADDRESS);
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_INFO, 0x7aeb, "r24c %x", misc_readl(PHY_DATA));
#endif
	}
	else if (lane_num == 3)
	{
		misc_writel(0x268, PHY_ADDRESS);
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_INFO, 0xea8b, "r268 %x", misc_readl(PHY_DATA));
#endif
	}
	cvop = misc_readl(PHY_DATA);
	vori = (u8)(cvop >> 7);
#if RXLM_SOUT == 1
	rtos_core_trace(LOG_INFO, 0x5b49, "The Current Voltage Origin Point = 0x%x (2's complement)", (u8)vori);
	mdelay(1);
#endif

	//Decide final voltage offset value
	//set $voffset_final =  ($vori + $new_voffset) << 24
	//printf "The Final Voltage Offset(origin+offset) = %xh (2's complement) \n", (char)($vori + $new_voffset)

	if (lane_num == 0)
	{
		voffset_final = (vori + new_voffset) << 24;
	}
	else if (lane_num == 1)
	{
		voffset_final = (vori + new_voffset) << 24;
	}
	else if (lane_num == 2)
	{
		voffset_final = (vori + new_voffset) << 8;
	}
	else if (lane_num == 3)
	{
		voffset_final = (vori + new_voffset) << 16;
	}
#if RXLM_SOUT == 1
	mdelay(1);
	rtos_core_trace(LOG_INFO, 0x25a3, "The Final Voltage Offset(origin+offset) = 0x%x (2's complement)", (u8)(vori + new_voffset));
#endif

	/* Read voltage offset value and calculate final voltage offset value */
	if (lane_num == 0)
	{
		misc_writel(0x84, PHY_ADDRESS);
	}
	else if (lane_num == 1)
	{
		misc_writel(0x84, PHY_ADDRESS);
	}
	else if (lane_num == 2)
	{
		misc_writel(0xB8, PHY_ADDRESS);
	}
	else if (lane_num == 3)
	{
		misc_writel(0xD0, PHY_ADDRESS);
	}

	vo_val = misc_readl(PHY_DATA);
	final_vo_val = (vo_val & voffset_mask) | voffset_final;
	//
	if (lane_num == 0)
	{
		misc_writel(0x84, PHY_ADDRESS);
	}
	else if (lane_num == 1)
	{
		misc_writel(0x84, PHY_ADDRESS);
	}
	else if (lane_num == 2)
	{
		misc_writel(0xB8, PHY_ADDRESS);
	}
	else if (lane_num == 3)
	{
		misc_writel(0xD0, PHY_ADDRESS);
	}
	misc_writel(final_vo_val, PHY_DATA);
	rtos_core_trace(LOG_ALW, 0x58be, "final_vo_val = 0x%x ", final_vo_val);

	/* set error count */
	eclr_offset += MARGIN_LANE_CNTRL_STATUS0_REG;
	pcie_core_writel((0x0000C016 | (err_cnt << 8)), eclr_offset);
#if RXLM_SOUT == 1
	rtos_core_trace(LOG_ALW, 0x085b, "Error Count Limit = 0x%x ", err_cnt);
#endif

	//#-------------------------------
	//# STEP 1
	//#-------------------------------
	//# No Command
	/* Lane0: set *(int *)0xc00201a4=0x00009C38 */
	pcie_core_writel(0x00009C38, eclr_offset);

	//# Clear Error Log
	/* Lane0: set *(int *)0xc00201a4=0x00005516 */
	pcie_core_writel(0x00005516, eclr_offset);

	//#-------------------------------
	//# STEP 2: Start Margin Timing: direction and Offset.
	//#-------------------------------

	//#-------------------------------
	//# STEP 3: Change the Timing offset to start Timing margin again.
	//#-------------------------------
	//# No Command
	/* Lane0: set *(int *)0xc00201a4=0x00009C38 */
	pcie_core_writel(0x00009C38, eclr_offset);

	//# Step Margin to Timing offset to right/left of default. [6] right/left [5:0] number of steps.
	pcie_core_writel((0x1E | (hdir << 14) | (offset << 8)), eclr_offset);

	//# read error count
#if RXLM_SOUT == 1
	rtos_core_trace(LOG_INFO, 0xf81e, "======= Result =======");
#endif
	mdelay(1);
	u32 status = pcie_core_readl(eclr_offset);
	status = (status & 0xc0000000) >> 30;

	switch (status)
	{
	case 0x0:
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_ALW, 0x0949, "Execution Status = Too Many Error");
#endif
		ret = RLM_TOO_MANY_ERR;
		break;
	case 0x1:
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_ALW, 0x722e, "Not started yet");
#endif
		ret = RLM_NOT_STARTED;
		break;
	case 0x2:
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_ALW, 0x1a37, "Execution Status = In progress");
#endif
		ret = RLM_IN_PROGRESS;
		break;
	case 0x3:
#if RXLM_SOUT == 1
		rtos_core_trace(LOG_ALW, 0xd29a, "Execution Status = NAK");
#endif
		ret = RLM_NAK;
		break;
	default:
		rtos_core_trace(LOG_INFO, 0x84bc, "default status, should not come here");
		break;
	}

	u32 error_cnt = (pcie_core_readl(eclr_offset) & 0x3f000000) >> 24;
	rtos_core_trace(LOG_ALW, 0xfd92, "Status %d Error Count = 0x%x ", ret, error_cnt);

	//#-------------------------------
	//# STEP 4: Stop Margin
	//#-------------------------------
	//# No Command
	pcie_core_writel(0x00009C38, eclr_offset);
	//# Go To Normal Settings
	pcie_core_writel(0x00000F16, eclr_offset);
	//# No Command
	pcie_core_writel(0x00009C38, eclr_offset);
	//# Clear Error Log
	pcie_core_writel(0x00005516, eclr_offset);
	//# No Command
	pcie_core_writel(0x00009C38, eclr_offset);

	//#-------------------------------
	//# Phy Forced Voltage Disable
	//#-------------------------------

	if (lane_num == 0)
	{
		/* Lane0 */
		misc_writel(0x08C, PHY_ADDRESS);
		misc_writel(0x000, PHY_DATA);
	}
	else if (lane_num == 1)
	{
		/* Lane1 */
		misc_writel(0x0A4, PHY_ADDRESS);
		misc_writel(0x00000, PHY_DATA);
	}
	else if (lane_num == 2)
	{
		/* Lane2 */
		misc_writel(0x0BC, PHY_ADDRESS);
		misc_writel(0x0000000, PHY_DATA);
	}
	else if (lane_num == 3)
	{
		/* Lane3 */
		misc_writel(0x0D8, PHY_ADDRESS);
		misc_writel(0x0, PHY_DATA);
	}

#if RXLM_SOUT == 1
	//rtos_core_trace(LOG_INFO, 0, "===================================================\n");
	rtos_core_trace(LOG_INFO, 0x6dd0, "========== Rx Lane Margin Done ====================\n");
	//rtos_core_trace(LOG_INFO, 0, "===================================================\n");
#endif
	mdelay(1);

	if ((ret != RLM_IN_PROGRESS) && (retry_cnt < 3)) {
		retry_cnt++;
		//printk("Retry x_offset %d, v_offset %d (%d)\n", offset, voffset, retry_cnt);
		goto redo_test;
	}

	return ret;
}

#define PCIE_RXLM_TIMING_OFFSET 16	/* number of timing offset steps */
#define PCIE_RXLM_VOLTAGE_OFFSET 96 /* number of voltage offset steps */
#define PCIE_RXLM_STEP_SIZE 1		/* default size of steps */

/*!
 * @brief This function provide uart interface to test pcie rx lane margin for
 *	  different timing direction, timing offset, voltage direction and voltage offset.
 *
 * @param arg1	pcie lane # (optional. default: 0; supported 0, 1,2,3)
 * @param arg2	timing and voltage offsets (optional. detault:1; 2, 3, 4 etc.)
 *
 * @return
 *	0:  pass
 */

slow_code int pcie_rx_margin(u32 lane_num, u32 step, u32 *x1, u32 *x2, u32 *y1, u32 *y2)
{
	// check if PCIe Link is Gen 4
	pcie_core_status2_t core_sts2 = {
		.all = pcie_wrap_readl(PCIE_CORE_STATUS2),
	};
	if (core_sts2.b.neg_link_speed != 4)
	{
		rtos_core_trace(LOG_ERR, 0xf424, "PCIe speed not Gen4!");
		return -1;
	}

	u32 i;
	u32 result;
	/* 1: timing direction is right */
	for (i = 0; i < PCIE_RXLM_TIMING_OFFSET; i += step)
	{
		result = pcie_rlm_test(0, i, 0, 0, 5, lane_num);
		if (result != 2)
		{
			break;
		}
	}
	*x1 = i - step;
	mdelay(1);

	/* 2: timing direction is left */
	for (i = 0; i < PCIE_RXLM_TIMING_OFFSET; i += step)
	{
		result = pcie_rlm_test(1, i, 0, 0, 5, lane_num);
		if (result != 2)
		{
			break;
		}
	}
	*x2 = i - step;
	mdelay(1);

	u32 mid_x;
	if (*x1 >= *x2) {
		mid_x = (*x1 - *x2) / 2;
		rtos_core_trace(LOG_ERR, 0x97eb, "====== mid_x %d ======\n", mid_x);
	} else {
		mid_x = (*x2 - *x1) / 2;
		rtos_core_trace(LOG_ERR, 0x5f1c, "====== mid_x -%d ======\n", mid_x);
	}

	u32 hdir = 1;
	if (mid_x > 0) hdir = 0;

	/* 3: voltage direction is up */
	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i += step)
	{
		result = pcie_rlm_test(hdir, mid_x, 0, i, 5, lane_num);
		if (result != 2)
		{
			break;
		}
	}
	*y1 = i - step;
	mdelay(1);

	/* 4: voltage direction is down */
	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i += step)
	{
		result = pcie_rlm_test(hdir, mid_x, 1, i, 5, lane_num);
		if (result != 2)
		{
			break;
		}
	}
	*y2 = i - step;
	mdelay(1);

	return 0;
}

// static ps_code int pcie_rx_lane_margin_main(int argc, char *argv[])
// {

// 	u32 x1 = 0;
// 	u32 x2 = 0;
// 	u32 y1 = 0;
// 	u32 y2 = 0;
// 	u32 lane_num;
// 	u32 step = 1;
//	pcie_core_status2_t link_sts;
//	u64 rxlm_start;

//	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);

//	if (link_sts.b.neg_link_speed != 4) {
//		printk("\nPCIe speed not Gen4!\n");
//		return -1;
//	}

// 	if (argc > 3) {
// 		rtos_core_trace(LOG_ERR, 0, "\nIncorrect # of parameters %d\n", argc);
// 		return -1;
// 	}
// 	if (argc == 3) {
// 		lane_num = atoi(argv[1]);
// 		step = atoi(argv[2]);
// 	} else if (argc == 2) {
// 		lane_num = atoi(argv[1]);
// 		step = PCIE_RXLM_STEP_SIZE;
// 	} else {
// 		lane_num = 0;
// 		step = PCIE_RXLM_STEP_SIZE;
// 	}
// 	if ((lane_num < 0) || (lane_num > 3)) {
// 		rtos_core_trace(LOG_ERR, 0, "\nWrong lane # %d\n", lane_num);
// 		return -2;
// 	}

//	rxlm_start = get_tsc_64();
// 	pcie_rx_margin(lane_num,step,&x1,&x2,&y1,&y2);
//	rtos_core_trace(LOG_ALW, 0, "===== Lane %d Four point %d ms================", lane_num, time_elapsed_in_ms(rxlm_start));
//	rtos_core_trace(LOG_ALW, 0, "===== rx_lane_margin result x1 = %d", x1);
//	rtos_core_trace(LOG_ALW, 0, "===== rx_lane_margin result x2 = -%d", x2);
//	rtos_core_trace(LOG_ALW, 0, "===== rx_lane_margin result y1 = %d", y1);
//	rtos_core_trace(LOG_ALW, 0, "===== rx_lane_margin result y2 = -%d", y2);


// 	return 0;
// }

slow_code int pcie_rx_eye(u32 lane_num, u32 op_mode, int *x1, int *x2, int *x3, int *x4)
{
	//                   |
	//                   |
	//       X2          |         X1
	//                   |
	//-----------------------------------------
	//                   |
	//       X3          |         X4
	//                   |
	//                   |
	u32 i, j, k = 0;
	u32 result;
	// int x1[96]; // up right
	// int x2[96]; // up left
	// int x3[96]; // down left
	// int x4[96]; // down right
	//u32 lane_num;
	u32 middle;
	u32 right_max;
	u32 left_max;
	rtos_core_trace(LOG_ERR, 0x83ba, "lane_num = %d, op_mode = %d\n", lane_num, op_mode);

	pcie_set_xfer_busy(true);

	// if (argc > 2) {
	// 	rtos_core_trace(LOG_ERR, 0, "\nIncorrect # of parameters %d\n", argc);
	// 	return -1;
	// }
	// if (argc == 2) {
	// 	lane_num = atoi(argv[1]);
	// 	rtos_core_trace(LOG_ERR, 0, " lane num : %d \n", lane_num);
	// } else {
	// 	lane_num = 0;
	// }

	/* When Y = 0, find MaxX value */
	for (i = 0; i < PCIE_RXLM_TIMING_OFFSET; i++)
	{
		result = pcie_rlm_test(0, i, 0, 0, 5, lane_num);
		if (result != 2)
		{
			break;
		}
	}
	right_max = i;

	/* When Y = 0, find MinX value */
	for (i = 0; i < PCIE_RXLM_TIMING_OFFSET; i++)
	{
		result = pcie_rlm_test(1, i, 0, 0, 5, lane_num);
		if (result != 2)
		{
			break;
		}
	}
	left_max = i;

	if (right_max > left_max)
	{
		middle = (right_max - left_max) >> 1;
	}
	else
	{
		middle = (left_max - right_max) >> 1;
		rtos_core_trace(LOG_ERR, 0x11a6, "Should not come here\n");
	}

	rtos_core_trace(LOG_ERR, 0x05ee, " right max %d, left max %d, middle %x\n", right_max, left_max, middle);

	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i++)
	{
		for (j = 0; j < PCIE_RXLM_TIMING_OFFSET; j += 1)
		{
			result = pcie_rlm_test(0, j, 0, i, 5, lane_num);
			if (result != 2)
			{
				if (middle > 0)
				{
					if (j == 0)
					{
						j = middle;
						continue;
					}
					else
					{
						if (j == middle)
						{
							rtos_core_trace(LOG_ERR, 0x31db, " j %d, middle %d, result %d\n", j, middle, result);
							j = 0;
						}
						break;
					}
				}
				break;
			}
		}
		rtos_core_trace(LOG_ERR, 0x7575, "[result]x1 i[%d] j[%d] \n", i, j);
		x1[i] = j;
	}

	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i++)
	{
		for (j = 0; j < PCIE_RXLM_TIMING_OFFSET; j += 1)
		{
			result = pcie_rlm_test(1, j, 0, i, 5, lane_num);
			if (result != 2)
			{
				if ((middle > 0) && (j == 0))
				{
					for (k = 0; k < middle; k++)
					{
						result = pcie_rlm_test(0, k, 0, i, 5, lane_num);
						if (result == 2)
						{
							break;
						}
					}
				}

				break;
			}
		}
		rtos_core_trace(LOG_ERR, 0xb7a8, "[result]x2 i[%d] j[%d] k[%d]\n", i, j, k);
		if (j == 0)
		{
			x2[i] = -k;
		}
		else
		{
			x2[i] = j;
		}
	}

	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i++)
	{
		for (j = 0; j < PCIE_RXLM_TIMING_OFFSET; j += 1)
		{
			result = pcie_rlm_test(1, j, 1, i, 5, lane_num);
			if (result != 2)
			{
				if ((middle > 0) && (j == 0))
				{
					for (k = 0; k < middle; k++)
					{
						result = pcie_rlm_test(0, k, 1, i, 5, lane_num);
						if (result == 2)
						{
							break;
						}
					}
				}

				break;
			}
		}
		rtos_core_trace(LOG_ERR, 0x86af, "[result]x3 i[%d] j[%d] k[%d]\n", i, j, k);
		if (j == 0)
		{
			x3[i] = -k;
		}
		else
		{
			x3[i] = j;
		}
	}

	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i++)
	{
		for (j = 0; j < PCIE_RXLM_TIMING_OFFSET; j += 1)
		{
			result = pcie_rlm_test(0, j, 1, i, 5, lane_num);
			if (result != 2)
			{
				if (middle > 0)
				{
					if (j == 0)
					{
						j = middle;
						continue;
					}
					else
					{
						if (j == middle)
							j = 0;
						break;
					}
				}

				break;
			}
		}
		rtos_core_trace(LOG_ERR, 0xf53f, "[result]x4 i[%d] j[%d] \n", i, j);
		x4[i] = j;
	}

	for (i = 0; i < PCIE_RXLM_VOLTAGE_OFFSET; i++)
	{
		rtos_core_trace(LOG_ERR, 0x22ca, "[%d] x1 %d x2 %d x3 %d x4 %d \n", i, x1[i], x2[i], x3[i], x4[i]);
	}

	pcie_set_xfer_busy(false);
	return 0;
}

/* @param hdir	 - timing direction (0 for right, 1 for left)
 * @param offset - timing offset (1, 2, 3 etc.)
 * @param vdir	 - voltage direction (0 for up, 1 for down)
 * @param voffset - voltage offset (1, 2, 3, etc.)
 * @param err_cnt - maximum allowed error count
 * @param lane_num - pcie lane num (now support only lane 0)
 */
static ddr_code int pcie_rx_point(int argc, char *argv[])
{
	u32 hdir = 0;
	u32 offset = 0;
	u32 vdir = 0;
	u32 voffset = 0;
	u32 lane_num = 0;
	u32 result = 0;

	if (argc == 6)
	{
		hdir = atoi(argv[1]);
		offset = atoi(argv[2]);
		vdir = atoi(argv[3]);
		voffset = atoi(argv[4]);
		lane_num = atoi(argv[5]);
		rtos_core_trace(LOG_ERR, 0x68af, "lane %x :\n", lane_num);
		rtos_core_trace(LOG_ERR, 0xbfd4, "hdir %d, ofst %d, vdir %d, voffset %d\n", hdir, offset, vdir, voffset, lane_num);
	}

	result = pcie_rlm_test(hdir, offset, vdir, voffset, 5, lane_num);

	rtos_core_trace(LOG_ERR, 0x5ef2, "result %x \n", result);

	return 0;
}

static ddr_code int lspci_main(int argc, char *argv[])
{
	pcie_core_status2_t link_sts;

	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);

	rtos_core_trace(LOG_ALW, 0x4bae, "PCIe gen%d x %d\n",
					link_sts.b.neg_link_speed,
					link_sts.b.neg_link_width);
	pcie_dbglog();						// Print out history LTSSM.
	return 0;
}

#if GET_PCIE_ERR
//fast_code void corr_uncorr_err_counter(void *data)
fast_code void corr_uncorr_err_counter(void *data)
{
    #if 1 // added by Ethan, for PLP not activate, 20210305
    volatile pcie_core_status_t core_status;
    core_status.all = pcie_wrap_readl(PCIE_CORE_STATUS);

    //extern u8 plp_trigger;

    if( pcie_perst_flag || plp_trigger || (err_timer_flag == false) ||(core_status.b.smlh_ltssm_state != 0x11))
	{
        mod_timer(&corr_err_cnter_timer, jiffies + 1);
        return;
    }
    #else
    if((err_timer_flag == false) || (cc_en_set == false)){
        mod_timer(&corr_err_cnter_timer, jiffies + 1*HZ/10);
        return;
    }
    #endif
	if(!is_bad_tlp_masked)
	{
		//rtos_core_trace(LOG_PCIE_ERR, 0, "AER counter will read/write register");
	    correctable_err_status_reg_t correctable_err_status = {.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG)};
	    //uncorrectable_err_status_reg_t uncorrectable_err_status = {.all = pcie_core_readl(UNCORRECTABLE_ERROR_STATUS_REG)};
	    //device_control_device_status_t device_control_device_status = {.all = pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS)};

		if(correctable_err_status.b.bad_tlp)
		{
	        if (((bad_tlp_start_time < get_tsc_lo()) && (get_tsc_lo() - bad_tlp_start_time > (CYCLE_PER_MS*100))) || \
	            ((bad_tlp_start_time > get_tsc_lo()) && (INV_U32 - bad_tlp_start_time + get_tsc_lo() >  \
	            (CYCLE_PER_MS*100))) || bad_tlp_start_time == 0)
	        {
				tx_smart_stat->pcie_correctable_error_count[0]+=1;
	            corr_err_cnt.bad_tlp_cnt++;
	            bad_tlp_start_time = get_tsc_lo();
	            rtos_core_trace(LOG_PCIE_ERR, 0xf24a, " bad tlp occurs, cnt %d",corr_err_cnt.bad_tlp_cnt);
	        }
	    }
		/*
		if(correctable_err_status.all){
	    if(device_control_device_status.b.pcie_cap_corr_err_report_en == 0)
	    {
	        rtos_core_trace(LOG_PCIE_ERR, 0, "device no report, correctable_err_status %x ", correctable_err_status.all);
	        pcie_corr_err(correctable_err_status);
	    }
	    else{
	        correctable_err_mask_reg_t correctable_err_mask = {.all = pcie_core_readl(CORRECTABLE_ERROR_MASK_REG)};
	        correctable_err_status.all &= correctable_err_mask.all;
	        if(correctable_err_status.all){
	            rtos_core_trace(LOG_PCIE_ERR, 0, "polling masked correctable_err_status %x ", correctable_err_status.all);
	            pcie_corr_err(correctable_err_status);
	        }
	    }
	    */
	     /*
		    if(uncorrectable_err_status.all){
		        if(device_control_device_status.b.pcie_cap_non_fatal_err_report_en == 0 && device_control_device_status.b.\
					pcie_cap_fatal_err_report_en == 0)
		        {
		            rtos_core_trace(LOG_PCIE_ERR, 0, "device no report, uncorrectable_err_status %x ", uncorrectable_err_status.all);
		            pcie_uncorr_err(uncorrectable_err_status);
		        }
		        else{
		            uncorrectable_err_mask_reg_t uncorrectable_err_mask = {.all = pcie_core_readl(UNCORRECTABLE_ERROR_MASK_REG)};
		            uncorrectable_err_status.all &= uncorrectable_err_mask.all;
		            if(uncorrectable_err_status.all){
		                rtos_core_trace(LOG_PCIE_ERR, 0, "polling masked uncorrectable_err_status %x ", uncorrectable_err_status.all);
		                pcie_uncorr_err(uncorrectable_err_status);
		            }
		        }
		    }
	    */
	    pcie_core_enable_ro_write(true);
	    pcie_core_writel(correctable_err_status.all, CORRECTABLE_ERROR_STATUS_REG);
	    //pcie_core_writel(uncorrectable_err_status.all, CORRECTABLE_ERROR_STATUS_REG);
	    pcie_core_enable_ro_write(false);
	}
	    //if(err_timer_flag)
	    mod_timer(&corr_err_cnter_timer, jiffies + 1*HZ/10);
}
/*
static ddr_code int reset_err_status(int argc, char *argv[])
{
    rtos_core_trace(LOG_ERR, 0, " correctable err ");
    correctable_err_status_reg_t correctable_err_status;
    correctable_err_status.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG);
    rtos_core_trace(LOG_ERR, 0, " bad tlp occurs, correctable_err_status %x",correctable_err_status.all);
    if(correctable_err_status.b.bad_tlp){
        bad_tlp_cnt++;
        rtos_core_trace(LOG_ERR, 0, " bad tlp occurs, cnt %d",bad_tlp_cnt);
        //correctable_err_status.all |= BAD_TLP_MASK_MASK;
        pcie_core_enable_ro_write(true);
        pcie_core_writel(correctable_err_status.all, CORRECTABLE_ERROR_STATUS_REG);
        pcie_core_enable_ro_write(false);
        correctable_err_status.all = pcie_core_readl(CORRECTABLE_ERROR_STATUS_REG);
        rtos_core_trace(LOG_ERR, 0, " bad tlp occurs, correctable_err_status %x",correctable_err_status.all);
    }
    return 0;
}*/

ddr_code void print_corr_err(void)
{
    rtos_core_trace(LOG_INFO, 0xb36b, " receiver_err_cnt %d ",corr_err_cnt.receiver_err_cnt);
    rtos_core_trace(LOG_INFO, 0x474f, " bad_tlp_cnt %d ",corr_err_cnt.bad_tlp_cnt);
    rtos_core_trace(LOG_INFO, 0xc6ee, " bad_dllp_cnt %d ",corr_err_cnt.bad_dllp_cnt);
    rtos_core_trace(LOG_INFO, 0x2500, " replay_num_rollover_cnt %d ",corr_err_cnt.replay_num_rollover_cnt);
    rtos_core_trace(LOG_INFO, 0xbf1e, " replay_timer_timeout_cnt %d ",corr_err_cnt.replay_timer_timeout_cnt);
    rtos_core_trace(LOG_INFO, 0xec3c, " advisory_non_fatal_error_cnt %d ",corr_err_cnt.advisory_non_fatal_error_cnt);
    rtos_core_trace(LOG_INFO, 0x3f0e, " corrected_internal_error_cnt %d ",corr_err_cnt.corrected_internal_error_cnt);
    rtos_core_trace(LOG_INFO, 0xceff, " header_log_overflow_cnt %d ",corr_err_cnt.header_log_overflow_cnt);
}

ddr_code void print_uncorr_err(void)
{
    rtos_core_trace(LOG_ERR, 0x47d9, " undefined err occurs, cnt %d",uncorr_err_cnt.undefined_cnt);
    rtos_core_trace(LOG_ERR, 0xda3a, " data_link_protocol_err err occurs, cnt %d",uncorr_err_cnt.data_link_protocol_err_cnt);
    rtos_core_trace(LOG_ERR, 0x3a1c, " surprise_down_err err occurs, cnt %d",uncorr_err_cnt.surprise_down_err_cnt);
    rtos_core_trace(LOG_ERR, 0x115c, " poisoned_tlp_received err occurs, cnt %d",uncorr_err_cnt.poisoned_tlp_received_cnt);
    rtos_core_trace(LOG_ERR, 0x7238, " flow_control_protocol_err err occurs, cnt %d",uncorr_err_cnt.flow_control_protocol_err_cnt);
    rtos_core_trace(LOG_ERR, 0x1a92, " complement_timeout err occurs, cnt %d",uncorr_err_cnt.complement_timeout_cnt);
    rtos_core_trace(LOG_ERR, 0x358e, " completer_abort err occurs, cnt %d",uncorr_err_cnt.completer_abort_cnt);
    rtos_core_trace(LOG_ERR, 0x7908, " unexpected_complement err occurs, cnt %d",uncorr_err_cnt.unexpected_complement_cnt);
    rtos_core_trace(LOG_ERR, 0x210a, " receiver_overflow err occurs, cnt %d",uncorr_err_cnt.receiver_overflow_cnt);
    rtos_core_trace(LOG_ERR, 0xbe48, " malformed_tlp_cnt err occurs, cnt %d",uncorr_err_cnt.malformed_tlp_cnt);
    rtos_core_trace(LOG_ERR, 0x3523, " ercr_err err occurs, cnt %d",uncorr_err_cnt.ercr_err_cnt);
    rtos_core_trace(LOG_ERR, 0x151d, " unsupported_request_err err occurs, cnt %d",uncorr_err_cnt.unsupported_request_err_cnt);
    rtos_core_trace(LOG_ERR, 0xe9a7, " acs_violation err occurs, cnt %d",uncorr_err_cnt.acs_violation_cnt);
    rtos_core_trace(LOG_ERR, 0x3954, " uncorrectable_internal_err err occurs, cnt %d",uncorr_err_cnt.uncorrectable_internal_err_cnt);
    rtos_core_trace(LOG_ERR, 0xa534, " mc_blocked_tlp err occurs, cnt %d",uncorr_err_cnt.mc_blocked_tlp_cnt);
    rtos_core_trace(LOG_ERR, 0x922f, " atomicop_egress_blocked err occurs, cnt %d",uncorr_err_cnt.atomicop_egress_blocked_cnt);
    rtos_core_trace(LOG_ERR, 0xb3e7, " tlp_prefix_blocked_err err occurs, cnt %d",uncorr_err_cnt.tlp_prefix_blocked_err_cnt);
    rtos_core_trace(LOG_ERR, 0xeaf5, " poisoned_tlp_egress_blocked err occurs, cnt %d",uncorr_err_cnt.poisoned_tlp_egress_blocked_cnt);
}

ddr_code void dump_pcie_corr_err(void *data)
{
    if((err_timer_flag == false) || (cc_en_set == false)){
        mod_timer(&dump_corr_timer, jiffies + 100*HZ/10);
        return;
    }
    print_corr_err();
    mod_timer(&dump_corr_timer, jiffies + 6000*HZ/10);
}

static ddr_code int pcie_err(int argc, char *argv[])
{
    int type;
    type = atoi(argv[1]);
    switch(type){
        case 1 :
            print_corr_err();
            break;
        case 2 :
            print_uncorr_err();
            break;
        default:
            break;
    }
    return 0;
}


static DEFINE_UART_CMD(pcie_err, "pcie_err",
					   "pcie_err",
					   "pcie_err",
					   0, 1, pcie_err);
#endif

#ifdef CPL_TO_ERR_INJECT
ddr_code void pcie_set_core_mps(u32 mps)
{
    device_control_device_status_t pcie_device_sts;
    pcie_device_sts.all= pcie_core_readl(DEVICE_CONTROL_DEVICE_STATUS);
    pcie_device_sts.b.pcie_cap_max_payload_size_cs = mps;
    pcie_core_writel(pcie_device_sts.all, DEVICE_CONTROL_DEVICE_STATUS);
}
static ddr_code int pcie_mps_main(int argc, char *argv[])
{
       u32 mps = 0;

       if (argc == 2) {
               mps = atoi(argv[1]);
       }

       printk("uart set pcie mps %x \n", mps);
       if (mps > 2) {
               printk("Valid mps = 0/1/2,  %x is not valid\n", mps);
               goto end;
       }

       pcie_set_core_mps(mps);

end:
       return 0;
}

static DEFINE_UART_CMD(pcie_mps, "pcie_mps",
       "change pcie mps setting",
       "change pcie mps setting",
       0, 1, pcie_mps_main);
#endif

ddr_code void pcie_link_width_config(u32 width)
{
	//if ((width != 1) && (width != 2) && (width != 4)) {
	//	rtos_core_trace(LOG_ERR, 0, "invalid width :%x, only support 1/2/4", width);
	//}

	u32 pcie_width_cfg;

	pcie_width_cfg = readl((void*)0xc00208c0);
	pcie_width_cfg &= (~0x3F);
	if (width == 1) {
		pcie_width_cfg |= BIT(0);
	}
	else if (width == 2) {
		pcie_width_cfg |= BIT(1);
	}
	else if (width == 4) {
		pcie_width_cfg |= BIT(2);
	}

	pcie_width_cfg |= BIT6;

	writel(pcie_width_cfg, (void*)0xc00208c0);
}

fast_code void pcie_link_retrain(void)
{
	pcie_core_status2_t link_sts;
	//u32 loop_cnt = 0;
	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	rtos_core_trace(LOG_ALW, 0xc0cf, "gen: %d lane :%d",link_sts.b.neg_link_speed,link_sts.b.neg_link_width);

#ifdef LANE_LOSS_RETRAIN
	//if (((link_sts.b.neg_link_speed != 4) || (link_sts.b.neg_link_width != 4)) && (link_retry_cnt<3)) //TODO SUDA
	if ((link_sts.b.neg_link_width != 4) && (link_retry_cnt < 3))
	{
		disable_ltssm(2);
		enable_ltssm(0);
		link_retry_cnt++;
		rtos_core_trace(LOG_ALW, 0x09c8, "retry_cnt :%d",link_retry_cnt);
		//misc_reset(RESET_PCIE);
	}
#endif

	#ifdef LONG_CPL_TO
	else
	{
		set_cpl_to_val();
	}
	#endif
	//do
	//{
	//	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);

	//	pcie_link_width_config(4);
	//	mdelay(1);

	//	if (loop_cnt >= 3000)
	//	{
	//		rtos_core_trace(LOG_ALW, 0, "retrain fail.....");
	//		pcie_phy_leq_dfe();
	//		pcie_phy_cali_results();
	//		break;
	//	}
	//	loop_cnt++;
	//} while (link_sts.b.neg_link_width != 4);

	//rtos_core_trace(LOG_ALW, 0, "retrain:%d",loop_cnt);

	//link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	//rtos_core_trace(LOG_ALW, 0, "lane :%d", link_sts.b.neg_link_width);
}

static ddr_code int pcie_width_main(int argc, char *argv[])
{
	pcie_core_status2_t link_sts;
	if (argc == 1)
		goto end;

	u32 width = atoi(argv[1]);

	pcie_link_width_config(width);
	mdelay(1);
end:

	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	rtos_core_trace(LOG_ALW, 0xb717, "PCIe gen %d x %d", link_sts.b.neg_link_speed,
						    link_sts.b.neg_link_width);
	return 0;
}

static DEFINE_UART_CMD(pcie_width, "pcie_width",
	"pcie_width 1/2/4",
	"syntax: pcie_width 1/2/4",
	0, 1, pcie_width_main);

/*
static DEFINE_UART_CMD(reset_err_status, "reset_err_status",
					   "reset_err_status",
					   "reset_err_status",
					   0, 0, reset_err_status);*/


static DEFINE_UART_CMD(pcie_link, "pcie_link",
					   "change pcie link down or up",
					   "syntax: pcie_link n [n = 0(link down), n = 1(link up)]",
					   0, 1, pcie_link_main);

// static DEFINE_UART_CMD(rx_lane_margin, "rx_lane_margin",
// 	"pcie rx lane margin",
// 	"rx_lane_margin [pcie lane #] [offset step size]",
// 	0, 3, pcie_rx_lane_margin_main);

// static DEFINE_UART_CMD(rx_eye, "rx_eye",
// 	"pcie rx eye diagram",
// 	"rx_eye [pcie lane #]",
// 	0, 1, pcie_rx_eye);

static DEFINE_UART_CMD(rx_point, "rx_point",
					   "pcie rx lane_point",
					   "rx_point [pcie lane #]",
					   5, 5, pcie_rx_point);

static DEFINE_UART_CMD(lspci, "lspci",
					   "show pcie link status",
					   "lspci",
					   0, 0, lspci_main);


#endif

/*! @} */
