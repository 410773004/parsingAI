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

#include "types.h"
#include "stdio.h"
#include "io.h"
#include "rainier_soc.h"
#include "uart_registers.h"
#include "sect.h"
#include "string.h"
#include "event.h"
#include "irq.h"
#include "pic.h"
#include "pmu.h"
#include "mod.h"
#include "vic_id.h"
#include "misc.h"
#if defined(SAVE_CDUMP)
#include "mpc.h" //_GENE_4cpu cdump
#endif
#define XON    17  /* Ctrl-Q */
#define XOFF   19  /* Ctrl-S */

#if CPU_ID == 1
#define URSH_INTR_STS_1		URSH_CPU1_INTR_STS_1
#define URSH_INTR_STS_2		URSH_CPU1_INTR_STS_2
#define URSH_TXFF_BYTE_COUNT	URSH_CPU1_TXFF_BYTE_COUNT
#define URSH_TXFF		URSH_CPU1_TX_FIFO
#elif CPU_ID == 2
#define URSH_INTR_STS_1		URSH_CPU2_INTR_STS_1
#define URSH_INTR_STS_2		URSH_CPU2_INTR_STS_2
#define URSH_TXFF_BYTE_COUNT	URSH_CPU2_TXFF_BYTE_COUNT
#define URSH_TXFF		URSH_CPU2_TX_FIFO
#elif CPU_ID == 3
#define URSH_INTR_STS_1		URSH_CPU3_INTR_STS_1
#define URSH_INTR_STS_2		URSH_CPU3_INTR_STS_2
#define URSH_TXFF_BYTE_COUNT	URSH_CPU3_TXFF_BYTE_COUNT
#define URSH_TXFF		URSH_CPU3_TX_FIFO
#elif CPU_ID == 4
#define URSH_INTR_STS_1		URSH_CPU4_INTR_STS_1
#define URSH_INTR_STS_2		URSH_CPU4_INTR_STS_2
#define URSH_TXFF_BYTE_COUNT	URSH_CPU4_TXFF_BYTE_COUNT
#define URSH_TXFF		URSH_CPU4_TX_FIFO
#else
#error "wrong CPU ID"
#endif

enum uart_mode {
	UART_MODE_ASCII,
	UART_MODE_BIN
};

struct {
	enum uart_mode mode;
	u16 in;
	u16 out;
	u16 avail;
#define UART_RX_BF_SZ         256
#define UART_RX_BF_TH  (UART_RX_BF_SZ - 32)
	u8 rx_buf[UART_RX_BF_SZ];
} uart_rx_mgr fast_data;

static fast_data uart_share_register_regs_t *uart = (uart_share_register_regs_t*)(UART_BASE);
static fast_data_zi bool uart_init_done = false;
extern u8 evt_uart_rx;
extern bool all_init_done;
extern u16 shr_uart_dis;
/*!
 * @brief registers READ access
 *
 * @param reg	which register to access
 *
 * @return	Register value
 */
static inline u32 uart_readl(u32 reg)
{
	return readl((void *)(UART_BASE + reg));
}

/*!
 * @brief registers WRITE access
 *
 * @param val	the value to update
 * @param reg	which register to access
 *
 * @return	None
 */
static inline void uart_writel(u32 val, u32 reg)
{
	writel(val, (void *)(UART_BASE + reg));
}

#if 0
/*! uart register 0x20, it can not be used with uart share design */
#define UART_TEST_DEBUG                          0x00000020
#define   RXFF_BYCNT_MASK                        0x001f0000
#define   RXFF_BYCNT_SHIFT                               16
#define   TXFF_BYCNT_MASK                        0x00001f00
#define   TXFF_BYCNT_SHIFT                                8
#define   RXRESET_MASK                           0x00000004
#define   RXRESET_SHIFT                                   2
#define   TXRESET_MASK                           0x00000002
#define   TXRESET_SHIFT                                   1
#define   LOOPBACK_MASK                          0x00000001
#define   LOOPBACK_SHIFT                                  0

typedef union {
    u32 all;
    struct {
	u32 loopback:1;
	u32 txreset:1;
	u32 rxreset:1;
        u32 rsvd_3:5;
        u32 txff_bycnt:5;
        u32 rsvd_13:3;
        u32 rxff_bycnt:5;
        u32 rsvd_21:11;
    } b;
} uart_test_debug_t;

fast_code int __putchar__(int c)
{
	do {
		uart_test_debug_t sts;
		sts.all = uart_readl(UART_TEST_DEBUG);
		if (sts.b.txff_bycnt < 0x8)
			break;
	} while (uart_init_done);

	if (c == '\n')
		uart_writel('\r', UR_TX_FIFO);
	uart_writel(c, UR_TX_FIFO);
	return 0;
}
#endif

#if defined(SAVE_CDUMP)
extern u8 sgfulink_cpu_seq[MAX_MPC]; //_GENE_ 4cpu cdump
extern u8 sgfulink_cpu_flag[MAX_MPC];//_GENE_ 4cpu cdump

extern u8 sgfulink_cpu1;
extern u8 sgfulink_cpu2;
extern u8 sgfulink_cpu3;
extern u8 sgfulink_cpu4;
ddr_code void ulink_putc(int c)
{
	do {
		ursh_cpu1_txff_byte_count_t sts;
		sts.all = uart_readl(URSH_TXFF_BYTE_COUNT);
		if (sts.b.cpu1_txff_bc < 0x8)
			break;
	} while (uart_init_done);
	uart_writel(c, URSH_TXFF);
}
#endif
#if defined(SAVE_CDUMP)
ddr_code int putchar(int c)
#else
ps_code int putchar(int c)
#endif
{
#if defined(SAVE_CDUMP)
#if (CPU_ID == sgfulink_cpu1)//0520
	if (sgfulink_cpu_flag[0] == 2) {
		return 0;
	}
#endif
#if (CPU_ID == sgfulink_cpu2)//0520
	if (sgfulink_cpu_flag[1] == 2) {
		return 0;
	}
#endif
#if (CPU_ID == sgfulink_cpu3)//0520
	if (sgfulink_cpu_flag[1] == 2) {
		return 0;
	}
#endif
#if (CPU_ID == sgfulink_cpu4)//0520
	if (sgfulink_cpu_flag[3] == 2) {
		return 0;
	}
#endif
#endif
#ifndef HAVE_VELOCE
	do {
		ursh_cpu1_txff_byte_count_t sts;
		sts.all = uart_readl(URSH_TXFF_BYTE_COUNT);
		if (sts.b.cpu1_txff_bc < 0x8)
			break;
	} while (uart_init_done);
#endif
	uart_writel(c, URSH_TXFF);
	if (c == '\n')
		uart_writel('\r', URSH_TXFF);
	return 0;
}

#if defined(SAVE_CDUMP)
#if 1 //_GENE_ 4cpu cdump
ddr_code void ulink_putnum_2b(u32 num)
{
	ulink_putc(num & 0xFF);
	ulink_putc((num >> 8) & 0xFF);
}

ddr_code void ulink_putnum(u32 num)
{
	ulink_putnum_2b(num);
	ulink_putnum_2b(num >> 16);
}

ddr_code void ulink_putnum_ascii_4bit(u32 num)
{
	num = num & 0xF;
	if (num > 9) {
		num = num - 0xA;
		ulink_putc(num + 'a');
		return;
	}
	ulink_putc(num + '0');
}

ddr_code void ulink_putnum_ascii(u32 num, u32 bytecnt)
{
	u32 id;

	if ((0 == bytecnt) || (bytecnt > 4))
		return;

	do {
		bytecnt--;
		id = num>> (bytecnt << 3);
		ulink_putnum_ascii_4bit(id >> 4);
		ulink_putnum_ascii_4bit(id);
	} while (bytecnt);
}

ddr_code void ulink_putstr(char *s)
{
	u16 i = 60;

	do {
		if (0 == *s)
			break;
		ulink_putc(*s);
		s++;
	} while (--i);
}

ddr_code void ulink_putpad(u32 cnt)
{
	do {
		ulink_putc(0);
	} while (--cnt);
}

ddr_code void ulink_putstr_len(char *s, u32 len)
{
	u16 i = 0;

	while (i < len) {
		if (0 == *s) {
			ulink_putc(0);
		} else {
			ulink_putc(*s);
			s++;
		}
		i++;
	};
}

ddr_code void ulink_wait_c2da(void)
{
    extern void ulink_cpu_cmd(void); //_GENE_ 4cpu cdump
    ulink_cpu_cmd();                 //_GENE_ 4cpu cdump
}

/*!
 * @brief get uart command
 *
 * @param flag		0: Keep loop scan	-- Search "dump"
 *					1: check fifo one time -- Search "cdump"
 * @return		0  -- Get none
 * 				1  -- Get "dump"
 * 				2  -- Get "cdump"
 */
#define CDUMP_CMDLEN_MAX 5
ddr_code u16 ulink_getdumpcmd(u32 flag)
{
	u16 idx = 0;
	u16 ret = 0;
	u8 cmd[] = {'c', 'd', 'u', 'm', 'p' };
	ursh_cpu1_intr_sts_2_t dbg_sts;

	do {
		u16 c = 0;
		dbg_sts.all = uart_readl(URSH_INTR_STS_2);

		if (dbg_sts.b.cpu1_rxff_cnt) {
			ursh_cpu1_intr_sts_1_t rx_fifo;

			rx_fifo.all = uart_readl(URSH_INTR_STS_1);
			c = rx_fifo.b.cpu1_rxff_rpo;
			if (idx < CDUMP_CMDLEN_MAX) {
				if (0 == idx) {
					idx = 5;//none
					if (cmd[0] == c) {
						if (flag) {
							ret = 1;//=> "cdump"
						} else {
							ret = 3;//=> "cdump" => DA2
						}
						idx = 0;
					} else if ((cmd[1] == c) && (0 == flag)) {
						ret = 2;//=> "dump"
						idx = 1 ;
					}
				} else if (cmd[idx] != c) {
					idx = 5;//none
				}
				idx ++;
			} else if (CDUMP_CMDLEN_MAX == idx) {
				if (0xD == c) {
					//printk("ret:%d\n",ret);
					if ((3 == ret) && ((sgfulink_cpu_flag[0] != 2) || (sgfulink_cpu_flag[1] != 2) || (sgfulink_cpu_flag[2] != 2))) { //0520
						ulink_wait_c2da(); // call ulink_cpu1_cmd() issue ipc to request other cpu entry data abort mode.
						idx = 0;
						ret = 0;
						continue;
					}
					return ret;
				}
				idx++;
			}
		}
		if (flag && (0 == idx)) {
			udelay(50); // avoid miss uart command.
			return 0;
		}
		if (0xD == c) {
			idx = 0;
			ret = 0;
		}
		mdelay(5);
	} while(1);
}
#endif

ddr_code u32 ulink_da_mode(void)
{
	volatile u32 *addr = (u32 *)(~0);
	volatile u32 data;

	printk("entry data abort mode\n");
	data = *addr;
	*addr = 0xFFFF;
	return data;
}
#endif

ps_code int putcharex(int c, unsigned int count_out)
{
#ifndef HAVE_VELOCE
	do
	{
		ursh_cpu1_txff_byte_count_t sts;
		sts.all = uart_readl(URSH_TXFF_BYTE_COUNT);
		if (sts.b.cpu1_txff_bc < 0x8)
			break;
		if(--count_out == 0)
			return 0;
	} while (uart_init_done);
#endif
	if (!(all_init_done && (shr_uart_dis == 1))) {
		uart_writel(c, URSH_TXFF);
	}
	return 1;
}

ddr_code int getchar(char *c)//ps_code->ddr_code by suda
{
	if (uart_rx_mgr.avail > 0) {
		*c = uart_rx_mgr.rx_buf[uart_rx_mgr.out];

		uart_rx_mgr.avail--;
		uart_rx_mgr.out++;

		if (uart_rx_mgr.out == UART_RX_BF_SZ) {
			uart_rx_mgr.out = 0;
		}
		return 0;
	}
	return -1;
}

fast_code void uart_flush(void)
{
	int tm_loop = 65536;

	do {
		ursh_cpu1_txff_byte_count_t sts;
		sts.all = uart_readl(URSH_TXFF_BYTE_COUNT);
		if (sts.b.cpu1_txff_bc == 0)
			break;
	} while (uart_init_done && --tm_loop);

	return;
}

fast_code void uart_rx_clear(void)
{
	do {
		int ret = getchar(NULL);
		if (ret == -1)
			break;
	} while (1);
}

static ps_code void uart_isr(void)
{
	uart_interrupt_status_t sts = {
		.all = readl(&uart->uart_interrupt_status),
	};

	if (sts.b.rxff_rdy_int) {
		ursh_cpu1_intr_sts_2_t dbg_sts =  {
			.all = uart_readl(URSH_INTR_STS_2),
		};

		if ((dbg_sts.b.cpu1_rxff_cnt + uart_rx_mgr.avail > UART_RX_BF_TH) &&
				(uart_rx_mgr.mode == UART_MODE_ASCII)) {
			putchar(XOFF);
		}

		while (dbg_sts.b.cpu1_rxff_cnt) {
			if (uart_rx_mgr.in == UART_RX_BF_SZ)
				uart_rx_mgr.in = 0;

			ursh_cpu1_intr_sts_1_t rx_fifo = {
				.all = uart_readl(URSH_INTR_STS_1),
			};

			uart_rx_mgr.rx_buf[uart_rx_mgr.in] = rx_fifo.b.cpu1_rxff_rpo;

			uart_rx_mgr.in++;
			uart_rx_mgr.avail++;

			dbg_sts.b.cpu1_rxff_cnt--;
		}

		evt_set_cs(evt_uart_rx, 0, 0, CS_NOW);
	}

	writel(sts.all, &uart->uart_interrupt_status);
}

static void ps_code wait_uart_txfifo_to_clear(void)
{
#if 0
	uart_test_debug_t sts;
	do {
		sts.all = readl(&uart->uart_test_debug);
	} while (sts.b.txff_bycnt);
#endif
}

void ddr_code set_uart_divisor(u32 sys_clk)////ps_code -> ddr_code by suda
{
	wait_uart_txfifo_to_clear();

	uart_control_t ctrl = {0};
#if defined(FPGA)
	ctrl.b.divisor = sys_clk/57600/16;
#else
	//ctrl.b.divisor = sys_clk/115200/16;
	ctrl.b.divisor = sys_clk/921600/16;//adams
#endif
	ctrl.b.ur_enable   = 1;
	ctrl.b.rxff_thhold = 0; /* 1 Bytes threshold for Rx int */

	writel(ctrl.all, &uart->uart_control);
}

ddr_code int uart_rx_switch(int cpu_id)//ps_code -> ddr_code by suda
{
	ursh_rx_cpu_sel_t sel = { .all = uart_readl(URSH_RX_CPU_SEL)};

	if (cpu_id < 1 || cpu_id > 4)
		return -1;

	sel.b.ur_sh_rx_cpu_sel = cpu_id;
	uart_writel(sel.all, URSH_RX_CPU_SEL);
	if (cpu_id == CPU_ID) {
		misc_sys_isr_enable(SYS_VID_UART);
	}
#if defined(MPC)
	else {
		extern void ipc_api_misc_sys_isr_ctrl(u32 rx, u32 sirq, bool ctrl);
		ipc_api_misc_sys_isr_ctrl(cpu_id - 1, SYS_VID_UART, true);
	}
#endif
	return 0;
}

ps_code bool uart_suspend(enum sleep_mode_t mode)
{
	return true;
}

ps_code void uart_resume(enum sleep_mode_t mode)
{
	if (SUSPEND_ABORT == mode)
		return;

	/* unmask Rx interrupt */
	uart_interrupt_mask_t uart_int_mask = {
		.all = readl(&uart->uart_interrupt_mask.all),
	};
	uart_int_mask.b.rxff_rdy_intm = 0;
	writel(uart_int_mask.all, &uart->uart_interrupt_mask.all);

	set_uart_divisor(SYS_CLK);
}

ps_code int putchar_binary(int c)
{
	do {
		ursh_cpu1_txff_byte_count_t sts;
		sts.all = uart_readl(URSH_TXFF_BYTE_COUNT);
		if (sts.b.cpu1_txff_bc < 0x8)
			break;
	} while (uart_init_done);

	uart_writel(c, URSH_TXFF);
	return 0;
}

fast_code int getchar_sync(char *c, unsigned int timeout_ms)
{
	timeout_ms *= 20;

	do {
		ursh_cpu1_intr_sts_2_t sts2 =  {
			.all = uart_readl(URSH_INTR_STS_2),
		};

		if (sts2.b.cpu1_rxff_cnt) {
			if (c) {
				ursh_cpu1_intr_sts_1_t sts1 = {
					.all = uart_readl(URSH_INTR_STS_1),
				};
				*c = sts1.b.cpu1_rxff_rpo;
			}

			return 1;
		}

		udelay(50);
		if (timeout_ms == 0 || --timeout_ms == 0)
			break;

	} while (1);

	return -1;
}

slow_code void uart_poll(void)
{
	if (uart_init_done) {
		uart_isr();
	}
}

static init_code void uart_init(void)
{
	uart_resume(SUSPEND_INIT);
	uart_init_done = true;
	uart_rx_mgr.avail = 0;

	sirq_register(SYS_VID_UART, uart_isr, true);
#if CPU_ID == 2
	uart_rx_switch(2);
#endif
	//uart_rx_switch(2);//Benson Modify 20210521
	extern void console_init(void);
	console_init();
}

module_init(uart_init, RTOS_UART);
