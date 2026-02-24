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

.equ PMU_CTRL,     0xC0040080
.equ PMU_STATUS,   0xC0040084
.equ PMU_DONE,     (1 << 0)

.equ FW_STTS_3,    0xC00400CC
.equ FW_STTS_4,    0xC00400D0
.equ FW_STTS_5,    0xC00400D4
.equ FW_STTS_6,    0xC00400D8
.equ FW_STTS_7,    0xC00400DC

.equ MAC_PD_CTRL,  0xC00400A4
.equ TIMER4_CTRL,  0xC0040140
.equ TIMER4_VAL_L, 0xC0040144

.equ ATCM_BASE,    0x00000000
.equ ATCM_SIZE,    0x00020000

.equ BTCM_BASE,    0x00080000
.equ BTCM_SIZE,    0x00040000

.equ RAID_BASE,    0x20200000
.equ RAID_SIZE,    0x00018000

.equ PMU_WCNT,     0x400

.text
.align 4
.code 32
.section .tcm_code.pmu  /* avoid venner jump */
.type pmu_enable, %function
.size pmu_enable, .-pmu_enable
.global pmu_enable
pmu_enable:
	push  {r14}               @ save lr
	mrs   r14, cpsr           @ we can use R14 as common register
	push  {r14}               @ save CPSR
	mrs   r14, spsr
	push  {r14}               @ save SPSR
	push  {r0-r12}            @ save common r0-r12 registers

	ldr   r0, =FW_STTS_7
	str   sp, [r0]            @ save irq sp to pmu register

	ldr   r0, =FW_STTS_6
	ldr   r2, [r0]            @ r80 value in pmu_enter

	ldr   r1, =power_resume
	str   r1, [r0]

	ldr   r0, =PMU_CTRL
	str   r2, [r0]            @ start PMU
cpu_halt:
	nop                       @ PC PFU prefetch
	nop
	nop
	ldr  r0,   =PMU_STATUS    @Check PMU_STATUS
	ldr  r1,   [r0]
	tst  r1,   #PMU_DONE
	beq  cpu_halt 		@ PMU Done set

	/* resume handler */
power_resume:
.if 0
	/* CPU PFU for branch prediction */
	mrc     p15, 0, r0, c1, c0, 1   @ ACTLR
	orr     r0, r0, #(0x1 << 17)

	bic     r0, r0, #(0x1 << 15)
	orr     r0, r0, #(0x1 << 16)

	orr     r0, r0, #(0x1 << 20)
	orr     r0, r0, #(0x1 << 21)
	mcr     p15, 0, r0, c1, c0, 1
.endif

.if 0
	/* Enable Timer */
	mov     r0, #2
	mov     r1, #1
	ldr     r2, =TIMER4_CTRL
	str     r0, [r2]
	str     r1, [r2]

	ldr     r2, =TIMER4_VAL_L
	ldr     r9, [r2]
.endif

.if 0
.if 0
	ldr  r0, =MAC_PD_CTRL
mac_pmu_state:                    @ Mac Quit state 7, 8, 9, A
	ldr  r1, [r0]
	ubfx r3, r1, #20, #4
	cmp  r3, #2
	bne  mac_pmu_state

#	ldr  r10, [r2]
.else
	movw r0, #(PMU_WCNT & 0xFFFF)
	movt r0, #(PMU_WCNT >> 16)
cont:
	subs r0, #1
	bne cont

#	ldr   r11, [r2]
.endif
.endif

	/* switch to normal exception vector after return from ROM */
	mrc     p15, 0, r0, c1, c0, 0   @ SCTLR
	bic     r0, r0, #(0x1 << 13)    @ use low vector
	dsb
	mcr     p15, 0, r0, c1, c0, 0   @ SCTLR
	isb

	mrs   r0, cpsr            @ get CPSR
	bic   r0, r0, #0x1F       @ clear mode bits
	orr   r0, r0, #0x13       @ SVC_mode
	orr   r0, r0, #0xC0       @ disable FIQ and IRQ
	msr   cpsr, r0            @ switch to SVC_mode for Recovery

	ldr   r0, =FW_STTS_7
	ldr   sp, [r0]            @ restore IRQ SP

	pop   {r0-r12}
	pop   {r14}               @ pop old SPSR
	msr   spsr_cxsf, r14
	pop   {r14}               @ pop old CPSR
	msr   cpsr_cxsf, r14
	pop   {r14}               @ restore old LR

	bx    lr
