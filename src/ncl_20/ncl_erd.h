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
#ifndef _NCL_ERD_H_
#define _NCL_ERD_H_

#define STRONG_1 0x1C
#define STRONG_0 0x0C
#define MEDIUM_1 0x17
#define MEDIUM_0 0x07
#define WEAK_1 0x11
#define WEAK_0 0x01

extern struct finstr_format erd_fins_templ;
extern bool support_erd;

// Generic functions in ncl_erd.c
extern void ncl_erd_init(void);
extern void _ncl_cmd_sp_erd(struct ncl_cmd_t *ncl_cmd);
extern void ncl_cmd_sp_erd(struct ncl_cmd_t *ncl_cmd);
extern bool ncl_cmd_erd_add(struct ncl_cmd_t *ncl_cmd);
extern void ncl_cmd_sp_erd_cmpl(struct ncl_cmd_t *erd_cmd);

extern void erd_add_ncl_cmd(struct ncl_cmd_t* ncl_cmd);
extern void erd_process(void);
extern void erd_cw_decode_trigger(void);
extern void ncl_erd_cfg_llr_lut(void);

// Vendor specific functions in erd_xxx.c
extern bool erd_du(struct ncl_cmd_t* ncl_cmd, u32 idx, u32 loop);
extern void du_erd_exit(pda_t pda);

// If support vendor specific ERD, vendor specific code must provide this function
extern __attribute__((weak)) u32 erd_max_steps_vendor(void);
#endif
