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
					../fcore/export
					../nvme/apl/decoder
					../nvme/inc
					../dispatcher
					../srb
					../spi
					../ftl
					../rtos/inc
					../${BE_MOD}/
					../${BE_MOD}/nal
					../${BE_MOD}/ncl
					../${BE_MOD}/hal/crypto
					../${BE_MOD}/hal/include
					${SRC_ROOT}/scheduler
					)

add_library(libschl
	die_que.c
	addr_trans.c
	read_error.c
	)
set(SCHL_OBJS scheduler.c)

add_library(schl OBJECT ${SCHL_OBJS})

if (NOT(DUAL_BE))
	target_link_libraries(libschl libfcore)
endif()

if (COMMAND add_pc_lint)
	add_pc_lint(schl ${SCHL_OBJS})
endif(COMMAND add_pc_lint)
