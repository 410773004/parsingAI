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

.text
.align 4
.code 32
.section .vectors

.globl _start
_start:
	/*b     reset*/
/*00*/	ldr	pc, _reset
/*04*/	b	undefined_instruction
/*08*/	b	software_interrupt
/*0c*/	b	prefetch_abort
/*10*/	b	data_abort
/*14*/	b	not_used
/*18*/	ldr	pc, _irq
/*1c*/	b	fiq

_reset: .word reset
_irq:   .word irq

.global do_exception
do_exception:
	sub     sp, sp, #0x44
	stmfa   sp!, {r0-r12}
	mrs     r0, cpsr
	mov     r4, r0
	and     r1, r0, #0x1f       @ mask mode bits
	bic     r0, r0, #0x1f       @ clear all mode bits
	orr     r0, r0, #0x13       @ set SVC mode
	orr     r0, r0, #0xc0       @ disable FIQ and IRQ
	msr     cpsr, r0

	mov     r2, r13
	mov     r3, r14
	bic     r0, r0, #0x1f
	orr     r0, r0, r1
	msr     cpsr, r0
	stmfa   sp!, {r2-r4}
	sub     sp, sp, $0x44

	mov     r0, lr
	ldr     r12, =exception_handler
	mov     lr, pc
	bx      r12
	b .

.global undefined_instruction
undefined_instruction:
	ldr     sp, =exception_sp
	add     sp, sp, #0x180
	b       do_exception

.global software_interrupt
software_interrupt:
	stmfd   sp!, {r0-r3,r12,lr}
	ldr     r12, =do_panic
	mov     lr, pc
	bx      r12
	b .

.global prefetch_abort
prefetch_abort:
	ldr     sp, =exception_sp
	str	lr, [sp]
	add     sp, sp, #0x180
	mov     lr, #0x1
	b       do_exception

.global data_abort
data_abort:
	ldr     sp, =exception_sp
	str	lr, [sp]
	add     sp, sp, #0x180
	mov     lr, #0x0
	b       do_exception

.global not_used
not_used:
	stmfd   sp!, {r0-r3,r12,lr}
	ldr     r12, =do_panic
	mov     lr, pc
	bx      r12
	b .

.global irq
irq:
	stmfd   sp!, {r0-r3,r12,lr}
	ldr     r12, =_record_lr
	str lr, [r12]
	ldr     r12, =do_irq
	mov     lr, pc
	bx      r12

	ldmfd sp!, {r0-r3,r12,lr}
	subs pc, lr, #4

.global fiq
fiq:
	stmfd   sp!, {r0-r3,r12,lr}
	ldr     r12, =do_panic
	mov     lr, pc
	bx      r12
	b .

.equ USR_MODE,            0x10
.equ FIQ_MODE,            0x11
.equ IRQ_MODE,            0x12
.equ SVC_MODE,            0x13
.equ ABT_MODE,            0x17
.equ UND_MODE,            0x1B
.equ SYS_MODE,            0x1F

.equ MPU_BIT,             (1 << 0)     /* MPU */
.equ I_BIT,               (1 << 7)     /* when I bit is set, IRQ is disabled */
.equ F_BIT,               (1 << 6)     /* when F bit is set, FIQ is disabled */
.equ V_BIT,               (1 << 13)    /* SCTLR exception vector */
.equ SCTLR_D,             (1 << 2)
.equ SCTLR_I,             (1 << 12)

.equ FW_STTS_0,           0xC00400C0

#define _reset_sec(x, y)	x ## y
#define reset_sec(cpu_id)	_reset_sec(.tcm_code., cpu_id)

.code 32
.section reset_sec(CPU_ID_0)
.globl	reset
reset:
	/*
	 * disable interrupts (FIQ and IRQ), also set the cpu to SVC32 mode
	 */
	mrs	r0, cpsr
	and	r1, r0, #0x1f		@ mask mode bits
	teq	r1, #0x1a		@ test for HYP mode
	bicne	r0, r0, #0x1f		@ clear all mode bits
	orrne	r0, r0, #0x13		@ set SVC mode
	orr	r0, r0, #0xc0		@ disable FIQ and IRQ
	msr	cpsr,r0

	msr     cpsr, #(IRQ_MODE | I_BIT | F_BIT)
	ldr     sp, =__irq_stack_base

	msr     cpsr, #(SVC_MODE | I_BIT | F_BIT)
	ldr     sp, =__svc_stack_base
	bic     sp, sp, #7              /* 8-byte alignment for ABI compliance */

	mov     r0, #0
	bl      c_runtime_cpu_setup

	mov     r1, #0
	ldr     r1, =FW_STTS_0
	ldr     r0, [r1]
	mov     r1, #0
	cmp     r0, r1
	bne     pmu_en_l

	ldr     r0, =_start_bss
	ldr     r1, =_end_bss
	mov     r2, #0x00000000		/* prepare zero to clear BSS */

clbss_l:
        cmp	r0, r1			/* while not at end of BSS */
	strlo	r2, [r0]		/* clear 32-bit BSS word */
	addlo	r0, r0, #4		/* move to next */
	blo	    clbss_l

	ldr     lr, =main
	bx      lr

pmu_en_l:
	ldr     lr, =pmumain
	bx      lr

c_runtime_cpu_setup:
	mrc     p15, 0, r1, c1, c0, 0   @ read SCTLR
	bic     r1,  r1, #SCTLR_D
	orr     r1,  r1, #SCTLR_I
	bic     r1,  r1, #MPU_BIT
	bic     r1,  r1, #V_BIT         @ Clear V, use normal vector
	mcr     p15, 0, r0, c7, c10, 4	@ DSB
	mcr     p15, 0, r0, c15,c5, 0   @ invalidate entire data cache
	mcr	    p15, 0, r0, c7, c5, 0	@ invalidate entire instruction cache
	mcr     p15, 0, r1, c1, c0, 0   @ enabled cache RAMs, write SCTLR
	mcr     p15, 0, r0, c7, c5, 4	@ ISB
	bx      lr

.text
.align 4
.code 32
.section .tcm_code.fastcpy
.type fastcpy, %function
.size fastcpy, .-fastcpy
.global fastcpy
fastcpy:
     /*
      * fast copy blocks of data by load and store multiple register
      * note: the copy data size must be aligned with 32 bytes
      */
      PUSH {r3-r10}
fastloop:
      LDMIA r1!, {r3 - r10}
      STMIA r0!, {r3 - r10}
      SUBS r2, r2, #32
      BNE fastloop
      POP {r3-r10}
      bx lr
