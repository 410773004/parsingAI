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

#include "nand_cfg.h"

#define NO_DEV_SIGNAL		0xFF		///< Nand read value when nand not exist

#define NDPHY_ODT0_150OHM	0
#define NDPHY_ODT1_100OHM	1
#define NDPHY_ODT3_75OHM	3
#define NDPHY_ODT5_60OHM	5
#define NDPHY_ODT7_50OHM	7

#define NDPHY_DRVSTR_0_TRISTATE	0
#define NDPHY_DRVSTR_1_160OHM	1
#define NDPHY_DRVSTR_2_100OHM	2
#define NDPHY_DRVSTR_4_50OHM	4
#define NDPHY_DRVSTR_6_35OHM	6
#define NDPHY_DRVSTR_12_25OHM	12
#define NDPHY_DRVSTR_14_20OHM	14
#define NDPHY_DRVSTR_15_18OHM	15
/*!
 * @brief Set Nand PHY RE to DQS delay
 *
 * @param val_sync	Value of sync (toggle or ONFI DDR) mode
 * @param val_async	Value of async mode
 *
 * @return	not used
 */
void ndphy_set_re2qs(u32 val_sync, u32 val_async);

/*!
 * @brief Set Nand PHY interface timing mode
 *
 * @param ch	Channel
 * @param intf	Interface type
 *
 * @return	not used
 */
void ndphy_set_tm(u32 ch, ndcu_if_mode intf);

/*!
 * @brief Nand PHY module reset
 *
 * @return	not used
 */
void ndphy_hw_reset(void);

/*!
 * @brief Nand PHY module initialization
 *
 * @return	not used
 */
void ndphy_hw_init(void);

/*!
 * @brief Set Nand PHY DLL phase
 *
 * @param ch	Channel
 * @param value	DLL phase
 *
 * @return	not used
 */
void ndphy_set_dll_phase(u8 ch, u8 value);
void ndphy_set_dll_phase_enhance(u8 ch, u8 ce, u8 lun, u8 value);
/*!
 * @brief Nand PHY DLL phase lock
 *
 * @param ch	Channel
 *
 * @return	not used
 */
void ndphy_init_per_ch_dll_lock(int ch);

/*!
 * @brief Nand PHY differential mode enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
void ndphy_set_differential_mode(bool en);

/*!
 * @brief Nand PHY ODT enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
void ndphy_enable_odt(bool en);
void ndphy_set_re2qs_gate_dly(u32 delay);
void ndphy_set_qs_gate_dis(u8 dis);
void ndphy_set_re2qs_dly(u32 sync_val);


/*!
 * @brief Nand PHY lock after timing mode switched
 *
 * @return	not used
 */
void ndphy_locking_mode(void);

/*!
 * @brief Nand PHY drive strength configure
 *
 * @param ch	Channel
 * @param level	Drive strength level
 *
 * @return	not used
 */
void ndphy_drv_str_level(u32 ch, u32 level);

/*!
 * @brief Nand PHY ODT configure
 *
 * @param ch	Channel
 * @param level	ODT level
 *
 * @return	not used
 */
void ndphy_odt_level(u32 ch, u32 level);
/*!
 * @brief Nand PHY reset
 *
 * @return	not used
 */
void ndphy_reset(void);

void ndphy_io_strobe_enable(bool enable);