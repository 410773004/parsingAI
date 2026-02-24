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

#ifndef _FICU_H_
#define _FICU_H_

#include "finstr.h"
#include "ard.h"
#include "pmu.h"
#include "../btn/inc/btn.h"
#include "GList.h"
#if HAVE_HW_CPDA
// "Interleave layouts in terms of CH, CE, LUN and plane is fixed to 5'd15 in Tacoma"-from CPDA appnote
#define INTERLEAVE_TYPE	15 /*CE first, then channel, LUN, plane */
#else
#define INTERLEAVE_TYPE	2 /*Plane first, then channel, CE, LUN */
#endif

#if NCL_USE_DTCM_SQ
#define DTCM_SQ_FIFO_COUNT	256
#define DTCM_HSQ_FIFO_COUNT	32
#define DTCM_USQ_FIFO_COUNT	32
#endif
#define FSPM_CQ_FIFO_COUNT	256
#define FSPM_SQ_FIFO_COUNT	1024
#define FSPM_HSQ_FIFO_COUNT	0
#define FSPM_USQ_FIFO_COUNT	0
#define FSPM_DU_LOG_COUNT 0
#define FSPM_PARAM_FIFO_COUNT	32
#define FSPM_LUT_SIZE		((40 << 10) / sizeof(u32))	/* Bo Fu said LUT need at least 320 * 128 Byte = 40 KB in ticket 3690*/
#define FSPM_META_FIFO_COUNT	256
#define FSPM_DTAG_INDEX_COUNT	256
#define FSPM_VSC_TEMPLATE_COUNT 32


/*!
 * @brief NCB Completion Queue Format
 */
struct ficu_cq_entry {
	u32 fcmd_id		: 15;
	u32 rsvd		: 14;
	u32 ard			: 1;
	u32 sq_grp		: 1;
	u32 phase		: 1;
};

typedef struct {
	hdr_reg_t reg;				///< common queue header register
	u32 ptr __attribute__ ((aligned(8)));	///< asic update pointer
} dtcm_cq_t;//bcl_que_t;

struct dtcm_cq_entry {
	u32 fcmd_id 	: 12;
	u32 lst_du_offset	: 12;
	u32 rsvd1		: 5;
	u32 ard 				: 1;
	u32 rsvd2		: 2;
};

struct param_fifo_format {
	u32 finst_id		: 16;
	u32 fcmd_id		: 16;
	u32 param[2];
};

struct du_log_format {
	// DW0
	u32 du_idx		: 4;
	u32 mp_idx		: 4;
	u32 finst_id		: 12;
	u32 fcmd_id		: 12;

	// DW1
	u32 mdec_cw_found	: 4;
	u32 du_mdec_iter_cnt	: 8;// 4-11
	u32 du_pdec_iter_cnt	: 10;//12-21
	u32 du_ecnt_1to0_lo	: 10;//22-31
	u32 du_ecnt_1to0_hi	: 2;//32-33
	u32 du_ecnt_0to1	: 12;//34-45
	u32 du_erase		: 1;//46
	u32 du_ard_trig_cnt	: 3;//47-49
	u32 du_mcrc_fail	: 1;//50
	u32 cw_overlmt		: 4;//51-54
	u32 cw_unclean		: 4;//55-58
	u32 cw_found		: 4;//59-62
	u32 rsvd		: 1;
};

/*! FSPM usage structure */
struct fspm_usage_t {
	struct {
		struct ficu_cq_entry cq_fifo[FSPM_CQ_FIFO_COUNT];
		struct ficu_cq_entry cq_fifo_end[0];
	};
	struct {
		struct finstr_format sq0_fifo[FSPM_SQ_FIFO_COUNT];
		struct finstr_format sq0_fifo_end[0];
	};
	struct {
		struct finstr_format sq1_fifo[FSPM_SQ_FIFO_COUNT];
		struct finstr_format sq1_fifo_end[0];
	};
	struct {// High SQ0
		struct finstr_format hsq0_fifo[FSPM_HSQ_FIFO_COUNT];
		struct finstr_format hsq0_fifo_end[0];
	};
	struct {// High SQ1
		struct finstr_format hsq1_fifo[FSPM_HSQ_FIFO_COUNT];
		struct finstr_format hsq1_fifo_end[0];
	};
	struct {// Urgent SQ0
		struct finstr_format usq0_fifo[FSPM_USQ_FIFO_COUNT];
		struct finstr_format usq0_fifo_end[0];
	};
	struct {// Urgent SQ1
		struct finstr_format usq1_fifo[FSPM_USQ_FIFO_COUNT];
		struct finstr_format usq1_fifo_end[0];
	};
	struct {
		struct param_fifo_format param_fifo[FSPM_PARAM_FIFO_COUNT];
		struct param_fifo_format param_fifo_end[0];
	};
	struct {
		struct ncb_vsc_format vsc_fmt_ba[0];
		struct ncb_vsc_format vsc_fmt[FSPM_VSC_TEMPLATE_COUNT];
		struct ncb_vsc_format vsc_fmt_end[0];
	};
	struct ncb_vsc_format vsc_template[FSPM_VSC_TEMPLATE_COUNT];
	struct finstr_ard_format ard_template[FSPM_ARD_COUNT];
	struct {
		u32 lut[FSPM_LUT_SIZE];
		u32 lut_end[0];
	};
	struct {
		struct du_log_format du_logs[FSPM_DU_LOG_COUNT];
		struct du_log_format du_logs_end[0];
	};
} __attribute__((aligned(8)));

/*! FINST FIFO structure */
struct finst_fifo_t {
	struct finstr_format* fifo_ptr;
	struct finstr_format* fifo_start;
	struct finstr_format* fifo_end;
	struct finstr_format* fifo_rptr;
};

#if NCL_USE_DTCM_SQ
struct dtcm_sq_t {
	struct finstr_format* base_addr;
	u32 max_sz;
	u32 wr_ptr;
	u32 *rd_ptr;
	u32 ptrs_reg;
};
#endif

extern struct fspm_usage_t *fspm_usage_ptr;

#define CQ_START	(SPRM_BASE_ADJ)
#define CQ_END		(CQ_START + FSPM_CQ_FIFO_COUNT * sizeof(struct ficu_cq_entry))
#define SQ_START	(CQ_END)
#define SQ_END		(SQ_START + FSPM_SQ_FIFO_COUNT * sizeof(struct finstr_format))

/*!
 * @brief Get FDA by dtag pointer
 *
 * @param[in] du_dtag_ptr		DU datg pointer
 *
 * @return FDA dtag pointer
 */
struct fda_dtag_format *ficu_get_addr_dtag_ptr(u32 du_dtag_ptr);
/*!
 * @brief Enable FICU mode
 *
 * @return Not used
 */
void ficu_mode_enable(void);
/*!
 * @brief Disable FICU mode
 *
 * @return Not used
 */
void ficu_mode_disable(void);
/*!
 * @brief Switch to the PDA mode
 *
 * @return Not used
 */
void ficu_switch_pda_mode(void);
/*!
 * @brief Switch to the CPDA mode
 *
 * @return Not used
 */
void ficu_switch_cpda_mode(void);
/*!
 * @brief Remapping the channel location
 *
 * @param[in] remap	remap value
 *
 * @return Not used
 */
void ficu_channel_remap(u32 remap);
/*!
 * @brief Get the error PDA
 *
 * @return value(PDA)
 */
pda_t eccu_error_pda_get(void);
void ncl_hw_poll(u32 wrptr);
/*!
 * @brief FCMD DONE or ERR interrupt handling
 *
 * @return Not used
 */
void ficu_isr(void);
/*!
 * @brief Initialization FICU module
 *
 * @return Not used
 */

void ficu_init(void);
/*!
 * @brief Get specific FIFO parameter
 *
 * @return value(FIFO pointer)
 */
struct param_fifo_format* get_param_fifo(void);
/*!
 * @brief Get FDA by dtag pointer
 *
 * @param[in] du_dtag_ptr		DU datg pointer
 *
 * @return FDA dtag pointer
 */
struct fda_dtag_format *ficu_get_addr_dtag_ptr(u32 du_dtag_ptr);
/*!
 * @brief Get an FINST slot to fill
 *
 * @return FIFO pointer
 */
u32* ficu_get_finstr_slot(void);
/*!
 * @brief FCMD DONE or ERR interrupt handling
 *
 * @return Not used
 */
void ficu_isr(void);
/*!
 * @brief Switch the SQ type
 *
 * @param[in] sq	SQ type
 *
 * @return Not used
 */
void ficu_sq_switch(u32 sq);
#ifdef ERRHANDLE_GLIST	
/*!
 * @brief Fill Errinfo for GList
 *
 * @param ERRinfo need
 *
 * @return Not used
 */
void Fill_Error_Info(pda_t pda, u32 fcmd_id, u32 err);
#endif
/*!
 * @brief FCMD error handler
 *
 * @param[in] int_src	interrupt source register value
 *
 * @return Not used
 */
void ficu_fcmd_err_handler(ficu_int_src_reg_t int_src);
/*!
 * @brief No_dqs_err isr error handler
 *
 * @return Not used
 */
void ficu_no_dqs_err_handler(void);
/*!
 * @brief Dump FINST structure value
 *
 * @return Not used
 */
void dump_finstr(struct finstr_format* ins);
/*!
 * @brief Transfer a PDA to CPDA
 *
 * @param[in] inst		FINST format pointer
 *
 * @return Not used
 */
void convert_pda2cpda(struct finstr_format* inst);
/*!
 * @brief Set the FICU transfer count config
 *
 * @return Not used
 */
void ficu_xfcnt_reg_cfg(void);
/*!
 * @brief FSPM initialization
 *
 * @return Not used
 */
void ficu_set_ard_du_size(u16 encoded_du_size);
void ficu_fspm_init(void);
/*!
 * @brief Set ARD enable or not
 *
 * @param[in] enable	enable or not
 *
 * @return Not used
 */
void ficu_mode_ard_control(u32 mode);

/*!
 * @brief resume ficu isr in be lite
 *
 * @param mode		resume from mode
 *
 * @return		not used
 */
void ficu_isr_resume(enum sleep_mode_t mode);

/*!
 * @brief register ficu isr in be lite
 *
 * @return		not used
 */
void ficu_isr_init(void);

static inline void ficu_fill_finst(struct finstr_format ins)
{
	struct finstr_format* pins;
	pins = (struct finstr_format*)ficu_get_finstr_slot();
	*pins = ins;
}

#endif
