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

cmake_minimum_required(VERSION 2.8)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_C_COMPILER_FORCED TRUE)

project(matrix LANGUAGES C)

set(SRC_ROOT ${PROJECT_SOURCE_DIR}/)

include(build/cmake/common.cmake)
include(build/cmake/trace.cmake)
include(build/cmake/pc_lint.cmake)

set(COMMON_INC
	${CMAKE_BINARY_DIR}/
	${SRC_ROOT}/inc
	${SRC_ROOT}/rtos/inc
	${SRC_ROOT}/rtos/armv7r
	)

# if you would like to add new feature control, please add it in feature.cmake
include(build/cmake/feature.cmake)

# if you would like to add new nand control, please add it in feature.cmake
include(build/cmake/nand.cmake)

# if you would like to add new ddr control, please add it to ddr.cmake
include(build/cmake/ddr.cmake)

if (MPC)
	# if would like to modify multiple CPU build env, please modify mpc.cmake
	include(build/cmake/mpc.cmake)
else()
	# if would like to modify single CPU build env, please modify spc.cmake
	include(build/cmake/spc.cmake)
endif()

add_custom_command(
	TARGET ${image}
	POST_BUILD
	COMMAND ${CMAKE_OBJCOPY} -O binary -j .rostr_data ${image} ${image}.rostr_data
	COMMENT "Creating RO string Data"
)

add_custom_command(
	TARGET ${image}
	POST_BUILD
	COMMAND ${CMAKE_OBJDUMP} -h ${image} > ${image}.elfinfo
	COMMENT "Creating ELF info file"
)
