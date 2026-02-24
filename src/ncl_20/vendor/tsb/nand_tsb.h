//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#pragma once

#define NAND_FA_TSB_TEMP_CODE_UPDT	0x90	///< Refer to M2CXI02-036_Innogrit_XL-FLASH Temperature Code Update for Fast Read Rev.0.11.pdf
#define NAND_FA_TSB_AIPR			0x91	///< Refer to M2CMI19-047 BiCS FLASH Gen4 TLC Asynchronous Independent Plane Read Preliminary Rev0.1.pdf
#define NAND_FA_TSB_DRIVE_STRENGTH	0xB1	///< ODT, Drive strength setting. Refer to TH58LJTxT24BBxx_152BGA_D_20181106_0.1.pdf

//tony 20200723
#define EH_ENABLE_2BIT_RETRY
//#define EH_ENABLE_1BIT_ADD_2BIT

#if !QLC_SUPPORT
#define NAND_FA_LOW_READ_RETRY 0x89	///< Set feature address for TLC low page read retry
#define NAND_FA_MID_READ_RETRY 0x89	///< Set feature address for TLC middle page read retry
#define NAND_FA_UPR_READ_RETRY 0x8A	///< Set feature address for TLC upper page read retry
#define NAND_FA_SLC_READ_RETRY 0x8B	///< Set feature address for pseudo SLC page read retry
#define NAND_FA_LOW_READ_RETRY_P1 0x85	///< Set feature address for AIPR plane 1 TLC low page read retry
#define NAND_FA_MID_READ_RETRY_P1 0x85	///< Set feature address for AIPR plane 1 TLC middle page read retry
#define NAND_FA_UPR_READ_RETRY_P1 0x86	///< Set feature address for AIPR plane 1 TLC upper page read retry
#define NAND_FA_SLC_READ_RETRY_P1 0x87	///< Set feature address for AIPR plane 1 pseudo SLC page read retry
#define NAND_FA_LOW_READ_RETRY_P2 0xA9	///< Set feature address for AIPR plane 2 TLC low page read retry
#define NAND_FA_MID_READ_RETRY_P2 0xA9	///< Set feature address for AIPR plane 2 TLC middle page read retry
#define NAND_FA_UPR_READ_RETRY_P2 0xAA	///< Set feature address for AIPR plane 2 TLC upper page read retry
#define NAND_FA_SLC_READ_RETRY_P2 0xAB	///< Set feature address for AIPR plane 2 pseudo SLC page read retry
#define NAND_FA_LOW_READ_RETRY_P3 0xA5	///< Set feature address for AIPR plane 3 TLC low page read retry
#define NAND_FA_MID_READ_RETRY_P3 0xA5	///< Set feature address for AIPR plane 3 TLC middle page read retry
#define NAND_FA_UPR_READ_RETRY_P3 0xA6	///< Set feature address for AIPR plane 3 TLC upper page read retry
#define NAND_FA_SLC_READ_RETRY_P3 0xA7	///< Set feature address for AIPR plane 3 pseudo SLC page read retry

#ifdef EH_ENABLE_2BIT_RETRY
#ifdef EH_ENABLE_1BIT_ADD_2BIT
#define NAND_FA_TOP_READ_RETRY 0x89	///< Set feature address for TLC low page (2bit) read retry
#define NAND_FA_MAX_READ_RETRY 0x8A	///< Set feature address for TLC middle page (2bit) read retry
#define NAND_FA_SPR_READ_RETRY 0x89	///< Set feature address for TLC upper page (2bit) read retry
#define NAND_FA_TOP_READ_RETRY_P1 0x89	///< Set feature address for TLC low page (2bit) read retry
#define NAND_FA_MAX_READ_RETRY_P1 0x8A	///< Set feature address for TLC middle page (2bit) read retry
#define NAND_FA_SPR_READ_RETRY_P1 0x89	///< Set feature address for TLC upper page (2bit) read retry
#endif

#define NAND_FA_SLC_READ_RETRY_2BIT_P 0x96	///< Set feature address for postive 2bit soft decoding of SLC read retry
#define NAND_FA_SLC_READ_RETRY_2BIT_N 0x96	///< Set feature address for negitive 2bit soft decoding of SLC read retry

#define NAND_FA_LOW_READ_RETRY_2BIT_P 0x94	///< Set feature address for postive 2bit soft decoding of TLC low page  read retry
#define NAND_FA_MID_READ_RETRY_2BIT_P 0x94	///< Set feature address for postive 2bit soft decoding of TLC middle page read retry
#define NAND_FA_UPR_READ_RETRY_2BIT_P 0x95	///< Set feature address for postive 2bit soft decoding of TLC upper page read retry
#define NAND_FA_LOW_READ_RETRY_2BIT_N 0x92	///< Set feature address for negitive 2bit soft decoding of TLC low page read retry
#define NAND_FA_MID_READ_RETRY_2BIT_N 0x92	///< Set feature address for negitive 2bit soft decoding of TLC middle page read retry
#define NAND_FA_UPR_READ_RETRY_2BIT_N 0x93	///< Set feature address for negitive 2bit soft decoding of TLC upper page read retry
//for AIPR plane 1 
//#define NAND_FA_LOW_READ_RETRY_2BIT_P_P1 0x99	///< Set feature address for postive 2bit soft decoding of AIPR plane 1 TLC low page read retry
//#define NAND_FA_MID_READ_RETRY_2BIT_P_P1 0x9A	///< Set feature address for postive 2bit soft decoding of AIPR plane 1 TLC middle page read retry
//#define NAND_FA_UPR_READ_RETRY_2BIT_P_P1 0x99	///< Set feature address for postive 2bit soft decoding of AIPR plane 1 TLC upper page read retry
//#define NAND_FA_LOW_READ_RETRY_2BIT_N_P1 0x97	///< Set feature address for negitive 2bit soft decoding of AIPR plane 1 TLC low page read retry
//#define NAND_FA_MID_READ_RETRY_2BIT_N_P1 0x97	///< Set feature address for negitive 2bit soft decoding of AIPR plane 1 TLC middle page read retry
//#define NAND_FA_UPR_READ_RETRY_2BIT_N_P1 0x97	///< Set feature address for negitive 2bit soft decoding of AIPR plane 1 TLC upper page read retry
#endif

#else
#define NAND_FA_LOW_READ_RETRY 0x87	///< Set feature address for QLC low page read retry
#define NAND_FA_MID_READ_RETRY 0x88	///< Set feature address for QLC middle page read retry
#define NAND_FA_UPR_READ_RETRY 0x89	///< Set feature address for QLC upper page read retry
#define NAND_FA_TOP_READ_RETRY 0x8A	///< Set feature address for QLC top page read retry
#define NAND_FA_SLC_READ_RETRY 0x8B	///< Set feature address for pseudo SLC page read retry
#endif

#define NAND_FA_TSB_INTF_CONFIGURE 0xB0	///< Refer to TSV spec TH58VHT3V42VA82_152BGA_20170515.pdf
#define NAND_FA_TSB_DCC_SETTING	0xB4	///< Set feature address for DCC training (introduced in TH58LJTxT24BBxx_152BGA_D_20181106_0.1.pdf)
#define NAND_FA_TSB_DCC_132BGA	0x20	///< Set feature address for DCC training (M2CMI19-055 BiCS FLASH Gen4 TLC DCC Training Rev1.0.pdf)
#define NAND_FA_TOGGLE_HIGH_SPEED_ENHANCED	0xC0
enum page_type_t {			///< Nand page type
	XLC_LOW_PAGE = 0,
	XLC_MID_PAGE,
	XLC_UPR_PAGE,
	XLC_TOP_PAGE,
	SLC_PAGE,
};

extern u8 ard_feature_addr[];		///< ARD set feature address for vref shift
extern u8 ard_feature_addr_p1[];	///< ARD set feature address for AIPR plane 1 vref shift
extern u8 ard_feature_addr_p2[];	///< ARD set feature address for AIPR plane 2 vref shift
extern u8 ard_feature_addr_p3[];	///< ARD set feature address for AIPR plane 3 vref shift
extern u8 tsb_slc_8b_rr_offset[];	///< ARD SLC vref shift values of feature 8Bh
#ifdef EH_ENABLE_2BIT_RETRY
extern u8 ard_feature_addr_2bit_p[];
extern u8 ard_feature_addr_2bit_n[];
extern u8 ard_feature_addr_2bit_p_p1[];
extern u8 ard_feature_addr_2bit_n_p1[];
#endif

#if !QLC_SUPPORT
extern u32 tsb_tlc_low_upr_89_rr_offset_256Gb[];///< ARD TLC(lower and upper pages) vref shift values of feature 89h
extern u32 tsb_tlc_mid_8a_rr_offset_256Gb[];	///< ARD TLC(middle pages) vref shift values of feature 8Ah
extern u32 tsb_tlc_low_mid_89_rr_offset_512Gb[];///< ARD TLC(lower and middle pages) vref shift values of feature 89h
extern u32 tsb_tlc_upr_8a_rr_offset_512Gb[];	///< ARD TLC(upper pages) vref shift values of feature 8Ah
//extern u32 tsb_tlc_low_upr_89_rr_offset[];///< ARD TLC(lower and upper pages) vref shift values of feature 89h
//extern u32 tsb_tlc_mid_8a_rr_offset[];	///< ARD TLC(middle pages) vref shift values of feature 8Ah
#ifdef EH_ENABLE_2BIT_RETRY
extern u32 tsb_tlc_2bit_89_rr_offset[];
extern u32 tsb_tlc_2bit_8a_rr_offset[];
extern u8  tsb_soft_level_p[];
extern u8  tsb_soft_level_n[];
#endif
#else
extern u32 tsb_qlc_low_87_rr_offset[];	///< ARD QLC(lower pages) vref shift values of feature 87h
extern u32 tsb_qlc_mid_88_rr_offset[];	///< ARD QLC(middle pages) vref shift values of feature 88h
extern u32 tsb_qlc_upr_89_rr_offset[];	///< ARD QLC(upper pages) vref shift values of feature 89h
extern u32 tsb_qlc_top_8a_rr_offset[];	///< ARD QLC(top pages) vref shift values of feature 8Ah
#endif
extern u8 Retry_feature_addr[];

#if !QLC_SUPPORT
#define RR_STEP_SLC 7			///< ARD SLC read retry counts
#else
#define RR_STEP_SLC 8			///< ARD SLC read retry counts
#endif
#define RR_STEP_XLC 31          //42 original setting  ///< ARD XLC read retry counts

//tony 20200813
#ifdef EH_ENABLE_2BIT_RETRY
#define RR_STEP_2BIT_XLC 7
#define SOFT_RETRY_LEVEL 4  //Tony 20201125
#endif
//tony 20200714
//typedef enum { false , true } bool;
#define TSB_BICS5
//#define TSB_BICS4_272  0
//#define TSB_BICS4_132  0 
//#define EH_2BIT_RETRY_TEST

/*!
 * @brief Get XLC page type from page index
 *
 * @param page_no	page index
 *
 * @return	low/middle/upper/top page type
 */
enum page_type_t get_xlc_page_type(u32 page_no);

/*!
 * @brief Check if nand is BiCS3
 *
 * @return	BiCS3 or not
 */
extern bool nand_is_bics3(void);

/*!
 * @brief Check if nand is BiCS4
 *
 * @return	BiCS4 or not
 */
extern bool nand_is_bics4(void);

/*!
 * @brief Check if nand is BiCS5
 *
 * @return	BiCS5 or not
 */
extern bool nand_is_bics5(void);

/*!
 * @brief Check if nand is Toshiba TSV
 *
 * @return	Toshiba TSV or not
 */
extern bool nand_is_tsb_tsv(void);

/*!
 * @brief Check if nand is Toshiba BiCS4 (labeled 800MT/s on nand module, parameter page unavailable)
 *
 * @return	Toshiba BiCS4(800MT/s) or not
 */
extern bool nand_is_bics4_800mts(void);
extern bool nand_is_bics4_16DP(void);

/*!
 * @brief Check if nand is Toshiba TH58TFT2T23BA8J
 *
 * @return	Toshiba BiCS4(TH58TFT2T23BA8J) or not
 */
extern bool nand_is_bics4_TH58TFT2T23BA8J(void);

/*!
 * @brief Check if nand is Toshiba TH58LJT1T24BA8C
 *
 * @return	Toshiba BiCS4(TH58LJT1T24BA8C) or not
 */
extern bool nand_is_bics4_TH58LJT1T24BA8C(void);

/*!
 * @brief Check if nand is Toshiba TH58LJT1V24BA8H
 *
 * @return	Toshiba BiCS4(TH58LJT1V24BA8H) or not
 */
extern bool nand_is_bics4_TH58LJT1V24BA8H(void);

/*!
 * @brief Check if nand is Toshiba TH58LJT1T24BAEF
 *
 * @return	Toshiba BiCS4(TH58LJT1T24BAEF) or not
 */
extern bool nand_is_bics4_TH58LJT1T24BAEF(void);

/*!
 * @brief Check if nand is Toshiba TH58LJT2V24BB8N
 *
 * @return	Toshiba BiCS4(TH58LJT2V24BB8N) or not
 */
extern bool nand_is_bics4_TH58LJT2V24BB8N(void);

/*!
 * @brief Check if nand is Toshiba Bics4 HDR
 *
 * @return	Toshiba BiCS4(HDR) or not
 */
extern bool nand_is_bics4_HDR(void);

extern bool nand_is_bics5_TH58LKT1Y45BA8C(void);

/*!
 * @brief Check if nand is Toshiba TH58LKT2Y45BA8H
 *
 * @return	Toshiba BiCS5(TH58LKT2Y45BA8H) or not
 */
extern bool nand_is_bics5_TH58LKT2Y45BA8H(void);


/*!
 * @brief DCC training required by some BiCS4 in order to run high speed
 *
 * @return	not used
 */
extern void ncl_dcc_training(void);

/*!
 * @brief Get Toshiba self-adjust read detected step
 * Refer to M2CMI19-047 BiCS FLASH Gen4 TLC Asynchronous Independent Plane Read Preliminary Rev0.1.pdf
 *
 * @param ch	Channel
 * @param ce	Target
 * @param lun	LUN
 * @param fa	Feature address
 *
 * @return	Detected self-adjust read optimal vref step
 */
extern u32 tsb_read_detected_step(u8 ch, u8 ce, u8 lun, u8 fa);
