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

#ifndef _NCL_NEW_H_
#define _NCL_NEW_H_
#include "nand_cfg.h"
#include "ficu.h"
#include "mpc.h"

#define SPOR_RETRY 1
#define OPEN_BLK_RETRY 1
#define History_read //Test for history read(James 2023/06/29)
#define NCL_FW_RETRY    1
#define DFU_MARK_ECC    1
#if !NCL_FW_RETRY
#define NCL_HAVE_reARD
#else
#define NCL_FW_RETRY_EX
#define RETRY_COMMIT_EVENT_TRIGGER
//#define PREVENT_PDA_MISMATCH
#define NCL_FW_RETRY_BY_SUBMIT
#endif
#define RD_FAIL_GET_LDA
//#define DBG_NCL_SET_FEA_BE4_READ  //for DEBUG: Host read pass by default read retry level 
#ifdef DBG_NCL_SET_FEA_BE4_READ
#define SETFEA_NCL_CMD_MAX_CNT 8
#endif
#define MAX_DLL_COUNT	64  //Sean_add_for_new_dll_221005
#define OPEN_BLK_RR_VALUE_IDX 17

enum meta_base_type {
	META_DTAG_SRAM_BASE = 0,
	META_DTAG_DDR_BASE,
	META_IDX_SRAM_BASE,
	META_IDX_DDR_BASE,
	META_DTAG_HMB_BASE,
	META_IDX_HBM_BASE,
};

/*!  Operation Table for NCL command */
enum ncl_cmd_op_t {
	NCL_CMD_OP_READ = 0,
    NCL_CMD_OP_READ_RAW,  //Sean_add_for_data_training
    NCL_CMD_OP_READ_STREAMING_FAST,
	NCL_CMD_OP_READ_FW_ARD,
	NCL_CMD_READ_RAW,
	NCL_CMD_READ_REFRESH,
    NCL_CMD_PATROL_READ,

	NCL_CMD_OP_CB_MODE0,
	NCL_CMD_OP_CB_MODE1,
	NCL_CMD_OP_CB_MODE2,
	NCL_CMD_READ_DEFECT,
	NCL_CMD_SET_GET_FEATURE,
	NCL_CMD_OP_READ_ARD,
	NCL_CMD_OP_READ_ERD,
	NCL_CMD_OP_POLLING_STATUS,
	NCL_CMD_P2L_SCAN_PG_AUX, //15
    NCL_CMD_OP_WRITE,
	NCL_CMD_OP_ERASE,
#if (SPOR_FLOW == mENABLE)
    NCL_CMD_SPOR_SCAN_FIRST_PG,  // 18
    NCL_CMD_SPOR_SCAN_LAST_PG,   // 19
    NCL_CMD_SPOR_SCAN_PG_AUX,    // 20
    NCL_CMD_SPOR_P2L_READ,       // 21
    NCL_CMD_SPOR_P2L_READ_POS,   // 22
    NCL_CMD_SPOR_BLIST_READ,
    NCL_CMD_SPOR_SCAN_BLANK_POS,
#endif
#ifdef NCL_FW_RETRY_BY_SUBMIT
    NCL_CMD_FW_RETRY_SET_FEATURE,  //24
    NCL_CMD_FW_RETRY_READ,         //25
#endif
    NCL_CMD_OP_SSR_READ,          //26
};

enum{ //vth_tracking_step
    SF_tracking_ini = 0,    //0
    SF_tracking_left,       //1
    SF_tracking_right,      //2
    SF_tracking_flag,       //3
    CaCu_tracking,          //4
};

enum{ //read_level for vth tracking
    SSR_AR = 0, //0 
    SSR_BR,     //1
    SSR_CR,     //2
    SSR_DR,     //3
    SSR_ER,     //4
    SSR_FR,     //5
    SSR_GR,     //6
    SSR_END,    //7
};

/*! \brief NAND PB type */
enum nal_pb_type {
	NAL_PB_TYPE_SLC = 0,
	NAL_PB_TYPE_XLC,
	NAL_PB_TYPE_UNKNOWN,
};

/*! \brief read mode interface */
enum {
	/* ref: rainier group mail */
	/* 0xxx NVM read data */
	/* 1xxx internal read data */
	HOST_READ                 = 0b0000, ///< Host data read
	//						  = 0b0001, ///< Reserved
	HOST_READ_DYN_HW_RECY     = 0b0010, ///< Auto release host read and HW recycle the DTAG (streaming mode)
	HOST_READ_DYN_FW_RECY     = 0b0011, ///< Auto release host read and report the RD_ENTRY to FW. (streaming mode)
	FDMA_FAST_READ_MODE	  	  = 0b0100, ///< fdma fast read mode
	//						  = 0b0101, ///< Reserved
	//						  = 0b0110, ///< Reserved
	//						  = 0b0111, ///< Reserved
	INT_DATA_GC_READ_DYN      = 0b1000, ///< dynamic assign tag internal GC read
	INT_DATA_PREREAD_DYN      = 0b1001, ///< dynamic assign tag internal pre-read
	INT_TABLE_READ_DYN        = 0b1010, ///< dynamic assign tag for table
	INT_DATA_READ_PRE_ASSIGN  = 0b1011, ///< preassign assign tag for internal data
	INT_TABLE_READ_PRE_ASSIGN = 0b1100, ///< preassign assign tag for table
	INT_READ_HW_RECY_HM       = 0b1101, ///< internal read operation with auto-recycle DTAG, and the data will be directly written to HMB with HMB_OFST prepared by FW
	//						  = 0b1110, ///< Reserved
	//						  = 0b1111, ///< Reserved
};

/*! \brief write mode interface */
enum {
	/* ref: rainier group mail */
	/* 0xxx NVM write data */
	/* 1xxx internal write data */
	HOST_WRITE                = 0b0000, ///< Host write read
	HOST_WRITE_DATA_HMB       = 0b0001, ///< write data from HMB to NAND
	//						  = 0b0010, ///< Reserved
	//						  = 0b0011, ///< Reserved
	//						  = 0b0100, ///< Reserved
	//						  = 0b0101, ///< Reserved
	//						  = 0b0110, ///< Reserved
	//						  = 0b0111, ///< Reserved
	//						  = 0b1000, ///< Reserved
	//						  = 0b1001, ///< Reserved
	INT_DATA_WRITE_PRE_ASSIGN  = 0b1010, ///< dynamic assign tag for table
	INT_TABLE_WRITE_PRE_ASSIGN = 0b1011, ///< preassign assign tag for internal data
	//						  = 0b1100, ///< Reserved
	//						  = 0b1101, ///< Reserved
	//						  = 0b1110, ///< Reserved
	//						  = 0b1111, ///< Reserved
};

#define DTAG_QID_DROP	(BIT3)
#define META_SRAM_DTAG	(0)
#define META_SRAM_IDX	(BIT0)
#define META_DDR_DTAG	(BIT1)
#define META_DDR_IDX	(BIT1 | BIT0)
#define META_DDR_MASK	(BIT1 | BIT0)

#define DATA_SRAM_DTAG	0
#define DATA_DDR_DTAG	1

/*! use ncl_cmd->completion NULL or not as sync/async indicator */
#define NCL_CMD_SYNC_FLAG		BIT0
/*! All PDAs are SLC mode, used by erase/program cmd. */
#define NCL_CMD_SLC_PB_TYPE_FLAG	BIT1
/*! All PDAs are XLC mode, used by erase/program cmd */
#define NCL_CMD_XLC_PB_TYPE_FLAG	BIT2
/*! Cache program flag for write, WIP */
#define NCL_CMD_CACHE_PROGRAM_FLAG	BIT3
/*! command completed flag */
#define NCL_CMD_COMPLETED_FLAG			BIT4
/*! Used during validation */
#define NCL_CMD_SCH_FLAG			BIT5
/*! This command is used for read flag check, it works snap read now */
#if defined(USE_MU_NAND)
#define NCL_CMD_FLAGCHECK_FLAG		BIT6
#endif
#if (Synology_case)
#define NCL_CMD_GC_PROG_FLAG 		BIT6
#endif	
/*! to distinguish if normal path or rapid path */
#define NCL_CMD_RAPID_PATH			BIT7
#define NCL_CMD_META_DISCARD		BIT8
#if ONFI_DCC_TRAINING
#define NCL_CMD_RD_DQ_TRAINING		BIT9  //Sean_0419
#else
/*! to ignore NCl block check and exec */
#define NCL_CMD_UNBLK_FLAG		BIT9
#endif
/*! to indicate ncl have done raid correct */
#define NCL_CMD_RCED_FLAG		BIT10
/*! Get feature flag (otherwise, set feature) */
#define NCL_CMD_FEATURE_GET_FLAG	BIT11
/*! Set/get feature by LUN flag (otherwise, set die) */
#define NCL_CMD_FEATURE_LUN_FLAG	BIT12
#if ONFI_DCC_TRAINING
#define NCL_CMD_FEATURE_DCC_TRAINING BIT13  //Sean_221229
#else
/*! Set/get parameter of Hynix flag */
#define NCL_CMD_HYNIX_PARAM_FLAG	BIT13
#endif
#define NCL_CMD_IGNORE_ERR_FLAG		BIT14
/*! Set ncl cmd as host & internal (P2L) data mix */
#define NCL_CMD_HOST_INTERNAL_MIX	BIT15

/*! Flags set for raid operation */
/*! NCL do raid path operation */
#define NCL_CMD_RAID_FLAG		BIT16
/*! NCL do xor to stripe with transfer */
#define NCL_CMD_XOR_FLAG		BIT17
/*! NCL do parity output after xor */
#define NCL_CMD_POUT_FLAG		BIT18
/*! NCL do parity output after xor */
#define NCL_CMD_SUSPEND_FLAG		BIT19
/*! NCL do parity output after xor */
#define NCL_CMD_RESUME_FLAG		BIT20
/*! NCL do xor to stripe without data transfer */
#define NCL_CMD_XOR_ONLY_FLAG	BIT21
#if ONFI_DCC_TRAINING
#define NCL_CMD_WR_DQ_TRAINING		BIT22
#else
/*! NCL do parity out to stripe without data transfer */
#define NCL_CMD_POUT_ONLY_FLAG		BIT22  //temp_mark_for_dq_training_flag_0420
#endif
/*! NCL high priority flag, only for read */
#define NCL_CMD_HIGH_PRIOR_FLAG		BIT23
#define NCL_CMD_DIS_ARD_FLAG		BIT24 //_GENE_20201015

#if defined(HMETA_SIZE)
#define NCL_CMD_DIS_HCRC_FLAG       BIT25 // Jamie 20210311
#define NCL_CMD_RETRY_CB_FLAG       BIT26 // Jamie 20210319
#endif

/*! p2l read flag */
#define NCL_CMD_P2L_READ_FLAG       BIT27
/*! l2p read flag */
#define NCL_CMD_L2P_READ_FLAG       BIT28
/*! NCL read do not retry flag */
#define NCL_CMD_NO_READ_RETRY_FLAG	BIT29

/*!
 * @brief
 * Set this flag if all user data bm_payloads are filled and valid
 * It uses for one level dtag fetching with dynamic assign dtag.
 * May not set ext flag if you fill first ctag entry only for command and latency.
 */
#define NCL_CMD_TAG_EXT_FLAG		BIT30

/*! Internal flag to indicate cache read involved */
#define NCL_CMD_CACHE_READ_FLAG		BIT31

/*! NCL do data xor and program to nand */
#define NCL_CMD_RAID_XOR_FLAG_SET		(NCL_CMD_RAID_FLAG | NCL_CMD_XOR_FLAG)
/*! NCL do data read and xor without data xfer */
#define NCL_CMD_RAID_XOR_ONLY_FLAG_SET	(NCL_CMD_RAID_FLAG | NCL_CMD_XOR_ONLY_FLAG)
/*! NCL do data parity out and pgrram to nand */
#define NCL_CMD_RAID_POUT_FLAG_SET		(NCL_CMD_RAID_FLAG | NCL_CMD_POUT_FLAG)
#if !ONFI_DCC_TRAINING
/*! NCL do data parity out and pgrram to nand */
#define NCL_CMD_RAID_POUT_ONLY_FLAG_SET	(NCL_CMD_RAID_FLAG | NCL_CMD_POUT_ONLY_FLAG) //temp_mark_for_dq_training_flag_0420
#endif
/*! NCL swap SRAM raid buffer to DDR raid buffer */
#define NCL_CMD_RAID_SUSPEND_FLAG_SET	(NCL_CMD_RAID_FLAG | NCL_CMD_SUSPEND_FLAG)
/*! NCL swap DDR raid buffer to SRAM raid buffer */
#define NCL_CMD_RAID_RESUME_FLAG_SET		(NCL_CMD_RAID_FLAG | NCL_CMD_RESUME_FLAG)


/*! NCL command enters pending state */
#define NCL_CMD_PENDING_STATUS		BIT0
/*! There is NCL command error */
#define NCL_CMD_ERROR_STATUS		BIT1
/*! The NCL command is aborted by FW */
#define NCL_CMD_FW_ABORT_STATUS		BIT2
/*! The NCL command is aborted by NCB module */
#define NCL_CMD_HW_ABORT_STATUS		BIT3
#define NCL_CMD_TIMEOUT_STATUS		BIT4
#define NCL_CMD_PWR_DROP_STATUS		BIT5
#define NCL_CMD_ARD_STATUS			BIT6

#define DYNAMIC_DTAG			BIT4
#define NORELOAD_CMF     //Reload cmf will possibly occur fail , temp turn off reload _GENE_20200928
/*! NCL operation type for commands */
enum ncl_op_type_t {
	/*! Host data read with dynamnic-assigned DTAG */
	NCL_CMD_HOST_READ_DA_DTAG	= HOST_READ | DYNAMIC_DTAG,
	/*! Host data write with wdtag_HMB_Llist DTAG */
	NCL_CMD_HOST_WRITE_HMB_DTAG   = HOST_WRITE_DATA_HMB,
	/*! Internal Table read with pre-assigned DTAG */
	NCL_CMD_FW_TABLE_READ_PA_DTAG	= INT_TABLE_READ_PRE_ASSIGN,
	/*! Internal Table read with dynamic-assigned DTAG */
	NCL_CMD_FW_TABLE_READ_DA_DTAG	= INT_TABLE_READ_DYN | DYNAMIC_DTAG,
	/*! Internal Data read with pre-assigned DTAG */
	NCL_CMD_FW_DATA_READ_PA_DTAG	= INT_DATA_READ_PRE_ASSIGN,
	/*! Internal Data read with dynamic-assigned DTAG */
	NCL_CMD_FW_DATA_GC_READ_DA_DTAG	= INT_DATA_GC_READ_DYN | DYNAMIC_DTAG,
	/*! internal Data pre-read with dynamic-assigned DTAG */
	NCL_CMD_FW_DATA_PREREAD_DA_DTAG	= INT_DATA_PREREAD_DYN | DYNAMIC_DTAG,
	/*! Streaming mode for read */
	NCL_CMD_FW_DATA_READ_STREAMING	= HOST_READ_DYN_HW_RECY | DYNAMIC_DTAG,
	/*! Host Data Program */
	NCL_CMD_PROGRAM_HOST	= 0x0,
	/*! Internal Table write with pre-assigned DTAG */
	NCL_CMD_PROGRAM_TABLE	= INT_TABLE_WRITE_PRE_ASSIGN,
	/*! Internal Data write with pre-assigned DTAG */
	NCL_CMD_PROGRAM_DATA	= INT_DATA_WRITE_PRE_ASSIGN,
	/*! FDMA fast read mode */
	NCL_CMD_FDMA_FAST_READ_MODE	= FDMA_FAST_READ_MODE,
	/*! Host data read with rdtag_HMB_Llist DTAG */
	NCL_CMD_HOST_READ_HMB_DTAG	= INT_READ_HW_RECY_HM | DYNAMIC_DTAG,
};

typedef u8 nal_pb_type_t;
typedef u8 nal_status_t;	///< Set by enum ficu_du_status
typedef u16 du_dtag_ptr_t;
typedef CBF(u8, 16) ard_que_t;  //FW ARD queue template
struct raw_column_list {
	u16 column;				///< Column address for RAW format
};

enum xlc_prog_step {
	PROG_1ST_STEP = 0,
	PROG_2ND_STEP,
};

struct info_param_t {
	OUT nal_status_t	status;		///< NCL will update status Buffer when error happens
	struct {
		IN u8		slc_idx:2;	///< XLC: 0,1,2,3 for program scrambler seed index
		IN u8		cb_step:2;	///< XLC: copyback or QLC program steps
		u8		reserved:4;
	} xlc;

	IN u16 pb_type : 2;
	IN u16 raid_id : 6;
	IN u16 bank_id : 3;
	IN u16 op_type : 5;	///< Per Yi Chen, for P2L reason, host write & internal write may co-exist in 1 cmd
};

/*!
 * @brief for common ncl command, DU basis for read, page basis for erase/program
 */
struct common_param_t {
	IN u32			list_len;	///< PDA List Count
	IN pda_t		*pda_list;	///< PDA List Array
	IN struct info_param_t	*info_list;	///< Info list
};

/*!
 * @brief for MP rapid ncl command, page basis for read/erase/program
 */
struct rapid_param_t {
	IN u32			list_len;	///< PDA count
	IN pda_t		*pda_list;	///< Page Basis
	IN struct info_param_t	*info_list;	///< Info list
};

/*!
 * @brief for single DU rapid ncl command, DU basis for read only
 */
struct rapid_du_param_t {
	IN u32			list_len;	///< Must set to 1
	IN pda_t		pda;		///< DU Basis, PDA address for one DU read
	IN struct info_param_t	info;		///< Point to single info
};

typedef union _Fda_t {
        struct {
                u64 ch : 8;
                u64 ce : 8;
                u64 du : 8;
                u64 tsb_prefix : 8;
                           //   Low -------------- high
                row_t row; // | PageInBlock | PlaneInBlock | BlockInPlane | Lun |
        } b;
        u64 all;
} Fda_t;

/*!
 * @brief for RAW ncl command, DU basis for read, program is TBD
 */
struct rw_raw_param_t {
	IN u32			list_len;	///< PDA List Count, single DU support only
	IN pda_t		*pda_list;	///< PDA List, single PDA support
	IN struct info_param_t	*info_list;	///< Info list
	IN struct raw_column_list *column;	///< Column address list for RAW format
};

/*!
 * @brief XLC CB address setting
 */
struct xlc_cb_addr_t {
	IN pda_t		*slc_list_src;	///< Page Basis
	IN pda_t		*tlc_list_dst;	///< Page Basis
	IN u16			data_tag_count;	///< allocated data tag count which used for NCL_CMD_OP_CB_MODE2
	IN u16			width; 		///< interleave width
};

/*!
 * @brief for copyback ncl command
 */
struct xlc_cb_param_t {
	IN u32			list_len;	///< Page List Count
	IN struct xlc_cb_addr_t	*addr;		///< Address
	IN struct info_param_t	*info_list;	///< Info list
};

/*!
 * @brief for fda command
 */
struct fda_param_t {
	IN u32			list_len;	///< PDA count
	IN Fda_t		*fda_list;	///< FDA List Array
	IN struct info_param_t	*info_list;	///< Info list
	IN u32 ch;
	IN u32 ce;
	IN u32 row;
};

/*!
 * @brief share a buffer for 5 addr param
 */
union ncl_addr_param_t {
	/*!
	 * The unit basis is block for erase;
	 * The unit basis is page for program, MP operation
	 * The unit basis is DU for normal read
	 */
	struct common_param_t	common_param;

	/*!
	 * The unit basis is page for MP operation.
	 *	The function "ncl_cmd_rapid_mp_write/read will touch it.
	 */
	struct rapid_param_t	rapid_param;

	/*!
	 * The unit basis is DU for 4K rapid read.
	 *	The function "ncl_cmd_rapid_4k_read will touch it.
	 */
	struct rapid_du_param_t	rapid_du_param;

	/*! For Read/Write RAW, READ RAW only supports one DU read */
	struct rw_raw_param_t	rw_raw_param;

	/*! For TLC copyback */
	struct xlc_cb_param_t	cb_param;

	/*! For FDA cmd use */
	struct fda_param_t	fda_param;
};

/*! \brief meta format */
struct du_meta_fmt {

#ifdef LJ_Meta
	union {
		struct{

			u32 lda;

			union
			{
				u32 fmt;
				struct
				{
					u16 rev1;
					u8  rev2;
					u8  WUNC;
				}wunc;

				struct
				{
					u32 page_sn_L : 24;
					u32 WUNC : 8;
				} fmt1;

				struct
				{
					u32 page_sn_H : 24;
					u32 WUNC : 8;
				} fmt2;

				struct
				{
					u32 blk_sn_L : 16;
					u32 blk_type : 8;
					u32 WUNC : 8;
				} fmt3;

				struct
				{
					u32 blk_sn_H : 16;
					u32 Debug : 8;
					u32 WUNC : 8;
				} fmt4;

			};
			u64 hlba;
		};
		u32 meta[META_SIZE/sizeof(u32)];
	};
#else
	union {
		struct{
			union {
				u32 seed_index;
				struct {
					u16 seed;
					u16 pdu_bitmap;	///< partial DU error bitmap, each sector has one bit, if set, report ficu_err_par_err when read
				};
			};
			u32 lda;
			u32 sn;
			u32 hlba;
		};
		u32 meta[META_SIZE/sizeof(u32)];
	};
#endif
};

BUILD_BUG_ON(META_SIZE != sizeof(struct du_meta_fmt));

/*!
 * The upper will fill ncl_cmd_t to issue NCL command,
 */
struct ncl_cmd_t {
	QSIMPLEQ_ENTRY(ncl_cmd_t) entry;	///< Link list
#if 0
	u8 list_len;
	union {
		struct {
			pda_t	*pda_list;
			pda_info_t *info_list;
		};
	};
#else
union ncl_addr_param_t addr_param;

#endif
	union {
		bm_pl_t *user_tag_list;		///< user data buffer
		/*! bind to rapid_du_param_t */
		bm_pl_t du_user_tag_list;	///< DU payload for rapid DU read
		u32 sf_val;// Set feature value
	};

	/*!
	 * call back function when completion for async mode
	 */
	void (*completion)(struct ncl_cmd_t *);

	/*!
	 * Don't clean pending status until IO done.
	 */
	vu16 status;				///< NCL_CMD_XXX_STATUS
	#if OPEN_ROW == 1
	u8 via_sch;
	#endif
    u8 dis_hcrc;
	u32 flags;				///< NCL_CMD_XXX_FLAG
#if HAVE_CACHE_READ
	u32 lun_id;				///< Used for cache read LUN indication
#endif
	u8 du_format_no;			///< DU format
	u8 op_code;				///< use ncl_cmd_op_t
	u8 op_type;				///< Set ncl_op_type_t for FINST op type
	u8 die_id;
	du_dtag_ptr_t dtag_ptr;

#if NCL_HAVE_ERD
    du_dtag_ptr_t erd_id;			///< ERD ID also used as du_dtag_ptr
	u32 erd_step;
	u16 erd_cnt;				// Sub ERD ncl command count
	u16 erd_ch_bmp;				// CH bitmap this is waiting because of ERD full
#endif

union{
	u16 ard_rd_cnt;
    u16 raw_1bit_cnt;   //DU 1 bit count.(Provided from the Reg.)
};

    void *caller_priv;			///< record upper layer context
#if HAVE_CACHE_READ
	u32 task_cnt;
#endif
#if DEBUG
	void *meta;				///< meta data buffer
	u32 sq_grp;
	u32 start_time;
	u32 end_time;
#endif

#ifdef NCL_HAVE_reARD
    bool re_ard_flag;
#endif

#if NCL_FW_RETRY

union{
    u8 retry_step;
    struct{
	    u8 read_level:4;        //For A~G read level.
	    u8 tracking_step:3;     //For init,right,left and flag.
	    u8 flag_vtt:1;          //Flag for valley search direction. 
    };
};

union{
    u8 retry_cnt;
    u8 shift_value; //Initial read level
};

u8 err_du_cnt;
#endif

#if RAID_SUPPORT_UECC
u8 uecc_type;  //for rd errhandle
#endif

#ifdef NCL_FW_RETRY
bool rty_blank_flg; 
bool rty_dfu_flg; 
#endif
};

//////////////////////////////////////////////////////////////////////////////////////////
// Basic geometry shared to FTL
struct target_geo_t {
	u8 nr_channels;
	u8 nr_targets;// per channel
	u8 nr_luns;// per target
	u8 nr_planes;// per LUN
	u16 nr_blocks;// per plane
	u16 nr_pages;// per block
};

#if HAVE_TSB_SUPPORT
#define NAND_FLAG_TSB_AIPR	BIT0
#endif

struct nand_info_t{
	// Nand ID
	u8 id[(NAND_ID_LENGTH + 3) & ~3];	// READ ID Buffer
	u8 nr_id_bytes;

	// Nand basic info
	enum nand_if_mode device_class;		// ONFI or Toggle
	u8 bit_per_cell;
	u8 addr_cycles;
	bool vcc_1p2v;				// ONFI 1.2V always DDR3, non 1.2V SDR/DDR/DDR2
	bool fake_page_param;	// When NAND no parameter page, fake one
	u8 max_tm;				// Max support timing mode, high nibble ndcu_if_mode, low nibble timing mode number
	u16 max_speed;
	u8 def_tm;				// Default timing mode after reset, high nibble ndcu_if_mode, low nibble timing mode number
	u8 cur_tm;				// Current timing mode
	u8 flags;				// Nand capability/status flag

	// Nand organization info
	struct target_geo_t geo;// Basic geometry shared to FTL
	u32 lun_num;// LUN (CH + CE + LUN) count
	u32 pda_lun_mask;// LUN id (CH + CE + LUN) mask
	u16 page_sz;// page user size excluding spare area, usually 16KB,
	u16 spare_sz;// spare area size
	u16 interleave;// #channels * #targets * #lun * #plane

	// Fields shift(and mask for non power of 2 fields) in width PDA format
	u8 pda_du_shift;
	u8 pda_interleave_shift;
	u8 pda_plane_shift;
	u8 pda_ch_shift;
	u8 pda_ce_shift;
	u8 pda_lun_shift;
	u8 pda_page_shift;
	u8 pda_block_shift;
	u32 pda_page_mask;
	u32 pda_block_mask;
	u32 pda_pageblock_mask;
	u32 cpda_blk_pg_shift;

	// LUN, block, plane, page shift in NAND row address
	u8 row_lun_shift;
	u8 row_block_shift;
	u8 row_pl_shift;
	u8 row_page_shift;
};

#ifdef RETRY_COMMIT_EVENT_TRIGGER
typedef struct
{
    bm_pl_t bm_pl;
    u16 pdu_bmp;
}recover_commit_t;
#endif


extern struct nand_info_t nand_info;

extern struct target_geo_t *nand_geo[];
bool ncl_cmd_empty(bool rw);
static inline struct target_geo_t * nal_get_geo(enum nal_pb_type pb_type) { return NULL; }
void ncl_spb_defect_scan(u32 spb, u32*defect);
struct target_geo_t* ncl_get_geo(void);
void ncl_set_meta_base(void *base_addr, enum meta_base_type);
extern u32 ncl_polling_status(pda_t pda);
void ncl_set_feature(pda_t pda, u32 fa, bool by_lun, u32 val);
u32 ncl_get_feature(pda_t pda, u32 fa, bool by_lun);
#ifdef DBG_NCL_SET_FEA_BE4_READ
void set_feature_dbg(pda_t pda, u32 val);
void set_feature_be4_read_dbg(struct ncl_cmd_t *ncl_cmd, u32 val);
#endif
/*!
 * @brief Read/Write a PDA for RAW data
 *
 * @param[in] pda		PDA
 * @param[in] pb_type		PDA PB type
 * @param[in] column		column address
 * @param[in] bm_tag		Pointer to one BM Tag
 * @param[in] is_prog		is prog or read
 * @param[in] rd_page_mode	Read RAW operation with page access, or 4K access
 * @param[in] src_data		Input source data
 *
 * @return the first DWORD for dtag
 */
u32 ncl_pda_raw_operation(pda_t pda, enum nal_pb_type pb_type, u16 column,
	bm_pl_t *bm_tag, bool is_prog, bool rd_page_mode, dtag_t *src_data);

/*!
 * @brief Make NCL enter MR operation mode
 *
 * @param void
 *
 * @return enter status
 */
bool ncl_enter_mr_mode(void);

/*!
 * @brief ncl leaves MR operation mode & return original operation mode
 *
 * @param void
 *
 * @return N/A
 */
void ncl_leave_mr_mode(void);

void ncl_cmd_wait_completion(void);

void ncl_handle_pending_cmd(void);

void ncl_cmd_wait_finish(void);

/*!
 * @brief NCL pmu init
 *
 * @param void
 *
 * @return N/A
 */
void ncl_pmu_init(void);

#define ficu_done_wait()	do {u32 flags; flags = irq_save(); ficu_isr(); irq_restore(flags);} while (0);

#endif
