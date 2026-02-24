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

#define PUB_KEY_DIGEST_LEN		0x20

extern bool otp_get_pk(int id, u32 *buf);
extern u32 regs_check_otp_sign(void);
extern int save_pub_key_to_otp(u32 *pub_key, u32 key_len, bool first);
/*!
 * @brief get SOC UNID_INFO data
 *
 * @param u32 *uid0	UNID_INFO[0] data
 * @param u32 *uid1	UNID_INFO[1] data
*
 * @return		none
 */
extern void get_soc_uid(u32 *uid0, u32 *uid1);
