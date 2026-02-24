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
 * @brief for internal thermal sensor
 *
 * \addtogroup rtos
 * \defgroup thermal
 * \ingroup rtos
 */
//=============================================================================
#include "types.h"
#include "sect.h"
#include "stdio.h"
#include "string.h"
#include "assert.h"
#include "vic_id.h"
#include "console.h"
#include "misc_register.h"
#include "misc.h"
#include "spin_lock.h"
#include "mpc.h"
#include "smbus.h"
#include "ddr.h"
#include "srb.h"
#include "thermal.h"

/*! \cond PRIVATE */
#define __FILEID__ thermal
#include "trace.h"
/*! \endcond */
#include "nvme_spec.h"
#define ts_to_degree(x) (int)(60 + ((x * 200) / 4094) - 101)	  ///< 60 + 200*(x/4094-0.5) + (-0.1) * SYS_CLK / ( 2 * (cpu_half_clk + 1))
#define degree_to_ts(x) (((((int)x) - 56) * 40940 / 2071) + 2047) ///< (x + 41) * 4094 / 200

#define TS_RUN_INT 0xff ///< unit: 8ms

slow_data u32 rd_upt = 50;
slow_data u32 wr_upt = 5;
slow_data_zi u32 up_time = 0;
slow_data_zi u32 down_time = 0;
slow_data u8 cap_index = 1;


enum
{
	TS_INT_EN_INTERNAL = 0x1, ///< enable interrupt each interval
	TS_INT_EN_HIGH = 0x2,	  ///< enable interrupt when > threshold0 high
	TS_INT_EN_LOW = 0x4,	  ///< enable interrupt when < threshold0 low
	TS_INT_EN_HIGH1 = 0x8,	  ///< enable interrupt when > threshold1 high
	TS_INT_EN_LOW1 = 0x10	  ///< enable interrupt when < threshold1 low
};

enum
{
	TS_MODE_NORMAL = 0, ///< run once
	TS_MODE_INTERNAL,	///< run after each interval
};

enum
{
	TS_CFG_CONVERTSION_MODE1 = 0, ///< not implemented yet, need calibration
	TS_CFG_CONVERTSION_MODE2 = 1, ///< using coefficients G & H, no calibration
};

share_data_ni volatile u32 ts_rd_credit;
share_data_ni volatile u32 ts_wr_credit;
share_data_zi volatile bool ts_io_block;
share_data_zi ts_tmt_t ts_tmt;
extern volatile ftl_flags_t shr_ftl_flags;
extern read_only_t read_only_flags;
extern u8  cur_ro_status;

extern volatile ftl_flags_t shr_ftl_flags;

u16 *SensorTemp;
extern smart_statistics_t *smart_stat;

#if 1 //CPU_ID == CPU_BE_LITE
/*
* all the paramters need to be tuned with experiment
*/
#define MAX_TS_RD_CREDIT 5000 ///< tR = 150us, 666MTs
#define MAX_TS_WR_CREDIT 380  ///< tProg = 3.5ms, 666MTs
//1920
#define MIN_TS_RD_CREDIT 100 ///< 3% of max performance
//#define MIN_TS_WR_CREDIT 5	 ///< 1% of max performance
#define MIN_TS_WR_CREDIT 20	 ///< 4% of max performance

#define MAX_NONE_RD_CREDIT ~0 ///< max performance
#define MAX_NONE_WR_CREDIT  ~0///< max performance

#define DEFAULT_LIGHT_RD_CREDIT 2000 ///< 75% of max performance
#define DEFAULT_LIGHT_WR_CREDIT 320///< 75% of max performance

#define MAX_HEAVY_RD_CREDIT 450 ///< 10% of max performance
#define MAX_HEAVY_WR_CREDIT 50	 ///< 10% of max performance

#define MAX_LIGHT_RD_CREDIT 3000 ///< 90% of max performance
#define MAX_LIGHT_WR_CREDIT 400	 ///< 90% of max performance


//unused

#define MID_LIGHT_RD_CREDIT 1500 ///< 30% of max performance
#define MID_LIGHT_WR_CREDIT 180	 ///< 40% of max performance

#define MIN_LIGHT_RD_CREDIT 1500 ///< 30% of max performance
#define MIN_LIGHT_WR_CREDIT 100	 ///< 40% of max performance

#define DEFAULT_HEAVY_RD_CREDIT 1000 ///< 20% of max performance
#define DEFAULT_HEAVY_WR_CREDIT 210	 ///< 30% of max performance


#define MIN_HEAVY_RD_CREDIT 500 ///< 10% of max performance
#define MIN_HEAVY_WR_CREDIT 100	///< 10% of max performance

#define MIN_NONE_RD_CREDIT 1500 ///< 30% of max performance
#define MIN_NONE_WR_CREDIT 140	 ///< 30% of max performance


#define TS_TRAIN_ROUND 5
/*
typedef struct _ts_mgr_t
{
	u16 gear_prv;

	int temp_prv;
	int temp_inj;

	u32 rd_credit[TS_THROTTLE_MAX];
	u32 wr_credit[TS_THROTTLE_MAX];

	struct timer_list timer;

	union
	{
		struct
		{
			u16 enabled : 1;
			u16 nand_temp : 1; ///< use nand temperature to do thermal throttle
			u16 block_io : 1;  ///< block all host io when TS_THROTTLE_BLOCK
			u16 training_enable : 1;
		} b;
		u16 all;
	} attr;

	union
	{
		struct
		{
			u16 inj_en : 1; ///< inject temperature
			u16 start : 1;
		} b;
		u16 all;
	} flags;
} ts_mgr_t;
*/
fast_data_ni ts_mgr_t ts_mgr;
share_data_ni volatile bool ts_reset_sts;
extern misc_register_regs_t *misc_reg;

fast_code void ts_thr_set(u32 high_thr, u32 low_thr)//joe slow->ddr 20201124
{
	ts_thresh1_t threshold;

	threshold.all = readl(&misc_reg->ts_thresh1);
	threshold.b.high_temp_th1 = degree_to_ts(high_thr);
	threshold.b.low_temp_th1 = degree_to_ts(low_thr);
	threshold.b.int_en = TS_INT_EN_HIGH | TS_INT_EN_LOW;
	writel(threshold.all, &misc_reg->ts_thresh1);
}

fast_code void ts_sec_time_updt(int ts)//joe slow->ddr 20201124
{
	u32 next_sec;

	if (ts >= ts_tmt.critical)
		next_sec = TS_SEC_CRITICAL;
	else if (ts >= ts_tmt.warning)
		next_sec = TS_SEC_WARNING;
	else
		next_sec = TS_SEC_NORMAL;

	if (next_sec != ts_tmt.sec)
	{
		spin_lock_take(SPIN_LOCK_TS_VAR, 0, true);

		ts_tmt.sec_time[ts_tmt.sec] += time_elapsed_in_jiffies(ts_tmt.sec_start);
		ts_tmt.sec_start = jiffies;
		ts_tmt.sec = next_sec;

		spin_lock_release(SPIN_LOCK_TS_VAR);
	}
}

fast_code void ts_tmt_time_updt(u8 gear, u8 shift)
{
	spin_lock_take(SPIN_LOCK_TS_VAR, 0, true);
	extern smart_statistics_t *smart_stat;

	if(smart_stat->thermal_management_t1_trans_cnt < smart_stat->thermal_management_t2_trans_cnt)
	{
		smart_stat->thermal_management_t1_trans_cnt = smart_stat->thermal_management_t2_trans_cnt;
		ts_tmt.tmt_cnt[0] = ts_tmt.tmt_cnt[1];
	}
	if (ts_tmt.gear != TS_THROTTLE_NONE)
	{
		u32 tick = time_elapsed_in_jiffies(ts_tmt.tmt_start);
		//account block as heavy
		if (ts_tmt.gear == TS_THROTTLE_HEAVY || ts_tmt.gear == TS_THROTTLE_BLOCK){
			ts_tmt.tmt_time[1] += tick / HZ;
			smart_stat->thermal_management_t2_total_time += tick / HZ;
		}
		else{
			ts_tmt.tmt_time[0] += tick / HZ;
			smart_stat->thermal_management_t1_total_time += tick / HZ;
		}
	}

	if (shift != TS_THROTTLE_NONE)
	{
		ts_tmt.tmt_start = jiffies;
		//account block as heavy
		if (shift == TS_THROTTLE_BLOCK || shift == TS_THROTTLE_HEAVY)
		{
			if(ts_tmt.gear == TS_THROTTLE_NONE || ts_tmt.gear == TS_THROTTLE_LIGHT)
			{
				ts_tmt.tmt_cnt[1]++;
				smart_stat->thermal_management_t2_trans_cnt++;
				if(ts_tmt.gear == TS_THROTTLE_NONE)
				{
					ts_tmt.tmt_cnt[0]++;
					smart_stat->thermal_management_t1_trans_cnt++;
				}
			}
		}
		else{
			ts_tmt.tmt_cnt[0]++;
			smart_stat->thermal_management_t1_trans_cnt++;
		}
	}

	spin_lock_release(SPIN_LOCK_TS_VAR);
}


/*
fast_code int ts_get_soc_temp(void)
{
	int ts;
	ts_int_t ts_int;

	if (ts_mgr.flags.b.inj_en == false)
	{
		ts_int.all = readl(&misc_reg->ts_int);
        printk("ts_int.b.ts_dout = %x \n",ts_int.b.ts_dout);
		ts = ts_to_degree(ts_int.b.ts_dout);
	}
	else
	{
		ts = ts_mgr.temp_inj;
	}

	return ts;
}
*/

fast_code u32 ts_get_shift(int ts)//joe slow->ddr 20201124
{
	/*
	* get shift according to current temperature, refer to NVMe spec 8.4.5
	*/
    if (ts < TS_DEFAULT_UNDER)
        return TS_THROTTLE_BLOCK;
	
#if (HOST_THERMAL_MANAGE == FEATURE_SUPPORTED) //for edvx script case
	if(ts_tmt.tmt1 == ~0 && ts_tmt.tmt2 == ~0)
	{	
		rtos_core_trace(LOG_ALW, 0xf90f, "tmt1 & tmt2 = -1");
		if(ts_tmt.gear == TS_THROTTLE_HEAVY)
            return TS_THROTTLE_NONE;
        if (ts_tmt.gear == TS_THROTTLE_LIGHT)
            return TS_THROTTLE_NONE;
        else
            return TS_THROTTLE_NONE;
	}
	/*else if(ts <= ts_tmt.tmt2 && ts >= ts_tmt.tmt1 )
	{
		if(ts_tmt.gear == TS_THROTTLE_HEAVY)
            return TS_THROTTLE_HEAVY;
        if (ts_tmt.gear == TS_THROTTLE_LIGHT)
            return TS_THROTTLE_LIGHT;
        else
            return TS_THROTTLE_LIGHT;
	}

	else if(ts > ts_tmt.tmt1 && ts > ts_tmt.tmt1+1)
	{
		if(ts_tmt.gear == TS_THROTTLE_HEAVY)
            return TS_THROTTLE_HEAVY;
        if (ts_tmt.gear == TS_THROTTLE_LIGHT)
            return TS_THROTTLE_HEAVY;
        else
            return TS_THROTTLE_HEAVY;
	}*/
#endif
    
    // (under, tmt1-5)
    if (ts < (ts_tmt.tmt1-5))
        return TS_THROTTLE_NONE;

    // (tmt1-5, tmt1]
    else if (ts < ts_tmt.tmt1)
    {
        if(ts_tmt.gear == TS_THROTTLE_HEAVY)
            return TS_THROTTLE_LIGHT;
        if (ts_tmt.gear == TS_THROTTLE_LIGHT)
            return TS_THROTTLE_LIGHT;
        else
            return TS_THROTTLE_NONE;
    }


    // (tmt1, tmt2]
    else if (ts < ts_tmt.tmt2)
    {
        if (ts_tmt.gear == TS_THROTTLE_BLOCK)
            return TS_THROTTLE_HEAVY;
        if (ts_tmt.gear == TS_THROTTLE_HEAVY)
            return TS_THROTTLE_HEAVY;
        else
            return TS_THROTTLE_LIGHT;
    }

    // (tmt2, tmt2+5]
    else if (ts < ts_tmt.tmt2 + 5){
        if (ts_tmt.gear == TS_THROTTLE_BLOCK)
            return TS_THROTTLE_BLOCK;
        else
            return TS_THROTTLE_HEAVY;
    }

	// (tmt2 + 5, ...)
	else
		return TS_THROTTLE_BLOCK;
}

#if 0
fast_code void thermal_throttle_credit_inc(u8 gear, u8 delta, u8 wr_step, u8 rd_step)
{
	u32 max_wr_credit;
	u32 max_rd_credit;

	sys_assert(gear == TS_THROTTLE_LIGHT || gear == TS_THROTTLE_HEAVY);

	if (gear == TS_THROTTLE_LIGHT)
	{
		max_rd_credit = MAX_LIGHT_RD_CREDIT;
		max_wr_credit = MAX_LIGHT_WR_CREDIT;
	}
	else
	{
		max_rd_credit = MAX_HEAVY_RD_CREDIT;
		max_wr_credit = MAX_HEAVY_WR_CREDIT;
	}

	rtos_core_trace(LOG_INFO, 0x7710, "credit inc: gear %d delta %d", gear, delta);

	ts_mgr.wr_credit[gear] += delta * wr_step;
	if (ts_mgr.wr_credit[gear] > max_wr_credit)
		ts_mgr.wr_credit[gear] = max_wr_credit;

	ts_mgr.rd_credit[gear] += delta * rd_step;
	if (ts_mgr.rd_credit[gear] > max_rd_credit)
		ts_mgr.rd_credit[gear] = max_rd_credit;
}

fast_code void thermal_throttle_credit_dec(u8 gear, u8 delta, u8 wr_step, u8 rd_step)//joe slow->ddr 20201124
{
	sys_assert(gear == TS_THROTTLE_LIGHT || gear == TS_THROTTLE_HEAVY);

	rtos_core_trace(LOG_INFO, 0x2634, "credit dec: gear %d delta %d", gear, delta);

	if (delta * wr_step >= ts_mgr.wr_credit[gear])
		ts_mgr.wr_credit[gear] = MIN_TS_WR_CREDIT;
	else
		ts_mgr.wr_credit[gear] -= delta * wr_step;

	if (ts_mgr.wr_credit[gear] < MIN_TS_WR_CREDIT)
		ts_mgr.wr_credit[gear] = MIN_TS_WR_CREDIT;

	if (delta * rd_step >= ts_mgr.rd_credit[gear])
		ts_mgr.rd_credit[gear] = MIN_TS_RD_CREDIT;
	else
		ts_mgr.rd_credit[gear] -= delta * rd_step;

	if (ts_mgr.rd_credit[gear] < MIN_TS_RD_CREDIT)
		ts_mgr.rd_credit[gear] = MIN_TS_RD_CREDIT;
}
#endif
fast_code void thermal_throttle_credit_reset(void)
{
	ts_mgr.rd_credit[TS_THROTTLE_NONE] = ~0;
    ts_mgr.rd_credit[TS_THROTTLE_LIGHT] = cap_index*DEFAULT_LIGHT_RD_CREDIT;
	ts_mgr.rd_credit[TS_THROTTLE_HEAVY] = cap_index*MAX_HEAVY_RD_CREDIT;
	ts_mgr.rd_credit[TS_THROTTLE_BLOCK] = cap_index*MIN_TS_RD_CREDIT;

	ts_mgr.wr_credit[TS_THROTTLE_NONE] = ~0;
    ts_mgr.wr_credit[TS_THROTTLE_LIGHT] = DEFAULT_LIGHT_WR_CREDIT;
	ts_mgr.wr_credit[TS_THROTTLE_HEAVY] = MAX_HEAVY_WR_CREDIT;
	ts_mgr.wr_credit[TS_THROTTLE_BLOCK] = MIN_TS_WR_CREDIT;
}

fast_code void thermal_throttle_credit_training(u8 gear)
{
	//u32 delta;
    //s32 static weight = 0;
	int avg_temp = ts_tmt.cur_ts;//max(ts_mgr.temp_sum / ts_mgr.round, ts_mgr.temp_max);
	if (avg_temp != ts_mgr.temp_prv)
		rtos_core_trace(LOG_INFO, 0xd2e6, "avg %d prv %d %d",avg_temp, ts_mgr.temp_prv, ts_mgr.gear_prv);
    /*if(ts_tmt.gear != gear)
        weight = 0;
    else weight++;*/
    switch(gear){
        case TS_THROTTLE_NONE:
            /*none below 70 celsius degree*/
            ts_mgr.wr_credit[gear] = MAX_NONE_WR_CREDIT;
            ts_mgr.rd_credit[gear] = MAX_NONE_WR_CREDIT;
            break;

        case TS_THROTTLE_LIGHT:
            /*if(avg_temp <= ts_tmt.tmt1)
                ts_mgr.wr_credit[gear] = TRAINING(DEFAULT_LIGHT_WR_CREDIT,5);*/
            /*light 76-80 celsius degree*/
            ts_mgr.wr_credit[gear] = DEFAULT_LIGHT_WR_CREDIT;
            ts_mgr.rd_credit[gear] = cap_index*DEFAULT_LIGHT_RD_CREDIT;
            break;

        case TS_THROTTLE_HEAVY:
            /*if(avg_temp <= ts_tmt.tmt2)
                ts_mgr.wr_credit[gear] = TRAINING(MAX_HEAVY_WR_CREDIT,5);*/
            /*heavy 81-85 celsius degree*/
            ts_mgr.wr_credit[gear] = MAX_HEAVY_WR_CREDIT;
            ts_mgr.rd_credit[gear] = cap_index*MAX_HEAVY_RD_CREDIT;
            break;

        case TS_THROTTLE_BLOCK:
            /*from block to heavy or above 85 celsius degree*/
            ts_mgr.wr_credit[gear] = MIN_TS_WR_CREDIT;
            ts_mgr.rd_credit[gear] = cap_index*MIN_TS_RD_CREDIT;
            break;

        default:
            rtos_core_trace(LOG_WARNING, 0xa811, "thermal throttle gear err");
            break;
    }
#if 0
    switch(gear){
        case TS_THROTTLE_NONE:
            /*none below 70 celsius degree*/
            ts_mgr.wr_credit[gear] = MAX_NONE_WR_CREDIT;
            break;

        case TS_THROTTLE_LIGHT1:
            /*from light to none and below 70 celsius degree*/
            delta = avg_temp - ts_tmt.tmt0;
            if(avg_temp == ts_mgr.temp_prv)
                weight++;
            if(avg_temp <= ts_tmt.tmt0)
                ts_mgr.wr_credit[gear] = MID_LIGHT_WR_CREDIT + (delta*10) - (weight<<2);
            /*light 70-75 celsius degree*/
            else
                ts_mgr.wr_credit[gear] = MAX_LIGHT_WR_CREDIT - (delta*40) + (weight<<2);
            break;

        case TS_THROTTLE_LIGHT2:
            delta = avg_temp - ts_tmt.tmt1;
            if(avg_temp == ts_mgr.temp_prv)
                weight++;
            if(avg_temp <= ts_tmt.tmt1)
                ts_mgr.wr_credit[gear] = MIN_LIGHT_WR_CREDIT + (delta*10) - (weight<<2);
            else
                ts_mgr.wr_credit[gear] = DEFAULT_LIGHT_WR_CREDIT - (delta*40) + (weight<<2);
            break;

        case TS_THROTTLE_HEAVY:
            /*from heavy to light and below 80 celsius degree*/
            delta = avg_temp - ts_tmt.tmt2;
            if(avg_temp == ts_mgr.temp_prv)
                weight++;
            if(avg_temp <= ts_tmt.tmt2)
                ts_mgr.wr_credit[gear] = MIN_HEAVY_WR_CREDIT + (delta + weight) * 10;
            /*heavy 80-85 celsius degree*/
            else
                ts_mgr.wr_credit[gear] = MAX_HEAVY_WR_CREDIT - (delta - weight) * 10;
            break;

        case TS_THROTTLE_BLOCK:
            /*from block to heavy or above 85 celsius degree*/
            ts_mgr.wr_credit[gear] = MIN_TS_WR_CREDIT;
    }
#endif
#if 0	// above tmt1 and temperature increase
    //-40-70
    if(gear == TS_THROTTLE_NONE){
        if(avg_temp >= ts_tmt.tmt0)
            ts_mgr.wr_credit[gear] = MAX_TS_WR_CREDIT - delta * 5;
        else ts_mgr.wr_credit[gear] = MAX_NONE_WR_CREDIT;
    }
    //70-75
    else if(gear == TS_THROTTLE_LIGHT){
        ts_mgr.wr_credit[gear] = MAX_LIGHT_WR_CREDIT - delta * 5;
    }
    //75-80
    else if(gear == TS_THROTTLE_HEAVY && avg_temp < ts_tmt.tmt2){
        ts_mgr.wr_credit[gear] = MAX_HEAVY_WR_CREDIT - delta * 10;
    }
    //80-85
    else if(ts_tmt.gear == TS_THROTTLE_HEAVY){
        ts_mgr.wr_credit[gear] = DEFAULT_HEAVY_WR_CREDIT - delta * 10;
    }
    else
        ts_mgr.wr_credit[gear] = 0;


    //-40-70
	if (avg_temp >= ts_tmt.tmt1 && avg_temp > ts_mgr.temp_prv)
	{
		delta = avg_temp - ts_mgr.temp_prv;
		if (ts_mgr.gear_prv == TS_THROTTLE_HEAVY)
			thermal_throttle_credit_dec(TS_THROTTLE_HEAVY, delta, 10, 5);

		if (ts_mgr.gear_prv == TS_THROTTLE_LIGHT)
			thermal_throttle_credit_dec(TS_THROTTLE_LIGHT, delta, 20, 10);
	}

	// above tmt1 but temperature decrese
	if (avg_temp >= ts_tmt.tmt1 && avg_temp < ts_mgr.temp_prv)
	{
		// do nothing, keep current performance
	}

	// below tmt1 and temperature increase
	if (avg_temp < ts_tmt.tmt1 && avg_temp > ts_mgr.temp_prv)
	{
		delta = avg_temp > ts_mgr.temp_prv;
		// dec credit to drop terperature
		if ((ts_mgr.gear_prv == TS_THROTTLE_BLOCK && ts_mgr.temp_prv > ts_tmt.critical) || (ts_mgr.gear_prv == TS_THROTTLE_HEAVY) || (ts_mgr.gear_prv == TS_THROTTLE_LIGHT))
		{
			thermal_throttle_credit_dec(TS_THROTTLE_HEAVY, delta, 10, 5);
		}

		if (ts_mgr.gear_prv == TS_THROTTLE_NONE)
		{
			// do nothing, keep max performance
		}

		// reset credit and train again
		if (ts_mgr.gear_prv == TS_THROTTLE_BLOCK && ts_mgr.temp_prv < TS_DEFAULT_UNDER)
			thermal_throttle_credit_reset();
	}

	// below tmt1 but terperature decrease
	if (avg_temp < ts_tmt.tmt1 && avg_temp < ts_mgr.temp_prv)
	{
		// do nothing, keep current performance
	}
#endif
    //printk( "ts_wr_credit = %d ruond = %d \n",ts_wr_credit,ts_mgr.round);


}
extern volatile u8 plp_trigger;
fast_code void thermal_throttle(u8 shift, int cur_temp)
{
	//train credit with gear and temp
	if(plp_trigger || ts_reset_sts)
	{
		if(shift >= TS_THROTTLE_HEAVY )
			rtos_core_trace(LOG_INFO, 0x0154, "plp_trigger or ts_reset_sts, no set CPU slow down, shift:%d",shift);
		return ;
	}
	thermal_throttle_credit_training(shift);
    rtos_core_trace(LOG_DEBUG, 0xed3d, "current ts %d gear %d wr credit %d rd credit %d\n",ts_tmt.cur_ts, shift, ts_wr_credit, ts_rd_credit);
	if (ts_tmt.gear != shift)
	{
		ts_tmt_time_updt(ts_tmt.gear, shift);
		down_time = 0;
		up_time = 0;
		rtos_core_trace(LOG_INFO, 0xb5e6, "thermal throttle %d -> %d ", ts_tmt.gear, shift);
		ts_tmt.gear = shift;
	}

#if(degrade_mode == ENABLE)
	if (ts_tmt.cur_ts > TS_DEFAULT_CRITICAL) {
		bool enterRO_flag = false;

		if (read_only_flags.b.high_temp == 0)
		{
			read_only_flags.b.high_temp = 1;
			smart_stat->critical_warning.bits.read_only = 1;
			rtos_core_trace(LOG_INFO, 0x690c, "Set critical warning bit[3] because temp > 85");

			if(cur_ro_status != RO_MD_IN)
			{
				enterRO_flag = true;
			}
	    }

		if( enterRO_flag == true ) {
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
			flush_to_nand(EVT_READ_ONLY_MODE_IN);
		}
    }
	else {
		bool leaveRO_flag = false;

		if (read_only_flags.b.high_temp )
		{
			read_only_flags.b.high_temp = 0;
			if(read_only_flags.all == 0)
			{
				smart_stat->critical_warning.bits.read_only = 0;
				rtos_core_trace(LOG_ALW, 0xef47, "clear critical warning bit[3] %d because temp <= 85", ts_tmt.cur_ts);
				leaveRO_flag = true;
			}
		}

		if( leaveRO_flag == true ) {
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
			flush_to_nand(EVT_READ_ONLY_MODE_OUT);
		}
	}
#else
	if (ts_tmt.cur_ts > TS_DEFAULT_CRITICAL) {
		if (read_only_flags.b.ts_block == 0)
	    {
			flush_to_nand(EVT_READ_ONLY_MODE_IN);
	    }
        read_only_flags.b.ts_block = 1;

		if(cur_ro_status != RO_MD_IN)
		{
			cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
		}
    }
    else {
        if (read_only_flags.b.ts_block) {
            flush_to_nand(EVT_READ_ONLY_MODE_OUT);

	        read_only_flags.b.ts_block = 0;
			read_only_flags.b.high_temp = 0;
		

			if(cur_ro_status == RO_MD_IN)
			{
				cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
			}
        }
    }
#endif
	//update credit according to current gear
	if(ts_mgr.attr.b.training_enable)
	{
		if(ts_mgr.gear_prv != ts_tmt.gear)
		{
	        ts_rd_credit = ts_mgr.rd_credit[ts_tmt.gear];
	        ts_wr_credit = ts_mgr.wr_credit[ts_tmt.gear];

		}
    }
    //printk( "ts_wr_credit = %d ruond = %d \n",ts_wr_credit,ts_mgr.round);

	//save prv gear & temp
	ts_mgr.gear_prv = ts_tmt.gear;
	ts_mgr.temp_prv = ts_tmt.cur_ts;
}

#if 0
fast_code static void ts_isr(void)
{
	int ts;
	ts_int_t ts_int;

	ts = ts_get_soc_temp();

	rtos_core_trace(LOG_ALW, 0xec20, "%x ts_dout = 0x%x %d degree",
			ts_int.all & 0x7, ts_int.b.ts_dout, ts);

     /*
	* TODO: we use timer to monitor temperature mostly,
	* but we still can use isr to to thermal throttle in time
	*/

	ts_int.b.ts_dout_rdy = 1;
	ts_int.b.ts_above_th1 = 1;
	ts_int.b.ts_below_th1 = 1;
	writel(ts_int.all, &misc_reg->ts_int);
}
#endif
fast_code void ts_setup(void)
{
	ts_time_t time;
	ts_ctrl_t ctrl;
	ts_int_t ts_int;

    //Benson : IG fix the ts bug ==> ts init too long
    //normally about 64usec
	ctrl.all = readl(&misc_reg->ts_ctrl);
	ctrl.b.ts_pwr_on = 0;
	writel(ctrl.all, &misc_reg->ts_ctrl);
	ctrl.b.ts_pwr_on = 1;
	ctrl.b.ts_mode = TS_MODE_NORMAL;
	ctrl.b.ts_start = 0;
	writel(ctrl.all, &misc_reg->ts_ctrl);

	time.all = readl(&misc_reg->ts_time);
	time.b.run_interval = TS_RUN_INT;
	time.b.ts_cfg = TS_CFG_CONVERTSION_MODE2;
	time.b.ts_cload = 1;
	writel(time.all, &misc_reg->ts_time);
	do
	{
		time.all = readl(&misc_reg->ts_time);
	} while (time.b.ts_cload == 1);

	ts_thr_set(ts_tmt.tmt1, TS_DEFAULT_UNDER);

	ts_int.all = readl(&misc_reg->ts_int);
	writel(ts_int.all, &misc_reg->ts_int);

	ctrl.all = readl(&misc_reg->ts_ctrl);
	ctrl.b.ts_num_run = 0x0;
	ctrl.b.ts_mode = TS_MODE_INTERNAL;
	ctrl.b.ts_start = 1;
	writel(ctrl.all, &misc_reg->ts_ctrl);
}

#if (CPU_ID == CPU_BE_LITE)
fast_code void ts_get_temp(void)
{
    if (ts_mgr.flags.b.inj_en == false){
            int oldTemp = ts_tmt.cur_ts;
      		ts_tmt.cur_ts = smart_stat->temperature - 273;
			if(ts_tmt.cur_ts == 0x80)
            rtos_core_trace(LOG_INFO, 0x0428, "temp sensor err, ts_get_temp ts_tmt.cur_ts %d ",ts_tmt.cur_ts);

            if(ts_tmt.cur_ts == -273)	//Clearing SMART wiil cause smart_stat->temperature == 0
            {
                rtos_core_trace(LOG_INFO, 0x04cd, "no smart->temperature error %d ",ts_tmt.cur_ts);
                ts_tmt.cur_ts = oldTemp;
            }
    }
    else
        ts_tmt.cur_ts = ts_mgr.temp_inj;

}

share_data bool all_init_done;

fast_code void ts_timer_handler(void *data)
{
    if(!all_init_done){
        ts_mgr.flags.b.start = true;
        mod_timer(&ts_mgr.timer, jiffies + 1*HZ/10);
        return;
    }

    if(ts_mgr.flags.b.start == true){
        ts_mgr.flags.b.start = false;
        mod_timer(&ts_mgr.timer, jiffies + 200*HZ/10);
        rtos_core_trace(LOG_INFO, 0xec53, "start thermal throttle, temp %d",ts_tmt.cur_ts);
        return;
    }
    ts_get_temp();
    u8 shift = ts_get_shift(ts_tmt.cur_ts);
	if(plp_trigger || ts_reset_sts)
	{
		if(shift >= TS_THROTTLE_HEAVY )
			rtos_core_trace(LOG_INFO, 0x96e4, "plp_trigger or ts_reset_sts , no set CPU slow down, shift:%d",shift);
	}
	else if(ts_mgr.attr.b.training_enable)
	{
		if(shift == TS_THROTTLE_LIGHT)
		{
			int ts_now1 = ts_tmt.cur_ts;		
			if(ts_now1 < ts_tmt.tmt1)
			{
		
				int down_ts = ts_tmt.tmt1 - ts_now1;
				up_time = 0;
				
				if(down_time < 100)
				{
					if(down_ts < 2)
					{
						down_time = (down_time + 2)*down_ts;
					}
					else
					{
						if(down_ts > 5)
						{
							down_ts  = 5;
						}
						down_time = down_ts*(down_time+1);
						down_time += 5;
					}
				}
				
				
				if(down_time >= 20)
				{	
					if((ts_rd_credit + cap_index*rd_upt*down_ts) <= (cap_index*MAX_LIGHT_RD_CREDIT))//wait
					{
						ts_rd_credit = ts_rd_credit + cap_index*rd_upt*down_ts;
					}
					else
					{
						ts_rd_credit = cap_index*MAX_LIGHT_RD_CREDIT;
					}
					if(ts_wr_credit + wr_upt*down_ts <= MAX_LIGHT_WR_CREDIT)//90%
					{
						ts_wr_credit = ts_wr_credit + wr_upt*down_ts;
					}
					else
					{
						ts_wr_credit = MAX_LIGHT_WR_CREDIT;
					}
					down_time = 0;
				}
			
			
			}
			else if(ts_now1 > ts_tmt.tmt1)
			{

				int up_ts = ts_now1 - ts_tmt.tmt1;
				down_time = 0;
				if(up_time < 100)
				{
					if(up_ts < 3)
					{
						up_time = (up_time + 2)*up_ts;
					}
					else
					{
						if(up_ts > 5)
						{
							up_ts  = 5;
						}
						up_time = up_ts*(up_time+1);
						up_time += 5;
					}
				}

				if(up_time >= 20)
				{

					if(ts_rd_credit > (cap_index*rd_upt*up_ts+cap_index*MAX_HEAVY_RD_CREDIT))
					{
						ts_rd_credit = ts_rd_credit - cap_index*rd_upt*up_ts;
						
					}
					else
					{
						ts_rd_credit = cap_index*MAX_HEAVY_RD_CREDIT;
					}
				
					

					if(ts_wr_credit > (wr_upt*up_ts+MAX_HEAVY_WR_CREDIT))
					{
						ts_wr_credit = ts_wr_credit - wr_upt*up_ts;
						
					}
					else
					{
						ts_wr_credit = MAX_HEAVY_WR_CREDIT;//10%
					}
				
					up_time = 0;
				}
			}
		}
		else if(shift == TS_THROTTLE_HEAVY)
		{
			int ts_now2 = ts_tmt.cur_ts;		
			if(ts_now2 < ts_tmt.tmt2)
			{
		
				int down_ts = ts_tmt.tmt2 - ts_now2;
				up_time = 0;
				
				if(down_time < 100)
				{
					if(down_ts < 2)
					{
						down_time = (down_time + 2)*down_ts;
					}
					else
					{
						if(down_ts > 5)
						{
							down_ts  = 5;
						}
						down_time = down_ts*(down_time+1);
						down_time += 5;
					}
				}
				
				
				if(down_time >= 20)
				{	
					if((ts_rd_credit + cap_index*rd_upt*down_ts) <= (cap_index*MAX_HEAVY_RD_CREDIT*2))//wait
					{
						ts_rd_credit = ts_rd_credit + cap_index*rd_upt*down_ts;
					}
					else
					{
						ts_rd_credit = cap_index*MAX_HEAVY_RD_CREDIT*2;
					}
					if(ts_wr_credit + wr_upt*down_ts <= MAX_HEAVY_WR_CREDIT*2)
					{
						ts_wr_credit = ts_wr_credit + wr_upt*down_ts;
					}
					else
					{
						ts_wr_credit = MAX_HEAVY_WR_CREDIT*2;
					}
					down_time = 0;
				}
			
			
			}
			else if(ts_now2 > ts_tmt.tmt2)
			{

				int up_ts = ts_now2 - ts_tmt.tmt2;
				down_time = 0;
				if(up_time < 100)
				{
					if(up_ts < 3)
					{
						up_time = (up_time + 2)*up_ts;
					}
					else
					{
						if(up_ts > 5)
						{
							up_ts  = 5;
						}
						up_time = up_ts*(up_time+1);
						up_time += 5;
					}
				}

				if(up_time >= 20)
				{


					if(ts_rd_credit > (cap_index*rd_upt+cap_index*MIN_TS_RD_CREDIT))
					{
						ts_rd_credit = ts_rd_credit - cap_index*rd_upt;
					}
					else
					{
						ts_rd_credit = cap_index*MIN_TS_RD_CREDIT;
					}
					

					if(ts_wr_credit > (wr_upt+MIN_TS_WR_CREDIT))
					{
						ts_wr_credit = ts_wr_credit - wr_upt;
					}
					else
					{
						ts_wr_credit = MIN_TS_WR_CREDIT;//4%
					}

				
					up_time = 0;
				}
			}
		}
	}

	//if (ts != ts_tmt.cur_ts || shift != ts_tmt.gear)
	//printk( "ts %d shift %d gear %d \n", ts, shift, ts_tmt.gear);
	ts_sec_time_updt(ts_tmt.cur_ts);

	thermal_throttle(shift, ts_tmt.cur_ts);
	mod_timer(&ts_mgr.timer, jiffies + 2*HZ);
}

fast_code void ts_start(void)
{
	/*
	* only setup soc temperature parameters but don't enable irq,
	* get temperature periodically in timer and train thermal throttle credits
	*/
	ts_setup();

	ts_io_block = false;
	memset(&ts_tmt, 0, sizeof(ts_tmt_t));
	memset(&ts_mgr, 0, sizeof(ts_mgr_t));
	ts_reset_sts = false;

	ts_tmt.cur_ts = 30;
	ts_tmt.sec = TS_SEC_NORMAL;
	ts_tmt.gear = TS_THROTTLE_NONE;
	ts_tmt.tmt0 = TS_DEFAULT_TMT0;//not used
	ts_tmt.tmt1 = TS_DEFAULT_TMT1;
	ts_tmt.tmt2 = TS_DEFAULT_TMT2;
	ts_tmt.warning = TS_DEFAULT_WARNING;
	ts_tmt.critical = TS_DEFAULT_CRITICAL;

	//monitor nand/soc temp for thermal throttle
	ts_mgr.attr.b.nand_temp = USE_NAND_TEMP;
    ts_mgr.attr.b.enabled = true;
    ts_mgr.flags.b.start = false;
	ts_mgr.temp_prv = 30;
    ts_mgr.attr.b.training_enable = 1;
	srb_t *srb = (srb_t *)SRAM_BASE;
	if (srb->cap_idx == 2){ //2T
    	cap_index = 1;
	}else if(srb->cap_idx == 1){ //1T
		cap_index = 2;
	}else if(srb->cap_idx == 0){ //512g
		cap_index = 4;
	}
	else{
		cap_index = 1;
	}
	thermal_throttle_credit_reset();

	// max performance as default
	ts_mgr.gear_prv = TS_THROTTLE_NONE;
	ts_rd_credit = ts_mgr.rd_credit[TS_THROTTLE_NONE];
	ts_wr_credit = ts_mgr.wr_credit[TS_THROTTLE_NONE];

	ts_mgr.timer.data = NULL;
	ts_mgr.timer.function = ts_timer_handler;
	INIT_LIST_HEAD(&ts_mgr.timer.entry);

    rtos_core_trace(LOG_INFO, 0x3e0c, "enable thermal throttle timer ");
	if (ts_mgr.attr.b.enabled)
	{
		if (misc_is_warm_boot() == true)
		{
			mod_timer(&ts_mgr.timer, jiffies + 2*HZ);
		}
		else
		{
			mod_timer(&ts_mgr.timer, jiffies + 100*HZ/10);
		}
	}

}

ddr_code int ts_console(int argc, char *argv[]) // 20210224 Jamie slow_code -> ddr_code
{
	//ts_int_t ts_int;
	u32 mode = strtol(argv[1], (void *)0, 10);

	switch (mode)
	{
	case 0: //read current temperature
		//ts_int.all = readl(&misc_reg->ts_int);
		//rtos_core_trace(LOG_INFO, 0, "current ts %d", ts_to_degree(ts_int.b.ts_dout));
		ts_mgr.flags.b.inj_en = false;
        ts_mgr.attr.b.training_enable = true;
        rtos_core_trace(LOG_INFO, 0x448c, "cacel inject temp and ts_wr_credit");
		break;

	case 1: //inject temperature
		ts_mgr.flags.b.inj_en = true;
		ts_mgr.temp_inj = strtol(argv[2], (void *)0, 10);
        rtos_core_trace(LOG_INFO, 0x5fd7, "set ts %d", ts_mgr.temp_inj);
		break;

	case 2: //show current ts and gear
		rtos_core_trace(LOG_INFO, 0xc583, "current ts %d gear %d wr credit %d rd credit %d",
						ts_tmt.cur_ts, ts_tmt.gear, ts_wr_credit, ts_rd_credit);
		break;

    case 3:
        ts_mgr.attr.b.training_enable = false;
        ts_wr_credit = strtol(argv[2], (void *)0, 10);
        ts_rd_credit = ts_wr_credit;
        rtos_core_trace(LOG_INFO, 0x4eb5, "set speed %d", ts_wr_credit);
        break;
	//case 4:
	//case 4:
	//	read_only_flags.b.spb_retire = 1;
	//	if(cur_ro_status != RO_MD_IN)
	//	{
 	//		cpu_msg_issue(CPU_FE - 1, CPU_MSG_ENTER_READ_ONLY_MODE, 0, false);
	//	}
	//	break;
	//case 5:
    //    read_only_flags.b.spb_retire = 0;
	//	if(cur_ro_status == RO_MD_IN)
	//	{
	//		cpu_msg_issue(CPU_FE - 1, CPU_MSG_LEAVE_READ_ONLY_MODE, 0, false);
	//	}
	//	break;
	default:
		rtos_core_trace(LOG_ERR, 0x2f22, "invalid parameter");
		break;
	}

	return 0;
}

static DEFINE_UART_CMD(ts, "ts", "ts",
					   "Read SoC temperature", 0, 2, ts_console);
#endif
#endif

#if (CPU_ID == CPU_FE) || (CPU_ID == 1)
fast_code void ts_sec_time_get(u32 sec_time[3], u32 tmt_tran_cnt[2], u32 tmt_time[2])
{
	u32 i;
#if defined(MPC)
	spin_lock_take(SPIN_LOCK_TS_VAR, 0, true);
#endif
	ts_tmt.sec_time[ts_tmt.sec] += time_elapsed_in_jiffies(ts_tmt.sec_start);
	ts_tmt.sec_start = jiffies;

	for (i = 0; i < TS_SEC_MAX; i++)
	{
		sec_time[i] = ts_tmt.sec_time[i] / (HZ * 60); // sec time in minutes
		ts_tmt.sec_time[i] -= sec_time[i] * HZ * 60;
	}

	/*if (ts_tmt.gear != TS_THROTTLE_NONE)
	{
		if (ts_tmt.gear == TS_THROTTLE_BLOCK)
			ts_tmt.tmt_time[ts_tmt.gear - 2] += time_elapsed_in_jiffies(ts_tmt.tmt_start);
		else
			ts_tmt.tmt_time[ts_tmt.gear - 1] += time_elapsed_in_jiffies(ts_tmt.tmt_start);

		ts_tmt.tmt_start = jiffies;
	}

	for (i = 0; i < 2; i++)
	{
		tmt_tran_cnt[i] = ts_tmt.tmt_cnt[i];
		ts_tmt.tmt_cnt[i] = 0;

		tmt_time[i] = ts_tmt.tmt_time[i] / HZ; // tmt time in seconds
		ts_tmt.tmt_time[i] -= tmt_time[i] * HZ;
	}*/
#if defined(MPC)
	spin_lock_release(SPIN_LOCK_TS_VAR);
#endif
}

fast_code void ts_tmt_setup(u32 tmt1, u32 tmt2)
{
	extern bool set_hctm_flag;
	set_hctm_flag = 1;
	rtos_core_trace(LOG_INFO, 0x8459, "tmt setup: tmt1 %d tmt2 %d", tmt1, tmt2);
	
	// no tmt1, use tmt2, there is no light throttle
	if (tmt1 == ~0 && tmt2 != ~0)
		tmt1 = tmt2;

	// no tmt2, use default tmt2
	if (tmt2 == ~0 && tmt1 != ~0)
		tmt2 = TS_DEFAULT_TMT2;

	ts_tmt.tmt1 = tmt1;
	ts_tmt.tmt2 = tmt2;
}

fast_code u32 ts_get(void)
{
	/*
	* We don't read real-time temperature but return the temperature BE updated in last round.
	* If nand temperature supported, then FE need issue sync msg to read nand temperature,
	* this may affect performance.
	* It's acceptable to return BE's temperature as BE updates temperature every 100ms.
	*/

	u32 ts = (readl((void *)0xC004025C) >> 16) & 0x0FFF;
	ts = (int)(60 + ((ts * 200) / 4094) - 101);
	return ts;
}

fast_code void ts_warn_cri_tmt_setup(u32 tmt_warning, u32 tmt_critical)
{
	if (tmt_warning != ~0)
		ts_tmt.warning = tmt_warning;

	if (tmt_critical != ~0)
		ts_tmt.critical = tmt_critical;
}
#endif
