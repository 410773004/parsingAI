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

/*! \file ns.h
 * @brief namespace management common macro and function
 *
 * \addtogroup
 * \defgroup
 * \ingroup
 * @{
 */
#pragma once

#define NS_SUPPORT	1			///< support NVMe namespace count
#define HOST_NS_ID	(NS_SUPPORT)		///< host namespace id
#define INT_NS_ID	(NS_SUPPORT + 1)	///< + 1 for internal NS id
#define TOTAL_NS	(INT_NS_ID + 1)		///< + 1 for NS 0, NS 0 is default namespace which can not be used

#ifdef NS_MANAGE
#define NVMET_MAX_NS_CNT               (32)	// max supported namespace count
#define NVMET_NR_NS                    (NVMET_MAX_NS_CNT)
#define NS_MANAGE_CTRL_SIZE (384)	// host xfer 384 bytes ctrl data
///< namespace size/capacity granularity size
///< 8GB, customer required, ticket #6081.
//#define NS_SIZE_GRANULARITY	(0x200000000 >> LBA_SIZE_SHIFT)	// lba count
//#define NS_CAP_GRANULARITY	(NS_SIZE_GRANULARITY)
#define NS_SIZE_GRANULARITY1	(0x200000000 >> LBA_SIZE_SHIFT1)	// lba count
#define NS_CAP_GRANULARITY1	(NS_SIZE_GRANULARITY1)
#define NS_SIZE_GRANULARITY1_BITOP1	(24)

#define NS_SIZE_GRANULARITY2	(0x200000000 >> LBA_SIZE_SHIFT2)	// lba count
#define NS_CAP_GRANULARITY2	(NS_SIZE_GRANULARITY2)
#define NS_SIZE_GRANULARITY1_BITOP2	(21)

#else
#if defined(RAMDISK_2NS)
#define NVMET_NR_NS                    2
#else
#define NVMET_NR_NS                    1
#endif
#endif

#ifdef Dynamic_OP_En
#define DYOP_MINLBA                    	  0x1BF2976   // LBAF 4K MINLBA, cap 120G (0x1BF2976)
#endif

/*! */
typedef struct _ns_share_t {
	u64 slba;	///< start lba
	u64 elba;	///< end lba
} ns_share_t;
extern volatile ns_share_t _ns_share[];

#ifdef NS_MANAGE
/*!
 * @brief set namespace start lba
 *
 * @param nsid	namespace id
 * @param slba	start lba
 * @param elba	end lba
 *
 * @return not used
 */
static inline void set_ns_slba_elba(u8 nsid, u64 slba, u64 elba)
{
	_ns_share[nsid - 1].slba = slba;
	_ns_share[nsid - 1].elba = elba;
}

/*!
 * @brief get namespace start lba
 *
 * @param nsid	namespace id
 *
 * @return lba	namespace start lba
 */
static inline u64 get_ns_slba(u8 nsid)
{
	return _ns_share[nsid - 1].slba;
}

/*!
 * @brief convert lba to namespace based lba (host command)
 *
 * @param lba	internal lba (ns + lba)
 *
 * @return lba	ns base lba
 */
static inline u64 convert_lba_to_nlba(u64 lba)
{
	u8 i = 0;
	do {
		if (lba < _ns_share[i].elba) {
			break;
		}
	} while (++i < NVMET_NR_NS);

	sys_assert(i != NVMET_NR_NS);
	//src_inc_trace(LOG_ERR, 0, "lba: 0x%x nsid: %d slba: 0x%x elba: 0x%x\n",
	//	lba, i, _ns_share[i].slba, _ns_share[i].elba);
	return (lba - _ns_share[i].slba);
}
#endif
