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
#include "task.h"
//#include "atomic.h"
#include "stdio.h"
#include "irq.h"
#include "pic.h"
//#include "io.h"
#include "string.h"

#include "heap.h"
#include "stdlib.h"
#include "mod.h"
#include "event.h"
#include "console.h"
#include "cpu_msg.h"
#include "spin_lock.h"
#include "misc.h"
#include "pmu.h"
#include "dma.h"
//#include "rainier_soc.h"
#include "trng.h"
#if defined(RDISK)
#include "evlog.h"
#endif
#if defined(SAVE_CDUMP)
#include "mpc.h" //_GENE_ 4cpu cdump
#endif
/*! \cond PRIVATE */
#define __FILEID__ rtos
#include "trace.h"
/*! \endcond */

#define EXP_SP_DW_SZ		96		///< exception stack size in dword
#define EXP_CPSR_POS		(EXP_SP_DW_SZ - 1)	///< CPSR position in exception stack
#define EXP_REG_SZ		16			///< backup register count
#define EXP_REG_OFF		(EXP_SP_DW_SZ - EXP_REG_SZ)	///< offset of backup register

#if defined(SAVE_CDUMP)
extern u8 sgfulink_cpu_seq[MAX_MPC];//_GENE_ 4cpu cdump
extern u8 sgfulink_cpu_flag[MAX_MPC];
extern u8 sgfulink;
extern u8 sgfulink_cpu1;
extern u8 sgfulink_cpu2;
extern u8 sgfulink_cpu3;
extern u8 sgfulink_cpu4;
extern u8 avoid_hang_CPU[MAX_MPC];//_GENE_ 4cpu cdump
extern u32 sgexception_sp_cpu1;//_GENE_ 4cpu cdump
extern u32 sgexception_sp_cpu2;//_GENE_ 4cpu cdump
extern u32 sgexception_sp_cpu3;//_GENE_ 4cpu cdump
extern u32 sgexception_sp_cpu4;//_GENE_ 4cpu cdump
extern u32 ulink_getdumpcmd(u32 flag);
extern u32 ulink_da_mode(void);
extern u8 seq;//_GENE_ 4cpu cdump
#endif
extern void misc_resume(enum sleep_mode_t mode);
extern bool panic_occure[4];
extern u32 panic_info[4][9];
#if CO_SUPPORT_PANIC_DEGRADED_MODE
extern bool all_init_done;
#endif
slow_data_ni u32 exception_sp[EXP_SP_DW_SZ];	///< for exception stack
slow_data_zi panic_dump_t *panic_dump_list = NULL;///< dump function list when panic
share_data_zi volatile u8 is_A1_SoC;
#if CO_SUPPORT_PANIC_DEGRADED_MODE
share_data_zi volatile u32 smCPUxAssert;
#endif
share_data volatile u32 shr_frontend_error_cnt;
share_data volatile u32 shr_backend_error_cnt;
share_data volatile u32 shr_ftl_error_cnt;
share_data volatile u32 shr_error_handle_cnt;
share_data volatile u32 shr_misc_error_cnt;
#if defined(SAVE_CDUMP)
ddr_code void ulink_pending_flow(void)
{
	if (2 == ulink_getdumpcmd(1)) {
        extern void cdump_cpu_flow(void);
        cdump_cpu_flow();
	}
}
#endif

#if CO_SUPPORT_PANIC_DEGRADED_MODE
ddr_code void AplEnterDegradedEvent(void)
{
	// Use flag to enter degraded mode in order to reduce
	if (all_init_done)
	{
		#if CPU_ID == 1
		assert_evt_set(evt_degradedMode, 0, 0);
		#else
		//[degrade] TODO HOW TO LET CPU1 KNOW
		{
			smCPUxAssert++;
			__dmb();
		}
		#endif
	}
	else
	{
		// TODO Set enter flag
		smCPUxAssert++;
		__dmb();
	}
}
#endif

/*!
 * @brief do panic and print message
 *
 * @param r0	message which is interger format
 *
 * @return	None
 */
#if defined(SAVE_CDUMP)
ddr_code void do_panic(u32 r0) //_GENE_20210527 fast_code
#else
ddr_code void do_panic(u32 r0)
#endif
{
    #if defined(SAVE_CDUMP)
    #if (CPU_ID == sgfulink_cpu1)
        if (sgfulink_cpu_flag[0] != 2)
            sgfulink_cpu_flag[0] = 1;
    #endif
    #if (CPU_ID == sgfulink_cpu2)
        if (sgfulink_cpu_flag[1] != 2)
            sgfulink_cpu_flag[1] = 1;
    #endif
    #if (CPU_ID == sgfulink_cpu3)
        if (sgfulink_cpu_flag[2] != 2)
            sgfulink_cpu_flag[2] = 1;
    #endif
    #if (CPU_ID == sgfulink_cpu4)
        if (sgfulink_cpu_flag[3] != 2)
            sgfulink_cpu_flag[3] = 1;
    #endif
    #endif
	evlog_printk(LOG_PANIC, "panic %x\n", r0);
	panic_occure[CPU_ID_0] = true;
	#if CO_SUPPORT_PANIC_DEGRADED_MODE
	AplEnterDegradedEvent();
	#endif
	flush_to_nand(EVT_PANIC);
    #if defined(SAVE_CDUMP)
	 do {
		ulink_pending_flow();
    } while (1); /*lint !e506 suppress 'Constant value Boolean'*/
     #endif

}
#if defined(SAVE_CDUMP)
#if 1
extern void ulink_putc(int c);
extern void ulink_putstr(char *s);
extern void ulink_putnum(u32 num);
extern void ulink_putstr_len(char *s, u32 len);
extern void ulink_putnum_2b(u32 num);
extern void ulink_putnum_ascii(u32 num,u32 bytecnt);
extern void ulink_putpad(u32 cnt);
	// } while (1); /*lint !e506 suppress 'Constant value Boolean'*/
#define ULINK_FILE_HEADER_SIZE_MAX 60
#define ULINK_FILE_NAME_LEN_MAX 42
ddr_code void ulink_idwmem_ouart(u32 *mem, u32 bytecnt)
{
	u32 index;
	u32 dwCnt;

	dwCnt = (bytecnt + 3) >> 2;
	for (index = 0; index < dwCnt; index++) {
		ulink_putnum(mem[index]);
	}
}

ddr_code void ulink_ibmem_ouart(u8 *mem, u32 bytecnt)
{
	u32 index;

	for(index = 0; index < bytecnt; index++){
		ulink_putc(mem[index]);
	}
}

ddr_code void ulink_header(u32 cpu, u32 addr, u32 type, u32 size, char *s, u32 file_tail_id)
{
	ulink_putstr("INNO");
	ulink_putnum(ULINK_FILE_HEADER_SIZE_MAX);
	ulink_putnum(size);
	ulink_putc(type);
	ulink_putc(cpu);
	ulink_putnum(addr);
	switch(type){
		case 0:
		{
			u32 len;
			u32 len_Num = 0;
			ulink_putnum_ascii(addr, 4);
			ulink_putc('_');
			len = strlen(s);
			ulink_putstr(s);
			if (file_tail_id < 0x100) {
				len_Num = 3;
				ulink_putc('_');
				ulink_putnum_ascii(file_tail_id, 1);
			}
			ulink_putstr(".mem");
			len = ULINK_FILE_NAME_LEN_MAX - 13 - len - len_Num;
			ulink_putpad(len);
		}
		break;

		case 1:
			ulink_putstr_len(s, ULINK_FILE_NAME_LEN_MAX);
		break;

		case 2:
		{
			u32 len;
			len = strlen(s);
			ulink_putstr(s);
			ulink_putstr(".txt");
			len = ULINK_FILE_NAME_LEN_MAX - 4 - len;
			ulink_putpad(len);
		}
		break;

		case 3:
		{
			u32 len;
			len = strlen(s);
			ulink_putstr(s);
			len = ULINK_FILE_NAME_LEN_MAX - len;
			ulink_putpad(len);
		}
		break;

		default:
		break;
	}
}

ddr_code void ulink_cpu_reqs_p(u32 cpu)
{
	u32 *sp;
	u32 idx;

	if (1 == cpu) {
        extern u32 sgexception_sp_cpu1;
        if (0 == sgexception_sp_cpu1)
            return;
        sp = (u32 *)sgexception_sp_cpu1;
	} else if (2 == cpu) {
		extern u32 sgexception_sp_cpu2;
		if (0 == sgexception_sp_cpu2)
			return;
		sp = (u32 *)sgexception_sp_cpu2;
	} else if (3 == cpu) {
		extern u32 sgexception_sp_cpu3;
		if (0 == sgexception_sp_cpu3)
			return;
		sp = (u32 *)sgexception_sp_cpu3;
	} else if (4 == cpu) {
		extern u32 sgexception_sp_cpu4;
		if (0 == sgexception_sp_cpu4)
			return;
		sp = (u32 *)sgexception_sp_cpu4;
	} else {
		return;
	}
	ulink_header(cpu, 0, 1, 17 * 4, "cpu_regs.p", 0xFFFF);

	for (idx = 0; idx < 15; idx++)
		ulink_putnum(sp[idx + EXP_REG_OFF]);
	ulink_putnum(sp[14 + EXP_REG_OFF]);
	ulink_putnum(sp[EXP_CPSR_POS]);
}

ddr_code u32 ulink_mainmem(u32 cpu)
{
    #if 0
	u32 addr_C2[] = {
		0x00180000,
		0x001A0000
	};
	u32 addr_C3[] = {
		0x00200000,
		0x00220000
	};
	u32 addr_C4[] = {
		0x00280000,
		0x002A0000
	};
        #endif
	u32 addr[] = {
		0x00000000,	//0: ATCM
		0x00080000,	//1: BTCM
		0x000B0000,	//2: BTCM_SH
		0x20000000,	//3: SRAM
		0x20300000	//4: SPRM
	};
	u32 Len[] = {
		0x00020000 - 0x00000000,
		0x000B0000 - 0x00080000,
		0x000D0000 - 0x000B0000,
		0x20100000 - 0x20000000,
		0x20328000 - 0x20300000
	};
	char *str[] = {
		"atcm", //0: ATCM
		"btcm", //1: BTCM
		"shbt", //2: BTCM_SH
		"sram", //3: SRAM
		"sprm"  //4: SPM
	};
	u32 ret = 0;
	u32 mem;
	u32 len;
	u32 addrx;
	u32 i;

	for (i = 0; i < 5; i++) { // ATCM/BTCM/BTCM_SH/SRAM/SPRM
		addrx = mem = addr[i];
        #if 0
		if ((cpu == 2) && (i < 2)) {
			mem = addr_C2[i];
		} else if ((cpu == 3) && (i < 2)) {
			mem = addr_C3[i];
		} else if ((cpu == 4) && (i < 2)) {
			mem = addr_C4[i];
		}
        #endif
		len = Len[i];
		ulink_header(cpu, addrx, 0, len, str[i], 0xFFFF);
		ulink_idwmem_ouart((u32 *)mem, len);
	}
	return ret;
}
#if defined(SAVE_CDUMP)
ddr_data u32 mem[] = {
	0xc0000000,// NCB_ECCU.mem
	0xc0000200,// DEC_TOP_CTRL.mem
	0xc0000300,// ECCU_MDEC0.mem
	0xc0000400,// ECCU_MDEC1.mem
	0xc0000500,// ECCU_PDEC.mem
	0xc0000600,// ECCU_BCH_DEC.mem
	0xc0000700,// ECCU_LDPC_ENC.mem
	0xc0000800,// ECCU_BCH_ENC.mem
	0xc0001000,// NCB_FICU.mem
	0xc0002000,// NCB_NDCU.mem
	0xc0003000,// NPHY0.mem
	0xc0003100,// NPHY1.mem
	0xc0003200,// NPHY2.mem
	0xc0003300,// NPHY3.mem
	0xc0003400,// NPHY4.mem
	0xc0003500,// NPHY5.mem
	0xc0003600,// NPHY6.mem
	0xc0003700,// NPHY7.mem
	0xc0005000,// NCB_MISC.mem
	0xc000f000,// RAID_TOP.mem
	0xc000f100,// NCB_TOP_WRAP_REG.mem
	0xc0010000,// btn_cmd_data_reg.mem
	0xc0030000,// PF_REG.mem
	0xc0033000,// VF_REG.mem
	0xc0034000,// FLEX_DBL.mem
	0xc0035000,// FLEX_ADDR.mem
	0xc0036000,// FLEX_MAP.mem
	0xc0037000,// FLEX_MISC.mem
	0xc003c000,// common_ctrl.mem
	0xc003c200,// port_ctrl.mem
	0xc0040000,// Misc_register.mem
	0xc0043000,// PCIE_Wrapper_register.mem
	0xc0060000,// mc_reg.mem
	0xc0064000,// dfi_reg.mem
	0xc0068000,// ddr_top_register.mem
	0xc00d0000,// btn_ftl_ncl_reg.mem
	0xc0200000,// CPU_VIC.mem
	0xc0201000 // CPU_TIMER.mem
};

ddr_data u32 len[] = {
	0xc0000134 - 0xc0000000,// NCB_ECCU.mem
	0xc0000248 - 0xc0000200,// DEC_TOP_CTRL.mem
	0xc000031c - 0xc0000300,// ECCU_MDEC0.mem
	0xc000041c - 0xc0000400,// ECCU_MDEC1.mem
	0xc000055c - 0xc0000500,// ECCU_PDEC.mem
	0xc0000618 - 0xc0000600,// ECCU_BCH_DEC.mem, *
	0xc0000714 - 0xc0000700,// ECCU_LDPC_ENC.mem
	0xc0000808 - 0xc0000800,// ECCU_BCH_ENC.mem, *
	0xc0001474 - 0xc0001000,// NCB_FICU.mem
	0xc0002620 - 0xc0002000,// NCB_NDCU.mem
	0xc00030f0 - 0xc0003000,// NPHY0.mem
	0xc00031f0 - 0xc0003100,// NPHY1.mem
	0xc00032f0 - 0xc0003200,// NPHY2.mem
	0xc00033f0 - 0xc0003300,// NPHY3.mem
	0xc00034f0 - 0xc0003400,// NPHY4.mem
	0xc00035f0 - 0xc0003500,// NPHY5.mem
	0xc00036f0 - 0xc0003600,// NPHY6.mem
	0xc00037f0 - 0xc0003700,// NPHY7.mem
	0xc0005010 - 0xc0005000,// NCB_MISC.mem
	0xc000f048 - 0xc000f000,// RAID_TOP.mem
	0xc000f16c - 0xc000f100,// NCB_TOP_WRAP_REG.mem
	0xc001088c - 0xc0010000,// btn_cmd_data_reg.mem
	0xc003230c - 0xc0030000,// PF_REG.mem
	0xc00333fc - 0xc0033000,// VF_REG.mem
	0xc0034704 - 0xc0034000,// FLEX_DBL.mem
	0xc0035e24 - 0xc0035000,// FLEX_ADDR.mem
	0xc0036704 - 0xc0036000,// FLEX_MAP.mem
	0xc0037a34 - 0xc0037000,// FLEX_MISC.mem
	0xc003c1fc - 0xc003c000,// common_ctrl.mem
	0xc003c2bc - 0xc003c200,// port_ctrl.mem
	0xc004030c - 0xc0040000,// Misc_register.mem
	0xc00430ec - 0xc0043000,// PCIE_Wrapper_register.mem
	0xc0060400 - 0xc0060000,// mc_reg.mem
	0xc00643dc - 0xc0064000,// dfi_reg.mem
	0xc006801c - 0xc0068000,// ddr_top_register.mem
	0xc00d0730 - 0xc00d0000,// btn_ftl_ncl_reg.mem
	0xc020017c - 0xc0200000,// CPU_VIC.mem, *
	0xc0201048 - 0xc0201000 // CPU_TIMER.mem
};

ddr_data char * str[] = {
	"NCB_ECCU",
	"DEC_TOP_CTRL",
	"ECCU_MDEC0",
	"ECCU_MDEC1",
	"ECCU_PDEC",
	"ECCU_BCH_DEC",
	"ECCU_LDPC_ENC",
	"ECCU_BCH_ENC",
	"NCB_FICU",
	"NCB_NDCU",
	"NPHY0",
	"NPHY1",
	"NPHY2",
	"NPHY3",
	"NPHY4",
	"NPHY5",
	"NPHY6",
	"NPHY7",
	"NCB_MISC",
	"RAID_TOP",
	"NCB_TOP_WRAP_REG",
	"btn_cmd_data_reg",
	"PF_REG",
	"VF_REG",
	"FLEX_DBL",
	"FLEX_ADDR",
	"FLEX_MAP",
	"FLEX_MISC",
	"common_ctrl",
	"port_ctrl",
	"Misc_register",
	"PCIE_Wrapper_register",
	"mc_reg",
	"dfi_reg",
	"ddr_top_register",
	"btn_ftl_ncl_reg",
	"CPU_VIC",
	"CPU_TIMER"
};
#endif

slow_code u32 ulink_register(u32 cpu)
{
    u32 ret = 0;
    u32 i;
    u32 cnt;

	cnt = 38;
	for (i = 0; i < cnt; i++) {
		ulink_header(cpu, mem[i], 0, len[i], str[i], 0xFFFF);
		ulink_idwmem_ouart((u32 *)mem[i], len[i]);
		if (2 == ulink_getdumpcmd(1)) {
			ret = 1;
			break;
		}
	}
	return ret;
}

extern void ulink_putc(int c);
extern void ulink_putstr(char *s);
extern void delay_s(u32 s);
extern u32 ulink_getdumpcmd(u32 flag);
extern void ulink_putstr(char *s);
extern void ulink_putstr_len(char *s, u32 len);
extern void ulink_putnum(u32 num);

ddr_code void ulink_dump_data(u8 cpu)
{


	ulink_putstr("cdumpstart");
	do {
		ulink_cpu_reqs_p(cpu);
		if (ulink_mainmem(cpu)) {
			break;
		}
		if (ulink_register(cpu)) {
			break;
		}
		cpu++;
	} while (cpu < 5);
	ulink_putstr("cdumpend");
}
#endif
#endif

/*!
 * @brief dump register and stack information for kernel debug
 *
 * @param prefetch 	1 for prefetch abort
 *
 * @return		none
 */
ps_code void exception_handler(u32 prefetch)
{
	u32 cpsr = exception_sp[EXP_CPSR_POS];
	u32 mode = 0;
	u32 tmp = 0;
	u32 *stack;
	char *reason[] = {"undefined instruction", "prefetch abort", "data abort"};

#define UND		0x1b
#define SVC		0x13
#define ABT		0x17
#define MOD_MASK	0x1f


	mode = cpsr & MOD_MASK;

#if defined(SAVE_CDUMP)
        u8 cpu_idx = CPU_ID;

        if (ABT == mode) {
            u32 CFLR, FSR, AxFSR, FAR;
			__asm__ __volatile__("mrc p15, 0, %0, c15, c3, 0" : "=r"(CFLR) :: "memory", "cc");			/* Read CFLR */

			if (prefetch) {
				// prefetch instruction exception, get fault info from IFSR/AIFSR/IFAR
				__asm__ __volatile__("mrc p15, 0, %0, c5, c0, 1" :"=r"(FSR):: "memory", "cc");			/* Read IFSR */
				__asm__ __volatile__("mrc p15, 0, %0, c6, c0, 2" :"=r"(FAR):: "memory", "cc");			/* Read IFAR */
				__asm__ __volatile__("mrc p15, 0, %0, c5, c1, 1" :"=r"(AxFSR):: "memory", "cc");		/* Read AIFSR */
			} else {
				__asm__ __volatile__("mrc p15, 0, %0, c5, c0, 0" :"=r"(FSR):: "memory", "cc");			/* Read DFSR */
				__asm__ __volatile__("mrc p15, 0, %0, c6, c0, 0" :"=r"(FAR):: "memory", "cc");			/* Read DFAR */
				__asm__ __volatile__("mrc p15, 0, %0, c5, c1, 0" :"=r"(AxFSR):: "memory", "cc");		/* Read ADFSR */
				extern void mpu_exit(void);
				mpu_exit();
			}
			printk("-- RW %d SD %d STS %d\n", (FSR >> 11) & 1, (FSR >> 12) & 1, ((FSR >> 6) & 0x10) | (FSR & 0xF));
			// RW (0:Read 1:Write), SD:(0:DECERR 1:SLVERR)
			printk("-- Recoverable %d\n", (AxFSR >> 21) & 1);
			// 0: unrecoverable error -> 2-bits error or more, 1: recoverable error -> 1-bit error
			printk("-- Prefetch: %d, CFLR: %x, FSR: %x, AxFSR: %x, Fault Addr:%x\n", prefetch, CFLR, FSR, AxFSR, FAR);
        }
#endif
	switch (mode) {
	case UND:
		tmp = 0;
		break;
	case ABT:
		tmp = 1;
		if (prefetch == 0) {
			tmp++;
		}
		break;
	default:
		break;
	};
    #if defined(SAVE_CDUMP)
    mode = tmp;
    #if CPU_ID == 1
        extern u32 sgexception_sp_cpu1;
        sgexception_sp_cpu1 = (u32)exception_sp;
    #endif

    #if CPU_ID == 2
        extern u32 sgexception_sp_cpu2;
        sgexception_sp_cpu2 = (u32)exception_sp;
    #endif
    #if CPU_ID == 3
        extern u32 sgexception_sp_cpu3;
        sgexception_sp_cpu3 = (u32)exception_sp;
    #endif
    #if CPU_ID == 4
        extern u32 sgexception_sp_cpu4;
        sgexception_sp_cpu4 = (u32)exception_sp;
    #endif
    #endif

	rtos_mmgr_trace(LOG_PANIC, 0x5624, "===> Kernel CRASH!!!");
	rtos_mmgr_trace(LOG_ALW, 0x1a58, "Exception: %s 0x%x", reason[tmp], exception_sp[0]);
	for (tmp = EXP_REG_OFF; tmp < EXP_REG_OFF + 14; tmp += 2) {
		rtos_mmgr_trace(LOG_ALW, 0x8e1c, " R[%d]: 0x%x R[%d]: 0x%x", tmp - EXP_REG_OFF,
				exception_sp[tmp], (tmp - EXP_REG_OFF) + 1,
				exception_sp[tmp + 1]);
	}
	rtos_mmgr_trace(LOG_ALW, 0x685b, " R[14] 0x%x CPSR: 0x%x", exception_sp[EXP_REG_OFF + 14],
			exception_sp[EXP_CPSR_POS]);
	rtos_mmgr_trace(LOG_ALW, 0x0c81, "stack dump:");

	stack = (u32*) exception_sp[EXP_REG_OFF + 13];

	for (tmp = 0; tmp < 64; tmp += 4) {
		rtos_mmgr_trace(LOG_ALW, 0x0387, "%p: %x %x %x %x", &stack[tmp], stack[tmp],
				stack[tmp + 1], stack[tmp + 2], stack[tmp + 3]);
	}
	rtos_mmgr_trace(LOG_ALW, 0xe163, "end of dump");

	#ifndef SAVE_CDUMP
	panic_occure[CPU_ID_0] = true;
	#if CO_SUPPORT_PANIC_DEGRADED_MODE
	AplEnterDegradedEvent();
	#endif
	flush_to_nand(EVT_PANIC);
	#endif

    #if defined(SAVE_CDUMP)
    if (cpu_idx == (sgfulink_cpu1))
    {
        sgfulink_cpu_flag[0] = 2;
    }
    if (cpu_idx == (sgfulink_cpu2))
    {
        sgfulink_cpu_flag[1] = 2;
    }
    if (cpu_idx == (sgfulink_cpu3))
    {
        sgfulink_cpu_flag[2] = 2;
    }
    if (cpu_idx == (sgfulink_cpu4))
    {
        sgfulink_cpu_flag[3] = 2;

        do {
            ulink_getdumpcmd(0);
            ulink_dump_data(sgfulink_cpu4);
            mdelay(1000);
        } while (1); /*lint !e506 suppress 'Constant value Boolean'*/
    }
    #endif
	// do {
	// } while (1); /*lint !e506 suppress 'Constant value Boolean'*/
}

fast_code void panic_dump_add(panic_dump_t *dump)
{
	dump->next = panic_dump_list;
	panic_dump_list = dump;
}

fast_code static void panic_dump(void)
{
	panic_dump_t *dump = panic_dump_list;

	while (dump) {
		dump->dump();
		dump = dump->next;
	}
}

/*!
 * @brief do panic and print message
 *
 * @param str	message which is string format
 *
 * @return	None
 */
#if defined(SAVE_CDUMP)
ddr_code void _panic(const char *str) //_GENE_20210527  fast_code
#else
ddr_code void _panic(const char *str)
#endif
{
	evlog_printk(LOG_PANIC, "panic %s", str);
#if defined(MPC)
	uart_rx_switch(CPU_ID);
#if defined(SAVE_CDUMP)
    u8 cpu_idx;
    cpu_idx = CPU_ID;
    u8 cpu;
    avoid_hang_CPU[seq] = cpu_idx;
    evlog_printk(LOG_PANIC, "0.seq %d cpu id %d avd %d" ,seq,cpu_idx ,avoid_hang_CPU[seq] );
    seq++;

for(cpu=0;cpu<MAX_MPC;cpu++)
{
    evlog_printk(LOG_PANIC, "cpu id %d = hang cpu %d",cpu ,avoid_hang_CPU[cpu]);
}

#endif
#endif

	panic_dump();
	#if (CPU_ID == 2) || (CPU_ID == 4)
	extern void ncl_panic_dump(void);
	ncl_panic_dump();
	#endif
	#ifndef SAVE_CDUMP    //
	panic_occure[CPU_ID_0] = true;
	#if CO_SUPPORT_PANIC_DEGRADED_MODE
	AplEnterDegradedEvent();
	#endif
	flush_to_nand(EVT_PANIC);
	#endif

    #if defined(SAVE_CDUMP)
	do {
 		extern void uart_poll(void);
 		uart_poll();
            ulink_pending_flow();
 	} while (1); /*lint !e506 suppress 'Constant value Boolean'*/
    #endif
}

ddr_code void update_error_log_cnt_and_show_func_line(const char *func, int line)
{
	evlog_printk(LOG_PANIC, "%s %d\n", func, line);
	if(func[1] == '\0' || func[2] == '\0')
		shr_misc_error_cnt++;
	else if(((*(u32 *)func) & 0x00FFFFFF) == ('b' | ('t' << 8) | ('n' << 16)))
		shr_frontend_error_cnt++;
	else if( (*(u32 *)func)               == ('n' | ('a' << 8) | ('n' << 16) | ('d' << 24)))
		shr_backend_error_cnt++;
	else if(((*(u32 *)func) & 0x00FFFFFF) == ('f' | ('t' << 8) | ('l' << 16)))
		shr_ftl_error_cnt++;
	else if( (*(u32 *)func)               == ('f' | ('i' << 8) | ('c' << 16) | ('u' << 24)))
		shr_error_handle_cnt++;
	else
		shr_misc_error_cnt++;

	if(panic_occure[CPU_ID_0] == false){
		panic_info[CPU_ID_0][0] = (*(u32 *)func);
		panic_info[CPU_ID_0][1] = ((*((u32 *)func + 1)));
		panic_info[CPU_ID_0][2] = ((*((u32 *)func + 2)));
		panic_info[CPU_ID_0][3] = ((*((u32 *)func + 3)));
		panic_info[CPU_ID_0][4] = ((*((u32 *)func + 4)));
		panic_info[CPU_ID_0][5] = ((*((u32 *)func + 5)));
		panic_info[CPU_ID_0][6] = ((*((u32 *)func + 6)));
		panic_info[CPU_ID_0][7] = ((*((u32 *)func + 7)));
		panic_info[CPU_ID_0][8] = line;
	}
}

/*! @brief simple timer */
fast_data_zi u32 cticks;

/*! @brief simple heap mem mgr */
static fast_data_zi heap_t *mem_heap[MAX_MEM_TYPE];

/*!
 * @brief allocate memory
 *
 * @param type	memory type
 * @param size	how many size memory need to allocated
 *
 * @return	None
 */
fast_code void *sys_malloc(mem_type_t type, u32 size)
{
    //evlog_printk(LOG_ALW, "sys_malloc %d %d", type, size);
	if (mem_heap[type] == NULL) {
		panic("mem not ready yet.");
		return NULL;
	}
	return heap_alloc(mem_heap[type], size);
}

/*!
 * @brief allocate aligned memory, must be 2's power
 *
 * Some memory for hardware access need special alignment
 *
 * @param type		memory type, FAST_DATA or SLOW_DATA
 * @param size		requested memory size
 * @param aligned	aligned number
 *
 * @return 		Allocated memory pointer or NULL
 */
fast_code void* sys_malloc_aligned(mem_type_t type, u32 size,
		u32 aligned)
{
	void *mem;
	void **ret;

	size += sizeof(void*) + aligned;
	mem = sys_malloc(type, size);
	if (mem == NULL)
		return NULL;

	ret = (void**) ((((u32) mem) + aligned + sizeof(void*)) & (~(aligned - 1)));
	ret[-1] = mem;

	return ret;
}

/*!
 * @brief free memory
 *
 * @param type	memory type
 * @param ptr	buffer to add to free list
 *
 * @return	None
 */
fast_code void sys_free(mem_type_t type, void *ptr)
{
	// Temp workaround, current there is no PS_DATA
	if (mem_heap[type] == NULL) {
		panic("mem not ready yet.");
	}
	heap_free(mem_heap[type], ptr);
}

/*!
 * @brief free memory pointer which allocated by sys_malloc_aligned
 *
 * @param type	memory type, FAST_DATA or SLOW_DATA
 * @param ptr	pointer to be free
 *
 * @return 	None
 */
fast_code void sys_free_aligned(mem_type_t type, void *ptr)
{
	void** mem = (void**) ptr;

	sys_free(type, mem[-1]);
}

static init_code void sys_mem_init(void)
{
	struct mem_array {
		int type;
		u32 start, end;
	} array[] = {
		{ .type  = FAST_DATA,
		  .start = (u32)&__btcm_free_start,
		  .end   = (u32)&__btcm_free_end,
		},

		{ .type  = SLOW_DATA,
		  .start = (u32)&__sram_free_start,
		  .end   = (u32)&__sram_free_end,
		},

		{ .type  = PS_DATA,
		  .start = (u32)&__ps_free_start,
		  .end   = (u32)&__ps_free_end,
		},

	};
	u32 i;

	for (i = 0; i < ARRAY_SIZE(array); i ++) {
		struct mem_array *m = &array[i];
		int size = m->end - m->start;
		rtos_mmgr_trace(LOG_DEBUG, 0x3de9, "%d: size=%d, start=0x%x",
				m->type, size, m->start);
		mem_heap[m->type] = heap_init((void *)m->start, size);
	}
}

/*!
 * @brief pmu callback for rtos core when pmu suspend
 *
 * @param mode		sleep mode
 *
 * @return		always return true
 */
ddr_code bool rtos_core_pmu_suspend(enum sleep_mode_t mode)
{
	u32 size;
	void *buf;

	size = heap_get_mblock_size(mem_heap[SLOW_DATA]);
	buf = sys_malloc(PS_DATA, size);
	if (buf == NULL)
		panic("Lack ps mem.");
	heap_save_mblock_struct(mem_heap[SLOW_DATA], buf);
	mem_heap[SLOW_DATA] = (heap_t *)buf;
	size = heap_get_mblock_size(mem_heap[FAST_DATA]);
	buf = sys_malloc(PS_DATA, size);
	if (buf == NULL)
		panic("Lack ps mem.");
	heap_save_mblock_struct(mem_heap[FAST_DATA], buf);
	mem_heap[FAST_DATA] = (heap_t *)buf;
#if CPU_ID == 1
	pmu_swap_file_register((void *)BTCM_SH_BASE, (u32)&__sh_btcm_free_start - BTCM_SH_BASE);
	pmu_swap_file_register((void *)&__ucmd_start, (u32)&__sram_free_start - (u32)&__ucmd_start);
#elif CPU_ID != 2
	pmu_swap_file_register((void *)btcm_dma_base, (u32)&_end_bss - BTCM_BASE);
	pmu_swap_file_register((void *)&__ucmd_start, (u32)&__sram_free_start - (u32)&__ucmd_start);
#endif

	return true;
}

/*!
 * @brief pmu callback for rtos core when pmu resume
 *
 * @param mode		sleep mode
 *
 * @return		not used
 */
ddr_code void rtos_core_pmu_resume(enum sleep_mode_t mode)
{
	void *buf;

	buf = (void *)mem_heap[SLOW_DATA];
	mem_heap[SLOW_DATA] = (heap_t *)&__sram_free_start;
	heap_restore_mblock_struct(mem_heap[SLOW_DATA], buf);
	sys_free(PS_DATA, buf);

	buf = (void *)mem_heap[FAST_DATA];
	mem_heap[FAST_DATA] = (heap_t *)&__btcm_free_start;
	heap_restore_mblock_struct(mem_heap[FAST_DATA], buf);
	sys_free(PS_DATA, buf);
#if defined(RDISK) && (CPU_ID == 2)
	void pmu_swap_mem_restore(bool resume);
	pmu_swap_mem_restore(true);
#endif
	misc_resume(mode);
}

fast_code bool icache_ctrl(bool enable)
{
	u32 sctlr;
	bool saved;

	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) :: "memory", "cc");
	if (sctlr & BIT(12))
		saved = true;
	else
		saved = false;

	if (saved == enable)
		return saved;	// setting was not changed

	if (enable) {
		sctlr |= BIT(12);
		__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");
		__asm__ __volatile__("ISB");
		rtos_mmgr_trace(LOG_ERR, 0x48b6, "icached en\n");
	} else {
		sctlr &= ~BIT(12);
		__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");
		__asm__ __volatile__("ISB");
		rtos_mmgr_trace(LOG_ERR, 0xb5a4, "icached dis\n");
	}

	return saved;
}

static init_code void rtos_core_init(void)
{
	exception_sp[0] = 0xdeadbeef;
	sys_mem_init();
	sys_event_init();

#if CPU_ID == 1

#ifndef MDOT2_SUPPORT
	extern slow_code u32 pr_write(u8 cmd_code, u16 index, u8 type);
	if (pr_write(0, 85, 0) == 0){  // set SY8827 cpu core power 0.85V, 12.5mv per step
		rtos_mmgr_trace(LOG_ERR, 0xbfcc, "Vdd 0.85V\n");
	}
	else{
		rtos_mmgr_trace(LOG_ERR, 0xbcf6, "Power IC is Ti Vdd 0.85V\n");
	}
#endif

	gpio_dump();
	writel(0x4400, (void *)0xC0068014);  // bit[8:15] P1 QoS highest priority

  extern bool is_rainier_a1();
  is_A1_SoC = is_rainier_a1();

#endif
#if defined(MPC)
	cpu_msg_init();
	spin_lock_init();
#endif
#if defined(RDISK)
	evlog_agt_init();
#if CPU_ID == 2
	pmu_register_handler(SUSPEND_COOKIE_PLATFORM, NULL,
			RESUME_COOKIE_PLATFORM, rtos_core_pmu_resume);
#else
	pmu_register_handler(SUSPEND_COOKIE_PLATFORM, rtos_core_pmu_suspend,
			RESUME_COOKIE_PLATFORM, rtos_core_pmu_resume);
#endif
#endif
#if (defined(USE_CRYPTO_HW) && (CPU_ID == 1))
	trng_init();
#endif /* USE_CRYPTO_HW */

#if CPU_ID == 1
	#ifdef MDOT2_SUPPORT
	#if ((Tencent_case) || (RD_VERIFY))
		#if (FIX_I2C_SDA_ALWAYS_LOW_ISSUE == mENABLE)
			extern bool _fg_warm_boot;
			if ((_fg_warm_boot == false) && (misc_is_warm_boot() == false))
			{
				extern void i2c_bus_validation_test(void);
				i2c_bus_validation_test();
			}
		#endif
	#endif
	#endif
#endif
}

module_init(rtos_core_init, RTOS_CORE);

/*lint -e{438, 530} sctlr is used and initialized*/
static ps_code int icache_main(int argc, char *argv[])
{
	u32 sctlr;
	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) :: "memory", "cc");

	rtos_mmgr_trace(LOG_ERR, 0x0a7c, "\n");
	if (argc == 1) {
		rtos_mmgr_trace(LOG_ERR, 0x633e, "icache: %s", sctlr & BIT(12) ? "on" : "off");
		return 0;
	}

	if (strstr(argv[1], "on") &&
			(!(sctlr & BIT(12)))) {
		rtos_mmgr_trace(LOG_ERR, 0x40d2, "Enable Icache ... ");
		sctlr |= BIT(12);
		__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");
		__asm__ __volatile__("ISB");
		rtos_mmgr_trace(LOG_ERR, 0xfa05, "Done");
	}
	else if (strstr(argv[1], "off") &&
			(sctlr & BIT(12))) {
		rtos_mmgr_trace(LOG_ERR, 0x0ea4, "Disable Icache ... ");
		sctlr &= ~BIT(12);
		__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");
		__asm__ __volatile__("ISB");
		rtos_mmgr_trace(LOG_ERR, 0x2830, "Done");
	}
	else {
		rtos_mmgr_trace(LOG_ERR, 0xebb0, "instruction cache status no changed.");
	}

	return 0;
}

static DEFINE_UART_CMD(icache, "icache",
		"icache [on|off]",
		"status or on/off toggle",
		0, 1, icache_main);
