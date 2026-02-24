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
/*! \file bucket.h
 * @brief bucket data structure for some asic resource management
 *
 * \addtogroup bucket
 * \defgroup bucket
 * \ingroup bucket
 * @{
 */
#pragma once

#include "types.h"

/*! @brief bucket entry */
typedef union _bucket_t {
	u32 all;
	struct {
		u32 base : 24;		///< resource dma address
		u32 cnt : 8;		///< how many entries in bucket
	} b;
} bucket_t;

/*! @brief bucket entry manager, entry in bucket, bucket in buckets */
typedef struct _buckets_t {
	bucket_t *bucket;		///< bucket buffer
	u8 *link;			///< link
	u32 bucket_sz;			///< bucket size, entry size * entry in bucket

	u8 nr_ent_per_bucket;		///< how many entries per bucket
	u8 free_bucket_cnt;		///< free bucket count
	u8 total_bucket_cnt;		///< total bucket count
	u8 free_entry_cnt;		///< the sum of free entry count in free bucket

	u8 rsvd;
	u8 entry_sz;			///< entry size
	u8 bucket_sz_shf;		///< bucket size shift
	u8 free_item;			///< free bucket id

	u32 base;			///< entry base address
} buckets_t;

/*!
 * @brief buckets initialization function, according parameter to construct buckets
 *
 * @param ent_sz		entry size
 * @param ent_cnt		total entry count
 * @param base			entry base address
 * @param nr_ent_per_bucket	number of entry per bucket
 *
 * @return			return buckets manager
 */
buckets_t *bucket_init(u32 ent_sz, u32 ent_cnt, u32 base, u8 nr_ent_per_bucket);

/*!
 * @brief re-initialize buckets, no buffer allocated
 *
 * @param buckets		buckets to be re-initialized
 * @param ent_sz		entry size
 * @param ent_cnt		total entry count
 * @param base			entry base address
 * @param nr_ent_per_bucket	number of entry per bucket
 *
 * @return			not used
 */
void bucket_resume(buckets_t *buckets, u32 ent_sz, u32 ent_cnt, u32 base, u8 nr_ent_per_bucket);

/*!
 * @brief get a free bucket from buckets manager, we only support get whole free bucket, instead of single entry
 *
 * @param buckets		buckets manager
 * @param retd			return free bucket
 *
 * @return			return true if successfully
 */
bool bucket_get_item(buckets_t *buckets, bucket_t *retd);

/*!
 * @brief return a entry to buckets, will increase count of bucket which belong to this entry, invoke bucket return if all entries in bucket is free
 *
 * @param buckets		buckets manager
 * @param entry		entry to be free
 *
 * @return			return true if bucket free was happened in this call
 */
bool bucket_put_entry(buckets_t *buckets, u32 entry);

/*! @} */
