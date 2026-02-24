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
/*!
 * \file
 * \addtogroup decoder
 * \defgroup core core
 * \ingroup decoder
 * @{
 */
#pragma once

#include "nvme_cfg.h"
#include "nvme_spec.h"
#include "nvme_apl.h"
//Andy test IOL 5.4 20201013
extern volatile u8 flagtestS;
extern volatile u8 flagtestC;
extern bool dwnld_com_lock;


// #define MAX_LBAF	3

/*!
 * @brief command result
 */
enum cmd_rslt_t {
	HANDLE_RESULT_FINISHED = 0,	///< command finished
	HANDLE_RESULT_FAILURE,		///< command failed
	HANDLE_RESULT_RUNNING,     	///< command is still running
	HANDLE_RESULT_PENDING_FE,	///< pending in the front-end
	HANDLE_RESULT_PENDING_BE,	///< pending in the back-end
	HANDLE_RESULT_DATA_XFER,	///< command is transferring data
	HANDLE_RESULT_PRP_XFER,		///< command is transferring prp list
};

/*!
 * @brief nvme submission queue
 */
struct nvmet_sq {
	u16 cqid;     	/*! associated CQ id */

	enum nvme_qprio qprio;	/*! submission queue priority */

	struct list_head reqs;	/*! running reqs list */

	u32 q_flags;	/*! queue flags */
	u16 head;		/*! head of last sq timer */
	u16 tail;		/*! tail of last sq timer */
};

/*!
 * @brief nvme completion queue
 */
struct nvmet_cq {
	u16 qid;		/*! completion queue id */
};

/*!
 * @brief attributes for nvme namespace
 */
struct nvmet_namespace {
	struct {
		/** metadata size */
		u32 ms : 16;
		/** lba data size */
		u32 lbads : 8;
		/** relative performance */
		u32 rp : 2;
		u32 reserved6 : 6;
	} lbaf_tbl[MAX_LBAF];

	u8 npit;		/* number of pi type support */
	u8 lbaf_cnt;		/*! supported lba format number */

	u64 ncap;		/*! namespace capacity */
	u64 nsze;		/*! namespace size */

	/*
	 * PI information
	 */
	u8 pit;		/*! PI type */
	u8 pil;	/*! metadata start */
	u8 lbaf;	/*! lba format index */
	u8 ms;		/*! metadata separated or extended */

	struct {
		u8 all;
		struct {
			u8 thin_prov : 1; /*! support thin provision */
			u8 atomic : 1;	/*! support atomic feature*/
			u8 err_ctrl : 1; /*! support error ctrl*/
			u8 guid : 1; /*! support guid feature*/
			u8 per_op : 1; /*! support performance optiomize feature*/
			u8 rsv : 3; /*! reserved */
		};
	} nsfeat; /*! ns features ctrl, detail refer idfy cns 00h byte 24 */

	struct {
		u16 nabsn; /*! ns atomic boundary size normal */
		u16 nacwu; /*! ns atomic compare & write unit */
		u16 nabo; /*! ns atomic boundary offset */
		u16 nabspf; /*! ns atomic boundary size power fail */
	} nabp; /*! ns atomic boundary parameter*/

#ifdef NS_MANAGE
	u64 start_lba;	/*! start lba, raw disk level */
#endif

#ifdef MULTI_CTRL
	u8 ana_id; /*! ANA group ID*/
	u8 nmic; /*! namespace multi-path io and sharing capabilities */
#endif
	bool (*issue)(req_t *);	/*! command dispatcher function */
};

/*!
 * @brief attributes for nvme Active Firmware Info
 */
struct nvmet_afi {
	u8	slot		: 3; /* slot for current FW */
	u8 rvsd3 		: 1;
	u8 slot_next 	: 3; /* slot for next FW */
	u8	rvsd7 		: 1;
};

/*!
 * @brief attributes for nvme namespace id
 */
struct nvmet_nsid {
	u8 nsid;
	enum nsid_type type;
	enum nsid_wp_state wp_state;
	struct nvmet_namespace *ns;
};

#ifdef NVM_SET
/*!
* @brief attributes for preditable latency mode
*/
struct pre_lat_mode {
	bool plm_en; // predictable laency enable
	u8 enable_event; // enable event
	u64 dtwin_r_th; // DTWIN reads threshold
	u64 dtwin_w_th;	// DTWIN writes threshold
	u64 dtwin_t_th;	// DTWIN time threshold
};

/*!
* @brief attributes for nvm set id
*/
struct nvmet_nvm_set {
	u8 rrl; // read recovery level
	struct pre_lat_mode plm;
	bool asyn_event_cfg_flag;
	u16 nvm_set_id;
	u16 en_grp_id;
	u32 rr_4k_lat;	// random 4 kib read typical
	u32 opt_w_size;	// optimal write size
	u64 ns_cap_h;	// nvm set capacity high 64 byte, in bystes.
	u64 ns_cap_l;	// nvm set capacity low 64 byte, in bystes.
	u64 ns_unaloc_cap_h;	// nvm set unallocated capacity high 64 byte, in bytes.
	u64 ns_unaloc_cap_l;	// nvm set unallocated capacity low 64 byte, in bytes.
	//struct nvmet_nsid *nsid;
};
#endif

/*!
* @brief attributes for endurance group id
*/
#ifdef ENDURANCE_GROUP
struct nvmet_en_grp {
	union {
		u8 all;
		struct {
			u8 sp_cap_warning : 1;	// bit0 set to 1, the available spare capacity of th EG has fallen below the threshold.
			u8 rsv: 1;
			u8 reliability_warning : 1;	// bit2 set to 1, the EG reliability has been degraded
			u8 read_only_mode : 1;
		} b;
	} critial_warning;	// critical warning

	u8 avaliable_spare;	// percentage
	u8 available_spare_th;	// available spare threshold
	u8 percentage_used;	// the percentage of life used
	u64 endurance_estimate_h;	// total number of data bytes high
	u64 endurance_estimate_l;	// total number of data bytes low
	u64 data_unit_r_h;	// data units read high
	u64 data_unit_r_l;	// data units read low
	u64 data_unit_w_h;	// data units write high
	u64 data_unit_w_l;	// data units write low
	u64 media_unit_w_h;	// media units write high
	u64 media_unit_w_l;	// media units write low
	u64 host_r_cmd_h;	// host read commands completed
	u64 host_r_cmd_l;	// host read commands completed
	u64 host_w_cmd_h;	// host write commands completed
	u64 host_w_cmd_l;	// host write commands completed
	bool eg_event_cfg_flag;

	struct nvmet_nvm_set *nvm_set;
};
#endif

#if defined(SRIOV_SUPPORT)
/*!
 * @brief attributes for Virtual Function
 */
/*struct nvmet_virtual_function {
	u16 vq_count		:8;
	u16 vi_count		:7;
	u16 ctrl_state		:1;
};*/

/*!
 * @brief enumeration for seconday controller state
 */
enum sec_ctrl_state {
	SEC_CTRL_STATE_OFFLINE = 0,	///< offline state
	SEC_CTRL_STATE_ONLINE,		///< online state
	SEC_CTRL_STATE_CONFIGURED	///< configured state
};

/*!
 * @brief This structure is used to manage flexible queue resources (fqr)
 *	This is used to set, unset, bind and unbind SQ/CQ.
 */
struct nvmet_fqr_sq_cq {
	struct nvmet_cq     *fqrcq[SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC];
	struct nvmet_sq     *fqrsq[SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC];
};

/*!
 * @brief attributes for SR_IOV
 */
struct nvmet_sr_iov {
	struct nvmet_fqr_sq_cq fqr_sq_cq[SRIOV_VF_PER_CTRLR]; /* VF1 to VF32 */

	u8 sq_count[SRIOV_VF_PER_CTRLR];
	u8 cq_count[SRIOV_VF_PER_CTRLR];
	u8 vi_count[SRIOV_VF_PER_CTRLR];

	u8 ctrl_state[SRIOV_VF_PER_CTRLR+1]; /* for PF and VF */

	u8 vq_flex_strt_idx[SRIOV_VF_PER_CTRLR]; /* vq start index */
	u8 vi_flex_strt_idx[SRIOV_VF_PER_CTRLR]; /* vi start index */

	/* Monitor the flexible queue resources */
	u8 sec_vq_count;
	u8 pri_vq_count;

	/* Monitor the flexible interupt resources */
	u8 sec_vi_count;
	u8 pri_vi_count;

	u8 vq_flex_cur_idx; /* vq current index */
	u8 vi_flex_cur_idx; /* vi current index */

	/* To handle AER for VFs */
	u8 aer_evt_bitmap[SRIOV_VF_PER_CTRLR];
	u8 aer_evt_mask_bitmap[SRIOV_VF_PER_CTRLR];
	u16 aer_evt_sts[SRIOV_VF_PER_CTRLR][NUMBER_OF_NVME_EVENT_TYPE];
	u8 aer_outstanding[SRIOV_VF_PER_CTRLR];
	struct list_head aer_outstanding_reqs[SRIOV_VF_PER_CTRLR];

	/* Monitor requests for VFs */
	struct list_head aborted_reqs[SRIOV_VF_PER_CTRLR];
	struct list_head waiting_reqs[SRIOV_VF_PER_CTRLR];
};
#endif

/*! \cond PRIVATE */
#define CTRLR_F_QUEUE_ABORTED    BIT(0)
/*! \endcond */

/*!
 * @brief nvme controller data structure
 */
struct nvmet_ctrlr {
	u8 max_qid; 	/*! up to max_qid */
	u8 last_sqid;	/*! last Submission Queue for arbitration */
	u16 max_ns;	/*! max namspace count, only one */

	u16 page_bits;	/*! controller page size shift */
	u16 page_size;	/*! controller page size */

#define NVME_EVENT_TYPE_MAX_STATUS  16
	u8 aer_evt_bitmap;
	u8 aer_evt_mask_bitmap;
	u16 aer_evt_sts[NUMBER_OF_NVME_EVENT_TYPE];
	u8 aer_outstanding;
	u8 cur_abort_cnt;	/*current running abort cmd count,shayne 2020/8/4*/
	struct list_head aer_outstanding_reqs;

	nvme_ctrl_attr_t attr;

	struct nvmet_feat def_feat;
	struct nvmet_feat cur_feat;
	struct nvmet_feat saved_feat;
	//u64    timestamp;

	struct nvme_ctrlr_data *idtfy;

#if defined(SRIOV_SUPPORT)
	/* Primary (PF0) controller use flexible resource as private resource */
	struct nvmet_cq     *cqs[SRIOV_FLEX_PF_ADM_IO_Q_TOTAL]; /*! created CQ pointer array */
	struct nvmet_sq     *sqs[SRIOV_FLEX_PF_ADM_IO_Q_TOTAL]; /*! created SQ pointer array */
#else
	struct nvmet_cq     *cqs[NVMET_RESOURCES_FLEXIBLE_TOTAL + 1]; /*! created CQ pointer array */
	struct nvmet_sq     *sqs[NVMET_RESOURCES_FLEXIBLE_TOTAL + 1]; /*! created SQ pointer array */
#endif

	/*!
	 * Any NSID is valid, except if it's zero or greater then the
	 * Number of Namespaces field reported in the Identify Controller data structure.
	 */
	struct nvmet_nsid *nsid;

#ifdef NS_MANAGE
	u64 drive_capacity;	// LBA CNT
#endif

	struct nvme_registers *regs;

	struct nvmet_sr_iov *sr_iov;

	u32 flags;

	struct list_head aborted_reqs;	/*! aborted reqs etc. SQ deletion */
	struct list_head waiting_reqs;

	u16 admin_running_cmds;
	u16 cmd_proc_running_cmds;
	u16 free_nvme_cmds;
	u16 currAbortCnt;

	u8 elog_cur;
	u8 elog_valid;
	u64 elog_tot;  	/*! cross power off conditions */
	struct nvme_error_information_entry *elog;
	struct nvme_health_information_page *health;

	bool ns_smart;

	u8 max_fw_slot;
	struct nvmet_afi cur_afi;
	u64 fw_slot_version[MAX_FWSLOT];

	struct nvme_host_memory_attributes hmb_attr;	/*! HMB attributes */
	u32 hmb_cdw11;
	u32 last_ps;
	u32 nvme_req_flush;
};

extern struct nvmet_ctrlr *ctrlr;

// #if CO_SUPPORT_DEVICE_SELF_TEST
typedef struct {
    u32             Tag;
    u32             TotalDSTCnt;
    tDST_LOG_ENTRY  DSTLogEntry[20];
    tDST_LOG_ENTRY  DSTResult;
    u8              DSTErr;
	u64				DSTSpendTime;   //Elapsed time of last extend DST if blocked by controller level reset 
    u8              reserved1[3];
} tDST_LOG;

void DST_CmdAbort(req_t *req, u8 stc);
// #endif


typedef struct
{
	u32 NSID;
	u8 OPC;
	u8 RSVD[3];
} CmdRefInfo_t;

/*!
 * @brief NVMe admin command initialization
 *
 * @return None
 */
void nvmet_admin_cmd_init(void);

/*!
 * @brief NVMe I/O command initialization
 *
 * @return	None
 */
void nvmet_io_cmd_init(void);

/*!
 * @brief abort a request which in pending list
 *
 * @param req	request to be aborted
 *
 * @return	none
 */
void nvmet_abort_pending_req(req_t *req);

/*!
 * @brief get a request from the pool
 *
 * @return 	request resource
 */
req_t *nvmet_get_req(void);

/*!
 * @brief put the request to the pool
 *
 * @param req	the request to be recycled
 *
 * @return 		None
 */
void nvmet_put_req(req_t *req);

/*!
 * @brief set CQ status
 *
 * Set the completion entry status field
 *
 * @param req	which the completion entry attached
 * @param sct	status code type
 * @param sc	status code
 *
 * @return 	None
 */
void nvmet_set_status(fe_t *fe, u8 sct, u8 sc);

/*!
 * @brief setup NVMe controller SQ
 *
 * @param sqid 		SQ id to be setup
 *
 * @return 		None
 */
void nvmet_set_sq(u16 sqid);

/*!
 * @brief unset NVMe controller SQ
 *
 * @param sqid		SQ id to be unset
 *
 * @return 		None
 */
void nvmet_unset_sq(u16 sqid);

/*!
 * @brief setup NVMe controller CQ
 *
 * @param cqid		CQ id to be setup
 *
 * @return 		None
 */
void nvmet_set_cq(u16 cqid);

/*!
 * @brief unset NVMe controller CQ
 *
 * @param cqid		CQ id to be setup
 *
 * @return 		None
 */
void nvmet_unset_cq(u16 cqid);

/*!
 * @brief binding SQ and CQ
 *
 * @param sqid		SQ id to bind
 * @param cqid		CQ id for SQ to be binded
 *
 * @return 		None
 */
void nvmet_bind_cq(u16 sqid, u16 cqid);

/*!
 * @brief unbinding SQ and CQ
 *
 * @param sqid		SQ id to unbind
 * @param cqid		CQ id for SQ to be unbinded
 *
 */
void nvmet_unbind_cq(u16 sqid, u16 cqid);

/*!
 * @brief command done callback handler
 *
 * Admin/IO command process done callback function
 *
 * @param req		the completed request
 *
 * @return		Command process success or not
 */
bool nvmet_core_cmd_done(req_t *req);

/*!
 * @brief return CQ from fw
 *
 * @param fe		fe paramter
 *
 * @return		return false if hw resource is not enough
 */
bool nvmet_core_handle_cq(fe_t *fe);

/*!
 * @brief restart sq timer
 *
 * @return		none
 */
void nvmet_sq_timer_enable(void);

/*!
 * @brief get request elapsed time
 *
 * @param req		request
 *
 * @return		elapsed time in ms
 */
static inline u32 nvmet_req_elapsed_time(req_t *req)
{
	extern u32 jiffies;
	return (jiffies - req->start) * 100;
}

/*!
 * @brief nvme io start
 *
 * @param None
 *
 * @return		None
 */
void nvmet_io_start(void);

/*!
 * @brief nvme io endif
 *
 * @param None
 *
 * @return		None
 */
void nvmet_io_end(void);

#if defined(SRIOV_SUPPORT)
/*!
 * @brief This function set Host Submission Queue ID for nvme submission queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs.
 * @param hst_sqid	Function SQID. For Admin 0, IO > 0
 *
 * @return	none
 */
void nvmet_set_vfsq(u8 fid, u8 hst_sqid);

/*!
 * @brief This function unset Host Submission Queue ID for nvme submission queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs.
 * @param hst_sqid	Function SQID. For Admin 0, IO > 0
 *
 * @return	none
 */
void nvmet_unset_vfsq(u8 fid, u8 hst_sqid);

/*!
 * @brief This function set Host Completion Queue ID for nvme completiton queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 *
 */
void nvmet_set_vfcq(u8 fid, u8 hst_cqid);

/*!
 * @brief This function unset Host Completion Queue ID for nvme completiton queue
 * for PF0 and VF1 to VF32.
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 */
void nvmet_unset_vfcq(u8 fid, u8 hst_cqid);

/*!
 * @brief This function binds host CQ-SQ IDs for PF0 and VFs
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_sqid	Function SQID. For Admin 0; IO > 0
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 */
void nvmet_bind_vfcq(u8 fid, u8 hst_sqid, u8 hst_cqid);

/*!
 * @brief This function unbinds host CQ-SQ IDs for PF0 and VFs
 *
 * @param fid		Function ID. 0 for PF0; 1 to 32 for VFs
 * @param hst_sqid	Function SQID. For Admin 0; IO > 0
 * @param hst_cqid	Function CQID. For Admin 0; IO > 0
 *
 * @return	none
 */
void nvmet_unbind_vfcq(u8 fid, u8 hst_sqid, u8 hst_cqid);
#endif

/*!
 *@brief API to restore HW setting of namespace in CMD_PROC after HW reset
 */
void nvmet_ns_resume(void);

/*!
 * @brief for shutdown, clear IO queue
 *
 * @param fid 		functiuon id
 * @param cmd_proc_del	if need to use cmd_proc delete sq, is's bitmap
 *
 * @return 		not used
 */
void nvmet_clear_io_queue(u8 fid, u32 cmd_proc_del[3]);

void nvmet_abort_running_req(req_t *req);
void reset_sq_arbitration_en(void);

/*!
 * @brief overlapped command done callback handler
 *
 * Admin/IO overlapped command process done callback function
 *
 * @param req		the completed request
 *
 * @return		Command process success or not
 */
bool nvmet_core_cmd_overlapped(req_t *req);
/*! @} */
