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

include_directories(${COMMON_INC} ../inc ../export ../)

set(BM_SRC sync_dpe.c)

add_library(dpe OBJECT ${BM_SRC})

if(COMMAND add_pc_lint)
	add_pc_lint(dpe ${BM_SRC})
endif(COMMAND add_pc_lint)

