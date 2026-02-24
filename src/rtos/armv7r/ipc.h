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

#include "types.h"
#include "vic_id.h"
#include "io.h"
#include "rainier_soc.h"

#define CPU_CNT                 4
#define IPC_RXQ_SIZE            10
#define IPC_TXQ_SIZE            256

enum {
	IPC_MSG_START = 0,
	IPC_MSG_NCMD_SYNC,		///< ncl_cmd sync submit
	IPC_MSG_NCMD_ASYNC,		///< ncl_cmd async submit
	IPC_MSG_NCMD_ASYNC_DONE,	///< ncl_cmd async submit done
	IPC_MSG_END,
};

enum {
	IPC_TYPE_START = 0,
	IPC_TYPE_RXQ = IPC_TYPE_START,
	IPC_TYPE_TXQ,
	IPC_TYPE_RXQ_CTRL,
	IPC_TYPE_TXQ_CTRL,
	IPC_TYPE_END
};

typedef enum {
	IPC_SUCCESS = 0,
	IPC_OP_ERR,
	IPC_TXQ_FULL,
	IPC_NO_READY
} ipc_err_t;

typedef enum {
	SINGLE_MODE,
	DEDICATED_MODE,
	FLEXIBLE_MODE
} ipc_mode_t;

typedef struct {
	union {
		u32 dw0;
		struct {
			u8 hdr_type;
			u8 tx;		//tx cpu id
			u16 hdr_ctrl;
		} b;
	};
	u32 dw1;
	u32 dw2;
	u32 dw3;
	u32 dw4;
	u32 dw5;
	u32 dw6;
	u32 dw7;
} ipc_msg_t;

static inline u32 ipc_readl(u32 reg)
{
	return readl((void *)(IPC_BASE + reg));
}

static inline void ipc_writel(u32 data, u32 reg)
{
	writel(data, (void *)(IPC_BASE + reg));
}

/* TXQ */
typedef union {
	u32 all;
	struct {
		u32 head:8;
		u32 tail:8;
		u32 credit:8;
		u32 wptr:8;
	} b;
} xxq_ctl_t;

typedef struct {
	xxq_ctl_t txq_ctrl;
	u32 cpu_id;
	void *reg_base;
	u32 maxq_size;
	ipc_msg_t *msg_base;
} ipc_txq_t;

void ipc_dma_mode_cfg(ipc_mode_t value);

ipc_txq_t *txq_open(u32 maxq_shift, u32 rx_cpu_id);
ipc_msg_t *txq_alloc(ipc_txq_t *txq, bool wait);

ipc_err_t  txq_submit(ipc_txq_t *txq, u32 msg_size);
bool txq_close(ipc_txq_t *txq);
bool ipc_txq_credit(ipc_txq_t *txq);
bool ipc_dst_cpu_rdy(u32 cpu_id);

/* RXQ */
typedef struct {
	xxq_ctl_t rxq_ctrl;
	u32 cpu_id;
	void *reg_base;
	u32 maxq_size;
	ipc_msg_t *msg_base;
	u32 hdr_cb_cnt;
	void (**hdr_cb)(ipc_msg_t *msg); /* hardcode for 32 hdr */
} ipc_rxq_t;

u32 cpu_index_cal(u32 cpu_id);
void ipc_isr(void);
 
ipc_rxq_t *rxq_open(u32 maxq_shift, u32 tx_cpu_id, u32 hdr_cb_cnt);
bool rxq_close(ipc_rxq_t *rxq);
int rxq_register(ipc_rxq_t *rxq, u8 hdr_type, void (*cb)(ipc_msg_t *msg));

void ipc_init(void);
void ipc_helper_init(void);

void ipc_msg_tx_2(u8 rx, u8 hdr_type, u32 dw1);
void ipc_msg_tx_3(u8 rx, u8 hdr_type, u32 dw1, u32 dw2);
void ipc_msg_tx_4(u8 rx, u8 hdr_type, u32 dw1, u32 dw2, u32 dw3);
int ipc_hdr_register(u8 tx, u8 hdr_type, void (*cb)(ipc_msg_t *msg));
