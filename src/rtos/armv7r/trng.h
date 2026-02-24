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
#pragma once

#include "types.h"
#include "io.h"
#include "rainier_soc.h"
#include "itrng_reg.h"

#define TRNG_FUNC_MODE 0x01
#define TRNG_TEST_MODE 0x00
#define TRNG_NORMALE_MODE 0x00
#define TRNG_NONCE_MODE 0x02
#define TRNG_MODE_MASK 0x03

/* TRNG Seeding Flow */
#define TRNG_NORMAL_SEEDING 0x01
#define TRNG_NONCE_SEEDING   0x02
/* TRNG request */
enum trng_req {
	TRNG_REQ_SEED		= BIT0,
	TRNG_REQ_SEED_NONCE = BIT1,
	TRNG_REQ_SEED_QUICK = BIT2,
	TRNG_REQ_RNAD_GEN	= BIT5,
};

enum {
	TRNG_SEED0 = 0x00,
	TRNG_SEED1 = 0x01,
	TRNG_SEED2 = 0x02,
	TRNG_SEED3 = 0x03,
	TRNG_SEED4 = 0x04,
	TRNG_SEED5 = 0x05,
	TRNG_SEED6 = 0x06,
	TRNG_SEED7 = 0x07,
};

enum {
	TRNG_RAND0 = 0x00,
	TRNG_RAND1 = 0x01,
	TRNG_RAND2 = 0x02,
	TRNG_RAND3 = 0x03,
	TRNG_RAND4 = 0x04,
	TRNG_RAND5 = 0x05,
	TRNG_RAND6 = 0x06,
	TRNG_RAND7 = 0x07,
};

/*!
 * @brief generte random number
 *
 * This function will generate 256 bit random number each cycle
 *
 * @param buf pointer to a buffer that provided by the caller
 * @param dw_cnt the request random number length, unit is Dword
 *
 * @return none
 */
extern void trng_gen_random(u32 *buf, u32 dw_cnt);

/*!
 * @brief Trng module initialization
 *
 * This function will trigger trng generate new seeds
 *
 * @param none
 *
 * @return none
 */
extern void trng_init(void);