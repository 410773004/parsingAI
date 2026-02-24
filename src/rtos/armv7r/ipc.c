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

//Process the communication between different CPU
//=============================================================================
#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "ipc_register.h"
#include "irq.h"
#include "assert.h"
#include "sect.h"
#include "mod.h"
#include "vic_id.h"
#include "bitops.h"
#include "string.h"
#include "console.h"
#include "event.h"
#include "pic.h"
#include "dma.h"
#include "ipc.h"
#include "rainier_soc.h"

/*! \cond PRIVATE */
#define __FILEID__ ipc
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
static fast_data ipc_rxq_t rxq_array[CPU_CNT-1]   __attribute__ ((aligned(4)));

fast_code u32 cpu_index_cal(u32 cpu_id)
{
    u32 cpu_idx;

    if (cpu_id > (CPU_ID - 1))
        cpu_idx = cpu_id - 1;
    else
        cpu_idx = cpu_id;

    return cpu_idx;
}

//Set IPC_DMA_Mode
//2'h0: Single Mode
//2'h1: Dedicated Mode
//2'h2: Flexible Mode
fast_code void ipc_dma_mode_cfg(ipc_mode_t value)
{
    ipc_writel(value, IPC_DMA_MODE);
}

//Use txq_open to create txq from our CPU to destination CPU.
//maxq_shift: set TXQ size.
//rx_cpu_id: set the desination CPU
fast_code ipc_txq_t *txq_open(u32 maxq_shift, u32 rx_cpu_id)
{
    bool maxq_shift_valid, rx_cpu_id_valid;

    maxq_shift_valid = (maxq_shift > 0) & (maxq_shift < 9);
    rx_cpu_id_valid  = (rx_cpu_id > 0) & (rx_cpu_id < 9);

    if (maxq_shift_valid == false)
        return NULL;

    if (rx_cpu_id_valid == false)
        return NULL;

    u32 base_offset = (0x100 * CPU_ID) + (0x4 * (rx_cpu_id - 1));
    ipc_txq_t *txq = sys_malloc(FAST_DATA, sizeof(*txq));
    if (txq == NULL)
        return txq;

    txq->txq_ctrl.all = 0x0;
    txq->cpu_id = rx_cpu_id;
    txq->maxq_size = 1 << maxq_shift;
    txq->reg_base = (void *)base_offset;
    txq->msg_base = sys_malloc_aligned(FAST_DATA, sizeof(ipc_msg_t) * txq->maxq_size, 32);

    if (txq->msg_base == NULL) {
        sys_free(FAST_DATA, txq);
        return NULL;
    }

    //prepare txq and configure txq registers
    cpuxy_txq_msgbuf_t  txq_msg_reg;
    cpuxy_txq_ctl_t     txq_ctl_reg;
    u32 offset;

    offset = base_offset + 0x20 * IPC_TYPE_TXQ;
    txq_msg_reg.all = ipc_readl(offset);

    txq_msg_reg.b.cpuxy_tx_base = btcm_to_dma(txq->msg_base);
    txq_msg_reg.b.cpuxy_tx_entry_size = txq->maxq_size - 1;
    ipc_writel(txq_msg_reg.all, offset);

    offset = base_offset + 0x20 * IPC_TYPE_TXQ_CTRL;
    txq_ctl_reg.all = ipc_readl(offset);

    txq_ctl_reg.b.cpuxy_tx_tail = 0x0;
    txq_ctl_reg.b.cpuxy_tx_rst  = 0x1;
    ipc_writel(txq_ctl_reg.all, offset);

    return txq;
}

//Check the available credit of TXQ. Only used by txq_alloc
//False: None credit in TXQ; True: Enough credit in TXQ
fast_code bool ipc_txq_credit(ipc_txq_t *txq)
{
    bool txq_sts;

    if (txq != NULL)
    {
        cpuxy_txq_ctl_t txq_ctrl_reg;
        u32 offset = (u32)txq->reg_base + 0x20 * IPC_TYPE_TXQ_CTRL;
        txq_ctrl_reg.all = ipc_readl(offset);

        u8 tail = txq_ctrl_reg.b.cpuxy_tx_tail;
        u8 head = txq_ctrl_reg.b.cpuxy_tx_head;
        u8 avail_credit = ((txq->maxq_size - 1) - (tail - head)) & (txq->maxq_size - 1);

        txq->txq_ctrl.b.credit = avail_credit;
        txq->txq_ctrl.b.head   = txq_ctrl_reg.b.cpuxy_tx_head;

        if (txq_ctrl_reg.b.cpuxy_tx_tail != txq->txq_ctrl.b.tail)
            rtos_mmgr_trace(LOG_ERR, 0xe619, "Error: get wrong txq tail\n");

        if (avail_credit != 0x0)
            txq_sts = true;
        else
            txq_sts = false;
    }
    else
        txq_sts = false;

    return txq_sts;
}

//Check Ready status of destination CPU before txq_alloc.
//False: destination cpu is not ready. True: destination cpu is ready to recieve the message
fast_code bool ipc_dst_cpu_rdy(u32 dst_cpu_id)
{
    bool rdy_sts = false;
    u32 cpu_rx_ready_reg;
    u32 offset;
    u32 cpux_id_map = 0x1;

    if ((dst_cpu_id > 0) && (dst_cpu_id < 9))
    {
        cpux_id_map = (cpux_id_map << (dst_cpu_id - 1));

        offset = 0xA04 + (CPU_ID * 0x10);
        cpu_rx_ready_reg = ipc_readl(offset);

        if ((cpu_rx_ready_reg & cpux_id_map) != 0)
            rdy_sts = true;
    }

    return rdy_sts;
}

//Call the function to get resouce before txq_submit
fast_code ipc_msg_t *txq_alloc(ipc_txq_t *txq, bool wait)
{
    bool txq_sts;
    ipc_msg_t *msg_ptr;

    if(txq == NULL)
        return NULL;

    if (txq->txq_ctrl.b.credit == 0x0)
    {
        do {
            txq_sts = ipc_txq_credit(txq);
        } while((txq_sts == false) & (wait == true));

        if (txq_sts == false)
            return NULL;
    }

    msg_ptr = &txq->msg_base[txq->txq_ctrl.b.wptr];

    if (txq->txq_ctrl.b.credit != 0)
        txq->txq_ctrl.b.credit--;
    else
        rtos_mmgr_trace(LOG_ERR, 0xbdb1, "Error: txq credit is zero\n");

    if (txq->txq_ctrl.b.wptr == (txq->maxq_size - 1))
        txq->txq_ctrl.b.wptr = 0;
    else
        txq->txq_ctrl.b.wptr++;

    return msg_ptr;
}

//Use txq_submit to send message in txq
//Return IPC_SUCCESS if the meessage have been transmitted.
//Return IPC_OP_ERR if input parameter isn't correct.
fast_code ipc_err_t txq_submit(ipc_txq_t *txq, u32 msg_size)
{
    u32 expect_tail;
    ipc_err_t ipc_rsp;
    cpuxy_txq_ctl_t cpuxy_txq_ctrl_reg;

    if (txq == NULL)
        return IPC_OP_ERR;

    if ((msg_size > (txq->maxq_size - 1)) || (msg_size == 0x0))
        return IPC_OP_ERR;

    //Make sure wptr is equal to (tail + msg_size)
    expect_tail = (txq->txq_ctrl.b.tail + msg_size) & (txq->maxq_size - 1);

    if (txq->txq_ctrl.b.wptr == expect_tail) {
        u32 offset = (u32)txq->reg_base + 0x20 * IPC_TYPE_TXQ_CTRL;

        txq->txq_ctrl.b.tail = expect_tail;

        cpuxy_txq_ctrl_reg.b.cpuxy_tx_tail = txq->txq_ctrl.b.tail;
        cpuxy_txq_ctrl_reg.b.cpuxy_tx_rst  = 0x0;
        ipc_writel(cpuxy_txq_ctrl_reg.all, offset);

        ipc_rsp = IPC_SUCCESS;
    }
    else
        ipc_rsp = IPC_OP_ERR;

    return ipc_rsp;
}

fast_code bool txq_close(ipc_txq_t *txq)
{
    bool ret = true;
    cpuxy_txq_msgbuf_t  txq_msg_reg;
    cpuxy_txq_ctl_t     txq_ctl_reg;
    u32 offset;

    if (txq == NULL)
        return false;

    //clear TXQ and TXQ CTRL registers
    rtos_mmgr_trace(LOG_ERR, 0x0a9f, "txq_close:: txq->reg_base: %0x\n", txq->reg_base);

    offset = (u32)txq->reg_base + 0x20 * IPC_TYPE_TXQ;
    txq_msg_reg.b.cpuxy_tx_base = 0x0;
    txq_msg_reg.b.cpuxy_tx_entry_size = 0x0;
    ipc_writel(txq_msg_reg.all, offset);

    offset = (u32)txq->reg_base + 0x20 * IPC_TYPE_TXQ_CTRL;
    txq_ctl_reg.b.cpuxy_tx_tail = 0x0;
    txq_ctl_reg.b.cpuxy_tx_rst  = 0x1;
    ipc_writel(txq_ctl_reg.all, offset);

    sys_free_aligned(FAST_DATA, txq->msg_base);
    sys_free(FAST_DATA, txq);

    return ret;
}

//Use rxq_open to create RXQ
//maxq_shift: Set maximum queue size of RXQ
//tx_cpu_id: Set source CPU ID
fast_code ipc_rxq_t *rxq_open(u32 maxq_shift, u32 tx_cpu_id, u32 hdr_cb_cnt)
{
    bool maxq_shift_valid, tx_cpu_id_valid;
    
    maxq_shift_valid = (maxq_shift > 0) & (maxq_shift < 9);
    tx_cpu_id_valid  = (tx_cpu_id > 0) & (tx_cpu_id < 9);

    if (maxq_shift_valid == false)
        return NULL;

    if (tx_cpu_id_valid == false)
        return NULL;

    u32 base_offset = (0x100 * tx_cpu_id) + (0x4 * (CPU_ID - 1));
    ipc_rxq_t *rxq = sys_malloc(FAST_DATA, sizeof(*rxq));
    if (rxq == NULL)
        return rxq;

    rxq->rxq_ctrl.all = 0x0;
    rxq->cpu_id = tx_cpu_id;
    rxq->maxq_size = 1 << maxq_shift;
    rxq->reg_base = (void *)base_offset;
    rxq->msg_base = sys_malloc_aligned(FAST_DATA, sizeof(ipc_msg_t) * rxq->maxq_size, 32);

    if (rxq->msg_base == NULL) {
        sys_free(FAST_DATA, rxq);
        return NULL;
    }

    rxq->hdr_cb_cnt = hdr_cb_cnt;
    rxq->hdr_cb = sys_malloc(FAST_DATA, hdr_cb_cnt * sizeof(void *));
    memset(rxq->hdr_cb, 0, hdr_cb_cnt * sizeof(void *));

    //Save rxq to fetch in ipc_isr
    u32 cpu_index;
    cpu_index = cpu_index_cal(tx_cpu_id - 1);
    rxq_array[cpu_index] = *rxq;

    //prepare rxq and configure rxq registers
    cpuxy_rxq_msgbuf_t rxq_msg_reg;
    cpuxy_rxq_ctl_t rxq_ctl_reg;
    cpuxy_int_sts_msk_t cpu_int_sts_msk_reg;
    u32 offset;

    //Configure RXQ_CTRL to avoid to miss message by HW
    offset = base_offset + 0x20 * IPC_TYPE_RXQ_CTRL;
    rxq_ctl_reg.all = ipc_readl(offset);

    rxq_ctl_reg.b.cpuxy_rx_head = 0x0;
    rxq_ctl_reg.b.cpuxy_rx_rst  = 0x1;
    ipc_writel(rxq_ctl_reg.all, offset);

    //Configure RXQ register
    offset = base_offset + 0x20 * IPC_TYPE_RXQ;
    rxq_msg_reg.all = ipc_readl(offset);

    rxq_msg_reg.b.cpuxy_rx_base = btcm_to_dma(rxq->msg_base);
    rxq_msg_reg.b.cpuxy_rx_entry_size = rxq->maxq_size - 1;
    ipc_writel(rxq_msg_reg.all, offset);

    //Enable rx_ready bit
    offset = base_offset + 0x20 * IPC_TYPE_RXQ_CTRL;
    rxq_ctl_reg.all = ipc_readl(offset);

    rxq_ctl_reg.b.cpuxy_rx_rst   = 0x0;
    rxq_ctl_reg.b.cpuxy_rx_ready = 0x1;
    ipc_writel(rxq_ctl_reg.all, offset);

    //Enable interrupt
    u8 int_msk_map;
    offset = 0xA00 + (CPU_ID * 0x10);
    cpu_int_sts_msk_reg.all = ipc_readl(offset);

    int_msk_map = 0x1 << (tx_cpu_id - 1);

    rtos_mmgr_trace(LOG_ERR, 0x4271, "Enable int msk %x for cpu:%d\n", int_msk_map, tx_cpu_id);

    cpu_int_sts_msk_reg.b.rxq_int_msk &= (~int_msk_map);
    ipc_writel(cpu_int_sts_msk_reg.all, offset);

    return rxq;
}

fast_code bool rxq_close(ipc_rxq_t *rxq)
{
    bool ret = true;    
    cpuxy_rxq_msgbuf_t rxq_msg_reg;
    cpuxy_rxq_ctl_t rxq_ctl_reg;
    cpuxy_int_sts_msk_t cpu_int_sts_msk_reg;
    u32 offset;
    u8 int_msk_map;

    if (rxq == NULL)
        return false;

    //Disable interrupt
    offset = 0xA00 + (CPU_ID * 0x10);
    cpu_int_sts_msk_reg.all = ipc_readl(offset);

    int_msk_map = 0x1 << (rxq->cpu_id - 1);

    cpu_int_sts_msk_reg.b.rxq_int_msk |= int_msk_map;
    ipc_writel(cpu_int_sts_msk_reg.all, offset);

    //Clear TXQ and TXQ_CTRL register
    offset = (u32)rxq->reg_base + 0x20 * IPC_TYPE_RXQ;
    rxq_msg_reg.all = ipc_readl(offset);

    rxq_msg_reg.b.cpuxy_rx_base = 0x0;
    rxq_msg_reg.b.cpuxy_rx_entry_size = 0x0;
    ipc_writel(rxq_msg_reg.all, offset);

    offset = (u32)rxq->reg_base + 0x20 * IPC_TYPE_RXQ_CTRL;
    rxq_ctl_reg.all = ipc_readl(offset);
    
    rxq_ctl_reg.b.cpuxy_rx_head = 0x0;
    rxq_ctl_reg.b.cpuxy_rx_rst  = 0x1;
    ipc_writel(rxq_ctl_reg.all, offset);

    sys_free_aligned(FAST_DATA, rxq->msg_base);
    sys_free(FAST_DATA, rxq);

    return ret;
}

fast_code void ipc_msg_handler(ipc_rxq_t *rxq, ipc_msg_t *msg)
{
	u32 hdr_type = msg->b.hdr_type;

	if (hdr_type >= rxq->hdr_cb_cnt)
		sys_assert(0);

	if (!rxq->hdr_cb[hdr_type])
		sys_assert(0);

	rxq->hdr_cb[hdr_type](msg);
}

fast_code int rxq_register(ipc_rxq_t *rxq, u8 hdr_type, void (*cb)(ipc_msg_t *msg))
{
	if (rxq == NULL)
		return -1;

	if (hdr_type >= rxq->hdr_cb_cnt)
		return -2;

	sys_assert(rxq->hdr_cb[hdr_type] == NULL);
	rxq->hdr_cb[hdr_type] = cb;

	return 0;
}

fast_code void ipc_isr(void)
{
	cpuxy_int_sts_msk_t cpu_int_sts_msk_reg;
	cpuxy_rxq_ctl_t rxq_ctl_reg;
	u32 offset, int_sts_offset;
	u32 rxq_int, rxq_map = 0x1;
	u32 rx_tail;
	u32 src_cpu_idx, i = 0;
	ipc_rxq_t *rxq;

	//Read interrupt status register to check
	int_sts_offset = 0xA00 + (CPU_ID * 0x10);

	cpu_int_sts_msk_reg.all = ipc_readl(int_sts_offset);
	rxq_int = cpu_int_sts_msk_reg.b.rxq_int;

	//Disable all interrupts mask to handle with recieved data
	cpu_int_sts_msk_reg.b.rxq_int_msk = 0xFF;
	ipc_writel(cpu_int_sts_msk_reg.all, int_sts_offset);

	while(rxq_int != 0) {
		rxq_map = 0x1 << i;
		if ((rxq_int & rxq_map) != 0) {
			//Read cpu_rx_tail to confirm the available data entry
			offset = (0x100 * (i + 1)) + 0x20 * IPC_TYPE_RXQ_CTRL + ((CPU_ID - 1) * 0x4);
			rxq_ctl_reg.all = ipc_readl(offset);

			rx_tail = rxq_ctl_reg.b.cpuxy_rx_tail;
			src_cpu_idx = cpu_index_cal(i);

			rxq = &rxq_array[src_cpu_idx];

			while(rx_tail != rxq->rxq_ctrl.b.head) {
				ipc_msg_handler(rxq, &rxq->msg_base[rxq->rxq_ctrl.b.head]);
				if (rxq->rxq_ctrl.b.head == (rxq->maxq_size - 1))
					rxq->rxq_ctrl.b.head = 0;
				else
					rxq->rxq_ctrl.b.head++;
			}

			rxq_ctl_reg.b.cpuxy_rx_head = rxq->rxq_ctrl.b.head;
			ipc_writel(rxq_ctl_reg.all, offset);

			rxq_int = rxq_int & (~rxq_map);
		}

		if (i < CPU_CNT)
			i++;
	}

	cpu_int_sts_msk_reg.b.rxq_int_msk = 0x0;
	ipc_writel(cpu_int_sts_msk_reg.all, int_sts_offset);
}

init_code void ipc_init(void)
{
	u32 offset;

	cpuxy_int_sts_msk_t cpu_int_sts_msk_reg;
	ipc_debug1_t ipc_debug1_reg;
	ipc_debug2_t ipc_debug2_reg;

	//Disable all interrupt of IPC
	offset = 0xA00 + (CPU_ID * 0x10);
	cpu_int_sts_msk_reg.b.rxq_int_msk = 0xFF;
	ipc_writel(cpu_int_sts_msk_reg.all, offset);

	//Configure debug register
	ipc_debug1_reg.all = ipc_readl(IPC_DEBUG1);
	ipc_debug1_reg.b.axiwr_err_cnt = 0x0;
	ipc_writel(ipc_debug1_reg.all, IPC_DEBUG1);

	ipc_debug2_reg.all = ipc_readl(IPC_DEBUG2);
	ipc_debug2_reg.b.axird_err_cnt = 0x0;
	ipc_writel(ipc_debug2_reg.all, IPC_DEBUG2);

	//Bind interrupt and intertupt handle function
	poll_irq_register(VID_IPC, ipc_isr);
	poll_mask(VID_IPC);
}

