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

// todo: change to 1024 when new ncl is ready
//#define RA_DTAG_CNT (1024) ///< 4MB RA DRAM buffer
//#define RA_DTAG_CNT (512) ///< 2MB RA DRAM buffer
#define RA_DTAG_CNT (384) ///< 2MB RA DRAM buffer

#define RA_CMD_SEQUENTIAL_CNT				32

#define RA_ERROR_WORKAROUND

extern u16 ra_btag;
extern u32 shr_ra_ddtag_start;

extern bool ra_data_out(u32 btag, lda_t lda, u32 ndu);
extern void ra_forecast(lda_t lda, u32 ndu);
extern void ra_init();
extern bool ra_whole_hit(lda_t lda, u32 ndu);
extern bool ra_range_chk(lda_t lda, u32 ndu);
extern void ra_disable();
extern void ra_disable_time(u32 time);
extern void ra_unmap_data_in(u32 ofst);
extern void ra_nrm_data_in(u32 ofst, dtag_t dtag);
extern void ra_err_data_in(u32 ofst);
extern void ra_validate();
extern bool is_ra_dtag(u32 _dtag);
extern void ra_data_out_cmpl();
extern void ra_dump();
extern bool ra_suspend();
extern void ra_resume();
extern bool ra_seq_detect(lda_t lda, u32 ndu);
extern void ra_dump(void);
extern void ra_clearReadPend(void);
extern void ra_forcestartup(void);
