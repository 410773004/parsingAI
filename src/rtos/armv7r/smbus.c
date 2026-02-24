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
//! @brief SMBUS initialization code.
//! @brief The Usage model is as follows:
//! @brief	- SMBUS0 is in SLAVE mode. This is interfaced to host with
//! @brief	  PCIe out-of-band SCL/SDA signals. This is used to support
//! @brief	  NVME MI Basic Management Commands.
//! @brief	- SMBUS 1 is in MASTER mode. All internal I2C slave devices
//! @brief	  like, TMP102 - temperature controller, MP5417 - Power
//! @brief	  Managment IC are connected to this bus.
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
#include "string.h"
#include "console.h"
#include "smbus.h"
#include "srb.h"
#include "ssstc_cmd.h"

#define __FILEID__ smb
#include "trace.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
/* SMBUS 0 Base Addr - Slave mode */
#define SMB0_SLV_BASE 0xC0052000		///< smbus 0 control register base address
/* SMBUS 1 Base Addr - Master mode */
#define SMB1_MAS_BASE 0xC0053000		///< smbus 1 control register base address
#define MI_VPD

#ifdef MI_VPD
#define VPD_SIZE 256
#else
#define VPD_SIZE 92
#endif
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
fast_data smb_registers_regs_t *smb_slv =
		(smb_registers_regs_t *)(SMB0_SLV_BASE);	///< smbus 0 control register
fast_data smb_registers_regs_t *smb_mas =
		(smb_registers_regs_t *)(SMB1_MAS_BASE);	///< smbus 1 control register
		  srb_t *srb = (srb_t *) SRAM_BASE;
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------

/*!
 * @brief SMBus master mode initialization
 *
 * @param none
 *
 * @return none
 */

extern void GPIO_Set(void);

/*!
 * @brief Calculate the 8-bit checksum for a sequence of hex data
 *        The 8-bit checksum is the 2's complement of the sum of all bytes
 * @param data	hex data to calculate checksum
 * @param len	length
 * @return none
 */
u8 checksum_8bit(u8 *data, u8 len) {
	int i;
	u8 sum = 0;
	for (i = 0; i < len; i++) {
		sum += data[i];
	}
	return (~sum)+1;
}

 ddr_code void smb_master_init(u8 strb_cnt)
{
	u32 data;
	smb_strobe_cnt_t smb_strobe_cnt;
	smb_timer_0_t smb_timer_0;
	smb_timer_1_t smb_timer_1;
	smb_timer_2_t smb_timer_2;
	smb_timer_3_t smb_timer_3;
	smb_timer_4_t smb_timer_4;
	smb_timer_5_t smb_timer_5;


	/* reset */
	writel(0x0000ffff, &smb_mas->smb_intr_sts);
	data = readl(&smb_mas->smb_intr_sts);
	rtos_smb_trace(LOG_DEBUG, 0xf2d3, "smb_intr_sts: %x", data);

	smb_strobe_cnt.all = 0;
	smb_strobe_cnt.b.smb_strobe_cnt = strb_cnt;
	writel(smb_strobe_cnt.all, &smb_mas->smb_strobe_cnt);

	/* set start a, b, c, d timer */
	smb_timer_0.all = 0;
	smb_timer_0.b.smb_start_a_timer = 0x62;
	smb_timer_0.b.smb_start_b_timer = 0x05;
	smb_timer_0.b.smb_start_c_timer = 0x55;
	smb_timer_0.b.smb_start_d_timer = 0x05;
	writel(smb_timer_0.all, &smb_mas->smb_timer_0);

	/* set stop a, b, c, d timer */
	smb_timer_1.all = 0;
	smb_timer_1.b.smb_stop_a_timer = 0x62;
	smb_timer_1.b.smb_stop_b_timer = 0x10;
	smb_timer_1.b.smb_stop_c_timer = 0x55;
	smb_timer_1.b.smb_stop_d_timer = 0x30;
	writel(smb_timer_1.all, &smb_mas->smb_timer_1);

	/* set wr a, b, c, d timer */
	smb_timer_2.all = 0;
	smb_timer_2.b.smb_wr_a_timer = 0x2E;
	smb_timer_2.b.smb_wr_b_timer = 0x2E;
	smb_timer_2.b.smb_wr_c_timer = 0x11;
	smb_timer_2.b.smb_wr_d_timer = 0x4F;
	writel(smb_timer_2.all, &smb_mas->smb_timer_2);

	/* set wr e, and rd a, b, c timer */
	smb_timer_3.all = 0;
	smb_timer_3.b.smb_wr_e_timer = 0x05;
	smb_timer_3.b.smb_rd_a_timer = 0x2E;
	smb_timer_3.b.smb_rd_b_timer = 0x2E;
	smb_timer_3.b.smb_rd_c_timer = 0x11;
	writel(smb_timer_3.all, &smb_mas->smb_timer_3);

	/* set rd d, e, and ack a, b timer */
	smb_timer_4.all = 0;
	smb_timer_4.b.smb_rd_d_timer = 0x4F;
	smb_timer_4.b.smb_rd_e_timer = 0x05;
	smb_timer_4.b.smb_ack_a_timer = 0x2E;
	smb_timer_4.b.smb_ack_b_timer = 0x2E;
	writel(smb_timer_4.all, &smb_mas->smb_timer_4);

	/* set ack c, d, e, and start r timer */
	smb_timer_5.all = 0;
	smb_timer_5.b.smb_ack_c_timer = 0x11;
	smb_timer_5.b.smb_ack_d_timer = 0x4F;
	smb_timer_5.b.smb_ack_e_timer = 0x05;
	smb_timer_5.b.smb_start_r_timer = 0x5B;
	writel(smb_timer_5.all, &smb_mas->smb_timer_5);
}

ddr_code void smb_master_init_VPD(u8 strb_cnt)				//Shane add for VPD_init VU CMD 20200813
{
	u32 data;
	smb_strobe_cnt_t smb_strobe_cnt;
	smb_timer_0_t smb_timer_0;
	smb_timer_1_t smb_timer_1;
	smb_timer_2_t smb_timer_2;
	smb_timer_3_t smb_timer_3;
	smb_timer_4_t smb_timer_4;
	smb_timer_5_t smb_timer_5;


	/* reset */
	writel(0x0000ffff, &smb_slv->smb_intr_sts);
	data = readl(&smb_slv->smb_intr_sts);
	rtos_smb_trace(LOG_DEBUG, 0xeb40, "smb_intr_sts: %x", data);

	smb_strobe_cnt.all = 0;
	smb_strobe_cnt.b.smb_strobe_cnt = strb_cnt;
	writel(smb_strobe_cnt.all, &smb_slv->smb_strobe_cnt);

	/* set start a, b, c, d timer */
	smb_timer_0.all = 0;
	smb_timer_0.b.smb_start_a_timer = 0x62;
	smb_timer_0.b.smb_start_b_timer = 0x05;
	smb_timer_0.b.smb_start_c_timer = 0x55;
	smb_timer_0.b.smb_start_d_timer = 0x05;
	writel(smb_timer_0.all, &smb_slv->smb_timer_0);

	/* set stop a, b, c, d timer */
	smb_timer_1.all = 0;
	smb_timer_1.b.smb_stop_a_timer = 0x62;
	smb_timer_1.b.smb_stop_b_timer = 0x10;
	smb_timer_1.b.smb_stop_c_timer = 0x55;
	smb_timer_1.b.smb_stop_d_timer = 0x30;
	writel(smb_timer_1.all, &smb_slv->smb_timer_1);

	/* set wr a, b, c, d timer */
	smb_timer_2.all = 0;
	smb_timer_2.b.smb_wr_a_timer = 0x2E;
	smb_timer_2.b.smb_wr_b_timer = 0x2E;
	smb_timer_2.b.smb_wr_c_timer = 0x11;
	smb_timer_2.b.smb_wr_d_timer = 0x4F;
	writel(smb_timer_2.all, &smb_slv->smb_timer_2);

	/* set wr e, and rd a, b, c timer */
	smb_timer_3.all = 0;
	smb_timer_3.b.smb_wr_e_timer = 0x05;
	smb_timer_3.b.smb_rd_a_timer = 0x2E;
	smb_timer_3.b.smb_rd_b_timer = 0x2E;
	smb_timer_3.b.smb_rd_c_timer = 0x11;
	writel(smb_timer_3.all, &smb_slv->smb_timer_3);

	/* set rd d, e, and ack a, b timer */
	smb_timer_4.all = 0;
	smb_timer_4.b.smb_rd_d_timer = 0x4F;
	smb_timer_4.b.smb_rd_e_timer = 0x05;
	smb_timer_4.b.smb_ack_a_timer = 0x2E;
	smb_timer_4.b.smb_ack_b_timer = 0x2E;
	writel(smb_timer_4.all, &smb_slv->smb_timer_4);

	/* set ack c, d, e, and start r timer */
	smb_timer_5.all = 0;
	smb_timer_5.b.smb_ack_c_timer = 0x11;
	smb_timer_5.b.smb_ack_d_timer = 0x4F;
	smb_timer_5.b.smb_ack_e_timer = 0x05;
	smb_timer_5.b.smb_start_r_timer = 0x5B;
	writel(smb_timer_5.all, &smb_slv->smb_timer_5);
}
extern AGING_TEST_MAP_t *MPIN;
extern void smb_i2c_slv_mode_9_init(void);

ps_code const char* getMnValue(int cap_idx) {
#if MDOT2_SUPPORT == 1
    #if Mdot2_22110 == 1
    	if (cap_idx == CAP_SIZE_8T) {
    		return "SSSTC PJ1-KW7680P";
    	} else if (cap_idx == CAP_SIZE_4T) {
    		return "SSSTC PJ1-KW3840P";
    	} else if (cap_idx == CAP_SIZE_2T) {
    		return "SSSTC PJ1-KW1920P";
    	} else if (cap_idx == CAP_SIZE_1T) {
    		return "SSSTC PJ1-KW960P";
    	} else if (cap_idx == CAP_SIZE_512G) {
    		return "SSSTC PJ1-KW480P";
    	} else {
    		return "SSSTC PJ1-KWXXXP";
    	}
    #elif E1S == 1
    	if (cap_idx == CAP_SIZE_8T) {
    		return "SSSTC PJ1-6W7680P";
    	} else if (cap_idx == CAP_SIZE_4T) {
    		return "SSSTC PJ1-6W3840P";
    	} else if (cap_idx == CAP_SIZE_2T) {
    		return "SSSTC PJ1-6W1920P";
    	} else if (cap_idx == CAP_SIZE_1T) {
    		return "SSSTC PJ1-6W960P";
    	} else if (cap_idx == CAP_SIZE_512G) {
    		return "SSSTC PJ1-6W480P";
    	} else {
    		return "SSSTC PJ1-6WXXXP";
    	}
    #else
    	#if PLP_SUPPORT == 1
    		#if PURE_SLC == 1
    			if (cap_idx == CAP_SIZE_8T) {
    		        return "SSSTC PJ1-GT960S";
    		    } else if (cap_idx == CAP_SIZE_4T) {
    		        return "SSSTC PJ1-GT800S";
    		    } else if (cap_idx == CAP_SIZE_2T) {
    		        return "SSSTC PJ1-GT640S";
    		    } else if (cap_idx == CAP_SIZE_1T) {
    		        return "SSSTC PJ1-GT320S";
    		    } else if (cap_idx == CAP_SIZE_512G) {
    		        return "SSSTC PJ1-GT160S";
    		    } else {
    		        return "SSSTC PJ1-GTXXXS";
    		    }
    		#else
				#if (Synology_case)
					if (cap_idx == CAP_SIZE_8T) {
	    		        return "SNV5420-6400G";
	    		    } else if (cap_idx == CAP_SIZE_4T) {
	    		        return "SNV5420-3200G";
	    		    } else if (cap_idx == CAP_SIZE_2T) {
	    		        return "SNV5420-1600G";
	    		    } else if (cap_idx == CAP_SIZE_1T) {
	    		        return "SNV5420-800G";
	    		    } else if (cap_idx == CAP_SIZE_512G) {
	    		        return "SNV5420-400G";
	    		    } else {
	    		        return "SNV5420-XXXG";
	    		    }
				#elif (Smart_Modular_case)
					if (cap_idx == CAP_SIZE_8T) {
	    		        return "FDMP87680F1S11G1";
	    		    } else if (cap_idx == CAP_SIZE_4T) {
	    		        return "FDMP83840F1S11G1";
	    		    } else if (cap_idx == CAP_SIZE_2T) {
	    		        return "FDMP81920F1S11G1";
	    		    } else if (cap_idx == CAP_SIZE_1T) {
	    		        return "FDMP8960GF1S11G1";
	    		    } else if (cap_idx == CAP_SIZE_512G) {
	    		        return "FDMP8480GF1S11G1";
	    		    } else {
	    		        return "FDMP8480GF1S11G1";
	    		    }
				#else
					if (cap_idx == CAP_SIZE_8T) {
	    		        return "SSSTC PJ1-GW7680P";
	    		    } else if (cap_idx == CAP_SIZE_4T) {
	    		        return "SSSTC PJ1-GW3840P";
	    		    } else if (cap_idx == CAP_SIZE_2T) {
	    		        return "SSSTC PJ1-GW1920P";
	    		    } else if (cap_idx == CAP_SIZE_1T) {
	    		        return "SSSTC PJ1-GW960P";
	    		    } else if (cap_idx == CAP_SIZE_512G) {
	    		        return "SSSTC PJ1-GW480P";
	    		    } else {
	    		        return "SSSTC PJ1-GWXXXP";
	    		    }
				#endif
    		#endif
    	#else
    		#if PURE_SLC == 1
    			if (cap_idx == CAP_SIZE_8T) {
    		        return "SSSTC PJ1-GT960N";
    		    } else if (cap_idx == CAP_SIZE_4T) {
    		        return "SSSTC PJ1-GT800N";
    		    } else if (cap_idx == CAP_SIZE_2T) {
    		        return "SSSTC PJ1-GT640N";
    		    } else if (cap_idx == CAP_SIZE_1T) {
    		        return "SSSTC PJ1-GT320N";
    		    } else if (cap_idx == CAP_SIZE_512G) {
    		        return "SSSTC PJ1-GT160N";
    		    } else {
    		        return "SSSTC PJ1-GTXXXN";
    		    }
    		#else
				#if (Synology_case)
					if (cap_idx == CAP_SIZE_8T) {
	    		        return "SNV5420-6400G";
	    		    } else if (cap_idx == CAP_SIZE_4T) {
	    		        return "SNV5420-3200G";
	    		    } else if (cap_idx == CAP_SIZE_2T) {
	    		        return "SNV5420-1600G";
	    		    } else if (cap_idx == CAP_SIZE_1T) {
	    		        return "SNV5420-800G";
	    		    } else if (cap_idx == CAP_SIZE_512G) {
	    		        return "SNV5420-400G";
	    		    } else {
	    		        return "SNV5420-XXXG";
	    		    }
				#elif (Smart_Modular_case)
					if (cap_idx == CAP_SIZE_8T) {
	    		        return "FDMP87680FCS11G1";
	    		    } else if (cap_idx == CAP_SIZE_4T) {
	    		        return "FDMP83840FCS11G1";
	    		    } else if (cap_idx == CAP_SIZE_2T) {
	    		        return "FDMP81920FCS11G1";
	    		    } else if (cap_idx == CAP_SIZE_1T) {
	    		        return "FDMP8960GFCS11G1";
	    		    } else if (cap_idx == CAP_SIZE_512G) {
	    		        return "FDMP8480GFCS11G1";
	    		    } else {
	    		        return "FDMP8480GFCS11G1";
	    		    }
				#else
	    			if (cap_idx == CAP_SIZE_8T) {
	    		        return "SSSTC PJ1-GW7680";
	    		    } else if (cap_idx == CAP_SIZE_4T) {
	    		        return "SSSTC PJ1-GW3840";
	    		    } else if (cap_idx == CAP_SIZE_2T) {
	    		        return "SSSTC PJ1-GW1920";
	    		    } else if (cap_idx == CAP_SIZE_1T) {
	    		        return "SSSTC PJ1-GW960";
	    		    } else if (cap_idx == CAP_SIZE_512G) {
	    		        return "SSSTC PJ1-GW480";
	    		    } else {
	    		        return "SSSTC PJ1-GWXXX";
	    		    }
				#endif
    		#endif
    	#endif
    #endif	
#elif UDOT2_SUPPORT == 1
	if (cap_idx == CAP_SIZE_8T) {
        return "SSSTC PJ1-2W7680P";
    } else if (cap_idx == CAP_SIZE_4T) {
        return "SSSTC PJ1-2W3840P";
    } else if (cap_idx == CAP_SIZE_2T) {
        return "SSSTC PJ1-2W1920P";
    } else if (cap_idx == CAP_SIZE_1T) {
        return "SSSTC PJ1-2W960P";
    } else if (cap_idx == CAP_SIZE_512G) {
        return "SSSTC PJ1-2W480P";
    } else {
        return "SSSTC PJ1-2WXXXP";
    }
#else
	#if PLP_SUPPORT == 1
		#if PURE_SLC == 1 
			if (cap_idx == CAP_SIZE_8T) {
				return "SSSTC PJ1-GT960S";
			} else if (cap_idx == CAP_SIZE_4T) {
				return "SSSTC PJ1-GT800S";
			} else if (cap_idx == CAP_SIZE_2T) {
				return "SSSTC PJ1-GT640S";
			} else if (cap_idx == CAP_SIZE_1T) {
				return "SSSTC PJ1-GT320S";
			} else if (cap_idx == CAP_SIZE_512G) {
				return "SSSTC PJ1-GT160S";
			} else {
				return "SSSTC PJ1-GTXXXS";
			}
		#else
			 if (cap_idx == CAP_SIZE_8T) {
				return "SSSTC PJ1-GW7680P";
			} else if (cap_idx == CAP_SIZE_4T) {
				return "SSSTC PJ1-GW3840P";
			} else if (cap_idx == CAP_SIZE_2T) {
				return "SSSTC PJ1-GW1920P";
			} else if (cap_idx == CAP_SIZE_1T) {
				return "SSSTC PJ1-GW960P";
			} else if (cap_idx == CAP_SIZE_512G) {
				return "SSSTC PJ1-GW480P";
			} else {
				return "SSSTC PJ1-GWXXXP";
			}
		#endif
	#else
		#if PURE_SLC == 1
			if (cap_idx == CAP_SIZE_8T) {
				return "SSSTC PJ1-GT960N";
			} else if (cap_idx == CAP_SIZE_4T) {
				return "SSSTC PJ1-GT800N";
			} else if (cap_idx == CAP_SIZE_2T) {
				return "SSSTC PJ1-GT640N";
			} else if (cap_idx == CAP_SIZE_1T) {
				return "SSSTC PJ1-GT320N";
			} else if (cap_idx == CAP_SIZE_512G) {
				return "SSSTC PJ1-GT160N";
			} else {
				return "SSSTC PJ1-GTXXXN";
			}
		#else
			if (cap_idx == CAP_SIZE_8T) {
				return "SSSTC PJ1-GW7680";
			} else if (cap_idx == CAP_SIZE_4T) {
				return "SSSTC PJ1-GW3840";
			} else if (cap_idx == CAP_SIZE_2T) {
				return "SSSTC PJ1-GW1920";
			} else if (cap_idx == CAP_SIZE_1T) {
				return "SSSTC PJ1-GW960";
			} else if (cap_idx == CAP_SIZE_512G) {
				return "SSSTC PJ1-GW480";
			} else {
				return "SSSTC PJ1-GWXXX";
			}
		#endif
	#endif
#endif
}


ddr_code u32 VPD_blk_write(u8 cmd_code, u8 *vpd)						//Shane add for VPD_init VU CMD 20200813  //
{
	smb_init_0_t smb_init_0;
	smb_init_wr_1_t smb_wr_1_data;
	smb_control_register_t smb_control_register;
	u32 cnt = 0;
	u32 sts;
	u16 len = 0;
	u8 len_pn = 0;
	#ifndef Xfusion_case
	u8 len_mn = 0;
	u8 len_sn = 0;
	#endif
	u8 pn_start = 0;
#ifdef MI_VPD //follow NVMe MI spec, also same as PM9A3/P4510/P5510
	const char* MN = getMnValue(srb->cap_idx);

    u8 cmn_hdr[8]  = {0x01, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x00, 0xEF}; //common header
	// SSTSC
	u8 PIA_hdr[12] = {0x01, 0x0E, 0x19, 0xC8, 0x53, 0x53, 0x53, 0x54, 0x43, 0x00, 0x00, 0x00}; // Product Info Area header

	char MN_M[41] = {0};
	strncpy(MN_M, MN, sizeof(MN_M) - 1); // Copy at most sizeof(MN_M) - 1 characters to leave space for the null terminator
	MN_M[sizeof(MN_M) - 1] = '\0'; // Ensure null termination
    memset(MN_M + strlen(MN), 0, sizeof(MN_M) - strlen(MN) - 1);  // Fill the remaining space with 0



	#if Synology_case
		pn_start = 0;
	#elif Smart_Modular_case
		pn_start = 0;
	#else
		pn_start = 6;
	#endif
	char SN_VPD[20];
	memcpy(SN_VPD, MPIN->drive_serial_number, 16);
	SN_VPD[16] = 0x00;
	SN_VPD[17] = 0x00;
	SN_VPD[18] = 0x00;
	SN_VPD[19] = 0x00;
	u8 NMRIA_hdr[6]   = {0x0B, 0x02, 0x3B, 0x00, 0x00, 0x00}; // NVMe Mulit-Record Info Area header
	u8 NMRIA_data[32] = {0x00,0x34,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x09};
	u8 PMRIA_hdr[6]   = {0x0C, 0x82, 0x0B, 0x00, 0x00, 0x01}; // PCIe Mulit-Record Info Area header
	u8 PMRIA_data[11] = {0x01, 0x00, 0x01, 0x0F, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00};
	if(srb->cap_idx == CAP_SIZE_4T) { // little endian
		NMRIA_data[19] = 0x00;
		NMRIA_data[20] = 0x60;
		NMRIA_data[21] = 0xE5;
		NMRIA_data[22] = 0x3E;
		NMRIA_data[23] = 0x7E;
		NMRIA_data[24] = 0x03;
	}
	else if(srb->cap_idx == CAP_SIZE_8T) {
		NMRIA_data[19] = 0x00;
		NMRIA_data[20] = 0x60;
		NMRIA_data[21] = 0x25;
		NMRIA_data[22] = 0x7D;
		NMRIA_data[23] = 0xFC;
		NMRIA_data[24] = 0x06;
	}
	else if(srb->cap_idx == CAP_SIZE_2T) {
		NMRIA_data[11] = 0x0C;
		NMRIA_data[18] = 0x0C;
		#if PURE_SLC == 1
			NMRIA_data[19] = 0x00;
			NMRIA_data[20] = 0x20;
			NMRIA_data[21] = 0x97;
			NMRIA_data[22] = 0x0A;
			NMRIA_data[23] = 0x95;
			NMRIA_data[24] = 0x00;
		#else
			NMRIA_data[19] = 0x00;
			NMRIA_data[20] = 0x60;
			NMRIA_data[21] = 0xC5;
			NMRIA_data[22] = 0x1F;
			NMRIA_data[23] = 0xBF;
			NMRIA_data[24] = 0x01;
		#endif
	}
	else if(srb->cap_idx == CAP_SIZE_1T) {
		NMRIA_data[11] = 0x09;
		NMRIA_data[18] = 0x09;
		#if PURE_SLC == 1
			NMRIA_data[19] = 0x00;
			NMRIA_data[20] = 0x20;
			NMRIA_data[21] = 0x67;
			NMRIA_data[22] = 0x85;
			NMRIA_data[23] = 0x4A;
			NMRIA_data[24] = 0x00;
		#else
			NMRIA_data[19] = 0x00;
			NMRIA_data[20] = 0x60;
			NMRIA_data[21] = 0x35;
			NMRIA_data[22] = 0x90;
			NMRIA_data[23] = 0xDF;
			NMRIA_data[24] = 0x00;
		#endif
	}
	else if(srb->cap_idx == CAP_SIZE_512G) {
		NMRIA_data[11] = 0x09;
		NMRIA_data[18] = 0x09;
		#if PURE_SLC == 1
			NMRIA_data[19] = 0x00;
			NMRIA_data[20] = 0x20;
			NMRIA_data[21] = 0xCF;
			NMRIA_data[22] = 0x42;
			NMRIA_data[23] = 0x25;
			NMRIA_data[24] = 0x00;
		#else
			NMRIA_data[19] = 0x00;
			NMRIA_data[20] = 0x60;
			NMRIA_data[21] = 0x6D;
			NMRIA_data[22] = 0xC8;
			NMRIA_data[23] = 0x6F;
			NMRIA_data[24] = 0x00;
		#endif
	}
	#if Mdot2_22110 == 1
		NMRIA_data[1] = 0x35;	//M.2 module – 22110
	#elif E1S == 1
		NMRIA_data[1] = 0x51;	//E1.S - (SFF-TA-1006) 5.9 mm
	#elif MDOT2_SUPPORT == 1
		NMRIA_data[1] = 0x34;	//M.2 module – 2280
	#elif UDOT2_SUPPORT == 1
		NMRIA_data[1] = 0x11;	//2.5” Form Factor – U.2 (SFF-8639) 15 mm
	#endif
	NMRIA_hdr[3] = checksum_8bit(NMRIA_data, sizeof(NMRIA_data)); // NVMe Multi-Record Info Area data checksum
	NMRIA_hdr[4] = checksum_8bit(NMRIA_hdr, 4); // NVMe Multi-Record Info Area header chceksum
	PMRIA_hdr[3] = checksum_8bit(PMRIA_data, sizeof(PMRIA_data)); // PCIe Multi-Record Info Area data checksum
	PMRIA_hdr[4] = checksum_8bit(PMRIA_hdr, 4); // PCIe Multi-Record Info Area header chceksum
	u8 sum = 0; // for PIA checksum
#endif
	if (cmd_code == 0) {
		GPIO_Set();
		smb_master_init_VPD(20);
		len = VPD_SIZE;
	}
	else {
		// beacause of checksum, we need to scan all the bytes
		cmn_hdr[6] = 0xf0;
		cmn_hdr[7] = 0xff;
		len = cmd_code << 4;
	}
	int i;
	for(i = 0; i < len; i += 2)	 //VPD Write Loop
	{
		if (cmd_code == 0) {
			smb_control_register.all = readl(&smb_slv->smb_control_register);
			smb_control_register.all = 0;
			writel(smb_control_register.all, &smb_slv->smb_control_register);
			smb_init_0.all = 0;
			smb_init_0.b.smb_init_addr_w = 0xA6;
			smb_init_0.b.smb_init_addr_r = 0xA7;
			smb_init_0.b.smb_init_cmd_code = i;  // command code = offset
			writel(smb_init_0.all, &smb_slv->smb_init_0);
		}
#ifdef MI_VPD
        if (i <= 6) {
			smb_wr_1_data.b.smb_init_wr_data01 = cmn_hdr[i];
			smb_wr_1_data.b.smb_init_wr_data02 = cmn_hdr[i+1];
		}
		else if (i >= 8 && i <= 18) { // Product Info Area start
			smb_wr_1_data.b.smb_init_wr_data01 = PIA_hdr[i-8];
			smb_wr_1_data.b.smb_init_wr_data02 = PIA_hdr[i-7];
			sum += PIA_hdr[i-8];
			sum += PIA_hdr[i-7];
		}
		else if (i == 20) {

				len_pn = strlen(MN_M+pn_start);
				#if (Xfusion_case)
				smb_wr_1_data.b.smb_init_wr_data01 = 0xD8; // byte 20: Product Name type/length
				#else
				smb_wr_1_data.b.smb_init_wr_data01 = 0xC0 | len_pn; // byte 20: Product Name type/length
				#endif
				smb_wr_1_data.b.smb_init_wr_data02 = MN_M[pn_start]; // byte21: product name start
				sum += smb_wr_1_data.b.smb_init_wr_data01;
				sum += MN_M[pn_start];

		}
		else if (i >= 22 && i <= 42) {

			smb_wr_1_data.b.smb_init_wr_data01 = MN_M[i-22+pn_start+1];
			smb_wr_1_data.b.smb_init_wr_data02 = MN_M[i-21+pn_start+1];

			sum += smb_wr_1_data.b.smb_init_wr_data01;
			sum += smb_wr_1_data.b.smb_init_wr_data02;
		}
		else if (i == 44) {
			if(len_pn == 24){
				smb_wr_1_data.b.smb_init_wr_data01 = MN_M[23];
			}else{
				smb_wr_1_data.b.smb_init_wr_data01 = 0x00; // byte 44: product name fill 0x00
			}
			#if (Xfusion_case)
			smb_wr_1_data.b.smb_init_wr_data02 = 0xE8; // byte 45: model number type/length
			#else
			len_mn = strlen(MN_M);
			smb_wr_1_data.b.smb_init_wr_data02 = 0xC0 | len_mn; // byte 45: model number type/length
			#endif
			sum += smb_wr_1_data.b.smb_init_wr_data01;
			sum += smb_wr_1_data.b.smb_init_wr_data02;
		}
		else if (i >= 46 && i <= 84) { //byte 46-67: model number start
			smb_wr_1_data.b.smb_init_wr_data01 = MN_M[i-46];
			smb_wr_1_data.b.smb_init_wr_data02 = MN_M[i-45];
			sum += smb_wr_1_data.b.smb_init_wr_data01;
			sum += smb_wr_1_data.b.smb_init_wr_data02;
		}
		else if (i == 86) {
			smb_wr_1_data.b.smb_init_wr_data01 = 0xC2; // byte 86: product version type/length
			smb_wr_1_data.b.smb_init_wr_data02 = 0x31; // byte 87: product version
			sum += 0xC2;
			sum += 0x31;
		}
		else if (i == 88) {
			#if (Xfusion_case)
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00; // byte 88: product version
			smb_wr_1_data.b.smb_init_wr_data02 = 0xD4; // byte 89: SN type/length
			#else
			len_sn = strlen(SN_VPD);
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00; // byte 88: product version
			smb_wr_1_data.b.smb_init_wr_data02 = 0xC0 | len_sn; // byte 89: SN type/length
			#endif
			sum += smb_wr_1_data.b.smb_init_wr_data02;
		}
		else if (i >= 90 && i <= 108) { // byte 90-109: SN
			smb_wr_1_data.b.smb_init_wr_data01 = SN_VPD[i-90];
			smb_wr_1_data.b.smb_init_wr_data02 = SN_VPD[i-89];
			sum += SN_VPD[i-90];
			sum += SN_VPD[i-89];
		}
		else if (i == 110) {
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00; // byte 110: assert tag type/length
			smb_wr_1_data.b.smb_init_wr_data02 = 0x00; // byte 110: FRU File ID type/length
		}
		else if (i == 112) {
			smb_wr_1_data.b.smb_init_wr_data01 = 0xC1; // byte 112: end of record
			smb_wr_1_data.b.smb_init_wr_data02 = 0x00; // To pad PIA to multiple of 8 bytes
			sum += 0xC1;
		}
		else if (i >= 114 && i <= 116) {
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00;
			smb_wr_1_data.b.smb_init_wr_data02 = 0x00;
		}
		else if (i == 118) { // Product Info Area end
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00;
			smb_wr_1_data.b.smb_init_wr_data02 = (~sum)+1; // byte 119: checksum
		}
		else if (i >= 120 && i <= 124) { // NVMe Multi-Record Info Area start
			smb_wr_1_data.b.smb_init_wr_data01 = NMRIA_hdr[i-120];
			smb_wr_1_data.b.smb_init_wr_data02 = NMRIA_hdr[i-119];
		}
		else if (i >= 126 && i <= 154) {
			smb_wr_1_data.b.smb_init_wr_data01 = NMRIA_data[i-125];
			smb_wr_1_data.b.smb_init_wr_data02 = NMRIA_data[i-124];
		}
		else if (i >= 156 && i <= 182) { // NVMe Multi-Record Info Area end
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00;
			smb_wr_1_data.b.smb_init_wr_data02 = 0x00;
		}
		else if (i >= 184 && i <= 188) { // PCIe mulit-record info area start
			smb_wr_1_data.b.smb_init_wr_data01 = PMRIA_hdr[i-184];
			smb_wr_1_data.b.smb_init_wr_data02 = PMRIA_hdr[i-183];
		}
		else if (i >= 190  && i <= 198) { // PCIe mulit-record info area end
			smb_wr_1_data.b.smb_init_wr_data01 = PMRIA_data[i-190+1];
			smb_wr_1_data.b.smb_init_wr_data02 = PMRIA_data[i-189+1];
		}
		else {
			smb_wr_1_data.b.smb_init_wr_data01 = 0x00;
			smb_wr_1_data.b.smb_init_wr_data02 = 0x00;
		}
#endif
		if(cmd_code == 0) {
			writel(smb_wr_1_data.all, &smb_slv->smb_init_wr_1);
			smb_control_register.b.smb_en = 0x01;
			smb_control_register.b.smb_tran_tp_sel = 0x06;  //0x06: word write mode
			smb_control_register.b.smb_mas_tran_st = 0x01;
			smb_control_register.b.smb_slv_timer_opt = 0x01;
			writel(smb_control_register.all, &smb_slv->smb_control_register);
			udelay(8000);
			while (!(readl(&smb_slv->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK))
			{
				if (++cnt == 100)
				{
					cnt = 0;
					sts = readl(&smb_slv->smb_intr_sts);
					rtos_smb_trace(LOG_ALW, 0x0077, "smb_intr_sts %x %d", sts, i);
				}
			}
			smb_slv->smb_intr_sts.all = readl(&smb_slv->smb_intr_sts);
			if (smb_slv->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK)
			goto error;
			smb_slv->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
			writel(smb_slv->smb_intr_sts.all, &smb_slv->smb_intr_sts);
		} else if (i >= ((cmd_code-1)<<4) && i < ((cmd_code<<4)-1)) {
			// get the bytes we need, e.g. cmd_code 1 --> byte 16 ~31
			vpd[i-((cmd_code-1)<<4)] = smb_wr_1_data.b.smb_init_wr_data01;
			vpd[i-((cmd_code-1)<<4)+1] = smb_wr_1_data.b.smb_init_wr_data02;
		}
	}
		if (cmd_code > 0 && cmd_code <= 16) {
			return 0x0;
		}
		smb_control_register.all = readl(&smb_slv->smb_control_register);
		smb_control_register.all = 0;

		smb_slave_init(20);
		smb_i2c_slv_mode_9_init();

		smb_control_register.b.smb_en = 0x01;
		smb_control_register.b.smb_clk_stretch_en = 0x01;
		smb_control_register.b.smb_tran_tp_sel = 0x09;
		smb_control_register.b.smb_tran_i2c_en = 0x01;
		writel(smb_control_register.all, &smb_slv->smb_control_register);

	return 0x0;
error:
		smb_control_register.all = readl(&smb_slv->smb_control_register);
		smb_control_register.all = 0;

		smb_slave_init(20);
		smb_i2c_slv_mode_9_init();

		smb_control_register.b.smb_en = 0x01;
		smb_control_register.b.smb_clk_stretch_en = 0x01;
		smb_control_register.b.smb_tran_tp_sel = 0x09;
		smb_control_register.b.smb_tran_i2c_en = 0x01;
		writel(smb_control_register.all, &smb_slv->smb_control_register);

	return 0xFFFFFFFF;
}

static ps_code int strobe_cnt_set(int argc, char* argv[])
{
	u8 strb_cnt;
	if (argc < 1) {
		rtos_mmgr_trace(LOG_ERR, 0xd1bf, "\nusage: regulator g reg_address or regulator s reg_addr value\n");
		return 0;
	}
	else {
		strb_cnt = atoi(argv[1]);
		rtos_mmgr_trace(LOG_ERR, 0x3ac1, "argv[2] = %s, strb_cnt = %02x\n", argv[1], strb_cnt);

		smb_master_init_VPD(strb_cnt);
		rtos_mmgr_trace(LOG_ERR, 0xac64, "strobe count has been set to %s", strb_cnt);
	}
	return 0;
}

static DEFINE_UART_CMD (strobe_cnt_set, "strb",
	"strobe_cnt_set",
	"strobe_cnt_set",
	1, 2, strobe_cnt_set);

	//400Khz
	// /* set start a, b, c, d timer */
	// smb_timer_0.all = 0;
	// smb_timer_0.b.smb_start_a_timer = 0x1a;
	// smb_timer_0.b.smb_start_b_timer = 0x05;
	// smb_timer_0.b.smb_start_c_timer = 0x0d;
	// smb_timer_0.b.smb_start_d_timer = 0x05;
	// writel(smb_timer_0.all, &smb_slv->smb_timer_0);

	// /* set stop a, b, c, d timer */
	// smb_timer_1.all = 0;
	// smb_timer_1.b.smb_stop_a_timer = 0x1a;
	// smb_timer_1.b.smb_stop_b_timer = 0x05;
	// smb_timer_1.b.smb_stop_c_timer = 0x0d;
	// smb_timer_1.b.smb_stop_d_timer = 0x1a;
	// writel(smb_timer_1.all, &smb_slv->smb_timer_1);

	// /* set wr a, b, c, d timer */
	// smb_timer_2.all = 0;
	// smb_timer_2.b.smb_wr_a_timer = 0x0d;
	// smb_timer_2.b.smb_wr_b_timer = 0x0d;
	// smb_timer_2.b.smb_wr_c_timer = 0x05;
	// smb_timer_2.b.smb_wr_d_timer = 0x0d;
	// writel(smb_timer_2.all, &smb_slv->smb_timer_2);

	// /* set wr e, and rd a, b, c timer */
	// smb_timer_3.all = 0;
	// smb_timer_3.b.smb_wr_e_timer = 0x05;
	// smb_timer_3.b.smb_rd_a_timer = 0x0d;
	// smb_timer_3.b.smb_rd_b_timer = 0x0d;
	// smb_timer_3.b.smb_rd_c_timer = 0x05;
	// writel(smb_timer_3.all, &smb_slv->smb_timer_3);

	// /* set rd d, e, and ack a, b timer */
	// smb_timer_4.all = 0;
	// smb_timer_4.b.smb_rd_d_timer = 0x0d;
	// smb_timer_4.b.smb_rd_e_timer = 0x05;
	// smb_timer_4.b.smb_ack_a_timer = 0x0d;
	// smb_timer_4.b.smb_ack_b_timer = 0x0d;
	// writel(smb_timer_4.all, &smb_slv->smb_timer_4);

	// /* set ack c, d, e, and start r timer */
	// smb_timer_5.all = 0;
	// smb_timer_5.b.smb_ack_c_timer = 0x05;
	// smb_timer_5.b.smb_ack_d_timer = 0x0d;
	// smb_timer_5.b.smb_ack_e_timer = 0x05;
	// smb_timer_5.b.smb_start_r_timer = 0x0b;
	// writel(smb_timer_5.all, &smb_slv->smb_timer_5);
// }

/*
slow_code u32 VPD_blk_write(u8 cmd_code)//joe slow->ddr 20201124
{
	smb_init_0_t smb_init_0;
	smb_init_wr_1_t smb_wr_1_data;
	smb_init_wr_2_t smb_wr_2_data;
	smb_init_wr_3_t smb_wr_3_data;
	smb_init_wr_4_t smb_wr_4_data;
	smb_init_wr_5_t smb_wr_5_data;
	smb_init_wr_6_t smb_wr_6_data;
	smb_init_wr_7_t smb_wr_7_data;
	smb_init_wr_8_t smb_wr_8_data;
	smb_init_wr_9_t smb_wr_9_data;
	smb_init_wr_10_t smb_wr_10_data;
	smb_init_wr_11_t smb_wr_11_data;
	smb_init_wr_12_t smb_wr_12_data;
	smb_init_wr_13_t smb_wr_13_data;
	smb_init_wr_14_t smb_wr_14_data;
	smb_init_wr_15_t smb_wr_15_data;
	smb_init_wr_16_t smb_wr_16_data;

	smb_control_register_t smb_control_register;
	u32 cnt = 0x7FFFFF;
	u32 sts;

	GPIO_Set();
	smb_master_init_VPD(20);


	smb_init_0.all = 0;
    smb_init_0.b.smb_init_addr_w = 0xA6;
    smb_init_0.b.smb_init_addr_r = 0xA7;
	smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = offset
	smb_init_0.b.smb_init_wr_blk_cnt = 0x10;
	writel(smb_init_0.all, &smb_slv->smb_init_0);

						//common header
    smb_wr_1_data.b.smb_init_wr_data01 = 0x01;		//IPMI format version number: factory default
    smb_wr_1_data.b.smb_init_wr_data02 = 0x00;		//Internal Use Area Starting Offset: not present
	smb_wr_1_data.b.smb_init_wr_data03 = 0x00;      //Chassis Info Area Starting Offset:  Not Present
    smb_wr_1_data.b.smb_init_wr_data04 = 0x00;		//Board Info Area Starting Offset: Not Present

	writel(smb_wr_1_data.all, &smb_slv->smb_init_wr_1);

	smb_wr_2_data.b.smb_init_wr_data05 = 0x08;			//Product Info Area Starting Offset: Byte 8
    smb_wr_2_data.b.smb_init_wr_data06 = 0x1D;			//Multirecord Info Area Starting Offset: 29(1D)
	smb_wr_2_data.b.smb_init_wr_data07 = 0x00;			//Reserved: Factory Default
	smb_wr_2_data.b.smb_init_wr_data08 = 0x00;			//Common Header Checksum: Not Set

	writel(smb_wr_2_data.all, &smb_slv->smb_init_wr_2);

	smb_wr_3_data.b.smb_init_wr_data09 = 0x01;			//IPMI format version number: factory default
    smb_wr_3_data.b.smb_init_wr_data10 = 0x15;			//Product Info Area Length: 21(0x15)
	smb_wr_3_data.b.smb_init_wr_data11 = 0x19;			//Language Code: 0x19 (English)
	smb_wr_3_data.b.smb_init_wr_data12 = 0x05;			//Manufacturer Name Type/ Length: 5

	writel(smb_wr_3_data.all, &smb_slv->smb_init_wr_3);	//(8-bit ASCII)

	smb_wr_4_data.b.smb_init_wr_data13 = 0x53;			//Manufacturer Name:S
    smb_wr_4_data.b.smb_init_wr_data14 = 0x53;			//Manufacturer Name:S
 	smb_wr_4_data.b.smb_init_wr_data15 = 0x53;			//Manufacturer Name:S
	smb_wr_4_data.b.smb_init_wr_data16 = 0x54;			//Manufacturer Name:T

	writel(smb_wr_4_data.all, &smb_slv->smb_init_wr_4);

	smb_wr_5_data.b.smb_init_wr_data17 = 0x43;			//Manufacturer Name:C
	smb_wr_5_data.b.smb_init_wr_data18 = 0x03;			//Product Name Length: 3
	smb_wr_5_data.b.smb_init_wr_data19 = 0x4C;			//Product Name:L
	smb_wr_5_data.b.smb_init_wr_data20 = 0x4A;			//Product Name:J

	writel(smb_wr_5_data.all, &smb_slv->smb_init_wr_5);

	smb_wr_6_data.b.smb_init_wr_data21 = 0x31;			//Product Name:1
	smb_wr_6_data.b.smb_init_wr_data22 = 0x2D;			//Product Name:-
	smb_wr_6_data.b.smb_init_wr_data23 = 0x32;			//Product Name:2
	smb_wr_6_data.b.smb_init_wr_data24 = 0x57;			//Product Name:W

	writel(smb_wr_6_data.all, &smb_slv->smb_init_wr_6);

	smb_wr_7_data.b.smb_init_wr_data25 = 0x37;			//Product Name:7
	smb_wr_7_data.b.smb_init_wr_data26 = 0x36;			//Product Name:6
	smb_wr_7_data.b.smb_init_wr_data27 = 0x38;			//Product Name:8
	smb_wr_7_data.b.smb_init_wr_data28 = 0x30;			//Product Name:0

	writel(smb_wr_7_data.all, &smb_slv->smb_init_wr_7);

	smb_wr_8_data.b.smb_init_wr_data29 = 0xC1;			//EOR
	smb_wr_8_data.b.smb_init_wr_data30 = 0x00;
	smb_wr_8_data.b.smb_init_wr_data31 = 0x00;
	smb_wr_8_data.b.smb_init_wr_data32 = 0x00;

	writel(smb_wr_8_data.all, &smb_slv->smb_init_wr_8);

	smb_wr_9_data.b.smb_init_wr_data33 = 0x00;
	smb_wr_9_data.b.smb_init_wr_data34 = 0x00;
	smb_wr_9_data.b.smb_init_wr_data35 = 0x00;
	smb_wr_9_data.b.smb_init_wr_data36 = 0x00;

	writel(smb_wr_9_data.all, &smb_slv->smb_init_wr_9);

	smb_wr_10_data.b.smb_init_wr_data37 = 0x00;
	smb_wr_10_data.b.smb_init_wr_data38 = 0x00;
	smb_wr_10_data.b.smb_init_wr_data39 = 0x00;
	smb_wr_10_data.b.smb_init_wr_data40 = 0x00;

	writel(smb_wr_10_data.all, &smb_slv->smb_init_wr_10);

	smb_wr_11_data.b.smb_init_wr_data41 = 0x00;
	smb_wr_11_data.b.smb_init_wr_data42 = 0x00;
	smb_wr_11_data.b.smb_init_wr_data43 = 0x00;
	smb_wr_11_data.b.smb_init_wr_data44 = 0x00;

	writel(smb_wr_11_data.all, &smb_slv->smb_init_wr_11);

	smb_wr_12_data.b.smb_init_wr_data45 = 0x00;
	smb_wr_12_data.b.smb_init_wr_data46 = 0x00;
	smb_wr_12_data.b.smb_init_wr_data47 = 0x00;
	smb_wr_12_data.b.smb_init_wr_data48 = 0x00;

	writel(smb_wr_12_data.all, &smb_slv->smb_init_wr_12);

	smb_wr_13_data.b.smb_init_wr_data49 = 0x00;
	smb_wr_13_data.b.smb_init_wr_data50 = 0x00;
	smb_wr_13_data.b.smb_init_wr_data51 = 0x00;
	smb_wr_13_data.b.smb_init_wr_data52 = 0x00;

	writel(smb_wr_13_data.all, &smb_slv->smb_init_wr_13);

	smb_wr_14_data.b.smb_init_wr_data53 = 0x00;
	smb_wr_14_data.b.smb_init_wr_data54 = 0x00;
	smb_wr_14_data.b.smb_init_wr_data55 = 0x00;
	smb_wr_14_data.b.smb_init_wr_data56 = 0x00;

	writel(smb_wr_14_data.all, &smb_slv->smb_init_wr_14);

	smb_wr_15_data.b.smb_init_wr_data57 = 0x00;
	smb_wr_15_data.b.smb_init_wr_data58 = 0x00;
	smb_wr_15_data.b.smb_init_wr_data59 = 0x00;
	smb_wr_15_data.b.smb_init_wr_data60 = 0x00;

	writel(smb_wr_15_data.all, &smb_slv->smb_init_wr_15);

	smb_wr_16_data.b.smb_init_wr_data61 = 0x00;
	smb_wr_16_data.b.smb_init_wr_data62 = 0x00;
	smb_wr_16_data.b.smb_init_wr_data63 = 0x00;
	smb_wr_16_data.b.smb_init_wr_data64 = 0x00;

	writel(smb_wr_16_data.all, &smb_slv->smb_init_wr_16);
	


	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x03;//master mode, read, 0x6: master, write
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb_slv->smb_control_register);

	while (!(readl(&smb_slv->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
		if (++cnt == 0x7FFFFFF) {
			cnt = 0;
			sts = readl(&smb_slv->smb_intr_sts);
			rtos_smb_trace(LOG_ALW, 0, "smb_intr_sts %x", sts);
		}
	}
	smb_slv->smb_intr_sts.all = readl(&smb_slv->smb_intr_sts);
	// when ACK_DONE_ERR, return hardcode value
	if (smb_slv->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK)
		goto error;
	smb_slv->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
	writel(smb_slv->smb_intr_sts.all, &smb_slv->smb_intr_sts);

	smb_slave_init(20);
	smb_setup_nvme_mi_basic_data();

	return 0x0;
error:

	smb_slave_init(20);
	smb_setup_nvme_mi_basic_data();

	return 0xFFFFFFFF;
}
*/
/*
fast_code u32 VPD_read(u8 cmd_code, u8 *value)
{
	u32 data0;
	smb_init_0_t smb_init_0;
	smb_control_register_t smb_control_register;
	u32 cnt = 0x7FFFFF;
	u32 sts;

	GPIO_Set();
	smb_master_init_VPD(20);

	smb_init_0.all = 0;

	smb_init_0.b.smb_init_addr_w = 0xA6; //target address,0xD2: 1101 0010, 0x90: 1001 000x
	smb_init_0.b.smb_init_addr_r = 0xA7; //target address 0xD3: 1101 0011, 0x91: 1001 0001
	smb_init_0.b.smb_init_cmd_code = cmd_code; //command code = 0x10 for id register; 0x01: ctrl 1
	writel(smb_init_0.all, &smb_slv->smb_init_0);


	smb_control_register.all = 0;
	smb_control_register.b.smb_en = 0x01;
	smb_control_register.b.smb_tran_tp_sel = 0x0B;//master mode, read, 0x6: master, write
	smb_control_register.b.smb_mas_tran_st = 0x01;
	smb_control_register.b.smb_slv_timer_opt = 0x01;
	writel(smb_control_register.all, &smb_slv->smb_control_register);

	// temp printing before sensor issue clarified
	while (!(readl(&smb_slv->smb_intr_sts) & MAS_TRAN_DONE_NML_STS_MASK)) {
		if (++cnt == 0x7FFFFFF) {
			cnt = 0;
			sts = readl(&smb_slv->smb_intr_sts);
			rtos_smb_trace(LOG_ALW, 0, "smb_intr_sts %x", sts);
		}
	}
	smb_slv->smb_intr_sts.all = readl(&smb_slv->smb_intr_sts);
	if (smb_slv->smb_intr_sts.all & WR_ACK_DONE_ERR_STS_MASK)
		goto error;
	smb_slv->smb_intr_sts.b.mas_tran_done_nml_sts = 0;
	writel(smb_slv->smb_intr_sts.all, &smb_slv->smb_intr_sts);
	data0 = readl(&smb_slv->smb_pasv_0);
	*value = data0 >> 24;

	smb_slave_init(20);
	smb_setup_nvme_mi_basic_data();

	return 0;
error:

	smb_slave_init(20);
	smb_setup_nvme_mi_basic_data();

	return 0xFFFFFFFF;
}
*/

// static fast_code int VPD_test(int argc, char* argv[])
// {
	// u32 ret;
	// u8 cmd_code;
	// if (argc < 3) {
		// rtos_mmgr_trace(LOG_ERR, 0, "\nusage: regulator g reg_address or regulator s reg_addr value\n");
		// return 0;
	// }
	// if (argv[1][0] == 'g' || argv[1][0] == 'G') { // get register value
		// u8 value;

		// cmd_code = atoi(argv[2]);
		// rtos_mmgr_trace(LOG_ERR, 0, "\nargc = %d, argv[2] = %s, cmd_code = %02x\n", argc, argv[2], cmd_code);

		// ret = VPD_read(cmd_code, &value);
		// rtos_mmgr_trace(LOG_ERR, 0, "\ncontrol the power regulator: reg = %d, value = %08x, ret = %08x\n", cmd_code, value, ret);
	// }
	// else if (argv[1][0] == 's' || argv[1][0] == 'S') {
		// cmd_code = atoi(argv[2]);

		// ret = VPD_blk_write(cmd_code);
	// }
	// else {
		// rtos_mmgr_trace(LOG_ERR, 0, "\nusage: regulator g reg_address or regulator s reg_addr value\n");
	// }
	// return 0;
// }

// static DEFINE_UART_CMD (VPD_Test, "vpd_test",
	// "Test VPD",
	// "Test VPD Blk Write",
	// 1, 3, VPD_test);

/*!
 * @brief SMBus slave mode initialization
 *
 * @param none
 *
 * @return none
 */
ddr_code void smb_slave_init(u8 strb_cnt)
{
	rtos_smb_trace(LOG_ALW, 0xd192, "SHANE ENTER smb_slave_init ");
	u32 data;
	smb_strobe_cnt_t smb_strobe_cnt;
	smb_timer_0_t smb_timer_0;
	smb_timer_1_t smb_timer_1;
	smb_timer_2_t smb_timer_2;
	smb_timer_3_t smb_timer_3;
	smb_timer_4_t smb_timer_4;
	smb_timer_5_t smb_timer_5;

	/* reset */
	writel(0x0000ffff, &smb_slv->smb_intr_sts);
	data = readl(&smb_slv->smb_intr_sts);
	rtos_smb_trace(LOG_DEBUG, 0xfb6a, "smb_intr_sts: %x", data);
#if defined(BASIC_MI)
			/* Set Basic MI Endpoint Address to 0x6A */
			smb_control_register_2_t smb_ctrl_reg2;
			smb_ctrl_reg2.all = readl(&smb_slv->smb_control_register_2);
			smb_ctrl_reg2.b.smb_new_dev_addr = 0x6A;
			writel(smb_ctrl_reg2.all, &smb_slv->smb_control_register_2);
			smb_ctrl_reg2.b.smb_set_dev_addr = 0x1;
			writel(smb_ctrl_reg2.all, &smb_slv->smb_control_register_2);
#endif

#if defined(FPGA)
	smb_strobe_cnt.all = 0;
	smb_strobe_cnt.b.smb_strobe_cnt = 1;
	writel(smb_strobe_cnt.all, &smb_slv->smb_strobe_cnt);

	/* set start a, b, c, d timer */
	smb_timer_0.all = 0;
	smb_timer_0.b.smb_start_a_timer = 0x2b;
	smb_timer_0.b.smb_start_b_timer = 0x02;
	smb_timer_0.b.smb_start_c_timer = 0x25;
	smb_timer_0.b.smb_start_d_timer = 0x02;
	writel(smb_timer_0.all, &smb_slv->smb_timer_0);

	/* set stop a, b, c, d timer */
	smb_timer_1.all = 0;
	smb_timer_1.b.smb_stop_a_timer = 0x2b;
	smb_timer_1.b.smb_stop_b_timer = 0x07;
	smb_timer_1.b.smb_stop_c_timer = 0x25;
	smb_timer_1.b.smb_stop_d_timer = 0x32;
	writel(smb_timer_1.all, &smb_slv->smb_timer_1);

	/* set wr a, b, c, d timer */
	smb_timer_2.all = 0;
	smb_timer_2.b.smb_wr_a_timer = 0x14;
	smb_timer_2.b.smb_wr_b_timer = 0x14;
	smb_timer_2.b.smb_wr_c_timer = 0x07;
	smb_timer_2.b.smb_wr_d_timer = 0x20;
	writel(smb_timer_2.all, &smb_slv->smb_timer_2);

	/* set wr e, and rd a, b, c timer */
	smb_timer_3.all = 0;
	smb_timer_3.b.smb_wr_e_timer = 0x04;
	smb_timer_3.b.smb_rd_a_timer = 0x14;
	smb_timer_3.b.smb_rd_b_timer = 0x14;
	smb_timer_3.b.smb_rd_c_timer = 0x07;
	writel(smb_timer_3.all, &smb_slv->smb_timer_3);

	/* set rd d, e, and ack a, b timer */
	smb_timer_4.all = 0;
	smb_timer_4.b.smb_rd_d_timer = 0x20;
	smb_timer_4.b.smb_rd_e_timer = 0x04;
	smb_timer_4.b.smb_ack_a_timer = 0x14;
	smb_timer_4.b.smb_ack_b_timer = 0x14;
	writel(smb_timer_4.all, &smb_slv->smb_timer_4);

	/* set ack c, d, e, and start r timer */
	smb_timer_5.all = 0;
	smb_timer_5.b.smb_ack_c_timer = 0x07;
	smb_timer_5.b.smb_ack_d_timer = 0x20;
	smb_timer_5.b.smb_ack_e_timer = 0x04;
	smb_timer_5.b.smb_start_r_timer = 0x28;
	writel(smb_timer_5.all, &smb_slv->smb_timer_5);

#else
	/* ASIC specific changes */
	smb_strobe_cnt.all = 0;
	smb_strobe_cnt.b.smb_strobe_cnt = strb_cnt;
	writel(smb_strobe_cnt.all, &smb_slv->smb_strobe_cnt);

	/* Values for SMB Bit Timer (100 KHz) */
	/* set start a, b, c, d timer */
	smb_timer_0.all = 0;
	smb_timer_0.b.smb_start_a_timer = 0x62;
	smb_timer_0.b.smb_start_b_timer = 0x05;
	smb_timer_0.b.smb_start_c_timer = 0x55;
	smb_timer_0.b.smb_start_d_timer = 0x05;
	writel(smb_timer_0.all, &smb_slv->smb_timer_0);

	/* set stop a, b, c, d timer */
	smb_timer_1.all = 0;
	smb_timer_1.b.smb_stop_a_timer = 0x62;
	smb_timer_1.b.smb_stop_b_timer = 0x10;
	smb_timer_1.b.smb_stop_c_timer = 0x55;
	smb_timer_1.b.smb_stop_d_timer = 0x30;
	writel(smb_timer_1.all, &smb_slv->smb_timer_1);

	/* set wr a, b, c, d timer */
	smb_timer_2.all = 0;
	smb_timer_2.b.smb_wr_a_timer = 0x2e;
	smb_timer_2.b.smb_wr_b_timer = 0x2e;
	smb_timer_2.b.smb_wr_c_timer = 0x11;
	smb_timer_2.b.smb_wr_d_timer = 0x4f;
	writel(smb_timer_2.all, &smb_slv->smb_timer_2);

	/* set wr e, and rd a, b, c timer */
	smb_timer_3.all = 0;
	smb_timer_3.b.smb_wr_e_timer = 0x05;
	smb_timer_3.b.smb_rd_a_timer = 0x2e;
	smb_timer_3.b.smb_rd_b_timer = 0x2e;
	smb_timer_3.b.smb_rd_c_timer = 0x11;
	writel(smb_timer_3.all, &smb_slv->smb_timer_3);

	/* set rd d, e, and ack a, b timer */
	smb_timer_4.all = 0;
	smb_timer_4.b.smb_rd_d_timer = 0x4f;
	smb_timer_4.b.smb_rd_e_timer = 0x05;
	smb_timer_4.b.smb_ack_a_timer = 0x2e;
	smb_timer_4.b.smb_ack_b_timer = 0x2e;
	writel(smb_timer_4.all, &smb_slv->smb_timer_4);

	/* set ack c, d, e, and start r timer */
	smb_timer_5.all = 0;
	smb_timer_5.b.smb_ack_c_timer = 0x11;
	smb_timer_5.b.smb_ack_d_timer = 0x8f;				 //IG WA: 0x8F
	smb_timer_5.b.smb_ack_e_timer = 0x05;
	smb_timer_5.b.smb_start_r_timer = 0x36;
	writel(smb_timer_5.all, &smb_slv->smb_timer_5);
#endif
}
