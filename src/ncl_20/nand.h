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
#ifndef _NAND_H_
#define _NAND_H_

#include "ncl.h"

#include "ndcmd_fmt.h"

/*!
 * @brief Get nand user page size(NOT include spare area)
 *
 * @return value(user page size)
 */
u16 nand_page_user_size(void);
/*!
 * @brief Get nand whole page size(include spare area)
 *
 * @return value(whole page size)
 */
u16 nand_whole_page_size(void);
/*!
 * @brief Get nand page spare area size
 *
 * @return value(page spare area size)
 */
u32 nand_spare_size(void);
/*!
 * @brief Get nand interleave number
 *
 * @return value(interleave number)
 */
u32 nand_interleave_num(void);
/*!
 * @brief Get nand channel number
 *
 * @return value(channel number)
 */
u8 nand_channel_num(void);
/*!
 * @brief Get nand target(ce) number
 *
 * @return value(target(ce) number)
 */
u8 nand_target_num(void);
/*!
 * @brief Get nand lun number
 *
 * @return value(lun number)
 */
u8 nand_lun_num(void);
/*!
 * @brief Get nand plane number
 *
 * @return value(plane number)
 */
u8 nand_plane_num(void);
/*!
 * @brief Get nand block number
 *
 * @return value(block number)
 */
u32 nand_block_num(void);
/*!
 * @brief Get nand page number
 *
 * @return value(page number)
 */
u32 nand_page_num(void);
/*!
 * @brief Get nand SLC page number
 *
 * @return value(SLC page number)
 */
u32 nand_page_num_slc(void);
/*!
 * @brief Get nand bit per cell type
 *
 * @return value(bit per cell type)
 */
u32 nand_bits_per_cell(void);
/*!
 * @brief Get nand PDA block shift
 *
 * @return value(PDA block shift)
 */
u32 nand_pda_block_shift(void);
/*!
 * @brief Get nand row address lun shift number
 *
 * @return value(lun shift number)
 */
u8 nand_row_lun_shift(void);
/*!
 * @brief Get nand row address block shift number
 *
 * @return value(block shift number)
 */
u8 nand_row_blk_shift(void);
/*!
 * @brief Get nand row address plane shift number
 *
 * @return value(plane shift number)
 */
u8 nand_row_plane_shift(void);
/*!
 * @brief Get nand row address page shift number
 *
 * @return value(page shift number)
 */
u8 nand_row_page_shift(void);

extern u32 pda2ActDiePlane_Row(pda_t pda);

bool nand_data_training(void);

pda_t nal_rda_to_pda(rda_t *rda);
void nal_pda_to_rda(pda_t pda, enum nal_pb_type pb_type, rda_t *rda);


/*!
 * @brief Get the flash channel number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(channel value)
 */
u32 pda2ch(pda_t pda);
/*!
 * @brief Get the flash ce number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(ce value)
 */
u32 pda2ce(pda_t pda);
/*!
 * @brief Get the flash plane number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(plane value)
 */
u32 pda2plane(pda_t pda);
/*!
 * @brief Get the flash page number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(page value)
 */
u32 pda2page(pda_t pda);
/*!
 * @brief Get the flash du number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(page value)
 */
u32 pda2du(pda_t pda);
/*!
 * @brief Get the flash lun number in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(lun value)
 */
u32 pda2lun(pda_t pda);
/*!
 * @brief Get the CH, CE, LUN combined ID in PDA
 *
 * @param[in] pda		PDA
 *
 * @return value(combined ID)
 */
u32 pda2die(pda_t pda);
u32 pda2lun_id(pda_t pda);
/*!
 * @brief Get the flash row address in PDA
 *
 * @param[in] pda			PDA
 * @param[in] pb_type		PDA's PB type
 *
 * @return value(row address)
 */
u32 pda2row(pda_t pda, enum nal_pb_type pb_type);
/*!
 * @brief Get the nand column address in PDA
 *
 * @param[in] pda			PDA
 *
 * @return value(flash column address)
 */
u32 pda2column(pda_t pda);
/*!
 * @brief Get the nand page index in PDA
 *
 * @param[in] pda			PDA
 *
 * @return value(page index)
 */
u8 pda2pg_idx(pda_t pda);
/*!
 * @brief Get the row address
 *
 * @param[in] lun		flash lun
 * @param[in] pl		flash plane
 * @param[in] block		flash block
 * @param[in] page		flash page
 *
 * @return value(row address)
 */
u32 row_assemble(u32 lun, u32 pl, u32 block, u32 page);
/*!
 * @brief Get the PDA number
 *
 * @param[in] ch		flash ch
 * @param[in] ce		flash ce
 * @param[in] row		flash row addr
 * @param[in] col		flash col addr
 *
 * @return value(PDA)
 */
pda_t pda_assemble(u32 ch, u32 ce, u32 row, u32 col);
pda_t pda_assemble_xlc(u32 ch, u32 ce, u32 row, u32 col, u8 prefix);//_GENE_20201005

/*!
 * @brief Do the nand module initialization
 *
 * @return Not used
 */
void nand_init(void);
/*!
 * @brief Do the nand module reinitialization
 *
 * @return Not used
 */
void nand_reinit(void);
/*!
 * @brief Wait indirect mode done
 *
 * @return Not used
 */
void ndcu_wait_cmd_end(void);
/*!
 * @brief Wait flash ready
 *
 * @param[in] ch		flash channel
 * @param[in] ce		flash ce
 *
 * @return value(flash status)
 */
u8 nand_wait_ready(u8 ch, u8 ce);
u8 nand_wait_ready_78h(u8 ch, u8 ce , u8 lun);
/*!
 * @brief Reset all flash
 *
 * @return Not used
 */
void nand_reset_all(void);
void nand_reset_FD(u8 ch, u8 ce, u8 lun);


/*!
 * @brief Nand set feature configuration
 *
 * @return Not used
 */
void nand_set_timing(void);
bool nand_target_exist(u32 ch, u32 ce);
/*!
 * @brief Initialization nand information value
 *
 * @return Not used
 */
void nand_info_init(void);
extern void nand_read_page_parameter(void);
extern u32 get_feature(u8 ch, u8 ce, u8 fa);
extern u32 get_feature_D4h(u8 ch, u8 ce, u8 lun, u8 fa);
extern void set_feature(u8 ch, u8 ce, u8 fa, u32 val);
extern void set_feature_D5h(u8 ch, u8 ce, u8 lun, u8 fa, u32 val);
/*!
 * @brief Judge if nand support AIPR
 *
 * @return value(true or false)
 */
extern bool nand_support_aipr(void);
/*!
 * @brief Judge if nand is support 1.2v
 *
 * @return value(true or false)
 */
extern bool nand_is_1p2v(void);
/*!
 * @brief Get the nand speed MT
 *
 * @param[in] speed_grade		speed_grade value
 *
 * @return value(MT number)
 */
extern u16 nand_speed(u16 speed_grade);

/*!
 * @brief Nand switch NF clock
 *
 * @param freq	New frequency
 *
 * @return	not used
 */
void nand_clk_switch(u32 freq);

u32 nand_phy_ch(u32 log_ch);
u32 nand_log_ch(u32 log_ch);
u32 nand_phy_ce(u32 log_ch, u32 log_ce);
u32 nand_log_ce(u32 phy_ch, u32 phy_ce);
/*!
 * @brief Some nand command sequence in order to run high speed (e.g. ZQ calibration)
 *
 * @return	not used
 */
extern __attribute__((weak)) void nand_set_high_speed(void);

/*!
 * @brief Nand specific initialization different from other vendors or other models
 *
 * @return	not used
 */
extern __attribute__((weak)) void nand_specific_init(void);

/*!
 * @brief Nand vendor specific operation when switching NF clock
 *
 * @param old_freq	Old frequency
 * @param new_freq	New frequency
 *
 * @return	not used
 */
extern __attribute__((weak)) void nand_vendor_clk_switch(u32 old_freq, u32 new_freq);

/*!
 * @brief DCC training required by some nands
 *
 * @return	not used
 */
extern __attribute__((weak)) void nand_dcc_training(void);

#if HAVE_MICRON_SUPPORT
#include "nand_mu.h"
#endif
#if HAVE_UNIC_SUPPORT
#include "nand_unic.h"
#endif
#if HAVE_YMTC_SUPPORT
#include "nand_ymtc.h"
#endif

#if HAVE_TOGGLE_SUPPORT
#include "nand_toggle.h"
#endif
#endif
