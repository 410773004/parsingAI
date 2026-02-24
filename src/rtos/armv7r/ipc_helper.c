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

#include "ipc.h"
#include "sect.h"
#include "assert.h"

#define TXQ_SZ_SHF	8	// 2**n
#define RXQ_SZ_SHF	8	// 2**n

#define ZERO_BASE	1

static fast_data_zi ipc_txq_t *txq[MPC];
static fast_data_zi ipc_rxq_t *rxq[MPC];

init_code void ipc_helper_init(void)
{
	u8 i;

	for (i = 0; i < MPC; i++) {
		txq[i] = NULL;
		rxq[i] = NULL;

		if (i != (CPU_ID - ZERO_BASE)) {
			txq[i] = txq_open(TXQ_SZ_SHF, i + ZERO_BASE);
			sys_assert(txq != NULL);

			rxq[i] = rxq_open(RXQ_SZ_SHF, i + ZERO_BASE, IPC_MSG_END);
			sys_assert(rxq != NULL);
		}
	}
}

fast_code void ipc_msg_tx_2(u8 rx, u8 hdr_type, u32 dw1)
{
	sys_assert(txq[rx - ZERO_BASE]);
	ipc_msg_t *msg = txq_alloc(txq[rx - ZERO_BASE], 0);
	sys_assert(msg != NULL);

	msg->b.hdr_type = hdr_type;
	msg->b.tx = CPU_ID;

	msg->dw1 = dw1;

	sys_assert(txq_submit(txq[rx - ZERO_BASE], 1) == IPC_SUCCESS);
}

fast_code void ipc_msg_tx_3(u8 rx, u8 hdr_type, u32 dw1, u32 dw2)
{
	sys_assert(txq[rx - ZERO_BASE]);
	ipc_msg_t *msg = txq_alloc(txq[rx - ZERO_BASE], 0);
	sys_assert(msg != NULL);

	msg->b.hdr_type = hdr_type;
	msg->b.tx = CPU_ID;

	msg->dw1 = dw1;
	msg->dw2 = dw2;

	sys_assert(txq_submit(txq[rx - ZERO_BASE], 1) == IPC_SUCCESS);
}

fast_code void ipc_msg_tx_4(u8 rx, u8 hdr_type, u32 dw1, u32 dw2, u32 dw3)
{
	sys_assert(txq[rx - ZERO_BASE]);
	ipc_msg_t *msg = txq_alloc(txq[rx - ZERO_BASE], 0);
	sys_assert(msg != NULL);

	msg->b.hdr_type = hdr_type;
	msg->b.tx = CPU_ID;

	msg->dw1 = dw1;
	msg->dw2 = dw2;
	msg->dw3 = dw3;

	sys_assert(txq_submit(txq[rx - ZERO_BASE], 1) == IPC_SUCCESS);
}

fast_code int ipc_hdr_register(u8 tx, u8 hdr_type, void (*cb)(ipc_msg_t *msg))
{
	sys_assert(rxq[tx - ZERO_BASE]);
	return rxq_register(rxq[tx - ZERO_BASE], hdr_type, cb);
}
