//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
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
/*! \file dfi_init.c
 * @brief dfi module
 *
 * \addtogroup utils
 * \defgroup dfi
 * \ingroup utils
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "sect.h"
#include "dfi_init.h"
#include "dfi_reg.h"
#include "dfi_common.h"
#include "dfi_config.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "misc_register.h"
#include "bf_mgr.h"
#include "stdio.h"
#include "string.h"

#define __FILEID__ dfiinit
#include "trace.h"

extern volatile u64 ddr_capacity;

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------
volatile tDRAM_Training_Result   DRAM_Train_Result;

// DDR-side DS setting 
u8 DRAM_Drive_Strength_Value[4];
// DDR-side ODT setting 
u8 DRAM_ODT_VALUE_WRITE[4];
u8 DRAM_ODT_VALUE[4]; //DRAM ODT RTT_NOM in MR1(DDR34), 2 -> 120ohm, 3 -> 40ohm / DQ ODT in MR11(LPDDR4
// SOC-side DS setting 
u8 SOC_DATA_PDRV_VALUE;
u8 SOC_DATA_NDRV_VALUE;
u8 SOC_ADCM_PDRV_VALUE;
u8 SOC_ADCM_NDRV_VALUE;
// SOC-side ODT setting 
u8 SOC_DATA_PODT_VALUE;
u8 SOC_DATA_NODT_VALUE;


//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
/*!
 * @brief DFY (DDR PHY Interface) phy init function
 *
 * @return	not used
 */
dfi_code void dfi_dram_setting_verify(void)
{
    //select CS to read 
    utils_dfi_trace(LOG_ERR, 0xf5b0, "\n========[DRAM VERIFY]========\n");
    for (int cs = 0; cs < DDR_CS_NUM; cs++) 
    {
        cs_config_sel_t config = { .all = 0 };
    	config.b.cs_config_cs_sel = cs;
    	mc0_writel(config.all, CS_CONFIG_SEL);
        
        cs_config_odt_t read_config = { .all = mc0_readl(CS_CONFIG_ODT) };  
        utils_dfi_trace(LOG_ERR, 0x0e44, "CS[%d]:\n",cs);
        utils_dfi_trace(LOG_ERR, 0xbd4a, "CS_CONFIG_ODT 0xc0060034: 0x%x\n",read_config.all); 
        utils_dfi_trace(LOG_ERR, 0x6b6c, "ODT_VALUE:0x%x, ODT_VALUE_WRITE:0x%x, ODT_VALUE_PARK:0x%x, OUTPUT_DRIVER_IMPEDENCE(DS):0x%x\n",read_config.b.odt_value,read_config.b.odt_value_write,read_config.b.odt_value_park,read_config.b.output_driver_impedence);
    }
    //Data Bus IO settings
	io_data_0_t data0 = { .all = dfi_readl(IO_DATA_0) };
	utils_dfi_trace(LOG_ERR, 0x404a, "SOC PODT: 0x%x, NODT: 0x%x, PDS: 0x%x, NDS: 0x%x\n",data0.b.data_podt_sel,data0.b.data_nodt_sel,data0.b.data_pdrv_sel,data0.b.data_ndrv_sel);
}
dfi_code void dfi_dram_size_related_init(u32 dram_capacity)
{
    // SOC-side DS setting 
    if (dram_capacity == DDR_4GB)
    { 
        // SOC-side ODT-DS setting 
        SOC_DATA_PODT_VALUE =  0x7;
        SOC_DATA_NODT_VALUE =  0x0;
        SOC_DATA_PDRV_VALUE =  0xD;
        SOC_DATA_NDRV_VALUE =  0xC; 
        SOC_ADCM_PDRV_VALUE =  ADCM_PDRV_SEL;
        SOC_ADCM_NDRV_VALUE =  ADCM_NDRV_SEL;        
        // DDR-side ODT setting 
        for(int i =0; i<4;i++)
        {    
            DRAM_ODT_VALUE[i] = 3; //DRAM ODT RTT_NOM in MR1(DDR34), 2 -> 120ohm, 3 -> 40ohm / DQ ODT in MR11(LPDDR4
            DRAM_ODT_VALUE_WRITE[i] = 4;            
        }
        // DDR-side DS setting 
        for(int i =0; i<4;i++)
        {
            DRAM_Drive_Strength_Value[i] = 1;
        }        
    }
    else
    {
        // SOC-side ODT-DS setting  
        SOC_DATA_PODT_VALUE =  0xA;
        SOC_DATA_NODT_VALUE =  0x0;
        SOC_DATA_PDRV_VALUE =  0xB;
        SOC_DATA_NDRV_VALUE =  0xC;
        SOC_ADCM_PDRV_VALUE =  ADCM_PDRV_SEL;
        SOC_ADCM_NDRV_VALUE =  ADCM_NDRV_SEL;
        
        // DDR-side ODT setting 
        for(int i =0; i<4;i++)
        {    
            DRAM_ODT_VALUE[i] = 1; //DRAM ODT RTT_NOM in MR1(DDR34), 2 -> 120ohm, 3 -> 40ohm / DQ ODT in MR11(LPDDR4
			DRAM_ODT_VALUE_WRITE[i] = 4;
        }
        // DDR-side DS setting 
        for(int i =0; i<4;i++)
        {
            DRAM_Drive_Strength_Value[i] = 0;
        }
    }
    

}
dfi_code void dfi_train_result_init(void)
{
    //utils_dfi_trace(LOG_ERR, 0, "Size of tDRAM_Training_Result[%d]\n",sizeof(tDRAM_Training_Result));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tPAD[%d]\n",sizeof(tPAD));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tWRLVL[%d]\n",sizeof(tWRLVL));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tRDLAT[%d]\n",sizeof(tRDLAT));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tRDEYE[%d]\n",sizeof(tRDEYE));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tWRVREF[%d]\n",sizeof(tWRVREF));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tWRDESKEW[%d]\n",sizeof(tWRDESKEW));
    //utils_dfi_trace(LOG_ERR, 0, "Size of tRDLVLDPE[%d]\n",sizeof(tRDLVLDPE));
    //utils_dfi_trace(LOG_ERR, 0, "Init DRAM training ......\n");
    memset((void*)&DRAM_Train_Result, 0, sizeof(tDRAM_Training_Result)); 
    utils_dfi_trace(LOG_ERR, 0x3166, "Init DRAM training area done......\n");
}
dfi_code void dfi_phy_init(void)
{
	//DFI configurations
	dfi_ctrl_0_t ctrl = { .all = 0 };
	ctrl.b.pipe_i_sel = 1;			//Use this setting for all speed and config
	ctrl.b.pipe_o_sel = 1;			//Use this setting for all speed and config
	ctrl.b.ecc_byte_remap = ECC_BYTE_REMAP;	//ECC_BYTE_REMAP;
	ctrl.b.dram_type = DFI_DRAM_TYPE; 	//In DFI, DDR3=2, DDR4=3, LPDDR4 = 11
	ctrl.b.pinmux_sel = (DFI_DRAM_TYPE == DFI_DDR4) ? 0 :
				(DFI_DRAM_TYPE == DFI_DDR3) ? 1 :
				(DFI_DRAM_TYPE == DFI_LPDDR4) ? 4 : 0;

	dfi_writel(ctrl.all, DFI_CTRL_0);

	//PHY Sync
	sync_ctrl_0_t sync = { .all = 0 };
	sync.b.cgs_sel = CGS_SEL;		//PHY CG Start Delay Select, range from 0 to 3
	sync.b.wr_rstb_sel = WR_RSTB_SEL;	//PHY Write Reset Delay Select, range from 0 to 3
	sync.b.wr_pt_init = WR_PT_INIT;		//PHY WR_LD Initial Value, could be 1, 2, 4 and 8.
	dfi_writel(sync.all, SYNC_CTRL_0);

	dfi_sync();

	//Data Bus IO settings
	io_data_0_t data0 = { .all = 0 };
	data0.b.data_pstr = DATA_PSTR;		//default strength
	data0.b.data_nstr = DATA_NSTR;		//default strength
	//data0.b.data_podt_sel = DATA_PODT_SEL;
	//data0.b.data_nodt_sel = DATA_NODT_SEL;
	//data0.b.data_pdrv_sel = DATA_PDRV_SEL;
	//data0.b.data_ndrv_sel = DATA_NDRV_SEL;
	#if 1
	data0.b.data_podt_sel = SOC_DATA_PODT_VALUE;
	data0.b.data_nodt_sel = SOC_DATA_NODT_VALUE;
	data0.b.data_pdrv_sel = SOC_DATA_PDRV_VALUE;
	data0.b.data_ndrv_sel = SOC_DATA_NDRV_VALUE;
    #else
    if ((ddr_capacity>>20) == DDR_4GB)// 4GB
    {
        data0.b.data_podt_sel = 0x7;
    	data0.b.data_nodt_sel = 0x2;
    	data0.b.data_pdrv_sel = 0xD;
    	data0.b.data_ndrv_sel = 0x8;    
    }
    else if ((ddr_capacity>>20) == DDR_8GB)// 8GB
    {
        data0.b.data_podt_sel = 0x8;
    	data0.b.data_nodt_sel = 0x0;
    	data0.b.data_pdrv_sel = 0xE;
    	data0.b.data_ndrv_sel = 0x8; 
    }
    else
    {
        data0.b.data_podt_sel = DATA_PODT_SEL;
	    data0.b.data_nodt_sel = DATA_NODT_SEL;
	    data0.b.data_pdrv_sel = DATA_PDRV_SEL;
	    data0.b.data_ndrv_sel = DATA_NDRV_SEL;
    }
    #endif
	dfi_writel(data0.all, IO_DATA_0);
    utils_dfi_trace(LOG_ERR, 0xf340, "SOC PODT: 0x%x, NODT: 0x%x, PDS: 0x%x, NDS: 0x%x\n",data0.b.data_podt_sel,data0.b.data_nodt_sel,data0.b.data_pdrv_sel,data0.b.data_ndrv_sel);

	io_data_1_t data1 = { .all = 0 };
	data1.b.dq_edge_sel = 1;		//Center align Write DQ-DQS
	data1.b.data_diff_en = DATA_DIFF_EN;	//Enable differential receiver
	data1.b.dqs_rcv_sel = DQS_RCV_SEL;	//Choose differential receiver
	data1.b.dq_rcv_sel = DQ_RCV_SEL;	//Choose differential receiver
	data1.b.dqs_rcv_mode = DQS_RCV_MODE;
	data1.b.dq_rcv_mode = DQ_RCV_MODE;
	data1.b.dqs_pd = DQS_PD;		//turn off QS PD. If no termination, set to 1
	data1.b.dqs_pu = DQS_PU;		//turn off QS PU.
	data1.b.dqsn_pd = DQSN_PD;		//turn off QSN PD.
	data1.b.dqsn_pu = DQSN_PU;		//turn off QSN PU. If no termination, set to 0.
	data1.b.dq_pd = DQ_PD;			//turn off DQ PD.
	data1.b.dq_pu = DQ_PU;			//turn off DQ PU. If no termination, set to 0.
	data1.b.data_mode = DATA_MODE;
	data1.b.data_aux_en = DATA_AUX_EN;
	dfi_writel(data1.all, IO_DATA_1);

	//ADCM Bus IO settings
	io_adcm_0_t adcm0 = { .all = 0 };
	adcm0.b.adcm_pstr = ADCM_PSTR;		//default strength
	adcm0.b.adcm_nstr = ADCM_NSTR;		//default strength
	adcm0.b.adcm_podt_sel = 0;
	adcm0.b.adcm_nodt_sel = 0;
	//adcm0.b.adcm_pdrv_sel = ADCM_PDRV_SEL;
	//adcm0.b.adcm_ndrv_sel = ADCM_NDRV_SEL;
	adcm0.b.adcm_pdrv_sel = SOC_ADCM_PDRV_VALUE;
	adcm0.b.adcm_ndrv_sel = SOC_ADCM_NDRV_VALUE;
	dfi_writel(adcm0.all, IO_ADCM_0);

	io_adcm_1_t adcm1 = { .all = 0 };
	adcm1.b.addr_pu = ADCM_PU;		//turn off ADCM PU. If no termination, set to 0.
	adcm1.b.addr_pd = ADCM_PD;		//turn off ADCM PD. If no termination, set to 1
	dfi_writel(adcm1.all, IO_ADCM_1);

	io_adcm_3_t adcm3 = { .all = 0 };
	adcm3.b.ck_pdrv_sel = CK_PDRV_SEL;
	adcm3.b.ck_ndrv_sel = CK_NDRV_SEL;
	dfi_writel(adcm3.all, IO_ADCM_3);

	//Turn on Pull Up for DDR4 ALERTN
	//PU is active low (future design will be active high)
	io_alertn_t alert = { .all = 0 };
	alert.b.alert_pu = 0;
#if defined(EVB_0501)
	alert.b.alert_nodt_sel = 0xF;
#endif
	dfi_writel(alert.all, IO_ALERTN);

	//VREF gen settings
	u8 vgen_vsel_ddr4 = 0;
	u8 vgen_range_ddr4 = 0;
	switch (DATA_PODT_SEL) {
	case 1:
	case 2: //odt 240ohm
		vgen_range_ddr4 = 0x2; //44.3%~83.1% of 1.2v
		vgen_vsel_ddr4 = 0x13; //19-56.08%
		break;
	case 3:
	case 4: //odt 120ohm
		vgen_range_ddr4 = 0x2; //44.3%~83.1% of 1.2v
		vgen_vsel_ddr4 = 0x1b; //27-61.04%
		break;
	case 5:
	case 6: //odt 80ohm
		vgen_range_ddr4 = 0x2; //44.3%~83.1% of 1.2v
		vgen_vsel_ddr4 = 0x22; //34-65.38%
		break;
	case 7:
	case 8: //odt 60ohm
		vgen_range_ddr4 = 0x2; //44.3%~83.1% of 1.2v
		vgen_vsel_ddr4 = 0x27; //39-68.48%
		break;
	case 9:
	case 10: //odt 48ohm
		vgen_range_ddr4 = 0x3; //58.1%~95% of 1.2v
		vgen_vsel_ddr4 = 0x16; //22-71.08%
		break;
	case 11:
	case 12: //odt 40ohm
		vgen_range_ddr4 = 0x3; //58.1%~95% of 1.2v
		vgen_vsel_ddr4 = 0x19; //25-72.85%
		break;
	case 13:
	case 14: //odt 34ohm
		vgen_range_ddr4 = 0x3; //58.1%~95% of 1.2v
		vgen_vsel_ddr4 = 0x1d; //29-75.21%
		break;
	case 15: //odt 30ohm
		vgen_range_ddr4 = 0x3; //58.1%~95% of 1.2v
		vgen_vsel_ddr4 = 0x20; //32-76.84%
		break;
	default: //no odt
		vgen_range_ddr4 = 0x2; //44.3%~83.1% of 1.2v
		vgen_vsel_ddr4 = 0x09; //9-49.88%
	}

	//LP4 VOH is default at 367mV,set VREF to 183mV (16%)
	u8 vgen_vsel_lp4 = 0x20; //15.92% VREF
	u8 vgen_range_lp4 = 0; //10%~33.4% of 1.1v

	vgen_ctrl_0_t vgen = { .all = 0 };
	vgen.b.vgen_bypass = 0;
	vgen.b.vgen_buf_en = 0; //vgen_buf_en = 0 -> DAC_EN = 1: The VREF pad is connected to the internal VREF

	switch (DFI_DRAM_TYPE) {
	case DFI_DDR4: //DDR4
		vgen.b.vgen_vsel = vgen_vsel_ddr4; //65.38%
		vgen.b.vgen_range = vgen_range_ddr4; //DDR4, choose Range 2 (44.3% to 83.1% of 1.2v).
		break;
	case DFI_DDR3: //DDR3
		vgen.b.vgen_bypass = 1; //use internal half VDDQ
		vgen.b.vgen_pu = 0; //power up
		break;
	case DFI_LPDDR4: //LPDDR4
		vgen.b.vgen_vsel = vgen_vsel_lp4; //65.38%
		vgen.b.vgen_range = vgen_range_lp4; //DDR4, choose Range 2 (44.3% to 83.1% of 1.2v).
		break;
	default: //default to DDR4
		vgen.b.vgen_vsel = vgen_vsel_ddr4; //65.38%
		vgen.b.vgen_range = vgen_range_ddr4; //DDR4, choose Range 2 (44.3% to 83.1% of 1.2v).
	}
	vgen.b.vgen_pu = 0; //power down
	dfi_writel(vgen.all, VGEN_CTRL_0);
	ndelay(1000);
	vgen.b.vgen_pu = 1; //power up
	dfi_writel(vgen.all, VGEN_CTRL_0);
	ndelay(1000);

	//config output timing
	out_ctrl_0_t out_ctrl_0 = { .all = 0 };
	out_ctrl_0.b.oed_ph_dly = OED_PH_DLY;
	out_ctrl_0.b.oep_e_ph_dly = OEP_E_PH_DLY;
	out_ctrl_0.b.oep_s_ph_dly = OEP_S_PH_DLY;
#if defined(LPDDR4)
	out_ctrl_0.b.wdqs_control_mode2_on = WDQS_CONTROL_MODE2_ON_LP4;
	out_ctrl_0.b.wdqs_control_mode1 = WDQS_CONTROL_MODE1_LP4;
	out_ctrl_0.b.wdqs_on = WDQS_ON_LP4;
#endif
	out_ctrl_0.b.addr_mirror_en = 0;
	dfi_writel(out_ctrl_0.all, OUT_CTRL_0);

	out_ctrl_1_t out_ctrl_1 = { .all = 0 };
	out_ctrl_1.b.oea_auto_addr = OEA_AUTO_ADDR;
	out_ctrl_1.b.addr_ph_dly = 0;
#if defined(LPDDR4)
	out_ctrl_1.b.ca_5_0_on_ch_b = 1;
	out_ctrl_1.b.wdqs_on_pend_th_en = 0;
	out_ctrl_1.b.wdqs_on_pend_th = 2;
	out_ctrl_1.b.wdqs_off = WDQS_OFF_LP4;
#endif
	dfi_writel(out_ctrl_1.all, OUT_CTRL_1);

	//config input timing
	in_ctrl_0_t in_ctrl_0 = { .all = 0 };
	in_ctrl_0.b.rdptr_pre_adj = RDPTR_PRE_ADJ;
	in_ctrl_0.b.rdptr_post_adj = RDPTR_POST_ADJ;
	in_ctrl_0.b.rdptr_mode = 1;
	in_ctrl_0.b.rcvlat = 0;
	in_ctrl_0.b.rdlat = RDLAT;
	in_ctrl_0.b.rd_rstb_oepfe_dly = RD_RSTB_OEPFE_DLY;
	in_ctrl_0.b.rd_rstb_dly = RD_RSTB_DLY;
	dfi_writel(in_ctrl_0.all, IN_CTRL_0);

	lvl_all_wo_0_t lvlwo = { .all = 0 };
	lvlwo.b.rdlat_update = 1;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);
	lvlwo.b.rdlat_update = 0;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);

	strgt_ctrl_0_t strgt = { .all = 0 };
	strgt.b.strgt_mode = STRGT_MODE; //single-ended DQS
	strgt.b.strgt_en = STRGT_EN; //disable strobe gate
	dfi_writel(strgt.all, STRGT_CTRL_0);

	//config DLL settings
	dll_ctrl_0_t dll_ctrl_0 = { .all = 0 };
	dll_ctrl_0.b.dll_bypass_dly = 0xFF;
	dll_ctrl_0.b.dll_test_en = 0;
	dll_ctrl_0.b.dll_bypass_en = 0;
	dll_ctrl_0.b.dll_rstb = 0; //reset DLL
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);
	ndelay(10);
	dll_ctrl_0.b.dll_rstb = 1; //release reset
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);

	sel_ctrl_1_t sel_ctrl_1 = { .all = 0 };
	sel_ctrl_1.b.simul_dll_phase = 1;
	dfi_writel(sel_ctrl_1.all, SEL_CTRL_1);

	sel_dll_0_t sel_dll_0 = { .all = 0 };
	sel_dll_0.b.dll_phase1 = 0x22; //90 degrees
	sel_dll_0.b.dll_phase0 = 0x1F; //90 degrees
	dfi_writel(sel_dll_0.all, SEL_DLL_0);
	sel_ctrl_1.b.simul_dll_phase = 0;
	dfi_writel(sel_ctrl_1.all, SEL_CTRL_1);
	ndelay(5000); //wait 4096 cycles
	dll_ctrl_0.b.dll_update_en = 1;
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);
	ndelay(100);
	dll_ctrl_0.b.dll_update_en = 0;
	dfi_writel(dll_ctrl_0.all, DLL_CTRL_0);

	//config output delays
	sel_ctrl_1.b.simul_oddly_dqs  = 1;
	sel_ctrl_1.b.simul_oddly_dqdm = 1;
	sel_ctrl_1.b.simul_strgt_0 = 1;
	sel_ctrl_1.b.simul_oadly_ca = 1;
	sel_ctrl_1.b.simul_oadly_ck = 1;
	sel_ctrl_1.b.simul_oadly_rank = 1;
	dfi_writel(sel_ctrl_1.all,SEL_CTRL_1);

	dfi_ck_gate_adcm(CK_GATE_CS_ALL);
	dfi_ck_gate_ck(CK_GATE_CS_ALL);
	dfi_ck_gate_rank(CK_GATE_CS_ALL);
	dfi_ck_gate_data(CK_GATE_CS_ALL);

	sel_oadly_0_t oadly0 = { .all = 0 };
	oadly0.b.ca_wr_dly = ADCM_DLY_OFFSET;
	dfi_writel(oadly0.all,SEL_OADLY_0);

	sel_oadly_1_t oadly1 = { .all = 0 };
	oadly1.b.ck_wr_dly = ADCM_DLY_OFFSET;
	dfi_writel(oadly1.all,SEL_OADLY_1);

	sel_oadly_2_t oadly2 = { .all = 0 };
	oadly2.b.rank_wr_dly = ADCM_DLY_OFFSET;
	dfi_writel(oadly2.all,SEL_OADLY_2);

	oadly_0_t cm_oadly0 = { .all = 0 };
	cm_oadly0.b.cm_wr_dly = ADCM_DLY_OFFSET;
	dfi_writel(cm_oadly0.all,OADLY_0);

	sel_oddly_0_t oddly0 = { .all = 0 };
	oddly0.b.dqs_wr_dly = DATA_DLY_OFFSET;
	dfi_writel(oddly0.all,SEL_ODDLY_0);

	sel_oddly_1_t oddly1 = { .all = 0 };
	oddly1.b.dm_wr_dly = DATA_DLY_OFFSET;
	dfi_writel(oddly1.all,SEL_ODDLY_1);

	sel_oddly_2_t oddly2 = { .all = 0 };
	oddly2.b.dq_wr_dly = DATA_DLY_OFFSET;
	dfi_writel(oddly2.all,SEL_ODDLY_2);

	sel_strgt_0_t strgt0 = { .all = 0 };
	dfi_writel(strgt0.all,SEL_CTRL_0);

	dfi_ck_gate_data(CK_GATE_NORMAL);
	dfi_ck_gate_adcm(CK_GATE_NORMAL);
	dfi_ck_gate_ck(CK_GATE_NORMAL);
	dfi_ck_gate_rank(CK_GATE_NORMAL);

	sel_ctrl_1.b.simul_oddly_dqs  = 0;
	sel_ctrl_1.b.simul_oddly_dqdm = 0;
	sel_ctrl_1.b.simul_strgt_0 = 0;
	sel_ctrl_1.b.simul_oadly_ca = 0;
	sel_ctrl_1.b.simul_oadly_ck = 0;
	sel_ctrl_1.b.simul_oadly_rank = 0;
	dfi_writel(sel_ctrl_1.all,SEL_CTRL_1);

#if defined(EVB_0501)
	alert.b.alert_nodt_sel = 0x0;
	dfi_writel(alert.all, IO_ALERTN);
#endif
}

/*!
 * @brief Memory Control Vref training enable
 *
 * @param en	true to enabe
 *
 * @return	not used
 */
norm_ps_code void mc_vref_en(bool en)
{
#ifndef LPDDR4
	u8 cmd_cs_all = 0xf;
	device_config_training_t config = { .all = mc0_readl(DEVICE_CONFIG_TRAINING), };
	config.b.dq_vref_training = en ? 1 : 0;
	mc0_writel(config.all, DEVICE_CONFIG_TRAINING);

	dfi_mc_mr_rw_req_seq(MR_WRITE, MR6, cmd_cs_all);
#endif
}

/*!
 * @brief Read current PLL setting and return target speed
 *
 * @return	result
 */
norm_ps_code u16 mc_get_target_speed(void)
{
	// Get PLL settings to know what frequency is being used
	// Then program CL, CWL, nWR accordingly
	void *plladdr = (void*) 0xc0040100;
	u32 r_pll = readl(plladdr);

	u16 target_speed;

	if (((r_pll >> 30) & 1) != 1)
		utils_dfi_trace(LOG_ERR, 0xdb06, "Warning: PLL2 not ready. PLL2 setting = 0x%x.\n", r_pll);

	switch (r_pll & 0x3FFFFFFF) {
	case DDR_PLL_3200:
		target_speed = 3200;
		break;
	case DDR_PLL_2666:
		target_speed = 2666;
		break;
	case DDR_PLL_2400:
		target_speed = 2400;
		break;
	case DDR_PLL_2133:
		target_speed = 2133;
		break;
	case DDR_PLL_2000:
		target_speed = 2000;
		break;
	case DDR_PLL_1866:
		target_speed = 1866;
		break;
	case DDR_PLL_1600:
		target_speed = 1600;
		break;
	case DDR_PLL_1333:
		target_speed = 1333;
		break;
	case DDR_PLL_1066:
		target_speed = 1066;
		break;
	case DDR_PLL_800:
		target_speed = 800;
		break;
	case DDR_PLL_400:
		target_speed = 400;
		break;
	default:
		utils_dfi_trace(LOG_ERR, 0x98de, "Unspecified PLL register setting=0x%x. Defaulting target speed to 800.\n", r_pll);
		target_speed = 800;
	}
	return target_speed;
}

/*!
 * @brief DRAM Latency switch based on current PLL setting
 *
 * @param target_speed		Set DDR latency according to target speed
 *
 * @return	not used
 */
dfi_code void mc_switch_latency(u16 target_speed)
{
	device_config0_t config0 = { .all = 0 };
	config0.b.cas_write_latency = CAS_WRITE_LATENCY_2666;
	config0.b.nwr = NWR_2666;
	config0.b.cas_latency = CAS_LATENCY_2666;

	switch (target_speed) {
	case 3200:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_3200;
		config0.b.nwr = NWR_3200;
		config0.b.cas_latency = CAS_LATENCY_3200;
		break;
	case 2666:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_2666;
		config0.b.nwr = NWR_2666;
		config0.b.cas_latency = CAS_LATENCY_2666;
		break;
	case 2400:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_2400;
		config0.b.nwr = NWR_2400;
		config0.b.cas_latency = CAS_LATENCY_2400;
		break;
	case 2133:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_2133;
		config0.b.nwr = NWR_2133;
		config0.b.cas_latency = CAS_LATENCY_2133;
		break;
	case 2000:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_2000;
		config0.b.nwr = NWR_2000;
		config0.b.cas_latency = CAS_LATENCY_2000;
		break;
	case 1866:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_1866;
		config0.b.nwr = NWR_1866;
		config0.b.cas_latency = CAS_LATENCY_1866;
		break;
	case 1600:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_1600;
		config0.b.nwr = NWR_1600;
		config0.b.cas_latency = CAS_LATENCY_1600;
		break;
	case 800:
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_800;
		config0.b.nwr = NWR_800;
		config0.b.cas_latency = CAS_LATENCY_800;
		break;
	default:
		//utils_dfi_trace(LOG_ERR, 0, "Unspecified target speed=%d. Defaulting DDR low mode.\n", target_speed);
		config0.b.cas_write_latency = CAS_WRITE_LATENCY_LOW;
		config0.b.nwr = NWR_LOW;
		config0.b.cas_latency = CAS_LATENCY_LOW;

	}

	// If read DBI is enabled, CAS latency is increased according to JEDEC spec
	u8 cl_dbi_offset;
#if defined(LPDDR4)
	// CL offset due to read DBI is done automatically by hardware
	// FW needs to always set the non-DBI cas latency
	cl_dbi_offset = 0;
#else
	// DDR4 increase is based on CWL (see tAA_DBI in JEDEC)
	// Assumes CWL set A. CWL set B has slightly different conditions.
	if (config0.b.cas_write_latency <= 10)
		cl_dbi_offset = 2;
	else if (config0.b.cas_write_latency <= 14)	// includes CWL==11,12,14
		cl_dbi_offset = 3;
	else // includes CWL==16, as 15 is not valid
		cl_dbi_offset = 4;
#endif
	cl_dbi_offset = READ_DBI_ENABLE ? cl_dbi_offset : 0;
//	u16 cl_orig = config0.b.cas_latency;
	config0.b.cas_latency = config0.b.cas_latency + cl_dbi_offset;
//	utils_dfi_trace(LOG_ERR, 0, "CL orig = %d, offset = %d.\n", cl_orig, cl_dbi_offset);
	mc0_writel(config0.all, DEVICE_CONFIG0);

	return;
}

/*!
 * @brief Memory Control ddr init request
 *
 * @return	not used
 */
dfi_code void mc_ddr_init_req(void)
{
	mc_cmd_t cmd = { .all = 0 };
	cmd.b.dram_init_req = 1;
	mc0_writel(cmd.all, MC_CMD);
	cmd.b.dram_init_req = 0; //auto clear

	while (1) { //poll(mc_reg_blk.DRAM_power_status.init_done_cs0 == 1)
		dram_power_status_t power = { .all = mc0_readl(DRAM_POWER_STATUS), };
		if (power.b.init_done_cs0 == 1)
			break;
	};

 	if (DRAM_TYPE_MC == MC_LPDDR4) {
		u8 cs = CS_ALL_BITS; // Apply settings to all Chip Select
		device_config2_t config2 = { .all = mc0_readl(DEVICE_CONFIG2), };

		for (int i =0; i<1; i++) {
			config2.b.fsp_wr = i;
			mc0_writel(config2.all,DEVICE_CONFIG2);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cs); //Write to LP4 MR13, switch FSP_WR
			ndelay(100);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR1, cs); //Write to LP4 MR1
			ndelay(100);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR2, cs); //Write to LP4 MR2
			ndelay(100);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR3, cs); //Write to LP4 MR3
			ndelay(100);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR11, cs); //Write to LP4 MR11
		}
		config2.b.fsp_wr = 0;
		mc0_writel(config2.all,DEVICE_CONFIG2);
	}
}

/*!
 * @brief Memory Control init post
 *
 * @return	not used
 */
void mc_init_post(void)
{
#ifdef FPGA
	device_config1_t config1 = { .all = mc0_readl(DEVICE_CONFIG1) };
	config1.b.dram_dll_reset = 1;
	mc0_writel(config1.all, DEVICE_CONFIG1);

	//Post DRAM Init
	config1.b.dram_dll_reset = 0;
	mc0_writel(config1.all, DEVICE_CONFIG1);

	config1.b.dram_dll_disable = 0;
	mc0_writel(config1.all, DEVICE_CONFIG1);

	// MRW to MR1
	dram_mode_reg_cmd_t modereg = { .all = 0 };
	modereg.b.mode_reg_cs_sel = 0xf;
	modereg.b.mode_reg_device_sel = 1;
	modereg.b.mode_reg_write_req = 1;
	modereg.b.mode_reg_addr_sel = 1;
	mc0_writel(modereg.all, DRAM_MODE_REG_CMD);

	while (1) {
		mc_status_t status = { .all = mc0_readl(MC_STATUS), };
		if ((status.all & (~0x20000000)) == 0xef71f1f)
			break;
	} //Ignore bit 29 (redmine #3906)

	while (1) {
		mc_status_t status = { .all = mc0_readl(MC_STATUS), };
		if ((status.all & (~0x20000000)) == 0xef71f1f)
			break;
	} //Ignore bit 29 (redmine #3906)

	//DDR4
	io_data_1_t data1 = { .all = 0 };
	data1.b.dqs_pd = 1;
	data1.b.dqsn_pu = 0; //changed from 1 to 0. PU is active low
	dfi_writel(data1.all, IO_DATA_1);
#endif
}

/*!
 * @brief Memory Control init with specific cs
 *
 * @param cs
 * @param area_length
 * @param data_width
 * @param col
 * @param row
 * @param bank
 * @param bank_group
 * @param brc
 *
 * @return	not used
 */
norm_ps_code void mc_init_single(u8 cs, u8 area_length, u8 data_width, u8 col, u8 row, u8 bank, u8 bank_group, bool brc)
{
	u8 cs1_start_addr_low[] = {0x04, 0x08, 0x10, 0x20, 0x00, 0x00};	//B[31:26] 0x04: 256MB, 0x08: 512MB, 0x10: 1GB, 0x20: 2GB, 0x00: 4GB, 0x00: 8GB
	u8 cs1_start_addr_high[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x02};//B[63:32] 0x00: 256MB, 0x00: 512MB, 0x00: 1GB, 0x00: 2GB, 0x01: 4GB, 0x02: 8GB

	cs_config_sel_t config = { .all = 0 };
	config.b.cs_config_cs_sel = cs;
	mc0_writel(config.all, CS_CONFIG_SEL);

	cs_config_addr_map0_t map0 = { .all = 0 };
	if (cs == 0)
		map0.b.start_addr_low = 0;
	else if (cs == 1)
		map0.b.start_addr_low = cs1_start_addr_low[area_length-MC_AREA_LEN_256M];
	map0.b.area_length = area_length;
	map0.b.cs_enable = 1;
	mc0_writel(map0.all, CS_CONFIG_ADDR_MAP0);

	cs_config_addr_map1_t map1 = { .all = 0 };
	if (cs == 0)
		map1.b.start_addr_high = 0;
	else if (cs == 1)
		map1.b.start_addr_high = cs1_start_addr_high[area_length-MC_AREA_LEN_256M];
	mc0_writel(map1.all, CS_CONFIG_ADDR_MAP1);

	cs_config_device0_t device0 = { .all = 0 };

#if defined(LPDDR4)
	if (brc) //BRC: <DATA_WIDTH> + <COLUMN_ADDR_WIDTH_NUM> + <ROW_ADDR_WIDTH_NUM> + 11
		device0.b.bank_addr_mapping = data_width + col + row + 11 - 1;
	else // //RBC: <DATA_WIDTH> + <COLUMN_ADDR_WIDTH_NUM>
		device0.b.bank_addr_mapping = data_width + col - 1;
#else
	if (brc) //BRC: <DATA_WIDTH> + <COLUMN_ADDR_WIDTH_NUM> + <ROW_ADDR_WIDTH_NUM> + 11
		device0.b.bank_addr_mapping = data_width + col + row + 11;
	else // //RBC: <DATA_WIDTH> + <COLUMN_ADDR_WIDTH_NUM>
		device0.b.bank_addr_mapping = data_width + col;
#endif
	device0.b.chip_id_num = 0;
	device0.b.row_addr_width_num = row;
	device0.b.column_addr_width_num = col;
#if defined(LPDDR4)
	device0.b.bank_group_num = 0;
#else
	device0.b.bank_group_num = bank_group;
#endif
	device0.b.bank_num = bank;
	mc0_writel(device0.all, CS_CONFIG_DEVICE0);

	cs_config_device1_t device1 = { .all = 0 };
	device1.b.device_die_data_width = DEVICE_DIE_DATA_WIDTH[cs];
	mc0_writel(device1.all, CS_CONFIG_DEVICE1);

	cs_config_odt_t odt = { .all = 0 };
	odt.b.odt_value_write = ODT_VALUE_WRITE[cs];
	odt.b.odtd_ca = ODTD_CA[cs];
	odt.b.odt_value_park = ODT_VALUE_PARK[cs];
	odt.b.odt_value = ODT_VALUE[cs];
	odt.b.odte_cs = ODTE_CS[cs];
	odt.b.odte_ck = ODTE_CK[cs];
	mc0_writel(odt.all, CS_CONFIG_ODT);


	//Shared Config
	device_mode_t mode = { .all = 0 };
	mode.b.burst_length = BURST_LENGTH;
	mode.b.data_width = data_width;
	mode.b.data_mask_enable = DATA_MASK_ENABLE;
	mode.b.dram_type = DRAM_TYPE_MC;
	mc0_writel(mode.all, DEVICE_MODE);

	u8 rdbi_enable = READ_DBI_ENABLE;
	u8 wdbi_enable = WRITE_DBI_ENABLE;

	u16 target_speed_current = mc_get_target_speed();
	mc_switch_latency(target_speed_current); // program CL, CWL, nWR
	device_config0_t config0 = { .all = mc0_readl(DEVICE_CONFIG0) };
	config0.b.wl_select = 0;
	mc0_writel(config0.all, DEVICE_CONFIG0);

	device_config1_t config1 = { .all = mc0_readl(DEVICE_CONFIG1) };
	config1.b.dram_dll_disable = 1;
	mc0_writel(config1.all, DEVICE_CONFIG1);
	config1.all = 0;	// todo why clear? what about dram_dll_disable?
	config1.b.dram_dll_reset = 1;
	mc0_writel(config1.all, DEVICE_CONFIG1);

	device_config2_t config2 = { .all = 0 };
	config2.b.pu_cal = 1; //Match MC Reg Init Value, 0:VDDQ/2.5 1:VDDQ/3
	mc0_writel(config2.all, DEVICE_CONFIG2);

	device_config3_t config3 = { .all = 0 };
	config3.b.read_dbi_enable = rdbi_enable;
	config3.b.write_dbi_enable = wdbi_enable;
	config3.b.wr_postamble = WR_POSTAMBLE;
	config3.b.wr_preamble = WR_PREAMBLE;
	config3.b.rd_postamble = RD_POSTAMBLE;
	config3.b.rd_preamble = RD_PREAMBLE;
	mc0_writel(config3.all, DEVICE_CONFIG3);

	u16 dfi_data_byte_disable;
#ifdef ENABLE_PARALLEL_ECC
	if (mode.b.data_width == 4)
		dfi_data_byte_disable = 0x000;
	else if (mode.b.data_width == 3)
		dfi_data_byte_disable = 0x0F0;//0x0F0;//0x1E0;
	else if (mode.b.data_width == 2)
		dfi_data_byte_disable = 0x1F8;
	else
		dfi_data_byte_disable = 0x1FC;
#else
	if (mode.b.data_width == 4)
		dfi_data_byte_disable = 0x100;
	else if (mode.b.data_width == 3)
		dfi_data_byte_disable = 0x1F0;
	else if (mode.b.data_width == 2)
		dfi_data_byte_disable = 0x1FC;
	else
		dfi_data_byte_disable = 0x1FE;
#endif
	dfi_phy_cntl3_t cntl3 = { .all = 0 };
	cntl3.b.dfi_data_byte_disable = dfi_data_byte_disable;
	mc0_writel(cntl3.all, DFI_PHY_CNTL3);

	//Timing Registers
	dram_timing_init0_t init0;
	dram_timing_init1_t init1;
	dram_timing_init2_t init2;
	dram_timing_init3_t init3;
	dram_timing_init4_t init4;
	dram_timing_core0_t core0;
	dram_timing_core1_t core1;
	dram_timing_core2_t core2;
	dram_timing_core3_t core3;
	dram_timing_ref0_t ref0;
	dram_timing_sr0_t sr0;
	dram_timing_sr1_t sr1;
	dram_timing_pd_t pd;
	dram_timing_offspec_t offspec;

	init0.all = DDR_INIT_DRAM_TIMING_INIT0_RAINIER;
	init1.all = DDR_INIT_DRAM_TIMING_INIT1_RAINIER;
	init2.all = DDR_INIT_DRAM_TIMING_INIT2_RAINIER;
	init3.all = DDR_INIT_DRAM_TIMING_INIT3_RAINIER;
	init4.all = DDR_INIT_DRAM_TIMING_INIT4_RAINIER;
	core0.all = DDR_INIT_DRAM_TIMING_CORE0_RAINIER;
	core1.all = DDR_INIT_DRAM_TIMING_CORE1_RAINIER;
	core2.all = DDR_INIT_DRAM_TIMING_CORE2_RAINIER;
	core3.all = DDR_INIT_DRAM_TIMING_CORE3_RAINIER;
	ref0.all = DDR_INIT_DRAM_TIMING_REF0_RAINIER;
	sr0.all = DDR_INIT_DRAM_TIMING_SR0_RAINIER;
	sr1.all = DDR_INIT_DRAM_TIMING_SR1_RAINIER;
	pd.all = DDR_INIT_DRAM_TIMING_PD_RAINIER;
	offspec.all = DDR_INIT_DRAM_TIMING_OFFSPEC_RAINIER;

	mc0_writel(init0.all, DRAM_TIMING_INIT0);
	mc0_writel(init1.all, DRAM_TIMING_INIT1);
	mc0_writel(init2.all, DRAM_TIMING_INIT2);
	mc0_writel(init3.all, DRAM_TIMING_INIT3);
	mc0_writel(init4.all, DRAM_TIMING_INIT4);
	mc0_writel(core0.all, DRAM_TIMING_CORE0);
	mc0_writel(core1.all, DRAM_TIMING_CORE1);
	mc0_writel(core2.all, DRAM_TIMING_CORE2);
	mc0_writel(core3.all, DRAM_TIMING_CORE3);
	mc0_writel(ref0.all, DRAM_TIMING_REF0);
	mc0_writel(sr0.all, DRAM_TIMING_SR0);
	mc0_writel(sr1.all, DRAM_TIMING_SR1);
	mc0_writel(pd.all, DRAM_TIMING_PD);
	mc0_writel(offspec.all, DRAM_TIMING_OFFSPEC);

	dram_timing_training0_t training0 = { .all = 0 };
	training0.b.dummy = 0;
	mc0_writel(training0.all, DRAM_TIMING_TRAINING0);

	timing_misc_t misc;
	misc.all = DDR_INIT_DRAM_TIMING_MISC;
	mc0_writel(misc.all, TIMING_MISC);

	//For CRUCIAL DIMM, CWL need set to 12 for 2400 and lower speed.
	//CWL need set to 14, only if speed is above 2666.
	dfi_phy_cntl1_t cntl1 = { .all = 0 };
	if (DRAM_TYPE_MC == MC_LPDDR4) {
		if (config0.b.cas_latency >= 6)
			cntl1.b.trddata_en = config0.b.cas_latency - 2;
		else
			cntl1.b.trddata_en = 0;
	}
	else {//Default to DDR4
		cntl1.b.trddata_en = config0.b.cas_latency - 3;
	}
	cntl1.b.tphy_wrlat = config0.b.cas_write_latency;
	cntl1.b.tphy_wrdata = TPHY_WRDATA;
	mc0_writel(cntl1.all, DFI_PHY_CNTL1);

	//Optional
	odt_cntl_0_t odt0 = { .all = 0 };
	odt0.b.odt_termination_enb = ODT_TERMINATION_ENB;
#if defined(LPDDR4)
	odt0.b.force_odt = FORCE_ODT;
#endif
	mc0_writel(odt0.all, ODT_CNTL_0);

	odt_cntl_1_t odt1 = { .all = 0 };
	odt1.all = 0x0;
	mc0_writel(odt1.all, ODT_CNTL_1);

	axi_cntl_t axi = { .all = 0 };
	axi.b.axi_b_slverr_dis = 1; //default no AXI errors
	axi.b.axi_r_slverr_dis = 1; //default no AXI errors
	axi.b.read_data_interleaving_disable = 0; //AXI Port Read Data Interleaving Disable, 0: Enable, 1: Disable
	mc0_writel(axi.all, AXI_CNTL);

	write_buffer_cntl_t buffer = { .all = 0 };
	buffer.b.force_read_serve_count = 0x20;
	buffer.b.force_read_serve_enb = 1;
	buffer.b.drain_level = 3;
	buffer.b.drain_trigger_threshold = 3;
	buffer.b.auto_drain_enb = 1; //drain when read is empty
	mc0_writel(buffer.all, WRITE_BUFFER_CNTL);

	read_starvation_cntl_t starvation = { .all = 0 };
	starvation.b.rcp_starvation_cntl_enb = 1;
	starvation.b.rcp_starv_timer_value_init = 0x200;
	mc0_writel(starvation.all, READ_STARVATION_CNTL);

	//DRAM Initialization
	mc_ddr_init_req();

	mc_init_post();

	mc_dfc_cmd_t dfc = { .all = 0 };
	dfc.b.halt_rw_traffic = 1;
	dfc.b.dfc_mode_enb = 1;
	mc0_writel(dfc.all, MC_DFC_CMD); //halt traffic

	//DDR4 only
	dram_calibration_cmd_t calibration = { .all = 0 };
	calibration.b.dram_calibration_cs_sel = 0xf;
	calibration.b.zqcl_req = 1;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);

	ndelay(20000);
	dfc.b.halt_rw_traffic = 0;
	dfc.b.dfc_mode_enb = 0;
	mc0_writel(dfc.all, MC_DFC_CMD); //resume traffic

#ifdef ENABLE_PARALLEL_ECC
	ras_cntl_t ras = { .all = 0 };
	ras.b.ecc_enb = 1;
	mc0_writel(ras.all, RAS_CNTL);

	ecc_err_count_status_1_t err_cnt = { .all = mc0_readl(ECC_ERR_COUNT_STATUS_1), };
	err_cnt.b.ecc_1bit_err_count_clr = 1;
	err_cnt.b.ecc_2bit_err_count_clr = 1;
	mc0_writel(err_cnt.all, ECC_ERR_COUNT_STATUS_1);
	mc0_writel(ECC_ERR_INT_MASK, INTERRUPT_STATUS);
#else
	ras_cntl_t ras = { .all = 0 };
	mc0_writel(ras.all, RAS_CNTL);
#endif
}

/*!
 * @brief Memory Control init
 *
 * @return	not used
 */
norm_ps_code void mc_init(void)
{
	u32 ddr_cs_num = DDR_CS_NUM;
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	// NOTE: make sure GPIO14 on rainier board is set to 1!
#if M2
	u8 rainier_mode = 1;
	utils_dfi_trace(LOG_ERR, 0x24bf, "M2 mode, forced Rainier\n");
#else
	gpio_pad_ctrl_t gpio_i = { .all = readl((void *) 0xc0040130), };
	u8 rainier_mode = ((gpio_i.b.gpio_in >> 14) & 1);
#endif

	if (DRAM_TYPE_MC == MC_LPDDR4) // LPDDR4 need boot from low speed
		dfi_set_pll_freq(800, false);

#if defined(DDR4) && !defined(U2_LJ)
	extern volatile u64 ddr_capacity;
	if (ddr_capacity > 0x100000000 && ddr_cs_num == 1)
		ddr_cs_num++;
#endif

	//Loop Thru Per-CS Config
	for (int cs = 0; cs < ddr_cs_num; cs++) {
		cs_config_sel_t config = { .all = 0 };
		config.b.cs_config_cs_sel = cs;
		mc0_writel(config.all, CS_CONFIG_SEL);

		cs_config_addr_map0_t map0 = { .all = 0 };
		map0.b.start_addr_low = START_ADDR_LOW[cs];
#if 0	//20200822-Eddie
		u32 area_length = AREA_LENGTH_RAINIER[cs];
#if defined(U2_LJ) && defined(DDR4) && (DDR_CS_NUM == 1)
		extern u64 ddr_capacity;
		if (ddr_capacity == 0x200000000)
			area_length += 1; // 0x15
#endif

		if (rainier_mode)
			map0.b.area_length = area_length;
		else
			map0.b.area_length = AREA_LENGTH[cs];
#else	
	if (rainier_mode){
			if ((ddr_capacity>>20) == DDR_4GB){	// 4GB
				utils_dfi_trace(LOG_ERR, 0x942a, "DDRConfig by AutoSZ 4GB \n");
				map0.b.area_length = 0x14;
			}
			else	if ((ddr_capacity>>20) == DDR_8GB){	// 8GB
				utils_dfi_trace(LOG_ERR, 0x1236, "DDRConfig by AutoSZ 8GB \n");
				map0.b.area_length = 0x15;
			}
			else{
				utils_dfi_trace(LOG_ERR, 0x4a21, "DDRConfig by CompilerSZ \n");
			map0.b.area_length = AREA_LENGTH_RAINIER[cs];
			}
	}		
	else
		map0.b.area_length = AREA_LENGTH[cs];
#endif	
		map0.b.cs_enable = 1;
		mc0_writel(map0.all, CS_CONFIG_ADDR_MAP0);

		cs_config_addr_map1_t map1 = { .all = 0 };
		map1.b.start_addr_high = START_ADDR_HIGH[cs];
		mc0_writel(map1.all, CS_CONFIG_ADDR_MAP1);

		cs_config_device0_t device0 = { .all = 0 };
		//<data_width> - 1 + <column_addr_width_num> + 6
		u8 bank_addr_mapping;
		if (rainier_mode)
			bank_addr_mapping = DATA_WIDTH_RAINIER - 1 + COLUMN_ADDR_WIDTH_NUM[cs] + 6;
		else
			bank_addr_mapping = DATA_WIDTH - 1 + COLUMN_ADDR_WIDTH_NUM[cs] + 6;
		device0.b.bank_addr_mapping = bank_addr_mapping;
		device0.b.chip_id_num = CHIP_ID_NUM[cs];
#if 0	//20200822-Eddie
    u32 row_addr_width = ROW_ADDR_WIDTH_NUM[cs];
#if defined(U2_LJ) && defined(DDR4) && (DDR_CS_NUM == 1)
		if (ddr_capacity == 0x200000000)
			row_addr_width += 1; // 0x6
#endif
		device0.b.row_addr_width_num = row_addr_width;
#else
		if ((ddr_capacity>>20) == DDR_4GB){	// 4GB
			device0.b.row_addr_width_num = 0x5;
		}
		else	if ((ddr_capacity>>20) == DDR_8GB){	// 8GB
			device0.b.row_addr_width_num = 0x6;
		}
		else
			device0.b.row_addr_width_num = ROW_ADDR_WIDTH_NUM[cs];
#endif
		
		device0.b.column_addr_width_num = COLUMN_ADDR_WIDTH_NUM[cs];
		device0.b.bank_group_num = BANK_GROUP_NUM[cs];
		device0.b.bank_num = BANK_NUM[cs];
		mc0_writel(device0.all, CS_CONFIG_DEVICE0);

		cs_config_device1_t device1 = { .all = 0 };
		device1.b.device_die_data_width = DEVICE_DIE_DATA_WIDTH[cs];
		mc0_writel(device1.all, CS_CONFIG_DEVICE1);

		cs_config_odt_t odt = { .all = 0 };
        odt.b.odtd_ca = ODTD_CA[cs];
		odt.b.odt_value_park = ODT_VALUE_PARK[cs];
        odt.b.odte_cs = ODTE_CS[cs];
		odt.b.odte_ck = ODTE_CK[cs];
        #if 1
		//odt.b.odt_value_write = ODT_VALUE_WRITE[cs];
        odt.b.odt_value_write = DRAM_ODT_VALUE_WRITE[cs];		
		//odt.b.odt_value = ODT_VALUE[cs];
        odt.b.odt_value = DRAM_ODT_VALUE[cs];		
        odt.b.output_driver_impedence = DRAM_Drive_Strength_Value[cs];
        #else
        if ((ddr_capacity>>20) == DDR_4GB)// 4GB
        {   
            odt.b.odt_value = 0x5;
            odt.b.odt_value_write = 0x4;
			odt.b.output_driver_impedence = 0x0;
		}
		else if ((ddr_capacity>>20) == DDR_8GB)// 8GB
        {  
            odt.b.odt_value = 0x2;
            odt.b.odt_value_write = 0x4;
			odt.b.output_driver_impedence = 0x0;
		}
		else
        {
            odt.b.odt_value_write = ODT_VALUE_WRITE[cs];
            odt.b.odt_value = ODT_VALUE[cs];
        }      
        #endif
        mc0_writel(odt.all, CS_CONFIG_ODT);
        utils_dfi_trace(LOG_ERR, 0xcec4, "ODT_VALUE:0x%x, ODT_VALUE_WRITE:0x%x, ODT_VALUE_PARK:0x%x, OUTPUT_DRIVER_IMPEDENCE(DS):0x%x\n",odt.b.odt_value,odt.b.odt_value_write,odt.b.odt_value_park,odt.b.output_driver_impedence);
		//dfi_device_vref_cfg_t_seq(cs, 1, 23);
	}

	//Shared Config
	device_mode_t mode = { .all = 0 };
	mode.b.burst_length = BURST_LENGTH;
	if (rainier_mode)
		mode.b.data_width = DATA_WIDTH_RAINIER;
	else
		mode.b.data_width = DATA_WIDTH;
	mode.b.data_mask_enable = DATA_MASK_ENABLE;
	mode.b.dram_type = DRAM_TYPE_MC;
	mc0_writel(mode.all, DEVICE_MODE);

//	u8 dll_disable = 0;
	u8 rdbi_enable = READ_DBI_ENABLE;
	u8 wdbi_enable = WRITE_DBI_ENABLE;

	u16 target_speed_current = mc_get_target_speed();
	mc_switch_latency(target_speed_current); // program CL, CWL, nWR
	device_config0_t config0 = { .all = mc0_readl(DEVICE_CONFIG0) };
	config0.b.wl_select = 0;
	mc0_writel(config0.all, DEVICE_CONFIG0);

	device_config1_t config1 = { .all = mc0_readl(DEVICE_CONFIG1) };
	config1.b.dram_dll_disable = 1;
	mc0_writel(config1.all, DEVICE_CONFIG1);
	config1.all = 0;	// todo why clear? what about dram_dll_disable?
	config1.b.dram_dll_reset = 1;
	mc0_writel(config1.all, DEVICE_CONFIG1);

	device_config2_t config2 = { .all = 0 };
	config2.b.pu_cal = 1; //Match MC Reg Init Value, 0:VDDQ/2.5 1:VDDQ/3
	mc0_writel(config2.all, DEVICE_CONFIG2);

	device_config3_t config3 = { .all = 0 };
	config3.b.read_dbi_enable = rdbi_enable;
	config3.b.write_dbi_enable = wdbi_enable;
	config3.b.wr_postamble = WR_POSTAMBLE;
	config3.b.wr_preamble = WR_PREAMBLE;
	config3.b.rd_postamble = RD_POSTAMBLE;
	config3.b.rd_preamble = RD_PREAMBLE;
	mc0_writel(config3.all, DEVICE_CONFIG3);

	u16 dfi_data_byte_disable;
#ifdef ENABLE_PARALLEL_ECC
	if (mode.b.data_width == 4)
		dfi_data_byte_disable = 0x000;
	else if (mode.b.data_width == 3)
		dfi_data_byte_disable = 0x0F0;//0x0F0;//0x1E0;
	else if (mode.b.data_width == 2)
		dfi_data_byte_disable = 0x1F8;
	else
		dfi_data_byte_disable = 0x1FC;
#else
	if (mode.b.data_width == 4)
		dfi_data_byte_disable = 0x100;
	else if (mode.b.data_width == 3)
		dfi_data_byte_disable = 0x1F0;
	else if (mode.b.data_width == 2)
		dfi_data_byte_disable = 0x1FC;
	else
		dfi_data_byte_disable = 0x1FE;
#endif
	dfi_phy_cntl3_t cntl3 = { .all = 0 };
	//if (rainier_mode)
	//	dfi_data_byte_disable = 0x1F0;
	cntl3.b.dfi_data_byte_disable = dfi_data_byte_disable;
	mc0_writel(cntl3.all, DFI_PHY_CNTL3);

	//Timing Registers
	dram_timing_init0_t init0;
	dram_timing_init1_t init1;
	dram_timing_init2_t init2;
	dram_timing_init3_t init3;
	dram_timing_init4_t init4;
	dram_timing_core0_t core0;
	dram_timing_core1_t core1;
	dram_timing_core2_t core2;
	dram_timing_core3_t core3;
	dram_timing_ref0_t ref0;
	dram_timing_sr0_t sr0;
	dram_timing_sr1_t sr1;
	dram_timing_pd_t pd;
	dram_timing_offspec_t offspec;

	if (rainier_mode) {		//20200822-Eddie
		if ((ddr_capacity>>20) == DDR_4GB){	// 4GB
			init0.all = 0x6404E200;    
			init1.all = 0x0C350010;    
			init2.all = 0x00000010;    
			init3.all = 0x00400400;    
			init4.all = 0x02000080;    
			core0.all = 0x4C18340B;    
			core1.all = 0x60C18048;    
			core2.all = 0x010C4B0C;    
			core3.all = 0x08000018;    
			ref0.all = 0x00C30230;     
			sr0.all =  0x10102408;     
			sr1.all =  0x00000000;     
			pd.all =   0x2142004A;     
			offspec.all =   0x00c0838e;
		}
		else	if ((ddr_capacity>>20) == DDR_8GB){	// 8GB
			init0.all = 0x6404E200;  
			init1.all = 0x0C350010;  
			init2.all = 0x00000010;  
			init3.all = 0x00400400;  
			init4.all = 0x02000080;  
			core0.all = 0x4C18340B;  
			core1.all = 0x60C18048;  
			core2.all = 0x010C4B0C;  
			core3.all = 0x08000018;  
			ref0.all = 0x00C30730;   
			sr0.all = 0x10102408;    
			sr1.all = 0x00000000;    
			pd.all = 0x2142004A;     
			offspec.all = 0x00c0838e;
		}
		else{
		init0.all = DDR_INIT_DRAM_TIMING_INIT0_RAINIER;
		init1.all = DDR_INIT_DRAM_TIMING_INIT1_RAINIER;
		init2.all = DDR_INIT_DRAM_TIMING_INIT2_RAINIER;
		init3.all = DDR_INIT_DRAM_TIMING_INIT3_RAINIER;
		init4.all = DDR_INIT_DRAM_TIMING_INIT4_RAINIER;
		core0.all = DDR_INIT_DRAM_TIMING_CORE0_RAINIER;
		core1.all = DDR_INIT_DRAM_TIMING_CORE1_RAINIER;
		core2.all = DDR_INIT_DRAM_TIMING_CORE2_RAINIER;
		core3.all = DDR_INIT_DRAM_TIMING_CORE3_RAINIER;
		ref0.all = DDR_INIT_DRAM_TIMING_REF0_RAINIER;
		sr0.all = DDR_INIT_DRAM_TIMING_SR0_RAINIER;
		sr1.all = DDR_INIT_DRAM_TIMING_SR1_RAINIER;
		pd.all = DDR_INIT_DRAM_TIMING_PD_RAINIER;
		offspec.all = DDR_INIT_DRAM_TIMING_OFFSPEC_RAINIER;
		}
	} else {
		init0.all = DDR_INIT_DRAM_TIMING_INIT0;
		init1.all = DDR_INIT_DRAM_TIMING_INIT1;
		init2.all = DDR_INIT_DRAM_TIMING_INIT2;
		init3.all = DDR_INIT_DRAM_TIMING_INIT3;
		init4.all = DDR_INIT_DRAM_TIMING_INIT4;
		core0.all = DDR_INIT_DRAM_TIMING_CORE0;
		core1.all = DDR_INIT_DRAM_TIMING_CORE1;
		core2.all = DDR_INIT_DRAM_TIMING_CORE2;
		core3.all = DDR_INIT_DRAM_TIMING_CORE3;
		ref0.all = DDR_INIT_DRAM_TIMING_REF0;
		sr0.all = DDR_INIT_DRAM_TIMING_SR0;
		sr1.all = DDR_INIT_DRAM_TIMING_SR1;
		pd.all = DDR_INIT_DRAM_TIMING_PD;
		offspec.all = DDR_INIT_DRAM_TIMING_OFFSPEC;
	}
	mc0_writel(init0.all, DRAM_TIMING_INIT0);
	mc0_writel(init1.all, DRAM_TIMING_INIT1);
	mc0_writel(init2.all, DRAM_TIMING_INIT2);
	mc0_writel(init3.all, DRAM_TIMING_INIT3);
	mc0_writel(init4.all, DRAM_TIMING_INIT4);
	mc0_writel(core0.all, DRAM_TIMING_CORE0);
	mc0_writel(core1.all, DRAM_TIMING_CORE1);
	mc0_writel(core2.all, DRAM_TIMING_CORE2);
	mc0_writel(core3.all, DRAM_TIMING_CORE3);
	mc0_writel(ref0.all, DRAM_TIMING_REF0);
	mc0_writel(sr0.all, DRAM_TIMING_SR0);
	mc0_writel(sr1.all, DRAM_TIMING_SR1);
	mc0_writel(pd.all, DRAM_TIMING_PD);
	mc0_writel(offspec.all, DRAM_TIMING_OFFSPEC);

	dram_timing_training0_t training0 = { .all = 0 };
	training0.b.dummy = 0;
	mc0_writel(training0.all, DRAM_TIMING_TRAINING0);

	timing_misc_t misc;
	misc.all = DDR_INIT_DRAM_TIMING_MISC;
	mc0_writel(misc.all, TIMING_MISC);

	//For CRUCIAL DIMM, CWL need set to 12 for 2400 and lower speed.
	//CWL need set to 14, only if speed is above 2666.
	dfi_phy_cntl1_t cntl1 = { .all = 0 };
	if (DRAM_TYPE_MC == MC_LPDDR4) {
		if (config0.b.cas_latency >= 6)
			cntl1.b.trddata_en = config0.b.cas_latency - 2;
		else
			cntl1.b.trddata_en = 0;
	}
	else {//Default to DDR4
		cntl1.b.trddata_en = config0.b.cas_latency - 3;
	}
	cntl1.b.tphy_wrlat = config0.b.cas_write_latency;
	cntl1.b.tphy_wrdata = TPHY_WRDATA;
	mc0_writel(cntl1.all, DFI_PHY_CNTL1);

	//Optional
	odt_cntl_0_t odt0 = { .all = 0 };
	odt0.b.odt_termination_enb = ODT_TERMINATION_ENB;
#if defined(LPDDR4)
	odt0.b.force_odt = FORCE_ODT;
#endif
	mc0_writel(odt0.all, ODT_CNTL_0);

	odt_cntl_1_t odt1 = { .all = 0 };
	odt1.all = 0x0;
	mc0_writel(odt1.all, ODT_CNTL_1);

	axi_cntl_t axi = { .all = 0 };
	axi.b.axi_b_slverr_dis = 1; //default no AXI errors
	axi.b.axi_r_slverr_dis = 1; //default no AXI errors
	axi.b.read_data_interleaving_disable = 0; //AXI Port Read Data Interleaving Disable, 0: Enable, 1: Disable
	mc0_writel(axi.all, AXI_CNTL);

	write_buffer_cntl_t buffer = { .all = 0 };
	buffer.b.force_read_serve_count = 0x20;
	buffer.b.force_read_serve_enb = 1;
	buffer.b.drain_level = 3;
	buffer.b.drain_trigger_threshold = 3;
	buffer.b.auto_drain_enb = 1; //drain when read is empty
	mc0_writel(buffer.all, WRITE_BUFFER_CNTL);

	read_starvation_cntl_t starvation = { .all = 0 };
	starvation.b.rcp_starvation_cntl_enb = 1;
	starvation.b.rcp_starv_timer_value_init = 0x200;
	mc0_writel(starvation.all, READ_STARVATION_CNTL);

	//DRAM Initialization
	mc_ddr_init_req();

	mc_init_post();

	//DFC - Dynamic frequency change
	//if (dfc)

	mc_dfc_cmd_t dfc = { .all = 0 };
	dfc.b.halt_rw_traffic = 1;
	dfc.b.dfc_mode_enb = 1;
	mc0_writel(dfc.all, MC_DFC_CMD); //halt traffic

	//DDR4 only
	dram_calibration_cmd_t calibration = { .all = 0 };
	calibration.b.dram_calibration_cs_sel = 0xf;
	calibration.b.zqcl_req = 1;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);

	ndelay(20000);
	dfc.b.halt_rw_traffic = 0;
	dfc.b.dfc_mode_enb = 0;
	mc0_writel(dfc.all, MC_DFC_CMD); //resume traffic

#ifdef ENABLE_PARALLEL_ECC
	ras_cntl_t ras = { .all = 0 };
	ras.b.ecc_enb = 1;
	mc0_writel(ras.all, RAS_CNTL);

	ecc_err_count_status_1_t err_cnt = { .all = mc0_readl(ECC_ERR_COUNT_STATUS_1), };
	err_cnt.b.ecc_1bit_err_count_clr = 1;
	err_cnt.b.ecc_2bit_err_count_clr = 1;
	mc0_writel(err_cnt.all, ECC_ERR_COUNT_STATUS_1);
	mc0_writel(ECC_ERR_INT_MASK, INTERRUPT_STATUS);
#else
	ras_cntl_t ras = { .all = 0 };
	mc0_writel(ras.all, RAS_CNTL);
#endif
	//Memory scrubbing should be done after training

#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0); // Call before returns so that counter is stopped regardless of pass/fail
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0xb7ab, "mc_init - Total cycles take: %d.\n", pc_val);
#endif
}
/*! @} */
