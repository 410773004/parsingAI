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

#ifndef _ELF_SECTION_H_
#define _ELF_SECTION_H_

#include "types.h"

/*!
 * \file sect.h
 *
 */
#define SEC_EXT stringify(CPU_ID_0) "."__FILE__ \
                                    "." stringify(__LINE__)

#define fast_code __attribute__((__section__(".tcm_code." SEC_EXT)))
#define fast_data __attribute__((__section__(".tcm_data.nrm." SEC_EXT)))
#define fast_data_zi __attribute__((__section__(".tcm_data.zi." SEC_EXT)))
#define fast_data_ni __attribute__((__section__(".tcm_data.ni." SEC_EXT)))

#define ps_code __attribute__((__section__(".ps_code." SEC_EXT)))
#define ps_data __attribute__((__section__(".ps_data." SEC_EXT)))

#if CPU_ID <= 2 // ATCM is power safe
#define norm_ps_code fast_code
#define norm_ps_data fast_data
#else
#define norm_ps_code ps_code
#define norm_ps_data ps_data
#endif

#define ucmd_tbl __attribute__((__section__(".ucmd_tbl." SEC_EXT)))

#define slow_code __attribute__((__section__(".sram_code." SEC_EXT)))
#define slow_data __attribute__((__section__(".sram_data.nrm." SEC_EXT)))
#define slow_data_zi __attribute__((__section__(".sram_data.zi." SEC_EXT)))
#define slow_data_ni __attribute__((__section__(".sram_data.ni." SEC_EXT)))
#define slow_code_ex __attribute__((__section__(".sram_code_ex." SEC_EXT)))

#define cmf_enc_data __attribute__((__section__(".cmf_enc_data." SEC_EXT)))
#define cmf_dec_data __attribute__((__section__(".cmf_dec_data." SEC_EXT)))
#define seed_lut_data __attribute__((__section__(".seed_lut_data." SEC_EXT)))

#define init_code __attribute__((__section__(".init_code." SEC_EXT)))
#define init_data __attribute__((__section__(".init_data." SEC_EXT)))

#define cold_code __attribute__((__section__(".cold_code." SEC_EXT)))
/*#define cold_data __attribute__((__section__(".cold_data")))*/

#if defined(MPC)

#define ddr_code __attribute__((__section__(".ddr_code." SEC_EXT)))
#define ddr_data __attribute__((__section__(".ddr_data.nrm." SEC_EXT)))
// NOT need ddr_data_zi because DRAM will be scrubbed in loader (clear 0 already)
#define ddr_data_ni __attribute__((__section__(".ddr_data.ni." SEC_EXT)))

#if CPU_ID == 1
#define share_code __attribute__((__section__(".tcm_sh_code." SEC_EXT)))
#define share_data __attribute__((__section__(".tcm_sh_data.nrm." SEC_EXT)))
#define share_data_zi __attribute__((__section__(".tcm_sh_data.zi." SEC_EXT)))
#define share_data_ni __attribute__((__section__(".tcm_sh_data.ni." SEC_EXT)))
#define ddr_sh_code __attribute__((__section__(".ddr_sh_code." SEC_EXT)))
#define ddr_sh_data __attribute__((__section__(".ddr_sh_data." SEC_EXT)))
#define sram_sh_code __attribute__((__section__(".sram_sh_code." SEC_EXT)))
#define sram_sh_data __attribute__((__section__(".sram_sh_data." SEC_EXT)))
#else
#define share_code extern
#define share_data extern
#define share_data_zi extern
#define share_data_ni extern
#define ddr_sh_code extern
#define ddr_sh_data extern
#define sram_sh_code extern
#define sram_sh_data extern
#endif
#else
#define sram_sh_code __attribute__((__section__(".sram_code." SEC_EXT)))
#define sram_sh_data __attribute__((__section__(".sram_data." SEC_EXT)))
#define share_code fast_data
#define share_data fast_data
#define share_data_ni fast_data_ni
#define share_data_zi fast_data_zi
#endif

extern void *__atcm_free_start;
extern void *__atcm_free_end;

extern void *__btcm_free_start;
extern void *__btcm_free_end;

extern void *__btcm_data_ni_start;
extern void *__btcm_data_ni_end;

extern void *__sh_btcm_free_start;
extern void *__sh_ddr_free_start;

extern void *__sram_free_start;
extern void *__sram_free_end;

extern void *_end_bss;

#if defined(MPC)
extern void *__btcm_data_end;

extern void *__sysinit_start_0;
extern void *__sysinit_end_0;

extern void *__sysinit_start_1;
extern void *__sysinit_end_1;

extern void *__sysinit_start_2;
extern void *__sysinit_end_2;

extern void *__sysinit_start_3;
extern void *__sysinit_end_3;

extern void *__init_start_0;
extern void *__init_end_0;

extern void *__init_start_1;
extern void *__init_end_1;

extern void *__init_start_2;
extern void *__init_end_2;

extern void *__init_start_3;
extern void *__init_end_3;

extern void *__rsvd_dtag_section_start_0;
extern void *__rsvd_dtag_section_end_0;

extern void *__rsvd_dtag_section_start_1;
extern void *__rsvd_dtag_section_end_1;

extern void *__rsvd_dtag_section_start_2;
extern void *__rsvd_dtag_section_end_2;

extern void *__rsvd_dtag_section_start_3;
extern void *__rsvd_dtag_section_end_3;

extern void *__evlog_dtag_start;
extern void *__evlog_dtag_end;

extern void *__evlog_flush_buf_start;
extern void *__evlog_flush_buf_end;

extern void *__evlog_agt_buf_start;
extern void *__evlog_agt_buf_end;

extern void *__evlog_log_tag_start;
extern void *__evlog_log_tag_end;

extern void *__his_tab_start;
extern void *__his_tab_end;

#if defined(MPC)
extern void *__dtag_stream_read_start;
extern void *__dtag_stream_read_end;
#endif
#if defined(RDISK) && defined(SEMI_WRITE_ENABLE)
extern void *__dtag_stream_write_start;
extern void *__dtag_stream_write_end;
extern void *__dtag_stream_write_ex_start;
extern void *__dtag_stream_write_ex_end;
#endif
#else
extern void *__sysinit_start;
extern void *__sysinit_end;

extern void *__init_start;
extern void *__init_end;
#endif

extern void *__ps_free_start;
extern void *__ps_free_end;

extern void *__dtag_mem_start;
extern void *__dtag_mem_end;

extern void *__cmf_data_start;
extern void *__cmf_lut_start;
extern void *__cmf_data_end;
extern void *__cmf_lut_data_end;

extern void *__ucmd_start;
extern void *__ucmd_end;

extern void *__dtag_fwconfig_start;
extern void *__dtag_fwconfig_end;

//#define cpu_id_concat(x, y)	#x"_"stringify(y)

#if defined(MPC)

#if CPU_ID_0 == 0
#define ___sysinit_start __sysinit_start_0
#define ___sysinit_end __sysinit_end_0
#define ___rsvd_dtag_section_start __rsvd_dtag_section_start_0
#define ___rsvd_dtag_section_end __rsvd_dtag_section_end_0
#elif CPU_ID_0 == 1
#define ___sysinit_start __sysinit_start_1
#define ___sysinit_end __sysinit_end_1
#define ___rsvd_dtag_section_start __rsvd_dtag_section_start_1
#define ___rsvd_dtag_section_end __rsvd_dtag_section_end_1
#elif CPU_ID_0 == 2
#define ___sysinit_start __sysinit_start_2
#define ___sysinit_end __sysinit_end_2
#define ___rsvd_dtag_section_start __rsvd_dtag_section_start_2
#define ___rsvd_dtag_section_end __rsvd_dtag_section_end_2
#elif CPU_ID_0 == 3
#define ___sysinit_start __sysinit_start_3
#define ___sysinit_end __sysinit_end_3
#define ___rsvd_dtag_section_start __rsvd_dtag_section_start_3
#define ___rsvd_dtag_section_end __rsvd_dtag_section_end_3
#else
#error "CPU id error"
#endif
#else

#define ___sysinit_start __sysinit_start
#define ___sysinit_end __sysinit_end

#endif

#endif
