/***** BEGIN LICENSE BLOCK *****
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2001-16 David Ireland, D.I. Management Services Pty Limited
 * <http://www.di-mgt.com.au/bigdigits.html>. All rights reserved.
 *
 ***** END LICENSE BLOCK *****/
/*
 * Last updated:
 * $Date: 2016-03-31 09:51:00 $
 * $Revision: 2.6.1 $
 * $Author: dai $
 */
#include "types.h"
#include "assert.h"
#include "string.h"
#include "stdlib.h"
#include "bigdigits.h"
#include "sect.h"
#include "dma.h"
#include "dtag.h"

#define MAX_HALF_DIGIT 0xFFFFUL	/* NB 'L' */
#define LOHALF(x) ((u32)((x) & MAX_HALF_DIGIT))
#define HIHALF(x) ((u32)((x) >> BITS_PER_HALF_DIGIT & MAX_HALF_DIGIT))
#define TOHIGH(x) ((u32)((x) << BITS_PER_HALF_DIGIT))
#define mpNEXTBITMASK(mask, n) \
	do {                        \
		if (mask == 1) {        \
			mask = HIBITMASK;   \
			n--;                \
		} else {                \
			mask >>= 1;         \
		}                       \
	} while (0);                \

#if 0
static void *mpAlloc(u32 ndigits)
{
	void *p = sys_malloc(SLOW_DATA, ndigits * sizeof(u32));
	sys_assert(p != NULL);
	return p;
}

static void mpFree(void *p)
{
	sys_free(SLOW_DATA, p);
}
#endif

static u32 mpSizeof(const u32 *a, u32 ndigits)
{
	while (ndigits--) {
		if (a[ndigits])
			return (++ndigits);
	}
	return 0;
}

void mpSetDigit(u32 *a, u32 d, u32 ndigits)
{
	memset((void *)a, 0, ndigits * sizeof(u32));
	a[0] = d;
}

static void mpSetEqual(u32 *a, const u32 *b, u32 ndigits)
{
	memcpy(a, b, ndigits * sizeof(u32));
}

static int mpCompare(const u32 *a, const u32 *b, u32 ndigits)
{
	while (ndigits--) {
		if (a[ndigits] > b[ndigits])
			return 1;
		if (a[ndigits] < b[ndigits])
			return -1;
	}
	return 0;
}

/** Returns sign of (a - d) where d is a single digit */
int mpShortCmp(const u32 a[], u32 d, u32 ndigits)
{
	u32 i;
	int gt = 0;
	int lt = 0;

	/* Zero-length a => a is zero */
	if (ndigits == 0) return (d ? -1 : 0);

	/* If |a| > 1 then a > d */
	for (i = 1; i < ndigits; i++) {
		if (a[i] != 0)
			return 1;	/* GT */
	}

	lt = (a[0] < d);
	gt = (a[0] > d);

	return gt - lt;	/* EQ=0 GT=+1 LT=-1 */
}

fast_code void mpSetZero(u32 *a, u32 ndigits)
{
	memset(a, 0, ndigits * sizeof(u32));
}

int mpSetBit(u32 a[], u32 ndigits, u32 ibit, int value)
	/* Set bit n (0..nbits-1) with value 1 or 0 */
{
	u32 idigit, bit_to_set;
	u32 mask;

	/* Which digit? (0-based) */
	idigit = ibit / BITS_PER_DIGIT;
	if (idigit >= ndigits)
		return -1;

	/* Set mask */
	bit_to_set = ibit % BITS_PER_DIGIT;
	mask = 0x01 << bit_to_set;

	if (value)
		a[idigit] |= mask;
	else
		a[idigit] &= (~mask);

	return 0;
}

static int mpIsZero(const u32 *a, u32 ndigits)
{
	u32 i;
	for (i = 0; i < ndigits; i++) {
		if (a[i])
			return 0;
	}
	return 1;
}

/**********************/
/* BIT-WISE FUNCTIONS */
/**********************/
u32 mpShiftLeft(u32 a[], const u32 *b, u32 shift, u32 ndigits)
{	/* Computes a = b << shift */
	/* [v2.1] Modified to cope with shift > BITS_PERDIGIT */
	u32 i, y, nw, bits;
	u32 mask, carry, nextcarry;

	/* Do we shift whole digits? */
	if (shift >= BITS_PER_DIGIT)
	{
		nw = shift / BITS_PER_DIGIT;
		i = ndigits;
		while (i--) {
			if (i >= nw)
				a[i] = b[i-nw];
			else
				a[i] = 0;
		}
		/* Call again to shift bits inside digits */
		bits = shift % BITS_PER_DIGIT;
		carry = b[ndigits-nw] << bits;
		if (bits)
			carry |= mpShiftLeft(a, a, bits, ndigits);
		return carry;
	} else {
		bits = shift;
	}

	/* Construct mask = high bits set */
	mask = ~(~(u32)0 >> bits);

	y = BITS_PER_DIGIT - bits;
	carry = 0;
	for (i = 0; i < ndigits; i++) {
		nextcarry = (b[i] & mask) >> y;
		a[i] = b[i] << bits | carry;
		carry = nextcarry;
	}

	return carry;
}

u32 mpShiftRight(u32 a[], const u32 b[], u32 shift, u32 ndigits)
{	/* Computes a = b >> shift */
	/* [v2.1] Modified to cope with shift > BITS_PERDIGIT */
	u32 i, y, nw, bits;
	u32 mask, carry, nextcarry;

	/* Do we shift whole digits? */
	if (shift >= BITS_PER_DIGIT) {
		nw = shift / BITS_PER_DIGIT;
		for (i = 0; i < ndigits; i++) {
			if ((i+nw) < ndigits)
				a[i] = b[i+nw];
			else
				a[i] = 0;
		}
		/* Call again to shift bits inside digits */
		bits = shift % BITS_PER_DIGIT;
		carry = b[nw-1] >> bits;
		if (bits)
			carry |= mpShiftRight(a, a, bits, ndigits);
		return carry;
	} else {
		bits = shift;
	}

	/* Construct mask to set low bits */
	/* (thanks to Jesse Chisholm for suggesting this improved technique) */
	mask = ~(~(u32)0 << bits);

	y = BITS_PER_DIGIT - bits;
	carry = 0;
	i = ndigits;
	while (i--)
	{
		nextcarry = (b[i] & mask) << y;
		a[i] = b[i] >> bits | carry;
		carry = nextcarry;
	}

	return carry;
}

/*!
 * @brief computes p = x * y
 *
 */
static int spMultiply(u32 *pout, u32 x, u32 y)
{
	u64 t = (u64)x * (u64)y;
	pout[1] = (u32)(t >> 32);
	pout[0] = (u32)(t & 0xFFFFFFFF);
	return 0;
}

#define B (MAX_HALF_DIGIT + 1)

static void spMultSub(u32 uu[2], u32 qhat, u32 v1, u32 v0)
{
	/*	Compute uu = uu - q(v1v0)
		where uu = u3u2u1u0, u3 = 0
		and u_n, v_n are all half-digits
		even though v1, v2 are passed as full digits.
	*/
	u32 p0, p1, t;

	p0 = qhat * v0;
	p1 = qhat * v1;
	t = p0 + TOHIGH(LOHALF(p1));
	uu[0] -= t;
	if (uu[0] > MAX_DIGIT - t)
		uu[1]--;	/* Borrow */
	uu[1] -= HIHALF(p1);
}

static u32 spDivide(u32 *q, u32 *r, const u32 u[2], u32 v)
{	/*	Computes quotient q = u / v, remainder r = u mod v
		where u is a double digit
		and q, v, r are single precision digits.
		Returns high digit of quotient (max value is 1)
		CAUTION: Assumes normalised such that v1 >= b/2
		where b is size of HALF_DIGIT
		i.e. the most significant bit of v should be one

		In terms of half-digits in Knuth notation:
		(q2q1q0) = (u4u3u2u1u0) / (v1v0)
		(r1r0) = (u4u3u2u1u0) mod (v1v0)
		for m = 2, n = 2 where u4 = 0
		q2 is either 0 or 1.
		We set q = (q1q0) and return q2 as "overflow"
	*/
	u32 qhat, rhat, t, v0, v1, u0, u1, u2, u3;
	u32 uu[2], q2;

	/* Check for normalisation */
	if (!(v & HIBITMASK))
	{	/* Stop if assert is working, else return error */
		sys_assert(v & HIBITMASK);
		*q = *r = 0;
		return MAX_DIGIT;
	}

	/* Split up into half-digits */
	v0 = LOHALF(v);
	v1 = HIHALF(v);
	u0 = LOHALF(u[0]);
	u1 = HIHALF(u[0]);
	u2 = LOHALF(u[1]);
	u3 = HIHALF(u[1]);

	/* Do three rounds of Knuth Algorithm D Vol 2 p272 */

	/*	ROUND 1. Set j = 2 and calculate q2 */
	/*	Estimate qhat = (u4u3)/v1  = 0 or 1
		then set (u4u3u2) -= qhat(v1v0)
		where u4 = 0.
	*/
/* [Replaced in Version 2] -->
	qhat = u3 / v1;
	if (qhat > 0)
	{
		rhat = u3 - qhat * v1;
		t = TOHIGH(rhat) | u2;
		if (qhat * v0 > t)
			qhat--;
	}
<-- */
	qhat = (u3 < v1 ? 0 : 1);
	if (qhat > 0)
	{	/* qhat is one, so no need to mult */
		rhat = u3 - v1;
		/* t = r.b + u2 */
		t = TOHIGH(rhat) | u2;
		if (v0 > t)
			qhat--;
	}

	uu[1] = 0;		/* (u4) */
	uu[0] = u[1];	/* (u3u2) */
	if (qhat > 0)
	{
		/* (u4u3u2) -= qhat(v1v0) where u4 = 0 */
		spMultSub(uu, qhat, v1, v0);
		if (HIHALF(uu[1]) != 0)
		{	/* Add back */
			qhat--;
			uu[0] += v;
			uu[1] = 0;
		}
	}
	q2 = qhat;

	/*	ROUND 2. Set j = 1 and calculate q1 */
	/*	Estimate qhat = (u3u2) / v1
		then set (u3u2u1) -= qhat(v1v0)
	*/
	t = uu[0];
	qhat = t / v1;
	rhat = t - qhat * v1;
	/* Test on v0 */
	t = TOHIGH(rhat) | u1;
	if ((qhat == B) || (qhat * v0 > t))
	{
		qhat--;
		rhat += v1;
		t = TOHIGH(rhat) | u1;
		if ((rhat < B) && (qhat * v0 > t))
			qhat--;
	}

	/*	Multiply and subtract
		(u3u2u1)' = (u3u2u1) - qhat(v1v0)
	*/
	uu[1] = HIHALF(uu[0]);	/* (0u3) */
	uu[0] = TOHIGH(LOHALF(uu[0])) | u1;	/* (u2u1) */
	spMultSub(uu, qhat, v1, v0);
	if (HIHALF(uu[1]) != 0)
	{	/* Add back */
		qhat--;
		uu[0] += v;
		uu[1] = 0;
	}

	/* q1 = qhat */
	*q = TOHIGH(qhat);

	/* ROUND 3. Set j = 0 and calculate q0 */
	/*	Estimate qhat = (u2u1) / v1
		then set (u2u1u0) -= qhat(v1v0)
	*/
	t = uu[0];
	qhat = t / v1;
	rhat = t - qhat * v1;
	/* Test on v0 */
	t = TOHIGH(rhat) | u0;
	if ((qhat == B) || (qhat * v0 > t))
	{
		qhat--;
		rhat += v1;
		t = TOHIGH(rhat) | u0;
		if ((rhat < B) && (qhat * v0 > t))
			qhat--;
	}

	/*	Multiply and subtract
		(u2u1u0)" = (u2u1u0)' - qhat(v1v0)
	*/
	uu[1] = HIHALF(uu[0]);	/* (0u2) */
	uu[0] = TOHIGH(LOHALF(uu[0])) | u0;	/* (u1u0) */
	spMultSub(uu, qhat, v1, v0);
	if (HIHALF(uu[1]) != 0)
	{	/* Add back */
		qhat--;
		uu[0] += v;
		uu[1] = 0;
	}

	/* q0 = qhat */
	*q |= LOHALF(qhat);

	/* Remainder is in (u1u0) i.e. uu[0] */
	*r = uu[0];
	return q2;
}

static u32 mpMultSub(u32 wn, u32 w[], const u32 v[],
					   u32 q, u32 n)
{	/*	Compute w = w - qv
		where w = (WnW[n-1]...W[0])
		return modified Wn.
	*/
	u32 k, t[2];
	u32 i;

	if (q == 0)	/* No change */
		return wn;

	k = 0;

	for (i = 0; i < n; i++)
	{
		spMultiply(t, q, v[i]);
		w[i] -= k;
		if (w[i] > MAX_DIGIT - k)
			k = 1;
		else
			k = 0;
		w[i] -= t[0];
		if (w[i] > MAX_DIGIT - t[0])
			k++;
		k += t[1];
	}

	/* Cope with Wn not stored in array w[0..n-1] */
	wn -= k;

	return wn;
}

static int QhatTooBig(u32 qhat, u32 rhat,
					  u32 vn2, u32 ujn2)
{	/*	Returns true if Qhat is too big
		i.e. if (Qhat * Vn-2) > (b.Rhat + Uj+n-2)
	*/
	u32 t[2];

	spMultiply(t, qhat, vn2);
	if (t[1] < rhat)
		return 0;
	else if (t[1] > rhat)
		return 1;
	else if (t[0] > ujn2)
		return 1;

	return 0;
}

u32 mpAdd(u32 w[], const u32 u[], const u32 v[], u32 ndigits)
{
	/*	Calculates w = u + v
		where w, u, v are multiprecision integers of ndigits each
		Returns carry if overflow. Carry = 0 or 1.

		Ref: Knuth Vol 2 Ch 4.3.1 p 266 Algorithm A.
	*/

	u32 k;
	u32 j;

	sys_assert(w != v);

	/* Step A1. Initialise */
	k = 0;

	for (j = 0; j < ndigits; j++)
	{
		/*	Step A2. Add digits w_j = (u_j + v_j + k)
			Set k = 1 if carry (overflow) occurs
		*/
		w[j] = u[j] + k;
		if (w[j] < k)
			k = 1;
		else
			k = 0;

		w[j] += v[j];
		if (w[j] < v[j])
			k++;

	}	/* Step A3. Loop on j */

	return k;	/* w_n = k */
}

u32 mpSubtract(u32 w[], const u32 u[], const u32 v[], u32 ndigits)
{
	/*	Calculates w = u - v where u >= v
		w, u, v are multiprecision integers of ndigits each
		Returns 0 if OK, or 1 if v > u.

		Ref: Knuth Vol 2 Ch 4.3.1 p 267 Algorithm S.
	*/

	u32 k;
	u32 j;

	sys_assert(w != v);

	/* Step S1. Initialise */
	k = 0;

	for (j = 0; j < ndigits; j++)
	{
		/*	Step S2. Subtract digits w_j = (u_j - v_j - k)
			Set k = 1 if borrow occurs.
		*/
		w[j] = u[j] - k;
		if (w[j] > MAX_DIGIT - k)
			k = 1;
		else
			k = 0;

		w[j] -= v[j];
		if (w[j] > MAX_DIGIT - v[j])
			k++;

	}	/* Step S3. Loop on j */

	return k;	/* Should be zero if u >= v */
}

int mpMultiply(u32 w[], const u32 u[], const u32 v[], u32 ndigits)
{
	/*	Computes product w = u * v
		where u, v are multiprecision integers of ndigits each
		and w is a multiprecision integer of 2*ndigits

		Ref: Knuth Vol 2 Ch 4.3.1 p 268 Algorithm M.
	*/

	u32 k, t[2];
	u32 i, j, m, n;

	sys_assert(w != u && w != v);

	m = n = ndigits;

	/* Step M1. Initialise */
	for (i = 0; i < 2 * m; i++)
		w[i] = 0;

	for (j = 0; j < n; j++) {
		/* Step M2. Zero multiplier? */
		if (v[j] == 0) {
			w[j + m] = 0;
		} else {
			/* Step M3. Initialise i */
			k = 0;
			for (i = 0; i < m; i++) {
				/* Step M4. Multiply and add */
				/* t = u_i * v_j + w_(i+j) + k */
				spMultiply(t, u[i], v[j]);

				t[0] += k;
				if (t[0] < k)
					t[1]++;
				t[0] += w[i+j];
				if (t[0] < w[i + j])
					t[1]++;

				w[i + j] = t[0];
				k = t[1];
			}
			/* Step M5. Loop on i, set w_(j+m) = k */
			w[j + m] = k;
		}
	}	/* Step M6. Loop on j */

	return 0;
}

u32 mpShortDiv(u32 q[], const u32 u[], u32 v,
				   u32 ndigits)
{
	/*	Calculates quotient q = u div v
		Returns remainder r = u mod v
		where q, u are multiprecision integers of ndigits each
		and r, v are single precision digits.

		Makes no assumptions about normalisation.

		Ref: Knuth Vol 2 Ch 4.3.1 Exercise 16 p625
	*/
	u32 j;
	u32 t[2], r;
	u32 shift;
	u32 bitmask, overflow, *uu;

	if (ndigits == 0) return 0;
	if (v == 0)	return 0;	/* Divide by zero error */

	/*	Normalise first */
	/*	Requires high bit of V
		to be set, so find most signif. bit then shift left,
		i.e. d = 2^shift, u' = u * d, v' = v * d.
	*/
	bitmask = HIBITMASK;
	for (shift = 0; shift < BITS_PER_DIGIT; shift++)
	{
		if (v & bitmask)
			break;
		bitmask >>= 1;
	}

	v <<= shift;
	overflow = mpShiftLeft(q, u, shift, ndigits);
	uu = q;

	/* Step S1 - modified for extra digit. */
	r = overflow;	/* New digit Un */
	j = ndigits;
	while (j--)
	{
		/* Step S2. */
		t[1] = r;
		t[0] = uu[j];
		overflow = spDivide(&q[j], &r, t, v);
	}

	/* Unnormalise */
	r >>= shift;

	return r;
}

int mpDivide(u32 q[], u32 r[], u32 u[], u32 udigits, u32 v[], u32 vdigits)
{	/*	Computes quotient q = u / v and remainder r = u mod v
		where q, r, u are multiple precision digits
		all of udigits and the divisor v is vdigits.

		Ref: Knuth Vol 2 Ch 4.3.1 p 272 Algorithm D.

		Do without extra storage space, i.e. use r[] for
		normalised u[], unnormalise v[] at end, and cope with
		extra digit Uj+n added to u after normalisation.

		WARNING: this trashes q and r first, so cannot do
		u = u / v or v = u mod v.
		It also changes v temporarily so cannot make it const.
	*/
	u32 shift;
	int n, m, j;
	u32 bitmask, overflow;
	u32 qhat = 0, rhat = 0, t[2];
	u32 *uu, *ww;
	int qhatOK, cmp;

	/* Clear q and r */
	mpSetZero(q, udigits);
	mpSetZero(r, udigits);

	/* Work out exact sizes of u and v */
	n = (int)mpSizeof(v, vdigits);
	m = (int)mpSizeof(u, udigits);
	m -= n;

	/* Catch special cases */
	if (n == 0)
		return -1;	/* Error: divide by zero */

	if (n == 1) {	/* Use short division instead */
		r[0] = mpShortDiv(q, u, v[0], udigits);
		return 0;
	}

	if (m < 0) {	/* v > u, so just set q = 0 and r = u */
		mpSetEqual(r, u, udigits);
		return 0;
	}

	if (m == 0) {	/* u and v are the same length */
		cmp = mpCompare(u, v, (u32)n);
		if (cmp < 0) {	/* v > u, as above */
			mpSetEqual(r, u, udigits);
			return 0;
		} else if (cmp == 0) {	/* v == u, so set q = 1 and r = 0 */
			mpSetDigit(q, 1, udigits);
			return 0;
		}
	}

	/*	In Knuth notation, we have:
		Given
		u = (Um+n-1 ... U1U0)
		v = (Vn-1 ... V1V0)
		Compute
		q = u/v = (QmQm-1 ... Q0)
		r = u mod v = (Rn-1 ... R1R0)
	*/

	/*	Step D1. Normalise */
	/*	Requires high bit of Vn-1
		to be set, so find most signif. bit then shift left,
		i.e. d = 2^shift, u' = u * d, v' = v * d.
	*/
	bitmask = HIBITMASK;
	for (shift = 0; shift < BITS_PER_DIGIT; shift++) {
		if (v[n-1] & bitmask)
			break;
		bitmask >>= 1;
	}

	/* Normalise v in situ - NB only shift non-zero digits */
	overflow = mpShiftLeft(v, v, shift, n);

	/* Copy normalised dividend u*d into r */
	overflow = mpShiftLeft(r, u, shift, n + m);
	uu = r;	/* Use ptr to keep notation constant */

	t[0] = overflow;	/* Extra digit Um+n */

	/* Step D2. Initialise j. Set j = m */
	for (j = m; j >= 0; j--) {
		/* Step D3. Set Qhat = [(b.Uj+n + Uj+n-1)/Vn-1]
		   and Rhat = remainder */
		qhatOK = 0;
		t[1] = t[0];	/* This is Uj+n */
		t[0] = uu[j+n-1];
		overflow = spDivide(&qhat, &rhat, t, v[n-1]);

		/* Test Qhat */
		if (overflow) {	/* Qhat == b so set Qhat = b - 1 */
			qhat = MAX_DIGIT;
			rhat = uu[j+n-1];
			rhat += v[n-1];
			if (rhat < v[n-1])	/* Rhat >= b, so no re-test */
				qhatOK = 1;
		}
		/* [VERSION 2: Added extra test "qhat && "] */
		if (qhat && !qhatOK && QhatTooBig(qhat, rhat, v[n-2], uu[j+n-2])) {
			/* If Qhat.Vn-2 > b.Rhat + Uj+n-2
			 *decrease Qhat by one, increase Rhat by Vn-1
			 */
			qhat--;
			rhat += v[n-1];
			/* Repeat this test if Rhat < b */
			if (!(rhat < v[n-1]))
				if (QhatTooBig(qhat, rhat, v[n-2], uu[j+n-2]))
					qhat--;
		}


		/* Step D4. Multiply and subtract */
		ww = &uu[j];
		overflow = mpMultSub(t[1], ww, v, qhat, (u32)n);

		/* Step D5. Test remainder. Set Qj = Qhat */
		q[j] = qhat;
		if (overflow) {
		/* Step D6. Add back if D4 was negative */
			q[j]--;
			overflow = mpAdd(ww, ww, v, (u32)n);
		}

		t[0] = uu[j + n - 1];	/* Uj+n on next round */

	}	/* Step D7. Loop on j */

	/* Clear high digits in uu */
	for (j = n; j < m + n; j++)
		uu[j] = 0;

	/* Step D8. Unnormalise. */

	mpShiftRight(r, r, shift, n);
	mpShiftRight(v, v, shift, n);

	return 0;
}

int mpSquare(u32 w[], const u32 x[], u32 ndigits)
/* New in Version 2.0 */
{
	/*	Computes square w = x * x
		where x is a multiprecision integer of ndigits
		and w is a multiprecision integer of 2*ndigits

		Ref: Menezes p596 Algorithm 14.16 with errata.
	*/

	u32 k, p[2], u[2], cbit, carry;
	u32 i, j, t, i2, cpos;

	sys_assert(w != x);

	t = ndigits;

	/* 1. For i from 0 to (2t-1) do: w_i = 0 */
	i2 = t << 1;
	for (i = 0; i < i2; i++)
		w[i] = 0;

	carry = 0;
	cpos = i2-1;
	/* 2. For i from 0 to (t-1) do: */
	for (i = 0; i < t; i++)
	{
		/* 2.1 (uv) = w_2i + x_i * x_i, w_2i = v, c = u
		   Careful, w_2i may be double-prec
		*/
		i2 = i << 1; /* 2*i */
		spMultiply(p, x[i], x[i]);
		p[0] += w[i2];
		if (p[0] < w[i2])
			p[1]++;
		k = 0;	/* p[1] < b, so no overflow here */
		if (i2 == cpos && carry)
		{
			p[1] += carry;
			if (p[1] < carry)
				k++;
			carry = 0;
		}
		w[i2] = p[0];
		u[0] = p[1];
		u[1] = k;

		/* 2.2 for j from (i+1) to (t-1) do:
		   (uv) = w_{i+j} + 2x_j * x_i + c,
		   w_{i+j} = v, c = u,
		   u is double-prec
		   w_{i+j} is dbl if [i+j] == cpos
		*/
		k = 0;
		for (j = i+1; j < t; j++)
		{
			/* p = x_j * x_i */
			spMultiply(p, x[j], x[i]);
			/* p = 2p <=> p <<= 1 */
			cbit = (p[0] & HIBITMASK) != 0;
			k =  (p[1] & HIBITMASK) != 0;
			p[0] <<= 1;
			p[1] <<= 1;
			p[1] |= cbit;
			/* p = p + c */
			p[0] += u[0];
			if (p[0] < u[0])
			{
				p[1]++;
				if (p[1] == 0)
					k++;
			}
			p[1] += u[1];
			if (p[1] < u[1])
				k++;
			/* p = p + w_{i+j} */
			p[0] += w[i+j];
			if (p[0] < w[i+j])
			{
				p[1]++;
				if (p[1] == 0)
					k++;
			}
			if ((i+j) == cpos && carry)
			{	/* catch overflow from last round */
				p[1] += carry;
				if (p[1] < carry)
					k++;
				carry = 0;
			}
			/* w_{i+j} = v, c = u */
			w[i+j] = p[0];
			u[0] = p[1];
			u[1] = k;
		}
		/* 2.3 w_{i+t} = u */
		w[i+t] = u[0];
		/* remember overflow in w_{i+t} */
		carry = u[1];
		cpos = i+t;
	}

	/* (NB original step 3 deleted in Menezes errata) */

	/* Return w */

	return 0;
}

/* Square: y = (y * y) mod m */
#define mpMODSQUARETEMP(y, m, n, t1, t2) \
	do{ \
		mpSquare(t1, y, n); \
		mpDivide(t2, y, t1, n * 2, m, n); \
	} while(0);
/* Mult:   y = (y * x) mod m */
#define mpMODMULTTEMP(y, x, m, n, t1, t2) \
	do{ \
		mpMultiply(t1, x, y, n); \
		mpDivide(t2, y, t1, n * 2, m, n); \
	} while(0);
/* Mult:   w = (y * x) mod m */
#define mpMODMULTXYTEMP(w, y, x, m, n, t1, t2) \
	do{ \
		mpMultiply(t1, x, y, (n)); \
		mpDivide(t2, w, t1, (n)*2, m, (n)); \
	} while(0);
/*
 * @brief computes y = x^e mode m
 * classic binary left-to-right method
 */

int mpModExp(u32 *yout, const u32 *x, const u32 *e, u32 *m, u32 ndigits, u32 *work_buffer)
{
#if 1
	/* Algorithm: Coron�s exponentiation (left-to-right)
	 * Square-and-multiply resistant against simple power attacks (SPA)
	 * Ref: Jean-Sebastian Coron, "Resistance Against Differential Power Analysis for
	 * Elliptic Curve Cryptosystems", August 1999.
	 * -- This version adapted from Coron's elliptic curve point scalar multiplication
	 *    to RSA-style modular exponentiation.
	 * Input: base x, modulus m, and
	 *   exponent e = (e_k, e_{k-1},...,e_0) with e_k = 1
	 * Output: c = x^e mod m
	 * 1. c[0] = x
	 * 2. For i = k-2 downto 0 do:
	 * 3.    c[0] = c[0]^2 mod m
	 * 4.    c[1] = c[0] * x mod m
	 * 5.    c[0] = c[d_i]
	 * 6. Return c[0]
	 */
	u32 mask;
	u32 n;
	u32 nn = ndigits * 2;
	unsigned int ej;

	/* Create some double-length temps */
	u32 *t1, *t2;
	u32 *c[2];
	sys_assert(nn * 4 * 4 <= DTAG_SZE);
	// t1 = mpAlloc(nn);
	// t2 = mpAlloc(nn);
	// c[0] = mpAlloc(nn);
	// c[1] = mpAlloc(nn);
	memset(work_buffer, 0, nn * 4 * 4);
	t1 = work_buffer;
	t2 = t1 + nn;
	c[0] = t2 + nn;
	c[1] = c[0] + nn;

	sys_assert(ndigits != 0);

	n = mpSizeof(e, ndigits);
	/* Catch e==0 => x^0=1 */
	if (0 == n)
	{
		mpSetDigit(yout, 1, ndigits);
		goto done;
	}
	/* Find second-most significant bit in e */
	for (mask = HIBITMASK; mask > 0; mask >>= 1)
	{
		if (e[n-1] & mask)
			break;
	}
	mpNEXTBITMASK(mask, n);

	/* Set c[0] = x */
	mpSetEqual(c[0], x, ndigits);

	/* For bit j = k-2 downto 0 */
	while (n)
	{
		/* Square c[0] = c[0]^2 mod n */
		mpMODSQUARETEMP(c[0], m, ndigits, t1, t2);
		/* Multiply c[1] = c[0] * x mod n */
		mpMODMULTXYTEMP(c[1], c[0], x, m, ndigits, t1, t2);
		/* c[0] = c[e(j)] */
		ej = (e[n-1] & mask) != 0;
		sys_assert(ej <= 1);
		mpSetEqual(c[0], c[ej], ndigits);

		/* Move to next bit */
		mpNEXTBITMASK(mask, n);
	}

	/* Return c[0] */
	mpSetEqual(yout, c[0], ndigits);

done:
	// mpFree(t1);
	// mpFree(t2);
	// mpFree(c[0]);
	// mpFree(c[1]);
	return 0;
#else

	/*	"Classic" binary left-to-right method */
	/*  [v2.2] removed const restriction on m[] to avoid using an extra alloc'd var
		(m is changed in-situ during the divide operation then restored) */
	u32 mask;
	u32 n;
	u32 nn = ndigits * 2;
	/* Create some double-length temps */
	u32 *t1, *t2, *y;
	sys_assert(nn * 4 * 3 <= DTAG_SZE);
	memset(work_buffer, 0, nn * 4 * 3);
	// t1 = mpAlloc(nn);
	// t2 = mpAlloc(nn);
	// y  = mpAlloc(nn);
	t1 = work_buffer;
	t2 = t1 + nn;
	y = t2 + nn;

	sys_assert(ndigits != 0);

	n = mpSizeof(e, ndigits);
	/* Catch e==0 => x^0=1 */
	if (0 == n)
	{
		mpSetDigit(yout, 1, ndigits);
		goto done;
	}
	/* Find second-most significant bit in e */
	for (mask = HIBITMASK; mask > 0; mask >>= 1)
	{
		if (e[n-1] & mask)
			break;
	}
	mpNEXTBITMASK(mask, n);

	/* Set y = x */
	mpSetEqual(y, x, ndigits);

	/* For bit j = k-2 downto 0 */
	while (n)
	{
		/* Square y = y * y mod n */
		mpMODSQUARETEMP(y, m, ndigits, t1, t2);
		if (e[n-1] & mask)
		{	/*	if e(j) == 1 then multiply
				y = y * x mod n */
			mpMODMULTTEMP(y, x, m, ndigits, t1, t2);
		}

		/* Move to next bit */
		mpNEXTBITMASK(mask, n);
	}

	/* Return y */
	mpSetEqual(yout, y, ndigits);

done:
	// mpFree(t1);
	// mpFree(t2);
	// mpFree(y);
	return 0;
#endif
}

int mpModInv(u32 inv[], const u32 u[], const u32 v[], u32 ndigits, u32 **work_buffers)
{	/*	Computes inv = u^(-1) mod v */
	/*	Ref: Knuth Algorithm X Vol 2 p 342
		ignoring u2, v2, t2
		and avoiding negative numbers.
		Returns non-zero if inverse undefined.
	*/
	int bIterations;
	int result;
	/* Allocate temp storage */
	u32 *u1, *u3, *v1, *v3, *t1, *t3, *q, *w;
	// u1 = mpAlloc(ndigits);
	// u3 = mpAlloc(ndigits);
	// v1 = mpAlloc(ndigits);
	// v3 = mpAlloc(ndigits);
	// t1 = mpAlloc(ndigits);
	// t3 = mpAlloc(ndigits);
	// q = mpAlloc(ndigits);
	// w = mpAlloc(2 * ndigits);
	u1 = work_buffers[0];
	u3 = u1 + ndigits;
	v1 = u3 + ndigits;
	v3 = v1 + ndigits;
	t1 = v3 + ndigits;
	t3 = t1 + ndigits;
	q = t3 + ndigits;
	w = work_buffers[1];

	/* Step X1. Initialise */
	mpSetDigit(u1, 1, ndigits);		/* u1 = 1 */
	mpSetEqual(u3, u, ndigits);		/* u3 = u */
	mpSetZero(v1, ndigits);			/* v1 = 0 */
	mpSetEqual(v3, v, ndigits);		/* v3 = v */

	bIterations = 1;	/* Remember odd/even iterations */
	while (!mpIsZero(v3, ndigits))		/* Step X2. Loop while v3 != 0 */
	{					/* Step X3. Divide and "Subtract" */
		mpDivide(q, t3, u3, ndigits, v3, ndigits);
						/* q = u3 / v3, t3 = u3 % v3 */
		mpMultiply(w, q, v1, ndigits);	/* w = q * v1 */
		mpAdd(t1, u1, w, ndigits);		/* t1 = u1 + w */

		/* Swap u1 = v1; v1 = t1; u3 = v3; v3 = t3 */
		mpSetEqual(u1, v1, ndigits);
		mpSetEqual(v1, t1, ndigits);
		mpSetEqual(u3, v3, ndigits);
		mpSetEqual(v3, t3, ndigits);

		bIterations = -bIterations;
	}

	if (bIterations < 0)
		mpSubtract(inv, v, u1, ndigits);	/* inv = v - u1 */
	else
		mpSetEqual(inv, u1, ndigits);	/* inv = u1 */

	/* Make sure u3 = gcd(u,v) == 1 */
	if (mpShortCmp(u3, 1, ndigits) != 0) {
		result = 1;
		mpSetZero(inv, ndigits);
	}
	else
		result = 0;

	/* Clear up */
	// mpFree(u1);
	// mpFree(v1);
	// mpFree(t1);
	// mpFree(u3);
	// mpFree(v3);
	// mpFree(t3);
	// mpFree(q);
	// mpFree(w);

	return result;
}

u32 mpConvFromOctets(u32 a[], u32 ndigits, const unsigned char *c, u32 nbytes)
/* Converts nbytes octets into big digit a of max size ndigits
	Returns actual number of digits set (may be larger than mpSizeof)
*/
{
	u32 i;
	int j, k;
	u32 t;

	mpSetZero(a, ndigits);

	/* Read in octets, least significant first */
	/* i counts into big_d, j along c, and k is # bits to shift */
	for (i = 0, j = (int)nbytes - 1; i < ndigits && j >= 0; i++) {
		t = 0;
		for (k = 0; j >= 0 && k < BITS_PER_DIGIT; j--, k += 8)
			t |= ((u32)c[j]) << k;
		a[i] = t;
	}

	return i;
}

/* Compute w = u + v (mod m) where 0 <= u,v < m and w != v */
void mpModAdd(u32 w[], const u32 u[], const u32 v[], const u32 m[], u32 ndigits)
{
	int carry;
	// w != v
	carry = mpAdd(w, u, v, ndigits);
	// NB This works even with overflow beyond ndigits
	if (carry || mpCompare(w, m, ndigits) >= 0) {
		mpSubtract(w, w, m, ndigits);
	}
}

/* Compute w = u - v (mod m) where 0 <= u,v < m and w != v */
void mpModSub(u32 w[], const u32 u[], const u32 v[], const u32 m[], u32 ndigits, u32 *work_buffer)
{
	/* We need a temp variable t [to allow mpModSub(w,w,v,...)] */
	u32 *t;
	// t = mpAlloc(ndigits);
	memset(work_buffer, 0, ndigits * sizeof(u32));
	t = work_buffer;
	/* w <-- m - v [always > 0] */
	mpSubtract(t, m, v, ndigits);
	/* w <-- w + u (mod m) */
	mpModAdd(w, u, t, m, ndigits);
	// mpFree(t);
}

u32 mpConvFromHex(u32 a[], u32 ndigits, const char *s, void *work_buffer)
/* Convert a string in hexadecimal format to a big digit.
	Return actual number of digits set (may be larger than mpSizeof).
	Just ignores invalid characters in s.
*/
{
	u8 *newdigits;
	u32 newlen;
	u32 n;
	u16 t;
	u32 i, j;

	mpSetZero(a, ndigits);

	/* Create some temp storage for int values */
	n = strlen(s);
	if (0 == n)
		return 0;
	newlen  = n;
	// newdigits = mpAlloc(newlen);
	sys_assert(newlen <= DTAG_SZE);
	newdigits = (u8 *)work_buffer;
	memset(newdigits, 0, newlen);

	/* Work through zero-terminated string */
	for (i = 0; s[i]; i++) {
		t = s[i];
		if ((t >= '0') && (t <= '9'))
			t = (t - '0');
		else if ((t >= 'a') && (t <= 'f'))
			t = (t - 'a' + 10);
		else if ((t >= 'A') && (t <= 'F'))
			t = (t - 'A' + 10);
		else
			continue;
		for (j = newlen; j > 0; j--) {
			t += (u16)newdigits[j - 1] << 4;
			newdigits[j - 1] = (unsigned char)(t & 0xFF);
			t >>= 8;
		}
	}

	/* Convert bytes to big digits */
	n = mpConvFromOctets(a, ndigits, newdigits, newlen);

	/* Clean up */
	// mpFree(newdigits);

	return n;
}
