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
/*! \file ftl_flash_geo.h
 * @brief define flash geometry interface in FTL
 *
 * \addtogroup ftl_flash_geo
 *
 * @{
 * Define common geometry parameters which are used in FTL
 */
#pragma once
#include "bf_mgr.h"
#include "ncl_exports.h"
#include "bitops.h"
#include "mpc.h"

#define page2du(page)		((page) << DU_CNT_SHIFT)	///< convert page number to du number
#define du2page(du)		((du) >> DU_CNT_SHIFT)		///< convert du number to pager number


/*! @brief define flash geometry in ftl */
typedef struct _ftl_flash_geo_t {
	u32 disk_size_in_gb;		///< raw disk size in GB
	u32 slc_page_cnt_in_spb;	///< slc page count in one SPB
	u32 xlc_page_cnt_in_spb;	///< native page count in one SPB
	u32 du_cnt_per_slc_spb;		///< du count in one SLC SPB
	u32 du_cnt_per_die;		///< du count in one native die
} ftl_flash_geo_t;

extern ftl_flash_geo_t ftl_flash_geo;	///< ftl flash geometry entity

/*!
 * @brief Get how many SLC pages in a physical block
 *
 * @return	slc page count in a physical block
 */
static inline u32 get_slc_page_per_block(void)
{
	return shr_nand_info.geo.nr_pages / shr_nand_info.bit_per_cell;
}

/*!
 * @brief Get how many pages in a physical block
 *
 * @return	page count in a physical block
 */
static inline u32 get_page_per_block(void)
{
	return shr_nand_info.geo.nr_pages;
}

/*!
 * @brief Get how many SPB in flash
 *
 * @return	spb count in flash
 */
static inline __attribute__((always_inline)) u32 get_total_spb_cnt(void)
{
	return shr_nand_info.geo.nr_blocks;
}

/*!
 * @brief Get how many bits in a flash cell
 *
 * @return	bit count per flush cell
 */
static inline u32 get_nr_bits_per_cell(void)
{
	return shr_nand_info.bit_per_cell;
}

/*!
 * @brief Get how many plane in a lun
 *
 * @return	number of plane per LUN
 */
static inline u32 get_mp(void)
{
	return shr_nand_info.geo.nr_planes;
}

/*!
 * @brief get how many lun per ce
 *
 * @return	number of lun per ce
 */
static inline u32 get_lun(void)
{
	return shr_nand_info.geo.nr_luns;
}

/*!
 * @brief get channel shift bits
 *
 * @return	channel shift
 */
static inline u32 get_ch_shift(void)
{
	return shr_nand_info.pda_ch_shift;
}

/*!
 * @brief get how many channel
 *
 * @return	number of channel
 */
static inline u32 get_ch(void)
{
	return shr_nand_info.geo.nr_channels;
}

/*!
 * @brief get how many ce per channel
 * @return
 */
static inline u32 get_ce(void)
{
	return shr_nand_info.geo.nr_targets;
}

/*!
 * @brief Get flash page count in a native SPB
 *
 * @return	flash page count in a native SPB
 */
static inline u32 get_page_cnt_in_native_spb(void)
{
	return ftl_flash_geo.xlc_page_cnt_in_spb;
}

/*!
 * @brief Get flash page count in a SLC SPB
 *
 * @return	flash page count in a SLC SPB
 */
static inline u32 get_page_cnt_in_slc_spb(void)
{
	return ftl_flash_geo.slc_page_cnt_in_spb;
}

/*!
 * @brief Get DU count in a native SPB
 *
 * @return	DU count in a native SPB
 */
static inline u32 get_du_cnt_in_native_spb(void)
{
	return get_page_cnt_in_native_spb() << DU_CNT_SHIFT;
}

/*!
 * @brief Get DU count in a SLC SPB
 *
 * @return	DU count in a SLC SPB
 */
static inline u32 get_du_cnt_in_slc_spb(void)
{
	return ftl_flash_geo.du_cnt_per_slc_spb;
}

/*!
 * @brief Get nand raw disk size
 *
 * @return	Round down to nearest power of 2 in GB
 */
static inline u32 get_disk_size(void)
{
	//return ftl_flash_geo.disk_size_in_gb;
#ifdef IGNORE_FACTORY_INFO_DISK_SIZE
	//return (ftl_flash_geo.disk_size_in_gb*9375/10000);
	return (ftl_flash_geo.disk_size_in_gb);
#else
	extern u32 Get_FTL_Capacity(void);
	return Get_FTL_Capacity();
#endif
}

/*!
 * @brief get page size of physical page
 *
 * @return	page size
 */
static inline u32 get_page_sz(void)
{
	return shr_nand_info.page_sz;
}

/*!
 * @brief get nand interleave, how many physical blocks in a SPB
 *
 * @return	interleave
 */
static inline u32 get_interleave(void)
{
	return shr_nand_info.interleave;
}

/*!
 * @brief get disk size in GB
 *
 * @return	disk size in GB
 */
static inline u32 get_disk_size_in_gb(void)
{
	return ftl_flash_geo.disk_size_in_gb;
}

/*!
 * @brief get du count per nand die
 *
 * @return	XLC du count per die in
 */
static inline u32 get_du_cnt_per_die(void)
{
	return ftl_flash_geo.du_cnt_per_die;
}

/*!
 * @brief get lun number per spb
 *
 * @return	lun number per spb
 */
static inline u32 get_lun_per_spb(void)
{
	return shr_nand_info.lun_num;
}

/*!
 * @brief initialize FTL flash geometry
 * initialize flash geometry parameter in FTL
 *
 * @return	not used
 */
static inline void ftl_flash_geo_init(void)
{
	u64 weru_sz_in_kb;
	u32 disk_sz_in_gb;
	u32 interleave = get_interleave();

	weru_sz_in_kb = interleave * get_page_per_block() * (get_page_sz() >> 10);
	disk_sz_in_gb = (weru_sz_in_kb * get_total_spb_cnt()) >> 20;
	ftl_flash_geo.disk_size_in_gb = disk_sz_in_gb = 1 << (31 - clz(disk_sz_in_gb));

	ftl_flash_geo.slc_page_cnt_in_spb = interleave * get_slc_page_per_block();

	ftl_flash_geo.xlc_page_cnt_in_spb = interleave * get_page_per_block();

	ftl_flash_geo.du_cnt_per_slc_spb =
			ftl_flash_geo.slc_page_cnt_in_spb << DU_CNT_SHIFT;

	ftl_flash_geo.du_cnt_per_die = (get_mp() * get_page_per_block()) << DU_CNT_SHIFT;

    extern u16 min_good_pl;

    if (ftl_flash_geo.disk_size_in_gb <= 512)   //512G
    {
        min_good_pl = 24;
    }
    else if (ftl_flash_geo.disk_size_in_gb <= 1024) //1T
    {
        min_good_pl = 45;
    }
    else    //2T
    {
        min_good_pl = 88;  
    }
    
#ifndef SKIP_MODE
    min_good_pl = nal_get_interleave();  // (means all dies are good)
#endif
}


/*! @} */
