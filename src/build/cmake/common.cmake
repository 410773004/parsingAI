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

#set(CMAKE_SYSTEM_NAME  Linux)
#set(CMAKE_C_COMPILER arm-none-eabi-gcc)

set(LINKER_SCRIPT_FILENAME ${CMAKE_PROJECT_NAME}.ld)
set(LINKER_SCRIPT_DIR ${SRC_ROOT}build/gcc/ldscript/)

if (MPC)
	set(post_id _${CPU_ID})
	set(postfix "_x")
endif()

set(LINKER_SCRIPT_CONSTANTS constants${postfix}.ld CACHE INTERNAL "Linker script CONSTANTS component")
set(LINKER_SCRIPT_SECTIONS sections${postfix}.ld CACHE INTERNAL "Linker script SECTIONS component")
set(LINKER_SCRIPT_MEM mem${post_id}.ld CACHE INTERNAL "Linker script MEM component")

execute_process(
	COMMAND ${CMAKE_COMMAND} -E echo "INCLUDE \"${LINKER_SCRIPT_MEM}\"\nINCLUDE \"${LINKER_SCRIPT_CONSTANTS}\"\nINCLUDE \"${LINKER_SCRIPT_SECTIONS}\""
	WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
	OUTPUT_QUIET
	OUTPUT_FILE ${LINKER_SCRIPT_FILENAME}
	)

set(LINKER_SCRIPT ${PROJECT_BINARY_DIR}/${LINKER_SCRIPT_FILENAME} CACHE INTERNAL "Linker script")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib -Xlinker -T ${LINKER_SCRIPT} -L ${LINKER_SCRIPT_DIR} -Wl,-Map=output.map,--gc-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-r -mthumb -mthumb-interwork -fno-stack-protector -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -ggdb -Wall -Werror")

if (MPC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wa,-mimplicit-it=thumb")
endif()

# disable -rdynamic with cmake2.8
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)

# git version
if (CUSTOMER)
# no git index
	if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
		add_custom_target(top_revision.h
			echo. > ${PROJECT_BINARY_DIR}/top_revision.h
			WORKING_DIRECTORY
			${CMAKE_SOURCE_DIR}
			VERBATIM)
	else()
			add_custom_target(top_revision.h
			echo "" > ${PROJECT_BINARY_DIR}/top_revision.h
			WORKING_DIRECTORY
			${CMAKE_SOURCE_DIR}
			VERBATIM)
	endif()
else()
	configure_file(${PROJECT_SOURCE_DIR}/scripts/muxterm.py ${PROJECT_BINARY_DIR}/muxterm.py COPYONLY)
	configure_file(${PROJECT_SOURCE_DIR}/scripts/linux_iterm.sh ${PROJECT_BINARY_DIR}/linux_iterm.sh COPYONLY)
	configure_file(${PROJECT_SOURCE_DIR}/scripts/code_analyze.sh ${PROJECT_BINARY_DIR}/code_analyze.sh COPYONLY)

	if ((CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows") AND NOT ("$ENV{MAKE_MODE}" STREQUAL "unix"))
		add_custom_target(top_revision.h
			git log -1 "--format=format:#define GIT_TOP_REVISION \"%%h\"%%n" HEAD > ${PROJECT_BINARY_DIR}/top_revision.h
			WORKING_DIRECTORY
			${CMAKE_SOURCE_DIR}
			VERBATIM)
	else()
		add_custom_target(top_revision.h
			git log -1 "--format=format:#define GIT_TOP_REVISION \"%h\"%n" HEAD > ${PROJECT_BINARY_DIR}/top_revision.h
			WORKING_DIRECTORY
			${CMAKE_SOURCE_DIR}
			VERBATIM)
	endif()
endif()
