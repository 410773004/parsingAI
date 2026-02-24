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
add_custom_target(
	trace-eventid.h
	COMMAND python ${SRC_ROOT}/scripts/trace-gen.py -o ${PROJECT_BINARY_DIR}/
	WORKING_DIRECTORY ${SRC_ROOT}
	)

execute_process(
	COMMAND python ${SRC_ROOT}/scripts/trace-gen.py -o ${PROJECT_BINARY_DIR}/
	WORKING_DIRECTORY ${SRC_ROOT}
	)
