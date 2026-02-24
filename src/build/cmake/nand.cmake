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

if (USE_TSB_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_TSB_NAND=1)
endif()

if (USE_MU_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_MU_NAND=1)
	if (MU_B27B)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMU_B27B=1")
	endif()
endif()

if (USE_UNIC_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_UNIC_NAND=1)
endif()

if (USE_HYNX_NAND)
	if (USE_8K_PAGE)
		set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_8K_PAGE=1)
	endif()

	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_HYNX_NAND=1)
endif()

if (USE_SNDK_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_SNDK_NAND=1)
endif()

if (USE_SS_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_SS_NAND=1)
endif()

if (USE_YMTC_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DUSE_YMTC_NAND=1)
endif()

if (TSB_XL_NAND)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DTSB_XL_NAND=1)
endif()

if (QLC_SUPPORT)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DQLC_SUPPORT=1)
endif()

if (NCL_STRESS)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DNCL_STRESS=1)
endif()

if (MAX_CHANNEL)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DMAX_CHANNEL=${MAX_CHANNEL})
endif()

if (MAX_TARGET)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DMAX_TARGET=${MAX_TARGET})
endif()

if (MAX_PLANE)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DMAX_PLANE=${MAX_PLANE})
else()
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DMAX_PLANE=4)
endif()

if (FAST_MODE)
	set(PP_DEFINITIONS ${PP_DEFINITIONS} -DFAST_MODE=1)
endif()
