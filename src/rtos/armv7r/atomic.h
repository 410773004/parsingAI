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

#ifndef _ATOMIC_H_
#define _ATOMIC_H_

/*!
 * \file atomic.h
 *
 */

/*!
 * @brief type of data in atomic_t
 */
typedef struct {
	u32 data;    ///< data inside atomic_t
} atomic_t;

/*!
 * @brief
 *
 * ARMv6 UP and SMP safe atomic ops.  We use load exclusive and
 * store exclusive to ensure that these are atomic.  We may loop
 * to ensure that the update happens.
 */
#define ATOMIC_OP(op, c_op, asm_op)				\
/*lint -e{529}*/						\
static inline __attribute__((always_inline)) void atomic_##op(int i, atomic_t *v)	\
{								\
	u32 tmp;						\
	int result;						\
								\
	__asm__ __volatile__("@ atomic_" #op "\n"		\
"1:	ldrex	%0, [%3]\n"					\
"	" #asm_op "	%0, %0, %4\n"				\
"	strex	%1, %0, [%3]\n"					\
"	teq	%1, #0\n"					\
"	bne	1b"						\
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->data)		\
	: "r" (&v->data), "Ir" (i)				\
	: "cc");						\
}								\

#define ATOMIC_OP_RETURN(op, c_op, asm_op)			\
/*lint -e{529, 530}*/						\
static inline __attribute__((always_inline)) int atomic_##op##_return_relaxed(int i, atomic_t *v)	\
{								\
	u32 tmp;						\
	int result;						\
								\
	__asm__ __volatile__("@ atomic_" #op "_return\n"	\
"1:	ldrex	%0, [%3]\n"					\
"	" #asm_op "	%0, %0, %4\n"				\
"	strex	%1, %0, [%3]\n"					\
"	teq	%1, #0\n"					\
"	bne	1b"						\
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->data)		\
	: "r" (&v->data), "Ir" (i)				\
	: "cc");						\
								\
	return result;						\
}

#define ATOMIC_FETCH_OP(op, c_op, asm_op)			\
/*lint -e{529, 530}*/						\
static inline __attribute__((always_inline)) int atomic_fetch_##op##_relaxed(int i, atomic_t *v)	\
{								\
	u32 tmp;						\
	int result, val;					\
								\
	__asm__ __volatile__("@ atomic_fetch_" #op "\n"		\
"1:	ldrex	%0, [%4]\n"					\
"	" #asm_op "	%1, %0, %5\n"				\
"	strex	%2, %1, [%4]\n"					\
"	teq	%2, #0\n"					\
"	bne	1b"						\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Qo" (v->data)	\
	: "r" (&v->data), "Ir" (i)				\
	: "cc");						\
								\
	return result;						\
}

#define atomic_add_return_relaxed	atomic_add_return_relaxed
#define atomic_sub_return_relaxed	atomic_sub_return_relaxed
#define atomic_fetch_add_relaxed	atomic_fetch_add_relaxed
#define atomic_fetch_sub_relaxed	atomic_fetch_sub_relaxed

#define atomic_fetch_and_relaxed	atomic_fetch_and_relaxed
#define atomic_fetch_andnot_relaxed	atomic_fetch_andnot_relaxed
#define atomic_fetch_or_relaxed		atomic_fetch_or_relaxed
#define atomic_fetch_xor_relaxed	atomic_fetch_xor_relaxed

/*lint -e{529, 530}*/
static inline __attribute__((always_inline)) int atomic_cmpxchg_relaxed(atomic_t *ptr, int old, int new)
{
	int oldval;
	unsigned long res;

#if 0
	prefetchw(&ptr->data);
#endif

	do {
		__asm__ __volatile__("@ atomic_cmpxchg\n"
		"ldrex	%1, [%3]\n"
		"mov	%0, #0\n"
		"teq	%1, %4\n"
		"strexeq %0, %5, [%3]\n"
		    : "=&r" (res), "=&r" (oldval), "+Qo" (ptr->data)
		    : "r" (&ptr->data), "Ir" (old), "r" (new)
		    : "cc");
	} while (res);

	return oldval;
}
//#define atomic_cmpxchg_relaxed		atomic_cmpxchg_relaxed

/*lint -e{529, 530}*/
static inline __attribute__((always_inline)) int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int oldval, newval;
	unsigned long tmp;

#if 0
	smp_mb();
	prefetchw(&v->data);
#endif

	__asm__ __volatile__ ("@ atomic_add_unless\n"
"1:	ldrex	%0, [%4]\n"
"	teq	%0, %5\n"
"	beq	2f\n"
"	add	%1, %0, %6\n"
"	strex	%2, %1, [%4]\n"
"	teq	%2, #0\n"
"	bne	1b\n"
"2:"
	: "=&r" (oldval), "=&r" (newval), "=&r" (tmp), "+Qo" (v->data)
	: "r" (&v->data), "r" (u), "r" (a)
	: "cc");

#if 0
	if (oldval != u)
		smp_mb();
#endif

	return oldval;
}


/*lint -save -e666 */

#define ATOMIC_OPS(op, c_op, asm_op)		\
	ATOMIC_OP(op, c_op, asm_op)		\
	ATOMIC_OP_RETURN(op, c_op, asm_op)	\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(add, +=, add)
ATOMIC_OPS(sub, -=, sub)

#define atomic_andnot atomic_andnot

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op, asm_op)		\
	ATOMIC_OP(op, c_op, asm_op)		\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(and, &=, and)
ATOMIC_OPS(andnot, &= ~, bic)
ATOMIC_OPS(or,  |=, orr)
ATOMIC_OPS(xor, ^=, eor)

/*lint -restore */
#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

/*lint -e{529, 530}*/
static inline __attribute__((always_inline)) unsigned long __xchg(u32 x, volatile void *ptr)
{
	unsigned long ret;
	unsigned int tmp;

	__asm__ volatile("@	__xchg4\n"
		"1:	ldrex	%0, [%3]\n"
		"	strex	%1, %2, [%3]\n"
		"	teq	%1, #0\n"
		"	bne	1b"
			: "=&r" (ret), "=&r" (tmp)
			: "r" (x), "r" (ptr)
			: "memory", "cc");

	return ret;
}

/**
 * @endcond
 */
#define xchg(x, p) __xchg((u32)x, p)

#define atomic_inc(v)		atomic_add(1, v)
#define atomic_dec(v)		atomic_sub(1, v)

#define atomic_inc_and_test(v)	(atomic_add_return(1, v) == 0)
#define atomic_dec_and_test(v)	(atomic_sub_return(1, v) == 0)
#define atomic_inc_return_relaxed(v)    (atomic_add_return_relaxed(1, v))
#define atomic_dec_return_relaxed(v)    (atomic_sub_return_relaxed(1, v))
#define atomic_sub_and_test(i, v) (atomic_sub_return(i, v) == 0)

#define atomic_add_negative(i, v) (atomic_add_return(i, v) < 0)

#define CPU_IDLE __asm__ volatile ("wfi");

#endif
