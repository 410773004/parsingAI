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

#include "inc/ncb_eccu_register.h"

#define TTL_RAID_ID	64
#define TTL_RAID_BUF	64
#define RAID_BUF_PER_BANK 4
#define RAID_BANK_NUM 8
#define RAID_BANK_SIZE 0x19800 /// < 102KB

#define MAX_WR_RAID_ID (RAID_BUF_PER_BANK * RAID_BANK_NUM - 1)	///< total raid id for wr, reserve one for raid correct
#define MAX_CWL_STRIPE (MAX_WR_RAID_ID / XLC)	///< least plane is 2, so max cwl stripe is TTL_RAID_ID / 2

enum buf_type {
	BUF_TYPE_SRAM = 0,
	BUF_TYPE_DDR,
};

typedef struct _buf_info_t {
	u32 bank;
	u32 buf_idx;
} buf_info_t;

struct partial_du_fmt_t {
	eccu_du_fmt_reg0_t reg0;
	eccu_du_fmt_reg1_t reg1;
};

typedef union {
    u32 all;
    struct {
        u32 raid_sel_raid_id:6;
        u32 raid_sel_3:2;
        u32 raid_sel_7:4;
        u32 raid_sel_rsvd:4;
        u32 raid_status_sram_buffer_id:3;
        u32 raid_status_raid_rsvd1:1;
        u32 raid_status_sram_bank_id:3;
        u32 raid_status_raid_rsvd2:1;
        u32 raid_status_ddr_buffer_id:7;
        u32 raid_status_raid_rsvd3:1;
    } b;
} raid_dbg_id_sel_t;

enum raid_cmd_t
{
	NO_RAID				= 0,
	INIT_BUF_W_ZERO		= 1,
	XOR_W_DATA			= 2,
	XOR_WO_DATA			= 3,
	POUT_W_DATA			= 4,
	POUT_W_DATA_W_STS	= 5,
	POUT_WO_DATA_W_STS	= 6,
	RAID_ID_N_BUF_RLS		= 7,
	POUT_W_DATA_WO_STS_WO_BUF_RLS	= 8,
	POUT_WO_DATA_WO_STS_WO_BUF_RLS	= 9,
	SRAM_BUF_DATA_TO_DDR			= 0xA,
	DDR_BUF_DATA_TO_SRAM			= 0xB,
	DDR_BUF_DATA_TO_SRAM_W_XOR	= 0xC,
};

typedef struct raid_cmpl_entry_t {
	u32 raid_id	: 6;	///< raid id to report cmpl
	u32 rsvd1	: 2;
	u32 raid_cmd	: 5;	///< raid cmd to report cmpl
	u32 rsvd2	: 3;
	u32 internal_du_num	: 13;	///< operated du number of the raid cmd(xor, pout etc.)
	u32 rsvd3	: 3;
} raid_cmpl_entry_t;

extern u8 evt_xor_cmpl; ///< export to ftl raid
extern u8 stripe_lut[TTL_RAID_ID];	///< map table of raid id to stripe id
extern u8 stripe_left_cmpl[MAX_CWL_STRIPE];	///< expected cmpl cnt of each stripe

/*!
 * @brief  ncl raid configuration init
 *
 * @return	not used
 */
void ncl_raid_init(void);

/*!
 * @brief  get RAID cmd according ncl_cmd flags
 *
 * @param ncl_cmd	ncl command which include RAID operation
 *
 * @return	raid cmd
 */
u8 get_raid_cmd(struct ncl_cmd_t *ncl_cmd);

/*!
 * @brief  get raid_id's raid buf bank and index
 *
 * @param raid_id	raid id
 *
 * @return	raid buffer info: bank & index
 */
buf_info_t get_raid_buf_info(u32 raid_id);

/*!
 * @brief  get raid buffer address
 *
 * @param buf_type	SRAM or DDR
 * @param bank	raid buffer bank
 * @param buf_idx	index in bank
 * @param du_idx	du index in raid page buf
 *
 * @return	address of the raid buffer
 */
void *get_raid_buf_du_addr(u8 buf_type, u8 bank, u8 buf_idx, u8 du_idx);

/*!
 * @brief  set raid id xor cmpl paramters
 *
 * @param raid_id	raid id
 * @param du_cnt	du count to be xor of this raid id
 * @param add	add the du cnt to internal du cnt
 * @param last	the final du cnt of this raid id
 *
 * @return	not used
 */
void ncl_raid_set_cmpl_para(u32 raid_id, u32 du_cnt, bool add, bool last);

/*!
 * @brief  reset the ctx of raid id in raid_cmpl_ctrl
 *
 * @param raid_id	raid id
 *
 * @return	not used
 */
void ncl_raid_id_reset(u32 raid_id);

/*!
 * @brief  clear raid id last flag
 *
 * @param raid_id	raid id
 *
 * @return	not used
 */
void ncl_raid_clr_last_flag(u32 raid_id);

/*!
 * @brief  direct set raid id internal xored cnt
 *
 * @param raid_id	raid id
 * @param du_cnt	du count to be xor of this raid id
 *
 * @return	not used
 */
void ncl_raid_set_intl_cnt(u32 raid_id, u32 du_cnt);

/*!
 * @brief  set stripe id of raid id
 *
 * @param raid_id	raid id
 * @param stripe_id	stripe id
 *
 * @return	not used
 */
static inline void ncl_raid_set_stripe_lut(u32 raid_id, u32 stripe_id)
{
	stripe_lut[raid_id]	 = stripe_id;
}

/*!
 * @brief  set expected cmpl cnt of stripe id
 *
 * @param stripe_id	stripe id
 * @param cmpl_cnt	expected cmpl cnt
 *
 * @return	not used
 */
static inline void ncl_raid_set_stripe_cmpl_cnt(u32 stripe_id, u32 cmpl_cnt)
{
	stripe_left_cmpl[stripe_id] = cmpl_cnt;
}

/*!
 * @brief  backup raid engine status
 *
 * @return	not used
 */
void ncl_raid_status_save(void);

/*!
 * @brief  restore raid engine status
 *
 * @return	not used
 */
void ncl_raid_status_restore(void);
void *get_raid_sram_buff_addr(u8 raid_id);


