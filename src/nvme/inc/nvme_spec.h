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

/**
 * \file
 * NVMe specification definitions
 */

#pragma once

#include "stat_head.h"
#include "nvme_cfg.h"
#include "nvme_apl.h" //joe add 20200810 //joe add NS 20200813

/**
 * Use to mark a command to apply to all namespaces, or to retrieve global
 *  log pages.
 */
#define NVME_GLOBAL_NS_TAG ((u32)0xFFFFFFFF)

#define NVME_MAX_IO_QUEUES (65535)

#define NVME_ADMIN_QUEUE_MIN_ENTRIES 2
#define NVME_ADMIN_QUEUE_MAX_ENTRIES 4096

#define NVME_IO_QUEUE_MIN_ENTRIES 2
#define NVME_IO_QUEUE_MAX_ENTRIES 65536

#define NVME_MAX_DATA_TRANSFER_SIZE 6 // 5->6 For nvme spec NVMe-CFG-2  eric 20230619

#define NVMET_RESOURCES_FLEXIBLE_TOTAL 66
//#if(PLP_SUPPORT == 1)
#if PLP_SUPPORT == 1
#define SMART_PLP_NOT_DONE
#endif
#if defined(SRIOV_SUPPORT)

/* To support more VFs, change "SRIOV_VF_PER_CTRLR" to a new value.
 * But controller should have enough resources (ex: nvme buffer, request buffer etc.)
 * Currently set to 1 to test. This may be changed to 2, 4, 8 and so on.
 * For fixed resources, when changing it, the "SRIOV_FLEX_VF_IO_Q_PER_FUNC" should be
 * changed accordingly. Please see the example shown below.
 *---------------------------------------------------------------------------------------
 *    SRIOV_VF_PER_CTRLR   SRIOV_FLEX_VF_IO_Q_PER_FUNC  VF1 Queues   VF2 Queues
 *---------------------------------------------------------------------------------------
 *            1                         8                0 - Admin
 *                                                       1-8 IO
 *---------------------------------------------------------------------------------------
 *            2                         4                0 - Admin     5 - Admin
 *                                                       1-4 IO        6-9 IO
 *---------------------------------------------------------------------------------------*/
#define SRIOV_PF_PER_CTRLR (1)
//#define SRIOV_VF_PER_CTRLR		(1)
#define SRIOV_VF_PER_CTRLR (2)
//#define SRIOV_VF_PER_CTRLR		(4)
//#define SRIOV_VF_PER_CTRLR		(8)
#define SRIOV_PF_VF_PER_CTRLR (SRIOV_PF_PER_CTRLR + SRIOV_VF_PER_CTRLR)

#define SRIOV_FLEX_PRIV_PF_IO_Q (8)
#define SRIOV_FLEX_PRIV_PF_ADM_Q (1)
/* TODO: Since cap_idtfy->crt = BIT1|BIT0, SRIOV_FLEX_PF_IO_Q the must be non-zero?? */
#define SRIOV_FLEX_PF_IO_Q (0)
#define SRIOV_PRIV_PF_ADM_IO_Q_TOTAL (SRIOV_FLEX_PRIV_PF_ADM_Q + SRIOV_FLEX_PRIV_PF_IO_Q)
#define SRIOV_FLEX_PF_ADM_IO_Q_TOTAL (SRIOV_PRIV_PF_ADM_IO_Q_TOTAL + SRIOV_FLEX_PF_IO_Q)

/* To work with fixed resources, this can be changed to 4, 2 and 1 */
#define SRIOV_FLEX_VF_IO_Q_PER_FUNC (8 / SRIOV_VF_PER_CTRLR)

#define SRIOV_FLEX_VF_ADM_Q_PER_FUNC (1)
#define SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC (SRIOV_FLEX_VF_ADM_Q_PER_FUNC + SRIOV_FLEX_VF_IO_Q_PER_FUNC)

#define SRIOV_FLEX_VF_Q_PER_CTRLR (SRIOV_VF_PER_CTRLR * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC)
#define SRIOV_FLEX_Q_PER_CTRLR (SRIOV_FLEX_PF_IO_Q + SRIOV_FLEX_VF_Q_PER_CTRLR)

#define SRIOV_PF_VF_Q_PER_CTRLR (SRIOV_FLEX_PF_ADM_IO_Q_TOTAL + SRIOV_FLEX_VF_Q_PER_CTRLR)

#endif //SRIOV_SUPPORT

#define	NVME_ALL_FUNCTIONS	 0xff
enum {
	NVME_ADM_RX_CMD = 0x0,
	NVME_RX_IO_CMD,
	NVME_IO_WR_CMD,
	NVME_IO_RD_CMD,
	NVME_IO_NO_WRT_CMD,
	NVME_IO_RDWR_CMD,
	NVME_ALL_CMD,
	NVME_DST_ALL_CMD,
	NVME_ADMIN_CMD,
};

/**
 * Indicates the maximum number of range sets that may be specified
 *  in the dataset mangement command.
 */
#define NVME_DATASET_MANAGEMENT_MAX_RANGES 256

union nvme_cap_register
{
	u64 raw;
	struct
	{
		/** maximum queue entries supported */
		u32 mqes : 16;

		/** contiguous queues required */
		u32 cqr : 1;

		/** arbitration mechanism supported */
		u32 ams : 2;

		u32 reserved1 : 5;

		/** timeout */
		u32 to : 8;

		/** doorbell stride */
		u32 dstrd : 4;

		/** NVM subsystem reset supported */
		u32 nssrs : 1;

		/** command sets supported */
		u32 css : 8;

		/** Boot Partition Support */
		u32 bps : 1;

		u32 reserved2 : 2;

		/** memory page size minimum */
		u32 mpsmin : 4;

		/** memory page size maximum */
		u32 mpsmax : 4;

		u32 reserved3 : 8;
	} bits;
};

union nvme_cc_register
{
	u32 raw;
	struct
	{
		/** enable */
		u32 en : 1;

		u32 reserved1 : 3;

		/** i/o command set selected */
		u32 css : 3;

		/** memory page size */
		u32 mps : 4;

		/** arbitration mechanism selected */
		u32 ams : 3;

		/** shutdown notification */
		u32 shn : 2;

		/** i/o submission queue entry size */
		u32 iosqes : 4;

		/** i/o completion queue entry size */
		u32 iocqes : 4;

		u32 reserved2 : 8;
	} bits;
};

enum nvme_shn_value
{
	NVME_SHN_NORMAL = 0x1,
	NVME_SHN_ABRUPT = 0x2,
};

union nvme_csts_register
{
	u32 raw;
	struct
	{
		/** ready */
		u32 rdy : 1;

		/** controller fatal status */
		u32 cfs : 1;

		/** shutdown status */
		u32 shst : 2;

		/** nvm subsystem occurs */
		u32 nssro : 1;

		/** Processing Paused */
        	u32 pp        : 1;
		
		u32 reserved1 : 26;
	} bits;
};

enum nvme_shst_value
{
	NVME_SHST_NORMAL = 0x0,
	NVME_SHST_OCCURRING = 0x1,
	NVME_SHST_COMPLETE = 0x2,
};

union nvme_aqa_register
{
	u32 raw;
	struct
	{
		/** admin submission queue size */
		u32 asqs : 12;

		u32 reserved1 : 4;

		/** admin completion queue size */
		u32 acqs : 12;

		u32 reserved2 : 4;
	} bits;
};

union nvme_vs_register
{
	u32 raw;
	struct
	{
		/** indicates the tertiary version */
		u32 ter : 8;
		/** indicates the minor version */
		u32 mnr : 8;
		/** indicates the major version */
		u32 mjr : 16;
	} bits;
};

#define BUILD_NVME_VERSION(mjr, mnr, ter) \
	(((u32)(mjr) << 16) |                 \
	 ((u32)(mnr) << 8) |                  \
	 (u32)(ter))

union nvme_cmbloc_register
{
	u32 raw;
	struct
	{
		/** indicator of BAR which contains controller memory buffer(CMB) */
		u32 bir : 3;
		u32 reserved1 : 9;
		/** offset of CMB in multiples of the size unit */
		u32 ofst : 20;
	} bits;
};

union nvme_cmbsz_register
{
	u32 raw;
	struct
	{
		/** support submission queues in CMB */
		u32 sqs : 1;
		/** support completion queues in CMB */
		u32 cqs : 1;
		/** support PRP and SGLs lists in CMB */
		u32 lists : 1;
		/** support read data and metadata in CMB */
		u32 rds : 1;
		/** support write data and metadata in CMB */
		u32 wds : 1;
		u32 reserved1 : 3;
		/** indicates the granularity of the size unit */
		u32 szu : 4;
		/** size of CMB in multiples of the size unit */
		u32 sz : 20;
	} bits;
};

enum nvme_brs
{
	NVME_BP_BRS_NOOP = 0,
	NVME_BP_BRS_AIP,
	NVME_BP_BRS_CMPL,
	NVME_BP_BRS_ERR,
};

union nvme_bpinfo_register
{
	u32 raw;
	struct
	{
		/** size of BP in multiple of 128KB */
		u32 bpsz : 15;
		u32 reserved1 : 9;

		/** indicates boot read status */
		u32 brs : 2;
		u32 reserved2 : 5;

		/** indicates active boot partition id */
		u32 abpid : 1;
	} bits;
};

union nvme_bprsel_register
{
	u32 raw;

	struct
	{
		/** size in multiples of 4KB to copy into the Boot Partition Memory Buffer */
		u32 bprsz : 10;
		/** offset into to the Boot Partition in 4K units */
		u32 bprof : 20;
		u32 reserved : 1;

		/** indicates the Boot Partition identifier for the Boot Read operation */
		u32 bpid : 1;
	} bits;
};

struct nvme_bpmbl_register
{
	u64 bmbba;
};

struct nvme_vs_idir_addr_register
{
	u32 addr;
};

struct nvme_vs_idir_data_register
{
	u32 data;
};

struct nvme_registers
{
	/** controller capabilities */
	union nvme_cap_register cap;

	/** version of NVMe specification */
	union nvme_vs_register vs;
	u32 intms; /* interrupt mask set */
	u32 intmc; /* interrupt mask clear */

	/** controller configuration */
	union nvme_cc_register cc;

	u32 reserved1;
	union nvme_csts_register csts; /* controller status */
	u32 nssr;					   /* NVM subsystem reset */

	/** admin queue attributes */
	union nvme_aqa_register aqa;

	u64 asq; /* admin submission queue base addr */
	u64 acq; /* admin completion queue base addr */
	/** controller memory buffer location */
	union nvme_cmbloc_register cmbloc;
	/** controller memory buffer size */
	union nvme_cmbsz_register cmbsz;

	union nvme_bpinfo_register bpinfo;
	union nvme_bprsel_register bprsel;

	struct nvme_vs_idir_addr_register vs_idir_addr;
	struct nvme_vs_idir_data_register vs_idir_data;
};

#if defined(SRIOV_SUPPORT)
struct nvme_vf_registers
{
	/** controller capabilities */
	union nvme_cap_register cap;

	/** version of NVMe specification */
	union nvme_vs_register vs;
	u32 rsvd_c[2];

	/** controller configuration */
	union nvme_cc_register cc;

	u32 rsvd_18;
	/* controller status */
	union nvme_csts_register csts;
};
#endif

/* NVMe controller register space offsets */
enum nvme_sgl_descriptor_type
{
	NVME_SGL_TYPE_DATA_BLOCK = 0x0,
	NVME_SGL_TYPE_BIT_BUCKET = 0x1,
	NVME_SGL_TYPE_SEGMENT = 0x2,
	NVME_SGL_TYPE_LAST_SEGMENT = 0x3,
	NVME_SGL_TYPE_KEYED_DATA_BLOCK = 0x4,
	/* 0x5 - 0xE reserved */
	NVME_SGL_TYPE_VENDOR_SPECIFIC = 0xF
};

enum nvme_sgl_descriptor_subtype
{
	NVME_SGL_SUBTYPE_ADDRESS = 0x0,
	NVME_SGL_SUBTYPE_OFFSET = 0x1,
};

struct __attribute__((packed)) nvme_sgl_descriptor
{
	u64 address;
	union
	{
		struct
		{
			u8 reserved[7];
			u8 subtype : 4;
			u8 type : 4;
		} generic;

		struct
		{
			u32 length;
			u8 reserved[3];
			u8 subtype : 4;
			u8 type : 4;
		} unkeyed;

		struct
		{
			u64 length : 24;
			u64 key : 32;
			u64 subtype : 4;
			u64 type : 4;
		} keyed;
	};
};

enum nvme_psdt_value
{
	NVME_PSDT_PRP = 0x0,
	NVME_PSDT_SGL_MPTR_CONTIG = 0x1,
	NVME_PSDT_SGL_MPTR_SGL = 0x2,
	NVME_PSDT_RESERVED = 0x3
};

/**
 * Submission queue priority values for Create I/O Submission Queue Command.
 *
 * Only valid for weighted round robin arbitration method.
 */
enum nvme_qprio
{
	NVME_QPRIO_URGENT = 0x0,
	NVME_QPRIO_HIGH = 0x1,
	NVME_QPRIO_MEDIUM = 0x2,
	NVME_QPRIO_LOW = 0x3,
	NVME_QPRIO_ADMIN = 0x4
};

/**
 * Optional Arbitration Mechanism Supported by the controller.
 *
 * Two bits for CAP.AMS (18:17) field are set to '1' when the controller supports.
 * There is no bit for AMS_RR where all controllers support and set to 0x0 by default.
 */
enum nvme_cap_ams
{
	NVME_CAP_AMS_WRR = 0x1, /**< weighted round robin */
	NVME_CAP_AMS_VS = 0x2,	/**< vendor specific */
};

/**
 * Arbitration Mechanism Selected to the controller.
 *
 * Value 0x2 to 0x6 is reserved.
 */
enum nvme_cc_ams
{
	NVME_CC_AMS_RR = 0x0,  /**< default round robin */
	NVME_CC_AMS_WRR = 0x1, /**< weighted round robin */
	NVME_CC_AMS_VS = 0x7,  /**< vendor specific */
};

struct nvme_cmd
{
	/* dword 0 */
	u16 opc : 8;  /* opcode */
	u16 fuse : 2; /* fused operation */
	u16 rsvd1 : 4;
	u16 psdt : 2;
	u16 cid; /* command identifier */

	/* dword 1 */
	u32 nsid; /* namespace identifier */

	/* dword 2-3 */
	u32 rsvd2;
	u32 rsvd3;

	/* dword 4-5 */
	u64 mptr; /* metadata pointer */

	/* dword 6-9: data pointer */
	union
	{
		struct
		{
			u64 prp1; /* prp entry 1 */
			u64 prp2; /* prp entry 2 */
		} prp;

		struct nvme_sgl_descriptor sgl1;
	} dptr;

	/* dword 10-15 */
	u32 cdw10; /* command-specific */
	u32 cdw11; /* command-specific */
	u32 cdw12; /* command-specific */
	u32 cdw13; /* command-specific */
	u32 cdw14; /* command-specific */
	u32 cdw15; /* command-specific */
};

struct nvme_status
{
	u16 p : 1;	 /* phase tag */
	u16 sc : 8;	 /* status code */
	u16 sct : 3; /* status code type */
	u16 rsvd2 : 2;
	u16 m : 1;	 /* more */
	u16 dnr : 1; /* do not retry */
};

typedef union _nvme_status_alias_t
{
	u16 all;
	struct nvme_status status;
} nvme_status_alias_t;

/**
 * Completion queue entry
 */
struct nvme_cpl
{
	/* dword 0 */
	u32 cdw0; /* command-specific */

	/* dword 1 */
	u32 rsvd1;

	/* dword 2 */
	u32 sqhd : 16;	/* submission queue head pointer */
	u32 sqid : 8;	/* submission queue identifier */
	u32 cntlid : 8; /* nvme controller identifier */

	/* dword 3 */
	u16 cid; /* command identifier */
	struct nvme_status status;
};

/**
 * Dataset Management range
 */
struct nvme_dsm_range
{
	u32 attributes;
	u32 length;
	u64 starting_lba;
};

/**
 * Status code types
 */
enum nvme_status_code_type
{
	NVME_SCT_GENERIC = 0x0,
	NVME_SCT_COMMAND_SPECIFIC = 0x1,
	NVME_SCT_MEDIA_ERROR = 0x2,
	NVME_SCT_VENDOR_SPECIFIC = 0x7,
};

/**
 * Generic command status codes
 */
enum nvme_generic_command_status_code
{
	NVME_SC_SUCCESS = 0x00,
	NVME_SC_INVALID_OPCODE = 0x01,
	NVME_SC_INVALID_FIELD = 0x02,
	NVME_SC_COMMAND_ID_CONFLICT = 0x03,
	NVME_SC_DATA_TRANSFER_ERROR = 0x04,
	NVME_SC_ABORTED_POWER_LOSS = 0x05,
	NVME_SC_INTERNAL_DEVICE_ERROR = 0x06,
	NVME_SC_ABORTED_BY_REQUEST = 0x07,
	NVME_SC_ABORTED_SQ_DELETION = 0x08,
	NVME_SC_ABORTED_FAILED_FUSED = 0x09,
	NVME_SC_ABORTED_MISSING_FUSED = 0x0a,
	NVME_SC_INVALID_NAMESPACE_OR_FORMAT = 0x0b,
	NVME_SC_COMMAND_SEQUENCE_ERROR = 0x0c,
	NVME_SC_INVALID_SGL_SEG_DESCRIPTOR = 0x0d,
	NVME_SC_INVALID_NUM_SGL_DESCIRPTORS = 0x0e,
	NVME_SC_DATA_SGL_LENGTH_INVALID = 0x0f,
	NVME_SC_METADATA_SGL_LENGTH_INVALID = 0x10,
	NVME_SC_SGL_DESCRIPTOR_TYPE_INVALID = 0x11,
	NVME_SC_INVALID_CONTROLLER_MEM_BUF = 0x12,
	NVME_SC_INVALID_PRP_OFFSET = 0x13,
	NVME_SC_ATOMIC_WRITE_UNIT_EXCEEDED = 0x14,
	NVME_SC_OPERATION_DENIED = 0x15,
	NVME_SC_INVALID_SGL_OFFSET = 0x16,
	//NVME_SC_INVALID_SGL_SUBTYPE		= 0x17,
	NVME_SC_HOSTID_INCONSISTENT_FORMAT = 0x18,
	NVME_SC_KEEP_ALIVE_EXPIRED = 0x19,
	NVME_SC_KEEP_ALIVE_INVALID = 0x1a,
	NVME_SC_CMD_ABORTED_DUE_TO_PREEMPT_AND_ABORT = 0x1b,
	NVME_SC_SANITIZE_FAILED = 0x1c,
	NVME_SC_SANITIZE_IN_PROGRESS = 0x1d,
	NVME_SC_SGL_DATA_BLOCK_GRANULARITY_INVALID = 0x1e,
	NVME_SC_CMD_NOT_SUPPORTED_FOR_QUEUE_IN_CMB = 0x1f,
	NVME_SC_NS_WRITE_PROTECTED = 0x20,
	NVME_SC_COMMMAND_INTERRUPTED = 0x21,
	NVME_SC_TRANSIENT_TRANSPORT_ERROR = 0x22,

	NVME_SC_LBA_OUT_OF_RANGE = 0x80,
	NVME_SC_CAPACITY_EXCEEDED = 0x81,
	NVME_SC_NAMESPACE_NOT_READY = 0x82,
	NVME_SC_RESERVATION_CONFLICT = 0x83,
	NVME_SC_FORMAT_IN_PROGRESS = 0x84,
};

/**
 * Command specific status codes
 */
enum nvme_command_specific_status_code
{
	NVME_SC_COMPLETION_QUEUE_INVALID = 0x00,
	NVME_SC_INVALID_QUEUE_IDENTIFIER = 0x01,
	NVME_SC_INVALID_QUEUE_SIZE = 0x02,
	NVME_SC_ABORT_COMMAND_LIMIT_EXCEEDED = 0x03,
	/* 0x04 - reserved */
	NVME_SC_ASYNC_EVENT_REQUEST_LIMIT_EXCEEDED = 0x05,
	NVME_SC_INVALID_FIRMWARE_SLOT = 0x06,
	NVME_SC_INVALID_FIRMWARE_IMAGE = 0x07,
	NVME_SC_INVALID_INTERRUPT_VECTOR = 0x08,
	NVME_SC_INVALID_LOG_PAGE = 0x09,
	NVME_SC_INVALID_FORMAT = 0x0a,
	NVME_SC_FIRMWARE_REQ_CONVENTIONAL_RESET = 0x0b,
	NVME_SC_INVALID_QUEUE_DELETION = 0x0c,
	NVME_SC_FEATURE_ID_NOT_SAVEABLE = 0x0d,
	NVME_SC_FEATURE_NOT_CHANGEABLE = 0x0e,
	NVME_SC_FEATURE_NOT_NAMESPACE_SPECIFIC = 0x0f,
	NVME_SC_FIRMWARE_REQ_NVM_RESET = 0x10,
	NVME_SC_FIRMWARE_REQ_RESET = 0x11,
	NVME_SC_FIRMWARE_REQ_MAX_TIME_VIOLATION = 0x12,
	NVME_SC_FIRMWARE_ACTIVATION_PROHIBITED = 0x13,
	NVME_SC_OVERLAPPING_RANGE = 0x14,
	NVME_SC_NAMESPACE_INSUFFICIENT_CAPACITY = 0x15,
	NVME_SC_NAMESPACE_ID_UNAVAILABLE = 0x16,
	/* 0x17 - reserved */
	NVME_SC_NAMESPACE_ALREADY_ATTACHED = 0x18,
	NVME_SC_NAMESPACE_IS_PRIVATE = 0x19,
	NVME_SC_NAMESPACE_NOT_ATTACHED = 0x1a,
	NVME_SC_THINPROVISIONING_NOT_SUPPORTED = 0x1b,
	NVME_SC_CONTROLLER_LIST_INVALID = 0x1c,
	NVME_SC_DEVICE_SELF_TEST_IN_PROGRESS = 0x1d,
	NVME_SC_BOOT_PARTITION_WRITE_PROHIBITED = 0x1e,
	NVME_SC_INVALID_CONTROLLER_IDENTIFIER = 0x1f,
	NVME_SC_INVALID_SECONDARY_CONTROLLER_STATE = 0x20,
	NVME_SC_INVALID_NUMBER_OF_CONTROLLER_RESOURCES = 0x21,
	NVME_SC_INVALID_RESOURCE_IDENTIFIER = 0x22,
	NVME_SC_ANA_GROUP_ID_IVALID = 0x24,
	NVME_SC_ANA_ATTACH_FAIL = 0x25,

	NVME_SC_CONFLICTING_ATTRIBUTES = 0x80,
	NVME_SC_INVALID_PROTECTION_INFO = 0x81,
	NVME_SC_ATTEMPTED_WRITE_TO_RO_PAGE = 0x82,
};

/**
 * Media error status codes
 */
enum nvme_media_error_status_code
{
	NVME_SC_WRITE_FAULTS = 0x80,
	NVME_SC_UNRECOVERED_READ_ERROR = 0x81,
	NVME_SC_GUARD_CHECK_ERROR = 0x82,
	NVME_SC_APPLICATION_TAG_CHECK_ERROR = 0x83,
	NVME_SC_REFERENCE_TAG_CHECK_ERROR = 0x84,
	NVME_SC_COMPARE_FAILURE = 0x85,
	NVME_SC_ACCESS_DENIED = 0x86,
	NVME_SC_DEALLOCATED_OR_UNWRITTEN_BLOCK = 0x87,

	/* Open Channel specification 2.0 related data structures, constants etc */
	OCSSD_V20_SC_OFFLINE_CHUNK = 0xC0,
	OCSSD_V20_SC_INVALID_RESET = 0xC1,
	OCSSD_V20_SC_HIGH_ECC = 0xD0,
	OCSSD_V20_SC_WRITE_FAIL_WRITE_NEXT_UNIT = 0xF0,
	OCSSD_V20_SC_WRITE_FAIL_CHUNK_EARLY_CLOSE = 0xF1,
	OCSSD_V20_SC_OUT_OF_ORDER_WRITE = 0xF2,
};

/**
 * Admin opcodes
 */
enum nvme_admin_opcode
{
	NVME_OPC_DELETE_IO_SQ = 0x00,
	NVME_OPC_CREATE_IO_SQ = 0x01,
	NVME_OPC_GET_LOG_PAGE = 0x02,
	/* 0x03 - reserved */
	NVME_OPC_DELETE_IO_CQ = 0x04,
	NVME_OPC_CREATE_IO_CQ = 0x05,
	NVME_OPC_IDENTIFY = 0x06,
	/* 0x07 - reserved */
	NVME_OPC_ABORT = 0x08,
	NVME_OPC_SET_FEATURES = 0x09,
	NVME_OPC_GET_FEATURES = 0x0a,
	/* 0x0b - reserved */
	NVME_OPC_ASYNC_EVENT_REQUEST = 0x0c,
	NVME_OPC_NS_MANAGEMENT = 0x0d,
	/* 0x0e-0x0f - reserved */
	NVME_OPC_FIRMWARE_COMMIT = 0x10,
	NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD = 0x11,
	/* 0x12-0x13 - reserved */
	NVME_OPC_DEV_SELF_TEST = 0x14,

	NVME_OPC_NS_ATTACHMENT = 0x15,

	NVME_OPC_VIRTUALIZATION_MANAGEMENT = 0x1c,

	NVME_OPC_FORMAT_NVM = 0x80,
	NVME_OPC_SECURITY_SEND = 0x81,
	NVME_OPC_SECURITY_RECEIVE = 0x82,
	//Andy add SANITIZE IOL Test 1.7 Case 2
	NVME_OPC_SANITIZE               = 0x84,
#if VALIDATE_BOOT_PARTITION
	NVME_OPC_VSC_BP_READ = 0xCA,
#endif //VALIDATE_BOOT_PARTITION


	/* Open channel specification 2.0 Admin commands */
	NVME_OPC_OCSSD_V20_GEOMETRY = 0xE2,

	/* add for vsc*/
	NVME_OPC_SSSTC_VSC_CUSTOMER = 0xF0,	// VSC for release to customer, FET, RelsP2AndGL
	NVME_OPC_SSSTC_VSC_NONE 	= 0xFC,
	NVME_OPC_SSSTC_VSC_WRITE 	= 0xFD,
	NVME_OPC_SSSTC_VSC_READ 	= 0xFE,
};

/**
 * NVM command set opcodes
 */
enum nvme_nvm_opcode
{
	NVME_OPC_FLUSH = 0x00,
	NVME_OPC_WRITE = 0x01,
	NVME_OPC_READ = 0x02,
	/* 0x03 - reserved */
	NVME_OPC_WRITE_UNCORRECTABLE = 0x04,
	NVME_OPC_COMPARE = 0x05,
	/* 0x06-0x07 - reserved */
	NVME_OPC_WRITE_ZEROES = 0x08,
	NVME_OPC_DATASET_MANAGEMENT = 0x09,

	NVME_OPC_RESERVATION_REGISTER = 0x0d,
	NVME_OPC_RESERVATION_REPORT = 0x0e,

	NVME_OPC_RESERVATION_ACQUIRE = 0x11,
	NVME_OPC_RESERVATION_RELEASE = 0x15,

	/* Open channel specification 2.0 NVM commands */
	NVME_OPC_OCSSD_V20_VEC_CHUNK_RESET = 0x90,
	NVME_OPC_OCSSD_V20_VEC_CHUNK_WRITE = 0x91,
	NVME_OPC_OCSSD_V20_VEC_CHUNK_READ = 0x92,
	NVME_OPC_OCSSD_V20_VEC_CHUNK_COPY = 0x93,
};

/**
 * Data transfer (bits 1:0) of an NVMe opcode.
 *
 * \sa nvme_opc_get_data_transfer
 */
enum nvme_data_transfer
{
	/** Opcode does not transfer data */
	NVME_DATA_NONE = 0,
	/** Opcode transfers data from host to controller (e.g. Write) */
	NVME_DATA_HOST_TO_CONTROLLER = 1,
	/** Opcode transfers data from controller to host (e.g. Read) */
	NVME_DATA_CONTROLLER_TO_HOST = 2,
	/** Opcode transfers data both directions */
	NVME_DATA_BIDIRECTIONAL = 3
};

/**
 * Extract the Data Transfer bits from an NVMe opcode.
 *
 * This determines whether a command requires a data buffer and
 * which direction (host to controller or controller to host) it is
 * transferred.
 */
static inline enum nvme_data_transfer nvme_opc_get_data_transfer(u8 opc)
{
	return (enum nvme_data_transfer)(opc & 3);
}

enum nvme_feat
{
	/* 0x00 - reserved */
	NVME_FEAT_ARBITRATION = 0x01,
	NVME_FEAT_POWER_MANAGEMENT = 0x02,
	NVME_FEAT_LBA_RANGE_TYPE = 0x03,
	NVME_FEAT_TEMPERATURE_THRESHOLD = 0x04,
	NVME_FEAT_ERROR_RECOVERY = 0x05,
	NVME_FEAT_VOLATILE_WRITE_CACHE = 0x06,
	NVME_FEAT_NUMBER_OF_QUEUES = 0x07,
	NVME_FEAT_INTERRUPT_COALESCING = 0x08,
	NVME_FEAT_INTERRUPT_VECTOR_CONFIGURATION = 0x09,
	NVME_FEAT_WRITE_ATOMICITY = 0x0A,
	NVME_FEAT_ASYNC_EVENT_CONFIGURATION = 0x0B,
	NVME_FEAT_AUTONOMOUS_POWER_STATE_TRANSITION = 0x0C,
	NVME_FEAT_HOST_MEM_BUFFER = 0x0D,
	NVME_FEAT_TIMESTAMP = 0x0E,
	NVME_FEAT_KEEP_ALIVE_TIMER = 0x0F,
	NVME_FEAT_HOST_THERMAL_MANAGEMENT = 0x10,
	NVME_FEAT_NON_OPERATIONAL_POWER_STATE_CONFIG = 0x11,
#ifdef EN_NVME_14
	NVME_FEAT_READ_RECOVERY_LEVEL_CONFIG = 0x12,
	NVME_FEAT_PREDICTABLE_LATENCY_MODE_CONFIG = 0x13,
	NVME_FEAT_PREDICTABLE_LATENCY_MODE_WINDOW = 0x14,
	NVME_FEAT_LBA_STATUS_INFORMATION_REPORT_INTERVAL = 0x15,
	NVME_FEAT_HOST_BEHAVIOR_SUPPORT = 0x16,
	NVME_FEAT_SANITIZE_CONFIG = 0x17,
	NVME_FEAT_ENDURANCE_GROUP_EVENT_CONFIGURATION = 0x18,
#endif
	/* 0x0C-0x7F - reserved */
	NVME_FEAT_SOFTWARE_PROGRESS_MARKER = 0x80,
	/* 0x81-0xBF - command set specific */
	NVME_FEAT_HOST_IDENTIFIER = 0x81,
	NVME_FEAT_HOST_RESERVE_MASK = 0x82,
	NVME_FEAT_HOST_RESERVE_PERSIST = 0x83,
	NVME_FEAT_NS_WRITE_PROTECT_CONFIG = 0x84,
	/* 0xC0-0xFF - vendor specific */
	NVME_FEAT_Dynamic_OP = 0xC1,
#if !defined(PROGRAMMER)
	NVME_LOG_SAVELOG = 0xD1,
#endif//save log 20210714 young add
	NVME_FEAT_EN_PWRDIS = 0xD6,
	NVME_FEAT_EN_PLP = 0xF0,
};

enum nvme_feat_select
{
	NVME_FEAT_SELECT_CURRENT = 0x0,
	NVME_FEAT_SELECT_DEFAULT = 0x1,
	NVME_FEAT_SELECT_SAVED = 0x2,
	NVME_FEAT_SELECT_SUPPORTED = 0x3,
	NVME_FEAT_SELECT_NOT_SUPPORT
	/* 0x4-0x7 -reserved */
};

typedef union
{
	struct
	{
		u32 ab : 3; /*! arbitration burst */
		u32 rsvd : 5;
		u32 lpw : 8; /*! low priority weight */
		u32 mpw : 8; /*! medium priority weight */
		u32 hpw : 8; /*! high priority weight */
	} b;
	u32 all;
} arb_feat_t; /*! arbitration feature */

typedef union
{
	struct
	{
		u32 ps : 5; /*! power state */
		u32 wh : 3; /*! workload hint */
		u32 rsvd : 24;
	} b;
	u32 all;
} ps_feat_t;

#define OVER_TH 0
#define UNDER_TH 1
#define MAX_TH 2

typedef union
{
	u16 tmpth[9][MAX_TH]; /*! temperature threshold */
} temp_feat_t;			  /*! temperature threshold feature */

typedef union
{
	struct
	{
		u32 tler : 15; /*! timer limited error recovery */
		u32 dulbe : 1; /*! deallocated or unwritten logical block error enable */
	} b;
	u32 all;
} error_feat_t; /*! error recovery feature */

typedef union
{
	struct
	{
		u32 thr : 8;  /*! aggregation threshold */
		u32 time : 8; /*! aggregation time */
		u32 rsvd : 16;
	} b;
	u32 all;
} ic_feat_t; /*! interrupt coalescing feature */

typedef union
{
	struct
	{
		u32 iv : 16; /*! interrupt vector */
		u32 cd : 1;	 /*! coalescing disable */
		u32 rsvd : 15;
	} b;
	u32 all;
} ivc_feat_t; /*! interrupt vector configuration feature */

typedef union
{
	struct
	{
		u32 dn : 1; /*! disable normal */
		u32 rsvd : 31;
	} b;
	u32 all;
} wan_feat_t; /*! write atomicity normal feature */

typedef union
{
	struct
	{
		u32 smart : 8; /*! smart / health critical warnings */
		u32 nan : 1;   /*! namespace attribute notices */
		u32 fan : 1;   /*! firmware activation notices */
		u32 tln : 1;   /*! telemetry log notices */
		u32 anaacn : 1;  /*! Asymmetric Namespace Access Change notices */
		u32 plealc : 1;  /*! Predicable Latency Event Aggregate Log Change notices */
		u32 lsin : 1;  /*! LBA Status Information notices */
		u32 egealcn : 1; /*! Endurance Group Event Aggregate Log Change notices */
		u32 rsvd : 17;
	} b;
	u32 all;
} aec_feat_t; /*! asynchronous event configuration feature */

typedef union
{
	struct
	{
		u32 wce : 1; /*! volatile write cache enable */
		u32 rsvd : 31;
	} b;
	u32 all;
} vwc_feat_t;

typedef union
{
	struct
	{
		u16 nsqa : 16; /*! number of SQ allocated */
		u16 ncqa : 16; /*! number of CQ allocated */
	} b;
	u32 all;
} nque_feat_t;

typedef union
{
	struct
	{
		u16 tmt2 : 16;
		u16 tmt1 : 16;
	} b;
	u32 all;
} hctm_feat_t;

typedef struct
{
	char fr[8];
	char mn[40];
	char sn[20];
} device_feat_t;

#if CO_SUPPORT_SANITIZE
typedef union
{
	struct
	{
		u32 NODRM :1;
		u32 rsvd  :31;
	} b;
	u32 all;
} sanitize_feat_t;
#endif

typedef struct
{
	u16 tmt_warning;
	u16 tmt_critical;
} warn_cri_thre_t;

#ifdef POWER_APST
typedef union
{
	struct
	{
		u32 apst_en : 1;
		u32 rsvd1 : 31;
	} b;
	u32 all;
} apst_t;

typedef struct _apst_data_entry_t
{
	u32 rsvd0 : 3;
	u32 itps : 5;  /*idle transition power state*/
	u32 itpt : 24; /*idle time prior to transition*/
	u32 rsvd32;
} apst_data_entry_t;

typedef struct _apst_feat_t
{
	apst_t apst;
	apst_data_entry_t apst_de[IDTFY_NPSS];
} apst_feat_t;
#endif

typedef struct
{
	u32 timestamp_lo;
	u16 timestamp_hi;
	u8 sync : 1;
	u8 ts_org : 3;
	u8 resv : 4;
	u8 rev;
} get_ts_feat_t;

typedef struct
{
	u32 timestamp_l;
	u16 timestamp_h;
	u16 rev;
} timestamp_feat_t;

typedef union
{
	struct
	{
		u32 opie : 1; //Enable or Disable plp triggered by PLN
		u32 oppe : 1;
		u32 ople : 1;
		u32 rsvd1 : 29;
	} b;
	u32 all;
} en_plp_feat_t;

typedef union
{
	struct
	{
		u32 pwrdis : 1; //Enable or Disable PWRDIS
		u32 rsvd1 : 31;
	} b;
	u32 all;
} en_pwrdis_feat_t;

struct nvmet_feat
{
	u32 Tag;
	arb_feat_t arb_feat;
	ps_feat_t ps_feat;
	temp_feat_t temp_feat;
	error_feat_t error_feat;
	ic_feat_t ic_feat;
	ivc_feat_t ivc_feat;
	wan_feat_t wan_feat;
	aec_feat_t aec_feat;
	vwc_feat_t vwc_feat;
	nque_feat_t nque_feat;
	hctm_feat_t hctm_feat;
#ifdef POWER_APST
	apst_feat_t apst_feat;
#endif
	device_feat_t device_feat;
#if CO_SUPPORT_SANITIZE
	sanitize_feat_t sanitize_feat;
#endif
	warn_cri_thre_t warn_cri_feat;
	en_plp_feat_t en_plp_feat;
	en_pwrdis_feat_t en_pwrdis_feat;
	u64 timestamp;
};
//BUILD_BUG_ON(sizeof(struct nvmet_feat) != 176);

#define NVME_FEAT_SAVABLE BIT0
#define NVME_FEAT_NS_SPECIFIC BIT1
#define NVME_FEAT_CHANGEABLE BIT2

enum nvme_dsm_attribute
{
	NVME_DSM_ATTR_INTEGRAL_READ = 0x1,
	NVME_DSM_ATTR_INTEGRAL_WRITE = 0x2,
	NVME_DSM_ATTR_DEALLOCATE = 0x4,
};

struct nvme_power_state
{
	u16 mp; /* bits 15:00: maximum power */

	u8 reserved1;

	u8 mps : 1;	 /* bit 24: max power scale */
	u8 nops : 1; /* bit 25: non-operational state */
	u8 reserved2 : 6;

	u32 enlat; /* bits 63:32: entry latency in microseconds */
	u32 exlat; /* bits 95:64: exit latency in microseconds */

	u8 rrt : 5; /* bits 100:96: relative read throughput */
	u8 reserved3 : 3;

	u8 rrl : 5; /* bits 108:104: relative read latency */
	u8 reserved4 : 3;

	u8 rwt : 5; /* bits 116:112: relative write throughput */
	u8 reserved5 : 3;

	u8 rwl : 5; /* bits 124:120: relative write latency */
	u8 reserved6 : 3;

	u8 reserved7[16];
};

/** Identify command CNS value */
enum nvme_identify_cns
{
	/** Identify namespace indicated in CDW1.NSID */
	NVME_IDENTIFY_NS = 0x00,

	/** Identify controller */
	NVME_IDENTIFY_CTRLR = 0x01,

	/** List active NSIDs greater than CDW1.NSID */
	NVME_IDENTIFY_ACTIVE_NS_LIST = 0x02,

	/** List Namespace Identification Descriptor structures indicated in CDW1.NSID */
	NVME_ID_CNS_NS_DESC_LIST = 0x03,

	/** List allocated NSIDs greater than CDW1.NSID */
	NVME_IDENTIFY_ALLOCATED_NS_LIST = 0x10,

	/** Identify namespace if CDW1.NSID is allocated */
	NVME_IDENTIFY_NS_ALLOCATED = 0x11,

	/** Get list of controllers starting at CDW10.CNTID that are attached to CDW1.NSID */
	NVME_IDENTIFY_NS_ATTACHED_CTRLR_LIST = 0x12,

	/** Get list of controllers starting at CDW10.CNTID */
	NVME_IDENTIFY_CTRLR_LIST = 0x13,

	/** Get primary Controller Capabilities Structure */
	NVME_IDENTIFY_PRIMARY_CONTROLLER_CAPABILITIES = 0x14,

	/** Get Secondary Controller list */
	NVME_IDENTIFY_SECONDARY_CONTROLLER_LIST = 0x15,

	/** Namespace Granularity List */
	NVME_IDENTIFY_NS_GRANULARITY_LIST = 0x16,
};

enum vwc_flush_cmd_behavior
{
	nvme_1_3_and_early = 0,	  ///< Support for NSID field set to FFFFFFFFh is not indicated.
	not_support_FFFFFFFF = 2, ///< not support NSID set to 0xFFFFFFFF
	support_FFFFFFFF = 3,	  ///< support NSID to set to 0xFFFFFFFF
};

#define IDENTIFY_XFER_SZ 4096

struct __attribute__((packed)) nvme_ctrlr_data
{
	/* bytes 0-255: controller capabilities and features */

	/** pci vendor id */
	u16 vid;

	/** pci subsystem vendor id */
	u16 ssvid;

	/** serial number */
	s8 sn[20];

	/** model number */
	s8 mn[40];

	/** firmware revision */
	u8 fr[8];

	/** recommended arbitration burst */
	u8 rab;

	/** ieee oui identifier */
	u8 ieee[3];

	/** controller multi-path I/O and namespace sharing capabilities */
	struct
	{
		u8 multi_port : 1;
		u8 multi_host : 1;
		u8 sr_iov : 1;
		u8 reserved : 5;
	} cmic;

	/** maximum data transfer size */
	u8 mdts;

	/** controller id */
	u16 cntid;

	/** version */
	union nvme_vs_register ver;

	/** RTD3 resume latency */
	u32 rtd3r;

	/** RTD3 entry latency */
	u32 rtd3e;

	/** optional asynchronous events supported */
	struct
	{
		u32 reserved1 : 8;

		/** Supports sending Namespace Attribute Notices. */
		u32 ns_attribute_notices : 1;

		/** Supports sending Firmware Activation Notices. */
		u32 fw_activation_notices : 1;

		u32 reserved2 : 22;
	} oaes;

	/** controller attributes */
	struct
	{
		u32 host_id_exhid_supported : 1;
		u32 rsvd1 : 6;
		u32 namespace_granualrity : 1;
		u32 rsvd2 : 24;
	} ctratt;

	u8 reserved7[11];

	u8 cntrltype;

	/** FRU Globally Unique Identifier */
	u8 fguid[16];

	/** Command Retry Delay Time */
	u16 crdt1;
	u16 crdt2;
	u16 crdt3;
	
	u8 reserved1[122];

	/* bytes 256-511: admin command set attributes */

	/** optional admin command support */
	struct
	{
		/* supports security send/receive commands */
		u16 security : 1;

		/* supports format nvm command */
		u16 format : 1;

		/* supports firmware activate/download commands */
		u16 firmware : 1;

		/* supports ns manage/ns attach commands */
		u16 ns_manage : 1;

		/* supports  the Device Self-test commands */
		u16 dv_self_test : 1;

		/* supports  Directives */
		u16 directives : 1;

		/* supports  the NVMe-MI Send and Receive commands */
		u16 nvme_mi : 1;

		/* supports  the Virtualization Management commands */
		u16 vi_manage : 1;

		/* supports  the  Doorbell Buffer Config  commands */
		u16 db_config : 1;

		u16 oacs_rsvd : 7;
	} oacs;

	/** abort command limit */
	u8 acl;

	/** asynchronous event request limit */
	u8 aerl;

	/** firmware updates */
	struct
	{
		/* first slot is read-only */
		u8 slot1_ro : 1;

		/* number of firmware slots */
		u8 num_slots : 3;

		/* support activation without reset */
		u8 activation_without_reset : 1;

		u8 frmw_rsvd : 3;
	} frmw;

	/** log page attributes */
	struct
	{
		/* per namespace smart/health log page */
		u8 ns_smart : 1;
		/* command effects log page */
		u8 celp : 1;
		/* extended data */
		u8 ext_data : 1;
		/* telemetry */
		u8 telemetry : 1;
		/* pe log */
		u8 pelog : 1;
		u8 lpa_rsvd : 3;
	} lpa;

	/** error log page entries */
	u8 elpe;

	/** number of power states supported */
	u8 npss;

	/** admin vendor specific command configuration */
	struct
	{
		/* admin vendor specific commands use disk format */
		u8 spec_format : 1;

		u8 avscc_rsvd : 7;
	} avscc;

	/** autonomous power state transition attributes */
	struct
	{
		/** controller supports autonomous power state transitions */
		u8 supported : 1;

		u8 apsta_rsvd : 7;
	} apsta;

	/** warning composite temperature threshold */
	u16 wctemp;

	/** critical composite temperature threshold */
	u16 cctemp;

	/** maximum time for firmware activation */
	u16 mtfa;

	/** host memory buffer preferred size */
	u32 hmpre;

	/** host memory buffer minimum size */
	u32 hmmin;

	/** total NVM capacity */
	u64 tnvmcap[2];

	/** unallocated NVM capacity */
	u64 unvmcap[2];

	/** replay protected memory block support */
	struct
	{
		u8 num_rpmb_units : 3;
		u8 auth_method : 3;
		u8 reserved1 : 2;

		u8 reserved2;

		u8 total_size;
		u8 access_size;
	} rpmbs;

	/** Extended Device Self-test Time */
	u16 edstt; 

	/** Device Self-test Options */
	struct
	{
		/**supports type */
		u8 supported_type : 1;

		u8 dsto_rsvd : 7;
	} dsto;
	
	/* 319 FWUG */
	u8 fwug;

	u16 kas;

	/* 323:322 host controlled thermal management attributes */
	u16 hctma;
	/* 325:324 minimum thermal management temperature */
	u16 mntmt;
	/* 327:326 maximum thermal management temperature */
	u16 mxtmt;

	/* Sanitize Capabilities */
#if CO_SUPPORT_SANITIZE
	struct {
		u32 CES     :1;
		u32 BES     :1;
		u32 OWS     :1;
		u32 rsvd    :26;
		u32 NDI     :1;
		u32 NODMMAS :2;
	}SANICAP;
#else
	u32 sanicap;
#endif

	/* Host Memory Buffer Minimum Descriptor Entry Size */
	u32 hmminnds;

	/* Host Memory Maximum Descriptors Entries */
	u16 hmmaxd;
	
	u8 reserved338[4];

	/* ANA Transition Time */
	u8 anatt;

	/* Asymmetric Namespace Access Capabilities */
	struct
	{
		u8 support : 1;
		u8 changed : 1;
		u8 reserved : 1;
		u8 report_change : 1;
		u8 report_persistentloss : 1;
		u8 report_inacessable : 1;
		u8 report_nonoptimized : 1;
		u8 report_optimized : 1;
	} anacap;

	/* ANA Group Identifier Maximum */
	u32 anagrpmax;

	/* Number of ANA Group Identifiers */
	u32 nanagrpid;
	
	u8 reserved352[160];
	/* bytes 512-703: nvm command set attributes */

	/** submission queue entry size */
	struct
	{
		u8 sq_min : 4;
		u8 sq_max : 4;
	} sqes;

	/** completion queue entry size */
	struct
	{
		u8 cq_min : 4;
		u8 cq_max : 4;
	} cqes;

	u16 maxcmd;

	/** number of namespaces */
	u32 nn;

	/** optional nvm command support */
	struct
	{
		u16 compare : 1;
		u16 write_unc : 1;
		u16 dsm : 1;
		u16 write_zeroes : 1;
		u16 set_features_save : 1;
		u16 reservations : 1;
		u16 timestamp : 1;
		u16 reserved : 9;
	} oncs;

	/** fused operation support */
	u16 fuses;

	/** format nvm attributes */
	struct
	{
		u8 format_all_ns : 1;
		u8 erase_all_ns : 1;
		u8 crypto_erase_supported : 1;
		u8 reserved : 5;
	} fna;

	/** volatile write cache */
	struct
	{
		u8 present : 1;
		u8 flush_behavior : 2; // vwc_flush_cmd_behavior
		u8 reserved : 5;
	} vwc;

	/** atomic write unit normal */
	u16 awun;

	/** atomic write unit power fail */
	u16 awupf;

	/** NVM vendor specific command configuration */
	u8 nvscc;

	/** Namespace Write Protection Capabilities */
	u8 nwpc;

	/** atomic compare & write unit */
	u16 acwu;

	u8 reserved534[2];

	/** SGL support, update format to NVME rev 1.3 */
	struct
	{
		u32 supported : 2;
		u32 keyed : 1;
		u32 reserved0 : 13;
		u32 bit_bucket_descriptor : 1;
		u32 metadata_pointer : 1;
		u32 oversized_sgl : 1;
		u32 metadata_address : 1;
		u32 sgl_offset : 1;
		u32 reserved2 : 11;
	} sgls;

	/** Maximum Number of Allowed Namespaces */
	u32 mnan;
	
	u8 reserved544[224];

	u8 subnqn[256];

	u8 reserved6[768];

	/** NVMe over Fabrics-specific fields */
	struct
	{
		/** I/O queue command capsule supported size (16-byte units) */
		u32 ioccsz;

		/** I/O queue response capsule supported size (16-byte units) */
		u32 iorcsz;

		/** In-capsule data offset (16-byte units) */
		u16 icdoff;

		/** Controller attributes */
		struct
		{
			/** Controller model: \ref nvmf_ctrlr_model */
			u8 ctrlr_model : 1;
			u8 reserved : 7;
		} ctrattr;

		/** Maximum SGL block descriptors (0 = no limit) */
		u8 msdbd;

		u8 reserved1804[2];
		u8 dctype;
		u8 reserved1807[241];
	} nvmf_specific;

	/* bytes 2048-3071: power state descriptors */
	struct nvme_power_state psd[32];

	/* bytes 3072-4095: vendor specific */

	/* bytes 3072-3999: vs1*/
	u8 vs1[928];

	/* byte 4000-4001: Lenovo unique features*/
	struct
	{
		u16 plp_supported : 1; //support plp feature or not(F0)
		u16 OPPE : 1;
		u16 OPLE : 1;
		u16 reserved : 13;
	} lenovo_en_plp_features;

	/*bytes 4002-4005: vs2*/
	u8 vs2[4];

	/* byte 4006-4007: Lenovo unique features*/
	struct
	{
		u16 pwrdis_supported : 1; //pwrdis feature or not(D6)
		u16 reserved : 15;
	} lenovo_en_pwrdis_features;

	/*bytes 4008-4095: vs3*/
	u8 vs3[88];
};

struct nvme_ns_data
{
	/** namespace size */
	u64 nsze;

	/** namespace capacity */
	u64 ncap;

	/** namespace utilization */
	u64 nuse;

	/** namespace features */
	union
	{
		u8 all;
		struct
		{
			u8 thin_prov : 1;
			u8 atomic : 1;
			u8 err_ctrl : 1;
			u8 guid : 1;
			u8 per_op : 1;
			u8 rsv : 3;
		};
	} nsfeat; /*! ns features ctrl, detail refer idfy cns 00h byte 24 */

	/** number of lba formats */
	u8 nlbaf;

	/** formatted lba size */
	struct
	{
		u8 format : 4;
		u8 extended : 1;
		u8 reserved2 : 3;
	} flbas;

	/** metadata capabilities */
	struct
	{
		/** metadata can be transferred as part of data prp list */
		u8 extended : 1;

		/** metadata can be transferred with separate metadata pointer */
		u8 pointer : 1;

		/** reserved */
		u8 reserved3 : 6;
	} mc;

	/** end-to-end data protection capabilities */
	struct
	{
		/** protection information type 1 */
		u8 pit1 : 1;

		/** protection information type 2 */
		u8 pit2 : 1;

		/** protection information type 3 */
		u8 pit3 : 1;

		/** first eight bytes of metadata */
		u8 md_start : 1;

		/** last eight bytes of metadata */
		u8 md_end : 1;
	} dpc;

	/** end-to-end data protection type settings */
	struct
	{
		/** protection information type */
		u8 pit : 3;

		/** 1 == protection info transferred at start of metadata */
		/** 0 == protection info transferred at end of metadata */
		u8 md_start : 1;

		u8 reserved4 : 4;
	} dps;

	/** namespace multi-path I/O and namespace sharing capabilities */
	struct
	{
		u8 can_share : 1;
		u8 reserved : 7;
	} nmic;

	/** reservation capabilities */
	union
	{
		struct
		{
			/** supports persist through power loss */
			u8 persist : 1;

			/** supports write exclusive */
			u8 write_exclusive : 1;

			/** supports exclusive access */
			u8 exclusive_access : 1;

			/** supports write exclusive - registrants only */
			u8 write_exclusive_reg_only : 1;

			/** supports exclusive access - registrants only */
			u8 exclusive_access_reg_only : 1;

			/** supports write exclusive - all registrants */
			u8 write_exclusive_all_reg : 1;

			/** supports exclusive access - all registrants */
			u8 exclusive_access_all_reg : 1;

			u8 reserved : 1;
		} rescap;
		u8 raw;
	} nsrescap;
	/** format progress indicator */
	struct
	{
		u8 percentage_remaining : 7;
		u8 fpi_supported : 1;
	} fpi;

	u8 dlfeat;// joe add 20201214//reserved33;

	/** namespace atomic write unit normal */
	u16 nawun;

	/** namespace atomic write unit power fail */
	u16 nawupf;

	/** namespace atomic compare & write unit */
	u16 nacwu;

	/** namespace atomic boundary size normal */
	u16 nabsn;

	/** namespace atomic boundary offset */
	u16 nabo;

	/** namespace atomic boundary size power fail */
	u16 nabspf;

	/** Namespace Optimal I/O Boundary */
	u16 noiob;

	/** NVM capacity */
	u64 nvmcap[2];

	/** Namespace Preferred Write Granularity */
	u16 npwg;

	/** Namespace Preferred Write Alignment */
	u16 npwa;

	/** Namespace Preferred Deallocate Granularity */
	u16 npdg;

	/** Namespace Preferred Deallocate Alignment */
	u16 npda;

	/** Namespace Optimal Write Size */
	u16 nows;

	u8 reserved74[18];

	/** ANA Group Identifier */
	u32 anagrpid;

	u8 reserved96[3];

	/** Namespace Attributes */
	u8 nsattr;

	/** NVM Set Identifier */
	u16 nvmsetid;

	/** Endurance Group Identifier */
	u16 endgid;
	
	/** namespace globally unique identifier */
	u8 nguid[16];

	/** IEEE extended unique identifier */
	u64 eui64;

	/** lba format support */
	struct
	{
		/** metadata size */
		u32 ms : 16;

		/** lba data size */
		u32 lbads : 8;

		/** relative performance */
		u32 rp : 2;

		u32 reserved6 : 6;
	} lbaf[16];

	u8 reserved6[192];

	u8 vendor_specific[3712];
};

typedef struct _ns_manage_t
{
	/** namespace size, in logical block */
	u64 nsze;

	/** namespace capacity, in logical block */
	u64 ncap;

	u8 rsv1[10]; // 25:16

	/** formatted lba size */
	union
	{
		u8 all;
		struct
		{
			u8 format : 4;
			u8 extended : 1; // 1: extended lba buffer, 0: separate continuous buffer
			u8 reserved : 3;
		};
	} flbas;

	u8 rsv2[2]; // byte 28:27

	/** e2e data protection type, byte 29**/
	u8 dps;

	/** Namepsace Multi-path I/O and Namespace Sharing Capabilities **/
	u8 nmic;

	u8 rsv3[61]; // byte 91:31

	/** ANA Group Identifier, byte 95:92**/
	u32 ana_id;

	u8 rsv4[4]; // byte 99:96

	/** NVM Set Identifier, byte 102:100 **/
	u16 nvm_set_id;

	u8 rsv5[282];
} ns_manage_t;

#if defined(SRIOV_SUPPORT)
/** Virtualization Management command ACT value */
enum nvme_virt_mgmt_act
{
	/* Reserved */
	NVME_SR_IOV_RSVD = 0,

	/* Primary Controller Flexible Allocation */
	NVME_SR_IOV_PRI_ALLOC = 1,

	/* Secondary Controller Offline */
	NVME_SR_IOV_SEC_OFFLINE = 7,

	/* Secondary Controller Assign */
	NVME_SR_IOV_SEC_ASSIGN = 8,

	/* Secondary Controller Online */
	NVME_SR_IOV_SEC_ONLINE = 9,
};

#define MAX_SEC_VF (32)
struct __attribute__((packed)) nvme_pri_ctrlr_cap
{
	/** Controller Identifier */
	u16 cntl_id;

	/** Port Identifier */
	u16 port_id;

	/** Controller Resource Types */
	u8 crt;

	u8 reserved0[3];

	/** VQ Resources Flexible Total */
	u32 vqfrt;

	/** VQ Resources Flexible Assigned */
	u32 vqrfa;

	/** VQ Resources Flexible Allocated to Primary */
	u16 vqrfap;

	/** VQ Resources Private Total */
	u16 vqprt;

	/** VQ Resources Flexible Secondary Maximum */
	u16 vqfrsm;

	/** VQ Flexible Resource Preferred Granularity */
	u16 vqgran;

	u8 reserved1[16];

	/** VI Resources Flexible Total */
	u32 vifrt;

	/** VI Resources Flexible Assigned */
	u32 virfa;

	/** VI Resources Flexible Allocated to Primary */
	u16 virfap;

	/** VI Resources Private Total */
	u16 viprt;

	/** VI Resources Flexible Secondary Maximum */
	u16 vifrsm;

	/** VI Flexible Resource Preferred Granularity */
	u16 vigran;

	u8 reserved2[4016];
};

struct nvme_sec_ctrlr_entry
{
	/** Secondary Controller Identifier */
	u16 scid;

	/** Primary Controller Identifier */
	u16 pcid;

	/** Secondary Controller State */
	u8 scs;

	u8 reserved0[3];

	/** Virtual Function Number */
	u16 vfn;

	/** Number of VQ Flexible Resources Assigned */
	u16 nvq;

	/** Number of VI Flexible Resources Assigned */
	u16 nvi;

	u8 reserved1[18];
};

struct __attribute__((packed)) nvme_sec_ctrlr_list
{
	/** Number of Identifiers: number of Secondary Controller Entries in the list */
	u8 num_id;

	u8 reserved0[31];

	struct nvme_sec_ctrlr_entry sc[SRIOV_VF_PER_CTRLR];

	u8 reserved1[4096 - 32 - (32 * SRIOV_VF_PER_CTRLR)];
};
#endif

/**
 * Reservation Type Encoding
 */
enum nvme_reservation_type
{
	/* 0x00 - reserved */

	/* Write Exclusive Reservation */
	NVME_RESERVE_WRITE_EXCLUSIVE = 0x1,

	/* Exclusive Access Reservation */
	NVME_RESERVE_EXCLUSIVE_ACCESS = 0x2,

	/* Write Exclusive - Registrants Only Reservation */
	NVME_RESERVE_WRITE_EXCLUSIVE_REG_ONLY = 0x3,

	/* Exclusive Access - Registrants Only Reservation */
	NVME_RESERVE_EXCLUSIVE_ACCESS_REG_ONLY = 0x4,

	/* Write Exclusive - All Registrants Reservation */
	NVME_RESERVE_WRITE_EXCLUSIVE_ALL_REGS = 0x5,

	/* Exclusive Access - All Registrants Reservation */
	NVME_RESERVE_EXCLUSIVE_ACCESS_ALL_REGS = 0x6,

	/* 0x7-0xFF - Reserved */
};

struct nvme_reservation_acquire_data
{
	/** current reservation key */
	u64 crkey;
	/** preempt reservation key */
	u64 prkey;
};

/**
 * Reservation Acquire action
 */
enum nvme_reservation_acquire_action
{
	NVME_RESERVE_ACQUIRE = 0x0,
	NVME_RESERVE_PREEMPT = 0x1,
	NVME_RESERVE_PREEMPT_ABORT = 0x2,
};

struct __attribute__((packed)) nvme_reservation_status_data
{
	/** reservation action generation counter */
	u32 generation;
	/** reservation type */
	u8 type;
	/** number of registered controllers */
	u16 nr_regctl;
	u16 reserved1;
	/** persist through power loss state */
	u8 ptpl_state;
	u8 reserved[14];
};

struct __attribute__((packed)) nvme_reservation_ctrlr_data
{
	u16 ctrlr_id;
	/** reservation status */
	struct
	{
		u8 status : 1;
		u8 reserved1 : 7;
	} rcsts;
	u8 reserved2[5];
	/** host identifier */
	u64 host_id;
	/** reservation key */
	u64 key;
};

/**
 * Change persist through power loss state for
 *  Reservation Register command
 */
enum nvme_reservation_register_cptpl
{
	NVME_RESERVE_PTPL_NO_CHANGES = 0x0,
	NVME_RESERVE_PTPL_CLEAR_POWER_ON = 0x2,
	NVME_RESERVE_PTPL_PERSIST_POWER_LOSS = 0x3,
};

/**
 * Registration action for Reservation Register command
 */
enum nvme_reservation_register_action
{
	NVME_RESERVE_REGISTER_KEY = 0x0,
	NVME_RESERVE_UNREGISTER_KEY = 0x1,
	NVME_RESERVE_REPLACE_KEY = 0x2,
};

struct nvme_reservation_register_data
{
	/** current reservation key */
	u64 crkey;
	/** new reservation key */
	u64 nrkey;
};

struct nvme_reservation_key_data
{
	/** current reservation key */
	u64 crkey;
};

/**
 * Reservation Release action
 */
enum nvme_reservation_release_action
{
	NVME_RESERVE_RELEASE = 0x0,
	NVME_RESERVE_CLEAR = 0x1,
};

/**
 * Log page identifiers for NVME_OPC_GET_LOG_PAGE
 */
enum nvme_log_page
{
	/* 0x00 - reserved */

	/** Error information (mandatory) - \ref nvme_error_information_entry */
	NVME_LOG_ERROR = 0x01,

	/** SMART / health information (mandatory) - \ref nvme_health_information_page */
	NVME_LOG_HEALTH_INFORMATION = 0x02,

	/** Firmware slot information (mandatory) - \ref nvme_firmware_page */
	NVME_LOG_FIRMWARE_SLOT = 0x03,

	/** Changed namespace list (optional) */
	NVME_LOG_CHANGED_NS_LIST = 0x04,

	/** Command effects log (optional) */
	NVME_LOG_COMMAND_EFFECTS_LOG = 0x05,

	/* 0x06-0x6F - reserved */
		/**  */
	NVME_LOG_DEVICE_SELF_TEST	= 0x06,

	/** telemetry host-initiated (optional) */
	NVME_LOG_TELEMETRY_HOST_INIT	= 0x07,

	/** telemetry controller-initiated (optional) */
	NVME_LOG_TELEMETRY_CTRLR_INIT	= 0x08,

	NVME_LOG_ENDURANCE_GROUP	= 0x09,
	
	/** telemetry controller-initiated (optional) */
	NVME_LOG_ANA	= 0x0C,
	/** Discovery(refer to the NVMe over Fabrics specification) */
	NVME_LOG_DISCOVERY = 0x70,

	/* 0x71-0x7f - reserved for NVMe over Fabrics */

	/** Reservation notification (optional) */
	NVME_LOG_RESERVATION_NOTIFICATION = 0x80,

	/* 0x81-0xBF - I/O command set specific */
#if CO_SUPPORT_SANITIZE
	NVME_LOG_SANITIZE_STATUS = 0x81,
#endif

	/* 0xC0-0xFF - vendor specific */
#if (Synology_case)
	NVME_SYNOLOGY_SMART = 0xC0,
#endif
	OCSSD_V20_LOG_CHUNK_INFO = 0xC2,  // 2024-02-19 Conflict with synology smart opcode, temporarily changed to C2
	NVME_LOG_ADDITIONAL_SMART = 0xCA, //2020-09-07 Alan Lin
	NVME_LOG_PHY_TRAINING_REUSLT = 0xCB, //2021-01-22 Alan Lin
#if CO_SUPPORT_OCP
	NVME_OCP_SMART = 0xE0,
#endif	
#if !defined(PROGRAMMER)
        NVME_LOG_DUMPLOG_NAND = 0xD6,
        READ_GLIST = 0xD7,
#endif//dump/save log read glist Young add 20210714 

};

/**
 * Error information log page (\ref NVME_LOG_ERROR)
 */
struct nvme_error_information_entry
{
	u64 error_count;
	u16 sqid;
	u16 cid;
	struct nvme_status status;
	u16 error_location;
	u64 lba;
	u32 nsid;
	u8 vendor_specific;
	u8 trtype;
	u8 reserved1[2];
	u64 cmd_specific;
	u16 trtype_specific_info;
	u8 reserved2[22];
} PACKED;

/*!
 * @brief insert Error information log page (\ref NVME_LOG_ERROR)
 */
struct insert_nvme_error_information_entry
{
	u64 error_count;
	u16 sqid;
	u16 cid;
	struct nvme_status status;
	u16 error_location;
	u64 lba;
	u32 nsid;
	u8 vendor_specific;
	u8 trtype;
	u8 reserved1[2];
	u64 cmd_specific;
	u16 trtype_specific_info;
	u8 reserved2[22];
} PACKED;

union nvme_critical_warning_state
{
	u8 raw;

	struct
	{
		u8 available_spare : 1; //Bit 0
		u8 temperature : 1; //Bit 1
		u8 device_reliability : 1; //Bit 2
		u8 read_only : 1; //Bit 3
		u8 volatile_memory_backup : 1; //Bit 4
		u8 persistent_memory : 1; //Bit 5
#ifdef SMART_PLP_NOT_DONE
		u8 epm_vac_err : 1;//Bit6
		u8 reserbed : 1;
#else
		u8 reserved : 2;
#endif
	} bits;
};

union nvme_endurance_group_critical_warning_state
{
	u8 raw;

	struct
	{
		u8 available_spare : 1;
		u8 reserved1 : 1;
		u8 device_reliability : 1;
		u8 read_only : 1;
		u8 reserved : 4;
	} bits;
};

/*!
 * @brief Definition of S.M.A.R.T information (NVME spec 5.14.1.2), it will be saved in device block
 */
typedef struct _smart_statistics_t
{
	stat_head_t head; //8 bytes

	// 6 Bytes
	union nvme_critical_warning_state critical_warning; //8 Bits
	u16 temperature; //16 Bits	
	u8 available_spare; //8 Bits
	u8 available_spare_threshold; //8 Bits
	u8 percentage_used; //8 Bits

	// 8*8 = 64 Bytes
	u64 host_read_commands;			///< number of read commands completed by the controller
	u64 host_write_commands;		///< number of write commands completed by the controller
	u64 data_units_written;			///< number of 512 byte data units the host has written to the controller
	u64 data_units_read;			///< number of 512 byte data units the host has read from the controller
	u64 controller_busy_time;		///< amount of time the controller is busy with I/O commands
	u64 power_cycles;				///< number of power cycles
	u64 num_error_info_log_entries; ///< mnumber of all error info log entries.
	u64 unsafe_shutdowns;		    ///< number of unsafe shutdowns

	// 8*2 = 16 Bytes
	u16 temperature_sensor[8];

	// 7*4 = 28 Bytes
	u32 power_on_minutes;					///< time of power on (minutes)
	u32 warning_composite_temperature_time; ///< how much time in warning composite temperature
	u32 critial_composite_temperature_time; ///< how much time in critial composite temperature
	u32 thermal_management_t1_trans_cnt;
	u32 thermal_management_t2_trans_cnt;
	u32 thermal_management_t1_total_time;
	u32 thermal_management_t2_total_time;

	// 1*9 = 9 Bytes
	// u8 program_fail_count;
	// u8 erase_fail_count;
	// u8 wear_leveing_count;
	// u8 end_to_end_detection_count;
	// u8 nand_bytes_written;
	// u8 host_bytes_written;
	// u8 bad_block_failure_rate;
	// u8 nand_ecc_detection_count;
	// u8 nand_ecc_correction_count;

	u64 media_err[2];
	u8 rvsd[110];

} smart_statistics_t; //256 Bytes
BUILD_BUG_ON(sizeof(smart_statistics_t) != 256);

/*!
 * @brief 
 */
 #pragma pack(1)
typedef struct _tencnet_smart_statistics_t
{	
	//The order cannot be changed, unless the EPM is cleared, otherwise the data will be messed up
	u32 program_fail_count; //0:Program Fail Count, 1:Reserved, 2:Reserved
	u16 reserved1;
	u32 erase_fail_count; //0:Erase Fail Count, 1:Reserved, 2:Reserved
	u16 reserved2;
	u16 wear_levelng_count[3]; //0:min erase count, 1:max erase count, 2:avg erase count
	u16 end_to_end_detection_count[3]; //0:guard check error, 1:application tag check error, 2:reference tag check error
	u32 crc_error_count; //0:Bad TLP Count, 1:Reserved, 2:Reserved
	u16 reserved3;
	u16 nand_bytes_written[3]; //0:NAND sectors written (1count=32MiB), 1:Reserved, 2:Reserved
	//u16 nand_bytes_written_remain;
	u16 host_bytes_written[3]; //0:Host sectors written (1count=32MiB), 1:Reserved, 2:Reserved
	u32 reallocated_sector_count; //0:1-avail spare, 1:Reserved, 2:Reserved
	u16 host_uecc_detection_cnt;
	u32 uncorrectable_sector_count; //Not Use Now
	u16 reserved4;
	u32 nand_ecc_detection_count; //Not Use Now
	u16 reserved5;
	u32 nand_ecc_correction_count; //Not Use Now
	u16 reserved6;
	u32 bad_block_failure_rate; //Not Use Now
	u16 reserved8;
	u16 dram_error_count[3]; //0:Increases by 1 when Enter interrupt and have 2 bit error count, 1:1 bit eror count, 2:2 bit error count
	u16 sram_error_count[3]; //0:parity error detected, 1:ecc error detection, 2:axi data parity errors
	u16 hcrc_error_count[3]; //0:Read HCRC Error Count, 1:Write HCRC Error Conut, 2:Reserved
	u32 gc_count; //0:Wear Leveling, 1:Read Disturb, 2:Data Retention
	u16 reserved13;
	u16 inflight_cmd[3]; //0:Read Command, 1:Write Command, 2:Admin Command
	u16 pcie_correctable_error_count[3];//0:Correctable Error Count, 1:Reserved, 2:Reserved
	u32 raid_recovery_fail_count;////0:Host XOR Fail Count, 1:Internal XOR Fail Count, 2:Reserved
	u16 reserved7;
	u32 die_fail_count;
	u16 reserved9;
	u32 wl_exec_count; 
	u16 reserved10;
	u32 rd_count;
	u16 reserved11;
	u32 dr_count;
	u16 reserved12;
	u8 no_clk;
	u8 no_perst;
	u16 corr_err_cnt;
#ifdef	SMART_PLP_NOT_DONE
	u16 plp_not_done_cnt;
	u8 rvsd[368];
#else
	u8 rvsd[370]; 
#endif
} tencnet_smart_statistics_t; //512 Bytes
#pragma pack()

BUILD_BUG_ON(sizeof(tencnet_smart_statistics_t) != 512);

/*!
 * @brief Definition of Additional S.M.A.R.T information
 */
#pragma pack(1)
struct __attribute__((packed)) nvme_additional_health_information_page
{
	// Program Fail Count
	u8 program_fail_count;
	u8 program_fail_count_normalized_value;
	u32 program_fail_count_current_raw_value;
	u8 reserved[4];
	// Erase Fail Count
	u8 erase_fail_count;
	u8 erase_fail_count_normalized_value;
	u32 erase_fail_count_current_raw_value;
	u8 reserved2[4];
	// Wear Leveling Count
	u8 wear_leveling_count;
	u8 wear_leveling_count_normalized_value;
	u16 wear_leveling_count_current_raw_value[3];
	u8 reserved3[2];
	// End to End Error Detection Count
	u8 end_to_end_error_detection_count;
	u8 end_to_end_error_detection_count_normalized_value;
	u16 end_to_end_error_detection_count_current_raw_value[3];
	u8 reserved4[2];
	// CRC Error Counte
	u8 crc_error_count;
	u8 crc_error_count_normalized_value;
	u32 crc_error_count_current_raw_value;
	u8 reserved5[4];
	// NAND Bytes Written
	u8 nand_bytes_written;
	u8 nand_bytes_written_normalized_value;
	u16 nand_bytes_written_current_value[3];
	u8 reserved6[2];
	// Host Bytes Written
	u8 host_bytes_written;
	u8 host_bytes_written_normalized_value;
	u16 host_bytes_written_current_value[3];
	u8 reserved7[2];
	//Reallocated Sector Count
	u8 reallocated_sector_count;
	u8 reallocated_sector_count_normalized_value;
	u32 reallocated_sector_count_current_value;
	u8 reserved8[4];
	//Uncorrectable Sector Count
	u8 uncorrectable_sector_count;
	u8 uncorrectable_sector_count_normalized_value;
	u32 uncorrectable_sector_count_current_value;
	u8 reserved9[4];
	//NAND ECC Detection Coun
	u8 nand_ecc_detection_count;
	u8 nand_ecc_detection_count_normalized_value;
	u32 nand_ecc_detection_count_current_value;
	u8 reserve10[4];
	//NAND ECC Correction Count
	u8 nand_ecc_correction_count;
	u8 nand_ecc_correction_count_normalized_value;
	u32 nand_ecc_correction_count_current_value;
	u8 reserved11[4];
	//Bad Block Failure Rate
	u8 bad_block_failure_rate;
	u8 bad_block_failure_rate_normalized_value;
	u32 bad_block_failure_rate_current_value;
	u8 reserved12[4];
	//GC Error Count
	u8 gc_error_count;
	u8 gc_error_count_normalized_value;
	u32 gc_error_count_current_value;
	u8 reserved13[4];
	//DRAM UECC Detection Count
	u8 dram_uecc_detection_count;
	u8 dram_uecc_detection_count_normalized_value;
	u16 dram_uecc_detection_count_current_value[3];
	u8 reserved14[2];
	//SRAM UECC Detection Count
	u8 sram_uecc_detection_count;
	u8 sram_uecc_detection_count_normalized_value;
	u16 sram_uecc_detection_count_current_value[3];
	u8 reserved15[2];
	//Raid Recovery Fail Count
	u8 raid_recovery_fail_count;
	u8 raid_recovery_fail_count_normalized_value;
	u32 raid_recovery_fail_count_current_value;
	u8 reserved16[4];
	//Inflight Command
	u8 inflight_command_count;
	u8 inflight_command_count_normalized_value;
	u16 inflight_command_count_current_value[3];
	u8 reserved17[2];
	//Internal End to End Detection Count
	u8 hcrc_detection_count;
	u8 hcrc_detection_count_normalized_value;
	u16 hcrc_detection_count_current_value[3];
	u8 reserved18[2];
	//PCIe Correctable Error Count
	u8 pcie_correctable_error_count;
	u8 pcie_correctable_error_count_normalized_value;
	u16 pcie_correctable_error_count_current_value[3];
	u8 reserved19[2];
	//Die Fail Count
	u8 die_fail_count;
	u8 die_fail_count_normalized_value;
	u32 die_fail_count_current_value;
	u8 reserved20[4];
	//Wear Leveling Execution Count
	u8 wear_leveling_exec_count;
	u8 wear_leveling_exec_count_normalized_value;
	u32 wear_leveling_exec_count_current_value;
	u8 reserved21[4];
	//Read Disturb Count
	u8 read_disturb_count;
	u8 read_disturb_count_normalized_value;
	u32 read_disturb_count_current_value;
	u8 reserved22[4];
	//Data Retention Count
	u8 data_retention_count;
	u8 data_retention_count_normalized_value;
	u32 data_retention_count_current_value;
	u8 reserved23[4];
#ifdef SMART_PLP_NOT_DONE
	u8 plp_not_done_sign;
	u16 plp_not_done_cnt;
	u8 RSVD[1245];
#else
	u8 RSVD[1248];
#endif
} additional_smart_info_t;
#pragma pack()
BUILD_BUG_ON(sizeof(additional_smart_info_t) != 1478);

/*!
 * @brief Definition of PHY Training Result
 */
struct __attribute__((packed)) nvme_phy_trainging_result_page
{
	u32 lane_leq[4]; //LEQ 
	s32 lane_dfe[4]; //DFE
	u16 pll_kr; //PLL_BAND
	u32 lane_cdr[4]; //CDR
	s8 lane_rc[4]; //DAC 

	u8 pcie_gen;
	u8 pcie_lanes;
};

#define SAVED_FEAT_VER 8		  ///< version of feature
#define SAVED_FEAT_SIG 0x46454154 ///< signature of feature "FEAT"

/*!
 * @brief definition of NVME feature saved value in NVM
 */
typedef struct _nvme_feature_saved_t
{
	stat_head_t head;
	struct nvmet_feat saved_feat;
	u32 rsvd[16];
} nvme_feature_saved_t;
//BUILD_BUG_ON(sizeof(nvme_feature_saved_t) != 256);

/**
 * SMART / health information page (\ref NVME_LOG_HEALTH_INFORMATION)
 */
struct __attribute__((packed)) nvme_health_information_page
{
	union nvme_critical_warning_state critical_warning;

	u16 temperature;
	u8 available_spare;
	u8 available_spare_threshold;
	u8 percentage_used;
	union nvme_endurance_group_critical_warning_state endu_grp_crit_warn_sumry;
	u8 reserved[25];

	/*
	 * Note that the following are 128-bit values, but are
	 *  defined as an array of 2 64-bit values.
	 */
	/* Data Units Read is always in 512-byte units. */
	u64 data_units_read[2];
	/* Data Units Written is always in 512-byte units. */
	u64 data_units_written[2];
	/* For NVM command set, this includes Compare commands. */
	u64 host_read_commands[2];
	u64 host_write_commands[2];
	/* Controller Busy Time is reported in minutes. */
	u64 controller_busy_time[2];
	u64 power_cycles[2];
	u64 power_on_hours[2];
	u64 unsafe_shutdowns[2];
	u64 media_errors[2];
	/* 191:176 */
	u64 num_error_info_log_entries[2];

	/* TODO: */
	u32 warning_composite_temperature_time;
	u32 critial_composite_temperature_time;

	u16 temperature_sensor[8];

	volatile u32 thermal_management_t1_trans_cnt;
	volatile u32 thermal_management_t2_trans_cnt;

	volatile u32 thermal_management_t1_total_time;
	volatile u32 thermal_management_t2_total_time;

	u8 reserved2[280];
};

#if (Synology_case)
/**
 * Synology smart page (\ref NVME_SYNOLOGY_SMART)
 */
#pragma pack(1)
struct __attribute__((packed)) nvme_synology_smart_page
{
	u32 host_UNC_error_cnt;						//4
	u32 avg_erase_cnt;							//4
	u32 max_erase_cnt;							//4
	u32 total_early_bad_block_cnt;				//4
	u32 total_later_bad_block_cnt;				//4
	u16 nand_write_sector[4]; 					//8
	u16 host_write_sector[4];					//8
	u32 data_read_retry_cnt;					//4
	u8 rd_io;
	u8 reserved[215];
}synology_smart_info_t;
#pragma pack()
BUILD_BUG_ON(sizeof(synology_smart_info_t) != 256);

#pragma pack(1)
typedef struct _synology_smart_statistics_t
{
	u32 host_UNC_error_cnt;
	u32 avg_erase_cnt;
	u32 max_erase_cnt;
	u32 total_early_bad_block_cnt;
	u32 total_later_bad_block_cnt;
	u16 nand_write_sector[4];
	u16 host_write_sector[4];
	u32 data_read_retry_cnt;
	u8 rd_io;
	u8 reserved[215];
} synology_smart_statistics_t; //256 Bytes
#pragma pack()
BUILD_BUG_ON(sizeof(synology_smart_statistics_t) != 256);
#endif

#if CO_SUPPORT_OCP
/**
 * Datacenter OCP smart page (\ref NVME_OCP_SMART)
*/
#pragma pack(1)
struct __attribute__((packed)) nvme_ocp_smart_page
{
	/*Physical media units written*/
	u64 phy_media_units_written[2];
	/*Physical media units read*/
	u64 phy_media_units_read[2];
	/*Bad User NAND blocks*/
	u8 bad_user_nand_blocks_normalized_value[2];
	u16 bad_user_nand_blocks_raw_cnt[3];
	/*Bad System NAND blocks*/
	u8 bad_system_nand_blocks_normalized_value[2];
	u16 bad_system_nand_blocks_raw_cnt[3];
	/*XOR recovery count*/
	u64 xor_recovery_cnt;
	/*Uncorrectable read error count*/
	u64 uncorrect_read_err_cnt;
	/*Soft ECC error count*/
	u64 soft_ecc_err_cnt;
	/*End to end correction counts*/
	u32 end_to_end_correction_cnt[2];			//[0]->Corrected err cnt, [1]->Detected err cnt
	/*System data % used*/
	u8 system_data_used;
	/*Refresh counts*/
	u8 refresh_cnt[7];
	/*User data erase counts*/
	u32 user_data_erase_cnt[2];					//[0]->Minimum user data erase cnt, [1]->Maximum user data erase cnt
	/*Thermal throttlinh status and count*/
	u8 thermal_throttling_status_and_cnt[2];	//[0]->Current throttling status, [1]->Numbers of thermal throtling events
	/*DSSD specification verssion*/
	struct 	__attribute__((packed)) ocp_dssd_spec_version{
		u8 major_version;
		u16 minor_version;
		u16 point_version;
		u8	errata_version;
	}dssd_spec_version;	
	/*PCIE correctable error count*/
	u64 pcie_correctable_err_cnt;
	/*Incomplete  shutdowns*/
	u32 incomplete_shutdowns;
	/*Reserved*/
	u8 rsvd_1[4];
	/*% Free Blocks*/
	u8 free_blocks;
	/*Reserved*/
	u8 rsvd_2[7];
	/*Capacitor health*/
	u8 capacitor_health[2];
	/*NVMe errata version*/
	u8 nvme_errata_version;
	/*NVMe command set errata version*/
	u8 nvme_cmd_set_errata_version;
	/*Reserved*/
	u8 rsvd_3[4];
	/*Unaligned I/O*/
	u64 unaligned_io_cnt;
	/*Security version number*/
	u64 securiity_version_num;
	/*Total NUSE*/
	u64 total_nuse;
	/*PLP start count*/
	u64 plp_start_cnt[2];
	/*Endurance estimate*/
	u64 endurance_estimate[2];
	/*PCLe Link retraining count*/
	u64 pcie_link_retraining_cnt;
	/*Power  state change count*/
	u64 power_state_change_cnt;
	/*Lowest permitted firmware revision*/
	u64 lowest_permitted_fw_revision;
	/*Reserved*/
	u8 rsvd_4[278];
	/*Log page version*/
	u8 log_page_version[2];
	/*Log page GUID*/
	u64 log_page_GUID[2];
}ocp_smart_info_t;
#pragma pack()
BUILD_BUG_ON(sizeof(ocp_smart_info_t) != 512);

#pragma pack(1)
typedef struct _ocp_smart_statistics_t
{
	/*Physical media units written*/
	u64 phy_media_units_written[2];
	/*Physical media units read*/
	u64 phy_media_units_read[2];
	/*Bad User NAND blocks*/
	u8 bad_user_nand_blocks_normalized_value[2];
	u16 bad_user_nand_blocks_raw_cnt[3];
	/*Bad System NAND blocks*/
	u8 bad_system_nand_blocks_normalized_value[2];
	u16 bad_system_nand_blocks_raw_cnt[3];
	/*XOR recovery count*/
	u64 xor_recovery_cnt;
	/*Uncorrectable read error count*/
	u64 uncorrect_read_err_cnt;
	/*Soft ECC error count*/
	u64 soft_ecc_err_cnt;
	/*End to end correction counts*/
	u32 end_to_end_correction_cnt[2];			//[0]->Corrected err cnt, [1]->Detected err cnt
	/*System data % used*/
	u8 system_data_used;
	/*Refresh counts*/
	u8 refresh_cnt[7];
	/*User data erase counts*/
	u32 user_data_erase_cnt[2];					//[0]->Minimum user data erase cnt, [1]->Maximum user data erase cnt
	/*Thermal throttlinh status and count*/
	u8 thermal_throttling_status_and_cnt[2];	//[0]->Current throttling status, [1]->Numbers of thermal throtling events
	/*DSSD specification verssion*/
	struct 	__attribute__((packed)) ocp_dssd_spec_version_t{
		u8 major_version;
		u16 minor_version;
		u16 point_version;
		u8	errata_version;
	}dssd_spec_version_t;	
	/*PCIE correctable error count*/
	u64 pcie_correctable_err_cnt;
	/*Incomplete  shutdowns*/
	u32 incomplete_shutdowns;
	/*Reserved*/
	u8 rsvd_1[4];
	/*% Free Blocks*/
	u8 free_blocks;
	/*Reserved*/
	u8 rsvd_2[7];
	/*Capacitor health*/
	u8 capacitor_health[2];
	/*NVMe errata version*/
	u8 nvme_errata_version;
	/*NVMe command set errata version*/
	u8 nvme_cmd_set_errata_version;
	/*Reserved*/
	u8 rsvd_3[4];
	/*Unaligned I/O*/
	u64 unaligned_io_cnt;
	/*Security version number*/
	u64 securiity_version_num;
	/*Total NUSE*/
	u64 total_nuse;
	/*PLP start count*/
	u64 plp_start_cnt[2];
	/*Endurance estimate*/
	u64 endurance_estimate[2];
	/*PCLe Link retraining count*/
	u64 pcie_link_retraining_cnt;
	/*Power  state change count*/
	u64 power_state_change_cnt;
	/*Lowest permitted firmware revision*/
	u64 lowest_permitted_fw_revision;
	/*Reserved*/
	u8 rsvd_4[278];
	/*Log page version*/
	u8 log_page_version[2];
	/*Log page GUID*/
	u64 log_page_GUID[2];
} ocp_smart_statistics_t; //256 Bytes
#pragma pack()
BUILD_BUG_ON(sizeof(ocp_smart_statistics_t) != 512);
#endif

#if (Xfusion_case)
/**
 * Xfusion intel additional smart page
 */

typedef struct __attribute__((packed)) _nvme_intel_smart_log_item {
    u8	key;
	u8  _kp[2];
    u8	norm;
	u8  _np;
    union __attribute__((packed)){
    	u16 raw[3];
    	struct __attribute__((packed)) wear_level {
    		__le16	min;
    		__le16	max;
    		__le16	avg;
    	} wear_level;
    	struct  __attribute__((packed)) thermal_throttle {
    		u8	pct;
    		u32	count;
    	} thermal_throttle;
    };
	u8 _rp;
}nvme_intel_smart_log_item;

#pragma pack(1)
struct __attribute__((packed)) nvme_intel_smart_page
{
	nvme_intel_smart_log_item	program_fail_cnt;
	nvme_intel_smart_log_item	erase_fail_cnt;
	nvme_intel_smart_log_item	wear_leveling_cnt;
	nvme_intel_smart_log_item	e2e_err_cnt;
	nvme_intel_smart_log_item	crc_err_cnt;
	nvme_intel_smart_log_item	timed_workload_media_wear;
	nvme_intel_smart_log_item	timed_workload_host_reads;
	nvme_intel_smart_log_item	timed_workload_timer;
	nvme_intel_smart_log_item	thermal_throttle_status;
	nvme_intel_smart_log_item	retry_buffer_overflow_cnt;
	nvme_intel_smart_log_item	pll_lock_loss_cnt;
	nvme_intel_smart_log_item	nand_bytes_written;
	nvme_intel_smart_log_item	host_bytes_written;
	nvme_intel_smart_log_item	host_ctx_wear_used;
	nvme_intel_smart_log_item	perf_stat_indicator;
	nvme_intel_smart_log_item	re_alloc_sectr_cnt;
	nvme_intel_smart_log_item	soft_ecc_err_rate;
	nvme_intel_smart_log_item	unexp_power_loss;
	nvme_intel_smart_log_item	media_bytes_read;
	nvme_intel_smart_log_item	avail_fw_downgrades;
	u8 rsvd[16];
}intel_smart_info_t;
#pragma pack()
BUILD_BUG_ON(sizeof(intel_smart_info_t) != 256);
#endif

/**
 * Firmware slot information page (\ref NVME_LOG_FIRMWARE_SLOT)
 */
struct nvme_firmware_page
{
	struct
	{
		u8 slot : 3; /* slot for current FW */
		u8 rvsd3 : 1;
		u8 slot_next : 3; /* slot for next FW */
		u8 rvsd7 : 1;
	} afi;

	u8 reserved[7];
	u64 revision[7]; /* revisions for 7 slots */
	u8 reserved2[448];
};

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT

typedef struct _nvme_debug_reg_info_t {
	//pcie core //36
	u32 status_commnad; //0x0004
	u32 pm_con_status; //0x0044
	u32 device_capabilities; //0x0074
	u32 device_control_device_status; //0x0078
	u32 link_capabilities; //0x007C
	u32 link_control_link_status; //0x0080
	u32 device_capablities2; //0x0094
	u32 device_control_device_status2; //0x0098
	u32 link_capabilities2; //0x009C
	u32 link_control_link_status2; //0x00A0
	u32 uncorr_err_status; //0x0104
	u32 uncorr_err_sev; //0x010C
	u32 corr_err_status; //0x0110
	u32 adv_err_cap_ctrl; //0x0118
	u32 tlp_prefix_log1; //0x0138;
	u32 tlp_prefix_log2; //0x013C;
	u32 tlp_prefix_log3; //0x0140;
	u32 tlp_prefix_log4; //0x0144;
	u32 lane_err_status; //0x0160
	u32 pl16G_status; //0x0184
	u32 pl16G_lc_dpar_status; //0x0188
	u32 pl16G_first_retimer_dpar_status; //0x018C
	u32 pl16G_second_retimer_dpar_status; //0x0190
	u32 margin_lane0_cntrl_status; //0x01A4
	u32 margin_lane1_cntrl_status; //0x01A8
	u32 margin_lane2_cntrl_status; //0x01AC
	u32 margin_lane3_cntrl_status; //0x01B0
	u32 system_page_size; //0x01D4
	u32 ltr_latency; //0x01F8
	u32 l1sub_capbilities; //0x0200
	u32 l1sub_control1; //0x0204
	u32 l1sub_control2; //0x0208
	u32 rasdp_corr_counter_ctrl; //0x0218
	u32 rasdp_corr_counter_report; //0x021C
	u32 rasdp_uncorr_counter_ctrl; //0x0220
	u32 rasdp_uncorr_counter_report; //0x0224


	//pcie wrap //12
	u32 pcie_core_status; //0x018
	u32 pcie_powr_status; //0x01C
	u32 cfg_mps_mrr; //0x0A8
	u32 pcie_core_status2; //0x0B4
	u32 rxsts_l0_rcvry_log0; //0x0D0
	u32 rxsts_l0_rcvry_log1; //0x0D4
	u32 rxsts_l0_rcvry_log2; //0x0D8
	u32 rxsts_l0_rcvry_log3; //0x0DC
	u32 rxsts_l0_rcvry_log4; //0x0E0
	u32 rxsts_l0_rcvry_log5; //0x0E4
	u32 rxsts_l0_rcvry_log6; //0x0E8
	u32 rxsts_l0_rcvry_log7; //0x0EC

	//nvme ctrl - pf 0x0000~0x2fff  //65
	u32 controller_cap_low; //0x000
	u32 controller_cap_high; //0x004
	u32 controller_config; //0x014
	u32 controller_status; //0x01C
	u32 admQ_attr; //0x024
	u32 adm_sq_base_addr_l; //0x028
	u32 adm_sq_base_addr_h; //0x02C
	u32 adm_cq_base_addr_l; //0x030
	u32 adm_cq_base_addr_h; //0x034

	u32 internal_int_status; //0x1760
	u32 intv01_coalescing_cnts; //0x1980
	u32 intv23_coalescing_cnts; //0x1984
	u32 intv45_coalescing_cnts; //0x1988
	u32 intv67_coalescing_cnts; //0x198C
	u32 intv8_coalescing_cnts; //0x1990

	u32 arbitration_control_sts; //0x1A00

	u32 nvme_unmasked_int_status; //0x2000
	u32 nvme_masked_int_status_cpu1; //0x2008
	u32 nvme_masked_int_status_cpu2; //0x2010
	u32 nvme_ctrl_status; //0x2014
	u32 outstanding_sq_bitmap; //0x2018
	u32 nvme_axi_interface_ctrl; //0x201C
	u32 sq_pointer_updt_errs; //0x2030
	u32 cq_pointer_updt_errs; //0x2034
	u32 errants_sq_pointers; //0x2038
	u32 errants_cq_pointers; //0x203C
	u32 nvme_misc_errors; //0x2040
	u32 dbl_sram_p_err_addr; //0x2044
	u32 dbl_sram_p_err_data; //0x2048
	u32 pcie_errant_awlen; //0x2050
	u32 pcie_errant_awlen_addr; //0x2054
	u32 pcie_errant_arlen; //0x2058
	u32 pcie_errant_arlen_addr; //0x205C
	u32 pcie_p_err_awadr; //0x2060
	u32 pcie_p_err_wdata; //0x2064
	u32 pcie_p_err_aradr; //0x2068
	u32 cmd_fetch_err_bmap; //0x206C
	u32 queue_full_bitmap; //0x2070
	u32 fw_cq_err_log; //0x2074
	u32 hw_cq_buf_ctrl_sts; //0x2080
	u32 hw_cq_buf_pointers; //0x2084
	u32 hw_cq_buf_content; //0x2088
	u32 hw_cq_wait_time; //0x208C
	u32 cmpl_gen_dbg_info; //0x2090
	u32 cmpl_gen_dbg_ctrl; //0x2094
	u32 dbl_blk_dbg_info; //0x2098

	u32 pcie_lpbk_enable; //0x20B0
	u32 pcie_lpbk_data_pattern; //0x20B4
	u32 pcie_lpbk_addr_low; //0x20B8
	u32 pcie_lpbk_addr_high; //0x20BC
	u32 pcie_lpbk_status; //0x20C0
	u32 nvme_masked_int_status_cpu3; //0x20D4
	u32 nvme_masked_int_status_cpu4; //0x20DC
	u32 dbl_busy_time; //0x20E8
	u32 outstanding_cmd_cnt; //0x20EC

	u32 dma_queue0_ctrl_sts; //0x21A0
	u32 dma_queue1_ctrl_sts; //0x21B0

	u32 hmb_nand_tbl_ctrl; //0x2200
	u32 hmb_ntbl_aotu_lkup_ctrl_sts; //0x2220
	u32 fw_ntbl_access_ctrl_sts; //0x2234
	u32 hmb_ntbl_updt_transaction_id; //0x2250
	u32 hmb_inter_status_0; //0x2280
	u32 hmb_inter_status_1; //0x2284
	u32 hmb_inter_status_2; //0x2288
	u32 hmb_ntbl_ns_ctrl_sts; //0x2290

	//nvme proc - 0xc003_c000 //22
	u32 unmask_err; //0x200
	u32 mask_err; //0x208
	u32 port_reset_ctrl; //0x214
	u32 cmd_q_base; //0x218
	u32 cmd_q_ptr; //0x21C
	u32 cmd_rx_q_base; //0x220
	u32 cmd_rx_q_bas1; //0x224
	u32 cmd_rx_q_ptr; //0x228
	u32 cmd_cmpl_q_base; //0x22C
	u32 cmd_cmpl_q_ptr; //0x230
	u32 host_ctrl; //0x234
	u32 abort_xfer; //0x238

	u32 hmb_tbl_ctrl; //0x250
	u32 hmb_err_stts; //0x26C

	u32 srmb_key_0; //0x2A0
	u32 srmb_key_1; //0x2A4
	u32 srmb_key_2; //0x2A8
	u32 srmb_key_3; //0x2AC
	u32 dcrc_key_0; //0x2B0
	u32 dcrc_key_1; //0x2B4
	u32 dcrc_key_2; //0x2B8
	u32 dcrc_key_3; //0x2BC

	//btn - 0xC001_0000~0xC001_7FFF  //51
	u32 btn_um_int_sts; //0x0000
	u32 btn_control_reg; //0x0044
	u32 btn_ctrl_status; //0x0048
	u32 btn_err_status; //0x004C
	u32 btn_sram_ecc_err_sts; //0x0050
	u32 rd_hcrc_err_data; //0x0054
	u32 dxfr_sram_ecc_sts; //0x0058
	u32 inter_dbg_sts_select; //0x0060
	u32 selected_inter_dbg_sts; //0x0064
	u32 btn_ctag_pointers; //0x006C

	u32 wr_semistrm_ctrl_status; //0x0070
	u32 wr_semistrm_list0_status; //0x0074
	u32 wr_semistrm_list1_status; //0x0078

	u32 dtag_list_ctrl_status; //0x0080

	u32 wd_entry_list_ctrl_status; //0x00C0

	u32 rd_entry_list_ctrl_status; //0x0100

	u32 btn_auto_updt_enable; //0x0150
	u32 btn_wd_adrs_surc0_base; //0x0154
	u32 btn_wd_adrs_surc0_ptrs; //0x0158
	u32 btn_wd_adrs0_rptr_dbase; //0x015C
	u32 btn_wd_adrs0_updt_base; //0x0160
	u32 btn_wd_adrs0_updt_ptrs; //0x0164
	u32 btn_wd_adrs0_wptr_dbase; //0x0168
	u32 btn_wd_adrs_surc1_base; //0x016C
	u32 btn_wd_adrs_surc1_ptrs; //0x0170
	u32 btn_wd_adrs1_rptr_dbase; //0x0174
	u32 btn_wd_adrs1_updt_base; //0x0178
	u32 btn_wd_adrs1_updt_ptrs; //0x017C
	u32 btn_wd_adrs1_wptr_dbase; //0x0180

	u32 btn_rd_adrs_surc0_base; //0x0184
	u32 btn_rd_adrs_surc0_ptrs; //0x0188
	u32 btn_rd_adrs0_rptr_dbase; //0x018C
	u32 btn_rd_adrs0_updt_base; //0x0190
	u32 btn_rd_adrs0_updt_ptrs; //0x0194
	u32 btn_rd_adrs0_wptr_dbase; //0x0198
	u32 btn_rd_adrs_surc1_base; //0x019C
	u32 btn_rd_adrs_surc1_ptrs; //0x01A0
	u32 btn_rd_adrs1_rptr_dbase; //0x01A4
	u32 btn_rd_adrs1_updt_base; //0x01A8
	u32 btn_rd_adrs1_updt_ptrs; //0x01AC
	u32 btn_rd_adrs1_wptr_dbase; //0x01B0

	u32 btn_wd_adrs0_updt1_base; //0x01B4
	u32 btn_wd_adrs0_updt1_ptrs; //0x01B8
	u32 btn_wd_adrs0_wptr1_dbase; //0x01BC
	u32 btn_wd_adrs0_updt2_base; //0x01C0
	u32 btn_wd_adrs0_updt2_ptrs; //0x01C4
	u32 btn_wd_adrs0_wptr2_dbase; //0x01C8

	u32 btn_com_free_dtag_dbase; //0x01E4
	u32 btn_com_free_dtag_ptrs; //0x01E8
	u32 btn_com_free_wptr_dbase; //0x01EC
	u32 data_entry_err_count; //0x01F0
}nvme_debug_reg_info_t;

typedef struct _eccu_reg_info_t {
	//NCB ECCU_TOP
	u32 eccu_ver_reg;  // 0x0000
	u32 eccu_ctrl_reg0;  // 0x0004
	u32 eccu_ctrl_reg1;  // 0x0008
	u32 eccu_ctrl_reg2;  // 0x000c
	u32 eccu_ctrl_reg3;  // 0x0010
	u32 eccu_ctrl_reg4;  // 0x0014
	u32 eccu_ctrl_reg5;  // 0x0018
	u32 eccu_du_fmt_sel_reg;  // 0x001c
	u32 eccu_du_fmt_reg0;  // 0x0020
	u32 eccu_du_fmt_reg1;  // 0x0024
	u32 eccu_du_fmt_reg2;  // 0x0028
	u32 eccu_du_fmt_reg3;  // 0x002c
	u32 eccu_du_fmt_reg4;  // 0x0030
	u32 eccu_du_fmt_reg5;  // 0x0034
	u32 eccu_du_fmt_reg6;  // 0x0038
	u32 eccu_enc_status_reg0;  // 0x003c
	u32 eccu_enc_status_reg1;  // 0x0040
	u32 eccu_enc_status_reg2;  // 0x0044
	u32 eccu_dec_status_reg0;  // 0x0048
	u32 eccu_dec_status_reg1;  // 0x004c
	u32 eccu_dec_status_reg2;  // 0x0050
	u32 eccu_dec_status_reg3;  // 0x0054
	u32 eccu_dec_status_reg4;  // 0x0058
	u32 eccu_dec_status_reg5;  // 0x005c
	u32 eccu_dec_status_reg6;  // 0x0060
	u32 eccu_dbg_reg0;	// 0x0064
	u32 eccu_dbg_reg1;	// 0x0068
	u32 eccu_mem_cpu_acc_reg0;	// 0x006c
	u32 eccu_mem_cpu_acc_reg1;	// 0x0070
	u32 ecc_clk_gate_ctrl_reg;	// 0x0074
	u32 rsvd_78[2];  // 0x0078
	u32 eccu_fdma_cfg_reg;	// 0x0080
	u32 eccu_fdma_cfg_addr_reg;  // 0x0084
	u32 eccu_fdma_cfg_xfsz_reg;  // 0x0088
	u32 eccu_rgbk_cmf_cfg_reg0;  // 0x008c
	u32 eccu_rgbk_cmf_cfg_reg1;  // 0x0090
	u32 eccu_rgbk_cmf_cfg_reg2;  // 0x0094
	u32 eccu_fault_reg0;  // 0x0098
	u32 rsvd_9c[25];  // 0x009c
	u32 fdma_ctrl_reg0;  // 0x0100
	u32 fdma_loopbk_ctrl_reg;  // 0x0104
	u32 fdma_loopbk_addr_low_reg;  // 0x0108
	u32 fdma_debug_reg;  // 0x010c
	u32 eccu_conf_reg;	// 0x0110
	u32 rsvd_114[3];  // 0x0114
	u32 eccu_mem_timing_ctrl_reg0;	// 0x0120
	u32 eccu_mem_timing_ctrl_reg1;	// 0x0124
	u32 eccu_mem_timing_ctrl_reg2;	// 0x0128
	u32 eccu_rom_sz_reg0;  // 0x012c
	u32 eccu_partial_du_detect_reg0;  // 0x0130
	u32 eccu_partial_du_detect_reg1;  // 0x0134
	u32 rsvd_138[50];		//0x0138
	//NCB DEC_TOP_CTRL
	u32 dec_top_ctrs_reg0;             // 0x0200
	u32 dec_top_ctrs_reg1;             // 0x0204
	u32 dec_top_ctrs_reg2;             // 0x0208
	u32 dec_top_ctrs_reg3;             // 0x020c
	u32 dec_dsm_reg0;                  // 0x0210
	u32 dec_dsm_reg1;                  // 0x0214
	u32 dec_dsm_reg2;                  // 0x0218
	u32 dec_dsm_reg3;                  // 0x021c
	u32 dec_dsm_reg4;                  // 0x0220
	u32 dec_ard_llrm_reg0;             // 0x0224
	u32 rsvd_28;                       // 0x0228
	u32 dec_clk_gate_ctrl_reg;  	   // 0x022c
	u32 dec_01_accu_reg0;              // 0x0230
	u32 dec_01_accu_reg1;              // 0x0234
	u32 dec_bed_reg0;                  // 0x0238
	u32 dec_bed_reg1;                  // 0x023c
	u32 dec_erd_ddr_st_addr_reg;       // 0x0240
	u32 dec_erd_ddr_end_addr_reg;      // 0x0244
	u32 dec_erd_ddr_st;                // 0x0248
	u32 rsvd_24c[45];				   // 0x024c
	//NCB ECCU_MDEC_0
	u32 mdec_ctrs_reg0;   // 0x0300
	u32 mdec_ctrs_reg1;   // 0x0304
	u32 mdec_ctrs_reg2;   // 0x0308
	u32 mdec_ctrs_reg3;   // 0x030c
	u32 mdec_ctrs_reg4;   // 0x0310
	u32 mdec_dbg_reg;     // 0x0314
	u32 mdec_mem_reg0;    // 0x0318
	u32 mdec_mbist_reg;   // 0x031c
	u32 rsvd_320[56];	  // 0x0320
	//NCB ECCU_MDEC_1
	u32 mdec_ctrs_reg0_1;   // 0x0400
	u32 mdec_ctrs_reg1_1;   // 0x0404
	u32 mdec_ctrs_reg2_1;   // 0x0408
	u32 mdec_ctrs_reg3_1;   // 0x040c
	u32 mdec_ctrs_reg4_1;   // 0x0410
	u32 mdec_dbg_reg_1;     // 0x0414
	u32 mdec_mem_reg0_1;    // 0x0418
	u32 mdec_mbist_reg_1;   // 0x041c
	u32 rsvd_420[56];	  // 0x0420
	//NCB ECCU_PDEC
	u32 pdec_ctrs_reg0;                 // 0x0500
	u32 pdec_ctrs_reg1;                 // 0x0504
	u32 pdec_ctrs_reg2;                 // 0x0508
	u32 pdec_ctrs_reg3;                 // 0x050c
	u32 pdec_ctrs_reg4;                 // 0x0510
	u32 pdec_ctrs_reg5;                 // 0x0514
	u32 pdec_ctrs_reg6;                 // 0x0518
	u32 pdec_ctrs_reg7;                 // 0x051c
	u32 pdec_ctrs_reg8;                 // 0x0520
	u32 pdec_mem_reg0;                  // 0x0524
	u32 pdec_mem_reg1;                  // 0x0528
	u32 pdec_dbg_reg;                   // 0x052c
	u32 pdec_erd_llr_lut_reg0;          // 0x0530
	u32 pdec_erd_llr_lut_reg1;          // 0x0534
	u32 pdec_erd_llr_lut_reg2;          // 0x0538
	u32 pdec_fault_reg0;                // 0x053c
	u32 pdec_llrmap_reg0;               // 0x0540
	u32 pdec_llrmap_reg1;               // 0x0544
	u32 pdec_llrmap_reg2;               // 0x0548
	u32 pdec_llrmap_reg3;               // 0x054c
	u32 pdec_llrmap_reg4;               // 0x0550
	u32 pdec_llrmap_reg5;               // 0x0554
	u32 pdec_llrmap_reg6;               // 0x0558
	u32 pdec_llrmap_reg7;               // 0x055c
	u32 rsvd_560[104];					// 0x0560
	//NCB ECCU_LDPC_ENC
	u32 enc_ctrl_reg0;		// 0x0700
	u32 enc_status_reg0;	// 0x0704
	u32 enc_dbg_reg;		// 0x0708
	u32 enc_mem_reg;		// 0x070c
	u32 enc_mbist_reg;		// 0x0710
	u32 enc_fault_reg0; 	// 0x0714
	u32 rsvd_718[58];		// 0x0718 align 0x600+0x800
}eccu_reg_info_t;  //0X23c = 572Byte  //0x714 = 1812Byte  

typedef struct _ficu_reg_info_t {
	u32 ficu_ctrl_reg0;  // 0x1000
	u32 ficu_ctrl_reg1;  // 0x1004
	u32 ficu_int_en_reg0;  // 0x1008
	u32 ficu_int_status_reg0;  // 0x100c
	u32 ficu_int_src_reg;  // 0x1010
	u32 ficu_status_reg0;  // 0x1014
	u32 ficu_status_reg1;  // 0x1018
	u32 ficu_status_reg2;  // 0x101c
	u32 ficu_status_reg3;  // 0x1020
	u32 ficu_status_reg4;  // 0x1024
	u32 ficu_status_reg5;  // 0x1028
	u32 ficu_status_reg6;  // 0x102c
	u32 rsvd_30[9];  // 0x1030
	u32 ficu_lut_st_addr_reg;  // 0x1054
	u32 ficu_lut_end_addr_reg;	// 0x1058
	u32 ficu_erd_para_fifo_st_addr_reg;  // 0x105c
	u32 ficu_erd_para_fifo_end_addr_reg;  // 0x1060
	u32 ficu_erd_para_fifo_ptr_reg;  // 0x1064
	u32 rsvd_68[3];  // 0x1068
	u32 ficu_du_logs_fifo_st_addr_reg;	// 0x1074
	u32 ficu_du_logs_fifo_end_addr_reg;  // 0x1078
	u32 ficu_du_logs_fifo_ptr_reg;	// 0x107c
	u32 ficu_get_param_fifo_st_addr_reg;  // 0x1080
	u32 ficu_get_param_fifo_end_addr_reg;  // 0x1084
	u32 ficu_get_param_fifo_ptr_reg;  // 0x1088
	u32 ficu_ard_conf_reg0;  // 0x108c
	u32 ficu_ard_conf_reg1;  // 0x1090
	u32 ficu_ard_conf_reg2;  // 0x1094
	u32 ficu_ard_conf_reg3;  // 0x1098
	u32 ficu_ard_conf_reg4;  // 0x109c
	u32 ficu_ard_conf_reg5;  // 0x10a0
	u32 ficu_ard_conf_reg6;  // 0x10a4
	u32 ficu_ard_conf_reg7;  // 0x10a8
	u32 ficu_xfcnt_lut_conf_reg;  // 0x10ac
	u32 ficu_xfcnt_lut_val_reg;  // 0x10b0
	u32 ficu_fspm_init_reg;  // 0x10b4
	u32 rsvd_b8[2];  // 0x10b8
	u32 ficu_vsc_tmpl_info_conf_reg;  // 0x10c0
	u32 ficu_vsc_tmpl_info_reg;  // 0x10c4
	u32 ficu_fcmd_cq_st_addr_reg;  // 0x10c8
	u32 ficu_fcmd_cq_end_addr_reg;	// 0x10cc
	u32 ficu_fcmd_cq_cnt_reg;  // 0x10d0
	u32 ficu_ch_mapping_reg;  // 0x10d4
	u32 ficu_ch_rev_mapping_reg;  // 0x10d8
	u32 ficu_ref_mask_reg0;  // 0x10dc
	u32 ficu_ref_mask_reg1;  // 0x10e0
	u32 ficu_mp_row_offset;  // 0x10e4
	u32 ficu_fspm_st_addr_reg;	// 0x10e8
	u32 ficu_fspm_end_addr_reg;  // 0x10ec
	u32 ficu_sys_sram_st_addr_reg;	// 0x10f0
	u32 ficu_sys_sram_end_addr_reg;  // 0x10f4
	u32 ficu_fda_dtag_list_dtcm_base_addr0_reg;  // 0x10f8
	u32 ficu_fda_dtag_list_dtcm_base_addr1_reg;  // 0x10fc
	u32 ficu_fda_dtag_list_sram_base_addr0_reg;  // 0x1100
	u32 ficu_fda_dtag_list_sram_base_addr1_reg;  // 0x1104
	u32 ficu_cost_llr_list_dtcm_base_addr0_reg;  // 0x1108
	u32 ficu_cost_llr_list_dtcm_base_addr1_reg;  // 0x110c
	u32 ficu_cost_llr_list_sram_base_addr0_reg;  // 0x1110
	u32 ficu_cost_llr_list_sram_base_addr1_reg;  // 0x1114
	u32 ficu_lda_list_dtcm_base_addr0_reg;	// 0x1118
	u32 ficu_lda_list_dtcm_base_addr1_reg;	// 0x111c
	u32 ficu_lda_list_sram_base_addr0_reg;	// 0x1120
	u32 ficu_lda_list_sram_base_addr1_reg;	// 0x1124
	u32 ficu_meta_dtag_map_sram_base_addr_reg;	// 0x1128
	u32 ficu_meta_dtag_map_ddr_base_addr_reg;  // 0x112c
	u32 ficu_meta_idx_map_sram_base_addr_reg;  // 0x1130
	u32 ficu_meta_idx_map_ddr_base_addr_reg;  // 0x1134
	u32 ficu_meta_dtag_idx_map_unit_size_reg;  // 0x1138
	u32 ficu_meta_auto_idx_enc_base_addr0_reg;	// 0x113c
	u32 ficu_meta_auto_idx_enc_base_addr1_reg;	// 0x1140
	u32 ficu_meta_auto_idx_dec_base_addr0_reg;	// 0x1144
	u32 ficu_meta_auto_idx_dec_base_addr1_reg;	// 0x1148
	u32 ficu_meta_dtag_map_hmb_base_addr_reg;  // 0x114c
	u32 ficu_meta_idx_map_hmb_base_addr_reg;  // 0x1150
	u32 rsvd_154[5];  // 0x1154
	u32 ficu_pda_conf_reg0;  // 0x1168
	u32 ficu_pda_conf_reg1;  // 0x116c
	u32 ficu_row_addr_conf_reg;  // 0x1170
	u32 ficu_pda_conf_reg2;  // 0x1174
	u32 ficu_nf_finst_cnt_reg;	// 0x1178
	u32 rsvd_17c;  // 0x117c
	u32 ficu_du_done_cnt_reg;  // 0x1180
	u32 ficu_du_rcv_done_cnt_reg;  // 0x1184
	u32 ficu_du_start_cnt_reg;	// 0x1188
	u32 ficu_fcmd_status_reg0;	// 0x118c
	u32 ficu_fcmd_status_reg1;	// 0x1190
	u32 ficu_fcmd_status_reg2;	// 0x1194
	u32 ficu_fcmd_status_reg3;	// 0x1198
	u32 ficu_finst_status_reg0;  // 0x119c
	u32 ficu_finst_status_reg1;  // 0x11a0
	u32 ficu_finst_status_reg2;  // 0x11a4
	u32 ficu_ard_finst_status_reg0;  // 0x11a8
	u32 ficu_ard_du_cnt_reg;  // 0x11ac
	u32 ficu_enc_raid_par_du_cnt_reg;  // 0x11b0
	u32 ficu_dec_raid_par_du_cnt_reg;  // 0x11b4
	u32 ficu_dummy_du_dtag_reg;  // 0x11b8
	u32 ficu_fspm_ecc_err_inject;  // 0x11bc
	u32 ficu_fspm_norm_finst_que0_cnt_reg;	// 0x11c0
	u32 ficu_fspm_high_finst_que0_cnt_reg;	// 0x11c4
	u32 ficu_fspm_urgent_finst_que0_cnt_reg;  // 0x11c8
	u32 ficu_fspm_norm_finst_que0_st_addr_reg;	// 0x11cc
	u32 ficu_fspm_norm_finst_que0_end_addr_reg;  // 0x11d0
	u32 ficu_fspm_norm_finst_que0_ptr_reg;	// 0x11d4
	u32 ficu_fspm_high_finst_que0_st_addr_reg;	// 0x11d8
	u32 ficu_fspm_high_finst_que0_end_addr_reg;  // 0x11dc
	u32 ficu_fspm_high_finst_que0_ptr_reg;	// 0x11e0
	u32 ficu_fspm_urgent_finst_que0_st_addr_reg;  // 0x11e4
	u32 ficu_fspm_urgent_finst_que0_end_addr_reg;  // 0x11e8
	u32 ficu_fspm_urgent_finst_que0_ptr_reg;  // 0x11ec
	u32 ficu_fspm_norm_finst_que1_cnt_reg;	// 0x11f0
	u32 ficu_fspm_high_finst_que1_cnt_reg;	// 0x11f4
	u32 ficu_fspm_urgent_finst_que1_cnt_reg;  // 0x11f8
	u32 ficu_fspm_norm_finst_que1_st_addr_reg;	// 0x11fc
	u32 ficu_fspm_norm_finst_que1_end_addr_reg;  // 0x1200
	u32 ficu_fspm_norm_finst_que1_ptr_reg;	// 0x1204
	u32 ficu_fspm_high_finst_que1_st_addr_reg;	// 0x1208
	u32 ficu_fspm_high_finst_que1_end_addr_reg;  // 0x120c
	u32 ficu_fspm_high_finst_que1_ptr_reg;	// 0x1210
	u32 ficu_fspm_urgent_finst_que1_st_addr_reg;  // 0x1214
	u32 ficu_fspm_urgent_finst_que1_end_addr_reg;  // 0x1218
	u32 ficu_fspm_urgent_finst_que1_ptr_reg;  // 0x121c
	u32 rsvd_220[4];  // 0x1220
	u32 ficu_int_en_reg1;  // 0x1230
	u32 ficu_int_status_reg1;  // 0x1234
	u32 ficu_int_en_reg2;  // 0x1238
	u32 ficu_int_status_reg2;  // 0x123c
	u32 ficu_int_en_reg3;  // 0x1240
	u32 ficu_int_status_reg3;  // 0x1244
	u32 rsvd_248[46];  // 0x1248
	u32 ficu_debug_reg;  // 0x1300
	u32 ficu_debug_pin_reg;  // 0x1304
	u32 ficu_debug_st_reg;	// 0x1308
	u32 ficu_ctrl_reg2;  // 0x130c
	u32 ficu_ard_finst_status_reg1;  // 0x1310
	u32 ficu_ard_finst_status_reg2;  // 0x1314
	u32 ficu_fspm_sram_ecc_err;  // 0x1318
	u32 ficu_dec_erd_du_st_cnt_reg;  // 0x131c
	u32 ficu_dec_erd_du_done_cnt_reg;  // 0x1320
	u32 rsvd_324[55];  // 0x1324
	u32 ficu_norm_finst_que0_base_addr;  // 0x1400
	u32 ficu_norm_finst_que0_ptrs;	// 0x1404
	u32 ficu_norm_finst_que0_rdptr_base_addr;  // 0x1408
	u32 ficu_high_finst_que0_base_addr;  // 0x140c
	u32 ficu_high_finst_que0_ptrs;	// 0x1410
	u32 ficu_high_finst_que0_rdptr_base_addr;  // 0x1414
	u32 ficu_urgent_finst_que0_base_addr;  // 0x1418
	u32 ficu_urgent_finst_que0_ptrs;  // 0x141c
	u32 ficu_urgent_finst_que0_rdptr_base_addr;  // 0x1420
	u32 ficu_norm_finst_que1_base_addr;  // 0x1424
	u32 ficu_norm_finst_que1_ptrs;	// 0x1428
	u32 ficu_norm_finst_que1_rdptr_base_addr;  // 0x142c
	u32 ficu_high_finst_que1_base_addr;  // 0x1430
	u32 ficu_high_finst_que1_ptrs;	// 0x1434
	u32 ficu_high_finst_que1_rdptr_base_addr;  // 0x1438
	u32 ficu_urgent_finst_que1_base_addr;  // 0x143c
	u32 ficu_urgent_finst_que1_ptrs;  // 0x1440
	u32 ficu_urgent_finst_que1_rdptr_base_addr;  // 0x1444
	u32 rsvd_448[6];  // 0x1448
	u32 ficu_grp1_fcmd_compl_que_base_addr;  // 0x1460
	u32 ficu_grp1_fcmd_compl_que_ptrs;	// 0x1464
	u32 ficu_grp1_fcmd_compl_que_wrptr_base_addr;  // 0x1468
	u32 ficu_grp0_fcmd_compl_que_base_addr;  // 0x146c
	u32 ficu_grp0_fcmd_compl_que_ptrs;	// 0x1470
	u32 ficu_grp0_fcmd_compl_que_wrptr_base_addr;  // 0x1474
	u32 rsvd_1478[34];				// 0x1478  align 0x600+0x800+0x500 
}ficu_reg_info_t;  //0X478 = 1144Byte  

typedef struct _ndcu_reg_info_t {
	u32 nf_ctrl_reg0;                  // 0x2000
	u32 nf_ind_reg0;                   // 0x2004
	u32 nf_ind_reg1;                   // 0x2008
	u32 nf_ind_reg2;                   // 0x200C
	u32 nf_ind_reg3;                   // 0x2010
	u32 nf_ind_reg4;                   // 0x2014
	u32 nf_ind_reg5;                   // 0x2018
	u32 nf_ind_reg6;                   // 0x201C
	u32 nf_async_tim_reg0;             // 0x2020
	u32 nf_sync_tim_reg0;              // 0x2024
	u32 nf_sync_tim_reg1;              // 0x2028
	u32 nf_sync_tim_reg2;              // 0x202C
	u32 nf_tgl_tim_reg0;               // 0x2030
	u32 nf_tgl_tim_reg1;               // 0x2034
	u32 nf_gen_tim_reg0;               // 0x2038
	u32 nf_gen_tim_reg1;               // 0x203C
	u32 nf_tim_ctrl_reg0;              // 0x2040
	u32 nf_tim_ctrl_reg1;              // 0x2044
	u32 nf_tim_ctrl_reg2;              // 0x2048
	u32 nf_tim_ctrl_reg3;              // 0x204C
	u32 nf_tim_ctrl_reg4;              // 0x2050
	u32 nf_tim_ctrl_reg5;              // 0x2054
	u32 nf_tim_ctrl_reg6;              // 0x2058
	u32 nf_tim_ctrl_reg7;              // 0x205C
	u32 nf_rcmd_reg00;                 // 0x2060
	u32 nf_rcmd_reg01;                 // 0x2064
	u32 nf_rcmd_reg10;                 // 0x2068
	u32 nf_rcmd_reg11;                 // 0x206C
	u32 nf_rcmd_mp_reg00;              // 0x2070
	u32 nf_rcmd_mp_reg01;              // 0x2074
	u32 nf_rcmd_mp_reg10;              // 0x2078
	u32 nf_rcmd_mp_reg11;              // 0x207C
	u32 nf_rxcmd_reg0;                 // 0x2080
	u32 nf_rxcmd_reg1;                 // 0x2084
	u32 nf_rxcmd_mp_reg0;              // 0x2088
	u32 nf_rxcmd_mp_reg1;              // 0x208C
	u32 nf_rdcmd_dummy;                 // 0x2090
	u32 nf_pcmd_reg00;                 // 0x2094
	u32 nf_pcmd_reg01;                 // 0x2098
	u32 nf_pcmd_reg10;                 // 0x209C
	u32 nf_pcmd_reg11;                 // 0x20A0
	u32 nf_pcmd_mp_reg00;              // 0x20A4
	u32 nf_pcmd_mp_reg01;              // 0x20A8
	u32 nf_pcmd_mp_reg10;              // 0x20AC
	u32 nf_pcmd_mp_reg11;              // 0x20B0
	u32 nf_ecmd_reg0;                  // 0x20B4
	u32 nf_ecmd_reg1;                  // 0x20B8
	u32 nf_ecmd_mp_reg0;               // 0x20BC
	u32 nf_ecmd_mp_reg1;               // 0x20C0
	u32 nf_ecmd_jdc0;                  // 0x20C4
	u32 nf_scmd_reg0;                  // 0x20C8
	u32 nf_scmd_reg1;                  // 0x20CC
	u32 nf_rstcmd_reg0;                // 0x20D0
	u32 nf_rstcmd_reg1;                // 0x20D4
	u32 nf_precmd_reg00;               // 0x20D8
	u32 nf_precmd_reg01;               // 0x20DC
	u32 nf_precmd_reg10;               // 0x20E0
	u32 nf_precmd_reg11;               // 0x20E4
	u32 nf_precmd_reg20;               // 0x20E8
	u32 nf_precmd_reg21;               // 0x20EC
	u32 nf_wcnt_reg0;                  // 0x20F0
	u32 nf_wcnt_reg1;                  // 0x20F4
	u32 nf_wcnt_reg2;                  // 0x20F8
	u32 nf_wcnt_reg3;                  // 0x20FC
	u32 nf_pcnt_reg0;                  // 0x2100
	u32 nf_pcnt_reg1;                  // 0x2104
	u32 nf_fail_reg0;                  // 0x2108
	u32 nf_fail_reg1;                  // 0x210C
	u32 nf_fail_reg2;                  // 0x2110
	u32 nf_fail_reg3;                  // 0x2114
	u32 nf_fail_reg4;                  // 0x2118
	u32 nf_rdy_reg0;                   // 0x211C
	u32 nf_lun_ctrl_reg0;              // 0x2120
	u32 nf_pdwn_ctrl_reg0;             // 0x2124
	u32 nf_ddr_data_dly_ctrl_reg0;     // 0x2128
	u32 nf_ce_remap_reg0;              // 0x212C
	u32 nf_ce_remap_reg1;              // 0x2130
	u32 nf_ce_remap_reg2;              // 0x2134
	u32 nf_ce_remap_reg3;              // 0x2138
	u32 nf_ce_remap_reg4;              // 0x213C
	u32 nf_ce_remap_reg5;              // 0x2140
	u32 nf_ce_remap_reg6;              // 0x2144
	u32 nf_ce_remap_reg7;              // 0x2148
	u32 rsvd_14c[9];                    // 0x214C
	u32 nf_pwdog_timer_reg0;           // 0x2170
	u32 nf_ewdog_timer_reg0;           // 0x2174
	u32 nf_force_nd_err_reg0;          // 0x2178
	u32 rsvd_17c;                       // 0x217C
	u32 nf_fail_reg5;                   // 0x2180
	u32 nf_fail_reg6;                   // 0x2184
	u32 nf_fail_reg7;                   // 0x2188
	u32 rsvd_18c;                       // 0x218C
	u32 nf_fail_reg_sp;                 // 0x2190
	u32 nf_fail_reg8;                   // 0x2194
	u32 nf_fail_reg9;                   // 0x2198
	u32 nf_fail_reg10;                  // 0x219C
	u32 nf_fail_reg11;                  // 0x21A0
	u32 nf_fail_reg12;                  // 0x21A4
	u32 nf_fail_reg13;                  // 0x21A8
	u32 nf_fail_reg14;                  // 0x21AC
	u32 nf_fail_reg15;                  // 0x21B0
	u32 rsvd_1b4[15];                   // 0x21B4
	u32 nf_pstcmd_reg00;               // 0x21F0
	u32 nf_pstcmd_reg01;               // 0x21F4
	u32 rsvd_1f8[2];                    // 0x21F8
	u32 nf_ncmd_fmt_ptr;                // 0x2200
	u32 nf_ncmd_fmt_reg0;               // 0x2204
	u32 nf_ncmd_fmt_reg1;               // 0x2208
	u32 rsvd_20c[61];                   // 0x220C
	u32 nf_dbg_reg0;                    // 0x2300
	u32 nf_dbg_reg1;                    // 0x2304
	u32 nf_dbg_reg2;                    // 0x2308
	u32 nf_dbg_reg3;                    // 0x230C
	u32 rsvd_310[4];                    // 0x2310
	u32 nf_scmd_reg2;                   // 0x2320
	u32 nf_rdy_err_loc_reg0;            // 0x2324
	u32 nf_pdwn_ctrl_reg1;              // 0x2328
	u32 rsvd_32c[5];                    // 0x232C
	u32 nf_ddr2_tim_reg0;               // 0x2340
	u32 rsvd_344[15];                   // 0x2344
	u32 nf_lpbk_reg0;                   // 0x2380
	u32 nf_lpbk_reg1;                   // 0x2384
	u32 nf_lpbk_reg2;                   // 0x2388
	u32 nf_lpbk_reg3;                   // 0x238C
	u32 nf_lpbk_reg4;                   // 0x2390
	u32 nf_lpbk_reg5;                   // 0x2394
	u32 nf_lpbk_reg6;                   // 0x2398
	u32 nf_lpbk_reg7;                   // 0x239C
	u32 nf_lpbk_reg8;                   // 0x23A0
	u32 nf_lpbk_reg9;                   // 0x23A4
	u32 nf_lpbk_reg10;                  // 0x23A8
	u32 nf_lpbk_reg11;                  // 0x23AC
	u32 nf_lpbk_reg12;                  // 0x23B0
	u32 nf_lpbk_reg13;                  // 0x23B4
	u32 rsvd_3b8[18];                   // 0x23B8
	u32 nf_susp_reg0;                   // 0x2400
	u32 nf_susp_reg1;                  // 0x2404
	u32 rsvd_408[30];                   // 0x2408
	u32 nf_lun_ctrl_reg1;               // 0x2480
	u32 nf_tim_ctrl_reg8;               // 0x2484
	u32 rsvd_488[30];                   // 0x2488
	u32 nf_mp_enh_ctrl_reg0;            // 0x2500
	u32 nf_mp_enh_ctrl_reg1;            // 0x2504
	u32 nf_scmd_type_reg0;              // 0x2508
	u32 rsvd_50c;                       // 0x250C
	u32 nf_wcnt_enh_reg0;               // 0x2510
	u32 nf_wcnt_enh_reg1;               // 0x2514
	u32 nf_wcnt_enh_reg2;               // 0x2518
	u32 nf_wcnt_enh_reg3;               // 0x251C
	u32 nf_wcnt_enh_reg4;               // 0x2520
	u32 nf_wcnt_enh_reg5;               // 0x2524
	u32 nf_wcnt_enh_reg6;               // 0x2528
	u32 nf_wcnt_enh_reg7;               // 0x252C
	u32 nf_pcnt_enh_reg0;               // 0x2530
	u32 nf_pcnt_enh_reg1;               // 0x2534
	u32 rsvd_538[26];                   // 0x2538
	u32 nf_dll_updt_ctrl_reg0;          // 0x25A0
	u32 rsvd_5a4[23];                   // 0x25A4
	u32 nf_fac_ctrl_reg0;               // 0x2600
	u32 nf_fac_ctrl_reg1;               // 0x2604
	u32 rsvd_608[6];                    // 0x2608
	u32 nf_warmup_ctrl_reg0;            // 0x2620
	u32 rsvd_2624[55];					// 0x2624  align 0x600+0x800+0x500+0x700
}ndcu_reg_info_t;  //0x624 = 1572Byte    

typedef struct _raid_reg_info_t {
	u32 raid_cfg_blk_reg;                 // 0xF000
	u32 raid_cfg_sram_reg;                // 0xF004
	u32 raid_ctrs_reg0;                   // 0xF008
	u32 raid_dbg_reg;                     // 0xF00C
	u32 raid_eng_int_fault_reg;           // 0xF010
	u32 raid_cfg_ddr_st_addr_reg;         // 0xF014
	u32 raid_cfg_ddr_end_addr_reg;        // 0xF018
	u32 raid_cfg_ddr_reg;                 // 0xF01C
	u32 raid_enc_xor_fault_mask_reg;      // 0xF020
	u32 raid_enc_xor_fault_reg;           // 0xF024
	u32 raid_dec_xor_fault_mask_reg;      // 0xF028
	u32 raid_dec_xor_fault_reg;           // 0xF02C
	u32 raid_sus_switch_fault_mask_reg;   // 0xF030
	u32 raid_sus_switch_fault_reg;        // 0xF034
	u32 raid_res_switch_fault_mask_reg;   // 0xF038
	u32 raid_res_switch_fault_reg;        // 0xF03C
	u32 raid_id_status_reg;               // 0xF040
	u32 raid_pwr_ctrl_reg;                // 0xF044
	u32 raid_memory_config_reg;           // 0xF048
}raid_reg_info_t;  //0x4C = 76Byte

typedef struct {
	u8 nsid;
	u64 nsz;
	u64 attached_ctrl_bitmap;
	u64 rsvd;				///< add 8 bytes reserved space
} telemetry_ns_attr_t;

struct __attribute__((packed)) nvme_telemetry_host_initated_log {
	//u8 testData[ 32768 ]; //32K
	//   -- Header -- (512)
	u8 lid;
	u8 rsvd1[4];
	u8 IEEE[3];
	u16 last_blk1;
	u16 last_blk2;
	u16 last_blk3;
	u8 rsvd2[368];
	u8 available;
	u8 gen_num;
	//u8 reason_id;
	u32 format_version;
	u32 extened_last_blk1;
	u32 extened_last_blk2;
	u32 extened_last_blk3;
	u8 rsvd3[112];
	//   -- FeatureSetting -- (128)
	struct __attribute__((packed)) telemetry_featureSetting {
		u16 temperatureThreshold_over;		//2
		u16 temperatureThreshold_under;		//2
		u32 hctm;							//4
		u32 volatileWriteCache;				//4
		u8 rsvd[116];
	} featureSetting;
	//   -- DeviceProperty -- (128)
	struct __attribute__((packed)) telemetry_deviceproperty {
		char programmer_ver[6];		//6
		char loader_ver[6];			//6
		u8	fw_ver[8];				//8
		s8 	sn[20];					//20
		s8 	mn[40];					//40
		u32 rtd3e;					//4
		u32 rtd3r;					//4
		u64 eui64;					//8	
		u8  lbaf;					//1
		u8 	rsvd[31];
	}deviceproperty;
	//   -- NamespaceInfo -- (1056)
	struct __attribute__((packed)) telemetry_namespaceinfo {
		telemetry_ns_attr_t ns_attr[32];	// telemetry_ns_attr_t size = 32bytes
		u8 total_order_now;					//1
		u8 host_sec_bits;					//1
		u8 rsvd[30];
	}namespaceinfo;
	//   -- Front End Register -- (1024)
	struct __attribute__((packed)) telemetry_front_end_register {
		nvme_debug_reg_info_t nvme_debug_reg_info;	//744
		u8 rsvd[280];
	} front_end_register;
	//   -- Back End Register -- (8192)
	struct __attribute__((packed)) telemetry_back_end_register {
		eccu_reg_info_t eccu_reg_info;	//1812 Byte  //0x800	512*4
		ficu_reg_info_t ficu_reg_info;	//1144 Byte	//0x500		320*4
		ndcu_reg_info_t ndcu_reg_info;	//1572 Byte	//0x700		448*4
		raid_reg_info_t raid_reg_info;	//76   Byte	//0x100		19*4
		u8 rsvd[2996];
	} back_end_register;
	//   -- Fw Image and Download -- (128)
	struct __attribute__((packed)) telemetry_fw_image_and_download {
		u8 rsvd[128];
	} fw_image_and_download;
	//   -- TCG_related -- (512)
	struct __attribute__((packed)) telemetry_TCG_sts_info {
		// variables
		u32 tcg_en_dis_tag;
		u32 init_pfmt_tag;
		u32 tcg_err_flag;
		u32 bmp_ns_created;

		u32 tcg_status;
		u8  res_cmp_PSID;       // cmp with MSID
		u8  res_cmp_SID;        // cmp with MSID
		u8  rsvd_1[2];

		u32 bmp_nGR_rd_locked;
		u32 bmp_nGR_wr_locked;
		u64 nGR_slba[16];
		u64 nGR_elba[16];

		// on table, ofst 286 bytes
		u8  tcg_act;
		u8  mbr_en;
		u8  mbr_done;
		u8  rsvd_2[13];

		u32 rd_lock_en;         // GR, R1, ..., R16
		u32 rd_locked;
		u32 wr_lock_en;
		u32 wr_locked;

		u32 auth_en;            //            Admin(Adm1-4), Lck(Adm1-4, Usr1-9, rsvd_Usr10-17)
		u8  auth_tries[27];     // SID, PSID, Admin(Adm1-4), Lck(Adm1-4, Usr1-9, rsvd_Usr10-17)

		u8  rsvd[161];
	} TCG_sts_info;
	//   -- ErrorHandle -- (128)
	struct __attribute__((packed)) telemetry_errorhandle {
	    //CPU1
	    u8 tel_epm_Glist_updating;  //1byte
        //CPU2
        u16 tel_ecc_table_cnt;  //2bytes
        u8 tel_read_error_state_ex_cpu2[4]; //4bytes
        u8 tel_vth_cs_flag_cpu2;  //1byte
        u8 tel_hp_retry_flag_cpu2[4];  //4bytes
        u8 tel_err_ncl_cmd_retry_step_cpu2[4];  //4bytes
        u16 tel_total_retry_du_cnt_cpu2;  //2bytes
        //SMART
		u32 tel_program_fail_count;  //4bytes
		u32 tel_erase_fail_count;  //4bytes
		u32 tel_reallocated_sector_count; //4bytes
		u16 tel_host_uecc_detection_cnt;  //2bytes
		u32 tel_uncorrectable_sector_count;  //4bytes
		u32 tel_nand_ecc_detection_count;  //4bytes
		u32 tel_nand_ecc_correction_count;  //4bytes
		u16 tel_hcrc_error_count[3];  //6bytes
		u32 tel_raid_recovery_fail_count;  //4bytes
        //CPU3
        u8 tel_ard_cs_flag; //1byte
        u8 tel_bFail_Blk_Cnt;  //1byte
        //CPU4
        u8 tel_read_error_state_ex_cpu4[4];  //4bytes
        u8 tel_vth_cs_flag_cpu4;  //1byte
        u8 tel_hp_retry_flag_cpu4[4];  //4bytes  
        u8 tel_err_ncl_cmd_retry_step_cpu4[4]; //4bytes
        u16 tel_total_retry_du_cnt_cpu4;  //2bytes
        //above total 71bytes
		u8 rsvd[57];
	} errorhandle;
	struct __attribute__((packed)) telemetry_middle_end_related {
		u8 rsvd[4096];
	} middle_end_related;
	//   -- Reserved -- (4576)
	u8 rsvd4[480];
};

struct __attribute__((packed)) nvme_telemetry_ctrlr_initated_log {
	//u8 testData[ 131072 ];  //128K
	//   -- Header -- (512)
	u8 lid;
	u8 rsvd1[4];
	u8 IEEE[3];
	u16 last_blk1;
	u16 last_blk2;
	u16 last_blk3;
	u8 rsvd2[368];
	u8 available;
	u8 gen_num;
	//u8 reason_id;
	u32 format_version;
	u32 extened_last_blk1;
	u32 extened_last_blk2;
	u32 extened_last_blk3;
	u8 rsvd3[112];
	//   -- Journal -- (4k)
	u8 journal[4096];
	//   -- Runtime Log -- (12k)
	u8 runtime_log[11776];
};
#endif

#ifdef NS_MANAGE //joe add define 20200916
//joe add NS 20200813
//joe 20200730
struct ns_section_id
{ //joe add test 20200520  //joe fix test 20200522
	u16 *sec_id;
	u16 sec_num;
}; //joe add test 20200520//joe fix test 20200522
struct ns_section_id2
{ //joe add test 20200608  //joe fix test 20200608
	u16 *sec_id;
	u16 array_start;
	u16 sec_num;
	u16 array_last;
}; //joe add test 20200520//joe fix test 20200522
//share_data u32 ns_sec_start_address[32];//joe add test//20200605

//joe 20200616  NS_ARRAY_MANAGE_STRUCT
struct ns_array_manage
{
	nvme_ns_attr_t ns_attr[32];//sizeof nvme_ns_attr_t is 232 byte
	u16 free_start_array_point;
	u16 ns_sec_array[1024];
	u8 valid_sec2[1024];
	struct ns_section_id2 ns_array[32];
	u8 array_order[32];
	u8 total_order_now;
	u16 sec_order[1024];
	u16 ns_valid_sector;
	u8 drive_flag;
	u8 host_sec_bits; //joe add 20200901
};

//joe 20200616  NS_ARRAY_MANAGE_STRUCT
//joe 20200730
#endif
/**
 * Namespace attachment Type Encoding
 */

/**
 * NVMe Admin Get Log Page - Command Effects Log (\ref NVME_LOG_COMMAND_EFFECTS_LOG)
 * 2019-12-12 Alan MS Lin
 */
typedef struct nvme_command_effects_log
{
	u32 CSUPP : 1;		 ///< Command Supported (bits[0])
	u32 LBCC : 1;		 ///< Logical Block Content Change (bits[1])
	u32 NCC : 1;		 ///< Namespace Capability Change (bits[2])
	u32 NIC : 1;		 ///< Namespace Inventory Change (bits[3])
	u32 CCC : 1;		 ///< Controller Capability Change (bits[4])
	u32 reserved11 : 11; ///< Reserved (bits[15:5])
	u32 CSE_B16 : 1;	 ///< Command Submission and Execution (bits[16])
	u32 CSE_B17 : 1;	 ///< Command Submission and Execution (bits[17])
	u32 CSE_B18 : 1;	 ///< Command Submission and Execution (bits[18])
	u32 reserved13 : 13; ///< Reserved (bits[31:19])
} nvme_command_effects_log_t;

struct LogPageCommandEffectsEntry_t
{
	nvme_command_effects_log_t ACS[256];  ///< Admin Command Supported
	nvme_command_effects_log_t IOCS[256]; ///< I/O Command Supported
	u32 reserved512[512];
};
// #if CO_SUPPORT_DEVICE_SELF_TEST
typedef struct
{
    // u8  DSTResult:4;     ///< Self-test Result
    // U8  DSTCode:4;       ///< Self-test Code
	//
    // u8  Segment;         ///< Segment Number (bytes[1])
	//
    // u8  Valid;           ///< Valid Diagnostic Information (bytes[2])
	//
    // u8  reserved3;       ///< Reserved  (bytes[3])
    // u32 POH[2];          ///< Power On Hours  (bytes[11:4])
    // u32 NSID;            ///< Namespace Identifier  (bytes[15:12])
    // u32 FLBA[2];         ///< Failing LBA (bytes[23:16])
    // u8  CodeType:3;      ///< Status Code Type (bytes[24] bits[2:0])
    // u8  reserved24:5;    ///< reserved         (bytes[24] bits[7:3])


	u8  DSTResult:4;     ///< Self-test Result
    u8  DSTCode:4;       ///< Self-test Code
	u8  Segment;         ///< Segment Number (bytes[1])
	u8  ValidDiagnosticInfo;           ///< Valid Diagnostic Information (bytes[2])
	u8  reserved3;       ///< Reserved  (bytes[3])
	u32 POH[2];          ///< Power On Hours  (bytes[11:4])
	u32 NSID;            ///< Namespace Identifier  (bytes[15:12])
	u32 FLBA[2];         ///< Failing LBA (bytes[23:16])
	u8  CodeType:3;      ///< Status Code Type (bytes[24] bits[2:0])
    u8  reserved24:5;    ///< reserved         (bytes[24] bits[7:3])
    u8  StatusCode;      ///< Status Code (bytes[25])
    u8  reserved26[2];   ///< Vendor Specific (bytes[27:26])

} tDST_LOG_ENTRY;


/// @brief NVMe Admin Get Log Page - Device Self Test (Log Identifier 06h)
typedef struct
{
    u8         Operation;          ///< Current Device Self-Test Operation
    u8         Completion;         ///< Current Device Self-Test Completion
    u16        reserved2;          ///< reserved
    tDST_LOG_ENTRY  DSTResultData[20];  ///< Newest Self-test Result Data Structure
} LogPageDeviceSelfTestEntry_t;

// #endif

enum nvme_ns_attach_type
{
	/* Controller attach */
	NVME_NS_CTRLR_ATTACH = 0x0,

	/* Controller detach */
	NVME_NS_CTRLR_DETACH = 0x1,

	/* 0x2-0xF - Reserved */
	NVME_NS_CTRLR_RESERVED = 0x2,
};

/**
 * Namespace management Type Encoding
 */
enum nvme_ns_management_type
{
	/* Create */
	NVME_NS_MANAGEMENT_CREATE = 0x0,

	/* Delete */
	NVME_NS_MANAGEMENT_DELETE = 0x1,

	/* 0x2-0xF - Reserved */
	NVME_NS_MANAGEMENT_RESERVED = 0x2,
};

struct nvme_ns_list
{
	u32 ns_list[1024];
};

#define NVME_NIDT_EUI64_LEN 8
#define NVME_NIDT_NGUID_LEN 16
#define NVME_NIDT_UUID_LEN 16

enum
{
	NVME_NIDT_EUI64 = 0x01,
	NVME_NIDT_NGUID = 0x02,
	NVME_NIDT_UUID = 0x03,
};

struct nvme_ns_id_desc
{
	u8 nidt;
	u8 nidl;
	u16 reserved;
};

struct nvme_ctrlr_list
{
	u16 ctrlr_count;
	u16 ctrlr_list[2047];
};

struct nvme_ng_des
{			   // namespace granularity descriptors.
	u64 nszeg; // namespace size granularity, in bytes.
	u64 ncapg; // namespace capacity granularity, in bytes.
};

struct nvme_ng_list
{								   // total 288 bytes
	u32 nsga;					   // ns granularity attributes;
	u8 nod;						   // number of descriptors;
	u8 rsv1[27];				   // byte 31:05
	struct nvme_ng_des ng_des[16]; // namespace granularity descriptors.
};

enum nvme_secure_erase_setting
{
	NVME_FMT_NVM_SES_NO_SECURE_ERASE = 0x0,
	NVME_FMT_NVM_SES_USER_DATA_ERASE = 0x1,
	NVME_FMT_NVM_SES_CRYPTO_ERASE = 0x2,
};

enum nvme_pi_location
{
	NVME_FMT_NVM_PROTECTION_AT_TAIL = 0x0,
	NVME_FMT_NVM_PROTECTION_AT_HEAD = 0x1,
};

enum nvme_pi_type
{
	NVME_FMT_NVM_PROTECTION_DISABLE = 0x0,
	NVME_FMT_NVM_PROTECTION_TYPE1 = 0x1,
	NVME_FMT_NVM_PROTECTION_TYPE2 = 0x2,
	NVME_FMT_NVM_PROTECTION_TYPE3 = 0x3,
};

enum nvme_metadata_setting
{
	NVME_FMT_NVM_METADATA_TRANSFER_AS_BUFFER = 0x0,
	NVME_FMT_NVM_METADATA_TRANSFER_AS_LBA = 0x1,
};

union nvme_format
{
	struct
	{
		u32 lbaf : 4; ///< lba format
		u32 mset : 1; ///< metadata settings, enum nvme_metadata_setting
		u32 pi : 3;	  ///< protection information, enum nvme_pi_type
		u32 pil : 1;  ///< protection information location, enum nvme_pi_location
		u32 ses : 3;  ///< secure erase settings, enum nvme_secure_erase_setting
		u32 reserved : 20;
	} b;
	u32 all;
};

struct nvme_protection_info
{
	u16 guard;
	u16 app_tag;
	u32 ref_tag;
};

/** Parameters for NVME_OPC_FIRMWARE_COMMIT cdw10: commit action */
enum nvme_fw_commit_action
{
	/**
	 * Downloaded image replaces the image specified by
	 * the Firmware Slot field. This image is not activated.
	 */
	NVME_FW_COMMIT_REPLACE_IMG = 0x0,
	/**
	 * Downloaded image replaces the image specified by
	 * the Firmware Slot field. This image is activated at the next reset.
	 */
	NVME_FW_COMMIT_REPLACE_AND_ENABLE_IMG = 0x1,
	/**
	 * The image specified by the Firmware Slot field is
	 * activated at the next reset.
	 */
	NVME_FW_COMMIT_ENABLE_IMG = 0x2,
	/**
	 * The image specified by the Firmware Slot field is
	 * requested to be activated immediately without reset.
	 */
	NVME_FW_COMMIT_RUN_IMG = 0x3,
};

/** Parameters for NVME_OPC_FIRMWARE_COMMIT cdw10 */
struct nvme_fw_commit
{
	/**
	 * Firmware Slot. Specifies the firmware slot that shall be used for the
	 * Commit Action. The controller shall choose the firmware slot (slot 1 - 7)
	 * to use for the operation if the value specified is 0h.
	 */
	u32 fs : 3;
	/**
	 * Commit Action. Specifies the action that is taken on the image downloaded
	 * with the Firmware Image Download command or on a previously downloaded and
	 * placed image.
	 */
	u32 ca : 3;
	u32 reserved : 26;
};

/** HMB, host memory buffer descriptor entry */
struct nvme_host_memory_desc_entry
{
	u64 badd;  /** buffer address */
	u32 bsize; /** buffer size */
	u32 rsvd;
};

struct nvme_host_memory_attributes
{
	u32 hsize;
	u32 hmdlal;
	u32 hmdlau;
	u32 hmdlec;

	/** total 4096 bytes */
};

enum nvme_asynchronous_event_type
{
	NVME_EVENT_TYPE_ERROR_STATUS = 0,
	NVME_EVENT_TYPE_SMART_HEALTH = 1,
	NVME_EVENT_TYPE_NOTICE = 2,
	NVME_EVENT_TYPE_IO_COMMAND_SPECIFIC_STATUS = 6,
	NVME_EVENT_TYPE_VENDOR_SPECIFIC = 7,
	NUMBER_OF_NVME_EVENT_TYPE,
};

enum nvme_event_error_status_type
{
	ERR_STS_INVALID_SQ = 0,
	ERR_STS_INVALID_DB_WR = 1,
	ERR_STS_DIAG_FAIL = 2,
	ERR_STS_PERSISTENT_INTERNAL_DEV_ERR = 3,
	ERR_STS_TRANSIENT_INTERNAL_DEV_ERR = 4,
	ERR_STS_FW_IMG_LOAD_ERR = 5,
};

enum nvme_event_smart_health_status_type
{
	SMART_STS_RELIABILITY = 0,
	SMART_STS_TEMP_THRESH = 1,
	SMART_STS_SPARE_THRESH = 2
};

enum nvme_event_smart_critical_en_type
{
	SMART_CRITICAL_BELOW_AVA_SPARE = 0,
	SMART_CRITICAL_TEMPR_EXCEED_THR,
	SMART_CRITICAL_INTERNAL_ERR,
	SMART_CRITICAL_READ_ONLY,
	SMART_CRITICAL_BACKUP_FAILED,
	SMART_CRITICAL_PMR_RD_ONLY
};

#define BELOW_AVA_SPARE_MASK 0x01
#define TEMPR_EXCEED_THR_MASK 0x02
#define INTERNAL_ERR_MASK 0x04
#define READ_ONLY_MASK 0x08
#define BACKUP_FAILED_MASK 0x10
#define PMR_RD_ONLY_MASK 0x20

enum nvme_event_notice_status_type
{
	NOTICE_STS_NAMESPACE_ATTRIBUTE_CHANGED = 0,
	NOTICE_STS_FIRMWARE_ACTIVATION_STARTING = 1,
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	NOTICE_STS_TELEMETRY_LOG_CHANGED = 2,
#endif
	NOTICE_STS_PROGRAM_FAIL = 7,
	NOTICE_STS_ERASE_FAIL = 8,
	NOTICE_STS_GC_READ_FAIL
};

enum nvme_event_internal_error_status_type
{
	AXI_PARITY_ERRS = 0,
	SRAM_PARITY_ERRS,
	DRAM_ERR,
};

#define INVALID_SQ_ERR 0xFFFF
#define INVALID_DB_WR INVALID_SQ_ERR - 1
#define INTERNAL_ERR INVALID_SQ_ERR - 3

typedef union{
	u32 all;
	struct{
		u32 lid :8;
		u32 lsp :4;
		u32 res :3;
		u32 RAE :1;
		u32 NUMDL:16;
	}b;
}Get_Log_CDW10;

#define nvme_cpl_is_error(cpl) \
	((cpl)->status.sc != 0 || (cpl)->status.sct != 0)

/** Enable protection information checking of the Logical Block Reference Tag field */
#define NVME_IO_FLAGS_PRCHK_REFTAG (1U << 26)
/** Enable protection information checking of the Application Tag field */
#define NVME_IO_FLAGS_PRCHK_APPTAG (1U << 27)
/** Enable protection information checking of the Guard field */
#define NVME_IO_FLAGS_PRCHK_GUARD (1U << 28)
/** The protection information is stripped or inserted when set this bit */
#define NVME_IO_FLAGS_PRACT (1U << 29)
#define NVME_IO_FLAGS_FORCE_UNIT_ACCESS (1U << 30)
#define NVME_IO_FLAGS_LIMITED_RETRY (1U << 31)

#define SMART_STAT_VER 1		  ///< version of smart
#define SMART_STAT_SIG 0x53737461 ///< signature of smart "Ssta"

#if CO_SUPPORT_SANITIZE
#if defined(USE_CRYPTO_HW)
#define SANICAP_CES 1
#define EST_TIMECOST_SANITIZE_CRY_ERASE 0x3
#else
#define SANICAP_CES 0
#endif
#define SANICAP_BES 1
#define EST_TIMECOST_SANITIZE_BLK_ERASE 0x6

#define SANICAP_OWS 0
#define SANICAP_NDI 1
#define SANICAP_NODMMAS 2 //shall not be 0 -> allow return 0 for nvme ver.1.3 or earlier only

enum nvme_event_io_command_status_type
{
	NOTICE_STS_RESERVATION_LOG_PAGE_AVAILABLE          = 0,
	NOTICE_STS_SANITIZE_OPERATION_COMPLETED            = 1,
	NOTICE_STS_SANITIZE_OPERATION_COMPLETED_WITH_UN_DA = 2,
};

typedef enum
{
    cExit_Failure_Mode   = 1,
    cStart_Block_Erase   = 2,
    cStart_Overwrite     = 3,
    cStart_Crypto_Erase  = 4,
}sanitize_action_code_t;

typedef union
{
    u32  all;
    struct
    {
        u32 SANACT    :3;     ///< Sanitize Action (DW10 bits[02:00])
        u32 AUSE      :1;     ///< Allow Unrestricted Sanitize Exit  (DW10 bits[03])
        u32 OWPASS    :4;     ///< Overwrite Pass Count  (DW10 bits[07:04])
        u32 OIPBP     :1;     ///< Overwrite Invert Pattern Between Passes  (DW10 bits[08])
        u32 NDAS      :1;     ///< No Deallocate After Sanitize  (DW10 bits[09])
        u32 rsvd      :22;    ///< reserved (DW10 bits[31:10])
    }b;
} sanitize_dw10_t;

typedef enum
{
    sSanitizeNeverIssue         = 0,  // Never been sabitized
    sSanitizeCompleted          = 1,  // Operation completed without error
    sSanitizeInProgress         = 2,  // Operation in progress
    sSanitizeCompletedFail      = 3,  // Operation completed with failed
    sSanitizeCompletedNDI       = 4,  // Operation completed with No-Deallocate request
} sanitize_status_t;

typedef enum
{
    sSanitize_FW_None             = 0,  // no sanitize request
    sSanitize_FW_Start            = 1,  // Shall start process sanitize
    sSanitize_FW_Completed        = 2,  // Operation completed
    sSanitize_FW_InProgress       = 3   // Operation in progress
} sanitize_FW_process_status_t;

typedef union
{
	u64  all;
	struct
	{
    	u16               SPROG;
    	u16               SanitizeStatus      :3;  //Bits 2:0  contains the status of the most recent sanitize operation as shown below
    	u16               OverwritePassNunber :5;  //Bits 7:3  contains the number of completed passes
    	u16               GlobalDataErased    :1;  //Bits 8    Global Data Erased
    	u16               Reserved            :7;  //Bits 15:9 reserved.
    	sanitize_dw10_t   DW10;
	}b;
} sanitize_log_page_t;

struct LogPageSanitizeStatus_t
{
	u16 SPROG;
    u16 SanitizeStatus     :3;  //Bits 2:0  contains the status of the most recent sanitize operation as shown below
    u16 OverwritePassNunber:5;  //Bits 7:3  contains the number of completed passes
    u16 GlobalDataErased   :1;  //Bits 8    Global Data Erased
    u16 rsvd1              :7;  //Bits 15:9 reserved.
    u32 SCDW10;
	u32 EstTimeOverwrite;
	u32 EstTimeBlkErase;
	u32 EstTimeCyptErase;
	u32 EstTimeOverwriteNDMM;
	u32 EstTimeBlkEraseNDMM;
	u32 EstTimeCyptEraseNDMM;
	u8  rsvd2[480];
};

typedef struct
{
    u32                    Tag;
    sanitize_log_page_t    sanitize_log_page;
	volatile u32           fwSanitizeProcessStates;
} sanitize_info_backup_t;

#endif

