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
//=============================================================================
//
/*! \file
 * @brief rainier BTN command manger
 *
 * \addtogroup btn
 * \defgroup btn_cmd
 * \ingroup btn
 * @{
 *
 */
//=============================================================================

BUILD_BUG_ON((BTN_INT_CNT >= 4095));

/* XXX: shared variables shall be declared in CPU1 */
#define Q_NWR_FREE_ENTRY_CNT	32	///< write free btn command slot count
#define Q_NWR_IN_ENTRY_CNT	32	///< normal write btn command IN slot count
#define Q_RLS_W_CNT		BTN_W_CMD_CNT	///< release command queue size
#define Q_RLS_FW_CNT 		BTN_W_CMD_CNT

typedef struct _btn_cmd_entry_t {
	u16 btag;			///< btag id
} btn_cmd_entry_t;

/*!
 * @brief btn command queue type definition
 */
enum btn_cmdq_type_t {
	BTN_CMDQ_START = 0,              //!< BTN_CMDQ_START
	BTN_CMDQ_Q0_NRD = BTN_CMDQ_START,//!< BTN_CMDQ_Q0_NRD
	BTN_CMDQ_Q1_PRD,                 //!< BTN_CMDQ_Q1_PRD
	BTN_CMDQ_Q2_NWR,                 //!< BTN_CMDQ_Q2_NWR
	BTN_CMDQ_END,                    //!< BTN_CMDQ_END
	BTN_CMDQ_NUM = BTN_CMDQ_END,     //!< BTN_CMDQ_NUM
};

/*!
 * @brief btn command release queue type definition
 */
enum btn_cmdq_rls_type_t {
	BTN_Q_RLS_START = 0,              //!< BTN_Q_RLS_START
	BTN_Q_RLS_R_NVM = BTN_Q_RLS_START,//!< BTN_Q_RLS_R_NVM
	BTN_Q_RLS_W_NVM,                  //!< BTN_Q_RLS_W_NVM
	BTN_Q_RLS_FW,                     //!< BTN_Q_RLS_FW
	BTN_Q_RLS_END,                    //!< BTN_Q_RLS_END
	BTN_Q_RLS_NUM = BTN_Q_RLS_END     //!< BTN_Q_RLS_NUM
};

/*!
 * @brief btn command release completion entry
 */
typedef union {
	u32 all;
	struct {
		u32 btag       : 12;
		u32 type       : 4; /* command release type for Write */

		u32 nvm_cmd_id : 10;
		u32 rsvd       : 6;
	} wr;
	struct {
		u32 btag       : 12;
		u32 type       : 4; /* command release type for Read */

		u32 hcmd_cid   : 16;
	} rd;
} btn_iom_cmd_rels_cpl_t;

extern struct list_head _otf_bcmd;
extern struct list_head _pending_bcmd;
extern volatile u8 is_A1_SoC;
extern bool rd_gc_flag;


#ifdef NS_MANAGE
extern u32 _btn_otf_cmd_cnt[];
/*!
 * @brief btn cmd idle check
 *
 * @param nsid, namespace id
 *
 * @return	bool, true, NSID based btn commands are done.
 */
bool btn_otf_cmd_idle_check(u8 nsid);
#endif

/*!
 * @brief initialize btn command queue
 *
 * @return	not used
 */
void btn_cmdq_resume(void);

/*!
 * @brief initialize btn read command queue (normal and priority)
 *
 * @return	not used
 */
void bcmd_r_cmdq_init(void);

/*!
 * @brief initialize btn write command queue
 *
 * @return	not used
 */
void bcmd_w_cmdq_init(void);

/*!
 * @brief initialize btn write command release push queue
 *
 * @return	not used
 */
void bcmd_fw_cmdq_init(void);

/*!
 * @brief btn command in handler
 *
 * @param cmdq	command queue
 * @param fcmdq	free command queue
 * @param wr	if write command
 *
 * @return	not used
 */
void btn_cmd_in(btn_cmdq_t *cmdq, btn_fcmdq_t *fcmdq, bool wr);

/*!
 * @brief btn command release handler, pop all release entries in release queue and recycle btn command
 *
 * @param cmdq		release command queue
 * @param rls_qid	release queue id, enum btn_cmdq_rls_type_t rls_qid
 *
 * @return		not used
 */
void btn_cmd_rls(btn_cmdq_t *cmdq, u32 qid);

/*!
 * @brief push free btn command back to free command queue
 *
 * @param fcmq		btn free command queue
 * @param cmd		free btn command, it can be NULL, if NULL, pop one from pool
 * @param wr		if write command queue
 *
 * @return		not used
 */
void btn_bcmd_push_freeQ(btn_fcmdq_t *fcmdq, btn_cmd_t *cmd, bool wr);

/*!
 * @brief handle btn write command in
 *
 * @return		not used
 */
void btn_w_cmd_in(void);

/*!
 * @brief handle btn write command release interrupt
 *
 * @return		not used
 */
void btn_wcmd_rls_isr(void);

/*!
 * @brief handle btn normal read command in
 *
 * @return		not used
 */
void btn_nrd_cmd_in(void);

/*!
 * @brief handle btn priority read command in
 *
 * @return		not used
 */
void btn_prd_cmd_in(void);

/*!
 * @brief handle btn read command release interrupt
 *
 * @return		not used
 */
void btn_rcmd_rls_isr(void);

/*!
 * @brief abort all read command in btn
 *
 * @return		not used
 */
void btn_rcmd_abort(void);

/*!
 * @brief abort all write command int btn
 *
 * @return		not used
 */
void btn_wcmd_abort(void);

/*!
 * @brief btn smart io data structure move, just copy command counter and access unit counter
 *
 * @param dst	destination
 * @param src	source
 *
 * @return	not used
 */
void btn_smart_io_mov(volatile btn_smart_io_t *dst, btn_smart_io_t *src);

/*!
 * @brief initialize btn command
 *
 * @return	not used
 */
void btn_cmdq_init(void);

/*!
 * @brief dispatch a io btn command
 *
 * @param bcmd		btn command
 * @param btag		command tag of btn command
 *
 * @return		return -1 if it was not a io command, return 0 for dispatched
 */
int btn_cmd_dispatcher(btn_cmd_t *bcmd, int btag);

/*! @} */
