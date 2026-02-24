# Related Firmware Code (deduped)

- FW_ROOT: `src`
- INPUT: `TEST.txt`
- Context: ±20 lines

## ftl/ftl_spor.c

- Resolved path: `src\ftl\ftl_spor.c`
- Hits: 8 (deduped), Snippets: 5 (merged)

### Snippet 1: L658-L713
- Reasons: ftl/ftl_spor.c+678 ftl_spor_blk_sn_sync(), ftl/ftl_spor.c+693 ftl_spor_spb_desc_sync()

```c
 *
 *
 * @return	not used
 */
init_code u16 ftl_spor_get_next_blk(u16 blk)
{
    return ftl_spor_blk_node[blk].nxt_blk;
}

init_code u16 ftl_spor_get_pre_blk(u16 blk)
{
    return ftl_spor_blk_node[blk].pre_blk;
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_spor_blk_sn_sync(void)
{
	u16 spb_id;

	ftl_apl_trace(LOG_ALW, 0xdb45, "[IN] ftl_spor_blk_sn_sync");
	for(spb_id=0;spb_id<shr_nand_info.geo.nr_blocks;spb_id++)
    {
        spb_set_sn(spb_id, ftl_init_sn_tbl[spb_id]);
    }
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code void ftl_spor_spb_desc_sync(void)
{
	ftl_apl_trace(LOG_ALW, 0x4757, "[IN] ftl_spor_spb_desc_sync");

	if(ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P || blk_list_need_check_flag)
	{
		//set nsid
		u16 loop = 0;
		u16 user_cnt = spb_get_free_cnt(SPB_POOL_USER);
		u16 user_idx = spb_mgr_get_head(SPB_POOL_USER);

		for(loop=0; loop<user_cnt; loop++)
		{
			spb_get_desc(user_idx)->ns_id = FTL_NS_ID_START;
			user_idx = spb_info_get(user_idx)->block;
		}

		u16 qbt_cnt = spb_get_free_cnt(SPB_POOL_QBT_ALLOC);
		u16 qbt_idx = spb_mgr_get_head(SPB_POOL_QBT_ALLOC);
```

### Snippet 2: L1182-L1233
- Reasons: ftl/ftl_spor.c+1202 ftl_chk_slc_buffer_done(), ftl/ftl_spor.c+1213 ftl_chk_slc_buffer_done()

```c
init_code void ftl_set_trigger_slc_gc(bool type)
{
	need_trigger_slc_gc = type;
}


init_code void ftl_clean_slc_blk(void)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_sblk_erase(PLP_SLC_BUFFER_BLK_ID);
	epm_ftl_data->plp_pre_slc_wl = 0;
    epm_ftl_data->plp_slc_wl     = 0;
    //epm_ftl_data->plp_slc_times  = 0;
    epm_ftl_data->plp_slc_gc_tag = 0;    
    //epm_ftl_data->plp_slc_disable = 0;
    epm_ftl_data->slc_format_tag = 0;
    epm_ftl_data->slc_eraseing_tag = 0;
}



init_code u8 ftl_chk_slc_buffer_done(void)
{
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	ftl_apl_trace(LOG_ALW, 0xd694, "[IN] ftl_chk_slc_buffer_done");

	//0. essr err , lock slc block
	if(epm_ftl_data->esr_lock_slc_block == true)
	{
		return mFALSE;
	}
	//1. if epm err , don't use slc buffer 
	if((epm_ftl_data->epm_sign != FTL_sign) || (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
	{
		if(epm_ftl_data->spor_tag == 0)//first power on
		{
			//not need disable slc buffer
			return mFALSE;
		}
		epm_ftl_data->plp_slc_disable = true;
		epm_ftl_data->plp_slc_gc_tag  = 0;//avoid fail
	 	ftl_apl_trace(LOG_ALW, 0x9f53, "[SLC] EPM ERR , disable slc buffer sign:%d tag:0x%x",epm_ftl_data->epm_sign,epm_ftl_data->spor_tag);
		#if (IGNORE_PLP_NOT_DONE == mENABLE)
		epm_ftl_data->plp_slc_disable = false;
		ftl_clean_slc_blk();
		#endif
		
		return mTRUE;
	}

	//2. if slc format flag not clear , need continue erase slc blk
	if(epm_ftl_data->slc_format_tag == SLC_ERASE_FORMAT_TAG)
```

### Snippet 3: L1315-L1355
- Reasons: ftl/ftl_spor.c+1335 ftl_need_p2l_rebuild()

```c
        // Host block SN larger than QBT block SN
        if(ftl_spor_info->maxHostDataBlkSn > ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC)))
        {

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
            if((ftl_spor_info->load_blist_mode == FTL_QBT_LOAD_BLIST) && (epm_ftl_data->epm_vc_table[ftl_spor_info->maxHostDataBlk] == 0) && (ftl_spor_info->maxQBTSn+1 == ftl_spor_info->maxHostDataBlkSn))
#else
            //if((ftl_spor_info->load_blist_mode == FTL_QBT_LOAD_BLIST) && (ftl_spor_info->maxQBTSn+1 == ftl_spor_info->maxHostDataBlkSn))
			if(0)//nonplp can't judge max host blk invalid
#endif
        	{
				spor_rebuild_flag = mFALSE;
			}
			else
			{
				spor_rebuild_flag = mTRUE;
			}
        }
    }
    else if(ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P)
    {
        // ToDo : May need to add PBT force close case
        spor_rebuild_flag = mTRUE;
    }

    // need enter spor flow
    if(spor_rebuild_flag)
    {
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
        epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
        if((epm_ftl_data->epm_sign != FTL_sign) || (epm_ftl_data->spor_tag != FTL_EPM_SPOR_TAG))
        {
			// vac table on epm is corrupted, check if need qbt/pbt build
			if ((ftl_spor_info->build_l2P_mode == FTL_QBT_BUILD_L2P) || (ftl_spor_info->build_l2P_mode == FTL_PBT_BUILD_L2P)) {
				// we can make sure blklist and l2p is ok at this point
				ftl_apl_trace(LOG_ALW, 0x642d, "EPM VAC read fail, swith to FTL_QBT/PBT_BUILD_L2P mode");
				ftl_spor_info->epm_vac_error = mTRUE;
			} else {
				sys_assert(ftl_spor_info->build_l2P_mode == FTL_USR_BUILD_L2P);
				ftl_apl_trace(LOG_ALW, 0xec2e, "EPM VAC read fail, swith to FTL_USR_BUILD_L2P mode");
				ftl_spor_info->scan_sn = 0;
```

### Snippet 4: L1373-L1415
- Reasons: ftl/ftl_spor.c+1393 ftl_spor_build_l2p_mode(), ftl/ftl_spor.c+1395 ftl_spor_build_l2p_mode()

```c
	gc_blk  = ftl_get_spor_p2l_blk_head(ftl_spor_info->secondDataBlkSn, SPB_POOL_GC, mTRUE);

	ftl_apl_trace(LOG_ALW, 0x2f7e,"Blist ---------- usr:%d gc:%d",usr_blk,gc_blk);

	if(usr_blk != INV_U16 && gc_blk != INV_U16)
	{
		if(ftl_init_sn_tbl[usr_blk] > ftl_init_sn_tbl[gc_blk])
			cur_blk = usr_blk;
		else
			cur_blk = gc_blk;

		return  cur_blk;
	}
	else if(usr_blk == INV_U16 && gc_blk != INV_U16)
	{
		return gc_blk;
	}
	else if(usr_blk != INV_U16 && gc_blk == INV_U16)
	{
		return usr_blk;
	}
	else
	{
		return INV_U16;
	}
}

/*!
 * @brief ftl spor function
 *
 *
 * @return	not used
 */
init_code u8 ftl_spor_build_l2p_mode(void)
{
    ftl_apl_trace(LOG_ALW, 0x35e1, "QBT Exist : %d, PBT Exist : %d", ftl_spor_info->existQBT, ftl_spor_info->existPBT);

    ftl_apl_trace(LOG_ALW, 0x7970, "QBT Head idx : 0x%x, QBT Head SN : 0x%x ; PBT Head idx : 0x%x, PBT Head SN : 0x%x",\
           ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC),\
           ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC)),\
           ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC),\
           ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC)));

```

### Snippet 5: L1423-L1463
- Reasons: ftl/ftl_spor.c+1443 ftl_spor_build_l2p_mode()

```c
    else
    {
        ftl_spor_info->build_l2P_mode = FTL_USR_BUILD_L2P;
    }
#else
    if((ftl_spor_info->existQBT == mTRUE)&&(ftl_spor_info->existPBT == mTRUE))
    {
        if(ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC)) >
           ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_QBT_ALLOC)))
        {
            ftl_spor_info->build_l2P_mode = FTL_PBT_BUILD_L2P;
        }
        else
        {
            ftl_spor_info->build_l2P_mode = FTL_QBT_BUILD_L2P;
        }
    }
    else if(ftl_spor_info->existQBT == mTRUE)
    {
        ftl_spor_info->build_l2P_mode = FTL_QBT_BUILD_L2P;
    }
    else if(ftl_spor_info->existPBT == mTRUE)
    {
        ftl_spor_info->build_l2P_mode = FTL_PBT_BUILD_L2P;
    }
    else
    {
        ftl_spor_info->build_l2P_mode = FTL_USR_BUILD_L2P;
    }
#endif

    switch(ftl_spor_info->build_l2P_mode)
    {
        case FTL_QBT_BUILD_L2P:
            ftl_spor_info->scan_sn = ftl_spor_get_blk_sn(ftl_spor_get_tail_from_pool(SPB_POOL_QBT_ALLOC));
            ftl_apl_trace(LOG_ALW, 0xb020, "Load Build L2P mode [QBT]");
            break;

        case FTL_PBT_BUILD_L2P:
            ftl_spor_info->scan_sn = ftl_spor_get_blk_sn(ftl_spor_get_head_from_pool(SPB_POOL_PBT_ALLOC));
            ftl_apl_trace(LOG_ALW, 0x2acd, "Load Build L2P mode [PBT]");
```

## fcore/ftl_core.c

- Resolved path: `src\fcore\ftl_core.c`
- Hits: 8 (deduped), Snippets: 6 (merged)

### Snippet 1: L663-L703
- Reasons: fcore/ftl_core.c+683 ftl_set_force_dump_pbt()

```c
	}else{
		tFtlPbt.force_dump_pbt = false;
		tFtlPbt.force_dump_pbt_mode = DUMP_PBT_REGULAR;

        //flush_io_each = tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl;
		if(otf_forcepbt){
			otf_forcepbt  = false;
			//shr_host_write_cnt = shr_host_write_cnt_old;
		}
		tFtlPbt.force_cnt = 0;

		CBF_MAKE_EMPTY(&pbt_updt_ntf_que);
		//ftl_core_trace(LOG_INFO, 0, "[PBT]force_clear:%d",tFtlPbt.force_dump_pbt_mode);
		if(flag_need_clear_api){
            flag_need_clear_api = false;
    	    cpu_msg_issue(2, CPU_MSG_CLEAR_API, 0, 0);
        }
        #if(BG_TRIM == ENABLE)
        if(BG_TRIM_HANDERING){
            cpu_msg_issue(0, CPU_MSG_FORCE_PBT_CPU1_ACK, 0, 0);
        }
        #endif
		// Set it back, ISU, EHPerfImprove, Paul_20220221
		flush_io_each = ftl_io_ctl;
	}
	//ftl_core_trace(LOG_INFO, 0, "[PBT]set_force:%d, mode:%d",force_start,tFtlPbt.force_dump_pbt_mode);

	/*enable/disable spor use 2 group pbt build*/
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(epm_ftl_data->pbt_force_flush_flag != mode)
	{
		epm_ftl_data->pbt_force_flush_flag = mode;
		epm_update(FTL_sign,(CPU_ID-1)); 
	}


	ftl_core_trace(LOG_INFO, 0x9163, "[PBT]set_force:%d, force_mode:%d, flush_io_each:%d loop_idx:%d", force_start, tFtlPbt.force_dump_pbt_mode, flush_io_each,tFtlPbt.pbt_cur_loop);
}

fast_code inline void pbt_flush_proc(void){
	u32 tmp;
```

### Snippet 2: L809-L849
- Reasons: fcore/ftl_core.c+829 ftl_core_pbt_seg_flush()

```c
				ftl_core_trace(LOG_INFO, 0, "[PBT]pda:0x%x",pda);
			}
			*/

			#ifndef LJ_Meta
			//res->io_meta[i].sn = 0;
			u32 page_idx = wr_pl->cnt >> DU_CNT_SHIFT;
			u32 du_offst = wr_pl->cnt & (DU_CNT_PER_PAGE - 1);
			pda = wr_pl->pda[page_idx] + du_offst;
			#endif
			wr_pl->lda[wr_pl->cnt] = PBT_LDA;
			wr_pl->pl[wr_pl->cnt].pl.du_ofst = i;

#if RAID_SUPPORT
            if(wr_pl->flags.b.parity_mix)
                wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (raid_id_mgr.pad_meta_idx[wr_pl->stripe_id] + wr_pl->cnt);
            else
#endif
                wr_pl->pl[wr_pl->cnt].pl.nvm_cmd_id = (_tbl_res[0].meta_idx + wr_pl->cnt);

			if(tFtlPbt.isfilldummy){
			wr_pl->pl[wr_pl->cnt].pl.dtag = WVTAG_ID;
			}else{
			wr_pl->pl[wr_pl->cnt].pl.dtag = DTAG_IN_DDR_MASK | (dtag_base + i);
			}
			wr_pl->pl[wr_pl->cnt].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			wr_pl->cnt++;
			wr_pl->flags.b.ftl = 1;

            if (wr_pl->cnt == wr_pl->max_cnt) { 
				ncl_w_req_t *req = ftl_core_submit(wr_pl, NULL, NULL);  
                pda = wr_pl->pda[0] + (DU_CNT_PER_PAGE - 1);
				wr_pl = NULL;  
                //if (req && ((pda2die(pda) + 1) % nand_info.geo.nr_channels == 0))
                if (req)    
                {
                    loop++;
                }
			} 

		}
```

### Snippet 3: L890-L930
- Reasons: fcore/ftl_core.c+910 ftl_pbt_init()

```c
		tFtlPbt.cur_seg_ofst 		 = 0;
        tFtlPbt.fill_wl_cnt          = 0; 
		tFtlPbt.total_write_tu_cnt	 = 0;
		tFtlPbt.total_write_page_cnt = 0;
        ftl_clear_pbt_cover_usr_cnt();
	}
}


slow_code void ftl_pbt_init(bool force_mode){
    tFtlPbt.cur_seg_ofst            = 0;
    tFtlPbt.force_dump_pbt          = mFALSE;
    tFtlPbt.force_dump_pbt_mode     = DUMP_PBT_REGULAR;
    tFtlPbt.pbt_force_end_wptr      = INV_U32;
    tFtlPbt.dump_pbt_start          = mFALSE;
    tFtlPbt.pbt_res_ready           = mTRUE;
    tFtlPbt.isfilldummy             = mFALSE;
    tFtlPbt.filldummy_done          = mFALSE;
    tFtlPbt.fill_wl_cnt             = 0; 
    tFtlPbt.pbt_spb_cur             = INV_U16;
    tFtlPbt.pbt_cover_usr_cnt       = 0;
    tFtlPbt.pbt_flush_IO_cnt        = 0;
    tFtlPbt.total_write_page_cnt    = 0;
    tFtlPbt.total_write_tu_cnt      = 0;
    tFtlPbt.force_cnt               = 0;
	otf_forcepbt 					= mFALSE;
    if(force_mode == false){
        tFtlPbt.pbt_flush_total_cnt     = ((nand_info.interleave*4*3/shr_l2pp_per_seg)*ftl_wcnt_ctl);
        tFtlPbt.pbt_flush_page_cnt      = PBT_RATIO*ftl_wcnt_ctl;
        tFtlPbt.tbl_res_pool_cnt        = ftl_io_ctl;
        tFtlPbt.force_pbt_halt          = false;
        flush_io_each                   = ftl_io_ctl;
        #if 0 //(SPOR_FLOW == mENABLE)
        //tFtlPbt.pbt_cur_loop            = (shr_pbt_loop!=INV_U8)? shr_pbt_loop:0;
        #else
        tFtlPbt.pbt_cur_loop            = 0;    // 1 base
        if (_fg_warm_boot == false)
        {
            shr_pbt_loop = tFtlPbt.pbt_cur_loop;
        }
        #endif
```

### Snippet 4: L969-L1009
- Reasons: fcore/ftl_core.c+989 ftl_set_pbt_halt()

```c
	flush_io_each	= tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl;
	ftl_core_trace(LOG_INFO, 0xed17, "modify io: %d -> %d, each:%d",io_tmp,ftl_io_ctl,flush_io_each);
	return 0;
}

static DEFINE_UART_CMD(pbtio, "pbtio","pbt","modify pbt io",1, 1, pbt_io_main);


static slow_code int pbt_wcnt_main(int argc, char *argv[])
{
	u64 io_tmp = ftl_wcnt_ctl;
	ftl_wcnt_ctl = (u64) atoi(argv[1]);
	tFtlPbt.pbt_flush_total_cnt = ((ns[0].seg_end/(1152)))*ftl_wcnt_ctl;
	tFtlPbt.pbt_flush_page_cnt  = (PBT_RATIO)*ftl_wcnt_ctl;
	flush_io_each	= tFtlPbt.pbt_flush_total_cnt/ftl_io_ctl;
	ftl_core_trace(LOG_INFO, 0xbf80, "modify wcnt: %d -> %d flush_total:%d, pg_ratio:%d, each:%d",io_tmp,ftl_wcnt_ctl,tFtlPbt.pbt_flush_total_cnt,tFtlPbt.pbt_flush_page_cnt,flush_io_each);
	return 0;
}

static DEFINE_UART_CMD(pbtwcnt, "pbtwcnt","pbt","modify pbt wcnt",1, 1, pbt_wcnt_main);
#endif

static slow_code int pbt_stop_main(int argc, char *argv[])
{
	tFtlPbt.force_pbt_halt = (u64) atoi(argv[1]);
	ftl_core_trace(LOG_ERR, 0x7ba9, "pbt stop: %d \n",tFtlPbt.force_pbt_halt);
	return 0;
}

static DEFINE_UART_CMD(pbtstop, "pbtstop","pbt","stop flush pbt",1, 1, pbt_stop_main);

static slow_code int pbt_force_dump_main(int argc, char *argv[])
{
	//u64 force_mode = 0;
	//force_mode = (u64) atoi(argv[1]);
	//ftl_set_force_dump_pbt(true,DUMP_PBT_FORCE_FINISHED_CUR_GROUP);
	ftl_set_force_dump_pbt(0,true,1);
	ftl_core_trace(LOG_INFO, 0xbe1f, "[PBT]force_mode:%d",tFtlPbt.force_dump_pbt_mode);
	return 0;
}

```

### Snippet 5: L1138-L1178
- Reasons: fcore/ftl_core.c+1158 ftl_core_restore_pbt_param()

```c
    else if(fcore->wreq_cnt == 0)
    {
        pbt_resume_param->pbt_info.flags.b.wb_flushing_pbt = mFALSE;
        ftl_core_trace(LOG_ALW, 0xfbe3, "no need to wait");
    }else{
        ftl_core_trace(LOG_ALW, 0xb74d, "Need to wait. fcore->wreq_cnt:%u", fcore->wreq_cnt);
    }

    //save tFtlPbt
    pbt_resume_param->cur_seg_ofst = tFtlPbt.cur_seg_ofst;
    pbt_resume_param->pbt_cover_usr_cnt = tFtlPbt.pbt_cover_usr_cnt;   
    pbt_resume_param->fill_wl_cnt = tFtlPbt.fill_wl_cnt; 

    //save pbt pstream information
    if (ps->avail_cnt == 0) 
    { 
        pbt_resume_param->pbt_info.spb_id = INV_SPB_ID; 
        return; 
    } 
    pbt_resume_param->pbt_info.spb_id = spb_id;
    pbt_resume_param->pbt_info.flags.b.slc = aspb->flags.b.slc;
    pbt_resume_param->pbt_info.flags.b.valid = aspb->flags.b.valid;
    pbt_resume_param->pbt_info.open_skip_cnt = aspb->open_skip_cnt;
    pbt_resume_param->pbt_info.total_bad_die_cnt = aspb->total_bad_die_cnt;
#ifdef RAID_SUPPORT
    pbt_resume_param->pbt_info.parity_die = aspb->parity_die;
    pbt_resume_param->pbt_info.parity_die_pln_idx = aspb->parity_die_pln_idx;
    pbt_resume_param->pbt_info.flags.b.parity_mix = aspb->flags.b.parity_mix;
#endif

    pbt_resume_param->pbt_info.ptr = aspb->wptr;
    pbt_resume_param->pbt_info.sn = aspb->sn;

    ftl_core_trace(LOG_ALW, 0xab02, "blk: %u, wptr: %u, avail_cnt: %u, fill_wl_cnt: %u, wreq: %u", 
        spb_id, aspb->wptr, ps->avail_cnt, tFtlPbt.fill_wl_cnt, fcore->wreq_cnt); 
}

/*!
 * @brief restore the PBT information after fw update, and continue to write this PBT
 *
 * @return 	not used
```

### Snippet 6: L1820-L1897
- Reasons: fcore/ftl_core.c+1840 ftl_core_flush_shutdown(), fcore/ftl_core.c+1858 ftl_core_flush_shutdown(), fcore/ftl_core.c+1877 ftl_core_flush_shutdown()

```c
 */
slow_code void tbl_pda_updt(ncl_w_req_t *req)
{
	if (req->w.lda[0] == INV_LDA)
		return;	// pad req

	//u32 seg = req->w.lda[0] >> shr_l2pp_per_seg_shf;
	u32 seg = (req->w.lda[0]/12);
	pda_t old;

	old = shr_ins_lut[seg];
#ifdef SKIP_MODE
	shr_ins_lut[seg] = req->w.pda[0];
	dvl_info_updt(_inv_dtag, req->w.pda[0], false);
#else
	shr_ins_lut[seg] = req->w.l2p_pda[0];
	dvl_info_updt(_inv_dtag, req->w.l2p_pda[0], false);
#endif

	if (is_normal_pda(old))
		dvl_info_updt(_inv_dtag, old, true);

#if 1
	u32 lda_cnt = req->req.cnt * DU_CNT_PER_PAGE;
	u32 i;
	for (i = 1; i < lda_cnt; i++) {
		sys_assert(req->w.lda[i] == req->w.lda[i - 1] + 1);
	}
	sys_assert(req->req.cnt == nand_plane_num());
	for (i = 1; i < req->req.cnt; i++) {
#ifdef SKIP_MODE
				sys_assert(req->w.pda[i] == req->w.pda[i - 1] + DU_CNT_PER_PAGE);
#else
				sys_assert(req->w.l2p_pda[i] == req->w.l2p_pda[i - 1] + DU_CNT_PER_PAGE);
#endif

	}
#endif
}

fast_data_zi ftl_flush_data_t * shutdown_flush_ctx;

slow_code void ftl_core_flush_shutdown(flush_ctx_t *ctx)
{
	u32 nsid = ctx->nsid;
    sys_assert(shutdown_flush_ctx);
	ftl_flush_data_t *fctx = shutdown_flush_ctx;

	if (ctx->flags.b.shutdown) {
        u8 i;
        for (i = 0; i < FTL_CORE_MAX; i++) {
    		ftl_core_t *fc = fcns[HOST_NS_ID]->ftl_core[i];
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(SLC_init == true && i == FTL_CORE_SLC) //when shundown , don't reset slc pstream
				continue;
#else
			if(i == FTL_CORE_PBT)
				continue;
#endif	
			ftl_core_trace(LOG_INFO, 0x503d, "ns:%d(%d), wreq_cnt:%d",HOST_NS_ID,i,fc->wreq_cnt);
			if(fc->wreq_cnt !=0)
			{
				ftl_core_trace(LOG_WARNING, 0x62e1, "ns:%d(%d)not ready chk again",HOST_NS_ID,i);
				evt_set_cs(evt_fc_shutdown_chk, (u32)ctx, 0, CS_TASK);
				return;
			}
    		sys_assert(fc->wreq_cnt == 0);
    		pstream_reset(&fc->ps);
    		fc->cwl.cur.mpl_qt = 0; // 20200908 Curry initial to get new blk open die
    	}

    	for (i = 0; i < FTL_CORE_MAX; i++) {
    		ftl_core_t *fc = fcns[INT_NS_ID]->ftl_core[i];

    		if (fc == NULL)
    			continue;

			ftl_core_trace(LOG_INFO, 0x0d3c, "ns:%d(%d), wreq_cnt:%d",INT_NS_ID,i,fc->wreq_cnt);
```

## rtos/armv7r/pcie.c

- Resolved path: `src\rtos\armv7r\pcie.c`
- Hits: 8 (deduped), Snippets: 2 (merged)

### Snippet 1: L1125-L1178
- Reasons: rtos/armv7r/pcie.c+1145 pcie_phy_leq_dfe(), rtos/armv7r/pcie.c+1146 pcie_phy_leq_dfe(), rtos/armv7r/pcie.c+1149 pcie_phy_leq_dfe(), rtos/armv7r/pcie.c+1158 pcie_phy_leq_dfe()

```c
					pcie_force_1lane();
					pcie_lane1_cnt++;
					return;
				}
				// lane2/Lane3 active, but lane0/lane1 has term
				pcie_ctrl.all = pcie_wrap_readl(PCIE_CONTROL_REG);
				pcie_ctrl.b.tx_lane_flip_en = 1;
				pcie_ctrl.b.rx_lane_flip_en = 1;
				pcie_wrap_writel(pcie_ctrl.all, PCIE_CONTROL_REG);
				pcie_force_2lane();
				//rtos_core_trace(LOG_ALW, 0xa21c, "enable ltssm rvs x2");
				//printk( "enable ltssm rvs x2 \n");
			} else if (((lane & 0xF) == 0x7) || ((lane & 0xF) == 0x5) ){
				// lane3 active, but lane0/lane1/lane2 has term
				pcie_ctrl.all = pcie_wrap_readl(PCIE_CONTROL_REG);
				pcie_ctrl.b.tx_lane_flip_en = 1;
				pcie_ctrl.b.rx_lane_flip_en = 1;
				pcie_wrap_writel(pcie_ctrl.all, PCIE_CONTROL_REG);
				pcie_lane1_cnt++;
				pcie_force_1lane();
				//pcie_term_wa_enabled = 1;
				//rtos_core_trace(LOG_ALW, 0xa21f, "enable ltssm rvs x1");
			}
		

		}
	}
	else
	{
		pcie_timer_enable = 1;
		lane_chk_times += PCIE_RE_CHK_LANE;
		timer_count_down(PCIE_RE_CHK_LANE, pcie_lane_chk);
	}

}


/*!
 * @brief use 25MHz timer to do timer count down, now this api is only used to warm boot check
 *        please don't call irq_enable in irq mode, or it will cause nest irq
 * @param us		timerout value in us, if us is 0, request elapsed time
 * @param isr		isr to check something
 *
 * @return		return elapsed time after calculating timer value * prescale
 */
fast_code u32 timer_count_down(u32 us, void (*isr)(void))
{
	if (us) {
		shared_timer0_value_t val;
		shared_timer0_ctrl_t ctrl;
		u32 e = 1;
		u32 f = us;

		while (f >= 65536) {
```

### Snippet 2: L1216-L1260
- Reasons: rtos/armv7r/pcie.c+1236 pcie_phy_cali_results(), rtos/armv7r/pcie.c+1237 pcie_phy_cali_results(), rtos/armv7r/pcie.c+1239 pcie_phy_cali_results(), rtos/armv7r/pcie.c+1240 pcie_phy_cali_results()

```c
	pcie_unmsk_intr_t pcie_int;

	pcie_intr_mask_init();

	pcie_control_reg_t pcie_ctrlr = {
		.all = pcie_wrap_readl(PCIE_CONTROL_REG),
	};

	del_timer(&pcie_link_timer); // delete timer for dfe and leq to avoid re-entry
#ifdef RXERR_IRQ_RETRAIN
	del_timer(&pcie_retrain_timer);	
#endif
	extern void nvmet_pcie_config_resume(void);
	nvmet_pcie_config_resume();
	pcie_wait_clear_perst_n();

	if (gPlpDetectInIrqDisableMode)
	{
		gPlpDetectInIrqDisableMode = false;
		return;
	}

	//pcie_tdr_setting();
#ifdef SHORT_CHANNEL
	pcie_short_channel_improve();
#endif

#ifdef IG_MODIFIED_PHY
	//pcie_tdr_setting();
#ifndef PCIE_PHY_IG_SDK
	pcie_tx_update();
#endif
	pcie_pll_bw_update();
#ifdef PCIE_PHY_IG_SDK
	pcie_tx_update();
#endif
	//pcie_gen3_bandwidth();
	pcie_phy_leq_train();
	pcie_phy_cdr_setting();
#ifdef PCIE_PHY_IG_SDK
	pcie_phy_squelch_update();
	pcie_phy_cdr_relock();
	pcie_phy_pll_lock();
	pcie_phy_cdr_lock_enhance();
	pcie_phy_l11_tx_commode_on();
```

## nvme/apl/decoder/admin_cmd.c

- Resolved path: `src\nvme\apl\decoder\admin_cmd.c`
- Hits: 8 (deduped), Snippets: 7 (merged)

### Snippet 1: L1678-L1718
- Reasons: nvme/apl/decoder/admin_cmd.c+1698 nvmet_delete_io_cq()

```c
	return true;
}

/*!
 * @brief delete I/O completion queue
 *
 * @param req	request to delete I/O CQ
 * @param cmd	nvme command
 *
 * @return	command execution status
 */
static enum cmd_rslt_t nvmet_delete_io_cq(req_t *req, struct nvme_cmd *cmd)
{
	u16 qid = 1;
	u16 cqid = cmd->cdw10;
	struct nvmet_cq *cq;
	u32 nsid = cmd->nsid;
	nvme_apl_trace(LOG_INFO, 0xe942, "Delete I/O CQ: CQID(%d)", cqid);

	if (!nvmet_queue_sanity_check(req, cqid, 0, nsid))
		return HANDLE_RESULT_FAILURE;

#if defined(SRIOV_SUPPORT)
	u8 fnid = req->fe.nvme.cntlid;
	cq = (fnid == 0) ? ctrlr->cqs[cqid] : ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrcq[cqid];
#else
	cq = ctrlr->cqs[cqid];
#endif
	if (cq == NULL)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

#if defined(SRIOV_SUPPORT)
	u8 error = 0;
	if (fnid == 0)
	{
		for (; qid < ctrlr->max_qid; qid++)
```

### Snippet 2: L1828-L1868
- Reasons: nvme/apl/decoder/admin_cmd.c+1848 nvmet_delete_io_sq()

```c

#if defined(SRIOV_SUPPORT)
	fnid = req->fe.nvme.cntlid;
	sq = (fnid == 0) ? ctrlr->sqs[sqid] : ctrlr->sr_iov->fqr_sq_cq[fnid - 1].fqrsq[sqid];
#else
	fnid = 0;
	sq = ctrlr->sqs[sqid];
#endif

	if ((sqid == 0) || (sq == NULL))
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}

	nvme_apl_trace(LOG_INFO, 0xe988, "Delete I/O SQ: SQID(%d)", sqid);

#if defined(SRIOV_SUPPORT)
	sq_mid = nvmet_get_flex_sq_map_idx(fnid, sqid);
	if (sq_mid == 0xFF)
	{
		nvme_apl_trace(LOG_ERR, 0x52c2, "Mapping failed .....");
		nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_QUEUE_IDENTIFIER);
		return HANDLE_RESULT_FAILURE;
	}
#else
	sq_mid = sqid;
#endif

	req->op_fields.admin.sq_id = sq_mid;

	//extern int btn_cmd_idle(void);

	//if ((nvme_hal_check_pending_cmd_by_sq(sq_mid) && list_empty(&nvme_sq_del_reqs)) &&
		//(hal_nvmet_check_io_sq_pending()) &&
		//(btn_cmd_idle()) ) {
	if (1) {
		/*
		 * The command causes all commands submitted to the indicated Submission
```

### Snippet 3: L1893-L1933
- Reasons: nvme/apl/decoder/admin_cmd.c+1913 nvmet_create_io_sq()

```c
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	error code
 */
static enum cmd_rslt_t nvmet_create_io_sq(req_t *req, struct nvme_cmd *cmd)
{
	u16 sqid = cmd->cdw10;
	u16 qsize = cmd->cdw10 >> 16;
	u16 cqid = cmd->cdw11 >> 16;
	enum nvme_qprio qprio = (enum nvme_qprio)((cmd->cdw11 >> 1) & 0x3);
	u8 pc = cmd->cdw11 & 0x1;
	bool result;
	struct nvmet_sq *sq = NULL;
	u32 nsid = cmd->nsid;

	nvme_apl_trace(LOG_INFO, 0xf2d9, " Create I/O SQ: SQID(%d) CQID(%d) SIZE(%d) PRIO(%d)", sqid, cqid, qsize, qprio);

	/* IOL tnvme 17:3.0.0, Qsize==0 is invalid*/
	if (qsize == 0)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_SIZE);
		return HANDLE_RESULT_FAILURE;
	}

	if (!nvmet_queue_sanity_check(req, sqid, qsize, nsid))
		return HANDLE_RESULT_FAILURE;

	/* IOL tnvme 17:6.0.0, prp should have 0h offset */
	if ((cmd->psdt == 0) && (cmd->dptr.prp.prp1 & 0xFFF))
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_PRP_OFFSET);
		return HANDLE_RESULT_FAILURE;
	}

	if (cqid == 0)
```

### Snippet 4: L2122-L2162
- Reasons: nvme/apl/decoder/admin_cmd.c+2142 nvmet_create_io_cq()

```c
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	Command execution status
 */
static enum cmd_rslt_t nvmet_create_io_cq(req_t *req, struct nvme_cmd *cmd)
{
	u16 qid = cmd->cdw10;
	u16 qsize = cmd->cdw10 >> 16;
	u16 pc = (cmd->cdw11 & BIT(0)) ? 1 : 0;
	u16 ien = (cmd->cdw11 & BIT(1)) ? 1 : 0;
	u16 iv = cmd->cdw11 >> 16;
	struct nvmet_cq *cq = NULL;
	bool result;
	u32 nsid = cmd->nsid;

	nvme_apl_trace(LOG_INFO, 0x2731, "Create I/O CQ: CQID(%d) SIZE(%d) PC(%d)", qid, qsize, pc);

	/* IOL tnvme 16:3.0.0, Qsize==0 is invalid*/
	if (qsize == 0)
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_COMMAND_SPECIFIC,
						 NVME_SC_INVALID_QUEUE_SIZE);
		return HANDLE_RESULT_FAILURE;
	}

	if (!nvmet_queue_sanity_check(req, qid, qsize, nsid))
		return HANDLE_RESULT_FAILURE;

	/* IOL tnvme 16:5.0.0, prp should be 0h offset */
	if ((cmd->psdt == 0) && (cmd->dptr.prp.prp1 & 0xFFF))
	{
		nvmet_set_status(&req->fe,
						 NVME_SCT_GENERIC,
						 NVME_SC_INVALID_PRP_OFFSET);
		return HANDLE_RESULT_FAILURE;
	}

	/* For SR-IOV, this value could be 0 to 65 */
```

### Snippet 5: L2289-L2367
- Reasons: nvme/apl/decoder/admin_cmd.c+2309 AER_Polling_SMART_Critical_Warning_bit(), nvme/apl/decoder/admin_cmd.c+2347 AER_Polling_SMART_Critical_Warning_bit()

```c
		list_del_init(&req->entry);
		ctrlr->aer_outstanding--;
		evt_set_imt(evt_aer_out, (u32)req, 0);
	}
}
/*
 *@for aer polling smart critical warning bit
*/
slow_code void AER_Polling_SMART_Critical_Warning_bit(u8 smart_warning){

	enum nvme_event_smart_critical_en_type warning = SMART_CRITICAL_BELOW_AVA_SPARE;
	u8 smart_en = ctrlr->cur_feat.aec_feat.b.smart;
#ifdef AER_Polling
	extern smart_statistics_t *smart_stat;
	u8 smart_warning = smart_stat->critical_warning.raw;
#endif
	//feature support and critical warning set
	nvme_apl_trace(LOG_INFO, 0x852e, "AER smarten: %d smart warning: %d",smart_en,smart_warning);
	if((smart_en & smart_warning) != 0){
		u8 aer_out = smart_en & smart_warning;
		bool flag = false;

		if((aer_out & BELOW_AVA_SPARE_MASK) != 0){//no space
			warning = SMART_STS_SPARE_THRESH;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & TEMPR_EXCEED_THR_MASK) != 0)	{//temp tho
			warning = SMART_STS_TEMP_THRESH;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & INTERNAL_ERR_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & READ_ONLY_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & BACKUP_FAILED_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}
		if((aer_out & PMR_RD_ONLY_MASK) != 0){
			warning = SMART_STS_RELIABILITY;
			flag = true;
			goto AER_IN;
		}

AER_IN:
		if(flag == true)
			nvme_apl_trace(LOG_INFO, 0x1bfd, "sub type: %d\n",warning);
			nvmet_evt_aer_in(((NVME_EVENT_TYPE_SMART_HEALTH << 16)|warning),0);

	}
#ifdef AER_Polling
	mod_timer(&AER_timer, jiffies + 2*HZ);
#endif
}

ddr_code void nvmet_aer_unmask_for_getlogcmd(Get_Log_CDW10 cdw10){
	//Get_Log_CDW10 cdw10 = (Get_Log_CDW10)cmd->cdw10;
	enum nvme_asynchronous_event_type type;
	//enum nvme_event_notice_status_type sts;
	u8 sts;
	//if(cmd->opc != NVME_OPC_GET_LOG_PAGE)
	//	return;

	if (cdw10.b.RAE != 0)
		return;

	switch(cdw10.b.lid){
		case NVME_LOG_ERROR:
			type = NVME_EVENT_TYPE_ERROR_STATUS;
			break;
```

### Snippet 6: L2642-L2682
- Reasons: nvme/apl/decoder/admin_cmd.c+2662 ipc_get_spare_avg_erase_cnt_done()

```c
		health->critical_warning.bits.read_only = is_system_read_only();
	}

	health_get_io_info(health);
	health_get_power_info(health);
	health_get_errors(health);
	health_get_temperature(health);

	health->critical_warning.raw = smart_stat->critical_warning.raw;

#ifdef SMART_PLP_NOT_DONE
	if(smart_stat->critical_warning.bits.epm_vac_err)
		health->critical_warning.bits.epm_vac_err = 1;
#endif

	if (health->critical_warning.raw & 0x7F)
	{
		nvme_apl_trace(LOG_ERR, 0xa702, "smart error %x", health->critical_warning.raw);
		//smart_stat->critical_warning.raw = health->critical_warning.raw;
#ifndef AER_Polling
		AER_Polling_SMART_Critical_Warning_bit(health->critical_warning.raw);
#endif
		if(pre_critical != health->critical_warning.raw){
		    flush_to_nand(EVT_CRITICAL_WARNING);
        }
	}
	pre_critical = health->critical_warning.raw & 0x7F;

#if 0//(_TCG_)
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	health->rdlocked_sts = mReadLockedStatus;
	health->wrlocked_sts = mWriteLockedStatus;
	health->tcg_sts      = mTcgStatus;
	health->prefmtted    = epm_aes_data->prefmtted;
	health->tcg_err_flag = epm_aes_data->tcg_err_flag;
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
```

### Snippet 7: L4857-L4897
- Reasons: nvme/apl/decoder/admin_cmd.c+4877 nvmet_get_log_page()

```c
	handle_result = nvmet_map_admin_prp(req, cmd, transfer, nvmet_admin_public_xfer_done);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(train_result, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, nvmet_admin_public_xfer_done);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return handle_result;
}

/*!
 * @brief NVMe get misc log pages handler
 *
 * @param req	request
 * @param cmd	nvme command
 *
 * @return	command status
 */
static ddr_code enum cmd_rslt_t nvmet_get_log_page(req_t *req, struct nvme_cmd *cmd)
{
	u8 lid = cmd->cdw10;
	u16 numd = ((cmd->cdw10 >> 16) & 0x0FFF);
	u16 numd_bytes = (numd + 1) << 2;
	u16 numd_bytes_g = cmd ->cdw12;
    u32 lpol = cmd->cdw12;
	u32 lpou = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;
//    nvme_apl_trace(LOG_ERR, 0, "nvme_vsc_ev_log cdw10:(%d) cdw11:(%d) cdw12:(%d) cdw13:(%d) cdw14:(%d) cdw15:(%d)", cmd->cdw10, cmd->cdw11, cmd->cdw12, cmd->cdw13, cmd->cdw14, cmd->cdw15);
	nvme_apl_trace(LOG_INFO, 0x28c0, "GetLogPage: LID(%d) NUMD(%d) PRP1(%x) PRP2(%x)",
				   lid, numd, cmd->dptr.prp.prp1, cmd->dptr.prp.prp2);
    if(lid == 0xD6 || lid == 0xD7)
    {
        switch (lid)
        {
        #if !defined(PROGRAMMER)
```

## ncl_20/epm.c

- Resolved path: `src\ncl_20\epm.c`
- Hits: 8 (deduped), Snippets: 6 (merged)

### Snippet 1: L90-L139
- Reasons: ncl_20/epm.c+110 epm_init(), ncl_20/epm.c+119 epm_init()

```c
epm_remap_tbl_t *epm_remap_tbl;
#endif
share_data epm_info_t *shr_epm_info; //epm.c init
share_data volatile bool OTF_WARM_BOOT_FLAG;

fast_data u8 evt_call_epm_update = 0xFF;

u32 EpmMetaIdx; //epm metadata
void *EpmMeta;

ddr_code void epm_evt_update(u32 p0, u32 p1, u32 type)
{
	if(type >= FTL_sign && type < EPM_PLP_end)
		epm_update(type, (CPU_ID - 1));
}


ddr_code void epm_init()
{
	epm_back_counter = 0;
	epm_plp_flag = false;
    
    extern u8 epm_Glist_updating;
    epm_Glist_updating = 0;
	GLIST_sign_flag = 0;
    
	epm_io_header_cmd[0].ncl_cmd.completion = NULL;
	epm_io_header_cmd[1].ncl_cmd.completion = NULL;
#if epm_debug
	epm_debug_log = 1;
	ncl_cmd_trace(LOG_ALW, 0xcf73, "epm debug enable");
#else
	epm_debug_log = 0;
	ncl_cmd_trace(LOG_ALW, 0x5706, "epm debug disable");
#endif

#if EPM_NOT_SAVE_Again	
    extern u8 EPM_NorShutdown;	
	EPM_NorShutdown = 0;
#endif	

	srb_t *srb = (srb_t *)SRAM_BASE;
	ncl_cmd_trace(LOG_ALW, 0xba1f, "epm_init");
	u32 i = 0;
	//init meta space
	EpmMeta = idx_meta_allocate(1, DDR_IDX_META, &EpmMetaIdx);
	//allocate dram space to shr_epm_info first
	//u32 shr_epm_info_ddtag = ddr_dtag_register(1);
	u32 shr_epm_info_ddtag = ddr_dtag_epm_register(1);
	shr_epm_info = (epm_info_t *)ddtag2mem(shr_epm_info_ddtag);
```

### Snippet 2: L274-L338
- Reasons: ncl_20/epm.c+294 epm_init(), ncl_20/epm.c+318 epm_init()

```c
		//memset(epm_misc_ptr, 0, (shr_epm_info->epm_misc.ddtag_cnt * DTAG_SZE));
		memset(epm_error_warn_ptr, 0, (shr_epm_info->epm_error_warn_data.ddtag_cnt * DTAG_SZE));
		//memset(epm_tcg_info_ptr, 0, (shr_epm_info->epm_tcg_info.ddtag_cnt * DTAG_SZE));
		
		//erase all
		for (i = 0; i < EPM_TAG_end; i++)
		{
			epm_erase(i);
		}
        
		epm_header_data->EPM_Head_tag = epm_head_tag;
		epm_header_data->EPM_SN = 0;

		epm_header_data->epm_header_last_pda = nal_rda_to_pda(&srb->epm_header_pos[0]);
		epm_header_data->epm_last_pda = nal_rda_to_pda(&srb->epm_pos[0]);
		epm_header_data->valid_tag = epm_valid_tag;
		epm_header_data->epm_header_mirror_mask = nal_rda_to_pda(&srb->epm_header_pos[0]) ^ nal_rda_to_pda(&srb->epm_header_mir_pos[0]);
		epm_header_data->epm_mirror_mask = nal_rda_to_pda(&srb->epm_pos[0]) ^ nal_rda_to_pda(&srb->epm_pos_mir[0]);

		epm_flush_all(ALL_FLUSH, NULL);
		epm_header_update();
	}
	else
	{
		bool epm_header_valid = false;
		u8 reflush = ALL_FLUSH;
		if (latest_epm_header_pda == invalid_epm)
		{
			ncl_cmd_trace(LOG_ALW, 0x8c42, "epm_header invalid, epm blk valid");
			epm_header_valid = false;
		}
		else
		{
			ncl_cmd_trace(LOG_ALW, 0x5300, "epm_header valid pda=0x%x", latest_epm_header_pda);
			epm_header_valid = epm_header_load(latest_epm_header_pda);
			//if read fail, retry and read mirror////
			if (epm_header_valid == false)
			{
				ncl_cmd_trace(LOG_ALW, 0x5f1b, "load latest epm_header fail");
				//panic("\n");
				/*
				ncl_cmd_trace(LOG_ALW, 0, "load latest epm_header fail, load mirror pda=0x%x", latest_epm_header_pda | epm_header_data->epm_header_mirror_mask);
				epm_header_valid = epm_header_load(latest_epm_header_pda | epm_header_data->epm_header_mirror_mask);

				if (epm_header_valid == false)
				{
					ncl_cmd_trace(LOG_ALW, 0, "load mirror latest epm_header fail, panic");
					panic("\n");
				}
				*/
			}
		}

		if ((epm_header_valid == true) && (epm_header_data->EPM_SN == max_epm_data_sn) && (epm_header_data->EPM_SN == max_epm_data_mirror_sn))
		{
			if (head_valid_status == ONLY_MASTER)
			{
				ncl_cmd_trace(LOG_ALW, 0x8f95, "only master");
				//reflush = ONLY_HEADER;
			}
			else
			{
				ncl_cmd_trace(LOG_ALW, 0x9e4b, "normal case");
			}

```

### Snippet 3: L384-L424
- Reasons: ncl_20/epm.c+404 epm_init()

```c
					epm_flush_all(ONLY_MIRROR, &only_mirror_pda);
					epm_erase(EPM_DATA_TAG);
					epm_flush_all(ONLY_MASTER, NULL);
				}
				else
				{
					ncl_cmd_trace(LOG_ALW, 0xa2db, "error case");
					panic("error case\n");
				}
			}

			epm_header_data->epm_header_last_pda = nal_rda_to_pda(&srb->epm_header_pos[0]);
			//pda_t only_mirror_head_pda = (nal_rda_to_pda(&srb->epm_header_pos[0])) | epm_header_data->epm_header_mirror_mask;
			if (reflush == ONLY_HEADER)
			{
				ncl_cmd_trace(LOG_ALW, 0x5aeb, "erase mirror head and reflush-ONLY_HEADER");
				//epm_erase(EPM_HEADER_MIRROR_TAG);
				//epm_header_flush(ONLY_MIRROR, only_mirror_head_pda, false);
				epm_erase(EPM_HEADER_TAG);
				epm_header_flush(ONLY_MASTER, 0, false);
			}
			else
			{
				//reflush header
				epm_erase(EPM_HEADER_TAG);
				epm_header_flush(ONLY_MASTER, 0, true);
				//epm_erase(EPM_HEADER_MIRROR_TAG);
				//epm_header_flush(ONLY_MIRROR, only_mirror_head_pda, false);
			}
		}
	}
}

	evt_register(epm_evt_update, 0, &evt_call_epm_update);
	//epm_update(ERROR_WARN_sign, (CPU_ID - 1));
	dump_error_warn_info();
	ncl_cmd_trace(LOG_ALW, 0xd51a, "epm_init done");
	
#ifdef ERRHANDLE_GLIST
    gl_build_table();
    
```

### Snippet 4: L482-L522
- Reasons: ncl_20/epm.c+502 force_set_srb_rda()

```c
{
	srb_t *srb = (srb_t *)SRAM_BASE;
	//epm header pos
	srb->epm_header_pos[0].row = 0x00000200;
	srb->epm_header_pos[0].ch = 0x5;
	srb->epm_header_pos[0].dev = 0;
	srb->epm_header_pos[0].du_off = 0;
	srb->epm_header_pos[0].pb_type = 0;
	srb->epm_header_pos[1].row = 0x00000000;
	srb->epm_header_pos[1].ch = 0x0;
	srb->epm_header_pos[1].dev = 0;
	srb->epm_header_pos[1].du_off = 0;
	srb->epm_header_pos[1].pb_type = 0;

	srb->epm_header_mir_pos[0].row = 0x00000000;
	srb->epm_header_mir_pos[0].ch = 0;
	srb->epm_header_mir_pos[0].dev = 0;
	srb->epm_header_mir_pos[0].du_off = 0;
	srb->epm_header_mir_pos[0].pb_type = 0;
	srb->epm_header_mir_pos[1].row = 0x00000000;
	srb->epm_header_mir_pos[1].ch = 0;
	srb->epm_header_mir_pos[1].dev = 0;
	srb->epm_header_mir_pos[1].du_off = 0;
	srb->epm_header_mir_pos[1].pb_type = 0;
	//epm pos
	srb->epm_pos[0].row = 0x00000000;
	srb->epm_pos[0].ch = 0x6;
	srb->epm_pos[0].dev = 0;
	srb->epm_pos[0].du_off = 0;
	srb->epm_pos[0].pb_type = 0;
	srb->epm_pos[1].row = 0x00000200;
	srb->epm_pos[1].ch = 0x6;
	srb->epm_pos[1].dev = 0;
	srb->epm_pos[1].du_off = 0;
	srb->epm_pos[1].pb_type = 0;
	srb->epm_pos[2].row = 0x00000000;
	srb->epm_pos[2].ch = 0x0;
	srb->epm_pos[2].dev = 0;
	srb->epm_pos[2].du_off = 0;
	srb->epm_pos[2].pb_type = 0;
	srb->epm_pos[3].row = 0x00000000;
```

### Snippet 5: L917-L957
- Reasons: ncl_20/epm.c+937 scan_the_latest_epm_ramap()

```c
		else
			*pda_base = epm_remap_tbl->remap_tbl_blk[0];*/
		*pda_base = epm_remap_tbl->remap_tbl_blk[0];
		mirror_erase = 1; //mirror also need to erase before write
		status = epm_ncl_cmd(pda_base, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0x7543, "remap erase error");
	}
	epm_remap_get_pda(pda_base, epm_io_cmd.pda_list, 1);
	epm_remap_tbl->remap_last_pda = *pda_base ;
	ncl_cmd_trace(LOG_ALW, 0x2162, "get_remap_pda[0x%x]", epm_io_cmd.pda_list[0]);
	chk_pda(epm_io_cmd.pda_list[0]);
	for (i = 0; i < DU_CNT_PER_PAGE; i++)
	{
		epm_io_cmd.bm_pl_list[i].pl.dtag = (DTAG_IN_DDR_MASK | (shr_epm_info->epm_remap_tbl_info_ddtag + (i)));
		epm_io_cmd.bm_pl_list[i].pl.du_ofst = i;
		epm_io_cmd.bm_pl_list[i].pl.btag = 0;
		epm_io_cmd.bm_pl_list[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_DDR_IDX;
		epm_io_cmd.bm_pl_list[i].pl.nvm_cmd_id = EpmMetaIdx;
	}
	epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE, 0);

	//write mirror
	epm_io_cmd.pda_list[0] |= epm_remap_tbl->remap_tbl_mirror_mask;
	ncl_cmd_trace(LOG_ALW, 0xe691, "get_mir_remap_pda[0x%x]", epm_io_cmd.pda_list[0]);
	if (mirror_erase == 1)
	{
		status = epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_ERASE, NULL, 1, DU_4K_DEFAULT_MODE, 0);
		if (status != 0)
			ncl_cmd_trace(LOG_ALW, 0xa6d4, "mirror remap erase error");
	}

	if (epm_debug_log)
		ncl_cmd_trace(LOG_ALW, 0xd544, "get mirror pda0=0x%x", epm_io_cmd.pda_list[0]);

	epm_ncl_cmd(epm_io_cmd.pda_list, NCL_CMD_OP_WRITE, epm_io_cmd.bm_pl_list, 1, DU_4K_DEFAULT_MODE, 0);
}

u32 scan_the_latest_epm_ramap(pda_t *the_latest_pda, pda_t *pda_base)
{
	pda_t remap_start_pda = 0;
```

### Snippet 6: L972-L1012
- Reasons: ncl_20/epm.c+992 scan_the_latest_epm_ramap()

```c
		if (pda2page(*pda_base) >= nand_page_num_slc())
		{
			ncl_cmd_trace(LOG_ALW, 0xfc02, "blk not enough change blk pda[0x%x]", *pda_base);
			chk_pda(*pda_base);
			break;
		}
		sts = epm_ncl_cmd(pda_base, NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
		//ncl_cmd_trace(LOG_ERR, 0, "read remap head pda[0x%x]\n",*pda_base);
		//chk_pda(*pda_base);
		if (sts == ficu_err_du_erased)
		{
			ncl_cmd_trace(LOG_ALW, 0xb567, "load epm remap empty sts=%d", sts);
			break;
		}
		else if (sts == 0)
		{
			if (*data != epm_head_tag)
			{
				ncl_cmd_trace(LOG_ALW, 0xe6c2, "remap tag fail");
				break;
			}
			epm_remap_sn = *(data + 1);
			remap_start_pda = *pda_base;
			epm_remap_get_pda(pda_base, epm_io_cmd.pda_list, DU_CNT_PER_PAGE);
			sts = epm_ncl_cmd(&epm_io_cmd.pda_list[(DU_CNT_PER_PAGE - 1)], NCL_CMD_OP_READ, &pl, 1, DU_4K_DEFAULT_MODE, 0);
			//ncl_cmd_trace(LOG_ERR, 0, "epm_io_cmd.pda_list[%d]=0x%x\n",(DU_CNT_PER_PAGE-1),epm_io_cmd.pda_list[(DU_CNT_PER_PAGE-1)]);
			//chk_pda(epm_io_cmd.pda_list[(DU_CNT_PER_PAGE-1)]);
			if (sts == ficu_err_du_erased)
			{
				ncl_cmd_trace(LOG_ALW, 0x1d9f, "load epm remap head valid tail empty");
				break;
			}
			else if (sts == 0)
			{
				valid_tag = *(data + ((4096 / 4) - 1));
				if (valid_tag == epm_valid_tag)
				{
					the_latest_sn = epm_remap_sn;	   //scan out SN
					*the_latest_pda = remap_start_pda; //scan out pda
				}
			}
```

## ftl/spb_mgr.c

- Resolved path: `src\ftl\spb_mgr.c`
- Hits: 8 (deduped), Snippets: 3 (merged)

### Snippet 1: L265-L305
- Reasons: ftl/spb_mgr.c+285 FTL_CACL_QBTPBT_CNT()

```c
{
	u32 spb_size_in_du = 0;
	u32 gb = get_disk_size_in_gb();
	u32 real_gb;
	u32 l2P_table_size;
    u32 spb_size;

	extern bool unlock_power_on;
	extern u16 global_capacity;
	extern u32 delay_cycle;
	global_capacity = gb;


	if (gb == 2048) {
        real_gb = 1920;
		delay_cycle = 10400; //13us
    } else if (gb == 1024) {
        real_gb = 960;
		delay_cycle = 14400; //18us
    } else {
        real_gb = 480;
		delay_cycle = 29600; //37us
    }

	ftl_apl_trace(LOG_ERR, 0xaa34, "[FTL]get_disk_size_in_gb():%u, cal capacity gb:%u, QD1 delay_cycle:%u", gb, real_gb, delay_cycle);
	_max_capacity = calc_jesd218a_capacity(real_gb);// LDA CNT
	spb_size_in_du = get_du_cnt_in_native_spb();// TLC
#if RAID_SUPPORT
	spb_size_in_du *= shr_nand_info.lun_num * shr_nand_info.geo.nr_planes - 1;
	spb_size_in_du /= shr_nand_info.lun_num * shr_nand_info.geo.nr_planes;
#endif

#if 0//def Dynamic_OP_En, change to lba mode
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	//ftl_apl_trace(LOG_ERR, 0, "Ocan0 _max_capacity %x, OPFlag %d \n", _max_capacity, epm_ftl_data->OPFlag);
    u32 DyOP_gb;

	//DYOPCapacity = _max_capacity;
	if(epm_ftl_data->OPFlag == 1)
	{
```

### Snippet 2: L315-L415
- Reasons: ftl/spb_mgr.c+335 FTL_CACL_QBTPBT_CNT(), ftl/spb_mgr.c+343 FTL_CopyFtlBlkDataFromBuffer(), ftl/spb_mgr.c+348 FTL_CopyFtlBlkDataFromBuffer(), ftl/spb_mgr.c+350 FTL_CopyFtlBlkDataFromBuffer(), ftl/spb_mgr.c+395 spb_poh_init()

```c
		}
		ftl_apl_trace(LOG_ERR, 0xa3c0, "Ocan2 Capa_OP14 %x, DYOPCapacity %x, DyOP_gb %d, OPValue %d \n", epm_ftl_data->Capa_OP14, DYOPCapacity, DyOP_gb, epm_ftl_data->OPValue);
	}
	else if((epm_ftl_data->OPFlag == 0) && (epm_ftl_data->Capa_OP14 == 0))
	{
		epm_ftl_data->Capa_OP14 = (real_gb * (14 + 100) + 100)/100 ;   // OP = (SSD Phy Cap. - User Cap.)/ User Cap.  14% 3.84TB ==> SSD Phy Cap. 4377.6GB
 		ftl_apl_trace(LOG_ERR, 0x3139, "Ocan3 Capa_OP14 %x, _max_capacity %x \n", epm_ftl_data->Capa_OP14, _max_capacity);
		epm_update(FTL_sign,(CPU_ID-1));
	}
#endif

    l2P_table_size = _max_capacity;
	spb_size = spb_size_in_du * (NAND_DU_SIZE/sizeof(pda_t));
	CAP_NEED_SPBBLK_CNT = occupied_by(l2P_table_size, spb_size_in_du);
    CAP_NEED_PHYBLK_CNT = occupied_by(l2P_table_size, shr_nand_info.geo.nr_pages * DU_CNT_PER_PAGE);
	shr_ftl_smart.capability_alloc_spb = CAP_NEED_SPBBLK_CNT;
	shr_ftl_smart.model = real_gb;
	QBT_BLK_CNT = occupied_by(l2P_table_size, spb_size);

	u32 unlock_power_on_min_blk = CAP_NEED_PHYBLK_CNT + spb_pool_get_ttl_spb_cnt(0) * RAID_SUPPORT
	                              + 8 * (shr_nand_info.interleave - RAID_SUPPORT);  //need more 8 free SPB (4+1 read_only + 3 host/pbt/gc)
	if(shr_ftl_smart.good_phy_spb < unlock_power_on_min_blk)
		unlock_power_on = true;

    ftl_apl_trace(LOG_ERR, 0xfcbb, "[FTL]_max_capacity %d QBT cal cnt %d Min spb cnt %d, need phy blk cnt: %u, unlock %d|%d",
    	_max_capacity, QBT_BLK_CNT, CAP_NEED_SPBBLK_CNT, CAP_NEED_PHYBLK_CNT,unlock_power_on_min_blk,unlock_power_on);
}
fast_code void FTL_CopyFtlBlkDataFromBuffer(u32 type)
{
    u32 dtag_start_idx = GET_BLKLIST_START_DTAGIDX(type);
    u8 *FtlBlkDataBuffer = (u8 *)ddtag2mem(dtag_start_idx);

	ftl_apl_trace(LOG_ALW, 0xa40c, "[FTL]copy data from buffer idx 0x%x", dtag_start_idx);
    memcpy((u8*)spb_info_tbl,        (u8*)FtlBlkDataBuffer,   SIZE_BLKLISTTBL);
    memcpy((u8*)_spb_mgr.spb_desc,   (u8*)(FtlBlkDataBuffer + OFFSET_SPBDESC),   SIZE_SPBDESC);
	memcpy((u8*)&_spb_mgr.pool_info, (u8*)(FtlBlkDataBuffer + OFFSET_POOL_INFO), SIZE_POOL_INFO);
    memcpy((u8*)&gFtlMgr,            (u8*)(FtlBlkDataBuffer + OFFSET_MANAGER),   SIZE_MANAGER);
	ftl_apl_trace(LOG_ALW, 0xdcc7, "[FTL]GPgsn 0x%x%x", (u32)(gFtlMgr.GlobalPageSN >> 32), (u32)(gFtlMgr.GlobalPageSN));
	memcpy((u8*)&VU_FLAG,            (u8*)(FtlBlkDataBuffer + MISC_INFO),   SIZE_MISC_INFO);
	ftl_apl_trace(LOG_ALW, 0x84c2, "VU FLAG VALUE RETORE");
}
fast_code void FTL_CopyFtlBlkDataToBuffer(u32 type)
{
	u32 dtag_start_idx = GET_BLKLIST_START_DTAGIDX(type);
	u8 *FtlBlkDataBuffer = (u8*)ddtag2mem(dtag_start_idx);;
	//ftl_apl_trace(LOG_ALW, 0, "[FTL]copy data to buffer dtag_start_idx 0x%x", dtag_start_idx);
	memcpy((u8*)FtlBlkDataBuffer,                      (u8*)spb_info_tbl,        SIZE_BLKLISTTBL);
	memcpy((u8*)(FtlBlkDataBuffer + OFFSET_SPBDESC),   (u8*)_spb_mgr.spb_desc,   SIZE_SPBDESC);
	memcpy((u8*)(FtlBlkDataBuffer + OFFSET_POOL_INFO), (u8*)&_spb_mgr.pool_info, SIZE_POOL_INFO);
    memcpy((u8*)(FtlBlkDataBuffer + OFFSET_MANAGER),   (u8*)&gFtlMgr,            SIZE_MANAGER);
	//ftl_apl_trace(LOG_ALW, 0, "[FTL]copy data to buffer end");

	memcpy((u8*)(FtlBlkDataBuffer + MISC_INFO),        (u8*)&VU_FLAG,    SIZE_MISC_INFO);
	//ftl_apl_trace(LOG_ALW, 0, "VU FLAG VALUE BACK UP");
}

ddr_code void spb_scan_over_temp_blk(void *data)
{
    //ftl_apl_trace(LOG_INFO, 0, "[FTL]Check Temperature");
	if (temperature >= (MIN_TEMP + 10) && temperature <= (MAX_TEMP - 15)) // check SPB_INFO_F_OVER_TEMP need GC in normal temperature
	{
		extern volatile u8 plp_trigger;
		u16 spb_id = _spb_mgr.pool_info.head[SPB_POOL_USER];
        while (spb_id != INV_U16 && !plp_trigger)
        {
            if (spb_info_get(spb_id)->flags & SPB_INFO_F_OVER_TEMP)
            {
            	spb_set_flag(spb_id, SPB_DESC_F_OVER_TEMP_GC);
                spb_mgr_rd_cnt_upd(spb_id);
				break;
            }
			spb_id = spb_info_tbl[spb_id].block;
        }
	}
    mod_timer(&spb_scan_over_temp_blk_timer, jiffies + HZ * 10); // Set 10s delay
}

ddr_code void spb_poh_init(void)
{
    u16 spb_cnt = get_total_spb_cnt();
    u16 i;
    if(spb_info_tbl[0].poh != POHTAG)
    {

        ftl_apl_trace(LOG_INFO, 0x3b8b, "[POH]Reset POH table");

        for (i = srb_reserved_spb_cnt; i < spb_cnt; i++)
        {
            if(spb_info_tbl[i].pool_id == SPB_POOL_USER)
            {
                spb_info_tbl[i].poh = poh;
            }
            else
            {
                spb_info_tbl[i].poh = INV_U32;
            }
        }
        spb_info_tbl[0].poh = POHTAG;
    }
}

```

### Snippet 3: L590-L670
- Reasons: ftl/spb_mgr.c+610 spb_mgr_init(), ftl/spb_mgr.c+650 spb_mgr_init()

```c
//	u32 i;
//	spb_desc_t *spb_desc;

	memset(desc->ptr, 0, desc->total_size);
//	spb_desc = (spb_desc_t *) desc->ptr;
//	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt(); i++) {
//		spb_desc[i].pool_id = spb_info_get(i)->pool_id;
//	}
}

ddr_code void spb_mgr_init(ftl_initial_mode_t mode)
{
	u32 sz;
	sz = sizeof(u16) * get_total_spb_cnt();

	if(mode != FTL_INITIAL_PREFORMAT)
	{
		sys_log_desc_register(&_spb_mgr.slog_desc, get_total_spb_cnt(), sizeof(spb_desc_t), spb_desc_init);
		_spb_mgr.sw_flags = sys_malloc(FAST_DATA, sz);
		sys_assert(_spb_mgr.sw_flags);

		evt_register(spb_allocation_handler, 0, &_spb_mgr.evt_spb_alloc);
		evt_register(spb_mgr_desc_delay_flush, 0, &_spb_mgr.evt_flush_desc);
		evt_register(spb_special_erase, 0, &_spb_mgr.evt_special_erase_spb);
		ftl_apl_trace(LOG_INFO, 0x7408, "[FTL] poweron spb_mgr_init");
	}

	memset(_spb_mgr.sw_flags, 0, sz);

	QSIMPLEQ_INIT(&_spb_mgr.appl_queue);

	_spb_mgr.flags.all = 0;

	fsm_queue_init(&_spb_mgr.allocate_wait_que);

	_spb_mgr.ttl_open = 0;

#if (SPB_BLKLIST == mENABLE)
	{
		u8 i;
		host_spb_close_idx = INV_U16;
		gc_spb_close_idx = INV_U16;
		host_spb_last_idx = INV_U16;
		gc_spb_last_idx = INV_U16;
		shr_shutdownflag = false;
		for (i = 0; i < SPB_POOL_MAX; i++) {
			_spb_mgr.pool_info.head[i] = INV_U16;
			_spb_mgr.pool_info.tail[i] = INV_U16;
			_spb_mgr.pool_info.free_cnt[i] = 0;
			_spb_mgr.pool_info.open_cnt[i] = 0;
		}
		CBF_INIT(&close_host_blk_que);
		CBF_INIT(&close_gc_blk_que);
		pbt_query_ready			= 1;
		pbt_query_need_resume 	= 0;
		for (i = 0; i<2; i++){
			blklist_flush_query[i] = false;
			blklist_tbl[i].type = INV_U16;
		}
	}
#endif

	if(mode == FTL_INITIAL_PREFORMAT)
	{
		ftl_apl_trace(LOG_INFO, 0xf461, "[FTL] preformat spb_mgr_init");
	}

    _spb_mgr.poh_timer.function = ChkDR;
    _spb_mgr.poh_timer.data = "ChkDR";
    mod_timer(&_spb_mgr.poh_timer, jiffies + 6000*HZ/10);
#if CROSS_TEMP_OP
    temperature = 45; // init temperature 45°C
    cpu_msg_register(CPU_MSG_SET_SPB_OVER_TEMP_FLAG, set_spb_over_temp_flag);
    spb_scan_over_temp_blk_timer.function = spb_scan_over_temp_blk;
    spb_scan_over_temp_blk_timer.data = NULL;
    mod_timer(&spb_scan_over_temp_blk_timer, jiffies + HZ * 20); // Set 20s delay
    is_scan_over_temp_timer_del = false;
#endif
}

ddr_code void set_spb_over_temp_flag(volatile cpu_msg_req_t *req)
```

## ftl/ftl.c

- Resolved path: `src\ftl\ftl.c`
- Hits: 8 (deduped), Snippets: 6 (merged)

### Snippet 1: L941-L993
- Reasons: ftl/ftl.c+961 get_avg_erase_cnt(), ftl/ftl.c+973 get_avg_erase_cnt_1()

```c
				}
			}
		}

		CBF_REMOVE_HEAD(&l2p_updt_que);
	}
}

fast_code void get_avg_erase_cnt(u32 *avg_erase, u32 *max_erase, u32 *min_erase, u32 *total_ec)
{
	u32 i;
	u32 sum = 0;
	u32 spb_cnt = 0;

    *max_erase = 0;
    *min_erase = INV_U32;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	for (i = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#else
	for (i = srb_reserved_spb_cnt; i < get_total_spb_cnt() - tcg_reserved_spb_cnt; i++) 
#endif
	{
		u8 id = spb_get_poolid(i);

		if (!(id == SPB_POOL_UNALLOC)) {
			sum += spb_info_get(i)->erase_cnt;
			spb_cnt++;
            if(*max_erase < spb_info_get(i)->erase_cnt)
            {
                *max_erase = spb_info_get(i)->erase_cnt;
//				shr_max_erase = spb_info_get(i)->erase_cnt;
                max_ec_blk = i;
            }
            if(*min_erase > spb_info_get(i)->erase_cnt)
            {
                *min_erase = spb_info_get(i)->erase_cnt;
//				shr_min_erase = spb_info_get(i)->erase_cnt;
            }
		}
	}
    *total_ec = sum;
//	shr_total_ec = sum;
	*avg_erase = sum / spb_cnt;
//	shr_avg_erase = sum / spb_cnt;

    ftl_apl_trace(LOG_INFO, 0xe82b, "A: %d, Max: %d, Min: %d, t: %d", *avg_erase, *max_erase, *min_erase, *total_ec);
}
share_data_zi volatile u16 ps_open[3];

ddr_code void get_avg_erase_cnt_1(u32 flags, u32 vu_sm_pl)
{
//	u64 init_time_start = get_tsc_64();
```

### Snippet 2: L1028-L1068
- Reasons: ftl/ftl.c+1048 ftl_set_spb_query()

```c
	{
		u8 id = spb_get_poolid(i);

		if (!(id == SPB_POOL_UNALLOC || id == SPB_POOL_QBT_ALLOC || id == SPB_POOL_QBT_FREE))
        {
			sum += (PHY_BLK_SIZE*256* spb_info_get(i)->erase_cnt);
		}
	}

    scan_written = ((sum >> 5) && 0xFFFFFFFF);
    //cpu_msg_issue(CPU_BE - 1, CPU_MSG_INIT_WRITTEN, 0, scan_written);
    //extern tencnet_smart_statistics_t *tx_smart_stat;
	//tx_smart_stat->nand_bytes_written = scan_written;

    ftl_apl_trace(LOG_INFO, 0x8990, "scan_written: %d", scan_written);

}
fast_code void ftl_set_spb_query(u32 nsid, u32 type)
{
	ftl_ns_t *ns = ftl_ns_get(nsid);
	bool empty = CBF_EMPTY(ns->spb_queue[type]);

    if (plp_trigger)
    {
        if (!(nsid == FTL_NS_ID_START && type == FTL_CORE_NRM))
        {
            return;
        }
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
        else if(SLC_init == true)
        {
            return;
        }
#endif
    }
	if (empty && (ns->flags.b.spb_queried & (1 << type)) == 0) {
		if(nsid == 2 && type == 2){
			if(pbt_query_ready == false){
				if(pbt_query_need_resume == false){
					pbt_query_need_resume = true;	//fix TableSN update unpredictable issue
				}
```

### Snippet 3: L1764-L1804
- Reasons: ftl/ftl.c+1784 _ipc_ftl_flush_done()

```c
    shr_flag_vac_compare = error;
	shr_flag_vac_compare_result = mTRUE;
    #endif

	ftl_apl_trace(LOG_ALW, 0xb12b, "compare finish !!!! err:%d ", error);
	ftl_apl_trace(LOG_ALW, 0x6707, "Function time cost : %d us", time_elapsed_in_us(time_start));

    sys_free(SLOW_DATA, temp_vc_table);

    cpu_msg_issue(CPU_FE - 1, CPU_MSG_PLP_DONE, 0, 0);  //only for debug
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_ACTIVE_GC, 0, 0);
    //extern u8 cal_done;
    //cal_done = 1;

}


fast_code void ipc_ftl_core_ctx_done(volatile cpu_msg_req_t *req)
{
	ftl_core_ctx_t *ctx = (ftl_core_ctx_t *) req->pl;

	if (is_ptr_tcm_share((void *) ctx))
		ctx = (ftl_core_ctx_t *) tcm_share_to_local((void *) ctx);

	ctx->cmpl(ctx);
}

fast_code void ipc_spb_gc_done(volatile cpu_msg_req_t *req)
{
	u16 spb_id = req->cmd.flags;
	u32 free_du_cnt = req->pl;

	ftl_gc_done(spb_id, free_du_cnt);
}

fast_code void _ipc_ftl_flush_done(flush_ctx_t *ctx)
{
	flush_ctx_ipc_hdl_t *hdl = (flush_ctx_ipc_hdl_t *) ctx;
	flush_ctx_t *remote_ctx = (flush_ctx_t *) hdl->ctx.caller;
	/*
	if(hdl->tx == 0)
```

### Snippet 4: L1924-L1964
- Reasons: ftl/ftl.c+1944 ftl_format()

```c
	spb_mgr_init(mode);
	spb_pool_init();
}
#ifdef Dynamic_OP_En
extern u8 DYOP_FRB_Erase_flag;
#endif

#if (PLP_SUPPORT == 0) 
ddr_code void ftl_save_non_plp_ec_table(u8 method)
{
	ftl_apl_trace(LOG_INFO, 0xddb1, "[IN] ftl_save_non_plp_ec_table method:%d", method);
	spb_id_t spb_id;
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	if(method)//blist to epm
	{
		for(spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
		{
			if( spb_info_tbl[spb_id].erase_cnt  > epm_ftl_data->epm_ec_table[spb_id])
			{
				epm_ftl_data->epm_ec_table[spb_id] = spb_info_tbl[spb_id].erase_cnt;
			}
		}
	}
	else//epm to blist
	{
	    for(spb_id = 0; spb_id < shr_nand_info.geo.nr_blocks; spb_id++)
	    {
			if(epm_ftl_data->epm_ec_table[spb_id] > spb_info_tbl[spb_id].erase_cnt)
	    	{
				spb_info_tbl[spb_id].erase_cnt = epm_ftl_data->epm_ec_table[spb_id];
			}
	    }		
	}
}
#endif

ddr_code void ftl_format(format_ctx_t *ctx)
{

#if defined(ENABLE_L2CACHE)
	l2cache_flush_all();
```

### Snippet 5: L1970-L2041
- Reasons: ftl/ftl.c+1990 ftl_format(), ftl/ftl.c+2021 spb_preformat_erase_continue()

```c
	{
		ftl_ns_format(FTL_NS_ID_INTERNAL, false);
	}
	ftl_ns_format(ctx->b.ns_id, ctx->b.host_meta);

    ftl_apl_trace(LOG_ERR, 0xa343, "[Preformat] init spor_last_rec_blk_sn");
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    epm_ftl_data->pbt_force_flush_flag = 0;
    epm_ftl_data->record_PrevTableSN = 0;
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)     
    epm_ftl_data->spor_last_rec_blk_sn = INV_U32;
    epm_ftl_data->panic_build_vac = false;
    extern volatile u32 spor_qbtsn_for_epm;
    spor_qbtsn_for_epm = 0;
    
    epm_ftl_data->max_shuttle_gc_blk_sn = 0;
	epm_ftl_data->max_shuttle_gc_blk = INV_SPB_ID;
#endif
#if (PLP_SUPPORT == 0) 
    for(u8 i_idx = 0;i_idx<2;i_idx++) 
    { 
        epm_ftl_data->host_open_blk[i_idx] = INV_U16; 
        epm_ftl_data->gc_open_blk[i_idx]   = INV_U16; 
    } 
    for(u8 i=0;i<SPOR_CHK_WL_CNT;i++) 
    { 
        epm_ftl_data->host_open_wl[i] = INV_U16; 
        epm_ftl_data->host_die_bit[i] = 0; 
        epm_ftl_data->gc_open_wl[i]   = INV_U16; 
        epm_ftl_data->gc_die_bit[i]   = 0; 
    }  
	epm_ftl_data->host_aux_group 	  = INV_U16;
	epm_ftl_data->gc_aux_group   	  = INV_U16;

	epm_ftl_data->non_plp_gc_tag 	  = 0;
	epm_ftl_data->non_plp_last_blk_sn = INV_U32;

	ftl_save_non_plp_ec_table(1);//restore ec table    from blist to epm

    extern volatile u8 non_plp_format_type;
    if(non_plp_format_type != NON_PLP_PREFORMAT)
    	non_plp_format_type = NON_PLP_FORMAT; 

#endif 
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	epm_ftl_data->plp_slc_gc_tag = 0;
	epm_ftl_data->plp_pre_slc_wl = 0;
    epm_ftl_data->plp_slc_wl     = 0;
    epm_ftl_data->plp_slc_times  = 0;
    epm_ftl_data->slc_format_tag = SLC_ERASE_FORMAT_TAG;

	epm_ftl_data->plp_slc_disable = 0;
	epm_ftl_data->plp_last_blk_sn = INV_U32;
	epm_ftl_data->esr_lock_slc_block = false;
	extern volatile u32 max_usr_blk_sn;
	max_usr_blk_sn = INV_U32;
	ftl_apl_trace(LOG_ERR, 0xa593, "[SLC] epm_ftl_data->slc_format_tag:0x%x",epm_ftl_data->slc_format_tag);
#endif

    epm_update(FTL_sign, (CPU_ID-1));

	epm_error_warn_t* epm_error_warn_data = (epm_error_warn_t*)ddtag2mem(shr_epm_info->epm_error_warn_data.ddtag);
	if(epm_error_warn_data->need_init)
	{
		u16* epm_addr_p = &epm_error_warn_data->need_init;
		memset(epm_addr_p, 0x0 ,EPM_ERROR_WARN_SIZE*4);
    	epm_update(ERROR_WARN_sign, (CPU_ID-1));
	}
}

ddr_code void ftl_format_flush_done(flush_ctx_t *ctx)
{
```

### Snippet 6: L2061-L2101
- Reasons: ftl/ftl.c+2081 spb_preformat_erase_done()

```c
    	if((srb_reserved_spb_cnt <= epm_ftl_data->epm_fmt_not_finish && epm_ftl_data->epm_fmt_not_finish < get_total_spb_cnt() - tcg_reserved_spb_cnt ) || epm_ftl_data->epm_fmt_not_finish == 0xffffffff )
#endif
    	{
    		if(epm_ftl_data->epm_fmt_not_finish == 0xffffffff)//epm_fmt_not_finish == 0xffffffff previous format not finish(plp happend before erase block)
    		{
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
    			epm_ftl_data->epm_fmt_not_finish = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT;//skip psb block 0
#else
    			epm_ftl_data->epm_fmt_not_finish = srb_reserved_spb_cnt;//skip psb block 0
#endif
    		}

    		for(erase_index = epm_ftl_data->epm_fmt_not_finish;erase_index<get_total_spb_cnt() - tcg_reserved_spb_cnt;erase_index++){
                ftl_sblk_erase(erase_index);
    		}
#if(PLP_SUPPORT == 1)
            ftl_clear_vac_ec_in_epm();
#endif
    	}
    }
    else if ((epm_ftl_data->format_tag != FTL_FULL_TRIM_TAG) && ((epm_ftl_data->format_tag != 0) || (epm_ftl_data->epm_fmt_not_finish != 0)))
    {
        ftl_apl_trace(LOG_WARNING, 0xcbf2, "format may error!!!!!");
    
    }
}

void spb_preformat_erase(flush_ctx_t* flush);
ddr_code void spb_preformat_erase_done(erase_ctx_t *ctx)
{
	u32 spb_cnt = get_total_spb_cnt() - tcg_reserved_spb_cnt;
	flush_ctx_t* flush = (flush_ctx_t*) ctx->caller;
	sys_free(FAST_DATA, ctx);

	if(plp_trigger)
	{
        u32 first_erase_spb;
#if (PLP_SLC_BUFFER_ENABLE == mENABLE)
        first_erase_spb = srb_reserved_spb_cnt + CACHE_RSVD_BLOCK_CNT;
#else
        first_erase_spb = srb_reserved_spb_cnt;
```

## rtos/armv7r/ddr.c

- Resolved path: `src\rtos\armv7r\ddr.c`
- Hits: 8 (deduped), Snippets: 3 (merged)

### Snippet 1: L168-L226
- Reasons: rtos/armv7r/ddr.c+188 mem_scan_test(), rtos/armv7r/ddr.c+206 mem_scan_test()

```c

#if defined(MPC)
	spin_lock_release(SPIN_LOCK_KEY_DDR_WINDOWS);
#endif
}

#if CPU_ID_0 == 0
#if DDR_SCAN_TEST
static void mem_scan_test(u32 start, u32 end, char* str)
{
	start = start & (~3);
	u32 size = end - start;
	u32 i;
	u32 *ptr;
	u32 pat;
	u32 error = 0;

	size /= sizeof(u32);

	ptr = (u32*) start;
	rtos_ddr_trace(LOG_ERR, 0x3f8c, "cpu memory scan start %08x end %08x\n", start, end);

	pat = start;
	for (i = 0; i < size; i++) {
		ptr[i] = pat;
		pat += 4;
	}

	pat = start;
	for (i = 0; i < size; i++) {
		if (ptr[i] != pat) {
			rtos_ddr_trace(LOG_ERR, 0x4380, "%p expect %08x but %08x\n",
					&ptr[i], pat, ptr[i]);
			error++;
			sys_assert(0);
		}
		pat += 4;
	}
	rtos_ddr_trace(LOG_ERR, 0xe579, "%s memory scan error cnt %d\n", str, error);
}
#endif
/*
#ifdef FPGA
#define writelr(reg, val)	writel(val, (void *) reg)
#define readl(reg)		readl((void *) (reg))
static void ddr4_vu440_init(void)
{
	// MC_Init_Code_svn1317_Micron_DDR4_8g_2133_64bit_mc10M_ref40M.txt
	rtos_core_trace(LOG_ALW, 0, "DDR4 initializing");
	writelr(0xc0068004, 0x0);

	//phase 1 - phy init
	//mc
	writelr(0xc00601c8, 0x2120900);
	writelr(0xc00601d0, 0x00000000);
	//dfi
	writelr(0xc0064080, 0x1310);
	writelr(0xc0064080, 0x1310);
	writelr(0xc0064080, 0x1311);
```

### Snippet 2: L1299-L1339
- Reasons: rtos/armv7r/ddr.c+1319 Get_DDR_Size_From_LOADER_CFG()

```c
		ret = dfi_train_all(rainier_freq, 0, 3);
#endif

	sys_assert(ret == 0);
	ddr_info_buf.cfg.training_done = true;
	ddr_info_bkup(DDR_INFO_ALL, &ddr_info_buf);
}
#endif

#ifdef GET_CFG	//20200822-Eddie
init_code void Get_DDR_Size_From_LOADER_CFG(void)
{
	u64 size = 0;

#ifdef DDR_AUTOIZE_IN_SRB
	srb_t *srb = (srb_t *) SRAM_BASE;
#endif

	if (fw_config_main->header.signature == IMAGE_CONFIG){
		size = (u64)((1 << fw_config_main->board.ddr_size) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
		rtos_ddr_trace(LOG_INFO, 0x95e2, "DDR size : %d \n",(1 << fw_config_main->board.ddr_size) << 8);
		size = size << 20; // bytes
	}
#ifdef DDR_AUTOIZE_IN_SRB
	else if ((u8)srb->ddr_idx){		//20201028-Eddie
		size =(u64)((1 << (u8)srb->ddr_idx) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
		size = size << 20; // bytes
	}
#endif
	else
	{
		size = DDR_SIZE_RAINIER;   // 1024
		rtos_ddr_trace(LOG_ERR, 0x3718, "No DDR size data, set to default : %d \n", (size>>20));
	}

	ddr_set_capacity(size);
}
#endif
#if defined(DRAM_Error_injection)
void Error_injection_1bit(void)
{   
```

### Snippet 3: L1774-L1827
- Reasons: rtos/armv7r/ddr.c+1794 ddr_init_bypass_warmboot(), rtos/armv7r/ddr.c+1801 ddr_init_bypass_warmboot(), rtos/armv7r/ddr.c+1803 ddr_init_bypass_warmboot(), rtos/armv7r/ddr.c+1805 ddr_init_bypass_warmboot(), rtos/armv7r/ddr.c+1807 ddr_init_bypass_warmboot()

```c
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xd66d, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#else
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xe91e, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#else
	#ifdef EPM_DDTAG_ALLOC
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - ( ddr_epm_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x76ef, "ddr c = 0x%x, l2p c = 0x%x, epm c = 0x%x, max cnt = %d\n"
								,ddr_get_capapcity(), ddr_l2p_capacity, ddr_epm_capacity, max_ddr_sect_cnt);
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_epm = ( ddr_epm_capacity / DTAG_SZE);
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xe5bf, "DDR SECTION info.__ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
								,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
		rtos_ddr_trace(LOG_ERR, 0xb89f, "DDTAG info. __ddr_dtag_start 0x%x , __ddr_dtag_end 0x%x , max_ddr_dtag_cnt %d \n"
			, (u32) &__ddr_dtag_start, (u32) &__ddr_dtag_end, max_ddr_dtag_cnt);
		rtos_ddr_trace(LOG_ERR, 0xa953, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
								,ddr_dtag_free, ddr_dtag_next, ddr_dtag_free_l2p, ddr_dtag_next_l2p);
		rtos_ddr_trace(LOG_ERR, 0x441f, "DDR DTAG info. ddr_dtag_free_epm %d , ddr_dtag_next_epm %d", ddr_dtag_free_epm, ddr_dtag_next_epm);
	#else 
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x2f42, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#endif	
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
	ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
	ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_dtag_cnt;
#endif	
	rtos_ddr_trace(LOG_ERR, 0x621b, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
		,ddr_dtag_free,ddr_dtag_next,ddr_dtag_free_l2p,ddr_dtag_next_l2p);
	#ifdef L2P_FROM_DDREND
		ddr_dtag_next_from_end = (ddr_get_capapcity() / DTAG_SZE)-1;
		rtos_ddr_trace(LOG_ERR, 0x4c97, "ddr_dtag_next_from_end %d \n",ddr_dtag_next_from_end);
```

## ftl/ftl_l2p.c

- Resolved path: `src\ftl\ftl_l2p.c`
- Hits: 8 (deduped), Snippets: 6 (merged)

### Snippet 1: L182-L222
- Reasons: ftl/ftl_l2p.c+202 ftl_l2p_para_init()

```c
static void ftl_l2p_urgent_load_exec(void);

/*!
 * @brief execute background l2p load request
 *
 * @return		not used
 */
static void ftl_l2p_bg_load_exec(void);

/*!
 * @brief l2p read done handle
 *
 * @return		not used
 */
static void l2p_seg_read_done(struct ncl_cmd_t *ncl_cmd);

init_code void ftl_l2p_para_init(void)
{
	seg_size = DU_CNT_PER_PAGE * DTAG_SZE *3;
	shr_l2p_seg_sz = seg_size;
	shr_l2pp_per_seg = NR_L2PP_IN_SEG;
	ftl_apl_trace(LOG_ERR, 0x441d, "seg_size %d NR_L2PP_IN_SEG %d", seg_size, NR_L2PP_IN_SEG);
}

init_code static void ftl_l2p_load_init(void)
{
	u32 ddr_dtag;
	u32 dtag_required;

	memset(&l2p_load_mgr, 0, sizeof(l2p_load_mgr_t));
	dtag_required = occupied_by(shr_l2p_seg_bmp_sz, DTAG_SZE);

#ifdef L2P_DDTAG_ALLOC	//20201029-Eddie
	ddr_dtag = ddr_dtag_l2p_register(dtag_required);
#else
	ddr_dtag = ddr_dtag_register(dtag_required);
#endif
	l2p_load_mgr.ready_bmp = (u32*)ddtag2mem(ddr_dtag);
	shr_l2p_ready_bmp = l2p_load_mgr.ready_bmp;

#ifdef L2P_DDTAG_ALLOC
```

### Snippet 2: L294-L337
- Reasons: ftl/ftl_l2p.c+314 ftl_l2p_init(), ftl/ftl_l2p.c+317 ftl_l2p_init()

```c
		shr_vac_drambuffer_start  = ddr_dtag_l2p_register(shr_vac_drambuffer_need);
	#else
		shr_vac_drambuffer_start  = ddr_dtag_l2p_register(shr_vac_drambuffer_need);
		shr_blklistbuffer_start[0]  = DDR_BLIST_DTAG_START;
		shr_blklistbuffer_start[1]  = DDR_BLIST_DTAG_START+shr_blklistbuffer_need;
		shr_l2p_entry_start = ddr_dtag_l2p_register(shr_l2p_entry_need);
	#endif
#else
	shr_blklistbuffer_start  = ddr_dtag_register(shr_blklistbuffer_need);
/////////////////////////////////////////////////////////////////////
	shr_l2p_entry_start = ddr_dtag_register(shr_l2p_entry_need);
#endif
	sys_assert(shr_l2p_entry_start != ~0);


#ifdef L2P_DDTAG_ALLOC
	ddr_dtag_l2p_register_lock();
#endif

	ddr_dtag_register_lock();

	ftl_apl_trace(LOG_ALW, 0x50ac, "vac res got entry %d %x blklist %d %x/%x",
		shr_vac_drambuffer_need, shr_vac_drambuffer_start, shr_blklistbuffer_need,
		shr_blklistbuffer_start[0],shr_blklistbuffer_start[1]);
	ftl_apl_trace(LOG_ALW, 0x7386, "L2P res got entry %d %x seg_cnt %d", shr_l2p_entry_need, shr_l2p_entry_start, shr_l2p_seg_cnt);

	l2p_base = ddtag2off(shr_l2p_entry_start);
	l2p_mgr_ctrl(false, cap);
	l2p_mgr_buf_init(l2p_base, (u64)shr_l2p_entry_need * DTAG_SZE,
			0, 0,
			0, (u64)get_total_spb_cnt() * sizeof(u32), CNT_IN_INTB);
	l2p_mgr_init();
	_l2p_tbl.base = ddtag2off(shr_l2p_entry_start);
#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
	extern u64 l2p_base_addr;
	l2p_base_addr = _l2p_tbl.base;
#endif
	//ftl_apl_trace(LOG_ERR, 0, "_l2p_tbl.base 0x%x%x, _l2p_tbl.max_size 0x%x%x \n", (u32)(_l2p_tbl.base>>32), _l2p_tbl.base, (u32)(_l2p_tbl.max_size>>32), _l2p_tbl.max_size);
	// vcnt
	_l2p_tbl.vcnt_in_seg = occupied_by(get_total_spb_cnt() * sizeof(u32), seg_size);

    evt_register(warmboot_save_pbt_done, 0, &evt_wb_save_pb);
}

```

### Snippet 3: L369-L409
- Reasons: ftl/ftl_l2p.c+389 ftl_spb_vcnt_fet_done()

```c
	spb_set_flag(rslt->spb_id, SPB_DESC_F_CLOSED);
	//ftl_ns_inc_closed_spb(rslt->spb_id);

	if ((rslt->cnt == 0) && (spb_get_poolid(rslt->spb_id) != SPB_POOL_QBT_ALLOC)){
		#if (RELEASE_LOG_CTL == mENABLE)
		u64 release_last_tmp = get_tsc_64() - release_last;
		bool log_bypass = false;
		log_level_t old = 0x1;
		if(release_last_tmp < (500*CYCLE_PER_MS))	//<500ms bypass log
		{
			log_bypass = true;
			//ftl_apl_trace(LOG_INFO, 0, "%dms over",release_last_tmp);
		}
		force_empty = false;
		if(log_bypass){
			old = log_level_chg(LOG_PANIC);
			if(spb_get_free_cnt(SPB_POOL_FREE) > 20){
				force_empty = true;
			}			
		}

		ftl_apl_trace(LOG_INFO, 0xc336, "spb %d vcnt_srch %d, %x %x",
				rslt->spb_id, rslt->cnt,
				flags, sw_flags);	

		release_last = get_tsc_64();
		spb_set_sw_flag(rslt->spb_id, SPB_SW_F_VC_ZERO);
		spb_release(rslt->spb_id);
		if(log_bypass){
			log_level_chg(old);
		}
		force_empty = false;
		#else
		spb_set_sw_flag(rslt->spb_id, SPB_SW_F_VC_ZERO);
		spb_release(rslt->spb_id);		
		#endif		
	}

//	if (flags & SPB_DESC_F_WEAK) {
//		u32 nsid = spb_get_nsid(rslt->spb_id);
//
```

### Snippet 4: L435-L503
- Reasons: ftl/ftl_l2p.c+455 ftl_spb_vcnt_notify(), ftl/ftl_l2p.c+483 ftl_l2p_alloc()

```c
			if (get_gc_blk() == spb_id)
				spb_set_sw_flag(spb_id, SPB_SW_F_VC_ZERO); // delay release this SPB in gc_end
			else{
                last_spb_vcnt_zero = spb_id;
				#if (RELEASE_LOG_CTL == mENABLE)
				u64 release_last_tmp = get_tsc_64() - release_last;
				bool log_bypass = false;
				log_level_t old = 0x1;
				if(release_last_tmp < (500*CYCLE_PER_MS))	//<500ms bypass log
				{
					log_bypass = true;
					//ftl_apl_trace(LOG_INFO, 0, "%dms over",release_last_tmp);
				}
				force_empty = false;
				if(log_bypass){
					old = log_level_chg(LOG_PANIC);
					if(spb_get_free_cnt(SPB_POOL_FREE) > 20){
						force_empty = true;
					}
				}

                if (shr_format_copy_vac_flag)   // Precautions: If spb is used to store QBT and plp is triggered during QBT storage, the VAC table will go wrong after SPOR.
                {
                    force_empty = true;
                }
				ftl_apl_trace(LOG_INFO, 0x4291, "notify spb %d with %x(%x)",rslt->spb_id, flag,sw_flag);
				release_last = get_tsc_64();
				spb_release(spb_id);
				if(log_bypass){
					log_level_chg(old);
				}
				force_empty = false;
				#else
				spb_release(spb_id);		
				#endif		
			}
		}
	}
	else
	{
		// just ignore it
	}
}

init_code u64 ftl_l2p_alloc(u32 cap, u32 nsid, l2p_ele_t *ele)
{
	u32 seg_cnt;
	u32 lut_seg;
	u64 lut_sz;
	u64 ret;
	u32 sz = cap;

	seg_cnt = occupied_by(sz, seg_size/sizeof(pda_t));
    ftl_apl_trace(LOG_ERR, 0x0b7b, "[FTL]host real seg_cnt %d\n", seg_cnt);
	lut_seg = seg_cnt;
	lut_sz  = lut_seg * seg_size;

	ret = _l2p_tbl.base + _l2p_tbl.alloc_off;
	ele->lut = ret;
	ele->seg_off = 0;
	_l2p_tbl.alloc_off += lut_sz;
	ele->seg_end = seg_cnt;
	//ftl_apl_trace(LOG_ERR, 0, "ret %d lut_sz 0x%x alloc_off 0x%x max_size 0x%x\n", ret, lut_sz,_l2p_tbl.alloc_off ,_l2p_tbl.max_size);
	sys_assert(_l2p_tbl.alloc_off <= _l2p_tbl.max_size);

	return ret;
}
typedef struct _ftl_hns_t {
	ftl_ns_t *ns;		///< parent ftl namespace object
```

### Snippet 5: L554-L594
- Reasons: ftl/ftl_l2p.c+574 gc_re()

```c
		action->act = 0;// 2
		action->caller = NULL;
		action->cmpl = gc_action_done;

		if (gc_action(action))
		sys_free(FAST_DATA, action);
		else
		sys_free(FAST_DATA, action);
		}*/

		//if(lda_trim<(447*0x200000))
		l2p_updt_trim(lda_trim, 0, 1, 65536, 1, 87);
	}
	//internal_trim_flag=1;
}
fast_code void gc_re(void)//joe add for ns 2020121
{


	ftl_apl_trace(LOG_INFO, 0xc763, " gc deletens re\n");//joe add gc stop actiion 202011
	gc_action_t *action = sys_malloc(FAST_DATA, sizeof(gc_action_t));

	action->act = GC_ACT_RESUME;// 2
	action->caller = NULL;
	action->cmpl = gc_action_done;

#if(PLP_GC_SUSPEND == mDISABLE)
	if (gc_action2(action))
#else
	if (gc_action(action))
#endif
		sys_free(FAST_DATA, action);


}

fast_code void ftl_l2p_reset_partial(l2p_ele_t *ele,u16 seg_cnt) 
{ 
	u64 base = _l2p_tbl.base; 
	u64 len; 

```

### Snippet 6: L880-L920
- Reasons: ftl/ftl_l2p.c+900 l2p_seg_read_done()

```c
        }

        ftl_set_read_data_fail_flag(mTRUE);
#endif
        return;
    }

	//ftl_apl_trace(LOG_INFO, 0, "l2p_seg_read_done");
    ncl_cmd->flags &= ~NCL_CMD_L2P_READ_FLAG;  //prevent RD_FTL bit to be use for other ncl_cmd of read.

    _l2p_free_cmd_bmp |= (1ULL << idx);

	if (l2p_cmd->flags.b.misc_data == false) {
		int ret = test_and_set_bit(seg_id, load_mgr->ready_bmp);
		sys_assert(ret == 0);

		load_mgr->ttl_ready++;
		if (load_mgr->ttl_ready == shr_l2p_seg_cnt) {
			shr_ftl_flags.b.l2p_all_ready = true;
			ftl_apl_trace(LOG_INFO, 0x70ef, "all l2p ready");
		}
	}

	if (l2p_cmd->flags.b.bg_load || l2p_cmd->flags.b.urg_load) {
		load_mgr->otf_load--;
		int ret = test_and_clear_bit(seg_id, load_mgr->loading_bmp);
		sys_assert(ret == 1);

		l2p_bg_load_req_t *bg_load;
		l2p_urg_load_req_t *urg_load;
		struct list_head *curr, *saved;

		if ((seg_id >= load_mgr->seg_off) && (seg_id < load_mgr->seg_end)) {
			load_mgr->ready_cnt++;
		} else {
			list_for_each_safe(curr, saved, &load_mgr->bg_waiting_list) {
				bg_load = container_of(curr, l2p_bg_load_req_t, entry);
				if ((seg_id >= bg_load->seg_off) && (seg_id < bg_load->seg_end)) {
					bg_load->ready_cnt++;
					break;
				}
```

## tcg/tcg_if_nf_api.c

- Resolved path: `src\tcg\tcg_if_nf_api.c`
- Hits: 8 (deduped), Snippets: 1 (merged)

### Snippet 1: L340-L388
- Reasons: tcg/tcg_if_nf_api.c+360 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+361 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+362 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+363 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+364 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+365 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+366 tcg_nf_G4WrDefault(), tcg/tcg_if_nf_api.c+368 tcg_nf_G4WrDefault()

```c
ddr_code u8 tcg_nf_G4WrDefault(void)
{
	void *ptcg_ipc;

	if(plp_trigger)
		return true;

	if(tcgTmpBuf==NULL)
		return true;
	
	// need sycn G1~G3 and G1d~G3d in G4
	if(bTcgTblErr)
	{
		memcpy(tcgTmpBuf                                                                                                                     , (const void *)pG1, sizeof(tG1));
		memcpy(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE)                                                           , (const void *)pG2, sizeof(tG2));
		memcpy(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE), (const void *)pG3, sizeof(tG3));

		tG1 *pt1 = (tG1 *)tcgTmpBuf;
		tG2 *pt2 = (tG2 *)(tcgTmpBuf+NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE));
		tG3 *pt3 = (tG3 *)(tcgTmpBuf+NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE)+NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE));
		tcg_api_trace(LOG_INFO, 0x0639, "[TCG] for write default G4 table check");
		tcg_api_trace(LOG_INFO, 0xfc19, "[TCG] Buf G1 ver.0x%x", pt1->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x6092, "[TCG] Buf G1 end.0x%x", pt1->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x3d56, "[TCG] Buf G2 ver.0x%x", pt2->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0xced2, "[TCG] Buf G2 end.0x%x", pt2->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x5f21, "[TCG] Buf G3 ver.0x%x", pt3->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x2a35, "[TCG] Buf G3 end.0x%x", pt3->b.mEndTag);

		tcg_api_trace(LOG_INFO, 0xd68b, "[TCG] G1 ver.0x%x", pG1->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x77af, "[TCG] G1 end.0x%x", pG1->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x2d67, "[TCG] G2 ver.0x%x", pG2->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0x294a, "[TCG] G2 end.0x%x", pG2->b.mEndTag);
		tcg_api_trace(LOG_INFO, 0x2df2, "[TCG] G3 ver.0x%x", pG3->b.mTcgTblInfo.ver);
		tcg_api_trace(LOG_INFO, 0xeb72, "[TCG] G3 end.0x%x", pG3->b.mEndTag);

		tcg_nf_params.result = 0;
		tcg_nf_params.sync   = true;
		tcg_nf_params.op     = NCL_CMD_OP_WRITE;
		tcg_nf_params.laas   = TCG_G1_DEFAULT_LAA0;
		tcg_nf_params.laacnt = 5;

		ptcg_ipc = tcm_local_to_share((void *)(&tcg_nf_params));
		tcg_nf_Start(ptcg_ipc);
	}
	else
	{
		//tcg_nf_G4RdDefault();
	}

```

## dispatcher/rdisk.c

- Resolved path: `src\dispatcher\rdisk.c`
- Hits: 8 (deduped), Snippets: 7 (merged)

### Snippet 1: L2227-L2267
- Reasons: dispatcher/rdisk.c+2247 rdisk_format_done()

```c
 *
 * @return		None
 */
fast_code static void rdisk_com_free_updt_pasg(u32 param, u32 payload, u32 count)
{
	u32 *dtag = (u32 *)payload;
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	bool ra_in = false;
	#endif
	int i = 0;
	for (i = 0; i < count; i++)
	{
#if 0//((TCG_WRITE_DATA_ENTRY_ABORT) && defined(TCG_NAND_BACKUP))
		if(otf_tcg_mbr_dtag)
		{
			u32 dtag_buffer_tcg = mem2ddtag(tcgTmpBuf);
			if((dtag[i] & DTAG_IN_DDR_MASK) && (dtag[i] >= dtag_buffer_tcg))
			{
				//disp_apl_trace(LOG_ERR, 0, "[TCG] otf_tcg_mbr_dtag: %d, Put dtag: 0x%x", otf_tcg_mbr_dtag, dtag[i]);
				--otf_tcg_mbr_dtag;
				if(otf_tcg_mbr_dtag == 0)
				{
					//mbr_rd_cid = 0xFFFF;
					disp_apl_trace(LOG_INFO, 0x08f1, "[TCG] MBR reset cid: %d", mbr_rd_cid);
				}
				continue;
			}
		}
#endif
		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		if (!is_ra_dtag(dtag[i]))
		{
			rdisk_dref_dec(&dtag[i], 1);
		}
		else
		{
			ra_in = true;
		}
		#else
		rdisk_dref_dec(&dtag[i], 1);
		#endif
```

### Snippet 2: L2482-L2522
- Reasons: dispatcher/rdisk.c+2502 rdisk_trim()

```c
    				RecordRangeIdx = trim_data->Validcnt;
    				slda = (slba + lbanum - 1) >> (LDA_SIZE_SHIFT - host_sec_bitz);
    				eldanext = (nlb + slba) >> (LDA_SIZE_SHIFT - host_sec_bitz);
    				count = eldanext - slda;
#if 1 //(TRIM_DEBUG ==ENABLE)
    				disp_apl_trace(LOG_INFO, 0xeeed, "[Trim] NSID|%d  LBA|0x%x-%x NLB|0x%x-%x", ns_id, (u32)(slba >> 32), (u32)slba, (u32)(nlb >> 32), (u32)nlb);
#endif
    				trim_data->Ranges[RecordRangeIdx].sLDA = slda;
    				trim_data->Ranges[RecordRangeIdx].Length = count;
                    if(count)
    				    trim_data->Validcnt++;
    			}
    			slba = Nextlba;
    			nlb = 0;
    		}
    	}

        #ifdef ERRHANDLE_ECCT
        if(ecct_cnt)
        {
            rdisk_ECCT_op(slba, nlb, VSC_ECC_unreg);
        }
        #endif


    	nlb = dsmr[list_cnt - 1].length + nlb;
        // #if NS_MANAGE
        //     UpdtTrimInfo_with_NS(slba, nlb,req->nsid);
        // #else
        //     UpdtTrimInfo(slba, nlb, Register);
        // #endif



        #if(BG_TRIM == ENABLE)
        if(host_sec_bitz == 9){
            if((TrimUnalignLDA.LBA != INV_U64)&&(TrimUnalignLDA.LBA == slba)){
                slba = slba & ~(7);
                nlb += TrimUnalignLDA.LBA&7;
            }
            u64 tail = (slba+nlb)&7;
```

### Snippet 3: L3994-L4034
- Reasons: dispatcher/rdisk.c+4014 rdisk_shutdown()

```c
	fctx->ctx.cmpl = rdisk_dtag_gc_done;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->flags.b.dtag_gc = 1;
	//ftl_core_flush(fctx);
	if (ftl_core_flush(fctx) == FTL_ERR_OK)
	{
		disp_apl_trace(LOG_INFO, 0x2650, "fctx:0x%x done", fctx);
		fctx->ctx.cmpl(&fctx->ctx);
	}
}

fast_code void rdisk_flush_done(ftl_core_ctx_t *ctx)
{
	evt_set_cs(evt_flush_done, (u32)ctx, 0, CS_TASK);
    //disp_apl_trace(LOG_INFO, 0xb151, "ctx:0x%x", ctx);
}

fast_code void evt_rdisk_flush_done(u32 param, u32 data, u32 r1)
{
    ftl_core_ctx_t *ctx = (ftl_core_ctx_t *)data;
    //disp_apl_trace(LOG_INFO, 0xb6ec, "ctx:0x%x", ctx);
	req_t *req = (req_t *)ctx->caller;
	ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;

	fctx->nsid = 0;
	list_del(&req->entry2);

	evt_set_imt(evt_cmd_done, (u32)req, 0);

	btn_de_wr_enable();
	bcmd_resume();
	req_resume();
}
//callback for set feature 06 VWC
fast_code void VWC_flush_done(ftl_core_ctx_t *ctx)
{

	ftl_flush_data_t *fctx = (ftl_flush_data_t *)ctx;
	req_t *req = (req_t *)ctx->caller;
	fctx->nsid = 0;
```

### Snippet 4: L4433-L4473
- Reasons: dispatcher/rdisk.c+4453 rdisk_power_loss_flush()

```c
    ucache_flush_flag = true;
    if(PLN_in_low&&PLN_flush_cache_end)
    {
        disp_apl_trace(LOG_ALW, 0x62dd, " pln+plp");
    }
    else if(host_dummy_start_wl != INV_U16 && shr_dtag_comt.que.rptr == shr_dtag_comt.que.wptr)
    {
    	//shutdown running , cache empty , no need wait shutdown fill host dummy.
    	//disp_apl_trace(LOG_ALW, 0x0885,"[Jay] plp no call ucache flush when shutdown running");
		ipc_plp_flush_done(NULL);
    }
    else
    {
    	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
    	sys_assert(fctx);
    	fctx->ctx.caller = NULL;
    	fctx->ctx.cmpl = ipc_plp_flush_done;
    	fctx->nsid = 1;
    	fctx->flags.all = 0;
        plp_cache_tick = get_tsc_64();
    	ucache_flush(fctx);
    }
	return;
}



fast_code void plp_evt(u32 r0, u32 r1, u32 r2)
{
	/*
	 * stop host xfer here, otherwise ncl will not done if there are pending host read.
	 * fake plp need double comfirm. TODO add resume xfer after plp finish.
	 */
	CPU1_plp_step = 3;
    nvmet_update_smart_stat(NULL);
	#if (CO_SUPPORT_READ_AHEAD == TRUE)
	ra_disable_time(20); //2s
	#endif
	u64 start = get_tsc_64();
	u8  tick = 0;
	while (cpu2_cancel_streaming || cpu4_cancel_streaming)
```

### Snippet 5: L4500-L4540
- Reasons: dispatcher/rdisk.c+4520 rdisk_fe_info_flush_done()

```c

	cpu2_cancel_streaming = cpu4_cancel_streaming = 1;
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_SUSPEND_GC, 0, 0);
	plp_cancel_die_que();

    nvmet_io_fetch_ctrl(true);
	btn_de_wr_disable();
    hal_nvmet_abort_xfer(true);
    CPU1_plp_step = 2;
    log_level_chg(LOG_ERR);
    urg_evt_set(evt_plp_flush, 0, 0);
}

/*!
 * @brief interface to start force flush, before flush, host write is not enable
 *
 * @param req: not used, please input NULL
 *
 * @return	not used
 */
ftl_flush_data_t plp_fctx;
fast_code void plp_force_flush(void)
{
	/*disp_apl_trace(LOG_INFO, 0, "ipc force flush start");

	btn_de_wr_disable(); //disable host wr data entry

	ftl_flush_data_t *fctx = &plp_fctx; //sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));

	plp_trigger = 0xEE;

	fctx->ctx.caller = NULL;
	fctx->ctx.cmpl = ipc_plp_flush_done;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	//fctx->flags.b.dtag_gc = 1;
	fctx->flags.b.plp = 1;
	//disp_apl_trace(LOG_INFO, 0, "fctx:%x cmpl:%x", fctx, fctx->ctx.cmpl);
	ucache_flush(fctx);*/
}

```

### Snippet 6: L5439-L5494
- Reasons: dispatcher/rdisk.c+5459 rdisk_alloc_streaming_rw_dtags(), dispatcher/rdisk.c+5474 rdisk_alloc_streaming_rw_dtags()

```c
		//ncl_set_ncb_clk(BIT0, 533); ///< BIT0 for CLK_TYPE_ECC
	}
	else if (1 == ps)
	{
		//ncl_set_ncb_clk(BIT0, 200);
	}
	else if (2 == ps)
	{
		//ncl_set_ncb_clk(BIT0, 100);
	}
	else if (3 == ps || 4 == ps)
	{
		rdisk_pmu_shutdown();
	}
	disp_apl_trace(LOG_WARNING, 0x9a17, "change power state [%d] complete", ps);
	return;
}

/*!
 * @brief rdisk suspned function
 *
 * @param mode	sleep mode
 *
 * @return		always true
 */
ps_code bool rdisk_suspend(enum sleep_mode_t mode)
{
	ucache_suspend();
	return true;
}

/*!
 * @brief rdisk allocate streaming read/write
 *
 *
 * @return		None
 */
fast_code void rdisk_alloc_streaming_rw_dtags(void)
{
#if defined(SEMI_WRITE_ENABLE)
	dtag_t dtag_res[RDISK_W_STREAMING_MODE_DTAG_CNT];
	u32 start;
	u32 end;
	u32 sz;
	u32 cnt;
	u32 i;
#endif
#if defined(ENABLE_EXTERNAL_STREAMING_READ_DTAG)
	start = (u32)&__dtag_stream_read_start;
	end = (u32)&__dtag_stream_read_end;
	sz = end - start;
	cnt = sz / DTAG_SZE;
	disp_apl_trace(LOG_INFO, 0x6922, "alloc Streaming Read #Dtags(%d) start %x end %x", cnt, start, end);
	sys_assert(cnt == RDISK_R_STREAMING_MODE_DTAG_CNT);
	sys_assert((start & 0xFFF) == 0);
	sys_assert((end & 0xFFF) == 0);
```

### Snippet 7: L5837-L5877
- Reasons: dispatcher/rdisk.c+5857 rdisk_fe_info_restore()

```c
ddr_code void dma_error_cnt_incr(void)
{
	ftl_stat.total_dma_error_cnt++;
}

/*!
 * @brief get read only flag
 *
 * @return	return true if read only was set
 */
ddr_code bool is_system_read_only(void)
{
	shr_ftl_flags.b.read_only = (read_only_flags.all > 0) ? true : false;
	return shr_ftl_flags.b.read_only;
}

ddr_code u64 get_req_statistics(void)
{
	u32 ttl = 0;

	ttl += req_statistics.wr_du_ttl;
	ttl += req_statistics.rd_rcv_ttl;
	ttl += req_statistics.req_rcv_ttl;

	req_statistics.wr_du_ttl = 0;
	req_statistics.rd_rcv_ttl = 0;
	req_statistics.req_rcv_ttl = 0;

	return ttl;
}

/*!
 * @brief restore fe info from latest fe block
 *
 * @note if read fail, use default value
 *
 * @return	not used
 */
ddr_code static void rdisk_fe_info_restore(void)
{
	u32 du_cnt = occupied_by(sizeof(fe_info_t), DTAG_SZE);
```

## ftl/ftl_flush.c

- Resolved path: `src\ftl\ftl_flush.c`
- Hits: 8 (deduped), Snippets: 4 (merged)

### Snippet 1: L241-L281
- Reasons: ftl/ftl_flush.c+261 flush_fsm_done()

```c
	.fns = _tbl_flush_st_func,
	.max = ARRAY_SIZE(_tbl_flush_st_func)
};
*/
init_code void flush_fsm_init(u32 nsid)
{
	flush_fsm_t *ffsm;

	ffsm = &_ffsm[nsid];
	ffsm->nsid = nsid;
	ffsm->flags.all = 0;
	INIT_LIST_HEAD(&ffsm->pend_que);
}

extern void pop_shuttle_back_to_gc(void);
fast_code int flush_fsm_done(void *ctx)
{
	fsm_ctx_t *fsm = (fsm_ctx_t*)ctx;
	flush_fsm_t *ffsm = (flush_fsm_t*)fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *)fsm->done_priv;
	
	ftl_apl_trace(LOG_INFO, 0x6ad3, "flush_fsm_done in %d ms", time_elapsed_in_ms(ffsm_all_start));
	
	pop_shuttle_back_to_gc();	//pop shuttle blk back to GC_POOL, prevent shut down without power off
	
	shr_ftl_flags.b.flushing = false;
	ffsm->flags.b.flushing = 0;
	flush_ctx->cmpl(flush_ctx);

	if (!list_empty(&ffsm->pend_que)) {
		flush_ctx = list_first_entry(&ffsm->pend_que, flush_ctx_t, entry);
		list_del(&flush_ctx->entry);

		//if (flush_ctx->flags.b.spb_close)
			//tbl_flush_fsm_run(flush_ctx);
		//else
			flush_fsm_run(flush_ctx);
	}

	return 0;
}
```

### Snippet 2: L321-L428
- Reasons: ftl/ftl_flush.c+341 ftl_open_qbt_done(), ftl/ftl_flush.c+376 flush_l2p_done(), ftl/ftl_flush.c+408 flush_misc_done()

```c
 * @brief completion callback when l2p table was flushed, continue to next step
 *
 * @param flush_tbl	flush table object
 *
 * @return		not used
 */

fast_code void gc_suspend_done(gc_action_t* action)
{
	flush_fsm_t *fsm_ctx = (flush_fsm_t*)action->caller;

	ftl_apl_trace(LOG_INFO, 0xbdd8, "gc suspended done");

	fsm_ctx_next(&fsm_ctx->fsm);
	fsm_ctx_run(&fsm_ctx->fsm);
}

fast_code void ftl_open_qbt_done(ns_start_t *ctx)
{
	flush_fsm_t* fsm_ctx = (flush_fsm_t*)ctx->caller;

	ftl_apl_trace(LOG_ERR, 0x4936, "open qbt done");
    if (shr_qbt_prog_err)
    {
        ftl_apl_trace(LOG_ERR, 0x1bc1, "skip filling dummy for PBT,Host and GC, start flush l2p");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_FLUSH_L2P);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else
    {
    	fsm_ctx_next(&fsm_ctx->fsm);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
}

/*!
 * @brief completion callback when l2p table was flushed, continue to next step
 *
 * @param flush_tbl	flush table object
 *
 * @return		not used
 */
fast_code void flush_l2p_done(ftl_core_ctx_t *ctx)
{
	ftl_flush_tbl_t *flush_tbl = (ftl_flush_tbl_t *) ctx;
	ftl_ns_t *ftl_ns = ftl_ns_get(flush_tbl->nsid);
	flush_fsm_t *fsm_ctx = (flush_fsm_t *) ctx->caller;

    if (shr_qbt_prog_err)
    {
        //ftl_apl_trace(LOG_INFO, 0, "qbt prog err, jump to FTL_ST_CLOSE_QBT");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_CLOSE_QBT);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else
    {
    	ftl_apl_trace(LOG_INFO, 0x2314, "l2p %d flushed done in %d ms", flush_tbl->nsid, time_elapsed_in_ms(ffsm_start));
    	sys_assert(ftl_ns->flags.b.flushing);
    	ftl_ns->flags.b.flushing = 0;

    	fsm_ctx_next(&fsm_ctx->fsm);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
}

/*!
 * @brief completion callback when misc data was flushed, continue to next step
 *
 * @param flush_misc	flush misc object
 *
 * @return		not used
 */
fast_code void flush_misc_done(ftl_core_ctx_t *ctx)
{
	flush_fsm_t *fsm_ctx = (flush_fsm_t *) ctx->caller;
	ftl_flush_misc_t *flush_misc = (ftl_flush_misc_t *) ctx;
	dtag_t dtag = { .dtag = flush_misc->dtag_start[0] };

	ftl_l2p_put_vcnt_buf(dtag, flush_misc->dtag_cnt[0], false);

    if (shr_qbt_prog_err)
    {
        //ftl_apl_trace(LOG_INFO, 0, "qbt prog err, jump to FTL_ST_CLOSE_QBT");
        fsm_ctx_set(&fsm_ctx->fsm, FTL_ST_CLOSE_QBT);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
    else
    {
    	ftl_apl_trace(LOG_INFO, 0x4bd3, "misc flushed done");
    	fsm_ctx_next(&fsm_ctx->fsm);
        fsm_ctx_run(&fsm_ctx->fsm);
    }
}

/*!
 * @brief completion callback when descriptor was flushed continue to next step
 *
 * @param _ctx	should be state machine pointer
 *
 * @return	not used
 */
fast_code void flush_desc_done(void *_ctx)
{
	fsm_ctx_t *fsm = (fsm_ctx_t *) _ctx;
	flush_fsm_t *ctx = (flush_fsm_t *) _ctx;

	if (ctx->flags.b.make_ins_dirty == 0)
		fsm_ctx_next(fsm);
```

### Snippet 3: L459-L499
- Reasons: ftl/ftl_flush.c+479 flush_st_suspend_gc()

```c
#endif



fast_code fsm_res_t flush_st_suspend_gc(fsm_ctx_t *fsm)
{
#if CROSS_TEMP_OP
	extern struct timer_list spb_scan_over_temp_blk_timer;
	del_timer(&spb_scan_over_temp_blk_timer);
#endif
	flush_fsm_t *ctx = (flush_fsm_t *)fsm;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
	gc_action_t *action = &ctx->gc_action;

	if(plp_trigger)
	{
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	shr_shutdownflag = true;
	gc_suspend_stop_next_spb = true;
	ftl_apl_trace(LOG_INFO, 0x6bd1, "set flag %d/%d", gc_suspend_stop_next_spb, shr_shutdownflag);

	if (flush_ctx->flags.b.format)
		action->act = GC_ACT_ABORT;
	else
		action->act = GC_ACT_SUSPEND;

	action->caller = ctx;
	action->cmpl = gc_suspend_done;

	if (gc_action(action))
		return FSM_CONT;

	ftl_apl_trace(LOG_INFO, 0x69d3, "ffsm[%d]: suspend gc", fsm->state_cur);
	return FSM_PAUSE;
}

slow_code fsm_res_t flush_st_wait_qbt(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *)fsm;
```

### Snippet 4: L537-L592
- Reasons: ftl/ftl_flush.c+557 flush_st_wait_qbt(), ftl/ftl_flush.c+564 flush_st_wait_qbt(), ftl/ftl_flush.c+572 flush_st_wait_qbt()

```c
			}
			for(cnt = 0; cnt < spor_read_pbt_cnt; cnt++){
				if(qbt_tar == spor_read_pbt_blk[cnt]){
				  FTL_BlockPushList(SPB_POOL_FREE, qbt_tar, FTL_SORT_NONE);
				  goto again;
				}					
			}
		}
		else{
		    qbt_tar = FTL_BlockPopHead(SPB_POOL_FREE);
            if (qbt_tar == last_spb_vcnt_zero)
            {
                spb_id_t tmp_tar = qbt_tar;
                qbt_tar = FTL_BlockPopHead(SPB_POOL_FREE);
                sys_assert(qbt_tar!=INV_SPB_ID);
                ftl_apl_trace(LOG_INFO, 0xc22d, "[spb]last_spb_vcnt_zero:%u, qbt_tar:%u", last_spb_vcnt_zero, qbt_tar);
    			FTL_BlockPushList(SPB_POOL_FREE, tmp_tar, FTL_SORT_BY_EC);
            }
		}
		sys_assert(qbt_tar != INV_U16);
		FTL_BlockPushList(SPB_POOL_QBT_FREE, qbt_tar, FTL_SORT_NONE);
		ftl_apl_trace(LOG_INFO, 0x2967, "[FTL]qbtblk %d to qbt free cnt %d sw_flag %d", qbt_tar, spb_get_free_cnt(SPB_POOL_QBT_FREE), spb_get_sw_flag(qbt_tar));
	}
	
	ftl_core_start(FTL_NS_ID_INTERNAL);// open qbt Curry 20201013
	alloc->type = FTL_CORE_NRM;
	alloc->nsid = FTL_NS_ID_INTERNAL;
	alloc->caller = ctx;
	alloc->cmpl = ftl_open_qbt_done;
	ftl_core_qbt_alloc(alloc);
	ftl_apl_trace(LOG_INFO, 0x9c07, "ffsm[%d]: wait_qbt", fsm->state_cur);
	return FSM_PAUSE;
}

fast_code fsm_res_t flush_st_flush_l2p(fsm_ctx_t *fsm)
{
	flush_fsm_t *ctx = (flush_fsm_t *) fsm;
	ftl_flush_tbl_t *flush_tbl = &ctx->flush_tbl;
	flush_ctx_t *flush_ctx = (flush_ctx_t *) fsm->done_priv;
	ffsm_start = get_tsc_64();
    shr_qbt_prog_err = false;
	if(plp_trigger)
	{
		shr_qbtflag = false;
		fsm_ctx_set(fsm, FTL_ST_FLUSH_DONE);
		return FSM_JMP;
	}
	extern u16 host_dummy_start_wl;
	host_dummy_start_wl = INV_U16;
	//if (flush_ctx->flags.b.format == false) {
		ftl_apl_trace(LOG_INFO, 0xed73, "ffsm[%d]: flush l2p %d", fsm->state_cur, ctx->nsid);
		flush_tbl->flags.all = 0;
		if(flush_ctx->flags.b.shutdown)
			flush_tbl->flags.b.shutdown = 1;

		
```

## tcg/tcg.c

- Resolved path: `src\tcg\tcg.c`
- Hits: 8 (deduped), Snippets: 2 (merged)

### Snippet 1: L887-L927
- Reasons: tcg/tcg.c+907 TcgHardReset()

```c

ddr_code void TcgHardReset(void)
{
    if (mSessionManager.state == SESSION_START)
        ResetSessionManager();

    host_Properties_Reset();

    // *** [ No Lock State Reset for HardReset
    //LockingTbl_Reset(PowerCycle);       // LckLocking_Tbl "ProgrammaReset"
    //LockingRangeTable_Update();         //Update Read/Write LockedTable for Media Read/Write control

    //MbrCtrlTbl_Reset(PowerCycle);
    // ] &&&
#if TCG_FS_BLOCK_SID_AUTH
    if (mTcgStatus&SID_HW_RESET)
        mTcgStatus &= ~(SID_BLOCKED + SID_HW_RESET);      // Clear Events are reset when a Clear Event occurs
#endif
    gTcgCmdState = ST_AWAIT_IF_SEND;
	
	tcg_core_trace(LOG_INFO, 0x5e9a, "HardReset() - TcgStatus|%x",mTcgStatus);
}


//-------------------tcg_cmdPkt_payload_decoder-------------------------
ddr_code u32 tcg_cmdPkt_payload_decoder(req_t *req)
{
	u8  byte;
    u32 tmp32;  //for transaction
    u32 result = STS_SUCCESS;

    iPload = 0;   //reset payload index
    if (bControlSession)
    {   // Session is not started => Control Session, only accept SMUID
        // Control Session, TSN/HSN=0x00, A6-3-1...
        byte = ChkToken();
        if (byte == TOK_Call)
        {   //1. Call token, start of a method invocation...
            //2. check method header:

            // get Invoking UID
```

### Snippet 2: L6340-L6469
- Reasons: tcg/tcg.c+6360 Tcg_Key_wp_uwp(), tcg/tcg.c+6373 Tcg_Key_wp_uwp(), tcg/tcg.c+6388 Tcg_Key_wp_uwp(), tcg/tcg.c+6389 Tcg_Key_wp_uwp(), tcg/tcg.c+6421 Tcg_Key_wp_uwp(), tcg/tcg.c+6437 Tcg_Key_wp_uwp(), tcg/tcg.c+6449 Tcg_Key_wp_uwp()

```c

		memset(wrap_buf, 0, sizeof(wrap_buf));

		Raw_Key1 = wrap_buf;
        //sys_assert(Raw_Key1);

		WP_Key1 = Raw_Key1 + 32;

		Raw_Key2 = WP_Key1 + 64;
		
		WP_Key2 = Raw_Key2 + 32;
			
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Raw_Key1|%x , &WP_Key1|%x", Raw_Key1, WP_Key1);
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Raw_Key2|%x , &WP_Key2|%x", Raw_Key2, WP_Key2);

		//memset(Raw_Key1, 0, DTAG_SZE);
		
		memcpy(Raw_Key1, mRawKey[idx].dek.aesKey, 32);
		memcpy(Raw_Key2, mRawKey[idx].dek.xtsKey, 32);
		
		tcg_core_trace(LOG_INFO, 0xb938, "[Max set] Raw_Key1|%x , Raw_Key2|%x", *(u32 *)Raw_Key1, *(u32 *)Raw_Key2);

		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		//kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */  //non used
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */	
		
		sec_ss_key_wp_uwp(Raw_Key1, WP_Key1, &kwp_cfg);  //Wrap Key1(AES key)
		sec_ss_key_wp_uwp(Raw_Key2, WP_Key2, &kwp_cfg);  //Wrap Key2(XTS key)
	
		tcg_core_trace(LOG_INFO, 0xc0b2, "[TCG]Wrapped K1|%x K2|%x",*(u32 *)WP_Key1,*(u32 *)WP_Key2);

		memcpy(pG3->b.mLckKAES_256_Tbl.val[idx].key1, WP_Key1, 40);
		memcpy(pG3->b.mLckKAES_256_Tbl.val[idx].key2, WP_Key2, 40);
		
		//tcg_core_trace(LOG_INFO, 0, "[TCG]output icv|%x K1 icv|%x",*(((u32 *)WP_Key1)+8),pG3->b.mLckKAES_256_Tbl.val[idx].icv1[0]);

		memset(mRawKey[idx].dek.aesKey, 0, sizeof(mRawKey[idx].dek.aesKey));
		memset(mRawKey[idx].dek.xtsKey, 0, sizeof(mRawKey[idx].dek.xtsKey));

		/* Release dtags */
		//dtag_put(DTAG_T_SRAM, dtag);
		//sys_free_aligned(SLOW_DATA, Raw_Key1);
		memset(wrap_buf, 0, sizeof(wrap_buf));
		
		tcg_core_trace(LOG_INFO, 0x3b20, "[TCG]Range|%x key wrap done!",idx);
		tcg_core_trace(LOG_INFO, 0x6dc7, "[TCG]Raw|%x Tbl|%x",mRawKey[idx].dek.aesKey[0],pG3->b.mLckKAES_256_Tbl.val[idx].key1[0]);
		
	}
	else //AES_256B_KUWP_NO_SECURE
	{
		void* Tbl_Key1;     //wrapped aes key
		void* Tbl_Key2;     //wrapped xts key
		void* UWP_Key1;    //Unwrapped aes key
		void* UWP_Key2;    //Unwrapped xts key

		//dtag = dtag_get(DTAG_T_SRAM, &Tbl_Key1);
		//sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
		
		memset(wrap_buf, 0, sizeof(wrap_buf));

		Tbl_Key1 = wrap_buf;
        //sys_assert(Tbl_Key1);

		UWP_Key1 = Tbl_Key1 + 64;

		Tbl_Key2 = UWP_Key1 + 32;
		
		UWP_Key2 = Tbl_Key2 + 64;

		//memset(Tbl_Key1, 0, DTAG_SZE);
		
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Tbl_Key1|%x , &UWP_Key1|%x", Tbl_Key1, UWP_Key1);
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Tbl_Key2|%x , &UWP_Key2|%x", Tbl_Key2, UWP_Key2);

		memcpy(Tbl_Key1, pG3->b.mLckKAES_256_Tbl.val[idx].key1, 40);
		memcpy(Tbl_Key2, pG3->b.mLckKAES_256_Tbl.val[idx].key2, 40);
		
		tcg_core_trace(LOG_INFO, 0xa0f6, "[Max set] Tbl_Key1|%x , Tbl_Key2|%x", *(u32 *)Tbl_Key1, *(u32 *)Tbl_Key2);
		//tcg_core_trace(LOG_INFO, 0, "[TCG]Input icv|%x K1 icv|%x",*(((u32 *)Tbl_Key1)+8),pG3->b.mLckKAES_256_Tbl.val[idx].icv1[0]);
		
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		//kwp_cfg.din_spr_idx = SS_SPR_REG1; /* SPR register index for data input 0 to 3 */  
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		//kwp_cfg.sec_mode_en = 0; /* secure mode enable(1)/disable(0) */
		
		sec_ss_key_wp_uwp(Tbl_Key1, UWP_Key1, &kwp_cfg);  //unwrap Key1(AES key)
				
		sec_ss_key_wp_uwp(Tbl_Key2, UWP_Key2, &kwp_cfg);  //unwrap Key2(XTS key)

		tcg_core_trace(LOG_INFO, 0x8cdf, "[TCG]Uwp K1|%x K2|%x",*(u32 *)UWP_Key1, *(u32 *)UWP_Key2);

		memcpy(mRawKey[idx].dek.aesKey, UWP_Key1, sizeof(mRawKey[idx].dek.aesKey));
		memcpy(mRawKey[idx].dek.xtsKey, UWP_Key2, sizeof(mRawKey[idx].dek.xtsKey));
		
		mRawKey[idx].state = TCG_KEY_UNWRAPPED;

		/* Release dtags */
		//dtag_put(DTAG_T_SRAM, dtag);
		//sys_free_aligned(SLOW_DATA, Tbl_Key1);
		memset(wrap_buf, 0, sizeof(wrap_buf));
		
		tcg_core_trace(LOG_INFO, 0xc109, "[TCG]Range|%x key unwrap done!",idx);		
		tcg_core_trace(LOG_INFO, 0xa1f1, "[TCG]Raw|%x Tbl|%x",mRawKey[idx].dek.aesKey[0],pG3->b.mLckKAES_256_Tbl.val[idx].key1[0]);
	}
	/*----------------------------------------------*/
	
	memset(WrapKEK, 0, sizeof(WrapKEK));
	
}


// Generate a new key ->Wrap ->update to G3.b.mLckKAES_256_Tbl[]

ddr_code void TcgChangeKey(u8 idx)  // if not CNL, idx = rangeNo
{
    if (idx >= TCG_MAX_KEY_CNT)
    {  
        tcg_core_trace(LOG_INFO, 0x6540, "!! TcgChangeKey err");
        return;
    }

	u32 cnt = 1;
```

## nvme/apl/decoder/core.c

- Resolved path: `src\nvme\apl\decoder\core.c`
- Hits: 8 (deduped), Snippets: 3 (merged)

### Snippet 1: L494-L534
- Reasons: nvme/apl/decoder/core.c+514 nvmet_set_ns_attrs()

```c
		nvme_apl_trace(LOG_ERR, 0x7a60, "attr->hw_attr.nsid:%d",attr->hw_attr.nsid);
		panic("Invalid NSID\n");
		return -EBUSY;
	}

	nsid = attr->hw_attr.nsid - 1;
	memcpy((void *)&_nvme_ns_info[nsid], (void *)attr, sizeof(nvme_ns_attr_t));
	memcpy((void *)&ns_array_menu->ns_attr[nsid], (void *)attr, sizeof(nvme_ns_attr_t));//joe add epm 20200901
#if 0
		if ((is_power_on) && (NS_WP_ONCE == _nvme_ns_info[nsid].wp_state)) {
			_nvme_ns_info[nsid].wp_state = NS_NO_WP;
			_nvme_ns_info.hw_attr.wr_prot = 0;
		}
		cmd_proc_ns_cfg(&_nvme_ns_info[nsid].hw_attr, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].ms, _lbaf_tbl[_nvm_ns_info[nsid].hw_attr.lbaf].lbads);
		return;
#endif

	nvme_ns_attr_t *p_ns = &_nvme_ns_info[nsid];

	if (_nsid[nsid].ns)
	{
		nvme_apl_trace(LOG_INFO, 0xc6ce, "NS update cap(%x -> %x), lbaf(%d -> %d)", _nsid[nsid].ns->ncap, p_ns->hw_attr.lb_cnt, _nsid[nsid].ns->lbaf, p_ns->hw_attr.lbaf);
		#if defined(HMETA_SIZE)
		nvme_apl_trace(LOG_INFO, 0x1070, "PI update pit(%d -> %d), pil(%d -> %d), ms(%d -> %d)",_nsid[nsid].ns->pit, p_ns->hw_attr.pit, _nsid[nsid].ns->pil, p_ns->hw_attr.pil, _nsid[nsid].ns->ms, p_ns->hw_attr.ms);
		#endif
	}

	//nvme_apl_trace(LOG_INFO, 0, "ns_array_menu->ns_attr[nsid].hw_attr.lbaf(%d)", ns_array_menu->ns_attr[nsid].hw_attr.lbaf);
	_nsid[nsid].nsid = p_ns->hw_attr.nsid;

	_nsid[nsid].ns = &_ns[nsid];
	_nsid[nsid].ns->ncap = p_ns->fw_attr.ncap;
	_nsid[nsid].ns->nsze = p_ns->fw_attr.nsz;

     /*  for(a=0;a<MAX_LBAF;a++){//joe add sec size 20200820

		if(host_sec_bitz==_lbaf_tbl[a].lbads){
			p_ns->hw_attr.lbaf=a;
			ns_array_menu->ns_attr[nsid].hw_attr.lbaf=a;
			break;
			}
```

### Snippet 2: L759-L859
- Reasons: nvme/apl/decoder/core.c+779 nvmet_restore_feat(), nvme/apl/decoder/core.c+790 nvmet_restore_feat(), nvme/apl/decoder/core.c+806 nvmet_restore_feat(), nvme/apl/decoder/core.c+817 nvmet_restore_feat(), nvme/apl/decoder/core.c+830 nvmet_restore_feat(), nvme/apl/decoder/core.c+839 nvmet_reinit_feat()

```c
		goto def;

	if (stat_match(&saved_feat->head, SAVED_FEAT_VER, new_fsize, SAVED_FEAT_SIG)) {
		ctrlr->cur_feat = saved_feat->saved_feat;
		ctrlr->saved_feat = saved_feat->saved_feat;
	} else {
def:
		nvme_apl_trace(LOG_ALW, 0, "def feat used");
		ctrlr->cur_feat = ctrlr->def_feat;
		ctrlr->saved_feat = ctrlr->def_feat;
	}

	nvmet_restore_tmt();
*/
		extern epm_info_t *shr_epm_info;
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		memcpy(&ctrlr->saved_feat, epm_smart_data->feature_save, sizeof(struct nvmet_feat));

        u32 gpio_reg = readl((void *)(MISC_BASE + GPIO_INT_CTRL));


		if(ctrlr->saved_feat.Tag == 0x53415645)//"SAVE"
		{
			nvme_apl_trace(LOG_ALW, 0xf62a, "saved feat used");
			if(!ctrlr->saved_feat.warn_cri_feat.tmt_critical)
			{
				ctrlr->saved_feat.hctm_feat.b.tmt1 = c_deg_to_k_deg(TS_DEFAULT_TMT1);
				ctrlr->saved_feat.hctm_feat.b.tmt2 = c_deg_to_k_deg(TS_DEFAULT_TMT2);
				ctrlr->saved_feat.warn_cri_feat.tmt_warning = c_deg_to_k_deg(TS_DEFAULT_WARNING);
				ctrlr->saved_feat.warn_cri_feat.tmt_critical = c_deg_to_k_deg(TS_DEFAULT_CRITICAL);
			}

			ctrlr->cur_feat = ctrlr->saved_feat;
			ctrlr->cur_feat.ic_feat.all = ctrlr->def_feat.ic_feat.all;
			nvmet_restore_tmt();
#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
				if(ctrlr->cur_feat.en_plp_feat.b.opie == 1){
                    PLN_open_flag = true;
                    nvme_apl_trace(LOG_ALW, 0xfaed, "PLP Enable ");
				}
				else{
                    PLN_open_flag = false;
                    nvme_apl_trace(LOG_ALW, 0x8d40, "PLP Disable ");
				}
#endif
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
                if(ctrlr->cur_feat.en_pwrdis_feat.b.pwrdis){
                    PWRDIS_open_flag = true;
                    nvme_apl_trace(LOG_ALW, 0x1c82, "PWRDIS enable ");
                }
                else{
                    PWRDIS_open_flag = false;
                    nvme_apl_trace(LOG_ALW, 0x4898, "PWRDIS disable ");
                }
#endif
			return;
		}

		nvme_apl_trace(LOG_ALW, 0x9817, "def feat used");
		ctrlr->saved_feat = ctrlr->def_feat;
		ctrlr->cur_feat = ctrlr->saved_feat;

#if (EN_PLP_FEATURE == FEATURE_SUPPORTED)
			if(ctrlr->cur_feat.en_plp_feat.b.opie == 1){
                PLN_open_flag = true;
                nvme_apl_trace(LOG_ALW, 0x34c8, "PLP Enable ");
			}
			else{
                PLN_open_flag = false;
                nvme_apl_trace(LOG_ALW, 0x1501, "PLP Disable ");
			}
#endif
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)
            if(ctrlr->cur_feat.en_pwrdis_feat.b.pwrdis){
                PWRDIS_open_flag = true;
                nvme_apl_trace(LOG_ALW, 0x979f, "PWRDIS enable ");
            }
            else{
                PWRDIS_open_flag = false;
                nvme_apl_trace(LOG_ALW, 0x9282, "PWRDIS disable ");
            }
#endif
        nvme_apl_trace(LOG_ALW, 0x17d3, " GPIO_INT_CTRL 0x%x",gpio_reg);

		//nvme_apl_trace(LOG_ERR, 0, "cur_ARB %x,saved_arb %x\n",ctrlr->cur_feat.arb_feat.all,ctrlr->saved_feat.arb_feat.all);
		//ctrlr->saved_feat = saved_feat->saved_feat;
}


fast_code void nvmet_reinit_feat(void)  //3.1.7.4 merged 20201201 Eddie
{
	nvme_apl_trace(LOG_INFO, 0x07af, "reinit nvme feature (saved) -> (cur)");
	ctrlr->cur_feat = ctrlr->saved_feat;
	old_time_stamp = get_cur_sys_time();
}

/*!
 * @brief update NVME feature
 *
 * @param saved_feat 	nvme feature struct
 *
```

### Snippet 3: L1248-L1288
- Reasons: nvme/apl/decoder/core.c+1268 nvmet_core_handle_cq()

```c
	if (ctrlr->sqs[fe->nvme.sqid] == NULL)
		return true;
#endif

	/* To avoid memset the 16 bytes */
	struct nvme_cpl cpl; // = {.rsvd1 = 0 };
#if defined(SRIOV_SUPPORT)
	u16 cqid;
	if (fid == 0) {
		cqid = ctrlr->sqs[fe->nvme.sqid]->cqid;
	} else {
		cqid = ctrlr->sr_iov->fqr_sq_cq[fid -1].fqrsq[hst_sqid]->cqid;
		cqid = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL + (fid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
		nvme_apl_trace(LOG_DEBUG, 0x7569, "cntlid(%d), flex sqid(%d), flex cqid(%d), hst_sqid(%d)",fid, sqid, cqid, hst_sqid);
	}
#else
	u16 cqid = ctrlr->sqs[fe->nvme.sqid]->cqid;
#endif
	/* it's ugly due to type-punned pointer breaks strict-aliasing rules */
	nvme_status_alias_t _status;

	cpl.rsvd1 = 0;
	cpl.sqid = fe->nvme.sqid;
	cpl.cntlid = fe->nvme.cntlid;
	cpl.cdw0 = fe->nvme.cmd_spec;
	cpl.cid = fe->nvme.cid;

	_status.all = fe->nvme.nvme_status;
	cpl.status = _status.status;

	if (cpl.status.sct != 0 || cpl.status.sc != 0) {
		nvme_apl_trace(LOG_ERR, 0x08d5, "sq %d cid %d err sct %d sc %d",
				fe->nvme.sqid, fe->nvme.cid, cpl.status.sct, cpl.status.sc);
	}

	return hal_nvmet_update_cq(cqid, &cpl, false);
}
void ddr_code nvmet_warmboot_handle_commit_done(fe_t *fe)
{
	nvmet_core_handle_cq(fe);
}
```

## ncl_20/nand.c

- Resolved path: `src\ncl_20\nand.c`
- Hits: 8 (deduped), Snippets: 2 (merged)

### Snippet 1: L674-L723
- Reasons: ncl_20/nand.c+694 nand_detect(), ncl_20/nand.c+695 nand_detect(), ncl_20/nand.c+703 nand_detect()

```c
				if (ndcu_xfer(&ctrl))
					break;
			} while (1);
			ndcu_close(&ctrl);

#if HAVE_ONFI_SUPPORT
			if (id[1] == NAND_ID_VENDOR) {
				if (first_ce) {
					ncb_nand_trace(LOG_ERR, 0x4857, "IO volitage is 1.2V\n");
				}
				for (i = 0; i < NAND_ID_LENGTH - 1; i++) {
					id[i] = id[i + 1];
				}
				id[NAND_ID_LENGTH - 1] = 0;
				// 1.2V default to NVDDR3 mode
				nand_info.def_tm = NDCU_IF_DDR3 | 0;
				nand_info.vcc_1p2v = true;
			}
#endif
			if (first_ce) {
				ncb_nand_trace(LOG_ERR, 0xc237, "Read ID ch%d ce%d:", ch, ce);
				ncb_nand_trace(LOG_ERR, 0x2840, "    %b %b %b %b %b %b", id[0], id[1], id[2], id[3], id[4], id[5]);

				for (i = 0; i < sizeof(nand_codename) / sizeof(nand_codename[0]); i++) {
					if (memcmp(id, nand_codename[i].id, nand_codename[i].id_len) == 0) {
						ncb_nand_trace(LOG_ERR, 0x27ef, " (%s)", nand_codename[i].name);
						break;
					}
				}
				ncb_nand_trace(LOG_ERR, 0xe06f, "\n");

				if (id[0] != NAND_ID_VENDOR) {
					ncb_nand_trace(LOG_ERR, 0x5c5e, "Read ID ch%d ce%d:", ch, ce);
					ncb_nand_trace(LOG_ERR, 0xaad7, "    %b %b %b %b %b %b", id[0], id[1], id[2], id[3], id[4], id[5]);
					ncb_nand_trace(LOG_WARNING, 0xeedb, "expect ID %b", NAND_ID_VENDOR);
				}
#if HAVE_VELOCE
				sys_assert(id[0] == NAND_ID_VELOCE || id[0] == NAND_ID_VENDOR);
#else
				sys_assert(id[0] == NAND_ID_VENDOR);
#endif
				switch (id[0]) {
#ifdef HAVE_VELOCE
				case NAND_ID_VELOCE:
#endif
				case NAND_ID_TOSHIBA:
					ncb_nand_trace(LOG_DEBUG, 0xd0ce, "TSB Toggle nand");
					support = true;
					nand_info.device_class = TOGGLE_NAND;
					break;
```

### Snippet 2: L745-L839
- Reasons: ncl_20/nand.c+765 nand_detect(), ncl_20/nand.c+771 nand_detect(), ncl_20/nand.c+775 nand_detect(), ncl_20/nand.c+791 nand_detect(), ncl_20/nand.c+819 nand_detect()

```c
					ncb_nand_trace(LOG_DEBUG, 0x549b, "Ymtc ONFI nand");
					support = true;
					nand_info.device_class = ONFI_NAND;
					break;
				case NAND_ID_UNIC:
					ncb_nand_trace(LOG_DEBUG, 0xbf23, "Unic ONFI nand");
					support = true;
					nand_info.device_class = ONFI_NAND;
					break;
				default:
					ncb_nand_trace(LOG_DEBUG, 0xf80a, "Nand vendor 0x%x not support", id[0]);
					support = false;
					sys_assert(0);
					break;
				}
			}
			if (first_ce) {
				memcpy(nand_info.id, id, nr_id_bytes);
				nand_info.nr_id_bytes = nr_id_bytes;
				first_ce = false;
				ncb_nand_trace(LOG_ERR, 0xaea9, "#");
			} else {
				if (memcmp(nand_info.id, id, nr_id_bytes) != 0) {
					if (id[0] != NO_DEV_SIGNAL) {
						ncb_nand_trace(LOG_ERR, 0xae74, "x");
					} else {
						ncb_nand_trace(LOG_ERR, 0xae4d, "_");
					}
					support = false;
				} else {
					ncb_nand_trace(LOG_ERR, 0x8ca4, "#");
				}
			}
			if (!support) {
				ce_stop = true;
				continue;
			} else {
				if (ce_stop) {
					ce_consective = false;
				}
			}
			ce_exist_bmp[ch] |= 1 << ce;
			ch_exist = true;
			ce_remap |= ce << (ce_cnt << 2);
			ce_cnt++;
		}
		ncb_nand_trace(LOG_ERR, 0xc801, "\n");
		if (ch_exist) {
			if (nand_info.geo.nr_channels == 0){
				nand_info.geo.nr_targets = ce_cnt;
			} else {
				if (nand_info.geo.nr_targets != ce_cnt) {
					ncb_nand_trace(LOG_WARNING, 0x7296, "CE cnt diff");
					sys_assert(0);
				}
			}
			nand_info.geo.nr_channels++;
			ch_remap |= ch << (ch_cnt << 2);
			ch_cnt++;
			if (!ce_consective) {// CE not consecutive, update CE remapping
				ndcu_writel(ce_remap, NF_CE_REMAP_REG0 + (ch << 2));
				ncb_nand_trace(LOG_INFO, 0x0c9f, "CE remap %x", ce_remap);
			}
		} else {
			// Make sure non-exist logic channel remaps to non-exist physical channel
			ch_remap |= ch << (((max_channel - 1) - (ch - ch_cnt)) << 2);
		}
	}


	if (max_channel < 8) {
		// Make sure non-exist logic channel remaps to non-exist physical channel
		ch_remap |= 0x77777777 << (max_channel * 4);
	}
	ncb_nand_trace(LOG_INFO, 0x76ea, "CH remap %x", ch_remap);

	ficu_channel_remap(ch_remap);
	if (!ce_consective) {// CE not consecutive, enable CE remapping
		nf_ddr_data_dly_ctrl_reg0_t remap_en;
		remap_en.all = ndcu_readl(NF_DDR_DATA_DLY_CTRL_REG0);
		remap_en.b.nf_ce_remap_en = 1;
		ndcu_writel(remap_en.all, NF_DDR_DATA_DLY_CTRL_REG0);
	}

	ncb_nand_trace(LOG_INFO, 0x3ed4, "CH %d CE %d", nand_info.geo.nr_channels, nand_info.geo.nr_targets);
#if defined(FAST_MODE)
	nand_info.geo.nr_channels = 1;
	ncb_nand_trace(LOG_INFO, 0x543e, "FAST mode, (%d) virtual channel", nand_info.geo.nr_channels);
#endif

#if HAVE_TSB_SUPPORT
	if (nand_is_bics4() || nand_is_bics5()) {
		nand_info.vcc_1p2v = true;
	} else if (nand_is_bics3()) {
		nand_info.vcc_1p2v = false;
```

## rtos/armv7r/misc.c

- Resolved path: `src\rtos\armv7r\misc.c`
- Hits: 8 (deduped), Snippets: 5 (merged)

### Snippet 1: L1050-L1090
- Reasons: rtos/armv7r/misc.c+1070 misc_reset()

```c
{
	u32 delay = 0;
	delay |= (readl(&misc_reg->fw_stts_14.all) >> 16);
	return delay;
}

slow_code_ex void misc_clear_startwb(void)
{
	writel(0, &misc_reg->fw_stts_14.all);
}

fast_code void misc_reset(enum reset_type type)
{
	reset_ctrl_t reset_ctrl = {
        // Benson Modify : prevent HW return a non-zero value
		//.all = readl(&misc_reg->reset_ctrl.all),
        .all = 0xFFFF0000 & readl(&misc_reg->reset_ctrl.all),
	};

	if (type == RESET_CPU){
		//No Print
	}
	else	{
		if(misc_is_warm_boot()==false)
        {
			rtos_core_trace(LOG_INFO, 0xd1b8, "misc_reset type %d", type);
        }
	}
	/*
	if (type > RESET_CPU){
		rtos_core_trace(LOG_INFO, 0, "misc_reset type %d > RESET_CPU", type);
		return;
	}
	*/
	reset_ctrl.b.reset_ctrl |= BIT(type);
	writel(reset_ctrl.all, &misc_reg->reset_ctrl.all);
#if 0 //Stanley Modified
	if (type == RESET_CPU){
		do{		//20210510-Eddie
			__asm__("nop");
			__asm__("nop");
```

### Snippet 2: L1871-L1911
- Reasons: rtos/armv7r/misc.c+1891 is_rainier_a1()

```c
	pcie_init();
	pmu_init();
	extern void xmodem_init();
	xmodem_init();
#endif
    gpio_init();
#endif

	pmu_wakeup_timer_set(5);
	pmu_register_handler(SUSPEND_COOKIE_SYSTEM, misc_suspend,
			RESUME_COOKIE_PLATFORM, misc_resume);


#if CPU_ID == 1
	/* Iniialize SMBus 1 in Master mode */
	smb_master_init(20);


	/* Initialize SMBus 0 in Slave mode to support
	 * NVME MI Basic Management commands */
	extern void smb_i2c_slv_mode_9_init(void);
	smb_slave_init(20);
	//smb_setup_nvme_mi_basic_data();
	smb_i2c_slv_mode_9_init();
    misc_chk_otp_deep_stdby_mode(); //_Benson_20211008
#endif

	vol_mon_stts_t vol_stts;
	vol_stts.all = misc_readl(VOL_MON_STTS);
	misc_writel(vol_stts.all, VOL_MON_STTS);
    //misc_set_otp_deep_stdby_mode(); //_GENE_20210928
}


slow_code UNUSED bool is_rainier_a1(void)
{
    chip_id_t chip_id = {
        .all = misc_readl(CHIP_ID),
    };

    if (chip_id.all == 0x52360003){
```

### Snippet 3: L2163-L2204
- Reasons: rtos/armv7r/misc.c+2183 gpio_isr(), rtos/armv7r/misc.c+2184 gpio_isr()

```c
			#endif
		}
    //rtos_core_trace(LOG_ERR, 0, "1:%x 2:%x 3:%x 4:%x", smb_dev_stc_data_1.all, smb_dev_stc_data_2.all, smb_dev_stc_data_3.all, smb_dev_stc_data_4.all);
    //rtos_core_trace(LOG_ERR, 0, "1:%x 2:%x 3:%x 4:%x", readl(&smb_slv->smb_dev_stc_data_1), readl(&smb_slv->smb_dev_stc_data_2),readl(&smb_slv->smb_dev_stc_data_3),readl(&smb_slv->smb_dev_stc_data_4));
	writel(ack.all, &smb_slv->smb_intr_sts);
	//log_isr = LOG_IRQ_DOWN;
}

extern volatile u8 plp_trigger;
extern u8 plp_PWRDIS_flag;
extern void plp_iic_read_write(u8 slaveID, u8 cmd_code, u8 *value, u8 data, bool rw);
fast_code void write_plp_ic(void)
{
    u8 data = 0x39; //ENA 0->1
    plp_iic_read_write(0xB4, 0x1, NULL, data, 0);
    //plp_iic_read_write(PLP_SLAVE_ID, LSC_PARAMETER_REG_ADDR, NULL, data, write);
    /*data = 0;  
    plp_iic_read_write(0xB4, 0x1, &data, 0, 1); 
    rtos_core_trace(LOG_ALW, 0xc0b7,"data 0x%x ",data);*/

}

fast_data u32 _record_lr =~0;
extern volatile u8 shr_lock_power_on;
extern void btn_de_wr_enable(void);
extern volatile bool shr_shutdownflag;
extern volatile u8 plp_trigger;
extern volatile u32 plp_log_number_start;
extern volatile u32 plp_record_cpu1_lr;

fast_code void gpio_isr(void)
{
	//log_isr = LOG_IRQ_DO;
    gpio_int_t gpio_int_status = {
        .all = misc_readl(GPIO_INT),
    };
#if(PLP_SUPPORT == 1)
#if (EN_PWRDIS_FEATURE == FEATURE_SUPPORTED)  

    if (((gpio_int_status.b.gpio_int_48 & (1 << GPIO_PLP_DETECT_SHIFT))
        ||(gpio_int_status.b.gpio_int_48 & (1 << GPIO_POWER_DISABLE_SHIFT)))
        && PWRDIS_open_flag) 
```

### Snippet 4: L2318-L2396
- Reasons: rtos/armv7r/misc.c+2338 gpio_init(), rtos/armv7r/misc.c+2367 gpio_init(), rtos/armv7r/misc.c+2376 gpio_init()

```c
fast_code void rdisk_pln_format_sanitize(u32 p0, u32 p1, u32 p2)
{
    rtos_misc_trace(LOG_INFO, 0x7f7a, "[IN]rdisk_pln_format_sanitize, pln in");

    u32 PLA;
    PLA = readl((void *)(MISC_BASE + GPIO_OUT));

    writel((PLA | BIT(GPIO_PLA_SHIFT + GPIO_OUT_SHIFT)) | BIT(GPIO_PLA_SHIFT) , (void *)(MISC_BASE + GPIO_OUT));
    PLN_FLAG = false;
    PLA_FLAG = false;
    nvmet_io_fetch_ctrl(true); //disable fetch io
    btn_de_wr_disable();
    pln_flush();
    //cpu_msg_issue(CPU_BE - 1, CPU_MSG_FPLP_TRIGGER, 0, 0);

    return ;
}

ddr_code void gpio_set_gpio0(u32 value)
{
    gpio_out_t gpio_out = {
        .all = misc_readl(GPIO_OUT),
    };

    gpio_out.b.gpio_oe |= (1 << GPIO_POWER_DISABLE_SHIFT);
    if (value) {
        gpio_out.b.gpio_out |= (1 << GPIO_POWER_DISABLE_SHIFT);
    } else {
        gpio_out.b.gpio_out &= (~(1 << GPIO_POWER_DISABLE_SHIFT));
    }

    misc_writel(gpio_out.all,GPIO_OUT);
}

fast_code void gpio_set_gpio15(u32 value)
{
    gpio_out_t gpio_out = {
		.all = misc_readl(GPIO_OUT),
	};

    gpio_out.b.gpio_oe |= (1 << GPIO_PLP_DEBUG_SHIFT);
    if (value) {
        gpio_out.b.gpio_out |= (1 << GPIO_PLP_DEBUG_SHIFT);
    } else {
        gpio_out.b.gpio_out &= (~(1 << GPIO_PLP_DEBUG_SHIFT));
    }

    misc_writel(gpio_out.all,GPIO_OUT);
}

fast_code bool gpio_get_value(u32 gpio_ofst)
{
    gpio_pad_ctrl_t gpio_ctr = {
		.all = misc_readl(GPIO_PAD_CTRL),
	};

    return (gpio_ctr.b.gpio_in & (1 << gpio_ofst)) ? true : false;
}

//plp gpio init
fast_code void gpio_init(void)
{
    /*
    Read RC0040130h [31:16] (gpio_in [15:0])
    Write 1 to RC004003Ch [16] and write the gpio_in [15:0] to RC004003Ch [15:0].
    Then GPIO0 a��?GPIO15 can be used for other purpose.
    To use GPIO as output, FW needs to program RC0040040h.  - Bits [15:0] is GPIO output enable. Need to set the
corresponding bit to 1. - Bits [31:16] is
    GPIO output. Set the corresponding bit to 0 or 1.
    RC0040134 Bits [31:16] is GPIO pull-up or pull_down resistor enable.
    Need to set the corresponding bit to 0 to disable the pull-down or pull-up for each GPIO pin
    RC0040130 Bits [15:0] is GPIO pull status control. 0 is pull_down and 1 is pull_u
    */

    //read RC0040130h
    gpio_pad_ctrl_t gpio_ctr = {
		.all = misc_readl(GPIO_PAD_CTRL),
	};
    rtos_misc_trace(LOG_ERR, 0x0519, "GPIO in value %x\n", gpio_ctr.b.gpio_in);
```

### Snippet 5: L2425-L2465
- Reasons: rtos/armv7r/misc.c+2445 GPIO_Set()

```c
    rtos_misc_trace(LOG_ERR, 0x0c12, "GPIO init mode 0x%x\n", int_mode.all);
    gpio_int_ctrl_t int_ctrl = {
		.all = misc_readl(GPIO_INT_CTRL),
	};
    //int_ctrl.all = 0;
    int_ctrl.b.gpio_int_en |= 1 << GPIO_PLP_DETECT_SHIFT;
	int_ctrl.b.gpio_int_en |= 1 << GPIO_POWER_DISABLE_SHIFT; //power disable function
    int_ctrl.b.gpio_int_en |= 1 << GPIO_PLN_SHIFT;//PLN
    misc_writel(int_ctrl.all, GPIO_INT_CTRL);
    rtos_misc_trace(LOG_ERR, 0x3dea, "GPIO int status 0x%x\n", misc_readl(GPIO_INT));

    PLA_FLAG = true;
    PLN_FLAG = true;
    PLN_evt_trigger = false;
    PLN_in_low = false;

    sirq_register(SYS_VID_GPIO, gpio_isr, false);
	//misc_sys_isr_enable(SYS_VID_GPIO);

    u32 PLA = readl((void *)(MISC_BASE + GPIO_OUT));
    //rtos_misc_trace(LOG_INFO, 0x3d7b, "PLA 0x%x",PLA);
    writel((PLA | BIT(GPIO_PLA_SHIFT)), (void *)(MISC_BASE + GPIO_OUT));

    u32 gpio_int = misc_readl(GPIO_INT);
    misc_writel(gpio_int, GPIO_INT); //W1C
    //rtos_misc_trace(LOG_ERR, 0x23fc, "GPIO int status 0x%x\n", misc_readl(GPIO_INT));

    pad_ctrl_t pda_ctrl = {
		.all = misc_readl(PAD_CTRL),
	};
    pda_ctrl.b.gpio_ie = 1;  //enable gpio input
    misc_writel(pda_ctrl.all, PAD_CTRL);
    //rtos_misc_trace(LOG_ERR, 0xa6f3, "PAD_CTRL 0x%x", misc_readl(PAD_CTRL));

    test_ctrl_t test_ctrl = {
		.all = misc_readl(TEST_CTRL),
	};

    //rtos_misc_trace(LOG_ERR, 0, "TEST_CTRL 0x%x",test_ctrl.all);

    test_ctrl.b.gpio_pull_enable = 0xfffd; //0xc0040134 gpio_pull_enable default FFFFh
```

## ftl/gc.c

- Resolved path: `src\ftl\gc.c`
- Hits: 8 (deduped), Snippets: 5 (merged)

### Snippet 1: L280-L320
- Reasons: ftl/gc.c+300 gc_busy()

```c

	gc_max_free_du_cnt = (shr_nand_info.lun_num * shr_nand_info.geo.nr_planes - RAID_SUPPORT) * DU_CNT_PER_PAGE * shr_nand_info.geo.nr_pages \
		-  shr_nand_info.geo.nr_planes * DU_CNT_PER_PAGE * shr_p2l_grp_cnt;
	gc_idle_threshold = gc_max_free_du_cnt >> 10;  // less than 1/1000 vac will trigger idle gc
}

fast_code bool gc_busy(void)
{
	if (_gc.spb_id != INV_SPB_ID)
    {
    	#if 0
    	u32 decrease = _gc.prev_free_cnt - spb_get_free_cnt(SPB_POOL_FREE);
    	if(decrease > 3)
    	{
    		decrease = 100-(decrease*10);
			if(decrease <= 0)
				decrease=10;
			ftl_ns_upd_gc_perf(_gc.nsid,decrease);
		}
		#endif
		//ftl_ns_upd_gc_perf(100);

        ftl_apl_trace(LOG_ERR, 0xf168, "_gc.spb_id: %d", _gc.spb_id);
		tzu_get_gc_info();
		return true;
    }

	return false;
}

fast_code bool gc_start(spb_id_t spb_id, u32 nsid, u32 vac, completion_t done)
{
	extern u32 shr_gc_op;
#if(PLP_SUPPORT == 0) 
    epm_FTL_t* epm_ftl_data; 
    epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag); 
#endif 
//  ftl_apl_trace(LOG_INFO, 0, "rd_gc_flag %d, shr_gc_read_disturb_ctrl %d, shr_gc_fc_ctrl %d",
//        rd_gc_flag, shr_gc_read_disturb_ctrl, shr_gc_fc_ctrl);
    if((GC_MODE_CHECK & GC_EMGR) || ((!host_idle) && (GC_MODE_CHECK & GC_FORCE)) || gc_in_small_vac || (GC_MODE_CHECK & GC_NON_PLP)){
		gc_in_small_vac = false;
```

### Snippet 2: L484-L526
- Reasons: ftl/gc.c+504 gc_start(), ftl/gc.c+506 gc_start()

```c
//		ftl_apl_trace(LOG_INFO, 0, "shr_host_write_cnt %d, pre_shr_host_write_cnt %d", shr_host_write_cnt, pre_shr_host_write_cnt);
    }
	/*
	if(otf_forcepbt == true){
		shr_host_write_cnt_old = shr_host_write_cnt;
		shr_host_write_cnt     = 512;
		ftl_apl_trace(LOG_INFO, 0, "shr_host_write_cnt_old %d",shr_host_write_cnt_old);
	}
	*/
    u32 OP = (denominator << 10) / total;
	if(global_gc_mode != GC_MD_STATIC_WL)
	{
	    pre_shr_host_write_cnt = shr_host_write_cnt;
		if(global_gc_mode <= GC_MD_LOCK)
			shr_gc_op = OP;
		if(gc_after_wl_slow_cnt)  // slow down 5% after WL
		{
		    if(freeblkCnt < GC_BLKCNT_ACTIVE && (!(GC_MODE_CHECK & GC_EMGR)))
				shr_host_write_cnt = shr_host_write_cnt * 95 / 100;
			gc_after_wl_slow_cnt--;
		}
	}
	ftl_apl_trace(LOG_INFO, 0x2ab0, "start gc spb %d (ns %d flag 0x%x sw 0x%x sn %d) OP %d/10", 
		spb_id, nsid, flags,sw_flags, _gc.req.sn, OP); 
	ftl_apl_trace(LOG_INFO, 0x40ea, "rd_gc_flag %d, shr_gc_read_disturb_ctrl %d, shr_gc_fc_ctrl %d, defect %d->%d/32, vac %d",
        rd_gc_flag, shr_gc_read_disturb_ctrl, shr_gc_fc_ctrl, gc_defect_cnt, ttl_blk_defect, vac);

#if(PLP_SUPPORT == 1) 
	if (flags & SPB_DESC_F_OPEN)
#else
    if ((flags & SPB_DESC_F_OPEN)||(epm_ftl_data->host_open_blk[0] == spb_id)||(epm_ftl_data->gc_open_blk[0] == spb_id)) 
#endif
	{
		_gc.req.flags.b.spb_open = true;
		if(flags & SPB_DESC_F_WARMBOOT_OPEN)
			_gc.req.flags.b.spb_warmboot_open = true;
		//evlog_printk(LOG_ERR,"tzu force close blk %d %d",spb_id);
		//spb_set_flag(spb_id,SPB_DESC_F_CLOSED);//for SPB_SW_F_VC_ZERO flag
	}
#if (PLP_FORCE_FLUSH_P2L == mENABLE)
	if (flags & SPB_DESC_F_PLP_LAST_P2L)
		_gc.req.flags.b.spb_spor_open = true;
#endif
```

### Snippet 3: L671-L711
- Reasons: ftl/gc.c+691 gc_end()

```c
	}
	//gc_read_only_stat = 0;
	//shr_lock_power_on &= (~READ_ONLY_LOCK_IO);  // read_only_lock --
}

fast_code void gc_end(spb_id_t spb_id, u32 write_du_cnt)
{
	//u32 gc_ttl_time;
	//u32 new_gc_perf;
	//ftl_ns_pool_t *ns_pool;
	u32 nsid = spb_get_nsid(spb_id);
	u32 *vc;
	u32 dtag_cnt;
	dtag_t dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
	bool skip_chk = false;

#ifdef ERRHANDLE_GLIST
    u16 spb_flag = 0;
#endif
	//u32 pool_id = spb_get_poolid(spb_id);
	#ifdef STOP_BG_GC
    in_gc_end = true;
    #endif
	sys_assert(_gc.spb_id == spb_id);
	ftl_apl_trace(LOG_ALW, 0xb578, "spb %d(%d) gc done, flag: 0x%x, write du %d in %d ms",
		spb_id, nsid, spb_info_get(spb_id)->flags, write_du_cnt, time_elapsed_in_ms(_gc.start_time));

	/*
	if (free_du_cnt != ~0) {
		gc_ttl_time = jiffies - _gc.start_time;
		if(gc_ttl_time == 0)
			gc_ttl_time=1;
		new_gc_perf = (free_du_cnt-(spb_get_defect_cnt(spb_id)*12*384)) / gc_ttl_time;
		//shr_host_perf = new_gc_perf * HZ;
		//u32 remain = spb_get_free_cnt(SPB_POOL_FREE);//ns_pool->quota - ns_pool->allocated;

		ftl_apl_trace(LOG_ALW, 0, "free du %d in %d 100ms", free_du_cnt, gc_ttl_time);
//		if (remain < ns_pool->gc_thr.end) {

        if (remain < GC_BLKCNT_DEACTIVE) {
            if(remain < 15)
```

### Snippet 4: L1006-L1046
- Reasons: ftl/gc.c+1026 gc_action()

```c
			}
			else
			#endif
			{
		        gc_suspend_stop_next_spb = true;
				clear_gc_suspend_flag = false;
	            //ftl_apl_trace(LOG_INFO, 0, "set gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
			    //gc is running
	    		if (_gc.spb_id != INV_SPB_ID)
	    		{
	    		     if(ftl_core_gc_action(action) == false)
	    			{

	    				//ftl_apl_trace(LOG_INFO, 0, "set gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
	    			}
	                ftl_apl_trace(LOG_INFO, 0xba6b, " gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
	    			return false;
	    		}
			}
            break;
         default:
            sys_assert(0);
            break;
    }
    ftl_apl_trace(LOG_INFO, 0x707a, " gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
	return true;
}
#if(PLP_GC_SUSPEND == mDISABLE)
fast_code bool gc_action2(gc_action_t *action)//joe add test 202011
{
	if (action->act == GC_ACT_RESUME) {
		gc_suspend_stop_next_spb = false;
		shr_gc_fc_ctrl = 2;
		_fc_credit = 0;
        global_gc_mode = 0; // ISU, GCRdFalClrWeak
		ftl_apl_trace(LOG_INFO, 0xc7fc, "[gc2]initial gc_suspend_stop_next_spb %d" ,gc_suspend_stop_next_spb);
		if (_gc.spb_id == INV_SPB_ID)
			ftl_ns_gc_start_chk(FTL_NS_ID_START);
	}
	else
	{
```

### Snippet 5: L1289-L1352
- Reasons: ftl/gc.c+1309 GC_FSM_Blk_Select(), ftl/gc.c+1331 GC_FSM_Blk_Select(), ftl/gc.c+1332 GC_FSM_Blk_Select()

```c
						gc_suspend_stop_next_spb = true;
						return INV_SPB_ID;
					}
					else
					{
						_gc.gc_mode.mode = gc_get_mode();
						if(_gc.gc_mode.mode == GC_MD_NON)
							return INV_SPB_ID;
						else
							goto re_select;
					}
				}
			}
            break;
        }
		#endif
		case GC_MD_IDLE:
		{
			can = find_min_vc(nsid, pool_id);
			if(min_vac > gc_idle_threshold || can == INV_SPB_ID)
			{
				GC_MODE_CLEAN(GC_IDLE);
				return INV_SPB_ID;
			}
			gc_in_small_vac = true;
			break;
		}
        default:
        {
            ftl_apl_trace(LOG_INFO, 0x783a, "[GC] GC False MD[%x]", _gc.gc_mode.mode);
            can = INV_SPB_ID;
            break;
        }
    }
        if(can < get_total_spb_cnt())
        {
			// ISU, EHPerfImprove
			global_gc_mode = (u32)_gc.gc_mode.mode;
			ftl_apl_trace(LOG_INFO, 0x4c26, "_gc.gc_mode.mode: %d", _gc.gc_mode.mode);
			#if (PLP_SLC_BUFFER_ENABLE  == mENABLE)
			if(can != PLP_SLC_BUFFER_BLK_ID)
			#endif
            	FTL_BlockPopPushList(SPB_POOL_GCD, can, FTL_SORT_NONE);
            //ftl_apl_trace(LOG_INFO, 0, "[GC]push block %d in GCD POOL", can);

            if(_gc.gc_mode.mode > GC_MD_LOCK)
            {
                dtag = ftl_l2p_get_vcnt_buf(&dtag_cnt, (void **) &vc);
                min_vac = vc[can];
				if(_gc.gc_mode.mode != GC_MD_STATIC_WL)
					gc_pre_vac = min_vac;
				else if(!(GC_MODE_CHECK & GC_FORCE) && (min_vac < (gc_max_free_du_cnt / 5)))
					gc_in_small_vac = true; // if WL without Force GC in small vac(20%), WL need speed up
                ftl_l2p_put_vcnt_buf(dtag, dtag_cnt, false);
            }
			else if((_gc.gc_mode.mode > GC_MD_FORCE) && (gc_pre_vac < (gc_max_free_du_cnt / 10)))
			{
				shr_gc_no_slow_down = true;  // if EMER/LOCK GC in small vac(10%), no need wait for host
			}

            ftl_apl_trace(LOG_INFO, 0x89a2, "[GC]Blk_Select:MD[0x%x]Blk[%d]VAC[%d]EC[%d]", _gc.gc_mode.mode, can, min_vac, spb_info_get(can)->erase_cnt);
			ftl_apl_trace(LOG_INFO, 0x14b5, "sw %x flag %x",spb_get_sw_flag(can),spb_get_desc(can)->flags);
            return can;
        }
```

## ftl/ftl_ns.c

- Resolved path: `src\ftl\ftl_ns.c`
- Hits: 8 (deduped), Snippets: 4 (merged)

### Snippet 1: L205-L245
- Reasons: ftl/ftl_ns.c+225 ftl_ns_alloc()

```c
	}

	sys_assert(nsid != 0);
	sys_assert(ftl_ns[nsid].capacity == 0);
	if (ftl_ns[0].capacity < cap)
		return false;

#if 0//def Dynamic_OP_En, change to LBA mode  
	epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	ftl_apl_trace(LOG_ERR, 0x4f03, "Ocan00 ns0CAP %x, OPFlag %d, size %x, cap %x, DYOPCapacity %x \n", ftl_ns[0].capacity, epm_ftl_data->OPFlag, size, cap, DYOPCapacity);

	u32 DYOPCap;
	DYOPCap = cap;
	if(epm_ftl_data->OPFlag == 1)
	{
		DYOPCap = DYOPCapacity;
	}
#endif

	ftl_apl_trace(LOG_ALW, 0xe2fa, "ftl ns create: %d %d(%d) attr: slc %d", nsid, cap, size, slc);
#if 0//def Dynamic_OP_En, change to LBA mode
	ftl_ns[nsid].capacity = DYOPCap;
#else
	ftl_ns[nsid].capacity = cap;
#endif
	ftl_ns[nsid].attr.b.slc = slc;
	ftl_ns[nsid].attr.b.native = native;
	ftl_ns[nsid].attr.b.mix = DSLC_SPB_RATIO && slc && native;

	#if 0
	ftl_ns[2].spb_queue[FTL_CORE_PBT] = &spb_queue[1][FTL_CORE_PBT];
	sys_assert(SPB_QUEUE_SZ == ARRAY_SIZE(ftl_ns[2].spb_queue[FTL_CORE_PBT]->buf));
	CBF_INIT(&ftl_ns[2].dirty_spb_que[FTL_CORE_PBT]);
	_spb_appl[2][FTL_CORE_PBT].ns_id = 2;
	_spb_appl[2][FTL_CORE_PBT].notify_func = ftl_ns_spb_notify;
	_spb_appl[2][FTL_CORE_PBT].type = FTL_CORE_PBT;
	_spb_appl[2][FTL_CORE_PBT].flags.all = 0;
	#endif
	for (i = 0; i < FTL_CORE_MAX; i++) {
		ftl_ns[nsid].spb_queue[i] = &spb_queue[nsid - 1][i];
```

### Snippet 2: L315-L428
- Reasons: ftl/ftl_ns.c+335 ftl_ns_check_appl_allocating(), ftl/ftl_ns.c+341 ftl_ns_check_appl_allocating(), ftl/ftl_ns.c+377 ftl_ns_spb_notify(), ftl/ftl_ns.c+407 ftl_ns_spb_notify(), ftl/ftl_ns.c+408 ftl_ns_spb_notify()

```c
fast_code void ftl_ns_start()
{
	u32 fc_chk_bmp = 0;

	ftl_core_start(1);
	#if (PBT_OP == mENABLE)
	ftl_core_start(FTL_NS_ID_INTERNAL);
	fc_chk_bmp = ((1 << 1) | (1 << FTL_NS_ID_INTERNAL));
	#else
	fc_chk_bmp = (1 << 1);
	#endif

	shr_ftl_flags.b.boot_cmpl = true;
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_FC_READY_CHK, 0, fc_chk_bmp);
}

slow_code void ftl_ns_check_appl_allocating(void){
	if(shr_format_fulltrim_flag){
		u8 nsid = 0;
		u8 type = 0;
		ftl_apl_trace(LOG_INFO, 0xbf45, "[IN]");
		for(nsid = FTL_NS_ID_START; nsid < FTL_NS_ID_MAX;nsid++){
			for (type = 0; type < FTL_CORE_MAX; type++) {
				if((nsid == FTL_NS_ID_START && type == FTL_CORE_PBT) || (nsid == FTL_NS_ID_INTERNAL && type == FTL_CORE_GC)){
					continue;
				}
				ftl_apl_trace(LOG_INFO, 0x69a7, "%d/%d, alloc:%d, abort:%d",nsid,type,_spb_appl[nsid][type].flags.b.allocating,_spb_appl[nsid][type].flags.b.abort);
				if(_spb_appl[nsid][type].flags.b.allocating == true && _spb_appl[nsid][type].flags.b.abort == 0){
					_spb_appl[nsid][type].flags.b.abort = true;
					if(nsid == FTL_NS_ID_START){
						ftl_apl_trace(LOG_INFO, 0x0576, "[Do]free_appl:%d/%d",nsid,type);
						ftl_free_blist_dtag(type);				
					}

					if(nsid == FTL_NS_ID_INTERNAL && type == FTL_CORE_PBT){
						pbt_query_ready 		= 1;
						pbt_query_need_resume 	= 0;	
					}
				}
			}
		}
	}
}

fast_code bool ftl_ns_spb_notify(spb_appl_t *appl)
{
	bool ret;
	spb_ent_t ent;
	ftl_ns_t *ns = &ftl_ns[appl->ns_id];

	if(appl->flags.b.abort == true){
		goto notify_abort_end;
	}	

	if (appl->flags.b.ready == 1) {
		gFtlMgr.SerialNumber++;
		spb_set_sn(appl->spb_id, gFtlMgr.SerialNumber);
        //ftl_apl_trace(LOG_INFO, 0, "[Notify]spb_id : 0x%x Blk_SN : 0x%x",appl->spb_id, gFtlMgr.SerialNumber);
	}

    if(appl->flags.b.QBT){
        gFtlMgr.LastQbtSN = gFtlMgr.SerialNumber;
        ftl_apl_trace(LOG_INFO, 0xb62c, "gFtlMgr.LastQbtSN : 0x%x", gFtlMgr.LastQbtSN);
#if(SPOR_FTLINITDONE_SAVE_QBT == mENABLE)
	    spor_qbtsn_for_epm = gFtlMgr.LastQbtSN;
#endif
    }

	if(appl->type == FTL_CORE_PBT){
		if((ftl_pbt_cnt % QBT_BLK_CNT)==0){
		    if((ps_open[FTL_CORE_NRM] != INV_U16) &&
		       (ps_open[FTL_CORE_GC] != INV_U16))
		    {
		        gFtlMgr.TableSN = (spb_get_sn(ps_open[FTL_CORE_NRM]) < spb_get_sn(ps_open[FTL_CORE_GC])) ? spb_get_sn(ps_open[FTL_CORE_GC]) : spb_get_sn(ps_open[FTL_CORE_NRM]);
				gFtlMgr.pbt_host_blk = ps_open[FTL_CORE_NRM];
				gFtlMgr.pbt_gc_blk = ps_open[FTL_CORE_GC];
			}
		    else if(ps_open[FTL_CORE_NRM] != INV_U16)
		    {
		        gFtlMgr.TableSN = spb_get_sn(ps_open[FTL_CORE_NRM]);
				gFtlMgr.pbt_host_blk = ps_open[FTL_CORE_NRM];
		    }
		    else if(ps_open[FTL_CORE_GC] != INV_U16)
		    {
		        gFtlMgr.TableSN = spb_get_sn(ps_open[FTL_CORE_GC]);
				gFtlMgr.pbt_gc_blk = ps_open[FTL_CORE_GC];
		    }
		    else
		    {
		        gFtlMgr.TableSN = gFtlMgr.SerialNumber;
		    }
			gFtlMgr.pbt_pre_host_blk = host_spb_pre_idx;
			ftl_apl_trace(LOG_INFO, 0xbbf1, "pbt:%d, TableSN set:0x%x, openHOST:%d, openGC:%d",appl->spb_id,gFtlMgr.TableSN,ps_open[FTL_CORE_NRM],ps_open[FTL_CORE_GC]);
			ftl_apl_trace(LOG_INFO, 0xbf98, "pbt_host:%d, pbt_gc:%d, preHOST:%d",gFtlMgr.pbt_host_blk, gFtlMgr.pbt_gc_blk, gFtlMgr.pbt_pre_host_blk);
		}
		//ftl_ns_qbf_clear(appl->ns_id,FTL_CORE_PBT);
	}

notify_abort_end:
	sys_assert(appl->flags.b.ready);

	ns->pools[appl->pool_id].allocated++;
	ent.all = appl->spb_id;
	ent.b.sn = spb_get_sn(appl->spb_id);
	ent.b.slc = appl->flags.b.slc | appl->flags.b.dslc;
	if(appl->flags.b.abort == true){
		ent.b.ps_abort = true;
	}
	CBF_INS(ns->spb_queue[appl->type], ret, ent.all);
	sys_assert(ret);
	appl->spb_id = INV_SPB_ID;
	appl->flags.b.ready = 0;
	appl->flags.b.allocating = 0;
	appl->flags.b.abort = 0;
```

### Snippet 3: L628-L668
- Reasons: ftl/ftl_ns.c+648 ftl_ns_gc_start_chk()

```c
                if (gc_action(action))
                {
                sys_free(FAST_DATA, action);
                }
                if(ps_open[FTL_CORE_GC] < get_total_spb_cnt())
                {
                    spb_id_t gc_open = ps_open[FTL_CORE_GC];
                    sys_assert(rd_open_close_spb[FTL_CORE_GC] == INV_SPB_ID);
                    rd_open_close_spb[FTL_CORE_GC] = gc_open;
            	    spb_set_flag(gc_open, SPB_DESC_F_OPEN);
                    ftl_ns_close_open(spb_get_nsid(gc_open), gc_open, FILL_TYPE_RD_OPEN);	// ISU, LJ1-337, PgFalClsNotDone (1)
                }
                set_open_gc_blk_rd(false);
            }
            else if(GC_MODE_CHECK)
            #else
            if(GC_MODE_CHECK)
            #endif
            {
                //if (GC_MODE_CHECK & (GC_EMGR | GC_LOCK))  // free block too few, do not slow down GC speed
            	if (spb_get_free_cnt(SPB_POOL_FREE) < GC_BLKCNT_EMER_SPD_CTL)  // free block too few, do not slow down GC speed
					shr_gc_no_slow_down = true;
				else
					shr_gc_no_slow_down = false;
				
               ftl_apl_trace(LOG_INFO, 0xef15, "GC_MODE_CHECK 0x%x", GC_MODE_CHECK);

                can = GC_FSM_Blk_Select(nsid, SPB_POOL_USER);
                //ftl_ns_upd_gc_perf(nsid, 100);
                if (can != INV_SPB_ID)
                {
            		gc_start(can, nsid, min_vac, NULL);
                }
            }
        }
    }
    #ifdef STOP_BG_GC
    else if(STOP_BG_GC_flag && in_gc_end)
    {
        gc_suspend_stop_next_spb = false;
        STOP_BG_GC_flag = 0;
```

### Snippet 4: L778-L818
- Reasons: ftl/ftl_ns.c+798 ftl_ns_make_clean()

```c

	memset(ftl_ns_desc->spb_queue, 0xff, sizeof(ftl_ns_desc->spb_queue));
}

/*!
 * @brief callback function of ftl namespace descriptor flush
 *
 * @param ctx		system log descriptor of ftl namespace descriptor
 *
 * @return		not used
 */
fast_code void ftl_ns_desc_flush_cmpl(sld_flush_t *ctx)
{
	ftl_ns_t *this_ns = container_of(ctx, ftl_ns_t, sld_flush);

	this_ns->flags.b.desc_flushing = 0;

	if (ctx->caller_cmpl)
		ctx->caller_cmpl(ctx->caller);
}

fast_code bool ftl_ns_make_clean(u32 nsid, void *caller, completion_t caller_cmpl)
{
	ftl_ns_t *this_ns = &ftl_ns[nsid];

	ftl_apl_trace(LOG_INFO, 0x4bef, "make ns %d clean, now %d", nsid,
			this_ns->ftl_ns_desc->flags.b.clean);
	if (this_ns->ftl_ns_desc->flags.b.clean)
		return true;

	this_ns->ftl_ns_desc->flags.b.clean = 1;
	ftl_apl_trace(LOG_ERR, 0xa4bf, "make clean bit 1");
	return true;
	/*
	this_ns->sld_flush.caller = caller;
	this_ns->sld_flush.caller_cmpl = caller_cmpl;
	this_ns->sld_flush.cmpl = ftl_ns_desc_flush_cmpl;
	this_ns->flags.b.desc_flushing = 1;
	sys_log_desc_flush(&this_ns->ns_log_desc, 0, &this_ns->sld_flush);
	return false;
	*/
```

## srb/srb.c

- Resolved path: `src\srb\srb.c`
- Hits: 8 (deduped), Snippets: 1 (merged)

### Snippet 1: L270-L347
- Reasons: srb/srb.c+290 defect_Rd_SRB(), srb/srb.c+296 defect_Rd_SRB(), srb/srb.c+300 defect_Rd_SRB(), srb/srb.c+307 defect_Rd_SRB(), srb/srb.c+311 defect_Rd_SRB(), srb/srb.c+318 defect_Rd_SRB(), srb/srb.c+322 defect_Rd_SRB(), srb/srb.c+327 defect_Rd_SRB()

```c
    dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);
    sys_assert(dtag.dtag != _inv_dtag.dtag);
    bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG
	};
    rda_t aging_rda = {
        .ch = Ch,
        .dev = CE,
        .pb_type = NAL_PB_TYPE_SLC,
        .du_off = DU_offset,
        .row = nda2row(page,plane,0,Lun)
    };
   if(ncl_access_mr(&aging_rda, NCL_CMD_OP_READ, &pl, 1)==0) {
        //srb_apl_trace(LOG_ERR, 0, "Read OK \n");
        if(flag == FLAG_AGINGTESTMAP) {
            //srb_apl_trace(LOG_ERR, 0, "Total 1 DU \n");
            memcpy(record, (void *)mem, sizeof(AGING_TEST_MAP_t));//1
            srb_apl_trace(LOG_ERR, 0xc630, "AgingTestMap, page: %d, DU: %d, size: %d \n",page,DU_offset,sizeof(AGING_TEST_MAP_t));
#if 1
        } else if(flag == FLAG_AGINGBITMAP) {

            if(((page-1)*3+DU_offset)<(sizeof(AgingPlistBitmap_t)/SRB_MR_DU_SZE)) {    //!!!!!!!!!!!!!!!!!!!cant get the DEFINE values
                size = SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0xd317, "AgingBitMap, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, SRB_MR_DU_SZE);
            } else {
                size = sizeof(AgingPlistBitmap_t)%SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0x3dfd, "AgingBitMap, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, size);
            }
        } else if(flag == FLAG_AGINGP1LISTTABLE) {

            if(((page-12)*3+DU_offset)<(sizeof(AgingPlistTable_t)/SRB_MR_DU_SZE)) {
                size = SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0xe2a7, "AgingP1Table, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, SRB_MR_DU_SZE);
            } else {
                size = sizeof(AgingPlistTable_t)%SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0x2e7e, "AgingP1Table, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, size);
            }
        } else if(flag == FLAG_AGINGP2LISTTABLE) {

            if(((page-36)*3+DU_offset)<(sizeof(AgingPlistTable_t)/SRB_MR_DU_SZE)) {
                size = SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0xd94e, "AgingP2Table, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, SRB_MR_DU_SZE);
            } else {
                size = sizeof(AgingPlistTable_t)%SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0x43e8, "AgingP2Table, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, size);
            }
        } else if(flag == FLAG_AGINGCTQMAP) {
			memcpy(record, (void *)mem, sizeof(CTQ_t));//1
            srb_apl_trace(LOG_ERR, 0xd2af, "AgingCTQMap, page: %d, DU: %d, size: %d \n",page,DU_offset,sizeof(CTQ_t));
        } else if(flag == FLAG_AGINGHEADER) {
			memcpy(record, (void *)mem, sizeof(stNOR_HEADER));//1
            srb_apl_trace(LOG_ERR, 0x9438, "AgingCTQMap, page: %d, DU: %d, size: %d \n",page,DU_offset,sizeof(stNOR_HEADER));
        /*} else if(flag == FLAG_ECCTABLE) {

            if((page*3+DU_offset)<(sizeof(AgingECC_t)*AgingTest->Total_ECC_Count/SRB_MR_DU_SZE)) {
                size = SRB_MR_DU_SZE;
                srb_apl_trace(LOG_ERR, 0, "ECC_Table, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, SRB_MR_DU_SZE);
            } else {
                size = sizeof(AgingECC_t)*AgingTest->Total_ECC_Count%SRB_MR_DU_SZE;                             //read AgingTestMap before read AgingECC!!!!!
                srb_apl_trace(LOG_ERR, 0, "ECC_Table, page: %d, DU: %d, size: %d \n",page,DU_offset,size);
                memcpy(record, (void *)mem, size);
            }
          */
#endif
        }
        ret = true;
    }else if(flag == FLAG_AGINGHEADER){
		ret = false;
```

## nvme/hal/ctrlr.c

- Resolved path: `src\nvme\hal\ctrlr.c`
- Hits: 8 (deduped), Snippets: 6 (merged)

### Snippet 1: L1269-L1309
- Reasons: nvme/hal/ctrlr.c+1289 hal_nvmet_rebuild_vf_sq()

```c
		pcie_set_pf_max_msi_x(msi_x_count);
	else
		nvme_hal_trace(LOG_ERR, 0x1f53, "not have pcie clock when init nvme");
#endif
}
#endif

/*!
 * @brief	rebuild sq after warm boot used for normal mode
 *
 * @param	sqid	sq id
 *
 * @return	None
 */
void hal_nvmet_rebuild_vf_sq(u16 sqid)
{
	u32 i = 0;
	//u32 val = 0;
	u32 q_ofst = 0;	/* queue offset */
	nvmet_sq_t *sq = &sq_base[sqid];

	/* Set SQ size */
	q_ofst = sqid << 3;
	flex_sqcq0_size_t q_size = {
		.all = flex_dbl_readl(FLEX_SQCQ0_SIZE + (sqid << 2)),
	};
	sq->qsize = q_size.b.sq_size;

	if((sqid == 0) || (sqid == 65))
	{
	nvme_hal_trace(LOG_INFO, 0x159e, "enable SQ (%d), QDepth(%d)", sqid, sq->qsize);
	}

	if (sqid == 0)
	{
		/* from ASIC side, use its own queue depth with empty/full control */
		adm_cmd_sram_base_func0_t aqb;

		aqb.b.adm_cmd_sram_base = btcm_to_dma(cmd_base[0]);
		aqb.b.addr_is_for_dtcm = 1;
		aqb.b.adm_cmd_fetch_max_sz = IO_SQUEUE_MAX_DEPTH - 1;
```

### Snippet 2: L3601-L3641
- Reasons: nvme/hal/ctrlr.c+3621 nvmet_config_xfer_payload()

```c
 * @return	None
 */
static fast_code void nvmet_config_xfer_payload(void)
{
	nvme_axi_interface_ctrl_t nvme_axi;
	u32 pcie_mps; /* PCIE Max payload size */
	u32 pcie_mrr; /* PCIE MAX Read request size */
	u32 cmd_p_max_pl = 0;
	u32 cmd_p_max_rd = 0;

	pcie_set_xfer_busy(true);

	/* confirm pcie reference clock */
	if (is_pcie_clk_enable())
	{
		pcie_mps = get_pcie_xfer_mps();
		pcie_mrr = get_pcie_xfer_mrr();
	}
	else
	{
		nvme_hal_trace(LOG_ERR, 0x09c7, " No pcie clock ");
		pcie_mps = 0;
		pcie_mrr = 0;
	}

	nvme_axi.all = nvme_readl(NVME_AXI_INTERFACE_CTRL);
	/*
	 * Set maximum read-req size = min { MPS , MRR}
	 * Set maximum payload size = MPS
	 */
#if (MRR_EQUAL_MPS == ENABLE)
	pcie_core_status2_t link_sts;
	link_sts.all = pcie_wrap_readl(PCIE_CORE_STATUS2);
	if(link_sts.b.neg_link_speed == PCIE_GEN2)
	{
		pcie_mrr = pcie_mps;
		nvme_hal_trace(LOG_ERR, 0xf6fb, "Gen2, Force MRR equal to MPS (%d)",pcie_mps);
	}
#endif
	nvme_hal_trace(LOG_ALW, 0xc4fd, "PCIE mps %d mrr %d", pcie_mps, pcie_mrr);
	if (pcie_mps == 0)
```

### Snippet 3: L3747-L3787
- Reasons: nvme/hal/ctrlr.c+3767 shutdown2000()

```c
#if EPM_NOT_SAVE_Again
	EPM_NorShutdown = 1;
#endif
	/* Reset SQ/CQ DB registers of function fid */
	if (fid == 0)
	{
		for (i = 0; i < SRIOV_FLEX_PF_ADM_IO_Q_TOTAL; i++)
		{
			flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
			flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
		}
	}
	else
	{
		j = SRIOV_FLEX_PF_ADM_IO_Q_TOTAL + (fid - 1) * SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC;
		for (i = j; i < (j + SRIOV_FLEX_VF_ADM_IO_Q_PER_FUNC); i++)
		{
			flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
			flex_dbl_writel(0, FLEX_CQ0_DB_REG + (i << 2));
		}
	}
}
#else
#ifdef EVT_DELAY_ARRAY
fast_code void shutdown2000(void)
#else
static void shutdown2000(void)
#endif
{
	// If any reset flow trigger, shutdown bit should be clear. 12/14 Richard modify for PCBasher
	if((gResetFlag & BIT(cNvmeShutDown)) == 0)
	{
		nvme_hal_trace(LOG_ERR, 0x23cb, "bypass shutdown2000");
		nvmet_csts_shst(NVME_SHST_NORMAL);
	}
	else
	{
#if 0
#if (TRIM_SUPPORT == ENABLE)
	extern void TrimPowerLost();
	TrimPowerLost();
```

### Snippet 4: L3863-L3903
- Reasons: nvme/hal/ctrlr.c+3883 shutdown()

```c
#else
#ifdef EVT_DELAY_ARRAY
fast_code void shutdown(void)
#else
fast_code static void shutdown(void)
#endif
{
	/*
	/ because of flush and so on, cpu1 can't do this func quickly
	/ rdy bit keep 1,and host send create io sq/cq again
	/ then do cc_en_reset flow to delete sq/cq for next cc.en set and set number of queue cmd
	*/
	if(flagtestS && flagtestC)
	{
		nvme_hal_trace(LOG_ERR, 0x766b, "delete Q not clean or maybe get admin cmd again after cc_clr");
		nvmet_cc_en_reset();
		return;
	}
	nvmet_csts_shst(NVME_SHST_NORMAL); /* No SHST has been requested */

	nvmet_fini_aqa_attrs();
	nvmet_fini_dma_queues();

	nvme_writel(0, INT_MASK_SET);
	nvme_writel(0, CONTROLLER_CONFG);
	/*
	 * When this field is clear to '0', the CSTS.RDY bit is cleared to '0'
	 * by the controller once the controller is ready to be re-enabled.
	 */
	nvmet_csts_rdy(false);
#ifndef NVME_SHASTA_MODE_ENABLE
	hal_nvmet_delete_vq_sq(0);
	hal_nvmet_delete_vq_cq(0);
	hal_nvmet_reset_normal_mode();
	flex_misc_writel(0, ADM_CMD_FETCH_EN_PF);
	flex_misc_writel(0, ADM_CMD_FETCH_EN_VF);

	int i;
	for (i = 0; i < NVMET_RESOURCES_FLEXIBLE_TOTAL; i++)
	{
		flex_dbl_writel(0, FLEX_SQ0_DB_REG + (i << 2));
```

### Snippet 5: L4266-L4343
- Reasons: nvme/hal/ctrlr.c+4286 nvmet_cc_en_clr_delay_check(), nvme/hal/ctrlr.c+4323 nvmet_shutdown_delay_check()

```c
}

ddr_code bool nvmet_rst_req_flush(void)
{
	req_t *req;

	req = nvmet_get_req();
	sys_assert(req);
	req->req_from = REQ_Q_OTHER;
	req->opcode = REQ_T_FLUSH;
	req->op_fields.flush.shutdown = false;
	req->completion = nvme_rst_req_flush_callback;

	ctrlr->nvme_req_flush = true;

	ctrlr->nsid[0].ns->issue(req);
	return true;
}

ddr_code void nvmet_cc_en_clr_delay_check(void)
{
	if (shst_running)
	{
		shst_running = false;
		nvme_apl_trace(LOG_INFO, 0x88f9, "FORCE CLR");
	}

	//if ((get_btn_running_cmds() != 0) || (nvme_check_hw_pending_by_func(0) == false))
	//if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds)!= 0)//check IO & RX cmd
	if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds != 0) ||
		//(nvme_check_hw_pending_by_func(0) == false) ||
		//(!list_empty(&nvme_sq_del_reqs)) ||
		//(hal_nvmet_check_io_sq_pending() == false) ||
		(btn_cmd_idle() != true)
	)//check IO & RX cmd && HW pointer
	{
		if (cclr_running == false)
		{
			nvme_apl_trace(LOG_INFO, 0x1e8e, "[CLR] WIO");
			cclr_running = true;
		}
	}
	else
	{
		cclr_running = false;

		if (ctrlr->nvme_req_flush == false)
		{
			extern bool ucache_clean(void);
			if (ucache_clean() == false)
			{
				nvmet_rst_req_flush();
			}
			else
			{
				nvme_apl_trace(LOG_INFO, 0xe64d, "[CLR] Cache");
				nvmet_cc_en_reset();
			}
		}
	}
}

ddr_code void nvmet_shutdown_delay_check(u32 type)
{
	//if ((get_btn_running_cmds() != 0) || (nvme_check_hw_pending_by_func(0) == false))
	//if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds)!= 0)//check IO & RX cmd
	if ((get_btn_running_cmds() + ctrlr->cmd_proc_running_cmds != 0) ||
		//(nvme_check_hw_pending_by_func(0) == false) ||
		//(!list_empty(&nvme_sq_del_reqs)) ||
		//(hal_nvmet_check_io_sq_pending() == false) ||
		(btn_cmd_idle() != true)
	)//check IO & RX cmd && HW pointer
	{
		if (shst_running == false)
		{
			nvme_apl_trace(LOG_INFO, 0x5530, "[SHST] WIO");
			shst_running = true;
		}
```

### Snippet 6: L4430-L4497
- Reasons: nvme/hal/ctrlr.c+4450 nvmet_slow_isr(), nvme/hal/ctrlr.c+4477 nvmet_slow_isr()

```c
 * @param	ctrlr_sts interrupt flag
 *
 * @return	None
 *
 */
static fast_code __attribute__((noinline)) void
nvmet_slow_isr(nvme_unmasked_int_status_t ctrlr_sts)
{
	/* Boot Partition event */
	if (ctrlr_sts.b.boot_partition_read_op)
	{
		nvme_hal_trace(LOG_ERR, 0x9067, "Host want to read Boot Partition");
		if (dma_queue_initialized == false)
		{
			nvmet_init_dma_queues();
		}
		evt_set_imt(evt_bootpart_rd, 0, 0);
		ctrlr_sts.b.boot_partition_read_op = 0;
	}

	/* so we clear Queue(s) heres if any */
	if (ctrlr_sts.b.cc_en_clear)
	{
		if(plp_trigger)
		{
			nvmet_clear_pf0_disable_active();
			return ;
		}
		cc_en_set = false;
		is_IOQ_ever_create_or_not->flag = 0;

		#if (CO_SUPPORT_READ_AHEAD == TRUE)
		ra_disable_time(25); // 2.5s// TODO DISCUSS
		#endif

		if (ctrlr_sts.b.cc_en_set && ctrlr_sts.b.cc_en_status)
		{
			nvme_hal_trace(LOG_ERR, 0xcb7b, "both cc_en and cc_dis set, cc_dis is last, ignore cc_dis");
			nvmet_clear_pf0_disable_active();
		}
		else
		{
			#if GET_PCIE_ERR
			disable_corr_uncorr_isr();
			#endif

			// Clear Shutdown bit when cc en clr. 12/14 Richard modify for PCBasher
			BEGIN_CS1
			if(gResetFlag & BIT(cNvmeShutDown))
			{
				gResetFlag &= ~BIT(cNvmeShutDown);
			}

			if (!(gResetFlag & BIT(cNvmeControllerResetClr)))
			{
				nvme_apl_trace(LOG_ALW, 0xb2ae, "[RSET] CC_EN CLR");
			}
			gResetFlag |= BIT(cNvmeControllerResetClr);
			END_CS1

			ctrlr_sts.b.cc_shn_2000 = 0;

			nvmet_cc_en_clr_delay_check();
		}
	}

	if (ctrlr_sts.b.cc_en_set)
	{
```
