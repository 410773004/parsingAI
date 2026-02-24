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
/*! \file ard_tsb.c
 * @brief ARD toshiba
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * Toshiba related ARD configurations
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#if NCL_HAVE_ARD
#include "ard.h"
#include "ncb_ficu_register.h"
#include "ndcmd_fmt.h"
#include "finstr.h"
#include "ficu.h"
#include "nand_tsb.h"
#include "nand.h"

#define ARD_LOOP 16

extern struct finstr_format read_fins_templ;
extern struct fspm_usage_t *fspm_usage_ptr;


/*!
 * @brief Configure ARD templates
 *
 * @return	not used
 */
init_code void ficu_conf_ard_template(void)
{
	u32 idx, loop;
	u32 cnt = 0;
	ficu_ard_conf_reg1_t ard_conf_reg;
	struct finstr_ard_format *fspm_template;
	struct finstr_ard_format ard_tmpl = {// Set feature template
		.dw0 = {
			/* 0: 7 */
			.b.fins_vld		= 1,
			.b.fins			= 0,
			.b.lins			= 0,
			.b.rsvd0		= 0,
			.b.poll_dis		= 0,
			.b.susp_en		= 0,
			.b.set_feat_enh		= 1,
			.b.rsvd1		= 0,

			/* 8: 15 */
			.b.ndcmd_fmt_sel	= FINST_NAND_FMT_FEATURE_LUN,
			.b.vsc_en		= 0,

			/* 16: 23 */
			.b.rsvd2		= 0,
			.b.finst_type		= FINST_TYPE_PROG,
			.b.fins_fuse		= 1,

			/* 24: 31 */
			.b.rsvd4		= 0,
			.b.no_eccu_path		= 1,
		},
		.dw1 = {
			.b.rsvd0		= 0,
			.b.rsvd1		= 0,
			.b.set_feature_addr	= 0,
			.b.xfcnt_sel		= FINST_XFER_4B,
		},
		.set_feature_data_low = 0,
		.set_feature_data_high = 0,
	};
	struct  finstr_ard_format ard_sts_tmpl = {// Read status template
		.dw0 = {
			/* 0: 7 */
			.b.fins_vld		= 1,
			.b.fins		= 0,
			.b.lins		= 1,
			.b.rsvd0		= 0,
			.b.poll_dis		= 0,
			.b.susp_en		= 0,
			.b.set_feat_enh	= 0,
			.b.rsvd1		= 0,

			/* 8: 15 */
			.b.ndcmd_fmt_sel	= FINST_NAND_FMT_READ_STATUS,
			.b.vsc_en		= 0,

			/* 16: 23 */
			.b.rsvd2		= 0,
			.b.finst_type		= FINST_TYPE_READ_STS,
			.b.fins_fuse		= 0,

			/* 24: 31 */
			.b.ard_loop_idx	= 0,
			.b.rsvd4		= 0,
			.b.no_eccu_path	= 1,
		},
		.dw1 = {
			.b.rsvd0		= 0,
			.b.rsvd1		= 0,
			.b.xfcnt_sel		= FINST_XFER_ZERO,
			.b.set_feature_addr	= 0,
		},
		.set_feature_data_low	= 0,
		.set_feature_data_high = 0,
	};

	fspm_template = (struct finstr_ard_format *)fspm_usage_ptr->ard_template;
//-----------------------------------1BIT + 2BIT RETRY----------------------------------// //tony 20200826
#ifdef EH_ENABLE_1BIT_ADD_2BIT
#ifdef EH_ENABLE_2BIT_RETRY
	for (idx = ARD_TMPLT_SLC; idx < 8; idx++) 
#else
	for (idx = ARD_TMPLT_SLC; idx < ARD_TMPLT_TOP; idx++) 
#endif
	{
        u32  count = 0;
        u32* array = NULL;
#ifdef EH_ENABLE_2BIT_RETRY
        u32  loop_hd = 0;
    	u32  sf_bit_p_array[RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL] = {0};
    	u32  sf_bit_n_array[RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL] = {0};
        //u8   sf_slc_bit_p_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
    	//u8   sf_slc_bit_n_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
    	u32   sf_slc_bit_p_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
        u32   sf_slc_bit_n_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
        u8   soft_idx = 0;
        u8   hard_idx = 0;
        u8   soft_array_idx = 0;
#endif

		switch(idx) 
        {
    		case ARD_TMPLT_SLC:
                array = (u32 *)tsb_slc_8b_rr_offset;
#ifdef EH_ENABLE_2BIT_RETRY
                for(hard_idx = 0; hard_idx < RR_STEP_SLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {
                        //soft_array_idx = (RR_STEP_SLC * soft_idx) + hard_idx;
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
            			//sf_slc_bit_p_array[soft_array_idx] = tsb_slc_8b_rr_offset[hard_idx] + tsb_soft_level_p[soft_idx];
                        //sf_slc_bit_n_array[soft_array_idx] = tsb_slc_8b_rr_offset[hard_idx] + tsb_soft_level_n[soft_idx];
                        sf_slc_bit_p_array[soft_array_idx] = (((((u32 *)tsb_slc_8b_rr_offset)[hard_idx] + ((u32 *)tsb_soft_level_n)[soft_idx]) << 8) | (((u32 *)tsb_slc_8b_rr_offset)[hard_idx] + ((u32 *)tsb_soft_level_p)[soft_idx]));
                        sf_slc_bit_n_array[soft_array_idx] = sf_slc_bit_p_array[soft_array_idx];
                    }
                }
    			count = RR_STEP_SLC * SOFT_RETRY_LEVEL;
#else
                count = RR_STEP_SLC;
#endif
                break;
    		case ARD_TMPLT_LOW:
    		case ARD_TMPLT_UPR:				
           		if (nand_info.id[1] == 0x3E) {  //4T
					array = tsb_tlc_low_upr_89_rr_offset_256Gb;
           		} else {
					array = tsb_tlc_low_upr_89_rr_offset_512Gb;
           		}
    			//array = tsb_tlc_low_upr_89_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_MID:
				if (nand_info.id[1] == 0x3E) {  //4T
					array = tsb_tlc_mid_8a_rr_offset_256Gb;
				} else {
					array = tsb_tlc_mid_8a_rr_offset_512Gb;
				}
    			//array = tsb_tlc_mid_8a_rr_offset;
    			count = RR_STEP_XLC;
    			break;
#ifdef EH_ENABLE_2BIT_RETRY
    		case ARD_TMPLT_TOP:
                //array = tsb_tlc_low_upr_89_rr_offset;
                array = tsb_tlc_2bit_89_rr_offset;
                for(hard_idx = 0; hard_idx < RR_STEP_2BIT_XLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
                        sf_bit_p_array[soft_array_idx] = tsb_tlc_2bit_89_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_p)[soft_idx] << 24) | (((u32 *)tsb_soft_level_p)[soft_idx] << 16) | (((u32 *)tsb_soft_level_p)[soft_idx] << 8) | ((u32 *)tsb_soft_level_p)[soft_idx]);
                        sf_bit_p_array[soft_array_idx] = sf_bit_p_array[soft_array_idx] & 0x00FF00FF;
                        sf_bit_n_array[soft_array_idx] = tsb_tlc_2bit_89_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_n)[soft_idx] << 24) | (((u32 *)tsb_soft_level_n)[soft_idx] << 16) | (((u32 *)tsb_soft_level_n)[soft_idx] << 8) | ((u32 *)tsb_soft_level_n)[soft_idx]);
                        sf_bit_n_array[soft_array_idx] = sf_bit_n_array[soft_array_idx] & 0x00FF00FF;
                    }
                }
                count = RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL;
    			break;
    		case ARD_TMPLT_MAX:
    			//array = tsb_tlc_mid_8a_rr_offset;
    			array = tsb_tlc_2bit_8a_rr_offset;
                for(hard_idx = 0; hard_idx < RR_STEP_2BIT_XLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {  
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
                		//sf_bit_p_array[soft_array_idx] = tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_p)[soft_idx] << 24) | (((u32 *)tsb_soft_level_p)[soft_idx] << 16) | (((u32 *)tsb_soft_level_p)[soft_idx] << 8) | ((u32 *)tsb_soft_level_p)[soft_idx]);
                        //sf_bit_n_array[soft_array_idx] = tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_n)[soft_idx] << 24) | (((u32 *)tsb_soft_level_n)[soft_idx] << 16) | (((u32 *)tsb_soft_level_n)[soft_idx] << 8) | ((u32 *)tsb_soft_level_n)[soft_idx]);
                		sf_bit_p_array[soft_array_idx] = tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_p)[soft_idx] << 16) | (((u32 *)tsb_soft_level_p)[soft_idx] << 8) | ((u32 *)tsb_soft_level_p)[soft_idx]);
                        sf_bit_n_array[soft_array_idx] = tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_n)[soft_idx] << 16) | (((u32 *)tsb_soft_level_n)[soft_idx] << 8) | ((u32 *)tsb_soft_level_n)[soft_idx]);
                    }
                }
                count = RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL;

    			break;
    		case ARD_TMPLT_SPR:
    			//array = tsb_tlc_low_upr_89_rr_offset;
    			array = tsb_tlc_2bit_89_rr_offset;
                for(hard_idx = 0; hard_idx < RR_STEP_2BIT_XLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
            			sf_bit_p_array[soft_array_idx] = tsb_tlc_2bit_89_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_p)[soft_idx] << 24) | (((u32 *)tsb_soft_level_p)[soft_idx] << 16) | (((u32 *)tsb_soft_level_p)[soft_idx] << 8) | ((u32 *)tsb_soft_level_p)[soft_idx]);
                        sf_bit_p_array[soft_array_idx] = sf_bit_p_array[soft_array_idx] & 0xFF00FF00;
                        sf_bit_n_array[soft_array_idx] = tsb_tlc_2bit_89_rr_offset[hard_idx] + ((((u32 *)tsb_soft_level_n)[soft_idx] << 24) | (((u32 *)tsb_soft_level_n)[soft_idx] << 16) | (((u32 *)tsb_soft_level_n)[soft_idx] << 8) | ((u32 *)tsb_soft_level_n)[soft_idx]);
                        sf_bit_n_array[soft_array_idx] = sf_bit_n_array[soft_array_idx] & 0xFF00FF00;
                    }
                }
                count = RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL;
                break;
#endif
		}
        
		if (count > FSPM_ARD_TEMPLATE_FCMD_CNT) 
        {
			count = FSPM_ARD_TEMPLATE_FCMD_CNT;
		}
        
		ard_conf_reg.b.ficu_ard_tmpl0_loop = count;
		ard_conf_reg.b.ficu_ard_tmpl0_offset_addr = cnt * sizeof(struct finstr_ard_format);
		ficu_writel(ard_conf_reg.all, FICU_ARD_CONF_REG1 - 4 + (idx << 2));

        //set HD mode feature address 
		ard_tmpl.dw1.b.set_feature_addr = ard_feature_addr[idx];

        //set HD mode feature address for plane 1 
		struct finstr_ard_format ard_aipr_p1_tmpl = ard_tmpl; // Set feature template for AIPR plane 1
		if (nand_support_aipr()) 
        {
			ard_aipr_p1_tmpl.dw1.b.set_feature_addr = ard_feature_addr_p1[idx];
		}
        
#ifdef EH_ENABLE_2BIT_RETRY
        //set SD mode feature address        
        struct finstr_ard_format ard_2bit_soft_p_tmpl = ard_tmpl;
        struct finstr_ard_format ard_2bit_soft_n_tmpl = ard_tmpl;
        
        ard_2bit_soft_p_tmpl.dw1.b.set_feature_addr = ard_feature_addr_2bit_p[idx];
        ard_2bit_soft_n_tmpl.dw1.b.set_feature_addr = ard_feature_addr_2bit_n[idx];        
#endif 
          
		for (loop = 0; loop < count; loop++) 
        {
			//HD set feature
			ard_tmpl.dw0.b.ard_loop_idx = loop;
			fspm_template[cnt] = ard_tmpl;
			fspm_template[cnt].dw0.b.fins = 1;
#ifdef EH_ENABLE_2BIT_RETRY
            if ((idx == ARD_TMPLT_SLC) || (idx == ARD_TMPLT_TOP) || (idx == ARD_TMPLT_MAX) || (idx == ARD_TMPLT_SPR))
            {
                loop_hd = (loop / SOFT_RETRY_LEVEL);
				fspm_template[cnt].set_feature_data_low = array[loop_hd];
            }
            else 
#endif
            {
				fspm_template[cnt].set_feature_data_low = array[loop];
			}
			cnt++;

			if (nand_support_aipr()) 
            {
				ard_aipr_p1_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p1_tmpl;
#ifdef EH_ENABLE_2BIT_RETRY
                if ((idx == ARD_TMPLT_SLC) || (idx == ARD_TMPLT_TOP) || (idx == ARD_TMPLT_MAX) || (idx == ARD_TMPLT_SPR))
                {
                    loop_hd = (loop / SOFT_RETRY_LEVEL);
					fspm_template[cnt].set_feature_data_low = array[loop_hd];
                }
                else 
#endif
                {
					fspm_template[cnt].set_feature_data_low = array[loop];
				}
				cnt++;
			}


			//HD re-read
			memcpy(&fspm_template[cnt], &read_fins_templ, sizeof(struct finstr_ard_format));
			fspm_template[cnt].dw0.b.set_feat_enh	= 0;
			fspm_template[cnt].dw0.b.fins_fuse 	    = 1;
			fspm_template[cnt].dw0.b.fins		    = 0;
			fspm_template[cnt].dw0.b.lins		    = 0;
			fspm_template[cnt].dw0.b.ard_loop_idx	= loop;
			//fspm_template[cnt].dw1.b.xfcnt_sel      = FINST_XFER_ONE_DU;
			fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_AUTO;
			//fspm_template[cnt].dw2.b.ard_schem_sel  = ARD_DISABLE; //idx;
            if(idx == ARD_TMPLT_SLC)
            {			
                fspm_template[cnt].dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
            }
#ifdef EH_ENABLE_2BIT_RETRY
            else if((idx == ARD_TMPLT_TOP) || (idx == ARD_TMPLT_MAX) || (idx == ARD_TMPLT_SPR))
            {
                fspm_template[cnt].dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_READ_LOW + (idx - ARD_TMPLT_TOP);
            }
#endif
            else
            {
                fspm_template[cnt].dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_READ_LOW + (idx - ARD_TMPLT_LOW);
            }

            cnt++;

			//HD read status to make sure ARDY
			//fspm_template[cnt] = ard_sts_tmpl;
			//fspm_template[cnt].dw0.b.fins_fuse	= 1;
			//fspm_template[cnt].dw0.b.lins		= 0;
			//fspm_template[cnt].dw0.b.ard_loop_idx	= loop;
			//cnt++;
            
#ifdef EH_ENABLE_2BIT_RETRY
            if ((idx == ARD_TMPLT_SLC) || (idx == ARD_TMPLT_TOP) || (idx == ARD_TMPLT_MAX) || (idx == ARD_TMPLT_SPR)) 
            {   
                //Postive of SD set feature for plan0
                ard_2bit_soft_p_tmpl.dw0.b.ard_loop_idx = loop;
                fspm_template[cnt] = ard_2bit_soft_p_tmpl;
                fspm_template[cnt].set_feature_data_low = (idx == ARD_TMPLT_SLC) ? (u32)sf_slc_bit_p_array[loop] : sf_bit_p_array[loop];
                cnt++;

                //Negative of SD set feature for plan0
                ard_2bit_soft_n_tmpl.dw0.b.ard_loop_idx = loop;
                fspm_template[cnt] = ard_2bit_soft_n_tmpl;
                fspm_template[cnt].set_feature_data_low = (idx == ARD_TMPLT_SLC) ? (u32)sf_slc_bit_n_array[loop] : sf_bit_n_array[loop];
                cnt++;

                //SD re-read
                memcpy(&fspm_template[cnt], &read_fins_templ, sizeof(struct finstr_ard_format));
                fspm_template[cnt].dw0.b.set_feat_enh  = 0;
                fspm_template[cnt].dw0.b.fins_fuse     = 1;
                fspm_template[cnt].dw0.b.fins          = 0;
                fspm_template[cnt].dw0.b.lins          = 0;
                fspm_template[cnt].dw0.b.ard_loop_idx  = loop;
                fspm_template[cnt].dw0.b.ndcmd_fmt_sel = (idx == ARD_TMPLT_SLC) ? FINST_NAND_FMT_SLC_SOFT_BIT_SBN_READ : (FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW + (idx - ARD_TMPLT_TOP));
                //fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_ONE_DU;
                fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_AUTO;
				//fspm_template[cnt].dw2.b.ard_schem_sel = ARD_DISABLE; //idx;				
				cnt++;
            }
#endif

            //Read status to make sure ARDY
            //fspm_template[cnt] = ard_sts_tmpl;
            //fspm_template[cnt].dw0.b.fins_fuse    = 1;
            //fspm_template[cnt].dw0.b.lins         = 0;
            //fspm_template[cnt].dw0.b.ard_loop_idx = loop;
            //cnt++;

			//HD reset vref change
			fspm_template[cnt] = ard_tmpl;
			cnt++;
            
            //HD reset for plane1
			if (nand_support_aipr()) 
            {
				fspm_template[cnt] = ard_aipr_p1_tmpl;
				cnt++;
			}
            
#ifdef EH_ENABLE_2BIT_RETRY
			if((idx == ARD_TMPLT_SLC) || (idx == ARD_TMPLT_TOP) || (idx == ARD_TMPLT_MAX) || (idx == ARD_TMPLT_SPR))
			{  
                //Postive of SD reset for plane0
                fspm_template[cnt] = ard_2bit_soft_p_tmpl;
                fspm_template[cnt].set_feature_data_low = 0x00000000;
                cnt++;

                //Negative of SD reset for plane0
                fspm_template[cnt] = ard_2bit_soft_n_tmpl;
                fspm_template[cnt].set_feature_data_low = 0x00000000;
                cnt++;                 
            }
#endif
            //Read status to W/A asic bug
            fspm_template[cnt] = ard_sts_tmpl;
            fspm_template[cnt].dw0.b.ard_loop_idx = loop;
            cnt++;
		}
	}

#else
//-----------------------------------1BIT RETRY----------------------------------// //tony 20200826
#ifndef EH_ENABLE_2BIT_RETRY
	for (idx = ARD_TMPLT_SLC; idx < ARD_TMPLT_TOP; idx++) 
    {
		u32 count = 0;
		u32* array = NULL;

		switch(idx) 
        {
    		case ARD_TMPLT_SLC:
    			count = RR_STEP_SLC;
    			break;
#if !QLC_SUPPORT
    		case ARD_TMPLT_LOW:
    		case ARD_TMPLT_UPR:
				if (nand_info.id[1] == 0x3E) {  //4T
					array = tsb_tlc_low_upr_89_rr_offset_256Gb;
				} else {
					array = tsb_tlc_low_upr_89_rr_offset_512Gb;
				}
     			//array = tsb_tlc_low_upr_89_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_MID:
				if (nand_info.id[1] == 0x3E) {  //4T
					array = tsb_tlc_mid_8a_rr_offset_256Gb;
				} else {
					array = tsb_tlc_mid_8a_rr_offset_512Gb;
				}
    			//array = tsb_tlc_mid_8a_rr_offset;
    			count = RR_STEP_XLC;
    			break;
#else
    		case ARD_TMPLT_LOW:
    			array = tsb_qlc_low_87_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_MID:
    			array = tsb_qlc_mid_88_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_UPR:
    			array = tsb_qlc_upr_89_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_TOP:
    			array = tsb_qlc_top_8a_rr_offset;
    			count = RR_STEP_XLC;
    			break;
#endif
		}
		if (count > FSPM_ARD_TEMPLATE_FCMD_CNT) 
        {
			count = FSPM_ARD_TEMPLATE_FCMD_CNT;
		}
		ard_conf_reg.b.ficu_ard_tmpl0_loop = count;
		ard_conf_reg.b.ficu_ard_tmpl0_offset_addr = cnt * sizeof(struct finstr_ard_format);
		ficu_writel(ard_conf_reg.all, FICU_ARD_CONF_REG1 - 4 + (idx << 2));


		ard_tmpl.dw1.b.set_feature_addr = ard_feature_addr[idx];
#if !QLC_SUPPORT
		struct finstr_ard_format ard_aipr_p1_tmpl = ard_tmpl; // Set feature template for AIPR plane 1
		if (nand_support_aipr()) 
        {
			ard_aipr_p1_tmpl.dw1.b.set_feature_addr = ard_feature_addr_p1[idx];
		}
#endif
		for (loop = 0; loop < count; loop++) 
        {
			// Set feature to change vref
			ard_tmpl.dw0.b.ard_loop_idx = loop;
			fspm_template[cnt] = ard_tmpl;
			fspm_template[cnt].dw0.b.fins = 1;
			if (idx == ARD_TMPLT_SLC) 
            {
				fspm_template[cnt].set_feature_data_low = tsb_slc_8b_rr_offset[loop];
			} 
            else 
            {
				fspm_template[cnt].set_feature_data_low = array[loop];
			}
			cnt++;
#if !QLC_SUPPORT
			if (nand_support_aipr()) 
            {
				ard_aipr_p1_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p1_tmpl;
				if (idx == ARD_TMPLT_SLC) 
                {
					fspm_template[cnt].set_feature_data_low = tsb_slc_8b_rr_offset[loop];
				} 
                else 
                {
					fspm_template[cnt].set_feature_data_low = array[loop];
				}
				cnt++;
			}
#endif

			// Re-read
			memcpy(&fspm_template[cnt], &read_fins_templ,
			       sizeof(struct finstr_ard_format));
			fspm_template[cnt].dw0.b.set_feat_enh	= 0;
			fspm_template[cnt].dw0.b.fins_fuse 	= 1;
			fspm_template[cnt].dw0.b.fins		= 0;
			fspm_template[cnt].dw0.b.lins		= 0;
#if QLC_SUPPORT
			fspm_template[cnt].dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_REREAD_SLC + (idx - ARD_TMPLT_SLC);
			//fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_ONE_DU;
            fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_AUTO;
#else
			fspm_template[cnt].dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_XLC_READ_LOW + (idx - ARD_TMPLT_LOW);
			//fspm_template[cnt].dw2.b.ard_schem_sel  = ARD_DISABLE; //idx;
			//fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_ONE_DU;
            fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_AUTO;
#endif

			fspm_template[cnt].dw0.b.ard_loop_idx	= loop;
			cnt++;

			// Read status to make sure ARDY
			fspm_template[cnt] = ard_sts_tmpl;
			fspm_template[cnt].dw0.b.fins_fuse	= 1;
			fspm_template[cnt].dw0.b.lins		= 0;
			fspm_template[cnt].dw0.b.ard_loop_idx	= loop;
			cnt++;

			// Reset vref change
			fspm_template[cnt] = ard_tmpl;
			cnt++;

#if !QLC_SUPPORT
			if (nand_support_aipr()) 
            {
				fspm_template[cnt] = ard_aipr_p1_tmpl;
				cnt++;
			}
#endif
			// Read status to W/A asic bug
			fspm_template[cnt] = ard_sts_tmpl;
			fspm_template[cnt].dw0.b.ard_loop_idx = loop;
			cnt++;
		}
	}
//-----------------------------------2BIT RETRY----------------------------------// //tony 20200805
#else 

	for (idx = ARD_TMPLT_SLC; idx < ARD_TMPLT_TOP; idx++) 
    {
    	u32  count = 0;
        u32  loop_hd = 0;
    	u32* array = NULL;
    	u32  sf_bit_p_array[RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL] = {0};
    	u32  sf_bit_n_array[RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL] = {0};
        //u8   sf_slc_bit_p_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
    	//u8   sf_slc_bit_n_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
    	u32   sf_slc_bit_p_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
        u32   sf_slc_bit_n_array[RR_STEP_SLC * SOFT_RETRY_LEVEL] = {0};
        u8   soft_idx = 0;
    	u8   hard_idx = 0;
        u8   soft_array_idx = 0;
        
		switch(idx) 
        {
    		case ARD_TMPLT_SLC:
                array = (u32 *)tsb_slc_8b_rr_offset; 
                for(hard_idx = 0; hard_idx < RR_STEP_SLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {
                        //soft_array_idx = (RR_STEP_SLC * soft_idx) + hard_idx;
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
            			//sf_slc_bit_p_array[soft_array_idx] = tsb_slc_8b_rr_offset[hard_idx] + tsb_soft_level_p[soft_idx];
                        //sf_slc_bit_n_array[soft_array_idx] = tsb_slc_8b_rr_offset[hard_idx] + tsb_soft_level_n[soft_idx];
            			sf_slc_bit_p_array[soft_array_idx] = (0xFF & ((u32)tsb_slc_8b_rr_offset[hard_idx] + (u32)tsb_soft_level_p[soft_idx]))|((0xFF << 8) & (((u32)tsb_slc_8b_rr_offset[hard_idx] + (u32)tsb_soft_level_n[soft_idx]) << 8));
                        sf_slc_bit_n_array[soft_array_idx] = sf_slc_bit_p_array[soft_array_idx];
                    }
                }
    			count = RR_STEP_SLC * SOFT_RETRY_LEVEL;
    			break;
#if !QLC_SUPPORT
    		case ARD_TMPLT_LOW:
    			//array = tsb_tlc_low_upr_89_rr_offset;
    			array = tsb_tlc_2bit_89_rr_offset;
                for(hard_idx = 0; hard_idx < RR_STEP_2BIT_XLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
                        sf_bit_p_array[soft_array_idx] = ((0xFF << 16) & (tsb_tlc_2bit_89_rr_offset[hard_idx] + ((u32)tsb_soft_level_p[soft_idx] << 16)));
                        sf_bit_n_array[soft_array_idx] = ((0xFF << 16) & (tsb_tlc_2bit_89_rr_offset[hard_idx] + ((u32)tsb_soft_level_n[soft_idx] << 16)));
                    }
                }
                count = RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL;
    			break; 
    		case ARD_TMPLT_MID:
    			//array = tsb_tlc_mid_8a_rr_offset;
    			array = tsb_tlc_2bit_89_rr_offset;
                for(hard_idx = 0; hard_idx < RR_STEP_2BIT_XLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {  
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
                        sf_bit_p_array[soft_array_idx] = (0xFF & (tsb_tlc_2bit_89_rr_offset[hard_idx] + (u32)tsb_soft_level_p[soft_idx])) | ((0xFF << 8) & (tsb_tlc_2bit_89_rr_offset[hard_idx] + ((u32)tsb_soft_level_p[soft_idx] << 8))) | ((0xFF << 24) & (tsb_tlc_2bit_89_rr_offset[hard_idx] + ((u32)tsb_soft_level_p[soft_idx] << 24)));
                        sf_bit_n_array[soft_array_idx] = (0xFF & (tsb_tlc_2bit_89_rr_offset[hard_idx] + (u32)tsb_soft_level_n[soft_idx])) | ((0xFF << 8) & (tsb_tlc_2bit_89_rr_offset[hard_idx] + ((u32)tsb_soft_level_n[soft_idx] << 8))) | ((0xFF << 24) & (tsb_tlc_2bit_89_rr_offset[hard_idx] + ((u32)tsb_soft_level_n[soft_idx] << 24)));
                    }
                }
                count = RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL;
    			break;
            case ARD_TMPLT_UPR:
                //array = tsb_tlc_low_upr_89_rr_offset;
                array = tsb_tlc_2bit_8a_rr_offset;
                for(hard_idx = 0; hard_idx < RR_STEP_2BIT_XLC; hard_idx++)
                {
                    for(soft_idx = 0; soft_idx < SOFT_RETRY_LEVEL; soft_idx++)
                    {
                        soft_array_idx = (SOFT_RETRY_LEVEL * hard_idx) + soft_idx;
                        sf_bit_p_array[soft_array_idx] = (0xFF & (tsb_tlc_2bit_8a_rr_offset[hard_idx] + (u32)tsb_soft_level_p[soft_idx])) | ((0xFF << 8) & (tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((u32)tsb_soft_level_p[soft_idx] << 8))) | ((0xFF << 16) & (tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((u32)tsb_soft_level_p[soft_idx] << 16)));
                        sf_bit_n_array[soft_array_idx] = (0xFF & (tsb_tlc_2bit_8a_rr_offset[hard_idx] + (u32)tsb_soft_level_n[soft_idx])) | ((0xFF << 8) & (tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((u32)tsb_soft_level_n[soft_idx] << 8))) | ((0xFF << 16) & (tsb_tlc_2bit_8a_rr_offset[hard_idx] + ((u32)tsb_soft_level_n[soft_idx] << 16)));
                    }
                }
                count = RR_STEP_2BIT_XLC * SOFT_RETRY_LEVEL;
                break;
#else
    		case ARD_TMPLT_LOW:
    			array = tsb_qlc_low_87_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_MID:
    			array = tsb_qlc_mid_88_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_UPR:
    			array = tsb_qlc_upr_89_rr_offset;
    			count = RR_STEP_XLC;
    			break;
    		case ARD_TMPLT_TOP:
    			array = tsb_qlc_top_8a_rr_offset;
    			count = RR_STEP_XLC;
    			break;
#endif
		}
        
		if (count > FSPM_ARD_TEMPLATE_FCMD_CNT) 
        {
			count = FSPM_ARD_TEMPLATE_FCMD_CNT;
		}
		ard_conf_reg.b.ficu_ard_tmpl0_loop = count;
		ard_conf_reg.b.ficu_ard_tmpl0_offset_addr = cnt * sizeof(struct finstr_ard_format);
		ficu_writel(ard_conf_reg.all, FICU_ARD_CONF_REG1 - 4 + (idx << 2));

//Set all of mode feature address
        //HD of plane0
		ard_tmpl.dw1.b.set_feature_addr = ard_feature_addr[idx];

#if !QLC_SUPPORT
        //HD of plane1
		struct finstr_ard_format ard_aipr_p1_tmpl = ard_tmpl; // Set feature template for AIPR plane 1
		struct finstr_ard_format ard_aipr_p2_tmpl = ard_tmpl; // Set feature template for AIPR plane 2
		struct finstr_ard_format ard_aipr_p3_tmpl = ard_tmpl; // Set feature template for AIPR plane 3
		if (nand_support_aipr())
        {      
            ard_aipr_p1_tmpl.dw1.b.set_feature_addr = ard_feature_addr_p1[idx];
            ard_aipr_p2_tmpl.dw1.b.set_feature_addr = ard_feature_addr_p2[idx];
            ard_aipr_p3_tmpl.dw1.b.set_feature_addr = ard_feature_addr_p3[idx];
		}
#endif
        //SD of plane0        
        struct finstr_ard_format ard_2bit_soft_p_tmpl = ard_tmpl;
        struct finstr_ard_format ard_2bit_soft_n_tmpl = ard_tmpl;
        //SD of plane1
        //struct finstr_ard_format ard_2bit_soft_p_tmpl_p1 = ard_tmpl;
        //struct finstr_ard_format ard_2bit_soft_n_tmpl_p1 = ard_tmpl;
        
        ard_2bit_soft_p_tmpl.dw1.b.set_feature_addr = ard_feature_addr_2bit_p[idx];
        ard_2bit_soft_n_tmpl.dw1.b.set_feature_addr = ard_feature_addr_2bit_n[idx];
                  
        //Set retry table
		for (loop = 0; loop < count; loop++) 
        {
			//HD set feature for plan0
			ard_tmpl.dw0.b.ard_loop_idx = loop;
			fspm_template[cnt] = ard_tmpl;
			fspm_template[cnt].dw0.b.fins = 1;
            //loop_hd = (idx == ARD_TMPLT_SLC) ? (loop % RR_STEP_SLC) : (loop % RR_STEP_XLC);
            loop_hd = (loop / SOFT_RETRY_LEVEL);
            fspm_template[cnt].set_feature_data_low = array[loop_hd];
            cnt++; 
            
#if !QLC_SUPPORT
			//HD set feature for plan1
			if (nand_support_aipr()) 
            {
				ard_aipr_p1_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p1_tmpl;
                loop_hd = (loop / SOFT_RETRY_LEVEL);
                fspm_template[cnt].set_feature_data_low = array[loop_hd];
				cnt++;
				ard_aipr_p2_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p2_tmpl;
                loop_hd = (loop / SOFT_RETRY_LEVEL);
                fspm_template[cnt].set_feature_data_low = array[loop_hd];
				cnt++;
				ard_aipr_p3_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p3_tmpl;
                loop_hd = (loop / SOFT_RETRY_LEVEL);
                fspm_template[cnt].set_feature_data_low = array[loop_hd];
				cnt++;
			}
#endif
            //HD re-read
			memcpy(&fspm_template[cnt], &read_fins_templ, sizeof(struct finstr_ard_format));
			fspm_template[cnt].dw0.b.set_feat_enh = 0;
			fspm_template[cnt].dw0.b.fins_fuse 	  = 1;
			fspm_template[cnt].dw0.b.fins		  = 0;
			fspm_template[cnt].dw0.b.lins		  = 0;
            fspm_template[cnt].dw0.b.ndcmd_fmt_sel = (idx == ARD_TMPLT_SLC) ? FINST_NAND_FMT_SLC_READ_CMD : (FINST_NAND_FMT_XLC_READ_LOW + (idx - ARD_TMPLT_LOW));
			//fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_ONE_DU;
			fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_AUTO;
			//fspm_template[cnt].dw2.b.ard_schem_sel  = ARD_DISABLE; //idx;		
			fspm_template[cnt].dw0.b.ard_loop_idx = loop;
			cnt++;

            //HD Read status to make sure ARDY
            //fspm_template[cnt] = ard_sts_tmpl;
            //fspm_template[cnt].dw0.b.fins_fuse    = 1;
            //fspm_template[cnt].dw0.b.lins         = 0;
            //fspm_template[cnt].dw0.b.ard_loop_idx = loop;
            //cnt++;

            //Postive of SD set feature for plan0
            ard_2bit_soft_p_tmpl.dw0.b.ard_loop_idx = loop;
            fspm_template[cnt] = ard_2bit_soft_p_tmpl;
            //fspm_template[cnt].set_feature_data_low = (idx == ARD_TMPLT_SLC) ? (u32)sf_slc_bit_p_array[loop] : sf_bit_p_array[loop];
            fspm_template[cnt].set_feature_data_low = (idx == ARD_TMPLT_SLC) ? sf_slc_bit_p_array[loop] : sf_bit_p_array[loop];
            cnt++;

            //Negative of SD set feature for plan0
            ard_2bit_soft_n_tmpl.dw0.b.ard_loop_idx = loop;
            fspm_template[cnt] = ard_2bit_soft_n_tmpl;
            //fspm_template[cnt].set_feature_data_low = (idx == ARD_TMPLT_SLC) ? (u32)sf_slc_bit_n_array[loop] : sf_bit_n_array[loop];
            fspm_template[cnt].set_feature_data_low = (idx == ARD_TMPLT_SLC) ? sf_slc_bit_n_array[loop] : sf_bit_n_array[loop];
            cnt++;
                        
            //SD re-read
			memcpy(&fspm_template[cnt], &read_fins_templ, sizeof(struct finstr_ard_format));
			fspm_template[cnt].dw0.b.set_feat_enh = 0;
			fspm_template[cnt].dw0.b.fins_fuse 	  = 1;
			fspm_template[cnt].dw0.b.fins		  = 0;
			fspm_template[cnt].dw0.b.lins		  = 0;
			fspm_template[cnt].dw0.b.ard_loop_idx = loop;
		    fspm_template[cnt].dw0.b.ndcmd_fmt_sel = (idx == ARD_TMPLT_SLC) ? FINST_NAND_FMT_SLC_SOFT_BIT_SBN_READ : (FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW + (idx - ARD_TMPLT_LOW));
			//fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_ONE_DU;
			fspm_template[cnt].dw1.b.xfcnt_sel     = FINST_XFER_AUTO;
            //fspm_template[cnt].dw2.b.ard_schem_sel  = ARD_DISABLE; //idx;	
			cnt++;

            //SD Read status to make sure ARDY
			//fspm_template[cnt] = ard_sts_tmpl;
			//fspm_template[cnt].dw0.b.fins_fuse    = 1;
			//fspm_template[cnt].dw0.b.lins		  = 0;
			//fspm_template[cnt].dw0.b.ard_loop_idx = loop;
			//cnt++;
           
            //HD reset for plane0
			ard_tmpl.dw0.b.ard_loop_idx = loop;
			fspm_template[cnt] = ard_tmpl;
            fspm_template[cnt].set_feature_data_low = 0x00000000;
            cnt++;           
#if !QLC_SUPPORT
            //HD reset for plane1
			if (nand_support_aipr()) 
            {
				ard_aipr_p1_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p1_tmpl;
                fspm_template[cnt].set_feature_data_low = 0x00000000;
				cnt++;
                ard_aipr_p2_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p2_tmpl;
                fspm_template[cnt].set_feature_data_low = 0x00000000;
				cnt++;
                ard_aipr_p3_tmpl.dw0.b.ard_loop_idx = loop;
				fspm_template[cnt] = ard_aipr_p3_tmpl;
                fspm_template[cnt].set_feature_data_low = 0x00000000;
				cnt++;
			}
#endif            
            //HD Read status to W/A asic bug
			//fspm_template[cnt] = ard_sts_tmpl;
			//if(idx != ARD_TMPLT_SLC)
			//{
			//	fspm_template[cnt].dw0.b.fins_fuse	  = 1;
			//	fspm_template[cnt].dw0.b.lins		  = 0;
			//}
			//fspm_template[cnt].dw0.b.ard_loop_idx = loop;
			//cnt++;

            //Postive of SD reset for plane0
			fspm_template[cnt] = ard_2bit_soft_p_tmpl;
            fspm_template[cnt].set_feature_data_low = 0x00000000;
			cnt++;
            
            //Negative of SD reset for plane0
			fspm_template[cnt] = ard_2bit_soft_n_tmpl;
            fspm_template[cnt].set_feature_data_low = 0x00000000;
			cnt++; 
                            
            //SD Read status to W/A asic bug
			fspm_template[cnt] = ard_sts_tmpl;
			fspm_template[cnt].dw0.b.ard_loop_idx = loop;
			cnt++;

		}
	}

#endif
#endif
	sys_assert(cnt <= (sizeof(fspm_usage_ptr->ard_template) / sizeof(fspm_usage_ptr->ard_template[0])));

}
#endif
