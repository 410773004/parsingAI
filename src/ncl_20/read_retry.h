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
#pragma once
#include "read_error.h"
#include "ncl.h"

void handle_read_retry_done(struct ncl_cmd_t *ncl_cmd);
void ard_handle_read_retry_done(struct ncl_cmd_t *ncl_cmd);
void read_retry_handling(rd_err_ent_t *ent);
void ard_read_retry_handling(rd_err_ent_t *ent);
void calculate_read_err_du_cnt(struct ncl_cmd_t *ncl_cmd);
//bool is_target_die_under_retry(struct ncl_cmd_t *ncl_cmd);
void System_Blk_read_retry(struct ncl_cmd_t *ncl_cmd);
extern  u8 need_retry_du_cnt[128];
extern  u32 current_shift_value[128][2];
#ifdef NCL_FW_RETRY_EX
extern  u32 under_retry_pda_ex[RTY_QUEUE_CH_CNT];
extern  u8 complete_du_cnt_ex[RTY_QUEUE_CH_CNT];
#else
extern  u32 under_retry_pda;
extern volatile u8 complete_handler_du_cnt;
#endif
extern volatile u16 total_retry_du_cnt;

#ifdef NCL_RETRY_PASS_REWRITE
#if 0
#if (CPU_ID == CPU_BE_LITE)
#define DDR_RD_REWRITE_START	DDR_RD_REWRITE_START_1
#else
#define DDR_RD_REWRITE_START	DDR_RD_REWRITE_START_0
#endif
//#define DDR_RD_REWRITE_MAX_CNT  DDR_RD_REWRITE_TTL_CNT
#endif

typedef struct
{
    bm_pl_t bm_pl;
    lda_t lda;
    u32 flag;  //read rewrite array is using flag    
    pda_t pda;
}retry_rewrite_t;

void evt_retry_rewrite_check(u32 parm, u32 payload, u32 sts);
void host_retry_rewrite_evt_init(void);
//bool host_read_retry_add_rewrite(struct ncl_cmd_t *ncl_cmd, u8 type);
void host_read_retry_add_rewrite(retry_rewrite_t *info);
void ipc_host_read_retry_add_rewrite(volatile cpu_msg_req_t *req);

#endif
//==============do retry rewrite type===============//
//#define REWRITE_RETRY_TYPE  0
//#define REWRITE_RAID_TYPE   1
//==================================================//
