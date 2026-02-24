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

#ifndef _HEAP_H_
#define _HEAP_H_

/*!
 * \file heap.h
 * @brief This file contains definitions of heap api.
 * \addtogroup heap
 * @{
 * Details
 */

/*!
 * @brief object to heap
 */
typedef struct heap {
	struct mblock *base;	///< pointer to base address
	struct mblock *free;	///< pointer to first free entry
	u32 totfree;	///< byte of total free entry
} heap_t;

/*!
 * @brief Called at system initialization to setup heap for using.
 *
 * MUST be called before any calls to heap_alloc(), takes a single contiguous
 * memory space and sets it up to be used by heap_alloc() and heap_free().
 *
 * @param base	address for start of heap area in memory
 * @param size	size of heap area at address
 *
 * @return	pointer of the heap handler
 */
heap_t *heap_init(char *base,
		u32 size
		);

/*!
 * @brief allocate memory from heap
 *
 * @param heap	heap object
 * @param size	how much memory need to be allocated
 *
 * @return	pointer to memory allocated
 */
char *heap_alloc(heap_t *heap,
		u32 size
		);

/*!
 * @brief free memory back to heap object
 *
 * Find block which contains buffer and insert it in free list. Maintains list
 * in order, low to high memory.
 *
 * @param heap	heap object
 * @param ptr	buffer to add to free list
 *
 * @return	None
 */
void heap_free(heap_t *heap,
		char *ptr
		);

/*!
 * @brief get size for heap add mblock
 *
 * @param heap	heap object
 *
 * @return	size for heap add mblock
 */
u32 heap_get_mblock_size(heap_t *heap);

/*!
 * @brief save heap add mblock to buffer
 *
 * @param heap	heap object
 * @param buf	save buffer
 *
 * @return	None
 */
void heap_save_mblock_struct(heap_t *heap, void *buf);

/*!
 * @brief restore heap add mblock from buffer
 *
 * @param heap	heap object
 * @param buf	source buffer
 *
 * @return	None
 */
void heap_restore_mblock_struct(heap_t *heap, void *buf);
/*! @} */
#endif
