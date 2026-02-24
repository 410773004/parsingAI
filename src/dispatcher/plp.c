
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "l2cache.h"
#include "l2p_mgr.h"
#include "mpc.h"
#include "rdisk.h"
#include "ftl_export.h"
#include "fc_export.h"
#include "ipc_api.h"
#include "ncl_exports.h"
#include "ncl.h"
#include "cbf.h"
#include "pmu.h"
#include "die_que.h"
#include "smb_registers.h"
#include "plp.h"
#include "cmd_proc.h"
#include "smart.h"
#include "trim.h"
#include "srb.h"
#include "ssstc_cmd.h"
#if (CO_SUPPORT_READ_AHEAD == TRUE)
#include "ra.h"
#endif
#define __FILEID__ plp
#include "trace.h"

//#include "ucache.h"
#include "console.h"

#define PLP_TIMEOUT                     (20) //10 * 100ms
#define PLP_SLAVE_ADDR                  (0x5A)
#define PLP_SLAVE_ID                    (PLP_SLAVE_ADDR << 1)

#define PLP_CAP_TEST_BIT_OFST           (BIT5)
//SYT664 C88
#define CONTROL_REG_ADDR				(0x00)
#define LSC_PARAMETER_REG_ADDR			(0x01)
#define DC_DC_CONVERTER_REG_ADDR		(0x02)
#define BUCK_OFF_VOL_REG_ADDR			(0x03)
#define VOL_START_REG_ADDR				(0x04)
#define VOL_END_REG_ADDR				(0x05)
#define FREQUENCY_CAPACITANCY_REG_ADDR	(0x06)
#define DISCHARGE_TIME_H_REG_ADDR       (0x07)
#define DISCHARGE_TIME_L_REG_ADDR       (0x08)
#define LOAD_SWITCH_REG_ADDR            (0x09)

//SGM41664
#define Vendor_ID_ADDR				        (0x00) //0x0
#define BLKFET_Control_P_ADDR			    (0x01) //0x39
#define DC_DC_CONVERTER_Control_ADDR		(0x02) //0xc9
#define BUCK_OFF_Voltage_ADDR			    (0x03) //0x37
#define VDIS1_ADDR				            (0x04) //0xa6
#define VDIS2_ADDR				            (0x05) //0x8f
#define Switching_Frequency_ADDR	        (0x06) //0x1
#define CSTR_Discharge_Timer_H_ADDR         (0x07) //0x0
#define CSTR_Discharge_Timer_L_ADDR         (0x08) //0x0
#define ADC_In_ESR_Detection_ADDR           (0x09) //0x0
#define VBUS_Data_ADDR                      (0x0A) //0x0
#define STR_Over_Voltage_Protection_ADDR    (0x0B) //0x1d OVP
#define VIN_Data_ADDR                       (0x0C) //0x0
#define VSTR_Data_ADDR                      (0x0D) //0x0
#define Interrupt_Mask_Control_ADDR         (0x0E) //0x0
#define Interrupt_FLAG_ADDR                 (0x0F) //0x0
#define System_Control_ADDR                 (0x10) //0x2

//SY72025
#define MTP_reg				                (0x00) //0x0
#define LSW_CTRL_reg			            (0x01) //0xB9
#define DCDC_CONVERTER_REG		            (0x02) //0xc7
#define BUCK_OFF_reg			            (0x03) //0x37
#define VDIS1_reg				            (0x04) //0x46
#define VDIS2_reg				            (0x05) //0x2f
#define SF_controls_reg	                    (0x06) //0x11
#define CSTR_Timer_H_reg                    (0x07) //0x0
#define CSTR_Timer_L_reg                    (0x08) //0x0
#define Load_switch_current_reg             (0x09) //0x0
#define VBUS_v_reg                          (0x0A) //0x0
#define STR_OVP_reg                         (0x0B) //0x17 OVP
#define VIN_v_reg                           (0x0C) //0x0
#define STR_v_reg                           (0x0D) //0x0
#define STR_fall_time_Fault_th_reg          (0x0E) //0x0
#define STR_soft_start_t_reg                (0x0F) //0x1

#define PLP_VOLTAGE_13				    (1320)
#define	TEST_ESR_HIGH_13				(PLP_VOLTAGE_13/15/11*10)//10/11
#define	TEST_ESR_LOW_13					(PLP_VOLTAGE_13/15/11* 1)// 1/11
#define PLP_VOLTAGE_30					(3000)
#define	TEST_ESR_HIGH_Default		    (PLP_VOLTAGE_30*10/15/11)//10/11
#define	TEST_ESR_LOW_Default			(PLP_VOLTAGE_30*7/15/11) // 1/11

#if MDOT2_SUPPORT | Mdot2_22110
    #define ESR_THRESHOLD                   33 //(47uF*9)*(27-23.55)/20mA*0.46 = 33
    #define ESR_THRESHOLD_240               18 //(47uF*5)*(27-23.55)/20mA*0.46 = 18
    #define ESR_THRESHOLD_480               21 //(47uF*6)*(27-23.55)/20mA*0.46 = 21
    #define ESR_THRESHOLD_960               25 //(47uF*7)*(27-23.55)/20mA*0.46 = 25
    #define ESR_THRESHOLD_1920              35 //(56uF*8)*(27-23.55)/20mA*0.46 = 35
    #define ESR_THRESHOLD_3840              53 //(56uF*12)*(27-23.55)/20mA*0.46 = 53 //erase 1 blk   
#endif
#if UDOT2_SUPPORT
    #define ESR_THRESHOLD                   111 //1500*(27-23.55)/20*0.46
#endif


#define ESR_CYCLE                       2592000*HZ/10  //100ms*10*60*60*24*3 three days
#define write (0)
#define read  (1)

#define SGM_reboot_tag  (0xffff08F0)


typedef struct _esr_cond_judge_t{
	uint64 equal_start;
    uint64 cur_time;
    uint64 start_dis_time;
    u8 time_l;
    u8 time_h;
    u8 time_tmp_l;
    u8 time_tmp_h;
	u32 start_time;
    u16 time;
    bool flag;
}esr_cond_judge_t;

extern volatile bool shr_shutdownflag;
fast_data bool esr_status = true;
esr_cond_judge_t esr_cond_judge;
fast_data_zi esr_err_flags_t esr_err_flags;

fast_data_zi struct timer_list plp_cap_trigger_timer;
fast_data_zi struct timer_list plp_cap_check_timer;
//extern read_only_t read_only_flags;
extern u8  cur_ro_status;

fast_data u8 evt_mtp_check = 0xff;
share_data u8 evt_plp_set_ENA = 0xff;
share_data_zi volatile u8 plp_PWRDIS_flag = 0;

fast_data_zi u32 check_time;
fast_data_zi u8 plp_first_check = 0;

share_data_zi volatile bool plp_gc_suspend_done;
fast_data_zi u8 evt_plp_flush = 0;
fast_data u8 evt_check_streaming = 1;

extern volatile u8 plp_trigger;      //use to indicate plp trigger if value is not 0
extern volatile u8 plp_epm_update_done;
extern volatile u8 plp_epm_back_flag;
extern volatile bool plp_test_flag;
extern volatile bool esr_err_fua_flag;
#ifdef SKIP_MODE
extern u8* gl_pt_defect_tbl;
#endif

extern void btn_de_wr_disable(void);
extern void btn_de_wr_enable(void);
extern void btn_de_rd_disable(void);
extern void btn_de_rd_enable(void);

extern u32 I2C_write(u8 slaveID, u8 cmd_code, u8 value);
extern u32 I2C_read(u8 slaveID, u8 cmd_code, u8 *value);
extern void plp_iic_read_write(u8 slaveID, u8 cmd_code, u8 *value, u8 data, bool rw);
extern volatile bool PWRDIS_open_flag;
extern volatile bool PWRDIS_664;
extern volatile bool PLP_IC_SGM41664;
extern volatile bool PLP_IC_SYTC88_664;

extern epm_info_t*  shr_epm_info;

fast_code void plp_cap_test(bool *esr_status, u16 *time, u8 input_h, u8 input_l);

#if 0
static init_code void plp_init(void)
{

}
#endif

/*!
 * @brief ipc to tell ftl core plp flush done, trigger next event
 *
 * @param data	fctx plp flush data ctx
 *
 * @return	not used
 */
 extern ftl_flush_data_t plp_fctx;
fast_code void ipc_plp_flush_done(ftl_core_ctx_t *fctx1)
{
	//u64 start_ipc = get_tsc_64();
    //ipc
    #if PLP_DEBUG_GPIO
    gpio_set_gpio15(0);
    #endif
    /*ftl_core_reset_notify();
    while(ncl_cmd_empty && (ncl_cmd_empty(false) == false))
    {
        btn_feed_rd_dtag();
    }*/
    //disp_apl_trace(LOG_ALW, 0, "cpu1 get flush done:0x%x-%x", start_ipc>>32, start_ipc&0xFFFFFFFF);
    //sys_free(FAST_DATA, fctx);
    #ifdef DTAG_FREE_Q_RECV
    if (!CBF_EMPTY(dtag_free_recv.cbf) && dtag_free_recv.handler)               // first free dtag avoid loss data
        dtag_free_recv.handler();
    #endif
    if(shr_dtag_comt.que.rptr != shr_dtag_comt.que.wptr)                        //avoid program fail when plp happen
    {
        ftl_flush_data_t *fctx = &plp_fctx;
        fctx->ctx.caller = NULL;
        fctx->ctx.cmpl = ipc_plp_flush_done;
        fctx->nsid = 1;
        fctx->flags.all = 0;
        extern void ucache_flush();
        ucache_flush(fctx);
    }
    else
    {
		#if(PLP_NO_DONE_DEBUG == mENABLE)
		u64 start_ipc2 = get_tsc_64();
		disp_apl_trace(LOG_ALW, 0x63cd, "cpu1->cpu2 set plp trigger:0x%x-%x", start_ipc2>>32, start_ipc2&0xFFFFFFFF);
		#endif
		if(esr_err_fua_flag == true)
		{
			//esr err fua , just flush cache
			return;
		}

        cpu_msg_issue(CPU_BE - 1, CPU_MSG_PLP_TRIGGER, 0, 0);

    }
}


/*!
 * @brief after plp function done, enable host write. only used in debug
 *
 * @param req: not used, please input NULL
 *
 * @return	not used
 */
fast_code void ipc_plp_done_ack(volatile cpu_msg_req_t *req)
{
    mdelay(2000);
    nvmet_io_fetch_ctrl(false);
    btn_de_wr_enable();
	extern void btn_de_wr_cancel_hold(void);
	btn_de_wr_cancel_hold();
   // btn_de_rd_enable();

}

#if PLP_DEBUG
/*!
 * @brief debug code used to simulate host read case
 *
 * @param req: not used, please input NULL
 *
 * @return	not used
 */
 #define RDISK_L2P_FE_SRCH_QUE 		0		///< l2p search queue id
fast_code void ipc_plp_debug(volatile cpu_msg_req_t *req)
{
    // l2p search
    disp_apl_trace(LOG_INFO, 0xf4d5, "ipc plp debug fill up start");
    extern u16 ua_btag;
    l2p_srch(0, 127, ua_btag, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
}
#endif

/*!
 * @brief plp detect function
 *  step 1: set plp trigger flag
 *  step 2: start flush case
 *
 * @param req: not used
 *
 * @return	not used
 */
fast_code void plp_detect(void)
{
    #if PLP_DEBUG_GPIO
    gpio_set_gpio15(0);
    gpio_set_gpio15(1);
    #endif

	//nvmet_update_smart_stat(NULL); //move to rdisk_power_loss_flush
    //disp_apl_trace(LOG_ERR, 0, "plp_debug_main value:%x\n", plp_trigger);
    //if (!plp_test_flag) {
        shr_shutdownflag = mTRUE; // for do not open blk after PLP
        plp_trigger = 0xEE;
        plp_epm_update_done = false;
        plp_gc_suspend_done = false;
		plp_epm_back_flag = true;
        rdisk_power_loss_flush();
    //}

}

/*
static fast_code int plp_debug_main(int argc, char *argv[])
{
    disp_apl_trace(LOG_INFO, 0, "plp_debug_main value:%x", plp_trigger);
    //plp_trigger = 0xEE;
    //fsm_ctx_init(&plp_fsm.fsm, &_plp_fsm_state, NULL, NULL);
    //fsm_ctx_run(&plp_fsm.fsm);
    plp_trigger = 0xEE;
    rdisk_power_loss_flush();

    return 0;
}
*/
slow_code bool strpg_check(u32 gpio_port)
{
	if(plp_trigger)
	{
		return 0;
	}
    bool strpg;
    if(gpio_get_value(GPIO_PLP_STRPG_SHIFT))
        return 0;
    else{
        int i = 0;
        do{
            disp_apl_trace(LOG_INFO, 0xaa96, "strpg err, check again");
            strpg = gpio_get_value(GPIO_PLP_STRPG_SHIFT);
            i++;
            if(i == 3)
                break;
        }while(!strpg);
    }
    return !strpg;
}
extern bool all_init_done;
extern u8 Detect_FLAG;

ddr_code void GetSensorTemp_check(void)
{
	/*
		If GetSensorTemp stage1 done (when Detect_FLAG == 1)
		Do GetSensorTemp stage2 before any other action on SMBus 1
		Prevent remaining smb_intr_sts & data from GetSensorTemp
	*/

	u32 cnt = 0;
	extern smb_registers_regs_t *smb_mas;
    if(Detect_FLAG) {
        extern void GetSensorTemp();
		while(readl(&smb_mas->smb_intr_sts))
		{
			mdelay_plp(10);
			if(++cnt>10)
			{
				cnt=0;
				disp_apl_trace(LOG_ALW, 0x0514, "smb_intr_sts: 0x%x",readl((void*)0xC0053000));
				break;
			}
		}
        GetSensorTemp();
    }
}

#if(PLP_SUPPORT == 1)

ddr_code void plp_cap_trigger(esr_cond_judge_t *esr_cond_judge)
{
    if(plp_trigger){
        mod_timer(&plp_cap_trigger_timer,jiffies+HZ);
        return;
    }

	GetSensorTemp_check();

    esr_err_flags.b.iic_read_err = false;
	u8 strpg_cnt = 0;
	while(strpg_check(GPIO_PLP_STRPG_SHIFT))
	{
		strpg_cnt++;
		mdelay_plp(20);
		if(strpg_cnt >= 20)
		{
			break;
		}
	}

	if(plp_trigger){
        mod_timer(&plp_cap_trigger_timer,jiffies+HZ);
        return;
    }
    esr_err_flags.b.strpg = strpg_check(GPIO_PLP_STRPG_SHIFT);

    if(PLP_IC_SGM41664)
    {
        disp_apl_trace(LOG_INFO, 0xb336, "SGM enter trigger ");
        plp_iic_read_write(PLP_SLAVE_ID, Switching_Frequency_ADDR, NULL, 0x9, write);       
        plp_iic_read_write(PLP_SLAVE_ID, Switching_Frequency_ADDR, NULL, 0x29, write);
    }
    else if(PLP_IC_SYTC88_664)
    {    
        disp_apl_trace(LOG_INFO, 0xf73c, "SYTC88 664 enter trigger ");
        plp_iic_read_write(PLP_SLAVE_ID, FREQUENCY_CAPACITANCY_REG_ADDR, NULL, 0x9, write);    
        plp_iic_read_write(PLP_SLAVE_ID, FREQUENCY_CAPACITANCY_REG_ADDR, NULL, 0x29, write);
    }
    else
    {       
        disp_apl_trace(LOG_INFO, 0xc320, "SY72025 enter trigger ");
        plp_iic_read_write(PLP_SLAVE_ID, SF_controls_reg, NULL, 0x91, write);
        plp_iic_read_write(PLP_SLAVE_ID, SF_controls_reg, NULL, 0xB1, write);
    }
    plp_test_flag = true;
    esr_cond_judge->equal_start.all = 0;
    esr_cond_judge->cur_time.all = 0;
    esr_cond_judge->start_dis_time.all = 0;
    esr_cond_judge->time_l = 0;
    esr_cond_judge->time_h = 0;
    esr_cond_judge->time_tmp_l = 0;
    esr_cond_judge->time_tmp_h = 0;
    mod_timer(&plp_cap_trigger_timer,jiffies+ESR_CYCLE);

    /*if(esr_cond_judge->flag == 0)
        return;    */

    esr_cond_judge->start_time = get_tsc_lo();
	plp_first_check = 0;
    
    if((PLP_IC_SGM41664 == true) ||(PLP_IC_SYTC88_664 == true))
    {
        mod_timer(&plp_cap_check_timer,jiffies+HZ/10);
    }
    else{
        mod_timer(&plp_cap_check_timer,jiffies+HZ);
    }
}


ddr_code void plp_cap_check(bool *esr_status, esr_cond_judge_t *esr_cond_judge)
{
	GetSensorTemp_check();

	u16 esr_thr=~0;
    /*if(!esr_cond_judge->flag){
        esr_cond_judge->flag = 1;
        esr_cond_judge->start_time = get_tsc_lo();
    }*/
    //get_tsc_lo(void);
    //disp_apl_trace(LOG_ERR, 0, "enter check \n");
    if(plp_trigger)
    {
    	plp_test_flag = false;
	    return;
    }
    u8 ESRRegData;


    //check current discharge time
    if ((esr_cond_judge->time_l == 0) && (esr_cond_judge->time_h == 0)) {
        plp_iic_read_write(PLP_SLAVE_ID, DISCHARGE_TIME_L_REG_ADDR, &esr_cond_judge->time_l, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, DISCHARGE_TIME_H_REG_ADDR, &esr_cond_judge->time_h, 0, read);        
        //disp_apl_trace(LOG_DEBUG, 0, "time of discharge is 0x%x%x ",esr_cond_judge->time_h,esr_cond_judge->time_l);
        goto out;
    }
    else {
        plp_iic_read_write(PLP_SLAVE_ID, DISCHARGE_TIME_L_REG_ADDR, &esr_cond_judge->time_tmp_l, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, DISCHARGE_TIME_H_REG_ADDR, &esr_cond_judge->time_tmp_h, 0, read);       
        disp_apl_trace(LOG_ALW, 0x4f39, "discharge h 0x%x l 0x%x ",esr_cond_judge->time_tmp_h,esr_cond_judge->time_tmp_l);
        #if 0
        disp_apl_trace(LOG_ERR, 0x857f, "time:%x time_tmp:%x cur:%x-%x, equ:%x-%x\n",
        ((time_h<<8)+time_l),((time_tmp_h<<8)+time_tmp_l),cur_time.dw.h,cur_time.dw.l,equal_start.dw.h,equal_start.dw.l);
        #endif
        if ((esr_cond_judge->time_l == esr_cond_judge->time_tmp_l) && (esr_cond_judge->time_h == esr_cond_judge->time_tmp_h)) 
        {

                epm_FTL_t* epm_ftl_data;
                epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
                if((PLP_IC_SGM41664 == true) || (PLP_IC_SYTC88_664 == true) ||(epm_ftl_data->SGM_for_reboot == SGM_reboot_tag))
                {
                    esr_cond_judge->time = 2*(esr_cond_judge->time_l + (esr_cond_judge->time_h << 8));
                }
                else //SY72025
                {
                    esr_cond_judge->time = ((esr_cond_judge->time_l) >> 3) + ((esr_cond_judge->time_h) << 5);
                }
                goto success;
        }
        else {
            esr_cond_judge->time_l = esr_cond_judge->time_tmp_l;
            esr_cond_judge->time_h = esr_cond_judge->time_tmp_h;
            goto out;
        }
    }

out:
	if(plp_first_check)
	{
	    if (((esr_cond_judge->start_time < get_tsc_lo()) && (get_tsc_lo() - esr_cond_judge->start_time > (PLP_TIMEOUT*CYCLE_PER_MS*100))) ||
	        ((esr_cond_judge->start_time > get_tsc_lo()) && (INV_U32 - esr_cond_judge->start_time + get_tsc_lo() > (PLP_TIMEOUT*CYCLE_PER_MS*100))))
	    {
	        esr_cond_judge->time = 0;
	        disp_apl_trace(LOG_INFO, 0xc3ce, "plp test cap time out!!");
			plp_down_time = get_tsc_64();
	        *esr_status = false;
	        goto success;
	    }
	}
	plp_first_check = 1;
    if((PLP_IC_SGM41664 == true)||(PLP_IC_SYTC88_664 == true))
    {
        mod_timer(&plp_cap_check_timer,jiffies+HZ/10);
    }
    else{//sy72025
        mod_timer(&plp_cap_check_timer,jiffies+HZ);
    }    
    return;

success:
    plp_iic_read_write(PLP_SLAVE_ID, LOAD_SWITCH_REG_ADDR, &ESRRegData, 0, read);

#ifdef MDOT2_SUPPORT
	srb_t *srb = (srb_t *)SRAM_BASE; 
	if (srb->cap_idx == CAP_SIZE_4T)
    { //4T
    	esr_thr = ESR_THRESHOLD_3840;
	}
	if (srb->cap_idx == CAP_SIZE_2T)
    { //2T
    	esr_thr = ESR_THRESHOLD_1920;
	}
    else if(srb->cap_idx == CAP_SIZE_1T)
    { //1T
		esr_thr = ESR_THRESHOLD_960;
	}
    else if(srb->cap_idx == CAP_SIZE_512G)
    { //512g
		esr_thr = ESR_THRESHOLD_480;
	}   
#endif

	esr_err_flags.b.plp_cap_err = 0;
	esr_err_flags.b.esr_err = 0;
	esr_err_flags.b.plp_cap_err = (esr_cond_judge->time < esr_thr) ? true : false;
    //esr_err_flags.b.plp_cap_err = (esr_cond_judge->time < ESR_THRESHOLD) ? true : false;
    if(PLP_IC_SYTC88_664 == true)
    {
        esr_err_flags.b.esr_err = (ESRRegData & BIT0) ? true : false;
    }
	plp_down_time = get_tsc_64();
	//disp_apl_trace(LOG_INFO, 0, "plp test sown time:0x%x ",plp_down_time);
    plp_test_flag = false;
    disp_apl_trace(LOG_INFO, 0x53e8, "plp test esr status:0x%x time:%d STRPG_status:%d",esr_err_flags.all, esr_cond_judge->time,  esr_err_flags.b.strpg);
	if(esr_err_flags.all & 0xF)
	{
		flush_to_nand(EVT_PLP_IC_ERR);
	}
    cpu_msg_issue(2, CPU_MSG_CHK_GC, 0, 0);

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    //When the good plane number of spb1(slc spb) is less than 10, enter fua mode.
	u32 df_width = occupied_by(shr_nand_info.interleave, 8) << 3; 
	u32 df_byte = (PLP_SLC_BUFFER_BLK_ID * df_width) >> 3; 
    u8* ftl_df_ptr = gl_pt_defect_tbl + df_byte;
    u32 k;
    u32 good_plane_ttl_cnt = shr_nand_info.interleave;
    for(k = 0; k < df_byte; k++)
	{
	    if ((k == df_byte - 1) && (shr_nand_info.interleave%8 != 0))
        {
            u8 last_byte_pl = shr_nand_info.interleave%8;
            good_plane_ttl_cnt -= pop32(ftl_df_ptr[k] & ((1 << last_byte_pl) - 1));
        }
        else
        {
            good_plane_ttl_cnt -= pop32(ftl_df_ptr[k]);
        }
	}
    if (good_plane_ttl_cnt < 10)
    {
        // fua write
		esr_err_fua_flag = true;
        esr_err_flags.b.plp_cap_err = true;
    }

    disp_apl_trace(LOG_INFO, 0xfe44, "spb 1 good_plane_ttl_cnt:%u, esr_err_fua_flag:%u",good_plane_ttl_cnt, esr_err_fua_flag);
#endif

#if(BG_TRIM == ENABLE)
    chk_bg_trim();
#endif
}

slow_code void plp_out_of_work()
{
    //disp_apl_trace(LOG_ERR, 0, "plp out of work!!!");
    //read_only_flags.b.esr_err = 1;
	//if(cur_ro_status != RO_MD_IN)
	//{
		//ipc_enter_read_only_handle(false);
	//}
    flush_to_nand(EVT_READ_ONLY_MODE_IN);
	esr_err_fua_flag = true;
	
	#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	extern volatile u8 SLC_init;
	SLC_init = false;
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(epm_ftl_data->esr_lock_slc_block != true)
	{
		epm_ftl_data->esr_lock_slc_block = true;
		epm_update(FTL_sign, CPU_ID-1);
	}
	#endif
    disp_apl_trace(LOG_WARNING, 0x3016, "DISK enter FUA!!");
}

slow_code void _plp_cap_trigger(void *data)
{
    //disp_apl_trace(LOG_INFO, 0, "plp cap trigger start");
    plp_cap_trigger(&esr_cond_judge);
}

slow_code void _plp_cap_check(void *data)
{
    //disp_apl_trace(LOG_INFO, 0, "plp cap check start");
    plp_cap_check(&esr_status, &esr_cond_judge);
    if(esr_err_flags.all & 0xF)
    {
        if(!esr_err_flags.b.strpg_bak)
        {
            mod_timer(&plp_cap_trigger_timer,jiffies+HZ);// retry delay 1s
            esr_err_flags.b.strpg_bak = 1;
        }
        else
        {
            plp_out_of_work();
        }
    }
    else
    {
        esr_err_flags.b.strpg_bak = 0;
    }

}

fast_data_zi u32 loop_times;
fast_data_zi u8 loop_retry_cnt;

ddr_code void mtp_check_task(u32 param, u32 cap_type, u32 cmd_code)
{

#if MDOT2_SUPPORT
#if(PLP_SUPPORT == 1)

    epm_FTL_t* epm_ftl_data;
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    disp_apl_trace(LOG_INFO, 0x6963, "SGM  %d  0x%x",PLP_IC_SGM41664,epm_ftl_data->SGM_for_reboot);
    if((PLP_IC_SGM41664 == true) ||(epm_ftl_data->SGM_for_reboot == SGM_reboot_tag))
    {
        disp_apl_trace(LOG_INFO, 0xd9ff, "SGM reboot skip MTP %d tag 0x%x",PLP_IC_SGM41664,epm_ftl_data->SGM_for_reboot);
        return;
    }
#endif
#endif
	GetSensorTemp_check();

    u8 MTP_busy;
	//loop_times = 0; //infinite loop bug, 2023.6.27
    //disp_apl_trace(LOG_INFO, 0, "mtp_check_task");
     if (((check_time < get_tsc_lo()) && (get_tsc_lo() - check_time > (CYCLE_PER_MS*10))) || \
            ((check_time > get_tsc_lo()) && (INV_U32 - check_time + get_tsc_lo() > (CYCLE_PER_MS*10)))){
                loop_times++;
                I2C_read(cap_type, cmd_code, &MTP_busy);
                disp_apl_trace(LOG_INFO, 0x2f8e, "MTP_busy value 0x%x", MTP_busy);
                if(MTP_busy != 0x0 && loop_times < 10){
                    check_time = get_tsc_lo();
                    evt_set_cs(evt_mtp_check, cap_type, cmd_code, CS_TASK);
                }
                else if(loop_times >= 10){
                    disp_apl_trace(LOG_INFO, 0xde30, "MTP save time out, PLP ic MTP save again");
					loop_retry_cnt ++;
					if(loop_retry_cnt > 3)
					{
						esr_err_flags.b.iic_read_err = true;
       					disp_apl_trace(LOG_INFO, 0xdf8a, "I2C read retry err esr_err_flags %x",esr_err_flags.all);
        				flush_to_nand(EVT_PLP_IC_ERR);
						return;
					}                   
					else
					{
                    	if(plp_init(cap_type))
                    	{
                    		disp_apl_trace(LOG_INFO, 0x08ea, "PLP ic MTP has been saved");
		                    plp_cap_check_timer.function = _plp_cap_check;
		                    plp_cap_check_timer.data = NULL;
		                    plp_cap_trigger_timer.function = _plp_cap_trigger;
		                    plp_cap_trigger_timer.data = NULL;
		                    _plp_cap_trigger(NULL);
                    	}
					}                   
                }                
                else{
                    disp_apl_trace(LOG_INFO, 0xe1e3, "PLP ic MTP has been saved");
                    plp_cap_check_timer.function = _plp_cap_check;
                    plp_cap_check_timer.data = NULL;
                    plp_cap_trigger_timer.function = _plp_cap_trigger;
                    plp_cap_trigger_timer.data = NULL;
                    _plp_cap_trigger(NULL);
                }               
                //refresh_read_start_time = get_tsc_lo();
            }
     else{
        evt_set_cs(evt_mtp_check, cap_type, cmd_code, CS_TASK);
     }
}

#define PMIC_slave   0x25
#define PMIC_add   (PMIC_slave << 1)
#define write (0)
#define read  (1)

extern u32 I2C_read(u8 slaveID, u8 cmd_code, u8 *value);
extern u32 I2C_write(u8 slaveID, u8 cmd_code, u8 value);

ddr_code void pmic_print_init(void)
{
    u32 res;
    bool retry = true;
pmic_print_start:
    res = 0;
    u8 d_0,d_1,d_2,d_3,d_4,d_5,d_6,d_7,d_8,d_9,d_a,d_b,d_c,d_d,d_e,d_f,d_10;    
    d_0=d_1=d_2=d_3=d_4=d_5=d_6=d_7=d_8=d_9=d_a=d_b=d_c=d_d=d_e=d_f=d_10=0;
    res |= I2C_read(PMIC_add, 0x0, &d_0);
    res |= I2C_read(PMIC_add, 0x1, &d_1);
    res |= I2C_read(PMIC_add, 0x2, &d_2);
    res |= I2C_read(PMIC_add, 0x3, &d_3);
    res |= I2C_read(PMIC_add, 0x4, &d_4);
    res |= I2C_read(PMIC_add, 0x5, &d_5);
    res |= I2C_read(PMIC_add, 0x6, &d_6);
    res |= I2C_read(PMIC_add, 0x7, &d_7);
    res |= I2C_read(PMIC_add, 0x8, &d_8);
    res |= I2C_read(PMIC_add, 0x9, &d_9);
    res |= I2C_read(PMIC_add, 0xa, &d_a);
    res |= I2C_read(PMIC_add, 0xb, &d_b);
    res |= I2C_read(PMIC_add, 0xc, &d_c);
    res |= I2C_read(PMIC_add, 0xd, &d_d);
    res |= I2C_read(PMIC_add, 0xe, &d_e);
    res |= I2C_read(PMIC_add, 0xf, &d_f);
    res |= I2C_read(PMIC_add, 0x10, &d_10);
    disp_apl_trace(LOG_ALW, 0xa24e, "0 0x%x 1 0x%x 2 0x%x 3 0x%x 4 0x%x 5 0x%x", d_0,d_1,d_2,d_3,d_4,d_5);
    disp_apl_trace(LOG_ALW, 0x3313, "6 0x%x 7 0x%x 8 0x%x 9 0x%x a 0x%x b 0x%x", d_6,d_7,d_8,d_9,d_a,d_b);    
    disp_apl_trace(LOG_ALW, 0xc200, "c 0x%x d 0x%x e 0x%x f 0x%x 10 0x%x", d_c,d_d,d_e,d_f,d_10);  

    d_0=d_1=d_2=d_3=d_4=d_5=d_6=d_7=d_8=d_9=d_a=d_b=d_c=d_d=d_e=d_f=d_10=0;

    res |= I2C_read(PMIC_add, 0x11, &d_0);
    res |= I2C_read(PMIC_add, 0x12, &d_1);
    res |= I2C_read(PMIC_add, 0x13, &d_2);
    res |= I2C_read(PMIC_add, 0x14, &d_3);
    res |= I2C_read(PMIC_add, 0x15, &d_4);
    res |= I2C_read(PMIC_add, 0x16, &d_5);
    res |= I2C_read(PMIC_add, 0x17, &d_6);
    res |= I2C_read(PMIC_add, 0x18, &d_7);
    res |= I2C_read(PMIC_add, 0x19, &d_8);
    res |= I2C_read(PMIC_add, 0x1A, &d_9);
    res |= I2C_read(PMIC_add, 0x1B, &d_a);
    res |= I2C_read(PMIC_add, 0x1C, &d_b);
    res |= I2C_read(PMIC_add, 0x1D, &d_c);
    res |= I2C_read(PMIC_add, 0x1E, &d_d);
    res |= I2C_read(PMIC_add, 0x1F, &d_e);
    res |= I2C_read(PMIC_add, 0x20, &d_f);
    res |= I2C_read(PMIC_add, 0x21, &d_10);
    disp_apl_trace(LOG_ALW, 0x19c8, "11 0x%x 12 0x%x 13 0x%x 14 0x%x 15 0x%x 16 0x%x", d_0,d_1,d_2,d_3,d_4,d_5);
    disp_apl_trace(LOG_ALW, 0x797c, "17 0x%x 18 0x%x 19 0x%x 1A 0x%x 1B 0x%x 1C 0x%x", d_6,d_7,d_8,d_9,d_a,d_b);    
    disp_apl_trace(LOG_ALW, 0x9636, "1D 0x%x 1E 0x%x 1F 0x%x 20 0x%x 21 0x%x", d_c,d_d,d_e,d_f,d_10); 

    d_0=d_1=d_2=d_3=d_4=d_5=d_6=d_7=d_8=d_9=d_a=d_b=d_c=d_d=d_e=d_f=d_10=0;
    res |= I2C_read(PMIC_add, 0x22, &d_0);
    res |= I2C_read(PMIC_add, 0x23, &d_1);
    res |= I2C_read(PMIC_add, 0x24, &d_2);
    res |= I2C_read(PMIC_add, 0x25, &d_3);
    res |= I2C_read(PMIC_add, 0x26, &d_4);
    res |= I2C_read(PMIC_add, 0x27, &d_5);
    res |= I2C_read(PMIC_add, 0x28, &d_6);
    res |= I2C_read(PMIC_add, 0x29, &d_7);
    res |= I2C_read(PMIC_add, 0x2A, &d_8);
    res |= I2C_read(PMIC_add, 0x2B, &d_9);
    res |= I2C_read(PMIC_add, 0x2C, &d_a);
    res |= I2C_read(PMIC_add, 0x2D, &d_b);
    res |= I2C_read(PMIC_add, 0x2E, &d_c);
    res |= I2C_read(PMIC_add, 0x2F, &d_d);
    res |= I2C_read(PMIC_add, 0x30, &d_e);
    res |= I2C_read(PMIC_add, 0x31, &d_f);
    res |= I2C_read(PMIC_add, 0x32, &d_10);
    disp_apl_trace(LOG_ALW, 0x5b94, "22 0x%x 23 0x%x 24 0x%x 25 0x%x 26 0x%x 27 0x%x", d_0,d_1,d_2,d_3,d_4,d_5);
    disp_apl_trace(LOG_ALW, 0x3659, "28 0x%x 29 0x%x 2A 0x%x 2B 0x%x 2C 0x%x 2D 0x%x", d_6,d_7,d_8,d_9,d_a,d_b);    
    disp_apl_trace(LOG_ALW, 0x957d, "2E 0x%x 2F 0x%x 30 0x%x 31 0x%x 32 0x%x", d_c,d_d,d_e,d_f,d_10); 
    
    d_0=d_1=d_2=d_3=d_4=d_5=d_6=d_7=d_8=d_9=d_a=d_b=d_c=d_d=d_e=d_f=d_10=0;
    res |= I2C_read(PMIC_add, 0x33, &d_0);
    res |= I2C_read(PMIC_add, 0x34, &d_1);
    res |= I2C_read(PMIC_add, 0x35, &d_2);
    res |= I2C_read(PMIC_add, 0x36, &d_3);
    res |= I2C_read(PMIC_add, 0x37, &d_4);
    res |= I2C_read(PMIC_add, 0x38, &d_5);
    res |= I2C_read(PMIC_add, 0x39, &d_6);
    res |= I2C_read(PMIC_add, 0x3A, &d_7);
    res |= I2C_read(PMIC_add, 0x3B, &d_8);
    res |= I2C_read(PMIC_add, 0x3C, &d_9);
    res |= I2C_read(PMIC_add, 0x3D, &d_a);
    res |= I2C_read(PMIC_add, 0x3E, &d_b);
    res |= I2C_read(PMIC_add, 0xF0, &d_c);
    res |= I2C_read(PMIC_add, 0xF1, &d_d);

    disp_apl_trace(LOG_ALW, 0x6751, "33 0x%x 34 0x%x 35 0x%x 36 0x%x 37 0x%x 38 0x%x", d_0,d_1,d_2,d_3,d_4,d_5);
    disp_apl_trace(LOG_ALW, 0x0aa5, "39 0x%x 3A 0x%x 3B 0x%x 3C 0x%x 3D 0x%x 3E 0x%x", d_6,d_7,d_8,d_9,d_a,d_b);    
    disp_apl_trace(LOG_ALW, 0xd5cf, "F0 0x%x F1 0x%x", d_c,d_d); 
    if(res && retry){
		retry = false;
		goto pmic_print_start;
    }
}


ddr_code bool plp_init(u8 cap_type)  
{
	GetSensorTemp_check();  

#if MDOT2_SUPPORT
#if(PLP_SUPPORT == 1) 
    PLP_IC_SGM41664 = false; 
    PLP_IC_SYTC88_664 = false;

    u8 data_3,data_4,data_f,data_10;
    data_3=data_4=data_f=data_10 = 0;
    epm_FTL_t* epm_ftl_data;
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    plp_iic_read_write(PLP_SLAVE_ID, BUCK_OFF_Voltage_ADDR, &data_3, 0, read);
    plp_iic_read_write(PLP_SLAVE_ID, VDIS1_ADDR, &data_4, 0, read);
    plp_iic_read_write(PLP_SLAVE_ID, Interrupt_FLAG_ADDR, &data_f, 0, read);
    
    if( ((data_3 == 0x37)&&(data_4 == 0xA6)) ||(epm_ftl_data->SGM_for_reboot == SGM_reboot_tag))
    {
        PLP_IC_SGM41664 = true; 
        epm_ftl_data->SGM_for_reboot = SGM_reboot_tag;
        plp_iic_read_write(PLP_SLAVE_ID, Interrupt_FLAG_ADDR, &data_f, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, System_Control_ADDR, &data_10, 0, read);
        disp_apl_trace(LOG_INFO, 0xae6b, " SGM4166 FLAG 0x%x Sys_control 0x%x  data_3_4 0x%x",data_f,data_10,data_3<<8 | data_4);

        plp_iic_read_write(PLP_SLAVE_ID, VDIS1_ADDR, NULL, 0xB4, write);
        plp_iic_read_write(PLP_SLAVE_ID, VDIS2_ADDR, NULL, 0x9D, write);
        
        plp_iic_read_write(PLP_SLAVE_ID, BUCK_OFF_Voltage_ADDR, NULL, 0x44, write); //3.264V
        plp_iic_read_write(PLP_SLAVE_ID, DC_DC_CONVERTER_Control_ADDR, NULL, 0xC5, write);   
        return true;
    }
    else    
#endif
#endif    
    {        
    	u8 data, en_data;
        plp_iic_read_write(cap_type, LSC_PARAMETER_REG_ADDR, &en_data, 0, read);
    	extern AGING_TEST_MAP_t *MPIN;
        u8 plp_ic_type = MPIN->PLP_type;
        disp_apl_trace(LOG_INFO, 0x1252, "en_data 0x%x plp_ic_type 0x%x",en_data,plp_ic_type);
        
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)         
        if(plp_ic_type == 0x38)
        {
            PWRDIS_open_flag = true;
            if(en_data & BIT0){
                u8 u_data = 0x38;
                disp_apl_trace(LOG_INFO, 0x346f, "664 set default en_data 0x%x plp_ic_type 0x%x",en_data,plp_ic_type);
        	    plp_iic_read_write(cap_type, LSC_PARAMETER_REG_ADDR, NULL, u_data, write); //SYT664 set defult
            }
        }
        else
        {
            PWRDIS_open_flag = false;
        }
#endif
        if(data_f == 0x1)
        {
            plp_iic_read_write(cap_type, BUCK_OFF_reg, &data, 0 , read);
            if(data == 0x45) 
            {
                disp_apl_trace(LOG_INFO, 0x517e, " SY72025 MTP OK");
                return true;
            }
            disp_apl_trace(LOG_INFO, 0xe107, " SY72025 save MTP !");

        	plp_iic_read_write(cap_type, LSW_CTRL_reg, NULL, 0xBD, write); 
            plp_iic_read_write(cap_type, DCDC_CONVERTER_REG, NULL, 0xC5, write);
        	plp_iic_read_write(cap_type, BUCK_OFF_reg, NULL, 0x45, write);
        	plp_iic_read_write(cap_type, VDIS1_reg, NULL, 0xB4, write);
            plp_iic_read_write(cap_type, VDIS2_reg, NULL, 0x9D, write);
            plp_iic_read_write(cap_type, SF_controls_reg, NULL, 0x99, write);
            plp_iic_read_write(cap_type, STR_OVP_reg, NULL, 0x1C, write);
        }
        else
        {
            PLP_IC_SYTC88_664 = true;
            plp_iic_read_write(cap_type, LSC_PARAMETER_REG_ADDR, NULL, 0x39, write);
            plp_iic_read_write(cap_type, BUCK_OFF_VOL_REG_ADDR, &data, 0 , read);
            if(data == 0x43)
            {
                disp_apl_trace(LOG_INFO, 0x2367, "SYTC88 SYT664 MTP OK");
                return true;
            }
            disp_apl_trace(LOG_INFO, 0xebab, "SYTC88 SYT664 save MTP !");
            plp_iic_read_write(cap_type,DC_DC_CONVERTER_REG_ADDR, NULL, 0xC5, write);
            plp_iic_read_write(cap_type, BUCK_OFF_VOL_REG_ADDR, NULL, 0x43, write);
            plp_iic_read_write(cap_type, VOL_START_REG_ADDR, NULL, 0xB4, write);
            plp_iic_read_write(cap_type, VOL_END_REG_ADDR, NULL, 0x9D, write);
            plp_iic_read_write(cap_type, FREQUENCY_CAPACITANCY_REG_ADDR, NULL, 0x9, write);
        }

    	plp_iic_read_write(cap_type, MTP_reg, NULL, 0x1, write);
        
        check_time = get_tsc_lo();
        evt_set_cs(evt_mtp_check, cap_type, MTP_reg, CS_TASK);
        return 0;
    }
}


init_code void plp_one_time_init(void)
{
#if(PLP_SUPPORT == 1)
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if((epm_ftl_data->epm_sign != FTL_sign) || (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
	{
		pmic_print_init();
	}
#endif
    //init plp ic
	u8 data_0,data_1,data_2;
    data_0=data_1=data_2= 0;
    for(u8 i = 0;i < System_Control_ADDR; i+=3)
    {
        plp_iic_read_write(PLP_SLAVE_ID, i, &data_0, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, i+1, &data_1, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, i+2, &data_2, 0, read);
        disp_apl_trace(LOG_ALW, 0xf487, "0x%x  0x%x 0x%x 0x%x 0x%x 0x%x ", i,data_0,i+1,data_1,i+2,data_2);
    }
    bool save = plp_init(PLP_SLAVE_ID);

#if 0
    u8 iic_red_vel;
    u8 i;
    for(i = 0;i < 0x0a; i++)
    {
        I2C_read(PLP_SLAVE_ID, i, &iic_red_vel);
        disp_apl_trace(LOG_ERR, 0xf7a6, "plp %xH 0x%x \n",i,iic_red_vel);
    }
#endif

    plp_trigger = 0;
    #if PLP_DEBUG_GPIO
    gpio_set_gpio15(0);
    #endif

    if(save){
        disp_apl_trace(LOG_ERR, 0x88d6, "plp_one_time_init gpio value:%d\n", gpio_get_value(GPIO_PLP_STRPG_SHIFT));
        plp_cap_check_timer.function = _plp_cap_check;
        plp_cap_check_timer.data = NULL;
        plp_cap_trigger_timer.function = _plp_cap_trigger;
        plp_cap_trigger_timer.data = NULL;
		if(misc_is_warm_boot()==true){    //20210709 Johnny
		    mod_timer(&plp_cap_trigger_timer,jiffies+ESR_CYCLE);
		}
		else{
            plp_cap_trigger(&esr_cond_judge);
		}
    }
}

static ddr_code int plp_debug_main(int argc, char *argv[])
{
    plp_detect();
    return 0;
}

static DEFINE_UART_CMD(plp, "plp", "plp debug flow",
			"plp debug flow", 0, 0, plp_debug_main);

#endif
slow_code void plp_set_ENA(u32 param, u32 flag, u32 r1) //for plp power disable function
{
	GetSensorTemp_check();

	//disp_apl_trace(LOG_ALW, 0, "PLPIC ENA bit set to: %d", flag); //Eat log!!
	printk("\nPLPIC ENA bit set to: %d\n", flag);

	u8 data;
	if(flag == 1) data = 0x39; //ENA 0->1
	else data = 0x38; //ENA 1->0

	plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, NULL, data, write);

	//disp_apl_trace(LOG_ALW, 0, "PLPIC set ENA done");
	printk("\nPLPIC set ENA done\n");

}

ddr_code void plp_read_ENA() //for plp power disable function
{
	//GetSensorTemp_check();

	//disp_apl_trace(LOG_ALW, 0, "PLPIC ENA bit set to: %d", flag); //Eat log!!
	//printk("\nPLPIC ENA bit set to: %d\n", flag);

	u8 data;
	data = 0;
	plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, &data, 0, read);

	//disp_apl_trace(LOG_ALW, 0, "PLPIC set ENA done");
	printk("\nPLPIC read ENA done LSC_PARAMETER_REG_ADDR:01h = 0x%x\n", data);

}


ddr_code void ipc_plp_set_ENA(volatile cpu_msg_req_t *req){
	printk("\nIn ipc_plp_set_ENA\n");
	u32 flag = req->pl;
	plp_set_ENA(0, flag, 0);
}

#if(PLP_SUPPORT == 1)
static ddr_code int ena_main(int argc, char *argv[])
{
    u8 in = (u8)strtol(argv[1], (void *)0, 0);

	u8 data;
	data = 0;
	plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, &data, 0, read);
    extern AGING_TEST_MAP_t *MPIN;
    u8 plp_ic_type = MPIN->PLP_type;
    disp_apl_trace(LOG_ALW, 0xc584, "ena data 0x%x plp_ic_type 0x%x sgm %d", data,plp_ic_type,PLP_IC_SGM41664);

	u8 data_0,data_1,data_2;
    data_0=data_1=data_2= 0;

    for(u8 i = 0;i < System_Control_ADDR; i+=3)
    {
        plp_iic_read_write(PLP_SLAVE_ID, i, &data_0, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, i+1, &data_1, 0, read);
        plp_iic_read_write(PLP_SLAVE_ID, i+2, &data_2, 0, read);
        disp_apl_trace(LOG_ALW, 0xf449, "print-> 0x%x  0x%x 0x%x 0x%x 0x%x 0x%x ", i,data_0,i+1,data_1,i+2,data_2);
    } 

    if(in ==1)
    {
        data =0x39;
        plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, NULL, data, write);
    	plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, &data, 0, read);
        disp_apl_trace(LOG_ALW, 0x1e92, "write ena data 0x%x", data);
    }
    if(in ==2)
    {
        data =0x38;
        plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, NULL, data, write);
    	plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, &data, 0, read);
        disp_apl_trace(LOG_ALW, 0x7b61, "write ena data 0x%x", data);
    }
    if(in ==3)
    {   
        epm_FTL_t* epm_ftl_data;
        epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
        epm_ftl_data->SGM_for_reboot = INV_U32;        
        plp_iic_read_write(PLP_SLAVE_ID, BUCK_OFF_reg, NULL, 0x40, write);//for
        plp_iic_read_write(PLP_SLAVE_ID, MTP_reg, NULL, 0x1, write);
        disp_apl_trace(LOG_ALW, 0x62c6, "mtp for test");
    }

	return 0;
}

static DEFINE_UART_CMD(ena, "ena", "ena",
		"ena", 1, 1, ena_main);

#endif

/*
static fast_code int plp_test_main(int argc, char *argv[])
{
    u8 input_h = (u8)strtol(argv[1], (void *)0, 0);
    u8 input_l = (u8)strtol(argv[2], (void *)0, 0);

    plp_cap_trigger(&esr_cond_judge);
    #if 0
	u8 value = 0;
    u8 id = 0x59;
    id = (u8)strtol(argv[1], (void *)0, 0);

    while(I2C_read(id,0x01,&value));
    disp_apl_trace(LOG_ERR, 0, "reg 0x01 value:%x\n", value);

    while(I2C_read(id,0x02,&value));
    disp_apl_trace(LOG_ERR, 0, "reg 0x02 value:%x\n", value);

    while(I2C_read(id,0x03,&value));
    disp_apl_trace(LOG_ERR, 0, "reg 0x03 value:%x\n", value);

    while(I2C_read(id,0x04,&value));
    disp_apl_trace(LOG_ERR, 0, "reg 0x04 value:%x\n", value);

    while(I2C_read(id,0x05,&value));
    disp_apl_trace(LOG_ERR, 0, "reg 0x05 value:%x\n", value);
    #endif


	return 0;
}

static DEFINE_UART_CMD(plp_test3, "plp_test", "plp test1",
		"test plp function", 2, 2, plp_test_main);
*/
