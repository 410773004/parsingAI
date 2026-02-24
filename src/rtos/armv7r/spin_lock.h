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
/*! \file spin_lock.c
 * @brief implementation of spin_lock
 *
 * \addtogroup rtos
 * \defgroup spin_lock
 * \ingroup rtos
 *
 * {@
 */
#pragma once

enum {
	SPIN_LOCK_KEY_DDR = 0,
	SPIN_LOCK_KEY_IDX_META,
	SPIN_LOCK_KEY_SHARE_TCM,
	SPIN_LOCK_KEY_L2C_OP,
	SPIN_LOCK_KEY_FTL_STAT,
	SPIN_LOCK_KEY_EVLOG,
	SPIN_LOCK_KEY_SYNC_DPE,
	SPIN_LOCK_KEY_DDR_WINDOWS,
	SPIN_LOCK_PR_ACCESS,
	SPIN_LOCK_TS_VAR,
	SPIN_LOCK_KEY_CACHE,
	SPIN_LOCK_KEY_VCNT,
	SPIN_LOCK_KEY_JOURNAL,
	SPIN_LOCK_KEY_VC_RECON,
};

/*!
 * @brief take spin lock resource
 *
 * @param splk_idx the index(0 - 63) of the spin lock registers
 * @param val CPU write data
 * @param wait Whether the CPU is willing to wait until the resource is obtained
 *
 * @param true means CPU get what he want
 */
bool spin_lock_take(u32 splk_idx, u32 val, bool wait);

/*!
 * @brief release spin lock resource via normal key
 *
 * @param splk_idx the index(0 - 63) of the spin lock registers
 *
 * @return true means release it successfully
 */
bool spin_lock_release(u32 splk_idx);

/*!
 * @brief initialize spin lock resource via super key
 *
 * @return 	not used
 */
void spin_lock_init(void);

/*! @} */
