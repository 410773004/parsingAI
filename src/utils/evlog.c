//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
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
/*! \file evlog.c
 * @brief event log module, record event log and save/retrieve into/from nand
 *
 * \addtogroup utils
 * \defgroup evlog
 * \ingroup utils
 * @{
 */
//=============================================================================
#if defined(RDISK)
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#define EVLOG_C

#include "evlog.h"
#include "srb.h"
#include "mod.h"
#include "sect.h"
#include "eccu.h"
#include "string.h"
#include "cpu_msg.h"
#include "types.h"
#include "stdlib.h"
#include "queue.h"
#include "io.h"
#include "ncl.h"
#include "ncl_cmd.h"
#include "ncl_err.h"
#include "ipc_api.h"
#include "console.h"
#include "idx_meta.h"
#include "cbf.h"
#include "sync_ncl_helper.h"
#include "spin_lock.h"
#include "event.h"
#include "l2p_mgr.h"
#include "../nvme/inc/req.h"
#include "../nvme/inc/nvme_spec.h"
#include "spi.h"
#include "spi_register.h"
#include "fc_export.h"

/*! \cond PRIVATE */
#define __FILEID__ utils
#include "trace.h"
/*! \endcond */

extern void *get_fw_version();
volatile extern u32 log_number;
// ----------------------------------------------------------------------------
// debug switch
// ----------------------------------------------------------------------------
// #define EVLOG_DUMP_AGT
// #define EVLOG_DUMP_EVLOG
// #define EVLOG_PRINT_FUNC_LINE
// #define EVLOG_DUMP_MGR
// #define EVLOG_DUMP_RECONSTRUCTION_BUF
// #define EVLOG_DEBUG_BUF
// #define EVLOG_DEBUG_AGT

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define EVLOG_MGR_CPU		      3		///< evlog manager allocation CPU
#define NVME_CPU                  1

#define EVLOG_SIG		          0x65766c67 	///< 'evlg'

#define EVLOG_AGT_BUF_SZ	      (78 * DTAG_SZE)
#define EVLOG_AGT_LOG_STRUCT_CNT  (EVLOG_AGT_BUF_SZ / sizeof(log_buf_t))
#define EVLOG_DDR_BUF_SZE	      (EVLOG_AGT_BUF_SZ * 4)
#define EVLOG_FLUSH_BUF_SZE	      (NAND_PAGE_SIZE)

#define MAX_BYTE_CNT_OF_A_LOG     (11 * sizeof(log_buf_t))

#define PL_TO_CURR_BUF_IDX(x)     (((x) & 0xFFFF0000) >> 16)
#define PL_TO_SUGGEST_BYTE_CNT(x) ( (x) & 0x0000FFFF       )
#define DEFAULT_FLUSh_PL(buf_idx, byte_cnt) ((((buf_idx) & 0xFFFF) << 16) | ((byte_cnt) & 0xFFFF))

#define SWAP_X_Y(x,y,tmp)         do{memcpy(&(tmp),&(x),sizeof(tmp)); memcpy(&(x),&(y),sizeof(x));memcpy(&(y),&(tmp),sizeof(y));}while(0)

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef enum
{
	NAND_LOG_BLK_UNINITIALIZED,
	NAND_LOG_BLK_NEED_TO_BE_ERASED,
	NAND_LOG_BLK_READY_TO_WRITE_DATA,
}first_nand_log_blk_status_t;

/*! @brief meta field of du0 in event log page */
typedef struct _evlog_du0_meta_t {
	u32 seed;		///< seed
	u32 signature;		///< signature of event log
	u32 flush_id_lo;	///< flush id lo 32b of u64
	u32 flush_id_hi;	///< flush id hi 32b of u64
	u32 rsvd[4];
} evlog_du0_meta_t;

/*! @brief meta field of du1 in event log page */
typedef struct _evlog_du1_meta_t {
	u32 seed;		///< seed
	u32 payload[7];		///< reserved payload of event log
} evlog_du1_meta_t;

/*! @brief meta field of du2 in event log page */
typedef struct _evlog_du2_meta_t {
	u32 seed;		///< seed
	u32 payload[7];		///< reserved payload of event log
} evlog_du2_meta_t;

/*! @brief meta field of du3 in event log page */
typedef struct _log_du3_meta_t {
	u32 seed;		///< seed
	u32 payload[7];		///< reserved payload of event log
} evlog_du3_meta_t;

/*! @brief du meta of log page */
typedef struct _evlog_meta_t {
	evlog_du0_meta_t meta0;	///< meta of evlog du0
	evlog_du1_meta_t meta1;	///< meta of evlog du1
	evlog_du2_meta_t meta2;	///< meta of evlog du2
	evlog_du3_meta_t meta3;	///< meta of evlog du3
} evlog_meta_t;
BUILD_BUG_ON(sizeof(evlog_meta_t) != (sizeof(struct du_meta_fmt) << DU_CNT_SHIFT));

typedef struct _evlog_t {
	pda_t *blk_pda_base;
	u64 newest_flush_id;
	// u64 oldest_flush_id;
	u32 next_flush_idx;
	u32 nr_pg_per_blk;
	void* flush_buf;
	u32 flush_buf_size;
	u32 itlv_du_cnt;
	first_nand_log_blk_status_t first_nand_log_blk_status;
	bool flush_enable;
} evlog_t;

typedef struct _evlog_agt_buf_t
{
	void *ptr;
	u32 buf_byte_size;
	u32 byte_wptr;
	u32 byte_rptr;
	u32 uart_byte_rptr;
	u32 uart_data_byte_cnt;
	u8 *log_tag;
	u8 curr_tag;
}evlog_agt_buf_t;

typedef struct _evlog_agt_t {
	bool initialized;	///< initialization of event log agent
	bool evt_triggering;
	evlog_agt_buf_t buf[2];
	u16 curr_buf_idx;
} evlog_agt_t;

typedef enum {
	MGR_IDLE = 0,	///< manager status idle
	MGR_BUSY = 1,	///< manager status busy, e.g. copy and flush
} mgr_sts_t;

typedef struct _evt_trigger_cnt_t
{
	u32 trigger[MPC];
	u32 complete_buf_swap[MPC];
	u32 complete_copy[MPC];
	u32 complete_flush[MPC];
	bool complete_uart[MPC];
}evt_trigger_cnt_t;

/*!
 * @brief definition state machine to event log manager
 */
typedef struct _evlog_mgr_t {
	bool initialized;		///< initialization of event log manager
	bool panic_nand_log_triggered;
	bool panic_before_initialized[MPC];
	void* ddr_flush_buf;
	void* ddr_flush_buf_cur;
	u32 ddr_buf_byte_size;
	evlog_agt_t agt[MPC];		///< event log agents information for each cpus
	evt_trigger_cnt_t evt_trigger_cnt;
} evlog_mgr_t;

typedef struct _flush_opt_t
{
	u32 cpu_id;
	bool must;
} flush_opt_t;

typedef struct _flush_id_t
{
	u32 lo;
	u32 hi;
}flush_id_t;

typedef struct _cpu_flush_info_t
{
	u32 byte_cnt;
	u16 flush_buf_idx;
	u32 cpu_id_0;
}cpu_flush_info_t;
//-----------------------------------------------------------------------------
// event message
//-----------------------------------------------------------------------------
extern u64 sys_time;

static const char digits[] = { "0123456789ABCDEF" };
static int count_out = 320;

#ifdef EVLOG_DEBUG_AGT
static bool has_printed = false;
#define DEBUG_MSG_CNT 16
static fast_data_zi char debug_msg[DEBUG_MSG_CNT][256];

#define OPT_MON_AGT_WRITE_PTR_GT                0x00000001
#define OPT_MON_AGT_WRITE_PTR_GEQ               0x00000002
#define OPT_MON_AGT_READ_PTR_GT                 0x00000004
#define OPT_MON_AGT_READ_PTR_GEQ                0x00000008
#define OPT_MON_AGT_UART_READ_PTR_GT            0x00000004
#define OPT_MON_AGT_UART_READ_PTR_GEQ           0x00000008
#define OPT_MON_AGT_QUOTE_LT_ZERO               0x00000010
#define OPT_MON_AGT_UART_READ_PTR_GEQ_WRTIE_PTR 0x00000020
#define OPT_MON_AGT_AGT_ALL                     0xFFFFFFFF
#endif
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
#if (CPU_ID == EVLOG_MGR_CPU)
static fast_data_zi evlog_t _evlog;			///< event log data information
static slow_data_zi u32 evlog_byte_limit[MPC];
#endif
#if (CPU_ID == EVLOG_MGR_CPU)|| (CPU_ID == NVME_CPU)
share_data_zi volatile dtag_t evlog_dtag;			///< read/write dtag for event log
#endif

#if (CPU_ID == EVLOG_MGR_CPU) || (CPU_ID == NVME_CPU)
#define EVT_BLK_CNT 8
static fast_data_zi ncl_page_res_t evlog_page_res;		///< ncl page resource for event log
static fast_data_zi evlog_meta_t *evlog_meta = NULL;	///< read/write meta for event log
#endif

#if (CPU_ID == 1)
#define _evlog_agt_buf ((u8 (*)[2][EVLOG_AGT_BUF_SZ])&__evlog_agt_buf_start)
#define _evlog_tag ((u8 (*)[2][EVLOG_AGT_LOG_STRUCT_CNT])&__evlog_log_tag_start)

ddr_sh_data bool panic_occure[4] = {false, false, false, false};
ddr_sh_data u32 panic_info[4][9] = {{0,0,0,0,0,0,0,0,0}, {0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0}};
ddr_sh_data evlog_mgr_t _evlog_mgr = { .initialized = false, .panic_nand_log_triggered = false,
					.panic_before_initialized[0] = false, .panic_before_initialized[1] = false, .panic_before_initialized[2] = false, .panic_before_initialized[3] = false,
					.ddr_flush_buf = &__evlog_flush_buf_start, .ddr_flush_buf_cur = &__evlog_flush_buf_start, .ddr_buf_byte_size = EVLOG_DDR_BUF_SZE,
					.agt[0] = {.initialized = true, .curr_buf_idx = 0, .buf[0].ptr = _evlog_agt_buf[0][0], .buf[0].buf_byte_size = sizeof(_evlog_agt_buf[0][0]), .buf[0].log_tag = _evlog_tag[0][0], .buf[0].byte_wptr = 0, .buf[0].byte_rptr = 0, .buf[0].uart_byte_rptr = 0, .buf[0].uart_data_byte_cnt = 0, .buf[0].curr_tag = 0, .buf[1].ptr = _evlog_agt_buf[0][1], .buf[1].buf_byte_size = sizeof(_evlog_agt_buf[0][1]),  .buf[1].log_tag = _evlog_tag[0][1], .buf[1].byte_wptr = 0, .buf[1].byte_rptr = 0, .buf[1].uart_byte_rptr = 0, .buf[1].uart_data_byte_cnt = 0, .buf[1].curr_tag = 0},
					.agt[1] = {.initialized = true, .curr_buf_idx = 0, .buf[0].ptr = _evlog_agt_buf[1][0], .buf[0].buf_byte_size = sizeof(_evlog_agt_buf[1][0]), .buf[0].log_tag = _evlog_tag[1][0], .buf[0].byte_wptr = 0, .buf[0].byte_rptr = 0, .buf[0].uart_byte_rptr = 0, .buf[0].uart_data_byte_cnt = 0, .buf[0].curr_tag = 0, .buf[1].ptr = _evlog_agt_buf[1][1], .buf[1].buf_byte_size = sizeof(_evlog_agt_buf[1][1]),  .buf[1].log_tag = _evlog_tag[1][1], .buf[1].byte_wptr = 0, .buf[1].byte_rptr = 0, .buf[1].uart_byte_rptr = 0, .buf[1].uart_data_byte_cnt = 0, .buf[1].curr_tag = 0},
					.agt[2] = {.initialized = true, .curr_buf_idx = 0, .buf[0].ptr = _evlog_agt_buf[2][0], .buf[0].buf_byte_size = sizeof(_evlog_agt_buf[2][0]), .buf[0].log_tag = _evlog_tag[2][0], .buf[0].byte_wptr = 0, .buf[0].byte_rptr = 0, .buf[0].uart_byte_rptr = 0, .buf[0].uart_data_byte_cnt = 0, .buf[0].curr_tag = 0, .buf[1].ptr = _evlog_agt_buf[2][1], .buf[1].buf_byte_size = sizeof(_evlog_agt_buf[2][1]),  .buf[1].log_tag = _evlog_tag[2][1], .buf[1].byte_wptr = 0, .buf[1].byte_rptr = 0, .buf[1].uart_byte_rptr = 0, .buf[1].uart_data_byte_cnt = 0, .buf[1].curr_tag = 0},
					.agt[3] = {.initialized = true, .curr_buf_idx = 0, .buf[0].ptr = _evlog_agt_buf[3][0], .buf[0].buf_byte_size = sizeof(_evlog_agt_buf[3][0]), .buf[0].log_tag = _evlog_tag[3][0], .buf[0].byte_wptr = 0, .buf[0].byte_rptr = 0, .buf[0].uart_byte_rptr = 0, .buf[0].uart_data_byte_cnt = 0, .buf[0].curr_tag = 0, .buf[1].ptr = _evlog_agt_buf[3][1], .buf[1].buf_byte_size = sizeof(_evlog_agt_buf[3][1]),  .buf[1].log_tag = _evlog_tag[3][1], .buf[1].byte_wptr = 0, .buf[1].byte_rptr = 0, .buf[1].uart_byte_rptr = 0, .buf[1].uart_data_byte_cnt = 0, .buf[1].curr_tag = 0},
					.evt_trigger_cnt.trigger[0] = 0, .evt_trigger_cnt.complete_buf_swap[0] = 0, .evt_trigger_cnt.complete_copy[0] = 0, .evt_trigger_cnt.complete_flush[0] = 0, .evt_trigger_cnt.complete_uart[0] = true,
					.evt_trigger_cnt.trigger[1] = 0, .evt_trigger_cnt.complete_buf_swap[1] = 0, .evt_trigger_cnt.complete_copy[1] = 0, .evt_trigger_cnt.complete_flush[1] = 0, .evt_trigger_cnt.complete_uart[1] = true,
					.evt_trigger_cnt.trigger[2] = 0, .evt_trigger_cnt.complete_buf_swap[2] = 0, .evt_trigger_cnt.complete_copy[2] = 0, .evt_trigger_cnt.complete_flush[2] = 0, .evt_trigger_cnt.complete_uart[2] = true,
					.evt_trigger_cnt.trigger[3] = 0, .evt_trigger_cnt.complete_buf_swap[3] = 0, .evt_trigger_cnt.complete_copy[3] = 0, .evt_trigger_cnt.complete_flush[3] = 0, .evt_trigger_cnt.complete_uart[3] = true,};			///< event log manager
share_data_zi pda_t blk_pda_base[EVT_BLK_CNT];
#if CO_SUPPORT_PANIC_DEGRADED_MODE
fast_data_zi u8 evt_degradedMode = 0;
#endif
#else
ddr_sh_data evlog_mgr_t _evlog_mgr;
ddr_sh_data bool panic_occure[4];
ddr_sh_data u32 panic_info[4][9];
#endif

share_data_zi volatile bool in_panic;
extern volatile u32 plp_log_number_start;

static fast_data evlog_agt_t* agt = &_evlog_mgr.agt[CPU_ID_0];		///< event log agent pointer for local cpu
static fast_data evt_trigger_cnt_t *evt_trigger_cnt = &(_evlog_mgr.evt_trigger_cnt);
extern volatile u8 plp_trigger;
share_data_zi u32 evlog_next_index; 

extern bool _fg_warm_boot; 
extern fast_data stFTL_MANAGER gFtlMgr; 

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------
share_data_zi void *shr_dtag_meta;				///< share dtag meta buffer

void evlog_flush(evlog_mgr_t* mgr,u16 evt_reason_id);
ddr_code void mgr_spi_nor(evlog_mgr_t* mgr);

fast_data u8 evt_evlog_regularly_flush_uart = 0xFF;
fast_data u8 evt_evlog_default_flush_uart = 0xFF;


#if (CPU_ID == EVLOG_MGR_CPU)
static u32 agt_buf_wait_flush_byte_cnt(evlog_agt_buf_t *buf, u32 mgr_log_buf_budget);
fast_data u8 evt_evlog_flush = 0xFF;			///< event to flush log
fast_data u8 evt_evlog_erase_and_reset = 0xFF;
void mgr_copy_and_flush_to_nand(u32 p0, u32 p1, u32 pl);
void clear_nand_log_block_and_reset(u32 p0, u32 p1, u32 pl);
share_data_zi pda_t blk_pda_base[EVT_BLK_CNT];
#endif
void mgr_init(void);
u32 flush_to_uart(evlog_agt_buf_t *buf, u32 expect_or_absolute, u32 byte_cnt);
//static bool is_evt_triggered();
static u32 evt_triggered_cnt();

extern char dri_sn[32];
#if(CORR_ERR_INT == ENABLE)
extern volatile u32 RxErr_cnt;
#ifdef RXERR_IRQ_RETRAIN
extern volatile u8 retrain_cnt;
#endif
#endif

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
#ifdef EVLOG_PRINT_FUNC_LINE
#define printk_func_line() printk("[%s][%d]\n", __FUNCTION__, __LINE__)
#else
#define printk_func_line()
#endif

// get bytes count of log
static u32 agt_buf_data_byte_cnt(evlog_agt_buf_t *buf)
{
	if(buf->byte_wptr == buf->byte_rptr)
		return 0;
	return (buf->byte_wptr > buf->byte_rptr)? (buf->byte_wptr - buf->byte_rptr): (buf->buf_byte_size - (buf->byte_rptr - buf->byte_wptr));
}

#ifdef EVLOG_DEBUG_AGT
// get bytes count quote to save log
static u32 agt_buf_byte_cnt_quote(evlog_agt_buf_t *buf)
{
	if(buf->byte_wptr == buf->byte_rptr)
		return buf->buf_byte_size - sizeof(log_buf_t);
	return (buf->byte_rptr > buf->byte_wptr)? (buf->byte_rptr - buf->byte_wptr - sizeof(log_buf_t)): (buf->buf_byte_size - (buf->byte_wptr - buf->byte_rptr) - sizeof(log_buf_t));
}


static bool check_agt(const char *function, s32 line, u32 opt)
{
	bool does_error_occurred = false;
	static int old_idx = 0, new_idx = 0;
	int i = 0;
	evlog_agt_buf_t *buf = &(agt->buf[agt->curr_buf_idx]);
	s32 data[] = {line, CPU_ID, agt->initialized, agt_buf_byte_cnt_quote(buf), buf->buf_byte_size, agt_buf_data_byte_cnt(buf), buf->uart_data_byte_cnt, buf->byte_wptr, buf->byte_rptr, buf->uart_byte_rptr};

	if(!(agt->initialized))
		return false;

	memset(&(debug_msg[new_idx]), 0, 256);
	debug_msg[new_idx][0] = '[';
	strcpy(&(debug_msg[new_idx][1]), function);
	memset(&(debug_msg[new_idx][1 + strlen(function)]), ' ', 30 - (1 + strlen(function)));
	debug_msg[new_idx][29] = ']';

	// if(agt_buf_data_byte_cnt(buf) < 0 || agt_buf_data_byte_cnt(buf) + sizeof(log_buf_t) > buf->buf_byte_size || buf->byte_wptr > buf->buf_byte_size || buf->byte_rptr > buf->buf_byte_size)
	// {
	// 	does_error_occurred = true;
	// 	doprint(&(debug_msg[new_idx][30]), 226, "[%d]xxxxxx CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	// }
	// if(buf->byte_wptr > buf->uart_byte_rptr && buf->byte_wptr - buf->uart_byte_rptr != buf->uart_data_byte_cnt)
	// {
	// 	does_error_occurred = true;
	// 	doprint(&(debug_msg[new_idx][30]), 226, "[%d]~~~~~~ CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	// }
	// if(buf->byte_wptr < buf->uart_byte_rptr && buf->buf_byte_size - (buf->uart_byte_rptr - buf->byte_wptr) != buf->uart_data_byte_cnt)
	// {
	// 	does_error_occurred = true;
	// 	doprint(&(debug_msg[new_idx][30]), 226, "[%d]###### CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	// }
	// if((buf->byte_wptr == buf->uart_byte_rptr) && (buf->uart_data_byte_cnt != 0) && (buf->uart_data_byte_cnt != buf->buf_byte_size))
	// {
	// 	does_error_occurred = true;
	// 	doprint(&(debug_msg[new_idx][30]), 226, "[%d]^^^^^^ CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	// }
	// if((buf->byte_wptr == buf->byte_rptr) && (agt_buf_data_byte_cnt(buf) != 0))
	// {
	// 	does_error_occurred = true;
	// 	doprint(&(debug_msg[new_idx][30]), 226, "[%d]****** CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	// }

	if((opt & OPT_MON_AGT_WRITE_PTR_GT) && buf->byte_wptr > buf->buf_byte_size)
	{
		does_error_occurred = true;
		doprint(&(debug_msg[new_idx][30]), 226, "[%d]------ CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	}

	if((opt & OPT_MON_AGT_WRITE_PTR_GEQ) && buf->byte_wptr >= buf->buf_byte_size)
	{
		does_error_occurred = true;
		doprint(&(debug_msg[new_idx][30]), 226, "[%d]------ CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	}

	if((opt & OPT_MON_AGT_QUOTE_LT_ZERO) && agt_buf_byte_cnt_quote(buf) < 0)
	{
		does_error_occurred = true;
		doprint(&(debug_msg[new_idx][30]), 226, "[%d]!!!!!! CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	}

	if((opt & OPT_MON_AGT_READ_PTR_GEQ) && buf->byte_rptr >= buf->buf_byte_size)
	{
		does_error_occurred = true;
		doprint(&(debug_msg[new_idx][30]), 226, "[%d]`````` CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	}

	if((opt & OPT_MON_AGT_UART_READ_PTR_GEQ) && buf->uart_byte_rptr >= buf->buf_byte_size)
	{
		does_error_occurred = true;
		doprint(&(debug_msg[new_idx][30]), 226, "[%d]?????? CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	}

	// if((opt & OPT_MON_AGT_UART_READ_PTR_GEQ_WRTIE_PTR) && buf->uart_byte_rptr >= buf->byte_wptr)
	// {
	// 	does_error_occurred = true;
	// 	doprint(&(debug_msg[new_idx][30]), 226, "[%d]|||||| CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);
	// }

	if(does_error_occurred == false)
		doprint(&(debug_msg[new_idx][30]), 226, "[%d]       CPU = %d, init = %d, quote = %d, buf_byte = %d, data_byte = %d, uart_data_byte = %d, byte_wptr = %d, byte_rptr = %d, uart_byte_rptr = %d", data);

	++new_idx;
	if(new_idx == DEBUG_MSG_CNT)
		new_idx = 0;
	if(new_idx == old_idx)
		++old_idx;
	if(old_idx == DEBUG_MSG_CNT)
		old_idx = 0;

	if(does_error_occurred == true && has_printed == false)
	{
		printk("\n=== debug msg start ===\n");
		for(i = old_idx; i != new_idx; i = (i + 1) % DEBUG_MSG_CNT)
			printk("\n %s \n", debug_msg[i]);
		printk("\n=== debug msg end ===\n");
		// has_printed = true;
	}

	return does_error_occurred;
}
#else
#define check_agt(function, line, opt)
#endif

// static  u32 agt_buf_to_curr_idx(evlog_agt_buf_t *buf)
// {
// 	if(buf == &(agt->buf[0]))
// 		return 0;
// 	else
// 		return 1;
// }


#if (CPU_ID == EVLOG_MGR_CPU) || (CPU_ID == NVME_CPU)
/*!
 * @brief read one event block page
 *
 * @param pda		pda to be writte
 * @param ptr		dtat buffer pointer
 *
 * @return status	read status
 */
nal_status_t read_one_evtb_page(pda_t pda, u32 dtag)
{
	u32 i = 0;
	struct ncl_cmd_t *ncl_cmd = &evlog_page_res.ncl_cmd;

	evlog_meta = (evlog_meta_t *) (&((evlog_du0_meta_t *)shr_dtag_meta)[dtag]);

	memset(evlog_meta, 0, sizeof(evlog_meta_t));

	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_READ;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_TAG_EXT_FLAG;
    ncl_cmd->retry_step = 0;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = NULL;

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		evlog_page_res.pda[i] = pda + i;

		evlog_page_res.info[i].status = ficu_err_good;
		evlog_page_res.info[i].pb_type = NAL_PB_TYPE_SLC;

		evlog_page_res.bm_pl[i].all = 0;
		evlog_page_res.bm_pl[i].pl.dtag = dtag + i;
		evlog_page_res.bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG;
	}

	ncl_cmd->user_tag_list = evlog_page_res.bm_pl;
	ncl_cmd->addr_param.common_param.list_len = DU_CNT_PER_PAGE;
	ncl_cmd->addr_param.common_param.pda_list = evlog_page_res.pda;
	ncl_cmd->addr_param.common_param.info_list = evlog_page_res.info;

	ncl_cmd_submit(ncl_cmd);

	// printk("[%s][%d] ncl_cmd->status = %d\n", __FUNCTION__, __LINE__, ncl_cmd->status);

    #if 0 //NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    if (ncl_cmd->status)
    {
        if (ncl_cmd->retry_step == 0)
        {
	        evlog_printk(LOG_ERR, "Read evlog fail, enter read-retry, ncl_cmd: 0x%d", ncl_cmd);
            rd_err_handling(ncl_cmd);
        }
    }
    #endif

	if (ncl_cmd->status != ficu_err_good && ncl_cmd->status != ficu_err_par_err)
	{
		for (i = 0; i < DU_CNT_PER_PAGE; i++) {
			if (evlog_page_res.info[i].status)
				return evlog_page_res.info[i].status;
		}
	}

	return ficu_err_good;
}

/*!
 * @brief get event block pda address
 *
 * @param base		event block base pda address
 * @param page_idx	block (page) index of event block to be found
 *
 * @return pda		page of pda to be found
 */
pda_t get_evtb_page_pda(pda_t base, u32 page_idx)
{
#if (CPU_ID == EVLOG_MGR_CPU)
	return (base + _evlog.itlv_du_cnt * page_idx);
#elif (CPU_ID == NVME_CPU)
	return (base + (shr_nand_info.interleave << DU_CNT_SHIFT) * page_idx);
#endif
}

/*!
 * @brief Determine the latest written page in a event log blk via binary search with meta's signature check
 *
 * @param b	event log blk pda base
 * @param s	start of page index
 * @param c	count of page in event log blk
 *
 * @return	return latest written page index
 */
u32 evtb_decide_latest_page(pda_t b, u32 s, u32 c, u32 sig, dtag_t dtag)
{
	u32 r = ~0;
	pda_t p;
	u32 e = c - 1;

	utils_apl_trace(LOG_ALW, 0x2020, "b = %x, s = %x, c = %x\n", b, s, c);
	while (s <= e) {
		u32 m = (s + e) >> 1;
		p = get_evtb_page_pda(b, m);

		read_one_evtb_page(p, dtag.dtag);
		utils_apl_trace(LOG_ALW, 0xb3b4, "b = %x, s = %x, e = %x, m = %x, p = %x, meta0.signature = %x", b, s, e, m, p, evlog_meta->meta0.signature);
		if (evlog_meta->meta0.signature == sig) {
			s = m + 1;
			r = m;
			utils_apl_trace(LOG_ALW, 0xeedc, "b = %x, s = %x, r = %x, m = %x", b, s, r, m);
		} else {
			if (m == 0) {
				r = ~0;
				break;
			}
			e = m - 1;
		}
	}

	sys_assert(r != ~0);
	utils_apl_trace(LOG_ALW, 0xd5b2, "b = %x, r = %x", b, r);
	return r;
}

void get_ev_log_desc(ev_log_desc_t *desc, u32 elem_cnt)
{
	int i = 0;
	//dtag_t buf_dtag = dtag_cont_get(DTAG_T_SRAM, 4);
	nal_status_t ret = ficu_err_good;
	u32 cnt = min(elem_cnt, EVT_BLK_CNT);
	for(i = 0; i < cnt; ++i)
	{
		desc[i].pda = blk_pda_base[i];
		ret = read_one_evtb_page(desc[i].pda, evlog_dtag.dtag);
		if ((ret == ficu_err_good || ret == ficu_err_par_err) && (evlog_meta->meta0.signature == EVLOG_SIG))
		{
			desc[i].data_page_cnt = 1 + evtb_decide_latest_page(desc[i].pda, 0, shr_nand_info.geo.nr_pages / shr_nand_info.bit_per_cell, EVLOG_SIG, evlog_dtag);
			read_one_evtb_page(get_evtb_page_pda(desc[i].pda, desc[i].data_page_cnt - 1), evlog_dtag.dtag);
			desc[i].flush_id_lo = evlog_meta->meta0.flush_id_lo;
			desc[i].flush_id_hi = evlog_meta->meta0.flush_id_hi;
		}
		else
		{
			desc[i].data_page_cnt = 0;
			desc[i].flush_id_lo = 0;
			desc[i].flush_id_hi = 0;
		}
	}
	//dtag_cont_put(DTAG_T_SRAM, buf_dtag, 4);
}
ddr_code void ipc_evlog_dump(volatile cpu_msg_req_t *req)
{
    req_t *cmd_req = (req_t *) req->pl;
    u16 flags = req->cmd.flags;
	u16 i = 0;
    ev_log_desc_t desc[8] = {0};
	dtag_t * prp_dtag = (dtag_t *)cmd_req->req_prp.mem;
    struct nvme_cmd *cmd =(struct nvme_cmd *) cmd_req->host_cmd;
    u32 nbt = cmd_req->req_prp.data_size;
    u16 start_page = (cmd->opc == 0x2)?cmd->cdw12:(cmd->cdw13>>16);
	pda_t pda = cmd->cdw14;
    switch (flags){
    case 0:
		get_ev_log_desc(desc, ARRAY_SIZE(desc));
		memset(dtag2mem(prp_dtag[0]), 0, nbt);
		memcpy(dtag2mem(prp_dtag[0]), desc, sizeof(desc));
        break;
	case 1:
		for(i = 0; i < nbt / DTAG_SZE; i += 4)
        {
			read_one_evtb_page(get_evtb_page_pda(pda, start_page + i / 4), evlog_dtag.dtag);
            memcpy((void*)sdtag2mem(prp_dtag[i].b.dtag), (void*)sdtag2mem(evlog_dtag.b.dtag), DTAG_SZE);
            memcpy((void*)sdtag2mem(prp_dtag[i+1].b.dtag), (void*)sdtag2mem((evlog_dtag.b.dtag+1)), DTAG_SZE);
            memcpy((void*)sdtag2mem(prp_dtag[i+2].b.dtag), (void*)sdtag2mem((evlog_dtag.b.dtag+2)), DTAG_SZE);
            memcpy((void*)sdtag2mem(prp_dtag[i+3].b.dtag), (void*)sdtag2mem((evlog_dtag.b.dtag+3)), DTAG_SZE);
        }
        break;
     default:
        sys_assert(0);
    }
    cpu_msg_issue(CPU_FE-1,CPU_MSG_EVLOG_DUMP_ACK,flags,(u32)cmd_req);
}
#endif


ddr_code void ipc_get_telemetry_ctrlr_from_U3(volatile cpu_msg_req_t *req)
{
	/*extern evlog_t _evlog;
	u8 *buf = (u8 *) req->pl;
	memcpy(buf, _evlog.flush_buf,7680);*/
	flush_to_nand(EVT_TELE);
    cpu_msg_issue(req->cmd.tx, CPU_MSG_GET_TELEMETRY_DATA_DONE, 0, 0);
}

ddr_code void loop_forever()
{
	// printk("[%s][%d] CPU_ID = %d\n", __FUNCTION__, __LINE__, CPU_ID);
	do {
		extern void uart_poll(void);
		uart_poll();
	} while (1);
}

ddr_code void ipc_panic(volatile cpu_msg_req_t *req)
{
	loop_forever();
}

#if (CPU_ID == EVLOG_MGR_CPU)
#ifdef EVLOG_DEBUG_BUF
static void dump_buf(char *desc, int d)
{
	int i = 0;
	printk("\n=== [%s][%d] ===", desc, d);
	for(i = 0; i < EVLOG_FLUSH_BUF_SZE; ++i)
	{
		if(i % 16 == 0)
			printk("\n0x%x: ", i);

		printk("%b ", ((unsigned char *)_evlog.flush_buf)[i]);
	}
	printk("\n=== [%s][%d] ===\n", desc, d);
}
#else
#define dump_buf(desc,d)
#endif

typedef void *(*cpy_fun)(void *dest, const void *src, unsigned long count);
typedef void *(*set_fun)(void *s, int c, unsigned int count);

fast_code void *memcpy_plp(void *dest, const void *src, unsigned long count)
{
	char *tmp = (char *)dest, *s = (char *)src;

	while (count--)
	{
		if(plp_trigger)
			return NULL;
		*tmp++ = *s++;
	}


	return dest;
}

fast_code void *memset_plp(void *s, int c, unsigned int count)
{
	char *xs = (char *)s;

	while (count--)
	{
		if(plp_trigger)
			return NULL;
		*xs++ = c;
	}

	return s;
}

/*!
 * @brief erase event block
 *
 * @param pda		event block pda base address
 *
 * @return status	erase status
 */
static int erase_evtb(pda_t pda)
{
	struct ncl_cmd_t *ncl_cmd = &evlog_page_res.ncl_cmd;
	pda_t pda_list[32];
	struct info_param_t info_list[32];
	memset(info_list, 0, sizeof(struct info_param_t));
	pda_list[0] = pda;
	info_list[0].pb_type = NAL_PB_TYPE_SLC;

	ncl_cmd->addr_param.common_param.info_list = info_list;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;
	ncl_cmd->addr_param.common_param.list_len = 1;

	ncl_cmd->caller_priv = NULL;
	ncl_cmd->completion = NULL;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG | NCL_CMD_META_DISCARD;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->op_type = INT_TABLE_READ_PRE_ASSIGN;

	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->du_format_no = DU_FMT_USER_4K;
	ncl_cmd->status = 0;

	ncl_cmd_submit(ncl_cmd);

	return ncl_cmd->status;
}

/*!
 * @brief write one event block page
 *
 * @param pda	pda to be writte
 * @param ptr	dtat buffer pointer
 *
 * @return	not used
 */
static int write_one_evtb_page(pda_t pda, void* ptr)
{
	// evlog_printk(LOG_INFO, "[%s][%d] base pda = 0x%x, cnt = %d", __FUNCTION__, __LINE__, pda % 1024, pda / 1024);
	u32 i;
	dtag_t dtag = mem2dtag(ptr);
	struct ncl_cmd_t *ncl_cmd = &evlog_page_res.ncl_cmd;

	evlog_meta = (evlog_meta_t *) (&((evlog_du0_meta_t *)shr_dtag_meta)[mem2sdtag(ptr)]);
	evlog_meta->meta0.signature = EVLOG_SIG;
	evlog_meta->meta0.seed = pda;
	evlog_meta->meta1.seed = pda + 1;
	evlog_meta->meta2.seed = pda + 2;
	evlog_meta->meta3.seed = pda + 3;
	evlog_meta->meta0.flush_id_hi = (_evlog.newest_flush_id >> 16) >> 16;
	evlog_meta->meta0.flush_id_lo = (u32) _evlog.newest_flush_id;
	_evlog.newest_flush_id++;

	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		evlog_page_res.pda[i] = pda + i;

		evlog_page_res.info[i].pb_type = NAL_PB_TYPE_SLC;
		evlog_page_res.info[i].xlc.slc_idx = 0;

		evlog_page_res.bm_pl[i].all = 0;
		evlog_page_res.bm_pl[i].pl.dtag = dtag.dtag + i;
		evlog_page_res.bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG;
	}

	ncl_cmd->completion = NULL;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_WRITE;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;

	ncl_cmd->op_type = NCL_CMD_PROGRAM_TABLE;
	ncl_cmd->user_tag_list = evlog_page_res.bm_pl;

	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = evlog_page_res.pda;
	ncl_cmd->addr_param.common_param.info_list = evlog_page_res.info;

	ncl_cmd->du_format_no = DU_FMT_USER_4K;

	ncl_cmd_submit(ncl_cmd);

	return ncl_cmd->status;
}

/*!
 * @brief Flush all event log witn one page
 *
 * @return	not used
 */
void evlog_flush(evlog_mgr_t* mgr,u16 evt_reason_id)
{
	pda_t pda = INV_PDA;
	extern struct nvme_telemetry_ctrlr_initated_log *telemetry_ctrlr_data;

	if (!_evlog.flush_enable || _evlog.first_nand_log_blk_status == NAND_LOG_BLK_UNINITIALIZED)
		return;
    cpy_fun cpy_function;
    if(evt_reason_id != EVT_PLP_HANDLE_DONE)
    {
		cpy_function = memcpy_plp;
    }
    else
    {
		cpy_function = memcpy;
    }

	s32 offset = 0;
	u8 telemetry_trans_cnt = 0;
	while(mgr->ddr_flush_buf + offset < mgr->ddr_flush_buf_cur)
	{
        if((plp_trigger)&&(evt_reason_id != EVT_PANIC)&&(evt_reason_id != EVT_PLP_HANDLE_DONE)){
            //utils_apl_trace(LOG_ERR, 0, "plp return 3");
            break;
        }
		if(_evlog.first_nand_log_blk_status == NAND_LOG_BLK_READY_TO_WRITE_DATA && _evlog.next_flush_idx < _evlog.nr_pg_per_blk)
		{
			pda = get_evtb_page_pda(_evlog.blk_pda_base[0], _evlog.next_flush_idx);
		}
		else if(_evlog.first_nand_log_blk_status == NAND_LOG_BLK_NEED_TO_BE_ERASED)
		{
			evlog_next_index = _evlog.next_flush_idx = 0;
			pda = _evlog.blk_pda_base[0];
			erase_evtb(pda);
			_evlog.first_nand_log_blk_status = NAND_LOG_BLK_READY_TO_WRITE_DATA;
		}
		else
		{
			int i = 0;

			evlog_next_index = _evlog.next_flush_idx = 0;

			pda = _evlog.blk_pda_base[1];
			for(i = 1; i < 7; ++i)
				_evlog.blk_pda_base[i] = _evlog.blk_pda_base[i + 1];
			_evlog.blk_pda_base[7] = _evlog.blk_pda_base[0];
			_evlog.blk_pda_base[0] = pda;
			erase_evtb(pda);
			_evlog.first_nand_log_blk_status = NAND_LOG_BLK_READY_TO_WRITE_DATA;
		}

		sys_assert(pda != INV_PDA);
		dump_buf("write", 0);

		if(evt_reason_id == EVT_TELE && telemetry_trans_cnt<=0)
		{
			if(NULL ==  cpy_function((telemetry_ctrlr_data->runtime_log) + offset, mgr->ddr_flush_buf + offset, 11776))
				return;
			telemetry_trans_cnt++;
		}

		if(evt_reason_id != EVT_PLP_HANDLE_DONE)
		{
			if(NULL == cpy_function(_evlog.flush_buf, mgr->ddr_flush_buf + offset, NAND_PAGE_SIZE))
				return;
		}
		else
		{
			sync_dpe_copy((u32)mgr->ddr_flush_buf + offset, (u32)_evlog.flush_buf, NAND_PAGE_SIZE);
		}

		write_one_evtb_page(pda, _evlog.flush_buf);
		offset += NAND_PAGE_SIZE;
		_evlog.next_flush_idx++;
		evlog_next_index++;
		// evlog_printk(LOG_INFO, "next_flush_idx %x", _evlog.next_flush_idx);
	}

	// printk("panic_occure[0] = %d\n", panic_occure[0]);
	// printk("panic_occure[1] = %d\n", panic_occure[1]);
	// printk("panic_occure[2] = %d\n", panic_occure[2]);
	// printk("panic_occure[3] = %d\n", panic_occure[3]);
	#if (CO_SUPPORT_PANIC_DEGRADED_MODE == FALSE)
	if(panic_occure[0] == true)
		cpu_msg_issue(0, CPU_MSG_CPU1_PANIC, 0, 0);
	if(panic_occure[1] == true)
		cpu_msg_issue(1, CPU_MSG_CPU2_PANIC, 0, 0);
	if(panic_occure[3] == true)
		cpu_msg_issue(3, CPU_MSG_CPU4_PANIC, 0, 0);
	if(panic_occure[2] == true)
		loop_forever();
	#endif
}

#ifdef EVLOG_DUMP_RECONSTRUCTION_BUF
void dump_reconstruction_buf(pda_t evtb_pda_base, char *buf)
{
	int j = 0;
	printk("pda: %d:", (u32)evtb_pda_base);
	for(j = 0; j < NAND_PAGE_SIZE; ++j)
	{
		if(j % 16 == 0)
			printk("\n0x%x ", j);
		printk("%b ", buf[j]);
	}
	printk("\n");
}
#else
#define dump_reconstruction_buf(evtb_pda_base, buf)
#endif

static void evtbs_decide_blk_order(pda_t *evtb_pda_base, u32 signature)
{
	u32 i = 0, j = 0;
	nal_status_t ret;
	flush_id_t flush_id = {0, 0}, flush_id_arr[8] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
	pda_t pda_base[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	pda_t pda = 0;
	bool has_blk_been_used = false;

	ret = read_one_evtb_page(evtb_pda_base[0], evlog_dtag.dtag);
	if (ficu_du_data_good(ret) && (evlog_meta->meta0.signature == signature))
	{
		dump_reconstruction_buf(evtb_pda_base[0], _evlog.flush_buf);
		flush_id_arr[0].lo = evlog_meta->meta0.flush_id_lo;
		flush_id_arr[0].hi = evlog_meta->meta0.flush_id_hi;
		has_blk_been_used = true;
	}
	else
		flush_id_arr[0].lo = flush_id_arr[0].hi = 0;

	pda_base[0] = evtb_pda_base[0];

	for (i = 1; i < 8; i++)
	{
		ret = read_one_evtb_page(evtb_pda_base[i], evlog_dtag.dtag);
		if (ficu_du_data_good(ret) && (evlog_meta->meta0.signature == signature))
		{
			dump_reconstruction_buf(evtb_pda_base[i], _evlog.flush_buf);
			has_blk_been_used = true;
			flush_id.lo = evlog_meta->meta0.flush_id_lo;
			flush_id.hi = evlog_meta->meta0.flush_id_hi;
			if((flush_id.hi > flush_id_arr[0].hi) || (flush_id.hi == flush_id_arr[0].hi && flush_id.lo > flush_id_arr[0].lo))
			{
				flush_id_arr[i].lo = flush_id_arr[0].lo;
				flush_id_arr[i].hi = flush_id_arr[0].hi;
				pda_base[i] = pda_base[0];
				flush_id_arr[0].lo = flush_id.lo;
				flush_id_arr[0].hi = flush_id.hi;
				pda_base[0] = evtb_pda_base[i];
			}
			else
			{
				for(j = i - 1; j >= 1; --j)
				{
					if(((flush_id_arr[j].hi < flush_id.hi) || (flush_id_arr[j].hi == flush_id.hi && flush_id_arr[j].lo <= flush_id.lo)))
						break;
					flush_id_arr[j + 1].lo = flush_id_arr[j].lo;
					flush_id_arr[j + 1].hi = flush_id_arr[j].hi;
					pda_base[j + 1] = pda_base[j];
				}
				flush_id_arr[j + 1].lo = flush_id.lo;
				flush_id_arr[j + 1].hi = flush_id.hi;
				pda_base[j + 1] = evtb_pda_base[i];
			}
		}
		else
		{
			for(j = i; j >= 2; --j)
			{
				pda_base[j] = pda_base[j - 1];
				flush_id_arr[j].lo = flush_id_arr[j - 1].lo;
				flush_id_arr[j].hi = flush_id_arr[j - 1].hi;
			}
			pda_base[1] = evtb_pda_base[i];
			flush_id_arr[1].lo = flush_id_arr[1].hi = 0;
		}
	}

	memcpy((void *)evtb_pda_base, pda_base, sizeof(pda_base));
	if(!has_blk_been_used)
	{
		// _evlog.oldest_flush_id = 0;
		_evlog.newest_flush_id = 1;
		evlog_next_index = _evlog.next_flush_idx = 0;
	}
	else
	{
		if(_fg_warm_boot)
		{
			if((gFtlMgr.evlog_index == 0)||(gFtlMgr.evlog_index > _evlog.nr_pg_per_blk))
			{	
				evlog_next_index = _evlog.next_flush_idx = _evlog.nr_pg_per_blk;
			}
			else
			{
				evlog_next_index = _evlog.next_flush_idx = gFtlMgr.evlog_index - 1;
			}
		}
		else
		{

			_evlog.next_flush_idx = evtb_decide_latest_page(evtb_pda_base[0], 0, _evlog.nr_pg_per_blk, EVLOG_SIG, evlog_dtag);
			evlog_next_index = _evlog.next_flush_idx;

		}
		pda = get_evtb_page_pda(evtb_pda_base[0], _evlog.next_flush_idx);
		read_one_evtb_page(pda, evlog_dtag.dtag);
		_evlog.newest_flush_id = ((evlog_meta->meta0.flush_id_hi << 16 ) << 16);
		_evlog.newest_flush_id += evlog_meta->meta0.flush_id_lo;
		_evlog.newest_flush_id++;
		// _evlog.oldest_flush_id = ((flush_id_arr[2].hi << 16) << 16) + flush_id_arr[2].lo;
		if(!(_evlog.next_flush_idx < _evlog.nr_pg_per_blk))
		{
			pda = _evlog.blk_pda_base[1];
			for(i = 1; i < 7; ++i)
				_evlog.blk_pda_base[i] = _evlog.blk_pda_base[i + 1];
			_evlog.blk_pda_base[7] = _evlog.blk_pda_base[0];
			_evlog.blk_pda_base[0] = pda;
			evlog_next_index = _evlog.next_flush_idx = 0;
		}
		else
		{
			_evlog.next_flush_idx++;
			evlog_next_index++;
		}
	}
	_evlog.first_nand_log_blk_status = NAND_LOG_BLK_READY_TO_WRITE_DATA;
}

/*!
 * @brief event log reconstruction
 *
 * @return	true if reconstruction success
 */
bool evlog_reconstruction(void)
{
	log_level_t old = ipc_api_log_level_chg(LOG_ALW);

	evtbs_decide_blk_order(_evlog.blk_pda_base, EVLOG_SIG);

	ipc_api_log_level_chg(old);

	return true;
}

#include "nand.h"

#ifdef EVLOG_DUMP_EVLOG
void dump_evlog()
{
	int i = 0;
	printk("=== dump evlog ===\n");
	printk("nr_pg_per_blk = %d\n", _evlog.nr_pg_per_blk);
	printk("buf = 0x%x\n", _evlog.flush_buf);
	printk("evlog_meta = 0x%x\n", evlog_meta);
	printk("buf_size = %d\n", _evlog.flush_buf_size);
	printk("itlv_du_cnt = %d\n", _evlog.itlv_du_cnt);
	printk("newest_id = 0x%x-%x\n", ((u32) (_evlog.newest_flush_id >> 16) >> 16), (u32) _evlog.newest_flush_id);
	// printk("oldest_id = 0x%x-%x\n", ((u32) (_evlog.oldest_flush_id >> 16) >> 16), (u32) _evlog.oldest_flush_id);
	printk("first_nand_log_blk_status = %d\n", _evlog.first_nand_log_blk_status);
	printk("next_flush_idx = %d\n", _evlog.next_flush_idx);
	for(i = 0; i < 8; ++i)
		printk("blk_pda_base[%d] = 0x%x\n", i, _evlog.blk_pda_base[i]);
	printk("flush_enable = %d\n", _evlog.flush_enable);

	printk("=== dump evlog ===\n");
}
#else
#define dump_evlog()
#endif

/*!
 * @brief event log initialization
 *
 * @return	not used
 */
init_code void evlog_init(void)
{
    //utils_apl_trace(LOG_ALW, 0, "[IN] evlog_init");
	// initial _evlog data structure
	memset(&_evlog, 0, sizeof(evlog_t));
	_evlog.blk_pda_base = blk_pda_base;
	_evlog.blk_pda_base[0] = 0x8;//544;
	_evlog.blk_pda_base[1] = 0xC;//552;
	_evlog.blk_pda_base[2] = 0x18;//608;
	_evlog.blk_pda_base[3] = 0x1C;//616;
	_evlog.blk_pda_base[4] = 0x28;//548;
	_evlog.blk_pda_base[5] = 0x2C;//556;
	_evlog.blk_pda_base[6] = 0x38;//612;
	_evlog.blk_pda_base[7] = 0x3C;//620;

	_evlog.newest_flush_id = 0;
	// _evlog.oldest_flush_id = 0;
	_evlog.next_flush_idx = 0;
	_evlog.nr_pg_per_blk = shr_nand_info.geo.nr_pages / shr_nand_info.bit_per_cell;
	_evlog.flush_buf = (void *)&__evlog_dtag_start;
	_evlog.flush_buf_size = EVLOG_FLUSH_BUF_SZE;

	evlog_dtag.dtag = mem2sdtag((void *)&__evlog_dtag_start);
	evlog_meta = (evlog_meta_t *) (&((evlog_du0_meta_t *)shr_dtag_meta)[mem2sdtag(&__evlog_dtag_start)]);
	memset(evlog_meta, 0, sizeof(evlog_meta_t));

	//utils_apl_trace(LOG_ALW, 0xa873,"meta idx:%d buf_start:0x%x mgr_buf_start:0x%x",
	//	mem2sdtag(&__evlog_dtag_start),(u32)&__evlog_dtag_start,_evlog_mgr.ddr_flush_buf);
	sys_assert(evlog_dtag.dtag != _inv_dtag.dtag);
	sys_assert(EVLOG_FLUSH_BUF_SZE == ((u32)&__evlog_dtag_end - (u32)&__evlog_dtag_start));

	_evlog.itlv_du_cnt = (shr_nand_info.interleave << DU_CNT_SHIFT);
	_evlog.first_nand_log_blk_status = NAND_LOG_BLK_UNINITIALIZED;

	_evlog.flush_enable = evlog_reconstruction();

	dump_evlog();
	//M.2 flush 3 page
	evlog_byte_limit[0] = 4 * DTAG_SZE - 32 * 2;
	evlog_byte_limit[1] = 3 * DTAG_SZE - 32 * 2;
	evlog_byte_limit[2] = 3 * DTAG_SZE - 32 * 2;
	evlog_byte_limit[3] = 2 * DTAG_SZE - 32 * 2;

	// initial evlog manager
	mgr_init();
}

/*! \cond PRIVATE */
module_init(evlog_init, ELOG_APL);
/*! \endcond */
#endif //#if (CPU_ID == EVLOG_MGR_CPU)
void agt_buf_flush_to_uart_all(u32 param, u32 payload, u32 curr_buf_idx)
{
	evlog_agt_buf_t *buf = &(agt->buf[curr_buf_idx]);
	flush_to_uart(buf, LOG_FLUSH_ABSOLUTE, buf->uart_data_byte_cnt);
	evt_trigger_cnt->complete_uart[CPU_ID_0] = true;
}

void agt_buf_flush_to_uart_all2()
{
	#if(CPU_ID == 1 || CPU_ID == 4)
    //DIS_ISR();
	//BEGIN_CS1
	 u32 flags = irq_save();
    #endif
	if (!agt->initialized || evt_triggered_cnt()) // || log_isr == LOG_IRQ_DO)
	{
		#if(CPU_ID == 1 || CPU_ID == 4)
   		//EN_ISR();
		//END_CS1
		irq_restore(flags);
    	#endif
		return;
	}

	agt_buf_flush_to_uart_all(0, 0, agt->curr_buf_idx);
	#if(CPU_ID == 1 || CPU_ID == 4)
    //EN_ISR();
	//END_CS1
	irq_restore(flags);
    #endif
}


void agt_buf_flush_to_uart_regularly(u32 param, u32 payload, u32 curr_buf_idx)
{
	//if(log_isr == LOG_IRQ_DO)
	//	return;
	#if(CPU_ID == 1 || CPU_ID == 4)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	evlog_agt_buf_t *buf = &(agt->buf[curr_buf_idx]);
	if(buf->uart_data_byte_cnt > MAX_BYTE_CNT_OF_A_LOG)
		flush_to_uart(buf, LOG_FLUSH_EXPECT, MAX_BYTE_CNT_OF_A_LOG);
	else
		flush_to_uart(buf, LOG_FLUSH_ABSOLUTE, MAX_BYTE_CNT_OF_A_LOG);


	if(buf->uart_data_byte_cnt && evt_evlog_regularly_flush_uart != 0xFF)
		evt_set_cs(evt_evlog_regularly_flush_uart, 0, curr_buf_idx, CS_TASK);
    #if(CPU_ID == 1 || CPU_ID == 4)
    //EN_ISR();
	END_CS1
    #endif
}

#ifdef EVLOG_DUMP_AGT
void dump_agt()
{
	int i = 0;
	evlog_agt_buf_t *buf = NULL;
	printk("init = %d\n", agt->initialized);
	printk("evt_triggered = %d\n", agt->evt_triggering);
	for(i = 0; i < ARRAY_SIZE(agt->buf); ++i)
	{
		buf = &(agt->buf[i]);
		printk("buf%d:\n", i);
		printk("  ptr = 0x%x\n", buf->ptr);
		printk("  buf_byte_size = %d\n", buf->buf_byte_size);
		printk("  byte_wptr = %d\n", buf->byte_wptr);
		printk("  byte_rptr = %d\n", buf->byte_rptr);
		printk("  uart_byte_rptr = %d\n", buf->uart_byte_rptr);
		printk("  uart_data_byte_cnt = %d\n", buf->uart_data_byte_cnt);
		printk("  log_tag = 0x%x\n", buf->log_tag);
		printk("  curr_tag = %d\n", buf->curr_tag);
	}
	printk("curr_buf_idx = %d\n", agt->curr_buf_idx);
}
#else
#define dump_agt()
#endif
#if 0
static  bool is_evt_triggered()
{
	int i = 0;
	for(i = 0; i < MPC; ++i)
	{
		if(evt_trigger_cnt->trigger[i] != evt_trigger_cnt->complete_flush[i])
			return true;
	}
	return false;
}

static  bool are_two_evt_triggered()
{
	int i = 0;
	int trigger_cnt = 0;
	for(i = 0; i < MPC; ++i)
	{
		if(evt_trigger_cnt->trigger[i] > evt_trigger_cnt->complete_flush[i])
			trigger_cnt += evt_trigger_cnt->trigger[i] - evt_trigger_cnt->complete_flush[i];
	}
	return trigger_cnt >= 2;
}
#endif
static u32 evt_triggered_cnt()
{
	int i = 0;
	int trigger_cnt = 0;
	for(i = 0; i < MPC; ++i)
	{
		if(evt_trigger_cnt->trigger[i] > evt_trigger_cnt->complete_flush[i])
			trigger_cnt += evt_trigger_cnt->trigger[i] - evt_trigger_cnt->complete_flush[i];
	}
	return trigger_cnt;
}

static u32 buf_need_swap_cnt()
{
	int i = 0;
	int swap_cnt = 0;
	for(i = 0; i < MPC; ++i)
	{
		if(evt_trigger_cnt->trigger[i] > evt_trigger_cnt->complete_buf_swap[i])
			swap_cnt+=evt_trigger_cnt->trigger[i] - evt_trigger_cnt->complete_buf_swap[i];
	}
	return swap_cnt;
}

// the internal strategy to flush uart
static void agt_buf_flush_to_uart_default(u32 param, u32 payload, u32 pl)
{
	//if(log_isr == LOG_IRQ_DO)
		//return;
	#if(CPU_ID == 1 || CPU_ID == 4)
	//DIS_ISR();
	BEGIN_CS1
    #endif
	u16 curr_buf_idx = PL_TO_CURR_BUF_IDX(pl);
	u16 suggest_byte_cnt_to_flush = PL_TO_SUGGEST_BYTE_CNT(pl);
	evlog_agt_buf_t *buf = &(agt->buf[curr_buf_idx]);

	if(evt_triggered_cnt() == 0 || evt_trigger_cnt->complete_uart[CPU_ID_0])
	{
        #if 0
		if(log_isr == LOG_IRQ_REST)
		{
            sys_assert(0);
			agt_buf_flush_to_uart_all2();
			log_isr = LOG_IRQ_DOWN;
			if(buf->uart_data_byte_cnt && evt_evlog_regularly_flush_uart != 0xFF)
				evt_set_cs(evt_evlog_regularly_flush_uart, 0, curr_buf_idx, CS_TASK);
            #if(CPU_ID == 1 || CPU_ID == 4)
            //EN_ISR();
			END_CS1
            #endif
			return;
		}
        #endif
		flush_to_uart(buf, LOG_FLUSH_ABSOLUTE, suggest_byte_cnt_to_flush / 2);
		flush_to_uart(buf, LOG_FLUSH_EXPECT, max(buf->uart_data_byte_cnt / 2, MAX_BYTE_CNT_OF_A_LOG));
		if(buf->uart_data_byte_cnt && evt_evlog_regularly_flush_uart != 0xFF)
			evt_set_cs(evt_evlog_regularly_flush_uart, 0, curr_buf_idx, CS_TASK);
	}
	else
	{
		agt_buf_flush_to_uart_all(0, 0, (curr_buf_idx + 1) & 1);
		if(evt_evlog_default_flush_uart != 0xFF)
		evt_set_cs(evt_evlog_default_flush_uart, 0,	DEFAULT_FLUSh_PL(curr_buf_idx, suggest_byte_cnt_to_flush), CS_TASK);
	}
    #if(CPU_ID == 1 || CPU_ID == 4)
    //EN_ISR();
	END_CS1
    #endif
}

/*!
 * @brief event log agent initialization
 *
 * @return	not used
 */
init_code void evlog_agt_init(void)
{
	dump_agt();
	evt_register(agt_buf_flush_to_uart_regularly, 0, &evt_evlog_regularly_flush_uart);
	evt_register(agt_buf_flush_to_uart_default, 0, &evt_evlog_default_flush_uart);

	// printk("EVLOG_AGT_BUF_SZ = %x\n", EVLOG_AGT_BUF_SZ);
	// printk("&__evlog_flush_buf_end = %x, &__evlog_flush_buf_start = %x, sizeof(*(u8 (*)[2][EVLOG_AGT_BUF_SZ])&__evlog_flush_buf_start) = %x\n", &__evlog_flush_buf_end, &__evlog_flush_buf_start, sizeof(*(u8 (*)[2][EVLOG_AGT_BUF_SZ])&__evlog_flush_buf_start));
	// printk("&__evlog_log_tag_end = %x, &__evlog_log_tag_start = %x, sizeof(*(u8 (*)[2][EVLOG_AGT_LOG_STRUCT_CNT])&__evlog_log_tag_start) = %x\n", &__evlog_log_tag_end, &__evlog_log_tag_start, sizeof(*(u8 (*)[2][EVLOG_AGT_LOG_STRUCT_CNT])&__evlog_log_tag_start));
	sys_assert((u32)&__evlog_log_tag_end - (u32)&__evlog_log_tag_start == MPC * sizeof(*(u8 (*)[2][EVLOG_AGT_LOG_STRUCT_CNT])&__evlog_log_tag_start));

#if CPU_ID == 1
	cpu_msg_register(CPU_MSG_CPU1_PANIC, ipc_panic);
#elif CPU_ID == 2
	cpu_msg_register(CPU_MSG_CPU2_PANIC, ipc_panic);
#elif CPU_ID == 4
	cpu_msg_register(CPU_MSG_CPU4_PANIC, ipc_panic);
#endif
}

#if (CPU_ID == EVLOG_MGR_CPU)

#if (PLP_SUPPORT == 1)
slow_code void* memcpy_fast(void *dest, void *src, unsigned long byte_cnt, evlog_agt_buf_t* buf)
{
	//use sync dpe copy speed up memcpy , need 32 byte aligned
	u32 addr_start = (u32)src;
	u32 addr_ofst;
	u32 done_length = 0;
	void* dest_ptr = dest;
	void* src_ptr  = src;
	//eg:  src->  16byte ||----32 byte align ------|| 8byte
	//			  memcpy		sync_dpe_cpy		  memcpy

	//step1: adjust src and dest 32 byte align
	if((addr_start & 0x1F) != 0 || (byte_cnt < 32))
	{
		addr_ofst = addr_start&0x1F;

		if((32 - addr_ofst) <= byte_cnt)
			done_length = 32 -addr_ofst;
		else
			done_length = byte_cnt;

		//memcpy(dest_ptr, src_ptr, done_length);
		memset(dest_ptr,0x00,done_length);//abort it , otherwise dump TT log may error  Jay 240923

		dest_ptr = ptr_inc(dest_ptr, done_length);
		src_ptr  = ptr_inc(src_ptr, done_length);
		if(done_length == byte_cnt)
			return dest_ptr;	
	}
	sys_assert(((u32)src_ptr & 0x1F) == 0x0);

	if(((u32)dest_ptr & 0x1F) != 0x0)
	{
		addr_ofst = 32 -  ((u32)dest_ptr&0x1F)  ;
		memset(dest_ptr,0x00,addr_ofst);

		//utils_apl_trace(LOG_ALW, 0x76ce,"pre addr:0x%x cur addr:0x%x addr_ofst:%d",(u32)dest_ptr,(u32)dest_ptr + addr_ofst,addr_ofst);
		dest_ptr = ptr_inc(dest_ptr, addr_ofst);
	}
	sys_assert(((u32)dest_ptr & 0x1F) == 0x0);

	//step2: sync dpe copy 32 align
	u32 need_cpy_byte_cnt = byte_cnt - done_length;

	need_cpy_byte_cnt &= ~(0x20 - 1);//32 byte align
	#if 1
	// reset first log tag  Jay 240923
	if(buf != NULL && need_cpy_byte_cnt > sizeof(log_buf_t) * 8)
	{
		u32 buf_cur_wptr = (u32)src_ptr - (u32)buf->ptr;
		u8 first_log_tag = buf->log_tag[buf_cur_wptr / sizeof(log_buf_t)];
		u8 next_log_tag;
		u32 skip_size = 0;
		do{
			buf_cur_wptr += sizeof(log_buf_t);//8byte
			next_log_tag  = buf->log_tag[buf_cur_wptr / sizeof(log_buf_t)];
			if(first_log_tag != next_log_tag)
			{
				//reset first log tag , avoid TT dump log fail
				memset(src_ptr,0x00,skip_size);
				break;
			}
			skip_size += sizeof(log_buf_t);
		}while(skip_size < sizeof(log_buf_t) * 8);
	}
	#endif

	sync_dpe_copy((u32)src_ptr, (u32)dest_ptr, need_cpy_byte_cnt);

	dest_ptr = ptr_inc(dest_ptr, need_cpy_byte_cnt);
	src_ptr  = ptr_inc(src_ptr, need_cpy_byte_cnt);
	done_length += need_cpy_byte_cnt;

	//step3: last unalgin 32byte data
	if(byte_cnt - done_length != 0)
	{
		u32 last_unalign_data = byte_cnt - done_length;
		memcpy(dest_ptr, src_ptr, last_unalign_data);
		dest_ptr = ptr_inc(dest_ptr, last_unalign_data);
		src_ptr  = ptr_inc(src_ptr, last_unalign_data);
		done_length += last_unalign_data;
	}

	sys_assert(done_length == byte_cnt);
	return dest_ptr;
}
#endif

/*!
 * @brief receive ipc message to alert manager to copy
 *
 * @param req	ipc msg request
 *
 * @return	not used
 */
void ipc_mgr_flush_to_nand(volatile cpu_msg_req_t *req)
{
	evt_set_cs(evt_evlog_flush, req->cmd.flags, req->pl, CS_NOW);
}

void ipc_clear_nand_log_block_and_reset(volatile cpu_msg_req_t *req)
{
	// evlog_printk(LOG_ERR, "[%s][%d] CPU%d\n", __FUNCTION__, __LINE__, CPU_ID);
	evt_set_cs(evt_evlog_erase_and_reset, 0, req->pl, CS_NOW);
}


/*!
 * @brief event log manager flush
 *
 * @return	not used
 */

//extern u8 plp_trigger;
void mgr_flush(u16 evt_reason_id)
{
	evlog_mgr_t* mgr = &_evlog_mgr;

	evlog_flush(mgr,evt_reason_id);
	//if(plp_trigger)
	//	return;

    if (in_panic) {
	    mgr_spi_nor(mgr); // Jimmitt 2021.4.23
	    in_panic = false;
    }

	memset_plp(mgr->ddr_flush_buf, 0, mgr->ddr_buf_byte_size);

	mgr->ddr_flush_buf_cur = mgr->ddr_flush_buf;
	extern bool save_evlog_start;
	save_evlog_start = false;
}

static u32 agt_buf_wait_flush_byte_cnt(evlog_agt_buf_t *buf, u32 mgr_log_buf_budget)
{
	if(agt_buf_data_byte_cnt(buf) == 0)
		return 0;
	u32 i = 0, data_len = 0;
	s32 start_idx = buf->byte_rptr / sizeof(log_buf_t), end_idx = -1;
	u8 cur_tag = buf->log_tag[start_idx];
	bool is_end_idx_updated = false;
	int budget =  min((buf->byte_rptr > buf->byte_wptr? buf->buf_byte_size - buf->byte_rptr: buf->byte_wptr - buf->byte_rptr) / sizeof(log_buf_t), mgr_log_buf_budget);
	mgr_log_buf_budget -= budget;
	for(i = start_idx; i < EVLOG_AGT_LOG_STRUCT_CNT; ++i)
	{
		budget--;
		if(!budget)
		{
			data_len = (i + 1 - start_idx) * sizeof(log_buf_t);
			if(buf->byte_rptr + data_len == buf->byte_wptr)
				return data_len;
			if(mgr_log_buf_budget == 0)
			{
				if(cur_tag == buf->log_tag[(i + 1 != EVLOG_AGT_LOG_STRUCT_CNT)? i + 1: 0])
					return (end_idx != -1)? (end_idx + 1 - start_idx) * sizeof(log_buf_t): 0;
				else
					return data_len;
			}
			break;
		}
		if(cur_tag == buf->log_tag[i])
			continue;
		end_idx = i - 1;
		cur_tag = buf->log_tag[i];
	}
	budget = min(buf->byte_wptr / sizeof(log_buf_t), mgr_log_buf_budget);
	mgr_log_buf_budget -= budget;
	is_end_idx_updated = false;
	for(i = 0; i < EVLOG_AGT_LOG_STRUCT_CNT; ++i)
	{
		budget--;
		if(!budget)
		{
			if((i + 1) * sizeof(log_buf_t) == buf->byte_wptr)
				return data_len + (i + 1) * sizeof(log_buf_t);
			if(!mgr_log_buf_budget)
			{
				if(cur_tag == buf->log_tag[i + 1])
				{
					if(is_end_idx_updated == true)
						return data_len + (end_idx + 1) * sizeof(log_buf_t);
					else
						return (end_idx + 1 - start_idx) * sizeof(log_buf_t);
				}
				else
					return data_len + (i + 1) * sizeof(log_buf_t);
			}
		}
		if(cur_tag == buf->log_tag[i])
			continue;
		if(i > 0)
		{
			end_idx = i - 1;
			is_end_idx_updated = true;
		}
		else
			end_idx = EVLOG_AGT_LOG_STRUCT_CNT - 1;
		cur_tag = buf->log_tag[i];
	}

	return 0;
}

ddr_data_ni u32 u32byte_cnt[MPC];
ddr_code void mgr_spi_nor(evlog_mgr_t* mgr)
{
    //u32 u32idx;
//	evlog_mgr_t* mgr = &_evlog_mgr;
	spi_savelog_info_t spi_savelog_info;

	u8 fail = 0;

	//printk("Save log to SPI-NOR!!!\n");


	//SPI Erase
	for(u32 EraseIdx=0; EraseIdx<40; EraseIdx++){
		//spi_nor_erase((EraseIdx*SPI_BLOCK_SIZE)+(MpcIdx*SPI_LOG_SIZE));
		if(spi_nor_erase(EraseIdx*SPI_BLOCK_SIZE) != 0){
			fail |= 0x1;
			//printk("SPI-Erase Fail!!!\n");
		}
	}

	//SPI Write
	spi_savelog_info.b.signature = 0x535049;
	spi_savelog_info.b.Cpu_ID_n = 0x00;

	if(spi_nor_write(0, spi_savelog_info.all) != 0){
		fail |= 0x2;
		//printk("SPI-Write Fail!!!\n");
	}
	for(u32 MpcIdx=0; MpcIdx < MPC; MpcIdx++){
		if(spi_nor_write(4 +(4*MpcIdx), u32byte_cnt[MpcIdx]) != 0){
			fail |= 0x4;
			//printk("SPI-Write Fail!!!\n");
		}
	}

	#if 0
    printk("===================================================================\n");
    for(u32idx=0; u32idx<u32byte_cnt[3]; u32idx+=4){
		printk("%x",(u32)readl(mgr->ddr_flush_buf +u32idx));
		if(u32idx%16==0){ printk("\n"); }//1024
    }
    u32idx = 0;
    #endif

	for(u32 AddrIdx = 0; AddrIdx < u32byte_cnt[3]; AddrIdx=AddrIdx+4){
		if(spi_nor_write((20+AddrIdx), (u32)readl(mgr->ddr_flush_buf +AddrIdx)) != 0){
			fail |= 0x8;
			//printk("SPI-Write Fail!!!\n");
		}

	}
	//printk("NOR log sts %x !\n", fail);
	evlog_printk(LOG_ALW, "SPI %x\n",fail);

}

#if (PLP_SUPPORT == 1)
slow_code void mgr_plp_copy_evlog(evlog_agt_buf_t *buf ,u32 flush_byte_cnt, u32 max_flush_byte)
{
	u64 cost = get_tsc_64();
	u32 byte_wptr = buf->byte_wptr;
	u32 byte_rptr = buf->byte_rptr;
	u32 done_length = 0;
	bool rptr_chk = false;
	u32 cpu_buf_byte[2];
	evlog_mgr_t* mgr = &_evlog_mgr;
	u8 cpy_type = 0;
	u32 byte_cpy;

	if(byte_wptr < byte_rptr)
	{
		cpu_buf_byte[0] =  buf->buf_byte_size - byte_rptr;
		rptr_chk = true;
	}
	cpu_buf_byte[1] = byte_wptr;

	if(flush_byte_cnt <= max_flush_byte)//just flush all log
	{
		if(rptr_chk)
		{
			byte_cpy = cpu_buf_byte[0];
			mgr->ddr_flush_buf_cur = memcpy_fast(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, byte_rptr), byte_cpy, buf);
			done_length += byte_cpy;
			//mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cpy);
			byte_rptr += done_length;
			sys_assert(byte_rptr == buf->buf_byte_size);
			//if (byte_rptr == buf->buf_byte_size)
				byte_rptr = 0;
			cpy_type = 1;
		}
		byte_cpy = byte_wptr - byte_rptr;
		mgr->ddr_flush_buf_cur = memcpy_fast(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, byte_rptr), byte_cpy, buf);
		//mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cpy);
		done_length += byte_cpy;
		//sys_assert(done_length == flush_byte_cnt);
		cpy_type += 1;
	}
	else 
	{
		if(rptr_chk)
		{
			if(cpu_buf_byte[1] >= max_flush_byte)//copy partial wptr , start from wptr - max_flush_byte 
			{
				u32 ptr = cpu_buf_byte[1] - max_flush_byte;
				byte_cpy = max_flush_byte;
				mgr->ddr_flush_buf_cur = memcpy_fast(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, ptr), byte_cpy, buf);//ptr = 
				done_length += byte_cpy;
				//mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cpy);
				cpy_type = 3;
			}
			else//copy all wptr + partial rptr
			{
				byte_cpy = max_flush_byte - cpu_buf_byte[1];
				u32 ptr = buf->buf_byte_size - byte_cpy;
				sys_assert(byte_cpy < cpu_buf_byte[0]);
				mgr->ddr_flush_buf_cur = memcpy_fast(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, ptr), byte_cpy, buf);
				done_length += byte_cpy;
				//mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cpy);

				byte_cpy = cpu_buf_byte[1];
				mgr->ddr_flush_buf_cur = memcpy_fast(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, 0),byte_cpy, buf);
				done_length += byte_cpy;
				//mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cpy);
				cpy_type = 4;
			}
		}
		else//copy partial wptr , start from wptr - max_flush_byte 
		{
			u32 ptr = cpu_buf_byte[1] - max_flush_byte;
			byte_cpy = max_flush_byte;
			mgr->ddr_flush_buf_cur = memcpy_fast(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, ptr), byte_cpy, buf);
			done_length += byte_cpy;
			//mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cpy);
			cpy_type = 5;
		}
	}	
	//sys_assert((byte_cpy & 0x7) == 0); need consider evlog_printk 
	buf->byte_rptr = buf->byte_wptr;
	utils_apl_trace(LOG_DEBUG, 0x5411,"copy type:%d  done_length:%d flush_byte_cnt:%d max:%d cost:%d us buf_ptr:0x%x",
		cpy_type,done_length,flush_byte_cnt,max_flush_byte,time_elapsed_in_us(cost),(u32)mgr->ddr_flush_buf_cur);
}
#endif
/*!
 * @brief event log manager copy handler
 *
 * @param p0	not used
 * @param p1	not used
 * @param pl	Event id and CPU id which trigger this event
 *
 * @return	not used
 */
 fast_code void mgr_copy_and_flush_to_nand(u32 p0, u32 p1, u32 p2)
{
	extern bool save_evlog_start;
	save_evlog_start = true;
	//extern struct nvme_telemetry_ctrlr_initated_log *telemetry_ctrlr_data;
	u32 cpu_id_0 = p2 - 1;
	cpu_flush_info_t cpu_flush_info[MPC] = {0};
	cpu_flush_info_t tmp = {0};
	evlog_agt_buf_t *buf = NULL;

	evlog_mgr_t* mgr = &_evlog_mgr;
	evlog_agt_t* pagt = NULL;
	u32 budget = EVLOG_DDR_BUF_SZE;
	u32 cid = 0;
	u32 i = 0, j = 0;
	u32 byte_wptr = 0;
	u32 byte_rptr = 0;
	u32 byte_cnt = 0;
    u16 evt_reason_id = p1;
    cpy_fun cpy_function;
    if(evt_reason_id != EVT_PLP_HANDLE_DONE)
    {
		cpy_function = memcpy_plp;
    }
    else
    {
		cpy_function = memcpy;
    }

	if((buf_need_swap_cnt()==0) && (evt_triggered_cnt()>=2))
		return;

	mgr->ddr_flush_buf_cur = mgr->ddr_flush_buf;

	for(i = 0; i < MPC; ++i)
		cpu_flush_info[i].cpu_id_0 = i;

	spin_lock_take(SPIN_LOCK_KEY_EVLOG, 0, true);
	for(i = 0; i < MPC; ++i)
	{
		pagt = &mgr->agt[cpu_flush_info[i].cpu_id_0];
		buf = &(pagt->buf[pagt->curr_buf_idx]);
		cpu_flush_info[i].flush_buf_idx = pagt->curr_buf_idx;
		pagt->curr_buf_idx = (pagt->curr_buf_idx + 1) & 1;
	}
	evt_trigger_cnt->complete_buf_swap[cpu_id_0]++;
	spin_lock_release(SPIN_LOCK_KEY_EVLOG);
	//if(log_isr != LOG_IRQ_DO)
	//{
	//	agt_buf_flush_to_uart_all(0, 0, cpu_flush_info[CPU_ID_0].flush_buf_idx);
	//}
    if((plp_trigger)&&(evt_reason_id != EVT_PANIC)&&(evt_reason_id != EVT_PLP_HANDLE_DONE)){
        evt_trigger_cnt->complete_copy[cpu_id_0]++;
        evt_trigger_cnt->complete_flush[cpu_id_0]++;
        //utils_apl_trace(LOG_ERR, 0, "plp return 1");
        return;
    }

	for(i = 0; i < MPC; ++i)
	{
		pagt = &mgr->agt[cpu_flush_info[i].cpu_id_0];
		buf = &(pagt->buf[cpu_flush_info[i].flush_buf_idx]);
		cpu_flush_info[i].byte_cnt = agt_buf_wait_flush_byte_cnt(buf, min(budget, EVLOG_DDR_BUF_SZE / 4) / sizeof(log_buf_t));
		budget -= cpu_flush_info[i].byte_cnt;
	}

	if(cpu_id_0 != 3)
		SWAP_X_Y(cpu_flush_info[cpu_id_0], cpu_flush_info[3],tmp);
	for(j = 0; j < 2; ++j)
	{
		if(cpu_flush_info[j].byte_cnt < cpu_flush_info[j+1].byte_cnt)
			SWAP_X_Y(cpu_flush_info[j], cpu_flush_info[j+1],tmp);
	}
	if(cpu_flush_info[0].byte_cnt < cpu_flush_info[1].byte_cnt)
		SWAP_X_Y(cpu_flush_info[0], cpu_flush_info[1],tmp);

	for (cid = 0; cid < MPC; cid++)
	{
		for (i = 0; i < MPC; i++)
		{
			if(cpu_flush_info[i].cpu_id_0 == cid)
				break;
		}
		if(cpu_flush_info[i].byte_cnt == 0)
			continue;
		pagt = &mgr->agt[cpu_flush_info[i].cpu_id_0];
		//evlog_printk(LOG_ALW, "albert agt : %d,%d\n",i ,cpu_flush_info[i].cpu_id_0);
		buf = &(pagt->buf[cpu_flush_info[i].flush_buf_idx]);
        if((plp_trigger)&&(evt_reason_id != EVT_PANIC)&&(evt_reason_id != EVT_PLP_HANDLE_DONE)){
        plp_return:
            evt_trigger_cnt->complete_copy[cpu_id_0]++;
            evt_trigger_cnt->complete_flush[cpu_id_0]++;
            #if !(MDOT2_SUPPORT == 1)
            memset(mgr->ddr_flush_buf, 0, (mgr->ddr_flush_buf_cur-mgr->ddr_flush_buf));
            #endif
            //utils_apl_trace(LOG_ERR, 0xac46, "plp return 222222222222222222222222222222");
    	    mgr->ddr_flush_buf_cur = mgr->ddr_flush_buf;
            return;
        }

		#if (PLP_SUPPORT == 1)
		if( evt_reason_id == EVT_PLP_HANDLE_DONE)
		{
			mgr_plp_copy_evlog(buf,cpu_flush_info[i].byte_cnt,evlog_byte_limit[cid]);
		}
		else
		{
		#endif
			byte_wptr = buf->byte_wptr;
			byte_rptr = buf->byte_rptr;

			if (byte_wptr < byte_rptr)
			{
				byte_cnt = min(buf->buf_byte_size - byte_rptr, cpu_flush_info[i].byte_cnt);

				if(NULL == cpy_function(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, byte_rptr), byte_cnt))
					goto plp_return;
				mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cnt);
				cpu_flush_info[i].byte_cnt -= byte_cnt;
				byte_rptr += byte_cnt;
				if (byte_rptr == buf->buf_byte_size)
					byte_rptr = 0;
			}

			if (cpu_flush_info[i].byte_cnt && byte_wptr > byte_rptr)
			{
				byte_cnt = min(byte_wptr - byte_rptr, cpu_flush_info[i].byte_cnt);
				if(NULL == cpy_function(mgr->ddr_flush_buf_cur, ptr_inc(buf->ptr, byte_rptr), byte_cnt))
					goto plp_return;
				mgr->ddr_flush_buf_cur = ptr_inc(mgr->ddr_flush_buf_cur, byte_cnt);
				cpu_flush_info[i].byte_cnt -= byte_cnt;
				byte_rptr += byte_cnt;
			}
			u32byte_cnt[cid] = mgr->ddr_flush_buf_cur - mgr->ddr_flush_buf;
			buf->byte_rptr = byte_rptr;
			sys_assert(cpu_flush_info[i].byte_cnt == 0);
		#if (PLP_SUPPORT == 1)
		}
		#endif
	} // End for (i = 0; i < MPC; i++)
	evt_trigger_cnt->complete_copy[cpu_id_0]++;

#if 0 // test by Jimmitt
    if(!plp_trigger){
        for(u32 u32idx=0; u32idx<u32byte_cnt[3]; u32idx+=4){//u32byte_cnt[3]
    		printk("%x",readl(mgr->ddr_flush_buf+u32idx));
    		if(u32idx%16==0){ printk("\n"); }//1024
        }
    }
#endif

	/*if(evt_reason_id == EVT_TELE)
	{
		memcpy((telemetry_ctrlr_data->runtime_log), mgr->ddr_flush_buf, 11776);
	}
	else
	{
		mgr_flush(evt_reason_id);
		evt_trigger_cnt->complete_flush[cpu_id_0]++;
		//utils_apl_trace(LOG_ERR, 0, "done");
	}*/
	mgr_flush(evt_reason_id);
	evt_trigger_cnt->complete_flush[cpu_id_0]++;

}

ddr_code void clear_nand_log_block_and_reset(u32 p0, u32 p1, u32 pl)
{
	utils_apl_trace(LOG_ERR, 0xb8e6, "clear and reset nand log");
	int i = 0;
	evlog_reconstruction();
	for(i = 0; i < 8; ++i)
		erase_evtb(_evlog.blk_pda_base[i]);
	_evlog.first_nand_log_blk_status = NAND_LOG_BLK_READY_TO_WRITE_DATA;
}

#ifdef EVLOG_DUMP_MGR
void dump_mgr()
{
	evlog_mgr_t* mgr = &_evlog_mgr;
	int i = 0;
	printk("initialized = %d\n", mgr->initialized);
	printk("ddr_flush_buf = 0x%x\n", mgr->ddr_flush_buf);
	printk("ddr_buf_byte_size = %d\n", mgr->ddr_buf_byte_size);
	printk("ddr_flush_buf_cur = 0x%x\n", mgr->ddr_flush_buf_cur);

	for(i = 0; i < MPC; ++i)
	{
		printk("cpu%d:\n", i + 1);
		printk("   trigger = %d\n", mgr->evt_trigger_cnt.cpu[i]);
		printk("   complete_buf_swap = %d\n", i, mgr->evt_trigger_cnt.complete_buf_swap[i]);
		printk("   complete_copy = %d\n", i, mgr->evt_trigger_cnt.complete_copy[i]);
		printk("   complete_uart = %d\n", i, mgr->evt_trigger_cnt.complete_uart[i]);
		printk("   complete_flush = %d\n", i, mgr->evt_trigger_cnt.complete_flush[i]);
	}
}
#else
#define dump_mgr()
#endif

/*!
 * @brief event log manager module initialization
 *
 * @return	not used
 */
init_code void mgr_init(void)
{
	evlog_mgr_t* mgr = &_evlog_mgr;
	bool panic_before_initialized = false;
	int i = 0;

    //utils_apl_trace(LOG_ALW, 0, "[IN] mgr_init");

	sys_assert(mgr->ddr_buf_byte_size == (u32)&__evlog_flush_buf_end - (u32)&__evlog_flush_buf_start);
	cpu_msg_register(CPU_MSG_EVLOG_MGR_COPY, ipc_mgr_flush_to_nand);
	cpu_msg_register(CPU_MSG_EVLOG_CLEAR_AND_RESET, ipc_clear_nand_log_block_and_reset);
    cpu_msg_register(CPU_MSG_EVLOG_DUMP, ipc_evlog_dump);
	evt_register(mgr_copy_and_flush_to_nand, 0, &evt_evlog_flush);
	evt_register(clear_nand_log_block_and_reset, 0, &evt_evlog_erase_and_reset);

	for(i = 0; i < MPC; ++i)
	{
		if(mgr->panic_before_initialized[i])
			panic_before_initialized = true;
		}

	mgr->initialized = true;
	if(panic_before_initialized)
		evt_set_cs(evt_evlog_flush, 0, get_cpu_id(), CS_NOW);
}
#endif // #if (CPU_ID == EVLOG_MGR_CPU)



// static  bool is_data_copied()
// {
// 	int i = 0;
// 	for(i = 0; i < MPC; ++i)
// 	{
// 		if(evt_trigger_cnt->complete_buf_swap[i] != evt_trigger_cnt->complete_copy[i])
// 			return false;
// 	}
// 	return true;
// }

static  void rptr_move_to_next(evlog_agt_buf_t *buf)
{
	u32 i = 0, log_buf_cnt = 1;
	s32 start_idx = buf->byte_rptr / sizeof(log_buf_t);
	u8 cur_tag = buf->log_tag[start_idx];
	for(i = start_idx + 1; i < EVLOG_AGT_LOG_STRUCT_CNT; ++i)
	{
		if(cur_tag != buf->log_tag[i])
			break;
		log_buf_cnt++;
	}

	if(i == EVLOG_AGT_LOG_STRUCT_CNT)
	{
		for(i = 0; i < EVLOG_AGT_LOG_STRUCT_CNT; ++i)
		{
			if(cur_tag != buf->log_tag[i])
				break;
			log_buf_cnt++;
		}
	}

	if(log_buf_cnt <= 5)
		buf->byte_rptr = i * sizeof(log_buf_t);
	else
	{
		if((buf->byte_rptr += 5 * sizeof(log_buf_t)) >= buf->buf_byte_size)
			buf->byte_rptr -= buf->buf_byte_size;
	}
}

// used only in evlog_doprint(...)
#define putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, value) \
do\
{\
	*(uart_write_buf) = (value);\
	(uart_write_buf)++;\
	(buf)->byte_wptr++;\
	(budget)--;\
	if(!(budget))\
	{\
		if(!(max_left_byte_cnt))\
			goto add_padding;\
		if((buf)->byte_wptr == (buf)->buf_byte_size)\
		{\
			(buf)->byte_wptr = 0;\
			(uart_write_buf) = (buf)->ptr;\
		}\
		if((buf)->byte_wptr == (buf)->byte_rptr)\
			rptr_move_to_next(buf);\
		sys_assert((buf)->byte_wptr != (buf)->byte_rptr);\
		set_budget_and_byte_cnt_left((budget),(max_left_byte_cnt),(buf)->byte_wptr,(buf)->byte_rptr,(buf)->buf_byte_size);\
	}\
} while(0)

// used only in evlog_doprint(...)
#define putdeclong_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, value) \
do{\
	s32 x = (value);\
	u32 tens = 0;\
	int t = 0;\
\
	if (x == 0) \
		putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, '0');\
	else\
	{\
		if ((x) < 0L) \
		{\
			putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, '-');\
			x = -x;\
		}\
\
		tens = 10;\
		while ((u32)x >= tens)\
			tens *= 10;\
		tens /= 10;\
\
		while (tens) \
		{\
			t = x / tens;\
\
			putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[t]);\
			x -= (t * tens);\
			tens /= 10;\
		}\
	}\
}while(0)

// used only in evlog_doprint(...)
#define putdeculong_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, value) \
do{\
	u32 x = (value);\
	u32 tens = 0;\
	int t = 0;\
\
	if (x == 0)\
		putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, '0');\
	else\
	{\
		tens = 10;\
		while (x >= tens)\
			tens *= 10;\
		tens /= 10;\
\
		while (tens) \
		{\
			t = x / tens;\
\
			putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[t]);\
			x -= (t * tens);\
			tens /= 10;\
		}\
	}\
}while(0)

#define set_budget_and_byte_cnt_left(budget,max_left_byte_cnt,byte_wptr,byte_rptr,buf_byte_size) \
do{\
	(budget) = ((byte_wptr) >= (byte_rptr))? (buf_byte_size) - (byte_wptr): (byte_rptr) - (byte_wptr);\
	if((budget) >= (max_left_byte_cnt))\
	{\
		(budget) = (max_left_byte_cnt);\
		(max_left_byte_cnt) = 0;\
	}\
	else\
		(max_left_byte_cnt) -= (budget);\
}while(0)

ddr_code void Save_info_before_error(u8 type)// add by Jay
{
	extern epm_info_t* shr_epm_info;
	extern volatile u8 plp_epm_update_done;
	plp_log_number_start = 0;//reset , don't save again when plp
	if(plp_epm_update_done)//cpu4 may lock soon , don't submit ncl cmd
		return;

	u16 save_idx;
	epm_error_warn_t* epm_error_warn_data = (epm_error_warn_t*)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);

	epm_error_warn_data->need_init = true;
	epm_error_warn_data->cur_save_idx &= 1;//make sure idx <= 1 
	epm_error_warn_data->cur_save_idx ^= 1;
	save_idx = epm_error_warn_data->cur_save_idx;
	//-------------------------------------------------------
	epm_error_warn_data->cur_save_type[save_idx] = type;
	epm_error_warn_data->cur_update_cpu_id[save_idx] = CPU_ID;
	epm_error_warn_data->cur_power_cycle_cnt[save_idx] = pc_cnt;
	epm_error_warn_data->cur_temperature[save_idx] = ts_tmt.cur_ts;
	//-------------------------------------------------------
	extern u8  CPU1_plp_step;
	extern u8  CPU2_plp_step;
	extern u32 plp_record_cpu1_lr;
	extern u16* plp_host_open_die;
	epm_error_warn_data->record_CPU1_plp_step[save_idx] = CPU1_plp_step;
	epm_error_warn_data->record_CPU2_plp_step[save_idx] = CPU2_plp_step;
	epm_error_warn_data->record_cpu1_gpio_lr[save_idx]  = plp_record_cpu1_lr;
	u16* open_die_addr;
	u16  open_die,next_die = 0;
	#if CPU_ID == 2
	open_die_addr = tcm_share_to_local(plp_host_open_die);
	#else
	open_die_addr = plp_host_open_die;
	#endif

	if(open_die_addr != NULL)
	{
		open_die = *open_die_addr;
		next_die  = *(open_die_addr + 2);//struct _core_wl_t , next_die
	}
	else
	{
		open_die = 0xEE;
	}

	epm_error_warn_data->record_host_open_die[save_idx] = open_die;
	epm_error_warn_data->record_host_next_die[save_idx] = next_die;
	//-------------------------------------------------------
	extern bool host_idle;
	extern volatile ftl_flags_t shr_ftl_flags;
	extern volatile u32 cache_handle_dtag_cnt;
	epm_error_warn_data->is_host_idle[save_idx] = host_idle;
	epm_error_warn_data->is_gcing[save_idx] = shr_ftl_flags.b.gcing;
	epm_error_warn_data->cache_handle_cnt[save_idx] = cache_handle_dtag_cnt;
	epm_error_warn_data->FICU_start[save_idx] = readl((const void *)(0xc0001188));
	epm_error_warn_data->FICU_done[save_idx]  = readl((const void *)(0xc0001180));
	//-------------------------------------------------------
	extern volatile u32 global_gc_mode;
	epm_error_warn_data->cur_global_gc_mode[save_idx] = global_gc_mode;
	//-------------------------------------------------------
	extern volatile u8 cpu_feedback[];
	u32 struct_feedback = (u32)(cpu_feedback[0] << 24) | (u32)(cpu_feedback[1] << 16) | (u32)(cpu_feedback[2] << 8) | (u32)(cpu_feedback[3]);
	epm_error_warn_data->cur_cpu_feedback[save_idx] = struct_feedback;
	
	#if CPU_ID != 4
	epm_update(ERROR_WARN_sign, (CPU_ID - 1));
	#else
	extern u8 evt_call_epm_update;
	evt_set_cs(evt_call_epm_update, 0, ERROR_WARN_sign, CS_TASK);
	#endif

	utils_trace_trace(LOG_ALW, 0x0e30,"[warning]call by cpu%d ,save type:%d,save_idx:%d open_die:%d next_die:%d fdb:0x%x",
		CPU_ID, type, save_idx, open_die, next_die,struct_feedback);
}

static int agt_doprint(log_level_t log_level, evlog_agt_buf_t *buf, const char *sp, int *vp)
{
	u32 old_byte_wptr = buf->byte_wptr;
	int i = 0, struct_cnt = 0;
	s32 tag_start_idx = buf->byte_wptr / sizeof(log_buf_t);
	u32 *info = NULL;
	u32 budget = 0;
	u32 max_left_byte_cnt = MAX_BYTE_CNT_OF_A_LOG;
	u32 bytes_written = 0;
	unsigned char *uart_write_buf = ptr_inc(buf->ptr,buf->byte_wptr);
	set_budget_and_byte_cnt_left(budget,max_left_byte_cnt,buf->byte_wptr,buf->byte_rptr,buf->buf_byte_size);

	// printk("[%s][%d] agt_buf_data_byte_cnt(buf) = %d, buf->buf_byte_size = %d\n", __FUNCTION__, __LINE__, agt_buf_data_byte_cnt(buf), buf->buf_byte_size);
	// printk("[%s][%d] buf = %x, buf->byte_wptr = %d, buf->byte_rptr = %d\n", __FUNCTION__, __LINE__, buf, buf->byte_wptr, buf->byte_rptr);

	*(u32 *)uart_write_buf = log_number++;
	info = ((u32 *)uart_write_buf) + 1;
	uart_write_buf += 8;
	buf->byte_wptr += 8;
	budget -= 8;

	if(budget == 0)
	{
		if(buf->byte_wptr == buf->buf_byte_size)
		{
			buf->byte_wptr = 0;
			uart_write_buf = buf->ptr;
		}

		if(buf->byte_wptr == buf->byte_rptr)
			rptr_move_to_next(buf);
		set_budget_and_byte_cnt_left(budget,max_left_byte_cnt,buf->byte_wptr,buf->byte_rptr,buf->buf_byte_size);
	}

	while (*sp)
	{
		if (*sp != '%')
		{
			*uart_write_buf = *sp;
			uart_write_buf++;
			buf->byte_wptr++;
			budget--;

			if(!budget)
			{
				if(!max_left_byte_cnt)
					goto add_padding;

				if(buf->byte_wptr == buf->buf_byte_size)
				{
					buf->byte_wptr = 0;
					uart_write_buf = buf->ptr;
				}

				if(buf->byte_wptr == buf->byte_rptr)
					rptr_move_to_next(buf);

				//sys_assert(buf->byte_wptr != buf->byte_rptr);
				set_budget_and_byte_cnt_left(budget,max_left_byte_cnt,buf->byte_wptr,buf->byte_rptr,buf->buf_byte_size);
			}
			sp++;
			continue;
		}
		sp++;

		char *cp = NULL;

		while (*sp == '-' || (*sp >= '0' && *sp <= '9') || *sp == '.')
			sp++;

		switch (*sp)
		{
		case 'p':	/* %p */
		case 'x':	/* %x */
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >> 28) & 0xF]);
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >> 24) & 0xF]);
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >> 20) & 0xF]);
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >> 16) & 0xF]);
		case 'w':	/* %w */
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >> 12) & 0xF]);
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >>  8) & 0xF]);
		case 'b':	/* %b */
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[((*vp) >>  4) & 0xF]);
		    putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, digits[ (*vp)        & 0xF]);
			vp++;
			break;
		case 'd':	/* %d */
			putdeclong_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, (s32) *vp);
			vp++;
			break;
		case 'u':	/* %u */
			putdeculong_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, (u32) *vp);
			vp++;
			break;
		case 'c':	/* %c */
			putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, (u8) *vp);
			vp++;
			break;
		case 's':	/* %s */
			cp = *(char **)vp;
			vp += sizeof(char *) / sizeof(int);
			for (; *cp != 0; cp++)
				putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, *cp);
			break;
		case '%':
			putchar_to_agt(budget, max_left_byte_cnt, buf, uart_write_buf, *sp);
			break;
		default:	/* ignore it */
			break;
		}
		sp++;
	}
add_padding:

	if(buf->byte_wptr == buf->buf_byte_size)
	{
		buf->byte_wptr = 0;
		uart_write_buf = buf->ptr;
	}

	bytes_written = (buf->byte_wptr > old_byte_wptr)? buf->byte_wptr - old_byte_wptr: buf->buf_byte_size - (old_byte_wptr - buf->byte_wptr);
	*info = LOG_SET_STRING_INFO(CPU_ID, log_level, bytes_written - 8);

	sys_assert(bytes_written <= MAX_BYTE_CNT_OF_A_LOG);

	for(struct_cnt = 1; bytes_written > struct_cnt * sizeof(log_buf_t); ++struct_cnt)
		;

	for(i = 0; i < struct_cnt; ++i)
		buf->log_tag[(tag_start_idx + i) % EVLOG_AGT_LOG_STRUCT_CNT] = buf->curr_tag;
	buf->curr_tag++;
	budget = struct_cnt * sizeof(log_buf_t) - bytes_written;
	memset(uart_write_buf, ' ', budget);
	buf->byte_wptr += budget;
	if(buf->byte_wptr == buf->buf_byte_size)
		buf->byte_wptr = 0;
	if(buf->byte_wptr == buf->byte_rptr)
		rptr_move_to_next(buf);

	//sys_assert(buf->byte_wptr < buf->buf_byte_size);
	bytes_written = (buf->byte_wptr > old_byte_wptr)? buf->byte_wptr - old_byte_wptr: buf->buf_byte_size - (old_byte_wptr - buf->byte_wptr);
	buf->uart_data_byte_cnt += bytes_written;
	return bytes_written;
}

// used to flush agt log to uart
static u32 agt_buf_flush_to_uart_given_byte_cnt(evlog_agt_buf_t *buf, u32 byte_cnt, int count_out)
{
	if(buf->uart_data_byte_cnt == 0)
		return 0;
	u32 i = 0;
	u32 tot_byte_cnt_flush = 0;
	u32 budget = (buf->uart_byte_rptr > buf->byte_wptr)? buf->buf_byte_size - buf->uart_byte_rptr: buf->byte_wptr - buf->uart_byte_rptr;
	unsigned char *flushed_buf = ptr_inc(buf->ptr,buf->uart_byte_rptr);
	u32 time_to_check_tag = sizeof(log_buf_t) - buf->uart_byte_rptr % sizeof(log_buf_t), curr_tag_idx = buf->uart_byte_rptr / sizeof(log_buf_t), curr_tag = buf->log_tag[curr_tag_idx];

	if(!budget)
	{
		buf->uart_data_byte_cnt = 0;
		return byte_cnt;
	}
	for(i = 0; i < byte_cnt; ++i)
	{
		if(putcharex(*flushed_buf, count_out))
		{
			tot_byte_cnt_flush++;
			buf->uart_byte_rptr++;
			buf->uart_data_byte_cnt--;
			flushed_buf++;
			budget--;
			time_to_check_tag--;
			if(time_to_check_tag == 0)
			{
				time_to_check_tag = sizeof(log_buf_t);
				curr_tag_idx++;
				if(curr_tag_idx == EVLOG_AGT_LOG_STRUCT_CNT)
					curr_tag_idx = 0;
				if(curr_tag != buf->log_tag[curr_tag_idx] || buf->uart_byte_rptr == buf->byte_wptr)
				{
					putcharex('\n', 64 * count_out);
					putcharex('\r', 64 * count_out);
					curr_tag = buf->log_tag[curr_tag_idx];
				}
			}
			if(budget == 0)
			{
				if(buf->uart_byte_rptr == buf->byte_wptr)
					return tot_byte_cnt_flush;
				buf->uart_byte_rptr = 0;
				flushed_buf = buf->ptr;
				if((budget = buf->byte_wptr - buf->uart_byte_rptr) == 0)
					return tot_byte_cnt_flush;
			}
		}
	}

	return tot_byte_cnt_flush;
}

u32 evlog_save_encode(log_level_t log_level, log_buf_t *header, log_buf_t *buf1, log_buf_t *buf2, log_buf_t *buf3)
{
	if (!agt->initialized || log_level < level)
		return 0;
	#if(CPU_ID == 1 || CPU_ID == 4)
	u32 flags = irq_save();
    #endif
	int i = 0;
	evlog_agt_buf_t *buf = NULL;
	log_buf_t *encode[4] = {header, buf1, buf2, buf3};
	u32 byte_to_flush = 0;
	u16 buf_idx = agt->curr_buf_idx;
	u32 evt_cnt = evt_triggered_cnt();
	u32 swap_cnt = 0;
	u32 buffer_swap = 0;
	if(evt_cnt)
	{
		spin_lock_take(SPIN_LOCK_KEY_EVLOG, 0, true);
		swap_cnt = buf_need_swap_cnt();
		buffer_swap = evt_cnt+swap_cnt;
		if((buffer_swap %2 == 0)&&(buffer_swap!=0))
		{
			buf_idx = (agt->curr_buf_idx + 1) & 1;
		}
		else
		{
			buf_idx = agt->curr_buf_idx;
		}
		spin_lock_release(SPIN_LOCK_KEY_EVLOG);

		//sys_assert((evt_cnt>=swap_cnt)&&(buffer_swap<=3));
		if(agt->buf[(buf_idx + 1) & 1].uart_data_byte_cnt != 0)
			evt_trigger_cnt->complete_uart[CPU_ID_0] = false;
		else
			evt_trigger_cnt->complete_uart[CPU_ID_0] = true;
	}

	buf = &(agt->buf[buf_idx]);

	// printk("[%s][%d] agt_buf_data_byte_cnt(buf) = %d, buf->buf_byte_size = %d\n", __FUNCTION__, __LINE__, agt_buf_data_byte_cnt(buf), buf->buf_byte_size);
	while(agt_buf_data_byte_cnt(buf) + 5 * sizeof(log_buf_t) > buf->buf_byte_size)
		rptr_move_to_next(buf);

	for(i = 0 ; i < 4; ++i)
	{
		memcpy(buf->ptr + buf->byte_wptr, encode[i], sizeof(log_buf_t));
		buf->log_tag[buf->byte_wptr / sizeof(log_buf_t)] = buf->curr_tag;
		buf->byte_wptr += sizeof(log_buf_t);
		if(buf->byte_wptr == buf->buf_byte_size)
			buf->byte_wptr = 0;
		buf->uart_data_byte_cnt += sizeof(log_buf_t);
	}

	buf->curr_tag++;
	//sys_assert(buf->byte_wptr < buf->buf_byte_size);

	agt_buf_flush_to_uart_default(0, 0, DEFAULT_FLUSh_PL(buf_idx, byte_to_flush));

	#if(CPU_ID == 1 || CPU_ID == 4)
	irq_restore(flags);
	#endif
	if((log_number & 63) == 0)
		evlog_printk(LOG_ALW, "timestamp of cpu%d: 0x%w%x", CPU_ID, (u32)(sys_time >> 32), (u32)(sys_time));

	if((plp_log_number_start) && (log_number - plp_log_number_start >= 70))
		Save_info_before_error(SAVE_MODE_PLP_TO_MANY_LOGS);

	return 4 * sizeof(log_buf_t);
}

ddr_code u32 evlog_printk(log_level_t log_level, const char *fmt, ...)
{
	#if(CPU_ID == 1 || CPU_ID == 4)
    //DIS_ISR();
	//BEGIN_CS1
	u32 flags = irq_save();
    #endif
	if (!agt->initialized || log_level < level)
	{
		#if(CPU_ID == 1 || CPU_ID == 4)
   		//EN_ISR();
		//END_CS1
		irq_restore(flags);
    	#endif
		return 0;
	}

	u16 buf_idx = agt->curr_buf_idx;
	evlog_agt_buf_t *buf = NULL;
	u32 byte_written_to_buf = 0, byte_to_flush = 0;
	u32 evt_cnt = evt_triggered_cnt();
	u32 swap_cnt = 0;
	u32 buffer_swap = 0;
	if(evt_cnt)
	{
		spin_lock_take(SPIN_LOCK_KEY_EVLOG, 0, true);
		swap_cnt = buf_need_swap_cnt();
		buffer_swap = evt_cnt+swap_cnt;
		if((buffer_swap %2 == 0)&&(buffer_swap!=0))
		{
			buf_idx = (agt->curr_buf_idx + 1) & 1;
		}
		else
		{
			buf_idx = agt->curr_buf_idx;
		}
		spin_lock_release(SPIN_LOCK_KEY_EVLOG);

		//sys_assert((evt_cnt>=swap_cnt)&&(buffer_swap<=3));
		if(agt->buf[(buf_idx + 1) & 1].uart_data_byte_cnt != 0)
			evt_trigger_cnt->complete_uart[CPU_ID_0] = false;
		else
			evt_trigger_cnt->complete_uart[CPU_ID_0] = true;
	}

	buf = &(agt->buf[buf_idx]);

	int *nextarg = (int *)&fmt;
	nextarg += sizeof(char *) / sizeof(int);
	byte_to_flush = byte_written_to_buf = agt_doprint(log_level, buf, fmt, nextarg);
	agt_buf_flush_to_uart_default(0, 0, DEFAULT_FLUSh_PL(buf_idx, byte_to_flush));


	#if(CPU_ID == 1 || CPU_ID == 4)
	//EN_ISR();
	//END_CS1
	irq_restore(flags);
	#endif

	if((log_number & 63) == 0)
		evlog_printk(LOG_ALW, "timestamp of cpu%d: 0x%w%x", CPU_ID, (u32)(sys_time >> 32), (u32)(sys_time));

	return byte_written_to_buf;
}

char *get_evt_reason(u16 evt_id)
{
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(evt_reason); ++i)
	{
		if(evt_reason[i].evt_id == evt_id)
			return evt_reason[i].reason;
	}

	return "invalid evt_id";
}

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
#if Panic_save_epm
extern volatile spb_id_t       host_spb_close_idx;
extern spb_id_t       gc_spb_close_idx;
extern epm_info_t* shr_epm_info;
extern struct nand_info_t nand_info;
ddr_code void panic_save_epm(void)
{
	extern volatile u8 plp_epm_update_done;
	if(plp_epm_update_done)
		return;
	u32    *vc;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	u32 dtag_cnt = occupied_by( (shr_nand_info.geo.nr_blocks) * sizeof(u32), DTAG_SZE);
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);

	evlog_printk(LOG_ALW, "Attention : plp_copy_ftl_data_to_epm, blknum %d ", shr_nand_info.geo.nr_blocks);

	sys_assert(dtag.dtag != _inv_dtag.dtag);
	l2p_mgr_vcnt_move(0, dtag2mem(dtag), dtag_cnt * DTAG_SZE);
	vc = dtag2mem(dtag);

	memcpy(epm_ftl_data->epm_vc_table, vc, (shr_nand_info.geo.nr_blocks) * sizeof(u32));
	dtag_cont_put(DTAG_T_SRAM, dtag, dtag_cnt);

	epm_ftl_data->spor_tag = FTL_EPM_SPOR_TAG;
	epm_ftl_data->last_close_host_blk = host_spb_close_idx;
	epm_ftl_data->last_close_gc_blk = gc_spb_close_idx;
	epm_ftl_data->panic_build_vac = true;
	evlog_printk(LOG_ALW, "Attention : save epm");
	epm_update(EPM_PLP1,(CPU_ID-1));
}
#endif
#endif
// ----------------------------------------------------------------------------
// Export function
// ----------------------------------------------------------------------------

//ddr_data bool plp_log_only_once = false;

ddr_code void flush_to_nand(u16 evt_reason_id)
{
	evlog_mgr_t* mgr = &_evlog_mgr;
	int i = 0;

	if(mgr->panic_nand_log_triggered)
		return;
	for(i = 0; i < MPC; ++i)
	{
		if(panic_occure[i])
			mgr->panic_nand_log_triggered = true;
	}
	if(evt_triggered_cnt()>=2)
		return;
    if((plp_trigger)&&(evt_reason_id != EVT_PANIC)&&(evt_reason_id != EVT_PLP_HANDLE_DONE)){
        return;
    }


	spin_lock_take(SPIN_LOCK_KEY_EVLOG, 0, true);
	if(buf_need_swap_cnt()>=1)
	{
		spin_lock_release(SPIN_LOCK_KEY_EVLOG);
		return;
	}
	spin_lock_release(SPIN_LOCK_KEY_EVLOG);
	if((evt_reason_id == EVT_PANIC) || (evt_reason_id == EVT_while_break) || (evt_reason_id == EVT_CMD_TIMEOUT))
	{
		//log_isr = LOG_IRQ_DOWN;
		extern bool save_nor_flag;
		save_nor_flag = false;//if other panic trigger first to save nor
		in_panic = true;

	}

	bool reduce_plp_log = false;
	extern u8 plp_evlog_trigger;
	if(plp_evlog_trigger == EVLOG_TRIGGER_START  && evt_reason_id == EVT_PLP_HANDLE_DONE )//may plp no done,save log soon!!
	{
		reduce_plp_log = true;//
		plp_evlog_trigger = EVLOG_TRIGGER_DONE;
		extern u8  CPU1_plp_step;
		extern u8  CPU2_plp_step;
		utils_trace_trace(LOG_ALW, 0xbfb2,"plp may fail!! need check cpu1:%d cpu2:%d",CPU1_plp_step,CPU2_plp_step);
	}


	// utils_apl_trace(LOG_ALW, 0, "[evt by cpu%d] error code %d", CPU_ID, evt_reason_id);
	if(reduce_plp_log == false)
		evlog_printk(LOG_ALW, "[evt by cpu%d] %s", CPU_ID, get_evt_reason(evt_reason_id));
	else
		evlog_printk(LOG_ALW, "[evt by cpu%d] plp may fail", CPU_ID);

	evlog_printk(LOG_ALW, "timestamp:0x%w%x,fw%s(%s %s)", (u32)(sys_time >> 32), (u32)(sys_time), get_fw_version(), __DATE__, __TIME__);
	u32 log_poh = (poh + (jiffies / 36000));
	evlog_printk(LOG_ALW, "POH:0x%x, Power cycle cnt:0x%x", log_poh, pc_cnt);

	if(reduce_plp_log == false)
	{
	srb_t *srb = (srb_t *) SRAM_BASE;
	evlog_printk(LOG_ALW, "PGR Ver : %s ",srb->pgr_ver);
	evlog_printk(LOG_ALW, "Loader Ver : %s",srb->ldr_ver);
	evlog_printk(LOG_ALW, "SN: %s ",dri_sn);
#if(CORR_ERR_INT == ENABLE)
	evlog_printk(LOG_ALW, "Rx Err Cnt: %d ", RxErr_cnt);
#ifdef RXERR_IRQ_RETRAIN
	evlog_printk(LOG_ALW, "Retrain(timer) Cnt: %d ", retrain_cnt);
#endif
#endif
	}



	evt_trigger_cnt->trigger[CPU_ID_0]++;

	if(evt_reason_id == EVT_PANIC)
	{

	#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
	#if Panic_save_epm
        extern volatile bool all_init_done;
        if(all_init_done){
		panic_save_epm();
        }

	#endif
	#endif
	}
#if 0//(PLP_SUPPORT == 0)     
    if(evt_reason_id == EVT_GC_CANT_FREE_SPB)
    {
       panic("GC CANT FREE SPB");

    }
#endif

	if(mgr->initialized)
	{
#if (CPU_ID == EVLOG_MGR_CPU)
		evt_set_cs(evt_evlog_flush, evt_reason_id, get_cpu_id(), CS_NOW);
#else
		cpu_msg_issue(EVLOG_MGR_CPU - 1, CPU_MSG_EVLOG_MGR_COPY, evt_reason_id, get_cpu_id());
#endif
	}
	else {
		mgr->panic_before_initialized[CPU_ID_0] = true;
	}

	if(reduce_plp_log)//only once
	{
		Save_info_before_error(SAVE_MODE_PLP_MAY_ERROR);
	}

}

fast_code bool check_evlog_flush_done()
{
	bool ret = (evt_trigger_cnt->trigger[CPU_ID_0] == evt_trigger_cnt->complete_flush[CPU_ID_0]);
	return ret;
}

ddr_code void evlog_clear_nand_log_block_and_reset()
{
#if (CPU_ID == EVLOG_MGR_CPU)
	evt_set_cs(evt_evlog_erase_and_reset, 0, 0, CS_NOW);
#else
	cpu_msg_issue(EVLOG_MGR_CPU - 1, CPU_MSG_EVLOG_CLEAR_AND_RESET, 0, 0);
#endif
}

#if CPU_ID == NVME_CPU
pda_t idx_to_pda(int idx)
{
	return blk_pda_base[idx];
}


#endif

// used to flush log to uart with less but user-friendly interface
ddr_code u32 flush_to_uart(evlog_agt_buf_t *buf, u32 expect_or_absolute, u32 byte_cnt)
{
	u32 tot_flush_cnt = 0, flush_cnt = 0;
	u32 check = 0;
	u32 budget = (byte_cnt == FLUSH_ALL_LOG)? buf->uart_data_byte_cnt: min(byte_cnt, buf->uart_data_byte_cnt);
	/*
	if(log_isr == LOG_IRQ_REST)
	{
		printk("albert debug2: r: %x ;w: %x;cnt: %x\n",buf->byte_rptr,buf->byte_wptr,buf->uart_data_byte_cnt);
	}
	*/
	check = budget;

	if((check > buf->buf_byte_size))
	{
		//printk("albert debug3: r: %x ;w: %x;cnt: %x\n",buf->byte_rptr,buf->byte_wptr,buf->uart_data_byte_cnt);
		budget = (buf->uart_byte_rptr > buf->byte_wptr)? buf->buf_byte_size - buf->uart_byte_rptr: buf->byte_wptr - buf->uart_byte_rptr;
		buf->uart_data_byte_cnt = budget;
	}

	if(expect_or_absolute == LOG_FLUSH_EXPECT)
	{
		return agt_buf_flush_to_uart_given_byte_cnt(buf, budget, count_out);
	}
	else if(expect_or_absolute == LOG_FLUSH_ABSOLUTE)
	{
		while(budget && buf->uart_data_byte_cnt)
		{
			flush_cnt = agt_buf_flush_to_uart_given_byte_cnt(buf, budget, count_out);
			budget -= flush_cnt;
			tot_flush_cnt += flush_cnt;
		}
		return tot_flush_cnt;
	}
	return 0;
}

typedef enum
{
	EV_MAIN_NONE,
	EV_MAIN_COPY,
	EV_MAIN_CLEAR_AND_RESET_NAND_LOG,
	EV_MAIN_STRING,
	EV_MAIN_ENCODE,
	EV_MAIN_RECONSTRUCT
}ev_main_t;

ddr_code int log_level_main(int argc, char *argv[])
{
	if (argc == 1) {
		utils_trace_trace(LOG_INFO, 0x3d18, "log_level(%d)\n", level);
	}
	else {
		utils_trace_trace(LOG_INFO, 0x63ac, "log_level(%d)->(%d)\n", level, atoi(argv[1]));
		level = atoi(argv[1]);
	}
	return 0;
}
static DEFINE_UART_CMD(log_level, "log_level",
	"change default_log_level",
	"change log_level, LOG_INFO, LOG_ERR, LOG_DEBUG, LOG_PANIC or some value above them",
		0, 1, log_level_main);

static ddr_code int evlog_main(int argc, char *argv[])
{
	ev_main_t act = (ev_main_t) atoi(argv[1]);

	if (act == EV_MAIN_COPY)
		flush_to_nand(EVT_UART_SAVE_LOG);
	else if(act == EV_MAIN_CLEAR_AND_RESET_NAND_LOG)
		evlog_clear_nand_log_block_and_reset();
// 	else if(act == EV_MAIN_STRING)
// 	{
// 		int i = 0;
// 		for(i = 0; i < 20000; ++i)
// 			evlog_printk(LOG_ERR, "[%s][%d] %x\n", __FUNCTION__, __LINE__, i);
// 		// flush_to_nand(EVT_UART_SAVE_LOG);
// 		// panic("Sunday");
// 		evlog_printk(LOG_ERR, "[%s][%d]\n", __FUNCTION__, __LINE__);
// 	}
// 	else if(act == EV_MAIN_ENCODE)
// 	{
// 		int i = 0;
// 		for(i = 0; i < 20000; ++i)
// 			utils_apl_trace(LOG_ERR, 0, "[%s][%d] %x\n", __FUNCTION__, __LINE__, i);
// 		// flush_to_nand(EVT_UART_SAVE_LOG);
// 		// panic("Sunday");
// 		utils_apl_trace(LOG_ERR, 0, "[%s][%d]\n", __FUNCTION__, __LINE__);
// 	}
// #if CPU_ID == EVLOG_MGR_CPU
// 	else if(act == EV_MAIN_RECONSTRUCT)
// 	{
// 		evlog_printk(LOG_ERR, "[%s][%d]\n", __FUNCTION__, __LINE__);
// 		evlog_reconstruction();
// 	}
// 	evlog_printk(LOG_ERR, "[%s][%d]\n", __FUNCTION__, __LINE__);
// #endif
	return 0;
}

static DEFINE_UART_CMD(evlog_main, "evlog_main", "evlog_main", "evlog status", 1, 1, evlog_main);
#endif // #if defined(RDISK)
