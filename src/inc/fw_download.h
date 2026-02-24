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

#define IMAGE_SIGNATURE    0x54495247 /* GRIT */	///< signature pattern
#define IMAGE_COMBO        0x424D4F43 /* COMB */
#define IMAGE_CMFG        0x47464D43 /* CMFG */
#define INVALID_PMA     0xFFFFFFFF /* SPECIAL Section */

//Benson add for Firmware Signing
#define FW_SECURITY

#define FW_SECURITY_REVISION  0x56455253 /* SREV */
#define FW_SECURITY_VERSION_H 0x00000000
#define FW_SECURITY_VERSION_L 0x00000000
#define SSIG_SIGNATURE        0x47495353

#if USE_8K_DU || USE_8K_PAGE
#define DOWNLOAD_MR_DTAG_CNT_PAGE (1)			///< dtag count per MR page
#else
#define DOWNLOAD_MR_DTAG_CNT_PAGE (3)			///< dtag count per MR page
#endif
/*!
 * @brief definition of download firmware
 */

/*
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
*/	
typedef struct _download_fw_t {
	u64 fw_version;
	rda_t fw_dw_buffer_rda;				///< firmware raw data buffer
	rda_t fwb_pri_rda;
	rda_t fwb_sec_rda;
	dtag_t sb_dus[DOWNLOAD_MR_DTAG_CNT_PAGE];	///< SB du array
	u32 fw_crc;				///< firmware crc
	void *p_key_result;				///< public key security hash
	void *s_result;			///< firmware image security hash
	void *k_result;			///< public key security hash
	void *public_key;			///< public key point
	u32 calc_image_len;				///< calculate fw image complete length
	u16 calc_pkey_len;				///< calculate public key complete length
	u16 image_dus;					///< image du size
	u16 mfw_dus;					///< multi_fw image du size
	u16 sb_dus_amt;				///< SB du amount
	u8 fw_dw_status;				///< FW download status
	u8 fw_start;					///< FW image start dus
	u8 security_enable;			/// < FW security boot enable flag
	u8 security_mode;			/// <  FW security boot RSA or SM mode
} download_fw_t;

typedef enum {
	FWDL_DOWNLOAD = 0,
	FWDL_COMMIT = 1,
} fwdl_req_op_t;

typedef struct _fwdl_req_t {
	u32 status;
	fwdl_req_op_t op;
	union {
		struct {
			dtag_t *dtags;
			u32 count;
		} download;
		struct {
			u32 slot;
			u32 ca;
		} commit;
	} field;
	u8 tx;  //3.1.7.4 merged 20201201 Eddie
	void *ctx;
	QSIMPLEQ_ENTRY(_fwdl_req_t) link;
} fwdl_req_t;

extern download_fw_t upgrade_fw;

/*!
 * @brief initialize firmware download library
 */
void fwdl_init(srb_t *srb);

/*!
 * @brief commit firmware command event handler function
 *
 * @param slot		firmware slot
 * @param ca		commit action
 *
 * @return		nvme status
 */
bool fwdl_commit(fwdl_req_t *req);

/*!
 * @brief download firmware command event handler function
 *
 * @param dtags		should be download fw image buffer point
 * @param count		count of download fw image dtag
 *
 * @return		true for handled done, always return true
 */
bool fwdl_download(fwdl_req_t *req);
