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
/*! \file ftl_p2l.c
 * @brief define p2l behavior in ftl core, included initilization, update, flush, load, recover
 *
 * \addtogroup fcore
 * \defgroup p2l
 * \ingroup fcore
 * @{
 *
 */

#include "types.h"
#include "queue.h"
#include "bf_mgr.h"
#include "dtag.h"
#include "ftl_p2l.h"
#include "assert.h"
#include "bitops.h"
#include "ncl_exports.h"
#include "ipc_api.h"
#include "string.h"
#include "console.h"
#include "event.h"
#include "ftl_core.h"
#include "ftl_remap.h"
#include "idx_meta.h"
#include "l2cache.h"
#include "die_que.h"
#include "../ncl_20/nand.h"

#define __FILEID__ ptol
#include "trace.h"

#define PGSN_CNT_PER_DU     (512)

fast_data_zi p2l_para_t p2l_para;
share_data_zi volatile p2l_ext_para_t p2l_ext_para;
share_data_zi volatile u32 shr_p2l_grp_cnt; //ray
share_data_zi volatile u32 shr_wl_per_p2l;
share_data_zi u32 _max_capacity;
fast_data_zi p2l_build_pending_list_t p2l_build_pending_list;
static fast_data_zi p2l_mgr_res_t p2l_res_mgr;
static slow_data_zi p2l_load_mgr_t p2l_load_mgr;
//static slow_data_zi np2l_build_mgr_t p2l_build_mgr;
//static fast_data_zi log_level_t old_log_lvl;
slow_data_zi u16 _p2l_build_free_cmd_bmp;
slow_data_zi u32 p2l_build_meta_idx;	   // frl spor ddr meta index
slow_data_zi struct du_meta_fmt *p2l_build_meta; // frl spor ddr meta addr

slow_data_ni struct ncl_cmd_t    *p2l_build_ncl_cmd;  // 80B
slow_data_zi struct info_param_t *p2l_build_info;     // 4B
slow_data_zi bm_pl_t             *p2l_build_bm_pl;    // 8B
slow_data_zi pda_t               *p2l_build_pda_list; // 4B
slow_data_ni u32                 p2l_build_p2l_id;
slow_data_zi bool                p2l_build_flag;
slow_data_zi dtag_t              p2l_build_dtag;
slow_data_zi u8                  p2l_build_dtag_cnt;
fast_data_zi pending_p2l_build_t *tmp_pend;
slow_data_zi u16                 p2l_build_cur_spb_id;
slow_data_zi u16                 blank_cnt;

fast_data_zi p2l_ncl_cmd_t *p2l_build_ncl;
fast_data_zi p2l_load_req_t *p2l_build_load;
fast_data_zi bool p2l_build_plp_trigger_done; 


extern u32 rd_dummy_meta_idx;

extern u32 wr_dummy_meta_idx;
slow_data_zi u32 p2l_dummy_dtag;
slow_data_zi pda_t p2l_b_pda, p2l_b_cpda, p2l_b_cpda_end, p2l_b_pda_start;
slow_data_zi bool p2l_uart_flag = false;
slow_data_zi bool load_all_p2l = false;

extern enum du_fmt_t host_du_fmt;
extern u16 gc_gen_p2l_num;

share_data epm_info_t*  shr_epm_info;

#define P2L_FIXED_DIE 1
u64 global_page_sn = 0;

#if (PLP_FORCE_FLUSH_P2L == mENABLE)
extern volatile spb_id_t plp_spb_flush_p2l;
#endif

/*!
 * @brief p2l load complete callback
 *
 * @param ncl_cmd	ncl command to load p2l
 *
 * @return	not used
 */
static void p2l_load_done(struct ncl_cmd_t *ncl_cmd);
static ddr_code bool p2l_build_dbg_cmpl(void);

/*!
 * @brief scan meta to build p2l
 *
 * @param ncl_cmd	ncl command to load p2l
 *
 * @return	not used
 */
//static void p2l_build_scan_meta(void);

init_code void p2l_para_init(void)
{
	u32 intlv_du_cnt = nand_interleave_num() * DU_CNT_PER_PAGE;

	sys_assert(DU_CNT_PER_P2L % intlv_du_cnt == 0);
	p2l_para.pg_per_p2l = DU_CNT_PER_P2L / intlv_du_cnt;
	p2l_para.pg_p2l_shift = ctz(p2l_para.pg_per_p2l);
	p2l_para.pg_p2l_mask = BIT(p2l_para.pg_p2l_shift) - 1;

	sys_assert(1024 % nand_info.lun_num == 0);
	//p2l_para.pg_per_grp = 3;            //each grp contains 1 wl
	p2l_para.pg_per_grp = 3 * p2l_para.pg_per_p2l;            //each grp contains 1 wl
	shr_wl_per_p2l  = p2l_para.pg_per_p2l;
	shr_p2l_grp_cnt = shr_nand_info.geo.nr_pages / p2l_para.pg_per_grp; 
	//p2l_para.pg_grp_shift = ctz(p2l_para.pg_per_grp);
	//p2l_para.pg_grp_mask = BIT(p2l_para.pg_grp_shift) - 1;

	p2l_para.p2l_per_grp = 3;           //128 die / grp, need 3 du to store p2l table
	//p2l_para.p2l_grp_shift = ctz(p2l_para.p2l_per_grp);
	//p2l_para.p2l_grp_mask = BIT(p2l_para.p2l_grp_shift) - 1;

	sys_assert(nand_page_num() %  p2l_para.pg_per_p2l == 0);
	sys_assert(nand_page_num_slc() %  p2l_para.pg_per_p2l == 0);
	p2l_para.xlc_p2l_cnt = occupied_by(nand_page_num(), p2l_para.pg_per_p2l);
	p2l_para.xlc_grp_cnt = occupied_by(p2l_para.xlc_p2l_cnt, p2l_para.p2l_per_grp);
	p2l_para.slc_p2l_cnt = occupied_by(nand_page_num_slc(), p2l_para.pg_per_p2l);
	p2l_para.slc_grp_cnt = occupied_by(p2l_para.slc_p2l_cnt, p2l_para.p2l_per_grp);

	p2l_ext_para.p2l_per_grp = p2l_para.p2l_per_grp;
	p2l_ext_para.slc_p2l_cnt = p2l_para.slc_p2l_cnt;
	p2l_ext_para.slc_grp_cnt = p2l_para.slc_grp_cnt;
	p2l_ext_para.xlc_p2l_cnt = p2l_para.xlc_p2l_cnt;
	p2l_ext_para.xlc_grp_cnt = p2l_para.xlc_grp_cnt;
}

init_code void p2l_res_init(void)
{
	p2l_res_mgr.avail = MAX_P2L_CNT;
	p2l_res_mgr.total = MAX_P2L_CNT;
    p2l_res_mgr.ddtag_start = DDR_P2L_DTAG_START;//ddr_dtag_register(MAX_P2L_CNT);
	p2l_res_mgr.dbg_ddtag_start = ddr_dtag_register(p2l_para.p2l_per_grp);
	p2l_res_mgr.dbg_p2l_build_ddtag_start = ddr_dtag_register(p2l_para.p2l_per_grp);

	memset(p2l_res_mgr.bmp, 0, sizeof(p2l_res_mgr.bmp));

	u32 i;
	for (i = 0; i < MAX_P2L_CNT; i++) {
		p2l_t *p2l = &p2l_res_mgr.p2l[i];
		p2l->ddtag.dtag = (p2l_res_mgr.ddtag_start + i) | DTAG_IN_DDR_MASK;
		p2l->dmem = (lda_t*)ddtag2mem(p2l->ddtag.b.dtag);
		p2l->flags.all = 0;
	}

	bool ret;
	dtag_t sdtag;

	CBF_INIT(&p2l_res_mgr.sdtag_pool);
	for (i = 0; i < MAX_P2L_SDTAG_CNT; i++) {
		sdtag = dtag_get(DTAG_T_SRAM, NULL);
		sys_assert(sdtag.dtag != _inv_dtag.dtag);

		CBF_INS(&p2l_res_mgr.sdtag_pool, ret, sdtag);
		sys_assert(ret == true);
	}
}

init_code void p2l_load_init(void)
{
	u32 i;
	for (i = 0; i < TTL_P2L_LOAD_CMD; i++) {
		p2l_ncl_cmd_t *p2l_cmd = &p2l_load_mgr.cmd[i];
		struct ncl_cmd_t *ncl_cmd = &p2l_cmd->ncl_cmd;

		ncl_cmd->user_tag_list = p2l_cmd->bm_pl;
		ncl_cmd->addr_param.common_param.pda_list = p2l_cmd->pda;
		ncl_cmd->addr_param.common_param.info_list = p2l_cmd->info;

		ncl_cmd->status = 0;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
        ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE; 
#if defined(HMETA_SIZE)
        ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;
        ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG; // Jamie 20210311
#else
        ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
        ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
#endif
        #if RAID_SUPPORT_UECC
        ncl_cmd->flags |= NCL_CMD_P2L_READ_FLAG;
        #endif
        #if NCL_FW_RETRY
        ncl_cmd->retry_step = default_read;
        #endif
		ncl_cmd->completion = p2l_load_done;

	}

	p2l_load_mgr.avail = TTL_P2L_LOAD_CMD;
	p2l_load_mgr.bmp = ~(BIT(TTL_P2L_LOAD_CMD) - 1);
}

slow_code void p2l_build_init(void)
{
//	np2l_build_mgr_t *build_mgr = &p2l_build_mgr;
////	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
//	u32 ttl_du_cnt = mp_du_cnt * MAX_P2L_SCAN_CMD;
//
//	memset(build_mgr, 0, sizeof(np2l_build_mgr_t));
////	INIT_LIST_HEAD(&build_mgr->wait_que);
//    p2l_build_meta = idx_meta_allocate((P2L_BUILD_NCL_CMD_MAX*DU_CNT_PER_PAGE), DDR_IDX_META, &p2l_build_meta_idx);
    ftl_core_trace(LOG_ALW, 0xb804, "p2l_build_meta : 0x%x, p2l_build_meta_idx : 0x%x", p2l_build_meta, p2l_build_meta_idx);


//	dtag_t dtag = {.dtag = (ddr_dtag_register(1) | DTAG_IN_DDR_MASK)};
//	build_mgr->meta = idx_meta_allocate(ttl_du_cnt, DDR_IDX_META, &build_mgr->meta_idx);
//
//	u32 i, j;
//	for (i = 0; i < 32; i++) {
//		u32 meta_idx = build_mgr->meta_idx + i * DU_CNT_PER_PAGE;
//		p2l_ncl_cmd_t *scan_cmd = &build_mgr->scan_cmd[i];
//		struct ncl_cmd_t *ncl_cmd = &scan_cmd->ncl_cmd;
//		bm_pl_t *bm_pl = scan_cmd->bm_pl;
//
//		for (j = 0; j < DU_CNT_PER_PAGE; j++) {
//			bm_pl[j].pl.btag = i;
//			bm_pl[j].pl.du_ofst = j;
//			bm_pl[j].pl.dtag = dtag.dtag;
//			bm_pl[j].pl.nvm_cmd_id = meta_idx + j;
//			bm_pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
//		}
//
//		ncl_cmd->user_tag_list = scan_cmd->bm_pl;
//		ncl_cmd->addr_param.common_param.list_len = DU_CNT_PER_PAGE;
//		ncl_cmd->addr_param.common_param.pda_list = scan_cmd->pda;
//		ncl_cmd->addr_param.common_param.info_list = scan_cmd->info;
//
//		ncl_cmd->status = 0;
//		ncl_cmd->op_code = NCL_CMD_P2L_SCAN_PG_AUX;
//		ncl_cmd->flags = NCL_CMD_TAG_EXT_FLAG;
//		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
//		ncl_cmd->op_type = NCL_CMD_FW_TABLE_READ_PA_DTAG;
//
//	}

    u32 cnt_1 = sizeof(*p2l_build_ncl_cmd)*P2L_BUILD_NCL_CMD_MAX;
    u32 cnt_2 = sizeof(*p2l_build_pda_list)*P2L_BUILD_NCL_CMD_MAX*DU_PER_WL*shr_nand_info.geo.nr_planes;
    u32 cnt_3 = sizeof(*p2l_build_bm_pl)*P2L_BUILD_NCL_CMD_MAX*DU_PER_WL*shr_nand_info.geo.nr_planes;
    u32 cnt_4 = sizeof(*p2l_build_info)*P2L_BUILD_NCL_CMD_MAX*DU_PER_WL*shr_nand_info.geo.nr_planes;
    u32 tot_length = cnt_1 + cnt_2 + cnt_3 + cnt_4;
    p2l_build_dtag_cnt = (tot_length >> DTAG_SHF) + ((tot_length & DTAG_MSK)?1:0);

    ftl_core_trace(LOG_ALW, 0x85c2, "ncl_cmd size: 0x%x, pda_list size: 0x%x, bm_pl size: 0x%x, info size: 0x%x, tot_size 0x%x, dtag_cnt %u",
                  cnt_1, cnt_2, cnt_3, cnt_4, tot_length, p2l_build_dtag_cnt);
	p2l_build_dtag = dtag_cont_get(DTAG_T_SRAM, p2l_build_dtag_cnt);
    
	sys_assert(p2l_build_dtag.dtag != _inv_dtag.dtag);
    u8 * add = dtag2mem(p2l_build_dtag);

    memset(add, 0, DTAG_SZE * p2l_build_dtag_cnt);

    p2l_build_ncl_cmd  = (struct ncl_cmd_t *)(add);
    add += cnt_1;
    p2l_build_pda_list = (pda_t *)(add);
    add += cnt_2;
    p2l_build_bm_pl    = (bm_pl_t *)(add);
    add += cnt_3;
    p2l_build_info     = (struct info_param_t *)(add);
    p2l_build_flag = true;
    // for share memory usage test
    ftl_core_trace(LOG_ALW, 0x967d, "ncl_cmd addr: 0x%x, pda_list addr: 0x%x, bm_pl addr: 0x%x, info addr: 0x%x",
                  p2l_build_ncl_cmd, p2l_build_pda_list, p2l_build_bm_pl, p2l_build_info);

    ftl_core_trace(LOG_ALW, 0xec4f, "p2l_build_flag: %d", p2l_build_flag);
}

init_code void p2l_build_pending_init(void)
{
        //==============build p2l pendinig*====================
        p2l_build_pending_list.pending_p2l_build_buf = (pending_p2l_build_t *)ddtag2mem(ddr_dtag_register(((sizeof(pending_p2l_build_t)*P2L_PENDING_CNT_MAX)/4096)+1));
        evlog_printk(LOG_INFO, "pending_p2l_build_tbl size : %d, addr : 0x%x", (sizeof(pending_p2l_build_t)*P2L_PENDING_CNT_MAX), p2l_build_pending_list.pending_p2l_build_buf);
        sys_assert(p2l_build_pending_list.pending_p2l_build_buf);
        p2l_build_pending_list.p2l_build_pend_cnt = 0;
        p2l_build_pending_list.p2l_build_tmp_cnt = 0;
        memset(p2l_build_pending_list.pending_p2l_build_buf, 0, (sizeof(pending_p2l_build_t)*P2L_PENDING_CNT_MAX));
        
        p2l_dummy_dtag = ddr_dtag_register(1);
        p2l_build_meta = idx_meta_allocate((P2L_BUILD_NCL_CMD_MAX*DU_CNT_PER_PAGE), DDR_IDX_META, &p2l_build_meta_idx);
        p2l_build_flag = false;
		_p2l_build_free_cmd_bmp = INV_U16;
        //==============build p2l pendinig&====================
}

slow_code void p2l_build_meta_init(void)
{
        p2l_dummy_dtag = ddr_dtag_register(1);
        p2l_build_meta = idx_meta_allocate((P2L_BUILD_NCL_CMD_MAX*DU_CNT_PER_PAGE), DDR_IDX_META, &p2l_build_meta_idx);
}


init_code void p2l_init(void)
{
	p2l_para_init();
	p2l_res_init();
	p2l_load_init();
//	p2l_build_init();
//    p2l_dummy_dtag = ddr_dtag_register(1);
//    p2l_build_meta = idx_meta_allocate((P2L_BUILD_NCL_CMD_MAX*DU_CNT_PER_PAGE), DDR_IDX_META, &p2l_build_meta_idx);
    p2l_build_pending_init();
	p2l_build_meta_init();
//    p2l_build_flag = false;
}

fast_code void p2l_user_init(p2l_user_t *pu, u32 nsid)
{
	u32 i, j, c;
	for (i = 0; i < GRP_PER_P2L_USR; i++) {
        for (c = 0; c < MAX_PGSN_TABLE_CNT; c++) {
            pu->grp[i].pgsn[c] = NULL;
        }
        pu->grp[i].pgsn_ofst = 0;
        pu->grp[i].pda = NULL;
		pu->grp[i].grp_id = ~0;
        pu->grp[i].ttl_p2l_cnt = 0;
		pu->grp[i].ttl_pgsn_cnt = 0;
		pu->grp[i].cur_p2l_cnt = 0;
		pu->grp[i].updt_p2l_cnt = 0;
		for (j = 0; j < p2l_para.p2l_per_grp; j++)
			pu->grp[i].res_id[j] = GRP_FREE_RES_ID;
	}
}

fast_code void p2l_get_grp_pda(u32 grp_id, u32 spb_id, u32 nsid, pda_t* p2l_pda, pda_t* pgsn_pda)
{
    //u32 p2l_page = (grp_id + 1) * nand_info.bit_per_cell;
    u32 p2l_page = (grp_id + 1) * nand_info.bit_per_cell * p2l_para.pg_per_p2l;//adams  64 die one for 2 WL
    u32 p2l_die;
    u32 parity_die = nand_info.lun_num;
    u8 ttl_good_pl_cnt = nand_plane_num();
    bool parity_mix = true;
    bool last_p2l = false;
    u8 p2l_plane = 0;
    u8 pgsn_plane = 0;
    u32 pgsn_page;
#ifdef SKIP_MODE
	bool find_p2l_plane = false; 
#else
	u32 plane_info;
#endif

    //if ((grp_id + 1) * nand_info.bit_per_cell >= nand_page_num()) {
    if ((grp_id + 1) * nand_info.bit_per_cell * p2l_para.pg_per_p2l >= nand_page_num()) {//adams  64 die one for 2 WL
        p2l_die = nand_info.lun_num - 1;
        p2l_page = nand_page_num() - 1;
        last_p2l = true;
    } else
    {
        p2l_die = grp_id & ((nand_info.lun_num >> 1) - 1);
    }

#ifdef While_break
	u64 start = get_tsc_64();
#endif		
	
    //calculate parity die 
#ifdef SKIP_MODE
    u8* df_ptr =  get_spb_defect(spb_id);
#endif
    if(fcns_raid_enabled(nsid)) {
        parity_die --;
        //get parity die
        while (1) {
#ifdef SKIP_MODE
			if (BLOCK_ALL_BAD != get_defect_pl_pair(df_ptr, parity_die*nand_plane_num()))
#else
			if(1)
#endif
			{
                break;
            }
            sys_assert(parity_die != 0);
            parity_die--;

#ifdef While_break		
			if(Chk_break(start,__FUNCTION__, __LINE__))
				break;
#endif			
        }
        ttl_good_pl_cnt = nand_plane_num() - pop32(get_defect_pl_pair(df_ptr, parity_die*nand_plane_num()));
        if(ttl_good_pl_cnt == 1){
            parity_mix = false;
        }
    }

#ifdef While_break
start = get_tsc_64();
#endif		

    while (1) //check bad block
    {
        if(fcns_raid_enabled(nsid)) {
            if (p2l_die == parity_die
                && (!parity_mix)){
                if (last_p2l == false)
                    p2l_die = (p2l_die + 1) & ((nand_info.lun_num >> 1) - 1);
                else
                    p2l_die--;
                continue;
            }
        }

#ifdef SKIP_MODE
		u32 pl;
		u32 bad_pln_cnt = 0;
		u32 good_pln_array[2] = {0};
		u32 good_pln_cnt = 0; 
		u32 max_good_pln_cnt = 2;

#if RAID_SUPPORT
        bool skip_parity_plane = false;
        if(p2l_die == parity_die){
            skip_parity_plane = true;
            if(ttl_good_pl_cnt == 2){
                max_good_pln_cnt = 1;
            }
        }
#endif

		if (last_p2l == false){
			for (pl = 0; pl < nand_info.geo.nr_planes; pl++){
				u32 iid = (p2l_die*nand_plane_num()) + pl;
				u32 idx = iid >> 3;
				u32 off = iid & 7;
				if (((df_ptr[idx] >> off)&1)==0){ //good plane
#if RAID_SUPPORT
				    if(skip_parity_plane){
                        skip_parity_plane = false;
                        continue;
                    }
#endif
					good_pln_array[good_pln_cnt++] = pl;
					find_p2l_plane = true;
					if(good_pln_cnt == max_good_pln_cnt)
						break;
				}
				else
					bad_pln_cnt++;
			}
		}
		else{
			for (pl = nand_info.geo.nr_planes; pl > 0; pl--){
				u32 iid = (p2l_die*nand_plane_num()) + (pl - 1);
				u32 idx = iid >> 3;
				u32 off = iid & 7;
				if (((df_ptr[idx] >> off)&1)==0){ //good plane
					good_pln_array[good_pln_cnt++] = (pl - 1);
					find_p2l_plane = true;
					if(good_pln_cnt == max_good_pln_cnt)
						break;
				}
				else
					bad_pln_cnt++;
			}
		}

		if (good_pln_cnt == 2){
			if(last_p2l){
				p2l_plane = good_pln_array[1];
				pgsn_plane = good_pln_array[0];
			}else{
				p2l_plane = good_pln_array[0];
				pgsn_plane = good_pln_array[1];
			}
		}
		else if(good_pln_cnt == 1)
			p2l_plane = pgsn_plane = good_pln_array[0];


		if (find_p2l_plane)
			break;

		if (bad_pln_cnt == nand_info.geo.nr_planes){
		    if (last_p2l == false)
	            p2l_die = (p2l_die+1) & ((nand_info.lun_num >> 1) - 1);
	        else
	            p2l_die--;
		}
#else
		plane_info = BLOCK_NO_BAD;
        if (BLOCK_ALL_BAD != plane_info) {
            if (plane_info == BLOCK_NO_BAD) {
                if (p2l_die == parity_die) {
                    p2l_plane = pgsn_plane = 0;
                } else {
                    p2l_plane = 0;
                    pgsn_plane = 1;
                }
            }else if (plane_info == BLOCK_P0_BAD) {
                p2l_plane = pgsn_plane = 1;
            } else {
                p2l_plane = pgsn_plane = 0;
            }
            if (nand_plane_num() == 4) {  // adams 4P last P2L
                if(p2l_die == parity_die) {
                    p2l_plane = 1;  // nand_plane_num() - 3
                    pgsn_plane = 2; // nand_plane_num() - 3 + 1
                } else {
                    p2l_plane = 2;  // nand_plane_num() - 2
                    pgsn_plane = 3; // nand_plane_num() - 2 + 1
                }
            }
            break;
        }

        if (last_p2l == false)
            p2l_die = (p2l_die + 1) & ((nand_info.lun_num >> 1) - 1);
        else
            p2l_die--;

#endif

        if (p2l_die == nand_info.lun_num) {
            p2l_die = 0;
            sys_assert(0);
        }

#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif		
    }

    if (pgsn_plane == p2l_plane) {
        if (last_p2l) {
            pgsn_page = p2l_page - 1;
        } else {
            pgsn_page = p2l_page + 1;
        }
    } else {
        pgsn_page = p2l_page;
    }


    //combine pda
    u32 interleave = p2l_die * nand_plane_num() + p2l_plane;
    *p2l_pda = nal_make_pda(spb_id, (interleave<<nand_info.pda_interleave_shift) + (p2l_page << nand_info.pda_page_shift));
    interleave = p2l_die * nand_plane_num() + pgsn_plane;
    *pgsn_pda = nal_make_pda(spb_id, (interleave<<nand_info.pda_interleave_shift) + (pgsn_page << nand_info.pda_page_shift));
#if 0
    ftl_core_trace(LOG_DEBUG, 0x09c2, "p2l grp:%d get p2l pda:%x(die:%d page:%d plane:%d)", grp_id, p2l_pda, p2l_die, p2l_page,
        plane);
#endif
    //return p2l_pda;
}

fast_code void p2l_get_next_pos(aspb_t *aspb, p2l_user_t *pu, u32 nsid)
{	
	/*TODO: decide p2l die, p2l plane, pgsn plane, p2l page, pgsn page*/
    u16 cur_wl = (aspb->wptr / nand_info.interleave) / nand_info.bit_per_cell;
    //u16 p2l_page = (cur_wl + 1) * nand_info.bit_per_cell;
    u16 p2l_page = (cur_wl + p2l_para.pg_per_p2l) * nand_info.bit_per_cell;//adams  64 die one for 2 WL
    u16 p2l_die = (cur_wl / p2l_para.pg_per_p2l) & ((nand_info.lun_num >> 1) - 1);  //(p2l die 0 -> 63)
    bool last_p2l = false;
    u8 p2l_plane = 0;
    u8 pgsn_plane = 0;
#ifdef SKIP_MODE	
	bool find_p2l_plane = false; 
#else
	u8 bad_pln;
#endif	

    if (p2l_page >= nand_page_num()) {
        p2l_page = nand_page_num() - 1;
        p2l_die = nand_info.lun_num - 1;
        last_p2l = true;
    }

    u16 p2l_die_start = p2l_die;

#ifdef While_break
	u64 start = get_tsc_64();
#endif	

#ifdef SKIP_MODE
        u8* df_ptr =  get_spb_defect(aspb->spb_id);
#endif
    while(1) {
		u32 max_good_pln_cnt = 2; 
         
#if RAID_SUPPORT 
        //check p2l die is parity die
        if(fcns_raid_enabled(nsid)) {
            if (p2l_die == aspb->parity_die 
                && (!aspb->flags.b.parity_mix)) {
                if (last_p2l == false)
                    p2l_die = (p2l_die+1) & ((nand_info.lun_num >> 1) - 1);
                else
                    p2l_die--;
                continue;
            }

        }

        bool skip_parity_plane = false;
        if(p2l_die == aspb->parity_die){
            skip_parity_plane = true;
            if(nand_plane_num() - pop32(get_defect_pl_pair(df_ptr, aspb->parity_die*nand_plane_num())) == 2){ 
                max_good_pln_cnt = 1; 
            } 
        } 
#endif 

        /*check p2l die exist bad block*/
#ifdef SKIP_MODE
		u32 pl;
		u32 bad_pln_cnt = 0;
		u32 good_pln_array[2] = {0};
		u32 good_pln_cnt = 0; 
		if (last_p2l == false){
			for (pl = 0; pl < nand_info.geo.nr_planes; pl++){
				u32 iid = (p2l_die*nand_plane_num()) + pl;
				u32 idx = iid >> 3;
				u32 off = iid & 7;
				if (((df_ptr[idx] >> off)&1)==0){ //good plane
#if RAID_SUPPORT
                    if(skip_parity_plane){
                        skip_parity_plane = false;
                        continue;
                    }
#endif
					good_pln_array[good_pln_cnt++] = pl;
					find_p2l_plane = true;
					if(good_pln_cnt == max_good_pln_cnt)
						break;
				}
				else
					bad_pln_cnt++;
			}
		}
		else{ 
			for (pl = nand_info.geo.nr_planes ; pl > 0; pl--){
				u32 iid = (p2l_die*nand_plane_num()) + (pl - 1);
				u32 idx = iid >> 3;
				u32 off = iid & 7;
				if (((df_ptr[idx] >> off)&1)==0){ //good plane
					good_pln_array[good_pln_cnt++] = (pl - 1);
					find_p2l_plane = true;
					if(good_pln_cnt == max_good_pln_cnt)
						break;
				}
				else{
					bad_pln_cnt++;
				}
			}
		}

		if (good_pln_cnt == 2){
			if(last_p2l){ //no bad plane, p2l_plane = 2 ,pgsn_plane = 3
				p2l_plane = good_pln_array[1];
				pgsn_plane = good_pln_array[0];
			}else{
				p2l_plane = good_pln_array[0];
				pgsn_plane = good_pln_array[1];
			}
		}
		else if(good_pln_cnt == 1)
			p2l_plane = pgsn_plane = good_pln_array[0];


		if (find_p2l_plane)
			break;

		if (bad_pln_cnt == nand_info.geo.nr_planes){
		    if (last_p2l == false)
	            p2l_die = (p2l_die+1) & ((nand_info.lun_num >> 1) - 1);
	        else
	            p2l_die--;
		}	
#else
		bad_pln = BLOCK_NO_BAD;
        if (bad_pln != BLOCK_ALL_BAD) {
            if (bad_pln == BLOCK_NO_BAD) {
                if (p2l_die == aspb->parity_die) {
                    p2l_plane = pgsn_plane = 0;
                } else {
                    p2l_plane = 0;
                    pgsn_plane = 1;
                }
            }else if (bad_pln == BLOCK_P0_BAD) {
                p2l_plane = pgsn_plane = 1;
            } else {
                p2l_plane = pgsn_plane = 0;
            }
            if (nand_plane_num() == 4) {  // adams 4P last P2L
            	if (last_p2l) { 
    	          p2l_plane = 2;  // nand_plane_num() - 2
                pgsn_plane = 3; // nand_plane_num() - 2 + 1
              }
            }
            break;
        }


        if (last_p2l == false)
            p2l_die = (p2l_die+1) & ((nand_info.lun_num >> 1) - 1);
        else
            p2l_die--;

#endif

        sys_assert(p2l_die_start != p2l_die);
        if (p2l_die == nand_info.lun_num) {
            p2l_die = 0;
            sys_assert(last_p2l == false);
        }

#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif
    }
    aspb->p2l_die = p2l_die;
    aspb->p2l_page = p2l_page;
    if (p2l_plane == pgsn_plane) {  //bad plane exist, p2l and pgsn locate in different page
        if (last_p2l == false)
            aspb->pgsn_page = p2l_page + 1;
        else
            aspb->pgsn_page = p2l_page - 1;
    } else {                        //no bad plane, p2l and pgsn locate in the same page
        aspb->pgsn_page = aspb->p2l_page;
    }
    aspb->p2l_plane = p2l_plane;
    aspb->pgsn_plane = pgsn_plane;
    sys_assert(aspb->pgsn_page < nand_page_num());
    pda_t p2l_pda, pgsn_pda;
    //p2l_get_grp_pda(cur_wl, aspb->spb_id, nsid, &p2l_pda, &pgsn_pda);
    p2l_get_grp_pda(cur_wl / p2l_para.pg_per_p2l, aspb->spb_id, nsid, &p2l_pda, &pgsn_pda);  // adams p2l grp_id = cur_wl (128die), = cur_wl / 2 (64 die)
    sys_assert(aspb->p2l_plane == pda2plane(p2l_pda));
    sys_assert(aspb->p2l_die == pda2die(p2l_pda));
    sys_assert(aspb->p2l_page == pda2page(p2l_pda));
    if (cur_wl != 0)
        aspb->p2l_grp_id++;
    #if 0
    ftl_core_trace(LOG_ERR, 0x12a7, "aspb:%d wptr:%x p2l die:%d p2l page:%d grpid:%d\n", aspb->spb_id, aspb->wptr, p2l_die,
        p2l_page, aspb->p2l_grp_id);
    #endif
}

fast_code void p2l_user_reset(p2l_user_t *pu, u32 nsid)
{
	u32 i;
	u32 j;

	for (i = 0; i < GRP_PER_P2L_USR; i++) {
		p2l_grp_t *p2l_grp = &pu->grp[i];

		for (j = 0; j < p2l_para.p2l_per_grp; j++) {
			if (p2l_grp->res_id[j] == GRP_FREE_RES_ID) {
				p2l_grp->ttl_p2l_cnt = j;
				break;
			} else {
				u32 res_id = p2l_grp->res_id[j];
				p2l_t *p2l = &p2l_res_mgr.p2l[res_id];
				sys_assert(p2l->flags.b.sdtag_flush == false);
			}
		}
		p2l_grp_nrm_done(p2l_grp, true);
	}
	p2l_user_init(pu, nsid);
}

static inline u32 pda_to_p2l_id(pda_t pda)
{
	return pda2page(pda) >> p2l_para.pg_p2l_shift;
}

static inline u32 p2l_res_id_get(void)
{
	u32 res_id = find_first_zero_bit(p2l_res_mgr.bmp, p2l_res_mgr.total);
	sys_assert(res_id < p2l_res_mgr.total);
	set_bit(res_id, p2l_res_mgr.bmp);
	p2l_res_mgr.avail--;

	return res_id;
}

static inline void p2l_res_id_put(u32 res_id)
{
	bool ret = test_and_clear_bit(res_id, p2l_res_mgr.bmp);
	sys_assert(ret == 1);
	p2l_res_mgr.avail++;
}

fast_code u32 p2l_get_grp_last_page(u32 grp_id, bool slc)
{
	u32 spb_last_page = nand_page_num() - 1;

	if (slc)
		spb_last_page = nand_page_num_slc() - 1;

	u32 grp_last_page = (grp_id * p2l_para.pg_per_grp) + p2l_para.pg_per_grp - 1;
	if (grp_last_page > spb_last_page)
		grp_last_page = spb_last_page;

	return grp_last_page;
}

fast_code u32 p2l_get_grp_p2l_cnt(u32 grp_id, bool slc)
{
	u32 grp_p2l_cnt = p2l_para.p2l_per_grp;
	u32 first_p2l_id = grp_id * p2l_para.p2l_per_grp;
	u32 spb_p2l_cnt = (slc) ? p2l_para.slc_p2l_cnt : p2l_para.xlc_p2l_cnt;

	if (first_p2l_id + grp_p2l_cnt > spb_p2l_cnt)
		grp_p2l_cnt = spb_p2l_cnt - first_p2l_id;

	return grp_p2l_cnt;
}

fast_code u32 p2l_get_last_page(u32 p2l_id, bool slc)
{
	u32 spb_last_page = nand_page_num() - 1;
	if (slc)
		spb_last_page = nand_page_num_slc() - 1;

	u32 p2l_last_page = (p2l_id << p2l_para.pg_p2l_shift) + p2l_para.pg_per_p2l - 1;
	if (p2l_last_page > spb_last_page)
		p2l_last_page = spb_last_page;

	return p2l_last_page;
}

fast_code p2l_t* pda_buffer_get(p2l_user_t *pu, u32 p2l_id, bool slc)
{
    p2l_t* p2l;
    p2l_grp_t *grp = NULL;
    u32 grp_id = p2l_id / p2l_para.p2l_per_grp;

    if (pu->grp[0].grp_id == grp_id)
        grp = &pu->grp[0];
    else if (pu->grp[1].grp_id == grp_id)
        grp = &pu->grp[1];
    else if (pu->grp[2].grp_id == grp_id)
        grp = &pu->grp[2];
    else {
        if (pu->grp[0].cur_p2l_cnt == 0)
            grp = &pu->grp[0];
        else if (pu->grp[1].cur_p2l_cnt == 0)
            grp = &pu->grp[1];
        else if (pu->grp[2].cur_p2l_cnt == 0)
            grp = &pu->grp[2];
        else
            sys_assert(false);

        grp->grp_id = grp_id;
        grp->ttl_p2l_cnt = p2l_get_grp_p2l_cnt(grp_id, slc);
    }

    if (grp->pda != NULL) {
        return (p2l_t*)grp->pda;
    }

    u32 res_id = p2l_res_id_get();
    sys_assert(grp->pda == NULL);
    p2l = &p2l_res_mgr.p2l[res_id];
    p2l->grp = grp;
    p2l->id = p2l_id;
    p2l->flags.all = 0;
    grp->pda = (void*)p2l;
    grp->pgsn_ofst = 0;
    if (CBF_EMPTY(&p2l_res_mgr.sdtag_pool) == false) {
        CBF_FETCH(&p2l_res_mgr.sdtag_pool, p2l->sdtag);
        p2l->smem = sdtag2mem(p2l->sdtag.b.dtag);
    } else {
        p2l->smem = NULL;
        p2l->sdtag = _inv_dtag;
        p2l->flags.b.ddtag_only = true;
    }
    p2l->flags.b.init_done = true;

    return p2l;

}

fast_code p2l_t* pgsn_get(p2l_user_t *pu, u32 p2l_id, bool slc, u32 index)
{
    p2l_t* p2l;
    p2l_grp_t *grp = NULL;
    u32 grp_id = p2l_id / p2l_para.p2l_per_grp;

    if (pu->grp[0].grp_id == grp_id)
		grp = &pu->grp[0];
	else if (pu->grp[1].grp_id == grp_id)
		grp = &pu->grp[1];
    else if (pu->grp[2].grp_id == grp_id)
        grp = &pu->grp[2];
	else {
		if (pu->grp[0].cur_p2l_cnt == 0)
			grp = &pu->grp[0];
		else if (pu->grp[1].cur_p2l_cnt == 0)
			grp = &pu->grp[1];
        else if (pu->grp[2].cur_p2l_cnt == 0)
			grp = &pu->grp[2];
		else
			sys_assert(false);

		grp->grp_id = grp_id;
		grp->ttl_p2l_cnt = p2l_get_grp_p2l_cnt(grp_id, slc);
	}

    if (grp->pgsn[index] != NULL) {
        return (p2l_t*)grp->pgsn[index];
    }

    u32 res_id = p2l_res_id_get();
    sys_assert(grp->pgsn[index] == NULL);
    grp->ttl_pgsn_cnt++;

    p2l = &p2l_res_mgr.p2l[res_id];
    p2l->grp = grp;
	p2l->id = p2l_id;
    p2l->flags.all = 0;
    grp->pgsn[index] = (void*)p2l;
    if (CBF_EMPTY(&p2l_res_mgr.sdtag_pool) == false) {
        CBF_FETCH(&p2l_res_mgr.sdtag_pool, p2l->sdtag);
        p2l->smem = sdtag2mem(p2l->sdtag.b.dtag);
    } else {
        p2l->smem = NULL;
        p2l->sdtag = _inv_dtag;
        p2l->flags.b.ddtag_only = true;
        //ftl_core_trace(LOG_WARNING, 0, "pgsn %d no sdtag", p2l_id);
    }
    p2l->flags.b.init_done = true;
    return p2l;

}

fast_code p2l_t* p2l_get(p2l_user_t *pu, u32 p2l_id, bool slc,u32 bad_die_cnt, bool parity_mix)
{
	p2l_t* p2l;
	u32 res_id;
	p2l_grp_t *grp = NULL;
	u32 grp_id = p2l_id / p2l_para.p2l_per_grp;
	u32 offset = p2l_id % p2l_para.p2l_per_grp;

	if (pu->grp[0].grp_id == grp_id)
		grp = &pu->grp[0];
	else if (pu->grp[1].grp_id == grp_id)
		grp = &pu->grp[1];
    else if (pu->grp[2].grp_id == grp_id)
        grp = &pu->grp[2];
	else {
		if (pu->grp[0].cur_p2l_cnt == 0)
			grp = &pu->grp[0];
		else if (pu->grp[1].cur_p2l_cnt == 0)
			grp = &pu->grp[1];
        else if (pu->grp[2].cur_p2l_cnt == 0)
			grp = &pu->grp[2];
		else
			sys_assert(false);

		grp->grp_id = grp_id;
		grp->ttl_p2l_cnt = p2l_get_grp_p2l_cnt(grp_id, slc);
	}

	res_id = grp->res_id[offset];
	if (res_id != GRP_FREE_RES_ID)
		return &p2l_res_mgr.p2l[res_id];

	res_id = p2l_res_id_get();
    //ftl_core_trace(LOG_INFO, 0, "p2l get id:%d grp:%d p2l cnt:%d bad die:%d", p2l_id, grp_id, grp->ttl_p2l_cnt, bad_die_cnt);
	sys_assert(offset == grp->cur_p2l_cnt);
	sys_assert(grp->cur_p2l_cnt < grp->ttl_p2l_cnt);
	sys_assert(grp->res_id[offset] == GRP_FREE_RES_ID);

	grp->cur_p2l_cnt++;
	grp->res_id[offset] = res_id;

	p2l = &p2l_res_mgr.p2l[res_id];
	p2l->grp = grp;
	p2l->id = p2l_id;
	p2l->flags.all = 0;
#ifdef SKIP_MODE
    p2l->updt_cnt = bad_die_cnt * DU_CNT_PER_PAGE * nand_plane_num() * p2l_para.pg_per_p2l;  // initial value : bad_die_cnt means all plane bad die, skip write, 64 die ==> 2 page combine
#else
    p2l->updt_cnt = 0;
#endif

#if RAID_SUPPORT
    //p2l->updt_cnt += DU_CNT_PER_PAGE * nand_plane_num();
    if(!parity_mix){
        p2l->updt_cnt += DU_CNT_PER_PAGE * nand_plane_num() * p2l_para.pg_per_p2l;  // adams 64 die ==> 2 page combine
    }
#endif

    p2l->flags.b.init_done = true; 
	if (CBF_EMPTY(&p2l_res_mgr.sdtag_pool) == false) { 
		CBF_FETCH(&p2l_res_mgr.sdtag_pool, p2l->sdtag); 
		p2l->smem = sdtag2mem(p2l->sdtag.b.dtag); 
		//memset(p2l->smem,0xFF,DTAG_SZE); 
	} else { 
		p2l->smem = NULL; 
		p2l->sdtag = _inv_dtag; 
		p2l->flags.b.ddtag_only = true; 
		//memset(p2l->dmem,0xFF,DTAG_SZE); 
		//ftl_core_trace(LOG_WARNING, 0, "p2l %d no sdtag", p2l_id); 
	} 

	return p2l;
}

fast_code int p2l_copy_done(void *ctx, dpe_rst_t *rst)
{
	bool ret;
	u32 res_id;
	p2l_t *p2l = (p2l_t*)ctx;

	p2l->flags.b.copy_done = true;
    p2l->flags.b.copy_send = false;
	if (p2l->flags.b.sdtag_flush == false) {
		/*
		** sram dtag not used for p2l flush, release sram dtag when p2l copy done
		*/
		CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
		sys_assert(ret == true);
	} else if (p2l->flags.b.flush_done) {
		/*
		** sram dtag used for p2l flush, release sram dtag when p2l copy and p2l flush done
		*/
		CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
		sys_assert(ret == true);

		res_id = p2l - &p2l_res_mgr.p2l[0];
		p2l_res_id_put(res_id);
	}

	return 0;
}

fast_code void p2l_inv_updt(p2l_t *p2l, aspb_t *aspb, bool raid)
{
		u32 ptr, i;
#ifdef SKIP_MODE
		u32 *df_ptr = (u32*)get_spb_defect(aspb->spb_id);
		u32 index, itl, du;
		u32 df_value;


    for (index = 0; index < (nand_info.interleave >> 5); index++) {
        if (df_ptr[index] == 0) {
            continue;
        }
        df_value = df_ptr[index];
        ptr = index << (5 + DU_CNT_PER_PAGE_SHIFT); //u32 --> 5 bit
        for (itl = 0; itl < 32; itl++) {
            if (df_value & (1 << itl)) {
                for (du = 0; du < DU_CNT_PER_PAGE; du++) {
                    if (p2l->flags.b.ddtag_only) {
                        p2l->dmem[ptr + du] = INV_LDA;
                    } else {
                        p2l->smem[ptr + du] = INV_LDA;
                    }
                }
            }
            ptr += DU_CNT_PER_PAGE;
        }
    }
#endif
    if (raid) {
        u8 du_cnt = DU_CNT_PER_PAGE * nand_plane_num();
        ptr = aspb->parity_die << (DU_CNT_PER_PAGE_SHIFT + 1);
        if(aspb->flags.b.parity_mix){ // parity use plane x
            ptr += DU_CNT_PER_PAGE * aspb->parity_die_pln_idx;
            du_cnt = DU_CNT_PER_PAGE;
        }
		for (i = 0; i < du_cnt; i++) {
			if (p2l->flags.b.ddtag_only == false)
				p2l->smem[ptr + i] = INV_LDA;
			else
				p2l->dmem[ptr + i] = INV_LDA;
		}
    }


	//u32 i;
	//u32 die;
	//u32 page;
	//u32 updt_ptr;
	//u32 mp_du_cnt = nand_plane_num() << DU_CNT_SHIFT;
	//u32 intlv_du_cnt = nand_info.interleave << DU_CNT_SHIFT;
#if 0
    if (p2l->flags.b.ddtag_only == false)
        memset(p2l->smem, 0xFF, 4096);
	else
        memset(p2l->dmem, 0xFF, 4096);
#endif
    #if 0
	for (page = 0; page < p2l_para.pg_per_p2l; page++) {
		for (die = 0; die < nand_info.lun_num; die++) {
			// TODO: defect support
			if (p2l->flags.b.ddtag_only == false)
                memset(p2l->smem, INV_LDA, unsigned int count);
				p2l->smem[updt_ptr + i] = INV_LDA;
			else
                memset(p2l->dmem, INV_LDA, unsigned int count);
				p2l->dmem[updt_ptr + i] = INV_LDA;
			#if 0
			if (raid && (die == aspb->parity_die)) {
				p2l->updt_cnt += mp_du_cnt;
				updt_ptr = page * intlv_du_cnt + die * mp_du_cnt;
				for (i = 0; i < mp_du_cnt; i++) {
					if (p2l->flags.b.ddtag_only == false)
						p2l->smem[updt_ptr + i] = INV_LDA;
					else
						p2l->dmem[updt_ptr + i] = INV_LDA;
				}
			}
            #endif
		}
	}
    #endif
}

//each mp plane update page sn once
fast_code void pagesn_update(pstream_t *ps, ncl_w_req_t *req)
{
    p2l_t *p2l = NULL;
    u32 pfs_ofst =  req->req.cnt >> DU_CNT_SHIFT;
#ifdef SKIP_MODE	
    pda_t pda = req->w.pda[pfs_ofst];
#else
	pda_t pda = req->w.l2p_pda[pfs_ofst];
#endif
    p2l_user_t *pu = &ps->pu[req->req.aspb_id];
    aspb_t *aspb = &ps->aspb[req->req.aspb_id];
    u32 id = pda_to_p2l_id(pda);
    u32 index = 0;
    p2l_grp_t *grp;
    p2l_t* pda_buffer;

    pda_buffer = pda_buffer_get(pu, id, aspb->flags.b.slc);
    grp = pda_buffer->grp;

    u32 pgsn_ofst = grp->pgsn_ofst;

#if (SPOR_FLOW == mENABLE)
    global_page_sn = (global_page_sn > shr_page_sn)?global_page_sn:shr_page_sn;
#endif

    if (pgsn_ofst >= PGSN_CNT_PER_DU)
    {
        pgsn_ofst -= PGSN_CNT_PER_DU;
        index = 1;
    }
    p2l = pgsn_get(pu, id, aspb->flags.b.slc, index);

    if (p2l->flags.b.init_done == true) {
        p2l->flags.b.init_done = false;
        //ftl_core_trace(LOG_DEBUG, 0, "pgsn init");
        //p2l_inv_updt(p2l, aspb, fcns_raid_enabled(ps->nsid));
    }

    pda_t* pda_bf;
    u64 *pgsn;

    if (pda_buffer->flags.b.ddtag_only == false) {
        pda_bf = (pda_t*) pda_buffer->smem;
        pda_bf[grp->pgsn_ofst]   = pda;
        pda_bf[grp->pgsn_ofst+1] = INV_U32;
	}
	else {
        pda_bf = (pda_t*) pda_buffer->dmem;
        pda_bf[grp->pgsn_ofst]   = pda;
        pda_bf[grp->pgsn_ofst+1] = INV_U32;
	}

    sys_assert(pgsn_ofst < PGSN_CNT_PER_DU);
    if (p2l->flags.b.ddtag_only == false) {
        pgsn = (u64*) p2l->smem;
        pgsn[pgsn_ofst]   = global_page_sn;
    }
	else
    {
        pgsn = (u64*) p2l->dmem;
        pgsn[pgsn_ofst] = global_page_sn;
	}

    sys_assert(grp->pgsn_ofst < 768);
    grp->pgsn_ofst++;


    global_page_sn++;
    shr_page_sn = global_page_sn;
}

fast_code void p2l_update(pstream_t *ps, ncl_w_req_t *req)
{
	u32 id;
	u32 i, j, k;
	p2l_t *p2l = NULL;
	u32 plane_num = nand_plane_num();
	p2l_user_t *pu = &ps->pu[req->req.aspb_id];
	aspb_t *aspb = &ps->aspb[req->req.aspb_id];
 #ifdef SKIP_MODE
    if (req->req.op_type.b.pln_write) {
        plane_num = req->req.op_type.b.pln_write;
    }
#endif
    u32 update_du_cnt = plane_num << DU_CNT_SHIFT;

    //ftl_core_trace(LOG_INFO, 0, "p2l update die:%d", pda2die(req->w.l2p_pda[0]));
	for (i = 0; i < req->req.tpage; i++) {
#ifdef SKIP_MODE
		pda_t pda = req->w.pda[i * plane_num];
#else
		pda_t pda = req->w.l2p_pda[i * plane_num];
#endif
		id = pda_to_p2l_id(pda);
        //ftl_core_trace(LOG_DEBUG, 0, "id:%d pda:%x, p2l:%x", id, pda, p2l);
		if ((p2l == NULL) || (p2l->id != id)) {
			p2l = p2l_get(pu, id, aspb->flags.b.slc,req->req.bad_die_cnt, aspb->flags.b.parity_mix);
			//new p2l, updt parity & defect die with inv_lda
			if (p2l->flags.b.init_done == true) {
                p2l->flags.b.init_done = false;
                //ftl_core_trace(LOG_DEBUG, 0, "p2l init");
                p2l_inv_updt(p2l, aspb, fcns_raid_enabled(ps->nsid));
            }
		}

		/*Configure lda to p2l_buffer (for GC use)*/
		u32 pln_idx;
        u32 lda_idx = i * update_du_cnt;
		for (pln_idx = 0; pln_idx < plane_num ; pln_idx++) {
			pda = req->w.pda[i * plane_num + pln_idx];
			u32 du_off = nal_pda_offset_in_spb(pda);
			u32 updt_ptr = du_off & DU_CNT_PER_P2L_MASK;
			for (j = 0; j < DU_CNT_PER_PAGE; j++) {
				if (p2l->flags.b.ddtag_only == false)
					p2l->smem[updt_ptr + j] = req->w.lda[lda_idx++];
				else
					p2l->dmem[updt_ptr + j] = req->w.lda[lda_idx++];
			}
		}

		p2l->updt_cnt += req->req.mp_du_cnt;
		sys_assert(p2l->updt_cnt <= DU_CNT_PER_P2L);
		if (p2l->updt_cnt == DU_CNT_PER_P2L) {
            //ftl_core_trace(LOG_DEBUG, 0, "p2l update done,id:%d", p2l->id);
			p2l->grp->updt_p2l_cnt++;
			if (p2l->flags.b.ddtag_only == false){
                p2l->flags.b.copy_send = true;
				bm_data_copy((u32)p2l->smem, (u32)p2l->dmem, NAND_DU_SIZE, p2l_copy_done, p2l);
			}

            //if p2l all update done, also start copy page sn table
            if (p2l->grp->updt_p2l_cnt == p2l->grp->ttl_p2l_cnt) {
                for (k = 0; k < p2l->grp->ttl_pgsn_cnt; k++) {
                    p2l_t* pgsn = (p2l_t*)p2l->grp->pgsn[k];
                    if (pgsn->flags.b.ddtag_only == false){
                        pgsn->flags.b.copy_send = true;
                        bm_data_copy((u32)pgsn->smem, (u32)pgsn->dmem, NAND_DU_SIZE, p2l_copy_done, pgsn);
                    }
                    if (pgsn->flags.b.ddtag_only) {
                        #if defined(ENABLE_L2CACHE)
                        l2cache_mem_flush(pgsn->dmem, DTAG_SZE);
                        #endif
                    }
                }

                p2l_t* pda_bf = (p2l_t*)p2l->grp->pda;
                if (pda_bf->flags.b.ddtag_only == false){
                    pda_bf->flags.b.copy_send = true;
                    bm_data_copy((u32)pda_bf->smem, (u32)pda_bf->dmem, NAND_DU_SIZE, p2l_copy_done, pda_bf);
                }
                if (pda_bf->flags.b.ddtag_only) {
                    #if defined(ENABLE_L2CACHE)
                    l2cache_mem_flush(pda_bf->dmem, DTAG_SZE);
                    #endif
                }
            }
		}

		if (p2l->flags.b.ddtag_only == true) {
			if ((p2l->updt_cnt == DU_CNT_PER_P2L) || p2l->flags.b.force_flush) {
		#if defined(ENABLE_L2CACHE)
				l2cache_mem_flush(p2l->dmem, DTAG_SZE);
		#endif
			}
		}
	}
}

static inline p2l_grp_t* p2l_grp_get(p2l_user_t *pu, u32 grp_id)
{
	p2l_grp_t* grp = NULL;

	if (pu->grp[0].grp_id == grp_id)
		grp = &pu->grp[0];
	else if (pu->grp[1].grp_id == grp_id)
		grp = &pu->grp[1];
    else if (pu->grp[2].grp_id == grp_id)
		grp = &pu->grp[2];
	else
		sys_assert(false);

	return grp;
}

fast_code void p2l_req_pl_ins(u32 p2l_grp_id, p2l_user_t *pu, ncl_w_req_t *req, bool ins_p2l)
{
	u32 i = 0;
	//u32 page;
	u32 res_id;
	u32 pl_idx;
	p2l_t *p2l;
	p2l_grp_t *grp;
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
	spb_id_t spb = pda2blk(req->w.pda[0]);
#endif
    //sys_assert(req->req.p2l_page == 0);
	//page = pda2page(req->w.l2p_pda[0]) + cwl->p2l_page;
	grp = p2l_grp_get(pu, p2l_grp_id);
    pl_idx = (req->req.cpage - 1) * DU_CNT_PER_PAGE;

    if (ins_p2l) 
    {
        for (i = 0; i < p2l_para.p2l_per_grp; i++) {
            if (i < grp->ttl_p2l_cnt) 
            {
                res_id = grp->res_id[i];
			#if (PLP_FORCE_FLUSH_P2L == mENABLE)
                if(plp_spb_flush_p2l == spb)   	//plp force flush p2l,lda dtag may need init  
                { 
					if(res_id == 0xFF)  	//Incomplete lda   set dummy du 
					{ 
						req->w.pl[pl_idx + i].pl.dtag = WVTAG_ID; 
                		req->w.lda[pl_idx + i] = INV_LDA;  
                        sys_assert(req->req.p2l_dtag_cnt);
                        req->req.p2l_dtag_cnt --; 
					} 
					else 
					{ 
						p2l = &p2l_res_mgr.p2l[res_id]; 
		                if ((p2l->flags.b.ddtag_only == false) && (p2l->flags.b.copy_done == false))  
		                { 
		                    p2l->flags.b.sdtag_flush = true; 
		                    if(p2l->updt_cnt != DU_CNT_PER_P2L)     //partial lda dtag,need init 
								memset(p2l->smem + get_plp_lda_dtag_w_idx(),0xFF,DU_CNT_PER_P2L - get_plp_lda_dtag_w_idx()); 
		                    req->w.pl[pl_idx + i].pl.dtag = p2l->sdtag.dtag; 
		                }  
		                else  
		                {		 
		                	if(p2l->updt_cnt != DU_CNT_PER_P2L)		//partial lda dtag,need init 
								memset(p2l->dmem + get_plp_lda_dtag_w_idx(),0xFF,DU_CNT_PER_P2L - get_plp_lda_dtag_w_idx()); 
		                    req->w.pl[pl_idx + i].pl.dtag = p2l->ddtag.dtag; 
		                } 
						req->w.lda[pl_idx + i] = P2L_LDA; 

						//ftl_core_trace(LOG_ALW, 0, "updt cnt:%d grp:%d p2l->id:%d res_id:%d ",p2l->updt_cnt,grp->grp_id,p2l->id,res_id); 
					} 
                } 
                else										 //normal code
            #endif
                {
                	p2l = &p2l_res_mgr.p2l[res_id];
	                if ((p2l->flags.b.ddtag_only == false) && (p2l->flags.b.copy_done == false)) 
	                {
	                    p2l->flags.b.sdtag_flush = true;
	                    req->w.pl[pl_idx + i].pl.dtag = p2l->sdtag.dtag;
	                } 
	                else 
	                {
	                    req->w.pl[pl_idx + i].pl.dtag = p2l->ddtag.dtag;
	                }
					req->w.lda[pl_idx + i] = P2L_LDA;
                }	
                req->req.p2l_dtag_cnt++;
                //ftl_core_trace(LOG_DEBUG, 0, "p2l req ins i:%d dtag:%x", i, req->w.pl[pl_idx + i].pl.dtag);

            } 
            else 
            {
                req->w.pl[pl_idx + i].pl.dtag = WVTAG_ID;
                req->w.lda[pl_idx + i] = INV_LDA;
                sys_assert(0);
            }

            //req->w.lda[pl_idx + i] = INV_LDA;
            req->w.pl[pl_idx + i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
        }
    }
    else 
    {
        //pgsn update wy pyload

#if (PLP_FORCE_FLUSH_P2L == mENABLE)
        sys_assert((grp->ttl_pgsn_cnt == 2)||plp_spb_flush_p2l);
#else
		sys_assert(grp->ttl_pgsn_cnt == 2);
#endif
        for (i = 0; i < grp->ttl_pgsn_cnt; i++) 
        {
            p2l = (p2l_t*)grp->pgsn[i];
            if ((p2l->flags.b.ddtag_only == false) && (p2l->flags.b.copy_done == false)) {
                p2l->flags.b.sdtag_flush = true;
                req->w.pl[pl_idx + i].pl.dtag = p2l->sdtag.dtag;
            } else {
                req->w.pl[pl_idx + i].pl.dtag = p2l->ddtag.dtag;
            }

            req->w.lda[pl_idx + i] = INV_LDA;
            req->w.pl[pl_idx + i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
        }
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
        //---------------------Incomplete pgsn buffer-----------------//
		if((plp_spb_flush_p2l) && (grp->ttl_pgsn_cnt == 1))
	    {
	    	//extern volatile fcns_attr_t fcns_attr[INT_NS_ID + 1];
            //fcns_attr[1].b.p2l = false;
			req->w.pl[pl_idx + i].pl.dtag = WVTAG_ID;	
			req->w.lda[pl_idx + i] = INV_LDA;
            req->w.pl[pl_idx + i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
            i++;
            ftl_core_trace(LOG_ALW, 0xebe7, "set pgsn dummy du for plp p2l!!!");
	    }
#endif
	    //----------------------fill one dummy du here----------------//
        p2l = (p2l_t*)grp->pda;
        if ((p2l->flags.b.ddtag_only == false) && (p2l->flags.b.copy_done == false)) {
            p2l->flags.b.sdtag_flush = true;
            req->w.pl[pl_idx + i].pl.dtag = p2l->sdtag.dtag;
        } else {
            req->w.pl[pl_idx + i].pl.dtag = p2l->ddtag.dtag;
        }
        req->w.lda[pl_idx + i] = INV_LDA;
        req->w.pl[pl_idx + i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
        i++;
    }

    u32 total_p2l_du_cnt = DU_CNT_PER_PAGE;

    for (; i < total_p2l_du_cnt; i++) {  //total 8 dtag. 2 plane
        //ftl_core_trace(LOG_DEBUG, 0, "p2l fill dummy idx:%d", i);
        req->w.pl[pl_idx + i].pl.dtag = WVTAG_ID;
        req->w.lda[pl_idx + i] = INV_LDA;
		req->w.pl[pl_idx + i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
    }

    //ftl_core_trace(LOG_DEBUG, 0, "p2l req submit:%x du:%d pda:%x", req, total_p2l_du_cnt, req->w.l2p_pda[pl_idx/DU_CNT_PER_PAGE]);
	//grp indx will be used when prog done
	req->req.p2l_grp_idx_nrm = grp - pu->grp;
}

#ifndef DUAL_BE
fast_code void set_ncl_cmd_p2l_info(ncl_req_t *req, struct ncl_cmd_t *ncl_cmd)
{
	u32 page;
	u32 plane_num = nand_plane_num();
	struct info_param_t *info = ncl_cmd->addr_param.common_param.info_list;

	//only program one page, the data must be all P2L
	if (req->tpage == 1) {
		ncl_cmd->op_type = NCL_CMD_PROGRAM_DATA;
		return;
	}

	ncl_cmd->flags |= NCL_CMD_HOST_INTERNAL_MIX;
	for (page = 0; page < req->tpage; page++) {
		u8 index = page * plane_num;
		u8 op_type = NCL_CMD_PROGRAM_HOST;
		if (page == req->p2l_page)
			op_type = NCL_CMD_PROGRAM_DATA;

		if (plane_num == 2) {
			info[index + 0].op_type = op_type;
			info[index + 1].op_type = op_type;
		} else if (plane_num == 4) {
			info[index + 0].op_type = op_type;
			info[index + 1].op_type = op_type;
			info[index + 2].op_type = op_type;
			info[index + 3].op_type = op_type;
		}
	}
}
#endif

#if 0
fast_code void p2l_req_submit(p2l_grp_t* grp, ncl_w_req_t *req)
{
	u32 i;
	u32 res_id;
	u32 lda_cnt;
	u32 du_ofst;
	p2l_t *p2l;
	stripe_user_t* su;

	sys_assert(grp->updt_p2l_cnt == grp->ttl_p2l_cnt);
#ifdef SKIP_MODE  //need to check
	du_ofst = ftl_core_next_write(req->req.nsid, req->req.type) * req->req.p2l_page;
#else
	du_ofst = ftl_core_next_write(req->req.nsid, req->req.type) * req->req.p2l_page;
#endif

    sys_assert(grp->updt_p2l_cnt == grp->ttl_p2l_cnt);
	for (i = 0; i < grp->ttl_p2l_cnt; i++) {
		res_id = grp->res_id[i];
		p2l = &p2l_res_mgr.p2l[res_id];
        sys_assert(p2l->updt_cnt == DU_CNT_PER_P2L);
        ftl_core_trace(LOG_DEBUG, 0x49fa, "sub mit p2l idx:%d dtag:%x update cnt:%d grp:%d", p2l->id, p2l->sdtag.dtag, p2l->updt_cnt, p2l->grp->grp_id);
		//replace ddtag with sdtag if copy haven't done
		if ((p2l->flags.b.ddtag_only == false) && (p2l->flags.b.copy_done == false)) {
			p2l->flags.b.sdtag_flush = true;
			req->w.pl[du_ofst + i].pl.dtag = p2l->sdtag.dtag;
		}
	}
    //page sn dtag check
    p2l = (p2l_t*)grp->pgsn;
    if ((p2l->flags.b.ddtag_only == false) && (p2l->flags.b.copy_done == false)) {
			p2l->flags.b.sdtag_flush = true;
			req->w.pl[du_ofst + i].pl.dtag = p2l->sdtag.dtag;
		}
}
#endif

fast_code u32 p2l_get_res_id(p2l_t* p2l)
{
    return p2l-p2l_res_mgr.p2l;
}
fast_code void p2l_not_copy_free_dtag(p2l_t *p2l)
{
	bool ret;
    if ((p2l->flags.b.copy_send == false)) {
        p2l_res_id_put(p2l_get_res_id(p2l));
		CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
		sys_assert(ret == true);
	}
	else{
		p2l->flags.b.sdtag_flush = true;
	}
}
fast_code void p2l_grp_nrm_done(p2l_grp_t *grp, bool reset)
{
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
	if(plp_spb_flush_p2l) 
		return; 
#endif

	u32 i;
	bool ret;
	u32 res_id;
	p2l_t *p2l;

    //ftl_core_trace(LOG_DEBUG, 0, "p2l grp nrm done grp:%d", grp->grp_id);
	for (i = 0; i < grp->ttl_p2l_cnt; i++) {
		res_id = grp->res_id[i];
		p2l = &p2l_res_mgr.p2l[res_id];
		p2l->flags.b.flush_done = true;
		grp->res_id[i] = GRP_FREE_RES_ID;

		if (reset == false) {
			sys_assert(res_id < p2l_res_mgr.total);
			if(p2l->updt_cnt != DU_CNT_PER_P2L){
                ftl_core_trace(LOG_INFO, 0xce6a, "grp->grp_id %u p2l->updt_cnt %u", grp->grp_id, p2l->updt_cnt);
            }
			sys_assert(p2l->updt_cnt == DU_CNT_PER_P2L);
        }

		if (p2l->flags.b.ddtag_only) {
			p2l_res_id_put(res_id);
		} else if (p2l->flags.b.copy_done) {
			p2l_res_id_put(res_id);
			/*
			** p2l sram dtag used for p2l flush, release it when p2l copy & flush done
			*/
			if (p2l->flags.b.sdtag_flush) {
				CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
				sys_assert(ret == true);
			}
		}
		else if(reset){
            p2l_not_copy_free_dtag(p2l);
        } 
	}
//pgsn done update info
    for (i = 0; i < grp->ttl_pgsn_cnt; i++) {
        p2l = (p2l_t*)grp->pgsn[i];
        p2l->flags.b.flush_done = true;
        //grp->res_id[i] = GRP_FREE_RES_ID;
        if (p2l->flags.b.ddtag_only) {
            p2l_res_id_put(p2l_get_res_id(p2l));
        } else if (p2l->flags.b.copy_done) {
            p2l_res_id_put(p2l_get_res_id(p2l));
            /*
            ** p2l sram dtag used for p2l flush, release it when p2l copy & flush done
            */
            if (p2l->flags.b.sdtag_flush) {
                CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
                sys_assert(ret == true);
            }
        }
        else if(reset){
            p2l_not_copy_free_dtag(p2l);
        } 
        grp->pgsn[i] = NULL;
    }

    //pda buffer handle
    if (grp->pda != NULL) {
        p2l = (p2l_t*)grp->pda;
        p2l->flags.b.flush_done = true;
        if (p2l->flags.b.ddtag_only) {
            p2l_res_id_put(p2l_get_res_id(p2l));
        } else if (p2l->flags.b.copy_done) {
            p2l_res_id_put(p2l_get_res_id(p2l));
            /*
            ** p2l sram dtag used for p2l flush, release it when p2l copy & flush done
            */
            if (p2l->flags.b.sdtag_flush) {
                CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
                sys_assert(ret == true);
            }
        }		
        else if(reset){
            p2l_not_copy_free_dtag(p2l);
        } 
    }
    grp->pda = NULL;
    grp->pgsn_ofst = 0;

    //grp->pgsn = NULL;
	grp->grp_id = ~0;
    grp->ttl_pgsn_cnt = 0;
	grp->ttl_p2l_cnt = 0;
	grp->cur_p2l_cnt = 0;
	grp->updt_p2l_cnt = 0;
}

fast_code void p2l_grp_pad_done(p2l_grp_t *grp)
{
	u32 i;
	u32 res_id;
	p2l_t *p2l;

	sys_assert(grp->updt_p2l_cnt < grp->ttl_p2l_cnt);

	for (i = 0; i < grp->cur_p2l_cnt; i++) {
		res_id = grp->res_id[i];
		p2l = &p2l_res_mgr.p2l[res_id];

		if (p2l->flags.b.sdtag_flush) {
			p2l->flags.b.sdtag_flush = false;

			if (p2l->flags.b.copy_done) {
				bool ret;
				CBF_INS(&p2l_res_mgr.sdtag_pool, ret, p2l->sdtag);
				sys_assert(ret == true);
			}
		}
	}
}

fast_code p2l_ncl_cmd_t* p2l_load_cmd_get(void)
{
	u32 cmd_id;
	sys_assert(p2l_load_mgr.avail);

	cmd_id = find_first_zero_bit(&p2l_load_mgr.bmp, TTL_P2L_LOAD_CMD);
	sys_assert(cmd_id < TTL_P2L_LOAD_CMD);
	set_bit(cmd_id, &p2l_load_mgr.bmp);
	p2l_load_mgr.avail--;

	return &p2l_load_mgr.cmd[cmd_id];
}

fast_code void p2l_load_cmd_put(p2l_ncl_cmd_t* p2l_cmd)
{
	bool ret;
	u32 cmd_id;

	cmd_id = p2l_cmd - &p2l_load_mgr.cmd[0];
	ret = test_and_clear_bit(cmd_id, &p2l_load_mgr.bmp);
	sys_assert(ret == 1);
	p2l_load_mgr.avail++;
}
#if 0
fast_code void p2l_load_err_cmpl(void *ctx)
{
	u32 i;
	u32 p2l_id;
	bool is_slc;
	bool get_err_p2l = false;
	p2l_build_req_t *p2l_build = (p2l_build_req_t*)ctx;
	struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t*)p2l_build->ctx;
	p2l_load_req_t *p2l_load = (p2l_load_req_t*)ncl_cmd->caller_priv;
	struct info_param_t *info_list = ncl_cmd->addr_param.common_param.info_list;

	is_slc = ftl_core_get_spb_type(p2l_build->spb_id);

	//get load fail p2l
	for (i = 0; i < p2l_load->p2l_cnt; i++) {
		if (info_list[i].status) {
			p2l_id = p2l_load->p2l_id + i;
			p2l_build->start_page = p2l_id << p2l_para.pg_p2l_shift;
			p2l_build->last_page = p2l_get_last_page(p2l_id, is_slc);
			p2l_build->dtags[0] = p2l_load->dtags[i];

			get_err_p2l = true;
			info_list[i].status = ficu_err_good;
			ftl_core_trace(LOG_INFO, 0xaacb, "get fail p2l: spb %d p2l %d", p2l_load->spb_id, p2l_id);
		}
	}

	if (get_err_p2l) {
		p2l_build_push(p2l_build);
	} else {
		ncl_cmd->status = 0;
		ncl_cmd->completion(ncl_cmd);
		sys_free(FAST_DATA, p2l_build);
	}
}
fast_code void p2l_load_err_handle(struct ncl_cmd_t *ncl_cmd)
{
	u32 i;
	u32 p2l_id;
	bool is_slc;
	p2l_load_req_t *p2l_load = (p2l_load_req_t*)ncl_cmd->caller_priv;
	p2l_build_req_t *p2l_build = sys_malloc(FAST_DATA, sizeof(p2l_build_req_t));
	struct info_param_t *info_list = ncl_cmd->addr_param.common_param.info_list;

	p2l_build->spb_id = p2l_load->spb_id;
	is_slc = ftl_core_get_spb_type(p2l_build->spb_id);

	//get load fail p2l
	for (i = 0; i < p2l_load->p2l_cnt; i++) {
		if (info_list[i].status) {
			p2l_id = p2l_load->p2l_id + i;
			p2l_build->start_page = p2l_id << p2l_para.pg_p2l_shift;
			p2l_build->last_page = p2l_get_last_page(p2l_id, is_slc);
			p2l_build->dtags[0] = p2l_load->dtags[i];

			info_list[i].status = ficu_err_good;
			ftl_core_trace(LOG_INFO, 0x18de, "get fail p2l: spb %d p2l %d", p2l_load->spb_id, p2l_id);
			break;
		}
	}

	sys_assert(i < p2l_load->p2l_cnt);

	p2l_build->ctx = ncl_cmd;
	p2l_build->cmpl = p2l_load_err_cmpl;
	p2l_build_push(p2l_build);
}
#endif
fast_code void p2l_build_pend_in(pending_p2l_build_t *pending_p2l_build_list)
{

    u32 pend_cnt = p2l_build_pending_list.p2l_build_pend_cnt;
    if((pend_cnt + 1) > P2L_PENDING_CNT_MAX)
    {
        p2l_build_pending_list.p2l_build_pend_cnt = 0;
    }
    else
    {
        p2l_build_pending_list.p2l_build_pend_cnt++;
    }

    ftl_core_trace(LOG_INFO, 0xa1a0, "spb_id : %d, p2l_id : %d ", pending_p2l_build_list->spb_id, pending_p2l_build_list->p2l_id);
    memcpy(&p2l_build_pending_list.pending_p2l_build_buf[pend_cnt], pending_p2l_build_list, sizeof(pending_p2l_build_t));

    sys_assert(p2l_build_pending_list.p2l_build_pend_cnt != p2l_build_pending_list.p2l_build_tmp_cnt);
}

extern bool ftl_core_gc_chk_stop(); 
fast_code bool p2l_build_pend_out(void)
{

    u32 pend_cnt = p2l_build_pending_list.p2l_build_pend_cnt;
    u32 tmp_cnt = p2l_build_pending_list.p2l_build_tmp_cnt;
    if(pend_cnt != tmp_cnt)
    {
        if((tmp_cnt + 1) > P2L_PENDING_CNT_MAX)
        {
            p2l_build_pending_list.p2l_build_tmp_cnt = 0;
        }
        else
		{
            p2l_build_pending_list.p2l_build_tmp_cnt++;
        }

        p2l_build_ncl = p2l_build_pending_list.pending_p2l_build_buf[tmp_cnt].p2l_build_ncl_list;
        p2l_build_load = p2l_build_pending_list.pending_p2l_build_buf[tmp_cnt].p2l_build_load_list;

		if(ftl_core_gc_chk_stop() || p2l_build_plp_trigger_done){
			ftl_core_trace(LOG_INFO, 0x2c37, "spbid %d p2lid %d build suspend ncl_cmd 0x%x", p2l_build_load->spb_id, p2l_build_load->p2l_id, p2l_build_ncl);
			p2l_load_cmd_put(p2l_build_ncl);
			p2l_build_load->cmpl((void*)p2l_build_load);
			return true;
		}
		else if(_p2l_build_free_cmd_bmp == INV_U16){ 
            p2l_build_plp_trigger_done = false; 
        	p2l_build_aux(p2l_build_pending_list.pending_p2l_build_buf[tmp_cnt].spb_id, p2l_build_pending_list.pending_p2l_build_buf[tmp_cnt].p2l_id);
        }else{ 
            return p2l_build_dbg_cmpl(); 
        }
        return false;
    }
//    if(pda2blk(p2l_b_pda) == p2l_build_pending_list.pending_p2l_build_buf[tmp_cnt].spb_id)
//    {
//        return false;
//    }

    return true;
}


extern bool ncl_cmd_in;
fast_code void p2l_load_done(struct ncl_cmd_t *ncl_cmd)
{
	p2l_ncl_cmd_t *p2l_cmd = (p2l_ncl_cmd_t*)ncl_cmd;
	p2l_load_req_t *load_req = (p2l_load_req_t*)ncl_cmd->caller_priv;

    if (ncl_cmd->flags & NCL_CMD_SCH_FLAG)
    {
        decrease_otf_cnt(ncl_cmd->die_id);
		ncl_cmd->flags &= ~NCL_CMD_SCH_FLAG;
    }

    #if NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    #endif

	if (ncl_cmd->status || load_req->flags.b.aux) {
        #if NCL_FW_RETRY
        if (ncl_cmd->retry_step == default_read && !load_req->flags.b.aux)
        {
	        ftl_core_trace(LOG_INFO, 0xf4dc, "p2l_rd_err_handling, ncl_cmd: 0x%x", ncl_cmd);
            rd_err_handling(ncl_cmd);
            return;
        } 
		#if RAID_SUPPORT
        else if(ncl_cmd->retry_step == retry_end && !load_req->flags.b.aux)
        {
            u32 nsid = INT_NS_ID - 1;
    		bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
            {   
                //ftl_core_trace(LOG_INFO, 0, "p2l raid_correct_push");
                rc_req_t* rc_req = rc_req_prepare(ncl_cmd, false);
                rc_req->flags.all = 0;
	            rc_req->flags.b.gc = 0;
	            rc_req->flags.b.host = 1;
                rc_req->flags.b.stream = 0;
                raid_correct_push(rc_req);
                return;
            }
			else
			{
				ftl_core_trace(LOG_ERR, 0xdc13, "p2l raid have not push! raid_enable:%d, rced:%d", fcns_raid_enabled(nsid), rced);
				goto AUX_BUILD;
			}
        }
		#endif
        else
        #endif
        {
		#if RAID_SUPPORT
			AUX_BUILD:
		#endif
            if(p2l_build_flag == false)  // && (ncl_cmd_in == false)
            {
                p2l_build_flag = true;				
			    p2l_build_ncl = (p2l_ncl_cmd_t*)ncl_cmd;
			    p2l_build_load = (p2l_load_req_t*)load_req;
				p2l_build_plp_trigger_done = false; 
                sys_assert(_p2l_build_free_cmd_bmp == INV_U16); 
                //evlog_printk(LOG_ERR,"ncl_cmd 0x%x p2l_build_load_p2l_id %d" , p2l_build_ncl, p2l_build_load->p2l_id);
			    p2l_build_aux(load_req->spb_id,p2l_cmd->bm_pl[0].pl.du_ofst);
            }
            else// if(p2l_build_flag == true)
            {

                //evlog_printk(LOG_ERR,"pending rebuild p2l blk %d p2l %d"
                //        ,load_req->spb_id,p2l_cmd->bm_pl[0].pl.du_ofst);
                //evlog_printk(LOG_ERR,"ncl_cmd 0x%x caller_priv 0x%x" ,(p2l_ncl_cmd_t*)ncl_cmd, (p2l_load_req_t*)ncl_cmd->caller_priv);
                tmp_pend->spb_id = (u32)load_req->spb_id;
                tmp_pend->p2l_id = p2l_cmd->bm_pl[0].pl.du_ofst;
                tmp_pend->p2l_build_ncl_list = (p2l_ncl_cmd_t*)ncl_cmd;
                tmp_pend->p2l_build_load_list = (p2l_load_req_t*)load_req;
                p2l_build_pend_in(tmp_pend);
            }
			return;
    		//p2l_load_err_handle(ncl_cmd);
//    		return;
        }
	}


    //ftl_core_trace(LOG_INFO, 0, "[DBG] p2l_load_cmd_put, ncl_cmd: 0x%x", ncl_cmd);
    p2l_load_cmd_put(p2l_cmd);
	load_req->cmpl((void*)load_req);
}

fast_code void ftl_core_p2l_load(p2l_load_req_t *load_req)
{
	u32 spb_type = NAL_PB_TYPE_XLC;
	u32 grp_id = load_req->p2l_id / p2l_para.p2l_per_grp;
	//u32 grp_off = load_req->p2l_id % p2l_para.p2l_per_grp;

#if(PLP_SUPPORT == 0) //nonplp
    u16 openwl;
    bool openblk_loadflag = false;
    epm_FTL_t* epm_ftl_data; 
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag); 
#endif 
    pda_t p2l_pda;
    pda_t pgsn_pda;

#if (PLP_FORCE_FLUSH_P2L == mENABLE)
	if(load_req->grp_id == load_req->p2l_dummy_grp_idx && load_req->flags.b.last_wl_p2l)
	{
		p2l_get_grp_pda(shr_p2l_grp_cnt-1, load_req->spb_id, 1, &p2l_pda, &pgsn_pda);
		ftl_core_trace(LOG_INFO, 0x7357, "plp_p2l last_p2l_pda: 0x%x", p2l_pda);
	}
	else
#endif
    p2l_get_grp_pda(grp_id, load_req->spb_id, 1, &p2l_pda, &pgsn_pda);

    //p2l_pda += grp_off;

    sys_assert(fcns_p2l_enabled(HOST_NS_ID) == 1);
	//ftl_core_trace(LOG_DEBUG, 0, "load spb %d grp %d p2l id %d cnt %d pda:%x",
		//load_req->spb_id, load_req->grp_id, load_req->p2l_id, load_req->p2l_cnt, p2l_pda);

	sys_assert(load_req->p2l_cnt <= p2l_para.p2l_per_grp);
	sys_assert(grp_id == ((load_req->p2l_id + load_req->p2l_cnt - 1) / p2l_para.p2l_per_grp));
#if(PLP_SUPPORT == 0) 
    openwl = pda2wl(p2l_pda);
    if(epm_ftl_data->host_open_blk[0] == load_req->spb_id)
    {
        for(u8 i=0;i<SPOR_CHK_WL_CNT;i++)
        {
            if(epm_ftl_data->host_open_wl[i] == openwl)
            {
                openblk_loadflag = true;
                break;
            }
        }
    }
    if(epm_ftl_data->gc_open_blk[0] == load_req->spb_id)
    {
        for(u8 i=0;i<SPOR_CHK_WL_CNT;i++)
        {
            if(epm_ftl_data->gc_open_wl[i] == openwl)
            {
                openblk_loadflag = true;
                break;
            }
        }
    }
#endif    
	u32 i;
	dtag_t dtag;
	p2l_ncl_cmd_t *p2l_cmd = p2l_load_cmd_get();
    u32 die_id = pda2die(p2l_pda);

	for (i = 0; i < load_req->p2l_cnt; i++)
	{
#ifdef SKIP_MODE
		p2l_cmd->pda[i] = p2l_pda + i;
#else
		p2l_cmd->pda[i] = ftl_remap_pda(p2l_pda + i);
#endif
		p2l_cmd->info[i].status = 0;
		p2l_cmd->info[i].pb_type = spb_type;
		if(p2l_uart_flag == true)
		{
			grp_id = 0;
			p2l_uart_flag = false;
		}
		dtag = load_req->dtags[i+((grp_id%(gc_gen_p2l_num*2))*3)];
		//dtag = load_req->dtags[i+(((grp_id/NUM_P2L_GEN_PDA)%2)*load_req->p2l_cnt)];
		p2l_cmd->bm_pl[i].pl.du_ofst = load_req->p2l_id+i;
		p2l_cmd->bm_pl[i].pl.dtag = dtag.dtag;
		p2l_cmd->bm_pl[i].pl.nvm_cmd_id = rd_dummy_meta_idx;
		p2l_cmd->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
        p2l_cmd->ncl_cmd.die_id = die_id;
#if defined(ENABLE_L2CACHE)
		if (dtag.b.in_ddr)
			l2cache_mem_invalidate(dtag2mem(dtag), DTAG_SZE);
#endif
	}
	p2l_cmd->ncl_cmd.status = 0;
	p2l_cmd->ncl_cmd.flags |= NCL_CMD_SCH_FLAG;
    p2l_cmd->ncl_cmd.flags &= ~(NCL_CMD_COMPLETED_FLAG | NCL_CMD_RCED_FLAG);
	p2l_cmd->ncl_cmd.caller_priv = (void*)load_req;
	p2l_cmd->ncl_cmd.addr_param.common_param.list_len = load_req->p2l_cnt;
    #if NCL_FW_RETRY
    p2l_cmd->ncl_cmd.retry_step = default_read;
    p2l_cmd->ncl_cmd.retry_cnt = 0;
    p2l_cmd->ncl_cmd.err_du_cnt = 0;
    #endif
    #if RAID_SUPPORT_UECC
	p2l_cmd->ncl_cmd.uecc_type = NCL_UECC_NORMAL_RD;
    #endif
    p2l_cmd->ncl_cmd.du_format_no = host_du_fmt;
#if(PLP_SUPPORT == 1)     
    if(load_req->grp_id >= load_req->p2l_dummy_grp_idx && load_req->flags.b.p2l_dummy) 
#else
    if((load_req->grp_id >= load_req->p2l_dummy_grp_idx && load_req->flags.b.p2l_dummy)||openblk_loadflag) 
#endif  
    { 
	    //p2l is dummy, aux build directly 
        load_req->flags.b.aux = true; 
		p2l_cmd->ncl_cmd.flags &= ~NCL_CMD_SCH_FLAG; 
		p2l_load_done(&p2l_cmd->ncl_cmd); 
	    load_req->flags.b.aux = false; 
	}
	else if (get_target_cpu(die_id) == CPU_ID)
		ncl_cmd_submit_insert_schedule(&p2l_cmd->ncl_cmd, false);
	else
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD_INSERT_SCH, 0, (u32)&p2l_cmd->ncl_cmd);
}


fast_code void p2l_open_grp_restore(aspb_t *aspb, p2l_user_t *pu, u32 nsid)
{
	u32 p2l_id;
	u32 grp_id;
	p2l_grp_t *grp;
	u32 first_p2l_id;
	u32 last_p2l_id;
	u32 last_wr_page;
	u32 grp_last_page;
	u32 spb_last_page;

	sys_assert((aspb->wptr % nand_interleave_num()) == 0);

	last_wr_page = aspb->wptr / nand_info.interleave - 1;
	grp_id = last_wr_page / p2l_para.p2l_per_grp;
	grp_last_page = p2l_get_grp_last_page(grp_id, aspb->flags.b.slc);

	//grp closed, no need to restore
	if (last_wr_page == grp_last_page){
		if (aspb->flags.b.slc)
			spb_last_page = nand_page_num_slc() - 1;
		else
			spb_last_page = nand_page_num() - 1;

		pu->flush_page = grp_last_page + p2l_para.pg_per_grp;
		if (pu->flush_page > spb_last_page)
			pu->flush_page = spb_last_page;

		return;
	}

	//restore p2l user context
	pu->flush_page = grp_last_page;
#if (P2L_FIXED_DIE == 0)
	pu->flush_die = grp_id % nand_info.lun_num;
	//parity die must be the last non-defect die
	if (fcns_raid_enabled(nsid) && (pu->flush_die == aspb->parity_die))
		pu->flush_die = 0;
#endif

	//restore p2l group context
	first_p2l_id = grp_id * p2l_para.p2l_per_grp;
	last_p2l_id = last_wr_page >> p2l_para.pg_p2l_shift;

	grp = &pu->grp[0];
	grp->grp_id = grp_id;
	grp->cur_p2l_cnt = last_p2l_id - first_p2l_id + 1;
	grp->ttl_p2l_cnt = p2l_get_grp_p2l_cnt(grp_id, aspb->flags.b.slc);

	u32 res_id;
	p2l_t *p2l;
	for (p2l_id = first_p2l_id; p2l_id <= last_p2l_id; p2l_id++) {
		res_id = p2l_res_id_get();
		grp->res_id[p2l_id - first_p2l_id] = res_id;

		p2l = &p2l_res_mgr.p2l[res_id];
		p2l->grp = grp;
		p2l->id = p2l_id;
		p2l->flags.all = 0;

		if (last_wr_page >= p2l_get_last_page(p2l_id, aspb->flags.b.slc)) {
			//closed p2l
			grp->updt_p2l_cnt++;
			p2l->flags.b.copy_done = true;
			p2l->updt_cnt = DU_CNT_PER_P2L;
		} else {
			//open p2l
			CBF_FETCH(&p2l_res_mgr.sdtag_pool, p2l->sdtag);
			p2l->smem = sdtag2mem(p2l->sdtag.b.dtag);

			u32 p2l_used_page = last_wr_page - (p2l->id << p2l_para.pg_p2l_shift);
            if(!aspb->flags.b.parity_mix){
				p2l_used_page ++;
			}
			p2l->updt_cnt = p2l_used_page * nand_info.interleave * DU_CNT_PER_PAGE;
		}
	}

	u32 i;
	u32 p2l_save_wptr = aspb->wptr - nand_plane_num();
	if (fcns_raid_enabled(nsid))
		p2l_save_wptr = aspb->wptr - nand_plane_num() * 2;

	u32 spb_type = (aspb->flags.b.slc) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
	pda_t pda = nal_make_pda(aspb->spb_id, p2l_save_wptr * DU_CNT_PER_PAGE);

	ftl_core_trace(LOG_INFO, 0x03f6, "load grp %d from pda 0x%x(spb %d page %d)",
		grp_id, pda, aspb->spb_id, pda2page(pda));

	p2l_ncl_cmd_t *p2l_cmd = p2l_load_cmd_get();
	for (i = 0; i < grp->cur_p2l_cnt; i++) {
		p2l_cmd->pda[i] = ftl_remap_pda(pda + i);

		p2l_cmd->info[i].status = 0;
		p2l_cmd->bm_pl[i].pl.du_ofst = i;
		p2l_cmd->info[i].pb_type = spb_type;

		p2l = &p2l_res_mgr.p2l[grp->res_id[i]];
		if (p2l->flags.b.copy_done)
			p2l_cmd->bm_pl[i].pl.dtag = p2l->ddtag.dtag;
		else
			p2l_cmd->bm_pl[i].pl.dtag = p2l->sdtag.dtag;

		p2l_cmd->bm_pl[i].pl.nvm_cmd_id = rd_dummy_meta_idx;
		p2l_cmd->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
	}

	p2l_cmd->ncl_cmd.caller_priv = NULL;
	p2l_cmd->ncl_cmd.completion = NULL;
	p2l_cmd->ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_TAG_EXT_FLAG;
	p2l_cmd->ncl_cmd.addr_param.common_param.list_len = grp->cur_p2l_cnt;
    p2l_cmd->ncl_cmd.retry_step = 0;
    p2l_cmd->ncl_cmd.du_format_no = host_du_fmt;
	ncl_cmd_submit(&p2l_cmd->ncl_cmd);
	sys_assert(p2l_cmd->ncl_cmd.status == 0);

	p2l_cmd->ncl_cmd.completion = p2l_load_done;
	p2l_cmd->ncl_cmd.flags = NCL_CMD_TAG_EXT_FLAG;
	p2l_load_cmd_put(p2l_cmd);
}

#if 0
fast_code void p2l_resume(aspb_t *aspb, p2l_grp_t *grp, u32 nsid)
{
	u32 p2l_save_wptr = aspb->wptr - nand_plane_num();
	if (fcns_raid_enabled(nsid))
		p2l_save_wptr = aspb->wptr - nand_plane_num() * 2;

	u32 spb_type = (aspb->flags.b.slc) ? NAL_PB_TYPE_SLC : NAL_PB_TYPE_XLC;
	pda_t pda = nal_make_pda(aspb->spb_id, p2l_save_wptr * DU_CNT_PER_PAGE);

	ftl_core_trace(LOG_INFO, 0x919e, "load grp %d from pda 0x%x(spb %d page %d)",
		grp->grp_id, pda, aspb->spb_id, pda2page(pda));

	u32 i;
	p2l_ncl_cmd_t *p2l_cmd = p2l_load_cmd_get();

	for (i = 0; i < grp->cur_p2l_cnt; i++) {
		p2l_cmd->pda[i] = ftl_remap_pda(pda + i);

		p2l_cmd->info[i].status = 0;
		p2l_cmd->bm_pl[i].pl.du_ofst = i;
		p2l_cmd->info[i].pb_type = spb_type;

		u32 res_id = grp->res_id[i];
		p2l_t *p2l = &p2l_res_mgr.p2l[res_id];
		if (p2l->flags.b.ddtag_only || p2l->flags.b.copy_done)
			p2l_cmd->bm_pl[i].pl.dtag = p2l->ddtag.dtag;
		else
			p2l_cmd->bm_pl[i].pl.dtag = p2l->sdtag.dtag;

		p2l_cmd->bm_pl[i].pl.nvm_cmd_id = rd_dummy_meta_idx;
		p2l_cmd->bm_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_IDX;
	}

	p2l_cmd->ncl_cmd.caller_priv = NULL;
	p2l_cmd->ncl_cmd.completion = NULL;
	p2l_cmd->ncl_cmd.flags |= NCL_CMD_SYNC_FLAG;
	p2l_cmd->ncl_cmd.addr_param.common_param.list_len = grp->cur_p2l_cnt;
	ncl_cmd_submit(&p2l_cmd->ncl_cmd);
	sys_assert(p2l_cmd->ncl_cmd.status == 0);

	p2l_cmd->ncl_cmd.completion = p2l_load_done;
	p2l_cmd->ncl_cmd.flags &= ~NCL_CMD_SYNC_FLAG;
	p2l_load_cmd_put(p2l_cmd);
}
#endif

ddr_code void p2l_build_scan_meta_done(struct ncl_cmd_t *ncl_cmd)
{
    #if 0

	u32 i;
	p2l_build_mgr_t *build_mgr = &p2l_build_mgr;
	p2l_build_req_t *build_req = build_mgr->busy_req;
	p2l_ncl_cmd_t *scan_cmd = (p2l_ncl_cmd_t*)ncl_cmd;

	u32 mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
	u32 intlv_du_cnt = nand_info.interleave * DU_CNT_PER_PAGE;
	u32 updt_ptr = nal_pda_offset_in_spb(scan_cmd->pda[0]) & DU_CNT_PER_P2L_MASK;
	io_meta_t *meta = build_mgr->meta + (scan_cmd - build_mgr->scan_cmd) * mp_du_cnt;

	u32 p2l_id_start = build_req->start_page >> p2l_para.pg_p2l_shift;
	u32 p2l_id_curr = build_mgr->scan_page >> p2l_para.pg_p2l_shift;
	lda_t *p2l_base = dtag2mem(build_req->dtags[p2l_id_curr - p2l_id_start]);

	//updt p2l with lda in meta
	for (i = 0; i < mp_du_cnt; i++) {
		if (scan_cmd->info[i].status)
			p2l_base[updt_ptr + i] = INV_LDA;
		else
			p2l_base[updt_ptr + i] = meta[i].lda;
	}

	build_mgr->otf_req--;
	if (build_mgr->otf_req)
		return;

	//continue to scan left die in this interleave page
	if (build_mgr->scan_die < nand_info.lun_num) {
		p2l_build_scan_meta();
	} else if (build_mgr->scan_page < build_req->last_page) {
		//scan next page
		build_mgr->scan_die = 0;
		build_mgr->scan_page++;
		p2l_build_scan_meta();
	} else {
		//check if the p2l is closed
		bool slc = ftl_core_get_spb_type(build_req->spb_id);
		u32 p2l_last_page = p2l_get_last_page(p2l_id_curr, slc);
		if (build_req->last_page < p2l_last_page) {
			//pad INV_LDA to the un-programmed page
			u32 pad_page = build_req->last_page + 1;
			u32 updt_ptr = (pad_page * intlv_du_cnt) & DU_CNT_PER_P2L_MASK;
			for (; updt_ptr < DU_CNT_PER_P2L; updt_ptr++)
				p2l_base[updt_ptr] = INV_LDA;
		}

		build_req->cmpl(build_req);
		build_mgr->busy_req = NULL;
		//ipc_api_log_level_chg(old_log_lvl);

		//chk wait que and exec next build req
		if (list_empty(&build_mgr->wait_que) == false) {
			build_req = list_first_entry(&build_mgr->wait_que, p2l_build_req_t, entry);
			list_del(&build_req->entry);

			build_mgr->busy_req = build_req;
			build_mgr->scan_die = 0;
			build_mgr->scan_page = build_req->start_page;
			p2l_build_scan_meta();
		}
	}
    #endif
}

ddr_code void p2l_build_scan_meta(void)
{  
    #if 0
	u32 i, j;
	pda_t pda;
	u32 spb_type;
	u32 mp_du_cnt;
	u32 intlv_du_cnt;
	u32 scan_die_cnt;
	p2l_ncl_cmd_t *scan_cmd;
	p2l_build_req_t *build_req;
	p2l_build_mgr_t *build_mgr;

	build_mgr = &p2l_build_mgr;
	build_req = build_mgr->busy_req;
	mp_du_cnt = nand_plane_num() * DU_CNT_PER_PAGE;
	intlv_du_cnt = nand_info.interleave * DU_CNT_PER_PAGE;

	pda = nal_make_pda(build_req->spb_id, build_mgr->scan_page * intlv_du_cnt);
	pda += build_mgr->scan_die * mp_du_cnt;

	spb_type = NAL_PB_TYPE_XLC;
	if (ftl_core_get_spb_type(build_req->spb_id))
		spb_type = NAL_PB_TYPE_SLC;

	scan_die_cnt = nand_info.lun_num - build_mgr->scan_die;
	scan_die_cnt = min(scan_die_cnt, MAX_P2L_SCAN_CMD);
	build_mgr->scan_die += scan_die_cnt;
	build_mgr->otf_req = scan_die_cnt;

	for (i = 0; i < scan_die_cnt; i++) {
		scan_cmd = &build_mgr->scan_cmd[i];
		scan_cmd->ncl_cmd.caller_priv = build_mgr;
		scan_cmd->ncl_cmd.completion = p2l_build_scan_meta_done;
		scan_cmd->ncl_cmd.retry_step = 0;
		for (j = 0; j < mp_du_cnt; j++) {
			scan_cmd->info[j].status = 0;
			scan_cmd->info[j].pb_type = spb_type;
			scan_cmd->pda[j] = ftl_remap_pda(pda + i * mp_du_cnt + j);
		}
    	scan_cmd->ncl_cmd.flags &= ~NCL_CMD_COMPLETED_FLAG;

		if (get_target_cpu(i) == CPU_ID)
			ncl_cmd_submit(&scan_cmd->ncl_cmd);
		else
			cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD, 0, (u32)&scan_cmd->ncl_cmd);
	}
    #endif
}

ddr_code void p2l_build_push(p2l_build_req_t *build_req)
{
    #if 0
	p2l_build_req_t *req;
	p2l_build_mgr_t *build_mgr = &p2l_build_mgr;

	INIT_LIST_HEAD(&build_req->entry);
	list_add_tail(&build_req->entry, &build_mgr->wait_que);

	if (build_mgr->busy_req == NULL) {
		req = list_first_entry(&build_mgr->wait_que, p2l_build_req_t, entry);
		list_del(&req->entry);
		//change log level as scan may report much err log
		//old_log_lvl = ipc_api_log_level_chg(LOG_ALW);//tzu cpu will hang?

		build_mgr->busy_req = req;
		build_mgr->scan_die = 0;
		build_mgr->scan_page = req->start_page;
		p2l_build_scan_meta();
	}
    #endif
}

ddr_code void p2l_rc_done(rc_req_t* req){
    struct ncl_cmd_t *ncl_cmd = (struct ncl_cmd_t *)req->caller_priv;
//	ftl_core_trace(LOG_INFO, 0, "p2l_rc_done, ncl_cmd 0x%x", ncl_cmd);
    if(req->flags.b.fail == true){
        ncl_cmd->status |= NCL_CMD_ERROR_STATUS;
        ncl_cmd->retry_step = raid_recover_fail;
    }else{
        ncl_cmd->status &= ~NCL_CMD_ERROR_STATUS;
    }

	ncl_cmd->completion(ncl_cmd);

    sys_free(SLOW_DATA, req);
}

ddr_code rc_req_t* p2l_rc_req_prepare(struct ncl_cmd_t *ncl_cmd)
{
	rc_req_t *req;

	req = sys_malloc(SLOW_DATA, sizeof(rc_req_t));
	sys_assert(req);

	req->list_len = ncl_cmd->addr_param.common_param.list_len;
	req->pda_list= ncl_cmd->addr_param.common_param.pda_list;
	req->info_list = ncl_cmd->addr_param.common_param.info_list;
	req->bm_pl_list = ncl_cmd->user_tag_list;

	req->flags.all = 0;

	req->caller_priv = ncl_cmd;
	req->cmpl = p2l_rc_done;

	return req;
}


static ddr_code void p2l_load_dbg_main1(u16 spb, u16 p2l_id);

static ps_code void p2l_load_dbg_cmpl(void *ctx)
{
    u32 i;
	p2l_load_req_t *load = (p2l_load_req_t*)ctx;


    u32 * p2l_pos = (u32 *)ddtag2mem(p2l_res_mgr.dbg_ddtag_start);
    
    ftl_core_trace(LOG_WARNING, 0x17d9, "p2l_pos 0x%x", p2l_pos);

	ftl_core_trace(LOG_WARNING, 0xb762, "spb %d p2l %d cnt %d load done",load->spb_id, load->p2l_id, load->p2l_cnt);

    for(i = 0; i < 3072; i++)
    {
    	ftl_core_trace(LOG_WARNING, 0xf7cc, "p2l_pos[%d] 0x%x", i, p2l_pos[i]);
    }

	if(load_all_p2l == true)
	{
		if(load->p2l_id == 1152)
		{
			load_all_p2l = false;
	sys_free(FAST_DATA, load);
		}
		else
		{
			mdelay(1000);
			p2l_load_dbg_main1(load->spb_id, load->p2l_id);
			sys_free(FAST_DATA, load);
		}
	}
	else
	{
		sys_free(FAST_DATA, load);
	}
}

static ddr_code int p2l_load_dbg_main(int argc, char *argv[])
{
	u32 i;
	u32 dtag_start = p2l_res_mgr.dbg_ddtag_start;
	p2l_load_req_t *load_req = sys_malloc(FAST_DATA, sizeof(p2l_load_req_t));

	load_req->spb_id = (spb_id_t)strtol(argv[1], (void *)0, 10);
	load_req->p2l_id = strtol(argv[2], (void *)0, 10);
	load_req->p2l_cnt = strtol(argv[3], (void *)0, 10);

	p2l_uart_flag = true;
	if(load_req->p2l_id == 1152)
	{
		load_all_p2l = true;
		load_req->p2l_id = 0;
	}
	for (i = 0; i < load_req->p2l_cnt; i++) {
		load_req->dtags[i].dtag = (dtag_start + i) | DTAG_IN_DDR_MASK;
		memset(dtag2mem(load_req->dtags[i]), 0xFFFFFFFF, 4096);
	}

	load_req->caller = NULL;
	load_req->cmpl = p2l_load_dbg_cmpl;

	ftl_core_p2l_load(load_req);
	return 0;
}
static ddr_code void p2l_load_dbg_main1(u16 spb, u16 p2l_id)
{
	u32 i;
	u32 dtag_start = p2l_res_mgr.dbg_ddtag_start;
	p2l_load_req_t *load_req = sys_malloc(FAST_DATA, sizeof(p2l_load_req_t));
	load_req->spb_id = spb;
	p2l_id += 3;
	load_req->p2l_id = p2l_id;
	load_req->p2l_cnt = 3;
	p2l_uart_flag = true;
	for (i = 0; i < load_req->p2l_cnt; i++) {
		load_req->dtags[i].dtag = (dtag_start + i) | DTAG_IN_DDR_MASK;
		memset(dtag2mem(load_req->dtags[i]), 0xFFFFFFFF, 4096);
	}
	load_req->caller = NULL;
	load_req->cmpl = p2l_load_dbg_cmpl;
	ftl_core_p2l_load(load_req);
}

#if 0
static ps_code int p2l_get_grp_pda_main(int argc, char *argv[])
{
    u32 grp_id = strtol(argv[1], (void *)0, 0);
    u32 spb_id = strtol(argv[2], (void *)0, 0);
    u32 nsid = strtol(argv[3], (void *)0, 0);

    pda_t pda;
    pda_t pgsn_pda;

    p2l_get_grp_pda(grp_id, spb_id, nsid, &pda, &pgsn_pda);

    ftl_core_trace(LOG_INFO, 0x30df, "p2l grp main, grp:%d spb_id:%d nsid:%d, pda:%x", grp_id, spb_id, nsid, pda);
    return 0;
}
static DEFINE_UART_CMD(p2l_pda, "p2l_pda",
    "p2l_pda", "p2l_pda",
    3, 3, p2l_get_grp_pda_main);

#endif

static DEFINE_UART_CMD(p2l_load, "p2l_load",
	"p2l_load", "p2l_load",
	3, 3, p2l_load_dbg_main);



ddr_code pda_t pda2cpda(pda_t pda)
{
    pda_t cpda = 0;
    u32 ch, ce, lun, plane, block, pg_in_wl, du, wl, page;

    // Split each fields
    block = (pda >> shr_nand_info.pda_block_shift) & shr_nand_info.pda_block_mask;
    page  = (pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask;
    lun   = (pda >> shr_nand_info.pda_lun_shift) & (shr_nand_info.geo.nr_luns - 1);
    ce    = (pda >> shr_nand_info.pda_ce_shift) & (shr_nand_info.geo.nr_targets - 1);
    ch    = (pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.geo.nr_channels - 1);
    plane = (pda >> shr_nand_info.pda_plane_shift) & (shr_nand_info.geo.nr_planes - 1);
    du    = (pda >> shr_nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);

    pg_in_wl = page % shr_nand_info.bit_per_cell;
    wl       = page / shr_nand_info.bit_per_cell;

    // Construct CPDA
    cpda = block;
    cpda *= (shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);
    cpda += wl;
    cpda *= shr_nand_info.geo.nr_luns;
    cpda += lun;
    cpda *= shr_nand_info.geo.nr_targets;
    cpda += ce;
    cpda *= shr_nand_info.geo.nr_channels;
    cpda += ch;
    cpda *= shr_nand_info.bit_per_cell;
    cpda += pg_in_wl;
    cpda *= shr_nand_info.geo.nr_planes;
    cpda += plane;
    cpda *= DU_CNT_PER_PAGE;
    cpda += du;

    return cpda;
}

ddr_code pda_t cpda2pda(pda_t cpda)
{
	pda_t pda;
	u32 ch=0, ce=0, lun=0, plane=0, block=0, pg_in_wl=0, du=0, wl=0, page=0;

	// CPDA to ch, ce, lun, pl, blk, pg, du
    du = cpda & (DU_CNT_PER_PAGE - 1);
	cpda >>= DU_CNT_SHIFT;
    plane = cpda & (shr_nand_info.geo.nr_planes- 1);
    cpda >>= ctz(shr_nand_info.geo.nr_planes);
    pg_in_wl = cpda % shr_nand_info.bit_per_cell;
    cpda /= shr_nand_info.bit_per_cell;
    ch = cpda % shr_nand_info.geo.nr_channels;
	cpda /= shr_nand_info.geo.nr_channels;
    ce = cpda % shr_nand_info.geo.nr_targets;
	cpda /= shr_nand_info.geo.nr_targets;
	lun = cpda & (shr_nand_info.geo.nr_luns - 1);
	cpda >>= ctz(shr_nand_info.geo.nr_luns);
    wl = cpda % (shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);
	block = cpda/(shr_nand_info.geo.nr_pages/shr_nand_info.bit_per_cell);

    page = (wl*shr_nand_info.bit_per_cell) + pg_in_wl;

	// ch, ce, lun, pl, blk, pg, du to PDA
	pda = block << shr_nand_info.pda_block_shift;
	pda |= page << shr_nand_info.pda_page_shift;
	pda |= lun << shr_nand_info.pda_lun_shift;
    pda |= ce << shr_nand_info.pda_ce_shift;
    pda |= ch << shr_nand_info.pda_ch_shift;
	pda |= plane << shr_nand_info.pda_plane_shift;
	pda |= du << shr_nand_info.pda_du_shift;

	return pda;
}

ddr_code bool chk_p2l_build_done(u8 * df_ptr)
{
    u32 die_pn_idx;
    u32 idx, off;

    p2l_b_pda = cpda2pda(p2l_b_cpda);

    die_pn_idx = (pda2die(p2l_b_pda) << ctz(shr_nand_info.geo.nr_planes)) | pda2plane(p2l_b_pda);
    idx = die_pn_idx >> 3;
    off = die_pn_idx & (7);

    return ((df_ptr[idx] >> off) & 1);
}

static inline u16 get_p2l_ncl_cmd_idx(void)
{
    u8 _idx;
    if (_p2l_build_free_cmd_bmp == 0)
    {
#ifdef While_break
		u64 start = get_tsc_64();
#endif

        while (_p2l_build_free_cmd_bmp != INV_U16)
        {
//            ftl_core_trace(LOG_INFO, 0, "_p2l_build_free_cmd_bmp 0x%x", _p2l_build_free_cmd_bmp);
            cpu_msg_isr();

#ifdef While_break		
			if(Chk_break(start,__FUNCTION__, __LINE__))
				break;
#endif
        }
    }

    _idx = ctz(_p2l_build_free_cmd_bmp);
    sys_assert(_idx < 16);
    _p2l_build_free_cmd_bmp &= ~(1 << _idx);

    return _idx;
}

static ddr_code bool p2l_build_dbg_cmpl(void)
{
	//dtag=p2l_res_mgr.dbg_p2l_build_ddtag_start;
	//p2l_load_req_t *load_req = (p2l_load_req_t*)p2l_build_ncl->caller_priv;
	//p2l_load_req_t *p2l_build_load = (p2l_load_req_t*)p2l_build_ncl->caller_priv;
	if(!p2l_build_plp_trigger_done)
	{
	    ftl_core_trace(LOG_INFO, 0x8d62, "spbid %d p2lid %d build success ncl_cmd 0x%x", p2l_build_load->spb_id, p2l_build_load->p2l_id, p2l_build_ncl);
		p2l_load_cmd_put(p2l_build_ncl);
		p2l_build_load->cmpl((void*)p2l_build_load);
	}
	#if 0
    u32 i;

//    u32 * p2l_pos = (u32 *)ddtag2mem(p2l_res_mgr.dbg_p2l_build_ddtag_start);
	dtag_t p2l_build_dtag = p2l_build_load->dtags[((p2l_build_load->grp_id%(NUM_P2L_GEN_PDA*2))*3)];
	u32 * p2l_pos = (u32 *)ddtag2mem(p2l_build_dtag.dtag & (~DTAG_IN_DDR_MASK));


    ftl_core_trace(LOG_WARNING, 0x5dd7, "p2l_pos 0x%x", p2l_pos);


    for(i = 0; i < 3072; i++)
    {
    	ftl_core_trace(LOG_WARNING, 0x39c9, "p2l_pos[%d] 0x%x", i, p2l_pos[i]);
    }
	#endif
	bool pend_out_value = p2l_build_pend_out();

    if(pend_out_value && _p2l_build_free_cmd_bmp == INV_U16)
    {
		ftl_core_trace(LOG_INFO, 0xf56d, "pend_out_value %d", pend_out_value);
        p2l_build_flag = false;
        dtag_cont_put(DTAG_T_SRAM, p2l_build_dtag, p2l_build_dtag_cnt);
        p2l_build_ncl_cmd = NULL;
        p2l_build_pda_list = NULL;
        p2l_build_bm_pl = NULL;
        p2l_build_info = NULL;
		pend_out_value = false;
    }

    return pend_out_value;
}


slow_code void p2l_build_send_read_ncl_form(u8 op_code, dtag_t *dtag, pda_t pda, u8 du_cnt,
                                      void (*completion)(struct ncl_cmd_t *))
{
#if 1
    u8   du_idx;
    u16  cont_idx;
    u16  cmd_idx = 0;
    u32  meta_idx;
	u16  die_id;

    //ftl_apl_trace(LOG_ALW, 0, "[IN] ftl_send_read_ncl_form");

    cmd_idx = get_p2l_ncl_cmd_idx();
//	ftl_core_trace(LOG_INFO, 0, "Current cmd_idx : 0x%x, _p2l_build_free_cmd_bmp : 0x%x", cmd_idx, _p2l_build_free_cmd_bmp);

    //  ----- fill in form info -----
    //cont_idx = cmd_idx * du_cnt;
    cont_idx = cmd_idx * DU_CNT_PER_PAGE;
    meta_idx = p2l_build_meta_idx + cont_idx;
//    ftl_core_trace(LOG_INFO, 0, "cont_idx %d", cont_idx);
//	ftl_core_trace(LOG_INFO, 0, "meta_idx %d", meta_idx);

    memset(&p2l_build_info[cont_idx], 0, (du_cnt * sizeof(*p2l_build_info)));

    for(du_idx = 0; du_idx < du_cnt; du_idx++)
    {
        p2l_build_info[cont_idx+du_idx].pb_type = NAL_PB_TYPE_XLC;
        p2l_build_pda_list[cont_idx+du_idx]     = p2l_b_pda+du_idx;

        p2l_build_bm_pl[cont_idx+du_idx].all    = 0;
        p2l_build_bm_pl[cont_idx+du_idx].pl.btag     = cmd_idx;
        p2l_build_bm_pl[cont_idx+du_idx].pl.du_ofst  = du_idx;
        p2l_build_bm_pl[cont_idx+du_idx].pl.type_ctrl  = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
        p2l_build_bm_pl[cont_idx+du_idx].pl.nvm_cmd_id = meta_idx + du_idx;

//        p2l_build_bm_pl[cont_idx+du_idx].pl.dtag = (dtag->dtag+du_idx);
        p2l_build_bm_pl[cont_idx+du_idx].pl.dtag = dtag->dtag;
    }

    p2l_build_ncl_cmd[cmd_idx].addr_param.common_param.info_list = &p2l_build_info[cont_idx];
    p2l_build_ncl_cmd[cmd_idx].addr_param.common_param.pda_list  = &p2l_build_pda_list[cont_idx];
    p2l_build_ncl_cmd[cmd_idx].addr_param.common_param.list_len  = du_cnt;
	die_id = pda2die(p2l_build_pda_list[0]);

    p2l_build_ncl_cmd[cmd_idx].status        = 0;
//    p2l_build_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
//    p2l_build_ncl_cmd[cmd_idx].flags         &= ~(NCL_CMD_COMPLETED_FLAG | NCL_CMD_RCED_FLAG);
    p2l_build_ncl_cmd[cmd_idx].op_code       = op_code;
	p2l_build_ncl_cmd[cmd_idx].die_id        = die_id;

//    p2l_build_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_DATA_READ_PA_DTAG;
#if defined(HMETA_SIZE)
    p2l_build_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG | NCL_CMD_DIS_HCRC_FLAG | NCL_CMD_RETRY_CB_FLAG;
    p2l_build_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_DATA_READ_PA_DTAG;
    p2l_build_ncl_cmd[cmd_idx].du_format_no  = host_du_fmt;
#else
    p2l_build_ncl_cmd[cmd_idx].flags         = NCL_CMD_TAG_EXT_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
    p2l_build_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_TABLE_READ_PA_DTAG;
    p2l_build_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
#endif
	p2l_build_ncl_cmd[cmd_idx].flags         &= ~(NCL_CMD_COMPLETED_FLAG | NCL_CMD_RCED_FLAG);
    //ftl_ncl_cmd[cmd_idx].op_type       = NCL_CMD_FW_TABLE_READ_PA_DTAG;
//    p2l_build_ncl_cmd[cmd_idx].du_format_no  = DU_4K_DEFAULT_MODE;
    p2l_build_ncl_cmd[cmd_idx].completion    = completion;
    p2l_build_ncl_cmd[cmd_idx].user_tag_list = &p2l_build_bm_pl[cont_idx];
    p2l_build_ncl_cmd[cmd_idx].caller_priv   = (void*)p2l_b_pda;
    #if NCL_FW_RETRY
    p2l_build_ncl_cmd[cmd_idx].retry_step    = default_read;
    #endif
    #if RAID_SUPPORT_UECC
    if(op_code == NCL_CMD_P2L_SCAN_PG_AUX)
        p2l_build_ncl_cmd[cmd_idx].uecc_type = NCL_UECC_AUX_RD;
    else
	    p2l_build_ncl_cmd[cmd_idx].uecc_type = NCL_UECC_NORMAL_RD;
    #endif
	p2l_build_ncl_cmd[cmd_idx].flags |= NCL_CMD_SCH_FLAG;

#if 0
    ftl_apl_trace(LOG_ALW, 0, "[FUNC] completion addr : 0x%x, ftl_bm_pl.pl.btag : 0x%x, ftl_bm_pl.pl.nvm_cmd_id : 0x%x",\
                  completion, ftl_bm_pl[cont_idx].pl.btag, ftl_bm_pl[cont_idx].pl.nvm_cmd_id);
#endif
	if (get_target_cpu(die_id) == CPU_ID)
		ncl_cmd_submit_insert_schedule(&p2l_build_ncl_cmd[cmd_idx], false);
	else
		cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_NCMD_INSERT_SCH, 0, (u32)&p2l_build_ncl_cmd[cmd_idx]);
    //ncl_cmd_submit(&p2l_build_ncl_cmd[cmd_idx]);
#endif
}
#if (PLP_SUPPORT == 0)
slow_code u32 pda2wl(pda_t pda)
{
	u32 wl = ((pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)/shr_nand_info.bit_per_cell;
	return wl;
}
#endif
slow_code void page_aux_build_p2l_done(struct ncl_cmd_t *ncl_cmd)
{ 
//    u32 die_pn_idx;
    //u16 page;
//    u8  *df_ptr;
//    u32 idx, off;
    u32   meta_idx, cmd_idx;
    //    u8  blank_flag = mFALSE, uecc_flag = mFALSE;
    u8  i, length;
    struct info_param_t *info_list;

#if(PLP_SUPPORT == 0) 
    epm_FTL_t* epm_ftl_data;
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    pda_t chk_pda;
    u16 chk_wl,chk_die;
#endif
    //      ftl_core_trace(LOG_ALW, 0, "[IN] page_aux_build_p2l_done");

	if(ncl_cmd->flags & NCL_CMD_SCH_FLAG)
	{
		decrease_otf_cnt(ncl_cmd->die_id);
		ncl_cmd->flags &= ~NCL_CMD_SCH_FLAG;
	}

    cmd_idx   = ncl_cmd->user_tag_list[0].pl.btag;
    meta_idx = ncl_cmd->user_tag_list[0].pl.nvm_cmd_id;
    info_list = ncl_cmd->addr_param.common_param.info_list;
    length    = ncl_cmd->addr_param.common_param.list_len;

    pda_t r_pda = (pda_t)ncl_cmd->caller_priv; 
#if (PLP_SUPPORT == 0)     
    u16 open_wlid = pda2wl(r_pda);
    u8 open_dieid = pda2die(r_pda); 
    u16 wl_bit_idx = 0;
#endif  
	u32	p2l_id_off = pda_to_p2l_id(r_pda)%3; 
	u32 du_off = nal_pda_offset_in_spb(r_pda); 
	u32 updt_ptr = du_off & (DU_CNT_PER_P2L_MASK); 
    meta_idx -= p2l_build_meta_idx; 

    u16 grp_id = p2l_build_p2l_id / p2l_para.p2l_per_grp;
	dtag_t p2l_build_dtag = p2l_build_load->dtags[((grp_id%(gc_gen_p2l_num*2))*3+p2l_id_off)];
	//evlog_printk(LOG_INFO,"tzu %d %d %d",p2l_build_dtag,p2l_build_dtag.dtag & (~DTAG_IN_DDR_MASK),p2l_res_mgr.dbg_p2l_build_ddtag_start);
	//ddtag2mem(dtag)
    //u32 * new_build_p2l = (u32 *)ddtag2mem(p2l_res_mgr.dbg_p2l_build_ddtag_start);
	u32 * new_build_p2l = (u32 *)ddtag2mem(p2l_build_dtag.dtag & (~DTAG_IN_DDR_MASK));
	//p2l_load->dtags[0].dtag &(~DTAG_IN_DDR_MASK);
//    ftl_core_trace(LOG_INFO, 0, "new_build_p2l  0x%x", new_build_p2l);
//    ftl_core_trace(LOG_INFO, 0, "p2l_build_meta  0x%x", &p2l_build_meta[0]);

#ifdef SKIP_MODE
    u32 spb_id = pda2blk(r_pda);
#endif

	if(ftl_core_gc_chk_stop() || p2l_build_plp_trigger_done){ 
        _p2l_build_free_cmd_bmp |= (1 << cmd_idx); 
        if(!p2l_build_plp_trigger_done || _p2l_build_free_cmd_bmp == INV_U16){
        	p2l_build_dbg_cmpl();
            p2l_build_plp_trigger_done = true; 
        } 
        return; 
    } 

    #if NCL_FW_RETRY
	extern __attribute__((weak)) void rd_err_handling(struct ncl_cmd_t *ncl_cmd);
    #endif

	if(ncl_cmd->status)
	{
        #if NCL_FW_RETRY
        if (ncl_cmd->retry_step == default_read)
        {
        	for (i = 0; i < length; i++)
        	{
				if((info_list[i].status != ficu_err_good) && (info_list[i].status != ficu_err_du_erased))
				{
#if (PLP_SUPPORT == 0)				
                    if(epm_ftl_data->host_open_blk[0] == spb_id)
                    {
                    	if((open_wlid >= epm_ftl_data->host_open_wl[0]) && (open_wlid < epm_ftl_data->host_open_wl[0] + SPOR_CHK_WL_CNT))
						{
							wl_bit_idx = open_wlid - epm_ftl_data->host_open_wl[0];
                            if((epm_ftl_data->host_open_wl[wl_bit_idx] == open_wlid)&&((epm_ftl_data->host_die_bit[wl_bit_idx]>>open_dieid)&0x1))
                            {
                            	#if (SPOR_NON_PLP_LOG == mENABLE)
                                ftl_core_trace(LOG_INFO, 0xc205, "[NONPLP gc retry] 1--spb %d wl %d die %d pda 0x%x cpda %d ",spb_id, open_wlid, open_dieid,r_pda,p2l_b_cpda);
								#endif
                                goto outo;
                            }
                        }
                        ftl_core_trace(LOG_INFO, 0xbcf3, "p2l_build_rd_err_handling, ncl_cmd: 0x%x, _p2l_build_free_cmd_bmp 0x%x r_pda:0x%x", ncl_cmd, _p2l_build_free_cmd_bmp,r_pda);
                        rd_err_handling(ncl_cmd);
                        return;
                    }
                    else if(epm_ftl_data->gc_open_blk[0] == spb_id)
                    {
                        if((open_wlid >= epm_ftl_data->gc_open_wl[0]) && (open_wlid < epm_ftl_data->gc_open_wl[0] + SPOR_CHK_WL_CNT))
						{
							wl_bit_idx = open_wlid - epm_ftl_data->gc_open_wl[0];
                            if((epm_ftl_data->gc_open_wl[wl_bit_idx] == open_wlid)&&((epm_ftl_data->gc_die_bit[wl_bit_idx]>>open_dieid)&0x1))
                            {
                            	#if (SPOR_NON_PLP_LOG == mENABLE)
                                ftl_core_trace(LOG_INFO, 0x1a9f, "[NONPLP gc retry] 2--spb %d wl %d die %d pda 0x%x cpda %d ",spb_id, open_wlid, open_dieid,r_pda,p2l_b_cpda);
								#endif
                                goto outo;
                            }
                        }
                        ftl_core_trace(LOG_INFO, 0x4b76, "p2l_build_rd_err_handling, ncl_cmd: 0x%x, _p2l_build_free_cmd_bmp 0x%x r_pda:0x%x", ncl_cmd, _p2l_build_free_cmd_bmp,r_pda);
                        rd_err_handling(ncl_cmd);
                        return;
                    }                   
                    else
                    {
                        //ftl_core_trace(LOG_INFO, 0, "p2l_build_rd_err_handling, ncl_cmd: 0x%x, _p2l_build_free_cmd_bmp 0x%x", ncl_cmd, _p2l_build_free_cmd_bmp);
                        ftl_core_trace(LOG_INFO, 0x9118, "rd_err_handling   wl %d die %d pda 0x%x cpda %d", open_wlid, open_dieid,r_pda,p2l_b_cpda);
                        rd_err_handling(ncl_cmd);
                        return;
                        //goto outo;
                    }
#else                    
                    ftl_core_trace(LOG_INFO, 0xf62e, "p2l_build_rd_err_handling, ncl_cmd: 0x%x, _p2l_build_free_cmd_bmp 0x%x r_pda:0x%x", ncl_cmd, _p2l_build_free_cmd_bmp,r_pda);
                    rd_err_handling(ncl_cmd);
                    return;
#endif
				}
			}
        } 
        else if(ncl_cmd->retry_step != raid_recover_fail)
        {
#if (PLP_SUPPORT == 0)
			#if 1
			ftl_core_trace(LOG_INFO, 0xb2e0, "[NONPLP retry fail] spb %d wl %d die %d pda 0x%x cpda %d bitmap:0x%x",spb_id, open_wlid, open_dieid,r_pda,p2l_b_cpda,_p2l_build_free_cmd_bmp);
			//skip raid
            goto outo;

			#else
            if(epm_ftl_data->host_open_blk[0] == spb_id)
            {
            	if((open_wlid >= epm_ftl_data->host_open_wl[0]) && (open_wlid < epm_ftl_data->host_open_wl[0] + SPOR_CHK_WL_CNT))
				{
					wl_bit_idx = open_wlid - epm_ftl_data->host_open_wl[0];
                    if(epm_ftl_data->host_open_wl[wl_bit_idx] == open_wlid)
                    {
                        ftl_core_trace(LOG_INFO, 0x780a, "[NONPLP gc raid] 1--spb %d wl %d die %d pda 0x%x cpda %d bitmap:0x%x",spb_id, open_wlid, open_dieid,r_pda,p2l_b_cpda,_p2l_build_free_cmd_bmp);
                        goto outo;
                    }
                }
            }
            else if(epm_ftl_data->gc_open_blk[0] == spb_id)
            {
                if((open_wlid >= epm_ftl_data->gc_open_wl[0]) && (open_wlid < epm_ftl_data->gc_open_wl[0] + SPOR_CHK_WL_CNT))
				{
					wl_bit_idx = open_wlid - epm_ftl_data->gc_open_wl[0];
                    if(epm_ftl_data->gc_open_wl[wl_bit_idx] == open_wlid)
                    {
                        ftl_core_trace(LOG_INFO, 0xbb7d, "[NONPLP gc raid] 2--spb %d wl %d die %d pda 0x%x cpda %d bitmap:0x%x",spb_id, open_wlid, open_dieid,r_pda,p2l_b_cpda,_p2l_build_free_cmd_bmp);
                        goto outo;
                    }
                }
            }
            #endif//nonplp disable raid
#endif
            u32 nsid = INT_NS_ID;
            bool rced = (ncl_cmd->flags & NCL_CMD_RCED_FLAG) ? true : false;
            if (fcns_raid_enabled(nsid) && is_there_uc_pda(ncl_cmd) && (rced == false))
            {
    	        ftl_core_trace(LOG_INFO, 0x16a8, "p2l_build_RAID, ncl_cmd: 0x%x, _p2l_build_free_cmd_bmp 0x%x r_pda:0x%x", ncl_cmd, _p2l_build_free_cmd_bmp,r_pda);
            	for (i = 0; i < length; i++)
                {
        		    if (info_list[i].status > ficu_err_du_ovrlmt)
                    {
                        rc_req_t* rc_req = p2l_rc_req_prepare(ncl_cmd);
                        raid_correct_push(rc_req);
                        return;
                    }
    	        }
            }
        }
        #endif
	}

    //ftl_core_trace(LOG_INFO, 0, "page_aux_build_p2l_done, ncl_cmd: 0x%x, _p2l_build_free_cmd_bmp 0x%x", ncl_cmd, _p2l_build_free_cmd_bmp);

//     ToDo : current ncl cmd error interrupt might have problem, by Tony
//     ============ Error handle area ============
    for(i=0; i<length; i++)
    {
        if(info_list[i].status == cur_du_erase) // Blank
        {
            blank_cnt++;
        }
    }
//     ============ Error handle area ============

    // ----- fill lda into buffer -----ftl_spor_meta[meta_idx].lda;
    for(i=0; i < length; i++)
    {
//        ftl_core_trace(LOG_INFO, 0, "p2l_build_meta[%d].lda  %d", meta_idx+i, p2l_build_meta[meta_idx+i].lda);
//        if(info_list[i].status == cur_du_erase) // Blank
//        {
//            new_build_p2l[(group * 1024 + posi)]   = INV_LDA;
//        }
//        else 
		if((ncl_cmd->status && (info_list[i].status >= cur_du_ucerr)) 
			|| (p2l_build_meta[meta_idx+i].lda > _max_capacity)) 
		{ 
			new_build_p2l[updt_ptr + i]   = P2L_INV_LDA; 
		} 
        else if(p2l_build_meta[meta_idx+i].lda == FTL_BLIST_TAG)
        {
            new_build_p2l[updt_ptr + i]   = BLIST_LDA;
        }
        else
        {
            new_build_p2l[updt_ptr + i]   = p2l_build_meta[meta_idx+i].lda;
        }

    } // end for(i=0; i < length; i++)
#if(PLP_SUPPORT == 0) 
outo:
#endif
    _p2l_build_free_cmd_bmp |= (1 << cmd_idx);


    dtag_t dtag = {.dtag = (p2l_dummy_dtag | DTAG_IN_DDR_MASK)};
//    df_ptr = get_spb_defect(pda2blk(cpda2pda(p2l_b_cpda)));


//    p2l_build_done:
    if(_p2l_build_free_cmd_bmp == INV_U16)
    {
        if(p2l_b_cpda >= p2l_b_cpda_end)
        {
    //        ftl_core_trace(LOG_INFO, 0, "r_cpda 0x%x", r_cpda);
            p2l_build_dbg_cmpl();
            return;
        }
        #if 1
        else
        {
            if(blank_cnt > 24)
            {
                for(i= p2l_b_cpda; i < (p2l_b_cpda_end - p2l_b_cpda) * 4; i++)
                {
                   ftl_core_trace(LOG_INFO, 0xa14f, "p2l_build_meta[%d].lda  %d", meta_idx+i, p2l_build_meta[meta_idx+i].lda);
                    //group = ((i + rec_posi) / 8) % 3;
                    //posi = ((i + rec_posi) / 24) * 8 + ((i + rec_posi) % 24 % 8);

                    new_build_p2l[updt_ptr + i]   = INV_LDA;

                }
                p2l_b_cpda = p2l_b_cpda_end;
                p2l_build_dbg_cmpl();
                return;
            }
        }
        #endif
    }
    else
    {

#ifdef While_break
			u64 start = get_tsc_64();
#endif			
            while(p2l_b_cpda < p2l_b_cpda_end)
            {
#ifdef SKIP_MODE
                if(chk_p2l_build_done(get_spb_defect((u32)spb_id)) == 0)
#endif
                {
#if(PLP_SUPPORT == 0)                 
                    chk_pda = cpda2pda(p2l_b_cpda);
                    chk_wl = pda2wl(chk_pda);
                    chk_die = pda2die(chk_pda);
                     //ftl_core_trace(LOG_INFO, 0, "[GC_AUX]0---------1 %d",pda2wl(chk_pda));
					if(epm_ftl_data->host_open_blk[0] == spb_id) //open host blk
					{
						// first open wl + 16 >  cur_wl >= first open wl
						if((chk_wl >= epm_ftl_data->host_open_wl[0]) && (chk_wl < epm_ftl_data->host_open_wl[0] + SPOR_CHK_WL_CNT))
						{
							wl_bit_idx = chk_wl - epm_ftl_data->host_open_wl[0];
							 if(epm_ftl_data->host_open_wl[wl_bit_idx] == chk_wl && 
								((epm_ftl_data->host_die_bit[wl_bit_idx]>>chk_die)&0x1))
							{
								#if (SPOR_NON_PLP_LOG == mENABLE)
								ftl_core_trace(LOG_INFO, 0x162a, "[NONPLP gc done]1---------open spb %d wl %d die %d pda 0x%x cpda %d",spb_id,chk_wl, chk_die,chk_pda,p2l_b_cpda);
								#endif
								p2l_b_cpda+=DU_CNT_PER_PAGE;
								continue;
								
							}
						}
					}
					if(epm_ftl_data->gc_open_blk[0] == spb_id)//open gc blk
					{
						// first open wl + 16 >  cur_wl >= first open wl
						if((chk_wl >= epm_ftl_data->gc_open_wl[0]) && (chk_wl < epm_ftl_data->gc_open_wl[0] + SPOR_CHK_WL_CNT))
						{
							wl_bit_idx = chk_wl - epm_ftl_data->gc_open_wl[0];
							 if(epm_ftl_data->gc_open_wl[wl_bit_idx] == chk_wl && 
								((epm_ftl_data->gc_die_bit[wl_bit_idx]>>chk_die)&0x1))
							{
								#if (SPOR_NON_PLP_LOG == mENABLE)
								ftl_core_trace(LOG_INFO, 0x9507, "[NONPLP gc done]2---------open spb %d wl %d die %d pda 0x%x cpda %d",spb_id,chk_wl, chk_die,chk_pda,p2l_b_cpda);
								#endif
								p2l_b_cpda+=DU_CNT_PER_PAGE;
								continue;

							}
						}
					}
#endif                
                    p2l_build_send_read_ncl_form(NCL_CMD_P2L_SCAN_PG_AUX, &dtag, p2l_b_pda, DU_CNT_PER_PAGE, page_aux_build_p2l_done);
                    p2l_b_cpda+=DU_CNT_PER_PAGE; 
//                    ftl_core_trace(LOG_INFO, 0, "[SEND]p2l_b_cpda 0x%x p2l_b_cpda_end 0x%x", p2l_b_cpda ,p2l_b_cpda_end);
                    break;
                }
                p2l_b_cpda+=DU_CNT_PER_PAGE; 
//                ftl_core_trace(LOG_INFO, 0, "[GC_AUX]p2l_b_cpda 0x%x", p2l_b_cpda);

#ifdef While_break		
				if(Chk_break(start,__FUNCTION__, __LINE__))
					break;
#endif				
            }
        }
        if((_p2l_build_free_cmd_bmp == INV_U16) && (p2l_b_cpda >= p2l_b_cpda_end))
        {
//            ftl_core_trace(LOG_INFO, 0, "r_cpda 0x%x", r_cpda);
            p2l_build_dbg_cmpl();
        }
        //else
//            ftl_core_trace(LOG_INFO, 0, "else _p2l_build_free_cmd_bmp 0x%x, p2l_b_cpda 0x%x p2l_b_cpda_end 0x%x", _p2l_build_free_cmd_bmp, p2l_b_cpda ,p2l_b_cpda_end);

}


#if 0
static ddr_code int p2l_build_dbg_main(int argc, char *argv[])
{

    p2l_build_init();

//    u32 die_pn_idx;
//    //u16 page;
    u8  * df_ptr;
//    u32 idx, off;
    dtag_t dtag = {.dtag = (p2l_dummy_dtag | DTAG_IN_DDR_MASK)};
	u16 spb_id = (spb_id_t)strtol(argv[1], (void *)0, 10);
	u16 p2l_id = strtol(argv[2], (void *)0, 10);
//	u16 p2l_cnt = strtol(argv[3], (void *)0, 10);
//	ftl_core_trace(LOG_INFO, 0, "p2l_build_dbg_main %d %d", spb_id, p2l_id);
//	ftl_core_trace(LOG_INFO, 0, "_max_capacity %d", _max_capacity);

    u16 grp_id = p2l_id / p2l_para.p2l_per_grp;

    p2l_b_pda_start = nal_make_pda(spb_id, ((grp_id * shr_nand_info.bit_per_cell) << shr_nand_info.pda_page_shift));
//    ftl_core_trace(LOG_INFO, 0, "[GC_AUX]pda_start 0x%x", pda_start);

    p2l_b_cpda     = pda2cpda(p2l_b_pda_start);

    p2l_b_cpda_end = p2l_b_cpda + (8*8*2*2*4*3);

    df_ptr = get_spb_defect((u32)spb_id);
    if(p2l_b_cpda < p2l_b_cpda_end)
    {
        for(u32 i = 0; i < P2L_BUILD_NCL_CMD_MAX; i++)
        {
//            p2l_b_pda = cpda2pda(p2l_b_cpda);
//        
////            ftl_core_trace(LOG_INFO, 0, "[GC_AUX]cpda 0x%x, pda 0x%x", cpda, pda);
//            die_pn_idx = (pda2die(p2l_b_pda) << 1) | pda2plane(p2l_b_pda);
//            idx = die_pn_idx >> 3;
//         	off = die_pn_idx & (7);
//            if(((df_ptr[idx] >> off)&1)!=0)
          	if(chk_p2l_build_done(df_ptr) != 0)
            {
                // is bad block skip to next page entry
                p2l_b_cpda+=DU_CNT_PER_PAGE;
                continue;
            }

        	p2l_build_send_read_ncl_form(NCL_CMD_P2L_SCAN_PG_AUX, &dtag, p2l_b_pda, DU_CNT_PER_PAGE, page_aux_build_p2l_done);
            p2l_b_cpda+=DU_CNT_PER_PAGE;
        }
    }

    return 0;
}
#endif
slow_code void p2l_build_aux(u32 spb_id,u16 p2l_id)//struct ncl_cmd_t *ncl_cmd)
{

    ftl_core_trace(LOG_INFO, 0x1b5f, "p2l_build_aux %d %d", spb_id, p2l_id);
#if(PLP_SUPPORT == 0)     
    epm_FTL_t* epm_ftl_data;
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    pda_t chk_pda;
    u16 chk_wl,chk_die;
    u16 wl_bit_idx;
#endif   
	u16 grp_id = p2l_id / p2l_para.p2l_per_grp;
	sys_assert(grp_id < shr_p2l_grp_cnt);
    p2l_build_p2l_id = p2l_id;
    if(p2l_build_ncl_cmd == NULL)
    {
        p2l_build_cur_spb_id = (u16)spb_id;
        p2l_build_init();//take
    }
	//p2l_load_req_t *load_req = (p2l_load_req_t*)ncl_cmd->caller_priv;
	//u32 spb_id = load_req->spb_id;
	//u16 p2l_id = load_req->p2l_id;
//    u32 die_pn_idx;
//    //u16 page;

    u8 i = P2L_BUILD_NCL_CMD_MAX;
//    u32 idx, off;
    dtag_t dtag = {.dtag = (p2l_dummy_dtag | DTAG_IN_DDR_MASK)};
	//u16 spb_id = (spb_id_t)strtol(argv[1], (void *)0, 10);
	//u16 p2l_id = strtol(argv[2], (void *)0, 10);
//	u16 p2l_cnt = strtol(argv[3], (void *)0, 10);
//	ftl_core_trace(LOG_INFO, 0, "p2l_build_dbg_main %d %d", spb_id, p2l_id);
//	ftl_core_trace(LOG_INFO, 0, "_max_capacity %d", _max_capacity);
#if(PLP_SUPPORT == 0) 
     if((epm_ftl_data->host_open_blk[0] == (u16)spb_id)||(epm_ftl_data->gc_open_blk[0] == (u16)spb_id))
     {
		for (u8 i = 0; i < p2l_build_load->p2l_cnt; i++)
			memset(dtag2mem(p2l_build_load->dtags[i+((grp_id%(NUM_P2L_GEN_PDA*2))*3)]), 0xFFFFFFFF, 4096);
    }
#endif
    blank_cnt = 0;
    p2l_b_pda_start = nal_make_pda(spb_id, (((grp_id * shr_nand_info.bit_per_cell) << p2l_para.pg_p2l_shift)<< shr_nand_info.pda_page_shift));
//    ftl_core_trace(LOG_INFO, 0, "[GC_AUX]pda_start 0x%x", p2l_b_pda_start);

    p2l_b_cpda     = pda2cpda(p2l_b_pda_start);
    
    p2l_b_cpda_end = (p2l_b_cpda + DU_PER_P2L_GRP);

#ifdef SKIP_MODE
	u8	* df_ptr;
	df_ptr = get_spb_defect((u32)spb_id);
#endif

    if(p2l_b_cpda < p2l_b_cpda_end)
    {
//        for(u32 i = 0; i < P2L_BUILD_NCL_CMD_MAX; i++)
        while(i > 0)
        {
//            p2l_b_pda = cpda2pda(p2l_b_cpda);
//        
////            ftl_core_trace(LOG_INFO, 0, "[GC_AUX]cpda 0x%x, pda 0x%x", cpda, pda);
//            die_pn_idx = (pda2die(p2l_b_pda) << 1) | pda2plane(p2l_b_pda);
//            idx = die_pn_idx >> 3;
//         	off = die_pn_idx & (7);
//            if(((df_ptr[idx] >> off)&1)!=0)
#ifdef SKIP_MODE
				if(chk_p2l_build_done(df_ptr) != 0)
				{
					// is bad block skip to next page entry
					p2l_b_cpda+=DU_CNT_PER_PAGE;
					continue;
				}
#endif
#if(PLP_SUPPORT == 0) 
                chk_pda = cpda2pda(p2l_b_cpda);
                chk_wl = pda2wl(chk_pda);
                chk_die = pda2die(chk_pda);
                //ftl_core_trace(LOG_INFO, 0, "[GC_AUX]0---------1  %d",pda2wl(chk_pda));
                if(epm_ftl_data->host_open_blk[0] == spb_id) //open host blk
                {
                    // first open wl + 16 >  cur_wl >= first open wl
                    if((chk_wl >= epm_ftl_data->host_open_wl[0]) && (chk_wl < epm_ftl_data->host_open_wl[0] + SPOR_CHK_WL_CNT))
                    {
                        wl_bit_idx = chk_wl - epm_ftl_data->host_open_wl[0];
                         if(epm_ftl_data->host_open_wl[wl_bit_idx] == chk_wl && 
                            ((epm_ftl_data->host_die_bit[wl_bit_idx]>>chk_die)&0x1))
                        {
                        	#if (SPOR_NON_PLP_LOG == mENABLE)
                            ftl_core_trace(LOG_INFO, 0x9342, "[NONPLP gc aux]1---------open spb %d wl %d die %d pda 0x%x cpda %d",spb_id,chk_wl, chk_die,chk_pda,p2l_b_cpda);
							#endif
                            p2l_b_cpda+=DU_CNT_PER_PAGE;
                            continue;

                        }
                    }
                }
                if(epm_ftl_data->gc_open_blk[0] == spb_id)//open gc blk
                {
                    // first open wl + 16 >  cur_wl >= first open wl
                    if((chk_wl >= epm_ftl_data->gc_open_wl[0]) && (chk_wl < epm_ftl_data->gc_open_wl[0] + SPOR_CHK_WL_CNT))
                    {
                        wl_bit_idx = chk_wl - epm_ftl_data->gc_open_wl[0];
                         if(epm_ftl_data->gc_open_wl[wl_bit_idx] == chk_wl && 
                            ((epm_ftl_data->gc_die_bit[wl_bit_idx]>>chk_die)&0x1))
                        {
                        	#if (SPOR_NON_PLP_LOG == mENABLE)
                            ftl_core_trace(LOG_INFO, 0xed70, "[NONPLP gc aux]2---------open spb %d wl %d die %d pda 0x%x cpda %d",spb_id,chk_wl, chk_die,chk_pda,p2l_b_cpda);
							#endif
                            p2l_b_cpda+=DU_CNT_PER_PAGE;
                            continue;
                            
                        }
                    }
                }
                if(p2l_b_cpda >= p2l_b_cpda_end)
                	break;

#endif
        	p2l_build_send_read_ncl_form(NCL_CMD_P2L_SCAN_PG_AUX, &dtag, p2l_b_pda, DU_CNT_PER_PAGE, page_aux_build_p2l_done);
            p2l_b_cpda+=DU_CNT_PER_PAGE;
            i--;
        }
    }
	if(i == P2L_BUILD_NCL_CMD_MAX)
		p2l_build_dbg_cmpl();
}
#if 0
static DEFINE_UART_CMD(p2l_build, "p2l_build",
    "p2l_build", "p2l_build",
    2, 2, p2l_build_dbg_main);
#endif

