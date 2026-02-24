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

/**
  * tk_frame
  *
  * Setup initial stack frame for new task.
  *
  */
.section .tcm_code.task_frame
.code 32
.global task_frame
.type   task_frame, %function

	/* task_frame(task_t *ts, int(*start)(int), int param); r0 = ts, r1 = entry, r2 = param */
task_frame:
	stmfd	sp!, {r8-r9}

	/* get a pointer to end of the stack area 
	 * r8 = ts->stack + ts->stsize 
	 */
	ldr     r8, [r0, #0x8]
	ldr     r9, [r0, #0xc]
	add     r8, r8, r9

	/* align it (downward)
	 * r8 &= ~3 
	 */
	bic     r8, r8, #3

	/* build the initial stack frame below there */
	/* link register, will end up in r14/lr
	 * don't save anything for r13/sp */
	stmfd   r8!, {r1} 
	/* filler for r8-r12 */
	stmfd   r8!, {r8-r12}
	/* param as r0, and filler for r4-r7 */
	stmfd   r8!, {r2, r4-r7}

	/* ts->fp = r8 */
	str     r8, [r0, #0x4]

	/* return ts->sp */
	mov     r0, r8

	ldmfd  sp!, {r8-r9}
	mov    pc, lr

.section .tcm_code.task_switch
.code 32
.global task_switch
.type   task_switch, %function
	/* void task_switch(task_t *ts); r0 = ts */
task_switch:
	/* save old-current task's registers on the stack */
	stmfd  sp!, {r0, r4-r12, r14} 

	/* save old-current tasks' stack pointer
	 * task_cur->fp = sp 
	 */
	ldr    r2,  1f
	ldr    r1,  [r2]
	str    sp,  [r1, #0x4]

	/* get the new task's stack pointer */
	ldr    sp,  [r0, #0x4]
	/* make the new task current */
	str    r0,  [r2]

	/* restore new-current task's saved registers from stack */
	ldmfd  sp!, {r0, r4-r12, r14}
	/* return into new-current task */
	mov    pc, lr

	.align
1:     .word task_cur
	.ltorg

.section .tcm_code.task_getsp
.code 32
.global task_getsp
.type   task_getsp, %function
task_getsp:
	/* setup to return current stack pointer */
	mov    r0, sp
	/* return */
	mov    pc, lr
