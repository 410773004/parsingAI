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


#include "types.h"
#include "stdio.h"
#include "io.h"
#include "misc.h"
#include "console.h"
#include "sect.h"
#include "string.h"
#include "assert.h"
#if defined (RDISK)
#include "evlog.h"
#endif

#define __FILEID__ trace
#include "trace.h"

#include "rainier_soc.h"

/* 1024 * 32 = 32k */
#define TRACE_BUF_NUM 1024

typedef struct {
	u32 magic;
	u32 fmt_crc;
	u32 wptr;
	u32 rptr;
	u32 pad[4];
} trace_hdr_t;

fast_data log_level_t level = LOG_INFO;
fast_data log_stat_t log_isr = LOG_IRQ_DOWN;

fast_data_zi static log_buf_t st_header, st_buf1, st_buf2, st_buf3;
#if CPU_ID == 1
volatile share_data u32 log_number = 1;
#else
volatile share_data u32 log_number;
#endif

log_level_t log_level_chg(log_level_t lvl)
{
	log_level_t old = level;

	level = lvl;
	return old;
}


/**
 * trace_init - init trace subsystem
 * @level: default log level
 */
void trace_init(log_level_t level)
{

}

/**
 * trace_set - update trace log level
 * @cat: cat name
 * @level: log level
 */
void trace_set(trace_cat_t cat, log_level_t level)
{
	/* TODO */
}

void _trace_0(log_level_t level, u16 eventid)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = 0;
	st_buf1.encode_ex.vp[1]     = 0;
	st_buf2.encode_ex.vp[0]     = 0;
	st_buf2.encode_ex.vp[1]     = 0;
	st_buf3.encode_ex.vp[0]     = 0;
	st_buf3.encode_ex.vp[1]     = 0;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}

void _trace_1(log_level_t level, u16 eventid, u32 r0)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = r0;
	st_buf1.encode_ex.vp[1]     = 0;
	st_buf2.encode_ex.vp[0]     = 0;
	st_buf2.encode_ex.vp[1]     = 0;
	st_buf3.encode_ex.vp[0]     = 0;
	st_buf3.encode_ex.vp[1]     = 0;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}

void _trace_2(log_level_t level, u16 eventid, u32 r0, u32 r1)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = r0;
	st_buf1.encode_ex.vp[1]     = r1;
	st_buf2.encode_ex.vp[0]     = 0;
	st_buf2.encode_ex.vp[1]     = 0;
	st_buf3.encode_ex.vp[0]     = 0;
	st_buf3.encode_ex.vp[1]     = 0;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}

void _trace_3(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = r0;
	st_buf1.encode_ex.vp[1]     = r1;
	st_buf2.encode_ex.vp[0]     = r2;
	st_buf2.encode_ex.vp[1]     = 0;
	st_buf3.encode_ex.vp[0]     = 0;
	st_buf3.encode_ex.vp[1]     = 0;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}

void _trace_4(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2, u32 r3)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = r0;
	st_buf1.encode_ex.vp[1]     = r1;
	st_buf2.encode_ex.vp[0]     = r2;
	st_buf2.encode_ex.vp[1]     = r3;
	st_buf3.encode_ex.vp[0]     = 0;
	st_buf3.encode_ex.vp[1]     = 0;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}

void _trace_5(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = r0;
	st_buf1.encode_ex.vp[1]     = r1;
	st_buf2.encode_ex.vp[0]     = r2;
	st_buf2.encode_ex.vp[1]     = r3;
	st_buf3.encode_ex.vp[0]     = r4;
	st_buf3.encode_ex.vp[1]     = 0;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}

void _trace_6(log_level_t level, u16 eventid, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5)
{
    #if(CPU_ID == 1)
    //DIS_ISR();
	BEGIN_CS1
    #endif
	st_header.encode.log_number = log_number++;
	st_header.encode.info.is_encoded = 1;
	st_header.encode.info.cpu_id = CPU_ID;
	st_header.encode.info.log_level = level;
	st_header.encode.info.length = 6;
	st_header.encode.info.event_id  = eventid;
	st_buf1.encode_ex.vp[0]     = r0;
	st_buf1.encode_ex.vp[1]     = r1;
	st_buf2.encode_ex.vp[0]     = r2;
	st_buf2.encode_ex.vp[1]     = r3;
	st_buf3.encode_ex.vp[0]     = r4;
	st_buf3.encode_ex.vp[1]     = r5;

	evlog_save_encode(level, &st_header, &st_buf1, &st_buf2, &st_buf3);
    #if(CPU_ID == 1)
    //EN_ISR();
	END_CS1
    #endif
}
