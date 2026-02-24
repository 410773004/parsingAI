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

/*! \file btn_dat.c
 * @brief Rainier BTN Module, data queue part
 *
 * \addtogroup btn
 * \defgroup btn data queue
 * \ingroup btn
 * @{
 */

#include "btn_precomp.h"
#include "btn.h"
#include "btn_dat.h"
#include "bf_mgr.h"
#include "btn_cmd_data_reg.h"
#include "event.h"
#include "mpc.h"
#include "GList.h"
#include "read_retry.h"
#include "ncl.h"
#include "fc_export.h"
#include "eccu.h"
#include "ncl_cmd.h"
#include "read_error.h"


/*! \cond PRIVATE */
#define __FILEID__ btnd
#include "trace.h"
/*! \endcond */

/*! @brief common free entry count */
#define COM_FREE_ENTRY_SHF	7
#define COM_FREE_ENTRY_CNT	(1 << COM_FREE_ENTRY_SHF)
#define COM_FREE_ENTRY_MSK	(COM_FREE_ENTRY_CNT - 1)

/*! @brief host read entry count */
#define HST_RD_ENTRY_SHF	7
#define HST_RD_ENTRY_CNT	(1 << HST_RD_ENTRY_SHF)
#define HST_RD_ENTRY_MSK	(HST_RD_ENTRY_CNT - 1)

/*! @brief free write entry count */
#define FREE_WR_ENTRY_SHF	8
#define FREE_WR_ENTRY_CNT	(1 << FREE_WR_ENTRY_SHF)
#define FREE_WR_ENTRY_MSK	(FREE_WR_ENTRY_CNT - 1)

#define WD_DES_CNT		(1 << 7)	///< total write data entry count
#define RD_DES_CNT		(1 << 7)	///< total read data entry count
#define ADDR_CNT		(1 << 7)	///< read/write source queue size

#define HMB_ADDR_CNT	32
#define HMB_DES_CNT	128

#define UNMAP_DATA_LOSS_WORKAROUND
#ifdef ERRHANDLE_ECCT
extern stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
extern tEcctReg ecct_pl_info[MAX_RC_REG_ECCT_CNT];
extern u8 rc_ecct_cnt;
extern u8 ecct_pl_cnt;
extern lda_t *gc_lda_list;  //gc lda list from p2l
extern u8 host_sec_bitz;
extern void *shr_dtag_meta;
extern void *shr_ddtag_meta;
#endif
#ifdef NCL_RETRY_PASS_REWRITE
extern u32 rd_rewrite_ptr;
extern retry_rewrite_t rd_rew_info[DDR_RD_REWRITE_TTL_CNT];
extern u32 shr_gc_ddtag_start;
extern u32 rd_rew_start_dtag;
extern enum du_fmt_t host_du_fmt;
#endif
#ifdef RD_FAIL_GET_LDA
share_data rd_get_lda_t *rd_err_lda_entry_0;
share_data rd_get_lda_t *rd_err_lda_entry_1;
share_data rd_get_lda_t *rd_err_lda_entry_ua;
share_data u16 rd_err_lda_ptr_0;
share_data u16 rd_err_lda_ptr_1;
extern u16 ua_btag;
#endif

/*! data entry of hmb dtag queue */
typedef union _hmb_dat_ent_t {
	u64 all;
	struct {
		u64 dtag_id : 24;		///< dtag id, must be HMB dtag
		u64 hmb_ofst_l : 12;		///< hmb offset low
		u64 btag : 12;			///< btn command tag
		u64 hmb_ofst_h : 4;		///< hmb offset high
		u64 rsvd : 6;
		u64 type_ctrl : 6;		///< type ctrl
	} pl;
} hmb_dat_ent_t;

/*!
 * @brief Buffer Manager Queue Groups
 */
typedef enum {
	BM_GRP_DTAG = 0,	///< dtag group
	BM_GRP_WR_DATA,		///< write data entry group
	BM_GRP_RD_DATA		///< read data entry group
} grp_t;

/*!
 * @brief enable or disable group setting, export this due to ncl_test.c
 *
 * @param grp		which group to set
 * @param enable	enable or disable the group
 *
 * @return		not used
 */
void btn_grp_enable(grp_t grp, bool enable);

/* !!!!! to be revised....
 * a) NVMe Read:
 *  1) HDR_DT_FREE_RD (Dynamic Read) -> HDR_RD_HST_RD
 *  2) Assign Dtags to NCB (Pre-assign Read) -> HDR_RD_FW_PASG
 *  3) HDR_DT_FREE_AURL (Streaming mode)
 *
 *  then push HDR_FW_RADJ -> HDR_DT_COM_FREE
 *
 *  for Unmap, we need fill vtag to HDR_FW_RADJ with offset.
 *
 * b) NVMe Write:
 *    HDR_DT_FREE_WR -> HDR_WR_NRM_WR -> FW to FTL -> HDR_DT_COM_FREE
 *
 * c) NVMe Partial Write:
 *     HDR_DT_FREE_WR -> HDR_WR_PART_WR
 *     FDMA -> HDR_RD_FW_PASG
 *     then DPE merge
 */

#if (BTN_HOST_READ_CPU == CPU_ID)
static fast_data_ni bm_pl_t btn_host_rd_entry[HST_RD_ENTRY_CNT] __attribute__ ((aligned(8)));	///< btn host read entry buffer
static fast_data_ni btn_cmdq_t btn_host_rd;	///< btn host read queue, (fw -> btn)
#endif

#if BTN_COM_FREE_CPU == CPU_ID
fast_data_ni u32 btn_com_free_entry[COM_FREE_ENTRY_CNT] __attribute__ ((aligned(32)));	///< btn common free data entry buffer
fast_data_ni btn_cmdq_t btn_com_free;	///< btn common free data entry queue
fast_data u8 evt_com_free_upt = 0xFF;		///< common free updated event
#endif

#if (BTN_DATA_IN_CPU == CPU_ID)
static fast_data_ni btn_data_entry_t _btn_wd_data_entry[WD_DES_CNT] __attribute__ ((aligned(32)));	///< btn write data entry buffer
static fast_data_ni pool_t btn_wd_de_pool;	///< write data entry buffer pool
static fast_data_ni u32 btn_wd_surc_entry[ADDR_CNT] __attribute__ ((aligned(32)));		///< btn write source queue buffer
static fast_data_ni u32 btn_wd_uptd0_entry[ADDR_CNT] __attribute__ ((aligned(32)));	///< btn write updated queue 0 buffer
static fast_data_ni u32 btn_wd_uptd1_entry[ADDR_CNT] __attribute__ ((aligned(32)));	///< btn write updated queue 0 buffer
static fast_data_ni u32 btn_wd_uptd2_entry[ADDR_CNT] __attribute__ ((aligned(32)));	///< btn write updated queue 0 buffer
static fast_data_ni btn_cmdq_t btn_wd_surc;	///< btn write source queue
static fast_data_ni btn_cmdq_t btn_wd_uptd0;	///< btn write updated queue 0, normal/partial
static fast_data_ni btn_cmdq_t btn_wd_uptd1;	///< btn write updated queue 1, compare
static fast_data_ni btn_cmdq_t btn_wd_uptd2;	///< btn write updated queue 2, pdone
fast_data u8 evt_wd_grp0_nrm_par_upt = 0xFF;		///< normal/partial write data entry updated event
fast_data u8 evt_wd_grp0_cmp_upt = 0xFF;		///< compare write data entry updated event
fast_data u8 evt_wd_grp0_pdone_upt = 0xFF;		///< pdone data entry updated event

static fast_data_ni btn_data_entry_t _btn_rd_data_entry[RD_DES_CNT] __attribute__ ((aligned(32)));	///< btn read data entry buffer
static fast_data_ni pool_t btn_rd_de_pool;	///< read data entry buffer pool
static fast_data_ni u32 btn_rd_surc_entry[ADDR_CNT] __attribute__ ((aligned(32)));	///< btn read source queue buffer
static fast_data_ni u32 btn_rd_uptd_entry[ADDR_CNT] __attribute__ ((aligned(32)));	///< btn read updated queue buffer
static fast_data_ni btn_cmdq_t btn_rd_surc;	///< btn read source queue
static fast_data_ni btn_cmdq_t btn_rd_uptd;	///< btn read updated queue
fast_data u8 evt_rd_ent_upt = 0xFF;
#endif

/* HMB */
#if BTN_DATA_IN_CPU == CPU_ID
static fast_data_ni btn_data_entry_t _btn_wd1_data_entry[WD_DES_CNT] __attribute__ ((aligned(32)));
static fast_data_ni pool_t btn_wd1_de_pool;
static fast_data_ni u32 btn_wd1_surc_entry[HMB_ADDR_CNT] __attribute__ ((aligned(32)));
static fast_data_ni u32 btn_wd1_uptd_entry[HMB_ADDR_CNT] __attribute__ ((aligned(32)));
static fast_data_ni btn_cmdq_t btn_wd1_surc;
static fast_data_ni btn_cmdq_t btn_wd1_uptd;
fast_data u8 evt_wr_grp0 = 0xFF;
#endif

#if BTN_FREE_WRITE_CPU == CPU_ID
fast_data_ni u32 btn_free_wr_entry[FREE_WR_ENTRY_CNT] __attribute__ ((aligned(8)));	///< btn free write entry buffer
static fast_data_ni btn_cmdq_t btn_free_wr;	///< btn free write entry queue
static fast_data_ni u32 abort_dtag_cnt = 0;	///< abort dtag push count
#endif

#if BTN_DATA_IN_CPU == CPU_ID && defined(HMB_DTAG)
static fast_data_ni btn_data_entry_t _btn_rd_hmb_data_entry[HMB_DES_CNT] __attribute__ ((aligned(32)));
static fast_data_ni btn_data_entry_t _btn_wr_hmb_data_entry[HMB_DES_CNT] __attribute__ ((aligned(32)));
static fast_data_ni btn_cmdq_t  btn_rd_hmb_cmdq;
static fast_data_ni btn_cmdq_t  btn_wr_hmb_cmdq;
#endif

extern volatile u8 plp_trigger;
//joe add sec size 20200820
extern u8 host_sec_bitz;
extern u16 host_sec_size;
//joe add sec size 20200820
#if BTN_DATA_IN_CPU == CPU_ID
#if defined(HMB_DTAG)
fast_code void bm_hmb_req(bm_pl_t *bm_tags, u16 hmb_ofst, bool read)
{
	//bf_mgr_trace(LOG_DEBUG, 0, "btag %x, read %x, hmb_ofst %x, dtag: %x",
			     bm_tags->pl.btag, read, hmb_ofst, bm_tags->pl.dtag);
	btn_cmdq_t *cmdq;
	hdr_reg_t *reg;
	hmb_dat_ent_t *entry;

	if (read) {
		cmdq = &btn_rd_hmb_cmdq;
		bm_tags->pl.type_ctrl = (bm_tags->pl.type_ctrl & 0xF8) | 5;
	} else {
		cmdq = &btn_wr_hmb_cmdq;
		bm_tags->pl.type_ctrl = (bm_tags->pl.type_ctrl & 0xF8) | 4;
	}
	reg = &cmdq->reg;
	entry = (hmb_dat_ent_t *)reg->base;

	reg->entry_pnter.all = readl((void *)(reg->mmio + 4));
	u16 rptr      = reg->entry_pnter.b.rptr;
	u16 wptr_nxt = (reg->entry_pnter.b.wptr + 1) & reg->entry_dbase.b.max_sz;

#ifdef While_break
	u64 start = get_tsc_64();
#endif

	while (rptr == wptr_nxt) {/* TODO Q busy */
		bf_mgr_trace(LOG_WARNING, 0x18a3, "Q busy, rptr %d wptr_nxt %d", rptr, wptr_nxt);
		reg->entry_pnter.all = readl((void *)(reg->mmio + 4));
		rptr = reg->entry_pnter.b.rptr;

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

	entry[reg->entry_pnter.b.wptr].all = bm_tags->all;
	entry[reg->entry_pnter.b.wptr].pl.hmb_ofst_h = hmb_ofst >> 12;
	entry[reg->entry_pnter.b.wptr].pl.hmb_ofst_l = hmb_ofst;

	reg->entry_pnter.b.wptr = wptr_nxt;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}
#endif

/*!
 * @brief handled all write data entries
 *
 * all write data entries will be handled in this interrupt
 * updated event handler need to dispatch it according bm_pl->type_ctrl
 *
 * @note refer to enum write_data_entry_type_ctrl_t
 *
 * @param cmdq	data entry updated queue
 * @param evt	event to handler data entries
 *
 * @return	not used
 */
fast_code void btn_de_wr_grp0(btn_cmdq_t *cmdq, u8 evt)
{
	hdr_reg_t *reg = &cmdq->reg;
	u32 wptr = cmdq->wptr;
	u32 rptr = reg->entry_pnter.b.rptr;
	u32 *entry = (u32 *)reg->base;
	extern volatile u8 shr_lock_power_on;
#if BTN_DATA_IN_CPU == CPU_ID
    if (cmdq->flags.b.hold) {
        //bf_mgr_trace(LOG_INFO, 0, "btn hold wr grp0 %d", 0);
        //evt_set_cs(evt_wr_grp0, (u32)cmdq, evt, CS_TASK);
        return;
    }
#endif
	if ((rptr == wptr) || cmdq->flags.b.bypass)
		return;

	if (shr_lock_power_on) // 	PCBaher plp01 workaround
		return;

	if (rptr > wptr) {
		u32 cnt = reg->entry_dbase.b.max_sz;
		u32 *addr = &entry[rptr];

		cnt = cnt - rptr + 1;
		evt_set_imt(evt, (u32)addr, cnt);

		hdr_surc_list_push(&btn_wd_surc, addr, cnt);

		rptr = (rptr + cnt) & reg->entry_dbase.b.max_sz;

		sys_assert(rptr == 0);
	}

	if (rptr < wptr) {
		u32 cnt = wptr;
		u32 *addr = &entry[rptr];

		cnt = cnt - rptr;
		evt_set_imt(evt, (u32)addr, cnt);

		hdr_surc_list_push(&btn_wd_surc, addr, cnt);

		rptr += cnt;

		sys_assert(rptr == wptr);
	}

	reg->entry_pnter.b.rptr = rptr;

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void btn_de_wr_grp1(void)
{
	btn_cmdq_t *cmdq = &btn_wd1_uptd;
	hdr_reg_t *reg = &cmdq->reg;
	u32 wptr = cmdq->wptr;
	u32 rptr = reg->entry_pnter.b.rptr;
	u32 *entry = (u32 *)reg->base;

	while (wptr != rptr) {
		u32 addr = entry[rptr];
		btn_data_entry_t *de = dma_to_btcm(addr);

		//bf_mgr_trace(LOG_DEBUG, 0, "dtag %x, lda_ofst %x %x, btn tag %x, %x",
			//     de->dw0.b.dtag,
			//     de->dw1.b.lda_ofst_h, de->dw0.b.lda_ofst_l,
			//     de->dw1.b.btn_cmd_tag, de->dw1.b.ctrl_type);

		/* TODO: bm_pl_t and btn_data_entry_t should only keep one */
		bm_pl_t *bm_tags = (bm_pl_t *)de;
		evt_set_imt(evt_hmb_rd_upt, (u32) bm_tags, 1);

		hdr_surc_push(&btn_wd1_surc.reg, btn_wd1_surc.wptr, addr);

		rptr = (rptr + 1) & reg->entry_dbase.b.max_sz;
	}

	reg->entry_pnter.b.rptr = rptr;

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code void btn_wr_grp0_handle(u32 param, u32 que, u32 evt_in)
{
    btn_de_wr_grp0(&btn_wd_uptd0, evt_wd_grp0_nrm_par_upt);
}

/*!
 * @brief handled all read data entries
 *
 * all read data entries will be handled in this interrupt
 * updated event handler need to dispatch it according bm_pl->type_ctrl
 *
 * @note refer to enum read_data_entry_type_ctrl_t
 *
 * @return	not used
 */
fast_code void btn_de_rd_grp0(void)
{
	btn_cmdq_t *cmdq = &btn_rd_uptd;
	hdr_reg_t *reg = &cmdq->reg;
	u32 wptr = cmdq->wptr;
	u32 rptr = reg->entry_pnter.b.rptr;
	u32 *entry = (u32 *)reg->base;
    if (cmdq->wptr == reg->entry_pnter.b.rptr){
        return;
    }
	if (cmdq->flags.b.bypass){
        //bf_mgr_trace(LOG_PLP, 0x19db, "plp doing, abort read");
		return;
	}
	if (rptr > wptr) {
		u32 cnt = reg->entry_dbase.b.max_sz;
		u32 *addr = &entry[rptr];

		cnt = cnt - rptr + 1;
		evt_set_imt(evt_rd_ent_upt, (u32)addr, cnt);

		hdr_surc_list_push(&btn_rd_surc, addr, cnt);

		rptr = (rptr + cnt) & reg->entry_dbase.b.max_sz;

		sys_assert(rptr == 0);
	}

	if (rptr < wptr) {
		u32 cnt = wptr;
		u32 *addr = &entry[rptr];

		cnt = cnt - rptr;
		evt_set_imt(evt_rd_ent_upt, (u32)addr, cnt);

		hdr_surc_list_push(&btn_rd_surc, addr, cnt);

		rptr += cnt;

		sys_assert(rptr == wptr);
	}

	reg->entry_pnter.b.rptr = rptr;
	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

/*!
 * @brief common interrupt routine for data entries updated
 *
 * @return	not used
 */
#ifdef WCMD_DROP_SEMI
fast_data_zi u64 delay_tick;
#endif
share_data_zi volatile u32 delay_cycle; //control delay for QD1 4K randwr QoS
//extern volatile bool gc_wl_speed_flag;

fast_code void btn_data_in_isr(void)
{
	btn_um_int_sts_t sts = { .all = btn_readl(BTN_MK_INT_STS), };
	btn_um_int_sts_t ack = { .all = 0 , };

	//bf_mgr_trace(LOG_DEBUG, 0, "sts = %x", sts.all);
	if (sts.b.rd_data_group0_update) {
		btn_de_rd_grp0();
		ack.b.rd_data_group0_update = 1;
	}
	if (sts.b.wr_data_group0_update) {
	    #ifdef WCMD_DROP_SEMI
		extern u32 dropsemi, shr_gc_op;
		extern u8 shr_gc_fc_ctrl;
        //bool bypass = false;

        if ((shr_gc_fc_ctrl != 1) || (dropsemi < 100) || (shr_gc_op > 700) || (get_tsc_64() - delay_tick >= delay_cycle)) {
        //if (global_gc_mode && (dropsemi >= 100) && (get_tsc_64() - delay_tick < delay_cycle)) {
        //    bypass = true;
        //}
        //if (!bypass) {
            delay_tick = get_tsc_64();
        #endif
		btn_de_wr_grp0(&btn_wd_uptd0, evt_wd_grp0_nrm_par_upt);
        #ifdef WCMD_DROP_SEMI
        }
        #endif
		btn_de_wr_grp0(&btn_wd_uptd1, evt_wd_grp0_cmp_upt);
		btn_de_wr_grp0(&btn_wd_uptd2, evt_wd_grp0_pdone_upt);
		ack.b.wr_data_group0_update = 1;
	}
	if (sts.b.wr_data_group1_update) {
		btn_de_wr_grp1();
		ack.b.wr_data_group1_update = 1;
	}

	btn_writel(ack.all, BTN_UM_INT_STS);
}

fast_code void btn_de_wr_hold(void)
{
    btn_wd_uptd0.flags.b.hold = true;
}

fast_code void btn_de_wr_cancel_hold(void)
{
    btn_wd_uptd0.flags.b.hold = false;
}

fast_code void btn_de_wr_hold_handle(u32 cnt, u32 hold_thr, u32 dis_thr)
{
	extern u32 ucache_read_pend_cnt;
    if (((cnt <= hold_thr)|| (ucache_read_pend_cnt >= 32)) && (!btn_wd_uptd0.flags.b.hold)) {
        //bf_mgr_trace(LOG_INFO, 0, "btn set hold cnt:%d", cnt);
        btn_wd_uptd0.flags.b.hold = true;
    } else if(((cnt >= dis_thr)&& (ucache_read_pend_cnt < 32)) && (btn_wd_uptd0.flags.b.hold)) {
        //bf_mgr_trace(LOG_INFO, 0, "btn cancel hold cnt:%d", cnt);
        btn_wd_uptd0.flags.b.hold = false;
        evt_set_cs(evt_wr_grp0, (u32)&btn_wd_uptd0, evt_wd_grp0_nrm_par_upt, CS_TASK);
    }
}

ddr_code void btn_data_abort(u32 btag)
{
	bm_pl_t *bm_pl;
	btn_um_int_sts_t sts;
	btn_cmdq_t *cmd_que = &btn_wd_uptd0;
	u32 *entry = (u32 *)cmd_que->reg.base;
	u32 cmdq_mxsz = cmd_que->reg.entry_dbase.b.max_sz;
	u32 cmdq_rptr = cmd_que->reg.entry_pnter.b.rptr;
	u32 cmdq_wptr = cmd_que->wptr;

	sts.all = btn_readl(BTN_MK_INT_STS);
	if (sts.b.wr_data_group0_update) {
		while (cmdq_rptr != cmdq_wptr) {
			bm_pl = (bm_pl_t*)dma_to_btcm(entry[cmdq_rptr]);

			if (bm_pl->pl.btag == btag) {
				bm_pl->pl.btag = BTN_ABT_BTAG;
				bf_mgr_trace(LOG_ERR, 0x2ca9, "btag %d off %d de drop", btag, bm_pl->pl.du_ofst);
			}

			cmdq_rptr = (cmdq_rptr + 1) & cmdq_mxsz;
		}
	}
}

slow_code static void ipc_btn_wr_switch(volatile cpu_msg_req_t *req)
{
	bool enable_switch = (bool)req->pl;
	bool rw_type   = (bool)req->cmd.flags;
	if(!plp_trigger)
	{
    	bf_mgr_trace(LOG_INFO, 0x852d,"[Pre] 0/1 w/r:%d enable/disable:%d read_q:%d write_q:%d ",
			rw_type, enable_switch,btn_rd_uptd.flags.b.bypass,btn_wd_uptd0.flags.b.bypass);
	}

	if(rw_type == 0)//write btn queue
	{
	    if (enable_switch == true){
	        btn_de_wr_enable();
	    }else{
	        btn_de_wr_disable();
	    }
	}
    else//read btn queue
    {
		if (enable_switch == true){
	        btn_de_rd_enable();
	    }else{
	        btn_de_rd_disable();
	    }
    }
    if(!plp_trigger)
    {
    	bf_mgr_trace(LOG_INFO, 0xd425,"[After]w/r:%d  enable/disable:%d read_q:%d write_q:%d ",
        	rw_type, enable_switch,btn_rd_uptd.flags.b.bypass,btn_wd_uptd0.flags.b.bypass);

    }
}


fast_code void btn_de_wr_disable(void)
{
	btn_wd_uptd0.flags.b.bypass = true;
	btn_wd_uptd1.flags.b.bypass = true;
	btn_wd_uptd2.flags.b.bypass = false;
	if(!plp_trigger)
	{
    	bf_mgr_trace(LOG_INFO, 0xd83e, "btn_wd_uptd0.flags: 0x%x, btn_wd_uptd1.flags: 0x%x, btn_wd_uptd2.flags: 0x%x",
        	btn_wd_uptd0.flags.all, btn_wd_uptd1.flags.all, btn_wd_uptd2.flags.all);
	}
}

fast_code void btn_de_wr_enable(void)
{
	btn_wd_uptd0.flags.b.bypass = false;
	btn_wd_uptd1.flags.b.bypass = false;
	btn_wd_uptd2.flags.b.bypass = false;
	if(!plp_trigger)
	{
    	bf_mgr_trace(LOG_INFO, 0x8a7e, "btn_wd_uptd0.flags: 0x%x, btn_wd_uptd1.flags: 0x%x, btn_wd_uptd2.flags: 0x%x",
        	btn_wd_uptd0.flags.all, btn_wd_uptd1.flags.all, btn_wd_uptd2.flags.all);
	}
}

fast_code void btn_de_rd_disable(void)
{
	btn_rd_uptd.flags.b.bypass = true;
}

fast_code void btn_de_rd_enable(void)
{
	btn_rd_uptd.flags.b.bypass = false;
}

ddr_code void btn_de_wr_dump(void)
{
//	btn_cmdq_t *cmdq = &btn_wd_uptd0;
//	hdr_reg_t *reg = &cmdq->reg;
//	u32 rptr = reg->entry_pnter.b.rptr;
//	u32 *entry = (u32 *)reg->base;

	bf_mgr_trace(LOG_INFO, 0x97ef, "flag %x", btn_wd_uptd0.flags.all);

//	u32 cnt = 32;
//	while (cnt--) {
//		if (rptr == 0)
//			rptr = reg->entry_dbase.b.max_sz - 1;
//		else
//			rptr --;

//		bm_pl_t *bm_pl = (bm_pl_t *)dma_to_btcm(entry[rptr]);

//		bf_mgr_trace(LOG_INFO, 0, "[%d] dtag %d, du_ofst %d, btag %d, nvm_cmd_id %d, type_ctrl %d",
//				rptr, bm_pl->pl.dtag, bm_pl->pl.du_ofst, bm_pl->pl.btag,
//				bm_pl->pl.nvm_cmd_id, bm_pl->pl.type_ctrl);
//	}
}
#endif

#if (BTN_COM_FREE_CPU == CPU_ID)
fast_code void btn_handler_com_free(void)
{
	btn_cmdq_t *cmdq = &btn_com_free;
	hdr_reg_t *reg = &cmdq->reg;
	u32 wptr = cmdq->wptr;
	u32 rptr = reg->entry_pnter.b.rptr;
	u32 *entry = (u32 *)reg->base;

	if (wptr == rptr)
		return;

	/* TODO: handle cmf data */;
	if (rptr > wptr) {
		u32 *dtag = &entry[rptr];
		u32 cnt = reg->entry_dbase.b.max_sz;

		cnt = cnt - rptr + 1;
		evt_set_imt(evt_com_free_upt, (u32)dtag, cnt);
		reg->entry_pnter.b.rptr = 0;
	}

	if (rptr < wptr) {
		u32 *dtag = &entry[rptr];
		u32 cnt = wptr;

		cnt -= rptr;

		evt_set_imt(evt_com_free_upt, (u32)dtag, cnt);
		reg->entry_pnter.b.rptr = wptr;
	}

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

/*!
 * @brief interrupt routine for common free entry queue updated
 *
 * @return	not used
 */
fast_code void bm_isr_com_free(void)
{
	btn_um_int_sts_t sts = { .all = btn_readl(BTN_MK_INT_STS), };
	btn_um_int_sts_t ack = { .all = 0 , };

	//bf_mgr_trace(LOG_DEBUG, 0, "sts = %x", sts.all);
	if (sts.b.com_free_dtag_update) {
		btn_handler_com_free();
		ack.b.com_free_dtag_update = 1;
	}

	btn_writel(ack.all, BTN_UM_INT_STS);
}
#endif

#if (BTN_HOST_READ_CPU == CPU_ID)
extern bool bm_pop_rd_entry_list(enum rd_entry_llist_sel sel, bm_pl_t *bm_pl);
extern void btn_push_entry_rd_fw_que(bm_pl_t bm_pl);
/*!
 * @brief push a bm_pl entry to a btn queue
 *
 * @param bm_tag_push	bm payload to be pushed
 * @param reg		btn queue header register pointer
 * @param pnter_base	pointer of btn queue updated pointer
 * @param wait		true to wait for queue available when queue was full
 *
 * @return		return -1 for push fail
 */
 
static int bm_entry_push(bm_pl_t bm_tag_push, hdr_reg_t *reg, volatile void *pnter_base, bool wait);
#define BM_ENTRY_PENDING_MAX_CNT 256
slow_data_zi bm_pl_t bm_entry_pending_bm_pl[BM_ENTRY_PENDING_MAX_CNT];
slow_data_zi u32 bm_entry_pending_cnt;

fast_code void bm_pending_queue_pop_all(){
    while(bm_entry_pending_cnt){
        bm_pl_t *bm_pl_pending = &bm_entry_pending_bm_pl[bm_entry_pending_cnt - 1];
        bm_entry_pending_cnt --;
        bf_mgr_trace(LOG_WARNING, 0xada2, "re-push bm_pl %x %x, pending cnt %u", bm_pl_pending->dw0, bm_pl_pending->dw1, bm_entry_pending_cnt);
        bm_entry_push(*bm_pl_pending, &btn_host_rd.reg, &btn_host_rd.wptr, true);
    }
}

fast_data_zi u64 bm_hang_chk_evt_issue_time;
fast_data u8 bm_hang_chk_evt_issue = 0xff;
fast_data_zi u8 bm_hang_chk;
fast_data_zi rd_entry_llist_sts_data_t free_sts1;
fast_data_zi rd_entry_llist_sts_data_t drop_sts1;

fast_code void bm_queue_full_chk(){
    btn_writel(FW_RD_ADJ_FREE_DTAG_DATA_ENTRY_LLIST, RD_ENTRY_LLIST_STS_SEL);
    free_sts1.all = btn_readl(RD_ENTRY_LLIST_STS_DATA);
    btn_writel(FW_RD_ADJ_DROP_DTAG_DATA_ENTRY_LLIST, RD_ENTRY_LLIST_STS_SEL);
    drop_sts1.all = btn_readl(RD_ENTRY_LLIST_STS_DATA);
    if(free_sts1.b.rd_entry_llist_v_count + drop_sts1.b.rd_entry_llist_v_count ==0x100){
        bm_hang_chk_evt_issue_time = get_tsc_64();
        if(!bm_hang_chk){
            bm_hang_chk = true;
            evt_set_cs(bm_hang_chk_evt_issue, 0, 0, CS_TASK);
        }
    }
}

fast_code void bm_hang_chk_evt(u32 nouse0, u32 nouse1, u32 nouse2) 
{
    u64 now = get_tsc_64();
    if(now > bm_hang_chk_evt_issue_time && now - bm_hang_chk_evt_issue_time < CYCLE_PER_MS * 100){
        evt_set_cs(bm_hang_chk_evt_issue, 0, 0, CS_TASK);
        return;
    }
    
    rd_entry_llist_sts_data_t free_sts, drop_sts;
    btn_writel(FW_RD_ADJ_FREE_DTAG_DATA_ENTRY_LLIST, RD_ENTRY_LLIST_STS_SEL);
    free_sts.all = btn_readl(RD_ENTRY_LLIST_STS_DATA);
    btn_writel(FW_RD_ADJ_DROP_DTAG_DATA_ENTRY_LLIST, RD_ENTRY_LLIST_STS_SEL);
    drop_sts.all = btn_readl(RD_ENTRY_LLIST_STS_DATA);
    if(free_sts.b.rd_entry_llist_v_count + drop_sts.b.rd_entry_llist_v_count == 0x100){
        bm_pl_t *bm_pl = &bm_entry_pending_bm_pl[bm_entry_pending_cnt];
        if (bm_pop_rd_entry_list(FW_RD_ADJ_FREE_DTAG_DATA_ENTRY_LLIST, bm_pl)) {
            bm_entry_pending_cnt ++;
            bf_mgr_trace(LOG_WARNING, 0x1c3d, "pop adj free list %x %x, pending cnt %u", bm_pl->dw0, bm_pl->dw1, bm_entry_pending_cnt);
        } else if (bm_pop_rd_entry_list(FW_RD_ADJ_DROP_DTAG_DATA_ENTRY_LLIST, bm_pl)) {
            bm_entry_pending_cnt ++;
            bf_mgr_trace(LOG_WARNING, 0x1744, "pop adj drop list %x %x, pending cnt %u", bm_pl->dw0, bm_pl->dw1, bm_entry_pending_cnt);
        }
        bm_pending_queue_pop_all();
        bm_queue_full_chk();
        evt_set_cs(bm_hang_chk_evt_issue, 0, 0, CS_TASK);
    }else{
        bm_hang_chk = false;
    }
}

static fast_code int bm_entry_push(bm_pl_t bm_tag_push, hdr_reg_t *reg, volatile void *pnter_base, bool wait)
{

	#ifdef UNMAP_DATA_LOSS_WORKAROUND
	fw_push_rd_entry0_ptrs_t ptr = { .all = btn_readl(FW_PUSH_RD_ENTRY0_PTRS), };
	u16 rptr = ptr.b.fw_push_rd_entry0_rptr;
	#else
	u16 rptr = *(u32 *) pnter_base;
	#endif
	u16 wptr_nxt;
	bm_pl_t *base;

	wptr_nxt = (reg->entry_pnter.b.wptr + 1) & reg->entry_dbase.b.max_sz;

	if (wptr_nxt == rptr && wait == false) { /* ring buf is full */
		bf_mgr_trace(LOG_WARNING, 0xadeb, "ring buf is full, wptr_nxt %d rptr %d wait %d",
			wptr_nxt, rptr, wait);
		return -1;
	}


#ifdef While_break
	u64 start = get_tsc_64();
#endif

    u32 pop_cnt = 0;

	while (wptr_nxt == rptr) {
#if BTN_DATA_IN_CPU == CPU_ID
		btn_de_rd_grp0();
#endif
#if (BTN_COM_FREE_CPU == CPU_ID)
		bm_isr_com_free();
#endif
#if (BTN_CORE_CPU == CPU_ID)
		bm_handle_rd_err();
#endif
#if (BTN_CORE_CPU == CPU_ID)
		extern bool cmd_proc_err_cmpl_cmd(void);
		cmd_proc_err_cmpl_cmd();
#endif
		if (plp_trigger) {
			bf_mgr_trace(LOG_ALW, 0xc76e, "plp force break");
			return -1;
		}
		reg->entry_pnter.all = readl((void *) (reg->mmio + 4));
		/* FIXME: avoid busy wait */
		rptr = reg->entry_pnter.b.rptr;

        if((time_elapsed_in_ms(start)) > (100 + 10 * pop_cnt) && bm_entry_pending_cnt < BM_ENTRY_PENDING_MAX_CNT){
            // maybe bm entry hang, 5s
            
            bm_pl_t *bm_pl = &bm_entry_pending_bm_pl[bm_entry_pending_cnt];
            if (bm_pop_rd_entry_list(FW_RD_ADJ_FREE_DTAG_DATA_ENTRY_LLIST, bm_pl)) {
                bm_entry_pending_cnt ++;
                bf_mgr_trace(LOG_WARNING, 0x3a39, "pop adj free list %x %x, pending cnt %u", bm_pl->dw0, bm_pl->dw1, bm_entry_pending_cnt);
                pop_cnt ++;
            } else if (bm_pop_rd_entry_list(FW_RD_ADJ_DROP_DTAG_DATA_ENTRY_LLIST, bm_pl)) {
                bm_entry_pending_cnt ++;
                bf_mgr_trace(LOG_WARNING, 0x5460, "pop adj drop list %x %x, pending cnt %u", bm_pl->dw0, bm_pl->dw1, bm_entry_pending_cnt);
                pop_cnt ++;
            }
        }

#ifdef While_break
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
	}

    u32 time_ms = (time_elapsed_in_ms(start));
    if(time_ms){
        bf_mgr_trace(LOG_WARNING, 0x69e2, "time_ms %u", time_ms);
    }

	base = reg->base;
	base[reg->entry_pnter.b.wptr] = bm_tag_push;
	reg->entry_pnter.b.wptr = wptr_nxt;

	writel(reg->entry_pnter.all, (void *) (reg->mmio + 4));

	return 0;
}

fast_code void bm_rd_de_push(bm_pl_t *bm_pl)
{
    bm_entry_push(*bm_pl, &btn_host_rd.reg, &btn_host_rd.wptr, true);
    bm_pending_queue_pop_all();
    bm_queue_full_chk();
}

fast_code void bm_rd_dtag_commit(u32 du_ofst, u32 btag, dtag_t dtag)
{
	btn_cmd_t *bcmd = btag2bcmd(btag);
	bm_pl_t bm_tag;

	bm_tag.all = 0;

	bm_tag.pl.du_ofst    = du_ofst;
	bm_tag.pl.btag       = btag;
	bm_tag.pl.nvm_cmd_id = bcmd->dw0.b.nvm_cmd_id;
	bm_tag.pl.dtag       = dtag.dtag;

	//bf_mgr_trace(LOG_DEBUG, 0, "Du_ofst(%d) btag(%d) cid(%d) Dtag(%d) type(%d)", du_ofst,
		//     btag, bcmd->dw0.b.nvm_cmd_id, dtag.b.dtag, dtag.b.type);

#if !defined (RAMDISK_FULL)
	if (dtag.dtag != RVTAG_ID && dtag.dtag != EVTAG_ID)
		bm_tag.pl.type_ctrl = BTN_NVME_QID_TYPE_CTRL_NRM; //After data transfer is done, recycle the DTAG to "com-free-DTAG_llist"
	else
#endif
		bm_tag.pl.type_ctrl = BTN_NVME_QID_TYPE_CTRL_DROP; //After data transfer is done, drop the DTAG

	bm_rd_de_push(&bm_tag);
}
//extern struct ns_array_manage *ns_array_menu;//joe add 20200914
extern u32 host_unc_err_cnt;
fast_code void bm_err_commit(u32 du_ofst, u32 btag)
{
	dtag_t dtag = { .dtag = EVTAG_ID };
	lda_t lda;
       //joe add 20200820
	#if (Synology_case)
	host_unc_err_cnt ++;// for Synology case log page C0h Host UNC Error Count (1 cnt for 1 cmd)
	#endif
    u64 slba=bcmd_get_slba(btag2bcmd(btag));
	//lda = LBA_TO_LDA(bcmd_get_slba(btag2bcmd(btag)));
	lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz));//joe add sec size 20200820

	bf_mgr_trace(LOG_ERR, 0x6808, "ins EVTAG %d-%d lda %x",
			btag, du_ofst, lda);

	bm_rd_dtag_commit(du_ofst, btag, dtag);
}

#ifdef RD_FAIL_GET_LDA
ddr_code lda_t host_rd_get_lda(u16 btag, u16 du_ofst, pda_t pda)
{
    u64 slba = 0;
	lda_t lda = 0;

    slba = bcmd_get_slba(btag2bcmd(btag));
    lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)) + du_ofst;  //slda + du_offset
    bf_mgr_trace(LOG_INFO, 0x4dd9, "[LDA] HOST lda[0x%x] pda[0x%x] btag[0x%x]", lda, pda, btag);

    return lda;
}

extern lda_t ucache_srch_ua_lda(u16 ofst);
ddr_code lda_t ua_rd_get_lda(u16 btag, u16 du_ofst, pda_t pda)
{	
	lda_t lda = ucache_srch_ua_lda(du_ofst);
	bf_mgr_trace(LOG_INFO, 0xc904, "[LDA] Other lda[0x%x] pda[0x%x] btag[0x%x]", lda, pda, btag);
	return lda;
}


ddr_code void ipc_host_rd_get_lda(volatile cpu_msg_req_t *req)
{
	rd_get_lda_t *lda_entry = (rd_get_lda_t *) req->pl;
	if( lda_entry->btag == ua_btag )
		ua_rd_get_lda(lda_entry->btag, lda_entry->du_ofst, lda_entry->pda);
	else
		host_rd_get_lda(lda_entry->btag, lda_entry->du_ofst, lda_entry->pda);
}
#endif

#ifdef ERRHANDLE_ECCT   //register ECCT
ddr_code void rc_reg_ecct(bm_pl_t *bm_pl, u8 type)
{
    u32 dtag_idx = 0;
    //u64 slba = 0;
	lda_t lda = 0;
    io_meta_t *meta;

    switch(type)
    {
        case HOST_ECCT_TYPE:
            lda = host_rd_get_lda(bm_pl->pl.btag, bm_pl->pl.du_ofst, 0);
            break;
        case GC_ECCT_TYPE:
            dtag_idx = bm_pl->pl.du_ofst;
            dtag_idx = (DTAG_CNT_PER_GRP * bm_pl->pl.nvm_cmd_id) + dtag_idx;
            //dtag_idx = DTAG_GRP_IDX_TO_IDX(dtag_idx,  pl.pl.nvm_cmd_id);
            lda = gc_lda_list[dtag_idx];
            //bf_mgr_trace(LOG_INFO, 0, "[ECCT]gc rc_ecct_reg, lda: 0x%x, rc_ecct_cnt: %d, dtag_idx: 0x%x", lda, rc_ecct_cnt, dtag_idx);
            break;
        default:
        	if(bm_pl->pl.btag == ua_btag)
            {    
            	lda = ua_rd_get_lda(bm_pl->pl.btag, bm_pl->pl.du_ofst, 0);
            	break;
            }
            meta = (bm_pl->pl.dtag & DTAG_IN_DDR_MASK) ? (io_meta_t *)shr_ddtag_meta : (io_meta_t *)shr_dtag_meta;
            dtag_idx = bm_pl->pl.dtag & 0x3FFFFF;
            lda = (u64)meta[dtag_idx].lda;
            //bf_mgr_trace(LOG_INFO, 0, "[ECCT]other rc_ecct_reg, lda: 0x%x, rc_ecct_cnt: %d", lda, rc_ecct_cnt);
            break;
    }

    if(bm_pl->pl.type_ctrl == 0x3F)
    {
        rc_ecct_info[rc_ecct_cnt].source = ECC_REG_WHCRC;
    }
    else if(bm_pl->pl.type_ctrl == 0x38)
    {
       rc_ecct_info[rc_ecct_cnt].source = ECC_REG_DFU;
    }
    else if(bm_pl->pl.type_ctrl == 0x3A)
    {
       rc_ecct_info[rc_ecct_cnt].source = ECC_REG_BLANK;
    }
    else
    {
        rc_ecct_info[rc_ecct_cnt].source = ECC_REG_RECV;
    }

    bf_mgr_trace(LOG_INFO, 0x754f, "[ECCT]rc_ecct_reg, type[%d] lda[0x%x] source[%d] rc_ecct_cnt[%d]", type, lda, rc_ecct_info[rc_ecct_cnt].source, rc_ecct_cnt);

    rc_ecct_info[rc_ecct_cnt].lba       = (u64)lda;
    rc_ecct_info[rc_ecct_cnt].total_len = 0;
    rc_ecct_info[rc_ecct_cnt].type      = VSC_ECC_rc_reg;

    memset(bm_pl, 0, sizeof(bm_pl_t));

    if(rc_ecct_cnt >= MAX_RC_REG_ECCT_CNT - 1)
    {
        rc_ecct_cnt = 0;
        ECC_Table_Operation(&rc_ecct_info[MAX_RC_REG_ECCT_CNT-1]);
    }
    else
    {
        rc_ecct_cnt++;
        ECC_Table_Operation(&rc_ecct_info[rc_ecct_cnt-1]);
    }
}


#endif

fast_code void bm_radj_push_rel(bm_pl_t *pl, u32 count)
{
	u32 i;
	for (i = 0; i < count; i++) {
		dtag_t pl_dtag = {.dtag = pl[i].pl.dtag,};
		sys_assert(pl_dtag.b.type == 0); // it's not HMB
		bm_rd_dtag_commit(pl[i].pl.du_ofst, pl[i].pl.btag, pl_dtag);
	}
}

#ifdef NCL_RETRY_PASS_REWRITE
ddr_code void retry_get_lda_do_rewrite(bm_pl_t *bm_pl)
{
    //u8 rewrite_cnt = 0;
    u8 i;
    u16 hcrc = 0;
    u64 slba = bcmd_get_slba(btag2bcmd(bm_pl->pl.btag));
    lda_t lda = 0;
    void* curr_data;
    void* bm_pl_data;
    dtag_t curr_dtag;
    dtag_t pl_dtag = { .dtag = bm_pl->pl.dtag};
    //io_meta_t *pl_meta = (pl_dtag.b.in_ddr) ? shr_ddtag_meta : shr_dtag_meta;
    io_meta_t *pl_meta = shr_ddtag_meta;
    io_meta_t *curr_meta = shr_ddtag_meta;
    u8 lba_cnt_per_lba = 4096 / host_sec_size;

    sys_assert((bm_pl->pl.dtag != 0));

    if(rd_rew_info[rd_rewrite_ptr].flag == 0X5A5A)
    {
        lda = ((slba) >> (LDA_SIZE_SHIFT - host_sec_bitz)) + bm_pl->pl.du_ofst;  //slda + du_offset

        curr_dtag.b.dtag = rd_rew_start_dtag + rd_rewrite_ptr;
        curr_dtag.b.in_ddr = 1;
        //curr_dtag.b.dtag = shr_gc_ddtag_start + rd_rewrite_ptr;
        memset(&rd_rew_info[rd_rewrite_ptr], 0, sizeof(retry_rewrite_t));
        rd_rew_info[rd_rewrite_ptr].bm_pl.all = bm_pl->all;
        rd_rew_info[rd_rewrite_ptr].bm_pl.pl.btag = 0;
        //rd_rew_info[rd_rewrite_ptr].bm_pl.pl.du_ofst = bm_pl->pl.du_ofst;
        rd_rew_info[rd_rewrite_ptr].bm_pl.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
        //rd_rew_info[rd_rewrite_ptr].bm_pl.pl.nvm_cmd_id = 0;
        rd_rew_info[rd_rewrite_ptr].bm_pl.pl.dtag = curr_dtag.dtag;
        rd_rew_info[rd_rewrite_ptr].lda = lda;
        rd_rew_info[rd_rewrite_ptr].flag = rd_rewrite_ptr;

        curr_data = (void*)ddtag2mem(curr_dtag.b.dtag);
        bm_pl_data = (void*)ddtag2mem(pl_dtag.b.dtag);
        memcpy(curr_data, bm_pl_data, DTAG_SZE);  //copy data
        memcpy(&curr_meta[curr_dtag.b.dtag], &pl_meta[pl_dtag.b.dtag],  sizeof(struct du_meta_fmt));  //copy meta
        curr_meta[curr_dtag.b.dtag].lda = lda;

        //for (i = 0; i < NR_LBA_PER_LDA; i++)
        for (i = 0; i < lba_cnt_per_lba; i++)
        {
            hcrc = btn_hcrc_read(pl_dtag, i);
            btn_hcrc_write(curr_dtag, i, hcrc);  //copy hcrc buffer
            //hcrc = btn_hcrc_read(curr_dtag, i);
            //bf_mgr_trace(LOG_INFO, 0, "[DBG]Retry rewrite: lda: %d, hcrc[%d]: 0x%x", lda, i, hcrc);
        }

        //copy pi buffer
        #if defined(HMETA_SIZE)
        if(host_du_fmt == DU_FMT_USER_4K_HMETA)
        {
            eccu_du_fmt_reg0_t du_fmt_reg0;
            eccu_du_fmt_reg1_t du_fmt_reg1;
            writel(host_du_fmt, (void*)(0xC000001C)); //ECCU_DU_FMT_SEL_REG
            du_fmt_reg0.all = readl((void*)(0xC0000020)); //ECCU_DU_FMT_REG0
            du_fmt_reg1.all = readl((void*)(0xC0000024)); //ECCU_DU_FMT_REG1
            if(du_fmt_reg1.b.pi_sz != 0)
            {
                u8 j;
                u32 *pl_pi_buf = (u32*)((btn_readl(D_BASE_D_DTAG_HMETA) << 5) + DDR_BASE);
                u32 *curr_pi_buf = (u32*)((btn_readl(D_BASE_D_DTAG_HMETA) << 5) + DDR_BASE);
                pl_pi_buf = &pl_pi_buf[pl_dtag.b.dtag * du_fmt_reg0.b.du2host_ratio * 2];
                curr_pi_buf = &curr_pi_buf[curr_dtag.b.dtag * du_fmt_reg0.b.du2host_ratio * 2];
                for(j = 0; j < du_fmt_reg0.b.du2host_ratio; j++)
                {
                    curr_pi_buf[j * 2] = pl_pi_buf[j * 2];
                    curr_pi_buf[(j * 2) + 1] = pl_pi_buf[(j * 2) + 1];
                }
            }
        }
        #endif

        bf_mgr_trace(LOG_INFO, 0x942b, "[REW]rd rewrite: lda: 0x%x, dtag_id: 0x%x, rewrite_ptr: %d", lda, curr_dtag.b.dtag, rd_rewrite_ptr);

        if(rd_rewrite_ptr >= (DDR_RD_REWRITE_TTL_CNT-1))
        {
            rd_rewrite_ptr = 0;
            cpu_msg_issue(CPU_BE - 1, CPU_MSG_RETRY_REWRITE, 0, (u32)&rd_rew_info[DDR_RD_REWRITE_TTL_CNT-1]);
        }
        else
        {
            rd_rewrite_ptr++;
            cpu_msg_issue(CPU_BE - 1, CPU_MSG_RETRY_REWRITE, 0, (u32)&rd_rew_info[rd_rewrite_ptr-1]);
        }
    }
    else
    {
        //bf_mgr_trace(LOG_INFO, 0, "[REW]rd rewrite: rew dtag is busy");
        if(rd_rewrite_ptr >= (DDR_RD_REWRITE_TTL_CNT-1))
        {
            rd_rewrite_ptr = 0;
        }
        else
        {
            rd_rewrite_ptr++;
        }
        return;
    }
    //return;
}

#endif
#endif

#if (BTN_FREE_WRITE_CPU == CPU_ID)
fast_code void bm_free_wr_load(dtag_t *dtags, u32 count)
{
	btn_cmdq_t *q = &btn_free_wr;
	hdr_reg_t *reg = &q->reg;
	u32 wptr = reg->entry_pnter.b.wptr; // FW maintains write pointer
	volatile u32 *rptr = &q->wptr; // HW maintains read pointer
	u32 *entry = (u32 *)reg->base;
	u32 dis;
	u32 i;

	do {
		if (wptr == *rptr) {
			dis = FREE_WR_ENTRY_CNT - 1;
		} else if (wptr < *rptr) {
			dis = *rptr - wptr - 1;
		} else  {
			dis = FREE_WR_ENTRY_CNT - wptr - 1;
			dis += *rptr;
		}
	} while (dis < count);

	for (i = 0; i < count; i++) {
		u32 idx = (wptr + i) & FREE_WR_ENTRY_MSK;
		entry[idx] = dtags[i].dtag;
	}

	reg->entry_pnter.b.wptr = (wptr + count) & FREE_WR_ENTRY_MSK;

	writel(reg->entry_pnter.all, (void *)(reg->mmio + 4));
}

fast_code u32 bm_free_wr_get_wptr(void)
{
	btn_cmdq_t *q = &btn_free_wr;
	hdr_reg_t *reg = &q->reg;

	return reg->entry_pnter.b.wptr;
}

fast_code u32 bm_free_wr_get_rptr(void)
{
	btn_cmdq_t *q = &btn_free_wr;

	return q->wptr;
}

fast_code void bm_free_wr_set_wptr(u32 wptr)
{
	btn_cmdq_t *q = &btn_free_wr;
	hdr_reg_t *reg = &q->reg;

	reg->entry_pnter.b.wptr = wptr;

	btn_writel(reg->entry_pnter.all, FW_ISU_WD_DTAG_PTRS);
}

fast_code u32 bm_abort_free_wr_load(dtag_t dtag, u32 count)
{
	u32 i;
	u32 t;
	dtag_t dtags[32];

	abort_dtag_cnt += count;

	if (abort_dtag_cnt == 0)
		return 0;

	t = min(abort_dtag_cnt, 32);
	for (i = 0; i < t; i++)
		dtags[i] = dtag;

	bm_free_wr_load(dtags, t);

	abort_dtag_cnt -= t;
	return t;
}
#endif

/*!
 * @brief default commone free handler
 *
 * @param param		not used
 * @param payload	BM payload list
 * @param count		list length
 *
 * @return		not used
 */
fast_code UNUSED void default_common_free_handler(u32 param, u32 payload, u32 count)
{
	dtag_put_bulk(DTAG_T_MAX, count, (dtag_t *) payload);
}

#if defined(MPC)
fast_code void ipc_bm_err_commit(volatile cpu_msg_req_t *req)
{
	bm_pl_t *bm_pl = (bm_pl_t *) req->pl;
	u8 tx = req->cmd.tx;

	bm_err_commit(bm_pl->pl.du_ofst, bm_pl->pl.btag);
	cpu_msg_sync_done(tx);
}
#endif

#ifdef ERRHANDLE_ECCT   //register ECCT
fast_code void ipc_rc_reg_ecct(volatile cpu_msg_req_t *req)
{
    tEcctReg *ecct_reg = (tEcctReg *) req->pl;
    //u8 tx = req->cmd.tx;

    rc_reg_ecct(&ecct_reg->bm_pl, (u8)req->cmd.flags);
    //cpu_msg_sync_done(tx);
}
#endif
#ifdef NCL_RETRY_PASS_REWRITE
fast_code void ipc_retry_get_lda_do_rewrite(volatile cpu_msg_req_t *req)
{
     bm_pl_t *bm_pl = (bm_pl_t *) req->pl;
     retry_get_lda_do_rewrite(bm_pl);
     //return;
}
#endif

init_code void btn_datq_init(void)
{
#if (BTN_DATA_IN_CPU == CPU_ID)
	poll_irq_register(VID_WR_DATA_GRP0, btn_data_in_isr);
	btn_enable_isr(WR_DATA_GROUP0_UPDATE_MASK);
	poll_irq_register(VID_WR_DATA_GRP1, btn_data_in_isr);
	btn_enable_isr(WR_DATA_GROUP1_UPDATE_MASK);
	poll_irq_register(VID_RD_DATA_GRP0, btn_data_in_isr);
	btn_enable_isr(RD_DATA_GROUP0_UPDATE_MASK);
    evt_register(btn_wr_grp0_handle,0,&evt_wr_grp0);
    cpu_msg_register(CPU_MSG_BTN_WR_SWITCH, ipc_btn_wr_switch);
#if defined(MPC)
	cpu_msg_register(CPU_MSG_RD_ERR, ipc_bm_err_commit);
#endif
#ifdef ERRHANDLE_ECCT   //register ECCT
    cpu_msg_register(CPU_MSG_RC_REG_ECCT, ipc_rc_reg_ecct);
#endif

#ifdef RD_FAIL_GET_LDA
    cpu_msg_register(CPU_MSG_HOST_GET_LDA, ipc_host_rd_get_lda);
#endif

#ifdef NCL_RETRY_PASS_REWRITE
    u8 idx;
    rd_rewrite_ptr = 0;
    rd_rew_start_dtag = DDR_RD_REWRITE_START;
    memset(rd_rew_info, 0, DDR_RD_REWRITE_TTL_CNT * sizeof(retry_rewrite_t));
    for(idx = 0; idx < DDR_RD_REWRITE_TTL_CNT; idx++)
    {
        rd_rew_info[idx].bm_pl.pl.dtag = (rd_rew_start_dtag + idx) | DTAG_IN_DDR_MASK;
        rd_rew_info[idx].flag = 0x5A5A;
        rd_rew_info[idx].lda = INV_U32;
        //memset(dtag2mem(rd_rew_info[idx].bm_pl.pl.dtag), 0, DTAG_SZE)
        //memset(&shr_ddtag_meta[rd_rew_info[idx].bm_pl.pl.dtag], 0, sizeof(struct du_meta_fmt))
    }
    cpu_msg_register(CPU_MSG_RETRY_GET_LDA_REWRITE, ipc_retry_get_lda_do_rewrite);
    //bf_mgr_trace(LOG_INFO, 0, "[DBG]btn_datq_init: rd rewrite done");
#endif
#endif

#if (BTN_COM_FREE_CPU == CPU_ID)
	poll_irq_register(VID_COM_FREE, bm_isr_com_free);
	btn_enable_isr(COM_FREE_DTAG_UPDATE_MASK);
	evt_register(default_common_free_handler, 0, &evt_com_free_upt);
    evt_register(bm_hang_chk_evt, 0, &bm_hang_chk_evt_issue);
#endif
}

ps_code void btn_grp_enable(grp_t grp, bool enable)
{
	u32 regs = 0;
	u32 val;
	u32 msk;

	switch (grp) {
	case BM_GRP_DTAG:
		regs = DTAG_LLIST_CTRL_STATUS;
		msk = DATA_TAG_LLIST_EN_MASK;
		break;
	case BM_GRP_WR_DATA:
		regs = WD_ENTRY_LLIST_CTRL_STATUS;
		msk = WD_ENTRY_LLIST_EN_MASK;
		break;
	case BM_GRP_RD_DATA:
		regs = RD_ENTRY_LLIST_CTRL_STATUS;
		msk = RD_ENTRY_LLIST_EN_MASK;
		break;
	default:
		msk = 0;
		panic("grp_enable: no such group\n");
	}

	val = btn_readl(regs);
	if (enable) {
		if (val & msk) {
			val &= ~msk;
			btn_writel(val, regs);
		}
		val |= msk;
	} else
		val &= ~msk;

	btn_writel(val, regs);
}

fast_code void btn_datq_resume(void)
{
#if (BTN_CORE_CPU == CPU_ID)
	btn_grp_enable(BM_GRP_DTAG, false);
	btn_grp_enable(BM_GRP_RD_DATA, false);
	btn_grp_enable(BM_GRP_WR_DATA, false);

	btn_grp_enable(BM_GRP_DTAG, true);
	btn_grp_enable(BM_GRP_RD_DATA, true);
	btn_grp_enable(BM_GRP_WR_DATA, true);
#endif

	/* enable stream buffer and clear DLINK was*/
	dtag_llist_ctrl_status_t ctrl_sts;

	ctrl_sts.all = btn_readl(DTAG_LLIST_CTRL_STATUS);
	ctrl_sts.b.rd_strmbuf_dtag_en = 1;
	ctrl_sts.b.rd_strmbuf_dtag_only = 1;
	ctrl_sts.b.data_tag_llist_full = 0;
	btn_writel(ctrl_sts.all, DTAG_LLIST_CTRL_STATUS);

	/* fw -> nvme */
#if (BTN_HOST_READ_CPU == CPU_ID)
	btn_hdr_init(&btn_host_rd.reg,
			&btn_host_rd_entry[0], HST_RD_ENTRY_CNT,
			&btn_host_rd.wptr, FW_PUSH_RD_ENTRY0_BASE);
#endif

	/* nvme/ncb -> fw */
#if (BTN_COM_FREE_CPU == CPU_ID)
	btn_hdr_init(&btn_com_free.reg,
			&btn_com_free_entry[0], COM_FREE_ENTRY_CNT,
			&btn_com_free.wptr, BTN_COM_FREE_DTAG_DBASE);
#endif

#if (BTN_DATA_IN_CPU == CPU_ID)
	pool_init(&btn_wd_de_pool, (void *)_btn_wd_data_entry,
		  sizeof(_btn_wd_data_entry), sizeof(btn_data_entry_t),
		  WD_DES_CNT);
	pool_init(&btn_rd_de_pool, (void *)_btn_rd_data_entry,
		  sizeof(_btn_rd_data_entry), sizeof(btn_data_entry_t),
		  RD_DES_CNT);

	pool_init(&btn_wd1_de_pool, (void *)_btn_wd1_data_entry,
		  sizeof(_btn_wd1_data_entry), sizeof(btn_data_entry_t),
		  WD_DES_CNT);

	btn_hdr_init(&btn_wd_surc.reg,
		    &btn_wd_surc_entry[0], ADDR_CNT,
		    &btn_wd_surc.wptr, BTN_WD_ADRS_SURC0_BASE);
	btn_hdr_init(&btn_wd_uptd0.reg,
		    &btn_wd_uptd0_entry, ADDR_CNT,
		    &btn_wd_uptd0.wptr, BTN_WD_ADRS0_UPDT_BASE);
	btn_hdr_init(&btn_wd_uptd1.reg,
		    &btn_wd_uptd1_entry, ADDR_CNT,
		    &btn_wd_uptd1.wptr, BTN_WD_ADRS0_UPDT1_BASE);
	btn_hdr_init(&btn_wd_uptd2.reg,
		    &btn_wd_uptd2_entry, ADDR_CNT,
		    &btn_wd_uptd2.wptr, BTN_WD_ADRS0_UPDT2_BASE);

	btn_wd_uptd0.flags.all = 0;
	btn_wd_uptd1.flags.all = 0;
	btn_wd_uptd2.flags.all = 0;

	int i;
	for (i = 0; i < ADDR_CNT - 1; i ++) {
		btn_data_entry_t *e = pool_get(&btn_wd_de_pool, 0);
		u32 addr = btcm_to_dma(e);
		hdr_surc_push(&btn_wd_surc.reg, btn_wd_surc.wptr, addr);
	}

	btn_hdr_init(&btn_wd1_surc.reg,
		    &btn_wd1_surc_entry[0], HMB_ADDR_CNT,
		    &btn_wd1_surc.wptr, BTN_WD_ADRS_SURC1_BASE);
	btn_hdr_init(&btn_wd1_uptd.reg,
		    &btn_wd1_uptd_entry, HMB_ADDR_CNT,
		    &btn_wd1_uptd.wptr, BTN_WD_ADRS1_UPDT_BASE);
	for (i = 0; i < HMB_ADDR_CNT; i ++) {
		btn_data_entry_t *e = pool_get(&btn_wd1_de_pool, 0);
		u32 addr = btcm_to_dma(e);
		hdr_surc_push(&btn_wd1_surc.reg, btn_wd1_surc.wptr, addr);
	}

	btn_hdr_init(&btn_rd_surc.reg,
		    &btn_rd_surc_entry[0], ADDR_CNT,
		    &btn_rd_surc.wptr, BTN_RD_ADRS_SURC0_BASE);
	btn_hdr_init(&btn_rd_uptd.reg,
		    &btn_rd_uptd_entry, ADDR_CNT,
		    &btn_rd_uptd.wptr, BTN_RD_ADRS0_UPDT_BASE);

	for (i = 0; i < ADDR_CNT - 1; i ++) {
		btn_data_entry_t *e = pool_get(&btn_rd_de_pool, 0);
		u32 addr = btcm_to_dma(e);
		hdr_surc_push(&btn_rd_surc.reg, btn_rd_surc.wptr, addr);
	}
#endif

#if (BTN_FREE_WRITE_CPU == CPU_ID)
	btn_hdr_init(&btn_free_wr.reg,
			&btn_free_wr_entry[0], FREE_WR_ENTRY_CNT,
			&btn_free_wr.wptr, FW_ISU_WD_DTAG_BASE);
#endif

#if BTN_DATA_IN_CPU == CPU_ID && defined(HMB_DTAG)
	btn_hdr_init(&btn_rd_hmb_cmdq.reg,
		    &_btn_rd_hmb_data_entry[0], HMB_DES_CNT,
		    &btn_rd_hmb_cmdq.wptr, FW_RD_HMB_REQST_BASE);

	btn_hdr_init(&btn_wr_hmb_cmdq.reg,
		    &_btn_wr_hmb_data_entry[0], HMB_DES_CNT,
		    &btn_wr_hmb_cmdq.wptr, FW_WR_HMB_REQST_BASE);
#endif

#if (BTN_CORE_CPU == CPU_ID)
	btn_auto_updt_enable_t reg = {
		.all = btn_readl(BTN_AUTO_UPDT_ENABLE),
	};

	reg.b.btn_wd_auto_updt_en = ~0; /* enable all */
	reg.b.btn_rd_auto_updt_en = ~0; /* enable all */
	reg.b.com_free_auto_updt_en = 1;
	btn_writel(reg.all, BTN_AUTO_UPDT_ENABLE);
#endif

#if (BTN_DATA_IN_CPU == CPU_ID)
	poll_mask(VID_WR_DATA_GRP0);
	poll_mask(VID_WR_DATA_GRP1);
	poll_mask(VID_RD_DATA_GRP0);
	poll_mask(VID_FW_RD_GRP0);
#endif

#if (BTN_COM_FREE_CPU == CPU_ID)
	poll_mask(VID_COM_FREE);
#endif
}

/*! @} */
