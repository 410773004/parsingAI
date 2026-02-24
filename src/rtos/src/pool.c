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

#include "types.h"
#include "pool.h"
#include "task.h"
#include "irq.h"
#include "stdio.h"

/*! \cond PRIVATE */
#define __FILEID__ pool
#include "trace.h"
/*! \endcond */

#define NDEBUG

#ifndef NDEBUG
static void pool_fill_free(pool_t *pl, pool_entry *e)
{
	u32 *data = (u32 *)(e + 1);
	int size = pl->esize - sizeof(pool_entry);
	u32 ck = (u32)e;

	ck = (ck & 0x00ffffff) | 0xa5000000;
	for (; size > 0; size -= 4, data++)
		*data = ck;
}

static void pool_check_free(pool_t *pl, pool_entry *e)
{
	u32 *data = (u32 *)(e + 1);
	int size = pl->esize - sizeof(pool_entry);
	u32 ck = (u32)e;

	ck = (ck & 0x00ffffff) | 0xa5000000;
	for (; size > 0; size -= 4, data++) {
		if (*data != ck)
			panic("pool check failed");

		*data = (ck & 0x00ffffff) | 0x5a000000;
	}
}
#endif

fast_code void pool_init(pool_t *pl, char *base, u32 size, int entry_size,
			 int entry_num)
{
	pl->base = base;
	pl->size = size;
	pl->esize = entry_size;

	pl->free = (pool_entry_t *)base;
	pool_entry_t *entry = pl->free;

	entry_num--;
	while (entry_num) {
		char *nmem = ((char *)entry) + entry_size;
		pool_entry_t *next = (pool_entry_t *) nmem;

#ifndef NDEBUG
		pool_fill_free(pl, entry);
#endif
		entry->next = next;
		entry = next;
		entry_num--;
	}
	entry->next = NULL;
#ifndef NDEBUG
	pool_fill_free(pl, entry);
#endif
}

fast_code void *pool_get(pool_t *pl, int wait)
{
	pool_entry_t *e;

	if (pl->free == NULL && wait == 0) {
		return NULL;
	}

	if (pl->free == NULL) {
		task_ev_block(pl);
	}

	u32 oe;

	u32 flags = irq_save();

	do {
		e = pl->free;
		oe = xchg(e->next, &pl->free);
	} while (oe != (u32) e);

#ifndef NDEBUG
	pool_check_free(pl, e);
#endif
	irq_restore(flags);

	return e;
}

fast_code void pool_put(pool_t *pl, void *p)
{
	char *q = (char *)p;

	if ((q < pl->base) || q > (pl->base + pl->size))
		panic("pool_put");

	u32 oe;
	pool_entry_t *e = (pool_entry_t *)p;

	u32 flags = irq_save();

#ifndef NDEBUG
	pool_fill_free(pl, e);
#endif

	do {
		e->next = pl->free;
		oe = xchg(e, &pl->free);
	} while (oe != (u32) e->next);
	irq_restore(flags);

	if (e->next == NULL)
		task_ev_wake(pl);
}
