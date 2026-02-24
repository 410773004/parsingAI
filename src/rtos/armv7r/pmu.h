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
#include "list.h"

enum sleep_mode_t {
	SLEEP_MODE = 0,		///< both MAC & PHY off
	SLEEP_MODE_2,		///< MAC on & PHY off
	SLEEP_MODE_3,		///< MAC off & PHY on
	SLEEP_MODE_4,		///< both MAC & PHY on
	SLEEP_MODE_PS4,		///< standby, MAC/PHY and SRAM all on
	SLEEP_MODE_PS3_L12, 	///< 3.5 Standby Mode (both MAC and PHY on, PHY in L1.2)
	// modes above are all in L1.2 state
	SLEEP_MODE_PS3_L1,	///< 3.6 Standby Mode (both MAC and PHY on, PHY in L1)

	SUSPEND_ABORT,		///< suspend abort,
	SUSPEND_INIT,		///< initialization
};

/*! XXX: Change order should be carefull */
enum pmu_suspend_cookie_t {
	SUSPEND_COOKIE_START = 0,
	SUSPEND_COOKIE_SYSTEM = SUSPEND_COOKIE_START,   ///< sysmeta etc.
	SUSPEND_COOKIE_CACHE,
	SUSPEND_COOKIE_FTL,
	SUSPEND_COOKIE_DDR,
	SUSPEND_COOKIE_NCB,
	SUSPEND_COOKIE_BM,
	SUSPEND_COOKIE_L2P,
	SUSPEND_COOKIE_CMD_PROC,
	SUSPEND_COOKIE_NVME,
	SUSPEND_COOKIE_PLATFORM,
	SUSPEND_COOKIE_END,
};

enum pmu_resume_cookie_t {
	RESUME_COOKIE_START = 0,
	RESUME_COOKIE_PLATFORM = RESUME_COOKIE_START,
	RESUME_COOKIE_BM,
	RESUME_COOKIE_L2P,
	RESUME_COOKIE_CMD_PROC,
	RESUME_COOKIE_NVME,
	RESUME_COOKIE_NCB,
	RESUME_COOKIE_DDR,
	RESUME_COOKIE_FTL,
	RESUME_COOKIE_CACHE,
	RESUME_SYSTEM,
	RESUME_COOKIE_END,
};

typedef struct _swap_page_t {
	void *loc;
	u32 size;
	union {
		pda_t pda;
		void *swap_buf;
	};
	union {
		struct {
			u32 in_nand : 1;
			u32 rsvd : 15;
			u32 offset : 16;
		} b;
		u32 all;
	} attr;
} swap_page_t;

typedef struct _swap_mem_t {
	void *loc;
	u32 size;
} swap_mem_t;


/*! Be caution: suspend/resume handlers should be always synchronous, and resume in case one fails */
typedef	bool (*pmu_suspend_handle_t)(enum sleep_mode_t mode);
typedef	void (*pmu_resume_handle_t)(enum sleep_mode_t mode);

//extern bool pmu_enter(enum sleep_mode_t mode);

extern bool pmu_register_handler(
	enum pmu_suspend_cookie_t suspend_cookie, pmu_suspend_handle_t suspend,
	enum pmu_resume_cookie_t resume_cookie,pmu_resume_handle_t resume);

/*!
 * @brief pmu module initialization, setup timer
 *
 * @return	not used
 */
extern void pmu_init(void);
extern void pmu_wakeup_timer_set(int);

extern void sys_sleep(enum sleep_mode_t mode);
extern void sys_sleep_cancel(void);

/*!
 * @brief switch if allow to enter L1 or not
 *
 * @param enable	true to allow
 *
 * @return		not used
 */
extern void l1_switch(bool enable);

/*!
 * @brief check if current hw status allow to enter PMU or not
 *
 * @return		return 0 if allow to enter PMU
 */
extern u32 pmu_hw_sts(enum sleep_mode_t);

/*!
 * @brief register pmu swap file
 *
 * @param loc		buffer location to be saved
 * @param size		buffer size
 *
 * @return		None
 */
extern void pmu_swap_file_register(void *loc, u32 size);

/*!
 * @brief update pmu swap page information
 *
 * @param idx		swap page index
 * @param loc		new buffer location
 * @param size		new buffer size
 *
 * @return		not used
 */
extern void pmu_swap_file_update(u8 idx, void *loc, u32 size);
