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
/*! \file
 * @brief HMB command operations
 *
 * \addtogroup decoder
 * \defgroup hmb_cmd HMB command
 * \ingroup decoder
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "hmb.h"
#include "hmb_cmd.h"
#include "assert.h"
#include "misc.h"

/*! \cond PRIVATE */
#define __FILEID__ hmbc
#include "trace.h"
/*! \endcond */

#if (HOST_NVME_FEATURE_HMB == FEATURE_SUPPORTED)

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define HMB_MAX_LKP_NUM		33

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

/*!
 * @brief definition of fused command type
 */
enum {
	fused_single = 0,	///< single command
	fused_cmd1 = 1,		///< fused first command
	fused_cmd2 = 3		///< fused second command
};

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------


/*!
 * @brief check if hit HMB fmt2 64 dword boundary
 *
 * @param lda		start lda
 * @param cnt		count
 *
 * @return		return count when hit boundary
 */
static fast_code u32 hmb_cmd_fmt2_boundary_chk(u32 lda, u32 cnt)
{
	u32 e = (lda + cnt - 1) >> HMB_CMD_FMT2_BOUNDARY_SFT;
	u32 s = lda >> HMB_CMD_FMT2_BOUNDARY_SFT;

	if (s == e)
		return cnt;

	return (e << HMB_CMD_FMT2_BOUNDARY_SFT) - lda;
}

fast_code bool hmb_tbl_write(u32 nsid, void *buf, u32 tbl_id, hmb_cmd_cb_t *cb)
{
	hmb_tbl_write_cmd_t *cmd;

	cmd = (hmb_tbl_write_cmd_t *) hmb_get_next_cmd(cb, 0);

	if (cmd == NULL)
		return false;

	cb->tid = jiffies;
	cb->nsid = nsid;
	cmd->nsid = nsid;
	cmd->cmd_code = HMB_CMD_TABLE_WRITE;
	cmd->starting_lda = tbl_id << 10;
	cmd->tu_cnt = 1;
	cmd->starting_sram_addr = sram_to_dma(buf);

	hmb_cmd_submit(1);
	return true;
}

fast_code bool hmb_tbl_read(u32 nsid, void *buf, u32 tbl_id, hmb_cmd_cb_t *cb)
{
	hmb_tbl_read_cmd_t *cmd;

	cmd = (hmb_tbl_read_cmd_t *) hmb_get_next_cmd(cb, 0);

	if (cmd == NULL)
		return false;

	cb->tid = jiffies;
	cb->nsid = nsid;
	cmd->nsid = nsid;
	cmd->cmd_code = HMB_CMD_TABLE_READ;
	cmd->starting_lda = tbl_id << 10;
	cmd->tu_cnt = 1;
	cmd->starting_sram_addr = sram_to_dma(buf);

	hmb_cmd_submit(1);
	return true;
}

fast_code bool hmb_lkp_fmt1(u32 nsid, u32 cnt, u32 lda0, u32 lda1,
		hmb_cmd_cb_t *cb)
{
	hmb_tbl_lkp_fmt1_t *cmd;

	cmd = (hmb_tbl_lkp_fmt1_t *) hmb_get_next_cmd(cb, 0);

	if (cmd == NULL)
		return false;

	cb->tid = jiffies;
	cb->nsid = nsid;
	cmd->nsid = nsid;
	cmd->cmd_code = HMB_CMD_LOOKUP_FMT1;
	cmd->lda_cnt = cnt;
	cmd->lda[0] = lda0;
	cmd->lda[1] = lda1;

	hmb_cmd_submit(1);
	return true;
}

static fast_code bool _hmb_lkp_fmt2(u32 nsid, u32 start_lda, u32 cnt, hmb_cmd_cb_t *cb)
{
	hmb_tbl_lkp_fmt2_t *cmd;

	cmd = (hmb_tbl_lkp_fmt2_t *) hmb_get_next_cmd(cb, 0);

	if (cmd == NULL)
		return false;

	if (cnt > HMB_CMD_LKP_FMT2_MAX_CNT)
		hmb_hal_trace(LOG_ERR, 0x6443, "no support %d lda lookup", cnt);

	cb->tid = jiffies;
	cb->nsid = nsid;
	cmd->nsid = nsid;
	cmd->cmd_code = HMB_CMD_LOOKUP_FMT2;
	cmd->starting_lda = start_lda;
	cmd->lda_cnt = cnt;

	hmb_cmd_submit(1);
	return true;
}

static fast_code void hmb_lkp_fmt2_done(hmb_cmd_cb_t *cb)
{
	u32 rest_cnt = cb->rst.lkp_fmt2.rest_cnt;

	if (rest_cnt) {
		u32 next_slda = cb->rst.lkp_fmt2.next_slda;

		// lookup not done, issue extra lookup cmd
		cb->cmpl = cb->rst.lkp_fmt2.cmpl_ori;
		cb->rst.lkp_fmt2.pda += cb->rst.lkp_fmt2.lookup_cnt;

		hmb_lkp_fmt2(cb->nsid, next_slda, rest_cnt, cb);
	} else {
		// free cb
		hmb_cmd_put_cb_ctx(cb, HMB_RESP_CNT_LKUP2);
	}
}

fast_code bool hmb_lkp_fmt2(u32 nsid, u32 start_lda, u32 lkp_cnt, hmb_cmd_cb_t *cb)
{
	hmb_cmd_cb_t *next_cb;
	u32 total_cnt;
	u32 cnt;
	u32 *pda;
	void *caller;
	void (*cmpl)(hmb_cmd_cb_t *);

	total_cnt = cb->rst.lkp_fmt2.total_cnt;
	caller = cb->caller;
	cmpl = cb->cmpl;
	pda = cb->rst.lkp_fmt2.pda;

	sys_assert(lkp_cnt <= HMB_MAX_LKP_NUM);

	while (lkp_cnt && cb) {
		cnt = min(lkp_cnt, HMB_CMD_LKP_FMT2_MAX_CNT);
		cnt = hmb_cmd_fmt2_boundary_chk(start_lda, cnt);

		next_cb = hmb_cmd_get_cb_ctx(false, HMB_RESP_CNT_LKUP2);

		cb->nsid = nsid;
		cb->caller = caller;
		cb->rst.lkp_fmt2.total_cnt = total_cnt;
		cb->rst.lkp_fmt2.pda = pda;
		cb->rst.lkp_fmt2.lookup_cnt = cnt;

		if ((lkp_cnt - cnt) == 0) {
			// last command for this request
			cb->cmpl = cmpl;
			if (next_cb) {
				hmb_cmd_put_cb_ctx(next_cb, HMB_RESP_CNT_LKUP2);
				next_cb = NULL;
			}
		} else {
			cb->cmpl = hmb_lkp_fmt2_done;
			cb->rst.lkp_fmt2.rest_cnt = 0;
			if (next_cb == NULL) {
				// last command this time
				cb->rst.lkp_fmt2.cmpl_ori = cmpl;
				cb->rst.lkp_fmt2.next_slda = start_lda + cnt;
				cb->rst.lkp_fmt2.rest_cnt = lkp_cnt - cnt;
			}
		}

		_hmb_lkp_fmt2(nsid, start_lda, cnt, cb);

		pda += cnt;
		start_lda += cnt;
		lkp_cnt -= cnt;
		cb = next_cb;
	}

	return true;
}


fast_code bool hmb_lkp_rep_fmt1(u32 nsid, u32 lda, u32 pda, hmb_cmd_cb_t *cb)
{
	hmb_tbl_lkp_rep_fmt1_t *cmd;

	cmd = (hmb_tbl_lkp_rep_fmt1_t *) hmb_get_next_cmd(cb, 0);

	if (cmd == NULL)
		return false;

	cmd->nsid = nsid;
	cmd->cmd_code = HMB_CMD_LOOKUP_REPLACE_FMT1;
	cb->nsid = nsid;
	cb->tid = jiffies;
	cmd->lda = lda;
	cmd->lda_cnt = 1;  /* Must be 1 */
	cmd->new_pda = pda;

	hmb_cmd_submit(1);
	return true;
}

/*
 * lookup and replace with fused 2 command
 */
fast_code bool hmb_lkp_rep_fmt2(u32 nsid, u32 cnt, u32 lda,
		u32 pda[HMB_CMD_LKP_REP_FMT2_MAX_CNT], hmb_cmd_cb_t *cb)
{
	hmb_tbl_lkp_rep_fmt2_t *cmd;
	hmb_tbl_lkp_rep_fmt2_t *cmd2 = NULL;

	if (cnt > HMB_CMD_LKP_REP_FMT2_MAX_CNT) {
		hmb_apl_trace(LOG_ERR, 0x3af6, "HMB lookup/replace fmt2 cnt err %d", cnt);
		return false;
	}


	cmd = (hmb_tbl_lkp_rep_fmt2_t *) hmb_get_next_cmd(cb, 0);
	if (cnt > 2) {
		cmd2 = (hmb_tbl_lkp_rep_fmt2_t *) hmb_get_next_cmd(NULL, 1);
		if (cmd2 == NULL)
			goto no_cmd;
	}

	if (cmd == NULL) {
no_cmd:
		hmb_apl_trace(LOG_ERR, 0x8018, "HMB lookup/replace no cmd");
		return false;
	}

	cmd->lda_cnt = cnt;  //1~5
	cmd->cmd_code = HMB_CMD_LOOKUP_REPLACE_FMT2;
	cb->tid = jiffies;
	cb->nsid = nsid;
	if (cnt < 3) {
		cmd->fused = fused_single;
		cmd->nsid = nsid;
		cmd->payload.single.lda_start = lda;
		cmd->payload.single.new_pda[0] = pda[0];
		cmd->payload.single.new_pda[1] = pda[1];
		hmb_cmd_submit(1);

	} else {
		cmd->nsid = nsid;
		cmd->fused = fused_cmd1;
		cmd->payload.fused1.lda_start = lda;
		cmd->payload.fused1.new_pda[0] = pda[0];
		cmd->payload.fused1.new_pda[1] = pda[1];

		cmd2->lda_cnt = cnt;
		cmd2->cmd_code = HMB_CMD_LOOKUP_REPLACE_FMT2;
		cmd2->fused = fused_cmd2;
		cmd2->payload.fused2.new_pda[0] = pda[2];
		cmd2->payload.fused2.new_pda[1] = pda[3];
		cmd2->payload.fused2.new_pda[2] = pda[4];
		hmb_cmd_submit(2);
	}

	return true;
}

fast_code bool is_hmb_enabled(void)
{
	extern bool hmb_enabled;
	return hmb_enabled;
}

fast_code hmb_cmd_cb_t *hmb_cmd_get_cb_ctx(bool urgent, u32 resp_cnt)
{
	extern pool_t hmb_cbs_pool;
	extern u8 free_cb_cnt;
	extern u32 free_resp_cnt;
	hmb_cmd_cb_t *cb;

	if (urgent == false && ((free_cb_cnt <= 1) || (free_resp_cnt <= resp_cnt)))
		return NULL;

	cb = (hmb_cmd_cb_t *) pool_get_ex(&hmb_cbs_pool);
	if (cb) {
		free_cb_cnt--;
		free_resp_cnt -= resp_cnt;
	}

	return cb;
}

fast_code void hmb_cmd_put_cb_ctx(hmb_cmd_cb_t *cmd_cb, u32 resp_cnt)
{
	extern pool_t hmb_cbs_pool;
	extern u8 free_cb_cnt;
	extern u32 free_resp_cnt;

	free_cb_cnt++;
	free_resp_cnt += resp_cnt;
	pool_put_ex(&hmb_cbs_pool, (void *)cmd_cb);
}

#endif
/*! @} */
