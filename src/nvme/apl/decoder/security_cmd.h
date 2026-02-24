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

#ifdef HAVE_CONFIG_H
/* XXX: we detect the sedutils build option by using HAVE_CONFIG_H */
#define _WITH_SED_UTILS_
#endif

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef _WITH_SED_UTILS_

#include <stdint.h> /* for uintx_t */

enum cmd_rslt_t {
	HANDLE_RESULT_FINISHED = 0,	///< command finished
	HANDLE_RESULT_FAILURE,		///< command failed
	HANDLE_RESULT_RUNNING,     	///< command is still running
	HANDLE_RESULT_PENDING_FE,	///< pending in the front-end
	HANDLE_RESULT_PENDING_BE,	///< pending in the back-end
	HANDLE_RESULT_DATA_XFER,	///< command is transferring data
	HANDLE_RESULT_PRP_XFER,		///< command is transferring prp list
};
typedef struct req req_t;

#include <stdio.h>

#define dprintf(fmt, ...) \
	do { \
		printf(" <fw> %20s:%04d ", __func__, __LINE__); \
		printf(fmt, ##__VA_ARGS__); fflush(stdout); \
	} while (0)

#include <string.h> /* memset */
#include <linux/nvme_ioctl.h>

#define nvme_opc opcode
typedef struct nvme_passthru_cmd nvme_cmd_t;

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

#else /* without sed utils */

#define dprintf(fmt, ...) \
    do { \
		printk(fmt, ##__VA_ARGS__); \
    } while (0)

#include "nvme_precomp.h"
#include "req.h"
#include "nvmet.h"

#define nvme_opc opc
typedef struct nvme_cmd nvme_cmd_t;

#endif

enum cmd_rslt_t nvmet_security_handle_cmd(req_t *req, nvme_cmd_t *cmd, u8 *buf, int buflen);

#ifdef  __cplusplus
}
#endif
