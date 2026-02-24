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
/*! \file ncl_helper.h
 *
 * @brief define some helper function for NCL
 *
 * \addtogroup ftl
 * @{
 */
#pragma once

/*!
 * @brief helper function to setup ncl write command
 *
 * @param ncl_cmd		ncl command
 * @param op_type		refer to enum ncl_op_type_t
 * @param prog_qid		qid of program done dtag
 * @param flags			ncl command flags
 * @param bm_pl_list		user data tag list
 * @param completion		completion callback
 * @param caller		caller context
 *
 * @return			none
 */
static inline void ncl_cmd_prog_setup_helper(struct ncl_cmd_t *ncl_cmd,
		u8 op_type, u32 flags, bm_pl_t *bm_pl_list,
		void (*completion)(struct ncl_cmd_t*),
		void *caller)
{
	ncl_cmd->op_type = op_type;

	ncl_cmd->flags = flags;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->user_tag_list = bm_pl_list;
	ncl_cmd->completion = completion;
	ncl_cmd->caller_priv = caller;
	ncl_cmd->status = 0;

}


/*!
 * @brief helper function to setup ncl command
 *
 * @param ncl_cmd		ncl command
 * @param flags			flags of ncl command
 * @param op_code		ncl op code
 * @param bm_pl_list		user data tag list
 * @param completion		completion callback
 * @param caller		caller context
 *
 * @return			none
 */
static inline void ncl_cmd_setup_helper(struct ncl_cmd_t *ncl_cmd,
		u32 flags, enum ncl_cmd_op_t op_code,
		bm_pl_t *bm_pl_list,
		void (*completion)(struct ncl_cmd_t*), void *caller)
{
	ncl_cmd->op_type = 0;

	ncl_cmd->flags = flags;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
	ncl_cmd->op_code = op_code;
	ncl_cmd->user_tag_list = bm_pl_list;
	ncl_cmd->completion = completion;
	ncl_cmd->caller_priv = caller;
	ncl_cmd->status = 0;

}

/*!
 * @brief helper function to setup ncl command common address
 *
 * @param param		common address pointer
 * @param pda_list	pda list
 * @param cnt		list length
 * @param info_list	ncl information list
 *
 * @return		none
 */
static inline void ncl_cmd_setup_addr_common(struct common_param_t *param,
	pda_t *pda_list, u32 cnt, struct info_param_t *info_list)
{
	param->list_len = cnt;
	param->info_list = info_list;
	param->pda_list = pda_list;
}

/*! @} */
