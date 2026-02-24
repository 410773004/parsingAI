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
/*! \file log.h
 * @brief define log data structure for system log and spb log
 *
 * \addtogroup log
 * @{
 *
 * Log is a special area to use physical block to save critical data, it support
 * dual copy or not
 */
#pragma once
#include "ftl_remap.h"

#define PAGE_META_SZ	(sizeof(struct du_meta_fmt) << DU_CNT_SHIFT)	///< page meta size

/*! @brief meta field of du0 in log page */
typedef struct _log_du0_meta_t {
	u32 seed;		///< seed
	u32 signature;		///< signature of log
	u32 flush_id;		///< flush id
	pblk_t backup;		///< backup blk

	u32 rsvd[4];
} log_du0_meta_t;

/*! @brief meta field of du1 in log page */
typedef struct _log_du1_meta_t {
	u32 seed;		///< seed
	u8 flush_cnt;		///< flush page count in one flush
	u8 flush_idx;		///< flush index in one flush
	u8 log_page_id;		///< log page id
	u8 _rsvd;		///< reserved
	u32 payload[6];		///< payload of derived log data structure
} log_du1_meta_t;

/*! @brief meta field of du2 in log page */
typedef struct _log_du2_meta_t {
	u32 seed;		///< seed
	u32 payload[7];		///< payload of derived log data structure
} log_du2_meta_t;

/*! @brief meta field of du3 in log page */
typedef struct _log_du3_meta_t {
	u32 seed;		///< seed
	u32 payload[7];		///< payload of derived log data structure
} log_du3_meta_t;

/*! @brief du meta of log page */
typedef struct _log_meta_t {
	log_du0_meta_t meta0;	///< meta of du0
	log_du1_meta_t meta1;	///< meta of du1
#if DU_CNT_PER_PAGE == 4
	log_du2_meta_t meta2;	///< meta of du2
	log_du3_meta_t meta3;	///< meta of du3
#endif
} log_meta_t;

#define LOG_F_ATOMIC		0x01	///< log flag: atomic enabled
#define LOG_F_BACKUP		0x02	///< log flag: if dual copy
#define LOG_F_SWITCHED		0x04	///< log flag: if block was switched
#define LOG_F_FLUSH_ALL		0x10	///< log flag: force flush all
#define LOG_F_SINGLE		0x20	///< log flag: only using one block, no PingPong

/*! @brief log context definition */
typedef struct _log_t {
	pblk_t cur[2];
	pblk_t next[2];

	u8 log_page_cnt;	///< total log page count

	u32 nr_page_per_blk;
	u32 next_page;

	u32 flush_id;

	u32 flags;
} log_t;

struct _log_recon_t;

/*! @brief callback function to read l2p table of log */
typedef pda_t (*log_l2pt_read_t)(struct _log_recon_t*, log_meta_t*, pda_t);

/*!
 * @brief log reconstruction parameter
 */
typedef struct _log_recon_t {
	pda_t *l2pt;			///< l2p table to be reconstructed
	void *data;			///< resource for page user data
	void *meta;			///< resource for page meta data
	u32 meta_idx;			///< meta index of meta buffer
	log_l2pt_read_t log_l2pt_read;	///< read the latest log l2pt
	u32 sig;			///< signature of log
	bool read_error;

	pblk_t pblk[16];		///< support 16 physical blocks in log reconstruction
	u32 pblk_cnt;			///< physical block insert count
} log_recon_t;

#define LOG_INIT(log)	{				\
	.cur[0].spb_id = INV_SPB_ID,	\
	.cur[1].spb_id = INV_SPB_ID,	\
	.next[0].spb_id = INV_SPB_ID,	\
	.next[1].spb_id = INV_SPB_ID,	\
	.flush_id = 0,			\
	.log_page_cnt = 0, .flags = 0,	\
	.nr_page_per_blk = 0,		\
	.next_page = 0			\
}		///< macro to initialize log data structure

/*!
 * @brief Initialize log context
 *
 * @param log			log context
 * @param log_pg_cnt		number of log page
 * @param nr_pg_per_blk		number of page in log block
 * @param flags			initialized flags
 *
 * @return			none
 */
void log_init(log_t *log, u32 log_pg_cnt, u32 nr_pg_per_blk, u32 flags);

/*!
 * @brief Initialize log reconstruction parameter
 *
 * @param log_recon		log reconstruction parameter
 * @param l2pt			L2P table
 * @param log_l2pt_read		callback to restore a old L2P table from latest page
 * @param sig			signature of log
 *
 * @return			none
 */
void log_recon_init(log_recon_t *log_recon, pda_t *l2pt,
		log_l2pt_read_t log_l2pt_read, u32 sig);

/*!
 * @brief Release resource of log reconstruction parameter
 *
 * @param log_recon	log reconstruction parameter
 *
 * @return		none
 */
void log_recon_rel(log_recon_t *log_recon);

/*!
 * @brief Reconstruct log
 *
 * @param log		log context
 * @param log_recon	log reconstruction parameter
 *
 * @return		Return false if L2P table was not fully reconstructed
 */
bool log_reconstruction(log_t *log, log_recon_t *log_recon);

/*!
 * @brief Make a PDA for a page in a block of spb_id
 *
 * @param spb_id	SPB ID
 * @param blk		block ID
 * @param page		page number in block
 *
 * @return		the PDA
 */
static inline fast_code pda_t blk_page_make_pda(spb_id_t spb_id, u32 blk, u32 page)
{
	u32 offset;

	offset = (blk + page * nal_get_interleave()) << DU_CNT_SHIFT;
	return nal_make_pda(spb_id, offset);
}

/*!
 * @brief get block and page from a PDA
 *
 * @param pda		pda
 * @param page		return page number in block
 *
 * @return		block number
 */
static inline fast_code u32 pda_to_blk_page(pda_t pda, u32 *page)
{
	u32 offset = nal_pda_offset_in_spb(pda);

	offset >>= DU_CNT_SHIFT;
	*page = offset / nal_get_interleave();
	return offset & (nal_get_interleave() - 1);
}

/*!
 * @brief Retrieve block and page id from PDA
 *
 * @param pda	target PDA
 * @param blk	return block id of PDA
 * @param page	return block page id of PDA
 *
 * @return	Return SPB ID of PDA
 */
static inline spb_id_t pda_make_blk_page(pda_t pda, u32 *blk, u32 *page)
{
	u32 offset;

	offset = nal_pda_offset_in_spb(pda);
	offset >>= DU_CNT_SHIFT;

	*blk = offset & (nal_get_interleave() - 1);
	*page = offset / nal_get_interleave();

	return nal_pda_get_block_id(pda);
}

/*!
 * @brief from log_recon, decide latest block and insert them back to log
 *
 * @param log		log
 * @param log_recon	log reconstruction data structure
 *
 * @return		always return true
 */
bool log_blk_decide(log_t *log, log_recon_t *log_recon);

/*!
 * @brief recycle all old block in log_recon
 *
 * @param log		log
 * @param log_recon	log reconstruction data structure
 *
 * @return		not used
 */
void log_blk_recycled(log_t *log, log_recon_t *log_recon);
/*! @} */
