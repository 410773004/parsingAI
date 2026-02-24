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

#include "fcprecomp.h"
#include "mod.h"
#include "queue.h"
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "srb.h"
#include "l2p_mgr.h"
#include "ftl_core.h"
#include "rdisk.h"
#include "ipc_api.h"
#include "event.h"
#include "addr_trans.h"
#include "fw_download.h"
#include "idx_meta.h"
#include "scheduler.h"
#include "ddr_info.h"
/*! \file be_main.c
 * @brief define BE CPU behavior, including scheduler, ftl core, receive data entries from FE CPU
 *
 * \addtogroup be_main
 * \defgroup be_main
 * \ingroup be_main
 * @{
 */

/*! \cond PRIVATE */
#define __FILEID__ bemc
#include "trace.h"
/*! \endcond */

fast_data_zi u32 dtag_recv = 0;						///< number of received dtag

share_data_zi volatile u32 shr_dtag_ins_cnt;					///< the count of dtag was inserted to be
share_data_zi volatile int _fc_credit;
share_data_zi void *shr_ddtag_meta;					///< ddr dtag meta global pointer
share_data_zi void *shr_dtag_meta;					///< dtag meta global pointer
share_data_zi void *shr_tbl_meta;					


slow_data_ni struct du_meta_fmt dtag_meta[SRAM_IN_DTAG_CNT] ALIGNED(32);	///< dtag meta data
slow_data_zi struct du_meta_fmt *ddtag_meta;				///< dtag meta data

BUILD_BUG_ON(sizeof(io_meta_t) != sizeof(struct du_meta_fmt));

#ifdef SAVE_DDR_CFG		//20201008-Eddie
	//extern u8 need_save_to_CTQ;
	share_data volatile ddr_info_t* ddr_info_buf_in_ddr;
	share_data fw_config_set_t* fw_config_main_in_ddr;
	share_data volatile u8 need_save_to_CTQ;
	extern fw_config_set_t *fw_config_main;
#endif
/*!
 * @brief l2p pda updated done callback function
 *
 * @param p0	not used
 * @param p1	not used
 * @param p2	not used
 *
 * @return	not used
 */
fast_code void be_l2p_updt_done(u32 p0, u32 p1, u32 p2)
{
	// do nothing
}
share_data volatile u32 cache_handle_dtag_cnt;
share_data volatile u32 otf_fua_cmd_cnt;
fast_code void be_dtag_comt(void)
{
	bool dirty;
	u32 rptr = shr_dtag_comt.que.rptr;
	u32 end = shr_dtag_comt.que.wptr;
	u32 size = shr_dtag_comt.que.size;
	u32 cnt = 0;
	u32 ins_cnt;

	if (rptr == end)
		return;

	dirty = ftl_core_open(1);
	//if (!dirty || ftl_core_ns_padding(1) || ftl_core_nrm_bypass(1))
	if (!dirty || ftl_core_nrm_bypass(1))
		return;

	ftl_core_flush_blklist_need_resume();

	if (rptr > end) {
		cnt = size - rptr;
		ins_cnt = ftl_core_host_ins_dcomt(&shr_dtag_comt.lda[rptr], &shr_dtag_comt.que.buf[rptr], cnt);
		rptr = (ins_cnt == cnt) ? 0 : (rptr + ins_cnt);
		dtag_recv += ins_cnt;
		shr_dtag_comt.que.rptr = rptr;
		if (ins_cnt < cnt)
			return;
	}

	if (rptr < end) {
		cnt = end - rptr;
		ins_cnt = ftl_core_host_ins_dcomt(&shr_dtag_comt.lda[rptr], &shr_dtag_comt.que.buf[rptr], cnt);
		rptr = (ins_cnt == cnt) ? end : (rptr + ins_cnt);
		dtag_recv += ins_cnt;
		shr_dtag_comt.que.rptr = rptr;
		if (ins_cnt < cnt)
			return;
	}

	if (dtag_recv == shr_dtag_ins_cnt) {
        //only fua cmd exist, force flush
        if ((cache_handle_dtag_cnt == 0) && otf_fua_cmd_cnt) {
            u16 padding_cnt = ftl_core_close_open_die(HOST_NS_ID, FTL_CORE_NRM, NULL);
			if(_fc_credit > padding_cnt)
				_fc_credit -= padding_cnt;
        }
        ftl_core_flush_resume(1);
	}

}

/*!
 * @brief dtag initialization, initialized reserved dtag in current CPU
 *
 * @return	not used
 */
 #if 0 //No Need
init_code static void be_dtag_init(void)
{
	dtag_mgr_init(DTAG_T_SRAM, SRAM_IN_DTAG_CNT, 0);
	dtag_add(DTAG_T_SRAM, &___rsvd_dtag_section_start,
			(u32)&___rsvd_dtag_section_end - (u32)&___rsvd_dtag_section_start);
}

module_init(be_dtag_init, NCL_PRE);
#endif
/*!
 * @brief initialize l2p engine for be CPU, l2pe address initialization
 *
 * @return	not used
 */
init_code static void l2p_be_init(void)
{
	l2p_addr_init(nand_channel_num(),
			nand_target_num(),
			nand_lun_num(),
			nand_plane_num(),
			nand_block_num(),
			nand_page_num(),
			DU_CNT_PER_PAGE,
			true);

	l2p_mgr_init();

	evt_register(be_l2p_updt_done, 0, &evt_l2p_updt2);
}

/*!
 * @brief if SRB was not loaded, try to find it
 *
 * @return	not used
 */
init_code void misc_modules_init_base_on_srb(void)
{
	srb_t *srb = (srb_t *) SRB_HD_ADDR;
	dtag_t srb_dtag;

	srb_dtag = _inv_dtag;
	if (srb->srb_hdr.srb_signature != SRB_SIGNATURE) {
		sys_assert(ncl_enter_mr_mode() != false);
		sys_assert(srb_scan_and_load(&srb_dtag) != false);

		srb = (srb_t *)dtag2mem(srb_dtag);
	}

	fwdl_init(srb);
	//srb_sus_init(srb);

	if (srb_dtag.dtag != _inv_dtag.dtag) {
		memcpy((void *)SRB_HD_ADDR, dtag2mem(srb_dtag), sizeof(srb_t));
		dtag_put(DTAG_T_SRAM ,srb_dtag);
		ncl_leave_mr_mode();
	}
}

/*!
 * @brief initialization of be CPU, there is ftl core and scheduler
 *
 * @return	not used
 */
init_code static void be_init(void)
{
	shr_nand_info = nand_info;

	CBF_INIT(&shr_dtag_comt.que);
	dtag_comt_recv_hook((cbf_t *)&shr_dtag_comt.que, be_dtag_comt);

	local_item_done(nand_geo_init);
	wait_remote_item_done(ddr_init);
	wait_remote_item_done(l2p_para_init);

	idx_meta_init();

	local_item_done(idx_meta_init);
	wait_remote_item_done(tag_meta_init);

	ncl_set_meta_base(dtag_meta, META_DTAG_SRAM_BASE);
	ncl_set_meta_base(shr_ddtag_meta, META_DTAG_DDR_BASE);
	shr_dtag_meta = &dtag_meta;
	ddtag_meta = shr_ddtag_meta;
	memset(dtag_meta, 0x00, SRAM_IN_DTAG_CNT * sizeof(struct du_meta_fmt));

	//init beforce gc init
	scheduler_init(&evt_l2p_srch0, 0);

	ipc_api_init();
	l2p_be_init();
	ftl_core_init();
	local_item_done(fcore_init);

	misc_modules_init_base_on_srb();

	srb_t *srb = (srb_t *) SRAM_BASE;
	sys_assert(srb->srb_hdr.srb_signature == SRB_SIGNATURE);
	srb->dftb_ftl_sz = spb_total * width_in_dws * sizeof(u32);

	fwdl_init(srb);

#ifdef DUAL_BE
	wait_remote_item_done(be_lite_init);
#endif

	local_item_done(be_init);
	ftl_core_trace(LOG_ALW, 0xeb1d, "BE_init done");

#ifdef SAVE_DDR_CFG		//20200922-Eddie
	//extern ddr_info_t* ddr_info_buf_in_ddr;
	//printk("info_need_update:(0x%x)\n",ddr_info_buf_in_ddr->info_need_update);
	if (ddr_info_buf_in_ddr->info_need_update){
        ddr_info_buf_in_ddr->info_need_update = false;
		ftl_core_trace(LOG_INFO, 0x2701, "DDR info need to update \n");
		//Jerry close for NP-JIRA2
		memcpy(fw_config_main_in_ddr->board.ddr_info, (void*) ddr_info_buf_in_ddr, sizeof(ddr_info_t));	//Copy ddr info from fw_config to ddr_info_buf
		//memprint("main save",fw_config_main_in_ddr,1024);
		FW_CONFIG_Rebuild(fw_config_main_in_ddr);
	}
#endif
    ftl_core_trace(LOG_ALW, 0xd142, "BE done");

}

/*! \cond PRIVATE */
module_init(be_init, z.1);
/*! \endcond */

/*! @} */
