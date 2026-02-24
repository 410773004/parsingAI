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

#include "security_cmd.h"
#include "token.h"
#include "endian.h"

void token_init(token_t *tk, u8 *buf, int len)
{
	tk->buf = buf;
	tk->len = len;
}

int token_eof(token_t *tk)
{
	return tk->len <= 0;
}

token_type_t token_dta(token_type_t t)
{
	u8 tok = t;

	if (!(tok & 0x80)) {        /* tiny atom  */
		return (tok & 0x40) ? SINT : UINT;
	} else if (!(tok & 0x40)) { /* short atom */
		return (tok & 0x20) ? STRING :
			(tok & 0x10) ? SINT : UINT;
	} else if (!(tok & 0x20)) { /* long atom  */
		return (tok & 0x10) ? STRING :
			(tok & 0x08) ? SINT : UINT;
	} else if (!(tok & 0x10)) { /* media atom */
		return (tok & 0x02) ? STRING :
			(tok & 0x01) ? SINT : UINT;
	} else
		return t;
}

token_type_t token_type_len(token_t *tk, int *len, u8 **pp)
{
	u8 tok = *tk->buf;
	token_type_t t = (token_type_t) tok;
	int dpos = 0;

	if (!(tok & 0x80)) {        /* tiny atom */
		*len = 1;
	} else if (!(tok & 0x40)) { /* short atom */
		*len = (tok & 0x0f) + 1;
		dpos = 1;
	} else if (!(tok & 0x20)) { /* medium atom */
		*len = ((tok & 0x07) << 8) + tk->buf[1] + 2;
		dpos = 2;
	} else if (!(tok & 0x10)) { /* long atom */
		*len = (tk->buf[1] << 16) + (tk->buf[2] << 8) + tk->buf[3] + 4;
		dpos = 4;
	} else {
		*len = 1;
	}

	if (pp)
		*pp = tk->buf + dpos;

	tk->buf += *len;
	tk->len -= *len;

	*len -= dpos;

	return t;
}

#if 0
void token_dump(token_t *tk)
{
	do {
		int tlen;
		u8 *pp;

		token_type_t t = token_type_len(tk, &tlen, &pp);

		dprintf(" token 0x%x, tlen 0x%d, ", t, tlen);
		for (int i = 0; i < tlen; i++) {
			dprintf("%x ", pp[i]);
		}
		dprintf("|");
		for (int i = 0; i < tlen; i++) {
			dprintf("%c", isprint(pp[i]) ? pp[i] : '.');
		}
		dprintf("\n");
	} while (tk->len > 0);
}
#endif

void token_put_u8(token_t *tk, u8 val)
{
	*tk->buf = val;
	tk->buf++;
	tk->len++;
}

void token_put_u16(token_t *tk, u16 val)
{
	cpu_to_be16(val, tk->buf);
	tk->buf += 2;
	tk->len += 2;
}

void token_put_u32(token_t *tk, u32 val)
{
	cpu_to_be32(val, tk->buf);
	tk->buf += 4;
	tk->len += 4;
}

void token_put_u64(token_t *tk, u64 val)
{
	cpu_to_be64(val, tk->buf);
	tk->buf += 8;
	tk->len += 8;
}

void token_put_bytes(token_t *tk, const u8 *name, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		tk->buf[i] = name[i];
	}
	tk->buf += len;
	tk->len += len;
}

/* this is NULL terminated string */
void token_put_str(token_t *tk, const char *str)
{
	u8 len = strlen(str);

	token_put_bytes(tk, (const u8 *)str, len);
	tk->buf += len;
	tk->len += len;
}

void token_put_status_list(token_t *tk, u8 sc)
{
	token_put_u8(tk, 0xf0);
	token_put_u8(tk, sc);
	token_put_u8(tk, 0x00);
	token_put_u8(tk, 0x00);
	token_put_u8(tk, 0xf1);
}

void token_put_pad_bytes(token_t *tk)
{
	u8 pad, i;

	pad = 4 - (tk->len % 4);
	for (i = 0; i < pad; i++) {
		token_put_u8(tk, 0x00);
	}
}

/* tk - token buffer, name: name of property,
 * value: value of property, name_len: length of name in bytes.
 */
void token_put_name_value(token_t *tk, const char *name, u32 value, u32 name_len)
{
	u8 val_u8;
	u16 val_u16;
	u32 val_u32;

	/* start name */
	token_put_u8(tk, TK_SN);

	/* name size = token and size */
	if (name_len < 0x10) {
		val_u8 = 0xa0 + name_len;
		token_put_u8(tk, val_u8);
	} else {
		val_u16 = 0xd000 + name_len;
		token_put_u16(tk, val_u16);
	}

	/* name - use token_put_bytes() */
	token_put_bytes(tk, (const u8 *)name, name_len);

	/* value size - token and size */
	if (value < 0x10) {
		token_put_u8(tk, (u8)value);
	} else if (value < 0x100) {
		val_u16 = 0x8100 + (u16)value;
		token_put_u16(tk, val_u16);
	} else if (value < 0x10000) {
		token_put_u8(tk, 0x82);
		token_put_u16(tk, (u16)value);
	} else if (value < 0x1000000) {
		val_u32 = 0x83000000 + value;
		token_put_u32(tk, val_u32);
	}

	/* end name */
	token_put_u8(tk, TK_EN);
}
