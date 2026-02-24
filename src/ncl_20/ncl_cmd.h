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

typedef QSIMPLEQ_HEAD(_ncl_cmd_queue, ncl_cmd_t) ncl_cmd_queue;

extern bool ncl_cmd_wait;
extern bool FW_ARD_EN;
extern u8 FW_ARD_CPU_ID;
extern ncl_cmd_queue ncl_cmds_wait_queue;
#if OPEN_ROW == 1
extern u32 openRow[];
//#define GET_DIE_PLANE_NUM_FORM_PDA(die_id,plane)((plane * MAX_DIE_MGR_CNT) + die_id);
#define GET_DIE_PLANE_NUM_FORM_PDA(die_id,plane)((2 * die_id) + plane);
#endif

#define NCL_CMD_PENDING(ncl_cmd) (!((ncl_cmd)->flags & NCL_CMD_COMPLETED_FLAG))	///< Check NCL cmd pending or not
#define NCL_CMD_COMPLETED(ncl_cmd) ((ncl_cmd)->flags & NCL_CMD_COMPLETED_FLAG)	///< Check NCL cmd completed or not
#if defined(RDISK)
#define PATROL_READ 1
#define PARD_ALL_DIE_AIPR (MAX_TARGET*MAX_CHANNEL*4) //plane_cnt //only for judge whether plane_cnt>32
#if(PARD_ALL_DIE_AIPR > 32)
#define PARD_DIE_AIPR 64
#else
#define PARD_DIE_AIPR 32
#endif

//==========================================================Refresh read
#define REFR_RD //define for refresh, modify by James 2023/07/05
#ifdef REFR_RD
fast_data struct rfrd_times_s
{
    u16 rfrd_all_die;
    u16 power_on_rfrd_die;
    u16 idle_rfrd_die;
    u16 current_rfrd_die;
    u8 idle_rfrd_times;
    u8 power_on_rfrd_times;
};
extern struct rfrd_times_s rfrd_times_struct;

#endif
//==========================================================
#endif
#if NCL_USE_DTCM_SQ
#define DTAG_PTR_COUNT		DTCM_SQ_FIFO_COUNT
#else
#define DTAG_PTR_COUNT		FSPM_SQ_FIFO_COUNT
#endif
#define DTAG_PTR_INVALID	DTAG_PTR_COUNT

#ifdef History_read
#define Group_num (3*14) //meaning 3 page/WL * 14 WL/WL_Group
#endif

#ifdef DUAL_BE							///< Two NCL CPU access same NCB, each manage half channels
#define NCB_NRM_SQ_CNT		2				///< Normal SQ cnt 2: SQ 0 and SQ1

extern volatile bool ncl_busy[NCB_NRM_SQ_CNT];				///< Busy indicator of 2 NCLs

#if CPU_ID == CPU_BE || CPU_ID == CPU_BE2
#define NRM_SQ_IDX		0				///< Normal SQ 0
#define DTAG_PTR_STR_SQ		START_FCMD_ID			///< Start du_dtag_ptr
#define DTAG_PTR_END_SQ		((DTAG_PTR_COUNT >> 1))		///< End du_dtag_ptr
#else
#define NRM_SQ_IDX		1				///< Normal SQ 1
#define DTAG_PTR_STR_SQ		(DTAG_PTR_COUNT >> 1)		///< Start du_dtag_ptr on another CPU
#define DTAG_PTR_END_SQ		(DTAG_PTR_COUNT)		///< End du_dtag_ptr on another CPU
#endif

#define SET_NCL_BUSY()	(ncl_busy[NRM_SQ_IDX] = true)		///< Set NCL busy on this CPU
#define SET_NCL_IDLE()	(ncl_busy[NRM_SQ_IDX] = false)		///< Set NCL idle on this CPU
#define IS_NCL_IDLE()	(!(ncl_busy[0] | ncl_busy[1]))		///< Check both CPU NCL idleness

#else
extern u32 fcmd_outstanding_cnt;
#define DTAG_PTR_STR_SQ		START_FCMD_ID			///< Start du_dtag_ptr
#define DTAG_PTR_END_SQ		DTAG_PTR_COUNT			///< End du_dtag_ptr
#define SET_NCL_BUSY()
#define SET_NCL_IDLE()
#define IS_NCL_IDLE()	(fcmd_outstanding_cnt ? false : true)
#endif
/*!
 * @brief Read RDA list with sync mode, it may use for ROM
 *
 * @param[in] rda_list		point to a RDA list
 * @param[in] op		opcode
 * @param[in] dtag		point to a dtag/ctag array
 * @param[in] count		rda count
 * @param[in] du_format		DU format index
 * @param[in] stripe_id		Stripe ID for RAID operation
 *
 * @return success or fail
 */
int ncl_cmd_simple_submit(rda_t *rda_list, enum ncl_cmd_op_t op, bm_pl_t *dtag,
	u32 count, int du_format, int stripe_id);

/*!
 * @brief NCL command initialization before any NCL cmd handling
 *
 * @return	not used
 */
void ncl_cmd_init(void);

/*!
 * @brief NCL command submit main function
 *
 * @param ncl_cmd	NCL command pointer
 *
 * @return	not used
 */
void ncl_cmd_submit(struct ncl_cmd_t *ncl_cmd);


/*!
 * @brief Acquire free du_dtag_ptr used for F-inst
 * The du_dtag_ptr of 1st F-inst within FCMD will also be multiplex as fcmd_id,
 * so we do not to allocate/free another resource.
 *
 * @return	free du_dtag_ptr
 */
du_dtag_ptr_t ncl_acquire_dtag_ptr(void);

/*!
 * @brief Save ncl_cmd pointer which will be retrieved by fcmd_id later when error occurs or fcmd completion
 *
 * @param fcmd_id	FCMD ID
 * @param ncl_cmd	NCL command pointer
 *
 * @return	not used
 */
void ncl_cmd_save(u32 fcmd_id, struct ncl_cmd_t* ncl_cmd);

/*!
 * @brief Rapid path for single DU read to speed up random 4KB IOPS
 * May be removed if ncl_cmd_submit is good & fast enough
 *
 * @param ncl_cmd	NCL command pointer
 *
 * @return	not used
 */
void ncl_cmd_rapid_single_du_read(struct ncl_cmd_t *ncl_cmd);
void ncl_cmd_dus_read_ard(struct ncl_cmd_t *ncl_cmd);
void ncl_cmd_read_retry(struct ncl_cmd_t *ncl_cmd);
/*!
 * @brief Get MP count from beginning of PDA list (for erase/program etc page-unit operations, each PDA is a page)
 *
 * @param pda_list	PDA list
 * @param pda_cnt	PDA count
 *
 * @return	Sequential multi-plane count
 */
extern int get_mp_cnt(pda_t* pda_list, u32 pda_cnt);

/*!
 * @brief Get DU count from beginning of PDA list (for read etc DU-unit operations, each PDA is a DU)
 *
 * @param pda_list	PDA list
 * @param pda_cnt	PDA count
 *
 * @return	Sequential DU count within a LUN
 */
extern int get_read_du_cnt(pda_t* pda_list, u32 pda_cnt);

/*!
 * @brief Convert CPU memory address to HW used DMA memory address
 *
 * @param ptr	CPU address
 *
 * @return	DMA address
 */
extern u32 ptr2busmem(void* ptr);

/*!
 * @brief Notifier for a ncl_cmd that ARD occur
 *
 * @param fcmd_id	FCMD ID of FCMD that trigger ARD
 *
 * @return	not used
 */
void ncl_fcmd_ard_occur(u32 fcmd_id);

/*!
 * @brief NCL command completion handler within NCL module
 *
 * @param fcmd_id	FCMD ID of completed FCMD
 *
 * @return	not used
 */
void ncl_fcmd_completion(u32 fcmd_id);

/*!
 * @brief Refresh read a spb per 200ms
 *
 * @return	not used
 */
void refresh_read_task(void *data);
void _refresh_read_task(u32 param, u32 payload, u32 count);




/*!
 * @brief Refresh read cpl
 *
 * @return	not used
 */




