//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2018 Innogrit Corporation
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
#include "crc16.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "sect.h"
#include "assert.h"
#include "rainier_soc.h"
#include "console.h"
#include "bf_mgr.h"
#include "srb.h"
#include "misc.h"
#include "event.h"
#include "fw_download.h"

#define __FILEID__ xmodem
#include "trace.h"

#define SOH_PAYLOAD_SZ		128
#define STX_PAYLOAD_SZ		1024

#define MAX_RETRANS_COUNT	32

#define XMODEM_BUF_SZ		1030 /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */

#define XMODEM_IMG_POS		(SRAM_BASE + 896 * 1024)
#define XMODEM_IMG_SZ		(SRAM_SIZE - 896 * 1024)

typedef union _xmodem_st_t {
	u32 all;
	struct {
		u32 start : 1;
		u32 xfer_start : 1;
		u32 xfer_abort : 1;
		u32 packet_got : 1;
		u32 eot_got : 1;
		u32 header_err : 1;
		u32 crc_err : 1;
		u32 csum_err : 1;
		u32 err_in_payload : 1;
	} b;
} xmodem_st_t;

enum {
	IDLE = 0,
	XLOADER,
	IMAGE
};

typedef struct _xmodem_t {
	u8 xbuff[XMODEM_BUF_SZ];
	xmodem_st_t st;
	int abort_reason;
	u32 file_sz;
	u32 recv_sz;
	u32 packet_cnt;
	u32 nak_sent;
	u32 escape_before_xfer;
	u16 crc_recv;
	u16 crc_calc;
	u32 recv_cnt_in_payload_when_err;
	u32 state;
	u32 xloader_sz;
	u32 image_sz;

	u16 last_id;
	u16 last_sz;
	u16 last_fsz;
	u16 resent_packet;
} xmodem_t;

#if CPU_ID == 1

fast_data_ni xmodem_t xmodem;
fast_data u8 evt_uart_commit = 0xFF;

static int crc_check(int crc, const u8 *buf, int sz)
{
	if (crc) {
		u16 crc = crc16_ccitt(buf, sz);
		u16 tcrc = (buf[sz] << 8) + buf[sz + 1];

		xmodem.crc_recv = tcrc;
		xmodem.crc_calc = crc;
		if (crc == tcrc)
			return 1;

		xmodem.st.b.crc_err = 1;
	} else {
		int i;
		u8 cks = 0;

		for (i = 0; i < sz; i++)
			cks += buf[i];


		if (cks == buf[sz])
			return 1;

		xmodem.st.b.csum_err = 1;
	}

	return 0;
}

static int chk_payload_sz(const u8 *buf, int sz)
{
#if 1
	return sz;	// image should be 1k aligned
#else
	int i;

	for (i = sz - 1; i >= 0; i--) {
		if (buf[i] != CTRLZ)
			return i + 1;
	}
	return -1;
#endif
}

ddr_code int packet_recv(void)
{
	char c;
	u32 packet_sz;
	u32 payload_sz = 0;
	u32 i;
	bool crc = false;
	int ret;
	bool cancel_if_error = false;

again:
	if (cancel_if_error) {
		putchar_binary(CAN);
		return -2;
	}
	cancel_if_error = true; // set false if allow retry
	do {
		ret = getchar_sync(&c, 2000);

		if (ret < 0)
			continue;

		switch (c) {
		case SOH:
			payload_sz = SOH_PAYLOAD_SZ;
			crc = false;
			break;
		case STX:
			payload_sz = STX_PAYLOAD_SZ;
			crc = true;
			break;
		case EOT:
			return 0;

		case CAN:
			ret = getchar_sync(&c, 1000);
			if (ret >= 0 && c == CAN) {
				uart_flush();
				putchar_binary(ACK);
				return -1; /* canceled by remote */
			}
			continue;
		default:
			return -1;
		}
	} while (0);

	xmodem.st.b.packet_got = 1;
	xmodem.xbuff[0] = c;
	// receive packet
	packet_sz = payload_sz + 3 + crc + 1;

	for (i = 1;  i < packet_sz; i++) {
		ret = getchar_sync(&c, 2000);
		if (ret < 0) {
			xmodem.recv_cnt_in_payload_when_err = i;
			xmodem.st.b.err_in_payload = 1;
			// error, notify host re-send, need to check if host will wait for next nak or just re-send from SOH/STX
			goto again;
		}
		xmodem.xbuff[i] = c;
	}

	u32 h = xmodem.xbuff[1] + xmodem.xbuff[2];

	if (h != 0xFF) {
		xmodem.st.b.header_err = 1;
		goto again;
	}

	if (crc_check(crc, xmodem.xbuff + 3, payload_sz) != 1)
		goto again;

	return chk_payload_sz(xmodem.xbuff + 3, payload_sz);
}

ddr_code int packet_polate(u8 *dst, u8 *src, int len)
{
#if 0
	int i;

	for (i = 0; i < len; i += 2) {
		*dst = src[i];
		dst++;
	}
	return len >> 1;
#else
	memcpy(dst, src, len);
	return len;
#endif
}

ddr_code int xmodem_recv(u8 *dest_buf, int dest_sz)
{
	int len;
	int ret;
	int file_sz = 0;
	int retry = 0;

	xmodem.file_sz = 0;
	xmodem.recv_sz = 0;
	xmodem.packet_cnt = 0;
	xmodem.st.all = 0;
	xmodem.st.b.start = 1;
	xmodem.nak_sent = 0;
	xmodem.escape_before_xfer = 0;
	xmodem.last_id = 0xFFFF;
	xmodem.last_sz = 0;
	xmodem.last_fsz = 0;
	xmodem.resent_packet = 0;
	uart_rx_clear();

	// wait first packet
again:
	xmodem.st.b.xfer_start = 0;
	do {
		int i;

		putchar_binary('C');
		xmodem.nak_sent++;
		for (i = 0; i < 1; i++)
			ret = getchar_sync(NULL, 2000);

	} while (ret != 1 && ++retry < 20);

	xmodem.st.b.xfer_start = 1;
	do {
		len = packet_recv();
		if (len > 0) {
			if (xmodem.last_id != 0xFFFF) {
				u8 cur = (xmodem.last_id + 1) & 0xFF;

				if (cur != xmodem.xbuff[1]) {
					// host re-transfer the same packet
					// cancel last packet
					xmodem.resent_packet++;
					xmodem.packet_cnt--;
					xmodem.recv_sz -= xmodem.last_sz;
					file_sz -= xmodem.last_fsz;
					xmodem.file_sz -= xmodem.last_fsz;
				}
			}

			putchar_binary(ACK);
			xmodem.last_id = xmodem.xbuff[1];
			xmodem.last_sz = len;
			xmodem.packet_cnt++;
			xmodem.recv_sz += len;
			len = packet_polate(dest_buf + file_sz, xmodem.xbuff + 3, len);
			xmodem.last_fsz = len;
			file_sz += len;
			xmodem.file_sz += len;
		} else if (len == 0) {
			xmodem.st.b.eot_got = 1;
			break;
		} else {
			if (file_sz == 0) {
				// xfer is not start yet
				xmodem.escape_before_xfer++;
				goto again;
			}

			xmodem.st.b.xfer_abort = 1;
			xmodem.abort_reason = len;
			putchar_binary(CAN);
			putchar_binary(CAN);
			return 0;
		}
	} while (1);

	uart_flush();
	putchar_binary(ACK);

	return file_sz;
}

ps_code int xmodem_xfer(int img_type)
{
	int ret;
	void *firmware_image;
	int sz;

	if (img_type == XLOADER) {
		firmware_image = (void *)(XMODEM_IMG_POS);
		sz = SRAM_SIZE - 1024 * 1024;
	} else if (img_type == IMAGE) {
		if (xmodem.state != XLOADER)
			return 0;

		firmware_image = (void *)(XMODEM_IMG_POS + xmodem.xloader_sz);
		sz = XMODEM_IMG_SZ - xmodem.xloader_sz;
	} else {
		rtos_xmodem_trace(LOG_ERR, 0x9525, "wrong type %d\n", img_type);
		return 0;
	}

	ret = xmodem_recv((u8 *)firmware_image, sz);

	if (ret < 0) {
		xmodem.state = IDLE;
		rtos_xmodem_trace(LOG_ERR, 0xb6d6, "\nXmodem receive error: status: %d\n", ret);
	} else  {
		//toDO: image crc32 check

		if (img_type == XLOADER) {
			ret = round_up_by_2_power(ret, DTAG_SZE);
			xmodem.xloader_sz = ret;
			xmodem.state = XLOADER;
		} else {
			xmodem.state = IMAGE;
			xmodem.image_sz = ret;
			rtos_xmodem_trace(LOG_ERR, 0x87d2, "\nXmodem successfully received %d bytes\n", ret);
		}
	}

	return 0;
}

ps_code int loader_console(int argc, char *argv[])
{
	xmodem_xfer(XLOADER);
	return 0;
}

ps_code int gxdm_console(int argc, char *argv[])
{
	xmodem_xfer(IMAGE);
	return 0;
}

typedef struct {
	unsigned int identifier;   /* identifier of the section, etc. uEFI, ATCM and so on */
	unsigned int offset;       /* offset of the section into the image */
	unsigned int length;       /* length of the section */
	unsigned int pma;          /* PMA of the section to load */
} section_t;

typedef struct {
	unsigned int signature;     /* equals to IMAGE_SIG */
	unsigned int entry_point;   /* entry point of ELF */
	unsigned short section_num; /* # of sections */
	unsigned short image_dus;   /* # of 2K dus */
	unsigned int section_csum;  /* checksum of section(s) */
	union {
		section_t sections[0];
		fw_slice_t fw_slice[0];
	};
} image_t;

typedef int (*main_t)(void);

ddr_code int go(image_t *image, u32 ep)
{
	int i;

	for (i = 0; i < image->section_num; i++) {
		section_t *section = &image->sections[i];

		rtos_xmodem_trace(LOG_ERR, 0x37b6, "Section(%d): Identifier:0x%x PMA 0x%x@0x%x\n", i, section->identifier, section->pma, section->length);
		if (section->pma != 0xFFFFFFFF && section->length != 0)
			memcpy((void *)section->pma, (void *)(ep + section->offset), section->length);
	}
	rtos_xmodem_trace(LOG_ERR, 0x2270, "Go \033[91m0x%x\x1b[0m to execute ...\n", image->entry_point);
	((main_t)image->entry_point)();
	return -11;
}

ddr_code int xgo_console(int argc, char *argv[])
{
	int ca = atoi(argv[1]);

	if (ca == 0) {
		u32 image_pos;
		image_t *image;
		if (xmodem.state != IMAGE)
			return 0;

		image_pos = XMODEM_IMG_POS + xmodem.xloader_sz;
		image = (image_t *) image_pos;
		if (image->signature == IMAGE_SIGNATURE || image->signature == IMAGE_COMBO || image->signature == IMAGE_CMFG) {
			rtos_xmodem_trace(LOG_ERR, 0x5384, "XGO: CA(%d)\n", ca);
			misc_set_xloader_boot(XMODEM_IMG_POS + xmodem.xloader_sz);
			// this function should be better in ROM
			int rc = go((image_t *) XMODEM_IMG_POS, XMODEM_IMG_POS);
			rtos_xmodem_trace(LOG_ERR, 0xd804, "XGO fail: %d\n", rc);
		} else {
			rtos_xmodem_trace(LOG_ERR, 0x3136, "error image %x\n", image->signature);
		}
	} else if (ca == 1) {
		if (xmodem.state != IMAGE)
			return 0;
		rtos_xmodem_trace(LOG_ERR, 0xc9cb, "XGO: CA(%d)\n", ca);
		if (evt_uart_commit != 0xFF)
			evt_set_cs(evt_uart_commit, XMODEM_IMG_POS + xmodem.xloader_sz, xmodem.image_sz, CS_TASK);
		rtos_xmodem_trace(LOG_ERR, 0x3a40, "XGO: CA(%d) end\n", ca);
	} else {
		rtos_xmodem_trace(LOG_ERR, 0x9367, "error CA %d\n", ca);
	}
	return 0;
}

init_code void xmodem_init(void)
{
	xmodem.state = IDLE;
	return;
}

static DEFINE_UART_CMD(loader, "loader",
		      "loader",
		      "transfer loader",
		      0, 0, loader_console);

static DEFINE_UART_CMD(gxdm, "gxdm",
		      "gxdm",
		      "transfer image",
		      0, 0, gxdm_console);

static DEFINE_UART_CMD(xgo, "xgo", "xgo", "boot from ximage", 1, 1, xgo_console);

#endif

