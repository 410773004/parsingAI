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
//! \file
//! @brief vdisk support
//!
//! Vdisk is used for Programmer, it supports factory defect scan, SRB update,
//! Phy tuning and misc functions.
//!
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nvme_precomp.h"
#include "req.h"
#include "nvme_apl.h"
#include "hal_nvme.h"
#include "bf_mgr.h"
#include "mod.h"
#include "event.h"
#include "assert.h"
#include "queue.h"
#include "misc.h"
#include "crc32.h"
#include "ncl_exports.h"
#include "ncl_cmd.h"
#include "console.h"
#include "srb.h"
#include "fwconfig.h"
#include "ddr_info.h"
#include "ddr.h"
#include "lib.h"

/*! \cond PRIVATE */
#define __FILEID__ vdisk
#include "trace.h"
/*! \endcond */

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define VDISK                z.1

#define DEFECT_SIGNATURE            0x54464543  /* DEFT */


#define IMAGE_SIGNATURE    0x54495247 /* GRIT */
#define IMAGE_COMBO        0x424D4F43 /* COMB */
#define IMAGE_CMFG        0x47464D43 /* CMFG */
#define IMAGE_MAX_SIZE      (900 << 10) /*900K*/

#define DOWNLOAD_MAX_DU      (256)

#define SB_SRAM_ADDR      0x200e0000

BUILD_BUG_ON(sizeof(fw_config_set_t) != 4096);
#define FW_CONFIG_DUS		occupied_by(sizeof(fw_config_set_t), DTAG_SZE)

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

/* each SPB 256 bit */
typedef struct _defect_t {
	u32 signature;
	u32 bitmap_sz;
	u32 width_nominal;
	u32 total_spbs;
	dft_btmp_t *bitmap[0];
} PACKED defect_t;

typedef struct {
	unsigned int identifier;   /* identifier of the section, etc. uEFI, ATCM and so on */
	unsigned int offset;       /* offset of the section into the image */
	unsigned int length;       /* length of the section */
	unsigned int pma;          /* PMA of the section to load */
} section_t;

typedef struct {
	unsigned int signature;     /* equals to IMAGE_SIG */
	unsigned int entry_point;   /* entry point of ELF */
	unsigned short section_num; /* # of sections */
	unsigned short image_dus;   /* # of 2K dus */
	unsigned int section_csum;  /* checksum of section(s) */
	union {
		section_t sections[0];
		fw_slice_t fw_slice[0];
	};
} image_t;
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
static fast_data u16 spb_total;
static fast_data u32  width_nominal; /* #CH *#DIE * #PLN */
static fast_data u32 slc_pgs_per_blk;

dft_btmp_t *defect_bitmap = NULL;//(dft_btmp_t *) CPU2_BTCM_BASE;	///< this buffer is hugh, use another CPU's BTCM
int defect_bitmap_size;
u32 ch, dev;

/* fdma_top.i_ecu_wr_dw_len[11:0] -> 4096 * 4 = 16K, /2K -> 8 */
static dtag_t enc_cmf_dtags[8];
static dtag_t dec_cmf_dtags[8];
static dtag_t dftb_dtags[64];
static u32 enc_cmf_sze;
static u32 dec_cmf_sze;

static slow_data u32 sb_du_amt = 0;
static slow_data dtag_t sb_dus[DOWNLOAD_MAX_DU];
static slow_data bool require_bbt_scan = false;
static slow_data bool vdisk_erase_enable = true;

static bool load_old_mr = false;
static slow_data u16 fw_slot = 0xFFFF;
slow_data u16 fw_max_slot = 0xFFFF;
static slow_data bool fwb_mode = false;
static slow_data u32 fw_start_du;
static slow_data u32 fw_end_du;
static slow_data dtag_t fw_config_dtag;

static slow_data u32 sb_du_cnt = 0;
static slow_data rda_t  fwb_pri_pos;      /* Firmware Primary Block */
static slow_data rda_t  fwb_sec_pos;      /* Firmware Secondary Block */
static slow_data rda_t  ftlb_pri_pos;     /* FTL Primary block */
static slow_data rda_t  ftlb_sec_pos;     /* FTL Secondary block */
static slow_data rda_t  evtb_pi_pos;      /* Event Log Ping Block */
static slow_data rda_t  evtb_po_pos;      /* Event Log Pong block */
slow_data struct du_meta_fmt _dummy_meta[64] ALIGNED(32);		///< dummy meta data
slow_data struct du_meta_fmt *dummy_meta = &_dummy_meta[0];
slow_data struct du_meta_fmt _wr_dummy_meta[64] ALIGNED(32);		///< dummy meta data for write
slow_data struct du_meta_fmt *wr_dummy_meta = &_wr_dummy_meta[0];
slow_data struct du_meta_fmt dtag_meta[SRAM_IN_DTAG_CNT] ALIGNED(32);	///< dtag meta data
static int erase_spb(u32 spb_id);

//-----------------------------------------------------------------------------
//  External Functions
//-----------------------------------------------------------------------------
extern u32 *eccu_get_binding_dec_cmf(u32 *size);
extern u32 *eccu_get_binding_enc_cmf(u32 *size);
/* TODO */
extern int ncl_cmd_simple_submit(rda_t * rda_list, enum ncl_cmd_op_t op,
	bm_pl_t * dtag, u32 count, int du_format, int stripe_id);
extern int ncl_access_mr(rda_t * rda_list, enum ncl_cmd_op_t op,
	bm_pl_t * dtag, u32 count);
extern void nal_pda_to_rda(pda_t pda, enum nal_pb_type pb_type, rda_t *rda);
extern pda_t nal_make_pda(u32 spb_id, u32 index);
extern pda_t nal_rda_to_pda(rda_t * rda);
extern void nal_get_first_dev(u32 *ch, u32 *dev);
extern u8	ndphy_dll_cali[16][8][4];	/* ndphy dll value per ch/ce/lun */
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
#if 0
static fast_code void memset_dword(void *dst, u32 pattern, u32 nr_bytes)
{
	u32 *p = (unsigned int *) dst;
	sys_assert(((u32)p & 0x3) == 0);
	sys_assert((nr_bytes % 4) == 0);

	nr_bytes >>= 2;
	while (nr_bytes--) {
		*p++ = pattern;
	}
}

static slow_code void memcpy_dword(void *dst, const void *src, u32 nr_bytes)
{
	u32 *p_dst = (u32 *) dst;
	u32 *p_src = (u32 *) src;
	sys_assert(((u32)p_dst & 0x3) == 0);
	sys_assert(((u32)p_src & 0x3) == 0);
	sys_assert((nr_bytes % 4) == 0);

	nr_bytes /= sizeof(u32);

	while (nr_bytes--) {
		*p_dst++ = *p_src++;
	}
}

static slow_code int memcmp_dword(void *buf, u32 pattern, u32 count)
{
	u32 *u32_buf = (u32 *)buf;
	u32 i = 0;
	sys_assert(((u32)u32_buf & 0x3) == 0);
	sys_assert((count % 4) == 0);
	count >>= 2;
	for (i=0; i<count; i++) {
		if (u32_buf[i] != pattern) {
			return 1;
		}
	}
	return 0;
}
#endif

static fast_code int rda_erase(rda_t rda)
{
	disp_apl_trace(LOG_ALW, 0x181d, "Erasing block Row(0x%x)@CH/CE(%d/%d)", rda.row, rda.ch, rda.dev);
	int retval = ncl_cmd_simple_submit(&rda, NCL_CMD_OP_ERASE, NULL, 1, DU_2K_MR_MODE, 0);

	disp_apl_trace(LOG_ALW, 0x81c7, "result: %d", retval);
	return retval;
}

static row_t nda2row(u32 pgn, u8 pln, u16 blk, u8 lun)
{
	return (pgn << nand_row_page_shift()) | (pln << nand_row_plane_shift()) | (blk << nand_row_blk_shift()) | (lun << nand_row_lun_shift());
}

static void row2nda(row_t row, u8 *pln, u16 *blk, u8 *lun)
{
	*pln = (row >> nand_row_plane_shift()) & (nal_plane_count_per_lun() - 1);
	*blk = (row >> nand_row_blk_shift()) & (nand_page_num_slc() - 1);
	*lun = (row >> nand_row_lun_shift()) & (nal_lun_count_per_dev() - 1);
}

/* we can not use the orignal CMF due to CMF is 2K mode */
static fast_code void bf_to_dtags(void *cmf, dtag_t *dtags, u8 required)
{
	int i;

	for (i = 0; i < required; i++) {
		void *mem = dtag2mem(dtags[i]);

		memcpy(mem, cmf + SRB_MR_DU_SZE * i, SRB_MR_DU_SZE);
	}
}

static fast_code bool sb_in_sram(void)
{
	if (sb_du_amt > 0) {
		image_t *image = (image_t *) dtag2mem(sb_dus[fwb_mode ? 1 : 0]);

		if ((image->signature == IMAGE_SIGNATURE) &&
				(crc32(image->sections, sizeof(section_t) * image->section_num) == image->section_csum))
			return true;
	}
	return false;
}

static fast_code int sb_size_in_du(void)
{
	return sb_in_sram() ? ((image_t *) dtag2mem(sb_dus[fwb_mode ? 1 : 0]))->image_dus : 9;
}


fast_code void vdisk_sync_erase(u32 spb_id, u32 *df)
{
	int i;
	struct ncl_cmd_t _ncl_cmd;
	struct ncl_cmd_t *ncl_cmd = &_ncl_cmd;
	pda_t *pda;
	u32 width = nal_get_interleave();
	struct info_param_t *info;
	u32 ch;

	pda = sys_malloc(FAST_DATA, sizeof(pda_t) * width);
	sys_assert(pda);
	info = sys_malloc(FAST_DATA, sizeof(*info) * width);
	sys_assert(info);
	memset(info, 0, sizeof(*info) * width);

	i = 0;
	for (ch = 0; ch < nand_channel_num(); ch++) {
		u32 ce;
		for (ce = 0; ce < nand_target_num(); ce++) {
			u32 lun;
			for (lun = 0; lun < nand_lun_num(); lun++) {
				u32 pl;
				for (pl = 0; pl < nand_plane_num(); pl++) {
					u32 row = row_assemble(lun, pl, spb_id, 0);

					pda[i] = pda_assemble(ch, ce, row, 0);
					info[i].pb_type = NAL_PB_TYPE_XLC;
					i++;
				}
			}
		}
	}

	ncl_cmd->completion = NULL;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = NCL_CMD_SYNC_FLAG | NCL_CMD_XLC_PB_TYPE_FLAG;
	ncl_cmd->addr_param.common_param.list_len = width;
	ncl_cmd->addr_param.common_param.pda_list = pda;
	ncl_cmd->addr_param.common_param.info_list = info;
	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = NULL;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;

	ncl_cmd_submit(ncl_cmd);

	for (i = 0; i < width; i++) {
		if (info[i].status != 0)
			set_bit(i, df);
	}

	sys_free(FAST_DATA, pda);
	sys_free(FAST_DATA, info);
}

static fast_code void srb_bbt_scan(void)
{
	u32 spb_id;
	u32 df[FACTORY_DEFECT_DWORD_LEN];
	u32 er_df[FACTORY_DEFECT_DWORD_LEN];
	u32 i;

	log_level_t old;
	old = log_level_chg(LOG_ALW);// To disable NCL NF err print
	for (spb_id = 0; spb_id < spb_total; spb_id++) {
		memset(df, 0, sizeof(df));
		memset(er_df, 0, sizeof(er_df));
		if (vdisk_erase_enable)
			vdisk_sync_erase(spb_id, er_df);

		// pl->lun->ce->ch
		ncl_spb_defect_scan(spb_id, df);

		for (i = 0; i < FACTORY_DEFECT_DWORD_LEN; i++)
			df[i] |= er_df[i];

		// clear bit for valid blk
		srb_trans_to_mr_df(&defect_bitmap[spb_id], df);
	}
	log_level_chg(old);
}
static fast_code section_t *get_section(u32 section_id)
{
	image_t *image = (image_t *)dtag2mem(sb_dus[fwb_mode == true ? 1 : 0]); ///<get image hdr address
	if (image->signature == IMAGE_SIGNATURE) {
		int i = 0;
		u32 section_crc = crc32(image->sections, image->section_num * sizeof(section_t));
		if (section_crc != image->section_csum)
			return NULL;

		for (i = 0; i < image->section_num; i++) {
			section_t *section = &image->sections[i];
			if (section->identifier == section_id) {
				disp_apl_trace(LOG_INFO, 0xe0a9, "get section 0x%x, len %d, offset %x",
					section->identifier, section->length, section->offset);
				return section;
			}
		}
	}
	return NULL;
}

static fast_code void get_section_data_from_img(section_t *sect, void *tgt_mem, u8 dtag_idx)
{
	void *mem = NULL;
	u32 remain_len = sect->length - DTAG_SZE * dtag_idx;
	u32 dtag_off = (sect->offset >> DTAG_SHF) + dtag_idx;
	u32 sect_off_in_dtag = sect->offset & DTAG_MSK;
	mem = dtag2mem(sb_dus[dtag_off + dtag_idx + ((fwb_mode == true)? 1 : 0)]);
	mem = mem + (sect->offset & DTAG_MSK);
	if (remain_len < (DTAG_SZE - sect_off_in_dtag)) {
		///< sect length in one dtag
		memcpy(tgt_mem, mem, sect->length);
		disp_apl_trace(LOG_INFO, 0xf5da, "[single DU %d] 0x%x - 0x%x", dtag_idx, *(u32 *)tgt_mem, *(u32 *)(tgt_mem + remain_len - 4));
	} else {
		///< sect length cross two dtags
		memcpy(tgt_mem, mem, DTAG_SZE - sect_off_in_dtag);
		mem = dtag2mem(sb_dus[dtag_off + dtag_idx + 1 + ((fwb_mode == true) ? 1 : 0)]);
		memcpy(tgt_mem + DTAG_SZE - sect_off_in_dtag, mem, sect_off_in_dtag);
		disp_apl_trace(LOG_INFO, 0x4371, "[cross DU %d] 0x%x - 0x%x", dtag_idx, *(u32 *)tgt_mem, *(u32 *)(tgt_mem + DTAG_SZE - 4));
	}
}

static fast_code void pkey_build(rda_t pkey_rda, rda_t pkey_rda_dual)
{
	rda_t rda;
	section_t *pkey_sect;
	int i = 0, j = 0, mode = 0;
	int pkey_du_cnt;
	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	bm_pl_t pl[SRB_MR_DU_CNT_PAGE];
	void *mem;

	pkey_sect = get_section(ID_PKEY);
	sys_assert(pkey_sect);
	pkey_du_cnt = (pkey_sect->length + SRB_MR_DU_SZE) / SRB_MR_DU_SZE;
	mem = sys_malloc_aligned(SLOW_DATA, SRB_MR_DU_CNT_PAGE * DTAG_SZE, DTAG_SZE);
	sys_assert(mem);
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++) {
		dbase[i] = mem2dtag((mem + (i << DTAG_SHF)));
		pl[i].all = 0;
		pl[i].pl.dtag = WVTAG_ID;
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

	for (mode = 0; mode < 2; mode++) {
		rda = (mode == 0) ? pkey_rda : pkey_rda_dual;
		for (i = 0; i < pkey_du_cnt; i += SRB_MR_DU_CNT_PAGE) {
			rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

			//nvme_apl_trace(LOG_ALW, 0, "Program %s Row(0x%x)@CH/CE(%d/%d)",	(mode == 0 ? "_PKEY_" : "_PKEYD_"), rda.row, rda.ch, rda.dev);
			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
				tflush_rdas[j] = rda;
				tflush_rdas[j].du_off = j;

				if (i + j >= pkey_du_cnt) {
					pl[j].pl.dtag = WVTAG_ID;
				} else {
					pl[j].pl.dtag = dbase[j].dtag;
					memset(mem, 0x5A, SRB_MR_DU_SZE);
					get_section_data_from_img(pkey_sect, dtag2mem(dbase[j]), i + j);
				}
			}
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
			rda.row += 1;
		}
	}
	sys_free_aligned(SLOW_DATA, mem);
}

fast_code void otp_build(void)
{
	u32 i, value;
	value = regs_check_otp_sign();
	if (value != 0xFFFFFFFF) {
		disp_apl_trace(LOG_ERR, 0xfb4a, "rainier signature already programmed 0x%x\n", value);
		return;
	}
	section_t *kotp_sect;
	kotp_sect = get_section(ID_KOTP);
	sys_assert(kotp_sect);
	u32 *kopt = (u32 *)sys_malloc_aligned(SLOW_DATA, kotp_sect->length, 32);
	sys_assert(kopt);
	get_section_data_from_img(kotp_sect, kopt, 0);
	for (i = 0; i < kotp_sect->length / 16; i++) {
		disp_apl_trace(LOG_ERR, 0xb1d0, "KOTP %x: %x_%x_%x_%x\n", i * 16,
			kopt[0 + i * 4],
			kopt[1 + i * 4],
			kopt[2 + i * 4],
			kopt[3 + i * 4]);
	}

	save_pub_key_to_otp(kopt, kotp_sect->length, true);
	sys_free_aligned(SLOW_DATA, kopt);

}

static fast_code void pkey_verify(srb_t *srb, rda_t rda)
{
	int i, j;
	int pkey_du_cnt = (srb->srb_hdr.srb_pub_key_len + SRB_MR_DU_SZE) / SRB_MR_DU_SZE;
	u32 *mem;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void **)&mem);

	sys_assert(dtag.dtag != _inv_dtag.dtag);
	bm_pl_t pl[DU_CNT_PER_PAGE];
	for (i = 0 ; i < DU_CNT_PER_PAGE; i++) {
		pl[i].all = 0;
		pl[i].pl.dtag = dtag.b.dtag;
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

	for (i = 0; i < pkey_du_cnt; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if (i + j < pkey_du_cnt) {
				rda.du_off = j;
				memset(mem, 0, DTAG_SZE);
				//nvme_apl_trace(LOG_ALW, 0, "Verify %s Row(0x%x)@CH/CE(%d/%d)",	 "_PKEY_" , rda.row, rda.ch, rda.dev);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, pl, 1) == 0) {
					u32 idx;
					for (idx = 0; idx < srb->srb_hdr.srb_pub_key_len / 16; idx++) {
						disp_apl_trace(LOG_ERR, 0xcf8b, "PKEY %x: %x_%x_%x_%x\n", idx * 16,
							mem[0 + idx * 4],
							mem[1 + idx * 4],
							mem[2 + idx * 4],
							mem[3 + idx * 4]);
					}
				} else {
					disp_apl_trace(LOG_ERR, 0xaf2a, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
				}
			}
		}
		rda.row += 1;
	}

	dtag_put(DTAG_T_SRAM, dtag);
}
static fast_code void security_section_check(u32 *sec_onoff,u32 *sec_mode)
{
	int check_bits = 0;
	u32 onoff = SECURITY_DISABLE;
	u32 mode = 0;
	image_t *image = (image_t *)dtag2mem(sb_dus[fwb_mode == true ? 1 : 0]); ///<get image hdr address
	int i = 0;
	if (image->signature == IMAGE_SIGNATURE) {
		u32 section_crc = crc32(image->sections, image->section_num * sizeof(section_t));
		if (section_crc != image->section_csum)
			goto check_done;
		for (i = 0; i < image->section_num; i++) {
			section_t *section = &image->sections[i];
			if (section->identifier == ID_PKEY) {
				check_bits |= BIT(0);
				continue;
			}
			if (section->identifier == ID_KOTP) {
				check_bits |= BIT(1);
				continue;
			}
			if (section->identifier == ID_SHA3_256) {
				check_bits |= BIT(2);
				continue;
			}
			if ((section->identifier == ID_SHA256) || (section->identifier == ID_SM2_ZA)) {
				check_bits |= BIT(3);
				continue;
			}
			if ((section->identifier == ID_SIGN) || (section->identifier == ID_SM2_SIGN)) {
				check_bits |= BIT(4);
				mode = (section->identifier == ID_SM2_SIGN ? SM_SECURITY_MODE : RS_SECURITY_MODE);
				continue;
			}
		}
	}

	if (mode == SM_SECURITY_MODE && ((BIT(0) | BIT(1) | BIT(3) | (BIT(4))) & check_bits))
		onoff = SECURITY_ENABLE;
	if (mode == RS_SECURITY_MODE && (0x1F & check_bits))
		onoff = SECURITY_ENABLE;
check_done:
	*sec_onoff = onoff;
	*sec_mode = mode;
}

static fast_code void fwb_build(rda_t fwb_pri, rda_t fwb_sec)
{
	fwb_t *fwb;
	rda_t rda = fwb_pri;
	rda_t rda_mirror = fwb_sec;
	dtag_t dtag = dtag_get_urgt(DTAG_T_SRAM, (void **)&fwb);

	rda_erase(fwb_pri);
	rda_erase(fwb_sec);

	disp_apl_trace(LOG_ALW, 0xb424, "firmware commit to slot [%d]", fw_slot);

	sys_assert((fw_slot >= 1) && (fw_slot <= 7));
	memset((void *) fwb, 0, DTAG_SZE);
	fw_slot--;

	fwb->signature = 0x46524D42;
	fwb->active_slot = fw_slot;
	fwb->total_slot = fw_max_slot;

	fwb->fw_slot[fw_slot].fw_slot_du_cnt = (fw_end_du - fw_start_du + 1); /* in SRB_MR_DU_SZE unit size */
	u32 fw_du_cnt = fwb->fw_slot[fw_slot].fw_slot_du_cnt;
	u32 fw_crc = ~0U;
	int i = 0;
	for (i = fw_start_du; i < fw_end_du; i++)
		fw_crc = crc32_cont(dtag2mem(sb_dus[i]), DTAG_SZE, fw_crc, false);
	fw_verify_image_crc(sb_dus[fw_end_du], &fw_crc, &fwb->fw_slot[fw_slot].fw_slot_version);

	/* position */
	fwb->fw_slot[fw_slot].fw_slot = fwb_pri;
	fwb->fw_slot[fw_slot].fw_slot.row = fwb_pri.row + 1;  /* revised */

	fwb->fw_slot[fw_slot].fw_slot_mirror = fwb_sec;
	fwb->fw_slot[fw_slot].fw_slot_mirror.row = fwb_sec.row + 1; /* revised */

	/* Dual position */
	fwb->fw_slot[fw_slot].fw_slot_dual = fwb->fw_slot[fw_slot].fw_slot;
	fwb->fw_slot[fw_slot].fw_slot_dual.row += NR_PAGES_SLICE((fwb->fw_slot[fw_slot].fw_slot_du_cnt) * SRB_MR_DU_SZE);

	fwb->fw_slot[fw_slot].fw_slot_dual_mirror = fwb->fw_slot[fw_slot].fw_slot_mirror;
	fwb->fw_slot[fw_slot].fw_slot_dual_mirror.row += NR_PAGES_SLICE((fwb->fw_slot[fw_slot].fw_slot_du_cnt) * SRB_MR_DU_SZE);

	/* I. update FWB header */
	rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];
	bm_pl_t pl[DU_CNT_PER_PAGE];
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		pl[i].all = 0;
		pl[i].pl.dtag = (i == 0 ? dtag.b.dtag : WVTAG_ID);
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

	int j = 0;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			tflush_rdas[j] = (i == 0) ? rda : rda_mirror;
			tflush_rdas[j].du_off = j;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
	}

	rda.row = fwb_pri.row + 1;
	rda_mirror.row = fwb_sec.row + 1;

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	u32 size = SRB_MR_DU_CNT_PAGE * DTAG_SZE;
	void *mem = sys_malloc_aligned(SLOW_DATA, size, DTAG_SZE);
	sys_assert(mem);
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++)
		dbase[i] = mem2dtag((mem + (i << DTAG_SHF)));

	int slice = 0, n = 0;
	/* II. update the image content */
	for (i = 0; i < 2; i++) {
		rda_t trda = (i == 0) ? rda : rda_mirror;
		slice = 0;
		for (n = 0; n < fw_du_cnt; n += SRB_MR_DU_CNT_PAGE) {
			rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

			disp_apl_trace(LOG_ALW, 0x2f4f, "Program %s Row(0x%x)@CH/CE(%d/%d)",
					(i == 0 ? "_FW_" : "_FWD_"), trda.row, trda.ch, trda.dev);
			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
				tflush_rdas[j] = trda;
				tflush_rdas[j].du_off = j;

				if (n + j >= fw_du_cnt) {
					pl[j].pl.dtag = WVTAG_ID;
				} else {
					pl[j].pl.dtag = dbase[j].dtag;
					memcpy(dtag2mem(dbase[j]), dtag2mem(sb_dus[slice + fw_start_du]), DTAG_SZE);
					slice++;
				}
			}
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
			trda.row += 1;
		}
	}

	sys_free_aligned(SLOW_DATA, mem);
	dtag_put(DTAG_T_SRAM, dtag);

	/* III. Shall we close the block for SLC for retention */
}

static fast_code void sb_build(rda_t sb_rda, rda_t sb_rda_dual)
{
	rda_t rda;
	int i = 0, j = 0, mode = 0;
	int sb_du_cnt = sb_size_in_du();
	int slice = 0;

	dtag_t dbase[SRB_MR_DU_CNT_PAGE];
	u32 size;
	void *mem;

	size = SRB_MR_DU_CNT_PAGE * DTAG_SZE;
	mem = sys_malloc_aligned(SLOW_DATA, size, DTAG_SZE);
	sys_assert(mem);
	disp_apl_trace(LOG_ERR, 0xbd32, "size: 0x%x mem: 0x%x  fw du cnt: 0x%x\n", size, mem, sb_du_cnt);
	for (i = 0; i < SRB_MR_DU_CNT_PAGE; i++)
		dbase[i] = mem2dtag((mem + (i << DTAG_SHF)));

	bm_pl_t pl[DU_CNT_PER_PAGE];
	for (i = 0; i < DU_CNT_PER_PAGE; i++) {
		pl[i].all = 0;
		pl[i].pl.dtag = WVTAG_ID;
		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

	for (mode = 0; mode < 2; mode++) {
		rda = (mode == 0) ? sb_rda : sb_rda_dual;
		slice = 0;
		for (i = 0; i < sb_du_cnt; i += SRB_MR_DU_CNT_PAGE) {
			rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

			disp_apl_trace(LOG_ALW, 0xbfaf, "Program %s Row(0x%x)@CH/CE(%d/%d)",
					(mode == 0 ? "_SB_" : "_SBD_"), rda.row, rda.ch, rda.dev);
			for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
				tflush_rdas[j] = rda;
				tflush_rdas[j].du_off = j;

				if (i + j >= sb_du_cnt) {
					pl[j].pl.dtag = WVTAG_ID;
				} else {
					pl[j].pl.dtag = dbase[j].dtag;
					if (!sb_in_sram())
						memset(dtag2mem(dbase[j]), i + j, DTAG_SZE);
					else {
						memcpy(dtag2mem(dbase[j]), dtag2mem(sb_dus[slice + (fwb_mode ? 1 : 0)]), DTAG_SZE);
						slice++;
					}
				}
			}
			ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
			rda.row += 1;
		}
	}

	sys_free_aligned(SLOW_DATA, mem);
}

static fast_code void sb_verify(rda_t rda)
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

	for (i = 0; i < sb_du_cnt; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if (i + j < sb_du_cnt) {
				rda.du_off = j;
				memset(mem, 0, DTAG_SZE);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) == 0) {
					//disp_apl_trace(LOG_INFO, 0, "[%d] 0x%x - 0x%x", i + j, *mem, *(mem + 0x400 - 1));
				} else {
					disp_apl_trace(LOG_ERR, 0x15b4, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
				}
			}
		}
		rda.row += 1;
	}

	dtag_put(DTAG_T_SRAM, dtag);

}

static fast_code void srb_read_verify(rda_t rda, u32 du_cnt)
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
	disp_apl_trace(LOG_ERR, 0x541f, "verify rda row %x, CH/CE(%d/%d).\n", rda.row, rda.ch, rda.dev);
	for (i = 0; i < du_cnt; i += SRB_MR_DU_CNT_PAGE) {
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			if (i + j < du_cnt) {
				rda.du_off = j;
				memset(mem, 0, DTAG_SZE);
				if (ncl_access_mr(&rda, NCL_CMD_OP_READ, &pl, 1) == 0) {
					//disp_apl_trace(LOG_INFO, 0, "[%d] 0x%x - 0x%x", i + j, *mem, *(mem + 0x400 - 1));
				} else {
					disp_apl_trace(LOG_ERR, 0x700b, "Row(0x%x)@CH/CE(%d/%d) fail", rda.row, rda.ch, rda.dev);
				}
			}
		}
		rda.row += 1;
	}

	dtag_put(DTAG_T_SRAM, dtag);

}

static fast_code bool srb_scan(bool load_fbbt)
{
	u32 pln;
	u32 blkno;
	srb_t *srb;
	bool retval = true;

	if (load_fbbt)
		irq_disable();

	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void *)&srb);

	if (dtag.dtag == _inv_dtag.dtag)
		sys_assert(0);

	memset(dtag2mem(dtag), 0, DTAG_SZE);

	bm_pl_t pl = {
		.pl.dtag = dtag.b.dtag,
		.pl.du_ofst = 0,
		.pl.btag = 0,
		.pl.type_ctrl = DTAG_QID_DROP | META_SRAM_DTAG
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
			disp_apl_trace(LOG_ALW, 0x6297, "SRB hdr scan Blk(%d) Row(0x%x)@CH/CE(%d/%d)", blkno, srb_hdr_rda.row, srb_hdr_rda.ch, srb_hdr_rda.dev);
			if (ncl_access_mr(&srb_hdr_rda, NCL_CMD_OP_READ, &pl, 1) != 0)
				continue;

			u32 sig_hi = (srb->srb_hdr.srb_signature >> 16) >> 16;
			u32 sig_lo = srb->srb_hdr.srb_signature;

			disp_apl_trace(LOG_ERR, 0x4a75, "SRB Signature -> 0x%x%x", sig_hi, sig_lo);

			if (srb->srb_hdr.srb_signature == SRB_SIGNATURE &&
					(srb->srb_hdr.srb_csum == crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum))))
			{
				disp_apl_trace(LOG_ALW, 0x206b, "SRB hdr founded");
				if (load_fbbt) {
					disp_apl_trace(LOG_ALW, 0x0c2d, "Loading fbbt if any ...");
					if (srb->dftb_sz != defect_bitmap_size) {
						disp_apl_trace(LOG_ALW, 0x7cd6, "SRB fbbt length(%x -> %x), consider incomplete.", defect_bitmap_size, srb->dftb_sz);
						retval = false;
						goto out;
					}

					retval = srb_load_fbbt(defect_bitmap, defect_bitmap_size, srb->dftb_pos, NR_DUS_SLICE(defect_bitmap_size));
					if (retval == false) {
						disp_apl_trace(LOG_ALW, 0xbb70, "Trying mirror fbbt ...");
						retval = srb_load_fbbt(defect_bitmap, defect_bitmap_size, srb->dftb_m_pos, NR_DUS_SLICE(defect_bitmap_size));
					}

					if (retval == true) {
						/* Free Current MR block for reusable */
						/* SB residents in MR block */
						u8 _pln, _lun;
						u16 _blk;

						row2nda(srb->srb_hdr.srb_sb_row, &_pln, &_blk, &_lun);
						u32 pos = _pln + (_lun << LUN_SHF) + (dev << CE_SHF) + (ch << CH_SHF);

						defect_bitmap[_blk].dft_bitmap[pos >> 5] &= ~(1 << (pos & 0x1F));

						row2nda(srb->srb_hdr.srb_sb_row_mirror, &_pln, &_blk, &_lun);
						pos = _pln + (_lun << LUN_SHF) + (dev << CE_SHF) + (ch << CH_SHF);
						defect_bitmap[_blk].dft_bitmap[pos >> 5] &= ~(1 << (pos & 0x1F));

						/* In case of wrong usage, clean the found one too */
						pos = pln + (dev << CE_SHF) + (ch << CH_SHF);
						defect_bitmap[blkno].dft_bitmap[pos >> 5] &= ~(1 << (pos & 0x1F));

						/* Free fw buffer block for reusable */
						row2nda(srb->fwb_buf_pos.row, &_pln, &_blk, &_lun);
						pos = _pln + (_lun << LUN_SHF) + (srb->fwb_buf_pos.dev << CE_SHF) + (srb->fwb_buf_pos.ch << CH_SHF);
						defect_bitmap[_blk].dft_bitmap[pos >> 5] &= ~(1 << (pos & 0x1F));

						disp_apl_trace(LOG_ALW, 0x6bed, "SRB fbbt loaded @ row %x CH/CE(%d/%d)", srb->dftb_pos.row, srb->dftb_pos.ch, srb->dftb_pos.dev);

						fwb_pri_pos = srb->fwb_pri_pos;
						fwb_sec_pos = srb->fwb_sec_pos;
						ftlb_pri_pos = srb->ftlb_pri_pos;
						ftlb_sec_pos = srb->ftlb_sec_pos;
						evtb_pi_pos = srb->evtb_pi_pos;
						evtb_po_pos = srb->evtb_po_pos;
					} else
						disp_apl_trace(LOG_ALW, 0x1f5f, "SRB fbbt load fail");
				}
				goto out;
			}
		}
	}

	retval = false;
out:
	dtag_put(DTAG_T_SRAM, dtag);

	return retval;
}

static fast_code void srb_init_rdas_buf(rda_t *tflush_rdas, rda_t addr, bm_pl_t *pl, u8 init_cnt)
{
	u8 j;
	for (j = 0; j < init_cnt; j++) {
		tflush_rdas[j] = addr;
		tflush_rdas[j].du_off = j;
		pl[j].all = 0;
		pl[j].pl.dtag = WVTAG_ID;
		pl[j].pl.du_ofst = j;
		pl[j].pl.btag = 0;
		pl[j].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
	}

}

static fast_code void srb_build(void)
{
	bool ret = 1;

	/* Search MR block candidates */
	u32 i = 0;
	rda_t srb_hdr_rda, srb_hdr_rda_mirror;
	ret &= srb_alloc_block(defect_bitmap, true, &srb_hdr_rda);
	ret &= srb_alloc_block(defect_bitmap, true, &srb_hdr_rda_mirror);

	if (ret) {
		u8 pln, lun;
		u16 blk;

		row2nda(srb_hdr_rda.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0xb876, "MR  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb_hdr_rda.ch, srb_hdr_rda.dev);

		row2nda(srb_hdr_rda_mirror.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0x2ebd, "MR(m)  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb_hdr_rda_mirror.ch, srb_hdr_rda_mirror.dev);

		rda_erase(srb_hdr_rda);
		rda_erase(srb_hdr_rda_mirror);
	} else {
		panic("no valid block for SRB HEADER\n");
	}

	srb_t *srb, *srb_mirror;
	dtag_t dtag = dtag_get(DTAG_T_SRAM, (void *) &srb);
	dtag_t dtag_mirror = dtag_get(DTAG_T_SRAM, (void*) &srb_mirror);

	sys_assert(dtag.dtag != _inv_dtag.dtag);
	sys_assert(dtag_mirror.dtag != _inv_dtag.dtag);

	memset(srb, 0, SRB_MR_DU_SZE);
	memset(srb_mirror, 0, SRB_MR_DU_SZE);

	srb->srb_hdr.srb_signature = SRB_SIGNATURE;
	srb_mirror->srb_hdr.srb_signature = SRB_SIGNATURE;

	/* Tweak UART deviation */
	srb->srb_hdr.srb_clk_deviation  = 0;
	srb_mirror->srb_hdr.srb_clk_deviation  = 0;

	u8 ch_idx, ce_idx, lun_idx;
	srb->ndphy_dll_valid = true;
	for (ch_idx = 0; ch_idx < nand_channel_num(); ch_idx++) {
		for (ce_idx = 0; ce_idx < nand_target_num(); ce_idx++) {
			for (lun_idx = 0; lun_idx < nand_lun_num(); lun_idx++) {
				srb->ndphy_dll_set[ch_idx][ce_idx][lun_idx] = ndphy_dll_cali[ch_idx][ce_idx][lun_idx];
				disp_apl_trace(LOG_ALW, 0xdc00, "save dll %b on ch %d ce %d lun %d", srb->ndphy_dll_set[ch_idx][ce_idx][lun_idx], 
					ce_idx, ce_idx, lun_idx);
			}
		}
	}

	if (load_old_mr == true) {
		disp_apl_trace(LOG_ALW, 0x91b7, "Using old MR parameters: %d", load_old_mr);
		srb->fwb_pri_pos = fwb_pri_pos;
		srb->fwb_sec_pos = fwb_sec_pos;
		srb->ftlb_pri_pos = ftlb_pri_pos;
		srb->ftlb_sec_pos = ftlb_sec_pos;
		srb->evtb_pi_pos = evtb_pi_pos;
		srb->evtb_po_pos = evtb_po_pos;
		srb_mirror->fwb_pri_pos = fwb_pri_pos;
		srb_mirror->fwb_sec_pos = fwb_sec_pos;
		srb_mirror->ftlb_pri_pos = ftlb_pri_pos;
		srb_mirror->ftlb_sec_pos = ftlb_sec_pos;
		srb_mirror->evtb_pi_pos = evtb_pi_pos;
		srb_mirror->evtb_po_pos = evtb_po_pos;

		u8 pln, lun;
		u16 blk;

		row2nda(ftlb_pri_pos.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0x9270, "(O) FTL B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, ftlb_pri_pos.ch, ftlb_pri_pos.dev);
		row2nda(ftlb_sec_pos.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0x64e9, "(O) FTL(m) B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, ftlb_sec_pos.ch, ftlb_sec_pos.dev);

		row2nda(evtb_pi_pos.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0x15a2, "(O) EVT(pi) B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, evtb_pi_pos.ch, evtb_pi_pos.dev);
		row2nda(evtb_po_pos.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0x44b6, "(O) EVT(po) B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, evtb_po_pos.ch, evtb_po_pos.dev);

		load_old_mr = false;
	} else {
		ret &= srb_alloc_block(defect_bitmap, false, &srb->ftlb_pri_pos);
		ret &= srb_alloc_block(defect_bitmap, false, &srb->ftlb_sec_pos);

		ftlb_pri_pos = srb_mirror->ftlb_pri_pos = srb->ftlb_pri_pos;
		fwb_sec_pos = srb_mirror->ftlb_sec_pos = srb->ftlb_sec_pos;

		if (ret) {
			u8 pln, lun;
			u16 blk;

			row2nda(srb->ftlb_pri_pos.row, &pln, &blk, &lun);
			disp_apl_trace(LOG_ALW, 0xf8b7, "FTL  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->ftlb_pri_pos.ch, srb->ftlb_pri_pos.dev);
			row2nda(srb->ftlb_sec_pos.row, &pln, &blk, &lun);
			disp_apl_trace(LOG_ALW, 0x56ff, "FTL(m)  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->ftlb_sec_pos.ch, srb->ftlb_sec_pos.dev);

			rda_erase(srb->ftlb_pri_pos);
			rda_erase(srb->ftlb_sec_pos);
		} else {
			panic("no valid block for FTLB\n");
		}

		ret &= srb_alloc_block(defect_bitmap, false, &srb->fwb_pri_pos);
		ret &= srb_alloc_block(defect_bitmap, false, &srb->fwb_sec_pos);

		srb_mirror->fwb_pri_pos = srb->fwb_pri_pos;
		srb_mirror->fwb_sec_pos = srb->fwb_sec_pos;

		if (ret) {
			u8 pln, lun;
			u16 blk;

			row2nda(srb->fwb_pri_pos.row, &pln, &blk, &lun);
			disp_apl_trace(LOG_ALW, 0xcd37, "FWB  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->fwb_pri_pos.ch, srb->fwb_pri_pos.dev);
			row2nda(srb->fwb_sec_pos.row, &pln, &blk, &lun);
			disp_apl_trace(LOG_ALW, 0x7853, "FWB(m) B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->fwb_sec_pos.ch, srb->fwb_sec_pos.dev);
		} else {
			//panic("no valid block for FWB\n");
		}

		ret &= srb_alloc_block(defect_bitmap, false, &srb->evtb_pi_pos);
		ret &= srb_alloc_block(defect_bitmap, false, &srb->evtb_po_pos);

		srb_mirror->evtb_pi_pos = srb->evtb_pi_pos;
		srb_mirror->evtb_po_pos = srb->evtb_po_pos;

		if (ret) {
			u8 pln, lun;
			u16 blk;

			row2nda(srb->evtb_pi_pos.row, &pln, &blk, &lun);
			disp_apl_trace(LOG_ALW, 0x942f, "EVTB(pi)  B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->evtb_pi_pos.ch, srb->evtb_pi_pos.dev);
			row2nda(srb->evtb_po_pos.row, &pln, &blk, &lun);
			disp_apl_trace(LOG_ALW, 0x358c, "EVTB(po) B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->evtb_po_pos.ch, srb->evtb_po_pos.dev);

			rda_erase(srb->evtb_pi_pos);
			rda_erase(srb->evtb_po_pos);

		} else {
			panic("no valid block for EVTB\n");
		}
	}

	ret = srb_alloc_block(defect_bitmap, false, &srb->fwb_buf_pos);
	srb_mirror->fwb_buf_pos = srb->fwb_buf_pos;

	if (ret) {
		u8 pln, lun;
		u16 blk;

		row2nda(srb->fwb_buf_pos.row, &pln, &blk, &lun);
		disp_apl_trace(LOG_ALW, 0x0181, "FWB(buffer) B(%d) PL(%d) LUN (%d) @CH/CE(%d/%d)", blk, pln, lun, srb->fwb_buf_pos.ch, srb->fwb_buf_pos.dev);
	} else {
		panic("no valid block for FW buffer block\n");
	}

	/* Calculate Row address for each section */
	rda_t dftb_rda = srb_hdr_rda;
	rda_t dftb_rda_mirror = srb_hdr_rda_mirror;

	dftb_rda.row = srb_hdr_rda.row + slc_pgs_per_blk - NR_PAGES_SLICE(defect_bitmap_size);
	dftb_rda_mirror.row = srb_hdr_rda_mirror.row + slc_pgs_per_blk - NR_PAGES_SLICE(defect_bitmap_size);

	int enc_cmf_pgs = NR_PAGES_SLICE(enc_cmf_sze);
	int dec_cmf_pgs = NR_PAGES_SLICE(dec_cmf_sze);

	row_t dec_cmf_row = dftb_rda.row - dec_cmf_pgs;
	row_t dec_cmf_row_mirror = dftb_rda_mirror.row - dec_cmf_pgs;
	row_t enc_cmf_row = dec_cmf_row - enc_cmf_pgs;
	row_t enc_cmf_row_mirror = dec_cmf_row_mirror - enc_cmf_pgs;

	sb_du_cnt = sb_size_in_du();
	int sb_du_pgs = NR_PAGES_SLICE(sb_du_cnt * SRB_MR_DU_SZE);

	row_t sb_row_dual = enc_cmf_row - sb_du_pgs;
	row_t sb_row_dual_mirror = enc_cmf_row_mirror - sb_du_pgs;
	row_t sb_row = sb_row_dual - sb_du_pgs;
	row_t sb_row_mirror = sb_row_dual_mirror - sb_du_pgs;

	u8 is_security_img = 0;
	u32 sec_enable = 0, sec_mode = 0;
	int pkey_pgs = 0;
	row_t pkey_row_dual;
	row_t pkey_row_dual_mirror;
	row_t pkey_row;
	row_t pkey_row_mirror;
	int srb_hdr_pgs ;
	security_section_check(&sec_enable,&sec_mode);

	srb->srb_hdr.srb_security_enable = sec_enable;
	srb->srb_hdr.srb_security_mode = sec_mode;

	disp_apl_trace(LOG_ALW, 0x8506, "sec_enable:0x%x sec_mode:0x%x\n",sec_enable,sec_mode);
	if (srb->srb_hdr.srb_security_enable == SECURITY_ENABLE) {
		is_security_img = 1;
		pkey_pgs = NR_PAGES_SLICE(MAX_PUBLIC_KEY_LEN);
		pkey_row_dual = sb_row - pkey_pgs;
		pkey_row_dual_mirror = sb_row_mirror - pkey_pgs;
		pkey_row = pkey_row_dual - pkey_pgs;
		pkey_row_mirror = pkey_row_dual_mirror - pkey_pgs;
		disp_apl_trace(LOG_ALW, 0xf22e, "sec is enabled");
		/*     fw config x2         Loader x2        Pkeyx2  Encoder       Decoder            BBT */
		srb_hdr_pgs = slc_pgs_per_blk - FW_CONFIG_DUS * 2 - pkey_pgs * 2 - sb_du_pgs * 2 - enc_cmf_pgs - dec_cmf_pgs - NR_PAGES_SLICE(defect_bitmap_size);
		disp_apl_trace(LOG_ALW, 0x7937, "SRBH(0~0x%x) C -> {SB(0x%x) SBD(0x%x) ECMF(0x%x) DCMF(0x%x) DBBT(0x%x)}",
			srb_hdr_pgs - 1, sb_row, sb_row_dual, enc_cmf_row, dec_cmf_row, dftb_rda.row);
		disp_apl_trace(LOG_ALW, 0x74df, "SRBH(m)(0~0x%x) C -> {SB(0x%x) SBD(0x%x) ECMF(0x%x) DCMF(0x%x) DBBT(0x%x)}",
			srb_hdr_pgs - 1, sb_row_mirror, sb_row_dual_mirror, enc_cmf_row_mirror, dec_cmf_row_mirror, dftb_rda_mirror.row);
		disp_apl_trace(LOG_ALW, 0x5a9d, "security image\n");
		srb->srb_hdr.srb_config_row = pkey_row - FW_CONFIG_DUS * 2;
		srb->srb_hdr.srb_config_row_dual = pkey_row - FW_CONFIG_DUS;
		srb_mirror->srb_hdr.srb_config_row = pkey_row_mirror - FW_CONFIG_DUS * 2;
		srb_mirror->srb_hdr.srb_config_row_dual = pkey_row_mirror - FW_CONFIG_DUS;
	} else {
		/*     fw config x2         Loader x2         Encoder       Decoder            BBT */
		srb_hdr_pgs = slc_pgs_per_blk - FW_CONFIG_DUS * 2 - sb_du_pgs * 2 - enc_cmf_pgs - dec_cmf_pgs - NR_PAGES_SLICE(defect_bitmap_size);
		disp_apl_trace(LOG_ALW, 0xdaa5, "SRBH(0~0x%x) C -> {SB(0x%x) SBD(0x%x) ECMF(0x%x) DCMF(0x%x) DBBT(0x%x)}",
		      srb_hdr_pgs - 1, sb_row, sb_row_dual, enc_cmf_row, dec_cmf_row, dftb_rda.row);
		disp_apl_trace(LOG_ALW, 0xab0a, "SRBH(m)(0~0x%x) C -> {SB(0x%x) SBD(0x%x) ECMF(0x%x) DCMF(0x%x) DBBT(0x%x)}",
		      srb_hdr_pgs - 1, sb_row_mirror, sb_row_dual_mirror, enc_cmf_row_mirror, dec_cmf_row_mirror, dftb_rda_mirror.row);
		srb->srb_hdr.srb_config_row = sb_row - FW_CONFIG_DUS * 2;
		srb->srb_hdr.srb_config_row_dual = sb_row - FW_CONFIG_DUS;
		srb_mirror->srb_hdr.srb_config_row = sb_row_mirror - FW_CONFIG_DUS * 2;
		srb_mirror->srb_hdr.srb_config_row_dual = sb_row_mirror - FW_CONFIG_DUS;
	}

	srb->srb_hdr.srb_sb_row = sb_row;
	srb->srb_hdr.srb_sb_row_dual = sb_row_dual;
	srb_mirror->srb_hdr.srb_sb_row = sb_row_mirror;
	srb_mirror->srb_hdr.srb_sb_row_dual = sb_row_dual_mirror;

	srb->srb_hdr.srb_sb_row_mirror = srb_hdr_rda_mirror.row + (sb_row - srb_hdr_rda.row);
	srb->srb_hdr.srb_sb_row_dual_mirror = srb_hdr_rda_mirror.row + (sb_row_dual - srb_hdr_rda.row);
	srb_mirror->srb_hdr.srb_sb_row_mirror = srb_hdr_rda.row + (sb_row - srb_hdr_rda.row);
	srb_mirror->srb_hdr.srb_sb_row_dual_mirror = srb_hdr_rda.row + (sb_row_dual - srb_hdr_rda.row);

	if (is_security_img) {
		srb->srb_hdr.srb_pub_key = pkey_row;
		srb->srb_hdr.srb_pub_key_dual = pkey_row_dual;
		srb_mirror->srb_hdr.srb_pub_key = pkey_row_mirror;
		srb_mirror->srb_hdr.srb_pub_key_dual = pkey_row_dual_mirror;

		srb->srb_hdr.srb_pub_key_mirror = srb_hdr_rda_mirror.row + (pkey_row - srb_hdr_rda.row);
		srb->srb_hdr.srb_pub_key_dual_mirror = srb_hdr_rda_mirror.row + (pkey_row_dual - srb_hdr_rda.row);
		srb_mirror->srb_hdr.srb_pub_key_mirror = srb_hdr_rda.row + (pkey_row - srb_hdr_rda.row);
		srb_mirror->srb_hdr.srb_pub_key_dual_mirror = srb_hdr_rda.row + (pkey_row_dual - srb_hdr_rda.row);
		section_t *section = get_section(ID_PKEY);
		sys_assert(section);
		sys_assert(section->length <= MAX_PUBLIC_KEY_LEN);
		srb->srb_hdr.srb_pub_key_len = section->length;
		disp_apl_trace(LOG_ALW, 0x77b7, "srb->srb_hdr.srb_pub_key_len:0x%x", srb->srb_hdr.srb_pub_key_len);
		srb_mirror->srb_hdr.srb_pub_key_len = section->length;
	}
	srb->srb_hdr.srb_sb_du_cnt = sb_du_cnt;
	srb->srb_hdr.srb_sb_ep = SB_SRAM_ADDR; /* entry point */
	srb_mirror->srb_hdr.srb_sb_du_cnt = sb_du_cnt;
	srb_mirror->srb_hdr.srb_sb_ep = SB_SRAM_ADDR; /* entry point */

	srb->srb_hdr.srb_csum = crc32(&srb->srb_hdr, offsetof(srb_hdr_t, srb_csum));
	srb_mirror->srb_hdr.srb_csum = crc32(&srb_mirror->srb_hdr, offsetof(srb_hdr_t, srb_csum));

	srb->srb_hdr.srb_sb_du_in_page = SRB_MR_DU_CNT_PAGE;
	disp_apl_trace(LOG_ALW, 0x0113, "SRB Header CRC(0x%x)", srb->srb_hdr.srb_csum);
	srb_mirror->srb_hdr.srb_sb_du_in_page = SRB_MR_DU_CNT_PAGE;
	disp_apl_trace(LOG_ALW, 0x0813, "SRB(m) Header CRC(0x%x)", srb_mirror->srb_hdr.srb_csum);

	srb->dftb_pos = dftb_rda;
	srb->dftb_sz = defect_bitmap_size;
	srb->dftb_m_pos = dftb_rda_mirror;
	srb_mirror->dftb_pos = dftb_rda_mirror;
	srb_mirror->dftb_sz = defect_bitmap_size;
	srb_mirror->dftb_m_pos = dftb_rda;

	rda_t dcmf_rda = srb_hdr_rda;
	rda_t dcmf_rda_mirror = srb_hdr_rda_mirror;

	dcmf_rda.row = dec_cmf_row;
	dcmf_rda_mirror.row = dec_cmf_row_mirror;

	srb->dec_pos = dcmf_rda;
	srb->dec_sz = dec_cmf_sze;
	srb_mirror->dec_pos = dcmf_rda_mirror;
	srb_mirror->dec_sz = dec_cmf_sze;

	rda_t ecmf_rda = srb_hdr_rda;
	rda_t ecmf_rda_mirror = srb_hdr_rda_mirror;

	ecmf_rda.row = enc_cmf_row;
	ecmf_rda_mirror.row = enc_cmf_row_mirror;

	srb->enc_pos = ecmf_rda;
	srb->enc_sz = enc_cmf_sze;
	srb_mirror->enc_pos = ecmf_rda_mirror;
	srb_mirror->enc_sz = enc_cmf_sze;

	rda_t sb_rda = srb_hdr_rda;
	rda_t sb_rda_mirror = srb_hdr_rda_mirror;
	rda_t sb_rda_dual = srb_hdr_rda;
	rda_t sb_rda_dual_mirror = srb_hdr_rda_mirror;

	sb_rda.row = sb_row;
	sb_rda_dual.row = sb_row_dual;
	sb_rda_mirror.row = sb_row_mirror;
	sb_rda_dual_mirror.row = sb_row_dual_mirror;

	rda_t pkey_rda = srb_hdr_rda;
	rda_t pkey_rda_mirror = srb_hdr_rda_mirror;
	rda_t pkey_rda_dual = srb_hdr_rda;
	rda_t pkey_rda_dual_mirror = srb_hdr_rda_mirror;

	if (is_security_img) {
		pkey_rda.row = pkey_row;
		pkey_rda_dual.row = pkey_row_dual;
		pkey_rda_mirror.row = pkey_row_mirror;
		pkey_rda_dual_mirror.row = pkey_row_dual_mirror;
	}

	int j;
	/* Program SRB Header */
	disp_apl_trace(LOG_ALW, 0xa24f, "Program _SRBH_ Row(0x0~0x%x)@CH/CE(%d/%d)", srb_hdr_rda.row + srb_hdr_pgs - 1, srb_hdr_rda.ch, srb_hdr_rda.dev);
	rda_t tflush_rdas[DU_CNT_PER_PAGE];
	bm_pl_t pl[DU_CNT_PER_PAGE];

	for (i = 0; i < srb_hdr_pgs; i++) {
		srb_init_rdas_buf(tflush_rdas, srb_hdr_rda, pl, DU_CNT_PER_PAGE);
		pl[0].pl.dtag = dtag.b.dtag;
		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		srb_hdr_rda.row++;
	}

	disp_apl_trace(LOG_ALW, 0x1fb2, "Program _SRBH_(m) Row(0x0~0x%x)@CH/CE(%d/%d)", srb_hdr_rda_mirror.row + srb_hdr_pgs - 1, srb_hdr_rda_mirror.ch, srb_hdr_rda_mirror.dev);
	for (i = 0; i < srb_hdr_pgs; i++) {
		srb_init_rdas_buf(tflush_rdas, srb_hdr_rda_mirror, pl, DU_CNT_PER_PAGE);
		pl[0].pl.dtag = dtag_mirror.b.dtag;
		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		srb_hdr_rda_mirror.row = srb_hdr_rda_mirror.row + 1;
	}

	/* Program SRB fw config */
	disp_apl_trace(LOG_ALW, 0xdd4b, "Program FWCONFIG Row(0x%x)@CH/CE(%d/%d)", srb_hdr_rda.row, srb_hdr_rda.ch, srb_hdr_rda.dev);
	srb_init_rdas_buf(tflush_rdas, srb_hdr_rda, pl, DU_CNT_PER_PAGE);
	pl[0].pl.dtag = (fw_config_dtag.dtag != _inv_dtag.dtag) ? fw_config_dtag.b.dtag : WVTAG_ID;
	ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
	srb_hdr_rda.row = srb_hdr_rda.row + 1;
	srb_init_rdas_buf(tflush_rdas, srb_hdr_rda, pl, DU_CNT_PER_PAGE);
	pl[0].pl.dtag = (fw_config_dtag.dtag != _inv_dtag.dtag) ? fw_config_dtag.b.dtag : WVTAG_ID;
	ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);

	disp_apl_trace(LOG_ALW, 0xf8c5, "Program FWCONFIG(m) Row(0x%x)@CH/CE(%d/%d)", srb_hdr_rda_mirror.row, srb_hdr_rda_mirror.ch, srb_hdr_rda_mirror.dev);
	srb_init_rdas_buf(tflush_rdas, srb_hdr_rda_mirror, pl, DU_CNT_PER_PAGE);
	pl[0].pl.dtag = (fw_config_dtag.dtag != _inv_dtag.dtag) ? fw_config_dtag.b.dtag : WVTAG_ID;
	ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
	srb_hdr_rda_mirror.row = srb_hdr_rda_mirror.row + 1;
	srb_init_rdas_buf(tflush_rdas, srb_hdr_rda_mirror, pl, DU_CNT_PER_PAGE);
	pl[0].pl.dtag = (fw_config_dtag.dtag != _inv_dtag.dtag) ? fw_config_dtag.b.dtag : WVTAG_ID;
	ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);

	if (is_security_img) {
		///< Program sectury public key to OTP
		otp_build();

		///< Program public key to SRB
		disp_apl_trace(LOG_ALW, 0x7a59, "Program _PKEY_Row(0x%x) dual Row (0x%x), length %d @CH/CE(%d/%d)",
			srb->srb_hdr.srb_pub_key, srb->srb_hdr.srb_pub_key_dual, srb->srb_hdr.srb_pub_key_len, pkey_rda.ch, pkey_rda.dev);
		pkey_build(pkey_rda, pkey_rda_dual);

		disp_apl_trace(LOG_ALW, 0xbe01, "Program _PKEY(m)_Row(0x%x) dual Row (0x%x), length %d @CH/CE(%d/%d)",
			srb->srb_hdr.srb_pub_key_mirror, srb->srb_hdr.srb_pub_key_dual_mirror, srb->srb_hdr.srb_pub_key_len, pkey_rda_mirror.ch, pkey_rda_mirror.dev);
		pkey_build(pkey_rda_mirror, pkey_rda_dual_mirror);
	}

	sb_build(sb_rda, sb_rda_dual);
	sb_build(sb_rda_mirror, sb_rda_dual_mirror);
	disp_apl_trace(LOG_ALW, 0xd465, "Rainier Loader Program Done.");

	if (fwb_mode) {
		fwb_build(srb->fwb_pri_pos, srb->fwb_sec_pos);
		disp_apl_trace(LOG_ALW, 0xc094, "Rainier Firmware Program Done.");
	}

	dtag_put_bulk(DTAG_T_SRAM, sb_du_amt, sb_dus);

	/* Program CMF: Enc & Dec */
	int k;
	for (i = 0, k = 0; i < NR_PAGES_SLICE(enc_cmf_sze); i++) {
		disp_apl_trace(LOG_INFO, 0x43d4, "Program _ECMF_ Row(0x%x)@CH/CE(%d/%d)", ecmf_rda.row, ecmf_rda.ch, ecmf_rda.dev);
		srb_init_rdas_buf(tflush_rdas, ecmf_rda, pl, DU_CNT_PER_PAGE);

		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = ecmf_rda;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(enc_cmf_sze) ? enc_cmf_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		ecmf_rda.row += 1;
	}

	for (i = 0, k = 0; i < NR_PAGES_SLICE(enc_cmf_sze); i++) {

		disp_apl_trace(LOG_INFO, 0xa2b6, "Program _ECMF_(m) Row(0x%x)@CH/CE(%d/%d)", ecmf_rda_mirror.row, ecmf_rda_mirror.ch, ecmf_rda_mirror.dev);
		srb_init_rdas_buf(tflush_rdas, ecmf_rda_mirror, pl, DU_CNT_PER_PAGE);
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = ecmf_rda_mirror;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(enc_cmf_sze) ? enc_cmf_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		ecmf_rda_mirror.row += 1;
	}

	for (i = 0, k = 0; i < NR_PAGES_SLICE(dec_cmf_sze); i++) {
		disp_apl_trace(LOG_INFO, 0x5f15, "Program _DCMF_ Row(0x%x)@CH/CE(%d/%d)", dcmf_rda.row, dcmf_rda.ch, dcmf_rda.dev);
		srb_init_rdas_buf(tflush_rdas, dcmf_rda, pl, DU_CNT_PER_PAGE);
		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = dcmf_rda;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(dec_cmf_sze) ? dec_cmf_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		dcmf_rda.row += 1;
	}

	for (i = 0, k = 0; i < NR_PAGES_SLICE(dec_cmf_sze); i++) {
		disp_apl_trace(LOG_INFO, 0x424c, "Program _DCMF_(m) Row(0x%x)@CH/CE(%d/%d)", dcmf_rda_mirror.row, dcmf_rda_mirror.ch, dcmf_rda_mirror.dev);
		srb_init_rdas_buf(tflush_rdas, dcmf_rda_mirror, pl, DU_CNT_PER_PAGE);

		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = dcmf_rda_mirror;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(dec_cmf_sze) ? dec_cmf_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		dcmf_rda_mirror.row += 1;
	}

	/* Program Factory BBT */
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, NR_DUS_SLICE(defect_bitmap_size), dftb_dtags) == NR_DUS_SLICE(defect_bitmap_size));
	bf_to_dtags((void *)defect_bitmap, dftb_dtags, NR_DUS_SLICE(defect_bitmap_size));
	disp_apl_trace(LOG_ALW, 0x2171, "Dftb size(%d) -> #Dus(%d) #PGs(%d)", defect_bitmap_size, NR_DUS_SLICE(defect_bitmap_size), NR_PAGES_SLICE(defect_bitmap_size));

	rda_t dftb_rda_saved = dftb_rda;
	for (i = 0, k = 0; i < NR_PAGES_SLICE(defect_bitmap_size); i++) {
		disp_apl_trace(LOG_INFO, 0xc464, "Program _DFTB_ Row(0x%x)@CH/CE(%d/%d)", dftb_rda.row, dftb_rda.ch, dftb_rda.dev);
		srb_init_rdas_buf(tflush_rdas, dftb_rda, pl, DU_CNT_PER_PAGE);

		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = dftb_rda;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(defect_bitmap_size) ? dftb_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		dftb_rda.row += 1;
	}
	rda_t dftb_rda_mirror_saved = dftb_rda_mirror;
	for (i = 0, k = 0; i < NR_PAGES_SLICE(defect_bitmap_size); i++) {
		disp_apl_trace(LOG_INFO, 0xd1c2, "Program _DFTB_(m) Row(0x%x)@CH/CE(%d/%d)", dftb_rda_mirror.row, dftb_rda_mirror.ch, dftb_rda_mirror.dev);
		srb_init_rdas_buf(tflush_rdas, dftb_rda_mirror, pl, DU_CNT_PER_PAGE);

		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = dftb_rda_mirror;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(defect_bitmap_size) ? dftb_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_WRITE, pl, 1);
		dftb_rda_mirror.row += 1;
	}

	srb_scan(false);

	disp_apl_trace(LOG_ERR, 0xa827, "sb content:\n");
	sb_verify(sb_rda);
	disp_apl_trace(LOG_ERR, 0xdd89, "sb_dual content:\n");
	sb_verify(sb_rda_dual);
	disp_apl_trace(LOG_ERR, 0x49d1, "sb_mirror content:\n");
	sb_verify(sb_rda_mirror);
	disp_apl_trace(LOG_ERR, 0x34a7, "sb_dual_mirror content:\n");
	sb_verify(sb_rda_dual_mirror);

	if (is_security_img) {
		disp_apl_trace(LOG_ERR, 0x0502, "pkey content:\n");
		pkey_verify(srb, pkey_rda);
		disp_apl_trace(LOG_ERR, 0x53a3, "pkey_dual content:\n");
		pkey_verify(srb, pkey_rda_dual);
		disp_apl_trace(LOG_ERR, 0x4378, "pkey_mirror content:\n");
		pkey_verify(srb, pkey_rda_mirror);
		disp_apl_trace(LOG_ERR, 0x4ab3, "pkey_dual_mirror content:\n");
		pkey_verify(srb, pkey_rda_dual_mirror);
	}

	disp_apl_trace(LOG_ERR, 0x0f48, "defect content:\n");
	disp_apl_trace(LOG_INFO, 0xeb2f, "Scan _DFTB_ Row(0x%x)@CH/CE(%d/%d) PDA(0x%x)", dftb_rda_saved.row, dftb_rda_saved.ch, dftb_rda_saved.dev, nal_rda_to_pda(&dftb_rda_saved));
	srb_read_verify(dftb_rda_saved, NR_DUS_SLICE(defect_bitmap_size));
#if 0
	for (i = 0, k = 0; i < NR_PAGES_SLICE(defect_bitmap_size); i++) {
		rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

		disp_apl_trace(LOG_INFO, 0x790d, "Scan _DFTB_ Row(0x%x)@CH/CE(%d/%d) PDA(0x%x)", dftb_rda_saved.row, dftb_rda_saved.ch, dftb_rda_saved.dev, nal_rda_to_pda(&dftb_rda_saved));

		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = dftb_rda_saved;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(defect_bitmap_size) ? dftb_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}
		ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, 1);
		dftb_rda_saved.row += 1;
	}
#endif

	disp_apl_trace(LOG_ERR, 0x221f, "defect mirror content:\n");
	disp_apl_trace(LOG_INFO, 0x1459, "Scan _DFTB_(m) Row(0x%x)@CH/CE(%d/%d) PDA(0x%x)", dftb_rda_mirror_saved.row, dftb_rda_mirror_saved.ch, dftb_rda_mirror_saved.dev, nal_rda_to_pda(&dftb_rda_mirror_saved));
	srb_read_verify(dftb_rda_mirror_saved, NR_DUS_SLICE(defect_bitmap_size));
#if 0
	for (i = 0, k = 0; i < NR_PAGES_SLICE(defect_bitmap_size); i++) {
		rda_t tflush_rdas[SRB_MR_DU_CNT_PAGE];

		disp_apl_trace(LOG_INFO, 0xdc96, "Scan _DFTB_(m) Row(0x%x)@CH/CE(%d/%d) PDA(0x%x)", dftb_rda_mirror_saved.row, dftb_rda_mirror_saved.ch, dftb_rda_mirror_saved.dev, nal_rda_to_pda(&dftb_rda_mirror_saved));

		for (j = 0; j < SRB_MR_DU_CNT_PAGE; j++) {
			bm_pl_t *_pl = &pl[j];

			tflush_rdas[j] = dftb_rda_mirror_saved;
			tflush_rdas[j].du_off = j;

			_pl->pl.dtag = k < NR_DUS_SLICE(defect_bitmap_size) ? dftb_dtags[k].b.dtag : WVTAG_ID;
			_pl->pl.du_ofst = j;
			_pl->pl.btag = 0;
			_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_DROP;
			k++;
		}

		ncl_access_mr(tflush_rdas, NCL_CMD_OP_READ, pl, 1);
		dftb_rda_mirror_saved.row += 1;
	}
#endif
	dtag_put(DTAG_T_SRAM, dtag);
	dtag_put(DTAG_T_SRAM, dtag_mirror);
	disp_apl_trace(LOG_ALW, 0x0798, "MR Block Program Done");
}

static fast_code void
vdisk_com_free_updt(u32 param, u32 payload, u32 count)
{
	int i = 0;
	bm_pl_t *bm_tags = (bm_pl_t *) payload;

	for (i = 0; i < count; i++) {
		dtag_t dtag = {.b.dtag = bm_tags[i].pl.dtag,};

		if (((u32) (&__cmf_data_start) <= (u32) dtag2mem(dtag)) &&
				((u32) (&__cmf_data_end) > (u32) dtag2mem(dtag))
		   ) {
			/* XXX: change CMF on the fly, never recyle those memory */
			//continue;
			disp_apl_trace(LOG_ERR, 0x6a7d, "CMF dtag always fly\n");
		}
		disp_apl_trace(LOG_ERR, 0xacd5, "common free\n");
		dtag_put(get_dtag_type(dtag), dtag);
	}
}

static fast_code void
vdisk_fw_dwnld(u32 param, u32 _req, u32 not_used)
{
	req_t *req = (req_t *) _req;
	void *payload = req->req_prp.mem;
	u32 count = req->req_prp.mem_sz;

	sys_assert(sb_du_amt + count <= DOWNLOAD_MAX_DU);
	memcpy((void *)&sb_dus[sb_du_amt], (void *)payload, count * sizeof(dtag_t));
	if (sb_du_amt == 0) {
		image_t *image = (image_t *) dtag2mem(sb_dus[0]);

		disp_apl_trace(LOG_ALW, 0x8589, "Image SIG (0x%x) <- Exp. (0x%x) or (0x%x) or (0x%x)", image->signature, IMAGE_SIGNATURE, IMAGE_COMBO, IMAGE_CMFG);
		if (image->signature == 0xFFFFFFFF) {
			disp_apl_trace(LOG_ERR, 0x2cbc, "\r\033[91mChange another PCIe Slot ... 0x%p\x1b[0m\n", image);
			sys_assert(0);
		}
	}
	disp_apl_trace(LOG_ALW, 0x42df, "xfer (%d) -> cur(%d)", count, sb_du_amt);
	sb_du_amt += count;

	sys_free(SLOW_DATA, req->req_prp.mem);
	sys_free(SLOW_DATA, req->req_prp.prp);
	evt_set_imt(evt_cmd_done, _req, 0);
}

slow_code static void vdisk_dump_mem(const void *buf, int size)
{
	u32 *p = (u32 *)buf;
	u32 addr = 0;

	while (size > 0) {
		if((size & 0xF) == 0) {
			disp_apl_trace(LOG_ERR, 0x6bcc, "\n0x%x: ",addr);
		}
		disp_apl_trace(LOG_ERR, 0xbdb7, "  0x%x ",*p);
		p++;
		size -= sizeof(u32);
		addr += sizeof(u32);
	}
}

static fast_code void
vdisk_fw_commit(u32 param, u32 slot_ca, u32 _req)
{
	u32 slot = slot_ca >> 16;
	//u32 ca = slot_ca & 0xFFFF;

	if (_req != 0)
		evt_set_imt(evt_cmd_done, _req, 0);

	if (require_bbt_scan == true) {
		srb_bbt_scan();
		require_bbt_scan = false;
	}

	int i;
	u32 fw_crc;
	u32 *val = (u32 *) dtag2mem(sb_dus[sb_du_amt - 1]);
	bool fw_verified = false;
	i = (DTAG_SZE/sizeof(u32)) - 4;
	val += i;

	for (; i >= 0; i--, val--) {
		if ((*val == 0x1DBE5236) &&
			(*(val + 1) == 0x20161201) &&
			(*(val + 2) == 0)) {
			fw_crc = *(val + 3);
			int j;
			int crc = ~0U;
			for (j = 0; j < sb_du_amt - 1; j++) {
				crc = crc32_cont(dtag2mem(sb_dus[j]), DTAG_SZE, crc, false);
			}
			crc = crc32_cont(dtag2mem(sb_dus[sb_du_amt - 1]), i * sizeof(u32), crc, true);
			if (fw_crc != crc) {
				disp_apl_trace(LOG_ALW, 0x6c3e, "CRC(0x%x) vs. Real(0x%x), please retry!", fw_crc, crc);
				dtag_put_bulk(DTAG_T_SRAM, sb_du_amt, sb_dus);
				sb_du_amt = 0;
				return;
			}
			fw_verified = true;
			disp_apl_trace(LOG_ALW, 0x4ff7, "firmware CRC verified.");
			break;
		}
	}

	if (fw_verified == false) {
		disp_apl_trace(LOG_ALW, 0xbf52, "There is no CRC information in FW, please retry!");
		vdisk_dump_mem(dtag2mem(sb_dus[sb_du_amt - 1]), DTAG_SZE);
		dtag_put_bulk(DTAG_T_SRAM, sb_du_amt, sb_dus);
		sb_du_amt = 0;
		return;
	}

	ncl_enter_mr_mode();
	image_t *image = (image_t *) dtag2mem(sb_dus[0]);

	disp_apl_trace(LOG_ALW, 0xd531, "Image SIG (0x%x) -> Exp (0x%x) | AUs (%d)", image->signature, IMAGE_SIGNATURE, sb_du_amt);
	if ((image->signature == IMAGE_SIGNATURE) &&
			(crc32(image->sections, sizeof(section_t) * image->section_num) == image->section_csum)) {
		disp_apl_trace(LOG_ALW, 0x0b29, "true GRIT image.");
	} else if ((image->signature == IMAGE_COMBO) || (image->signature == IMAGE_CMFG)) {
		fw_config_set_t *fw_config;
		fw_config_dtag = _inv_dtag;

		/* This is a combo image contains loader & firmware */
		fw_slot = slot;
		fwb_mode = true;

		fw_start_du = image->fw_slice[1].slice_start;
		fw_end_du = image->fw_slice[1].slice_end;
		if ((image->signature == IMAGE_CMFG) && (image->fw_slice[2].slice_start != 0)) {
			fw_config = dtag2mem(sb_dus[image->fw_slice[2].slice_start]);
			if (fw_config->header.signature == IMAGE_CONFIG) {
#if defined(DDR)
				fw_config_dtag = sb_dus[image->fw_slice[2].slice_start];
				disp_apl_trace(LOG_ALW, 0x096e, "train DDR by type %d, size %x, speed %d",
						fw_config->board.ddr_board, fw_config->board.ddr_size, fw_config->board.ddr_clk);
				ddr_train(fw_config->board.ddr_board, fw_config->board.ddr_clk, fw_config->board.ddr_size);
				if (is_ddr_training_done()) {
					set_ddr_info_bkup_fwconfig_done();
					memcpy(fw_config->board.ddr_info, get_ddr_info_buf(), sizeof(ddr_info_t));
					disp_apl_trace(LOG_ALW, 0xca21, "copy ddr info to fwconfig dst/src: %x/%x", fw_config->board.ddr_info, get_ddr_info_buf());
				}
#endif
			} else {
				disp_apl_trace(LOG_ALW, 0xed87, "fw config (0x%x) -> Exp (0x%x) | AUs (%d)", fw_config->header.signature, IMAGE_CONFIG, image->fw_slice[2].slice_start);
			}
		}

		disp_apl_trace(LOG_ALW, 0x15d7, "Combo image, start du: 0x%x  end: 0x%x fw config: 0x%x",
			fw_start_du, fw_end_du, fw_config_dtag.dtag);
	}

	disp_apl_trace(LOG_ERR, 0xbd98, "------------------build SRB start------------------\n");
	srb_build();
	disp_apl_trace(LOG_ERR, 0x737b, "------------------build SRB end------------------\n");
	ncl_leave_mr_mode();
}

static fast_code void
vdisk_uart_commit(u32 param, u32 image_pos, u32 _image_size)
{
	u32 i;

	dtag_blackout(DTAG_T_SRAM, (void *) image_pos, _image_size);
	sb_du_amt = NR_DUS_SLICE(_image_size);
	for(i=0; i < sb_du_amt; i++) {
		sb_dus[i] = mem2dtag((void *)image_pos);
		image_pos += DTAG_SZE;
	}

	vdisk_fw_commit(param, 1 << 16, 0);
}

static init_code void vdisk_init(void)
{
	extern u8 evt_uart_commit;
	ncl_set_meta_base(dtag_meta, META_DTAG_SRAM_BASE);
	ncl_set_meta_base(dummy_meta, META_IDX_SRAM_BASE);

	width_nominal = nand_interleave_num();
	spb_total = nand_block_num();

	slc_pgs_per_blk = nand_page_num_slc();

	defect_bitmap_size = spb_total * sizeof(dft_btmp_t);
	defect_bitmap = sys_malloc(SLOW_DATA, defect_bitmap_size);
	sys_assert(defect_bitmap);
	memset((void *)defect_bitmap, 0xff, defect_bitmap_size);

	nal_get_first_dev(&ch, &dev);

	evt_register(vdisk_com_free_updt, 0, &evt_com_free_upt);
	evt_register(vdisk_fw_dwnld, 0, &evt_fw_dwnld);
	evt_register(vdisk_fw_commit, 0, &evt_fw_commit);
	evt_register(vdisk_uart_commit, 0, &evt_uart_commit);

	fw_max_slot = (u8)(slc_pgs_per_blk / ((IMAGE_MAX_SIZE + MR_PAGE_SZE - 1) / MR_PAGE_SZE) / 2);

	/* CMF saved before enable IRQ */
	u32 *cmf_hex;
	cmf_hex = eccu_get_binding_enc_cmf(&enc_cmf_sze);
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, NR_DUS_SLICE(enc_cmf_sze), enc_cmf_dtags) == NR_DUS_SLICE(enc_cmf_sze));
	bf_to_dtags((void *)cmf_hex, enc_cmf_dtags, NR_DUS_SLICE(enc_cmf_sze));
	disp_apl_trace(LOG_ALW, 0xf5fb, "Enc size(%d) -> #Dus(%d) #PGs(%d)", enc_cmf_sze, NR_DUS_SLICE(enc_cmf_sze), NR_PAGES_SLICE(enc_cmf_sze));

	cmf_hex = eccu_get_binding_dec_cmf(&dec_cmf_sze);
	sys_assert(dtag_get_bulk(DTAG_T_SRAM, NR_DUS_SLICE(dec_cmf_sze), dec_cmf_dtags) == NR_DUS_SLICE(dec_cmf_sze));
	bf_to_dtags((void *)cmf_hex, dec_cmf_dtags, NR_DUS_SLICE(dec_cmf_sze));
	disp_apl_trace(LOG_ALW, 0x07d0, "Dec size(%d) -> #Dus(%d) #PGs(%d)", dec_cmf_sze, NR_DUS_SLICE(dec_cmf_sze), NR_PAGES_SLICE(dec_cmf_sze));

	/* WA: in case of PERST, it will reset PCIe, Not good! */
	writel(0xF, (void *)0xC0040000);

	//irq_enable();   ///< this will cause undefined instruction issue when load FW via GDB

	// TODO: disable FICU ISR, removed spin lock in the ficu_cmpl_fcmd_handling
	// poll_unmask(VID_FICU_FCMD_DONE);

	ncl_enter_mr_mode();
	if (srb_scan(true) == false)
		require_bbt_scan = true;
	else
		load_old_mr = true;
	ncl_leave_mr_mode();
#if 0
	require_bbt_scan = true;
	memset((void *)defect_bitmap, 0xFF, defect_bitmap_size);
	vdisk_fw_commit(0, 0, 0);
#endif

	//For programmer, we only report a virtual disk and just complete all IO commands directly
	nvme_ns_attr_t attr;
	memset((void*)&attr, 0, sizeof(attr));
	nvme_ns_attr_t *p_ns = &attr;
	p_ns->hw_attr.nsid = 1;
	p_ns->hw_attr.pad_pat_sel = 1;
	p_ns->fw_attr.support_pit_cnt = 0;
	p_ns->fw_attr.support_lbaf_cnt = 1;
	p_ns->fw_attr.type = NSID_TYPE_ACTIVE;
	p_ns->fw_attr.ncap = p_ns->fw_attr.nsz = p_ns->hw_attr.lb_cnt = 0x100000;

	nvmet_set_ns_attrs(p_ns, true);
	disp_apl_trace(LOG_ALW, 0x231b, "Vdisk init done!");
#if 0
	erase_spb(0);
	erase_spb(1);
#endif

}

module_init(vdisk_init, VDISK);

static ps_code int forcescan_main(int argc, char *argv[])
{
	//Force defect scan
	memset((void *)defect_bitmap, 0xFF, defect_bitmap_size);
	srb_bbt_scan();
	require_bbt_scan = false;

	ncl_enter_mr_mode();
	disp_apl_trace(LOG_ERR, 0x1b7c, "------------------build SRB start------------------\n");
	srb_build();
	disp_apl_trace(LOG_ERR, 0xf3ea, "------------------build SRB end------------------\n");
	ncl_leave_mr_mode();
	return 0;
}

static ps_code int vdisk_erase_st_switch(int argc, char *argv[])
{
	disp_apl_trace(LOG_ERR, 0x7912, "\n");
	if (strstr(argv[1], "on") && (vdisk_erase_enable == false)) {
		vdisk_erase_enable = true;
		disp_apl_trace(LOG_ERR, 0x3fc1, "Enable vdisk erase!\n ");
	} else if (strstr(argv[1], "off") && (vdisk_erase_enable == true)) {
		vdisk_erase_enable = false;
		disp_apl_trace(LOG_ERR, 0x12b4, "Disable vdisk erase!\n ");
	} else {
		disp_apl_trace(LOG_ERR, 0x166f, "Current status has already been: %s\n", argv[1]);
	}
	return 0;
}

static ps_code int erase_spb(u32 spb_id)
{
	u8 ch, ce, lun, pln;

	rda_t rda = {
		.pb_type = NAL_PB_TYPE_SLC,
		.du_off = 0,
	};

	disp_apl_trace(LOG_ERR, 0x88b7, "\n");
	if (spb_id >= spb_total) {
		disp_apl_trace(LOG_ERR, 0xb3ca, "PB(%d) is out of (%d)\n", spb_id, spb_total);
		return 0;
	}
	ncl_enter_mr_mode();

	for (ch = 0; ch < nal_get_channels(); ch++) {
		for (ce = 0; ce < nal_get_targets(); ce++) {
			rda.ch = ch;
			rda.dev = ce;
			for (lun = 0; lun < nand_lun_num(); lun++) {
				for (pln = 0; pln < nand_plane_num(); pln++) {
					rda.row = nda2row(0, pln, spb_id, lun);
					int retval = rda_erase(rda);
					u8 dpos = pln + (lun << LUN_SHF) + (ce << CE_SHF) + (ch << CH_SHF);

					if (retval == 0) {
						defect_bitmap[spb_id].dft_bitmap[dpos >> 5] &= ~BIT(dpos & ((1 << 5) - 1));
					}
				}
			}
		}
	}
	ncl_leave_mr_mode();

	disp_apl_trace(LOG_ERR, 0x07c5, "PB (%d) Erased\n", spb_id);
	return 0;
}


static ps_code int pberase_main(int argc, char *argv[])
{
	char *endp;
	u32 spb_id = strtoull(argv[1], &endp, 0);
	erase_spb(spb_id);

	return 0;
}

static fast_code int frberase_main(int argc, char *argv[])
{
	u32 t;

	disp_apl_trace(LOG_ERR, 0x6e78, "frbP : %d,%d,%x st", ftlb_pri_pos.ch, ftlb_pri_pos.dev, ftlb_pri_pos.row);
	t = ftlb_pri_pos.dev + ftlb_pri_pos.ch + ftlb_pri_pos.row;
	ncl_enter_mr_mode();
	if (t != 0) {
		int retval = rda_erase(ftlb_pri_pos);
		disp_apl_trace(LOG_ERR, 0xad5c, " ret %d\n", retval);
	}

	disp_apl_trace(LOG_ERR, 0x54c5, "frbS : %d,%d,%x st", ftlb_sec_pos.ch, ftlb_sec_pos.dev, ftlb_sec_pos.row);
	t = ftlb_sec_pos.dev + ftlb_sec_pos.ch + ftlb_sec_pos.row;
	if (t != 0) {
		int retval = rda_erase(ftlb_sec_pos);
		disp_apl_trace(LOG_ERR, 0x1276, " ret %d\n", retval);
	}
	ncl_leave_mr_mode();

	return 0;
}

static DEFINE_UART_CMD(frberase, "frberase",
		"erase frb",
		"erase frb, let ftl start from virgin device",
		0, 0, frberase_main);

static DEFINE_UART_CMD(forcescan, "forcescan",
		"forcescan",
		"scan all blocks and update defect table to MR",
		0, 0, forcescan_main);

static DEFINE_UART_CMD(pberase, "pberase",
		"pberase id",
		"erase parallel blocks",
		1, 1, pberase_main);

static DEFINE_UART_CMD(vdisk_erase_switch, "vdisk_erase_switch",
		"vdisk_erase_switch [on|off]",
		"Enable/disable vdisk erase when srb_bbt_scan",
		1, 1, vdisk_erase_st_switch);
