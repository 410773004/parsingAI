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

if (DDR_SIZE_RAINIER)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DDDR_SIZE_RAINIER=${DDR_SIZE_RAINIER})
endif()