#include "fcprecomp.h"
#include "ftl_export.h"
#include "dpe_export.h"
#include "rainier_soc.h"
#include "log.h"
#include "srb.h"
#include "dma.h"
#include "dtag.h"
#include "stdlib.h"

/*! \cond PRIVATE */
#define __FILEID__ ftlcore
#include "trace.h"
/*! \endcond */

#define MAX_SWAP_PAGE		20
#define NAND_SWAP_PAGE_SZ	16384	///< force 16K, for 8K nand page, will use two pages
#define SRAM_DATA_START_ADDR 0x20040000
#define SRAM_DATA_SIZE 0x80000

share_data volatile pblk_t fc_pmu_pblk;		///< pmu swap save space use nand block
share_data volatile u16 cur_page;			///< pmu swap use nand block current write page
share_data volatile u16 need_page;		///< pmu swap next save need nand page
share_data volatile u32 cpu1_btcm_free_start;	///< share cpu1 btcm heap address for pmu swap

static fast_data_ni swap_page_t _swap_pages[MAX_SWAP_PAGE];
static fast_data_ni swap_mem_t _swap_mem[2];
static fast_data u32 _swap_page_ttl_sz = 0;
static fast_data u32 _swap_mem_free = 0;
static fast_data u8 _swap_page_idx = 0;

extern u16 ncl_pmu_page_access(pda_t pda, dtag_t dtag, bool write);

ddr_code void pmu_swap_copy(void *src, void *dst, u32 size)
{
	if ((((u32)src) & 0x1F) || (((u32)dst) & 0x1F)) {
		memcpy(dst, src, size);
		return;
	}

	if (is_btcm(src) && is_btcm(dst)) {
		memcpy(dst, src, size);
		return;
	}

	sync_dpe_copy((u32)src, (u32)dst, size);
}

ddr_code void *pmu_swap_malloc_copy_mem(void *ptr, u32 size)
{
	void *mem = NULL;
	u32 malloc_szie = size;

	if ((malloc_szie & 0x1f) != 0)
		malloc_szie = malloc_szie - (malloc_szie & 0x1f) + 0x20;

	if (_swap_mem_free < malloc_szie)
		return mem;

	if (_swap_mem_free <= _swap_mem[1].size) {
		mem = _swap_mem[1].loc + _swap_mem[1].size - _swap_mem_free;
		pmu_swap_copy(ptr, mem, size);
	} else {
		u32 cur_size;

		cur_size  = _swap_mem[0].size + _swap_mem[1].size - _swap_mem_free;
		mem = _swap_mem[0].loc + cur_size;
		cur_size = _swap_mem[0].size - cur_size;
		cur_size = min(size, cur_size);
		pmu_swap_copy(ptr, mem, cur_size);
		if (cur_size < size)
			pmu_swap_copy(ptr, _swap_mem[1].loc, size - cur_size);
	}

	_swap_mem_free -= malloc_szie;
	return mem;
}

ddr_code void pmu_swap_next_pda(pda_t *pda)
{
	if (fc_pmu_pblk.spb_id == INV_SPB_ID || (cur_page >= shr_nand_info.geo.nr_pages / XLC)) {
		*pda = INV_PDA;
		return;
	}

	if (pda == NULL)
		cur_page++;
	else
		*pda = blk_page_make_pda(fc_pmu_pblk.spb_id, fc_pmu_pblk.iid, cur_page);
}

ddr_code void pmu_swap_file_register(void *loc, u32 size)
{
	sys_assert(_swap_page_idx < MAX_SWAP_PAGE);

	ftl_core_trace(LOG_INFO, 0x28f7, "pmu swap %d - %x %x", _swap_page_idx,
		loc, size);

	_swap_pages[_swap_page_idx].loc = loc;
	_swap_pages[_swap_page_idx].size = size;
	_swap_page_ttl_sz += size;
	_swap_page_idx++;

	return;
}

ddr_code void pmu_swap_file_update(u8 idx, void *loc, u32 size)
{
	sys_assert(idx < _swap_page_idx);
	_swap_page_ttl_sz -= _swap_pages[idx].size;

	_swap_pages[idx].loc = loc;
	_swap_pages[idx].size = size;
	_swap_page_ttl_sz += _swap_pages[idx].size;
}

ddr_code static void pmu_swap_file_save_abort(u32 abort_id)
{
	u32 i;

	for (i = 0; i < abort_id; i++) {
		if (_swap_pages[i].attr.b.in_nand == 0) {
			_swap_pages[i].swap_buf = NULL;
		}
	}
	_swap_mem_free = _swap_mem[0].size + _swap_mem[1].size;
	_swap_page_ttl_sz = 0;
	_swap_page_idx = 0;
}

slow_code bool pmu_swap_file_save(void)
{
	u8 i;
	u32 copied = 0;
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, NAND_SWAP_PAGE_SZ / DTAG_SZE);
	void *nand_page;
	pda_t pda;

	pmu_swap_next_pda(&pda);

	if (pda == INV_PDA)
		return false;

	if (dtag.dtag == DTAG_INV)
		return false;

	nand_page = dtag2mem(dtag);

	for (i = 0; i < _swap_page_idx; i++) {
		u32 size = _swap_pages[i].size;
		void *src = _swap_pages[i].loc;
		void *swap_buf = pmu_swap_malloc_copy_mem(src, size);

		_swap_pages[i].attr.all = 0;
		if (swap_buf) {
			_swap_pages[i].swap_buf = swap_buf;
		} else {
			u32 c;

			if ((copied & 0x1f) != 0)
				copied = copied - (copied & 0x1f) + 0x20;
			_swap_pages[i].attr.b.in_nand = 1;
			_swap_pages[i].pda = pda;
			_swap_pages[i].attr.b.offset = copied;
			while (size) {
				c = min(NAND_SWAP_PAGE_SZ - copied, size);
				pmu_swap_copy(src, ptr_inc(nand_page, copied), c);
				size -= c;
				copied += c;

				if (copied == NAND_SWAP_PAGE_SZ) {
					u16 ret = ncl_pmu_page_access(pda, dtag, true);

					if (ret != 0) {
						dtag_cont_put(DTAG_T_SRAM, dtag, NAND_SWAP_PAGE_SZ / DTAG_SZE);
						pmu_swap_file_save_abort(i);
						return false;
					}
					copied = 0;
					pda++;
					pmu_swap_next_pda(NULL);
				}
			}
		}
	}

	dtag_cont_put(DTAG_T_SRAM, dtag, NAND_SWAP_PAGE_SZ / DTAG_SZE);
	return true;
}

ddr_code void pmu_swap_mem_restore(bool resume)
{
	u8 i;

	for (i = 0; i < _swap_page_idx; i++) {
		u32 size = _swap_pages[i].size;
		void *dst = _swap_pages[i].loc;

		if (_swap_pages[i].attr.b.in_nand == 0) {
			if (resume == true)
				pmu_swap_copy(_swap_pages[i].swap_buf, dst, size);

			_swap_pages[i].swap_buf = NULL;
		}
	}
}

ddr_code void pmu_swap_file_restore(bool resume)
{
	u8 i;
	u32 copied = NAND_SWAP_PAGE_SZ;
	log_level_t old = log_level_chg(LOG_ALW);
	dtag_t dtag = dtag_cont_get(DTAG_T_SRAM, NAND_SWAP_PAGE_SZ / DTAG_SZE);
	void *nand_page;
	pda_t pda = 0;

	sys_assert(dtag.dtag != DTAG_INV);

	nand_page = dtag2mem(dtag);

	for (i = 0; i < _swap_page_idx; i++) {
		u32 size = _swap_pages[i].size;
		void *dst = _swap_pages[i].loc;

		if (_swap_pages[i].attr.b.in_nand != 0) {
			u32 c;
			u32 off = 0;

			if (resume == false)
				continue;

			if (copied == NAND_SWAP_PAGE_SZ) {
				pda_t pda = _swap_pages[i].pda;
				u16 ret = ncl_pmu_page_access(pda, dtag, false);

				sys_assert(ret == 0);
				copied = 0;
				pda++;
			}

			sys_assert(copied == _swap_pages[i].attr.b.offset);
			while (size) {
				c = min(NAND_SWAP_PAGE_SZ - copied, size);
				pmu_swap_copy(ptr_inc(nand_page, copied), ptr_inc(dst, off), c);
				size -= c;
				copied += c;
				off += c;

				if (size && copied == NAND_SWAP_PAGE_SZ) {
					// issue ncl read
					u16 ret = ncl_pmu_page_access(pda, dtag, false);

					sys_assert(ret == 0);
					copied = 0;
					pda++;
				}
			}
		}
	}
	_swap_mem_free = _swap_mem[0].size + _swap_mem[1].size;
	_swap_page_ttl_sz = 0;
	_swap_page_idx = 0;

	log_level_chg(old);
}

ddr_code void pmu_swap_register_other_cpu_memory(void)
{
	need_page = NR_PAGES_SLICE(_swap_page_ttl_sz);
	_swap_mem[1].loc = (void *)cpu1_btcm_free_start - BTCM_BASE + CPU1_BTCM_BASE;
	if (((u32)_swap_mem[1].loc & 0x1f) != 0)
		_swap_mem[1].loc = _swap_mem[1].loc  - ((u32)_swap_mem[1].loc & 0x1f) +0x20;
	_swap_mem[1].size = BTCM_SIZE + CPU1_BTCM_BASE - (u32)_swap_mem[1].loc;
	_swap_mem_free += _swap_mem[1].size;
	ftl_core_trace(LOG_INFO, 0x9cd9, "pmu swap mem: 0x%x -- 0x%x size: 0x%x", (u32)_swap_mem[0].loc,
		(u32)_swap_mem[1].loc, _swap_mem_free);
}

init_code void pmu_swap_init(void)
{
	u32 save_size;

	save_size = (u32)&__sram_free_start - (u32)&__ucmd_start;
	pmu_swap_file_register((void *)&__ucmd_start, save_size);
	_swap_mem[0].loc = (void *)&__btcm_data_ni_start - BTCM_BASE + btcm_dma_base;
	if (((u32)_swap_mem[0].loc & 0x1f) != 0)
		_swap_mem[0].loc = _swap_mem[0].loc  - ((u32)_swap_mem[0].loc & 0x1f) +0x20;
	_swap_mem[0].size = BTCM_SIZE + btcm_dma_base - (u32)_swap_mem[0].loc;
	_swap_mem_free += _swap_mem[0].size;
}

