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

/*! \file
 * @brief request definition in nvme
 *
 */

#include "list.h"

#define FE_SPECIFIC_FIELDS               \
union {                                  \
	struct                           \
	{                                \
		u8 sqid;           \
		u8 cntlid;           \
		u16 cid;            \
		u16 nvme_status;    \
		u32 cmd_spec;       \
	} nvme;                          \
} fe PACKED;

/*!
 * @brief Data direction
 */
typedef enum dir {
	READ = 0,	///< read, device to host
	WRITE		///< write, host to device
} dir_t;

/*!
 * @brief request op_code
 */
enum req_op_t {
	REQ_T_READ = 0,		///< read command
	REQ_T_WRITE,		///< write command
	REQ_T_FLUSH,		///< write cache flush command
	REQ_T_COMPARE,		///< write compare command
	REQ_T_TRIM,		///< trim command
	REQ_T_FORMAT, 		///< format command, may be merged with trim
	REQ_T_BP_READ,		///< read boot partition
	REQ_T_WZEROS,           ///< write zeros
	REQ_T_WUNC,             ///< write uncorrectable
	REQ_T_MAX
};

/*!
 * @brief definition of start lba range
 */
typedef struct _slba_range_t {
	u64 slba;		///< start lba
	u16 nlb;		///< number of lb
	u16 ndu;		///< number of du
} slba_range_t;

/*!
 * @brief definition of SLAB range list
 *
 * @list_cnt:
 * @range:
 *
 */
typedef struct _slba_range_list_t {
	u32 list_cnt;		///< number of LBA range list
	slba_range_t *range;		///< range list
} slba_range_list_t;

/*!
 * @brief definition of LBA range
 */
typedef union lba_range {
	slba_range_t srage;		///< slba range
	slba_range_list_t sragel;	///< lba range list
} lba_t;

typedef u64 prp_t;

/*!
 * @brief definition of prp entry
 */
typedef struct prp_entry {
	prp_t prp;		///< physical region page
	u32 size;		///< data length
} prp_entry_t;

/*!
 * @brief definition of Request prp
 */
typedef struct _req_prp_t {
	bool fetch_prp_list;	///< need to fetch prp list or not
	void *prp_list;		///< prp linked list

	u8 nalloc;		///< number to allocate
	u8 nprp;		///< number of prp

	u8 transferred;	///< how many prp data transfered
	u8 required;	///< how many resourced to store prp data

	prp_entry_t *prp;	///< prp entry list

	void *mem;		///< SRAM to store data in prp
	u32 mem_sz;	///< SRAM size

	u32 size;		///< transfer size

	// following is for admin large data transfer	20201222-Eddie-from CA6
       u16 sec_got_dtag_cnts; ///< security got dtag amount
       u16 sec_xfer_idx;      ///< security transfer index for large data transfer
       u32 sec_xfered_cnt;    ///< security transfered n bytes
       void *sec_xfer_buf;    ///< security buffer
       bool prp1_idx;
   
       u32 prp_offse;         ///< prp xfer start offset
       u32 data_size;         ///< actually ssd return host data

	bool fg_prp_list;	///indicate this transfer is prp lsit  
	bool is2dtag;		//indicate is this cmd get two dtag
} req_prp_t;

enum {
	REQ_ST_UNALLOC = 0,	///< request in free pool
	REQ_ST_ALLOC,		///< just allocated from free pool
	REQ_ST_FE_ADMIN,	///< admin command
	REQ_ST_FE_IO,		///< io command
	REQ_ST_WAIT_AUTO_LKP,	///< waiting for auto lookup result

	REQ_ST_DISK = 5,	///< already issued to dispatcher
};

enum {
	REQ_Q_IO	= 0,	///< request from io sq
	REQ_Q_ADMIN,		///< request from admin sq
	REQ_Q_OTHER,		///< request from others
	REQ_Q_BG		///< request from bg
};

/*!
 * @brief definition of Request
 */
typedef struct _req_t {
	u32 start;			///< request start time
	struct list_head entry;		///< entry list 1
	struct list_head entry2;	///< entry list 2

	u32 error     : 1;		///< for read error
	u32 req_from  : 2;		///< request from, REQ_Q_XXX
	u32 state     : 3;		///< REQ_ST_XXX
	u32 wholeCdb  : 1;		///< update whole CDB
	u32 postformat: 1;		///< restore cmd post task

	enum req_op_t opcode;		///< operation code

	u16 nvm_cmd_id;		///< the nvm cmd id of this request
	u8 req_id;
	u8 nsid;		///< namespace id

	req_prp_t req_prp;		///< request prp

	lba_t lba;			///< access lba

	union {
		struct {
			u32 *auto_rst;	///< auto lookup result
			u32 tid;		///< transaction ID
			u16 cnt;		///< count of auto lookup result
			u16 ofst;		///< large command processed offset
		} read;
		struct {
			bool shutdown;	///< if flush due to shutdown
			bool issued;	///< already issued or not
			u16 btag;	///< btag of BTN command of this req
		} flush;
		struct {
			bool meta_enable;	///< if pi was enabled
			u8 erase_type;		///< erase type for Secure Erase Setting
		} format;
		struct {
			u16 nr;	///< number of range, 1 based against 0 based from cdw10
			u8 att;	///< attribute from cdw11
			void *dsmr;	///< dsm trim ranges
		} trim;
		struct {
			void *dtag_mem;	///< transfer buffer
		} bp_read;
		struct {
			union {
				u16 sq_id;
			};
		} admin;
		struct {
			bool op_type;	///< attach or detach
			u32 ns_id;	///< namespace id
		} ns_ctrl;	///< only works for namespace management operation.
		struct {
			bool issued;	///< if this req was handled
			u32 left;	///< wucc should be FUA, the left DU should be decreased when program done
		} wucc;
	} op_fields;

	void *host_cmd;			///< host command pointer
	fe_t fe;		///< specific field depending on FE(nvme, or others)
	bool (*completion)(struct _req_t *);	///< completion callback function
} req_t;

typedef struct{
	fe_t fe;
	u16 flag;
}commit_ca3;

typedef struct{
	u8 flag;
	u8 rsvd[3];
}is_IOQ;
BUILD_BUG_ON(sizeof(is_IOQ) != 4); 

extern void nvmet_warmboot_handle_commit_done();
