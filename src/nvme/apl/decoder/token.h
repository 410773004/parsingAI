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

#pragma once

typedef struct token {
	u8 *buf;
	int len;
} token_t;

void token_init(token_t *, u8 *buf, int len);

typedef enum {
	SINT,
	UINT,
	STRING,

	/* core 3.2.2.3 , page35 Table 04 */
	AT_TINY,  /* 0x00 ... 0x7F */
	AT_SHORT, /* 0x80 ... 0x8F */
	AT_MEDIUM,/* 0xC0 ... 0xDF */
	AT_LONG,  /* 0xE0 ... 0xE3 */

	AT_BYTE4 = 0xA4,
	AT_BYTE8 = 0xA8,
	AT_BYTE9 = 0xA9,

	TK_SL = 0xF0,
	TK_EL = 0xF1,
	TK_SN = 0xF2,
	TK_EN = 0xF3,

	TK_CALL = 0xF8,
	TK_EOD  = 0xF9,
	TK_EOS  = 0xFA,
	TK_ST   = 0xFB,
	TK_ET   = 0xFC,
	TK_EMPTY= 0xFF,
} token_type_t;

void token_put_u64(token_t *, u64);
void token_put_u32(token_t *, u32);
void token_put_u16(token_t *, u16);
void token_put_u8 (token_t *, u8);
void token_put_str(token_t *, const char *);
void token_put_bytes(token_t*,const u8 *, int len);
void token_put_b8(token_t *,  u8 *);
void token_put_eod(token_t *, u8);
void token_put_status_list(token_t *, u8);
void token_put_pad_bytes(token_t *);

void token_put_name_value(token_t *, const char *, u32, u32);

token_type_t token_type_len(token_t *, int *len, u8 **);
token_type_t token_dta(token_type_t);
int token_eof(token_t *tk);

void token_dump(token_t *);

