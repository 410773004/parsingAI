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

#define ENTRY(name) \
.text; \
.section .tcm_code.\name; \
.global name ;\
.align 4 ;\
.code 32 ;\
name:

#define ENDPROC(name) \
.type name, %function ; \
.size name, .-name

.macro bitop instr
    ands    ip, r1, #3
    strneb  r1, [ip]            @ assert word-aligned
    mov     r2, #1
    and     r3, r0, #31         @ get bit offset
    mov     r0, r0, lsr #5
    add     r1, r1, r0, lsl #2  @ get word offset
    /* SMP ? */
    mov     r3, r2, lsl r3
    ldr     r2, [r1]
    \instr  r2, r2, r3
    str     r2, [r1]
    bx      lr
.endm

.macro testop instr
    ands   ip, r1, #3
    strneb r1, [ip]           @ assert word-aligned
    mov    r2, #1
    and    r3, r0, #31        @ get bits offset
    mov    r0, r0, lsr #5
    add    r1, r1, r0, lsl #2 @ get word offset
    mov    r3, r2, lsl r3     @ create mask
1:  ldrex  r2, [r1]
    ands   r0, r2, r3         @ save old value of bit
    \instr r2, r2, r3         @ toggle bit
    strex  ip, r2, [r1]
    cmp    ip, #0
    bne    1b
    cmp    r0, #0
    movne  r0, #1
    bx     lr
.endm

ENTRY(change_bit)
 bitop eor
ENDPROC(change_bit)

ENTRY(clear_bit)
 bitop bic
ENDPROC(clear_bit)

ENTRY(set_bit)
 bitop orr
ENDPROC(set_bit)

ENTRY(test_and_change_bit)
 testop eor
ENDPROC(test_and_change_bit)

ENTRY(test_and_clear_bit)
 testop bicne
ENDPROC(test_and_clear_bit)

ENTRY(test_and_set_bit)
 testop orreq
ENDPROC(test_and_set_bit)

.macro _find_first_zero_bit, op, r1, r2, r3
    teq  r1, #0
    beq  3f
    mov  r2, #0
.ifeqs "\op", "nop"
.else
    \op \r1, \r2, \r3
.endif
1:  ldrb r3, [r0, r2, lsr #3]
    eors r3, r3, #0xff @ invert bits
    bne  .L_found      @ any now set - found zero bit
    add  r2, r2, #8    @ next bit pointer
2:  cmp  r2, r1        @ any more?
    blo  1b
3:  mov  r0, r1        @ no free bits
    bx   lr
.endm

.macro _find_first_bit, op, r1, r2, r3
    teq  r1, #0
    beq  3f
    mov  r2, #0
.ifeqs "\op", "nop"
.else
	\op \r1, \r2, \r3
.endif
1:  ldrb r3, [r0, r2, lsr #3]
    movs r3, r3
    bne  .L_found     @ any now set - found zero bit
    add  r2, r2, #8   @ next bit pointer
2:  cmp  r2, r1       @ any more?
    blo  1b
3:  mov  r0, r1       @ no free bits
    bx   lr
.endm

/*
 * Purpose  : Find a 'zero' bit
 * Prototype: int find_first_zero_bit(void *addr, unsigned int maxbit);
 */
ENTRY(find_first_zero_bit)
_find_first_zero_bit nop
ENDPROC(find_first_zero_bit)

.macro _find_next_zero_bit, op, r1, r2, r3
    teq   r1, #0
    beq   3b
    ands  ip, r2, #7
    beq   1b            @ If new byte, goto old routine
.ifeqs "\op", "nop"
.else
	\op \r1, \r2, \r3
.endif
    lsr   r3, r2, #3
    ldrb  r3, [r0, r3]
    eor   r3, r3, #0xff @ now looking for a 1 bits
    movs  r3, r3, lsr ip @ shift off unused bits
    bne   .L_found
    orr   r2, r2, #7    @ if zero, then no bits here
    add   r2, r2, #1    @ align bit pointer
    b     2b            @ loop for next bit
.endm

.macro _find_next_bit, op, r1, r2, r3
    teq  r1, #0
    beq  3b
    ands ip, r2, #7
    beq  1b
.ifeqs "\op", "nop"
.else
	\op \r1, \r2, \r3
.endif
    ldrb r3, [r0, r2, lsr #3]
    movs r3, r3, lsr ip @ shift off unused bits
    bne  .L_found
    orr  r2, r2, #7     @ if zero, the no bits here
    add  r2, r2, #1     @ align bit pointer
    b    2b             @ loop for next bit
.endm

/*
 * Purpose  : Find next 'zero' bit
 * Prototype: int find_next_zero_bit(void *addr, unsigned int maxbit, int offset)
 */
ENTRY(find_next_zero_bit)
_find_next_zero_bit nop
ENDPROC(find_next_zero_bit)

/*
 * Purpose  : Find a 'one' bit
 * Prototype: int _find_first_bit(const unsigned long *addr, unsigned int maxbit);
 */
ENTRY(find_first_bit)
_find_first_bit nop
ENDPROC(find_first_bit)

/*
 * Purpose  : Find next 'one' bit
 * Prototype: int _find_next_bit(void *addr, unsigned int maxbit, int offset)
 */
ENTRY(find_next_bit)
_find_next_bit nop
ENDPROC(find_next_bit)

ENTRY(find_first_zero_bit_be)
_find_first_zero_bit   eor, r3, r2, #0x18
ENDPROC(find_first_zero_bit_be)

ENTRY(find_next_zero_bit_be)
_find_next_zero_bit    eor, r3, r2, #0x18
ENDPROC(find_next_zero_bit_be)

ENTRY(find_first_bit_be)
_find_first_bit        eor, r3, r2, #0x18
ENDPROC(find_first_bit_be)

ENTRY(find_next_bit_be)
_find_next_bit         eor, r3, r2, #0x18
ENDPROC(find_next_bit_be)

ENTRY(.L_found)
    rsb  r0, r3, #0
    and  r3, r3, r0
    clz  r3, r3
    rsb  r3, r3, #31
    add  r0, r2, r3
    cmp  r1, r0       @ clamp to maxbit
    movlo r0, r1
    bx   lr
ENDPROC(.L_found)
