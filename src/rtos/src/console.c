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

#include "stdio.h"
#include "types.h"
#include "stdlib.h"
#include "sect.h"
#include "event.h"
#include "console.h"
#include "string.h"
#include "evlog.h"
#if defined(SAVE_CDUMP)
#include "mpc.h" //_GENE_ 4cpu cdump
#include "io.h"  //_GENE_ 4cpu cdump
#endif
#define __FILEID__ console
#include "trace.h"
/*!
 * \file console.c
 * @brief support console command to help debug and dump information
 * \addtogroup console
 * @{
 *
 * -# Command history
 * -# Arrow up/down/right/left
 * -# ^p/^n/^a/^e
 * -# Tab key for command auto-completion
 * -# Command cursor move then insert/delete
 * -# Kermit & SecureCRT
 */

#define HISTORY_RECORD_SHIFT  4		///< command history record number shift
#define HISTORY_RECORDS     (1 << HISTORY_RECORD_SHIFT)	///< command history record number
#define HISTORY_RECORDS_MASK (HISTORY_RECORDS - 1)	///< command history record mask
#define LINE_LEN           256				///< max length per line

#define INT_TO_STR2(x) #x
#define INT_TO_STR(x) INT_TO_STR2(x)
#define SSSTC_PROMPTS "ssstc"INT_TO_STR(CPU_ID)"#"

fast_data u8 evt_uart_rx;				///< uart rx event

/*! @brief uart command log */
typedef struct {
	u8 ln_bf[LINE_LEN];	///< line buffer
	u32 len;			///< length of command
} ucmd_log_t;

/*! @brief current mode to parse received char */
enum {
	GENERAL_KEY,		///< normal key
	SPECIAL_KEY_ESC,	///< escape key
	SPECIAL_KEY_5B,

	SPECIAL_KEY_31,
	SPECIAL_KEY_32,

	SPECIAL_KEY_33,
	SPECIAL_KEY_35,
	SPECIAL_KEY_36,

	SPECIAL_KEY_31_31,
	SPECIAL_KEY_31_32,
	SPECIAL_KEY_31_33,
	SPECIAL_KEY_31_34,
	SPECIAL_KEY_31_35,
	SPECIAL_KEY_31_37,
	SPECIAL_KEY_31_38,
	SPECIAL_KEY_31_39,

	SPECIAL_KEY_32_30,
	SPECIAL_KEY_32_31,
	SPECIAL_KEY_32_33,
	SPECIAL_KEY_32_34,
};

fast_data u8 mode = GENERAL_KEY;		///< received mode

slow_data_ni u8 ln_bf[LINE_LEN];		///< received line buffer
slow_data_ni u8 his_ln_bf[LINE_LEN];
slow_data_ni u8 parser_ln_bf[LINE_LEN];		///< parser command buffer

fast_data_zi u32 brushed = 0;			///< index to brush
fast_data_zi u32 ln_idx = 0;			///< index to add new byte
fast_data_zi s32 ln_idx_arrow = 0;
fast_data_zi u32 his_ln_idx = 0;
fast_data_zi s32 his_ln_idx_arrow = 0;

fast_data_zi u32 his_cur = 0;			///< current history index
fast_data_zi u32 his_last = 0;		///< last history index
fast_data_zi u32 his_total = 0;		///< total command in history

slow_data_ni ucmd_log_t ucmd_log[HISTORY_RECORDS];	///< command history buffer

/*!
 * @brief console command: help
 *
 * @param argc		argument count, if 1 dump all console command
 * @param argv		if argument count > 1, it should be command to be requested
 *
 * @return		always return0
 */
#if defined(SAVE_CDUMP)
ddr_code int switch_main(int argc, char *argv[]) //_GENE_20210527 ps_code
#else
ps_code int switch_main(int argc, char *argv[]) //_GENE_20210527 ps_code
#endif
{
	if (argc == 1) {
		rtos_console_trace(LOG_ERR, 0xc034, "current control CPU %d\n", CPU_ID);
		return 0;
	}
#ifdef MPC
	int cpu_id = atoi(argv[1]);

	uart_rx_switch(cpu_id);
#endif
	return 0;
}

/*! @brief help command declaration */
static DEFINE_UART_CMD(switch, "switch",
		"switch [cpu_id]",
		"switch uart control to other CPU",
		0, 1, switch_main);

/*!
 * @brief console command: help
 *
 * @param argc		argument count, if 1 dump all console command
 * @param argv		if argument count > 1, it should be command to be requested
 *
 * @return		always return0
 */
#if defined(SAVE_CDUMP)
ddr_code int help_main(int argc, char *argv[])//_GENE_20210527 ps_code
#else
ps_code int help_main(int argc, char *argv[])//_GENE_20210527 ps_code
#endif
{
	uart_cmd_t **ucmd = (uart_cmd_t **)&__ucmd_start;
	if (argc == 1) {
		rtos_console_trace(LOG_ERR, 0xe14c, "====== Command(s) List ========");
		while (ucmd < (uart_cmd_t **)&__ucmd_end) { /*lint !e681 */
			evlog_printk(LOG_ERR, "%s : %s", (*ucmd)->cmd_desc, (*ucmd)->cmd_brief);
			ucmd++;
		}
		return 0;
	}

	while (ucmd < (uart_cmd_t **)&__ucmd_end) { /*lint !e681 */
		if ((strlen((*ucmd)->cmd_desc) == strlen(argv[1])) &&
				(!strncmp((*ucmd)->cmd_desc, argv[1], strlen(argv[1])))) {
			rtos_console_trace(LOG_ERR, 0x509b, " %s: %s\n", (*ucmd)->cmd_desc, (*ucmd)->cmd_help);
			return 0;
		}
		ucmd++;
	}

	return 0;
}

/*! @brief help command declaration */
static DEFINE_UART_CMD(help, "help",
		"help [cmd]",
		"help display all supported commands or a specifc command",
		0, 1, help_main);
#if defined(SAVE_CDUMP)
#if 1
extern u8 sgfulink_cpu_seq[MAX_MPC];
extern u8 sgfulink_cpu_flag[MAX_MPC];
extern u8 sgfulink;
extern u8 sgfulink_cpu1;
extern u8 sgfulink_cpu2;
extern u8 sgfulink_cpu3;
extern u8 sgfulink_cpu4;
extern void ulink_wait_c2da(void);
extern u32 ulink_da_mode(void);
ddr_code void sgfulink_cpu_seq_init(u8 first)
{
	switch (first) {
		case 1:
			sgfulink_cpu1 = 4;
			sgfulink_cpu2 = 3;
			sgfulink_cpu3 = 2;
			sgfulink_cpu4 = first;
			break;
		case 2:
			sgfulink_cpu1 = 4;
			sgfulink_cpu2 = 3;
			sgfulink_cpu3 = 1;
			sgfulink_cpu4 = first;
			break;
		case 3:
			sgfulink_cpu1 = 4;
			sgfulink_cpu2 = 2;
			sgfulink_cpu3 = 1;
			sgfulink_cpu4 = first;
			break;
		case MAX_MPC:
			sgfulink_cpu1 = 3;
			sgfulink_cpu2 = 2;
			sgfulink_cpu3 = 1;
			sgfulink_cpu4 = first;
			break;
		default:
			break;
	}
	sgfulink_cpu_seq[0] = sgfulink_cpu1;
	sgfulink_cpu_seq[1] = sgfulink_cpu2;
	sgfulink_cpu_seq[2] = sgfulink_cpu3;
	sgfulink_cpu_seq[3] = sgfulink_cpu4;
}
ddr_code void cdump_cpu_flow(u8 first)
{
 	extern void pic_unmask(u32 irq);
 	pic_unmask(21);

	sgfulink_cpu_seq_init(first);

	if ((sgfulink_cpu_flag[0] < 2) || (sgfulink_cpu_flag[1] < 2) || (sgfulink_cpu_flag[2] < 2)) {
 		sgfulink = 0x1;
 		ulink_wait_c2da(); // issue ipc to cpu4~2, entry exception_handler()
 	}

	if (sgfulink_cpu_flag[3] < 2) {
 		ulink_da_mode(); // entry exception_handler()
 	}
}
ddr_code static int cdump_console(int argc, char *argv[])
{
    printk("get cpu id %d\n", get_cpu_id());
    cdump_cpu_flow(get_cpu_id());
	return 0;
}
static DEFINE_UART_CMD(cdump, "cdump",
		"cdump",
		"cdump function",
		0, 0, cdump_console);
#endif
#endif
/*!
 * @brief console command: history
 *
 * dump all command in history
 *
 * @param argc		not used
 * @param argv		not used
 *
 * @return 		always return 0
 */
/*
ps_code int history_main(int argc, char *argv[])
{
	u8 _his_cur = his_cur;

	if (his_total < HISTORY_RECORDS) {
		int i = 0;
		for (i = 0; i < _his_cur; i++) {
			rtos_console_trace(LOG_ERR, 0, "[%d] %s\n", i, ucmd_log[i].ln_bf);
		}
	}
	else {
		s8 i = 0;
		_his_cur = (his_cur + 1) & HISTORY_RECORDS_MASK;
		for (; _his_cur < HISTORY_RECORDS; _his_cur++) {
			rtos_console_trace(LOG_ERR, 0, "[%d] %s\n", i++, ucmd_log[_his_cur].ln_bf);
		}

		for (_his_cur = 0; _his_cur < his_cur; _his_cur++) {
			rtos_console_trace(LOG_ERR, 0, "[%d] %s\n", i++, ucmd_log[_his_cur].ln_bf);
		}
	}

	return 0;
}

//! @brief history command declaration /
static DEFINE_UART_CMD(history, "history",
		"history",
		"display all command execution history",
		0, 0, history_main);
*/
/*!
 * @brief Lookup uart command and reference
 *
 * @param cmd	command description to be looked up
 *
 * @return	uart command pointer if command was found, or null pointer
 */
ps_code uart_cmd_t *ucmd_lookup(char *cmd)
{
	uart_cmd_t **ucmd = (uart_cmd_t **)&__ucmd_start;

	while (ucmd < (uart_cmd_t **)&__ucmd_end) { /*lint !e681 */
		if ((strlen((*ucmd)->cmd_desc) == strlen(cmd)) &&
				(!strncmp((*ucmd)->cmd_desc, cmd, strlen(cmd)))) {
			return *ucmd;
		}
		ucmd++;
	}

	return NULL;
}

/*!
 * @brief Use tab key to speed up command search
 *
 * @return	None
 */
ps_code void lp_bf_cmd_tab()
{
	u32 i;

	/* if command search is gone */
	for (i = 0; i < ln_idx; i++) {
		if (ln_bf[i] == ' ') {
			/* User cares about the tab, record it */
			ln_bf[ln_idx] = '\t';
			if (++ln_idx >= LINE_LEN) {
				ln_idx = 0;
			}
			ln_idx_arrow = ln_idx;
			putc('\t');
			return;
		}
	}

	/* so we are still in command receiving */
	uart_cmd_t **ucmd = (uart_cmd_t **)&__ucmd_start;
	while (ucmd < (uart_cmd_t **)&__ucmd_end) { /*lint !e681 */
		/* we use the first match one */
		if (!strncmp((*ucmd)->cmd_desc, (char *)ln_bf, ln_idx)) {
			int i = 0;
			int delta = strlen((*ucmd)->cmd_desc) - ln_idx;
			memcpy(&ln_bf[ln_idx], (*ucmd)->cmd_desc + ln_idx, delta);

			for (i = 0; i < delta; i++) {
				putc(ln_bf[ln_idx]);
				ln_idx++;
			}
			ln_idx_arrow = ln_idx;
			return;
		}
		ucmd++;
	}
}

/*!
 * @brief discard current input and refresh last command
 *
 * @return 	none
 */
ps_code void ln_bf_refresh()
{
	u32 i = 0;
	for (; i < brushed; i++) {
		putc('\b');
		putc(' ');
		putc('\b');
	}

	for (i = 0; i < ucmd_log[his_last].len; i++) {
		putc(ucmd_log[his_last].ln_bf[i]);
	}

	brushed = ucmd_log[his_last].len;
	memcpy(his_ln_bf, ucmd_log[his_last].ln_bf, brushed);
	his_ln_idx = brushed;
	his_ln_idx_arrow = brushed;
}

/*!
 * @brief check if current command is equal to last one
 *
 * @param bf		current command
 * @param len		command length
 * @param index		current history idnex
 *
 * @return		true if command is the same as last one
 */
ps_code bool ln_bf_dup(u8 *bf, u32 len, u8 index)
{
	if (his_total == 0)
		return false;

	index = (index - 1) & HISTORY_RECORDS_MASK;

	if (!memcmp(ucmd_log[index].ln_bf, bf, len)) {
		return true;
	}

	return false;
}

/*!
 * @brief command line buffer parser
 *
 * add command to history, and search uart command for input
 *
 * @return	None
 */
ps_code void ln_bf_parse(void)
{
	/* Enter to restore shadow */
	ln_idx_arrow = 0;
	his_ln_idx_arrow = 0;

	if (his_last != his_cur) {
		/* overwrite current ln_bf */
		ln_idx = his_ln_idx;
		memcpy(ln_bf, his_ln_bf, ln_idx);
		his_last = his_cur;
	}

	if (ln_idx == 0) {
		his_last = his_cur;
		goto out;
	}

	u8 argc = 1;
	char **argv = NULL;
	char *scmd;
	u32 len = ln_idx;

	char *p = (char *) parser_ln_bf;
	memcpy(parser_ln_bf, ln_bf, len);
	parser_ln_bf[len] = 0;

	/* save current cmd line */
	memcpy(ucmd_log[his_cur].ln_bf, ln_bf, len);
	ucmd_log[his_cur].ln_bf[len] = 0;
	ucmd_log[his_cur].len = len;

	/* trim leading white space */
	while (*p == ' ' || *p == '\t') {p++; len--;}
	scmd = p;
	while (*p != ' ' && *p != '\t' && (*p != 0)) {p++; len--;}
	*p = 0; /* split line buffer for parameters */

	if (strlen(scmd) == 0) {
		his_last = his_cur;
		ln_idx = 0;
		goto out;
	}

	uart_cmd_t *ucmd = ucmd_lookup(scmd);
	if (ucmd == NULL) {
		rtos_console_trace(LOG_ERR, 0xe77d, "%s: command not found\n", scmd);
		goto updt_ln_bf;
	}

	argv = sys_malloc(SLOW_DATA, sizeof(char *) * ucmd->max_argc);
	argv[0] = scmd;

	/* parsing parameters if any */
	while (len > 0) {
		p++; len--;
		while (*p == ' ' || *p == '\t')	{p++; len--;}
		if (*p == 0)
			break;
		argv[argc] = p;
		argc++;
		while (*p != ' ' && *p != '\t' && (*p != 0)) {p++; len--;}
		*p = 0;
	}

	if (argc < ucmd->min_argc) {
		rtos_console_trace(LOG_ERR, 0x7d31, "%s: too few parameters.\n", scmd);
		rtos_console_trace(LOG_ERR, 0xa9de, "%s\n", ucmd->cmd_help);
		goto _free;
	}

	if (argc > ucmd->max_argc) {
		rtos_console_trace(LOG_ERR, 0xb14c, "%s: too many parameter\n", scmd);
		rtos_console_trace(LOG_ERR, 0x0d95, "%s\n", ucmd->cmd_help);
		goto _free;
	}

	if (ucmd->_main(argc, argv) < 0) {
		rtos_console_trace(LOG_ERR, 0xfe5e, "%s\n", ucmd->cmd_help);
	}

_free:
	sys_free(SLOW_DATA, argv);
updt_ln_bf:
	/* if the command is same as the last one */
	if (!ln_bf_dup(ln_bf, ln_idx, his_cur)) {
		his_total++;
		if (++his_cur == HISTORY_RECORDS)
			his_cur = 0;
	}
	his_last = his_cur;
	ln_idx = 0;
out:
	agt_buf_flush_to_uart_all2();
	puts(SSSTC_PROMPTS);
#if 1//!defined(ENABLE_SOUT)
	putc('\n');
#endif
}

/*!
 * @brief uart rx event handler
 *
 * @param param		not used
 * @param payload	not used
 * @param sts		not used
 *
 * @return 		None
 */
static ps_code void uart_console(u32 param, u32 payload, u32 sts)
{
	char c;
	int ret;

	while (1) {
next_char:
		ret = getchar(&c);
		if (ret == -1)
			return;

		switch (c) {
		case 0x10:  /* DLE ^p */
			goto ARROW_UP;
		case 0xE:   /* SO ^n */
			goto ARROW_DOWN;
		case 0x1:   /* SOH ^a */
			if (his_cur == his_last) {
				while (ln_idx_arrow != 0) {
					putc('\b');
					ln_idx_arrow--;
				}
			}
			else {
				while (his_ln_idx_arrow != 0) {
					putc('\b');
					his_ln_idx_arrow--;
				}
			}
			goto next_char;
		case 0x5:   /* EOQ ^e */
			if (his_cur == his_last) {
				while (ln_idx_arrow != ln_idx) {
					putc(ln_bf[ln_idx_arrow]);
					ln_idx_arrow++;
				}
			}
			else {
				while (his_ln_idx_arrow != his_ln_idx) {
					putc(his_ln_bf[his_ln_idx_arrow]);
					his_ln_idx_arrow++;
				}
			}
			goto next_char;
		default:
			break;
		}

/*
ANSI escape sequence is a sequence of ASCII characters, the first two of which are ASCII
"Escape" character 27(1Bh) and the left-bracket character "[" (5Bh).

Up:           0x1B 0x5B 0x41
Down:         0x1B 0x5B 0x42
Right:        0x1B 0x5B 0x43
Left:         0x1B 0x5B 0x44

Ins:          0x1B 0x5B 0x32 0x7E
Del:          0x1B 0x5B 0x33 0x7E
PgUp:         0x1B 0x5B 0x35 0x7E
PgDn:         0x1B 0x5B 0x36 0x7E

F1:           0x1B 0x5B 0x31 0x31 0x7E
F2:           0x1B 0x5B 0x31 0x32 0x7E
F3:           0x1B 0x5B 0x31 0x33 0x7E
F4:           0x1B 0x5B 0x31 0x34 0x7E
F5:           0x1B 0x5B 0x31 0x35 0x7E
F6:           0x1B 0x5B 0x31 0x37 0x7E
F7:           0x1B 0x5B 0x31 0x38 0x7E
F8:           0x1B 0x5B 0x31 0x39 0x7E
F9:           0x1B 0x5B 0x32 0x30 0x7E
F10:          0x1B 0x5B 0x32 0x31 0x7E
F11:          0x1B 0x5B 0x32 0x33 0x7E
F12:          0x1B 0x5B 0x32 0x34 0x7E
*/
		switch (mode) {
		case GENERAL_KEY:
			if (c == 0x1B) {
				mode = SPECIAL_KEY_ESC;
				break;
			}

			if (c == '\r') {
                if((his_last != his_cur)?(his_ln_idx > 0):(ln_idx > 0))
                {
                    putchar(3);
                }
				ln_bf_parse();
				break;
			}

			if (his_cur == his_last) {
				/* Backspace to delete a character */
				if (c == 0x7F || c == '\b') {
					if (ln_idx_arrow == ln_idx) {
						if (ln_idx > 0) {
							ln_idx--;
							ln_idx_arrow--;
							putc('\b');
							putc(' ');
							putc('\b');
						}
					}
					else {
						if (ln_idx_arrow > 0) {
							/* delete a byte */
							u32 i = 0;
							u32 delta = ln_idx - ln_idx_arrow;
							memmove(&ln_bf[ln_idx_arrow - 1], &ln_bf[ln_idx_arrow], delta);

							/* unmask the byte */
							putc('\b');
							putc(' ');
							putc('\b');

							ln_idx--;
							ln_idx_arrow--;

							for (i = 0; i < 1 + delta; i++)
								putc(' ');
							for (i = 0; i < 1 + delta; i++)
								putc('\b');

							for (i = 0; i < delta; i++)
								putc(ln_bf[ln_idx_arrow + i]);
							for (i = 0; i < delta; i++)
								putc('\b');
						}
					}
                    putchar(0x04);
					break;
				}

				if (ln_idx_arrow == ln_idx) {
					if (c == '\t') {
						if (ln_idx == 0)
							break;
						/* Speed up command search */
						lp_bf_cmd_tab();
                        putchar(0x04);
						break;
					}

					ln_bf[ln_idx] = c;
					if (++ln_idx >= LINE_LEN) {
						ln_idx = 0;
					}
					ln_idx_arrow = ln_idx;
				#if 1//defined(ENABLE_SOUT)
					putc(c);
				#endif
				}
				else {
					/* insert a new byte */
					u32 delta = ln_idx - ln_idx_arrow;
					memmove(&ln_bf[ln_idx_arrow + 1], &ln_bf[ln_idx_arrow], delta);
					ln_bf[ln_idx_arrow] = c;

					u32 i = 0;
					for (; i < 1 + delta; i++)
						putc(ln_bf[ln_idx_arrow + i]);
					for (i = 0; i < delta; i++)
						putc('\b');

					if (++ln_idx >= LINE_LEN)
						ln_idx = 0;
					if (++ln_idx_arrow >= LINE_LEN)
						ln_idx_arrow = 0;
				}
			}
			else {
				if (c == 0x7F || c == '\b') {
					if (his_ln_idx_arrow == his_ln_idx) {
						if (his_ln_idx > 0) {
							his_ln_idx--;
							his_ln_idx_arrow--;
							putc('\b');
							putc(' ');
							putc('\b');
						}
					}
					else {
						if (his_ln_idx_arrow > 0) {
							/* delete a byte */
							u32 i = 0;
							u32 delta = his_ln_idx - his_ln_idx_arrow;
							memmove(&his_ln_bf[his_ln_idx_arrow - 1], &his_ln_bf[his_ln_idx_arrow], delta);

							/* unmask the byte */
							putc('\b');
							putc(' ');
							putc('\b');

							his_ln_idx--;
							his_ln_idx_arrow--;

							for (i = 0; i < 1 + delta; i++)
								putc(' ');
							for (i = 0; i < 1 + delta; i++)
								putc('\b');

							for (i = 0; i < delta; i++)
								putc(his_ln_bf[his_ln_idx_arrow + i]);
							for (i = 0; i < delta; i++)
								putc('\b');
						}
					}
                    putchar(0x04);
					break;
				}

				if (his_ln_idx_arrow == his_ln_idx) {
					his_ln_bf[his_ln_idx] = c;
					if (++his_ln_idx >= LINE_LEN) {
						his_ln_idx = 0;
					}
					his_ln_idx_arrow = his_ln_idx;
					putc(c);
				}
				else {
					/* Insert a new byte */
					u32 delta = his_ln_idx - his_ln_idx_arrow;
					memmove(&his_ln_bf[his_ln_idx_arrow + 1], &his_ln_bf[his_ln_idx_arrow], delta);
					his_ln_bf[his_ln_idx_arrow] = c;

					u32 i = 0;
					for (; i < 1 + delta; i++)
						putc(his_ln_bf[his_ln_idx_arrow + i]);
					for (i = 0; i < delta; i++)
						putc('\b');

					if (++his_ln_idx >= LINE_LEN)
						his_ln_idx = 0;
					if (++his_ln_idx_arrow >= LINE_LEN)
						his_ln_idx_arrow = 0;
				}
			}            
            putchar(0x04);
			break;
		case SPECIAL_KEY_ESC:
			if (c != 0x5B) {
				mode = GENERAL_KEY;
				break;
			}
			mode = SPECIAL_KEY_5B;
			break;
		case SPECIAL_KEY_5B:
			mode = GENERAL_KEY;
			switch (c) {
			/* Arrow serial */
			case 0x41:
		ARROW_UP:
				/* Up */
				if (his_total == 0)
                {            
                    putchar(0x04);
					break;
                }

				if (his_last == his_cur)
                {
					brushed = ln_idx;
                    while(ln_idx_arrow < ln_idx)
                    {
                        putc(ln_bf[ln_idx_arrow++]);
                    }
                }
                else
                {
                    while(his_ln_idx_arrow < his_ln_idx)
                    {
                        putc(his_ln_bf[his_ln_idx_arrow++]);
                    }
                }

				u32 nxt = (his_last - 1) & HISTORY_RECORDS_MASK;
				u32 delta = (nxt < his_cur) ? his_cur - nxt : (HISTORY_RECORDS - nxt + his_cur);

				if (delta > his_total)
                {
                    putchar(0x04);
					break;
                }

				his_last = (his_last - 1) & HISTORY_RECORDS_MASK;
				ln_bf_refresh();
                putchar(0x04);
				break;
			case 0x42:
		ARROW_DOWN:
				/* Down */
				if (his_last == his_cur)
                {            
                    while(ln_idx_arrow < ln_idx)
                    {
                        putc(ln_bf[ln_idx_arrow++]);
                    }
                    putchar(0x04);
                    break;
                }
                else
                {
                    while(his_ln_idx_arrow < his_ln_idx)
                    {
                        putc(his_ln_bf[his_ln_idx_arrow++]);
                    }
                }
				his_last = (his_last + 1) & HISTORY_RECORDS_MASK;
                if(his_last == his_cur)
                {
                    u32 i = 0;
                	for (; i < brushed; i++) {
                		putc('\b');
                		putc(' ');
                		putc('\b');
            	    }
                    for(i = 0; i < ln_idx; i++)
                        putc(ln_bf[i]);
                }
                else
                {
				    ln_bf_refresh();
                }
                putchar(0x04);
				break;
			case 0x43:
				/* Right */
				if (his_cur == his_last) {
					if (ln_idx_arrow != ln_idx) {
						putc(ln_bf[ln_idx_arrow]);
						ln_idx_arrow++;
					}
				}
				else {
					if (his_ln_idx_arrow != his_ln_idx) {
						putc(his_ln_bf[his_ln_idx_arrow]);
						his_ln_idx_arrow++;
					}
				}
                putchar(0x04);
				break;
			case 0x44:
				/* Left */
				if (his_cur == his_last) {
					if (ln_idx_arrow != 0) {
						putc('\b');
						ln_idx_arrow--;
					}
				}
				else {
					if (his_ln_idx_arrow != 0) {
						putc('\b');
						his_ln_idx_arrow--;
					}
				}
                putchar(0x04);
				break;
			case 0x31:
				mode = SPECIAL_KEY_31;
				break;
			case 0x32:
				mode = SPECIAL_KEY_32;
				break;
			/* Del/PgUp/PgDn */
			case 0x33:
				mode = SPECIAL_KEY_33;
				break;
			case 0x35:
				mode = SPECIAL_KEY_35;
				break;
			case 0x36:
				mode = SPECIAL_KEY_36;
				break;
			}
			break;
		case SPECIAL_KEY_31:
			mode = GENERAL_KEY;
			switch (c) {
			/* Fn serial */
			case 0x31:
				mode = SPECIAL_KEY_31_31;
				break;
			case 0x32:
				mode = SPECIAL_KEY_31_32;
				break;
			case 0x33:
				mode = SPECIAL_KEY_31_33;
				break;
			case 0x34:
				mode = SPECIAL_KEY_31_34;
				break;
			case 0x35:
				mode = SPECIAL_KEY_31_35;
				break;
			case 0x37:
				mode = SPECIAL_KEY_31_37;
				break;
			case 0x38:
				mode = SPECIAL_KEY_31_38;
				break;
			case 0x39:
				mode = SPECIAL_KEY_31_39;
				break;
			}
			break;
		/* Fn 1-8 */
		case SPECIAL_KEY_31_31:
			mode = GENERAL_KEY;
			/* F1 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F1\n");
			break;
		case SPECIAL_KEY_31_32:
			mode = GENERAL_KEY;
			/* F2 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F2\n");
			break;
		case SPECIAL_KEY_31_33:
			mode = GENERAL_KEY;
			/* F3 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F3\n");
			break;
		case SPECIAL_KEY_31_34:
			mode = GENERAL_KEY;
			/* F4 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F4\n");
			break;
		case SPECIAL_KEY_31_35:
			mode = GENERAL_KEY;
			/* F5 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F5\n");
			break;
		case SPECIAL_KEY_31_37:
			mode = GENERAL_KEY;
			/* F6 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F6\n");
			break;
		case SPECIAL_KEY_31_38:
			mode = GENERAL_KEY;
			/* F7 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F7\n");
			break;
		case SPECIAL_KEY_31_39:
			mode = GENERAL_KEY;
			/* F8 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F8\n");
			break;
		case SPECIAL_KEY_32:
			mode = GENERAL_KEY;
			switch (c) {
			case 0x7E:
				// rtos_console_trace(LOG_ERR, 0, "unsupported Ins\n");
				break;
			case 0x30:
				mode = SPECIAL_KEY_32_30;
				break;
			case 0x31:
				mode = SPECIAL_KEY_32_31;
				break;
			case 0x33:
				mode = SPECIAL_KEY_32_33;
				break;
			case 0x34:
				mode = SPECIAL_KEY_32_34;
				break;
			}
			break;
		/* Del/PgUp/PgDn */
		case SPECIAL_KEY_33:
			mode = GENERAL_KEY;
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported Del\n");
			break;
		case SPECIAL_KEY_35:
			mode = GENERAL_KEY;
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported PgUp\n");
			break;
		case SPECIAL_KEY_36:
			mode = GENERAL_KEY;
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported PgDn\n");
			break;

		/* Fn 9-12 */
		case SPECIAL_KEY_32_30:
			mode = GENERAL_KEY;
			/* F9 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F9\n");
			break;
		case SPECIAL_KEY_32_31:
			mode = GENERAL_KEY;
			/* F10 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F10\n");
			break;
		case SPECIAL_KEY_32_33:
			mode = GENERAL_KEY;
			/* F11 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F11\n");
			break;
		case SPECIAL_KEY_32_34:
			mode = GENERAL_KEY;
			/* F12 */
			// if (c == 0x7E)
			// 	rtos_console_trace(LOG_ERR, 0, "unsupported F12\n");
			break;
		default:
			mode = GENERAL_KEY;
		}
	}
}

/*!
 * @brief console command initialization
 *
 * register uart rx event handler
 *
 * @return 	none
 */
init_code void console_init(void)
{
	(void) evt_register(uart_console, 0, &evt_uart_rx);
}

/*! @} */
