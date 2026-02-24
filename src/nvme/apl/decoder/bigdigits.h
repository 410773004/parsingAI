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

#define HIBITMASK 0x80000000UL
#define MAX_DIGIT 0xFFFFFFFFUL
#define BITS_PER_DIGIT 32
#define BITS_PER_HALF_DIGIT (BITS_PER_DIGIT / 2)
#define BYTES_PER_DIGIT (BITS_PER_DIGIT / 8)
#define MAX_DIGITS_BIT_LENGITH 8192    ///< 8192 bit length
#define MAX_DIGITS_BYTE_LENGITH (MAX_DIGITS_BIT_LENGITH / 8)
#define MAX_DIGITS_DW_CNT ((MAX_DIGITS_BIT_LENGITH + BITS_PER_DIGIT - 1) / BITS_PER_DIGIT)


void mpSetZero(u32 *a, u32 ndigits);

int mpSetBit(u32 a[], u32 ndigits, u32 ibit, int value);

int mpModInv(u32 inv[], const u32 u[], const u32 v[], u32 ndigits, u32 **work_buffers);

void mpModSub(u32 w[], const u32 u[], const u32 v[], const u32 m[], u32 ndigits, u32 *work_buffer);

u32 mpConvFromHex(u32 a[], u32 ndigits, const char *s, void *work_buffer);

int mpModExp(u32 *yout, const u32 *x, const u32 *e, u32 *m, u32 ndigits, u32 *work_buffer);

void mpSetDigit(u32 *a, u32 d, u32 ndigits);
