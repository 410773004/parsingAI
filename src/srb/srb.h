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
#include "bf_mgr.h"
#include "ddr.h"
#include "fwconfig.h"

#if defined (SSSTC_FW_UPDATE)	//20200511-Eddie-4
	#define FWB_INDEPENDENT
	#define MULTI_SLOT		//20200521-Eddie
	#define FWUPDATE_SPOR
	#define UPDATE_LOADER		//20200529-Eddie
	//#define EXTRA_FWBK		//20201101-Eddie
	#define FWCA_2
	#define FW_IN_GROUP
#endif
#ifdef FWB_INDEPENDENT	//20200706-Eddie
	#define FWB_PGS		3
#endif

#ifdef UPDATE_LOADER
//#define PRT_LOADER_LOG
#endif

#define SAVE_DDR_CFG		//20201008-Eddie
#ifdef SAVE_DDR_CFG
#define FWcfg_Rebuild
#endif
//20200922-Eddie
   #define CTQ_PGS		3
   #define RVTAG_ID		((1 << 20) - 2)	    ///< unmapped vtag id 1, 1048574, 0xFFFFE
   #define RVTAG2_ID		(RVTAG_ID - 1)	///< unmapped vtag id 2, 1048573, 0xFFFFD
   #define WVTAG_ID		(RVTAG2_ID - 1)	    ///< dummy vtag id, 1048572, 0xFFFFC
   #define EVTAG_ID		(WVTAG_ID - 1)	    ///< read error vtag id, 1048571, 0xFFFFB
//#define LOADER_EXPAN
#define DDR_AUTO_SIZE_DET

#ifdef DDR_AUTO_SIZE_DET	//20200720-Eddie
	extern ddr_cfg_size_t ddr_auto_size;

 	#define DETDDR_2GB		2400
 	#define DETDDR_4GB		4600
 	#define DETDDR_8GB		9000
 	#define DETDDR_16GB	18000
#endif

#define CAP_IDX
#ifdef CAP_IDX	//20230712
	typedef enum {
	CAP_SIZE_512G = 0,
	CAP_SIZE_1T,	// 1
	CAP_SIZE_2T,	// 2
	CAP_SIZE_4T,	// 3
	CAP_SIZE_8T,	// 4	
	} cap_cfg_size_t;

	extern cap_cfg_size_t cap_idx;
#endif

#define FIX_HEADER_PAGE
#define RESERVE_SB_IN_FIX_POS

#ifdef RESERVE_SB_IN_FIX_POS
	#define RESERVE_SB_DUS		30
	#define RESERVE_SB_PGS		RESERVE_SB_DUS/SRB_MR_DU_CNT_PAGE	// 1 DU = 4 KB
	#define CLR_SCAN_DONE
#endif

#ifdef MPC
extern struct nand_info_t shr_nand_info;
#define ninfo	shr_nand_info
#else
#define ninfo	nand_info
#endif

#if !defined(PROGRAMMER)
#define epm_enable 1
#include "epm.h"
#endif

#define SRB_SIGNATURE 0x544952474F4E4E49ULL      /* INNOGRIT */

#if defined(USE_YMTC_NAND) || defined(USE_SNDK_NAND) || defined(QLC_SUPPORT)
/* the interleave value of YMTC TLC nand in my hand is 4,
 * so only reserve 2 blocks for SRB is not enough
 */
#define SRB_BLKNO             4   /* BLOCK0 ~ 3 */
#else
#define SRB_BLKNO             2   /* BLOCK0, BLOCK 1 */
#endif /* USE_YMTC_NAND */
#if defined(USE_8K_DU) || defined(USE_8K_PAGE)
#define SRB_MR_DU_CNT_PAGE    1
#else
#define SRB_MR_DU_CNT_PAGE    3
#endif
#define SRB_MR_DU_SZE        DTAG_SZE
#define NR_DUS_SLICE(sze)     (((sze) + SRB_MR_DU_SZE - 1) / SRB_MR_DU_SZE)

// this definie may be moved to .c

#define FACTORY_DEFECT_DWORD_LEN	32 // Bisc4:16 Bisc5:32

#define FLAG_AGINGBITMAP  1
#define FLAG_AGINGTESTMAP 2

/*! @brief define defect bitmap geo in MR, should follow programmer setting*/
#define PLN_SHF			(0)
#define PLN_BTS			2 // Bisc4:1 Bisc5:2
#define LUN_SHF			(PLN_SHF + PLN_BTS)
#define LUN_BTS			2
#define CE_SHF			(LUN_SHF + LUN_BTS)
#define CE_BTS			3
#define CH_SHF			(CE_SHF  + CE_BTS)
#define CH_BTS			4

/*! @brief define defect bitmap geo in FTL/rawdisk, used to tran MR defect*/
#define FTL_PLN_SHF		(0)
#define FTL_PLN_BTS		ctz(ninfo.geo.nr_planes)
#define FTL_CH_SHF		(FTL_PLN_SHF + FTL_PLN_BTS)
#define FTL_CH_BTS		ctz(ninfo.geo.nr_channels)
#define FTL_CE_SHF		(FTL_CH_SHF + FTL_CH_BTS)
#define FTL_CE_BTS		ctz(ninfo.geo.nr_targets)
#define FTL_LUN_SHF		(FTL_CE_SHF + FTL_CE_BTS)
#define FTL_LUN_BTS		ctz(ninfo.geo.nr_luns)
#define ftl_media_layout		\
	.pln_shf = FTL_PLN_SHF,		\
	.pln_bts = FTL_PLN_BTS,		\
	.ch_shf = FTL_CH_SHF,		\
	.ch_bts = FTL_CH_BTS,		\
	.ce_shf = FTL_CE_SHF,		\
	.ce_bts = FTL_CE_BTS,		\
	.lun_shf = FTL_LUN_SHF,		\
	.lun_bts = FTL_LUN_BTS,		\

#define MR_PAGE_SZE           (SRB_MR_DU_CNT_PAGE * SRB_MR_DU_SZE)
#define NR_PAGES_SLICE(sze)   (((sze) + MR_PAGE_SZE - 1) / MR_PAGE_SZE)

#ifndef SRB_HD_ADDR
#define SRB_HD_ADDR 0x20000000
#endif
#define LOADER_COM_BUFFER_ADDR 0x200f9000 //loader copy FW block head struct fwb_t addr
#define CONFIG_COM_BUFFER_ADDR 0x200fa000 //loader copy FW block head struct fwb_t addr
#define MAX_PUBLIC_KEY_LEN		4096 ///< Fixed public key to one page, max 4096 bytes
#define WCHAR(a, b, c, d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))
#define ID_PKEY         WCHAR('P', 'K', 'E', 'Y')
#define ID_KOTP         WCHAR('K', 'O', 'T', 'P')
#define ID_SHA3_256     WCHAR('F', 'S', 'H', 'A')
#define ID_SHA256       WCHAR('S', 'S', 'H', 'A')
#define ID_SIGN         WCHAR('S', 'S', 'I', 'G')  /* sha256 with RSA */
#define ID_SM2_ZA       WCHAR('S', 'M', 'Z', 'A')
#define ID_SM2_SIGN     WCHAR('S', 'M', 'S', 'G')  /* SM2 RS */

enum {
	RS_SECURITY_MODE = 0,
	SM_SECURITY_MODE = 1,
	NONE_SECURITY_MODE = 0xFF,
};

enum {
	SECURITY_DISABLE = 0,
	SECURITY_ENABLE = 1,
};

typedef struct _media_layout_t {
	u8 pln_shf;
	u8 pln_bts;

	u8 lun_shf;
	u8 lun_bts;

	u8 ce_shf;
	u8 ce_bts;

	u8 ch_shf;
	u8 ch_bts;
} media_layout_t;

/* XXX: Never change the structure */
typedef struct _srb_header_t {
	u64 srb_signature;          /* INNOGRIT*/
	s32  srb_clk_deviation;      /* diviation of SYS_CLK for UART */
	u32 srb_sb_row;             /* Rainier Bootloader row address */
	u32 srb_sb_row_mirror;      /* mirror row on the same CH-CE-LUN */
	u32 srb_sb_row_dual;        /* Dual Copy of Rainier Bootload row */
	u32 srb_sb_row_dual_mirror; /* mirror dual row on the same CH-CE-LUN */
	u32 srb_sb_du_cnt;          /* # of DU. 2K */
	u32 srb_sb_ep;              /* Entry Pointer of Loader */
	u32 srb_csum;               /* CRC32 */
	u32 srb_sb_du_in_page;      /* SB DUs in a page */
	u32 srb_symb_pattern;       /* Signature ON to exports share functions, enabled by ROM, 0x4F4E4E49 */
	u32 srb_symb_tbl_in_rom;    /* ROM exports share functions */
	u32 srb_symb_tbl_len;       /* ROM exports share functions length */
	u32 srb_pub_key;            /* Public key row addr */
	u32 srb_pub_key_mirror;     /* Public key row mirror addr */
	u32 srb_pub_key_dual;       /* Public key row dual addr */
	u32 srb_pub_key_dual_mirror;/* Public key row dual mirror addr */
	u32 srb_pub_key_len;        /* Public key length */
	u32 srb_security_enable;
	u32 srb_security_mode;
	u32 srb_sectest_cfg;         /* security self test configure */
	u32 srb_config_row;             /* Rainier fw config row address */
	u32 srb_config_row_mirror;      /* mirror row on the same CH-CE-LUN */
	u32 srb_config_row_dual;        /* Dual Copy of Rainier fw config row */
	u32 srb_config_row_dual_mirror; /* mirror dual row on the same CH-CE-LUN */
	u32 srb_rsvd[16];
} PACKED srb_hdr_t;

typedef struct _srb {
	srb_hdr_t srb_hdr;          /* SRB header */

	rda_t     enc_pos;          /* position of Encoding CMF */
	u32       enc_sz;           /* real size of Encoding CMF */

	rda_t     dec_pos;          /* position of Decoding CMF */
	u32       dec_sz;           /* real size of Decoding CMF */

	rda_t     dftb_pos;         /* position of factory defect bitmap */
	u32       dftb_sz;          /* real size of factory defect bitmap */

	rda_t     fwb_pri_pos;      /* Firmware Primary Block */
	rda_t     fwb_sec_pos;      /* Firmware Secondary Block */

	rda_t     ftlb_pri_pos;     /* FTL Primary block */
	rda_t     ftlb_sec_pos;     /* FTL Secondary block */

	rda_t     devb_pri_pos;     /* Device Primary block */
	rda_t     devb_sec_pos;     /* Device Secondary block */

	rda_t     evtb_pi_pos;      /* Event Ping block */
	rda_t     evtb_po_pos;      /* Event Pong block */

	/* Cached indexes */
	rda_t     opal_pos;
	rda_t     opal_sz;

	rda_t     dftb_m_pos;

	rda_t     fwb_buf_pos;      /* Firmware upgrade buffer Block */

	u32  dftb_ftl_sz;          /* real size of ftl defect bitmap default is 0 */

	u8	ndphy_dll_valid;	/* saved ndphy dll value in SRB */
	u8	ndphy_rsvd[3];
	u8	ndphy_dll_set[16][8][4];	/* ndphy dll value per ch/ce/lun */
	//20200727-Eddie
	rda_t     ctq_pri_pos;     /* CTQ Primary block */
	rda_t     ctq_mir_pos;     /* CTQ Mirror block */
	u8	do_scan_fbb;

	rda_t     srb_buf_pos;		/* SRB buffer Block */

	rda_t     srb_1st_pos;		/* 1ST SRB Block Position*/

	rda_t     srb_2nd_pos;		/* 2nd SRB Block Position*/

	rda_t     srb_3rd_pos;		/* 3rd SRB Block Position*/

	rda_t     srb_4th_pos;		/* 4th SRB Block Position*/

	rda_t     srb_5th_pos;		/* 5th SRB Block Position*/

	rda_t     srb_6th_pos;		/* 6th SRB Block Position*/

	rda_t     srb_7th_pos;		/* 7th SRB Block Position*/

	rda_t     srb_8th_pos;		/* 8th SRB Block Position*/

	u32	srb_hdr_pgs;		/* Total srb header pages in SRB Block*/

	u32	sb_csum;		//section_csum

	/*EMP USE total 224 bytes*/   //20200715-Kevin
	rda_t     epm_header_pos[2];      /* emp header */
	rda_t     epm_header_mir_pos[2];      /* emp header mirror */
	rda_t     epm_pos[8];      /* emp data */
	rda_t     epm_pos_mir[8];      /* emp data mirror*/

	u8		cap_idx;		//Capacity Info

	u8      ALL_defect_scan_Done;	//AlanHuang

	char		ddr_idx;		//DDR info
// Jamie 20201016 for DLL range record
    u8 dll_start[16][8][4];
    u8 dll_end[16][8][4];
    u8 dll_maxerr[16][8][4];

    char pgr_ver[6];
    char ldr_ver[6];	//20201020-Eddie
    err_t pgr_err[10];	//20201028-Eddie
    u32 ldr_SHA3[8];
	u8 board_cfg;
    u8 DLL_margin_ndphy_odt; //2024/09/26 James
    u8 DLL_margin_ndphy_drv; //2024/09/26 James
    u8 DLL_margin_nand_odt; //2024/09/26 James
    u8 DLL_margin_nand_drv; //2024/09/26 James
    u8 DLL_flag; //2024/10/25 James,use srb-dll value or not
    u8 DLL_Auto_Tune;
    u8 ndphy_odt[8];
    u8 ndphy_drv[8];
    u8 nand_odt[8][8];
    u8 nand_drv[8][8];
    u8 rsvd[1261]; 
} srb_t;
BUILD_BUG_ON(sizeof(srb_t) != 4096);

typedef struct {
	u32 dft_bitmap[FACTORY_DEFECT_DWORD_LEN]; /* parallelism up to 256 */
} dft_btmp_t;

typedef struct {
	unsigned int identifier;   /* identifier of the section, etc. uEFI, ATCM and so on */
	unsigned int offset;       /* offset of the section into the image */
	unsigned int length;       /* length of the section */
	unsigned int pma;          /* PMA of the section to load */
} loader_section_t;

typedef struct {
	unsigned int slice_start;
	unsigned int slice_end;
} fw_slice_t;

typedef struct {
	unsigned int signature;     /* equals to IMAGE_SIG */
	unsigned int entry_point;   /* entry point of ELF */
	unsigned short section_num; /* # of sections */
	unsigned short image_dus;   /* # of 2K dus */
	unsigned int section_csum;  /* checksum of section(s) */
	union {
		loader_section_t sections[0];
		fw_slice_t fw_slice[0];
	};
} loader_image_t;

typedef struct {
	unsigned int slices;       /* NR CPU slices */
	unsigned int cpus[8];
	fw_slice_t  fw_slice[0];
} __attribute__((packed)) multi_fw_t;

//typedef struct  {
//    u32 AgingPlistBitmap[1980][FACTORY_DEFECT_DWORD_LEN];
//    u32 AgingBitMap_Tag;
//} AgingPlistBitmap_t;


typedef struct  {
	u64  fw_slot_version;       /* 1.0.0.5z */
	u32  fw_slot_du_cnt;        /* #DU in unit 2K */
	u8  rsvd[3];
	u8	grp_idx;		// For FW upgrade SPOR 1:CH1,LUN0,PL1,CE0 ; 2:CH2,LUN0,PL1,CE0
	rda_t     fw_slot;
	rda_t     fw_slot_mirror;

	rda_t     fw_slot_dual;
	rda_t     fw_slot_dual_mirror;
} fw_slot_t;

#define MAX_FWB_SLOT (7)
typedef struct {
	u32 signature;                /* 'FRMB' */
	u16 active_slot;              /* active slot in use */
	u16 total_slot;               /* up to 7 */

	fw_slot_t fw_slot[MAX_FWB_SLOT];
	u8   rsvd[1704];
} fwb_t;
BUILD_BUG_ON(sizeof(fwb_t) != 2048);
typedef struct {
	u64 fw_version;
	rda_t fw_pri;
	rda_t fw_sec;
	rda_t buf_rda;
	u16 image_dus;
	u16 mfw_dus;
	u8 slot;
	u8 active_slot;
	u8 ca;	//commit action
} rebuild_fwb_para_t;


#ifdef UPDATE_LOADER
	#include "fw_download.h"
	typedef struct {
		u32 sb_csum;
		u16 sb_image_dus;
		u16 srb_hdr_pgs;
		rda_t fwb_buf_pos;
		u16 sb_start;
		u16 sb_end;
		dtag_t sb_dus[DOWNLOAD_MR_DTAG_CNT_PAGE];	///< SB du array
		rda_t srb_buf_pos;
		rda_t srb_1st_pos;
		rda_t srb_2nd_pos;
		rda_t srb_3rd_pos;
		rda_t srb_4th_pos;
		rda_t srb_5th_pos;
		rda_t srb_6th_pos;
		rda_t srb_7th_pos;
		rda_t srb_8th_pos;
	} rebuild_srb_para_t;
#endif

void dtagprint(dtag_t d2m, int mem_len);
bool srb_alloc_block(dft_btmp_t *dft, bool use_die0, rda_t *rda);
void fbbt_trans_layout(dft_btmp_t *dft, u8 *bbt, media_layout_t *geo, u32 count, u32 bbt_width);
bool srb_load_fbbt(dft_btmp_t *dft, u32 size, rda_t rda, u32 dus);
bool srb_scan_and_load(dtag_t *srb_dtag);
void srb_page_copy(rda_t des_rda, rda_t sou_rda, u16 page_cnt);
void srb_rebuild_fwb(rebuild_fwb_para_t para);
bool srb_fwb_verify_after_upgrade(rda_t fwb_pri_rda, rda_t fwb_sec_rda);
void fw_build_buffer_block(rda_t bf_rda, dtag_t *sb_dus,u8 pg_offset, u8 du_cnt);
bool fw_verify_image_crc(dtag_t last_du, u32 *pre_crc, u64 *fw_version);
bool srb_loader_verify(rda_t image_rda);
bool srb_combo_verify_and_restore(rda_t image_rda);
bool srb_read_srb_to_memory(rda_t srb_rda, char *des, u8 count);
void srb_trans_to_mr_df(dft_btmp_t *mr_df, u32 *df);
bool srb_image_verify_sha3(rda_t image_rda, void *fw_sha3, u8 mode);
bool srb_image_loader_cpu34_atcm(rda_t image_rda);
void Aging_Program_SRB(void *record, u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page,u8 flag);
void fill_SRB(u32 SRBtype);
void AgingSRB_Erase(u8 Ch, u8 CE, u8 Lun, u8 plane, u16 page);
void mpin_init();
void fill_cur_SRB(u32 SRBtype, u8 pl);   //Sean_20220511
void SysInfo_bkup(u8 pl);                 //Sean_20220511

/*!
 * read defect bitmap from MR
 *
 * @param ftl_bitmap	compact bitmap
 * @param bbt_width	aligned bytes of defect bitmap of spb
 */
void srb_load_mr_defect(u8 *ftl_bitmap, u32 bbt_width);
//20200720-Eddie
void memprint(char *str, void *ptr, int mem_len);
//20200714-Eddie
void srb_read_verify_(rda_t rda, u32 du_cnt, u32 mem_len);
void sb_verify_(rda_t rda, u32 mem_len, u32 dus);
bool srb_read_kotp(rda_t rda, u32 *kotp);
bool srb_alloc_block_addr_assign(dft_btmp_t *dft,bool reset ,rda_t *rda, u8 _ch, u8 _dev, u8 _lun, u8 _pln);
#ifdef UPDATE_LOADER
void srb_buf_modify_write(rebuild_srb_para_t srb_para ,u16 page_cnt, rda_t rda_sou);	//20200603-Eddie
void srb_rebuild_sb(rebuild_srb_para_t srb_para);
void srb_page_read(rda_t des_rda, rda_t sou_rda, u16 page_cnt);
#endif

#ifdef SAVE_DDR_CFG		//2020922-Eddie
	extern void FW_CONFIG_Rebuild(fw_config_set_t *fw_config);
#endif
#ifdef FWcfg_Rebuild
	void __FW_CONFIG_Rebuild(fw_config_set_t *fw_config);
#endif
extern bool erase_srb(void);	//20201014-Eddie
bool __erase_srb(void);