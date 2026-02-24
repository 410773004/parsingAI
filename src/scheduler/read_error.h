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

/*! \file read_error.h
 * @brief scheduler module, handle read error PDA
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#pragma once

#include "cbf.h"
#include "dtag.h"
#include "ncl.h"
#include "eccu.h"

#ifdef NCL_FW_RETRY_EX
#define RTY_DTAG_CNT_PER_CH 8
#define RTY_QUEUE_CH_CNT 4
#if CPU_ID == CPU_BE
#define DDR_RTY_RECOVERY_EX_START DDR_RD_RECOVERY_EX_START
#else
#define DDR_RTY_RECOVERY_EX_START DDR_RD_RECOVERY_EX_START+(DDR_RD_RECOVERY_EX_CNT/2)
#endif

#else
#if CPU_ID == CPU_BE_LITE
#define DDR_RD_RECOVERY	DDR_RD_RECOVERY_1
#else
#define DDR_RD_RECOVERY	DDR_RD_RECOVERY_0
#endif
#endif

typedef enum {
	RD_ERR_IDLE = 0,	///< no error pda
	RD_RECOVERY_START,	///< error pda is recoverying
	RD_FE_ISSUING,		///< error pda was recoveried, and sent to host
	RD_VTH_START,
	RD_VTH_DONE,
	RD_SET_CMD_BY_EVT_TRIG,   //pdu_ncl_cmd will handled by event handler
	RD_RECOVERY_END		///< error pda was recoveried done
} read_error_state_t;

typedef struct _rd_err_ent_t {
	pda_t pda;		///< read error pda
	bm_pl_t bm_pl;		///< read error bm_pl
	bm_pl_t bm_pl_list[DU_CNT_PER_PAGE];		///< read error bm_pl
	pda_t retry_pda_list[DU_CNT_PER_PAGE];
    //struct info_param_t retry_info_list[DU_CNT_PER_PAGE];
	u8 pb_type;		///< read error pda type
	u8 op_type;
    //u8 du_offset;
    u8 err_du_offset[DU_CNT_PER_PAGE];
    u8 need_retry_du_cnt;
    u8 complete_retry_du_cnt;
	nal_status_t du_sts;	///< origin read error
	struct ncl_cmd_t *ncl_cmd;
} rd_err_ent_t;

enum
{
    DEFAULT_READ_TYPE = 0,
    HOST_READ_TYPE,
    GC_READ_TYPE,
    OTHER_READ_TYPE,
    MAX_READ_TYPE,
};


typedef struct{
	u16 btag;
	u16 du_ofst;
	pda_t pda;
} rd_get_lda_t;

#ifdef History_read

typedef struct
{
    //LUN_CNT 32
    //GROUP_CNT 56 // WL/8
    //PAGE_CNT 3
	u8 shift_index[32][56][3];
} hist_tab_offset_t;
#endif

extern u8 evt_errhandle_mon;
extern rd_err_ent_t current_retry_entry[RTY_QUEUE_CH_CNT];
extern bm_pl_t retry_bm_pl_list[RTY_QUEUE_CH_CNT][DU_CNT_PER_PAGE];

typedef CBF(u8, 4) vth_que_t;
#ifdef NCL_FW_RETRY_EX
typedef CBF(rd_err_ent_t, 64) rd_err_que_t;
//typedef CBF(rd_err_ent_t, 512) rd_err_que_t;
void hst_rd_err_cmpl(u8 ch_id);
dtag_t rty_get_dtag(u8 ch_id);
void ent_ptr_accumulate(u8 *ptr, u16 max_cnt);
#else
typedef CBF(rd_err_ent_t, 1024) rd_err_que_t;
void hst_rd_err_cmpl(void);
#endif

extern void read_recoveried_commit(bm_pl_t *bm_pl, u16 pdu_bmp);

/*!
 * @brief re-initialized read error resource queue
 *
 * @return	not used
 */
void read_error_resume(void);
/*!
 * @brief initialization of read error ipc and event
 *
 * @return	not used
 */
void read_error_init(void);

/*!
 * @brief handle function for read error ncl command
 *
 * @param ncl_cmd	pointer to ncl command
 * @param gc		if read from GC
 *
 * @return		not used
 */
void rd_err_handling(struct ncl_cmd_t *ncl_cmd);

void AplReadRetry_AbortJob(u32 p0, u32 p1, u32 p2);

void rd_err_ncl_cmd_submit_trigger(u32 parm, u32 payload, u32 sts);

void AplReadRetry_AbortRa(struct ncl_cmd_t *ncl_cmd);

void rd_err_get_lda_init(void);
void rd_err_get_lda(bm_pl_t *bm_pl, pda_t pda ,u8 type);
void rd_err_ptr2next(u16 *ptr, u16 max_cnt);

/*! @} */
