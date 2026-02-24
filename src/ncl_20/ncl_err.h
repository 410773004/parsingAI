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

#ifndef _NCL_ERR_H_
#define _NCL_ERR_H_

////////////////////////////////////////////////////////////////////////////////
///   Public Constants Definitions
////////////////////////////////////////////////////////////////////////////////
enum ncl_err_status {
	ncl_err_good			= 0,		///< good
	ncl_err_recoveried,				///< err is recovered
	ncl_err_failed			= 0xE0000001,	///< failed
	ncl_err_not_perm,				///< Oper not permitted
	ncl_err_pending,				///< Operation Pending
	ncl_err_no_mem,					///< Out of Memory
	ncl_err_fault,					///< Bad Address
	ncl_err_timeout,				///< Timeout
	ncl_err_ficu_aborted,				///< FICU abort
	ncl_err_op_aborted,				///< NCL abort operation
	ncl_err_bad_crc,				///< CRC error
	ncl_err_busy,					///< Dev or Res is Busy
	ncl_err_readonly,				///< device is in RO
	ncl_err_ecc_dec,				///< Decoding error
	ncl_err_powerinterrupt,				///< power interupt
	ncl_err_no_dev,					///< No NAND devices
	ncl_err_noexist,				///< No error detection
};

enum ncb_err {
	cur_du_good = 0,
	cur_du_ovrlmt_err,
	cur_du_partial_err, // 2 wunc
	cur_du_dfu_err, // 3 meta crc
	cur_du_ppu_err,
    cur_du_nard, // 5 not do ard //tony 20201020
	cur_du_ucerr,// 6 Uncorrectable error
	cur_du_erase,// 7
	cur_du_raid_err,
	unknown_err,// Similar as ficu_err_ncb
	nand_err,
	cur_du_enc_err,
	cur_du_timeout,
	cur_du_1bit_retry_err,
    cur_du_1bit_vth_retry_err,
	cur_du_2bit_retry_err,
	cur_du_2bit_nard_err,  //16
	cur_du_spor_err, //17
};

/*! It is for du status and it is valid only when ncl cmd status is not good */
enum ficu_du_status {
	ficu_err_good			= 0,		///< good
	ficu_err_du_ovrlmt,				///< du is overlimit
	ficu_err_par_err,				///< partial DU error was detected
	ficu_err_dfu,					///< dfu err
	ficu_err_ppu,					///< ppu err
    ficu_err_nard,                  ///< du not do ard //tony 20201020
	ficu_err_du_uc,					///< du is decoding err
	ficu_err_du_erased,				///< du is erased
	ficu_err_raid,					///< raid err
	ficu_err_ncb,					///< NCB error
	ficu_err_ndcu,					///< NDCU error
	ficu_err_encoding,				///< encoding error

	ficu_err_fdma,					///< fdma err
	ficu_err_1bit_retry_err,
    ficu_err_1bit_vth_retry_err,
	ficu_err_2bit_retry_err,
	ficu_err_2bit_nard_err,           // 2bit not do ard
	ficu_err_spor_err,           // 2bit not do ard
	ficu_err_erase,					///< erase error
	ficu_err_prog,					///< program error
	ficu_err_max			= 0xFF,		///< not over this value
};

enum retry_handle_step {
    default_read            = 0,
    retry_set_shift_value,
    retry_read,
    retry_history_step,      //history read use
    retry_history_read,      //history read use
    last_1bit_step,
    last_1bit_read,
    last_1bit_vth_step,
    last_1bit_vth_read,    
    last_2bit_step,
    last_2bit_read,
    retry_end,
    raid_recover_fail,
    abort_retry_step,        //For abort retry flow, reset shift value to default.
    spor_retry_type,		 //non-plp 
    spor_retry_step,
    spor_retry_read,
};

////////////////////////////////////////////////////////////////////////////////
///   Public Macros Definitions
////////////////////////////////////////////////////////////////////////////////

#define ncl_good(err)		((err) == ncl_err_good)
#define ncl_failed(err)		((err) != ncl_err_good)
#define ncl_success(err)	((err) <= ncl_err_recoveried)

#define ficu_du_data_good(err)	((err) <= ficu_err_du_ovrlmt)
#define ficu_du_erased(err)	((err) == ficu_err_du_erased)
#define ficu_du_need_retry(err)	((err) == cur_du_ucerr)
#endif
