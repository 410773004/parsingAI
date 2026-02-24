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
//! \file pmu.c
//! @brief Power management unit
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "stdio.h"
#include "types.h"
#include "pmu.h"
#include "sect.h"
#include "crc32.h"
#include "misc.h"
#include "string.h"
#include "console.h"
#include "misc_register.h"
#include "rainier_soc.h"
#include "cpu_msg.h"
#include "assert.h"
#include "dma.h"
#include "btn_export.h"

#define __FILEID__ pmu
#include "trace.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define PMU_TMOUT	20		/* in unit of 4us */
#define SIGNATURE	0x53554D50	/* PMUS */
#define UNDEFINED	0xFFFFFFFF
#define PMU_CYCLE_IN_SEC	2
#define PMU_MODE  SLEEP_MODE_PS4 // SLEEP_MODE_PS3_L12 // SLEEP_MODE_PS3_L1 //

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/* TODO: renew the mapping */
static fast_data u8 pmu_suspend_resume_mapping[SUSPEND_COOKIE_END] = {
		RESUME_COOKIE_END, 	// system suspend fail
		RESUME_COOKIE_END,	// cache suspend fail
		RESUME_COOKIE_CACHE,	// ftl suspend fail
		RESUME_COOKIE_FTL,	// ncb suspend fail
		RESUME_COOKIE_NCB,	// bm suspend fail
		RESUME_COOKIE_FTL,	// l2p suspend fail
		RESUME_COOKIE_NCB, 	// cmd_proc suspend fail, bm don't need to be inited
		RESUME_COOKIE_NCB,	// nvme suspend fail, cmd_proc/bm don't need to be inited
		RESUME_COOKIE_NVME,	// platform suspend fail
};

/*! misc register 0x94 cld_dis_ctrk setting */
enum {
	NAND_INTF_CLK_DIS = BIT(0),
	NAND_CTRL_CLK_DIS = BIT(1),
	PCIE_MAC_CLKC_DIS = BIT(2),
	CPU_N_SYS_CLK_DIS = BIT(3),
	MISC_ON_BLK_CLK_DIS = BIT(4),
	PHY_REG_BLK_CLK_DIS = BIT(5),
	FICU_CLK_DIS = BIT(6),
	MC_CLK_DIS = BIT(7),
	MC_REF_CLK_DIS = BIT(8),
	ATCLK_DIS = BIT(9),
	PCLKDBG_DIS = BIT(10),
	SRAM_CLK_DIS = BIT(11)
};

/*!
 * @brief save in fw_stts_3_t, cpu resume/sleep mode
 */
typedef union _other_cpu_resume_t {
	struct {
		u16 sleep_mode;
		u16 resume_mode;
	} b;
	u32 all;
} other_cpu_resume_t;

static fast_data_zi pmu_suspend_handle_t suspend_handlers[SUSPEND_COOKIE_END];
static fast_data_zi pmu_resume_handle_t resume_handlers[RESUME_COOKIE_END];

/*!
 * @brief pmu timer function to check hw status and then enter PMU
 *
 * @param data		not used
 *
 * @return		not used
 */
static void pmu_timer_func(void *data);
extern misc_register_regs_t *misc_reg;

//static fast_data_zi u32 pmu_cnt;
static fast_data_zi bool enable_pmu = false;				///< if pmu timer was enabled or not

/*!
 * @brief pmu timer to enter pmu
 */
static fast_data struct timer_list pmu_timer = {
	.entry = LIST_HEAD_INIT(pmu_timer.entry),
	.function = pmu_timer_func,
	.data = NULL,
};

static fast_data_zi struct timer_list l1_pmu_timer;	///< timer to enter L1

static fast_data u32 l1_delay = 5*HZ/10;
static fast_data u32 l12_delay = 1 * HZ;

#ifdef FPGA
static fast_data u32 r94 = 0x00101385; // 0x003000f5; // 0x03F0188D;
#else
/*
static fast_data u32 r94 = \
	EN_CORE_LDO_LP_MASK | \
	DIS_PHY_LDO_MASK | \
	SEL_OSC_CLK_MASK | \
	PD_PLL0_MASK | \
	PD_PLL1_MASK |
	((NAND_INTF_CLK_DIS | NAND_CTRL_CLK_DIS | PCIE_MAC_CLKC_DIS | CPU_N_SYS_CLK_DIS ) << CLK_DIS_CTRL_SHIFT);
*/
#endif
/*
static fast_data u32 r98 = \
	HALT_PHY_EXIT_L1_MASK | (0x00 << LDO_WAIT_TIME_SHIFT);
*/
/*
static fast_data u32 r80 = \
	PMU_EN_MASK | PMU_START_MASK | EN_PMU_TMOUT_TMR_MASK | DIS_PHY_EXIT_L1_MASK | \
	((0x80) << PMU_TMOUT_VAL_SHIFT);
*/
share_data_zi volatile bool pmu_ps3_mode;			///< shared pmu is ps3 mode
//static fast_data u32 R94 = 0xFFF0FFF5; //0x00100385; // 0x00100385;
//static fast_data u32 R98 = 0x2;
//static fast_data u32 R80 = 0x800f;
extern void pmu_enable(void);

/* TODO: remove two define when the timer move to nvme core */
extern int nvmet_cmd_pending(void);
extern int hal_nvmet_hw_sts(void);
extern int btn_cmd_idle(void);
extern bool __attribute__((weak)) ncl_cmd_empty(bool rw);

/*!
 * @brief alias function for ramdisk call ncl_cmd_empty
 *
 * @return	always true means ncl always empty
 */


// bool slow_code __ncl_cmd_empty(void)
// {
// #if defined(MPC)
// 	cpu_msg_sync_start();
// 	cpu_msg_issue(CPU_BE - 1, CPU_MSG_NCL_CMD_EMPTY, 0, (u32)&misc_reg->fw_stts_0);
// 	cpu_msg_sync_end();
// 	if (readl(&misc_reg->fw_stts_0)) {
// 		writel(0, &misc_reg->fw_stts_0);
// 		return true;
// 	}
//
// 	return false;
// #else
// 	return true;
// #endif
// }

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
bool ddr_code pmu_register_handler(
	enum pmu_suspend_cookie_t suspend_cookie, pmu_suspend_handle_t suspend,
	enum pmu_resume_cookie_t resume_cookie,pmu_resume_handle_t resume)
{
	if ((suspend_cookie > SUSPEND_COOKIE_END) ||
		(resume_cookie > RESUME_COOKIE_END))
		return false;
	/*lint --e{661} out-of-bounds checked */
	suspend_handlers[suspend_cookie] = suspend;
	resume_handlers[resume_cookie] = resume;

	return true;
}

static void ddr_code pmu_resume(enum pmu_resume_cookie_t since,
		enum pmu_resume_cookie_t to, enum sleep_mode_t mode)
{
	enum pmu_resume_cookie_t cookie = since;

	for ( ; cookie < to; cookie++) {
		if (resume_handlers[cookie] == NULL)
			continue;
		resume_handlers[cookie](mode);
	}
}

static enum pmu_suspend_cookie_t ddr_code pmu_suspend(enum pmu_suspend_cookie_t since,
		enum pmu_suspend_cookie_t to, enum sleep_mode_t mode)
{
	enum pmu_suspend_cookie_t cookie = since;

	for ( ; cookie < to; cookie++) {
		if (suspend_handlers[cookie] == NULL)
			continue;
		if (suspend_handlers[cookie](mode) != true) {
			pmu_resume((enum pmu_resume_cookie_t) pmu_suspend_resume_mapping[cookie],
					to, mode);
			return cookie;
		}
	}
	return to;
}

enum pmu_suspend_cookie_t ddr_code pmu_suspend_all(enum sleep_mode_t mode)
{
	return pmu_suspend(SUSPEND_COOKIE_START, SUSPEND_COOKIE_END, mode);
}

init_code u32 pmu_hw_sts(enum sleep_mode_t mode)
{
	u32 in_l12 = (mode != SLEEP_MODE_PS3_L1);

	if (in_l12) {
		/* 0xC00400A4 32'hA4 bit27, bit19 */
		mac_pd_ctrl_t ctlr = {
			.all = readl(&misc_reg->mac_pd_ctrl.all),
		};

		if (ctlr.b.phy_in_l1_2 != true)
			return BIT(0);

		if (ctlr.b.clk_req_n != true)
			return BIT(1);
	} else {
		if (!pcie_phy_in_l1())
			return BIT(0);
	}

#if (CPU_ID == 1)
	if (nvmet_cmd_pending() != 0)
		return BIT(2);

	if (hal_nvmet_hw_sts() != 0)
		return BIT(3);
#endif

if (ncl_cmd_empty && ncl_cmd_empty(false) != true)
		return BIT(4);

#if defined(RDISK)
	//if (0 && ftl_is_busy() != 0) //TODO
		//return BIT(5);

#if (CPU_ID == 2)
	extern bool fwdl_is_runing_fw_upgrade(void);
	if (fwdl_is_runing_fw_upgrade())
		return BIT(6);
#endif
#endif

#if (CPU_ID == BTN_CORE_CPU)
	if (btn_cmd_idle() != true)
		return BIT(7);
#endif
	return 0;
}
/*
static init_code u32 pmu_keep_l1(enum sleep_mode_t mode)
{
	u32 loop = 0;
	u32 evt;
	mac_pd_ctrl_t ctlr;

	// block exit from L1
	ctlr.all = readl(&misc_reg->mac_pd_ctrl.all);
	if (ctlr.b.phy_in_l1_2 != true)
		return BIT(0);

	if (ctlr.b.clk_req_n != true)
		return BIT(1);

	writel(DIS_PHY_EXIT_L1_MASK, &misc_reg->pmu_control.all);
	do {
#if 0 // injection delay for test ABORT
		mdelay(1);
#endif
		evt = pmu_hw_sts(mode);
		if (evt) {
			writel(0, &misc_reg->pmu_control.all);
			return evt;
		}
	} while (++loop < 20);
	return 0;
}
*/
/*
static init_code int pmu_keep_l1_ps3(enum sleep_mode_t mode)
{
	// Make sure Host still sends Electrical Idle (RC004_303Ch[2] = 1)
	if (!pcie_link_idle())
		return BIT10;

	// R80h = 0x8 (reset PMU, disable PHY exit)
	writel(DIS_PHY_EXIT_L1_MASK, &misc_reg->pmu_control);

	// Continue to read RC004_303Ch to make sure bit 1 is still 1 (loop 50 times)
	u32 cnt = 50;
	while (cnt--) {
		if (!pcie_link_idle()) {
			writel(0, &misc_reg->pmu_control.all);
			return BIT11;
		}
	}
	u32 sts = pmu_hw_sts(mode);
	if (sts) {
		writel(0, &misc_reg->pmu_control.all);
		return sts;
	}

	return 0;
}

static ddr_code void pmu_stby_ncb_off(void)
{

"	Power-off NCB
-	Set bit 1 of R0Ch to 1
-	R90h = 0x2
-	R8Ch = 0x202

	writel(readl((void*)0xc004000c)|BIT1, (void*)0xc004000c);
	writel(0x2, (void*)0xc0040090);
	writel(0x202, (void*)0xc004008c);
}
*/
/*
static ddr_code void pmu_stby_ncb_on(void)
{

-	R8Ch = 0x0
-	Wait for 10us, set R90h = 0x0
-	Set bit 1 of R0Ch to 0

	writel(0x0, (void*)0xc004008c);

	u32 i = 10000;
	while (i--)
		__asm__("nop");

	writel(0x0, (void*)0xc0040090);

	writel(readl((void*)0xc004000c) & ~BIT1, (void*)0xc004000c);
}
*/
/*
static ps_code void pmu_post(void)
{
	pmu_control_t ctlr;

	ctlr.all = 0;
	writel(ctlr.all, &misc_reg->pmu_control.all);

	ctlr.b.pmu_en = 1;
	writel(ctlr.all, &misc_reg->pmu_control.all);

	pmu_cnt++;
}
*/

/*lint -esym(528, pmu_mode_str)*/
__attribute__((unused)) static ddr_code const char *pmu_mode_str(enum sleep_mode_t pmu_mode)
{
	switch (pmu_mode) {
	case SLEEP_MODE:
		return "Sleep";
	case SLEEP_MODE_2:
		return "Sleep 2";
	case SLEEP_MODE_3:
		return "Sleep 3";
	case SLEEP_MODE_4:
		return "Sleep 4";
	case SLEEP_MODE_PS4:
		return "PS4 in L1.2";
	case SLEEP_MODE_PS3_L12:
		return "PS3 in L1.2";
	case SLEEP_MODE_PS3_L1:
		return "PS3 in L1";
	default:
		break;
	}

	return "unknown";
}
/*
static ps_code void pmu_mode(enum sleep_mode_t sleep_mode)
{
	hw_lp_mode_t mode = {.all = 0};
	hw_lp_mode1_t mode1 = {.all = 0};

	mode.b.en_core_ldo_lp = 1;   // Enable core LDO low power mode //
	mode.b.dis_phy_ldo = 1;      // Enable PHY LDO low power mode //
	mode.b.sel_osc_clk = 1;      // Select OSC clock //
	mode.b.pd_pll0 = 1;          // Power Down PLL0 //
	mode.b.pd_pll1 = 1;          // Power Down PLL1 //
	mode.b.clk_dis_ctrl = 0x3F;  // Disable all clocks //

	mode1.b.mac_exit_mode = 0;    // Wake up MAC after PMU exit is done //
	mode1.b.ldo_wait_time = 0x80;  // LDO wait time in 512us //

	//
	 //  Always on: A/BTCM, SRAM 0, top PMU, PCIe Mac PMU
	 //  Domain 0: pd_top(NVMEe, BM, NCB, CPU)
	 //  Domain 1: SRAM 1
	 //  Domain 2: SRAM 2-5
	 //  Domain 3: RAID buffer
	 //  Domain 4: PCIe PHY
	 //  Domain 5: PCIEe MAC
	 //  Domain 6: Not used by top PMU
	 //
	switch (sleep_mode) {
	case SLEEP_MODE:
		mode.b.pd_off_ctrl = 0x3F; // Domain 0/1/2/3/4/5 off //
		break;
	case SLEEP_MODE_2:
		mode.b.pd_off_ctrl = 0x1F; // Domain 0/1/2/3/4 off, 5 on //
		break;
	case SLEEP_MODE_3:
		mode.b.dis_phy_ldo = 0;
		mode.b.en_phy_ldo_lp = 1;;
		mode.b.pd_off_ctrl = 0x2F; // Domain 0/1/2/3/5 off, 4 on //
		break;
	case SLEEP_MODE_4:
		mode.b.dis_phy_ldo = 0;
		mode.b.en_phy_ldo_lp = 1;
		mode.b.pd_off_ctrl = 0xF; // Domain 0/1/2/3 off, 4/5 on //
		break;
	case SLEEP_MODE_PS4:
		#if 0
		mode.b.en_core_ldo_lp = 0;
		mode.b.dis_phy_ldo = 0;
		mode.b.pd_off_ctrl = 0x1;  // Domain 1/2/3/4/5 on //
		mode.b.en_phy_ldo_lp = 0;
		mode1.b.ldo_wait_time = 0;  // LDO wait time in 1us unit //
		#else
		r94 = R94;
		r98 = R98;
		r80 = R80;
		#endif
		break;
	case SLEEP_MODE_PS3_L12:
#ifdef FPGA
		r94 = 0x03B00060;
#else
		r94 = 0x03B00070;
#endif
		r98 = 0x2;
		r80 = 0x1000B;
		break;
	case SLEEP_MODE_PS3_L1:
#ifdef FPGA
		r94 = 0x0FB00060;
#else
		r94 = 0x0FB00070;
#endif
		r98 = 0x2;
		r80 = 0x4000B;
		break;
	default:
		break;
	}

#if 0
	rtos_core_trace(LOG_ALW, 0, "%s:94(0x%x), 98(0x%x), 80(0x%x))",
			pmu_mode_str(pmu_mode), mode.all, mode1.all, r80);
#endif

	if ((r94 != UNDEFINED) && (r98 != UNDEFINED)) {
		mode.all = r94;
		mode1.all = r98;
		//rtos_core_trace(LOG_ALW, 0, "(M) -> 32'h94(0x%x), 32'h98(0x%x)", mode.all, mode1.all);
	}

	pmu_clk_ctrl_t pclk;
	pclk.all = readl(&misc_reg->pmu_clk_ctrl);
	pclk.b.pmu_sw_clk_src = 1;
	writel(pclk.all, &misc_reg->pmu_clk_ctrl);

	writel(mode.all, &misc_reg->hw_lp_mode.all);
	writel(mode1.all, &misc_reg->hw_lp_mode1.all);

	//r80 |= EN_WAKE_BY_UART_MASK | EN_WAKE_BY_PMU_TIMER_MASK;
#if defined(M2_2A)
	r80 |= BIT23; // M2_2A use GPIO 14 to control PMIC
#endif
	writel(r80, &misc_reg->fw_stts_6.all);
}
*/
/*
ddr_code static void cpus_unhalt(void)
{
	int i;

	for (i = 1; i < MPC; i++)
		cpu_halt(i, false);
}
*/
/*!
 * @brief suspend remote CPU
 *
 * @param mode		sleep mode
 *
 * @return		return 0 for all remote CPU suspended, or fail CPU ID
 */
/*
ddr_code static u32 cpu_suspend(enum sleep_mode_t mode)
{
	int i;

	for (i = MPC - 1; i > 0; i--) {
		int ret;

		writel(0, &misc_reg->fw_stts_0);
		cpu_msg_issue(i, CPU_MSG_PMU_SYNC_SUSPEND, 0,  (u32) mode);
		do {
			ret = readl(&misc_reg->fw_stts_0);
		} while (ret == 0);

		if (ret < 0) {	// fail
			int j;

			for (j = i + 1; j < MPC; j++)
				cpu_halt(j, false);

			return i;
		}
		cpu_halt(i, true);
	}

	return 0;
}
*/
#if defined(MPC)
ddr_code void ipc_pmu_suspend(volatile cpu_msg_req_t *req)
{
	enum pmu_suspend_cookie_t sus;
	enum sleep_mode_t mode = (enum sleep_mode_t)req->pl;

	sus = pmu_suspend_all(mode);
	if (sus != SUSPEND_COOKIE_END) {
		rtos_core_trace(LOG_ALW, 0x5e82, "PMU suspend failed %d.", sus);
		writel((u32)-1, &misc_reg->fw_stts_0);
		return;
	}

	writel(1, &misc_reg->fw_stts_0);
	//ipc_helper_close();
}

ddr_code void cpu_resume(enum sleep_mode_t mode)
{
	int i;
	other_cpu_resume_t m = { .all = readl(&misc_reg->fw_stts_3) };

	m.b.resume_mode = (u16) mode;

	for (i = 2; i < MPC; i++) {
		writel(m.all, &misc_reg->fw_stts_3);
		writel(SIGNATURE, &misc_reg->fw_stts_0);

		cpu_reset(i);
		cpu_halt(i, false);

		while (readl(&misc_reg->fw_stts_0) == SIGNATURE);
	}
}

ddr_code void cpu2_resume(enum sleep_mode_t mode)
{
	other_cpu_resume_t m = { .all = readl(&misc_reg->fw_stts_3) };

	m.b.resume_mode = (u16) mode;

	writel(m.all, &misc_reg->fw_stts_3);
	writel(SIGNATURE, &misc_reg->fw_stts_0);

	cpu_reset(1);
	cpu_halt(1, false);

	while (readl(&misc_reg->fw_stts_0) == SIGNATURE);
}

#endif

init_code void pmu_wakeup_timer_set(int sec)
{
	pmu_timer_ctrl_t pmu_timer_ctrl = { .all = readl(&misc_reg->pmu_timer_ctrl), };
	pmu_timer_ctrl.b.pmu_timer_en = 1;
	writel(pmu_timer_ctrl.all, &misc_reg->pmu_timer_ctrl);

	pmu_wake_time_t pmu_wake_time = { .b.pmu_wake_time = 125000 * sec, };
	writel(pmu_wake_time.all, &misc_reg->pmu_wake_time);
}

init_code void pmu_wakeup_timer_halt(void)
{
	pmu_timer_ctrl_t pmu_timer_ctrl = { .all = readl(&misc_reg->pmu_timer_ctrl), };
	pmu_timer_ctrl.b.pmu_timer_run = 0;
	writel(pmu_timer_ctrl.all, &misc_reg->pmu_timer_ctrl);
}
/*
init_code bool pmu_enter(enum sleep_mode_t mode)  //fast_code
{
	extern void poweron_ddr();
	extern void poweroff_ddr();
	extern void poweron_nand();
	extern void poweroff_nand();
	enum sleep_mode_t resume_mode = mode;
	enum pmu_suspend_cookie_t sus = 0, to_sus = 0;
	u32 flags = irq_save();

	writel(DIS_PHY_EXIT_L1_MASK, &misc_reg->pmu_control.all);

	// stage 1: saving state and registers
	//pmu_mode(mode);  Change R94h/R98h/R80

	// clear PERST to avoid HW reset MAC
	writel(PERST_ASSERTED_MASK, &misc_reg->unm_sys_int);

	if (mode <= SLEEP_MODE_PS4) {
		to_sus = SUSPEND_COOKIE_END;
		sus = pmu_suspend_all(mode);
	}
	else if (mode == SLEEP_MODE_PS3_L1 || mode == SLEEP_MODE_PS3_L12) { //PS3, only NCB is off
		to_sus = SUSPEND_COOKIE_NCB + 1;
		sus = pmu_suspend(SUSPEND_COOKIE_NCB, to_sus, mode);
	}
	else {
		panic("invalid mode");
	}
	if (sus != to_sus) {
		rtos_core_trace(LOG_ALW, 0, "PMU suspend failed %d.", sus);
		irq_restore(flags);
		return false;
	}

#if defined(MPC)
	u32 ret = cpu_suspend(mode);
	if (ret != 0) {
		rtos_core_trace(LOG_ALW, 0, "CPU %d suspend fail", ret);
		pmu_resume(RESUME_COOKIE_START, RESUME_COOKIE_END, SUSPEND_ABORT);
		irq_restore(flags);
		return false;
	}
#endif

#if 0 // injection delay for test ABORT
	mdelay(500);
#endif

	// stage 2: double check the state, make sure system is idle
	//     XXX: if system is not idle, we have toc call suspend abort
	//
	u32 evt = pmu_hw_sts(mode);
	if (evt) {
abort:
		if (mode <= SLEEP_MODE_PS4) {
			pmu_resume(RESUME_COOKIE_START, RESUME_COOKIE_END, SUSPEND_ABORT);
		}
		else { // PS3
			pmu_resume(RESUME_COOKIE_PLATFORM, RESUME_COOKIE_PLATFORM + 1, SUSPEND_ABORT); // init cpu private regs
			pmu_resume(RESUME_COOKIE_NCB, RESUME_COOKIE_NCB + 1, SUSPEND_ABORT);
		}
		writel(0, &misc_reg->pmu_control.all);
#if defined(MPC)
		cpus_unhalt();
#endif
		rtos_core_trace(LOG_ALW, 0, "PMU suspend Abort(%x).", evt);
		irq_restore(flags);
		return false;
	}

	if (mode == SLEEP_MODE_PS3_L1)
		evt = pmu_keep_l1_ps3(mode);
	else
		evt = pmu_keep_l1(mode);

	if (evt)
		goto abort;

	// FW stores current status, use RC0h ~ RDCh
	writel(SIGNATURE, &misc_reg->fw_stts_0.all);

	if (mode == SLEEP_MODE_PS3_L1 || mode == SLEEP_MODE_PS3_L12) {
		pmu_stby_ncb_off();
	}

	// store current tsc
	tsc64_base = get_tsc_64();

	// mark sleep mode
	other_cpu_resume_t m = { .all = 0 };
	m.b.sleep_mode = (u16) mode;
	writel(m.all, &misc_reg->fw_stts_3);

	pmu_status_t sts;
#define PMU_SUCCESSFUL	(PMU_DONE_MASK | PD_OCCURED_MASK)

	if (mode == SLEEP_MODE_PS4) {
		poweroff_ddr();
		poweroff_nand();
	}

	// stage 3: call the PMU to suspend ASAP
	pmu_enable(); goto pmu handler in hiber.S

	// XXX: Mr. Anderson, welcome back, we missed you
	sts.all = readl(&misc_reg->pmu_status.all);
	writel(sts.all, &misc_reg->pmu_status.all);
	if (mode == SLEEP_MODE_PS4) {
		poweron_ddr();
		poweron_nand();
	}

	// [0] done
	// [1] power_off
	// [2] mac time out
	// [3] not in L1.2
#if defined(MPC)
	cpu2_resume(mode);
#endif
	if ((sts.all & 0xf) == PMU_SUCCESSFUL) {
		// for test, reset BM, NVME, NCB
		writel(1 << RESET_BM, (void*)0xc004000c);
		writel(0x0, (void*)0xc004000c);
		pmu_resume(RESUME_COOKIE_START, RESUME_COOKIE_NCB, mode);
	}
	else if ((sts.all & 0xf) == PMU_DONE_MASK) {
		sys_assert(mode >= SLEEP_MODE_PS3_L12);
		pmu_resume(RESUME_COOKIE_PLATFORM, RESUME_COOKIE_PLATFORM + 1, mode);
	}
	else {
		rtos_mmgr_trace(LOG_ERR, 0, "sts %x\n", sts.all);
		panic("pmu return err");
		pmu_resume(RESUME_COOKIE_START, RESUME_COOKIE_NCB, SUSPEND_ABORT);
		mode = SUSPEND_ABORT;
	}

#if defined(PMU_DB_DEBUG) && (CPU_ID == 1)
	extern void nvme_db_pmu_debug();
	nvme_db_pmu_debug();
#endif
	//pmu_post();

	//rtos_core_trace(LOG_ALW, 0, "PMU enter, cnt(%d)", pmu_cnt);

#if defined(MPC)
	cpu_resume(mode);
#endif

	if (mode <= SLEEP_MODE_PS4) {
		pmu_resume(RESUME_COOKIE_NCB, RESUME_COOKIE_END, mode);
	}
	else { // PS3
		pmu_stby_ncb_on();
		pmu_resume(RESUME_COOKIE_NCB, RESUME_COOKIE_NCB + 1, resume_mode);
	}

	extern void pcie_isr_clear();
	pcie_isr_clear();
	irq_restore(flags);

	pmu_wakeup_timer_halt();

	rtos_core_trace(LOG_ALW, 0, "wakeup by: %s", (int)(sts.b.waked_by_pcie ? "PCIE" : \
			(sts.b.waked_by_pmu_timer ? "TIMER" : \
			(sts.b.waked_by_pwr_loss ? "PWR LOSS" : \
			(sts.b.waked_by_i2c ? "I2C" : \
			(sts.b.waked_by_uart ? "UART" : "UNKNOWN"))))));

	rtos_core_trace(LOG_ALW, 0, "PMU h94(0x%x) h98(0x%x)\nresume: sts 0x%x, cnt(%d)",
			readl(&misc_reg->hw_lp_mode.all), readl(&misc_reg->hw_lp_mode1.all), sts.all, pmu_cnt);
	// Clear fw sts
	writel(0, &misc_reg->fw_stts_0.all);
	writel(0, &misc_reg->fw_stts_3.all);
	writel(0, &misc_reg->fw_stts_6.all);
	writel(0, &misc_reg->fw_stts_7.all);

	return true;
}
*/
init_code void pmu_resume_on_other_core(void)
{
	other_cpu_resume_t m = { .all = readl(&misc_reg->fw_stts_3) };

	// platfrom must be resumed
	pmu_resume(RESUME_COOKIE_START, RESUME_COOKIE_BM, (enum sleep_mode_t) m.b.sleep_mode);

	pmu_resume(RESUME_COOKIE_BM, RESUME_COOKIE_END, (enum sleep_mode_t) m.b.resume_mode);
	rtos_mmgr_trace(LOG_ERR, 0xb7c7, "CPU %d resumed\n", CPU_ID);
	writel(0, &misc_reg->fw_stts_0);
}

init_code void l1_switch(bool enable)
{
	if (!enable) {
		// keep L0
		pcie_set_xfer_busy(true);
		del_timer(&l1_pmu_timer);
	} else {
		// try to enter L1
		mod_timer(&l1_pmu_timer, jiffies + l1_delay);
	}
}

/*!
 * @brief timer handler to allow to enter L1
 *
 * @param data		not used
 *
 * @return		not used
 */
static init_code void l1_pmu_timer_handler(void *data)
{
	pcie_set_xfer_busy(false);
}

init_code void pmu_init(void)
{
	INIT_LIST_HEAD(&l1_pmu_timer.entry);
	l1_pmu_timer.function = l1_pmu_timer_handler;
	l1_pmu_timer.data = NULL;

	/* 0x009c */
	lp_wait_time_t lp_wait_time = {
			.all = readl(&misc_reg->lp_wait_time.all),
	};
	//writel((lp_wait_time.all & ~0xffff) | 0xff12, &misc_reg->lp_wait_time.all);
	lp_wait_time.all = 0x5001ff12;
	writel(lp_wait_time.all, &misc_reg->lp_wait_time.all);

	/* 0x00a0 */
	usec_count_t usec_count = {
			.all = readl(&misc_reg->usec_count.all),
	};
	usec_count.b.us_cnt_sys = 0x140;
	usec_count.b.us_cnt_osc = 0x1;
	usec_count.all = 0xa0004a0;
	writel(usec_count.all, &misc_reg->usec_count.all);

	/* 0x0130*/
	writel(0xc020, &misc_reg->gpio_pad_ctrl.all);
	//mod_timer(&pmu_timer, jiffies + 15 * HZ);
}

init_code void sys_sleep(enum sleep_mode_t mode)
{
	//u32 sts = pmu_hw_sts(mode);

	//if (sts == 0)
		//pmu_enter(mode);
	//else
		//rtos_core_trace(LOG_ALW, 0, "PMU sts 0x%x, can't enter", sts);

	pmu_timer.data = (void *) mode;
	mod_timer(&pmu_timer, jiffies + l12_delay);
	enable_pmu = true;
}

init_code void sys_sleep_cancel(void)
{
	if (enable_pmu == true) {
		if (timer_is_pending(&pmu_timer))
			del_timer(&pmu_timer);
		enable_pmu = false;
	}
}

init_code static void pmu_timer_func(void *data)
{
	//enum sleep_mode_t mode = (enum sleep_mode_t ) data;
	//u32 sts = pmu_hw_sts(mode);

	//if (sts == 0)
		//pmu_enter(mode);
	//else
		//rtos_core_trace(LOG_ALW, 0, "PMU sts 0x%x, can't enter", sts);

	mod_timer(&pmu_timer, jiffies + PMU_CYCLE_IN_SEC * HZ);
}

#if !defined(PROGRAMMER) || CPU_ID == 1
static init_code int set_pmu_delay(int argc, char *argv[])
{
	int mode;
	u32 delay;

	if (argc != 3) {
		rtos_core_trace(LOG_ALW, 0x4725, "current L1 %d ms, L1.2 %d ms",
				l1_delay * 100, l12_delay * 100);
		rtos_core_trace(LOG_ALW, 0x3901, "set_pmu_delay [0/1] [time], unit:100ms");
		return 0;
	}

	mode = atoi(argv[1]);
	if (mode > 1)
		mode = 1;

	delay = atoi(argv[2]);
	if (mode == 0) {
		rtos_core_trace(LOG_ALW, 0x67b9, "set L1 delay %d -> %d", l1_delay, delay);
		l1_delay = delay;
	} else {
		rtos_core_trace(LOG_ALW, 0xb122, "set L1.2 delay %d -> %d", l12_delay, delay);
		l12_delay = delay;
	}

	return 0;
}

static DEFINE_UART_CMD(set_pcie_delay, "set_pmu_delay",
                "set_pmu_delay",
                "set pmu delay time [0:L1;1:L1.2] n [1:100ms/2:200ms/...] ",
                0, 2, set_pmu_delay);


/*
init_code int pmu_main(int argc, char *argv[])

{
	if (argc == 1) {
		rtos_core_trace(LOG_ALW, 0, "Current PMU: %s", (enable_pmu ? "on" : "off"));
		return 0;
	}

	if (strstr(argv[1], "on") && (enable_pmu == false)) {
		rtos_core_trace(LOG_ALW, 0, "Enable PMU once...");
		enable_pmu = true;
		u32 sts = pmu_hw_sts(PMU_MODE);
		// rtos_core_trace(LOG_ALW, 0, "PMU sts 0x%x, enter", sts);
		if (sts == 0) {
			pmu_enter(PMU_MODE);
		}  else {
			rtos_core_trace(LOG_ALW, 0, "PMU sts 0x%x, can't enter", sts);
		}
		enable_pmu = false;
	} else if (strstr(argv[1], "ON") && (enable_pmu == false)) {
		rtos_core_trace(LOG_ALW, 0, "Enable PMU ...");
		enable_pmu = true;
		mod_timer(&pmu_timer, jiffies + 2 * HZ);
	} else if (strstr(argv[1], "off") && (enable_pmu == true)) {
		rtos_core_trace(LOG_ALW, 0, "Disable PMU ...");
		enable_pmu = false;
		del_timer(&pmu_timer);
	} else if (strstr(argv[1], "reg")) {
		char *endp;

		r94 = strtoul(argv[2], &endp, 0);
		if (argc > 2)
			r98 = strtoul(argv[3], &endp, 0);
		if (argc > 3)
			r80 = strtoul(argv[4], &endp, 0);
		rtos_core_trace(LOG_ALW, 0, "PMU r94(0x%x), r98(0x%x) r80(0x%x)", r94, r98, r80);
	} else if (strstr(argv[1], "wait")) {
		char *endp;
		u32 r9c = readl(&misc_reg->lp_wait_time.all);
		u32 ra0 = readl(&misc_reg->usec_count.all);
		if (argc > 2) {
			r9c = strtoul(argv[2], &endp, 0);
			writel(r9c, &misc_reg->lp_wait_time.all);
		}
		if (argc > 3) {
			ra0 = strtoul(argv[3], &endp, 0);
			writel(ra0, &misc_reg->usec_count.all);
		}
		rtos_core_trace(LOG_ALW, 0, "PMU wait r9c(0x%x), ra0(0x%x)", r9c, ra0);
	} else if (strstr(argv[1], "dbg")) {
		u32 atcm_crc = crc32((void *)ATCM_BASE, ATCM_SIZE);
		rtos_mmgr_trace(LOG_ERR, 0, "ATCM CRC (0x%x)\n", atcm_crc);
		pmu_enable();
	}

	return 0;
}

static DEFINE_UART_CMD(pmu, "pmu",
		"pmu [on|off]",
		"status or on/off toggle",
		0, 5, pmu_main);
*/

// fast_code int pmu_main(int argc, char *argv[])
// {
// 	if (argc == 1) {
// 		rtos_core_trace(LOG_ALW, 0, "Current PMU: %s", (enable_pmu ? "on" : "off"));
// 		return 0;
// 	}

// 	if (strstr(argv[1], "on") && (enable_pmu == false)) {
// 		rtos_core_trace(LOG_ALW, 0, "Enable PMU once...");
// 		enable_pmu = true;
// 		u32 sts = pmu_hw_sts(PMU_MODE);
// 		// rtos_core_trace(LOG_ALW, 0, "PMU sts 0x%x, enter", sts);
// 		if (sts == 0) {
// 			pmu_enter(PMU_MODE);
// 		}  else {
// 			rtos_core_trace(LOG_ALW, 0, "PMU sts 0x%x, can't enter", sts);
// 		}
// 		enable_pmu = false;
// 	} else if (strstr(argv[1], "ON") && (enable_pmu == false)) {
// 		rtos_core_trace(LOG_ALW, 0, "Enable PMU ...");
// 		enable_pmu = true;
// 		mod_timer(&pmu_timer, jiffies + 2 * HZ);
// 	} else if (strstr(argv[1], "off") && (enable_pmu == true)) {
// 		rtos_core_trace(LOG_ALW, 0, "Disable PMU ...");
// 		enable_pmu = false;
// 		del_timer(&pmu_timer);
// 	} else if (strstr(argv[1], "reg")) {
// 		char *endp;

// 		r94 = strtoul(argv[2], &endp, 0);
// 		if (argc > 2)
// 			r98 = strtoul(argv[3], &endp, 0);
// 		if (argc > 3)
// 			r80 = strtoul(argv[4], &endp, 0);
// 		rtos_core_trace(LOG_ALW, 0, "PMU r94(0x%x), r98(0x%x) r80(0x%x)", r94, r98, r80);
// 	} else if (strstr(argv[1], "wait")) {
// 		char *endp;
// 		u32 r9c = readl(&misc_reg->lp_wait_time.all);
// 		u32 ra0 = readl(&misc_reg->usec_count.all);
// 		if (argc > 2) {
// 			r9c = strtoul(argv[2], &endp, 0);
// 			writel(r9c, &misc_reg->lp_wait_time.all);
// 		}
// 		if (argc > 3) {
// 			ra0 = strtoul(argv[3], &endp, 0);
// 			writel(ra0, &misc_reg->usec_count.all);
// 		}
// 		rtos_core_trace(LOG_ALW, 0, "PMU wait r9c(0x%x), ra0(0x%x)", r9c, ra0);
// 	} else if (strstr(argv[1], "dbg")) {
// 		u32 atcm_crc = crc32((void *)ATCM_BASE, ATCM_SIZE);
// 		rtos_mmgr_trace(LOG_ERR, 0, "ATCM CRC (0x%x)\n", atcm_crc);
// 		pmu_enable();
// 	}

// 	return 0;
// }

// static DEFINE_UART_CMD(pmu, "pmu",
// 		"pmu [on|off]",
// 		"status or on/off toggle",
// 		0, 5, pmu_main);

#endif
