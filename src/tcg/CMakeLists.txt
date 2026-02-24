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

set(TCG_DIR
    ${SRC_ROOT}/tcg       
)

include_directories(${COMMON_INC} ${TCG_DIR})
include_directories(${COMMON_INC} ../btn
				  ../btn/export
				  ../btn/inc
				  ../inc
				  ../rtos/armv7r
				  ../nvme/inc
				  ../nvme/apl/decoder
				  ../ncl_20
				  ../dispatcher)

#set(TCG_LIB
#    )

if (CPU_ID EQUAL 1)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTCG_SUPPORT=1")
#	if (NOT(_TCG_ STREQUAL "NONE"))
		set(TCG_SRCS
#			${SRC_ROOT}/tcg/security_cmd.c 
			${SRC_ROOT}/tcg/tcg_sh_vars.c
			${SRC_ROOT}/tcg/tcg_if_vars.c
			${SRC_ROOT}/tcg/tcg_if_nf_api.c
			${SRC_ROOT}/tcg/tcgnvme.c
			${SRC_ROOT}/tcg/tcg.c
			${SRC_ROOT}/tcg/tcgtbl.c
		)
		#add_library(cpu1libtcg ${TCG_LIB})
		set(TCG_OBJS ./tcg.c ./tcgnvme.c ./tcg_if_vars.c)
#	endif()

	add_library(libtcg ${TCG_SRCS})
	target_link_libraries(libtcg ${SRC_ROOT}/lib/liblib.a)
	add_library(tcg OBJECT ${TCG_OBJS})

	if (COMMAND add_pc_lint)
		add_pc_lint(tcg ${TCG_OBJS} ${TCG_SRCS})
	endif(COMMAND add_pc_lint)
endif()	

if (CPU_ID EQUAL 4)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTCG_SUPPORT=1")
#	if (NOT(_TCG_ STREQUAL "NONE"))
		set(TCGNF_SRCS
			${SRC_ROOT}/tcg/tcg_nf_mid.c
		)
		#add_library(cpu1libtcg ${TCG_LIB})
		set(TCGNF_OBJS ./tcg_nf_mid.c)
#	endif()

	add_library(libtcgnf ${TCGNF_SRCS})
	target_link_libraries(libtcgnf ${SRC_ROOT}/lib/liblib.a)
	add_library(tcgnf OBJECT ${TCGNF_OBJS})

	if (COMMAND add_pc_lint)
		add_pc_lint(tcgnf ${TCGNF_OBJS} ${TCGNF_SRCS})
	endif(COMMAND add_pc_lint)
endif()

#if (CPU_ID EQUAL 4)
#	if (NOT(_TCG_ STREQUAL "NONE"))
#		set(TCG_OBJS
#			${SRC_ROOT}/tcg/tcg_sh_vars.c
#		)
#		#add_library(cpu4libtcg ${TCG_LIB})
#	endif()
#endif()	


