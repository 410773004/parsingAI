#------------------------------------------------------------------------------
#                 Copyright(c) 2016-2019 Innogrit Corporation
#                             All Rights reserved.
#
#  The confidential and proprietary information contained in this file may
#  only be used by a person authorized under and to the extent permitted
#  by a subsisting licensing agreement from Innogrit Corporation.
#  Dissemination of this information or reproduction of this material
#  is strictly forbidden unless prior written permission is obtained
#  from Innogrit Corporation.
#------------------------------------------------------------------------------

include_directories(${COMMON_INC} ../btn/export
					../ftl/export
					../dispatcher
					../${BE_MOD}/
					../${BE_MOD}/nal
					../${BE_MOD}/ncl
					../${BE_MOD}/hal/crypto
					../${BE_MOD}/hal/include
					./export
					../scheduler
					../srb
					../utils/dfi
					../rtos/armv7r
					../btn/dtag
                                         ../nvme/inc)

add_library(libfcore
	ftl_core.c
	pstream.c
	ftl_core_gc.c
	ftl_raid.c
	ftl_core_ipc.c
	ftl_p2l.c
	ftl_core_pmu.c
	)

set(FTL_CORE_OBJS be_main.c erase.c)

add_library(fcore OBJECT ${FTL_CORE_OBJS})
target_link_libraries(libfcore libschl)
target_link_libraries(libfcore libutils)

if (COMMAND add_pc_lint)
	add_pc_lint(fcore ${FTL_CORE_OBJS})
endif(COMMAND add_pc_lint)


