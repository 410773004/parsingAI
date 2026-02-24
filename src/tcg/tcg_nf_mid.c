/*
//-----------------------------------------------------------------------------
//       Copyright(c) 2019-2020 Solid State Storage Technology Corporation.
//                         All Rights reserved.
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from SSSTC.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from SSSTC.
//-----------------------------------------------------------------------------
*/
#if (_TCG_)//def TCG_NAND_BACKUP // Jack Li

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------

#include "sect.h"
#include "cpu_msg.h"
#include "event.h"
#include "ddr.h"
#include "dtag.h"
#include "epm.h"
#include "ncl.h"
#include "idx_meta.h"
#include "eccu.h"
#include "ncl_cmd.h"
#include "ipc_api.h"
#include "bf_mgr.h"
#include "console.h"
#include "evlog.h"

#include "tcg_nf_mid.h"
#include "tcgcommon.h"
#include "tcgtbl.h"
#include "tcg.h"

//#undef __FILEID__
#define __FILEID__ tcgmid
#include "trace.h"
#include "ncl_err.h"


//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Imported variables/function
//-----------------------------------------------------------------------------
//extern epm_info_t* shr_epm_info;
extern struct nand_info_t shr_nand_info;
extern bool bTcgTblErr;
extern void* tcgTmpBuf;
extern void *tcgTmpBuf_gc;
extern volatile u8 host_sec_bitz;
extern epm_info_t *shr_epm_info;

#ifdef TCG_NAND_BACKUP
extern u8 isTcgNfBusy;
#endif
//-----------------------------------------------------------------------------
//  Declared variables/function
//-----------------------------------------------------------------------------
fast_data u8 evt_tcg_gc_trigger = 0xFF;
//fast_data_zi struct ncl_cmd_t tcg_ncl_cmd;

// for BTN write
fast_data_zi tcg_io_chk_func_t tcg_io_chk_range;

#ifdef TCG_NAND_BACKUP     // Jack Li

extern tcg_mid_info_t* tcg_mid_info;

fast_data_zi pda_t tcg_next_paa;

fast_data_ni tcg_io_res_t tcg_io_cmd;
fast_data_ni u32 TcgMetaIdx;
fast_data_ni void *TcgMeta;

fast_data_ni u32 TcgMetaIdx_gc;
fast_data_ni void *TcgMeta_gc;

//-----------------------------------------------------------------------------
//  Codes
//-----------------------------------------------------------------------------
ddr_code pda_t tcg_blksn2pda(u32 blk_ofst)
{
	u32 pl_id, ch_id, ce_id, lun_id, blk_id;
	pda_t pda = 0;
	u32 blk_id_ptk = blk_ofst;
	
	pl_id  = blk_ofst % shr_nand_info.geo.nr_planes;
			
	blk_ofst /= shr_nand_info.geo.nr_planes;
	ch_id = blk_ofst % shr_nand_info.geo.nr_channels;
			
	blk_ofst /= shr_nand_info.geo.nr_channels;
	ce_id  = blk_ofst % shr_nand_info.geo.nr_targets;
			
	blk_ofst /= shr_nand_info.geo.nr_targets;
	lun_id  = blk_ofst % shr_nand_info.geo.nr_luns;
			
	blk_ofst /= shr_nand_info.geo.nr_luns;
	blk_id = blk_ofst % TCG_SPB_ALLOCED + TCG_SPB_ID_START;

	tcg_mid_trace(LOG_ERR, 0x1678, "[TCG] blk ofst:%d is located @ BLK:%d, LUN:%d, CE:%d, CH:%d, PL:%d", blk_id_ptk, blk_id, lun_id, ce_id, ch_id, pl_id);

	pda |= (pl_id  << shr_nand_info.pda_plane_shift);
	pda |= (ch_id  << shr_nand_info.pda_ch_shift);
	pda |= (ce_id  << shr_nand_info.pda_ce_shift);
	pda |= (lun_id << shr_nand_info.pda_lun_shift);
	pda |= (blk_id << shr_nand_info.pda_block_shift);

	return pda;
}

ddr_code u32 tcg_pda2blksn(pda_t pda)
{
	u32 amount, blk_id = 0;

	blk_id += ((pda >> shr_nand_info.pda_plane_shift) & (shr_nand_info.geo.nr_planes-1));
	
	amount = shr_nand_info.geo.nr_planes;
	blk_id += (((pda >> shr_nand_info.pda_ch_shift) & (shr_nand_info.geo.nr_channels-1)) * amount);
	
	amount *= shr_nand_info.geo.nr_channels;
	blk_id += (((pda >> shr_nand_info.pda_ce_shift) & (shr_nand_info.geo.nr_targets-1)) * amount);

	amount *= shr_nand_info.geo.nr_targets;
	blk_id += (((pda >> shr_nand_info.pda_lun_shift) & (shr_nand_info.geo.nr_luns-1)) * amount);

	amount *= shr_nand_info.geo.nr_luns;
	blk_id += (((pda >> shr_nand_info.pda_block_shift) - TCG_SPB_ID_START) * amount);

	return blk_id;
}

// OFST
// BITS 0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0
ddr_code u16 get_PDA_list(pda_t *pda_list, u16 page_cnt)
{
	u16 i;
	u32 pages_flush, blk_ofst;
	//u32 cur_blk_used;
	u32 max_blk_alloced = TCG_SPB_ALLOCED*shr_nand_info.interleave;
	//epm_tcg_info_t *tcg_info = (epm_tcg_info_t *)ddtag2mem(shr_epm_info->epm_tcg_info.ddtag);

	if(page_cnt==0)
		return 0;
	
	if(tcg_next_paa == INV_PDA)
	{
		//cur_blk_used = blk_ofst = find_next_bit(tcg_info->blk_erased, max_blk_alloced, 0);
		blk_ofst = find_next_bit(tcg_mid_info->blk_erased, max_blk_alloced, 0);
		if (blk_ofst >= max_blk_alloced)
		{
			//   all 0'b   //
			tcg_mid_trace(LOG_ERR, 0x9533, "[TCG] Blk ID: %d (0-based), Max Blk: %d; TCG block FULL !!", blk_ofst, max_blk_alloced);
			//sys_assert(blk_ofst == max_blk_alloced);
		}
		else
		{
			//tcg_mid_trace(LOG_INFO, 0, "[TCG] Blk ofst: %d (0-based) to be used", blk_ofst);
			u32 erased_blk_cnt = 0;
			for(u32 j=0; j<(sizeof(tcg_mid_info->blk_erased)/sizeof(u32)); j++)
			{
				tcg_mid_trace(LOG_DEBUG, 0x8fc8, "[TCG] Mid Blk erased map: 0x%x", tcg_mid_info->blk_erased[j]);
				erased_blk_cnt += pop32(tcg_mid_info->blk_erased[j]);
			}
			if(erased_blk_cnt > 1)
			{
				clear_bit(blk_ofst, tcg_mid_info->blk_erased);
				tcg_mid_info->vac[blk_ofst] = 0;
				tcg_next_paa = tcg_blksn2pda(blk_ofst);

				tcg_mid_trace(LOG_INFO, 0x4bd5, "[TCG] Blk ofst: %d (0-based) used | Next PDA: 0x%x", blk_ofst, tcg_next_paa);
			}
			else
				tcg_mid_trace(LOG_INFO, 0x1da8, "[TCG] Blk rem: %d, GC one blk first", erased_blk_cnt);
		}
		return 0;
	}
	
	// still using the same blk, but need to know which one to update vac table
	tcg_mid_trace(LOG_DEBUG, 0xaade, "[TCG] Start PDA allocating for TCG data");
	
	pages_flush = (shr_nand_info.geo.nr_pages/3) - ((tcg_next_paa >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask);
	pages_flush = min(pages_flush, page_cnt);
	for(i = 0; i < pages_flush; i++)
	{
		//tcg_next_paa += (1 << shr_nand_info.pda_page_shift);
				
		pda_list[i]   = tcg_next_paa;
		tcg_next_paa += (1 << shr_nand_info.pda_page_shift);
		tcg_mid_trace(LOG_DEBUG, 0xc9c5, "[TCG] PDA List[%d]: 0x%x", i, pda_list[i]);
	}

	// remained pages is less than flush page cnt
	// Or last page was token in this time
	// mask=0x7FF (2047) > actual page cnt (448)
	if((page_cnt > pages_flush) || (((tcg_next_paa >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask) >= (shr_nand_info.geo.nr_pages/3)))
		tcg_next_paa = INV_PDA;
	
	return pages_flush;
}


//#ifdef NEED_CODING_BUT_NOT_YET
extern volatile u8 eccu_during_change;
ddr_code u32 tcg_ncl_cmd(pda_t *pda_list, enum ncl_cmd_op_t op, bm_pl_t *bm_pl, u32 count, int du_format)
{
	//tcg_mid_trace(LOG_ERR, 0, "tcg_ncl_cmd start");
	
	int i;
	tcg_io_cmd.ncl_cmd.addr_param.common_param.info_list = tcg_io_cmd.info_list;
	tcg_io_cmd.ncl_cmd.addr_param.common_param.list_len = count;
	tcg_io_cmd.ncl_cmd.addr_param.common_param.pda_list = pda_list;

	memset(tcg_io_cmd.info_list, 0, sizeof(struct info_param_t) * count);

	for (i = 0; i < count; i++)
		tcg_io_cmd.info_list[i].pb_type = NAL_PB_TYPE_SLC;

	tcg_io_cmd.ncl_cmd.caller_priv = NULL;
	tcg_io_cmd.ncl_cmd.completion = NULL;
	tcg_io_cmd.ncl_cmd.flags = NCL_CMD_SYNC_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;
	tcg_io_cmd.ncl_cmd.op_code = op;

    if(op == NCL_CMD_OP_READ){
        tcg_io_cmd.ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
        #if RAID_SUPPORT_UECC
	    tcg_io_cmd.ncl_cmd.uecc_type = NCL_UECC_NORMAL_RD;
        #endif
    }else{
        tcg_io_cmd.ncl_cmd.op_type = INT_TABLE_WRITE_PRE_ASSIGN;
    }

	tcg_io_cmd.ncl_cmd.user_tag_list = bm_pl;
	tcg_io_cmd.ncl_cmd.du_format_no = DU_FMT_USER_4K;

	tcg_io_cmd.ncl_cmd.status = 0;

	u8 dump_flag;
    dump_flag = 0;
	while(eccu_during_change == true)
	{
			if(dump_flag == 0)
			{
					tcg_mid_trace(LOG_ERR, 0xe73f, "tcg in while");
					dump_flag |= BIT0;
			}

			if(ncl_busy[NRM_SQ_IDX] == true)
			{
					dump_flag |= BIT1;
					ficu_done_wait();
			}
	};

	ncl_cmd_submit(&tcg_io_cmd.ncl_cmd);
	if (op == NCL_CMD_OP_READ)
	{
		i = 0;
		nal_status_t ret;
		ret = ficu_err_good;
		if (tcg_io_cmd.ncl_cmd.status != 0)
		{
			do
			{
				if (tcg_io_cmd.info_list[i].status > ret)
				{
					ret = tcg_io_cmd.info_list[i].status;
				}
			} while (++i < count);
		}
		
		//tcg_mid_trace(LOG_ERR, 0, "[TCG] NCL Read result: %d", ret);
		
		return ret;
	}
	
	//tcg_mid_trace(LOG_ERR, 0, "[TCG] NCL Write/Erase result: %d", tcg_io_cmd.ncl_cmd.status);
	
	return tcg_io_cmd.ncl_cmd.status;
}
//#endif

ddr_code void force_tcg_vac_tbl(void)
{
	u32 max_blk_alloced = TCG_SPB_ALLOCED*shr_nand_info.interleave;
	for(u32 blk = 0; blk < max_blk_alloced; blk++)
	{
		// defect 0 means blk good
		//if(((tcg_mid_info->defect_map[blk/32] & (1 << (blk%32))) == 0) && ((tcg_mid_info->blk_erased[blk/32] & (1 << (blk%32))) == 0))
		if(((tcg_mid_info->defect_map[blk / 32] | tcg_mid_info->blk_erased[blk / 32]) & 1 << (blk % 32)) == 0)
			tcg_mid_info->vac[blk] = 0;
		else
			tcg_mid_info->vac[blk] = 0xFFFF;
	}
	
	for(u32 laa = 0; laa < TCG_TOTAL_UESD_LAA; laa++)
	{
		pda_t pda_list = tcg_mid_info->l2p_tbl[laa];
		if(pda_list != UNMAP_PDA)
		{
			u32 blk_id = tcg_pda2blksn(pda_list);
		
			tcg_mid_info->vac[blk_id] += 1;

			tcg_mid_trace(LOG_ERR, 0x54f2, "[TCG] L2P update vac LAA: 0x%x", laa);
		}
	}
}

ddr_code void clear_mbr_ds_tbl(void)
{
	for(u32 i=TCG_SMBR_LAA_START; i<=TCG_SMBR_LAA_END; i++)
	{
		// G4
		if(tcg_mid_info->l2p_tbl[i] != UNMAP_PDA)
		{
			pda_t pda_list = tcg_mid_info->l2p_tbl[i];
			u32 blk_id = tcg_pda2blksn(pda_list);
			tcg_mid_info->vac[blk_id] -= 1;
			tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
		}
		// G5
		if(tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] != UNMAP_PDA)
		{
			pda_t pda_list = tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA];
			u32 blk_id = tcg_pda2blksn(pda_list);
			tcg_mid_info->vac[blk_id] -= 1;
			tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
		}
	}
	for(u32 j=TCG_DS_LAA_START; j<=TCG_DS_LAA_END; j++)
	{
		// G4
		if(tcg_mid_info->l2p_tbl[j] != UNMAP_PDA)
		{
			pda_t pda_list = tcg_mid_info->l2p_tbl[j];
			u32 blk_id = tcg_pda2blksn(pda_list);
			tcg_mid_info->vac[blk_id] -= 1;
			tcg_mid_info->l2p_tbl[j] = UNMAP_PDA;
		}
		// G5
		if(tcg_mid_info->l2p_tbl[j + TCG_LAST_USE_LAA] != UNMAP_PDA)
		{
			pda_t pda_list = tcg_mid_info->l2p_tbl[j + TCG_LAST_USE_LAA];
			u32 blk_id = tcg_pda2blksn(pda_list);
			tcg_mid_info->vac[blk_id] -= 1;
			tcg_mid_info->l2p_tbl[j + TCG_LAST_USE_LAA] = UNMAP_PDA;
		}
	}
}

ddr_code void tcg_nf_write(tcg_nf_params_t *param, bool from_gc)
{
	u16 i, j, erased_blk_cnt = 0;
	u16 laa_ofst = 0;
	u16 required = param->laacnt;
	u16 xferred = 0;
	u32 old_paa, blk_id = 0;
	u32 dtag_buffer;// = mem2ddtag(tcgTmpBuf);
	u32 dtag_l2p;
	u32 sts;
	u32 meta_idx;
	tcg_du_meta_fmt_t *meta;// = (tcg_du_meta_fmt_t *)TcgMeta;

	//pda_t *pda_list = (pda_t *)sys_malloc(SLOW_DATA, (max(param->laacnt, 5)*4*sizeof(pda_t)));
	pda_t pda_list[max(param->laacnt, 5)];
	for(i=0; i<(max(param->laacnt, 5)); i++)
	{
		pda_list[i] = INV_PDA;
	}

	if(from_gc)
	{
		dtag_buffer = mem2ddtag(tcgTmpBuf_gc);
		meta = (tcg_du_meta_fmt_t *)TcgMeta_gc;
		meta_idx = TcgMetaIdx_gc;
	}
	else
	{
		dtag_buffer = mem2ddtag(tcgTmpBuf);
		meta = (tcg_du_meta_fmt_t *)TcgMeta;
		meta_idx = TcgMetaIdx;
	}

	tcg_mid_trace(LOG_DEBUG, 0x6a1b, "[TCG]Write param->laas : 0x%x", param->laas);

	if(param->updt_l2p_wr)
	{
		tcg_mid_trace(LOG_INFO, 0x4ce8, "[TCG] Update L2P force when Writing");
		tcg_next_paa = INV_PDA;

		// update VAC table
		//force_tcg_vac_tbl();
		clear_mbr_ds_tbl();
		
		param->updt_l2p_wr = false;
	}
	while(required)
	{
loop_start:
		// Get numbers of PAA needed -> How many Pages for Write/Read 
		xferred = get_PDA_list(pda_list, required);
		if(xferred == 0)
		{
			//tcg_mid_trace(LOG_ERR, 0, "[TCG] PDA page mask: 0x%x", shr_nand_info.pda_page_mask);
			if(((tcg_next_paa >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask)==0)
			{
				tcg_mid_trace(LOG_ERR, 0xb1c2, "[TCG] Flush L2P at first 5 pages");
				// flush L2P table in first
				get_PDA_list(pda_list, 5);
				dtag_l2p = mem2ddtag(tcg_mid_info);
				tcg_mid_info->history_no++;
				for(i = 0; i < 5; i++)
				{
					meta->tcg_l2p.tag = TCG_L2P_TAG;
					meta->tcg_l2p.history_no = tcg_mid_info->history_no;
					for(j = 0; j < DU_CNT_PER_PAGE; j++)
					{
						tcg_io_cmd.bm_pl_list[j].all = 0;
						tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_l2p+(i*DU_CNT_PER_PAGE)+j));
						tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
						tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
						tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = meta_idx;
					}
					if(tcg_ncl_cmd(pda_list+i, NCL_CMD_OP_WRITE, tcg_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE))
					{
						u32 blk_id = tcg_pda2blksn(pda_list[i]);
						//tcg_mid_info->defect_map[blk_id/32] |= (1 << (blk_id % 32));
						tcg_next_paa = INV_PDA;   // -> to find next accessable block

						tcg_mid_trace(LOG_ERR, 0xe8e0, "[TCG] write L2P fail, block ID: %d", blk_id);
				
						break;
					}
				}
				if(tcg_next_paa != INV_PDA)
				{
					// update EPM
					epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
					epm_aes_data->new_blk_prog = tcg_pda2blksn(pda_list[0]);
					epm_update(AES_sign, (CPU_ID - 1));
				}
				continue;
			}
			else
			{
				tcg_mid_trace(LOG_WARNING, 0xe8e7, "[TCG] GC action immediately");

				isTcgNfBusy = true;
				evt_set_cs(evt_tcg_gc_trigger, 0, 0, CS_NOW);
				continue; // find a new accessable block to program after GC done
			}
		}

		// write to the same block of NAND, remained pages are written to another block in the next loop
		//tcg_ncl_cmd(pda_t * pda_list, enum ncl_cmd_op_t op, bm_pl_t * bm_pl, u32 count, int du_format, int stripe_id);
		for(i = 0; i < xferred; i++)
		{
			//meta->tcg_data.tag = TCG_TAG;
			//meta->tcg_data.laa = (param->laas + laa_ofst + i);
			for(j = 0; j < DU_CNT_PER_PAGE; j++)
			{
				meta[j].tcg_data.tag = TCG_TAG;
				meta[j].tcg_data.laa = (param->laas + laa_ofst + i);
				if(meta[j].tcg_data.laa < 0x2000)
				{
					u64 lba_ref = (u64)(meta[j].tcg_data.laa);
					lba_ref = (lba_ref << (DTAG_SHF + DU_CNT_PER_PAGE_SHIFT - host_sec_bitz));
					lba_ref += (j*(1 << (DTAG_SHF - host_sec_bitz)));
					meta[j].tcg_data.hlba_h = (u32)(lba_ref >> 32);
					meta[j].tcg_data.hlba_l = (u32)lba_ref;
				}
				tcg_io_cmd.bm_pl_list[j].all = 0;
				tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_buffer+j));
				tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
				tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
				tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = meta_idx+j;
			}
			tcg_mid_trace(LOG_DEBUG, 0x737c, "[TCG] write page: %d !!", ((pda_list[i] >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask));
			sts = tcg_ncl_cmd(pda_list+i, NCL_CMD_OP_WRITE, tcg_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE);
			if(sts)
			{
				tcg_mid_trace(LOG_ERR, 0x3914, "[TCG] write data fail @ block ID: %d !!", tcg_pda2blksn(pda_list[i]));
					
				tcg_next_paa = INV_PDA;   // -> to find next accessable block
				goto loop_start;
			}
			else
				dtag_buffer += DU_CNT_PER_PAGE;
			
			// update TCG L2P table
			// search L2P
			old_paa = tcg_mid_info->l2p_tbl[param->laas+laa_ofst+i];
			if(old_paa!=UNMAP_PDA)
			{
				blk_id = tcg_pda2blksn(old_paa);
			
				// Subtract VAC with blkID of old PAA
				tcg_mid_info->vac[blk_id]--;
			}
			// update L2P
			tcg_mid_info->l2p_tbl[param->laas+laa_ofst+i] = pda_list[i];
			blk_id = tcg_pda2blksn(pda_list[i]);
		}
		laa_ofst += i;
		
		// update VAC table
		tcg_mid_info->vac[blk_id] += xferred;

		required -= xferred;
	}

	// Move data when empty block less than TCG_GC_THRESHOLD
	erased_blk_cnt = 0;
	for(i=0; i<(sizeof(tcg_mid_info->blk_erased)/sizeof(u32)); i++)
	{
		erased_blk_cnt += pop32(tcg_mid_info->blk_erased[i]);
	}
	if(erased_blk_cnt <= TCG_GC_THRESHOLD)
	{
		if(isTcgNfBusy)
			tcg_mid_trace(LOG_WARNING, 0xa19e, "[TCG] still continue GC, rem blk cnt: %d", erased_blk_cnt);
		else
		{
			isTcgNfBusy = true;
			evt_set_cs(evt_tcg_gc_trigger, TCG_GC_DONE_THRESHOLD, 0, CS_TASK);    // Not verify yet ...
			tcg_mid_trace(LOG_WARNING, 0x4341, "[TCG] remained blk cnt: %d", erased_blk_cnt);
		}
	}

	if(param->updt_l2p_wr)
		param->updt_l2p_wr = false;
	
	//sys_free(SLOW_DATA, pda_list);
}

ddr_code u32 tcg_nf_read(tcg_nf_params_t *param)
{
	if(bTcgTblErr)
		return true;
	
	u32 i, j, sts;
	u32 dtag_buffer = mem2ddtag(tcgTmpBuf);
	//pda_t *pda_list = (pda_t *)sys_malloc(SLOW_DATA, (4 * sizeof(pda_t)));
	pda_t pda_list[DU_CNT_PER_PAGE];

	//memset(tcgTmpBuf, 0, (DTAG_SZE << DU_CNT_PER_PAGE_SHIFT)*param->laacnt);

	tcg_mid_trace(LOG_DEBUG, 0x5378, "[TCG]Read param->laas : 0x%x",param->laas);
	
	for(i=0; i<param->laacnt; i++)
	{
		if(tcg_mid_info->l2p_tbl[param->laas + i] == UNMAP_PDA)
		{
			tcg_mid_trace(LOG_ERR, 0xfbd8, "[TCG] L2P not search, read fail");
			// maybe use memset to all 0'b/1'b ?
			memset((tcgTmpBuf + (i*(DTAG_SZE << DU_CNT_PER_PAGE_SHIFT))), 0, (DTAG_SZE << DU_CNT_PER_PAGE_SHIFT));
			
			continue;
		}
		pda_list[0] = tcg_mid_info->l2p_tbl[param->laas + i];
		
		// read from NAND in single page units
		for(j = 0; j < DU_CNT_PER_PAGE; j++)
		{
			tcg_io_cmd.bm_pl_list[j].all = 0;
			tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_buffer+(i*DU_CNT_PER_PAGE)+j));
			tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
			tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
			tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = TcgMetaIdx;

			pda_list[j] = pda_list[0] + j;
/*
			pda_list[j] = pda_list[0]+j;
			if(tcg_ncl_cmd(&pda_list[j], NCL_CMD_OP_READ, &tcg_io_cmd.bm_pl_list[j], 1, DU_4K_DEFAULT_MODE))
			{
				u32 blk_id = tcg_pda2blksn(pda_list[j]);
				tcg_mid_info->defect_map[blk_id/32] |= (1 << (blk_id % 32));
				bTcgTblErr  = true;

				tcg_mid_trace(LOG_ERR, 0, "[TCG] read fail, set block ID: %d defect !!", blk_id);
				
				goto TBL_ERR;
			}
*/
		}
		sts = tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, tcg_io_cmd.bm_pl_list, 4, DU_4K_DEFAULT_MODE);
		if(sts)
		{
			u32 blk_id = tcg_pda2blksn(pda_list[0]);
			bTcgTblErr  = true;

			tcg_mid_trace(LOG_ERR, 0xdf96, "[TCG] read data fail @ block ID: %d !!", blk_id);

			return sts;
		}
	}
	
//TBL_ERR:
	//sys_free(SLOW_DATA, pda_list);

	return false;
}

ddr_code u32 tcg_nf_erase(u32 blk_id)
{
	u32 sts;
	//pda_t *pda_list = (pda_t *)sys_malloc(SLOW_DATA, sizeof(pda_t));
	pda_t pda_list;
	
	pda_list = tcg_blksn2pda(blk_id);
	sts = tcg_ncl_cmd(&pda_list, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE);

	if(sts)
		tcg_mid_trace(LOG_ERR, 0x50c7, "[TCG] Erase blk %d after GC, result: %d", blk_id, sts);
	
	//sys_free(SLOW_DATA, pda_list);

	return sts;
}

ddr_code void tcg_nf_eraseAll(tcg_nf_params_t *param)
{
	u32 i, sts;
	//pda_t *pda_list = (pda_t *)sys_malloc(SLOW_DATA, sizeof(pda_t));
	pda_t pda_list;
	
	tcg_next_paa = INV_PDA;

	for(i=0; i<TCG_ALL_LAA_CNT; i++)
	{
		tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
	}
	tcg_mid_info->history_no = 0;
	memset(tcg_mid_info->vac,        0xFF, sizeof(tcg_mid_info->vac));
	memset(tcg_mid_info->blk_erased, 0x00, sizeof(tcg_mid_info->blk_erased));

	for(i=0; i<(TCG_SPB_ALLOCED*shr_nand_info.interleave); i++)
	{
		// defect 0 means blk good
		if((tcg_mid_info->defect_map[i/32] & (1<<(i%32))) == 0)
		{
			pda_list = tcg_blksn2pda(i);
			//tcg_mid_trace(LOG_ERR, 0, "[TCG] Erase %dth blk, PDA: 0x%x", i, pda_list);
			
			sts = tcg_ncl_cmd(&pda_list, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE);
			//tcg_mid_trace(LOG_ERR, 0, "[TCG] Result of erasing %dth blk: %d", i, sts);
			
			if(sts==0)
				set_bit(i, (void *)tcg_mid_info->blk_erased);
			else
				tcg_mid_info->defect_map[i/32] |= (1<<(i%32));
		}
	}
/*
	for(i=0; i<(sizeof(tcg_mid_info->blk_erased)/sizeof(u32)); i++)
	{
		tcg_mid_trace(LOG_ERR, 0, "[TCG] Block erased map: 0x%x, defect map: 0x%x", tcg_mid_info->blk_erased[i], tcg_mid_info->defect_map[i]);
	}
*/
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	if((bTcgTblErr) && ((epm_aes_data->tcg_err_flag & BIT0) == 0) && (epm_aes_data->prefmtted == TCG_INIT_TAG))
	{
		if(epm_aes_data->tcg_en_dis_tag == TCG_TAG)
			epm_aes_data->tcg_err_flag &= BIT0;
		else
			epm_aes_data->tcg_err_flag = 0;
		epm_update(AES_sign, (CPU_ID - 1));

		flush_to_nand(EVT_TCG_TBL_ERR);
	}
	//sys_free(SLOW_DATA, pda_list);
	
}

ddr_code u8 tcg_nf_commit(tcg_nf_params_t *param)
{
	if(bTcgTblErr)
		return true;
	
	u16 i;
	u16 laas = param->laas;
	u16 laacnt = param->laacnt;
	u16 laa_rd = laas;
	u16 laa_wr = laas;
	u32 sts = 0;
	u32 trnsctn = param->trnsctn_sts;
	
	if(param->op == 0xFF)
		laa_rd += TCG_LAST_USE_LAA;
	else
		laa_wr += TCG_LAST_USE_LAA;

	for(i = 0; i < laacnt; i++)
	{
		//if(tcg_mid_info->l2p_tbl[i + laa_rd] != UNMAP_PDA)
		if(((tcg_mid_info->trsac_bitmap[(laas+i)/32] & (1 << ((laas+i)%32))) && (trnsctn == TRNSCTN_ACTIVE))
			|| (trnsctn != TRNSCTN_ACTIVE))
		{
#ifdef TCG_CMT_L2P_UPDT
			tcg_mid_info->l2p_tbl[i + laa_wr] = tcg_mid_info->l2p_tbl[i + laa_rd];
#else
			param->result = 0;
			param->op     = NCL_CMD_OP_READ;
			param->laas   = i + laa_rd;
			param->laacnt = 1;
			
			tcg_mid_trace(LOG_INFO, 0xfdc2, "[TCG]Read Laas : 0x%x", param->laas);

			sts = tcg_nf_read(param);
			if(sts)
			{
				tcg_mid_trace(LOG_ERR, 0x07ab, "[TCG] read fail when commit/abort !!");

				bTcgTblErr  = true;
				
				return sts;
			}
			else
			{
				param->result = 0;
				param->op     = NCL_CMD_OP_WRITE;
				param->laas   = i + laa_wr;
				param->laacnt = 1;

				tcg_mid_trace(LOG_DEBUG, 0x6c9e, "[TCG]Write Laas : 0x%x", param->laas);
				
				tcg_nf_write(param, false);
			}
#endif
		}
	}

#ifdef TCG_CMT_L2P_UPDT
	param->result = 0;
	param->op     = NCL_CMD_OP_WRITE;
	param->laas   = TCG_DUMMY_LAA;
	param->laacnt = 1;
	tcg_next_paa = INV_PDA;

	tcg_mid_trace(LOG_INFO, 0x754c, "[TCG] Write: 0x%x for commit", param->laas);
	tcg_nf_write(param, false);
#endif

	return sts;
}

ddr_code void tcg_nf_Start(tcg_nf_params_t *param)
{
	//tcg_mid_trace(LOG_WARNING, 0, "[TCG] IPC sent, NCL op: %d", param->op);

	u32 result = 0;
	
	if(param->op == NCL_CMD_OP_READ)
		result = tcg_nf_read(param);
	else if(param->op == NCL_CMD_OP_WRITE)
		tcg_nf_write(param, false);
	else if(param->op == NCL_CMD_OP_ERASE)
		tcg_nf_eraseAll(param);
	else
		result = tcg_nf_commit(param);

	param->result =  result;
}

ddr_code void ipc_tcg_nf_op(volatile cpu_msg_req_t *req)
{
	tcg_nf_params_t *nf_params = (tcg_nf_params_t *) req->pl;

	tcg_nf_Start(nf_params);

	if(nf_params->sync)
		cpu_msg_sync_done(req->cmd.tx);
}

ddr_code void ipc_tcg_change_chkfunc(volatile cpu_msg_req_t *req)
{
	u32 sts = req->pl;
	tcg_mid_trace(LOG_ERR, 0x43f9, "[TCG] change IO check func for BTN write, status: %d", sts);
	
	if(sts & MBR_SHADOW_MODE)
		tcg_io_chk_range = TcgRangeCheck_SMBR_cpu4;
	else
		tcg_io_chk_range = TcgRangeCheck_cpu4;

	//cpu_msg_sync_done(req->cmd.tx);
}


fast_code void tcg_gc_handle(u32 param, u32 payload, u32 sts)
{
	tcg_mid_trace(LOG_WARNING, 0xe71c, "[TCG] GC trigger");
	//sys_assert(isTcgNfBusy);
	
	// Jack Li
	//return; // Nothing to do

	u32 i, j, k;
	u32 erased_blk_cnt = 0;
	u32 blk_id = 0;	
	u32 cur_blk_used = 0xFFFFFFFF;
	u16 min_vac = 0xFFFF;
	
	//u32 dtag_buffer = mem2ddtag(tcgTmpBuf);
	u32 dtag_buffer = mem2ddtag(tcgTmpBuf_gc);
	tcg_du_meta_fmt_t *meta = (tcg_du_meta_fmt_t *)TcgMeta_gc;

	//pda_t *pda_list = sys_malloc(SLOW_DATA, 4*sizeof(pda_t));
	pda_t pda_list[4];
	//tcg_nf_params_t *nf_params = sys_malloc(SLOW_DATA, sizeof(tcg_nf_params_t));
	tcg_nf_params_t nf_params;

	bool one_blk_only = (payload == 0) ? true:false;

	while(erased_blk_cnt < TCG_GC_DONE_THRESHOLD)
	{
		tcg_mid_trace(LOG_INFO, 0x9c97, "[TCG] GC loop, blk cnt: %d", erased_blk_cnt);
		cpu_msg_isr();
		// find a block with min vac
		if(tcg_next_paa!=INV_PDA)
			cur_blk_used = tcg_pda2blksn(tcg_next_paa);
		else
			cur_blk_used = 0xFFFFFFFF;
		for(i=0; i<(TCG_SPB_ALLOCED*nand_info.interleave); i++)
		{
			//if((tcg_mid_info->vac[i] < min_vac) && (tcg_mid_info->vac[i] != 0) && (i != cur_blk_used))
			if((tcg_mid_info->vac[i] < min_vac) && (i != cur_blk_used))
			{
				blk_id = i;
				min_vac = tcg_mid_info->vac[i];
			}
		}

		// read -> check L2P -> write page by page
		j = 0;
		tcg_mid_trace(LOG_DEBUG, 0x8b79, "[TCG] Move Blk: %d, VAC: %d", blk_id, tcg_mid_info->vac[blk_id]);
		while(tcg_mid_info->vac[blk_id] > 0)
		{
			//tcg_mid_trace(LOG_WARNING, 0, "[TCG] Move PG: %d", j);
			
			// read a page
			pda_list[0] = tcg_blksn2pda(blk_id) + (j << nand_info.pda_page_shift);
			for(k=0; k<DU_CNT_PER_PAGE; k++)
			{
				pda_list[k] = pda_list[0] + k;
				tcg_io_cmd.bm_pl_list[k].all = 0;
				tcg_io_cmd.bm_pl_list[k].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_buffer+k));
				tcg_io_cmd.bm_pl_list[k].pl.du_ofst = k;
				tcg_io_cmd.bm_pl_list[k].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
				tcg_io_cmd.bm_pl_list[k].pl.nvm_cmd_id = TcgMetaIdx_gc;
			}
			if(tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, tcg_io_cmd.bm_pl_list, 4, DU_4K_DEFAULT_MODE)==0)
			{
				// Compare L2P
				if((meta->tcg_data.tag == TCG_TAG) && (tcg_mid_info->l2p_tbl[meta->tcg_data.laa] == pda_list[0]))
				{
					// write a page
					nf_params.laas = meta->tcg_data.laa;
					nf_params.laacnt = 1;
					tcg_nf_write(&nf_params, true);
				}
				//tcg_mid_trace(LOG_ERR, 0, "[TCG] GC read cmp PAA@L2P: 0x%x, scan PAA: 0x%x", tcg_mid_info->l2p_tbl[meta->tcg_data.laa], pda_list[0]);
			}
			else
			{
				tcg_mid_trace(LOG_ERR, 0x5f76, "[TCG] Read Blk: %d (PAA 0x%x) FAIL !!", blk_id, pda_list[0]);
			}

			// increasing page number in the block with min VAC
			j++;
			if(j >= (shr_nand_info.geo.nr_pages/3))
				break;
		}
		
		//sys_assert(tcg_mid_info->vac[blk_id] == 0);
		
		if(((tcg_mid_info->defect_map[blk_id / 32] | tcg_mid_info->blk_erased[blk_id / 32]) & 1 << (blk_id % 32)) == 0)
		{
			if(tcg_nf_erase(blk_id))
				tcg_mid_info->defect_map[blk_id / 32] |= (1 << (blk_id % 32));
			else
				tcg_mid_info->blk_erased[blk_id / 32] |= (1 << (blk_id % 32));

			// init VAC
			tcg_mid_info->vac[blk_id] = 0xFFFF;
		}
		min_vac = 0xFFFF;
		
		tcg_mid_trace(LOG_DEBUG, 0xda88, "[TCG] Blk: %d GC done, vac: %d", blk_id, tcg_mid_info->vac[blk_id]);
		sys_assert(tcg_mid_info->vac[blk_id] == 0xFFFF);

		// update erased/accessable blk cnt
		erased_blk_cnt = 0;
		for(i=0; i<(sizeof(tcg_mid_info->blk_erased)/sizeof(u32)); i++)
		{
			tcg_mid_trace(LOG_DEBUG, 0x4ff5, "[TCG] Mid Blk erased map: 0x%x", tcg_mid_info->blk_erased[i]);
			erased_blk_cnt += pop32(tcg_mid_info->blk_erased[i]);
		}
		tcg_mid_trace(LOG_DEBUG, 0xc34c, "[TCG] Programmable Blk cnt: %d", erased_blk_cnt);

		if(one_blk_only)
			return;
	}

	//sys_free(SLOW_DATA, pda_list);
	//sys_free(SLOW_DATA, nf_params);

	tcg_mid_trace(LOG_INFO, 0x90a6, "[TCG] GC loop done, clear flag");
	isTcgNfBusy = false;
}

#endif // end for ifdef TCG_NAND_BACKUP

// for BYN write cmd
share_data_zi u32 mTcgStatus;           //TCG status variable for others
share_data_zi u16 mReadLockedStatus;    // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
share_data_zi u16 mWriteLockedStatus;   // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.
share_data_zi enabledLockingTable_t* globla_pLockingRangeTable;


fast_code u8 TcgRangeCheck_cpu4(u64 lbaStart, u64 len, u16 locked_status)
{
#ifdef TCG_RNG_CHK_DEBUG
	tcg_mid_trace(LOG_INFO, 0xc248, "TcgRangeCheck_cpu4() - lbaStart:0x%x , len:%x , locked_status:%x", lbaStart, len, locked_status);

		for(u8 rng_cnt=0; rng_cnt<LOCKING_RANGE_CNT; rng_cnt++){

			tcg_mid_trace(LOG_INFO, 0x71ba, "pR[%d]: 0x%x ~ 0x%x", globla_pLockingRangeTable[rng_cnt].rangeNo, globla_pLockingRangeTable[rng_cnt].rangeStart, globla_pLockingRangeTable[rng_cnt].rangeEnd);

			if(globla_pLockingRangeTable[rng_cnt].rangeNo==0)
				break;
		}

		tcg_mid_trace(LOG_INFO, 0x27f2, "Tcg Sts: 0x%x, Act: 0x%x", mTcgStatus, mTcgActivated);
#endif
    //if(mTcgStatus & TCG_ACTIVATED) // TCG_ACTIVATED
    {
        u8  i;
	    u64 startLBA, endLBA;

		if(locked_status==0)
			return false;

	    if(len==0)
		    return false;
		
	    startLBA = lbaStart;
	    endLBA = lbaStart+len-1;

	    // Starting LBA is located at Global
	    if(startLBA < globla_pLockingRangeTable[0].rangeStart){
		    if(locked_status & 0x1)
			    return true;
		    startLBA = globla_pLockingRangeTable[0].rangeStart;
	    }

	    // for-loop //
	    for(i=0; i<LOCKING_RANGE_CNT; i++){

		    if(startLBA>endLBA)
			    return false;

		    if(globla_pLockingRangeTable[i].rangeNo==0)
			    break;

		    // check one locking range
		    if((startLBA >= globla_pLockingRangeTable[i].rangeStart) && (startLBA <= globla_pLockingRangeTable[i].rangeEnd)){
			    if(locked_status & ((0x1)<<(globla_pLockingRangeTable[i].rangeNo)) )
				    return true;
			    startLBA = globla_pLockingRangeTable[i].rangeEnd+1;

			    // Gap between 2 locking ranges
			    if(startLBA!=globla_pLockingRangeTable[i+1].rangeStart)
				    i--;
		    }
		    // check global range (Gap)
		    else if((startLBA < globla_pLockingRangeTable[i+1].rangeStart) && (startLBA > globla_pLockingRangeTable[i].rangeEnd)){
			    if(locked_status & 0x1)
				    return true;
			    else
				    startLBA = globla_pLockingRangeTable[i+1].rangeStart;
		    }
	    }

	    // Ending LBA is located at the last segment of Global range
	    if(locked_status & 0x1)
		    return true;
    }
	return false;
}

#ifdef TCG_NAND_BACKUP
fast_code u8 TcgRangeCheck_SMBR_cpu4(u64 lbaStart, u64 len, u16 locked_status)
{
#ifdef TCG_RNG_CHK_DEBUG
	tcg_mid_trace(LOG_INFO, 0x1174, "TcgRangeCheck_SMBR_cpu4() - lbaStart:0x%x , len:%x , locked_status:%x", lbaStart, len, locked_status);

		for(u8 rng_cnt=0; rng_cnt<LOCKING_RANGE_CNT; rng_cnt++){

			tcg_mid_trace(LOG_INFO, 0x157b, "pR[%d]: 0x%x ~ 0x%x", globla_pLockingRangeTable[rng_cnt].rangeNo, globla_pLockingRangeTable[rng_cnt].rangeStart, globla_pLockingRangeTable[rng_cnt].rangeEnd);

			if(globla_pLockingRangeTable[rng_cnt].rangeNo==0)
				break;
		}

		tcg_mid_trace(LOG_INFO, 0x6f73, "Tcg Sts: 0x%x, Act: 0x%x", mTcgStatus, mTcgActivated);
#endif
	if(len==0)
		return false;
	
	if(mTcgStatus & MBR_SHADOW_MODE) // MBR_SHADOW_MODE
    {
    	u64 smbr_size = (0x8000000 >> host_sec_bitz);
		if(lbaStart < smbr_size)
			return true;
	}
	
	return TcgRangeCheck_cpu4(lbaStart, len, locked_status);
}
#endif

init_code void tcg_nf_onetime_init(void)
{
	tcg_mid_trace(LOG_ERR, 0xea87, "[TCG] NF init, SPB: %d~%d", TCG_SPB_ID_START, TCG_SPB_ID_END);

#ifdef TCG_NAND_BACKUP
	//u8  lun_id, ce_id, ch_id, pl_id;
	//u32 blk_id;
	u16 i, j;
	//u32 hist_no = 0;
	bool l2p_err = false;
	u32 dtag_l2p, dtag_buf;
	u32 result;
	pda_t pda_list[4];
	
	tcg_du_meta_fmt_t *meta;
	pda_t newest_paa = INV_PDA;
	tcg_next_paa = INV_PDA;

	// alloc 4*5 ddtag to L2P table ()
	tcg_mid_info = (tcg_mid_info_t *)ddtag2mem(ddr_dtag_register(DU_CNT_PER_PAGE * 5));
	dtag_l2p = mem2ddtag(tcg_mid_info);

	// alloc DTAG for Dram buffer
	// alloc    4 ddtag to TCG GC buffer
	//        4*5 ddtag to tcgTmpBuf for TCG NCL R/W
	//          8 ddtag to dataBuf for TCG prepare packet
	tcgTmpBuf_gc = (void *)ddtag2mem(ddr_dtag_register(DU_CNT_PER_PAGE));
	tcgTmpBuf    = (void *)ddtag2mem(ddr_dtag_register((occupied_by(TCG_BE_BUF_SIZE, DTAG_SZE) + occupied_by(SECURE_COM_BUF_SZ, DTAG_SZE) + occupied_by(TCG_BUF_LEN, DTAG_SZE))));
	
	// Allocate meta
	TcgMeta    = idx_meta_allocate(4, DDR_IDX_META, &TcgMetaIdx);
	TcgMeta_gc = idx_meta_allocate(4, DDR_IDX_META, &TcgMetaIdx_gc);

	dtag_buf = mem2ddtag(tcgTmpBuf);
	meta = (tcg_du_meta_fmt_t *)TcgMeta;

#ifdef FW_UPDT_TCG_SWITCH
	goto ERR_TO_SKIP;
#endif

	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	if((epm_aes_data->prefmtted != TCG_INIT_TAG) || (epm_aes_data->tcg_err_flag & BIT0))
	{
		tcg_mid_trace(LOG_ERR, 0xc75a, "[TCG] Table Err, EPM TAG:0x%x, flag:%d", epm_aes_data->prefmtted, epm_aes_data->tcg_err_flag);

		bTcgTblErr = true;
		goto ERR_TO_SKIP;
	}

	tG1 *tmpBuf_tbl_G1 = (tG1 *)tcgTmpBuf;
	tG2 *tmpBuf_tbl_G2 = (tG2 *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE));
	tG3 *tmpBuf_tbl_G3 = (tG3 *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE));
	
	// Scan L2P table by checking meta (L2P TAG & History Number)
#if 0
	for(blk_id = TCG_SPB_ID_START; blk_id <= TCG_SPB_ID_END; blk_id++)
	{
		for(lun_id = 0; lun_id < shr_nand_info.geo.nr_channels; lun_id++)
		{
			for(ce_id = 0; ce_id < shr_nand_info.geo.nr_targets; ce_id++)
			{
				for(ch_id = 0; ch_id < shr_nand_info.geo.nr_luns; ch_id++)
				{
					for(pl_id = 0; pl_id < shr_nand_info.geo.nr_planes; pl_id++)
					{
						// read first page in a block
						pda_list[0] = 0;
						pda_list[0] |= (pl_id  << shr_nand_info.pda_plane_shift);
						pda_list[0] |= (ch_id  << shr_nand_info.pda_ch_shift);
						pda_list[0] |= (ce_id  << shr_nand_info.pda_ce_shift);
						pda_list[0] |= (lun_id << shr_nand_info.pda_lun_shift);
						pda_list[0] |= (blk_id << shr_nand_info.pda_block_shift);
						/*
						pda_list[1] = pda_list[0]+1;
						pda_list[2] = pda_list[0]+2;
						pda_list[3] = pda_list[0]+3;
						*/
						//tcg_mid_trace(LOG_ERR, 0, "[TCG] Scan L2P first PDA: 0x%x", pda_list[0]);
						for(j = 0; j < DU_CNT_PER_PAGE; j++)
						{
							tcg_io_cmd.bm_pl_list[j].all = 0;
							tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_buf+j));
							tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
							tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
							tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = TcgMetaIdx;

							//tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, &tcg_io_cmd.bm_pl_list[j], 1, DU_4K_DEFAULT_MODE);
							//tcg_mid_trace(LOG_ERR, 0, "[TCG] check for debug, need to be UNMAP: 0x%x", ((tcg_mid_info_t *)tcgTmpBuf)->l2p_tbl[j*1024]);
							//tcg_mid_trace(LOG_ERR, 0, "[TCG] check for debug, hist_no: %d", ((tcg_mid_info_t *)tcgTmpBuf)->history_no);

							//pda_list[0] += 1;
						}
						tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, tcg_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE);
						//tcg_mid_trace(LOG_ERR, 0, "[TCG] check for debug, need to be UNMAP[0]:  0x%x", ((tcg_mid_info_t *)tcgTmpBuf)->l2p_tbl[0]);
						//tcg_mid_trace(LOG_ERR, 0, "[TCG] check for debug, need to be UNMAP[1k]: 0x%x", ((tcg_mid_info_t *)tcgTmpBuf)->l2p_tbl[1024]);
						
						// get info
						if((meta->tcg_l2p.tag == TCG_L2P_TAG) && (meta->tcg_l2p.history_no > hist_no)) // still not handle hist_no overflow yet
						{
							tcg_mid_trace(LOG_ERR, 0x3a4b, "[TCG] history update");
							hist_no = meta->tcg_l2p.history_no;
							newest_paa = pda_list[0];
						}
					}
				}
			}
		}
	}
	if(hist_no)
#endif
	newest_paa = tcg_blksn2pda(epm_aes_data->new_blk_prog);
	{
		// read entire L2P table (5 pages)
		for(i=0; i<5; i++)
		{
			pda_list[0] = (newest_paa + (i<<shr_nand_info.pda_page_shift));
			pda_list[1] = pda_list[0]+1;
			pda_list[2] = pda_list[0]+2;
			pda_list[3] = pda_list[0]+3;

			// read from NAND in single page units
			for(j = 0; j < DU_CNT_PER_PAGE; j++)
			{
				tcg_io_cmd.bm_pl_list[j].all = 0;
				tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_l2p+(i*DU_CNT_PER_PAGE)+j));
				tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
				tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
				tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = TcgMetaIdx;

				//tcg_ncl_cmd(&pda_list[j], NCL_CMD_OP_READ, &tcg_io_cmd.bm_pl_list[j], 1, DU_4K_DEFAULT_MODE);
			}
			if(tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, tcg_io_cmd.bm_pl_list, 4, DU_4K_DEFAULT_MODE))
			{
				l2p_err = true;
				goto L2P_NOT_EXIST;
			}
		}
		/*for(j=TCG_G1_LAA0; j<=TCG_G3_LAA1; j++)
		{
			tcg_mid_trace(LOG_INFO, 0, "[TCG] check L2P normal:  0x%x", tcg_mid_info->l2p_tbl[j]);
		}*/
		
		// update L2P for after L2P with greatest history number
		// update next paa when scan finish
		for(i=5; i<(shr_nand_info.geo.nr_pages/3); i++)
		{
			// read newest_paa + (i << shr_nand_info.pda_page_shift)
			pda_list[0] = newest_paa + (i << shr_nand_info.pda_page_shift);
			
			tcg_io_cmd.bm_pl_list[0].all = 0;
			tcg_io_cmd.bm_pl_list[0].pl.dtag = (DTAG_IN_DDR_MASK | dtag_buf);
			//tcg_io_cmd.bm_pl_list[0].pl.du_ofst = 0;
			tcg_io_cmd.bm_pl_list[0].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
			tcg_io_cmd.bm_pl_list[0].pl.nvm_cmd_id = TcgMetaIdx;
			
			tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, tcg_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE);
			

			// check TCG data is valid or not
			if(meta->tcg_data.tag == TCG_TAG)
			{
				//tcg_mid_trace(LOG_INFO, 0, "[TCG] valid data is exist, count %d", i);

				u32 blk_updt;
				u32 old_paa = tcg_mid_info->l2p_tbl[meta->tcg_data.laa];
				if(old_paa!=UNMAP_PDA)
				{
					blk_updt = tcg_pda2blksn(old_paa);
			
					// Subtract VAC with blkID of old PAA
					tcg_mid_info->vac[blk_updt]--;
				}
				blk_updt = tcg_pda2blksn(pda_list[0]);
				tcg_mid_info->vac[blk_updt]++;
				tcg_mid_info->l2p_tbl[meta->tcg_data.laa] = pda_list[0];
			}
			else
			{
				tcg_mid_trace(LOG_ERR, 0xd10c, "[TCG] valid data is NOT exist, BREAK count %d", i);
				tcg_next_paa = pda_list[0];
				break;
			}
		}
		//if(i == shr_nand_info.geo.nr_pages)
		//	tcg_next_paa = INV_PDA;

		// Rebuild TCG table for G1-3 according L2P table
		// for loop in page unit
		bool tag_chk;
		for(i=0; i<5; i++)
		{
			tag_chk = false;
			
			// read entire TCG G1-3 table to vars G1-3 (5 pages, from TCG_G1_LAA0 to TCG_G3_LAA1)
			pda_list[0] = tcg_mid_info->l2p_tbl[TCG_G1_LAA0 + i];
			pda_list[1] = pda_list[0]+1;
			pda_list[2] = pda_list[0]+2;
			pda_list[3] = pda_list[0]+3;
			tcg_mid_trace(LOG_ERR, 0xac8c, "[TCG] Read normal Table, PAA: 0x%x", pda_list[0]);

			for(j = 0; j < DU_CNT_PER_PAGE; j++)
			{
				tcg_io_cmd.bm_pl_list[j].all = 0;
				tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_buf+(i*DU_CNT_PER_PAGE)+j));
				tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
				tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
				tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = TcgMetaIdx;

				//result = tcg_ncl_cmd(&pda_list[j], NCL_CMD_OP_READ, &tcg_io_cmd.bm_pl_list[j], 1, DU_4K_DEFAULT_MODE);
			}
G5_CHK_AGAIN:
			result = tcg_ncl_cmd(pda_list, NCL_CMD_OP_READ, tcg_io_cmd.bm_pl_list, 4, DU_4K_DEFAULT_MODE);

			switch(i)
			{
				case 0:
					tag_chk = ((tmpBuf_tbl_G1->b.mTcgTblInfo.ID != TCG_TBL_ID) || (tmpBuf_tbl_G1->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) || (tmpBuf_tbl_G1->b.mEndTag != TCG_END_TAG));
					break;
				case 1:
					tag_chk = ((tmpBuf_tbl_G2->b.mTcgTblInfo.ID != TCG_TBL_ID) || (tmpBuf_tbl_G2->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)));
					break;
				case 2:
					tag_chk = (tmpBuf_tbl_G2->b.mEndTag != TCG_END_TAG);
					break;
				case 3:
					tag_chk = ((tmpBuf_tbl_G3->b.mTcgTblInfo.ID != TCG_TBL_ID) || (tmpBuf_tbl_G3->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)));
					break;
				case 4:
					tag_chk = (tmpBuf_tbl_G3->b.mEndTag != TCG_END_TAG);
					break;
				
				default:
					tag_chk = false;
					break;
			}
			
			if((meta->tcg_data.tag!=TCG_TAG) || (result!=0) || tag_chk)
			{
				if(pda_list[0] < TCG_LAST_USE_LAA)
				{
					tag_chk = false;
			
					// read entire TCG G1-3 table to vars G1-3 (5 pages, from TCG_G1_LAA0 to TCG_G3_LAA1)
					pda_list[0] = tcg_mid_info->l2p_tbl[TCG_G1_LAA0 + TCG_LAST_USE_LAA + i];
					pda_list[1] = pda_list[0]+1;
					pda_list[2] = pda_list[0]+2;
					pda_list[3] = pda_list[0]+3;
					tcg_mid_trace(LOG_ERR, 0x7909, "[TCG] Read normal Table in G5, PAA: 0x%x", pda_list[0]);

					for(j = 0; j < DU_CNT_PER_PAGE; j++)
					{
						tcg_io_cmd.bm_pl_list[j].all = 0;
						tcg_io_cmd.bm_pl_list[j].pl.dtag = (DTAG_IN_DDR_MASK | (dtag_buf+(i*DU_CNT_PER_PAGE)+j));
						tcg_io_cmd.bm_pl_list[j].pl.du_ofst = j;
						tcg_io_cmd.bm_pl_list[j].pl.type_ctrl = DTAG_QID_DROP | META_DDR_IDX;
						tcg_io_cmd.bm_pl_list[j].pl.nvm_cmd_id = TcgMetaIdx;

						//result = tcg_ncl_cmd(&pda_list[j], NCL_CMD_OP_READ, &tcg_io_cmd.bm_pl_list[j], 1, DU_4K_DEFAULT_MODE);
					}

					goto G5_CHK_AGAIN;
				}
				else
				{
					tcg_mid_trace(LOG_ERR, 0x8b3b, "[TCG] Table Error, LAA: 0x%x !!", pda_list[0]);
				
					bTcgTblErr = true;
					epm_aes_data->tcg_err_flag &= BIT2;
		
					break;
				}
			}
		}
		
	}
	//else
L2P_NOT_EXIST:
	if(l2p_err)
	{
		tcg_mid_trace(LOG_ERR, 0x4be2, "[TCG] L2P table is not exist !!");
		
		bTcgTblErr = true;
		epm_aes_data->tcg_err_flag &= BIT1;
	}
	

	// According scanning result, update init TAG
	// assign pG1-3 to G1-3

ERR_TO_SKIP:

	evt_register(tcg_gc_handle, 0, &evt_tcg_gc_trigger);
	cpu_msg_register(CPU_MSG_TCG_NAND_API, ipc_tcg_nf_op);
	
#endif

	// initialize io check function
	tcg_io_chk_range = TcgRangeCheck_SMBR_cpu4;
#ifdef TCG_NAND_BACKUP
	cpu_msg_register(CPU_MSG_TCG_CHANGE_CHKFUNC_API, ipc_tcg_change_chkfunc);
#endif

}

#ifdef TCG_NAND_BACKUP
ddr_code int print_tcg_mid_main(int argc, char *argv[])
{
	tcg_mid_trace(LOG_ALW, 0x45d1, "[TCG] Print TCG mid info");
	tcg_mid_trace(LOG_ALW, 0x3922, "[TCG] Print TCG mid info");
	
	for(u8 i=0; i<(sizeof(tcg_mid_info->blk_erased)/sizeof(u32)); i++)
	{
		tcg_mid_trace(LOG_ALW, 0x5230, "[TCG] Block Erased map: 0x%x", tcg_mid_info->blk_erased[i]);
		tcg_mid_trace(LOG_ALW, 0xbdaa, "[TCG] Block Defect map: 0x%x", tcg_mid_info->defect_map[i]);
	}
	
	for(u16 j=0; j<(sizeof(tcg_mid_info->vac)/sizeof(u16)); j++)
	{
		tcg_mid_trace(LOG_ALW, 0xe919, "[TCG] Block vac: %d", tcg_mid_info->vac[j]);
	}
	
	return 0;
}

ddr_code int force_tcg_tbl_err_main(int argc, char *argv[])
{
	tcg_mid_trace(LOG_ALW, 0xb635, "[TCG] Force TCG tbl err");
	
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	epm_aes_data->prefmtted = 0;
	epm_update(AES_sign, (CPU_ID - 1));

	tcg_mid_trace(LOG_ALW, 0xe53d, "[TCG] pfmt TAG on EPM: 0x%x", epm_aes_data->prefmtted);
	
	return 0;
}

//static DEFINE_UART_CMD(get_tcg_paa, "get_tcg_paa", "get_tcg_paa [address(hex)]", "get_tcg_paa 0x2000", 1, 1, print_tcg_paa_main);
static DEFINE_UART_CMD(get_tcg_mid, "get_tcg_mid", "get_tcg_mid", "get_tcg_mid", 0, 0, print_tcg_mid_main);
static DEFINE_UART_CMD(set_tcg_err, "set_tcg_err", "set_tcg_err", "set_tcg_err", 0, 0, force_tcg_tbl_err_main);

#endif

#else // Jack Li
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#define __TCG_NF_MID
#include <string.h>
#include "sect.h"
#include "ipc.h"
#include "customer.h"
#include "FeaturesDef.h"
#include "nvme_spec.h"
#include "nvmet.h"
#include "MemAlloc.h"
#include "SharedVars.h"
#include "ErrorCodes.h"
#include "Monitor.h"
#include "SysInfo.h"
#include "dtag.h"
#include "btn.h"
#include "fio.h"
#include "misc.h"
#include "ftl.h"
#include "dpe.h"
#include "otp.h"
#include "tcgcommon.h"
#include "tcgtbl.h"
#include "tcg.h"
#include "tcg_sh_vars.h"
#include "tcg_nf_mid.h"
#include "tcg_nf_hal.h"
#include "aes.h"
#include "aes_api.h"
#include "crc32.h"


//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
#define LAA_PER_TCGLAA          (0x4000 / MBU_SIZE)  //TCG LAA = 16k, LAA = 4k

//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
/******************************************************
 * fast(DTCM) variables declare
 ******************************************************/
tcg_data U32     TCG_FIO_F_MUTE = 0;
tcg_data ALIGNED(4) enabledLockingTable_t mLockingRangeTable[LOCKING_RANGE_CNT + 1];

/******************************************************
 * normal(DDR) variables declare
 ******************************************************/

static tcg_data int (*gcbTcgNfHalHandle[MSG_TCG_NF_MID_LAST])(req_t* req) =
{
    tcg_nf_G1RdDefault,     tcg_nf_G2RdDefault,     tcg_nf_G3RdDefault,     tcg_nf_G4RdDefault,
    tcg_nf_G5RdDefault,     tcg_nf_G4WrDefault,     tcg_nf_G5WrDefault,     tcg_nf_G4BuildDefect,
    tcg_nf_G5BuildDefect,   tcg_nf_G1Rd,            tcg_nf_G1Wr,            tcg_nf_G2Rd,
    tcg_nf_G2Wr,            tcg_nf_G3Rd,            tcg_nf_G3Wr,            tcg_nf_G4DmyRd,
    tcg_nf_G4DmyWr,         tcg_nf_G5DmyRd,         tcg_nf_G5DmyWr,         tcg_nf_SMBRRd,
    tcg_nf_SMBRWr,          tcg_nf_SMBRCommit,      tcg_nf_SMBRClear,       tcg_nf_TSMBRClear,
    tcg_nf_DSRd,            tcg_nf_DSWr,            tcg_nf_DSCommit,        tcg_nf_DSClear,
    tcg_nf_TDSClear,        tcg_nf_G4FTL,           tcg_nf_G5FTL,           tcg_nf_InitCache,
    tcg_nf_ClrCache,        tcg_nf_NorEepInit,      tcg_nf_NorEepRd,        tcg_nf_NorEepWr,
    tcg_nf_NfCpuInit,       tcg_nf_syncZone51Media, tcg_secure_boot_enable, tcg_nf_tbl_recovery,
    tcg_nf_tbl_update,      tcg_nf_G3Wr_syncZone51,
};

// tcg_data tcgLaa_t    *MBR_TEMPL2PTBL = NULL;
// tcg_data tcgLaa_t    *DS_TEMPL2PTBL  = NULL;
tcg_data ALIGNED(4) U8                  tcgKEK[32];      //KEK for TcgTbl
tcg_data ALIGNED(4) tcgNfHalParams_t    tcgNfHalParams;
tcg_data    U32         FirstTx_ssdStartIdx_Record_flag = 0x0;      // get first write ssdStartIdx flag;
tcg_data    bool        TcgSuBlkExist;

tcm_data    bool        aes_St;
tcg_data    tPAA        *DR_G4PaaBuf;
tcg_data    tPAA        *DR_G5PaaBuf;
tcg_data    tcgLaa_t    *pMBR_TEMPL2PTBL = NULL;
tcg_data    tcgLaa_t    *pDS_TEMPL2PTBL  = NULL;
// tcm_data    U32         gTcgG4Defects = 0;
// tcm_data    U32         gTcgG5Defects = 0;

// tcg_data ALIGNED(4) sTcgKekData_Nor     mTcgKekDataNor;

//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------
extern tFIO_JOB*    pTcgJob;
extern U16*         gwDefSuperBlkTbl;
extern U8*          OnePgSzBuf;
extern tcg_sync_t   tcg_sync;
extern enabledLockingTable_t* pLockingRangeTable;
extern SecretZone_t secretZone;
// extern NfInfo_t     gNfInfo_CPU2;

//-----------------------------------------------------------------------------
//  Imported function proto-type without header include
//-----------------------------------------------------------------------------
extern void bm_ss_rn_gen(spr_reg_idx spr_idx, dpe_cb_t callback, void *ctx);
extern void trng_gen_random(U32 *buf, U32 dw_cnt);
extern  U32 read_otp_data(U32);
extern  int program_otp_data(U32 , U32);
void Tcg_GenCPinHash_Cpu4(U8 *pSrc, U8 srcLen, sCPin *pCPin);
int  CPinMsidCompare_Cpu4(U8 cpinIdx);
int  TcgPsidVerify_Cpu4(void);
void tcgWaitTCGXferDone(void);

//-----------------------------------------------------------------------------
//  Codes
//-----------------------------------------------------------------------------
tcg_code int tcg_nf_Start(req_t* req)
{
    int st;
    // TCG_DBG_P(1, 3, 0x820300);  //82 03 00, "[F]tcg_nf_Start"
    TCGPRN("tcg_nf_Start() subOp|%x\n", req->op_fields.TCG.subOpCode);
    DBG_P(0x2, 0x03, 0x720000, 4, req->op_fields.TCG.subOpCode);  // tcg_nf_Start() subOp|%x
    #ifndef alexcheck
    pHcmdMsg_cpy = (tMSG_HOST*)pHcmdMsg;
    #endif
    if( gcbTcgNfHalHandle[req->op_fields.TCG.subOpCode](req) == zNG ){
        // req->error = REQ_ERROR;     // Does req->error need to fill result after finish Job ?
        req->op_fields.TCG.param[3] = REQ_ERROR;
        st = zNG;
    }else{
        // req->error = REQ_NO_ERROR;   // Does req->error need to fill result after finish Job ?
        req->op_fields.TCG.param[3] = REQ_NO_ERROR;
        st = zOK;
    }

    save_l2pTblAssis();
    return st;
}

//-------------------------------------------------
// table key linked with root key
//-------------------------------------------------
tcg_code int tcg_tblkey_calc(U8* cbc_tbl_kek, U8* dest)
{
    U32 rootkey[8];
    U32 offset = 0x20 + 0x100; // OTP offset : PSID(0x20)
    if(read_otp_data(offset) == 0xFFFFFFFF)
    {
        HAL_Gen_Key(rootkey, 32);
        DBG_P(0xa, 0x03, 0x7200AE, 2, offset, 4, rootkey[0], 4, rootkey[1], 4, rootkey[2], 4, rootkey[3], 4, rootkey[4], 4, rootkey[5], 4, rootkey[6], 4, rootkey[7]);
        for(U8 i = 0; i < 8 ; i++)
        {
            if(program_otp_data(rootkey[i], offset + (i * sizeof(U32))) != 0)
            {
                TCG_ERR_PRN("Error!! otp write fail ...\n");
                DBG_P(0x01, 0x03, 0x7F7F13);  // Error!! otp write fail ...
                return zNG;
            }
        }
    }
    for(U8 i = 0; i < 8 ; i++)
    {
        rootkey[i] = read_otp_data(offset);
        offset = offset + 4;
    }
    #if 0
    DBG_P(0xa, 0x03, 0x7200AE, 2, offset - 32, 4, rootkey[0], 4, rootkey[1], 4, rootkey[2], 4, rootkey[3], 4, rootkey[4], 4, rootkey[5], 4, rootkey[6], 4, rootkey[7]);
    #endif
    HAL_PBKDF2((U32*)cbc_tbl_kek, 32, (U32*)rootkey, 32, (U32*)dest);
    //memcpy(dest, cbc_tbl_kek, 32);
    #if 0
    DBG_P(0xa, 0x03, 0x7200AE, 2, offset - 32, 4, *(U32*)dest, 4, *((U32*)dest + 1), 4, *((U32*)dest + 2), 4, *((U32*)dest + 3), 4, *((U32*)dest + 4), 4, *((U32*)dest + 5), 4, *((U32*)dest + 6), 4, *((U32*)dest + 7));
    #endif
    return zOK;
}

//-------------------------------------------------
//  read G1 Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G1RdDefault(req_t* req)
{
    // DBG_P(1, 3, 0x820319);  //82 03 19, "[F]tcg_nf_G1RdDefault"
    TCGPRN("tcg_nf_G1RdDefault()\n");
    DBG_P(0x01, 0x03, 0x720001 );  // tcg_nf_G1RdDefault()
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0 + 1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG){
        if(TCG_G5Rd() == zNG) return zNG;
    }
    // tcg_sw_cbc_decrypt(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 decrypt with tcgKEK
    hal_crypto(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G1));   // G1 decrypt with tcgKEK

    return zOK;
}

//-------------------------------------------------
//  read G2 Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G2RdDefault(req_t* req)
{
    // DBG_P(1, 3, 0x82031A);  //82 03 1A, "[F]tcg_nf_G2RdDefault"
    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1 + 1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG){
        if(TCG_G5Rd() == zNG) return zNG;
    }
    // tcg_sw_cbc_decrypt(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 decrypt with tcgKEK
    hal_crypto(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G2));   // G2 decrypt with tcgKEK
    return zOK;
}

//-------------------------------------------------
//  read G3 Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G3RdDefault(req_t* req)
{
    // DBG_P(1, 3, 0x82031B);  //82 03 1B, "[F]tcg_nf_G3RdDefault"
    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1 + 1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG){
        if(TCG_G5Rd() == zNG) return zNG;
    }
    // tcg_sw_cbc_decrypt(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 decrypt with tcgKEK
    hal_crypto(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G3));   // G3 decrypt with tcgKEK
    return zOK;
}


//-------------------------------------------------
//  read G4 (G1, G2 & G3) Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G4RdDefault(req_t* req)
{
    // DBG_P(1, 3, 0x82031D);  //82 03 1D, "[F]tcg_nf_G4RdDefault"
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0+1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG){
        if(TCG_G5Rd() == zNG) return zNG;
    }
    // tcg_sw_cbc_decrypt(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 decrypt with tcgKEK
    hal_crypto(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G1));   // G1 decrypt with tcgKEK

    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG){
        if(TCG_G5Rd() == zNG) return zNG;
    }
    // tcg_sw_cbc_decrypt(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 decrypt with tcgKEK
    hal_crypto(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G2));   // G2 decrypt with tcgKEK

    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG){
        if(TCG_G5Rd() == zNG) return zNG;
    }
    // tcg_sw_cbc_decrypt(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 decrypt with tcgKEK
    hal_crypto(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G3));   // G3 decrypt with tcgKEK

    return zOK;
}

//-------------------------------------------------
//  read G5 (TG1, TG2 & TG3) Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G5RdDefault(req_t* req)
{
    // DBG_P(1, 3, 0x82031C);  //82 03 1C, "[F]tcg_nf_G5RdDefault"
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0+1, tcgTmpBuf);
    if(TCG_G5Rd() == zNG) return zNG;
    // tcg_sw_cbc_decrypt(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 decrypt with tcgKEK
    hal_crypto(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G1));   // G1 decrypt with tcgKEK

    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G5Rd() == zNG) return zNG;
    // tcg_sw_cbc_decrypt(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 decrypt with tcgKEK
    hal_crypto(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G2));   // G2 decrypt with tcgKEK

    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G5Rd() == zNG) return zNG;
    // tcg_sw_cbc_decrypt(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 decrypt with tcgKEK
    hal_crypto(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G3));   // G3 decrypt with tcgKEK
    return zOK;
}

//-------------------------------------------------
//  write G4 (G1, G2 & G3) Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G4WrDefault(req_t* req)
{
    // DBG_P(1, 3, 0x82031F);  //82 03 1F, "[F]tcg_nf_G4WrDefault"

    // tcg_sw_cbc_encrypt(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G1));   // G1 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0+1, tcgTmpBuf);
    if(TCG_G4Wr() == zNG) return zNG;

    // tcg_sw_cbc_encrypt(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G2));   // G2 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G4Wr() == zNG) return zNG;

    // tcg_sw_cbc_encrypt(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G3));   // G3 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1+1, tcgTmpBuf);
    return TCG_G4Wr();
}

//-------------------------------------------------
//  write G5 (TG1, TG2 & TG3) Default table
//-------------------------------------------------
tcg_code static int tcg_nf_G5WrDefault(req_t* req)
{
    // DBG_P(1, 3, 0x82031E);  //82 03 1E, "[F]tcg_nf_G5WrDefault"

    // tcg_sw_cbc_encrypt(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G1));   // G1 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0+1, tcgTmpBuf);
    if(TCG_G5Wr() == zNG) return zNG;

    // tcg_sw_cbc_encrypt(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G2));   // G2 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G5Wr() == zNG) return zNG;

    // tcg_sw_cbc_encrypt(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G3));   // G3 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1+1, tcgTmpBuf);
    return TCG_G5Wr();
}

//-------------------------------------------------
//   G4  build defect
//-------------------------------------------------
tcg_code static int tcg_nf_G4BuildDefect(req_t* req)
{
    U32 x;
    U32 DefChanMap;

    TCGPRN("tcg_nf_G4BuildDefect()\n");
    DBG_P(0x01, 0x03, 0x720002 );  // tcg_nf_G4BuildDefect()
    // DBG_P(9, 3, 0x820321, //82 03 21, "[F]tcg_nf_G4BuildDefect, FTL provide BlkNo0[%X] 1[%X] 2[%X] 3[%X] 4[%X] 5[%X] 6[%X] 7[%X]", 2 2 2 2 2 2 2 2
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[0],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[1],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[2],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[3],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[4],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[5],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[6],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[7]);
    TCGPRN("FTL TCG BLK0|%x 1|%x 2|%x 3|%x 4|%x 5|%x 6|%x 7|%x\n", smSysInfo->d.TCGData.d.TCGBlockNo[0],
        smSysInfo->d.TCGData.d.TCGBlockNo[1], smSysInfo->d.TCGData.d.TCGBlockNo[2], smSysInfo->d.TCGData.d.TCGBlockNo[3],
        smSysInfo->d.TCGData.d.TCGBlockNo[4], smSysInfo->d.TCGData.d.TCGBlockNo[5], smSysInfo->d.TCGData.d.TCGBlockNo[6],
        smSysInfo->d.TCGData.d.TCGBlockNo[7]);
    DBG_P(0x9, 0x03, 0x720003, 4, smSysInfo->d.TCGData.d.TCGBlockNo[0], 4,smSysInfo->d.TCGData.d.TCGBlockNo[1], 4, smSysInfo->d.TCGData.d.TCGBlockNo[2], 4, smSysInfo->d.TCGData.d.TCGBlockNo[3], 4,smSysInfo->d.TCGData.d.TCGBlockNo[4], 4, smSysInfo->d.TCGData.d.TCGBlockNo[5], 4, smSysInfo->d.TCGData.d.TCGBlockNo[6], 4,smSysInfo->d.TCGData.d.TCGBlockNo[7]);  // FTL TCG BLK0|%x 1|%x 2|%x 3|%x 4|%x 5|%x 6|%x 7|%x

    if(smSysInfo->d.TCGData.d.TCGBlockNo[0] == smSysInfo->d.TCGData.d.TCGBlockNo[1]){
        // DBG_P(1, 3, 0x820330); //82 03 30, "!!!Error!!!, FTL didn't offer blocks for TCG. check out!"
    }
    //------------------------------ Group4 -----------------------------------
    TCG_FIO_F_MUTE = FIO_F_MUTE;
    memset(tcgTmpBuf, 0, TCG_GENERAL_BUF_SIZE);

    // GROUP 4 safe erase all and write pattern (multi chan)
    TCGPRN("=erase=\n");
    DBG_P(0x01, 0x03, 0x720004 );  // =erase=
    for(TcgG4Pnt.all = 0; TcgG4Pnt.pc.cell < TCG_MBR_CELLS; TcgG4Pnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap=0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + TcgG4Pnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG4Dft[TcgG4Pnt.pc.cell + x] != 0){
                    TCGPRN("ori dft|%x\n", TcgG4Pnt.pc.cell + x);
                    DBG_P(0x2, 0x03, 0x720005, 4, TcgG4Pnt.pc.cell + x);  // ori dft|%x
                    // DBG_P(2, 3, 0x820331, 1, TcgG4Pnt.pc.cell + x);  //82 03 31, "ori DF = %X", 1
                    DefChanMap|=mBIT(x);
                }
            }
        }

        // ___ERASE___,  check any erasing error happened ?
        #if 0  //Due to Seqencer Erase block is not clean in same CH,CE,LUN and different PLN in one trace.
        if(TCG_ErMulChan(ZONE_GRP4, TcgG4Pnt, DefChanMap) == zNG){
            int i;
            U16 DftBlk;

            for(i = 0; i < ValidCnt*PAA_NUM_PER_PAGE; i += PAA_NUM_PER_PAGE){
                if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i)!=0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, i+1)!=0) ||
                   (FIO_CHK_ERR_BMP(pTcgJob->job_id, i+2)!=0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, i+3)!=0)){
                    DftBlk = ValidBlks[i/PAA_NUM_PER_PAGE];
                    // DBG_P(2, 3, 0x820332, 2, DftBlk);  //82 03 32, "era DF = %X", 2
                    tcgG4Dft[DftBlk] = 0xDF;
                    DefChanMap |= mBIT(DftBlk & (DEVICES_NUM_PER_ROW - 1));
                }
            }
        }
        #else
        TCG_ErMulChan(ZONE_GRP4, TcgG4Pnt, DefChanMap);
        #endif

        // ___WRITE___,   multi write
        TCGPRN("=write=\n");
        DBG_P(0x01, 0x03, 0x720006 );  // =write=
        for(x = 0; x < PAGE_NUM_PER_BLOCK; x += SLC_OFF(1)){
            TcgG4Pnt.pc.page = x;

            // check any writing error happened ?
            if(TCG_WrMulChan(ZONE_GRP4,TcgG4Pnt,DefChanMap) == zNG){
                int i;
                U16 DftBlk;

                for(i = 0; i < ValidCnt*PAA_NUM_PER_PAGE; i += PAA_NUM_PER_PAGE){
                    if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+1)) != 0) ||
                      (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+2)) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+3)) != 0)){
                        DftBlk = ValidBlks[i / PAA_NUM_PER_PAGE];
                        TCGPRN("wr dft|%x\n", DftBlk);
                        DBG_P(0x2, 0x03, 0x720007, 4, DftBlk);  // wr dft|%x
                        // DBG_P(2, 3, 0x820333, 2, DftBlk);  //82 03 33, "wr DF = %X", 2
                        tcgG4Dft[DftBlk] = 0xDF;
                        DefChanMap |= mBIT(DftBlk & (DEVICES_NUM_PER_ROW - 1));
                    }
                }
            }
        }

        TcgG4Pnt.pc.page = 0;
    }

    // ___READ___,   GROUP4 read check, mark defect if found any (single chan)
    TCGPRN("=read=\n");
    DBG_P(0x01, 0x03, 0x720008 );  // =read=
    for(TcgG4Pnt.all = 0; TcgG4Pnt.pc.cell < TCG_MBR_CELLS; TcgG4Pnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap = 0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + TcgG4Pnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG4Dft[TcgG4Pnt.pc.cell + x] != 0){
                    DefChanMap |= mBIT(x);
                }
            }
        }
        // multi read
        for(x = 0; x < PAGE_NUM_PER_BLOCK; x += SLC_OFF(1)){
            TcgG4Pnt.pc.page = x;

            if(TCG_RdMulChan(ZONE_GRP4, TcgG4Pnt, DefChanMap) == zNG){
                int i;
                U16 DftBlk;

                for(i = 0; i < ValidCnt*PAA_NUM_PER_PAGE; i += PAA_NUM_PER_PAGE){
                    if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+1)) != 0) ||
                      (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+2)) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+3)) != 0)){
                        DftBlk = ValidBlks[i/PAA_NUM_PER_PAGE];
                        TCGPRN("rd dft|%x\n", DftBlk);
                        DBG_P(0x2, 0x03, 0x720009, 4, DftBlk);  // rd dft|%x
                        // DBG_P(2, 3, 0x820334, 2, DftBlk);  //82 03 34, "rd DF = %X", 2
                        tcgG4Dft[DftBlk] = 0xDF;
                        DefChanMap |= mBIT(DftBlk & (DEVICES_NUM_PER_ROW - 1));
                    }
                }
            }
        }

        TcgG4Pnt.pc.page = 0;
    }
    // ___ERASE___,   GROUP4 safe erase all (multi chan)
    TCGPRN("=erase=\n");
    DBG_P(0x01, 0x03, 0x72000A );  // =erase=
    for(TcgG4Pnt.all = 0; TcgG4Pnt.pc.cell < TCG_MBR_CELLS; TcgG4Pnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap=0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + TcgG4Pnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG4Dft[TcgG4Pnt.pc.cell+x] != 0){
                    TCGPRN("fnl dft|%x\n", TcgG4Pnt.pc.cell + x);
                    DBG_P(0x2, 0x03, 0x72000B, 4, TcgG4Pnt.pc.cell + x);  // fnl dft|%x
                    // DBG_P(2, 3, 0x820335, 1, TcgG4Pnt.pc.cell + x);  //82 03 35, "fnl DF = %X", 1
                    DefChanMap |= mBIT(x);
                }
            }
        }
        TCG_ErMulChan(ZONE_GRP4, TcgG4Pnt, DefChanMap);
    }
    TCG_FIO_F_MUTE = 0;
    memset(pG4->b.TcgMbrL2P, 0xFF, sizeof(pG4->b.TcgMbrL2P));
    TCGPRN("=G4 OK=\n");
    DBG_P(0x01, 0x03, 0x72000C );  // =G4 OK=
    return zOK;
}

//-------------------------------------------------
//   G5  build defect
//-------------------------------------------------
tcg_code static int tcg_nf_G5BuildDefect(req_t* req)
{
    U32 x;
    U32 DefChanMap;

    TCGPRN("tcg_nf_G5BuildDefect()\n");
    DBG_P(0x01, 0x03, 0x72000D );  // tcg_nf_G5BuildDefect()
    // DBG_P(9, 3, 0x820320, //82 03 20, "[F]tcg_nf_G5BuildDefect, FTL provide BlkNo0[%X] 1[%X] 2[%X] 3[%X] 4[%X] 5[%X] 6[%X] 7[%X]", 2 2 2 2 2 2 2 2
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[0],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[1],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[2],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[3],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[4],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[5],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[6],
             // 2, smSysInfo->d.TCGData.d.TCGBlockNo[7]);
    if(smSysInfo->d.TCGData.d.TCGBlockNo[0] == smSysInfo->d.TCGData.d.TCGBlockNo[1]){
        // DBG_P(1, 3, 0x820330); //82 03 30, "!!!Error!!!, FTL didn't offer blocks for TCG. check out!"
    }
    //------------------------------ Group5 -----------------------------------
    TCG_FIO_F_MUTE = FIO_F_MUTE;
    memset(tcgTmpBuf, 0, TCG_GENERAL_BUF_SIZE);
    // GROUP 5 safe erase all and write pattern (multi chan)
    TCGPRN("=erase=\n");
    DBG_P(0x01, 0x03, 0x72000E );  // =erase=
    for(TcgG5Pnt.all = 0; TcgG5Pnt.pc.cell < TCG_MBR_CELLS; TcgG5Pnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap = 0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + TcgG5Pnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG5Dft[TcgG5Pnt.pc.cell+x] != 0){
                    TCGPRN("ori dft|%x\n", TcgG5Pnt.pc.cell + x);
                    DBG_P(0x2, 0x03, 0x72000F, 4, TcgG5Pnt.pc.cell + x);  // ori dft|%x
                    // DBG_P(2, 3, 0x820331, 1, TcgG5Pnt.pc.cell + x);  //82 03 31, "ori DF = %X", 1
                    DefChanMap |= mBIT(x);
                }
            }
        }
        // ___ERASE___,  check any error happened after erase?
        #if 0  //Due to Seqencer Erase block is not clean in same CH,CE,LUN and different PLN in one trace.
        if(TCG_ErMulChan(ZONE_GRP5, TcgG5Pnt, DefChanMap) == zNG){
            int i;
            U16 DftBlk;

            for(i = 0; i < ValidCnt*PAA_NUM_PER_PAGE; i += PAA_NUM_PER_PAGE){
                if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i)!=0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, i+1)!=0) ||
                    (FIO_CHK_ERR_BMP(pTcgJob->job_id, i+2)!=0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, i+3)!=0)){
                    DftBlk = ValidBlks[i/PAA_NUM_PER_PAGE];
                    // DBG_P(2, 3, 0x820332, 2, DftBlk);  //82 03 32, "era DF = %X", 2
                    tcgG5Dft[DftBlk] = 0xDF;
                    DefChanMap |= mBIT(DftBlk & (DEVICES_NUM_PER_ROW - 1));
                }
            }
        }
        #else
        TCG_ErMulChan(ZONE_GRP5, TcgG5Pnt, DefChanMap);
        #endif

        // ___WRITE___,  multi write
        TCGPRN("=write=\n");
        DBG_P(0x01, 0x03, 0x720010 );  // =write=
        for(x = 0; x < PAGE_NUM_PER_BLOCK; x += SLC_OFF(1)){
            TcgG5Pnt.pc.page = x;

            // check any writing error happened ?
            if(TCG_WrMulChan(ZONE_GRP5, TcgG5Pnt, DefChanMap) == zNG){
                int i;
                U16 DftBlk;

                for(i = 0; i < ValidCnt*PAA_NUM_PER_PAGE; i += PAA_NUM_PER_PAGE){
                    if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+1)) != 0) ||
                      (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+2)) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+3)) != 0)){
                        DftBlk = ValidBlks[i/PAA_NUM_PER_PAGE];
                        TCGPRN("wr dft|%x\n", DftBlk);
                        DBG_P(0x2, 0x03, 0x720011, 4, DftBlk);  // wr dft|%x
                        // DBG_P(2, 3, 0x820333, 2, DftBlk);  //82 03 33, "wr DF = %X", 2
                        tcgG5Dft[DftBlk] = 0xDF;
                        DefChanMap |= mBIT(DftBlk & (DEVICES_NUM_PER_ROW - 1));
                    }
                }
            }
        }

        TcgG5Pnt.pc.page = 0;
    }

    // ___READ___,  multi read, GROUP5 read check, mark defect if found any (single chan)
    TCGPRN("=read=\n");
    DBG_P(0x01, 0x03, 0x720012 );  // =read=
    for(TcgG5Pnt.all = 0; TcgG5Pnt.pc.cell < TCG_MBR_CELLS; TcgG5Pnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap = 0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + TcgG5Pnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG5Dft[TcgG5Pnt.pc.cell + x] != 0){
                    DefChanMap |= mBIT(x);
                }
            }
        }
        //multi read
        for(x = 0; x < PAGE_NUM_PER_BLOCK; x += SLC_OFF(1)){
            TcgG5Pnt.pc.page = x;
            TCGPRN("blk|%x page|%x\n", TcgG5Pnt.pc.cell, x);
            // DBG_P(0x3, 0x03, 0x7200DB, 4, TcgG5Pnt.pc.cell, 4, x);  // blk|%x page|%x
            if(TCG_RdMulChan(ZONE_GRP5, TcgG5Pnt, DefChanMap) == zNG){
                int i;
                U16 DftBlk;

                for(i = 0; i < ValidCnt*PAA_NUM_PER_PAGE; i += PAA_NUM_PER_PAGE){
                    if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+1)) != 0) ||
                      (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+2)) != 0) || (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+3)) != 0)){
                        DftBlk = ValidBlks[i/PAA_NUM_PER_PAGE];
                        TCGPRN("rd dft|%x\n", DftBlk);
                        DBG_P(0x2, 0x03, 0x720013, 4, DftBlk);  // rd dft|%x
                        // DBG_P(2, 3, 0x820334, 2, DftBlk);  //82 03 34, "rd DF = %X", 2
                        tcgG5Dft[DftBlk] = 0xDF;
                        DefChanMap |= mBIT(DftBlk & (DEVICES_NUM_PER_ROW - 1));
                    }
                }
            }
        }

        TcgG5Pnt.pc.page = 0;
    }

    // ___ERASE___,  GROUP5 safe erase all (multi chan)
    TCGPRN("=erase=\n");
    DBG_P(0x01, 0x03, 0x720014 );  // =erase=
    for(TcgG5Pnt.all = 0; TcgG5Pnt.pc.cell < TCG_MBR_CELLS; TcgG5Pnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap=0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + TcgG5Pnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG5Dft[TcgG5Pnt.pc.cell + x]!=0){
                    TCGPRN("fnl dft|%x\n", TcgG5Pnt.pc.cell + x);
                    DBG_P(0x2, 0x03, 0x720015, 4, TcgG5Pnt.pc.cell + x);  // fnl dft|%x
                    // DBG_P(2, 3, 0x820335, 1, TcgG5Pnt.pc.cell + x);  //82 03 35, "fnl DF = %X", 1
                    DefChanMap |= mBIT(x);
                }
            }
        }
        TCG_ErMulChan(ZONE_GRP5, TcgG5Pnt, DefChanMap);
    }
    TCG_FIO_F_MUTE = 0;
    memset(pG5->b.TcgTempMbrL2P, 0xFF, sizeof(pG5->b.TcgTempMbrL2P));
    TCGPRN("=G5 OK=\n");
    DBG_P(0x01, 0x03, 0x720016 );  // =G5 OK=
    return zOK;
}


//-------------------------------------------------
//  Group1 read
//-------------------------------------------------
tcg_code static int tcg_nf_G1Rd(req_t* req)
{
    // DBG_P(1, 3, 0x820301);  //82 03 01, "[F]tcg_nf_G1Rd"
    tcg_nf_mid_params_set(TCG_G1_LAA0, TCG_G1_LAA0+1, tcgTmpBuf);

    if(TCG_G4Rd() == zNG){
        if(G4RdRetry() == zNG){         // TODO: read retry
            if(TCG_G5Rd() == zNG){
                if(G5RdRetry() == zNG){        // TODO: read retry
                    return zNG;   // if read retry failure
                }
            }else{
                TCG_G4Wr();
            }
        }
    }
    // tcg_sw_cbc_decrypt(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 decrypt with tcgKEK
    hal_crypto(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G1));   // G1 decrypt with tcgKEK
    return zOK;
}

//-------------------------------------------------
//  Group1 write
//-------------------------------------------------
tcg_code static int tcg_nf_G1Wr(req_t* req)
{
    int status = zOK;

    // DBG_P(1, 3, 0x820302);  //82 03 02, "[F]tcg_nf_G1Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G1));   // G1 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G1_LAA0, TCG_G1_LAA0+1, tcgTmpBuf);

    if(TCG_G5Wr() == zNG)  status = zNG;
    if(TCG_G4Wr() == zNG)  status = zNG;
    return status;
}

//-------------------------------------------------
//  Group2 read
//-------------------------------------------------
tcg_code static int tcg_nf_G2Rd(req_t* req)
{
    // DBG_P(1, 3, 0x820305);  //82 03 05, "[F]tcg_nf_G2Rd"
    tcg_nf_mid_params_set(TCG_G2_LAA0, TCG_G2_LAA1+1, tcgTmpBuf);

    if(TCG_G4Rd() == zNG){
        if(G4RdRetry() == zNG){         // TODO: read retry
            if(TCG_G5Rd() == zNG){
                if(G5RdRetry() == zNG){        // TODO: read retry
                    return zNG;   // if read retry failure
                }
            }else{
                TCG_G4Wr();
            }
        }
    }
    // tcg_sw_cbc_decrypt(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));    // G2 decrypt with tcgKEK
    hal_crypto(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G2));    // G2 decrypt with tcgKEK

    return zOK;
}

//-------------------------------------------------
//  Group2 write
//-------------------------------------------------
tcg_code static int tcg_nf_G2Wr(req_t* req)
{
    int status = zOK;

    // DBG_P(1, 3, 0x820306);  //82 03 06, "[F]tcg_nf_G2Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));  // G2 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G2));  // G2 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G2_LAA0, TCG_G2_LAA1+1, tcgTmpBuf);

    if(TCG_G5Wr() == zNG)  status = zNG;
    if(TCG_G4Wr() == zNG)  status = zNG;
    return status;
}


//-------------------------------------------------
//  Group3 read
//-------------------------------------------------
tcg_code static int tcg_nf_G3Rd(req_t* req)
{
    // DBG_P(1, 3, 0x820309);  //82 03 09, "[F]tcg_nf_G3Rd"
    tcg_nf_mid_params_set(TCG_G3_LAA0, TCG_G3_LAA1+1, tcgTmpBuf);

    if(TCG_G4Rd() == zNG){
        if(G4RdRetry() == zNG){         // TODO: read retry
            if(TCG_G5Rd() == zNG){
                if(G5RdRetry() == zNG){        // TODO: read retry
                    return zNG;   // if read retry failure
                }
            }else{
                TCG_G4Wr();
            }
        }
    }

    // decrypt aes key for all range
    #if _TCG_ != TCG_PYRITE
    for(int i = 0; i < (LOCKING_RANGE_CNT + 1); i++){
        // tcg_sw_ebc_decrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[i].dek.aesKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, FALSE, sizeof(G3.b.mWKey[i].dek.aesKey));
        // tcg_sw_ebc_decrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[i].dek.xtsKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, FALSE, sizeof(G3.b.mWKey[i].dek.xtsKey));
    }
    #endif
    // decrypt G3
    // tcg_sw_cbc_decrypt(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));
    hal_crypto(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G3));
    //alexcheck
    // memcpy(G3.all, tcgTmpBuf, sizeof(G3));

    if (G3.b.mTcgTblInfo.ID == TCG_TBL_ID)
        return zOK;
    else
        return zNG;
}

//-------------------------------------------------
//  Group3 write
//-------------------------------------------------
tcg_code static int tcg_nf_G3Wr(req_t* req)
{
    int status = zOK;
    // bool bChangeKey = FALSE;

    // DBG_P(1, 3, 0x82030A);  //82 03 0A, "[F]tcg_nf_G3Wr"

    // encrypt G3
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G3));   // G3 encrypt with tcgKEK
    // encrypt aes key for all range
    #if _TCG_ != TCG_PYRITE
    for(int i = 0; i < (LOCKING_RANGE_CNT + 1); i++){
        // tcg_sw_ebc_encrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[0].dek.aesKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, TRUE, sizeof(G3.b.mWKey[0].dek.aesKey));
        // tcg_sw_ebc_encrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[i].dek.xtsKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, TRUE, sizeof(G3.b.mWKey[i].dek.xtsKey));
    }
    #endif

    //alexcheck
    // if ((req->op_fields.TCG.param[1]) == 0xEE){
        // bChangeKey = TRUE;
        // req->op_fields.TCG.param[1] = 0;
    // }

    tcg_nf_mid_params_set(TCG_G3_LAA0, TCG_G3_LAA1+1, tcgTmpBuf);

    if(TCG_G5Wr() == zNG) status = zNG;
    if(TCG_G4Wr() == zNG) status = zNG;

    // need to check G4Wr()/G5Wr() status or not?
    //cjdbg, TCG_NorKekWriteDone();

    return status;
}


//-------------------------------------------------
//  G4 dummy read
//-------------------------------------------------
tcg_code static int tcg_nf_G4DmyRd(req_t* req)
{
    U16 laas = req->op_fields.TCG.laas;
    U16 laae = req->op_fields.TCG.laae;

    // DBG_P(1, 3, 0x820323);  //82 03 23, "[F]tcg_nf_G4DmyRd"
    //memset(tcgTmpBuf, 0, (pHcmdMsg->laae - pHcmdMsg->laas) * CFG_UDATA_PER_PAGE);  // clear requirment
    memset(tcgTmpBuf, 0, TCG_GENERAL_BUF_SIZE);  // clear buffer all.
    FirstTx_ssdStartIdx_Record_flag = 0x53415645;
    tcg_nf_mid_params_set(TCG_DUMMY_LAA, TCG_DUMMY_LAA + (laae - laas), tcgTmpBuf);
    return TCG_G4DmyRd();
}


//-------------------------------------------------
//  G4 dummy write
//-------------------------------------------------
tcg_code static int tcg_nf_G4DmyWr(req_t* req)
{
    // DBG_P(1, 3, 0x820325);  //82 03 25, "[F]tcg_nf_G4DmyWr"

    memset(tcgTmpBuf, 0, CFG_UDATA_PER_PAGE);
    tcg_nf_mid_params_set(TCG_DUMMY_LAA, TCG_DUMMY_LAA+1, tcgTmpBuf);

    return TCG_G4Wr();
}


//-------------------------------------------------
//  G5 dummy read
//-------------------------------------------------
tcg_code static int tcg_nf_G5DmyRd(req_t* req)
{
    U16 laas = req->op_fields.TCG.laas;
    U16 laae = req->op_fields.TCG.laae;
    // DBG_P(1, 3, 0x820322);  //82 03 22, "[F]tcg_nf_G5DmyRd"
    //memset(tcgTmpBuf, 0, (pHcmdMsg->laae - pHcmdMsg->laas) * CFG_UDATA_PER_PAGE);  // clear requirment
    memset(tcgTmpBuf, 0, TCG_GENERAL_BUF_SIZE);   // clear buffer all.
    FirstTx_ssdStartIdx_Record_flag = 0x53415645;
    tcg_nf_mid_params_set(TCG_DUMMY_LAA, TCG_DUMMY_LAA + (laae - laas), tcgTmpBuf);

    return TCG_G5DmyRd();
}

//-------------------------------------------------
//  G5 dummy write
//-------------------------------------------------
tcg_code static int tcg_nf_G5DmyWr(req_t* req)
{
    // DBG_P(1, 3, 0x820324);  //82 03 24, "[F]tcg_nf_G5DmyWr"

    memset(tcgTmpBuf, 0, CFG_UDATA_PER_PAGE);
    tcg_nf_mid_params_set(TCG_DUMMY_LAA, TCG_DUMMY_LAA+1, tcgTmpBuf);

    return TCG_G5Wr();
}

//-------------------------------------------------
//  SMBR read
//-------------------------------------------------
tcg_code static int tcg_nf_SMBRRd(req_t* req)
{
    U16             laa, TotalRdCnt, RdCnt;
    U16             laas = req->op_fields.TCG.laas;  // laas
    U16             laae = req->op_fields.TCG.laae;  // laae
    U8*             bufptr = req->op_fields.TCG.pBuffer;
    U16             NextLaaStartIdx = 0;
    tTcgGrpDef      GrpZone,GrpZone_bak;
    //pHcmdMsg->param[0] : SMBR_ioCmdReq
    //pHcmdMsg->param[1] : mSessionManager.TransactionState
    // bool            SMBR_ioCmdReq = req->op_fields.TCG.param[2];                //pHcmdMsg->param[0] : SMBR_ioCmdReq
    // tTRNSCTN_STATE  TrnsctnSt = (tTRNSCTN_STATE)req->op_fields.TCG.param[3];   //pHcmdMsg->param[1] : mSessionManager.TransactionState
    int             st;

    // DBG_P(3, 3, 0x82030D, 2, laas, 2, laae); //82 03 0D, "[F]tcg_nf_SMBRRd laas[%X] laae[%X]"
    TCGPRN("tcg_nf_SMBRRd() laas|%x laae|%x\n", laas, laae);
    DBG_P(0x3, 0x03, 0x720017, 4, laas, 4, laae);  // tcg_nf_SMBRRd() laas|%x laae|%x
    FirstTx_ssdStartIdx_Record_flag = 0x53415645;        // get first write ssdStartIdx flag;
    if((laae - laas) > (TCG_GENERAL_BUF_SIZE / CFG_UDATA_PER_PAGE)){   //read length could be over buffer size ( TcgTmpBuf )
        TCGPRN("Error!!, Read size is over TcgTmpBuf, Rd BlkCnt=%x", laae - laas);
        DBG_P(0x2, 0x03, 0x720018, 4, laae - laas);  // Error!!, Read size is over TcgTmpBuf, Rd BlkCnt=%x
        return zNG;
    }

    if(((SMBR_ioCmdReq == TRUE) && (mSessionManager.TransactionState == TRNSCTN_ACTIVE)) || ((pMBR_TEMPL2PTBL[laas].blk) >= (TCG_MBR_CELLS))){
        GrpZone_bak = ZONE_SMBR;     //read from group4 (MBR)
    }else{
        GrpZone_bak = ZONE_TSMBR;  //read from group5 (tempMBR)
    }
    TotalRdCnt = laae - laas;
    RdCnt = 0;
    for(laa=0; laa<TotalRdCnt; laa++){
        if(RdCnt == 0) NextLaaStartIdx = laas+laa;
        if(((SMBR_ioCmdReq == TRUE) && (mSessionManager.TransactionState == TRNSCTN_ACTIVE)) || ((pMBR_TEMPL2PTBL[laas+laa].blk) >= (TCG_MBR_CELLS))){
            GrpZone = ZONE_SMBR;     //read from group4 (MBR)
        }else{
            GrpZone = ZONE_TSMBR;  //read from group5 (tempMBR)
        }

        if((GrpZone != GrpZone_bak) && RdCnt){
            tcg_nf_mid_params_set(NextLaaStartIdx, NextLaaStartIdx+RdCnt, bufptr+(NextLaaStartIdx-laas)*CFG_UDATA_PER_PAGE);
            if(GrpZone_bak == ZONE_SMBR) st = TCG_G4Rd();
            else st = TCG_G5Rd();
            if(st == zOK){  // read OK
                RdCnt = 0;
                GrpZone_bak = GrpZone;
                NextLaaStartIdx = laas + laa;
            }else{          // read NG
                if(GrpZone == ZONE_TSMBR){
                    if(TCG_G5Rd() == zOK){
                        if(tcg_nf_SMBRCommit(req) == zOK){    // copy to G4
                            RdCnt = 0;
                            GrpZone_bak = GrpZone;
                            NextLaaStartIdx = laas + laa;
                        }else{
                            return zNG;
                        }
                    }else{
                        return zNG;
                    }
                }else{
                    return zNG;
                }
            }
        }

        RdCnt++;
        if((RdCnt >= MAX_PAGE_PER_TRK) || (laa == (TotalRdCnt-1))){
            tcg_nf_mid_params_set(NextLaaStartIdx, NextLaaStartIdx+RdCnt, bufptr+(NextLaaStartIdx-laas)*CFG_UDATA_PER_PAGE);
            if(GrpZone == ZONE_SMBR) st = TCG_G4Rd();
            else st = TCG_G5Rd();
            if(st == zOK){
                RdCnt = 0;
                GrpZone_bak = GrpZone;
            }else{
                if(GrpZone_bak == ZONE_TSMBR){
                    if(TCG_G5Rd() == zOK){
                        if(tcg_nf_SMBRCommit(req) == zOK){    // copy to G4
                            RdCnt = 0;
                            GrpZone_bak = GrpZone;
                            NextLaaStartIdx = laas + laa;
                        }else{
                            return zNG;
                        }
                    }else{
                        return zNG;
                    }
                }else{
                    return zNG;
                }
            }
        }
    }
    return zOK;
}

//-------------------------------------------------
//  SMBR write
//-------------------------------------------------
tcg_code static int tcg_nf_SMBRWr(req_t* req)
{
    U32 laas = req->op_fields.TCG.laas;
    U32 laae = req->op_fields.TCG.laae;
    // U16 SrcLen = req->op_fields.TCG.param[2];
    // U16 DesOffset = req->op_fields.TCG.param[2] >> 16;
    // U8* SrcBuf = (U8 *)req->op_fields.TCG.param[3];

    TCGPRN("tcg_nf_SMBRWr()\n");
    DBG_P(0x01, 0x03, 0x720019 );  // tcg_nf_SMBRWr()
#if 1
    U16 laa;
    // DBG_P(3, 3, 0x82030E, 2, laas, 2, laae); //82 03 0E, "[F]tcg_nf_SMBRWr laas[%X] laae[%X]"

    // memcpy((U8 *)req->op_fields.TCG.pBuffer + DesOffset, SrcBuf, SrcLen);
    tcg_nf_mid_params_set(laas, laae, req->op_fields.TCG.pBuffer);
    if(TCG_G5Wr() == zNG){
        return zNG;
    }

    for(laa = laas; laa < laae; laa++){
        pMBR_TEMPL2PTBL[laa].all   =   pG5->b.TcgTempMbrL2P[laa].all;
    }
    return zOK;
#else
    U16 laa;
    U16 i;
    U8 *pbuf;

    HERE(NULL);
    memcpy((U8 *)pHcmdMsg->pBuffer + DesOffset, SrcBuf, SrcLen);
    pbuf = pHcmdMsg->pBuffer;
    for(i = pHcmdMsg->laas; i < pHcmdMsg->laae; i++){
        tcg_nf_mid_params_set(i, i + 1, (pbuf + ((i - (pHcmdMsg->laas)) * CFG_UDATA_PER_PAGE)));
        if(TCG_G5Wr() == zNG){
            return zNG;
        }

        pMBR_TEMPL2PTBL[i].all   =   pG5->b.TcgTempMbrL2P[i].all;
    }
    return zOK;

#endif
}

//-------------------------------------------------
//  SMBR cimmit
//-------------------------------------------------
tcg_code static int tcg_nf_SMBRCommit(req_t* req)
{
    U16     laa, FirstLaa = 0xFFFF;
    U16     ValidLaaCnt;

    TCGPRN("tcg_nf_SMBRCommit()\n");
    DBG_P(0x01, 0x03, 0x72001A );  // tcg_nf_SMBRCommit()
    // DBG_P(1, 3, 0x82030F);  //82 03 0F, "[F]tcg_nf_SMBRCommit"
    ValidLaaCnt = 0;
    for(laa=0; laa<((U32)MBR_LEN/CFG_UDATA_PER_PAGE); laa++){
        if((pMBR_TEMPL2PTBL[laa].blk) < (TCG_MBR_CELLS)){
            if(laa < FirstLaa) FirstLaa = laa;
            ValidLaaCnt++;
            // if(ValidLaaCnt < MAX_PAGE_PER_TRK){
            if(ValidLaaCnt < (MAX_PAGE_PER_TRK / 2)){   // since tcgTempBuf size has 256K and last 48K check L2P (TCG_G4WrL2p)
                continue;
            }else{
                tcg_nf_mid_params_set(FirstLaa, FirstLaa+ValidLaaCnt, tcgTmpBuf);
                if(TCG_G5Rd()== zNG) return zNG;
                if(TCG_G4Wr()== zNG) return zNG;
                ValidLaaCnt = 0;
                FirstLaa = 0xFFFF;
            }
        }else{
            if(ValidLaaCnt){
                tcg_nf_mid_params_set(FirstLaa, FirstLaa+ValidLaaCnt, tcgTmpBuf);
                if(TCG_G5Rd()== zNG) return zNG;
                if(TCG_G4Wr()== zNG) return zNG;
                ValidLaaCnt = 0;
                FirstLaa = 0xFFFF;
            }
        }
    }
    if(ValidLaaCnt){
        tcg_nf_mid_params_set(FirstLaa, FirstLaa+ValidLaaCnt, tcgTmpBuf);
        if(TCG_G5Rd()== zNG) return zNG;
        if(TCG_G4Wr()== zNG) return zNG;
    }
    memset(pMBR_TEMPL2PTBL, 0xFF, ((U32)MBR_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    return zOK;
}

//-------------------------------------------------
//  SMBR clear
//-------------------------------------------------
tcg_code static int tcg_nf_SMBRClear(req_t* req)
{
    TCGPRN("tcg_nf_SMBRClear()\n");
    DBG_P(0x01, 0x03, 0x72001B );  // tcg_nf_SMBRClear()
    // DBG_P(1, 3, 0x820310);  //82 03 10, "[F]tcg_nf_SMBRClear"

    memset(pMBR_TEMPL2PTBL, 0xFF, ((U32)MBR_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    memset(&pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START], 0xFF, ((U32)MBR_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));

    memset(&pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START], 0xFF, ((U32)MBR_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    memset(tcgTmpBuf, 0, CFG_UDATA_PER_PAGE);
    tcg_nf_mid_params_set(TCG_SMBR_LAA_START, TCG_SMBR_LAA_START+1, tcgTmpBuf);
    return TCG_G4Wr();
}

//-------------------------------------------------
//  SMBR clear
//-------------------------------------------------
tcg_code static int tcg_nf_TSMBRClear(req_t* req)
{
    TCGPRN("tcg_nf_TSMBRClear()\n");
    DBG_P(0x01, 0x03, 0x72001C );  // tcg_nf_TSMBRClear()
    // DBG_P(1, 3, 0x820311);  //82 03 11, "[F]tcg_nf_TSMBRClear"

    memset(pMBR_TEMPL2PTBL, 0xFF, ((U32)MBR_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    memset(&pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START], 0xFF, ((U32)MBR_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    TcgG5NxtPnt.all = 0xFFFFFFFF;
    memset(tcgTmpBuf, 0, CFG_UDATA_PER_PAGE);
    tcg_nf_mid_params_set(TCG_DUMMY_LAA, TCG_DUMMY_LAA+1, tcgTmpBuf);
    return TCG_G5Wr();
}

//-------------------------------------------------
//  Data Store read
//-------------------------------------------------
tcg_code static int tcg_nf_DSRd(req_t* req)
{
    U16             laa, TotalRdCnt, RdCnt;
    U16             laas = req->op_fields.TCG.laas + TCG_DS_LAA_START;
    U16             laae = req->op_fields.TCG.laae + TCG_DS_LAA_START;
    U8*             bufptr = req->op_fields.TCG.pBuffer;
    U16             NextLaaStartIdx = 0;
    tTcgGrpDef      GrpZone,GrpZone_bak;
    int             st;

    // DBG_P(3, 3, 0x820312, 2, laas, 2, laae); //82 03 12, "[F]tcg_nf_DSRd laas[%X] laae[%X]"
    TCGPRN("tcg_nf_DSRd()\n");
    DBG_P(0x01, 0x03, 0x72001D );  // tcg_nf_DSRd()
    if((laae - laas) > (TCG_GENERAL_BUF_SIZE / CFG_UDATA_PER_PAGE)){   //read length could be over buffer size ( TcgTmpBuf )
        TCG_PRINTF("!!Read size is over TcgTmpBuf, Rd BlkCnt=%x", req->op_fields.TCG.laae - req->op_fields.TCG.laas);
        return zNG;
    }

    if((pDS_TEMPL2PTBL[laas - TCG_DS_LAA_START].blk) >= (TCG_MBR_CELLS)){
        GrpZone_bak = ZONE_DS;     //read from group4 (MBR)
    }else{
        GrpZone_bak = ZONE_TDS;  //read from group5 (tempMBR)
    }
    TotalRdCnt = laae - laas;
    RdCnt = 0;
    for(laa=0; laa<TotalRdCnt; laa++){
        if(RdCnt == 0) NextLaaStartIdx = laas+laa;
        if((pDS_TEMPL2PTBL[laas + laa - TCG_DS_LAA_START].blk) >= (TCG_MBR_CELLS)){
            GrpZone = ZONE_DS;     //read from group4 (MBR)
        }else{
            GrpZone = ZONE_TDS;  //read from group5 (tempMBR)
        }
        if((GrpZone != GrpZone_bak) && RdCnt){
            tcg_nf_mid_params_set(NextLaaStartIdx, NextLaaStartIdx+RdCnt, bufptr+(NextLaaStartIdx-laas)*CFG_UDATA_PER_PAGE);
            if(GrpZone_bak == ZONE_DS) st = TCG_G4Rd();
            else st = TCG_G5Rd();
            if(st == zOK){  // read OK
                RdCnt = 0;
                GrpZone_bak = GrpZone;
                NextLaaStartIdx = laas + laa;
            }else{          // read NG
                if(GrpZone == ZONE_TDS){
                    if(TCG_G5Rd() == zOK){
                        if(tcg_nf_DSCommit(req) == zOK){    // copy to G4
                            RdCnt = 0;
                            GrpZone_bak = GrpZone;
                            NextLaaStartIdx = laas + laa;
                        }else{
                            return zNG;
                        }
                    }else{
                        return zNG;
                    }
                }else{
                    return zNG;
                }
            }
        }

        RdCnt++;
        if((RdCnt >= MAX_PAGE_PER_TRK) || (laa == (TotalRdCnt-1))){
            tcg_nf_mid_params_set(NextLaaStartIdx, NextLaaStartIdx+RdCnt, bufptr+(NextLaaStartIdx-laas)*CFG_UDATA_PER_PAGE);
            if(GrpZone == ZONE_DS) st = TCG_G4Rd();
            else st = TCG_G5Rd();
            if(st == zOK){
                RdCnt = 0;
                GrpZone_bak = GrpZone;
            }else{
                if(GrpZone_bak == ZONE_TDS){
                    if(TCG_G5Rd() == zOK){
                        if(tcg_nf_DSCommit(req) == zOK){    // copy to G4
                            RdCnt = 0;
                            GrpZone_bak = GrpZone;
                            NextLaaStartIdx = laas + laa;
                        }else{
                            return zNG;
                        }
                    }else{
                        return zNG;
                    }
                }else{
                    return zNG;
                }
            }
        }
    }
    return zOK;
}

//-------------------------------------------------
//  Data Store write
//-------------------------------------------------
tcg_code static int tcg_nf_DSWr(req_t* req)
{
    U16 laa;
    U16 laas = req->op_fields.TCG.laas + TCG_DS_LAA_START;
    U16 laae = req->op_fields.TCG.laae + TCG_DS_LAA_START;
    // U16 SrcLen = req->op_fields.TCG.param[2];
    // U16 DesOffset = req->op_fields.TCG.param[2] >> 16;
    // U8* SrcBuf = (U8 *)req->op_fields.TCG.param[3];
    TCGPRN("tcg_nf_DSWr()\n");
    DBG_P(0x01, 0x03, 0x72001E );  // tcg_nf_DSWr()
    // DBG_P(3, 3, 0x820313, 2, laas, 2, laae); //82 03 13, "[F]tcg_nf_DSWr laas[%X] laae[%X]"
    // memcpy((U8 *)req->op_fields.TCG.pBuffer + DesOffset, SrcBuf, SrcLen);

    tcg_nf_mid_params_set(laas, laae, req->op_fields.TCG.pBuffer);
    if(TCG_G5Wr() == zNG){
        return zNG;
    }

    for(laa=laas; laa<laae; laa++){
        pDS_TEMPL2PTBL[laa - TCG_DS_LAA_START].all   =   pG5->b.TcgTempMbrL2P[laa].all;
    }
    return zOK;
}

//-------------------------------------------------
//  Data Store commit
//-------------------------------------------------
tcg_code static int tcg_nf_DSCommit(req_t* req)
{
    U16     laa, FirstLaa = 0xFFFF;
    U16     ValidLaaCnt;

    TCGPRN("tcg_nf_DSCommit()\n");
    DBG_P(0x01, 0x03, 0x72001F );  // tcg_nf_DSCommit()
    // DBG_P(1, 3, 0x820314);  //82 03 14, "[F]tcg_nf_DSCommit"
    ValidLaaCnt = 0;
    for(laa=TCG_DS_LAA_START; laa<((U32)DATASTORE_LEN/CFG_UDATA_PER_PAGE)+TCG_DS_LAA_START; laa++){
        if((pDS_TEMPL2PTBL[laa-TCG_DS_LAA_START].blk) < (TCG_MBR_CELLS)){
            if(laa < FirstLaa) FirstLaa = laa;
            ValidLaaCnt++;
            if(ValidLaaCnt < MAX_PAGE_PER_TRK){
                continue;
            }else{
                tcg_nf_mid_params_set(FirstLaa, FirstLaa+ValidLaaCnt, tcgTmpBuf);
                if(TCG_G5Rd()== zNG) return zNG;
                if(TCG_G4Wr()== zNG) return zNG;
                ValidLaaCnt = 0;
                FirstLaa = 0xFFFF;
            }
        }else{
            if(ValidLaaCnt){
                tcg_nf_mid_params_set(FirstLaa, FirstLaa+ValidLaaCnt, tcgTmpBuf);
                if(TCG_G5Rd()== zNG) return zNG;
                if(TCG_G4Wr()== zNG) return zNG;
                ValidLaaCnt = 0;
                FirstLaa = 0xFFFF;
            }
        }
    }
    if(ValidLaaCnt){
        tcg_nf_mid_params_set(FirstLaa, FirstLaa+ValidLaaCnt, tcgTmpBuf);
        if(TCG_G5Rd()== zNG) return zNG;
        if(TCG_G4Wr()== zNG) return zNG;
    }
    memset(pDS_TEMPL2PTBL, 0xFF, ((U32)DATASTORE_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    return zOK;
}

//-------------------------------------------------
//  Data Store clear
//-------------------------------------------------
tcg_code static int tcg_nf_DSClear(req_t* req)
{
    // DBG_P(1, 3, 0x820315);  //82 03 15, "[F]tcg_nf_DSClear"

    TCGPRN("tcg_nf_DSClear()\n");
    DBG_P(0x01, 0x03, 0x720020 );  // tcg_nf_DSClear()
    memset(pDS_TEMPL2PTBL, 0xFF, ((U32)DATASTORE_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    memset(&pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START], 0xFF, ((U32)DATASTORE_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));

    memset(&pG4->b.TcgMbrL2P[TCG_DS_LAA_START],0xFF,((U32)DATASTORE_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    memset(tcgTmpBuf, 0, CFG_UDATA_PER_PAGE);
    tcg_nf_mid_params_set(TCG_DS_LAA_START, TCG_DS_LAA_START+1, tcgTmpBuf);
    return TCG_G4Wr();
}

//-------------------------------------------------
//  Data Store clear
//-------------------------------------------------
tcg_code static int tcg_nf_TDSClear(req_t* req)
{
    // DBG_P(1, 3, 0x820316);  //82 03 16, "[F]tcg_nf_TDSClear"

    TCGPRN("tcg_nf_TDSClear()\n");
    DBG_P(0x01, 0x03, 0x720021 );  // tcg_nf_TDSClear()
    memset(pDS_TEMPL2PTBL, 0xFF, ((U32)DATASTORE_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    memset(&pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START], 0xFF, ((U32)DATASTORE_LEN/(U32)CFG_UDATA_PER_PAGE)*sizeof(tcgLaa_t));
    TcgG5NxtPnt.all = 0xFFFFFFFF;
    memset(tcgTmpBuf, 0, CFG_UDATA_PER_PAGE);
    tcg_nf_mid_params_set(TCG_DS_DUMMY_LAA, TCG_DS_DUMMY_LAA+1, tcgTmpBuf);
    return TCG_G5Wr();
}

//-------------------------------------------------
//  get G4 FTL
//-------------------------------------------------
tcg_code static int tcg_nf_G4FTL(req_t* req)
{
    // G4, TCG MBR Group -start------------------------------------------------------------------
    U32 x;
    U32 DefChanMap;
    U8 DefsPerRow;
    U8 RdOKInx;
    int status;
    int i;
    U64 FunctionTime;

    // *(U32*)pRawKey = 0;
    mRawKey[0].state = 0;

    // DBG_P(1, 3, 0x820318);  //82 03 18, "[F]tcg_nf_G4FTL"
    TCGPRN("tcg_nf_G4FTL()\n");
    DBG_P(0x01, 0x03, 0x720022 );  // tcg_nf_G4FTL()

    TcgG4CurHistNo=0;
    TcgG4NxtHistNo=1;
    TcgG4CurPnt.all=0xFFFFFFFF;
    TcgG4NxtPnt.all=0;

    TCG_FIO_F_MUTE = FIO_F_MUTE;
    for(TcgG4Pnt.all = 0; TcgG4Pnt.pc.cell < TCG_MBR_CELLS; TcgG4Pnt.pc.cell += (DEVICES_NUM_PER_ROW)){
        DefChanMap = 0;
        DefsPerRow = 0;
        for(x = 0; x < (DEVICES_NUM_PER_ROW); x++){
            if(tcgG4Dft[TcgG4Pnt.pc.cell + x] != 0){
                DefChanMap |= mBIT(x);
                DefsPerRow++;
            }
        }

        // read first page of each block
        TcgG4Pnt.pc.page = 0;
        TCG_RdMulChan(ZONE_GRP4, TcgG4Pnt, DefChanMap);

        for(i = 0; i < (PAA_NUM_PER_ROW - DefsPerRow*PAA_NUM_PER_PAGE); i += PAA_NUM_PER_PAGE)
        {
            if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i)==0) && (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+1))==0) && (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+2))==0) && (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+3))==0))   // i & i+1 is same page for 16K page size
            {
                if(gTcgAux[i].aux0 == TCG_G4_TAG)
                {
                    if(gTcgAux[i].aux1 >= TcgG4CurHistNo)
                    {
                        TcgG4CurHistNo = gTcgAux[i].aux1;
                        RdOKInx = 0;
                        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
                            if((DefChanMap & mBIT(x)) || ((TcgG4Pnt.pc.cell + x) >= TCG_MBR_CELLS))
                            {
                                 continue;
                            }
                            else
                            {
                                if(i/PAA_NUM_PER_PAGE == RdOKInx)
                                {
                                    TcgG4CurPnt.pc.cell = TcgG4Pnt.pc.cell + x;
                                    break;
                                }
                                else
                                {
                                    RdOKInx++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    //alexdebugws(TcgGroup4CurrPnt.all,TcgGroup4CurrPnt);

    TcgG4NxtHistNo=TcgG4CurHistNo+1;

    // DBG_P(3, 3, 0x820336, 4, TcgG4NxtPnt.all, 4, TcgG4CurPnt.all);  //82 03 36, "be TcgG4NxtPnt[%08X] TcgG4CurPnt[%08X]", 4 4
    TCGPRN("be TcgG4NxtPnt|%x TcgG4CurPnt|%x\n", TcgG4NxtPnt.all, TcgG4CurPnt.all);
    DBG_P(0x3, 0x03, 0x720023, 4, TcgG4NxtPnt.all, 4, TcgG4CurPnt.all);  // be TcgG4NxtPnt|%x TcgG4CurPnt|%x

    FunctionTime = Misc_GetTick_U64();  //StanleyLin2090524 Add debug POR time

    if(TcgG4CurPnt.all == 0xFFFFFFFF){   // tbl not in nand, write the very first one
        //soutb3(0x38, 0x01, 0x04);   //38 01,       "  TCG group %02X write" 1
        pG4->b.TcgG4Header=0x5631;     // "V1"
        memset(pG4->b.TcgMbrL2P,0xFF,sizeof(pG4->b.TcgMbrL2P));
        TcgG4NxtPnt.all = 0;

        while (TcgG4NxtPnt.pc.cell < TCG_MBR_CELLS)
        {
            status = zOK;
            while (tcgG4Dft[TcgG4NxtPnt.pc.cell])   TcgG4NxtPnt.pc.cell++;

            // if (tcg_nf_G5RdDefault(pHcmdMsg) == zNG)  status = zNG;  // Re-extract default table beacaue G1 buffer had changed at tcg_nf_G5FTL. New PSID is loaded to G1 buffer and write to G4 default table at tcg_nf_G4FTL.
            if (TCG_G4WrL2p() == zNG)  status = zNG;
            TCGPRN("st1|%x\n", status);
            DBG_P(0x2, 0x03, 0x720024, 4, status);  // st1|%x
            // for Eric EL Lin requirement to reduce risks.
            //Tcg_GenCPinHash_Cpu4(G1.b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);

            #if CO_SUPPORT_AES
            memcpy(OnePgSzBuf, G3.b.mWKey, sizeof(sWrappedKey));
            #endif
            if (tcg_nf_G5RdDefault(req) == zNG) status = zNG;
            #if CO_SUPPORT_AES
            memcpy(G3.b.mWKey, OnePgSzBuf, sizeof(sWrappedKey));
            #endif

            if (tcg_nf_G4WrDefault(req) == zNG)  status = zNG;
            TCGPRN("st2|%x\n", status);
            DBG_P(0x2, 0x03, 0x720025, 4, status);  // st2|%x
            if (TCG_G4_NewTable(req) == zNG)  status = zNG;
            TCGPRN("st3|%x\n", status);
            DBG_P(0x2, 0x03, 0x720026, 4, status);  // st3|%x

            if (status == zOK) break;
            TcgG4NxtPnt.pc.cell++;
            TcgG5NxtPnt.pc.page = 0;  //alexjan 20190417 , for CA5 team Ｇary say why page add 8(L2P+G1+G2+G3) when write fail
        }
        ASSERT(TcgG4NxtPnt.pc.cell != TCG_MBR_CELLS);

    }else{                              // tbl exists in nand, read it
        //soutb3(0x38, 0x02, 0x04);       //38 02,       "  TCG group %02X read" 1
        TcgG4CurPnt.pc.page = 0;
        TcgG4Pnt.all=TcgG4CurPnt.all;
        TcgG4NxtPnt.all=TcgG4CurPnt.all+SLC_OFF(L2P_PAGE_CNT);

        tcgL2pAssis.g4.tag = L2P_ASS_TAG;
        tcgL2pAssis.g4.cell_no = TcgG4NxtPnt.pc.cell;
        memset(tcgL2pAssis.g4.laa_no, 0, sizeof(tcgL2pAssis.g4.laa_no));
        // 1. read l2p on page0~4
        if(TCG_G4RdL2p() == zNG){
            // TODO: L2P readretry
        }

        // 2. walk thru page5~255, decide NextPnt and update l2p
        TcgG4Pnt.pc.page=SLC_OFF(L2P_PAGE_CNT);;
        while(1){
            if(TCG_Rd1Pg(TcgG4Pnt, ZONE_GRP4) == zNG){
                // TODO: readretry
            }

            if((pTcgJob->status & 0x0FFF) == FIO_S_NO_ERR){           // 0:ecc ok
                pG4->b.TcgMbrL2P[gTcgAux[0].aux0].all = TcgG4Pnt.all;
                TcgG4CurPnt.all = TcgG4NxtPnt.all;

                tcgL2pAssis.g4.tbl_idx = TcgG4NxtPnt.pc.page - L2P_PAGE_CNT;
                tcgL2pAssis.g4.laa_no[tcgL2pAssis.g4.tbl_idx] = gTcgAux[0].aux0;

                TcgG4NxtPnt.all += SLC_OFF(1);
            }else if(pTcgJob->status & FIO_S_BLANK_ERR){    // 1:blank
                break;
            }else{                  // other:ecc error
                TcgG4NxtPnt.all += SLC_OFF(1);
            }
            TcgG4Pnt.pc.page += SLC_OFF(1);
            if(TcgG4Pnt.pc.page == PAGE_NUM_PER_BLOCK){
                TcgG4Pnt.pc.page = 0;
                TcgG4NxtPnt.all = 0xFFFFFFFF;
                break;
            }
        }
    }

    // DBG_P(2,3,0xA6073A,4,Misc_GetElapsedTimeUsec(FunctionTime));
    TCGPRN("tcg_nf_G4FTL() time|%x\n", Misc_GetElapsedTimeUsec(FunctionTime));
    DBG_P(0x2, 0x03, 0x720027, 4, Misc_GetElapsedTimeUsec(FunctionTime));  // tcg_nf_G4FTL() time|%x

    TcgG4Defects=0;
    TCG_FIO_F_MUTE = 0;
    for(i=0;i<TCG_MBR_CELLS;i++){       // calculate G4 defect blocks
        if(tcgG4Dft[i]) TcgG4Defects++;
    }

    // DBG_P(3, 3, 0x820337, 4, TcgG4NxtPnt.all, 4, TcgG4CurPnt.all);  //82 03 37, "af TcgG4NxtPnt[%08X] TcgG4CurPnt[%08X]", 4 4
    TCGPRN("af TcgG4NxtPnt|%x TcgG4CurPnt|%x\n", TcgG4NxtPnt.all, TcgG4CurPnt.all);
    DBG_P(0x3, 0x03, 0x720028, 4, TcgG4NxtPnt.all, 4, TcgG4CurPnt.all);  // af TcgG4NxtPnt|%x TcgG4CurPnt|%x
    // G4, TCG MBR Group -end-------------------------------------------------------------------------------
    return zOK;
}


//-------------------------------------------------
//  get G5 FTL
//-------------------------------------------------
tcg_code static int tcg_nf_G5FTL(req_t* req)
{
    // G5, TCG Temp MBR Group -start-------------------------------------------------------------------------
    U32 x;
    U32 DefChanMap;
    U8 DefsPerRow;
    U8 RdOKInx;
    int status;
    int i;
    U64 FunctionTime;

    // DBG_P(1, 3, 0x820317);  //82 03 17, "[F]tcg_nf_G5FTL"
    TCGPRN("tcg_nf_G5FTL()\n");
    DBG_P(0x01, 0x03, 0x720029 );  // tcg_nf_G5FTL()

    TcgG5CurHistNo=0;
    TcgG5NxtHistNo=1;
    TcgG5CurPnt.all=0xFFFFFFFF;
    TcgG5NxtPnt.all=0;

    TCG_FIO_F_MUTE = FIO_F_MUTE;
    for(TcgG5Pnt.all = 0; TcgG5Pnt.pc.cell < TCG_MBR_CELLS; TcgG5Pnt.pc.cell += (DEVICES_NUM_PER_ROW)){
        DefChanMap = 0;
        DefsPerRow = 0;
        for(x = 0; x < (DEVICES_NUM_PER_ROW); x++){
            if(tcgG5Dft[TcgG5Pnt.pc.cell + x] != 0){
                DefChanMap |= mBIT(x);
                DefsPerRow++;
            }
        }
        // read first page of each block
        TcgG5Pnt.pc.page = 0;
        TCG_RdMulChan(ZONE_GRP5, TcgG5Pnt, DefChanMap);

        for(i = 0; i < (PAA_NUM_PER_ROW - DefsPerRow*PAA_NUM_PER_PAGE); i+=PAA_NUM_PER_PAGE)
        {
            if((FIO_CHK_ERR_BMP(pTcgJob->job_id, i)==0) && (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+1))==0) && (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+2))==0) && (FIO_CHK_ERR_BMP(pTcgJob->job_id, (i+3))==0))
            {
                if(gTcgAux[i].aux0 == TCG_G5_TAG)
                {
                    if(gTcgAux[i].aux1 >= TcgG5CurHistNo)
                    {
                        TcgG5CurHistNo = gTcgAux[i].aux1;
                        RdOKInx = 0;
                        for(x = 0; x < DEVICES_NUM_PER_ROW; x++)
                        {
                            if((DefChanMap & mBIT(x)) || ((TcgG5Pnt.pc.cell + x) >= TCG_MBR_CELLS))
                            {
                                 continue;
                            }
                            else
                            {
                                if(i/PAA_NUM_PER_PAGE == RdOKInx)
                                {
                                    TcgG5CurPnt.pc.cell = TcgG5Pnt.pc.cell + x;
                                    break;
                                }
                                else
                                {
                                    RdOKInx++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    TcgG5NxtHistNo=TcgG5CurHistNo+1;

    // DBG_P(3, 3, 0x820338,4, TcgG5NxtPnt.all, 4, TcgG5CurPnt.all);  //82 03 38, "be TcgG5NxtPnt[%08X] TcgG5CurPnt[%08X]", 4 4
    TCGPRN("be TcgG5NxtPnt|%x TcgG5CurPnt|%x\n", TcgG5NxtPnt.all, TcgG5CurPnt.all);
    DBG_P(0x3, 0x03, 0x72002A, 4, TcgG5NxtPnt.all, 4, TcgG5CurPnt.all);  // be TcgG5NxtPnt|%x TcgG5CurPnt|%x

    FunctionTime = Misc_GetTick_U64();  //StanleyLin2090524 Add debug POR time

    if(TcgG5CurPnt.all == 0xFFFFFFFF){   // tbl not in nand, write the very first one
        //soutb3(0x38, 0x01, 0x05);       //38 01,       "  TCG group %02X write" 1
        pG5->b.TcgG5Header = 0x5631;         // "V1"
        memset(pG5->b.TcgTempMbrL2P,0xFF,sizeof(pG5->b.TcgTempMbrL2P));
        TcgG5NxtPnt.all = 0;

        while (TcgG5NxtPnt.pc.cell < TCG_MBR_CELLS)
        {
            status = zOK;
            while (tcgG5Dft[TcgG5NxtPnt.pc.cell])   TcgG5NxtPnt.pc.cell++;

            if (TCG_G5WrL2p() == zNG)  status = zNG;
            TCGPRN("st1|%x\n", status);
            DBG_P(0x2, 0x03, 0x72002B, 4, status);  // st1|%x
            // for Eric EL Lin requirement to reduce risks.
            //Tcg_GenCPinHash_Cpu4(G1.b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);

            if (tcg_nf_G5WrDefault(req) == zNG)  status = zNG;
            TCGPRN("st2|%x\n", status);
            DBG_P(0x2, 0x03, 0x72002C, 4, status);  // st2|%x
            if (TCG_G5_NewTable(req) == zNG)  status = zNG;
            TCGPRN("st3|%x\n", status);
            DBG_P(0x2, 0x03, 0x72002D, 4, status);  // st3|%x
            if (status == zOK) break;
            TcgG5NxtPnt.pc.cell++;
            TcgG5NxtPnt.pc.page = 0;  //alexjan 20190417 , for CA5 team Ｇary say why page add 8(L2P+G1+G2+G3) when write fail
        }
        ASSERT(TcgG5NxtPnt.pc.cell != TCG_MBR_CELLS);

    }else{                              // tbl exists in nand, read it
        //soutb3(0x38, 0x02, 0x05);               //38 02,       "  TCG group %02X read" 1
        TcgG5CurPnt.pc.page = 0;
        TcgG5Pnt.all=TcgG5CurPnt.all;
        TcgG5NxtPnt.all=TcgG5CurPnt.all+SLC_OFF(L2P_PAGE_CNT);

        tcgL2pAssis.g5.tag = L2P_ASS_TAG;
        tcgL2pAssis.g5.cell_no = TcgG5NxtPnt.pc.cell;
        memset(tcgL2pAssis.g5.laa_no, 0, sizeof(tcgL2pAssis.g5.laa_no));
        // 1. read l2p on page0~4
        if(TCG_G5RdL2p() == zNG){
            // TODO: L2P readretry
        }

        // 2. walk thru page5~255, decide NextPnt and update l2p
        TcgG5Pnt.pc.page=SLC_OFF(L2P_PAGE_CNT);
        while(1){
            if(TCG_Rd1Pg(TcgG5Pnt, ZONE_GRP5) == zNG){
                // TODO: readretry
            }

            if((pTcgJob->status & 0x0FFF) == FIO_S_NO_ERR){          // 0:ecc ok
                pG5->b.TcgTempMbrL2P[gTcgAux[0].aux0].all = TcgG5Pnt.all;
                TcgG5CurPnt.all = TcgG5NxtPnt.all;

                tcgL2pAssis.g5.tbl_idx = TcgG5NxtPnt.pc.page - L2P_PAGE_CNT;
                tcgL2pAssis.g5.laa_no[tcgL2pAssis.g5.tbl_idx] = gTcgAux[0].aux0;

                TcgG5NxtPnt.all += SLC_OFF(1);
            }else if(pTcgJob->status & FIO_S_BLANK_ERR){    // 1:blank
                break;
            }else{                  // other:ecc error
                TcgG5NxtPnt.all += SLC_OFF(1);
            }
            TcgG5Pnt.pc.page += SLC_OFF(1);
            if(TcgG5Pnt.pc.page == PAGE_NUM_PER_BLOCK){
                TcgG5Pnt.pc.page = 0;
                TcgG5NxtPnt.all = 0xFFFFFFFF;
                break;
            }
        }
        //memset(TcgTempMbrL2P,0xFF,sizeof(TcgTempMbrL2P));
        //memset((mUINT_8 *)TcgTempMbrL2P,0xFF,((mUINT_32)MBR_LEN/(mUINT_32)CFG_UDATA_PER_PAGE)*2);  //clr G5 SMBR
        //memset((mUINT_8 *)&TcgTempMbrL2P[TCG_DATASTORE_LAA_START],0xFF,((mUINT_32)DATASTORE_LEN/CFG_UDATA_PER_PAGE)*2); //clr G5 DataStore
    }

    // DBG_P(2,3,0xA6073A,4,Misc_GetElapsedTimeUsec(FunctionTime));
    TCGPRN("tcg_nf_G5FTL() time|%x\n", Misc_GetElapsedTimeUsec(FunctionTime));
    DBG_P(0x2, 0x03, 0x72002E, 4, Misc_GetElapsedTimeUsec(FunctionTime));  // tcg_nf_G5FTL() time|%x

    TcgG5Defects=0;
    TCG_FIO_F_MUTE = 0;
    for(i=0;i<TCG_MBR_CELLS;i++){   //calculate G5 defect blocks
        if(tcgG5Dft[i]) TcgG5Defects++;
    }
    // G5, TCG Temp MBR Group -end-------------------------------------------------------------------------------
    // DBG_P(3, 3, 0x820339, 4, TcgG5NxtPnt.all, 4, TcgG5CurPnt.all);  //82 03 39, "af TcgG5NxtPnt[%08X] TcgG5CurPnt[%08X]", 4 4
    TCGPRN("af TcgG5NxtPnt|%x TcgG5CurPnt|%x\n", TcgG5NxtPnt.all, TcgG5CurPnt.all);
    DBG_P(0x3, 0x03, 0x72002F, 4, TcgG5NxtPnt.all, 4, TcgG5CurPnt.all);  // af TcgG5NxtPnt|%x TcgG5CurPnt|%x
    return zOK;
}


//-------------------------------------------------
//  initial Cache
//-------------------------------------------------
tcg_code static int tcg_nf_InitCache(req_t* req)
{
    // DBG_P(1, 3, 0x820340);  //82 03 40, "[F]tcg_nf_INITCache"

    //CACHE_Reset();

    return zOK;
}

//-------------------------------------------------
//  clear Cache
//-------------------------------------------------
tcg_code static int tcg_nf_ClrCache(req_t* req)
{
    // DBG_P(1, 3, 0x820329);  //82 03 29, "[F]tcg_nf_CLRCache"

    //CACHE_Reset();

    return zOK;
}

//-------------------------------------------------
//  init Nor Eeprom
//-------------------------------------------------
tcg_code static int tcg_nf_NorEepInit(req_t* req)
{
    // DBG_P(1, 3, 0x82032A);  //82 03 2A, "[F]tcg_nf_NorEepInit"

    // TCG_NorEepInit();

    return zOK;
}

//-------------------------------------------------
//  Read Nor Eeprom
//-------------------------------------------------
tcg_code static int tcg_nf_NorEepRd(req_t* req)
{
    // DBG_P(1, 3, 0x82032B);  //82 03 2B, "[F]tcg_nf_NorEepRd"

    // TCG_NorEepRead();

    return zOK;
}

//-------------------------------------------------
//  Write Nor Eeprom
//-------------------------------------------------
tcg_code static int tcg_nf_NorEepWr(req_t* req)
{
    // DBG_P(1, 3, 0x82032C);  //82 03 2C, "[F]tcg_nf_NorEepWr"

    // TCG_NorEepWrite();

    return zOK;
}

//-------------------------------------------------
//  CPU2 TcgInit
//-------------------------------------------------
tcg_code static int tcg_nf_NfCpuInit(req_t* req)
{
    // DBG_P(1, 3, 0x820344);  //82 03 44, "[F]tcg_nf_CPU2TcgInit"

    // tcg_nf_sync_clear();        //clear Tag
    tcgCpu4_init(FALSE, FALSE);         //reset and clear cache

    return zOK;
}

//-------------------------------------------------
//  sync zone51 media
//-------------------------------------------------
tcg_code static int tcg_nf_syncZone51Media(req_t* req)
{
    // DBG_P(1, 3, 0x820344);  //82 03 44, "[F]tcg_nf_CPU2TcgInit"  //alexcheck
    TCGPRN("tcg_nf_syncZone51Media()\n");
    DBG_P(0x01, 0x03, 0x720030 );  // tcg_nf_syncZone51Media()
    sync_zone51_media();
    return zOK;
}

//-------------------------------------------------
//  tcg table recovery if transcetion abort
//-------------------------------------------------
tcg_code static int tcg_nf_tbl_recovery(req_t* req)
{
    tcgMtblChngFlg_t flgs_MChnged = {0};
    U32 errCode = 0;

    flgs_MChnged.all32 = req->op_fields.TCG.param[0];
    TCGPRN("tcg_nf_tbl_recovery() param0[%08x]\n", flgs_MChnged.all32);
    DBG_P(0x02, 0x03, 0x7200ED, 4, flgs_MChnged.all32);  // tcg_nf_tbl_recovery()

    if(flgs_MChnged.b.G1){
        if(tcg_nf_G1Rd(req) == zNG){
            errCode = 0x0100;
        }
    }
    if(flgs_MChnged.b.G2){
        if(tcg_nf_G2Rd(req) == zNG){
            errCode = 0x0200;
        }
    }
    if(flgs_MChnged.b.G3){
        if(tcg_nf_G3Rd(req) == zNG){
            errCode = 0x0300;
        }
    }
    if(flgs_MChnged.b.SMBR){
        if(tcg_nf_TSMBRClear(req) == zNG){
            errCode = 0x0400;
        }
    }
    if(flgs_MChnged.b.DS){
        if(tcg_nf_TDSClear(req) == zNG){
            errCode = 0x0500;
        }
    }
    if(errCode != 0){
        printk("Error!! tcg_nf_tbl_recovery() errCode = %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7F7F4B, 4, errCode);  // Error!! tcg_nf_tbl_recovery() errCode = %08x
        return zNG;
    }
    return zOK;
}


//-------------------------------------------------
//  tcg table update
//-------------------------------------------------
tcg_code static int tcg_nf_tbl_update(req_t* req)
{
    tcgMtblChngFlg_t flgs_MChnged = {0};
    U32 errCode = 0;

    flgs_MChnged.all32 = req->op_fields.TCG.param[0];
    TCGPRN("tcg_nf_tbl_update() param0[%08x]\n", flgs_MChnged.all32);
    DBG_P(0x2, 0x03, 0x7200EE, 4, flgs_MChnged.all32);  // tcg_nf_tbl_update() param0[%08x]

    if(flgs_MChnged.b.G1){
        if(tcg_nf_G1Wr(req) == zNG){
            errCode = 0x0100;
        }
    }
    if(flgs_MChnged.b.G2){
        if(tcg_nf_G2Wr(req) == zNG){
            errCode = 0x0200;
        }
    }
    if(flgs_MChnged.b.G3){
        if(tcg_nf_G3Wr(req) == zNG){
            errCode = 0x0300;
        }
    }
    if(flgs_MChnged.b.SMBR){
        if(tcg_nf_SMBRCommit(req) == zNG){
            errCode = 0x0400;
        }
    }
    if(flgs_MChnged.b.DS){
        if(tcg_nf_DSCommit(req) == zNG){
            errCode = 0x0500;
        }
    }
    if(errCode != 0){
        printk("Error!! tcg_nf_tbl_update() errCode = %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7F7F4F, 4, errCode);  // Error!! tcg_nf_tbl_update() errCode = %08x
        return zNG;
    }
    return zOK;
}

//-------------------------------------------------
//  tcg write G3 with zone51 key
//-------------------------------------------------
tcg_code static int tcg_nf_G3Wr_syncZone51(req_t* req)
{
    U32 errCode = 0;

    printk("tcg_nf_G3Wr_syncZone51\n");
    DBG_P(0x01, 0x03, 0x7200F0 );  // tcg_nf_G3Wr_syncZone51
    #if (_TCG_ != TCG_PYRITE) || (CO_SUPPORT_AES == TRUE)

    CPU4_chg_ebc_key_key();

    if(tcg_nf_G3Wr(req) == zNG){
        errCode = 0x0100;
        goto tcg_nf_G3Wr_syncZone51_exit;
    }

    if(tcg_nf_syncZone51Media(req) == zNG){
        errCode = 0x0200;
        goto tcg_nf_G3Wr_syncZone51_exit;
    }

    #else
    if(tcg_nf_G3Wr(req) == zNG){
        errCode = 0x0300;
        goto tcg_nf_G3Wr_syncZone51_exit;
    }

    #endif

tcg_nf_G3Wr_syncZone51_exit:
    if(errCode != 0){
        printk("Error!! tcg_nf_G3Wr_syncZone51() errCode = %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7200F1, 4, errCode);  // Error!! tcg_nf_G3Wr_syncZone51() errCode = %08x
        return zNG;
    }
    return zOK;
}
#if 0
//-------------------------------------------------
//  TcgTbl DestoryHistory
//-------------------------------------------------
tcg_code static int tcg_nf_TcgTblHistoryDestory(req_t* req)
{
#if TCG_TBL_HISTORY_DESTORY
    U32 i;
    U32 writed_page_cnt;
    // DBG_P(1, 3, 0x82034A);  // 82 03 4A, "[F]tcg_nf_TcgTblDestoryHistory"

    // datastore exist ? if so, how many pages ?
    writed_page_cnt = 0;
    for(i=TCG_DS_LAA_START; i<=TCG_DS_LAA_END; i++){
        if(pG4->b.TcgMbrL2P[i].all != 0xFFFFFFFF) writed_page_cnt++;
    }
    if(writed_page_cnt > 32){
        // DBG_P(2, 3, 0x82034B, 4, writed_page_cnt);  // 82 03 4B, "error! DS writed pages over 32 pages [%08X]" 4
        return zNG;
    }

    // SMBR exist ?  if so, how many pages ?
    writed_page_cnt = 0;
    for(i=TCG_SMBR_LAA_START; i<=TCG_SMBR_LAA_END; i++){
        if(pG4->b.TcgMbrL2P[i].all != 0xFFFFFFFF) writed_page_cnt++;
    }
    if(writed_page_cnt > 32){
        // DBG_P(2, 3, 0x82034C, 4, writed_page_cnt);  // 82 03 4C, ""error! SMBR writed pages over 32 pages [%08X]" 4
        return zNG;
    }

    //-------------G5---------------
    TcgG5NxtPnt.all = 0xFFFFFFFF;   // jump to next block
    tcg_nf_G5RdDefault(req);
    tcg_nf_G5WrDefault(req);
    tcg_nf_G5DmyRd(req);
    tcg_nf_G5DmyWr(req);

    //-------------G4---------------
    TcgG4NxtPnt.all = 0xFFFFFFFF;   // jump to next block
    tcg_nf_G4RdDefault(req);
    tcg_nf_G4WrDefault(req);
    tcg_nf_G1Rd(req);
    tcg_nf_G1Wr(req);
    tcg_nf_G2Rd(req);
    tcg_nf_G2Wr(req);
    tcg_nf_G3Rd(req);
    tcg_nf_G3Wr(req);
    tcg_nf_G4DmyRd(req);
    tcg_nf_G4DmyWr(req);

    //datastore move to new block
    for(i=TCG_DS_LAA_START; i<=TCG_DS_LAA_END; i++){
        if(pG4->b.TcgMbrL2P[i].all != 0xFFFFFFFF){
            tcg_nf_mid_params_set(i, i+1, tcgTmpBuf);
            TCG_G4Rd();
            TCG_G4Wr();
        }
    }


    //SMBR move to new block
    for(i=TCG_SMBR_LAA_START; i<=TCG_SMBR_LAA_END; i++){
        if(pG4->b.TcgMbrL2P[i].all != 0xFFFFFFFF){
            tcg_nf_mid_params_set(i, i+1, tcgTmpBuf);
            TCG_G4Rd();
            TCG_G4Wr();
        }
    }

    TCG_G4HistoryTbl_Destroy();
    TCG_G5HistoryTbl_Destroy();
#endif
    return zOK;
}
#endif

//-------------------------------------------------
//  G4 read retry
//-------------------------------------------------
tcg_code int G4RdRetry(void)
{
    // DBG_P(1, 3, 0x82032D);  //82 03 2D, "[F]G4RdRetry"

    return zNG;
}

//-------------------------------------------------
//  G5 read retry
//-------------------------------------------------
tcg_code int G5RdRetry(void)
{
    // DBG_P(1, 3, 0x82032E);  //82 03 2E, "[F]G5RdRetry"
    return zNG;
}

//===================================================
//
// CPU2 TCG one time initial at Power On
//
//===================================================
#if 0
Error_t TcgRootKey_OnetimeInit(void)
{
    ROOTKEY_STATE_t RootKeyState;
    tSI_TCG_TBL *SyInTcgTbl;

    TCG_PRINTF("TcgRootKey_OnetimeInit|");

    // Get OTP RKEK Tag state
    RootKeyState = TcgRootKey_State_Check();

    switch (RootKeyState)
    {
        case ROOTKEY_NORMAL:
            break;

        case ROOTKEY_CLEAN:
            D_PRINTF("RootKeyClean\n");
            if (TcgRootKey_Generate() == cEcNoError) break;

        case ROOTKEY_CMP_FAIL:
        case ROOTKEY_OTP_ERROR:
        default:
            D_PRINTF("RootKeyInitErr:%x\n", RootKeyState);
            return cEcError;
    }

    // Check Rootkey XTS key and IV on NOR.
    if (TcgRootKey_DynamicIVOneTimeInit())
    {
        D_PRINTF("DynamicIVOneTimeInitErr");
        return cEcError;
    }

    D_PRINTF("Done\n");

    SyInTcgTbl = &smSysInfo->d.Security.d.TcgmTbl;
    if ((SyInTcgTbl->G1.b.mTcgTblInfo.ID == TCG_TBL_ID)
     && (SyInTcgTbl->G2.b.mTcgTblInfo.ID == TCG_TBL_ID)
     && (SyInTcgTbl->G3.b.mTcgTblInfo.ID == TCG_TBL_ID))
    {
        TCG_PRINTF("EncryptMTbl...\n");
        memcpy(&G1, &SyInTcgTbl->G1, sizeof(tSI_TCG_TBL));
        TcgSyInWriteTable(TCG_SYIN_ALL, FALSE);
        TcgSyInTableFlush();
    }

    return cEcNoError;
}
#endif
#if 0  //cjdbg
tcg_code int  TcgInit_NorKek(void)
{
     // DBG_P(1, 3, 0x820350); // 82 03 50, "[F]TcgInit_NorKek"

    //if (TCG_NorKekCrcChk(&mTcgKekDataNor))
    if (TCG_NorKekRead())
    {
        return zNG; // first time used
    }

    // Check if kekInfoNew is NULL or not.
    //if (MemSumU8((uint8_t*)&mTcgKekDataNor.kekInfoNew, sizeof(sTcgKekInfo)) == 0) return cEcNoError;
    if ((mTcgKekDataNor.kekInfoNew.tag==0) && (mTcgKekDataNor.kekInfoCur.tag==ROOTKEY_TAG))
        return zOK;

    //D_PRINTF("DiscoverNewIV");

    // SPOR while updating G3+KEK_NOR, need to check if G3 is properly encrypted by new key or not
    if (mTcgKekDataNor.kekInfoNew.tag == ROOTKEY_TAG)
    {
        // tMSG_TCG* req = (tMSG_TCG*)&smShareMsg[TCG_H2C_MSG_IDX];
        req_t *req = nvmet_get_req();

        if ((tcg_nf_G3Rd(req) == zNG) || (G3.b.mEndTag != TCG_END_TAG))
        { // G3 table NG, change to current key and test again
            memset(&(mTcgKekDataNor.kekInfoNew), 0, sizeof(sTcgKekInfo));
            if ((tcg_nf_G3Rd(req) == zNG) || (G3.b.mEndTag != TCG_END_TAG))
            { // still NG...
                nvmet_put_req(req);
                return zNG;
            }
        }
        else
        { // RootKeyInfoNew is correct, copy to current RootKeyInfoCur.
            memcpy(&(mTcgKekDataNor.kekInfoCur), &(mTcgKekDataNor.kekInfoNew), sizeof(sTcgKekInfo));
        }
        nvmet_put_req(req);
        TCG_NorKekWrite();
    }
    return zOK;
// ]
}
#endif
/* tcg_code void TcgGetKEK(void)
{
    U32 *pt;
    U32 i,j;

    pt = (U32*)tcgKEK;
    // MemCopyU32(pt, (U32 *)&smSysInfo->d.MPInfo.d.SerialNumberPCBA[0], sizeof(smSysInfo->d.MPInfo.d.SerialNumberPCBA)/4);
    memcpy(tcgKEK, &smSysInfo->d.MPInfo.d.SerialNumberPCBA[0], sizeof(smSysInfo->d.MPInfo.d.SerialNumberPCBA));

    TCGPRN("PCBA: %x %x %x %x %x %x %x %x\n", pt[0], pt[1], pt[2], pt[3], pt[4], pt[5], pt[6], pt[7]);
    DBG_P(0x9, 0x03, 0x720031, 4, pt[0], 4, pt[1], 4, pt[2], 4, pt[3], 4, pt[4], 4, pt[5], 4, pt[6], 4, pt[7]);  // PCBA: %x %x %x %x %x %x %x %x
    for(j=0; j<8; j+=4)
        // DBG_P(5,3,0xAC0A04, 4, pt[j], 4,pt[j+1], 4,pt[j+2], 4,pt[j+3]);

    for(i=1; i<=2; i++) {
        pt[0] += i;
        // HAL_Sha256((U8*)pt, 256/8, (U8*)pt);
        sha256((U8*)pt, 256/8, (U8*)pt);
    }

    //Cdbg1S("hashed:");
    //for(j=0; j<8; j+=4)
    //    DBG_P(5,3,0xAC0A04, 4, pt[j], 4,pt[j+1], 4,pt[j+2], 4,pt[j+3]);
}*/

/*
tcg_code void TCG_MRE_Engine(U32 *dest, U32 *src, U32 byteSize, U32 mreDirection, bool bChangeKey)
{
    #ifndef alexcheck
    //U32 org_state;
    U32 *pAesKey, *pIV;

    if (mreDirection==MRE_D_ENCRYPT)
    {
        if (bChangeKey)
        {
            // Use RootKeyInfoNew for encryption
            pIV     = mTcgKekDataNor.kekInfoNew.iv;
            pAesKey = mTcgKekDataNor.kekInfoNew.aesKey;

            HAL_SEC_CollectEntropySamples(mTcgKekDataNor.kekInfoNew.iv, 4);
            HAL_SEC_CollectEntropySamples(mTcgKekDataNor.kekInfoNew.aesKey, 8);
            mTcgKekDataNor.kekInfoNew.tag = ROOTKEY_TAG;
            // DBG_P(2, 3, 0x820351, 4, *(U32*)pAesKey);   //82 03 51, "New KEK: %08x", 4

            //cjdbg, TCG_NorKekWrite();
        }
        else
        {
            pIV     = mTcgKekDataNor.kekInfoCur.iv;
            pAesKey = mTcgKekDataNor.kekInfoCur.aesKey;
        }
    }
    else
    {
        if (mTcgKekDataNor.kekInfoNew.tag == ROOTKEY_TAG)
        {
            pIV     = mTcgKekDataNor.kekInfoNew.iv;
            pAesKey = mTcgKekDataNor.kekInfoNew.aesKey;
        }
        else
        {
            pIV     = mTcgKekDataNor.kekInfoCur.iv;
            pAesKey = mTcgKekDataNor.kekInfoCur.aesKey;
        }
    }

    //BEGIN_MULTI_CS(org_state, cSpinLockDmac)
    BCM_aes_engine(src, dest, pAesKey, (U32*)NULL, pIV, AES_MODE_CBC, mreDirection, byteSize, 256);
    //END_MULTI_CS(org_state, cSpinLockDmac)
    return; // cEcNoError;

    #endif
}
*/
//===================================================
//
//===================================================
tcg_code U16 u16_chk_sum_calc(U16 *p, U16 len)
{
    U16 i, chksum = 0;
    for(i = 0; i < len/sizeof(U16); i++){
        chksum += *(p + i);
    }
    return chksum;
}

//===================================================
//
//===================================================
tcg_code int save_l2pTblAssis(void)
{
    U32 errCode = 0;

    if(tcgL2pAssis.g4.tag != L2P_ASS_TAG){
        errCode = 0x0100;
        goto save_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g4.cell_no != tcgL2pAssis.g4.last_sync_cell_no){
        goto save_immed;
    }

    if(tcgL2pAssis.g4.tbl_idx != tcgL2pAssis.g4.last_sync_tbl_idx){
        goto save_immed;
    }

    if(tcgL2pAssis.g5.tag != L2P_ASS_TAG){
        errCode = 0x0200;
        goto save_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g5.cell_no != tcgL2pAssis.g5.last_sync_cell_no){
        goto save_immed;
    }

    if(tcgL2pAssis.g5.tbl_idx != tcgL2pAssis.g5.last_sync_tbl_idx){
        goto save_immed;
    }
    goto save_l2pTblAssis_exit;

save_immed:
    tcgL2pAssis.g4.last_sync_cell_no = tcgL2pAssis.g4.cell_no;
    tcgL2pAssis.g4.last_sync_tbl_idx = tcgL2pAssis.g4.tbl_idx;
    tcgL2pAssis.g5.last_sync_cell_no = tcgL2pAssis.g5.cell_no;
    tcgL2pAssis.g5.last_sync_tbl_idx = tcgL2pAssis.g5.tbl_idx;
    tcgL2pAssis.chksum = u16_chk_sum_calc((U16*)&tcgL2pAssis.g4, offsetof(tcg_l2p_tbl_assistor_t, chksum));
    TCG_NorEepWrite();

save_l2pTblAssis_exit:
    if(errCode == 0){
        printk("save_l2pTblAssis() ==OK==\n");
        DBG_P(0x01, 0x03, 0x7200DB );  // save_l2pTblAssis() ==OK==
    }else{
        printk("Warnning!! save_l2pTblAssis() errCode= %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7200DC, 4, errCode);  // Warnning!! save_l2pTblAssis() errCode= %08x
    }
    return errCode;
}



tcg_code int chk_l2pTblAssis_accuracy(void)
{
    U32 errCode = 0;
    if(u16_chk_sum_calc((U16*)&tcgL2pAssis.g4, offsetof(tcg_l2p_tbl_assistor_t, chksum)) != tcgL2pAssis.chksum){
        errCode = 0x0100;
        goto chk_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g4.tag != L2P_ASS_TAG){
        errCode = 0x0200;
        goto chk_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g4.cell_no != tcgL2pAssis.g4.last_sync_cell_no){
        errCode = 0x0300;
        goto chk_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g4.tbl_idx != tcgL2pAssis.g4.last_sync_tbl_idx){
        errCode = 0x0400;
        goto chk_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g5.tag != L2P_ASS_TAG){
        errCode = 0x0500;
        goto chk_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g5.cell_no != tcgL2pAssis.g5.last_sync_cell_no){
        errCode = 0x0600;
        goto chk_l2pTblAssis_exit;
    }

    if(tcgL2pAssis.g5.tbl_idx != tcgL2pAssis.g5.last_sync_tbl_idx){
        errCode = 0x0700;
        goto chk_l2pTblAssis_exit;
    }

chk_l2pTblAssis_exit:
    if(errCode == 0){
        printk("chk_l2pTblAssis_accuracy() ==OK==\n");
        DBG_P(0x01, 0x03, 0x7200DD );  // chk_l2pTblAssis_accuracy() ==OK==
    }else{
        printk("Error!! chk_l2pTblAssis_accuracy() errCode= %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7200DE, 4, errCode);  // Error!! chk_l2pTblAssis_accuracy() errCode= %08x
    }
    return errCode;
}

tcg_code void make_l2p_table(tcgLaa_t *l2ptbl, paa2laa_partial_tbl_t *p)
{
    U16 pg;

    #if 0
    for(pg = 0; pg <= tcgL2pAssis.g4.tbl_idx; pg++){
        // pG4->b.TcgMbrL2P[tcgL2pAssis.g4.laa_no[pg]].pc.cell = tcgL2pAssis.g4.cell_no;
        // pG4->b.TcgMbrL2P[tcgL2pAssis.g4.laa_no[pg]].pc.page = pg + L2P_PAGE_CNT;
    }
    #endif

    for(pg = 0; pg <= p->tbl_idx; pg++){
        (l2ptbl + *(p->laa_no + pg))->blk = p->cell_no;
        (l2ptbl + *(p->laa_no + pg))->page = pg + L2P_PAGE_CNT;
    }

}


//===================================================
//
//===================================================
tcg_code int tcg_make_l2p_tbl_no_scan(void)
{
    U32 errCode = 0;

    // --------------- G4 --------------------
    TcgG4Pnt.all = 0;
    TcgG4Pnt.pc.cell = tcgL2pAssis.g4.cell_no;
    if(TCG_G4RdL2p() == zNG){
        errCode = 0x0100;
        goto make_l2p_tbl_exit;
    }

    if(gTcgAux[0].aux0 != TCG_G4_TAG){
        errCode = 0x0200;
        goto make_l2p_tbl_exit;
    }
    TcgG4CurHistNo = gTcgAux[0].aux1;
    TcgG4NxtHistNo = TcgG4CurHistNo + 1;

    make_l2p_table(pG4->b.TcgMbrL2P, &tcgL2pAssis.g4);
    TcgG4NxtPnt.pc.cell = TcgG4Pnt.pc.cell;
    TcgG4NxtPnt.pc.page = tcgL2pAssis.g4.tbl_idx + L2P_PAGE_CNT;
    TcgG4Pnt.all = TcgG4CurPnt.all = TcgG4NxtPnt.all;
    TcgG4NxtPnt.all += SLC_OFF(1);
    if(TcgG4NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){
        TcgG4NxtPnt.all = 0xFFFFFFFF;
    }

    // --------------- G5 --------------------
    TcgG5Pnt.all = 0;
    TcgG5Pnt.pc.cell = tcgL2pAssis.g5.cell_no;
    if(TCG_G5RdL2p() == zNG){
        errCode = 0x0300;
        goto make_l2p_tbl_exit;
    }

    if(gTcgAux[0].aux0 != TCG_G5_TAG){
        errCode = 0x0400;
        goto make_l2p_tbl_exit;
    }
    TcgG5CurHistNo = gTcgAux[0].aux1;
    TcgG5NxtHistNo = TcgG5CurHistNo + 1;

    make_l2p_table(pG5->b.TcgTempMbrL2P, &tcgL2pAssis.g5);
    TcgG5NxtPnt.pc.cell = TcgG5Pnt.pc.cell;
    TcgG5NxtPnt.pc.page = tcgL2pAssis.g5.tbl_idx + L2P_PAGE_CNT;
    TcgG5Pnt.all = TcgG5CurPnt.all = TcgG5NxtPnt.all;
    TcgG5NxtPnt.all += SLC_OFF(1);
    if(TcgG5NxtPnt.pc.page == PAGE_NUM_PER_BLOCK){
        TcgG5NxtPnt.all = 0xFFFFFFFF;
    }

make_l2p_tbl_exit:
    // --- dump ----
    #if 0
    {
        U16 i;
        for(i = 0; i <= tcgL2pAssis.g4.tbl_idx; i++){
            printk("g4.laa_no[%04x] = %04x", i, tcgL2pAssis.g4.laa_no[i]);
            DBG_P(0x3, 0x03, 0x7200E1, 2, i, 2, tcgL2pAssis.g4.laa_no[i]);  // g4.laa_no[%04x] = %04x
        }
        for(i = 0; i <= tcgL2pAssis.g5.tbl_idx; i++){
            printk("g5.laa_no[%04x] = %04x", i, tcgL2pAssis.g5.laa_no[i]);
            DBG_P(0x3, 0x03, 0x7200E2, 2, i, 2, tcgL2pAssis.g5.laa_no[i]);  // g5.laa_no[%04x] = %04x
        }
    }
    #endif
    // --- error checking ---
    if(errCode == 0){
        printk("tcg_make_l2p_tbl_no_scan() ==OK==\n");
        DBG_P(0x01, 0x03, 0x7200DF );  // tcg_make_l2p_tbl_no_scan() ==OK==
    }else{
        printk("Error!! tcg_make_l2p_tbl_no_scan() errCode= %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7200E0, 4, errCode);  // Error!! tcg_make_l2p_tbl_no_scan() errCode= %08x
    }
    return errCode;
}



tcg_code int bufs_allocation(InitBootMode_t initMode)
{
    if (initMode == cInitBootDeepPowerDown)
    {
        pmu_restore(PM_CPU4_TCG_IDX);
        pmu_restore(PM_CPU1_TCG_IDX);    // here, to restore cpu1 variable first. becaue dram variables were backuped only
        tcgL2pAssis.g4.last_sync_cell_no = tcgL2pAssis.g4.cell_no;
        tcgL2pAssis.g4.last_sync_tbl_idx = tcgL2pAssis.g4.tbl_idx;
        tcgL2pAssis.g5.last_sync_cell_no = tcgL2pAssis.g5.cell_no;
        tcgL2pAssis.g5.last_sync_tbl_idx = tcgL2pAssis.g5.tbl_idx;
        tcgL2pAssis.chksum = u16_chk_sum_calc((U16*)&tcgL2pAssis.g4, offsetof(tcg_l2p_tbl_assistor_t, chksum));
    }
    else
    {
        pLockingRangeTable = NULL;
        pG1 = NULL;
        pG2 = NULL;
        pG3 = NULL;
        pG4 = NULL;
        pG5 = NULL;
        DR_G4PaaBuf = NULL;
    }

    if(pG4 == NULL){
        pG4 = MEM_AllocBuffer(LAA_TBL_SZ * 2 + 0x4000, 4096);
        ASM_DMB();
        if (pG4 == NULL){
            TCG_ERR_PRN("error! allocate G4 fail...\n");
            DBG_P(0x01, 0x03, 0x7F7F16);  // error! allocate G4 fail...
            return zNG;
        }
    }

    pG5        = (tG5*)((U8*)pG4 + LAA_TBL_SZ);
    OnePgSzBuf = (U8*)((U8*)pG5 + LAA_TBL_SZ);

    if(tcgTmpBuf == NULL){
        // tcgTmpBuf = MEM_AllocBuffer_SRAM(TCG_TEMP_BUF_SZ, 4096);
        tcgTmpBuf = MEM_AllocBuffer(TCG_TEMP_BUF_SZ, 4096);
        ASM_DMB();
        if (tcgTmpBuf == NULL){
            TCG_ERR_PRN("error! allocate tcgTmpBuf fail...\n");
            DBG_P(0x01, 0x03, 0x7F7F1B);  // error! allocate tcgTmpBuf fail...
            return zNG;
        }
    }

    if(DR_G4PaaBuf  == NULL){
        DR_G4PaaBuf = MEM_AllocBuffer(128 * sizeof(tPAA) * 2, 16);     // allocate 256 tPAA for G4 & G5
        ASM_DMB();
        if(DR_G4PaaBuf == NULL){
            TCG_ERR_PRN("error! allocate DR_G4PaaBuf...\n");
            DBG_P(0x01, 0x03, 0x7F7F41);  // error! allocate DR_G4PaaBuf...
            return zNG;
        }
    }
    #if 0
    if(pG5 == NULL){
         pG5 = MEM_AllocBuffer(LAA_TBL_SZ, 4);
         ASM_DMB();
        if (pG5 == NULL){
            TCG_ERR_PRN("error! allocate G5 fail...\n");
            DBG_P(0x01, 0x03, 0x7F7F17);  // error! allocate G5 fail...
            return zNG;
        }
    }

    if(OnePgSzBuf == NULL){
        OnePgSzBuf = (U8*) MEM_AllocBuffer(0x4000, 4);
        ASM_DMB();
        if(OnePgSzBuf == NULL){
            TCG_ERR_PRN("error! allocate OnePgSzBuf...\n");
            DBG_P(0x01, 0x03, 0x7F7F18);  // error! allocate OnePgSzBuf...
            return zNG;
        }
    }
    #endif
    if(pMBR_TEMPL2PTBL == NULL){
        pMBR_TEMPL2PTBL = (tcgLaa_t*) MEM_AllocBuffer(sizeof(tcgLaa_t) * (MBR_LEN / CFG_UDATA_PER_PAGE), 4);
        ASM_DMB();
        if(pMBR_TEMPL2PTBL == NULL){
            TCG_ERR_PRN("error! allocate MBR_TEMPL2PTBL...\n");
            DBG_P(0x01, 0x03, 0x7F7F19);  // error! allocate MBR_TEMPL2PTBL...
            return zNG;
        }
    }

    if(pDS_TEMPL2PTBL == NULL){
        pDS_TEMPL2PTBL = (tcgLaa_t*) MEM_AllocBuffer(sizeof(tcgLaa_t) * (DATASTORE_LEN / CFG_UDATA_PER_PAGE), 4);
        ASM_DMB();
        if(pDS_TEMPL2PTBL == NULL){
            TCG_ERR_PRN("error! allocate DS_TEMPL2PTBL...\n");
            DBG_P(0x01, 0x03, 0x7F7F1A);  // error! allocate DS_TEMPL2PTBL...
            return zNG;
        }
    }

    tcgTmpBufCnt = TCG_TMP_BUF_ALL_CNT;

    TCGPRN("bufs alloc G4|%x G5|%x tcgtmp|%x MBRL2P|%x DSL2P|%x ONE|%x\n", pG4, pG5, tcgTmpBuf, pMBR_TEMPL2PTBL, pDS_TEMPL2PTBL, OnePgSzBuf);
    DBG_P(0x7, 0x03, 0x720038, 4, pG4, 4, pG5, 4, tcgTmpBuf, 4, pMBR_TEMPL2PTBL, 4, pDS_TEMPL2PTBL, 4, OnePgSzBuf);  // bufs alloc G4|%x G5|%x tcgtmp|%x MBRL2P|%x DSL2P|%x ONE|%x
    return zOK;
}

/***************************************************************
 *  tcg nf one time initial
 ***************************************************************/
tcg_code int tcgCpu4_oneTimeInit(InitBootMode_t initMode)
{
    TCGPRN("tcgCpu4_oneTimeInit()\n");
    DBG_P(0x01, 0x03, 0x720039 );  // tcgCpu4_oneTimeInit()

    bootMode = initMode;
    pLockingRangeTable  = (enabledLockingTable_t *)(btcm_to_dma(mLockingRangeTable));
    #if CO_SUPPORT_AES
    aes_St = TRUE;
    #else
    aes_St = FALSE;
    #endif

    if(pG1 == NULL) pG1 = (tG1 *)&G1;
    if(pG2 == NULL) pG2 = (tG2 *)&G2;
    if(pG3 == NULL) pG3 = (tG3 *)&G3;

    tcg_nf_sync_clear();

    if(pG4 == NULL){
        TCG_ERR_PRN("error!! G4/G5 are NULL...\n");
        DBG_P(0x01, 0x03, 0x7F7F40 );  // error!! G4/G5 are NULL...
        bTcgTblErr = TRUE;
        return zNG;
    }

    TcgSuBlkExist = TRUE;
    if(smSysInfo->d.TCGData.d.TCGBlockTag != SI_MISC_TAG_TCGINFO)
    {
        TCG_ERR_PRN("Error!! FTL didn't offer TCG blk\n");
        DBG_P(0x01, 0x03, 0x7F7F48 );  // Error!! FTL didn't offer TCG blk
        bTcgTblErr = TRUE;
        TcgSuBlkExist = FALSE;
        return zNG;
    }

    U16* blk = smSysInfo->d.TCGData.d.TCGBlockNo;
    TCGPRN("FTL TCG BLK0|%x 1|%x 2|%x 3|%x 4|%x 5|%x 6|%x 7|%x\n", *blk, *(blk+1), *(blk+2), *(blk+3), *(blk+4), *(blk+5),*(blk+6), *(blk+7));
    DBG_P(0x9, 0x03, 0x72003B, 4, *blk, 4, *(blk+1), 4, *(blk+2), 4, *(blk+3), 4,*(blk+4), 4, *(blk+5), 4,*(blk+6), 4, *(blk+7));  // FTL TCG BLK0|%x 1|%x 2|%x 3|%x 4|%x 5|%x 6|%x 7|%x

    if( (strcmp((char *)tcgDefectID, DEFECT_STRING) != 0)                 /*   // first time ? , if so, then init defect table  */
         || (strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0)       /*   // first time ? , if so, then init erased count table */
    ){
        bTcgTblErr = TRUE;   // NOR or NAND table error
        TCGPRN("FW_Dft|%s, EE_Dft|%s, FW_Era|%s, EE_Era|%s\n", DEFECT_STRING, (char *)tcgDefectID, ERASED_CNT_STRING, (char *)tcgErasedCntID);
        DBG_P(0x5, 0x03, 0x72003C, 0xFF, DEFECT_STRING, 0xFF, (char *)tcgDefectID, 0xFF, ERASED_CNT_STRING, 0xFF, (char *)tcgErasedCntID);  // FW_Dft|%s, EE_Dft|%s, FW_Era|%s, EE_Era|%s
        TCG_ERR_PRN("Error!!, Do prformat to paint Defect ID & Erase ID\n");
        DBG_P(0x01, 0x03, 0x7F7F1C);  // Error!!, Do prformat to paint Defect ID & Erase ID
    }else{
        bTcgTblErr = FALSE;
    }

    TCGPRN("tcg_nontcg_switcher|%x, %s\n", tcg_nontcg_switcher, (tcg_nontcg_switcher == TCG_TAG) ? "TCG" : ((tcg_nontcg_switcher == NONTCG_TAG) ? "NONTCG" : "UNSETTING"));
    DBG_P(0x3, 0x03, 0x72003E, 4, tcg_nontcg_switcher, 0xFF, (tcg_nontcg_switcher == TCG_TAG) ? "TCG" : ((tcg_nontcg_switcher == NONTCG_TAG) ? "NONTCG" : "UNSETTING"));  // tcg_nontcg_switcher|%x, %s
    if((tcg_nontcg_switcher != TCG_TAG) && (tcg_nontcg_switcher != NONTCG_TAG)){
        tcg_nontcg_switcher = TCG_TAG;
        SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);
    }

    if(initMode == cInitBootDeepPowerDown){
        DR_G5PaaBuf = &(((tPAA *)DR_G4PaaBuf)[TCG_MBR_CELLS]);   // 0~63-->G4,  64~127-->G5
        tcg_sync.b.nf_ps4_init_req      = TRUE;
        tcg_sync.b.if_ps4_init_resp     = FALSE;
    } else{
        tcg_sync.b.nf_ps4_init_req      = FALSE;
        tcg_sync.b.if_ps4_init_resp     = FALSE;
        tcgCpu4_init(FALSE, FALSE);
    }
    return zOK;
}


tcg_code int tcgCpu4_init(bool mRst, bool bClearCache)
{
    bool updateEE = FALSE;
    req_t req;
    TCGPRN("tcgCpu4_init() req|%x req sz|%x bTcgTblErr|%x\n", (U32)&req, sizeof(req), bTcgTblErr);
    DBG_P(0x4, 0x03, 0x72003F, 4, (U32)&req, 2, sizeof(req), 4, bTcgTblErr);  // tcgCpu4_init() req|%x bTcgTblErr|%x

    if(tcg_sync.b.if_ps4_init_resp == FALSE){
        ASSERT(Build_DefectRemovedPaaMappingTable() != zNG);

        if(check_zone51_blank() == ZONE51_BLANK_TAG){
            new_zone51();
        }else{
            update_zone51_buffer();
        }
    }

    tcg_tblkey_calc((U8*)secretZone.cbcTbl.kek, (U8*)tcgTblkeyBuf);

    gTcgG4Defects       = 0;
    gTcgG5Defects       = 0;

    if(tcg_phy_valid_blk_tbl_tag != 0x54434753){  // "TCGS"
        memcpy(tcg_phy_valid_blk_tbl, DR_G4PaaBuf, sizeof(tcg_phy_valid_blk_tbl));
        memcpy(TCGBlockNo_ee2, smSysInfo->d.TCGData.d.TCGBlockNo, sizeof(TCGBlockNo_ee2));
        tcg_phy_valid_blk_tbl_tag = 0x54434753;   // "TCGS"
        updateEE = TRUE;  // need to update EEPROM before exit function.
    }else{
        if(memcmp(TCGBlockNo_ee2, smSysInfo->d.TCGData.d.TCGBlockNo, sizeof(TCGBlockNo_ee2)) != 0){
            TCG_ERR_PRN("Error!! TCGBlockNo[] != TCGBlockNo_EE[]\n");
            DBG_P(0x01, 0x03, 0x7F7F1D);  // Error!! TCGBlockNo[] != TCGBlockNo_EE[]
            bTcgTblErr = TRUE;
        }

        if(memcmp(tcg_phy_valid_blk_tbl, DR_G4PaaBuf, sizeof(tcg_phy_valid_blk_tbl)) != 0){
            TCG_ERR_PRN("Error!! TCG_PHY_VALID_BLK_TBL != DR_G4PaaBuf\n");
            DBG_P(0x01, 0x03, 0x7F7F1E);  // Error!! TCG_PHY_VALID_BLK_TBL != DR_G4PaaBuf
            bTcgTblErr = TRUE;  // need to update EEPROM before exit function.
        }
    }

    if((tcgDevTyp != TCG_SSC_EDRIVE) && (tcgDevTyp != TCG_SSC_OPAL) && (tcgDevTyp != TCG_SSC_PYRITE) && (tcgDevTyp != TCG_SSC_PYRITE_AES) )
    {
        tcgDevTyp = TCG_DEVICE_TYPE;     //Initial by FW setting if there is no identify tcg type
        updateEE = TRUE;    // need to update EEPROM before exit function.
    }
    //--------- Direct assign TCG type by FW, not by EEPROM  --------
    tcgDevTyp = TCG_DEVICE_TYPE;       //This line will be remarked if assign by EEPROM.
    //--------------------------------------------------------------------------------------------
    // DBG_P(3, 3, 0x82010B, 1, tcgDevTyp, 0xFF, (tcgDevTyp == TCG_SSC_OPAL) ? "OPAL" : ((tcgDevTyp == TCG_SSC_PYRITE) ? "PYRITE" : "PYRITE_AES"));  //82 01 0B, "TCG_TYPE = %X [%s]", 1
    TCGPRN("%s\n", (tcgDevTyp == TCG_SSC_OPAL) ? "OPAL" : ((tcgDevTyp == TCG_SSC_PYRITE) ? "PYRITE" : "PYRITE_AES"));
    DBG_P(0x2, 0x03, 0x720043, 0xFF, (tcgDevTyp == TCG_SSC_OPAL) ? "OPAL" : ((tcgDevTyp == TCG_SSC_PYRITE) ? "PYRITE" : "PYRITE_AES"));  // %s

    //------ check ctable version between FW and EEPROM -----
    if((G1.b.mTcgTblInfo.ID != tcg_cTbl_id) || (G1.b.mTcgTblInfo.ver != tcg_cTbl_ver)) {
        strcpy((char *)tcg_cTbl_idStr , "cID");
        tcg_cTbl_id = G1.b.mTcgTblInfo.ID;     //update ctable to EEPROM
        strcpy((char *)tcg_cTbl_verStr , "Ver");
        tcg_cTbl_ver = G1.b.mTcgTblInfo.ver;   //update ctable to EEPROM
        updateEE = TRUE;    // need to update EEPROM before exit function.
    }

    if(chk_l2pTblAssis_accuracy() == zOK){
        if(tcg_make_l2p_tbl_no_scan() != zOK){
            tcg_nf_G5FTL(&req);
            tcg_nf_G4FTL(&req);
        }
    }else{
        // G5, build FTL ---------------
        tcg_nf_G5FTL(&req);

        // G4, build FTL ---------------
        tcg_nf_G4FTL(&req);
    }

    // build TCG table -----------------
    if(IsG1G2G3DSAllBlank()){
        TCG_ERR_PRN("Error!! G1~G3 are blank.\n");
        DBG_P(0x01, 0x03, 0x7F7F1F);  // Error!! G1~G3 are blank.
        bTcgTblErr = TRUE;
    }else{
        if(IsG1G2G3OneOfBlank()){
            TCG_ERR_PRN("Error!! There is blank one of G1~G3.\n");
            DBG_P(0x01, 0x03, 0x7F7F20);  // Error!! There is blank one of G1~G3.
            bTcgTblErr = TRUE;
        }else{
            if(TCG_BuildTable(&req) == zNG){         // build TCG table
                TCG_ERR_PRN("Error!!, build TBL NG.\n");
                DBG_P(0x01, 0x03, 0x7F7F21);  // Error!!, build TBL NG.
                bTcgTblErr = TRUE;
            }
        }
    }
    // G4, TCG MBR Group -end-------------------------------------------------------------------------------

    //------ check mtable version between FW and EEPROM -----
    if((G1.b.mTcgTblInfo.ID != tcg_mTbl_id) || (G1.b.mTcgTblInfo.ver != tcg_mTbl_ver)){
        strcpy((char *)tcg_mTbl_idStr , "mID");
        tcg_mTbl_id = G1.b.mTcgTblInfo.ID;
        strcpy((char *)tcg_mTbl_verStr , "Ver");
        tcg_mTbl_ver = G1.b.mTcgTblInfo.ver;
        updateEE = TRUE;    // need to update EEPROM before exit function.
    }

    memset(pMBR_TEMPL2PTBL, 0xFF, sizeof(tcgLaa_t) * (MBR_LEN / CFG_UDATA_PER_PAGE));
    memset(pDS_TEMPL2PTBL, 0xFF, sizeof(tcgLaa_t) * (DATASTORE_LEN / CFG_UDATA_PER_PAGE));

/*  // remark , this one keep at CPU0, because cpu4 need to go back Core service loop for writing Zero Block.
    TcgFuncRequest1(MSG_TCG_INIT_CACHE);


    MEM_CLR(req, sizeof(tMSG_TCG));
    req->hdr.b.opCode = cMcTcg;
    req->hdr.b.status = cMsgPosted;
    req->hdr.b.gpCode = cMgHostNorm;
    req->hdr.b.cq     = 0;
    req->hdr.b.opCode = cMcResetCache;
    req->subOpCode    = MSG_TCG_INIT_CACHE;
    req->param[0]     = RST_CACHE_INIT;
    while(Core_ProcessHostResetCache((MsgHostIO_t*) req) != HMSG_CMD_COMPLETE);
*/

#if (MUTI_LBAF == TRUE)
    if(G2.b.mLckLockingInfo_Tbl.val[0].logicalBlockSize != smLogicBlockSize){
        // DBG_P(3, 3, 0x820347, 2, G2.b.mLckLockingInfo_Tbl.val[0].logicalBlockSize, 4, smLogicBlockSize);  //82 03 47, "Multi LBA tblLBA[%04X] smLogicBlockSize[%08X]", 2 4
        G2.b.mLckLockingInfo_Tbl.val[0].logicalBlockSize = smLogicBlockSize;    //LBU_SIZE = smLogicBlockSize
        G2.b.mLckLockingInfo_Tbl.val[0].alignmentGranularity = TCG_AlignmentGranularity;
        // tcg_nf_G2Wr(req);  //alexcheck
    }
#endif

    if(tcg_sync.b.if_ps4_init_resp == FALSE){
        dump_G4_erased_count();
        dump_G5_erased_count();
    }else{
        printk("*tcg PS4 wakeup\n");
        DBG_P(0x01, 0x03, 0x7200E7 );  // @tcg PS4 wakeup
    }

    if((gTcgG4Defects > TCG_MBR_CELLS/2) || (gTcgG5Defects > TCG_MBR_CELLS/2))
    {
        bTcgTblErr = TRUE;
        TCG_ERR_PRN("Error!! There are a lot of defect blocks");
        DBG_P(0x01, 0x03, 0x7F7F22);  // Error!! There are a lot of defect blocks
    }

#if CO_SUPPORT_AES
    Init_Zero_Pattern_Cache();
#endif
    // tcg_nf_post_sync_sign();
    tcg_nf_post_sync_request();

    if(updateEE == TRUE){
        SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);
    }

    save_l2pTblAssis();
    tcg_sync.b.if_ps4_init_resp = FALSE;
    return zOK;
}

tcg_code int  Init_Zero_Pattern_Cache(void)
{
#if CO_SUPPORT_AES
    U32 idx;
    for (idx = 0; idx < LOCKING_RANGE_CNT + 1; idx++){
        HAL_MRE_SecCopy((U32)&smCache[(idx + SSD_TCG_ZERO_START) * MBU_SIZE] >> SSD_ADDR_SHIFT, (U32)&smCache[SSD_TCG_ZERO_END * MBU_SIZE] >> SSD_ADDR_SHIFT, MBU_SIZE, idx, (U32)MRE_D_ENCRYPT);
    }
#endif
    return zOK;
}


tcg_code void tcg_nf_sync_clear(void)
{
    tcg_sync.b.nf_sync_req  = FALSE;
    tcg_sync.b.if_sync_resp = FALSE;
    tcg_sync.b.if_sync_req  = FALSE;
    tcg_sync.b.nf_sync_resp = FALSE;
}

tcg_code void tcg_nf_post_sync_request(void)
{
    tcg_sync.b.nf_sync_req  = TRUE;
    tcg_sync.b.if_sync_resp = FALSE;
}

tcg_code void tcg_nf_post_sync_response(void)
{
    tcg_sync.b.if_sync_req  = FALSE;
    tcg_sync.b.nf_sync_resp = TRUE;
}


tcg_code U32 IsG1Blank(void)
{
    return((pG4->b.TcgMbrL2P[TCG_G1_LAA0].blk) >= (TCG_MBR_CELLS) &&
       (pG5->b.TcgTempMbrL2P[TCG_G1_LAA0].blk) >= (TCG_MBR_CELLS));
}


tcg_code U32 IsG2Blank(void)
{
    return((pG4->b.TcgMbrL2P[TCG_G2_LAA0].blk) >= (TCG_MBR_CELLS) &&
       (pG5->b.TcgTempMbrL2P[TCG_G2_LAA0].blk) >= (TCG_MBR_CELLS) &&
           (pG4->b.TcgMbrL2P[TCG_G2_LAA1].blk) >= (TCG_MBR_CELLS) &&
       (pG5->b.TcgTempMbrL2P[TCG_G2_LAA1].blk) >= (TCG_MBR_CELLS));
}


tcg_code U32 IsG3Blank(void)
{
    return((pG4->b.TcgMbrL2P[TCG_G3_LAA0].blk) >= (TCG_MBR_CELLS) &&
       (pG5->b.TcgTempMbrL2P[TCG_G3_LAA0].blk) >= (TCG_MBR_CELLS) &&
           (pG4->b.TcgMbrL2P[TCG_G3_LAA1].blk) >= (TCG_MBR_CELLS) &&
       (pG5->b.TcgTempMbrL2P[TCG_G3_LAA1].blk) >= (TCG_MBR_CELLS));
}

tcg_code U32 IsDSBlank(void)
{
    return((pG4->b.TcgMbrL2P[TCG_DS_LAA_START].blk) >= (TCG_MBR_CELLS));
}


tcg_code U32 IsG1G2G3DSAllBlank(void)
{
    return(IsG1Blank() && IsG2Blank() && IsG3Blank() && IsDSBlank());
}

tcg_code U32 IsG1G2G3OneOfBlank(void)
{
    return ( (pG4->b.TcgMbrL2P[TCG_G1_LAA0].blk >= (TCG_MBR_CELLS)) || (pG5->b.TcgTempMbrL2P[TCG_G1_LAA0].blk >= (TCG_MBR_CELLS)) ||
             (pG4->b.TcgMbrL2P[TCG_G2_LAA0].blk >= (TCG_MBR_CELLS)) || (pG5->b.TcgTempMbrL2P[TCG_G2_LAA0].blk >= (TCG_MBR_CELLS)) ||
             (pG4->b.TcgMbrL2P[TCG_G2_LAA1].blk >= (TCG_MBR_CELLS)) || (pG5->b.TcgTempMbrL2P[TCG_G2_LAA1].blk >= (TCG_MBR_CELLS)) ||
             (pG4->b.TcgMbrL2P[TCG_G3_LAA0].blk >= (TCG_MBR_CELLS)) || (pG5->b.TcgTempMbrL2P[TCG_G3_LAA0].blk >= (TCG_MBR_CELLS)) ||
             (pG4->b.TcgMbrL2P[TCG_G3_LAA1].blk >= (TCG_MBR_CELLS)) || (pG5->b.TcgTempMbrL2P[TCG_G3_LAA1].blk >= (TCG_MBR_CELLS)) );

}

tcg_code int TCG_G4_NewTable(req_t *req)
{
    // int       j;
    #if CO_SUPPORT_AES
    U8 tmpBuf[sizeof(sWrappedKey)];
    #endif

    //G1 Write====================================================================================================================================================
    TCGPRN("w1\n");
    DBG_P(0x01, 0x03, 0x720048 );  // w1
    // DBG_P(1, 3, 0x82014A);  //82 01 4A, "w1"
    // DBG_P(1, 3, 0x820319);  //82 03 19, "[F]tcg_nf_G1RdDefault"
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0+1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG)  return zNG;
    // tcg_sw_cbc_decrypt(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 decrypt with tcgKEK
    hal_crypto(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G1));   // G1 decrypt with tcgKEK

    // copy Hash(MSID) to SID
    Tcg_GenCPinHash_Cpu4(G1.b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin);
#if TCG_FS_PSID
    TcgPsidRestore_Cpu2();  // PSID restore from EEPROM if PSID had been saved to EEPROM. otherwise PSID is default.
#endif
    // DBG_P(10, 3, 0x82014E, 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[0], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[1], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[2],
                           // 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[3], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[4], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[5],
                           // 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[6], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[7], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[8]);

    // DBG_P(1, 3, 0x820302);  //82 03 02, "[F]tcg_nf_G1Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G1));   // G1 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G1_LAA0, TCG_G1_LAA0+1, tcgTmpBuf);
    if(TCG_G4Wr() == zNG) return zNG;


    //G2 Write====================================================================================================================================================
    TCGPRN("w2\n");
    DBG_P(0x01, 0x03, 0x720049 );  // w2
    // DBG_P(1, 3, 0x82014B);  //82 01 4B, "w2"
    // DBG_P(1, 3, 0x82031A);  //82 03 1A, "[F]tcg_nf_G2RdDefault"
    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG)  return zNG;
    // tcg_sw_cbc_decrypt(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 decrypt with tcgKEK
    hal_crypto(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G2));   // G2 decrypt with tcgKEK

    // DBG_P(1, 3, 0x820306);  //82 03 06, "[F]tcg_nf_G2Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));   // G2 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G2));   // G2 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G2_LAA0, TCG_G2_LAA1+1, tcgTmpBuf);
    if(TCG_G4Wr() == zNG) return zNG;


    //G3 Write====================================================================================================================================================
    TCGPRN("w3\n");
    DBG_P(0x01, 0x03, 0x72004A );  // w3
    // DBG_P(1, 3, 0x82014C);  //82 01 4C, "w3"
    // DBG_P(1, 3, 0x82031B);  //82 03 1B, "[F]tcg_nf_G3RdDefault"
#if CO_SUPPORT_AES
    memcpy(tmpBuf, G3.b.mWKey, sizeof(sWrappedKey));
#endif
    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G4Rd() == zNG)  return zNG;
    // tcg_sw_cbc_decrypt(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));    // G3 decrypt with tcgKEK
    hal_crypto(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G3));    // G3 decrypt with tcgKEK
#if CO_SUPPORT_AES
    //CPU4_changeKey(0);  // only init global range key.
    memcpy(G3.b.mWKey, tmpBuf, sizeof(sWrappedKey));
#endif

    // DBG_P(1, 3, 0x82030A);  //82 03 0A, "[F]tcg_nf_G3Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));  // G3 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G3));  // G3 encrypt with tcgKEK

    #if _TCG_ != TCG_PYRITE
    for(int i = 0; i < (LOCKING_RANGE_CNT + 1); i++){
        // tcg_sw_ebc_encrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[0].dek.aesKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, TRUE, sizeof(G3.b.mWKey[0].dek.aesKey));
        // tcg_sw_ebc_encrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[i].dek.xtsKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, TRUE, sizeof(G3.b.mWKey[i].dek.xtsKey));
    }
    #endif
    // TCG_MRE_Engine((U32*)tcgTmpBuf, (U32*)&G3.all[0],  sizeof(tG3), (U32)MRE_D_ENCRYPT, FALSE);
    //alexcheck
    // memcpy(tcgTmpBuf, G3.all, sizeof(G3));

    tcg_nf_mid_params_set(TCG_G3_LAA0, TCG_G3_LAA1+1, tcgTmpBuf);
    if(TCG_G4Wr() == zNG) return zNG;


    //MBR Write====================================================================================================================================================
    if(tcg_nf_SMBRClear(req) == zNG) return zNG;


    //DSC, G4Dmy Write====================================================================================================================================================
    TCGPRN("w6\n");
    DBG_P(0x01, 0x03, 0x72004B );  // w6
    // DBG_P(1, 3, 0x82014D);  //82 01 4D, "w6"
    if(tcg_nf_DSClear(req) == zNG) return zNG;
    if(tcg_nf_G4DmyWr(req) == zNG) return zNG;

    // *(U32*)pRawKey = 0x6e657762;    // "newb"
    mRawKey[0].state = 0x6e657762;    // "newb"

    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif

    return zOK;
}


tcg_code int TCG_G5_NewTable(req_t *req)
{
    // int       j;

    //G1 Write====================================================================================================================================================
    // DBG_P(1, 3, 0x82014A);  //82 01 4A, "w1"
    // DBG_P(1, 3, 0x820319);  //82 03 19, "[F]tcg_nf_G1RdDefault"
    tcg_nf_mid_params_set(TCG_G1_DEFAULT_LAA0, TCG_G1_DEFAULT_LAA0+1, tcgTmpBuf);
    if(TCG_G5Rd() == zNG) return zNG;
    // tcg_sw_cbc_decrypt(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));  // G1 decrypt with tcgKEK
    hal_crypto(G1.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G1));  // G1 decrypt with tcgKEK

    // copy Hash(MSID) to SID
    Tcg_GenCPinHash_Cpu4(G1.b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin);

//#if TCG_FS_PSID
    TcgPsidRestore_Cpu2();  // PSID restore from EEPROM if PSID had been saved to EEPROM. otherwise PSID is default. Restore PSID in G4 only.
//#endif
    // DBG_P(10, 3, 0x82014E, 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[0], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[1], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[2],
                           // 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[3], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[4], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[5],
                           // 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[6], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[7], 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val[8]);

    // DBG_P(1, 3, 0x820302);  //82 03 02, "[F]tcg_nf_G1Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G1));   // G1 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G1.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G1));   // G1 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G1_LAA0, TCG_G1_LAA0+1, tcgTmpBuf);
    if(TCG_G5Wr() == zNG) return zNG;


    //G2 Write====================================================================================================================================================
    // DBG_P(1, 3, 0x82014B);  //82 01 4B, "w2"
    // DBG_P(1, 3, 0x82031A);  //82 03 1A, "[F]tcg_nf_G2RdDefault"
    tcg_nf_mid_params_set(TCG_G2_DEFAULT_LAA0, TCG_G2_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G5Rd() == zNG) return zNG;
    // tcg_sw_cbc_decrypt(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));    // G2 decrypt with tcgKEK
    hal_crypto(G2.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G2));    // G2 decrypt with tcgKEK

    // DBG_P(1, 3, 0x820306);  //82 03 06, "[F]tcg_nf_G2Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G2));    // G2 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G2.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G2));    // G2 encrypt with tcgKEK
    tcg_nf_mid_params_set(TCG_G2_LAA0, TCG_G2_LAA1+1, tcgTmpBuf);
    if(TCG_G5Wr() == zNG) return zNG;


    //G3 Write====================================================================================================================================================
    // DBG_P(1, 3, 0x82014C);  //82 01 4C, "w3"
    // DBG_P(1, 3, 0x82031B);  //82 03 1B, "[F]tcg_nf_G3RdDefault"
    tcg_nf_mid_params_set(TCG_G3_DEFAULT_LAA0, TCG_G3_DEFAULT_LAA1+1, tcgTmpBuf);
    if(TCG_G5Rd() == zNG) return zNG;
    // tcg_sw_cbc_decrypt(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 decrypt with tcgKEK
    hal_crypto(G3.all, tcgTmpBuf, (U8*)tcgTblkeyBuf, FALSE, sizeof(G3));   // G3 decrypt with tcgKEK
#if CO_SUPPORT_AES
    CPU4_changeKey(0);  // only init global range key.
    // CPU4_chg_cbc_tbl_key();   // Don't add this line because it will make read wrong default table
#endif
    // DBG_P(1, 3, 0x82030A);  //82 03 0A, "[F]tcg_nf_G3Wr"
    // tcg_sw_cbc_encrypt(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, 256, (U8*)secretZone.cbcTbl.iv, sizeof(G3));   // G3 encrypt with tcgKEK
    hal_crypto(tcgTmpBuf, G3.all, (U8*)tcgTblkeyBuf, TRUE, sizeof(G3));   // G3 encrypt with tcgKEK

    #if _TCG_ != TCG_PYRITE
    CPU4_chg_ebc_key_key();
    for(int i = 0; i < (LOCKING_RANGE_CNT + 1); i++){
        // tcg_sw_ebc_encrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[0].dek.aesKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.aesKey, (U8*)secretZone.ebcKey.key, TRUE, sizeof(G3.b.mWKey[0].dek.aesKey));
        // tcg_sw_ebc_encrypt((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, 256, sizeof(G3.b.mWKey[i].dek.xtsKey));
        hal_crypto((U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)((tG3*)tcgTmpBuf)->b.mWKey[i].dek.xtsKey, (U8*)secretZone.ebcKey.key, TRUE, sizeof(G3.b.mWKey[i].dek.xtsKey));
    }
    #endif
    sync_zone51_media();
    // TCG_MRE_Engine((U32*)tcgTmpBuf, (U32*)&G3.all[0],  sizeof(tG3), (U32)MRE_D_ENCRYPT, TRUE);
    //alexcheck
    // memcpy(tcgTmpBuf, G3.all, sizeof(G3));

    tcg_nf_mid_params_set(TCG_G3_LAA0, TCG_G3_LAA1+1, tcgTmpBuf);
    if(TCG_G5Wr() == zNG) return zNG;

    // need to check G5Wr() status or not?
    //cjdbg, TCG_NorKekWriteDone();

    return zOK;
}

tcg_code int TCG_BuildTable(req_t *req)
{
    int j;
    // tMSG_TCG* req = (tMSG_TCG*)&smShareMsg[TCG_H2C_MSG_IDX];

    TCGPRN("TCG_BuildTable()\n");
    DBG_P(0x01, 0x03, 0x72004C );  // TCG_BuildTable()
    // DBG_P(1, 3, 0x820149);  //82 01 49, "[F]TCG_BuildTable"
    //read G1
    if(tcg_nf_G1Rd(req) == zNG) return zNG;   //RW_WaitG1Rd();

    // DBG_P(1, 3, 0x82014F); //82 01 4F, "%X %X %X %X %X %X <- SID", 1 1 1 1 1
    for(j=0; j<6; j++){
        // DBG_P(1, 1, G1.b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val);
    }
#if TCG_FS_PSID
    // DBG_P(1, 3, 0x820150); //82 01 50, "%X %X %X %X %X %X <- PSID", 1 1 1 1 1
    for(j=0; j<6; j++){
        // DBG_P(1, 1, G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val);
    }
    TcgPsidVerify_Cpu4();
#endif
    //read G2
    if(tcg_nf_G2Rd(req) == zNG) return zNG;    //RW_WaitG2Rd();

    //read G3
    #if 0 //cjdbg
    if (TcgInit_NorKek())
    {
        TCG_ERR_PRN("!!Error, TcgInit_NorKek() NG, updating G3...\n");
        DBG_P(0x01, 0x03, 0x7F7F23);  // !!Error, TcgInit_NorKek() NG, updating G3...
        // DBG_P(1, 3, 0x820030);   //82 00 30, "!!!Error, TcgInit_NorKek() NG, updating G3..."
        if(tcg_nf_G3Rd_OldKey(req) == zNG)
            return zNG;    //RW_WaitG3Rd();

        req->op_fields.TCG.param[1] = 0xEE;
        tcg_nf_G3Wr(req);
    }
    else
    #endif
    if (tcg_nf_G3Rd(req) == zNG) return zNG;    //RW_WaitG3Rd();

    //DBG_P(3, 3, 0x82010C, 4, G1.b.mTcgTblInfo.ID, 4, G1.b.mTcgTblInfo.ver);
    //DBG_P(3, 3, 0x82010C, 4, G2.b.mTcgTblInfo.ID, 4, G2.b.mTcgTblInfo.ver);
    //DBG_P(3, 3, 0x82010C, 4, G3.b.mTcgTblInfo.ID, 4, G3.b.mTcgTblInfo.ver);
    #if 0
    chkDefaultTblPattern_cpu4(req);
    #else
    if(chk_tbl_pattern(req) == zNG){
        bTcgTblErr = TRUE;
    }
    #endif

    return zOK;
}

tcg_code void Tcg_GenCPinHash_Cpu4(U8 *pSrc, U8 srcLen, sCPin *pCPin)
{
    //printk("Tcg_GenCPinHash_Cpu4()\n");
    DBG_P(0x01, 0x03, 0x7200D2 );  // Tcg_GenCPinHash_Cpu4()
#if _TCG_DEBUG
    memset((U32 *)pCPin->cPin_salt, 0, sizeof(pCPin->cPin_salt));
#else
    HAL_Gen_Key((U32 *)pCPin->cPin_salt, sizeof(pCPin->cPin_salt));
#endif
    HAL_PBKDF2((U32 *)pSrc,                 // pwd src
                 (U32)srcLen,               // pwd len
                 (U32 *)pCPin->cPin_salt,   // Salt val
                 sizeof(pCPin->cPin_salt),
                 (U32 *)pCPin->cPin_val);   // dest

    // salt | 0 , cpinhash | 416c1030
    //printk("GenCpinHash : %x\n", *(U32 *)pCPin->cPin_val);
    DBG_P(0x2, 0x03, 0x7200D3, 4, *(U32 *)pCPin->cPin_val);  // GenCpinHash : %x
    pCPin->cPin_Tag = CPIN_IN_PBKDF;
}

tcg_code void CPU4_changeKey(U8 idx)
{
#if CO_SUPPORT_AES
    if (idx > TCG_MAX_KEY_CNT)
    {
        TCG_ERR_PRN("!! TcgChangeKey err\n");
        DBG_P(0x01, 0x03, 0x7F7F07);  // !! TcgChangeKey err
        return;
    }
    G3.b.mWKey[idx].nsid = 1;
    G3.b.mWKey[idx].range = idx;
    G3.b.mWKey[idx].state = TCG_KEY_UNWRAPPED;

    HAL_Gen_Key((U32*)&G3.b.mWKey[idx].dek.aesKey, sizeof(G3.b.mWKey[0].dek.aesKey));
    HAL_Gen_Key((U32*)&G3.b.mWKey[idx].dek.xtsKey, sizeof(G3.b.mWKey[0].dek.xtsKey));
    memset(pG3->b.mWKey[idx].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
    memset(pG3->b.mWKey[idx].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
#ifdef KW_DBG
    pG3->b.mWKey[idx].dek.aesKey[0] = (U32)0xAAAA0000 + (U32)0x1111 * idx;
    pG3->b.mWKey[idx].dek.xtsKey[0] = (U32)0xBBBB0000 + (U32)0x1111 * idx;
#endif
    memcpy(&mRawKey[idx].dek, &G3.b.mWKey[idx].dek, sizeof(G3.b.mWKey[0].dek));
    mRawKey[idx].state = G3.b.mWKey[idx].state;

    TCGPRN("range|%x mWkey0|%x-mWkey1|%x\n", idx, G3.b.mWKey[idx].dek.aesKey[0], G3.b.mWKey[idx].dek.xtsKey[0]);
    DBG_P(0x4, 0x03, 0x72004F, 4, idx, 4, G3.b.mWKey[idx].dek.aesKey[0], 4, G3.b.mWKey[idx].dek.xtsKey[0]);  // range|%x mWkey0|%x-mWkey1|%x
    TCGPRN("range|%x Rawkey0|%x-Rawkey1|%x\n", idx, mRawKey[idx].dek.aesKey[0], mRawKey[idx].dek.xtsKey[0]);
    DBG_P(0x4, 0x03, 0x720050, 4, idx, 4, mRawKey[idx].dek.aesKey[0], 4, mRawKey[idx].dek.xtsKey[0]);  // range|%x Rawkey0|%x-Rawkey1|%x
    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif
#endif   //CO_SUPPORT_AES
}


tcg_code int Build_DefectRemovedPaaMappingTable(void)
{
    U16  defmap, zdefmap;
    U16  i = 0, OKIdx = 0;
    tPAA zPaa;
    U8   zlun, zce, zch;

    //allocate for Defect Removed Buffer
    #if 0  // following move to bufs_allocation()
    if(DR_G4PaaBuf  == NULL){
        DR_G4PaaBuf = MEM_AllocBuffer(128 * sizeof(tPAA) * 2, 16);     // allocate 256 tPAA for G4 & G5
    }
    #endif
    if(DR_G4PaaBuf  == NULL){
        DBG_P(0x01, 0x03, 0x7F7F42);  // Error!! DR_G4PaaBuf = NULL, System ASSERT
        return zNG;                                  // allocate fail
    }
    memset(DR_G4PaaBuf, 0, 128 * sizeof(tPAA) * 2);    // clear G4 & G5

    TCGPRN("ch|%x ce|%x lun|%x\n", smNandInfo.geo.nr_channels, smNandInfo.geo.nr_targets, smNandInfo.geo.nr_luns);
    DBG_P(0x4, 0x03, 0x720051, 4, smNandInfo.geo.nr_channels, 4, smNandInfo.geo.nr_targets, 4, smNandInfo.geo.nr_luns);  // ch|%x ce|%x lun|%x
    for(i=0; i<SI_MAX_TCGINFO_CNT; i++){
        if((smSysInfo->d.TCGData.d.TCGBlockNo[i] == WORD_MASK) || (OKIdx >= SI_MAX_TCGINFO_CNT)) break;
        zPaa.all32 = 0;   //clear
        zPaa.b.block = smSysInfo->d.TCGData.d.TCGBlockNo[i];
        for(zPaa.b.lun = 0, zlun = 0; zlun < smNandInfo.geo.nr_luns; zPaa.b.lun++, zlun++){
            for(zPaa.b.ce = 0, zce = 0; zce < smNandInfo.geo.nr_targets; zPaa.b.ce++, zce++){
                defmap = zdefmap = gwDefSuperBlkTbl[FTL_GET_BLK_IDX(zPaa.b.block, zPaa.b.lun, zPaa.b.ce)];
                for(zPaa.b.ch = 0, zch = 0; zch < smNandInfo.geo.nr_channels; zPaa.b.ch++, zch++){
                    if((!(defmap & 0x0001)) && (OKIdx < SI_MAX_TCGINFO_CNT)){   //bit0~ 7 ??plane0??ch0~7??defect??? ????：defect
                        zPaa.b.plane = 0;
                        DR_G4PaaBuf[OKIdx++].all32 = zPaa.all32;  //store to buf
                    }
                    if((!(defmap & 0x0100)) && (OKIdx < SI_MAX_TCGINFO_CNT)){   //bit8~15??plane1??ch0~7??defect??? ????：defect
                        zPaa.b.plane = 1;
                        DR_G4PaaBuf[OKIdx++].all32 = zPaa.all32;  //store to buf
                    }
                    defmap >>= 1;
                }
                #if 1
                TCGPRN("i|%x TCGBlockNo[i]|%x zdefmap|%x OKIdx|%x\n", i, smSysInfo->d.TCGData.d.TCGBlockNo[i], zdefmap, OKIdx);
                DBG_P(0x5, 0x03, 0x720052, 4, i, 4, smSysInfo->d.TCGData.d.TCGBlockNo[i], 4, zdefmap, 4, OKIdx);  // i|%x TCGBlockNo[i]|%x zdefmap|%x OKIdx|%x
                // DBG_P(5, 3, 0x820348, 2, i, 2, smSysInfo->d.TCGData.d.TCGBlockNo[i], 2, zdefmap, 2, OKIdx);   //82 03 48, "SUBLK[%04X]= %04X ,defmap[%04X] OKIdx[%04X]", 2 2 2 2
                #endif
            }
        }
    }

    if((OKIdx * PAGE_NUM_PER_BLOCK * CFG_UDATA_PER_PAGE) < (200 * MBYTE * 2)){  //G4 & G5 should have 400M bytes roughly.
        // DBG_P(2, 3, 0x82001A, 2, OKIdx);      //82 00 1A, "!!!Error, FTL TCG offer super block is not enough [%04X]", 2
        return zNG;
    }

    DR_G5PaaBuf = &(((tPAA *)DR_G4PaaBuf)[TCG_MBR_CELLS]);   // 0~63-->G4,  64~127-->G5
    return zOK;
}

tcg_code int CPinMsidCompare_Cpu4(U8 cpinIdx)
{
    U16 i;
    int result = zOK;

    if(pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_Tag != CPIN_IN_RAW)
    {
        //printk("Encrypted Cpin compare ");
        DBG_P(0x01, 0x03, 0x720053 );  // Encrypted Cpin compare
        U8 digest[CPIN_LENGTH] = { 0 };

        HAL_PBKDF2((U32*)pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val,
                    CPIN_MSID_LEN,
                    (U32*)pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_salt,
                    sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin.cPin_salt),
                    (U32 *)digest);
        //printk("%x | %x\n", pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[0], digest[0]);
        DBG_P(0x3, 0x03, 0x7200D9, 4, pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[0], 4, digest[0]);  // %x | %x
        for (i = 0; i < CPIN_LENGTH; i++)
        {
            if (pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[i] != digest[i])
            {
                result = zNG;
            }
        }
    }
    else
    {
        //printk("Unencrypted Cpin compare ");
        DBG_P(0x01, 0x03, 0x720056 );  // Unencrypted Cpin compare
        for (i = 0; i < CPIN_LENGTH; i++)
        {
            if (pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[i] != pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val[i])
            {
                result = zNG;
            }
        }
    }

    if (result == zNG)
    {
        TCG_ERR_PRN("NG \n");
        DBG_P(0x01, 0x03, 0x7F7F04 );  // NG
    }
    else
    {
        //printk("pass \n");
        DBG_P(0x01, 0x03, 0x720058 );  // pass
    }
    return result;
}

#if TCG_FS_PSID
/* tcg_code int TcgForcePSIDSetToDefault_Cpu2(void)
{
    // DBG_P(1, 3, 0x820225); //82 02 25, "[F]TcgForcePSIDSetToDefault_Cpu2"
    Tcg_GenCPinHash_Cpu4(G1.b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);

    return zOK;
} */

tcg_code int  TcgPsidVerify_Cpu4(void)
{
    if (*(U32*)tcg_ee_Psid == CPIN_IN_PBKDF)
    {   // table vs. EEPROM
        if (memcmp((U8*)tcg_ee_Psid, (U8*)&G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, sizeof(G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin)))
        {
            // DBG_P(1, 3, 0x820026);   // 82 00 26, "!!!Error, PSID tag exist but table PSID != EE PSID"
            //ASSERT(0);
            TCG_ERR_PRN("Error!!, PSID tag exist but table PSID != EE PSID\n");
            DBG_P(0x01, 0x03, 0x7F7F24);  // Error!!, PSID tag exist but table PSID != EE PSID
            bTcgTblErr = TRUE;
        }
    }
    else
    {   // table vs. MSID
        if (CPinMsidCompare_Cpu4(CPIN_PSID_IDX) == zNG){
            // DBG_P(1, 3, 0x820027);   // 82 00 27, "!!!Error, PSID tag doesn't exist but table PSID != MSID"
            TCG_ERR_PRN("Error!!, PSID tag doesn't exist but table PSID != MSID\n");
            DBG_P(0x01, 0x03, 0x7F7F25);  // Error!!, PSID tag doesn't exist but table PSID != MSID
            // for Eric EL Lin requirement to reduce risks.
            // TcgForcePSIDSetToDefault();
            //ASSERT(0);
            bTcgTblErr = TRUE;
        }
    }
    return zOK;
}
#if 0
tcg_code void TcgPsidBackup_Cpu2(void)
{
    // Backup PSID
    if ((bTcgTblErr==FALSE)
     && (G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_Tag==CPIN_IN_PBKDF))
    {
        memcpy((U8*)tcg_ee_Psid, (U8*)&G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, sizeof(G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));
    }
}
#endif
tcg_code void TcgPsidRestore_Cpu2(void)
{
    //printk("TcgPsidRestore_Cpu2()\n");
    DBG_P(0x01, 0x03, 0x7200D4 );  // TcgPsidRestore_Cpu2()
    if (*(U32*)tcg_ee_Psid == CPIN_IN_PBKDF){
        memcpy((U8*)&G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, (U8*)tcg_ee_Psid, sizeof(G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));
#if _TCG_DEBUG
        //printk("\nG1PSID | %x", *(U8*)&G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);
        DBG_P(0x2, 0x03, 0x7200D5, 4, *(U8*)&G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);  // G1PSID | %x
#endif
    }else{
        // DBG_P(1, 3, 0x8201C5);   // 82 01 C5, "= Default PSID ="
        //printk("= Default PSID =\n");
        DBG_P(0x01, 0x03, 0x7200D6 );  // = Default PSID =
    }
}

#endif

tcg_code int BackwardSearch_FTL(U32 tgtPg, tTcgLogAddr curPosIdx, tTcgGrpDef grp)
{
    tTcgLogAddr posidx;
    U32 foundCnt = 0;

    fDBG1(tgtPg);
    posidx.all = curPosIdx.all;
    while(posidx.pc.page >= SLC_OFF(L2P_PAGE_CNT)){
        if(TCG_Rd1Pg(posidx, grp) == zNG){
            return zNG;
        }
        if((pTcgJob->status & 0x0FFF) == FIO_S_NO_ERR){           // 0:ecc ok
            if(tgtPg == gTcgAux[0].aux0){
                if(++foundCnt > 1){
                    if(grp < ZONE_TGRP1){
                        pG4->b.TcgMbrL2P[gTcgAux[0].aux0].all = posidx.all;
                    }else{
                        pG5->b.TcgTempMbrL2P[gTcgAux[0].aux0].all = posidx.all;
                    }
                    // DBG_P(3, 3, 0x820354, 4, gTcgAux[0].aux0, 4, posidx.all);  //82 03 54, "Search Tgt = %08X , Pg Idx = %08X", 4 4
                    return zOK;
                }
            }
        }else if(pTcgJob->status & FIO_S_BLANK_ERR){    // 1:blank
            return zNG;
        }
        posidx.pc.page -= SLC_OFF(1);
    }
    return zNG;
}

tcg_code int chkDefaultTblPattern_cpu4(req_t *req)
{
    sTcgTblInfo *pi;
    TCGPRN("chkDefaultTblPattern_cpu4()\n");
    DBG_P(0x01, 0x03, 0x72005B );  // chkDefaultTblPattern_cpu4()
    // DBG_P(1, 3, 0x820353);  //82 03 53, "[F]chkDefaultTblPattern_cpu4"

    if((G1.b.mTcgTblInfo.ID != TCG_TBL_ID) ||
       (G1.b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)) ||
       (G1.b.mEndTag != TCG_END_TAG))
    {
        pi = &G1.b.mTcgTblInfo;
        TCG_ERR_PRN("Error!!, tbl G1|%x %x FW G1|%x %x\n", pi->ID, pi->ver, TCG_TBL_ID, (TCG_G1_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0B, 4, pi->ID, 4, pi->ver, 4, TCG_TBL_ID, 4, (TCG_G1_TAG + TCG_TBL_VER));  // Error!!, tbl G1|%x %x FW G1|%x %x
        //82 00 11, "!!!Error, G1 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820011, 4, TCG_TBL_ID, 4, G1.b.mTcgTblInfo.ID, 4, (TCG_G1_TAG + TCG_TBL_VER), 4, G1.b.mTcgTblInfo.ver);
        if(BackwardSearch_FTL(TCG_G1_LAA0, TcgG4CurPnt, ZONE_GRP4) == zOK){
            tcg_nf_G1Rd(req);
        }
        return zNG;
    }

    if((G2.b.mTcgTblInfo.ID != TCG_TBL_ID) ||
       (G2.b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)) ||
       (G2.b.mEndTag != TCG_END_TAG))
    {
        pi = &G2.b.mTcgTblInfo;
        TCG_ERR_PRN("Error!!, tbl G2|%x %x FW G2|%x %x\n", pi->ID, pi->ver, TCG_TBL_ID, (TCG_G2_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0C, 4, pi->ID, 4, pi->ver, 4, TCG_TBL_ID, 4, (TCG_G2_TAG + TCG_TBL_VER));  // Error!!, tbl G2|%x %x FW G2|%x %x
        //82 00 12, "!!!Error, G2 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820012, 4, TCG_TBL_ID, 4, G2.b.mTcgTblInfo.ID, 4, (TCG_G2_TAG + TCG_TBL_VER), 4, G2.b.mTcgTblInfo.ver);
        if((BackwardSearch_FTL(TCG_G2_LAA0, TcgG4CurPnt, ZONE_GRP4) == zOK) &&
           ((BackwardSearch_FTL(TCG_G2_LAA1, TcgG4CurPnt, ZONE_GRP4) == zOK))){
            tcg_nf_G2Rd(req);
        }
        return zNG;
    }
    if((G3.b.mTcgTblInfo.ID != TCG_TBL_ID) ||
       (G3.b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)) ||
       (G3.b.mEndTag != TCG_END_TAG))
    {
        pi = &G3.b.mTcgTblInfo;
        TCG_ERR_PRN("Error!!, tbl G3|%x %x FW G3|%x %x\n", pi->ID, pi->ver, TCG_TBL_ID, (TCG_G3_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0D, 4, pi->ID, 4, pi->ver, 4, TCG_TBL_ID, 4, (TCG_G3_TAG + TCG_TBL_VER));  // Error!!, tbl G3|%x %x FW G3|%x %x
        // DBG_P(2, 3, 0x820101, 4, pi); // temporary // alexcheck
        //82 00 21, "!!!Error, G3 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820021, 4, TCG_TBL_ID, 4, G3.b.mTcgTblInfo.ID, 4, (TCG_G3_TAG + TCG_TBL_VER), 4, G3.b.mTcgTblInfo.ver);
        if((BackwardSearch_FTL(TCG_G3_LAA0, TcgG4CurPnt, ZONE_GRP4) == zOK) &&
           ((BackwardSearch_FTL(TCG_G3_LAA1, TcgG4CurPnt, ZONE_GRP4) == zOK))){
            tcg_nf_G3Rd(req);
        }
        return zNG;
    }
    return zOK;
}

tcg_code void chk_G1_pattern(U32 *errCode)
{
    if(G1.b.mTcgTblInfo.ID  != TCG_TBL_ID)                  *errCode |= BIT0;
    if(G1.b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER))  *errCode |= BIT1;
    if(G1.b.mEndTag         != TCG_END_TAG)                 *errCode |= BIT2;
}

tcg_code void chk_G2_pattern(U32 *errCode)
{
    if(G2.b.mTcgTblInfo.ID  != TCG_TBL_ID)                  *errCode |= BIT8;
    if(G2.b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER))  *errCode |= BIT9;
    if(G2.b.mEndTag         != TCG_END_TAG)                 *errCode |= BIT9;
}

tcg_code void chk_G3_pattern(U32 *errCode)
{
    if(G3.b.mTcgTblInfo.ID  != TCG_TBL_ID)                  *errCode |= BIT16;
    if(G3.b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER))  *errCode |= BIT17;
    if(G3.b.mEndTag         != TCG_END_TAG)                 *errCode |= BIT18;
}

tcg_code int chk_tbl_pattern(req_t *req)
{
    U32 errCode = 0, errCode_cpy = 0;  // G1 -> Byte0,  G2 -> Byte1,  G3 -> Byte2
    U32 loop;
    fDBG();
    for(loop = 0; loop < 32; loop++){
        errCode = 0;
        chk_G1_pattern(&errCode);
        chk_G2_pattern(&errCode);
        chk_G3_pattern(&errCode);

        sDBG1(errCode, "tblPtnr err code");
        if(errCode == 0) break;    // OK

        if(errCode & 0x000000FF){  // chk G1
            if(BackwardSearch_FTL(TCG_G1_LAA0, TcgG4CurPnt, ZONE_GRP4) == zNG) break;
            tcg_nf_G1Rd(req);
        }
        if(errCode & 0x0000FF00){  // chk G2
            if((BackwardSearch_FTL(TCG_G2_LAA0, TcgG4CurPnt, ZONE_GRP4) == zNG) ||
               (BackwardSearch_FTL(TCG_G2_LAA1, TcgG4CurPnt, ZONE_GRP4) == zNG)) break;
            tcg_nf_G2Rd(req);
        }
        if(errCode & 0x00FF0000){  // chk G3
            if((BackwardSearch_FTL(TCG_G3_LAA0, TcgG4CurPnt, ZONE_GRP4) == zNG) ||
               (BackwardSearch_FTL(TCG_G3_LAA1, TcgG4CurPnt, ZONE_GRP4) == zNG)) break;
            tcg_nf_G3Rd(req);
        }
        errCode_cpy |= errCode;
    }
    if((errCode == 0) && (loop < 32)){
        if(loop > 0){
            printk("Warnning!! errCode_cpy[%08x] loop[%08x]\n", errCode_cpy, loop);
            DBG_P(0x3, 0x03, 0x7200F2, 4, errCode_cpy, 4, loop);  // Warnning!! errCode_cpy[%08x] loop[%08x]
        }
        return zOK;
    }
    DBG_P(4, 3, 0x7F7F4C, 4, errCode, 4, loop, 4, errCode_cpy);
    return zNG;
}


tcg_code int chkDefaultTblPattern_in_format(void)
{
    if((G1.b.mTcgTblInfo.ID != TCG_TBL_ID)
    || (G1.b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)))
    {
        TCG_ERR_PRN("Error!!, C tbl G1|%x %x FW G1|%x %x\n", G1.b.mTcgTblInfo.ID, G1.b.mTcgTblInfo.ver, TCG_TBL_ID, (TCG_G1_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0E, 4, G1.b.mTcgTblInfo.ID, 4, G1.b.mTcgTblInfo.ver, 4, TCG_TBL_ID, 4, (TCG_G1_TAG + TCG_TBL_VER));  // Error!!, C tbl G1|%x %x FW G1|%x %x
        //82 00 11, "!!!Error, G1 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820011, 4, TCG_TBL_ID, 4, G1.b.mTcgTblInfo.ID, 4, (TCG_G1_TAG + TCG_TBL_VER), 4, G1.b.mTcgTblInfo.ver);
        return zNG;
    }
    else if((G2.b.mTcgTblInfo.ID != TCG_TBL_ID)
         || (G2.b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)))
    {
        TCG_ERR_PRN("Error!!, C tbl G2|%x %x FW G2|%x %x\n", G2.b.mTcgTblInfo.ID, G2.b.mTcgTblInfo.ver, TCG_TBL_ID, (TCG_G2_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0F, 4, G2.b.mTcgTblInfo.ID, 4, G2.b.mTcgTblInfo.ver, 4, TCG_TBL_ID, 4, (TCG_G2_TAG + TCG_TBL_VER));  // Error!!, C tbl G2|%x %x FW G2|%x %x
        //82 00 12, "!!!Error, G2 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820012, 4, TCG_TBL_ID, 4, G2.b.mTcgTblInfo.ID, 4, (TCG_G2_TAG + TCG_TBL_VER), 4, G2.b.mTcgTblInfo.ver);
        return zNG;
    }
    else if((G3.b.mTcgTblInfo.ID != TCG_TBL_ID)
         || (G3.b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)))
    {
        TCG_ERR_PRN("Error!!, C tbl G3|%x %x FW G3|%x %x\n", G3.b.mTcgTblInfo.ID, G3.b.mTcgTblInfo.ver, TCG_TBL_ID, (TCG_G3_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F10, 4, G3.b.mTcgTblInfo.ID, 4, G3.b.mTcgTblInfo.ver, 4, TCG_TBL_ID, 4, (TCG_G3_TAG + TCG_TBL_VER));  // Error!!, C tbl G3|%x %x FW G3|%x %x
        //82 00 21, "!!!Error, G3 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820021, 4, TCG_TBL_ID, 4, G3.b.mTcgTblInfo.ID, 4, (TCG_G3_TAG + TCG_TBL_VER), 4, G3.b.mTcgTblInfo.ver);
        return zNG;
    }
    else if ((G1.b.mEndTag != TCG_END_TAG) || (G2.b.mEndTag != TCG_END_TAG) || (G3.b.mEndTag != TCG_END_TAG))
    {
        TCG_ERR_PRN("Error!!, C tbl End Tag|%x %x %x FW End Tag|%x\n", G1.b.mEndTag, G2.b.mEndTag, G3.b.mEndTag, TCG_END_TAG);
        DBG_P(0x5, 0x03, 0x7F7F11, 4, G1.b.mEndTag, 4, G2.b.mEndTag, 4, G3.b.mEndTag, 4, TCG_END_TAG);  // Error!!, C tbl End Tag|%x %x %x FW End Tag|%x
        // DBG_P(4, 3, 0x820029, 4, G1.b.mEndTag, 4, G2.b.mEndTag, 4, G3.b.mEndTag);
        return zNG;
    }


    //DBG_P(1, 3, 0x820148);  //82 01 48, "Default Tbl OK"
    TCGPRN("Default Tbl OK");
    DBG_P(0x01, 0x03, 0x7200DA );  // Default Tbl OK
    return zOK;
}

tcg_code void dump_G4_defect_amount(void)
{
    U8 i;
    U8 amount = 0;

    for(i = 0; i < TCG_MBR_CELLS; i++){
        if(tcgG4Dft[i] != 0) amount++;
    }
    gTcgG4Defects = amount;
    TCGPRN("G4 defect amount |%x\n", amount);
    DBG_P(0x2, 0x03, 0x720063, 4, amount);  // G4 defect amount |%x
    // DBG_P(2, 3, 0x820146, 1, amount);  //82 01 46, "==> G4 defect amount = %02X" 1
}

tcg_code void dump_G5_defect_amount(void)
{
    U8 i;
    U8 amount = 0;

    for(i = 0; i < TCG_MBR_CELLS; i++){
        if(tcgG5Dft[i] != 0) amount++;
    }
    gTcgG5Defects = amount;
    TCGPRN("G5 defect amount |%x\n", amount);
    DBG_P(0x2, 0x03, 0x720064, 4, amount);  // G5 defect amount |%x
    // DBG_P(2, 3, 0x820147, 1, amount);  //82 01 47, "==> G5 defect amount = %02X" 1
}

tcg_code void DumpTcgKeyInfo(void)
{
#if CO_SUPPORT_AES
    U32 i = 0;

    printk("\n\nmWKey:\n");
    DBG_P(0x01, 0x03, 0x720065 );  // mWKey:
    for(i=0; i<=LOCKING_RANGE_CNT; i++)
    {
        if (pG3->b.mWKey[i].state != TCG_KEY_NULL)
        {
            printk(" [%x] N|%x R|%x s|%x k1|%x icv1|%x k2|%x icv2|%x\n", i,
                                                pG3->b.mWKey[i].nsid,
                                                pG3->b.mWKey[i].range,
                                                pG3->b.mWKey[i].state,
                                                pG3->b.mWKey[i].dek.aesKey[0],
                                                pG3->b.mWKey[i].dek.icv1[0],
                                                pG3->b.mWKey[i].dek.xtsKey[0],
                                                pG3->b.mWKey[i].dek.icv2[0]);
            DBG_P(0x9, 0x03, 0x720066, 4, i, 4,pG3->b.mWKey[i].nsid, 4,pG3->b.mWKey[i].range, 4,pG3->b.mWKey[i].state, 4,pG3->b.mWKey[i].dek.aesKey[0], 4,pG3->b.mWKey[i].dek.icv1[0], 4,pG3->b.mWKey[i].dek.xtsKey[0], 4,pG3->b.mWKey[i].dek.icv2[0]);  //  [%x] N|%x R|%x s|%x k1|%x icv1|%x k2|%x icv2|%x
        }
    }
    //printk("\n\nmRKey:\n");
    DBG_P(0x01, 0x03, 0x720067 );  // mRKey:
    for(i=0; i<=LOCKING_RANGE_CNT; i++)
    {
        if (mRawKey[i].state != TCG_KEY_NULL)
        {
            printk(" [%x] s|%x k1|%x k2|%x\n", i, mRawKey[i].state,
                                            mRawKey[i].dek.aesKey[0],
                                            mRawKey[i].dek.xtsKey[0]);
            DBG_P(0x5, 0x03, 0x720068, 4, i, 4, mRawKey[i].state, 4,mRawKey[i].dek.aesKey[0], 4,mRawKey[i].dek.xtsKey[0]);  //  [%x] s|%x k1|%x k2|%x
        }
    }

    printk("\n\nmOpalKEK\n");
    DBG_P(0x01, 0x03, 0x720069 );  // mOpalKEK
    DBG_P(0x01, 0x03, 0x7100E4 );  // mOpalWrapKEK
    for (i = 0; i <sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); i++)
    {
        if (pG3->b.mOpalWrapKEK[i].state != TCG_KEY_NULL)
        {
            printk(" [%x] s|%x icv|%x kek|%x slt|%x\n",
                pG3->b.mOpalWrapKEK[i].idx,
                pG3->b.mOpalWrapKEK[i].state,
                pG3->b.mOpalWrapKEK[i].icv[0],
                pG3->b.mOpalWrapKEK[i].opalKEK[0],
                pG3->b.mOpalWrapKEK[i].salt[0]);
            DBG_P(0x6, 0x03, 0x72006A, 4,pG3->b.mOpalWrapKEK[i].idx, 4,pG3->b.mOpalWrapKEK[i].state, 4,pG3->b.mOpalWrapKEK[i].icv[0], 4,pG3->b.mOpalWrapKEK[i].opalKEK[0], 4,pG3->b.mOpalWrapKEK[i].salt[0]);  //  [%x] s|%x icv|%x kek|%x slt|%x
        }
    }
#endif  // CO_SUPPORT_AES
}
tcg_code void DumpRangeInfo(void)
{
    U16 i;

    for(i=0; i<LOCKING_RANGE_CNT+1; i++)
    {
        printk("%x R:%x W:%x *%x ~ &%x %x\n",
                pLockingRangeTable[i].rangeNo,
                pLockingRangeTable[i].readLocked,
                pLockingRangeTable[i].writeLocked,
                (U32)pLockingRangeTable[i].rangeStart, (U32)pLockingRangeTable[i].rangeEnd,
                pLockingRangeTable[i].blkcnt);
        DBG_P(0x7, 0x03, 0x72006B, 4,pLockingRangeTable[i].rangeNo, 4,pLockingRangeTable[i].readLocked, 4,pLockingRangeTable[i].writeLocked, 4,(U32)pLockingRangeTable[i].rangeStart, 4, (U32)pLockingRangeTable[i].rangeEnd, 4,pLockingRangeTable[i].blkcnt);  // %x R:%x W:%x *%x ~ &%x %x

        if(pLockingRangeTable[i].rangeNo==0x00)
            break;
    }
}
tcg_code void dump_G4_erased_count(void)
{
#if 1
    U8   i,j;
    U8   cnt;
    U16  erase_cnt[8];

    cnt = 0;
    for(i = 0; i < TCG_MBR_CELLS / 8; i++)
    {
        for(j = 0; j < 8; j++)
        {
            if((tcgG4Dft[i * 8 + j] != 0) || ((i * 8 + j) >= TCG_MBR_CELLS))
            {      // is defect block ?
                erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
            }else
            {
                erase_cnt[j] = tcgG4EraCnt[i * 8 + j];  // show erased count
            }
            cnt++;
        }
        // DBG_P(17, 3, 0x820144, 1, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
        printk("G4 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x\n", i * 8, erase_cnt[0], erase_cnt[1], erase_cnt[2], erase_cnt[3], erase_cnt[4], erase_cnt[5], erase_cnt[6], erase_cnt[7]);
        DBG_P(0xa, 0x03, 0x72006C, 2, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3], 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]);  // G4 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x
    }

    if (TCG_MBR_CELLS - cnt)
    {
        for(j = 0; j < 8; j++)
        {
            if(j < (TCG_MBR_CELLS - cnt)){
                if((tcgG4Dft[i * 8 + j] != 0) || ((i * 8 + j) >= TCG_MBR_CELLS))
                {      // is defect block ?
                    erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
                }else{
                    erase_cnt[j] = tcgG4EraCnt[i * 8 + j];  // show erased count
                }
            }
            else
            {
                erase_cnt[j] = 0;  // show 0x0000 if it is not exist.
            }
        }
        // DBG_P(17, 3, 0x820144, 1, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
        printk("G4 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x\n", i * 8, erase_cnt[0], erase_cnt[1], erase_cnt[2], erase_cnt[3], erase_cnt[4], erase_cnt[5], erase_cnt[6], erase_cnt[7]);
        DBG_P(0xa, 0x03, 0x72006D, 2, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3], 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]);  // G4 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x
    }
    dump_G4_defect_amount();
    // DBG_P(1, 3, 0x820000);  //82 00 00, "----------------------"
#else
    #define MAX_DISPLAY_PER_ROW 8
    U8 i, j;

    for(i = 0; i < (TCG_MBR_CELLS + (MAX_DISPLAY_PER_ROW - 1)) / MAX_DISPLAY_PER_ROW; i++){
        TCGPRN("G4 EraCnt=%2x : ", i*MAX_DISPLAY_PER_ROW);
        DBG_P(0x2, 0x03, 0x72006E, 1, i*MAX_DISPLAY_PER_ROW);  // G4 EraCnt=%2x :
        for(j = 0; j < MAX_DISPLAY_PER_ROW; j++){
            if(j == (MAX_DISPLAY_PER_ROW >> 1)){
                //printk("- ");
                DBG_P(0x01, 0x03, 0x72006F );  // -
            }
            if((i * MAX_DISPLAY_PER_ROW + j) < TCG_MBR_CELLS){
                if(tcgG4Dft[i * MAX_DISPLAY_PER_ROW + j] != 0){      // is defect block ?
                    //printk("FFFFFFFF ");                          // show 0xFFFF if it is defect block
                    DBG_P(0x01, 0x03, 0x720070 );  // FFFFFFFF
                }else{
                    //printk("%4x ", tcgG4EraCnt[i * MAX_DISPLAY_PER_ROW + j]);   // show erased count
                    DBG_P(0x2, 0x03, 0x720071, 2, tcgG4EraCnt[i * MAX_DISPLAY_PER_ROW + j]);  // %4x
                }
            }else{
                break;
            }
        }
        //printk("\n");
        DBG_P(0x01, 0x03, 0x720072 );  //
    }
    dump_G4_defect_amount();
    TCGPRN("---------------------\n");
    DBG_P(0x01, 0x03, 0x720073 );  // ---------------------
    TCGPRN("---------------------\n");  // CA A1 23,       "----------------------"
    DBG_P(0x01, 0x03, 0x720074 );  // ---------------------
#endif
}

tcg_code void dump_G5_erased_count(void)
{
#if 1

    U8   i,j;
    U8   cnt;
    U16  erase_cnt[8];

    cnt = 0;
    for(i = 0; i < TCG_MBR_CELLS / 8; i++)
    {
        for(j = 0; j < 8; j++)
        {
            if((tcgG5Dft[i * 8 + j] != 0) || ((i * 8 + j) >= TCG_MBR_CELLS))  // is defect block ?
            {
                erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
            }else
            {
                erase_cnt[j] = tcgG5EraCnt[i * 8 + j];  // show erased count
            }
            cnt++;
        }
        // DBG_P(17, 3, 0x820145, 1, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
        printk("G5 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x\n", i * 8, erase_cnt[0], erase_cnt[1], erase_cnt[2], erase_cnt[3], erase_cnt[4], erase_cnt[5], erase_cnt[6], erase_cnt[7]);
        DBG_P(0xa, 0x03, 0x720075, 2, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3], 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]);  // G5 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x
    }

    if (TCG_MBR_CELLS - cnt){
        for(j = 0; j < 8; j++)
        {
            if(j < (TCG_MBR_CELLS - cnt)){
                if((tcgG5Dft[i * 8 + j] != 0) || ((i * 8 + j) >= TCG_MBR_CELLS))  // is defect block ?
                {
                    erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
                }else
                {
                    erase_cnt[j] = tcgG5EraCnt[i * 8 + j];  // show erased count
                }
            }else
            {
                erase_cnt[j] = 0;  // show 0x0000 if it is not exist.
            }
        }
        // DBG_P(17, 3, 0x820145, 1, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
        printk("G5 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x\n", i * 8, erase_cnt[0], erase_cnt[1], erase_cnt[2], erase_cnt[3], erase_cnt[4], erase_cnt[5], erase_cnt[6], erase_cnt[7]);
        DBG_P(0xa, 0x03, 0x720076, 2, i * 8, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3], 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]);  // G5 EraCnt %04x: %04x %04x %04x %04x - %04x %04x %04x %04x
    }
    dump_G5_defect_amount();
    TCGPRN("---------------------\n");
    DBG_P(0x01, 0x03, 0x720077 );  // ---------------------
    // DBG_P(1, 3, 0x820000);  //82 00 00, "----------------------"
#else
    #define MAX_DISPLAY_PER_ROW 8
    U8 i, j;

    for(i = 0; i < (TCG_MBR_CELLS + (MAX_DISPLAY_PER_ROW - 1)) / MAX_DISPLAY_PER_ROW; i++){
        TCGPRN("G5 EraCnt=%2x : ", i*MAX_DISPLAY_PER_ROW);
        DBG_P(0x2, 0x03, 0x720078, 1, i*MAX_DISPLAY_PER_ROW);  // G5 EraCnt=%2x :
        for(j = 0; j < MAX_DISPLAY_PER_ROW; j++){
            if(j == (MAX_DISPLAY_PER_ROW >> 1)){
                //printk("- ");
                DBG_P(0x01, 0x03, 0x720079 );  // -
            }
            if((i * MAX_DISPLAY_PER_ROW + j) < TCG_MBR_CELLS){
                if(tcgG5Dft[i * MAX_DISPLAY_PER_ROW + j] != 0){      // is defect block ?
                    //printk("FFFFFFFF ");                          // show 0xFFFF if it is defect block
                    DBG_P(0x01, 0x03, 0x72007A );  // FFFFFFFF
                }else{
                    //printk("%4x ", tcgG5EraCnt[i * MAX_DISPLAY_PER_ROW + j]);   // show erased count
                    DBG_P(0x2, 0x03, 0x72007B, 2, tcgG5EraCnt[i * MAX_DISPLAY_PER_ROW + j]);  // %4x
                }
            }else{
                break;
            }
        }
        //printk("\n");
        DBG_P(0x01, 0x03, 0x72007C );  //
    }
    dump_G5_defect_amount();
    TCGPRN("---------------------\n");  // CA A1 23,       "----------------------"
    DBG_P(0x01, 0x03, 0x72007D );  // ---------------------
#endif
}

/*************************************************************
 * TCG UR function subroutinue
 *
 *************************************************************/
tcg_code tERROR ur_tcg_kill_tables(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 x;
    U32 DefChanMap;
    tTcgLogAddr tblPnt;

    TCGPRN("=erase tcg table=\n");
    DBG_P(0x01, 0x03, 0x72007E );  // =erase tcg table=
    for(tblPnt.all = 0; tblPnt.pc.cell < TCG_MBR_CELLS; tblPnt.pc.cell += DEVICES_NUM_PER_ROW){
        DefChanMap = 0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + tblPnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG4Dft[tblPnt.pc.cell + x] != 0){
                    TCGPRN("G4 dft|%x\n", tblPnt.pc.cell + x);
                    DBG_P(0x2, 0x03, 0x72007F, 4, tblPnt.pc.cell + x);  // G4 dft|%x
                    // DBG_P(2, 3, 0x820331, 1, tblPnt.pc.cell + x);  //82 03 31, "ori DF = %X", 1
                    DefChanMap |= mBIT(x);
                }
            }
        }
        if(argv[0] == 1){  // force erase, don't care defect block
            TCGPRN("Force erase ...\n");
            DBG_P(0x01, 0x03, 0x720080 );  // Force erase ...
            TCG_ErMulChan(ZONE_GRP4, tblPnt, 0);
        }else{
            TCGPRN("skip defect erase ...\n");
            DBG_P(0x01, 0x03, 0x720081 );  // skip defect erase ...
            TCG_ErMulChan(ZONE_GRP4, tblPnt, DefChanMap);
        }

        DefChanMap = 0;
        for(x = 0; x < DEVICES_NUM_PER_ROW; x++){
            if((x + tblPnt.pc.cell) < TCG_MBR_CELLS){   //if TCG_MBR_CELLS is not align at 8
                if(tcgG5Dft[tblPnt.pc.cell + x] != 0){
                    TCGPRN("G5 dft|%x\n", tblPnt.pc.cell + x);
                    DBG_P(0x2, 0x03, 0x720082, 4, tblPnt.pc.cell + x);  // G5 dft|%x
                    // DBG_P(2, 3, 0x820331, 1, tblPnt.pc.cell + x);  //82 03 31, "ori DF = %X", 1
                    DefChanMap |= mBIT(x);
                }
            }
        }
        if(argv[0] == 1){  // force erase, don't care defect block
            TCGPRN("Force erase ...\n");
            DBG_P(0x01, 0x03, 0x720083 );  // Force erase ...
            TCG_ErMulChan(ZONE_GRP5, tblPnt, 0);
        }else{
            TCGPRN("skip defect erase ...\n");
            DBG_P(0x01, 0x03, 0x720084 );  // skip defect erase ...
            TCG_ErMulChan(ZONE_GRP5, tblPnt, DefChanMap);
        }
    }

    TCG_ERR_PRN("Finish, power off immediately, please !!!\n");
    DBG_P(0x01, 0x03, 0x7F7F43 );  // Finish, power off immediately, please !!!
    return EC_NO_ERROR;
}

tcg_code void prepare_zone51_write_data(zone51_t *pzone51)
{
    memset(pzone51, 0, sizeof(zone51_t));

    strcpy((char*)pzone51->sz.cbcTbl.tag, (char*)ZONE51_CBCTBL_TAG);
    memcpy(pzone51->sz.cbcTbl.kek, (U8*)secretZone.cbcTbl.kek, sizeof(secretZone.cbcTbl.kek));
    memcpy(pzone51->sz.cbcTbl.iv,  (U8*)secretZone.cbcTbl.iv,  sizeof(secretZone.cbcTbl.iv));
    pzone51->sz.cbcTbl.writedCnt = secretZone.cbcTbl.writedCnt;

    strcpy((char*)pzone51->sz.ebcKey.tag, (char*)ZONE51_EBCKEY_TAG);
    memcpy(pzone51->sz.ebcKey.key, (U8*)secretZone.ebcKey.key, sizeof(secretZone.ebcKey.key));
    pzone51->sz.ebcKey.writedCnt = secretZone.ebcKey.writedCnt;

    strcpy((char*)pzone51->sz.cbcFwImage.tag, (char*)ZONE51_CBCFWIMAGE_TAG);
    memcpy(pzone51->sz.cbcFwImage.key, (U8*)secretZone.cbcFwImage.key, sizeof(secretZone.cbcFwImage.key));
    memcpy(pzone51->sz.cbcFwImage.iv,  (U8*)secretZone.cbcFwImage.iv,  sizeof(secretZone.cbcFwImage.iv));
    pzone51->sz.cbcFwImage.writedCnt = secretZone.cbcFwImage.writedCnt;

    pzone51->sz.histCnt = ++secretZone.histCnt;
    pzone51->sz.crc = crc32(pzone51, sizeof(SecretZone_t) - 4);
    pzone51->dw.crc = crc32(pzone51, sizeof(zone51_t) - 4);
}

tcg_code void cover_zone51_data(zone51_t *pzone51)
{
    TCGPRN("cover_zone51_data()\n");
    DBG_P(0x01, 0x03, 0x720086 );  // cover_zone51_data()
    memcpy((U8*)secretZone.cbcTbl.kek, pzone51->sz.cbcTbl.kek, sizeof(secretZone.cbcTbl.kek));
    memcpy((U8*)secretZone.cbcTbl.iv,  pzone51->sz.cbcTbl.iv , sizeof(secretZone.cbcTbl.iv));
    secretZone.cbcTbl.writedCnt = pzone51->sz.cbcTbl.writedCnt;

    memcpy((U8*)secretZone.ebcKey.key, pzone51->sz.ebcKey.key, sizeof(secretZone.ebcKey.key));
    secretZone.ebcKey.writedCnt = pzone51->sz.ebcKey.writedCnt;

    memcpy((U8*)secretZone.cbcFwImage.key, pzone51->sz.cbcFwImage.key, sizeof(secretZone.cbcFwImage.key));
    memcpy((U8*)secretZone.cbcFwImage.iv,  pzone51->sz.cbcFwImage.iv,  sizeof(secretZone.cbcFwImage.iv));
    secretZone.cbcFwImage.writedCnt = pzone51->sz.cbcFwImage.writedCnt;

    secretZone.histCnt = pzone51->sz.histCnt;
}

tcg_code void dump_zone51_data(zone51_t *pzone51)
{
    TCGPRN("cbcTbl.tag = %s\n", pzone51->sz.cbcTbl.tag);
    DBG_P(0x2, 0x03, 0x720087, 0xFF, pzone51->sz.cbcTbl.tag);  // cbcTbl.tag = %s
    TCGPRN("cbcTbl.key = %x %x\n", pzone51->sz.cbcTbl.kek[0], pzone51->sz.cbcTbl.kek[1]);
    DBG_P(0x3, 0x03, 0x720088, 4, pzone51->sz.cbcTbl.kek[0], 4, pzone51->sz.cbcTbl.kek[1]);  // cbcTbl.key = %x %x
    TCGPRN("cbcTbl.iv  = %x %x\n", pzone51->sz.cbcTbl.iv[0],  pzone51->sz.cbcTbl.iv[1]);
    DBG_P(0x3, 0x03, 0x720089, 4, pzone51->sz.cbcTbl.iv[0], 4,  pzone51->sz.cbcTbl.iv[1]);  // cbcTbl.iv  = %x %x
    TCGPRN("cbcTbl.writedCnt = %x\n", pzone51->sz.cbcTbl.writedCnt);
    DBG_P(0x2, 0x03, 0x72008A, 4, pzone51->sz.cbcTbl.writedCnt);  // cbcTbl.writedCnt = %x

    TCGPRN("ebcKey.tag = %s\n", pzone51->sz.ebcKey.tag);
    DBG_P(0x2, 0x03, 0x72008B, 0xFF, pzone51->sz.ebcKey.tag);  // ebcKey.tag = %s
    TCGPRN("ebcKey.key = %x %x\n", pzone51->sz.ebcKey.key[0], pzone51->sz.ebcKey.key[1]);
    DBG_P(0x3, 0x03, 0x72008C, 4, pzone51->sz.ebcKey.key[0], 4, pzone51->sz.ebcKey.key[1]);  // ebcKey.key = %x %x
    TCGPRN("ebcKey.writedCnt = %x\n", pzone51->sz.ebcKey.writedCnt);
    DBG_P(0x2, 0x03, 0x72008D, 4, pzone51->sz.ebcKey.writedCnt);  // ebcKey.writedCnt = %x

    TCGPRN("cbcFwImage.tag = %s\n", pzone51->sz.cbcFwImage.tag);
    DBG_P(0x2, 0x03, 0x72008E, 0xFF, pzone51->sz.cbcFwImage.tag);  // cbcFwImage.tag = %s
    TCGPRN("cbcFwImage.key = %x %x\n", pzone51->sz.cbcFwImage.key[0], pzone51->sz.cbcFwImage.key[1]);
    DBG_P(0x3, 0x03, 0x72008F, 4, pzone51->sz.cbcFwImage.key[0], 4, pzone51->sz.cbcFwImage.key[1]);  // cbcFwImage.key = %x %x
    TCGPRN("cbcFwImage.iv  = %x %x\n", pzone51->sz.cbcFwImage.iv[0],  pzone51->sz.cbcFwImage.iv[1]);
    DBG_P(0x3, 0x03, 0x720090, 4, pzone51->sz.cbcFwImage.iv[0], 4,  pzone51->sz.cbcFwImage.iv[1]);  // cbcFwImage.iv  = %x %x
    TCGPRN("cbcFwImage.writedCnt = %x\n", pzone51->sz.cbcFwImage.writedCnt);
    DBG_P(0x2, 0x03, 0x720091, 4, pzone51->sz.cbcFwImage.writedCnt);  // cbcFwImage.writedCnt = %x

    TCGPRN("histCnt = %x\n", pzone51->sz.histCnt);
    DBG_P(0x2, 0x03, 0x720092, 4, pzone51->sz.histCnt);  // histCnt = %x
}

tcg_code int zone51_detail_check(zone51_t *p1)
{
    if(p1->sz.crc != crc32(p1, sizeof(SecretZone_t) - 4)) return 0x020;
    if(p1->dw.crc != crc32(p1, sizeof(zone51_t) - 4)) return 0x030;
    if(strcmp((char*)p1->sz.cbcTbl.tag, (char*)ZONE51_CBCTBL_TAG) != 0) return 0x040;
    if(strcmp((char*)p1->sz.ebcKey.tag, (char*)ZONE51_EBCKEY_TAG) != 0) return 0x050;
    if(strcmp((char*)p1->sz.cbcFwImage.tag, (char*)ZONE51_CBCFWIMAGE_TAG) != 0) return 0x060;
    return zOK;
}

tcg_code int zone51_verify(void *pzone51_pri, void *pzone51_sec)
{
    int errcode = zOK;
    zone51_t *p1 = (zone51_t*)pzone51_pri;
    zone51_t *p2 = (zone51_t*)pzone51_sec;

    TCGPRN("p1|%x %x %x %x %x\n", (U32)p1, *((U32*)p1+0), *((U32*)p1+1), *((U32*)p1+2), *((U32*)p1+3));
    DBG_P(0x6, 0x03, 0x720093, 4, (U32)p1, 4, *((U32*)p1+0), 4, *((U32*)p1+1), 4, *((U32*)p1+2), 4, *((U32*)p1+3));  // p1|%x %x %x %x %x
    TCGPRN("p2|%x %x %x %x %x\n", (U32)p2, *((U32*)p2+0), *((U32*)p2+1), *((U32*)p2+2), *((U32*)p2+3));
    DBG_P(0x6, 0x03, 0x720094, 4, (U32)p2, 4, *((U32*)p2+0), 4, *((U32*)p2+1), 4, *((U32*)p2+2), 4, *((U32*)p2+3));  // p2|%x %x %x %x %x

    if(memcmp(p1, p2, sizeof(zone51_t)) != 0) errcode = 0x010;
    if(!errcode)
        errcode = zone51_detail_check(p1);
    TCGPRN("zone51_verify() errcode|%x\n", errcode);
    DBG_P(0x2, 0x03, 0x720095, 4, errcode);  // zone51_verify() errcode|%x
    return errcode;
}

tcg_code tERROR ur_tcg_zone51_op(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    tPAA        paa;
    uint32_t    i;
    zone51_t    *pzone51 = (zone51_t*)tcgTmpBuf;

    switch(argv[0]){
    case 0:
        TCGPRN("dump TCG_SRB_BLK ...\n");
        DBG_P(0x01, 0x03, 0x720096 );  // dump TCG_SRB_BLK ...
        for(i = 0; i < TCG_PHYBLK_CNT; i++){
            paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
            TCG_RdZone51(paa , (void *)pzone51 + (i * sizeof(zone51_t)));
        }
        if(zone51_verify((void*)pzone51, (void*)pzone51 + sizeof(zone51_t)) == zOK){
            cover_zone51_data(pzone51);
            dump_zone51_data(pzone51);
        }
        break;
    case 1: {
        void *cbcTbl_old = tcgTmpBuf + TCG_TEMP_BUF_SZ - 0x1000;
        void *ebcKey_old = cbcTbl_old + sizeof(secretZone.cbcTbl.kek);
        void *cbcTbl_new = ebcKey_old + sizeof(secretZone.ebcKey.key);
        void *ebcKey_new = cbcTbl_new + sizeof(secretZone.cbcTbl.kek);
        req_t *req = (req_t*)(cbcTbl_old + 0x400);
        if(req == NULL) return EC_ERROR;
        // step1: backup cbcTbl & ebcKey old key
        memcpy(cbcTbl_old, secretZone.cbcTbl.kek, sizeof(secretZone.cbcTbl.kek));
        memcpy(ebcKey_old, secretZone.ebcKey.key, sizeof(secretZone.ebcKey.key));
        // step2: change cbcTbl & ebcKey new key
        CPU4_chg_cbc_tbl_key();
        CPU4_chg_ebc_key_key();
        // step3: write cbcTbl & ebcKey key to Zone51
        TCGPRN("Write default to TCG_SRB_BLK ...\n");
        DBG_P(0x01, 0x03, 0x720097 );  // Write default to TCG_SRB_BLK ...
        prepare_zone51_write_data(pzone51);
        for(i = 0; i < TCG_PHYBLK_CNT; i++){
            paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
            TCG_WrZone51(paa , (void *)pzone51);
        }
        // step4: write G1, G2 & G3
        tcg_nf_G1Wr(req);
        tcg_nf_G2Wr(req);
        tcg_nf_G3Wr(req);
        // step5: backup cbcTbl & ebcKey new key & restore old key
        memcpy(cbcTbl_new, secretZone.cbcTbl.kek, sizeof(secretZone.cbcTbl.kek));
        memcpy(ebcKey_new, secretZone.ebcKey.key, sizeof(secretZone.ebcKey.key));
        memcpy(secretZone.cbcTbl.kek, cbcTbl_old, sizeof(secretZone.cbcTbl.kek));
        memcpy(secretZone.ebcKey.key, ebcKey_old, sizeof(secretZone.ebcKey.key));
        // step6: read default table
        tcg_nf_G4RdDefault(req);
        // step7: restore new key
        memcpy(secretZone.cbcTbl.kek, cbcTbl_new, sizeof(secretZone.cbcTbl.kek));
        memcpy(secretZone.ebcKey.key, ebcKey_new, sizeof(secretZone.ebcKey.key));
        // step8: write default table
        tcg_nf_G4WrDefault(req);
        tcg_nf_G5WrDefault(req);
        // step9: read G1, G2 & G3
        tcg_nf_G1Wr(req);
        tcg_nf_G2Wr(req);
        tcg_nf_G3Wr(req);
        // step10: destory key history
        memset(tcgTmpBuf + TCG_TEMP_BUF_SZ - 0x1000, 0, 0x1000);
        TCGPRN("Wr Finish ...\n");
        DBG_P(0x01, 0x03, 0x720098 );  // Wr Finish ...
        break;
    }
    case 2:
        TCGPRN("erase TCG_SRB_BLK ...\n");
        DBG_P(0x01, 0x03, 0x720099 );  // erase TCG_SRB_BLK ...
        for(i = 0; i < TCG_PHYBLK_CNT; i++){
            paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
            TCG_EraseZone51(paa);
        }
        break;
    }

    return EC_NO_ERROR;
}

tcg_code int sync_zone51_media(void)
{
    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    tPAA        paa;
    uint32_t    i;
    zone51_t    *pzone51 = (zone51_t*)tcgTmpBuf;

    TCGPRN("sync_zone51_media()\n");
    DBG_P(0x01, 0x03, 0x72009A );  // sync_zone51_media()
    // erase
    for(i = 0; i < TCG_PHYBLK_CNT; i++){
        paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
        TCG_EraseZone51(paa);
    }
    // write
    prepare_zone51_write_data(pzone51);
    for(i = 0; i < TCG_PHYBLK_CNT; i++){
        paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
        if(TCG_WrZone51(paa , (void *)pzone51) == zNG){
            return zNG;
        }
    }
    return zOK;
}

tcg_code int new_zone51(void)
{
    TCGPRN("new_zone51()\n");
    DBG_P(0x01, 0x03, 0x72009B );  // ew_zone51()
    secretZone.cbcTbl.writedCnt = 1;
    secretZone.ebcKey.writedCnt = 1;
    secretZone.cbcFwImage.writedCnt = 1;
    secretZone.histCnt = 0;
    CPU4_chg_cbc_tbl_key();
    CPU4_chg_ebc_key_key();
    return sync_zone51_media();
}

tcg_code int update_zone51_buffer(void)
{
    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    tPAA        paa;
    uint32_t    i;
    zone51_t    *pzone51 = (zone51_t*)tcgTmpBuf;
    uint32_t    pass_cnt = 0, blank_cnt = 0;
    uint32_t    keep_pass_num = 0xffffffff;

    for(i = 0; i < TCG_PHYBLK_CNT; i++){
        paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
        if(TCG_RdZone51(paa , (void *)pzone51 + (i * sizeof(zone51_t))) == zNG){
            if(pTcgJob->status & FIO_S_BLANK_ERR) blank_cnt++;
        }else{
            pass_cnt++;
            keep_pass_num = i;
        }
    }

    if(blank_cnt  == TCG_PHYBLK_CNT){
        return ZONE51_BLANK_TAG;
    }

    if(pass_cnt == TCG_PHYBLK_CNT){ // all blocks are pass.
        if(zone51_verify((void*)pzone51, (void*)pzone51 + sizeof(zone51_t)) == zOK){
            cover_zone51_data(pzone51);
            return zOK;
        }
    }else if(pass_cnt > 0){   // one of blocks is pass.
        TCG_ERR_PRN("Error!! one of TCG_SRB_BLKs is bad ...");
        DBG_P(0x01, 0x03, 0x7F7F26);  // Error!! one of TCG_SRB_BLKs is bad ...
        paa = srb_rda_to_paa(srb->tcg_srb_pos[keep_pass_num]);
        TCG_RdZone51(paa , (void *)pzone51);
        if(zone51_detail_check(pzone51) == zOK){
            sync_zone51_media();    // re-write to NAND
            return zOK;
        }
    }

    return zNG;
}

tcg_code int check_zone51_blank()
{
    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    uint32_t    i;
    tPAA        paa;
    uint32_t    pass_cnt = 0, blank_cnt = 0;

    TCG_FIO_F_MUTE = FIO_F_MUTE;
    for(i = 0; i < TCG_PHYBLK_CNT; i++){
        paa = srb_rda_to_paa(srb->tcg_srb_pos[i]);
        if(TCG_Zone51Rd1Pg(paa) == zNG){
            if(pTcgJob->status & FIO_S_BLANK_ERR) blank_cnt++;
        }else{
            pass_cnt++;
        }
    }
    TCG_FIO_F_MUTE = 0;

    if(blank_cnt  == TCG_PHYBLK_CNT){
        return ZONE51_BLANK_TAG;
    }
    if(pass_cnt == TCG_PHYBLK_CNT) return zOK;
    else return zNG;
}

tcg_code void CPU4_chg_cbc_tbl_key(void)
{
    HAL_Gen_Key(secretZone.cbcTbl.kek, sizeof(secretZone.cbcTbl.kek));
}

tcg_code void CPU4_chg_ebc_key_key(void)
{
    HAL_Gen_Key(secretZone.ebcKey.key, sizeof(secretZone.ebcKey.key));
}

tcg_code void CPU4_chg_cbc_fwImage_key(void)
{
    HAL_Gen_Key(secretZone.cbcFwImage.key, sizeof(secretZone.cbcFwImage.key));
}


tcg_code int tcg_backup_before_preformat(void)
{
    req_t *req = MEM_AllocBuffer(sizeof(req_t), 32);
    if(req == NULL){
        TCG_ERR_PRN("Error!! tcg_backup_before_preformat() allocate DDR fail\n");
        DBG_P(0x01, 0x03, 0x7F7F27 );  // Error!! tcg_backup_before_preformat() allocate DDR fail
        return zNG;
    }

    if(tcg_nf_G4RdDefault(req) == zNG){  // read C table
        TCG_ERR_PRN("Error!, read default table fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F28);  // Error!, read default table fail.
        bTcgTblErr = TRUE;
    }

    MEM_FreeBuffer(req);
    return bTcgTblErr ? zNG : zOK;
}

tcg_code int tcg_preformat_and_init(bool IsTcgInit)
{
    req_t *req = MEM_AllocBuffer(sizeof(req_t), 32);
    if(req == NULL){
        TCG_ERR_PRN("Error!! tcg_preformat_and_init() allocate DDR fail\n");
        DBG_P(0x01, 0x03, 0x7F7F29);  // Error!! tcg_preformat_and_init() allocate DDR fail
        return zNG;
    }

    TCGPRN("tcg_preformat_and_init() IsTcgInit|%x\n", IsTcgInit);
    DBG_P(0x2, 0x03, 0x7200A0, 4, IsTcgInit);  // tcg_preformat_and_init() IsTcgInit|%x
    // DBG_P(1, 3, 0x82013B);  //82 01 3B, "[F]TcgPreformatAndInit"
    strcpy((char *)tcg_prefmt_tag, PREFORMAT_START_TAG);

    /*  remove these code to tcg_backup_before_preformat()
    if(tcg_nf_G4RdDefault(req) == zNG){  // read C table
        TCG_ERR_PRN("Error!, read default table fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F2A);  // Error!, read default table fail.
        bTcgTblErr = TRUE;
    }
    */

#ifdef FORCE_TO_CLEAR_ERASED_COUNT
    memset((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));        // force clear G4 erased count
    memset((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));        // force clear G5 erased count
#endif

    if(strcmp((char *)tcgDefectID, DEFECT_STRING) != 0){        // first time ? , if so, then init defect table
        memset((void *)tcgG4Dft, 0, sizeof(tcgG4Dft));          // force clear G4 defect table
        memset((void *)tcgG5Dft, 0, sizeof(tcgG5Dft));          // force clear G5 defect table
        strcpy((char *)tcgDefectID,DEFECT_STRING);              // ID
        TCGPRN("... Defet table is cleared.\n");
        DBG_P(0x01, 0x03, 0x7200A2 );  // ... Defet table is cleared.
        // DBG_P(1, 3, 0x8201C3);  //82 01 C3, ">>> Defect table is cleared."
    }

    if(strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0){ // first time ? , if so, then init erased count table
        memset((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));    // force clear G4 erased count table
        memset((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));    // force clear G5 erased count table
        strcpy((char *)tcgErasedCntID,ERASED_CNT_STRING);       // ID
        TCGPRN("... Erase table is cleared.\n");
        DBG_P(0x01, 0x03, 0x7200A3 );  // ... Erase table is cleared.
        // DBG_P(1, 3, 0x8201C4);  //82 01 C4, ">>> Erase count table is cleared."
    }

    tcg_nf_G4BuildDefect(req);
    dump_G4_defect_amount();

    tcg_nf_G5BuildDefect(req);
    dump_G5_defect_amount();

    if((gTcgG4Defects > TCG_MBR_CELLS / 2) || (gTcgG5Defects > TCG_MBR_CELLS / 2))
    {
        TCGPRN("gTcgG4Defects|%x gTcgG5Defects|%x TCG_MBR_CELLS|%x\n", gTcgG4Defects, gTcgG5Defects, TCG_MBR_CELLS);
        DBG_P(0x4, 0x03, 0x7200A4, 4, gTcgG4Defects, 4, gTcgG5Defects, 4, TCG_MBR_CELLS);  // gTcgG4Defects|%x gTcgG5Defects|%x TCG_MBR_CELLS|%x
        TCG_ERR_PRN("Error!, There are a lot of defect blocks. TCG function off.\n");
        DBG_P(0x01, 0x03, 0x7F7F2B);  // Error!, There are a lot of defect blocks. TCG function off.
        // DBG_P(1, 3, 0x820013);  //82 00 13, "!!!Error, There are a lot of defect blocks. TCG function off."
        bTcgTblErr = TRUE;
        goto quit;
    }

    TCGPRN("Erase EEPROM G4 & G5 Valid Blk Table\n");
    DBG_P(0x01, 0x03, 0x7200A6 );  // Erase EEPROM G4 & G5 Valid Blk Table
    // DBG_P(2, 3, 0x8201E2);  //82 01 E2, "Erase EEPROM G4 & G5 Valid Blk Table"
    memset(tcg_phy_valid_blk_tbl, 0xFF, sizeof(tcg_phy_valid_blk_tbl));
    memset(TCGBlockNo_ee2,        0xFF, sizeof(TCGBlockNo_ee2));
    tcg_phy_valid_blk_tbl_tag = 0xFFFFFFFF;

    strcpy((char *)tcg_prefmt_tag, PREFORMAT_END_TAG);
    SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);

    if(IsTcgInit == TRUE)
    {
        if(chkDefaultTblPattern_in_format() == zOK)
        {
            tcgCpu4_init(TRUE, TRUE);
            // TcgInit_Cpu0(cInitBootPowerDown, TRUE); //reset and clear cache   // alexcheck
        }
        else
        {
            TCG_ERR_PRN("Error!!, Default table error , power off please.\n");
            DBG_P(0x01, 0x03, 0x7F7F08);  // Error!!, Default table error , power off please.
            // DBG_P(1, 3, 0x820014);  //82 00 14, "!!!Error, Default table error, power off."
        }
    }

quit:
    MEM_FreeBuffer(req);
    return zOK;
}


tcg_code U16 be_tcgRangeCheck(U32 lbaStart, U32 lbaEnd, bool writemode)
{
    TCGPRN("be_tcgRangeCheck() mTcgStatus|%x, slba|%x elba|%x wrmode|%x\n", mTcgStatus, lbaStart, lbaEnd, writemode);
    DBG_P(0x5, 0x03, 0x7200A8, 4, mTcgStatus, 4, lbaStart, 4, lbaEnd, 4, writemode);  // be_tcgRangeCheck() mTcgStatus|%x, slba|%x elba|%x wrmode|%x
    if (mTcgStatus & TCG_ACTIVATED) //TCG is activated
    {
        bool   startChecked = FALSE, globalChecked = FALSE;
        U32    i;
        // U32    lbaEnd;

        // lbaEnd = lbaStart + sc - 1;
        if (mTcgStatus & MBR_SHADOW_MODE)
        {
        #if (MUTI_LBAF == TRUE)
            U32 SMBR128M = 0x8000000 / smLogicBlockSize;
        #else
            U32 SMBR128M = 0x40000;
        #endif
            if (lbaEnd >= SMBR128M) //TODO: data '0' for read
            {
                if(lbaStart < SMBR128M)
                {
                    // DBG_P(1, 3, 0x8201C7);  //82 01 C7, "!!MS X"
                    return TCG_DOMAIN_ERROR;
                }
                //else
                //{
                //    if(!writemode)
                //    {     return TCG_DOMAIN_DUMMY;    }
                //}
            }
            else //<128M
            {
                if(writemode)
                {
                    // DBG_P(1, 3, 0x8201C8);  //82 01 C8, "!!MS W"
                    return TCG_DOMAIN_ERROR;
                }
                else
                {
                    // DBG_P(1, 3, 0x8201C9);  //82 01 C9, "**MS"
                    return TCG_DOMAIN_SHADOW;
                }
            }
        }

        for (i = 0; i <= LOCKING_RANGE_CNT; i++)
        {
            if(mLockingRangeTable[i].rangeNo == 0)
            { //Global Range: last row
                if(globalChecked == FALSE)
                {
                    if(((!writemode) && mLockingRangeTable[LOCKING_RANGE_CNT].readLocked) || (writemode && mLockingRangeTable[LOCKING_RANGE_CNT].writeLocked))
                    { //range is read / write locked!
                        // DBG_P(1, 3, 0x8201CA);  //82 01 CA, "!! R0 Lck"
                        if ((!writemode) && (mTcgStatus & MBR_SHADOW_MODE)){
                            return TCG_DOMAIN_DUMMY;
                        }
                        return TCG_DOMAIN_ERROR;
                    }
                    else
                        return TCG_DOMAIN_NORMAL;
                }
                else
                    return TCG_DOMAIN_NORMAL;
            }


            if(startChecked == FALSE)
            { // find startLBA range first
                if(lbaStart <= mLockingRangeTable[i].rangeEnd)
                { //startLBA range is found!
                    startChecked = TRUE;

                    if(lbaStart >= mLockingRangeTable[i].rangeStart)
                    { //startLBA at Tbl[i]
                        if(((!writemode) && mLockingRangeTable[i].readLocked) || (writemode && mLockingRangeTable[i].writeLocked))
                        { //range is read / write locked!
                            // DBG_P(2, 3, 0x8201CB, 1, (U8)mLockingRangeTable[i].rangeNo);  //82 01 CB, "!! R%02X Lck", 1
                            if ((!writemode) && (mTcgStatus & MBR_SHADOW_MODE)){
                                return TCG_DOMAIN_DUMMY;
                            }
                            return TCG_DOMAIN_ERROR;
                        }
                    }

                    else
                    { //startLBA @ global range
                        globalChecked = TRUE;
                        if(((!writemode) && mLockingRangeTable[LOCKING_RANGE_CNT].readLocked) || (writemode && mLockingRangeTable[LOCKING_RANGE_CNT].writeLocked))
                        { //range is read / write locked!
                            // DBG_P(1, 3, 0x8201CC);  //82 01 CC, "!! R00 Lck"
                            return TCG_DOMAIN_ERROR;
                        }
                    }

                    //check if lbaEnd is at this range or not
                    if(lbaEnd <= mLockingRangeTable[i].rangeEnd)
                    { // endLBA range is found!
                        //endChecked = TRUE;

                        if(lbaEnd >= mLockingRangeTable[i].rangeStart)
                        { //endLBA at Tbl[i]
                            if(globalChecked == TRUE)
                            { // startLBA is at global range
                                if(((!writemode) && mLockingRangeTable[i].readLocked) || (writemode && mLockingRangeTable[i].writeLocked))
                                { //range is read / write locked!
                                    // DBG_P(2, 3, 0x8201CD, 1, (U8)mLockingRangeTable[i].rangeNo);  //82 01 CD, "!!@ R%02X Lck", 1
                                    return TCG_DOMAIN_ERROR;
                                }
                                else
                                    return TCG_DOMAIN_NORMAL;

                            }
                            //else startLBA is at Tbl[i] (already checked)
                        }
                        //else    endLBA at global range (already checked)

                        return TCG_DOMAIN_NORMAL;
                    }
                }
            }

        #if 1 //def TCG_EDRIVE
            else //if(endChecked==FALSE)
            { // find endLBA range
                if(globalChecked == FALSE)
                { // check if there is gap between ranges...
                    if ((i > 0) && (mLockingRangeTable[i].rangeStart != (mLockingRangeTable[i-1].rangeEnd + 1)))
                    {
                        globalChecked = TRUE;
                        if(((!writemode) && mLockingRangeTable[LOCKING_RANGE_CNT].readLocked) || (writemode&&mLockingRangeTable[LOCKING_RANGE_CNT].writeLocked))
                        { //range is read / write locked!
                            // DBG_P(1, 3, 0x8201CE);  //82 01 CE, "!!@ R00 Lck"
                            return TCG_DOMAIN_ERROR;
                        }
                    }
                }

                if(lbaEnd>=mLockingRangeTable[i].rangeStart)
                { // endLBA  passed Tbl[i]
                    if(((!writemode) && mLockingRangeTable[i].readLocked)|| (writemode && mLockingRangeTable[i].writeLocked))
                    { //range is read / write locked!
                        // DBG_P(2, 3, 0x8201CF, 1, (U8)mLockingRangeTable[i].rangeNo);  //82 01 CF, "!!# R%02X Lck", 1
                        return TCG_DOMAIN_ERROR;
                    }

                    if(lbaEnd <= mLockingRangeTable[i].rangeEnd)
                    {
                        //endChecked=TRUE;
                        return TCG_DOMAIN_NORMAL;
                    }

                }
            }
        #endif
        }
    }

    return TCG_DOMAIN_NORMAL;
}

///*********************************************
/// ATA read command read SMBR
///*********************************************
tcg_code int tcgReadNormal(U32 sLaa, U32 eLaa, U16 RngSts, U16 btag)
{
    tDTAG dtag;
    U32 sTLaa, eTLaa;  //start TCG LAA, end TCG LAA
    U32 laa;
    int st = zOK;
    U32 idx;

    sTLaa = sLaa / LAA_PER_TCGLAA;
    eTLaa = eLaa / LAA_PER_TCGLAA;

    if (tcgTmpBufCnt != TCG_TMP_BUF_ALL_CNT)
    {
        return zNG;
    }

    TCGPRN("tcgReadNormal() slaa|%x elaa|%x RngSts|%x\n", sTLaa, eTLaa, RngSts);
    DBG_P(0x4, 0x03, 0x7200A9, 4, sTLaa, 4, eTLaa, 4, RngSts);  // tcgReadNormal() slaa|%x elaa|%x RngSts|%x
    // DBG_P(4, 3, 0x8201DA, 2, (U16)slaa, 2, (U16)elaa, 2, RngSts);  //82 01 DA, "[F]tcgReadNormal slaa[%04X] elaa[%04X] RngSts[%04X]", 2 2 2

    SMBR_ioCmdReq = TRUE;

    if(RngSts == TCG_DOMAIN_DUMMY)   // TCG_DOMAIN_DUMMY
    {
        dtag.all32 = UNMAP_DTAG_ID;
        for (laa = sLaa; laa <= eLaa; laa++)
        {
            bm_rd_dtag_commit((laa - sLaa), btag, dtag);
        }

        return zOK;
    }
    else
    {
        req_t req;

        for (idx = sTLaa; idx <= eTLaa; idx++)
        {
            if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + idx].blk < TCG_MBR_CELLS)  //blank ?
            {
                req.op_fields.TCG.laas    = idx;      // laas
                req.op_fields.TCG.laae    = idx + 1;  // laae
                req.op_fields.TCG.pBuffer = (U8 *)tcgTmpBuf + (U32)(idx - sTLaa) * CFG_UDATA_PER_PAGE;;

                if(tcg_nf_SMBRRd(&req) == zOK)
                {
                    dtag.all32 = GET_DTag_FROM_ADDR(req.op_fields.TCG.pBuffer);
                }
                else
                {
                    st = zNG;
                    dtag.all32 = RDERR_DTAG_ID;
                }
            }
            else
            {
                dtag.all32 = UNMAP_DTAG_ID;
            }

            for (laa = (idx * LAA_PER_TCGLAA); laa < ((idx + 1) * LAA_PER_TCGLAA); laa++)
            {
                if ((sLaa <= laa) && (laa <= eLaa))
                {
                    if ((dtag.all32 != RDERR_DTAG_ID) && (dtag.all32 != UNMAP_DTAG_ID))
                    {
                        tcgTmpBufCnt--;
                        // bm_rd_dtag_commit((laa - sLaa), btag, (tDTAG)(dtag.all32 + (laa - sLaa)));
                        bm_rd_dtag_commit((laa - sLaa), btag, (tDTAG)(dtag.all32 + (laa - (idx * LAA_PER_TCGLAA))));
                    }
                    else
                    {
                        bm_rd_dtag_commit((laa - sLaa), btag, dtag);
                    }
                }
            }
        }

        tcgWaitTCGXferDone();

        if (st == zNG)
        {
            //TCGPRN("fail!! rd NAND st = %x\n", st);
            DBG_P(0x2, 0x03, 0x7200AB, 4, st);  // fail!! rd NAND st = %x
        }
    }

    return zOK;
}

///*********************************************
/// ATA read command read SMBR
///*********************************************
tcg_code void tcgWaitTCGXferDone(void)
{
  #if (CPU_ID == CPU_ID4)
    U32 cnt = 0;
    U8 read_wptr;

    while (tcgTmpBufCnt != TCG_TMP_BUF_ALL_CNT)
    {
        read_wptr = (U8)btn_com_free.wptr;

        if (btn_com_free.reg.entry_pnter.b.rptr != read_wptr)
        {
            btn_um_int_sts_t upd = {.all = 0};
            CACHE_Read_XferDone(read_wptr);
            upd.b.com_free_dtag_update = 1;
            btn_writel(upd.all, BTN_UM_INT_STS);
        }

        if (cnt++ > 0x4000)  //prevent host reset
        {
            break;
        }
    }
  #endif
}

/*******************************************************************
 *  dump some info by UR
 *
 *******************************************************************/
#ifdef BCM_test // uart_cmd for bcm test
tcg_code U32 Bcm_Test(U32 argc, U32* argv)
{
    //printk("\nbcm test\n");
    DBG_P(0x01, 0x03, 0x7200AC );  // bcm test

    switch(argv[0]){
        case 0:
            DumpTcgKeyInfo();
            DumpRangeInfo();
            break;
        default:
            //printk("Dump KeyInfo      : 0\n");
            DBG_P(0x01, 0x03, 0x7200AD );  // Dump KeyInfo      : 0
            break;
    }

    return 0;
}
#endif // uart_cmd for dpe test

/***************
 * otp operator
 ***************/
#define COLUMN_CNT      8
#define DOC_REG_GAP     0x100
tcg_code tERROR ur_otp_dump(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 i, j, offset;
    U32 *p;

    for(i = 0; i < (512/sizeof(U32)/COLUMN_CNT); i++){
        for(j = 0; j < COLUMN_CNT; j++){
            offset = (i * COLUMN_CNT + j ) * sizeof(U32) + DOC_REG_GAP;
            *((U32 *)(tcgTmpBuf + (i * COLUMN_CNT + j ) * sizeof(U32))) = read_otp_data(offset);
        }
        p = (U32 *)(tcgTmpBuf + (i * COLUMN_CNT) * sizeof(U32));
        //printk("addr %04x :%08x %08x %08x %08x -  %08x %08x %08x %08x\n", (i * COLUMN_CNT) * sizeof(U32), *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
        DBG_P(0xa, 0x03, 0x7200AE, 2, offset, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // addr %04x :%08x %08x %08x %08x -  %08x %08x %08x %08x
    }
    return 0;
}

tcg_code tERROR ur_otp_write(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 i, offset, data;

    if((argc < 2) || (argc > 9)){
        TCG_ERR_PRN("Wrong arguments amount!!\n");
        DBG_P(0x01, 0x03, 0x7F7F12);  // Wrong arguments amount!!
        return 0;
    }
    offset = argv[0];
    data   = argv[1];
    //printk("argc|%x argv[0]=%x argv[1]=%x argv[2]=%x argv[3]=%x argv[4]=%x argv[5]=%x\n", argc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
    DBG_P(0x8, 0x03, 0x7200B0, 4, argc, 4, argv[0], 4, argv[1], 4, argv[2], 4, argv[3], 4, argv[4], 4, argv[5]);  // argc|%x argv[0]=%x argv[1]=%x argv[2]=%x argv[3]=%x argv[4]=%x argv[5]=%x
    for(i = 1; i < argc; i++){
        data = argv[i];
        if(program_otp_data(data, offset + DOC_REG_GAP) != 0){
            TCG_ERR_PRN("Error!! otp write fail ...\n");
            DBG_P(0x01, 0x03, 0x7F7F13);  // Error!! otp write fail ...
            return 0;
        }
        offset += 4;
    }
    //printk("otp write ok ...\n");
    DBG_P(0x01, 0x03, 0x7200B2 );  // otp write ok ...
    return 0;
}

tcg_code int read_srb_pkey(U32 dtagsIdx[], rda_t rda)
{
    U32      cacheCnt = 0;
    tDTAG_NODE* pHead = NULL;
    tDTAG_NODE* pTail = NULL;
    // U32 dtagsIdx[SRB_MR_DU_CNT_PAGE];
    U32  freeIdx;
    tPAA paa;
    bool ret = TRUE;

    Core_JobCompleteWait(9);
    srb_switch_mr_mode(FIO_F_ENTER_MR_MODE);

    //pop 3 dtag
    pHead   = smCacheMgr.head[DTAG_POOL_DRAM_FREE];
    freeIdx = GET_DTAG_INDEX_FROM_PTR(pHead);

    for(cacheCnt = 0; cacheCnt < SRB_MR_DU_CNT_PAGE; cacheCnt++){
        dtagsIdx[cacheCnt] = freeIdx;
        if(CacheEntry[freeIdx].laa.all32 != DWORD_MASK){
            CACHE_Delete(freeIdx);
            CacheEntry[freeIdx].laa.all32 = DWORD_MASK;
          #if (WUNC_ENABLE == TRUE)
            if (WUNCError_CHECK(freeIdx))
            {
                WUNCError_BitClr(freeIdx);
                WUNCBitMapEntry[freeIdx] = WUNC_BMP_ALL_UNMARK;
            }
          #endif
        }
        CacheEntry[freeIdx].cache_LL_Map.bitmap = CACHE_BMP_ALL_INVALID;
        pTail    = &srmDTAG[freeIdx];
        freeIdx = pTail->nextIndex;
    }
    CACHE_PopDTag(DTAG_POOL_DRAM_FREE, pHead, pTail, SRB_MR_DU_CNT_PAGE);

    paa = srb_rda_to_paa(rda);
    ret = srb_read_mr(paa, dtagsIdx);

    CACHE_PushDTag(DTAG_POOL_DRAM_FREE, pHead, pTail, SRB_MR_DU_CNT_PAGE, FALSE);
    srb_switch_mr_mode(FIO_F_LEAVE_MR_MODE);
    if(ret == FALSE){
        return zNG;
    }
    return zOK;
}

extern U32 *pkey, *sha3Buf;
tcg_code void* get_pkey_digest(void *src, U32 len)
{
    #if 0
    void *srcbuf;
    srcbuf = MEM_AllocBuffer_SRAM(512 + 64, 32);
    if(srcbuf == NULL){
        //printk("get_pkey_digest() memory allocate fail ...\n");
        DBG_P(0x01, 0x03, 0x7200B3 );  // get_pkey_digest() memory allocate fail ...
        return NULL;
    }
    memcpy(srcbuf, src, len);
    bm_sha3_sm3_calc_part(srcbuf, srcbuf + 32, FALSE, len, len, NULL, NULL, TRUE);
    memcpy(tcgTmpBuf, srcbuf + 32, 32);
    MEM_FreeBuffer_SRAM(srcbuf);
    #else
    memcpy((void *)pkey, src, len);
    bm_sha3_sm3_calc_part((void *)pkey, (void *)sha3Buf, FALSE, len, len, NULL, NULL, TRUE);
    memcpy(tcgTmpBuf, (void *)sha3Buf, 32);
    #endif
    return tcgTmpBuf;
}

tcg_code void pkey_dump(srb_t *srb, rda_t rda, void *pkey_buf)
{
    int i, j, st;
    int pkey_du_cnt = (srb->srb_hdr.srb_pub_key_len + SRB_MR_DU_SZE) / SRB_MR_DU_SZE;
    U32 dtagsIdx[SRB_MR_DU_CNT_PAGE];
    U32 *mem;
    U32 idx;

    st = read_srb_pkey(dtagsIdx, rda);
    if(st == zNG){
        TCG_ERR_PRN("Error!!, Row(0x%02x)@CH/CE(%02d/%02d) fail", rda.row, rda.ch, rda.dev);
        DBG_P(0x4, 0x03, 0x7F7F2E, 1, rda.row, 1, rda.ch, 1, rda.dev);  // Error!!, Row(0x%02x)@CH/CE(%02d/%02d) fail
        return;
    }

    for (i = 0; i < pkey_du_cnt; i += SRB_MR_DU_CNT_PAGE) {
        for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
            mem = (U32*)(GET_ADDR_FROM_DTagIdx(dtagsIdx[j]));
            //printk("===== DU%2x (len = %04x)=====\n", j, srb->srb_hdr.srb_pub_key_len);
            DBG_P(0x3, 0x03, 0x7200B5, 1, j, 2, srb->srb_hdr.srb_pub_key_len);  // ===== DU%2x (len = %04x)=====
            for (idx = 0; idx < srb->srb_hdr.srb_pub_key_len / 16; idx++) {
                //printk("PKEY %08x: %08x_%08x_%08x_%08x\n", idx * 16, mem[0 + idx * 4], mem[1 + idx * 4], mem[2 + idx * 4], mem[3 + idx * 4]);
                DBG_P(0x6, 0x03, 0x7200B6, 4, idx * 16, 4, mem[0 + idx * 4], 4, mem[1 + idx * 4], 4, mem[2 + idx * 4], 4, mem[3 + idx * 4]);  // PKEY %08x: %08x_%08x_%08x_%08x
            }
            if(j == 0){
                memcpy(pkey_buf, mem, srb->srb_hdr.srb_pub_key_len);
                U32 *p = get_pkey_digest(mem, srb->srb_hdr.srb_pub_key_len);
                if(p == NULL){
                    break;
                }
                //printk("pkey digest = %08x %8x %08x %8x - %08x %8x %08x %8x\n", *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
                DBG_P(0x9, 0x03, 0x7200B7, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // pkey digest = %08x %8x %08x %8x - %08x %8x %08x %8x
                memcpy((U8*)pkey_buf + srb->srb_hdr.srb_pub_key_len, (U8*)p, 32); // copy pkey digest
            }
        }
    }
}

tcg_code int chk_all_rows_pkey_unity(void)
{
    bool f_same = FALSE;

    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    rda_t srb_hdr_rda, srb_hdr_rda_mirror;

    srb_hdr_rda = srb->dftb_pos;
    srb_hdr_rda_mirror = srb->dftb_m_pos;

    //printk("pkey content: (row = 0x%08x)\n", srb->srb_hdr.srb_pub_key);
    DBG_P(0x2, 0x03, 0x7200B8, 4, srb->srb_hdr.srb_pub_key);  // pkey content: (row = 0x%08x)

    srb_hdr_rda.row = srb->srb_hdr.srb_pub_key;
    pkey_dump(srb, srb_hdr_rda, tcgTmpBuf + 0x400);

    //printk("pkey_dual content: (row = 0x%08x)\n", srb->srb_hdr.srb_pub_key_dual);
    DBG_P(0x2, 0x03, 0x7200B9, 4, srb->srb_hdr.srb_pub_key_dual);  // pkey_dual content: (row = 0x%08x)

    srb_hdr_rda.row = srb->srb_hdr.srb_pub_key_dual;
    pkey_dump(srb, srb_hdr_rda, tcgTmpBuf + 0x800);

    //printk("pkey_mirror content: (row = 0x%08x)\n", srb->srb_hdr.srb_pub_key_mirror);
    DBG_P(0x2, 0x03, 0x7200BA, 4, srb->srb_hdr.srb_pub_key_mirror);  // pkey_mirror content: (row = 0x%08x)

    srb_hdr_rda_mirror.row = srb->srb_hdr.srb_pub_key_mirror;
    pkey_dump(srb, srb_hdr_rda_mirror, tcgTmpBuf + 0xC00);

    //printk("pkey_dual_mirror content: (row = 0x%08x)\n", srb->srb_hdr.srb_pub_key_dual_mirror);
    DBG_P(0x2, 0x03, 0x7200BB, 4, srb->srb_hdr.srb_pub_key_dual_mirror);  // pkey_dual_mirror content: (row = 0x%08x)

    srb_hdr_rda_mirror.row = srb->srb_hdr.srb_pub_key_dual_mirror;
    pkey_dump(srb, srb_hdr_rda_mirror, tcgTmpBuf + 0x1000);

    // Are all of pkeys same ?  pkey + pkey digest
    if(memcmp(tcgTmpBuf+0x400, tcgTmpBuf+0x800, srb->srb_hdr.srb_pub_key_len + 32) == 0){
        if(memcmp(tcgTmpBuf+0xC00, tcgTmpBuf+0x1000, srb->srb_hdr.srb_pub_key_len + 32) == 0){
            if(memcmp(tcgTmpBuf+0x400, tcgTmpBuf+0x1000, srb->srb_hdr.srb_pub_key_len + 32) == 0){
                //printk("YA!! all of pkeys are same.");
                DBG_P(0x01, 0x03, 0x7200BC );  // YA!! all of pkeys are same.
                f_same = TRUE;
            }
        }
    }

    if(f_same == FALSE){
        //printk("one of pkeys is differet, the pkey can't be trust ...");
        DBG_P(0x01, 0x03, 0x7200BD );  // one of pkeys is differet, the pkey can't be trust ...
        return zNG;
    }

    return zOK;
}

tcg_code tERROR ur_pkey_dump(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    chk_all_rows_pkey_unity();
    return 0;
}


extern int save_pub_key_to_otp(U32 *pub_key, U32 key_len, bool first);
tcg_code U32 hal_enable_secure_boot(U32 *p, bool first)
{
    U32 otp_value;
    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    U32 errCode = 0;

    //printk("security enable =%02x , security mode =%02x\n", srb->srb_hdr.srb_security_enable, srb->srb_hdr.srb_security_mode);
    DBG_P(0x3, 0x03, 0x7200BE, 1, srb->srb_hdr.srb_security_enable, 1, srb->srb_hdr.srb_security_mode);  // security enable =%02x , security mode =%02x
    if(srb->srb_hdr.srb_security_enable == SECURITY_DISABLE){
        //printk("SRB secure boot is disable ...\n");
        DBG_P(0x01, 0x03, 0x7200BF );  // SRB secure boot is disable ...
        errCode = 0x0100;
        goto hal_enable_secure_boot_exit;
    }

    if(srb->srb_hdr.srb_security_mode != RS_SECURITY_MODE){
        //printk("SRB security mode is not RSA ...\n");
        DBG_P(0x01, 0x03, 0x7200C0 );  // SRB security mode is not RSA ...
        errCode = 0x0200;
        goto hal_enable_secure_boot_exit;
    }

    otp_value = read_otp_data(OTP_SIGNATURE_OFFSET);
    if (otp_value == OTP_SECUR_SIGN) {
        //printk("rainier signature already programmed 0x%x", otp_value);
        DBG_P(0x2, 0x03, 0x7200C1, 4, otp_value);  // rainier signature already programmed 0x%x
        errCode = 0x0300;
        goto hal_enable_secure_boot_exit;
    }

    //printk("pkey digest = %08x %8x %08x %8x - %08x %8x %08x %8x\n", *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
    DBG_P(0x9, 0x03, 0x7200C2, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // pkey digest = %08x %8x %08x %8x - %08x %8x %08x %8x
#if 1
    if(save_pub_key_to_otp(p, 32, first) == 0){
        //printk("Pass!! programmed ok ...\n");
        DBG_P(0x01, 0x03, 0x7200C3 );  // Pass!! programmed ok ...
    }else{
        TCG_ERR_PRN("Error!! programmed fail ...\n");
        DBG_P(0x01, 0x03, 0x7F7F2F);  // Error!! programmed fail ...
        errCode = 0x0300;
        goto hal_enable_secure_boot_exit;
    }
#endif
hal_enable_secure_boot_exit:
    if(errCode != 0){
        printk("Error!! hal_enable_secure_boot() errCode = %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7F7F4E, 4, errCode);  // Error!! hal_enable_secure_boot() errCode = %08x
    }
    return errCode;
}

tcg_code tERROR ur_enable_secure_boot(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 *p = tcgTmpBuf;

    if(argc < 1 || argv[0] != 0xaa55aa55){
        //printk("pkey digest = %08x %8x %08x %8x - %08x %8x %08x %8x\n", *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
        DBG_P(0x9, 0x03, 0x7200C5, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // pkey digest = %08x %8x %08x %8x - %08x %8x %08x %8x
        return 0;
    }
    hal_enable_secure_boot(p, true);
    return 0;
}

tcg_code void disable_jtag(void)
{
    PackedU32_t otp_value;
    otp_value.dword = read_otp_data(DISABLE_DEBUG_MODE_OFFSET);
    otp_value.byte.b3 = DISABLE_DEBUG_MODE_SIGNATURE;
    otp_value.byte.b1 = DISABEL_JTAG;

    if(program_otp_data(otp_value.dword, DISABLE_DEBUG_MODE_OFFSET) != 0){
        TCG_ERR_PRN("Error!! otp disable JTAG fail ...\n");
        DBG_P(0x01, 0x03, 0x7F7F30);  // Error!! otp disable JTAG fail ...
    }
    //printk("disable JTAG ok ...\n");
    DBG_P(0x01, 0x03, 0x7200C7 );  // disable JTAG ok ...
}

tcg_code tERROR ur_disable_jtag(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    disable_jtag();
    return 0;
}


tcg_code tERROR ur_secure_boot_master_params_dump(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    srb_t *srb = (srb_t *) SRB_HD_ADDR;
    U32 i, d[PUB_KEY_DIGEST_LEN / sizeof(U32)];

    //printk("security enable =%02x , security mode =%02x [0:RSA, 1:SM2]\n", srb->srb_hdr.srb_security_enable, srb->srb_hdr.srb_security_mode);
    DBG_P(0x3, 0x03, 0x7200C8, 1, srb->srb_hdr.srb_security_enable, 1, srb->srb_hdr.srb_security_mode);  // security enable =%02x , security mode =%02x [0:RSA, 1:SM2]
    //printk("pkey1 >>>");
    DBG_P(0x01, 0x03, 0x7200C9 );  // pkey1 >>>
    for(i = 0; i < PUB_KEY_DIGEST_LEN / sizeof(U32); i++){
        d[i] = read_otp_data(PUB_KEY_DIGEST_1_OFF + i * 4);
    }
    //printk("%08X %08X %08X %08X - %08X %08X %08X %08X\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    DBG_P(0x9, 0x03, 0x7200CA, 4, d[0], 4, d[1], 4, d[2], 4, d[3], 4, d[4], 4, d[5], 4, d[6], 4, d[7]);  // %08X %08X %08X %08X - %08X %08X %08X %08X

    //printk("pkey2 >>>");
    DBG_P(0x01, 0x03, 0x7200CB );  // pkey2 >>>
    for(i = 0; i < PUB_KEY_DIGEST_LEN / sizeof(U32); i++){
        d[i] = read_otp_data(PUB_KEY_DIGEST_2_OFF + i * 4);
    }
    //printk("%08X %08X %08X %08X - %08X %08X %08X %08X\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    DBG_P(0x9, 0x03, 0x7200CC, 4, d[0], 4, d[1], 4, d[2], 4, d[3], 4, d[4], 4, d[5], 4, d[6], 4, d[7]);  // %08X %08X %08X %08X - %08X %08X %08X %08X
    return 0;
}

tcg_code tERROR ur_tcg_nf(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    switch(argv[0]){
    case 0 :
        dump_G4_erased_count();
        dump_G5_erased_count();
        // DumpTcgEepInfo();
        break;
    case 1 :
        memset((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));   // force clear G4 erased count
        memset((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));   // force clear G5 erased count

        memset((void *)tcgG4Dft, 0, sizeof(tcgG4Dft));   // force clear G4 defect table
        memset((void *)tcgG5Dft, 0, sizeof(tcgG5Dft));   // force clear G5 defect table
        SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);
        break;
    default:
        break;
    }
    return 0;
}

tcg_code void dump_u16_buf(U16 *p, U16 len)
{
    U16 i;
    printk("valid adr[%04x]\n", len - 1);
    DBG_P(0x2, 0x03, 0x7200E6, 2, len - 1);  // valid adr[%04x]
    for(i = 0; i < len/sizeof(U16); i+=8){
        printk("adr[%04x] %04x %04x %04x %04x %04x %04x %04x %04x\n", i*sizeof(U16), *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
        DBG_P(0xa, 0x03, 0x7200E3, 2, i*sizeof(U16), 2, *(p+0), 2, *(p+1), 2, *(p+2), 2, *(p+3), 2, *(p+4), 2, *(p+5), 2, *(p+6), 2, *(p+7));  // adr[%04x] %04x %04x %04x %04x %04x %04x %04x %04x
        p += 16/sizeof(U16);
    }
}

tcg_code tERROR ur_l2pAssis(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    switch(argv[0]){
    case 0 :
        printk("=== G4 l2p assistor ===\n");
        DBG_P(0x01, 0x03, 0x7200E4 );  // === G4 l2p assistor ===
        dump_u16_buf((U16 *)&tcgL2pAssis.g4, sizeof(paa2laa_partial_tbl_t));
        printk("=== G5 l2p assistor ===\n");
        DBG_P(0x01, 0x03, 0x7200E5 );  // === G5 l2p assistor ===
        dump_u16_buf((U16 *)&tcgL2pAssis.g5, sizeof(paa2laa_partial_tbl_t) + 2);   // include chksum
        break;
    case 1 :
        memset((void *)&tcgL2pAssis.g4, 0, sizeof(tcg_l2p_tbl_assistor_t));   // force g4,g5 l2p assistor & chksum
        SI_Synchronize(SI_AREA_BIT_TCG, SYSINFO_WRITE_FORCE, SI_SYNC_BY_SYSINFO);
        break;
    default:
        break;
    }
    return 0;
}

#ifdef __TCG_NF_MID
tcg_code void tcg_nf_mid_params_set(U16 laas, U16 laae, PVOID Pbuf)
{
    // DBG_P(3, 3, 0x82032F, 2, laas, 2, laae);  //82 03 2F, "[F]tcg_nf_mid_params_set laas[%X] laae[%X]"
    tcgNfHalParams.laas = laas;
    tcgNfHalParams.laae = laae;   // doen't include laae
    //tcgNfHalParams.laacnt = tcgNfHalParams.laae - tcgNfHalParams.laas + 1;    // Don't add 1
    tcgNfHalParams.laacnt = tcgNfHalParams.laae - tcgNfHalParams.laas;
    tcgNfHalParams.pBuf = Pbuf;
    TCGPRN("laas|%04x laae|%04x laaCnt|%04x\n", tcgNfHalParams.laas, tcgNfHalParams.laae, tcgNfHalParams.laacnt);
    DBG_P(0x4, 0x03, 0x700200, 2, tcgNfHalParams.laas, 2, tcgNfHalParams.laae, 2, tcgNfHalParams.laacnt);  // laas|%04x laae|%04x laaCnt|%04x
}
#endif

//-------------------------------------------------
//  secure_boot_enable (to write OTP)
//-------------------------------------------------
extern ca6_secure_info_t ca6_secure_info;
tcg_code static int tcg_secure_boot_enable(req_t* req)
{
    // srb_t *srb = (srb_t *) SRB_HD_ADDR;
    U32 errCode = 0;
    TCGPRN("tcg_secure_boot_enable()\n");
    DBG_P(0x01, 0x03, 0x7200EC );  // tcg_secure_boot_enable()

    bool first = TRUE;
    if(req->op_fields.TCG.laas == 0x55AA){  // second OTP key tag ?
        first = FALSE;
    }
    printk("pkey %s\n", first ? "OTP1" : "OTP2");
    DBG_P(0x2, 0x03, 0x7200EB, 0xFF, first ? "OTP1" : "OTP2");  // pkey %s

    if(chk_all_rows_pkey_unity() == zNG){
        errCode = 0x0100;
        goto secure_boot_enable_exit;
    }

    // check RLS key digest with ca6_secure_info.rls ?
    #if 1
    if(memcmp((U8*)tcgTmpBuf, ca6_secure_info.loader.rls_digest, 32) != 0){
        errCode = 0x0200;
        goto secure_boot_enable_exit;
    }
    #endif

    disable_jtag();

    if(hal_enable_secure_boot(tcgTmpBuf, first) != 0){
        errCode = 0x0300;
        goto secure_boot_enable_exit;
    }

secure_boot_enable_exit:
    if(errCode != 0){
        printk("Error!! tcg_secure_boot_enable() errCode = %08x\n", errCode);
        DBG_P(0x2, 0x03, 0x7F7F4D, 4, errCode);  // Error!! tcg_secure_boot_enable() errCode = %08x
        return zNG;
    }
    printk("sercure boot OTP write success ...\n");
    DBG_P(0x01, 0x03, 0x7200EA );  // sercure boot OTP write success ...
    return zOK;
}
#endif // Jack Li
