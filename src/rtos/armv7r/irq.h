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

/*! \file irq.h
 * @brief interrupt handler
 *
 * \addtogroup rtos
 * \defgroup irq
 * \ingroup rtos
 *
 * {@
 */

#pragma once
#include "vic_id.h"
extern u8 in_isr;
extern u8 irq_disabled;
extern u8 irq_int_done;
typedef void (*irq_handler_t)(void);
extern void pln_evt(void);

/*!
 * @brief register a poll_irq handler
 *
 * @param irqno		irq number
 * @param isr		pointer to isr handler
 *
 * @return		not used
 */
void poll_irq_register(vic_vid_t irqno, irq_handler_t handler);


/*!
 * @brief register a irq handler
 *
 * @param irqno		irq number
 * @param isr		pointer to isr handler
 *
 * @return		not used
 */
void irq_register(vic_vid_t irqno, irq_handler_t handler);

/*!
 * @brief register a system irq handle in IRQ mode
 *
 * @param sirq		system irq number
 * @param handler	pointer to system isr handler
 * @param poll		if this sirq should be handled in polling mode, set it true
 *
 * @return		not used
 */
void sirq_register(u32 sirq, irq_handler_t handler, bool poll);

/*!
 * @brief disable the irq
 *
 * @param sirq	sirq number
 *
 * @return	not used
 */
void misc_sys_isr_disable(u32 sirq);

/*!
 * @brief enable the sys irq
 *
 * @param sirq	sys irq number
 *
 * @return	not used
 */
void misc_sys_isr_enable(u32 sirq);

/*!
 * @brief One-time initialization for Non-Polling Interrupt
 *
 * @param     not used
 *
 * @return    not used
 */
void HalIrq_InitNonPollingInterrupt(void);

/*!
 * @brief enable irq interrupt of CPU
 *
 * @return	not used
 */
static inline void irq_enable(void)
{
	__asm__ __volatile__ ("cpsie if");
}

/*!
 * @brief disable irq interrupt of CPU
 *
 * @return	not used
 */
static inline void irq_disable(void)
{
	__asm__ __volatile__ ("cpsid if");
}

/*!
 * @brief save CPU irq status
 *
 * @return	return saved cpu irq status
 */
static inline u32 irq_save(void)
{
	u32 flags;

	__asm__ volatile (" mrs     %0, cpsr\n\t"
			" cpsid   i"
			: "=r" (flags)
			:
			: "memory", "cc");
	return flags;
}

/*!
 * @brief restore CPU irq status
 *
 * @param flags		irq status to be restored
 *
 * @return		not used
 */
static inline void irq_restore(u32 flags)
{
	__asm__ volatile (" msr     cpsr_c, %0"
			:
			: "r" (flags)
			: "memory", "cc");
}

/*!
 * @brief clear all interrupts enable bit and all interrupts status
 *
 * @param not used
 *
 * @return not used
 */
extern void pic_clear_all(void);

/// Critical section for single CPU access region
#define BEGIN_CS1                   { u32 _org_state_; _org_state_=irq_save();
#define END_CS1                       irq_restore(_org_state_); }

#define DIS_ISR()  do{ if(irq_int_done && (!irq_disabled) && (!in_isr)){\
    irq_disable(); irq_disabled = true;}\
    }while(0)
#define EN_ISR()  do{ if(irq_int_done && (irq_disabled) && (!in_isr)){\
    irq_enable(); irq_disabled = false;}\
    }while(0)
/*! @} */
