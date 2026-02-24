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

//=============================================================================
//
/*! \file
 * @brief tmp102 interface
 *
 * \addtogroup rtos
 */
//=============================================================================
#ifndef _SMBUS_H_
#define _SMBUS_H_
#define A1_DC_WA
#define BASIC_MI
/*!
 * @brief read temperature from tmp102
 *
 * @return	absolute temperature
 */
extern u16 smb_tmp102_read(u8 addr_w, u8 addr_r);

/*!
 * @brief SMB Master initialization
 *
 * Setup smb master FSM timer values
 *
 * @return	None
 */
void smb_master_init(u8 strb_cnt);
void smb_master_init_VPD(u8 strb_cnt);
extern void smb_slave_init(u8 strb_cnt);
extern void smb_setup_nvme_mi_basic_data(void);
extern u16 Read_Temp_data(u16 *ret,u8 i);
extern void Send_Temp_Read(u8 addr_w, u8 addr_r);
#endif
