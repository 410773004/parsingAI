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

#include "nand_cfg.h"
#include "inc/ndcu_reg_access.h"
#include "inc/eccu_reg_access.h"
#include "nand_tsb.h"
#include "nand_define.h"
#include "ncl.h"
#if ONFI_DCC_TRAINING
#include "ndphy.h"
#include "nand.h"
#include "ndcu.h"
#include "ncl_cmd.h"
#include "ndcmd_fmt.h"
#include "eccu.h"
#if TACOMAX
#include "srb.h"
#include "ncl_onfi_dcc_training.h"
#endif

fast_data_zi u32 Dcc_Training_Try_Times;
fast_data_zi u32 DccReadInProcess;

//here 0x35 0xCA data is from b47r/onfi datasheet data training pat
static u32 READ_DQTRAINING_DATA[8] = {0x3535CA35,0XCA353535,0X35CA35CA,0XCA35CA35,0x5353AC53,0xAC535353,0x53AC53AC,0xAC53AC53};//32 BYTES
static u8 READ_DQxTRAINING_DATA[32] = {0x35,0xCA,0x35,0x35,0x35,0x35,0x35,0xCA,0xCA,0x35,0xCA,0x35,0x35,0XCA,0X35,0XCA,\
									  0X53,0XAC,0X53,0X53,0X53,0X53,0X53,0XAC,0XAC,0X53,0XAC,0X53,0X53,0XAC,0X53,0XAC};//32 BYTES
static nf_ncmd_fmt_reg0_t reg_val_temp;
fast_data bool skip_dq_training = false;
#if DLL_PHASE_2_EDGE
extern void ndphy_set_dll_phase(u8 ch, u8 value, bool rise_edge);
#else
extern void ndphy_set_dll_phase(u8 ch, u8 value);
#endif
extern void ndphy_set_DQx_dll_phase(u8 ch, u8 DQx, u8 value);
extern void ndphy_write_DQx_dll_phase(u8 ch, u8 DQx, u8 value);

/*!
 * @brief Temp modify read status fail bit 0 check for dcc training
 *
 * @return	not used
 */
ps_code void mu_dcc_read_status_change(void)
{
	nf_ncmd_fmt_ptr_t reg_ptr;
	nf_ncmd_fmt_reg0_t reg;

	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_READ_STATUS;
	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
	reg.all = ndcu_readl(NF_NCMD_FMT_REG0);
	reg_val_temp.all = reg.all;
	reg.b.rd_scmd_mode = NAND_FMT_RD_SCMD_SBYTE0;
	ndcu_writel(reg.all, NF_NCMD_FMT_REG0);
}

/*!
 * @brief restore temp modify read status failbit check for dcc training
 *
 * @return	not used
 */

ps_code void mu_dcc_read_status_restore(void)
{
	nf_ncmd_fmt_ptr_t reg_ptr;
	nf_ncmd_fmt_reg0_t reg;

	reg_ptr.b.nf_ncmd_fmt_cfg_ptr = FINST_NAND_FMT_READ_STATUS;
	ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
	reg.all = ndcu_readl(NF_NCMD_FMT_REG0);
	reg.all = reg_val_temp.all;
	ndcu_writel(reg.all, NF_NCMD_FMT_REG0);
}

/*!
 * @brief DCC read operation
 *
 * @param pda	PDA
 *
 * @return	not used
 */
slow_code void ncl_dcc_read(pda_t* pda_list, u32 cnt)
{
	struct ncl_cmd_t ncl_cmd;
	struct info_param_t info_list[DU_CNT_PER_PAGE];
	u32 i;

	sys_assert(cnt <= DU_CNT_PER_PAGE);
	extern struct finstr_format read_fins_templ;
	read_fins_templ.dw0.b.finst_type = FINST_TYPE_READ_DATA;
	read_fins_templ.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_READ_DATA;
	read_fins_templ.dw2.b.ard_schem_sel = ARD_DISABLE;
	read_fins_templ.dw2.b.host_trx_dis = 1;
    read_fins_templ.dw0.b.fins_fuse = 1;
    read_fins_templ.dw0.b.susp_en = 1;
	mu_dcc_read_status_change();
	ncl_cmd.op_code = NCL_CMD_OP_READ_RAW;
    #if NCL_FW_RETRY
    ncl_cmd.retry_step = 0;
    #endif
	ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
	ncl_cmd.flags = NAL_PB_TYPE_SLC|NCL_CMD_NO_READ_RETRY_FLAG|NCL_CMD_FEATURE_DCC_TRAINING;
	ncl_cmd.status = 0;
	ncl_cmd.addr_param.common_param.list_len = cnt;
	ncl_cmd.addr_param.common_param.pda_list = pda_list;
	ncl_cmd.addr_param.common_param.info_list = info_list;
	for (i = 0; i < cnt; i++) {
		info_list[i].xlc.slc_idx = 0;
		info_list[i].pb_type = NAL_PB_TYPE_SLC;
	}
	ncl_cmd.du_format_no = DU_FMT_RAW_4588B;
	ncl_cmd.user_tag_list = (bm_pl_t*)SPRM_BASE;
	ncl_cmd.completion = NULL;

	ncl_cmd_submit(&ncl_cmd);
	mu_dcc_read_status_restore();
	read_fins_templ.dw0.b.finst_type = FINST_TYPE_READ;
	read_fins_templ.dw0.b.ndcmd_fmt_sel = FINST_NAND_FMT_SLC_READ_CMD;
	#if HAVE_CACHE_READ
		read_fins_templ.dw2.b.ard_schem_sel = ARD_DISABLE;
	#else
        #if NCL_FW_RETRY
        	read_fins_templ.dw2.b.ard_schem_sel = ARD_DISABLE;
        #else
			read_fins_templ.dw2.b.ard_schem_sel = ARD_TMPLT_SLC;
        #endif
	#endif
	read_fins_templ.dw2.b.host_trx_dis = 0;
    read_fins_templ.dw0.b.fins_fuse = 0;
    read_fins_templ.dw0.b.susp_en = 0;
}

/*!
 * @brief Read DQ operation,and here use Inverse Data(35h)/1st Pattern(5Ah)/2st Pattern(82h) is refer to b47r datasheet
 * and base on datasheet the return data.
 * @param pda	PDA(CH,CE)
 * @param cnt	DU count
 * @param risingEdge 1: DQS rising edge direction 0:DQS fall egde direction
 *
 * @return	not used
 */
u32 dll_phases[MAX_LUN];
#if DLL_PHASE_2_EDGE
ps_code bool ncl_Read_DQ_DQsTraining(pda_t pda,bool rise_edge)
#else
ps_code bool ncl_Read_DQ_DQsTraining(pda_t pda)
#endif
{
	bool fail = false;
	struct ncl_cmd_t ncl_cmd;
	u8 lun = 0;
	bm_pl_t bm_pl;
	u32 *mem = NULL;
	dtag_t dtag;
	u8 ch = 0, ce = 0;
	u32 val = 0;
	u32 mask_value = 0xFFFFFFFF;
	u8 phy_ch = 0;
	struct info_param_t info = {.xlc.slc_idx = 0,.pb_type = NAL_PB_TYPE_SLC};
	u32 i, dll_pass_cnt = 0, width = 0;
	u32 val_start = 0xFFFFFFFF, val_end = 0xFFFFFFFF;
	u32 valStg1_start = 0xFFFFFFFF, valStg1_end = 0xFFFFFFFF;
	u32 lun_value[MAX_LUN][3] = {0};
	u32 result = 0;
	u32 dw_cnt = 8;	// How many DW to check

	enum nand_dll_type {
		best_value = 0,
		min_value,
		max_value
	};

	ch = pda2ch(pda);
	ce = pda2ce(pda);
	phy_ch = nand_phy_ch(ch);
#if DLL_PHASE_2_EDGE
	if (rise_edge) {
		mask_value = 0x00FF00FF;//check even byte
	} else {
		mask_value = 0xFF00FF00;
	}
#endif
	dtag = dtag_get(DTAG_T_SRAM, (void *)&mem);
	if (mem == NULL) {
		printk("alloc DTAG fail.\n");
		return true;
	}

	for (lun = 0; lun < nand_lun_num(); lun++) {
		dll_pass_cnt = 0;
		bool full_dll = true;
		u32 dll_start = 0;
		u32 dll_end = MAX_DLL_COUNT;
#if TACOMAX
#if ZNS
		if (misc_is_warm_boot()) {
#endif
			srb_t *srb = (srb_t *)SRB_HD_ADDR;
			if ((srb->srb_hdr.srb_signature == SRB_SIGNATURE) && (srb->ndphy_dll_valid)) {
#if DLL_PHASE_2_EDGE
				if (rise_edge) {
					dll_start = (u32)srb->ndphy_dll_set[ch][ce][lun];
				} else {
					dll_start = (u32)srb->ndphy_dll_set[ch][ce][lun + 2];
				}
#else
				dll_start = (u32)srb->ndphy_dll_set[ch][ce][lun];
#endif
				dll_end = dll_start + 1; // for reduce calibration time.
				full_dll = false;
				sys_assert(dll_start < MAX_DLL_COUNT);
			}
#if ZNS
		}
#endif
#endif
		for (val = dll_start; val < dll_end; val++) {
#if DLL_PHASE_2_EDGE
			ndphy_set_dll_phase(phy_ch, val, rise_edge);
#else
			ndphy_set_dll_phase(phy_ch, val);
#endif

			memset(mem, 0, dw_cnt * 4);

			bm_pl.all = 0;
			bm_pl.pl.dtag = dtag.b.dtag;
#if   TACOMAX
			bm_pl.pl.type_ctrl = DTAG_QID_DROP;
			bm_pl.pl.nvm_cmd_id = 0;
#endif

			pda |= lun << nand_info.pda_lun_shift;

			ncl_cmd.op_code = NCL_CMD_OP_READ_RAW;
			ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
			ncl_cmd.flags = NCL_CMD_NO_READ_RETRY_FLAG | NCL_CMD_RD_DQ_TRAINING | NCL_CMD_SLC_PB_TYPE_FLAG;
			ncl_cmd.status = 0;
			ncl_cmd.addr_param.common_param.list_len = 1;
			ncl_cmd.addr_param.common_param.pda_list = &pda;
			ncl_cmd.addr_param.common_param.info_list = &info;
			ncl_cmd.du_format_no = DU_FMT_RAW_512B;
			ncl_cmd.user_tag_list = &bm_pl;
			ncl_cmd.completion = NULL;
			ncl_cmd_submit(&ncl_cmd);

//			printk("***********%d******************\n",val);
			for (i = 0; i < dw_cnt; i++) {//when adjust dll phase 0(rise_edge=1),need check even byte
				//if ((i & 3) == 0) {
				//	printk("%x:", i << 2);
				//}
				//printk("mem %d, %x ", i, mem[i]);
				//if ((i & 3) == 3) {
				//	printk("\n");
				//}
				if ((mem[i] & mask_value) != (READ_DQTRAINING_DATA[i] & mask_value)) {
					break;
				}
			}
			if (i == dw_cnt) {// All bytes correct
				if (dll_pass_cnt == 0) {
					val_start = val;
					val_end = val;
				}
				dll_pass_cnt++;
				//check the dll value whether is continuous
				if ((val - val_end) <= 1) {
					val_end = val;//here means continuous
				} else {
					if ((val_end - val_start) > width) {
						valStg1_start = val_start;
						valStg1_end = val_end;
						width = val_end - val_start;
					}
					val_start = val;
					val_end = val;
				}
			}
		}

		if (val_start == ~0) {
			u32 default_dll;
			if (full_dll) {
				default_dll = (MAX_DLL_COUNT - 1) / 2;
			} else {
				default_dll = dll_start;
			}
#if DLL_PHASE_2_EDGE
			if (rise_edge) {
				lun_value[lun][best_value] = default_dll;
			} else {
				lun_value[lun][best_value] |= default_dll << 6;
			}
			printk("Ch %d lun %d cal fail (DQs %d)\n", ch, lun, rise_edge);
#else
			lun_value[lun][best_value] = default_dll;
			printk("Ch %d lun %d cal fail\n", ch, lun);

#endif
			result += lun_value[lun][best_value];
			fail = true;
			continue;
		}
		u8 nand_dll_th = 3;
		//if (si_setting.dll_width) {
		//	nand_dll_th = si_setting.dll_width;
		//}
#if PROGRAMMER
		extern u8 dll_window_th;
		if (dll_window_th != 0) {
			nand_dll_th = dll_window_th;
		}
#endif

		if (valStg1_start != ~0) {
			if ((valStg1_end - valStg1_start) > (val_end - val_start)) {
				val_end = valStg1_end;
				val_start = valStg1_start;
			}
		}
		lun_value[lun][best_value] = (val_start + val_end) / 2;
		lun_value[lun][min_value] = val_start;
		lun_value[lun][max_value] = val_end;
		if (((val_end - val_start) < nand_dll_th) && full_dll) {
#if DLL_PHASE_2_EDGE
			printk("warning narrow window [%d < %d] (DQs %d)\n", val_end - val_start + 1, nand_dll_th, rise_edge);
#else
			printk("warning narrow window [%d < %d]\n", val_end - val_start + 1, nand_dll_th);
#endif
		} else {
			if (!misc_is_warm_boot()) {
#if DLL_PHASE_2_EDGE
				printk("Ch %d ce %d lun %d value [%d] once good %b (DQs %d)\n", ch, ce, lun, val_end - val_start + 1, lun_value[lun][best_value], rise_edge);
#else
				printk("Ch %d ce %d lun %d value [%d] once good %b\n", ch, ce, lun, val_end - val_start + 1, lun_value[lun][best_value]);
#endif
			}
		}
#if DLL_PHASE_2_EDGE
		if (rise_edge) {
			dll_phases[lun] = lun_value[lun][best_value];
		} else {
			lun_value[lun][best_value] <<= ctz(MAX_DLL_COUNT);
			lun_value[lun][best_value] |= dll_phases[lun];
		}
#endif

#if TACOMAX
		u32 phy_ch, phy_ce;

		phy_ch = nand_phy_ch(ch);
		phy_ce = nand_phy_ce(phy_ch, ce);
#if DLL_PHASE_2_EDGE
		if (!rise_edge) {
			//printk("phy_ch %d phy_ce %d lun %d value 0x%x\n",phy_ch, phy_ce, lun, DllValueRecord[ch][ce][lun] & 0xFFF);
			ndphy_set_dll_phase_enhance(phy_ch, phy_ce, lun, lun_value[lun][best_value] & 0xFFF);
		}
#elif RAINIER_A0
		ndphy_set_dll_phase_enhance(phy_ch, phy_ce, lun, lun_value[lun][best_value] & 0x3F);
#endif
#if PROGRAMMER
		extern u16 ndphy_dll_cali[MAX_CHANNEL][MAX_TARGET][MAX_LUN][3];
		for (i = 0; i < 3; i++) {
#if DLL_PHASE_2_EDGE
			if (rise_edge) {
				ndphy_dll_cali[ch][ce][lun][i] = lun_value[lun][i] & 0x3F;
			} else {
				ndphy_dll_cali[ch][ce][lun][i] |= (lun_value[lun][i] & 0x3F) << 6;
			}
#else
			ndphy_dll_cali[ch][ce][lun][i] = lun_value[lun][i] & 0x3F;
#endif
		}
#endif
#endif
		result += lun_value[lun][best_value];
	}
	dtag_put(DTAG_T_SRAM, dtag);
#if DLL_PHASE_2_EDGE
	ndphy_set_dll_phase(phy_ch, (result >> ctz(nand_lun_num())), rise_edge);
#else
	ndphy_set_dll_phase(phy_ch, (result >> ctz(nand_lun_num())));
#endif
	return fail;
}

ps_code void ncl_Read_DQ_DQxTraining(pda_t pda)
{
	struct ncl_cmd_t ncl_cmd;
	u8 lun = 0;
	bm_pl_t bm_pl;
	u8 *mem = NULL;
	dtag_t dtagOne;
	u8 ch = 0, ce = 0;
	u32 val = 0, DQx = 0;
	u8 phy_ch = 0;
	struct info_param_t info={.xlc.slc_idx = 0,
                              .pb_type = NAL_PB_TYPE_SLC};
	u32 i = 0, timeRecord = 0;
	u32 val_start = 0xFFFFFFFF, val_end = 0xFFFFFFFF;
	//u32 lun_value[8]={0};
	u32 result[8] = {0};
	u32 result_max = 0;

	ch = pda2ch(pda);
	ce = pda2ce(pda);
	phy_ch = nand_phy_ch(ch);
	for(lun = 0; lun < nand_lun_num(); lun++){
		for(DQx = 0; DQx < 8; DQx++){
			timeRecord = 0;
			for (val = 0; val < 8; val++) {
				ndphy_set_DQx_dll_phase(phy_ch, DQx, val);

				// Allocate DTAGs
				i = dtag_get_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
				if (1 != i) {
					dtag_put_bulk(DTAG_T_SRAM, i, (dtag_t *)&dtagOne);
					printk("alloc DTAG fail.\n");
					return;
				}
#if   TACOMAX
				mem = (u8*)dtag2mem(dtagOne);
#endif
				memset(mem, 0, 512*8);//512 Bytes

				bm_pl.all = 0;
				bm_pl.pl.dtag = dtagOne.b.dtag;
#if   TACOMAX
				bm_pl.pl.type_ctrl = DTAG_QID_DROP;
				bm_pl.pl.nvm_cmd_id = 0;
#endif

				pda |= lun << nand_info.pda_lun_shift;

				ncl_cmd.op_code = NCL_CMD_OP_READ_RAW;
				ncl_cmd.op_type = INT_TABLE_READ_PRE_ASSIGN;
				ncl_cmd.flags = NCL_CMD_NO_READ_RETRY_FLAG | NCL_CMD_RD_DQ_TRAINING | NCL_CMD_SLC_PB_TYPE_FLAG;
				ncl_cmd.status = 0;
				ncl_cmd.addr_param.common_param.list_len = 1;
				ncl_cmd.addr_param.common_param.pda_list = &pda;
				ncl_cmd.addr_param.common_param.info_list = &info;

				ncl_cmd.du_format_no = DU_FMT_RAW_512B;
				ncl_cmd.user_tag_list = &bm_pl;
				ncl_cmd.completion = NULL;

				ncl_cmd_submit(&ncl_cmd);

				//printk("***********%d******************\n",val);
				for (i = 0; i < 32; i++) {
					if ((mem[i]&BIT(DQx)) != (READ_DQxTRAINING_DATA[i]&BIT(DQx))) {
						break;
					}
//					if ((i & 3) == 0) {
//						printk("%x:", i << 2);
//					}
//					printk("%x ", mem[i]);
//					if ((i & 3) == 3) {
//						printk("\n");
//					}
				}
				if (i == 32) {
					if (timeRecord == 0) {
						val_start = val;
						val_end = val;
					}
					if ((val-val_end) > 1) {
//						ncl_assert(0);//suppose dll value should be continuous
						printk("pay attention here means read DQx 2 segment call window\n");
					}
					timeRecord++;
					val_end = val;
				}
				dtag_put_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
			}
			if ((val_start == 0xFFFFFFFF) && (val_end == 0xFFFFFFFF)) {
				printk("Ch %d ce %d lun %d rdDQx %d cal fail\n",ch,ce,lun,DQx);
				//DllValueRecord[ch][ce][lun]=0x12344321;
				ndphy_set_DQx_dll_phase(phy_ch, DQx, 0);
				continue;
			}
			result[DQx] = (val_start+val_end)/2;
			if (result[DQx] > result_max)
				result_max = result[DQx];
		}
		for(DQx = 0; DQx < 8; DQx++){
			ndphy_set_DQx_dll_phase(phy_ch, DQx, (result_max-result[DQx]));
			printk("Ch %d ce %d lun %d rdDQx %d cal success [%d]\n",ch,ce,lun,DQx,(result_max-result[DQx]));
		}
	}
//	if (!timeRecord2) {
//		ndphy_set_dll_phase(phy_ch, 0x1F);
//		return;
//	}
//	ndphy_set_dll_phase(phy_ch, (result/timeRecord2));
}


extern bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step);
extern enum ncb_err read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt, bool fail_retry);

/*!
 * @brief Write DQs training operation
 *
 * @param pda	PDA(CH,CE)
 * @param cnt	DU count
 *
 * @return	not used
 */
ps_code void ncl_Write_DQsTraining(pda_t pda)
{
	struct ncl_cmd_t ncl_cmd;
	u32 i = 0;
	u8 lun = 0, ch = 0;
	bm_pl_t bm_pl;
	u32 *mem = NULL;
	u32 *Readmem = NULL;
	dtag_t dtagOne;

	ch = pda2ch(pda);
#if NAND_WRITE_DQS_TRAINING
	u32 val_start = 0xFFFFFFFF, val_end = 0xFFFFFFFF;
	u32 result = 0, phy_ch = 0, timeRecord = 0, ce = 0;
	u8 val = 0;
	phy_ch = nand_phy_ch(ch);
	ce = pda2ce(pda);
#endif

	struct info_param_t info;
	memset(&info, 0, sizeof(struct info_param_t));
	info.pb_type = NAL_PB_TYPE_SLC;


	for(lun = 0; lun < nand_lun_num(); lun++){
		pda |= lun << nand_info.pda_lun_shift;
		//for(DQx = 0; DQx < 8; DQx++){
#if NAND_WRITE_DQS_TRAINING
			timeRecord = 0;
			for (val = 0; val < 64; val++) {
				ndphy_write_DQs_dll_phase(phy_ch, val);
#endif
				// Allocate DTAGs
				i = dtag_get_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
				if (1 != i) {
				dtag_put_bulk(DTAG_T_SRAM, i, (dtag_t *)&dtagOne);
				printk("alloc DTAG fail.\n");
				return;
				}
#if   TACOMAX
				mem = (u32*)dtag2mem(dtagOne);
#endif
				memset(mem, 0, NAND_DU_SIZE);

				for (i = 0; i < NAND_DU_SIZE/sizeof(u32);i++) {
					mem[i]=READ_DQTRAINING_DATA[i & 3];
				}
				bm_pl.all = 0;
				bm_pl.pl.dtag = dtagOne.b.dtag;
#if   TACOMAX
				bm_pl.pl.type_ctrl = DTAG_QID_DROP;
				bm_pl.pl.nvm_cmd_id = 0;
#endif

				ncl_cmd.op_code = NCL_CMD_OP_WRITE;
				ncl_cmd.op_type = INT_TABLE_WRITE_PRE_ASSIGN;
				ncl_cmd.flags = NCL_CMD_WR_DQ_TRAINING | NCL_CMD_SLC_PB_TYPE_FLAG;
				ncl_cmd.status = 0;
				ncl_cmd.addr_param.common_param.list_len = 1;
				ncl_cmd.addr_param.common_param.pda_list = &pda;
				ncl_cmd.addr_param.common_param.info_list = &info;

				ncl_cmd.du_format_no = DU_FMT_RAW_512B;
				ncl_cmd.user_tag_list = &bm_pl;
				ncl_cmd.completion = NULL;

				ncl_cmd_submit(&ncl_cmd);
				//lay_us(10000);

				ncl_cmd.flags = 0;
				dtag_put_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);

				// Allocate DTAGs
				i = dtag_get_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
#if   TACOMAX
				Readmem = (u32*)dtag2mem(dtagOne);
#endif
				memset(Readmem, 0, NAND_DU_SIZE);


				bm_pl.all = 0;
				bm_pl.pl.dtag = dtagOne.b.dtag;
#if   TACOMAX
				bm_pl.pl.type_ctrl = DTAG_QID_DROP;
				bm_pl.pl.nvm_cmd_id = 0;
#endif

				ncl_cmd.op_code = NCL_CMD_OP_READ_RAW;
				ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
				ncl_cmd.flags = NCL_CMD_WR_DQ_TRAINING | NCL_CMD_NO_READ_RETRY_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;
				ncl_cmd.status = 0;
				ncl_cmd.addr_param.common_param.list_len = 1;
				ncl_cmd.addr_param.common_param.pda_list = &pda;
				ncl_cmd.addr_param.common_param.info_list = &info;

				ncl_cmd.du_format_no = DU_FMT_RAW_512B;
				ncl_cmd.user_tag_list = &bm_pl;
				ncl_cmd.completion = NULL;

				ncl_cmd_submit(&ncl_cmd);

				for (i = 0; i < 4; i++) {
					if (Readmem[i] != READ_DQTRAINING_DATA[i]) {
#if !NAND_WRITE_DQS_TRAINING
						printk("Ch %d lun %d wrDQs fail\n",ch,lun);
#endif
						break;
					}
//						printk("%x ", Readmem[i]);
				}
#if NAND_WRITE_DQS_TRAINING
				//printk("\n");
				if (i == 8) {
					if (timeRecord == 0) {
						val_start = val;
						val_end = val;
					}
					if ((val - val_end) > 1) {
//						ncl_assert(0);//suppose dll value should be continuous
						printk("pay attention here means write DQs 2 segment call window\n");
					}
					timeRecord++;
					val_end = val;
				}
#endif
				dtag_put_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
#if NAND_WRITE_DQS_TRAINING
			}
			if ((val_start == 0xFFFFFFFF) && (val_end == 0xFFFFFFFF)) {
				printk("Ch %d lun %d wrDQs cal fail\n",ch,lun);
				ndphy_write_DQs_dll_phase(phy_ch, 0);
				continue;
			}
			result = (val_start + val_end)/2;
		//}
			ndphy_write_DQs_dll_phase(phy_ch, result);
			printk("Ch %d ce %d lun %d wrDQs [%b~%b] good [%b]\n",ch,ce,lun,val_start,val_end,result);
#endif
	}
}

/*!
 * @brief Write DQx training operation
 *
 * @param pda	PDA(CH,CE)
 * @param cnt	DU count
 *
 * @return	not used
 */
ps_code void ncl_Write_DQxTraining(pda_t pda)
{
	struct ncl_cmd_t ncl_cmd;
	u32 i = 0,timeRecord = 0;
	u8 lun = 0, val = 0, DQx = 0, phy_ch = 0, ch = 0, ce = 0;
	bm_pl_t bm_pl;
	u32 *mem = NULL;
	u8 *Readmem = NULL;
	dtag_t dtagOne;
	u32 result[8] = {0};
	u32 result_max = 0;
	struct info_param_t info;
	u32 val_start = 0xFFFFFFFF, val_end = 0xFFFFFFFF;
	memset(&info, 0, sizeof(struct info_param_t));
	info.pb_type = NAL_PB_TYPE_SLC;

	ch = pda2ch(pda);
	ce = pda2ce(pda);
	phy_ch = nand_phy_ch(ch);
	for(lun = 0; lun < nand_lun_num(); lun++){
		pda |= lun << nand_info.pda_lun_shift;
		for(DQx = 0; DQx < 8; DQx++){
			timeRecord = 0;
			for (val = 0; val < 64; val++) {
				ndphy_write_DQx_dll_phase(phy_ch, DQx, val);

				// Allocate DTAGs
				i = dtag_get_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
				if (1 != i) {
				dtag_put_bulk(DTAG_T_SRAM, i, (dtag_t *)&dtagOne);
				printk("alloc DTAG fail.\n");
				return;
				}
#if   TACOMAX
				mem = (u32*)dtag2mem(dtagOne);
#endif
				memset(mem, 0, NAND_DU_SIZE);

				for (i = 0; i < NAND_DU_SIZE/sizeof(u32);i++) {
					if (i < 8) {
						mem[i]=READ_DQTRAINING_DATA[i];
					}else{
						mem[i] = 0x12345678+i;
					}
				}
				bm_pl.all = 0;
				bm_pl.pl.dtag = dtagOne.b.dtag;
#if   TACOMAX
				bm_pl.pl.type_ctrl = DTAG_QID_DROP;
				bm_pl.pl.nvm_cmd_id = 0;
#endif

				ncl_cmd.op_code = NCL_CMD_OP_WRITE;
				ncl_cmd.op_type = INT_TABLE_WRITE_PRE_ASSIGN;
				ncl_cmd.flags = NCL_CMD_WR_DQ_TRAINING | NCL_CMD_SLC_PB_TYPE_FLAG;
				ncl_cmd.status = 0;
				ncl_cmd.addr_param.common_param.list_len = 1;
				ncl_cmd.addr_param.common_param.pda_list = &pda;
				ncl_cmd.addr_param.common_param.info_list = &info;

				ncl_cmd.du_format_no = DU_FMT_RAW_4K;
				ncl_cmd.user_tag_list = &bm_pl;
				ncl_cmd.completion = NULL;

				ncl_cmd_submit(&ncl_cmd);
				//lay_us(10000);

				ncl_cmd.flags = 0;
				dtag_put_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);

				// Allocate DTAGs
				i = dtag_get_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
#if   TACOMAX
				Readmem = (u8*)dtag2mem(dtagOne);
#endif
				memset(Readmem, 0, NAND_DU_SIZE*8);


				bm_pl.all = 0;
				bm_pl.pl.dtag = dtagOne.b.dtag;
#if   TACOMAX
				bm_pl.pl.type_ctrl = DTAG_QID_DROP;
				bm_pl.pl.nvm_cmd_id = 0;
#endif

				ncl_cmd.op_code = NCL_CMD_OP_READ_RAW;
				ncl_cmd.op_type = INT_DATA_READ_PRE_ASSIGN;
				ncl_cmd.flags = NCL_CMD_WR_DQ_TRAINING | NCL_CMD_NO_READ_RETRY_FLAG | NCL_CMD_SLC_PB_TYPE_FLAG;
				ncl_cmd.status = 0;
				ncl_cmd.addr_param.common_param.list_len = 1;
				ncl_cmd.addr_param.common_param.pda_list = &pda;
				ncl_cmd.addr_param.common_param.info_list = &info;

				ncl_cmd.du_format_no = DU_FMT_RAW_4K;
				ncl_cmd.user_tag_list = &bm_pl;
				ncl_cmd.completion = NULL;

				ncl_cmd_submit(&ncl_cmd);

				for (i = 0; i < 32; i++) {
						if ((Readmem[i]&BIT(DQx)) != (READ_DQxTRAINING_DATA[i]&BIT(DQx))) {
							break;
						}

//						printk("%x ", Readmem[i]);
//						if ((i & 3) == 3) {
//							printk("\n");
//						}
				}
				if (i == 32) {
					if (timeRecord == 0) {
						val_start = val;
						val_end = val;
					}
					if ((val-val_end) > 1) {
//						ncl_assert(0);//suppose dll value should be continuous
						printk("pay attention here means write DQx %d has 2 segment call window\n",val);
					}
					timeRecord++;
					val_end = val;
				}

				dtag_put_bulk(DTAG_T_SRAM, 1, (dtag_t *)&dtagOne);
			}
			if ((val_start == 0xFFFFFFFF) && (val_end == 0xFFFFFFFF)) {
				printk("Ch %d lun %d wrDQx %d cal fail\n",ch,lun,DQx);
				ndphy_write_DQx_dll_phase(phy_ch, DQx, 0);
				continue;
			}
			result[DQx]=(val_start+val_end)/2;
			if (result[DQx] > result_max)
				result_max = result[DQx];
		}
		for(DQx = 0; DQx < 8; DQx++){
			ndphy_write_DQx_dll_phase(phy_ch, DQx, (result_max-result[DQx]));
			printk("Ch %d ce %d lun %d wrDQx %d cal success [%d]\n",ch,ce,lun,DQx,(result_max-result[DQx]));
		}
	}
}


/*!
 * @brief Data Training (include DCC Training,Read DQ Training,Write DQ training)
 *
 * @return	not used
 */
ps_code bool nand_data_training(void)
{
		bool fail = false;
		pda_t pda;
        pda_t pda_list[DU_CNT_PER_PAGE];
		u32 ch, ce;
		extern u32 Dcc_Training_Try_Times;
		Dcc_Training_Try_Times = 0;
		u32 ficu_value = 0;
        u32 i = 0;
        u32 is_sts_fail = 0 ;
		u32 tempValue = ndcu_readl(NF_RXCMD_MP_REG0);
		u32 tempValue2 = ndcu_readl(NF_RXCMD_REG0);
		u32 tempValue3 = ndcu_readl(NF_PCMD_REG00);
		u32 readstsValue = ndcu_readl(NF_SCMD_REG0);
		nf_scmd_type_reg0_t scmd_type_reg;
		scmd_type_reg.all = ndcu_readl(NF_SCMD_TYPE_REG0);

		eccu_ctrl_reg0_t eccu_ctrl;
		eccu_ctrl.all = eccu_readl(ECCU_CTRL_REG0);
		eccu_ctrl.b.enc_rpt_on_enc_err = 0;
		eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);

		DccReadInProcess = 1;
#if   TACOMAX
		ficu_value = ficu_readl(FICU_INT_EN_REG0);
		ficu_writel(ficu_value&(~FICU_NO_DQS_ERR_EN_MASK), FICU_INT_EN_REG0);//disable no dqs INT,due to DQS is in High-Z status
#endif

		for (ch = 0; ch < nand_channel_num(); ch++) {
			for (ce = 0; ce < nand_target_num(); ce++) {
				pda = (ch << nand_info.pda_ch_shift) + (ce << nand_info.pda_ce_shift);
                for (i = 0; i < DU_CNT_PER_PAGE; i++) {
					pda_list[i] = pda + i;
				}
				Dcc_Training_Try_Times = 0;
				ndcu_writel(NAND_READ_STATUS_70 * 0x01010101, NF_SCMD_REG0);  //lun = 1 no aipr setting
				ndcu_writel(scmd_type_reg.all & ~0xFF, NF_SCMD_TYPE_REG0);

				ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, false, 1);

				// Read instruction with adress 00~6(addr)~05~2(addr)~e0
				ndcu_writel(0x05050505, NF_RXCMD_MP_REG0);//here just need use byte0 05h
                mu_dcc_read_status_change();
				ncl_dcc_read(pda_list, DU_CNT_PER_PAGE);
                is_sts_fail |= ncl_polling_status(pda_list[0]);
				if (is_sts_fail){
					printk("ch : %d ce: %d dcc test fail \n",ch ,ce );
				}
                mu_dcc_read_status_restore();
				ndcu_writel(tempValue, NF_RXCMD_MP_REG0);

#if USE_TSB_NAND
				ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, false, BIT20);// Read Training pattern length 32 bytes
#else
				ncl_set_feature(pda, NAND_FA_TSB_DCC_132BGA, false, 0);
#endif
				ndcu_writel(readstsValue, NF_SCMD_REG0);
				ndcu_writel(scmd_type_reg.all, NF_SCMD_TYPE_REG0);

				if (skip_dq_training) {
					continue; // Skip read/write DQ training to reduce training time.
				}

				nf_pdwn_ctrl_reg0_t pdwn_ctrl_reg, pdwn_ctrl_reg_bak;
				pdwn_ctrl_reg_bak.all = ndcu_readl(NF_PDWN_CTRL_REG0);
				pdwn_ctrl_reg.all = pdwn_ctrl_reg_bak.all;
				pdwn_ctrl_reg.b.nf_mlun_sel_en = 0; // Disable send 78h lun select before random data out
				ndcu_writel(pdwn_ctrl_reg.all, NF_PDWN_CTRL_REG0);

				//Read DQ Training 62h NAND_CMD_READ_DQ_TRAIN_PREFIX
				ndcu_writel(0x62626262, NF_RXCMD_REG0);//here just need use byte1 62h
#if DLL_PHASE_2_EDGE
				fail |= ncl_Read_DQ_DQsTraining(pda, 1);
				fail |= ncl_Read_DQ_DQsTraining(pda, 0);
#else
				fail |= ncl_Read_DQ_DQsTraining(pda);
#endif
#if NAND_READ_DQX_TRAINING
				ncl_Read_DQ_DQxTraining(pda);
#endif

				//Write DQ Training 63h WRITE DQ TRAINING - Tx Side (63h)
				ndcu_writel(0x63636363, NF_PCMD_REG00);
				//WRITE DQ TRAINING - Tx Read Back (64h)
				ndcu_writel(0x64646464, NF_RXCMD_REG0);
				ncl_Write_DQsTraining(pda);
#if NAND_WRITE_DQX_TRAINING
				ncl_Write_DQxTraining(pda);
#endif
				ndcu_writel(tempValue3, NF_PCMD_REG00);
				ndcu_writel(tempValue2, NF_RXCMD_REG0);

				ndcu_writel(pdwn_ctrl_reg_bak.all, NF_PDWN_CTRL_REG0); // restore register
			}
		}
		eccu_ctrl.b.enc_rpt_on_enc_err = 1;
		eccu_writel(eccu_ctrl.all, ECCU_CTRL_REG0);

		//here is for clear no dqs err,due to Data training DQS is in High-Z status
		extern void ndphy_hw_reset(void);
		ndphy_hw_reset();
#if RAINIER_S
		extern void ficu_no_dqs_err_bit_clr(void);
		ficu_no_dqs_err_bit_clr();
#else
		ficu_mode_disable();
		delay_us(1000);
		ficu_mode_enable();
#endif
		ficu_writel(FICU_NO_DQS_ERR_INT_MASK, FICU_INT_SRC_REG);
#if   TACOMAX
		ficu_writel(ficu_value, FICU_INT_EN_REG0);
#endif
#if TACOMAX
		ndphy_io_strobe_enable(true);
#endif
		DccReadInProcess = 0;
		return fail;
}

#endif

