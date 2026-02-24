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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")

include_directories(
    ${COMMON_INC}
    ${SRC_ROOT}/btn/export
    ${SRC_ROOT}/btn/inc
    ${SRC_ROOT}/utils/dfi
	${SRC_ROOT}/srb
    )

add_library(libdfi OBJECT
    ${SRC_ROOT}/utils/dfi/dfi_init.c
    ${SRC_ROOT}/utils/dfi/dfi_common.c
    ${SRC_ROOT}/utils/dfi/dfi_console.c
    ${SRC_ROOT}/utils/dfi/dfi_dpe_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_calvl_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_pad_cal_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_rdlvl_gate_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_rdlvl_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_vref_dq_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_wrlvl_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_wr_dqdqs_dly_seq.c
    ${SRC_ROOT}/utils/dfi/dfi_wrtrain_seq.c
    )
