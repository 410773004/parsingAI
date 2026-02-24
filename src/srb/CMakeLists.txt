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


include_directories(${COMMON_INC} ./inc ./export ../nvme/inc ../nvme/apl/decoder ../spi)
include_directories(${COMMON_INC} ../btn/export ../ncl_20/ ../${BE_MOD}/nal ../${BE_MOD}/ncl ../nvme/export/ ../ftl/)

add_library(libsrb
	./srb.c
	./fw_download.c
	)

target_link_libraries(libsrb ${SRC_ROOT}/lib/liblib.a)

if (SSSTC_FW_UPDATE) 
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSSSTC_FW_UPDATE=1") 
endif()