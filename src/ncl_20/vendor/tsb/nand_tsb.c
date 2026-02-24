//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2018 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

//=============================================================================
//
/*! \file nand_tsb.c
 * @brief nand toshiba
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * Toshiba related nand check and nand configurations etc
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nand.h"
#include "eccu.h"
#include "ncl.h"
#include "ncl_cmd.h"
#include "ard.h"
#include "ndcu.h"
#include "eccu_reg_access.h"
#include "ncb_eccu_register.h"
#include "nand_define.h"
#include "srb.h"

/*! \cond PRIVATE */
#define __FILEID__ vendor
#include "trace.h"
/*! \endcond */
ddr_sh_data u32 dcc_training_fail_cnt;
#if NCL_HAVE_ARD
    #if defined(EH_ENABLE_2BIT_RETRY)
        #if defined(EH_ENABLE_1BIT_ADD_2BIT)

        norm_ps_data u8 ard_feature_addr[8] = { ///< ARD set feature address for vref shift
            0,
            NAND_FA_SLC_READ_RETRY,
            NAND_FA_LOW_READ_RETRY,
            NAND_FA_MID_READ_RETRY,
            NAND_FA_UPR_READ_RETRY,
            NAND_FA_TOP_READ_RETRY,
            NAND_FA_MAX_READ_RETRY,
            NAND_FA_SPR_READ_RETRY,
        };

        norm_ps_data u8 ard_feature_addr_p1[8] = {  ///< ARD set feature address for AIPR plane 1 vref shift
            0,
            NAND_FA_SLC_READ_RETRY_P1,
            NAND_FA_LOW_READ_RETRY_P1,
            NAND_FA_MID_READ_RETRY_P1,
            NAND_FA_UPR_READ_RETRY_P1,
            NAND_FA_TOP_READ_RETRY_P1,
            NAND_FA_MAX_READ_RETRY_P1,
            NAND_FA_SPR_READ_RETRY_P1,
        };

        norm_ps_data u8 ard_feature_addr_2bit_p[8] = {	///< ARD set feature address for postive soft decode of 2 bit
        	0,  //default
            NAND_FA_SLC_READ_RETRY_2BIT_P,  //slc
            0,  //lower of 1bit + 2bit
            0,  //middle of 1bit + 2bit
            0,  //upper of 1bit + 2bit
            NAND_FA_LOW_READ_RETRY_2BIT_P,
            NAND_FA_MID_READ_RETRY_2BIT_P,
            NAND_FA_UPR_READ_RETRY_2BIT_P,
        };

        norm_ps_data u8 ard_feature_addr_2bit_n[8] = {	///< ARD set feature address for negative soft decode of 2 bit
        	0,
            NAND_FA_SLC_READ_RETRY_2BIT_N,
            0,  //lower of 1bit + 2bit
            0,  //middle of 1bit + 2bit
            0,  //upper of 1bit + 2bit
            NAND_FA_LOW_READ_RETRY_2BIT_N,
            NAND_FA_MID_READ_RETRY_2BIT_N,
            NAND_FA_UPR_READ_RETRY_2BIT_N,
        };

        #else

        norm_ps_data u8 ard_feature_addr[ARD_TMPLT_TOP] = { ///< ARD set feature address for vref shift
            0,
            NAND_FA_SLC_READ_RETRY,
            NAND_FA_LOW_READ_RETRY,
            NAND_FA_MID_READ_RETRY,
            NAND_FA_UPR_READ_RETRY,
        };

        norm_ps_data u8 ard_feature_addr_p1[ARD_TMPLT_TOP] = {  ///< ARD set feature address for AIPR plane 1 vref shift
            0,
            NAND_FA_SLC_READ_RETRY_P1,
            NAND_FA_LOW_READ_RETRY_P1,
            NAND_FA_MID_READ_RETRY_P1,
            NAND_FA_UPR_READ_RETRY_P1,
        };
        norm_ps_data u8 ard_feature_addr_p2[ARD_TMPLT_TOP] = {  ///< ARD set feature address for AIPR plane 2 vref shift
            0,
            NAND_FA_SLC_READ_RETRY_P2,
            NAND_FA_LOW_READ_RETRY_P2,
            NAND_FA_MID_READ_RETRY_P2,
            NAND_FA_UPR_READ_RETRY_P2,
        };
        norm_ps_data u8 ard_feature_addr_p3[ARD_TMPLT_TOP] = {  ///< ARD set feature address for AIPR plane 3 vref shift
            0,
            NAND_FA_SLC_READ_RETRY_P3,
            NAND_FA_LOW_READ_RETRY_P3,
            NAND_FA_MID_READ_RETRY_P3,
            NAND_FA_UPR_READ_RETRY_P3,
        };

        norm_ps_data u8 ard_feature_addr_2bit_p[ARD_TMPLT_TOP] = {	///< ARD set feature address for postive soft decode of 2 bit
        	0,  //default
            NAND_FA_SLC_READ_RETRY_2BIT_P,  //slc
            NAND_FA_LOW_READ_RETRY_2BIT_P,
            NAND_FA_MID_READ_RETRY_2BIT_P,
            NAND_FA_UPR_READ_RETRY_2BIT_P,
        };

        norm_ps_data u8 ard_feature_addr_2bit_n[ARD_TMPLT_TOP] = {	///< ARD set feature address for negative soft decode of 2 bit
        	0,
            NAND_FA_SLC_READ_RETRY_2BIT_N,
            NAND_FA_LOW_READ_RETRY_2BIT_N,
            NAND_FA_MID_READ_RETRY_2BIT_N,
            NAND_FA_UPR_READ_RETRY_2BIT_N,
        };

        #endif
    #else

        norm_ps_data u8 ard_feature_addr[ARD_TMPLT_TOP] = { ///< ARD set feature address for vref shift
            0,
            NAND_FA_SLC_READ_RETRY,
            NAND_FA_LOW_READ_RETRY,
            NAND_FA_MID_READ_RETRY,
            NAND_FA_UPR_READ_RETRY,
        };

        norm_ps_data u8 ard_feature_addr_p1[ARD_TMPLT_TOP] = {  ///< ARD set feature address for AIPR plane 1 vref shift
            0,
            NAND_FA_SLC_READ_RETRY_P1,
            NAND_FA_LOW_READ_RETRY_P1,
            NAND_FA_MID_READ_RETRY_P1,
            NAND_FA_UPR_READ_RETRY_P1,
        };

    #endif

/*
#if !QLC_SUPPORT
norm_ps_data u8 ard_feature_addr_2bit_p_p1[ARD_TMPLT_SPR] = {	///< ARD set feature address for postive soft decode of 2 bit of AIPR plane 1
	0,
    0,
#if defined(EH_ENABLE_1BIT_ADD_2BIT)  //ARD set feature address for 1bit + 2bit of plane 1
    0,  //lower of 1bit + 2bit
    0,  //middle of 1bit + 2bit
    0,  //upper of 1bit + 2bit
#endif
    NAND_FA_LOW_READ_RETRY_2BIT_P_P1,
    NAND_FA_MID_READ_RETRY_2BIT_P_P1,
    NAND_FA_UPR_READ_RETRY_2BIT_P_P1,
};

norm_ps_data u8 ard_feature_addr_2bit_n_p1[ARD_TMPLT_SPR] = {	///< ARD set feature address for negative soft decode of 2 bit of AIPR plane 1
	0,
    0,
#if defined(EH_ENABLE_1BIT_ADD_2BIT)  //ARD set feature address for 1bit + 2bit of plane 1
    0,  //lower of 1bit + 2bit
    0,  //middle of 1bit + 2bit
    0,  //upper of 1bit + 2bit
#endif
    NAND_FA_LOW_READ_RETRY_2BIT_N_P1,
    NAND_FA_MID_READ_RETRY_2BIT_N_P1,
    NAND_FA_UPR_READ_RETRY_2BIT_N_P1,
};
#endif
*/
#endif

slow_data u8 Retry_feature_addr[4] = {
	NAND_FA_LOW_READ_RETRY,
	NAND_FA_MID_READ_RETRY,
	NAND_FA_UPR_READ_RETRY,
	NAND_FA_SLC_READ_RETRY,
};


//--------------------------------------------Soft bit level-----------------------------------------------//
#ifdef EH_ENABLE_2BIT_RETRY
norm_ps_data u8 tsb_soft_level_p[SOFT_RETRY_LEVEL] = {
    0x08,
    0x0A,
    0x07,
    0x0D,
};
norm_ps_data u8 tsb_soft_level_n[SOFT_RETRY_LEVEL] = {
    0xF8,
    0xF6,
    0xF3,
    0xF9,
};
#endif
//--------------------------------------------Hard bit level-----------------------------------------------//
#if !QLC_SUPPORT
    #ifdef TSB_BICS5 //eTLC //tony20200817
        norm_ps_data u8 tsb_slc_8b_rr_offset[RR_STEP_SLC] = {	///< ARD SLC vref shift values of feature 8Bh

            0x00,   //0
            0xF8,   //1
            0xE8,   //2
            0x08,   //2
            0x18,   //4
            0xD8,   //5
            0x28,   //6
            //0x20,   //7
            //0xE8,   //8
            //0x18,   //9
            //0xD8,   //10
            //0x28,   //11
            //0xC8,   //12
            //0x30,   //13
            //0xC0,   //14
            //0x38,   //15
            //0xB8,   //16
            //0x40,   //17
            //0xB0,   //18
            //0x48,   //19
            //0x50,   //20
            //0xA8,   //21
            //0x58,   //22
            //0xA0    //23

        };

        norm_ps_data u32 tsb_tlc_low_upr_89_rr_offset_256Gb[RR_STEP_XLC] = {  ///< ARD TLC(lower and upper pages) vref shift values of feature 89h

            0x00000000, //0 Default read  
            0xEBF2F8F8, //1
            0xEAF3F7F9, //2
            0xE8F2F6F9, //3
            0xEAF7FBFA, //4
            0xE9F7FBFB, //5
            0xE7F6FBFB, //6
            0xE5F5FAFB, //7
            0xEAF7FBFB, //8
            0xE7F6FBFC, //9
            0xE9F8FBFD, //10
            0xE5F6FBFF, //11
            0xE4F5FBFF, //12
            0xE9F8FCFC, //13
            0xE5F6FCFB, //14
            0xE6F1F5F8, //15
            0xF7FAFBFE, //16
            0xF7FCFD04, //17
            0xF6F4F7F8, //18
            0xEEF2F6F6, //19
            0xEDF0F4F6, //20
            0xE4E9F1F1, //21
            0xE5E4EEEE, //22
            0xFEFDFAFA, //23
            0xF4FAFDFB, //24
            0xF2F7F9FA, //25
            0xF5FAFCFB, //26
            0xFB0101FC, //27
            0xFA0000FB, //28
            0xF6FDFEFC, //29
            0xF9FFFFFB, //30
        };

        norm_ps_data u32 tsb_tlc_mid_8a_rr_offset_256Gb[RR_STEP_XLC] = {      ///< ARD TLC(middle pages) vref shift values of feature 8Ah

            0x00000000 ,//0 Default read 
            0x00EEF8FA ,//1 
            0x00EEF6FA ,//2
            0x00ECF7FA ,//3
            0x00ECF5FA ,//4
            0x00F0FAFC ,//5
            0x00EFFAFC ,//6
            0x00EDF9FC ,//7
            0x00F0FBFD ,//8
            0x00EEFAFD ,//9
            0x00F0FBFD ,//10
            0x00EDFAFD ,//11
            0x00EDFAFE ,//12
            0x00EFFEFE ,//13
            0x00ECFDFE ,//14
            0x00ECF3F7 ,//15
            0x00F8FBFC ,//16
            0x00F9FDFD ,//17
            0x00F5F4F6 ,//18
            0x00EFF3F2 ,//19
            0x00EDF1F2 ,//20
            0x00E7EDF3 ,//21
            0x00E5EAF0 ,//22
            0x00FFFBF7 ,//23
            0x00F5FDFC ,//24
            0x00F4FAF9 ,//25
            0x00F7FDFC ,//26
            0x00FE0200 ,//27
            0x00FC01FF ,//28
            0x00FA00FD ,//29
            0x00FB00FE ,//30
        };

        norm_ps_data u32 tsb_tlc_low_mid_89_rr_offset_512Gb[RR_STEP_XLC] = {  ///< ARD TLC(lower and middle pages) vref shift values of feature 89h

            0x00000000,  //0 Default read  1-24 Kioxia tbl
            0xF9FDFFFF,  //1
            0xFAFEFF01,  //2
            0xF8FEFE01,  //3
            0xF7FCFE01,  //4
            0xF6FBFE01,  //5
            0xF5FAFE01,  //6
            0xFAFE0001,  //7
            0xF8FEFE01,  //8
            0xF6FCFE01,  //9
            0xF3FAFE01,  //10
            0xF1F9FE01,  //11
            0xF8FEFE01,  //12
            0xF7FCFF01,  //13
            0xF6FAFF02,  //14
            0xF3FAFE02,  //15
            0xF1F9FE02,  //16
            0xF5F5F5F5,  //17
            0xF4F5F5F7,  //18
            0xF3F4F5F9,  //19
            0xF0F5F6F0,  //20
            0xECF4F5F6,  //21
            0xEAF4F5FA,  //22
            0xEFF4F5F4,  //23
            0xEAF4F5FA,  //24
            0xE7F3F4FA,  //25
            0xEAF4F5FA,  //26
            0xE8F3F4F4,  //27
            0xE5F2F2F8,  //28
            0x06030300,  //29
            0xFEFCFAF8,  //30

        };

        norm_ps_data u32 tsb_tlc_upr_8a_rr_offset_512Gb[RR_STEP_XLC] = {      ///< ARD TLC(upper pages) vref shift values of feature 8Ah

            0x00000000,  //0 Default read  1-24 Kioxia tbl
            0x00F7FC02,  //1
            0x00F9FC00,  //2
            0x00F6FB01,  //3
            0x00F4FA00,  //4
            0x00F2F9FF,  //5
            0x00F0F8FF,  //6
            0x00F8FC01,  //7
            0x00F6FB01,  //8
            0x00F3F900,  //9
            0x00F1F7FF,  //10
            0x00EFF6FF,  //11
            0x00F6FB01,  //12
            0x00F4FA02,  //13
            0x00F2F902,  //14
            0x00EEF7FF,  //15
            0x00ECF6FF,  //16
            0x00F7F4F7,  //17
            0x00F5F4F7,  //18
            0x00F3F4F6,  //19
            0x00F0F1F6,  //20
            0x00ECF0F6,  //21
            0x00E8EFF6,  //22
            0x00EEF1F6,  //23
            0x00E8EFF6,  //24
            0x00E4ECF6,  //25
            0x00E8EFF6,  //26
            0x00E6ECF4,  //27
            0x00E2E9F4,  //28
            0x00080505,  //29
            0x0000FDF9,  //30
        };

		#if 0
        norm_ps_data u32 tsb_tlc_low_upr_89_rr_offset[RR_STEP_XLC] = {  ///< ARD TLC(lower and upper pages) vref shift values of feature 89h

            0x00000000, //0 Default read
            0xF20CF2F8, //1
            0x06E80AED, //2
            0xE2F4EDE7, //3
            0xE8FFEF09, //4
            0xEBFBF10A, //5
            0x0A0C0CFF, //6
            0x00F509EE, //7
            0xF803F30A, //8
            0xDEE4EBEF, //9
            0xE3EDF4E5, //10
            0x04EE0FEC, //11
            0xDCFAF1E7, //12
            0xE5EAE8F3, //13
            0x080806F4, //14
            0xECFEED0D, //15
            0xE9FAF606, //16
            0xF3E2EDF4, //17
            0x0EF50AE1, //18
            0xEFE0F7ED, //19
            0x01060507, //20
            0xE9F2FBF8, //21
            0xE5F7FBFB, //22
            0xF5F7FEFF, //23
            0xF7FEFCFB, //24
            0xF601F7FC, //25
            0xFE00FA00, //26
            0xF300FB00, //27
            0xFA000000, //28
            0xF4FAFDFB, //29
            0xF3F8FBFB, //30
        };

        norm_ps_data u32 tsb_tlc_mid_8a_rr_offset[RR_STEP_XLC] = {      ///< ARD TLC(middle pages) vref shift values of feature 8Ah

            0x00000000 ,//0 Default read
            0x00080808 ,//1
            0x00E6EBF2 ,//2
            0x00F4F6F6 ,//3
            0x00F8F8F7 ,//4
            0x00EAEFF3 ,//5
            0x000C0A09 ,//6
            0x00040607 ,//7
            0x00F0F3F5 ,//8
            0x00E4E7EF ,//9
            0x00F7F2F6 ,//10
            0x00E5EFF4 ,//11
            0x00060B0A ,//12
            0x00F1F9F7 ,//13
            0x000A0407 ,//14
            0x00ECEBF3 ,//15
            0x00E6EDEE ,//16
            0x00E6EAF6 ,//17
            0x00070904 ,//18
            0x00F6FBF9 ,//19
            0x00F2F0F3 ,//20
            0x00EEF9FC ,//21
            0x00EFFBFD ,//22
            0x00F901FE ,//23
            0x00ECFDFE ,//24
            0x00ECF3F7 ,//25
            0x00FAFEFD ,//26
            0x00EEF1F2 ,//27
            0x00FFFBF7 ,//28
            0x00FD02FE ,//29
            0x00F4FCFB ,//30

        };
		#endif
            #ifdef EH_ENABLE_2BIT_RETRY
            norm_ps_data u32 tsb_tlc_2bit_89_rr_offset[RR_STEP_2BIT_XLC] = {

                //0x00000000, //0 test
				0xFEFC0C0B, //0
                0xE6E0F9FA, //1
                0xE001EC0C, //2
                0xE1E6F5FE, //3
                0x00F9090D, //4
                0xFFE90F03, //5
                0xE9DDFBF7, //6

                //0xDDE0F1FE, //7
                //0x05E30DF8, //8
                //0xE105E80B, //9
                //0x07FC040F, //10
                //0xFBEA0BFC, //11
                //0xE8E3F600, //12
                //0xE4DAF0FC, //13
                //0xE3E6FBFA, //14
                //0x06FF0809, //15
                //0xFF04060E, //16
                //0x0AF90309, //17
                //0xE6EDECFF, //18
                //0xE9ECFF06, //19

            };

            norm_ps_data u32 tsb_tlc_2bit_8a_rr_offset[RR_STEP_2BIT_XLC] = {

                //0x00000000, //0 test
				0x00E4E9F3, //0
                0x00EEF8F6, //1
                0x00EEFCF9, //2
                0x00FCFFF9, //3
                0x00F8FBF9, //4
                0x00E3E5F0, //5
                0x00030208, //6

                //0x00090208, //7
                //0x00050505, //8
                //0x00E5ECFB, //9
                //0x00080900, //10
                //0x00E7F0FF, //11
                //0x0006010B, //12
                //0x00000808, //13
                //0x00020104, //14
                //0x00060709, //15
                //0x000506FF, //16
                //0x00E6EDF2, //17
                //0x00070C05, //18
                //0x00FF0109, //19

            };
            #endif

	#else
	norm_ps_data u8 tsb_slc_8b_rr_offset[RR_STEP_SLC] = {	///< ARD SLC vref shift values of feature 8Bh
		0xF8,
		0x08,
		0xF0,
		0xE8,
		0x18,
		0xD8,
		0x28,
	};

	norm_ps_data u32 tsb_tlc_low_upr_89_rr_offset[RR_STEP_XLC] = {	///< ARD TLC(lower and upper pages) vref shift values of feature 89h
		// 12 settings copied from Shasta
		0xEAEFF1F4,
		0xEEEDF5F4,
		0xE4E7EBF6,
		0xE0E5E9F4,
		0xEEF0F6F4,
		0xECE8F4F4,
		0xE6E4EEF4,
		0x0C091316,
		0x0A0B1312,
		0x0A0D1514,
		0x080B1516,
		0x080C100C,

		0xFBFAF7F9, // 1st
		0x02020107, // 2nd
		0x00F4F4FC, // 3th
		0xFCF0F000, // 4th
		0xE0ECF800, // 5th
		0xE4E4FC00, // 6th
		0xDEF0F404, // 7th
		0x04F8F4FC, // 8th
		0xF8FC0004, // 9th
		0xDCE8F8FC, // 10th
		0xECE4FC04, // 11th
		0xECECE4FB, // 12th
		0xF4F8FC04, // 13th
		0xECEAE8F8, // 14th
		0xE0E6ECFB, // 15th
		0xF0E6F8F8, // 16th
		0xFC000408, // 17th
		0xFCFC000C, // 18th
		0xECDCECF8, // 19th
		0xE4E6ECF4, // 20th
		0xE8E0E8FB, // 21th
		0x0008F808, // 22th
		0xEEF4F400, // 23th
		0x00F40808, // 24th
		0xE400F40C, // 25th
		0xE4F0E8F8, // 26th
		0xE400F010, // 27th
		0xF8000404, // 28th
		0xE8F8F400, // 29th
		0xD400F008, // 30th
	};

	norm_ps_data u32 tsb_tlc_mid_8a_rr_offset[RR_STEP_XLC] = {		///< ARD TLC(middle pages) vref shift values of feature 8Ah
		// 12 settings copied from Shasta
		0x00F0F2F2,
		0x00EEF0F0,
		0x00E6E8EC,
		0x00EAECEC,
		0x00F0F2F6,
		0x00ECECF0,
		0x00EAEAEC,
		0x000E1014,
		0x000E1010,
		0x00101212,
		0x000C1212,
		0x000C100C,

		0x00FBF8F7, // 1st
		0x00080201, // 2nd
		0x00FCF4F8, // 3th
		0x00F4F800, // 4th
		0x00ECF000, // 5th
		0x00F0F0F8, // 6th
		0x00E0E8F8, // 7th
		0x0000FCF8, // 8th
		0x00FCFC00, // 9th
		0x0000F8F4, // 10th
		0x00ECF400, // 11th
		0x00E8E8E8, // 12th
		0x00F8F8FC, // 13th
		0x00ECE8F0, // 14th
		0x00ECE4EC, // 15th
		0x00ECECF8, // 16th
		0x00000004, // 17th
		0x00000008, // 18th
		0x00F0ECF1, // 19th
		0x00E4E6F5, // 20th
		0x00E8ECF5, // 21th
		0x0000F4F4, // 22th
		0x00F0F4FC, // 23th
		0x00ECE8EC, // 24th
		0x00E4ECFC, // 25th
		0x00F8F4F8, // 26th
		0x00080808, // 27th
		0x00FC00FC, // 28th
		0x00E4F0FC, // 29th
		0x00040404, // 30th
	};
	#endif
#else
norm_ps_data u8 tsb_slc_8b_rr_offset[RR_STEP_SLC] = {	///< ARD SLC vref shift values of feature 8Bh
	0xF0,
	0xE0,
	0xD0,
	0xC0,
	0x10,
	0x20,
	0x30,
	0x40,
};

norm_ps_data u32 tsb_qlc_low_87_rr_offset[RR_STEP_XLC] = {	///< ARD QLC(lower pages) vref shift values of feature 87h
	0x00040404, // 1st
	0xFE020200, // 2nd
	0x02FEFEFC, // 3th
	0xFB00FF00, // 4th
	0xFD00FF00, // 5th
	0xFD030300, // 6th
	0xFC030200, // 7th
	0xFB020200, // 8th
	0xFA010200, // 9th
	0xF9010100, // 10th
	0xF8000100, // 11th
	0xF9FFFF00, // 12th
	0xF8FFFE00, // 13th
	0xF7FEFE00, // 14th
	0xF6FDFE00, // 15th
	0xF5FDFD00, // 16th
	0xF8030200, // 17th
	0xF7030100, // 18th
	0xF6020100, // 19th
	0xF5010100, // 20th
	0xF4010000, // 21th
	0xFF0101FC, // 22th
	0xFE0100FC, // 23th
	0xFD0000FC, // 24th
	0xFCFF00FC, // 25th
	0xFBFFFFFC, // 26th
	0x00000004, // 27th
	0x00020208, // 28th
	0xFE04040C, // 29th
	0xFE060610, // 30th
};
norm_ps_data u32 tsb_qlc_mid_88_rr_offset[RR_STEP_XLC] = {	///< ARD QLC(middle pages) vref shift values of feature 88h
	0x00000204, // 1st
	0xFC000002, // 2nd
	0x040000FC, // 3th
	0xFDF7FC01, // 4th
	0xFEFBFE01, // 5th
	0xFBFE0105, // 6th
	0xF9FD0105, // 7th
	0xF8FC0004, // 8th
	0xF6FCFF04, // 9th
	0xF4FBFF03, // 10th
	0xF2FAFE03, // 11th
	0xF7FAFD01, // 12th
	0xF5F9FD01, // 13th
	0xF4F8FC00, // 14th
	0xF2F8FB00, // 15th
	0xF0F7FBFF, // 16th
	0xF8F5FD06, // 17th
	0xF6F4FD06, // 18th
	0xF5F3FC05, // 19th
	0xF3F3FB05, // 20th
	0xF1F2FB04, // 21th
	0xFFFE0101, // 22th
	0xFDFD0101, // 23th
	0xFCFC0000, // 24th
	0xFAFCFF00, // 25th
	0xF8FBFFFF, // 26th
	0x00000002, // 27th
	0xFE000004, // 28th
	0xFC000206, // 29th
	0xFC000408, // 30th
};
norm_ps_data u32 tsb_qlc_upr_89_rr_offset[RR_STEP_XLC] = {	///< ARD QLC(upper pages) vref shift values of feature 89h
	0x00000204, // 1st
	0x00FC0002, // 2nd
	0x000400FC, // 3th
	0x00FFFB02, // 4th
	0x00FFFD01, // 5th
	0x00FC0105, // 6th
	0x00FA0005, // 7th
	0x00F80004, // 8th
	0x00F6FF04, // 9th
	0x00F4FF03, // 10th
	0x00F2FE03, // 11th
	0x00F8FD01, // 12th
	0x00F6FC01, // 13th
	0x00F4FC00, // 14th
	0x00F2FB00, // 15th
	0x00F0FBFF, // 16th
	0x00FBFC07, // 17th
	0x00F9FB07, // 18th
	0x00F7FB06, // 19th
	0x00F5FA06, // 20th
	0x00F3FA05, // 21th
	0x00000101, // 22th
	0x00FE0001, // 23th
	0x00FC0000, // 24th
	0x00FAFF00, // 25th
	0x00F8FFFF, // 26th
	0x00000004, // 27th
	0x00FE0006, // 28th
	0x00FC0208, // 29th
	0x00FC040A, // 30th
};
norm_ps_data u32 tsb_qlc_top_8a_rr_offset[RR_STEP_XLC] = {	///< ARD QLC(top pages) vref shift values of feature 8Ah
	0x00000004, // 1st
	0xFCFEFE02, // 2nd
	0x040202FE, // 3th
	0xFFFCFA01, // 4th
	0xFFFEFD01, // 5th
	0xFAFD0004, // 6th
	0xF8FCFF04, // 7th
	0xF6FAFE03, // 8th
	0xF3F9FD03, // 9th
	0xF1F8FC02, // 10th
	0xEEF7FC01, // 11th
	0xF6F9FC00, // 12th
	0xF4F8FB00, // 13th
	0xF2F6FAFF, // 14th
	0xEFF5F9FF, // 15th
	0xEDF4F8FE, // 16th
	0xF9F9FA05, // 17th
	0xF7F8F905, // 18th
	0xF5F6F804, // 19th
	0xF2F5F704, // 20th
	0xF0F4F603, // 21th
	0xFEFF0202, // 22th
	0xFCFE0102, // 23th
	0xFAFC0001, // 24th
	0xF7FBFF01, // 25th
	0xF5FAFE00, // 26th
	0xFE000000, // 27th
	0x00000002, // 28th
	0xFEFE0004, // 29th
	0xFEFE0006, // 30th
};
#endif

/*!
 * @brief Get XLC page type from page index
 *
 * @param page_no	page index
 *
 * @return	low/middle/upper/top page type
 */
fast_code enum page_type_t get_xlc_page_type(u32 page_no)
{
#if QLC_SUPPORT
	return XLC_LOW_PAGE + (page_no & 3);
#else
	return XLC_LOW_PAGE + fast_mod3(page_no);
#endif
}

fast_code void tsb_parameter_set(u8 ch, u8 ce)
{
	u32 i;
	u32 val;
	ndcu_ind_t ctrl = {
		.write = true,
		.buf = (u8 *)&val,
	};

	for (i = 0; i < 9; i++) {
		switch(i) {
		case 0:
			ctrl.reg1.b.ind_byte0 = 0x5C;
			ctrl.cmd_num = 0;
			ctrl.xfcnt = 0;
			break;
		case 1:
			ctrl.reg1.b.ind_byte0 = 0xC5;
			ctrl.cmd_num = 0;
			ctrl.xfcnt = 0;
			break;
		case 2:
		case 7:
			ctrl.reg1.b.ind_byte0 = 0x55;
			ctrl.reg1.b.ind_byte1 = 0x00;
			ctrl.cmd_num = 1;
			ctrl.xfcnt = 1;
			if (i == 2) {
				val = 0x01;
			} else {
				val = 0x00;
			}
			break;
		case 3:
		case 5:
			ctrl.reg1.b.ind_byte0 = 0x55;
			ctrl.reg1.b.ind_byte1 = 0xFF;
			ctrl.cmd_num = 1;
			ctrl.xfcnt = 1;
			if (i == 3) {
				val = 0x01;
			} else {
				val = 0x00;
			}
			break;

		case 4:
			ctrl.reg1.b.ind_byte0 = 0x55;
			ctrl.reg1.b.ind_byte1 = 0x16;
			ctrl.cmd_num = 1;
			ctrl.xfcnt = 1;
			val = 0x40;
			break;
		case 6:
			ctrl.reg1.b.ind_byte0 = 0x55;
			ctrl.reg1.b.ind_byte1 = 0x01;
			ctrl.cmd_num = 1;
			ctrl.xfcnt = 1;
			val = 0x00;
			break;
		case 8:
			ctrl.reg1.b.ind_byte0 = 0xFF;
			ctrl.cmd_num = 0;
			ctrl.xfcnt = 0;
			break;
		}
		ndcu_open(&ctrl, ch, ce);
		ndcu_start(&ctrl);
		if (ctrl.xfcnt != 0) {
			do {
				if (ndcu_xfer(&ctrl))
					break;
			} while (1);
		}

		ndcu_close(&ctrl);
	}
}

fast_code u32 tsb_read_detected_step(u8 ch, u8 ce, u8 lun, u8 fa)
{
	u32 data;
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 0,
		.reg1.b.ind_byte0 = 0xF0+lun,
		.reg1.b.ind_byte1 = fa,
		.xfcnt = 0,
		.buf = (u8 *)&data,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_close(&ctrl);

	ctrl.reg1.b.ind_byte0 = 0xB0;
	ctrl.cmd_num = 1;
	ctrl.write = false;
	ctrl.xfcnt = sizeof(data);
	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);
	ncb_vendor_trace(LOG_ERR, 0x2449, "%d data %x\n", fa, data);
	return data;
}

/*!
 * @brief Check if nand is BiCS3
 *
 * @return	BiCS3 or not
 */
slow_code bool nand_is_bics3(void)
{
	return ((nand_info.id[5] & 0x7) == 2);
}

/*!
 * @brief Check if nand is BiCS4
 *
 * @return	BiCS4 or not
 */
slow_code bool nand_is_bics4(void)
{
	return ((nand_info.id[5] & 0x7) == 3);
}

/*!
 * @brief Check if nand is BiCS5
 *
 * @return	BiCS5 or not
 */
slow_code bool nand_is_bics5(void)
{
	return ((nand_info.id[5] & 0x7) == 4);
}

/*!
 * @brief Check if nand is Toshiba TH58TFT2T23BA8J
 *
 * @return	Toshiba BiCS4(TH58TFT2T23BA8J) or not
 */
slow_code bool nand_is_bics4_TH58TFT2T23BA8J(void)
{
	u8 id[6] = {0x98, 0x48, 0x99, 0xB3, 0x7A, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

/*!
 * @brief Check if nand is Toshiba BiCS4 (labeled 800MT/s on nand module, parameter page unavailable)
 *
 * @return	Toshiba BiCS4(800MT/s) or not
 */
slow_code bool nand_is_bics4_800mts(void)  // only for BiCS4 16DP 4CE/4LUN
{
	u8 id[6] = {0x98, 0x49, 0x9A, 0xB3, 0x7E, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

/*!
 * @brief Check if nand is Toshiba TH58LJT1T24BA8C
 *
 * @return	Toshiba BiCS4(TH58LJT1T24BA8C) or not
 */
slow_code bool nand_is_bics4_TH58LJT1T24BA8C(void)
{
	u8 id[6] = {0x98, 0x3E, 0x98, 0xB3, 0x76, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

/*!
 * @brief Check if nand is Toshiba TH58LJT1V24BA8H
 * BiCS4 from LiteOn, don't know why, 1.2V DLL calibration result worse than 1.8V @666MT/s
 *
 * @return	Toshiba BiCS4(TH58LJT1V24BA8H) or not
 */
slow_code bool nand_is_bics4_TH58LJT1V24BA8H(void)
{
	u8 id[6] = {0x98, 0x3E, 0x99, 0xB3, 0x7A, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

slow_code bool nand_is_bics4_TH58LJT1T24BAEF(void)
{
	u8 id[6] = {0x98, 0x3C, 0x98, 0xB3, 0x76, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

/*!
 * @brief Check if nand is Toshiba TSV
 *
 * @return	Toshiba TSV or not
 */
slow_code bool nand_is_tsb_tsv(void)
{
	u8 id[6] = {0x98, 0x19, 0xAA, 0xB3, 0x7E, 0xF1};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}
//_GENE_20200624
slow_code bool nand_is_bics4_16DP(void)
{
        u8 id[6] = {0x98, 0x48, 0x9A, 0xB3, 0x7E, 0xE3};
        if (memcmp(nand_info.id, id, 6) == 0) {
                return true;
        } else {
                return false;
        }
}

slow_code bool nand_is_bics4_HDR(void)
{
	u8 id[6] = {0x98, 0x3E, 0x99, 0xB3, 0xFA, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

/*!
 * @brief Check if nand is Toshiba TH58LJT2V24BB8N
 *
 * @return	Toshiba BiCS4(TH58LJT2V24BB8N) or not
 */
slow_code bool nand_is_bics4_TH58LJT2V24BB8N(void)
{
	u8 id[6] = {0x98, 0x48, 0x9A, 0xB3, 0x7E, 0xE3};

	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}

slow_code bool nand_is_bics5_TH58LKT1Y45BA8C(void)
{
	u8 id[6] = {0x98, 0x3E, 0xA8, 0x03, 0x7A, 0xE4};
	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}
slow_code bool nand_is_bics5_TH58LKT2Y45BA8H(void)
{
	u8 id[6] = {0x98, 0x48, 0xA9, 0x03, 0x7E, 0xE4};
	if (memcmp(nand_info.id, id, 6) == 0) {
		return true;
	} else {
		return false;
	}
}
/*!
 * @brief Nand specific initialization different from other vendors or other models
 *
 * @return	not used
 */
//fast_code void nand_specific_init(void)
slow_code void nand_specific_init(void)
{
#if XLC == 3
	u32 ch, ce;
	if (!nand_is_bics4()) {
		return;
	}
	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			if (!nand_target_exist(ch, ce)) {
				continue;
			}

			tsb_parameter_set(ch, ce);
		}
	}
#endif
}

/*!
 * @brief Some nand command sequence in order to run high speed (e.g. ZQ calibration)
 *
 * @return	not used
 */
init_code void nand_set_high_speed(void)
{
	u32 ch, ce;
	if ((nand_is_bics4())||(nand_is_bics5())) {
		// ZQ calibration long
		u32 lun;
		for (lun = 0; lun < nand_lun_num(); lun++) {
			for (ce = 0; ce < max_target; ce++) {
				for (ch = 0; ch < max_channel; ch++) {
					if (!nand_target_exist(ch, ce)) {
						continue;
					}

					ndcu_ind_t ctrl = {
						.write = true,
						.cmd_num = 1,
						.reg1.b.ind_byte0 = NAND_ZQ_CALIB_LONG,
						.reg1.b.ind_byte1 = lun,
						.xfcnt = 0,
					};
					ndcu_open(&ctrl, ch, ce);
					ndcu_start(&ctrl);
					ndcu_close(&ctrl);
				}
				ndcu_delay(1);// Maximum 1us
			}
		}
	}
}

/*!
 * @brief Refer to TSV spec TH58VHT3V42VA82_152BGA_20170515.pdf
 *
 * @return	not used
 */
init_code void nand_tsb_tsv_intf_cfg(void)
{
	u32 val, output;
	u32 ch, ce;

	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			val = 0x28;
			set_feature(ch, ce, NAND_FA_TSB_INTF_CONFIGURE, val);
			output = get_feature(ch, ce, NAND_FA_TSB_INTF_CONFIGURE);
			if (output != val) {
				sys_assert(0);
			}
		}
	}
}

/*!
 * @brief DCC write operation
 *
 * @param pda	PDA
 *
 * @return	not used
 */
slow_code void ncl_dcc_write(pda_t pda)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info;
	bm_pl_t bm_pl[DU_CNT_PER_PAGE];
	dtag_t dtag;
	u32* mem = 0;

	extern void tsb_dcc_write_change(void);
	extern void tsb_dcc_write_restore(void);
	extern struct finstr_format prog_fins_templ;
	prog_fins_templ.dw0.b.poll_dis = 1;
	tsb_dcc_write_change();
	dtag = dtag_get(DTAG_T_SRAM, (void*)&mem);
	if (mem == NULL) {
		sys_assert(0);
	}
	memset(mem, 0, NAND_DU_SIZE);

	u32 i;
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		bm_pl[i].all = 0;
		bm_pl[i].pl.dtag = dtag.b.dtag;
		bm_pl[i].pl.type_ctrl = DTAG_QID_DROP;
		bm_pl[i].pl.nvm_cmd_id = 0;
	}

	ncl_cmd.op_code = NCL_CMD_OP_WRITE;
	ncl_cmd.op_type = INT_TABLE_WRITE_PRE_ASSIGN;
	ncl_cmd.addr_param.common_param.list_len = 1;
	ncl_cmd.addr_param.common_param.pda_list = &pda;
	ncl_cmd.addr_param.common_param.info_list = &info;
	ncl_cmd.flags = NCL_CMD_SLC_PB_TYPE_FLAG;
	ncl_cmd.du_format_no = DU_FMT_RAW_4K;
	ncl_cmd.user_tag_list = bm_pl;
	ncl_cmd.completion = NULL;
	ncl_cmd_submit(&ncl_cmd);

	prog_fins_templ.dw0.b.poll_dis = 0;
	tsb_dcc_write_restore();
	dtag_put(DTAG_T_SRAM, dtag);
}

/*!
 * @brief DCC read  operation
 *
 * @param pda	PDA
 * @param cnt	DU count
 *
 * @return	not used
 */
slow_code void ncl_dcc_read_tsb(pda_t* pda_list, u32 cnt)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info_list[DU_CNT_PER_PAGE];
	u32 i;

	sys_assert(cnt <= DU_CNT_PER_PAGE);
	extern struct finstr_format read_fins_templ;
	read_fins_templ.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	read_fins_templ.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	read_fins_templ.dw2.b.ard_schem_sel = ARD_DISABLE;
	read_fins_templ.dw2.b.host_trx_dis = 1;
	ncl_cmd.op_code = NCL_CMD_OP_READ;
    #if NCL_FW_RETRY
    ncl_cmd.retry_step = 0;
    #endif
	ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
	ncl_cmd.flags = NAL_PB_TYPE_SLC|NCL_CMD_NO_READ_RETRY_FLAG;
	ncl_cmd.status = 0;
	ncl_cmd.addr_param.common_param.list_len = cnt;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.addr_param.common_param.info_list = info_list;
	for (i = 0; i < cnt; i++) {
		info_list[i].xlc.slc_idx = 0;
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
	}
	ncl_cmd.du_format_no = DU_FMT_RAW_4K;
	ncl_cmd.user_tag_list = (bm_pl_t*)SPRM_BASE_ADJ;
	ncl_cmd.completion = NULL;

	ncl_cmd_submit(&ncl_cmd);
	read_fins_templ.dw0.b.finst_type = FINST_TYPE_READ;
	read_fins_templ.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
	#if HAVE_CACHE_READ
		read_fins_templ.dw2.b.ard_schem_sel = ARD_DISABLE;
	#else
        #if NCL_FW_RETRY
        	read_fins_templ.dw2.b.ard_schem_sel = ARD_DISABLE;
        #else
	read_fins_templ.dw2.b.ard_schem_sel = ARD_TMPLT_SLC;
        #endif
	#endif
	read_fins_templ.dw2.b.host_trx_dis = 0;
}

/*!
 * @brief DCC training for BiCS4 800MT/s
 *
 * @return	not used
 */
slow_code void nand_dcc_training_bics4_800mts(void)
{
	pda_t pda;
	u32 result;
	u32 ch, ce;

	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.enc_rpt_on_enc_err = 0;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);

	extern u32 nf_clk;
	// Refer to TH58LJTxT24BBxx_152BGA_D_20181106_0.1.pdf Figure 26 Explicit DCC Training Sequence
	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
			u32 retry_cnt;
			retry_cnt = 0;
retry:
			pda = (ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift);
			nf_clk_init(nf_clk);
			ncl_set_feature(pda, NAND_FA_TSB_DCC_SETTING, 0, 0x8100);
			ncl_dcc_write(pda);
			nf_clk_init(nf_clk);
			ncl_set_feature(pda, NAND_FA_TSB_DCC_SETTING, 0, 0x8081);
			ncl_dcc_read_tsb(&pda, 1);
			nf_clk_init(nf_clk);
			result = ncl_get_feature(pda, NAND_FA_TSB_DCC_SETTING, false);
			if (result != 0x8282) {
				// On William's nand, some CEs sometimes return other value, retry
				if (retry_cnt > 5) {
					ncb_vendor_trace(LOG_ERR, 0x74b2, "DCC fail @ ch %d ce %d, %x\n", ch, ce, result);
					//sys_assert(result == 0x8282);
				} else {
					retry_cnt++;
					goto retry;
				}
			}
		}
	}

	eccu_ctrl.b.enc_rpt_on_enc_err = 1;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
}

/*!
 * @brief DCC training for HDR BiCS4 1066MT/s
 *
 * @return	not used
 */
slow_code void nand_dcc_training_HDR_bics4_1066mts(void)
{
	pda_t pda;
	u32 result;
	u32 is_sts_fail;
	u32 ch, ce;
	volatile u32 val;

	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.enc_rpt_on_enc_err = 0;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);

	extern u32 nf_clk;
	// Refer to TH58LJTxT24BBxx_152BGA_D_20181106_0.1.pdf Figure 26 Explicit DCC Training Sequence
	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
			u32 retry_cnt;
			retry_cnt = 0;
			is_sts_fail= false;
retry:

			pda = (ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift);

			val=0x00000001;
			ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, 0, val);
			result = ncl_get_feature(pda, NAND_FA_TSB_DCC_132BGA, false);
			if (result & 0x00000001)
			{
				ncb_vendor_trace(LOG_ERR, 0xd44a, "Step 1: set_feature: %x\n", result);
			}
			//ncl_polling_status(pda);

			u32 i,lun;

			for (lun = 0; lun < nand_lun_num(); lun++) { //nand_info.geo.nr_luns
				pda = (ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift) + (lun << nand_info.pda_lun_shift);
				ncb_vendor_trace(LOG_ERR, 0x8c36, " ch %d ce %d lun %d, pda=%x, retry_cnt %d\n", ch, ce, lun, pda, retry_cnt);

				pda_t pda_list[DU_CNT_PER_PAGE];

				for (i = 0; i < DU_CNT_PER_PAGE; i++) {
					pda_list[i] = pda + (lun << nand_info.pda_lun_shift) + i;
				}

				ncl_dcc_read_tsb(pda_list, DU_CNT_PER_PAGE);
				ncb_vendor_trace(LOG_ERR, 0xe33f, "Step 2: 05h 5ALE E0h  16384 byte\n");
				extern u32 ncl_polling_status(pda_t pda);
				is_sts_fail |= ncl_polling_status(pda);
			}

			val=0x00000000;
			ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, 0, val);
			result = ncl_get_feature(pda, NAND_FA_TSB_DCC_132BGA, false);
			ncb_vendor_trace(LOG_ERR, 0x3388, "Step 3: set_feature: %x\n", result);
			if ((result & 0x00000001)||(is_sts_fail==true)) {
				if (retry_cnt > 3) {
					ncb_vendor_trace(LOG_ERR, 0x8c93, "DCC fail !!\n");
					//sys_assert(result == 0x8282);
				} else {
					retry_cnt++;
					goto retry;
				}
			}
		}
	}

	eccu_ctrl.b.enc_rpt_on_enc_err = 1;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
}

/*!
 * @brief DCC training for BiCS4 TH58LJT1V24BA8H
 * Refer to "M2CMI19-055 BiCS FLASH Gen4 TLC DCC Training Rev1.0.pdf"
 *
 * @return	not used
 */
slow_code void nand_dcc_training_TH58LJT1V24BA8H(void)
{
	pda_t pda;
	pda_t pda_list[DU_CNT_PER_PAGE];
	u32 result = 0;
	u32 ch, ce, lun;
	u32 i;

	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.enc_rpt_on_enc_err = 0;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
	dcc_training_fail_cnt =0;
	extern u32 nf_clk;
	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
			u32 retry_cnt;
			retry_cnt = 0;
retry:
			pda = (ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift);
			nf_clk_init(nf_clk);
			ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, 0, 0x03);
			for (lun = 0; lun < nand_lun_num(); lun++) {
				for (i = 0; i < DU_CNT_PER_PAGE; i++) {
					pda_list[i] = pda + (lun << nand_info.pda_lun_shift) + i;
				}
				ncl_dcc_read_tsb(pda_list, DU_CNT_PER_PAGE);
			}
			nf_clk_init(nf_clk);
			ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, 0, 0x00);
			for (lun = 0; lun < nand_lun_num(); lun++) {
				nf_clk_init(nf_clk);
				pda_list[0] = pda + (lun << nand_info.pda_lun_shift) + i;
				result = ncl_get_feature(pda_list[0], NAND_FA_TSB_DCC_132BGA, true);
				if (result != 0x80) {
					// On my nand, CH7 CE0 LUN1 sometimes return other value, retry
					if (retry_cnt > 3) {
						//sys_assert(result == 0x80);
						dcc_training_fail_cnt++;
						printk("DCC Training fail after retry 3 times, ch:%d, ce:%d, lun:%d, result:%x\n",ch,ce,lun,result);
						printk("DCC total fail cnt = %d\n",dcc_training_fail_cnt);
					} else {
						retry_cnt++;
						goto retry;
					}
				}
			}
			ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, 0, 0x02);
		}
	}
	eccu_ctrl.b.enc_rpt_on_enc_err = 1;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
}
slow_code void nand_dcc_training_TH58LKT1Y45BA8C(void)
{
	pda_t pda;
	pda_t pda_list[DU_CNT_PER_PAGE];
	u32 ch, ce, lun;
	u32 i = 0;  //Sean_test
	eccu_ctrl_reg0_t eccu_ctrl;
	eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
	eccu_ctrl.b.enc_rpt_on_enc_err = 0;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
	extern u32 nf_clk;
	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
			pda = (ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift);
            ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, 0,0x050001);  
			for (lun = 0; lun < nand_lun_num(); lun++) {
				for (i = 0; i < DU_CNT_PER_PAGE; i++) {
					pda_list[i] = pda + (lun << nand_info.pda_lun_shift) + i;
				}
				ncl_dcc_read_tsb(pda_list, DU_CNT_PER_PAGE);
			}
            ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, false, 0x00);  
		}
	}
	eccu_ctrl.b.enc_rpt_on_enc_err = 1;
	eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);
}

/*!
 * @brief DCC training required by some BiCS4 in order to run high speed
 *
 * @return	not used
 */
slow_code void nand_dcc_training(void)
{
	extern u32 nf_clk;
	extern struct finstr_format set_feature_tmpl;
	// TH58LJT1V24BA8H said read status cannot be sent
	set_feature_tmpl.dw0.b.poll_dis = 1;
	if ((nand_is_bics4_TH58LJT1V24BA8H() || nand_is_bics4_TH58TFT2T23BA8J()) && (nf_clk>=666)) {
		ncb_vendor_trace(LOG_ERR, 0x38c7, "nand_dcc_training_8DP\n");

		log_level_t old = (log_level_t) log_level_chg(LOG_ALW);	 //skip dcc_training NCL err log. AlanCC

		nand_dcc_training_TH58LJT1V24BA8H();

		log_level_chg(old);

	} else if ((nand_is_bics4_800mts()|| nand_is_bics4_16DP ()) && (nf_clk > 533)) { //_GENE_20200624
		ncb_vendor_trace(LOG_ERR, 0x179d, "DCC training_16DP\n");
		nand_dcc_training_bics4_800mts();
	} else if (nand_is_bics4_HDR() && (nf_clk > 533)) {
		ncb_vendor_trace(LOG_ERR, 0xa420, "nand_dcc_training_HDR_bics4_1066mts()\n");
		nand_dcc_training_HDR_bics4_1066mts();
	} else if (nand_is_bics5_TH58LKT1Y45BA8C()){
        printk("nand_dcc_training_TH58LKT1Y45BA8C()\n");
        nand_dcc_training_TH58LKT1Y45BA8C();
	}
	set_feature_tmpl.dw0.b.poll_dis = 0;
}

/*!
 * @brief Nand vendor specific operation when switching NF clock
 *
 * @param old_freq	Old frequency
 * @param new_freq	New frequency
 *
 * @return	not used
 */
fast_code void nand_vendor_clk_switch(u32 old_freq, u32 new_freq)
{
	u32 ch, ce;

	if (nand_is_bics4_800mts()) {
		if ((old_freq > 533) && (new_freq <= 533)) {
			for (ch = 0; ch < nand_channel_num(); ch++) {
				for (ce = 0; ce < nand_target_num(); ce++) {
					ncl_set_feature((ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift), NAND_FA_TSB_DCC_SETTING, 0, 0);
				}
			}
		} else if (new_freq > 533) {
			nand_dcc_training_bics4_800mts();
		}
	}
}

fast_code void nand_set_drive_strength(void)    //add for avoid page_param read fail_Sean_20230103
{
	u32 val, output;
    u32 ch, ce, lun;
	u8 fa;	// Feature address
    val = 0x06;// 25ohm overdrive drive strength on EVB
    fa = NAND_FA_DRIVE_STRENGH;
    for (ch = 0; ch < max_channel; ch++){
        for (ce = 0; ce < max_target; ce++){
		    if (!nand_target_exist(ch, ce)) {
					continue;
		    }
        	set_feature(ch, ce, fa, val);
            for (lun = 0; lun < 2; lun++) {
                output = get_feature_D4h(ch, ce, lun, fa);
                if (output != val) {
                    ncb_vendor_trace(LOG_ERR, 0x976c, "ch %d ce %d lun %d drive strength fail %x", ch, ce, lun, output);
                }
            }
        }
    }
}
/*!
 * @brief Toshiba Nand set feature configuration
 *
 * @return Not used
 */
slow_code void nand_set_timing(void)
{
	bool pass = true;
	u32 val, output;
    u32 ch, ce, lun;
	u8 fa;	// Feature address
	u8 retry_cnt;
	u8 FeatureErr_flag = 0;
	srb_t *srb = (srb_t *) SRAM_BASE;
	if (nand_is_bics4_800mts()) {
#if defined(M2)
		ncb_vendor_trace(LOG_ERR, 0x608a, "Inc VCCQ 1.35V\n");
		// Don't know why, 1.2V DLL pass, but stress has decoding error
		extern u32 pr_write(u8 cmd_code, u8 value);
		pr_write(5, 0xB4);// Increase NAND VCCQ to 1.35V. Default 0xA8 is 1.2V
#endif
	}

	for (ch = 0; ch < max_channel; ch++) {
		for (ce = 0; ce < max_target; ce++) {
			retry_cnt = 0;			
			do{
				pass = true;
				if (!nand_target_exist(ch, ce)) {
					continue;
				}
#if defined(QLC_SUPPORT)
				val = 4;
				set_feature(ch, ce, NAND_FA_TOGGLE2_MODE, val);
                for (lun = 0; lun < 2; lun++) {
                    output = get_feature_D4h(ch, ce, lun, NAND_FA_TOGGLE2_MODE);
                    if (output != val) {
                        ncb_vendor_trace(LOG_ERR, 0x8f77, "ch %d ce %d lun %d toggle 2.0 fail %x", ch, ce, lun, output);
                        pass = false;
                    }
                }
#else
                if (srb->DLL_Auto_Tune == 0x55)
                {
    			    val = srb->nand_drv[ch][ce];
                }
                else
                {
    				if (nand_is_bics5_TH58LKT1Y45BA8C() ) {	//PJ1	
    					val = 2;
    				}
    				else if (nand_is_bics5_TH58LKT2Y45BA8H()) {	//PJ1	
    					val = 4;
    				}
    				else {
    					val = 4;
    					panic("new nand model,need to tune the drive strength \n");
    				}
                }
        		srb->DLL_margin_nand_drv = val;
                //printk("drv: %d, srb->DLL_margin_nand_drv: %d\n", val, srb->DLL_margin_nand_drv);

                fa = NAND_FA_DRIVE_STRENGH;
				set_feature(ch, ce, fa, val);
                for (lun = 0; lun < 2; lun++) {
                    output = get_feature_D4h(ch, ce, lun, fa);
                    if (output != val) {
                        ncb_vendor_trace(LOG_ERR, 0xaad5, "ch %d ce %d lun %d drive strength fail %x", ch, ce, lun, output);
                        pass = false;
                    }
                }

                if (srb->DLL_Auto_Tune == 0x55)
                {
    			    val = 7 | (srb->nand_odt[ch][ce] << 4);
                }
                else
                {
    				val = 7;
#if TSB_XL_NAND
    				val = 7 | (4 << 4);// Enable 50omh ODT
#else
    				if (nand_is_bics4_TH58TFT2T23BA8J() || nand_is_tsb_tsv() || nand_is_bics4_TH58LJT1V24BA8H() || nand_is_bics4_TH58LJT1T24BA8C()) {
    					val = 7 | (0 << 4);// Disable NAND ODT
    				}
#endif
                }
#if WARMUP_RD_CYCLES
				val |= (ctz(WARMUP_RD_CYCLES) + 1) << 8;
#endif
#if WARMUP_WR_CYCLES
				val |= (ctz(WARMUP_WR_CYCLES) + 1) << 12;
#endif
        		srb->DLL_margin_nand_odt = (val >> 4);
                //printk("odt: %d, srb->DLL_margin_nand_odt: %d\n", val, srb->DLL_margin_nand_odt);
                set_feature(ch, ce, NAND_FA_TOGGLE2_MODE, val);
                for (lun = 0; lun < 2; lun++) {
                    output = get_feature_D4h(ch, ce, lun, NAND_FA_TOGGLE2_MODE);
                    if (output != val) {
                        ncb_vendor_trace(LOG_ERR, 0x3736, "ch %d ce %d lun %d ddr fail %x", ch, ce, lun, output);
                        pass = false;
                    }
                }
#if NON_TARGET_ODT_MODE
				if (nand_is_bics5()){
						val = 4 | (4 << 4) | (4 << 8)  ; //Enable 50omh non target ODT , 1->150 2->100, 3->75, 4 -> 50
						nand_set_feature (ch,ce,NAND_FA_TOGGLE_HIGH_SPEED_ENHANCED, val);
						output = nand_get_feature(ch, ce, NAND_FA_TOGGLE_HIGH_SPEED_ENHANCED);
						if (output != val) {
								ncb_vendor_trace(LOG_ERR, 0xb3d1, "ch %d ce %d non_target ODT fail %x", ch, ce, output);
								pass = false;
						}
				}
#endif
#ifndef TSB_XL_NAND
				if (nand_is_bics4() && (pass == true))
#endif
				{
#if ENABLE_VPP
					val = 1;
#else
					val = 0;
#endif
					set_feature(ch, ce, NAND_FA_EXTERNAL_VPP, val);
                    for (lun = 0; lun < 2; lun++) {
                        output = get_feature_D4h(ch, ce, lun, NAND_FA_EXTERNAL_VPP);
                        if (output != val) {
                            ncb_vendor_trace(LOG_ERR, 0x28f9, "ch %d ce %d lun %d vpp fail %x", ch, ce, lun, output);
                            pass = false;
                        }
                    }
#if TSB_XL_NAND
					// Enable "Auto refresh" temperature code update for XL nand
					val = 0x10000;
					// Enable early cache release for XL nand
					val = 0x00100;
					set_feature(ch, ce, NAND_FA_TSB_TEMP_CODE_UPDT, val);
                    for (lun = 0; lun < 2; lun++) {
                        output = get_feature_D4h(ch, ce, lun, NAND_FA_TSB_TEMP_CODE_UPDT);
                        if (output != val) {
                            ncb_vendor_trace(LOG_ERR, 0xb869, "ch %d ce %d lun %d auto temp code update fail %x", ch, ce, lun, output);
                            pass = false;
                        }
                    }
#endif
				}

				if(nand_support_aipr() && (pass == true)) {
					val = 1;
					set_feature(ch, ce, NAND_FA_TSB_AIPR, val);
                    for (lun = 0; lun < 2; lun++) {
                        output = get_feature_D4h(ch, ce, lun, NAND_FA_TSB_AIPR);
                        if (output != val) {
                            ncb_vendor_trace(LOG_ERR, 0xf734, "ch %d ce %d lun %d aipr fail %x", ch, ce, lun, output);
                            pass = false;
                        }
                    }
				}
#endif 
				if(pass==false){
					retry_cnt++;
					ncb_vendor_trace(LOG_ERR, 0x5ba4, "set feature retry cnt: %d", retry_cnt);
					if (retry_cnt == 3)                              //Sean_20220728
						FeatureErr_flag = 1;
                    for (lun = 0; lun < 2; lun++) {
                        nand_reset_FD(ch, ce, lun);
                    }
				}
			}while((pass!=true) && (retry_cnt<3));
		}
	}

	//extern read_only_t read_only_flags;
	if (!FeatureErr_flag) {
		ncb_vendor_trace(LOG_INFO, 0xbf2e, "Toshiba set feature pass");
	} else {
		panic("Toshiba set feature fail");
	    //ncb_vendor_trace(LOG_ERR, 0, "Toshiba set feature fail, set to Read Only Mode");
		//flush_to_nand(EVT_READ_ONLY_MODE_IN);
    	//read_only_flags.b.nand_feature_fail = 1;
		//cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
	}
}

