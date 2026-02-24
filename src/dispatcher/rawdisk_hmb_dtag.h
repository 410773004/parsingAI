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
 * @brief Rawdisk support HMB DTAG
 *
 * \addtogroup rawdisk
 *
 * \defgroup hmb_dtag
 * \ingroup rawdisk
 * @{
 * host write data will be transferred to HMB from SRAM, then program to NAND from HMB
 * host read data will be transferred to HMB from NAND, then fw transferred it to SRAM then return to HOST
 */

#ifdef HMB_DTAG

#undef __FILEID__
#define __FILEID__ rawhdtag

/*!
 * @brief write queue entry data format, it should be transferring to HMB
 */
typedef struct _to_hmb_pl_t {
	lda_t lda;		///< lda for sram cache hit
	bm_pl_t bm_pl;		///< origin bm payload
	dtag_t hdtag;		///< HMB dtag
} to_hmb_pl_t;

/*!
 * @brief toHMB transfer queue
 */
typedef struct _hmb_dtag_que_t {
	to_hmb_pl_t que[SRAM_IN_DTAG_CNT + 1];	///< queue buffer
	u16 wptr;				///< write pointer, request submit
	u16 rptr;				///< read pointer, request done
} hmb_dtag_que_t;

static fast_data hmb_dtag_que_t to_hmb_otf_que;		///< HMB push requests queue, used for sram hit
static fast_data bm_pl_que_t to_hmb_wait_que;		///< insert toHMB request in this queue when HMB dtag is not enough
static fast_data bool wait_hmb_dtag = false;		///< if HMB dtag event was registered
static fast_data bm_pl_que_t from_hmb_wait_que;		///< fromHMB pending queue, insert when SRAM dtag resource is not enough
static fast_data u16 hmb_du_ofst[SRAM_IN_DTAG_CNT];	///< record du_off for hmb xfring sdtag
static fast_data dtag_t hmb_dtag_fw_stream[RAWDISK_STREAMING_MODE_DTAG_CNT];	///< fw streaming sram dtag buffer, we use this buffer to pull data from HMB to SRAM then return data to host
static fast_data u32 fw_stream_bmp[(SRAM_IN_DTAG_CNT + 31) / 32];	///< indicate which SRAM dtag was used in fw streaming
static fast_data int hmb_dtag_fws_cnt;	///< fw stream dtag current count
static fast_data dtag_t par_hmb_dtag;

static void rawdisk_unmap_pda(lda_t slda, u32 ndu, u32 btag, u32 ofst);
static void rawdisk_collision(lda_t slda, u32 ndu, u32 btag, u32 ofst);
static void _rawdisk_nrm_wd_updt(bm_pl_t *bm_pl);
void rawdisk_hmb_dtag_pull(dtag_t hdtag, bm_pl_t *pl);
void fw_stream_push(dtag_t dtag);
static int rawdisk_read(ncl_dat_t *dat, bool sync, enum ncl_op_type_t op_type);
static void bm_pl_que_ins(bm_pl_que_t *que, bm_pl_t *bm_pl);
static void rawdisk_merge(bm_pl_t *bm_pl, dtag_t dst);
static void rawdisk_read_prep(int btag, ncl_dat_t *dat);

/*!
 * @brief API to insert a on-the-fly HMB write request, this queue was used to SRAM hit
 *
 * @param que		queue
 * @param bm_pl		SRAM data payload
 * @param hdtag		HMB dtag
 *
 * @return		not used
 */
fast_code static void hmb_dtag_que_ins(hmb_dtag_que_t *que, bm_pl_t *bm_pl, dtag_t hdtag)
{
	int btag = bm_pl->pl.btag;
	lda_t lda;

	lda = LBA_TO_LDA(bcmd_get_slba(btag2bcmd(btag))) + bm_pl->pl.du_ofst;
	que->que[que->wptr].lda = lda;
	que->que[que->wptr].bm_pl = *bm_pl;
	que->que[que->wptr].hdtag = hdtag;
	que->wptr++;
	if (que->wptr >= (SRAM_IN_DTAG_CNT + 1)) {
		que->wptr = 0;
	}
	sys_assert(que->wptr != que->rptr);
}

/*!
 * @brief handler when a HMB write data entry was done
 *
 * @param que		write request queue
 * @param sdtag		sram dtag which data was transferred to HMB
 *
 * @return		not used
 */
fast_code static void hmb_dtag_que_done(hmb_dtag_que_t *que, dtag_t sdtag)
{
	bm_pl_t *bm_pl;

	// completed from "HMB push"
	sdtag.b.type = 0; // here, asic return sdtag but with type = 1
	sys_assert(que->que[que->rptr].bm_pl.pl.dtag == sdtag.dtag);

	disp_apl_trace(LOG_DEBUG, 0x27de, "push sram %x to hmb %x done",
			que->que[que->rptr].bm_pl.pl.dtag,
			que->que[que->rptr].hdtag.dtag);

	/* replace with hdtag */
	bm_pl = &que->que[que->rptr].bm_pl;
	bm_pl->pl.dtag = que->que[que->rptr].hdtag.dtag;

	/* get lda by ctag, pl's ctag/du_off keep the former value */
	_rawdisk_nrm_wd_updt(bm_pl);
	dtag_put(RAWDISK_DTAG_TYPE, sdtag);

	que->rptr++;
	if (que->rptr >= (SRAM_IN_DTAG_CNT + 1)) {
		que->rptr = 0;
	}
}

/*!
 * @brief handle read pend queue due to sram dtag resource
 *
 * @param que		queue
 *
 * @return		not used
 */
fast_code static void read_pl_que_handle(bm_pl_que_t *que)
{
	dtag_t hdtag;

	if (que->wptr == que->rptr)
		return;

	disp_apl_trace(LOG_DEBUG, 0x3486, "fw stream resume");
	hdtag.dtag = que->que[que->rptr].pl.dtag;
	rawdisk_hmb_dtag_pull(hdtag, &que->que[que->rptr]);
	que->rptr++;
	if (que->rptr >= SRAM_IN_DTAG_CNT + 1) {
		que->rptr = 0;
	}
}

/*!
 * @brief HMB dtag event callback to handle SRAM to HMB transfer
 *
 * @param ctx	should be bm pl wait queue
 *
 * @return	not used
 */
fast_code static void rawdisk_write_hmb_resume(void *ctx)
{
	bm_pl_que_t *que = (bm_pl_que_t *)ctx;

	wait_hmb_dtag = false;

	if (que->rptr == que->wptr)
		return;

	do {
		bm_pl_t *bm_pl;
		u16 hmb_off;
		dtag_t hdtag = dtag_get(DTAG_T_HMB, NULL);

		if (hdtag.dtag == _inv_dtag.dtag)
			break;

		bm_pl = &que->que[que->rptr];
		bm_pl->pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_HMB;
		hmb_off = hdtag.b.dtag;

		hmb_dtag_que_ins(&to_hmb_otf_que, bm_pl, hdtag);
		disp_apl_trace(LOG_DEBUG, 0x57e6, "write sdtag %x to hmb %x",
				bm_pl->pl.dtag, hdtag.dtag);

		bm_hmb_req(bm_pl, hmb_off, false);
		que->rptr++;
		if (que->rptr >= SRAM_IN_DTAG_CNT + 1) {
			que->rptr = 0;
		}
	} while (que->rptr != que->wptr);

	if (que->rptr != que->wptr) {
		wait_hmb_dtag = true;
		dtag_register_evt(DTAG_T_HMB, rawdisk_write_hmb_resume, &to_hmb_wait_que, false);
	}
}

/*!
 * @brief write SRAM dtag to HMB
 *
 * @param pl	SRAM data payload list
 * @param count	list length
 *
 * @return	not used
 */
fast_code static void rawdisk_write_hmb(bm_pl_t *pl, u32 count)
{
	u32 i = 0;
	bool pend = false;

	for (i = 0; i < count; i++) {
		dtag_t hdtag = dtag_get(DTAG_T_HMB, NULL);
		u16 hmb_off;

		if (hdtag.dtag == _inv_dtag.dtag) {
			pend = true;
			bm_pl_que_ins(&to_hmb_wait_que, pl + i);
			continue;
		}

		pl[i].pl.type_ctrl = BTN_NCB_QID_TYPE_CTRL_HMB; // DATA_ENTRY_QID[61:60] = 0x1; HMB mode
		hmb_off = hdtag.b.dtag;	// truncate 16bit, remove type bit

		hmb_dtag_que_ins(&to_hmb_otf_que, pl + i, hdtag); // for sram read hit
		disp_apl_trace(LOG_DEBUG, 0x41ae, "write sdtag %x to hmb %x",
				pl[i].pl.dtag, hdtag.dtag);
		bm_hmb_req(pl + i, hmb_off, false);
	}

	if (pend && wait_hmb_dtag == false) {
		wait_hmb_dtag = true;
		disp_apl_trace(LOG_INFO, 0xa3b7, "write pend by HMB");
		dtag_register_evt(DTAG_T_HMB, rawdisk_write_hmb_resume, &to_hmb_wait_que, false);
	}
}

/*!
 * @brief event handler when data was pull from HMB
 *
 * @param param		not used
 * @param payload	bm payload list
 * @param count		list length
 *
 * @return		not used
 */
fast_code static void rawdisk_hmb_rd_updt(u32 param, u32 payload, u32 count)
{
	u32 i;
	dtag_t sdtag;
	bm_pl_t *pl = ((bm_pl_t *) payload);

	/* free hdtag */
	for (i = 0; i < count; i++) {
		dtag_t hdtag;

		sys_assert(pl[i].hpl.type_ctrl == 5); // write-data-entry read back
		/* put hdtag */
		hdtag.dtag = pl[i].hpl.hmb_ofst_p1 | (pl[i].hpl.hmb_ofst_p2 << 12);
		hdtag.b.type = 1;
		dtag_put(DTAG_T_HMB, hdtag);

		sdtag.dtag = pl[i].hpl.dtag;
		if (sdtag.dtag == par_hmb_dtag.dtag) {
			bm_pl_t *bm_pl = &par_pl_q.que[par_pl_q.rptr];

			par_handling = 6;
			sys_assert(par_hmb_dtag.dtag == dst.dtag);
			rawdisk_merge(bm_pl, par_hmb_dtag);
			par_hmb_dtag = _inv_dtag;
			continue;
		}
		pl[i].pl.du_ofst = hmb_du_ofst[sdtag.dtag];
		disp_apl_trace(LOG_DEBUG, 0x5695, "return %x to host", sdtag.dtag);
		bm_radj_push_rel(&pl[i], 1);
	}
}

/*!
 * @brief event handler for common free
 *
 * @param param		not used
 * @param payload	dtag list
 * @param count 	list length
 *
 * @return		not used
 */
fast_code static void rawdisk_hmb_dtag_com_free(u32 param, u32 payload, u32 count)
{
	dtag_t *dtag = (dtag_t *) payload;
	u32 i;

	for (i = 0; i < count; i++) {
		if (dtag[i].b.type == 0) {
			// SRAM, it's read dtag
			if (test_bit(dtag[i].dtag, &fw_stream_bmp[0])) {
				fw_stream_push(dtag[i]);
			} else {
				dtag_put(RAWDISK_DTAG_TYPE, dtag[i]);
			}
			disp_apl_trace(LOG_DEBUG, 0xe647, "com free dtag %x", dtag[i].dtag);
			continue;
		}

		// dtag should be hdtag
		hmb_dtag_que_done(&to_hmb_otf_que, dtag[i]);
	}
}

/*!
 * @brief fw stream initialization
 *
 * @param dtags		dtag list
 * @param cnt		list length
 *
 * @return		not used
 */
init_code void fw_stream_init(dtag_t *dtags, u32 cnt)
{
	u32 i;

	memset(fw_stream_bmp, 0, sizeof(fw_stream_bmp));
	memcpy(hmb_dtag_fw_stream, dtags, sizeof(dtag_t) * cnt);
	for (i = 0; i < cnt; i++) {
		set_bit(dtags[i].dtag, fw_stream_bmp);
	}
	hmb_dtag_fws_cnt = (int) (cnt - 1);
}

/*!
 * @brief fw stream resource pop
 *
 * @return		free dtag or inv_dtag
 */
fast_code dtag_t fw_stream_pop(void)
{
	int t;

	if (hmb_dtag_fws_cnt == -1) {
		return _inv_dtag;
	}

	t = hmb_dtag_fws_cnt;
	hmb_dtag_fws_cnt--;
	return hmb_dtag_fw_stream[t];
}

/*!
 * @brief fw stream push, return
 *
 * @param dtag		dtag to be free
 *
 * @return		not used
 */
fast_code void fw_stream_push(dtag_t dtag)
{
	hmb_dtag_fws_cnt++;
	sys_assert(hmb_dtag_fws_cnt >= 0 && hmb_dtag_fws_cnt <= RAWDISK_STREAMING_MODE_DTAG_CNT - 1);
	sys_assert(dtag.b.type == 0);
	hmb_dtag_fw_stream[hmb_dtag_fws_cnt] = dtag;

	read_pl_que_handle(&from_hmb_wait_que);
}

/*!
 * @brief dtag event callback to re-get HMB dtag for read command
 *
 * @param dtag		should be reget_dtag context
 *
 * @return		not used
 */
fast_code static void rawdisk_reget_rd_dtags(void *ctx)
{
	reget_dtag_t *reget_rd_dtag = ctx;
	int btag = reget_rd_dtag->btag;
	ncl_dat_t *dat = reget_rd_dtag->dat;

	while (reget_rd_dtag->required) {
		dtag_t dtag;

		dtag = dtag_get(DTAG_T_HMB, NULL);
		if (dtag.dtag == _inv_dtag.dtag)
			break;

		dat->bm_pl[reget_rd_dtag->got].pl.dtag = dtag.dtag;
		reget_rd_dtag->got++;
		reget_rd_dtag->required--;
	}

	if (reget_rd_dtag->required) {
		dtag_register_evt(DTAG_T_HMB, rawdisk_reget_rd_dtags, (void *)reget_rd_dtag, true);
	} else {
		sys_free(FAST_DATA, reget_rd_dtag);
		disp_apl_trace(LOG_INFO, 0x2a48, "read req resume %d", btag);
		rawdisk_read_prep(btag, dat);
		rawdisk_read(dat, false, NCL_CMD_HOST_READ_HMB_DTAG);
	}
}

/*!
 * @brief copy HMB DTAG to SRAM DTAG
 *
 * @param ncl_cmd	NCL read command
 *
 * @return		None
 */
fast_code static void rawdisk_copy_rd_dtags(struct ncl_cmd_t *ncl_cmd)
{
	ncl_dat_t *dat = ncl_cmd->caller_priv;
	u32 i;

	for (i = 0; i < dat->count; i++) {
		dtag_t hdtag;

		disp_apl_trace(LOG_DEBUG, 0xb16b, "read to HMB %x",
				ncl_cmd->user_tag_list[i].pl.dtag);
		hdtag.dtag = ncl_cmd->user_tag_list[i].pl.dtag;
		rawdisk_hmb_dtag_pull(hdtag, ncl_cmd->user_tag_list + i);
	}
}


/*!
 * @brief check toHMB reqeust queue for SRAM dtag hit
 *
 * @param slda		start lda
 * @param ndu		number of du
 * @param btag		command tag
 * @param ofst		offset in origin command
 *
 * @return		None
 */
fast_code void rawdisk_hmb_otf_collision(lda_t slda, u32 ndu, u32 btag, u32 ofst)
{
	lda_t elda = slda + ndu - 1;
	u16 i = to_hmb_otf_que.wptr;

	if (to_hmb_otf_que.wptr == to_hmb_otf_que.rptr)
		return;

	i = (i == 0) ? SRAM_IN_DTAG_CNT : (i - 1);

	while (1) {
		lda_t lda = to_hmb_otf_que.que[i].lda;
		dtag_t sdtag;

		if (lda < slda || lda > elda) {
			goto end;
		}

		if (test_bit((lda - slda), &lda_in_wb_unmap_bitmap)) {
			disp_apl_trace(LOG_DEBUG, 0x1462, "lda(0x%x) is re-written, discard it", lda);
			goto end;
		}

		sdtag.dtag = to_hmb_otf_que.que[i].bm_pl.pl.dtag;
		disp_apl_trace(LOG_DEBUG, 0x887b, "%x return from SDTAG %d", lda, sdtag.dtag);
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.btag = btag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.dtag = sdtag.dtag;
		wb_unmap_pl[lda_in_wb_unmap_cnt].pl.du_ofst = lda - slda + ofst;

		dtag_ref_inc(RAWDISK_DTAG_TYPE, sdtag);
		set_bit(lda - slda, (void *) &lda_in_wb_unmap_bitmap);
		lda_in_wb_unmap_cnt++;
end:
		if (i == to_hmb_otf_que.rptr)
			break;

		i = (i == 0) ? SRAM_IN_DTAG_CNT : (i - 1);
	}
}

/*!
 * @brief pull data from HMB, hdtag -> pl
 *
 * to allocate a sram dtag and pull hdtag to sdtag
 *
 * @param hdtag		HMB dtag
 * @param pl		data payload
 *
 * @return		not used
 */
fast_code void rawdisk_hmb_dtag_pull(dtag_t hdtag, bm_pl_t *pl)
{
	dtag_t sdtag = fw_stream_pop();
	u16 hmb_off;

	if (sdtag.dtag == _inv_dtag.dtag) {
		disp_apl_trace(LOG_DEBUG, 0x9f2a, "read que pend");
		bm_pl_que_ins(&from_hmb_wait_que, pl);
		return;
	}

	hmb_off = hdtag.dtag;

	disp_apl_trace(LOG_DEBUG, 0xed40, "read hmb %x to sram %x", hmb_off, sdtag.dtag);
	pl->pl.dtag = sdtag.dtag;
	hmb_du_ofst[sdtag.b.dtag] = pl->pl.du_ofst;
	bm_hmb_req(pl, hmb_off, true);
	//return from hmb rd done
}

/*!
 * @brief handle read adjust queue for read command, if dtag was in HMB, pull it before push
 *
 * @param pl	bm payload list, dtag should be in sram or HMB
 * @param count	list length
 *
 * @return	not used
 */
fast_code void rawdisk_read_adj_push(bm_pl_t *pl, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++) {
		dtag_t pl_dtag = {.dtag = pl[i].pl.dtag,};

		if (pl_dtag.b.type == 1) {
			rawdisk_hmb_dtag_pull(pl_dtag, pl + i);
			continue;
		}

		bm_rd_dtag_commit(pl[i].pl.du_ofst, pl[i].pl.btag, pl_dtag);
	}
}

fast_code void rawdisk_hmb_read(int btag, ncl_dat_t *dat)
{
	dtag_t dtags[MAX_NCL_DATA_LEN];
	u32 required;
	bm_pl_t *pl;
	u32 allocated;

	required = dat->ndu - dat->hit_cnt;
	pl = dat->bm_pl;

	allocated = dtag_get_bulk(DTAG_T_HMB, required, dtags);

	if (allocated > 0) {
		u32 i;

		for (i = 0; i < allocated; i++)
			pl[i].pl.dtag = dtags[i].dtag;
	}

	if (allocated < required) {
		reget_dtag_t *reget_rd_dtag;

		reget_rd_dtag = sys_malloc(FAST_DATA, sizeof(*reget_rd_dtag));
		sys_assert(reget_rd_dtag);
		reget_rd_dtag->btag = btag;
		reget_rd_dtag->dat = dat;
		reget_rd_dtag->got = allocated;
		reget_rd_dtag->required = required - allocated;
		dtag_register_evt(DTAG_T_HMB, rawdisk_reget_rd_dtags,
				(void *)reget_rd_dtag, false);
		disp_apl_trace(LOG_INFO, 0x167d, "read req pend %d %d", btag, reget_rd_dtag->required);
	} else {
		rawdisk_read_prep(btag, dat);
		rawdisk_read(dat, false, NCL_CMD_HOST_READ_HMB_DTAG);
	}
}

/*!
 * @brief read request handler
 *
 * @param req	read request
 * @param dtags	resource to allocate hdtag
 *
 * @return	not used
 */
fast_code void hmb_dtag_sub_rd_req_exec(int btag, ncl_dat_t *dat)
{
	lda_in_wb_unmap_cnt = 0;
	lda_in_wb_unmap_bitmap = 0;

	// search data in sram
	rawdisk_hmb_otf_collision(dat->slda, dat->ndu, btag, dat->ofst);

	// search data in hmb
	if (wr_pl_cur)
		rawdisk_collision(dat->slda, dat->ndu, btag, dat->ofst);

	// search data is unmap
	rawdisk_unmap_pda(dat->slda, dat->ndu, btag, dat->ofst);

	if (lda_in_wb_unmap_cnt)
		rawdisk_read_adj_push(wb_unmap_pl, lda_in_wb_unmap_cnt);

	sys_assert(lda_in_wb_unmap_cnt <= btag2bcmd_ex(btag)->ndu);

	/* all data in write buffer or unmap area */
	if (lda_in_wb_unmap_cnt == dat->ndu) {
		pool_put_ex(&ncl_dat_pool, dat);
		return;
	}

	dat->hit_bmp = lda_in_wb_unmap_bitmap;
	dat->hit_cnt = lda_in_wb_unmap_cnt;
	rawdisk_hmb_read(btag, dat);
}

#undef __FILEID__
// #define __FILEID__ rawdisk

#endif
