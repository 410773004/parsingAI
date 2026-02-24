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

/*! \file read_error.c
 * @brief scheduler module, handle read error PDA
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#include "types.h"
#include "sect.h"
#include "ddr.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "fc_export.h"
#include "ipc_api.h"
#include "read_error.h"
#include "l2cache.h"
#include "mpc.h"
#include "read_retry.h"
#include "ficu.h"
#include "die_que.h"
#include "scheduler.h"
#include "GList.h"
#include "event.h"
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
#include "nvme_spec.h"
#endif
/*! \cond PRIVATE */
#define __FILEID__ rder
#include "trace.h"
/*! \endcond */

share_data_zi bool FTL_INIT_DONE;
fast_data u8 evt_rd_err_ncl_cmd_submit = 0xff;  //Prepare to register a eds_index.

extern vth_que_t hp_vth_que;
extern vth_que_t pdu_vth_que;
extern void vth_queue_handler();
extern u8 vth_cs_flag;
extern volatile u8 plp_trigger;
#ifdef NCL_FW_RETRY_EX
ddr_data_ni rd_err_que_t pdu_err_que_ex[RTY_QUEUE_CH_CNT]; //4CH * 2CPU
slow_data u8 rty_dtag_ptr_ex[RTY_QUEUE_CH_CNT] = {0};
fast_data_zi read_error_state_t read_error_state_ex[RTY_QUEUE_CH_CNT] = {0};
fast_data_ni struct ncl_cmd_t pdu_err_ncl_cmd_ex[RTY_QUEUE_CH_CNT];
fast_data_zi u8 complete_du_cnt_ex[RTY_QUEUE_CH_CNT] = {0};
slow_data u8 rty_que_dtag_cmit_cnt[RTY_QUEUE_CH_CNT] = {0}; //by CH

ddr_data_ni rd_err_que_t hp_read_err_que[RTY_QUEUE_CH_CNT];
fast_data_ni volatile bool hp_retry_flag[RTY_QUEUE_CH_CNT];

//fast_data_ni u8 hp_cur_ch_id = 0;
slow_data rd_err_ent_t retry_entry;
slow_data rd_err_ent_t current_retry_entry[RTY_QUEUE_CH_CNT];
slow_data bm_pl_t retry_bm_pl_list[RTY_QUEUE_CH_CNT][DU_CNT_PER_PAGE];
ddr_data_ni u8 hdled_err_du_cnt;
//extern u8 recommit_ptr;
#else
ddr_data_ni rd_err_que_t pdu_err_que;
fast_data_ni struct ncl_cmd_t pdu_err_ncl_cmd;
ddr_data_ni rd_err_que_t hp_read_err_que;
#endif

ddr_data_ni volatile bool hp_retry_en;
ddr_data_ni volatile bool hp_retry_done;

//fast_data_ni struct ncl_cmd_t pdu_err_ncl_cmd;
fast_data_ni read_error_state_t read_error_state;
fast_data u8 evt_errhandle_mon = 0xff;

share_data_zi void *shr_dtag_meta;
share_data_zi void *shr_ddtag_meta;
share_data volatile enum du_fmt_t host_du_fmt;
share_data_zi volatile u16 ua_btag;

#if (CO_SUPPORT_READ_AHEAD == TRUE)
share_data_zi u16 ra_btag;
#endif

#ifdef RD_FAIL_GET_LDA
share_data rd_get_lda_t *rd_err_lda_entry_0;
share_data rd_get_lda_t *rd_err_lda_entry_1;
share_data rd_get_lda_t *rd_err_lda_entry_ua;
share_data u16 rd_err_lda_ptr_0;
share_data u16 rd_err_lda_ptr_1;
#endif

#if !NCL_FW_RETRY
static void hst_pdu_err_handling(rd_err_ent_t *ent, dtag_t dtag);
#endif
extern bool dref_comt_poll(void);

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
extern struct nvme_telemetry_host_initated_log *telemetry_host_data;

ddr_code void get_telemetry_ErrHandleVar_CPU_BE() //CPU_MSG_FW_ARD_HANDLER, handle FW ARD queue
{  
#if (CPU_ID == CPU_BE)
    telemetry_host_data->errorhandle.tel_total_retry_du_cnt_cpu2 = total_retry_du_cnt;
    telemetry_host_data->errorhandle.tel_vth_cs_flag_cpu2 = vth_cs_flag;

    for(u8 i = 0; i < RTY_QUEUE_CH_CNT; i++)
    {
        telemetry_host_data->errorhandle.tel_read_error_state_ex_cpu2[i] = read_error_state_ex[i];
        telemetry_host_data->errorhandle.tel_hp_retry_flag_cpu2[i] = hp_retry_flag[i];
        telemetry_host_data->errorhandle.tel_err_ncl_cmd_retry_step_cpu2[i] = pdu_err_ncl_cmd_ex[i].retry_step;
        
    }
#elif (CPU_ID == CPU_BE_LITE)
    telemetry_host_data->errorhandle.tel_total_retry_du_cnt_cpu4 = total_retry_du_cnt;
    telemetry_host_data->errorhandle.tel_vth_cs_flag_cpu4 = vth_cs_flag;

    for(u8 i = 0; i < RTY_QUEUE_CH_CNT; i++)
    {
        telemetry_host_data->errorhandle.tel_read_error_state_ex_cpu4[i] = read_error_state_ex[i];
        telemetry_host_data->errorhandle.tel_hp_retry_flag_cpu4[i] = hp_retry_flag[i];
        telemetry_host_data->errorhandle.tel_err_ncl_cmd_retry_step_cpu4[i] = pdu_err_ncl_cmd_ex[i].retry_step;
        
    }
#endif
    cpu_msg_issue(CPU_FE - 1, CPU_MSG_GET_TELEMETRY_DATA_DONE, 0, 0);
}

ddr_code void __get_telemetry_ErrHandleVar_CPU_BE(volatile cpu_msg_req_t *req) //CPU_MSG_FW_ARD_HANDLER
{
    get_telemetry_ErrHandleVar_CPU_BE();
}
#endif

#ifdef NCL_FW_RETRY_EX
ddr_code void rd_err_ncl_cmd_submit_trigger(u32 parm, u32 payload, u32 sts)
{
    //Submit pdu_err_ncl_cmd_ex if read_error_state_ex == RD_SET_CMD_BY_EVT_TRIG.
    for(u8 i = 0; i < RTY_QUEUE_CH_CNT; i++)
    {
        if(read_error_state_ex[i] == RD_SET_CMD_BY_EVT_TRIG)
        {
            read_error_state_ex[i] = RD_RECOVERY_START;
            schl_apl_trace(LOG_ERR, 0x2713, "[EH] rd_err_ncl_cmd_submit_trigger, ch_id = %d ",i);
            ncl_cmd_submit(&pdu_err_ncl_cmd_ex[i]);
        }
    }
}

slow_code void ent_ptr_accumulate(u8 *ptr, u16 max_cnt)
{
    if(*ptr >= (max_cnt - 1))   ////todo: dtag handle
        *ptr = 0;
    else
        (*ptr)++;

    return;
}

ddr_code dtag_t rty_get_dtag(u8 ch_id)
{
    dtag_t dtag;

    if(rty_que_dtag_cmit_cnt[ch_id] < RTY_DTAG_CNT_PER_CH)
    {
        dtag.b.dtag = DDR_RTY_RECOVERY_EX_START + (ch_id * RTY_DTAG_CNT_PER_CH) + rty_dtag_ptr_ex[ch_id];
        dtag.b.in_ddr = 1;
        rty_que_dtag_cmit_cnt[ch_id]++;
        ent_ptr_accumulate(&rty_dtag_ptr_ex[ch_id], RTY_DTAG_CNT_PER_CH);
    }
    else
    {
        dtag.dtag = INV_U32;
    }

    return dtag;
}

ddr_code bool is_open_blk(struct ncl_cmd_t *ncl_cmd)
{
    extern spb_rt_flags_t *spb_rt_flags;
    u16 spb = pda2blk(ncl_cmd->addr_param.common_param.pda_list[0]);

    return spb_rt_flags[spb].b.open;
}


ddr_code void hst_rd_err_recover(u8 ch_id)
{
    struct ncl_cmd_t *ncl_cmd = NULL;
    dtag_t dtag;

    vth_queue_handler();
#ifdef History_read
    u8 patrol_read_flag = false;
#endif
    if ((!CBF_EMPTY(&hp_read_err_que[ch_id])) && (complete_du_cnt_ex[ch_id] == 0))
    {
        CBF_HEAD(&hp_read_err_que[ch_id], current_retry_entry[ch_id]);
        ncl_cmd = &pdu_err_ncl_cmd_ex[ch_id];
        hp_retry_flag[ch_id] = true;
        read_error_state_ex[ch_id] = RD_RECOVERY_START;
		pdu_err_ncl_cmd_ex[ch_id].retry_cnt = 0;
    }
    else if(!CBF_EMPTY(&pdu_err_que_ex[ch_id]))
    {
        CBF_HEAD(&pdu_err_que_ex[ch_id], current_retry_entry[ch_id]);
        ncl_cmd = &pdu_err_ncl_cmd_ex[ch_id];
        hp_retry_flag[ch_id] = false;
        read_error_state_ex[ch_id] = RD_RECOVERY_START;
        #ifdef History_read
        patrol_read_flag = (current_retry_entry[ch_id].ncl_cmd->op_code == NCL_CMD_PATROL_READ)?true:false;
        pdu_err_ncl_cmd_ex[ch_id].retry_cnt = (patrol_read_flag==1)?1:0;
        #else
        pdu_err_ncl_cmd_ex[ch_id].retry_cnt = 0;
        #endif
    }
    else
    {
        hp_retry_flag[ch_id] = false;
        //schl_apl_trace(LOG_INFO, 0, "[DBG] ch_id[%d] recover done, ttl_need_rty_du_cnt[%d]", ch_id, total_retry_du_cnt);
        return;
    }

    if (current_retry_entry[ch_id].du_sts > ficu_err_du_ovrlmt
#ifdef History_read
        || current_retry_entry[ch_id].du_sts == ficu_err_du_ovrlmt
#endif
    )
    {

        #ifdef History_read
        if((!patrol_read_flag) && FTL_INIT_DONE) //only for ftl_init_done flag up, history_read_start_ptr had initialized
        {
            ncl_cmd->retry_step = retry_history_step;
        }
        else
        {
            ncl_cmd->retry_step = retry_set_shift_value;
        }
        #else
        ncl_cmd->retry_step = retry_set_shift_value;
        #endif
        
        #if OPEN_BLK_RETRY
        if(is_open_blk(current_retry_entry[ch_id].ncl_cmd) && (!hp_retry_flag[ch_id]))
        {
            ncl_cmd->retry_step = retry_set_shift_value;
            ncl_cmd->retry_cnt = OPEN_BLK_RR_VALUE_IDX;
        }
        #endif
        
        #if SPOR_RETRY
        if(current_retry_entry[ch_id].ncl_cmd->retry_step == spor_retry_type)
        {
            ncl_cmd->retry_step = spor_retry_step;
            ncl_cmd->retry_cnt = 0;
        }
        #endif

        if (current_retry_entry[ch_id].op_type & DYNAMIC_DTAG)
        {
            u8 i;
            sys_assert(rty_que_dtag_cmit_cnt[ch_id] == 0);
            for(i = 0; i < current_retry_entry[ch_id].need_retry_du_cnt; i++)
            {
                dtag = rty_get_dtag(ch_id);
                //schl_apl_trace(LOG_INFO, 0, "Retry dtag = 0x%x", dtag.dtag);
                if(dtag.dtag == INV_U32)
                {
                    schl_apl_trace(LOG_INFO, 0xf069, "Retry dtag[0x%x] ch_id[%d] dtag_cmit_cnt[%d]", dtag.dtag, ch_id, rty_que_dtag_cmit_cnt[ch_id]);
                    sys_assert(0);
                }

                current_retry_entry[ch_id].bm_pl_list[i].pl.dtag = dtag.dtag;

                current_retry_entry[ch_id].bm_pl_list[i].pl.type_ctrl &= ~META_SRAM_IDX;
                current_retry_entry[ch_id].bm_pl_list[i].pl.type_ctrl |= META_DDR_DTAG;
            }
        }

        read_retry_handling(&current_retry_entry[ch_id]);
    }
    else
    {
        // TODO: raid recovery entry point
        panic("todo");
    }
}

ddr_code void hst_rd_err_ins(struct ncl_cmd_t *ncl_cmd, u8 Idx)
{
    bool ret;
    u32 NextRetryPage;
    u8 rced, i, cnt, ch_id;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

    cnt = ncl_cmd->addr_param.common_param.list_len;
    i = Idx;
    retry_entry.bm_pl = ncl_cmd->user_tag_list[Idx];
    retry_entry.bm_pl_list[retry_entry.need_retry_du_cnt] = ncl_cmd->user_tag_list[Idx];
    retry_entry.err_du_offset[retry_entry.need_retry_du_cnt] = Idx;
    retry_entry.pda = ncl_cmd->addr_param.common_param.pda_list[Idx];

    ch_id = (pda2ch(retry_entry.pda) % 4);

    if (ncl_cmd->op_type & DYNAMIC_DTAG)
    {
        retry_entry.bm_pl.pl.dtag = 0;
        retry_entry.bm_pl_list[retry_entry.need_retry_du_cnt].pl.dtag = 0;
    }

    retry_entry.retry_pda_list[retry_entry.need_retry_du_cnt] = ncl_cmd->addr_param.common_param.pda_list[Idx];
    retry_entry.pb_type = ncl_cmd->addr_param.common_param.info_list[Idx].pb_type;

    if (retry_entry.need_retry_du_cnt == 0)
    {
        retry_entry.du_sts = ncl_cmd->addr_param.common_param.info_list[Idx].status;
    }

    //schl_apl_trace(LOG_ALW, 0, "idx: %d,  pda : 0x%x", Idx, ncl_cmd->addr_param.common_param.pda_list[Idx]);

    retry_entry.op_type = ncl_cmd->op_type;
    //retry_entry.du_offset = Idx;
    retry_entry.ncl_cmd = ncl_cmd;
    rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
    retry_entry.need_retry_du_cnt ++;
    total_retry_du_cnt ++;
    hdled_err_du_cnt++;

    do
    {
        i++;
        NextRetryPage = (ncl_cmd->addr_param.common_param.pda_list[i]) >> 2;
    }while((i < cnt) && (info[i].status < ficu_err_du_ovrlmt));

    if (((retry_entry.pda >> 2) != NextRetryPage) || (Idx == ncl_cmd->addr_param.common_param.list_len - 1) ||
        (hdled_err_du_cnt == ncl_cmd->err_du_cnt))
    {
        retry_entry.complete_retry_du_cnt = 0;

	    if (rced == true)
        {
            if(!plp_trigger){
                schl_apl_trace(LOG_ALW, 0x2b4f, "ins_in_hp_read_err_que, total_retry_du_cnt: %d, ncl_cmd: 0x%x, pda: 0x%x",  total_retry_du_cnt, ncl_cmd, retry_entry.retry_pda_list[0]);
            }
            CBF_INS(&hp_read_err_que[ch_id], ret, retry_entry);
        }
        else
        {
            if(!plp_trigger){
                schl_apl_trace(LOG_ALW, 0x7b92, "ins_in_pdu_err_que, total_retry_du_cnt: %d, ncl_cmd: 0x%x, pda: 0x%x, btag: %d",  total_retry_du_cnt, ncl_cmd, retry_entry.retry_pda_list[0], retry_entry.bm_pl.pl.btag);
            }
            CBF_INS(&pdu_err_que_ex[ch_id], ret, retry_entry);
        }

        sys_assert(ret == true);	// todo: overflow control
        memset(&retry_entry, 0x00, sizeof(retry_entry));
    }
    else
    {
        return;
    }


    if (read_error_state_ex[ch_id] == RD_ERR_IDLE)
    {
        hst_rd_err_recover(ch_id);
    }
}

ddr_code void hst_rd_err_cmpl(u8 ch_id)
{
    if(hp_retry_flag[ch_id])
        CBF_REMOVE_HEAD(&hp_read_err_que[ch_id]);
    else
        CBF_REMOVE_HEAD(&pdu_err_que_ex[ch_id]);

    //schl_apl_trace(LOG_ERR, 0, "[DBG] cmpl ch_id[%d] total_retry_du_cnt[%d]", ch_id, total_retry_du_cnt);

    if(pdu_err_ncl_cmd_ex[ch_id].ard_rd_cnt == 1)  //check if this ncl_cmd is doing FW ARD.
    {
        pdu_err_ncl_cmd_ex[ch_id].ard_rd_cnt = 0;  //Indicate that this ncl_cmd have done FW ARD.
        
        u32 fwArdParams = (0x02 << 8);  //op == 2, end FW ARD CS section
        //schl_apl_trace(LOG_ALW, 0, "fw_ard_handler, CPU%d call CPU3 to end CS section, pda2ch = %d", CPU_ID, pda2ch(pdu_err_ncl_cmd_ex[ch_id].addr_param.common_param.pda_list[0]));  //dbg log
        cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FW_ARD_HANDLER, 0, fwArdParams);  //call CPU3 to end FW ARD CS section, and handle FW ARD queue.
    }
    
    read_error_state_ex[ch_id] = RD_ERR_IDLE;
	hst_rd_err_recover(ch_id);
}

#else
slow_code void hst_rd_err_recover(void)
{
	sys_assert(read_error_state == RD_ERR_IDLE);

	if ((!CBF_EMPTY(&pdu_err_que)) || (!CBF_EMPTY(&hp_read_err_que))) {
		rd_err_ent_t ent;
        dtag_t dtag;

        if ((!CBF_EMPTY(&hp_read_err_que)) && (complete_handler_du_cnt == 0))
        {
            hp_retry_en = true;
            CBF_HEAD(&hp_read_err_que, ent);
        }
        else
        {
            hp_retry_en = false;
            CBF_HEAD(&pdu_err_que, ent);
        }

		read_error_state = RD_RECOVERY_START;

#ifdef NCL_HAVE_reARD
		if(ent.op_type == NCL_CMD_FW_DATA_READ_STREAMING)
		{
    		dtag.dtag = DDR_RD_RECOVERY;
        }
		else
		{
    	    dtag.dtag = ent.bm_pl.pl.dtag;
        }
		dtag.b.in_ddr = 1;
#else
        if (ent.op_type & DYNAMIC_DTAG)
        {
            dtag.dtag = DDR_RD_RECOVERY;
            dtag.b.in_ddr = 1;
        }
        else
        {
    	    dtag.dtag = ent.bm_pl.pl.dtag;
        }
#endif

        //tony 20200926
		//if (ent.du_sts == ficu_err_par_err)
        #if NCL_FW_RETRY
        if (ent.du_sts > ficu_err_du_ovrlmt)
        {
            if (ent.du_sts == ficu_err_du_erased)
            {
                pdu_err_ncl_cmd.retry_step = last_1bit_step;
            }
            else
            {
                pdu_err_ncl_cmd.retry_step = retry_set_shift_value;
            }

            read_retry_handling(&ent, dtag);
        }
        #else
		if ((ent.du_sts == ficu_err_par_err) || (ent.du_sts == ficu_err_nard))
        {
            //schl_apl_trace(LOG_ERR, 0, "[Tony] doing hst_pdu_err_handling\n");  //tony 20200928
			hst_pdu_err_handling(&ent, dtag);
		}
        #endif
        else
        {
			// TODO: raid recovery entry point
			panic("todo");
		}
	}
    else
    {
        hp_retry_en = false;
    }
}

slow_code void hst_rd_err_ins(struct ncl_cmd_t *ncl_cmd, u8 Idx)
{
    rd_err_ent_t ent;
    bool ret, rced;

    ent.bm_pl = ncl_cmd->user_tag_list[Idx];
    ent.pda = ncl_cmd->addr_param.common_param.pda_list[Idx];
    ent.pb_type = ncl_cmd->addr_param.common_param.info_list[Idx].pb_type;
    ent.du_sts = ncl_cmd->addr_param.common_param.info_list[Idx].status;
    ent.op_type = ncl_cmd->op_type;
    ent.du_offset = Idx;
    ent.ncl_cmd = ncl_cmd;
    rced = (ent.ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;

    if (rced == true)
    {
        CBF_INS(&hp_read_err_que, ret, ent);
    }
    else
    {
        CBF_INS(&pdu_err_que, ret, ent);
    }

    sys_assert(ret == true);	// todo: overflow control
    total_retry_du_cnt++;
    schl_apl_trace(LOG_INFO, 0x4167, "hst_rd_err_ins,  pda : 0x%x, btag = %d, total_retry_du_cnt = %d", ent.pda, ent.bm_pl.pl.btag, total_retry_du_cnt);

    if (read_error_state == RD_ERR_IDLE)
        hst_rd_err_recover();
}

slow_code void hst_rd_err_cmpl(void)
{
	read_error_state = RD_ERR_IDLE;

    if (hp_retry_done == true)
    {
        CBF_REMOVE_HEAD(&hp_read_err_que);
    }
    else
    {
	    CBF_REMOVE_HEAD(&pdu_err_que);
    }

	hst_rd_err_recover();
}
#endif


#if !NCL_FW_RETRY
slow_code void gc_pdu_err_read_done(struct ncl_cmd_t *ncl_cmd)
{
	nal_status_t du_sts = ncl_cmd->addr_param.rapid_du_param.info.status;
#ifdef LJ1_WUNC
	io_meta_t *meta = ptr_inc(shr_ddtag_meta, sizeof(io_meta_t) * MAX_DDR_GC_DTAG_CNT);
#endif
	bm_pl_t bm_pl = ncl_cmd->du_user_tag_list;

	schl_apl_trace(LOG_ERR, 0x108d, "recovery read done %d", du_sts);

    if (du_sts == ficu_err_par_err)
    {
#if defined(ENABLE_L2CACHE)
            l2cache_mem_invalidate(meta, sizeof(io_meta_t));
#endif
            read_error_state = RD_FE_ISSUING;
#ifdef LJ1_WUNC
            read_recoveried_commit(&bm_pl, meta->wunc.WUNC);
#endif
    }
    else
    {
        if(du_sts <= ficu_err_du_ovrlmt)
        {
            read_error_state = RD_FE_ISSUING;
            //read_recoveried_commit(&bm_pl, 0);
        }
        else if(du_sts == ficu_err_du_uc)
        {
            schl_apl_trace(LOG_INFO, 0x6f71, "[PDU] pdu check to do rc or not");
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
            {
            	rc_req_t* rc_req = rc_req_prepare(ncl_cmd, false);
            	raid_correct_push(rc_req);
                read_error_state = RD_FE_ISSUING;
                //read_recoveried_commit(&bm_pl, 0);
            }
        }

		//nvm_cmd_id used for gc pda group in gc read
		u32 grp = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
		u32 cnt = ncl_cmd->addr_param.common_param.list_len;
		scheduler_gc_read_done(grp, ncl_cmd->user_tag_list, cnt);
        hst_rd_err_cmpl();
    }
}

slow_code void hst_pdu_err_read_done(struct ncl_cmd_t *ncl_cmd)
{

#if 1
    union ncl_addr_param_t pdu_ncl_cmd_param = ncl_cmd->addr_param;

    memset(&ncl_cmd->addr_param, 0, sizeof(union ncl_addr_param_t));
	//ncl_cmd->addr_param.rapid_du_param.info = 0;
	//ncl_cmd->addr_param.rapid_du_param.pda = 0;
	//ncl_cmd->addr_param.rapid_du_param.list_len = 0;

    ncl_cmd->addr_param.common_param.info_list = &pdu_ncl_cmd_param.rapid_du_param.info;
    ncl_cmd->addr_param.common_param.pda_list = &pdu_ncl_cmd_param.rapid_du_param.pda;
    ncl_cmd->addr_param.common_param.list_len = pdu_ncl_cmd_param.rapid_du_param.list_len;
#endif

    nal_status_t du_sts = ncl_cmd->addr_param.common_param.info_list->status;  //tony 20201222
	//nal_status_t du_sts = ncl_cmd->addr_param.rapid_du_param.info.status;
#ifdef LJ1_WUNC
	io_meta_t *meta = ptr_inc(shr_ddtag_meta, sizeof(io_meta_t) * DDR_RD_RECOVERY);  //tony 20201204
#endif
	bm_pl_t bm_pl = ncl_cmd->du_user_tag_list;

	schl_apl_trace(LOG_ERR, 0x3ab4, "recovery read done %d", du_sts);

    if(du_sts <= ficu_err_du_ovrlmt)
    {
        read_error_state = RD_FE_ISSUING;
        read_recoveried_commit(&bm_pl, 0);
		//hst_rd_err_cmpl();
    }
	else if (du_sts == ficu_err_par_err)
    {
#if defined(ENABLE_L2CACHE)
		l2cache_mem_invalidate(meta, sizeof(io_meta_t));
#endif
		read_error_state = RD_FE_ISSUING;
#ifdef LJ1_WUNC
		read_recoveried_commit(&bm_pl, meta->wunc.WUNC);
#endif
	}
    else if(du_sts == ficu_err_du_uc)
    {
        schl_apl_trace(LOG_ERR, 0xc8ed, "[PDU] pdu check to do rc or not\n");  //tony 20201022
        u32 nsid = INT_NS_ID;
        bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
        if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
        {
        	rc_req_t* rc_req = rc_req_prepare(ncl_cmd, false);
        	raid_correct_push(rc_req);
            read_error_state = RD_FE_ISSUING;
            //read_recoveried_commit(&bm_pl, 0);
        }
        else
        {
            bm_err_commit(bm_pl.pl.du_ofst, bm_pl.pl.btag);
	    	hst_rd_err_cmpl();
        }
    }
    else if (du_sts > ficu_err_par_err)
    {
		bm_err_commit(bm_pl.pl.du_ofst, bm_pl.pl.btag);
		hst_rd_err_cmpl();
	}
}

slow_code void hst_pdu_err_handling(rd_err_ent_t *ent, dtag_t dtag)
{
	ent->bm_pl.pl.type_ctrl = META_DDR_DTAG | BTN_NCB_QID_TYPE_CTRL_DROP;
	ent->bm_pl.pl.dtag = dtag.dtag;

	memset(&pdu_err_ncl_cmd.addr_param.rapid_du_param.info, 0, sizeof(struct info_param_t));
	pdu_err_ncl_cmd.addr_param.rapid_du_param.info.pb_type = ent->pb_type;
	pdu_err_ncl_cmd.addr_param.rapid_du_param.pda = ent->pda;
	pdu_err_ncl_cmd.addr_param.rapid_du_param.list_len = 1;
	pdu_err_ncl_cmd.caller_priv = NULL;
    if(ent->op_type == NCL_CMD_FW_DATA_READ_STREAMING) //tony 20201204
	    pdu_err_ncl_cmd.completion = hst_pdu_err_read_done;
    else
        pdu_err_ncl_cmd.completion = gc_pdu_err_read_done;

    //pdu_err_ncl_cmd.host = ent->ncl_cmd->host;
    //pdu_err_ncl_cmd.gc = ent->ncl_cmd->gc;
    //pdu_err_ncl_cmd.re_ard_flag = true;

    pdu_err_ncl_cmd.du_user_tag_list = ent->bm_pl;
	pdu_err_ncl_cmd.du_format_no = host_du_fmt;
	pdu_err_ncl_cmd.flags = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_RAPID_PATH;
	if (ent->pb_type == NAL_PB_TYPE_SLC)
		pdu_err_ncl_cmd.flags |= NCL_CMD_SLC_PB_TYPE_FLAG;
	else
		pdu_err_ncl_cmd.flags |= NCL_CMD_XLC_PB_TYPE_FLAG;

	pdu_err_ncl_cmd.op_code = NCL_CMD_OP_READ;
	pdu_err_ncl_cmd.op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
	ncl_cmd_rapid_single_du_read(&pdu_err_ncl_cmd);
	schl_apl_trace(LOG_ERR, 0x16b0, "re read %x", ent->pda);
}
#endif

ddr_code void rd_err_handling(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

    #if !NCL_FW_RETRY
    die_cmd_info_t *die_cmd_info = (die_cmd_info_t *) ncl_cmd->caller_priv;
    bm_pl_t *bm_pl = ncl_cmd->user_tag_list;
    #endif

    #if NCL_FW_RETRY
    calculate_read_err_du_cnt(ncl_cmd);
    #endif

    if(!plp_trigger){
        schl_apl_trace(LOG_INFO, 0x8d57, "rd_err_handling, ncl_cmd : 0x%x, op_type : %d", ncl_cmd, ncl_cmd->op_type);
    }
    
    memset(&retry_entry, 0x00, sizeof(retry_entry));
    hdled_err_du_cnt = 0;

    for (i = 0; i < cnt; i++)
    {
    	if (info[i].status == ficu_err_good)
    		continue;
        #if NCL_FW_RETRY
        else if (info[i].status > ficu_err_du_ovrlmt
#ifdef History_read
        || info[i].status == ficu_err_du_ovrlmt
#endif
        )
    	{
            hst_rd_err_ins(ncl_cmd, i);
        }
        #else
        if (die_cmd_info->cmd_info.b.gc)
        {
            if ((info[i].status == ficu_err_par_err) || (info[i].status == ficu_err_nard)) //|| (info[i].status == ficu_err_dfu))
            {
    			hst_rd_err_ins(ncl_cmd, i);
    		}
            else if(info[i].status > ficu_err_du_ovrlmt)
            {
                bm_pl[i].pl.btag = 0xFFF;
            }
            else
            {
                continue;
            }
        }
        else
        {
    		if ((info[i].status == ficu_err_par_err) || (info[i].status == ficu_err_nard)) //|| (info[i].status == ficu_err_dfu))
            {
    			hst_rd_err_ins(ncl_cmd, i);
    		}
            else if(info[i].status == ficu_err_du_uc)
            {
    			continue;
            }
            else
            {
                bm_err_commit(bm_pl[i].pl.du_ofst, bm_pl[i].pl.btag);
            }
        }
        #endif
    }
}

#ifdef NCL_FW_RETRY_EX
fast_code void read_recoveried_done(dtag_t dtag)
{
    //u8 ch_id = (dtag.b.dtag - DDR_RTY_RECOVERY_EX_START)/RTY_DTAG_CNT_PER_CH;
    u8 ch_id = 0;
    //ch_id = (dtag.b.dtag - DDR_RTY_RECOVERY_EX_START) / RTY_DTAG_CNT_PER_CH;
    ch_id = (dtag.b.dtag - DDR_RD_RECOVERY_EX_START) / RTY_DTAG_CNT_PER_CH;
    #if(CPU_ID == CPU_BE_LITE)
    sys_assert(ch_id >= RTY_QUEUE_CH_CNT);
    ch_id -= 4;
    #endif

    //schl_apl_trace(LOG_ERR, 0, "dtag[0x%x] ch_id[%d] rty_dtag_start[0x%x]", dtag.b.dtag, ch_id, DDR_RTY_RECOVERY_EX_START);

	sys_assert((dtag.b.dtag >= DDR_RTY_RECOVERY_EX_START) && (dtag.b.dtag < (DDR_RTY_RECOVERY_EX_START + (DDR_RD_RECOVERY_EX_CNT/2))));
	//hst_rd_err_cmpl((dtag.b.dtag - DDR_RTY_RECOVERY_EX_START)/RTY_DTAG_CNT_PER_CH);
    rty_que_dtag_cmit_cnt[ch_id]--;

    if (rty_que_dtag_cmit_cnt[ch_id] == 0)
    {
        hst_rd_err_cmpl(ch_id);
    }
    else
    {
        handle_read_retry_done(&pdu_err_ncl_cmd_ex[ch_id]);
    }
}

norm_ps_code void read_error_resume(void)
{
    u8 i = 0;

	for(i = 0; i < RTY_QUEUE_CH_CNT; i++)
    {
	    CBF_INIT(&pdu_err_que_ex[i]);
        read_error_state_ex[i] = RD_ERR_IDLE;
        rty_dtag_ptr_ex[i] = 0;
        rty_que_dtag_cmit_cnt[i] = 0;
        complete_du_cnt_ex[i] = 0;

    	CBF_INIT(&hp_read_err_que[i]);
        hp_retry_flag[i] = false;
    }

        CBF_INIT(&hp_vth_que);
        CBF_INIT(&pdu_vth_que);
        
    #if (CPU_ID == CPU_BE)
        extern ard_que_t fw_ard_que;
        CBF_INIT(&fw_ard_que);  //Init FW ARD queue
    #endif
    
    memset(pdu_err_ncl_cmd_ex, 0, sizeof(struct ncl_cmd_t)*RTY_QUEUE_CH_CNT);
}
#else
fast_code void read_recoveried_done(dtag_t dtag)
{
	sys_assert(dtag.b.dtag == DDR_RD_RECOVERY);

	hst_rd_err_cmpl();
}

norm_ps_code void read_error_resume(void)
{
	CBF_INIT(&pdu_err_que);
	CBF_INIT(&hp_read_err_que);
    hp_retry_done = false;
	read_error_state = RD_ERR_IDLE;
}
#endif

fast_code void ipc_read_recoveried_done(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = { .dtag = req->pl };

	read_recoveried_done(dtag);
}

ddr_code void ipc_read_retry_handling(volatile cpu_msg_req_t *req)  //CPU_MSG_READ_RETRY_HANDLING
{
	u8 ch_id = (u8)req->pl;
    //schl_apl_trace(LOG_ALW, 0, "fw_ard_handler, ipc_read_retry_handling, ch_id = %d",ch_id);  //dbg log
    ard_read_retry_handling(&current_retry_entry[ch_id]);  //handle by ard_read_retry_handling, to retry each DU.
}

ddr_code void AplReadRetry_AbortJob(u32 p0, u32 p1, u32 p2)
{
	// if (!CBF_EMPTY(&pdu_err_que))
	// {
	// 	CBF_FETCH_LIST(&pdu_err_que, cnt, p, pcnt);
	// 	// PUT NCL
	// 	//ncl_cmd_put(ent.ncl_cmd);
	// }
	// else
	// {
	//
	// }
	//
	// //TODO PLP ABORT
	//schl_apl_trace(LOG_INFO, 0, "READ RETRY NEED ABORT");
}

#if (CO_SUPPORT_READ_AHEAD == TRUE)
fast_code void AplReadRetry_AbortRa(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	bm_pl_t *bm_pl = ncl_cmd->user_tag_list;
	u32 cnt = ncl_cmd->addr_param.common_param.list_len;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

	for (i = 0; i < cnt; i++) {
		if (ficu_du_data_good(info[i].status))
			continue;

		if (bm_pl[i].pl.btag == ra_btag) {
            if(!plp_trigger){
			    schl_apl_trace(LOG_ALW, 0x0060, "ra read err dtag 0x%d ofst:%d status:%d",bm_pl[i].pl.dtag, bm_pl[i].pl.du_ofst, info[i].status);
            }
            ipc_api_ra_err_data_in(bm_pl[i].pl.du_ofst, info[i].status);
			continue;
		}

	}
}
#endif

init_code void read_error_init(void)
{
	read_error_resume();
	cpu_msg_register(CPU_MSG_READ_RECOVERIED_DONE, ipc_read_recoveried_done);
    cpu_msg_register(CPU_MSG_READ_RETRY_HANDLING, ipc_read_retry_handling);

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
    cpu_msg_register(CPU_MSG_GET_ERRHANDLEVAR_CPU_BE, __get_telemetry_ErrHandleVar_CPU_BE);
#endif

	evt_register(AplReadRetry_AbortJob, 0, &evt_errhandle_mon);
    evt_register(rd_err_ncl_cmd_submit_trigger, 0, &evt_rd_err_ncl_cmd_submit);
    memset(&current_shift_value, 0x00, sizeof(current_shift_value));
}

#ifdef RD_FAIL_GET_LDA
ddr_code void rd_err_ptr2next(u16 *ptr, u16 max_cnt)
{
    //u32 *ecct_ptr = ptr;
    if(*ptr >= (max_cnt-1))
        *ptr = 0;
    else
        *ptr = *ptr + 1;
    return;
}

ddr_code void rd_err_get_lda(bm_pl_t *bm_pl, pda_t pda, u8 type)
{
    u16 *idx_ptr = NULL;
    rd_get_lda_t *hst_lda_entry;
    rd_get_lda_t *ua_lda_entry;  
    u32 dtag_idx = 0;
	lda_t lda = 0;
    io_meta_t *meta;
    switch(type)
    {
        case HOST_READ_TYPE:
            #if(CPU_ID == CPU_BE)
            idx_ptr = &rd_err_lda_ptr_0;
            hst_lda_entry = &rd_err_lda_entry_0[*idx_ptr];
            #else
            idx_ptr = &rd_err_lda_ptr_1;
            hst_lda_entry = &rd_err_lda_entry_1[*idx_ptr];
            #endif

            hst_lda_entry->btag = bm_pl->pl.btag;
            hst_lda_entry->du_ofst = bm_pl->pl.du_ofst;
            hst_lda_entry->pda = pda;

            if(CPU_ID == CPU_FE)
                host_rd_get_lda(bm_pl->pl.btag, bm_pl->pl.du_ofst, 0);
            else
                cpu_msg_issue(CPU_FE - 1, CPU_MSG_HOST_GET_LDA, 0, (u32)hst_lda_entry);

            rd_err_ptr2next(idx_ptr, (DTAG_SZE/sizeof(rd_get_lda_t))/2-1);
            break;
        case GC_READ_TYPE:
            dtag_idx = bm_pl->pl.du_ofst;
            dtag_idx = (DTAG_CNT_PER_GRP * bm_pl->pl.nvm_cmd_id) + dtag_idx;
            //dtag_idx = DTAG_GRP_IDX_TO_IDX(dtag_idx,  pl.pl.nvm_cmd_id);
            lda = gc_lda_list[dtag_idx];
            schl_apl_trace(LOG_INFO, 0xc729, "[LDA]GC lda[0x%x] pda[0x%x] type[%d]", lda, pda, type);
            break;
        default:
	        if(bm_pl->pl.btag == ua_btag)
            {    
           		ua_lda_entry = rd_err_lda_entry_ua;
	            ua_lda_entry->btag = bm_pl->pl.btag;
	            ua_lda_entry->du_ofst = bm_pl->pl.du_ofst;
	            ua_lda_entry->pda = pda;
            	cpu_msg_issue(CPU_FE - 1, CPU_MSG_HOST_GET_LDA, 0, (u32)ua_lda_entry);
            	break;
            }
            meta = (bm_pl->pl.dtag & DTAG_IN_DDR_MASK) ? (io_meta_t *)shr_ddtag_meta : (io_meta_t *)shr_dtag_meta;
            dtag_idx = bm_pl->pl.dtag & DDTAG_MASK;
            lda = (u64)meta[dtag_idx].lda;
            schl_apl_trace(LOG_INFO, 0xbc25, "[LDA]Other lda[0x%x] pda[0x%x] type[%d]", lda, pda, type);
            break;
    }
    return;
}
#endif
/*! @} */
