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

//Process the communication between different CPU
//=============================================================================
#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "ipc_register.h"
#include "irq.h"
#include "assert.h"
#include "sect.h"
#include "mod.h"
#include "vic_id.h"
#include "bitops.h"
#include "string.h"
#include "console.h"
#include "event.h"
#include "pic.h"
#include "dma.h"
#include "cpu_msg.h"
#include "rainier_soc.h"
#include "cbf.h"
#include "mpc.h"
#include "misc.h"
#include "cpu1_cfg_register.h"
#include "misc_register.h"

/*
cpu_msg_send()
--------------
cpu1->cpu1 => x
cpu1->cpu2 write wptr2 0x90 bit0-14, q[1][2]
cpu1->cpu3 write wptr3 0xa0 bit0-14, q[1][3]
cpu1->cpu4 write wptr4 0xb0 bit0-14, q[1][3]

cpu2->cpu1 write wptr1 0x80 bit0-14, q[2][1]
cpu2->cpu2 => x
cpu2->cpu3 write wptr3 0xa0 bit0-14, q[2][3]
cpu2->cpu4 write wptr4 0xb0 bit0-14, q[2][4]

cpu3->cpu1 write wptr1 0x80 bit0-14, q[3][1]
cpu3->cpu2 write wptr2 0x90 bit0-14, q[3][2]
cpu3->cpu3 => x
cpu3->cpu4 write wptr4 0xb0 bit0-14, q[3][4]

cpu4->cpu1 write wptr1 0x80 bit0-14, q[4][1]
cpu4->cpu2 write wptr2 0x90 bit0-14, q[4][2]
cpu4->cpu3 write wptr3 0xa0 bit0-14, q[4][3]
cpu4->cpu4 => x

cpu_msg_receive()
-----------------
cpu1<-cpu1 => x
cpu1<-cpu2 read wptr2_mirror 0x90 bit16-30, write rptr2 0x94 bit0-14, q[2][1]
cpu1<-cpu3 read wptr3_mirror 0xa0 bit16-30, write rptr3 0xa4 bit0-14, q[3][1]
cpu1<-cpu4 read wptr4_mirror 0xb0 bit16-30, write rptr4 0xb4 bit0-14, q[4][1]

cpu2<-cpu1 read wptr1_mirror 0x80 bit16-30, write rptr1 0x84 bit0-14, q[1][2]
cpu2<-cpu2 => x
cpu2<-cpu3 read wptr3_mirror 0xa0 bit16-30, write rptr3 0xa4 bit0-14, q[3][2]
cpu2<-cpu4 read wptr4_mirror 0xb0 bit16-30, write rptr4 0xb4 bit0-14, q[4][2]

cpu3<-cpu1 read wptr1_mirror 0x80 bit16-30, write rptr1 0x84 bit0-14, q[1][3]
cpu3<-cpu2 read wptr2_mirror 0x90 bit16-30, write rptr2 0x94 bit0-14, q[2][3]
cpu3<-cpu3 => x
cpu1<-cpu4 read wptr4_mirror 0xb0 bit16-30, write rptr4 0xb4 bit0-14, q[2][3]

cpu4<-cpu1 read wptr1_mirror 0x80 bit16-30, write rptr1 0x84 bit0-14, q[1][4]
cpu4<-cpu2 read wptr2_mirror 0x90 bit16-30, write rptr2 0x94 bit0-14, q[2][4]
cpu4<-cpu3 read wptr3_mirror 0xa0 bit16-30, write rptr3 0xa4 bit0-14, q[3][4]
cpu4<-cpu4 => x
*/

/*! \cond PRIVATE */
#define __FILEID__ cpumsg
#include "trace.h"
/*! \endcond */

share_data_zi cpu_msg_t	cpu_msg;				///< cpu msg information
fast_data_zi static cpu_msg_handler_t	_cpu_msg_handler[CPU_MSG_END];		///< cpu msg handler

#if (CPU_BE == CPU_ID) & defined(RDISK)
	#define DTAG_COMT_Q_RECV
	fast_data_zi cbf_recv_t dtag_comt_recv;
#endif

#if (CPU_FE == CPU_ID) & defined(RDISK)
	#define DTAG_FREE_Q_RECV
	fast_data_zi cbf_recv_t dtag_free_recv;
#endif

#if (CPU_DTAG == CPU_ID) & defined(RDISK)
	#define DREF_COMT_Q_RECV
	fast_data_zi cbf_recv_t dref_inc_comt_recv;
	fast_data_zi cbf_recv_t dref_dec_comt_recv;
#endif

#if (CPU_BE == CPU_ID) & defined(RDISK)
	#define GC_SRC_DATA_Q_RECV
	fast_data_zi cbf_recv_t gc_src_data_recv;
#endif

#if (CPU_BE_LITE == CPU_ID) & defined(RDISK)
	#define GC_RES_FREE_Q_RECV
	fast_data_zi cbf_recv_t gc_res_free_recv;
#endif

#if (CPU_FTL == CPU_ID) & defined(RDISK)
	#define L2P_UPDT_Q_RECV
	fast_data_zi cbf_recv_t l2p_updt_recv;
#endif

#if (CPU_BE == CPU_ID) & defined(RDISK)
	#define L2P_UPDT_DONE_Q_RECV
	fast_data_zi cbf_recv_t l2p_updt_done_recv;
#endif

#if (MSG_CPU2_ID == CPU_ID) & defined(RDISK)
	#define PBT_UPDT_Q_RECV
	fast_data_zi cbf_recv_t pbt_updt_recv;
#endif

#if (CPU_FE == CPU_ID) & defined(RDISK)
	#define UCACHE_FLUSH_RECV
	fast_data_zi cbf_recv_t ucache_flush_recv;
#endif

fast_data u32 sw_ipc_poll_cnt = 0;


static inline u32 cpu_cfg_readl(u32 reg)
{
	return readl((void *)(CPU_CONF_BASE + reg));
}

static inline void cpu_cfg_writel(u32 data, u32 reg)
{
	writel(data, (void *)(CPU_CONF_BASE + reg));
}

static inline u32 get_msg_rp_reg(u8 cpu_id)
{
	u32 reg[] = {MSG_RP_REG1, MSG_RP_REG2, MSG_RP_REG3, MSG_RP_REG4};

	return cpu_cfg_readl(reg[cpu_id - 1]);
}

static inline u32 get_msg_rp_reg_rptr(u8 cpu_id)
{
	return ((get_msg_rp_reg(cpu_id) & READ_POINTER_1_MASK) >> READ_POINTER_1_SHIFT);
}

static inline u32 get_msg_rp_reg_rptr_mirror(u8 cpu_id)
{
	return ((get_msg_rp_reg(cpu_id) & READ_POINTER_MIRROR_FROM1_MASK) >> READ_POINTER_MIRROR_FROM1_SHIFT);
}

static inline u32 get_msg_wp_reg(u8 cpu_id)
{
	u32 reg[] = {MSG_WP_REG1, MSG_WP_REG2, MSG_WP_REG3, MSG_WP_REG4};

	return cpu_cfg_readl(reg[cpu_id - 1]);
}

static inline u32 get_msg_wp_reg_wptr(u8 cpu_id)
{
	return ((get_msg_wp_reg(cpu_id) & WRITE_POINTER_TO1_MASK) >> WRITE_POINTER_TO1_SHIFT);
}

static inline u32 get_msg_wp_reg_wptr_mirror(u8 cpu_id)
{
	return ((get_msg_wp_reg(cpu_id) & WRITE_POINTER_MIRROR_FROM1_MASK) >> WRITE_POINTER_MIRROR_FROM1_SHIFT);
}

fast_code static u32 get_cpu_msg_q_idx(u8 tx_cpu_id, u8 rx_cpu_id)
{
	sys_assert(tx_cpu_id != rx_cpu_id);

	u8 idx = CPU_MSG_Q_CPU_TO_CPU_END;
	// tx_cpu and rx_cpu are 1-base
	if ((tx_cpu_id == MSG_CPU1_ID) && (rx_cpu_id == MSG_CPU2_ID)){
		idx = CPU_MSG_Q_CPU1_TO_CPU2;
	} else if ((tx_cpu_id == MSG_CPU2_ID) && (rx_cpu_id == MSG_CPU1_ID)){
		idx = CPU_MSG_Q_CPU2_TO_CPU1;
#if MPC >= 3
	} else if ((tx_cpu_id == MSG_CPU1_ID) && (rx_cpu_id == MSG_CPU3_ID)){
		idx = CPU_MSG_Q_CPU1_TO_CPU3;
	} else if ((tx_cpu_id == MSG_CPU2_ID) && (rx_cpu_id == MSG_CPU3_ID)){
		idx = CPU_MSG_Q_CPU2_TO_CPU3;
	} else if ((tx_cpu_id == MSG_CPU3_ID) && (rx_cpu_id == MSG_CPU1_ID)){
			idx = CPU_MSG_Q_CPU3_TO_CPU1;
	} else if ((tx_cpu_id == MSG_CPU3_ID) && (rx_cpu_id == MSG_CPU2_ID)){
			idx = CPU_MSG_Q_CPU3_TO_CPU2;
#endif
#if MPC >= 4
	} else if ((tx_cpu_id == MSG_CPU1_ID) && (rx_cpu_id == MSG_CPU4_ID)){
		idx = CPU_MSG_Q_CPU1_TO_CPU4;
	} else if ((tx_cpu_id == MSG_CPU2_ID) && (rx_cpu_id == MSG_CPU4_ID)){
		idx = CPU_MSG_Q_CPU2_TO_CPU4;
	} else if ((tx_cpu_id == MSG_CPU3_ID) && (rx_cpu_id == MSG_CPU4_ID)){
		idx = CPU_MSG_Q_CPU3_TO_CPU4;
	} else if ((tx_cpu_id == MSG_CPU4_ID) && (rx_cpu_id == MSG_CPU1_ID)){
		idx = CPU_MSG_Q_CPU4_TO_CPU1;
	} else if ((tx_cpu_id == MSG_CPU4_ID) && (rx_cpu_id == MSG_CPU2_ID)){
		idx = CPU_MSG_Q_CPU4_TO_CPU2;
	} else if ((tx_cpu_id == MSG_CPU4_ID) && (rx_cpu_id == MSG_CPU3_ID)){
		idx = CPU_MSG_Q_CPU4_TO_CPU3;
#endif
	}

	return idx;
}

fast_code static void set_msg_rp_reg(u32 data, u8 cpu_id)
{
	u32 reg[] = {MSG_RP_REG1, MSG_RP_REG2, MSG_RP_REG3, MSG_RP_REG4};

	cpu_cfg_writel(data, reg[cpu_id - 1]);
}

fast_code static void set_msg_wp_reg(u32 data, u8 cpu_id)
{
	u32 reg[] = {MSG_WP_REG1, MSG_WP_REG2, MSG_WP_REG3, MSG_WP_REG4};

	cpu_cfg_writel(data, reg[cpu_id - 1]);
}



fast_code void cpu_msg_isr_cpu(u8 tx_cpu_id)
{
	//msg_wp_reg_t msg_wp_reg;
	msg_rp_reg_t msg_rp_reg;
	//msg_wp_reg.all = get_msg_wp_reg(tx_cpu_id);
	msg_rp_reg.all = get_msg_rp_reg(tx_cpu_id);
	//rtos_core_trace(LOG_DEBUG, 0, "%d msg_wp_reg  %x  msg_rp_reg %x",
		//	tx_cpu_id, msg_wp_reg.all, msg_rp_reg.all);
	u8 q_idx;
	cpu_msg_q_t *msg_q;
	u32 wptr,rptr;

	q_idx = get_cpu_msg_q_idx(tx_cpu_id, CPU_MSG_RX_CPU_IDX + 1);
	msg_q = &cpu_msg.q[q_idx];
	wptr = get_msg_wp_reg_wptr_mirror(tx_cpu_id);
	rptr = get_msg_rp_reg_rptr(tx_cpu_id);

	if (rptr == wptr)
		return;

	while (rptr != wptr) {
		u32 msg = msg_q->req[rptr].cmd.msg;
		cpu_msg_handler_t func = _cpu_msg_handler[msg];
		//rtos_core_trace(LOG_DEBUG, 0, "rptr %x wptr %x req->msg:%x req->tx: %x req->flag: %x",
			//	rptr, wptr, msg_q->req[rptr].cmd.msg, msg_q->req[rptr].cmd.tx, msg_q->req[rptr].cmd.flags);

		if (func == NULL)
        {
            rtos_mmgr_trace(LOG_ERR, 0xd1da, "msg_q function == NUll,msg=%x\n",msg);
            rptr = next_cbf_idx_2n(rptr, CPU_MSG_Q_SIZE);
            sys_assert_CUST(func);
            continue;
        }
		//sys_assert(func);
		sys_assert(tx_cpu_id == (msg_q->req[rptr].cmd.tx + 1));
        u64 time_s = get_tsc_64();
		func(&msg_q->req[rptr]);
        if (get_tsc_64() - time_s > 50*CYCLE_PER_MS) {
            rtos_core_trace(LOG_INFO, 0x1726, "cpu msg time cost > 50,tx:%d msg:%d rptr:%d wptr:%d",tx_cpu_id, msg, rptr, wptr);
        }
		rptr = next_cbf_idx_2n(rptr, CPU_MSG_Q_SIZE);
	}

	msg_rp_reg.b.read_pointer_cpu = rptr;
	set_msg_rp_reg(msg_rp_reg.all, tx_cpu_id);
}

fast_data bool in_cpu_msg_isr = false;
fast_code void cpu_msg_isr(void)
{
	msg_int_ctrl_t msg_int_ctrl = {.all = cpu_cfg_readl(MSG_INT_CTRL) };
	u32 tx_busy = msg_int_ctrl.b.cpu_msg_non_empty_int_status;

	//rtos_core_trace(LOG_DEBUG, 0, "msg_int_ctrl.all %x", msg_int_ctrl.all);
    if (in_cpu_msg_isr == false) {
        in_cpu_msg_isr = true;
        while (tx_busy) {
            u8 tx_cpu_idx = ctz(tx_busy);
        
            tx_busy &= ~(1 << tx_cpu_idx);
            sys_assert(tx_cpu_idx != CPU_MSG_TX_CPU_IDX);
            sys_assert(tx_cpu_idx < CPU_MSG_CPU_NUM);
            cpu_msg_isr_cpu(tx_cpu_idx + 1);
        }
        in_cpu_msg_isr = false;
    }
    else {
        //rtos_core_trace(LOG_INFO, 0, "cpu msg isr cpu :%x", tx_busy);
        return;
    }
    if (sw_ipc_poll_cnt) {
        return;
    }
    sw_ipc_poll_cnt++;


#ifdef DTAG_COMT_Q_RECV
	if (!CBF_EMPTY(dtag_comt_recv.cbf) && dtag_comt_recv.handler)
		dtag_comt_recv.handler();
#endif

#ifdef DTAG_FREE_Q_RECV
	if (!CBF_EMPTY(dtag_free_recv.cbf) && dtag_free_recv.handler)
		dtag_free_recv.handler();
#endif

#ifdef GC_SRC_DATA_Q_RECV
	if (!CBF_EMPTY(gc_src_data_recv.cbf) && gc_src_data_recv.handler)
		gc_src_data_recv.handler();
#endif

#ifdef GC_RES_FREE_Q_RECV
	if (!CBF_EMPTY(gc_res_free_recv.cbf) && gc_res_free_recv.handler)
		gc_res_free_recv.handler();
#endif
    sw_ipc_poll_cnt--;
}

#if (CPU_DTAG == CPU_ID)
extern void ncl_cmd_wait_finish(void);
#endif
fast_code void cpu_msg_issue(u32 rx_cpu_idx, u32 msg, u32 flags, u32 pl)
{
	u8 rx_cpu_id;
	u8 q_idx;
	cpu_msg_q_t *msg_q;
	u64 timer = get_tsc_64();
	u32 rptr;
	u32 wptr;
	u32 nwptr;
	bool full = false;
	u64 full_time=0;
    u32 count = 0;  
    if(rx_cpu_idx >= MPC){
        rtos_core_trace(LOG_ERR, 0xf60b, "rx:%d,msg:0x%x",rx_cpu_idx,msg);
        sys_assert(0);
    }
    //DIS_ISR();
	BEGIN_CS1
	rx_cpu_id = rx_cpu_idx + 1;

	q_idx = get_cpu_msg_q_idx(CPU_MSG_TX_CPU_IDX + 1, rx_cpu_id);
	msg_q = &cpu_msg.q[q_idx];

again:
	rptr = get_msg_rp_reg_rptr_mirror(rx_cpu_id);
	wptr = get_msg_wp_reg_wptr(rx_cpu_id);


	nwptr = next_cbf_idx(wptr, CPU_MSG_Q_SIZE);

	if (nwptr == rptr) 
	{
		if(full == false)
		{
			full = true;
			full_time =get_tsc_64();
		}
		if ((get_tsc_64() - timer) < 100*CYCLE_PER_MS)
			goto again;

        if (count < 5) {
            count++;
            rtos_core_trace(LOG_ERR, 0x7ee4, "from cpu %d to cpu %d msg %d, wptr = %x rptr = %x",
			    CPU_MSG_TX_CPU_IDX + 1, rx_cpu_id, msg, wptr, rptr);
        }

		#if (CPU_DTAG == CPU_ID)
		ncl_cmd_wait_finish();
		#endif

        goto again;
		//panic("handle cpu msg Q full");
	}
	if(full == true)
	{
		full_time = (get_tsc_64() - full_time);
		u32 hi = full_time >> 32;
		u32 low = full_time & 0xFFFFFFFF;
		rtos_core_trace(LOG_ERR, 0x9d00, "ipc full break %d %d %d",hi, low, msg);
	}
	msg_q->req[wptr].cmd.msg   = msg;
	msg_q->req[wptr].cmd.tx    = CPU_MSG_TX_CPU_IDX;
	msg_q->req[wptr].cmd.flags = flags;
	msg_q->req[wptr].pl        = pl;

	msg_wp_reg_t msg_wp_reg;
	msg_wp_reg.all = get_msg_wp_reg(rx_cpu_id);
	msg_wp_reg.b.write_pointer_to_cpu = nwptr;

	//rtos_core_trace(LOG_DEBUG, 0, "msg_wp_reg%d.all current %x new %x",
		//	rx_cpu_id,  get_msg_wp_reg(rx_cpu_id), msg_wp_reg.all);
	set_msg_wp_reg(msg_wp_reg.all, rx_cpu_id);

	__dmb();
    //EN_ISR();
	END_CS1
}


#if (CPU_ID == 1)	
ddr_code void cpu_msg_show(u32 rx_cpu_idx)
{
	u8 rx_cpu_id;
	u8 q_idx;
	cpu_msg_q_t *msg_q;
	//u64 timer = get_tsc_64();
	u32 rptr;
	u32 wptr;
	u32 nwptr;
	//bool full = false;
	//u64 full_time=0;
    //u32 count = 0;  
    //DIS_ISR();
	BEGIN_CS1
	rx_cpu_id = rx_cpu_idx + 1;

	q_idx = get_cpu_msg_q_idx(CPU_MSG_TX_CPU_IDX + 1, rx_cpu_id);
	msg_q = &cpu_msg.q[q_idx];


	rptr = get_msg_rp_reg_rptr_mirror(rx_cpu_id);
	wptr = get_msg_wp_reg_wptr(rx_cpu_id);
	
	nwptr = next_cbf_idx(wptr, CPU_MSG_Q_SIZE);

	msg_int_ctrl_t msg_int_ctrl = {.all = cpu_cfg_readl(MSG_INT_CTRL) };
	u32 tx_busy = msg_int_ctrl.b.cpu_msg_non_empty_int_status;
	if(tx_busy)
	{
		rtos_core_trace(LOG_ALW, 0xefbb,"tx_busy:0x%x",tx_busy);
	}
	while(rptr != wptr)
	{
		u32 msg = msg_q->req[rptr].cmd.msg;
		rtos_core_trace(LOG_ALW, 0x7d72,"CPU%d info rptr:%d msg:%d wptr:%d nwptr:%d",rx_cpu_idx,rptr,msg,wptr,nwptr);
		rptr = next_cbf_idx(rptr, CPU_MSG_Q_SIZE);
	}

    //EN_ISR();
	END_CS1
}

#endif


fast_code void cpu_msg_sync_done(u8 tx_cpu_idx)
{
	cpu_msg.sync_done[tx_cpu_idx] = 1;
}

ps_code void cpu_msg_register(u32 msg, cpu_msg_handler_t func)
{
	sys_assert(msg < CPU_MSG_END);
	_cpu_msg_handler[msg] = func;
}

// toDO: share will power off when PMU
UNUSED init_code void cpu_msg_hw_init(void)
{
	bool cpu_msg_init = false;

	misc_reset(RESET_CPU_MSG);

	if ( (get_msg_rp_reg(MSG_CPU1_ID) || get_msg_wp_reg(MSG_CPU1_ID)) ||
		(get_msg_rp_reg(MSG_CPU2_ID) || get_msg_wp_reg(MSG_CPU2_ID)) ||
		(get_msg_rp_reg(MSG_CPU3_ID) || get_msg_wp_reg(MSG_CPU3_ID)) ||
		(get_msg_rp_reg(MSG_CPU4_ID) || get_msg_wp_reg(MSG_CPU4_ID)))
		cpu_msg_init = true;
	sys_assert(!cpu_msg_init);
}

init_code void cpu_msg_init(void)
{
#if CPU_ID == 1
	cpu_msg_hw_init();
#endif
	cpu_msg.sync_done[CPU_MSG_TX_CPU_IDX] = 0;

	msg_int_ctrl_t msg_int_ctrl = { .all = cpu_cfg_readl(MSG_INT_CTRL)};

	msg_int_ctrl.b.cpu_msg_non_empty_int_mask &= ~((1 << CPU_MSG_CPU_NUM) - 1);
	cpu_cfg_writel(msg_int_ctrl.all, MSG_INT_CTRL);

	poll_irq_register(VID_CPU_MSG_INT, cpu_msg_isr);
	poll_mask(VID_CPU_MSG_INT);
	rtos_core_trace(LOG_ALW, 0xb592, "cpu_msg init done msg_int_ctrl.all %x", cpu_cfg_readl(MSG_INT_CTRL));
}

#ifdef DREF_COMT_Q_RECV
fast_data_ni u32 dref_dec_wptr_fence;
fast_code bool dref_comt_poll(void)
{
	bool ret = false;

	dref_dec_wptr_fence = dref_dec_comt_recv.cbf->wptr;

	if (!CBF_EMPTY(dref_inc_comt_recv.cbf) && dref_inc_comt_recv.handler) {
		dref_inc_comt_recv.handler();
		ret = true;
	}

	if (!CBF_EMPTY(dref_dec_comt_recv.cbf) && dref_dec_comt_recv.handler) {
		dref_dec_comt_recv.handler();
		ret = true;
	}
	return ret;
}
#endif

fast_code void sw_ipc_poll(void)
{
    sw_ipc_poll_cnt++;
#ifdef DTAG_COMT_Q_RECV
	if (!CBF_EMPTY(dtag_comt_recv.cbf) && dtag_comt_recv.handler)
		dtag_comt_recv.handler();
#endif

#ifdef DTAG_FREE_Q_RECV
	if (!CBF_EMPTY(dtag_free_recv.cbf) && dtag_free_recv.handler)
		dtag_free_recv.handler();
#endif

#ifdef DREF_COMT_Q_RECV
	dref_comt_poll();
#endif

#ifdef GC_SRC_DATA_Q_RECV
	if (!CBF_EMPTY(gc_src_data_recv.cbf) && gc_src_data_recv.handler)
		gc_src_data_recv.handler();
#endif

#ifdef GC_RES_FREE_Q_RECV
	if (!CBF_EMPTY(gc_res_free_recv.cbf) && gc_res_free_recv.handler)
		gc_res_free_recv.handler();
#endif

#ifdef L2P_UPDT_Q_RECV
	if (!CBF_EMPTY(l2p_updt_recv.cbf) && l2p_updt_recv.handler)
		l2p_updt_recv.handler();
#endif

#ifdef L2P_UPDT_DONE_Q_RECV
	if (!CBF_EMPTY(l2p_updt_done_recv.cbf) && l2p_updt_done_recv.handler)
		l2p_updt_done_recv.handler();
#endif

#ifdef PBT_UPDT_Q_RECV
	if (!CBF_EMPTY(pbt_updt_recv.cbf) && pbt_updt_recv.handler)
		pbt_updt_recv.handler();
#endif

#if 0
//fix bug 
#ifdef UCACHE_FLUSH_RECV
	if (!CBF_EMPTY(ucache_flush_recv.cbf) && ucache_flush_recv.handler)
		ucache_flush_recv.handler();
#endif
#endif
    sw_ipc_poll_cnt--;
}
