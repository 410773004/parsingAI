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

/*! \file irq.c
 * @brief interrupt handler
 *
 * \addtogroup rtos
 * \defgroup irq
 * \ingroup rtos
 *
 * {@P
 */

#include "types.h"
#include "stdio.h"
#include "sect.h"
#include "pic.h"
#include "irq.h"
#include "cpu_msg.h"
#include "bitops.h"
//#include "io.h"
#include "event.h"
#include "vic_register.h"
#include "atomic.h"
#include "misc.h"

#if (CPU_ID == 1)
#define CO_SUPPORT_SANITIZE 1

#if CO_SUPPORT_SANITIZE
#include "epm.h"
#include "dtag.h"
extern epm_info_t *shr_epm_info;
extern epm_smart_t *epm_smart_data;
#endif

#ifdef TCG_NAND_BACKUP
#include "../../tcg/tcg_nf_mid.h"
extern bool bTcgTblErr;
#endif

#endif

/*! \cond PRIVATE */
#define __FILEID__ irq
#include "trace.h"
/*! \endcond */


extern volatile u64 pln_loop_time; 
extern volatile bool PLN_evt_trigger; 
extern volatile bool PLN_keep_50ms;

extern volatile u8 plp_trigger;
extern volatile u8 plp_evlog_trigger;
fast_data_zi irq_handler_t isr_tbl[32];	///< interrupt routine table
fast_data_zi irq_handler_t poll_isr_tbl[32];	///< mirror table of isr_tbl used by polling
fast_data_zi u8 in_isr;
fast_data_zi u8 irq_disabled;
fast_data_zi u8 irq_int_done;
#if (CPU_ID==CPU_BE)
extern volatile int _fc_credit;
extern int shr_gc_fc_ctrl;
//extern u32 gc_load_p2l_time;
extern u32 shr_bew_perf;
extern u8 evt_gc_issue_load_p2l;
extern u8 gc_issue_load_p2l;
//extern u8 gc_start_next_fc_cnt;
extern u8 shr_no_host_write;
extern u8 shr_gc_no_slow_down;
extern u64 gc_read_disturb_time;
extern u8 shr_gc_read_disturb_ctrl;
extern u8 du_cnt_per_die;
extern volatile bool host_idle;
share_data_zi volatile u8 shr_is_gc_emgr;
#endif
#if (CPU_ID==CPU_BE_LITE)
fast_data u64 cur_ms;
extern volatile int _fc_credit;
extern u32 shr_hostw_perf;
extern u32 shr_gc_rel_du_cnt;
extern u8 shr_gc_fc_ctrl;
extern fast_data u8 evt_gc_release_dtag_chk;
#endif
/*!
 * @brief register polling interrupt handler
 *
 * @param irqno		irq number
 * @param isr		interrupt service routine
 *
 * @return		not used
 */
ps_code void poll_irq_register(vic_vid_t irqno, irq_handler_t isr)
{
	if (NULL != poll_isr_tbl[irqno])
		panic("poll_irq_register");

	poll_isr_tbl[irqno] = isr;
}

/*!
 * @brief register interrupt handler
 *
 * @param irqno		irq number
 * @param isr		interrupt service routine
 *
 * @return		not used
 */
ps_code void irq_register(vic_vid_t irqno, irq_handler_t isr)
{
	if (isr_tbl[irqno] != NULL)
		panic("irq_register");

	isr_tbl[irqno] = isr;
}
/*!
 * @brief check interrupt status and handle it if trigger
 *
 * @return		not used
 */
fast_code void do_irq(void)
{
	u32 act = pic_active_irq();
    in_isr = true;
	/* The CLZ instruction counts the number of leading zeros in the value
	 * in Rm and returns the result in Rd. The result value is 32 if no bits
	 * are set in the source register, and zero if bit 31 is set.
	 */
	if (act == 0) {
		pic_ack_irq(31);
		rtos_mmgr_trace(LOG_INFO, 0x752f, "irq with no source %x", act);
		return;
	}

	do {
		u32 irq = 31 - clz(act);

		if (isr_tbl[irq])
			isr_tbl[irq]();
		else
			panic("no irq handler");
		act &= ~(1u<<irq);
		pic_ack_irq(irq);
	} while (act);
    in_isr = false;
}

#ifdef CPU_USAGE
fast_data u64 cpu_start;
fast_data u64 busy_start;
fast_data u64 cpu_busy = 0;

fast_code void cpu_idle(void)
{
	if (busy_start == 0) {
		cpu_start = get_tsc_64();
	} else {
		u64 now = get_tsc_64();
		u64 cpu_time = now - cpu_start;

		cpu_busy += get_tsc_64() - busy_start;
		// 2s
		if (cpu_time > 2000000*666) {
			u32 a = cpu_busy >> 32;
			u32 b = cpu_busy & 0xFFFFFFFF;

			rtos_mmgr_trace(LOG_ERR, 0xc35b, "CPU usage %x%x /", a, b);
			a = cpu_time >> 32;
			b = cpu_time & 0xFFFFFFFF;
			rtos_mmgr_trace(LOG_ERR, 0x958a, " %x%x\n", a, b);
			cpu_busy = 0;
			cpu_start = get_tsc_64();
		}

	}

	while (1) {
		if (vic_readl(IRQ_STATUS))
			break;

		if (evt_task_in())
			break;
	}

	busy_start = get_tsc_64();
}
#endif

/*!
 * @brief irq loop
 *
 * @return	not used
 */
fast_code void loop_irq(void)
{
	u32 act1, act = 0;

#ifdef CPU_USAGE
	cpu_idle();
#endif

	#if (CPU_ID == 1 || CPU_ID == 2 || CPU_ID == 4)
	u8 set = 0;
	u64 start, curr;
	u32 tick = 0;
	u32 loop_cnt = 0;
	u64 elapsed;
	u8 tick_updated = 0;
	#endif

#if ((CPU_ID == 1) && (CO_SUPPORT_SANITIZE))
	epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	if(epm_smart_data->sanitizeInfo.sanitize_Tag == 0xDEADFACE)
	{
		if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates)
		{
			extern u8 evt_admin_sanitize_operation;
			evt_set_cs(evt_admin_sanitize_operation, 0, 0, CS_TASK);
		}
	}
#endif
#if ((CPU_ID == 1) && (_TCG_)) && !defined(FW_UPDT_TCG_SWITCH)
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	if((epm_aes_data->tcg_en_dis_tag != TCG_TAG) && (bTcgTblErr))
	{
		rtos_mmgr_trace(LOG_ALW, 0x1f3b, "[TCG] tbl err when TCG disable -> clear tbl");
		extern void tcg_preformat_init(void);
		tcg_preformat_init();
	}
#endif
	while (1) {
		#if (CPU_ID == 1 || CPU_ID == 2 || CPU_ID == 4)
		// this is just for plp not done debug
		if (plp_trigger && !set) {
			start = get_tsc_64();
			set = 1;
			tick = 0;
			loop_cnt = 0;
			elapsed = 0;
		}
		if (plp_trigger && set) {
			curr = get_tsc_64();
			elapsed = curr - start + elapsed;
			start = curr;
			while (elapsed >= 800*30000) {
				elapsed -= 800*30000;
				++tick;
				tick_updated = 1;
			}
			if (tick_updated) {
				tick_updated = 0;
				extern u32 cpu2_streaming_read_cnt;
				extern u32 cpu4_streaming_read_cnt;
				extern u8  CPU1_plp_step;
				extern u8  CPU2_plp_step;
				rtos_mmgr_trace(LOG_ALW, 0x483b, "30ms tick:%d, cpu1:%d, cpu2:%d r_2:%d r_4:%d", tick, CPU1_plp_step, CPU2_plp_step,cpu2_streaming_read_cnt,cpu4_streaming_read_cnt);
				if(CPU2_plp_step < 5 && plp_evlog_trigger == EVLOG_TRIGGER_INIT)
				{
					plp_evlog_trigger = EVLOG_TRIGGER_START;
					flush_to_nand(EVT_PLP_HANDLE_DONE);
				}
#if (CPU_ID == 1)
				extern volatile u8 plp_PWRDIS_flag;
				extern u8 evt_plp_set_ENA;
				if(plp_PWRDIS_flag == 2){
					printk("\nIn loop_irq, plp_PWRDIS_flag: %d\n", plp_PWRDIS_flag);
		 			evt_set_cs(evt_plp_set_ENA, PLP_ENA_DISABLE, 0, CS_NOW); // default 1 -> 0
					plp_PWRDIS_flag = 0;
				}
#endif
			}
		}
		#endif
		u32 idle = 0;

		// loop for ISR, IPC, and EVENT
		act = vic_readl(RAW_INTERRUPT_STATUS) & _poll_event_mask;	// mask bit[31] which handled in ISR
		act |= 1 << VID_SYSTEM_MISC;
		if (act) {
			act1 = act;
			do {
				u32 irq = 31 - clz(act1);
#if 0//CPU_ID_0 == 0
				extern void GetSensorTemp(void);
				GetSensorTemp();
#endif

				if (urg_evt_chk())
					urg_evt_task_process();

				if (poll_isr_tbl[irq]) {
					poll_isr_tbl[irq]();
				} else {
					panic("no irq handler");
				}
				act1 &= ~(1u << irq);
			} while (act1);
			vic_writel(act , VECTOR_ADDRESS);
		} else {
			idle++;
		}
#if 0//CPU_ID_0 == 0
		extern void GetSensorTemp(void);
		GetSensorTemp();
#endif

		if (urg_evt_chk())
			urg_evt_task_process();
#if defined(RDISK)
		sw_ipc_poll();
#endif

		if (urg_evt_chk())
			urg_evt_task_process();

		idle += evt_task_process_one() ? 0 : 1;

		#if CO_SUPPORT_PANIC_DEGRADED_MODE
		if (assert_evt_chk())
			assert_evt_task_process();
		#endif

		if (idle == 3) {
#ifdef CPU_USAGE
			cpu_idle();
#else
			CPU_IDLE;
#endif
		}

    #if (CPU_ID == CPU_BE)
    /*if (shr_no_host_write == 0 && gc_load_p2l_time && jiffies - gc_load_p2l_time > HZ){  // no write in 1 second, let gc finish (prevent shutdown NG)
      shr_no_host_write = true;
      evlog_printk(LOG_ERR,"gc_load_p2l_time %d jiffies %d",gc_load_p2l_time,jiffies);
    }*/  // change to refer shr_bew_perf

    if(gc_issue_load_p2l == true && ((shr_gc_fc_ctrl != true) || _fc_credit < du_cnt_per_die || shr_no_host_write || shr_bew_perf == 0 || shr_gc_no_slow_down))  // todo : refer to BE write perf?
	{
		if((shr_is_gc_emgr) || !shr_gc_read_disturb_ctrl || time_elapsed_in_ms(gc_read_disturb_time) > (host_idle?175:375))//jiffies - gc_read_disturb_time >= (1)) //delay for read distrube
			evt_set_cs(evt_gc_issue_load_p2l, 0, 0, CS_NOW);
	}
	#endif

	  #if (CPU_ID == CPU_BE_LITE)
	  #ifndef WCMD_USE_IRQ  // IRQ mode force supply free Dtag when receive write command, especially in erase SPB, and cache will control
	  if (shr_gc_fc_ctrl)
	  #endif
	  {
          evt_set_cs(evt_gc_release_dtag_chk, 0, 0, CS_NOW);
          //evlog_printk(LOG_ERR,"hus %d lus %d fc %d sh %d ",(u32)(cur_ms>>32),(u32)cur_ms,_fc_credit,shr_gc_perf);
	  }
      #endif
      
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)   
#if (CPU_ID == 1) 
        if(PLN_evt_trigger == true){ 
            u64 pln_time_start,pln_time_curr; 
            pln_time_curr = get_tsc_64(); 
            pln_time_start = pln_time_curr - pln_loop_time;            
            if((pln_time_start > 800*45000)&&!plp_trigger) //pln_time_start > 800*50000   50ms
            { 
                PLN_keep_50ms = true;
            }     
            pln_evt();
        } 
#endif 
#endif 

#if (CPU_ID == 1 || CPU_ID == 2 || CPU_ID == 4)
	  loop_cnt++;
#endif
	}
}

/*! @} */
