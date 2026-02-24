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

#ifndef _POOL_H_
#define _POOL_H_

#include "sect.h"
/*!
 * \file pool.h
 * @brief define pool data structure
 *
 * Pool could improve allocate/free performance by reusing objects from a fixed
 * pool instead of allocate/free from heap.
 *
 * \addtogroup pool
 * @{
 *
 */

/*! @brief type for each entry in pool */
typedef struct pool_entry {
	struct pool_entry *next;	///< single linked list for each pool entry
} pool_entry_t;

/*! @brief pool object */
typedef struct pool {
	char *base;           ///< pointer to base memory
	int size;             ///< size of each pool entry
	pool_entry_t *free;   ///< pointer to first free entry
#ifndef NDEBUG
	int esize;
#endif
} pool_t;

/*!
 * @brief pool initialization
 *
 * This function must call from task context.
 *
 * @param pl		pointer to pool object
 * @param base		pointer to memory for pool entry
 * @param size		size of total size
 * @param entry_size	size of pool entry
 * @param entry_num	how may entries in pool
 *
 * @return		None
 */
void pool_init(pool_t *pl, char *base, u32 size, int entry_size,
		int entry_num);

/*!
 * @brief get one entry from pool
 *
 * This function must call from task context.
 *
 * @param pl	pointer to pool object
 * @param wait	wait for entry
 *
 * @return 	return entry to caller, or return NULL when no free space.
 */
void *pool_get(pool_t *pl, int wait);

/*!
 * @brief put one entry back to pool
 *
 * This function can call from task or irq context.
 *
 * @param pl	pointer to pool object
 * @param p	pointer to entry need be free
 *
 * @return	None
 */
void pool_put(pool_t *pl, void *p);

static inline fast_code void *pool_get_ex(pool_t *pl)
{
	pool_entry_t *e = pl->free;

	if (e != NULL)
		pl->free = e->next;

	return e;
}

static inline fast_code void pool_put_ex(pool_t *pl, void *p)
{
	((pool_entry_t *)p)->next = pl->free;
	pl->free = p;
}

/*! @} */
#endif
