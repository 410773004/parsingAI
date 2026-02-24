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
#include "nvme_cfg.h"

/*! \file
 * @brief nvme application layer header
 *
 * \addtogroup decoder
 * @{
 */
#if defined(HMETA_SIZE)
#define MAX_LBAF	4
#define MAX_PI_TYPE_CNT 3
#else
#define MAX_LBAF	2
#define MAX_PI_TYPE_CNT 0
#endif

extern u8 evt_fw_dwnld; 		///< Firmware downloading
extern u8 evt_fw_commit; 		///< Firmware activate
#ifdef POWER_APST
extern u8 evt_change_ps;		///< change power state
#endif

struct _nvme_feature_saved_t;
struct _smart_statistics_t;

typedef union _nvme_ctrl_attr_t {
	struct {
		u32 compare : 1;
		u32 write_uc : 1;
		u32 dsm : 1;
		u32 write_zero : 1;
		u32 set_feature_save : 1;
		u32 ns_mgt : 1;
		u32 timestamp : 1;
	} b;
	u32 all;
} nvme_ctrl_attr_t;

/*!
 * @brief firmware download image state //3.1.7.4 merged 20201201 Eddie
 */
 typedef enum fw_img_st{
	FW_IMG_NRM = 0,
	FW_IMG_OVERLAP,
	FW_IMG_INVALID
 }fw_img_st_t;

/*!
 * @brief enumeration of nvme namespace id type
 */
enum nsid_type {
	NSID_TYPE_UNALLOCATED = 0,		///< unallocated namespace type for NVM Subsystem */
	NSID_TYPE_ALLOCATED,		///< allocated namespace type for NVM Subsystem */
	NSID_TYPE_INACTIVE,		///< inactive namespace type
	NSID_TYPE_ACTIVE,		///< active namespace type
};

/*!
 * @brief enumeration of nvme namespace id write protect state
 */
enum nsid_wp_state {
	NS_NO_WP = 0,		///< not write protected
	NS_IN_WP,		///< the NS is write protected
	NS_WP_ONCE,		///< the NS is write protected until the next power cycle
	NS_WP_PERM,		///< the NS is permanently write protected
};

// new struct introduced for namespace info, would co-exist with old ones for a while
typedef struct {
	u32 ms : 16;	///< metadata size
	u32 lbads : 8;	///< lba data size
	u32 rp : 2;	///< relative performance
} lbaf_entry_t;

typedef struct _nvme_ns_hw_attr_t {
	u8 nsid;			///< nvmespace id
	u8 lbaf;			///< lba format index

	struct {
		u8 wr_prot : 1;		///< write protected state
		u8 pad_pat_sel : 1;	///< select pad pattert for partial write DU
		u8 rsvd : 6;
	};

	u8 pit;			///< PI type
	u8 pil;			///< metadata start
	u8 ms;			///< metadata separated or extended

	u64 lb_cnt;		///< lba space in unit LBA, 1-based
} nvme_ns_hw_attr_t;

typedef struct {
	enum nsid_type type;
	enum nsid_wp_state wp_state;	///< write protect state
	u64 ncap;		///< namespace capacity
	u64 nsz;		///< namespace size
	u64 attached_ctrl_bitmap;

	struct {
		u16 nabsn;	///< NS atomic boundary size normal
		u16 nacwu;	///< NS atomic compare & write unit
		u16 nabo;	///< NS atomic boundary offset
		u16 nabspf;	///< NS atomic boundary size power fail
	} nabp;			///< NS atomic boudary parameter

	union {
		u8 all;
		struct {
			u8 thin_prov : 1;	///< support thin provision
			u8 atomic : 1;		///< support atomic feature
			u8 unmap_abort : 1;	///< support to abort unmap read
			u8 guid : 1;		///< support guid feature
			u8 per_op : 1;		///< support perf optimize
			u8 rsvd : 3;
		};
	} nsfeat;

	u8 support_pit_cnt;	///< how many PI types supported
	u8 support_lbaf_cnt;	///< how many lba formats supported, 1-based

	u32 reserved[8];	///< resserve space
} nvme_ns_fw_attr_t;

typedef struct {		///< only support one lba range per namespace
	u32 start_lda;		///< start LDA of one namespace
	u32 len;		///< LDA count of one namespace
} nvme_ns_space_info_t;

// main context struct of individual namespace, need save to nand upon shutdown
typedef struct {
	nvme_ns_hw_attr_t hw_attr;		///< HW setting related
	u32 rsvd0[8];
	nvme_ns_fw_attr_t fw_attr;		///< NS info FW maintain
	u32 rsvd1[8];
	nvme_ns_space_info_t space_info;	///< namespace occupied lba space info
	u32 rsvd2[16];				///< add 64 bytes reserved space
} nvme_ns_attr_t;

// make NVME-controller-level variable visiable among files of NVME module
extern lbaf_entry_t _lbaf_tbl[];

/*!
 * @brief get one ns_info slot by its nsid
 */
extern nvme_ns_attr_t *get_ns_info_slot(u32 nsid);

/*!
 * @brief configure register set of namespace in CMD_PROC
 */
extern void cmd_proc_ns_cfg(nvme_ns_hw_attr_t *attr, u32 meta_size, u32 lbas_shift);


/*!
 * @brief set nvme contrller attributes
 *
 * @param ctrl_attr	plz refer to nvme_ctrl_attr_t
 *
 * @return		not used
 *
 */
extern void nvmet_set_ctrlr_attrs(nvme_ctrl_attr_t *ctrl_attr);


/*!
 * @brief Set namespace attributes.
 *
 * Set Namespace attributes, etc. Cap/Format/Lbads
 * The function can be invoked by Host(via create/delete NS) or
 * Ramdisk/Rawdisk/FTL
 *
 * @param attr			point to a struct used to update one NS info
 * @param is_power_on		indicates if it's called in power-on init process
 *
 * @return 		Error code
 */
extern int nvmet_set_ns_attrs(nvme_ns_attr_t *attr, bool update_reg);
#ifdef NS_MANAGE
extern int nvmet_set_ns_attrs_init(nvme_ns_attr_t *attr, bool is_power_on);//joe add 202009
#endif
#ifdef NS_MANAGE
/*!
 * @brief admin set drive capacity
 *
 * @param	drive capacity, in GB
 *
 */
extern void nvmet_set_drive_capacity(u64 capacity);
#endif

/*!
 * @brief set nvme FW slot revision info.
 *
 * @param slot		fw slot
 * @param revision	fw revision info
 *
 * @return 		not used
 */
extern void nvmet_set_fw_slot_revision(u8 slot, u64 revision);

/*!
 * @brief update nvme feature.
 *
 *
 * @param to_save_feat		nvme feature struct
 *
 * @return 			not used
 */
extern void nvmet_update_feat(struct _nvme_feature_saved_t *to_save_feat);

/*!
 * @brief restore nvme feature.
 *
 *
 * @param saved_feat		nvme feature struct
 *
 * @return 			not used
 */
extern void nvmet_restore_feat(struct _nvme_feature_saved_t *saved_feat);

/*!
 * @brief re-inition nvme feature from saved to cur
 *
 * @param 	not used
 *
 * @return 	not used
 */
extern void nvmet_reinit_feat(void);

/*!
 * @brief restore smart statistics information and increase power-on count
 *
 * @param smart		smart statistics information
 *
 * @return	not used
 */
extern void nvmet_restore_smart_stat(struct _smart_statistics_t *smart);

#if PLP_SUPPORT == 0
/*!
 * @brief update and flush smart information once 10 minutes
 *
 * @return	not used
 */
extern void nvmet_update_smart_timer(void *data);
#endif

/*!
 * @brief update smart statistics information
 *
 * @param smart		smart statistics information
 *
 * @return	not used
 */
extern void nvmet_update_smart_stat(struct _smart_statistics_t *smart);

/*!
 * @brief disable admin/io command queue fetch
 *
 * @param stop		if true to disable command fetch
 *
 * @return		not used
 */
extern void nvmet_io_fetch_ctrl(bool stop);

/*!
 * @brief return current host pending command count, AER was not included
 *
 * @return		outstanding command count
 */
extern int nvmet_cmd_pending(void);

/*!
 * @brief return last Operational Power States for Autonomous Power State Transitions
 *
 * @return		not used
 */
 #ifdef POWER_APST
extern void apst_transfer_to_last_ps(void);
#endif
/*!
 * @brief  BTN NVMe I/O SQ commands done handler
 *
 * @param btag		btn tag
 * @param ret_cq	need FW return CQ
 * @param cid		NVM command id
 *
 * @return		not used
 */
extern void nvmet_core_btn_cmd_done(int btag, bool ret_cq, u32 cid);

extern u8 evt_abort_wr_req;

extern void AplAdminCmdRelease(void);
/*! @} */
