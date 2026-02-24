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

///< ONFI devices
#if HAVE_MICRON_SUPPORT
#include "ndcmd_fmt_mu.h"
#endif

#if HAVE_UNIC_SUPPORT
#include "ndcmd_fmt_unic.h"
#endif

#if HAVE_YMTC_SUPPORT
#include "ndcmd_fmt_ymtc.h"
#endif

///< Toggle devices
#if HAVE_TSB_SUPPORT
#include "ndcmd_fmt_tsb.h"
#endif

#if HAVE_HYNIX_SUPPORT
#include "ndcmd_fmt_sk.h"
#endif

#if HAVE_SANDISK_SUPPORT
#include "ndcmd_fmt_sndk.h"
#endif

#if HAVE_SAMSUNG_SUPPORT
#include "ndcmd_fmt_ss.h"
#endif
