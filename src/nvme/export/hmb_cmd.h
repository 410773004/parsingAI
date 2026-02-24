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

/*!
 * \file
 * @brief export header of HMB command
 * \addtogroup export
 * \defgroup export_hmb_cmd export of HMB command
 * \ingroup export
 * @{
 */

#define HMB_CMD_LKP_FMT1_MAX_CNT	2	///< lookup format 1 supports 2 entries lookup
#define HMB_CMD_LKP_FMT2_MAX_CNT	14	///< lookup format 2 supports 14 entries lookup
#define HMB_CMD_LKP_REP_FMT2_MAX_CNT	5	///< lookup and replace format 2 supports 5 entries operation
#define HMB_AUTO_LOOKUP_MAX_CNT		14	///< auto lookup supports 14 entries operation
#define HMB_CMD_FMT2_BOUNDARY_SFT	6	///< HMB command format 2 boundary shift
#define HMB_CMD_FMT2_BOUNDARY		(1 << 6)	///< HMB command format 2 boundary, 64 dwords

#define HMB_RESP_CNT_LKUP1	1		///< max number of response lookup format 1 command would take
#define HMB_RESP_CNT_LKUP2	4		///< max number of response lookup format 2 command would take
#define HMB_RESP_CNT_RPL1	1		///< max number of response replace format 1 command would take
#define HMB_RESP_CNT_RPL2	2		///< max number of response replace format 2 command would take
#define HMB_RESP_CNT_READ	1		///< max number of response table read command would take
#define HMB_RESP_CNT_WRITE	1		///< max number of response table write command would take

/* if table was not in HMB, return lookup result HMB_LKP_MISS */
#define HMB_LKP_MISS			0xFFFFFFFF	///< if table was not in HMB, return lookup result HMB_LKP_MISS

struct _hmb_cmd_cb_t;

/*!
 * @brief result of HMB lookup format 1
 */
typedef struct _hmb_cmd_lkp_fmt1_rst_t
{
	u32 vbits;					///< valid bits of result pda array
	u32 *pda;					///< result PDA buffer
} hmb_cmd_lkp_fmt1_rst_t;

/*!
 * @brief result of HMB lookup format 2
 */
typedef struct _hmb_cmd_lkp_fmt2_rst_t
{
	u8 cnt;					///< PDA count of looked up successfully
	u8 lookup_cnt;				///< lookup PDA count
	u8 total_cnt;				///< total lookup cnt in this lookup process
	u8 rest_cnt;				///< how many lda have not been lookup yet
	u32 *pda;					///< result PDA buffer
	u32 next_slda;				///< next slda for issue extra lookup cmd
	void (*cmpl_ori)(struct _hmb_cmd_cb_t*);	///< original callback function
} hmb_cmd_lkp_fmt2_rst_t;

/*!
 * @brief result of HMB lookup/replace format 1
 */
typedef struct _hmb_cmd_lkp_rep_fmt1_rst_t
{
	u32 vbits;		///< valid bits of result pda array
	u32 old_pda;	///< pda result of looked up
} hmb_cmd_lkp_rep_fmt1_rst_t;

/*!
 * @brief result of HMB lookup/replace format 2
 */
typedef struct _hmb_cmd_lkp_rep_fmt2_rst_t
{
	u32 cnt;					///< count of lookup and place
	u32 *pda;					///< pda result buffer for lookup
} hmb_cmd_lkp_rep_fmt2_rst_t;

/*!
 * @brief callback context of HMB command
 */
typedef struct _hmb_cmd_cb_t
{
	void *caller;				///< caller context
	bool error;				///< error status of HMB command, true for error
	u8 nsid;				///< namesapce id
	u32 tid;				///< transaction id of this lookup was performed
	union {
		hmb_cmd_lkp_fmt1_rst_t lkp_fmt1;
		hmb_cmd_lkp_fmt2_rst_t lkp_fmt2;
		hmb_cmd_lkp_rep_fmt1_rst_t lkp_rep_fmt1;
		hmb_cmd_lkp_rep_fmt2_rst_t lkp_rep_fmt2;
	} rst;					///< union pointer of result buffer
	void (*cmpl)(struct _hmb_cmd_cb_t*);		///< callback function when command completed
} hmb_cmd_cb_t;

/*!
 * @brief result of HMB auto-lookup
 */
typedef struct _hmb_auto_lkp_rst_t
{
	u8 req_id;				///< req_id of request
	u8 cnt;				///< PDA count of auto-lookup
	u8 status;				///< status of auto-lookup
	u32 tid;				///< transaction id when auto-lookup was performed

	u32 pda[33];			///< pda buffer for result
} hmb_auto_lkp_rst_t;

extern u8 evt_auto_lkp_done;	///< HMB auto lookup finish event

/*!
 * @brief HMB lookup format 1
 *
 * HMB lookup format 1 command could look up 2 randomly LDA
 *
 * @param nsid 	indicate namespace id 0 or 1
 * @param cnt	how many LDA need to be looked up
 * @param lda1	1st LDA to be looked up
 * @param lda2	2nd LDA to be looked up
 * @param cb	HMB command callback context
 *
 * @return	Return true for command issued
 */
extern bool hmb_lkp_fmt1(u32 nsid, u32 cnt, u32 lda1, u32 lda2,
							hmb_cmd_cb_t *cb);

/*!
 * @brief HMB lookup format 2
 *
 * HMB lookup format 2 command could lookup continuous PDA by start LDA
 *
 * @param nsid 	indicate namespace id 0 or 1
 * @param start_lda		start LDA to be looked up
 * @param cnt			PDA count to be looked up, max is 14
 * @param cb			HMB command callback context
 *
 * @return			Return true for command issued
 */
extern bool hmb_lkp_fmt2(u32 nsid, u32 start_lda, u32 cnt, hmb_cmd_cb_t *cb);

/*!
 * @brief HMB lookup and replace format 1
 *
 * HMB lookup and replace format 1 could replace new PDA for LDA, and it will
 * return the old PDA of LDA to caller
 *
 * @param nsid 	indicate namespace id 0 or 1
 * @param lda	LDA to be replaced
 * @param pda	new PDA to be replaced to
 * @param cb	HMB command callback context
 *
 * @return	Return true for command issued
 */
extern bool hmb_lkp_rep_fmt1(u32 nsid, u32 lda, u32 pda, hmb_cmd_cb_t *cb);

/*!
 * @brief HMB lookup and replace format 2
 *
 * HMB lookup and replace format 2 could replace new PDA for continue LDA, and
 * it will return old PDA of LDA to caller, the maximum replace count is 5
 *
 * @param nsid 	indicate namespace id 0 or 1
 * @param cnt	LDA count to be replaced
 * @param lda	start LDA to be replaced
 * @param pda	start PDA to be replaced
 * @param cb	HMB command callback context
 *
 * @return	Return true for command issued
 */
extern bool hmb_lkp_rep_fmt2(u32 nsid, u32 cnt, u32 lda,
		u32 pda[HMB_CMD_LKP_REP_FMT2_MAX_CNT], hmb_cmd_cb_t *cb);

/*!
 * @brief HMB table write
 *
 * HMB table write will push a partial table (4K) of L2P table to host memory
 *
 * @param nsid 		indicate namespace id 0 or 1
 * @param buf		L2P partial table to be pushed
 * @param tbl_id	table id
 * @param cb		HMB command callback context
 *
 * @return		Return true for command issued
 */
extern bool hmb_tbl_write(u32 ns_id, void *buf, u32 tbl_id, hmb_cmd_cb_t *cb);

/*!
 * @brief HMB table read
 *
 * HMB table read will pop a partial table (4K) from host memory
 *
 * @param nsid 		indicate namespace id 0 or 1
 * @param buf		L2P partial table to be popped
 * @param tbl_id	table id
 * @param cb		HMB command callback context
 *
 * @return		Return true for command issued
 */
extern bool hmb_tbl_read(u32 nsid, void *buf, u32 tbl_id, hmb_cmd_cb_t *cb);

/*!
 * @brief get current HMB HW transaction ID
 *
 * Transaction ID was used to check if looked up response was valid

 *
 * @return	Return current transaction id
 */
extern u32 hmb_get_hw_tid(void);

/*!
 * @brief check if HMB is enabled or not
 *
 * @return	Return true for enabled
 */
extern bool is_hmb_enabled(void);

/*!
 * @brief get hmb command callback context
 *
 * @param urgent 	last one context is available when urgent == true
 * @param resp_cnt	number of response need
 *
 * @return		command callback pointer or NULL
 */
extern hmb_cmd_cb_t *hmb_cmd_get_cb_ctx(bool urgent, u32 resp_cnt);

/*!
 * @brief put hmb command callback context
 *
 * @param cmd_cb	command callback to be returned
 * @param resp_cnt	number of response return
 *
 * @return		none
 */
extern void hmb_cmd_put_cb_ctx(hmb_cmd_cb_t *cmd_cb, u32 resp_cnt);

/*! @} */
