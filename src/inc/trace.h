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

#include "evt_trace_log.h"

/*lint -e516 -e718 -e746 -save */
#include "trace-eventid.h"
#if defined(ENABLE_SOUT)
#include "trace-fmtstr.h"
#endif

typedef struct {
    u32 timestamp;
    u16 eventid;
    u16 seqid;
    u32 r[6];
} trace_data_t; /* 32byte */

void trace_init(log_level_t level);
log_level_t log_level_chg(log_level_t lvl);
void trace_cat_set(trace_cat_t cat, log_level_t level);

void _trace_0(log_level_t level, u16 eventid);
void _trace_1(log_level_t level, u16 eventid, u32 r0);
void _trace_2(log_level_t level, u16 eventid, u32 r0, u32 r1);
void _trace_3(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2);
void _trace_4(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2, u32 r3);
void _trace_5(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4);
void _trace_6(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5);
extern log_stat_t log_isr;
extern log_level_t level;
#define trace0(cat, _level, eventid) \
	do {\
        if(_level >= level)\
            _trace_0(_level, eventid);\
	} while (0)

#define trace1(cat, _level, eventid, r0) \
	do {\
        if(_level >= level)\
            _trace_1(_level, eventid, r0);\
	} while (0)

#define trace2(cat, _level, eventid, r0, r1) \
	do {\
        if(_level >= level)\
            _trace_2(_level, eventid, r0, r1);\
	} while (0)

#define trace3(cat, _level, eventid, r0, r1, r2) \
	do {\
        if(_level >= level)\
            _trace_3(_level, eventid, r0, r1, r2);\
	} while (0)

#define trace4(cat, _level, eventid, r0, r1, r2, r3) \
	do {\
        if(_level >= level)\
            _trace_4(_level, eventid, r0, r1, r2, r3);\
	} while (0)

#define trace5(cat, _level, eventid, r0, r1, r2, r3, r4) \
	do {\
        if(_level >= level)\
            _trace_5(_level, eventid, r0, r1, r2, r3, r4);\
	} while (0)

#define trace6(cat, _level, eventid, r0, r1, r2, r3, r4, r5) \
	do {\
        if(_level >= level)\
            _trace_6(_level, eventid, r0, r1, r2, r3, r4, r5);\
	} while (0)


#if !defined(PERF_BUILD)
#define MKFN(fn, ...) MKFN_N(fn, ##__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)(__VA_ARGS__)
#else
#define MKFN(fn, ...)
#endif
#define MKFN_N(fn, NR, n0, n1, n2, n3, n4, n5, n6, n7, n8, n, ...) fn##n

#ifndef __FILEID__
# error "must define __FILEID__ before include trace.h"
#endif
/*lint -restore*/
