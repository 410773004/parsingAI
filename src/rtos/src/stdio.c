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
#include "misc_register.h"
#include "misc.h"

//#include <stdarg.h>

static char digits[] = { "0123456789ABCDEF" };

extern volatile u8 plp_trigger;

/*!
 * @brief read misc register
 *
 * @param reg	register offset
 *
 * @return	current register value
 */
static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}

/*!
 * @brief object to context
 */
typedef struct ctx {
	unsigned char *targ;	///< string buffer
	int tlen;		///< total length
	int olen;		///< out length
} ctx_t;

static void ctx_putc(ctx_t *ctx, u8 c)
{
	if (ctx == NULL || ctx->targ == NULL) {
		putchar(c);
		return;
	}
	ctx->olen++;
	if (ctx->olen < ctx->tlen) {
		*(ctx->targ) = c;
		ctx->targ++;
	}
}

static int ctx_puts(ctx_t *ctx, char *s)
{
	char *p;

	for (p = s; *p != 0; p++)
		ctx_putc(ctx, *p);

	return p - s;
}

int puts(const char *c)
{
	return ctx_puts(NULL, (char *)c);
}

#if 0
static void hexbyte(ctx_t *ctx, u32 x)
{
	ctx_putc(ctx, digits[(x >> 4) & 0xf]);
	ctx_putc(ctx, digits[(x >> 0) & 0xf]);
}
#endif

static UNUSED void hexword(ctx_t *ctx, u32 x)
{
	ctx_putc(ctx, digits[(x >> 12) & 0xf]);
	ctx_putc(ctx, digits[(x >> 8) & 0xf]);
	ctx_putc(ctx, digits[(x >> 4) & 0xf]);
	ctx_putc(ctx, digits[(x >> 0) & 0xf]);
}

static UNUSED void hexbyte(ctx_t *ctx, u32 x)
{
	ctx_putc(ctx, digits[(x >> 4) & 0xf]);
	ctx_putc(ctx, digits[(x >> 0) & 0xf]);
	ctx_putc(ctx, 'h');
}

static UNUSED void declong(ctx_t *ctx, s32 x)
{
#ifndef __M0__
	u32 tens;

	if (x == 0) {
		ctx_putc(ctx, '0');
		return;
	}

	if (x < 0L) {
		ctx_putc(ctx, '-');
		x = -x;
	}

	tens = 1;
	while (((u32)x / 10UL) >= tens)
		tens *= 10;

	while (tens) {
		int t = x / tens;

		ctx_putc(ctx, digits[t]);
		x -= (t * tens);
		tens /= 10;
	}
#endif
}

static UNUSED void deculong(ctx_t *ctx, u32 x)
{
#ifndef __M0__
	u32 tens;

	if (x == 0) {
		ctx_putc(ctx, '0');
		return;
	}

	tens = 1;
	while ((x / 10) >= tens)
		tens *= 10;

	while (tens) {
		int t = x / tens;

		ctx_putc(ctx, digits[t]);
		x -= (t * tens);
		tens /= 10;
	}
#endif
}

int doprint(char *str, int size, const char *sp, int *vp)
{
	ctx_t octx;
	ctx_t *ctx = &octx;

	ctx->targ = (unsigned char *)str;
	ctx->tlen = size;
	ctx->olen = 0;

	while (*sp) {
		if (*sp != '%') {
			ctx_putc(ctx, *sp);
			sp++;
			continue;
		}
		sp++;

		char *cp = (char *)sp;

		while (*sp == '-' || (*sp >= '0' && *sp <= '9') || *sp == '.')
			sp++;

		switch (*sp) {
		case 'p':	/* %p */
		case 'x':	/* %x */
			hexword(ctx, (u32) (*vp >> 16));
			hexword(ctx, (u32) (*vp & 0xFFFF));
			vp++;
			break;
        case 'w':
            hexword(ctx, (u32) (*vp & 0xFFFF));
            vp++;
            break;
		case 'b':	/* %b */
			hexbyte(ctx, (u32) (*vp & 0xFF));
			vp++;
			break;
		case 'd':	/* %d */
			declong(ctx, (s32) *vp);
			vp++;
			break;
		case 'u':	/* %u */
			deculong(ctx, (u32) *vp);
			vp++;
			break;
		case 'c':	/* %c */
			ctx_putc(ctx, (u8) *vp);
			vp++;
			break;
		case 's':	/* %s */
			cp = *(char **)vp;
			vp += sizeof(char *) / sizeof(int);
			cp += ctx_puts(ctx, cp);
			break;
		case '%':
			ctx_putc(ctx, *sp);
			break;
		default:	/* ignore it */
			break;
		}
		sp++;
	}

	if (ctx->targ) {
		*ctx->targ = '\0';
	}

	return ctx->olen;
}

void mem_dump(void *mem, u32 dwcnt)
{
    u32 *dwm = (u32 *) mem;

 
#if(PLP_SUPPORT == 1)
		gpio_int_t gpio_int_status;
#endif

    while (dwcnt) {
        u32 i;
        u32 len = min(dwcnt, 4);

 #if(PLP_SUPPORT == 1)
		gpio_int_status.all = misc_readl(GPIO_INT);
		if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))||plp_trigger)
			return;
#endif

        evlog_printk(LOG_ALW,"%x: ", dwm);
        for (i = 0; i < len; i++) {
            evlog_printk(LOG_INFO,"%x ", dwm[i]);
        }
        evlog_printk(LOG_INFO,"\n");
        dwcnt -= len;
        dwm += len;
    }
}


int snprintf(char *str, int size, const char *fmt, ...)
{
	int *nextarg = (int *)&fmt;

	nextarg += sizeof(char *) / sizeof(int);

	return doprint(str, size, fmt, nextarg);
}

int printk(const char *fmt, ...)
{
	int *nextarg = (int *)&fmt;

	nextarg += sizeof(char *) / sizeof(int);

	return doprint(NULL, 0, fmt, nextarg);
}
