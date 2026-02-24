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
 * \file bg_traning.h
 * @brief define background idle training api
 *
 * \addtogroup dispatcher
 * @{
 */

/*!
 * @brief initialization of background training
 */
void bg_training_init(void);

/*!
 * @brief enable bg
 */
void bg_enable(void);

/*!
 * @brief only stop bg
 */
bool bg_disable(void);

/*!
 * @brief stop bg and abort running task
 *
 * @return true if no running task
 */
void bg_disable_enhance(void);

