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
/*! \file misc.c
 * @brief misc for timer, raid ecc, wdt ... etc
 *
 * \addtogroup rtos
 * \defgroup core misc
 * \ingroup rtos
 *
 * {@
 */
//=============================================================================
#include "types.h"
#include "stdio.h"
#include "misc_register.h"
#include "pcie_wrapper_register.h"
#include "misc.h"
#include "dma.h"
#include "irq.h"
#include "assert.h"
#include "pic.h"
#include "pmu.h"
#include "sect.h"
#include "mod.h"
#include "vic_id.h"
#include "bitops.h"
#include "string.h"
#include "console.h"
#include "event.h"
#include "timer_register.h"
#include "cpu_msg.h"
#include "mpc.h"
#include "ipc_api.h"
#include "smbus.h"
#include "cpu1_cfg_register.h"
#include "smb_registers.h"//shane add for BM data update
#include "../../btn/inc/btn_cmd_data_reg.h"
#include "../../btn/inc/btn_ftl_ncl_reg.h"
#include "otp_ctrl_register.h" //_GENE_20210906

#include "ndcu_reg_access.h"
#include "ndcu.h"

/*! \cond PRIVATE */
#define __FILEID__ misc
#include "trace.h"
#include "lib.h"
#include "nvme_spec.h"
#include "stdlib.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define CPU_CLK_333_MHZ		333	///< define CPU clk 333 Mhz
#define CPU_CLK_666_MHZ		800	///< define CPU clk 666 Mhz
#define CPU_CLK_DEFAULT		333	///< default CPU clock is 333 Mhz
#define NF_CLK_DEFAULT		100	///< default NF clock is 100 Mhz

#define TIMER0_CYCLE_PER_TICK   (CPU_CLK / HZ)		///< timer cycle is per cpu clock

// #if defined(FPGA)
// #define CPU_CLK			SYS_CLK
// #else
// #define CPU_CLK			(800 * 1000 * 1000)	///< default clock is 666Mhz
// #endif

#define TIMER0_CYCLE_PER_TICK   (CPU_CLK / HZ)

//#define SUPPORT_WDT
#if defined(SUPPORT_WDT)
#define WDT_RESET	0
#endif
//#define RAID_ERR_ISR

#define WARM_BOOT_SIGNATURE		0x54424D57  /* WMBT */
#define FW_WAIT_RESET_SIGNATURE		0x54575746  /* FWWT */
#define FW_RUN_RESET_SIGNATURE		0x54575742  /* FWWB */
#define FW_XLOADER_BOOT_SIGNATURE	0xbabeface  /* for xloader boot check */
#ifdef AGING2NORMAL_DDRBYPASS	//20201123-Eddie
#define A2N_FLAG_SIGNATURE		0x41024E42  /* A2NB */
#endif
//#if (CUST_CODE == 0) // RD Version
#define edevx_test
//#endif

/*!
 * @brief Check at compile time that something is of a particular type.
 *
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
/*lint -e(40, 550) __dummy2 is declared*/	\
	({      type __dummy; \
	         typeof(x) __dummy2; \
	         (void)(&__dummy == &__dummy2); \
	         1; \
	 })

#define time_eq_or_after(a,b)         \
	(typecheck(u32, a) && \
	 typecheck(u32, b) && \
	 ((s32)((b) - (a)) <= 0))

/*! @brief pll change flow */
#define PLLN_CTRL_0_SET(n, postdiv1, fbdiv)						\
{											\
	pll##n##_ctrl_2_t ctrl2;							\
	ctrl2.all = PLL##n##_PD_MASK | PLL##n##_DIS_SSCG_MASK | PLL##n##_DSMPD_MASK;	\
	writel(ctrl2.all, &misc_reg->pll##n##_ctrl_2);				\
	pll##n##_ctrl_0_t ctrl0;						\
	ctrl0.all = readl(&misc_reg->pll##n##_ctrl_0);				\
	ctrl0.b.pll##n##_postdiv1 = postdiv1;					\
	ctrl0.b.pll##n##_fbdiv = fbdiv;						\
	writel(ctrl0.all, &misc_reg->pll##n##_ctrl_0);				\
	ctrl2.all = PLL##n##_DIS_SSCG_MASK | PLL##n##_DSMPD_MASK;		\
	writel(ctrl2.all, &misc_reg->pll##n##_ctrl_2);				\
	delay_20us(n == 0 ? true : false);					\
}

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
enum {
	TIMER_MODE_SINGLE = 0,	///< single, stop when counting down to 0
	TIMER_MODE_CONTIG = 1,	///< continuous, restart from count down to 0
	TIMER_MODE_CHAIN  = 2,	///< chain, start to count down when preceding timer count down to 0
	TIMER_MODE_RSVD   = 3,	///<
};

enum {
	TIMER_VAL_CURRENT = 0,	///< load from current value register
	TIMER_VAL_RELOAD  = 1,	///< load from reload value register
};

enum power_state_t {
	POWER_STATE_DROP = 0,	///< Power drop below threshold
	POWER_STATE_RESUME,	///< Power resume above threshold
	POWER_STATE_STABLE,	///< Power stably above threshold for a period
};

/* pll/clk */
enum pll_sel {
	PLL_CLK_0 = 0,
	PLL_CLK_1
};

enum clk_sel {
	DIVIDER_OUTPUT = 0,
	SWITCH_CLK
};

enum clk_divider {
	DIVIDE_1 = 0,
	DIVIDE_2,
	DIVIDE_3,
	DIVIDE_4,
	DIVIDE_5,
	DIVIDE_6,
	DIVIDE_8,
	DIVIDE_10,
};

enum trace_clk_divider {
	TRACE_DIVIDE_1 = 0,
	TRACE_DIVIDE_2,
	TRACE_DIVIDE_3,
	TRACE_DIVIDE_4,
	TRACE_DIVIDE_5,
	TRACE_DIVIDE_6,
	TRACE_DIVIDE_7,
	TRACE_DIVIDE_8,
	TRACE_DIVIDE_9,
	TRACE_DIVIDE_10,
	TRACE_DIVIDE_11,
	TRACE_DIVIDE_12,
	TRACE_DIVIDE_13,
	TRACE_DIVIDE_14,
	TRACE_DIVIDE_15,
	TRACE_DIVIDE_16,
};

struct sys_pll_setting {
	int freq;
	int cpu_div;	//Eddie Merged from-3.1.7.4
	int postdiv1;
	int fbdiv;
	int ficu_src;
	int ficu_div;
	int sram_src;
	int sram_div;
	int trace_div;
};

extern volatile bool PLA_FLAG;
extern volatile bool PLN_FLAG;
extern volatile bool PLN_evt_trigger;
extern volatile bool PLN_keep_50ms;
extern volatile bool PLN_in_low;
extern volatile bool PLN_FORMAT_SANITIZE_FLAG;
extern volatile bool PLN_GPIOISR_FORMAT_SANITIZE_FLAG;
extern volatile u64 pln_loop_time;
extern volatile bool PLN_flush_cache_end;

extern volatile bool PLN_open_flag;
extern volatile bool PWRDIS_open_flag;
extern volatile bool PWRDIS_664;
extern volatile bool PLP_IC_SGM41664;
extern volatile u32 SGM_data;

/*! @brief definition of physical timer handler */
typedef void (*timer_handler_t)(void);

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
fast_data misc_register_regs_t *misc_reg = (misc_register_regs_t *) MISC_BASE;

#if CPU_ID == 1
share_data volatile int cur_cpu_clk_freq = CPU_CLK_DEFAULT;	///< global variable to show current cpu clock
share_data volatile int cur_nf_clk_freq  = NF_CLK_DEFAULT;	///< global variable to show current nf clock
#else
share_data volatile int cur_cpu_clk_freq;
share_data volatile int cur_nf_clk_freq;
#endif

fast_data_zi u32 jiffies = 0;				///< global software ticks per 100ms
fast_data_zi u64 elapsed = 0;				///< global cycles elapsed and not merged to jiffies, yet

#ifdef OTF_MEASURE_TIME
ddr_sh_data u32 baseTick;
ddr_sh_data vu32 recMsec[12];
ddr_sh_data u64 global_time_start[20];
#endif

static fast_data_zi u64 saved_tsc;			///< last saved cpu tick
#if CPU_ID == 1
share_data volatile u64 sys_time = 0;					//system time
#else
share_data volatile u64 sys_time;					//system time
#endif

fast_data LIST_HEAD(tick_timer);			///< timer list

fast_data_zi static timer_handler_t timer_handler[4];		///< each cpu has four physical timer
fast_data_zi static irq_handler_t sisr_tbl[32];			///< system misc interrupt handler table in IRQ mode
fast_data_zi static u32 sisr_in_poll_mode = 0;			///< system misc interrupt handled in SYS mode bitmap
fast_data static u32 _poll_sirq_mask = ~0;
#if !defined(FPGA)
#if CPU_ID == 1
share_data volatile u32 _SYS_CLK = 166 * 1000 * 1000;		///< default system clock is 166 Mhz
#else
share_data volatile u32 _SYS_CLK;
#endif
#endif
fast_data u8 evt_vol_mon_hook = 3;
fast_data enum power_state_t power_state = POWER_STATE_STABLE;	///< current power state
fast_data_zi struct timer_list power_timer;			///< timer to check if power stable after power drop

fast_data_zi u64 tsc64_base = 0;					///< last cpu tick

fast_data u32 gPcieRstRedirected = false;
volatile fast_data u32 gResetFlag = false;
fast_data bool gPerstInCmd = false;
// #if CO_SUPPORT_DEVICE_SELF_TEST
fast_data_zi u32    gDeviceSelfTest;
fast_data_zi u8     gCurrDSTOperation;
fast_data_zi u64    gDST_total_timer;
// recored device self test ndis
fast_data_zi u8     gCurrDSTOperationNSID;
fast_data_zi u8     gCurrDSTCompletion;
fast_data_zi u64    gCurrDSTTime;
fast_data_zi u64    gLastDSTTime; //Elapsed time of last extend DST if blocked by controller level reset
share_data_zi u8     gCurrDSTOperationImmed;
fast_data    u8*    gDSTScratchBuffer;
// #endif
fast_data bool gPlpDetectInIrqDisableMode = false;
fast_data bool gFormatInProgress = false;
fast_data bool ctrl_fatal_state = false;

/*! @brief pll0 reference setting, cpu/ficu/sram/ecc */
/*! @brief pll0 reference setting, cpu/ficu/sram/ecc */
#if 1	//Eddie Merged from-3.1.7.4
static ps_data struct sys_pll_setting cpu_pll_settings[] = {
	{ .freq = 166, .cpu_div = DIVIDE_8, .postdiv1 = 0x2, .fbdiv = 0xD5, .ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_5, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_5, .trace_div = TRACE_DIVIDE_14 },
	{ .freq = 333, .cpu_div = DIVIDE_4, .postdiv1 = 0x2, .fbdiv = 0xD5, .ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_5, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_5, .trace_div = TRACE_DIVIDE_14 },
	{ .freq = 666, .cpu_div = DIVIDE_4, .postdiv1 = 0x1, .fbdiv = 0xD5, .ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_5, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_5, .trace_div = TRACE_DIVIDE_14},
	{ .freq = 800, .cpu_div = DIVIDE_4, .postdiv1 = 0x1, .fbdiv = 0x100,.ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_6, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_6, .trace_div = TRACE_DIVIDE_8},
	{ .freq = 0 },
};
#else
static ps_data struct sys_pll_setting cpu_pll_settings[] = {
	{ .freq = 333, .postdiv1 = 0x2, .fbdiv = 0xD5, .ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_5, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_5, .trace_div = TRACE_DIVIDE_14 },
	{ .freq = 666, .postdiv1 = 0x1, .fbdiv = 0xD5, .ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_5, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_5, .trace_div = TRACE_DIVIDE_14},
	{ .freq = 800, .postdiv1 = 0x1, .fbdiv = 0x100,.ficu_src = PLL_CLK_0, .ficu_div = DIVIDE_6, .sram_src = PLL_CLK_0, .sram_div = DIVIDE_6, .trace_div = TRACE_DIVIDE_8},
	{ .freq = 0 },
};
#endif
/*! @brief pll1 reference setting, nf0 and nf1 */
static ps_data struct sys_pll_setting nf_pll_settings[] = {
	{ .freq = 100, .postdiv1 = 0x2, .fbdiv = 0x20 },
	{ .freq = 400, .postdiv1 = 0x2, .fbdiv = 0x80 },
	{ .freq = 533, .postdiv1 = 0x2, .fbdiv = 0xAB },
	{ .freq = 666, .postdiv1 = 0x2, .fbdiv = 0xD5 },
	{ .freq = 800, .postdiv1 = 0x1, .fbdiv = 0x80 },
	{ .freq = 950, .postdiv1 = 0x1, .fbdiv = 0x98 },
	{ .freq = 1066, .postdiv1 = 0x1, .fbdiv = 0xAB},
	{ .freq = 1200, .postdiv1 = 0x1, .fbdiv = 0xC0 },
	{ .freq = 1600, .postdiv1 = 0x0, .fbdiv = 0x80 },
	{ .freq = 0 },
};

//-----------------------------------------------------------------------------
// Codes
//-----------------------------------------------------------------------------
extern void __attribute__((weak, alias("__ncl_cmd_power_stable"))) ncl_cmd_power_stable(void);
extern void __attribute__((weak, alias("__ncl_cmd_power_loss"))) ncl_cmd_power_loss(void);
extern void __attribute__((weak, alias("__ftl_cmd_power_loss"))) ftl_cmd_power_loss(void);

/*!
 * @brief alias function for ramdisk call ncl_cmd_power_stable
 *
 * @return	none
 */
void fast_code __ncl_cmd_power_stable(void)
{
	return;
}

/*!
 * @brief alias function for ramdisk call ncl_cmd_power_loss
 *
 * @return	none
 */
void fast_code __ncl_cmd_power_loss(void)
{
#if defined(MPC)
	cpu_msg_issue(CPU_BE, CPU_MSG_BE_PL, 0, 0);
#endif
}

/*!
 * @brief alias function for ramdisk call ftl_cmd_power_loss
 *
 * @return	none
 */
void fast_code __ftl_cmd_power_loss(void)
{
#if defined(MPC)
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FTL_PL, 0, 0);
#endif
}

/*!
 * @brief read misc register
 *
 * @param reg	register offset
 *
 * @return	current register value
 */
static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}

/*!
 * @brief write misc register
 *
 * @param data	data to be written
 * @param reg	register offset
 *
 * @return	not used
 */
static inline void misc_writel(u32 data, u32 reg)
{
	writel(data, (void *)(MISC_BASE + reg));
}

/*!
 * @brief read timer register
 *
 * @param reg	register offset
 *
 * @return	current register value
 */
static inline u32 timer_readl(u32 reg)
{
	return readl((void *)(TIMER_BASE + reg));
}

/*!
 * @brief write timer register
 *
 * @param data	data to be written
 * @param reg	register offset
 *
 * @return	not used
 */
static inline void timer_writel(u32 data, u32 reg)
{
	writel(data, (void *)(TIMER_BASE + reg));
}

static inline u32 cpu_cfg_readl(u32 reg)
{
	return readl((void *)(CPU_CONF_BASE + reg));
}

static inline void cpu_cfg_writel(u32 data, u32 reg)
{
	writel(data, (void *)(CPU_CONF_BASE + reg));
}
static inline u32 otp_readl(u32 reg)
{
    return readl((void *)(OTP_BASE + reg));
}
static inline void otp_writel(u32 data, u32 reg)
{
    writel(data, (void *)(OTP_BASE + reg));
}

#if defined(MPC)
#if 1	//Stanley modified
fast_code UNUSED static u32 cpu_wb_lock(u8 cpu_id)
{	//20210510-Eddie
	cpu_msg_sync_start();
	dsb();
	cpu_msg_issue(cpu_id, CPU_MSG_WB_SYNC_LOCK, 0, 0);
	cpu_msg_sync_end();
	return 0;
}

fast_code void ipc_cpu_wb_lock(volatile cpu_msg_req_t *req)
{	//20210510-Eddie
	irq_disable();

		cpu_msg_sync_done(req->cmd.tx);
		dsb();
		isb();
		do{
			__asm__("nop");
		}while(1);
}
#else
fast_code UNUSED static u32 cpu_wb_lock(u8 cpu_id)
{
	int ret = 0;
	writel(0, &misc_reg->fw_stts_12);
	dsb();
	cpu_msg_issue(cpu_id, CPU_MSG_WB_SYNC_LOCK, 0, 0);
	do {
		ret = readl(&misc_reg->fw_stts_12);
	} while (ret == 0);
	return 0;
}

fast_code void ipc_cpu_wb_lock(volatile cpu_msg_req_t *req)
{
	u32 chk = readl(&misc_reg->fw_stts_12);
	if (chk == 0) {
		writel(1, &misc_reg->fw_stts_12);
		dsb();
		isb();
		do{
			__asm__("nop");
		}while(1);
	}
	sys_assert(0);
}
#endif
#endif


fast_code u32 get_tsc_lo(void)
{
	return timer_readl(TIMER4_VAL_L);
}

fast_code u32 get_tsc_hi(void)
{
	return timer_readl(TIMER4_VAL_H);
}

fast_code u64 get_tsc_64(void)
{
	u64 hi;
	u32 lo;

	hi = get_tsc_hi();
	lo = get_tsc_lo();
	if (lo < 50) {// If there is risk hi increments between read hi and lo
		hi = get_tsc_hi();// Re-read already stabled hi
	}
	//sys_assert((((u32) hi) >> 24) == 0);
	return ((hi << 32) | lo) + tsc64_base;
}

fast_code u32 get_tsc_ms(void)
{
	u32 tc;
	tc = *(vu32*)0xC0201044;
	tc = (tc*1000)/(CPU_CLK);
	return tc;
}

/*!
 * @brief get lower part of system tick, universal between CPUs
 *
 * @return	lower part of system tick
 */
fast_code u32 get_st1_lo(void)
{
	return misc_readl(SHARED_TIMER1_VAL_L);
}

/*!
 * @brief get higher part of system tick, universal between CPUs
 *
 * @return	lower part of system tick
 */
fast_code u32 get_st1_hi(void)
{
	return misc_readl(SHARED_TIMER1_VAL_H);
}

fast_code u64 get_st1_64(void)
{
	u64 hi;
	u32 lo;

	hi = get_st1_hi();
	lo = get_st1_lo();
	if (lo < 50) {// If there is risk hi increments between read hi and lo
		hi = get_st1_hi();// Re-read already stabled hi
	}
	//sys_assert((((u32) hi) >> 24) == 0);
	return (hi << 32) | lo;
}


fast_code bool timer_is_pending(const struct timer_list *timer_entry)
{
	struct list_head *cur, *saved;
	list_for_each_safe(cur, saved, &tick_timer) {
		struct timer_list *tm = container_of(cur, struct timer_list, entry);
		if (tm == timer_entry)
			return true;
	}
	return false;
}

fast_code int del_timer(struct timer_list *timer_entry)
{
	/* active */
	if (timer_is_pending(timer_entry)) {
		list_del_init(&timer_entry->entry);
		return 1;
	}

	return 0;
}

ps_code void misc_sys_isr_enable(u32 sirq)
{
	bool irq = (sisr_in_poll_mode & (1 << sirq)) ? false : true;

	if (irq) {
		u32 val = misc_readl(SYS_INT_MASK_REG);

		val &= ~(1u << sirq);
		misc_writel(val, SYS_INT_MASK_REG);
	} else {
		_poll_sirq_mask &= ~(1u << sirq);
	}
}

ps_code void misc_sys_isr_disable(u32 sirq)
{
	bool irq = (sisr_in_poll_mode & (1 << sirq)) ? false : true;

	if (irq) {
		u32 val = misc_readl(SYS_INT_MASK_REG);

		val |= (1 << sirq);
		misc_writel(val, SYS_INT_MASK_REG);
	} else {
		_poll_sirq_mask |= (1u << sirq);
	}
}

ps_code void sirq_register(u32 sirq, irq_handler_t handler, bool poll)
{
	sisr_tbl[sirq] = handler;
	if (poll)
		sisr_in_poll_mode |= 1 << sirq;
}
#if(PLP_SUPPORT == 1)
fast_code void check_power(void)
{
	int i = 0;

    while (!(gpio_get_value(GPIO_PLP_DETECT_SHIFT) & gpio_get_value(GPIO_PLP_STRPG_SHIFT)))
    {

		mdelay(2);
		i++;
		if(i > 250)
		{
			//rtos_core_trace(LOG_ALW, 0, "Albert wait %x ", i);
			break;
		}

    }
	return;
}
#endif

//-----------------------------------------------------------------------------
/**
    One-time initialization for Non-Polling Interrupt\n

    @return
**/
//-----------------------------------------------------------------------------
fast_code void HalIrq_InitNonPollingInterrupt(void)
{
    misc_sys_isr_enable(SYS_VID_GPIO);

#if (POWER_ISR == mENABLE)
    misc_sys_isr_enable(SYS_VID_PWR_LOSS);
#endif
    misc_sys_isr_enable(SYS_VID_SMB0);
    #if CPU_ID == 1
        #ifdef SUPPORT_WDT
        misc_sys_isr_enable(SYS_VID_WDT_RST);
        #endif
    #endif
}

fast_code int mod_timer(struct timer_list *timer_entry, u64 expires)
{
	/* Even we are still in tick_handler, we have move out the queue */
	if (timer_is_pending(timer_entry)) {
		timer_entry->expires = expires;
		return 1;
	} else {
		/* for simplicity, no sort */
		u32 flags = irq_save();
		timer_entry->expires = expires;
		list_add(&timer_entry->entry, &tick_timer);
		irq_restore(flags);
		return 0;
	}
}

fast_code void add_timer(struct timer_list *timer_entry)
{
	sys_assert(!timer_is_pending(timer_entry));
	mod_timer(timer_entry, timer_entry->expires);
}

/*!
 * @brief core handler for timer based on tick
 *
 * @return	None
 */
static fast_code void timer_events(void)
{
	struct list_head *cur, *saved;
	list_for_each_safe(cur, saved, &tick_timer) {
		struct timer_list *tm = container_of(cur, struct timer_list, entry);
		if (time_eq_or_after(jiffies, tm->expires)) {
			list_del_init(cur); /* in case of re-add */
			tm->function(tm->data);
		}
	}
}

static fast_code void tick_handler(void)
{
	u64 cur_tsc = get_tsc_64();
	elapsed = cur_tsc - saved_tsc + elapsed;
	saved_tsc = cur_tsc;

	/* we don't support divider for 64bit */
	while (elapsed >= TIMER0_CYCLE_PER_TICK) {
		jiffies += 1;
		#if CPU_ID == 1
		sys_time += (100*HZ/10);
		//extern struct nvmet_ctrlr *ctrlr;
		//ctrlr->cur_feat.timestamp += (100*HZ/10);
		#endif
		elapsed -= TIMER0_CYCLE_PER_TICK;
	}

	#if 0 //CPU_ID == CPU_FE // temp remove disable IRQ. 2024/6/14.shengbin yang
	BEGIN_CS1
	#endif
	timer_events();
	#if 0 // CPU_ID == CPU_FE
	END_CS1
	#endif
}

#if CPU_ID == 1
#define TICK_1MS (TIMER0_CYCLE_PER_TICK / 100)
ps_code u64 get_cur_sys_time(void)
{
	u64 cur_tsc = get_tsc_64();
	u32 aligned_1ms;

	aligned_1ms = cur_tsc - saved_tsc + elapsed;

	aligned_1ms = aligned_1ms / TICK_1MS;

	return (sys_time + aligned_1ms);
}

ps_code void set_cur_sys_time(u64 time)
{
	sys_time = time;
}
#endif


fast_code void udelay(u32 us)
{
#if !defined(HAVE_DISABLE_UDELAY) && !defined(HAVE_VELOCE)
	u64 cur = get_tsc_64();

	us *= CYCLE_PER_US;

	while (get_tsc_64() - cur < us);
#endif
}

fast_code void mdelay_plp(u32 ms)
{
#if !defined(HAVE_DISABLE_UDELAY) && !defined(HAVE_VELOCE)
	u64 cur = get_tsc_64();

	u32 us = 1000 * ms;
	us *= CYCLE_PER_US;
	extern volatile u8 plp_trigger;
	while (get_tsc_64() - cur < us)
	{
		if(plp_trigger)
			break;
	}
#endif
}


fast_code void mdelay(u32 ms)
{
	udelay(1000 * ms);
}

fast_code u32 time_elapsed_in_jiffies(u32 timestamp)
{
        if (jiffies >= timestamp) {
                return jiffies - timestamp;
        } else {
                timestamp = 0xffffffff - timestamp + 1;
                return jiffies + timestamp;
        }
}

fast_code u32 time_elapsed_in_us(u64 timestamp)
{
	u64 cur_tsc = get_tsc_64();
	u32 elapsed_in_us = 0;

	while (cur_tsc - timestamp > CYCLE_PER_US) {
		elapsed_in_us++;
		timestamp += CYCLE_PER_US;
	}

	return elapsed_in_us;
}

fast_code u32 time_elapsed_in_ms(u64 timestamp)
{
	u64 cur_tsc = get_tsc_64();
	u32 elapsed_in_ms = 0;

	while (cur_tsc - timestamp > CYCLE_PER_MS) {
		elapsed_in_ms++;
		timestamp += CYCLE_PER_MS;
	}

	return elapsed_in_ms;
}

/*!
 * @brief timer interrupt routine
 *
 * @return	not used
 */
static fast_code void timer_isr(void)
{
	timer_int_t timer_int = {
		.all = timer_readl(TIMER_INT),
	};

	if (timer_int.b.timer_int != 0) {
		int i = 0;
		for (i = 0; i < 4; i++) {
			if (timer_int.b.timer_int & (1 << i)) {
				if (timer_handler[i])
					timer_handler[i]();
			}
		}
	}
	timer_writel(timer_int.all, TIMER_INT);
}

/*!
 * @brief timer function to continue NCL process after power stable
 *
 * @param data		caller string
 *
 * @return		not used
 */
static ddr_code void power_timer_handling(void *data)
{
	vol_mon_stts_t vol_stts;

	switch(power_state) {
	case POWER_STATE_STABLE:
		// This timer should not be enabled when power stable
		//sys_assert(0);
		break;
	case POWER_STATE_DROP:
		vol_stts.all = misc_readl(VOL_MON_STTS);
		if (vol_stts.b.v33_good) {
			power_state = POWER_STATE_RESUME;
			// Reconfigure a longer timer
			mod_timer(&power_timer, jiffies + HZ);
		}
		break;
	case POWER_STATE_RESUME:
		del_timer(&power_timer);
		ncl_cmd_power_stable();
		break;
	}
}

/* only in asic */
#if !defined(FPGA) && CPU_ID == 1
#if (POWER_ISR == mENABLE)
static ddr_code void power_isr(void)
{
	vol_mon_stts_t vol_stts;
	vol_stts.all = misc_readl(VOL_MON_STTS);

	if (vol_stts.all & (POWER_LOSS_33V_MASK)) {
		extern void hal_nvmet_abort_xfer(bool abort);

		///< step 1. configure NVMe HW to disable SQE_fetching / CQE_returning
		misc_nvm_ctrl_reset(true);

		///< step 2. configure cmd_proc to abort DMA xfer
		///< continue to fetch DTAG from BTN and directly release DTAG to BTN
		hal_nvmet_abort_xfer(true);

		urg_evt_set(evt_vol_mon_hook, 0, 0);

	}
	misc_writel(vol_stts.all, VOL_MON_STTS);
}
#endif
#endif

ddr_code void misc_set_xloader_boot(u32 img_pos)
{
	writel(FW_XLOADER_BOOT_SIGNATURE, &misc_reg->fw_stts_8.all);
	writel(img_pos, &misc_reg->fw_stts_9.all);
}

slow_code void misc_set_warm_boot_ca_mode(u32 active_type)
{
    writel(active_type, &misc_reg->fw_stts_5.all);
}

fast_code void misc_set_warm_boot_flag(u32 *data, u32 count)
{
	u32 i;
	u32 *reg;

	writel(WARM_BOOT_SIGNATURE, &misc_reg->fw_stts_8.all);
	reg = &misc_reg->fw_stts_9.all;
	for (i = 0; i < count; i ++) {
		writel(*data, reg);
		data ++;
		reg++;
	}
}

fast_code void misc_clear_warm_boot_flag(void)
{
	writel(0, &misc_reg->fw_stts_8.all);
	writel(0, &misc_reg->fw_stts_9.all);
	writel(0, &misc_reg->fw_stts_5.all);	//Reset commit type
}

fast_code void misc_get_warm_boot_data(u32 *data, u32 count)
{
	u32 i;
	u32 *reg;

	reg = &misc_reg->fw_stts_9.all;
	for (i = 0; i < count; i ++) {
		*data = readl(reg);
		data++;
		reg++;
	}
}

fast_code bool misc_is_warm_boot(void)
{
	return (readl(&misc_reg->fw_stts_8.all) == WARM_BOOT_SIGNATURE);
}

fast_code void misc_set_fw_wait_reset_flag(void)
{
	writel(FW_WAIT_RESET_SIGNATURE, &misc_reg->fw_stts_8.all);
}

fast_code bool misc_is_fw_wait_reset(void)
{
	return (readl(&misc_reg->fw_stts_8.all) == FW_WAIT_RESET_SIGNATURE);
}

fast_code void misc_set_fw_run_reset_flag(void)
{
	writel(FW_RUN_RESET_SIGNATURE, &misc_reg->fw_stts_8.all);
}

fast_code bool misc_is_fw_run_reset(void)
{
	return (readl(&misc_reg->fw_stts_8.all) == FW_RUN_RESET_SIGNATURE);
}

#ifdef AGING2NORMAL_DDRBYPASS	//20201123-Eddie
fast_code void misc_set_aging2normal_ddr_bypass_flag(void)
{
	writel(A2N_FLAG_SIGNATURE, &misc_reg->fw_stts_10.all);
}

fast_code bool misc_is_aging2normal_ddr_bypass(void)
{
	return (readl(&misc_reg->fw_stts_10.all) == A2N_FLAG_SIGNATURE);
}
#endif
slow_code bool misc_is_CA1_active(void)	//20210426-Eddie
{
    return (readl(&misc_reg->fw_stts_5.all) == CA_1_SIGNATURE);
}
slow_code bool misc_is_CA3_active(void)
{
    return (readl(&misc_reg->fw_stts_5.all) == CA_3_SIGNATURE);
}
#if 1
fast_code void misc_set_STOP_BGREAD(void)
{
    writel(STTS11_BGREAD_STOP, &misc_reg->fw_stts_11.all);
}
fast_code bool misc_is_STOP_BGREAD(void)
{
    return (readl(&misc_reg->fw_stts_11.all) == STTS11_BGREAD_STOP);
}

fast_code void misc_clear_STOP_BGREAD(void)
{
    writel(0, &misc_reg->fw_stts_11.all);
}
#else
fast_code void misc_set_STOP_PARD(void)
{
    u32 misc_stts11;
    misc_stts11 = readl(&misc_reg->fw_stts_11.all);
    misc_stts11 |= STTS11_PARD_STOP;
    writel(misc_stts11, &misc_reg->fw_stts_11.all);
}

fast_code bool misc_is_STOP_PARD(void)
{
    return ((readl(&misc_reg->fw_stts_11.all)&STTS11_PARD_STOP)== STTS11_PARD_STOP);
}

fast_code void misc_clear_STOP_PARD(void)
{
    u32 misc_stts11;
    misc_stts11 = readl(&misc_reg->fw_stts_11.all);
    misc_stts11 &= ~STTS11_PARD_STOP;
    writel(misc_stts11, &misc_reg->fw_stts_11.all);
}

fast_code void misc_set_STOP_REFR(void)
{
    u32 misc_stts11;
    misc_stts11 = readl(&misc_reg->fw_stts_11.all);
    misc_stts11 |= STTS11_REFR_STOP;
    writel(misc_stts11, &misc_reg->fw_stts_11.all);
}

fast_code bool misc_is_STOP_REFR(void)
{
    return ((readl(&misc_reg->fw_stts_11.all)&STTS11_REFR_STOP)== STTS11_REFR_STOP);
}

fast_code void misc_clear_STOP_REFR(void)
{
    u32 misc_stts11;
    misc_stts11 = readl(&misc_reg->fw_stts_11.all);
    misc_stts11 &= ~STTS11_REFR_STOP;
    writel(misc_stts11, &misc_reg->fw_stts_11.all);
}
#endif

slow_code_ex void misc_set_startwb_flag(u32 delay)
{
	u32 sign;
	sign = 0x0000000B;
	if (delay)
		sign |= (delay << 16);

	writel(sign, &misc_reg->fw_stts_14.all);
}

slow_code_ex bool misc_is_startwb(void)
{
	return ((readl(&misc_reg->fw_stts_14.all) & 0x000000FF) == 0x0000000B);
}

slow_code_ex u32 misc_get_startwb(void)
{
	u32 delay = 0;
	delay |= (readl(&misc_reg->fw_stts_14.all) >> 16);
	return delay;
}

slow_code_ex void misc_clear_startwb(void)
{
	writel(0, &misc_reg->fw_stts_14.all);
}

fast_code void misc_reset(enum reset_type type)
{
	reset_ctrl_t reset_ctrl = {
        // Benson Modify : prevent HW return a non-zero value
		//.all = readl(&misc_reg->reset_ctrl.all),
        .all = 0xFFFF0000 & readl(&misc_reg->reset_ctrl.all),
	};

	if (type == RESET_CPU){
		//No Print
	}
	else	{
		if(misc_is_warm_boot()==false)
        {
			rtos_core_trace(LOG_INFO, 0xd1b8, "misc_reset type %d", type);
        }
	}
	/*
	if (type > RESET_CPU){
		rtos_core_trace(LOG_INFO, 0, "misc_reset type %d > RESET_CPU", type);
		return;
	}
	*/
	reset_ctrl.b.reset_ctrl |= BIT(type);
	writel(reset_ctrl.all, &misc_reg->reset_ctrl.all);
#if 0 //Stanley Modified
	if (type == RESET_CPU){
		do{		//20210510-Eddie
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			__asm__("nop");
			}while(1);
	}
#endif
	reset_ctrl.b.reset_ctrl &= ~(BIT(type));
	writel(reset_ctrl.all, &misc_reg->reset_ctrl.all);
}

/*!
 * @brief private watchdog timer interrupt routine
 *
 * @return		not used
 */
__attribute__((unused)) fast_code static void wdt_isr(void)
{
	wdt_int_t wdt_int;

#if WDT_RESET == 0 && CPU_ID != 1
	wdt_rst_int_sts_t wdt_rst_int_sts;
	wdt_rst_int_sts.all = misc_readl(WDT_RST_INT_STS);
	if (wdt_rst_int_sts.all) {
		rtos_core_trace(LOG_ERR, 0x31ef, "some CPU dead %x", wdt_rst_int_sts.all);
		panic("WDT");
	}
#endif

	wdt_int.all = 1;
	timer_writel(wdt_int.all, WDT_INT);

	//rtos_misc_trace(LOG_ERR, 0, "wdt isr %x\n", timer_readl(0));
}

/*!
 * @brief interrupt routine to handle watch dog reset signal
 *
 * @note if any CPU halt and not response to wdt, it will issue reset request to other cpu
 * when any cpu receive reset signal, it reset system by software
 *
 * not ready yet
 *
 * @return	not used
 */
__attribute__((unused)) ddr_code static void wdt_rst_isr(void)
{
	wdt_rst_int_sts_t wdt_rst_int_sts;

	wdt_rst_int_sts.all = misc_readl(WDT_RST_INT_STS);

	rtos_core_trace(LOG_ERR, 0x88b0, "cpu dead %x ", wdt_rst_int_sts.all);

	// todo: we should have some record here to save this status
	//       example save event log here, but if dead CPU is CPU_BE, how to ?

	// after error collection, we should panic this CPU to chip reset by do a panic
	panic("WDT_RST");
}

#ifndef SUPPORT_WDT
/*lint -esym(528, wdt_init) */
#endif
__attribute__((unused)) init_code static void wdt_init(void)
{
	wdt_ctrl_t ctrl;

	ctrl.all = 0;
	timer_writel(ctrl.all, WDT_CTRL);

	timer_writel(WDT_TIMER_CLK, WD_TIMER);
	timer_writel(WDT_TIMER_CLK, WDT_REL);

	sirq_register(SYS_VID_WDT, wdt_isr, true);
	misc_sys_isr_enable(SYS_VID_WDT);

#if CPU_ID == 1
	sirq_register(SYS_VID_WDT_RST, wdt_rst_isr, false);
	//misc_sys_isr_enable(SYS_VID_WDT_RST);
#endif
}

ddr_code void wdt_start(void)
{
#if defined(SUPPORT_WDT)
	wdt_ctrl_t ctrl;

	ctrl.all = 0;
	ctrl.b.watchdog_en = 1;
	ctrl.b.watchdog_int_en = 1;
	ctrl.b.watchdog_reload_en = 1;
#if CPU_ID == 1
	ctrl.b.watchdog_reset_en = WDT_RESET;
#else
	ctrl.b.watchdog_reset_en = 0;
#endif
	timer_writel(ctrl.all, WDT_CTRL);

#if CPU_ID == 1
	reset_ctrl_t reset_ctrl = {
		.all = readl(&misc_reg->reset_ctrl.all),
	};
	// determine wdt reset is chip reset or just CPU reset
	// cpu reset is useless, we need full reset
	reset_ctrl.b.wdt_chip_rst_en = WDT_RESET;
	writel(reset_ctrl.all, &misc_reg->reset_ctrl.all);
#endif
#endif
}


#ifndef FPGA
/*!
 * @brief raid buffer error interrupt routine for ecc error
 *
 * @return	not used
 */
__attribute__((unused)) ps_code static void raid_err_isr(void)
{
	raidm_err_stts0_t sts;

	sts.all = readl(&misc_reg->raidm_err_stts0);
	rtos_misc_trace(LOG_ERR, 0x204a, "raid err pos: %x\n", sts.b.raidm_err_loc0);
	sts.all = 0;
	sts.b.raidm_err_int_en = 1;
	writel(sts.all, &misc_reg->raidm_err_stts0);
	panic("raid ecc error please let me know!");
}

/*!
 * @brief ecc initialization for read buffer
 *
 * @return	not used
 */
ps_code UNUSED static void raid_err_init(void)
{
	raidm_err_stts0_t sts;

	sts.all = readl(&misc_reg->raidm_err_stts0);
	sts.b.raidm_err_int_en = 1;
	writel(sts.all, &misc_reg->raidm_err_stts0);
#if 0 /* not ready yet */
	sirq_register(SYS_VID_RAIDM_ERR, raid_err_isr);
	misc_sys_isr_enable(SYS_VID_RAIDM_ERR);
#endif
	writel(sts.all, &misc_reg->raidm_err_stts0);
}
#endif

fast_code void misc_nvm_ctrl_reset(bool reset)
{
	nvm_abt_ctrl_t abt_ctrl = {.all = misc_readl(NVM_ABT_CTRL),};

	abt_ctrl.b.nvm_ctrl_rst = reset;

	misc_writel(abt_ctrl.all, NVM_ABT_CTRL);
}
#if(WB_IDLE_MODE == ENABLE)
static UNUSED fast_code void misc_wait_dpe_idle(void)	//20210426-Eddie
{
	u32 dpe_req_ptr = readl((void *)0xc0010228);
	u32 dpe_rsp_ptr = readl((void *)0xc0010230);

	dpe_req_ptr &= 0x000000FF;
	dpe_rsp_ptr &= 0x000000FF;

	while(dpe_req_ptr != dpe_rsp_ptr);
}
#endif
static fast_code void _sys_misc_isr(u32 sirq)
{
	do {
		u32 irq = 31 - clz(sirq);

		if (sisr_tbl[irq]) {
			sisr_tbl[irq]();
		} else {
			panic("no sirq handler");
		}
		sirq &= ~(1u << irq);
	} while (sirq);
}

fast_code void share_timer_irq_disable(void)
{
	shared_timer0_ctrl_t ctrl;
	ctrl.all = misc_readl(SHARED_TIMER0_CTRL);
	ctrl.b.shr_timer0_start = 0;
	ctrl.b.shr_timer0_int_en = 0;

	/*
	 * When the timer is started, if a preset interrupt is inserted in the middle of the timer run,
	 * it may cause the preset interrupt to exit and the timer processing encounters panic, so clear
	 */
	writel(1 << SYS_VID_ST, &misc_reg->unm_sys_int);
	writel(1, &misc_reg->shared_timer0_int);
	misc_writel(ctrl.all, SHARED_TIMER0_CTRL);
	misc_sys_isr_disable(SYS_VID_ST);
	sisr_tbl[SYS_VID_ST] = NULL;
}
/*!
 * @brief system interrupt routine
 *
 * @return		not used
 */
static fast_code void sys_misc_isr(void)
{
	u32 sirq = misc_readl(MASKED_SYS_INT_REG);

	/* this interrupt may be clear by others CPU */
	if (sirq == 0)
		return;

	writel(sirq, &misc_reg->unm_sys_int);
	_sys_misc_isr(sirq);
}

fast_code static void poll_sys_misc_isr(void)
{
	u32 flags = irq_save();
	u32 sirq = misc_readl(UNM_SYS_INT) & (~_poll_sirq_mask);

	/* this interrupt may be clear by others CPU */
	if (sirq == 0) {
		irq_restore(flags);
		return;
	}

	writel(sirq, &misc_reg->unm_sys_int);
	irq_restore(flags);
	_sys_misc_isr(sirq);
}

fast_code void cpu_halt(int cpu_id, bool halt)
{
	cpu_ctrl_t ctrl = { .all = cpu_cfg_readl(CPU_CTRL)};
	u32 mask = 1 << (HALT_CPU1_SHIFT + cpu_id); // cpu id 0 base

	if (halt)
		ctrl.all |= mask;
	else
		ctrl.all &= ~mask;

	cpu_cfg_writel(ctrl.all, CPU_CTRL);
}

fast_code void cpu_reset(int cpu_id)
{
	cpu_ctrl_t ctrl = { .all = cpu_cfg_readl(CPU_CTRL)};
	u32 mask = 1 << cpu_id; // cpu id 0 base

	ctrl.all &= ~mask;
	cpu_cfg_writel(ctrl.all, CPU_CTRL);

	udelay(5);

	ctrl.all |= mask;
	cpu_cfg_writel(ctrl.all, CPU_CTRL);
}

ps_code void warm_boot_enable(bool enable)
{
	fw_stts_2_t fw_stts_2 = {
		.all = enable,
	};

	misc_writel(fw_stts_2.all, FW_STTS_2);
}

ps_code bool warm_boot_on(void)
{
	fw_stts_2_t fw_stts_2 = {
		.all = misc_readl(FW_STTS_2),
	};

	return (bool)!!fw_stts_2.all;
}

fast_code void warm_boot(void)
{
	UNUSED u32 i;
	/* there are 5 modules:
	 *
	 * 1) NVMe (SQ/CQ, DMA) <- power on and restore
	 * 2) CmdProc           <- reset
	 * 3) BM                <- reset
	 * 4) NCB               <- suspend NCB (restore NF clk and so on depends on NAND), otherwise we will experience NCB Dec error in ROM
	 * 5) misc              <- timer, ... and so on
	 *
	 * PS:
	 * 1. ROM will scrub all memory, so no information will be kept, you can save some information in media
	 * 2. FTL will flush
	 */
#if 1//Aligned to Stanley 0505
	irq_disable();

#if defined(MPC)	//20210326-Eddie-Disable cpu lock for btn cmds panic
	u16 cpu = 0;
	for (cpu = MPC - 1; cpu > 0; cpu--) {
		cpu_wb_lock(cpu);
	}
      #if 0
	writel(0, &misc_reg->fw_stts_12);
      #endif
#endif
#if WB_WO_RESET
	cpu_clk_init(320);
#endif
#endif
#if(WB_IDLE_MODE == ENABLE)
	//misc_wait_dpe_idle();	//20210426-Eddie
#endif
//	ncl_warmboot_suspend();
	nf_clk_init(100);
	udelay(20);
	misc_reset(RESET_NCB);

	mdelay(1);	//20210503

#if defined(MPC)
	for (i = 1; i < MPC; i++) { //warmboot_restart_from_rom_0515.patch
		cpu_halt(i, true);	//disable other cpu+
	#if WB_WO_RESET
		cpu_reset(i);
	#endif
	}
#endif

#if WB_WO_RESET	//warmboot_restart_from_rom_0515.patch
	typedef void (*main_t)(void);
	main_t rom = (main_t)0x10000000;
	u32 sctlr;
	icache_ctrl(false);
	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) :: "memory", "cc");
	sctlr &= ~1;
	__asm__ __volatile__("dsb");
	__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");
	rom();
#else
	//misc_reset(RESET_BM);
	cpu_clk_init(320);
	//icache_ctrl(false);	//Stanley req.
	//mdelay(1);	//20210430
	misc_reset(RESET_CPU);
	/*check if warm reset failed and return aer event to host*/
#endif

	do{	//Stanley req.

	}while(1);
	#if 0
	u64 start = get_tsc_64();
	extern void nvmet_evt_aer_in();
	while (time_elapsed_in_ms(start) <= 3000);
	nvmet_evt_aer_in((NVME_EVENT_TYPE_ERROR_STATUS << 16) | ERR_STS_FW_IMG_LOAD_ERR, 0);
	#endif
}

fast_code NOINLINE void delay_20us(bool pll0)
{
	if (pll0) {
		u32 cnt = 40; // 2Mhz SW CLK
		while (cnt-- > 0)
			__asm__("");
	} else {
		udelay(20);
	}
}
//_GENE_20201116
/*!
 * @brief turn on/off PLL1
 *
 * @param off		turn off or on
 *
 * @return		none
 */
ps_code void pll1_power(bool power_down) //orginal ps_code
{
	pll1_ctrl_2_t pll1 = { .all = misc_readl(PLL1_CTRL_2), };
	if (power_down == true)
		pll1.b.pll1_pd = 1;
	else
		pll1.b.pll1_pd = 0;
	misc_writel(pll1.all, PLL1_CTRL_2);

	if (power_down == false) {
		udelay(20);
        u8 ch;
        nphy_ctrl_reg0_t reg;
        for (ch = 0; ch < 8; ch++) {
            reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
            reg.b.nphy_sync_rstn = 0;
            ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);

            reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
            reg.b.nphy_sync_rstn = 1;
            ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);

            reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
            reg.b.nphy_sync_en = 1;
            ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
            ndcu_delay(1);
            reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
            reg.b.nphy_sync_en = 0;
            ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
        }
	}
}
/*!
 * @brief set cpu clock, affect ficu/sram/ecc also
 *
 * @param freq		new cpu frequency
 *
 * @return		current cpu frequency
 */
norm_ps_code u32 cpu_clk_init(int freq)
{
	struct sys_pll_setting *p = cpu_pll_settings;
	do {
		if (p->freq >= freq)
			break;
		p ++;
	} while (p->freq != 0);
	sys_assert(p->freq);

	cur_cpu_clk_freq = p->freq;

	clk_divider_t div;
	div.all = readl(&misc_reg->clk_divider);
	div.b.sw_clk_div = DIVIDE_8;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	// switch to sw_clk
	clk_src_mux_t clk_src_mux;
	clk_src_mux.all = readl(&misc_reg->clk_src_mux);

	clk_src_mux.b.sram_clk_sel = SWITCH_CLK;

	clk_src_mux.b.sw_clk_src = PLL_CLK_1;
	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	div.b.cpu_clk_sel = div.b.ficu_clk_sel = SWITCH_CLK;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	// set div and src
	div.b.cpu_clk_div = p->cpu_div;		//Eddie Merged from-3.1.7.4
	div.b.ficu_clk_div = p->ficu_div;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	clk_src_mux.b.cpu_clk_src = PLL_CLK_0;
	clk_src_mux.b.ficu_clk_src = p->ficu_src;

	clk_src_mux.b.sram_clk_src = p->sram_src;
	clk_src_mux.b.sram_clk_div = p->sram_div;

	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	// set pll freq
	PLLN_CTRL_0_SET(0, p->postdiv1, p->fbdiv);

	// switch back to div output
	div.b.cpu_clk_sel = div.b.ficu_clk_sel = DIVIDER_OUTPUT;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);
	clk_src_mux.b.sram_clk_sel = DIVIDER_OUTPUT;
	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	return p->freq;
}

/*!
 * @brief set nf clock
 *
 * @param freq		new frequency of nf
 *
 * @return		current frequency of nf
 */
ps_code u32 nf_clk_init(int freq)
{
	struct sys_pll_setting *p = nf_pll_settings;
	do {
		if (p->freq >= freq)
			break;
		p ++;
	} while (p->freq != 0);
	sys_assert(p->freq);

	cur_nf_clk_freq = p->freq;

	clk_divider_t div;
	div.all = readl(&misc_reg->clk_divider);
	div.b.sw_clk_div = DIVIDE_8;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	// switch to sw_clk
	clk_src_mux_t clk_src_mux;
	clk_src_mux.all = readl(&misc_reg->clk_src_mux);
	clk_src_mux.b.sram_clk_sel = SWITCH_CLK;
	clk_src_mux.b.sw_clk_src = PLL_CLK_0;
	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	div.b.nf0_clk_sel = SWITCH_CLK;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	// set div and src
	div.b.nf0_clk_div = DIVIDE_2;
	div.b.ecc_clk_div = DIVIDE_4;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	clk_src_mux.b.ecc_clk_src = PLL_CLK_1;
	clk_src_mux.b.nf0_clk_src = PLL_CLK_1;
	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	// set pll freq
#if !defined(FPGA)
	PLLN_CTRL_0_SET(1, p->postdiv1, p->fbdiv);
#endif
	// switch back to div output
	div.b.nf0_clk_sel = DIVIDER_OUTPUT;
	div.b.ecc_clk_sel = DIVIDER_OUTPUT;
	writel(div.all, &misc_reg->clk_divider);
	(void) readl(&misc_reg->clk_divider);

	clk_src_mux.b.sram_clk_sel = DIVIDER_OUTPUT;
	writel(clk_src_mux.all, &misc_reg->clk_src_mux);
	(void) readl(&misc_reg->clk_src_mux);

	return p->freq;
}

/*!
 * @brief callback function of misc when pmu resume
 *
 * @param mode		resume from sleep mode
 *
 * @return		always return true
 */
ps_code static bool misc_suspend(enum sleep_mode_t mode)
{
	extern bool uart_suspend(enum sleep_mode_t mode);
	uart_suspend(mode);
	cpu_clk_init(CPU_CLK_333_MHZ);
	return true;
}

/*!
 * @brief callback function when rtos misc module resume
 *
 * @param mode		resume from sleep mode
 *
 * @return		not used
 */
ps_code void misc_resume(enum sleep_mode_t mode)
{
	extern void uart_resume(enum sleep_mode_t mode);
	uart_resume(mode);

	if (mode == SUSPEND_ABORT)
		return;

	if (mode == SLEEP_MODE_PS3_L1 || mode == SLEEP_MODE_PS3_L12) {
		saved_tsc = get_tsc_64();
		elapsed = 0;
		return;
	}

	pic_init();
#if CPU_ID == 1
	cpu_clk_init(CPU_CLK_666_MHZ);
#endif

	timer4_ctrl_t ctrl;

	ctrl.all = 0;
	ctrl.b.timer4_reset = 1;
	timer_writel(ctrl.all, TIMER4_CTRL);

	ctrl.b.timer4_reset = 0;
	ctrl.b.timer4_start = 1;
	timer_writel(ctrl.all, TIMER4_CTRL);

	shared_timer1_ctrl_t shared_timer1_ctrl;
	shared_timer1_ctrl.all = 0;
	shared_timer1_ctrl.b.shared_timer1_reset = 1;
	misc_writel(shared_timer1_ctrl.all, SHARED_TIMER1_CTRL);

	shared_timer1_ctrl.b.shared_timer1_reset = 0;
	shared_timer1_ctrl.b.shared_timer1_start = 1;
	misc_writel(shared_timer1_ctrl.all, SHARED_TIMER1_CTRL);

#ifndef TIMER_VALIDATE
	/* Timer 0 for ticker */
	{
		timer0_ctrl_t timer0_ctrlr;
		timer0_value_t timer0_value;
#ifndef FPGA
		/* pre-scale based for precise cycle */
		#define CYCLE_PER_SCALE  (TIMER0_CYCLE_PER_TICK >> 16)
		#define CALIBRATIION_VAL (TIMER0_CYCLE_PER_TICK / (CYCLE_PER_SCALE + 1) - 1)
#else
		/* timer value based */
		#define CALIBRATIION_VAL 65535
		#define CYCLE_PER_SCALE  ((TIMER0_CYCLE_PER_TICK / (CALIBRATIION_VAL + 1)) - 1)
#endif

		/* toggle */
		timer0_ctrlr.all = 0;
		timer_writel(timer0_ctrlr.all, TIMER0_CTRL);

		/* set count down value */
		timer0_value.all = 0;
		timer0_value.b.timer0_cur_val = (u16) CALIBRATIION_VAL;
		timer0_value.b.timer0_rel_val = (u16) CALIBRATIION_VAL;
		timer_writel(timer0_value.all, TIMER0_VALUE);

		/* set control */
		timer0_ctrlr.all = 0;
		timer0_ctrlr.b.timer0_start = 1;
		timer0_ctrlr.b.timer0_int_en = 1;
		timer0_ctrlr.b.timer0_load = TIMER_VAL_CURRENT;
		timer0_ctrlr.b.timer0_mode = TIMER_MODE_CONTIG;
		timer0_ctrlr.b.timer0_prescale = CYCLE_PER_SCALE;

		timer_handler[0] = tick_handler;

		saved_tsc = get_tsc_64();
		elapsed = 0;
		timer_writel(timer0_ctrlr.all, TIMER0_CTRL);
	}
#endif
	poll_mask(VID_TIMER);
	pic_mask(VID_SYSTEM_MISC);
#if 0
	poll_mask(VID_PWR_LOSS_ASSERT);
#endif

#if defined(RAID_ERR_ISR)
	misc_sys_isr_enable(SYS_VID_RAIDM_ERR);
#endif
#if !defined(FPGA) && CPU_ID == 1
	misc_sys_isr_enable(SYS_VID_PRESET_DEASSERT);
	//misc_sys_isr_enable(SYS_VID_PWR_LOSS);
#endif
#if 0
	poll_mask(VID_PERIPHERAL);
#endif
}

#if defined(PROGRAMMER)
static u32 inline nvme_readl(int reg)
{
	return readl((void *)(NVME_BASE + reg));
}

static void inline nvme_writel(u32 data, int reg)
{
	writel(data, (void *)(NVME_BASE + reg));
}
#endif

/*!
 * @brief initialization of misc module
 *
 * @note must be in tcm, due to reset bm
 *
 * @return		not used
 */
static fast_code void misc_init_must_be_in_tcm(void)
{
#if !defined(FPGA) && CPU_ID == 1
	nf_clk_init(NF_CLK_DEFAULT);
	cpu_clk_init(CPU_CLK_666_MHZ);
	_SYS_CLK = cur_cpu_clk_freq * (1000 * 1000) / 2;
#endif

	{
#if defined(PROGRAMMER)
		if (nvme_readl(0x2008) & BIT(31)) {
			extern void hal_nvmet_suspend_cmd_fetch(void);
			hal_nvmet_suspend_cmd_fetch();
			misc_set_warm_boot_flag(NULL, 0);
		}
#endif
	}
#if defined(MPC)
#if CPU_ID == CPU_FE
	if (misc_is_warm_boot() == false)
		misc_reset(RESET_NVME);
	misc_reset(RESET_BM);
	misc_reset(RESET_NCB);

#endif
#else // else MPC
#if !defined(RAMDISK)
	misc_reset(RESET_NCB);
#endif

#if !defined(PROGRAMMER)
	if (misc_is_warm_boot() == false)
		misc_reset(RESET_NVME);
#endif

	misc_reset(RESET_BM);
#endif // end if MPC

	poll_irq_register(VID_TIMER, timer_isr);
	irq_register(VID_SYSTEM_MISC, sys_misc_isr);
	poll_irq_register(VID_SYSTEM_MISC, poll_sys_misc_isr);
	misc_writel(~0, SYS_INT_MASK_REG);		// disable all first

	/* only in asic */
#if !defined(FPGA) && CPU_ID == 1
#if (POWER_ISR == mENABLE)
	sirq_register(SYS_VID_PWR_LOSS, power_isr, false);
#endif
#endif

	vol_mon_ctrl_t vol_ctrl;
	vol_ctrl.all = misc_readl(VOL_MON_CTRL);
	vol_ctrl.b.power_loss_int_en = POWER_LOSS_INT_EN_MASK >> POWER_LOSS_INT_EN_SHIFT;
	vol_ctrl.b.power_up_int_en = POWER_UP_INT_EN_MASK >> POWER_UP_INT_EN_SHIFT;
	misc_writel(vol_ctrl.all, VOL_MON_CTRL);
	INIT_LIST_HEAD(&power_timer.entry);
	power_timer.function = power_timer_handling;
	power_timer.data = "power_timer";
#ifdef SUPPORT_WDT
	wdt_init();
#endif

#if defined(RAID_ERR_ISR)
	raid_err_init();
#endif

	misc_resume(SUSPEND_INIT);

#if CPU_ID == 1
#ifdef FPGA
	if (soc_cfg_reg2.b.nvm_en)
		pcie_init();
#else
	pcie_init();
	pmu_init();
	extern void xmodem_init();
	xmodem_init();
#endif
    gpio_init();
#endif

	pmu_wakeup_timer_set(5);
	pmu_register_handler(SUSPEND_COOKIE_SYSTEM, misc_suspend,
			RESUME_COOKIE_PLATFORM, misc_resume);


#if CPU_ID == 1
	/* Iniialize SMBus 1 in Master mode */
	smb_master_init(20);


	/* Initialize SMBus 0 in Slave mode to support
	 * NVME MI Basic Management commands */
	extern void smb_i2c_slv_mode_9_init(void);
	smb_slave_init(20);
	//smb_setup_nvme_mi_basic_data();
	smb_i2c_slv_mode_9_init();
    misc_chk_otp_deep_stdby_mode(); //_Benson_20211008
#endif

	vol_mon_stts_t vol_stts;
	vol_stts.all = misc_readl(VOL_MON_STTS);
	misc_writel(vol_stts.all, VOL_MON_STTS);
    //misc_set_otp_deep_stdby_mode(); //_GENE_20210928
}


slow_code UNUSED bool is_rainier_a1(void)
{
    chip_id_t chip_id = {
        .all = misc_readl(CHIP_ID),
    };

    if (chip_id.all == 0x52360003){
        //printk("Rainier A1 SoC,  ChipID: 0x%x\n", chip_id.all);
        rtos_misc_trace(LOG_INFO, 0x6d7a, "Rainier A1 SoC, ChipID: 0x%x", chip_id.all);
        return true;
    }
    //printk("Rainier A0 SoC,  ChipID: 0x%x\n", chip_id.all);
    rtos_misc_trace(LOG_INFO, 0x2ee0, "Rainier A0 SoC, ChipID: 0x%x", chip_id.all);
    return false;
}

extern smb_registers_regs_t *smb_slv;
extern u8 mi_cmd0_g[32];
extern u16 Temp_CMD0_Update;
fast_data u16 xfer_offset = 0;
fast_data u16 data_offset = 0;
extern void smb_static_data_FF(void);
extern void mi_basic_cmd0_update(u16 temp_data);
ddr_code void smb0_isr(void)
{
//log_isr = LOG_IRQ_DO;
smb_intr_sts_t sts = { .all = readl(&smb_slv->smb_intr_sts), };// Read 0xC0052000
smb_intr_sts_t ack = { .all = 0 , };
smb_dev_stc_data_1_t smb_dev_stc_data_1;
smb_dev_stc_data_2_t smb_dev_stc_data_2;
smb_dev_stc_data_3_t smb_dev_stc_data_3;
smb_dev_stc_data_4_t smb_dev_stc_data_4;
smb_pasv_0_t smb_pasv_0 = { .all = readl(&smb_slv->smb_pasv_0 ), };
//smb_control_register_t control = { .all = readl(&smb_slv->smb_control_register), };
    //rtos_core_trace(LOG_ERR, 0, "ab:%d lost:%d done:%d pause:%d xfer:%d",  sts.b.slv_tran_done_abnml_sts, sts.b.arb_lost_sts, sts.b.slv_tran_done_nml_sts, sts.b.i2c_pause_sts, xfer_offset);
    //rtos_core_trace(LOG_ERR, 0, "cmd:%x w:%x r:%x",  smb_pasv_0.b.smb_pasv_cmd_code, smb_pasv_0.b.smb_pasv_addr_w, smb_pasv_0.b.smb_pasv_addr_r);
	if (sts.b.slv_tran_done_abnml_sts || sts.b.arb_lost_sts || sts.b.slv_tran_done_nml_sts) { // Transfer is Done. Clear INTR
		xfer_offset = 0;
		data_offset = 0;
		ack.b.slv_tran_done_abnml_sts = 1;
		ack.b.arb_lost_sts = 1;
		ack.b.slv_tran_done_nml_sts = 1;
	}
#if defined(A1_DC_WA)
		/* Handle slv_tran_done_nml_sts[3] interrupt.
		 * 256 bytes Transfer done in sixteen 16 bytes. */
		if (sts.b.slv_tran_done_nml_sts) {
			if (xfer_offset > 15) {
				xfer_offset = 0;
				data_offset = 0;
			}
			ack.b.slv_tran_done_nml_sts = 1;
		}
#endif
	if (sts.b.i2c_pause_sts) { // Wait INTR of i2c_pause_sts(0xC0052000[16])
			ack.b.i2c_pause_sts = 1; // W1C
			#if 0
			/*
			if (smb_pasv_0.b.smb_pasv_addr_w == 0xA6 && sts.b.arb_lost_sts == 0) {
				extern u32 VPD_blk_write(u8 cmd_code, u8 *vpd);
				smb_dev_stc_data_1_t vpd_data[4];
				VPD_blk_write(xfer_offset+1, (u8 *)&vpd_data);
				smb_dev_stc_data_1.all = vpd_data[0].all;
				smb_dev_stc_data_2.all = vpd_data[1].all;
				smb_dev_stc_data_3.all = vpd_data[2].all;
				smb_dev_stc_data_4.all = vpd_data[3].all;
				writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
				writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
				writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
				writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
				xfer_offset++;
				if (xfer_offset == 16) {
					control.all = readl(&smb_slv->smb_control_register);
					control.b.smb_sw_rst = 1;
					writel(control.all, &smb_slv->smb_control_register);
					control.b.smb_sw_rst = 0;
					writel(control.all, &smb_slv->smb_control_register);
					smb_static_data_FF();
					xfer_offset = 0;
				}
			} else */if ((xfer_offset == 0 && smb_pasv_0.b.smb_pasv_addr_w != 0xD4)||
				(smb_pasv_0.b.smb_pasv_addr_w == 0xD4 && smb_pasv_0.b.smb_pasv_cmd_code == 0x20)) {// first time, need check 0xC0052100[7:0] = 0xD4 or not
				data_offset = 0;
				smb_static_data_FF();
			} else {
				if (xfer_offset == 0) {
				    mi_basic_cmd0_update(Temp_CMD0_Update - 273);
 				}
				if (data_offset < 32 && smb_pasv_0.b.smb_pasv_cmd_code == 0x00) {
					smb_dev_stc_data_1.b.smb_dev_stc_data01 = mi_cmd0_g[data_offset + 0];
					smb_dev_stc_data_1.b.smb_dev_stc_data02 = mi_cmd0_g[data_offset + 1];
					smb_dev_stc_data_1.b.smb_dev_stc_data03 = mi_cmd0_g[data_offset + 2];
					smb_dev_stc_data_1.b.smb_dev_stc_data04 = mi_cmd0_g[data_offset + 3];
					writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
					smb_dev_stc_data_2.b.smb_dev_stc_data05 = mi_cmd0_g[data_offset + 4];
					smb_dev_stc_data_2.b.smb_dev_stc_data06 = mi_cmd0_g[data_offset + 5];
					smb_dev_stc_data_2.b.smb_dev_stc_data07 = mi_cmd0_g[data_offset + 6];
					smb_dev_stc_data_2.b.smb_dev_stc_data08 = mi_cmd0_g[data_offset + 7];
					writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
					smb_dev_stc_data_3.b.smb_dev_stc_data09 = mi_cmd0_g[data_offset + 8];
					smb_dev_stc_data_3.b.smb_dev_stc_data10 = mi_cmd0_g[data_offset + 9];
					smb_dev_stc_data_3.b.smb_dev_stc_data11 = mi_cmd0_g[data_offset + 10];
					smb_dev_stc_data_3.b.smb_dev_stc_data12 = mi_cmd0_g[data_offset + 11];
					writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
					smb_dev_stc_data_4.b.smb_dev_stc_data13 = mi_cmd0_g[data_offset + 12];
					smb_dev_stc_data_4.b.smb_dev_stc_data14 = mi_cmd0_g[data_offset + 13];
					smb_dev_stc_data_4.b.smb_dev_stc_data15 = mi_cmd0_g[data_offset + 14];
					smb_dev_stc_data_4.b.smb_dev_stc_data16 = mi_cmd0_g[data_offset + 15];
					writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
					data_offset = data_offset + 16;
					xfer_offset++;
				}
				else if (data_offset < 32 && smb_pasv_0.b.smb_pasv_cmd_code == 0x08) {
					if(xfer_offset==0){
						smb_dev_stc_data_1.b.smb_dev_stc_data01 = mi_cmd0_g[8];
						smb_dev_stc_data_1.b.smb_dev_stc_data02 = mi_cmd0_g[9];
						smb_dev_stc_data_1.b.smb_dev_stc_data03 = mi_cmd0_g[10];
						smb_dev_stc_data_1.b.smb_dev_stc_data04 = mi_cmd0_g[11];
						writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
						smb_dev_stc_data_2.b.smb_dev_stc_data05 = mi_cmd0_g[12];
						smb_dev_stc_data_2.b.smb_dev_stc_data06 = mi_cmd0_g[13];
						smb_dev_stc_data_2.b.smb_dev_stc_data07 = mi_cmd0_g[14];
						smb_dev_stc_data_2.b.smb_dev_stc_data08 = mi_cmd0_g[15];
						writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
						smb_dev_stc_data_3.b.smb_dev_stc_data09 = mi_cmd0_g[16];
						smb_dev_stc_data_3.b.smb_dev_stc_data10 = mi_cmd0_g[17];
						smb_dev_stc_data_3.b.smb_dev_stc_data11 = mi_cmd0_g[18];
						smb_dev_stc_data_3.b.smb_dev_stc_data12 = mi_cmd0_g[19];
						writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
						smb_dev_stc_data_4.b.smb_dev_stc_data13 = mi_cmd0_g[20];
						smb_dev_stc_data_4.b.smb_dev_stc_data14 = mi_cmd0_g[21];
						smb_dev_stc_data_4.b.smb_dev_stc_data15 = mi_cmd0_g[22];
						smb_dev_stc_data_4.b.smb_dev_stc_data16 = mi_cmd0_g[23];
						writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
						data_offset = data_offset + 16;
						xfer_offset++;
						}
					else if(xfer_offset==1){
						smb_dev_stc_data_1.b.smb_dev_stc_data01 = mi_cmd0_g[24];
						smb_dev_stc_data_1.b.smb_dev_stc_data02 = mi_cmd0_g[25];
						smb_dev_stc_data_1.b.smb_dev_stc_data03 = mi_cmd0_g[26];
						smb_dev_stc_data_1.b.smb_dev_stc_data04 = mi_cmd0_g[27];
						writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
						smb_dev_stc_data_2.b.smb_dev_stc_data05 = mi_cmd0_g[28];
						smb_dev_stc_data_2.b.smb_dev_stc_data06 = mi_cmd0_g[29];
						smb_dev_stc_data_2.b.smb_dev_stc_data07 = mi_cmd0_g[30];
						smb_dev_stc_data_2.b.smb_dev_stc_data08 = mi_cmd0_g[31];
						writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
						smb_dev_stc_data_3.all = ~0;
						writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
						smb_dev_stc_data_4.all = ~0;
						writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
						data_offset = data_offset + 16;
						}
				}
#if defined(A1_DC_WA)
				else {
				smb_dev_stc_data_1.all = ~0;
				writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);

				smb_dev_stc_data_2.all = ~0;
				writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);

				smb_dev_stc_data_3.all = ~0;
				writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);

				smb_dev_stc_data_4.all = ~0;
				writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
				xfer_offset++;
				}
#endif

			}
			#else
			struct {
				    u8 *data;
				} smb_data[] = {
				    {(u8*)&smb_dev_stc_data_1},
				    {(u8*)&smb_dev_stc_data_1 + 1},
				    {(u8*)&smb_dev_stc_data_1 + 2},
				    {(u8*)&smb_dev_stc_data_1 + 3},
				    {(u8*)&smb_dev_stc_data_2},
				    {(u8*)&smb_dev_stc_data_2 + 1},
				    {(u8*)&smb_dev_stc_data_2 + 2},
				    {(u8*)&smb_dev_stc_data_2 + 3},
				    {(u8*)&smb_dev_stc_data_3},
				    {(u8*)&smb_dev_stc_data_3 + 1},
				    {(u8*)&smb_dev_stc_data_3 + 2},
				    {(u8*)&smb_dev_stc_data_3 + 3},
				    {(u8*)&smb_dev_stc_data_4},
				    {(u8*)&smb_dev_stc_data_4 + 1},
				    {(u8*)&smb_dev_stc_data_4 + 2},
				    {(u8*)&smb_dev_stc_data_4 + 3}
				};
			/*
			if (smb_pasv_0.b.smb_pasv_addr_w == 0xA6 && sts.b.arb_lost_sts == 0) {
				extern u32 VPD_blk_write(u8 cmd_code, u8 *vpd);
				smb_dev_stc_data_1_t vpd_data[4];
				VPD_blk_write(xfer_offset+1, (u8 *)&vpd_data);
				smb_dev_stc_data_1.all = vpd_data[0].all;
				smb_dev_stc_data_2.all = vpd_data[1].all;
				smb_dev_stc_data_3.all = vpd_data[2].all;
				smb_dev_stc_data_4.all = vpd_data[3].all;
				writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
				writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
				writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
				writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
				xfer_offset++;
				if (xfer_offset == 16) {
					control.all = readl(&smb_slv->smb_control_register);
					control.b.smb_sw_rst = 1;
					writel(control.all, &smb_slv->smb_control_register);
					control.b.smb_sw_rst = 0;
					writel(control.all, &smb_slv->smb_control_register);
					smb_static_data_FF();
					xfer_offset = 0;
				}
			} else */if (xfer_offset == 0 && smb_pasv_0.b.smb_pasv_addr_w != 0xD4) {// first time, need check 0xC0052100[7:0] = 0xD4 or not
				data_offset = 0;
				smb_static_data_FF();
			} else {
				if (xfer_offset == 0) {
				    mi_basic_cmd0_update(Temp_CMD0_Update - 273);
 				}
				if (data_offset < 256  && smb_pasv_0.b.smb_pasv_cmd_code < 256) {
					for (u8 i = 0; i < sizeof(smb_data) / sizeof(smb_data[0]); i++) {
					    u16 index = smb_pasv_0.b.smb_pasv_cmd_code + i + data_offset;
					    if (index > 255) {
				            *smb_data[i].data = ~0;
				        } else {
				            *smb_data[i].data = mi_cmd0_g[index];
				        }
					}
					writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
					writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
					writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
					writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
					data_offset = data_offset + 16;
					xfer_offset++;
				}
#if defined(A1_DC_WA)
				else {
					smb_dev_stc_data_1.all = ~0;
					writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);

					smb_dev_stc_data_2.all = ~0;
					writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);

					smb_dev_stc_data_3.all = ~0;
					writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);

					smb_dev_stc_data_4.all = ~0;
					writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
					xfer_offset++;
				}
#endif

			}
			#endif
		}
    //rtos_core_trace(LOG_ERR, 0, "1:%x 2:%x 3:%x 4:%x", smb_dev_stc_data_1.all, smb_dev_stc_data_2.all, smb_dev_stc_data_3.all, smb_dev_stc_data_4.all);
    //rtos_core_trace(LOG_ERR, 0, "1:%x 2:%x 3:%x 4:%x", readl(&smb_slv->smb_dev_stc_data_1), readl(&smb_slv->smb_dev_stc_data_2),readl(&smb_slv->smb_dev_stc_data_3),readl(&smb_slv->smb_dev_stc_data_4));
	writel(ack.all, &smb_slv->smb_intr_sts);
	//log_isr = LOG_IRQ_DOWN;
}

extern volatile u8 plp_trigger;
extern u8 plp_PWRDIS_flag;
extern void plp_iic_read_write(u8 slaveID, u8 cmd_code, u8 *value, u8 data, bool rw);
fast_code void write_plp_ic(void)
{
    u8 data = 0x39; //ENA 0->1
    plp_iic_read_write(0xB4, 0x1, NULL, data, 0);
    //plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, NULL, data, write);
    /*data = 0;  
    plp_iic_read_write(0xB4, 0x1, &data, 0, 1); 
    rtos_core_trace(LOG_ALW, 0xc0b7,"data 0x%x ",data);*/

}

fast_data u32 _record_lr =~0;
extern volatile u8 shr_lock_power_on;
extern void btn_de_wr_enable(void);
extern volatile bool shr_shutdownflag;
extern volatile u8 plp_trigger;
extern volatile u32 plp_log_number_start;
extern volatile u32 plp_record_cpu1_lr;

fast_code void gpio_isr(void)
{
	//log_isr = LOG_IRQ_DO;
    gpio_int_t gpio_int_status = {
        .all = misc_readl(GPIO_INT),
    };
#if(PLP_SUPPORT == 1)
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)  

    if (((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))
        ||(gpio_int_status.b.gpio_int_48 & (1 << GPIO_POWER_DISABLE_SHIFT)))
        && PWRDIS_open_flag) 
    {
        write_plp_ic();//write plp ic en 1
    }
#endif
#endif 

    rtos_core_trace(LOG_INFO, 0xba97, "GPIO int status 0x%x", gpio_int_status.all);
    rtos_core_trace(LOG_INFO, 0x8857, "R14 0x%x", _record_lr);
    extern u32 log_number;
	plp_record_cpu1_lr = _record_lr;
	plp_log_number_start = log_number;

#if 0//(MDOT2_SUPPORT == 1) 
    if(PLP_IC_SGM41664)
    {   
        u8 data_f,data_10;
        data_f=data_10=0;
        plp_iic_read_write(0xB4, 0xF, &data_f, 0, 1);
        plp_iic_read_write(0xB4, 0x10, &data_10, 0, 1);
        SGM_data = data_f <<8 | data_10;
        rtos_core_trace(LOG_INFO, 0xc76f, "isr SGM4166 FLAG 0x%x Sys_control 0x%x  data 0x%x",data_f,data_10,SGM_data);
    }
#endif

#if(PLP_SUPPORT == 1)

	extern void plp_detect(void);

    if (gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT)) {
        //rtos_misc_trace(LOG_ERR, 0, "plp detect\n");
#ifdef edevx_test
		disable_ltssm(0);
#endif
        plp_detect();
        //plp_force_flush();
        misc_writel((1 << GPIO_PLP_DETECT_SHIFT), GPIO_INT);
    }
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
	else if (gpio_int_status.b.gpio_int_48 & (1 << GPIO_POWER_DISABLE_SHIFT)) {
        if(PWRDIS_open_flag&&!plp_trigger){
    		extern u8 plp_PWRDIS_flag;
    		if(plp_PWRDIS_flag != 0){
    			rtos_misc_trace(LOG_ERR, 0xd8e3, "run plp flow now\n");
    			misc_writel(gpio_int_status.all, GPIO_INT);

    			return;
    		}
    		plp_PWRDIS_flag = 1;
    		// extern u8 evt_plp_set_ENA;
    		// evt_set_cs(evt_plp_set_ENA, PLP_ENA_ENABLE, 0, CS_NOW); // default 1 -> 1, can ignore
    		plp_detect();
        }
    }
#endif
     else

#endif
     if(gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLN_SHIFT)){
        if((shr_lock_power_on == 0)&&(!plp_trigger)&&(!shr_shutdownflag)&&(PLN_open_flag)){

            udelay(5000);
            if(PLN_evt_trigger == false){
                PLN_evt_trigger = true;
                pln_loop_time = get_tsc_64();
            }
       }
    }
    else {
        rtos_core_trace(LOG_INFO, 0xaedf, "not support");

    }
    misc_writel(gpio_int_status.all, GPIO_INT);
	//log_isr = LOG_IRQ_DOWN;

}

#if 1//(EN_PLP_FEATURE == FEATURE_SUPPORTED)
ddr_code void pln_evt()
{
    u32 gpio_input = 0, gpio0_input = 0;
    u32 PLA;

    PLA = readl((void *)(MISC_BASE + GPIO_OUT));
    gpio_input = readl((void *)(MISC_BASE + GPIO_PAD_CTRL));
    gpio0_input = (gpio_input >> (GPIO_OUT_SHIFT + GPIO_PLN_SHIFT)) & 0x1;

    if(PLN_FORMAT_SANITIZE_FLAG == true){
        PLN_GPIOISR_FORMAT_SANITIZE_FLAG = true;
    }
    if((PLA_FLAG == true)&&(PLN_FLAG == true)&&(gpio0_input == 1)&&(PLN_FORMAT_SANITIZE_FLAG == false)&&(PLN_in_low == false)){
        pln_loop_time = 0;
        PLN_evt_trigger = false;
        PLN_keep_50ms = false;
    }
    if((PLA_FLAG == true)&&(PLN_FLAG == true)&&(gpio0_input == 0)&&(PLN_FORMAT_SANITIZE_FLAG == false)&&(PLN_keep_50ms == true)&&(PLN_in_low == false)){
        writel((PLA | BIT(GPIO_PLA_SHIFT + GPIO_OUT_SHIFT)) | BIT(GPIO_PLA_SHIFT) , (void *)(MISC_BASE + GPIO_OUT));
        rtos_misc_trace(LOG_INFO, 0x4f14, "isr pln low 0x%x pla low",gpio_input);
        PLN_FLAG = false;
        PLA_FLAG = false;
        PLN_keep_50ms = false;
        PLN_in_low = true;
        PLN_flush_cache_end = true;
        PLN_evt_trigger = false;
        nvmet_io_fetch_ctrl(true); //disable fetch io
        btn_de_wr_disable();
        pln_flush();
        //cpu_msg_issue(CPU_BE - 1, CPU_MSG_FPLP_TRIGGER, 0, 0);
    }
}
#endif



fast_code void rdisk_pln_format_sanitize(u32 p0, u32 p1, u32 p2)
{
    rtos_misc_trace(LOG_INFO, 0x7f7a, "[IN]rdisk_pln_format_sanitize, pln in");

    u32 PLA;
    PLA = readl((void *)(MISC_BASE + GPIO_OUT));

    writel((PLA | BIT(GPIO_PLA_SHIFT + GPIO_OUT_SHIFT)) | BIT(GPIO_PLA_SHIFT) , (void *)(MISC_BASE + GPIO_OUT));
    PLN_FLAG = false;
    PLA_FLAG = false;
    nvmet_io_fetch_ctrl(true); //disable fetch io
    btn_de_wr_disable();
    pln_flush();
    //cpu_msg_issue(CPU_BE - 1, CPU_MSG_FPLP_TRIGGER, 0, 0);

    return ;
}

ddr_code void gpio_set_gpio0(u32 value)
{
    gpio_out_t gpio_out = {
        .all = misc_readl(GPIO_OUT),
    };

    gpio_out.b.gpio_oe |= (1 << GPIO_POWER_DISABLE_SHIFT);
    if (value) {
        gpio_out.b.gpio_out |= (1 << GPIO_POWER_DISABLE_SHIFT);
    } else {
        gpio_out.b.gpio_out &= (~(1 << GPIO_POWER_DISABLE_SHIFT));
    }

    misc_writel(gpio_out.all,GPIO_OUT);
}

fast_code void gpio_set_gpio15(u32 value)
{
    gpio_out_t gpio_out = {
		.all = misc_readl(GPIO_OUT),
	};

    gpio_out.b.gpio_oe |= (1 << GPIO_PLP_DEBUG_SHIFT);
    if (value) {
        gpio_out.b.gpio_out |= (1 << GPIO_PLP_DEBUG_SHIFT);
    } else {
        gpio_out.b.gpio_out &= (~(1 << GPIO_PLP_DEBUG_SHIFT));
    }

    misc_writel(gpio_out.all,GPIO_OUT);
}

fast_code bool gpio_get_value(u32 gpio_ofst)
{
    gpio_pad_ctrl_t gpio_ctr = {
		.all = misc_readl(GPIO_PAD_CTRL),
	};

    return (gpio_ctr.b.gpio_in & (1 << gpio_ofst)) ? true : false;
}

//plp gpio init
fast_code void gpio_init(void)
{
    /*
    Read RC0040130h [31:16] (gpio_in [15:0])
    Write 1 to RC004003Ch [16] and write the gpio_in [15:0] to RC004003Ch [15:0].
    Then GPIO0 a��?GPIO15 can be used for other purpose.
    To use GPIO as output, FW needs to program RC0040040h.  - Bits [15:0] is GPIO output enable. Need to set the
corresponding bit to 1. - Bits [31:16] is
    GPIO output. Set the corresponding bit to 0 or 1.
    RC0040134 Bits [31:16] is GPIO pull-up or pull_down resistor enable.
    Need to set the corresponding bit to 0 to disable the pull-down or pull-up for each GPIO pin
    RC0040130 Bits [15:0] is GPIO pull status control. 0 is pull_down and 1 is pull_u
    */

    //read RC0040130h
    gpio_pad_ctrl_t gpio_ctr = {
		.all = misc_readl(GPIO_PAD_CTRL),
	};
    rtos_misc_trace(LOG_ERR, 0x0519, "GPIO in value %x\n", gpio_ctr.b.gpio_in);
    gpio_ctrl_t ctrl = {
		.all = misc_readl(GPIO_CTRL),
	};
    ctrl.b.gpio_reg_value = gpio_ctr.b.gpio_in;
    misc_writel(ctrl.all,GPIO_CTRL);

    //ctrl.b.gpio_reg_value = (1 << GPIO_PLP_DETECT_SHIFT) | (1 << GPIO_PLP_DEBUG_SHIFT);
    ctrl.b.gpio_reg_value |= (1 << GPIO_PLP_DETECT_SHIFT) | (1 << GPIO_PLP_STRPG_SHIFT);
    ctrl.b.gpio_reg_value |= (1 << GPIO_PLN_SHIFT);//PLN
    ctrl.b.gpio_reg_value |= (1 << GPIO_PLA_SHIFT);//PLA
    misc_writel(ctrl.all,GPIO_CTRL);

    //ctrl.all = 0;
    ctrl.b.gpio_reg_en = 1;
    misc_writel(ctrl.all,GPIO_CTRL);
    //set gpio 15 as output
    //gpio_set_gpio15(0);


    gpio_int_mode_t int_mode = {
		.all = misc_readl(GPIO_INT_MODE),
	};
    //int_mode.all = 0;
    int_mode.all |= 1 << (GPIO_PLP_DETECT_SHIFT<<1); //0h: rising edge 1h:falling edge 2h:input high  3h: input low bit6
    int_mode.b.gpio0_int_mode = 0;// POWER DISABLE 0
    int_mode.b.gpio1_int_mode = 1;// pln high->low trigger isr
    misc_writel(int_mode.all, GPIO_INT_MODE);

    rtos_misc_trace(LOG_ERR, 0x0c12, "GPIO init mode 0x%x\n", int_mode.all);
    gpio_int_ctrl_t int_ctrl = {
		.all = misc_readl(GPIO_INT_CTRL),
	};
    //int_ctrl.all = 0;
    int_ctrl.b.gpio_int_en |= 1 << GPIO_PLP_DETECT_SHIFT;
	int_ctrl.b.gpio_int_en |= 1 << GPIO_POWER_DISABLE_SHIFT; //power disable function
    int_ctrl.b.gpio_int_en |= 1 << GPIO_PLN_SHIFT;//PLN
    misc_writel(int_ctrl.all, GPIO_INT_CTRL);
    rtos_misc_trace(LOG_ERR, 0x3dea, "GPIO int status 0x%x\n", misc_readl(GPIO_INT));

    PLA_FLAG = true;
    PLN_FLAG = true;
    PLN_evt_trigger = false;
    PLN_in_low = false;

    sirq_register(SYS_VID_GPIO, gpio_isr, false);
	//misc_sys_isr_enable(SYS_VID_GPIO);

    u32 PLA = readl((void *)(MISC_BASE + GPIO_OUT));
    //rtos_misc_trace(LOG_INFO, 0x3d7b, "PLA 0x%x",PLA);
    writel((PLA | BIT(GPIO_PLA_SHIFT)), (void *)(MISC_BASE + GPIO_OUT));

    u32 gpio_int = misc_readl(GPIO_INT);
    misc_writel(gpio_int, GPIO_INT); //W1C
    //rtos_misc_trace(LOG_ERR, 0x23fc, "GPIO int status 0x%x\n", misc_readl(GPIO_INT));

    pad_ctrl_t pda_ctrl = {
		.all = misc_readl(PAD_CTRL),
	};
    pda_ctrl.b.gpio_ie = 1;  //enable gpio input
    misc_writel(pda_ctrl.all, PAD_CTRL);
    //rtos_misc_trace(LOG_ERR, 0xa6f3, "PAD_CTRL 0x%x", misc_readl(PAD_CTRL));

    test_ctrl_t test_ctrl = {
		.all = misc_readl(TEST_CTRL),
	};

    //rtos_misc_trace(LOG_ERR, 0, "TEST_CTRL 0x%x",test_ctrl.all);

    test_ctrl.b.gpio_pull_enable = 0xfffd; //0xc0040134 gpio_pull_enable default FFFFh
    misc_writel(test_ctrl.all,TEST_CTRL);

}

ddr_code void GPIO_Set(void){


    test_ctrl_t test_ctrl = {
		.all = misc_readl(TEST_CTRL),
	};
    gpio_ctrl_t gpio_ctrl = {
       .all = misc_readl(GPIO_CTRL),
    };

    gpio_out_t gpio_out = {
       .all = misc_readl(GPIO_OUT),
    };

    gpio_pad_ctrl_t gpio_pad_ctrl = {
       .all = misc_readl(GPIO_PAD_CTRL),
    };

    test_ctrl.b.boot_debug_en = 0;
    misc_writel(test_ctrl.all,TEST_CTRL);

    gpio_ctrl.b.gpio_reg_value |= gpio_pad_ctrl.b.gpio_in;
    misc_writel(gpio_ctrl.all,GPIO_CTRL);       // Update registers
    gpio_ctrl.b.gpio_reg_value |= BIT(1);
    misc_writel(gpio_ctrl.all,GPIO_CTRL);       // Update registers

    gpio_ctrl.b.gpio_reg_en = 1;
    misc_writel(gpio_ctrl.all,GPIO_CTRL);

    gpio_out.b.gpio_oe |= BIT(1);
    gpio_out.b.gpio_out |= BIT(1);
    misc_writel(gpio_out.all,GPIO_OUT);

    rtos_misc_trace(LOG_ERR, 0xe9a2, "GPIO Set Done \n\n");

}


// static DEFINE_UART_CMD(gpio_set, "GPIO_Set",
	// "gpio",
	// "GPIO set",
	// 0, 0, GPIO_Set);

ddr_code UNUSED void gpio_dump(void)
{
#if 0//def FPGA
	return;
#endif
	gpio_pad_ctrl_t gpio_ctr = {
		.all = misc_readl(GPIO_PAD_CTRL),
	};

	rtos_core_trace(LOG_INFO, 0x6d40, "GPIO raw %x", gpio_ctr.b.gpio_in);
	if (gpio_ctr.b.gpio_in & BIT(5))
		rtos_core_trace(LOG_INFO, 0xfa5f, "GPIO[5]: Boot from ROM");
	if (gpio_ctr.b.gpio_in & BIT(6))
		rtos_core_trace(LOG_INFO, 0x1482, "GPIO[6]: IO 1.2v");

	rtos_core_trace(LOG_INFO, 0x391d, "GPIO[7:8]: PCIe x%d", ((gpio_ctr.b.gpio_in >> 7) & 0x3) << 1);
	if (gpio_ctr.b.gpio_in & BIT(9))
		rtos_core_trace(LOG_INFO, 0xa729, "GPIO[9]: write protection");
	if (gpio_ctr.b.gpio_in & BIT(11))
		rtos_core_trace(LOG_INFO, 0xc724, "GPIO[11]: recovery mode");
}

ps_code bool gpio_io_voltage_is_1_2v(void)
{
	return true;
#ifdef FPGA
	return false;// Current FPGA does not su
#endif
	gpio_pad_ctrl_t gpio_ctr = {
		.all = misc_readl(GPIO_PAD_CTRL),
	};
	return (bool)!!(gpio_ctr.b.gpio_in & BIT(6));
}
#if defined(SAVE_CDUMP)
static ddr_code int cpu_clk_main(int argc, char *argv[])//_GENE_20210601 ps_code
#else
static ps_code int cpu_clk_main(int argc, char *argv[])//_GENE_20210601 ps_code
#endif
{
	int cpu_freq, new_clk_freq;

	if (argc == 1) {
		rtos_misc_trace(LOG_ERR, 0x4ec5, "\ncpu_clk(%d MHz)", cur_cpu_clk_freq);
		return 0;
	}

	new_clk_freq = atoi(argv[1]);
	if (new_clk_freq >= ARRAY_SIZE(cpu_pll_settings) - 1) {
		rtos_misc_trace(LOG_ERR, 0x42c4, "\ncpu_clk: Error - wrong parameter(%d)", new_clk_freq);
		return -2;
	}

	new_clk_freq = cpu_pll_settings[new_clk_freq].freq;

	if (new_clk_freq == cur_cpu_clk_freq) {
		rtos_misc_trace(LOG_ERR, 0x71f7, "\nNo change: cpu_clk(Cur:%d MHz) = (New:%d MHz)", cur_cpu_clk_freq, new_clk_freq);
	} else {
		/* Change CPU clock and UART divisor frequencey */
		/* save current cpu frequency */
		int prev_freq = cur_cpu_clk_freq;
		cpu_freq = cpu_clk_init(new_clk_freq);
		set_uart_divisor(cpu_freq*(1000*1000)/2);
		rtos_misc_trace(LOG_ERR, 0xfdb6, "\ncpu_clk(%d MHz)->(%d MHz)", prev_freq, cur_cpu_clk_freq);
	}

	return 0;
}


ddr_code void showlog(const char *func, int line)
{
	evlog_printk(LOG_PANIC, "%s %d While_break WDT timeout\n", func, line);

#if CPU_ID == 1
	//btn C0010000 - C001088C 548
	u32 reg = 0xC0010000;
	rtos_misc_trace(LOG_ERR, 0xff38, "btn");
	for (int i = 0; i < 548; i++) {
		rtos_misc_trace(LOG_ERR, 0x43b2, "%x : (0x%x)", reg, readl((const void *)reg));
		reg += 4;
	}
	//pcie log
	rtos_misc_trace(LOG_ERR, 0x0212, "pcie");
	extern void pcie_dbglog(void);
	pcie_dbglog();
	//C00600168
	reg = 0xC0060168;
	rtos_misc_trace(LOG_ERR, 0x75f9, "%x : (0x%x)", reg, readl((const void *)reg));
	//C0060016C
	reg = 0xC006016C;
	rtos_misc_trace(LOG_ERR, 0x5870, "%x : (0x%x)", reg, readl((const void *)reg));
	//C0032028
	reg = 0xC0032028;
	rtos_misc_trace(LOG_ERR, 0x9344, "%x : (0x%x)", reg, readl((const void *)reg));
	//C0032014
	reg = 0xC0032014;
	rtos_misc_trace(LOG_ERR, 0x3211, "%x : (0x%x)", reg, readl((const void *)reg));
#endif
}


fast_code bool Chk_break(u64 time, const char *func, int line)
{
	if((time_elapsed_in_ms(time)) > 90000)  //90sec
	{
		showlog(func, line);
		flush_to_nand(EVT_while_break);
		return 1;
	}
	else
	{
		return 0;
	}
}

ddr_code void HalOtpValueConfirm(void)
{
	u32 val;

	val = readl((void *)(0xC0042100));

	if (val != 0xFFFFFFFF)
	{
		//TODO Flush event
		rtos_misc_trace(LOG_ERR, 0x4597, "[SOC] OTP val %x",val);
	}
}

#if defined(MPC) && CPU_ID == 1
ddr_code bool rtos_lock_other_cpu(bool btn_reset, bool wait)
{
	u32 i;
	u32 busy_cpu = 0;

	cpu_init_bmp[CPU_ID_0].b.btn_rst = 0;
	for (i = 0; i < MPC; i++)
	{
		if (i == CPU_ID_0)
			continue;
		cpu_init_bmp[i].b.wait_btn_rst = 0;
	}

	ipc_api_wait_btn_rst(CPU_ID_0 + 1, btn_reset);

	for (i = 0; i < MPC; i++) {
		if (i == CPU_ID_0)
			continue;

		if (cpu_init_bmp[i].b.wait_btn_rst)
			continue;

		if (wait == false) {
			busy_cpu++;
			continue;
		}

		while (cpu_init_bmp[i].b.wait_btn_rst == 0) {
			;
		}
	}

	return (busy_cpu == 0) ? true : false;
}

ddr_code void rtos_unlock_other_cpu(void)
{
	local_item_done(btn_rst);
}

ddr_code bool rtos_chk_other_cpu_locked(void)
{
	u32 i;
	u32 busy_cpu = 0;

	for (i = 0; i < MPC; i++) {
		if (i == CPU_ID_0)
			continue;

		if (cpu_init_bmp[i].b.wait_btn_rst == 0)
			busy_cpu++;
	}

	return (busy_cpu == 0) ? true : false;
}
#endif

init_code void raid_sram_init(void)
{
	raid_init_ctrl_t init_ctrl;
	init_ctrl.all = misc_readl(RAID_INIT_CTRL);

	init_ctrl.b.raidm_init_addr = 0;
	init_ctrl.b.raidm_init_size = 2144 * 8;
	init_ctrl.b.raidm_init_st = 1;

	misc_writel(init_ctrl.all, RAID_INIT_CTRL);

	do {
		init_ctrl.all = misc_readl(RAID_INIT_CTRL);
	} while (init_ctrl.b.raidm_init_done == 0);

	init_ctrl.all = misc_readl(RAID_INIT_CTRL);
	init_ctrl.b.raidm_init_st = 0;
	init_ctrl.b.raidm_init_done = 0;
	misc_writel(init_ctrl.all, RAID_INIT_CTRL);
}


#define CMD_PROC_BASE 0xC003C000

ps_code static int x_read_main(int argc, char *argv[])
{
//	char *endp;
//	u32 reg = strtoul(argv[1], &endp, 0);
//	rtos_misc_trace(LOG_ERR, 0, "\nR0x%x : (0x%x)", reg, readl((const void *)reg));
//	return 0;
	u32 reg, i, len;
	reg = strtoul(argv[1], (void *)0, 0);
	len = 4;
	if (argc > 2) {
		len = strtol(argv[2], (void *)0, 10);
		if ((len & 3) != 0) {
			len += 4;
			len &= ~3;
			rtos_misc_trace(LOG_ERR, 0x29bf, "Len not 4Byte alignment, Adjust len to:%d",len);
		}

	}
	for (i = 0; i < len; i += 4) {
		rtos_misc_trace(LOG_ERR, 0x0493, "Reg_0x%x : (0x%x)", reg + i, readl((const void *)reg + i));
	}
	return 0;
}

static DEFINE_UART_CMD(x_read, "x", "x [reg_addr] [len_byte]", "x 0xC0040000 3", 1, 2, x_read_main);

/*!
 * @brief console command: grpdump
 *
 * dump grouped register of BTN/CMD_PROC/L2P
 *
 * @param argc		argument should be 2
 * @param argv		should be btn, cmd_proc or l2p
 *
 * @return 		always return 0
 */
ps_code static int grpdump_main(int argc, char *argv[])
{
	u32 *addr = 0;
	u32 dw_cnt = 0;

	if (memcmp(argv[1], "btn", 4) == 0) {
		addr = (u32*)BM_BASE;
		dw_cnt = sizeof(btn_cmd_data_reg_regs_t) >> 2;
	} else if (memcmp(argv[1], "cmd_proc", 8) == 0) {
		addr = (u32*)CMD_PROC_BASE;
		dw_cnt = 508;
	} else if (memcmp(argv[1], "l2p", 4) == 0) {
		addr = (u32*)BTN_ME_BASE;
		dw_cnt = sizeof(btn_ftl_ncl_reg_regs_t) >> 2;
	} else {
		rtos_misc_trace(LOG_ERR, 0x036a, "\nwrong command format !!\n");
		rtos_misc_trace(LOG_ERR, 0x0570, "grpdump [btn/cmd_proc/l2p]\n");
	}


	rtos_misc_trace(LOG_ERR, 0x71cb, "\n");
	while (dw_cnt > 0) {
		if (dw_cnt < 4)
			dw_cnt = 4;

		rtos_misc_trace(LOG_ERR, 0x5b47, "R%08x: %08x %08x %08x %08x\n",
				addr, addr[0], addr[1], addr[2], addr[3]);
		addr += 4;
		dw_cnt -= 4;
	}

	return 0;
}

#if CPU_ID == 1
static ps_code int sys_time_ctrl(int argc, char *argv[])
{
	char *p=NULL;
	u8 ctrl_type = atoi(argv[1]);
	u32 input_timestamp = strtoul(argv[2], &p, 0);
	u64 timestamp;
	u32 high, low;

	if ((ctrl_type !=0) && (ctrl_type != 1)){
		rtos_misc_trace(LOG_ERR, 0xb483, "invalid parameter 1!\n");
		return 0;
	}

	if (ctrl_type == 0){
		timestamp = get_cur_sys_time();
		high = (u32)(timestamp >> 32);
		low  = (u32)timestamp;
		rtos_misc_trace(LOG_ERR, 0x6c29, "current sys time is high:%d[0x%x],low:%d[0x%x] ms.\n",high, high, low, low);
	}
	else
	{
		timestamp = input_timestamp;

		high = timestamp >> 32;
		low  = (u32)timestamp;

		set_cur_sys_time(timestamp);
		rtos_misc_trace(LOG_ERR, 0x2c50, "set current sys time is high:%d[0x%x],low:%d[0x%x] ms.\n",high, high, low, low);
	}

	return 0;
}

static DEFINE_UART_CMD(sys_time_ctrl, "sys_time_ctrl",
	"set/get sys time(2 paras),para1(1:set 0:get);para2(time[hex],unit(100ms),get ignore)",
	"set/get sys time(2 paras),para1(1:set 0:get);para2(time,unit(ms))",
		2, 2, sys_time_ctrl);

#endif


static DEFINE_UART_CMD(grpdump, "grpdump",
		"group reg dump",
		"grpdump btn/cmd_proc/l2p", 1, 1, grpdump_main);

static DEFINE_UART_CMD(cpu_clk, "cpu_clk",
	"change CPU CLK frequency",
	"syntax: cpu_clk n [n = 0(333MHz), n = 1(666MHz)]",
		0, 1, cpu_clk_main);


static ps_code int nf_clk_main(int argc, char *argv[])
{
	int new_clk_freq;

	if (argc == 1) {
		rtos_misc_trace(LOG_ERR, 0x74cf, "\nnf_clk(%d MHz)", cur_nf_clk_freq);
		return 0;
	}

	new_clk_freq = atoi(argv[1]);
	if (new_clk_freq >= ARRAY_SIZE(nf_pll_settings)-1) {
		rtos_misc_trace(LOG_ERR, 0x318b, "\nnf_clk: Error - wrong parameter(%d)", new_clk_freq);
		return -2;
	}

	new_clk_freq = nf_pll_settings[new_clk_freq].freq;

	if (new_clk_freq == cur_cpu_clk_freq) {
		rtos_misc_trace(LOG_ERR, 0xbf5d, "\nNo change: nf_clk(Cur:%d MHz) = (New:%d MHz)", cur_cpu_clk_freq, new_clk_freq);
	} else {
		int prev_freq = cur_nf_clk_freq;
		nf_clk_init(new_clk_freq);
		rtos_misc_trace(LOG_ERR, 0xe413, "\nnf_clk(%d MHz)->(%d MHz)", prev_freq, cur_nf_clk_freq);
	}

	return 0;
}

static DEFINE_UART_CMD(nf_clk, "nf_clk",
	"change NF CLK frequency",
	"syntax: nf_clk n [n = 0,1,2,3,4]",
		0, 1, nf_clk_main);


/*
static ps_code int gp_io(int argc, char *argv[])
{
    u32 gpio_input = 0, gpio0_input = 0;
    gpio_input = readl((void *)(MISC_BASE + GPIO_PAD_CTRL));
    gpio0_input = (gpio_input >> (GPIO_OUT_SHIFT + GPIO_PLN_SHIFT)) & 0x1;
    rtos_misc_trace(LOG_ALW, 0, " gpio0_input :%d 0x%x", gpio0_input,gpio_input);

	return 0;
}

static DEFINE_UART_CMD(gp, "gp",
	"gp",
	"gp_io",
		0, 0, gp_io);
*/

module_init(misc_init_must_be_in_tcm, RTOS_PRE);

#ifdef MEASURE_TIME
init_code u32 Misc_GetElapsedTimeMsec(u32 timestamp)
{
    u32 cur_tsc = readl((void *)0xC0201044);
    u32 elapsed_in_ms = 0;


    if (cur_tsc < timestamp)
    {
        elapsed_in_ms = ((0xFFFFFFFF - timestamp) + (cur_tsc)) / 666000;  //possible hang
    }
    else
    {
        elapsed_in_ms = (cur_tsc - timestamp) / 666000;
    }

    return elapsed_in_ms;
}
#endif
#if (CPU_ID == 4)
ddr_code int soc_uid_main(int argc, char *argv[])
	{
		u32 uid0, uid1;

		get_soc_uid(&uid0, &uid1);

		rtos_core_trace(LOG_INFO, 0x5939, "UNID_INFO[0] : 0x%x", uid0);
		rtos_core_trace(LOG_INFO, 0x9eb1, "UNID_INFO[1] : 0x%x", uid1);
		//printk("UNID_INFO[0] : 0x%x", uid0);
		//printk("UNID_INFO[1] : 0x%x", uid1);

		return 0;
}

static DEFINE_UART_CMD(soc_uid,"soc_uid",
							"soc_uid , get UNID[0]/UNID[1]",
							"soc_uid , get UNID[0]/UNID[1]",
							0, 1, soc_uid_main);
#endif
ddr_code u32 _read_otp_data(u32 offset)
{
	control_status_reg_t otp_ctrl_reg;
	u32 value;

	otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	while (otp_ctrl_reg.b.prog_done || !otp_ctrl_reg.b.otp_idle) {
		otp_writel(otp_ctrl_reg.all, CONTROL_STATUS_REG);
		otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	}
	if (otp_ctrl_reg.b.direct_mode) {
		otp_ctrl_reg.b.direct_mode = 0;
		otp_writel(otp_ctrl_reg.all, CONTROL_STATUS_REG);
	}
	value = otp_readl(offset);
	do {
		otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	} while (!otp_ctrl_reg.b.otp_idle);
	return value;
}
ddr_code int _program_otp_data(u32 data, u32 offset)
{
	control_status_reg_t otp_ctrl_reg;
	otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	u32 value, org_value;
	int ret = 0;
#if 1
	org_value = _read_otp_data(offset);
	otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	otp_ctrl_reg.b.fw_rst = 1;
	otp_writel(otp_ctrl_reg.all, CONTROL_STATUS_REG);
	while (otp_ctrl_reg.b.fw_rst) {
		otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
		udelay(100);
	}
#endif
	while (otp_ctrl_reg.b.prog_done || !otp_ctrl_reg.b.otp_idle) {
		otp_writel(otp_ctrl_reg.all, CONTROL_STATUS_REG);
		otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	}
	if (otp_ctrl_reg.b.direct_mode) {
		otp_ctrl_reg.b.direct_mode = 0;
		otp_writel(otp_ctrl_reg.all, CONTROL_STATUS_REG);
	}
	otp_writel(data, offset);
	otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	while (otp_ctrl_reg.b.prog_done == 0) {
		udelay(100);
		otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	}
	while (otp_ctrl_reg.b.prog_done) {
		otp_writel(otp_ctrl_reg.all, CONTROL_STATUS_REG);
		otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
	}
	if (!otp_ctrl_reg.b.otp_idle) {
		do {
			otp_ctrl_reg.all = otp_readl(CONTROL_STATUS_REG);
		} while (!otp_ctrl_reg.b.otp_idle);
	}
	value = _read_otp_data(offset);
	if (value != data) {
		evlog_printk(LOG_ALW,"otp prog data offset %x fail, act/exp[%x/%x]\n", offset, value, data);
		ret = -1;
	} else {
		evlog_printk(LOG_ALW,"otp prog data offset %x pass, act/org[%x/%x]\n", offset, value, org_value);
	}
	return ret;
}
ddr_code void misc_set_otp_deep_stdby_mode(void)
{
	control_status_reg_t otp_ctrl;
	otp_direct_ctrl_reg_t otp_dir_ctrl;
    evlog_printk(LOG_ALW,"Force set otp deep stdby mode\n");
	do {
		otp_ctrl.all = otp_readl(CONTROL_STATUS_REG);
		if (otp_ctrl.b.otp_idle == 1)
			break;
	} while (1);

	do
	{
		// direct mode
		otp_ctrl.b.direct_mode = 1;
		otp_writel(otp_ctrl.all, CONTROL_STATUS_REG);

		otp_dir_ctrl.all = otp_readl(OTP_DIRECT_CTRL_REG);
		otp_dir_ctrl.b.otp_pdstb_n = 0;
		otp_writel(otp_dir_ctrl.all, OTP_DIRECT_CTRL_REG);
	} while (((otp_readl(CONTROL_STATUS_REG)&(1<<DIRECT_MODE_SHIFT)) != 1) || ((otp_readl(OTP_DIRECT_CTRL_REG)&(1<<OTP_PDSTB_N_SHIFT)) != 0));

}
share_data_zi volatile u8 otp_set_result;

ddr_code void misc_chk_otp_deep_stdby_mode(void)
{
	control_status_reg_t otp_ctrl;
	otp_direct_ctrl_reg_t otp_dir_ctrl;
    evlog_printk(LOG_ALW,"Check otp deep stdby mode\n");
	do {
		otp_ctrl.all = otp_readl(CONTROL_STATUS_REG);
		if (otp_ctrl.b.otp_idle == 1)
			break;
	} while (1);
	otp_set_result = otp_ctrl.b.direct_mode;
	otp_dir_ctrl.all = otp_readl(OTP_DIRECT_CTRL_REG);
	otp_set_result |= otp_dir_ctrl.b.otp_pdstb_n << 4;
    evlog_printk(LOG_ALW,"OTP_CTRL = 0x%x\n", otp_ctrl.all);
    evlog_printk(LOG_ALW,"OTP_DIR = 0x%x\n", otp_dir_ctrl.all);
    for(int j=0; j<8; j++)
    {
        evlog_printk(LOG_ALW,"OTP addr 0x%x = 0x%x\n", 4*j, otp_readl(CONTROL_STATUS_REG+4*j));
    }
    if(otp_set_result != 0x01)
    {
    }
#if 0
    for (int i=0; i < 0x200 ; i+=4)
    {
        u32 value;
	     value = otp_readl(0x100+i);//0x100 ~ 0x2FF
	     if(i==0)
         {
            evlog_printk(LOG_ALW,"OTP addr 0x%x = 0x%x\n", 0x100+i, value);
         }
	     if(value != 0x0)
         {
            evlog_printk(LOG_ALW,"OTP error addr 0x%x = 0x%x\n", 0x100+i, value);
         }
    }
#endif
}

#if CO_SUPPORT_PANIC_DEGRADED_MODE
ddr_code void Assert_timer_isr(void)
{
	timer_isr();
}
#endif
/*! @} */
