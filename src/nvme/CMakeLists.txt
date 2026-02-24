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
include_directories(${COMMON_INC} ./inc/registers ./inc/registers ./inc ./export ./apl/decoder ../dispatcher ../ftl/export ../fcore/export)
include_directories(${COMMON_INC} ../btn/export
					../inc
					../srb
					../ftl/export
					../ncl_new/crypto
					../rtos/armv7r/
					../btn/
					../ncl_20
					../btn/inc/
					../spi
					../${BE_MOD}/
					../${BE_MOD}/nal
					../${BE_MOD}/ncl
					../${BE_MOD}/hal/crypto
					../${BE_MOD}/hal/include
					../utils/dfi)

if(TCG_SUPPORT)
include_directories(${COMMON_INC} ../tcg)
endif()

set(NVME_SRCS
	./apl/decoder/admin_cmd.c
	./apl/decoder/io_cmd.c
	./hal/cmd_proc.c
	./hal/hmb.c
	./apl/decoder/hmb_cmd.c
#	./apl/decoder/security_cmd.c
#	./apl/decoder/token.c
	./apl/decoder/smart.c
	./hal/ctrlr.c
	./apl/decoder/security_api.c
	./apl/decoder/bigdigits.c
	./apl/decoder/security_test.c
	./apl/decoder/ssstc_cmd.c
#	./tcg/tcg.c
	)

add_library(libfe ${NVME_SRCS})

set(NVME_OBJS ./apl/decoder/core.c ./hal/ctrlr.c)
target_link_libraries(libfe ${SRC_ROOT}/lib/liblib.a)
add_library(fe OBJECT ${NVME_OBJS})

if (COMMAND add_pc_lint)
	add_pc_lint(nvme ${NVME_OBJS} ${NVME_SRCS})
endif(COMMAND add_pc_lint)
