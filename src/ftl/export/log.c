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
/*! \file log.c
 * @brief define log data structure for system log and spb log
 *
 * \addtogroup log
 * @{
 *
 * Log is a special area to use physical block to save critical data, it support
 * dual copy or not
 */
#include "types.h"
#include "sect.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ftltype.h"
#include "stdlib.h"
#include "stdio.h"
#include "ncl_exports.h"
#include "log.h"
#include "sync_ncl_helper.h"
#include "dtag.h"

/*! \cond PRIVATE */
#define __FILEID__ log
#include "trace.h"
/*! \endcond */

/*!
 * @brief Determine the latest page in a log block
 *
 * @param s		start index
 * @param cnt		how many page in a log block
 * @param spb_id	log SPB ID
 * @param blk		log block ID
 * @param log_recon	log reconstruction parameter
 *
 * @return		Return latest page number
 */
static u32 blk_decide_latest_page(u32 s, u32 cnt, spb_id_t spb_id, u32 blk,
	log_recon_t *log_recon);

/*!
 * @brief Reconstruct a log block
 *
 * @param log		log context
 * @param log_recon	log reconstruction parameter
 *
 * @return		Return false if LUT was not reconstructed fully
 */
static bool log_blk_reconstruction(log_t *log, log_recon_t *log_recon);

/*!
 * @brief Check if atomic flush was broken
 *
 * @param log		log context
 * @param log_meta	log meta of latest page
 * @param page		latest page in log block
 *
 * @return		Return valid flush end
 */
static u32 log_atomic_check(log_t *log, log_meta_t *log_meta, u32 page);

/*!
 * @brief check if all pages in a fully atomic flush were readable
 *
 * @param log		log context
 * @param log_meta	log meta of latest page
 * @param page		latest page in log block
 * @param log_recon	log reconstruction resource
 * @param blk		current log block
 * @param spb_id	current log spb id
 *
 * @return		return valid flush end
 */
static u32 log_atomic_check_ex(log_t *log, log_meta_t *log_meta,
		u32 page, log_recon_t *log_recon, u32 blk,
		spb_id_t spb_id);

/*!
 * @brief Scan log block to reconstruct L2P table
 *
 * @param log		log context
 * @param log_recon	log reconstruction parameter
 * @param start		scan from this PDA
 * @param latest_blk	latest block
 * @param latest_page	latest page
 *
 * @return		none
 */
static void log_scan_blk_page(log_t *log, log_recon_t *log_recon,
		pda_t start, u32 latest_blk, u32 latest_page);

/*!
 * @brief Check if block page A was less than or equal to block page B
 *
 * @param blk_a		block id of block page A
 * @param page_a	page id of block page A
 * @param blk_b		block id of block page B
 * @param page_b	page id of block page B
 *
 * @return		Return true if block page A less than or equal to block page B
 */
static bool blk_page_lte_blk_page(u32 blk_a, u32 page_a, u32 blk_b,
		u32 page_b);

init_code void log_init(log_t *log2, u32 log_page_cnt, u32 nr_page_per_blk, u32 flags)
{
	log2->nr_page_per_blk = nr_page_per_blk;
	log2->log_page_cnt = log_page_cnt;

	log2->cur[0].spb_id = log2->cur[1].spb_id = INV_SPB_ID;
	log2->next[0].spb_id = log2->next[1].spb_id = INV_SPB_ID;
	log2->flags = flags;
}

fast_code void log_recon_init(log_recon_t *log_recon, pda_t *l2pt,
		log_l2pt_read_t log_l2pt_read, u32 sig)
{
	dtag_t dtag;
	// refine
	extern struct du_meta_fmt *sram_dummy_meta;
	extern u32 sram_dummy_meta_idx;

	log_recon->sig = sig;
	log_recon->l2pt = l2pt;
	log_recon->log_l2pt_read = log_l2pt_read;
	dtag = dtag_cont_get(DTAG_T_SRAM, DU_CNT_PER_PAGE);
	log_recon->data = dtag2mem(dtag);
	log_recon->meta = sram_dummy_meta;
	log_recon->meta_idx = sram_dummy_meta_idx;
	log_recon->read_error = false;
	log_recon->pblk_cnt = 0;
}

fast_code void log_recon_rel(log_recon_t *log_recon)
{
	dtag_t dtag;

	dtag = mem2dtag(log_recon->data);
	dtag_cont_put(DTAG_T_SRAM, dtag, DU_CNT_PER_PAGE);
}

init_code bool log_reconstruction(log_t *log, log_recon_t *log_recon)
{
	if (log->flags & LOG_F_SINGLE) {
		return log_blk_reconstruction(log, log_recon);
	} else {
		u32 ret = log_blk_reconstruction(log, log_recon);

		if (ret == false) {
			blk_pool_put_pblk(log->cur[0]);
			if (log->flags & LOG_F_BACKUP)
				blk_pool_put_pblk(log->cur[1]);

			log->cur[0] = log->next[0];
			log->cur[1] = log->next[1];
			ret = log_blk_reconstruction(log, log_recon);
			// after reconstruction only cur will be kept
		}
		return ret;
	}
	return true;
}

bool log_blk_reconstruction(log_t *log, log_recon_t *log_recon)
{
	u32 page;
	pda_t pda;
	nal_status_t ret;
	log_meta_t *log_meta;
	pda_t scan_start;
	bool last = true;
	spb_id_t spb_id;
	u32 blk;

	log_meta = (log_meta_t*) log_recon->meta;

	/* binary search the latest page */
	blk = log->cur[0].iid;
	spb_id = log->cur[0].spb_id;
	page = blk_decide_latest_page(0, log->nr_page_per_blk, spb_id, blk, log_recon);

	ftl_log_trace(LOG_INFO, 0x41df, "latest page %d, blk %d spb %d, error %d", page,
			blk, spb_id, log_recon->read_error);
	sys_assert(page != ~0);

	/* setup next page, block */
	log->next_page = (u16) (page + 1);

	if (log_recon->read_error)
		log->next_page = log->nr_page_per_blk;

again:
	pda = blk_page_make_pda(spb_id, blk, page);

	ret = ncl_read_one_page(pda, log_recon->data, log_recon->meta_idx);

	if (!ficu_du_data_good(ret)) {
		page--;
		goto again;
	}

	if (log_meta->meta0.signature != log_recon->sig) {
		ftl_log_trace(LOG_ERR, 0xf7ee, "sig err, %x %x %x %x",
				log_meta->meta0.signature,
				log_meta->meta0.backup.pblk,
				log_meta->meta0.seed,
				log_meta->meta0.flush_id);
		panic("stop");
	}

	if (log->flush_id <= log_meta->meta0.flush_id)
		log->flush_id = log_meta->meta0.flush_id + 1;

	if (log->flags & LOG_F_ATOMIC) {
		u32 t = log_atomic_check(log, log_meta, page);

		if (t == page && last) {
			t = log_atomic_check_ex(log, log_meta, page, log_recon, blk, spb_id);
			last = false;
		}

		if (t != page) {
			log->next_page = log->nr_page_per_blk;
			/* discarded */
			if (t == log->nr_page_per_blk - 1) {
				if (blk == 0) {
					ftl_log_trace(LOG_ALW, 0x7410, "spb %d recon fail %d", spb_id, log->flush_id);

					//change flush id to 1 from 0 because spb log recon will regard device as
					//virgin when flush id is 0 and return false
					if (log->flush_id == 0)
						log->flush_id = 1;
					return false;
				}

				log->flags |= LOG_F_FLUSH_ALL;
				blk--;
			}

			page = t;
			goto again;
		}
	}

	/* restore L2P table from latest page */
	if (log_recon->log_l2pt_read)
		scan_start = log_recon->log_l2pt_read(log_recon, log_meta, pda);
	else
		scan_start = pda; // root block case

	ftl_log_trace(LOG_INFO, 0xea80, "log scan start %x", scan_start);
	if (scan_start == INV_PDA)
		scan_start = blk_page_make_pda(spb_id, blk, 0);
	else {
		u32 p;
		u32 b = pda_to_blk_page(scan_start, &p);

		if (b < blk) {
			scan_start = blk_page_make_pda(spb_id, blk, 0);
			ftl_log_trace(LOG_INFO, 0x3e26, "%d, %d >start %x", b, blk,
					scan_start);
		}
	}

	/* rebuild all other pages of latest flush */
	log_scan_blk_page(log, log_recon, scan_start, blk, page);

	return true;
}

inline u32 log_atomic_check(log_t *log, log_meta_t *log_meta, u32 page)
{
	if (log_meta->meta1.flush_idx + 1 == log_meta->meta1.flush_cnt) {
		return page;
	}

	ftl_log_trace(LOG_INFO, 0x2045, "log atomic discard %d-%d-%d", page,
		log_meta->meta1.flush_idx, log_meta->meta1.flush_cnt);

	sys_assert(page >= log_meta->meta1.flush_idx);
	/*
	 * the last flush is not completed
	 * discard this flush
	 */
	if ((page - log_meta->meta1.flush_idx) == 0) {
		page = log->nr_page_per_blk;
	} else {
		page -= log_meta->meta1.flush_idx;
	}

	page -= 1;
	ftl_log_trace(LOG_INFO, 0xd400, "log atomic return page %d", page);

	return page;
}

inline u32 log_atomic_check_ex(log_t *log, log_meta_t *log_meta,
		u32 page, log_recon_t *log_recon, u32 blk,
		spb_id_t spb_id)
{
	pda_t pda;
	u32 i;
	u32 cnt = log_meta->meta1.flush_cnt;
	u32 last_idx = log_meta->meta1.flush_idx;
	nal_status_t ret;

	for (i = 0; i < cnt; i++) {
		pda = blk_page_make_pda(spb_id, blk, page - i);
		ret = ncl_read_one_page(pda, log_recon->data, log_recon->meta_idx);

		if (ficu_du_data_good(ret))
			continue;

		// read error
		ftl_log_trace(LOG_ERR, 0x2a64, "log page rd err %d %d %d, %x", ret, blk, page - i, pda);

		if (log->flags & LOG_F_BACKUP) {
			// try backup
			pda = blk_page_make_pda(log->cur[1].spb_id, log->cur[1].iid, page - i);
			ret = ncl_read_one_page(pda, log_recon->data, log_recon->meta_idx);
			if (ficu_du_data_good(ret)) {
				// discard current blk
				log->next_page = log->nr_page_per_blk;
				continue; // backup successfully
			}

			ftl_log_trace(LOG_ERR, 0xb82b, "backup err %d, %x", ret, pda);
		}

		/* the last flush has error */
		if ((page - last_idx) == 0)
			page = log->nr_page_per_blk;
		else
			page -= last_idx;

		page -= 1;
		ftl_log_trace(LOG_ERR, 0x98f2, "log atomic return page %d, %d/%d",
				page, i, cnt);
		break;
	}
	return page;
}

static inline void log_scan_blk_page(log_t *log, log_recon_t *log_recon,
	pda_t start, u32 latest_blk, u32 latest_page)
{
	u32 blk;
	u32 page;
	spb_id_t spb_id;
	pda_t *l2pt;
	log_meta_t *log_meta;
	void *data;
	u32 meta_idx;
	u32 max_erased;

	log_meta = (log_meta_t*) log_recon->meta;
	data = log_recon->data;
	meta_idx = log_recon->meta_idx;
	l2pt = log_recon->l2pt;

	spb_id = pda_make_blk_page(start, &blk, &page);
	max_erased = (log->flags & LOG_F_BACKUP) ? 2 : 1;

	do {
		pda_t pda;
		nal_status_t err;
		u32 log_page;
		u32 erased;

		ftl_log_trace(LOG_DEBUG, 0x09b2, "log scan [%d %d] to [%d %d]", blk,
				page, latest_blk, latest_page);

		pda = blk_page_make_pda(spb_id, blk, page);
		err = ncl_read_one_page(pda, data, meta_idx);
		if (ficu_du_data_good(err)) {
		fine:
			log_page = log_meta->meta1.log_page_id;
			if (log_page < log->log_page_cnt) {
				l2pt[log_page] = pda;
				ftl_log_trace(LOG_DEBUG, 0x331c, "scan lut[%d] %x",
					log_page, pda);
			}
		} else {
			erased = (err == ficu_err_du_erased) ? 1 : 0;
			ftl_log_trace(LOG_ERR, 0x8498, "log page rd err %d %d %d, %x",
				err, blk, page, pda);

			if (log->flags & LOG_F_BACKUP) {
				pda = blk_page_make_pda(log->cur[1].spb_id, log->cur[1].iid, page);
				err = ncl_read_one_page(pda, data, meta_idx);
				if (ficu_du_data_good(err)) {
					goto fine;
				}
				if (err == ficu_err_du_erased) {
					erased++;
				}
			}
			if (erased == max_erased) {
				ftl_log_trace(LOG_ERR, 0xee62, "log page erased");
			} else {
				panic("log scan error");
			}
		}
		page += 1;
		if (page == log->nr_page_per_blk) {
			blk++;
			page = 0;
		}
	} while (blk_page_lte_blk_page(blk, page, latest_blk, latest_page));
}

inline bool blk_page_lte_blk_page(u32 blk_a, u32 page_a, u32 blk_b,
		u32 page_b)
{
	if (blk_a < blk_b) {
		return true;
	} else if (blk_a == blk_b) {
		if (page_a <= page_b) {
			return true;
		}
	}

	return false;
}

init_code u32 blk_decide_latest_page(u32 s, u32 cnt, spb_id_t spb_id,
	u32 blk, log_recon_t *log_recon)
{
	u32 r = ~0;
	pda_t base;
	pda_t p;
	u32 interleave_du_cnt;
	u32 e = cnt - 1;
	void *data;
	u32 meta_idx;

	base = nal_make_pda(spb_id, blk << DU_CNT_SHIFT);
	interleave_du_cnt = nal_get_interleave();
	interleave_du_cnt <<= DU_CNT_SHIFT;

	data = log_recon->data;
	meta_idx = log_recon->meta_idx;

	while (s <= e) {
		u32 m = (s + e) >> 1;
		nal_status_t ret;

		p = base + interleave_du_cnt * m;

		ret = ncl_read_one_page(p, data, meta_idx);

		if (!ficu_du_erased(ret)) {
			if (!ficu_du_data_good(ret))
				log_recon->read_error = true;

			s = m + 1;
			r = m;
		} else {
			if (m == 0) {
				r = ~0;
				break;
			}
			e = m - 1;
		}
	}

	return r;
}

static inline u32 find_latest_pblk(log_recon_t *log_recon, u32 *flush_id, pblk_t *backup, u32 except)
{
	u32 can = ~0;
	u32 i;

	for (i = 0; i < log_recon->pblk_cnt; i++) {
		if (flush_id[i] == ~0)
			continue;

		if (i == except)
			continue;

		if (backup && backup[i].pblk == log_recon->pblk[i].pblk)
			continue; // backup block

		if (can == ~0 || flush_id[can] < flush_id[i])
			can = i;
	}
	return can;
}

init_code bool log_blk_decide(log_t *log, log_recon_t *log_recon)
{
	u32 i = 0;
	log_meta_t *log_meta;
	u32 flush_id[16];
	pblk_t backup[16];

	log_meta = (log_meta_t*) log_recon->meta;

	do {
		pda_t pda = blk_page_make_pda(log_recon->pblk[i].spb_id, log_recon->pblk[i].iid, 0);
		nal_status_t ret = ncl_read_one_page(pda, log_recon->data, log_recon->meta_idx);

		if (ficu_du_data_good(ret)) {
			flush_id[i] = log_meta->meta0.flush_id;
			backup[i] = log_meta->meta0.backup;
		} else {
			flush_id[i] = ~0;
		}
	} while (++i < log_recon->pblk_cnt);

	if (log->flags & LOG_F_SINGLE) {
		u32 can = find_latest_pblk(log_recon, flush_id, NULL, ~0);

		if (can == ~0)
			return false;

		log->cur[0] = log_recon->pblk[can];
		ftl_log_trace(LOG_INFO, 0x70a0, "single %d-%d", log->cur[0].spb_id, log->cur[0].iid);
	} else {
		pblk_t *b = NULL;

		if (log->flags & LOG_F_BACKUP)
			b = backup;

		// normal ping-pong case, find latest one, put it in current,
		u32 c = find_latest_pblk(log_recon, flush_id, b, ~0);
		u32 n;

		sys_assert(c != ~0);
		log->cur[0] = log_recon->pblk[c];

		n = find_latest_pblk(log_recon, flush_id, b, c);
		if (n != ~0)
			log->next[0] = log_recon->pblk[n];

		if (b) {
			log->cur[1] = b[c];
			log->next[1] = b[n];
		}

		ftl_log_trace(LOG_INFO, 0x8a03, "cur %x, b %x", log->cur[0].pblk, log->cur[1].pblk);
		ftl_log_trace(LOG_INFO, 0x60fd, "nex %x, b %x", log->next[0].pblk, log->next[1].pblk);

	}
	return true;
}

init_code void log_blk_recycled(log_t *log, log_recon_t *log_recon)
{
	u32 i;

	for (i = 0; i < log_recon->pblk_cnt; i++) {
		if (log_recon->pblk[i].spb_id == log->cur[0].spb_id &&
			log_recon->pblk[i].iid == log->cur[0].iid)
			continue;

		blk_pool_put_pblk(log_recon->pblk[i]);
	}
}

/*! @} */
