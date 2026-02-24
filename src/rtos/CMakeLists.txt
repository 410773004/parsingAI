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

include_directories(${COMMON_INC}
	${SRC_ROOT}/btn/export
	${SRC_ROOT}/srb
	${SRC_ROOT}/utils/dfi
	${SRC_ROOT}/btn/inc
	${SRC_ROOT}/nvme/inc
	 ${SRC_ROOT}/nvme/apl/decoder
	${SRC_ROOT}/dispatcher
	${SRC_ROOT}/fcore/export
	${SRC_ROOT}/ftl/export
	${SRC_ROOT}/ncl_20
	${SRC_ROOT}/ncl_20/inc
)

set(ASM_SRCS
	${SRC_ROOT}/rtos/armv7r/task.s
	${SRC_ROOT}/rtos/armv7r/hiber.S
	${SRC_ROOT}/rtos/armv7r/startup.S
	${SRC_ROOT}/rtos/armv7r/bitops.S
	)
set_property(SOURCE ${ASM_SRCS} PROPERTY LANGUAGE C)

set(RTOS_SRC_OBJS
	${SRC_ROOT}/rtos/src/task.c
	${SRC_ROOT}/rtos/src/stdio.c
	${SRC_ROOT}/rtos/src/heap.c
	${SRC_ROOT}/rtos/src/console.c
	${SRC_ROOT}/rtos/src/pool.c
	${SRC_ROOT}/rtos/src/fsm.c
	${SRC_ROOT}/rtos/src/event.c
	${SRC_ROOT}/rtos/src/string.c
	${SRC_ROOT}/rtos/src/crc16.c
	${SRC_ROOT}/rtos/src/crc32.c
	${SRC_ROOT}/rtos/src/stat_head.c
	${SRC_ROOT}/rtos/src/xmodem.c
	${ASM_SRCS}
	)

if (MPC)
	set(RTOS_SRC_OBJS ${RTOS_SRC_OBJS}
		${SRC_ROOT}/rtos/armv7r/cpu_msg.c
		${SRC_ROOT}/rtos/armv7r/spin_lock.c
	)
endif()

set(RTOS_OBJS ${SRC_ROOT}/rtos/src/rtos.c
	${SRC_ROOT}/rtos/armv7r/uart.c
	${SRC_ROOT}/rtos/armv7r/misc.c
	${SRC_ROOT}/rtos/armv7r/irq.c
	${SRC_ROOT}/rtos/armv7r/mpu.c
	${SRC_ROOT}/rtos/armv7r/pic.c
	${SRC_ROOT}/rtos/armv7r/pmu.c
	${SRC_ROOT}/rtos/armv7r/smbus.c
	${SRC_ROOT}/rtos/armv7r/smbus_slave.c
	${SRC_ROOT}/rtos/armv7r/ddr.c
	${SRC_ROOT}/rtos/armv7r/l2cache.c
	${SRC_ROOT}/rtos/armv7r/thermal.c
	${SRC_ROOT}/rtos/armv7r/regulator.c
	${SRC_ROOT}/rtos/armv7r/pcie.c
	${SRC_ROOT}/rtos/armv7r/bucket.c
	${SRC_ROOT}/rtos/armv7r/trng.c
	)

if(HAVE_SYSTEMC OR HAVE_DISABLE_UDELAY)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_DISABLE_UDELAY=1")
endif()

add_library(librtos ${RTOS_SRC_OBJS})
add_library(rtos OBJECT ${RTOS_OBJS})

if (COMMAND add_pc_lint)
	add_pc_lint(rtos ${RTOS_OBJS} ${RTOS_SRC_OBJS})
endif(COMMAND add_pc_lint)
