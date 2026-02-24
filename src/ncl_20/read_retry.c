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
/*! \file ncl.c
 * @brief NCL interface for misc operation
 *
 * \addtogroup ncl_20
 * \defgroup ncl
 * \ingroup ncl_20
 *
 * @{
 */
#include "types.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "read_retry.h"
#include "die_que.h" 
#include "scheduler.h"
#include "nand_tsb.h"
#include "ipc_api.h"
#include "ftl_core.h"
#include "ddr_top_register.h"
#include "GList.h"
#include "fc_export.h"
#include "eccu.h"
/*! \cond PRIVATE */
#define __FILEID__ rdrt
#include "trace.h"
/*! \endcond */


extern bool hp_retry_en;
extern bool hp_retry_done;
extern read_error_state_t read_error_state;
extern void *shr_ddtag_meta;
extern void *shr_dtag_meta;
extern enum du_fmt_t host_du_fmt;
extern u16 ua_btag;
slow_data_zi volatile u16 total_retry_du_cnt = 0;
ddr_data u8 need_retry_du_cnt[128] = {0};
ddr_data_ni u32 current_shift_value[128][2];

slow_data_zi volatile u8 complete_handler_du_cnt = 0;
slow_data_zi struct info_param_t retry_info_list[RTY_QUEUE_CH_CNT][DU_CNT_PER_PAGE];
slow_data_zi pda_t retry_pda_list[RTY_QUEUE_CH_CNT][DU_CNT_PER_PAGE];

#ifdef NCL_FW_RETRY_EX
extern rd_err_que_t pdu_err_que_ex[RTY_QUEUE_CH_CNT]; //4CH * 2CPU
extern u8 rty_dtag_ptr_ex[RTY_QUEUE_CH_CNT];
extern read_error_state_t read_error_state_ex[RTY_QUEUE_CH_CNT];
extern struct ncl_cmd_t pdu_err_ncl_cmd_ex[RTY_QUEUE_CH_CNT];
extern u8 complete_du_cnt_ex[RTY_QUEUE_CH_CNT];
fast_data_zi u32 under_retry_pda_ex[RTY_QUEUE_CH_CNT] = {0};
extern u8 rty_que_dtag_cmit_cnt[RTY_QUEUE_CH_CNT]; //by CH

extern rd_err_que_t hp_read_err_que[RTY_QUEUE_CH_CNT];
extern volatile bool hp_retry_flag[RTY_QUEUE_CH_CNT];

#else
extern rd_err_que_t hp_read_err_que;
fast_data_zi u32 under_retry_pda = 0;
fast_data_zi volatile u8 complete_handler_du_cnt = 0;
extern rd_err_que_t pdu_err_que;
extern struct ncl_cmd_t pdu_err_ncl_cmd;
#endif

extern bool dref_comt_poll(void);
 
#if NCL_FW_RETRY
#ifdef RETRY_COMMIT_EVENT_TRIGGER
#ifdef NCL_FW_RETRY_EX
extern recover_commit_t recommit_cpu2[RTY_QUEUE_CH_CNT];
extern recover_commit_t recommit_cpu4[RTY_QUEUE_CH_CNT];
#else
extern recover_commit_t recommit_cpu2;
extern recover_commit_t recommit_cpu4;
#endif
#endif

#ifdef NCL_RETRY_PASS_REWRITE
fast_data u8 evt_retry_rewrite_pend_out = 0xFF;
extern u32 rd_rewrite_ptr;
extern retry_rewrite_t rd_rew_info[DDR_RD_REWRITE_TTL_CNT];
extern u8 rd_rew_level;
#endif

#ifdef RD_FAIL_GET_LDA
share_data_zi bool FTL_INIT_DONE;
#endif

//For vth_tracking
#include "eccu_reg_access.h"

#define vth_tick 16         //vth tracking tick interval
#define vth_rounding 5      //rounding
#define vth_ratio 10        //used for rounding
#define vth_print 0         //print log

void close_tracking(struct ncl_cmd_t *ncl_cmd);
void reset_feature(pda_t *pda, void (*completion)(struct ncl_cmd_t *));
void set_feature_by_tracking(pda_t *pda, u8 D0);
void pda_ssr_read_by_tracking(struct ncl_cmd_t *ncl_cmd);
void ncl_vth_tracking(pda_t *pda);
void vth_queue_handler();

ddr_data_ni struct ncl_cmd_t ncl_cmd_sf;
ddr_data_ni struct ncl_cmd_t ncl_cmd_ssr;

ddr_data_ni u16 histogram[3];
ddr_data_ni u8 vth_value[3];
ddr_data_ni u16 bit1cnt[4];

ddr_data_ni bm_pl_t bm_pl_ssr;
ddr_data_ni struct info_param_t Vth_sf_info;

ddr_data u8 vth_cs_flag=0; //vth_tracking critical section flag

ddr_data_ni vth_que_t hp_vth_que;  //raid_vth_queue
ddr_data_ni vth_que_t pdu_vth_que; //vth_queue
//end For vth_tracking

//For patrol/history read
#ifdef History_read
#include "ftl.h"
extern hist_tab_offset_t *hist_tab_ptr_st;
share_data volatile u32 shr_avg_erase;
#endif
//end for patrol/history read

//slow_code void gc_dfu_err_chk(struct ncl_cmd_t *ncl_cmd)
ddr_code void dfu_blank_err_chk(struct ncl_cmd_t *ncl_cmd, die_ent_info_t ent_info)
{
	u32 i;
	u32 pda_cnt = ncl_cmd->addr_param.common_param.list_len;
    //lda_t lda = 0;
    //u32 dtag_idx = 0;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;               
    bm_pl_t *bm_pl_list = ncl_cmd->user_tag_list;
    u8 type;

    if(ent_info.b.stream)
    {
        type = HOST_ECCT_TYPE;
    }
    else if(ent_info.b.gc)
    {
        type = GC_ECCT_TYPE;
    }
    else
    {
        type = OTHER_ECCT_TYPE;
    }
    
	for (i = 0; i < pda_cnt; i++) 
    {
		//if((info[i].status == ficu_err_du_uc) || (info[i].status == ficu_err_du_uc) || (info[i].status == ficu_err_du_erased) || (info[i].status == ficu_err_1bit_retry_err) || (info[i].status == ficu_err_good)) //test
        #if DFU_MARK_ECC
        if(info[i].status == ficu_err_dfu)
        {
            if(type == GC_ECCT_TYPE)
                bm_pl_list[i].pl.btag = 0xFFF;  //gc bed blk mark

            #if 1
            Reg_ECCT_Fill_Info(&bm_pl_list[i], ncl_cmd->addr_param.common_param.pda_list[i], type, ECC_REG_DFU); //reg ecct
            #else
            dtag_idx = bm_pl_list[i].pl.du_ofst;
            dtag_idx = (DTAG_CNT_PER_GRP * bm_pl_list[i].pl.nvm_cmd_id) + dtag_idx;
            //dtag_idx = DTAG_GRP_IDX_TO_IDX(dtag_idx,  pl.pl.nvm_cmd_id);
            lda = gc_lda_list[dtag_idx];

            if (CPU_ID != CPU_BE)  //reg ecct
                cpu_msg_issue(CPU_BE - 1, CPU_MSG_ECCT_MARK, 0, lda);                                       
            else
                Register_ECCT_By_Raid_Recover(lda, ~(0x00), ECC_REG_DFU);                             
            #endif
        }
        #endif
        if(info[i].status == ficu_err_du_erased)
        {
            if(type == GC_ECCT_TYPE)
                bm_pl_list[i].pl.btag = 0xFFF;  //gc bed blk mark

            Reg_ECCT_Fill_Info(&bm_pl_list[i], ncl_cmd->addr_param.common_param.pda_list[i], type, ECC_REG_BLANK); //reg ecct
        }
    }
    return;
}
#ifdef NCL_FW_RETRY_EX
slow_code void abort_retry_flow(u8 ch_id)
{
    //If fe_issuing for dynamic op ,ent->complete_retry_du_cnt will not be zero
	die_cmd_info_t *die_cmd_info;
    u8 i;

    rd_err_que_t *curr_queue = NULL;
    
    if(hp_retry_flag[ch_id] == true)
        curr_queue = &hp_read_err_que[ch_id];     //If hp_retry_flag[ch_id] == true, do RCED queue fisrt.
    else
        curr_queue = &pdu_err_que_ex[ch_id];     //If hp_retry_flag[ch_id] == false, do normal queue first, prevent current_retry_entry not done.

    //Each ncl_cmd needs to be completed untill the (complete_du_cnt_ex[ch_id] == current_retry_entry[ch_id].ncl_cmd->err_du_cnt) is met.

    while(!CBF_EMPTY(curr_queue))
    {        
        total_retry_du_cnt -= (current_retry_entry[ch_id].need_retry_du_cnt - current_retry_entry[ch_id].complete_retry_du_cnt);
        complete_du_cnt_ex[ch_id] += (current_retry_entry[ch_id].need_retry_du_cnt - current_retry_entry[ch_id].complete_retry_du_cnt);            
        
        if (complete_du_cnt_ex[ch_id] == current_retry_entry[ch_id].ncl_cmd->err_du_cnt)
        {
            complete_du_cnt_ex[ch_id] = 0;
            current_retry_entry[ch_id].ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
            current_retry_entry[ch_id].ncl_cmd->retry_step = retry_end;
            current_retry_entry[ch_id].ncl_cmd->flags |= NCL_CMD_RCED_FLAG;
            #if defined(HMETA_SIZE)
            if (current_retry_entry[ch_id].op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || current_retry_entry[ch_id].ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
            #else
            if (current_retry_entry[ch_id].op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
            #endif
            {
                current_retry_entry[ch_id].ncl_cmd->completion(current_retry_entry[ch_id].ncl_cmd);
            }
            else
            {   
				bool flag_put_ncl_cmd = true;
                die_cmd_info = (die_cmd_info_t *) current_retry_entry[ch_id].ncl_cmd->caller_priv;
                
                if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug)) {
                	//nvm_cmd_id used for gc pda group in gc read
                	u32 grp = current_retry_entry[ch_id].ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                	u32 cnt = current_retry_entry[ch_id].ncl_cmd->addr_param.common_param.list_len;

                    for(i = 0; i < current_retry_entry[ch_id].need_retry_du_cnt; i++)
                    {
                        current_retry_entry[ch_id].ncl_cmd->user_tag_list[i].pl.btag = 0xFFF;
                    }
                    
                	scheduler_gc_read_done(grp, current_retry_entry[ch_id].ncl_cmd->user_tag_list, cnt);
                }
                else if(current_retry_entry[ch_id].bm_pl.pl.btag == ua_btag && FTL_INIT_DONE)
                {
                    for(i = 0; i < current_retry_entry[ch_id].need_retry_du_cnt; i++)
                    {
                        ipc_api_ucache_read_error_data_in(current_retry_entry[ch_id].bm_pl_list[i].pl.du_ofst, current_retry_entry[ch_id].du_sts);
                    }
                }
                
            	ncl_cmd_put(current_retry_entry[ch_id].ncl_cmd, flag_put_ncl_cmd);
            }               
        }
        
        CBF_REMOVE_HEAD(curr_queue);

        if(!CBF_EMPTY(curr_queue))
            CBF_HEAD(curr_queue, current_retry_entry[ch_id]);
    }

    //Second, do the another queue.
    
    if(hp_retry_flag[ch_id] == true)
        curr_queue = &pdu_err_que_ex[ch_id];
    else
        curr_queue = &hp_read_err_que[ch_id];

    while(!CBF_EMPTY(curr_queue))
    {
        CBF_HEAD(curr_queue, current_retry_entry[ch_id]);
        
        total_retry_du_cnt -= current_retry_entry[ch_id].need_retry_du_cnt;
        complete_du_cnt_ex[ch_id] += current_retry_entry[ch_id].need_retry_du_cnt;            
        
        if (complete_du_cnt_ex[ch_id] == current_retry_entry[ch_id].ncl_cmd->err_du_cnt)
        {
            complete_du_cnt_ex[ch_id] = 0;
            current_retry_entry[ch_id].ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
            current_retry_entry[ch_id].ncl_cmd->retry_step = retry_end;
            current_retry_entry[ch_id].ncl_cmd->flags |= NCL_CMD_RCED_FLAG;
            #if defined(HMETA_SIZE)
            if (current_retry_entry[ch_id].op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || current_retry_entry[ch_id].ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
            #else
            if (current_retry_entry[ch_id].op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
            #endif
            {
                current_retry_entry[ch_id].ncl_cmd->completion(current_retry_entry[ch_id].ncl_cmd);
            }
            else
            {   
				bool flag_put_ncl_cmd = true;
                die_cmd_info = (die_cmd_info_t *) current_retry_entry[ch_id].ncl_cmd->caller_priv;
                
                if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug)) {
                	//nvm_cmd_id used for gc pda group in gc read
                	u32 grp = current_retry_entry[ch_id].ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                	u32 cnt = current_retry_entry[ch_id].ncl_cmd->addr_param.common_param.list_len;

                    for(i = 0; i < current_retry_entry[ch_id].need_retry_du_cnt; i++)
                    {
                        current_retry_entry[ch_id].ncl_cmd->user_tag_list[i].pl.btag = 0xFFF;
                    }
                    
                	scheduler_gc_read_done(grp, current_retry_entry[ch_id].ncl_cmd->user_tag_list, cnt);
                }
                else if(current_retry_entry[ch_id].bm_pl.pl.btag == ua_btag && FTL_INIT_DONE)
                {
                    for(i = 0; i < current_retry_entry[ch_id].need_retry_du_cnt; i++)
                    {
                        ipc_api_ucache_read_error_data_in(current_retry_entry[ch_id].bm_pl_list[i].pl.du_ofst, current_retry_entry[ch_id].du_sts);
                    }
                }
                
            	ncl_cmd_put(current_retry_entry[ch_id].ncl_cmd, flag_put_ncl_cmd);
            }               
        }
        
        CBF_REMOVE_HEAD(curr_queue);
    }

    //Reset error handle settings
    rty_que_dtag_cmit_cnt[ch_id] = 0;
    hp_retry_flag[ch_id] = false;
    read_error_state_ex[ch_id] = RD_ERR_IDLE;
}

ddr_code void ard_read_retry_handling(rd_err_ent_t *ent)  //FW ARD can only read retry one DU at a time. list_len = 1.
{
	extern volatile u8 plp_trigger;
	
    u8 ch_id = (pda2ch(ent->pda) % 4);
    struct ncl_cmd_t *pdu_err_ncl_cmd;
    pdu_err_ncl_cmd = (struct ncl_cmd_t *)&pdu_err_ncl_cmd_ex[ch_id];
	
	if(plp_trigger)
    {
        handle_read_retry_done(pdu_err_ncl_cmd);
        return;
    }
    
    u8 du_idx = (pdu_err_ncl_cmd->ard_rd_cnt - 1);

    dtag_t dtag;
    dtag.dtag = ent->bm_pl_list[0].pl.dtag;

    memset(&retry_info_list[ch_id][du_idx], 0, sizeof(retry_info_list[0][0]));
    
    pdu_err_ncl_cmd->status = 0;
    pdu_err_ncl_cmd->addr_param.common_param.list_len = 1;
	pdu_err_ncl_cmd->addr_param.common_param.info_list = &retry_info_list[ch_id][du_idx];
	pdu_err_ncl_cmd->addr_param.common_param.pda_list = &retry_pda_list[ch_id][du_idx];
	pdu_err_ncl_cmd->user_tag_list = &retry_bm_pl_list[ch_id][du_idx];
    retry_pda_list[ch_id][du_idx] = ent->retry_pda_list[du_idx];


    #if RAID_SUPPORT_UECC
    pdu_err_ncl_cmd->caller_priv = ent->ncl_cmd->caller_priv;
	pdu_err_ncl_cmd->uecc_type = ent->ncl_cmd->uecc_type;
    #else
    pdu_err_ncl_cmd->caller_priv = NULL;
    #endif
    
    pdu_err_ncl_cmd->completion = ard_handle_read_retry_done;
	pdu_err_ncl_cmd->du_format_no = ent->ncl_cmd->du_format_no;
	pdu_err_ncl_cmd->flags = ent->ncl_cmd->flags;
    pdu_err_ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
    #ifdef EH_ENABLE_2BIT_RETRY
    if(ent->ncl_cmd->flags & NCL_CMD_RCED_FLAG)
        pdu_err_ncl_cmd->flags |= NCL_CMD_RCED_FLAG;
    #endif
    pdu_err_ncl_cmd->rty_blank_flg = false;
    pdu_err_ncl_cmd->rty_dfu_flg = false;
         
    ent->bm_pl_list[du_idx].pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
    
    if ((ent->du_sts == ficu_err_par_err) && ((dtag.b.in_ddr) & 0x01))
    {
        ent->bm_pl_list[du_idx].pl.type_ctrl |= META_DDR_DTAG;
        ent->bm_pl_list[du_idx].pl.type_ctrl &= ~META_SRAM_IDX;
    }

    retry_info_list[ch_id][du_idx].pb_type = ent->pb_type;
    retry_bm_pl_list[ch_id][du_idx].all = ent->bm_pl_list[du_idx].all;

    pdu_err_ncl_cmd->retry_step = last_2bit_read;
    pdu_err_ncl_cmd->op_code = NCL_CMD_OP_READ_FW_ARD;

    if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
    {
        pdu_err_ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    }
    else
    {
        pdu_err_ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
    }

    #ifdef NCL_FW_RETRY_BY_SUBMIT
    extern u8 evt_rd_err_ncl_cmd_submit;
    extern bool ncl_cmd_in;

    if(ncl_cmd_in)
    {
        read_error_state_ex[ch_id] = RD_SET_CMD_BY_EVT_TRIG;
        evt_set_cs(evt_rd_err_ncl_cmd_submit, 0, 0, CS_TASK); //ncl_cmd_submit by event handler.
    }
    else
    {
        ncl_cmd_submit(pdu_err_ncl_cmd);
    }
    
    #else
    if(pdu_err_ncl_cmd->retry_step != last_2bit_read){
        ncl_cmd_read_retry(pdu_err_ncl_cmd);
    }
    else{
        ncl_cmd_dus_read_ard(pdu_err_ncl_cmd);
    }
    #endif

}

ddr_code void read_retry_handling(rd_err_ent_t *ent)
{    
    u8 ch_id;
    struct ncl_cmd_t *pdu_err_ncl_cmd;
    dtag_t dtag;
    u8 i;
    
    dtag.dtag = ent->bm_pl_list[0].pl.dtag;
    
    ch_id = (pda2ch(ent->pda) % 4);
    
	memset(&retry_info_list[ch_id][0], 0, sizeof(retry_info_list[ch_id]));

    pdu_err_ncl_cmd = (struct ncl_cmd_t *)&pdu_err_ncl_cmd_ex[ch_id];
    pdu_err_ncl_cmd->status = 0;
	pdu_err_ncl_cmd->addr_param.common_param.info_list = &retry_info_list[ch_id][0];
	pdu_err_ncl_cmd->addr_param.common_param.list_len = ent->need_retry_du_cnt;
	pdu_err_ncl_cmd->addr_param.common_param.pda_list = &retry_pda_list[ch_id][0];
	pdu_err_ncl_cmd->user_tag_list = &retry_bm_pl_list[ch_id][0];    

    memcpy((void *) &retry_pda_list[ch_id][0], ent->retry_pda_list, sizeof(ent->retry_pda_list));

    #if RAID_SUPPORT_UECC
    pdu_err_ncl_cmd->caller_priv = ent->ncl_cmd->caller_priv;
	pdu_err_ncl_cmd->uecc_type = ent->ncl_cmd->uecc_type;
    #else
    pdu_err_ncl_cmd->caller_priv = NULL;
    #endif
    pdu_err_ncl_cmd->completion = handle_read_retry_done;
	pdu_err_ncl_cmd->du_format_no = ent->ncl_cmd->du_format_no;
	pdu_err_ncl_cmd->flags = ent->ncl_cmd->flags;
    pdu_err_ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
    #ifdef EH_ENABLE_2BIT_RETRY
    if(ent->ncl_cmd->flags & NCL_CMD_RCED_FLAG)
        pdu_err_ncl_cmd->flags |= NCL_CMD_RCED_FLAG;
    #endif
    pdu_err_ncl_cmd->rty_blank_flg = false;
    pdu_err_ncl_cmd->rty_dfu_flg = false;
    
    if ((pdu_err_ncl_cmd->retry_step == retry_set_shift_value) || (pdu_err_ncl_cmd->retry_step == last_1bit_step) || 
        (pdu_err_ncl_cmd->retry_step == last_1bit_vth_step) || (pdu_err_ncl_cmd->retry_step == abort_retry_step)
#ifdef History_read
        || (pdu_err_ncl_cmd->retry_step == retry_history_step)
#endif
#if SPOR_RETRY
        || (pdu_err_ncl_cmd->retry_step == spor_retry_step)
#endif
    )
    {
        #ifdef NCL_FW_RETRY_BY_SUBMIT
	    pdu_err_ncl_cmd->op_code = NCL_CMD_FW_RETRY_SET_FEATURE;
        #else
        pdu_err_ncl_cmd->op_code = NCL_CMD_SET_GET_FEATURE;
        #endif
    }
    else
    {
        
        for(i = 0; i < ent->need_retry_du_cnt; i++)
        {
            ent->bm_pl_list[i].pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
            if ((ent->du_sts == ficu_err_par_err) && ((dtag.b.in_ddr) & 0x01))
            {
                ent->bm_pl_list[i].pl.type_ctrl |= META_DDR_DTAG;
                ent->bm_pl_list[i].pl.type_ctrl &= ~META_SRAM_IDX;
            }

            retry_info_list[ch_id][i].pb_type = ent->pb_type;
            //ncl_cmd_trace(LOG_ALW, 0, "dtag: 0x%x", ent->bm_pl_list[i].pl.dtag);
        }

        memcpy((void *) &retry_bm_pl_list[ch_id][0], ent->bm_pl_list, sizeof(ent->bm_pl_list));

	    pdu_err_ncl_cmd->op_code = NCL_CMD_OP_READ;

        if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
        {
            pdu_err_ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
        }
        else
        {
            pdu_err_ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
        }
    }

    #ifdef NCL_FW_RETRY_BY_SUBMIT
    extern u8 evt_rd_err_ncl_cmd_submit;
    extern bool ncl_cmd_in;

    if(ncl_cmd_in)
    {
        read_error_state_ex[ch_id] = RD_SET_CMD_BY_EVT_TRIG;
        evt_set_cs(evt_rd_err_ncl_cmd_submit, 0, 0, CS_TASK); //ncl_cmd_submit by event handler.
    }
    else
    {
        ncl_cmd_submit(pdu_err_ncl_cmd);
    }
    
    #else
    if(pdu_err_ncl_cmd->retry_step != last_2bit_read){
        ncl_cmd_read_retry(pdu_err_ncl_cmd);
    }
    else{
        ncl_cmd_dus_read_ard(pdu_err_ncl_cmd);
    }
    #endif

}

ddr_code void shift_to_next_retry_step(u8 du_sts, rd_err_ent_t *ent)
{
    bool His_read = (ent->ncl_cmd->op_code == NCL_CMD_PATROL_READ) ? true : false;
    u8 ch_id;
    struct ncl_cmd_t *pdu_err_ncl_cmd;

    ch_id = (pda2ch(ent->pda) % 4);
    pdu_err_ncl_cmd = (struct ncl_cmd_t *)&pdu_err_ncl_cmd_ex[ch_id];

    u8 max_retry_cnt;
    bool rced = (ent->ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
    #if SPOR_RETRY
    bool spor = (pdu_err_ncl_cmd->retry_step     == spor_retry_read) ? true : false;
    #endif
    
	//dtag.b.in_ddr = 1;

    //if (hp_retry_en)
    //{
	//    CBF_HEAD(&hp_read_err_que, ent);
    //}
    //else
    //{
	//    CBF_HEAD(&pdu_err_que, ent);
    //}
    
    pdu_err_ncl_cmd->retry_cnt++;

    if (du_sts == ficu_err_du_erased)
    {
        max_retry_cnt = 1;
    }
    else
    {
        max_retry_cnt = (pdu_err_ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) ? RR_STEP_SLC : RR_STEP_XLC;
    }

    #if SPOR_RETRY
    if(spor)
    {
        pdu_err_ncl_cmd->retry_step = spor_retry_step;
        read_retry_handling(ent);
        return;
    }
    #endif
    
    if(!rced) //non-raid retry flow
    {
        if(pdu_err_ncl_cmd->retry_cnt > max_retry_cnt)
        {
            pdu_err_ncl_cmd->retry_step = last_2bit_step;  // Go to do FW ARD
        }
        else if(pdu_err_ncl_cmd->retry_cnt == max_retry_cnt)
        {
            pdu_err_ncl_cmd->retry_step = last_1bit_vth_step;
        }
        else if(pdu_err_ncl_cmd->retry_cnt == (max_retry_cnt-1))
        {
            pdu_err_ncl_cmd->retry_step = last_1bit_step;
        }
        else
        {
        #ifdef History_read
            if(pdu_err_ncl_cmd->retry_step == retry_history_read)
            {
                pdu_err_ncl_cmd->retry_cnt = 0;
                //ncl_cmd_trace(LOG_ALW, 0, "shift to retry_set_shift_value");
            }        
            else if(His_read && pdu_err_ncl_cmd->retry_cnt == 2)
            {
                //PE cycle:(A) 0~3K,"retry_cnt" jump to 1 (B)3~7K,"retry_cnt" jump to 7; (C)3~7K,"retry_cnt" jump to 12; 
                //ncl_cmd_trace(LOG_ALW, 0, "shr_avg_erase:%d",shr_avg_erase);
                if(3000 <= shr_avg_erase && shr_avg_erase< 7000)//3K<=PE<7K
                {
                    pdu_err_ncl_cmd->retry_cnt = 7;
                }
                else if(7000 <= shr_avg_erase && shr_avg_erase< 10000)//7K<=PE<=10K
                {
                    pdu_err_ncl_cmd->retry_cnt = 12;
                }
            }
        #endif    
            pdu_err_ncl_cmd->retry_step = retry_set_shift_value;
        }
    }
    else //Raid retry flow
    {
        if (pdu_err_ncl_cmd->retry_step == retry_read || pdu_err_ncl_cmd->retry_step == retry_history_read)
        {
            pdu_err_ncl_cmd->retry_step = last_1bit_vth_step; //vth + HB read.
        }
        else if (pdu_err_ncl_cmd->retry_step == last_1bit_vth_read)
        {
            pdu_err_ncl_cmd->retry_step = last_2bit_step; //vth + HSB read.
        }
    }
    
    if(pdu_err_ncl_cmd->retry_step != last_2bit_step)
    {
        read_retry_handling(ent);
    }
    else  //last_2bit_step, go do FW ARD
    {
        u32 fwArdParams = (0x01 << 8);      //op = 1, Insert ch_id to the ard_queue (byte positions: 0xFF00)
        fwArdParams |= ch_id;               //add ch_id (byte positions: 0x000F)
        fwArdParams |= (CPU_ID << 4);       //add CPU_ID (byte positions: 0x00F0)
        pdu_err_ncl_cmd->ard_rd_cnt = current_retry_entry[ch_id].need_retry_du_cnt;    //indicates that this ncl_cmd is doing FW ARD.
		//When a DU is retry by FW ARD, ard_rd_cnt reduce 1.
		    
        //ncl_cmd_trace(LOG_ALW, 0, "fw_ard_handler, ch_id = %d,CPU%d call CPU3 start FW ARD,pda2ch = %d", ch_id, CPU_ID, pda2ch(ent->pda));  //dbg log
        cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FW_ARD_HANDLER, 0, fwArdParams);  //call CPU3 to handle FW ARD
    }
}

ddr_code void restring_retry_cmd(u8 ch_id, struct ncl_cmd_t *ncl_cmd)
{
    u8 i, err_du_cnt = 0;
    nal_status_t du_sts;
    dtag_t dtag;
    u8 du_offset[DU_CNT_PER_PAGE];
    //memset(current_retry_entry.retry_pda_list, 0x00, sizeof(current_retry_entry.retry_pda_list));
    current_retry_entry[ch_id].complete_retry_du_cnt = 0;
    memcpy(du_offset, current_retry_entry[ch_id].err_du_offset, sizeof(du_offset));
    for(i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++)
    {
        du_sts = ncl_cmd->addr_param.common_param.info_list[i].status;
        //ncl_cmd_trace(LOG_ALW, 0, "du_sts: 0x%x,  err_du_cnt : 0x%d", du_sts, err_du_cnt);

#ifdef History_read
        if (du_sts > ficu_err_par_err || du_sts == ficu_err_du_ovrlmt)
#else
        if (du_sts > ficu_err_par_err)
#endif
        {
            current_retry_entry[ch_id].retry_pda_list[err_du_cnt] = ncl_cmd->addr_param.common_param.pda_list[i];            
            current_retry_entry[ch_id].bm_pl_list[err_du_cnt] = ncl_cmd->user_tag_list[i];
            current_retry_entry[ch_id].err_du_offset[err_du_cnt] = du_offset[i];

            if ((current_retry_entry[ch_id].ncl_cmd->op_type & DYNAMIC_DTAG))
            {
                dtag = rty_get_dtag(ch_id);
                //schl_apl_trace(LOG_INFO, 0, "Retry dtag = 0x%x", dtag.dtag);
                if(dtag.dtag == INV_U32)
                {
                    ncl_cmd_trace(LOG_INFO, 0x0a48, "Retry dtag[0x%x] ch_id[%d] dtag_cmit_cnt[%d]", dtag.dtag, ch_id, rty_que_dtag_cmit_cnt[ch_id]);
                    sys_assert(0);
                }

                current_retry_entry[ch_id].bm_pl.pl.dtag = dtag.dtag;
                current_retry_entry[ch_id].bm_pl_list[err_du_cnt].pl.dtag = dtag.dtag;

                current_retry_entry[ch_id].bm_pl_list[err_du_cnt].pl.type_ctrl &= ~META_SRAM_IDX;
                current_retry_entry[ch_id].bm_pl_list[err_du_cnt].pl.type_ctrl |= META_DDR_DTAG;
            }

            err_du_cnt++;
            
        }
    }
    current_retry_entry[ch_id].need_retry_du_cnt = err_du_cnt;
}

ddr_code void ard_handle_read_retry_done(struct ncl_cmd_t *ncl_cmd)  //For FW ARD to handle next DU.
{
    u8 ch_id = (pda2ch(ncl_cmd->addr_param.common_param.pda_list[0]) % 4);
    
    if(ncl_cmd->ard_rd_cnt == 1) //All DUs have been handled
    {
        ncl_cmd->addr_param.common_param.list_len = current_retry_entry[ch_id].need_retry_du_cnt;  //Restore original length
        handle_read_retry_done(ncl_cmd);  //go back to callback
        
        FW_ARD_EN = false;
    	ncl_cmd_trace(LOG_ALW, 0xb352, "ncl_cmd->ard_rd_cnt = %d, FW_ARD_EN : %x", ncl_cmd->ard_rd_cnt, FW_ARD_EN);
        ncl_handle_pending_cmd();
        return;
    }
    
    ncl_cmd->ard_rd_cnt--;  //A DU is completed.
    ard_read_retry_handling(&current_retry_entry[ch_id]); //handle next DU
}

ddr_code void handle_read_retry_done(struct ncl_cmd_t *ncl_cmd)
{
	bm_pl_t bm_pl = ncl_cmd->user_tag_list[0];
    u32 nsid = INT_NS_ID;
	u32 grp;
	u32 cnt;
    rd_err_ent_t *ent;
    u8 i, restring_retry = mFALSE, shift_cnt = 1, total_du_cnt;
    dtag_t dtag;
	nal_status_t du_sts;
    extern volatile u8 plp_trigger;
    #ifdef RD_FAIL_GET_LDA
    u8 type = DEFAULT_READ_TYPE;
    #endif
    die_cmd_info_t *die_cmd_info;
    rc_req_t* rc_req;
    #ifdef LJ1_WUNC
	io_meta_t *meta;  
    #endif
    #ifdef History_read
    bool His_read;
    hist_tab_offset_t* hist_tab_ptr;
    #endif
    bool rced;
    bool flag_put_ncl_cmd = true;

    u32 rls_du_ofst[DU_CNT_PER_PAGE*2] = {0}; //2Plane
    u32 rls_du_btag[DU_CNT_PER_PAGE*2] = {0};
    u8 idx =0;
    u8 ttl_rls_du_cnt = 0; //1bit need release du cnt
	
    u8 *complete_du_cnt = NULL;
    u8 ch_id;

    ch_id = (pda2ch(ncl_cmd->addr_param.common_param.pda_list[0]) % 4);

    //memcpy((void *) &ent, (void *) &current_retry_entry[pda2ch(ncl_cmd->addr_param.common_param.pda_list[0])], sizeof(ent));
    ent = (rd_err_ent_t *)&current_retry_entry[ch_id];
    if (plp_trigger)
    {
        if (ncl_cmd->retry_step != abort_retry_step)
        {
            ncl_cmd->retry_step = abort_retry_step;
            read_retry_handling(ent);
        }
        else
        {
            //ncl_cmd_trace(LOG_ALW, 0x1ecf, "abort_retry_start, ch_id = %d, total_retry_du_cnt : %d", ch_id, total_retry_du_cnt);
            abort_retry_flow(ch_id);
			ncl_cmd_trace(LOG_ALW, 0x58cc, "abort_retry_done, ch_id = %d, total_retry_du_cnt : %d", ch_id, total_retry_du_cnt);
        }
    }
    else
    {
        rced = (ent->ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;

        if (hp_retry_flag[ch_id] && rced)
        {
            ncl_cmd_trace(LOG_ALW, 0xbb80, "Raid retry pda: 0x%x,need_retry_du_cnt : %d, hp_retry_en: %d, ncl_cmd->flags: 0x%x", ncl_cmd->addr_param.common_param.pda_list[0], need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0])], hp_retry_flag[ch_id], ncl_cmd->flags);
        }

        complete_du_cnt = &complete_du_cnt_ex[ch_id];
        
        if ((ncl_cmd->retry_step == retry_set_shift_value) || (ncl_cmd->retry_step == last_1bit_step) ||
            (ncl_cmd->retry_step == last_1bit_vth_step)
#ifdef History_read
            || (ncl_cmd->retry_step == retry_history_step)
#endif
#if SPOR_RETRY
            || (ncl_cmd->retry_step == spor_retry_step)
#endif
        )
        {
            dtag.dtag = (u32)bm_pl.pl.dtag;

            if(ncl_cmd->retry_step == retry_set_shift_value)
            {
                ncl_cmd->retry_step = retry_read;
            }
#ifdef History_read
            else if(ncl_cmd->retry_step == retry_history_step)
            {
                ncl_cmd->retry_step = retry_history_read;
            }
#endif
#if SPOR_RETRY
            else if(ncl_cmd->retry_step == spor_retry_step)
            {
                ncl_cmd->retry_step = spor_retry_read;
            }
#endif
            else if (ncl_cmd->retry_step == last_1bit_step)
            {
                ncl_cmd->retry_step = last_1bit_read;
            }
            else if(ncl_cmd->retry_step == last_1bit_vth_step)
            {
                ncl_cmd->retry_step = last_1bit_vth_read;
            }

            read_retry_handling(ent);
        }
        else
        {
            #ifdef History_read
            His_read=(ent->ncl_cmd->op_code == NCL_CMD_PATROL_READ) ? true : false;
            #endif
            total_du_cnt = ent->need_retry_du_cnt;
            for(i = ent->complete_retry_du_cnt; i < total_du_cnt; i += shift_cnt)
            {
                bm_pl = ncl_cmd->user_tag_list[i];
                du_sts = ncl_cmd->addr_param.common_param.info_list[i].status;
                //ncl_cmd_trace(LOG_ALW, 0, "i: 0x%x, need_retry_du_cnt : %d, shift_cnt : %d, du_sts: %d", i, current_retry_entry.need_retry_du_cnt, shift_cnt, du_sts);
                #ifdef History_read
                u16 page_index=pda2page(ncl_cmd->addr_param.common_param.pda_list[i]);
                u32 die_idx = pda2die(ncl_cmd->addr_param.common_param.pda_list[i]);  
                u16 blk_idx = pda2blk(ncl_cmd->addr_param.common_param.pda_list[i]);  
                if(His_read && (ncl_cmd->retry_cnt == 4 || ncl_cmd->retry_cnt == 9 || ncl_cmd->retry_cnt == 14))//only read four times
                {
                    ent->ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
                    need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0])] -= (ent->need_retry_du_cnt - ent->complete_retry_du_cnt);
                    total_retry_du_cnt -= (ent->need_retry_du_cnt - ent->complete_retry_du_cnt);
                    (*complete_du_cnt) += (ent->need_retry_du_cnt - ent->complete_retry_du_cnt); //230209 complete_du_cnt bug fixed
                    ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                    shift_cnt = ent->complete_retry_du_cnt - i;
                    if (*complete_du_cnt == ent->ncl_cmd->err_du_cnt)
                    {
                        *complete_du_cnt = 0;               
                        ent->ncl_cmd->retry_step = retry_end;
                        ent->ncl_cmd->completion(ent->ncl_cmd);
                    }
                    read_error_state_ex[ch_id] = RD_FE_ISSUING;
                    hst_rd_err_cmpl(ch_id);
                    continue;
                }
                #endif
                if(du_sts == ficu_err_good || du_sts == ficu_err_par_err)
                {              
                    #ifdef History_read
                    //=========================================[BLK][DIE][WL Group in one BLK][page]->[831][32][WL/Group Num][3]
                    sys_assert(blk_idx < shr_nand_info.geo.nr_blocks);
                    if(FTL_INIT_DONE)
                    {
                        hist_tab_ptr = hist_tab_ptr_st+blk_idx;    //"hist_tab_ptr"shift by SPB index
                        hist_tab_ptr->shift_index[die_idx][page_index/Group_num][page_index%3] = ncl_cmd->retry_cnt;     
                        //ncl_cmd_trace(LOG_ALW, 0, "offset_temp address :0x%x",&hist_tab_ptr->shift_index[die_idx][page_index/Group_num][page_index%3]);     
                    }
                    #endif
                    need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0])]--;
                    total_retry_du_cnt--;
                    (*complete_du_cnt)++;
                    ent->complete_retry_du_cnt++;
                    ent->ncl_cmd->addr_param.common_param.info_list[ent->err_du_offset[i]].status = du_sts;
                    ncl_cmd_trace(LOG_ALW, 0xe729, "pda: 0x%x, retry pass, retry_table: %d, need_retry_du_cnt : %d, hp_retry_en: %d", ncl_cmd->addr_param.common_param.pda_list[i], ncl_cmd->retry_cnt, 
                                    need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[i])], hp_retry_en);

                    dtag.dtag = bm_pl.pl.dtag;
                    dtag.dtag = (u32)bm_pl.pl.dtag;
                    meta = dtag.b.in_ddr ? shr_ddtag_meta : shr_dtag_meta;

                    if (du_sts == ficu_err_par_err)
                    {
                        ent->ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
                    }

                    if (*complete_du_cnt == ent->ncl_cmd->err_du_cnt)
                    {
                        *complete_du_cnt = 0;                 
                        ent->ncl_cmd->retry_step = retry_end;
#if defined(HMETA_SIZE)
                        if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent->ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                        if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                        {
                            ent->ncl_cmd->completion(ent->ncl_cmd);
                        }
#ifdef History_read 
                        else if(His_read)
                        {
                            ent->ncl_cmd->completion(ent->ncl_cmd);
                        }
#endif
                        else
                        {   
                            die_cmd_info = (die_cmd_info_t *) ent->ncl_cmd->caller_priv;
                            if(ent->ncl_cmd->status & NCL_CMD_ERROR_STATUS)
                            {
                                if (die_cmd_info->cmd_info.b.host)
        						{
                                    nsid = INT_NS_ID - 1;
                                }

                                if (fcns_raid_enabled(nsid) && is_there_uc_pda(ent->ncl_cmd) && (rced == false))
                                {   
                                    ncl_cmd_trace(LOG_ERR, 0x1b79, "raid_correct_push, pda : 0x%x", ncl_cmd->addr_param.common_param.pda_list[0]);
                                    rc_req = rc_req_prepare(ent->ncl_cmd, true);
        							flag_put_ncl_cmd = false;
                                    raid_correct_push(rc_req);
                                }
                                
                                dfu_blank_err_chk(ent->ncl_cmd, die_cmd_info->cmd_info);
                            }
                            
                            
                            if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug) && (flag_put_ncl_cmd)) 
                            {
                        		//nvm_cmd_id used for gc pda group in gc read
                        		grp = ent->ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                        		cnt = ent->ncl_cmd->addr_param.common_param.list_len;
                                //gc_dfu_err_chk(ent.ncl_cmd);
                        		scheduler_gc_read_done(grp, ent->ncl_cmd->user_tag_list, cnt);
                        	}
                            
                     		ncl_cmd_put(ent->ncl_cmd, flag_put_ncl_cmd);
                        }               
                    } 
                    
                    read_error_state_ex[ch_id] = RD_FE_ISSUING;

                    if ((bm_pl.pl.btag == ua_btag) && (rced == false) && (FTL_INIT_DONE))
                    {
                        ncl_cmd_trace(LOG_INFO, 0x4908, "fill-up retry");
                        ipc_api_ucache_read_error_data_in(bm_pl.pl.du_ofst, du_sts);
                    }


                    if (ent->op_type & DYNAMIC_DTAG)
                    {                   
                        read_error_state_ex[ch_id] = RD_FE_ISSUING;

                        #ifdef RETRY_COMMIT_EVENT_TRIGGER
                        //if (!is_ptr_sharable(&bm_pl))
                        //    &bm_pl = tcm_local_to_share(&bm_pl);
                        if(CPU_BE == CPU_ID)
                        {
                            memset(&recommit_cpu2[ch_id], 0, sizeof(recover_commit_t));
                            memcpy(&recommit_cpu2[ch_id].bm_pl, &bm_pl, sizeof(bm_pl_t));
                        }
                        else
                        {
                            //ncl_cmd_trace(LOG_INFO, 0, "retry pass: cpu4 need to free dtag before commit");
                            dref_comt_poll();  //check that dtag is need to release or not, avoid cpu1 & cpu4 to conflict with each other
                            
                            memset(&recommit_cpu4[ch_id], 0, sizeof(recover_commit_t));
                            memcpy(&recommit_cpu4[ch_id].bm_pl, &bm_pl, sizeof(bm_pl_t));
                        }
                        #endif

    	                #ifdef NCL_RETRY_PASS_REWRITE
    	                if(die_cmd_info->cmd_info.b.stream)
    	                {
                            //if(ncl_cmd->retry_cnt >= 16)   
        	                if(ncl_cmd->retry_cnt >= rd_rew_level)
                            {   
        	                    //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry pass: do rewrite");
        	                    if(CPU_BE == CPU_ID)
        	                    {
        	                        //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry pass: cpu2 do rewrite");
        	                        cpu_msg_issue(CPU_FE - 1, CPU_MSG_RETRY_GET_LDA_REWRITE, 0, (u32)&recommit_cpu2[ch_id].bm_pl);
        	                    }
        	                    else
        	                    {
        	                        //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry pass: cpu4 do rewrite");
                                    cpu_msg_issue(CPU_FE - 1, CPU_MSG_RETRY_GET_LDA_REWRITE, 0, (u32)&recommit_cpu4[ch_id].bm_pl);
        	                    }
                            }
    	                }
    	                #endif

                        #ifdef LJ1_WUNC
                        if (du_sts == ficu_err_par_err)
                        {
                            #ifdef RETRY_COMMIT_EVENT_TRIGGER
                            if(CPU_BE == CPU_ID)
                            {
                                recommit_cpu2[ch_id].pdu_bmp = meta[dtag.b.dtag].wunc.WUNC;
                                read_recoveried_commit(&recommit_cpu2[ch_id].bm_pl, recommit_cpu2[ch_id].pdu_bmp);
                            }
                            else
                            {
                                recommit_cpu4[ch_id].pdu_bmp = meta[dtag.b.dtag].wunc.WUNC;
                                read_recoveried_commit(&recommit_cpu4[ch_id].bm_pl, recommit_cpu4[ch_id].pdu_bmp);                    
                            }
                            return;
                            #else
                            read_recoveried_commit(&bm_pl, meta->wunc.WUNC);
                            //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry done: read recver cmit return");
                            #endif
                        }
                        else
                        #endif
                        {
                            #ifdef RETRY_COMMIT_EVENT_TRIGGER
                            if(CPU_BE == CPU_ID)
                            {
                                recommit_cpu2[ch_id].pdu_bmp = 0;
                                read_recoveried_commit(&recommit_cpu2[ch_id].bm_pl, recommit_cpu2[ch_id].pdu_bmp);
                            }
                            else
                            {
                                recommit_cpu4[ch_id].pdu_bmp = 0;
                                read_recoveried_commit(&recommit_cpu4[ch_id].bm_pl, recommit_cpu4[ch_id].pdu_bmp);                    
                            }
                            return;
                            #else
                            read_recoveried_commit(&bm_pl, 0);
                            //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry done: read recver cmit return");
                            #endif
                        }
                    }
                    else
                    {
                        if (ent->complete_retry_du_cnt == total_du_cnt)
                        {
                            //ncl_cmd_trace(LOG_ALW, 0, "hst_rd_err_cmpl");
                            hst_rd_err_cmpl(ch_id);
                        }
                    }
                }
                #ifdef History_read 
                else if(du_sts == ficu_err_du_ovrlmt)
                {
                   // ncl_cmd_trace(LOG_ALW, 0, "History_read[lim]/entry_cmd~pda=0x%x,retry_cnt[%d]",ent->ncl_cmd->addr_param.common_param.pda_list[0],ncl_cmd->retry_cnt);
                    restring_retry = mTRUE;
                    ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                    shift_cnt = ent->complete_retry_du_cnt - i;
                }
                #endif
                else if((du_sts > ficu_err_par_err) && (du_sts < ficu_err_1bit_retry_err))
                {
                    restring_retry = mTRUE;

                    for (idx = i; idx < ent->need_retry_du_cnt; idx++)
                    {
                        if (ncl_cmd->addr_param.common_param.info_list[idx].status != ficu_err_du_erased)
                        {
                            ncl_cmd->addr_param.common_param.info_list[idx].status = ficu_err_du_uc;
                        }
                    }
                    
                    ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                    shift_cnt = ent->complete_retry_du_cnt - i;

                    if  (ent->op_type & DYNAMIC_DTAG)
                    {                    
                        rty_que_dtag_cmit_cnt[ch_id] = 0;
                    }
                }
                else if(du_sts == ficu_err_1bit_retry_err)
                {
                    if(ent->ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG)
                    {
                        switch (read_error_state_ex[ch_id])
                        {
                            case RD_VTH_START:
							    //When vth tracking has started, jump out read error handling untill vth tracking done.
                            break;
                            case RD_VTH_DONE:
                                //When vth tracking done, go to read retry with vth value.
                                restring_retry = mTRUE;
                    
                                for (idx = i; idx < ent->need_retry_du_cnt; idx++)
                                {
                                    if (ncl_cmd->addr_param.common_param.info_list[idx].status != ficu_err_du_erased)
                                    {
                                        ncl_cmd->addr_param.common_param.info_list[idx].status = ficu_err_du_uc;
                                    }
                                }
                    
                                ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                                shift_cnt = ent->complete_retry_du_cnt - i;

                                if (ent->op_type & DYNAMIC_DTAG)
                                {                    
                                    rty_que_dtag_cmit_cnt[ch_id] = 0;
                                }
                            
                            break;
                            default:
                                //1 bit error occurs, start vth tracking. 
                                read_error_state_ex[ch_id]=RD_VTH_START;
                                bool ret;
                                
                                if(rced == true)
                                {
                                    CBF_INS(&hp_vth_que, ret, ch_id);
                                }
                                else
                                {
                                    CBF_INS(&pdu_vth_que, ret, ch_id);
                                }
                                
                                sys_assert(ret == true);
                                vth_queue_handler(); //if vth_tracking not using, go to do vth_tracking.
                            break;
                        }
                    }
                    else
                    {
                        restring_retry = mTRUE;
                    
                        for (idx = i; idx < ent->need_retry_du_cnt; idx++)
                        {
                            if (ncl_cmd->addr_param.common_param.info_list[idx].status != ficu_err_du_erased)
                            {
                                ncl_cmd->addr_param.common_param.info_list[idx].status = ficu_err_du_uc;
                            }
                        }
                    
                        ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                        shift_cnt = ent->complete_retry_du_cnt - i;

                        if  (ent->op_type & DYNAMIC_DTAG)
                        {                    
                            rty_que_dtag_cmit_cnt[ch_id] = 0;
                        }
                    }
                    break; //jump out for loop
                }
                else if((du_sts == ficu_err_1bit_vth_retry_err) || (du_sts == ficu_err_2bit_retry_err)
                #if SPOR_RETRY
                        || (du_sts == ficu_err_spor_err)
                #endif        
                        )
                {
#ifdef EH_ENABLE_2BIT_RETRY  
                    if((du_sts == ficu_err_1bit_vth_retry_err) && (ncl_cmd->rty_blank_flg == false) 
                        && (ncl_cmd->rty_dfu_flg == false) && (ent->ncl_cmd->flags & NCL_CMD_XLC_PB_TYPE_FLAG))
                    {	
						//1bit vth error occurred, go to do FW ARD if the error is not blank or dfu error.
						//Only support XLC mode
                        restring_retry = mTRUE;
                    
                        for (idx = i; idx < ent->need_retry_du_cnt; idx++)
                        {
                            if (ncl_cmd->addr_param.common_param.info_list[idx].status != ficu_err_du_erased)
                            {
                                ncl_cmd->addr_param.common_param.info_list[idx].status = ficu_err_du_uc;
                            }
                        }
                    
                        ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                        shift_cnt = ent->complete_retry_du_cnt - i;

                        if  (ent->op_type & DYNAMIC_DTAG)
                        {                    
                            rty_que_dtag_cmit_cnt[ch_id] = 0;
                        }
                        
                    }
                    else  //End retry flow (Including Normal retry flow and RCED retry flow).
#endif
                    {                    
                        ncl_cmd_trace(LOG_ERR, 0xf36c, "pda: 0x%x, retry fail, need_retry_du_cnt : %d", ncl_cmd->addr_param.common_param.pda_list[0],need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0])]);
                        ent->ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
                        need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0])] -= (ent->need_retry_du_cnt - ent->complete_retry_du_cnt);
                        total_retry_du_cnt -= (ent->need_retry_du_cnt - ent->complete_retry_du_cnt);
                        //complete_handler_du_cnt += ent->need_retry_du_cnt;
                        (*complete_du_cnt)+= (ent->need_retry_du_cnt - ent->complete_retry_du_cnt); //230209 complete_du_cnt bug fixed
                        ent->complete_retry_du_cnt = ent->need_retry_du_cnt;
                        shift_cnt = ent->complete_retry_du_cnt - i;
                        //ncl_cmd_trace(LOG_ALW, 0, "hp_retry_en: %d, ncl_cmd->flags: 0x%x", hp_retry_en, ncl_cmd->flags);
                        ncl_cmd_trace(LOG_ALW, 0xe366, "complete_handler_du_cnt: %d, err_du_cnt : %d", *complete_du_cnt, current_retry_entry[ch_id].ncl_cmd->err_du_cnt);
                        //ncl_cmd_trace(LOG_ALW, 0, "i: 0x%x, need_retry_du_cnt : %d, shift_cnt : %d", i, current_retry_entry.need_retry_du_cnt, shift_cnt);
                        if  (ent->op_type & DYNAMIC_DTAG)
                        {
                            rty_que_dtag_cmit_cnt[ch_id] = 0;
                        }

                        //===============================================print lda=====================================================//
                        #ifdef RD_FAIL_GET_LDA
                        if(FTL_INIT_DONE						
    						#if RAID_SUPPORT_UECC
    						&& (ent->ncl_cmd->uecc_type != NCL_UECC_AUX_RD)
    						#endif
                            && rced == false
    						)
                        {
                            if(ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
                            {
                                type = OTHER_READ_TYPE;
                            }
                            else
                            {
                                die_cmd_info = (die_cmd_info_t *) ent->ncl_cmd->caller_priv;
                                if(die_cmd_info->cmd_info.b.stream)
                                    type = HOST_READ_TYPE;
                                else if(die_cmd_info->cmd_info.b.gc)
                                    type = GC_READ_TYPE;
                                else
                                    type = OTHER_READ_TYPE;
                            }
                            rd_err_get_lda(&bm_pl, ncl_cmd->addr_param.common_param.pda_list[i], type);
                        }
                        #endif
                        //=============================================================================================================//
                        for(idx = 0; idx < ent->need_retry_du_cnt; idx++)
                        {
                            if (ent->ncl_cmd->op_type & DYNAMIC_DTAG)
                            {
                                #if DFU_MARK_ECC
                                if((ent->du_sts == ficu_err_du_erased) || (ent->du_sts == ficu_err_dfu))
                                #else
                                if(ent->du_sts == ficu_err_du_erased)
                                #endif
                                {
                                    rls_du_ofst[ttl_rls_du_cnt] = (u32)ent->bm_pl_list[idx].pl.du_ofst;
                                    rls_du_btag[ttl_rls_du_cnt] = (u32)ent->bm_pl_list[idx].pl.btag;
                                    ttl_rls_du_cnt++;
                                }
                            }
                            else if (bm_pl.pl.btag == ua_btag && (FTL_INIT_DONE))
                            {
                                #if DFU_MARK_ECC
                                if((ent->du_sts == ficu_err_du_erased) || (ent->du_sts == ficu_err_dfu))
                                #else
                                if(ent->du_sts == ficu_err_du_erased)
                                #endif
                                {
                                    ttl_rls_du_cnt++;
                                }
                            }
                        }

                        if (*complete_du_cnt == ent->ncl_cmd->err_du_cnt)
                        {                  
                            ent->ncl_cmd->retry_step = retry_end;
                            *complete_du_cnt = 0;
#if defined(HMETA_SIZE)
                            if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent->ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                            if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                            {
                                ent->ncl_cmd->completion(ent->ncl_cmd);
                            }
#ifdef History_read
                            else if(ent->ncl_cmd->op_code == NCL_CMD_PATROL_READ)
                            {
                                ent->ncl_cmd->completion(ent->ncl_cmd);
                            }
#endif
                            else if (rced == false)
                            {
                                die_cmd_info = (die_cmd_info_t *) ent->ncl_cmd->caller_priv;

                                if (die_cmd_info->cmd_info.b.host)
            					{
                                    nsid = INT_NS_ID - 1;
                                }
                                //if (fcns_raid_enabled(nsid) && is_there_uc_pda(ent.ncl_cmd) && (rced == false) && (die_cmd_info->cmd_info.b.stream))  //test
                                if (fcns_raid_enabled(nsid) && is_there_uc_pda(ent->ncl_cmd) && (rced == false))
                                {   
                                    ncl_cmd_trace(LOG_ERR, 0xfd2f, "raid_correct_push, pda : 0x%x", ncl_cmd->addr_param.common_param.pda_list[0]);
                                    rc_req = rc_req_prepare(ent->ncl_cmd, true);
            						flag_put_ncl_cmd = false;
                                    raid_correct_push(rc_req);
                                }
                                
                                dfu_blank_err_chk(ent->ncl_cmd, die_cmd_info->cmd_info);  //dfu err mark ecct

                                if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug) && (flag_put_ncl_cmd)) 
                                {
                                	//nvm_cmd_id used for gc pda group in gc read
                                	grp = ent->ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                                	cnt = ent->ncl_cmd->addr_param.common_param.list_len;
                                    //gc_dfu_err_chk(ent.ncl_cmd);
                                	scheduler_gc_read_done(grp, ent->ncl_cmd->user_tag_list, cnt);
                                }
                               	ncl_cmd_put(ent->ncl_cmd, flag_put_ncl_cmd);
                            }
                        }
                        
    					//if (ent.ncl_cmd->op_type & DYNAMIC_DTAG)
                        if (ttl_rls_du_cnt != 0 && ent->ncl_cmd->op_type & DYNAMIC_DTAG)
                        {
                            //bm_err_commit(bm_pl.pl.du_ofst, bm_pl.pl.btag);
                            for(idx = 0; idx < ttl_rls_du_cnt; idx++)
                            { 
                                bm_err_commit(rls_du_ofst[idx], rls_du_btag[idx]);
                            }
                            ttl_rls_du_cnt = 0;
                        }
                        else if (ttl_rls_du_cnt != 0 && bm_pl.pl.btag == ua_btag && (FTL_INIT_DONE))
                        {
                            ncl_cmd_trace(LOG_INFO, 0x0ad7, "fill-up retry");
                            ipc_api_ucache_read_error_data_in(bm_pl.pl.du_ofst, du_sts);
                            ttl_rls_du_cnt = 0;
                        }
    					
                        hst_rd_err_cmpl(ch_id);
                    }
                }
                else
                {
                    ent->ncl_cmd->retry_step = retry_end;
                    *complete_du_cnt = 0;
#if defined(HMETA_SIZE)
                    if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent->ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                    if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                    {
                        ent->ncl_cmd->completion(ent->ncl_cmd);
                    }

                }
            }
            if (restring_retry)
            {
                restring_retry_cmd(ch_id, ncl_cmd);
                shift_to_next_retry_step((u8) ent->du_sts, ent);
            }
        }
    }
}

#else
slow_code void abort_retry_flow(void)
{
    rd_err_ent_t ent;
	die_cmd_info_t *die_cmd_info;
    
    while(!CBF_EMPTY(&pdu_err_que))
    {
        CBF_HEAD(&pdu_err_que, ent);
        die_cmd_info = (die_cmd_info_t *) ent.ncl_cmd->caller_priv;
        total_retry_du_cnt--;
        complete_handler_du_cnt++;
        ncl_cmd_trace(LOG_DEBUG, 0x6c1b, "complete_handler_du_cnt : %d", complete_handler_du_cnt);
        if (complete_handler_du_cnt ==  ent.ncl_cmd->err_du_cnt)
        {
            complete_handler_du_cnt = 0;
            ent.ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
            ent.ncl_cmd->retry_step = retry_end;
            ent.ncl_cmd->flags |= NCL_CMD_RCED_FLAG;
#if defined(HMETA_SIZE)
            if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent.ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
            if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
            {
                ent.ncl_cmd->completion(ent.ncl_cmd);
            }
            else
            {   
				bool flag_put_ncl_cmd = true;
                if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug)) {
                	//nvm_cmd_id used for gc pda group in gc read
                	u32 grp = ent.ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                	u32 cnt = ent.ncl_cmd->addr_param.common_param.list_len;

                	scheduler_gc_read_done(grp, ent.ncl_cmd->user_tag_list, cnt);
                }
                ncl_cmd_trace(LOG_DEBUG, 0x8e5d, "ncl_cmd_put,  ncl_cmd : %d", ent.ncl_cmd);
            	ncl_cmd_put(ent.ncl_cmd, flag_put_ncl_cmd);
            }               
        }

        if (ent.bm_pl.pl.btag == ua_btag)
        {
            ipc_api_ucache_read_error_data_in(ent.bm_pl.pl.du_ofst, ent.du_sts);
        }
        else
        {
            if (die_cmd_info->cmd_info.b.gc)
            {
                ent.bm_pl.pl.btag = 0xFFF;   //gc bed blk mark     
            }
        }

        CBF_REMOVE_HEAD(&pdu_err_que);
    }

    while(!CBF_EMPTY(&hp_read_err_que))
    {
        CBF_HEAD(&hp_read_err_que, ent);
        die_cmd_info = (die_cmd_info_t *) ent.ncl_cmd->caller_priv;
        total_retry_du_cnt--;
        complete_handler_du_cnt++;
        ncl_cmd_trace(LOG_DEBUG, 0xf783, "complete_handler_du_cnt : %d", complete_handler_du_cnt);
        if (complete_handler_du_cnt ==  ent.ncl_cmd->err_du_cnt)
        {
            complete_handler_du_cnt = 0;
            ent.ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
            ent.ncl_cmd->retry_step = retry_end;
            ent.ncl_cmd->flags |= NCL_CMD_RCED_FLAG;
#if defined(HMETA_SIZE)
            if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent.ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
            if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
            {
                ent.ncl_cmd->completion(ent.ncl_cmd);
            }
            else
            {   
				bool flag_put_ncl_cmd = true;
                if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug)) {
                	//nvm_cmd_id used for gc pda group in gc read
                	u32 grp = ent.ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                	u32 cnt = ent.ncl_cmd->addr_param.common_param.list_len;

                	scheduler_gc_read_done(grp, ent.ncl_cmd->user_tag_list, cnt);
                }
                ncl_cmd_trace(LOG_DEBUG, 0x00ec, "ncl_cmd_put,  ncl_cmd : %d", ent.ncl_cmd);
            	ncl_cmd_put(ent.ncl_cmd, flag_put_ncl_cmd);
            }               
        }

        if (ent.bm_pl.pl.btag == ua_btag)
        {
            ipc_api_ucache_read_error_data_in(ent.bm_pl.pl.du_ofst, ent.du_sts);
        }
        else
        {
                if (die_cmd_info->cmd_info.b.gc)
                {
                    ent.bm_pl.pl.btag = 0xFFF;   //gc bed blk mark     
                }
            }

        CBF_REMOVE_HEAD(&hp_read_err_que);
    }
    read_error_state = RD_ERR_IDLE;

}

slow_code void read_retry_handling(rd_err_ent_t *ent, dtag_t dtag)
{    
    ent->bm_pl.pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
    if (ent->op_type & DYNAMIC_DTAG)
    {
	    ent->bm_pl.pl.dtag = dtag.dtag;
    }

    if ((ent->du_sts == ficu_err_par_err) && (dtag.b.in_ddr))
    {
        ent->bm_pl.pl.type_ctrl |= META_DDR_DTAG;
        ent->bm_pl.pl.type_ctrl &= ~META_SRAM_IDX;
    }

	memset(&pdu_err_ncl_cmd.addr_param.rapid_du_param.info, 0, sizeof(struct info_param_t));
	pdu_err_ncl_cmd.addr_param.rapid_du_param.info.pb_type = ent->pb_type;
	pdu_err_ncl_cmd.addr_param.rapid_du_param.pda = ent->pda;
    
    if ((under_retry_pda>>2) != (ent->pda>>2))
    {
        pdu_err_ncl_cmd.retry_cnt = 0;
        under_retry_pda = ent->pda;
    }
    
	pdu_err_ncl_cmd.addr_param.rapid_du_param.list_len = 1;
    #if RAID_SUPPORT_UECC
    pdu_err_ncl_cmd.caller_priv = ent->ncl_cmd->caller_priv;
	pdu_err_ncl_cmd.uecc_type = ent->ncl_cmd->uecc_type;
    #else
    pdu_err_ncl_cmd.caller_priv = NULL;
    #endif
    pdu_err_ncl_cmd.completion = handle_read_retry_done;
	pdu_err_ncl_cmd.du_user_tag_list = ent->bm_pl;
	pdu_err_ncl_cmd.du_format_no = ent->ncl_cmd->du_format_no;
	pdu_err_ncl_cmd.flags = ent->ncl_cmd->flags;
    pdu_err_ncl_cmd.flags |= NCL_CMD_RAPID_PATH;
    pdu_err_ncl_cmd.flags &= ~NCL_CMD_COMPLETED_FLAG;
    #ifdef EH_ENABLE_2BIT_RETRY
    if(ent->ncl_cmd->flags & NCL_CMD_RCED_FLAG)
        pdu_err_ncl_cmd.flags |= NCL_CMD_RCED_FLAG;
    #endif
    pdu_err_ncl_cmd.rty_blank_flg = false;
    pdu_err_ncl_cmd.rty_dfu_flg = false;
    
    if ((pdu_err_ncl_cmd.retry_step == last_retry_step) || (pdu_err_ncl_cmd.retry_step == retry_set_shift_value) || 
        (pdu_err_ncl_cmd.retry_step == last_1bit_step))
    {
        #ifdef NCL_FW_RETRY_BY_SUBMIT
	    pdu_err_ncl_cmd.op_code = NCL_CMD_FW_RETRY_SET_FEATURE;
        #else
        pdu_err_ncl_cmd.op_code = NCL_CMD_SET_GET_FEATURE;
        #endif
    }
    else
    {
        #ifdef NCL_FW_RETRY_BY_SUBMIT
	    pdu_err_ncl_cmd.op_code = NCL_CMD_FW_RETRY_READ;
        #else
	    pdu_err_ncl_cmd.op_code = NCL_CMD_OP_READ;
        #endif
        if (ent->op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
        {
            pdu_err_ncl_cmd.op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
        }
        else
        {
            pdu_err_ncl_cmd.op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
        }
    }

    #ifdef NCL_FW_RETRY_BY_SUBMIT
    ncl_cmd_submit(&pdu_err_ncl_cmd);
    #else
    ncl_cmd_read_retry(&pdu_err_ncl_cmd);
    #endif

    
}

slow_code void shift_to_next_retry_step(u8 du_sts)
{
	rd_err_ent_t ent;
	dtag_t dtag = { .dtag = DDR_RD_RECOVERY };
    u8 max_retry_cnt;
    
	dtag.b.in_ddr = 1;

    if (hp_retry_en)
    {
	    CBF_HEAD(&hp_read_err_que, ent);
    }
    else
    {
	    CBF_HEAD(&pdu_err_que, ent);
    }
    
    pdu_err_ncl_cmd.retry_cnt++;

    if (du_sts == ficu_err_du_erased)
    {
        max_retry_cnt = 1;
    }
    else
    {
        max_retry_cnt = (pdu_err_ncl_cmd.flags & NCL_CMD_SLC_PB_TYPE_FLAG) ? RR_STEP_SLC : RR_STEP_XLC;
    }

    if (pdu_err_ncl_cmd.retry_cnt >= max_retry_cnt)
    {
        pdu_err_ncl_cmd.retry_step = last_retry_step;
    }
    else if(pdu_err_ncl_cmd.retry_cnt == (max_retry_cnt - 1))
    {
        pdu_err_ncl_cmd.retry_step = last_1bit_step;
    }
    else
    {
        pdu_err_ncl_cmd.retry_step = retry_set_shift_value;
    }
    
    read_retry_handling(&ent, dtag);
}

slow_code void handle_read_retry_done(struct ncl_cmd_t *ncl_cmd)
{
	bm_pl_t bm_pl = ncl_cmd->du_user_tag_list;
    u32 nsid = INT_NS_ID;
	u32 grp;
	u32 cnt;
    dtag_t dtag;
    rd_err_ent_t ent;
	nal_status_t du_sts = ncl_cmd->addr_param.rapid_du_param.info.status;
    extern volatile u8 plp_trigger;
    #ifdef RD_FAIL_GET_LDA
    u8 type = DEFAULT_READ_TYPE;
    #endif
    die_cmd_info_t *die_cmd_info;
    rc_req_t* rc_req;
    #ifdef LJ1_WUNC
	io_meta_t *meta;  
    #endif
    bool rced;
    bool flag_put_ncl_cmd = true;
    
    u32 rls_du_ofst[DU_CNT_PER_PAGE*2] = {0}; //2Plane
    u32 rls_du_btag[DU_CNT_PER_PAGE*2] = {0};
    u8 i =0;
    u8 ttl_rls_du_cnt = 0; //1bit need release du cnt
    
    if (plp_trigger)
    {
        ncl_cmd_trace(LOG_ALW, 0xc1d3, "abort_retry_flow, total_retry_du_cnt : %d", total_retry_du_cnt);

        if (ncl_cmd->retry_step != last_retry_step)
        {
             if (hp_retry_en && (ncl_cmd->flags & NCL_CMD_RCED_FLAG))
             {
                CBF_HEAD(&hp_read_err_que, ent);
             }
             else
             {
                CBF_HEAD(&pdu_err_que, ent);
             }
             
             if(ent.ncl_cmd->op_type & DYNAMIC_DTAG)
             {
         		dtag.dtag = DDR_RD_RECOVERY;
                 dtag.b.in_ddr = 1;
             }
             else
             {
         	    dtag.dtag = ent.bm_pl.pl.dtag;
             }
             ncl_cmd->retry_step = last_retry_step;
             read_retry_handling(&ent, dtag);
        }
        else
        {
            abort_retry_flow();
            ncl_cmd_trace(LOG_ALW, 0x137b, "abort_retry done");
        }
    }
    else
    {
        if ((hp_retry_en == true) && (ncl_cmd->flags & NCL_CMD_RCED_FLAG))
        {
            ncl_cmd_trace(LOG_ALW, 0x3ea8, "Raid retry pda: 0x%x,need_retry_du_cnt : %d, hp_retry_en: %d, ncl_cmd->flags: 0x%x", ncl_cmd->addr_param.rapid_du_param.pda, need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)], hp_retry_en, ncl_cmd->flags);
            CBF_HEAD(&hp_read_err_que, ent);
            ncl_cmd_trace(LOG_ALW, 0x4f22, "Raid retry NCL:CMD: 0x%x ", ent.ncl_cmd);
        }
        else
        {
            CBF_HEAD(&pdu_err_que, ent);
        }
        die_cmd_info = (die_cmd_info_t *) ent.ncl_cmd->caller_priv;
    	rced = (ent.ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
        
        if ((ncl_cmd->retry_step == retry_set_shift_value) || (ncl_cmd->retry_step == last_retry_step) ||
            (ncl_cmd->retry_step == last_1bit_step))
        {
            if(ent.ncl_cmd->op_type & DYNAMIC_DTAG)
            {
        		dtag.dtag = DDR_RD_RECOVERY;
                dtag.b.in_ddr = 1;
            }
            else
            {
        	    dtag.dtag = ent.bm_pl.pl.dtag;
            }
            
            if (ncl_cmd->retry_step == last_retry_step)
            {
                ncl_cmd->retry_step = last_retry_read;
            }
            else if(ncl_cmd->retry_step == last_1bit_step)
            {
                ncl_cmd->retry_step = last_1bit_read;
            }
            else
            {
                ncl_cmd->retry_step = retry_read;
            }
            read_retry_handling(&ent, dtag);
        }
        else
        {
            if(du_sts <= ficu_err_par_err)
            {
                need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)]--;
                total_retry_du_cnt--;
                complete_handler_du_cnt++;
                ent.ncl_cmd->addr_param.common_param.info_list[ent.du_offset].status = du_sts;
                ncl_cmd_trace(LOG_ALW, 0xc61c, "pda: 0x%x, retry pass, need_retry_du_cnt : %d", ncl_cmd->addr_param.rapid_du_param.pda,need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)]);
                dtag.dtag = bm_pl.pl.dtag;
                meta = dtag.b.in_ddr ? shr_ddtag_meta : shr_dtag_meta;

                if (du_sts == ficu_err_par_err)
                {
                    ent.ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
                }

                if ((hp_retry_en == true) && (ncl_cmd->flags & NCL_CMD_RCED_FLAG))
                {
                    hp_retry_done = true;
                }
                else
                {
                    hp_retry_done = false;
                }
                
                if (complete_handler_du_cnt ==  ent.ncl_cmd->err_du_cnt)
                {
                    complete_handler_du_cnt=0;
                    ent.ncl_cmd->retry_step = retry_end;
#if defined(HMETA_SIZE)
                    if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent.ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                    if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                    {
                        ent.ncl_cmd->completion(ent.ncl_cmd);
                    }
                    else
                    {   
                        if(ent.ncl_cmd->status & NCL_CMD_ERROR_STATUS)
                        {
                            if (die_cmd_info->cmd_info.b.host)
    						{
                                nsid = INT_NS_ID - 1;
                            }

                            if (fcns_raid_enabled(nsid) && is_there_uc_pda(ent.ncl_cmd) && (rced == false))
                            {   
                                ncl_cmd_trace(LOG_ERR, 0x9ac9, "raid_correct_push, pda : 0x%x", ncl_cmd->addr_param.rapid_du_param.pda);
                                rc_req = rc_req_prepare(ent.ncl_cmd, true);
    							flag_put_ncl_cmd = false;
                                raid_correct_push(rc_req);
                            }
                        }

                        dfu_blank_err_chk(ent.ncl_cmd, die_cmd_info->cmd_info);
                        
                        if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug) && (flag_put_ncl_cmd)) 
                        {
                    		//nvm_cmd_id used for gc pda group in gc read
                    		grp = ent.ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                    		cnt = ent.ncl_cmd->addr_param.common_param.list_len;
                            //gc_dfu_err_chk(ent.ncl_cmd);
                    		scheduler_gc_read_done(grp, ent.ncl_cmd->user_tag_list, cnt);
                    	}
                        
                 		ncl_cmd_put(ent.ncl_cmd, flag_put_ncl_cmd);
                    }               
                } 
                
                            
                read_error_state = RD_FE_ISSUING;
                

                if ((bm_pl.pl.btag == ua_btag) && (rced == false))
                {
                    ncl_cmd_trace(LOG_INFO, 0xed6f, "fill-up retry");
                    ipc_api_ucache_read_error_data_in(bm_pl.pl.du_ofst, du_sts);
                }


                if (ent.ncl_cmd->op_type & DYNAMIC_DTAG)
                {
                    read_error_state = RD_FE_ISSUING;
                    
                    #ifdef RETRY_COMMIT_EVENT_TRIGGER
                    //if (!is_ptr_sharable(&bm_pl))
                    //    &bm_pl = tcm_local_to_share(&bm_pl);
                    if(CPU_BE == CPU_ID)
                    {
                        memset(&recommit_cpu2, 0, sizeof(recover_commit_t));
                        memcpy(&recommit_cpu2.bm_pl, &bm_pl, sizeof(bm_pl_t));
                    }
                    else
                    {
                        //ncl_cmd_trace(LOG_INFO, 0, "retry pass: cpu4 need to free dtag before commit");
                        dref_comt_poll();  //check that dtag is need to release or not, avoid cpu1 & cpu4 to conflict with each other
                        memset(&recommit_cpu4, 0, sizeof(recover_commit_t));
                        memcpy(&recommit_cpu4.bm_pl, &bm_pl, sizeof(bm_pl_t));
                    }
                    #endif

	                #ifdef NCL_RETRY_PASS_REWRITE
	                if(die_cmd_info->cmd_info.b.stream)
	                {
                        //if(ncl_cmd->retry_cnt >= 16)   
    	                if(ncl_cmd->retry_cnt >= rd_rew_level)
                        {   
    	                    //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry pass: do rewrite");
    	                    if(CPU_BE == CPU_ID)
    	                    {
    	                        //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry pass: cpu2 do rewrite");
    	                        cpu_msg_issue(CPU_FE - 1, CPU_MSG_RETRY_GET_LDA_REWRITE, 0, (u32)&recommit_cpu2.bm_pl);
    	                    }
    	                    else
    	                    {
    	                        //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry pass: cpu4 do rewrite");
    	                        cpu_msg_issue(CPU_FE - 1, CPU_MSG_RETRY_GET_LDA_REWRITE, 0, (u32)&recommit_cpu4.bm_pl);
    	                    }
                        }
	                }
	                #endif

                    #ifdef LJ1_WUNC
                    if (du_sts == ficu_err_par_err)
                    {
                        #ifdef RETRY_COMMIT_EVENT_TRIGGER
                        if(CPU_BE == CPU_ID)
                        {
                            recommit_cpu2.pdu_bmp = meta[dtag.b.dtag].wunc.WUNC;
                            read_recoveried_commit(&recommit_cpu2.bm_pl, recommit_cpu2.pdu_bmp);
                        }
                        else
                        {
                            recommit_cpu4.pdu_bmp = meta[dtag.b.dtag].wunc.WUNC;
                            read_recoveried_commit(&recommit_cpu4.bm_pl, recommit_cpu4.pdu_bmp);                    
                        }
                        return;
                        #else
                        read_recoveried_commit(&bm_pl, meta->wunc.WUNC);
                        //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry done: read recver cmit return");
                        #endif
                    }
                    else
                    #endif
                    {
                        #ifdef RETRY_COMMIT_EVENT_TRIGGER
                        if(CPU_BE == CPU_ID)
                        {
                            recommit_cpu2.pdu_bmp = 0;
                            read_recoveried_commit(&recommit_cpu2.bm_pl, recommit_cpu2.pdu_bmp);
                        }
                        else
                        {
                            recommit_cpu4.pdu_bmp = 0;
                            read_recoveried_commit(&recommit_cpu4.bm_pl, recommit_cpu4.pdu_bmp);                    
                        }
                        return;
                        #else
                        read_recoveried_commit(&bm_pl, 0);
                        //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry done: read recver cmit return");
                        #endif
                    }
                }
                else
                {
                    hst_rd_err_cmpl();
                }
                
                #if 0

                if (total_retry_du_cnt == 0)
                {
                    ncl_cmd_trace(LOG_INFO, 0x0f1f, "Retry End, ncl_handle_pending_cmd");
                    ncl_handle_pending_cmd();
                }

                #endif



            }
            else if((du_sts > ficu_err_par_err) && (du_sts < ficu_err_1bit_retry_err))
            {


                //shift_to_next_retry_step(du_sts);
                shift_to_next_retry_step(ent.ncl_cmd->addr_param.common_param.info_list[ent.du_offset].status);
            }
            else if(du_sts == ficu_err_1bit_retry_err)
            {
#ifdef EH_ENABLE_2BIT_RETRY  
                if(rced == true)
                {
                    //shift_to_next_retry_step(du_sts);
                    shift_to_next_retry_step(ent.ncl_cmd->addr_param.common_param.info_list[ent.du_offset].status);
                }
                else
#endif
                {
                    pdu_err_ncl_cmd.retry_cnt=0;
                    ncl_cmd_trace(LOG_ERR, 0x26f2, "pda: 0x%x, retry fail, need_retry_du_cnt : %d", ncl_cmd->addr_param.rapid_du_param.pda,need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)]);
                    //ent.ncl_cmd->addr_param.common_param.info_list[ent.du_offset].status = du_sts;
                    ent.ncl_cmd->status |= NCL_CMD_ERROR_STATUS;

                    if ((hp_retry_en == true) && (ncl_cmd->flags & NCL_CMD_RCED_FLAG))
                    {
                        hp_retry_done = true;
                    }
                    else
                    {
                        hp_retry_done = false;
                    }
                    
                    //===============================================print lda=====================================================//
                    #ifdef RD_FAIL_GET_LDA
                    if(FTL_INIT_DONE 
						#if RAID_SUPPORT_UECC
						&& (ent.ncl_cmd->uecc_type != NCL_UECC_AUX_RD)
						#endif
						)
                    {
                        if(ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
                        {
                            type = OTHER_READ_TYPE;
                        }
                        else
                        {
                            if(die_cmd_info->cmd_info.b.stream)
                                type = HOST_READ_TYPE;
                            else if(die_cmd_info->cmd_info.b.gc)
                                type = GC_READ_TYPE;
                            else
                                type = OTHER_READ_TYPE;
                        }
                        rd_err_get_lda(&bm_pl, ncl_cmd->addr_param.rapid_du_param.pda, type);
                    }
                    #endif
                    //=============================================================================================================//

                    //if (ent.ncl_cmd->op_type & DYNAMIC_DTAG)
                    //{
                    //    if((ent.du_sts == ficu_err_du_erased) || (ent.du_sts == ficu_err_dfu))  //read blank not do rc & return error to host
                    //    {
                    //        rls_du_ofst[0] = (u32)ent.bm_pl.pl.du_ofst;
                    //        rls_du_btag[0] = (u32)ent.bm_pl.pl.btag;
                    //        ttl_rls_du_cnt++;
                    //    }
                    //}
                        
					while(complete_handler_du_cnt < ent.ncl_cmd->err_du_cnt)
                    {
                        need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)]--;
                        total_retry_du_cnt--;
                        complete_handler_du_cnt++;

                        if (ent.ncl_cmd->op_type & DYNAMIC_DTAG)
                        {
                            if(ttl_rls_du_cnt != 0)
                            {
                                rls_du_ofst[ttl_rls_du_cnt] = (u32)ent.bm_pl.pl.du_ofst;
                                rls_du_btag[ttl_rls_du_cnt] = (u32)ent.bm_pl.pl.btag;
                                ttl_rls_du_cnt++;
                            }
                            else
                            {
                                if((ent.du_sts == ficu_err_du_erased) || (ent.du_sts == ficu_err_dfu))
                                {
                                    rls_du_ofst[0] = (u32)ent.bm_pl.pl.du_ofst;
                                    rls_du_btag[0] = (u32)ent.bm_pl.pl.btag;
                                    ttl_rls_du_cnt++;
                                }
                            }
                        }

                        if (complete_handler_du_cnt < ent.ncl_cmd->err_du_cnt)
                        {
                            u32 NextRetryPage;
                            NextRetryPage = (ent.ncl_cmd->addr_param.common_param.pda_list[(ent.du_offset + 1)] >> 2);
                            ncl_cmd_trace(LOG_ERR, 0x7623, "complete_handler_du_cnt: %d, current pda: 0x%x, next pda: 0x%x", complete_handler_du_cnt, ncl_cmd->addr_param.rapid_du_param.pda, ent.ncl_cmd->addr_param.common_param.pda_list[(ent.du_offset + 1)]);
                            if ((ncl_cmd->addr_param.rapid_du_param.pda >> 2) == NextRetryPage)
                            {
                                CBF_REMOVE_HEAD(&pdu_err_que);
                                CBF_HEAD(&pdu_err_que, ent);
                            }
                            else
                            {
                                break;
                            }
                        }
                    }

                    if (complete_handler_du_cnt == ent.ncl_cmd->err_du_cnt)
                    {
                        ent.ncl_cmd->retry_step = retry_end;
                        complete_handler_du_cnt = 0;
#if defined(HMETA_SIZE)
                        if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent.ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                        if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                        {
                            ent.ncl_cmd->completion(ent.ncl_cmd);
                        }
                        else
                        {
                            if (die_cmd_info->cmd_info.b.host)
        					{
                                nsid = INT_NS_ID - 1;
                            }
                            //if (fcns_raid_enabled(nsid) && is_there_uc_pda(ent.ncl_cmd) && (rced == false) && (die_cmd_info->cmd_info.b.stream))  //test
                            if (fcns_raid_enabled(nsid) && is_there_uc_pda(ent.ncl_cmd) && (rced == false))
                            {   
                                ncl_cmd_trace(LOG_ERR, 0x340b, "raid_correct_push, pda : 0x%x", ncl_cmd->addr_param.rapid_du_param.pda);
                                rc_req = rc_req_prepare(ent.ncl_cmd, true);
        						flag_put_ncl_cmd = false;
                                raid_correct_push(rc_req);
                            }

                            dfu_blank_err_chk(ent.ncl_cmd, die_cmd_info->cmd_info);  //dfu err mark ecct

                            if (die_cmd_info->cmd_info.b.gc && (!die_cmd_info->cmd_info.b.debug) && (flag_put_ncl_cmd)) 
                            {
                            	//nvm_cmd_id used for gc pda group in gc read
                            	grp = ent.ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
                            	cnt = ent.ncl_cmd->addr_param.common_param.list_len;
                                //gc_dfu_err_chk(ent.ncl_cmd);
                            	scheduler_gc_read_done(grp, ent.ncl_cmd->user_tag_list, cnt);
                            }
                           	ncl_cmd_put(ent.ncl_cmd, flag_put_ncl_cmd);
                        }
                    }

                    //if (ent.ncl_cmd->op_type & DYNAMIC_DTAG)
                    if (ttl_rls_du_cnt != 0)
                    {
                        //bm_err_commit(bm_pl.pl.du_ofst, bm_pl.pl.btag);
                        for(i = 0; i < ttl_rls_du_cnt; i++)
                        { 
                            bm_err_commit(rls_du_ofst[i], rls_du_btag[i]);
                        }
                        ttl_rls_du_cnt = 0;
                    }

                    hst_rd_err_cmpl();

                    #if 0 

                    if (total_retry_du_cnt == 0)
                    {
                        ncl_handle_pending_cmd();
                    }

                    #endif

                }
            }
#ifdef EH_ENABLE_2BIT_RETRY 
            else if((du_sts == ficu_err_2bit_retry_err) || ((du_sts == ficu_err_2bit_nard_err) && (rced == true)))
            {
                pdu_err_ncl_cmd.retry_cnt=0;
                need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)]--;
                total_retry_du_cnt--;
                complete_handler_du_cnt++;
                ncl_cmd_trace(LOG_ERR, 0x078b, "pda: 0x%x, retry fail, need_retry_du_cnt : %d", ncl_cmd->addr_param.rapid_du_param.pda,need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.rapid_du_param.pda)]);
                //ent.ncl_cmd->addr_param.common_param.info_list[ent.du_offset].status = du_sts;
                ent.ncl_cmd->status |= NCL_CMD_ERROR_STATUS;

                if ((hp_retry_en == true) && (ncl_cmd->flags & NCL_CMD_RCED_FLAG))
                {
                    hp_retry_done = true;
                }
                else
                {
                    hp_retry_done = false;
                }
                
                if (complete_handler_du_cnt ==  ent.ncl_cmd->err_du_cnt)
                {
                    ent.ncl_cmd->retry_step = retry_end;
                    complete_handler_du_cnt = 0;
#if defined(HMETA_SIZE)
                    if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent.ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                    if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                    {
                        
                        ent.ncl_cmd->completion(ent.ncl_cmd);
                    }
                }


                hst_rd_err_cmpl();

    			#if 0 
                if (total_retry_du_cnt == 0)
                {
                    ncl_handle_pending_cmd();
                }
    			#endif

            }
#endif
            else
            {
                ent.ncl_cmd->retry_step = retry_end;
                complete_handler_du_cnt = 0;
#if defined(HMETA_SIZE)
                if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG || ent.ncl_cmd->flags & NCL_CMD_RETRY_CB_FLAG)
#else
                if (ent.op_type == NCL_CMD_FW_TABLE_READ_PA_DTAG)
#endif
                {
                    ent.ncl_cmd->completion(ent.ncl_cmd);
                }

            }
        }
    }
}

#endif

ddr_code void calculate_read_err_du_cnt(struct ncl_cmd_t *ncl_cmd)
{
	u8 i;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
    ncl_cmd->err_du_cnt = 0;
    
    for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++)
    {
        #ifdef History_read
        if (info[i].status >= ficu_err_du_ovrlmt)
        #else
        if (info[i].status > ficu_err_du_ovrlmt)
        #endif
        {
            need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[i])]++;
            ncl_cmd->err_du_cnt++;
            ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
        }            
    }

    if(ncl_cmd->err_du_cnt == 0)
    {
        ncl_cmd_trace(LOG_ERR, 0x74c7, "[ERR] PDA no err but get in errHandle, ncl_cmd: 0x%x", ncl_cmd);
        sys_assert(ncl_cmd->err_du_cnt != 0);
    }
    
}

#if 0
slow_code bool is_target_die_under_retry(struct ncl_cmd_t *ncl_cmd)
{
    if (need_retry_du_cnt[pda2lun_id(ncl_cmd->addr_param.common_param.pda_list[0])] != 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}
#endif

#ifdef NCL_FW_RETRY_EX
slow_code void System_Blk_read_retry(struct ncl_cmd_t *ncl_cmd)
{
    u8 max_retry_cnt, retry_cnt=0, retry_pass_step = 0;
    nal_status_t du_sts; // =pdu_err_ncl_cmd.addr_param.rapid_du_param.info.status;
	bm_pl_t bm_pl;		///< read error bm_pl
	u8 i;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;
    
    #ifdef NCL_FW_RETRY_EX
    struct ncl_cmd_t *pdu_err_ncl_cmd;
    u8 ch_id;
    ch_id = (pda2ch(ncl_cmd->addr_param.common_param.pda_list[0]) % 4);
    #endif 
    pdu_err_ncl_cmd = (struct ncl_cmd_t *)&pdu_err_ncl_cmd_ex[ch_id];
    ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
    
    for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++)
    {
        if (info[i].status == ficu_err_du_erased)
        {
            max_retry_cnt = 1;
        }
        else
        {
            max_retry_cnt = (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) ? RR_STEP_SLC : RR_STEP_XLC;
        }
        
        if ((info[i].status == ficu_err_du_uc) || (info[i].status == ficu_err_du_erased))
        {
            memset( &retry_info_list[ch_id][0], 0x00, sizeof(retry_info_list[0]));
            pdu_err_ncl_cmd->addr_param.common_param.info_list = &retry_info_list[ch_id][0];
            du_sts =pdu_err_ncl_cmd->addr_param.common_param.info_list[0].status;
            pdu_err_ncl_cmd->addr_param.common_param.pda_list = &retry_pda_list[ch_id][0];

            if (retry_cnt > max_retry_cnt)
            {
                retry_cnt = 0;
            }
            else
            {
               retry_cnt += retry_pass_step;
            }
            
            do
            {
                //memset(&pdu_err_ncl_cmd.addr_param.rapid_du_param.info, 0, sizeof(struct info_param_t));
                retry_info_list[ch_id][0].status = 0;
                pdu_err_ncl_cmd->addr_param.common_param.list_len = 1;
            	retry_info_list[ch_id][0].pb_type = ncl_cmd->addr_param.common_param.info_list[i].pb_type;
            	retry_pda_list[ch_id][0] = ncl_cmd->addr_param.common_param.pda_list[i];
        		bm_pl = ncl_cmd->user_tag_list[i];
                //if ((under_retry_pda>>2) != (ncl_cmd->addr_param.common_param.pda_list[i]>>2))
                if ((under_retry_pda_ex[ch_id]>>2) != (ncl_cmd->addr_param.common_param.pda_list[i]>>2))
                {
                    pdu_err_ncl_cmd->retry_cnt = 0;
                    //under_retry_pda = ncl_cmd->addr_param.common_param.pda_list[i];
                    under_retry_pda_ex[ch_id] = ncl_cmd->addr_param.common_param.pda_list[i];
                }
                
            	pdu_err_ncl_cmd->caller_priv = NULL;
            	pdu_err_ncl_cmd->completion = NULL;
            	pdu_err_ncl_cmd->user_tag_list = &bm_pl;
                pdu_err_ncl_cmd->retry_cnt = retry_pass_step = retry_cnt;
                pdu_err_ncl_cmd->flags = ncl_cmd->flags;
                pdu_err_ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
				
                if (retry_cnt >= max_retry_cnt)
                {
                    pdu_err_ncl_cmd->retry_step = last_1bit_vth_step;
                }
                else if(retry_cnt == (max_retry_cnt - 1))
                {
                    pdu_err_ncl_cmd->retry_step = last_1bit_step;
                }
                else
                {
                    pdu_err_ncl_cmd->retry_step = retry_set_shift_value;
                }
                
                pdu_err_ncl_cmd->op_code = NCL_CMD_SET_GET_FEATURE;
                //ncl_cmd_read_retry(&pdu_err_ncl_cmd);
                ncl_cmd_read_retry(pdu_err_ncl_cmd);

                pdu_err_ncl_cmd->flags = ncl_cmd->flags;
                pdu_err_ncl_cmd->flags &= ~NCL_CMD_COMPLETED_FLAG;
            	pdu_err_ncl_cmd->du_format_no = ncl_cmd->du_format_no;
                pdu_err_ncl_cmd->op_code = ncl_cmd->op_code;
                pdu_err_ncl_cmd->op_type = ncl_cmd->op_type;
                pdu_err_ncl_cmd->rty_blank_flg = false;
                pdu_err_ncl_cmd->rty_dfu_flg = false;
				
                if (retry_cnt >= max_retry_cnt)
                {
                    pdu_err_ncl_cmd->retry_step = last_1bit_vth_read;
                }
                else if(retry_cnt == (max_retry_cnt - 1))
                {
                    pdu_err_ncl_cmd->retry_step = last_1bit_read;
                }
                else
                {
                    pdu_err_ncl_cmd->retry_step = retry_read;
                }
                
                //ncl_cmd_read_retry(&pdu_err_ncl_cmd);
                ncl_cmd_read_retry(pdu_err_ncl_cmd);
                du_sts = pdu_err_ncl_cmd->addr_param.common_param.info_list[0].status;
                if (du_sts == ficu_err_good)
                {
                    info[0].status = du_sts;
                }
                else if(du_sts == ficu_err_1bit_vth_retry_err)
                {
                    ncl_cmd->status |= pdu_err_ncl_cmd->status;
					ncl_cmd_trace(LOG_ERR, 0x89ff, "pda: 0x%x, retry fail, err : %d",retry_pda_list[ch_id][0], info[i].status);
                }
                
                retry_cnt++;                
            }while((retry_cnt <= max_retry_cnt) && (du_sts > ficu_err_du_ovrlmt));
        }            
    }
}
#else
slow_code void System_Blk_read_retry(struct ncl_cmd_t *ncl_cmd)
{
    u8 max_retry_cnt, retry_cnt=0, retry_pass_step = 0;
    nal_status_t du_sts =pdu_err_ncl_cmd.addr_param.rapid_du_param.info.status;
	bm_pl_t bm_pl;		///< read error bm_pl
	u8 i;
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

    ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
    
    for (i = 0; i < ncl_cmd->addr_param.common_param.list_len; i++)
    {
        if (info[i].status == ficu_err_du_erased)
        {
            max_retry_cnt = 1;
        }
        else
        {
            max_retry_cnt = (ncl_cmd->flags & NCL_CMD_SLC_PB_TYPE_FLAG) ? RR_STEP_SLC : RR_STEP_XLC;
        }
        
        if ((info[i].status == ficu_err_du_uc) || (info[i].status == ficu_err_du_erased))
        {
            if (retry_cnt > max_retry_cnt)
            {
                retry_cnt = 0;
            }
            else
            {
               retry_cnt += retry_pass_step;
            }
            
            do
            {
                memset(&pdu_err_ncl_cmd.addr_param.rapid_du_param.info, 0, sizeof(struct info_param_t));
            	pdu_err_ncl_cmd.addr_param.rapid_du_param.info.pb_type = ncl_cmd->addr_param.common_param.info_list[i].pb_type;
            	pdu_err_ncl_cmd.addr_param.rapid_du_param.pda = ncl_cmd->addr_param.common_param.pda_list[i];
        		bm_pl = ncl_cmd->user_tag_list[i];
                if ((under_retry_pda>>2) != (ncl_cmd->addr_param.common_param.pda_list[i]>>2))
                {
                    pdu_err_ncl_cmd.retry_cnt = 0;
                    under_retry_pda = ncl_cmd->addr_param.common_param.pda_list[i];
                }
                
            	pdu_err_ncl_cmd.addr_param.rapid_du_param.list_len = 1;
            	pdu_err_ncl_cmd.caller_priv = NULL;
            	pdu_err_ncl_cmd.completion = NULL;
            	pdu_err_ncl_cmd.du_user_tag_list = bm_pl;
                pdu_err_ncl_cmd.retry_cnt = retry_pass_step = retry_cnt;
                pdu_err_ncl_cmd.flags = ncl_cmd->flags;
                pdu_err_ncl_cmd.flags |= NCL_CMD_RAPID_PATH;
                pdu_err_ncl_cmd.flags &= ~NCL_CMD_COMPLETED_FLAG;
                pdu_err_ncl_cmd.rty_blank_flg = false;
                pdu_err_ncl_cmd.rty_dfu_flg = false;

                if (retry_cnt >= max_retry_cnt)
                {
                    pdu_err_ncl_cmd.retry_step = last_1bit_vth_step;
                }
                else if(retry_cnt == (max_retry_cnt - 1))
                {
                    pdu_err_ncl_cmd.retry_step = last_1bit_step;
                }
                else
                {
                    pdu_err_ncl_cmd.retry_step = retry_set_shift_value;
                }
                
                pdu_err_ncl_cmd.op_code = NCL_CMD_SET_GET_FEATURE;
                ncl_cmd_read_retry(&pdu_err_ncl_cmd);

                pdu_err_ncl_cmd.flags = ncl_cmd->flags;
                pdu_err_ncl_cmd.flags |= NCL_CMD_RAPID_PATH;
                pdu_err_ncl_cmd.flags &= ~NCL_CMD_COMPLETED_FLAG;
            	pdu_err_ncl_cmd.du_format_no = ncl_cmd->du_format_no;
                pdu_err_ncl_cmd.op_code = ncl_cmd->op_code;
                pdu_err_ncl_cmd.op_type = ncl_cmd->op_type;
                pdu_err_ncl_cmd.rty_blank_flg = false;
                pdu_err_ncl_cmd.rty_dfu_flg = false;
                
                if (retry_cnt >= max_retry_cnt)
                {
                    pdu_err_ncl_cmd.retry_step = last_1bit_vth_read;
                }
                else if(retry_cnt == (max_retry_cnt - 1))
                {
                    pdu_err_ncl_cmd.retry_step = last_1bit_read;
                }
                else
                {
                    pdu_err_ncl_cmd.retry_step = retry_read;
                }
                
                ncl_cmd_read_retry(&pdu_err_ncl_cmd);
                du_sts = pdu_err_ncl_cmd.addr_param.rapid_du_param.info.status;
                if (du_sts == ficu_err_good)
                {
                    info[0].status = du_sts;
                }
                else if(du_sts >= ficu_err_1bit_retry_err)
                {
                    ncl_cmd->status |= pdu_err_ncl_cmd.status;
                }
                
                retry_cnt++;                
            }while((retry_cnt <= max_retry_cnt) && (du_sts > ficu_err_du_ovrlmt));
        }            
    }
}
#endif

#ifdef NCL_RETRY_PASS_REWRITE
#if(CPU_BE == CPU_ID)
slow_code void evt_retry_rewrite_check(u32 parm, u32 payload, u32 sts)
{
    //struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *)payload;
    retry_rewrite_t *info = (retry_rewrite_t *) payload;
    //ncl_cmd_trace(LOG_INFO, 0, "[DBG]retry rewrite trigger evt");
    cpu_msg_isr();
    host_read_retry_add_rewrite(info);
}

init_code void host_retry_rewrite_evt_init(void)
{
    rd_rew_level = 16;  //init need retry rewrite level, default = 16 
    evt_register(evt_retry_rewrite_check, 0, &evt_retry_rewrite_pend_out);
}

share_data volatile u8 host_sec_bitz;
//fast_code bool host_read_retry_add_rewrite(bm_pl_t *bm_pl_list, u32 pda_cnt)
//fast_code bool host_read_retry_add_rewrite(struct ncl_cmd_t *ncl_cmd, u8 type)
slow_code void host_read_retry_add_rewrite(retry_rewrite_t *info)
{
	//u32 padding_cnt = 0;
    cur_wr_pl_t *cur_wr_pl = NULL;
    u32 rew_curr_ptr = info->flag;
    u32 rew_pre_ptr;
    //==================================get pda=================================//
    #if 0
    u8 win = 0;
    u64 addr;
    mc_ctrl_reg0_t ctrl0 = { .all = readl((void *)(DDR_TOP_BASE + MC_CTRL_REG0))};
    #endif
    pda_t pda;
    //==========================================================================//

    if(rew_curr_ptr == 0)
        rew_pre_ptr = DDR_RD_REWRITE_TTL_CNT;
    else
        rew_pre_ptr = rew_curr_ptr - 1;
    info->flag = 0xA5A5; 
    if((rd_rew_info[rew_pre_ptr].flag != 0xA5A5) && (rd_rew_info[rew_pre_ptr].flag != 0x5A5A))   //0xA5A5: through host_read_retry_add_rewrite, 0x5A5A: init state
    {
        ncl_cmd_trace(LOG_INFO, 0xe3bb, "[REW] pre lda[0x%x] ptr[%d] not rewrite", rd_rew_info[rew_pre_ptr].lda, rew_pre_ptr);
        rd_rew_info[rew_pre_ptr].flag = 0x5A5A;
        rd_rew_info[rew_pre_ptr].lda = INV_U32;
    }

    sys_assert(info->lda != INV_LDA);

    bool ns_dirty = ftl_core_open(HOST_NS_ID);
    //if ((ns_dirty == false) || ftl_core_ns_padding(HOST_NS_ID))
    if (ns_dirty == false) 
    {
        //ftl_core_set_wreq_pend(1, FTL_CORE_GC, true);
        evt_set_cs(evt_retry_rewrite_pend_out, (u32)info, 0, CS_TASK);
        return;
    }

    if(ftl_core_gc_cache_search(info->lda))   //cache hit
    {
        ncl_cmd_trace(LOG_INFO, 0x0ddf, "[REW] hit cache lda: 0x%x",info->lda);
        info->flag = 0x5A5A;
        info->lda = INV_U32;    
        return;
    }

    //==================================get pda=================================//
    #if 0
    addr = (ddtag2off(shr_l2p_entry_start) | 0x40000000) + ((u64)info->lda * 4);
    while (addr >= 0xC0000000) 
    {
        addr -= 0x80000000;
        win++;
    }
    u8 old = ctrl0.b.cpu3_ddr_window_sel;
    ctrl0.b.cpu3_ddr_window_sel = win;
    isb();
    writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));   
    pda = *((u32*)(u32)addr);
    ctrl0.b.cpu3_ddr_window_sel = old;
    writel(ctrl0.all, (void *)(DDR_TOP_BASE + MC_CTRL_REG0));
    isb();

    #else
    pda = LDA_to_PDA(info->lda);
    #endif
    info->pda = pda;
    //==========================================================================//
 
    if (cur_wr_pl == NULL) 
    {
        //cur_wr_pl = ftl_core_get_wpl_mix_mode(HOST_NS_ID, FTL_CORE_NRM, WPL_V_MODE, true, NULL);
        cur_wr_pl = ftl_core_get_wpl_mix_mode(HOST_NS_ID, FTL_CORE_GC, WPL_V_MODE, true, NULL, NULL);
        if (cur_wr_pl == NULL) 
        {   
            evt_set_cs(evt_retry_rewrite_pend_out, (u32)info, 0, CS_TASK);
            return;
        }
    }
    ftl_core_host_data_in(HOST_NS_ID, FTL_CORE_GC, true);
    ncl_cmd_trace(LOG_INFO, 0x9453, "[REW] cur_wr_pl: %d, lda:0x%x, pda 0x%x, dtag: 0x%x, ptr: %d", cur_wr_pl->cnt, info->lda, pda, info->bm_pl.pl.dtag, rew_curr_ptr);
    //ftl_core_gc_data_in_update();
    cur_wr_pl->lda[cur_wr_pl->cnt] = info->lda;
    cur_wr_pl->pl[cur_wr_pl->cnt].all = info->bm_pl.all;
    cur_wr_pl->cnt++;
    
    //sys_assert(cur_wr_pl->cnt != 0);
	if (cur_wr_pl->cnt == cur_wr_pl->max_cnt)  
    //if (cur_wr_pl->cnt == ftl_core_next_write(1, FTL_CORE_NRM))
    {   
    	ftl_core_submit(cur_wr_pl, NULL, ftl_core_gc_wr_done);
        cur_wr_pl = NULL;
    }
    return;
}

slow_code void ipc_host_read_retry_add_rewrite(volatile cpu_msg_req_t *req)
{
    //struct ncl_cmd_t * ncl_cmd = (struct ncl_cmd_t *) req->pl;
    retry_rewrite_t *info = (retry_rewrite_t *) req->pl;
    host_read_retry_add_rewrite(info);
    return;
}

#if 1
ddr_code int set_rd_rewrite_level_console(int argc, char *argv[])
{
	if (argc >= 2) 
    {
        u8 cur_rew_level = strtol(argv[1], (void *)0, 10);
        u8 pre_rew_level = rd_rew_level;
        if(cur_rew_level >= 31)
        {
            ncl_cmd_trace(LOG_INFO, 0x7075, "[REW] rewrite retry level overlimit");
            return 0;
        }
        rd_rew_level = cur_rew_level;
        ncl_cmd_trace(LOG_INFO, 0x4ec2, "[REW] rewrite retry pass pre level: %d", pre_rew_level);
        ncl_cmd_trace(LOG_INFO, 0xbef5, "[REW] rewrite retry pass curr level: %d", rd_rew_level);
	} 
    else 
    {
		ncl_cmd_trace(LOG_ERR, 0xab45, "\nInvalid number of argument\n");
	}
    return 0;
}

static DEFINE_UART_CMD(set_rew_level, "set_rew_level",
		"set_rew_level [retry_level]",
		"Rewrite level is Decimal",
		1, 1, set_rd_rewrite_level_console);

#endif

#endif
#endif
#endif

ddr_code void vth_queue_handler(){
    u8 id=0;
    if(!vth_cs_flag){
        if(!CBF_EMPTY(&hp_vth_que)){
            CBF_HEAD(&hp_vth_que, id);
            CBF_REMOVE_HEAD(&hp_vth_que);
            
            vth_cs_flag=1;
            ncl_vth_tracking(pdu_err_ncl_cmd_ex[id].addr_param.common_param.pda_list);
        }
        else if(!CBF_EMPTY(&pdu_vth_que)){
            CBF_HEAD(&pdu_vth_que, id);
            CBF_REMOVE_HEAD(&pdu_vth_que);
            
            vth_cs_flag=1;
            ncl_vth_tracking(pdu_err_ncl_cmd_ex[id].addr_param.common_param.pda_list);
        }
    }   
}

ddr_code void buffer_cacu(struct ncl_cmd_t *ncl_cmd){
    histogram[1]=(u16)((bit1cnt[0]>= bit1cnt[1]) ? (bit1cnt[0]- bit1cnt[1]) : (bit1cnt[1]- bit1cnt[0]));
    histogram[2]=(u16)((bit1cnt[0]>= bit1cnt[2]) ? (bit1cnt[0]- bit1cnt[2]) : (bit1cnt[2]- bit1cnt[0]));
    if(histogram[1]<=histogram[2])
        (ncl_cmd->flag_vtt)=0;
    else (ncl_cmd->flag_vtt)=1;
}

ddr_code void caca_third_buffer(struct ncl_cmd_t *ncl_cmd){
    if(ncl_cmd->flag_vtt){
         histogram[0] = histogram[1];
         histogram[1] = histogram[2];
         histogram[2] =(u16)((bit1cnt[3]>= bit1cnt[2]) ? (bit1cnt[3]- bit1cnt[2]) : (bit1cnt[2]- bit1cnt[3]));
    }
    else{
         histogram[0]=(u16)((bit1cnt[3]>= bit1cnt[1]) ? (bit1cnt[3]- bit1cnt[1]) : (bit1cnt[1]- bit1cnt[3]));
    }
    
    #if vth_print
        int i;
        ncl_console_trace(LOG_ALW, 0xe5fd, "flag = %d",ncl_cmd->flag_vtt);
        for(i=0;i<3;i++){
            ncl_console_trace(LOG_ALW, 0x1c9a, "histogram[%d] = %d",i,histogram[i]);
        }
    #endif
}

ddr_code u8 print_vtt_rl(struct ncl_cmd_t *ncl_cmd){
    u8 Vmin=0;
    u8 readlevel=0;

    if(ncl_cmd->flag_vtt){
         Vmin=ncl_cmd->shift_value + (vth_tick/2);
         
         if(histogram[2]>histogram[1]){
                if(histogram[0]>=histogram[2]){
                    readlevel =  0xFF&(((u16)Vmin) + ((vth_rounding+(((vth_tick/2)*vth_ratio*(histogram[0]-histogram[2]))/(histogram[0]-histogram[1]+histogram[2]-histogram[1]))))/vth_ratio);
                }
                else{
                    readlevel =  0xFF&(((u16)Vmin) - ((vth_rounding+(((vth_tick/2)*vth_ratio*(histogram[2]-histogram[0]))/(histogram[0]-histogram[1]+histogram[2]-histogram[1]))))/vth_ratio);
                }
         }
         else{//valley not found, edge condition
                if(histogram[1]+histogram[2]>0){
                    readlevel= 0xFF&((u16)Vmin+(((vth_ratio*vth_tick*histogram[1]/(histogram[1]+histogram[2]))+vth_rounding)/vth_ratio));
                }
                else{//both zero
                    readlevel=Vmin+(vth_tick/2);
                }
         }
    }
    else{
         Vmin=ncl_cmd->shift_value + (256-(vth_tick/2));
         
         if(histogram[0]>histogram[1]){
                if(histogram[0]>=histogram[2]){
                    readlevel =  0xFF&(((u16)Vmin) + ((vth_rounding+(((vth_tick/2)*vth_ratio*(histogram[0]-histogram[2]))/(histogram[0]-histogram[1]+histogram[2]-histogram[1]))))/vth_ratio);
                }
                else{
                    readlevel =  0xFF&(((u16)Vmin) - ((vth_rounding+(((vth_tick/2)*vth_ratio*(histogram[2]-histogram[0]))/(histogram[0]-histogram[1]+histogram[2]-histogram[1]))))/vth_ratio);
                }
         }
         else{//valley not found, edge condition
                if(histogram[0]+histogram[1]>0){
                    readlevel=0xFF&((u16)Vmin-(((vth_ratio*vth_tick*histogram[1]/(histogram[0]+histogram[1]))+vth_rounding)/vth_ratio));
                }
                else{//both zero
                    readlevel=Vmin-(vth_tick/2);
                }
         }
    }
    return readlevel;
}

ddr_code void reset_feature(pda_t *pda, void (*completion)(struct ncl_cmd_t *)){
    u16 fa;

    switch(pda2plane(*pda)){
        case 0:
            fa = Retry_feature_addr[pda2pg_idx(*pda)];
            break;
        case 1:
            fa = (Retry_feature_addr[pda2pg_idx(*pda)] - 4);
            break;
        case 2:
            fa = (Retry_feature_addr[pda2pg_idx(*pda)] +0x20);
            break;
        case 3:
            fa = (Retry_feature_addr[pda2pg_idx(*pda)] +0x20-4);
            break;
        default:
            fa = Retry_feature_addr[pda2pg_idx(*pda)];
        break;
    }
    
    ncl_cmd_sf.addr_param.rw_raw_param.list_len=1;
    ncl_cmd_sf.addr_param.rw_raw_param.pda_list=pda;
    ncl_cmd_sf.addr_param.rw_raw_param.info_list=&Vth_sf_info;
    ncl_cmd_sf.status=0;
    ncl_cmd_sf.flags=0;
    ncl_cmd_sf.flags|=NCL_CMD_FEATURE_LUN_FLAG;
    ncl_cmd_sf.addr_param.rw_raw_param.column[0].column=fa;
    ncl_cmd_sf.sf_val=0;
    ncl_cmd_sf.completion = completion;
    ncl_cmd_sf.op_code = NCL_CMD_SET_GET_FEATURE;

    ncl_cmd_submit(&ncl_cmd_sf);
}

ddr_code void close_tracking(struct ncl_cmd_t *ncl_cmd){
    extern volatile u8 plp_trigger;

    //detect plp_trigger
    if(plp_trigger)
    {     
        if(vth_cs_flag)
        {
            pda_t pda = ncl_cmd_sf.addr_param.rw_raw_param.pda_list[0];
            read_error_state_ex[(pda2ch(pda)%4)] = RD_VTH_DONE;
            vth_cs_flag = 0;
            handle_read_retry_done(&pdu_err_ncl_cmd_ex[(pda2ch(pda) % 4)]);
            return;
        }
    }
    
    u8 shift_value=0;
    pda_t *pda_ptr;
    pda_ptr=ncl_cmd_sf.addr_param.rw_raw_param.pda_list;
    
    if(ncl_cmd_sf.read_level != SSR_END){
        dtag_t dtag;
        dtag.dtag=bm_pl_ssr.pl.dtag;
        dtag_put(DTAG_T_SRAM, dtag);
    }
    
    switch (ncl_cmd_sf.tracking_step){
        case SF_tracking_ini:
            bit1cnt[0]=ncl_cmd->raw_1bit_cnt;//Get 1 cnt from FICU SQ interrupt.
            ncl_cmd_sf.tracking_step = SF_tracking_left;
            shift_value=ncl_cmd_sf.shift_value-vth_tick;
            #if vth_print
            ncl_console_trace(LOG_ALW, 0x124f, "SF_tracking_left ncl_cmd_sf.shift_value-tick = %d",shift_value);
            #endif
            set_feature_by_tracking(pda_ptr, shift_value);
            break;
        case SF_tracking_left:
            bit1cnt[1]=ncl_cmd->raw_1bit_cnt;
            ncl_cmd_sf.tracking_step = SF_tracking_right;
            shift_value=ncl_cmd_sf.shift_value+vth_tick;
            #if vth_print
            ncl_console_trace(LOG_ALW, 0x38f0, "SF_tracking_right ncl_cmd_sf.shift_value+tick = %d",shift_value);
            #endif
            set_feature_by_tracking(pda_ptr, shift_value);
            break;
        case SF_tracking_right:
            bit1cnt[2]=ncl_cmd->raw_1bit_cnt;
            buffer_cacu(&ncl_cmd_sf);   //Determine the direction of the valley, 1:Positive direction 0:Negative direction
            ncl_cmd_sf.tracking_step = SF_tracking_flag;
                if(ncl_cmd_sf.flag_vtt)
                    shift_value=ncl_cmd_sf.shift_value+2*vth_tick;
                else
                    shift_value=ncl_cmd_sf.shift_value-2*vth_tick;
            #if vth_print
            ncl_console_trace(LOG_ALW, 0x33c2, "ncl_cmd_sf.flag_vtt = %d",ncl_cmd_sf.flag_vtt);
            ncl_console_trace(LOG_ALW, 0x90c9, "SF_tracking_flag ncl_cmd_sf.shift_value= %d",shift_value);
            #endif
            set_feature_by_tracking(pda_ptr, shift_value);
            break;
        case SF_tracking_flag:
            bit1cnt[3]=ncl_cmd->raw_1bit_cnt;
            ncl_cmd_sf.tracking_step = CaCu_tracking;
            caca_third_buffer(&ncl_cmd_sf);
                if((ncl_cmd_sf.read_level==SSR_AR)||(ncl_cmd_sf.read_level==SSR_BR)||(ncl_cmd_sf.read_level==SSR_DR))
                    vth_value[0]=print_vtt_rl(&ncl_cmd_sf);
                else if((ncl_cmd_sf.read_level==SSR_CR)||(ncl_cmd_sf.read_level==SSR_ER))
                    vth_value[1]=print_vtt_rl(&ncl_cmd_sf);
                else if((ncl_cmd_sf.read_level==SSR_FR)||(ncl_cmd_sf.read_level==SSR_GR))
                    vth_value[2]=print_vtt_rl(&ncl_cmd_sf);
            //change to another read level
            switch(ncl_cmd_sf.read_level){
                case SSR_AR:
                    ncl_cmd_sf.read_level=SSR_CR;
                    ncl_cmd_sf.tracking_step = SF_tracking_ini;
                    set_feature_by_tracking(pda_ptr, ncl_cmd_sf.shift_value);
                    break;
                case SSR_CR:
                    ncl_cmd_sf.read_level=SSR_FR;
                    ncl_cmd_sf.tracking_step = SF_tracking_ini;
                    ncl_cmd_sf.shift_value = 240;   //change vref to 0xF0
                    set_feature_by_tracking(pda_ptr, ncl_cmd_sf.shift_value);
                    break;
                case SSR_BR:
                    ncl_cmd_sf.read_level=SSR_ER;
                    ncl_cmd_sf.tracking_step = SF_tracking_ini;
                    set_feature_by_tracking(pda_ptr, ncl_cmd_sf.shift_value);
                    break;
                case SSR_ER:
                    ncl_cmd_sf.read_level=SSR_GR;
                    ncl_cmd_sf.tracking_step = SF_tracking_ini;
                    ncl_cmd_sf.shift_value = 240;   //change vref to 0xF0
                    set_feature_by_tracking(pda_ptr, ncl_cmd_sf.shift_value);
                    break;
            }
            break;
	}
    if(((ncl_cmd_sf.read_level == SSR_DR) || (ncl_cmd_sf.read_level==SSR_FR) || (ncl_cmd_sf.read_level==SSR_GR))&&(ncl_cmd_sf.tracking_step == CaCu_tracking)){
        ncl_cmd_sf.read_level = SSR_END;
        reset_feature(pda_ptr, close_tracking);
    }
    else if((ncl_cmd_sf.read_level == SSR_END)&&(ncl_cmd_sf.tracking_step == CaCu_tracking)){
            
        #if vth_print
        ncl_console_trace(LOG_ALW, 0x9004, "vth_value[0]=%d",vth_value[0]);
        ncl_console_trace(LOG_ALW, 0x8ca7, "vth_value[1]=%d",vth_value[1]);
        ncl_console_trace(LOG_ALW, 0x85e8, "vth_value[2]=%d",vth_value[2]);
        #endif
        
        pda_t pda=ncl_cmd_sf.addr_param.rw_raw_param.pda_list[0];
        u8 interleave = (pda >> nand_info.pda_interleave_shift) & (nand_info.interleave - 1); //Interleave 0~127
        
        switch ((pda2page(pda)%3)){
        case 0:
            current_shift_value[interleave][0]=(u32)(vth_value[0]<<16);
            break;
        case 1:
            current_shift_value[interleave][0]=(u32)((vth_value[2]<<24)|(vth_value[1]<<8)|(vth_value[0]));
            break;
        case 2:
            current_shift_value[interleave][1]=(u32)((vth_value[2]<<16)|(vth_value[1]<<8)|(vth_value[0]));
            break;
        }
        if(vth_cs_flag){
            read_error_state_ex[(pda2ch(pda)%4)]=RD_VTH_DONE;
            vth_cs_flag=0;
            handle_read_retry_done(&pdu_err_ncl_cmd_ex[(pda2ch(pda) % 4)]);
        }

    }
}

ddr_code void pda_ssr_read_by_tracking(struct ncl_cmd_t *ncl_cmd){
    extern volatile u8 plp_trigger;

    //detect plp_trigger
    if(plp_trigger)
    {      
        if(vth_cs_flag)
        {
            pda_t pda = ncl_cmd_sf.addr_param.rw_raw_param.pda_list[0];
            read_error_state_ex[(pda2ch(pda)%4)] = RD_VTH_DONE;
            vth_cs_flag = 0;
            handle_read_retry_done(&pdu_err_ncl_cmd_ex[(pda2ch(pda) % 4)]);
            return;
        }
    }
    
    u32 *mem = NULL;
	dtag_t dtag;
	dtag = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (mem == NULL) {
		ncl_console_trace(LOG_WARNING, 0xb90e, "No dtag");
		return;
	}

    //bm_pl settings
    bm_pl_ssr.all = 0;
    bm_pl_ssr.pl.dtag = dtag.b.dtag;
    bm_pl_ssr.pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG;
    bm_pl_ssr.pl.nvm_cmd_id = 0;

    //ncl_cmd settings
    ncl_cmd_ssr.op_code = NCL_CMD_OP_SSR_READ;
    ncl_cmd_ssr.op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    ncl_cmd_ssr.flags =  NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
    ncl_cmd_ssr.du_format_no = DU_FMT_USER_4K;
    ncl_cmd_ssr.addr_param.common_param.list_len = 1;
	ncl_cmd_ssr.addr_param.common_param.pda_list = ncl_cmd->addr_param.rw_raw_param.pda_list;

    //SSR setting and record tracking step
    ncl_cmd_ssr.read_level=ncl_cmd->read_level;
    ncl_cmd_ssr.tracking_step=ncl_cmd->tracking_step;
  
    ncl_cmd_ssr.user_tag_list = &bm_pl_ssr;
    ncl_cmd_ssr.completion = close_tracking;

    ncl_cmd_submit(&ncl_cmd_ssr); 
}

ddr_code void set_feature_by_tracking(pda_t *pda, u8 D0){
    u16 fa;
    
    switch(pda2plane(*pda)){
        case 0:
            fa = Retry_feature_addr[pda2pg_idx(*pda)];
            break;
        case 1:
            fa = (Retry_feature_addr[pda2pg_idx(*pda)] - 4);
            break;
        case 2:
            fa = (Retry_feature_addr[pda2pg_idx(*pda)] +0x20);
            break;
        case 3:
            fa = (Retry_feature_addr[pda2pg_idx(*pda)] +0x20-4);
            break;
        default:
            fa = Retry_feature_addr[pda2pg_idx(*pda)];
        break;
    }
    
    ncl_cmd_sf.addr_param.rw_raw_param.list_len=1;
    ncl_cmd_sf.addr_param.rw_raw_param.pda_list=pda;
    ncl_cmd_sf.addr_param.rw_raw_param.info_list=&Vth_sf_info;
    ncl_cmd_sf.status=0;
    ncl_cmd_sf.flags=0;
    ncl_cmd_sf.flags|=NCL_CMD_FEATURE_LUN_FLAG;
    ncl_cmd_sf.addr_param.rw_raw_param.column[0].column=fa;
    ncl_cmd_sf.sf_val=(u32)((D0<<24)|(D0<<16)|(D0<<8)|(D0));
    ncl_cmd_sf.completion =  pda_ssr_read_by_tracking;
    ncl_cmd_sf.op_code = NCL_CMD_SET_GET_FEATURE;

    ncl_cmd_submit(&ncl_cmd_sf);
}

ddr_code void ncl_vth_tracking(pda_t *pda){
    ncl_cmd_trace(LOG_ALW, 0xa32d, "Start vth tracking, pda = 0x%x", *pda);

    //Init all parameters 
    memset(histogram,0, sizeof(histogram));
    memset(vth_value,0, sizeof(vth_value));
    memset(bit1cnt,0, sizeof(bit1cnt));

    ncl_cmd_sf.shift_value=248;

    switch ((pda2page(*pda)%3)){
    case 0:
        ncl_cmd_sf.tracking_step = SF_tracking_ini;
        ncl_cmd_sf.read_level = SSR_DR;
        set_feature_by_tracking(pda, ncl_cmd_sf.shift_value);
        break;
    case 1:
        ncl_cmd_sf.tracking_step = SF_tracking_ini;
        ncl_cmd_sf.read_level = SSR_AR;
        set_feature_by_tracking(pda, ncl_cmd_sf.shift_value);
        break;
    case 2:
        ncl_cmd_sf.tracking_step = SF_tracking_ini;
        ncl_cmd_sf.read_level = SSR_BR;
        set_feature_by_tracking(pda, ncl_cmd_sf.shift_value);
        break;
    default:
        break;
	}
}
