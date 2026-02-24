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
//! \file
//! @brief I2C/SMBUS device or protocol specific APIs. For example, TI TMP102
//! @brief temperature sensor control, NVME MI Basic Management commands etc.
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "types.h"
#include "io.h"
#include "mod.h"
#include "smb_registers.h"
#include "sect.h"
#include "stdio.h"
#include "smbus.h"
#include "string.h"
#include "misc.h"
#include "nvme_spec.h"
#include "ssstc_cmd.h"

#define __FILEID__ smbs
#include "trace.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
extern smb_registers_regs_t *smb_mas;
extern smb_registers_regs_t *smb_slv;

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
/*
*To send cmd For Read temperature 
*
*/
#if CPU_ID == 1
slow_code void Send_Temp_Read(u8 addr_w, u8 addr_r){
	smb_init_0_t smb_init_0;
	smb_control_register_t smb_control_register;

	/* set init rd and wr address */
	smb_init_0.all = 0;
	smb_init_0.b.smb_init_addr_w = addr_w;
	smb_init_0.b.smb_init_addr_r = addr_r;
	//rtos_smb_trace(LOG_INFO, 0, "addr_w:%x,addr_r:%x",addr_w,addr_r);
	writel(smb_init_0.all, &smb_mas->smb_init_0);

	/* set smb enable and read word start */
	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x07;
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb_mas->smb_control_register);

}
/*
*To Read data For Read temperature 
*
*/
slow_code u16 Read_Temp_data(u16 *ret,u8 i){ // 20210224 Jamie fast_code -> slow_code
	u32 data0;
	u32 data1;

	smb_intr_sts_t current_interrupt_status;
		//rtos_smb_trace(LOG_INFO, 0, "not done");
	
		current_interrupt_status.all = readl(&smb_mas->smb_intr_sts);
		// when ACK_DONE_ERR, return hardcode value
		if ((current_interrupt_status.all & WR_ACK_DONE_ERR_STS_MASK) || 
			(current_interrupt_status.all & MAS_TRAN_DONE_ABNML_STS_MASK))
		{
			rtos_smb_trace(LOG_ALW, 0x2381, " [read temp error] sensor index:%x  sts:%x",i, current_interrupt_status.all);
			writel(current_interrupt_status.all, &smb_mas->smb_intr_sts);
			goto error;
		}

		writel(current_interrupt_status.all, &smb_mas->smb_intr_sts); //W1C Interrupt
		data0 = readl(&smb_mas->smb_pasv_0);
		data1 = readl(&smb_mas->smb_init_rd_1);
	
		*(ret+i) = ((data0 & 0xff000000) >> 20) |
					((data1 & 0xf0) >> 4);
	
		//rtos_smb_trace(LOG_INFO, 0, "Temperature: %d c", *(ret+i) >> 4);

		// Correction values based on wind tunnel experiments
	
			if ((*(ret+i) >> 4) > 65) // Sensor 1  is greater than 65 degrees, the displayed value is reduced by 6
			{	
				return (*(ret+i) >> 4) + 273 - 6;
			}
			else if ((*(ret+i) >> 4) > 63) // Sensor 1 is  64-65 degrees, the displayed value is reduced by 5
			{
				return (*(ret+i) >> 4) + 273 - 5;
			}
			else if ((*(ret+i) >> 4) > 60) // Sensor 1 is  61-63 degrees, the displayed value is reduced by 4
			{
				return (*(ret+i) >> 4) + 273 - 4;
			}
			else if ((*(ret+i) >> 4) > 58) // Sensor 1 is  59-60 degrees, the displayed value is reduced by 3
			{
				return (*(ret+i) >> 4) + 273 - 3;
			}
				
			else // Sensor 1 is  <59 degrees, the displayed value is reduced by 2
			{
				return (*(ret+i) >> 4) + 273 -2;
			}	
	

	error:
		return 0xFFFF;
	
}
/*!
 * @brief Read temperature from TI TMP102 I2C slave device.
 *
 * @param none
 *
 * @return temperature in C
 */
ddr_code u16 smb_tmp102_read(u8 addr_w, u8 addr_r)
{
	u32 data0;
	u32 data1;
	smb_init_0_t smb_init_0;
	smb_control_register_t smb_control_register;
	u32 cnt = 0;
	smb_intr_sts_t current_interrupt_status;

	rtos_smb_trace(LOG_DEBUG, 0xb98a, "TMP102 set ...");

	/* set init rd and wr address */
	smb_init_0.all = 0;
	smb_init_0.b.smb_init_addr_w = addr_w;//0x94,0x96
	smb_init_0.b.smb_init_addr_r = addr_r;//0x95,0x97
	writel(smb_init_0.all, &smb_mas->smb_init_0);

	/* set smb enable and read word start */
	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x07;
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb_mas->smb_control_register);

	// temp printing before sensor issue clarified
	while(!(readl(&smb_mas->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK))	//Check Normal TransferDone Status
	{		
		udelay(100);
		if (++cnt == 10)
		{
			cnt = 0;
    		current_interrupt_status.all = readl(&smb_mas->smb_intr_sts);
    		rtos_smb_trace(LOG_ALW, 0xf71f, "Thermal smb_intr_sts %x", current_interrupt_status.all);
			break; 	//Shane20201215
		}
	}
	current_interrupt_status.all = readl(&smb_mas->smb_intr_sts);
	// when ACK_DONE_ERR, return hardcode value
	if ((current_interrupt_status.all & WR_ACK_DONE_ERR_STS_MASK) || 
		(current_interrupt_status.all & MAS_TRAN_DONE_ABNML_STS_MASK))
	{
		rtos_smb_trace(LOG_ALW, 0xe291, " smb_tmp102_read: ACK_DONE / TRAN_DONE_ABNML");
		writel(current_interrupt_status.all, &smb_mas->smb_intr_sts);
		goto error;
	}

	writel(current_interrupt_status.all, &smb_mas->smb_intr_sts);

	

	data0 = readl(&smb_mas->smb_pasv_0);
	rtos_smb_trace(LOG_DEBUG, 0xb19a, "Data0: %x", data0);
	data1 = readl(&smb_mas->smb_init_rd_1);
	rtos_smb_trace(LOG_DEBUG, 0x0ae6, "Data1: %x", data1);

	u16 temperature = ((data0 & 0xff000000) >> 20) |
				((data1 & 0xf0) >> 4);

	rtos_smb_trace(LOG_DEBUG, 0x61cc, "Temperature: %d c", temperature >> 4);

		if ((temperature >> 4) > 60) // Sensor 1  is greater than 60 degrees, the displayed value is reduced by 4
		{	
			return (temperature >> 4) + 273 -3;
		}
		else // Sensor 1 is below 60 degrees, , the displayed value is reduced by 2
		{
			return (temperature >> 4) + 273 -2;
		}
error:
	return 0xFFFF;
}

/*!
 * @brief Setup NVME MI Basic command data for Blocks 0 and 8
 *
 * @param none
 *
 * @return none
 */
init_code void smb_setup_nvme_mi_basic_data(void)
{
	/* smbus block read of drive's status.
	 * status flags, SMART warnings, temperature */
	smb_dev_sts_data_1_t smb_dev_sts_data_1;
	smb_dev_sts_data_1.b.smb_dev_sts_data01 = 0x3f;
	smb_dev_sts_data_1.b.smb_dev_sts_data02 = 0xff;
	smb_dev_sts_data_1.b.smb_dev_sts_data03 = 0x1e;
	smb_dev_sts_data_1.b.smb_dev_sts_data04 = 0x01;
	writel(smb_dev_sts_data_1.all, &smb_slv->smb_dev_sts_data_1);
	//
	smb_dev_sts_data_2_t smb_dev_sts_data_2;
	smb_dev_sts_data_2.all = 0;
	smb_dev_sts_data_2.b.smb_dev_sts_data05 = 0x00;
	smb_dev_sts_data_2.b.smb_dev_sts_data06 = 0x00;
	smb_dev_sts_data_2.b.smb_dev_sts_pec = 0x10;
	writel(smb_dev_sts_data_2.all, &smb_slv->smb_dev_sts_data_2);

	/* smbus block read of drive's static data.
	 * VID and serial number */
	smb_dev_stc_data_1_t smb_dev_stc_data_1;
	smb_dev_stc_data_1.b.smb_dev_stc_data01 = 0x1E;
	smb_dev_stc_data_1.b.smb_dev_stc_data02 = 0x95;
	smb_dev_stc_data_1.b.smb_dev_stc_data03 = 0xAA;
	smb_dev_stc_data_1.b.smb_dev_stc_data04 = 0xAA;
	writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
	//
	smb_dev_stc_data_2_t smb_dev_stc_data_2;
	smb_dev_stc_data_2.b.smb_dev_stc_data05 = 0x31;
	smb_dev_stc_data_2.b.smb_dev_stc_data06 = 0x32;
	smb_dev_stc_data_2.b.smb_dev_stc_data07 = 0x33;
	smb_dev_stc_data_2.b.smb_dev_stc_data08 = 0x34;
	writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
	//
	smb_dev_stc_data_3_t smb_dev_stc_data_3;
	smb_dev_stc_data_3.b.smb_dev_stc_data09 = 0x35;
	smb_dev_stc_data_3.b.smb_dev_stc_data10 = 0x36;
	smb_dev_stc_data_3.b.smb_dev_stc_data11 = 0x20;
	smb_dev_stc_data_3.b.smb_dev_stc_data12 = 0x20;
	writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
	//
	smb_dev_stc_data_4_t smb_dev_stc_data_4;
	smb_dev_stc_data_4.b.smb_dev_stc_data13 = 0x20;
	smb_dev_stc_data_4.b.smb_dev_stc_data14 = 0x20;
	smb_dev_stc_data_4.b.smb_dev_stc_data15 = 0x20;
	smb_dev_stc_data_4.b.smb_dev_stc_data16 = 0x20;
	writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
	//
	smb_dev_stc_data_5_t smb_dev_stc_data_5;
	smb_dev_stc_data_5.b.smb_dev_stc_data17 = 0x20;
	smb_dev_stc_data_5.b.smb_dev_stc_data18 = 0x20;
	smb_dev_stc_data_5.b.smb_dev_stc_data19 = 0x20;
	smb_dev_stc_data_5.b.smb_dev_stc_data20 = 0x20;
	writel(smb_dev_stc_data_5.all, &smb_slv->smb_dev_stc_data_5);
	//
	smb_dev_stc_data_6_t smb_dev_stc_data_6;
	smb_dev_stc_data_6.b.smb_dev_stc_data21 = 0x20;
	smb_dev_stc_data_6.b.smb_dev_stc_data22 = 0x20;
	smb_dev_stc_data_6.b.smb_dev_stc_pec = 0xda;
	smb_dev_stc_data_6.b.smb_dev_inf_ry = 0x1;
	writel(smb_dev_stc_data_6.all, &smb_slv->smb_dev_stc_data_6);

	/* set UDID data */
	// smb_dev_udid_3_t smb_dev_udid_3;
	// smb_dev_udid_3.b.smb_dev_stc_data00 = 0x21;
	// smb_dev_udid_3.b.smb_dev_stc_data01_68 = 0x22;
	// smb_dev_udid_3.b.smb_dev_stc_data02_68 = 0x23;
	// smb_dev_udid_3.b.smb_dev_stc_data03_68 = 0x24;
	// writel(smb_dev_udid_3.all, &smb_slv->smb_dev_udid_3);
	//
	// smb_dev_udid_2_t smb_dev_udid_2;
	// smb_dev_udid_2.b.smb_dev_stc_data04_64 = 0x25;
	// smb_dev_udid_2.b.smb_dev_stc_data05_64 = 0x26;
	// smb_dev_udid_2.b.smb_dev_stc_data06_64 = 0x27;
	// smb_dev_udid_2.b.smb_dev_stc_data07_64 = 0x28;
	// writel(smb_dev_udid_2.all, &smb_slv->smb_dev_udid_2);
	//
	// smb_dev_udid_1_t smb_dev_udid_1;
	// smb_dev_udid_1.b.smb_dev_stc_data08_60 = 0x29;
	// smb_dev_udid_1.b.smb_dev_stc_data09_60 = 0x2a;
	// smb_dev_udid_1.b.smb_dev_stc_data10_60 = 0x2b;
	// smb_dev_udid_1.b.smb_dev_stc_data11_60 = 0x2c;
	// writel(smb_dev_udid_1.all, &smb_slv->smb_dev_udid_1);
	//
	// smb_dev_udid_0_t smb_dev_udid_0;
	// smb_dev_udid_0.b.smb_dev_stc_data12_5c = 0x2d;
	// smb_dev_udid_0.b.smb_dev_stc_data13_5c = 0x2e;
	// smb_dev_udid_0.b.smb_dev_stc_data14_5c = 0x2f;
	// smb_dev_udid_0.b.smb_dev_stc_data15_5c = 0x30;
	// writel(smb_dev_udid_0.all, &smb_slv->smb_dev_udid_0);

	/* setup VPD */
	// smb_vpd_01_t smb_vpd_01;
	// smb_vpd_01.b.smb_vpd_00 = 0x32;
	// smb_vpd_01.b.smb_vpd_01 = 0x33;
	// smb_vpd_01.b.smb_vpd_02 = 0x34;
	// smb_vpd_01.b.smb_vpd_03 = 0x35;
	// writel(smb_vpd_01.all, &smb_slv->smb_vpd_01);
	//
	// smb_vpd_02_t smb_vpd_02;
	// smb_vpd_02.b.smb_vpd_04 = 0x32;
	// smb_vpd_02.b.smb_vpd_05 = 0x33;
	// smb_vpd_02.b.smb_vpd_06 = 0x34;
	// smb_vpd_02.b.smb_vpd_07 = 0x35;
	// writel(smb_vpd_02.all, &smb_slv->smb_vpd_02);
	//
	// smb_vpd_03_t smb_vpd_03;
	// smb_vpd_03.b.smb_vpd_08 = 0x36;
	// smb_vpd_03.b.smb_vpd_09 = 0x37;
	// smb_vpd_03.b.smb_vpd_10 = 0x38;
	// smb_vpd_03.b.smb_vpd_11 = 0x39;
	// writel(smb_vpd_03.all, &smb_slv->smb_vpd_03);
	//
	// smb_vpd_04_t smb_vpd_04;
	// smb_vpd_04.b.smb_vpd_12 = 0x3a;
	// smb_vpd_04.b.smb_vpd_13 = 0x3b;
	// smb_vpd_04.b.smb_vpd_14 = 0x3c;
	// smb_vpd_04.b.smb_vpd_15 = 0x3d;
	// writel(smb_vpd_04.all, &smb_slv->smb_vpd_04);
	//
	// smb_vpd_05_t smb_vpd_05;
	// smb_vpd_05.b.smb_vpd_16 = 0x3e;
	// smb_vpd_05.b.smb_vpd_17 = 0x3f;
	// smb_vpd_05.b.smb_vpd_18 = 0x40;
	// smb_vpd_05.b.smb_vpd_19 = 0x41;
	// writel(smb_vpd_05.all, &smb_slv->smb_vpd_05);
	//
	// smb_vpd_06_t smb_vpd_06;
	// smb_vpd_06.b.smb_vpd_20 = 0x42;
	// smb_vpd_06.b.smb_vpd_21 = 0x43;
	// smb_vpd_06.b.smb_vpd_22 = 0x44;
	// smb_vpd_06.b.smb_vpd_23 = 0x45;
	// writel(smb_vpd_06.all, &smb_slv->smb_vpd_06);
	//
	// smb_vpd_07_t smb_vpd_07;
	// smb_vpd_07.b.smb_vpd_24 = 0x46;
	// smb_vpd_07.b.smb_vpd_25 = 0x47;
	// smb_vpd_07.b.smb_vpd_26 = 0x48;
	// smb_vpd_07.b.smb_vpd_27 = 0x49;
	// writel(smb_vpd_07.all, &smb_slv->smb_vpd_07);
	//
	// smb_vpd_08_t smb_vpd_08;
	// smb_vpd_08.b.smb_vpd_28 = 0x4a;
	// smb_vpd_08.b.smb_vpd_29 = 0x4b;
	// smb_vpd_08.b.smb_vpd_30 = 0x4c;
	// smb_vpd_08.b.smb_vpd_31 = 0x4d;
	// writel(smb_vpd_08.all, &smb_slv->smb_vpd_08);
	//
	// smb_vpd_09_t smb_vpd_09;
	// smb_vpd_09.b.smb_vpd_32 = 0x4e;
	// smb_vpd_09.b.smb_vpd_33 = 0x4f;
	// smb_vpd_09.b.smb_vpd_34 = 0x50;
	// smb_vpd_09.b.smb_vpd_35 = 0x51;
	// writel(smb_vpd_09.all, &smb_slv->smb_vpd_09);
	//
	// smb_vpd_10_t smb_vpd_10;
	// smb_vpd_10.b.smb_vpd_36 = 0x52;
	// smb_vpd_10.b.smb_vpd_37 = 0x53;
	// smb_vpd_10.b.smb_vpd_38 = 0x54;
	// smb_vpd_10.b.smb_vpd_39 = 0x55;
	// writel(smb_vpd_10.all, &smb_slv->smb_vpd_10);
	//
	// smb_vpd_11_t smb_vpd_11;
	// smb_vpd_11.b.smb_vpd_40 = 0x56;
	// smb_vpd_11.b.smb_vpd_41 = 0x57;
	// smb_vpd_11.b.smb_vpd_42 = 0x58;
	// smb_vpd_11.b.smb_vpd_43 = 0x59;
	// writel(smb_vpd_11.all, &smb_slv->smb_vpd_11);
	//
	// smb_vpd_12_t smb_vpd_12;
	// smb_vpd_12.b.smb_vpd_44 = 0x5a;
	// smb_vpd_12.b.smb_vpd_45 = 0x5b;
	// smb_vpd_12.b.smb_vpd_46 = 0x5c;
	// smb_vpd_12.b.smb_vpd_47 = 0x5d;
	// writel(smb_vpd_12.all, &smb_slv->smb_vpd_12);
	//
	// smb_vpd_13_t smb_vpd_13;
	// smb_vpd_13.b.smb_vpd_48 = 0x5e;
	// smb_vpd_13.b.smb_vpd_49 = 0x5f;
	// smb_vpd_13.b.smb_vpd_50 = 0x60;
	// smb_vpd_13.b.smb_vpd_51 = 0x61;
	// writel(smb_vpd_13.all, &smb_slv->smb_vpd_13);
	//
	// smb_vpd_14_t smb_vpd_14;
	// smb_vpd_14.b.smb_vpd_52 = 0x62;
	// smb_vpd_14.b.smb_vpd_53 = 0x63;
	// smb_vpd_14.b.smb_vpd_54 = 0x64;
	// smb_vpd_14.b.smb_vpd_55 = 0x65;
	// writel(smb_vpd_14.all, &smb_slv->smb_vpd_14);
	//
	// smb_vpd_15_t smb_vpd_15;
	// smb_vpd_15.b.smb_vpd_56 = 0x66;
	// smb_vpd_15.b.smb_vpd_57 = 0x67;
	// smb_vpd_15.b.smb_vpd_58 = 0x68;
	// smb_vpd_15.b.smb_vpd_59 = 0x69;
	// writel(smb_vpd_15.all, &smb_slv->smb_vpd_15);
	//
	// smb_vpd_16_t smb_vpd_16;
	// smb_vpd_16.b.smb_vpd_60 = 0x6a;
	// smb_vpd_16.b.smb_vpd_61 = 0x6b;
	// smb_vpd_16.b.smb_vpd_62 = 0x6c;
	// smb_vpd_16.b.smb_vpd_63 = 0x6d;
	// writel(smb_vpd_16.all, &smb_slv->smb_vpd_16);
	//
	// smb_vpd_17_t smb_vpd_17;
	// smb_vpd_17.b.smb_vpd_64 = 0x6e;
	// smb_vpd_17.b.smb_vpd_65 = 0x6f;
	// smb_vpd_17.b.smb_vpd_66 = 0x70;
	// smb_vpd_17.b.smb_vpd_67 = 0x71;
	// writel(smb_vpd_17.all, &smb_slv->smb_vpd_17);
	//
	// smb_vpd_18_t smb_vpd_18;
	// smb_vpd_18.b.smb_vpd_68 = 0x72;
	// smb_vpd_18.b.smb_vpd_69 = 0x73;
	// smb_vpd_18.b.smb_vpd_70 = 0x74;
	// smb_vpd_18.b.smb_vpd_71 = 0x75;
	// writel(smb_vpd_18.all, &smb_slv->smb_vpd_18);
	//
	// smb_vpd_19_t smb_vpd_19;
	// smb_vpd_19.b.smb_vpd_72 = 0x76;
	// smb_vpd_19.b.smb_vpd_73 = 0x77;
	// smb_vpd_19.b.smb_vpd_74 = 0x78;
	// smb_vpd_19.b.smb_vpd_75 = 0x79;
	// writel(smb_vpd_19.all, &smb_slv->smb_vpd_19);

	/* setup drives identify data */
	smb_dev_idf_data_1_t smb_dev_idf_data_1;
	smb_dev_idf_data_1.b.smb_dev_idf_data01 = 0x80;
	smb_dev_idf_data_1.b.smb_dev_idf_data02 = 0x81;
	smb_dev_idf_data_1.b.smb_dev_idf_data03 = 0x82;
	smb_dev_idf_data_1.b.smb_dev_idf_data04 = 0x83;
	writel(smb_dev_idf_data_1.all, &smb_slv->smb_dev_idf_data_1);
	//
	smb_dev_idf_data_2_t smb_dev_idf_data_2;
	smb_dev_idf_data_2.b.smb_dev_idf_data05 = 0x84;
	smb_dev_idf_data_2.b.smb_dev_idf_data06 = 0x85;
	smb_dev_idf_data_2.b.smb_dev_idf_data07 = 0x86;
	smb_dev_idf_data_2.b.smb_dev_idf_data08 = 0x87;
	writel(smb_dev_idf_data_2.all, &smb_slv->smb_dev_idf_data_2);
	//
	smb_dev_idf_data_3_t smb_dev_idf_data_3;
	smb_dev_idf_data_3.b.smb_dev_idf_data09 = 0x88;
	smb_dev_idf_data_3.b.smb_dev_idf_data10 = 0x89;
	smb_dev_idf_data_3.b.smb_dev_idf_data11 = 0x8a;
	smb_dev_idf_data_3.b.smb_dev_idf_data12 = 0x8b;
	writel(smb_dev_idf_data_3.all, &smb_slv->smb_dev_idf_data_3);
	//
	smb_dev_idf_data_4_t smb_dev_idf_data_4;
	smb_dev_idf_data_4.b.smb_dev_idf_data13 = 0x8c;
	smb_dev_idf_data_4.b.smb_dev_idf_data14 = 0x8d;
	smb_dev_idf_data_4.b.smb_dev_idf_data15 = 0x8e;
	smb_dev_idf_data_4.b.smb_dev_idf_data16 = 0x8f;
	writel(smb_dev_idf_data_4.all, &smb_slv->smb_dev_idf_data_4);
	//
	smb_dev_idf_data_5_t smb_dev_idf_data_5;
	smb_dev_idf_data_5.b.smb_dev_idf_data17 = 0x90;
	smb_dev_idf_data_5.b.smb_dev_idf_data18 = 0x91;
	smb_dev_idf_data_5.b.smb_dev_idf_data19 = 0x92;
	smb_dev_idf_data_5.b.smb_dev_idf_data20 = 0x93;
	writel(smb_dev_idf_data_5.all, &smb_slv->smb_dev_idf_data_5);
	//
	smb_dev_idf_data_6_t smb_dev_idf_data_6;
	smb_dev_idf_data_6.b.smb_dev_idf_data21 = 0x94;
	smb_dev_idf_data_6.b.smb_dev_idf_data22 = 0x95;
	smb_dev_idf_data_6.b.smb_dev_idf_data23 = 0x96;
	smb_dev_idf_data_6.b.smb_dev_idf_data24 = 0x97;
	writel(smb_dev_idf_data_6.all, &smb_slv->smb_dev_idf_data_6);
	//
	// smb_dev_idf_data_7_t smb_dev_idf_data_7;
	// smb_dev_idf_data_7.b.smb_dev_idf_data25 = 0x98;
	// smb_dev_idf_data_7.b.smb_dev_idf_data26 = 0x99;
	// smb_dev_idf_data_7.b.smb_dev_idf_data27 = 0x9a;
	// smb_dev_idf_data_7.b.smb_dev_idf_data28 = 0x9b;
	// writel(smb_dev_idf_data_7.all, &smb_slv->smb_dev_idf_data_7);
	//
	// smb_dev_idf_data_8_t smb_dev_idf_data_8;
	// smb_dev_idf_data_8.b.smb_dev_idf_data29 = 0x9c;
	// smb_dev_idf_data_8.b.smb_dev_idf_data30 = 0x9d;
	// smb_dev_idf_data_8.b.smb_dev_idf_data31 = 0x9e;
	// smb_dev_idf_data_8.b.smb_dev_idf_data32 = 0x9f;
	// writel(smb_dev_idf_data_8.all, &smb_slv->smb_dev_idf_data_8);
	//
	// smb_dev_idf_data_9_t smb_dev_idf_data_9;
	// smb_dev_idf_data_9.b.smb_dev_idf_data33 = 0xa0;
	// smb_dev_idf_data_9.b.smb_dev_idf_data34 = 0xa1;
	// smb_dev_idf_data_9.b.smb_dev_idf_data35 = 0xa2;
	// smb_dev_idf_data_9.b.smb_dev_idf_data36 = 0xa3;
	// writel(smb_dev_idf_data_9.all, &smb_slv->smb_dev_idf_data_9);
	//
	// smb_dev_idf_data_10_t smb_dev_idf_data_10;
	// smb_dev_idf_data_10.b.smb_dev_idf_data37 = 0xa4;
	// smb_dev_idf_data_10.b.smb_dev_idf_data38 = 0xa5;
	// smb_dev_idf_data_10.b.smb_dev_idf_data39 = 0xa6;
	// smb_dev_idf_data_10.b.smb_dev_idf_data40 = 0xa7;
	// writel(smb_dev_idf_data_10.all, &smb_slv->smb_dev_idf_data_10);
	//
	// smb_dev_idf_data_11_t smb_dev_idf_data_11;
	// smb_dev_idf_data_11.b.smb_dev_idf_data41 = 0xa8;
	// smb_dev_idf_data_11.b.smb_dev_idf_data42 = 0xa9;
	// smb_dev_idf_data_11.b.smb_dev_idf_data43 = 0xaa;
	// smb_dev_idf_data_11.b.smb_dev_idf_data44 = 0xab;
	// writel(smb_dev_idf_data_11.all, &smb_slv->smb_dev_idf_data_11);
	//
	// smb_dev_idf_data_12_t smb_dev_idf_data_12;
	// smb_dev_idf_data_12.b.smb_dev_idf_data45 = 0xac;
	// smb_dev_idf_data_12.b.smb_dev_idf_data46 = 0xad;
	// smb_dev_idf_data_12.b.smb_dev_idf_data47 = 0xae;
	// smb_dev_idf_data_12.b.smb_dev_idf_data48 = 0xaf;
	// writel(smb_dev_idf_data_12.all, &smb_slv->smb_dev_idf_data_12);
	//
	// smb_dev_idf_data_13_t smb_dev_idf_data_13;
	// smb_dev_idf_data_13.b.smb_dev_idf_data49 = 0xb0;
	// smb_dev_idf_data_13.b.smb_dev_idf_data50 = 0xb1;
	// smb_dev_idf_data_13.b.smb_dev_idf_data51 = 0xb2;
	// smb_dev_idf_data_13.b.smb_dev_idf_data52 = 0xb3;
	// writel(smb_dev_idf_data_13.all, &smb_slv->smb_dev_idf_data_13);
	//
	smb_dev_idf_data_14_t smb_dev_idf_data_14;
	smb_dev_idf_data_14.all = 0x00;
	smb_dev_idf_data_14.b.smb_dev_idf_data53 = 0xb4;
	smb_dev_idf_data_14.b.smb_dev_idf_pec = 0xcc;
	writel(smb_dev_idf_data_14.all, &smb_slv->smb_dev_idf_data_14);

	/* setup drives storage data */
	// smb_dev_str_data_1_t smb_dev_str_data_1;
	// smb_dev_str_data_1.b.smb_dev_str_data01 = 0xb7;
	// smb_dev_str_data_1.b.smb_dev_str_data02 = 0xb8;
	// smb_dev_str_data_1.b.smb_dev_str_data03 = 0xb9;
	// smb_dev_str_data_1.b.smb_dev_str_data04 = 0xba;
	// writel(smb_dev_str_data_1.all, &smb_slv->smb_dev_str_data_1);
	//
	// smb_dev_str_data_2_t smb_dev_str_data_2;
	// smb_dev_str_data_2.b.smb_dev_str_data05 = 0xbb;
	// smb_dev_str_data_2.b.smb_dev_str_data06 = 0xbc;
	// smb_dev_str_data_2.b.smb_dev_str_data07 = 0xbd;
	// smb_dev_str_data_2.b.smb_dev_str_pec = 0xcc;
	// writel(smb_dev_str_data_2.all, &smb_slv->smb_dev_str_data_2);

	/* set smb_i2c_det_data */
	smb_i2c_det_data_t smb_i2c_det_data;
	smb_i2c_det_data.b.smb_i2c_det_nvme_dat = 0x06;
	smb_i2c_det_data.b.smb_i2c_det_vpd_dat = 0xff;
	smb_i2c_det_data.b.smb_i2c_det_1b_dat = 0xff;
	smb_i2c_det_data.b.smb_i2c_det_33_dat = 0x00;
	writel(smb_i2c_det_data.all, &smb_slv->smb_i2c_det_data);

	/* set smb enable */
	smb_control_register_t smb_control_register;
	smb_control_register.all = 0;
	/* bit [1] */
	smb_control_register.b.smb_en = 0x01;
	/* Rainier A1 smb configuration - Basic MI */
#if defined(A1_DC_WA)
	/* bit [4] */
	smb_control_register.b.smb_clk_stretch_en = 0x01;
	/* bit [11:8] 0: smb_slave_mode; 5: i2c_slave_mode */
	smb_control_register.b.smb_tran_tp_sel = 0x09;// SM_WA
	/* bit [16] */
	smb_control_register.b.smb_slv_timer_opt = 0x01;// SM_WA
	/* bit [17] */
	smb_control_register.b.smb_tran_i2c_en = 0x01;// SM_WA
	writel(smb_control_register.all, &smb_slv->smb_control_register);
	/* mask not processing interrupts & clear any pending interrupts */
	smb_interrupt_mask_t mask = { .all = 0 , };
	mask.all = ~(INTR_MASK16_MASK | INTR_MASK6_MASK | INTR_MASK5_MASK | INTR_MASK3_MASK);
	writel(mask.all, &smb_slv->smb_interrupt_mask);
	smb_intr_sts_t sts = { .all = readl(&smb_slv->smb_intr_sts), };
	writel(sts.all, &smb_slv->smb_intr_sts);
#else

	/* bit [4] */
	smb_control_register.b.smb_clk_stretch_en = 0x01;
	/* bit [11:8] 0: smb_slave_mode; 5: i2c_slave_mode */
	smb_control_register.b.smb_tran_tp_sel = 0x00;
	/* bit [16] */
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	/* bit [17] */
	smb_control_register.b.smb_tran_i2c_en = 0x00;
	writel(smb_control_register.all, &smb_slv->smb_control_register);
#endif
}
slow_code void smb_static_data_FF(void) // 20210224 Jamie fast_code -> slow_code
{
		smb_dev_stc_data_1_t smb_dev_stc_data_1;
		smb_dev_stc_data_2_t smb_dev_stc_data_2;
		smb_dev_stc_data_3_t smb_dev_stc_data_3;
		smb_dev_stc_data_4_t smb_dev_stc_data_4;
		smb_dev_stc_data_1.b.smb_dev_stc_data01 = ~0;
		smb_dev_stc_data_1.b.smb_dev_stc_data02 = ~0;
		smb_dev_stc_data_1.b.smb_dev_stc_data03 = ~0;
		smb_dev_stc_data_1.b.smb_dev_stc_data04 = ~0;
		writel(smb_dev_stc_data_1.all, &smb_slv->smb_dev_stc_data_1);
		smb_dev_stc_data_2.b.smb_dev_stc_data05 = ~0;
		smb_dev_stc_data_2.b.smb_dev_stc_data06 = ~0;
		smb_dev_stc_data_2.b.smb_dev_stc_data07 = ~0;
		smb_dev_stc_data_2.b.smb_dev_stc_data08 = ~0;
		writel(smb_dev_stc_data_2.all, &smb_slv->smb_dev_stc_data_2);
		smb_dev_stc_data_3.b.smb_dev_stc_data09 = ~0;
		smb_dev_stc_data_3.b.smb_dev_stc_data10 = ~0;
		smb_dev_stc_data_3.b.smb_dev_stc_data11 = ~0;
		smb_dev_stc_data_3.b.smb_dev_stc_data12 = ~0;
		writel(smb_dev_stc_data_3.all, &smb_slv->smb_dev_stc_data_3);
		smb_dev_stc_data_4.b.smb_dev_stc_data13 = ~0;
		smb_dev_stc_data_4.b.smb_dev_stc_data14 = ~0;
		smb_dev_stc_data_4.b.smb_dev_stc_data15 = ~0;
		smb_dev_stc_data_4.b.smb_dev_stc_data16 = ~0;
		writel(smb_dev_stc_data_4.all, &smb_slv->smb_dev_stc_data_4);
#if (!Xfusion_case)&&(!Tencent_case)
#if defined(A1_DC_WA)	
		/* disable and enable smbus0 */
		smb_control_register_t ctrl = { .all = readl(&smb_slv->smb_control_register), };
		ctrl.b.smb_en = 0;
		writel(ctrl.all, &smb_slv->smb_control_register);
		ctrl.b.smb_en = 1;
		writel(ctrl.all, &smb_slv->smb_control_register);
#endif
#endif
}
extern void sirq_register(u32 sirq, irq_handler_t handler, bool poll);
slow_code void smb_i2c_slv_mode_9_init(void) // 20210224 Jamie fast_code -> slow_code
{ 
	extern void smb0_isr(void);
	smb_interrupt_mask_t smb_interrupt_mask;
	smb_interrupt_mask.all = ~0;
	writel(smb_interrupt_mask.all, &smb_slv->smb_interrupt_mask);
	writel(~0, &smb_slv->smb_intr_sts);
	sirq_register(SYS_VID_SMB0, smb0_isr, false);
	//misc_sys_isr_enable(SYS_VID_SMB0);
	smb_static_data_FF();
	smb_control_register_t smb_control_register;
	smb_control_register.all = 0;
	writel(~0, &smb_slv->smb_intr_sts);
	smb_interrupt_mask.b.intr_mask3 = 0;
	smb_interrupt_mask.b.intr_mask5 = 0;
	smb_interrupt_mask.b.intr_mask6 = 0;
	smb_interrupt_mask.b.intr_mask16= 0;
	writel(smb_interrupt_mask.all, &smb_slv->smb_interrupt_mask);
	writel(smb_control_register.all, &smb_slv->smb_control_register);
}
#endif
