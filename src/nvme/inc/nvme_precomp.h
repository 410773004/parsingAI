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

#include "types.h"
#include "io.h"
#include "string.h"
#include "stdio.h"
#include "errno.h"
#include "pool.h"
#include "stdlib.h"
#include "task.h"
#include "dma.h"
#include "bitops.h"

#include "pic.h"
#include "irq.h"

#include "mod.h"

#include "list.h"

#define FEATURE_SUPPORTED	1
#define FEATURE_NOT_SUPPORTED	0

#define VALIDATE_BOOT_PARTITION 0
#define BOOT_PARTITION_FEATURE_SUPPORT    1    /* For tnvme grp 1 test pass*/


/*Features Support Definitino*/
#define POWER_MANAGE_FEATURE		1		//02h  power management
#define AUTO_POWER_STATE_FEATURE	0		//0ch  autonomous power state transition
#define HOST_THERMAL_MANAGE			1		//10h  host thermal manage

#if (Lenovo_case)
#define EN_PLP_FEATURE				0		//F0h  Enable PLP(Lenovo)
#else
#define EN_PLP_FEATURE				1		//F0h  Enable PLP(Lenovo)
#endif

#define EN_PWRDIS_FEATURE			0		//D6h  Enable PWRDIS(Lenovo)


#define HOST_NVME_FEATURE_HMB		0

#if defined(SRIOV_SUPPORT)
#define HOST_NVME_FEATURE_SR_IOV 		1
#else
#define HOST_NVME_FEATURE_SR_IOV 		0
#endif

#define CO_SUPPORT_SECURITY             1
#define CO_SUPPORT_SANITIZE             1
#define CO_SUPPORT_WRITE_ZEROES         0
#define CO_SUPPORT_COMPARE              0
#if (_WUNC_)
#define CO_SUPPORT_WRITE_UNCORRECTABLE  1
#else
#define CO_SUPPORT_WRITE_UNCORRECTABLE  0
#endif
#define CO_SUPPORT_NSSR_PERST_FLOW      1
#define CO_SUPPORT_DEVICE_SELF_TEST     1


#define CO_SUPPORT_OCP					1

#define degrade_mode 					ENABLE

#define ACTIVE_COMMAND_COUNT     64
#if defined(RAWDISK)
#define NVME_ACTIVE_COMMAND_COUNT (ACTIVE_COMMAND_COUNT)
#else
#define NVME_ACTIVE_COMMAND_COUNT (ACTIVE_COMMAND_COUNT)
#endif
#define NUM_CMDP_RX_CMD      16	///< number of lot for cmd_proc rx nvme command
#define IO_SQUEUE_MAX_DEPTH      8                                              ///< controller I/O submission queue depth
#if defined(PROGRAMMER)
#define IO_CMD_IN_QUEUE    (IO_SQUEUE_MAX_DEPTH * 2)	///< controller command capacity in queue
#else
#define IO_CMD_IN_QUEUE    (IO_SQUEUE_MAX_DEPTH)	///< controller command capacity in queue
#endif
#define SRAM_NVME_CMD_CNT  (IO_CMD_IN_QUEUE + NVME_ACTIVE_COMMAND_COUNT + NUM_CMDP_RX_CMD)	///< controller command capacity in SRAM

#define temp_55C 			0x12F		//30C
#define temp_84C			0x165		//84C
#define	temp_65C			0x152		//65C

#define c_deg_to_k_deg(x)	(x + 273)
#define k_deg_to_c_deg(x)	(x - 273)
