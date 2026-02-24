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
#ifndef _ARD_H_
#define _ARD_H_

//#if NCL_HAVE_ARD  //tony 20200821
#if defined(HAVE_T0)
#define FSPM_ARD_TEMPLATE_FCMD_CNT			7	// Shasta T0
#else
//#define FSPM_ARD_TEMPLATE_FCMD_CNT			16	// Shasta A0 or Tacoma
#define FSPM_ARD_TEMPLATE_FCMD_CNT          31  //31 is max num of FCMD //tony 20200714 
#endif
//#define FSPM_ARD_TEMPLATE_FINST_PER_FCMD		4

#ifdef EH_ENABLE_2BIT_RETRY
#define FSPM_ARD_TEMPLATE_FINST_PER_FCMD    12  //SetFeature_HD+SetFeature_HD_P1+ReRead_HD
                                                //+SetFeature_Pos+SetFeature_Neg+ReRead_SD+ReadStatus_SD
                                                //+Reset_HD+Reset_HD_P1+Reset_Pos+Reset_Neg+ReadStatus_SD    
                                                //tony 20200824
                                                
#else
#define FSPM_ARD_TEMPLATE_FINST_PER_FCMD    7   //SetFeature+ReRead+ReadStatus+Reset+ReadStatus//tony 20200813
#endif
    
#define FSPM_ARD_TEMPLATE_COUNT		8
#define FSPM_ARD_TEMPLATE_FINSTS	(FSPM_ARD_TEMPLATE_FCMD_CNT * FSPM_ARD_TEMPLATE_FINST_PER_FCMD)
#define FSPM_ARD_COUNT		3500	//(FSPM_ARD_TEMPLATE_COUNT * FSPM_ARD_TEMPLATE_FINSTS) //2976
#define SB_POSITIVE_SHIFT_VALUE 0xA
#define SB_NEGATIVE_SHIFT_VALUE 0xF6 //Use two's complement to represent negative number.

enum {
	ARD_DISABLE	= 0,
	ARD_TMPLT_SLC,
	ARD_TMPLT_LOW,
	ARD_TMPLT_MID,
	ARD_TMPLT_UPR,
	ARD_TMPLT_TOP,
	ARD_TMPLT_MAX,
	ARD_TMPLT_SPR,
};

#define ARD_TMPLT_XLC(pg_idx) (ARD_TMPLT_LOW + pg_idx)
//#else
//#define FSPM_ARD_COUNT			0
//#define ARD_TMPLT_SLC			0
//#define ARD_TMPLT_XLC(pg_idx)	0
//#endif//NCL_HAVE_ARD

/*!
 * @brief FINST ARD format
 */
struct finstr_ard_format {
	union {
		u32 all;
		struct {
			/* 0: 3 */
			u32 fins_vld:1;
			u32 fins:1;
			u32 lins:1;
			u32 rsvd0:1;

			/* 4: 7 */
			u32 poll_dis:1;
			u32 susp_en:1;
			u32 set_feat_enh:1;
			u32 rsvd1:1;

			/* 8: 15 */
			u32 ndcmd_fmt_sel:7;
			u32 vsc_en:1;

			/* 16: 23 */
			u32 rsvd2:4;
			u32 finst_type:3;
			u32 fins_fuse:1;

			/* 24: 31 */
			u32 ard_loop_idx:5;
			u32 rsvd4:2;
			u32 no_eccu_path:1;
		} b;
	} dw0;

	union {
		u32 all;
		struct {
			u32 oper_time:14;
			u32 rsvd0:2;
			u32 set_feature_addr:8;
			u32 rsvd1:3;
			u32 xfcnt_sel:5;
		} b;
	} dw1;

	u32 set_feature_data_low;

	u32 set_feature_data_high;
};

//extern struct finstr_format read_fins_templ;
//extern struct fspm_usage_t *fspm_usage_ptr;

/*!
 * @brief Configure ARD templates
 *
 * @return	not used
 */
void ficu_conf_ard_template(void);
#endif
