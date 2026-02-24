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

/*! \file pstream.c
 * @brief define physical stream object, supply physical page resource
 *
 * \addtogroup fcore
 * \defgroup pstream
 * \ingroup fcore
 * @{
 * it will help to allocate new SPB from FTL, and provide page resource
 */
#include "fcprecomp.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"
#include "ipc_api.h"
#include "die_que.h"
#include "pstream.h"
#include "ftl_core.h"
#include "ftl_raid.h"
#include "ftl_export.h"
#include "ftl_remap.h"
#include "ftl_p2l.h"
#include "../ftl/ftl_l2p.h"
#include "console.h"
#include "../ftl/ErrorHandle.h"
#include "rdisk.h"
#include "nvme_spec.h"
#include "misc_register.h"

/*! \cond PRIVATE */
#define __FILEID__ pstr
#include "trace.h"
/*! \endcond PRIVATE */

//#define DEBUG_DEFECT_BLOCK
u8 skip_mode_debug_flag=0;
#ifdef SKIP_MODE
#include "srb.h"
#endif

fast_data_zi u32 du_per_intlv;		///< du count per interleave
fast_data_zi u32 pg_per_intlv;		///< page count per interleave
fast_data u8 ps_wait_semi = 0xFF;
share_data_zi volatile u16 ps_open[3];
share_data_zi volatile u8  QBT_BLK_CNT;
share_data_zi volatile bool shr_shutdownflag;
share_data_ni volatile close_blk_ntf_que_t close_host_blk_que;
share_data_ni volatile close_blk_ntf_que_t close_gc_blk_que;
share_data_zi volatile bool shr_format_fulltrim_flag;
share_data_zi volatile int temperature;
extern TFtlPbftType tFtlPbt;
extern ftl_core_ctx_t *qbt_core_ctx;
extern u64 shr_nand_bytes_written;	// DBG, SMARTVry
extern tencnet_smart_statistics_t *tx_smart_stat;
extern volatile bool shr_qbt_prog_err;
extern volatile u8 plp_trigger;
extern volatile u8 shr_gc_read_disturb_ctrl;
extern  u32 seg_w_blist[3];
extern  u32 cur_pbt_sn;

#if (PLP_FORCE_FLUSH_P2L == mENABLE)
extern  volatile spb_id_t plp_spb_flush_p2l;
#endif
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
extern volatile u8 shr_slc_flush_state;
#endif
#if 0//def ERRHANDLE_GLIST	// Should be move into header file later.
share_data_zi bool  fgFail_Blk_Full;        // need define in sram
share_data_zi sGLTable *pGList;             // need define in Dram  // GL_mod, Paul_20201130
share_data_zi u16   wOpenBlk[MAX_OPEN_CNT]; // need define in Dram
share_data_zi u8    bErrOpCnt;
share_data_zi u8    bErrOpIdx;
share_data_zi bool  FTL_INIT_DONE;

share_data_zi ncl_w_req_t *CloseReq;
share_data_zi u32 *CloseAspb;
share_data_zi u32 ClSBLK_H;									//host close blk

share_data_zi sGLEntry errInfo2;
share_data_zi sGLEntry errInfo4;
share_data_zi sEH_Manage_Info sErrHandle_Info;  
share_data_zi MK_FailBlk_Info Fail_Blk_Info_Temp;
share_data_zi u8 bFail_Blk_Cnt;                               //need define in sram
share_data_zi MK_FailBlk_Info Fail_Blk_Info[MAX_FAIL_BLK_CNT];

#endif
void defect_set(u32 spb_id, u32 die_idx, u32 plane);

/*!
 * @brief check if physical stream was full, if all active spb slots were occupied, it was full
 *
 * @param ps	physical stream object
 *
 * @return	return true if it was full
 */
fast_code bool pstream_full(pstream_t *ps)
{
	//return (ps->aspb[0].spb_id != INV_SPB_ID && ps->aspb[1].spb_id != INV_SPB_ID);
	return (ps->aspb[ps->curr].spb_id != INV_SPB_ID);
}

init_code void pstream_init(pstream_t *ps, u8 du_cnt_per_cmd, spb_queue_t *spb_que, u8 nsid, u8 type)
{	
	u8 i;
	ps->nsid = nsid;
	ps->type = type;
	ps->curr = 0;
	ps->aspb[0].spb_id = INV_SPB_ID;
	//ps->aspb[1].spb_id = INV_SPB_ID;
	ps->aspb[0].remap_tbl = sys_malloc(FAST_DATA, sizeof(u16) * nand_interleave_num() * 2);
	sys_assert(ps->aspb[0].remap_tbl);
	//ps->aspb[1].remap_tbl = &ps->aspb[0].remap_tbl[nand_interleave_num()];

	ps->du_cnt_per_cmd = du_cnt_per_cmd;
	ps->avail_cnt = 0;
	ps->flags.all = 0;
	ps->spb_que = spb_que;
    ps->parity_cnt = 0;
#if(SPOR_FLOW == mDISABLE)
    ps->qbt_cnt = 0;
    ftl_core_trace(LOG_ALW, 0x0e27, "Pstream QBT cnt : %d", ps->qbt_cnt);
#endif
	ps->flags.b.p2l_enable = fcns_p2l_enabled(nsid);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(nsid == HOST_NS_ID && type == FTL_CORE_SLC)
		ps->flags.b.p2l_enable = false;
#endif
	p2l_user_init(&ps->pu[0], ps->nsid);
	//p2l_user_init(&ps->pu[1], ps->nsid);

	du_per_intlv = nand_info.lun_num * nand_plane_num() * DU_CNT_PER_PAGE;
	pg_per_intlv = nand_info.lun_num * nand_plane_num();

	for(i = 0; i < 3; i++){
		ps_open[i] = INV_U16;
	}

	for(i = 0; i < 2; i++){
		host_flush_blklist_done[i] = 0;
		blist_flush_ctx[i]		   = NULL;
	}
}

fast_code bool pstream_ready(pstream_t *ps)
{
	return !!ps->avail_cnt;
}

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
slow_code void pstream_clear_epm_vac(void)
{
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(shr_slc_flush_state != SLC_FLOW_DONE)
	{	
		first_usr_open = false;
		//ftl_core_trace(LOG_ALW, 0x2bb3, "shr_slc_flush_state:%d",shr_slc_flush_state);
		return;
	}
#endif
	
	if(power_on_update_epm_flag != POWER_ON_EPM_UPDATE_ENABLE)
	{
		//first_usr_open = false;
		return;
	}

	gpio_int_t gpio_int_status;
	gpio_int_status.all = readl((void *)(MISC_BASE + GPIO_INT));
	if((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT)) && !plp_trigger)
	{
		//plp isr trigger ,but gpio_isr can't response
		extern volatile bool cc_en_set;	
		ftl_core_trace(LOG_PLP, 0x3b86,"power off without plp trigger ,just return ,cc_en_set:%d",cc_en_set);//add by Jay
		return;

	}

	extern epm_info_t*  shr_epm_info;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	//epm_ftl_data->epm_record_idx = 0;	
	epm_ftl_data->panic_build_vac = 0;
	epm_ftl_data->epm_record_full = 0;
    epm_ftl_data->glist_inc_cnt = 0;
    #ifdef Dynamic_OP_En
    epm_ftl_data->Set_OP_Start = INV_U32;
    #endif

    if(!plp_trigger)//avoid reset spor_tag after plp_copy_ftl_data_to_epm
    {
		epm_ftl_data->spor_tag = INV_U32;
		epm_ftl_data->last_close_host_blk = INV_U16;
		epm_ftl_data->last_close_gc_blk = INV_U16;
		memset(epm_ftl_data->blk_sn, 0xFF, 2048*4);
		memset(epm_ftl_data->pda_list, 0xFF, 2048*4);
    }
	else
	{
		ftl_core_trace(LOG_ALW, 0x6b0e, "plp not clear vac");
		return;
	}
	/*
	//ftl_core_trace(LOG_ALW, 0, "[IN]clear_epm_vac");
	extern volatile u8 plp_trigger;
	#if 0//(POWER_ON_OPEN == ENABLE)
	if (!plp_trigger) 
	#else
	if ((first_usr_open == true)&&(irq_int_done))
	#endif
	{
		ftl_core_trace(LOG_ALW, 0x3561, "[Do]clear_epm_vac");
		epm_update(FTL_sign,(CPU_ID-1));	
	} 
	else 
	{
		first_usr_open = false;
		
	}
	*/
	if(irq_int_done)
	{	
		power_on_update_epm_flag = POWER_ON_EPM_UPDATE_START;
		epm_update(FTL_sign,(CPU_ID-1));	
		/*
		u64 time_cost = get_tsc_64();
		while(power_on_update_epm_flag == POWER_ON_EPM_UPDATE_START)
		{
			// 1s force return ,avoid fail
			if(time_elapsed_in_ms(time_cost) > 200 )
			{
				ftl_core_trace(LOG_WARNING, 0x365f, "pstream update epm overtime");
				break;
			}
			if(plp_trigger)
				break;
		};
		*/
	}
#if 0//(PLP_SLC_BUFFER_ENABLE  == mENABLE)
	ftl_slc_pstream_enable();
#endif

}
#endif

extern volatile bool delay_flush_spor_qbt;
fast_code bool pstream_rsvd(pstream_t *ps)
{
	if (pstream_full(ps) == false || ps->flags.b.query_nxt) 	//active after pstream done 
	{
        if (plp_trigger)
        {   
            if (!(ps->nsid == HOST_NS_ID && ps->type == FTL_CORE_NRM))
            {
                return false;
            }
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
            else if (SLC_init == true){
                return false;
            }
#endif
        }

		if (CBF_EMPTY(ps->spb_que) && ps->flags.b.queried == 0) {			
			if((shr_qbtflag || delay_flush_spor_qbt) && (!((ps->nsid == 2) && (ps->type == FTL_CORE_NRM))))
			{
				//ftl_core_trace(LOG_INFO, 0, "[FTL]abort_open:%d", shr_qbtflag);
				return false;
			}
			ps->flags.b.queried = 1;
			ps->flags.b.query_nxt = 0;
			/*
			if((ps->nsid == INT_NS_ID) && (ps->type == FTL_CORE_NRM))
			{
				ps->qbt_cnt++;
				if(ps->qbt_cnt == QBT_BLK_CNT)
				{
					ps->flags.b.qbt_done = 1;
					ftl_core_trace(LOG_ERR, 0, "[FTL]qbt_grp_done %d qbt_grp_cnt %d\n", ps->flags.b.qbt_done, ps->qbt_cnt);
					ps->qbt_cnt = 0;
				}
			}
			*/
			sys_assert(ps->nsid != 0);
			
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(ps->nsid == 1 && ps->type != FTL_CORE_SLC)//skip blist
#else
			if(ps->nsid == 1)
#endif
			{
				ftl_core_return_blklist_ctx(ps->type);
				ipc_api_flush_blklist(ps->nsid, ps->type);  // adams mark
			}
			ipc_api_spb_query(ps->nsid, ps->type);
		} else {
			return pstream_supply(ps);
		}
		return false;
	}else{
		if(ftl_core_flush_blklist_need_resume()){  // adams mark
			return false;
		}

	}

	return false;
}

fast_code bool pstream_supply(pstream_t *ps)
{
	u8 cur = ps->curr;
	u32 i = 0;
    u16 good_plane_ttl_cnt = nand_info.lun_num * nand_info.geo.nr_planes;
	spb_ent_t ent;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(ps->nsid == HOST_NS_ID && ps->type == FTL_CORE_SLC)
	{
		//SLC blk 1 
		ent.all      = 0;
		ent.b.spb_id = PLP_SLC_BUFFER_BLK_ID;
		ent.b.sn 	 = 0xFFF6;
		ent.b.slc 	 = 1;
	}
	else
	{
#endif
		if(ftl_core_flush_blklist_need_resume()){
			return false;
		}

		if (pstream_full(ps))
			return false;
	
		if (CBF_EMPTY(ps->spb_que))
			return false;
	
		CBF_FETCH(ps->spb_que, ent.all);	
	
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	}
#endif	

	if(ent.b.ps_abort == true){
		if(ps->type < FTL_CORE_PBT){
			if(blist_flush_ctx[ps->type] != NULL){
				ftl_flush_misc_t *ctx = blist_flush_ctx[ps->type];
				//ctx->ctx.cmpl(&ctx->ctx);	//Jira434 free sdtag Move to CPU3 by pis20220411
				//blist_flush_ctx[ctx->type] = NULL;
				ftl_core_put_ctx(&ctx->ctx);
			}
		}
		CBF_MAKE_EMPTY(ps->spb_que);
		ps->flags.b.queried = 0;
		ps->flags.b.query_nxt = 0;
		ftl_core_trace(LOG_INFO, 0x2284, "ps_supply_again ns:%d, type:%d",ps->nsid,ps->type);
		return false;
	}

	if(ent.b.spb_id <= 0 || ent.b.spb_id == INV_SPB_ID){
		return false;
	}

	do {
		if (ps->aspb[cur].spb_id == INV_SPB_ID) {
			bool ret;
			ps->aspb[cur].spb_id = ent.b.spb_id;
			ps->aspb[cur].flags.all = 0;
			ps->aspb[cur].flags.b.valid = true;
			ret = ftl_get_remap_tbl(ent.b.spb_id, ps->aspb[cur].remap_tbl);
			if (ret == false) {
				u32 j;

				for (j = 0; j < nand_interleave_num(); j++)
					ps->aspb[cur].remap_tbl[j] = ent.b.spb_id;
			}

#ifdef SKIP_MODE
#ifdef DEBUG_DEFECT_BLOCK
            u32 bad_block_cnt, die_idx, bad_plane;
            //random set some block to bad block
            if (ps->nsid == 1) {
                //trng_gen_random(&bad_block_cnt, 1);

                bad_block_cnt = get_tsc_lo() % 10; //random 0-5 bad block
                u32 blk_idx = 0;
                for (blk_idx = 0; blk_idx < bad_block_cnt; blk_idx++){
                    //trng_gen_random(&die_idx, 1);
                    die_idx = get_tsc_lo() % 128;
                   // trng_gen_random(&bad_plane, 1);
                    bad_plane = get_tsc_lo() & 0x02;
                    bad_plane+=1;
                    if (bad_plane != 0) {
                        ftl_core_trace(LOG_INFO, 0x7f7b, "force set bad block:%d die:%d plane:%d", ps->aspb[cur].spb_id, die_idx,
                        bad_plane);
                        defect_set(ps->aspb[cur].spb_id, die_idx, bad_plane);
                    }
                }
                u32 flag = 1;//(get_tsc_lo() & 0x01);
                if (flag == 0x01) {
                    u32 plane = get_tsc_lo() & 0x02;
                    plane += 1;
                    if (plane != 0) {
                        ftl_core_trace(LOG_INFO, 0xae9c, "force set p2l die to bad blk:%d die:%d plane:%d",
                        ps->aspb[cur].spb_id, 127, plane);
                        defect_set(ps->aspb[cur].spb_id, 127, plane);
                    }
                }

            }


#endif



			u8* ftl_df_ptr = get_spb_defect(ps->aspb[cur].spb_id);

			ps->aspb[cur].open_skip_cnt=0;
            ps->aspb[cur].total_bad_die_cnt = 0;
            u8 parity_die = nand_info.lun_num;
            u8 parity_die_pln_pair = BLOCK_NO_BAD;
            bool parity_mix = false;
            if (fcns_raid_enabled(ps->nsid)) {
                parity_mix = true;
                parity_die = nand_info.lun_num - 1;
                u32 plane_idx = nand_interleave_num() - nand_plane_num();
                while(1)
    			{
    				u8 pln_pair = get_defect_pl_pair(ftl_df_ptr, plane_idx);

                    if ((pln_pair == BLOCK_ALL_BAD) && (plane_idx / nand_plane_num() == parity_die)) {
                        parity_die--;
                    } else {
                        parity_die_pln_pair = pln_pair;
                        if(nand_info.geo.nr_planes - pop32(parity_die_pln_pair) == 1) {
                            parity_mix = false;
                        }
                        break;
                    }

                    if (plane_idx == 0)
                        break;
                    plane_idx -= nand_plane_num();
    			}
                sys_assert(parity_die != 0xFF);
                sys_assert(parity_die != 0); //ftl_core_flush_blklist_done

            }

            u32 plane_idx = nand_interleave_num() - nand_plane_num();
            while(1)
			{
				u8 pln_pair = get_defect_pl_pair(ftl_df_ptr, plane_idx);
                u8 bad_pn = pop32(pln_pair);
                good_plane_ttl_cnt -= bad_pn;

                if (pln_pair != 0 && (!plp_trigger))
                    ftl_core_trace(LOG_INFO, 0xe85a, "plane_idx: %u, pln_pair: 0x%x", plane_idx, pln_pair);   

                if(nand_plane_num() == bad_pn)
                    ps->aspb[cur].total_bad_die_cnt ++;

                if (plane_idx == 0)
                    break;
                plane_idx -= nand_plane_num();
			}

            ps->aspb[cur].parity_die = parity_die;
            ps->aspb[cur].flags.b.parity_mix = parity_mix;
            ps->aspb[cur].parity_die_pln_idx = nand_info.geo.nr_planes;
            ps->aspb[cur].parity_die_pln_pair = parity_die_pln_pair;
            if(fcns_raid_enabled(ps->nsid)){
                bool ret = false;
                for (u8 pl = 0; pl < nand_plane_num(); pl++){
                    if ((parity_die_pln_pair & (1 << pl)) == 0){ //good plane
                        ps->aspb[cur].parity_die_pln_idx = pl;
                        ret = true;
                        break;
                    }
                }
                sys_assert(ret);
            }

			int k;
            for(k = 0; k < nand_interleave_num(); k += nand_plane_num())
			{
				u8 pln_pair = get_defect_pl_pair(ftl_df_ptr, k);

				if(pln_pair != BLOCK_ALL_BAD)
				{
                    if (pln_pair != 0) {
                        //ftl_core_trace(LOG_ERR, 0, "bad pln!!blk:%d die:%d pland:%d\n",ps->aspb[cur].spb_id, k/2, pln_pair);
                    }
					break;
				}else
				{
					ps->aspb[cur].open_skip_cnt++;
				}
			}



            u32 defect_cnt = nand_interleave_num() - good_plane_ttl_cnt;

            //ftl_core_trace(LOG_ERR, 0, "defect cnt:%d\n", defect_cnt);
			if (ent.b.slc) {
				ps->aspb[cur].flags.b.slc = 1;
				ps->aspb[cur].max_ptr = pg_cnt_in_slc_spb;
				ps->aspb[cur].defect_max_ptr = pg_cnt_in_slc_spb - (defect_cnt * (nand_page_num()/nand_bits_per_cell()));

			} else {
				ps->aspb[cur].max_ptr = pg_cnt_in_xlc_spb;
				ps->aspb[cur].defect_max_ptr = pg_cnt_in_xlc_spb - (defect_cnt* nand_page_num());
			}
            if (!plp_trigger){
        		ftl_core_trace(LOG_INFO, 0x7b9b, "total_bad_die_cnt:%u open_skip_cnt:%u defect_cnt:%u, parity info(die:%d pln_pair %u)"
        		    ,ps->aspb[cur].total_bad_die_cnt, ps->aspb[cur].open_skip_cnt, defect_cnt, parity_die,parity_die_pln_pair); //ray
            }
#else
            ps->aspb[cur].total_bad_die_cnt = 0;

		#if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
            if (fcns_raid_enabled(ps->nsid) || ps->flags.b.p2l_enable || plp_spb_flush_p2l)
		#else
			if (fcns_raid_enabled(ps->nsid) || ps->flags.b.p2l_enable)
		#endif
			{

                u8 parity_die = nand_info.lun_num - 1;


                            //defect_set(ps->aspb[cur].spb_id, parity_die, BLOCK_ALL_BAD);
                        //pln_pair = BLOCK_ALL_BAD;


                            //ftl_core_trace(LOG_ERR, 0, "blk:%d die:%d plane:%d\n",ps->aspb[cur].spb_id, plane_idx/2, pln_pair);
                        //ftl_core_trace(LOG_ERR, 0, "bad die!blk:%d die:%d\n",ps->aspb[cur].spb_id, plane_idx/2);


                if (fcns_raid_enabled(ps->nsid)) {
                    sys_assert(parity_die != 0xFF);
                    sys_assert(parity_die != 0);
                }

               // ps->aspb[cur].p2l_page = nand_info.bit_per_cell;
                //ps->aspb[cur].p2l_die = p2l_die;
                ps->aspb[cur].parity_die = parity_die;
                ftl_core_trace(LOG_ERR, 0xc810, "current blk:%d parity die:%d", ps->aspb[cur].spb_id, parity_die);
            }

                        //ftl_core_trace(LOG_ERR, 0, "bad pln!!blk:%d die:%d pland:%d\n",ps->aspb[cur].spb_id, k/2, pln_pair);

			if (ent.b.slc) {
				ps->aspb[cur].flags.b.slc = 1;
				ps->aspb[cur].max_ptr = pg_cnt_in_slc_spb;
			} else {
				ps->aspb[cur].max_ptr = pg_cnt_in_xlc_spb;
			}
#endif
            if (fcns_raid_enabled(ps->nsid)) {
                ps->aspb[cur].ttl_usr_intlv = good_plane_ttl_cnt - 1;
            }

			ps->aspb[cur].flags.b.print = true;
			ftl_core_set_spb_type(ent.b.spb_id, ent.b.slc);
			ftl_core_set_spb_open(ent.b.spb_id);
			if(ps->nsid == 1 && ps->type == FTL_CORE_NRM)
            {         
                Nand_Written += (shr_nand_info.geo.nr_pages * shr_nand_info.page_sz / 1024 / 1024) * (good_plane_ttl_cnt - RAID_SUPPORT);     // PHY_BLK_SIZE = 18MB
                shr_nand_bytes_written += ((Nand_Written >> 5) & 0xFFFFFFFF); 				// Unit is 32MB, DBG, SMARTVry
				Nand_Written &= 0x1F;														// Remain value.
				//ftl_core_trace(LOG_INFO, 0, "[SMART] nandWritnMB[32/remain](%d/%d)", shr_nand_bytes_written, Nand_Written);
				ps_open[FTL_CORE_NRM] = ent.b.spb_id;
				if(shr_gc_read_disturb_ctrl)
					ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_GC, ftl_core_gc_wr_done);
#if (POWER_ON_OPEN == DISABLE)
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
				if(first_usr_open == false){
					first_usr_open = true;
					pstream_clear_epm_vac();
				}
#endif
#endif
            }
			if(ps->nsid == 1 && ps->type == FTL_CORE_GC)
            {      
                Nand_Written += (shr_nand_info.geo.nr_pages * shr_nand_info.page_sz / 1024 / 1024) * (good_plane_ttl_cnt - RAID_SUPPORT);     // PHY_BLK_SIZE = 18MB
                shr_nand_bytes_written += ((Nand_Written >> 5) & 0xFFFFFFFF); 	 				// Unit is 32MB, DBG, SMARTVry
				Nand_Written &= 0x1F;														// Remain value.
                //ftl_core_trace(LOG_INFO, 0, "Nand_Written:%d good_plane_cnt[0]:%d good_plane_cnt[1]:%d PHY_BLK_SIZE :%d",(Nand_Written&0xFFFFFFFF), good_plane_cnt[0], good_plane_cnt[1], PHY_BLK_SIZE);
				ps_open[FTL_CORE_GC] = ent.b.spb_id;   
#if (POWER_ON_OPEN == DISABLE)
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
				if(first_usr_open == false){
					first_usr_open = true;
					pstream_clear_epm_vac();
				}
#endif
#endif
            }

#if (PBT_OP == mENABLE)
			if(ps->nsid != 2 && ps->type != FTL_CORE_PBT){
				ftl_pbt_cover_usr();
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			max_usr_blk_sn = ent.b.sn;
#endif
			}

            ftl_core_trace(LOG_INFO, 0xa1e3, "spb:%d(%d/%d), sn:0x%x, tbl_cvr:%d(%d)",ps->aspb[cur].spb_id,ps->nsid,ps->type,ent.b.sn,ftl_get_pbt_cover_usr_cnt(),ftl_get_pbt_cover_percent());

            if(ps->nsid == 2 && ps->type == FTL_CORE_PBT)
            {      
				ps_open[FTL_CORE_PBT] = ent.b.spb_id;
				cur_pbt_sn = ent.b.sn;
                ftl_pbt_supply_init();
            }		
#endif	
            ipc_api_pstream_get_open_ack(ent.b.spb_id);
			
			ps->aspb[cur].cmpl_cnt = 0;
			ps->aspb[cur].cmpl_ptr = 0;
			//ps->aspb[cur].wptr = 0;
			ps->aspb[cur].sn = ent.b.sn;
#ifdef SKIP_MODE
			ps->aspb[cur].wptr = 0;
			ps->aspb[cur].wptr += (ps->aspb[cur].open_skip_cnt*nand_info.geo.nr_planes);

			ps->avail_cnt += ps->aspb[cur].max_ptr;
			u32 page_cnt = 3 * nand_plane_num();
			if (ent.b.slc)
			{
				page_cnt = 1 * nand_plane_num();
			}

			ps->avail_cnt -= (ps->aspb[cur].open_skip_cnt * page_cnt);
#else
			ps->aspb[cur].wptr = 0;
			ps->avail_cnt += ps->aspb[cur].max_ptr;
#endif
			//ps->avail_cnt += ps->aspb[cur].max_ptr;
			#if 0
			if (fcns_raid_enabled(ps->nsid))
				set_spb_raid_info(&ps->aspb[cur]);
            #endif

		#if 0//(PLP_FORCE_FLUSH_P2L == mENABLE)
			if (ps->flags.b.p2l_enable || plp_spb_flush_p2l)
		#else
			if (ps->flags.b.p2l_enable)
		#endif
			{
				p2l_user_init(&ps->pu[cur], ps->nsid);
                p2l_get_next_pos(&ps->aspb[cur], &ps->pu[cur], ps->nsid);
                ps->aspb[cur].p2l_grp_id = 0;
            }
			
			if (ps->nsid == INT_NS_ID && ps->type == FTL_CORE_NRM && tbl_flush_block) {
				//tbl_flush_block = false;
				if(tbl_flush_ctx != NULL){
					ftl_core_seg_flush(tbl_flush_ctx);
				}
			}

			/*
			if(ps->nsid == 1){
				if(ps->flags.b.flush_blklist_start == 0){
					//ftl_core_trace(LOG_ERR, 0, "flush_begin");
					ipc_api_flush_blklist(ps->nsid, ps->type);
					ps->flags.b.flush_blklist_start = 1;
				}
				return false;
			}else{
				return true;
			}
			*/
			seg_w_blist[ps->type] = INV_U32;
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(ps->nsid == HOST_NS_ID && ps->type != FTL_CORE_SLC)
#else
			if(ps->nsid == HOST_NS_ID)//adams mark
#endif
			{
				ps->flags.b.flush_blklist_start = 1;
				ftl_core_flush_blklist_porc(ps->type);
				return false;
			}
			return true;
		}
		cur++;
		if (cur >= MAX_ASPB_IN_PS)
			cur = 0;
	} while (++i < MAX_ASPB_IN_PS);

	panic("impossible");
	return false;
}

u8 pstream_get_bad_die_cnt(pstream_t *ps)
{
    return ps->aspb[ps->curr].total_bad_die_cnt;
}

fast_code void pstream_get_cwl(pstream_t *ps, core_wl_t *cwl)
{
	/* TODO: add mu support */
	cwl->cur.die_qt = nand_info.lun_num;
	cwl->cur.page = 1;
	cwl->cur.die = 0;
	cwl->flags.all = 0;
	cwl->cur.open_die = 0;
	cwl->cur.start_die = 0;
	cwl->cur.next_open = 0;
	cwl->cur.issue_cnt = 0;
    cwl->cur.handle_ce_die = 0;


	if (ps->aspb[ps->curr].flags.b.slc) {
#if defined(SEMI_WRITE_ENABLE)
		if (ps->type == FTL_CORE_NRM && ps_wait_semi != true) {
			btn_semi_wait_switch(true);
			ps_wait_semi = true;
		}
#endif
		cwl->cur.page = nand_plane_num();
		cwl->flags.b.slc = 1;
		cwl->cur.mpl_qt = nand_info.lun_num * nand_plane_num();
		cwl->cur.usr_mpl_qt = nand_info.lun_num * nand_plane_num();
	} else {
#if defined(SEMI_WRITE_ENABLE)
        #if SEMI_WAIT_FALSE
        share_data_zi volatile stGC_GEN_INFO gGCInfo; 
        if (gGCInfo.mode.all16 && (ps_wait_semi != true)) {   //gc start,force semi wait true
            btn_semi_wait_switch(true);
			ps_wait_semi = true;
        }
        else if (ps->type == FTL_CORE_NRM && (ps_wait_semi != false)) {
			btn_semi_wait_switch(false);
			ps_wait_semi = false;
		}
        #else
		#ifdef WCMD_DROP_SEMI
        extern u32 dropsemi;
        if (dropsemi && ps_wait_semi != false){
            btn_semi_wait_switch(false);
            ps_wait_semi = false;
        }
		else
		#endif
		if (ps_wait_semi != true) {
			btn_semi_wait_switch(true);
			ps_wait_semi = true;
		}
        #endif
#endif

#if defined(USE_TSB_NAND)
        if (ps->nsid == 1) {
            cwl->cur.page = 3 * nand_plane_num();
            cwl->flags.b.slc = 0;
            cwl->cur.mpl_qt = nand_info.lun_num * 3 * nand_plane_num();
            cwl->cur.usr_mpl_qt = nand_info.lun_num * 3 * nand_plane_num();
        } else {
            cwl->cur.page = 3 * nand_plane_num();
            cwl->flags.b.slc = 0;
            cwl->cur.mpl_qt = nand_info.lun_num * 3 *  nand_plane_num();
            cwl->cur.usr_mpl_qt = nand_info.lun_num * 3 * nand_plane_num();
        }
#else
		u32 fpage = ps->aspb[ps->curr].wptr / nand_info.interleave;
		cwl->cur.page = mu_get_xlc_cwl(fpage);
		cwl->flags.b.slc = 0;
		cwl->cur.mpl_qt = nand_info.lun_num * cwl->cur.page;
		cwl->cur.usr_mpl_qt = nand_info.lun_num * cwl->cur.page;
#endif
	}

	if (fcns_raid_enabled(ps->nsid)) {
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
		if(cwl->flags.b.slc)
			cwl->cur.usr_mpl_qt -= 1; // 1page
		else
#endif
			cwl->cur.usr_mpl_qt -= XLC;  // 3page
		stripe_user_t *su = ftl_core_get_su(ps->nsid, ps->type);
		su->active = raid_get_cwl_stripe(ps, cwl->cur.page);
        ps->parity_cnt++;
        extern u32 parity_allocate_cnt;
        parity_allocate_cnt++;
	}

#ifdef SKIP_MODE
	cwl->cur.total_skip_cnt_in_ce = 0;
    u8 open_skip_cnt = ps->aspb[ps->curr].open_skip_cnt;
	cwl->cur.die_qt -= open_skip_cnt;
	cwl->cur.die += open_skip_cnt;
	cwl->cur.start_die += open_skip_cnt;
	cwl->cur.next_open += open_skip_cnt;
	cwl->cur.mpl_qt -= (open_skip_cnt*cwl->cur.page);
	cwl->cur.usr_mpl_qt -= (open_skip_cnt*cwl->cur.page);
	cwl->cur.skip_die_cnt = 0;
#if(H_MODE_WRITE == mENABLE) 
    cwl->cur.total_skip_cnt_in_ce += open_skip_cnt;
    cwl->cur.good_die_bm[0] = 0xFF;
    cwl->cur.good_die_bm[1] = 0xFF;
    if(open_skip_cnt != 0)
    {
        u8 skipCnt;

        for(skipCnt = 0; skipCnt < open_skip_cnt; skipCnt++)
        {
                clear_bit(skipCnt, &cwl->cur.good_die_bm[0]);
                clear_bit(skipCnt, &cwl->cur.good_die_bm[1]);
        }
        //show_log = false;
    }else{
        //show_log = false;
    }
#endif
    //cwl->cur.exp_issue_cnt = nand_info.lun_num - ps->aspb[ps->curr].total_bad_die_cnt;
    //cwl->cur.exp_issue_cnt = nand_info.lun_num;
#endif

}

#ifdef SKIP_MODE
//for debug pda
#if 0
void pda_chk(pda_t pda)
{
	u32 du = (pda >> shr_nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);
	//u32 pl = pda2plane(pda);
	u32 pl = (pda >> shr_nand_info.pda_plane_shift) & (shr_nand_info.geo.nr_planes - 1);
	u32 ch = pda2ch(pda);
	u32 ce = pda2ce(pda);
	u32 lun = pda2lun(pda);
	u32 page = pda2page(pda);
	u32 blk = pda2blk(pda);

	//ftl_core_trace(LOG_ERR, 0, "\ndu[%d] pl[%d] ch[%d] ce[%d] lun[%d] page[%d] blk[%d]\n",du,pl,ch,ce,lun,page,blk);
}
#endif
//
u8* get_spb_defect(u32 spb_id)
{
	//u32 interleave = get_interleave();
	u32 df_width = occupied_by(shr_nand_info.interleave, 8) << 3;
	u32 index = (spb_id * df_width) >> 3;
	return (gl_pt_defect_tbl + index);
}

void defect_set(u32 spb_id, u32 die_idx, u32 plane)
{
    u32 df_width = occupied_by(shr_nand_info.interleave, 8);
	u32 index = (spb_id * df_width);
    u8* ftl_df = gl_pt_defect_tbl + index;
    die_idx = die_idx << 1;
    u32 idx = die_idx >> 3;
	u32 off = die_idx & (7);

    ftl_df[idx] |= (plane << off);
    //ftl_core_trace(LOG_ERR, 0, "spb:%d die:%d bbt:%x\n", spb_id, die_idx, ftl_df[idx]);
}


u8 defect_check(u8* ftl_df, u32 interleave)
{
	//(bitmap[index >> 5] & (1 << (index & 0x1f)));
	u32 idx = interleave >> 3;
	u32 off = interleave & (7);
	return ((ftl_df[idx] >> off)&1);
}


fast_code u8 get_defect_pl_pair(u8* ftl_df, u32 interleave)
{
	//(bitmap[index >> 5] & (1 << (index & 0x1f)));
	if(interleave%shr_nand_info.geo.nr_planes!=0) panic("interleave not pl 0\n");
	u32 idx = interleave >> 3;
	u32 off = interleave & (7);
	if (shr_nand_info.geo.nr_planes == 4)
		return ((ftl_df[idx] >> off)&0xf);
	else
		return (((ftl_df[idx] >> off)&0x3));
}


fast_code u32 pstream_get_pda(pstream_t *ps, pda_t *pda, pda_t *l2p_pda, u32 page, u8 exp_fact, ncl_w_req_t *req)
{
	/* TODO: defect aspb handle */
	u32 skip_die_cnt = 0;
	aspb_t *aspb = &ps->aspb[ps->curr];
	u8 good_pl_cnt = 0;
	u32 page_cnt = page * exp_fact;
    page /= nand_plane_num();

	//req->req.op_type.b.one_pln = 0;
	req->req.op_type.b.pln_write = 0;

	sys_assert((aspb->max_ptr - aspb->wptr) >= page_cnt);
	sys_assert(ps->avail_cnt >= page_cnt);
	u8* ftl_df_ptr = get_spb_defect(aspb->spb_id);

	/*Assign pda (Low page pda) to good plane, skip bad plane*/
	u32 itl;
    u8 pln_pair = get_defect_pl_pair(ftl_df_ptr, (aspb->wptr & (nand_interleave_num() - 1)));
	for (itl=0; itl < nand_info.geo.nr_planes; itl++){ 
		if ((pln_pair&1) == 0){ //good plane
			pda[good_pl_cnt] = nal_make_pda(aspb->spb_id, aspb->wptr << DU_CNT_SHIFT);
			good_pl_cnt++;
		}	
		aspb->wptr++;
        pln_pair >>= 1;
	}
	if (good_pl_cnt != nand_info.geo.nr_planes) //e.g. bad_plane = 1, three pln write
		req->req.op_type.b.pln_write = good_pl_cnt;

	if (page > 1) {
		u32 k;
		u32 j, i;
		pda_t *p = pda;
		//pda_t *lp = l2p_pda;

		k = good_pl_cnt;
		for (j = 1; j < page; j++) {
			for (i = 0; i < good_pl_cnt; i++)
			{
				pda[k] = p[i] + du_per_intlv;
				//l2p_pda[k] = lp[i] + du_per_intlv;
				//ftl_core_trace(LOG_ERR, 0, "pg > 1 pda[%d]=0x%x ,l2p_pda[%d]=0x%x\n",k,pda[k],k,l2p_pda[k]);
				k++;
			}
			p += good_pl_cnt;
			//lp += pl_cnt;
		}

	}
	ps->avail_cnt -= page_cnt;

    extern void jump_to_last_wl(pstream_t *ps);
	if ((aspb->wptr & (pg_per_intlv - 1)) == 0) 
    {    
		aspb->wptr += (pg_per_intlv * (page - 1)); 

        if (tFtlPbt.isfilldummy == mTRUE && ps->nsid == INT_NS_ID && ps->type == FTL_CORE_PBT) 
        { 
            if(aspb->wptr != aspb->max_ptr && aspb->wptr != aspb->max_ptr - pg_per_intlv * page) 
            { 
                tFtlPbt.fill_wl_cnt++; 
                 
                //if PBT has filled a layer, go to the last wl to fill it 
                if (tFtlPbt.fill_wl_cnt == 4) 
                { 
    			    jump_to_last_wl(ps); 
                } 
            } 
        } 
    } 

	for(;;)
	{
		if (aspb->wptr >= aspb->max_ptr) break;
		pln_pair = get_defect_pl_pair(ftl_df_ptr, (aspb->wptr & (nand_interleave_num() - 1)));
		if(pln_pair != BLOCK_ALL_BAD)
		{
			break;
        }
        else
		{
			aspb->wptr += nand_plane_num();
            ps->avail_cnt -= page_cnt;
			skip_die_cnt++;
            
			//last two pl defect
			if ((aspb->wptr & (pg_per_intlv - 1)) == 0) 
            {    
            	aspb->wptr += (pg_per_intlv * (page - 1));   
                if (tFtlPbt.isfilldummy == mTRUE && ps->nsid == INT_NS_ID && ps->type == FTL_CORE_PBT) 
                { 
                    if(aspb->wptr != aspb->max_ptr && aspb->wptr != aspb->max_ptr - pg_per_intlv * page) 
                    { 
                        tFtlPbt.fill_wl_cnt++; 
                        
                        //if PBT has filled a layer, go to the last wl to fill it 
                        if (tFtlPbt.fill_wl_cnt == 4) 
                        { 
            			    jump_to_last_wl(ps); 
                        } 
                    } 
                } 
            } 
		}
	}

	if (aspb->wptr >= aspb->max_ptr) {
		ps->curr++;
		if (ps->curr >= MAX_ASPB_IN_PS)
			ps->curr = 0;
	}

	return skip_die_cnt;
}


#else

fast_code u32 pstream_get_pda(pstream_t *ps, pda_t *pda, pda_t *l2p_pda, u32 page, u8 exp_fact)
{
	/* TODO: defect aspb handle */

	u32 i;
	u32 cur = ps->curr;
	aspb_t *aspb = &ps->aspb[ps->curr];

    //adams if (ps->nsid == HOST_NS_ID)
    {
        page /= nand_plane_num();
    }
	u32 page_cnt = page * nand_plane_num() * exp_fact;
	u32 iid = aspb->wptr & (nand_interleave_num() - 1);

	sys_assert((aspb->max_ptr - aspb->wptr) >= page_cnt);
	sys_assert(ps->avail_cnt >= page_cnt);

	for (i = 0; i < nand_plane_num() * exp_fact; i++) {
		sys_assert(iid < nand_interleave_num());

		spb_id_t spb_id = aspb->remap_tbl[iid];

		pda[i] = nal_make_pda(spb_id, aspb->wptr << DU_CNT_SHIFT);
		l2p_pda[i] = nal_make_pda(aspb->spb_id, aspb->wptr << DU_CNT_SHIFT);
		iid++;
		aspb->wptr++;
	}

	if (page > 1) {
		u32 k;
		u32 j;
		pda_t *p = pda;
		pda_t *lp = l2p_pda;

		k = nand_plane_num();
		for (j = 1; j < page; j++) {
			for (i = 0; i < nand_plane_num(); i++) {
				pda[k] = p[i] + du_per_intlv;
				l2p_pda[k] = lp[i] + du_per_intlv;
				k++;
			}
			p += nand_plane_num();
			lp += nand_plane_num();
		}
	}

	ps->avail_cnt -= page_cnt;

	if ((aspb->wptr & (pg_per_intlv - 1)) == 0) 
    {    
		aspb->wptr += (pg_per_intlv * (page - 1)); 

        if (tFtlPbt.isfilldummy == mTRUE && ps->nsid == INT_NS_ID && ps->type == FTL_CORE_PBT) 
        { 
            if(aspb->wptr != aspb->max_ptr && aspb->wptr != aspb->max_ptr - pg_per_intlv * page) 
            { 
                extern void jump_to_last_wl(pstream_t *ps); 
                tFtlPbt.fill_wl_cnt++; 
                 
                //if PBT has filled a layer, go to the last wl to fill it 
                if (tFtlPbt.fill_wl_cnt == 4) 
                { 
    			    jump_to_last_wl(ps); 
                } 
            } 
        } 
    } 

	if (aspb->wptr >= aspb->max_ptr) {
		ps->curr++;
		ftl_set_pbt_filldummy_done();
		if (ps->curr >= MAX_ASPB_IN_PS)
			ps->curr = 0;
	}

	return cur;
}
#endif

fast_code void pstream_done(pstream_t *ps, ncl_w_req_t *req)
{
	aspb_t *aspb = &ps->aspb[req->req.aspb_id];
    #if 0
	u32 du_off = nal_pda_offset_in_spb(req->w.l2p_pda[0]);
	u32 wptr = du_off >> DU_CNT_SHIFT;

	if (req->req.nsid != INT_NS_ID)
	{
		//ftl_core_trace(LOG_ERR, 0, "req->req.aspb_id %d aspb->spbid %d\n",req->req.aspb_id ,aspb->spb_id);
		//ftl_core_trace(LOG_ERR, 0, "aspb->cmpl_ptr %d ,wptr %d \n", aspb->cmpl_ptr, wptr);
		//ftl_core_trace(LOG_ERR, 0, "req->req.die_id %d\n", req->req.die_id);
		//sys_assert(aspb->cmpl_ptr == wptr);
	}
    #endif
      /*
       * 1. host prog completes in sequence, so cmpl_ptr update in sequence
       * 2. l2p prog completes out of order, but when flush l2p desc, there's no outstanding l2p prog
	*/
	if (aspb->flags.b.slc) {
		aspb->cmpl_ptr += nand_plane_num();
	} else {
		aspb->cmpl_ptr += nand_plane_num();
		if (req->req.die_id == (nand_info.lun_num - 1))
			aspb->cmpl_ptr += nand_info.interleave * (req->req.tpage - 1);
	}

	aspb->cmpl_cnt += req->req.cnt;
#ifdef SKIP_MODE
	if (aspb->cmpl_cnt == aspb->defect_max_ptr) 
#else 
	if (aspb->cmpl_cnt == aspb->max_ptr) 
#endif
    {
		aspb->cmpl_ptr = aspb->max_ptr;
		if (aspb->flags.b.err)
			ftl_core_err_handle_done(req, aspb->spb_id);

		/* issue valid count check to L2PE*/
		/* FTL will handle result and set this SPB closed */
		l2p_status_cnt_fetch(aspb->spb_id, 0, 0);

        // Paul_20201202   
        if (aspb->flags.b.err)
        {
            // EH block closed, force_pad_spb_done will trigger GC directly.
            extern spb_rt_flags_t *spb_rt_flags;
        	spb_rt_flags[aspb->spb_id].b.open = false;    

            // TBC_EH, used in CPU 2
            //extern MK_FailBlk_Info Fail_Blk_Info_Temp;
            //Fail_Blk_Info_Temp.all          = 0;   
        	//Fail_Blk_Info_Temp.b.wBlkIdx    = aspb->spb_id;
        	//Fail_Blk_Info_Temp.b.bType      = ps->type;
        	//Fail_Blk_Info_Temp.b.bClsdone   = true;
        	//Fail_Blk_Info_Temp.b.bGCdone    = false;    
            //cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SET_FAILBLK, 0, (u32)&Fail_Blk_Info_Temp);
        }
        else
        {    
		    ftl_core_set_spb_close(aspb->spb_id, ps->type, ps->nsid);
	    }

		if(ps->nsid == HOST_NS_ID)
		{
#if CROSS_TEMP_OP
			if (temperature < MIN_TEMP || temperature > MAX_TEMP)
			{
				cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SET_SPB_OVER_TEMP_FLAG, 0, aspb->spb_id);
			}
#endif
			if(ps->type == FTL_CORE_NRM)
				ps_open[FTL_CORE_NRM] = INV_U16;
			else if(ps->type == FTL_CORE_GC)
				ps_open[FTL_CORE_GC] = INV_U16; 
		}
		else if(ps->nsid == INT_NS_ID && ps->type == FTL_CORE_PBT){
			if(tFtlPbt.force_dump_pbt == true){
				tFtlPbt.force_cnt++;
				if(tFtlPbt.force_cnt == QBT_BLK_CNT){
					if(tFtlPbt.force_dump_pbt_mode == DUMP_PBT_FORCE_FINISHED_CUR_GROUP || tFtlPbt.force_dump_pbt_mode == DUMP_PBT_NORMAL_PARTIAL){
						ftl_set_force_dump_pbt(0,false,DUMP_PBT_REGULAR);
					}			
				}
			}
			ps_open[FTL_CORE_PBT] = INV_U16; 	
		}
		#if (TRIM_SUPPORT == ENABLE)
        extern u8* TrimBlkBitamp;
		ftl_core_trace(LOG_ERR, 0xa921, "pstream done blk %d %d(%d), trimBlk 0x%x", aspb->spb_id,ps->nsid,ps->type, test_bit(aspb->spb_id, TrimBlkBitamp));
        #else
		ftl_core_trace(LOG_ERR, 0x5a00, "pstream done blk %d %d(%d)", aspb->spb_id,ps->nsid,ps->type);
        #endif
		aspb->spb_id = INV_SPB_ID;

		// issue IPC to get next SPB
		if((ps->nsid == INT_NS_ID) && (ps->type == FTL_CORE_NRM))
		{

#if (SPOR_FLOW == mENABLE)
            if (shr_qbt_prog_err)
            {
                shr_qbt_loop = (shr_qbt_loop/QBT_BLK_CNT)*QBT_BLK_CNT;
            }
            else if(((++shr_qbt_loop)%QBT_BLK_CNT) == 0)
			{
				ftl_core_trace(LOG_ALW, 0x4d3f, "[FTL]psdone not to open blk qbt_cnt %d", shr_qbt_loop);
                qbt_core_ctx = NULL;
                if(shr_qbt_loop == (QBT_BLK_CNT*2))  // 2 grp of QBT blocks
                {
                    shr_qbt_loop = 0;
                }
			}
#else
            ps->qbt_cnt++;
			if(ps->qbt_cnt == QBT_BLK_CNT)
			{
				ftl_core_trace(LOG_ERR, 0xb92a, "[FTL]psdone not to open blk qbt_cnt %d", ps->qbt_cnt);
				ps->qbt_cnt = 0;
			}	
#endif
			else
			{
				pstream_rsvd(ps);
			}
		}
		else if((shr_format_fulltrim_flag == true) || (host_idle == true) || (shr_shutdownflag == true))
		{
			ftl_core_trace(LOG_INFO, 0xac93, "[FTL]not to open blk.format_fulltrim:%u, host_idle:%u, shutdownflag:%u", shr_format_fulltrim_flag, host_idle, shr_shutdownflag);
		}
		else
		{
			pstream_rsvd(ps);
		}
		//pstream_supply(ps);
	}
}

init_code void pstream_restore(pstream_t *ps, u32 idx, spb_fence_t *spb_fence)
{
	aspb_t *aspb = &ps->aspb[idx];
	bool ret;

	aspb->flags.all = 0;
	if (spb_fence->spb_id == INV_SPB_ID) {
		aspb->spb_id = INV_SPB_ID;
		goto end;
	}

	aspb->wptr = spb_fence->ptr;
	aspb->cmpl_cnt = spb_fence->ptr;
	aspb->cmpl_ptr = spb_fence->ptr;

	aspb->flags.b.valid = true;
	aspb->flags.b.slc = spb_fence->flags.b.slc;
	if (aspb->flags.b.slc)
		aspb->max_ptr = pg_cnt_in_slc_spb;
	else
		aspb->max_ptr = pg_cnt_in_xlc_spb;

	aspb->sn = spb_fence->sn;
	aspb->spb_id = spb_fence->spb_id;

	if (aspb->wptr < aspb->max_ptr)
		ftl_core_set_spb_open(aspb->spb_id);

	ret = ftl_get_remap_tbl(aspb->spb_id, aspb->remap_tbl);
	if (ret == false) {
		u32 j;

		for (j = 0; j < nand_interleave_num(); j++)
			aspb->remap_tbl[j] = aspb->spb_id;
	}

	ftl_core_trace(LOG_INFO, 0x55f7, "ns %d t %d aspb [%d] spb %d sn %d w %x",
			ps->nsid, ps->type, idx, spb_fence->spb_id, spb_fence->sn, spb_fence->ptr);

	ps->avail_cnt += aspb->max_ptr - aspb->wptr;

	if (ps->flags.b.p2l_enable) {
		sys_assert(aspb->spb_id != INV_SPB_ID);
		if (aspb->wptr && (aspb->wptr != aspb->max_ptr))
			p2l_open_grp_restore(aspb, &ps->pu[idx], ps->nsid);
	}

	if (aspb->wptr == aspb->max_ptr)
		aspb->spb_id = INV_SPB_ID;

	if ((ps->curr != idx) && (aspb->spb_id != INV_SPB_ID)) {
		if (aspb->wptr || (ps->aspb[ps->curr].spb_id == INV_SPB_ID))
			ps->curr = idx;
	}

end:
	ftl_core_trace(LOG_INFO, 0x81a6, "ava %d cur %d", ps->avail_cnt, ps->curr);
}

fast_code void pstream_reset(pstream_t *ps)
{
	ps->curr = 0;
	ps->aspb[0].spb_id = INV_SPB_ID;
	//ps->aspb[1].spb_id = INV_SPB_ID;

	ps->avail_cnt = 0;
	ps->flags.all = 0;
	
	ps->flags.b.p2l_enable = fcns_p2l_enabled(ps->nsid);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	if(ps->nsid == HOST_NS_ID && ps->type == FTL_CORE_SLC)
		ps->flags.b.p2l_enable = false;
#endif

	if(ps->nsid == 1 && ps->type == FTL_CORE_NRM)
		ps_open[FTL_CORE_NRM] = INV_U16;
	if(ps->nsid == 1 && ps->type == FTL_CORE_GC)
		ps_open[FTL_CORE_GC] = INV_U16;
	if(ps->nsid == 2 && ps->type == FTL_CORE_PBT)
		ps_open[FTL_CORE_PBT] = INV_U16;		

	CBF_INIT(ps->spb_que);
	if (ps->flags.b.p2l_enable) {
		u32 i;

		for (i = 0; i < MAX_ASPB_IN_PS; i++)
			p2l_user_reset(&ps->pu[i], ps->nsid);
	}
}

fast_code void pstream_resume(pstream_t *ps)
{
    #if 1
    sys_assert(0); //todo for resume delete by Vito
    #else
	u32 i;
	aspb_t *aspb;
	p2l_grp_t *grp;

	if (fcns_p2l_enabled(ps->nsid)) {
		aspb = &ps->aspb[ps->curr];
		for (i = 0; i < GRP_PER_P2L_USR; i++) {
			grp = &ps->pu[ps->curr].grp[i];
			//open p2l group
			if (grp->ttl_p2l_cnt)
				p2l_resume(aspb, grp, ps->nsid);
		}
	}
    #endif
}



#ifdef SKIP_MODE
ddr_code int spb_defect_dump_main(int argc, char *argv[])
{
    u32 i;

	if (argc == 1) {
		ftl_core_trace(LOG_ERR, 0x9fa5, "plz input blk\n");
	}else {
		u32 spb_id = atoi(argv[1]);
		//u32 spb_id = strtol(argv[1], (void *)0, 0);
		u32 spb_cnt = shr_nand_info.geo.nr_blocks; //shr_nand_info.geo.nr_blocks

		if (spb_id >= spb_cnt) {
			ftl_core_trace(LOG_ERR, 0x1880, "invalid spb\n");
			return -1;
		}

		u8* defect_map = gl_pt_defect_tbl;
		u32 interleave=shr_nand_info.interleave;
		u32 index = (spb_id * interleave) >> 3;
		u32 offset = interleave >> 3;

		ftl_core_trace(LOG_ERR, 0x4ad4, "CYC : nr_blocks %d, interleave %d, index %d, offset %d \n",shr_nand_info.geo.nr_blocks, shr_nand_info.interleave, index, offset);
		ftl_core_trace(LOG_ERR, 0x1cf4, "CYC1 : gl_pt_defect_tbl %x, gl_pt_defect_tbl %x \n",gl_pt_defect_tbl, &gl_pt_defect_tbl);
		ftl_core_trace(LOG_ERR, 0xf547, "CYC2 : nand_info.interleave %x  \n",nand_info.interleave);
		ftl_core_trace(LOG_ERR, 0x7c83, "CYC3 : pl %d, ch %d, ce %d, lun %d \n",shr_nand_info.geo.nr_planes,shr_nand_info.geo.nr_channels,shr_nand_info.geo.nr_targets,shr_nand_info.geo.nr_luns);
		ftl_core_trace(LOG_ERR, 0x7914, "CYC4 : pl %d, ch %d, ce %d, lun %d \n",ninfo.geo.nr_planes,ninfo.geo.nr_channels,ninfo.geo.nr_targets,ninfo.geo.nr_luns);

		u8 *defect_map_blk = &defect_map[index];
		for(i = index;i<(index+offset);i++)
		{
			ftl_core_trace(LOG_ERR, 0xa295, "defect_map[%d]=0x%x\n",i,defect_map[i]);
		}

		//

		ftl_core_trace(LOG_ERR, 0x59ce, "defect_map check : \n");
		for(i=0;i<interleave;i++)
		{
			u32 idx = i >> 3;
			u32 off = i & (7);

			u8 defect_bit = (defect_map_blk[idx] >> off) & 1;

			if (defect_bit)
			{
				u32 pl = ((i >> 0) & (shr_nand_info.geo.nr_planes - 1));
				u32 ch = ((i >> FTL_PLN_BTS) & (shr_nand_info.geo.nr_channels - 1));
				u32 ce = ((i >> (FTL_PLN_BTS+FTL_CH_BTS)) & (shr_nand_info.geo.nr_targets - 1));
				u32 lun = ((i >> (FTL_PLN_BTS+FTL_CH_BTS+FTL_CE_BTS)) & (shr_nand_info.geo.nr_luns - 1));
				ftl_core_trace(LOG_ERR, 0x3032, "pl[%d] ch[%d] ce[%d] lun[%d] is defect\n",pl,ch,ce,lun);
			}
		}
	}


//#define FTL_PLN_BTS		ctz(ninfo.geo.nr_planes)
//#define FTL_CH_SHF		(FTL_PLN_SHF + FTL_PLN_BTS)
//#define FTL_CH_BTS		ctz(ninfo.geo.nr_channels)
//#define FTL_CE_SHF		(FTL_CH_SHF + FTL_CH_BTS)
//#define FTL_CE_BTS		ctz(ninfo.geo.nr_targets)
//#define FTL_LUN_SHF		(FTL_CE_SHF + FTL_CE_BTS)
//#define FTL_LUN_BTS		ctz(ninfo.geo.nr_luns)

	return 0;
}



static DEFINE_UART_CMD(spb_defect_dump, "spb_defect_dump",
					   "input spb_id",
					   "print defect tbl",
					   1, 1, spb_defect_dump_main);

ps_code static int dump_main(int argc, char *argv[])
{
   u32 *addr = 0;
   u32 dw_cnt = 0;
   //u8 i = 0;
   char *endp;
   u32 reg = strtoul(argv[1], &endp, 0);
   printk("\nR0x%x : (0x%x)", reg, readl((const void *)reg));  

   u32 bytecnt = atoi(argv[2]);        

   addr = (u32*)reg;
   dw_cnt = bytecnt >> 2;

   printk("\n");
   while (dw_cnt > 0) {
       if (dw_cnt < 4)
           dw_cnt = 4;

       //printk("R%08x: %08x %08x %08x %08x\n",
       //addr, *addr[0], *addr[1], *addr[2], *addr[3]);
       printk("R%08x: %08x %08x %08x %08x\n", addr, *(addr), *(addr+1), *(addr+2), *(addr+3));     
       //printk("R%08x: %08x %08x %08x %08x\n",addr, readl((const void *)addr[0]), readl((const void *)addr[1]), readl((const void *)addr[2]), readl((const void *)addr[3]));
   /*
        i++;   
       
       if(i == 2)
       {
           printk("R%08x: %08x %08x %08x %08x\n", 0x000000FF, 0, 0, 0, 0); 
           printk("R%08x: %08x %08x %08x %08x\n", 0x000000FF, 0, 0, 0, 0); 
           i=0;
       }  
       */
       addr += 4;
       dw_cnt -= 4;
   }

   return 0;
}
                       
static DEFINE_UART_CMD(dump, "dump",
                      "dump dump dump",
                      "dump mem", 2, 2, dump_main);

#endif
/*! @} */
