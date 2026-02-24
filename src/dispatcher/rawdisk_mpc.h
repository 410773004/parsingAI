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

#include "mpc.h"
#define MAX_NCL_DATA_LEN			32
#define NR_IDX_META				64
#define READ_DUMMY_IDX				0	// idx meta 0 is for read dummy
#define WRITE_DUMMY_IDX				1	// idx meta 1~63 is for write dummy

#ifdef DDR
	#define RAWDISK_DTAG_TYPE		DTAG_T_DDR
	#define RAWDISK_DTAG_CNT		DDR_DTAG_CNT
#else
	#define RAWDISK_DTAG_TYPE		DTAG_T_SRAM
	#define RAWDISK_DTAG_CNT		SRAM_IN_DTAG_CNT
#endif

/*!
 * @brief Read type of definitions
 */
enum rd_type_t {
	RD_T_STREAMING,		///< host read in streaming mode
	RD_T_DYNAMIC,
};

/*!
 * @brief definition of NCL data structure
 */
typedef struct {
	pda_t pda[MAX_NCL_DATA_LEN];			///< pda list, 1st element to cast, keep the order
	lda_t lda[MAX_NCL_DATA_LEN];			///< lda list
	struct info_param_t info[MAX_NCL_DATA_LEN];	///< NCL required information list
	bm_pl_t bm_pl[MAX_NCL_DATA_LEN];		///< user tag list
	int count;					///< list length

	// sub req
	lda_t slda;
	int ndu;

	int ofst;					///< du offset
	int id;
	u64 hit_bmp;				///< already handled DU bitmap
	u32 hit_cnt;
	u64 l2p_oft_hit_bmp;			///< l2p on-the-fly hit bit map
} ncl_dat_t;

void rawdisk_4k_read(int btag);
