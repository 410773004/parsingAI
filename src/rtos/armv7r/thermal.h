#pragma once

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


