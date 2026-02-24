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

include_directories(${COMMON_INC} ../nvme/inc ../nvme/export)
include_directories(${COMMON_INC} ../btn/export
					../srb
					../spi
					../ncl_20/
					../ncl_20/nal
					../ncl_20/ncl
					../ncl_20/hal/crypto
					../ncl_20/hal/include
                                        ../nvme/apl/decoder
					../fcore/export)
include_directories(${COMMON_INC} ../utils/dfi)

if (TCG_SUPPORT)
	include_directories(${COMMON_INC} ../tcg)
endif()


if (RDISK AND MPC)
	include_directories(../fcore/export/ ../scheduler/ ../ftl/export ./rdisk)
	if (RDISK_BE)
		set(DISP_OBJS rdisk_be.c trim.c)
	else()
		set(DISP_OBJS rdisk.c ucache.c bg_training.c trim.c plp.c ra.c)
	endif()
elseif (RAWDISK)
	if (MPC AND RAWDISK_FE)
		set(DISP_OBJS ${DISP_OBJS} rawdisk_fe.c)
	elseif (MPC AND RAWDISK_BE)
		set(DISP_OBJS ${DISP_OBJS} rawdisk_be.c)
	else()
		set(DISP_OBJS ${DISP_OBJS} rawdisk.c)
	endif()
elseif (PROGRAMMER)
	set(DISP_OBJS ${DISP_OBJS} vdisk.c)
elseif (OC_SSD)
	set(DISP_OBJS ${DISP_OBJS} ocssd.c)
elseif (RAMDISK_2NS)
	set(DISP_OBJS ${DISP_OBJS} ramdisk_ns.c)
elseif (RAMDISK_FULL)
	set(DISP_OBJS ${DISP_OBJS} ramdisk_full.c)
elseif (TFW_MOD OR RAMDISK)
	set(DISP_OBJS ${DISP_OBJS} ramdisk.c)
else()
	message(FATAL_ERROR "Please define target type, etc. FTKDISK, RAWDISK, PROGRAMMER, TWF_MOD or RAMDISK with -DXXX=1.")
endif()

add_library(disp OBJECT ${DISP_OBJS})

if (COMMAND add_pc_lint)
	add_pc_lint(disp ${DISP_OBJS})
endif(COMMAND add_pc_lint)

if (SSSTC_FW_UPDATE) 
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSSSTC_FW_UPDATE=1") 
endif()