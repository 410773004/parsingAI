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
/*! \file ra.c
 * @brief read ahead support
 *
 * \addtogroup dispatcher
 * \defgroup dispatcher
 * \ingroup dispatcher
 * @{
 *
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
/*
 * XXX: CAVEAT
 *
 */
#include "types.h"
#include "bf_mgr.h"
#include "ra.h"
#include "string.h"
#include "assert.h"
#include "misc.h"
#include "console.h"
#include "list.h"
#include "l2p_mgr.h"
#include "cpu_msg.h"
#include "ipc.h"
#include "btn_export.h"
#include "event.h"
#include "stdlib.h"
#include "mpc.h"
#include "fc_export.h"
#include "trim.h"

#define __FILEID__ ra
#include "trace.h"

#define RA_ROUND_SIZE 32
#define RA_DET_IO_SIZE 32	/// could be changed

#define RA_T_IDLE 0x0
#define RA_T_READY 0x2
#define RA_T_DISABLE 0x3

#define RA_NRM_DTAG 0
#define RA_UNMAP_DTAG 1
#define RA_ERR_DTAG	2

#define RA_DISABLE_TIMES 2
#define RA_LDA_MONITOR 1

#define RDISK_L2P_FE_SRCH_QUE 			0	///< l2p search queue id
//#define RA_MAX_DU_DEPTH				256 	///< must be power of two
#define RA_MAX_DU_DEPTH                 320     /// (32 * 10) Cover range 128K QD 8

//#define RA_SEQ_DET(lda, ndu) ra_seq_detect(lda, ndu)
#if (CO_SUPPORT_READ_AHEAD == TRUE)
enum {
	RA_DTAG_T_FREE,
	RA_DTAG_T_READING,
	RA_DTAG_T_ABORTING,
	RA_DTAG_T_VALID,
};

typedef struct
{
	u8 sts;
	u8 type;
	u16 ref_cnt;
} ra_dtag_info_t;

typedef struct
{
	bool auth;				///< true: enable ra feature, fals: disable ra feature
	bool suspended;
	u32 state;
	u32 statis_seqs;		///< seq size
	u32 statis_ras;			///< data ra reads dus
	u32 statis_obs;			///< data obsolate dus
	u32 statis_dos;			///< data out times
	u32 statis_dps;			///< data pend times
	u32 static_rbs;			///< ref block times
	lda_t slda; 			///< lda range start lda
	lda_t elda; 			///< lda range end lda
	u16 vptr;				///< valid ptr, range for data out
	u16 vcnt;				///< valid cnt, range for data out
	u16 wcnt;				///< whole cnt, range for data out
	u16 fptr; 				///< end ptr, range for pa read
	u16 fcnt;				///< free cnt, range for pa read
	u32 disable_times;
	bool read_pend;
	struct timer_list timer; ///< use for idle release
} rmgr_t;

typedef struct
{
	lda_t slda;
	lda_t elda;
	lda_t disturb_slda;
	lda_t disturb_elda;
	u32 disturb_cnt;
	s32 depth_fits;
	s16 weight;
	bool   seq;
} seq_det_t;

fast_data seq_det_t seq_det;
fast_data rmgr_t rmgr;

typedef struct
{
	ra_dtag_info_t dtag_infos[RA_DTAG_CNT];
	#if RA_LDA_MONITOR
	lda_t ldas[RA_DTAG_CNT];
	#endif
} RaLaatbl_t;

sram_sh_data RaLaatbl_t gRaLaaTbl;

share_data_zi u16 ra_btag;
share_data_zi u32 shr_ra_ddtag_start;
fast_data u8 evt_ra_read_resume = 0xFF;
fast_data bool ra_init_done = false;
fast_data u32 fast_read_in_seq = 0;
fast_data volatile bool prev_key = 0;
fast_data volatile u32 prev_ndu;

extern bool ucache_read_hit(lda_t lda);
extern ce_t* cache_search(u32 lda);

static void ra_exec(lda_t exp_lda);

static fast_code ALWAYS_INLINE bool ra_depth_fit_chk(seq_det_t *det, u32 ndu)
{
#if (Tencent_case)
	bool ret;
	//TODO DISCUSS
	if (ndu * get_btn_running_cmds() <= RA_MAX_DU_DEPTH || ndu <= 16)  // 16 for cover 64K
	{
		if(prev_key == 0 && prev_ndu != ndu)
		{
			ret = false;
		}
		else
		{
			ret = true;
		}
		prev_key = 1;
	}
	else
	{
		ret = false;
		prev_key = 0;
	}
	return ret;
#else
	bool ret;
	//TODO DISCUSS
	if (ndu * get_btn_running_cmds() <= RA_MAX_DU_DEPTH)
	{
		ret = true;
	}
	else
	{
		ret = false;
	}
	return ret;
#endif
}

fast_code ALWAYS_INLINE bool ra_seq_detect(lda_t lda, u32 ndu)
{
	// todo: detect with rand io-size, or detect with same io-size
	seq_det_t *det = &seq_det;

	if (lda == det->elda)
	{
		det->slda = lda;
		det->elda = lda + ndu;

		if (!ra_depth_fit_chk(det, ndu))
		{
			det->seq = false;
			return det->seq;
		}

		if(!det->seq)
		{
			// Limit Read Ahead start up condition
			fast_read_in_seq++;

			if (fast_read_in_seq >= RA_CMD_SEQUENTIAL_CNT) //
			{
				det->seq = true;
			}
		}
	}
	else
	{
		det->slda = lda;
		det->elda = lda + ndu;
		fast_read_in_seq = 0;

		if (det->seq == true)
		{
			det->seq = false;
		}
	}

	return det->seq;
}
// #endif

fast_code bool ra_dtag_get(u32 lda)
{
	rmgr_t *ra_mgr = &rmgr;
	u32 fptr = ra_mgr->fptr;
	ra_dtag_info_t *dtag_info;

	dtag_info = &gRaLaaTbl.dtag_infos[fptr];

	if (dtag_info->ref_cnt > 0)
	{
		ra_mgr->static_rbs++;
		return false;
	}

	if (dtag_info->sts != RA_DTAG_T_FREE)
	{
		sys_assert(dtag_info->sts == RA_DTAG_T_ABORTING);
		return false;
	}

	/* alloc it for pa read */
	dtag_info->type = RA_NRM_DTAG;
	dtag_info->sts = RA_DTAG_T_READING;

#if RA_LDA_MONITOR
	gRaLaaTbl.ldas[fptr] = lda;
	if (fptr >= ra_mgr->vptr)
		sys_assert(lda == ra_mgr->slda + fptr - ra_mgr->vptr);
	else
		sys_assert(lda == ra_mgr->slda + RA_DTAG_CNT + fptr - ra_mgr->vptr);
#endif

	ra_mgr->fptr = (fptr + 1) % RA_DTAG_CNT;
	ra_mgr->fcnt--;
	return true;
}

fast_code ALWAYS_INLINE void ra_unmap_data_in(u32 ofst)
{
	ra_dtag_info_t *dtag_info;
	rmgr_t *ra_mgr = &rmgr;

	sys_assert(ofst < RA_DTAG_CNT);

	dtag_info = &gRaLaaTbl.dtag_infos[ofst];


	if (dtag_info->sts == RA_DTAG_T_ABORTING)
	{
		ra_mgr->fcnt++;
		dtag_info->sts = RA_DTAG_T_FREE;
		return;
	}

	sys_assert(dtag_info->sts == RA_DTAG_T_READING);
	dtag_info->type = RA_UNMAP_DTAG;
	dtag_info->sts = RA_DTAG_T_VALID;
}

fast_code void ra_err_data_in(u32 ofst)
{
	ra_dtag_info_t *dtag_info;
	rmgr_t *ra_mgr = &rmgr;

	sys_assert(ofst < RA_DTAG_CNT);

	dtag_info = &gRaLaaTbl.dtag_infos[ofst];

	if (dtag_info->sts == RA_DTAG_T_ABORTING)
	{
		ra_mgr->fcnt++;
		dtag_info->sts = RA_DTAG_T_FREE;
		return;
	}

	#ifdef RA_ERROR_WORKAROUND
	if (ra_mgr->state == RA_T_DISABLE)
	{
		if (dtag_info->sts != RA_DTAG_T_READING)
		{
			ra_mgr->disable_times = 0xFFFFFFFF;
			dtag_info->sts = RA_DTAG_T_FREE;
			return;
		}
	}
	else
	#endif
	{
		sys_assert(dtag_info->sts == RA_DTAG_T_READING);
	}
	dtag_info->type = RA_ERR_DTAG;
	dtag_info->sts = RA_DTAG_T_VALID;
}

fast_code ALWAYS_INLINE void ra_nrm_data_in(u32 ofst, dtag_t dtag)
{
	ra_dtag_info_t *dtag_info;
	rmgr_t *ra_mgr = &rmgr;

	dtag_info = &gRaLaaTbl.dtag_infos[ofst];
	sys_assert(shr_ra_ddtag_start + ofst == dtag.b.dtag);

	if (dtag_info->sts == RA_DTAG_T_ABORTING)
	{
		ra_mgr->fcnt++;
		dtag_info->sts = RA_DTAG_T_FREE;
		return;
	}

	#ifdef RA_ERROR_WORKAROUND
	if (ra_mgr->state == RA_T_DISABLE)
	{
		if (dtag_info->sts != RA_DTAG_T_READING)
		{
			ra_mgr->disable_times = 0xFFFFFFFF;
			dtag_info->sts = RA_DTAG_T_FREE;
			return;
		}
	}
	else
	#endif
	{
		sys_assert(dtag_info->sts == RA_DTAG_T_READING);
	}

	dtag_info->sts = RA_DTAG_T_VALID;
	//disp_apl_trace(LOG_DEBUG, 0, "laa[0x%x] ofst:%d ", gRaLaaTbl.ldas[ofst],ofst);
}

fast_code ALWAYS_INLINE void ra_dtag_ref_inc(u32 ofst)
{
	gRaLaaTbl.dtag_infos[ofst].ref_cnt++;
	sys_assert(gRaLaaTbl.dtag_infos[ofst].ref_cnt > 0);
}

fast_code ALWAYS_INLINE void ra_dtag_ref_dec(u32 ofst)
{
	sys_assert(gRaLaaTbl.dtag_infos[ofst].ref_cnt > 0);
	gRaLaaTbl.dtag_infos[ofst].ref_cnt--;
}

fast_code void ra_validate(void)
{
	rmgr_t *ra_mgr = &rmgr;

	if (ra_mgr->state != RA_T_READY || ra_mgr->wcnt == ra_mgr->vcnt)
		goto out;

	u32 cptr;
	u32 reading;
	u32 valids = 0;

	sys_assert(ra_mgr->wcnt > ra_mgr->vcnt);
	reading = ra_mgr->wcnt - ra_mgr->vcnt;
	cptr = (ra_mgr->vptr + ra_mgr->vcnt) % RA_DTAG_CNT;

	while (gRaLaaTbl.dtag_infos[cptr].sts == RA_DTAG_T_VALID && valids < reading)
	{
		cptr = (cptr + 1) % RA_DTAG_CNT;
		valids++;
	}

	sys_assert(valids <= ra_mgr->wcnt - ra_mgr->vcnt);
	ra_mgr->vcnt += valids;

out:
	if (ra_mgr->read_pend)
		evt_set_cs(evt_ra_read_resume, 0, 0, CS_TASK);
}

fast_code void ra_dtags_put(u32 cnt)
{
	u32 i;
	rmgr_t *ra_mgr = &rmgr;
	u32 pptr = ra_mgr->vptr;

	for (i = 0; i < cnt; i++)
	{
		sys_assert(gRaLaaTbl.dtag_infos[pptr].sts == RA_DTAG_T_VALID);

		gRaLaaTbl.dtag_infos[pptr].sts = RA_DTAG_T_FREE;
		pptr = (pptr + 1) % RA_DTAG_CNT;
	}

	ra_mgr->vcnt -= cnt;
	ra_mgr->wcnt -= cnt;
	ra_mgr->vptr = pptr;
	ra_mgr->fcnt += cnt;
}

static fast_code ALWAYS_INLINE u32 ra_obsolete(lda_t lda)
{
	rmgr_t *ra_mgr = &rmgr;

	sys_assert(lda > ra_mgr->slda);
	if (ra_mgr->vcnt == 0)
		return 0;

	u32 obs_cnt = min(lda - ra_mgr->slda, ra_mgr->vcnt);
	ra_mgr->slda += obs_cnt;
	ra_dtags_put(obs_cnt);
	return obs_cnt;
}

fast_code void ra_abort(rmgr_t *ra_mgr)
{
	if (ra_mgr->state == RA_T_READY)
	{
		if (ra_mgr->fcnt < RA_DTAG_CNT)
		{
			u32 sts;
			ra_dtag_info_t *dtag_info;

			while (ra_mgr->wcnt)
			{
				dtag_info = &gRaLaaTbl.dtag_infos[ra_mgr->vptr];
				sts = dtag_info->sts;

				if (sts != RA_DTAG_T_READING)
				{
					if (sts == RA_DTAG_T_VALID)
					{
						ra_mgr->fcnt++;
						ra_mgr->statis_obs++;
						dtag_info->sts = RA_DTAG_T_FREE;
					}
				}
				else
				{
					dtag_info->sts = RA_DTAG_T_ABORTING;
					ra_mgr->statis_obs++;
				}

				ra_mgr->vptr = (ra_mgr->vptr + 1) % RA_DTAG_CNT;
				ra_mgr->wcnt--;
			}
		}
		else
		{
			sys_assert(ra_mgr->fcnt == RA_DTAG_CNT);
		}
	}

	sys_assert(ra_mgr->wcnt == 0);
	sys_assert(ra_mgr->vptr == ra_mgr->fptr);
	ra_mgr->vcnt = 0;
	ra_mgr->state = RA_T_IDLE;
	seq_det.seq = false;
	seq_det.weight = 0;
}

fast_code ALWAYS_INLINE bool is_ra_dtag(u32 _dtag)
{
	dtag_t dtag = { .dtag = _dtag };
	u32 did = dtag.b.dtag;

	if (dtag.b.in_ddr && did >= shr_ra_ddtag_start && did < shr_ra_ddtag_start + RA_DTAG_CNT)
	{
		ra_dtag_ref_dec(did - shr_ra_ddtag_start);
		return true;
	}

	return false;
}

fast_code bool ra_data_out(u32 btag, lda_t lda, u32 ndu)
{
	dtag_t dtag;
	u32 ra_ofst;
	u32 du_ofst = 0;
	rmgr_t *ra_mgr = &rmgr;
	/* release obsolate dtags */
	if (ra_mgr->vcnt && (lda >= ra_mgr->slda) && ((lda + ndu) <= (ra_mgr->slda + ra_mgr->vcnt)))
	{
		if (ra_mgr->slda < lda)
		{
			ra_obsolete(lda);
		}


		do {
			ra_ofst = (ra_mgr->vptr + du_ofst) % RA_DTAG_CNT;
			dtag.dtag = shr_ra_ddtag_start + ra_ofst;
			dtag.b.in_ddr = 1;

			#if RA_LDA_MONITOR
			sys_assert(gRaLaaTbl.ldas[ra_ofst] == lda + du_ofst);
			#endif

			if (gRaLaaTbl.dtag_infos[ra_ofst].type == RA_NRM_DTAG)
			{
				ra_dtag_ref_inc(ra_ofst);
			}
			else if (gRaLaaTbl.dtag_infos[ra_ofst].type == RA_UNMAP_DTAG)
			{
				dtag.dtag = RVTAG_ID;
			}
			else
			{
				sys_assert(gRaLaaTbl.dtag_infos[ra_ofst].type == RA_ERR_DTAG);
				dtag.dtag = EVTAG_ID;
			}

			bm_rd_dtag_commit(du_ofst, btag, dtag);
			du_ofst++;
		} while (du_ofst < ndu);

		ra_mgr->statis_dos += ndu;
		ra_mgr->read_pend = false;
		return true;
	}
	else
	{
		// NDU * CMD QD = 1M will happen or Read ahead is slower than cmd seq
		ra_mgr->statis_dps++;
		ra_mgr->read_pend = true;
		return false;
	}
}

static fast_code ALWAYS_INLINE void ra_fsm_handle(u32 exp_lda)
{
	switch (rmgr.state)
	{
	case RA_T_IDLE:
		rmgr.state = RA_T_READY;
	case RA_T_READY:
		ra_exec(exp_lda);
		break;
	default:
		sys_assert(false);
		break;
	}
}

fast_code ALWAYS_INLINE void ra_data_out_cmpl(void)
{
	if (rmgr.state == RA_T_READY)
	{
		ra_fsm_handle(0);
	}
}

static fast_code ALWAYS_INLINE void ra_submit(lda_t slda, u32 ofset, u32 lda_cnt)
{
	rmgr_t *ra_mgr = &rmgr;

	sys_assert(lda_cnt);
	sys_assert(lda_cnt <= RA_ROUND_SIZE);
	ra_mgr->statis_ras += lda_cnt;
	ra_mgr->elda = slda + lda_cnt;
	ra_mgr->wcnt += lda_cnt;


	if (ofset + lda_cnt <= RA_DTAG_CNT)
	{
		l2p_srch_ofst(slda, lda_cnt, ra_btag, ofset, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
	}
	else
	{
		u32 cnt = RA_DTAG_CNT - ofset;
		l2p_srch_ofst(slda, cnt, ra_btag, ofset, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
		slda += cnt;
		cnt = ofset + lda_cnt - RA_DTAG_CNT;
		l2p_srch_ofst(slda, cnt, ra_btag, 0, RDISK_L2P_FE_SRCH_QUE, SRCH_NRM);
	}
}

static fast_code ALWAYS_INLINE void ra_exec(lda_t exp_lda)
{
	rmgr_t *ra_mgr = &rmgr;

	/* ooo happened. obs all */
	if (ra_mgr->elda < exp_lda)
	{
		ra_mgr->statis_obs += ra_obsolete(exp_lda);
		if (ra_mgr->wcnt)
		{
			ra_abort(ra_mgr);
			return;
		}
	}

	if (ra_mgr->fcnt < RA_ROUND_SIZE)
	{
		return;
	}

	if (ra_mgr->wcnt == 0)
	{
		/* reset the ra range */
		sys_assert (ra_mgr->vptr == ra_mgr->fptr);
		ra_mgr->slda = exp_lda;
		ra_mgr->elda = exp_lda;
	}

	u32 ofst;
	lda_t slda;
	u32 cnt = 0;

	slda = ra_mgr->elda;
	ofst = ra_mgr->fptr;

	/* no need care whether slda out of max cap*/
	while(true)
	{
		lda_t lda = slda + cnt;

		/* data in write cache or ra cache is in xfer */
		//if (ucache_read_hit(lda) || !ra_dtag_get(lda))//
		if ((cache_search(lda) != NULL) || !ra_dtag_get(lda))//
		{
			break;
		}

		cnt++;

		if (cnt == RA_ROUND_SIZE)
		{
			ra_submit(slda, ofst, RA_ROUND_SIZE);

			if (ra_mgr->fcnt < RA_ROUND_SIZE)
			{
				return;
			}

			slda += RA_ROUND_SIZE;
			ofst = (ofst + RA_ROUND_SIZE) % RA_DTAG_CNT;
			cnt = 0;
		}
	}

	if (cnt)
	{
		ra_submit(slda, ofst, cnt);
	}
}

fast_code ALWAYS_INLINE bool ra_whole_hit(lda_t lda, u32 ndu)
{
	/* simply check read all hit in ra buffer include the reading lda */
	return (((rmgr.state == RA_T_READY)) && rmgr.wcnt && (lda >= rmgr.slda) && ((lda + ndu) <= (rmgr.slda + rmgr.wcnt)));
}

fast_code ALWAYS_INLINE bool ra_range_chk(lda_t lda, u32 ndu)
{
	return (((rmgr.state == RA_T_READY)) && rmgr.wcnt && (lda < (rmgr.slda + rmgr.wcnt)) && (rmgr.slda < (lda + ndu)));
}

static fast_code void _ra_disable(u32 dis_times)
{
	if (!ra_init_done) // may call before init done
		return;

	rmgr_t *ra_mgr = &rmgr;
	/* make ra disabled at later RA_DISABLE_TIMES 100ms */
	if (ra_mgr->disable_times < dis_times)
	{
		ra_mgr->disable_times = dis_times;
	}

	if (ra_mgr->state != RA_T_DISABLE)
	{
		/* prevent do ra */
		if (ra_mgr->state != RA_T_IDLE)
		{
			ra_abort(ra_mgr);
		}

		ra_mgr->state = RA_T_DISABLE;
	}
}

fast_code ALWAYS_INLINE void ra_disable(void)
{
	_ra_disable(RA_DISABLE_TIMES);
}

fast_code ALWAYS_INLINE void ra_disable_time(u32 time)
{
	_ra_disable(time); // 100ms unit
}

extern Trim_Info TrimInfo;

fast_code ALWAYS_INLINE void ra_forecast(lda_t lda, u32 ndu)
{
	rmgr_t *ra_mgr = &rmgr;

	if (ra_mgr->state == RA_T_DISABLE || TrimInfo.Dirty)
		return;

	if ((ndu == 1) && (get_btn_rd_otf_cnt() > 128)) // prevent large QD and job performance drop
	{
		ra_disable();
		return;
	}

	if (unlikely((ra_seq_detect(lda, ndu) == true)))
	{
		ra_mgr->statis_seqs += ndu;
		ra_fsm_handle(lda + ndu);
	}
	else
	{
		/* non-seq, release all ra dtags */
		if (ra_mgr->state != RA_T_IDLE)
		{
			ra_abort(ra_mgr);
		}
	}
}

fast_code void ra_idle_rls(void *data)
{
	rmgr_t *ra_mgr = &rmgr;
	static u32 last_seq_cnt = 0;

	if (ra_mgr->disable_times)
	{
		ra_mgr->disable_times--;
		mod_timer(&ra_mgr->timer, jiffies + 1); // 100ms
		return;
	}

	if (ra_mgr->state == RA_T_DISABLE)
	{
		if (ra_mgr->fcnt != RA_DTAG_CNT)
		{
			mod_timer(&ra_mgr->timer, jiffies + 1); // 100ms
			return;
		}
		else
		{
			ra_mgr->state = RA_T_IDLE;
		}
	}

	if (last_seq_cnt != ra_mgr->statis_seqs)
	{
		last_seq_cnt = ra_mgr->statis_seqs;
		mod_timer(&ra_mgr->timer, jiffies + 5); // 500ms
		return;
	}

	if (ra_mgr->state != RA_T_DISABLE && ra_mgr->state != RA_T_IDLE)
	{
		ra_abort(ra_mgr);
	}

	mod_timer(&ra_mgr->timer, jiffies + 5); // 500ms
}

static fast_code void ipc_ra_read_error(volatile cpu_msg_req_t *req)
{
	u32 ofst = req->pl;
	ra_disable_time(20);
	ra_err_data_in(ofst);
	ra_validate();
}

fast_code void ra_resume(void)
{
	rmgr_t *ra_mgr = &rmgr;
	u32 i;

	for (i = 0; i < RA_DTAG_CNT; i++)
	{
		if (gRaLaaTbl.dtag_infos[i].sts == RA_DTAG_T_ABORTING)
		{
			gRaLaaTbl.dtag_infos[i].sts = RA_DTAG_T_FREE;
			ra_mgr->fcnt++;
		}

		if (gRaLaaTbl.dtag_infos[i].ref_cnt)
		{
			gRaLaaTbl.dtag_infos[i].ref_cnt = 0;
		}
	}

	ra_mgr->auth = true;
	ra_mgr->fcnt = RA_DTAG_CNT;
	ra_mgr->disable_times = RA_DISABLE_TIMES;
}

fast_code ALWAYS_INLINE void ra_clearReadPend(void)
{
	rmgr_t *ra_mgr = &rmgr;
	ra_mgr->read_pend = false;
}

fast_code void ra_forcestartup(void)
{
	rmgr_t *ra_mgr = &rmgr;
	if (ra_mgr->disable_times > RA_DISABLE_TIMES)
	{
		ra_mgr->disable_times = RA_DISABLE_TIMES;
	}
}

ddr_code void ra_init(evt_handler_t evt_handle)
{
	rmgr_t *ra_mgr = &rmgr;
	memset(ra_mgr, 0, sizeof(rmgr_t));
	memset(&seq_det, 0, sizeof(seq_det_t));
	memset(&gRaLaaTbl, 0, sizeof(RaLaatbl_t));

	ra_mgr->timer.function = ra_idle_rls;
	ra_mgr->timer.data = "ra_idle_rls";
	/* set auth be false if want to close ra feature */
	ra_mgr->auth = true;
	sys_assert(evt_handle != NULL);
	evt_register(evt_handle, 0, &evt_ra_read_resume);
	ra_btag = RA_OFF;
	/* already allocated by rdisk_ddtag_prep */
	sys_assert(shr_ra_ddtag_start != 0);
	ra_mgr->fcnt = RA_DTAG_CNT;
	// todo: only trgger this timer when do ra */
	cpu_msg_register(CPU_MSG_RA_READ_ERROR, ipc_ra_read_error);
	mod_timer(&ra_mgr->timer, jiffies + HZ);
	ra_mgr->disable_times = RA_DISABLE_TIMES;

	seq_det.slda = ~0;
	seq_det.elda = ~0;
	ra_init_done = true;
}

ddr_code void ra_dump(void)
{
	disp_apl_trace(LOG_INFO, 0xbbd3, "RA: stat %d suspend %d pending %d vlda %d->%d",
			rmgr.state, rmgr.suspended, rmgr.read_pend, rmgr.slda, rmgr.elda);
	disp_apl_trace(LOG_INFO, 0xad40, "RA: vptr %d vcnt %d wcnt %d fptr %d fcnt %d",
			rmgr.vptr, rmgr.vcnt, rmgr.wcnt, rmgr.fptr, rmgr.fcnt);
	disp_apl_trace(LOG_ALW, 0x0e65, "ra dos %d",rmgr.statis_dos);
	disp_apl_trace(LOG_ALW, 0x8470, "seq det slda %x elda %x seq:%d",seq_det.slda, seq_det.elda,seq_det.seq);
}

static ddr_code int ra_main(int argc, char *argv[])
{
	ra_dump();
	return 1;
}

static DEFINE_UART_CMD(ra_main, "ra", "ra",
		"ra status", 0, 0, ra_main);
#endif
