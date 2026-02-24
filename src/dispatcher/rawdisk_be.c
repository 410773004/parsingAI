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
/*! \file
 * @brief Rawdisk support
 *
 * \addtogroup dispatcher
 *
 * \defgroup rawdisk
 * \ingroup dispatcher
 * @{
 * Due to no FTL, we use slc mode full page write, and dynamic/streaming mode
 * for Read & Write to demonstrate performance, it has data integrity.
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "types.h"
#include "string.h"
#include "bf_mgr.h"
#include "btn_export.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "queue.h"
#include "misc.h"
#include "ncl_exports.h"
#include "console.h"
#include "crc32.h"
#include "srb.h"
#include "fw_download.h"
#include "rainier_soc.h"
#include "pmu.h"
#include "ncl_cmd.h"
#include "ncl.h"
#include "helper.h"
#include "ipc_api.h"
#include "rawdisk_mpc.h"
#include "cbf.h"

/*! \cond PRIVATE */
#define __FILEID__ rawbe
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define RAWDISK_E2E                z.1	///< module init order


//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
share_data struct nand_info_t shr_nand_info;
share_data volatile u8 shr_nr_ch_in_ncb[2];
share_data volatile void *shr_dummy_meta;
share_data void *shr_dtag_meta;
share_data volatile bool enable_rawdisk_defect_scan;
share_data volatile pda_t btag2pda[BTN_CMD_CNT];
share_data volatile u16 nvm_cmd_id[BTN_CMD_CNT];
share_data volatile bool rawdisk_tlc_mode;
share_data volatile pda_t *shr_l2p;
share_data volatile enum rd_type_t rrd_type;

typedef struct _fast_ncl_cmd_t {
	struct ncl_cmd_t ncl_cmd;
	pda_t pda;
	bm_pl_t bm_pl;
	struct info_param_t info;
} fast_ncl_cmd_t;

typedef struct _ncl_sched_t {
	QSIMPLEQ_HEAD(ncl_cmd_list, ncl_cmd_t) list;
	u32 act;
} ncl_sched_t;

fast_data pool_t fast_cmds_pool;
fast_data fast_ncl_cmd_t fast_cmds[BTN_CMD_CNT];
fast_data ncl_sched_t *dies;
CBF(u16, 32) fast_cmd_pending;

fast_code static inline u32 pda2die(pda_t pda)
{
	pda >>= DU_CNT_SHIFT;
	pda &= nand_info.lun_num - 1;
	return (u32) pda;
}

fast_code void rawdisk_4k_read(int btag)
{
	fast_ncl_cmd_t *cmd = pool_get_ex(&fast_cmds_pool);

	if (cmd == NULL) {
		bool ret;

		CBF_INS(&fast_cmd_pending, ret, btag);
		sys_assert(ret == true);
		return;
	}

	pda_t pda = btag2pda[btag];
	u32 t;

	cmd->bm_pl.pl.nvm_cmd_id = nvm_cmd_id[btag];
	cmd->pda = pda;
	cmd->ncl_cmd.status = 0;

	t = pda2die(pda);
	cmd->ncl_cmd.caller_priv = (void *) t;
	if (dies[t].act >= 2) {
		QSIMPLEQ_INSERT_TAIL(&dies[t].list, &cmd->ncl_cmd, entry);
	} else {
		ncl_cmd_submit(&cmd->ncl_cmd);
		dies[t].act++;
	}
}

fast_code void fast_cmd_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	u32 t;

	ncl_cmd->flags = ((!rawdisk_tlc_mode) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG) |
			NCL_CMD_TAG_EXT_FLAG;
	if (ncl_cmd->status != 0)
		panic("todo");

	t = (u32) ncl_cmd->caller_priv;

	pool_put_ex(&fast_cmds_pool, ncl_cmd);

	if (!QSIMPLEQ_EMPTY(&dies[t].list)) {
		struct ncl_cmd_t *ncl_cmd = QSIMPLEQ_FIRST(&dies[t].list);

		QSIMPLEQ_REMOVE_HEAD(&dies[t].list, entry);
		ncl_cmd_submit(ncl_cmd);
	} else {
		dies[t].act--;
	}

	if (!CBF_EMPTY(&fast_cmd_pending)) {
		u16 btag;

		CBF_FETCH(&fast_cmd_pending, btag);
		rawdisk_4k_read((int)btag);
	}
}

fast_code void fast_cmds_reinit(void)
{
	u32 i;

	pool_init(&fast_cmds_pool, (void *) &fast_cmds[0], sizeof(fast_cmds), sizeof(fast_cmds[0]), BTN_CMD_CNT);

	for (i = 0; i < BTN_CMD_CNT; i++) {
		struct ncl_cmd_t *ncl_cmd = &fast_cmds[i].ncl_cmd;

		ncl_cmd->status = 0;
		ncl_cmd->op_code = NCL_CMD_OP_READ;
		ncl_cmd->flags =
			((!rawdisk_tlc_mode) ? NCL_CMD_SLC_PB_TYPE_FLAG : NCL_CMD_XLC_PB_TYPE_FLAG) |
			NCL_CMD_TAG_EXT_FLAG;

		ncl_cmd->op_type = NCL_CMD_FW_DATA_READ_STREAMING;

		ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;
		ncl_cmd->caller_priv = NULL;

		ncl_cmd->completion = fast_cmd_cmpl;
		ncl_cmd->user_tag_list = &fast_cmds[i].bm_pl;
		fast_cmds[i].bm_pl.pl.btag = i;
		fast_cmds[i].bm_pl.pl.du_ofst = 0;
		fast_cmds[i].bm_pl.pl.dtag = READ_DUMMY_IDX;
		fast_cmds[i].bm_pl.pl.type_ctrl = META_SRAM_IDX;
		memset(&fast_cmds[i].info, 0, sizeof(fast_cmds[i].info));
		fast_cmds[i].info.pb_type = rawdisk_tlc_mode ? NAL_PB_TYPE_XLC : NAL_PB_TYPE_SLC;

		ncl_cmd->addr_param.common_param.list_len = 1;
		ncl_cmd->addr_param.common_param.pda_list = &fast_cmds[i].pda;
		ncl_cmd->addr_param.common_param.info_list = &fast_cmds[i].info;
	}
}

init_code void fast_cmds_init(void)
{
	u32 i;

	dies = sys_malloc(FAST_DATA, sizeof(ncl_sched_t) * nand_info.lun_num);
	sys_assert(dies);

	CBF_INIT(&fast_cmd_pending);

	for (i = 0; i < nand_info.lun_num; i++) {
		QSIMPLEQ_INIT(&dies[i].list);
		dies[i].act = 0;
	}

	fast_cmds_reinit();
}


fast_data u32 wr_credit = 0;
/*!
 * @brief continue to get free dtag and push to free write queue
 *
 * @return		not used
 */
void handle_wr_credit(void);

/*!
 * @brief ipc handler for adding write credit
 *
 * @param req		ipc request
 *
 * @return		not used
 */
fast_code void wr_credit_handler(volatile cpu_msg_req_t *req)
{
	wr_credit += req->pl;
	//disp_apl_trace(LOG_ERR, 0, "rev %d %d\n", wr_credit, req->pl);
	handle_wr_credit();
}

fast_code void handle_wr_credit(void)
{
	dtag_t dtags[32];
	u32 cnt;

	do {
		u32 c;

		cnt = min(32, wr_credit);
		c = dtag_get_bulk(RAWDISK_DTAG_TYPE, cnt, dtags);
		if (c != 0) {
			wr_credit -= c;
			bm_free_wr_load(dtags, c);
			if (c == cnt)
				continue;
		}

		break;
	} while (wr_credit);
}

/*!
 * @brief ipc handler for adding reference count of dtag
 *
 * @param req		ipc request
 *
 * @return		not used
 */
fast_code void ref_cnt_adder(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = { .dtag = req->pl };

	dtag_ref_inc(DTAG_T_SRAM, dtag);
}

/*!
 * @brief ipc handler to return dtag from remote cpu
 *
 * @brief req		ipc request
 *
 * @return		not used
 */
fast_code void remote_dtag_put(volatile cpu_msg_req_t *req)
{
	dtag_t dtag = { .dtag = req->pl };

	dtag_put(RAWDISK_DTAG_TYPE, dtag);
}

/*!
 * @brief common free queue updated event handler function
 *
 * common free queue for NVMe read transfer or NCL write done, recycle dtags.
 *
 * @param param		not used
 * @param payload	should be dtag list
 * @param count		length of dtag list
 *
 * @return		None
 */
fast_code UNUSED static void rawdisk_com_free_updt(u32 param, u32 payload, u32 count)
{
	cpu_msg_isr();
	dtag_put_bulk(RAWDISK_DTAG_TYPE, count, (dtag_t *) payload);
	if (wr_credit)
		handle_wr_credit();
}


/*!
 * @brief rawdisk suspend function
 *
 * @param mode	sleep mode
 *
 * @return	always true
 */
ps_code bool rawdisk_suspend(enum sleep_mode_t mode)
{
	return true;
}

/*!
 * @brief rawdisk resume function
 *
 * @param mode	sleep mode
 *
 * @return	not used
 */
ps_code void rawdisk_resume(enum sleep_mode_t mode)
{
}

init_code void misc_modules_init_base_on_srb(void)
{
	srb_t *srb = (srb_t *) SRB_HD_ADDR;
	dtag_t srb_dtag;

	srb_dtag = _inv_dtag;
	if (srb->srb_hdr.srb_signature != SRB_SIGNATURE ||
		(srb->srb_hdr.srb_csum != crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum)))) {
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

fast_code void ipc_rawdisk_4k_read(volatile cpu_msg_req_t *req)
{
	int btag = (int) req->pl;

	rawdisk_4k_read(btag);
}

/*!
 * @brief Rawdisk initialization
 *
 * Setup rawdisk parameters. Register all buffer manager event handler
 * functions.
 *
 * @return	None
 */
static void init_code UNUSED rawdisk_init_be(void)
{
	UNUSED u32 alloc;
	wait_remote_item_done(ddr_init);

	shr_nand_info = nand_info;
	if (enable_rawdisk_defect_scan == false)
		misc_modules_init_base_on_srb();

	evt_register(rawdisk_com_free_updt, 0, &evt_com_free_upt);
	cpu_msg_register(CPU_MSG_RAWDISK_4K_READ, ipc_rawdisk_4k_read);
	cpu_msg_register(CPU_MSG_WR_CREDIT, wr_credit_handler);
	cpu_msg_register(CPU_MSG_ADD_REF, ref_cnt_adder);
	cpu_msg_register(CPU_MSG_DTAG_PUT, remote_dtag_put);

	fast_cmds_init();
	sys_assert(shr_dummy_meta);
	sys_assert(shr_dtag_meta);
	ncl_set_meta_base(shr_dummy_meta, META_IDX_SRAM_BASE);
	ncl_set_meta_base(shr_dtag_meta, META_DTAG_SRAM_BASE);

	pmu_register_handler(SUSPEND_COOKIE_FTL, rawdisk_suspend,
				RESUME_COOKIE_FTL, rawdisk_resume);

	ipc_api_init();
	local_item_done(be_init);
	return;
}

module_init(rawdisk_init_be, RAWDISK_E2E);

fast_data u32 ttl = 1 * 1024 * 1024;
fast_data u64 rr_start;
fast_data u64 rr_end;

UNUSED fast_code static void rr_perf_cmpl(struct ncl_cmd_t *ncl_cmd)
{
	if (ttl) {
		ncl_cmd_submit(ncl_cmd);
		ttl--;
	}

	extern u32 fcmd_outstanding_cnt;
	if (ttl == 0 && fcmd_outstanding_cnt == 0) {
		rr_end = get_tsc_64();
		u32 ret = (u32) (rr_end - rr_start);
		disp_apl_trace(LOG_ERR, 0x2ead, "%d\n", ret);
		fast_cmds_reinit();
	}
}

static fast_code int rr_perf_main(int argc, char *argv[])
{
	u32 i;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, NULL);

	ttl = 1 * 1024 * 1024;

#define ABCD 128
	for (i = 0; i < ABCD; i++) {
		fast_cmds[i].bm_pl.pl.btag = 0;
		fast_cmds[i].pda = shr_l2p[i * 8];
		fast_cmds[i].ncl_cmd.status = 0;
		//fast_cmds[i].ncl_cmd.flags |= NCL_CMD_SYNC_FLAG;
		fast_cmds[i].bm_pl.pl.dtag = dtag.dtag;
		fast_cmds[i].bm_pl.pl.type_ctrl |= BTN_NCB_QID_TYPE_CTRL_DROP;
		fast_cmds[i].ncl_cmd.op_type = NCL_CMD_FW_DATA_READ_PA_DTAG;

		fast_cmds[i].ncl_cmd.caller_priv = NULL;
		fast_cmds[i].ncl_cmd.completion = rr_perf_cmpl;
	}

	rr_start = get_tsc_64();
	for (i = 0; i < ABCD; i++) {
		ncl_cmd_submit(&fast_cmds[i].ncl_cmd);
		ttl--;
	}

	return 0;
}
static DEFINE_UART_CMD(rr_perf, "rr_perf", "rr_perf", "rr_perf: help misc rawdisk information", 0, 0, rr_perf_main);

/*! @} */
