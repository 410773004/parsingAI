//-----------------------------------------------------------------------------
//                 Copyright(c) SSSTC
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from SSSTC.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from SSSTC.
//-----------------------------------------------------------------------------

#if !defined(PROGRAMMER)
#include "ssstc_cmd.h"
#include "nvme_spec.h"//young add
//#include "admin_cmd.h"
#include "bf_mgr.h"
#include "pcie_core_register.h"
#include "pcie_wrapper_register.h"
//#include "ncl_exports.h"
//#include "aging.h"
#include "event.h"
#include "nvme_cfg.h"
//#include "ndcu.h"
//#include "ficu.h"
#include "srb.h"

#include "ddr_info.h"
#include "fwconfig.h"
#include "ncl.h"
#include "epm.h"
#include "mpc.h"
#include "evt_trace_log.h"
#include "evlog.h"
#include "l2p_mgr.h"
#include "../../../ftl/ErrorHandle.h"	// FET, PfmtHdlGL
#include "lib.h"
#include "dtag.h"
#include "ddr_top_register.h"
#include "ftl_export.h"
#include "ipc_api.h"
#include "smb_registers.h"
#include "types.h"

#ifdef TCG_NAND_BACKUP
#include "tcgcommon.h"
#include "tcg_nf_mid.h"
#include "tcgtbl.h"

#endif

#define __FILEID__ ssstcc
#include "trace.h"
#include "mc_config.h"

#include "ddr.h"

#define SECURITY_VU



//link_capabilities_reg_t  *pcie_reg_status = (link_capabilities_reg_t *)(PCIE_CORE_BASE + LINK_CAPABILITIES_REG);
//share_data u32 shr_NorID;
//share_data u32 Dram_tag;
share_data volatile u16 Vsc_on = 0xEE;
share_data DebugLogHeader_t *DebugLogHeader; //pochune
share_data volatile u8 Vu_temp;
share_data AGING_TEST_MAP_t *MPIN;
share_data volatile stNOR_HEADER *InfoHeader;
share_data volatile AgingPlistBitmap_t *AgingPlistBitmap;
share_data volatile AgingPlistTable_t *AgingP1listTable;
share_data volatile AgingPlistTable_t *AgingP2listTable;
share_data CTQ_t *Aging_CTQ;
share_data volatile AgingPlistBitmap_t *AgingPlistBitmapbkup;
share_data_zi volatile u8 dumplog_flag;//young add 20210730 for lock get log
#ifdef ERRHANDLE_ECCT
extern volatile stECC_table *pECC_table; //tony 20201030
share_data stECCT_ipc_t *vsc_ecct_data = NULL;

#if(SPOR_L2P_VC_CHK == mENABLE)
share_data volatile u8 shr_flag_vac_compare;
share_data volatile u8 shr_flag_vac_compare_result;
#endif

share_data_zi volatile u16 shr_uart_dis;
share_data_zi volatile bool fgdumplogpi;
share_data_zi cache_VU_FLAG VU_FLAG;
extern u32 _max_capacity;

extern u32 LEDparam;

extern stECCT_ipc_t rc_ecct_info[MAX_RC_REG_ECCT_CNT];
extern u8 rc_ecct_cnt;
#endif
#ifdef SAVE_DDR_CFG //20201008-Eddie
//extern u8 need_save_to_CTQ;
extern ddr_info_t *ddr_info_buf_in_ddr;
extern fw_config_set_t *fw_config_main_in_ddr;
extern bool ncl_enter_mr_mode(void);
extern void ncl_leave_mr_mode(void);
#endif
//extern  CTQ_t *Aging_CTQ;
extern NormalTest_Map *NormalTest;
extern struct nand_info_t shr_nand_info;

extern epm_info_t *shr_epm_info; //tony 20201030

#ifdef ERRHANDLE_GLIST
extern sGLTable *pGList;		 //albert 20201103  // GL_mod, Paul_20201130
#endif
extern read_only_t read_only_flags;
extern u8 cur_ro_status;
extern void cmd_proc_read_only_setting(u8 setting);

slow_data Ec_Table *EcTbl = NULL;
slow_data Vc_Table *VcTbl = NULL;

slow_data bool dump_log = false;

slow_data bool fgec_header = false;
//slow_data req_t *ec_req;
//slow_data struct nvme_cmd *ec_cmd;
//slow_data u16 ec_byte;
//slow_data tVSC_pl *Vsc_ec_pl = NULL;	// Did not allocate space? Paul_20210522
slow_data tVSC_pl Vsc_ec_pl;


extern char ldr_ver[5];

extern int pcie_rx_eye(u32 lane_num, u32 op_mode, int *x1, int *x2, int *x3, int *x4);
extern int pcie_rx_margin(u32 lane_num, u32 step, u32 *x1, u32 *x2, u32 *y1, u32 *y2);

extern u8 evt_cmd_done;

/*!
 * @brief swipc to cpu2(BE)
 *
 * @param param		VSC function define, 2 parameters
 * @param payload
 *
 * @return		None
 */
#if 1
static ddr_code enum cmd_rslt_t nvmet_send_vuevt_to_be(u8 cookie, u32 r0, u32 r1)//joe slow->ddr 20201124
{
	ipc_evt_t *ptr = (ipc_evt_t *)sys_malloc(SLOW_DATA, sizeof(ipc_evt_t));
	ptr->evt_opc = cookie;
	ptr->r0 = r0;
	ptr->r1 = r1;
	//_GENE_20200714 TEMP USE
	//nvme_apl_trace(LOG_ERR, 0, "erase_nand_flash(admin.c ipc)\n");
	cpu_msg_sync_start();
	if (cookie == VSC_Refresh_SysInfo) {
		cpu_msg_issue(CPU_BE - 1, CPU_MSG_EVT_VUCMD_SEND_CPU2, 0, (u32)ptr);
	} else {
		cpu_msg_issue(3, CPU_MSG_EVT_VUCMD_SEND, 0, (u32)ptr);
	}
	cpu_msg_sync_end();
	sys_free(SLOW_DATA, (void *)ptr);
	return HANDLE_RESULT_DATA_XFER;
}
#endif

#if 0 //call back albert 20200617,return dtag whlie after read transfer

static fast_code void Vu_callback(void *payload, bool error)
{
	req_t *req = (req_t *) payload;

	nvme_apl_trace(LOG_ERR, 0x059f, "req(%x) xfered sts=(Vucmd)", req);
	sys_assert(error == false);
	sys_assert(req->req_prp.fetch_prp_list == false);
	nvme_apl_trace(LOG_ERR, 0x8a48, "12345");
	/* Exactly 4K for misc data (ctrlr or others) of identify */
	if (++req->req_prp.transferred == req->req_prp.required)
	{
		dtag_t *dtag = (dtag_t *)req->req_prp.mem;
		dtag_put(DTAG_T_SRAM, dtag[0]);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_cs(evt_cmd_done, (u32)req, 0, CS_NOW);
	}
}
#else

static ddr_code void Vu_callback(void *payload, bool error)//joe slow->ddr 20201124
{
	req_t *req = (req_t *)payload;

	sys_assert(error == false);
	if (req->req_prp.fetch_prp_list == true)
	{
		void *mem = NULL;
		dtag_t *dtag;
		int xfer, ofst = 0, cur = 0, sz = 0, i = 0;
		u32 len = req->req_prp.size;
		prp_t *prp = req->req_prp.prp_list;
		req_prp_t *rprp = &req->req_prp;
		prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];

		req->req_prp.fetch_prp_list = false;
		req->req_prp.transferred = 0;
		req->req_prp.required = 0;

		while (len != 0)
		{
			u32 trans_len = min(len, ctrlr->page_size);

			prp_entry->prp = *prp;
			prp_entry->size = trans_len;
			prp_entry++;
			prp++;

			req->req_prp.nprp++;
			len -= trans_len;
		}
		sys_free(SLOW_DATA, req->req_prp.prp_list);

		/* trigger data transfer */
		dtag = req->req_prp.mem;
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			prp_entry_t *prp = &req->req_prp.prp[i];
			int prp_ofst = 0;
			int len = prp->size;
		again:
			if (sz == 0)
			{
				mem = dtag2mem(dtag[cur++]);
				ofst = 0;
				sz = DTAG_SZE;
			}

			xfer = min(len, sz);

			req->req_prp.required++;
			hal_nvmet_data_xfer(prp->prp + prp_ofst,
								ptr_inc(mem, ofst),
								xfer, WRITE, (void *)req, Vu_callback);

			ofst += xfer;
			prp_ofst += xfer;
			sz -= xfer;
			len -= xfer;

			if (len != 0)
			{
				goto again;
			}
		}
//        nvme_apl_trace(LOG_ERR, 0, "transfer done");

		return;
	}

	if (++req->req_prp.transferred == req->req_prp.required)
	{

		dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, (dtag_t *)req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}

static ddr_code void Write_sysinfo_callback(void *payload, bool error)//joe slow->ddr 20201124
{
	req_t *req = (req_t *)payload;
	//nvme_apl_trace(LOG_ERR, 0, "VU sys write\n");
	sys_assert(error == false);
	if (req->req_prp.fetch_prp_list == true)
	{
		void *mem = NULL;
		dtag_t *dtag;
		int xfer, ofst = 0, cur = 0, sz = 0, i = 0;
		u32 len = req->req_prp.size;
		prp_t *prp = req->req_prp.prp_list;
		req_prp_t *rprp = &req->req_prp;
		prp_entry_t *prp_entry = &rprp->prp[rprp->nprp];

		req->req_prp.fetch_prp_list = false;
		req->req_prp.transferred = 0;
		req->req_prp.required = 0;

		while (len != 0)
		{
			u32 trans_len = min(len, ctrlr->page_size);

			prp_entry->prp = *prp;
			prp_entry->size = trans_len;
			prp_entry++;
			prp++;

			req->req_prp.nprp++;
			len -= trans_len;
		}
		sys_free(SLOW_DATA, req->req_prp.prp_list);

		/* trigger data transfer */
		dtag = req->req_prp.mem;
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			prp_entry_t *prp = &req->req_prp.prp[i];
			int prp_ofst = 0;
			int len = prp->size;
		again:
			if (sz == 0)
			{
				mem = dtag2mem(dtag[cur++]);
				ofst = 0;
				sz = DTAG_SZE;
			}

			xfer = min(len, sz);

			req->req_prp.required++;
			hal_nvmet_data_xfer(prp->prp + prp_ofst,
								ptr_inc(mem, ofst),
								xfer, READ, (void *)req, Write_sysinfo_callback);

			ofst += xfer;
			prp_ofst += xfer;
			sz -= xfer;
			len -= xfer;

			if (len != 0)
			{
				goto again;
			}
		}

		return;
	}

	if (++req->req_prp.transferred == req->req_prp.required)
	{
		dtag_t *dtag;
		struct nvme_cmd *cmd = req->host_cmd;
		dtag = req->req_prp.mem;
		void *write_SysInfo = NULL;

		write_SysInfo = dtag2mem(dtag[0]);
		//nvme_apl_trace(LOG_ERR, 0, "cdw13:%x,cdw14:%x\n",cmd->cdw13,cmd->cdw14);
		memcpy((void *)(InfoHeader) + cmd->cdw13, write_SysInfo, cmd->cdw14);
#if defined(MPC)
#if (PI_SUPPORT)
		extern u8 cmf_idx;
		if((cmf_idx == 1) || (cmf_idx == 2))
			nvmet_send_vuevt_to_be(VSC_Refresh_SysInfo, 0, 0);
		else
			nvme_apl_trace(LOG_ERR, 0x1d60, "current du format fail to write system info");
#else
		nvmet_send_vuevt_to_be(VSC_Refresh_SysInfo, 0, 0);
#endif
#else
//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
#endif
		dtag_put_bulk(DTAG_T_SRAM, req->req_prp.mem_sz, (dtag_t *)req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.mem);
		sys_free(SLOW_DATA, req->req_prp.prp);
		evt_set_imt(evt_cmd_done, (u32)req, 0);
	}
}

#endif //call back albert 20200617

#if 1 //transfer data from dram to sram dtag add by suda

/*
 * mem refer to addr of fist dtag addr
 * ddr is Dram data addr
 */
static ddr_code void CP_from_D2S(void *mem, u32 cnt, u32 *ddr)//joe slow->ddr 20201124
{
	dtag_t *dtag;
	u32 dtag_cnt;
	u32 i;
	u32 *memory;
	u32 *base = ddr;

	dtag = (dtag_t *)mem;
	dtag_cnt = occupied_by(cnt, DTAG_SZE);

	for (i = 0; i < dtag_cnt; i++)
	{
		memory = dtag2mem(dtag[i]);
		if (cnt < DTAG_SZE)
		{
			memcpy(memory, base, cnt);
			break;
		}
		memcpy(memory, ddr, DTAG_SZE);
		base += DTAG_SZE;
		cnt -= DTAG_SZE;
	}
}

#endif

#if 0
/*!
 * @brief Capacitor health test
 *
 * @param param		status,
 * @param sts
 *
 * @return		discharge time
 * @author		Maurice Ma
 */
#define POWERIC_ADDRESS (0xB4) //(0x5A<<1)
#define CONTROL_REG_ADDR (0x00)
#define LSC_PARAMETER_REG_ADDR (0x01)
#define DC_DC_CONVERTER_REG_ADDR (0x02)
#define VBUCK_REG_ADDR (0x03)
#define VDISONE_REG_ADDR (0x04)
#define VDISTWO_REG_ADDR (0x05)
#define FREQUENCY_CAPACITANCY_REG_ADDR (0x06)
#define IIC_TDISCHARGE_HIGH_ADDR (0x07)
#define IIC_TDISCHARGE_LOW_ADDR (0x08)
#define IIC_ESR_CONDITION_ADDR (0x09)
extern slow_code u32 I2C_read(u8 slaveID, u8 cmd_code, u8 *value);
extern slow_code u32 I2C_write(u8 slaveID, u8 cmd_code, u8 value);

static enum cmd_rslt_t cap_check(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	u8 data, ESRRegData, TDischageH, TDischageL;
	cap_test_init();//1.Power ic init. 2.disable GPIO interrupt
	nvme_apl_trace(LOG_ERR, 0x39b4, "cap test");
	I2C_read(POWERIC_ADDRESS,DC_DC_CONVERTER_REG_ADDR,&data);
	nvme_apl_trace(LOG_ERR, 0x5d95, "Peak Current %x",data);
	data =| 1;

	nvme_apl_trace(LOG_ERR, 0x3df4, "[VU]Trigger PLP Test");
	I2C_write(POWERIC_ADDRESS, VDISONE_REG_ADDR, TEST_ESR_HIGH_30, 1);
    I2C_write(POWERIC_ADDRESS, VDISTWO_REG_ADDR, (1500/15), 1); //15V
	//////trigger
	I2C_read(POWERIC_ADDRESS, FREQUENCY_CAPACITANCY_REG_ADDR, &data);
    data &= ~B_5;
    I2C_write(POWERIC_ADDRESS, FREQUENCY_CAPACITANCY_REG_ADDR, Data, 1);
    data |= B_5;
    I2C_write(POWERIC_ADDRESS, FREQUENCY_CAPACITANCY_REG_ADDR, Data, 1);
	//////
	//wait discharge done
	/////
	I2C_read(POWERIC_ADDRESS, IIC_ESR_CONDITION_ADDR, &ESRRegData);
    if(ESRRegData & 0x80)//bit7
    {
        //Fail
    }
    else
    {
		I2C_read(POWERIC_ADDRESS, IIC_TDISCHARGE_HIGH_ADDR, &TDischageH);
		I2C_read(POWERIC_ADDRESS, IIC_TDISCHARGE_LOW_ADDR, &TDischageL);
    }
}
#endif

//pochune
/*
static u16 get_header_log(u8 seq_num){
     return DebugLogHeader->log_seq[seq_num].all;
}
*/


//pochune
#if 1
ddr_code static enum cmd_rslt_t get_header(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	DebugLogHeader_t* getDebugLogHeader;
	u32 ofst = 0;
	u32 transfer =	sizeof(*getDebugLogHeader) > bytes ?  sizeof(*getDebugLogHeader) : bytes ;
	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getDebugLogHeader = dtag2mem(dtag[0]);
	//getCTQ = dtag2mem(dtag[0]);

	memcpy(getDebugLogHeader, DebugLogHeader, sizeof(DebugLogHeader_t));
	handle_result = nvmet_map_admin_prp(req, cmd, transfer,Vu_callback); //_GENE_20200714 TEMP USE

	if (handle_result != HANDLE_RESULT_FAILURE) {
		nvme_apl_trace(LOG_ERR, 0x434d, "SHOW req_prp.nprp %d:",req->req_prp.nprp);
		for (i = 0; i < req->req_prp.nprp; i++) {
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getDebugLogHeader + ofst,
			req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
			//nvme_apl_trace(LOG_ERR, 0, "--prp 0x%x:",req->req_prp.prp[i].prp);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;

}
#endif

ddr_code static enum cmd_rslt_t read_otp(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
    u32 *chk_otp;
    
	u32 ofst = 0;
	u32 transfer =	sizeof(*chk_otp) > bytes ?  sizeof(*chk_otp) : bytes ;
	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	bool read_entire_otp  = (cmd->cdw13 == 0x1234ABCD)? true:false;
    extern u32 _read_otp_data(u32 offset);
	//Jerry: read the entire otp
	if(read_entire_otp){
	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	chk_otp = dtag2mem(dtag[0]);
	//Jerry: print OTP
	u32 otp_value=0,reg_idx;
	for(reg_idx=0;reg_idx<0x200;reg_idx+=4)//0x100~0x2FC
	{
		otp_value = _read_otp_data(0x100+reg_idx);
		evlog_printk(LOG_ALW, "otp_value[0x%x]:0x%x\n",0x100+reg_idx, otp_value);
		memcpy(chk_otp+reg_idx/sizeof(u32), &otp_value, sizeof(u32));
	}
	}
	else{	//PDline check otp already program or not(except offset 0x103 PCIe speed, Security enable mode and Public Key Digest #1 #2)
  u32 chk_result;
    _read_otp_data(0x100);

    u32 otp_value=0,reg_idx;
    bool otp_chk_1 = 0,otp_chk_2 =0; // if otp not meet our result ,otp_chk=1
    otp_value = _read_otp_data(0x100);
    if(otp_value != 0x0fffffff && otp_value != 0xffffffff) otp_chk_1 = 1;
    for(reg_idx=4;reg_idx<0x200;reg_idx+=4) //0x104~0x2FC
    {
			if(reg_idx==0x1C || (reg_idx>=0x60 && reg_idx<0xA0)) //SKIP Security enable mode and Public Key Digest #1 #2
				continue;
        otp_value = _read_otp_data(0x100+reg_idx);
        if(otp_value != 0xffffffff)
        {
            otp_chk_2 = 1;
            break;
        }
    }
    if(otp_chk_1 || otp_chk_2) chk_result = 0xFF; //FAIL
    else chk_result = 0x00; //PASS
	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	chk_otp = dtag2mem(dtag[0]);
	memset(chk_otp, chk_result, 1); //DebugLogHeader chk result
		evlog_printk(LOG_ALW, "chk_result %x", chk_result);
	}
	handle_result = nvmet_map_admin_prp(req, cmd, transfer,Vu_callback); //_GENE_20200714 TEMP USE

	if (handle_result != HANDLE_RESULT_FAILURE) {
		nvme_apl_trace(LOG_ERR, 0xa54b, "SHOW req_prp.nprp %d:",req->req_prp.nprp);
		for (i = 0; i < req->req_prp.nprp; i++) {
			req->req_prp.required++;
			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)chk_otp + ofst,
			req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

//pochune
#if 1
ddr_code static enum cmd_rslt_t get_CTQ(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
    dtag_t *dtag;
	CTQ_t *getCTQ;
	u32 ofst = 0;
	u32 transfer =	sizeof(*getCTQ) > bytes ?  sizeof(*getCTQ) : bytes ;
    int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;

	getCTQ = dtag2mem(dtag[0]);
	/*
#if defined(MPC)
		nvmet_send_vuevt_to_be(VSC_Read_CTQ, 0, 0);
#else
	    nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
#endif
*/
	memcpy(getCTQ, Aging_CTQ, sizeof(CTQ_t));

	handle_result = nvmet_map_admin_prp(req, cmd, transfer,Vu_callback); //_GENE_20200714 TEMP USE

	if (handle_result != HANDLE_RESULT_FAILURE) {
		for (i = 0; i < req->req_prp.nprp; i++) {
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getCTQ + ofst,
			req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;

}
#endif

static ddr_code enum cmd_rslt_t get_flashid(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct flashid_data *getflashid;
	u32 ofst = 0;
	u32 transfer = sizeof(*getflashid) > bytes ? bytes : sizeof(*getflashid);
	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;

	getflashid = dtag2mem(dtag[0]);
	memset(getflashid, 0, sizeof(*getflashid));

	getflashid->total_ch = shr_nand_info.geo.nr_channels;
	getflashid->total_dev = shr_nand_info.geo.nr_targets;
//TODO: Fill in the correct information
#if defined(MPC)
	int num = 0;
	tVSC_DW13_Mode dw13;
	dw13.all = cmd->cdw13;
	for (dw13.b.ch = 0; dw13.b.ch < shr_nand_info.geo.nr_channels; dw13.b.ch++)
	{
		for (dw13.b.ce = 0; dw13.b.ce < shr_nand_info.geo.nr_targets; dw13.b.ce++)
		{

			//getflashid->flashinfo[num].flashID[0] = MPIN->NandFlashID[0];
			//getflashid->flashinfo[num].flashID[1] = MPIN->NandFlashID[1];
			//getflashid->flashinfo[num].flashID[2] = MPIN->NandFlashID[2];
			//getflashid->flashinfo[num].flashID[3] = MPIN->NandFlashID[3];
			//getflashid->flashinfo[num].flashID[4] = MPIN->NandFlashID[4];
			//getflashid->flashinfo[num].flashID[5] = MPIN->NandFlashID[5];
			memcpy(getflashid->flashinfo[num].flashID, shr_nand_info.id, 6);
			getflashid->flashinfo[num].channel = dw13.b.ch;
			getflashid->flashinfo[num].device = dw13.b.ce;
			num++;
			//nvme_apl_trace(LOG_ERR, 0, "flashID0: %x ,CE:%d,CH:%d,NUM : %d\n",shr_flashID[0],dw13.b.ce,dw13.b.ch,num + 1);
		}
	}
#else
	//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback); //_GENE_20200714

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getflashid + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

/*static enum cmd_rslt_t get_SensorTemp(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	u32 *getTemp;
	u32 ofst = 0;
	u32 transfer =	sizeof(*getTemp) > bytes ?  sizeof(*getTemp) : bytes ;

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getTemp = dtag2mem(dtag[0]);

	memset(getTemp, 0, sizeof(*getTemp));
	//TODO: Fill in the correct information
	#if defined(MPC)
		nvmet_send_vuevt_to_be(VSC_Read_Temp, 0, 0);
    #else
		 nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
    #endif
	*getTemp = AgingTest->sensor_tempture_val;


	nvme_apl_trace(LOG_ERR, 0, "temp : %x\n",*getTemp);
	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE) {
		for (i = 0; i < req->req_prp.nprp; i++) {
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, getTemp + ofst,
			req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;

}*/

init_code void Fake_table_init(void)
{

	//-------------------------------------ECT---------------------------------------------//
	u32 ec_dtag_cnt = occupied_by((sizeof(Ec_Table)), DTAG_SZE);
	EcTbl = (Ec_Table *)ddtag2mem(ddr_dtag_register(ec_dtag_cnt));
	shr_ec_tbl_addr = (u32)EcTbl;
	//nvme_apl_trace(LOG_ERR, 0, "EcTbl: 0x%x, shr_ec_tbl_addr: 0x%x", EcTbl, shr_ec_tbl_addr);
	memset(EcTbl, 0x0, sizeof(Ec_Table));
	EcTbl->header.tag = 0x42544345;

	//-------------------------------------VCT---------------------------------------------//
	u32 vc_dtag_cnt = occupied_by((sizeof(Vc_Table)), DTAG_SZE);
	VcTbl = (Vc_Table *)ddtag2mem(ddr_dtag_register(vc_dtag_cnt));
	memset(VcTbl, 0x0, sizeof(Vc_Table));
	VcTbl->header.tag = 0x42544356;

}

static ddr_code enum cmd_rslt_t get_Tempture(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	u32 *getTemp;
	u32 ofst = 0;
    u32 cnt = 0;
	u32 transfer = sizeof(*getTemp) > bytes ? bytes : sizeof(*getTemp);
    extern smb_registers_regs_t *smb_mas;

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;
	//tVSC_DW13_Mode dw13;
	//dw13.all = cmd->cdw13;
	u32 dw14 = cmd->cdw14;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getTemp = dtag2mem(dtag[0]);
	memset(getTemp, 0, sizeof(*getTemp));
//TODO: Fill in the correct information
#if defined(MPC)
    extern u8 Detect_FLAG;
    if(Detect_FLAG) {
        extern void GetSensorTemp();
        while(readl(&smb_mas->smb_intr_sts))
		{
			mdelay(10);
			if(++cnt>10)
			{
				cnt=0;
				nvme_apl_trace(LOG_ALW, 0xbf48, "smb_intr_sts: 0x%x",readl((void*)0xC0053000));
				break;
			}
		}
        GetSensorTemp();
    }
	Vu_temp = 0;
	switch (dw14)
	{
	case 0:
	case 1:
		//nvmet_send_vuevt_to_be(VSC_Read_Tempture, dw13.all, dw14);
#ifdef MDOT2_SUPPORT
		*getTemp = smb_tmp102_read(0x90 + (dw14<<2), 0x91 + (dw14<<2)) - 273;
#else
		*getTemp = smb_tmp102_read(0x92 + (dw14<<2), 0x93 + (dw14<<2)) - 273;
#endif
		u8 cnt = 0;
		while (*getTemp == 0)
		{
#ifdef MDOT2_SUPPORT
			*getTemp = smb_tmp102_read(0x90 + (dw14<<2), 0x91 + (dw14<<2)) - 273;
#else
			*getTemp = smb_tmp102_read(0x92 + (dw14<<2), 0x93 + (dw14<<2)) - 273;
#endif
			cnt++;
			if (cnt > 5)
			{
				*getTemp = 37;
				//nvme_apl_trace(LOG_ERR, 0, "default temp returen \n");
				break;
			}
		}
		//nvme_apl_trace(LOG_ERR, 0, "SensorTemp : %d\n",*getTemp);
		break;

//	case 1:
//		nvmet_send_vuevt_to_be(VSC_Read_Tempture, dw13.all, dw14);
//		*getTemp = Vu_temp - 42;
//		//*getTemp = smb_tmp102_read() - 273;
//		//nvme_apl_trace(LOG_ERR, 0, "NandTemp : %d\n",*getTemp);
//		break;

	case 2:
		//nvmet_send_vuevt_to_be(VSC_Read_Tempture, dw13.all, dw14);
		*getTemp = ts_get();
		//nvme_apl_trace(LOG_ERR, 0, "SocTemp : %d\n",*getTemp);
		break;

	default:
		//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
		break;
	}
#else
	//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
#endif

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getTemp + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}

ddr_code enum cmd_rslt_t read_SOC_UID(req_t *req, struct nvme_cmd *cmd, u16 bytes) // 20210224 Jamie slow_code -> ddr_code
{
	dtag_t *dtag;
	u32 *getSOC_UID;
	u32 ofst = 0;
	u32 transfer = sizeof(u64);

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getSOC_UID = dtag2mem(dtag[0]);

	memset(getSOC_UID, 0, sizeof(u64));
	//TODO: Fill in the correct information
    get_soc_uid(getSOC_UID, (getSOC_UID+1));
	//printk("UNID_INFO[0] : 0x%x", *getSOC_UID);
	//printk("UNID_INFO[1] : 0x%x", *(getSOC_UID+1));
	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getSOC_UID + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}

static ddr_code enum cmd_rslt_t get_FWcfg(req_t *req, struct nvme_cmd *cmd, u16 bytes) //Albert for FWcfg flush in20200708
{
	dtag_t *dtag;
	struct FW_Config *getFWcfg;
	u32 ofst = 0;
	u32 transfer = sizeof(*getFWcfg) > bytes ? bytes : sizeof(*getFWcfg);

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getFWcfg = dtag2mem(dtag[0]);

	memset(getFWcfg, 0, sizeof(*getFWcfg));
	memset(getFWcfg, 0x20, 14);
	//TODO: Fill in the correct information
	getFWcfg->SSSTC_flag[0] = 'S';
	getFWcfg->SSSTC_flag[1] = 'S';
	getFWcfg->SSSTC_flag[2] = 'S';
	getFWcfg->SSSTC_flag[3] = 'T';
	getFWcfg->SSSTC_flag[4] = 'C';

	getFWcfg->NVME_flag[0] = 'N';
	getFWcfg->NVME_flag[1] = 'V';
	getFWcfg->NVME_flag[2] = 'M';
	getFWcfg->NVME_flag[3] = 'e';

	getFWcfg->VSC_flag[1] = 'V';
	getFWcfg->VSC_flag[2] = 'S';
	getFWcfg->VSC_flag[3] = 'C';

	getFWcfg->ASIC_Nickname[0] = 'R';
	getFWcfg->ASIC_Nickname[1] = 'A';
	getFWcfg->ASIC_Nickname[2] = 'I';
	getFWcfg->ASIC_Nickname[3] = 'N';
	getFWcfg->ASIC_Nickname[4] = 'I';
	getFWcfg->ASIC_Nickname[5] = 'E';
	getFWcfg->ASIC_Nickname[6] = 'R';
	getFWcfg->ASIC_Nickname[7] = ' ';
	getFWcfg->ASIC_Nickname[8] = 'D';
	getFWcfg->ASIC_Nickname[9] = 'C';

	getFWcfg->ASIC_Rev[0] = 'R';

	memcpy(getFWcfg->Aging_flag, "Aging", 6);
	memcpy(getFWcfg->FATAL_ERROR_flag, "Fatal Error", 12);
	memcpy(getFWcfg->Cluster_flag, "Cluster", 8);
	memcpy(getFWcfg->PCBA_SN, MPIN->pcba_serial_number, 16);
	memcpy(getFWcfg->Drive_SN, MPIN->drive_serial_number, 20);
	//todo fill crect info

	getFWcfg->CH_CNT = shr_nand_info.geo.nr_channels;;
	getFWcfg->CE_CNT = shr_nand_info.geo.nr_targets;
	getFWcfg->DIE_CNT = getFWcfg->CH_CNT*getFWcfg->CE_CNT*shr_nand_info.geo.nr_luns;
	getFWcfg->LAA_Size = 4096;
	getFWcfg->Write_Plane = shr_nand_info.geo.nr_planes;
	getFWcfg->Read_Plane = shr_nand_info.geo.nr_planes;
	getFWcfg->page_size = 18336;
	getFWcfg->page_cnt = shr_nand_info.geo.nr_pages;
	getFWcfg->write_size = 16;


	getFWcfg->DRAM_flag[0] = 'D';
	getFWcfg->DRAM_flag[1] = 'R';
	getFWcfg->DRAM_flag[2] = 'A';
	getFWcfg->DRAM_flag[3] = 'M';

	getFWcfg->ddr_size = ddr_capacity>>20;
	getFWcfg->cs = MC_MAX_CS_NUM;
	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getFWcfg + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}

static ddr_code enum cmd_rslt_t get_FWmodel(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct FWmodel_data *getFWmodel;
	u32 ofst = 0;
	u32 transfer = sizeof(*getFWmodel) > bytes ? bytes : sizeof(*getFWmodel);

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getFWmodel = dtag2mem(dtag[0]);

	memset(getFWmodel, 0, sizeof(*getFWmodel));

	memcpy(getFWmodel->internal_modelname, FR, strlen(FR) >= 8 ? 8 : strlen(FR));
	// getFWmodel->sub_modelname = '0';//0

	//getFWmodel->internal_modelname[0] = 'E';
	//getFWmodel->internal_modelname[1] = '4';
	//getFWmodel->internal_modelname[2] = 'M';
	//getFWmodel->internal_modelname[3] = 'A';
	//getFWmodel->internal_modelname[4] = 'P';
	//getFWmodel->internal_modelname[5] = 'Z';
	//getFWmodel->internal_modelname[6] = 'Z';
	//getFWmodel->sub_modelname         = '0';  //E4MAPZZ0

	//nvme_apl_trace(LOG_ERR, 0, "MODELNAME0 : %x\n",getFWmodel->internal_modelname[0]);
	//nvme_apl_trace(LOG_ERR, 0, "MODELNAMEsub : %x\n",getFWmodel->sub_modelname);
	//nvme_apl_trace(LOG_ERR, 0, "transfer : %x\n",transfer);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getFWmodel + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}
static ddr_code enum cmd_rslt_t get_PCIE_STATUS(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct PCIE_Status *getPCIEstatus;
	u32 ofst = 0;
	u32 transfer = sizeof(*getPCIEstatus) > bytes ? bytes : sizeof(*getPCIEstatus);
	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;
	pcie_core_status2_t link_status;
	link_status.all = readl((void *)(PCIE_WRAP_BASE + PCIE_CORE_STATUS2));
	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getPCIEstatus = dtag2mem(dtag[0]);
	//nvme_apl_trace(LOG_ERR, 0, "555: %d\n",AgingTest->FakeBlankCnt);
	memset(getPCIEstatus, 0, sizeof(*getPCIEstatus));
	//nvme_apl_trace(LOG_ERR, 0, "speed:%d,width:%d transfer byte :%d\n",link_status.b.neg_link_speed, link_status.b.neg_link_width,transfer);
	//TODO: Fill in the correct information
	getPCIEstatus->GEN = link_status.b.neg_link_speed; //wait for FW
	getPCIEstatus->LANES = link_status.b.neg_link_width;

	//nvme_apl_trace(LOG_ERR, 0, "GEN : %x\n",getPCIEstatus->GEN);
	//nvme_apl_trace(LOG_ERR, 0, "LANES : %x\n",getPCIEstatus->LANES);
	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getPCIEstatus + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}

/*
static slow_code enum cmd_rslt_t get_4Byte(req_t *req, struct nvme_cmd *cmd, u16 bytes, u32 data_4B)
{
	dtag_t *dtag;
	u32 *get4;
	u32 ofst = 0;
	u32 transfer =	sizeof(*get4) > bytes ?  bytes : sizeof(*get4) ;

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	get4 = dtag2mem(dtag[0]);

	memset(get4, 0, sizeof(*get4));
	//TODO: Fill in the correct information

	*get4 = data_4B;


	nvme_apl_trace(LOG_ERR, 0, "data4B : %x\n",*get4);
	handle_result = nvmet_map_admin_prp(req, cmd, transfer,Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE) {
		for (i = 0; i < req->req_prp.nprp; i++) {
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, get4 + ofst,
			req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;

}
*/
static ddr_code enum cmd_rslt_t get_PCIE_ID(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	dtag_t *dtag;
	struct PCIE_ID *getPCIEID;
	u32 ofst = 0;
	u32 transfer = sizeof(*getPCIEID) > bytes ? bytes : sizeof(*getPCIEID);

	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	getPCIEID = dtag2mem(dtag[0]);

	memset(getPCIEID, 0, sizeof(*getPCIEID));
	//TODO: Fill in the correct information
	//getPCIEID->all = misc_otp_read(0x114);
	getPCIEID->b.pDID = DID;
	getPCIEID->b.pVID = VID;
	getPCIEID->b.sub_VID = SSVID;
	getPCIEID->b.sub_ID = SSDID;
	//nvme_apl_trace(LOG_ERR, 0, "PCIE_DID : %x\n",getPCIEID->b.pDID);
	//nvme_apl_trace(LOG_ERR, 0, "PCIE_VID : %x\n",getPCIEID->b.pVID);
	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)getPCIEID + ofst,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}

static inline bool is_ptr_sharable(void *_ptr)
{
	u32 ptr = (u32)_ptr;

	if (ptr >= SRAM_BASE && ptr < (SRAM_BASE + SRAM_SIZE))
		return true;

	if (ptr >= BTCM_SH_BASE && ptr < (BTCM_SH_BASE + BTCM_SH_SIZE))
		return true;

	return false;
}

ddr_code void Get_EC_Table(u32 flags, u32 vu_sm_pl)
{
	void *ptr = (void*)vu_sm_pl;

	if (!is_ptr_sharable(ptr))
		ptr = tcm_local_to_share(ptr);

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_GET_NEW_EC, flags, (u32)ptr);
}

static ddr_code  enum cmd_rslt_t Read_EC_Table(req_t *req, struct nvme_cmd *cmd, u16 byte) //add by suda//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read EC Table\n");
	u32 offset;
	u32 transfer;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	transfer = byte; // vendor input
	//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
	u32 *src;
	// TODO: fill up with correct EC Table addr
	switch (cmd->cdw14)
	{
	case Read_Payload: //0x00
		src = (void *)EcTbl->EcCnt + offset;
		if (offset >= PayLoadSize)
			return HANDLE_RESULT_FAILURE;
		break;
	case Read_header: //0x01
		if(!fgec_header)
		{
			fgec_header = true;
			#if 0	// Paul_20210522
			Vsc_ec_pl->ec_req = req;
			Vsc_ec_pl->ec_cmd = cmd;
			Vsc_ec_pl->ec_byte = byte;
//			ec_req = req;
//			ec_cmd = cmd;
//			ec_byte = byte;
			Get_EC_Table(1, (u32)Vsc_ec_pl);
			#else
			Vsc_ec_pl.ec_req = req;
			Vsc_ec_pl.ec_cmd = cmd;
			Vsc_ec_pl.ec_byte = byte;

			Get_EC_Table(1, (u32)&Vsc_ec_pl);
			#endif

			return HANDLE_RESULT_DATA_XFER;
		}

		fgec_header = false;
		src = (void *)(&EcTbl->header) + offset;
		if (offset >= HeaderSize)
			return HANDLE_RESULT_FAILURE;
			//nvme_apl_trace(LOG_ERR, 0, "out of header range\n");
		break;
	default:
		//nvme_apl_trace(LOG_ERR, 0, "Wrong input\n");
		return handle_result;
	};
	EcTbl->header.PayLoadLen = shr_nand_info.geo.nr_blocks *  2;
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read EC Table get base addr\n");
	nvmet_alloc_admin_res(req, transfer); //alloc sram
	CP_from_D2S(req->req_prp.mem, transfer, src);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			mem += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}


ddr_code void ipc_get_new_ec_done(volatile cpu_msg_req_t *msg_req)
{
	tVSC_pl *vsc_ec_pl = (tVSC_pl *)msg_req->pl;

	if (is_ptr_tcm_share((void*)vsc_ec_pl))
		vsc_ec_pl = (tVSC_pl*)tcm_share_to_local((void*)vsc_ec_pl);
	Read_EC_Table(vsc_ec_pl->ec_req, vsc_ec_pl->ec_cmd, vsc_ec_pl->ec_byte);
}

ddr_code enum cmd_rslt_t Read_Bootcode(req_t *req, struct nvme_cmd *cmd)
{ //add by suda
	BootCode *boot;
	enum cmd_rslt_t handle_result;
	u32 transfer = sizeof(struct BootCode);

	//20201020-Eddie

	//nvme_apl_trace(LOG_ERR, 0, "BootCode Ver. : %s\n",ldr_ver);

	nvmet_alloc_admin_res(req, transfer);
	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	boot = (BootCode *)dtag2mem(mem[0]);

	//todo fill in right info
	memcpy(boot->Boot_version, ldr_ver, strlen(ldr_ver) >= 8 ? 8 : strlen(ldr_ver)); //20201020-Eddie
	boot->rsv = 0xFFFFFFFF;
	memcpy(boot->Main_Version1, FR , 7);
	memcpy(boot->Main_Version2, &ctrlr->fw_slot_version[1], 7);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	u32 i;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, boot,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

static ddr_code enum cmd_rslt_t Read_Dram_Tag(req_t *req, struct nvme_cmd *cmd, u16 bytes)//joe slow->ddr 20201124
{ //add by suda
	//nvme_apl_trace(LOG_ERR, 0, "Read_Dram_tag\n");
	u32 offset;
	u32 transfer;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	transfer = bytes; // vendor input
	//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
	u32 *src;
	src = NormalTest->PAYLOAD_BASE + offset;

	nvmet_alloc_admin_res(req, transfer); //alloc sram
	memcpy(req->req_prp.mem, src, transfer);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			mem += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

static ddr_code enum cmd_rslt_t VPD_init(req_t *req, struct nvme_cmd *cmd)
{
	//nvme_apl_trace(LOG_ERR, 0, "VPD_init\n");
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;

	return handle_result;
}

#if 0
static slow_code enum cmd_rslt_t PU_SetTemp(struct nvme_cmd *cmd){
	nvme_apl_trace(LOG_ERR, 0x2945, "PU_SetTemp\n");
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;


	return handle_result;

}

static slow_code enum cmd_rslt_t Disk_Lock(struct nvme_cmd *cmd){
	nvme_apl_trace(LOG_ERR, 0xf57b, "Disk_Lock\n");
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FINISHED;


	return handle_result;

}
#endif
static ddr_code enum cmd_rslt_t PTF_info(req_t *req, struct nvme_cmd *cmd, u16 bytes)//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "PTF_info\n");
	u32 offset;
	u32 transfer;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (req == NULL)
	{
		return handle_result = HANDLE_RESULT_FINISHED;
	}
	else
	{
		transfer = bytes; // vendor input
		//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
		u32 *src;
		src = NormalTest->PAYLOAD_BASE + offset;

		nvmet_alloc_admin_res(req, transfer); //alloc sram
		CP_from_D2S(req->req_prp.mem, transfer, src);

		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

		dtag_t *mem = (dtag_t *)req->req_prp.mem;
		if (handle_result != HANDLE_RESULT_FAILURE)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				mem += req->req_prp.prp[i].size;
			}
		}
		return HANDLE_RESULT_DATA_XFER;
	}
}

static ddr_code enum cmd_rslt_t pcie_rx_eye_info(req_t *req, struct nvme_cmd *cmd, u16 bytes) // 20210224 Jamie slow_code -> ddr_code
{
	dtag_t *dtag;
	struct pcie_rx_eye_info_t *rx_eye = NULL;
	u32 lane_num;
	u32 op_mode;
	u32 transfer;
	u32 i = 0;
	u32 ofst = 0;
	lane_num = cmd->cdw13;
	op_mode = cmd->cdw14;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	int x1[96]; // up right
	int x2[96]; // up left
	int x3[96]; // down left
	int x4[96]; // down right

	if (req == NULL)
	{
		return handle_result = HANDLE_RESULT_FINISHED;
	}
	else
	{
		transfer = sizeof(*rx_eye) >= bytes ? bytes : sizeof(*rx_eye);

		pcie_rx_eye(lane_num, op_mode, &x1[0], &x2[0], &x3[0], &x4[0]);

		nvmet_alloc_admin_res(req, transfer); //alloc sram
		// CP_from_D2S(req->req_prp.mem, transfer, src);
		dtag = req->req_prp.mem;
		rx_eye = dtag2mem(dtag[0]);
		memset(rx_eye, 0, sizeof(*rx_eye));

		for (i = 0; i < 96; i++)
		{
			rx_eye->x1[i] = x1[i];
			rx_eye->x2[i] = x2[i];
			rx_eye->x3[i] = x3[i];
			rx_eye->x4[i] = x4[i];
		}
		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

		// dtag_t *mem = (dtag_t *)req->req_prp.mem;
		if (handle_result != HANDLE_RESULT_FAILURE)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, rx_eye,
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				ofst += req->req_prp.prp[i].size;
			}
		}
		return handle_result;
	}
}

ddr_code void ipc_evlog_dump_ack(volatile cpu_msg_req_t *req)
{
    req_t *cmd_req = tcm_share_to_local((void *)req->pl);
	dtag_t *prp_dtag = (dtag_t *)cmd_req->req_prp.mem;
    cmd_req->host_cmd = tcm_share_to_local(cmd_req->host_cmd);
    struct nvme_cmd *cmd = (struct nvme_cmd *) cmd_req->host_cmd;
	int i = 0;
    u16 nbt_desc = cmd_req->req_prp.data_size;
    enum cmd_rslt_t handle_result = nvmet_map_admin_prp(cmd_req, cmd, nbt_desc, Vu_callback);
	if (HANDLE_RESULT_DATA_XFER == handle_result)
    {
		for (i = 0; i < cmd_req->req_prp.nprp; i++)
    {
			cmd_req->req_prp.required++;
			hal_nvmet_data_xfer(cmd_req->req_prp.prp[i].prp, dtag2mem(prp_dtag[i]), cmd_req->req_prp.prp[i].size, WRITE, (void *) cmd_req, Vu_callback);
    }
}
    if(handle_result == HANDLE_RESULT_FAILURE){
        nvme_apl_trace(LOG_ERR, 0x796d, "cmd fail req:0x%x", cmd_req);
        cmd_req->completion(cmd_req);
    }
}
#define opt_desc 0
#define opt_data  1
#define opt_nor_desc  2
#define opt_nor_data  3
#define opt_save_log  0
#define opt_clear_and_reset_nand_log  1
ddr_code bool checkevtb_page_pda(u32 start, u32 cnt, u16 limit)
{
	int i = 0;
	u32 pda_c;
	for(i = 0; i < limit; i += 4)
	{
		pda_c = get_evtb_page_pda(start, cnt + i / 4);
		if(((pda_c >> shr_nand_info.pda_block_shift) & shr_nand_info.pda_block_mask) != 0)
			return false;
		if(((pda_c >> shr_nand_info.pda_page_shift) & (shr_nand_info.pda_page_mask)) > shr_nand_info.geo.nr_pages)
			return false;
	}
	return true;
}
ddr_code enum cmd_rslt_t nvme_vsc_ev_log(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	u32 mdts = ctrlr->page_size * 8;
    u16 nbt_desc = 0;
    u8 OPC = 0xFF;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
    nvme_apl_trace(LOG_ERR, 0x8ea0, "OP:0x%x,cdw10:0x%x,cdw12:0x%x,cdw13:0x%x,cdw14:0x%x", cmd->opc,cmd->cdw10,cmd->cdw12,cmd->cdw13,cmd->cdw14);
    switch (cmd->opc){
        case NVME_OPC_SET_FEATURES:
        case NVME_OPC_SSSTC_VSC_NONE:
	{
            if(cmd->opc == NVME_OPC_SSSTC_VSC_NONE)
                OPC = cmd->cdw13&0xFF;
            else
                OPC = cmd->cdw10&0xFF;
            if(OPC == opt_save_log)
			flush_to_nand(EVT_NVME_SAVE_LOG);
		    else if(OPC == opt_clear_and_reset_nand_log)
		{
			// evlog_printk(LOG_ERR, "[%s][%d] CPU%d\n", __FUNCTION__, __LINE__, CPU_ID);
			evlog_clear_nand_log_block_and_reset();
		}
		else
			return HANDLE_RESULT_FAILURE;
		return HANDLE_RESULT_FINISHED;
	}
        case NVME_OPC_GET_LOG_PAGE:
        case NVME_OPC_SSSTC_VSC_READ:
	{
            u16 start_page = 0;
            u32 evlog_pda = 0;
            dtag_t *prp_dtag = NULL;
	        int i = 0;
            u32 * mem = NULL;
            if(cmd->opc == NVME_OPC_SSSTC_VSC_READ){
                OPC = cmd->cdw13&0xFF;
                nbt_desc = cmd->cdw10 * 4;
                start_page = (cmd->cdw13>>16)&0xFFFF;
                evlog_pda = cmd->cdw14;
            }else{
                OPC = (cmd->cdw10>>8)&0xFF;
                nbt_desc = bytes;
                start_page = (cmd->cdw12>>16)&0xFFFF;
                evlog_pda = cmd->cdw14;
	}
            if(OPC == opt_data){
                if((VU_FLAG.b.dump_log_flag != 1) || (nbt_desc < ctrlr->page_size) || ( nbt_desc > mdts ) || (nbt_desc % ctrlr->page_size))
                {
                    return HANDLE_RESULT_FAILURE;
                }
                if((start_page >= shr_nand_info.geo.nr_pages)||(start_page + (nbt_desc>>14) > shr_nand_info.geo.nr_pages)){
			return HANDLE_RESULT_FAILURE;
                }
                if(!checkevtb_page_pda(evlog_pda,start_page,nbt_desc/DTAG_SZE)){
			return HANDLE_RESULT_FAILURE;
                }
            }else if(OPC == opt_desc){
                if(nbt_desc < (sizeof(ev_log_desc_t) * 8))
            return HANDLE_RESULT_FAILURE;       //young add 20210730 for lock get log
        		if(nbt_desc > DTAG_SZE)
        		{nbt_desc = DTAG_SZE;
        		}
				VU_FLAG.b.dump_log_flag = 1;
            }else if(OPC == opt_nor_desc){
                nbt_desc = 4;
            }else if(OPC == opt_nor_data){
                if(nbt_desc > DTAG_SZE *4)
			return HANDLE_RESULT_FAILURE;
            }else{
                return HANDLE_RESULT_FAILURE;
	}
            nvmet_alloc_admin_res(req, nbt_desc);
	prp_dtag = (dtag_t *)req->req_prp.mem;
            mem = dtag2mem(prp_dtag[0]);
            switch (OPC){
                case opt_data:
                case opt_desc:
	{
                    void *pl = tcm_local_to_share(req);
                    req->host_cmd = tcm_local_to_share(req->host_cmd);
                    sys_assert(pl);
                    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_EVLOG_DUMP, OPC, (u32) pl);
                    return HANDLE_RESULT_PENDING_BE;
        }
                case opt_nor_desc:
	{
                    u32 nor_log_sig= spi_nor_read(0x0);
                    u32 nor_desc = spi_nor_read(0x10);
                    memset((void *)mem, 0, nbt_desc);
		if((nor_log_sig & 0xFFFFFF) == 0x535049)
		{
                    	*mem = nor_desc;
		}
                    break;
	}
                case opt_nor_data:
	{
        int j = 0;
                    u32 ofst = (cmd->opc == NVME_OPC_SSSTC_VSC_READ)?cmd->cdw14:cmd->cdw12;
                    memset((void *)mem, 0, nbt_desc);
        for(j = 0; j < nbt_desc; j += 4)
        {
                        *(mem + j/4) = spi_nor_read(0x14 + j + ofst*4);
        }
                    break;
	}
                default:
                    return HANDLE_RESULT_FAILURE;
            }
            handle_result = nvmet_map_admin_prp(req, cmd, nbt_desc, Vu_callback);
        	if (HANDLE_RESULT_DATA_XFER == handle_result)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(prp_dtag[i]), req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
        		}
		}
	        return handle_result;
	}
        default:
            return HANDLE_RESULT_FAILURE;
    }
    return HANDLE_RESULT_FAILURE;
}

static slow_code enum cmd_rslt_t Save_Log(req_t *req, struct nvme_cmd *cmd, u16 bytes)
{
	//nvme_apl_trace(LOG_ERR, 0, "save log\n");
	u32 offset;
	u32 transfer;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	if (req == NULL)
	{
		return handle_result = HANDLE_RESULT_FINISHED;
	}
	else
	{
		transfer = bytes; // vendor input
		//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
		u32 *src;
		src = NormalTest->PAYLOAD_BASE + offset;

		nvmet_alloc_admin_res(req, transfer); //alloc sram
		CP_from_D2S(req->req_prp.mem, transfer, src);

		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

		dtag_t *mem = (dtag_t *)req->req_prp.mem;
		if (handle_result != HANDLE_RESULT_FAILURE)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				mem += req->req_prp.prp[i].size;
			}
		}
		return HANDLE_RESULT_DATA_XFER;
	}
}

static ddr_code enum cmd_rslt_t Set_FW_CA(req_t *req, struct nvme_cmd *cmd, u16 bytes)//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "Set_FW_CA\n");
	u32 offset;
	u32 transfer;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	transfer = bytes; // vendor input
	//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
	u32 *src;
	src = NormalTest->PAYLOAD_BASE + offset;

	nvmet_alloc_admin_res(req, transfer); //alloc sram
	CP_from_D2S(req->req_prp.mem, transfer, src);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			mem += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

static ddr_code enum cmd_rslt_t FW_Check_Disable(req_t *req, struct nvme_cmd *cmd, u16 bytes)//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "FW_Check_Disable\n");
	u32 offset;
	u32 transfer;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	transfer = bytes; // vendor input
	//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
	u32 *src;
	src = NormalTest->PAYLOAD_BASE + offset;

	nvmet_alloc_admin_res(req, transfer); //alloc sram
	CP_from_D2S(req->req_prp.mem, transfer, src);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			mem += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

ddr_code enum cmd_rslt_t Read_Glist_Table(req_t *req, struct nvme_cmd *cmd, u16 byte) //add by suda // 20210224 Jamie slow_code -> ddr_code
{
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read Glist Table\n");
	u32 ofst=0;
	u32 transfer=0;
	u32 i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	dtag_t *dtag;
    if(byte == 0)
    {
        return HANDLE_RESULT_FINISHED;        //young add 20210809
    }
#ifdef ERRHANDLE_GLIST
	epm_glist_t *epm_glist_start = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	pGList = (sGLTable *)(&epm_glist_start->data[0]);

	u16 bTotalUsedCnt = 0;
	u16 Idx = 0;
    u16 Idx_off = 0;
	// TODO: fill up with correct Glist Table addr
	if (pGList->dCycle)
	{
		bTotalUsedCnt = GL_TOTAL_ENTRY_CNT;
	}
	else
	{
		bTotalUsedCnt = pGList->wGL_Mark_Cnt;
	}
#endif
    if(cmd->opc == 0xFE){
        ofst = cmd->cdw13;
    }else{
        ofst = cmd->cdw12;
    }
	switch (cmd->cdw14)
	{
	case Read_Payload: //0x00
	{
		Glist_Table *GltTbl;
        if((sizeof(*GltTbl) < ofst)||(cmd->cdw13 != 0)||(ofst&0x3))
        {
            return HANDLE_RESULT_FAILURE;
        }
        Idx_off = ofst>>2;
		transfer = (sizeof(*GltTbl) - ofst) > byte ? byte : (sizeof(*GltTbl) - ofst);
		nvmet_alloc_admin_res(req, transfer);
		dtag = req->req_prp.mem;
		GltTbl = dtag2mem(dtag[0]);
		memset(GltTbl, 0, transfer);
//TODO: Fill in the correct information
#ifdef ERRHANDLE_GLIST
		cpu_msg_issue(CPU_FTL - 1, CPU_MSG_DUMP_GLIST, 0, 0); // Dump GL on log, FET, DumpGLLog
		//cpu_msg_issue(CPU_BE - 1, CPU_MSG_DUMP_ECCT, 0, 0); // Dump ECCT on log.
		rc_ecct_info[rc_ecct_cnt].type = VSC_ECC_dump_table;
		//ECC_Table_Operation(&rc_ecct_info[rc_ecct_cnt]);    // LJ1-426, Tony_20211223
		//rc_ecct_cnt++;
        if(rc_ecct_cnt >= MAX_RC_REG_ECCT_CNT - 1)
        {
            rc_ecct_cnt = 0;
            ECC_Table_Operation(&rc_ecct_info[MAX_RC_REG_ECCT_CNT-1]);
        }
        else
        {
            rc_ecct_cnt++;
            ECC_Table_Operation(&rc_ecct_info[rc_ecct_cnt-1]);
        }


		for (Idx = 0; Idx < bTotalUsedCnt-Idx_off; Idx++)
		{
			GltTbl->Payload_Cnt[Idx].Type = pGList->GlistEntry[Idx+Idx_off].bError_Type;
			GltTbl->Payload_Cnt[Idx].Die = pGList->GlistEntry[Idx+Idx_off].bDie;
			GltTbl->Payload_Cnt[Idx].Blk = pGList->GlistEntry[Idx+Idx_off].wPhyBlk;
		}
#endif

		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);
        ofst = 0;
		if (handle_result == HANDLE_RESULT_DATA_XFER)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)GltTbl + ofst,
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				ofst += req->req_prp.prp[i].size;
			}
		}
		break;
	}
	case Read_header: //0x01
	{
		u16 bDefErCnt = 0;
		u16 bDefWrCnt = 0;
		u16 bDefRdCnt = 0;
		Glist_header *GltTbl;
        if((sizeof(*GltTbl) < ofst)||(cmd->cdw13 != 0)||(ofst&0x3))
        {
            return HANDLE_RESULT_FAILURE;
        }
		transfer = (sizeof(*GltTbl) - ofst) > byte ? byte : (sizeof(*GltTbl) - ofst);
		nvmet_alloc_admin_res(req, sizeof(*GltTbl));// transfer < 4K will not cause issue
		dtag = req->req_prp.mem;
		GltTbl = dtag2mem(dtag[0]);
		memset(GltTbl, 0, sizeof(*GltTbl));
//TODO: Fill in the correct information
#ifdef ERRHANDLE_GLIST

		GltTbl->tag = pGList->dGL_Tag;
		GltTbl->Type = 0x1; //TLC
		GltTbl->Ver = pGList->bGL_Ver;
		GltTbl->PayLoadLen = bTotalUsedCnt * 4;
		for (Idx = 0; Idx < bTotalUsedCnt; Idx++)
		{
			if (pGList->GlistEntry[Idx].bError_Type == GL_PROG_FAIL)
			{
				bDefWrCnt++;
			}
			else if (pGList->GlistEntry[Idx].bError_Type == GL_ERASE_FAIL)
			{
				bDefErCnt++;
			}
			else
			{
				bDefRdCnt++;
			}
		}
		GltTbl->DefErcnt = bDefErCnt;
		GltTbl->DefWrcnt = bDefWrCnt;
		GltTbl->DefRdcnt = bDefRdCnt;

#endif
		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

		if (handle_result == HANDLE_RESULT_DATA_XFER)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)GltTbl + ofst,
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				ofst += req->req_prp.prp[i].size;
			}
		}
		break;
	}
	default:
        break;
	}
		return handle_result;
}




static ddr_code enum cmd_rslt_t VSC_Clear_GList(void)
{
    #if 1   // Paul_20210105
    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_CLR_GLIST, 0, 0);    // Use by VSC only.
    #else   // Albert_20201231
    memset(pGList, 0, (GLIST_EPM_NEED_DTAG * DTAG_SZE));

	pGList->dGL_Tag = GLIST_TAG;
	pGList->bGL_Ver = GLIST_VER;
	pGList->bGL_VerInvert = ~(pGList->bGL_Ver);

	epm_update(GLIST_sign, (CPU_ID-1));
	#endif

    return HANDLE_RESULT_FINISHED;
}

extern sGLEntry errInfo4;
static ddr_code enum cmd_rslt_t VSC_Register_GList(struct nvme_cmd *cmd)	// FET, VSCRegGL
{
	// cdw13: Rsvd[31:24] | ErrType[15:8] | Die[23:16] | Plane[7:0]
	// cdw14: BlkH[31:24] | BlkL[23:16]	  | PgH[15:8]  | PgL[7:0]

	sGLEntry *errInfo = &errInfo4;

    //errInfo->RD_Ftl     = true; // Let it trigger mark bad directly.
    errInfo->bError_Type= (cmd->cdw13 >> 16) & 0xFF;
    errInfo->wPhyBlk    = (cmd->cdw14 >> 16) * shr_nand_info.geo.nr_planes + (cmd->cdw13 & ctz(shr_nand_info.geo.nr_planes));
    errInfo->bDie       = cmd->cdw13 >> 8;
    errInfo->wLBlk_Idx  = errInfo->wPhyBlk / shr_nand_info.geo.nr_planes;
	errInfo->NeedGC	    = (errInfo->bError_Type == GL_PROG_FAIL) ? 1 : 0;	// Verify, DBG, PgFalVry (3)

    nvme_apl_trace(LOG_INFO, 0x5b70, "[EH] VSCRegGL, (Err/D/LB/PB)[%d/%d/%d/%d]", errInfo->bError_Type, errInfo->bDie, errInfo->wLBlk_Idx, errInfo->wPhyBlk);
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_REG_GLIST, 0, (u32)errInfo);

	return HANDLE_RESULT_FINISHED;
}

static ddr_code enum cmd_rslt_t VSC_Preformat_Drop_P2GList(req_t *req)	// FET, RelsP2AndGL
{
	vsc_preformat(req, true);

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_CLR_GLIST, 0, 0);    // Use by VSC only.
    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FRB_DROP_P2GL, 0, 0);    // Use by VSC only.

    return HANDLE_RESULT_FINISHED;
}

static ddr_code enum cmd_rslt_t Read_SI(req_t *req, struct nvme_cmd *cmd) //add by suda//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read SI \n");
	u32 ofset = 0;
	u32 transfer;
	u32 i = 0;
	u32 offset = cmd->cdw13;
	//u32 cnt;
	void *read_SysInfo = NULL;

	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	transfer = cmd->cdw14;
	u8 chk = transfer % 4;
	u8 Duoft = 0;
	if (chk != 0)
	{
		Duoft = 4 - chk;
		transfer += Duoft;
	}
	//cnt = occupied_by(transfer, DTAG_SZE);
	nvmet_alloc_admin_res(req, transfer); //alloc sram
	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	read_SysInfo = dtag2mem(mem[0]);

	u32 *ptr;
	ptr = (void *)InfoHeader + offset;

	memcpy(read_SysInfo, ptr, transfer);
	//CP_from_D2S(mem, transfer, ptr);

	nvme_apl_trace(LOG_ERR, 0x3750, "sysinfo read: cdw10 %x; cdw12 %x; cdw13 %x; cdw14 %x ptr %x", cmd->cdw10, cmd->cdw12, cmd->cdw13, cmd->cdw14, ptr);
	//nvmet_send_vuevt_to_be(VSC_Read_SysInfo, dw13.all, dw14);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	//dtag_t *mem = (dtag_t *)req->req_prp.mem;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, read_SysInfo,
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofset += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

static ddr_code enum cmd_rslt_t write_SysInfo(req_t *req, struct nvme_cmd *cmd) // 20210224 Jamie slow_code -> ddr_code
{
	dtag_t *dtag;
	u32 dw14;
	dw14 = cmd->cdw14;
	void *write_SysInfo = NULL;
	u32 ofst = 0;
	//u32 transfer =	dw14 > bytes ?  dw14 : bytes ;
	u32 transfer = dw14;
	u8 chk = transfer % 4;
	u8 Duoft = 0;
	if (chk != 0)
	{
		Duoft = 4 - chk;
		transfer += Duoft;
	}
	int i = 0;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;
	//nvme_apl_trace(LOG_ERR, 0, "cnt %d\n",transfer);

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;

	write_SysInfo = dtag2mem(dtag[0]);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Write_sysinfo_callback);

	if (handle_result == HANDLE_RESULT_DATA_XFER)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(write_SysInfo, ofst),
								req->req_prp.prp[i].size, READ, (void *)req, Write_sysinfo_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}

ddr_code static enum cmd_rslt_t Read_VC_Table(req_t *req, struct nvme_cmd *cmd, u16 byte) //add by suda//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read VC Table\n");
	u32 ofst = 0;
	u32 offset;
	u32 transfer;
	u32 i = 0, sum = 0;
    VcTbl->header.MaxVc = 0;
    VcTbl->header.AvgVc = 0;
    VcTbl->header.MinVc = INV_U32;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	transfer = byte; // vendor input
	//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);
	//nvme_apl_trace(LOG_INFO, 0, "transfer byte: %x; offset: %x", transfer, offset);
	u32 *vc = sys_malloc_aligned(SLOW_DATA, shr_nand_info.geo.nr_blocks * sizeof(u32), 32);
    u16 cnt = 0;
	memset(vc, 0, shr_nand_info.geo.nr_blocks * sizeof(u32));
	l2p_mgr_vcnt_move(false, vc, shr_nand_info.geo.nr_blocks * sizeof(u32));
	for(i = 0; i < shr_nand_info.geo.nr_blocks; i++) {
        VcTbl->VcCnt[i] = vc[i];
		nvme_apl_trace(LOG_INFO, 0xc7bc, "spb %d vc %d", i, vc[i]);
        sum += VcTbl->VcCnt[i];
        if(VcTbl->header.MaxVc < VcTbl->VcCnt[i])
        {
            VcTbl->header.MaxVc = VcTbl->VcCnt[i];
        }
        if(VcTbl->header.MinVc > VcTbl->VcCnt[i])
        {
            VcTbl->header.MinVc = VcTbl->VcCnt[i];
        }
        cnt++;
	}

    if(cnt == 0)
    {
        VcTbl->header.MinVc = 0;
		VcTbl->header.AvgVc = 0;
    }
	else
	{
    	VcTbl->header.AvgVc = sum / cnt;
	}
	VcTbl->header.PayLoadLen = shr_nand_info.geo.nr_blocks *  4;
    
    nvme_apl_trace(LOG_INFO, 0xb377, "PayLoadLen 0x%x, MaxVc 0x%x AvgVc 0x%x MinVc 0x%x",
        VcTbl->header.PayLoadLen, VcTbl->header.MaxVc, VcTbl->header.AvgVc, VcTbl->header.MinVc);
    
	sys_free_aligned(SLOW_DATA, vc);
	// TODO: fill up with correct EC Table addr
	void *src = NULL;
	nvmet_alloc_admin_res(req, transfer); //alloc sram
	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	src = dtag2mem(mem[0]);

	switch (cmd->cdw14)
	{
		case Read_Payload: //0x00

			memcpy(src, (void *)VcTbl->VcCnt + offset, transfer);
			//nvme_apl_trace(LOG_INFO, 0, "VC1023 %d", VcTbl->VcCnt[1023]);
			break;
		case Read_header: //0x01

			memcpy(src, (void *)(&VcTbl->header) + offset, transfer);

			break;
		default:
			nvme_apl_trace(LOG_ERR, 0xd5fa, "Wrong input\n");
			return handle_result;
	};




	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE) {
		for (i = 0; i < req->req_prp.nprp; i++) {
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(src, ofst),
			req->req_prp.prp[i].size, WRITE, (void *) req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}


	return HANDLE_RESULT_DATA_XFER;
}
ddr_code static enum cmd_rslt_t Nand_Flash_Count(req_t *req, struct nvme_cmd *cmd, u16 byte)//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read NCF \n");
	NFCentry NFCentry;
	u32 offset;
	//u32 transfer;
	u32 i = 0;
	u32 transfer = sizeof(NFCentry) > byte ? byte : sizeof(NFCentry);
	//dw12.all = cmd->cdw12;
	offset = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;

	//transfer = byte;// vendor input
	//nvme_apl_trace(LOG_ERR, 0, "byte(%x)", transfer);

	u32 *src;
	// TODO: fill up with correct EC Table addr
	NFCentry.SLCNFC = 20;
	NFCentry.TCLNFC = 2343;
	NFCentry.TotalNFC = NFCentry.SLCNFC + NFCentry.TCLNFC;

	src = (u32 *)&NFCentry + offset;
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read VC Table get base addr\n");
	nvmet_alloc_admin_res(req, transfer); //alloc sram
	dtag_t *mem = (dtag_t *)req->req_prp.mem;
	memcpy(dtag2mem(mem[0]), src, transfer);
	//CP_from_D2S(req->req_prp.mem, transfer, src);

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	//dtag_t *mem = (dtag_t *)req->req_prp.mem;
	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, dtag2mem(mem[i]),
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			mem += req->req_prp.prp[i].size;
		}
	}
	return HANDLE_RESULT_DATA_XFER;
}
ddr_code static enum cmd_rslt_t Read_ECC_Table(req_t *req, struct nvme_cmd *cmd, u16 byte) //add by suda//joe slow->ddr 20201124
{
	//nvme_apl_trace(LOG_ERR, 0, "VSC Read ECC Table\n");
	u32 ofst;
	u32 transfer;
	int i = 0;
	//tVSC_CMD_Mode dw12;
	//dw12.all = cmd->cdw12;
	ofst = cmd->cdw13;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	// TODO: fill up with correct EC Table addr
	dtag_t *dtag;

#ifdef ERRHANDLE_ECCT
	epm_glist_t *epm_glist_start = (epm_glist_t *)ddtag2mem(shr_epm_info->epm_glist.ddtag);
	pECC_table = (stECC_table *)(&epm_glist_start->data[ECC_START_DATA_CNT]);

#endif

	switch (cmd->cdw14)
	{
	case Read_Payload: //0x00
	{
		ECC_Payload *ECCTbl;

		transfer = (sizeof(*ECCTbl) - ofst) > byte ? byte : (sizeof(*ECCTbl) - ofst);
		nvmet_alloc_admin_res(req, transfer);
		dtag = req->req_prp.mem;
		ECCTbl = dtag2mem(dtag[0]);
		memset(ECCTbl, 0, sizeof(*ECCTbl));
//TODO: Fill in the correct information
#ifdef ERRHANDLE_ECCT
		u32 idx;
		u16 ecc_cnt = 0;
		ECCTbl->cnt1[0] = pECC_table->ecc_table_cnt;
		if (pECC_table->ecc_table_cnt > 1022)
		{
			//nvme_apl_trace(LOG_ERR, 0, "ECCT over ssstc entry threshold");
			ecc_cnt = 1022;
		}
		else
		{
			ecc_cnt = pECC_table->ecc_table_cnt;
		}

		for (idx = 0; idx < ecc_cnt; idx++)
		{
			ECCTbl->cnt[idx].LAA = pECC_table->ecc_entry[idx].err_lda;
			ECCTbl->cnt[idx].BitMap = pECC_table->ecc_entry[idx].bit_map;
		}

#endif

		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

		if (handle_result != HANDLE_RESULT_FAILURE)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)ECCTbl + ofst,
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				ofst += req->req_prp.prp[i].size;
			}
		}
		break;
	}
	case Read_header: //0x01
	{
		ECC_header *ECCTbl;
		transfer = (sizeof(*ECCTbl) - ofst) > byte ? byte : (sizeof(*ECCTbl) - ofst);
		nvmet_alloc_admin_res(req, transfer);
		dtag = req->req_prp.mem;
		ECCTbl = dtag2mem(dtag[0]);
		memset(ECCTbl, 0, sizeof(*ECCTbl));
//TODO: Fill in the correct information
#ifdef ERRHANDLE_ECCT
		ECCTbl->tag = pECC_table->ecc_table_tag;
		ECCTbl->cnt = pECC_table->ecc_table_cnt;
#endif
		handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

		if (handle_result != HANDLE_RESULT_FAILURE)
		{
			for (i = 0; i < req->req_prp.nprp; i++)
			{
				req->req_prp.required++;

				hal_nvmet_data_xfer(req->req_prp.prp[i].prp, (void *)ECCTbl + ofst,
									req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
				ofst += req->req_prp.prp[i].size;
			}
		}
		break;
	}
	default:
	{
		//nvme_apl_trace(LOG_ERR, 0, "Wrong input\n");
		return handle_result;
	}
	}
	return HANDLE_RESULT_DATA_XFER;
};

	/*
	switch(cmd->cdw14){
		case Read_Payload://0x00
			src = (void*)ECCTbl->cnt1 + offset;
			if((transfer + offset - 1) > PayLoadSize )
				return HANDLE_RESULT_FAILURE;
			break;
		case Read_header://0x01
			src = (void*)(&ECCTbl->header) + offset;
			if((transfer + offset - 1) > HeaderSize )
				//return HANDLE_RESULT_FAILURE;
				nvme_apl_trace(LOG_ERR, 0, "out of header range\n");
			break;
		default :
			nvme_apl_trace(LOG_ERR, 0, "Wrong input\n");
			return handle_result;
	};
*/

extern fast_code void ig_pcie_link_retrain(u32 retrain_target_speed, u32 retrain_time_delay);
static ddr_code enum cmd_rslt_t Vu_pcie_retrain (struct nvme_cmd *cmd) // Jack 20220419
{
	u32 target_speed = cmd->cdw13;
	u32 retrain_delay = cmd->cdw14;

	ig_pcie_link_retrain(target_speed, retrain_delay);

	return HANDLE_RESULT_FINISHED;
}

#if 1
extern slow_code u32 pr_write(u8 cmd_code, u16 index, u8 type);
extern fast_code u32 pr_read(u8 cmd_code, u8 *value, u8 type);
extern fast_code void mdelay(u32 ms);

static ddr_code enum cmd_rslt_t Vu_rx_lane_margin(req_t *req, struct nvme_cmd *cmd, u16 byte) // 20210224 Jamie slow_code -> ddr_code
{

	u32 x1 = 0;
	u32 x2 = 0;
	u32 y1 = 0;
	u32 y2 = 0;
	u32 lane_num = 0;
	u32 step = 1;


	rx_lane_margin *rx;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	dtag_t *dtag;
	u32 ofst = 0;
	u32 transfer = sizeof(*rx) > byte ? byte : sizeof(*rx);
	int i = 0;

	nvmet_alloc_admin_res(req, transfer);
	dtag = req->req_prp.mem;
	rx = dtag2mem(dtag[0]);
	memset(rx, 0, sizeof(*rx));

#ifndef MDOT2_SUPPORT
	u16 vol = cmd->cdw14;
#endif
#if 0
	u8 value = 0;
	int cnt = 0;
	while((value == 0) && (cnt<300))
	{
		pr_read(0x0, &value, 0);
		mdelay(10);
		cnt++;
	}
	//nvme_apl_trace(LOG_ERR, 0, "read SY8827: %x, cnt: %d\n",value,cnt);
	if(value)
	{
		pr_write(0x0, vol, 0);
		//nvme_apl_trace(LOG_ERR, 0, "SY8827\n");
	}
	else
	{
		pr_write(0x1, vol, 1);

		//nvme_apl_trace(LOG_ERR, 0, "TPS62864\n");

	}
#endif
#ifndef MDOT2_SUPPORT
    pr_write(0x0, vol, 0);    //Silergy
	pr_write(0x1, vol, 1);    //TI
#endif

	for (lane_num = 0; lane_num < 4; lane_num++)
	{

		pcie_rx_margin(lane_num, step, &x1, &x2, &y1, &y2);

		rx->margin[lane_num].lane_number = lane_num;
		rx->margin[lane_num].xleft = 0x80 - x2;
		rx->margin[lane_num].xright = 0x80 + x1;
		rx->margin[lane_num].yleft = 0x80 - y2;
		rx->margin[lane_num].yright = 0x80 + y1;
	}

	handle_result = nvmet_map_admin_prp(req, cmd, transfer, Vu_callback);

	if (handle_result != HANDLE_RESULT_FAILURE)
	{
		for (i = 0; i < req->req_prp.nprp; i++)
		{
			req->req_prp.required++;

			hal_nvmet_data_xfer(req->req_prp.prp[i].prp, ptr_inc(rx, ofst),
								req->req_prp.prp[i].size, WRITE, (void *)req, Vu_callback);
			ofst += req->req_prp.prp[i].size;
		}
	}

	return HANDLE_RESULT_DATA_XFER;
}
#endif


// DBG, SMARTVry
extern u32 shr_program_fail_count;
extern u32 shr_erase_fail_count;
extern u32 shr_die_fail_count;
extern u16 shr_E2E_RefTag_detection_count;
extern u16 shr_E2E_AppTag_detection_count;
extern u16 shr_E2E_GuardTag_detection_count;
extern u64 shr_nand_bytes_written;
extern volatile u32 GrowPhyDefectCnt; //for SMART growDef Use
#if RAID_SUPPORT_UECC
extern u32 nand_ecc_detection_cnt;   //host + internal 1bit fail detection cnt
extern u32 host_uecc_detection_cnt;  //host 1bit fail detection cnt
extern u32 internal_uecc_detection_cnt; //internal 1bit fail detection cnt
extern u32 uncorrectable_sector_count;  //host raid recovery fail cnt
extern u32 internal_rc_fail_cnt;  //internal raid recovery fail cnt
extern u32 host_prog_fail_cnt;
#endif
extern fast_data_zi corr_err_cnt_t corr_err_cnt;
ddr_code static enum cmd_rslt_t VSC_Clear_SMART(req_t *req, struct nvme_cmd *cmd)//joe slow->ddr 20201124
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	//extern epm_info_t *shr_epm_info;
	//epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	extern smart_statistics_t *smart_stat;
	extern tencnet_smart_statistics_t *tx_smart_stat;
   	u32 mode = cmd->cdw13;
#ifdef SMART_PLP_NOT_DONE
	if(mode == 0x2){
		smart_stat->critical_warning.bits.epm_vac_err = 0;
		epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
		if(epm_smart_data->init_plp_not_flag == 0x89ABCDEF)
		{
			epm_smart_data->init_plp_not_flag = 0xFEDABD21;
			nvme_apl_trace(LOG_ALW, 0x1f4f, "disable plp not done");
		}
		else{
			epm_smart_data->init_plp_not_flag = 0x89ABCDEF;
			nvme_apl_trace(LOG_ALW, 0xe44f, "enable plp not done");
		}
		handle_result = HANDLE_RESULT_FINISHED;
		return handle_result;
	}
#endif

	// CPU1 access var, should be ok to clear directly, DBG, SMARTVry
/*
	shr_program_fail_count 			= 0;
	shr_erase_fail_count 			= 0;
	shr_E2E_GuardTag_detection_count= 0;
	shr_E2E_AppTag_detection_count	= 0;
	shr_E2E_RefTag_detection_count	= 0;
	corr_err_cnt.bad_tlp_cnt 		= 0;
	shr_nand_bytes_written 			= 0;
	#if RAID_SUPPORT_UECC
	host_uecc_detection_cnt 		= 0;
	uncorrectable_sector_count 		= 0;
	nand_ecc_detection_cnt 			= 0;
	internal_rc_fail_cnt 			= 0;
	#endif
	GrowPhyDefectCnt 				= 0;
	shr_die_fail_count 				= 0;
	// CPU3 access var, should be OK clear in VSC.
	extern u32 wl_cnt;
	extern u32 rd_cnt;
	extern u32 dr_cnt;
	extern u32 gc_cnt;
	wl_cnt = 0;
	rd_cnt = 0;
	dr_cnt = 0;
	gc_cnt = 0;
*/

	memset((void*)smart_stat, 0, sizeof(smart_statistics_t));
	//memcpy(epm_smart_data->smart_save, &smart_stat, sizeof(struct _smart_statistics_t));
	memset((void*)tx_smart_stat, 0, sizeof(tencnet_smart_statistics_t));
	//memcpy(epm_smart_data->ex_smart_save, &tx_smart_stat, sizeof(struct _tencnet_smart_statistics_t));
	#if (Synology_case)
	extern synology_smart_statistics_t *synology_smart_stat;
	memset((void*)synology_smart_stat, 0, sizeof(synology_smart_statistics_t));
	#endif

	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	epm_smart_data->hi_epm_nand_bytes_written = 0;
	epm_smart_data->lo_epm_nand_bytes_written = 0;
	shr_nand_bytes_written = 0;

#if CO_SUPPORT_DEVICE_SELF_TEST
	//clear DST
	memset(  epm_smart_data->LogTEL, 0, sizeof(epm_smart_data->LogTEL ) );
	memset(  epm_smart_data->LogPersistent, 0, sizeof(epm_smart_data->LogPersistent ) );
	memset(  epm_smart_data->LogDST, 0, sizeof(epm_smart_data->LogDST ) );

	u8 idx;
	extern tDST_LOG *smDSTInfo;
	for (idx = 0; idx < 20; idx++)
	{
		smDSTInfo->DSTLogEntry[idx].DSTResult = cDSTEntryNotUsed;
	}
	smDSTInfo->Tag = 0x44535400;  //DST0
#endif

	epm_update(SMART_sign, (CPU_ID - 1));
	if(mode == 0)
	{
		extern void spb_clear_ec();
	    spb_clear_ec();
		memset(EcTbl, 0x0, sizeof(Ec_Table));
		EcTbl->header.tag = 0x42544345;
		EcTbl->header.PayLoadLen = 0x1000;
	}
	handle_result = HANDLE_RESULT_FINISHED;
	return handle_result;
}

static inline ddr_code u32 EPM_bitmap_check(u32 *bitmap, u32 index)
{
	return (bitmap[index >> 5] & (1 << (index & 0xff)));
}

extern u8* gl_pt_defect_tbl;
extern u8* aging_pt_defect_tbl;
extern struct nand_info_t shr_nand_info;
ddr_code void chkdef(void)
{
    u16 i = 0;
    u32 value_aging = 0;
	u32 value_frb = 0;

	nvme_apl_trace(LOG_ALW, 0xed52, "chkdef gl_pt_defect_tbl 0x%x, aging_pt_defect_tbl 0x%x \n", gl_pt_defect_tbl, aging_pt_defect_tbl);

	for(i = 1; i < shr_nand_info.geo.nr_blocks ; i++)
	{
		for (u8 k = 0; k < 32; k++)
		{
			if(*(aging_pt_defect_tbl + i*64 + k) !=0)
			{
				for (u8 j = 0; j < 8; j++)
				{
					value_aging = *(aging_pt_defect_tbl + i*64 + k);
					value_frb = *(gl_pt_defect_tbl + i*32 + k);

					if(EPM_bitmap_check((u32*)( aging_pt_defect_tbl + i*64 + k ), j) != 0)
					{
						if(EPM_bitmap_check((u32*)( aging_pt_defect_tbl + i*64 + k ), j) != EPM_bitmap_check((u32*)( gl_pt_defect_tbl + i*32 + k ), j) )
						{
							nvme_apl_trace(LOG_ALW, 0x6722, "chkdef value_aging 0x%x, value_frb 0x%x, SPBCNT %d, Word_idx %d, Bit_idx %d \n", value_aging, value_frb, i, k, j);
						}
					}
				}
			}
		}
	}

	nvme_apl_trace(LOG_ALW, 0x040b, "chkdef Done \n");
}

ddr_code enum cmd_rslt_t VSC_Check_Defect()
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

	chkdef();
	handle_result = HANDLE_RESULT_FINISHED;
	return handle_result;
}


ddr_code void vac_compare(void)
{
    extern volatile u8 stop_gc_done;
    //extern volatile u8 cal_done;
    extern void btn_de_wr_hold(void);
    extern void btn_de_wr_cancel_hold(void);

    btn_de_wr_hold();
    stop_gc_done = 0;
    cpu_msg_issue(CPU_BE - 1, CPU_MSG_STOP_GC_FOR_VAC_CAL, 0, 0);
    while(!stop_gc_done){

	}

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_COMPARE_VAC, 0, 0);


}

#if(SPOR_L2P_VC_CHK == mENABLE)
static ddr_code enum cmd_rslt_t Check_L2P_VAC(void) //add by Sunny
{
    //u8 fail_flag = mFALSE;

    nvme_apl_trace(LOG_INFO, 0xa7e6, "Compare L2P VU Start");

    shr_flag_vac_compare = mFALSE;
	shr_flag_vac_compare_result = mFALSE;
    //cpu_msg_sync_start();
    vac_compare();
    //cpu_msg_issue(CPU_FTL - 1, CPU_MSG_COMPARE_VAC, 0, 0);
    //cpu_msg_sync_end();

    nvme_apl_trace(LOG_INFO, 0x511f, "Compare L2P VU End");

	return HANDLE_RESULT_FINISHED;
}


static ddr_code enum cmd_rslt_t Check_L2P_VAC_RESULT(void) //add by Joseph
{
    if(!shr_flag_vac_compare_result)
    {
        nvme_apl_trace(LOG_INFO, 0xc50f, "VU L2P CMP still running!!");
        return HANDLE_RESULT_FINISHED;
    }
    else
    {
        if(shr_flag_vac_compare)
        {
    		nvme_apl_trace(LOG_INFO, 0x8a88, "VAC mismatch!!");
    		return HANDLE_RESULT_FAILURE;
    	}
    	else
        {
    		nvme_apl_trace(LOG_INFO, 0x14fe, "VAC correct.");
    		return HANDLE_RESULT_FINISHED;
        }
    }
}
#endif
ddr_code static enum cmd_rslt_t VSC_TMT_SET(req_t *req, struct nvme_cmd *cmd)
{
		u32 tmt2 = cmd->cdw14 & 0xFFFF;
		u32 tmt1 = (cmd->cdw14 & 0xFFFF0000) >> 16;
		u32 minv = temp_55C;
		u32 maxv = temp_84C;
		u32 k;
		if ((tmt1 != 0 && (tmt1 < minv || tmt1 > maxv)) || (tmt2 != 0 && (tmt2 < minv || tmt2 > maxv))
			|| (tmt2 != 0 && tmt1 != 0 && tmt1 >= tmt2) || (tmt1 < minv)) 
			{
				return HANDLE_RESULT_FAILURE;
			}

		ctrlr->cur_feat.warn_cri_feat.tmt_warning = tmt1;
		ts_tmt.warning = tmt1 - 273;
		if (cmd->cdw13 == 1)
			ctrlr->saved_feat.warn_cri_feat.tmt_warning = tmt1;

		for (k = 0; k < MAX_TH; k++)
		{

			ctrlr->cur_feat.temp_feat.tmpth[k][OVER_TH] = tmt1;
			if (cmd->cdw13)
				ctrlr->saved_feat.temp_feat.tmpth[k][OVER_TH] = tmt1;

		}

		tmt1 = tmt1 != 0 ? k_deg_to_c_deg(tmt1) : ~0;
		tmt2 = tmt2 != 0 ? k_deg_to_c_deg(tmt2) : ~0;

		ts_tmt_setup(tmt1, tmt2);
		ctrlr->cur_feat.hctm_feat.all = cmd->cdw14;


		if (cmd->cdw13 == 1)
			ctrlr->saved_feat.hctm_feat.all = cmd->cdw14;

		return HANDLE_RESULT_FINISHED;
}


ddr_code static enum cmd_rslt_t VSC_ECC_Table_Operation(req_t *req, struct nvme_cmd *cmd, u32 type)//joe slow->ddr 20201124
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_DATA_XFER;

#ifdef ERRHANDLE_ECCT
	//tVSC_DW13_Mode dw13;
	//u32 dw14;
	//stECCT_ipc_t *ecct_req = (stECCT_ipc_t*)ddtag2mem(ddr_dtag_register(1));
	//stECCT_ipc_t *ecct_req;
	u32 lba = cmd->cdw13;
	u32 total_len = cmd->cdw14;
    dtag_t dtag;

	//ecct_req = (stECCT_ipc_t*)ddtag2mem(ddr_dtag_register(1));
	//nvmet_alloc_admin_res(req, 1); //alloc sram
	//dtag_t *mem = (dtag_t *)req->req_prp.mem;

	dtag_get_bulk(DTAG_T_SRAM, 1, &dtag);

	vsc_ecct_data = (stECCT_ipc_t *)dtag2mem(dtag);
	vsc_ecct_data->lba = lba;
	vsc_ecct_data->total_len = total_len;
	vsc_ecct_data->source = ECC_REG_VU;
	vsc_ecct_data->type = type;

	dtag_put_bulk(DTAG_T_SRAM, 1, &dtag);

	ECC_Table_Operation(vsc_ecct_data);
#endif

	//ECC_Table_Operation(lba, total_len, pda, type);
	handle_result = HANDLE_RESULT_FINISHED;
	return handle_result;
}

static ddr_code enum cmd_rslt_t VSC_Suspend_GC(req_t *req, struct nvme_cmd *cmd)
{
//	#define GC_ACT_SUSPEND		0
//	#define GC_ACT_RESUME		2
	cpu_msg_issue(CPU_BE - 1, CPU_MSG_CONTROL_GC, 0, (u32)cmd->cdw13);
	return HANDLE_RESULT_FINISHED;
}
#if (_TCG_)
static ddr_code enum cmd_rslt_t VSC_Enable_Disable_TCG(req_t *req, struct nvme_cmd *cmd)
{
	extern u32 mTcgStatus;
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	switch (cmd->cdw13)
	{
		case TCG_TAG:
		case NONTCG_TAG:
			if((mTcgStatus & TCG_ACTIVATED) == 0)
			{
				epm_aes_data->tcg_en_dis_tag = cmd->cdw13;
				epm_update(AES_sign, (CPU_ID - 1));
				nvme_apl_trace(LOG_ALW, 0xa53c, "[TCG] en/dis TAG is set: 0x%x", epm_aes_data->tcg_en_dis_tag);
				return HANDLE_RESULT_FINISHED;
			}
		default:
			nvme_apl_trace(LOG_ALW, 0x6f0f, "[TCG] en/dis switch fail, i/p TAG: 0x%x", cmd->cdw13);
			return HANDLE_RESULT_FAILURE;
	}
}

extern void sec_gen_sha3_256_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash);
extern tG1 *pG1;
extern AGING_TEST_MAP_t *MPIN;

//Verify PSID (mp_info & tbl)

static ddr_code enum cmd_rslt_t VSC_Verify_PSID(req_t *req, struct nvme_cmd *cmd)
{	
	u8 i = 0;

	//PSID TAG verify
	if((MPIN->PSID_tag != PLAIN_PSID) && (MPIN->PSID_tag != DIGEST_PSID))
	{
		nvme_apl_trace(LOG_ERR, 0x6d96, "Mp Info PSID Tag verify fail");
		return HANDLE_RESULT_FAILURE;
	}
	else if((pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_Tag != CPIN_IN_DIGEST) && (pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_Tag != CPIN_IN_RAW))
	{
		nvme_apl_trace(LOG_ERR, 0xac44, "Tcg tbl PSID Tag verify fail");
		return HANDLE_RESULT_FAILURE;
	}
#ifdef PSID_PRINT_CHK
	for(i = 0; i<32; i++)
	{
		nvme_apl_trace(LOG_INFO, 0x45d2, "[Max debug]MP_info[%x]|%x", i, MPIN->PSID[i]);
	}
		
	for(i = 0; i<32; i++)
	{
		nvme_apl_trace(LOG_INFO, 0xb54f, "[Max debug]TCG tbl[%x]|%x", i, pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_val[i]);
	}
#endif
	//PSID verify
	for(i=0; i<SHA256_DIGEST_SIZE; i++)
	{		
		if (pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_val[i] != MPIN->PSID[i]) //password compare NG
		{	
			nvme_apl_trace(LOG_ERR, 0x1923, "PSID verify fail | %x", i);
			return HANDLE_RESULT_FAILURE;
		}
	}


	nvme_apl_trace(LOG_INFO, 0xefde, "PSID verify Pass");
	return HANDLE_RESULT_FINISHED;
	
}

#endif
static ddr_code enum cmd_rslt_t VSC_Enable_secure_boot(req_t *req, struct nvme_cmd *cmd)
{
	// OTP check enable/disable & KOTP
	extern u32 _read_otp_data(u32 offset);
	extern int _program_otp_data(u32 data, u32 offset);
	extern void misc_set_otp_deep_stdby_mode(void);

	bool always_ptk        = (cmd->cdw13 == 0x1234ABCD)? true:false;
	bool always_programOTP = (cmd->cdw14 == 0x494E5345)? true:false;
		
	u32 enable = _read_otp_data(0x11C);
	if(enable == 0x494E5345)
	{
		nvme_apl_trace(LOG_INFO, 0xcf12, "Secure Boot Enabled already !!");
		if(always_ptk)
		{
			for(u8 i=0; i<8; i++)
			{
				u32 digest1 = _read_otp_data(0x160+(i<<2));
				u32 digest2 = _read_otp_data(0x180+(i<<2));
				nvme_apl_trace(LOG_INFO, 0x9730, "PubK on OTP: 0x%x, 0x%x", digest1, digest2);
			}
		}
		misc_set_otp_deep_stdby_mode();
		if(always_programOTP)
			return HANDLE_RESULT_FAILURE;
		else
			return HANDLE_RESULT_FINISHED;
	}
	else
	{
		bool non_prog_kotp = true;
		for(u8 i=0; i<8; i++)
		{
			u32 digest1 = _read_otp_data(0x160+(i<<2));
			u32 digest2 = _read_otp_data(0x180+(i<<2));
			if(always_ptk)
				nvme_apl_trace(LOG_INFO, 0xecbf, "KOTP #%d: 0x%x; 0x%x", i, digest1, digest2);
			if((digest1 != 0xFFFFFFFF) || (digest2 != 0xFFFFFFFF))
			{
				nvme_apl_trace(LOG_INFO, 0x1a3a, "KOTP prog already #%d: 0x%x; 0x%x", i, digest1, digest2);
				non_prog_kotp = false;
			}
		}
		if(non_prog_kotp)
		{
			// IPC to read image
			// read first DU of first Page in MR mode
			// read corresponding page/DU in MR mode to get KOTP
			u32 *KOTP = (u32 *)sys_malloc(FAST_DATA, 32);
			u32 *KOTP_sh = tcm_local_to_share((void *)KOTP);
			memset((void *)KOTP, 0x00, 32);
			
			cpu_msg_sync_start();
			cpu_msg_issue(CPU_BE - 1, CPU_MSG_RD_KOTP, 0, (u32)KOTP_sh);
			cpu_msg_sync_end();

			bool key_correct = false;
			for(u8 i=0; i<8; i++)
			{
				nvme_apl_trace(LOG_INFO, 0x083b, "Key digest[%d]: 0x%x", i, KOTP[i]);
				if(KOTP[i]!=0)
					key_correct = true;
			}
			if(key_correct && always_programOTP)
			{
				// OTP program
				nvme_apl_trace(LOG_ALW, 0x2d74, "OTP program for Secure Boot");
				for(u8 i=0; i<8; i++)
				{
					_program_otp_data(*(KOTP+i), 0x160+(i<<2));
					_program_otp_data(*(KOTP+i), 0x180+(i<<2));
				}
				_program_otp_data(0x494E5345, 0x11C);
			}
			
			sys_free(FAST_DATA, (void *)KOTP);
		}
		else
		{
			if(always_programOTP)
				_program_otp_data(0x494E5345, 0x11C);
			nvme_apl_trace(LOG_INFO, 0x91fa, "Secure Boot Disabled BUT KOTP prog already !!");
		}

		misc_set_otp_deep_stdby_mode();
		return HANDLE_RESULT_FINISHED;
	}
}

/*
slow_code static enum cmd_rslt_t plp_check(req_t *req, struct nvme_cmd *cmd, u16 byte)
{
	nvme_apl_trace(LOG_ERR, 0, "plp check\n");
	u32 k = 0;
	gpio_pad_ctrl_t gpio_ctr = {
		.all = readl((void *)(MISC_BASE + GPIO_PAD_CTRL)),
	};
	plperror = false;
	while(((gpio_ctr.b.gpio_in & (1 << GPIO_PLP_STRPG_SHIFT)) == 0) && (k < 5000))
	{

		mdelay(1);
#if CPU_ID ==1
		if(k == 0 || k == 4999)
			nvme_apl_trace(LOG_ERR, 0, "GPIO reg value: %x\n", gpio_ctr.b.gpio_in);
#endif
		gpio_ctr.all = readl((void *)(MISC_BASE + GPIO_PAD_CTRL));
		k++;
		plperror = true;
	}

	if((gpio_ctr.b.gpio_in & (1 << GPIO_PLP_STRPG_SHIFT)) != 0){
		nvme_apl_trace(LOG_ERR, 0, "plp no error condition\n");
		plperror = false;
		}

	return HANDLE_RESULT_FINISHED;
}
alberttemp*/

//static enum cmd_rslt_t Clear_Dram_tag(req_t *req,){

//}


#ifdef VSC_CUSTOMER_ENABLE	// FET, RelsP2AndGL
/*!
 * @brief Vendor F0 command,
 *	 list only the VscFunc/ VscMode released to customer, and the listed cases here should be included in VSCCmd FC/ FD/ FE.
 *
 * @param param		NVMe cmd 16DW
 * @param sts		None
 *
 * @return		cmd_rslt_t
 */
ddr_code enum cmd_rslt_t nvmet_ssstc_vsc_f0cmd(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	tVSC_CMD_Mode dw12;
	dw12.all = cmd->cdw12;
	#ifdef SECURITY_VU

	if(Vsc_on == 0xEE)
	{
		dw12.b.VscFunction = Vsc_NonSuppor;
	}

	#endif

	if (dw12.b.VscFunction == Vsc_DeviceHWCheck) //0x10
	{
		switch (dw12.b.VscMode)
		{

			case VSC_CHECK_LED: //0x22
			{
				u32 temp;
				temp = readl((void *)(MISC_BASE + GPIO_OUT));

				if(cmd->cdw14 == 1)
				{
					LEDparam = 1;
				}
				else if(!cmd->cdw14)
				{
					LEDparam = 0;
					switch(cmd->cdw13)
					{
					    case 0:
						writel((temp & (~(BIT(2 + GPIO_OUT_SHIFT)))) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));
                        break;

	                    case 1:
						writel((temp) | (BIT(2 + GPIO_OUT_SHIFT)) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));
                        break;

                        case 2:
                        writel((temp ^ (BIT(2 + GPIO_OUT_SHIFT))) | (BIT(2)), (void *)(MISC_BASE + GPIO_OUT));
                        break;

                        default:
                        break;
                    }
				}

				handle_result = HANDLE_RESULT_FINISHED;
				break;
			}
			default:
				//nvme_apl_trace(LOG_ERR, 0, "no define or not support\n");
				break;
		}
	}

	else if (dw12.b.VscFunction == Vsc_PurageOperation) //0x13
	{
		bool ns_reset;
		switch (dw12.b.VscMode)
		{
		case VSC_PU_PREFORMAT: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "VSC_PU_PREFORMAT\n");
			ns_reset = false;
			if(dw12.b.Dw12Byte2 == 1)
			{
				ns_reset = true;
			}
			vsc_preformat(req, ns_reset);
			handle_result = HANDLE_RESULT_PENDING_BE;
			break;

		case VSC_PU_PREFORMAT_DROP_P2GL: //0x03, FET, RelsP2AndGL
			VSC_Preformat_Drop_P2GList(req);
			handle_result = HANDLE_RESULT_PENDING_BE;
			break;
		case VSC_GC_CONTROL:	//0x04
			handle_result = VSC_Suspend_GC(req, cmd);
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
#if defined(DRAM_Error_injection)
    else if (dw12.b.VscFunction == Vsc_Error_injection_1bit)
    {
        Error_injection_1bit();
    }
    else if (dw12.b.VscFunction == Vsc_Error_injection_2bit)
    {
        Error_injection_2bit();
    }
#endif
	else
	{
		nvme_apl_trace(LOG_ERR, 0x2344, "no define or not supported\n");
	}

	if(handle_result == HANDLE_RESULT_FAILURE)
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);

	return handle_result;
}
#endif

/*!
 * @brief Vendor FC command
 *
 * @param param		NVMe cmd 16DW
 * @param sts		None
 *
 * @return		cmd_rslt_t
 */
  extern u32 VPD_blk_write(u8 cmd_code, u8* vpd);
extern void plp_set_ENA(u32 param, u32 flag, u32 r1);
extern void plp_read_ENA();

share_data_zi bool sb_update_idx = false;
share_data_zi bool sb_always_update_idx = false;
ddr_code enum cmd_rslt_t nvmet_ssstc_vsc_fccmd(req_t *req, struct nvme_cmd *cmd)//joe slow->ddr 20201124
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	tVSC_CMD_Mode dw12;
	dw12.all = cmd->cdw12;

#if 0
		tVSC_DW13_Mode dw13;
		u32 dw14;
	dw13.all = cmd->cdw13;
		dw14     = cmd->cdw14;
#endif

	nvme_apl_trace(LOG_ERR, 0xb663, "VscFunction =%2x, VscMode =%2x \n", dw12.b.VscFunction, dw12.b.VscMode);

#ifdef SECURITY_VU

	if((Vsc_on == 0xEE) && (dw12.all != 0x3013))
	{
		if(dw12.all == 0x0117)
		{
			if(!VU_FLAG.b.dump_log_en)
			{
				dw12.b.VscFunction = Vsc_NonSuppor;
			}
		}
		else
		{
			dw12.b.VscFunction = Vsc_NonSuppor;
		}
	}



#endif
	if (dw12.b.VscFunction == Vsc_DeviceHWCheck) //0x10
	{
		switch (dw12.b.VscMode)
		{

		case VSC_VPD_INIT: //0x50
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			VPD_blk_write(0, NULL);
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not support\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_PurageOperation) //0x13
	{
		bool ns_reset;
		switch (dw12.b.VscMode)
		{
		case VSC_PU_PREFORMAT: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "VSC_PU_PREFORMAT\n");
			ns_reset = true;
			if(dw12.b.Dw12Byte2 == 1)
			{
				ns_reset = false;
			}
		#ifdef TCG_NAND_BACKUP
			epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
			epm_aes_data->prefmtted = TCG_PFMT_TAG;
			epm_update(AES_sign, (CPU_ID - 1));
		#endif
			vsc_preformat(req, ns_reset);
			handle_result = HANDLE_RESULT_PENDING_BE;
			break;
		case VSC_PU_FOBFORMAT: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_PU_SETTEMP:						//0x02
			handle_result = HANDLE_RESULT_FINISHED; //PU_SetTemp(cmd);
			break;
		case VSC_PU_PREFORMAT_DROP_P2GL: //0x03, FET, RelsP2AndGL
			VSC_Preformat_Drop_P2GList(req);
			handle_result = HANDLE_RESULT_PENDING_BE;
			break;

		case VSC_AG_ERASEALL: //0x11
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			//panic(0);
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_AG_ERASENAND: //0x20
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_DENY_VSC: //0x30
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			if(Vsc_on != 0xEE)
			{
				if(cmd->cdw14 == 1) //Vsc cmd on
				{
					MPIN->Vsc_tag = 0;
				}
				else if(cmd->cdw14 == 0)
				{
					MPIN->Vsc_tag = 0xEE;
				}

				#if defined(HMETA_SIZE)
				extern u8 cmf_idx;
				extern void hal_nvmet_suspend_cmd_fetch(void);
				if (cmf_idx == 3 || cmf_idx == 4) {
					hal_nvmet_suspend_cmd_fetch();    //stop IO when pi enable
					fgdumplogpi = 1;
				}
				#endif

				nvmet_send_vuevt_to_be(VSC_Refresh_SysInfo, 0, 0);

				#if defined(HMETA_SIZE)
				extern void hal_nvmet_enable_cmd_fetch(void);
				if (cmf_idx == 3 || cmf_idx == 4) {
					hal_nvmet_enable_cmd_fetch();     //enable IO when pi enable
					fgdumplogpi = 0;
				}
				#endif
			}
			if(cmd->cdw14 == 2)
			{
				VU_FLAG.b.dump_log_en = 1;
			}
			else if(cmd->cdw14 == 3)
			{
				VU_FLAG.b.dump_log_en = 0;
			}
			handle_result = HANDLE_RESULT_FINISHED;
			break;

		case VSC_UART_DISABLE_FAKE: //0x40
			handle_result = HANDLE_RESULT_FINISHED;
			break;

		case VSC_UART_DISABLE: 		//0x50
			shr_uart_dis = cmd->cdw14;
			evlog_printk(LOG_ALW,"Disable Uart %x", shr_uart_dis);
			MPIN->uart_dis = shr_uart_dis ? 0xA5 : 0;
			nvmet_send_vuevt_to_be(VSC_Refresh_SysInfo, 0, 0);
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_AgingOnlyOperation) //0x14
	{
		switch (dw12.b.VscMode)
		{
		case VSC_ERASE_NANDFLASH: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "erase_nand_flash\n");
			//#if defined(MPC)
			//nvmet_send_vuevt_to_be(VSC_ERASE_NAND, 0, 0);//maurice
			//#else
			//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
			//#endif
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_SCAN_DEFECT: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "scan_defect\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_AGING_BATCH: //0x02
							  //nvme_apl_trace(LOG_ERR, 0, "aging_batch\n");
							  //#if defined(MPC)

			//handle_result = nvmet_send_vuevt_to_be(VSC_AGING_BATCH, dw13.all, dw14);//albert 20200521

			//#else
			//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
			handle_result = HANDLE_RESULT_FINISHED;
			//#endif
			break;
		case VSC_PLIST_OP: //0x03
//nvme_apl_trace(LOG_ERR, 0, "plist_backup\n");
#if 1
			if (cmd->cdw14 == 0xF)
			{
				nvmet_send_vuevt_to_be(VSC_GBBPlus, cmd->cdw13, 0);
				nvmet_send_vuevt_to_be(VSC_Refresh_SysInfo, 0, 0);
			}
			else
			{
				//nvme_apl_trace(LOG_ERR, 0, "cannot support\n");
			}
#else
			//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");

#endif
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_DRAM_DLL: //0x04
			//nvme_apl_trace(LOG_ERR, 0, "dram_dll\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_PROG_OTP: //0x05 _GENE_20210906
            evlog_printk(LOG_ALW,"force prog otp to 0x0fffffff");
            extern int _program_otp_data(u32 data, u32 offset);
            _program_otp_data(0x0fffffff, 0x100);
            misc_set_otp_deep_stdby_mode(); //_GENE_20210928
            handle_result = HANDLE_RESULT_FINISHED;
        break;
		case VSC_CLEAR_DRAM: //0x0A
							 //nvme_apl_trace(LOG_ERR, 0, "dram_Retraining\n");
#ifdef SAVE_DDR_CFG			 //20201008-Eddie
							 //memprint("ddr_info_buf",ddr_info_buf_in_ddr,320);
			ddr_info_buf_in_ddr->cfg.training_done = 0;
			ddr_info_buf_in_ddr->cfg.bkup_fwconfig_done = 0;
			//memprint("ddr_info_buf",ddr_info_buf_in_ddr,320);
			//memprint("fw_config_main_in_ddr",fw_config_main_in_ddr,4096);
			memcpy(fw_config_main_in_ddr->board.ddr_info, (void *)ddr_info_buf_in_ddr, sizeof(ddr_info_t));

			//memprint("main save",fw_config_main,320);
			FW_CONFIG_Rebuild(fw_config_main_in_ddr);

			nvme_apl_trace(LOG_ERR, 0x1ebb, "DDR traning done cleared !! \n");
#endif
			//#if defined(MPC)
			//#ifdef BC_DRAM_TEST
			//	handle_result = nvmet_send_vuevt_to_be(VSC_DRAMtag_Clear, dw13.all, dw14);//albert 20200629

			//#else
			//nvme_apl_trace(LOG_ERR, 0, "can't support DRAM\n");
			handle_result = HANDLE_RESULT_FINISHED;
			//#endif
			//#else
			//nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
			//handle_result = HANDLE_RESULT_FINISHED;
			//#endif

			break;
		case VSC_CHANGE_ODT: // 0x13
			if(cmd->cdw13 == 1 || cmd->cdw13 == 2 || cmd->cdw13 == 4)
			{
			    switch(cmd->cdw14)
		        {
		            case 0:
					    nvme_apl_trace(LOG_ERR, 0x1432, "won't retrain,just change odt \n");
					    break;
		            case 1:
#ifdef SAVE_DDR_CFG			 //20201008-Eddie			 
			            ddr_info_buf_in_ddr->cfg.training_done = 0;
			            ddr_info_buf_in_ddr->cfg.bkup_fwconfig_done = 0;
			            memcpy(fw_config_main_in_ddr->board.ddr_info, (void *)ddr_info_buf_in_ddr, sizeof(ddr_info_t));
			            nvme_apl_trace(LOG_ERR, 0x111e, "DDR traning done cleared !! \n");
#endif
                        break;
		        }
			    fw_config_main_in_ddr->board.odt = cmd->cdw13;
				FW_CONFIG_Rebuild(fw_config_main_in_ddr);
			    nvme_apl_trace(LOG_ERR, 0x1504, "Set DDR ODT 0x%x",cmd->cdw13);
				handle_result = HANDLE_RESULT_FINISHED;
			}
			else
			{
			    nvme_apl_trace(LOG_ERR, 0x220d, "won't support odt: 0x%x",cmd->cdw13);
				handle_result = HANDLE_RESULT_FAILURE;
			}
			break;
		case VSC_SRB_ERASE: //0x0B		20201014-Eddie
			nvme_apl_trace(LOG_ERR, 0x154f, "SRB erase (ch0~ch3 block0)\n");

			erase_srb();
			handle_result = HANDLE_RESULT_FINISHED;
			break;
        case VSC_DRAM_SET_CLK: //0x0C
            /*
			ddr_info_buf_in_ddr->cfg.training_done = 0;
			ddr_info_buf_in_ddr->cfg.bkup_fwconfig_done = 0;
			memcpy(fw_config_main_in_ddr->board.ddr_info, (void *)ddr_info_buf_in_ddr, sizeof(ddr_info_t));
            */
			//memprint("main save",fw_config_main,320);
			switch(cmd->cdw13)
            {
                case 2400:
                    nvme_apl_trace(LOG_INFO, 0x2a97, "Set DDR CLK 2400");
                    fw_config_main_in_ddr->board.ddr_clk = 2;
                    break;
                case 2666:
                    nvme_apl_trace(LOG_INFO, 0xd199, "Set DDR CLK 2666");
                    fw_config_main_in_ddr->board.ddr_clk = 3;
                    break;
                case 3200:
                    nvme_apl_trace(LOG_INFO, 0xed81, "Set DDR CLK 3200");
                    fw_config_main_in_ddr->board.ddr_clk = 4;
                    break;
                default:
                    nvme_apl_trace(LOG_INFO, 0x2afd, "Do nothing");
                    break;
            }
			FW_CONFIG_Rebuild(fw_config_main_in_ddr);


			handle_result = HANDLE_RESULT_FINISHED;
            break;
		case VSC_SET_TMT: //Albert add for TMT settings
		
			handle_result = VSC_TMT_SET(req, cmd);
		
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_FTLRelatedOperation) //0x15
	{
		switch (dw12.b.VscMode)
		{
		case VSC_DISK_LOCK: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED; //handle_result = Disk_Lock(cmd);
			break;
		case VSC_NAND_FLUSH_CNT: //0x08
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_ECC_INSERT: //0x10
			//nvme_apl_trace(LOG_ERR, 0, "ECCT Register\n");

			handle_result = VSC_ECC_Table_Operation(req, cmd, VSC_ECC_reg);
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_ECC_DELETE: //0x11
			//nvme_apl_trace(LOG_ERR, 0, "ECCT unRegister\n");
			handle_result = VSC_ECC_Table_Operation(req, cmd, VSC_ECC_unreg);
			break;
		case VSC_ECC_RESET: //0x12
			//nvme_apl_trace(LOG_ERR, 0, "ECCT reset\n");

			handle_result = VSC_ECC_Table_Operation(req, cmd, VSC_ECC_reset);
			break;
        case VSC_ECC_RC_REG: //0X13    //20210108 tony test
            handle_result = VSC_ECC_Table_Operation(req, cmd, VSC_ECC_rc_reg);
            break;
        case VSC_ECC_DUMP_TABLE: //0X14    //20210118 tony test
            handle_result = VSC_ECC_Table_Operation(req, cmd, VSC_ECC_dump_table);
            break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_InternalTableOperation) //0x16
	{
		switch (dw12.b.VscMode)
		{
		case VSC_CHECK_DEFECT: //0x05
			//nvme_apl_trace(LOG_ALW, 0, "VSC_Check_Defect\n");
			handle_result = VSC_Check_Defect();
			break;

		case VSC_CLEAR_GLIST: //0x10
			//nvme_apl_trace(LOG_ERR, 0, "Clear GList\n");
			handle_result = VSC_Clear_GList();
		    break;

		case VSC_CLEAR_SMART: //0x12
			//nvme_apl_trace(LOG_ERR, 0, "Clear SMART\n");
			handle_result = VSC_Clear_SMART(req, cmd);
			break;

		case VSC_REG_GLIST:   //0x13	// FET, VSCRegGL
			handle_result = VSC_Register_GList(cmd);
			break;

		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_DebugOperation) //0x17
	{
		switch (dw12.b.VscMode)
		{
		case VSC_PTF_INFO: //0x00

			handle_result = PTF_info(NULL, cmd, 0);
			break;
		case VSC_EV_LOG://0x01
				handle_result = nvme_vsc_ev_log(NULL, cmd, 0);
				break;
		case VSC_SAVE_LOG: //0x10

			handle_result = Save_Log(NULL, cmd, 0);
			break;
		case VSC_LOAD_LOG: //0x11
		{
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			/*switch(dw12.b.Dw12Byte3)
				{
					case 0x4: //load CTQ
					#if defined(MPC)
						handle_result = nvmet_send_vuevt_to_be(VSC_Read_CTQ, dw13.all, dw14);
					#else
						nvme_apl_trace(LOG_ERR, 0, "NOT support w/ single CPU\n");
	                    handle_result = HANDLE_RESULT_FINISHED;
					#endif
					break;
					default:
						nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");*/
			handle_result = HANDLE_RESULT_FINISHED;
			//break;
			//}
			break;
		}

#if(SPOR_L2P_VC_CHK == mENABLE)
        case VSC_L2P_VC_CHK:  //0x14
			handle_result = Check_L2P_VAC();
			break;
		case VSC_L2P_VC_CHK_RESULT: //0x15
			handle_result = Check_L2P_VAC_RESULT();
			break;
#endif

        case VSC_SCAN_WRITTEN: //0x20
		{
            cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SCAN_WRITTEN, 0, 0);

			handle_result = HANDLE_RESULT_FINISHED;

			break;
		}

		case VSC_LDR_UPDATE_ALWAYS: //0x30
		{
				sb_always_update_idx = true;
				nvme_apl_trace(LOG_ERR, 0x9ff6, "sb_always_update_idx = %x \n", sb_always_update_idx);
				handle_result = HANDLE_RESULT_FINISHED;

				break;
		}
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_OEMSpecific_1) //0x20
	{
		switch (dw12.b.VscMode)
		{
#if (_TCG_)
		case VSC_EN_DIS_TCG: //0x00
			handle_result = VSC_Enable_Disable_TCG(req, cmd);
			break;
#if TCG_FS_PSID
		case VSC_Verify_Psid: //0x01
			handle_result = VSC_Verify_PSID(req, cmd);
			break;
#endif
#endif
		case VSC_ENABLE_SECURE_BOOT: //0x02
			handle_result = VSC_Enable_secure_boot(req, cmd);
			break;
		case VSC_SET_MTD_01_TO_38: //0x10
			plp_set_ENA(0, 0, 0);
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_READ_MTD_01_TO_38: //0x11
			plp_read_ENA();
			handle_result = HANDLE_RESULT_FINISHED;
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not support\n");
			break;
		}
	}
	else
	{
		nvme_apl_trace(LOG_ERR, 0x4665, "no define or not supported\n");
	}
	//handle_result = HANDLE_RESULT_FINISHED;
	if(handle_result == HANDLE_RESULT_FAILURE)
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);

	return handle_result;
}
/*!
 * @brief Vendor FD command
 *
 * @param param		NVMe cmd 16DW
 * @param sts		None
 *
 * @return		cmd_rslt_t
 */
ddr_code enum cmd_rslt_t nvmet_ssstc_vsc_fdcmd(req_t *req, struct nvme_cmd *cmd)
{
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	tVSC_CMD_Mode dw12;

	dw12.all = cmd->cdw12;
	//nvme_apl_trace(LOG_ERR, 0, "VscFunction =%2x, VscMode =%2x \n",dw12.b.VscFunction,dw12.b.VscMode);

#ifdef SECURITY_VU

		if(Vsc_on == 0xEE)
		{
			dw12.b.VscFunction = Vsc_NonSuppor;
		}

#endif


	if (dw12.b.VscFunction == Vsc_DeviceHWCheck) //0x10
	{
		switch (dw12.b.VscMode)
		{
		case VSC_VPD_WRITE: //0x58
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_SysInfoOperation) //0x12
	{
		switch (dw12.b.VscMode)
		{
		case VSC_SI_WIRTE: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = write_SysInfo(req, cmd);
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_NVMeAlternative) //0x18
	{
		switch (dw12.b.VscMode)
		{
		case VSC_SECURITY_SEND: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_SECURITY_RECEIVE: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else
	{
		nvme_apl_trace(LOG_ERR, 0x738d, "no define or not supported\n");
	}
	//handle_result = HANDLE_RESULT_FINISHED;
	if(handle_result == HANDLE_RESULT_FAILURE)
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);

	return handle_result;
}
/*!
 * @brief Vendor FE command
 *
 * @param param		NVMe cmd 16DW
 * @param sts		None
 *
 * @return		cmd_rslt_t
 */
ddr_code enum cmd_rslt_t nvmet_ssstc_vsc_fecmd(req_t *req, struct nvme_cmd *cmd)
{
	//u16 numd = ((cmd->cdw10 >> 16) & 0x0FFF);
	u16 numd_bytes = cmd->cdw10 << 2;
	enum cmd_rslt_t handle_result = HANDLE_RESULT_FAILURE;
	tVSC_CMD_Mode dw12;
	dw12.all = cmd->cdw12;
	//nvme_apl_trace(LOG_ERR, 0, "VscFunction =%2x, VscMode =%2x \n",dw12.b.VscFunction,dw12.b.VscMode);

#ifdef SECURITY_VU

			if(Vsc_on == 0xEE)
			{
				if((dw12.all == 0x0117) || (dw12.all == 0x0216))
				{
					if(!VU_FLAG.b.dump_log_en)
					{
						dw12.b.VscFunction = Vsc_NonSuppor;
					}
				}
				else
				{
					dw12.b.VscFunction = Vsc_NonSuppor;
				}
			}

#endif

	if (dw12.b.VscFunction == Vsc_DeviceHWCheck) //0x10
	{
		switch (dw12.b.VscMode)
		{
		case VSC_READFLASHID: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = get_flashid(req, cmd, numd_bytes);
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_READNORID: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			//#if defined(MPC)
			//nvmet_send_vuevt_to_be(VSC_Read_NorID, 0, 0);
			//#else
			//nvme_apl_trace(LOG_ERR, 0, "can't support DRAM\n");
			//#endif
			//shr_NorID = spi_read_id();
			//nvme_apl_trace(LOG_ERR, 0, "Nor ID: %d \n", shr_NorID);
			//handle_result = get_4Byte(req, cmd, numd_bytes, shr_NorID);

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_PCIE_CONFIG: //0x10
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = get_PCIE_STATUS(req, cmd, numd_bytes);

			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_PCIE_VID_DID: //0x11
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = get_PCIE_ID(req, cmd, numd_bytes);

			//handle_result = HANDLE_RESULT_FINISHED;
			break;
#if 1
		case VSC_CHECK_PLP: //0x20

			//nvme_apl_trace(LOG_ERR, 0, "\n");
			//handle_result = plp_check(req, cmd, numd_bytes);
			break;
#endif
		case VSC_READ_PCB: //0x30
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_CHECK_SOC: //0x40
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_VPD_INIT: //0x50
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = VPD_init(req, cmd);
			break;
		case VSC_Read_DramTag: //0x51
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			/*
				#if defined(MPC)
					handle_result = HANDLE_RESULT_FINISHED;
					//nvmet_send_vuevt_to_be(VSC_Read_Dram_Tag, 0, 0);
				#else
				//nvme_apl_trace(LOG_ERR, 0, "can't support DRAM\n");
				break;
				#endif
				//nvme_apl_trace(LOG_ERR, 0, "Dram Tag: %d \n", Dram_tag);
				*/
			handle_result = Read_Dram_Tag(req, cmd, numd_bytes);
			break;
		case VSC_VPD_READ: //0x59
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_READ_DRAM_DLL: //0x60
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_CHECK_Temperature: //0x71 Albert 20200629
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = get_Tempture(req, cmd, numd_bytes);
			break;
		case VSC_SOC_UID: //0x73//20201127-Ma
			//printk("\n");
			handle_result = read_SOC_UID(req, cmd, numd_bytes);
			break;

		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not support MODE\n");
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_DeviceFWCheck) //0x11
	{
		switch (dw12.b.VscMode)
		{
		case VSC_READFW_CONFIG: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = get_FWcfg(req, cmd, numd_bytes);
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_READ_FW_MODEL: //0x40
								//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = get_FWmodel(req, cmd, numd_bytes);
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_READ_BOOT_VER: //0x80
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Read_Bootcode(req, cmd);
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_SysInfoOperation) //0x12
	{
		switch (dw12.b.VscMode)
		{
		case VSC_SI_READ: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");
			handle_result = Read_SI(req, cmd);
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_FTLRelatedOperation) //0x15
	{
		switch (dw12.b.VscMode)
		{
		case VSC_DISK_LOCK: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_NAND_FLUSH_CNT: //0x08
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Nand_Flash_Count(req, cmd, numd_bytes);
			break;
		case VSC_ECC_INSERT: //0x10
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_ECC_DELETE: //0x11
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_ECC_RESET: //0x12
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_InternalTableOperation) //0x16
	{
		switch (dw12.b.VscMode)
		{
		case VSC_READ_ERASE_CNT: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Read_EC_Table(req, cmd, numd_bytes);
			//handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_READ_VAILD_CNT: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Read_VC_Table(req, cmd, numd_bytes);
			break;
		case VSC_READ_GLIST: //0x02
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Read_Glist_Table(req, cmd, numd_bytes);
			break;
		case VSC_READ_ECCTB: //0x03
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Read_ECC_Table(req, cmd, numd_bytes);
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_DebugOperation) //0x17
	{
		switch (dw12.b.VscMode)
		{
		case VSC_PTF_INFO: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "print debug info\n");

			handle_result = PTF_info(req, cmd, numd_bytes);
			break;
		case VSC_EV_LOG://0x01
				handle_result = nvme_vsc_ev_log(req, cmd, numd_bytes);
				break;
		case VSC_SAVE_LOG: //0x10
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Save_Log(req, cmd, numd_bytes);
			break;
		case VSC_LOAD_LOG: //0x11
			//pochune
			if ( (dw12.b.Dw12Byte2 & 1) == 0)
			{
				switch(dw12.b.Dw12Byte3)
				{
					case 0x4: //load CTQ
						handle_result = get_CTQ(req, cmd, numd_bytes);
					break;
					default:
						//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
				handle_result = HANDLE_RESULT_FINISHED;
				break;
				}
			}
			else
			{
				handle_result = get_header(req, cmd, numd_bytes);
			}
			break;
		case VSC_PCIE_RX_EYE: //0x12
			//nvme_apl_trace(LOG_ERR, 0, "print dram info\n");
			handle_result = pcie_rx_eye_info(req, cmd, numd_bytes);
			break;
		case VSC_PCIE_RX_Margin: //0x13
			//nvme_apl_trace(LOG_ERR, 0, "RX Margin\n");
			handle_result = Vu_rx_lane_margin(req, cmd, numd_bytes);
			break;

        case VSC_CHECK_OTP: //0x16
            handle_result = read_otp(req, cmd, numd_bytes);
            misc_set_otp_deep_stdby_mode(); //_GENE_20210928
            break;

		case VSC_PCIE_RETRAIN: //0x17
			//nvme_apl_trace(LOG_ERR, 0, "PCIe Retrain");
			handle_result = Vu_pcie_retrain(cmd);  // Jack_20220504
			break;

		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else if (dw12.b.VscFunction == Vsc_NVMeAlternative) //0x18
	{
		switch (dw12.b.VscMode)
		{
		case VSC_SECURITY_SEND: //0x00
			//nvme_apl_trace(LOG_ERR, 0, "print debug info\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_SECURITY_RECEIVE: //0x01
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = HANDLE_RESULT_FINISHED;
			break;
		case VSC_Set_FW_CA: //0x02
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = Set_FW_CA(req, cmd, numd_bytes);
			break;
		case VSC_FW_CHECK_DISABLE: //0x40
			//nvme_apl_trace(LOG_ERR, 0, "\n");

			handle_result = FW_Check_Disable(req, cmd, numd_bytes);
			break;
		default:
			//nvme_apl_trace(LOG_ERR, 0, "no define or not supported\n");
			break;
		}
	}
	else
	{
		nvme_apl_trace(LOG_ERR, 0xc08b, "no define or not supported(func\n");
	}
	//handle_result = HANDLE_RESULT_FINISHED;
	if(handle_result == HANDLE_RESULT_FAILURE)
		nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);

	return handle_result;
}
#ifdef Dynamic_OP_En
extern u8 DYOP_FRB_Erase_flag;
extern void nvmet_start_warm_boot(void);
extern void gc_re(void);
#endif
//20200805 kevin add vsc preformat
ddr_code bool vsc_preformat_done(req_t *req)
{
	//nvme_apl_trace(LOG_ERR, 0, "vsc_preformat_done\n");
#ifdef TCG_NAND_BACKUP
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	if(epm_aes_data->prefmtted == TCG_PFMT_TAG)
	{
		extern void tcg_preformat_init(void);	
		tcg_preformat_init();
	
		nvme_apl_trace(LOG_INFO, 0xe585, "[TCG] default G1-3 table flush done !");
	}
#ifdef FW_UPDT_TCG_SWITCH
	else
	{
		extern u8 tcg_nf_allErase(u8 sync);
		tcg_nf_allErase(true);

		// update EPM
		bTcgTblErr = false;
		epm_aes_data->tcg_en_dis_tag = 0;
		epm_aes_data->prefmtted = 0;
		epm_aes_data->tcg_err_flag = 0;
		epm_aes_data->tcg_sts = 0;
		epm_aes_data->readlocked = 0;
		epm_aes_data->writelocked = 0;
		epm_aes_data->new_blk_prog = 0;

		epm_update(AES_sign, (CPU_ID - 1));
		nvme_apl_trace(LOG_WARNING, 0xcb5b, "[TCG] Rls TCG blk already !");
	}
#endif
#endif
#if (PLP_SUPPORT == 1)
	extern volatile u8 plp_trigger;
	if(!plp_trigger)
#endif
	{
		epm_format_state_update(0, 0); //vsc preformat done may call back when plp handle
		epm_update(FTL_sign, (CPU_ID-1));	//preformat done , update epm to clear format tag
	}

#if (PLP_SUPPORT == 0)
	extern volatile u8 non_plp_format_type;
	if(non_plp_format_type == NON_PLP_PREFORMAT)
	{
		non_plp_format_type = 0;
	}
#endif

#ifdef Dynamic_OP_En
	if(DYOP_FRB_Erase_flag == 1)
	{
		gc_re();
		nvmet_io_fetch_ctrl(false);
		DYOP_FRB_Erase_flag = 0;
	}
#endif

	cpu_msg_issue(CPU_FTL - 1, CPU_MSG_SCAN_GLIST_TRG_HANDLE, 0, EH_BUILD_GL_USER_ERASED);	// FET, PfmtHdlGL

	if (req->host_cmd != NULL)
	{
		evt_set_cs(evt_cmd_done, (u32)req->host_cmd, 0, CS_NOW);
	}
	nvmet_put_req(req);

#if 0//def Dynamic_OP_En
	if(DYOP_FRB_Erase_flag == 1)
	{
		//nvmet_start_warm_boot();
		DYOP_FRB_Erase_flag = 0;
	}
#endif

	return true;
}
#ifdef TCG_NAND_BACKUP
ddr_code void tcg_preformat_continue(void)
{
    epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	if ((epm_ftl_data->epm_fmt_not_finish != 0) && (epm_aes_data->prefmtted == TCG_PFMT_TAG))
    {   
    	extern void tcg_preformat_init(void);	
    	tcg_preformat_init();
    	
    	nvme_apl_trace(LOG_ERR, 0xd677, "[TCG] default G1-3 table flush done !, epm_fmt_not_finish:%u", epm_ftl_data->epm_fmt_not_finish);
    }
}
#endif

#ifdef NS_MANAGE
extern struct ns_array_manage *ns_array_menu;//joe add 20210119
extern void rdisk_fe_ns_restore(void);
extern u8 host_sec_bitz;
#endif
#ifdef Dynamic_OP_En
extern u32 DYOP_LBA_L;
extern u32 DYOP_LBA_H;
#endif
ddr_code void vsc_preformat(req_t *rq , bool ns_reset)
{
	nvme_apl_trace(LOG_ERR, 0xdc6a, "vsc_preformat start\n");
    epm_FTL_t *epm_ftl_data = (epm_FTL_t *)ddtag2mem(shr_epm_info->epm_ftl.ddtag);

#ifdef TCG_NAND_BACKUP
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
  #ifdef FW_UPDT_TCG_SWITCH
	if((epm_aes_data->prefmtted == TCG_PFMT_TAG) || ((epm_aes_data->prefmtted == TCG_INIT_TAG)))
		epm_aes_data->prefmtted = 0;
	else
		epm_aes_data->prefmtted = TCG_PFMT_TAG;
  #else
	if(epm_aes_data->prefmtted == TCG_PFMT_TAG)
	{
		extern u8 tcg_nf_G4RdDefault(void);
		tcg_nf_G4RdDefault();
		nvme_apl_trace(LOG_INFO, 0x13e4, "[TCG] G1-3 table read done !");
	}
  #endif
#endif

    #if (TRIM_SUPPORT == ENABLE)
    extern void ReinitTrimTable(bool one_time_init);
    ReinitTrimTable(false);
    #endif

	#if(degrade_mode == ENABLE)
	extern smart_statistics_t *smart_stat;
  	extern none_access_mode_t noneaccess_mode_flags;
	smart_stat->critical_warning.raw = 0;//preformat clean critical warning
	nvme_apl_trace(LOG_ALW, 0x18e8, "Clear all critical warning bit");
  	noneaccess_mode_flags.all = 0;
	read_only_flags.b.plp_not_done = 0;
	read_only_flags.b.spor_user_build = 0;
	#if CO_SUPPORT_SANITIZE
	epm_smart_t *epm_smart_data = (epm_smart_t *)ddtag2mem(shr_epm_info->epm_smart.ddtag);
	if(epm_smart_data->sanitizeInfo.fwSanitizeProcessStates == sSanitize_FW_None)
	#endif
	{
		//leave non-access mode(en rd)
		extern void cmd_disable_btn();
		cmd_disable_btn(-1,0);
	}
	if(cur_ro_status == RO_MD_IN && read_only_flags.all == 0)
	{
		cmd_proc_read_only_setting(false);
	}
  #endif
  
#ifdef SMART_PLP_NOT_DONE
	extern smart_statistics_t *smart_stat;
	smart_stat->critical_warning.bits.epm_vac_err = 0;
	smart_stat->critical_warning.raw = 0;//preformat clean critical warning
	read_only_flags.b.plp_not_done = 0;
	read_only_flags.b.spor_user_build = 0;
	nvme_apl_trace(LOG_ERR, 0xdd1c, "clear plp not done flag");
	if(cur_ro_status == RO_MD_IN && read_only_flags.all == 0)
	{
		cmd_proc_read_only_setting(false);
	}
#endif

#if (PLP_SUPPORT == 0) 
    for(u8 idx=0;idx<2;idx++){ 
        epm_ftl_data->host_open_blk[idx] = INV_U16; 
        epm_ftl_data->gc_open_blk[idx] = INV_U16; 
    } 
    for(u8 i=0; i<SPOR_CHK_WL_CNT;i++){ 
        epm_ftl_data->host_open_wl[i] = INV_U16; 
        epm_ftl_data->gc_open_wl[i]   = INV_U16; 
        epm_ftl_data->host_die_bit[i] = 0; 
        epm_ftl_data->gc_die_bit[i]   = 0; 
    } 
    epm_ftl_data->host_aux_group = INV_U16; 
    epm_ftl_data->gc_aux_group   = INV_U16; 
#endif 

#ifdef ERRHANDLE_ECCT
    //stECCT_ipc_t ecct_info;
    rc_ecct_info[rc_ecct_cnt].lba       = 0;
    rc_ecct_info[rc_ecct_cnt].source    = ECC_REG_VU;
    rc_ecct_info[rc_ecct_cnt].total_len = 0;
    rc_ecct_info[rc_ecct_cnt].type      = VSC_ECC_reset;

    if(rc_ecct_cnt >= MAX_RC_REG_ECCT_CNT - 1)
    {
        rc_ecct_cnt = 0;
        ECC_Table_Operation(&rc_ecct_info[MAX_RC_REG_ECCT_CNT-1]);
    }
    else
    {
        rc_ecct_cnt++;
        ECC_Table_Operation(&rc_ecct_info[rc_ecct_cnt-1]);
    }
#endif
#ifdef NS_MANAGE
	if(ns_reset == true)
	{
		//--delete ns epm & re create one ns--//joe 20210119
		u32 ii = 0;
		u32 aa = 0;
		u32 pp = 0;
		ns_array_menu->free_start_array_point = 0; //joe 20200616 marked test

		for (ii = 0; ii < 32; ii++)
			ns_array_menu->array_order[ii] = 0xff;

		for (aa = 0; aa < 1024; aa++)
			ns_array_menu->ns_sec_array[aa] = 0xffff;

		for (pp = 0; pp < 1024; pp++)
			ns_array_menu->valid_sec2[pp] = 0;

		ns_array_menu->total_order_now = 0;

		ns_array_menu->ns_valid_sector = 0;

		nvme_ns_attr_t *attr;
#ifdef Dynamic_OP_En
		if(DYOP_FRB_Erase_flag == 1)
		{//keep original lbaf
			ns[0].cap = (u64)DYOP_LBA_L + (((u64)(DYOP_LBA_H))<<32) ;
			ns_array_menu->drive_flag = 0xFF;
			for(ii=1;ii<32;ii++)
			{
				attr = &ns_array_menu->ns_attr[ii];
				memset((void*)attr, 0, sizeof(*attr));
			}
			nvme_apl_trace(LOG_ERR, 0x5be3, "ocan4 ns[0].cap 0x%x%x\n", (u32)(ns[0].cap>>32), ns[0].cap);
		}
		else
#endif
		{//back to lbaf 0 (512 byte)

			srb_t *srb = (srb_t *)SRAM_BASE;
			if(epm_ftl_data->OPFlag == 1)
			{
                ns[0].cap = (u64)((u64)_max_capacity*8);
				
			}
			nvme_apl_trace(LOG_INFO, 0x4445, "Ocan13 ns[0].cap 0x%x%x, srb->cap_idx %d, OPFlag %d \n",(u32)(ns[0].cap>>32), ns[0].cap, srb->cap_idx, epm_ftl_data->OPFlag );

			ns_array_menu->drive_flag = 0;
			host_sec_bitz =9;
			ns_array_menu->host_sec_bits = host_sec_bitz;
			for(ii=0;ii<32;ii++)
			{
				attr = &ns_array_menu->ns_attr[ii];
				memset((void*)attr, 0, sizeof(*attr));
			}
		}


		for (u8 i = 0; i < NVMET_NR_NS; i++)
		{
			ctrlr->nsid[i].type = NSID_TYPE_UNALLOCATED;
		}

		rdisk_fe_ns_restore();
		//epm_update(NAMESPACE_sign, (CPU_ID - 1)); //joe add update epm
		//--delete ns epm--//
	}
#endif

    epm_format_state_update(0xFFFFFFFF, FTL_PREFORMAT_TAG);//update epm ftl parameter. 0:format finish; 0xFFFFFFFF:during processing; less than spb_cnt:next erased spb;
#if (PLP_SUPPORT == 0)
	extern volatile u8 non_plp_format_type;
	non_plp_format_type = NON_PLP_PREFORMAT;
#endif
	req_t *req = nvmet_get_req();
	sys_assert(req);

	req->req_from = REQ_Q_OTHER;
	req->opcode = REQ_T_FORMAT;
	req->state = REQ_ST_DISK;
	req->host_cmd = (void *)rq; //for callback
	req->nsid = 1;				//host namespace
	struct nvmet_namespace *ns = ctrlr->nsid[req->nsid].ns;
	req->op_fields.format.meta_enable = (_lbaf_tbl[ns->lbaf].ms) ? true : false;
	req->op_fields.format.erase_type = 3; // for preformat erase blk, nvme spec 3 is reserved
	req->completion = vsc_preformat_done;
	ctrlr->nsid[req->nsid - 1].ns->issue(req);
}

//kevin debug test
#include "console.h"

ddr_code static int chkdef_main(int argc, char *argv[])
{
	chkdef();
	return 0;
}

static DEFINE_UART_CMD(chkdef, "chkdef",
					   "chkdef chkdef chkdef",
					   "chkdef mem", 0, 0, chkdef_main);


static ddr_code int vsc_op_main(int argc, char *argv[])
{
    int type;
    type = atoi(argv[1]);
    switch(type){
        case 1 :
            Vsc_on = 0;
			VU_FLAG.b.vsc_on = 0;
			nvme_apl_trace(LOG_ERR, 0x5183, "Vu open \n");
            break;
        case 2 :
            Vsc_on = 0xEE;
			VU_FLAG.b.vsc_on = 1;
			nvme_apl_trace(LOG_ERR, 0xe805, "Vu close \n");
            break;
        default:
            break;
    }
    return 0;
}

static DEFINE_UART_CMD(vsc_op, "vsc_op", "vsc_op", "vsc on/off", 0, 1, vsc_op_main);

static ddr_code int vac_cmp(int argc, char *argv[])
{
	vac_compare();
    return 0;
}

static DEFINE_UART_CMD(vac_cmp, "vac_cmp", "vac_cmp", "vac_cmp", 0, 0, vac_cmp);

// fast_code static int vsc_preformat_uart(int argc, char *argv[])
// {
// 	u8 run = 0;

// 	if (argc > 1)
// 		run = strtol(argv[1], (void *)0, 10);

// 	if(run)
// 	{
// 		vsc_preformat(NULL);
// 	}
// 	else
// 	{
// 		nvme_apl_trace(LOG_ERR, 0, "not valid parameter\n");
// 	}

// 	return 0;
// }

// static DEFINE_UART_CMD (preformat, "preformat", "preformat", "preformat", 0, 1, vsc_preformat_uart);

#endif
