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
/*! \file smart.c
 * @brief SMART/Health information log
 *
 * \addtogroup decoder
 * \defgroup smart
 * \ingroup decoder
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvmet.h"
#include "nvme_spec.h"
#include "misc.h"
#include "smart.h"
#include "cpu_msg.h"
#include "ipc_api.h"
#include "nvme_reg_access.h"
#include "btn_export.h"
#include "srb.h"
#include "l2p_mgr.h"
#if epm_enable
#include "console.h"
#include "mpc.h"
#endif
#include "epm.h"
#include "ftl_export.h"
#include "ssstc_cmd.h"
#include "fc_export.h"
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
#include "spin_lock.h"
#endif
/*! \cond PRIVATE */
#define __FILEID__ smart
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
extern struct nvmet_ctrlr *ctrlr;						///< controller context pointer
share_data_zi tencnet_smart_statistics_t *tx_smart_stat; ///< controller Tencent SMART statistics
fast_data_zi u32 calculated_poh;							///< power on time in jiffies
share_data_zi smart_statistics_t *smart_stat;
#if (Synology_case)
share_data_zi synology_smart_statistics_t *synology_smart_stat;
extern u32 host_unc_err_cnt;
#endif
#if CO_SUPPORT_OCP
share_data_zi ocp_smart_statistics_t *ocp_smart_stat;
#endif



fast_data_zi u64 ctrl_busy_time_sw_secs = 0; ///< sw controller busy time in seconds
fast_data_zi u64 host_rd_cmds_hw = 0;		 ///< hw host read commands count for validation
fast_data_zi u64 host_wr_cmds_hw = 0;		 ///< hw host write commands count for validation
share_data_zi volatile bool is_epm_vac_err = false;
fast_data_zi u32 less_1min;

#ifdef ERRHANDLE_GLIST						 //tony 20201016
share_data_zi volatile u32 *glist_valid_tag; //need define in Dram
#endif
#if PLP_SUPPORT == 0
extern struct timer_list smart_timer;
#endif
// DBG, SMARTVry
extern u32 shr_program_fail_count;
extern u32 shr_erase_fail_count;
extern u32 shr_die_fail_count;
extern u16 shr_E2E_RefTag_detection_count;
extern u16 shr_E2E_AppTag_detection_count;
extern u16 shr_E2E_GuardTag_detection_count;
extern u64 shr_nand_bytes_written;
extern u64 shr_unit_byte_nw;
extern volatile u32 GrowPhyDefectCnt; //for SMART growDef Use
#if RAID_SUPPORT_UECC
extern u32 nand_ecc_detection_cnt;   //host + internal 1bit fail detection cnt
extern u32 host_uecc_detection_cnt;  //host 1bit fail detection cnt
extern u32 internal_uecc_detection_cnt; //internal 1bit fail detection cnt
extern u32 uncorrectable_sector_count;  //host raid recovery fail cnt
extern u32 internal_rc_fail_cnt;  //internal raid recovery fail cnt
extern u32 host_prog_fail_cnt;
#endif
extern fast_data_zi corr_err_cnt_t corr_err_cnt;

extern ts_tmt_t ts_tmt;

ddr_sh_data u32 dcc_training_fail_cnt;
extern u32 shr_frontend_error_cnt;
extern u32 shr_backend_error_cnt;
extern u32 shr_ftl_error_cnt;
extern u32 shr_error_handle_cnt;
extern u32 shr_misc_error_cnt;
extern ftl_stat_t ftl_stat;
//for ftl data 
share_data volatile u32 resver_blk;



//-----------------------------------------------------------------------------
//  Function Definitions
//-----------------------------------------------------------------------------
ddr_code void smart_updt_rw_cmd_cnt(void) //fast_code joe
{
	host_rd_cmds_hw += (btn_get_rd_cmd_cnt() + btn_get_cp_cmd_cnt());
	host_wr_cmds_hw += btn_get_wr_cmd_cnt();
}

ddr_code u64 smart_cal_u64_secs_to_mins(u64 secs) //fast_code joe
{
	u32 hi;
	u32 lo;
	u64 cnt = 0;
	u64 ans = secs;
	u64 devider = 71582788; /* 2**32/60 */
	u64 remainder = 16;		/* 2**32%60 */

	hi = (ans >> 16) >> 16;
again:
	lo = ans & 0xFFFFFFFF;

	cnt += devider * hi;

	ans = lo + remainder * hi;
	hi = (ans >> 16) >> 16;
	if (hi)
		goto again;

	lo = ans & 0xFFFFFFFF;
	if (lo)
		cnt += lo / 60;

	return cnt;
}

ddr_code u64 smart_calcuate_dus_access(u64 count) //fast_code joe
{
	u32 hi;
	u32 lo;
	u64 cnt = 0;
	u64 ans = count;
	u64 devider = 4294967; /* 2**32/1000 */
	u64 remainder = 296;   /* 2**32%1000 */

	hi = (ans >> 16) >> 16;
again:
	lo = ans & 0xFFFFFFFF;

	cnt += devider * hi;

	ans = lo + remainder * hi;
	hi = (ans >> 16) >> 16;
	if (hi)
		goto again;

	lo = ans & 0xFFFFFFFF;
	if (lo)
		cnt += lo / 1000;

	if(lo%1000)
		cnt += 1;//for spec, 1 indicates the number from 1 to 1000

	return cnt;
}

slow_code void smart_update_power_on_time(void) //joe slow->ddr 20201124
{
	u32 t;

	t = ((jiffies / HZ) - calculated_poh) / 60;//1min

	if (t >= 1)
	{
		calculated_poh += t * 60;
		smart_stat->power_on_minutes += t;
	}
}

slow_code void smart_update_thermal_time(void) //joe slow->ddr 20201124
{
	u32 period[3];
	u32 tmt_sec[2];
	u32 tmt_cnt[2];

	ts_sec_time_get(period, tmt_cnt, tmt_sec);
	smart_stat->critial_composite_temperature_time += period[TS_SEC_CRITICAL];
	smart_stat->warning_composite_temperature_time += period[TS_SEC_WARNING];
	/*smart_stat->thermal_management_t1_total_time += tmt_sec[0];
	smart_stat->thermal_management_t1_trans_cnt += tmt_cnt[0];
	smart_stat->thermal_management_t2_total_time += tmt_sec[1];
	smart_stat->thermal_management_t2_trans_cnt += tmt_cnt[1];*/
}

slow_code void smart_inc_err_cnt(void) //joe fast-->slow 20200904
{
	smart_stat->num_error_info_log_entries++;
}
#if 0
slow_code void health_get_nvm_status(struct nvme_health_information_page *health) //joe fast-->slow 20200904
{
	extern __attribute__((weak)) void get_avg_erase_cnt(u32 * avg_erase, u32 * max_erase);
	extern __attribute__((weak)) void get_spare_cnt(u32 * avail, u32 * spare, u32 * thr);
	extern __attribute__((weak)) bool is_system_read_only(void);
	u32 avg_erase = 100;
	u32 max_erase = 1400;
	u32 avail = 100;
	u32 spare = 100;
	u32 thr = 10;
	u32 used;

	//if (get_spare_cnt)
	//	get_spare_cnt(&avail, &spare, &thr);

	health->available_spare = 100 * avail / spare;
	health->available_spare_threshold = thr;

	//if (get_avg_erase_cnt)
	//	get_avg_erase_cnt(&avg_erase, &max_erase);

	used = 100 * avg_erase / max_erase;
	if (used > 254)
		used = 255;

	health->percentage_used = (u8)used;

	if (health->available_spare < health->available_spare_threshold)
	{
		nvme_apl_trace(LOG_ERR, 0x5b64, "available spare(%d) < thr(%d)",
					   health->available_spare, health->available_spare_threshold);

		health->critical_warning.bits.available_spare = 1;
	}

	if (is_system_read_only)
		health->critical_warning.bits.read_only = is_system_read_only();
}
#endif

slow_code u32 smart_stat_get_ctrl_busy_time(void)
{
	dbl_busy_time_t dbl_busy_time = {.all = pf_readl(DBL_BUSY_TIME)};
	/*FW workaround fix busy time clk rate to 10ns, original default is 3ns*/
	return (dbl_busy_time.b.dbl_busy_time / 3);
}

ddr_code void smart_stat_init_ctrl_busy_time(u32 sys_clk)//init -> ddr
{
	dbl_busy_time_t dbl_busy_time = {.all = 0};
	dbl_busy_time.b.dbl_timer_en = 0;
	pf_writel(dbl_busy_time.all, DBL_BUSY_TIME);

	dbl_busy_time.b.dbl_timer_en = 1;
	// due to hw issue busy time will return to 0, fw workaround set rate = 7
	// dbl_busy_time.b.dbl_timer_clk_rate =
	// 	(sys_clk >=  1000 * 1000 * 1000) ? 0 :
	// 	(sys_clk >=  666 * 1000 * 1000) ? 1 :
	// 	(sys_clk >=  500 * 1000 * 1000) ? 2 :
	// 	(sys_clk >=  400 * 1000 * 1000) ? 3 :
	// 	(sys_clk >=  333 * 1000 * 1000) ? 4 :
	// 	(sys_clk >=  250 * 1000 * 1000) ? 5 :
	// 	(sys_clk >=  143 * 1000 * 1000) ? 6 : 7;
	dbl_busy_time.b.dbl_timer_clk_rate = 7;
	pf_writel(dbl_busy_time.all, DBL_BUSY_TIME);

	dbl_busy_time.all = pf_readl(DBL_BUSY_TIME);
	sys_assert(dbl_busy_time.b.dbl_busy_time == 0);
}

ddr_code void health_get_io_info(struct nvme_health_information_page *health)//joe slow->ddr 20201124
{
	btn_smart_io_t rio;
	btn_smart_io_t wio;

	btn_get_r_smart_io(&rio);
	btn_get_w_smart_io(&wio);

	smart_stat->data_units_read += rio.host_du_cnt;
	smart_stat->host_read_commands += rio.cmd_recv_cnt;

	smart_stat->data_units_written += wio.host_du_cnt;
	smart_stat->host_write_commands += wio.cmd_recv_cnt;

	/* read/write info */
	health->data_units_read[0] = smart_calcuate_dus_access(smart_stat->data_units_read);
	health->data_units_written[0] = smart_calcuate_dus_access(smart_stat->data_units_written);

	health->host_read_commands[0] = smart_stat->host_read_commands;
	health->host_write_commands[0] = smart_stat->host_write_commands;

	smart_stat->controller_busy_time += smart_stat_get_ctrl_busy_time();

	health->controller_busy_time[0] = smart_cal_u64_secs_to_mins(smart_stat->controller_busy_time);

	smart_updt_rw_cmd_cnt(); // only validation purpose, cmd counts may exist gap between fetching and clearing

	nvme_apl_trace(LOG_DEBUG, 0xef69, "data_units_written %d data_units_read %d", smart_stat->data_units_written, smart_stat->data_units_read);
	nvme_apl_trace(LOG_DEBUG, 0x5847, "host_write_commands HW/SW %d/%d host_read_commands HW/SW %d/%d", host_wr_cmds_hw, smart_stat->host_write_commands, host_rd_cmds_hw, smart_stat->host_read_commands);
	nvme_apl_trace(LOG_INFO, 0x906f, "controller_busy_time HW/SW %d/%d", smart_stat->controller_busy_time, ctrl_busy_time_sw_secs);
}

slow_code void health_get_power_info(struct nvme_health_information_page *health)//joe slow->ddr 20201124
{
	extern smart_statistics_t *smart_stat;
	//extern __attribute__((weak)) u64 get_spor_cnt(void);

	//POR+SPOR
	health->power_cycles[0] = smart_stat->power_cycles;

	smart_update_power_on_time();


	health->power_on_hours[0] = smart_stat->power_on_minutes / 60;


	//SPOR only
		health->unsafe_shutdowns[0] = smart_stat->unsafe_shutdowns;
}

slow_code void health_get_errors(struct nvme_health_information_page *health)//joe slow->ddr 20201124
{
	health->media_errors[0] = smart_stat->media_err[0];
	health->num_error_info_log_entries[0] = smart_stat->num_error_info_log_entries;
}

slow_code void health_get_temperature(struct nvme_health_information_page *health)//tony slow->ddr 20201230
{
	extern smart_statistics_t *smart_stat;
	// u32 i;
	//u16 max_temperature = 0;

	smart_update_thermal_time();

	health->warning_composite_temperature_time = smart_stat->warning_composite_temperature_time;
	// currTEMP >= WCTEMP and < CCTEMP time minutes
	health->critial_composite_temperature_time = smart_stat->critial_composite_temperature_time;
	// currTEMP >= CCTEMP time minutes

	// Only Three Sensor Now

	/*  020221 Shane Mark Off After Sensor Bypass Cut-in, Directly use smart_stat->temperature now 
	for (i = 0; i < MAX_TEMP_SENSOR; i++)
	{
		health->temperature_sensor[i] = smart_stat->temperature_sensor[i];
		if (health->temperature_sensor[i] > max_temperature && health->temperature_sensor[i] != 255+273)
		{
			max_temperature = health->temperature_sensor[i];
		}
	}
	for (i = MAX_TEMP_SENSOR; i < 8; i++)
	{
		health->temperature_sensor[i] = 0;
	}

	smart_stat->temperature = max_temperature; //ts_get() + 273;
	*/

	health->temperature_sensor[0] = health->temperature = smart_stat->temperature;

	smart_stat->temperature_sensor[0] = smart_stat->temperature;

	// wait for thermal handling
	health->thermal_management_t1_trans_cnt = smart_stat->thermal_management_t1_trans_cnt;
	health->thermal_management_t2_trans_cnt = smart_stat->thermal_management_t2_trans_cnt;

	//Number of  time for controller handling thermal
	health->thermal_management_t1_total_time = smart_stat->thermal_management_t1_total_time;
	health->thermal_management_t2_total_time = smart_stat->thermal_management_t2_total_time;
	
#if(degrade_mode == ENABLE)
	extern void cmd_proc_read_only_setting(u8 setting);
	extern read_only_t read_only_flags;
	extern u8  cur_ro_status;

	if( health->temperature > c_deg_to_k_deg(TS_DEFAULT_CRITICAL) )
	{	// > 85
		bool enterRO_flag = false;

		if( read_only_flags.b.high_temp == false )
		{
			read_only_flags.b.high_temp = true;
			smart_stat->critical_warning.bits.read_only = 1;
			smart_stat->critical_warning.bits.temperature = 1;
			nvme_apl_trace(LOG_ALW, 0x670c, "Set critical warning bit[3][1] because temp > 85");

			if(cur_ro_status != RO_MD_IN)
			{
				enterRO_flag = true;
			}
		}

		if( enterRO_flag == true ) {
			extern void cmd_proc_read_only_setting(u8 setting);
			cmd_proc_read_only_setting(true);
			flush_to_nand(EVT_READ_ONLY_MODE_IN);
		}

	}else
	{	// <= 85
		bool leaveRO_flag = false;

		if( read_only_flags.b.high_temp == true )
		{
			read_only_flags.b.high_temp = false;
			nvme_apl_trace(LOG_ALW, 0x9947, "clear critical warning bit[3] because temp <= 85");

			if( read_only_flags.all == 0 )
			{
				smart_stat->critical_warning.bits.read_only = 0;
				leaveRO_flag = true;
			}
		}

		if( leaveRO_flag == true ) {
			extern void cmd_proc_read_only_setting(u8 setting);
			cmd_proc_read_only_setting(false);
			flush_to_nand(EVT_READ_ONLY_MODE_OUT);
		}

		if ( smart_stat->critical_warning.bits.temperature )
		{
			if ( health->temperature < (ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH] - 5) )
			{
				smart_stat->critical_warning.bits.temperature = 0;
				nvme_apl_trace(LOG_ALW, 0x39f1, "clear critical warning bit[1]");
			}
		}
	}

	if ((health->temperature <= ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH]) || (health->temperature > ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]))
	{
		nvme_apl_trace(LOG_ALW, 0xf9cc, "temp %d, (%d %d)",
						health->temperature,
						ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH],
						ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]);
		smart_stat->critical_warning.bits.temperature = 1;
		nvme_apl_trace(LOG_ALW, 0xd96d, "set critical warning bit[1] because touch threshold");
	}

	// Smart use one temp sersor now
	// for (i = 0; i < SENSOR_IN_SMART; i++)
	// {
	// 	if( health->temperature_sensor[i] > c_deg_to_k_deg(TS_DEFAULT_CRITICAL) )
	// 	{	// > 85
	// 		bool enterRO_flag = false;

	// 		if( read_only_flags.b.high_temp == false )
	// 		{
	// 			read_only_flags.b.high_temp = true;
	// 			smart_stat->critical_warning.bits.read_only = 1;
	// 			smart_stat->critical_warning.bits.temperature = 1;
	// 			nvme_apl_trace(LOG_ALW, 0x670c, "Set critical warning bit[3][1] because temp > 85");

	// 			if(cur_ro_status != RO_MD_IN)
	// 			{
	// 				enterRO_flag = true;
	// 			}
	// 		}

	// 		if( enterRO_flag == true ) {
	// 			extern void cmd_proc_read_only_setting(u8 setting);
	// 			cmd_proc_read_only_setting(true);
	// 			flush_to_nand(EVT_READ_ONLY_MODE_IN);
	// 		}

	// 	}else
	// 	{	// <= 85
	// 		bool leaveRO_flag = false;

	// 		if( read_only_flags.b.high_temp == true )
	// 		{
	// 			read_only_flags.b.high_temp = false;
	// 			nvme_apl_trace(LOG_ALW, 0x9947, "clear critical warning bit[3] because temp <= 85");

	// 			if( read_only_flags.all == 0 )
	// 			{
	// 				smart_stat->critical_warning.bits.read_only = 0;
	// 				leaveRO_flag = true;
	// 			}
	// 		}

	// 		if( leaveRO_flag == true ) {
	// 			extern void cmd_proc_read_only_setting(u8 setting);
	// 			cmd_proc_read_only_setting(false);
	// 			flush_to_nand(EVT_READ_ONLY_MODE_OUT);
	// 		}

	// 		if ( smart_stat->critical_warning.bits.temperature )
	// 		{
	// 			if ( health->temperature_sensor[i] < temp_65C )
	// 			{
	// 				smart_stat->critical_warning.bits.temperature = 0;
	// 				nvme_apl_trace(LOG_ALW, 0x39f1, "clear critical warning bit[1]");
	// 			}
	// 		}
	// 	}

	// 	if ((health->temperature_sensor[i] <= ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH]) || (health->temperature_sensor[i] > ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]))
	// 	{
	// 		nvme_apl_trace(LOG_ALW, 0xf9cc, "temp %d, (%d %d)",
	// 						health->temperature,
	// 						ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH],
	// 						ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]);
	// 		smart_stat->critical_warning.bits.temperature = 1;
	// 		nvme_apl_trace(LOG_ALW, 0xd96d, "set critical warning bit[1] because touch threshold");
	// 	}
	// }
#else
	if ((health->temperature <= ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH]) ||
		(health->temperature >= ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]))
	{
		nvme_apl_trace(LOG_ALW, 0x26bb, "temp %d, (%d %d)",
						health->temperature,
						ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH],
						ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]);
		health->critical_warning.bits.temperature = 1;
	}
	
	/* three sensor now */
	for (i = 0; i < SENSOR_IN_SMART; i++)
	{
		if ((health->temperature_sensor[i] <= ctrlr->cur_feat.temp_feat.tmpth[i][UNDER_TH]) ||
			(health->temperature_sensor[i] >= ctrlr->cur_feat.temp_feat.tmpth[i][OVER_TH]))
		{
			nvme_apl_trace(LOG_ALW, 0x48ef, "temp[%d] %d, (%d %d)", i,
							health->temperature_sensor[i],
							ctrlr->cur_feat.temp_feat.tmpth[i][UNDER_TH],
							ctrlr->cur_feat.temp_feat.tmpth[i][OVER_TH]);
			health->critical_warning.bits.temperature = 1;
		}
	}
#endif
}

ddr_code void health_get_gc_count(struct nvme_additional_health_information_page *health)
{
	//extern u32 wl_cnt;
	//extern u32 rd_cnt;
	//extern u32 dr_cnt;
	//extern u32 gc_cnt;
	extern tencnet_smart_statistics_t *tx_smart_stat;

	//tx_smart_stat->wl_exec_count[0] = wl_cnt;
	//tx_smart_stat->wl_exec_count[1] = wl_cnt>>16;
	//tx_smart_stat->rd_count[0] = rd_cnt;
	//tx_smart_stat->rd_count[1] = rd_cnt>>16;
	//tx_smart_stat->dr_count[0] = dr_cnt;
	//tx_smart_stat->dr_count[1] = dr_cnt>>16;
	//tx_smart_stat->gc_count[0] = gc_cnt;
	//tx_smart_stat->gc_count[1] = gc_cnt>>16;

	health->wear_leveling_exec_count = 0xEA; //undefined
	health->wear_leveling_exec_count_normalized_value = 100; //Always 100
	health->read_disturb_count = 0xEB; //undefined
	health->read_disturb_count_normalized_value = 100; //Always 100
	health->data_retention_count = 0xEC; //undefined
	health->data_retention_count_normalized_value = 100; //Always 100
	health->gc_error_count = 0xED; //undefined
	health->gc_error_count_normalized_value = 100; //Always 100
	health->wear_leveling_exec_count_current_value 	= tx_smart_stat->wl_exec_count;
	//health->wear_leveling_exec_count_current_value[1] 	= tx_smart_stat->wl_exec_count[1];
	health->read_disturb_count_current_value 	= tx_smart_stat->rd_count; 
	//health->read_disturb_count_current_value[1] 	= tx_smart_stat->rd_count[1]; 
	health->data_retention_count_current_value 	= tx_smart_stat->dr_count;
	//health->data_retention_count_current_value[1] 	= tx_smart_stat->dr_count[1];
	health->gc_error_count_current_value = tx_smart_stat->gc_count; 
	//health->gc_error_count_current_value[1] = tx_smart_stat->gc_count[1];
}

ddr_code void health_get_inflight_command(struct nvme_additional_health_information_page *health)
{
	//0:Read, 1:Write, 2:Admin
	extern tencnet_smart_statistics_t *tx_smart_stat;
	
	btn_smart_io_t rio;
	btn_smart_io_t wio;

	btn_get_r_smart_io(&rio);
	btn_get_w_smart_io(&wio);

	//Can not delete this section, otherwise the value stored in smart_stat will be undercounted
	smart_stat->data_units_read += rio.host_du_cnt;
	smart_stat->host_read_commands += rio.cmd_recv_cnt;
	smart_stat->data_units_written += wio.host_du_cnt;
	smart_stat->host_write_commands += wio.cmd_recv_cnt;

	tx_smart_stat->inflight_cmd[0] = (u16)rio.running_cmd;//Inflight Read Commands
	tx_smart_stat->inflight_cmd[1] = (u16)wio.running_cmd;//Inflight Write Commands
	tx_smart_stat->inflight_cmd[2] = (u16)ctrlr->admin_running_cmds;//Inflight Admin Commands

	health->inflight_command_count = 0xFA; //undefined
	health->inflight_command_count_normalized_value = 100; //Always 100
	health->inflight_command_count_current_value[0] = tx_smart_stat->inflight_cmd[0];
	health->inflight_command_count_current_value[1] = tx_smart_stat->inflight_cmd[1];
	health->inflight_command_count_current_value[2] = tx_smart_stat->inflight_cmd[2];
}

ddr_code void health_get_hcrc_detection_count(struct nvme_additional_health_information_page *health)
{
	extern tencnet_smart_statistics_t *tx_smart_stat;
	
	health->hcrc_detection_count = 0xFB; //undefined
	health->hcrc_detection_count_normalized_value = 100; //Always 100
	health->hcrc_detection_count_current_value[0] = tx_smart_stat->hcrc_error_count[0];//Read HCRC Count
	health->hcrc_detection_count_current_value[1] = tx_smart_stat->hcrc_error_count[1];//Write HCRC Count
	health->hcrc_detection_count_current_value[2] = 0; //Reserved
}

init_code void nvmet_restore_smart_stat(struct _smart_statistics_t *smart)
{
	// if (smart == NULL)
	// 	stat_restore(&smart_stat->head, NULL, sizeof(*smart));
	// else
	// 	stat_restore(&smart_stat->head, &smart->head, sizeof(*smart));

	// ctrlr->elog_tot = smart_stat->num_error_info_log_entries;

	extern Ec_Table* EcTbl;
	extern epm_info_t *shr_epm_info;
	extern commit_ca3 *commit_ca3_fe;	
// #if CO_SUPPORT_DEVICE_SELF_TEST
	extern tDST_LOG *smDSTInfo;
// #endif
	extern is_IOQ *is_IOQ_ever_create_or_not;
	extern bool _fg_warm_boot;
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	smart_stat = (smart_statistics_t *)epm_smart_data->smart_save;
	if(!_fg_warm_boot){
		smart_stat->temperature = 30 + 273;
	}
	tx_smart_stat = (tencnet_smart_statistics_t *)epm_smart_data->ex_smart_save;
#if (Synology_case)	
	if( synology_smart_stat == 0 )
	{
		synology_smart_stat = (synology_smart_statistics_t *)epm_smart_data->synology_smart_save;
	}
#endif
#if CO_SUPPORT_OCP
	ocp_smart_stat = (ocp_smart_statistics_t *)epm_smart_data->ocp_smart_save;
#endif
	commit_ca3_fe = (commit_ca3*)epm_smart_data->commit_ca3_fe;
	smDSTInfo = (tDST_LOG*)epm_smart_data->LogDST;
	is_IOQ_ever_create_or_not = (is_IOQ*)&epm_smart_data->is_IOQ_ever_create_or_not;
#ifdef SMART_PLP_NOT_DONE     
	u32 *init_plp_not_flag = &(epm_smart_data->init_plp_not_flag);
#endif
	evlog_printk(LOG_INFO,"flag 0x%d fe:0x%x IOQ_flag %d", commit_ca3_fe->flag, commit_ca3_fe->fe, *is_IOQ_ever_create_or_not);
	//nvme_apl_trace(LOG_INFO, "[DST] Addr|%x", smDSTInfo);
	//memcpy(&smart_stat, epm_smart_data->smart_save, sizeof(struct _smart_statistics_t));
	//memcpy(&tx_smart_stat, epm_smart_data->ex_smart_save, sizeof(struct _tencnet_smart_statistics_t));
	
	poh = smart_stat->power_on_minutes / 60;
    pom_per_ms = (smart_stat->power_on_minutes % 60) * 600; //power_on_minutes change to ms
	nvme_apl_trace(LOG_INFO, 0x4646, "poh[%d] pom_per_ms[%d]", poh, pom_per_ms);

	if(misc_is_warm_boot() != true)//for fw update keep power_cycle cont
	{
		smart_stat_init();
		smart_stat->power_cycles++;
	}

#ifndef SMART_PLP_NOT_DONE
	smart_stat->critical_warning.raw = 0;
#else
	if(init_plp_not_flag[0] == 0x89ABCDEF)
		smart_stat->critical_warning.raw = smart_stat->critical_warning.raw & 0x7F;//plp not done set all the time
	else if(init_plp_not_flag[0] == 0xFEDABD21)
		smart_stat->critical_warning.raw = smart_stat->critical_warning.raw & 0x3F;
	else{
		init_plp_not_flag[0] = 0x89ABCDEF;
		smart_stat->critical_warning.raw = smart_stat->critical_warning.raw & 0x3F;
	}
#endif


	pc_cnt = smart_stat->power_cycles;
	
	nvme_apl_trace(LOG_INFO, 0xb43a, "cur power cycles:0x%08x", pc_cnt);
	
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	spin_lock_take(SPIN_LOCK_KEY_JOURNAL, 0, true); //for edevx
	journal_update(JNL_TAG_POWER_CYCLE, 0);
	epm_smart_data->telemetry_update_ctrlr_signal = 1;
	spin_lock_release(SPIN_LOCK_KEY_JOURNAL);
#endif

	epm_smart_data->dcc_training_fail_cnt += dcc_training_fail_cnt; //20201130-Ma
																	//printk("[Ma test]DCC total fail cnt = %d",dcc_training_fail_cnt);
	// TBD, init 0 if EPM SMART is unavailable (panic now), DBG, SMARTVry
	// Need not restore, CPU1 access var, should be ok to clear directly in ipc_get_additional_smart_info_done
    //shr_end_to_end_detection_count 	= tx_smart_stat->end_to_end_detection_count[1] << 16 | tx_smart_stat->end_to_end_detection_count[0];
	//corr_err_cnt.bad_tlp_cnt 		= tx_smart_stat->crc_error_count[1] << 16 | tx_smart_stat->crc_error_count[0];

	// Need not restore, rescan from GList every time get addtional SMART.
	//shr_die_fail_count	 = tx_smart_stat->die_fail_count[1] << 16 | tx_smart_stat->die_fail_count[0];																	

	//shr_program_fail_count 		= tx_smart_stat->program_fail_count[1] << 16 | tx_smart_stat->program_fail_count[0];	
	//shr_erase_fail_count   		= tx_smart_stat->erase_fail_count[1] << 16 | tx_smart_stat->erase_fail_count[0];
	//shr_nand_bytes_written		= tx_smart_stat->nand_bytes_written[1] << 16 | tx_smart_stat->nand_bytes_written[0];
	shr_nand_bytes_written	= (u64)epm_smart_data->hi_epm_nand_bytes_written << 32 | epm_smart_data->lo_epm_nand_bytes_written;
	//GrowPhyDefectCnt			= tx_smart_stat->bad_block_failure_rate[1] << 16 | tx_smart_stat->bad_block_failure_rate[0];

	// May record glist count already.
	#if RAID_SUPPORT_UECC
	//uncorrectable_sector_count	+= tx_smart_stat->uncorrectable_sector_count[1] << 16 | tx_smart_stat->uncorrectable_sector_count[0];
	//nand_ecc_detection_cnt 		+= (tx_smart_stat->nand_ecc_detection_count[1] << 16 | tx_smart_stat->nand_ecc_detection_count[0]);
	//internal_rc_fail_cnt 		+= (tx_smart_stat->raid_recovery_fail_count[1] << 16 | tx_smart_stat->raid_recovery_fail_count[0]);
	//tx_smart_stat->host_uecc_detection_cnt		= (tx_smart_stat->reallocated_sector_count[1] << 16 | tx_smart_stat->reallocated_sector_count[0]) - tx_smart_stat->program_fail_count;	// TBD, other method? Used to get host_uecc_detection_cnt
	internal_uecc_detection_cnt	= tx_smart_stat->nand_ecc_detection_count - tx_smart_stat->host_uecc_detection_cnt;	// TBD, other method? Used to get host_uecc_detection_cnt
	EcTbl->header.MinEC = tx_smart_stat->wear_levelng_count[0];
	nvme_apl_trace(LOG_INFO, 0xf050, "[DBG] (shr min/tx min)[%d/%d]", EcTbl->header.MinEC, tx_smart_stat->wear_levelng_count[0]);

	// DBG, SMARTVry
	nvme_apl_trace(LOG_INFO, 0x3e53, "[DBG] Rd(TotDet)[%d] Hst(Det/Unc)[%d/%d] Int(Det/Unc)[%d/%d]", \
		tx_smart_stat->nand_ecc_detection_count, tx_smart_stat->host_uecc_detection_cnt, tx_smart_stat->uncorrectable_sector_count, internal_uecc_detection_cnt, tx_smart_stat->raid_recovery_fail_count);
    #endif
	nvme_apl_trace(LOG_INFO, 0x012c, "[DBG] (Pg/Er)[%d/%d]", tx_smart_stat->program_fail_count, tx_smart_stat->erase_fail_count);

	error_log_t error_log;
	memcpy(&error_log, epm_smart_data->error_log, sizeof(struct _errr_log_t));
	shr_frontend_error_cnt = error_log.frontend_error_cnt;
	shr_backend_error_cnt = error_log.backend_error_cnt;
	shr_ftl_error_cnt = error_log.ftl_error_cnt;
	shr_error_handle_cnt = error_log.error_handle_cnt;
	shr_misc_error_cnt = error_log.misc_error_cnt;
}

// DBG, SMARTVry
ddr_code void nvmet_update_txsmart_stat(void)
{
	extern epm_info_t *shr_epm_info;
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	extern Ec_Table* EcTbl;
	//tx_smart_stat->program_fail_count[0] = shr_program_fail_count;
	//tx_smart_stat->program_fail_count[1] = shr_program_fail_count >> 16;

	//tx_smart_stat->erase_fail_count[0] = shr_erase_fail_count;
	//tx_smart_stat->erase_fail_count[1] = shr_erase_fail_count >> 16;

	//tx_smart_stat->end_to_end_detection_count[0] += shr_E2E_GuardTag_detection_count;
	//tx_smart_stat->end_to_end_detection_count[1] += shr_E2E_AppTag_detection_count;
	//tx_smart_stat->end_to_end_detection_count[2] += shr_E2E_RefTag_detection_count;
	//shr_E2E_GuardTag_detection_count= 0;	// CPU1 access var, should be ok to clear directly, DBG, SMARTVry
	//shr_E2E_AppTag_detection_count	= 0;
	//shr_E2E_RefTag_detection_count	= 0;

	// Handle carry in
	//if ((tx_smart_stat->crc_error_count[0] + (corr_err_cnt.bad_tlp_cnt & 0xFFFF)) > 0xFFFF)
	//	tx_smart_stat->crc_error_count[1] += 1;
	//tx_smart_stat->crc_error_count[0] += (u16)corr_err_cnt.bad_tlp_cnt;
	//tx_smart_stat->crc_error_count[1] += (u16)(corr_err_cnt.bad_tlp_cnt >> 16);
	//corr_err_cnt.bad_tlp_cnt = 0;	// CPU1 access var, should be ok to clear directly, DBG, SMARTVry

	//tx_smart_stat->nand_bytes_written[0] = shr_nand_bytes_written;
	//tx_smart_stat->nand_bytes_written[1] = shr_nand_bytes_written >> 16;
	shr_unit_byte_nw = shr_nand_bytes_written * 1024 * 1024;
	tx_smart_stat->nand_bytes_written[0] = shr_unit_byte_nw>>ctz(0x100000);
	tx_smart_stat->nand_bytes_written[1] = (shr_unit_byte_nw>>ctz(0x100000))>>16;
	tx_smart_stat->nand_bytes_written[2] = (shr_unit_byte_nw>>ctz(0x100000))>>32;
	epm_smart_data->hi_epm_nand_bytes_written = shr_nand_bytes_written >> 32;
	epm_smart_data->lo_epm_nand_bytes_written = shr_nand_bytes_written & 0xFFFFFFFF;
	
	btn_smart_io_t wio;
	btn_get_w_smart_io(&wio);
	//smart_stat->data_units_written += wio.host_du_cnt;
	//smart_stat->host_write_commands += wio.cmd_recv_cnt;
	tx_smart_stat->host_bytes_written[0] = (smart_stat->data_units_written)>>ctz(0x10000);	// 512 Bytes to Unit is 32MB
	tx_smart_stat->host_bytes_written[1] = (smart_stat->data_units_written)>>ctz(0x10000)>>16;
	tx_smart_stat->host_bytes_written[2] = (smart_stat->data_units_written)>>ctz(0x10000)>>32;
	
	#if RAID_SUPPORT_UECC	
	tx_smart_stat->reallocated_sector_count = tx_smart_stat->program_fail_count + tx_smart_stat->host_uecc_detection_cnt;    //host cnt (read+prog)
	//tx_smart_stat->reallocated_sector_count[0] = nand_reallocated_sector_cnt;              
	//tx_smart_stat->reallocated_sector_count[1] = nand_reallocated_sector_cnt>>16;
	
	//tx_smart_stat->uncorrectable_sector_count[0] = uncorrectable_sector_count;
	//tx_smart_stat->uncorrectable_sector_count[1] = uncorrectable_sector_count>>16;
	
	//tx_smart_stat->nand_ecc_detection_count[0] = nand_ecc_detection_cnt;
	//tx_smart_stat->nand_ecc_detection_count[1] = nand_ecc_detection_cnt>>16;
	
	tx_smart_stat->nand_ecc_correction_count = tx_smart_stat->reallocated_sector_count - tx_smart_stat->uncorrectable_sector_count;
	//tx_smart_stat->nand_ecc_correction_count[0] = nand_ecc_correction_cnt;
	//tx_smart_stat->nand_ecc_correction_count[1] = nand_ecc_correction_cnt>>16;
	
	//tx_smart_stat->raid_recovery_fail_count[0] = internal_rc_fail_cnt; 
	//tx_smart_stat->raid_recovery_fail_count[1] = internal_rc_fail_cnt>>16;
	#endif
	
	//tx_smart_stat->bad_block_failure_rate[0] = GrowPhyDefectCnt;
	//tx_smart_stat->bad_block_failure_rate[1] = GrowPhyDefectCnt>>16;
	
	//tx_smart_stat->die_fail_count[0] = shr_die_fail_count;
	//tx_smart_stat->die_fail_count[1] = shr_die_fail_count >> 16;

	// Handle GC count part
	//extern u32 wl_cnt;
	//extern u32 rd_cnt;
	//extern u32 dr_cnt;
	//extern u32 gc_cnt;
	//tx_smart_stat->wl_exec_count[0] = wl_cnt;
	//tx_smart_stat->wl_exec_count[1] = wl_cnt>>16;
	//tx_smart_stat->rd_count[0] = rd_cnt;
	//tx_smart_stat->rd_count[1] = rd_cnt>>16;
	//tx_smart_stat->dr_count[0] = dr_cnt;
	//tx_smart_stat->dr_count[1] = dr_cnt>>16;
	//tx_smart_stat->gc_count[0] = gc_cnt;
	//tx_smart_stat->gc_count[1] = gc_cnt>>16;
        
	// Need not update inflight cmd into EPM
}

#if (Synology_case)
ddr_code void nvmet_update_synology_smart_stat(void){

	extern smart_statistics_t *smart_stat;
	extern synology_smart_statistics_t *synology_smart_stat;
	
	/*Host UNC Error Count*/
	synology_smart_stat->host_UNC_error_cnt += host_unc_err_cnt;
	host_unc_err_cnt = 0;

	/*NAND Write sector*/
	shr_unit_byte_nw = shr_nand_bytes_written * 1024 *1024;
	synology_smart_stat->nand_write_sector[0] = (shr_unit_byte_nw >> 4);	//change to 512bytes unit
	synology_smart_stat->nand_write_sector[1] = ((shr_unit_byte_nw >> 4)) >> 16;
	synology_smart_stat->nand_write_sector[2] = ((shr_unit_byte_nw >> 4)) >> 32;
	synology_smart_stat->nand_write_sector[3] = ((shr_unit_byte_nw >> 4)) >> 48;
	
	
	/*Host Write Sector*/
	btn_smart_io_t wio;
	btn_get_w_smart_io(&wio);
	smart_stat->data_units_written += wio.host_du_cnt;
	synology_smart_stat->host_write_sector[0] = (smart_stat->data_units_written);
	synology_smart_stat->host_write_sector[1] = (smart_stat->data_units_written) >> 16;
	synology_smart_stat->host_write_sector[2] = (smart_stat->data_units_written) >> 32;
	synology_smart_stat->host_write_sector[3] = (smart_stat->data_units_written) >> 48;
	
}
#endif

#if CO_SUPPORT_OCP
ddr_code void nvmet_update_ocp_smart_stat(void){

	extern smart_statistics_t *smart_stat;
	extern ocp_smart_statistics_t *ocp_smart_stat;

	ocp_smart_stat->thermal_throttling_status_and_cnt[0] = ts_tmt.gear;
	ocp_smart_stat->thermal_throttling_status_and_cnt[1] = smart_stat->thermal_management_t1_trans_cnt + smart_stat->thermal_management_t2_trans_cnt;

	ocp_smart_stat->uncorrect_read_err_cnt = tx_smart_stat->uncorrectable_sector_count;

}
#endif

#if 0//RAID_SUPPORT_UECC
ddr_code void ipc_nvmet_update_smart_stat(volatile cpu_msg_req_t *req)
{
	nvmet_update_smart_stat(NULL);
	//nvme_apl_trace(LOG_INFO, 0, "[DBG] Update IPC SMART info");
}
#endif

#if PLP_SUPPORT == 0
ddr_code void nvmet_update_smart_timer(void *data)
{
	nvmet_update_smart_stat(NULL);
	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_UPDATE_SPARE_AVG_ERASE_CNT, 0, 1);
	mod_timer(&smart_timer, jiffies + 600*HZ);	//10 mins
}
#endif

ddr_code void nvmet_update_smart_stat(struct _smart_statistics_t *smart)
{
	extern volatile u8 plp_trigger;
	extern bool _fg_warm_boot;
	extern bool set_hctm_flag;
	u8 show_log = !plp_trigger;
	// should be called when flush
	// u32 i;
	// u32 used;
	extern epm_info_t *shr_epm_info;
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);

	// Update Power Info
	smart_update_power_on_time();

	// Update NVMe Status
	// struct health_ipc* hlt = sys_malloc(SLOW_DATA, sizeof(health_ipc));
	// cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_SPARE_AVG_ERASE_CNT, 0, (u32)hlt);
	// smart_stat->available_spare = 100 * hlt->avail / hlt->spare;
	// smart_stat->available_spare_threshold =hlt->thr;
	// used = 100 * hlt->avg_erase / hlt->max_erase;
	// if (used > 254)
	// 	used = 255;
	// smart_stat->percentage_used = (u8)used;

	// if (smart_stat->available_spare < smart_stat->available_spare_threshold)
	// {
	// 	nvme_apl_trace(LOG_ERR, 0, "available spare(%d) < thr(%d)",
	// 				   smart_stat->available_spare, smart_stat->available_spare_threshold);
	// 	smart_stat->critical_warning.bits.available_spare = 1;
	// }
	// sys_free(SLOW_DATA, hlt);

	// extern __attribute__((weak)) bool is_system_read_only(void);
	// if (is_system_read_only)
	// {
	// 	smart_stat->critical_warning.bits.read_only = is_system_read_only();
	// }

	// Update IO Info
	btn_smart_io_t rio;
	btn_smart_io_t wio;
	btn_get_r_smart_io(&rio);
	btn_get_w_smart_io(&wio);
	smart_stat->data_units_read += rio.host_du_cnt;
	smart_stat->host_read_commands += rio.cmd_recv_cnt;
	smart_stat->data_units_written += wio.host_du_cnt;
	smart_stat->host_write_commands += wio.cmd_recv_cnt;
	smart_stat->controller_busy_time += smart_stat_get_ctrl_busy_time();

	// Update Temperature Info
	//smart_stat->temperature = ts_get() + 273;

	if(!_fg_warm_boot && !set_hctm_flag){
		smart_stat->temperature = ts_tmt.cur_ts + 273;
	}
	
	smart_stat->temperature_sensor[0] = smart_stat->temperature;
#if(degrade_mode == ENABLE)
	extern void cmd_proc_read_only_setting(u8 setting);
	extern read_only_t read_only_flags;
	extern u8  cur_ro_status;

	if( smart_stat->temperature > c_deg_to_k_deg(TS_DEFAULT_CRITICAL) )
	{	// > 85
		bool enterRO_flag = false;

		if( read_only_flags.b.high_temp == false )
		{
			read_only_flags.b.high_temp = true;
			smart_stat->critical_warning.bits.read_only = 1;
			smart_stat->critical_warning.bits.temperature = 1;
			if(show_log)
				nvme_apl_trace(LOG_ALW, 0x738b, "Set critical warning bit[3][1] because temp > 85");

			if(cur_ro_status != RO_MD_IN)
			{
				enterRO_flag = true;
			}
		}

		if( enterRO_flag == true ) {
			extern void cmd_proc_read_only_setting(u8 setting);
			cmd_proc_read_only_setting(true);
			flush_to_nand(EVT_READ_ONLY_MODE_IN);
		}

	}else
	{	// <= 85
		bool leaveRO_flag = false;

		if( read_only_flags.b.high_temp == true )
		{
			read_only_flags.b.high_temp = false;
			if(show_log)
				nvme_apl_trace(LOG_ALW, 0x043c, "clear critical warning bit[3] because temp <= 85");

			if( read_only_flags.all == 0 )
			{
				smart_stat->critical_warning.bits.read_only = 0;
				leaveRO_flag = true;
			}
		}

		if( leaveRO_flag == true ) {
			extern void cmd_proc_read_only_setting(u8 setting);
			cmd_proc_read_only_setting(false);
			flush_to_nand(EVT_READ_ONLY_MODE_OUT);
		}

		if ( smart_stat->critical_warning.bits.temperature )
		{
			if ( smart_stat->temperature < (ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH] - 5) )
			{
				smart_stat->critical_warning.bits.temperature = 0;
				if(show_log)
					nvme_apl_trace(LOG_ALW, 0xd35e, "clear critical warning bit[1]");
			}
		}
	}

	if ((smart_stat->temperature <= ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH]) || (smart_stat->temperature > ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]))
	{
		if(show_log)
		{
			nvme_apl_trace(LOG_ALW, 0xd91b, "temp %d, (%d %d)",
							smart_stat->temperature,
							ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH],
							ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]);
			nvme_apl_trace(LOG_ALW, 0xb131, "set critical warning bit[1] because touch threshold");
		}
		smart_stat->critical_warning.bits.temperature = 1;
	}

	// Smart use one temp sersor now
	// for (i = 0; i < SENSOR_IN_SMART; i++)
	// {
	// 	if( health->temperature_sensor[i] > c_deg_to_k_deg(TS_DEFAULT_CRITICAL) )
	// 	{	// > 85
	// 		bool enterRO_flag = false;

	// 		if( read_only_flags.b.high_temp == false )
	// 		{
	// 			read_only_flags.b.high_temp = true;
	// 			smart_stat->critical_warning.bits.read_only = 1;
	// 			smart_stat->critical_warning.bits.temperature = 1;
	// 			nvme_apl_trace(LOG_ALW, 0x670c, "Set critical warning bit[3][1] because temp > 85");

	// 			if(cur_ro_status != RO_MD_IN)
	// 			{
	// 				enterRO_flag = true;
	// 			}
	// 		}

	// 		if( enterRO_flag == true ) {
	// 			extern void cmd_proc_read_only_setting(u8 setting);
	// 			cmd_proc_read_only_setting(true);
	// 			flush_to_nand(EVT_READ_ONLY_MODE_IN);
	// 		}

	// 	}else
	// 	{	// <= 85
	// 		bool leaveRO_flag = false;

	// 		if( read_only_flags.b.high_temp == true )
	// 		{
	// 			read_only_flags.b.high_temp = false;
	// 			nvme_apl_trace(LOG_ALW, 0x9947, "clear critical warning bit[3] because temp <= 85");

	// 			if( read_only_flags.all == 0 )
	// 			{
	// 				smart_stat->critical_warning.bits.read_only = 0;
	// 				leaveRO_flag = true;
	// 			}
	// 		}

	// 		if( leaveRO_flag == true ) {
	// 			extern void cmd_proc_read_only_setting(u8 setting);
	// 			cmd_proc_read_only_setting(false);
	// 			flush_to_nand(EVT_READ_ONLY_MODE_OUT);
	// 		}

	// 		if ( smart_stat->critical_warning.bits.temperature )
	// 		{
	// 			if ( health->temperature_sensor[i] < temp_65C )
	// 			{
	// 				smart_stat->critical_warning.bits.temperature = 0;
	// 				nvme_apl_trace(LOG_ALW, 0x39f1, "clear critical warning bit[1]");
	// 			}
	// 		}
	// 	}

	// 	if ((health->temperature_sensor[i] <= ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH]) || (health->temperature_sensor[i] > ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]))
	// 	{
	// 		nvme_apl_trace(LOG_ALW, 0xf9cc, "temp %d, (%d %d)",
	// 						health->temperature,
	// 						ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH],
	// 						ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]);
	// 		smart_stat->critical_warning.bits.temperature = 1;
	// 		nvme_apl_trace(LOG_ALW, 0xd96d, "set critical warning bit[1] because touch threshold");
	// 	}
	// }
#else
		if ((smart_stat->temperature < ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH]) ||
			(smart_stat->temperature > ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]))
		{
			if(show_log)
			{
				nvme_apl_trace(LOG_ALW, 0x6cc8, "temp %d, (%d %d)",
						   smart_stat->temperature,
						   ctrlr->cur_feat.temp_feat.tmpth[0][UNDER_TH],
						   ctrlr->cur_feat.temp_feat.tmpth[0][OVER_TH]);
			}
			
			// smart_stat->critical_warning.bits.temperature = 1;
		}
	
		/* three sensor now */
		for (i = 0; i < MAX_TEMP_SENSOR; i++)
		{
			if ((smart_stat->temperature_sensor[i] < ctrlr->cur_feat.temp_feat.tmpth[i + 1][UNDER_TH]) ||
				(smart_stat->temperature_sensor[i] > ctrlr->cur_feat.temp_feat.tmpth[i + 1][OVER_TH]))
			{
				if(show_log)
				{
					nvme_apl_trace(LOG_ALW, 0xb3fb, "temp[%d] %d, (%d %d)", i,
										   smart_stat->temperature,
										   ctrlr->cur_feat.temp_feat.tmpth[i + 1][UNDER_TH],
										   ctrlr->cur_feat.temp_feat.tmpth[i + 1][OVER_TH]);
				}
				
				// smart_stat->critical_warning.bits.temperature = 1;
			}
		}
#endif

	extern void nvmet_evt_aer_in();
	if (ctrlr->cur_feat.aec_feat.b.smart && (smart_stat->critical_warning.raw & 0x2 )){
		nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|SMART_STS_TEMP_THRESH ),0);
	}


	smart_update_thermal_time();

	error_log_t error_log;
	error_log.frontend_error_cnt = shr_frontend_error_cnt;
	error_log.backend_error_cnt = shr_backend_error_cnt;
	error_log.ftl_error_cnt = shr_ftl_error_cnt;
	error_log.error_handle_cnt = shr_error_handle_cnt;
	error_log.misc_error_cnt = shr_misc_error_cnt;

	// DBG, SMARTVry
	nvmet_update_txsmart_stat();

	#if (Synology_case)
	nvmet_update_synology_smart_stat();
	#endif

	#if CO_SUPPORT_OCP
	nvmet_update_ocp_smart_stat();
	#endif
	
	//Update EPM Data
	//memcpy(epm_smart_data->smart_save, &smart_stat, sizeof(struct _smart_statistics_t));
	//memcpy(epm_smart_data->ex_smart_save, &tx_smart_stat, sizeof(struct _tencnet_smart_statistics_t));
	memcpy(epm_smart_data->error_log, &error_log, sizeof(struct _errr_log_t));
	memcpy(epm_smart_data->feature_save, &ctrlr->saved_feat, sizeof(struct nvmet_feat));

	//epm_update(SMART_sign, (CPU_ID - 1)); // change to epm_update(EPM_POR, (CPU_ID - 1)) in rdisk_shutdown
	
}

init_code void smart_stat_clean(void)
{
	smart_stat->host_read_commands = 0;
	smart_stat->host_write_commands = 0;
	smart_stat->data_units_written = 0;
	smart_stat->data_units_read = 0;
	smart_stat->controller_busy_time = 0;
	smart_stat->power_cycles = 0;
	smart_stat->num_error_info_log_entries = 0;
	smart_stat->power_on_minutes = 0;
	smart_stat->warning_composite_temperature_time = 0;
	smart_stat->critial_composite_temperature_time = 0;
	smart_stat->thermal_management_t1_trans_cnt = 0;
	smart_stat->thermal_management_t2_trans_cnt = 0;
	smart_stat->thermal_management_t1_total_time = 0;
	smart_stat->thermal_management_t2_total_time = 0;
}

init_code void smart_stat_init(void)
{
	stat_init(&smart_stat->head, SMART_STAT_VER,
			  sizeof(smart_statistics_t) - sizeof(smart_stat->rvsd), SMART_STAT_SIG);

	//smart_stat_clean();//when init all set to 0

	smart_stat_init_ctrl_busy_time(0);

	btn_rst_nvm_cmd_cnt();
}

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)  
init_code void clear_epm_vac(void)
{
	extern volatile u8 plp_trigger;

    u8 updt_epm = false;
	extern epm_info_t*  shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	//epm_ftl_data->epm_record_idx = 0;
	epm_ftl_data->panic_build_vac = 0;
	epm_ftl_data->epm_record_full = 0;
	epm_ftl_data->glist_inc_cnt = 0;
	epm_ftl_data->max_shuttle_gc_blk_sn = 0;
	epm_ftl_data->max_shuttle_gc_blk = INV_SPB_ID;
	// reset epm_fmt_not_finish
	if (((epm_ftl_data->epm_fmt_not_finish != 0) || (epm_ftl_data->format_tag != 0)) && (epm_ftl_data->format_tag != FTL_FULL_TRIM_TAG))
    {
        updt_epm = true;
        epm_format_state_update(0, 0);
    }
    #ifdef Dynamic_OP_En
    epm_ftl_data->Set_OP_Start = INV_U32;
    #endif

    if(!plp_trigger)
    {
		//epm_ftl_data->spor_tag = INV_U32;
		epm_ftl_data->last_close_host_blk = INV_U16;
		epm_ftl_data->last_close_gc_blk = INV_U16;
		memset(epm_ftl_data->blk_sn, 0xFF, 2048*4);
		memset(epm_ftl_data->pda_list, 0xFF, 2048*4);
    }
	else
	{
		nvme_apl_trace(LOG_ALW, 0x7c9b, "plp not clear vac");
		return;
	}

	
	//nvme_apl_trace(LOG_ALW, 0, "[IN]clear_epm_vac");
	extern volatile u8 shr_lock_power_on;
#if 0//(POWER_ON_OPEN == ENABLE)
	if (!plp_trigger) 
#else
	if (((first_usr_open == true && !(shr_lock_power_on & SLC_LOCK_POWER_ON)) || updt_epm))
#endif
	{
	    //nvme_apl_trace(LOG_ALW, 0, "[Do]clear_epm_vac");
    	epm_update(FTL_sign, (CPU_ID-1));	
	} 
}
#endif

#if (PLP_SUPPORT == 0)
init_code void clear_non_plp_epm(void)
{
	extern epm_info_t*	shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if (((epm_ftl_data->epm_fmt_not_finish != 0) || (epm_ftl_data->format_tag != 0)) && (epm_ftl_data->format_tag != FTL_FULL_TRIM_TAG))
	{
        epm_format_state_update(0, 0);
		epm_update(FTL_sign, (CPU_ID-1));	
	}
}
#endif

ddr_code void smart_for_plp_not_done(void)
{
	//for spor case
	if (ftl_stat.total_spor_cnt)
	{
		smart_stat->unsafe_shutdowns += 1;
		ftl_stat.total_spor_cnt = 0;
	}

#if PLP_SUPPORT == 0
		extern epm_smart_t *epm_smart_data;
		extern epm_info_t *shr_epm_info;
		epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		epm_update(EPM_NON_PLP, (CPU_ID - 1));
#endif

#ifdef SMART_PLP_NOT_DONE
	extern epm_info_t *shr_epm_info;
	extern u8 cur_ro_status;
	extern read_only_t read_only_flags;
	extern void cmd_proc_read_only_setting(u8 setting);
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	if(epm_smart_data->init_plp_not_flag != 0xFEDABD21)
		smart_stat->critical_warning.bits.epm_vac_err |= is_epm_vac_err;
	#if(degrade_mode == ENABLE)
	if(smart_stat->critical_warning.bits.epm_vac_err)
	{
		smart_stat->critical_warning.bits.device_reliability = 1;
		nvme_apl_trace(LOG_ALW, 0xba47, "Set critical warning bit[2] because data integrity lost");
		read_only_flags.b.plp_not_done = true;
		tx_smart_stat->plp_not_done_cnt++;
		if(cur_ro_status != RO_MD_IN)
		{
			//cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
			nvme_apl_trace(LOG_INFO, 0x320b, "IN RO %x", read_only_flags.all);
			//enter non-access mode
			cmd_proc_read_only_setting(true);
			extern void cmd_disable_btn();
			cmd_disable_btn(-1,1);
		}
	}
	#else
	if(smart_stat->critical_warning.bits.epm_vac_err)
	{
		
		read_only_flags.b.plp_not_done = true;
		tx_smart_stat->plp_not_done_cnt++;
		if(cur_ro_status != RO_MD_IN)
		{
			//cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
			nvme_apl_trace(LOG_INFO, 0x9bc6, "IN RO %x", read_only_flags.all);
			cmd_proc_read_only_setting(true);
		}
	}
	#endif
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	for(u8 i = 0; i < 2; i++)
	{
		nvme_apl_trace(LOG_ALW, 0xcd90, "plp not done , loop:%d power cycle:0x%08x max_blk_sn:0x%x ",i,epm_ftl_data->epm_record_plp_not_done[i],epm_ftl_data->epm_record_max_blk_sn[i]);
	}

	nvme_apl_trace(LOG_ALW, 0x49f9, "smart plp : %d fail cnt : %d",is_epm_vac_err,tx_smart_stat->plp_not_done_cnt);

	
	extern volatile bool esr_err_fua_flag;
    extern  bool _fg_warm_boot;	
	if(smart_stat->critical_warning.bits.volatile_memory_backup == true && _fg_warm_boot == true)
	{
		esr_err_fua_flag = true;
		nvme_apl_trace(LOG_ALW, 0x22da, "warm boot , esr error enter FUA mode!!");
	}
	
#endif	
}

//-----------------------------------------------------------------------------
/**
    Backup SMART attribute datas
**/
//-----------------------------------------------------------------------------
void SmartDataBackup(LogSmartBackupType_t logSmartBackup)
{
    u32 nsid0;

    // TODO check with smart maintainer
	//for controller busy time will be reset after preset,so update here
	smart_stat->controller_busy_time += smart_stat_get_ctrl_busy_time();
	
    for (nsid0 = 0; nsid0 < NVMET_NR_NS; nsid0++)
    {

    }

    //epm_update(SMART_sign, (CPU_ID - 1));
}

#include "srb.h"
#if (epm_enable && epm_uart_enable)
#include "console.h"
extern epm_info_t *shr_epm_info;
// fast_code static int test_epm_uart(int argc, char *argv[])
// {
// 	u32 epm_sign = 0;
// 	epm_sign = strtol(argv[1], (void *)0, 10);
// #if epm_spin_lock_enable
// 	check_and_set_ddr_lock(epm_sign);
// #endif
// 	epm_update(epm_sign, (CPU_ID - 1));
// 	return 0;
// }

// static DEFINE_UART_CMD(epm_update_uart, "epm_update_uart", "epm_update_uart", "epm_update_uart", 1, 1, test_epm_uart);
// fast_code int epm_update_cpu1(int argc, char *argv[])
// {
// 	//extern struct nvme_health_information_page *health_for_update;
// 	nvme_apl_trace(LOG_ERR, 0, "epm_update_cpu1\n");
// #if epm_spin_lock_enable
// 	check_and_set_ddr_lock(SMART_sign);
// #endif
// 	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
// 	nvme_apl_trace(LOG_ERR, 0, "update SMART_sign=%d\n", SMART_sign);
// 	nvme_apl_trace(LOG_ERR, 0, "shr_epm_info->epm_smart.ddtag=0x%x\n", shr_epm_info->epm_smart.ddtag);
// 	//smart_statistics_t* smart_data = (smart_statistics_t*)(&epm_smart_data->data[0]);
// 	health_for_update = (struct nvme_health_information_page *)(&epm_smart_data->data[0]);
// 	health_for_update->temperature += 0x12;
// 	health_for_update->available_spare += 0x34;
// 	nvme_apl_trace(LOG_ERR, 0, "health_for_update->temperature=0x%x\n", health_for_update->temperature);
// 	nvme_apl_trace(LOG_ERR, 0, "health_for_update->available_spare=0x%x\n", health_for_update->available_spare);

// 	epm_update(SMART_sign, (CPU_ID - 1));
// 	return 0;
// }

// static DEFINE_UART_CMD(epm_update1, "epm_update1", "epm_update1", "epm_update1", 0, 0, epm_update_cpu1);

#endif

/*! @} */
