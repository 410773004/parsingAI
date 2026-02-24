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
#include "btn_cmd_data_reg.h"
#include "btn_export.h"

#if CPU_ID == 1
#define BTN_INT_MASK	BTN_INT_MASK0
#define BTN_MK_INT_STS	BTN_MK_INT_STS0
#elif CPU_ID == 2
#define BTN_INT_MASK	BTN_INT_MASK1
#define BTN_MK_INT_STS	BTN_MK_INT_STS1
#elif CPU_ID == 3
#define BTN_INT_MASK	BTN_INT_MASK2
#define BTN_MK_INT_STS	BTN_MK_INT_STS2
#elif CPU_ID == 4
#define BTN_INT_MASK	BTN_INT_MASK3
#define BTN_MK_INT_STS	BTN_MK_INT_STS3
#else
#error "wrong CPU id"
#endif


