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

/*! \file
 * @brief firmwre download library
 *
 * \addtogroup srb
 *
 * \defgroup fw_download
 * \ingroup srb
 * @{
 *
 * any disk should be able to use firmware download api in this library
 */

#include "types.h"
#include "sect.h"
#include "string.h"
#include "bf_mgr.h"
#include "queue.h"
#include "crc32.h"
#include "ncl.h"
#include "ncl_exports.h"
#include "event.h"
#include "req.h"
#include "nvme_spec.h"
#include "nvme_apl.h"
#include "srb.h"
#include "fw_download.h"
#include "cpu_msg.h"
#include "misc.h"
#include "stdlib.h"
#include "lib.h"
#include "ddr.h"
#include "hal_nvme.h"
#include "eccu.h"
/*! \cond PRIVATE */
#define __FILEID__ fwdl
#include "trace.h"
/*! \endcond */
#include "mpc.h"
#ifdef UPDATE_LOADER
	slow_data_zi rebuild_srb_para_t srb_para;
	extern bool sb_update_idx;
	extern bool sb_always_update_idx;
	extern u32 slc_pgs_per_blk;
	rda_t srb_rda;
	rda_t srb_rda_buf;
	rda_t sb_rda;
	rda_t sb_rda_dual;
	rda_t sb_rda_buf;
	rda_t sb_rda_buf_dual;
	u32 srb_sb_du_cnt;
	//rda_t sb_rda_mirror;
	//rda_t sb_rda_dual_mirror;
	//static srb_t *srb = (srb_t *) SRAM_BASE;
	//slow_data u32 sb_du_amt = 0;
#endif
/*!
 * @brief dowload fw status of definitions
 */
enum fw_dw_status_st {
	DW_ST_INIT = 0,			///< download fw init
	DW_ST_WAIT_FW,			///< download fw wait fw come in
	DW_ST_TRANSFER_FW,		///< downloading fw
	DW_ST_TRANSFER_COMPLETE,	///< fw download complete
	DW_ST_COMMIT_FW,		///< commit fw
	DW_ST_VERIFY_FW,		///< verify fw
	DW_ST_VERIFY_FW_ERR,		///< fw verify error
	DW_ST_UPGRADE_FW_COMPLETE,	///< fw upgrade complete
};

slow_data_zi bool use_old_fw;
slow_data_zi download_fw_t upgrade_fw;		///< fw upgrade structure
#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
share_data_zi volatile bool _fg_pi_sus_io;
share_data_zi volatile bool _fg_pi_resum_io;
slow_data_zi static volatile u8 Receive_LDR = true; //check loader or mainfw, 1:loader, 0:mainfw
slow_data_zi static volatile u8 Secure_sign_correct =0;//have secure info?
slow_data_zi u8 key[32] __attribute__ ((aligned(32))); //save loader key sha3
slow_data_zi u8 sign[32] __attribute__ ((aligned(32)));//save loader FW sha3
slow_data_zi u8 result_key[32] __attribute__ ((aligned(32))); //save main key sha3
slow_data_zi u8 result_sign[32] __attribute__ ((aligned(32)));//save main FW sha3
slow_data const u8 golden_key_digest[32] = {0xB1, 0x7B, 0xD7, 0x1A, 0x63, 0xB3, 0x86, 0xC7,
                                           0xB7, 0x21, 0xA7, 0x0F, 0xFA, 0xF2, 0x06, 0x65,
                                           0xC2, 0x25, 0x89, 0x8E, 0xD5, 0x63, 0xDA, 0xB6,
                                           0x13, 0x35, 0xD6, 0xB0, 0xED, 0xF7, 0xDD, 0xF8};
ddr_sh_data u8 cmf_idx;	//default : MR mode
slow_data_zi bool fw_update_first_page_get = false;
#endif
extern bool ncl_enter_mr_mode(void);
extern int ncl_cmd_simple_submit(rda_t *rda_list, enum ncl_cmd_op_t op,
		bm_pl_t *dtag, u32 count,
		int du_format, int stripe_id);
extern void ncl_leave_mr_mode(void);

typedef QSIMPLEQ_HEAD(_fwdl_req_q_t, _fwdl_req_t) fwdl_req_q_t;  //3.1.7.4 merged 20201201 Eddie

extern fw_img_st_t fw_image_status;
extern bool offset_backward;
extern u32 oldoffset;
#if defined(MPC)
fast_data u8 evt_fwdl_req = 0xFF;
fast_data_zi fwdl_req_q_t fwdl_req_q;
share_data_zi volatile u8 fw_prplist_trx_done;
ddr_sh_data bool _fg_prp_list;
#endif
ddr_sh_data u32 evt_delay_dtag_start;
#if GC_SUSPEND_FWDL
share_data_zi volatile u32 fwdl_gc_handle_dtag_start;
#endif
static int fwdl_calc_image_data_callback(void *ctx, dpe_rst_t *rst)
{
	srb_apl_trace(LOG_INFO, 0x2b2b, "Image calc complete %x", *(u32 *)upgrade_fw.s_result);
	return 0;
}

static int fwdl_calc_public_key_callback(void *ctx, dpe_rst_t *rst)
{
	srb_apl_trace(LOG_INFO, 0xd201, "public key calc complete %x", *(u32 *)upgrade_fw.k_result);
	if (upgrade_fw.public_key != NULL) {
		dtag_put(DTAG_T_SRAM, mem2dtag(upgrade_fw.public_key));
		upgrade_fw.public_key = NULL;
	}
	return 0;
}

/*!
 * @brief check fw image section for secure boot
 *
 * @param image		fw image
 *
 * @return		check result
 */
slow_code static bool fwdl_check_secure_section(loader_image_t *image) // 20201126-YC
{
	u32 i;
	u8 *pk_se = NULL;
	u32 pk_se_len = 0;
	u8 *pk_otp = NULL;
	u8 *fw_sha3_256 = NULL;
	u8 *fw_sha3_256_sha256 = NULL;
	u8 *fw_sign = NULL;
	//u8 *fw_za = NULL;
	u8 fw_rsa_sm = NONE_SECURITY_MODE;
	u8 sign_cnt = 0;
	//Andy_security_sign 20201130, fw len = offset
	u32 fw_len = 0;//sizeof(loader_image_t) + image->section_num * sizeof(loader_section_t);
	u8 *ep = (u8 *)image;

	upgrade_fw.calc_image_len = 0;
	if ((image->signature != IMAGE_SIGNATURE) ||
        (crc32(image->sections, sizeof(loader_section_t) * image->section_num) != image->section_csum))
    {
		//srb_apl_trace(LOG_ERR, 0, "Image sign:[0x%x != 0x%x] CRC:[0x%x != 0x%x] fail", image->signature, IMAGE_SIGNATURE,
			//crc32(image->sections, sizeof(loader_section_t) * image->section_num), image->section_csum);

		srb_apl_trace(LOG_ERR, 0xaa0e, "Image sign:[0x%x != 0x%x] sec_num: %d CRC:0x%x ", image->signature, IMAGE_SIGNATURE,
			image->section_num, image->section_csum);

		return false;
	}

	if (upgrade_fw.security_enable == SECURITY_DISABLE)
    {
		srb_apl_trace(LOG_ERR, 0xb999, "check sec disable");
		return true;
	}
    // PART1 &=======================================================================================================


    // PART2 *=======================================================================================================
    // part 2.1 Parsing Security Sections
	for (i = 0; i < image->section_num; i++)
    {
		loader_section_t *section = &image->sections[i];
		#if 0
		if (section->pma != INVALID_PMA)
			fw_len += section->length;
		#endif

		if (section->identifier == ID_PKEY)
        {
			pk_se = (u8 *)(ep + section->offset);
			pk_se_len = section->length;
			//#point 1, offset = fw_len
			fw_len = section->offset;
			if (pk_se_len > 4096)
				return false;
			continue;
		}

		if (section->identifier == ID_KOTP)
        {
			pk_otp = (u8 *)(ep + section->offset);
			if (section->length != 32)
				return false;
			continue;
		}
		if (section->identifier == ID_SHA3_256)
        {
			fw_sha3_256 = (u8 *)(ep + section->offset);
			if (section->length != 32)
				return false;
			continue;
		}

		if (section->identifier == ID_SHA256)
        {
			fw_sha3_256_sha256 = (u8 *)(ep + section->offset);
			if (section->length != 32)
				return false;
			continue;
		}

		if (section->identifier == ID_SIGN)
        {
			fw_sign = (u8 *)(ep + section->offset);
			fw_rsa_sm = RS_SECURITY_MODE;
			if (section->length != 256)
				return false;
			++sign_cnt;
			continue;
		}

        //Benson Comment
        //Currently only use RSA2048 so to disable SM
        /*
		if (section->identifier == ID_SM2_ZA) {
			fw_za = (u8 *)(ep + section->offset);
			if (section->length != 32)
				return false;
			continue;
		}

		if (section->identifier == ID_SM2_SIGN) {
			fw_sign = (u8 *)(ep + section->offset);
			fw_rsa_sm = SM_SECURITY_MODE;
			if (section->length != 64)
				return false;
			++sign_cnt;
			continue;
		}
		*/
	}

    // part 2.2 Check Security Sections Content
	/* if we get more than one sign section, go to recovery mode */
	if (sign_cnt > 1)
		return false;
	/* when boot fw, if secure mode is not the same as it in SRB, goto recovery mode */
	//if (fw_rsa_sm != upgrade_fw.security_mode)
	//	return false;

	/* Is OTP programmed pub key otherwise that's the first time for programmer */
	//bool v1 = otp_get_pk(0, pk1_otp);
	//bool v2 = otp_get_pk(1, pk2_otp);
	bool lost = false;
	if (fw_rsa_sm == RS_SECURITY_MODE)
    {
		lost = (pk_se == NULL || pk_otp == NULL || fw_sha3_256 == NULL || fw_sha3_256_sha256 == NULL || fw_sign == NULL);
	}
    else
    {
        lost = true;
    }
    //Benson Comment
    //Only use RSA2048
    /*
    else
    {
		lost = (pk_se == NULL || pk_otp == NULL || fw_za == NULL || fw_sign == NULL);
	}
	*/

    // If Security is enabled
    // All security sections must exist.
	if (lost)
    {
		return false;
	}

    #if 0
	if(Receive_LDR == 1)
	{
		if (sign != NULL)
			sys_free_aligned(SLOW_DATA, sign);
		if (key != NULL)
			sys_free_aligned(SLOW_DATA, key);
		sign = NULL;
		key = NULL;

		sign = sys_malloc_aligned(SLOW_DATA, 32, 32);
		sys_assert(sign != NULL);
		memset(sign, 0, 32);

		key = sys_malloc_aligned(SLOW_DATA, 32, 32);
		sys_assert(key != NULL);
		memset(key, 0, 32);

	}
    #else
    //memset(sign, 0, 32);
    //memset(key, 0, 32);
    #endif

    //Benson Comment
    //Will allocate a DDR space to store s_result and k_result
    #if 0
	upgrade_fw.s_result = sys_malloc_aligned(SLOW_DATA, 32, 32);
	sys_assert(upgrade_fw.s_result != NULL);
	memset(upgrade_fw.s_result, 0, 32);
	upgrade_fw.k_result = sys_malloc_aligned(SLOW_DATA, 32, 32);
	sys_assert(upgrade_fw.k_result != NULL);
	memset(upgrade_fw.k_result, 0, 32);
    #else
    if (Receive_LDR)
    {
	    upgrade_fw.s_result = &sign[0];
	    upgrade_fw.k_result = &key[0];
    }
    else
    {
	    upgrade_fw.s_result = &result_sign[0];
	    upgrade_fw.k_result = &result_key[0];
    }
	memset(upgrade_fw.s_result, 0, 32);
	memset(upgrade_fw.k_result, 0, 32);
    #endif

    // part 2.3 Calculate Hash Value for FW
	upgrade_fw.calc_image_len = fw_len;
	upgrade_fw.calc_pkey_len = pk_se_len;

	fw_len = min(DTAG_SZE, fw_len);

	//if (fw_rsa_sm == RS_SECURITY_MODE)
    //{
		bm_sha3_sm3_calc_part(ep, upgrade_fw.s_result, false, upgrade_fw.calc_image_len, fw_len, fwdl_calc_image_data_callback, NULL, true);
	//}
    //else
    //{
	//	bm_sha3_sm3_calc_part(ep, upgrade_fw.s_result, true, upgrade_fw.calc_image_len, fw_len, fwdl_calc_image_data_callback, NULL, true);
	//}

	srb_apl_trace(LOG_INFO, 0xcbe3, "check sec ok fw len:%x", upgrade_fw.calc_image_len);
	upgrade_fw.calc_image_len -= fw_len;

	////Andy_security_sign 20201130 test code
	Secure_sign_correct = 1;

	return true;
}

/*!
 * @brief check fw image data for secure boot
 *
 * @param image		fw image data
 *
 * @return		None
 */
slow_code static void fwdl_check_secure_data(void *image) // 20201126-YC
{
	u32 fw_len;
	u32 pkey_len;

	if (upgrade_fw.security_enable == SECURITY_DISABLE)
		return;

    // condition 1
	if (upgrade_fw.calc_image_len == 0)
    {
		if (upgrade_fw.calc_pkey_len != 0)
        {
			pkey_len = min(upgrade_fw.calc_pkey_len, DTAG_SZE);
			memcpy(upgrade_fw.public_key, image, pkey_len);
			pkey_len += (u32)upgrade_fw.public_key & DTAG_MSK;
			upgrade_fw.public_key = (void *)((u32)upgrade_fw.public_key & (~DTAG_MSK));
			//if (upgrade_fw.security_mode == RS_SECURITY_MODE) {
				bm_sha3_sm3_calc_part(upgrade_fw.public_key, upgrade_fw.k_result, false, pkey_len, pkey_len, fwdl_calc_public_key_callback, NULL, true);
			//} else {
			//	bm_sha3_sm3_calc_part(upgrade_fw.public_key, upgrade_fw.k_result, true, pkey_len, pkey_len, fwdl_calc_public_key_callback, NULL, true);
			//}
			upgrade_fw.calc_pkey_len = 0;
		}
		return;
	}

    // condition 2
	fw_len = min(upgrade_fw.calc_image_len, DTAG_SZE);

	//if (upgrade_fw.security_mode == RS_SECURITY_MODE) {
		bm_sha3_sm3_calc_part(image, upgrade_fw.s_result, false, upgrade_fw.calc_image_len, fw_len, fwdl_calc_image_data_callback, NULL, false);
	//} else {
	//	bm_sha3_sm3_calc_part(image, upgrade_fw.s_result, true, upgrade_fw.calc_image_len, fw_len, fwdl_calc_image_data_callback, NULL, false);
	//}
	upgrade_fw.calc_image_len -= fw_len;

	if (upgrade_fw.calc_image_len == 0)
    {
		dtag_t dtag;
		void *mem;

		dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);

		sys_assert(dtag.dtag != _inv_dtag.dtag);
		upgrade_fw.public_key = mem;

		pkey_len = min(upgrade_fw.calc_pkey_len, DTAG_SZE - fw_len);

		memcpy(mem, image + fw_len, pkey_len);

		upgrade_fw.calc_pkey_len -= pkey_len;

		if (upgrade_fw.calc_pkey_len == 0)
        {
			//if (upgrade_fw.security_mode == RS_SECURITY_MODE) {
				bm_sha3_sm3_calc_part(mem, upgrade_fw.k_result, false, pkey_len, pkey_len, fwdl_calc_public_key_callback, NULL, true);
			//} else {
			//	bm_sha3_sm3_calc_part(mem, upgrade_fw.k_result, true, pkey_len, pkey_len, fwdl_calc_public_key_callback, NULL, true);
			//}
		} else
        {
			upgrade_fw.public_key += pkey_len;
        }
	}
}

/*!
 * @brief check public key otp for secure boot
 *
 * @param image		fw image
 *
 * @return		check result
 */
slow_code static bool fwdl_check_public_key_otp(void) // 20201126-YC
{
#if 0
	u32 pk_opt[8];

	if (otp_get_pk(0, (u32 *)pk_opt) == false) {
		srb_apl_trace(LOG_ERR, 0xfb53, "get loader public key otp fail");
		return false;
	}
#endif
#if 0
	if(mode == 2)
	{
			if (memcmp(pk_opt, key, PUB_KEY_DIGEST_LEN)) 
			{
				srb_apl_trace(LOG_ERR, 0x4da3, "cmp public key otp 0 fail 0x%x 0x%x", *(u32 *)key, pk_opt[0]);
				otp_get_pk(1, (u32 *)pk_opt);
				if (memcmp(pk_opt, key, PUB_KEY_DIGEST_LEN)) 
				{
					srb_apl_trace(LOG_ERR, 0x41ba, "cmp public key otp 1 fail 0x%x 0x%x", *(u32 *)key, pk_opt[0]);
					return false;
				}
			}
	}
	else
#endif
#if 0
	if (memcmp(pk_opt, upgrade_fw.k_result, PUB_KEY_DIGEST_LEN))
    {
		srb_apl_trace(LOG_ERR, 0x26c4, "cmp public key otp 0 fail 0x%x 0x%x", *(u32 *)upgrade_fw.k_result, pk_opt[0]);
		otp_get_pk(1, (u32 *)pk_opt);
		if (memcmp(pk_opt, upgrade_fw.k_result, PUB_KEY_DIGEST_LEN))
        {
			srb_apl_trace(LOG_ERR, 0xe6c8, "cmp public key otp 1 fail 0x%x 0x%x", *(u32 *)upgrade_fw.k_result, pk_opt[0]);
			return false;
		}
	}
	return true;
#else
    //Benson Modify
    if (memcmp(&golden_key_digest[0], upgrade_fw.k_result, PUB_KEY_DIGEST_LEN))
    {
        srb_apl_trace(LOG_ERR, 0xa041, "Golden Key Addr = 0x%x", &golden_key_digest[0]);
        for (int i=0; i<32; i++)
        {
            srb_apl_trace(LOG_ERR, 0xdb5e, "CMP PK_Digest fail 0x%x 0x%x", *(u8 *)(upgrade_fw.k_result+i), golden_key_digest[i]);
        }
        return false;
    }
    else
        return true;
#endif
}
#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie
extern u32 fcmd_outstanding_cnt;
#endif
slow_code bool fwdl_signature_chk()
{
    //============================================================================
    // Check Digital Signature for Main FW
    //============================================================================
    if (upgrade_fw.security_enable != SECURITY_DISABLE)
    {
        rda_t rda = upgrade_fw.fw_dw_buffer_rda;

        //1st : check Public key correct or not
        if (fwdl_check_public_key_otp() == false)
        {
            upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
            srb_apl_trace(LOG_ALW, 0x7a2f, "transfer fw image complete, but pk digest error");
            return false;
        }

        srb_apl_trace(LOG_ALW, 0x9b93, "Verify Public Key Digest PASS!!");

        //2nd : verify loader and main FW
        if (upgrade_fw.security_mode == RS_SECURITY_MODE)
        {


            //verify loader
            srb_apl_trace(LOG_ALW, 0xc2a6, "Start to verify loader Signature...");
            rda = upgrade_fw.fw_dw_buffer_rda;

            if (srb_image_verify_sha3(rda, sign, LDR_Mode) == false)
            {
                upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
                srb_apl_trace(LOG_ALW, 0x00ac, "transfer LDR complete, but RSA error");
                return false;
            }
            else
                srb_apl_trace(LOG_ALW, 0xa9b6, "check fw LDR security PASS!!!");


            srb_apl_trace(LOG_ALW, 0xbb0a, "Start to verify main fw Signature...");
            // verify main fw
            ///add last variable to identify loader or main FW, 0:mainfw, 2:loader
            // Compiler MUST place main FW aligned to 12KB (MR page = 12KB)
            rda.row += NR_PAGES_SLICE(upgrade_fw.image_dus * SRB_MR_DU_SZE);
            //fw_verify = srb_image_verify_sha3(rda, result_sign, MAIN_Mode);

            if (srb_image_verify_sha3(rda, result_sign, MAIN_Mode) == false)
            {
                upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
                srb_apl_trace(LOG_ALW, 0x6112, "transfer Main complete, but RSA error");
                return false;
            }
            else
                srb_apl_trace(LOG_ALW, 0xfefb, "check Main security PASS!!!");
        }
    }

    return true;
}
extern volatile u8 plp_trigger;
#ifdef FW_SECURITY_REVISION
slow_data_zi u32 fw_sec_ver_h;
slow_data_zi u32 fw_sec_ver_l;
slow_data_zi u32 ssig_end;
slow_data_zi u32 sec_chk_du_idx;
#endif

/*!
 * @brief download firmware command event handler function
 *
 * @param req		firmware download request
 *
 * @return		true for handled done, always return true
 */
slow_code bool fwdl_download(fwdl_req_t *req) // 20201126-YC
{
	dtag_t *dtags = req->field.download.dtags;
	u32 count = req->field.download.count;
	u8 i, j;
	u8 cur_sb_dus;

	if(plp_trigger)
	{
		return true;
	}	
#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie
	if (_fg_pi_sus_io == false){
		if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
		}
		else if (cmf_idx == 3){	//Download MR CMF
			while(fcmd_outstanding_cnt != 0) {
				evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
				ficu_done_wait();
			}
		#if defined(HMETA_SIZE)	
			eccu_dufmt_switch(0);
			eccu_switch_cmf(0);
		#endif	
			evlog_printk(LOG_ALW,"512_8 to MR");
		}
		else if (cmf_idx == 4){	//Download MR CMF
			while(fcmd_outstanding_cnt != 0) {
				evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
				ficu_done_wait();
			}
		#if defined(HMETA_SIZE)	
			eccu_dufmt_switch(0);
			eccu_switch_cmf(0);
		#endif	
			evlog_printk(LOG_ALW,"4096_8 to MR");
		}
		_fg_pi_sus_io = true;
	}
#endif

	if ((upgrade_fw.fw_dw_status == DW_ST_UPGRADE_FW_COMPLETE) ||
		(upgrade_fw.fw_dw_status == DW_ST_VERIFY_FW_ERR) ||
		(upgrade_fw.fw_dw_status == DW_ST_TRANSFER_COMPLETE))
	{
		//srb_free_block((dft_btmp_t *)defect_bitmap, &upgrade_fw.fw_dw_buffer_rda);
		upgrade_fw.sb_dus_amt = 0;
		upgrade_fw.fw_dw_status = DW_ST_INIT;
		sb_update_idx = false;
        /*
		if (upgrade_fw.s_result != NULL)
			sys_free_aligned(SLOW_DATA, upgrade_fw.s_result);
		if (upgrade_fw.k_result != NULL)
			sys_free_aligned(SLOW_DATA, upgrade_fw.k_result);
	    */
		upgrade_fw.s_result = NULL;
		upgrade_fw.k_result = NULL;
		offset_backward = false;
	}
	else if(offset_backward)
	{
		upgrade_fw.sb_dus_amt = 0;
		upgrade_fw.fw_dw_status = DW_ST_INIT;
		sb_update_idx = false;

		upgrade_fw.s_result = NULL;
		upgrade_fw.k_result = NULL;

		evlog_printk(LOG_ALW, "offset backward happen, reset state machine");
		offset_backward = false;
		if(fw_update_first_page_get)
		{
			dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus);
			for (int i = 0; i< DOWNLOAD_MR_DTAG_CNT_PAGE; i++)
			{
				evlog_printk(LOG_ALW,"Retrive Dtag %d",upgrade_fw.sb_dus[i].dtag);
			}
		}
		fw_update_first_page_get = false;
	}

    // Parsing 1st 4K Image Data
	if (upgrade_fw.fw_dw_status == DW_ST_INIT)
    {
		//ret &= srb_alloc_block(defect_bitmap, true, &upgrade_fw.fw_dw_buffer_rda);
		loader_image_t *image = (loader_image_t *) dtag2mem(dtags[0]);
		//srb_apl_trace(LOG_ERR, 0, "oldoffset %d", oldoffset);
		// check FW support by name // 2023.10.18 Howard
		if(oldoffset == 0){
			//the position of fw_name in FW image
			char *fw_name = (char*)&((image+9)->signature); 
			char fw_char[] = {*fw_name, *(fw_name+1),*(fw_name+2),
			                  *(fw_name+3),*(fw_name+4),*(fw_name+5),
			                  *(fw_name+6),'\0'};
            char subversion = (char)*((char*)(image+5) + sizeof(loader_image_t) - 1); 

			//now FW name
			char fr[9]; 
			memcpy(fr, FR, 8);
            fr[8] = '\0';   
			if(fr[0] != fw_char[0] || fr[1] != fw_char[1] || fr[2] != fw_char[2]){
				evlog_printk(LOG_ALW, "FW now %s Doesn't support %s ver", FR, fw_char);
				goto invalid;
			}
            
            for (i = 0; i < 7; i++)
            {
                if (fw_char[i] != fr[i])
                    break;
            }

            if (i >= 5)
            {
                if(((fw_char[5] == '0') && (fr[5] == '0')) && (fw_char[6] < fr[6])){
                    evlog_printk(LOG_ALW, "FW now %s Doesn't support FW reduction ver %s ", FR, fw_char);
                    goto invalid;
                }
            }

            use_old_fw = false;
            if((i == 7) && (subversion < fr[i]))
            {
                use_old_fw = true;
            }

            evlog_printk(LOG_ALW, "fw_char:%s, subversion:%c, fr:%s, subversion:%c, use_old_fw:%u", fw_char, subversion, fr, fr[7], use_old_fw);
		}
        // check combo FW signature
		if ((image->signature != IMAGE_COMBO) && (image->signature != IMAGE_CMFG))
        {
			evlog_printk(LOG_ALW, "Image SIG (0x%x) <- Exp. (0x%x) fail", image->signature, IMAGE_SIGNATURE);
invalid:
			upgrade_fw.sb_dus_amt = 0;  //3.1.7.4 merged 20201201 Eddie
			fw_image_status = FW_IMG_INVALID;	//FW image is bad
			req->status = ((NVME_SCT_COMMAND_SPECIFIC << 8) | NVME_SC_INVALID_FIRMWARE_IMAGE);
			#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie	
				if (_fg_pi_resum_io == true){
					if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
					}
					else if (cmf_idx == 3){	//Download  512+8 CMF
						while(fcmd_outstanding_cnt != 0) {
							evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
							ficu_done_wait();
						}
					#if defined(HMETA_SIZE)
						eccu_dufmt_switch(3);
						eccu_switch_cmf(3);
					#endif
						evlog_printk(LOG_ALW,"MR to 512_8");
					}
					else if (cmf_idx == 4){	//Download  4086+8 CMF
						while(fcmd_outstanding_cnt != 0) {
							evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
							ficu_done_wait();
						}
					#if defined(HMETA_SIZE)	
						eccu_dufmt_switch(4);
						eccu_switch_cmf(4);
					#endif	
						evlog_printk(LOG_ALW,"MR to 4096_8");
					}
				}
			#endif
			if (_fg_prp_list)	
				fw_prplist_trx_done = 1;	//Unlock prp list transfer
			return true;
		}
	#ifdef UPDATE_LOADER
		srb_para.sb_start = image->fw_slice[0].slice_start;
		srb_para.sb_end = image->fw_slice[0].slice_end;
	#endif
		upgrade_fw.image_dus = image->image_dus;
		upgrade_fw.mfw_dus = image->fw_slice[1].slice_end - image->fw_slice[1].slice_start + 1;
		upgrade_fw.fw_start = image->fw_slice[1].slice_start;
		srb_apl_trace(LOG_ALW, 0x699f, "alloc download buffer Row(0x%x)@CH/CE(%d/%d)",
			upgrade_fw.fw_dw_buffer_rda.row, upgrade_fw.fw_dw_buffer_rda.ch, upgrade_fw.fw_dw_buffer_rda.dev);
		if (!ncl_enter_mr_mode()) {
			upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
			evlog_printk(LOG_ALW, "download enter mr mode fail");
			#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie	
				if (_fg_pi_resum_io == true){
					if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
					}
					else if (cmf_idx == 3){	//Download  512+8 CMF
						while(fcmd_outstanding_cnt != 0) {
							evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
							ficu_done_wait();
						}
					#if defined(HMETA_SIZE)
						eccu_dufmt_switch(3);
						eccu_switch_cmf(3);
					#endif
						evlog_printk(LOG_ALW,"MR to 512_8");
					}
					else if (cmf_idx == 4){	//Download  4086+8 CMF
						while(fcmd_outstanding_cnt != 0) {
							evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
							ficu_done_wait();
						}
					#if defined(HMETA_SIZE)	
						eccu_dufmt_switch(4);
						eccu_switch_cmf(4);
					#endif	
						evlog_printk(LOG_ALW,"MR to 4096_8");
					}
				}
			#endif
			if (_fg_prp_list)
				fw_prplist_trx_done = 1;	//Unlock prp list transfer
			return true;
		}
		ncl_cmd_simple_submit(&upgrade_fw.fw_dw_buffer_rda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
		ncl_leave_mr_mode();
		sys_assert(dtag_get_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus) == DOWNLOAD_MR_DTAG_CNT_PAGE);
		fw_update_first_page_get = true;
		upgrade_fw.fw_crc = ~0U;
		upgrade_fw.fw_dw_status = DW_ST_WAIT_FW;
	}

	upgrade_fw.fw_dw_status = DW_ST_TRANSFER_FW;
	for (i = 0; i < count; i++)
    {
		void *mem = dtag2mem(dtags[i]);
	#ifdef FW_SECURITY_REVISION
	#if 1
		if(((*((u32 *)mem)) == IMAGE_SIGNATURE) && (upgrade_fw.sb_dus_amt > 1))
		{
			u32 *pt_find_section = (u32 *)mem;
			for(u16 ofst=0; ofst<1024; ofst+=4)
			{
				if(pt_find_section[ofst] == SSIG_SIGNATURE)
				{
					ssig_end = pt_find_section[ofst+1] + (upgrade_fw.sb_dus_amt*0x1000) + 0x100;
					sec_chk_du_idx = ssig_end/0x1000;
					srb_apl_trace(LOG_ERR, 0x8657, "main FW SSIG: endOfst: 0x%x, DU idx: %d", ssig_end, sec_chk_du_idx);
					ssig_end %= 0x1000;
					break;
				}
			}
		}
		if((upgrade_fw.sb_dus_amt > 0) && (sec_chk_du_idx == upgrade_fw.sb_dus_amt))
		{
			u32 *pt_sec_rev_sig;
			if(((u32)mem+ssig_end) % 4)
				pt_sec_rev_sig = (u32 *)(((u32)mem+ssig_end) & 0xFFFFFFFC);
			else
				pt_sec_rev_sig = (u32 *)((u32)mem+ssig_end);
			u32 remain_ofst = ((0x1000 - ((u32)pt_sec_rev_sig % 0x1000)) >> 2);

			if((fw_sec_ver_h | fw_sec_ver_l)==0)
			{
				u16 ofst;
				for(ofst=0; ofst<remain_ofst; ofst++)
				{
					//if( ((*(pt_sec_rev_sig+ofst))==FW_SECURITY_REVISION) && ((*(pt_sec_rev_sig+ofst+3))==FW_SECURITY_REVISION) )
					u32 pattern_1 = (*(pt_sec_rev_sig+ofst));
					u32 pattern_2 = (*(pt_sec_rev_sig+ofst+3));
					if((pattern_1 == FW_SECURITY_REVISION) && (pattern_2 == FW_SECURITY_REVISION))
					{
						fw_sec_ver_h = (*(pt_sec_rev_sig+ofst+2));
						fw_sec_ver_l = (*(pt_sec_rev_sig+ofst+1));
						break;
					}
				}
				if(ofst==remain_ofst)
				{
					srb_apl_trace(LOG_ERR, 0x1648, "Security Ver. not found in DU%d", sec_chk_du_idx);
					sec_chk_du_idx++;
					ssig_end = 0;
				}

			}
		}
	#else
		if((fw_sec_ver_h | fw_sec_ver_l)==0)
		{
			if(upgrade_fw.sb_dus_amt >= (upgrade_fw.image_dus-4))
			{
				u32 *pt_sec_rev_sig = mem;
				u16 ofst;
				for(ofst=0; ofst<1024; ofst+=4)
				{
					//if( ((*(pt_sec_rev_sig+ofst))==FW_SECURITY_REVISION) && ((*(pt_sec_rev_sig+ofst+3))==FW_SECURITY_REVISION) )
					u32 pattern_1 = (*(pt_sec_rev_sig+ofst));
					u32 pattern_2 = (*(pt_sec_rev_sig+ofst+3));
					if((pattern_1 == FW_SECURITY_REVISION) && (pattern_2 == FW_SECURITY_REVISION))
					{
						fw_sec_ver_h = (*(pt_sec_rev_sig+ofst+2));
						fw_sec_ver_l = (*(pt_sec_rev_sig+ofst+1));
						break;
					}
				}
				if(ofst==1024)
					srb_apl_trace(LOG_ERR, 0xf3c4, "Security Ver. not found in DU%d", (upgrade_fw.sb_dus_amt+1));
			}
		}
	#endif
	#endif

		cur_sb_dus = upgrade_fw.sb_dus_amt % DOWNLOAD_MR_DTAG_CNT_PAGE;
        //This part will check
        // 1. Section Info. integrity check by CRC32
        // 2. If digital signature is exist, the hash value for loader and main FW will be calculated here.
        //===============================================================================================
        //1st 4K of main FW part
        //if (upgrade_fw.sb_dus_amt) // > 0
        {
    		if ((upgrade_fw.sb_dus_amt == srb_para.sb_start) || // loader 1st 4K
                (upgrade_fw.sb_dus_amt == upgrade_fw.fw_start)) // main fw 1st 4K
    		{

                // save pk_digest and data_digest to key and sign
                Receive_LDR = (upgrade_fw.sb_dus_amt < upgrade_fw.fw_start);
                srb_apl_trace(LOG_ERR, 0x98cc, "FW DL : Security En=%d, Mode=%d", upgrade_fw.security_enable, upgrade_fw.security_mode);

    		    // the fw uses fwdl_check_secure_section() to check section info. checksum 
    			if (fwdl_check_secure_section((loader_image_t *) mem) == false)
    			{
    				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
    				//Jerry: if fw img is invalid, than fw commit won't do warmboot and return fail
    				fw_image_status = FW_IMG_INVALID;
    				dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus);
    				srb_apl_trace(LOG_ERR, 0x8fa0, "check secure section fail");
    				#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie	
					if (_fg_pi_resum_io == true){
						if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
						}
						else if (cmf_idx == 3){	//Download  512+8 CMF
							while(fcmd_outstanding_cnt != 0) {
								evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
								ficu_done_wait();
							}
						#if defined(HMETA_SIZE)
							eccu_dufmt_switch(3);
							eccu_switch_cmf(3);
						#endif
							evlog_printk(LOG_ALW,"MR to 512_8");
						}
						else if (cmf_idx == 4){	//Download  4086+8 CMF
							while(fcmd_outstanding_cnt != 0) {
								evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
								ficu_done_wait();
							}
						#if defined(HMETA_SIZE)	
							eccu_dufmt_switch(4);
							eccu_switch_cmf(4);
						#endif	
							evlog_printk(LOG_ALW,"MR to 4096_8");
						}
					}
				    #endif
    				if (_fg_prp_list)
    					fw_prplist_trx_done = 1;	//Unlock prp list transfer
    				return true;
    			}
    		}
            //not 1st 4K for loader/main fw
    		else if (upgrade_fw.sb_dus_amt > srb_para.sb_start) // normally sb_start=1
            {      
    			fwdl_check_secure_data(mem);
            }
        }
        //===============================================================================================


		memcpy(dtag2mem(upgrade_fw.sb_dus[cur_sb_dus]), mem, SRB_MR_DU_SZE);


        // last DU for slice 2(0 base)
        // last DU stores checksum w/ CRC32
		if ((upgrade_fw.sb_dus_amt + 1) >= upgrade_fw.image_dus)
        {
			u8 last_sb_dus_amt = upgrade_fw.sb_dus_amt % DOWNLOAD_MR_DTAG_CNT_PAGE;

			for (j = 0; j < cur_sb_dus; j++)
            {
				upgrade_fw.fw_crc = crc32_cont(dtag2mem(upgrade_fw.sb_dus[j]), DTAG_SZE, upgrade_fw.fw_crc, false);
			}

			bool fw_verify = fw_verify_image_crc(upgrade_fw.sb_dus[cur_sb_dus], &upgrade_fw.fw_crc, &upgrade_fw.fw_version);

			if (!ncl_enter_mr_mode())
            {
				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
				dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus);
				srb_apl_trace(LOG_ERR, 0x67fd, "download enter mr mode fail");
				#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie	
					if (_fg_pi_resum_io == true){
						if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
						}
						else if (cmf_idx == 3){	//Download  512+8 CMF
							while(fcmd_outstanding_cnt != 0) {
								evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
								ficu_done_wait();
							}
						#if defined(HMETA_SIZE)
							eccu_dufmt_switch(3);
							eccu_switch_cmf(3);
						#endif
							evlog_printk(LOG_ALW,"MR to 512_8");
						}
						else if (cmf_idx == 4){	//Download  4086+8 CMF
							while(fcmd_outstanding_cnt != 0) {
								evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
								ficu_done_wait();
							}
						#if defined(HMETA_SIZE)	
							eccu_dufmt_switch(4);
							eccu_switch_cmf(4);
						#endif	
							evlog_printk(LOG_ALW,"MR to 4096_8");
						}
					}
				#endif
				if (_fg_prp_list)
					fw_prplist_trx_done = 1;	//Unlock prp list transfer
				return true;
			}

			fw_build_buffer_block(upgrade_fw.fw_dw_buffer_rda, upgrade_fw.sb_dus, upgrade_fw.sb_dus_amt / DOWNLOAD_MR_DTAG_CNT_PAGE, last_sb_dus_amt + 1);

			dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus);



			if (fw_verify) 
            {
                upgrade_fw.fw_dw_status = DW_ST_TRANSFER_COMPLETE;
			}
            else
            {
				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
				//Jerry: if fw img is invalid, than fw commit won't do warmboot and return fail
				fw_image_status = FW_IMG_INVALID;
				srb_apl_trace(LOG_ALW, 0x2538, "transfer fw image complete, but CRC error");
                ncl_leave_mr_mode();
				break;
			}

            //Firmware buffer read back to verify (Final Check)
			//fw_verify = srb_combo_verify_and_restore(upgrade_fw.fw_dw_buffer_rda);
            if (srb_combo_verify_and_restore(upgrade_fw.fw_dw_buffer_rda))
            {
			#if (PI_FW_UPDATE == mENABLE)		//20210110-Eddie
				_fg_pi_resum_io = true;
				_fg_pi_sus_io = false;
			#endif
				upgrade_fw.fw_dw_status = DW_ST_TRANSFER_COMPLETE;
				srb_apl_trace(LOG_ALW, 0x7493, "FW Buffer Read Verify PASS!");
			} else {
				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
				//Jerry: if fw img is invalid, than fw commit won't do warmboot and return fail
				fw_image_status = FW_IMG_INVALID;
				srb_apl_trace(LOG_ALW, 0xbac8, "FW Buffer Read Verify FAIL => CRC Error");
			}

			if (fwdl_signature_chk()) 
            {
                upgrade_fw.fw_dw_status = DW_ST_TRANSFER_COMPLETE;
			}
            else
            {
				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
				//Jerry: if fw img is invalid, than fw commit won't do warmboot and return fail
				fw_image_status = FW_IMG_INVALID;
				srb_apl_trace(LOG_ALW, 0x99f7, "Security Check Fail!!!!!!");
                ncl_leave_mr_mode();
				break;
			}

		#ifdef FW_SECURITY_REVISION
			srb_apl_trace(LOG_ALW, 0x5119, "(Curr) FW Security Rev. 0x%x_%x", FW_SECURITY_VERSION_H, FW_SECURITY_VERSION_L);
			srb_apl_trace(LOG_ALW, 0x6c43, "(Updt) FW Security Rev. 0x%x_%x", fw_sec_ver_h, fw_sec_ver_l);
			if((fw_sec_ver_h < FW_SECURITY_VERSION_H) || ((fw_sec_ver_h == FW_SECURITY_VERSION_H)&&(fw_sec_ver_l < FW_SECURITY_VERSION_L)))
			{
				fw_sec_ver_h = 0;
				fw_sec_ver_l = 0;
				ssig_end = 0;
				sec_chk_du_idx = 0;
				srb_apl_trace(LOG_ALW, 0x9bf9, "FW Security Rev. chk Fail !!!!!!");
				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
				//Jerry: if fw img is invalid, than fw commit won't do warmboot and return fail
				fw_image_status = FW_IMG_INVALID;
				ncl_leave_mr_mode();
				break;
			}
			else
			{
				fw_sec_ver_h = 0;
				fw_sec_ver_l = 0;
				ssig_end = 0;
				sec_chk_du_idx = 0;
				srb_apl_trace(LOG_ALW, 0x32da, "FW Security Rev. chk PASS!");
			}
		#endif
			ncl_leave_mr_mode();
		}
        // FW calculates checksum evey 3 DUs
        else if ((DOWNLOAD_MR_DTAG_CNT_PAGE - 1) == cur_sb_dus)
        {
			u32 i;

			for (i = 0; i < DOWNLOAD_MR_DTAG_CNT_PAGE; i++){
				upgrade_fw.fw_crc = crc32_cont(dtag2mem(upgrade_fw.sb_dus[i]), DTAG_SZE, upgrade_fw.fw_crc, false);
			}

			if (!ncl_enter_mr_mode())
            {

				upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
				dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus);
				upgrade_fw.sb_dus_amt++;
				srb_apl_trace(LOG_ERR, 0xcb5e, "download enter mr mode fail");
				continue;
			}
			evlog_printk(LOG_ALW,"dus_amt Chk %d sb_dus %d",upgrade_fw.sb_dus_amt,upgrade_fw.sb_dus);
			fw_build_buffer_block(upgrade_fw.fw_dw_buffer_rda, upgrade_fw.sb_dus, upgrade_fw.sb_dus_amt / DOWNLOAD_MR_DTAG_CNT_PAGE, SRB_MR_DU_CNT_PAGE);
			ncl_leave_mr_mode();
		}


		upgrade_fw.sb_dus_amt++;
	}

    // Compare coming loader section checksum w/ current loader.
    // The following code maybe move to function 'fwdl_check_secure_section()'.
    // ToDo................................
    // Should recover original section checksum value if there is a error.(TODO)
    #if 1
    if (upgrade_fw.sb_dus_amt == (u8)2)
    {

			#ifdef PRT_LOADER_LOG
				srb_apl_trace(LOG_ERR, 0xc51f, "[Eddie]Check DL sb \n");
				//dtagprint(upgrade_fw.sb_dus[0], 8192);
			#endif
			#ifdef UPDATE_LOADER
				loader_image_t *image_sb = (loader_image_t *) dtag2mem(upgrade_fw.sb_dus[1]);
				srb_apl_trace(LOG_ERR, 0x5814, "[Loader CHK]image_sb signature = %x, section_csum = %x, srb_sb_csnum =  %x , image_dus = %d\n",image_sb->signature,image_sb->section_csum, srb_para.sb_csum, image_sb->image_dus);
				srb_para.sb_image_dus = image_sb->image_dus;
				//sys_assert(dtag_get_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, srb_para.sb_dus) == DOWNLOAD_MR_DTAG_CNT_PAGE);
				//memcpy(dtag2mem(srb_para.sb_dus[0]), dtag2mem(upgrade_fw.sb_dus[1]), DTAG_SZE);
        if (sb_always_update_idx || image_sb->section_csum != srb_para.sb_csum)
        {
            srb_apl_trace(LOG_ERR, 0x3c31, "Loader need to update ! \n");
#if 0
            sys_assert(dtag_get_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, srb_para.sb_dus) == DOWNLOAD_MR_DTAG_CNT_PAGE);
            for (a = 0; a < srb_para.image_dus; a++){
                memcpy(dtag2mem(srb_para.sb_dus[a]), dtag2mem(upgrade_fw.sb_dus[1+a]), DTAG_SZE);
#ifdef PRT_LOADER_LOG
                srb_apl_trace(LOG_ERR, 0x75d6, "[Eddie]Check srb_para.sb_dus[0] 1 \n");
                dtagprint(srb_para.sb_dus[a], 4096);
#endif
            }
#endif
            srb_para.sb_csum = image_sb->section_csum;
            sb_update_idx = true;
        }
#endif
    }
#endif

#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie	
	if (_fg_pi_resum_io == true){
		if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
		}
		else if (cmf_idx == 3){	//Download  512+8 CMF
			while(fcmd_outstanding_cnt != 0) {
				evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
				ficu_done_wait();
			}
		#if defined(HMETA_SIZE)
			eccu_dufmt_switch(3);
			eccu_switch_cmf(3);
		#endif
			evlog_printk(LOG_ALW,"MR to 512_8");
		}
		else if (cmf_idx == 4){	//Download  4086+8 CMF
			while(fcmd_outstanding_cnt != 0) {
				evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
				ficu_done_wait();
			}
		#if defined(HMETA_SIZE)	
			eccu_dufmt_switch(4);
			eccu_switch_cmf(4);
		#endif	
			evlog_printk(LOG_ALW,"MR to 4096_8");
		}
	}
#endif
       srb_apl_trace(LOG_ALW, 0x3481, "xfer (%d) -> cur(%d)", count, upgrade_fw.sb_dus_amt);
	if (_fg_prp_list)
    {
		fw_prplist_trx_done = 1;	//Unlock prp list transfer
    }
       return true;
}

slow_code bool fwdl_commit(fwdl_req_t *req) // 20201126-YC
{
	u32 slot = req->field.commit.slot;
	u32 ca = req->field.commit.ca;
	u8 dw_slot, active_slot;

	if(plp_trigger)
	{
		return true;
	}	
#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie
	if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
	}
	else if (cmf_idx == 3){	//Download MR CMF
		while(fcmd_outstanding_cnt != 0) {
			evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
			ficu_done_wait();
		}
	#if defined(HMETA_SIZE)
		eccu_dufmt_switch(0);
		eccu_switch_cmf(0);
	#endif
		evlog_printk(LOG_ALW,"512_8 to MR");
	}
	else if (cmf_idx == 4){	//Download MR CMF
		while(fcmd_outstanding_cnt != 0) {
			evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
			ficu_done_wait();
		}
	#if defined(HMETA_SIZE)	
		eccu_dufmt_switch(0);
		eccu_switch_cmf(0);
	#endif	
		evlog_printk(LOG_ALW,"4096_8 to MR");
	}
#endif

	srb_apl_trace(LOG_INFO, 0xbff7, "fwdl_commit SLOT:%d, CA:%d use_old_fw:%u\n",slot,ca, use_old_fw);

	//printk("fwdl_commit SLOT:%d, CA:%d \n",slot,ca);

#ifdef OTF_MEASURE_TIME	
    recMsec[0] = *(vu32*)0xC0201044;
#endif
	rebuild_fwb_para_t para;

	dw_slot = 0;
	active_slot = 0;
	para.ca = ca;
	switch (ca) {
	case NVME_FW_COMMIT_REPLACE_IMG:
		dw_slot = slot;
		active_slot = slot; // edevx 1 slot non BIT0 power cycle case 
		break;
	case NVME_FW_COMMIT_REPLACE_AND_ENABLE_IMG:
		dw_slot = slot;
		active_slot = slot;
		break;
	case NVME_FW_COMMIT_ENABLE_IMG:
		active_slot = slot;
		break;
	case NVME_FW_COMMIT_RUN_IMG:
		active_slot = slot;
		if (upgrade_fw.fw_dw_status == DW_ST_TRANSFER_COMPLETE)
			dw_slot = slot;
		break;
	default:
		srb_apl_trace(LOG_ERR, 0xc941, "commit ca [%d] don't support\n", ca);
		req->status = NVME_SC_INVALID_FIELD;
		return true;
	}

    if (use_old_fw)
    {
        dw_slot = 0;
        use_old_fw = false;
    }

	if (ca == NVME_FW_COMMIT_RUN_IMG){	//20210520-Eddie CA3 ==> OTF Updates ==> delete refesh read cpu2
		extern struct timer_list refresh_read_timer;
		del_timer(&refresh_read_timer);
	}

	 #if 0
			u32*pdata=NULL;
			pdata = dtag2mem(upgrade_fw.sb_dus[0]);
			srb_apl_trace(LOG_ERR, 0x8b4e, "[Eddie]Check sb \n");
			for (int c = 0; c < 16384/ sizeof(u32); c++) {
			            if (1) {
			                    if ((c & 3) == 0) {
			                            srb_apl_trace(LOG_ERR, 0x506f, "%x:", c << 2);
			                    }
			                    srb_apl_trace(LOG_ERR, 0x5059, "%x ", pdata[c]);
			                    if ((c & 3) == 3) {
			                            srb_apl_trace(LOG_ERR, 0x2ca3, "\n");
			                    }
			            }
			        }

			loader_image_t *image_sb = (loader_image_t *) dtag2mem(upgrade_fw.sb_dus[1]);
			srb_apl_trace(LOG_ERR, 0xa539, "[Eddie]image_sb signature = %x, section_csum = %x \n",image_sb->signature,image_sb->section_csum);

	#endif
    srb_apl_trace(LOG_ERR, 0x11a1, "[commit]fw download status = %d", upgrade_fw.fw_dw_status);

	if ((((upgrade_fw.sb_dus_amt << 1) < upgrade_fw.image_dus) ||
		((upgrade_fw.fw_dw_status != DW_ST_TRANSFER_FW) &&
		(upgrade_fw.fw_dw_status != DW_ST_TRANSFER_COMPLETE))) &&
		(dw_slot != 0)) {
		dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, upgrade_fw.sb_dus);
		upgrade_fw.sb_dus_amt = 0;
		upgrade_fw.fw_dw_status = DW_ST_INIT;
		srb_apl_trace(LOG_ALW, 0x2947, "transfer fw not complete,can't commit\n");
		req->status = ((NVME_SCT_COMMAND_SPECIFIC << 8) | NVME_SC_INVALID_FIRMWARE_IMAGE);
		return true;
	}

	if (!ncl_enter_mr_mode())
    {
		upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
		srb_apl_trace(LOG_ERR, 0x28cc, "commit enter mr mode fail\n");
		req->status = ((NVME_SCT_MEDIA_ERROR << 8) | NVME_SC_ACCESS_DENIED);
		return true;
	}

	if ((dw_slot == 0) && (upgrade_fw.fw_dw_status == DW_ST_INIT)) {
		ncl_cmd_simple_submit(&upgrade_fw.fw_dw_buffer_rda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	}


	srb_apl_trace(LOG_DEBUG, 0x5065, "rawdisk srb rebuild start");

	para.fw_version = upgrade_fw.fw_version;
	para.fw_pri = upgrade_fw.fwb_pri_rda;
	para.fw_sec = upgrade_fw.fwb_sec_rda;
	para.buf_rda = upgrade_fw.fw_dw_buffer_rda;
	para.image_dus = upgrade_fw.image_dus;
	para.mfw_dus = upgrade_fw.mfw_dus;
	para.slot = dw_slot;
	para.active_slot = active_slot;

#ifdef UPDATE_LOADER
	//=======Check SRB 1==========
#ifdef PRT_LOADER_LOG	
	srb_apl_trace(LOG_ERR, 0xcdd9, "Check SRB 1 \n");
	srb_read_verify_(srb_rda,1,0x4);
	srb_apl_trace(LOG_ERR, 0xb333, "Check sb content:\n");
	sb_verify_(sb_rda,0x400,srb_sb_du_cnt);
	srb_apl_trace(LOG_ERR, 0xdab3, "Check sb_dual content:\n");
	sb_verify_(sb_rda_dual,0x400,srb_sb_du_cnt);
#endif	
	//========================
	if (sb_update_idx == true && upgrade_fw.fw_dw_status == DW_ST_TRANSFER_COMPLETE){

	//srb_page_read(srb_para.srb_1st_pos,srb_para.srb_buf_pos, slc_pgs_per_blk);
	#if 0//def PRT_LOADER_LOG
	for (int a = 0; a < srb_para.image_dus; a++){
		srb_apl_trace(LOG_ERR, 0x8011, "[Eddie]Check srb_para.sb_dus[0] 2 \n");
		dtagprint(srb_para.sb_dus[a], 4096);
	}	
	#endif	

		srb_rebuild_sb(srb_para);
	#ifdef PRT_LOADER_LOG
		srb_apl_trace(LOG_ERR, 0x1f05, "Check SRB 3 \n");
		srb_read_verify_(srb_rda,1,0x4);
		srb_apl_trace(LOG_ERR, 0xcabc, "Check sb content:\n");
		sb_verify_(sb_rda,0x400,srb_para.sb_image_dus);
		srb_apl_trace(LOG_ERR, 0x76ba, "Check sb_dual content:\n");
		sb_verify_(sb_rda_dual,0x400,srb_para.sb_image_dus);
	#endif
		sb_update_idx = false;
		sb_always_update_idx = false;
	}
	//dtag_put_bulk(DTAG_T_SRAM, DOWNLOAD_MR_DTAG_CNT_PAGE, srb_para.sb_dus);
#endif
	upgrade_fw.fw_dw_status = DW_ST_COMMIT_FW;
	srb_rebuild_fwb(para);

	upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW;
	if (false == srb_fwb_verify_after_upgrade(upgrade_fw.fwb_pri_rda, upgrade_fw.fwb_sec_rda)) {
		upgrade_fw.fw_dw_status = DW_ST_VERIFY_FW_ERR;
		srb_apl_trace(LOG_ERR, 0xafce, "commit load srb error\n");
		ncl_leave_mr_mode();
		req->status = ((NVME_SCT_COMMAND_SPECIFIC << 8) | NVME_SC_INVALID_FIRMWARE_IMAGE);
		return true;
	}

	ncl_leave_mr_mode();
	if (dw_slot != 0)
#if !defined(MPC)
		nvmet_set_fw_slot_revision(dw_slot, upgrade_fw.fw_version);
#else
		//TODO
#endif
	upgrade_fw.fw_dw_status = DW_ST_UPGRADE_FW_COMPLETE;
	srb_apl_trace(LOG_ALW, 0x002c, "commit fw complete");
    //#if (RDISK)
	if (ca == NVME_FW_COMMIT_RUN_IMG)
		misc_set_fw_run_reset_flag();
	else if (active_slot != 0){	//ca == 1 & 2
		misc_set_fw_wait_reset_flag();
		misc_set_warm_boot_ca_mode(CA_1_SIGNATURE);	//20210426-Eddie
	}

	if(misc_is_fw_run_reset())		//20201225-Eddie
		misc_set_warm_boot_ca_mode(CA_3_SIGNATURE);	//20210426-Eddie

#if (PI_FW_UPDATE == mENABLE)	//20210120-Eddie	
	if ((cmf_idx == 1) || (cmf_idx == 2)){	//4096+0/512+0 use same cmf, no need to download new one
	}
	else if (cmf_idx == 3){	//Download  512+8 CMF
		while(fcmd_outstanding_cnt != 0) {
			evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
			ficu_done_wait();
		}
	#if defined(HMETA_SIZE)	
		eccu_dufmt_switch(3);
		eccu_switch_cmf(3);
	#endif	
		evlog_printk(LOG_ALW,"MR to 512_8");
	}
	else if (cmf_idx == 4){	//Download  4086+8 CMF
		while(fcmd_outstanding_cnt != 0) {
			evlog_printk(LOG_ALW,"fcmd_outstanding_cnt : %d",fcmd_outstanding_cnt);
			ficu_done_wait();
		}
	#if defined(HMETA_SIZE)
		eccu_dufmt_switch(4);
		eccu_switch_cmf(4);
	#endif	
		evlog_printk(LOG_ALW,"MR to 4096_8");
	}
#endif

	req->status = NVME_SC_SUCCESS;
	return true;
}

slow_code bool fwdl_is_runing_fw_upgrade(void) // 20201126-YC
{
	return (upgrade_fw.fw_dw_status != DW_ST_INIT);
}

#if defined(MPC)
#if 1 //3.1.7.4 merged 20201201 Eddie
extern volatile u8 fw_update_flag;
slow_code_ex void fwdl_req_evt_handler(u32 p0, u32 p1, u32 p2)
{
	bool ret = true;

	while (!QSIMPLEQ_EMPTY(&fwdl_req_q)) {

	if(plp_trigger)
	{
		break;
	}		
		fwdl_req_t *fwdl_req = QSIMPLEQ_FIRST(&fwdl_req_q);

		if (fwdl_req->op == FWDL_DOWNLOAD)
		{	fw_update_flag = 1;
			ret = fwdl_download(fwdl_req);
			fw_update_flag = 0;
		}
		else if (fwdl_req->op == FWDL_COMMIT)
			ret = fwdl_commit(fwdl_req);
		else
			panic("no support");

		sys_assert(ret == true);
		cpu_msg_issue(fwdl_req->tx, CPU_MSG_FWDL_OP_DONE, 0, (u32)fwdl_req);
		QSIMPLEQ_REMOVE_HEAD(&fwdl_req_q, link);
	}
}

slow_code_ex void ipc_fwdl_op(volatile cpu_msg_req_t *req)
{
	fwdl_req_t *fwdl_req = (fwdl_req_t *) req->pl;

	fwdl_req->tx = req->cmd.tx;
	QSIMPLEQ_INSERT_TAIL(&fwdl_req_q, fwdl_req, link);
	evt_set_cs(evt_fwdl_req, 0, 0, CS_TASK);
}

ddr_code void ipc_read_kotp_fwimg(volatile cpu_msg_req_t *req)
{
	u32 *KOTP_sh = (u32 *) req->pl;
	srb_apl_trace(LOG_INFO, 0xa7b1, "Read digest of public key, addr:0x%x", KOTP_sh);

	if(!ncl_enter_mr_mode())
		srb_apl_trace(LOG_ERR, 0x71d1, "Enter MR mode fail");
	else {
		rda_t rda= {0};
		bool result = srb_read_kotp(rda, KOTP_sh);
		if(result)
			memset((void *)KOTP_sh, 0x00, 32);
		ncl_leave_mr_mode();
	}

	cpu_msg_sync_done(req->cmd.tx);
}

#else
fast_code void ipc_fwdl_op(volatile cpu_msg_req_t *req)
{
	fwdl_req_t *fwdl_req = (fwdl_req_t *) req->pl;
	bool ret = true;

	if (fwdl_req->op == FWDL_DOWNLOAD)
		ret = fwdl_download(fwdl_req);
	else if (fwdl_req->op == FWDL_COMMIT)
		ret = fwdl_commit(fwdl_req);
	else
		panic("no support");

	sys_assert(ret == true);
	cpu_msg_issue(req->cmd.tx, CPU_MSG_FWDL_OP_DONE, 0, (u32)fwdl_req);
}
#endif
#endif

init_code void fwdl_init(srb_t *srb)
{
#if defined(MPC)
#if 1  //3.1.7.4 merged 20201201 Eddie
	QSIMPLEQ_INIT(&fwdl_req_q);
	evt_register(fwdl_req_evt_handler, 0, &evt_fwdl_req);
#endif	
	cpu_msg_register(CPU_MSG_FWDL_OP, ipc_fwdl_op);
	cpu_msg_register(CPU_MSG_RD_KOTP, ipc_read_kotp_fwimg);
#endif

	if (srb->srb_hdr.srb_signature != SRB_SIGNATURE)
		panic("not support yet");

	memset((void *)&upgrade_fw, 0, sizeof(upgrade_fw));
	upgrade_fw.fw_dw_buffer_rda = srb->fwb_buf_pos;
	upgrade_fw.fwb_pri_rda = srb->fwb_pri_pos;
	upgrade_fw.fwb_sec_rda = srb->fwb_sec_pos;

    #ifdef FW_SECURITY
	upgrade_fw.security_enable = SECURITY_ENABLE;
	upgrade_fw.security_mode = RS_SECURITY_MODE;
    #else
	upgrade_fw.security_enable = SECURITY_DISABLE;
	upgrade_fw.security_mode = NONE_SECURITY_MODE;
    #endif
	upgrade_fw.s_result = NULL;
	upgrade_fw.k_result = NULL;
	upgrade_fw.public_key = NULL;
#ifdef UPDATE_LOADER	//20200603-Eddie
	slc_pgs_per_blk = nand_page_num_slc();
	srb_para.sb_csum = srb->sb_csum;
	srb_para.srb_hdr_pgs = srb->srb_hdr_pgs;
	srb_para.srb_buf_pos = srb->srb_buf_pos;
	srb_para.fwb_buf_pos = srb->fwb_buf_pos;
	srb_para.srb_1st_pos = srb->srb_1st_pos;
	srb_para.srb_2nd_pos = srb->srb_2nd_pos;
	srb_para.srb_3rd_pos = srb->srb_3rd_pos;
	srb_para.srb_4th_pos = srb->srb_4th_pos;
	srb_para.srb_5th_pos = srb->srb_5th_pos;
	srb_para.srb_6th_pos = srb->srb_6th_pos;
	srb_para.srb_7th_pos = srb->srb_7th_pos;
	srb_para.srb_8th_pos = srb->srb_8th_pos;
	srb_apl_trace(LOG_ERR, 0x2de8, "SRB 1ST row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_1st_pos.row,srb->srb_1st_pos.ch,srb->srb_1st_pos.dev);
	srb_apl_trace(LOG_ERR, 0x5f8f, "SRB 2ND row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_2nd_pos.row,srb->srb_2nd_pos.ch,srb->srb_2nd_pos.dev);
	#ifdef PRT_LOADER_LOG
		srb_apl_trace(LOG_ERR, 0x9d1f, "SRB 1ST row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_1st_pos.row,srb->srb_1st_pos.ch,srb->srb_1st_pos.dev);
		srb_apl_trace(LOG_ERR, 0xa1ed, "SRB 2ND row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_2nd_pos.row,srb->srb_2nd_pos.ch,srb->srb_2nd_pos.dev);
		//srb_apl_trace(LOG_ERR, 0, "SRB 3RD row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_3rd_pos.row,srb->srb_3rd_pos.ch,srb->srb_3rd_pos.dev);
		//srb_apl_trace(LOG_ERR, 0, "SRB 4TH row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_4th_pos.row,srb->srb_4th_pos.ch,srb->srb_4th_pos.dev);
		//srb_apl_trace(LOG_ERR, 0, "SRB 5TH row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_5th_pos.row,srb->srb_5th_pos.ch,srb->srb_5th_pos.dev);
		//srb_apl_trace(LOG_ERR, 0, "SRB 6TH row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_6th_pos.row,srb->srb_6th_pos.ch,srb->srb_6th_pos.dev);
		//srb_apl_trace(LOG_ERR, 0, "SRB 7TH row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_7th_pos.row,srb->srb_7th_pos.ch,srb->srb_7th_pos.dev);
		//srb_apl_trace(LOG_ERR, 0, "SRB 8TH row= 0x%x, @CH/CE(%d/%d) \n",srb->srb_8th_pos.row,srb->srb_8th_pos.ch,srb->srb_8th_pos.dev);
	#endif
	srb_rda = srb->srb_1st_pos;
	sb_rda = srb->srb_1st_pos;
	sb_rda_dual = srb->srb_1st_pos;
	sb_rda.row = srb->srb_hdr.srb_sb_row;
	sb_rda_dual.row = srb->srb_hdr.srb_sb_row_dual;

	srb_rda_buf = srb->srb_buf_pos;
	sb_rda_buf = srb->srb_buf_pos;
	sb_rda_buf_dual = srb->srb_buf_pos;
	sb_rda_buf.row = srb->srb_hdr.srb_sb_row;
	sb_rda_buf_dual.row = srb->srb_hdr.srb_sb_row_dual;
	srb_sb_du_cnt = srb->srb_hdr.srb_sb_du_cnt;
	//sb_rda_mirror.row = srb->srb_hdr.srb_sb_row_mirror;
	//sb_rda_dual_mirror.row = srb->srb_hdr.srb_sb_row_dual_mirror;
#endif

	evt_delay_dtag_start = ddr_dtag_register(1);
#if GC_SUSPEND_FWDL
	fwdl_gc_handle_dtag_start = ddr_dtag_register(1);	//20210308-Eddie-FWDL-GC-handle suspend->resume
#endif
    srb_apl_trace(LOG_ERR, 0xcfcb, "FW Update : Security En=%d, Mode=%d", upgrade_fw.security_enable, upgrade_fw.security_mode);
}

/*! @} */

