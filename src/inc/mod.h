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

typedef void (*mod_init_t)(void);

#define __sect_name(order) ".sysinit."stringify(CPU_ID_0)"."tostring(order)

#define module_init(fn, order) \
    const mod_init_t __attribute__((__section__(__sect_name(order)))) _init_##fn = fn;

#define RTOS_PRE    a.0.0
#define RTOS_UART   a.0.1
#define RTOS_VER    a.0.2
#define RTOS_CORE   a.1
#define RTOS_ARCH   a.2

#define BTN_PRE     a.3.0
#define BTN_APL     a.3.1

#define NVME_PRE    b.0
#define NVME_APL    b.1
#define NVME_HAL    b.2

#define NCL_PRE     b.9

#define NCL_APL     c.0
#define FTL_APL     c.1
#define MISC_APL    z.0
#define DISP_APL    z.1
#define ELOG_APL    z.2

#if 0 //20210401-Eddie- Resume flow
#define NVME_WB_HAL     z.3		//20210326-Eddie
#endif
