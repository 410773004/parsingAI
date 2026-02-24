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
/*! \file ftl_raid.c
 * @brief define raid behavior in ftl core, included parity flush, recover, and raid buffer switch
 *
 * \addtogroup fcore
 * \defgroup raid
 * \ingroup fcore
 * @{
 *
 */
#include "../ftl/ftlprecomp.h"
#include "sect.h"
#include "eccu.h"
#include "event.h"
#include "string.h"
#include "stdlib.h"
#include "bitops.h"
#include "bf_mgr.h"
#include "ftl_core.h"
#include "ftl_raid.h"
#include "ftltype.h"
#include "ncl_cmd.h"
#include "console.h"
#include "ncl_exports.h"
#include "pstream.h"
#include "nand_cfg.h"
#include "mpc.h"
#include "rdisk.h"
#include "ncl_raid.h"
#include "ftl_remap.h"
#include "ipc_api.h"
#include "idx_meta.h"
#include "scheduler.h"
#include "btn_export.h"
#include "GList.h"


#define __FILEID__ ftlraid
#include "trace.h"

#define RAID_LEVEL LOG_ALW

share_data_zi volatile bool FTL_INIT_DONE;

share_data_zi volatile rc_host_queue_t shr_rc_host_queue;

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
fast_data dtag_t raid_pad_dtag;
share_data_zi volatile u16 stripe_pdone_cnt[MAX_STRIPE_ID];	///< pdone counter
#endif

fast_data_zi pool_t cwl_stripe_pool;			///< stripe pool
fast_data_zi cwl_stripe_t cwl_stripe[MAX_CWL_STRIPE];	///< core wordline stripe
fast_data_zi raid_id_mgr_t raid_id_mgr;			///< raid id manager
fast_data_zi raid_bank_mgr_t raid_bank_mgr;		///< raid bank manager
fast_data_zi pbt_dtag_mgr_t pbt_dtag_mgr;			///< raid id manager

fast_data_ni static raid_sched_mgr_t raid_sched_mgr;	///< raid schedule manager
fast_data_ni static struct ncl_cmd_t sched_ncl_cmd;	///< schedule ncl command
fast_data_ni static pda_t sched_pda[XLC * MAX_PLANE];	///< schedule bm pda
fast_data_ni static bm_pl_t sched_bm[XLC * MAX_PLANE];	///< schedule bm payload
fast_data_ni static struct info_param_t sched_info[XLC * MAX_PLANE];	///< schedule info

slow_data_ni static raid_correct_t raid_correct;		///< raid correct
slow_data_ni static struct ncl_cmd_t rc_ncl_cmd;		///< recovery ncl command
slow_data_ni static bm_pl_t rc_bm_pl[DU_CNT_PER_RAID_XOR_CMD];	///< recovery bm payload
slow_data_ni static struct info_param_t rc_info[DU_CNT_PER_RAID_XOR_CMD];	///< recovery info

ddr_sh_data u8 cmf_idx;

extern u16 ua_btag;
extern volatile pbt_resume_t *pbt_resume_param;
#ifdef ERRHANDLE_ECCT
//extern lda_t rc_lda;
share_data_zi void *shr_ddtag_meta;
share_data_zi void *shr_dtag_meta;
share_data_zi volatile u32 shr_gc_ddtag_start;
//share_data u8 host_sec_bitz;
share_data_zi lda_t *gc_lda_list;  //gc lda list from p2l
//share_data stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
//share_data tEcctReg ecct_pl_info[MAX_RC_REG_ECCT_CNT];
//share_data u8 rc_ecct_cnt;
//share_data u8 ecct_pl_cnt;
#endif

typedef struct _raid_chk_t {
	dtag_t dtag[2];	///< one dtag for org data, one dtag for raid recovery
    struct du_meta_fmt *meta;
	u32 meta_idx_base;
	bm_pl_t bm_pl[2];
	struct info_param_t info[2];

	pda_t pda;	///< pda to check
	spb_id_t spb;	///< spb to check
	u16	page;	///< page to check
	u16 chk_cnt;	///< check countd
	u16 interval;	///< check page interval
	u8 spb_type;	///< SLC or XLC
	u32 ttl_fail;	///< total check fail count
	u32 ttl_pass;	///< total check pass count
	u32 ttl_meta_fail;	///< total check fail count
	u32 ttl_meta_pass;	///< total check pass count
	u32 ttl_err;	///< total error count(read error or raid recovery error)
	u32 start_time;
} raid_chk_t;

share_data volatile enum du_fmt_t host_du_fmt;
slow_data_ni static raid_chk_t raid_chk;
slow_data_ni static rc_req_t raid_chk_req;
slow_data_ni static struct ncl_cmd_t raid_chk_cmd;

/*!
 * @brief ncl raid id xor cmpl handler
 *
 * @param parm		not used
 * @param payload		ncl raid id cmpl entry addr
 * @param count		ncl raid id cmpl entry cnt
 *
 * @return	not used
 */
void raid_xor_cmpl_handle(u32 param, u32 payload, u32 count);

/*!
 * @brief resume pending schedule request
 *
 * @return	not used
 */
void raid_sched_resume(void);

/*!
 * @brief trigger recovery
 *
 * @param parm		not used
 * @param payload	not used
 * @param sts		not used
 *
 * @return		not used
 */
void raid_correct_trigger(u32 parm, u32 payload, u32 sts);

/*!
 * @brief trigger open stripe recovery
 *
 * @param parm		not used
 * @param payload	not used
 * @param sts		not used
 *
 * @return		not used
 */
void raid_correct_open_strtipe(u32 parm, u32 payload, u32 sts);

/*!
 * @brief check recovery stripe status
 *
 * @param fsm		pointer to state machine context
 *
 * @return		state machine function result
 */
fsm_res_t raid_correct_chk_stripe(fsm_ctx_t *fsm);

/*!
 * @brief allocate recovery datg
 *
 * @param fsm		pointer to state machine context
 *
 * @return		state machine function result
 */
fsm_res_t raid_correct_alloc_dtag(fsm_ctx_t *fsm);

/*!
 * @brief issue recovery read
 *
 * @param fsm		pointer to state machine context
 *
 * @return		state machine function result
 */
fsm_res_t raid_correct_read(fsm_ctx_t *fsm);

/*!
 * @brief do recovery xor operation
 *
 * @param fsm		pointer to state machine context
 *
 * @return		state machine function result
 */
fsm_res_t raid_correct_xor(fsm_ctx_t *fsm);

/*!
 * @brief issue recovery parity out
 *
 * @param fsm		pointer to state machine context
 *
 * @return		state machine function result
 */
fsm_res_t raid_correct_pout(fsm_ctx_t *fsm);

init_code void ftl_raid_init(void)
{
	u32 i;

    raid_chk.dtag[0].dtag = ddr_dtag_register(2) | DTAG_IN_DDR_MASK;
    raid_chk.dtag[1].dtag = raid_chk.dtag[0].dtag + 1;
    raid_chk.meta = idx_meta_allocate(2, DDR_IDX_META, &raid_chk.meta_idx_base);

	//raid construct init
	ncl_raid_init();

	memset(cwl_stripe, 0, sizeof(cwl_stripe));
	memset(&raid_id_mgr, 0, sizeof(raid_id_mgr_t));
	memset(&raid_bank_mgr, 0, sizeof(raid_bank_mgr_t));
	memset(&raid_sched_mgr, 0, sizeof(raid_sched_mgr_t));
	memset(&pbt_dtag_mgr, 0, sizeof(pbt_dtag_mgr_t));
    
	for (i = 0; i < MAX_CWL_STRIPE; i++) {
		idx_meta_allocate(EACH_DIE_DTAG_CNT, DDR_IDX_META, &raid_id_mgr.pad_meta_idx[i]);
	}

    for (i = 0; i < MAX_CWL_STRIPE * XLC * DU_CNT_PER_PAGE; ++i) // 120
    {
        raid_id_mgr.dtag[i].dtag = (DDR_RAID_DTAG_START + i) | DTAG_IN_DDR_MASK; 
    }
    ftl_raid_trace(LOG_INFO, 0x656b, "raid_id_mgr.dtag: 0x%x ~ 0x%x",raid_id_mgr.dtag[0].dtag, raid_id_mgr.dtag[MAX_CWL_STRIPE * XLC * DU_CNT_PER_PAGE - 1].dtag); 
    u32 dtag_base = (DDR_RAID_DTAG_START + i) | DTAG_IN_DDR_MASK; 

    for (i = 0; i < 3 * XLC * DU_CNT_PER_PAGE * DTAG_RES_CNT; ++i) //360
    {
        pbt_dtag_mgr.dtag[i].dtag = dtag_base + i; 
    }
    pbt_dtag_mgr.id_bmp = ~((1<<DTAG_RES_CNT) - 1);
    ftl_raid_trace(LOG_INFO, 0xf391, "pbt_dtag_mgr.dtag: 0x%x ~ 0x%x",pbt_dtag_mgr.dtag[0].dtag, pbt_dtag_mgr.dtag[3 * XLC * DU_CNT_PER_PAGE * DTAG_RES_CNT - 1].dtag); 
	sys_assert((MAX_CWL_STRIPE * XLC * DU_CNT_PER_PAGE) + (3 * XLC * DU_CNT_PER_PAGE * DTAG_RES_CNT) <= DDR_RAID_DTAG_CNT);

	QSIMPLEQ_INIT(&raid_sched_mgr.pend_que);
	raid_sched_mgr.ncl_cmd = &sched_ncl_cmd;
	raid_sched_mgr.ncl_cmd->user_tag_list = sched_bm;
	raid_sched_mgr.ncl_cmd->addr_param.common_param.pda_list = sched_pda;
	raid_sched_mgr.ncl_cmd->addr_param.common_param.info_list = sched_info;

	CBF_INIT(&shr_rc_host_queue);

	for (i = 0; i < MAX_CWL_STRIPE; i++){
        cwl_stripe[i].stripe_id = i;
	    struct ncl_cmd_t *ncl_cmd = &cwl_stripe[i].ncl_raid_cmd.ncl_cmd;
        ncl_cmd->user_tag_list = &cwl_stripe[i].ncl_raid_cmd.bm_pl_list[0];
        ncl_cmd->addr_param.common_param.pda_list = &cwl_stripe[i].ncl_raid_cmd.pda_list[0];
        ncl_cmd->addr_param.common_param.info_list = &cwl_stripe[i].ncl_raid_cmd.info_list[0];
	    struct ncl_cmd_t *ncl_cmd_pout = &cwl_stripe[i].ncl_raid_cmd.ncl_cmd_pout;
        ncl_cmd_pout->user_tag_list = &cwl_stripe[i].ncl_raid_cmd.bm_pl_list[0];
        ncl_cmd_pout->addr_param.common_param.pda_list = &cwl_stripe[i].ncl_raid_cmd.pda_list[0];
        ncl_cmd_pout->addr_param.common_param.info_list = &cwl_stripe[i].ncl_raid_cmd.info_list[0];
    }

	for (i = 0; i < RAID_BANK_NUM; i++)
		raid_bank_mgr.free_id_cnt[i] = RAID_BUF_PER_BANK;

	pool_init(&cwl_stripe_pool, (char*)&cwl_stripe[0],
		MAX_CWL_STRIPE * sizeof(cwl_stripe_t),
		sizeof(cwl_stripe_t), MAX_CWL_STRIPE);

#if XOR_CMPL_BY_CMPL_CTRL
	evt_register(raid_xor_cmpl_handle, 0, &evt_xor_cmpl);
#endif

	// raid correct init
	memset(&raid_correct, 0, sizeof(raid_correct_t));
	raid_correct.ncl_cmd = &rc_ncl_cmd;

	rc_ncl_cmd.caller_priv = &raid_correct;
	rc_ncl_cmd.du_format_no = DU_4K_DEFAULT_MODE;

	raid_correct.info = rc_info;
	raid_correct.bm_pl = rc_bm_pl;

	rc_ncl_cmd.user_tag_list = raid_correct.bm_pl;
	rc_ncl_cmd.addr_param.common_param.info_list = raid_correct.info;
	rc_ncl_cmd.addr_param.common_param.pda_list = raid_correct.pda;

	//raid correct always operates bank 7
	for (i = 0; i < DU_CNT_PER_RAID_XOR_CMD; i++) {
		rc_info[i].raid_id = RAID_CORRECT_RAID_ID;
		rc_info[i].bank_id = RAID_CORRECT_BANK_ID;
	}

	QSIMPLEQ_INIT(&raid_correct.pend_que);
	evt_register(raid_correct_trigger, 0, &raid_correct.evt_trig);
	evt_register(raid_correct_open_strtipe, 0, &raid_correct.evt_open);

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
#if (CPU_DTAG == CPU_ID)
	raid_pad_dtag = dtag_get(DTAG_T_DDR, NULL);
#else
	ipc_api_remote_dtag_get(&raid_pad_dtag.dtag, true, true);
#endif

	sys_assert(raid_pad_dtag.dtag != _inv_dtag.dtag);
	memset(dtag2mem(raid_pad_dtag), 0, DTAG_SZE);

#ifndef DISABLE_HS_CRC_SUPPORT
	extern void *ddr_hcrc_base;
	u16 *hcrc = (u16*)ddr_hcrc_base;

	/*
	* raid pad dtag is internal data, to pass hcrc check,
	* we get hcrc data by FDMA with all zero data and INV_LDA,
	* then fill into the right place in hcrc buffer manually.
	*/

	sys_assert(NR_LBA_PER_LDA == 8);  //no replace NR_LBA_PER_LDA, because allocate MAX Buf
	hcrc = &hcrc[raid_pad_dtag.b.dtag * NR_LBA_PER_LDA];
	hcrc[0] = 0xbb13;
	hcrc[1] = 0x3d64;
	hcrc[2] = 0x37f8;
	hcrc[3] = 0xb18f;
	hcrc[4] = 0x22c0;
	hcrc[5] = 0xa4b7;
	hcrc[6] = 0xae2b;
	hcrc[7] = 0x285c;
#endif
#endif
}

init_code void stripe_user_init(stripe_user_t *su)
{
	su->active = NULL;
	QSIMPLEQ_INIT(&su->wait_que);
	QSIMPLEQ_INIT(&su->cmpl_que);
}

fast_code void strip_user_reset(stripe_user_t *su)
{
	panic("todo");
}

/*!
 * @brief get available bank id
 *
 * @param skip_bmp	skipped search bank
 *
 * @return		bank id
 */
fast_code u32 raid_get_bank_id(u32 skip_bmp)
{
	u32 i;
	u32 left_buf;
	u32 ttl_id_cnt;
	u32 enc_buf_per_bank;
	u32 bank_id = RAID_BANK_NUM;
	u32 least_id_cnt = MAX_WR_RAID_ID;

	/*
	 * get least busy bank to banlance each bank's workload
	 * 1. firstly alloc the bank whose free buf satisfy required buf count
	 * 2. if no enough free buf, alloc the bank whose workload is the least
	 */
	for (i = 0; i < RAID_BANK_NUM; i++) {
		if (skip_bmp & BIT(i))
			continue;

		if (i != RAID_CORRECT_BANK_ID)
			enc_buf_per_bank = RAID_BUF_PER_BANK;
		else
			enc_buf_per_bank = RAID_BUF_PER_BANK - 1;

		ttl_id_cnt = raid_bank_mgr.ttl_id_cnt[i];
		if (ttl_id_cnt > enc_buf_per_bank)
			left_buf = 0;
		else
			left_buf = enc_buf_per_bank - ttl_id_cnt;

		if (left_buf) {
			bank_id = i;
			break;
		} else if (ttl_id_cnt < least_id_cnt) {
			bank_id = i;
			least_id_cnt = ttl_id_cnt;
		}
	}

	sys_assert(bank_id < RAID_BANK_NUM);
	return bank_id;
}

fast_code cwl_stripe_t* raid_get_cwl_stripe(pstream_t *ps, u32 page_cnt)
{
	u32 i;
	u32 raid_id;
	u32 bank_id;
	raid_id_info_t *id_info;
	cwl_stripe_t *cwl_stripe;

	aspb_t *aspb = &ps->aspb[ps->curr];
	u32 fpage = ps->aspb[ps->curr].wptr / nand_info.interleave;
	u32 du_cnt_per_id = aspb->ttl_usr_intlv << DU_CNT_SHIFT;

	cwl_stripe = pool_get_ex(&cwl_stripe_pool);
	sys_assert(cwl_stripe);

    page_cnt /= nand_info.geo.nr_planes;

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_CMPL_CTRL
#ifdef SKIP_MODE
    //u8* ftl_df_ptr = get_spb_defect(ps->aspb[cur].spb_id);
    stripe_pdone_cnt[cwl_stripe->stripe_id] = nand_info.lun_num - ps->aspb[ps->curr].total_bad_die_cnt;
    if(!aspb->flags.b.parity_mix){
        // parity die nood user data
        stripe_pdone_cnt[cwl_stripe->stripe_id] -= 1;
    }
#else
	stripe_pdone_cnt[cwl_stripe->stripe_id] = nand_info.lun_num;
#endif
#endif

	cwl_stripe->fly_wr = 0;
	cwl_stripe->done_wr = 0;
	cwl_stripe->fpage = fpage;
	cwl_stripe->ncl_req = NULL;
	cwl_stripe->nsid = ps->nsid;
	cwl_stripe->type = ps->type;
	cwl_stripe->page_cnt = page_cnt;
	cwl_stripe->spb_id = aspb->spb_id;
	cwl_stripe->state = STRIPE_ST_IDLE;
	cwl_stripe->wait = false;
	cwl_stripe->parity_mix = aspb->flags.b.parity_mix;
    cwl_stripe->parity_user_done = false;
	cwl_stripe->pout_send = false;
	cwl_stripe->pout_done = false;
	cwl_stripe->xor_done = false;
	cwl_stripe->xor_only_send = false;
	cwl_stripe->parity_die_ready = false;
    cwl_stripe->parity_req = NULL;
	u32 bank_skip_bmp = 0;
	u32 bank_alloc[RAID_BANK_NUM];

	memset(bank_alloc, 0, RAID_BANK_NUM * sizeof(u32));
	ncl_raid_set_stripe_cmpl_cnt(cwl_stripe->stripe_id, page_cnt);


	for (i = 0; i < page_cnt; i++) {
		//get least busy bank id
		bank_id = raid_get_bank_id(bank_skip_bmp);
		raid_bank_mgr.ttl_id_cnt[bank_id] ++;

		//todo: optimize to be more flexible
		bank_alloc[bank_id] ++;
		sys_assert(bank_alloc[bank_id] <= RAID_BUF_PER_BANK);
		if (!(RAID_BUF_PER_BANK - bank_alloc[bank_id]))
			bank_skip_bmp |= BIT(bank_id);

		//alloc raid id for each plane
		raid_id = find_first_zero_bit(&raid_id_mgr.id_bmp, MAX_WR_RAID_ID);
		sys_assert(raid_id < MAX_WR_RAID_ID);
		set_bit((int)raid_id, &raid_id_mgr.id_bmp);

		//alloc raid id for each plane
        for (u16 j = 0; j < DU_CNT_PER_PAGE; ++j)
        {
    		cwl_stripe->parity_buffer_dtag[i*DU_CNT_PER_PAGE+j].dtag = raid_id_mgr.dtag[raid_id*DU_CNT_PER_PAGE+j].dtag;
        }
		cwl_stripe->raid_id[i][0] = raid_id;
		cwl_stripe->bank_id[i][0] = bank_id;

		id_info = &raid_id_mgr.id_info[raid_id];
		id_info->nsid = ps->nsid;
		id_info->type = ps->type;
		id_info->bank_id = bank_id;
		id_info->stripe_id = cwl_stripe->stripe_id;
		id_info->state = RAID_ID_ST_IDLE;
        ncl_raid_id_reset(raid_id);
		ncl_raid_set_stripe_lut(raid_id, cwl_stripe->stripe_id);
		ncl_raid_set_cmpl_para(raid_id, du_cnt_per_id, true, true);
	}

	return cwl_stripe;
}

fast_code void raid_put_cwl_stripe(cwl_stripe_t *cwl_stripe)
{
	u32 i;
	int set;
	u32 raid_id;
	u32 bank_id;
	raid_id_info_t *id_info;

	for (i = 0; i < cwl_stripe->page_cnt; i++) {

		bank_id = cwl_stripe->bank_id[i][0];
		raid_bank_mgr.ttl_id_cnt[bank_id] --;
		raid_bank_mgr.free_id_cnt[bank_id] ++;

		raid_id = cwl_stripe->raid_id[i][0];
		set = test_and_clear_bit(raid_id, &raid_id_mgr.id_bmp);
		sys_assert(set == 1);

		id_info = &raid_id_mgr.id_info[raid_id];
		id_info->state = RAID_ID_ST_FREE;
		//clear internal cnt of parity out
		ncl_raid_set_intl_cnt(raid_id, 0);
	}

	cwl_stripe->state = STRIPE_ST_FREE;
	pool_put_ex(&cwl_stripe_pool, (void*)cwl_stripe);
}

fast_code cwl_stripe_t* get_stripe_by_id(u32 stripe_id)
{
	return &cwl_stripe[stripe_id];
}

#if 0
fast_code void set_spb_raid_info(aspb_t *aspb)
{
	// TODO: defect support
	aspb->parity_die = nand_info.lun_num - 1;
	aspb->ttl_usr_intlv = nand_interleave_num() - nand_plane_num();
}
#endif

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_FDONE
fast_code void ftl_raid_wr_done(ncl_w_req_t *req)
{
	u32 i;
	bm_pl_t *bm_pl;
	u32 lda_cnt = (req->req.op_type.b.xor) ? req->req.cnt * DU_CNT_PER_PAGE : 0;

	//restore ctrl_type
	if (lda_cnt) {
        i = lda_cnt - 1;
        bm_pl = &req->w.pl[i];
        bm_pl->pl.type_ctrl = bm_pl->pl.du_ofst & DU_SEMI_MASK;
        if (req->req.type == FTL_CORE_GC){
            bm_pl->pl.du_ofst = req->w.trim.dtag.dtag;
        }
    }
	req->req.cmpl = req->req.tmp_cmpl;
	req->req.cmpl(req);
}
#endif

fast_code void set_ncl_req_raid_info(ncl_w_req_t *req, cwl_stripe_t *stripe, u32 lda_cnt, bool parity_mix)
{
	req->req.stripe_id = stripe->stripe_id;
    
    req->req.op_type.b.xor = 0;
    req->req.op_type.b.pout = 0;
    req->req.op_type.b.parity_mix = 0;

	if (lda_cnt) {
        if(parity_mix)
		    req->req.op_type.b.parity_mix = 1;
        else
		    req->req.op_type.b.xor = 1;

#if XOR_CMPL_BY_PDONE && !XOR_CMPL_BY_FDONE
        u32 i;
        bm_pl_t *bm_pl;

        i = lda_cnt - 1;
        bm_pl = &req->w.pl[i];

        if (req->req.type == FTL_CORE_GC) {
            req->w.trim.dtag.dtag = bm_pl->pl.du_ofst;
        }
        //save type_ctrl in du_ofst
        bm_pl->pl.du_ofst = bm_pl->pl.type_ctrl | (req->req.stripe_id << DU_SEMI_OFST);

        //clr prv qid_type and set as pdone
        bm_pl->pl.type_ctrl &= ~BTN_NCL_QID_TYPE_MASK;
        bm_pl->pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DONE;

        if (bm_pl->pl.dtag == WVTAG_ID)
            bm_pl->pl.dtag = raid_pad_dtag.dtag;
        req->req.tmp_cmpl = req->req.cmpl;
        req->req.cmpl = ftl_raid_wr_done;
#endif
	} else {
		req->req.op_type.b.pout = 1;
	}


#ifdef DUAL_BE
    u32 j, k;

#ifdef SKIP_MODE   
    u32 plane = (req->req.op_type.b.pln_write > 0) ? req->req.op_type.b.pln_write : shr_nand_info.geo.nr_planes;

	for (j = 0; j < stripe->page_cnt; j++){
		for (k = 0; k < plane; k++){
			req->w.raid_id[j][k].raid_id = stripe->raid_id[j][0];
            req->w.raid_id[j][k].bank_id = stripe->bank_id[j][0];
		}
	}
#else
	if (req->req.op_type.b.pln_st == BLOCK_P0_BAD) {
        for (j = 0; j < stripe->page_cnt; j++) {
            req->w.raid_id[j][0].raid_id = stripe->raid_id[j][0];
            req->w.raid_id[j][0].bank_id = stripe->bank_id[j][0];
        }
    } else {
        u32 plane = nand_plane_num();
        if (req->req.op_type.b.pln_st != BLOCK_NO_BAD) {
            sys_assert(req->req.op_type.b.pln_st == BLOCK_P1_BAD);
            plane >>= 1;
        }
        for (j = 0; j < stripe->page_cnt; j++) {
            for (k = 0; k < plane; k++) {
                req->w.raid_id[j][k].raid_id = stripe->raid_id[j][0];
                req->w.raid_id[j][k].bank_id = stripe->bank_id[j][0];
            }
        }
    }
#endif
    sys_assert(req->w.raid_id[0][0].raid_id < 64);
#endif

}

#ifndef DUAL_BE
fast_code void set_ncl_cmd_raid_info(ncl_w_req_t *req, struct ncl_cmd_t *ncl_cmd)
{
	u32 i, j, k = 0;
	u32 plane_num;
	u32 mp_page_cnt;
	cwl_stripe_t *stripe;
	struct info_param_t *info;

	plane_num = nand_plane_num();
	stripe = &cwl_stripe[req->stripe_id];
	info = ncl_cmd->addr_param.common_param.info_list;
	mp_page_cnt = req->req.mp_du_cnt >> DU_CNT_SHIFT;

	for (i = 0; i < stripe->page_cnt; i++) {
		for (j = 0; j < mp_page_cnt; j++) {
			info[k].raid_id = stripe->raid_id[i][j % plane_num];
			info[k].bank_id = stripe->bank_id[i][j % plane_num];
			k++;
		}
	}

	if (req->op_type.b.xor)
		ncl_cmd->flags |= NCL_CMD_RAID_XOR_FLAG_SET;
	else if (req->op_type.b.pout)
		ncl_cmd->flags |= NCL_CMD_RAID_POUT_FLAG_SET;
}
#endif
extern bool wr_xor_only_issue(ncl_w_req_t * req);

fast_code int pbt_dtag_copy_done(void *ctx, dpe_rst_t *rst)
{
    u32 dtag_id = (u32)ctx;
    ncl_w_req_t * req = pbt_dtag_mgr.req[dtag_id];
    pbt_dtag_mgr.otf[dtag_id] --;
	// ftl_core_trace(LOG_INFO, 0, "dtag_id %x req %x otf %u", dtag_id, req, pbt_dtag_mgr.otf[dtag_id]); 
    if(!pbt_dtag_mgr.otf[dtag_id]){
        req->req.dtag_id = dtag_id;
        wr_xor_only_issue(req);
    }
    return 0;
}

fast_code void ftl_core_xor_only_submit(ncl_w_req_t * req)
{
    u32 dtag_id = DTAG_RES_CNT;
    dtag_t *dtag_new = NULL;
    req->req.dtag_id = dtag_id;
    if(req->req.nsid == INT_NS_ID && req->req.type == FTL_CORE_PBT){
        dtag_id = find_first_zero_bit(&pbt_dtag_mgr.id_bmp, DTAG_RES_CNT);
        sys_assert(dtag_id < DTAG_RES_CNT);
    }
    if(dtag_id < DTAG_RES_CNT){
        set_bit((int)dtag_id, &pbt_dtag_mgr.id_bmp);
        pbt_dtag_mgr.req[dtag_id] = req;
        dtag_new = &pbt_dtag_mgr.dtag[3 * XLC * DU_CNT_PER_PAGE * dtag_id];
        dtag_t dtag;
        dtag_t dtag_start;
        u16 i, k, j, l;
        k = 0;
        bm_pl_t *bm_pl_list_src = req->w.pl;
        u8 pn_cnt = req->req.cnt / req->req.tpage;
        for (i = 0; i < req->req.tpage; i++) {
            for (j = 1; j < pn_cnt; j++) {
                u16 idx = i * pn_cnt + j; 
                for(l = 0;l<DU_CNT_PER_PAGE;l++){
                    dtag.dtag = bm_pl_list_src[idx*DU_CNT_PER_PAGE+l].pl.dtag;
                    if(dtag.dtag != WVTAG_ID && pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt == mFALSE){
                        if(k == 0)
                            dtag_start.dtag = dtag.dtag;
                        else
                            sys_assert(dtag_start.dtag + k == dtag.dtag);

                        bm_pl_list_src[idx*DU_CNT_PER_PAGE+l].pl.dtag = dtag_new[k].dtag;
                        k++;
                    }
                }
            }
        }
        if(!k){
            pbt_dtag_mgr.req[dtag_id] = NULL;
            // ftl_core_trace(LOG_INFO, 0, "dtag_id:%d id_bmp:%x", dtag_id, pbt_dtag_mgr.id_bmp);
            u32 set = test_and_clear_bit(dtag_id, &pbt_dtag_mgr.id_bmp);
            sys_assert(set == 1);
            wr_xor_only_issue(req);
        }
        else{
            pbt_dtag_mgr.otf[dtag_id] ++;
			bm_data_copy(ddtag2mem((u64)dtag_start.b.dtag), ddtag2mem((u64)dtag_new[0].b.dtag), DTAG_SZE * k, pbt_dtag_copy_done, (void*)dtag_id);
        }
    }else{
        wr_xor_only_issue(req);
    }
}

extern bool wr_pout_issue(ncl_w_req_t * req);
#if XOR_CMPL_BY_CMPL_CTRL
fast_code void raid_xor_cmpl_handle(u32 param, u32 payload, u32 count)
{
	u8 stripe_id = payload;
	cwl_stripe_t *stripe = get_stripe_by_id(stripe_id); 
    // ftl_core_trace(LOG_ERR, 0, "stripe_id %u parity_die_ready %u, xor_done %u, xor_only_send %u",
    //     stripe->stripe_id, stripe->parity_die_ready, stripe->xor_done, stripe->xor_only_send);
    // ftl_core_trace(LOG_ERR, 0, "raid_xor_cmpl_handle, stripe_id %u", stripe_id);
    if(stripe->parity_mix){
        stripe->xor_done = true;
        if(stripe->parity_die_ready){
            if(stripe->xor_only_send == false){
                panic("stripe->xor_only_send != true !!");
            }else{
                sys_assert(stripe->parity_req != NULL);
                ncl_w_req_t *parity_req = stripe->parity_req;
                if(stripe->pout_send == false){
                    stripe->pout_send = true;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
					ncl_raid_set_stripe_cmpl_cnt(stripe->stripe_id, stripe->page_cnt);
                	for (u8 i = 0; i < stripe->page_cnt; i++) 
#else
					ncl_raid_set_stripe_cmpl_cnt(stripe->stripe_id, XLC);
                	for (u8 i = 0; i < XLC; i++) 
#endif
                	{
                        u32 raid_id = stripe->raid_id[i][0];
                        ncl_raid_id_reset(raid_id);
                		ncl_raid_set_stripe_lut(raid_id, stripe->stripe_id);
                        ncl_raid_set_cmpl_para(raid_id, DU_CNT_PER_PAGE, true, true);
                    }
                    wr_pout_issue(parity_req);
                }else{
                    // ftl_core_trace(LOG_ERR, 0, "lucas ftl_core_parity_flush stripe_id %u" stripe_id);
                    die_que_wr_era_ins(&parity_req->req, parity_req->req.die_id);
                    stripe->parity_req = NULL;
                }
            }
        }
    }else{
        // ftl_core_trace(LOG_ERR, 0, "lucas ftl_core_parity_flush stripe_id %u" stripe_id);
        ftl_core_parity_flush(stripe_id);
    }
}
#endif

fast_code void stripe_update(stripe_user_t *su, u32 stripe_id, pstream_t* ps)
{
	u32 i;
	u32 bank_id;
	cwl_stripe_t *stripe;
	raid_sched_que_t *pend_que;

	stripe = &cwl_stripe[stripe_id];
	stripe->fly_wr--;
	stripe->done_wr++;

	if (stripe->fly_wr)
		return;
    
	if (stripe->state != STRIPE_ST_FINISHING) {
		for (i = 0; i < stripe->page_cnt; i++) {
			bank_id = stripe->bank_id[i][0];
			raid_bank_mgr.switchable_id_cnt[bank_id] ++;
		}
	} else {
#if 0 // debug chk if stripe in cmpl queue
        bool hit = false;
	    cwl_stripe_t *_stripe;
    	QSIMPLEQ_FOREACH(_stripe, &su->cmpl_que, link) {
            if(_stripe == stripe){
                hit = true;
                break;
            }
    	}
        sys_assert(hit == true);
#endif
        QSIMPLEQ_REMOVE(&su->cmpl_que, stripe, _cwl_stripe_t, link);
        ps->parity_cnt--;
		raid_put_cwl_stripe(stripe);
        extern u32 parity_allocate_cnt;
        sys_assert(parity_allocate_cnt > 0);
        parity_allocate_cnt--;
	}

	pend_que = &raid_sched_mgr.pend_que;
	if (!QSIMPLEQ_EMPTY(pend_que))
		raid_sched_resume();
}

/*!
 * @brief submit request to schedule
 *
 * @param req		pointer to ncl request
 *
 * @return		not used
 */
fast_code void raid_sched_submit(ncl_w_req_t *req)
{
	u32 i;
	u32 raid_id;
	u32 bank_id;
	cwl_stripe_t* stripe;
	raid_id_info_t *id_info;

	stripe = &cwl_stripe[req->req.stripe_id];
	if (stripe->state == STRIPE_ST_IDLE) {
		for (i = 0; i < stripe->page_cnt; i++) {
			bank_id = stripe->bank_id[i][0];
			raid_id = stripe->raid_id[i][0];
			id_info = &raid_id_mgr.id_info[raid_id];

			sys_assert(id_info->state == RAID_ID_ST_IDLE);
			raid_bank_mgr.free_id_cnt[bank_id]--;
			id_info->state = RAID_ID_ST_BUSY;
		}
    } else if ((stripe->state != STRIPE_ST_SUSPEND) && (stripe->fly_wr == 0)) {
		for (i = 0; i < stripe->page_cnt; i++) {
			bank_id = stripe->bank_id[i][0];
			sys_assert(raid_bank_mgr.switchable_id_cnt[bank_id]);
			raid_bank_mgr.switchable_id_cnt[bank_id] --;
		}
	}

	stripe->fly_wr++;
	stripe->state = (req->req.op_type.b.pout||req->req.op_type.b.parity_mix) ? STRIPE_ST_FINISHING : STRIPE_ST_BUSY;

    if(req->req.op_type.b.parity_mix){
        extern u8 pending_parity_cnt;
        pending_parity_cnt ++;
        sys_assert(stripe->parity_req == NULL); 
        stripe->parity_req = req; 
        stripe->parity_die_ready = true;
        stripe->xor_only_send = true;
        ftl_core_xor_only_submit(req);
    }else{
        die_que_wr_era_ins(&req->req, req->req.die_id);
    }
}

/*!
 * @brief suspend raid id done handle function
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
slow_code void raid_suspend_done(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 cnt;
	u32 raid_id;
	u32 bank_id;
	cwl_stripe_t *stripe;
	raid_id_info_t *id_info;
	struct info_param_t *info;

	cnt = ncl_cmd->addr_param.common_param.list_len;
	info = ncl_cmd->addr_param.common_param.info_list;

	for (i = 0; i < cnt; i++) {
		raid_id = info[i].raid_id;
		bank_id = info[i].bank_id;
		id_info = &raid_id_mgr.id_info[raid_id];
		id_info->state = RAID_ID_ST_SUSPENED;

		stripe = &cwl_stripe[id_info->stripe_id];
		stripe->state = STRIPE_ST_SUSPEND;

		raid_bank_mgr.free_id_cnt[bank_id]++;
		sys_assert(raid_bank_mgr.free_id_cnt[bank_id] <= RAID_BUF_PER_BANK);

		ftl_raid_trace(RAID_LEVEL, 0x071f, "suspend id %d done", raid_id);
	}

	raid_sched_mgr.busy = false;
	raid_sched_resume();
}

/*!
 * @brief suspend raid id
 *
 * @param suspend_id_cnt	pointer to bank suspend count
 * @param nsid			namespace id
 * @param type			type
 *
 * @return			not used
 */
slow_code void _raid_suspend(u32 *suspend_id_cnt, u32 nsid, u32 type)
{
	u32 i;
	u32 cnt;
	u32 raid_id;
	u32 bank_id;
	pda_t *pda_list;
	bm_pl_t *bm_plt;
	u32 du_per_intlv;
	cwl_stripe_t *stripe;
	raid_id_info_t *id_info;
	struct info_param_t *info;
	struct ncl_cmd_t *ncl_cmd;

	cnt = 0;
	ncl_cmd = raid_sched_mgr.ncl_cmd;
	bm_plt = ncl_cmd->user_tag_list;
	info = ncl_cmd->addr_param.common_param.info_list;
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	du_per_intlv = nand_info.lun_num * nand_plane_num() * DU_CNT_PER_PAGE;

	//get suspend raid id
	for (i = 0; i < MAX_WR_RAID_ID; i++) {
		id_info = &raid_id_mgr.id_info[i];
		stripe = &cwl_stripe[id_info->stripe_id];

		if (stripe->fly_wr)
			continue;

		if (id_info->state != RAID_ID_ST_BUSY)
			continue;

		if ((id_info->nsid == nsid) && (id_info->type == type))
			continue;

		bank_id = id_info->bank_id;
		if (suspend_id_cnt[bank_id]) {
		     /*
                    * FINST can't cross raid bank,
                    * guarantee each pda of raid id is not sequential,
                    * then each raid id's suspend will be split into one FINST
			*/
			pda_list[cnt] = du_per_intlv * cnt;

			bm_plt[cnt].pl.du_ofst = cnt;
			//bm_plt[cnt].pl.dtag = WVTAG_ID;
			//bm_plt[cnt].pl.type_ctrl = META_SRAM_DTAG;

			info[cnt].raid_id = i;
			info[cnt].bank_id = bank_id;
			cnt++;

			suspend_id_cnt[bank_id]--;
		}
	}

	//check if get enough suspend id
	for (i = 0; i < RAID_BANK_NUM; i++) {
		if (suspend_id_cnt[i])
			return;
	}

	//update raid & stripe state
	for (i = 0; i < cnt; i++) {
		raid_id = info[i].raid_id;
		bank_id = info[i].bank_id;
		id_info = &raid_id_mgr.id_info[raid_id];
		id_info->state = RAID_ID_ST_SUSPENDING;

		stripe = &cwl_stripe[id_info->stripe_id];
		stripe->state = STRIPE_ST_SUSPENDING;
		stripe->suspend_cnt[bank_id]++;

		sys_assert(raid_bank_mgr.switchable_id_cnt[bank_id]);
		raid_bank_mgr.switchable_id_cnt[bank_id]--;

		ftl_raid_trace(RAID_LEVEL, 0xebac, "suspend id %d bank %d stripe %d ns %d %d",
			raid_id, bank_id, stripe->stripe_id, stripe->nsid, stripe->type);
	}

	//build suspend req
	ncl_cmd->status = 0;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->addr_param.common_param.list_len = cnt;
	ncl_cmd->completion = raid_suspend_done;

	ncl_cmd->flags = 0;
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->flags |= NCL_CMD_RAID_SUSPEND_FLAG_SET;

	raid_sched_mgr.busy = true;
	ncl_cmd_submit(ncl_cmd);
}

/*!
 * @brief interface to suspend raid id
 *
 * @param suspend_id_cnt	pointer to bank suspend count
 * @param nsid			namespace id
 * @param type			type
 *
 * @return			not used
 */
static slow_code void raid_suspend(u32 *suspend_id_cnt, u32 nsid, u32 type)
{
	if (raid_sched_mgr.busy == false)
		_raid_suspend(suspend_id_cnt, nsid, type);
	else
		ftl_raid_trace(LOG_WARNING, 0xc544, "ns %d type %d suspend pend", nsid, type);
}

/*!
 * @brief resume raid id done handle function
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
slow_code void raid_resume_done(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 cnt;
	u32 raid_id;
	u32 bank_id;
	cwl_stripe_t *stripe;
	raid_id_info_t *id_info;
	struct info_param_t *info;

	cnt = ncl_cmd->addr_param.common_param.list_len;
	info = ncl_cmd->addr_param.common_param.info_list;

	for (i = 0; i < cnt; i++) {
		raid_id = info[i].raid_id;
		id_info = &raid_id_mgr.id_info[raid_id];
		id_info->state = RAID_ID_ST_BUSY;

		bank_id = info[i].bank_id;
		raid_bank_mgr.switchable_id_cnt[bank_id]++;
	}

	raid_id = info[0].raid_id;
	id_info = &raid_id_mgr.id_info[raid_id];
	stripe = &cwl_stripe[id_info->stripe_id];
	stripe->state = STRIPE_ST_BUSY;
	ftl_raid_trace(RAID_LEVEL, 0xeef0, "stripe %d resume done", stripe->stripe_id);

	raid_sched_mgr.busy = false;
	raid_sched_resume();
}

/*!
 * @brief interface to resume stripe
 *
 * @param stripe	pointer to resuming stripe
 *
 * @return		not used
 */
slow_code void _raid_resume(cwl_stripe_t *stripe)
{
	u32 i, j;
	u32 cnt;
	u32 raid_id;
	u32 bank_id;
	pda_t *pda_list;
	bm_pl_t *bm_plt;
	u32 du_per_intlv;
	raid_id_info_t *id_info;
	struct info_param_t *info;
	struct ncl_cmd_t *ncl_cmd;

	cnt = 0;
	ncl_cmd = raid_sched_mgr.ncl_cmd;
	bm_plt = ncl_cmd->user_tag_list;
	info = ncl_cmd->addr_param.common_param.info_list;
	pda_list = ncl_cmd->addr_param.common_param.pda_list;
	du_per_intlv = nand_info.lun_num * nand_plane_num() * DU_CNT_PER_PAGE;

	for (i = 0; i < stripe->page_cnt; i++) {
		for (j = 0; j < nand_plane_num(); j++) {
			raid_id = stripe->raid_id[i][0];
			bank_id = stripe->bank_id[i][0];
			id_info = &raid_id_mgr.id_info[raid_id];
			sys_assert(bank_id == id_info->bank_id);

			if (id_info->state == RAID_ID_ST_SUSPENED) {
				pda_list[cnt] = du_per_intlv * cnt;
				bm_plt[cnt].pl.du_ofst = cnt;
				//bm_plt[cnt].pl.dtag = WVTAG_ID;
				//bm_plt[cnt].pl.type_ctrl = META_SRAM_DTAG;

				info[cnt].raid_id = raid_id;
				info[cnt].bank_id = bank_id;
				cnt++;

				id_info->state = RAID_ID_ST_RESUMING;

				sys_assert(stripe->suspend_cnt[bank_id]);
				stripe->suspend_cnt[bank_id]--;

				sys_assert(raid_bank_mgr.free_id_cnt[bank_id]);
				raid_bank_mgr.free_id_cnt[bank_id]--;

				ftl_raid_trace(RAID_LEVEL, 0xf7d1, "resume stripe %d id %d ns %d %d",
					stripe->stripe_id, raid_id, stripe->nsid, stripe->type);
			}
		}
	}

	stripe->state = STRIPE_ST_RESUMING;

	ncl_cmd->status = 0;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->addr_param.common_param.list_len = cnt;
	ncl_cmd->completion = raid_resume_done;

	ncl_cmd->flags = 0;
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->flags |= NCL_CMD_RAID_RESUME_FLAG_SET;

	raid_sched_mgr.busy = true;
	ncl_cmd_submit(ncl_cmd);
}

/*!
 * @brief resume stripe
 *
 * @param stripe	pointer to resuming stripe
 *
 * @return		not used
 */
static slow_code void raid_resume(cwl_stripe_t *stripe)
{
	if (raid_sched_mgr.busy == false)
		_raid_resume(stripe);
	else
		ftl_raid_trace(LOG_WARNING, 0x40d4, "stripe %d(ns %d type %d) resume pend",
			stripe->stripe_id, stripe->nsid, stripe->type);
}

/*!
 * @brief get stripe next action
 *
 * @param req		pointer to ncl request
 * @param suspend_cnt	pointer to bank suspend count
 *
 * @return		action
 */
fast_code u8 get_sched_action(ncl_w_req_t *req, u32 *suspend_cnt)
{
	u32 i;
	u32 nsid;
	u32 type;
	u32 bank_id;
	u32 free_cnt;
	u32 switch_cnt;
	u32 resume_cnt;
	cwl_stripe_t* stripe;
	u32 required_cnt[RAID_BANK_NUM];

	memset(required_cnt, 0, sizeof(u32) * RAID_BANK_NUM);

	nsid = req->req.nsid;
	type = req->req.type;
	stripe = &cwl_stripe[req->req.stripe_id];

	//parity out exec directly
	if (req->req.op_type.b.pout) {
		sys_assert(stripe->state == STRIPE_ST_BUSY);
		return RAID_SCHED_ACT_EXEC;
	}

	// each type's write must exec in order
	if (raid_sched_mgr.pend_cnt[nsid][type])
		return RAID_SCHED_ACT_PEND;

	if (stripe->state == STRIPE_ST_BUSY)
		return RAID_SCHED_ACT_EXEC;

	if (stripe->state == STRIPE_ST_RESUMING)
		return RAID_SCHED_ACT_PEND;

	if (stripe->state == STRIPE_ST_SUSPENDING)
		return RAID_SCHED_ACT_PEND;

	if (stripe->state == STRIPE_ST_IDLE) {
		for (i = 0; i < stripe->page_cnt; i++) {
			bank_id = stripe->bank_id[i][0];
			required_cnt[bank_id] ++;
		}

		u32 suspend_bmp = 0;
		for (i = 0; i < RAID_BANK_NUM; i++) {
			free_cnt = raid_bank_mgr.free_id_cnt[i];
			switch_cnt = raid_bank_mgr.switchable_id_cnt[i];
			if (free_cnt >= required_cnt[i])
				continue;

			if (free_cnt + switch_cnt >= required_cnt[i]) {
				sys_assert(switch_cnt);
				suspend_bmp |= BIT(i);
				suspend_cnt[i] += required_cnt[i] - free_cnt;
				continue;
			}

			return RAID_SCHED_ACT_PEND;
		}

		if (suspend_bmp)
			return RAID_SCHED_ACT_SUSPEND;
		else
			return RAID_SCHED_ACT_EXEC;
	}

	if (stripe->state == STRIPE_ST_SUSPEND) {
		u32 suspend_bmp = 0;
		for (i = 0; i < RAID_BANK_NUM; i++) {
			resume_cnt = stripe->suspend_cnt[i];
			free_cnt = raid_bank_mgr.free_id_cnt[i];
			switch_cnt = raid_bank_mgr.switchable_id_cnt[i];

			if (free_cnt >= resume_cnt)
				continue;

			if (free_cnt + switch_cnt >= resume_cnt) {
				suspend_bmp |= BIT(i);
				suspend_cnt[i] += resume_cnt - free_cnt;
			} else {
				return RAID_SCHED_ACT_PEND;
			}
		}

		if (suspend_bmp)
			return RAID_SCHED_ACT_SUSPEND;
		else
			return RAID_SCHED_ACT_RESUME;
	}

	sys_assert(false);
	return RAID_SCHED_ACT_EXEC;
}

fast_code void raid_sched_push(ncl_w_req_t *req)
{
	u32 nsid = req->req.nsid;
	u32 type = req->req.type;
	u32 suspend_cnt[RAID_BANK_NUM];
	cwl_stripe_t *stripe = &cwl_stripe[req->req.stripe_id];
	raid_sched_mgr_t *raid_sched = &raid_sched_mgr;
	raid_sched_que_t *pend_que = &raid_sched->pend_que;

	memset(suspend_cnt, 0, sizeof(u32) * RAID_BANK_NUM);

	u32 action = get_sched_action(req, suspend_cnt);
    sys_assert(action == RAID_SCHED_ACT_EXEC);
	switch (action) {
		case RAID_SCHED_ACT_EXEC:
			raid_sched_submit(req);
			break;

		case RAID_SCHED_ACT_PEND:
			raid_sched->pend_cnt[nsid][type]++;
			QSIMPLEQ_INSERT_TAIL(pend_que, &req->req, link);
			break;

		case RAID_SCHED_ACT_SUSPEND:
			raid_sched->pend_cnt[nsid][type]++;
			QSIMPLEQ_INSERT_TAIL(pend_que, &req->req, link);
			raid_suspend(suspend_cnt, nsid, type);
			break;

		case RAID_SCHED_ACT_RESUME:
			raid_sched->pend_cnt[nsid][type]++;
			QSIMPLEQ_INSERT_TAIL(pend_que, &req->req, link);
			raid_resume(stripe);
			break;

		default:
			sys_assert(false);
			break;
	}
}

fast_code void raid_sched_resume(void)
{
	ncl_w_req_t *req;
	raid_sched_que_t local_que;
	raid_sched_que_t *pend_que;

	pend_que = &raid_sched_mgr.pend_que;
	QSIMPLEQ_MOVE(&local_que, pend_que);
	memset(raid_sched_mgr.pend_cnt, 0, sizeof(raid_sched_mgr.pend_cnt));

	while (!QSIMPLEQ_EMPTY(&local_que)) {
		req = (ncl_w_req_t *)QSIMPLEQ_FIRST(&local_que);
		QSIMPLEQ_REMOVE_HEAD(&local_que, link);
		raid_sched_push(req);
	}
}

enum {
	RC_ST_CHK_STRIPE	= 0,
	RC_ST_ALLOC_DTAG	= 1,
	RC_ST_READ_DU	= 2,
	RC_ST_XOR_DU	= 3,
	RC_ST_POUT	= 4,
	RC_ST_MAX
};

/*! @brief raid correct state machine function array */
fast_data static fsm_funs_t _rc_st_func[] = {
	{"chk stripe", raid_correct_chk_stripe},
	{"alloc dtag", raid_correct_alloc_dtag},
	{"read du", raid_correct_read},
	{"xor du", raid_correct_xor},
	{"pout data", raid_correct_pout},
};

/*! @brief raid correct state machine */
fast_data static fsm_state_t _rc_fsm_state = {
	.name = "raid correct fsm",
	.fns = _rc_st_func,
	.max = ARRAY_SIZE(_rc_st_func)
};

/*!
 * @brief get next un-correct pda
 *
 * @return		true if get
 */
fast_code bool raid_correct_get_uc_pda(void)
{
	pda_t *pda;
	raid_correct_t *rc;
	struct info_param_t *info;

	rc = &raid_correct;
	info = rc->cur_req->info_list;
	pda = rc->cur_req->pda_list;

	while (rc->pda_idx < rc->cur_req->list_len) {
		if (info[rc->pda_idx].status > ficu_err_par_err){
			rc->uc_pda = pda[rc->pda_idx];
			ftl_raid_trace(LOG_WARNING, 0xc863, "get uc pda 0x%x", rc->uc_pda);
			// TODO: get org pda if support remap
			return true;
		}
		rc->pda_idx++;
	}

	return false;
}

/*!
 * @brief check if pda in open stripe
 *
 * @param stripe	pointer to stripe
 * @param un_pda	pda to check
 *
 * @return		true if hit
 */
fast_code bool hit_open_stripe(cwl_stripe_t* stripe, pda_t uc_pda)
{
	u32 spb = pda2blk(uc_pda);
	u32 page = pda2page(uc_pda);

	if (stripe->spb_id == spb) {
		u32 spage = stripe->fpage;
		u32 epage = spage + stripe->page_cnt - 1;
		if ((page >= spage) && (page <= epage)) {
			ftl_raid_trace(RAID_LEVEL, 0x91c1, "hit open stripe, spb %d page %d", spb, page);
			return true;
		}
	}

	return false;
}

fast_code fsm_res_t raid_correct_chk_stripe(fsm_ctx_t *fsm)
{
	u32 nsid;
	u32 type;
	rc_req_t *req;
	raid_correct_t *rc;
	stripe_user_t *su;
	cwl_stripe_t* stripe;

	rc = &raid_correct;
	req = rc->cur_req;

	//init var
	rc->wait_retry_cnt = 0;
	rc->plane_CPU2 = 0;
	rc->plane_CPU4 = 0;
	rc->flags.b.fail = false;

	rc->flags.b.open = false;
	nsid = (req->flags.b.host) ? (INT_NS_ID - 1) : INT_NS_ID;
	type = (req->flags.b.gc) ? FTL_CORE_GC : FTL_CORE_NRM;
	su = ftl_core_get_su(nsid, type);

	QSIMPLEQ_FOREACH(stripe, &su->cmpl_que, link) {
		if (hit_open_stripe(stripe, rc->uc_pda)) {
			rc->flags.b.open = true;
			evt_set_cs(rc->evt_open, nsid, type, CS_TASK);
			return FSM_PAUSE;
		}
	}

	QSIMPLEQ_FOREACH(stripe, &su->wait_que, link) {
		if (hit_open_stripe(stripe, rc->uc_pda)) {
			rc->flags.b.open = true;
			evt_set_cs(rc->evt_open, nsid, type, CS_TASK);
			return FSM_PAUSE;
		}
	}

	if (su->active && hit_open_stripe(su->active, rc->uc_pda)) {
		rc->flags.b.open = true;
		evt_set_cs(rc->evt_open, nsid, type, CS_TASK);
		return FSM_PAUSE;
	}

	return FSM_CONT;
}

fast_code void raid_correct_got_dtag(u32 dtag)
{
	raid_correct_t *rc = &raid_correct;

	rc->dtag.dtag = dtag;
	fsm_ctx_set(&rc->fsm, RC_ST_READ_DU);
	fsm_ctx_run(&rc->fsm);
}

fast_code fsm_res_t raid_correct_alloc_dtag(fsm_ctx_t *fsm)
{
	raid_correct_t *rc = &raid_correct;

	if (rc->cur_req->flags.b.stream) {
		cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_RC_DTAG_ALLOC, 0, 0);
		return FSM_PAUSE;
	} else {
		fsm_ctx_set(&rc->fsm, RC_ST_READ_DU);
		return FSM_JMP;
	}
}

/*!
 * @brief recovery read done handle function
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
fast_code void raid_correct_read_done(struct ncl_cmd_t *ncl_cmd)
{
	raid_correct_t *rc = &raid_correct;
    #if NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    #endif
	if (ncl_cmd->status && (ncl_cmd->retry_step == default_read)) {        
		ftl_core_trace(LOG_WARNING, 0xd787, "raid_correct_read_err, ncl_cmd: 0x%x ncl_cmd->status 0x%x", ncl_cmd, ncl_cmd->status);
		u8 cnt = ncl_cmd->addr_param.common_param.list_len;
		struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
		pda_t *pda_list = ncl_cmd->addr_param.common_param.pda_list;
		// recode retry pda
		for (u8 i = 0; i < cnt; i++)
		{
			if (info[i].status > ficu_err_par_err)
			{
// 				ftl_core_trace(LOG_DEBUG, 0, "PDA 0x%x, info[i].status 0x%x", pda_list[i], info[i].status);
				rc->pda[rc->wait_retry_cnt] = pda_list[i];
				rc->info[rc->wait_retry_cnt].status = info[i].status;
				rc->wait_retry_cnt++;
			}
		}
	}

	if(ncl_cmd->status && rc->wait_retry_cnt){	// read decode fail
        #if NCL_FW_RETRY
        if (ncl_cmd->retry_step == default_read)
        {
			////retry first pda and manual xor
			rc->backup_ncl_cmd_status = ncl_cmd->status;
			ncl_cmd->addr_param.common_param.list_len = 1;
			ncl_cmd->flags &= ~(NCL_CMD_RAID_XOR_ONLY_FLAG_SET|NCL_CMD_COMPLETED_FLAG);
	        ftl_core_trace(LOG_INFO, 0x0b3b, "raid_correct_read_err, ncl_cmd: 0x%x", ncl_cmd);
			//pda_t pda = ncl_cmd->addr_param.common_param.pda_list[0];
			u32 die = pda2die(rc->pda[0]);
			if (get_target_cpu(die) == CPU_ID)	
            	rd_err_handling(ncl_cmd);
			else
				cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_RD_ERR_HANDLING, 0, (u32) ncl_cmd);
			return;
        }
		#endif
		{
    		rc->flags.b.fail = true;
    		ftl_raid_trace(LOG_ERR, 0xb37f, "pda 0x%x read err status %d", rc->pda[0], rc->info[0].status); 
    		fsm_ctx_set(&rc->fsm, RC_ST_POUT);
    		fsm_ctx_run(&rc->fsm);
        }
	}
	else {	//read fall 
		if(rc->wait_retry_cnt)	//manual xor
			fsm_ctx_set(&rc->fsm, RC_ST_XOR_DU);
		else
			fsm_ctx_set(&rc->fsm, RC_ST_READ_DU);
		fsm_ctx_run(&rc->fsm);
	}
}

fast_code fsm_res_t raid_correct_read(fsm_ctx_t *fsm)
{

	u32 plane, plane_per_die;
	u32 uc_lun, uc_plane;
	u8 read_cnt = 0;
	u8 target_CPU;
	raid_correct_t *rc = &raid_correct;
	struct ncl_cmd_t *ncl_cmd = rc->ncl_cmd;

    plane_per_die = nand_plane_num();
	uc_lun = pda2lun_id(rc->uc_pda);
	uc_plane = pda2plane(rc->uc_pda) + uc_lun * plane_per_die;

	#ifdef SKIP_MODE
	u32 spb_id = (rc->uc_pda >> nand_info.pda_block_shift) & nand_info.pda_block_mask;
	u8 *spb_df_ptr = get_spb_defect(spb_id);
	#endif
calculate_pda:
	if(rc->plane_CPU2 <= rc->plane_CPU4){
		//use CPU2
		plane = rc->plane_CPU2;
		target_CPU = CPU_BE;
	}else{
		//use CPU4
		plane = rc->plane_CPU4;
		target_CPU = CPU_BE_LITE;

	}

	for (; plane < nand_info.lun_num * plane_per_die; plane++) {
		if ((plane == uc_plane) || (get_target_cpu(plane / plane_per_die) != target_CPU))
			continue;

		if (plane < uc_plane)
			rc->pda[read_cnt] = rc->uc_pda -  (uc_plane - plane) * DU_CNT_PER_PAGE;
		else
			rc->pda[read_cnt] = rc->uc_pda +  (plane - uc_plane) * DU_CNT_PER_PAGE;

		/* to check if this plane is bad */
		u32 plane_idx = (rc->pda[read_cnt] >> nand_info.pda_plane_shift) & (nand_info.interleave-1);
		u8 plane = plane_idx % nand_plane_num(); 
        plane_idx &= ~((shr_nand_info.geo.nr_planes - 1)); // &=...1100 (4pl) &=...1110 (2pl)
#ifdef SKIP_MODE
		u8 pln_pair = get_defect_pl_pair(spb_df_ptr, plane_idx);
#else
		u8 pln_pair = BLOCK_NO_BAD;
#endif

        if((1 << (plane % plane_per_die)) & pln_pair)
			continue;

 // 		ftl_raid_trace(LOG_INFO, 0, "uc_pda 0x%x read pda 0x%x plane %u die %u read_cnt %u CPU %u",
 //     		rc->uc_pda, rc->pda[read_cnt], plane, plane / plane_per_die, read_cnt+1,
 //     		get_target_cpu(plane / plane_per_die));
		read_cnt++;
		if(read_cnt >= DU_CNT_PER_RAID_XOR_CMD)
			break;
	}

/*	
	#else

	for (lun = rc->lun; lun < nand_info.lun_num; lun++) {
		// TODO: defect support

		if (lun == uc_lun)
			continue;

		if (lun < uc_lun)
			rc->pda[0] = rc->uc_pda -  (uc_lun - lun) * du_cnt_per_lun;
		else
			rc->pda[0] = rc->uc_pda +  (lun - uc_lun) * du_cnt_per_lun;

		read_done = false;
		break;
	}
	#endif //skip mode
*/
	//move to next lun
	if(target_CPU == CPU_BE){
		//use CPU2
		rc->plane_CPU2 = plane + 1;
	}else{
		//use CPU4
		rc->plane_CPU4 = plane + 1;
	}

	if (read_cnt == 0) {
		if((rc->plane_CPU2 >= nand_info.lun_num * plane_per_die) && (rc->plane_CPU4 >= nand_info.lun_num * plane_per_die))
		{
			fsm_ctx_set(&rc->fsm, RC_ST_POUT);
			return FSM_JMP;
		}
		else
			goto calculate_pda;
	}

	if (rc->cur_req->flags.b.stream) {
		for(u8 i=0;i<read_cnt;i++){
			rc->bm_pl[i].pl.btag = 0;
			rc->bm_pl[i].pl.du_ofst = 0;
			rc->bm_pl[i].pl.dtag = rc->dtag.b.dtag;
			rc->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG;
		}
	} else {
		void* src = &(rc->cur_req->bm_pl_list[rc->pda_idx]);
		for(u8 i=0;i<read_cnt;i++){
			memcpy(&rc->bm_pl[i], src, sizeof(bm_pl_t));
            rc->bm_pl[i].pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
            rc->bm_pl[i].pl.btag = 0;
		}
	}

	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_READ;

	ncl_cmd->flags = 0;
	ncl_cmd->flags |= (NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RCED_FLAG);
	ncl_cmd->flags |= NCL_CMD_RAID_XOR_ONLY_FLAG_SET;
#if defined(HMETA_SIZE) // Jamie 20210329
	ncl_cmd->flags |= NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG;
	ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG; 
#else
    ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#endif
    #if NCL_FW_RETRY
    ncl_cmd->retry_step = default_read;
    #endif
	ncl_cmd->addr_param.common_param.list_len = read_cnt;
	ncl_cmd->completion = raid_correct_read_done;

#if RAID_SUPPORT_UECC
    //ncl_cmd->caller_priv = (struct ncl_cmd_t *)rc->cur_req->caller_priv;

    //die_cmd_info_t *die_cmd_info = NULL;
    //ncl_cmd->caller_priv = uecc_ncl_cmd->caller_priv;
    //die_cmd_info = (die_cmd_info_t *)ncl_cmd->caller_priv;
    //if(rc->cur_req->flags.b.host)
    //{
        //die_cmd_info->cmd_info.b.host = 1;
        //if(rc->cur_req->flags.b.stream)
        //    die_cmd_info->cmd_info.b.stream = 1;
        //else if(rc->cur_req->flags.b.gc)
        //    die_cmd_info->cmd_info.b.gc = 1;
	//}
	if(FTL_INIT_DONE)
    {   
        if(rc->cur_req->flags.b.host)
            ncl_cmd->uecc_type = NCL_UECC_RC_HOST;
        else
    		ncl_cmd->uecc_type = NCL_UECC_RC_FTL;
    }
    else
    {
        struct ncl_cmd_t *uecc_ncl_cmd = (struct ncl_cmd_t *)rc->cur_req->caller_priv;
        if(uecc_ncl_cmd->uecc_type == NCL_UECC_L2P_RD)
            ncl_cmd->uecc_type = NCL_UECC_RC_L2P;
        else
            ncl_cmd->uecc_type = NCL_UECC_RC_SPOR;
    }
#endif

	for(u8 i=0;i<read_cnt;i++){
		rc->info[i].status = ficu_err_good;
	}
	if (ftl_core_get_spb_type(pda2blk(rc->uc_pda))) {
		for(u8 i=0;i<read_cnt;i++)
			rc->info[i].pb_type = NAL_PB_TYPE_SLC;
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	} else {
		for(u8 i=0;i<read_cnt;i++)
			rc->info[i].pb_type = NAL_PB_TYPE_XLC;
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;
	}


	if (target_CPU == CPU_ID)
		ncl_cmd_submit(ncl_cmd);
	else
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD, 0, (u32)ncl_cmd);

	return FSM_PAUSE;
}

/*!
 * @brief recovery xor operation done handle function
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
slow_code void raid_correct_xor_done(struct ncl_cmd_t *ncl_cmd)
{
	raid_correct_t *rc = &raid_correct;
#if NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
	if(rc->wait_retry_cnt)	// pop this pda
	{
		#if DU_CNT_PER_RAID_XOR_CMD > 1
		for (u8 i = 1; i < rc->wait_retry_cnt; i++)
		{
			rc->info[i-1].status = rc->info[i].status;
			rc->pda[i-1] = rc->pda[i];
		}
		#endif
		rc->wait_retry_cnt--;
	}

	if(rc->wait_retry_cnt)	// retry next pda
	{
		ncl_cmd->status = rc->backup_ncl_cmd_status;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
		ncl_cmd->flags &= ~(NCL_CMD_RAID_XOR_ONLY_FLAG_SET|NCL_CMD_COMPLETED_FLAG);
        ncl_cmd->retry_step = default_read;
#if defined(HMETA_SIZE)
	ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG; 
#else
    ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#endif
// 		ncl_cmd->addr_param.common_param.list_len = 1;
		ncl_cmd->completion = raid_correct_read_done;
		rd_err_handling(ncl_cmd);
	    return;
	}
	else
#endif
	{
		fsm_ctx_set(&rc->fsm, RC_ST_READ_DU);
		fsm_ctx_run(&rc->fsm);
	}
}

slow_code fsm_res_t raid_correct_xor(fsm_ctx_t *fsm)
{
	raid_correct_t *rc = &raid_correct;
	struct ncl_cmd_t *ncl_cmd = rc->ncl_cmd;

	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
#if defined(HMETA_SIZE)
	ncl_cmd->op_type = NCL_CMD_PROGRAM_DATA;
#else
	ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
#endif
    ncl_cmd->flags |= NCL_CMD_RAID_XOR_ONLY_FLAG_SET;
	ncl_cmd->flags &= ~(NCL_CMD_COMPLETED_FLAG);
	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->completion = raid_correct_xor_done;

// 	ftl_raid_trace(RAID_LEVEL, 0, "xor pda 0x%x", rc->pda[0]);

	rc->pda[0] = 0;
	ncl_cmd_submit(rc->ncl_cmd);
	return FSM_PAUSE;
}

/*!
 * @brief recovery parity out done handle function
 *
 * @param ncl_cmd	pointer to ncl command
 *
 * @return		not used
 */
fast_code void raid_correct_pout_done(struct ncl_cmd_t *ncl_cmd)
{
	raid_correct_t *rc = &raid_correct;
	struct info_param_t *info= rc->cur_req->info_list;
	bm_pl_t *plt = &rc->cur_req->bm_pl_list[rc->pda_idx];

	ftl_raid_trace(RAID_LEVEL, 0x0ca9, "pda 0x%x done, sts %d", rc->uc_pda, rc->flags.b.fail);

	if (rc->flags.b.fail) {
		rc->fail_cnt++;
		rc->ttl_fail_cnt++;

		if(rc->cur_req->flags.b.gc){
			extern __attribute__((weak)) void nvmet_evt_aer_in();
			nvmet_evt_aer_in((2<<16)|9,0);
		}

		if (rc->cur_req->flags.b.stream) 
        {

            #ifdef ERRHANDLE_ECCT
            #if 1
            Reg_ECCT_Fill_Info(plt, rc->uc_pda, HOST_ECCT_TYPE, ECC_REG_RECV);
            #else
            memcpy(&ecct_pl_info[ecct_pl_cnt].bm_pl, plt, sizeof(bm_pl_t));
            ecct_pl_info[ecct_pl_cnt].bm_pl.pl.type_ctrl &= 0;
            ecct_pl_info[ecct_pl_cnt].type = HOST_ECCT_TYPE;
            ftl_raid_trace(RAID_LEVEL, 0xd4ef, "[ECCT] HOST reg ECCT in rc fail, pda: 0x%x, dtag: 0x%x", rc->uc_pda, ecct_pl_info[ecct_pl_cnt].bm_pl.pl.dtag);
            rc_reg_ecct(&ecct_pl_info[ecct_pl_cnt].bm_pl, ecct_pl_info[ecct_pl_cnt].type);

            if(ecct_pl_cnt >= MAX_RC_REG_ECCT_CNT - 1)
            {
                ecct_pl_cnt = 0;
            }
            else
            {
                ecct_pl_cnt++;
            }
            #endif

            #endif

            bm_err_commit(plt->pl.du_ofst, plt->pl.btag);

			//release dtag when correct fail
			if (!rc->flags.b.open)
				cpu_msg_issue(CPU_DTAG - 1, CPU_MSG_RC_DTAG_FREE, 0, rc->dtag.dtag);
		}
        #ifdef ERRHANDLE_ECCT
        else if(rc->cur_req->flags.b.gc)
        {
            rc->cur_req->bm_pl_list[rc->pda_idx].pl.btag = 0xFFF;  //gc bed blk mark
            u32 gc_dtag_idx = plt->pl.du_ofst;
            gc_dtag_idx = DTAG_GRP_IDX_TO_IDX(gc_dtag_idx, plt->pl.nvm_cmd_id);

            #if 1
            Reg_ECCT_Fill_Info(plt, rc->uc_pda, GC_ECCT_TYPE, ECC_REG_RECV);
            #else
            ftl_raid_trace(RAID_LEVEL, 0x8833, "[ECCT] GC reg ECCT in rc fail, lda: 0x%x, pda: 0x%x, dtag_idx: 0x%x", gc_lda_list[gc_dtag_idx], rc->uc_pda, gc_dtag_idx); 
            memcpy(&ecct_pl_info[ecct_pl_cnt].bm_pl, plt, sizeof(bm_pl_t));
            ecct_pl_info[ecct_pl_cnt].bm_pl.pl.type_ctrl &= 0;
            ecct_pl_info[ecct_pl_cnt].type = GC_ECCT_TYPE;
            rc_reg_ecct(&ecct_pl_info[ecct_pl_cnt].bm_pl, ecct_pl_info[ecct_pl_cnt].type);
            
            if(ecct_pl_cnt >= MAX_RC_REG_ECCT_CNT - 1)
            {
                ecct_pl_cnt = 0;
            }
            else
            {
                ecct_pl_cnt++;
            }
            #endif
        }
        #endif   
        else if (plt->pl.btag == ua_btag)
        {
            ftl_raid_trace(LOG_ERR, 0x771e, "fill-up fail");
            info[rc->pda_idx].status = ficu_err_raid;
            ipc_api_ucache_read_error_data_in(plt->pl.du_ofst, info[rc->pda_idx].status);
        }

        struct ncl_cmd_t *err_ncl_cmd = (struct ncl_cmd_t *)rc->cur_req->caller_priv;
        if(err_ncl_cmd->flags & NCL_CMD_P2L_READ_FLAG){
		    info[rc->pda_idx].status = ficu_err_raid; //mark the uncorrected status
        }
	} else {
		rc->ttl_success_cnt++;

		//mark the corrected status as good
		info[rc->pda_idx].status = ficu_err_good;

		//host streaming read, xfer corrected data to host
		if (rc->cur_req->flags.b.stream) {
            bool ret = false;
			plt = rc->cur_req->bm_pl_list;
			bm_pl_t bm_pl = {.all=plt[rc->pda_idx].all};
			bm_pl.pl.dtag = rc->dtag.b.dtag;
            CBF_INS(&shr_rc_host_queue, ret, bm_pl.all);
            sys_assert(ret == true);
			ftl_raid_trace(RAID_LEVEL, 0x3993, "xfer data, nvm cmd id %d, btag %d, off %d, dtag %d",
				bm_pl.pl.nvm_cmd_id, bm_pl.pl.btag, bm_pl.pl.du_ofst, bm_pl.pl.dtag);
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_XFER_RC_DATA, 0, 0);
		}
        else if (plt->pl.btag == ua_btag)
        {
            ipc_api_ucache_read_error_data_in(plt->pl.du_ofst, info[rc->pda_idx].status);
        }
	}


	//move to next uc pda
	rc->pda_idx++;
	if(raid_correct_get_uc_pda()) {
		fsm_ctx_set(&rc->fsm, RC_ST_CHK_STRIPE);
		fsm_ctx_run(&rc->fsm);
		return;
	}

	//don't get uc pda, req correct done

    rc->ttl_pending_cnt --;

	ftl_raid_trace(RAID_LEVEL, 0x7111, "req 0x%x rc done, pending %u",
        rc->cur_req, rc->ttl_pending_cnt);

	if (rc->fail_cnt)
		rc->cur_req->flags.b.fail = true;

	rc->flags.b.busy = false;
	QSIMPLEQ_REMOVE_HEAD(&rc->pend_que, link);
	void __raid_correct_done(rc_req_t *rc_req);

	if(rc->cur_req->flags.b.remote)
		__raid_correct_done(rc->cur_req);
	else
		rc->cur_req->cmpl(rc->cur_req);

	if (!QSIMPLEQ_EMPTY(&rc->pend_que))
		evt_set_cs(rc->evt_trig, 0, 0, CS_TASK);
// 	else if(FTL_INIT_DONE == true) // The evlog is not initialized until ftl is initialized
//         flush_to_nand(EVT_RAID_RECVOER_DONE);
}

fast_code fsm_res_t raid_correct_pout(fsm_ctx_t *fsm)
{
	raid_correct_t *rc = &raid_correct;
	struct ncl_cmd_t *ncl_cmd = rc->ncl_cmd;

	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
    #if NCL_FW_RETRY
    ncl_cmd->retry_step = default_read;
    #endif
	ncl_cmd->flags = 0;
	ncl_cmd->flags |= NCL_CMD_TAG_EXT_FLAG;
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd->flags |= NCL_CMD_RAID_POUT_FLAG_SET;

	ncl_cmd->completion = raid_correct_pout_done;
	ncl_cmd->addr_param.common_param.list_len = 1;

#if defined(HMETA_SIZE)
    if(cmf_idx == 4 && !rc->flags.b.fail){ // workaround for raid recover PI error with 4K + 8 
        void * saddr = get_raid_sram_buff_addr(RAID_CORRECT_RAID_ID); 
        if(saddr != NULL){ 
            saddr += sizeof(io_meta_t) + DTAG_SZE; 
            memcpy(saddr + HMETA_SIZE, saddr, HMETA_SIZE); 
        } 
    } 
#endif

    dtag_t dtag = {.dtag = rc->bm_pl[0].pl.dtag};
    if(dtag.b.in_ddr && dtag.b.dtag >= max_ddr_dtag_cnt) {
        // disable hcrc check if no hcrc buffer
        ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    }

	ncl_cmd_submit(ncl_cmd);
	return FSM_PAUSE;
}

fast_code void raid_open_stripe_close_wl_done(ftl_core_ctx_t *ctx)
{
    raid_correct_t *rc = &raid_correct;
    fsm_ctx_set(&rc->fsm, RC_ST_CHK_STRIPE);  //check open stripe again
	fsm_ctx_run(&rc->fsm);
}

fast_code void raid_correct_open_strtipe(u32 parm, u32 payload, u32 sts)
{
	raid_correct_t *rc = &raid_correct;
    u8 nsid = (u8)payload;
    u8 type = (u8)sts;
	sys_assert(rc->flags.b.open == true);

    ftl_raid_trace(RAID_LEVEL, 0xf845, "open stripe corrent nsid:%d type:%d", nsid, type);

    fcore_fill_dummy(type, type, FILL_TYPE_WL, raid_open_stripe_close_wl_done, NULL);
    rc->flags.b.open = false;
    //now we don't correct open stripe
	//rc->flags.b.fail = true;
	//raid_correct_pout_done(rc->ncl_cmd);
}

fast_code void raid_correct_trigger(u32 parm, u32 payload, u32 sts)
{
	raid_correct_t *rc = &raid_correct;

	rc->pda_idx = 0;
	rc->fail_cnt = 0;
	//pop a req to do correct
	rc->cur_req = QSIMPLEQ_FIRST(&rc->pend_que);
#if defined(HMETA_SIZE)
    rc->ncl_cmd->du_format_no = host_du_fmt;  // Jamie 20210324
#endif

	ftl_raid_trace(RAID_LEVEL, 0xe368, "trigger req 0x%x do rc, pending %u",
        rc->cur_req, rc->ttl_pending_cnt);
// 	if(FTL_INIT_DONE == true) // The evlog is not initialized until ftl is initialized
//     	flush_to_nand(EVT_RAID_TRIGGER);
	//move to uc pda
	bool ret = raid_correct_get_uc_pda();
	sys_assert(ret == true);

	//start correction
	rc->flags.b.busy = true;
	fsm_ctx_init(&rc->fsm, &_rc_fsm_state, NULL, NULL);
	fsm_ctx_run(&rc->fsm);
}

fast_code void raid_correct_push(rc_req_t *req)
{
	raid_correct_t *rc = &raid_correct;

	QSIMPLEQ_INSERT_TAIL(&rc->pend_que, req, link);

    rc->ttl_pending_cnt ++;

	if (rc->flags.b.busy == false)
		evt_set_cs(rc->evt_trig, 0, 0, CS_TASK);
}

slow_code void ftl_raid_suspend(void)
{
	sys_assert(raid_sched_mgr.busy == false);
	sys_assert(QSIMPLEQ_EMPTY(&raid_sched_mgr.pend_que));

	sys_assert(raid_correct.flags.b.busy == false);
	sys_assert(QSIMPLEQ_EMPTY(&raid_correct.pend_que));

	u32 i;
	//now we don't support parity data in ddr when pmu
	for (i = 0; i < RAID_BANK_NUM; i++)
		sys_assert(raid_bank_mgr.ttl_id_cnt[i] <= RAID_BUF_PER_BANK);

	ncl_raid_status_save();
}

slow_code void ftl_raid_resume(void)
{
	memset(&raid_correct, 0, sizeof(raid_correct_t));
	memset(&raid_sched_mgr, 0, sizeof(raid_sched_mgr_t));

	QSIMPLEQ_INIT(&raid_correct.pend_que);
	QSIMPLEQ_INIT(&raid_sched_mgr.pend_que);

	raid_sched_mgr.ncl_cmd = &sched_ncl_cmd;
	raid_sched_mgr.ncl_cmd->user_tag_list = sched_bm;
	raid_sched_mgr.ncl_cmd->addr_param.common_param.pda_list = sched_pda;
	raid_sched_mgr.ncl_cmd->addr_param.common_param.info_list = sched_info;

	raid_correct.ncl_cmd = &rc_ncl_cmd;
	rc_ncl_cmd.caller_priv = &raid_correct;
	rc_ncl_cmd.du_format_no = DU_4K_DEFAULT_MODE;

	raid_correct.info = rc_info;
	raid_correct.bm_pl = rc_bm_pl;
	rc_ncl_cmd.user_tag_list = raid_correct.bm_pl;
	rc_ncl_cmd.addr_param.common_param.info_list = raid_correct.info;
	rc_ncl_cmd.addr_param.common_param.pda_list = raid_correct.pda;

	u32 i, j, k;
	for (i = 0; i < DU_CNT_PER_RAID_XOR_CMD; i++) {
		rc_info[i].raid_id = RAID_CORRECT_RAID_ID;
		rc_info[i].bank_id = RAID_CORRECT_BANK_ID;
	}

	//restrore raid engine status first
	ncl_raid_status_restore();

      /*
       * raid id's xor cmpl cnt is not in raid engine, these status has been reset when pmu,
       * we need to restrore each raid id's xor cmpl cnt and expected cnt with stripe context
	*/
	u32 left_du_cnt;
	cwl_stripe_t *stripe;
	for (i = 0; i < MAX_CWL_STRIPE; i++) {
		if (cwl_stripe[i].state == STRIPE_ST_FREE)
			continue;

		stripe = &cwl_stripe[i];
		left_du_cnt = (nand_info.lun_num - stripe->done_wr - 1) * DU_CNT_PER_PAGE;

		for (j = 0; j < stripe->page_cnt; j++) {
			for (k = 0; k < nand_plane_num(); k++) {
				u32 raid_id = stripe->raid_id[j][0];
				ncl_raid_set_cmpl_para(raid_id, left_du_cnt, true, true);
			}
		}
	}
}

static ddr_code void raid_chk_recovery_cmpl(rc_req_t *req)
{
	struct ncl_cmd_t *ncl_cmd = &raid_chk_cmd;
	u32 intlv_du_cnt = nand_interleave_num() * DU_CNT_PER_PAGE;

	if (req->flags.b.fail)
		raid_chk.ttl_err++;

	//cmp org data and recovered data
	if ((ncl_cmd->status == 0) && (req->flags.b.fail == 0)) {
		void *src = dtag2mem(raid_chk.dtag[0]);
		void *dst = dtag2mem(raid_chk.dtag[1]);

		void *src_meta = &raid_chk.meta[0];
		void *dst_meta = &raid_chk.meta[1];

		if (memcmp(src, dst, DTAG_SZE)) {
			raid_chk.ttl_fail++;
			ftl_raid_trace(LOG_ERR, 0xc98d, "spb %d page %d fail", raid_chk.spb, raid_chk.page);
		} else {
			raid_chk.ttl_pass++;
			ftl_raid_trace(RAID_LEVEL, 0x0cb7, "spb %d page %d pass", raid_chk.spb, raid_chk.page);
		}

		if (memcmp(src_meta, dst_meta, sizeof(struct du_meta_fmt))) {
			raid_chk.ttl_meta_fail++;
			ftl_raid_trace(LOG_ERR, 0xd233, "spb %d page %d meta fail", raid_chk.spb, raid_chk.page);
		} else {
			raid_chk.ttl_meta_pass++;
			ftl_raid_trace(RAID_LEVEL, 0x4221, "spb %d page %d meta pass", raid_chk.spb, raid_chk.page);
		}
	}


	if (--raid_chk.chk_cnt == 0) {
// 		dtag_put_bulk(DTAG_T_SRAM, 2, raid_chk.dtag);
		ftl_raid_trace(LOG_ALW, 0x5f74, "spb %d raid chk done, pass %d fail %d err %d, time %d",
			raid_chk.spb, raid_chk.ttl_pass, raid_chk.ttl_fail, raid_chk.ttl_err, jiffies - raid_chk.start_time);
		ftl_raid_trace(LOG_ALW, 0x4d36, "raid_chk.ttl_meta_fail %d, raid_chk.ttl_meta_pass %d",
			raid_chk.ttl_meta_fail, raid_chk.ttl_meta_pass);
		return;
	}

	raid_chk.page += raid_chk.interval;
	raid_chk.pda = nal_make_pda(raid_chk.spb, raid_chk.page * intlv_du_cnt);
	raid_chk.pda = ftl_remap_pda(raid_chk.pda);

	ncl_cmd->flags &= ~(NCL_CMD_COMPLETED_FLAG);

	ncl_cmd->status = 0;
	raid_chk.info[1].status = ficu_err_good;

	u32 die = pda2lun_id(raid_chk.pda);
	if (get_target_cpu(die) == CPU_ID)
		ncl_cmd_submit(ncl_cmd);
	else
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD, 0, (u32)ncl_cmd);
}

static ddr_code void raid_chk_org_data_read_done(struct ncl_cmd_t *ncl_cmd)
{
    ftl_raid_trace(LOG_ERR, 0xd0ff, "ncl_cmd->status %x status %u", ncl_cmd->status, raid_chk.info[1].status);
	if (ncl_cmd->status) {
		ftl_raid_trace(LOG_ERR, 0x5016, "pda 0x%x read fail", raid_chk.pda);
        raid_chk_req.flags.b.fail = true;
        raid_chk_recovery_cmpl(&raid_chk_req);
        return;
	}

	//trigger raid recovery
	raid_chk_req.flags.all = 0;
	raid_chk.info[0].status = ficu_err_du_uc;
	raid_correct_push(&raid_chk_req);
}

static ddr_code bool raid_chk_init(void)
{
	//alloc dtag for raid recovery and original data read
// 	u32 got = dtag_get_bulk(DTAG_T_SRAM, 2, raid_chk.dtag);
// 	if (got < 2) {
// 		if (got)
// 			dtag_put_bulk(DTAG_T_SRAM, got, raid_chk.dtag);

// 		ftl_raid_trace(LOG_ERR, 0, "dtag alloc fail");
// 		return false;
// 	}

	raid_chk.ttl_err = 0;
	raid_chk.ttl_fail = 0;
	raid_chk.ttl_pass = 0;
	raid_chk.ttl_meta_fail = 0;
	raid_chk.ttl_meta_pass = 0;
	raid_chk.start_time = jiffies;

	//init raid recovery check context
	if (ftl_core_get_spb_type(raid_chk.spb)) {
		raid_chk.spb_type = NAL_PB_TYPE_SLC;
		raid_chk.interval = nand_page_num_slc() / raid_chk.chk_cnt;
	} else {
		raid_chk.spb_type = NAL_PB_TYPE_XLC;
		raid_chk.interval = nand_page_num() / raid_chk.chk_cnt;
	}

	raid_chk.info[0].status = ficu_err_du_uc;
	raid_chk.info[0].pb_type = raid_chk.spb_type;

	raid_chk.bm_pl[0].pl.du_ofst = 0;
	raid_chk.bm_pl[0].pl.dtag = raid_chk.dtag[0].dtag;
	raid_chk.bm_pl[0].pl.nvm_cmd_id = raid_chk.meta_idx_base;
	raid_chk.bm_pl[0].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;

	raid_chk_req.list_len = 1;
	raid_chk_req.pda_list = &raid_chk.pda;
	raid_chk_req.info_list = &raid_chk.info[0];
	raid_chk_req.bm_pl_list = &raid_chk.bm_pl[0];

	raid_chk_req.flags.all = 0;
	raid_chk_req.caller_priv = NULL;
	raid_chk_req.cmpl = raid_chk_recovery_cmpl;

	//init original data read
	raid_chk.info[	1] = raid_chk.info[0];
	raid_chk.info[	1].status = ficu_err_good;

	raid_chk.bm_pl[1] = raid_chk.bm_pl[0];
	raid_chk.bm_pl[1].pl.dtag = raid_chk.dtag[1].dtag;
	raid_chk.bm_pl[1].pl.nvm_cmd_id = raid_chk.meta_idx_base + 1;
	raid_chk.bm_pl[1].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;

	struct ncl_cmd_t *ncl_cmd = &raid_chk_cmd;
	ncl_cmd->user_tag_list = &raid_chk.bm_pl[1];
	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = &raid_chk.pda;
	ncl_cmd->addr_param.common_param.info_list = &raid_chk.info[1];
#if defined(HMETA_SIZE)
	ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG;
#else
    ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
#endif
	if (raid_chk.spb_type == NAL_PB_TYPE_SLC)
		ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	else if (raid_chk.spb_type == NAL_PB_TYPE_XLC)
		ncl_cmd->flags |= NCL_CMD_XLC_PB_TYPE_FLAG;

	ncl_cmd->du_format_no = host_du_fmt;
	ncl_cmd->op_code = NCL_CMD_OP_READ;
#if defined(HMETA_SIZE)
	ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
#else
    ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
#endif
    #if NCL_FW_RETRY
    ncl_cmd->retry_step = default_read;
    #endif

	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = raid_chk_org_data_read_done;


	return true;
}

static ddr_code int raid_chk_main(int argc, char *argv[])
{
	u32 mode = strtol(argv[1], (void *)0, 0);

    ftl_raid_trace(LOG_ERR, 0x0a32, "raid_chk_main");

	if (mode == 0) {
		//check a pda
		raid_chk.chk_cnt = 1;
		raid_chk.pda = strtol(argv[2], (void *)0, 0);
		raid_chk.spb = pda2blk(raid_chk.pda);
		raid_chk.pda = ftl_remap_pda(raid_chk.pda);
	} else if (mode == 1) {
		//check a closed spb
		raid_chk.spb = strtol(argv[2], (void *)0, 0);
		raid_chk.chk_cnt = strtol(argv[3], (void *)0, 0);

		raid_chk.page = 0;
		raid_chk.pda = nal_make_pda(raid_chk.spb, 0);
		raid_chk.pda = ftl_remap_pda(raid_chk.pda);
    } else if (mode == 2) {
	    raid_correct_t *rc = &raid_correct;
		ftl_raid_trace(LOG_ERR, 0x1e31, "rc->ttl_pending_cnt %u, rc->flags.b.busy %u", rc->ttl_pending_cnt, rc->flags.b.busy);

		return 0;
	} else {
		ftl_raid_trace(LOG_ERR, 0xcb02, "invalid raid check mode");
		return 1;
	}

	if (raid_chk_init()) {
		//read org data first
		ftl_raid_trace(LOG_ERR, 0xaddd, "ncl_cmd_submit(&raid_chk_cmd); raid_chk.pda 0x%x", raid_chk.pda);
		u32 die = pda2lun_id(raid_chk.pda);
		if (get_target_cpu(die) == CPU_ID)
			ncl_cmd_submit(&raid_chk_cmd);
		else
			cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD, 0, (u32)&raid_chk_cmd);
	}

	return 0;
}

static slow_code int raid_dump_main(int argc, char *argv[])
{
	u32 i;
	ncl_req_t *req;

	for (i = 0; i < RAID_BANK_NUM; i++) {
		ftl_raid_trace(LOG_ALW, 0x65b4, "bank %d: ttl %d, free %d, switch %d", i,
			raid_bank_mgr.ttl_id_cnt[i],
			raid_bank_mgr.free_id_cnt[i],
			raid_bank_mgr.switchable_id_cnt[i]);
	}

	for (i = 0; i < TOTAL_NS; i++) {
		ftl_raid_trace(LOG_ALW, 0xba23, "ns %d pend %d %d", i,
			raid_sched_mgr.pend_cnt[i][0],
			raid_sched_mgr.pend_cnt[i][1]);
	}

	QSIMPLEQ_FOREACH(req, &raid_sched_mgr.pend_que, link) {
		ftl_raid_trace(LOG_ALW, 0x951b, "req 0x%x stripe %d ns %d type %d, xor %d",
			req, req->stripe_id, req->nsid, req->type, req->op_type.b.xor);
	}

	return 0;
}

static DEFINE_UART_CMD(raid_chk, "raid_chk", "raid_chk",
			"raid_chk [pda] [chk_du_cnt]", 1, 3, raid_chk_main);

static DEFINE_UART_CMD(raid_dump, "raid_dump", "raid_dump",
			"raid_dump", 0, 0, raid_dump_main);

/*! @} */
