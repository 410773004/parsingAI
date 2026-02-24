//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------
/*! \file module.h
 * @brief module header description
 *
 * \addtogroup module_group
 * \defgroup module
 * \ingroup module_group
 * @{
 */

#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
#define MC_DDR4   0x0//!< MC DDR4 TYPE
#define	MC_DDR3   0x1//!< MC DDR3 TYPE
#define	MC_LPDDR3 0x9//!< MC LPDDR3 TYPE
#define	MC_LPDDR4 0xa//!< MC LPDDR4 TYPE

/*!
 * @brief MC (Memory Controller) DRAM Bus Width
 */
#define	MC_BUS_WIDTH_8  0x1	//!< MC bus width x8 (reserved)
#define	MC_BUS_WIDTH_16 0x2	//!< MC bus width x16
#define	MC_BUS_WIDTH_32 0x3	//!< MC bus width x32
#define	MC_BUS_WIDTH_64 0x4	//!< MC bus width x64 (reserved)

/*!
 * @brief DDR CAPACITY be judged by ddr_capacity   20200822-Eddie
 */
#define DDR_1GB		1024
#define DDR_2GB		2048
#define DDR_4GB		4096
#define DDR_8GB		8192
/*!
 * @brief MC (Memory Controller) DRAM Row Address Bit
 */
#define	MC_MAX_ROW_ADDR 18	//!< MC max row address
#define	MC_ROW_ADDR(x) (x-11)	//!< x is 1 base

/*!
 * @brief MC (Memory Controller) DRAM Column Address Bit
 */
#define	MC_MAX_COL_ADDR 12	//!< MC max row address
#define	MC_COL_ADDR(x) (x-6)	//!< x is 1 base

/*!
 * @brief MC (Memory Controller) DRAM Bank Number
 */
#define	MC_MAX_BANK_NUM  8		//!< MC max bank num
#define	MC_BANK_NUM_4 	0		//!< MC bank number 4
#define	MC_BANK_NUM_8 	1		//!< MC bank number 8

/*!
 * @brief MC (Memory Controller) DRAM Bank Group Number
 */
#define	MC_BANK_GP_NUM_0 0x0		//!< MC bank group number 2
#define	MC_BANK_GP_NUM_2 0x1		//!< MC bank group number 2
#define	MC_BANK_GP_NUM_4 0x2		//!< MC bank group number 4

#define	MC_MAX_CS_NUM  2		//!< MC cs num

/*!
 * @brief MC (Memory Controller) DRAM Area Length
 */
#define	MC_AREA_LEN_256M 0x10	//!< MC bank area length 512M
#define	MC_AREA_LEN_512M 0x11	//!< MC bank area length 512M
#define	MC_AREA_LEN_1G   0x12	//!< MC bank area length 1G
#define	MC_AREA_LEN_2G   0x13	//!< MC bank area length 2G
#define	MC_AREA_LEN_4G   0x14	//!< MC bank area length 4G
#define	MC_AREA_LEN_8G   0x15	//!< MC bank area length 8G
#define	MC_AREA_LEN_16G  0x16	//!< MC bank area length 16G

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
#define MR_READ		0
#define MR_WRITE	1

#define MR1		1
#define MR2		2
#define MR3		3
#define MR6		6
#define MR11		11
#define MR12		12
#define MR13		13
#define MR14		14

#define CS0_BIT		0x1
#define CS1_BIT		0x2
#define CS2_BIT		0x4
#define CS3_BIT		0x8
#define CS_ALL_BITS	0xF

#if !defined(DDR_SIZE_RAINIER)
#ifdef FPGA
	#define DDR_SIZE_RAINIER	0x40000000		///< rainier ddr size 1GB with fpga
#elif (EVB_0501)
	#define DDR_SIZE_RAINIER	0x100000000		///< rainier ddr size 4GB with EVB
#elif (M2)
	#define DDR_SIZE_RAINIER	0x40000000		///< rainier ddr size 1GB with M.2
#elif (U2)
	#define DDR_SIZE_RAINIER	0x100000000		///< rainier ddr size 4GB with U.2
#endif
#endif

//MC init settings
//------------------Common settings for all DDR configurations-----------------
#define DDR_START                   0x40000000
#define DATA_WIDTH                  MC_BUS_WIDTH_64
#if (DDR_HALF)
#define DATA_WIDTH_RAINIER          MC_BUS_WIDTH_16
#else
#define DATA_WIDTH_RAINIER          MC_BUS_WIDTH_32
#endif
#define DATA_MASK_ENABLE            0
#define READ_DBI_ENABLE             1
#define WRITE_DBI_ENABLE            1
//-------------------------------LPDDR4----------------------------------------
#if defined(LPDDR4)
	// Currently settings are tuned for M.2
	#define DRAM_TYPE_MC                MC_LPDDR4 //In MC, 0x0: DDR4, 0x1: DDR3, 0xA: LPDDR4
	#define DDR_CS_NUM                  2 //number of chip select
	static const u8 CHIP_ID_NUM[4] = { 0, 0, 0, 0 }; //For DDR4 3DS only
	static const u8 AREA_LENGTH_RAINIER[4] = { 0x12, 0x12, 0x12, 0x12 };
	static const u8 AREA_LENGTH[4] = { 0x13, 0x13, 0x13, 0x13 };
	static const u8 ROW_ADDR_WIDTH_NUM[4] = { 0x5, 0x5, 0x5, 0x5 };
	//column_addr_width_num, TBM, 0x0:6, 0x1:7, 0x2:8, 0x3:9, 0x4:10, 0x5:11, 0x6:12
	static const u8 COLUMN_ADDR_WIDTH_NUM[4] = { 0x4, 0x4, 0x4, 0x4 };
	static const u8 BANK_GROUP_NUM[4] = { 0x0, 0x0, 0x0, 0x0 };
	static const u8 BANK_NUM[4] = { 0x1, 0x1, 0x1, 0x1 }; //bank_num, 0x0:4, 0x1:8
	static const u8 DEVICE_DIE_DATA_WIDTH[4] = { 2, 2, 2, 2 }; //device_die_data_width, 1:x8, 2:x16, 3:x32
	static const u8 START_ADDR_LOW[4] = { 0, 0x10, 0x20, 0x30 }; //0x10: 1GB, 0x20: 2GB, 0x30: 3GB
	static const u8 START_ADDR_HIGH[4] = { 0, 0, 0, 0 }; // Keep 0, if dapacity less than 4GB
	static const u8 ODT_VALUE_WRITE[4] = { 2, 2, 2, 2 }; //Dynamic ODT value, 0x1 -> 120ohm, 0x2 -> 240ohm, ox4 -> 80ohm, ox3 -> High-Z
	static const u8 ODTD_CA[4] = { 0, 0, 0, 0 }; //CA ODT Termination Disable (LP4)
	static const u8 ODT_VALUE_PARK[4] = { 1, 1, 1, 1 }; //DRAM ODT park value, not used
	static const u8 ODT_VALUE[4] = { 3, 3, 3, 3 }; //DRAM ODT RTT_NOM in MR1(DDR34), 2 -> 120ohm, 3 -> 40ohm / DQ ODT in MR11(LPDDR4), applied when ODT pin toggles.
	static const u8 ODTE_CS[4] = { 0, 0, 0, 0 }; //CS ODT Enable for Non-Terminating Rank (LP4)
	static const u8 ODTE_CK[4] = { 0, 0, 0, 0 }; //CK ODT Enable for Non-Terminating Rank (LP4)
	#define BURST_LENGTH                2 //BL16
	#define TPHY_WRDATA                 0
	#define ODT_TERMINATION_ENB         0 //Automatic ODT Enable, One hot encoding. Each bit corresponds to an ODT pad.
	#define FORCE_ODT                   0xf
	#define DRAM_DLL_DISABLE            0 //Don't care, LPDDR4 has no DLL

	#define CAS_WRITE_LATENCY_3200      14
	#define CAS_WRITE_LATENCY_2666      14
	#define CAS_WRITE_LATENCY_2400      12
	#define CAS_WRITE_LATENCY_2133      10
	#define CAS_WRITE_LATENCY_2000      10
	#define CAS_WRITE_LATENCY_1866      10
	#define CAS_WRITE_LATENCY_1600      8
	#define CAS_WRITE_LATENCY_800       8
	#define CAS_WRITE_LATENCY_LOW       8

	#define CAS_LATENCY_3200            28
	#define CAS_LATENCY_2666            28
	#define CAS_LATENCY_2400            24
	#define CAS_LATENCY_2133            20
	#define CAS_LATENCY_2000            20
	#define CAS_LATENCY_1866            20
	#define CAS_LATENCY_1600            20
	#define CAS_LATENCY_800             14
	#define CAS_LATENCY_LOW             14

	#define NWR_3200                    30
	#define NWR_2666                    30
	#define NWR_2400                    24
	#define NWR_2133                    20
	#define NWR_2000                    20
	#define NWR_1866                    20
	#define NWR_1600                    20
	#define NWR_800                     16
	#define NWR_LOW                     16
	#define WR_POSTAMBLE                0 //LPDDR4->0x0:0.5nCK, 0x1:1.5nCK
	#define WR_PREAMBLE                 1 //DDR4->0x0:1nCK, 0x1:2nCK, LPDDR4->0x0:NA, 0x1:2nCK
	#define RD_POSTAMBLE                0 //LPDDR4->0x0:0.5nCK, 0x1:1.5nCK
	#define RD_PREAMBLE                 0 //DDR4->0x0:1nCK, 0x1:2nCK, LPDDR4->0x0:Static, 0x1:Toggle

	// Settings based on 2400MT/s
	#define DDR_INIT_DRAM_TIMING_INIT0      0x0783A980
	#define DDR_INIT_DRAM_TIMING_INIT1      0x802C800C
	#define DDR_INIT_DRAM_TIMING_INIT2      0x00960005
	#define DDR_INIT_DRAM_TIMING_INIT3      0x004B00A5
	#define DDR_INIT_DRAM_TIMING_INIT4      0x003C0024
	#define DDR_INIT_DRAM_TIMING_CORE0      0x4D16330C
	#define DDR_INIT_DRAM_TIMING_CORE1      0x60B1A408
	#define DDR_INIT_DRAM_TIMING_CORE2      0x5C0C02C9
	#define DDR_INIT_DRAM_TIMING_CORE3      0x11080C00
	#define DDR_INIT_DRAM_TIMING_REF0       0x0062009C
	#define DDR_INIT_DRAM_TIMING_SR0        0x00000009
	#define DDR_INIT_DRAM_TIMING_SR1        0x1200012C
	#define DDR_INIT_DRAM_TIMING_PD         0x00030009
	#define DDR_INIT_DRAM_TIMING_OFFSPEC    0x00F1520C

	// Settings based on 3200MT/s
	#define DDR_INIT_DRAM_TIMING_INIT0_RAINIER      0x0A04E200
	#define DDR_INIT_DRAM_TIMING_INIT1_RAINIER      0xAAE60010
	#define DDR_INIT_DRAM_TIMING_INIT2_RAINIER      0x00C80005
	#define DDR_INIT_DRAM_TIMING_INIT3_RAINIER      0x006400DC
	#define DDR_INIT_DRAM_TIMING_INIT4_RAINIER      0x00500030
	#define DDR_INIT_DRAM_TIMING_CORE0_RAINIER      0x661D4410
	#define DDR_INIT_DRAM_TIMING_CORE1_RAINIER      0x80EA2408
	#define DDR_INIT_DRAM_TIMING_CORE2_RAINIER      0x701003AC
	#define DDR_INIT_DRAM_TIMING_CORE3_RAINIER      0x17081000
	#define DDR_INIT_DRAM_TIMING_REF0_RAINIER       0x006200D0
	#define DDR_INIT_DRAM_TIMING_SR0_RAINIER        0x0000000C
	#define DDR_INIT_DRAM_TIMING_SR1_RAINIER        0x18000190
	#define DDR_INIT_DRAM_TIMING_PD_RAINIER         0x0003000C
	#define DDR_INIT_DRAM_TIMING_OFFSPEC_RAINIER    0x0131610C
	#define DDR_INIT_DRAM_TIMING_MISC		0x00000400

#else
//-------------------------------DDR4 (Default)--------------------------------
	#define DRAM_TYPE_MC                MC_DDR4 //In MC, 0x0: DDR4, 0x1: DDR3, 0xA: LPDDR4
		#if defined(M2)
			static const u8 AREA_LENGTH_RAINIER[4] = { 0x15, 0x15, 0x15, 0x15 };
			static const u8 BANK_GROUP_NUM[4] = { 0x1, 0x1, 0x1, 0x1 }; //bank_group_num, 0x0:1, 0x1:2, 0x2:4
			static const u8 ODT_VALUE_WRITE[4] = { 4, 4, 4, 4 }; //Dynamic ODT value, 0x1 -> 120ohm, 0x2 -> 240ohm, ox4 -> 80ohm, ox3 -> High-Z
		#elif defined(U2)
        #if (DDR_SIZE_RAINIER == 0x100000000)
			static const u8 AREA_LENGTH_RAINIER[4] = { 0x14, 0x14, 0x14, 0x14 };
        #else
            static const u8 AREA_LENGTH_RAINIER[4] = { 0x15, 0x15, 0x15, 0x15 };
        #endif
			static const u8 BANK_GROUP_NUM[4] = { 0x2, 0x2, 0x2, 0x2 }; //bank_group_num, 0x0:1, 0x1:2, 0x2:4
			static const u8 ODT_VALUE_WRITE[4] = { 4, 4, 4, 4 }; //Dynamic ODT value, 0x1 -> 120ohm, 0x2 -> 240ohm, ox4 -> 80ohm, ox3 -> High-Z
		#elif defined(EVB_0501)
			static const u8 AREA_LENGTH_RAINIER[4] = { 0x14, 0x14, 0x14, 0x14 };
			static const u8 BANK_GROUP_NUM[4] = { 0x2, 0x2, 0x2, 0x2 }; //bank_group_num, 0x0:1, 0x1:2, 0x2:4
			static const u8 ODT_VALUE_WRITE[4] = { 2, 2, 2, 2 }; //Dynamic ODT value, 0x1 -> 120ohm, 0x2 -> 240ohm, ox4 -> 80ohm, ox3 -> High-Z
		#endif
	#define DDR_CS_NUM                 1 //number of chip select
	static const u8 AREA_LENGTH[4] = { 0x15, 0x15, 0x15, 0x15 };
	static const u8 CHIP_ID_NUM[4] = { 0, 0, 0, 0 }; //For DDR4 3DS only
	#if (DDR_SIZE_RAINIER == 0x100000000)
	//row_addr_width_num, TBM, 0x0:11, 0x1:12, 0x2:13, 0x3:14, 0x4:15, 0x5:16, 0x6:17, 0x7:18
	static const u8 ROW_ADDR_WIDTH_NUM[4] = { 0x5, 0x5, 0x5, 0x5 };
    #else
    static const u8 ROW_ADDR_WIDTH_NUM[4] = { 0x6, 0x6, 0x6, 0x6 };
    #endif

	//column_addr_width_num, TBM, 0x0:6, 0x1:7, 0x2:8, 0x3:9, 0x4:10, 0x5:11, 0x6:12
	static const u8 COLUMN_ADDR_WIDTH_NUM[4] = { 0x4, 0x4, 0x4, 0x4 };
	static const u8 BANK_NUM[4] = { 0x0, 0x0, 0x0, 0x0 }; //bank_num, 0x0:4, 0x1:8
	static const u8 DEVICE_DIE_DATA_WIDTH[4] = { 1, 1, 1, 1 }; //device_die_data_width, 1:x8, 2:x16, 3:x32
	static const u8 START_ADDR_LOW[4] = { 0, 0, 0, 0 };
    #if (DDR_CS_NUM != 1)
	static const u8 START_ADDR_HIGH[4] = { 0, 1, 0, 0 };
    #else 
    static const u8 START_ADDR_HIGH[4] = { 0, 0, 0, 0 };
     #endif
	static const u8 ODTD_CA[4] = { 0, 0, 0, 0 }; //CA ODT Termination Disable (LP4)
	static const u8 ODT_VALUE_PARK[4] = { 0, 0, 0, 0 }; //DRAM ODT park value, not used
	static const u8 ODT_VALUE[4] = { 3, 3, 3, 3 }; //DRAM ODT RTT_NOM in MR1(DDR34), 2 -> 120ohm, 3 -> 40ohm / DQ ODT in MR11(LPDDR4), applied when ODT pin toggles.
	static const u8 ODTE_CS[4] = { 0, 0, 0, 0 }; //CS ODT Enable for Non-Terminating Rank (LP4)
	static const u8 ODTE_CK[4] = { 0, 0, 0, 0 }; //CK ODT Enable for Non-Terminating Rank (LP4)

	#define TPHY_WRDATA                     0
	#define ODT_TERMINATION_ENB             1 //Automatic ODT Enable, One hot encoding. Each bit corresponds to an ODT pad.
	#define DRAM_DLL_DISABLE                0 //ONLY FPGA REQUIRES DLL DISABLE, TBM

	#define BURST_LENGTH                1 //BL8
	//For CRUCIAL DIMM, CWL need set to 12 for 2400 and lower speed.
	//CWL need set to 14, only if speed is above 2666.
	#define CAS_WRITE_LATENCY_3200      16
	#define CAS_WRITE_LATENCY_2666      14
	#define CAS_WRITE_LATENCY_2400      12
	#define CAS_WRITE_LATENCY_2133      11
	#define CAS_WRITE_LATENCY_2000      11
	#define CAS_WRITE_LATENCY_1866      10
	#define CAS_WRITE_LATENCY_1600      9
	#define CAS_WRITE_LATENCY_800       9
	#define CAS_WRITE_LATENCY_LOW       9
	#define CAS_WRITE_LATENCY_DLLOFF    9

	#define CAS_LATENCY_3200            24
	#define CAS_LATENCY_2666            20
	#define CAS_LATENCY_2400            18
	#define CAS_LATENCY_2133            16
	#define CAS_LATENCY_2000            16
	#define CAS_LATENCY_1866            14
	#define CAS_LATENCY_1600            12
	#define CAS_LATENCY_800             12
	#define CAS_LATENCY_LOW             12
	#define CAS_LATENCY_DLLOFF          10

	#define NWR_3200                    30
	#define NWR_2666                    20
	#define NWR_2400                    18
	#define NWR_2133                    16
	#define NWR_2000                    15
	#define NWR_1866                    14
	#define NWR_1600                    12
	#define NWR_800                     12
	#define NWR_LOW                     12
	#define NWR_DLLOFF                  12
	#define WR_POSTAMBLE                0 //LPDDR4->0x0:0.5nCK, 0x1:1.5nCK
	#define WR_PREAMBLE                 0 //DDR4->0x0:1nCK, 0x1:2nCK, LPDDR4->0x0:NA, 0x1:2nCK
	#define RD_POSTAMBLE                0 //LPDDR4->0x0:0.5nCK, 0x1:1.5nCK
	//#define RD_PREAMBLE                 0 //DDR4->0x0:1nCK, 0x1:2nCK, LPDDR4->0x0:Static, 0x1:Toggle
	#define RD_PREAMBLE                 1 //DDR4->0x0:1nCK, 0x1:2nCK, LPDDR4->0x0:Static, 0x1:Toggle
	
	#define DDR_INIT_DRAM_TIMING_INIT0		0x7ff3A980
	#define DDR_INIT_DRAM_TIMING_INIT1		0x0927C00C
	#define DDR_INIT_DRAM_TIMING_INIT2		0x0000000C
	#define DDR_INIT_DRAM_TIMING_INIT3		0x00400300
	#define DDR_INIT_DRAM_TIMING_INIT4		0x02000080
	#define DDR_INIT_DRAM_TIMING_CORE0		0x37102706
	#define DDR_INIT_DRAM_TIMING_CORE1		0x68810048
	#define DDR_INIT_DRAM_TIMING_CORE2		0x03092249
	#define DDR_INIT_DRAM_TIMING_CORE3		0x08000018
	#define DDR_INIT_DRAM_TIMING_REF0 		0x00C301A4
	#define DDR_INIT_DRAM_TIMING_SR0		0x0C0C1B06
	#define DDR_INIT_DRAM_TIMING_SR1		0x00000000
	#define DDR_INIT_DRAM_TIMING_PD			0x10F20048
	#define DDR_INIT_DRAM_TIMING_OFFSPEC		0x009085D7
	#define DDR_INIT_DRAM_TIMING_MISC 		0x00700000
#if defined(U2_LJ)
	#if (DDR_SIZE_RAINIER == 0x100000000)
		#define DDR_INIT_DRAM_TIMING_INIT0_RAINIER	0x6404E200
		#define DDR_INIT_DRAM_TIMING_INIT1_RAINIER	0x0C350010
		#define DDR_INIT_DRAM_TIMING_INIT2_RAINIER	0x00000010
		#define DDR_INIT_DRAM_TIMING_INIT3_RAINIER	0x00400400
		#define DDR_INIT_DRAM_TIMING_INIT4_RAINIER	0x02000080
		#define DDR_INIT_DRAM_TIMING_CORE0_RAINIER	0x4C18340B
		#define DDR_INIT_DRAM_TIMING_CORE1_RAINIER	0x60C18048
		#define DDR_INIT_DRAM_TIMING_CORE2_RAINIER	0x010C4B0C
		#define DDR_INIT_DRAM_TIMING_CORE3_RAINIER	0x08000018
		#define DDR_INIT_DRAM_TIMING_REF0_RAINIER	0x00C30230 // tRFC:350ns for 8Gb device
		#define DDR_INIT_DRAM_TIMING_SR0_RAINIER	0x10102408
		#define DDR_INIT_DRAM_TIMING_SR1_RAINIER	0x00000000
		#define DDR_INIT_DRAM_TIMING_PD_RAINIER		0x2142004A
		#define DDR_INIT_DRAM_TIMING_OFFSPEC_RAINIER	0x00c0838e
	#elif (DDR_SIZE_RAINIER == 0x200000000)
		#define DDR_INIT_DRAM_TIMING_INIT0_RAINIER	0x6404E200
		#define DDR_INIT_DRAM_TIMING_INIT1_RAINIER	0x0C350010
		#define DDR_INIT_DRAM_TIMING_INIT2_RAINIER	0x00000010
		#define DDR_INIT_DRAM_TIMING_INIT3_RAINIER	0x00400400
		#define DDR_INIT_DRAM_TIMING_INIT4_RAINIER	0x02000080
		#define DDR_INIT_DRAM_TIMING_CORE0_RAINIER	0x4C18340B
		#define DDR_INIT_DRAM_TIMING_CORE1_RAINIER	0x60C18048
		#define DDR_INIT_DRAM_TIMING_CORE2_RAINIER	0x010C4B0C
		#define DDR_INIT_DRAM_TIMING_CORE3_RAINIER	0x08000018
		#define DDR_INIT_DRAM_TIMING_REF0_RAINIER	0x00C30730 // tRFC:550ns for 16Gb device
		#define DDR_INIT_DRAM_TIMING_SR0_RAINIER	0x10102408
		#define DDR_INIT_DRAM_TIMING_SR1_RAINIER	0x00000000
		#define DDR_INIT_DRAM_TIMING_PD_RAINIER		0x2142004A
		#define DDR_INIT_DRAM_TIMING_OFFSPEC_RAINIER	0x00c0838e
	#endif

#else
	#define DDR_INIT_DRAM_TIMING_INIT0_RAINIER	0x08641168
	#define DDR_INIT_DRAM_TIMING_INIT1_RAINIER	0x0A2B840E
	#define DDR_INIT_DRAM_TIMING_INIT2_RAINIER	0x0000000E
	#define DDR_INIT_DRAM_TIMING_INIT3_RAINIER	0x00400356
	#define DDR_INIT_DRAM_TIMING_INIT4_RAINIER	0x02000080
	#define DDR_INIT_DRAM_TIMING_CORE0_RAINIER	0x3D122B07
	#define DDR_INIT_DRAM_TIMING_CORE1_RAINIER	0x70912048
	#define DDR_INIT_DRAM_TIMING_CORE2_RAINIER	0x040A228A
	#define DDR_INIT_DRAM_TIMING_CORE3_RAINIER	0x08000018
	#define DDR_INIT_DRAM_TIMING_REF0_RAINIER	0x00C301D3
	#define DDR_INIT_DRAM_TIMING_SR0_RAINIER	0x0E0E1E07
	#define DDR_INIT_DRAM_TIMING_SR1_RAINIER	0x00000000
	#define DDR_INIT_DRAM_TIMING_PD_RAINIER		0x11020048
	#define DDR_INIT_DRAM_TIMING_OFFSPEC_RAINIER	0x00A08659
#endif
#endif
/*! @} */
