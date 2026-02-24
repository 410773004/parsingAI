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

#include "stdio.h"
#include "evt_trace_log.h"

/*!
 * \file assert.h
 * @brief define assert macro to stop firmware
 *
 * \addtogroup system
 * @{
 */
 #if ASSERT_LVL <= ASSERT_LVL_CUST
#define sys_assert_CUST(x) /*lint -e{506} */ do {                          \
    if (!(x)) {                                                            \
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);   \
        _panic(#x);                                                        \
    }                                                                      \
} while (0)
#else
#define sys_assert_CUST(x) /*lint -e{506} */ do {                          \
    if (!(x)) {                                                            \
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);   \
        _panic(#x);                                                        \
    }                                                                      \
} while (0)

#endif

#if ASSERT_LVL <= ASSERT_LVL_DQA
#define sys_assert_DQA(x) /*lint -e{506} */ do {                           \
    if (!(x)) {                                                            \
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);   \
        _panic(#x);                                                        \
    }                                                                      \
} while (0)
#else
#define sys_assert_DQA(x) /*lint -e{506} */ do {                           \
    if (!(x)) {                                                            \
        //do something else
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);   \
        flush_to_nand(EVT_PANIC);                                          \
    }                                                                      \
} while (0)

#endif

#if ASSERT_LVL <= ASSERT_LVL_RD
#define sys_assert_RD(x) /*lint -e{506} */ do {                             \
    if (!(x)) {                                                             \
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);    \
        _panic(#x);                                                         \
    }                                                                       \
} while (0)
#else
#define sys_assert_RD(x) /*lint -e{506} */ do {                             \
    if (!(x)) {                                                             \
        //do something else
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);    \
        flush_to_nand(EVT_PANIC);                                           \
    }                                                                       \
} while (0)

#endif


/*! @brief assert macro to stop firmware */
#define sys_assert(x) /*lint -e{506} */ do {                                \
    if (!(x)) {                                                             \
        update_error_log_cnt_and_show_func_line(__FUNCTION__, __LINE__);    \
        _panic(#x);                                                         \
    }                                                                       \
} while (0)
/*! @} */
