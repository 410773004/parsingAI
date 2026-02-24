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

//=============================================================================
//
/*! \file system_log.h
 * @brief define system log data structure and method
 *
 * \addtogroup system_log
 * @{
 *
 * System log is used to save critical system information of disk. Each module
 * could define some log by itself, and register system log descriptor to
 * system log. System log has flush interface to flush data.
 *
 */
#pragma once

#include "queue.h"

#define SYSTEM_LOG_SIG          'syS'  //  'LsyS'  AlanCC for wunc ///< system log signature 

struct _sys_log_desc_t;

/*! @brief system log default initialization function type */
typedef void (*sld_default_t)(struct _sys_log_desc_t*);

/*!
 * @brief definition of system log descriptor
 *
 * If you would like to use system log to save data, you must register a
 * descriptor for your data.
 */
typedef struct _sys_log_desc_t {
	QSIMPLEQ_ENTRY(_sys_log_desc_t) link;	///< list entry

	void *ptr;		///< pointer for this descriptor
	u32 element_cnt;	///< how many element in this descriptor

	u16 element_size;	///< size of each elements in this descriptor
	u8 log_page_cnt;	///< system log page count
	u8 log_page_idx;	///< system log page start

	u32 total_size;		///< total size of descriptor
	sld_default_t init_default;	///< callback function to initialize content of this descriptor
} sys_log_desc_t;

struct _sld_flush_t;

/*! @brief system log descriptor flush completion function type */
typedef void (*sld_flush_cmpl_t)(struct _sld_flush_t*);

/*! @brief system log descriptor flush context */
typedef struct _sld_flush_t {
	void *caller;			///< caller
	completion_t caller_cmpl;	///< callback function of caller when flush completion
	sld_flush_cmpl_t cmpl;		///< callback function when flush completion
	QSIMPLEQ_ENTRY(_sld_flush_t) link;	///< list entry
} sld_flush_t;

/*!
 * @brief Initialize system log to accept system log descriptor registration
 *
 * @return	none
 */
void sys_log_init(void);


/*!
 * @brief Register system log descriptor to system log
 *
 * Callback function for initialization can be NULL(set 0xFF automatically)
 *
 * @param desc		system log descriptor
 * @param ele_cnt	element count in descriptor
 * @param ele_size	element size, ele_cnt * ele_size = total size
 * @param init_default	callback function to initialize descriptor to default value
 *
 * @return	not used
 */
void sys_log_desc_register(sys_log_desc_t *desc, u32 ele_cnt, u32 ele_size,
	sld_default_t init_default);

/*!
 * @brief Start system log, always reconstruct system log by scanning system log SPBs
 *
 * This function will allocate buffer for system log according registered
 * descriptors and start to scan system log SPBs to reconstruct it.
 * If return value is FTL_ERR_VIRGIN, system log SPB should be both INV_SPB_ID,
 * and other return value is error
 *
 * @param pblk	system log SPBs, should be saved in FTL root block
 *
 * @return	return FTL_ERR_OK if reconstructed successfully,
 */
ftl_err_t sys_log_start(pblk_t pblk[2]);

/*!
 * @brief Interface to flush a system log descriptor
 *
 * If flush caller need to do something when descriptor was flushed, flush
 * context can't be NULL.
 *
 * @param desc	pointer to system log descriptor to be flushed
 * @param ele	target element in system log descriptor to be flushed, ~0 for all
 * @param ctx	callback context for completion, can be NULL
 *
 * @return	none
 */
void sys_log_desc_flush(sys_log_desc_t *desc, u32 ele, sld_flush_t *ctx);

/*!
 * @brief Enable system log cache flush
 *
 * Support nested cache on.
 *
 * @return	none
 */
void sys_log_cache_on(void);

/*!
 * @brief Disable system log cache flush
 *
 * If cache is disable successfully, start to flush all cached system log pages.
 *
 * @return	none
 */
void sys_log_cache_off(void);

/*!
 * @brief Queue a system log flush context to wait for system log flush done
 *
 * @param flush		system log flush context
 *
 * @return		none
 */
void sys_log_flush_wait(sld_flush_t *flush);

/*!
 * @brief Check if system log was flushing
 *
 * @return		return true if system log was flushing
 */
bool is_sys_log_flushing(void);

/*!
 * @brief System log dump function
 */
void sys_log_dump(void);

/*!
 * @brief Interface to get data pointer of given descriptor.
 *
 * @param desc	pointer of system log descriptor
 *
 * @return	memory buffer os system log descriptor
 */
static inline void *sys_log_desc_get_ptr(sys_log_desc_t *desc)
{
	return desc->ptr;
}

/*!
 * @brief Check if system log free page count was enough for reconstruction critical gc
 *
 * @return		none
 */
void sys_log_critical_chk(void);

/*!
 * @brief resume system log after pmu enter
 *
 * @return		none
 */
void sys_log_resume(void);

/*! @} */
