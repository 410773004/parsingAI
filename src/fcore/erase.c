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

/*! \file erase.h
 * @brief define ftl core erase handler
 *
 * issue die erase commands for target spb
 *
 * \addtogroup fcore
 * \defgroup erase
 * \ingroup fcore
 * @{
 */
#include "types.h"
#include "rainier_soc.h"
#include "sect.h"
#include "queue.h"
#include "pool.h"
#include "bf_mgr.h"
#include "stdlib.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "erase.h"
#include "mpc.h"
#include "die_que.h"
#include "ftl_remap.h"

#ifdef SKIP_MODE
#include "pstream.h"
#include "ftl_core.h"
#endif

#include "GList.h"	// ISU, SPBErFalHdl
#include "../ftl/ErrorHandle.h"

extern struct nand_info_t shr_nand_info;	/// nand infomation

/*!
 * @brief callback function for each die erase command issued from ftl_core_erase
 *
 * @param ncl_req	ncl erase request
 *
 * @return		not used
 */

fast_code void erase_done(ncl_w_req_t *ncl_req)
{
	erase_ctx_t *ctx = (erase_ctx_t *) ncl_req->req.caller;

	// Record ErFal happened in ISU, erase_ctx_t (by SPB), SPBErFalHdl
	if (ncl_req->req.op_type.b.erase_err)
	{
		ctx->spb_ent.b.erFal = true;	
	}

	sys_assert(otf_e_reqs);
	otf_e_reqs--;

	put_ncl_e_req((ncl_e_req_t *)ncl_req);

	ctx->cmpl_cnt++;
	if (ctx->cmpl_cnt == ctx->issue_cnt)
	{
		// ISU, SPBErFalHdl
		#if 0	// ISU, TSSP, PushGCRelsFalInfo
		if (ctx->spb_ent.b.erFal == true)
		{
			MK_FailBlk_Info fail_blk_info;
			fail_blk_info.all = 0;	// Erase failed, no user data, need not backup PBT, gc.		
			fail_blk_info.b.wBlkIdx	= ctx->spb_ent.b.spb_id;
		    EH_SetFailBlk(fail_blk_info.all);	// Trigger ErrHandle_Task handle bad blocks.

			flush_to_nand(EVT_MARK_GLIST);	// Save log after whole SPB erase done, ISU, SPBErFalHdl
		}
		#endif

		ctx->cmpl(ctx);
	}	
}

/*!
 * @brief issue die commands to erase a spb
 *
 * @param ctx		erase context
 *
 * @return		not used
 */
fast_code void ftl_core_erase(erase_ctx_t *ctx)
{
	u32 i;
	u32 spb_id = ctx->spb_ent.b.spb_id;
	u32 ptr = 0;

#ifdef SKIP_MODE
	u8* spb_df_ptr = get_spb_defect(spb_id);
	u8 pl_cnt=0;
#endif
	for (i = 0; i < shr_nand_info.lun_num; i++) {
		ncl_e_req_t *ncl_req = get_ncl_e_req();
		u32 j;

		sys_assert(ncl_req);
		otf_e_reqs++;

#ifdef SKIP_MODE
		for (;;) {
			u32 itl = 0; 
			u32 bad_plane = 0;
			for (j = 0; j < shr_nand_info.geo.nr_planes; j++) {	
				u32 idx = ptr >> 3;
				u32 off = ptr & 7;
				if (((spb_df_ptr[idx] >> off)&1) == 0) { //good plane
					ncl_req->e.pda[itl] = nal_make_pda(spb_id, ptr << DU_CNT_SHIFT);
					ncl_req->req.die_id = i;
					itl++;
				} else {	
					bad_plane++;
				}
				ptr++;
			}

			pl_cnt = shr_nand_info.geo.nr_planes - bad_plane;
			if (bad_plane == shr_nand_info.geo.nr_planes) {			
				if (++i >= shr_nand_info.lun_num) {
					break;
				}
				continue; //for(;;)
			} else {
				break;
			}
		}

		if (i >= shr_nand_info.lun_num) {
			put_ncl_e_req(ncl_req);
			otf_e_reqs--;
			break;
		}

		ncl_req->req.cnt = pl_cnt;
#else
		for (j = 0; j < shr_nand_info.geo.nr_planes; j++) {
			pda_t pda = nal_make_pda(spb_id, ptr << DU_CNT_SHIFT);
			ncl_req->req.die_id = i;
			ncl_req->e.pda[j] = ftl_remap_pda(pda);
			ptr++;
		}
		ncl_req->req.cnt = shr_nand_info.geo.nr_planes;
#endif

		ncl_req->req.die_id = i;
		ncl_req->req.op_type.all = 0;
		ncl_req->req.op_type.b.erase = 1;
		ncl_req->req.op_type.b.slc = ctx->spb_ent.b.slc;
		ncl_req->req.caller = (void *) ctx;
		ncl_req->req.cmpl = erase_done;
		ctx->issue_cnt++;
		die_que_wr_era_ins(&ncl_req->req, i);
	}
}

/*! @} */
