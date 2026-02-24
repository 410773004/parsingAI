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

if (OC_SSD)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DOC_SSD=1)
endif()

if (HAVE_VELOCE)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DHAVE_VELOCE)
endif()

if (FPGA)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DFPGA)
endif()

if (HAVE_T0)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DHAVE_T0)
elseif (HAVE_A0)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DHAVE_A0)
endif()

if (RAMDISK)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DRAMDISK)
	if (RAMDISK_L2P)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DRAMDISK_L2P)
	elseif (SRIOV_SUPPORT)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DSRIOV_SUPPORT=1)
	endif()
	if (RAMDISK_2NS)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DRAMDISK_2NS=1)
	endif()
	if (RAMDISK_FULL)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DRAMDISK_FULL=1)
	endif()
endif()

if (RAWDISK)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DRAWDISK)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DCPU_BE=1)

	if (SRIOV_SUPPORT)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DSRIOV_SUPPORT=1)
	endif()

	if (RAWDISK_L2P)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DRAWDISK_L2P=1)
	endif()
endif()
if (PROGRAMMER)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DCPU_BE=1)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DPROGRAMMER=1)
endif()

if (ENABLE_SOUT)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DENABLE_SOUT=1)
endif()

if (CPU_ID)
	math(EXPR CPU_ID_0 "${CPU_ID} - 1")
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DCPU_ID=${CPU_ID} -DCPU_ID_0=${CPU_ID_0})
	if (${CPU_ID} LESS 1 OR ${CPU_ID} GREATER 4)
		message(FATAL_ERROR "CPU ID is out of range from 1 to 4, exit.")
	endif()
else()
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DCPU_ID=1 -DCPU_ID_0=0)
endif()

if (TFW_TEST)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DTFW_CTRL_${TFW_TEST})
endif()

set(RAINIER_PLATFORM "true")

add_definitions(${PP_DEFINITIONS})

set(TARGET_OBJS main.c version.c)
add_subdirectory(${SRC_ROOT}/rtos/)
set(LIB_OBJS ${LIB_OBJS} librtos)
set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:rtos>)

set(FE_MOD nvme CACHE STRING "nvme" FORCE)
set(BE_MOD ncl_20 CACHE STRING "ncl" FORCE)
set(FTL_MOD ftl CACHE STRING "ftl" FORCE)

add_subdirectory(${SRC_ROOT}/utils/)
add_subdirectory(${SRC_ROOT}/btn/)
set(LIB_OBJS ${LIB_OBJS} libbtn)
set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:btn>)

add_subdirectory(${SRC_ROOT}/btn/dpe)
set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:dpe>)

add_subdirectory(${SRC_ROOT}/dispatcher/)
set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:disp>)

add_subdirectory(${SRC_ROOT}/${FE_MOD}/)
set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:fe>)
set(LIB_OBJS ${LIB_OBJS} libfe)

if (RAWDISK OR PROGRAMMER)
	add_subdirectory(${SRC_ROOT}/srb/)
	set(LIB_OBJS ${LIB_OBJS} libsrb)

	if (STATIC_NCL)
		message("use static NCL library")
	else()
		if (USE_OLD_NCL)
			add_subdirectory(${SRC_ROOT}/ncl/)
		else()
			add_subdirectory(${SRC_ROOT}/ncl_20/)
			set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:be>)
		endif()
		set(LIB_OBJS ${LIB_OBJS} libbe)
	endif()
endif()

if (TFW_MOD)
	add_subdirectory(${SRC_ROOT}/${TFW_MOD}/)
	set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:tfw>)
	set(LIB_OBJS ${LIB_OBJS} libtfw)
endif()

if (DDR_TEST)
	add_subdirectory(${SRC_ROOT}/utils/dfi)
	set(TARGET_OBJS ${TARGET_OBJS} $<TARGET_OBJECTS:libdfi>)
endif()


set(LIB_OBJS ${LIB_OBJS} libutils)

include_directories(${COMMON_INC})

# message(${TARGET_OBJS})
# message(${LIB_OBJS})
set(image main)
add_executable(${image} ${TARGET_OBJS})

if (STATIC_NCL)
	if (USE_MU_NAND)
		target_link_libraries(main ${SRC_ROOT}/ncl/liblibbe_mu.a)
	endif()
	if (USE_TSB_NAND)
		target_link_libraries(main ${SRC_ROOT}/ncl/liblibbe_tsb.a)
	endif()
endif()
target_link_libraries(main ${SRC_ROOT}/lib/liblib.a)
target_link_libraries(main ${LIB_OBJS})

add_dependencies(main top_revision.h)

add_dependencies(${LIB_OBJS} trace-eventid.h)
add_dependencies(libutils trace-eventid.h)

if(COMMAND add_pc_lint)
	add_pc_lint(main ${TARGET_OBJS})
endif(COMMAND add_pc_lint)
