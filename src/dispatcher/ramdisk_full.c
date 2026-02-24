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
/*! \file ramdisk_full.c
 * @brief Ramdisk supports full available sram/dram size with one-one mapping
 *
 * \addtogroup dispatcher
 * \defgroup ramdisk
 * \ingroup dispatcher
 * @{
 *
 * Ramdisk supports full available sram/dram size with one-one mapping
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "btn_export.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "rainier_soc.h"
#include "ddr.h"

/*! \cond PRIVATE */
#define __FILEID__ ramdiskfull
#include "trace.h"
/*! \endcond */

#include "btn_helper.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define MAX_TRNASFER_DTAG	128	// 512KB

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
static fast_data dtag_t dtag_wr_list[MAX_TRNASFER_DTAG];
static fast_data u32 sdram_dtag_offset = 0;
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
/*!
 * @brief host normal write queue updated handler
 *
 * @param[in] payload	should be BM payload list
 *
 * @return		not used
 */
static fast_code void ramdisk_nrm_wd_updt(bm_pl_t *bm_tag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(bm_tag->pl.btag);
	btn_cmd_t *bcmd = btag2bcmd(bm_tag->pl.btag);

	if (--bcmd_ex->du_xfer_left == 0)
		btn_cmd_rels_push(bcmd, RLS_T_WRITE_CQ);
}

/*!
 * @brief event handler for btn write group 0 update
 *
 * @param param		not used
 * @param payload	payload address list
 * @param count		list count
 *
 * @return		not used
 */
fast_code static void ramdisk_wd_updt_nrm_par(u32 param, u32 payload, u32 count)
{
	u32 *addr = (u32 *) payload;
	u32 i;

	for (i = 0; i < count; i++) {
		bm_pl_t *bm_pl = (bm_pl_t *) dma_to_btcm(addr[i]);

		switch (bm_pl->pl.type_ctrl) {
		case BTN_NVM_TYPE_CTRL_NRM:
			ramdisk_nrm_wd_updt(bm_pl);
			break;
		default:
			panic("no support");
			break;
		}
	}
}

/*!
 * @brief btn command handler, handler write/read commands here
 *
 * @param bcmd		btn command
 * @param btag		tag of btn command
 *
 * @return		not used
 */
fast_code void bcmd_exec(btn_cmd_t *bcmd, int btag)
{
	btn_cmd_ex_t *bcmd_ex = btag2bcmd_ex(btag);
	lda_t lda;
	u64 slba;
	u32 i;
	slba = bcmd_get_slba(bcmd);
	bcmd_ex->ndu = calc_du_cnt(slba, bcmd->dw3.b.xfer_lba_num);
	disp_apl_trace(LOG_DEBUG, 0x9d05, "bcmd_exec: bcmd type %x slba %x nlb %x", bcmd->dw0.b.cmd_type, slba, bcmd_ex->ndu);

	switch (bcmd->dw0.b.cmd_type) {
	case PRI_READ:
	case NVM_READ:
		lda = LBA_TO_LDA(slba);

#if defined(DDR)
		dtag_t dtag = {.b.type=0, .b.in_ddr = 1,};
#else
		dtag_t dtag = {.b.type=0, .b.in_ddr = 0,};
#endif
		for (i = 0; i < bcmd_ex->ndu; i++, lda++) {
			dtag.b.dtag = lda + sdram_dtag_offset;
			bm_rd_dtag_commit(i, btag, dtag);
		}
		break;
	case NVM_WRITE:
		sys_assert(bcmd_ex->ndu <= MAX_TRNASFER_DTAG);
		bcmd_ex->du_xfer_left = bcmd_ex->ndu;
		for (i=0 ; i < bcmd_ex->ndu; i++){
			dtag_wr_list[i].b.dtag = slba + i + sdram_dtag_offset;
		}
		bm_free_wr_load(dtag_wr_list, bcmd_ex->ndu);
		break;
	case IO_MGR:
		btn_iom_cmd_rels(bcmd);
		break;
	default:
		panic("not support");
		break;
	}
}

/*!
 * @brief ramdisk non-IO request execution
 *
 * For read/write, will be handled in bcmd_exec
 *
 * @param req		request to be executed.
 *
 * @return		not used
 */
fast_code bool req_exec(req_t *req)
{
	disp_apl_trace(LOG_DEBUG, 0xbb8a, "req_exec: req->opcode %x", req->opcode);
	switch (req->opcode) {
	case REQ_T_READ:
	case REQ_T_WRITE:
	case REQ_T_COMPARE:
	case REQ_T_TRIM:
		panic("not support");
		break;
	case REQ_T_FORMAT:
		req->completion(req);
		break;
	case REQ_T_FLUSH:
		req->completion(req);
		break;
	case REQ_T_WZEROS:
		{
			u32 slda = req->lba.srage.slba;
			u16 nld = req->lba.srage.nlb;
#if defined(DDR)
			memset((void*)ddtag2mem(slda), 0 , DTAG_SZE*nld);
#else
			memset((void*)sdtag2mem(slda), 0 , DTAG_SZE*nld);
#endif
			req->completion(req);
		}
		break;
	case REQ_T_WUNC:
		panic("not support");
		break;
	default:
		sys_assert(0);
		break;
	}

	return true;
}

/*!
 * @brief ramdisk initialize
 *
 * Register all BTN event handler functions.
 *
 * @return	not used
 */
static void ramdisk_init(void)
{
	nvme_ctrl_attr_t ctrl_attr;

#if defined(DDR)
	u64 lb_cnt = (ddr_get_capapcity() >> HLBASZ);
	sdram_dtag_offset = 0;
#else
	u64 lb_cnt = (((u32) &__dtag_mem_end - (u32) &__dtag_mem_start) >> HLBASZ);
	sdram_dtag_offset = (((u32) &__dtag_mem_start - SRAM_BASE) >> HLBASZ);
#endif
	evt_register(ramdisk_wd_updt_nrm_par, 0, &evt_wd_grp0_nrm_par_upt);
	btn_cmd_hook(bcmd_exec, NULL);
	int i;
	for (i=0 ; i < MAX_TRNASFER_DTAG; i++){
#if defined(DDR)
		dtag_wr_list[i].b.in_ddr = 1;
#else
		dtag_wr_list[i].b.in_ddr = 0;
#endif
		dtag_wr_list[i].b.type = 0;
		dtag_wr_list[i].b.dtag = 0;
	}

	ctrl_attr.all = 0;
	ctrl_attr.b.compare = 0;
	ctrl_attr.b.write_uc = 0;
	ctrl_attr.b.dsm = 0;
	ctrl_attr.b.write_zero = 0;
	nvmet_set_ctrlr_attrs(&ctrl_attr);

	nvme_ns_attr_t attr;
	memset((void*)&attr, 0, sizeof(attr));

	nvme_ns_attr_t *p_ns = &attr;
	p_ns->hw_attr.nsid = 1;
	p_ns->hw_attr.pad_pat_sel = 1;
	p_ns->fw_attr.support_pit_cnt = 0;
	p_ns->fw_attr.support_lbaf_cnt = 1;
	p_ns->fw_attr.type = NSID_TYPE_ACTIVE;
	p_ns->fw_attr.ncap = lb_cnt;
	p_ns->fw_attr.nsz = lb_cnt;
	p_ns->hw_attr.lb_cnt = lb_cnt;

	nvmet_set_ns_attrs(p_ns, true);

	nvmet_restore_feat(NULL);

	disp_apl_trace(LOG_ALW, 0x6710, "Ramdisk full init done lb_cnt %d sdram_dtag_offset 0x%x", lb_cnt, sdram_dtag_offset);
}

/*! \cond PRIVATE */
module_init(ramdisk_init, DISP_APL);
/*! \endcond */
/*! @} */
