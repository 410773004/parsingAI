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


#include "mod.h"
#include "task.h"
#include "stdlib.h"
#include "irq.h"

#define __FILEID__ main
#include "trace.h"
#include "assert.h"
#include "rainier_soc.h"
#include "io.h"
#include "sect.h"
#include "mpu.h"
#include "smb_registers.h"
#include "misc.h"
#include "nvme/inc/hal_nvme.h"	//20210426-Eddie
extern void __attribute__((weak)) dtag_add(int type, void *mem, u32 size);
extern smb_registers_regs_t *smb_slv;
fast_data_zi static task_t sys;

#if defined(MPC)
#include "mpc.h"
#endif

/*!
 * @brief	main task
 * classify	DTAG and running tasks
 *
 * @return	not used
 */
static void task_main(void)
{
#if defined(MPC)
	dtag_add(0, &__init_start_0, (u32) &__init_end_0 - (u32) &__init_start_0);
	dtag_add(0, &__init_start_1, (u32) &__init_end_1 - (u32) &__init_start_1);
	dtag_add(0, &__init_start_2, (u32) &__init_end_2 - (u32) &__init_start_2);
	dtag_add(0, &__init_start_3, (u32) &__init_end_3 - (u32) &__init_start_3);
#else
	dtag_add(0, &__init_start, (u32) &__init_end - (u32) &__init_start);
#endif

	irq_enable();

	do {
		task_block();
		/*lint -e(506) */
	} while (1);
}

/*!
 * @brief	main loop
 * classify DTAG and main loop irq
 *
 * @return	not used
 */
static fast_code void loop_main(void)
{
	extern void boot_dtags_recycle(u32);
	extern void wdt_start();

	wdt_start();
#if defined(MPC)
	if (CPU_ID == CPU_FE) {
		u32 cpu;

		for (cpu = 0; cpu < MPC; cpu++) {

#if defined(CPU_FTL)	//CPU_FTL's boot dtag will be recycled after recon done
			if (cpu == CPU_FTL - 1)
				continue;
#endif
			boot_dtags_recycle(cpu);
		}
	}
#else
	boot_dtags_recycle(0);
#endif

	// enable interrupt
	irq_enable();
    irq_int_done = true;
#if CPU_ID ==1										//Shane Add 1120 Start
	#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
	extern volatile u8 plp_trigger;
	if (!plp_trigger) {
		extern void clear_epm_vac(void);
		clear_epm_vac();
	} else {
		evlog_printk(LOG_ALW, "plp not clear vac");
	}
	#endif

#if (PLP_SUPPORT == 1)
	/*
		slc flow done 
		1. trigger force gc and bg trim
		//2. chk slc need erase , then unlock slc write
				----slc gc end check
		3. update epm
				----
	*/
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_INIT_DONE_SAVE_EPM, 0, 0);
#endif
	extern volatile u8 power_on_update_epm_flag;
	power_on_update_epm_flag = POWER_ON_EPM_UPDATE_ENABLE;
#endif

	#if (PLP_SUPPORT == 0)
	extern void clear_non_plp_epm(void);
	clear_non_plp_epm();
	#endif

	smb_control_register_t smb_control_register;
	smb_control_register.all = readl(&smb_slv->smb_control_register);
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_clk_stretch_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x09;
	smb_control_register.b.smb_tran_i2c_en = 0x01;
	writel(smb_control_register.all, &smb_slv->smb_control_register);
	#if (WB_RESUME == ENABLE)
		nvmet_wb_init();		//20210426-Eddie
	#endif
	#if UART_STARTWB	//20210517-Eddie
		extern void start_warm_boot_main(void);
		start_warm_boot_main();
	#endif

#endif																//Shane Add 1120 Start
	extern void loop_irq(void);
	loop_irq();
}

static init_code void task_prep(void)
{
	extern u32 __svc_stack_start__;
	task_init(&sys, (stack_t *) &__svc_stack_start__, 4096);
}

/*!
 * @brief	fw init function
 * to mount module functions
 *
 * @return	not used
 */
static init_code void fw_init(void)
{
	mod_init_t *fn = (mod_init_t *)&___sysinit_start;

	do {
		(*fn)();
		fn ++;
	} while (fn < (mod_init_t *)&___sysinit_end);
}

fast_data u32 btcm_dma_base;

init_data u32 btcm_bases[] = {
	CPU1_BTCM_BASE,
	CPU2_BTCM_BASE,
	CPU3_BTCM_BASE,
	CPU4_BTCM_BASE,
};

#if defined(MPC)
fast_code UNUSED NOINLINE void wait_btn_rst(void)
{
	local_item_done(start);
	wait_remote_item_done_no_poll(btn_rst);
}

init_code void wait_others_start(void)
{
	int i;
	u32 cnt;

	local_item_done(start);
	do {
		cnt = 0;
		for (i = 0; i < MPC; i++) {
			if (chk_cpu_start(i))
				cnt++;
		}
	} while (cnt != MPC);
}
#endif

ddr_code int pmumain(void)//ps_code -> ddr_code by suda
{
	extern void pmu_resume_on_other_core(void);
	extern void loop_irq(void);
	pmu_resume_on_other_core();
	loop_irq();
	sys_assert(0);
	return 0;
}

/*!
 * @brief	main function
 * The function where the program start
 *
 * @return	not used
 */
#define MDLY_AUTO_UPDATE
//#define TCM_ECC_ENABLE     // 2022.08.10 Jack, TCM ECC Feature enable
//#define TCM_ECC_GPIO10     // 2022.08.10 for Check TCM ECC setting cost time (about 290us)
#ifdef TCM_ECC_GPIO10
#define MISC_BASE 0xC0040000
#include "misc_register.h"
static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}
static inline void misc_writel(u32 data, u32 reg)
{
	writel(data, (void *)(MISC_BASE + reg));
}
#endif
init_code int main(void)
{
#if defined(MPC)
			cpu_init_bmp[CPU_ID_0].all = 0;
#ifdef TCM_ECC_ENABLE
  #ifdef TCM_ECC_GPIO10
  #if CPU_ID_0 == 0
			gpio_out_t gpio_out;
			u8 pin = 10;
			gpio_out.all = misc_readl(GPIO_OUT);
			gpio_out.b.gpio_out |= (1 << pin);
			gpio_out.b.gpio_oe |= (1 << pin);
			misc_writel(gpio_out.all, GPIO_OUT);
  #endif
  #endif
			set_tcm_ecc();
  #ifdef TCM_ECC_GPIO10
  #if CPU_ID_0 == 0
			gpio_out.all = misc_readl(GPIO_OUT);
			gpio_out.b.gpio_out &= ~(1 << pin);
			gpio_out.b.gpio_oe |= (1 << pin);
			misc_writel(gpio_out.all, GPIO_OUT);
  #endif
  #endif
#endif
#if CPU_ID_0 == 0
			wait_others_start();
#else
			wait_btn_rst();
#endif
#endif

#ifdef MEASURE_TIME
    u32 sys_clk = 0;
    baseTick = readl((void *)0xC0201044);
    sys_clk = cpu_clk_init(1);    //set CPU clock to 666
    uart_control_t ctrl = {0};
    ctrl.b.divisor = (sys_clk*(1000 * 1000)/2)/115200/16;
    ctrl.b.ur_enable   = 1;
    ctrl.b.rxff_thhold = 0;
    writel(ctrl.all, (void *)0xC0050010);    //set uart divisor

    recMsec[0] = Misc_GetElapsedTimeMsec(baseTick);
#endif

#ifdef OTF_MEASURE_TIME
#if CPU_ID == 1
     global_time_start[0] = get_tsc_64();

#endif
#endif

    #ifdef MDLY_AUTO_UPDATE
    u32 dll_ctrl_0 = readl((void *)0xC0064110);
    dll_ctrl_0 |= 1 << 27; // enable master delay value auto update
    writel(dll_ctrl_0, (void *)0xC0064110);
    #endif

#if defined(FPGA)
	soc_cfg_reg1.all = readl((void *)0xc004017c);
	soc_cfg_reg2.all = readl((void *)0xc0040180);

	soc_svn_id       = 0x8000; //readl((void *)0xC0040074);

#else
	soc_svn_id = 0;
#endif
	// enable PMU for 2G DDR access
	set_mpu_region();

	sys_assert(CPU_ID == get_cpu_id());

	btcm_dma_base = btcm_bases[get_cpu_id() - 1];
	trace_init(LOG_DEBUG);
	task_prep();
	fw_init();
 	extern void ts_setup(void);
	ts_setup();
#if defined(MPC)
	local_item_done(end);
	cpu_init_bmp[CPU_ID_0].b.end = 1;
	main_core_trace(LOG_ERR, 0x8f44, "init done\n");
#if CPU_ID_0 == 0
	volatile int all_done;
	do {
		int i;
		all_done = true;

		extern void LedCtl(u32 param, u32 payload, u32 count);
		LedCtl(1, 1, 0);

		for (i = 0; i < MPC; i++) {
			all_done &= cpu_init_bmp[i].b.end;
		}

		#if CO_SUPPORT_PANIC_DEGRADED_MODE
		extern bool assert_evt_chk(void);
		if (assert_evt_chk())
		{
		// 	smDegradeMode = true;
		// 	assert_evt_set(evt_degradedMode, 0, 0);
		// 	degradeFlag = true;
		// 	main_core_trace(LOG_ERR, 0, "Init TO");
			break;
		}
		#endif
	} while (all_done == false);
	
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)	
	#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		extern volatile u8 shr_slc_need_trigger;
		extern volatile u8 shr_slc_flush_state;
		if(shr_slc_need_trigger)
		{
			cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 1); 
			evlog_printk(LOG_ALW, "[SLC]CPU1 trigger SLC GC!!!");
		}
	#endif	
	#if (PLP_SUPPORT == 0)
		extern volatile bool shr_nonplp_gc_state;
	#endif
		u64 deadlock_time = get_tsc_64();
		//force set SSD no ready
		extern volatile bool delay_flush_spor_qbt;
		while((delay_flush_spor_qbt == true) 
	#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		|| (shr_slc_flush_state != SLC_FLOW_DONE) //fqbt and slc gc
	#endif
	#if (PLP_SUPPORT == 0)
		|| (shr_nonplp_gc_state != SPOR_NONPLP_GC_DONE )//nonplp gc
	#endif
		)
		{
			extern void LedCtl();
			LedCtl();
	#if CO_SUPPORT_PANIC_DEGRADED_MODE
			extern bool assert_evt_chk(void);
			if (assert_evt_chk())
			{
				break;
			}
	#endif
			if(time_elapsed_in_ms(deadlock_time) > 20000)//20s 
			{
				flush_to_nand(EVT_POWERONLOCK);
				break;
			}
	
		}
#endif


	#if CO_SUPPORT_PANIC_DEGRADED_MODE
	extern void Assert_Core_OneTimeInit(void);
	Assert_Core_OneTimeInit();
	#endif
    #ifdef TCG_NAND_BACKUP
    extern void tcg_preformat_continue(void);
    tcg_preformat_continue();
    #endif
    
	extern void smart_for_plp_not_done(void);
	smart_for_plp_not_done();
    extern bool all_init_done;
    extern volatile bool _fg_warm_boot;	
    all_init_done = true;
	_fg_warm_boot = false;
	main_core_trace(LOG_ERR, 0xa5aa, "all init done\n");



    //extern void _plp_cap_check(void *data);
    //_plp_cap_check(NULL);
    {
    	#if(PLP_SUPPORT == 1)
		extern void check_power(void);
		check_power();
		#endif
        irq_disable();
        extern void HalIrq_InitPcieInterrupt(void);
        gPcieRstRedirected = true;
        HalIrq_InitPcieInterrupt();
        HalIrq_InitNonPollingInterrupt();

		extern void pcie_dbglog(void);
		pcie_dbglog();					// Print out LTSSM after all CPU init done. 2023/08/09 Richard
    }
#endif
#endif

#if CPU_ID == 1
#ifdef OTF_MEASURE_TIME
		//recMsec[9] = *(vu32*)0xC0201044;
		evlog_printk(LOG_ALW,"[M_T]All_Init_Elapsed %8d us\n", time_elapsed_in_us(global_time_start[0]));
#endif
#endif

	loop_main();
	task_main();

	return -1;
}
