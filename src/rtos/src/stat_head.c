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
#include "sect.h"
#include "string.h"
#include "stat_head.h"

/*! \cond PRIVATE */
#define __FILEID__ stat
#include "trace.h"
/*! \endcond */

fast_code void stat_init(stat_head_t *stat, u16 ver, u16 fsize, u32 sig)
{
	stat->version = ver;
	stat->field_size = fsize;
	stat->signature = sig;
}

init_code void stat_restore(stat_head_t *dst, stat_head_t *src, u32 ttl_size)
{
	u16 new_ver = dst->version;
	u16 new_size = dst->field_size;

	if (src == NULL || dst->signature != src->signature) {
		u32 sig = dst->signature;

		if (src) {
			rtos_stat_trace(LOG_ALW, 0x4e53, "new %x stat, sig %x error",
					dst->signature,	src->signature);
		} else {
			rtos_stat_trace(LOG_ALW, 0xbc9e, "src is NULL");
		}

		memset(dst, 0, ttl_size);
		dst->version = new_ver;
		dst->field_size = new_size;
		dst->signature = sig;
		return;
	}

	if (dst->version == src->version) {
		memcpy(dst, src, ttl_size);
	} else {
		rtos_stat_trace(LOG_ALW, 0x5421, "stat %x upg %d(%d) -> %d(%d)",
				dst->signature,
				src->version, src->field_size,
				new_ver, new_size);
		memcpy(dst, src, src->field_size);
		dst->version = new_ver;
		dst->field_size = new_size;
	}
}

init_code bool stat_match(stat_head_t *src, u16 ver, u16 fsize, u32 sig)
{
	if (src->version != ver || src->field_size != fsize || src->signature != sig) {
		return false;
	}
	return true;
}
