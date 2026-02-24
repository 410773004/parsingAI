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
/*! \file emp.h
 * @brief
 *
 * \addtogroup
 * \defgroup
 * \ingroup
 *
 */
#pragma once
#include "types.h"
#include "bf_mgr.h"
#include "queue.h"
#include "mpc.h"
#ifndef __FILEID__
#define __FILEID__ epmh
#include "trace.h"
#endif

#if defined(USE_CRYPTO_HW)
#include "../nvme/inc/nvme_precomp.h"
#endif

#define epm_uart_enable 0
#define epm_debug 0

//
#define epm_spin_lock_enable 0
#define epm_remap_enable 1 //1
#define FRB_remap_enable 0 //1
//

///////////////////////////////////////////
#define epm_head_tag 0x48454144 //"HEAD"
#define epm_valid_tag 0x45504D	//"EPM"
#define invalid_epm 0xFFFFFFFF

#if epm_spin_lock_enable
#define max_key_num 50
#endif

#define EPM_BLK_CNT 2
#define EPM_RM_BLK 3
#define DU_CNT_PER_EPM_FLUSH  EPM_BLK_CNT << DU_CNT_SHIFT  //(shr_nand_info.geo.nr_planes * DU_CNT_PER_PAGE)
#define PG_CNT_PER_EPM_FLUSH (DU_CNT_PER_EPM_FLUSH >> DU_CNT_SHIFT)

#define epm_set_bit(x, bit) (x) |= (1 << (bit - 1))

enum flush_mode
{
	ALL_FLUSH = 0,
	ONLY_MASTER,
	ONLY_MIRROR,
	ONLY_HEADER, //for reflush
};

enum area_tag
{
	EPM_HEADER_TAG = 0,
	//EPM_HEADER_MIRROR_TAG,
	EPM_DATA_TAG,
	EPM_DATA_MIRROR_TAG,
	EPM_TAG_end,
};

enum epm_sign
{
	FTL_sign = 1,
	GLIST_sign,
	SMART_sign,
	NAMESPACE_sign,
	AES_sign,
	TRIM_sign,
	JOURNAL_sign,
	//MISC_sign,
	ERROR_WARN_sign,
	//TCG_INFO_sign,
	EPM_sign_end,
	EPM_PLP1,
	EPM_PLP2,
	EPM_PLP_TEST1,
	EPM_PLP_TEST2,
	EPM_POR,
	EPM_NON_PLP,
	EPM_PLP_end
};

typedef struct _epm_pos_t
{
	u32 ddtag; // address
	u32 ddtag_cnt;
#if epm_spin_lock_enable
	volatile u8 busy_lock;
	volatile u8 cur_key;
	volatile u8 alloc_key;
	volatile u8 key_num[4];		 //4 CPU key number
	volatile u8 set_ddr_done[4]; //4 cpu mem set done for erase case
#endif
} epm_pos_t;

typedef struct _epm_info_t
{
	epm_pos_t epm_header;
	epm_pos_t epm_ftl;
	epm_pos_t epm_glist;
	epm_pos_t epm_smart;
	epm_pos_t epm_namespace;
	epm_pos_t epm_aes;
	epm_pos_t epm_trim;
	epm_pos_t epm_journal;
	//epm_pos_t epm_misc;
	epm_pos_t epm_error_warn_data;
	//epm_pos_t epm_tcg_info;
#if epm_remap_enable
	u32 epm_remap_tbl_info_ddtag;
#endif
	u32 rebuild_ddtag; // address
} epm_info_t;

typedef struct _epm_sub_header_t
{
	u32 epm_header_tag;
	pda_t epm_newest_head;
	pda_t epm_newest_tail;
	u32 valid_tag;
} epm_sub_header_t; //16bytes

#if epm_remap_enable
typedef struct _epm_remap_tbl_t
{
	u32 EPM_remap_tag;
	u32 epm_remap_sn; //4
	pda_t remap_last_pda;
	pda_t remap_tbl_blk[2];			  //8  [0]:master, [1]:mirror
	pda_t remap_tbl_mirror_mask;	  //4 bytes
	pda_t epm_remap[8];				  //32
	pda_t epm_remap_source[8];		  //32
	pda_t epm_mirror_remap[8];		  //32
	pda_t epm_mirror_remap_source[8]; //32
#if FRB_remap_enable
	pda_t frb_remap[2];		  //8
	pda_t frb_remap_source[2]; //8
	u32 rsvd[16212 / 4];
#else
	u32 rsvd[16228 / 4];
#endif
	u32 valid_tag; //4
} epm_remap_tbl_t; //16kbytes
#endif

//should be 16K align
typedef struct _epm_header_t
{
	u32 EPM_Head_tag;
	//not fixed value
	u32 EPM_SN;
	epm_sub_header_t epm_ftl_header;	   //16bytes
	epm_sub_header_t epm_glist_header;	   //16bytes
	epm_sub_header_t epm_smart_header;	   //16bytes
	epm_sub_header_t epm_namespace_header; //16bytes
	epm_sub_header_t epm_aes_header;	   //16bytes
	epm_sub_header_t epm_trim_header;	   //16bytes
	epm_sub_header_t epm_journal_header; //16bytes
	//epm_sub_header_t epm_misc_header;	   //16bytes
	epm_sub_header_t epm_nvme_data_header; //16bytes
	//epm_sub_header_t epm_tcg_info_header;  //16bytes
	pda_t epm_header_last_pda;			   //4 bytes
	pda_t epm_last_pda;					   //4bytes
										   //
	pda_t epm_header_mirror_mask;		   //4 bytes
	pda_t epm_mirror_mask;				   //4 bytes
	u32 rsvd[16228 / 4];
	u32 valid_tag; //4
} epm_header_t;	   //16kbytes

//should be 32K align  two pl write
typedef struct _epm_FTL_t
{
	u32 epm_sign; //4 bytes
	u32 EPM_SN;
	u32 BadSPBCnt;
	u32 Phyblk_OP;
	u16 last_close_host_blk;
	u16 last_close_gc_blk;
    u16 epm_ec_table[1958];    // 8T max blk cnt : 1958
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
    u32 epm_vc_table[1958];    // 8T max blk cnt : 1958
    u32 spor_tag;
	u32 blk_sn[2048];
	u32 pda_list[2048];
	//u16 epm_record_idx;
	u16 panic_build_vac;
	u16 epm_record_full;
    u32 spor_last_rec_blk_sn;
	u32 glist_inc_cnt;
	u16 qbt_grp1_head;
	u16 qbt_grp1_tail;
	u16 qbt_grp2_head;
	u16 qbt_grp2_tail;
	u32 qbt_tag;
	u32 POR_tag;
	u32 epm_fmt_not_finish;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	/*----------------SLC Cache------------------*/
	u16 plp_pre_slc_wl;
	u16 plp_slc_wl; 		// next wl to write
	u32 plp_slc_times;
	u32 plp_slc_gc_tag;
	u32 plp_slc_disable;	//epm err ,set this flag
	u32 slc_format_tag; 	//when preformat , set this flag
	u32 plp_last_blk_sn;	//record last blk sn
	u32 slc_eraseing_tag;
	u32 esr_lock_slc_block;		//esr error, enter FUA mode 
	/*----------------SLC Cache------------------*/
	u32 epm_slc_data[12];  // minus ec and vac table and spor tag
#else
	u32 epm_slc_data[20];  
#endif

#if MDOT2_SUPPORT 
#if(PLP_SUPPORT == 1) 
    u32 SGM_data_F_10;
    u32 SGM_for_reboot;
    u32 epm_SGM_data[3];
#else
    u32 epm_SGM_data[5]; 
#endif
#else
    u32 epm_SGM_data[5]; 
#endif

	u32 epm_record_plp_not_done[2];
	u32 epm_record_max_blk_sn[2];
	u16 epm_record_loop;
	u16 Reserve16;
    u32 format_tag;
    
	u32 max_shuttle_gc_blk;
	u32 max_shuttle_gc_blk_sn;
    u32 data[(262144/4)-979-100-1958-1-1-2048-2048-1-1-1-1-3-1-20-5-5-1-2];

#else
	u32 POR_tag;
    u32 glist_inc_cnt;
	u32 epm_fmt_not_finish;

    u16 host_open_blk[2];
    u16 gc_open_blk[2];    
    u32 host_die_bit[16];
    u32 gc_die_bit[16];    
    u16 host_open_wl[16];  
    u16 gc_open_wl[16];

    u16 host_aux_group;
    u16 gc_aux_group;

	u32 non_plp_gc_tag;
    u32 non_plp_last_blk_sn;

    u32 format_tag;
	u32 data[(262144/4)-100-979-1-1-1-1-53-1]; //256k - epm_ec_table   
#endif
	
	u32 pbt_force_flush_flag;		//for force close dummy pbt-----1.shuttle GC 2.program fail
	u32 record_PrevTableSN;			//for spor judge GC range,if blist don't update
    u32 ftl_data_rsv[100 -2];//plp\nonplp reserve space



#ifdef Dynamic_OP_En
	u32 OPValue;
	u32 OPFlag;
	u32 Capa_OP14;
	u32 DefectSource;
	u32 OP_LBA_L;
	u32 OP_LBA_H;
	u32 OP_CAP;
	u32 PhyOP;
    u32 Set_OP_Start;
	u32 rsvd[16352/4 -4-1];
#else
	u32 DefectSource;
	u32 rsvd[16364 / 4];
#endif
	epm_header_t header; //16k bytes
} epm_FTL_t;			 //256k+16k+16k

BUILD_BUG_ON(sizeof(epm_FTL_t) != 288*1024);

//should be 32K align  two pl write
typedef struct _epm_glist_t
{
	u32 epm_sign;
	u32 EPM_SN;
	u32 data[622592 / 4]; //608k
	u32 rsvd[(16384 - 4 - 4) / 4];
	epm_header_t header; //16k bytes
} epm_glist_t;			 //608k+16k+16k

typedef struct _errr_log_t
{
	u32 frontend_error_cnt;
	u32 backend_error_cnt;
	u32 ftl_error_cnt;
	u32 error_handle_cnt;
	u32 misc_error_cnt;
}error_log_t;

//should be 32K align  two pl write
typedef struct _epm_smart_t
{
	u32 epm_sign;			   //4
	u32 EPM_SN;				   //4
	u32 smart_save[64];		   //256
	u32 dcc_training_fail_cnt; //4
	u32 ex_smart_save[128];	   //512
	u32 hi_epm_nand_bytes_written; //4
	u32 lo_epm_nand_bytes_written; //4
	u32 init_plp_not_flag;		//4
	u32 error_log[5];          //20
	u32 feature_save[44];	   ///176  	172->176 23/11/17 Shengbin 
	u32 is_IOQ_ever_create_or_not;	//4		23/11/17 Shengbin add for Set feature 0x7
#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	u8 telemetry_ctrlr_available;//1
	u8 telemetry_ctrlr_gen_num;  //1
	u8 telemetry_update_ctrlr_signal;  //1
	u8 unused[1];              //1
#else
	u32 unused;                //4
#endif
#if CO_SUPPORT_DEVICE_SELF_TEST
	u32 LogTEL[1064];			 //4256B
	u32 LogPersistent[1613];	//6452
	u32 LogDST[152];			//608
	u32 rsvdb[1];				//4		   
#endif
	u32 synology_smart_save[64];	//256
#if CO_SUPPORT_OCP
	u32 ocp_smart_save[128];	   //512
#endif	
#if CO_SUPPORT_SANITIZE
	u32 data[813];			   //3252
	struct {
		u32 sanitize_Tag;
		volatile u32 fwSanitizeProcessStates;
		volatile u64 sanitize_log_page;
		u64 handled_wr_cmd_cnt;
		u32 bmp_w_cmd_sanitize;
		u32 last_time_cost;
	}sanitizeInfo;
#else
		u32 data[821];			   //3284
#endif
	u32 commit_ca3_fe[4];		//16
	epm_header_t header;	   //16k bytes
} epm_smart_t;				   //16k+16k
BUILD_BUG_ON(sizeof(epm_smart_t) != 32*1024);

//should be 32K align  two pl write
typedef struct _epm_namespace_t
{
	u32 epm_sign;
	u32 EPM_SN;
	u32 data[(16384 - 4 - 4) / 4];
	epm_header_t header; //16k bytes
} epm_namespace_t;		 //16k+16k

//should be 32K align  two pl write
typedef struct _epm_aes_t
{
	u32 epm_sign;
	u32 EPM_SN;
#if (_TCG_)

	u32 data[(16384 - 4 - 4 - 4*6) / 4];

	u32 tcg_en_dis_tag;
	u32 new_blk_prog;
	u16 readlocked;
	u16 writelocked;
	u32 tcg_sts;
	u32 prefmtted;
	u32 tcg_err_flag;

#else

	u32 data[(16384 - 4 - 4) / 4];

#endif

	epm_header_t header; //16k bytes
} epm_aes_t;			 //16k+16k

//should be 32K align  two pl write
typedef struct _epm_trim_t
{
	u32 epm_sign;
	u32 EPM_SN;
	u32 info[12];  // 48B
	u32 rsvd_al32[2]; // 8B
	u32 TrimTable[64 * 1024 / 4]; // 64k, 32k per 1TB
	u32 TrimBlkBitamp[256/4]; // 256 B
	u32 RESERVED[(15 * 1024 + 704)/4]; // 15 K + 704 B
	epm_header_t header; //16k bytes
} epm_trim_t;			 //80k+16k
BUILD_BUG_ON(sizeof(epm_trim_t) != 96*1024);
// BUILD_BUG_ON(offsetof(epm_trim_t, TrimTable) & 31 != 64);

//should be 32K align  two pl write
struct journal_tag
{
	u32 event_num;
	u32 system_time_hi;
	u32 system_time_lo;
	u32 event_id;
	u32 log_poh;
	u32 pc_cnt;
	u32 use_0;
	u32 use_1;
	//u32 use_2;
};

typedef struct _epm_journal_t
{
	u32 epm_sign;
	u32 EPM_SN;
	u32 journal_offset;  //record pointer offset
	u32 rsvd;
	struct journal_tag info[4096/4/8];
	u32 data[(16384-16-4096) / 4];
	epm_header_t header; //16k bytes
} epm_journal_t;		 //16k+16k

//should be 32K align  two pl write
typedef struct _epm_misc_t
{
	u32 epm_sign;
	u32 EPM_SN;
	u32 test_bit;
	u32 data[(16384 - 4 - 4 -4) / 4];
	epm_header_t header; //16k bytes
} epm_misc_t;			 //16k+16k

#define EPM_ERROR_WARN_SIZE ((1) + (6) + (6) + (8) + (2) + (2))//u32 base count

//should be 32K align  two pl write
typedef struct _epm_error_warn_t
{
	u32 epm_sign;
	u32 EPM_SN;

	//-----------init info--------------
	u16 need_init;
	u16 cur_save_idx;
	
	//------------basic info-------------
	u16 cur_save_type[2];
	u16 cur_update_cpu_id[2];
	u32 cur_power_cycle_cnt[2];
	u32 cur_temperature[2];//ts_tmt.cur_ts
	
	//-------------plp info--------------
	u16 record_CPU1_plp_step[2];
	u16 record_CPU2_plp_step[2];
	u32 record_cpu1_gpio_lr[2];
	u16 record_host_open_die[2];
	u16 record_host_next_die[2];
	//------------write info-------------
	u16 is_host_idle[2];
	u16 is_gcing[2];
	u32 cache_handle_cnt[2];
	u32 FICU_start[2];
	u32 FICU_done[2];

	//------------gc info---------------
	u32 cur_global_gc_mode[2];

	//------------front end info---------
	u32 cur_cpu_feedback[2];

	
	u32 data[(16384 - 4 - 4 - EPM_ERROR_WARN_SIZE * 4) / 4];
	epm_header_t header; //16k bytes
} epm_error_warn_t;		 //16k+16k
BUILD_BUG_ON(sizeof(epm_error_warn_t) != 32*1024);


//should be 32K align  two pl write
typedef struct _epm_tcg_info_t
{
	u32 epm_sign;
	u32 EPM_SN;
	u32 init_tag;
	u32 next_paa;
	u32 blk_erased[8];
	u32 vac[256];
	u32 l2p_tbl[8852+8852]; // 8852  *2 *4 = 70,816 = 4.x*16k
	u16 p2l_tbl[256][448];           // 256 *448 *2 = 14*16k
	u32 data[((16384 * 19) - (4 * 4) - (4 * 8) - (4 * 256) - (4 * 8852 * 2) - (2 * 256 * 448)) / 4];
	epm_header_t header; //16k bytes
} epm_tcg_info_t;		 //304k+16k

//for spor case : search the latest epm data
typedef struct _latest_epm_data_t
{
	u32 latest_epm_sn[EPM_sign_end];
	pda_t latest_epm_data_pda[EPM_sign_end];
	pda_t empty_pda; //for normal case double check
} latest_epm_data_t;

void epm_init();
void epm_init_pos(u32 epm_sign);
#if epm_remap_enable
void epm_init_remap();
#endif
pda_t scan_the_latest_epm_header();
u32 scan_the_latest_epm_data(latest_epm_data_t *latest_epm_data, u8 mode); // check spor case return epm_data sn value
u8 rebuild_epm_header(latest_epm_data_t latest_epm_data, latest_epm_data_t latest_epm_mirror_data);
void set_epm_data(u32 epm_sign);
void set_epm_header(u32 epm_sign, pda_t pda_header, pda_t pda_tail);
bool epm_header_load(pda_t pda_base);
epm_sub_header_t get_epm_newest_pda(u32 epm_sign);
void get_rd_epm_pda(pda_t *pda_base, pda_t *pda_list, u32 cnt);
void get_wr_epm_pda(pda_t *pda_base, pda_t *pda_list);
void get_rd_epm_header_pda(pda_t *pda_base, pda_t *pda_list, u32 cnt);
void get_wr_epm_header_pda(pda_t *pda_base, pda_t *pda_list);

void epm_flush_all(u8 flush_mode, pda_t *only_mirror_pda);
//void epm_flush(epm_pos_t* epm_pos, u32 epm_sign, u8 flush_mode, pda_t* only_mirror_pda);
void epm_flush_fast(u8 epm_sign_bit, u8 flush_mode, pda_t *only_mirror_pda);
void epm_update(u32 epm_sign, u32 cpu_id);
void journal_update(u16 evt_reason_id, u32 use_0);
void epm_header_flush(u8 flush_mode, pda_t only_mirror_pda, bool sn_update);
void epm_flush_done();
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
void slc_epm_flush_done();
#endif
void power_on_epm_flush_done();
void epm_flush_done_for_glist();
void epm_header_update();
void epm_read_all();
bool epm_read(u32 epm_sign, u8 read_mode);
void epm_erase(u32 area_tag);
epm_pos_t get_epm_pos(u32 epm_sign);
pda_t epm_gen_pda(u32 du, u8 pl, u8 ch, u8 ce, u8 lun, u32 pg, u32 blk);

void chk_pda(pda_t pda);  //for debug
void force_set_srb_rda(); //temp no programmer
u8 chk_epm_header_valid();
void dump_error_warn_info();

#if FRB_remap_enable
void epm_remap_tbl_flush(pda_t *pda_base);
#endif

static inline epm_pos_t *get_epm_pos_ptr(u32 epm_sign)
{
	extern epm_info_t *shr_epm_info;
	epm_pos_t *epm_pos = NULL;
	switch (epm_sign)
	{
	case FTL_sign:
		epm_pos = &shr_epm_info->epm_ftl;
		break;
	case GLIST_sign:
		epm_pos = &shr_epm_info->epm_glist;
		break;
	case SMART_sign:
		epm_pos = &shr_epm_info->epm_smart;
		break;
	case NAMESPACE_sign:
		epm_pos = &shr_epm_info->epm_namespace;
		break;
	case AES_sign:
		epm_pos = &shr_epm_info->epm_aes;
		break;
	case TRIM_sign:
		epm_pos = &shr_epm_info->epm_trim;
		break;
	case JOURNAL_sign:
		epm_pos = &shr_epm_info->epm_journal;
		break;
	/*case MISC_sign:
		epm_pos = &shr_epm_info->epm_misc;
		break;*/
	case ERROR_WARN_sign:
		epm_pos = &shr_epm_info->epm_error_warn_data;
		break;
	//case TCG_INFO_sign:
		//epm_pos = &shr_epm_info->epm_tcg_info;
		//break;
	case EPM_PLP1:
	case EPM_PLP2:
	case EPM_PLP_TEST1:
	case EPM_PLP_TEST2:
	case EPM_POR:
		src_inc_trace(LOG_ERR, 0x5049, "get_epm_pos_ptr powercycle case\n");
		break;
	default:
		panic("no this case\n");
	}
	return epm_pos;
}

static inline void epm_format_state_update(u32 state, u32 tag)
{
	extern epm_info_t *shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    epm_ftl_data->epm_fmt_not_finish = state;
    epm_ftl_data->format_tag = tag;
    src_inc_trace(LOG_INFO, 0xbf96, "epm_fmt_not_finish:0x%x, tag:0x%x", epm_ftl_data->epm_fmt_not_finish, epm_ftl_data->format_tag);
}


#if epm_spin_lock_enable
void get_epm_access_key(u32 cpu_id, u32 epm_sign);
void unlock_epm_ddr(u32 epm_sign, u32 cpu_id);
static inline void set_clr_ddr_set_done(bool set, u8 cpu_id, u32 epm_sign)
{
	if (epm_sign > EPM_sign_end)
	{
		src_inc_trace(LOG_ERR, 0x3713, "set_clr_ddr_set_done epm_sign[%d]>EPM_sign_end\n", epm_sign);
		return;
	}
	if (epm_debug)
		src_inc_trace(LOG_ERR, 0x4923, "set_clr_ddr_set_done set[%d],cpu_id[%d],epm_sign[%d]\n", set, cpu_id, epm_sign);
	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);
	if (set)
		epm_pos->set_ddr_done[cpu_id] = 1;
	else
		epm_pos->set_ddr_done[cpu_id] = 0;
}

static inline void check_and_set_ddr_lock(u32 epm_sign)
{
	if (epm_sign > EPM_sign_end)
	{
		src_inc_trace(LOG_ERR, 0xada8, "check_and_set_ddr_lock epm_sign[%d]>EPM_sign_end\n", epm_sign);
		return;
	}
	src_inc_trace(LOG_ERR, 0xd5e7, "check_and_set_ddr_lock epm_sign=%d cpuid=%d\n", epm_sign, (CPU_ID - 1));
	get_epm_access_key((CPU_ID - 1), epm_sign);
	epm_pos_t *epm_pos = get_epm_pos_ptr(epm_sign);

	if (epm_debug)
		src_inc_trace(LOG_ERR, 0x9857, "check_and_set_ddr_lock before while epm_sign[%d],cpuid[%d],busy_lock[%d],key_num[%d],cur_key[%d]\n", epm_sign, (CPU_ID - 1), epm_pos->busy_lock, epm_pos->key_num[(CPU_ID - 1)], epm_pos->cur_key);

	while ((epm_pos->busy_lock == 1) || (epm_pos->key_num[(CPU_ID - 1)] != epm_pos->cur_key) || (epm_pos->key_num[(CPU_ID - 1)] == 0))
	{
	};

	if (epm_debug)
		src_inc_trace(LOG_ERR, 0x1cd4, "check_and_set_ddr_lock : cpuid[%d],epm_sign[%d],key_num[%d],cur_key[%d]  unlock to set lock\n", (CPU_ID - 1), epm_sign, epm_pos->key_num[(CPU_ID - 1)], epm_pos->cur_key);

	epm_pos->key_num[(CPU_ID - 1)] = 0;
	set_clr_ddr_set_done(false, (CPU_ID - 1), epm_sign);
	epm_pos->busy_lock = 1;
}

#endif
#undef __FILEID__
