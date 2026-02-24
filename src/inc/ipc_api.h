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

/*! \file ipc_api.h
 * @brief provide api for ipc, all communication between CPU is better to rely on this
 *
 * \addtogroup utils
 * \defgroup ipc_api
 * \ingroup utils
 * @{
 */
#pragma once

/*! @brief ipc payload of CPU_MSG_SPB_ACK/CPU_MSG_SPB_QUERY */
typedef union _nsid_type_t {
	struct {
		u16 type;	///< block type, slc or native
		u16 nsid;	///< namespace id
	} b;
	u32 all;
} nsid_type_t;

struct _cpu_msg_req_t;

enum {
	SPB_READ_WEAK,			    ///< spb weak for read
	SPB_PROG_WEAK,              ///< spb weak for program, had not get criterion of retirement, Paul_20201202
	SPB_WEAK_HNDL_THRSHLD,      ///< before this case, apply action for weak spb
	SPB_RETIRED_BY_ERASE,		///< spb retired by erase
	SPB_RETIRED_BY_PROG,		///< spb retired by program
	SPB_RETIRED_BY_READ,		///< spb retired by host read error
	SPB_RETIRED_BY_READ_GC,		///< spb retired by gc read error
	OTHER,
};

typedef union _spb_weak_retire_t {
	struct {
		u32 spb_id : 16;	///< retired or weak spb
		u32 type : 16;		///< action type
	} b;
	u32 all;
} spb_weak_retire_t;

struct _btn_smart_io_t;

#ifdef MPC
/*!
 * @brief api to query new spb
 *
 * @param nsid	namespace id
 * @param type	block type
 *
 * @return	not used
 */
void ipc_api_spb_query(u32 nsid, u32 type);
void ipc_api_flush_blklist(u32 nsid, u32 type);
void ipc_api_free_flush_blklist_sdtag(void);
void ipc_api_pstream_get_open_ack(u16 blk_idx);

/*!
 * @brief api to ack spb was allocated
 *
 * @param nsid	namespace id
 * @param type	block type
 *
 * @return	not used
 */
void ipc_api_spb_ack(u32 nsid, u32 type);

/*!
 * @brief api to update spb read counter in FTL
 *
 * @param rd_cnt_sts	read counter status
 *
 * @return		not used
 */

void ipc_spb_rd_cnt_upd(u32 spb_id);

/*!
 * @brief api to ack spb read counter was updated done, or reset
 *
 * @param rx		rx cpu
 * @param spb_id	if updated done, spb_id shold be ~0, otherwise is reset specific spb
 *
 * @return		not used
 */
void ipc_spb_rd_cnt_upd_ack(u8 rx, u32 spb_id);

/*!
 * @brief initialization of ipc api
 *
 * @return	not used
 */
void ipc_api_init(void);

/*!
 * @brief api to ack spb id was gced
 *
 * @param spb_id	GC spb_id
 * @param ttl_du_cnt 	total move du count of this spb
 */
void ipc_api_gc_done(u32 spb_id, u32 ttl_du_cnt);

/*!
 * @brief api to set all cpus's log level
 *
 * @param new_level	new level
 *
 * @return		old level
 */
u32 ipc_api_log_level_chg(u32 new_level);

/*!
 * @brief api to get heap memory from remote cpu
 *
 * @param rx		remote cpu id
 * @param sz		size to be allocated
 *
 * @return		return remote allocated heap memory pointer or null
 */
void *ipc_api_remote_heap_malloc(u32 rx, u32 sz);

/*!
 * @brief api to get a dtag from remote cpu
 *
 * @param dtag		allocated dtag will be put in this buffer
 * @param sync		true for sync operation
 * @param ddr		ddr dtag
 *
 * @return		not used
 */
void ipc_api_remote_dtag_get(u32 *dtag, bool sync, bool ddr);

/*!
 * @brief handler of CPU_MSG_BM_COPY
 *
 * @param cpu_msg_req_t		remote message
 *
 * @return		not used
 */
void ipc_bm_copy_done(volatile struct _cpu_msg_req_t *req);

/*!
 * @brief check if pointer in shared tcm
 *
 * @param _ptr	pointer to be checked
 *
 * @return	return true if it was in shared tcm
 */
static inline bool is_ptr_tcm_share(void *_ptr)
{
	u32 ptr = (u32) _ptr;

	if (ptr >= REMOTE_BTCM_BASE && ptr < (REMOTE_BTCM_BASE + REMOTE_BTCM_SIZE))
		return true;

	return false;
}

/*!
 * @brief issue read error data in to ucache CPU
 *
 * @param ofst		du_ofst in bm_pl
 * @param status	error status
 *
 * @return		not used
 */
void ipc_api_ucache_read_error_data_in(int ofst, int status);

/*!
 * @brief issue read error data in to ra CPU
 *
 * @param ofst		du_ofst in bm_pl
 * @param status	error status
 *
 * @return		not used
 */
void ipc_api_ra_err_data_in(int ofst, int status);

/*!
 * @brief issue sync command to get btn write smart io info
 *
 * @param smart_io	pointer to smart io struct
 * @param rx		remote cpu
 * @param wr		true for write, false for read
 *
 * @return		not used
 */
void ipc_api_get_btn_get_smart_io(struct _btn_smart_io_t *smart_io, u32 rx, bool wr);

/*!
 * @brief issue cpu msg to rx cpu to enable or disable a system interrupt
 *
 * @param rx		rx cpu
 * @param sirq		system interrupt
 * @param ctrl		true to enable, false to disable
 *
 * @return		not used
 */
void ipc_api_misc_sys_isr_ctrl(u32 rx, u32 sirq, bool ctrl);

/*!
 * @brief issue cpu msg to CPU_BE to make a ftl ns clean, sync
 *
 * @param nsid		namespace id
 *
 * @return		not used
 */
void ipc_api_ns_clean(u32 nsid);

/*!
 * @brief ipc api to wait btn reset done, issue CPU is ready to reset btn
 *
 * @param cpu_idx	issue cpu index 0 base
 * @param btn_reset	if true btn will be reset
 *
 * @return 		not used
 */
void ipc_api_wait_btn_rst(u32 cpu_idx, bool btn_reset);

void eccu_switch_setting(u8 lbaf);


#if defined(USE_CRYPTO_HW)
//Andy_Crypto
void crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID);
void crypto_AES_EPM_Read(u8 cryptID, u8 mode);
#endif

#endif

/*! @} */
