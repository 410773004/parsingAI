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

#include "sect.h"

#if defined(MPC)
#define ucmd_code	fast_code
#else
#define ucmd_code	slow_code
#endif

/*!
 * \file console.h
 * @brief define console command interface
 *
 * \addtogroup console
 * @{
 */

/*! @brief define uart command structure */
typedef struct {
	const char *cmd_desc;			///< command description
	const char *cmd_brief;			///< command brief
	const char *cmd_help;			///< command help
	u8 min_argc;			///< minimal argument count
	u8 max_argc;			///< maximal argument count
	int (*_main)(int argc, char *argv[]);	///< uart command function
} uart_cmd_t;

/*! @brief declare uart command, the function could be found in section ucmd_tbl */
#define DEFINE_UART_CMD(ucmd, desc, brief, help, argc_min, argc_max, main)     \
	fast_data uart_cmd_t uart_cmd_##ucmd = {                               \
		.cmd_desc = desc,                                              \
		.cmd_brief = brief,                                            \
		.cmd_help = help,                                              \
		.min_argc = (argc_min) + 1,                                    \
		.max_argc = (argc_max) + 1,                                    \
		._main    = main,                                              \
	};                                                                     \
	const uart_cmd_t ucmd_tbl *_uart_cmd_##ucmd = &uart_cmd_##ucmd;

/*! @} */
