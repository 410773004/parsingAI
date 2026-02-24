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

include_directories(${COMMON_INC} ./inc ./export ../utils/dfi ../nvme/inc ${SRC_ROOT}/dispatcher ${SRC_ROOT}/srb ${SRC_ROOT}/fcore/export ${SRC_ROOT}/ftl/export ${SRC_ROOT}/ncl_20 ${SRC_ROOT}/scheduler)

set(BM_SRC
	./btn_core.c
	./btn_cmd.c
	./btn_cmd_rd.c
	./btn_cmd_wr.c
	./btn_cmd_wrls.c
	./btn_dat.c
	./l2p/l2p_mgr.c
	./l2p/l2p_sw_q.c	
	./dpe/dpe.c
	./crypto.c
	./btn_share.c
	)

add_library(libbtn
	${BM_SRC}
)

add_library(btn OBJECT ./dtag/dtag.c ./btn.c)

if(COMMAND add_pc_lint)
	add_pc_lint(btn ${BM_SRC})
endif(COMMAND add_pc_lint)

