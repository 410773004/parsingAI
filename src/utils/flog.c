//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2018 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

/*! \file flog.h
 * @brief memory log implementation for observation
 *
 * \addtogroup utils
 * \defgroup flog
 * \ingroup utils
 * @{
 */
#include "types.h"
#include "flog.h"
#include "sect.h"
#include "stdlib.h"

fast_code flog_t *flog_init(u32 size)
{
	u32 sz = size * sizeof(flog_ent_t) + offsetof(flog_t, ent);

	flog_t *flog = sys_malloc(SLOW_DATA, sz);

	if (flog == NULL)
		return NULL;

	flog->idx = 0;
	flog->total = size;
	return flog;
}

fast_code void flog_add(flog_t *flog, u32 a, u32 b, u32 c, u32 d)
{
	flog_ent_t *ent = &flog->ent[flog->idx];

	if (flog->idx >= flog->total)
		return;

	ent->d[0] = a;
	ent->d[1] = b;
	ent->d[2] = c;
	ent->d[3] = d;

	flog->idx += 1;
	if (flog->idx >= flog->total)
		flog->idx = 0;
}
