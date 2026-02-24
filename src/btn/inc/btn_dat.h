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

/*! \file btn_dat.h
 * @brief Rainier BTN Module, data queue part
 *
 * \addtogroup btn
 * \defgroup btn data queue
 * \ingroup btn
 * @{
 */

/*!
 * @brief initialize btn data queue
 */
void btn_datq_init(void);

/*!
 * @brief resume btn data queue, after 1st initialization or PMU
 */
void btn_datq_resume(void);

/*!
 * @brief api to handler common free updated queue
 *
 * @return	not used
 */
void btn_handler_com_free(void);

/*!
 * @brief btn data queue updated interrupt routine
 *
 * it will handle all data queues if they are not handled yet
 *
 * @return	not used
 */
void btn_data_in_isr(void); 
void btn_de_wr_hold_handle(u32 cnt, u32 hold_thr, u32 dis_thr);

#ifdef RD_FAIL_GET_LDA
void ipc_host_rd_get_lda(volatile cpu_msg_req_t *req);
#endif
#ifdef ERRHANDLE_ECCT   //register ECCT
void ipc_rc_reg_ecct(volatile cpu_msg_req_t *req);
#endif
#ifdef NCL_RETRY_PASS_REWRITE
void ipc_retry_get_lda_do_rewrite(volatile cpu_msg_req_t *req);
#endif

/*! @} */
