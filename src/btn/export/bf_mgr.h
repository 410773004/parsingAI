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
#pragma once

#include "dma.h"
#include "dtag.h"
#include "list.h"

/*!
 * \file
 * @brief export header of buffer manager
 * \addtogroup export
 * \defgroup export_bf_mgr export of buffer manager
 * \ingroup export
 * @{
 */

/*! VTAG ID = 0xF_FFFE, 1048574 */
#define RVTAG_ID		((1 << 20) - 2)	///< unmapped vtag id 1, 1048574
#define RVTAG2_ID		(RVTAG_ID - 1)	///< unmapped vtag id 2, 1048573
#define WVTAG_ID		(RVTAG2_ID - 1)	///< dummy vtag id, 1048572
#define EVTAG_ID		(WVTAG_ID - 1)	///< read error vtag id, 1048571
#define BTN_ABT_BTAG	0xFFF

#define SEMI_MODE_SHF	(3)

/*! @brief type_ctrl definition: (btn -> fw) */
enum write_data_entry_type_ctrl_t {
	BTN_NVM_TYPE_CTRL_NRM = 0,	///< normal data entry, 4K
	BTN_NVM_TYPE_CTRL_PAR,		///< partial data entry, not full 4K
	BTN_NVM_TYPE_CTRL_CMP,		///< compare data entry
	BTN_NCB_TYPE_CTRL_PDONE,	///< NCB transferring done data entry
	BTN_HMB_TYPE_CTRL_DTAG,		///< HMB Dtag read back
	BTN_NVM_TYPE_CTRL_NRM_PAR_MASK = 0x7,	///< mask for normal or partial
	BTN_NVM_TYPE_CTRL_POISON_BIT = 0x20	///< Wr-data-entry with poison bit set // TODO Need confirm with IG
};

enum write_data_entry_semi_ctrl_t {
	BTN_SEMI_NORMAL_MODE = (0 << SEMI_MODE_SHF),
	BTN_SEMI_STREAMING_MODE = (1 << SEMI_MODE_SHF),
	BTN_SEMI_DDRCOPY_STREAMING_MODE = (3 << SEMI_MODE_SHF),
	BTN_SEMI_MODE_MASK = 0x38
	//BTN_SEMI_MODE_MASK = (3 << SEMI_MODE_SHF)// TODO Need confirm with IG
};

/*! @brief type_ctrl: (ncb -> btn -> fw) */
enum read_data_entry_type_ctrl_t {
	BTN_NCB_TYPE_CTRL_DYN = 0,	///< dynamic assigned read data entries
	BTN_NVME_TYPE_CTRL_RDONE,	///< nvme transferring done data entries
	BTN_NCB_TYPE_CTRL_GC_DYN,	///< dynamic assigned for GC read data entries
	BTN_NCB_TYPE_CTRL_TBL_DYN,	///< dynamic assigned for table read data entries
	BTN_NCB_TYPE_CTRL_PREASSIGN,	///< pre-assigned read data entries
	BTN_NCB_TYPE_CTRL_PRE_RD_DYN,	///< dynamic assigned for pre-read data entries
	BTN_HMB_TYPE_CTRL_XFER		///< HMB operation
};

/*! @brief type_ctrl: (fw -> ncb/nvme) */
enum data_entry_qid_type_ctrl_t {
	BTN_NCB_QID_TYPE_CTRL_NRM = (0 << 2),	///< program: common free,
	BTN_NCB_QID_TYPE_CTRL_HMB = (1 << 2),	///< HMB ?
	BTN_NCB_QID_TYPE_CTRL_DROP = (2 << 2),	///< program/read: drop data entry
	BTN_NCB_QID_TYPE_CTRL_DONE = (3 << 2),	///< program: return in pdone, read: return in common free
	BTN_NCB_QID_TYPE_CTRL_SEMI_STREAM = (10 << 2),		///< program: semi stream
	BTN_NCB_QID_TYPE_CTRL_DDR_COPY_SEMI_STREAM = (11 << 2),	///< program: semi stream with DDR copy
	BTN_NCB_QID_TYPE_CTRL_GC_DONE = (12 << 2),	///< program: GC write done
	BTN_NCL_QID_TYPE_MASK = 0x3C,

	//BTN_NCL_QID_TYPE_CTRL_GRP0 = (0 << 5),// TODO Need confirm with IG
	//BTN_NCL_QID_TYPE_CTRL_GRP1 = (1 << 5),// TODO Need confirm with IG

	BTN_NVME_QID_TYPE_CTRL_NRM = 0,
	BTN_NVME_QID_TYPE_CTRL_DROP = 1,
	BTN_NVME_QID_TYPE_CTRL_RDONE = 2
};

/*!
 * @brief BM payload for data entry
 */
typedef union {
	u64 all;
	struct {
		u64 dtag       : 24;	///< dtag id
		u64 du_ofst    : 12;	///< du offsets in CMD
		u64 btag       : 12;	///< BTN command TAG, assigned by FW
		u64 nvm_cmd_id : 10;	///< NVM_CMD_ID, assigned by NVM-block
		u64 type_ctrl  :  6;	///< refer to usage
	} pl;
	struct {
		u32 dw0;
		u32 dw1;
	};
	struct {
		u64 dtag       : 24;	///< dtag id
		u64 hmb_ofst_p1: 12;	///< hmb offsets lower part
		u64 btag       : 12;	///< BTN command TAG, assigned by FW
		u64 hmb_ofst_p2:  4;	///< hmb offsets higher part
		u64 reservd    :  6;
		u64 type_ctrl  :  6;
	} hpl;

	/* come from Link list WD_ENTRY_ERROR_LLIST*/
	struct {
		u64 dtag       : 24;
		u64 du_ofst    : 12;	///< du offsets in CMD
		u64 rsvd       : 27;
		u64 type       : 1;		///< poison wr-data-entry, bit[63] is set to 1. force NVM release bit[63] is always 0.
	} wr_err;

	/* come from Link list WD_ENTRY_ERROR_LLIST, force mode*/
	struct {
		u64 dtag       : 24;
		u64 du_ofst    : 12;	///< du offsets in CMD
		u64 nvm_cmd_id : 10;	///< cmd_proc cmd slot
		u64 rsvd       : 18;
	} wr_err_f;	//force mode, eg, prp

	/* come from Link list WD_ENTRY_ERROR_LLIST, poison mode*/
	struct {
		u64 dtag       : 24;
		u64 du_ofst    : 12;	///< du offsets in CMD
		u64 btag       : 12;	///< BTN command TAG, assigned by FW
		u64 nvm_cmd_id : 10;	///< cmd_proc cmd slot
		u64 rsvd       : 6;
	} wr_err_p;	//poison mode, eg, PI

	/* come from Link list PROBLEMATIC_READ_DATA_ENTRY_LLIST */
	struct {
		u64 dtag       : 24;
		u64 du_ofst    : 12;	///< du offsets in CMD
		u64 btag       : 12;	///< cmd_proc cmd slot
		u64 nvm_cmd_id : 10;	///< cmd_proc cmd slot
		u64 rsvd       : 6;
	} rd_err;

	struct {
		u64 semi_sram  : 9;	///< dtag id in sram
		u64 semi_ddr   : 15;	///< dtag id in ddr
		u64 du_ofst    : 12;	///< du offsets in CMD
		u64 btag       : 12;	///< BTN command TAG, assigned by FW
		u64 nvm_cmd_id : 10;	///< NVM_CMD_ID, assigned by NVM-block
		u64 type_ctrl  :  6;	///< refer to usage
	} semi_ddr_pl;
} bm_pl_t;

/*!
 * @brief data tag linked-list group selection item
 */
enum dtag_llist_sel {
	FREE_WR_DTAG_LLIST = 0,		///< host free write dtag linked list
	FREE_NRM_HST_RD_DTAG_LLIST,	///< host free read dtag linked list
	FREE_AURL_RD_DTAG_LLIST,	///< auto release free dtag linked list
	FREE_FW_GC_RD_DTAG_LLIST,	///< firmware free dynamic GC read dtag linked list
	FREE_FW_RD_TBL_DTAG_LLIST,	///< firmware free dynamic read table linked list

	FREE_DTAG_RD_HMB_LLIST = 6,	///< auto-recycle DTAG list for read from HMB (HMB->NAND)
	FREE_FW_PRE_READ_LLIST = 7,
	FREE_DTAG_INT_RD_HMB_LLIST,	///< auto-recycle DTAG list for data from nand to HMB (NAND->HMB)
	FREE_DTAG_NVM_WR_HMB_LLIST,	///< not used
	FREE_SEMI_STRM_LLIST0 = 0xA,
	FREE_SEMI_STRM_LLIST1 = 0xB,
};

/*!
 * @brief wr-data-entry linked-list group selection item
 */
enum wd_entry_llist_sel {
	NRM_WD_ENTRY_LLIST = 0,	///< normal write data entry linked list
	PART_WD_ENTRY_LLIST,	///< partial write data entry linked list
	CMP_WD_ENTRY_LLIST,	///< compare write data entry linked list
	PDONE_WD_ENTRY_LLIST,	///< pdone write data entry linked list
	WD_ENTRY_ERROR_LLIST,	///< write error data entry linked list
	READ_FROM_HMB_WR_ENTRY_LLIST
};

/*!
 * @brief rd-data-entry linked-list group selection item
 */
enum rd_entry_llist_sel {
	HOST_NRM_RD_DATA_ENTRY_LLIST = 0,	///< host read data entry linked list
	HOST_RD_AURL_DATA_ENTRY_LLIST,		///< host auto release data entry linked list
	FW_RD_DATA_ENTRY_LLIST,			///< firmware read data entry linked list
	FW_RD_TABLE_ENTRY_LLIST,		///< firmware read table entry linked list
	FW_RD_PRE_ASSIGN_DTAG_DATA_ENTRY_LLIST,	///< firmware pre-assigned read data entry linked list
	FW_RD_ADJ_FREE_DTAG_DATA_ENTRY_LLIST,	///< firmware readjust free linked list
	FW_RD_ADJ_DROP_DTAG_DATA_ENTRY_LLIST,	///< firmware readjust drop linked list
	FW_RD_ADJ_FULL_DTAG_DATA_ENTRY_LLIST,	///< firmware readjust full linked list
	RDONE_DATA_ENTRY_LLIST,			///< rdone data entry linked list
	PROBLEMATIC_READ_DATA_ENTRY_LLIST,
};

/*!
 * @brief Fetch Security Subsystem Chip Finger Print (CFP) from selected
 * memory to its register.
 *
 * Fetch CFP from select 0 (memory from 0xC00421E0 to 0xC00421FF) or
 *		  select 1 (memory from 0xC00421C0 to 0xC00421DF) to its
 *		  register.
 *
 * @param select[in]	0 - offset 0x1E0 to 0x1FF
 *			1 - offset 0x1C0 to 0x1DF
 *
 * @return	1 - CFP fetch successful.
 *		0 - CFP fetch failed - CFP not programmed.
 */
extern u32 bm_fetch_ss_cfp(u8 select);

/*!
 * @brief Set secure mode
 *
 * Program to set secure mode.
 *
 * @param mode[in]	secure or set mode bits. 1 to set, 0 to clear
 *			bit 0 secure mode; bit 1 test mode.
 *
 * @return		not used
 */
extern void bm_set_secure_mode(u32 mode);

/*!
 * @brief clear secure mode and test mode
 *
 * Program to clear secure mode and test mode.
 *
 * @return		not used
 */
extern void bm_clear_secure_mode(void);

/*!
 * @brief Program security subsystem chip finger print (CFP) for select 0 or 1.
 *
 * Program CFP for select 0 (memory from 0xC00421E0 to 0xC00421FF) or
 *		   select 1 (memory from 0xC00421C0 to 0xC00421DF).
 *
 * @param select[in]	0 - offset 0x1E0 to 0x1FF
 *                      1 - offset 0x1C0 to 0x1DF
 *
 * @return	1 - CFP successful.
 *		2 - CFP programmed previously.
 */
extern u32 bm_program_ss_cfp(u8 select);

/*!
 * @brief push single prepared read entry to read adjust queue
 *
 * @param bm_pl		bm payload to be pused, it should be well prepared, btag, ofst, nvm_cmd_id, dtag, type_ctrl
 *
 * @return		not used
 */
extern void bm_rd_de_push(bm_pl_t *bm_pl);

/*!
 * @brief push single entry read adjust DROP/FREE queue
 *
 * Build a data entry and push it to read adjust queue. If this dtag was VTAG,
 * FREE queue will be used, otherwise DROP queue will be used.
 *
 * @param du_ofst	du offset of data entry
 * @param ctag		ctag of data entry
 * @param dtag		dtag of data entry
 *
 * @return		not used
 */
extern void bm_rd_dtag_commit(u32 du_ofst, u32 ctag, dtag_t dtag);

/*!
 * @brief push read error data entry to BTN
 *
 * @param du_ofst	du offset of data entry
 * @param btag		btag of data entry
 *
 * @return		not used
 */
extern void bm_err_commit(u32 du_ofst, u32 btag);

extern void rc_reg_ecct(bm_pl_t *bm_pl, u8 type);

extern void retry_get_lda_do_rewrite(bm_pl_t *bm_pl);

extern lda_t host_rd_get_lda(u16 btag, u16 du_ofst, pda_t pda);

/*!
 * @brief push free dtags into host free write linked list
 *
 * Command processor will fetch free dtags in this queue to receive host
 * write data. The write data entry will be returned to NRM_WD_ENTRY_LLIST.
 *
 * @param dtags		push free dtags into host free write queue.
 * @param count		dtags list length
 *
 * @return		not used
 */
extern void bm_free_wr_load(dtag_t *dtags, u32 count);

/*!
 * @brief allocate and push free dtag for HMB dtag dynamic assign
 *
 * @return		not used
 */
extern void bm_free_hmb_load(void);

/*!
 * @brief pop all dtag for HMB dtag and release them
 *
 * @return		not used
 */
extern void bm_free_hmb_pop(void);

/*!
 * @brief push dummy dtag into host free write linked list to abort a write request
 *
 * To abort a write request, we insert dummy dtags to make it completed from cmd_proc
 *
 * @param dtag		dummy dtag
 * @param count		abort dtag count
 *
 * @return		how many dummy dtags were pushed into BM
 */
extern u32 bm_abort_free_wr_load(dtag_t dtag, u32 count);

/*!
 * @brief push free dtags into host free read linked list
 *
 * NCB will fetch free dtags in this queue to receive host read data in NAND.
 * The read data entry will be returned to HOST_NRM_LLIST.
 *
 * @param dtags		push free dtags into host free read queue.
 * @param count		dtags list length
 *
 * @return		not used
 */
extern void bm_free_rd_load(dtag_t *dtags, u32 count);

/*!
 * @brief push free dtags into firmware free GC read data linked list
 *
 * NCB will fetch free dtags in this queue to receive firmware read data in
 * NAND. The read data entry will be returned to FW_DAT_LLIST.
 *
 * @param dtags		push free dtags into firmware free read data queue.
 * @param count		dtags list length
 *
 * @return		not used
 */
extern void bm_free_fr_gc_load(dtag_t *dtags, u32 count);

/*!
 * @brief push free dtags into firmware free pre-read data linked list
 *
 * NCB will fetch free dtags in this queue to receive firmware read data in
 * NAND. The read data entry will be returned to FW_DAT_LLIST.
 *
 * @param dtags		push free dtags into firmware free read data queue.
 * @param count		dtags list length
 *
 * @return		not used
 */
extern void bm_free_fr_pre_load(dtag_t *dtags, u32 count);

/*!
 * @brief push free dtags into firmware free read table linked list
 *
 * NCB will fetch free dtags in this queue to receive firmware read table in
 * NAND. The read data entry will be returned to FW_TBL_LLIST.
 *
 * @param dtags		push free dtags into firmware free read table queue.
 * @param count		dtags list length
 *
 * @return		not used
 */
extern void bm_free_frt_load(dtag_t *dtags, u32 count);

/*!
 * @brief push free dtags into auto release free read data linked list
 *
 * NCB will fetch free dtags in this queue to receive host read data in
 * NAND, and return it to command processor directly. When data was transfered
 * to host, the data entry will be back to auto release free read queue
 * automatically, there is not any software overhead.
 *
 * @param dtags		push free dtags into auto release free read queue.
 * @param count		dtags list length
 *
 * @return		not used
 */
extern void bm_free_aurl_load(dtag_t *dtags, u32 count);

/*!
 * @brief push free dtags into semi write link list
 *
 * @param dtags		free dtags to be pushed
 * @param count		count of dtag
 * @param id		0 or 1
 */
extern void bm_free_semi_write_load(dtag_t *dtags, u32 count, u32 id);

/*!
 * @brief push free dtags into auto release free read data linked list because read error
 *
 * @param dtags		push free dtags into auto release free read queue.
 * @param count		dtag list length
 *
 * @reutnr		not used
 */
extern void bm_free_aurl_return(dtag_t *dtags, u32 count);

/*!
 * @brief push host read data entries into host read adjust queue
 *
 * Command processor will fetch read data entry in read adjust queue, and
 * transfer data to host. After data transfered, the data entry will be returned
 * to common free dtag queue by default. In design, you could change register
 * which dtag entry was pushed to.
 * FW_RADJ_FREE_LLIST_PUSH: return to common free, only dtag.
 * FW_RADJ_DROP_LLIST_PUSH: not returned, silence mode.
 * FW_RADJ_FULL_LLIST_PUSH: returned to PDONE queue, full data entry.
 *
 * @param pl		push read data entries into host read adjust queue
 * @param count		pl list length
 *
 * @return		not used
 */
extern void bm_radj_push_rel(bm_pl_t *pl, u32 count);

/*!
 * @brief enable unmapped/dummy VTAG in BM
 *
 * BTN support unmapped VTAG for read, and dummy VTAG for program. It will send
 * fixed pattern.
 *
 * @param enable	true for enable
 * @param pattern	unmapped/dummy pattern
 * @param idx		which unmapped was setup, we have two unmap read register
 *
 * @return		not used
 */
extern void bm_unmap_ctrl(bool enable, u32 pattern, u32 idx);

/*!
 * @brief switch if btn wait for semi sram dtag
 *
 * @param wait		true to wait for semi sram dtag
 *
 * @return		not used
 */
extern void btn_semi_wait_switch(bool wait);

/*!
 * @brief enable semi-write,
 *
 * @param 		if enable ddr copy or not
 *
 * @return		always return 0 now
 */
extern u32 btn_semi_write_ctrl(bool ddr_copy);

/*!
 * @brief switch btn semi mode
 *
 * @param ddr_copy		true if enable ddr copy
 *
 * @return		not used
 */
extern void btn_semi_mode_switch(bool ddr_copy);

/*!
 * @brief get current semi sram dtag count
 *
 * @return		return sram dtag count in semi linked list
 */
extern u32 btn_semi_sram_cnt(void);

/*!
 * @brief BTN module(s) initialization that even FTL/NCB can initialize those sub-modules
 *
 * Initialize BTN
 *
 * @return	not used
 */
extern void btn_init(void);

#if defined(HMETA_SIZE)

/*!
 * @brief get PI from SRAM
 *
 * @param dtag dtag
 * @param pi   pointer to a buffer to store this DTAG's PI
 *
 * @return 	not used
 */
extern void btn_get_hmeta(dtag_t dtag, void *hmeta);
extern void btn_set_hmeta(dtag_t dtag, void *hmeta);
#endif //HMETA_SIZE

/*!
 * @brief pop free dtag from one linked list of free dtag group
 *
 * An interface to recycle dtags in hardware linked list of free dtag group.
 *
 * @param sel	which free dtag linked list to be popped
 *
 * @return	Popped dtag
 */
extern dtag_t bm_pop_dtag_llist(enum dtag_llist_sel sel);

/*!
 * @brief pop write data entry from one linked list of write group
 *
 * An interface to recycle dtags in hardware linked list of write group.
 *
 * @param sel	which write data entry linked list to be popped
 *
 * @return	Popped dtag
 */
extern dtag_t bm_pop_wd_entry_llist(enum wd_entry_llist_sel sel);

/*!
 * @brief dump last DPE command
 *
 * @param id	not used
 */
extern void bm_dpe_dump_last_cmd(u32 id);

/*!
 * @brief let bm to handle pre assign read queue, called in sync patch
 */
extern void bm_handler_pre_assign(void);

#if !defined(DISABLE_HS_CRC_SUPPORT)
/*!
 * @brief read host crc from bm register by input address
 *
 * @param dtag	dtag
 * @param off	LBA offset in LDA
 *
 * @return	hcrc value
 */
extern u16 btn_hcrc_read(dtag_t dtag, u32 off);

/*!
 * @brief write host crc into hcrc memory by input address and 16-bit hcrc to bm register
 *
 * @param dtag	dtag id to be set hcrc
 * @param off	lba offset in dtag
 * @param hcrc	calculated crc for write into crc buffer
 *
 * @return	not used
 */
extern void btn_hcrc_write(dtag_t dtag, u32 off, u16 hcrc);
#endif //DISABLE_HS_CRC_SUPPORT

/* Events exported to external modules */
extern u8 evt_wd_grp0_nrm_par_upt;
extern u8 evt_wd_grp0_cmp_upt;
extern u8 evt_wd_grp0_pdone_upt;

extern u8 evt_com_free_upt;		///< common free queue updated event
extern u8 evt_rd_ent_upt;		///< read entry updated event
extern u8 evt_wd_err_upt;		///< write error queue updated event
extern u8 evt_rd_err;			///< read error queue updated event

extern u8 evt_hmb_rd_upt;		///< normal write queue updated event

/// todo: should be revised to bm_pl
typedef struct {
	union {
		u32 all;
		struct {
			u32 dtag:24;
			u32 lda_ofst_l:8;
		} b;
	} dw0;
	union {
		u32 all;
		struct {
			u32 lda_ofst_h:4;
			u32 btn_cmd_tag:12;
			u32 nvm_cmd_id:10;
			u32 ctrl_type:6;
	/* - btn_ncmd_data_spec_0825 - 1.7.1
	 *  - write data entry ( btn -> fw )
	 *    - rsvd [63:61]
	 *    - type [60:58] 0b00 nvm norma write, 0b01 nvm partial write, 0b10 nvm compare write, 0b11 pdone, 0b100 hmb read back
	 *  - read data entry (ncb -> btn -> fw )
	 *    - ncb  [63]
	 *    - rsvd [62]
	 *    - grp  [61]
	 *    - type [60:58] 0b00 nrm read, 0b01 read data recycle after nvm xfer, 0b10 gc read, 0b11 fw table read dyn, 0b100 pre-assign read, 0b101 pre-read dyn, 0b110 gc read hmb
	 *  - fw push to btn
	 *    - rsvd [63:62]
	 *    - type [60:58] 0b00 xfer then recy to com free, 0b01 xfer then drop, 0b10 xfer then to fw, 0b100 hmb push
	 * - rainier_hw_ncl - 3.2.0
	 *    - rsvd[63]
	 *    - grp[62]
	 *    - qid[61:60] - 0b00 normal mode, 0b01 HMB, 0b10 drop without recycled, 0b11 drop and recycled (rainier_fdma - wr_silent_md)
	 *    - meta_entry_type[59:58]
	 */
		} b;
	} dw1;
} btn_data_entry_t;

/// todo: should be revised to bm_pl
typedef union {
	u64 all;
	struct {
		u64 dtag : 24;	///< dtag offset
		u64 du_ofst : 12;	///< du offset in nvm command
		u64 btag : 12;	///< btn command tag
		u64 nvm_cmd_id : 10;	///< cmd_proc command slot id
		u64 type : 3;		///< type of read entry, refer to enum read_data_entry_type_ctrl_t
		u64 grp_id : 1;		///< group id of data entry
		u64 rsvd : 1;
		u64 ncb_id : 1;		///< from ncb 0 or 1
	} b;
} btn_rd_de_t;

//-----------------------------------------------------------------------------
// btn btag pending list interface
// hold btag in the list when resources are not available
//-----------------------------------------------------------------------------
extern void bm_hmb_req(bm_pl_t *bm_tags, u16 hmb_ofst, bool read);

extern void dpe_handle_incoming(void);
#include "dpe_export.h"

/*! @} */
