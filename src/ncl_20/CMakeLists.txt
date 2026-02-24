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

if (USE_MU_NAND)
set (VENDOR "mu")
endif()

if (USE_UNIC_NAND)
set (VENDOR "unic")
endif()

if (USE_YMTC_NAND)
set (VENDOR "ymtc")
endif()

if (USE_TSB_NAND)
set (VENDOR "tsb")
endif()

if (USE_HYNX_NAND)
set (VENDOR "sk")
endif()

if (USE_SNDK_NAND)
set (VENDOR "sndk")
endif()

if (USE_SS_NAND)
set (VENDOR "ss")
endif()

include_directories(${COMMON_INC}
					../btn/export
					../srb
					../utils/dfi
					../btn/dtag
				        ../spi
					${SRC_ROOT}/${BE_MOD}
					${SRC_ROOT}/${BE_MOD}/vendor
					${SRC_ROOT}/${BE_MOD}/vendor/${VENDOR}
					${SRC_ROOT}/${BE_MOD}/cmf
					${SRC_ROOT}/${BE_MOD}/debug
					${SRC_ROOT}/${BE_MOD}/test
					${SRC_ROOT}/nvme/export
					${SRC_ROOT}/nvme/inc
					${SRC_ROOT}/ftl/export
					${SRC_ROOT}/fcore/export
					${SRC_ROOT}/fcore
					${SRC_ROOT}/scheduler
					${SRC_ROOT}/ftl
					${SRC_ROOT}/dispatcher)


include_directories(${COMMON_INC} ${SRC_ROOT}/${BE_MOD}/inc)

set_property(SOURCE ${ASM_SRCS} PROPERTY LANGUAGE C)

set (NOBJS
	${SRC_ROOT}/${BE_MOD}/ndcu.c
	${SRC_ROOT}/${BE_MOD}/eccu.c
	${SRC_ROOT}/${BE_MOD}/ficu.c
	${SRC_ROOT}/${BE_MOD}/nand.c
	${SRC_ROOT}/${BE_MOD}/ndphy.c
	${SRC_ROOT}/${BE_MOD}/ncl_cmd.c
	${SRC_ROOT}/${BE_MOD}/nand_onfi.c
	${SRC_ROOT}/${BE_MOD}/nand_toggle.c
	${SRC_ROOT}/${BE_MOD}/ncl_erd.c
	${SRC_ROOT}/${BE_MOD}/ncl_pmu.c
	${SRC_ROOT}/${BE_MOD}/ncl_raid.c
	${SRC_ROOT}/${BE_MOD}/ncl_cache_read.c
	${SRC_ROOT}/${BE_MOD}/vendor/${VENDOR}/ndcu_${VENDOR}.c
	${SRC_ROOT}/${BE_MOD}/vendor/${VENDOR}/nand_${VENDOR}.c
	${SRC_ROOT}/${BE_MOD}/vendor/${VENDOR}/ard_${VENDOR}.c
	${SRC_ROOT}/${BE_MOD}/vendor/${VENDOR}/erd_${VENDOR}.c
	${SRC_ROOT}/${BE_MOD}/debug/ncb_register.c
#	${SRC_ROOT}/${BE_MOD}/debug/ncl_console.c
	${SRC_ROOT}/${BE_MOD}/test/ncl_test.c
	${SRC_ROOT}/${BE_MOD}/test/nand_cache_read_test.c
	${SRC_ROOT}/${BE_MOD}/test/nand_cache_prog_test.c
	${SRC_ROOT}/${BE_MOD}/ncl_todo.c
	${SRC_ROOT}/${BE_MOD}/GList.c 
	${SRC_ROOT}/${BE_MOD}/feature/ncl_onfi_dcc_training.c
	${SRC_ROOT}/${BE_MOD}/read_retry.c
        ../spi/spi.c 
	)

add_library(libbe ${NOBJS})

set(NCL_OBJS ${SRC_ROOT}/${BE_MOD}/ncl.c ${SRC_ROOT}/${BE_MOD}/debug/ncl_console.c ${SRC_ROOT}/${BE_MOD}/epm.c)
add_library(be OBJECT ${NCL_OBJS})

if (USE_FICU_DBG)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_FICU_DBG=1")
endif()
if (USE_TSB_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_TSB_NAND=1")
endif()
if (USE_MU_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_MU_NAND=1")
endif()
if (USE_UNIC_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_UNIC_NAND=1")
endif()
if (USE_YMTC_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_YMTC_NAND=1")
endif()
if (USE_HYNX_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_HYNX_NAND=1")
endif()
if (USE_SNDK_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_SNDK_NAND=1")
endif()
if (USE_SS_NAND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_SS_NAND=1")
endif()
if (QLC_SUPPORT)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DQLC_SUPPORT=1")
endif()
if (USE_8K_DU)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_8K_DU=1")
endif()
if (USE_8K_PAGE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_8K_PAGE=1")
endif()
if (USE_512B_HOST_SECTOR_SIZE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_512B_HOST_SECTOR_SIZE=1")
endif()
if (NCL_STRESS)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNCL_STRESS=1")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${SRC_ROOT}/${BE_MOD}/ncl_header.pch")
