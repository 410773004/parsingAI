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

#ifndef _PIC_H_
#define _PIC_H_

#include "types.h"
#include "vic_id.h"
#include "io.h"
#include "rainier_soc.h"

u32 pic_active_irq(void);
void pic_ack_irq(u32 irq);

extern u32 _poll_event_mask;

/*!
 * @brief enable the irq
 *
 * @param irq   irq number
 *
 * @return      None
 */
void pic_mask(vic_vid_t irq);

/*!
 * @brief disable the irq
 *
 * @param irq   irq number
 *
 * @return      None
 */
void pic_unmask(u32 irq);

/*!
 * @brief enable the task event
 *
 * @param irq   irq number
 *
 * @return      None
 */
void poll_mask(vic_vid_t irq);

/*!
 * @brief disable the task event
 *
 * @param irq   irq number
 *
 * @return      None
 */
void poll_unmask(u32 irq);

void pic_poll(void);
void pic_init(void);

static inline void vic_writel(u32 data, u32 reg)
{
	writel(data, (void *)(VIC_BASE + reg));
}

static inline u32 vic_readl(u32 reg)
{
	return readl((void *)(VIC_BASE + reg));
}
#endif
