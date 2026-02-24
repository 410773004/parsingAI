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

#ifndef _STDIO_H_
#define _STDIO_H_

#include "evlog.h"

/*!
 * \file stdio.h
 * @brief define std in/out
 *
 * \addtogroup utility
 * @{
 */

#define SOH	0x01
#define STX	0x02
#define ETX	0x03	/* Ctrl-C */
#define EOT	0x04	/* Ctrl-D */
#define ACK	0x06
#define NAK	0x15
#define CAN	0x18
#define CTRLZ	0x1A

void update_error_log_cnt_and_show_func_line(const char *func, int line);

/*!
 * @brief format buffer
 *
 * currently understands: \%p, \%x, \%d, \%u, \%c, \%s
 *
 * @param str		buffer to store output
 * @param size		size of buffer
 * @param format	format of data that will be stored in buffer
 *
 * @return		This routine returns to the caller with bytecnt, or error it returns error-no.
 */
int snprintf(char *str,
	int size,
	const char *format,
	...
    );

void mem_dump(void *mem, u32 dwcnt);

/*!
 * @brief print to console
 *
 * @param format	format of data that will be printed
 *
 * @return		This routine returns to the caller with bytecnt, or error it returns error-no.
 */
int printk(const char *fmt, ...);

/*!
 * @brief print str to console
 *
 * @param s	string that will be printed
 *
 * @return	This routine returns to the caller with bytecnt, or error it returns error-no.
 */
int puts(const char *s);

/*!
 * @brief print char to console
 *
 * @param c	character that will be printed
 *
 * @return	This routine returns to the caller with bytecnt, or error it returns error-no.
 */
int putchar(int c);

int putcharex(int c, unsigned int count_out);

/*!
 * @brief print char to console with binary mode
 *
 * @param c	character that will be printed
 *
 * @return	This routine returns to the caller with bytecnt, or error it returns error-no.
 */
int putchar_binary(int c);

/*!
 * @brief read char from console
 *
 * @param c	character that will be read from cosole
 *
 * @return	This routine returns to the caller with char.
 */
int getchar(char *c);

/*!
 * @brief read char from console with sync mode
 *
 * @param c		character that will be read from cosole
 * @param timeout_ms	timeout value
 *
 * @return	This routine returns to the caller with char.
 */
int getchar_sync(char *c, unsigned int timeout_ms);

/*!
 * @brief UART TX flush
 *
 * @return	not used
 */
void uart_flush(void);

/*!
 * @brief UART RX cleanup
 *
 * @return	not used
 */
void uart_rx_clear(void);

/*!
 * @brief show panic message to console
 *
 * @param str	panic message that will be printed
 *
 * @return	None
 */
#define panic(str) do{                                               \
    update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__); \
    _panic(str);                                                     \
} while(0)

void _panic(const char *str);

/*!
 * @brief internal function for format string
 *
 * @param str	buffer for holder string, NULL will output to console
 * @param size	how many size of the string
 * @param sp	printf()-style format string
 * @param vp	pointer to variables
 *
 * @return	number of characters that would have been printed.
 */
int doprint(char *str, int size, const char *sp, int *vp);

#define putc(x) putchar(x)

/*!
 * @brief switch uart rx control CPU
 *
 * @param cpu_id	RX control CPU id to be set
 *
 * @return		return -1 if CPU id is illegal
 */
int uart_rx_switch(int cpu_id);

/*! @} */

#endif
