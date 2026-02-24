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
/*! \file blk_pool.c
 * @brief define physical block pool
 *
 * \addtogroup ftl
 *
 * @{
 */
#include "ftlprecomp.h"
#include "blk_pool.h"
#include "sync_ncl_helper.h"
#include "ncl_cmd.h"
#include "log.h"
#include "blk_log.h"

/*! \cond PRIVATE */
#define __FILEID__ pbpl
#include "trace.h"
/*! \endcond */

typedef struct _blk_pool_t {
	u16 valid;
	u16 total;
	u16 erase_busy;
	pblk_t root_blk;
} blk_pool_t;

fast_data_zi static blk_pool_t _blk_pool;
fast_data_zi phy_blk_t phy_blks[FTL_PBLK_MAX];
fast_data_ni u32 siga[FTL_PBLK_MAX];
slow_data_ni ncl_blk_res_t _pblk_erase_cmd;
fast_data_zi bool booting = false;

/*!
 * @brief erase api to erase a physical block
 *
 * @param pblk		physical block to be erased
 * @param idx		index of physical block table
 * @param sync		sync ncl command if true
 *
 * @return		true if issued or erased successfully
 */
bool pblk_erase(pblk_t pblk, u32 idx, bool sync);

init_code void blk_pool_init(u32 cnt, u32 pblk)
{
	_blk_pool.total = cnt;
	_blk_pool.valid = 0;
	_blk_pool.erase_busy = 0;
	_blk_pool.root_blk.pblk = pblk;
	booting = true;
}

init_code u32 get_blk_pool_size(void)
{
	return sizeof(phy_blk_t) * _blk_pool.total;
}

init_code pblk_t get_pblk_root(void)
{
	return _blk_pool.root_blk;
}

ddr_code void pblk_erase_done(struct ncl_cmd_t *ncl_cmd)
{
	u32 idx = (u32) ncl_cmd->caller_priv;

	phy_blks[idx].attr.b.erasing = 0;
	phy_blks[idx].attr.b.used = 0;
	sys_assert(_blk_pool.erase_busy);
	_blk_pool.valid++;
	_blk_pool.erase_busy--;

	if (_blk_pool.erase_busy) {
		u32 i;

		for (i = 0; i < _blk_pool.total - 1; i++) {
			if (phy_blks[i].attr.b.erasing) {
				pblk_erase(phy_blks[i].pblk, i, false);
				return;
			}
		}
		panic("not found");
	} else {
		blk_log_flush_trigger();
	}
}

slow_code bool pblk_erase(pblk_t pblk, u32 idx, bool sync)
{
	bool ret;
	struct ncl_cmd_t *ncl_cmd;
	pda_t *pda_list;
	struct info_param_t *info;

	ncl_cmd = &_pblk_erase_cmd.ncl_cmd;
	pda_list = &_pblk_erase_cmd.pda;
	info = &_pblk_erase_cmd.info;

	pda_list[0] = blk_page_make_pda(pblk.spb_id, pblk.iid, 0);
	info->pb_type = NAL_PB_TYPE_SLC;
	info->status = ficu_err_good;

	ncl_cmd->completion = sync ? NULL : pblk_erase_done;
	ncl_cmd->status = 0;
	ncl_cmd->op_code = NCL_CMD_OP_ERASE;
	ncl_cmd->flags = sync ? NCL_CMD_SYNC_FLAG : 0;
	ncl_cmd->flags |= NCL_CMD_SLC_PB_TYPE_FLAG;

	ncl_cmd->addr_param.common_param.list_len = 1;
	ncl_cmd->addr_param.common_param.pda_list = pda_list;
	ncl_cmd->addr_param.common_param.info_list = info;

	ncl_cmd->user_tag_list = NULL;
	ncl_cmd->caller_priv = (void *) idx;
	ncl_cmd->du_format_no = DU_4K_DEFAULT_MODE;


	ncl_cmd_submit(ncl_cmd);

	ret = true;
	if (sync && ncl_cmd->status != 0)
		ret = false;

	return ret;
}

init_code void blk_pool_clean(void)
{
	u32 i = 0;
	bool ret;

	memset(siga, 0xFF, _blk_pool.total * sizeof(u32));

	do {
	again:
		ret = pblk_erase(phy_blks[i].pblk, i, true);

		if (ret == false) {
			ftl_apl_trace(LOG_ALW, 0x2b9b, "drop pblk %d-%d", phy_blks[i].pblk.spb_id, phy_blks[i].pblk.iid);
			if (i != _blk_pool.total - 1) {
				phy_blks[i].pblk = phy_blks[_blk_pool.total - 1].pblk;
				_blk_pool.total--;
				goto again;
			}
			_blk_pool.total--;
		} else {
			_blk_pool.valid++;
		}
	} while (++i < _blk_pool.total);

	sys_assert(_blk_pool.total == _blk_pool.valid);
	_blk_pool.valid--;	/// remove last block in valid, it's root block
	ftl_apl_trace(LOG_ALW, 0x526e, "all %d pblk clean", _blk_pool.total);
}

fast_code void blk_pool_put_pblk(pblk_t pblk)
{
	u32 i;

	for (i = 0; i < _blk_pool.total; i++) {
		if (pblk.iid == phy_blks[i].pblk.iid && pblk.spb_id == phy_blks[i].pblk.spb_id) {
			phy_blks[i].attr.b.erase_cnt++;
			phy_blks[i].attr.b.erasing = 1;
			if (_blk_pool.erase_busy == 0 && booting == false)
				pblk_erase(phy_blks[i].pblk, i, false);

			_blk_pool.erase_busy++;
			break;
		}
	}

	ftl_apl_trace(LOG_INFO, 0x26a1, "put pblk %x (%d-%d)", pblk.pblk, _blk_pool.valid, _blk_pool.erase_busy);
}

fast_code void blk_pool_get_pblk(pblk_t *pblk)
{
	u32 i;
	u32 er = ~0;
	u32 can = ~0;

	for (i = 0; i < _blk_pool.total - 1; i++) {
		if (phy_blks[i].attr.b.used)
			continue;

		if (phy_blks[i].attr.b.erase_cnt < er) {
			can = i;
			er = phy_blks[i].attr.b.erase_cnt;
		}
	}

	if (can == ~0) {
		pblk->spb_id = INV_SPB_ID;
	} else {
		*pblk = phy_blks[can].pblk;
		phy_blks[can].attr.b.used = 1;
		_blk_pool.valid--;
	}

	ftl_apl_trace(LOG_INFO, 0x9774, "get pblk %x (%d/%d)", pblk->pblk, _blk_pool.valid, _blk_pool.total);
}

fast_code void blk_pool_copy(void *buf)
{
	memcpy(buf, phy_blks, sizeof(phy_blk_t) * _blk_pool.total);
}

fast_code void blk_pool_recovery(phy_blk_t *buf)
{
	memcpy(phy_blks, buf, sizeof(phy_blk_t) * _blk_pool.total);
}

init_code void blk_pool_rescan(void *data, log_meta_t *meta, u32 meta_idx)
{
	u32 i;

	memset(siga, 0xFF, _blk_pool.total * sizeof(u32));
	_blk_pool.valid = 0;
	for (i = 0; i < _blk_pool.total - 1; i++) {
		pda_t pda = blk_page_make_pda(phy_blks[i].pblk.spb_id, phy_blks[i].pblk.iid, 0);
		nal_status_t sts = ncl_read_one_page(pda, data, meta_idx);

		phy_blks[i].attr.all = phy_blks[i].attr.b.erase_cnt;
		if (ficu_du_data_good(sts)) {
			siga[i] = meta->meta0.signature;
			phy_blks[i].attr.b.used = 1;
		} else if (ficu_du_erased(sts)) {
			_blk_pool.valid++;
		} else {
			siga[i] = 0x12345678;
			phy_blks[i].attr.b.erasing = 1;
		}
	}
}

init_code u32 blk_pool_search(pblk_t *pblk, u32 sig, u32 cnt)
{
	u32 i;
	u32 c = 0;

	sys_assert(sig);
	for (i = 0; i < _blk_pool.total - 1; i++) {
		if (siga[i] == sig) {
			sys_assert(c < cnt);

			ftl_apl_trace(LOG_INFO, 0x9d76, "%x: pblk %x", sig, phy_blks[i].pblk.pblk);
			pblk[c++] = phy_blks[i].pblk;
			siga[i] = ~0;

		}
	}
	return c;
}

init_code void blk_pool_dump(void)
{
	u32 i;
	u32 used = 0;
	u32 erasing = 0;
	u32 defected = 0;
	u32 free = 0;

	for (i = 0; i < _blk_pool.total - 1; i++) {
		if (phy_blks[i].attr.b.used)
			used++;
		if (phy_blks[i].attr.b.defect)
			defected++;
		if (phy_blks[i].attr.b.erasing)
			erasing++;

		if (phy_blks[i].attr.b.used == 0 && phy_blks[i].attr.b.defect == 0)
			free++;
	}

	ftl_apl_trace(LOG_ALW, 0x38aa, "used %d erasing %d defect %d", used, erasing, defected);
	ftl_apl_trace(LOG_ALW, 0x1c4a, "valid %d/%d(%d)", _blk_pool.valid, _blk_pool.total, free);
	sys_assert(free == _blk_pool.valid);
}

init_code void blk_pool_drop_all(bool erase)
{
	u32 i;

	for (i = 0; i < _blk_pool.total - 1; i++) {
		if (siga[i] != ~0) {
#if defined(ENABLE_SOUT)
			char sig_ch[5];

			memcpy(sig_ch, &siga[i], 4);
			sig_ch[4] = 0;
			ftl_apl_trace(LOG_ALW, 0xfb67, "drop non used blk %s %x", sig_ch, phy_blks[i].pblk.pblk);
#else
			ftl_apl_trace(LOG_ALW, 0xde1a, "drop non used blk %x %x", siga[i], phy_blks[i].pblk.pblk);
#endif
			siga[i] = ~0;
			if (erase) {
				phy_blks[i].attr.b.erase_cnt++;
				pblk_erase(phy_blks[i].pblk, i, true);
				phy_blks[i].attr.b.erasing = 0;
				phy_blks[i].attr.b.used = 0;
				_blk_pool.valid++;
				continue;
			}

			blk_pool_put_pblk(phy_blks[i].pblk);
		}
	}
}

init_code void blk_pool_drop_useless(void)
{
	u32 i;

	booting = false;

	blk_pool_drop_all(false);

	blk_pool_dump();
	if (_blk_pool.erase_busy) {
		ftl_apl_trace(LOG_ALW, 0x3ba5, "blk poo erase busy %d", _blk_pool.erase_busy);
		for (i = 0; i < _blk_pool.total - 1; i++) {
			if (phy_blks[i].attr.b.erasing) {
				pblk_erase(phy_blks[i].pblk, i, false);
				break;
			}
		}
	}
}

/*! @} */


