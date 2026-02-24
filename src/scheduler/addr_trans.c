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

/*! \file addr_trans.c
 * @brief scheduler module, translate between pda, fda, cpda and l2pfda
 *
 * \addtogroup scheduler
 * \defgroup scheduler
 * \ingroup scheduler
 * @{
 */

#include "sect.h"
#include "stdlib.h"
#include "bf_mgr.h"
#include "l2p_mgr.h"
#include "queue.h"
#include "ncl_exports.h"

share_data struct nand_info_t shr_nand_info;		///< shared nand information

fast_code pda_t _cpda2pda(pda_t cpda)
{
	pda_t pda;
	u32 ch, ce, lun, plane, block, page, du;

	// CPDA to ch, ce, lun, pl, blk, pg, du
	du = cpda & (DU_CNT_PER_PAGE - 1);
	cpda >>= DU_CNT_SHIFT;
	plane = cpda & (nand_info.geo.nr_planes- 1);
	cpda >>= ctz(nand_info.geo.nr_planes);
	lun = cpda & (nand_info.geo.nr_luns - 1);
	cpda >>= ctz(nand_info.geo.nr_luns);
	ce = cpda % nand_info.geo.nr_targets;
	cpda /= nand_info.geo.nr_targets;
	ch = cpda % nand_info.geo.nr_channels;
	cpda /= nand_info.geo.nr_channels;
	page = cpda % nand_info.geo.nr_pages;
	block = cpda / nand_info.geo.nr_pages;

	// ch, ce, lun, pl, blk, pg, du to PDA
	pda = block << nand_info.pda_block_shift;
	pda |= page << nand_info.pda_page_shift;
	pda |= ch << nand_info.pda_ch_shift;
	pda |= ce << nand_info.pda_ce_shift;
	pda |= lun << nand_info.pda_lun_shift;
	pda |= plane << nand_info.pda_plane_shift;
	pda |= du << nand_info.pda_du_shift;

	return pda;
}

fast_code pda_t _pda2cpda(pda_t pda)
{
	pda_t cpda;
	u32 ch, ce, lun, plane, block, page, du;

	// Split each fields
	ch = (pda >> nand_info.pda_ch_shift) & (nand_info.geo.nr_channels - 1);
	ce = (pda >> nand_info.pda_ce_shift) & (nand_info.geo.nr_targets - 1);
	lun = (pda >> nand_info.pda_lun_shift) & (nand_info.geo.nr_luns - 1);
	plane = (pda >> nand_info.pda_plane_shift) & (nand_info.geo.nr_planes - 1);
	block = (pda >> nand_info.pda_block_shift) & nand_info.pda_block_mask;
	page = (pda >> nand_info.pda_page_shift) & nand_info.pda_page_mask;
	du = (pda >> nand_info.pda_du_shift) & (DU_CNT_PER_PAGE - 1);

	// Construct CPDA
	cpda = block;
	cpda *= nand_page_num();
	cpda += page;
	cpda *= nand_info.geo.nr_channels;
	cpda += ch;
	cpda *= nand_info.geo.nr_targets;
	cpda += ce;
	cpda *= nand_info.geo.nr_luns;
	cpda += lun;
	cpda *= nand_info.geo.nr_planes;
	cpda += plane;
	cpda *= DU_CNT_PER_PAGE;
	cpda += du;
	return cpda;
}

fast_code fda_t _cpda2fda(u32 cpda)
{
	fda_t fda_ret;
	fda_ret.du = cpda & (DU_CNT_PER_PAGE - 1); cpda >>= ctz(DU_CNT_PER_PAGE);
	fda_ret.pl = cpda & (nand_plane_num() - 1); cpda >>= ctz(nand_plane_num());
	fda_ret.lun = cpda & (nand_lun_num() - 1); cpda >>= ctz(nand_lun_num());
	fda_ret.ce = cpda % nand_target_num(); cpda /= nand_target_num();
	fda_ret.ch = cpda % nand_channel_num(); cpda /= nand_channel_num();
	fda_ret.pg = cpda % nand_page_num(); cpda /= nand_page_num();
	fda_ret.blk = cpda;
	return fda_ret;
}

fast_code l2p_fda_t pda2l2pfda(u32 pda)
{
	l2p_fda_t fda;

	fda.dw0.b.pda_du = 0;
	fda.dw0.b.pda_pl = (pda >> shr_nand_info.pda_plane_shift) & (nand_plane_num() - 1);
	fda.dw0.b.pda_ch = (pda >> shr_nand_info.pda_ch_shift) & (nand_channel_num() - 1);
	fda.dw0.b.pda_ce = (pda >> shr_nand_info.pda_ce_shift) & (nand_target_num() - 1);
	fda.dw0.b.pda_lun = (pda >> shr_nand_info.pda_lun_shift) & (nand_lun_num() - 1);
	fda.dw0.b.pda_pg = (pda >> shr_nand_info.pda_page_shift) & shr_nand_info.pda_page_mask;

	fda.dw1.b.pda_blk = (pda >> shr_nand_info.pda_block_shift) & shr_nand_info.pda_block_mask;
	return fda;
}

fast_code fda_t _pda2fda(u32 pda)
{
	return _cpda2fda(_pda2cpda(pda));
}

fast_code pda_t _fda2cpda(fda_t fda)
{
	pda_t cpda;

	cpda = fda.blk;
	cpda *= nand_page_num();  cpda += fda.pg;
	cpda *= nand_channel_num();  cpda += fda.ch;
	cpda *= nand_target_num();  cpda += fda.ce;
	cpda *= nand_lun_num(); cpda += fda.lun;
	cpda *= nand_plane_num();  cpda += fda.pl;
	cpda *= DU_CNT_PER_PAGE;  cpda += fda.du;

	return cpda;
}

fast_code pda_t _fda2pda(fda_t fda)
{
	return _cpda2pda(_fda2cpda(fda));
}
