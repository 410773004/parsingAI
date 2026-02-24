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

#include "types.h"
#include "io.h"
#include "trng.h"
#include "stdio.h"
#include "itrng_reg.h"
#include "console.h"
/*! \cond PRIVATE */
#define __FILEID__ trng
#include "trace.h"
/*! \endcond */

static inline u32 trng_readl(u32 reg)
{
	return readl((void *)(TRNG_BASE + reg));
}

static inline void trng_writel(u32 data, u32 reg)
{
	writel(data, (void *)(TRNG_BASE + reg));
}

static void trng_mode_switch(u32 mode)
{
	mode &= TRNG_MODE_MASK;
	trng_writel(mode, MODE);
}

static void trng_send_req(enum trng_req req)
{
	trng_writel((u32)req, REQ);
	reqsts_t reqsts;
	/* waiting request done status */
	do {
		reqsts.all = trng_readl(REQSTS);
	} while(!(reqsts.all & req));
	/* clear request */
	trng_writel(0, REQ);
}

static void trng_seed_flow(u8 flow_sel, enum trng_req req, u32 *seed)
{
	if (flow_sel & TRNG_NORMAL_SEEDING) {
		cntl_t cntl = {
			.all = trng_readl(CNTL),
		};
		cntl.b.dro_en = 1;
		cntl.b.ed_sel = 1;
		trng_writel(cntl.all, CNTL);
		/* switch to functional mode */
		trng_mode_switch(TRNG_FUNC_MODE);
	} else if (flow_sel & TRNG_NONCE_SEEDING) {
		u8 i;
		u32 ofst = SEED_0;
		/* switch to functional mode and nonce mode */
		trng_mode_switch(TRNG_FUNC_MODE | TRNG_NONCE_MODE);
		for (i = TRNG_SEED0; i <= TRNG_SEED7; i++, ofst += 4)
			trng_writel(seed[i], ofst);
	}
	/* send request: req_seed, req_seed_quick, req_nonce_seed */
	trng_send_req(req);
}

#if 0
static void trng_selftest(void)
{
	bist_t bist;
	/* step 1: provid a setting for <run_cnt_5_min> to <run_cnt_1_min>, recommand setting {1, 2, 4, 8, 15} */
	bist.b.run_cnt_1_min = 15;
	bist.b.run_cnt_2_min = 8;
	bist.b.run_cnt_3_min = 4;
	bist.b.run_cnt_4_min = 2;
	bist.b.run_cnt_5_min = 1;
	trng_writel(bist.all, BIST);
	/* step 2: clear run count */
	bist.all = trng_readl(BIST);
	bist.b.run_cnt_clr = 1;
	trng_writel(bist.all, BIST);
	/* step 3: set run count clear to 0 */
	bist.all = trng_readl(BIST);
	bist.b.run_cnt_clr = 0;
	trng_writel(bist.all, BIST);
	/* step 4: enable run count */
	bist.all = trng_readl(BIST);
	bist.b.run_cnt_en= 1;
	trng_writel(bist.all, BIST);
	/* step 5: Enable DRO */
	cntl_t cntl = { .all = trng_readl(CNTL), };
	cntl.b.dro_en = 1;
	trng_writel(cntl.all, CNTL);
	/* step 6: Random Bit edge select 1 */
	cntl.all = trng_readl(CNTL);
	cntl.b.ed_sel = 1;
	trng_writel(cntl.all, CNTL);
	/* step 7: wait until sts_seeded=1 and run_cnt_okay=1 */
	sts_t sts;
	do {
		sts.all = trng_readl(STS);
		bist.all = trng_readl(BIST);
	} while(sts.b.sts_seeded && bist.b.run_cnt_okay);
}
#endif

void trng_gen_random(u32 *buf, u32 dw_cnt)
{
	u32 *_buf = buf;
	do {
		u32 i, ofst = RAND_0;
		trng_send_req(TRNG_REQ_RNAD_GEN);
		for (i = TRNG_RAND0; i <= TRNG_RAND7; i++, ofst += 4) {
			*_buf = trng_readl(ofst);
			// rtos_trng_trace(LOG_ALW, 0, "addr: 0x%x val: 0x%x left: %d", _buf, *_buf, dw_cnt - 1);
			if(--dw_cnt == 0)
				return;
			_buf++;
		}
	} while(dw_cnt > 0);
}

init_code void trng_init(void)
{
	trng_seed_flow(TRNG_NORMAL_SEEDING, TRNG_REQ_SEED, NULL);
	rtos_trng_trace(LOG_ALW, 0x390b, "trng reseeding done");
}
