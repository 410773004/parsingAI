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

#include "sect.h"
#include "pic.h"
#include "vic_register.h"

fast_data u32 _poll_event_mask = 0;

fast_code u32 pic_active_irq(void)
{
	return vic_readl(IRQ_STATUS);
}

fast_code void pic_ack_irq(u32 irq)
{
	vic_writel((1<<irq), VECTOR_ADDRESS);
}

fast_code void pic_mask(vic_vid_t irq)
{
	vic_writel((u32) (1 << irq), INTERRUPT_ENABLE);
}

fast_code void pic_unmask(u32 irq)
{
	vic_writel(1<<irq, INTERRUPT_ENABLE_CLEAR);
}

fast_code void pic_clear_all(void)
{
	vic_writel(~0, INTERRUPT_ENABLE_CLEAR);
	vic_writel(~0, VECTOR_ADDRESS);
}

fast_code void poll_mask(vic_vid_t irq)
{
	_poll_event_mask |= ((u32)1 << irq);
}

fast_code void poll_unmask(u32 irq)
{
	_poll_event_mask &= ~((u32)1 << irq);
}

fast_code void pic_poll(void)
{
	if (pic_active_irq()) {
		extern void do_irq(void);
		do_irq();
	}
}

void ps_code pic_init(void)
{
	/* keep interupts off */
	vic_writel(0xFFFFFFFF, INTERRUPT_ENABLE_CLEAR);

	int i, j = 0;

	for (i = 0; i < 16; i ++, j ++) {
		vic_writel(j, VECTOR_ADDRESS_0+(i<<2));
	}
	for (i = 0; i < 16; i ++, j ++) {
		vic_writel(j, GROUP_A_VECTOR_ADDRESS_0+(i<<2));
	}
	vic_writel(0x0101, PRIORITY_WEIGHTING_OF_GROUP);
	vic_writel(1<<1, CONTROL_AND_STATUS);
}
