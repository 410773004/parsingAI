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

/*!
 * \file bg_task.h
 * @brief define background idle task api
 *
 * \addtogroup ftl
 * @{
 */

/*!
 * @brief initialization of background task
 */
void bg_task_init(void);

/*!
 * @brief reset background task
 */
void bg_task_resume(void);

/*! @} */

