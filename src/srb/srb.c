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

#include "srb.h"
#include "sect.h"
#include "stdlib.h"
#include "string.h"
#include "bf_mgr.h"
#include "queue.h"
#include "assert.h"
#include "types.h"
#include "ncl_exports.h"
#include "crc32.h"
#include "ddr.h"
#include "mpc.h"
#include "ssstc_cmd.h"
#include "dtag.h"
/*! \cond PRIVATE */
#define __FILEID__ srb
#include "trace.h"
#include "misc.h"
/*! \endcond */

#define IMAGE_SIGNATURE	0x54495247 /* GRIT */
#define IMAGE_COMBO        0x424D4F43 /* COMB */
#define IMAGE_CMFG        0x47464D43 /* CMFG */
#define FRMB_SIGNATURE 	0x46524D42 /* FRMB */
#define CTQBlk 1 //pochune
//for vu
extern AGING_TEST_MAP_t *MPIN;
extern u16 Vsc_on;
extern stNOR_HEADER *InfoHeader;
extern AgingPlistBitmap_t *AgingPlistBitmap;
extern AgingPlistTable_t *AgingP1listTable;
extern AgingPlistTable_t *AgingP2listTable;
extern CTQ_t *Aging_CTQ;
extern AgingPlistBitmap_t *AgingPlistBitmapbkup;
extern DebugLogHeader_t * DebugLogHeader; //pochune
extern u16 shr_uart_dis;
extern volatile u8 plp_trigger;

#ifdef UPDATE_LOADER
	fast_data_zi u32 slc_pgs_per_blk;
	extern bool sb_update_idx;
	extern bool sb_always_update_idx;
#endif

#ifdef CAP_IDX	//20200831
	slow_code cap_cfg_size_t cap_idx;
#endif
slow_data static volatile u8 Loader_data =0;

#if (Synology_case)
extern synology_smart_statistics_t *synology_smart_stat;
#endif

extern int ncl_cmd_simple_submit(rda_t *rda_list, enum ncl_cmd_op_t op,
				    bm_pl_t *dtag, u32 count,
				    int du_format, int stripe_id);

static void row2nda(row_t row, u8 *pln, u16 *blk, u8 *lun)
{
	*pln = (row >> nand_row_plane_shift()) & (nal_plane_count_per_lun() - 1);
	*blk = (row >> nand_row_blk_shift()) & (nand_page_num_slc() - 1);
	*lun = (row >> nand_row_lun_shift()) & (nal_lun_count_per_dev() - 1);
}

//pochune
ddr_code static void VSC_AssignDBGHead(u8* aVenBuffer)
{
	aVenBuffer[0] = 0;
	aVenBuffer[2] = 0;
	aVenBuffer[4] = 0;
	aVenBuffer[6] = 0;
	aVenBuffer[8] = CTQBlk;



}

slow_code void memprint(char *str, void *ptr, int mem_len)		//20200714-Eddie
{
	u32*pdata=NULL;
	pdata = ptr;
	srb_apl_trace(LOG_ERR, 0x8157, "%s \n",*str);
    int c=0;
	for (c = 0; c < (mem_len/ sizeof(u32)); c++) {
	            if (1) {
	                    if ((c & 3) == 0) {
	                            srb_apl_trace(LOG_ERR, 0x14d5, "%x:", c << 2);
	                    }
	                    srb_apl_trace(LOG_ERR, 0x223f, "%x ", pdata[c]);
	                    if ((c & 3) == 3) {
	                            srb_apl_trace(LOG_ERR, 0x9d1c, "\n");
	                    }
	            }
	        }
}
slow_code void dtagprint(dtag_t d2m, int mem_len)		//20200714-Eddie
{
	u32*pdata=NULL;
	pdata = dtag2mem(d2m);
    int c =0;
	for (c = 0; c < (mem_len/ sizeof(u32)); c++) {
	            if (1) {
	                    if ((c & 3) == 0) {
	                            srb_apl_trace(LOG_ERR, 0x387d, "%x:", c << 2);
	                    }
	                    srb_apl_trace(LOG_ERR, 0x3867, "%x ", pdata[c]);
	                    if ((c & 3) == 3) {
	                            srb_apl_trace(LOG_ERR, 0x5a8a, "\n");
	                    }
	            }
	        }
}

slow_code bool srb_alloc_block(dft_btmp_t *dft, bool use_die0, rda_t *rda)
{
	u8 _pln, _lun, _dev, _ch;
	u16 spb_idx;
	u16 pb_idx;
	for (spb_idx = 0; spb_idx < SRB_BLKNO; spb_idx++) {
		for (pb_idx = (use_die0? 0 : (1 << LUN_SHF));
				pb_idx < (use_die0? (1 << LUN_SHF) : (sizeof(dft_btmp_t) << 3)); pb_idx++) {
			if (!(dft[spb_idx].dft_bitmap[pb_idx >> 5] & BIT(pb_idx & ((1 << 5) - 1)))) {
				dft[spb_idx].dft_bitmap[pb_idx >> 5] |= BIT(pb_idx & ((1 << 5) - 1));
				goto found;
			}
		}
	}

	return false;

found:
	_pln = (pb_idx >> PLN_SHF) & (nal_plane_count_per_lun() - 1);
	_lun = (pb_idx >> LUN_SHF) & (nal_lun_count_per_dev() - 1);
	_dev = (pb_idx >> CE_SHF) & (nal_get_targets() - 1);
	_ch = (pb_idx >> CH_SHF) & (nal_get_channels() - 1);

	rda->du_off = 0;
	rda->pb_type = NAL_PB_TYPE_SLC;
	rda->row = (spb_idx << nand_row_blk_shift()) | (_lun << nand_row_lun_shift()) | (_pln << nand_row_plane_shift());
	rda->ch = _ch;
	rda->dev = _dev;

	return true;
}

slow_code void mr_df_to_layout(dft_btmp_t *dft, u8 *bbt, media_layout_t *layout)
{
	u32 pl;
	u32 ch;
	u32 ce;
	u32 lun;

	for (pl = 0; pl < (1 << layout->pln_bts); pl++) {
		for (ch = 0; ch < (1 << layout->ch_bts); ch++) {
			for (ce = 0; ce < (1 << layout->ce_bts); ce++) {
				for (lun = 0 ; lun < (1 << layout->lun_bts); lun++) {
					u32 dpos;
					u32 off;
					u32 idx;

					// mr df
					dpos = pl << PLN_SHF;
					dpos += lun << LUN_SHF;
					dpos += ce << CE_SHF;
					dpos += ch << CH_SHF;

					idx = dpos >> 5;
					off = dpos & (31);
					if ((dft->dft_bitmap[idx] & (1 << off)) == 0)
						continue;	// valid

					// defect
					// compact df pos
					dpos = pl << layout->pln_shf;
					dpos += lun << layout->lun_shf;
					dpos += ce << layout->ce_shf;
					dpos += ch << layout->ch_shf;
					idx = dpos >> 3;
					off = dpos & (7);

					bbt[idx] |= (1 << off);
				}
			}
		}
	}
}

slow_code void fbbt_trans_layout(dft_btmp_t *dft, u8 *bbt, media_layout_t *layout, u32 spb_count, u32 width_byte)
{
	media_layout_t _layout = {
			ftl_media_layout
	};
	int spb_id;

	if (layout == NULL)
		layout = &_layout;

	for (spb_id = 0; spb_id < spb_count; spb_id++) {
		mr_df_to_layout(&dft[spb_id], bbt, layout);
		bbt += width_byte;
	}
}

extern int ncl_access_mr(rda_t *rda_list, enum ncl_cmd_op_t op, bm_pl_t *dtag, u32 count);
slow_code bool srb_load_fbbt(dft_btmp_t *fbbt, u32 size, rda_t rda, u32 dus)
{
	int i, j;
	u32 *mem;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG
	};
	for (i = 0; i < dus; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if (i + j < dus) {
				rda.du_off = j;
				memset(mem, 0, DTAG_SZE);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) == 0) {
					/* before copy, we need make sure the left copy size is less than one MR DU size, so we plus one */
					if ((i + j + 1) * SRB_MR_DU_SZE < size) {
						memcpy((void *)fbbt + (i + j) * SRB_MR_DU_SZE,
								dtag2mem(dtag), SRB_MR_DU_SZE);
					} else {
						memcpy((void *)fbbt + (i + j) * SRB_MR_DU_SZE,
								dtag2mem(dtag), size - (i + j) * SRB_MR_DU_SZE);
					}
				} else {
					dtag_put(DTAG_T_SRAM, dtag);
					return false;
				}
			}
		}
		rda.row += 1;
	}

	dtag_put(DTAG_T_SRAM, dtag);

	return true;
}


// Tao comment: I believe you can call row_assemble() function in ncl
static row_t nda2row(u32 pgn, u8 pln, u16 blk, u8 lun)
{
	return (pgn << nand_row_page_shift()) | (pln << nand_row_plane_shift()) | (blk << nand_row_blk_shift()) | (lun << nand_row_lun_shift());
}

bool defect_Rd_SRB(void *record, u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page,u8 DU_offset,u8 flag)
{
    bool ret;
    u32 *mem;
    u32 size;
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
	}
	else{
        srb_apl_trace(LOG_ERR, 0x310f, "Read fail \n");
        ret = false;
    }
    dtag_put(DTAG_T_SRAM, dtag);
    return ret;
}

bool defect_Read_SRB(void *record, u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page,u8 DU_offset, u8 flag)
{
    bool ret;
    u32 j, k;
    u32 *mem;
    u32 flt_size_per_spb = nand_interleave_num() / 32 * sizeof(u32);
    u32 aging_size_per_spb = sizeof(u32) * FACTORY_DEFECT_DWORD_LEN;
    u32 aging_cnt_per_du = SRB_MR_DU_SZE / aging_size_per_spb;
    dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);
    sys_assert(dtag.dtag != _inv_dtag.dtag);
	sys_assert(ncl_enter_mr_mode() != false);
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

            memcpy(record, (void *)mem, SRB_MR_DU_SZE);//1
            srb_apl_trace(LOG_ERR, 0x64e0, "AgingTestMap, page: %d, DU: %d \n",page,DU_offset);
        }else if (flag == FLAG_AGINGBITMAP) {
    		if (((page - 1) * 3 + DU_offset) < (sizeof(AgingPlistBitmap_t) / SRB_MR_DU_SZE)) {
    			srb_apl_trace(LOG_ERR, 0x694b, "AgingBitMap1, page: %d, DU: %d, size: %d\n", page, DU_offset, SRB_MR_DU_SZE);
    			for (j = 0; j < aging_cnt_per_du ; j++) {
    	            memcpy((record + j * flt_size_per_spb), (void *)(mem + j * FACTORY_DEFECT_DWORD_LEN), flt_size_per_spb);
                    //df = record + j * flt_size_per_spb;
                    //srb_apl_trace(LOG_INFO, 0, "FTL record:%x j:%d",record,j);
                    //srb_apl_trace(LOG_INFO, 0, "%x %x", df[0], df[1]);
                 }
    		}
            else {
    			u32 size = sizeof(AgingPlistBitmap_t)%SRB_MR_DU_SZE;
    			srb_apl_trace(LOG_ERR, 0xcfb2, "AgingBitMap2, page: %d, DU: %d, remaining size: %d\n",page,DU_offset,size);
    			k = (size + aging_size_per_spb) / aging_size_per_spb;
    			for (j = 0; j < k ; j++) {
    	            memcpy((record + j * flt_size_per_spb), (void *)(mem + j * FACTORY_DEFECT_DWORD_LEN), flt_size_per_spb);
                    //df = mem + j * FACTORY_DEFECT_DWORD_LEN;
                    //srb_apl_trace(LOG_INFO, 0, "Aging");
                    //srb_apl_trace(LOG_INFO, 0, "%x %x", df[0], df[1]);
                    //df = record + j * flt_size_per_spb / sizeof(u32);
                    //srb_apl_trace(LOG_INFO, 0, "FTL");
                    //srb_apl_trace(LOG_INFO, 0, "%x %x", df[0], df[1]);
                 }
    		}

		}
        ret = true;
    }else {
        srb_apl_trace(LOG_ERR, 0xe97f, "Read fail \n");
        ret = false;
    }

    dtag_put(DTAG_T_SRAM, dtag);
    return ret;
}

#ifdef SAVE_DDR_CFG		//20200922-Eddie
slow_code void FW_CONFIG_Rebuild(fw_config_set_t *fw_config)
{
	srb_t *srb = (srb_t *)SRB_HD_ADDR;

	int i,pgs,ctq_pgs = CTQ_PGS;
	rda_t ctq_pri_pos = srb->ctq_pri_pos;
	rda_t ctq_mir_pos = srb->ctq_mir_pos;

	int j = 0;
	bm_pl_t pl[DU_CNT_PER_PAGE];

	if (!ncl_enter_mr_mode()) {
		srb_apl_trace(LOG_ERR, 0x3f24, "clrddrdone enter mr mode fail \n");
		return;
	}

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_INFO, 0xce84, "SRB_MR_DU_CNT_PAGE2");
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);

	//memprint("fw_config",fw_config,4096);

	if ((u32) fw_config > DDR_BASE)
		memcpy(dtag2mem(dbase[0]),(void*)fw_config, sizeof(fw_config_set_t));
	else
		dbase[0] = mem2dtag(fw_config);

	for (j = 0; j < DU_CNT_PER_PAGE; j++) {
		pl[j].pl.dtag = (j ==0) ?  dbase[0].dtag: WVTAG_ID;
		pl[j].pl.du_ofst = j;
		pl[j].pl.nvm_cmd_id = 0;
		pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}
	#if 0
		bm_pl_t pl = {
			.pl.dtag = mem2dtag(com_buf),
			.pl.du_ofst = 0,
			.pl.ctag = 0,
		};
	#endif

	if (ncl_cmd_simple_submit(&ctq_pri_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0) != 0){
		srb_apl_trace(LOG_ERR, 0xd98a, "CTQ Erase fail , rda : CH %d CE %d row 0x%x \n", srb->ctq_pri_pos.ch, srb->ctq_pri_pos.dev, srb->ctq_pri_pos.row);
		}
	//if (ncl_cmd_simple_submit(&ctq_mir_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0) != 0){
	//	srb_apl_trace(LOG_ERR, 0, "CTQ (m) Erase fail , rda : CH %d CE %d row 0x%x \n", srb->ctq_mir_pos.ch, srb->ctq_mir_pos.dev, srb->ctq_mir_pos.row);
	//	}

	//memprint("main save",com_buf,4096);

		for (pgs = 0; pgs < ctq_pgs; pgs++) {
		rda_t prda;
		//Jerry: no ctq mir
		for (i = 0; i < 1; i++) {
			prda = (i == 0) ? ctq_pri_pos : ctq_mir_pos;		//use CTQ primary, mirror
			srb_apl_trace(LOG_ERR, 0xff33, "Program CTQ %s Row(0x%x)@CH/CE(%d/%d) \n",(i == 0 ? "_CTQ_" : "_CTQ(m)_"), prda.row, prda.ch, prda.dev);

			rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
				tflush_rdas[j] = prda;

				if (ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1) != 0){
					if (i == 0)
					  srb_apl_trace(LOG_ERR, 0xc407, "CTQ  Write fail , rda : CH %d CE %d row 0x%x \n", ctq_pri_pos.ch, ctq_pri_pos.dev, ctq_pri_pos.row);
					else
					  srb_apl_trace(LOG_ERR, 0x11b0, "CTQ (m) Write fail , rda : CH %d CE %d row 0x%x \n", ctq_mir_pos.ch, ctq_mir_pos.dev, ctq_mir_pos.row);
				}

			}
		}

		ctq_pri_pos.row++;
		//ctq_mir_pos.row++;
	}
	dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);

	ncl_leave_mr_mode();
}
#endif

extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
slow_code bool erase_srb(void)		//20201014-Eddie
{
	u8 ch, ce, lun, pln;
	pda_t pda=0;
	bool retval = true;

	srb_apl_trace(LOG_ERR, 0x8eda, "\n");

	ncl_enter_mr_mode();

	for (ch = 0; ch < (nal_get_channels()-4); ch++) {		//Only ch 0 ~ 3
		for (ce = 0; ce < nal_get_targets(); ce++) {
			for (lun = 0; lun < nal_lun_count_per_dev(); lun++) {	//20200921-Eddie
				for (pln = 0; pln < nand_plane_num(); pln++) {

				       pda = 0 << nand_info.pda_block_shift;
					pda |= 0 << nand_info.pda_page_shift;
					pda |= ch << nand_info.pda_ch_shift;
					pda |= ce << nand_info.pda_ce_shift;
					pda |= lun << nand_info.pda_lun_shift;
					pda |= pln << nand_info.pda_plane_shift;

					retval = erase_pda(pda, NAL_PB_TYPE_SLC);

					}
				}
			}
		}
	ncl_leave_mr_mode();

	srb_apl_trace(LOG_ERR, 0x15c9, "ALLSRB Erased (ch0~ch4 block0): %d \n",retval);
	return retval;
}

///////////////////////////////
extern u8 FTL_scandefect;
extern epm_info_t* shr_epm_info;
slow_code void srb_load_mr_defect(u8 *ftl_bitmap, u32 bbt_width)
{
#if Aging_defect

	sys_assert(ncl_enter_mr_mode() != false);

	u8 i,offset;
	u8 *des = ftl_bitmap;
	u32 status;
    u32 aging_ftl_ratio = FACTORY_DEFECT_DWORD_LEN / (nand_interleave_num() / 32);
	epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

	offset = sizeof(AgingPlistBitmap_t)/(SRB_MR_DU_SZE)+1;
    srb_apl_trace(LOG_ERR, 0x01cd, "read %d DUs, des %d  \n", offset, *des);


    for( i=0;i<offset;i++) {
		u8 cur_pl = 0;    //Sean_20220504
        //status = defect_Read_SRB((void *)(((u32)des) + i*SRB_MR_DU_SZE/2), 4,0,0,cur_pl,(i/3+1),(i%3),FLAG_AGINGBITMAP);  //Aging defect tbl use 512bit, but normal use 256bit
        status = defect_Read_SRB((void *)(((u32)des) + i * (SRB_MR_DU_SZE / aging_ftl_ratio)), 4,0,0,cur_pl,(i/3+1),(i%3),FLAG_AGINGBITMAP);  //Aging defect tbl use 512bit, but normal use 256bit
        if (!status) {
			cur_pl = (cur_pl == 0) ? 1 : 0;
			//status = defect_Read_SRB((void *)(((u32)des) + i*SRB_MR_DU_SZE/2), 4,0,0,cur_pl,(i/3+1),(i%3),FLAG_AGINGBITMAP);
            status = defect_Read_SRB((void *)(((u32)des) + i * (SRB_MR_DU_SZE / aging_ftl_ratio)), 4,0,0,cur_pl,(i/3+1),(i%3),FLAG_AGINGBITMAP);  //Aging defect tbl use 512bit, but normal use 256bit
        }
	}

	ncl_leave_mr_mode();

	u32* Tag_ptr  =(u32*)(((u32)ftl_bitmap) + ((sizeof(AgingPlistBitmap_t) - sizeof(u32) )/aging_ftl_ratio));

	if( *Tag_ptr != 0x50303031) //DEFECT_BITMAP_TAG 0x50303031
	{
		srb_apl_trace(LOG_ERR, 0x8c66, " Attention : no aging defect, load srb defect %x \n", *Tag_ptr);
		srb_t *srb = (srb_t *)SRB_HD_ADDR;
		srb_apl_trace(LOG_ERR, 0x54c6, "dftb_ftl_sz %x, ALL_defect_scan_Done %d \n", srb->dftb_ftl_sz, srb->ALL_defect_scan_Done);
		if ((srb->dftb_ftl_sz != 0) && (srb->ALL_defect_scan_Done == 1))
		{
			dtag_t rdtag;
			rda_t rda;
			dft_btmp_t *sou;
			u16 cur_spb;
			u16 eve_spb;
			u8 dtag_cnt = SRB_MR_DU_CNT_PAGE;
			u8 rda_oft;
			bool ret;
			u8 *des = ftl_bitmap;
			u32 width_byte;

			width_byte = occupied_by(nand_interleave_num(), bbt_width << 3);
			width_byte *= bbt_width;
			rdtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
			sys_assert(rdtag.dtag != _inv_dtag.dtag);
			sou = (dft_btmp_t *)dtag2mem(rdtag);
			sys_assert(ncl_enter_mr_mode() != false);
			eve_spb = dtag_cnt * DTAG_SZE / sizeof(dft_btmp_t);
			rda_oft = 0;
			for (cur_spb = 0; cur_spb < nand_block_num(); cur_spb += eve_spb) {
				u32 cnt = min(eve_spb, nand_block_num() - cur_spb);

				rda = srb->dftb_pos;
				rda.row += rda_oft;
				ret = srb_load_fbbt(sou, dtag_cnt * DTAG_SZE, rda, dtag_cnt);
				if (ret == false) {
					rda = srb->dftb_m_pos;
					rda.row += rda_oft;
					ret = srb_load_fbbt(sou, dtag_cnt * DTAG_SZE, rda, dtag_cnt);
				}
				if (ret == false)
				{
					srb_apl_trace(LOG_ERR, 0x0561, "Attention : SRB defect load fail, do FTL_scan_defect \n");
					goto Scan_defect;
				}
				sys_assert(ret != false);
				fbbt_trans_layout(sou, des, NULL, cnt, width_byte);
				rda_oft++;
				des += eve_spb * width_byte;
				epm_ftl_data->DefectSource = 2 ; //load from SRB defect
			}
			ncl_leave_mr_mode();
			dtag_cont_put(DTAG_T_SRAM, rdtag, dtag_cnt);
		}
		else
		{
Scan_defect:
			FTL_scandefect = 1;
			srb_apl_trace(LOG_ERR, 0x1453, "Attention Attention : no agingdefect, no SRB defect, scan defect now \n");
			epm_ftl_data->DefectSource = 3 ; //FW scan defect
			//panic("not support yet, please run agingtest or use all_scan_defect programmer ");
		}
	}
	else
	{
		epm_ftl_data->DefectSource = 1 ; //load from Aging defect
	}
	epm_update(FTL_sign,(CPU_ID-1));
#else
		 dtag_t rdtag;
		 rda_t rda;
		 srb_t *srb = (srb_t *)SRB_HD_ADDR;
		 dft_btmp_t *sou;
		 u16 cur_spb;
		 u16 eve_spb;
		 u8 dtag_cnt = SRB_MR_DU_CNT_PAGE;
		 u8 rda_oft;
		 bool ret;
		 u8 *des = ftl_bitmap;
		 u32 width_byte;

		 width_byte = occupied_by(nand_interleave_num(), bbt_width << 3);
		 width_byte *= bbt_width;
		 rdtag = dtag_cont_get(DTAG_T_SRAM, dtag_cnt);
		 sys_assert(rdtag.dtag != _inv_dtag.dtag);
		 sou = (dft_btmp_t *)dtag2mem(rdtag);
		 sys_assert(ncl_enter_mr_mode() != false);
		 eve_spb = dtag_cnt * DTAG_SZE / sizeof(dft_btmp_t);
		 rda_oft = 0;
		 for (cur_spb = 0; cur_spb < nand_block_num(); cur_spb += eve_spb) {
			 u32 cnt = min(eve_spb, nand_block_num() - cur_spb);

			 rda = srb->dftb_pos;
			 rda.row += rda_oft;
			 ret = srb_load_fbbt(sou, dtag_cnt * DTAG_SZE, rda, dtag_cnt);
			 if (ret == false) {
				 rda = srb->dftb_m_pos;
				 rda.row += rda_oft;
				 ret = srb_load_fbbt(sou, dtag_cnt * DTAG_SZE, rda, dtag_cnt);
			 }
			 sys_assert(ret != false);
			 fbbt_trans_layout(sou, des, NULL, cnt, width_byte);
			 rda_oft++;
			 des += eve_spb * width_byte;
		 }
		 ncl_leave_mr_mode();
		 dtag_cont_put(DTAG_T_SRAM, rdtag, dtag_cnt);

#endif
}
extern u8* aging_pt_defect_tbl;
#if EPM_OTF_Time
extern bool _fg_warm_boot;
#endif
init_code void mpin_init(){
	#if !defined(PROGRAMMER)
		//for mpin map init
			u32 cnt = 0;
			u32 base;

			//pochune
			cnt = occupied_by((sizeof(stNOR_HEADER) + 2*sizeof(AgingPlistBitmap_t) + sizeof(AGING_TEST_MAP_t) + 2*sizeof(AgingPlistTable_t) + sizeof(CTQ_t))+sizeof(DebugLogHeader_t), DTAG_SZE);

			base = ddtag2mem(ddr_dtag_epm_register(cnt));
			InfoHeader = (stNOR_HEADER *) base;
			AgingPlistBitmap = (AgingPlistBitmap_t *)(base + sizeof(stNOR_HEADER));
			MPIN = (AGING_TEST_MAP_t *)(base + sizeof(stNOR_HEADER) + sizeof(AgingPlistBitmap_t));
			AgingP1listTable = (AgingPlistTable_t *)(base + sizeof(stNOR_HEADER) + sizeof(AgingPlistBitmap_t) + sizeof(AGING_TEST_MAP_t));
			AgingP2listTable = (AgingPlistTable_t *) (base + sizeof(AgingPlistBitmap_t) + sizeof(AGING_TEST_MAP_t) + sizeof(AgingPlistTable_t) + sizeof(stNOR_HEADER));
			Aging_CTQ = (CTQ_t *)(base + sizeof(AgingPlistBitmap_t) + sizeof(AGING_TEST_MAP_t) + sizeof(AgingPlistTable_t)*2 + sizeof(stNOR_HEADER));
			AgingPlistBitmapbkup = (AgingPlistBitmap_t *) (base + sizeof(AgingPlistBitmap_t) + sizeof(AGING_TEST_MAP_t) + sizeof(AgingPlistTable_t)*2 + sizeof(CTQ_t) + sizeof(stNOR_HEADER));
			DebugLogHeader = (DebugLogHeader_t *)(base + sizeof(AgingPlistBitmap_t)+sizeof(AgingPlistBitmap_t) + sizeof(AGING_TEST_MAP_t) + sizeof(AgingPlistTable_t)*2 + sizeof(CTQ_t) + sizeof(stNOR_HEADER));
			aging_pt_defect_tbl = (u8*)AgingPlistBitmap;

#if EPM_OTF_Time
			if(_fg_warm_boot == false)
#endif
			{
			memset((void *)MPIN, 0x00, sizeof(AGING_TEST_MAP_t));
			memset((void *)InfoHeader, 0x00, sizeof(AGING_TEST_MAP_t));
			memset((void *)AgingPlistBitmap, 0x00, sizeof(AgingPlistBitmap_t));
			memset((void *)AgingP1listTable, 0x00, sizeof(AgingPlistTable_t));
			memset((void *)AgingP2listTable, 0x00, sizeof(AgingPlistTable_t));
			memset((void *)Aging_CTQ, 0x00, sizeof(CTQ_t));
			memset((void *)AgingPlistBitmapbkup, 0x00, sizeof(AgingPlistBitmap_t));
			//pochune
			memset((void *)DebugLogHeader, 0x00, sizeof(DebugLogHeader_t));
			VSC_AssignDBGHead((u8*)DebugLogHeader->log_seq);

#if 1
			u32 status;
			u8 offset,i;
			u8 cur_pl = 0;   
			ncl_enter_mr_mode();
			status = defect_Rd_SRB((void *)MPIN, 4, 0, 0, cur_pl, 0, 0, FLAG_AGINGTESTMAP);
			ncl_leave_mr_mode();

			if(!status){
				cur_pl = (cur_pl == 0) ? 1 : 0;
				ncl_enter_mr_mode();
				status = defect_Rd_SRB((void *)MPIN, 4, 0, 0, cur_pl, 0, 0, FLAG_AGINGTESTMAP);
				ncl_leave_mr_mode();
				if(!status){
					MPIN->aging_signature = SIGNATURE_AGING;
					MPIN->test_station.station1.signature = SIGNATURE_STATION1;
	    			MPIN->test_station.station2.signature = SIGNATURE_STATION2;
					MPIN->Vsc_tag = 0;
					srb_apl_trace(LOG_ERR, 0xb1aa, "read MPIn fail\n");
					//sys_assert(status == true);
				}

			}
			offset = sizeof(AgingPlistTable_t)/SRB_MR_DU_SZE+1;

			ncl_enter_mr_mode();
			for(i = 0; i < offset; i++) {
				defect_Rd_SRB((void *)((u32)AgingP1listTable + i*SRB_MR_DU_SZE),4, 0, 0, cur_pl, (i/3+AGINGP1LISTTABLE_STARTPAGE), (i%3),FLAG_AGINGP1LISTTABLE);
				//if(!status) {srb_apl_trace(LOG_ERR, 0, "Read AgingP1listTable Fail! \n"); return 0;}
			}
			for(i = 0; i < offset; i++) {
				defect_Rd_SRB((void *)((u32)AgingP2listTable + i*SRB_MR_DU_SZE), 4, 0, 0,cur_pl, (i/3+AGINGP2LISTTABLE_STARTPAGE), (i%3),FLAG_AGINGP2LISTTABLE);
				//if(!status) {srb_apl_trace(LOG_ERR, 0, "Read AgingP2listTable Fail! \n"); return 0;}
			}
			ncl_leave_mr_mode();

			offset = sizeof(AgingPlistBitmap_t)/(SRB_MR_DU_SZE)+1;

			ncl_enter_mr_mode();
			for(i = 0; i < offset; i++) {
				defect_Rd_SRB((void *)((u32)AgingPlistBitmap + i*SRB_MR_DU_SZE),4, 0, 0, cur_pl, (i/3+1),(i%3), FLAG_AGINGBITMAP);
			}
			defect_Rd_SRB((void *)(Aging_CTQ), 4, 0, 0, cur_pl, AGINGCTQMAP_STARTPAGE, 0, FLAG_AGINGCTQMAP);
			status = defect_Rd_SRB((void *)(InfoHeader), 4, 0, 0, cur_pl, AGINGHEADER_STARTPAGE, 0, FLAG_AGINGHEADER);
			if((status == false)||(InfoHeader->d.MainHeader.d.wSize != 32)) {
				InfoHeader->d.MainHeader.d.dwTag = 0x4E495953;//SYIN
				InfoHeader->d.MainHeader.d.wSize = 32;
				InfoHeader->d.MainHeader.d.wSubHeaderSize = 64;
				InfoHeader->d.MainHeader.d.wSubHeaderCnt = 7;
				InfoHeader->d.MainHeader.d.dwVerNo = 666;


				InfoHeader->d.SubHeader.d.BitMap.d.dwTag = 0x4D544942;//Bitmap
				InfoHeader->d.SubHeader.d.BitMap.d.dwDataSize = sizeof(AgingPlistBitmap_t);
				InfoHeader->d.SubHeader.d.BitMap.d.dwDataOffset = (u32)AgingPlistBitmap - (u32)InfoHeader;

				InfoHeader->d.SubHeader.d.TestMap.d.dwTag = 0x4E49504D;//Agingtestmap(MPinfo)
				InfoHeader->d.SubHeader.d.TestMap.d.dwDataSize = sizeof(AGING_TEST_MAP_t);
				InfoHeader->d.SubHeader.d.TestMap.d.dwDataOffset = (u32)MPIN - (u32)InfoHeader;

				InfoHeader->d.SubHeader.d.P1table.d.dwTag = 0x31544C50;//P1list
				InfoHeader->d.SubHeader.d.P1table.d.dwDataSize = sizeof(AgingPlistTable_t);
				InfoHeader->d.SubHeader.d.P1table.d.dwDataOffset = (u32)AgingP1listTable - (u32)InfoHeader;

				InfoHeader->d.SubHeader.d.P2table.d.dwTag = 0x32544C50;//P2list
				InfoHeader->d.SubHeader.d.P2table.d.dwDataSize = sizeof(AgingPlistTable_t);
				InfoHeader->d.SubHeader.d.P2table.d.dwDataOffset = (u32)AgingP2listTable - (u32)InfoHeader;

				InfoHeader->d.SubHeader.d.CTQmap.d.dwTag = 0x4D515443;//CTQ
				InfoHeader->d.SubHeader.d.CTQmap.d.dwDataSize = sizeof(CTQ_t);
				InfoHeader->d.SubHeader.d.CTQmap.d.dwDataOffset = (u32)Aging_CTQ - (u32)InfoHeader;

				InfoHeader->d.SubHeader.d.AgingMap.d.dwTag = 0x4E494741;//Agingmap


				InfoHeader->d.SubHeader.d.AgingECC.d.dwTag = 0x54434345;//ECCtable
				InfoHeader->d.SubHeader.d.AgingECC.d.dwDataSize = 272*128*40;
				InfoHeader->d.SubHeader.d.AgingECC.d.dwDataOffset = (u32)Aging_CTQ + sizeof(CTQ_t) - (u32)InfoHeader;
				srb_apl_trace(LOG_ERR, 0xac3a, "read header fail\n");
			}
			ncl_leave_mr_mode();
            srb_apl_trace(LOG_ERR, 0xf154, "aging bitmap:0x%x, MPIN0x%x", AgingPlistBitmap, MPIN);
			}

#if (Synology_case)
            //SMART( nvmet_restore_smart_stat() ) hasn't been inited yet, so init here.
            epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
            synology_smart_stat = (synology_smart_statistics_t *)epm_smart_data->synology_smart_save;
            synology_smart_stat->total_early_bad_block_cnt = MPIN->defect_count.totaldefect;
            srb_apl_trace(LOG_ERR, 0xb389, "total_early_bad_block_cnt:0x%x", synology_smart_stat->total_early_bad_block_cnt);
#endif

#endif
#endif
	Vsc_on = MPIN->Vsc_tag;    //get Vsc on/off tag
	shr_uart_dis = (MPIN->uart_dis == 0xA5) ? 1 : 0;    //0xA5 = disable log
	evlog_printk(LOG_ALW,"Disable Uart Log = %x", shr_uart_dis);
}

extern void nal_get_first_dev(u32 *ch, u32 *dev);
slow_code bool srb_scan_and_load(dtag_t *srb_dtag)
{
	u32 ch, dev;
	u32 pln;
	u32 blkno;
	srb_t *srb;
	bool retval = true;

	nal_get_first_dev(&ch, &dev);

	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void *)&srb);
	if (dtag.dtag == _inv_dtag.dtag)
		sys_assert(0);

	memset(dtag2mem(dtag), 0, DTAG_SZE);

	bm_pl_t pl = {
		.pl.dtag = dtag.b.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG
	};

	rda_t srb_hdr_rda = {
		.ch = ch,
		.dev = dev,
		.du_off = 0,
		.pb_type = NAL_PB_TYPE_SLC,
	};

	/* Scan SRB accordingly */
	for (blkno = 0; blkno < SRB_BLKNO; blkno++) {
		for (pln = 0; pln < nand_plane_num(); pln++) {
			srb_hdr_rda.row = nda2row(0, pln, blkno, 0); /* always use LUN0 */
			srb_apl_trace(LOG_ALW, 0xaaa3, "SRB hdr scan Row(0x%x)@CH/CE(%d/%d)", srb_hdr_rda.row, srb_hdr_rda.ch, srb_hdr_rda.dev);
			if (ncl_access_mr(&srb_hdr_rda, NCL_CMD_OP_READ, &pl, 1) != 0)
				continue;

			u32 sig_hi = (srb->srb_hdr.srb_signature >> 32);
			u32 sig_lo = srb->srb_hdr.srb_signature;
			srb_apl_trace(LOG_ALW, 0x5727, "SRB Signature -> 0x%x%x", sig_hi, sig_lo);
			srb_apl_trace(LOG_ALW, 0x15f9, "SRB Header CRC:[0x%x] [0x%x]", srb->srb_hdr.srb_csum, crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum)));

			if (srb->srb_hdr.srb_signature == SRB_SIGNATURE &&
					(srb->srb_hdr.srb_csum == crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum))))
			{
				srb_apl_trace(LOG_ALW, 0xb7f9, "SRB hdr founded CRC:[0x%x]",srb->srb_hdr.srb_csum);

				retval = true;
				*srb_dtag = dtag;
				goto out;
			}
		}
	}

	retval = false;
	dtag_put(DTAG_T_SRAM, dtag);
out:

	return retval;
}

slow_code bool srb_read_srb_to_memory(rda_t srb_rda, char *des, u8 count)
{
	u32 i, j;
	bool retval = true;
	dtag_t srb_dtag;
	srb_dtag = mem2dtag(des);

	bm_pl_t pl[SRB_MR_DU_CNT_PAGE];
	rda_t rdas[SRB_MR_DU_CNT_PAGE];

	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
		pl[i].all = 0;
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}
	/* Scan SRB accordingly */
	for (i = 0; i < count; i += SRB_MR_DU_CNT_PAGE) {
		srb_apl_trace(LOG_ALW, 0x1893, "SRB read Row(0x%x)@CH/CE(%d/%d)", srb_rda.row, srb_rda.ch, srb_rda.dev);
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			rdas[j] = srb_rda;
			rdas[j].du_off = j;
			if((i + j) < count)
				pl[j].pl.dtag = srb_dtag.b.dtag;
			else
				pl[j].pl.dtag = WVTAG_ID;
			srb_dtag.b.dtag += 1;
		}
		if (ncl_access_mr(rdas, NCL_CMD_OP_READ, pl, SRB_MR_DU_CNT_PAGE) != 0) {
			retval = false;
			break;
		}
		srb_rda.row += 1;
	}

	return retval;
}

/*slow_code void srb_program_form_memory(rda_t sb_rda, char *sou, u8 duCnt)
{
	rda_t rda;
	char *pSou;
	int i = 0;
	int j = 0;

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	sys_assert(dtag_get_bulk(SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);

	bm_pl_t pl[SRB_MR_DU_CNT_PAGE] = {
		{.pl.du_ofst = 0, .pl.btag = rawdisk_ctag},
		{.pl.du_ofst = 0, .pl.btag = rawdisk_ctag},
		{.pl.du_ofst = 0, .pl.btag = rawdisk_ctag},
		{.pl.du_ofst = 0, .pl.btag = rawdisk_ctag},
	};

	rda = sb_rda;
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_ALW, 0, "download Program Row(0x%x)@CH/CE(%d/%d)",
			rda.row, rda.ch, rda.dev);
	pSou = sou;
	for (i = 0; i < duCnt; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[j] = rda;
			tflush_rdas[j].du_off = j;

	             if(j < duCnt)
				pl[j].pl.dtag = dbase[j].b.dtag;
			else
				pl[j].pl.dtag = WVTAG_ID;
			memcpy(dtag2mem(dbase[j].dtag), pSou, SRB_MR_DU_SZE);
			pSou += SRB_MR_DU_SZE;
		}
	}
	ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);

	dtag_put_bulk(SRB_MR_DU_CNT_PAGE, dbase);
}*/

slow_code void srb_page_read(rda_t des_rda, rda_t sou_rda, u16 page_cnt)
{
	rda_t sou;
	rda_t des;
	int i = 0;
	int j = 0;

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_INFO, 0xb4b5, "SRB_MR_DU_CNT_PAGE3");
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);

	bm_pl_t pl[4] = {
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#if SRB_MR_DU_CNT_PAGE == 3
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#endif
	};

	sou = sou_rda;
	des = des_rda;
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_ALW, 0xd2a6, "copy source Row(0x%x)@CH/CE(%d/%d)",
				sou.row, sou.ch, sou.dev);
	srb_apl_trace(LOG_ALW, 0x1d99, "copy Program Row(0x%x)@CH/CE(%d/%d)pageCnt(0x%x)",
				des.row, des.ch, des.dev,page_cnt);
	for (i = 0; i < page_cnt; i++) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[0] = sou;
			tflush_rdas[0].du_off = j;
			pl[0].pl.dtag = dbase[j].dtag;
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, 1);
		}
			srb_apl_trace(LOG_ERR, 0x5c9e, "Check content srb_page_read %d \n", i);
			dtagprint(dbase[0], 512);

		sou.row ++;
		des.row ++;
	}

		dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
}

slow_code void srb_page_copy(rda_t des_rda, rda_t sou_rda, u16 page_cnt)
{
	rda_t sou;
	rda_t des;
	int i = 0;
	int j = 0;

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
// 	srb_apl_trace(LOG_INFO, 0x4f84, "SRB_MR_DU_CNT_PAGE4");
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);

	bm_pl_t pl[SRB_MR_DU_CNT_PAGE];
	sou = sou_rda;
	des = des_rda;
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_ALW, 0xf01b, "copy source Row(0x%x)@CH/CE(%d/%d)",
				sou.row, sou.ch, sou.dev);
	srb_apl_trace(LOG_ALW, 0xe936, "copy Program Row(0x%x)@CH/CE(%d/%d)pageCnt(0x%x)",
				des.row, des.ch, des.dev,page_cnt);
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
		pl[i].all = 0;
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}
	for (i = 0; i < page_cnt; i++) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if(plp_trigger)
			{
				srb_apl_trace(LOG_WARNING, 0x556f, "plp_trigger 1, break, i %u, j %u", i, j);
				dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
				return;
			}
			tflush_rdas[0] = sou;
			tflush_rdas[0].du_off = j;
			pl[0].pl.dtag = dbase[j].dtag;
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, 1);
		}
		//ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, SRB_MR_DU_CNT_PAGE);
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[j] = des;
			tflush_rdas[j].du_off = j;
			pl[j].pl.dtag = dbase[j].dtag;
		}
		if(plp_trigger)
		{
			srb_apl_trace(LOG_WARNING, 0x9760, "plp_trigger 1, break, i %u, j %u", i, j);
			dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
			return;
		}
		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		sou.row ++;
		des.row ++;
	}

	dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
}

extern int ncl_cmd_simple_submit(rda_t *rda_list, enum ncl_cmd_op_t op,
				    bm_pl_t *dtag, u32 count,
				    int du_format, int stripe_id);

#define SPB_CNT 2
slow_code bool srb_alloc_block_addr_assign(dft_btmp_t *dft,bool reset ,rda_t *rda, u8 _ch, u8 _dev, u8 _lun, u8 _pln)
{
	u16 spb_idx = 0;	//Always use spb0
	u16 pb_idx;
	u8 count;

	pb_idx = ((_ch << CH_SHF)|(_dev << CE_SHF)|(_lun << LUN_SHF)|(_pln << PLN_SHF));

	if (reset){
		dft[spb_idx].dft_bitmap[pb_idx >> 5] &= ~(1 << (pb_idx & 0x1F));
	}

	for(count = 0; count<SPB_CNT; count++){
		if (!(dft[spb_idx].dft_bitmap[pb_idx >> 5] & BIT(pb_idx & ((1 << 5) - 1)))) {
			dft[spb_idx].dft_bitmap[pb_idx >> 5] |= BIT(pb_idx & ((1 << 5) - 1));
			srb_apl_trace(LOG_ERR, 0x96ce, "[Eddie]srb_alloc_block_addr_assign pb_idx %x dft[%x].dft_bitmap[%d] = 0x%x \n",pb_idx,spb_idx,pb_idx >> 5,dft[spb_idx].dft_bitmap[pb_idx >> 5] );
			goto found;
		}
		else if (count == 0){
			pb_idx &= ~(_dev << CE_SHF);
			pb_idx |= ((_dev+2) << CE_SHF);	//if ce0/ce1 is bad,then try ce2,ce3
			srb_apl_trace(LOG_ALW, 0x424d, "Warning : SPB 0 should be good !!PLZ check\n");
		}
		else{
			srb_apl_trace(LOG_ALW, 0xf645, "No available Block !!PLZ check\n");
			sys_assert(0);
		}
	}

	return false;

found:
	_pln = (pb_idx >> PLN_SHF) & (nal_plane_count_per_lun() - 1);
	_lun = (pb_idx >> LUN_SHF) & (nal_lun_count_per_dev() - 1);
	_dev = (pb_idx >> CE_SHF) & (nal_get_targets() - 1);
	_ch = (pb_idx >> CH_SHF) & (nal_get_channels() - 1);

	rda->du_off = 0;
	rda->pb_type = NAL_PB_TYPE_SLC;
	rda->row = (spb_idx << nand_row_blk_shift()) | (_lun << nand_row_lun_shift()) | (_pln << nand_row_plane_shift());
	rda->ch = _ch;
	rda->dev = _dev;

	return true;
}
#ifdef UPDATE_LOADER
extern rda_t sb_rda;
extern rda_t sb_rda_dual;
slow_code void srb_buf_modify_write(rebuild_srb_para_t srb_para ,u16 page_cnt, rda_t rda_sou)
{		//20200603-Eddie

#ifdef FIX_HEADER_PAGE
	srb_t *srb=(srb_t*)SRAM_BASE;
	if(srb_para.srb_hdr_pgs==0){
			srb_para.srb_hdr_pgs = srb->srb_hdr.srb_pub_key-1;
	}
#endif
	rda_t sou;
	rda_t des;
	rda_t fwb_buf_rda;
	rda_t srb_buf_rda;
	//srb_t *srb = (srb_t *)SRB_HD_ADDR;
	int i = 0;
	int j = 0;
	int pgs_cnt = 0;
	srb_t *srb_tp;
	row_t srb_sb_row =0, srb_sb_row_dual =0;
	u16 sb_start, sb_end,sb_image_dus;
#ifdef RESERVE_SB_IN_FIX_POS
	row_t srb_sb_row_mirror=0, srb_sb_row_dual_mirror=0;
#endif
	int k;
	//int slice = 0;

#ifdef RESERVE_SB_IN_FIX_POS
	bool ret = 1;
	u16 sb_res_pgs = 0;
	rda_t srb_hdr_rda, srb_hdr_rda_mirror;
	ret &= srb_alloc_block_addr_assign(NULL,true,&srb_hdr_rda,0,0,0,0);
	ret &= srb_alloc_block_addr_assign(NULL,true,&srb_hdr_rda_mirror,0,0,0,1);

	srb_sb_row_dual = srb_hdr_rda.row + slc_pgs_per_blk - RESERVE_SB_PGS;
	srb_sb_row = srb_sb_row_dual - RESERVE_SB_PGS;
	srb_sb_row_dual_mirror = srb_hdr_rda_mirror.row + slc_pgs_per_blk - RESERVE_SB_PGS;
	srb_sb_row_mirror = srb_sb_row_dual_mirror - RESERVE_SB_PGS;
#endif

	srb_apl_trace(LOG_ALW, 0x9470, "SRB MODIFY WRITE TO BUF");
	srb_buf_rda = srb_para.srb_buf_pos;
	sb_start = srb_para.sb_start;
	sb_end = srb_para.sb_end;
	sb_image_dus = srb_para.sb_image_dus;
#ifdef RESERVE_SB_IN_FIX_POS
	sb_res_pgs = RESERVE_SB_PGS - NR_PAGES_SLICE(sb_image_dus * SRB_MR_DU_SZE);
#endif
#if 0//def PRT_LOADER_LOG
for (a = 0; a < srb_para.image_dus; a++){
	srb_apl_trace(LOG_ERR, 0xa951, "[Eddie]Check srb_para.sb_dus[0] 4 \n");
	dtagprint(srb_para.sb_dus[a], 4096);
}
#endif

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
srb_apl_trace(LOG_INFO, 0x90c2, "SRB_MR_DU_CNT_PAGE5");
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);

	bm_pl_t tflush_pl[SRB_MR_DU_CNT_PAGE];
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
		tflush_pl[i].all = 0;
		tflush_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

#if 0//def PRT_LOADER_LOG
for (a = 0; a < srb_para.image_dus; a++){
	srb_apl_trace(LOG_ERR, 0xbf51, "[Eddie]Check srb_para.sb_dus[0] 5 \n");
	dtagprint(srb_para.sb_dus[a], 4096);
}
#endif

	bm_pl_t pl[4] = {
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#if SRB_MR_DU_CNT_PAGE == 3
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
		{.pl.du_ofst = 0, .pl.btag = 0, .pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP},
#endif
	};

	bm_pl_t pl_sb = {
		.pl.dtag = dbase[0].dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	sou = rda_sou;
	des = srb_buf_rda;
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_ALW, 0xdfb1, "copy source Row(0x%x)@CH/CE(%d/%d)",
				sou.row, sou.ch, sou.dev);
	srb_apl_trace(LOG_ALW, 0x8fb4, "copy Program Row(0x%x)@CH/CE(%d/%d)pageCnt(0x%x)",
				des.row, des.ch, des.dev,page_cnt);

#if 0//def PRT_LOADER_LOG
	srb_apl_trace(LOG_ERR, 0x0276, "[Eddie]Check srb_para.sb_dus[0] 5 \n");
	dtagprint(srb_para.sb_dus[0], 4096);
#endif

	//for (i = 0; i < page_cnt; i++)
	do{
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[0] = sou;
			tflush_rdas[0].du_off = j;
			pl[0].pl.dtag = dbase[j].dtag;
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, 1);
		}
		//Modify SRB	pgs_cnt : 0~172
		//if (sou.row == (srb_para.srb_1st_pos.row + pgs_cnt)){
		if (pgs_cnt <= srb_para.srb_hdr_pgs){
			srb_tp = (srb_t *) dtag2mem(dbase[0]);
		#if 1
			if (pgs_cnt == 0){
				srb_apl_trace(LOG_ERR, 0xccdb, "Replace CSNUM %x \n", srb_tp->sb_csum);
				srb_apl_trace(LOG_ERR, 0x0bd7, "(SB Ori.) srb_sb_row = 0x%x, srb_sb_row_dual = 0x%x  sb_image_dus = %d sb_res_pgs = %d\n",srb_tp->srb_hdr.srb_sb_row,srb_tp->srb_hdr.srb_sb_row_dual, sb_image_dus , sb_res_pgs);
				srb_apl_trace(LOG_ERR, 0x668c, "(SB Ori.) srb_sb_row (m) = 0x%x, srb_sb_row_dual (m) = 0x%x \n",srb_tp->srb_hdr.srb_sb_row_mirror,srb_tp->srb_hdr.srb_sb_row_dual_mirror);
				//dtagprint(dbase[0], 4096);
			}
		#endif
			srb_tp->sb_csum = srb_para.sb_csum;
			srb_tp->srb_hdr.srb_sb_du_cnt = sb_image_dus;
		#ifdef RESERVE_SB_IN_FIX_POS
			srb_tp->srb_hdr.srb_sb_row_dual = srb_sb_row_dual;
			srb_tp->srb_hdr.srb_sb_row = srb_sb_row;
			srb_tp->srb_hdr.srb_sb_row_dual_mirror = srb_sb_row_dual_mirror;
			srb_tp->srb_hdr.srb_sb_row_mirror = srb_sb_row_mirror;
		#endif
		#ifdef CLR_SCAN_DONE
			if ((srb_tp->srb_hdr.srb_sb_row != srb_sb_row) || (srb_tp->srb_hdr.srb_sb_row != srb_sb_row)
				||(srb_tp->srb_hdr.srb_sb_row_mirror !=srb_sb_row_mirror) ||(srb_tp->srb_hdr.srb_sb_row_dual_mirror !=srb_sb_row_dual_mirror)){
				srb_tp->ALL_defect_scan_Done = 0;
			}
		#endif
			srb_tp->srb_hdr.srb_csum = crc32(&srb_tp->srb_hdr, offsetof(srb_hdr_t, srb_csum));	//20200921-Eddie

		#ifdef RESERVE_SB_IN_FIX_POS
			if (pgs_cnt == 0){
				srb_apl_trace(LOG_ERR, 0xe41a, "(SB Modify) srb_sb_row = 0x%x, srb_sb_row_dual = 0x%x\n",srb_tp->srb_hdr.srb_sb_row,srb_tp->srb_hdr.srb_sb_row_dual);
				srb_apl_trace(LOG_ERR, 0xb4f6, "(SB Modify) srb_sb_row (m) = 0x%x, srb_sb_row_dual (m) = 0x%x \n",srb_tp->srb_hdr.srb_sb_row_mirror,srb_tp->srb_hdr.srb_sb_row_dual_mirror);
			#ifdef CLR_SCAN_DONE
				if (srb_tp->ALL_defect_scan_Done == 0)
					srb_apl_trace(LOG_ERR, 0x216f, "Clear ALL_defect_scan_Done \n");
			#endif
			}
		#endif


		}
		//Modify SB Loader
		if(sou.row == srb_sb_row || sou.row == srb_sb_row_dual || (sou.ch == 0 && sou.dev == 0 && (sou.row == srb_sb_row_mirror || sou.row == srb_sb_row_dual_mirror)))
		{
			fwb_buf_rda = srb_para.fwb_buf_pos;
		#if 0
			for (a = 0; a < sb_du_cnt; a++){
				memcpy(dtag2mem(dbase_sb[a]), dtag2mem(srb_para.sb_dus[a]), DTAG_SZE);
			}
		#endif
			srb_apl_trace(LOG_ERR, 0xc334, "(SB Modify) srb_sb_row = 0x%x, srb_sb_row_dual = 0x%x \n",srb_sb_row,srb_sb_row_dual);
		#if 1 //def PRT_LOADER_LOG
			loader_image_t *image_sb = (loader_image_t *) dtag2mem(dbase[0]);
			srb_apl_trace(LOG_ERR, 0x8284, "[Eddie] (SB Modify)image_sb signature = %x, section_csum = %x, image_dus = %d\n",image_sb->signature,image_sb->section_csum, image_sb->image_dus);
		#endif


	#if 1

			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
				tflush_rdas[j] = des;
				tflush_rdas[j].du_off = j;
				tflush_pl[j].pl.dtag = dbase[j].dtag;
			}

			k = 0;
			for (i = 0; i < (sb_image_dus+1); i += SRB_MR_DU_CNT_PAGE) {
				for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
					fwb_buf_rda.du_off = j;
					ncl_access_mr(&fwb_buf_rda, NCL_CMD_OP_READ, &pl_sb, 1);

					if (((i + j) >= sb_start) && ((i + j) <= sb_end)) {
						k++;
						if ((k == SRB_MR_DU_CNT_PAGE) || ((i + j) == sb_end)) {
							ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, tflush_pl, 1);
							//srb_apl_trace(LOG_ERR, 0, "check SB pos : des.row 0x%x \n", des.row);
							for (k = 0; k < SRB_MR_DU_CNT_PAGE; k++){
								tflush_rdas[k].row++;
							}
							//srb_apl_trace(LOG_ERR, 0, "check SB pos : sou.row 0x%x \n", sou.row);
							des.row ++;
							pgs_cnt ++;
							sou.row ++;
							k = 0;
						}
						pl_sb.pl.dtag = dbase[k].dtag;
					}
				}
				fwb_buf_rda.row += 1;
			}

		#ifdef RESERVE_SB_IN_FIX_POS		//20200921-Eddie
			for (i = 0; i < sb_res_pgs; i ++) {
				rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

				//srb_apl_trace(LOG_ERR, 0, "Program DUMMY in RES SB Row(0x%x)@CH/CE(%d/%d) \n", rda.row, rda.ch, rda.dev);
				for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
					tflush_rdas[j] = des;
					tflush_rdas[j].du_off = j;
					pl[j].pl.dtag = WVTAG_ID;
				}
				ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
				//srb_apl_trace(LOG_ERR, 0, "check Dummy pages pos : row 0x%x \n", des.row);
				des.row ++;
				pgs_cnt ++;
				sou.row ++;
				}
		#endif


	#else

			for (i = 0; i < sb_du_cnt; i += SRB_MR_DU_CNT_PAGE) {
			//ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, SRB_MR_DU_CNT_PAGE);
				for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
					tflush_rdas[j] = des;
					tflush_rdas[j].du_off = j;
					if (i + j >= sb_du_cnt)
						pl[j].pl.dtag = WVTAG_ID;
					else{
						pl[j].pl.dtag = dbase[j].dtag;
						//memcpy(dtag2mem(dbase[j]), dtag2mem(srb_para.sb_dus[slice+1]), DTAG_SZE);
						//dtagprint(srb_para.sb_dus[slice+1], 4096);
						//slice++;
					}
					ncl_access_mr(fwb_rda, NCL_CMD_OP_READ, pl, 1);  //Read loader image from FWB buffer
					ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);  //Write loader image to SRB buffer
				}

				srb_apl_trace(LOG_ERR, 0xbf42, "check SB pos : row 0x%x \n", sou.row);
				pgs_cnt ++;
				sou.row ++;
				des.row ++;
				fwb_rda.row++;
			}
	#endif
		}
		else{
			//ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, SRB_MR_DU_CNT_PAGE);
			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
				tflush_rdas[j] = des;
				tflush_rdas[j].du_off = j;
				pl[j].pl.dtag = dbase[j].dtag;
			}
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
			//srb_apl_trace(LOG_ERR, 0, "check SRB pages pos : row 0x%x \n", sou.row);
			pgs_cnt ++;
			sou.row ++;
			des.row ++;

		}
	}while(pgs_cnt < page_cnt);

	dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);

	ncl_cmd_simple_submit(&rda_sou, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	srb_page_copy(rda_sou,srb_para.srb_buf_pos, slc_pgs_per_blk);
}

slow_code void srb_rebuild_sb(rebuild_srb_para_t srb_para){
	srb_apl_trace(LOG_ALW, 0x49b5, "SRB REBUILD SB");

#if 0//def PRT_LOADER_LOG
for (int a = 0; a < srb_para.image_dus; a++){
	srb_apl_trace(LOG_ERR, 0xda90, "[Eddie]Check srb_para.sb_dus[0] 3 \n");
	dtagprint(srb_para.sb_dus[a], 4096);
}
#endif

	ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_1st_pos);
	ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_2nd_pos);
	//ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	//srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_3rd_pos);
	//ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	//srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_4th_pos);
	//ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	//srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_5th_pos);
	//ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	//srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_6th_pos);
	//ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	//srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_7th_pos);
	//ncl_cmd_simple_submit(&srb_para.srb_buf_pos, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	//srb_buf_modify_write(srb_para, slc_pgs_per_blk, srb_para.srb_8th_pos);
}
#endif
//#define CHK_FWB_REBUILD_LOG
#ifdef FW_IN_GROUP
slow_code void srb_fwb_grp_change(fwb_t *fwb, rebuild_fwb_para_t para)
{
	if ((fwb->fw_slot[para.slot].grp_idx != 1) && (fwb->fw_slot[para.slot].grp_idx != 2)) 	//Just for initialization
	{
		fwb->fw_slot[para.slot].grp_idx = 1;
	}
	else if (fwb->fw_slot[para.slot].grp_idx == 1)
    {
		fwb->fw_slot[para.slot].grp_idx = 2;
		fwb->fw_slot[para.slot].fw_slot.ch += 1;	//CH2
		fwb->fw_slot[para.slot].fw_slot_dual.ch += 1;
		fwb->fw_slot[para.slot].fw_slot_mirror.ch -= 1;	//CH1
		fwb->fw_slot[para.slot].fw_slot_dual_mirror.ch -= 1;	
	}
	else if (fwb->fw_slot[para.slot].grp_idx == 2)
    {
		fwb->fw_slot[para.slot].grp_idx = 1;
		fwb->fw_slot[para.slot].fw_slot.ch -= 1;	//CH1
		fwb->fw_slot[para.slot].fw_slot_dual.ch -= 1;
		fwb->fw_slot[para.slot].fw_slot_mirror.ch += 1;	 //CH2
		fwb->fw_slot[para.slot].fw_slot_dual_mirror.ch += 1;
	}
}
#endif

slow_code void srb_build_fw(rda_t rda, fwb_t *fwb, rda_t fw_bf_rda, u8 i)
{
    if (plp_trigger)
	{
		return;
	}

    srb_apl_trace(LOG_ALW, 0xa983, "Erase rda_t row 0x%x CH%d/CE%d", rda.row, rda.ch, rda.dev);
	ncl_cmd_simple_submit(&rda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	srb_page_copy(rda, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
	rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
	srb_page_copy(rda, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
}

#ifdef FWUPDATE_SPOR
slow_code void srb_build_fw_header(rda_t rda, rda_t rda_mirror, bm_pl_t *pl)
{
#ifdef EXTRA_FWBK
    rda_t rda_row_bak;
	rda_row_bak = rda.row;
	//rda_mirror_row_bak = rda_mirror.row;
	//Jerry k<2
	for (u8 k = 0; k < 1; k++)
    {
		rda.dev = (k == 0) ? rda.dev : (rda.dev+2);
		rda_mirror.dev = (k == 0) ? rda_mirror.dev : (rda_mirror.dev+2);
#endif

        int pgs, fwb_hdr_pgs = FWB_PGS;
    	/* I. update FWB header with SPOR */
    	//Jerry k<2 , no FWB mirror
    	for (u8 i = 0; i < 1; i++)
        {
    		if(plp_trigger)
    		{
    			return;
    		}
    		rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
    		u8 j;
    		rda_t trda;
    		trda = (i == 0) ? rda : rda_mirror;	//TODO: rda_mirror at CTQ
    		srb_apl_trace(LOG_ALW, 0x6f79, "Erase FWB trda_t row 0x%x CH%d/CE%d",trda.row, trda.ch, trda.dev);
    		ncl_cmd_simple_submit(&trda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
    		for (pgs = 0; pgs < fwb_hdr_pgs; pgs++)
            {
    			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++)
                {
    				tflush_rdas[j] = trda;
    				tflush_rdas[j].du_off = j;
    			}
    			ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
    			trda.row++;
    		}	
    	}
#ifdef EXTRA_FWBK
    	rda.row = rda_row_bak;
	    //rda_mirror.row = rda_mirror_row_bak;
	}
#endif
    srb_apl_trace(LOG_ERR, 0x2161, "[Rebuild FWB] FWB Program Done.\n");
}
#endif

slow_code void srb_rebuild_fwb(rebuild_fwb_para_t para)
{
	fwb_t *fwb;
	rda_t rda = para.fw_pri;
	rda_t rda_mirror = para.fw_sec;
	rda_t fw_bf_rda;
	dtag_t dtag = dtag_get_urgt(DTAG_T_SRAM, (void **)&fwb);
	//Jerry
	//fwb_t *fwb2;
	//fwb_t *fwb3;
	//dtag_t dtag2 = dtag_get_urgt(DTAG_T_SRAM, (void **)&fwb2);
	//dtag_t dtag3 = dtag_get_urgt(DTAG_T_SRAM, (void **)&fwb3);
#ifdef EXTRA_FWBK
	int k = 0;
	row_t rda_row_bak;
	row_t rda_mirror_row_bak;
#endif

#ifdef FWB_INDEPENDENT	//20200521-Eddie
	rda_t fw_slot_bk, fw_slot_mirror_bk; //, fw_slot_dual_bk, fw_slot_dual_mirror_bk;
#endif

	srb_apl_trace(LOG_INFO, 0xc59c, "srb_rebuild_fwb slot: %d\n",para.slot);

	sys_assert(dtag.dtag != _inv_dtag.dtag);
	bm_pl_t pl_read = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP | META_SRAM_DTAG
	};
	bm_pl_t pl[SRB_MR_DU_CNT_PAGE];
	bool fwb_pri_fail = false;
	u8 i;
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
		pl[i].all = 0;
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
		pl[i].pl.dtag = (i == 0 ? dtag.b.dtag : WVTAG_ID);// (i == 1) ? dtag2.b.dtag : dtag3.b.dtag);
	}

	//Jerry tmp: check fwb header backup
	if (ncl_access_mr(&rda_mirror, NCL_CMD_OP_READ, &pl_read, 1) == 0) {
		srb_apl_trace(LOG_INFO, 0xf93b, "tmp check fwb_sec ok, Row(0x%x)@CH/CE(%d/%d)", rda_mirror.row, rda_mirror.ch, rda_mirror.dev);
		if (fwb->signature != FRMB_SIGNATURE){
			srb_apl_trace(LOG_INFO, 0x51b5, "tmp check fwb_sec sig fail, sig 0x%x", fwb->signature);
		}
	}else{
		srb_apl_trace(LOG_INFO, 0x9c64, "tmp check fwb_sec fail, Row(0x%x)@CH/CE(%d/%d)", rda_mirror.row, rda_mirror.ch, rda_mirror.dev);
	}
	if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl_read, 1) != 0) {
		fwb_pri_fail = true;
		srb_apl_trace(LOG_ERR, 0x39e4, "fwb_pri_fail, Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
		if (ncl_access_mr(&rda_mirror, NCL_CMD_OP_READ, &pl_read, 1) != 0) {
			dtag_put(DTAG_T_SRAM, dtag);
			srb_apl_trace(LOG_ERR, 0x02f6, "fwb_sec_fail, Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
			return;
		}
	}

	if (fwb->signature != FRMB_SIGNATURE) {
		//dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0x5045, "firmware block signature:[0x%x] != [0x%x] fail",fwb->signature, FRMB_SIGNATURE);
		//return;
		if (ncl_access_mr(&rda_mirror, NCL_CMD_OP_READ, &pl_read, 1) != 0) {
			dtag_put(DTAG_T_SRAM, dtag);
			srb_apl_trace(LOG_ERR, 0x08aa, "fwb_sec_fail, Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
			return;
		}
	}
	if (fwb->signature != FRMB_SIGNATURE) {
		dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0x20ba, "sec firmware block signature:[0x%x] != [0x%x] fail",fwb->signature, FRMB_SIGNATURE);
		return;
	}
	if (fwb_pri_fail == true){
		//SPOR case: if FWB header broken, use the information that record in loader from FWB header mirror
		fwb_t *fwb_in_sram = (fwb_t *) (SRAM_BASE + sizeof(srb_t) - 512);
		fwb->fw_slot[fwb->active_slot].grp_idx = fwb_in_sram->fw_slot[fwb->active_slot].grp_idx;
		fwb->fw_slot[fwb->active_slot].fw_slot.ch = fwb_in_sram->fw_slot[fwb->active_slot].fw_slot.ch;
		fwb->fw_slot[fwb->active_slot].fw_slot_dual.ch = fwb_in_sram->fw_slot[fwb->active_slot].fw_slot_dual.ch ;
		fwb->fw_slot[fwb->active_slot].fw_slot_mirror.ch = fwb_in_sram->fw_slot[fwb->active_slot].fw_slot_mirror.ch; 
		fwb->fw_slot[fwb->active_slot].fw_slot_dual_mirror.ch = fwb_in_sram->fw_slot[fwb->active_slot].fw_slot_dual_mirror.ch;
	}
		u8 pln, lun;
		u16 blk;

		row2nda(fwb->fw_slot[fwb->active_slot].fw_slot.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0xde3c, "[Eddie] FW SLOT %d  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d) \n", fwb->active_slot, blk, pln, lun, fwb->fw_slot[fwb->active_slot].fw_slot.ch, fwb->fw_slot[fwb->active_slot].fw_slot.dev);
		row2nda(fwb->fw_slot[fwb->active_slot].fw_slot_dual.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0xe0f7, "[Eddie] FW SLOT %d dual  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d) \n", fwb->active_slot, blk, pln, lun, fwb->fw_slot[fwb->active_slot].fw_slot_dual.ch, fwb->fw_slot[fwb->active_slot].fw_slot_dual.dev);
		row2nda(fwb->fw_slot[fwb->active_slot].fw_slot_mirror.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0x85d8, "[Eddie] FW SLOT %d (m)  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d) \n", fwb->active_slot, blk, pln, lun, fwb->fw_slot[fwb->active_slot].fw_slot_mirror.ch, fwb->fw_slot[fwb->active_slot].fw_slot_mirror.dev);
		row2nda(fwb->fw_slot[fwb->active_slot].fw_slot_dual_mirror.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0x7322, "[Eddie] FW SLOT %d (m) dual B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d) \n", fwb->active_slot, blk, pln, lun, fwb->fw_slot[fwb->active_slot].fw_slot_dual_mirror.ch, fwb->fw_slot[fwb->active_slot].fw_slot_dual_mirror.dev);

	if (para.active_slot != 0) {
		if (((para.active_slot - 1) != fwb->active_slot) &&
			((para.active_slot == para.slot) ||
			(fwb->fw_slot[para.active_slot - 1].fw_slot_du_cnt != 0))) {
			fwb->active_slot = para.active_slot - 1;
			srb_apl_trace(LOG_INFO, 0x3bbb, "fwb->active_slot: %d\n",fwb->active_slot);
		} else if (para.slot == 0) {		//ca=2, Only change active slot, no update FW image
			dtag_put(DTAG_T_SRAM, dtag);
			srb_apl_trace(LOG_ERR, 0xe4e7, "firmware commit invaild download slot:%d, active slot:%d",para.slot, para.active_slot);
			return;
		}
	}

	if (para.slot != 0) {
		para.slot--;

	#ifdef FWB_INDEPENDENT
		fw_slot_bk = fwb->fw_slot[para.slot].fw_slot;
		fw_slot_mirror_bk = fwb->fw_slot[para.slot].fw_slot_mirror;
		//fw_slot_dual_bk = fwb->fw_slot[para.slot].fw_slot_dual;
		//fw_slot_dual_mirror_bk = fwb->fw_slot[para.slot].fw_slot_dual_mirror;

	#ifdef FW_IN_GROUP
		row2nda(fwb->fw_slot[para.slot].fw_slot.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0x827b,"FW img INFO : GRP:%d , para.slot = %d , B(%d) row(%x) @CH/CE(%d/%d)\n",fwb->fw_slot[para.slot].grp_idx, para.slot, blk, fwb->fw_slot[para.slot].fw_slot.row, fwb->fw_slot[para.slot].fw_slot.ch, fwb->fw_slot[para.slot].fw_slot.dev);
		row2nda(fwb->fw_slot[para.slot].fw_slot_mirror.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0x3023,"FW img Mirror INFO : GRP:%d , para.slot = %d , B(%d) row(%x) @CH/CE(%d/%d)\n",fwb->fw_slot[para.slot].grp_idx, para.slot, blk, fwb->fw_slot[para.slot].fw_slot_mirror.row, fwb->fw_slot[para.slot].fw_slot_mirror.ch, fwb->fw_slot[para.slot].fw_slot_mirror.dev);
	#else
		row2nda(fwb->fw_slot[para.slot].fw_slot.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0x624f,"FW img INFO :para.slot = %d , B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)\n", para.slot, blk, pln, lun, fwb->fw_slot[para.slot].fw_slot.ch, fwb->fw_slot[para.slot].fw_slot.dev);
		row2nda(fwb->fw_slot[para.slot].fw_slot_mirror.row, &pln, &blk, &lun);
		srb_apl_trace(LOG_INFO, 0x81b6, "FW img Mirror INFO :para.slot = %d , B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)\n", para.slot, blk, pln, lun, fwb->fw_slot[para.slot].fw_slot_mirror.ch, fwb->fw_slot[para.slot].fw_slot_mirror.dev);
	#endif
	#endif
		fwb->fw_slot[para.slot].fw_slot_version = para.fw_version;
		fwb->fw_slot[para.slot].fw_slot_du_cnt = para.mfw_dus;
		fwb->fw_slot[para.slot].fw_slot = para.buf_rda;
		fwb->fw_slot[para.slot].fw_slot.row += NR_PAGES_SLICE(para.image_dus * SRB_MR_DU_SZE);
		fwb->fw_slot[para.slot].fw_slot_mirror = para.buf_rda;
		fwb->fw_slot[para.slot].fw_slot_dual = para.buf_rda;
		fwb->fw_slot[para.slot].fw_slot_dual_mirror = para.buf_rda;
	}

#ifdef FWCA_2
if(para.ca != 2){
#endif
	rda.row++;  //for !FWB_INDEPENDENT case, put FWB header in page0, FW image start from page 1
	rda_mirror.row++; //for !FWB_INDEPENDENT case, put FWB header in page0, FW image start from page 1
	fw_bf_rda = para.buf_rda;
	fw_bf_rda.row += NR_PAGES_SLICE(para.image_dus * SRB_MR_DU_SZE);
	fw_bf_rda.row += NR_PAGES_SLICE(para.mfw_dus * SRB_MR_DU_SZE);
	for (i = 0; i < MAX_FWB_SLOT; i ++) {

	if(plp_trigger)
	{
		return;
	}

		#ifdef MULTI_SLOT
			if (i == para.slot)
		#else
			if (fwb->fw_slot[i].fw_slot_du_cnt != 0)
		#endif
			{
			srb_apl_trace(LOG_INFO, 0x8390,"Program to FWB buf");	//This is a strange operation, I think there is no need to copy from buffer to buffer.
			if (fwb_pri_fail == false)
				srb_page_copy(fw_bf_rda, fwb->fw_slot[i].fw_slot, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
			else
				srb_page_copy(fw_bf_rda, fwb->fw_slot[i].fw_slot_mirror, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));

			fw_bf_rda.row += NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE);

			#ifdef FWB_INDEPENDENT 	//20200521-Eddie
				fwb->fw_slot[i].fw_slot = fw_slot_bk;
				fwb->fw_slot[i].fw_slot_mirror = fw_slot_mirror_bk;
				//fwb->fw_slot[i].fw_slot_dual = fw_slot_dual_bk;
				//fwb->fw_slot[i].fw_slot_dual_mirror = fw_slot_dual_mirror_bk;
				fwb->fw_slot[i].fw_slot_dual = fwb->fw_slot[i].fw_slot;
				fwb->fw_slot[i].fw_slot_dual.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				fwb->fw_slot[i].fw_slot_dual_mirror = fwb->fw_slot[i].fw_slot_mirror;
				fwb->fw_slot[i].fw_slot_dual_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
			#else
			/* position */
			fwb->fw_slot[i].fw_slot = rda;
			fwb->fw_slot[i].fw_slot_mirror = rda_mirror;

			/* Dual position */
			rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
			rda_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
			fwb->fw_slot[i].fw_slot_dual = rda;
			fwb->fw_slot[i].fw_slot_dual_mirror = rda_mirror;
			rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
			rda_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
			#endif	
		}
	}
#ifdef FWCA_2
}
#endif

#ifdef FWCA_2
if(para.ca != 2){
#endif
	fw_bf_rda = para.buf_rda;
	fw_bf_rda.row += NR_PAGES_SLICE(para.image_dus * SRB_MR_DU_SZE);

	row2nda(fwb->fw_slot[para.slot].fw_slot.row, &pln, &blk, &lun);
#ifdef CHK_FWB_REBUILD_LOG
	srb_apl_trace(LOG_ERR, 0xc3f2, "[Eddie] Check 2 para.slot = %d ,B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)\n", para.slot, blk, pln, lun, fwb->fw_slot[para.slot].fw_slot.ch, fwb->fw_slot[para.slot].fw_slot.dev);
#endif
#ifdef FWB_INDEPENDENT 	//20200521-Eddie
	rda = fwb->fw_slot[para.slot].fw_slot;
	rda_mirror = fwb->fw_slot[para.slot].fw_slot_mirror;
#else
	rda.row++;
	rda_mirror.row++;
#endif
	row2nda(rda.row, &pln, &blk, &lun);
#ifdef CHK_FWB_REBUILD_LOG
	srb_apl_trace(LOG_ERR, 0xeae5, "[Eddie] Check 3 para.slot = %d ,B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)\n", para.slot, blk, pln, lun, rda.ch, rda.dev);
#endif
#ifdef FWB_INDEPENDENT 	//20200521-Eddie
	//Erase for FW Image Block slot[i]
	#if !defined (FWUPDATE_SPOR)	//20200521-Eddie	
		ncl_cmd_simple_submit(&rda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
		ncl_cmd_simple_submit(&rda_mirror, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	#endif
#ifdef CHK_FWB_REBUILD_LOG
	srb_apl_trace(LOG_ERR, 0x6205, "[Eddie] update FWB IMG rda_t row 0x%x CH%d/CE%d  \n",rda.row, rda.ch, rda.dev);
	srb_apl_trace(LOG_ERR, 0xb0d6, "[Eddie] update FWB IMG rda_t_mirror row 0x%x CH%d/CE%d  \n",rda_mirror.row, rda_mirror.ch, rda_mirror.dev);
#endif
#endif
	/* I. update FW Image Block slot[i] */
	srb_apl_trace(LOG_ERR, 0x688d, "[Eddie] fw_bf_rda.row = 0x%x \n",fw_bf_rda.row);
	for (i = 0; i < MAX_FWB_SLOT; i ++) {
		if(plp_trigger)
		{
			return;
		}
	#ifdef CHK_FWB_REBUILD_LOG
		srb_apl_trace(LOG_ERR, 0xac75, "[Eddie] FW slot[%d] row 0x%x CH%d/CE%d  \n",i,fwb->fw_slot[i].fw_slot.row, fwb->fw_slot[i].fw_slot.ch, fwb->fw_slot[i].fw_slot.dev);
		srb_apl_trace(LOG_ERR, 0x9b13, "[Eddie] FW slot[%d]_mirror row 0x%x CH%d/CE%d  \n",i,fwb->fw_slot[i].fw_slot_mirror.row, fwb->fw_slot[i].fw_slot_mirror.ch, fwb->fw_slot[i].fw_slot_mirror.dev);
	#endif
		#ifdef MULTI_SLOT
			if (i == para.slot) {
			#ifdef FWUPDATE_SPOR
				srb_apl_trace(LOG_ALW, 0x37f5, "firmware slot%d rebuild start",i);
			  #ifdef EXTRA_FWBK
				rda_row_bak = rda.row;
				rda_mirror_row_bak = rda_mirror.row;
				//Jerry k<2
				for (k = 0; k < 1; k++){
					rda.dev = (k == 0) ? rda.dev : (rda.dev+2);
					rda_mirror.dev = (k == 0) ? rda_mirror.dev : (rda_mirror.dev+2);
			  #endif

                    for (u8 j = 0 ; j < 2 ; j++)
                    {
#ifdef FW_IN_GROUP
                        srb_fwb_grp_change(fwb, para);
#endif

                        srb_build_fw(fwb->fw_slot[para.slot].fw_slot, fwb, fw_bf_rda, i);
                        srb_build_fw_header(para.fw_pri, para.fw_sec, pl);
                    }
			  #ifdef EXTRA_FWBK
				rda.row = rda_row_bak;
				rda_mirror.row = rda_mirror_row_bak;
				}
			  #endif
			#else	//<<else of #ifdef FWUPDATE_SPOR
				srb_apl_trace(LOG_ALW, 0x5ed4, "firmware slot%d rebuild start",i + 1);
				srb_page_copy(rda, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				srb_page_copy(rda_mirror, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				rda_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				srb_page_copy(rda, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				srb_page_copy(rda_mirror, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				rda_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				fw_bf_rda.row += NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE);
			#endif	
			}
		#else	//<< else of #ifdef MULTI_SLOT
			if (fwb->fw_slot[i].fw_slot_du_cnt != 0) {
				srb_apl_trace(LOG_ALW, 0xd583, "firmware slot%d rebuild start",i + 1);
				srb_page_copy(rda, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				srb_page_copy(rda_mirror, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				rda_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				srb_page_copy(rda, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				srb_page_copy(rda_mirror, fw_bf_rda, NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE));
				rda.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				rda_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[i].fw_slot_du_cnt) * SRB_MR_DU_SZE);
				fw_bf_rda.row += NR_PAGES_SLICE(fwb->fw_slot[i].fw_slot_du_cnt * SRB_MR_DU_SZE);
			}
		#endif
	}
	srb_apl_trace(LOG_ERR, 0xc654, "[Rebuild FWB] FW image Program Done.\n");
#ifdef FWCA_2
	}
#endif
// 20210129-Eddie-move update FWB here

	if(plp_trigger)
	{
		return;
	}

	rda = para.fw_pri;
	rda_mirror = para.fw_sec;
#if !defined (FWUPDATE_SPOR)	//20200521-Eddie
	//Erase for FWB Block
	ncl_cmd_simple_submit(&rda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
	ncl_cmd_simple_submit(&rda_mirror, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
#endif
	srb_apl_trace(LOG_ERR, 0xed94, "[Eddie] para.fw_pri.row = 0x%x, para.fw_sec.row = 0x%x, para.buf_rda.row = 0x%x \n",para.fw_pri.row, para.fw_sec.row, para.buf_rda.row);
#ifdef CHK_FWB_REBUILD_LOG
	srb_apl_trace(LOG_ERR, 0xcd7d, "[Eddie] update FWB header rda_t row 0x%x CH%d/CE%d  \n",rda.row, rda.ch, rda.dev);
	srb_apl_trace(LOG_ERR, 0x5f46, "[Eddie] update FWB header rda_t_mirror row 0x%x CH%d/CE%d  \n",rda_mirror.row, rda_mirror.ch, rda_mirror.dev);
#endif
#ifdef FWB_INDEPENDENT	//20200511-Eddie-4	
	#ifdef FWUPDATE_SPOR	//20200521-Eddie
/*
		#ifdef EXTRA_FWBK
			rda_row_bak = rda.row;
			//rda_mirror_row_bak = rda_mirror.row;
			//Jerry k<2
			for (k = 0; k < 1; k++){
				rda.dev = (k == 0) ? rda.dev : (rda.dev+2);
				rda_mirror.dev = (k == 0) ? rda_mirror.dev : (rda_mirror.dev+2);
		#endif

		int pgs, fwb_hdr_pgs = FWB_PGS;
			// I. update FWB header with SPOR
			//Jerry k<2 , no FWB mirror
			for (i = 0; i < 1; i++) {
				if(plp_trigger)
				{
					return;
				}
				rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
				u8 j;
				rda_t trda;
				trda = (i == 0) ? rda : rda_mirror;
				srb_apl_trace(LOG_ALW, 0x6f79, "Erase FWB trda_t row 0x%x CH%d/CE%d",trda.row, trda.ch, trda.dev);
				ncl_cmd_simple_submit(&trda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);
				for (pgs = 0; pgs < fwb_hdr_pgs; pgs++) {
					for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
						tflush_rdas[j] = trda;
						tflush_rdas[j].du_off = j;
					}
					ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
					trda.row++;
				}	
			}
		#ifdef EXTRA_FWBK
			rda.row = rda_row_bak;
			//rda_mirror.row = rda_mirror_row_bak;
			}
		#endif
	#else
		int pgs, fwb_hdr_pgs = FWB_PGS;
		for (pgs = 0; pgs < fwb_hdr_pgs; pgs++) {
	// I. update FWB header
	for (i = 0; i < 2; i++) {
		if(plp_trigger)
		{
			return;
		}
		rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
		u8 j;
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[j] = (i == 0) ? rda : rda_mirror;
			tflush_rdas[j].du_off = j;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
	}
			rda.row++;
			rda_mirror.row++;
		}
*/
	#endif
	srb_apl_trace(LOG_ERR, 0xa8b2, "[Rebuild FWB] Nothing here.\n");

#else	//<< else of #ifdef FWB_INDEPENDENT
	/* I. update FWB header */
	for (i = 0; i < 2; i++) {
		rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
		u8 j;
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[j] = (i == 0) ? rda : rda_mirror;
			tflush_rdas[j].du_off = j;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
	}
#endif

	dtag_put(DTAG_T_SRAM, dtag);
	srb_apl_trace(LOG_ALW, 0xecfd, "Rebuild fwb All Done.");
}

slow_code bool srb_fwb_verify_after_upgrade(rda_t fwb_pri_rda, rda_t fwb_sec_rda)
{
	fwb_t *fwb;
	dtag_t fwb_dtag = dtag_get_urgt(DTAG_T_SRAM, (void **)&fwb);
	bm_pl_t pl = {
		.pl.dtag = fwb_dtag.b.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};
	u32 i;
	bool fw_verify = false;
#ifdef EXTRA_FWBK
	int k = 0;
	row_t rda_row_bak;
	row_t rda_mirror_row_bak;
#endif

	if (ncl_access_mr(&fwb_pri_rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
		srb_apl_trace(LOG_ERR, 0xe52c, "fwb_pri_rda Row(0x%x)@CH/CE(%d/%d) fail", fwb_pri_rda.row, fwb_pri_rda.ch, fwb_pri_rda.dev);
		if (ncl_access_mr(&fwb_sec_rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
		dtag_put(DTAG_T_SRAM, fwb_dtag);
			srb_apl_trace(LOG_ERR, 0x6ce0, "fwb_sec_rda Row(0x%x)@CH/CE(%d/%d) fail", fwb_sec_rda.row, fwb_sec_rda.ch, fwb_sec_rda.dev);
		return false;
		}
	}

	for (i = 0; i < MAX_FWB_SLOT; i ++) {

		if(plp_trigger)
		{
			return true;
		}
		if (fwb->fw_slot[i].fw_slot_du_cnt != 0) {
		#ifdef EXTRA_FWBK
			rda_row_bak = fwb->fw_slot[i].fw_slot.row;
			rda_mirror_row_bak = fwb->fw_slot[i].fw_slot_mirror.row;
			//Jerry k<2
			for (k = 0; k < 1; k++){
				if(plp_trigger)
				{
					return true;
				}
				fwb->fw_slot[i].fw_slot_dual.dev = (k == 0) ? fwb->fw_slot[i].fw_slot.dev : (fwb->fw_slot[i].fw_slot.dev+2);
				fwb->fw_slot[i].fw_slot.dev = (k == 0) ? fwb->fw_slot[i].fw_slot.dev : (fwb->fw_slot[i].fw_slot.dev+2);
				fwb->fw_slot[i].fw_slot_dual_mirror.dev = (k == 0) ? fwb->fw_slot[i].fw_slot_mirror.dev : (fwb->fw_slot[i].fw_slot_mirror.dev+2);
				fwb->fw_slot[i].fw_slot_mirror.dev = (k == 0) ? fwb->fw_slot[i].fw_slot_mirror.dev : (fwb->fw_slot[i].fw_slot_mirror.dev+2);
		#endif
				fw_verify = srb_loader_verify(fwb->fw_slot[i].fw_slot);
		  #if (OTF_TIME_REDUCE == DISABLE)
			fw_verify |= srb_loader_verify(fwb->fw_slot[i].fw_slot_dual);
		  #endif
			fw_verify |= srb_loader_verify(fwb->fw_slot[i].fw_slot_mirror);
		  #if (OTF_TIME_REDUCE == DISABLE)
			fw_verify |= srb_loader_verify(fwb->fw_slot[i].fw_slot_dual_mirror);
		  #endif
			if(fw_verify == false){
				dtag_put(DTAG_T_SRAM, fwb_dtag);
				srb_apl_trace(LOG_ALW, 0xd3d0, "verify fw image slot%d error after fw commit\n", (i + 1));
				return false;
			}
/*
			if (false == srb_loader_verify(fwb->fw_slot[i].fw_slot)) {
				dtag_put(DTAG_T_SRAM, fwb_dtag);
				srb_apl_trace(LOG_ALW, 0xd3d0, "verify fw image slot%d error after fw commit\n", (i + 1));
				return false;
			}

		#if (OTF_TIME_REDUCE == DISABLE)
			if (false == srb_loader_verify(fwb->fw_slot[i].fw_slot_dual)) {
				dtag_put(DTAG_T_SRAM, fwb_dtag);
				srb_apl_trace(LOG_ALW, 0xf303, "verify fw image slot%d dual error after fw commit\n", (i + 1));
				return false;
			}
		#endif
			if (false == srb_loader_verify(fwb->fw_slot[i].fw_slot_mirror)) {
				dtag_put(DTAG_T_SRAM, fwb_dtag);
				srb_apl_trace(LOG_ALW, 0x7eec, "verify fw image slot%d mirror error after fw commit\n", (i + 1));
				return false;
			}
		#if (OTF_TIME_REDUCE == DISABLE)
			if (false == srb_loader_verify(fwb->fw_slot[i].fw_slot_dual_mirror)) {
				dtag_put(DTAG_T_SRAM, fwb_dtag);
				srb_apl_trace(LOG_ALW, 0xfbef, "verify fw image slot%d dual mirror error after fw commit\n", (i + 1));
				return false;
			}
		#endif
*/
		#ifdef EXTRA_FWBK
			fwb->fw_slot[i].fw_slot.row = rda_row_bak;
			fwb->fw_slot[i].fw_slot_mirror.row = rda_mirror_row_bak;
			}
		#endif
		}
	}

	dtag_put(DTAG_T_SRAM, fwb_dtag);
	return true;
}

slow_code void fw_build_buffer_block(rda_t bf_rda, dtag_t *sb_dus,u8 pg_offset, u8 du_cnt)
{
	rda_t rda;
	int j = 0;

	bm_pl_t pl[SRB_MR_DU_CNT_PAGE];
	rda = bf_rda;
	rda.row += pg_offset;
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	srb_apl_trace(LOG_ALW, 0x00a9, "download Program Row(0x%x)@CH/CE(%d/%d) dus:%d",
			rda.row, rda.ch, rda.dev, du_cnt);
	for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++)
    {
		tflush_rdas[j] = rda;
		tflush_rdas[j].du_off = j;
		pl[j].all = 0;
		pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;

		if(j < du_cnt)
			pl[j].pl.dtag = sb_dus[j].dtag;
		else
			pl[j].pl.dtag = WVTAG_ID;
	}
	if (0 != ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1))
		srb_apl_trace(LOG_ALW, 0x4526, "download Program Row(0x%x)@CH/CE(%d/%d) err",
			rda.row, rda.ch, rda.dev);
}

slow_code bool fw_verify_image_crc(dtag_t last_du, u32 *pre_crc, u64 *fw_version)
{
	int i;
	u32 fw_crc;
	u32 *val = (u32*) dtag2mem(last_du);
	i = (DTAG_SZE/sizeof(u32)) - 4;
	val += i;
	for (; i >= 0; i--, val--) {
		if ((*val == 0x1DBE5236) &&
			(*(val + 1) == 0x20161201) &&
			(*(val + 2) == 0)) {
			fw_crc = *(val + 3);
			int crc = *pre_crc;

			crc = crc32_cont(dtag2mem(last_du), i * sizeof(u32), crc, true);
			if (fw_crc != crc) {
				srb_apl_trace(LOG_ALW, 0xbe0e, "CRC(0x%x) vs. Real(0x%x), please retry!", fw_crc, crc);
				return false;
			}
			*pre_crc = fw_crc;
			*fw_version = *(val + 5);
			*fw_version = ((*fw_version) << 32) | *(val + 4);
			srb_apl_trace(LOG_ALW, 0xae9e, "firmware CRC verfied.");
			return true;
		}
	}

	srb_apl_trace(LOG_ALW, 0x302a, "this firmware image not have CRC verify info.");
	return true;
}

slow_code bool srb_loader_verify(rda_t image_rda)
{
	loader_image_t *image;
	rda_t rda;
	u32 fw_crc = ~0U;
	dtag_t dtag;
	u16 image_dus;
	u16 i, j;
	u8 image_du_off;

	rda = image_rda;
	dtag = dtag_get(DTAG_T_SRAM, (void **)&image);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	memset((void *)image, 0, DTAG_SZE);
	srb_apl_trace(LOG_ALW, 0xd33a, "verify image Row(0x%x)@CH/CE(%d/%d)",
			rda.row, rda.ch, rda.dev);

	if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
		dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0x1150, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
		return false;
	}

	if ((image->signature != IMAGE_SIGNATURE) ||
		(crc32(image->sections, sizeof(loader_section_t) * image->section_num) != image->section_csum)) {
		srb_apl_trace(LOG_ERR, 0xa522, "Image sign:[0x%x != 0x%x] CRC:[0x%x != 0x%x] fail", image->signature, IMAGE_SIGNATURE,
			crc32(image->sections, sizeof(loader_section_t) * image->section_num), image->section_csum);
		dtag_put(DTAG_T_SRAM, dtag);
		return false;
	}
	srb_apl_trace(LOG_INFO, 0x668a, "Row[%x] 0x%x - 0x%x dus:0x%x",rda.row , image->signature, image->section_csum, image->image_dus);

	image_dus = image->image_dus;

	image_du_off = rda.du_off;
	j = image_du_off;
	for (i = 0; i < (image_dus + image_du_off); i += SRB_MR_DU_CNT_PAGE) {
		for (; j < SRB_MR_DU_CNT_PAGE; j++) {

			if(plp_trigger)
			{
				return true;
			}
			if ((i + j - image_du_off) < (image_dus -1)) {
				rda.du_off = j;
				ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1);
				fw_crc = crc32_cont((void *)image, SRB_MR_DU_SZE, fw_crc, false);
			} else if((i + j - image_du_off) == (image_dus -1)) {
				rda.du_off = j;
				ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1);
			}
		}
		rda.row += 1;
		j = 0;
	}

	u64 fw_version;
	if (!fw_verify_image_crc(dtag, &fw_crc, &fw_version)) {
		dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0x951c, "fw image CRC error");
		return false;
	}
	dtag_put(DTAG_T_SRAM, dtag);

	return true;
}

slow_code bool srb_combo_verify_and_restore(rda_t image_rda)
{
	loader_image_t *image;
	rda_t rda;
	u32 fw_crc = ~0U;
	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	u16 image_dus;
	u16 fw_start, fw_end;
	u16 i, j, k;
	srb_apl_trace(LOG_INFO, 0x0481, "SRB_MR_DU_CNT_PAGE6");
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase) == SRB_MR_DU_CNT_PAGE);
	rda = image_rda;
	image = dtag2mem(dbase[0]);

	bm_pl_t pl = {
		.pl.dtag = dbase[0].dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	bm_pl_t tflush_pl[SRB_MR_DU_CNT_PAGE];
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
		tflush_pl[i].all = 0;
		tflush_pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

	memset((void *)image, 0, DTAG_SZE);
	srb_apl_trace(LOG_ALW, 0x78b1, "verify image Row(0x%x)@CH/CE(%d/%d)",
			rda.row, rda.ch, rda.dev);

	if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
		dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
		srb_apl_trace(LOG_ERR, 0x302c, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
		return false;
	}

	if ((image->signature != IMAGE_COMBO) && (image->signature != IMAGE_CMFG)) {
		srb_apl_trace(LOG_ERR, 0x1fb6, "Image sign:[0x%x != 0x%x] fail", image->signature, IMAGE_COMBO);
		dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
		return false;
	}
	srb_apl_trace(LOG_INFO, 0x95aa, "Row[%x] 0x%x - 0x%x dus:0x%x",rda.row , image->signature, image->section_csum, image->image_dus);

	image_dus = image->image_dus;
	fw_start = image->fw_slice[1].slice_start;
	fw_end = image->fw_slice[1].slice_end;
	rda.row += NR_PAGES_SLICE(image_dus * SRB_MR_DU_SZE);

	for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
		tflush_rdas[j] = rda;
		tflush_rdas[j].du_off = j;
		tflush_pl[j].pl.dtag = dbase[j].dtag;
	}

	rda = image_rda;
	k = 0;
	for (i = 0; i < image_dus; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if ((i + j) < (image_dus -1)) {
				rda.du_off = j;
				ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1);
				fw_crc = crc32_cont((void *)image, SRB_MR_DU_SZE, fw_crc, false);
			} else if((i + j) == (image_dus -1)) {
				rda.du_off = j;
				ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1);
			}

			if (((i + j) >= fw_start) && ((i + j) <= fw_end)) {
				k++;
				if ((k == SRB_MR_DU_CNT_PAGE) || ((i + j) == fw_end)) {
					ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, tflush_pl, 1);
					for (k = 0; k < SRB_MR_DU_CNT_PAGE; k++)
						tflush_rdas[k].row++;
					k = 0;
				}
				pl.pl.dtag = dbase[k].dtag;
				image = dtag2mem(dbase[k]);
			}
		}
		rda.row += 1;
	}

	u64 fw_version;
	if (!fw_verify_image_crc(dbase[k], &fw_crc, &fw_version)) {
		dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);
		srb_apl_trace(LOG_ERR, 0xc741, "fw image CRC error");
		return 0;
	}
	dtag_put_bulk(DTAG_T_SRAM, SRB_MR_DU_CNT_PAGE, dbase);

	return true;
}

slow_code void srb_read_verify_(rda_t rda, u32 du_cnt, u32 mem_len)
{
	int i, j;
	u32 *mem;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};
	srb_apl_trace(LOG_ERR, 0x4193, "verify rda row %x, CH/CE(%d/%d).\n", rda.row, rda.ch, rda.dev);
	for (i = 0; i < du_cnt; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if (i + j < du_cnt) {
				rda.du_off = j;
				memset(mem, 0, DTAG_SZE);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) == 0) {
					srb_apl_trace(LOG_ALW, 0x9add, "[%d] 0x%x - 0x%x", i + j, *mem, *(mem + mem_len - 1));
				} else {
					srb_apl_trace(LOG_ALW, 0x75fa, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
				}
			}
		}
		rda.row += 1;
	}
#ifdef CHK_FWCONFIG_CONTENT
	dtagprint(dtag,mem_len*4);
#endif
	dtag_put(DTAG_T_SRAM, dtag);

}

slow_code void sb_verify_(rda_t rda, u32 mem_len, u32 dus)
{
	int i, j;
	u32 *mem;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	for (i = 0; i < dus; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if (i + j < dus) {
				rda.du_off = j;
				memset(mem, 0, DTAG_SZE);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) == 0) {
					srb_apl_trace(LOG_ALW, 0x9876, "[%d] 0x%x - 0x%x", i + j, *mem, *(mem + mem_len - 1));
				} else {
					srb_apl_trace(LOG_ALW, 0x1618, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
				}
			}
		}
		rda.row += 1;
	}

	dtag_put(DTAG_T_SRAM, dtag);

}

ddr_code bool srb_read_kotp(rda_t rda, u32 *kotp)
{
	fwb_t *fwb = (fwb_t*) (SRAM_BASE+sizeof(srb_t)-512);
	u8 tmp_buffer[32];  // to save each reading from image
	u8 res_buffer[32];  // keep KOTP to compare following image
	u32 ofst = 0;
	u32 *mem;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	// read each image (cnt: 8)
	for(u32 bkup = 0; bkup < 4; bkup++)
	{
		// get rda
		if(bkup < 2)
			rda = fwb->fw_slot[0].fw_slot;
		else
			rda = fwb->fw_slot[0].fw_slot_mirror;
		if(bkup & 0x1)
		{
			// read 1st DU to chk img size
			//rda.du_off = 0;
			//pl.pl.du_ofst = rda.du_off;
			memset(mem, 0, DTAG_SZE);
			if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1))
			{
				goto MR_MODE_ACCESS_FAIL;
			}
			u16 img_du_cnt = (*(mem+2)) >> 16;
			srb_apl_trace(LOG_INFO, 0x337f, "read img du cnt: %d", img_du_cnt);

			rda.row += occupied_by(img_du_cnt, SRB_MR_DU_CNT_PAGE);
		}

		// read 1st DU
		pl.pl.du_ofst = 0;
		memset(mem, 0, DTAG_SZE);
		if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1))
		{
			goto MR_MODE_ACCESS_FAIL;
		}

		// search KOTP
		for(u32 i=0; i<1024; i+=4)
		{
			if((mem[i])==0x50544F4B) // KOTP
			{
				ofst = mem[i+1];
				break;
			}
		}

		srb_apl_trace(LOG_ALW, 0xbb13, "KOTP offset 0x%x", ofst);
		if(ofst)
		{
			//ofst += 0x1000;
			u32 du_idx = (ofst/SRB_MR_DU_SZE);
			u32 page_idx = (du_idx/SRB_MR_DU_CNT_PAGE);
			du_idx %= SRB_MR_DU_CNT_PAGE;
			srb_apl_trace(LOG_INFO, 0x6983, "read img pg_ofst: %d, du_ofst: %d", page_idx, du_idx);

			////////// TODO //////////
			// read specified DU
			rda.row += page_idx;
			rda.du_off = du_idx;
			//pl.pl.du_ofst = du_idx;
			memset(mem, 0, DTAG_SZE);
			if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1))
			{
				goto MR_MODE_ACCESS_FAIL;
			}

			u32 kotp_start = (ofst % SRB_MR_DU_SZE);
			if((SRB_MR_DU_SZE - kotp_start) < 32)
			{
				memcpy(tmp_buffer, ((u8 *)mem)+kotp_start, (SRB_MR_DU_SZE - kotp_start));
				////////// TODO //////////
				// read specified DU "next"
				rda.du_off = ((du_idx+1) % SRB_MR_DU_CNT_PAGE);
				//pl.pl.du_ofst = ((du_idx+1) % SRB_MR_DU_CNT_PAGE);
				if(rda.du_off == 0)
					rda.row += 1;
				memset(mem, 0, DTAG_SZE);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1))
				{
					goto MR_MODE_ACCESS_FAIL;
				}

				// copy from next DU
				memcpy(tmp_buffer + (SRB_MR_DU_SZE - kotp_start), ((u8 *)mem), (32 - SRB_MR_DU_SZE + kotp_start));
			}
			else
				memcpy(tmp_buffer, ((u8 *)mem)+kotp_start, 32);
		}
		else
			srb_apl_trace(LOG_ERR, 0xbbfe, "NOT find KOTP");

		for(u8 i=0; i<32; i++)
			srb_apl_trace(LOG_INFO, 0xf3b9, "IMG#%dKOTP 0x%x", bkup, tmp_buffer[i]);

		if(bkup)
		{
			for(u8 i=0; i<32; i++)
			{
				if(res_buffer[i]!=tmp_buffer[i])
				{
					srb_apl_trace(LOG_ERR, 0x2ed7, "KOTP compare err between 0 and %d", bkup);

					dtag_put(DTAG_T_SRAM, dtag);
					return true;
				}
			}
		}
		else
			memcpy(res_buffer, tmp_buffer, 32);
	}

	dtag_put(DTAG_T_SRAM, dtag);
	memcpy(kotp, res_buffer, 32);

	return false; // means OK

MR_MODE_ACCESS_FAIL:
	srb_apl_trace(LOG_ALW, 0xdbc7, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
	dtag_put(DTAG_T_SRAM, dtag);
	return true;
}

//#define rsa_verify_addr 0x10006d95
//#define rsa_verify_addr 0x10006db9 ///andy test
#define rsa_verify_addr 0x1000707d
typedef int (rsa_verify_t(void *, const u32 *, const u8 *, u32 *));
rsa_verify_t *rsa_verify = (rsa_verify_t*) rsa_verify_addr;

#if 0
#define sha256_init_addr	0x10006f35
#define sha256_update_addr	0x10006f89
#define sha256_final_addr	0x10006fd1
#else
#define sha256_init_addr	0x100072cd
#define sha256_update_addr	0x10007321
#define sha256_final_addr	0x10007369
#endif

typedef void (sha256_init_t(void *));
typedef void (sha256_update_t(void *, const u8 *, int));
typedef void (sha256_final_t(void *, u8 *));

sha256_init_t *sha256_init = (sha256_init_t*) sha256_init_addr;
sha256_update_t *sha256_update = (sha256_update_t*) sha256_update_addr;
sha256_final_t *sha256_final = (sha256_final_t*) sha256_final_addr;

#define CONFIG_RSA_KEY_SIZE 2048 /* default to 2048-bit key length */
#define RSANUMBYTES ((CONFIG_RSA_KEY_SIZE) / 8)       /* 256 Bytes */
#define RSANUMWORDS (RSANUMBYTES / sizeof(u32))  /* 64 DW */


static fast_code loader_section_t *get_section(u32 section_id, loader_image_t *image)
{
	if (image->signature == IMAGE_SIGNATURE) {
		int i = 0;
		u32 section_crc = crc32(image->sections, image->section_num * sizeof(loader_section_t));
		if (section_crc != image->section_csum)
			return NULL;

		for (i = 0; i < image->section_num; i++) {
			loader_section_t *section = &image->sections[i];
			if (section->identifier == section_id) {
				srb_apl_trace(LOG_DEBUG, 0xcdd3, "get section 0x%x, len %d, offset %x",
					section->identifier, section->length, section->offset);
				return section;
			}
		}
	}
	return NULL;
}

static fast_code u16 get_section_data(rda_t rda, dtag_t dtag, loader_section_t section, u16 cur_du_off, void *dest)
{
	u32 len;
	void *mem;
	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	rda.row += section.offset / MR_PAGE_SZE;
	rda.du_off += (section.offset / SRB_MR_DU_SZE) % SRB_MR_DU_CNT_PAGE;
	if(Loader_data == 0x5A)
	{
		rda.du_off = 0;
	}
	if (cur_du_off != (section.offset / SRB_MR_DU_SZE)) {
		if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
			dtag_put(DTAG_T_SRAM, dtag);
			srb_apl_trace(LOG_ERR, 0x7f84, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
			return cur_du_off;
		}
	} else
		cur_du_off = section.offset / SRB_MR_DU_SZE;

	mem = dtag2mem(dtag);
	len = section.offset & DTAG_MSK;
	mem += len;
	len = min(section.length, SRB_MR_DU_SZE - len);
	memcpy(dest, mem, len);
	if (len != section.length) {
		if (rda.du_off == (SRB_MR_DU_CNT_PAGE - 1)) {
			rda.row++;
			rda.du_off = 0;
		} else
			rda.du_off++;

		if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
			dtag_put(DTAG_T_SRAM, dtag);
			srb_apl_trace(LOG_ERR, 0x824e, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
			return cur_du_off;
		}
		cur_du_off++;
		mem = dtag2mem(dtag);
		dest += len;
		len = section.length - len;
		memcpy(dest, mem, len);
	}
	return cur_du_off;
}

slow_code bool srb_image_verify_sha3(rda_t image_rda, void *fw_sha3, u8 mode)
{
	loader_image_t *image;
	rda_t rda;
	dtag_t dtag;
	dtag_t dtag_data;
	loader_section_t *p;
	loader_section_t section_sha, section_sig, section_pk;
	void *fw_sha3_256_sha256;
	void *public_key;
	void *workspace;
	void *fw_sig;
	u16 cur_du_off;

	rda = image_rda;
	dtag = dtag_get(DTAG_T_SRAM, (void **)&image);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	memset((void *)image, 0, DTAG_SZE);

	if(mode == LDR_Mode)
		rda.du_off = 1;// 1st 4k data is combo header, it needs to bypass

	srb_apl_trace(LOG_ALW, 0x1ac4, "verify fw image sha3 Row(0x%x)@CH/CE/OFF(%d/%d/%d)",
			rda.row, rda.ch, rda.dev, rda.du_off);

	if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0)
    {
		dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0x835c, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
		return false;
	}

	p = get_section(ID_PKEY, image);
	sys_assert(p != NULL);
	sys_assert(p->length < DTAG_SZE);
	section_pk = *p;
	p = get_section(ID_SHA256, image);
	sys_assert(p != NULL);
	sys_assert(p->length == 32);
	section_sha = *p;
	p = get_section(ID_SIGN, image);
	sys_assert(p != NULL);
	sys_assert(p->length == 256);
	sys_assert(p->offset == (section_sha.offset + section_sha.length));
	section_sig = *p;

	dtag_data = dtag_get(DTAG_T_SRAM, (void **)&workspace);
	sys_assert(dtag.dtag != _inv_dtag.dtag);
    if(mode == LDR_Mode)
    {
        section_pk.offset += 0x1000;
        section_sha.offset += 0x1000;
        section_sig.offset += 0x1000;
    }

    //srb_apl_trace(LOG_ALW, 0, "",

	///Add for loader, du_off is fix to 1?
	if(0)//mode == LDR_Mode)
	{
		cur_du_off = 0;
		//image_rda.du_off = ((section_pk.offset / SRB_MR_DU_SZE)+1) % SRB_MR_DU_CNT_PAGE;
		//image_rda.row = image_rda.row + (section_pk.offset / MR_PAGE_SZE)+1;
		image_rda.row++;
		image_rda.du_off = 0;

		srb_apl_trace(LOG_ALW, 0x8834, "image_rda Row(0x%x)@CH/CE(%d/%d) off(%d)",
				image_rda.row, image_rda.ch, image_rda.dev, image_rda.du_off);
		Loader_data = 0x5A;
		//section_pk.offset = (section_pk.offset / SRB_MR_DU_SZE) % SRB_MR_DU_CNT_PAGE;

		//printk("offset:%x /PAGE(%d)\n",section_pk.offset, section_pk.offset / MR_PAGE_SZE);
		//printk("du:%x page(%d)\n",section_pk.offset ,(section_pk.offset / SRB_MR_DU_SZE) % SRB_MR_DU_CNT_PAGE);
	}
	else
	{
		Loader_data = 0;
		cur_du_off = 0;
	}


	public_key = workspace + (3 * RSANUMBYTES);
	cur_du_off = get_section_data(image_rda, dtag, section_pk, cur_du_off, public_key);

    #if 0
    for (int i=0; i<32; i++)
    {
        srb_apl_trace(LOG_ERR, 0x144d, "SHA3 value[%d] 0x%x", i, *(u8 *)(fw_sha3+i));
    }
    #endif

	sha256_init((void*)workspace);
	sha256_update((void*)workspace, (const u8 *)fw_sha3, 32);
	sha256_final((void*)workspace, fw_sha3);
	fw_sha3_256_sha256 = public_key + section_pk.length;

	if(0)//mode == LDR_Mode)
	{
		cur_du_off = 0;
		//image_rda.du_off = ((section_sha.offset / SRB_MR_DU_SZE)+1) % SRB_MR_DU_CNT_PAGE;
		//image_rda.row = image_rda.row + (section_sha.offset / MR_PAGE_SZE)+1;

		//image_rda.row++;
		image_rda.du_off = 0;
		srb_apl_trace(LOG_ALW, 0x1fff, "image_rda Row(0x%x)@CH/CE(%d/%d) off(%d)",
				image_rda.row, image_rda.ch, image_rda.dev, image_rda.du_off);
		Loader_data = 0x5A;
		//section_sha.offset = (section_sha.offset / SRB_MR_DU_SZE) % SRB_MR_DU_CNT_PAGE;
	}
	else
	{
		Loader_data = 0x0;
	}

	cur_du_off = get_section_data(image_rda, dtag, section_sha, cur_du_off, fw_sha3_256_sha256);

	if (memcmp(fw_sha3_256_sha256, fw_sha3, section_sha.length))
    {
		srb_apl_trace(LOG_ERR, 0x3c03, "SHA3 Cert Failed %x %x", *(u32 *)fw_sha3_256_sha256, *(u32 *)fw_sha3);
        #if 1
        for (int i=0; i<section_sha.length/4; i++)
        {
            srb_apl_trace(LOG_ERR, 0x4404, "SHA3-256 value[%d] 0x%x, 0x%x", i, *(u32 *)(fw_sha3_256_sha256+4*i), *(u32 *)(fw_sha3+4*i));
        }
        #endif
		dtag_put(DTAG_T_SRAM, dtag);
		dtag_put(DTAG_T_SRAM, dtag_data);
		return false;
	}
	else
	{
		srb_apl_trace(LOG_ALW, 0x73bf, "SHA3 Cert PASS %x %x", *(u32 *)fw_sha3_256_sha256, *(u32 *)fw_sha3);
	}

	fw_sig = fw_sha3_256_sha256 + section_sha.length;
	///Add for loader, du_off is fix to 1?
	if(0)//mode == LDR_Mode)
	{
		cur_du_off = 0;
		//image_rda.du_off = ((section_sig.offset / SRB_MR_DU_SZE)+1) % SRB_MR_DU_CNT_PAGE;
		//image_rda.row = image_rda.row + (section_sig.offset / MR_PAGE_SZE)+1;

		srb_apl_trace(LOG_ALW, 0x9c15, "image_rda Row(0x%x)@CH/CE(%d/%d) off(%d)",
				image_rda.row, image_rda.ch, image_rda.dev, image_rda.du_off);
		Loader_data = 0x5A;
		//section_sig.offset = (section_sig.offset / SRB_MR_DU_SZE) % SRB_MR_DU_CNT_PAGE;
	}
	else
	{
		Loader_data = 0x0;
	}
	cur_du_off = get_section_data(image_rda, dtag, section_sig, cur_du_off, fw_sig);
	if (rsa_verify(public_key, (u32 *)fw_sig, (const u8*)fw_sha3_256_sha256, workspace) != 0) {
		srb_apl_trace(LOG_ERR, 0xf4a2, "RSA Cert Failed ...");
		dtag_put(DTAG_T_SRAM, dtag);
		dtag_put(DTAG_T_SRAM, dtag_data);
		return false;
	}
	else
	{
		srb_apl_trace(LOG_ALW, 0x5dc1, "RSA Cert PASS");
	}
	dtag_put(DTAG_T_SRAM, dtag);
	dtag_put(DTAG_T_SRAM, dtag_data);
	return true;
}

slow_code bool srb_get_loader_public_key_otp(void *pkey)
{
	dtag_t dtag;
	srb_t *srb;
	loader_image_t *image;
	loader_section_t *p;
	rda_t rda;
	if (srb_scan_and_load(&dtag) == false)
		return false;

	srb = (srb_t *)dtag2mem(dtag);
	rda = srb->enc_pos;
	rda.row = srb->srb_hdr.srb_sb_row;
	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
		dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0xb59f, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
		return false;
	}

	image = (loader_image_t *)srb;
	p = get_section(ID_KOTP, image);
	sys_assert(p != NULL);
	sys_assert(p->length == 32);
	sys_assert(p->offset < DTAG_SZE);
	memcpy(pkey, ((void *)image) + p->offset, p->length);
	dtag_put(DTAG_T_SRAM, dtag);
	return true;
}

fast_code bool srb_image_loader_cpu34_atcm(rda_t image_rda)
{
	loader_image_t *image;
	rda_t rda;
	dtag_t dtag;
	u16 cur_du_off;
	loader_section_t cpu3_atcm_section, cpu4_atcm_section;

	rda = image_rda;
	dtag = dtag_get(DTAG_T_SRAM, (void **)&image);
	sys_assert(dtag.dtag != _inv_dtag.dtag);

	bm_pl_t pl = {
		.pl.dtag = dtag.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP
	};

	memset((void *)image, 0, DTAG_SZE);
	srb_apl_trace(LOG_ALW, 0xa4bc, "loader fw image Row(0x%x)@CH/CE(%d/%d)",
			rda.row, rda.ch, rda.dev);

	if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) != 0) {
		dtag_put(DTAG_T_SRAM, dtag);
		srb_apl_trace(LOG_ERR, 0x1d06, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
		return false;
	}

	memset(&cpu3_atcm_section, 0, sizeof(loader_section_t));
	memset(&cpu4_atcm_section, 0, sizeof(loader_section_t));
	if (image->signature == IMAGE_SIGNATURE) {
		int i = 0;
		u32 section_crc = crc32(image->sections, image->section_num * sizeof(loader_section_t));
		if (section_crc != image->section_csum)
			return false;

		for (i = 0; i < image->section_num; i++) {
			loader_section_t *section = &image->sections[i];
			srb_apl_trace(LOG_INFO, 0xd1c1, "get section 0x%x, len %d, offset %x",
					section->identifier, section->length, section->offset);
			if (section->pma == CPU3_ATCM_BASE) {
				cpu3_atcm_section = *section;
			} else if (section->pma == CPU4_ATCM_BASE)
				cpu4_atcm_section = *section;
		}
	} else
		return false;

	sys_assert(cpu3_atcm_section.pma != 0);
	sys_assert(cpu4_atcm_section.pma != 0);
	cur_du_off = 0;
	cur_du_off = get_section_data(image_rda, dtag, cpu3_atcm_section, cur_du_off, (void *)cpu3_atcm_section.pma);
	cur_du_off = get_section_data(image_rda, dtag, cpu4_atcm_section, cur_du_off, (void *)cpu4_atcm_section.pma);
	dtag_put(DTAG_T_SRAM, dtag);
	return true;
}

slow_code void srb_trans_to_mr_df(dft_btmp_t *mr_df, u32 *df)
{
	u32 pl;
	u32 lun;
	u32 ce;
	u32 ch;
	u32 pb_idx = 0;

	for (ch = 0; ch < nand_channel_num(); ch++) {
		for (ce = 0; ce < nand_target_num(); ce++) {
			for (lun = 0; lun < nand_lun_num(); lun++) {
				for (pl = 0; pl < nand_plane_num(); pl++) {
					u32 idx = pb_idx >> 5;
					u32 off = pb_idx & (31);

					if ((df[idx] & (1 << off)) == 0) {
						u32 dpos;

						dpos = pl << PLN_SHF;
						dpos += lun << LUN_SHF;
						dpos += ce << CE_SHF;
						dpos += ch << CH_SHF;

						idx = dpos >> 5;
						off = dpos & (31);
						mr_df->dft_bitmap[idx] &= ~(1 << off);
					}
					pb_idx++;
				}
			}
		}
	}
}
