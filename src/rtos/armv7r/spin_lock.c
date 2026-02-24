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
/*! \file spin_lock.c
 * @brief implementation of spin_lock
 *
 * \addtogroup rtos
 * \defgroup spin_lock
 * \ingroup rtos
 *
 * {@
 */
#include "sect.h"
#include "types.h"
#include "vic_id.h"
#include "io.h"
#include "rainier_soc.h"

#define SPLK_WDATA_MASK                          0x0000FF00
#define SPLK_WDATA_SHIFT                                  8
#define SPLK_AWID_MASK                           0x000000F0
#define SPLK_AWID_SHIFT                                   4

typedef union {
	u32 all;
	struct {
		u32 rsvd0:4;
		u32 awid:4;
		u32 wdata:8;
		u32 rsvd1:16;
	} b;
} spin_lock_regs_t;

typedef union {
	u32 all;
	struct {
		u32 splk_regs_sts;
	} b;
} spin_lock_sts0_t;

typedef union {
	u32 all;
	struct {
		u32 splk_regs_sts;
	} b;
} spin_lock_sts1_t;

#define SPIN_LOCK_STS_BASE 0xC0204040
#define SPIN_UNLOCK_NORMAL_KEY 0xFF00

typedef enum {
	SPIN_LOCK_REG0 = 0,
	SPIN_LOCK_REG1,
	SPIN_LOCK_REG2,
	SPIN_LOCK_REG3,
	SPIN_LOCK_REG4,
	SPIN_LOCK_REG5,
	SPIN_LOCK_REG6,
	SPIN_LOCK_REG7,
	SPIN_LOCK_REG8,
	SPIN_LOCK_REG9,
	SPIN_LOCK_REG10,
	SPIN_LOCK_REG11,
	SPIN_LOCK_REG12,
	SPIN_LOCK_REG13,
	SPIN_LOCK_REG14,
	SPIN_LOCK_REG15,
	SPIN_LOCK_REG16,
	SPIN_LOCK_REG17,
	SPIN_LOCK_REG18,
	SPIN_LOCK_REG19,
	SPIN_LOCK_REG20,
	SPIN_LOCK_REG21,
	SPIN_LOCK_REG22,
	SPIN_LOCK_REG23,
	SPIN_LOCK_REG24,
	SPIN_LOCK_REG25,
	SPIN_LOCK_REG26,
	SPIN_LOCK_REG27,
	SPIN_LOCK_REG28,
	SPIN_LOCK_REG29,
	SPIN_LOCK_REG30,
	SPIN_LOCK_REG31,
	SPIN_LOCK_REG32,
	SPIN_LOCK_REG33,
	SPIN_LOCK_REG34,
	SPIN_LOCK_REG35,
	SPIN_LOCK_REG36,
	SPIN_LOCK_REG37,
	SPIN_LOCK_REG38,
	SPIN_LOCK_REG39,
	SPIN_LOCK_REG40,
	SPIN_LOCK_REG41,
	SPIN_LOCK_REG42,
	SPIN_LOCK_REG43,
	SPIN_LOCK_REG44,
	SPIN_LOCK_REG45,
	SPIN_LOCK_REG46,
	SPIN_LOCK_REG47,
	SPIN_LOCK_REG48,
	SPIN_LOCK_REG49,
	SPIN_LOCK_REG50,
	SPIN_LOCK_REG51,
	SPIN_LOCK_REG52,
	SPIN_LOCK_REG53,
	SPIN_LOCK_REG54,
	SPIN_LOCK_REG55,
	SPIN_LOCK_REG56,
	SPIN_LOCK_REG57,
	SPIN_LOCK_REG58,
	SPIN_LOCK_REG59,
	SPIN_LOCK_REG60,
	SPIN_LOCK_REG61,
	SPIN_LOCK_REG62,
	SPIN_LOCK_REG63,
	SPIN_LOCK_REG64,
	SPIN_LOCK_REG65,
	SPIN_LOCK_REG66,
	SPIN_LOCK_REG67,
	SPIN_LOCK_REG68,
	SPIN_LOCK_REG69,
	SPIN_LOCK_REG70,
	SPIN_LOCK_REG71,
	SPIN_LOCK_REG72,
	SPIN_LOCK_REG73,
	SPIN_LOCK_REG74,
	SPIN_LOCK_REG75,
	SPIN_LOCK_REG76,
	SPIN_LOCK_REG77,
	SPIN_LOCK_REG78,
	SPIN_LOCK_REG79,
	SPIN_LOCK_REG80,
	SPIN_LOCK_REG81,
	SPIN_LOCK_REG82,
	SPIN_LOCK_REG83,
	SPIN_LOCK_REG84,
	SPIN_LOCK_REG85,
	SPIN_LOCK_REG86,
	SPIN_LOCK_REG87,
	SPIN_LOCK_REG88,
	SPIN_LOCK_REG89,
	SPIN_LOCK_REG90,
	SPIN_LOCK_REG91,
	SPIN_LOCK_REG92,
	SPIN_LOCK_REG93,
	SPIN_LOCK_REG94,
	SPIN_LOCK_REG95,
	SPIN_LOCK_REG96,
	SPIN_LOCK_REG97,
	SPIN_LOCK_REG98,
	SPIN_LOCK_REG99,
	SPIN_LOCK_REG100,
	SPIN_LOCK_REG101,
	SPIN_LOCK_REG102,
	SPIN_LOCK_REG103,
	SPIN_LOCK_REG104,
	SPIN_LOCK_REG105,
	SPIN_LOCK_REG106,
	SPIN_LOCK_REG107,
	SPIN_LOCK_REG108,
	SPIN_LOCK_REG109,
	SPIN_LOCK_REG110,
	SPIN_LOCK_REG111,
	SPIN_LOCK_REG112,
	SPIN_LOCK_REG113,
	SPIN_LOCK_REG114,
	SPIN_LOCK_REG115,
	SPIN_LOCK_REG116,
	SPIN_LOCK_REG117,
	SPIN_LOCK_REG118,
	SPIN_LOCK_REG119,
	SPIN_LOCK_REG120,
	SPIN_LOCK_REG121,
	SPIN_LOCK_REG122,
	SPIN_LOCK_REG123,
	SPIN_LOCK_REG124,
	SPIN_LOCK_REG125,
	SPIN_LOCK_REG126,
	SPIN_LOCK_REG127,
	SPIN_LOCK_REG128,
	SPIN_LOCK_REG129,
	SPIN_LOCK_REG130,
	SPIN_LOCK_REG131,
	SPIN_LOCK_REG132,
	SPIN_LOCK_REG133,
	SPIN_LOCK_REG134,
	SPIN_LOCK_REG135,
	SPIN_LOCK_REG136,
	SPIN_LOCK_REG137,
	SPIN_LOCK_REG138,
	SPIN_LOCK_REG139,
	SPIN_LOCK_REG140,
	SPIN_LOCK_REG141,
	SPIN_LOCK_REG142,
	SPIN_LOCK_REG143,
	SPIN_LOCK_REG144,
	SPIN_LOCK_REG145,
	SPIN_LOCK_REG146,
	SPIN_LOCK_REG147,
	SPIN_LOCK_REG148,
	SPIN_LOCK_REG149,
	SPIN_LOCK_REG150,
	SPIN_LOCK_REG151,
	SPIN_LOCK_REG152,
	SPIN_LOCK_REG153,
	SPIN_LOCK_REG154,
	SPIN_LOCK_REG155,
	SPIN_LOCK_REG156,
	SPIN_LOCK_REG157,
	SPIN_LOCK_REG158,
	SPIN_LOCK_REG159,
	SPIN_LOCK_REG160,
	SPIN_LOCK_REG161,
	SPIN_LOCK_REG162,
	SPIN_LOCK_REG163,
	SPIN_LOCK_REG164,
	SPIN_LOCK_REG165,
	SPIN_LOCK_REG166,
	SPIN_LOCK_REG167,
	SPIN_LOCK_REG168,
	SPIN_LOCK_REG169,
	SPIN_LOCK_REG170,
	SPIN_LOCK_REG171,
	SPIN_LOCK_REG172,
	SPIN_LOCK_REG173,
	SPIN_LOCK_REG174,
	SPIN_LOCK_REG175,
	SPIN_LOCK_REG176,
	SPIN_LOCK_REG177,
	SPIN_LOCK_REG178,
	SPIN_LOCK_REG179,
	SPIN_LOCK_REG180,
	SPIN_LOCK_REG181,
	SPIN_LOCK_REG182,
	SPIN_LOCK_REG183,
	SPIN_LOCK_REG184,
	SPIN_LOCK_REG185,
	SPIN_LOCK_REG186,
	SPIN_LOCK_REG187,
	SPIN_LOCK_REG188,
	SPIN_LOCK_REG189,
	SPIN_LOCK_REG190,
	SPIN_LOCK_REG191,
	SPIN_LOCK_REG192,
	SPIN_LOCK_REG193,
	SPIN_LOCK_REG194,
	SPIN_LOCK_REG195,
	SPIN_LOCK_REG196,
	SPIN_LOCK_REG197,
	SPIN_LOCK_REG198,
	SPIN_LOCK_REG199,
	SPIN_LOCK_REG200,
	SPIN_LOCK_REG201,
	SPIN_LOCK_REG202,
	SPIN_LOCK_REG203,
	SPIN_LOCK_REG204,
	SPIN_LOCK_REG205,
	SPIN_LOCK_REG206,
	SPIN_LOCK_REG207,
	SPIN_LOCK_REG208,
	SPIN_LOCK_REG209,
	SPIN_LOCK_REG210,
	SPIN_LOCK_REG211,
	SPIN_LOCK_REG212,
	SPIN_LOCK_REG213,
	SPIN_LOCK_REG214,
	SPIN_LOCK_REG215,
	SPIN_LOCK_REG216,
	SPIN_LOCK_REG217,
	SPIN_LOCK_REG218,
	SPIN_LOCK_REG219,
	SPIN_LOCK_REG220,
	SPIN_LOCK_REG221,
	SPIN_LOCK_REG222,
	SPIN_LOCK_REG223,
	SPIN_LOCK_REG224,
	SPIN_LOCK_REG225,
	SPIN_LOCK_REG226,
	SPIN_LOCK_REG227,
	SPIN_LOCK_REG228,
	SPIN_LOCK_REG229,
	SPIN_LOCK_REG230,
	SPIN_LOCK_REG231,
	SPIN_LOCK_REG232,
	SPIN_LOCK_REG233,
	SPIN_LOCK_REG234,
	SPIN_LOCK_REG235,
	SPIN_LOCK_REG236,
	SPIN_LOCK_REG237,
	SPIN_LOCK_REG238,
	SPIN_LOCK_REG239,
	SPIN_LOCK_REG240,
	SPIN_LOCK_REG241,
	SPIN_LOCK_REG242,
	SPIN_LOCK_REG243,
	SPIN_LOCK_REG244,
	SPIN_LOCK_REG245,
	SPIN_LOCK_REG246,
	SPIN_LOCK_REG247,
	SPIN_LOCK_REG248,
	SPIN_LOCK_REG249,
	SPIN_LOCK_REG250,
	SPIN_LOCK_REG251,
	SPIN_LOCK_REG252,
	SPIN_LOCK_REG253,
	SPIN_LOCK_REG254,
	SPIN_LOCK_REG255,
	SPIN_LOCK_REG_MAX,
} spin_lock_idx_t;


fast_data u8 awid_map[] = {
	[1] = 0,
	[2] = 1,
	[3] = 2,
	[4] = 3,
	[5] = 6,
	[6] = 7,
	[7] = 8,
	[8] = 9,
};

static inline void splk_writel(vu32 val, spin_lock_idx_t splk_idx)
{
	writel(val, (void *) (SPIN_LOCK_BASE + splk_idx * 4));
}

static inline vu32 splk_readl(spin_lock_idx_t splk_idx)
{
	return readl((const void *) (SPIN_LOCK_BASE + splk_idx * 4));
}

/*!
 * @brief spin lock index check
 * the spin lock index base on 0, maxmimum is 255
 *
 * @param spin lock index
 *
 * @return true means the index cross the range
 */
static fast_code bool splk_idx_chk(spin_lock_idx_t splk_idx)
{
	return splk_idx < 0 || splk_idx > SPIN_LOCK_REG255;
}

/*!
 * @brief spin lock resource status
 *
 * @param splk_idx the index of the spin lock registers(0-255)
 *
 * @return true means this register has been taken by a CPU
 */
fast_code bool spin_lock_regs_status(spin_lock_idx_t splk_idx)
{
	if (splk_idx_chk(splk_idx))
		return true;

	u32 splks_sts = readl((void *)(SPIN_LOCK_STS_BASE + (splk_idx/32) *4));
	return !(splks_sts & (1 << (splk_idx & 0x1F)));
}

fast_code bool spin_lock_take(u32 splk_idx, u32 val, bool wait)
{
	spin_lock_regs_t splk;
	if (splk_idx_chk(splk_idx))
		return false;
	if (spin_lock_regs_status(splk_idx) && !wait)
		return false;
	do {
		splk_writel(val << SPLK_WDATA_SHIFT, splk_idx);
		splk.all = splk_readl(splk_idx);
	} while((val) != splk.b.wdata || awid_map[CPU_ID] != splk.b.awid);
	return true;
}

fast_code bool spin_lock_release(u32 splk_idx)
{
	if (splk_idx_chk(splk_idx))
		return false;
	splk_writel(SPIN_UNLOCK_NORMAL_KEY, splk_idx);
	return splk_readl(splk_idx) == 0xFFFF;
}

init_code void spin_lock_init(void)
{
	u32 i;
	u32 super_key = 0xFFFF;
	for (i = 0; i < SPIN_LOCK_REG_MAX; i++)
		splk_writel(super_key, i);
}

/*! @} */
