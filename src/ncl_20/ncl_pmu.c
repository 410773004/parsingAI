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
/*! \file ncl_pmu.c
 * @brief NCL PMU module
 *
 * \addtogroup ncl_20
 * \defgroup ncl_pmu
 * \ingroup ncl_20
 *
 * @{
 */
#include "pmu.h"
#include "ndphy.h"
#include "ndcu.h"
#include "eccu.h"
#include "ficu.h"
#include "nand.h"
#include "ndcu_reg_access.h"
#include "eccu_reg_access.h"
#include "ficu_reg_access.h"
#include "ncl.h"
#include "ncl_erd.h"
#include "ncl_cmd.h"
#include "mpc.h"

/*! \cond PRIVATE */
#define __FILEID__ nclpmu
#include "trace.h"

/*! \endcond */
struct ncb_reg_range_t {
	u32 offset 	: 16;
	u32 cnt		: 16;
};

#define NDPHY_REG(reg) {.offset = (NDPHY_REG_ADDR(0) & 0xFFFF) + reg, .cnt = 1,}
#define NDPHY_RANGE(start, end) {.offset = (NDPHY_REG_ADDR(0) & 0xFFFF) + start, .cnt = (end - start)/4 + 1,}
#define NDCU_REG(reg) {.offset = (NDCU_REG_ADDR & 0xFFFF) + reg, .cnt = 1,}
#define NDCU_RANGE(start, end) {.offset = (NDCU_REG_ADDR & 0xFFFF) + start, .cnt = (end - start)/4 + 1,}
#define ECCU_REG(reg) {.offset = (ECCU_REG_ADDR & 0xFFFF) + reg, .cnt = 1,}
#define ECCU_RANGE(start, end) {.offset = (ECCU_REG_ADDR & 0xFFFF) + start, .cnt = (end - start)/4 + 1,}
#define DEC_TOP_RANGE(start, end) {.offset = (DEC_TOP_REG_ADDR & 0xFFFF) + start, .cnt = (end - start)/4 + 1,}
#define FICU_REG(reg) {.offset = (FICU_REG_ADDR & 0xFFFF) + reg, .cnt = 1,}
#define FICU_RANGE(start, end) {.offset = (FICU_REG_ADDR & 0xFFFF) + start, .cnt = (end - start)/4 + 1,}

/*!
 * @brief NCL PMU register array
 */
norm_ps_data struct ncb_reg_range_t ncl_pmu_regs[] =
{
	/*! Nand PHY registers */
	NDPHY_RANGE(NPHY_CTRL_REG0, DTPHY_CTRL_REG5),
	NDPHY_RANGE(DTPHY_CTRL_REG13, DTPHY_CTRL_REG14),
	NDPHY_REG(CMDPHY_WP_CTRL_REG),
	NDPHY_REG(CMDPHY_CTRL_REG5),

	/*! NDCU registers */
	NDCU_REG(NF_CTRL_REG0),
	NDCU_REG(NF_IND_REG5),
	NDCU_RANGE(NF_SYNC_TIM_REG0, NF_SYNC_TIM_REG2),
	NDCU_RANGE(NF_GEN_TIM_REG0, NF_GEN_TIM_REG1),
	NDCU_RANGE(NF_RCMD_REG00, NF_DDR_DATA_DLY_CTRL_REG0),
	NDCU_RANGE(NF_FAIL_REG5, NF_FAIL_REG15),
#if HAVE_SAMSUNG_SUPPORT
	NDCU_RANGE(NF_PSTCMD_REG00, NF_PSTCMD_REG01),
#endif
	NDCU_REG(NF_RDY_ERR_LOC_REG0),
	NDCU_RANGE(NF_SUSP_REG0, NF_SUSP_REG1),
	NDCU_RANGE(NF_MP_ENH_CTRL_REG0, NF_PCNT_ENH_REG1),
	NDCU_RANGE(NF_FAC_CTRL_REG0, NF_FAC_CTRL_REG1),

	/*! ECCU registers */
	ECCU_RANGE(ECCU_CTRL_REG0, ECCU_CTRL_REG5),
	ECCU_REG(ECC_CLK_GATE_CTRL_REG),
	ECCU_REG(FDMA_CTRL_REG0),
	ECCU_REG(ECCU_PARTIAL_DU_DETECT_REG0),
	ECCU_REG(ECCU_PARTIAL_DU_DETECT_REG1),

	/*! Decoder top registers */
	DEC_TOP_RANGE(DEC_TOP_CTRS_REG0, DEC_TOP_CTRS_REG3),

	// Mdec & Pdec seems not neccessary
	/*! FICU registers */
	FICU_RANGE(FICU_CTRL_REG0, FICU_INT_EN_REG0),
	FICU_RANGE(FICU_LUT_ST_ADDR_REG, FICU_LUT_END_ADDR_REG),
	FICU_RANGE(FICU_DU_LOGS_FIFO_ST_ADDR_REG, FICU_ARD_CONF_REG7),
	FICU_REG(FICU_FSPM_INIT_REG),
	FICU_RANGE(FICU_FCMD_CQ_ST_ADDR_REG, FICU_FCMD_CQ_END_ADDR_REG),
	FICU_REG(FICU_CH_MAPPING_REG),
	FICU_RANGE(FICU_MP_ROW_OFFSET, FICU_META_IDX_MAP_HMB_BASE_ADDR_REG),
	FICU_RANGE(FICU_PDA_CONF_REG0, FICU_PDA_CONF_REG2),
	FICU_REG(FICU_DUMMY_DU_DTAG_REG),
	FICU_RANGE(FICU_FSPM_NORM_FINST_QUE0_ST_ADDR_REG, FICU_FSPM_NORM_FINST_QUE0_END_ADDR_REG),
	FICU_RANGE(FICU_FSPM_HIGH_FINST_QUE0_ST_ADDR_REG, FICU_FSPM_HIGH_FINST_QUE0_END_ADDR_REG),
	FICU_RANGE(FICU_FSPM_URGENT_FINST_QUE0_ST_ADDR_REG, FICU_FSPM_URGENT_FINST_QUE0_END_ADDR_REG),
	FICU_RANGE(FICU_FSPM_NORM_FINST_QUE1_ST_ADDR_REG, FICU_FSPM_NORM_FINST_QUE1_END_ADDR_REG),
	FICU_RANGE(FICU_FSPM_HIGH_FINST_QUE1_ST_ADDR_REG, FICU_FSPM_HIGH_FINST_QUE1_END_ADDR_REG),
	FICU_RANGE(FICU_FSPM_URGENT_FINST_QUE1_ST_ADDR_REG, FICU_FSPM_URGENT_FINST_QUE1_END_ADDR_REG),
	FICU_REG(FICU_INT_EN_REG1),
	FICU_REG(FICU_GRP0_FCMD_COMPL_QUE_BASE_ADDR),
	FICU_REG(FICU_GRP0_FCMD_COMPL_QUE_WRPTR_BASE_ADDR),
	FICU_REG(FICU_GRP1_FCMD_COMPL_QUE_BASE_ADDR),
	FICU_REG(FICU_GRP1_FCMD_COMPL_QUE_WRPTR_BASE_ADDR),
};

ps_data struct finstr_ard_format* pmu_ard_tmpl = NULL;
#if FSPM_VSC_TEMPLATE_COUNT
ps_data struct ncb_vsc_format* pmu_vsc_tmpl = NULL;
#endif
ps_data u32* pmu_ncb_reg_buf = NULL;

typedef struct _ndphy_info_t {
	u8 drv_str;// Drive strength
	u8 dll_phase[MAX_TARGET][MAX_LUN];
}ndphy_info_t;

ps_data ndphy_info_t* pmu_ndphy_info = NULL;

#if defined(RDISK)
fast_code u16 ncl_pmu_page_access(pda_t pda, dtag_t dtag, bool write)
{
	struct ncl_cmd_t ncl_cmd;
	pda_t pda_list[DU_CNT_PER_PAGE];
	struct info_param_t info[DU_CNT_PER_PAGE];
	bm_pl_t bm_pl[DU_CNT_PER_PAGE];
	u32 i;
	struct du_meta_fmt *meta;

	ncl_cmd.addr_param.common_param.info_list = info;
	ncl_cmd.addr_param.common_param.list_len = write ? 1 : DU_CNT_PER_PAGE;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;

	ncl_cmd.caller_priv = NULL;
	ncl_cmd.completion = NULL;
	ncl_cmd.du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd.user_tag_list = bm_pl;
	memset(info, 0, sizeof(info));
	meta = (void *) (ficu_readl(FICU_META_DTAG_MAP_SRAM_BASE_ADDR_REG) << 4);
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		info[i].pb_type = NAL_PB_TYPE_SLC;
		pda_list[i] = pda + i;
		bm_pl[i].pl.dtag = dtag.dtag + i;
		bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG;
#ifndef LJ_Meta
		meta[dtag.dtag + i].seed_index = pda + i;
#endif
		meta[dtag.dtag + i].meta[0] = 0;
		meta[dtag.dtag + i].meta[1] = 0;
		meta[dtag.dtag + i].hlba = 0;
	}

	if (write) {
		ncl_cmd.op_code = NCL_CMD_OP_WRITE;
		ncl_cmd.op_type = NCL_CMD_PROGRAM_TABLE;
	} else {
		ncl_cmd.op_code = NCL_CMD_OP_READ;
		ncl_cmd.op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
        #if NCL_FW_RETRY
		ncl_cmd.retry_step = 0;
        #endif
	}
	ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd.status = 0;
	ncl_cmd_submit(&ncl_cmd);
	return ncl_cmd.status;
}
#endif

/*!
 * @brief Backup the NCBregister
 *
 * @return Not used
 */
norm_ps_code bool pmu_ncb_reg_backup(void)
{
	u32 i, j;
	u32 idx = 0;
	u32 cnt = 0;

	for (i = 0; i < sizeof(ncl_pmu_regs)/sizeof(ncl_pmu_regs[0]); i++) {
		cnt += ncl_pmu_regs[i].cnt;
		//ncl_pmu_trace(LOG_ERR, 0, "range 0x%x %d\n", ncl_pmu_regs[i].offset, ncl_pmu_regs[i].cnt);
	}
	//ncl_pmu_trace(LOG_ERR, 0, "total cnt %d\n", cnt);
	sys_assert (pmu_ncb_reg_buf == NULL);
	pmu_ncb_reg_buf = sys_malloc(PS_DATA, sizeof(u32) * cnt);
	if (pmu_ncb_reg_buf == NULL) {
		return false;
	}

	for (i = 0; i < sizeof(ncl_pmu_regs)/sizeof(ncl_pmu_regs[0]); i++) {
		for (j = 0; j < ncl_pmu_regs[i].cnt; j++) {
			//ncl_pmu_trace(LOG_ERR, 0, "backup 0x%x\n", ncl_pmu_regs[i].offset + j * sizeof(u32));
			pmu_ncb_reg_buf[idx++] = eccu_readl(ncl_pmu_regs[i].offset + j * sizeof(u32));
		}
	}
	return true;
}

/*!
 * @brief Restore the NCBregister
 *
 * @return Not used
 */
norm_ps_code void pmu_ncb_reg_restore(void)
{
	u32 i, j;
	u32 ch;
	u32 idx = 0;

	sys_assert (pmu_ncb_reg_buf != NULL);
	for (i = 0; i < sizeof(ncl_pmu_regs)/sizeof(ncl_pmu_regs[0]); i++) {
		for (j = 0; j < ncl_pmu_regs[i].cnt; j++) {
			if ((ncl_pmu_regs[i].offset & 0xF000) == 0x3000) {// Ndphy registers
				for (ch = 0; ch < nand_info.geo.nr_channels; ch++) {
					eccu_writel(pmu_ncb_reg_buf[idx], ncl_pmu_regs[i].offset + j * sizeof(u32) + (ch << 8));
				}
				idx++;
			} else {// NDCU/ECCU/FICU registers
				eccu_writel(pmu_ncb_reg_buf[idx++], ncl_pmu_regs[i].offset + j * sizeof(u32));
			}
		}
	}
	sys_free(PS_DATA, pmu_ncb_reg_buf);
	pmu_ncb_reg_buf = NULL;
}

/*!
 * @brief NCL pmu suspend handling
 *
 * @param[in]	mode	sleep mode type
 *
 * @return value(true or false)
 */
ddr_code bool ncl_pmu_suspend(enum sleep_mode_t mode)
{

	if (!ncl_cmd_empty(false)) {
		ncl_pmu_trace(LOG_ERR, 0xea29, "ncl command not empty");
		return false;
	}

	ndcu_write_protection(true);

	// Backup whole ARD template buffer
	sys_assert(pmu_ard_tmpl == NULL);
	pmu_ard_tmpl = sys_malloc(PS_DATA, sizeof(struct finstr_ard_format)*FSPM_ARD_COUNT);
	if (!pmu_ard_tmpl) {
		ncl_pmu_trace(LOG_ERR, 0x2b6a, "Lack PMU buf");
		return false;
	}
	memcpy(pmu_ard_tmpl, fspm_usage_ptr->ard_template, sizeof(struct finstr_ard_format)*FSPM_ARD_COUNT);
#if FSPM_VSC_TEMPLATE_COUNT
	sys_assert(pmu_vsc_tmpl == NULL);
	pmu_vsc_tmpl = sys_malloc(PS_DATA, sizeof(struct ncb_vsc_format)*FSPM_VSC_TEMPLATE_COUNT);
	if (!pmu_vsc_tmpl) {
		ncl_pmu_trace(LOG_ERR, 0x0a4d, "Lack PMU buf");
		sys_free(PS_DATA, pmu_ard_tmpl);
		pmu_ard_tmpl = NULL;
		return false;
	}
	memcpy(pmu_vsc_tmpl, fspm_usage_ptr->vsc_fmt, sizeof(struct ncb_vsc_format)*FSPM_VSC_TEMPLATE_COUNT);
#endif

	// Disable FICU mode before NCB reg backup, and enable FICU mode after NCB reg restore
	ficu_mode_disable();

	/* NCB registers backup */
	if (!pmu_ncb_reg_backup()) {
		sys_free(PS_DATA, pmu_ard_tmpl);
		sys_free(PS_DATA, pmu_vsc_tmpl);
		pmu_ard_tmpl = NULL;
		pmu_vsc_tmpl = NULL;
		ficu_mode_enable();
		ncl_pmu_trace(LOG_ERR, 0x8821, "Lack PMU buf");
		return false;
	}

	/* Backup drive strength and dll_phase of each channel */
	pmu_ndphy_info = sys_malloc(PS_DATA, sizeof(ndphy_info_t) * max_channel);
	if (!pmu_ndphy_info) {
		sys_free(PS_DATA, pmu_ard_tmpl);
		pmu_ard_tmpl = NULL;
		sys_free(PS_DATA, pmu_vsc_tmpl);
		pmu_vsc_tmpl = NULL;
		sys_free(PS_DATA, pmu_ncb_reg_buf);
		pmu_ncb_reg_buf = NULL;
		ficu_mode_enable();
		ncl_pmu_trace(LOG_ERR, 0xe9db, "Lack PMU buf");
		return false;
	}
	u32 ch, ce, lun;
	for (ch = 0; ch < max_channel; ch++) {
		dtphy_ctrl_reg14_t reg14;
		dtphy_dll_cfg_ptr_t ptr;
		dtphy_dll_phase_t phase;
		reg14.all = ndphy_readl(ch, DTPHY_CTRL_REG14);
		for (ce = 0; ce < max_target; ce++) {
			for (lun = 0; lun < max_lun; lun++) {
				ptr.all = ndphy_readl(ch, DTPHY_DLL_CFG_PTR);
				ptr.b.dtphy_dll_cfg_ptr = ce + lun * 8;
				ndphy_writel(ch, ptr.all, DTPHY_DLL_CFG_PTR);
				phase.all = ndphy_readl(ch, DTPHY_DLL_PHASE);
				pmu_ndphy_info[ch].dll_phase[ce][lun] = phase.b.dtphy_dll_phase;
			}
		}
		pmu_ndphy_info[ch].drv_str = (reg14.all >> DTPHY_QSP_DSTR0_SHIFT) & 0xF;
	}

	return true;
}

/*!
 * @brief NCL pmu resume handling
 *
 * @param[in]	mode	sleep mode type
 *
 * @return Not used
 */
norm_ps_code void ncl_pmu_resume(enum sleep_mode_t mode)
{
	/* Reset each NCB modules */
	ndphy_hw_reset();
	ndcu_hw_rst();
#if NAND_POWER_DOWN
	nand_reinit();
#endif

	eccu_hw_reset();
	ndphy_hw_init();

	/* Recover NCB registers */
	pmu_ncb_reg_restore();

	/* Restore drive strength and dll_phase of each channel */
	u32 ch, ce, lun;
	for (ch = 0; ch < max_channel; ch++) {
		ndphy_drv_str_level(ch, pmu_ndphy_info[ch].drv_str);
		for (ce = 0; ce < max_target; ce++) {
			for (lun = 0; lun < max_lun; lun++) {
				ndphy_set_dll_phase_enhance(ch, ce,lun,pmu_ndphy_info[ch].dll_phase[ce][lun]);
			}
		}
	}
	nf_dll_updt_ctrl_reg0_t reg0;
	reg0.all = ndcu_readl(NF_DLL_UPDT_CTRL_REG0);
	reg0.b.nf_dll_updt_en = 0x3;
	ndcu_writel(reg0.all, NF_DLL_UPDT_CTRL_REG0);
	nphy_ctrl_reg0_t reg1;
	for (ch = 0; ch < max_channel; ch++) {
		reg1.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		reg1.b.nphy_io_strb_en = 1;
		ndphy_writel(ch, reg1.all, NPHY_CTRL_REG0);
	}
	sys_free(PS_DATA, pmu_ndphy_info);
	pmu_ndphy_info = NULL;

	/* Restore ECCU CMF and Scrambler seed LUT */
	pmu_eccu_cmf_restore();

	/* Re-configure registers N-deep */
	ndcu_ndcmd_format_cfg();

	int idx;
	for (idx = DU_FMT_MR_4K; idx < DU_FMT_COUNT; idx++) {
		eccu_du_fmt_cfg(idx);
	}

#if NCL_HAVE_ERD
	ncl_erd_cfg_llr_lut();// ERD LLR LUT table
#endif
	ficu_xfcnt_reg_cfg();

	/* Init FSPM */
	ficu_fspm_init();

	/* Recover ARD templates */
	sys_assert(pmu_ard_tmpl != NULL);
	memcpy(fspm_usage_ptr->ard_template, pmu_ard_tmpl, sizeof(struct finstr_ard_format)*FSPM_ARD_COUNT);
	sys_free(PS_DATA, pmu_ard_tmpl);
	pmu_ard_tmpl = NULL;
#if FSPM_VSC_TEMPLATE_COUNT
	/* Recover VSC templates */
	sys_assert(pmu_vsc_tmpl != NULL);
	memcpy(fspm_usage_ptr->vsc_fmt, pmu_vsc_tmpl, sizeof(struct ncb_vsc_format)*FSPM_VSC_TEMPLATE_COUNT);
	sys_free(PS_DATA, pmu_vsc_tmpl);
	pmu_vsc_tmpl = NULL;
#endif

	// Recover SW data structures

	// Recover NF clock
	ndcu_write_protection(false);
	ficu_mode_enable();
	poll_mask(VID_NCB0_INT);
	poll_mask(VID_GRP0_FCMD_CPL);

#if !FPGA
	extern u32 nf_clk;
	nf_clk_init(nf_clk);
	ndphy_locking_mode();
#endif
	//extern void bcl_mgr_resume(enum sleep_mode_t mode);
	//bcl_mgr_resume(mode);

#if NAND_POWER_DOWN
	nand_dcc_training();
#endif

#ifdef REFR_RD
#define PWR_MAX_RFRD_DIE (32)
    if(rfrd_times_struct.rfrd_all_die == 0)
    {
        rfrd_times_struct.rfrd_all_die = nand_target_num() * nand_channel_num() * nand_lun_num() ;
        if(rfrd_times_struct.rfrd_all_die > PWR_MAX_RFRD_DIE)
        {
            rfrd_times_struct.power_on_rfrd_die = PWR_MAX_RFRD_DIE;
        }
        else
        {
            rfrd_times_struct.power_on_rfrd_die = rfrd_times_struct.rfrd_all_die;
        }
         rfrd_times_struct.power_on_rfrd_times = (rfrd_times_struct.rfrd_all_die / rfrd_times_struct.power_on_rfrd_die);
    }
	extern void read_refresh_poweron(u32 rfrd_time);
	read_refresh_poweron( rfrd_times_struct.power_on_rfrd_times);
#endif
}

/*!
 * @brief Initialization PMU module
 *
 * @return Not used
 */
init_code void ncl_pmu_init(void)
{
	pmu_register_handler(SUSPEND_COOKIE_NCB, ncl_pmu_suspend,
		RESUME_COOKIE_NCB, ncl_pmu_resume);

	return;
}
/*! @} */
