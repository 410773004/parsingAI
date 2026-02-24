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
#include "ncl.h"

void ncl_cache_read_init(void);
// Filter read command and split into sub-tasks, the tasks will be submitted as cache read when successor task arrives
void ncl_cmd_split_task(struct ncl_cmd_t *ncl_cmd);
// Call me when idle as soon as possible
void ncl_tasks_clear(void);
