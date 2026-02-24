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

/*! \file nvme_decoder.h
 * @brief nvme decode function
 *
 * \addtogroup decoder
 * @{
 */

/*!
 * @brief admin Command handler
 *
 * Pop nvme command from admin queue, and build request to process it.
 *
 * @param cntlid	controller id.
 *			  0 -- PF0 Admin queue request
 *			  1 -- VF1 Admin queue request
 *			  2 -- VF2 Admin queue request and so on ..
 *
 * @return		not used
 */
void nvmet_admin_cmd_in(u32 cntlid);

/*!
 * @brief core handler for asynchronous event request
 *
 * @param types_sts	asynchronous event type
 * @param param		AER event parameter
 *
 * @return		not used
 */
void nvmet_evt_aer_in(u32 type_sts, u32 param);

/*!
 * @brief insert error info to event log
 *
 * @param err_info 	error info
 *
 * @return		not used
 */
void nvmet_err_insert_event_log(struct insert_nvme_error_information_entry err_info);

/*!
 * @brief controller Configuration enable event handler
 *
 * CC.en 0 -> 1, restore miscellaneous software structures
 *
 * @param cc		Value of controller configuration register
 *
 * @return		not used
 */
void nvmet_cc_en(u32 cc);

void nvmet_assert_admin_cmd_in(u32 cntlid);
/*! @} */
