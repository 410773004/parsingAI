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
#include "string.h"
#include "ctype.h"
#include "sect.h"
#include "assert.h"

static char *___strtok;

#ifndef __HAVE_ARCH_ATOI

int atoi(const char *cp)
{
	return strtol(cp, (void *)0, 10);
}
#endif

#ifndef __HAVE_ARCH_STRTOUL

unsigned long strtoul(const char *cp, char **endp, unsigned int base)
{
	unsigned long result = 0, value;

	if (!base) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if ((*cp == 'x') && isxdigit(cp[1])) {
				cp++;
				base = 16;
			}
		}
	}
	while (isxdigit(*cp)
	       && (value =
		   isdigit(*cp) ? *cp -
		   '0' : (islower(*cp) ? toupper(*cp) : *cp) - 'A' + 10) <
	       base) {
		result = result * base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return result;
}

unsigned long long strtoull(const char *cp, char **endp, unsigned int base)
{
	unsigned long long result = 0, value;

	if (!base) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if ((*cp == 'x') && isxdigit(cp[1])) {
				cp++;
				base = 16;
			}
		}
	}
	while (isxdigit(*cp)
	       && (value =
		   isdigit(*cp) ? *cp -
		   '0' : (islower(*cp) ? toupper(*cp) : *cp) - 'A' + 10) <
	       base) {
		result = result * base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return result;
}
#endif

#ifndef __HAVE_ARCH_STRTOL
signed long strtol(const char *cp, char **endp, unsigned int base)
{
	signed long sign;
	signed long result = 0, value;

	if (!isdigit(*cp)) {
		if ((*cp) == '-')
			sign = -1;
		else
			sign = 1;
		cp++;
	} else
		sign = 1;
	if (!base) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if ((*cp == 'x') && isxdigit(cp[1])) {
				cp++;
				base = 16;
			}
		}
	}
	while (isxdigit(*cp)
	       && (value = isdigit(*cp) ? *cp - '0' : (islower(*cp)
						       ? toupper(*cp) : *cp) -
		   'A' + 10) < base) {
		result = result * base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return (result * sign);
}
#endif

#ifndef __HAVE_ARCH_STRCPY

char *strcpy(char *dest, const char *src)
{
	char *tmp = dest;

	while ((*dest++ = *src++) != '\0')
		/* nothing */;
	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRNCPY

char *strncpy(char *dest, const char *src, unsigned long count)
{
	char *tmp = dest;

	while (count-- && (*dest++ = *src++) != '\0')
		/* nothing */;

	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRCAT

char *strcat(char *dest, const char *src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++) != '\0');

	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRNCAT
char *strncat(char *dest, const char *src, unsigned long count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++)) {
			if (--count == 0)
				break;
		}
	}

	return tmp;
}
#endif

#ifndef __HAVE_ARCH_STRCMP

int strcmp(const char *cs, const char *ct)
{
	register signed char __res;

	while (1) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
	}

	return __res;
}
#endif

#ifndef __HAVE_ARCH_STRNCMP
int strncmp(const char *cs, const char *ct, unsigned long count)
{
	register signed char __res = 0;

	while (count) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
		count--;
	}

	return __res;
}
#endif

#ifndef __HAVE_ARCH_STRCHR

char *strchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}
#endif

#ifndef __HAVE_ARCH_STRLEN

unsigned long strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
#endif

#ifndef __HAVE_ARCH_STRNLEN
unsigned long strnlen(const char *s, unsigned long count)
{
	const char *sc;

	for (sc = s; count-- && *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
#endif

#ifndef __HAVE_ARCH_STRSPN

unsigned long strspn(const char *s, const char *accept)
{
	const char *p;
	const char *a;
	unsigned long count = 0;

	for (p = s; *p != '\0'; ++p) {
		for (a = accept; *a != '\0'; ++a) {
			if (*p == *a)
				break;
		}
		if (*a == '\0')
			return count;
		++count;
	}

	return count;
}
#endif

#ifndef __HAVE_ARCH_STRPBRK

char *strpbrk(const char *cs, const char *ct)
{
	const char *sc1, *sc2;

	for (sc1 = cs; *sc1 != '\0'; ++sc1) {
		for (sc2 = ct; *sc2 != '\0'; ++sc2) {
			if (*sc1 == *sc2)
				return (char *)sc1;
		}
	}
	return NULL;
}
#endif

#ifndef __HAVE_ARCH_STRTOK

char *strtok(char *s, const char *ct)
{
	char *sbegin, *send;

	sbegin = s ? s : ___strtok;
	if (!sbegin) {
		return NULL;
	}
	sbegin += strspn(sbegin, ct);
	if (*sbegin == '\0') {
		___strtok = NULL;
		return (NULL);
	}
	send = strpbrk(sbegin, ct);
	if (send && *send != '\0')
		*send++ = '\0';
	___strtok = send;
	return (sbegin);
}
#endif

#ifndef __HAVE_ARCH_MEMSET

fast_code void *memset(void *s, int c, unsigned int count)
{
	char *xs = (char *)s;

	while (count--)
		*xs++ = c;

	return s;
}
#endif

#ifndef __HAVE_ARCH_BCOPY

void bcopy(const void *src, void *dest, unsigned int count)
{
	char *tmp = dest;
	char *_src = (char *) src;

	while (count--)
		*tmp++ = *_src++;
}
#endif

#ifndef __HAVE_ARCH_MEMCPY

fast_code void *memcpy(void *dest, const void *src, unsigned long count)
{
	char *tmp = (char *)dest, *s = (char *)src;

	while (count--)
		*tmp++ = *s++;

	return dest;
}
#endif

fast_code void *memcpy32(void *dest, const void *src, unsigned long count)
{
	u32 *tmp = (u32 *)dest, *s = (u32 *)src;

	sys_assert((((u32) tmp | (u32) s | (u32) count) & 0x3) == 0);

	/*
	 * If the copy size is aligned with 32 bytes
	 * fw should use asm ldmia & stmia to copy, which is faster
	 */
	if (((count & 0x1F) == 0) && (count > 0)) {
		fastcpy(tmp, s, count);
	} else {
		while (count > 0) {
			*tmp++ = *s++;
			count -= sizeof(u32);
		}
	}

	return dest;
}

#ifndef __HAVE_ARCH_MEMMOVE

void *memmove(void *dest, const void *src, unsigned long count)
{
	char *tmp, *s;

	if (dest <= src) {
		tmp = (char *)dest;
		s = (char *)src;
		while (count--)
			*tmp++ = *s++;
	} else {
		tmp = (char *)dest + count;
		s = (char *)src + count;
		while (count--)
			*--tmp = *--s;
	}

	return dest;
}
#endif

#ifndef __HAVE_ARCH_MEMCMP

int memcmp(const void *cs, const void *ct, unsigned long count)
{
	const unsigned char *su1, *su2;
	signed char res = 0;

	for (su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
		if ((res = *su1 - *su2) != 0)
			break;
	return res;
}
#endif

#ifndef __HAVE_ARCH_MEMSCAN
void *memscan(void *addr, int c, unsigned long size)
{
	unsigned char *p = (unsigned char *)addr;

	while (size) {
		if (*p == c)
			return (void *)p;
		p++;
		size--;
	}
	return (void *)p;
}
#endif

#ifndef __HAVE_ARCH_STRSTR

char *strstr(const char *s1, const char *s2)
{
	int l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	l1 = strlen(s1);
	while (l1 >= l2) {
		l1--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
#endif

fast_data_zi char _ctmp;
#if CPU_ID == 1
sram_sh_data unsigned char _ctype[257] = { 0x00,	/* EOF */
	_C, _C, _C, _C, _C, _C, _C, _C,	/* 0-7 */
	_C, _C | _S, _C | _S, _C | _S, _C | _S, _C | _S, _C, _C,	/* 8-15 */
	_C, _C, _C, _C, _C, _C, _C, _C,	/* 16-23 */
	_C, _C, _C, _C, _C, _C, _C, _C,	/* 24-31 */
	_S | _SP, _P, _P, _P, _P, _P, _P, _P,	/* 32-39 */
	_P, _P, _P, _P, _P, _P, _P, _P,	/* 40-47 */
	_D, _D, _D, _D, _D, _D, _D, _D,	/* 48-55 */
	_D, _D, _P, _P, _P, _P, _P, _P,	/* 56-63 */
	_P, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U,	/* 64-71 */
	_U, _U, _U, _U, _U, _U, _U, _U,	/* 72-79 */
	_U, _U, _U, _U, _U, _U, _U, _U,	/* 80-87 */
	_U, _U, _U, _P, _P, _P, _P, _P,	/* 88-95 */
	_P, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L,	/* 96-103 */
	_L, _L, _L, _L, _L, _L, _L, _L,	/* 104-111 */
	_L, _L, _L, _L, _L, _L, _L, _L,	/* 112-119 */
	_L, _L, _L, _P, _P, _P, _P, _C,	/* 120-127 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 128-143 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 144-159 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 160-175 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 176-191 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 192-207 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 208-223 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 224-239 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0	/* 240-255 */
};

#else
sram_sh_data unsigned char _ctype[257];
#endif