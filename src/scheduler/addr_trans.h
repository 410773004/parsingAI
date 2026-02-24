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

/*! \file addr_trans.h
 * @brief scheduler module, translate between pda, fda, cpda and l2pfda
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#pragma once
#include "l2p_mgr.h"

/*!
 * @brief translate cpda to pda
 *
 * @param cpda		cpda to be translated
 *
 * @return		translated pda
 */
extern pda_t _cpda2pda(pda_t cpda);

/*!
 * @brief translate pda to spda
 *
 * @param pda		pda to be translated
 *
 * @return		translated cpda
 */
extern pda_t _pda2cpda(pda_t pda);

/*!
 * @brief translate cpda to fda
 *
 * @param cpda		cpda to be translated
 *
 * @return		translated fda
 */
extern fda_t _cpda2fda(u32 cpda);

/*!
 * @brief translate pda to fda
 *
 * @param pda		pda to be translated
 *
 * @return		translated fda
 */
extern fda_t _pda2fda(u32 pda);

/*!
 * @brief translate fda to cpda
 *
 * @param fda		fda to be translated
 *
 * @return		translated cpda
 */
extern pda_t _fda2cpda(fda_t fda);

/*!
 * @brief translate fda to pda
 *
 * @param fda		fda to be translated
 *
 * @return		translated pda
 */
extern pda_t _fda2pda(fda_t fda);

/*!
 * @brief translate pda to l2pfda
 *
 * @param pda		pda to be translated
 *
 * @return		translated l2pfda
 */
extern l2p_fda_t pda2l2pfda(u32 pda);
