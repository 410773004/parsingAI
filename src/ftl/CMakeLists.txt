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
					../fcore/export
					../ftl/export
					../scheduler
					../ncl_new/crypto
					../dispatcher
					../ncl_20/
					../ncl_20/nal
					../ncl_20/ncl
					../ncl_20/hal/crypto
					../ncl_20/hal/include
					../srb
					../nvme/inc
					../nvme/api/decoder
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-multichar")

add_library(libftl
	./export/sync_ncl_helper.c
	./export/spb_log.c
	./export/log.c
	log_flush.c
	frb_log.c
	spb_info.c
	spb_log_flush.c
	defect_mgr.c
	system_log.c
	ftl_ns.c
	spb_pool.c
	spb_mgr.c
	ftl_spor.c
	ftl_l2p.c
	ftl_host_ns.c
	ftl_int_ns.c
	ftl_flush.c
	gc.c
	recon.c
	blk_pool.c
	blk_log.c
	fe_info.c
	bg_task.c
	ftl_pmu.c
	ErrorHandle.c
	)

set(FTL_OBJS ftl.c)

add_library(ftl OBJECT ${FTL_OBJS})
target_link_libraries(libftl libl2p)

if (COMMAND add_pc_lint)
	add_pc_lint(ftl ${FTL_OBJS})
endif(COMMAND add_pc_lint)
