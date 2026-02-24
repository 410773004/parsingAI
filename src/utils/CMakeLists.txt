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

include_directories(${COMMON_INC}
					../ncl_20/
					../btn/export
					../scheduler
					../fcore/export
					../ftl/export
					../srb
                                        ../btn
					../btn/inc
					../dispatcher
					../spi
					../tcg
					)

add_library(libutils
	${SRC_ROOT}/utils/flog.c
	${SRC_ROOT}/utils/trace.c
	${SRC_ROOT}/utils/mpc.c
	${SRC_ROOT}/utils/ipc_api.c
	${SRC_ROOT}/utils/idx_meta.c
	${SRC_ROOT}/utils/evlog.c
	../spi/spi.c
	)
