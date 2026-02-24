//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------
/*! \file dfi_init.h
 * @brief dfi module header
 *
 * \addtogroup utils
 * \defgroup dfi
 * \ingroup utils
 * @{
 */
#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "types.h"
#include "io.h"
#include "rainier_soc.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define VREF_SET_CA	0
#define VREF_SET_DQ	1

//Jerry 240220: ddr init move to loader, so normal code don't need to do again, should not occupy ATCM.
//#if defined(MPC)
//#define dfi_code	fast_code
//#else
#define dfi_code	ddr_code
//#endif

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef struct _pass_window_t_t{
	u8 start;
	u8 end;
} pass_window_t;


//=================For Record DRAM draining result start
typedef struct
{
    int pstr;
    int nstr;
}tPAD;//8B
BUILD_BUG_ON(sizeof(tPAD) != 8);

typedef struct
{
    u16 level_delay[5];
    u16 WRLVL_rsv;
}tWRLVL;//12B
BUILD_BUG_ON(sizeof(tWRLVL) != 12);

typedef struct
{
    u8 rdlat;
    u8 rcvlat;
    u16 RDLAT_rsv;
}tRDLAT;//4B
BUILD_BUG_ON(sizeof(tRDLAT) != 4);

typedef struct
{
    u8 vgen_range_final;
    u8 vgen_vsel_final;
    u8 worst_range_all;
    u8 phase0_pre_final[5];
    u8 phase1_pre_final[5];
    u8 phase0_range[5];
    u8 phase1_range[5];
    u8 phase0_start[5];
    u8 phase1_start[5];
    u8 phase0_end[5];
    u8 phase1_end[5];
    u8 RDEYE_rsv;
}tRDEYE;//44B
BUILD_BUG_ON(sizeof(tRDEYE) != 44);

typedef struct
{
    u8 vref_range:1;
    u8 vref_value:6;
    u8 rsv:1;
    u8 vref_norm;
    u8 range1_pass_start;
    u8 range1_fail_start;
    u8 range2_pass_start;
    u8 range2_fail_start;
    u16 WRVREF_rsv;
}tWRVREF;//8B
BUILD_BUG_ON(sizeof(tWRVREF) != 8);

typedef struct
{
    int ByteX_Eye_Size_Byte[5];
    int ByteX_offset[5];
    u8  Right_start[5];
    u8  Right_end[5];
    u8  Left_start[5];
    u8  Left_end[5];
}tWRDESKEW;//60B
BUILD_BUG_ON(sizeof(tWRDESKEW) != 60);

typedef struct
{
    u8 window_size[5];
    u8 is_push[5];
    u8 push_delay[5];
    u8 pull_delay[5];    
    u8 phase0_post_final[5];
    u8 phase1_post_final[5];
    u16 RDLVLDPE_rsv;
}tRDLVLDPE;//16B
BUILD_BUG_ON(sizeof(tRDLVLDPE) != 32);

typedef struct
{
    u8 strgt_tap_dly[5];
    u8 strgt_phase_dly[5];
    u8 rsv[6];
}tRDLVLGATE;//16B
BUILD_BUG_ON(sizeof(tRDLVLGATE) != 16);

typedef union
{
    u8  bArray[256];
    struct
    {
        tPAD        pad;//8
        tWRLVL      wrlvl;//12
        tRDLAT      rdlat;//4
        tRDEYE      rdeye;//44
        tWRVREF     wrvref;//8
        tWRDESKEW   wrdeskew;//60
        tRDLVLDPE   rdlvldpe;//32B 
        tRDLVLGATE  rdlvlgate;//16B
        u8          rsvd[72];
    }element;    
}tDRAM_Training_Result;
BUILD_BUG_ON(sizeof(tDRAM_Training_Result) != 256);

//=================For Record DRAM draining result end


//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/*!
 * @brief read misc register
 *
 * @param reg	register offset
 *
 * @return	current register value
 */
static inline u32 misc_readl(u32 reg)
{
	return readl((void *)(MISC_BASE + reg));
}

/*!
 * @brief write misc register
 *
 * @param data	data to be written
 * @param reg	register offset
 *
 * @return	not used
 */
static inline void misc_writel(u32 data, u32 reg)
{
	writel(data, (void *)(MISC_BASE + reg));
}
/*!
 * @brief api to read dfi register
 *
 * @param reg		register offset
 *
 * @return		value of register
 */
static inline u32 dfi_readl(int reg)
{
	return readl((void *) (DFI_BASE + reg));
}

/*!
 * @brief api to write dfi register
 *
 * @param data		data to be written
 * @param reg		register offset
 *
 * @return		value of register
 */
static void inline dfi_writel(u32 data, int reg)
{
	writel(data, (void *) (DFI_BASE + reg));
}

/*!
 * @brief api to read mc0 register
 *
 * @param reg		register offset
 *
 * @return		value of register
 */
static inline u32 mc0_readl(int reg)
{
	return readl((void *) (MC0_BASE + reg));
}

/*!
 * @brief api to write mc0 register
 *
 * @param data		data to be written
 * @param reg		register offset
 *
 * @return		value of register
 */
static void inline mc0_writel(u32 data, int reg)
{
	writel(data, (void *) (MC0_BASE + reg));
}

// dfi_init.c
void dfi_dram_size_related_init(u32 dram_capacity);
void dfi_dram_setting_verify(void);
void dfi_train_result_init(void);
void dfi_phy_init(void);
void mc_vref_en(bool en);
u16 mc_get_target_speed(void);
void mc_switch_latency(u16 target_speed);
void mc_ddr_init_req(void);
void mc_init_post(void);
void mc_init(void);

// dfi_dpe_seq.c
u8 dfi_dpe_verify_write(u8 type, u32 bytes, bool debug);
u8 dfi_dpe_verify_read(u8 type, u32 bytes, bool debug);
u8 dfi_dpe_verify_repeat(u32 repeat);
void dfi_dpe_copy(void *src, void *dst, u32 bytes);
void dfi_dpe_wr_repeat(u8 type, u32 repeat);
void dfi_dpe_rd_repeat(u8 type, u32 repeat);

// dfi_wr_dqdqs_dly_seq.c
int dfi_wr_dqdqs_dly_seq(u8 target_cs, u8 train_type, u8 debug);
void dfi_wr_dqdqs_dly_loop(u8 target_cs, u32 loop);

//dfi_pad_cal_seq.c
int dfi_pad_cal_seq(u8 debug);
void dfi_pad_cal_seq_loop(u32 loop);

//dfi_wrlvl_seq.c
int dfi_wrlvl_seq_hs(u8 target_cs, u8 *level_order, u8 debug);
int dfi_wrlvl_seq_m2(u8 target_cs, u8 debug);
void dfi_wrlvl_loop(u8 target_cs, u32 loop, u8 summary);
void dfi_wrlvl_seq_debug(u8 target_cs, bool en);
int dfi_wrlvl_seq_single(u8 target_cs, u8 target_byte);

//dfi_rdlvl_seq.c
int dfi_rdlvl_rdlat_seq(u8 target_cs, u8 rcvlat_offset, u8 debug);
int dfi_rdlvl_2d_seq(u8 target_cs, u8 quick_sweep, u8 eye_size, u8 debug);
int dfi_rdlvl_dpe_seq(u8 target_cs, u8 train_type,u8 *win_size, int *mod_delay, u8 debug);
void dfi_rdlvl_dpe_loop(u8 target_cs, u32 loop);
int dfi_rdlvl_gate_seq(u8 target_cs, u8 quick_sweep, u8 phase_repeat_cnt_max, u8 debug);
void dfi_rd_lat_man_loop(u8 target_cs, u32 loop);
int dfi_dll_update(void);

//dfi_vref_dq_seq.c
int dfi_vref_dq_seq(u8 target_cs, u8 debug);
void dfi_vgen_loop(u32 cnt);
int dfi_dev_vref_cfg(u8 target_cs, u8 range, u8 value);

//dfi_calvl_seq.c
int dfi_calvl_seq(u8 target_cs, u32 target_speed, u8 debug);
void dfi_switch_fsp1(u16 target_speed);

//dfi_wr_train_seq.c
int dfi_wr_train_seq(u8 target_cs, int vref_range_max, int vref_value_max, u8 debug);
void dfi_mc_vref_set(u8 ca_dq, u8 vref_range, u8 vref_value, u8 cs, u8 debug);

/*! @} */
