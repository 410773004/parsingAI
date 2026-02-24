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
 * @brief Rainier HMB Module
 *
 * \addtogroup hal
 * \defgroup HMB
 * \ingroup hal
 * @{
 */
#pragma once

struct nvme_cmd;
struct _hmb_cmd_cb_t;
struct nvme_host_memory_desc_entry;
struct _req_t;

#define HMB_CMD_CNT		64	///< total concurrent HMB command count

#define HMB_AUTO_CMD_CNT	NVME_ACTIVE_COMMAND_COUNT	///< HMB auto lookup command count

#define HMB_RESP_CNT		256	///< HMB command response count
#define HMB_AUTO_RESP_CNT	256	///< HMB auto command response count

/*!
 * @brief prototype of HMB command
 */
typedef struct _hmb_cmd_t {
	struct {
		u32 cmd_id:20;	///< command index
		u32 rsvd:3;	///< reserved field
		u32 nsid:1;	///< namespace id 0 or 1
		u32 cmd_code:4;	///< command code
		u32 tu_cnt:4;	///< table count
	} dw0;
	u32 dw1;			///< payload, dword1
	u32 dw2;			///< payload, dword2
	u32 dw3;			///< payload, dword3
} hmb_cmd_t;

/*!
 * @brief HMB command to read table from HMB
 */
typedef struct _hmb_tbl_read_cmd_t {
	/*! dw0 */
	u32 cmd_tag:20;		///< command index
	u32 rsvd:3;		///< reserved field
	u32 nsid:1;		///< namespace id 0 or 1
	u32 cmd_code:4;		///< command code, must be 5
	u32 tu_cnt:4;		///< table unit count, must be 1

	/*! dw1 */
	u32 starting_lda;		///< first lda of table
	u32 starting_sram_addr;	///< sram DMA address of table
	u32 rsvd1;			///< reserved field
} hmb_tbl_read_cmd_t;

/*!
 * @brief HMB command to write table to HMB
 */
typedef struct _hmb_tbl_write_cmd_t {
	/*! dw0 */
	u32 cmd_tag:20;		///< command index
	u32 rsvd:3;		///< reserved field
	u32 nsid:1;		///< namespace id 0 or 1
	u32 cmd_code:4;		///< command code, must be 6
	u32 tu_cnt:4;		///< table unit count, must be 1

	/*! dw1 */
	u32 starting_lda;		///< first lda of table
	u32 starting_sram_addr;	///< sram DMA address of table
	u32 rsvd1;			///< reserved field
} hmb_tbl_write_cmd_t;

/*!
 * @brief HMB command to lookup lda randomly
 */
typedef struct _hmb_tbl_lkp_fmt1_t {
	u32 cmd_tag:20;		///< command index
	u32 rsvd:3;		///< reserved field
	u32 nsid:1;		///< namespace id 0 or 1
	u32 cmd_code:4;		///< command code, must be 1
	u32 lda_cnt:4;		///< lookup count, max is 2

	/*! dw 1~3 */
	u32 lda[2];		///< lda array to be looked up
	u32 rsvd1;			///< reserved field
} hmb_tbl_lkp_fmt1_t;

/*!
 * @brief HMB command to lookup lda sequentially
 */
typedef struct _hmb_tbl_lkp_fmt2_t {
	/*! dw 0 */
	u32 cmd_tag:20;		///< command index
	u32 rsvd:3;		///< reserved field
	u32 nsid:1;		///< namespace id 0 or 1
	u32 cmd_code:4;		///< command code, must be 2
	u32 lda_cnt:4;		///< lookup count, max is 2

	/*! dw 1 */
	u32 starting_lda;		///< start lda to be looked up
	u32 rsvd1[2];		///< reserved field
} hmb_tbl_lkp_fmt2_t;

/*!
 * @brief HMB command to lookup/replace pda randomly
 */
typedef struct _hmb_tbl_lkp_rep_fmt1_t {
	/*! dw 0 */
	u32 cmd_tag:20;		///< command index
	u32 rsvd:3;		///< reserved field
	u32 nsid:1;		///< namespace id 0 or 1
	u32 cmd_code:4;		///< command code, must be 3
	u32 lda_cnt:4;		///< lookup count, max is 1

	/*! dw 1~3 */
	u32 lda;			///< start lda to be looked up
	u32 new_pda;		///< new pda to replace
	u32 rsvd1;			///< reserved field
} hmb_tbl_lkp_rep_fmt1_t;

/*!
 * @brief for hmb_tbl_lkp_rep_fmt1_t, when fused bit = single command
 */
typedef struct _fused_single_t
{
	u32 lda_start;		///< start lda to replace
	u32 new_pda[2];		///< new pda to be replaced
} fused_single_t;

/*!
 * @brief for hmb_tbl_lkp_rep_fmt1_t, when fused bit = command 1
 */
typedef struct _fused_cmd1_t {
	u32 lda_start;		///< start lda to replace
	u32 new_pda[2];		///< new pda to be replaced
} fused_cmd1_t;

/*!
 * @brief for hmb_tbl_lkp_rep_fmt1_t, when fused bit = command 2
 */
typedef struct _fused_cmd2_t {
	u32 new_pda[3];		///< new pda to be replaced
} fused_cmd2_t;

/*!
 * @brief HMB command to lookup/replace pda sequentially
 */
typedef struct _hmb_tbl_lkp_rep_fmt2_t {
	u32 cmd_tag:20;		///< command index
	u32 fused:2;		///< fused bit if this command will use more than 1 command entries
	u32 rsvd:1;		///< reserved field
	u32 nsid:1;		///< namespace id 0 or 1
	u32 cmd_code:4;		///< command code, must be 4
	u32 lda_cnt:4;		///< lookup count, max is 5

	union {
		fused_single_t single;	///< payload for single command
		fused_cmd1_t fused1;	///< payload for fused command 1
		fused_cmd2_t fused2;	///> payload for fused command 2
	} payload;
} hmb_tbl_lkp_rep_fmt2_t;

/*!
 * @brief general header of HMB response
 */
typedef struct _hmb_cmd_resp_hdr_t {
	u32 cmd_tag:20;		///< command index
	u32 resp_ent_cnt:3;	///< how many response entries for HMB command
	u32 hmb_crc_err:1;		///< CRC error bit, 1 for error
	u32 resp_code:4;		///< command code of response
	u32 status:4;		///< status bits
} hmb_cmd_resp_hdr_t;

/*!
 * @brief header of auto lookup response
 */
typedef struct _hmb_auto_lkp_resp_hdr_t {
	u32 cmd_tag:16;		///< command fetch id or CID, depend on HMB_NTBL_AUTO_LKUP_CTRL_STS
	u32 sq_id:7;		///< host command sq id
	u32 hmb_crc_err:1;		///< CRC error bit, 1 for error
	u32 resp_code:1;		///< command code of response
	u32 resp_ent_cnt:3;	///< how many response entries
	u32 valid_cnt:4;		///< valid count of auto lookup
} hmb_auto_lkp_resp_hdr_t;

/*!
 * @brief prototype of HMB response
 */
typedef struct _hmb_cmd_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< response header
	u32 payload[3];		///< payload size is 3 dwords
} hmb_cmd_resp_t;

/*!
 * @brief prototype of HMB response entry
 */
typedef struct _hmb_cmd_resp_entry_t {
	u32 payload[4];			///< each response has 4 dwords
} hmb_cmd_resp_entry_t;

/*!
 * @brief response of hmb lookup format 1
 */
typedef struct _hmb_lkp_fmt1_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< header
	u32 transaction_id;		///< transaction id of this lookup
	u32 p[2];			///< result pda
} hmb_lkp_fmt1_resp_t;

/*!
 * @brief response of hmb lookup format 2
 */
typedef struct _hmb_lkp_fmt2_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< header
	u32 transaction_id;	///< transaction id of this lookup
	u32 p[2];			///< result pda, there may be more result in other entries
} hmb_lkp_fmt2_resp_t;

/*!
 * @brief response of hmb lookup/replace format 1
 */
typedef struct _hmb_lkp_rep_fmt1_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< header
	u32 transaction_id;	///< transaction id of this lookup and replace
	u32 pda;			///< old pda
	u32 rsvd1;			///< reserved field
} hmb_lkp_rep_fmt1_resp_t;

/*!
 * @brief response of HMB lookup/replace format 2
 */
typedef struct _hmb_lkp_rep_fmt2_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< header
	u32 transaction_id;	///< transaction id of this lookup and replace
	u32 pda[2];		///< old pda, there may be more results in other entries
} hmb_lkp_rep_fmt2_resp_t;

/*!
 * @brief response of HMB read table from HMB
 */
typedef struct _hmb_tbl_read_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< header
	u32 transaction_id;	///< transaction id
	u32 meta[2];		///< meta of this HMB table, won't be referenced
} hmb_tbl_read_resp_t;

/*!
 * @brief response of HMB write table from HMB
 */
typedef struct _hmb_tbl_write_resp_t {
	hmb_cmd_resp_hdr_t hdr;		///< header
	u32 transaction_id;	///< transaction id
	u32 rsvd[2];		///< reserved field
} hmb_tbl_write_resp_t;

/*!
 * @brief response of HMB auto lookup
 */
typedef struct _hmb_auto_lkp_resp_t {
	hmb_auto_lkp_resp_hdr_t hdr;	///< header
	u32 transaction_id;	///< transaction id
	u32 p[2];			///< result pda, there may be more results in other entries
} hmb_auto_lkp_resp_t;

typedef struct _hmb_auto_lkp_dat_resp_t {
	u32 p[4];			///< result pda, there may be more results in other entries
} hmb_auto_lkp_dat_resp_t;

/*!
 * @brief HMB command opcode
 */
enum {
	HMB_CMD_IDLE = 0,		///< idle command
	HMB_CMD_LOOKUP_FMT1,		///< lookup format 1, lookup 1 or 2 lda
	HMB_CMD_LOOKUP_FMT2,		///< lookup format 2, lookup 1~14 continuous lda
	HMB_CMD_LOOKUP_REPLACE_FMT1,	///< lookup/replace format 1, replace 1 lda
	HMB_CMD_LOOKUP_REPLACE_FMT2,	///< lookup/replace format 2, replace 1~5 continuous lda
	HMB_CMD_TABLE_READ,		///< read table from HMB
	HMB_CMD_TABLE_WRITE		///< write table to HMB
};

/*!
 * @brief attach host memory buffer, get from host
 *
 * @param hmb_desc_list		host memory buffer descriptor list
 * @param cnt			list count
 * @param page_bits		nvme page bits
 * @param req			host request
 *
 * @return			None
 */
extern void hmb_attach(struct nvme_host_memory_desc_entry* hmb_desc_list,
		u32 cnt, u32 page_bits, struct _req_t *req);

/*!
 * @brief detach host memory buffer, return to host
 *
 * @param ctag	ctag of detach command
 *
 * @return	Return detach completed or not
 */
extern bool hmb_detach(u32 ctag);

/*!
 * @brief enable HMB relative irq bit
 *
 * @param irq_bit	FW_NTBL_ACCESS_DONE_MASK or NTBL_AUTO_LKUP_DONE_MASK
 *
 * @return		None
 */
extern void hmb_enable_irq(u32 irq_bit);

/*!
 * @brief disable HMB relative irq bit
 *
 * @param irq_bit	FW_NTBL_ACCESS_DONE_MASK or NTBL_AUTO_LKUP_DONE_MASK
 *
 * @return		None
 */
extern void hmb_disable_irq(u32 irq_bit);

/*!
 * @brief get next free HMB command
 *
 * Get next usable HMB command, set pos > 0 to get more command.
 *
 * @param cb		HMB command callback context
 * @param pos		position different from next write pointer
 *
 * @return		HMB command pointer
 */
extern hmb_cmd_t *hmb_get_next_cmd(struct _hmb_cmd_cb_t* cb, u32 pos);

/*!
 * @brief submit HMB comamnds
 *
 * @param cmd_cnt	command count to be submitted
 *
 * @return		None
 */
extern void hmb_cmd_submit(u32 cmd_cnt);

/*!
 * @brief check if this command need to wait for auto lookup result
 *
 * @param cmd		nvme command
 *
 * @return		return true for read and partial write command
 */
bool hal_wait_hmb_auto_lookup(struct nvme_cmd *cmd);

/*!
 * @brief get hmb callback context index
 *
 * @param cb		HMB command callback context
 *
 * @return		corresponding index
 */
extern u32 hmb_get_cb_idx(struct _hmb_cmd_cb_t *cd);

/*!
 * @brief HMB suspend function, backup some register
 *
 * @return	none
 */
extern void hmb_suspend(void);

/*!
 * @brief HMB resume function, re-eanble HMB table
 *
 * @param abort		if resume from suspend aborted
 *
 * @return		none
 */
extern void hmb_resume(bool abort);

/*!
 * @brief HMB host memory reset function
 *
 * @param req		host reuqest
 * @param callback	reset complete callback function
 * @param page_bits	nvme page bits
 *
 * @return		none
 */
extern void hmb_mem_reset(struct _req_t *req, void (*callback)(struct _req_t *), u32 page_bits);
/*! @} */
