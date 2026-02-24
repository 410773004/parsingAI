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
#include "heap.h"
#include "stdio.h"
#include "sect.h"

/**
 * \file heap.c
 * \brief This file contains definitions of heap api.
 * \addtogroup heap
 * @{
 *
 * Heap allow firmware to dynamic allocate, free memory. We have three heap for
 * different memory type, SLOW_DATA, FAST_DATA, PS_DATA.
 */

/*! @brief object to memory block */
typedef struct mblock {
	struct mblock *next;	///< pointer to next memory block
	u32 size;		///< memory block size
} mblock_t;

#define ALIGN_TYPE 4		///< heap is aligned to
#define MEMBLOCKSIZE /*lint -e(506)*/  \
        ((sizeof(struct mblock) & (ALIGN_TYPE - 1)) ? \
		((sizeof(struct mblock) + ALIGN_TYPE) & ~(ALIGN_TYPE - 1)) : \
		(sizeof(struct mblock)))	///< aligned memory block size

fast_code heap_t *heap_init(char *base, u32 size)
{
	heap_t *mh = (heap_t *) base;

	/* make sure the heap is aligned */
	if ((u32)base & (ALIGN_TYPE - 1)) {
		base =
		    (char *)(((u32)base + (ALIGN_TYPE - 1)) &
			     ~(ALIGN_TYPE - 1));
		size -= (ALIGN_TYPE - 1);
	}
	/* trim heap to multiple of ALIGN_TYPE */
	size &= ~(ALIGN_TYPE - 1);

	size -= sizeof(*mh);
	base += sizeof(*mh);

	mh->base = (mblock_t *) base;
	mh->free = (mblock_t *) base;

	mh->free->size = size - MEMBLOCKSIZE;
	mh->free->next = NULL;

	mh->totfree = mh->free->size;

	return mh;
}

fast_code char *heap_alloc(heap_t *heap, u32 size)
{
	if (size & (ALIGN_TYPE - 1)) {
		size = (size + ALIGN_TYPE) & ~(ALIGN_TYPE - 1);
	}

	int lsize = size + MEMBLOCKSIZE;
	mblock_t *bp = heap->free;
	mblock_t *lastb = NULL;

	while (bp) {
		if (bp->size >= size) {	/* take first-fit free block */
			/* Decide if the block is big enough to be worth dividing */
			if (bp->size > (size + (MEMBLOCKSIZE * 2))) {
				mblock_t *new;
				/* Divide block and return front part to caller. First
				 * make a new block after the portion we will return
				 */
				new = (mblock_t *) ((char *)(bp) + lsize);
				new->size = bp->size - lsize;
				new->next = bp->next;

				/* modify bp to reflect smaller size we will return */
				bp->next = new;
				bp->size = size;
			} else {	/* not worth fragmenting block, return whole thing */
				lsize = bp->size;	/* adjust lostsize */
			}

			if (lastb)	/* unlink block from queue */
				lastb->next = bp->next;
			else
				heap->free = bp->next;

			/* keep statistics */
			heap->totfree -= lsize;
			bp->next = heap->base;	/* tag next ptr with illegal value */

			return ((char *)(bp) + MEMBLOCKSIZE);
		}
		lastb = bp;
		bp = bp->next;
	}

	return NULL;
}

fast_code void heap_free(heap_t *heap, char *ptr)
{
	/* find pointer to prepended mem_block struct */
	mblock_t *freep = (mblock_t *) (ptr - MEMBLOCKSIZE);

	if (freep->next != heap->base) {
		/* sanity check next ptr for tag */
		evlog_printk(LOG_ALW,"next 0x%x base 0x%x", freep->next, heap->base);
		panic("heap_free");
	}
	heap->totfree += freep->size + MEMBLOCKSIZE;

	int merged = 0;
	mblock_t *last = NULL, *tmp;

	for (tmp = heap->free; tmp; tmp = tmp->next)
	{
		if (freep < tmp) {
			/* found slot to insert freep */
			if (((char *)freep + freep->size + MEMBLOCKSIZE) == (char *)tmp) {
				/* merge with next block */
				freep->next = tmp->next;
				freep->size += (tmp->size + MEMBLOCKSIZE);
				if (last)
					last->next = freep;
				else
					heap->free = freep;
				merged++;
			}
			if (last &&
				(((char *)last + last->size + MEMBLOCKSIZE) == (char *)freep)) {
				/* merge with previous block */
				last->size += (freep->size + MEMBLOCKSIZE);
				if (merged) {	/* if already merged, preserve next ptr */
					last->next = freep->next;
				}
				merged++;
			}

			if (!merged) {
				/* insert into list */
				if (last) {
					freep->next = last->next;
					last->next = freep;
				} else {
					freep->next = heap->free;
					heap->free = freep;
				}
				heap->totfree -= MEMBLOCKSIZE;
			}

			if (merged == 2) {
				heap->totfree += MEMBLOCKSIZE;
			}

			return;
		}
		/* set "last" pointer for next loop */
		last = tmp;
	}

	if (!last) {
		heap->free = freep;
		freep->next = NULL;
		return;
	}

	if (((char *)last + last->size + MEMBLOCKSIZE) == (char *)freep) {
		/* merge it with last block */
		last->size += (freep->size + MEMBLOCKSIZE);
	} else {
		freep->next = last->next;
		last->next = freep;
	}
}

fast_code u32 heap_get_mblock_size(heap_t *heap)
{
	mblock_t *tmp;
	u32 size = sizeof(heap_t);

	for (tmp = (mblock_t *)(++heap); tmp->next; tmp = (mblock_t *)((void *)tmp + tmp->size + MEMBLOCKSIZE)) {
		size += sizeof(mblock_t);
	}
	size += sizeof(mblock_t);

	return size;
}

fast_code void heap_save_mblock_struct(heap_t *heap, void *buf)
{
	mblock_t *tmp;
	heap_t *p_heap = (heap_t *)buf;
	mblock_t *p_mblock = (mblock_t *)(buf + sizeof(heap_t));

	*p_heap = *heap;
	for (tmp = (mblock_t *)(++heap); tmp->next; tmp = (mblock_t *)((void *)tmp + tmp->size + MEMBLOCKSIZE)) {
		*p_mblock = *tmp;
		p_mblock++;
	}
	*p_mblock = *tmp;

	return;
}

fast_code void heap_restore_mblock_struct(heap_t *heap, void *buf)
{
	mblock_t *tmp;
	heap_t *p_heap = (heap_t *)buf;
	mblock_t *p_mblock = (mblock_t *)(buf + sizeof(heap_t));

	*heap = *p_heap;
	for (tmp = (mblock_t *)(++heap); p_mblock->next; p_mblock++) {
		*tmp = *p_mblock;
		tmp = (mblock_t *)((void *)tmp + tmp->size + MEMBLOCKSIZE);
	}
	*tmp = *p_mblock;

	return;
}
