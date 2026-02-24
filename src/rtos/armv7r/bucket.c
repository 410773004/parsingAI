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

/*! \file bucket.c
 * @brief bucket data structure for some asic resource management
 *
 * allocate a bucket with entries, and assign this resource to an asic queue entry
 * asic will return entry by entry to firmware, save free queue assign time
 *
 * \addtogroup bucket
 * \defgroup bucket
 * \ingroup bucket
 * @{
 */
#include "bucket.h"
#include "stdlib.h"
#include "sect.h"
#include "assert.h"

/*!
 * @brief put whole free bucket into buckets manager
 *
 * @param buckets	buckets manager
 * @param bucket	bucket to be free
 *
 * @return		not used
 */
fast_code void bucket_put_item(buckets_t *buckets, bucket_t *bucket)
{
	u8 p = buckets->free_item;

	buckets->free_item = bucket - buckets->bucket;
	buckets->link[buckets->free_item] = p;

	buckets->free_bucket_cnt += 1;
}

fast_code void bucket_resume(buckets_t *buckets, u32 ent_sz, u32 ent_cnt, u32 base, u8 nr_ent_per_bucket)
{
	u32 i;
	u32 bucket_cnt;

	sys_assert(is_power_of_two(nr_ent_per_bucket));
	sys_assert(is_power_of_two(ent_sz));
	sys_assert(is_power_of_two(ent_cnt));
	sys_assert((base & (ent_sz - 1)) == 0);

	bucket_cnt = ent_cnt / nr_ent_per_bucket;
	sys_assert(bucket_cnt * nr_ent_per_bucket == ent_cnt);
	sys_assert(bucket_cnt < 256);

	buckets->nr_ent_per_bucket = nr_ent_per_bucket;
	buckets->total_bucket_cnt = bucket_cnt;
	buckets->bucket = ptr_inc(buckets, sizeof(buckets_t));
	buckets->link = ptr_inc(buckets->bucket, sizeof(bucket_t) * bucket_cnt);

	buckets->entry_sz = ent_sz;
	buckets->bucket_sz = ent_sz * nr_ent_per_bucket;
	buckets->bucket_sz_shf = ctz(buckets->bucket_sz);
	buckets->base = base;

	buckets->free_entry_cnt = 0;
	buckets->free_bucket_cnt = 0;
	buckets->free_item = 0xFF;

	for (i = 0; i < buckets->total_bucket_cnt; i++) {
		buckets->bucket[i].b.base = base;
		buckets->bucket[i].b.cnt = buckets->nr_ent_per_bucket;
		base += buckets->bucket_sz;
		bucket_put_item(buckets, &buckets->bucket[i]);
		buckets->free_entry_cnt += buckets->nr_ent_per_bucket;
	}
}

init_code buckets_t *bucket_init(u32 ent_sz, u32 ent_cnt, u32 base, u8 nr_ent_per_bucket)
{
	u32 sz;
	u32 bucket_cnt;
	buckets_t *buckets;

	sys_assert(is_power_of_two(nr_ent_per_bucket));
	sys_assert(is_power_of_two(ent_sz));
	sys_assert(is_power_of_two(ent_cnt));
	sys_assert((base & (ent_sz - 1)) == 0);

	bucket_cnt = ent_cnt / nr_ent_per_bucket;
	sys_assert(bucket_cnt * nr_ent_per_bucket == ent_cnt);
	sys_assert(bucket_cnt < 256);

	sz = sizeof(buckets_t) + nr_ent_per_bucket * sizeof(bucket_t) * bucket_cnt;
	buckets = sys_malloc(FAST_DATA, sz);
	if (buckets == NULL)
		return NULL;

	bucket_resume(buckets, ent_sz, ent_cnt, base, nr_ent_per_bucket);

	return buckets;
}

fast_code bool bucket_get_item(buckets_t *buckets, bucket_t *retd)
{
	if (buckets->free_bucket_cnt == 0)
		return false;

	u8 ret = buckets->free_item;

	buckets->free_bucket_cnt--;
	buckets->free_entry_cnt -= buckets->nr_ent_per_bucket;

	buckets->free_item = buckets->link[ret];
	sys_assert(buckets->bucket[ret].b.cnt == buckets->nr_ent_per_bucket);
	retd->all = buckets->bucket[ret].all;
	buckets->bucket[ret].b.cnt = 0;
	retd->b.cnt -= 1;	/// for for asic, this value is 0 base, when bucket in buckets manager, it was 1 base
	return true;
}

fast_code bool bucket_put_entry(buckets_t *buckets, u32 entry)
{
	u32 bid = (entry - buckets->bucket[0].b.base) >> buckets->bucket_sz_shf;
	sys_assert(bid < buckets->total_bucket_cnt);
	bool bucket_recycled = false;

	sys_assert(buckets->bucket[bid].b.cnt < buckets->nr_ent_per_bucket);
	buckets->bucket[bid].b.cnt++;
	buckets->free_entry_cnt++;
	if (buckets->bucket[bid].b.cnt == buckets->nr_ent_per_bucket) {
		bucket_put_item(buckets, &buckets->bucket[bid]);
		bucket_recycled = true;
	}

	return bucket_recycled;
}

/*! @} */
