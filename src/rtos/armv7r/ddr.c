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

/*! \file ddr.c
 * @brief ddr initialization
 *
 * \addtogroup rtos
 * \defgroup ddr
 * \ingroup rtos
 *
 * {@
 */

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "types.h"
#include "stdio.h"
#include "stdlib.h"
#include "io.h"
#include "ddr.h"
#include "rainier_soc.h"
#include "mc_reg.h"
#include "sect.h"
#include "l2cache.h"
#include "ddr_top_register.h"
#include "bf_mgr.h"
#include "misc.h"
#include "pmu.h"
#include "console.h"
#ifdef MPC
#include "spin_lock.h"
#endif
#include "dfi_init.h"
#include "dfi_common.h"
#include "dfi_reg.h"
#include "ddr_info.h"
#include "string.h"
#include "fwconfig.h"
#include "a0_rom_10011.h"
#include "srb.h"
#include "nvme_spec.h"

/*! \cond PRIVATE */
#define __FILEID__ ddr
#include "trace.h"
/*! \endcond */

#ifdef GET_CFG	//20200822-Eddie
	u8 *cfg_pos = (u8*) ddrcfg_buf_rom_addr;  //This is position where loader already loaded
	slow_data fw_config_set_t *fw_config_main;
#endif
#ifdef SAVE_DDR_CFG		//20201008-Eddie
	share_data volatile ddr_info_t*  ddr_info_buf_in_ddr; //ddr_info_buf in DDR
	share_data fw_config_set_t* fw_config_main_in_ddr;
#endif
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define DDR_SCAN_TEST	1
#define PECC_1BIT_THRESHOLD	1			///< parallel ecc 1 bit error interrupt threshold
#define IECC_1BIT_THRESHOLD	1			///< inline ecc 1 bit error interrupt threshold
//20200727-Eddie
#define DST_RAM_TEST_DU_CNT 256  // 1MB

#if defined(LOADER)
//#define DDR_SIZE_RAINIER	0x100000000		///< rainier ddr size 4GB with U.2
#define DDR_SIZE_DEFAULT	0x40000000
extern u8 *com_buf;
extern fw_config_set_t *fw_config;
#endif
//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef enum ddr_info_bkup_t {
	DDR_INFO_ALL = 0,
	DDR_INFO_DFI,
	DDR_INFO_MC,
} ddr_info_bkup_t;

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
#ifdef OFFSET_DDR_DTAG   //20201106-Eddie-Leave 100 MB DDR space dor code
share_data_zi volatile u32 max_ddr_sect_cnt;			/// code+data
#endif
share_data_zi volatile u32 max_ddr_dtag_cnt;			///< include IO+GC
share_data_zi volatile u32 _ddr_dtag_free;
share_data_zi volatile u32 ddr_dtag_free;
share_data_zi volatile u32 ddr_dtag_next;
share_data_zi volatile u64 ddr_capacity;
#ifdef L2P_DDTAG_ALLOC
share_data_zi volatile u64 ddr_l2p_capacity;
#endif
#ifdef L2P_DDTAG_ALLOC
share_data_zi volatile u32 ddr_dtag_next_l2p;
share_data_zi volatile u32 ddr_dtag_free_l2p;
#ifdef L2P_FROM_DDREND
	share_data_zi volatile u32 ddr_dtag_next_from_end;
#endif
#endif

#ifdef EPM_DDTAG_ALLOC	//20210521-Eddie
share_data_zi volatile u64 ddr_epm_capacity;
share_data_zi volatile u32 ddr_dtag_next_epm;
share_data_zi volatile u32 ddr_dtag_free_epm;
#endif

#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
share_data_zi volatile u64 ddr_trim_capacity;
share_data_zi volatile u32 ddr_dtag_next_trim;
share_data_zi volatile u32 ddr_dtag_free_trim;
#endif

share_data_zi volatile u32 ddr_cache_cnt;

fast_data_zi int ddr_inited = 0;
norm_ps_data ddr_info_t ddr_info_buf;
#ifdef SAVE_DDR_CFG		//20201008-Eddie
	share_data volatile u8 need_save_to_CTQ;
#endif

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
share_data_zi volatile u32 ddr_dtag_next_telemetry;
share_data_zi volatile u32 ddr_dtag_free_telemetry;
#endif

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------
extern void poweron_ddr(void);
extern volatile tDRAM_Training_Result   DRAM_Train_Result;
extern tencnet_smart_statistics_t *tx_smart_stat;
//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------

static inline void ddr_top_writel(u32 data, u32 reg)
{
	writel(data, (void *)(DDR_TOP_BASE + reg));
}

static inline u32 ddr_top_readl(u32 reg)
{
	return readl((void *)(DDR_TOP_BASE + reg));
}

fast_code void ddr_setup_window(u32 ddr_seg)
{
	mc_ctrl_reg0_t ctrl0 = { .all = ddr_top_readl(MC_CTRL_REG0)};

#if defined(MPC)
	spin_lock_take(SPIN_LOCK_KEY_DDR_WINDOWS, 0, true);
#endif
	ctrl0.all &= ~((CPU1_DDR_WINDOW_SEL_MASK) << (CPU2_DDR_WINDOW_SEL_SHIFT * CPU_ID_0));
	//rtos_core_trace(LOG_DEBUG, 0, "set CPU %d windows %d\n", CPU_ID_0, ddr_seg);
	ctrl0.all |= (ddr_seg) << (CPU2_DDR_WINDOW_SEL_SHIFT * CPU_ID_0);
	ddr_top_writel(ctrl0.all, MC_CTRL_REG0);

#if defined(MPC)
	spin_lock_release(SPIN_LOCK_KEY_DDR_WINDOWS);
#endif
}

#if CPU_ID_0 == 0
#if DDR_SCAN_TEST
static void mem_scan_test(u32 start, u32 end, char* str)
{
	start = start & (~3);
	u32 size = end - start;
	u32 i;
	u32 *ptr;
	u32 pat;
	u32 error = 0;

	size /= sizeof(u32);

	ptr = (u32*) start;
	rtos_ddr_trace(LOG_ERR, 0x3f8c, "cpu memory scan start %08x end %08x\n", start, end);

	pat = start;
	for (i = 0; i < size; i++) {
		ptr[i] = pat;
		pat += 4;
	}

	pat = start;
	for (i = 0; i < size; i++) {
		if (ptr[i] != pat) {
			rtos_ddr_trace(LOG_ERR, 0x4380, "%p expect %08x but %08x\n",
					&ptr[i], pat, ptr[i]);
			error++;
			sys_assert(0);
		}
		pat += 4;
	}
	rtos_ddr_trace(LOG_ERR, 0xe579, "%s memory scan error cnt %d\n", str, error);
}
#endif
/*
#ifdef FPGA
#define writelr(reg, val)	writel(val, (void *) reg)
#define readl(reg)		readl((void *) (reg))
static void ddr4_vu440_init(void)
{
	// MC_Init_Code_svn1317_Micron_DDR4_8g_2133_64bit_mc10M_ref40M.txt
	rtos_core_trace(LOG_ALW, 0, "DDR4 initializing");
	writelr(0xc0068004, 0x0);

	//phase 1 - phy init
	//mc
	writelr(0xc00601c8, 0x2120900);
	writelr(0xc00601d0, 0x00000000);
	//dfi
	writelr(0xc0064080, 0x1310);
	writelr(0xc0064080, 0x1310);
	writelr(0xc0064080, 0x1311);
	writelr(0xc0064080, 0x1315);
	writelr(0xc0064080, 0x1311);

	writelr(0xc0064010, 0x1f1f00ef);
	writelr(0xc0064014, 0x100000);
	writelr(0xc0064020, 0x1f1f00fe);
#ifdef ENABLE_PARALLEL_ECC
	writelr(0xc0064088, 0x112300);
#else
	writelr(0xc0064088, 0x110300);
#endif
	writelr(0xc0064090, 0x231);
	writelr(0xc0064094, 0x0);
	writelr(0xc00640a0, 0x70c07);
	writelr(0xc00640c4, 0x10000);
	writelr(0xc00640c4, 0x0);
	writelr(0xc0064120, 0x6);
	writelr(0xc0064110, 0x0);
	writelr(0xc0064110, 0x1);
	writelr(0xc0064204, 0x1);
	writelr(0xc0064210, 0x1f1f);
	writelr(0xc0064204, 0x0);
	writelr(0xc0064110, 0x11);
	writelr(0xc0064110, 0x1);

	writelr(0xc0064204, 0x30);
	writelr(0xc00640b0, 0x10000);
	writelr(0xc0064230, 0x0);
	writelr(0xc0064234, 0x0);
	writelr(0xc0064238, 0x0);
	writelr(0xc00640b0, 0x0);
	writelr(0xc0064204, 0x0);

	writelr (0xc006402c, 0xfe);
	//phase 2 - mc init
	writelr(0xc0060008, 0x80400);
	writelr(0xc0060010, 0x200);
	writelr(0xc0060020, 0x0);
	//writelr(0xc0060024, 0x131);
	cs_config_addr_map0_t addr_map0 = {
			.all = 0,
			.b.cs_enable = 1,
			.b.area_length = 0x14, // 4G DDR area
			};
	mc0_writel(addr_map0.all, CS_CONFIG_ADDR_MAP0);

	writelr(0xc0060028, 0x0);
	writelr(0xc006002c, 0x448);
	writelr(0xc0060030, 0x1);
	writelr(0xc0060000, 0x1380); //For Rainier
	writelr(0xc0060008, 0x80400);
	writelr(0xc0060010, 0x200);

	writelr(0xc0060004, 0x90a0a0);

#define DDR_DOUBLE_SPEED
#ifdef DDR_DOUBLE_SPEED
	writelr(0xc0060060, 0x401F40);
	writelr(0xc0060064, 0x4E2001);
	writelr(0xc0060068, 0x5);
	writelr(0xc006006c, 0x400300);
	writelr(0xc0060070, 0x2000080);
	writelr(0xc0060074, 0x2010204);
	writelr(0xc0060078, 0x50102045);
	writelr(0xc006007c, 0x2042044);
	writelr(0xc0060080, 0x8000018);
	writelr(0xc0060084, 0xEA000E);
	writelr(0xc0060088, 0x50500F3);
	writelr(0xc006008c, 0x0);
	writelr(0xc0060090, 0x10120044);
	writelr(0xc0060094, 0x405306);
#else
	writelr( 0xc0060060,0x200fa0);
	writelr( 0xc0060064,0x271001);
	writelr( 0xc0060068,0x5);
	writelr( 0xc006006c,0x400300);
	writelr( 0xc0060070,0x2000080 );
	writelr( 0xc0060074,0x1010104 );
	writelr( 0xc0060078,0x28081045 );
	writelr( 0xc006007c,0x84214d );
	writelr( 0xc0060080,0x8000018 );
	writelr( 0xc0060084,0x138000a );
	writelr( 0xc0060088,0x50500a3 );
	writelr( 0xc006008c,0x0 );
	writelr( 0xc0060090,0x10120044 );
	writelr( 0xc0060094,0x405306 );
#endif

	writelr(0xc0060098, 0x0);
	writelr(0xc006009c, 0x0);
	writelr(0xc00600a0, 0x0);
	writelr(0xc00600a4, 0x0);
	writelr(0xc00600a8, 0x700000);
	writelr(0xc00600ac, 0x0);
	writelr(0xc00600e0, 0x0);
	writelr(0xc00600e4, 0x0);
	writelr(0xc0060020, 0x0);
	writelr(0xc0060034, 0x0);
	writelr(0xc00600c0, 0x30004);
	writelr(0xc00600d0, 0x1f);
	writelr(0xc00600d4, 0x10149);
	writelr(0xc00600f0, 0x0);
	writelr(0xc00601a4, 0x0);
	writelr(0xc0060100, 0x1);
	while (readl(0xc0060160) != 0x1);

	// phase3,	DDR3 to DLL-off mode
	writelr(0xc0060104, 0x5);
	while (readl(0xc0060008) != 0x80400);

	writelr(0xc0060008, 0x80200);
	writelr(0xc0060020, 0x0);
	writelr(0xc0060034, 0x0);
	writelr(0xc0060108, 0x1100201);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29

	writelr(0xc0060140, 0x1000001);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29
	while (readl(0xc0060160) != 0x3);

	writelr(0xc00600e0, 0x0);
	writelr(0xc0060140, 0x1000010);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29
	while (readl(0xc0060160) != 0x1);

	writelr(0xc0060108, 0x1100200);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29

	writelr(0xc0060108, 0x1300202);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29

	writelr(0xc0060108, 0x1200204);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29
		;

	writelr(0xc0060108, 0x1300205);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29

	writelr(0xc0060108, 0x1100206);
	while ((readl(0xc0060168)&(~0x20000000)) != 0xef71f1f); //<= ignore bit 29

	writelr(0xc0060120, 0x1000001);
	while (readl(0xc0060008) != 0x80200);

	writelr(0xc0060008, 0x80200);
	writelr(0xc0060104, 0x0);
	while (readl(0xc0060008) != 0x80200);

	writelr(0xc0060008, 0x80200);
	writelr(0xc0060020, 0x0);
	writelr(0xc006002c, 0xa000448);

	writelr(0xc0068014, 0);
	rtos_core_trace(LOG_ALW, 0, "done");
}

static void ddr3_h2k_init(void) // TODO : readability
{
	// MC_Init_Code_svn1315_Micron_DDR3_4gx8_1866_32bit_mc10M_ref10M.txt
	rtos_core_trace(LOG_ALW, 0, "DDR3 initializing");

	writelr(0xc0068004, 0x0);

	//phase 1 - phy init
	//mc
	writelr(0xc00601c8, 0x120500);
	writelr(0xc00601d0, 0x1f000000);
	//dfi
	writelr(0xc0064080, 0x1420);
	writelr(0xc0064080, 0x1420);
	writelr(0xc0064080, 0x1421);
	writelr(0xc0064080, 0x1425);
	writelr(0xc0064080, 0x1421);

	writelr(0xc0064010, 0x1f1f44ef);
	writelr(0xc0064014, 0x2100024);
	writelr(0xc0064020, 0x1f1f00fe);
	writelr(0xc0064088, 0x112201);
	writelr(0xc0064090, 0x231);
	writelr(0xc0064094, 0x10);
	writelr(0xc00640a0, 0xc07);
	writelr(0xc00640c4, 0x10000);
	writelr(0xc00640c4, 0x0);
	writelr(0xc0064120, 0x6);
	writelr(0xc0064110, 0x0);
	writelr(0xc0064110, 0x1);
	writelr(0xc0064204, 0x1);
	writelr(0xc0064210, 0x1f1f);
	writelr(0xc0064204, 0x0);
	writelr(0xc0064110, 0x11);
	writelr(0xc0064110, 0x1);

	writelr(0xc0064204, 0x30);
	writelr(0xc00640b0, 0x10000);
	writelr(0xc0064230, 0x0);
	writelr(0xc0064234, 0x0);
	writelr(0xc0064238, 0x0);
	writelr(0xc00640b0, 0x0);
	writelr(0xc0064204, 0x0);

	//phase 2 - mc init
	writelr(0xc0060008, 0x400);
	writelr(0xc0060020, 0x0);
	writelr(0xc0060024, 0x131);
	writelr(0xc0060028, 0x0);
	writelr(0xc006002c, 0x541);
	writelr(0xc0060030, 0x1);
	writelr(0xc0060000, 0x1301);
	writelr(0xc0060020, 0x0);
	writelr(0xc0060034, 0x200000);
	writelr(0xc0060004, 0x500060);
	writelr(0xc0060060, 0x200fa0);
	writelr(0xc0060064, 0x271001);
	writelr(0xc0060068, 0x5);
	writelr(0xc006006c, 0x200200);
	writelr(0xc0060070, 0x1000040);
	writelr(0xc0060074, 0x1010104);
	writelr(0xc0060078, 0x2081044);
	writelr(0xc006007c, 0x1042024);
	writelr(0xc0060080, 0x400000c);
	writelr(0xc0060084, 0x4e0006);
	writelr(0xc0060088, 0x5050063);
	writelr(0xc006008c, 0x0);
	writelr(0xc0060090, 0x10a23);
	writelr(0xc0060094, 0x404305);
	writelr(0xc0060098, 0x0);
	writelr(0xc006009c, 0x0);
	writelr(0xc00600a0, 0x0);
	writelr(0xc00600a4, 0x0);
	writelr(0xc00600a8, 0x0);
	writelr(0xc00600ac, 0x0);
	writelr(0xc00600e0, 0x0);
	writelr(0xc00600e4, 0x0);
	writelr(0xc0060020, 0x0);
	writelr(0xc0060034, 0x200000);
	writelr(0xc00600c0, 0x30000);
	writelr(0xc00600d0, 0x1f);
	writelr(0xc00600d4, 0x10149);
	writelr(0xc00600f0, 0x0);
	writelr(0xc00601a4, 0x0);
	writelr(0xc0060100, 0x1);
	// mc_init done status
	while (readl(0xc0060160) != 0x1);

	// phase3, DDR3 to DLL-off mode
	writelr(0xc0060104, 0x5);
	while (readl(0xc0060008) != 0x400);

	writelr(0xc0060008, 0x200);
	writelr(0xc0060020, 0x0);
	writelr(0xc0060034, 0x0);
	writelr(0xc0060108, 0x1000201);
	while (readl(0xc0060168) != 0x2ef71f1f);

	writelr(0xc0060140, 0x1000001);
	while (readl(0xc0060168) != 0xef71f1f)
		;
	while (readl(0xc0060160) != 0x3);

	writelr(0xc00600e0, 0x0);
	writelr(0xc0060140, 0x1000010);
	while (readl(0xc0060168) != 0x2af71f1f);
	while (readl(0xc0060160) != 0x1);

	writelr(0xc0060108, 0x1200200);
	while (readl(0xc0060168) != 0x2af71f1f);

	writelr(0xc0060108, 0x1200202);
	while (readl(0xc0060168) != 0x2af71f1f);

	writelr(0xc0060120, 0x1000001);
	while (readl(0xc0060008) != 0x200);

	writelr(0xc0060008, 0x200);
	writelr(0xc0060104, 0x0);
	while (readl(0xc0060008) != 0x200);

	writelr(0xc0060008, 0x200);
	writelr(0xc0060020, 0x0);
	writelr(0xc006002c, 0xa000541);

	rtos_core_trace(LOG_ALW, 0, "done");
}
#endif
*/
slow_code void memprint(char *str, void *ptr, int mem_len)		//20200714-Eddie
{
	u32*pdata=NULL;
	pdata = ptr;
	rtos_ddr_trace(LOG_ERR, 0xd53f, "%s \n",*str);
    int c=0;
	for (c = 0; c < (mem_len/ sizeof(u32)); c++) {
	            if (1) {
	                    if ((c & 3) == 0) {
	                            rtos_ddr_trace(LOG_ERR, 0x8e77, "%x:", c << 2);
	                    }
	                    rtos_ddr_trace(LOG_ERR, 0x8e61, "%x ", pdata[c]);
	                    if ((c & 3) == 3) {
	                            rtos_ddr_trace(LOG_ERR, 0x5324, "\n");
	                    }
	            }
	        }
}
slow_code void ddr_modify_refresh_time(u16 current_temperature)
{
    #if 0 //Test mode
    dram_timing_ref0_t ref0 = {.all = mc0_readl(DRAM_TIMING_REF0),};
    if(ref0.b.trefi == DDR_REFRESH_TIME_7p8_us)
    {
        printk("[DDR]Refresh time = 7.8us\n");
        printk("CurTemp:%d\n",current_temperature);
        //if(current_temperature > DDR_REFRESH_TIME_TEMPERATURE_THRESHOLD)
        {
            printk("[DDR]Change Refresh time = 3.9 us\n");
            ref0.b.trefi = DDR_REFRESH_TIME_3p9_us;
            mc0_writel(ref0.all,DRAM_TIMING_REF0);
        }
    }
    else if(ref0.b.trefi == DDR_REFRESH_TIME_3p9_us)
    {
        printk("[DDR]Refresh time = 3.9us\n");
        printk("CurTemp:%d\n",current_temperature);
        //if(current_temperature < DDR_REFRESH_TIME_TEMPERATURE_THRESHOLD)
        {
            printk("[DDR]Change Refresh time = 7.8 us\n");
            ref0.b.trefi = DDR_REFRESH_TIME_7p8_us;
            mc0_writel(ref0.all,DRAM_TIMING_REF0);
        }
    }
    else
    {
        //do nothing
    }
    #else
    //0xXXC3XXXX 0xC3 = 7.8us, 0x62 = 3.9us
    dram_timing_ref0_t ref0 = {.all = mc0_readl(DRAM_TIMING_REF0),};
    if(ref0.b.trefi == DDR_REFRESH_TIME_7p8_us)
    {
        //printk("[DDR]Refresh time = 7.8us\n");
        //printk("CurTemp:%d\n",current_temperature);
        if(current_temperature > DDR_REFRESH_TIME_TEMPERATURE_THRESHOLD)
        {
            rtos_ddr_trace(LOG_DEBUG, 0x3281, "[DDR]Change Refresh time = 3.9 us\n");
            ref0.b.trefi = DDR_REFRESH_TIME_3p9_us;
            mc0_writel(ref0.all,DRAM_TIMING_REF0);
        }
        if(current_temperature == Invalid_Temperature)
        {
           rtos_ddr_trace(LOG_DEBUG, 0x0c3c, "[DDR]Thermal sensor invalid, Set to 3.9us\n");
        }
    }
    else if(ref0.b.trefi == DDR_REFRESH_TIME_3p9_us)
    {
        //printk("[DDR]Refresh time = 3.9us\n");
        //printk("CurTemp:%d\n",current_temperature);
        if(current_temperature < DDR_REFRESH_TIME_TEMPERATURE_THRESHOLD)
        {
            rtos_ddr_trace(LOG_DEBUG, 0x9da6, "[DDR]Change Refresh time = 7.8 us\n");
            ref0.b.trefi = DDR_REFRESH_TIME_7p8_us;
            mc0_writel(ref0.all,DRAM_TIMING_REF0);
        }
        if(current_temperature == Invalid_Temperature)
        {
            rtos_ddr_trace(LOG_DEBUG, 0x408a, "[DDR]Thermal sensor invalid, always keep 3.9us\n");
        }
    }
    else
    {
        //do nothing
    }
    #endif
}

/*!
 * @brief enable ddr ecc
 *
 * @return	not used
 */
norm_ps_code void ddr_ecc_enable(void)
{
	bool scrub = false;
	ras_cntl_t cntl = {.all = mc0_readl(RAS_CNTL),};
	device_mode_t mode = {.all = mc0_readl(DEVICE_MODE)};
	dfi_phy_cntl3_t cntl3 = { .all = 0 };
	u16 dfi_data_byte_disable = 0;

	if (mode.b.data_width == 3)
		dfi_data_byte_disable = 0x1F0;
	else if (mode.b.data_width == 2)
		dfi_data_byte_disable = 0x1FC;
	else
		sys_assert(0);

	cntl.b.ecc_enb = 0;
	cntl.b.inline_ecc_p0_enb = 0;
	cntl.b.inline_ecc_p1_enb = 0;
	cntl.b.inline_ecc_p2_enb = 0;

#ifdef ENABLE_PARALLEL_ECC
	if (mode.b.data_width == 3)
		dfi_data_byte_disable = 0x0F0;
	else
		sys_assert(0);

	// enable parallel ecc
	cntl.b.ecc_enb = 1;
	#if 1
	axi_qos_ctrl0_t axi_qos_ctrl0 = { .all = ddr_top_readl(AXI_QOS_CTRL0)};
	rtos_ddr_trace(LOG_ERR, 0x72bf, "AXI_QOS_CTRL0=%x\n", axi_qos_ctrl0.all);
	axi_qos_ctrl0.all = 0;
	ddr_top_writel(axi_qos_ctrl0.all, AXI_QOS_CTRL0);
	rtos_core_trace(LOG_ALW, 0x871c, "DDR P-ECC was enabled");
	#endif
	scrub = true;
#endif

#ifdef ENABLE_INLINE_ECC

	// enable inline ECC
	cntl.b.inline_ecc_p0_enb = 1;
	cntl.b.inline_ecc_p1_enb = 1;
	cntl.b.inline_ecc_p2_enb = 1;
	rtos_core_trace(LOG_ALW, 0x3ef8, "DDR I-ECC was enabled");
	scrub = true;
#endif

	cntl3.b.dfi_data_byte_disable = dfi_data_byte_disable;
	mc0_writel(cntl3.all, DFI_PHY_CNTL3);

#if !defined(ENABLE_INLINE_ECC) && !defined(ENABLE_PARALLEL_ECC)
	rtos_core_trace(LOG_ALW, 0x3637, "DDR ECC was disabled");
#endif

	//set the parity starting address
	mc0_writel((ddr_get_capapcity() & 0xFFF00000), INLINE_ECC_REGION_LOW);

	mc0_writel(cntl.all, RAS_CNTL);

	if (scrub)
		bm_scrub_ddr(0, ddr_get_capapcity(), 0);

	mc0_writel(ERR_FIFO_RESET_MASK, ERR_FIFO_CNTL0);
	mc0_writel(ECC_2BIT_ERR_COUNT_CLR_MASK | ECC_1BIT_ERR_COUNT_CLR_MASK, ECC_ERR_COUNT_STATUS_1);
	mc0_writel(ECC_ERR_INT_MASK, INTERRUPT_STATUS);
}

fast_code bool __ddr_suspend(enum sleep_mode_t mode)
{
	return true;
}

fast_code void __ddr_resume(enum sleep_mode_t mode)
{
	return;
}

fast_code void _ddr_info_bkup(enum ddr_info_bkup_t bkup, ddr_info_t* info_buf)
{
	return;
}

#if defined(DDR)
norm_ps_code void ddr_info_bkup(enum ddr_info_bkup_t bkup, ddr_info_t* info_buf)
{
	u32 i = 0;
	if (bkup == DDR_INFO_DFI || bkup == DDR_INFO_ALL) {
		info_buf->dfi_bkup.data0.all = dfi_readl(IO_DATA_0);
		info_buf->dfi_bkup.data1.all = dfi_readl(IO_DATA_1);
		info_buf->dfi_bkup.data2.all = dfi_readl(IO_DATA_2);
		info_buf->dfi_bkup.adcm0.all = dfi_readl(IO_ADCM_0);
		info_buf->dfi_bkup.adcm1.all = dfi_readl(IO_ADCM_1);
		info_buf->dfi_bkup.adcm2.all = dfi_readl(IO_ADCM_2);
		info_buf->dfi_bkup.adcm3.all = dfi_readl(IO_ADCM_3);
		info_buf->dfi_bkup.ck0.all = dfi_readl(IO_CK_0);
		info_buf->dfi_bkup.alert.all = dfi_readl(IO_ALERTN);
		info_buf->dfi_bkup.resetn.all = dfi_readl(IO_RESETN);
		info_buf->dfi_bkup.cal0.all = dfi_readl(IO_CAL_0);
		info_buf->dfi_bkup.cal1.all = dfi_readl(IO_CAL_1);
		info_buf->dfi_bkup.vgen_ctrl0.all = dfi_readl(VGEN_CTRL_0);
		info_buf->dfi_bkup.sync_ctrl.all = dfi_readl(SYNC_CTRL_0);
		info_buf->dfi_bkup.dfi_ctrl.all = dfi_readl(DFI_CTRL_0);
		info_buf->dfi_bkup.out_ctrl0.all = dfi_readl(OUT_CTRL_0);
		info_buf->dfi_bkup.out_ctrl1.all = dfi_readl(OUT_CTRL_1);
		info_buf->dfi_bkup.in_ctrl0.all = dfi_readl(IN_CTRL_0);
		info_buf->dfi_bkup.ck_gate.all = dfi_readl(CK_GATE_0);
		info_buf->dfi_bkup.lvl_all_ctrl0.all = dfi_readl(LVL_ALL_CTRL_0);
		info_buf->dfi_bkup.dll_ctrl1.all = dfi_readl(DLL_CTRL_1);
		info_buf->dfi_bkup.strgt_ctrl0.all = dfi_readl(STRGT_CTRL_0);
		info_buf->dfi_bkup.oadly0.all = dfi_readl(OADLY_0);
		info_buf->dfi_bkup.lp_ctrl0.all = dfi_readl(LP_CTRL_0);

		sel_ctrl_0_t ctrl0  = { .all = 0, };
		for (i = 0; i < DDR_MAX_BIT; i++) {
			ctrl0.b.sel_dbit = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			info_buf->dfi_bkup.dfi_dbit_bkup[i].sel_rd_dly_1 = (u8) dfi_readl(SEL_RD_DLY_1);
			info_buf->dfi_bkup.dfi_dbit_bkup[i].dq_wr_dly = (u8) dfi_readl(SEL_ODDLY_2);
		}

		ctrl0.all = 0;
		for (i = 0; i < DDR_MAX_BYTE; i++) {
			ctrl0.b.sel_dbyte = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].dll_phase0 = (u8) ((DLL_PHASE0_MASK & dfi_readl(SEL_DLL_0)) >> DLL_PHASE0_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].dll_phase1 = (u8) ((DLL_PHASE1_MASK & dfi_readl(SEL_DLL_0)) >> DLL_PHASE1_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].dm_rd_dly = (u8) ((DM_RD_DLY_MASK & dfi_readl(SEL_RD_DLY_0)) >> DM_RD_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].strgt_tap_dly = (u8) ((STRGT_TAP_DLY_MASK & dfi_readl(SEL_STRGT_0)) >> STRGT_TAP_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].strgt_phase_dly = (u8) ((STRGT_PHASE_DLY_MASK & dfi_readl(SEL_STRGT_0)) >> STRGT_PHASE_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].dqs_wr_dly = (u8) ((DQS_WR_DLY_MASK & dfi_readl(SEL_ODDLY_0)) >> DQS_WR_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].dm_wr_dly = (u8) ((DM_WR_DLY_MASK & dfi_readl(SEL_ODDLY_1)) >> DM_WR_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].ck_wr_dly = (u8) ((CK_WR_DLY_MASK & dfi_readl(SEL_OADLY_1)) >> CK_WR_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].rank_wr_dly = (u8) ((RANK_WR_DLY_MASK & dfi_readl(SEL_OADLY_2)) >> RANK_WR_DLY_SHIFT);
			info_buf->dfi_bkup.dfi_dyte_bkup[i].rank_wr_rsv = (u8) ((RANK_WR_RSV_MASK & dfi_readl(SEL_OADLY_2)) >> RANK_WR_RSV_SHIFT);
		}

		ctrl0.all = 0;
		for (i = 0; i < DDR_MAX_CA_NUM; i++) {
			ctrl0.b.sel_ca = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			info_buf->dfi_bkup.dfi_ca_bkup[i].ca_wr_dly = (u8) ((CA_WR_DLY_MASK & dfi_readl(SEL_OADLY_0)) >> CA_WR_DLY_SHIFT);
		}
	}

	if (bkup == DDR_INFO_MC || bkup == DDR_INFO_ALL) {
		info_buf->mc_bkup.dev_cfg_training.all = mc0_readl(DEVICE_CONFIG_TRAINING);
		info_buf->mc_bkup.dram_timing_offspec.all = mc0_readl(DRAM_TIMING_OFFSPEC);
		info_buf->mc_bkup.dfi_phy_cntl1.all = mc0_readl(DFI_PHY_CNTL1);
		info_buf->mc_bkup.dfi_phy_cntl3.all = mc0_readl(DFI_PHY_CNTL3);
		info_buf->mc_bkup.interrupt_cntl0.all = mc0_readl(INTERRUPT_CNTL0);
	}
}

norm_ps_code void ddr_info_restore(enum ddr_info_bkup_t bkup, ddr_info_t* info_buf)
{
	u32 i = 0;

	if (bkup == DDR_INFO_DFI || bkup == DDR_INFO_ALL) {
		dfi_writel(info_buf->dfi_bkup.data0.all, IO_DATA_0);
		dfi_writel(info_buf->dfi_bkup.data1.all, IO_DATA_1);
		dfi_writel(info_buf->dfi_bkup.data2.all, IO_DATA_2);
		dfi_writel(info_buf->dfi_bkup.adcm0.all, IO_ADCM_0);
		dfi_writel(info_buf->dfi_bkup.adcm1.all, IO_ADCM_1);
		dfi_writel(info_buf->dfi_bkup.adcm2.all, IO_ADCM_2);
		dfi_writel(info_buf->dfi_bkup.adcm3.all, IO_ADCM_3);
		dfi_writel(info_buf->dfi_bkup.ck0.all, IO_CK_0);
		dfi_writel(info_buf->dfi_bkup.alert.all, IO_ALERTN);
		dfi_writel(info_buf->dfi_bkup.resetn.all, IO_RESETN);
		dfi_writel(info_buf->dfi_bkup.cal0.all, IO_CAL_0);
		dfi_writel(info_buf->dfi_bkup.cal1.all, IO_CAL_1);
		dfi_writel(info_buf->dfi_bkup.vgen_ctrl0.all, VGEN_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.sync_ctrl.all, SYNC_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.dfi_ctrl.all, DFI_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.out_ctrl0.all, OUT_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.out_ctrl1.all, OUT_CTRL_1);
		dfi_writel(info_buf->dfi_bkup.in_ctrl0.all, IN_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.ck_gate.all, CK_GATE_0);
		dfi_writel(info_buf->dfi_bkup.lvl_all_ctrl0.all,LVL_ALL_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.dll_ctrl1.all, DLL_CTRL_1);
		dfi_writel(info_buf->dfi_bkup.strgt_ctrl0.all, STRGT_CTRL_0);
		dfi_writel(info_buf->dfi_bkup.oadly0.all, OADLY_0);
		dfi_writel(info_buf->dfi_bkup.lp_ctrl0.all, LP_CTRL_0);


		sel_ctrl_0_t ctrl0  = { .all = 0, };
		sel_rd_dly_1_t rd_dly_1 = { .all = 0, };
		sel_oddly_2_t oddly_2 = { .all = 0, };

		for (i = 0; i < DDR_MAX_BIT; i++) {
			ctrl0.b.sel_dbit = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			rd_dly_1.b.dq_rd_dly = info_buf->dfi_bkup.dfi_dbit_bkup[i].sel_rd_dly_1;
			dfi_writel(rd_dly_1.all, SEL_RD_DLY_1);
			oddly_2.b.dq_wr_dly = info_buf->dfi_bkup.dfi_dbit_bkup[i].dq_wr_dly;
			dfi_writel(oddly_2.all, SEL_ODDLY_2);
		}

		ctrl0.all = 0;
		sel_dll_0_t dll_0 = { .all = 0, };
		sel_rd_dly_0_t rd_dly = { .all = 0, };
		sel_strgt_0_t strgt_0 = { .all = 0, };
		sel_oddly_0_t oddly_0 = { .all = 0, };
		sel_oddly_1_t oddly_1 = { .all = 0, };
		sel_oadly_1_t oadly_1 = { .all = 0, };
		sel_oadly_2_t oadly_2 = { .all = 0, };

		for (i = 0; i < DDR_MAX_BYTE; i++) {
			ctrl0.b.sel_dbyte = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);

			dll_0.b.dll_phase0 = info_buf->dfi_bkup.dfi_dyte_bkup[i].dll_phase0;
			dll_0.b.dll_phase1 = info_buf->dfi_bkup.dfi_dyte_bkup[i].dll_phase1;
			dfi_writel(dll_0.all, SEL_DLL_0);

			rd_dly.b.dm_rd_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].dm_rd_dly;
			dfi_writel(rd_dly.all, SEL_RD_DLY_0);

			strgt_0.b.strgt_tap_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].strgt_tap_dly;
			strgt_0.b.strgt_phase_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].strgt_phase_dly;
			dfi_writel(strgt_0.all, SEL_STRGT_0);

			oddly_0.b.dqs_wr_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].dqs_wr_dly;
			dfi_writel(oddly_0.all, SEL_ODDLY_0);

			oddly_1.b.dm_wr_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].dm_wr_dly;
			dfi_writel(oddly_1.all, SEL_ODDLY_1);

			oadly_1.b.ck_wr_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].ck_wr_dly;
			dfi_writel(oadly_1.all, SEL_OADLY_1);

			oadly_2.b.rank_wr_dly = info_buf->dfi_bkup.dfi_dyte_bkup[i].rank_wr_dly;
			oadly_2.b.rank_wr_rsv = info_buf->dfi_bkup.dfi_dyte_bkup[i].rank_wr_rsv;
			dfi_writel(oadly_2.all, SEL_OADLY_2);
		}

		ctrl0.all = 0;
		sel_oadly_0_t oadly_0 = { .all = 0, };
		for (i = 0; i < DDR_MAX_CA_NUM; i++) {
			ctrl0.b.sel_ca = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			oadly_0.b.ca_wr_dly = info_buf->dfi_bkup.dfi_ca_bkup[i].ca_wr_dly;
			dfi_writel(oadly_0.all, SEL_OADLY_0);
		}
	}

	if (bkup == DDR_INFO_MC || bkup == DDR_INFO_ALL) {
		mc0_writel(info_buf->mc_bkup.dev_cfg_training.all, DEVICE_CONFIG_TRAINING);
		mc0_writel(info_buf->mc_bkup.dram_timing_offspec.all, DRAM_TIMING_OFFSPEC);
		mc0_writel(info_buf->mc_bkup.dfi_phy_cntl1.all, DFI_PHY_CNTL1);
		mc0_writel(info_buf->mc_bkup.dfi_phy_cntl3.all, DFI_PHY_CNTL3);
		mc0_writel(info_buf->mc_bkup.interrupt_cntl0.all, INTERRUPT_CNTL0);
	}
}

fast_code void ddr_info_dump(enum ddr_info_bkup_t bkup, ddr_info_t* info_buf)
{
	u32 i = 0;
	//u32 addr;

	if (bkup == DDR_INFO_DFI || bkup == DDR_INFO_ALL) {
		rtos_ddr_trace(LOG_ERR, 0x5f33, "info_buf->dfi_bkup.data0.all = 0x%x;\n",info_buf->dfi_bkup.data0.all);
		rtos_ddr_trace(LOG_ERR, 0x3723, "info_buf->dfi_bkup.data1.all = 0x%x;\n",info_buf->dfi_bkup.data1.all);
		rtos_ddr_trace(LOG_ERR, 0xce0b, "info_buf->dfi_bkup.data2.all = 0x%x;\n",info_buf->dfi_bkup.data2.all);
		rtos_ddr_trace(LOG_ERR, 0x048c, "info_buf->dfi_bkup.adcm0.all = 0x%x;\n",info_buf->dfi_bkup.adcm0.all);
		rtos_ddr_trace(LOG_ERR, 0xca42, "info_buf->dfi_bkup.adcm1.all= 0x%x;\n", info_buf->dfi_bkup.adcm1.all);
		rtos_ddr_trace(LOG_ERR, 0x82a5, "info_buf->dfi_bkup.adcm2.all = 0x%x;\n", info_buf->dfi_bkup.adcm2.all);
		rtos_ddr_trace(LOG_ERR, 0x603f, "info_buf->dfi_bkup.adcm3 = 0x%x;\n",info_buf->dfi_bkup.adcm3.all);
		rtos_ddr_trace(LOG_ERR, 0xfe9a, "info_buf->dfi_bkup.ck0.all = 0x%x;\n",info_buf->dfi_bkup.ck0.all);
		rtos_ddr_trace(LOG_ERR, 0x7dc0, "info_buf->dfi_bkup.alert.all = 0x%x;\n", info_buf->dfi_bkup.alert.all);
		rtos_ddr_trace(LOG_ERR, 0xe89c, "info_buf->dfi_bkup.resetn.all = 0x%x;\n",info_buf->dfi_bkup.resetn.all);
		rtos_ddr_trace(LOG_ERR, 0xde28, "info_buf->dfi_bkup.cal0.all = 0x%x;\n",info_buf->dfi_bkup.cal0.all);
		rtos_ddr_trace(LOG_ERR, 0xc706, "info_buf->dfi_bkup.cal1.all = 0x%x;\n",info_buf->dfi_bkup.cal1.all);
		rtos_ddr_trace(LOG_ERR, 0x4bfd, "info_buf->dfi_bkup.vgen_ctrl0.all = 0x%x;\n", info_buf->dfi_bkup.vgen_ctrl0.all);
		rtos_ddr_trace(LOG_ERR, 0x8e95, "info_buf->dfi_bkup.sync_ctrl.all = 0x%x;\n", info_buf->dfi_bkup.sync_ctrl.all);
		rtos_ddr_trace(LOG_ERR, 0x558f, "info_buf->dfi_bkup.dfi_ctrl.all = 0x%x;\n", info_buf->dfi_bkup.dfi_ctrl.all);
		rtos_ddr_trace(LOG_ERR, 0xd77f, "info_buf->dfi_bkup.out_ctrl0.all = 0x%x;\n", info_buf->dfi_bkup.out_ctrl0.all);
		rtos_ddr_trace(LOG_ERR, 0x7281, "info_buf->dfi_bkup.out_ctrl1.all = 0x%x;\n", info_buf->dfi_bkup.out_ctrl1.all);
		rtos_ddr_trace(LOG_ERR, 0x1cdd, "info_buf->dfi_bkup.in_ctrl0.all = 0x%x;\n",info_buf->dfi_bkup.in_ctrl0.all);
		rtos_ddr_trace(LOG_ERR, 0xc426, "info_buf->dfi_bkup.ck_gate.all = 0x%x;\n", info_buf->dfi_bkup.ck_gate.all);
		rtos_ddr_trace(LOG_ERR, 0x7571, "info_buf->dfi_bkup.lvl_all_ctrl0.all = 0x%x;\n", info_buf->dfi_bkup.lvl_all_ctrl0.all);
		rtos_ddr_trace(LOG_ERR, 0x7086, "info_buf->dfi_bkup.dll_ctrl1.all = 0x%x;\n", info_buf->dfi_bkup.dll_ctrl1.all);
		rtos_ddr_trace(LOG_ERR, 0xc32b, "info_buf->dfi_bkup.strgt_ctrl0.all = 0x%x;\n", info_buf->dfi_bkup.strgt_ctrl0.all);
		rtos_ddr_trace(LOG_ERR, 0x85f3, "info_buf->dfi_bkup.oadly0.all = 0x%x;\n",info_buf->dfi_bkup.oadly0.all);
		rtos_ddr_trace(LOG_ERR, 0xd2fe, "info_buf->dfi_bkup.lp_ctrl0.all = 0x%x;\n", info_buf->dfi_bkup.lp_ctrl0.all);

		sel_rd_dly_1_t rd_dly_1 = { .all = 0, };
		sel_oddly_2_t oddly_2 = { .all = 0, };
		for (i = 0; i < DDR_MAX_BIT; i++) {
			rd_dly_1.b.dq_rd_dly = info_buf->dfi_bkup.dfi_dbit_bkup[i].sel_rd_dly_1;
			rtos_ddr_trace(LOG_ERR, 0xf523, "info_buf->dfi_bkup.dfi_dbit_bkup[%d].sel_rd_dly_1 = 0x%x;\n", i, rd_dly_1.all);
			oddly_2.b.dq_wr_dly = info_buf->dfi_bkup.dfi_dbit_bkup[i].dq_wr_dly;
			rtos_ddr_trace(LOG_ERR, 0xc3e6, "info_buf->dfi_bkup.dfi_dbit_bkup[%d].dq_wr_dly = 0x%x;\n",i, oddly_2.all);
		}

		for (i = 0; i < DDR_MAX_BYTE; i++) {
			rtos_ddr_trace(LOG_ERR, 0x8d78, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].dll_phase0 = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].dll_phase0);
			rtos_ddr_trace(LOG_ERR, 0x85b0, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].dll_phase1 = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].dll_phase1);
			rtos_ddr_trace(LOG_ERR, 0x7507, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].dm_rd_dly = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].dm_rd_dly);
			rtos_ddr_trace(LOG_ERR, 0xde36, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].strgt_tap_dly= 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].strgt_tap_dly) ;
			rtos_ddr_trace(LOG_ERR, 0x4968, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].strgt_phase_dly = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].strgt_phase_dly);
			rtos_ddr_trace(LOG_ERR, 0xa49a, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].dqs_wr_dly = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].dqs_wr_dly);
			rtos_ddr_trace(LOG_ERR, 0x8fb9, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].dm_wr_dly = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].dm_wr_dly);
			rtos_ddr_trace(LOG_ERR, 0xa8bc, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].ck_wr_dly = 0x%x;\n",i, info_buf->dfi_bkup.dfi_dyte_bkup[i].ck_wr_dly);
			rtos_ddr_trace(LOG_ERR, 0xf89b, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].rank_wr_dly = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].rank_wr_dly);
			rtos_ddr_trace(LOG_ERR, 0xdd4e, "info_buf->dfi_bkup.dfi_dyte_bkup[%d].rank_wr_rsv = 0x%x;\n", i, info_buf->dfi_bkup.dfi_dyte_bkup[i].rank_wr_rsv);
		}

		for (i = 0; i < DDR_MAX_CA_NUM; i++) {
			rtos_ddr_trace(LOG_ERR, 0x6aab, "info_buf->dfi_bkup.dfi_ca_bkup[%d].ca_wr_dly = 0x%x;\n", i, info_buf->dfi_bkup.dfi_ca_bkup[i].ca_wr_dly);
		}
		/*
		for (i = 0; i < 0x400; i=i+4) {
			addr = 0xC0064000 + i;
			rtos_ddr_trace(LOG_ERR, 0, "addr %x: %x\n", addr, dfi_readl(i));
		}*/
	}

	if (bkup == DDR_INFO_MC || bkup == DDR_INFO_ALL) {
		rtos_ddr_trace(LOG_ERR, 0x230a, "info_buf->mc_bkup.dev_cfg_training.all = 0x%x;\n", info_buf->mc_bkup.dev_cfg_training.all);
		rtos_ddr_trace(LOG_ERR, 0x3a10, "info_buf->mc_bkup.dram_timing_offspec.all = 0x%x;\n", info_buf->mc_bkup.dram_timing_offspec.all);
		rtos_ddr_trace(LOG_ERR, 0x8757, "info_buf->mc_bkup.dfi_phy_cntl1.all = 0x%x;\n", info_buf->mc_bkup.dfi_phy_cntl1.all);
		rtos_ddr_trace(LOG_ERR, 0x6ad1, "info_buf->mc_bkup.dfi_phy_cntl3.all = 0x%x;\n", info_buf->mc_bkup.dfi_phy_cntl3.all);
		rtos_ddr_trace(LOG_ERR, 0x7183, "info_buf->mc_bkup.interrupt_cntl0.all = 0x%x;\n", info_buf->mc_bkup.interrupt_cntl0.all);
		/*
		for (i = 0; i < 0x400; i=i+4) {
			addr = 0xC0060000 + i;
			rtos_ddr_trace(LOG_ERR, 0, "addr %x: %x\n", addr, mc0_readl(i));
		}
		*/
	}
}

norm_ps_code void pmu_ddr_restore(enum sleep_mode_t mode)
{
#if defined(LPDDR4)
	// DDR(in PD0) remains in self-refresh, content is not lost
	if (SLEEP_MODE_3 == mode) {
		dfi_dll_rst();				// reset first, and let it stable
		ndelay(1500);				// tVREFDQE >= 150ns
		dfi_sync();
		dfi_dll_update();
	} else { // DDR power & content is lost
		dfi_dll_rst();				// reset first, and let it stable
		ddr_info_restore(DDR_INFO_DFI, &ddr_info_buf);		// restore dfi
		dfi_sync();
		dfi_dll_update();

		// First backup original target speed
		u16 target_speed = mc_get_target_speed();
		// This will initialize DDR @ low speed (800mbps) to guarantee DDR operation
		mc_init();

		ddr_info_restore(DDR_INFO_MC, &ddr_info_buf);		// restore mc

		// Write hi-speed settings to FSP1 and switch to high speed
		dfi_switch_fsp1(target_speed);

		dfi_rdlvl_rdlat_seq(0, 10, 0);
	}
#else
	// DDR(in PD0) remains in self-refresh, content is not lost
	if (SLEEP_MODE_3 == mode) {
		dfi_dll_rst();				// reset first, and let it stable
		ndelay(1500);				// tVREFDQE >= 150ns
		dfi_sync();
		dfi_dll_update();
	} else { // DDR power & content is lost
		dfi_dll_rst();				// reset first, and let it stable
		ddr_info_restore(DDR_INFO_DFI, &ddr_info_buf);		// restore dfi
		dfi_sync();
		dfi_dll_update();

		mc_init();

		ddr_info_restore(DDR_INFO_MC, &ddr_info_buf);		// restore mc

		// After DRAM init, program previously-trained VREF to DRAM
		mc_vref_en(1);				// enable VREF training
		ndelay(150);				// tVREFDQE >= 150ns
		mc_vref_en(0);				// disable VREF training

        mdelay(10);
		//workaround to run m.2 at 2400MHz
		dfi_rdlvl_rdlat_seq(0, 10, 0);
	}
#endif
	ddr_ecc_enable();
}

norm_ps_code bool ddr_suspend(enum sleep_mode_t mode)
{
	if (SLEEP_MODE_3 == mode) {
		mc_self_refresh(true);
	}
	return true;
}

norm_ps_code void ddr_resume(enum sleep_mode_t mode)
{
	pmu_ddr_restore(mode);

	if (SLEEP_MODE_3 == mode)
		mc_self_refresh(false);

	return;
}

ddr_code bool ddr_suspend_test(void)
{
	return ddr_suspend(SLEEP_MODE_4);
}

ddr_code void ddr_resume_test(void)
{
	ddr_resume(SLEEP_MODE_4);
}

ddr_code int ddr_suspend_test_main(int argc, char* argv[])
{
	ddr_suspend_test();
	return 0;
}

ddr_code int ddr_resume_test_main(int argc, char* argv[])
{
	ddr_resume_test();
	return 0;
}


static DEFINE_UART_CMD(ddr_suspend_test, "ddr_suspend_test",
	"ddr_suspend_test",
	"ddr_suspend_test",
	0, 0, ddr_suspend_test_main);

static DEFINE_UART_CMD(ddr_resume_test, "ddr_resume_test",
	"ddr_resume_test()",
	"ddr_resume_test()",
	0, 0, ddr_resume_test_main);
#else
bool __attribute__((weak, alias("__ddr_suspend"))) ddr_suspend(enum sleep_mode_t mode);
void __attribute__((weak, alias("__ddr_resume"))) ddr_resume(enum sleep_mode_t mode);
void __attribute__((weak, alias("_ddr_info_bkup"))) ddr_info_bkup(enum ddr_info_bkup_t bkup, ddr_info_t* info_buf);
#endif

norm_ps_code void ddr_irq_enable(void)
{
	mc0_writel(PECC_1BIT_THRESHOLD, ECC_ERR_INT_THRESHOLD);

	interrupt_cntl0_t cntl0 = { .all = mc0_readl(INTERRUPT_CNTL0) };
	interrupt_status_t sts = { .all = mc0_readl(INTERRUPT_STATUS) };
	mc0_writel(sts.all, INTERRUPT_STATUS);
	//rtos_core_trace(LOG_DEBUG, 0, "ddr int sts old %x new %x", sts.all, mc0_readl(INTERRUPT_STATUS));

	cntl0.b.pc_overflow_int_enb = 1;
	cntl0.b.addr_decode_err_int_enb = 1;
	cntl0.b.sram_parity_err_int_enb = 0;		// reserved
	cntl0.b.err_fifo_int_enb = 1;
	cntl0.b.axi_p0_wdata_parity_err_int_enb = 1;
	cntl0.b.axi_p1_wdata_parity_err_int_enb = 1;
	cntl0.b.axi_p2_wdata_parity_err_int_enb =1;
	cntl0.b.ecc_err_int_enb = 1;
	cntl0.b.crc_ca_parity_err_int_enb = 0;		// not used
	cntl0.b.inline_ecc_p0_int_enb = 0;		// reserved
	cntl0.b.inline_ecc_p1_int_enb = 0;		// reserved
	cntl0.b.inline_ecc_p2_int_enb = 0;		// reserved
	cntl0.b.global_int_enb = 1;

	mc0_writel(cntl0.all, INTERRUPT_CNTL0);
}

fast_code NOINLINE void ddr_err_fifo(void)
{
	err_fifo_cntl0_t err_fifo_cntl0 = { .all = mc0_readl(ERR_FIFO_CNTL0) };
	rtos_core_trace(LOG_ERR, 0x0e4d, "ddr err fifo cnt %d", err_fifo_cntl0.b.err_fifo_err_count);

	while (err_fifo_cntl0.b.err_fifo_err_count) {
		u32 s[4];
		u32 i;
		u32 err_type = 0;

		for (i = 0; i < 4; i++)
			s[i] = mc0_readl(ERR_FIFO_STAT0 + i * 4);

		err_type = (s[0] & ERR_FIFO_TYPE_MASK);
		if ((err_type == 2) || (err_type == 3)) {
			rtos_core_trace(LOG_ERR, 0x34c3, "pecc in err fifo %x %x %x %x", s[0], s[1], s[2], s[3]);
		} else if ((err_type >= 6) && (err_type <= 9)) {
			rtos_core_trace(LOG_ERR, 0x91c4, "iecc in err fifo %x %x %x %x", s[0], s[1], s[2], s[3]);
		} else {
			rtos_core_trace(LOG_ERR, 0x99ea, "err fifo %x %x %x %x", s[0], s[1], s[2], s[3]);
		}

		err_fifo_cntl0.b.err_fifo_pop = 1;
		mc0_writel(err_fifo_cntl0.all, ERR_FIFO_CNTL0);

		err_fifo_cntl0.all = mc0_readl(ERR_FIFO_CNTL0);
	}
}

fast_code void ddr_irq(void)
{
	interrupt_status_t status = { .all = mc0_readl(INTERRUPT_STATUS) };
	bool stop = false;
	bool report = false;

	rtos_core_trace(LOG_INFO, 0x7349, "ddr int %x", status.all);

	if (status.b.err_fifo_int) {
		ddr_err_fifo();
		//stop = true;
		//report = true;
	}

	if (status.b.addr_decode_err_int) {
		rtos_core_trace(LOG_ERR, 0x5deb, "ddr addr dec err");
		stop = true;
		report = true;
	}

	if (status.b.axi_p0_wdata_parity_err_int ||
			status.b.axi_p1_wdata_parity_err_int ||
			status.b.axi_p2_wdata_parity_err_int) {
		rtos_core_trace(LOG_ERR, 0xf6f3, "axi wdata par err %d/%d/%d",
				status.b.axi_p0_wdata_parity_err_int,
				status.b.axi_p1_wdata_parity_err_int,
				status.b.axi_p2_wdata_parity_err_int);
		stop = true;
		report = true;
	}

	if (status.b.ecc_err_int)
	{
		ecc_err_count_status_1_t status1 = {. all = mc0_readl(ECC_ERR_COUNT_STATUS_1) };
		if (status1.b.ecc_2bit_err_count > 0)
		{
			tx_smart_stat->dram_error_count[0]++; //increases by 1 when Enter interrupt and have 2 bit error
			tx_smart_stat->dram_error_count[2] += status1.b.ecc_2bit_err_count; //2bit error count
		}
		tx_smart_stat->dram_error_count[1] += mc0_readl(ECC_ERR_COUNT_STATUS); //1 bit error count
		rtos_core_trace(LOG_ERR, 0xc243, "1bit err cnt %d", mc0_readl(ECC_ERR_COUNT_STATUS));
		rtos_core_trace(LOG_ERR, 0x1b0e, "2bit err cnt %d", status1.b.ecc_2bit_err_count);
		mc0_writel(ECC_2BIT_ERR_COUNT_CLR_MASK | ECC_1BIT_ERR_COUNT_CLR_MASK, ECC_ERR_COUNT_STATUS_1);
		//stop = true;

		if (status1.b.ecc_2bit_err_count)
		{
			extern u16 Temp_CMD0_Update;
			dram_timing_ref0_t ref0 = {.all = mc0_readl(DRAM_TIMING_REF0),};

			rtos_core_trace(LOG_ALW, 0x3758, "DDR_IRQ_CurrentTemp: %d", Temp_CMD0_Update-273);
			if(ref0.b.trefi == DDR_REFRESH_TIME_7p8_us)
			{
				rtos_core_trace(LOG_ALW, 0x2fb7, "DDR_IRQ_CurrentRefreshTime = 7p8_us");
			}
			if(ref0.b.trefi == DDR_REFRESH_TIME_3p9_us)
			{
				rtos_core_trace(LOG_ALW, 0x439d, "DDR_IRQ_CurrentRefreshTime = 3p9_us");
			}
			report = true;
            flush_to_nand(EVT_2BIT_DDR_ERR);
        }
	}

	if (status.b.crc_ca_parity_err_int) {
		//rtos_core_trace(LOG_DEBUG, 0, "crc ca par err");
		// a0 fpga default ALERT_n pin be low, so may always be triggered
		// stop = true;
		report = true;
	}

	if(report == true){
		extern void nvmet_evt_aer_in();
		nvmet_evt_aer_in(((NVME_EVENT_TYPE_ERROR_STATUS << 16)|ERR_STS_PERSISTENT_INTERNAL_DEV_ERR),DRAM_ERR);
	}
	sys_assert(stop == false);
	mc0_writel(status.all, INTERRUPT_STATUS);
}

init_code void ddr_set_capacity(u64 size)
{
	ddr_capacity = size;
}

/*!
 * @brief get ddr capacity
 *
 * @return	ddr capacity in bytes
 */
fast_code u64 ddr_get_capapcity(void)
{
	u64 ddr_size = ddr_capacity;
#if defined(ENABLE_INLINE_ECC)
	//ddr inline ecc with 512b/12b parity ratio ( * 31 / 32)
	ddr_size = (ddr_size >> 5) * 31;
#endif
	return ddr_size;
}

/*!
 * @brief get ddr info buffer
 *
 * @return	ddr pointer of ddr info buffer
 */
fast_code void* get_ddr_info_buf(void)
{
	return (void*) &ddr_info_buf;
}

/*!
 * @brief is ddr training done
 *
 * @return	true if ddr training done
 */
fast_code bool is_ddr_training_done(void)
{
	return ddr_info_buf.cfg.training_done;
}

/*!
 * @brief set ddr bkup fwconfig done
 *
 * @return	not used
 */
fast_code void set_ddr_info_bkup_fwconfig_done(void)
{
	ddr_info_buf.cfg.bkup_fwconfig_done = true;
}

/*!
 * @brief get fwconfig board type pointer
 *
 * @return	return fwconfig board pointer if it was valid
 */
init_code board_cfg_t *fwconfig_get_board(void)
{
	#ifdef GET_CFG	//20200822-Eddie
		fw_config_set_t *fwconfig = fw_config_main;
	#else	
	fw_config_set_t *fwconfig = (fw_config_set_t *) &__dtag_fwconfig_start;
	#endif
	//rtos_ddr_trace(LOG_ERR, 0, "[FWCFIG] fwconfig->header.signature : 0x%x \n", fwconfig->header.signature);
	if (fwconfig->header.signature == IMAGE_CONFIG)
		return &fwconfig->board;
	else
		return NULL;

}

/*!
 * @brief get ddr info from fwconfig board type
 *
 * @return	return treu if get
 */
init_code bool fwconfig_get_ddr_info(ddr_info_t* info_buf)
{
	sys_assert(info_buf != NULL);
	bool get = false;
	board_cfg_t *board = fwconfig_get_board();

	if (board != NULL) {
		memcpy(info_buf, (void*) board->ddr_info, sizeof(ddr_info_t));
		//rtos_ddr_trace(LOG_ERR, 0, "[FWCFIG] training_done : %x, bkup_fwconfig_done : %x",info_buf->cfg.training_done,info_buf->cfg.bkup_fwconfig_done);
		if (info_buf->cfg.training_done && info_buf->cfg.bkup_fwconfig_done) {
			get = true;
		}
	}

	return get;
}

#if defined(PROGRAMMER)
fast_code void ddr_train(ddr_cfg_type_t type, ddr_cfg_speed_t speed, ddr_cfg_size_t size)
{
	int ret;
	int freq[] = {800, 1600, 2400, 2666, 3200};
	int rainier_freq = freq[speed];

	rtos_ddr_trace(LOG_ERR, 0xb99b, "ddr train t:%d, clk:%d, sz:%d\n", type, rainier_freq, (1 << size) << 8);

	memset((void*) &ddr_info_buf, 0, sizeof(ddr_info_t));
	rtos_ddr_trace(LOG_ERR, 0x0dca, "ddr_info_buf %x\n", &ddr_info_buf);

#if defined(LPDDR4)
	ret = dfi_train_all_lpddr4(rainier_freq, 0, 0);
	rtos_ddr_trace(LOG_ERR, 0xeff4, "\nLPDDR4 Training Done %d !\n", ret);
	if (ret != 0)
		ret = dfi_train_all_lpddr4(rainier_freq, 0, 3);
#else
	// DDR 4
	ret = dfi_train_all(rainier_freq, 0, 0);
	rtos_ddr_trace(LOG_ERR, 0xfb3f, "\nDDR4 Training Done %d !\n", ret);
	if (ret != 0)
		ret = dfi_train_all(rainier_freq, 0, 3);
#endif

	sys_assert(ret == 0);
	ddr_info_buf.cfg.training_done = true;
	ddr_info_bkup(DDR_INFO_ALL, &ddr_info_buf);
}
#endif

#ifdef GET_CFG	//20200822-Eddie
init_code void Get_DDR_Size_From_LOADER_CFG(void)
{
	u64 size = 0;

#ifdef DDR_AUTOIZE_IN_SRB
	srb_t *srb = (srb_t *) SRAM_BASE;
#endif

	if (fw_config_main->header.signature == IMAGE_CONFIG){
		size = (u64)((1 << fw_config_main->board.ddr_size) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
		rtos_ddr_trace(LOG_INFO, 0x95e2, "DDR size : %d \n",(1 << fw_config_main->board.ddr_size) << 8);
		size = size << 20; // bytes
	}
#ifdef DDR_AUTOIZE_IN_SRB
	else if ((u8)srb->ddr_idx){		//20201028-Eddie
		size =(u64)((1 << (u8)srb->ddr_idx) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
		size = size << 20; // bytes
	}
#endif
	else
	{
		size = DDR_SIZE_RAINIER;   // 1024
		rtos_ddr_trace(LOG_ERR, 0x3718, "No DDR size data, set to default : %d \n", (size>>20));
	}

	ddr_set_capacity(size);
}
#endif
#if defined(DRAM_Error_injection)
void Error_injection_1bit(void)
{   
    ras_err_test_t cntl = {.all = mc0_readl(RAS_ERR_TEST)};
    interrupt_status_t status = { .all = mc0_readl(INTERRUPT_STATUS) };
    cntl.b.force_ecc_err_inject_enb = 1;
    cntl.b.force_ecc_err_inject_sel = 0;
    mc0_writel(cntl.all, RAS_ERR_TEST);
    u32* err_test;
    err_test=(u32*)0x43100000;
    *err_test=170;
    rtos_core_trace(LOG_ALW, 0xb87f, "Ecc_err_int:%d",status.b.ecc_err_int);
    rtos_core_trace(LOG_ALW, 0xbb83, "1 BIT ERROR:%x",*err_test);
    
}
void Error_injection_2bit(void)
{
    ras_err_test_t cntl = {.all = mc0_readl(RAS_ERR_TEST)};
    interrupt_status_t status = { .all = mc0_readl(INTERRUPT_STATUS) };
    cntl.b.force_ecc_err_inject_enb = 1;
    cntl.b.force_ecc_err_inject_sel = 1;
    mc0_writel(cntl.all, RAS_ERR_TEST);  
    u32* err_test;
    err_test=(u32*)0x43000000;
    *err_test=170;
    rtos_core_trace(LOG_ALW, 0xb4c7, "Ecc_err_int:%d",status.b.ecc_err_int);
    rtos_core_trace(LOG_ALW, 0x0439, "2 BIT ERROR:%x",*err_test);
}
#endif


/*!
 * @brief initialize ddr, start ddr training and create ddr dtag pool
 *
 * @return	not used
 */
 #define DDR_AUTOIZE_IN_SRB	//20201005-Eddie
init_code void ddr_init(void)
{

rtos_ddr_trace(LOG_ERR, 0x5c85, "DDR init in MAINCODE\n");

#ifdef DDR_AUTOIZE_IN_SRB
	srb_t *srb = (srb_t *) SRAM_BASE;
#endif

  #ifdef SAVE_DDR_CFG	//20201008-Eddie
 	need_save_to_CTQ = false;
  #endif
#ifdef FPGA
	if (ddr_inited)
		return;

	ddr_inited = 1;

	if (soc_cfg_reg1.b.ddr == 0x1)
		ddr3_h2k_init();
	else if (soc_cfg_reg1.b.ddr == 0x2)
		ddr4_vu440_init();
	else
		rtos_arch_trace(LOG_ALW, 0xcf0a, "No DDR");
#else
	//int rainier_freq = 2400;
	int rainier_freq = 3200;
#if defined(LOADER)	//20200720-Eddie
	//rainier_freq = 2400;
	rainier_freq = 3200;
#else
#if defined(M2)
	#if defined(LPDDR4)
		#if defined(M2_2A)
			rainier_freq = 3200;
		#elif defined(M2_0305)
			rainier_freq = 2666;
		#endif
	#else
	rainier_freq = 2400;
	#endif
#elif defined(U2)
		#if defined(U2_0504)
			rainier_freq = 2400;
		#elif defined(U2_LJ)
			//rainier_freq = 2400;
			rainier_freq = 3200;
		#endif
#elif defined(EVB_0501)
	rainier_freq = 1600;
#endif
    #endif
	memset((void*) &ddr_info_buf, 0, sizeof(ddr_info_t));
	rtos_ddr_trace(LOG_ERR, 0xf27c, "ddr_info_buf %x\n", &ddr_info_buf);
	bool ddr_info_valid = false;
    #if defined(LOADER)
    dfi_train_result_init();
    #endif

#if defined(LOADER)
	//memprint("in ddr init",fw_config,4096);
	memcpy(get_ddr_info_buf(), fw_config->board.ddr_info, sizeof(ddr_info_t));	//Copy ddr info from fw_config to ddr_info_buf
	//memprint(&ddr_info_buf,320);
	ddr_info_valid = (ddr_info_buf.cfg.training_done && ddr_info_buf.cfg.bkup_fwconfig_done);
	rtos_ddr_trace(LOG_ERR, 0x8e9f, "ddr_info_valid %d \n",ddr_info_valid);
#else
	ddr_info_valid = fwconfig_get_ddr_info(&ddr_info_buf);
#endif
	dpe_early_init();

#if 1
    #if defined(LOADER)
        dfi_dram_size_related_init((u32)((1 << fw_config->board.ddr_size) << 8));
        rtos_ddr_trace(LOG_ERR, 0xd73d, "dfi_dram_size_related_init done ! \n");
    #else
        board_cfg_t *fw_board = fwconfig_get_board();
		u32 ddr_size = (1 << fw_board->ddr_size) << 8; // MB
        dfi_dram_size_related_init(ddr_size);
        rtos_ddr_trace(LOG_ERR, 0x4c57, "dfi_dram_size_related_init done ! \n");
    #endif
#else
#endif


	if (!ddr_info_valid) {
#ifndef MDOT2_SUPPORT
		extern void poweron_ddr(); //No regulator in PJ1, WillWu 2023.6.5
		poweron_ddr();
#endif
                rtos_ddr_trace(LOG_ERR, 0x2814, "Invalid DDR tag, do DDR calibration !\n");
		//rainier_freq = 2400;
        rainier_freq = 3200;
	#if defined(LOADER)	//This flag indicate DDR calibration need to do again (VU cmd cleared), it doesn't mean do calibration in Loader originally.
		int freq[] = {800, 1600, 2400, 2666, 3200};
		u64 size;
		int clk;
		size =(u64)((1 << fw_config->board.ddr_size) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
		clk = (int)fw_config->board.ddr_clk;
		size = size << 20; // bytes
		ddr_set_capacity(size);
		rainier_freq = freq[clk];
		rtos_ddr_trace(LOG_ERR, 0x713a, "size from cfg %d, clk %d \n", (1 << fw_config->board.ddr_size) << 8 ,rainier_freq);
		//ddr_set_capacity(DDR_SIZE_DEFAULT);
	#else	//20200922-Eddie
		int freq[] = {800, 1600, 2400, 2666, 3200};
		u64 size;
		int clk;
		if (fw_config_main->header.signature == IMAGE_CONFIG){
			size =(u64)((1 << fw_config_main->board.ddr_size) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
			clk = (int)fw_config_main->board.ddr_clk;
			size = size << 20; // bytes
			ddr_set_capacity(size);
			rainier_freq = freq[clk];
			rtos_ddr_trace(LOG_ERR, 0x485e, "DDR size from cfg %d, from srb %d clk %d \n",(1 << fw_config_main->board.ddr_size) << 8 ,(1 << (u8)srb->ddr_idx) << 8 ,rainier_freq);
		}
		else{
			#ifdef DDR_AUTOIZE_IN_SRB	//20201005-Eddie
				if ((u8)srb->ddr_idx){
					size =(u64)((1 << (u8)srb->ddr_idx) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
					size = size << 20; // bytes
					ddr_set_capacity(size);
				}
				else
				ddr_set_capacity(DDR_SIZE_RAINIER);
			#else
				ddr_set_capacity(DDR_SIZE_RAINIER);
			#endif
			clk = 4; //3200
			rainier_freq = freq[clk];
			rtos_ddr_trace(LOG_ERR, 0x25ff, "DDR size from srb %d, clk %d \n",(1 << (u8)srb->ddr_idx) << 8 ,rainier_freq);
		}
	#endif
		dfi_dpe_copy((void*) 0x20000000, (void*) 0x20000000, 512); // dummy 512B dpe command SRAM to SRAM, to initial dpe buffer
		#if defined(LPDDR4)
			int ret = dfi_train_all_lpddr4(rainier_freq, 0, 0);
			rtos_ddr_trace(LOG_ERR, 0xa3e3, "\nLPDDR4 Training Done %d !\n", ret);
			if (ret != 0)
				ret = dfi_train_all_lpddr4(rainier_freq, 0, 3);
		#else //Default to DDR4
			int ret = dfi_train_all(rainier_freq, 0, 0);
			rtos_ddr_trace(LOG_ERR, 0x17d7, "\nDDR4 Training Done %d !\n", ret);
			if (ret != 0)
				ret = dfi_train_all(rainier_freq, 0, 3);
		#endif

		sys_assert(ret == 0);
		ddr_info_buf.cfg.training_done = true;
		ddr_info_bkup(DDR_INFO_ALL, &ddr_info_buf);
		ddr_ecc_enable();
		set_ddr_info_bkup_fwconfig_done();

	        #ifdef SAVE_DDR_CFG		//20201008-Eddie
		 	need_save_to_CTQ = true;
		 #endif
	}
        else {
	#if defined(LOADER)
		int freq[] = {800, 1600, 2400, 2666, 3200};
		u64 size;
		int clk;
		if (fw_config->header.signature == IMAGE_CONFIG){
			size = (u64)((1 << fw_config->board.ddr_size) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
			rtos_ddr_trace(LOG_ERR, 0xf2ed, "DDR size %d \n",size);
			clk = (int)fw_config->board.ddr_clk;
		}
		else{
			size = 1 << 10; // 1024
			clk = 1; //1600
		}
		size = size << 20; // bytes
		ddr_set_capacity(size);
		rainier_freq = freq[clk];
		rtos_ddr_trace(LOG_ERR, 0xaac2, "size from cfg %d, clk %d \n",(1 << fw_config->board.ddr_size) << 8 ,rainier_freq);
	#else
		int freq[] = {800, 1600, 2400, 2666, 3200};
		u64 size;
		int clk;
		if (fw_config_main->header.signature == IMAGE_CONFIG){
			size =(u64)((1 << fw_config_main->board.ddr_size) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
			clk = (int)fw_config_main->board.ddr_clk;
			size = size << 20; // bytes
			ddr_set_capacity(size);
			rainier_freq = freq[clk];
			rtos_ddr_trace(LOG_ERR, 0x3f86, "DDR size from cfg %d, from srb %d clk %d \n",(1 << fw_config_main->board.ddr_size) << 8 ,(1 << (u8)srb->ddr_idx) << 8 ,rainier_freq);
		}
		else{
			#ifdef DDR_AUTOIZE_IN_SRB	//20201005-Eddie
				if ((u8)srb->ddr_idx){
					size =(u64)((1 << (u8)srb->ddr_idx) << 8); // MB	//0:256, 1:512, 2:1024, 3:2048, 4:4096, 5:8192, 6:16384
					size = size << 20; // bytes
					ddr_set_capacity(size);
				}
				else
				ddr_set_capacity(DDR_SIZE_RAINIER);
			#else
				ddr_set_capacity(DDR_SIZE_RAINIER);
			#endif
			clk = 4; //3200
			rainier_freq = freq[clk];
			rtos_ddr_trace(LOG_ERR, 0xd6e0, "DDR size from srb %d, clk %d \n",(1 << (u8)srb->ddr_idx) << 8 ,rainier_freq);
		}

	#endif

#if defined(DDR)
		dfi_set_pll_freq(rainier_freq, true);
		dfi_phy_init();
		ddr_resume(SLEEP_MODE_4);    //20200720-Eddie-Here to restore calibration para. ddr_ecc_eable here
		#if defined(LPDDR4)
			rtos_ddr_trace(LOG_ERR, 0x9c89, "\nLPDDR4 init done from backup ddr info !\n");
		#else //Default to DDR4
			rtos_ddr_trace(LOG_ERR, 0x1677, "\nDDR4 init done from backup ddr info !\n");
		#endif
#else
		// LPDDR4 config train is not ready
		int ret = dfi_train_all_lpddr4(rainier_freq, 0, 0);
		sys_assert(ret == 0);
#endif

	}
#endif
 #if !defined(MOVE_DDRINIT_2_LOADER) //temporarily disabled--need comfirm later..
	extern void *__ddr_dtag_start;
	extern void *__ddr_dtag_end;
#ifdef OFFSET_DDR_DTAG
	extern void *__ddr_sect_start;
	extern void *__ddr_sect_end;
#endif
#ifdef OFFSET_DDR_DTAG
	max_ddr_sect_cnt = ((((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) + (((u32) &__ddr_sect_end) - ((u32) &__ddr_sect_start))) / DTAG_SZE;
	max_ddr_dtag_cnt = (((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) / DTAG_SZE;
#else
	max_ddr_dtag_cnt = (((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) / DTAG_SZE;
#endif

#ifdef EPM_DDTAG_ALLOC	//20210527-Eddie
	ddr_epm_capacity = EPM_ddr_capacity;
	ddr_epm_capacity = ddr_epm_capacity<<20;
#endif

#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
	if ((ddr_capacity>>20) == DDR_4GB)
		ddr_trim_capacity = TRIM_ddr_4T_capacity;
	else	if ((ddr_capacity>>20) == DDR_8GB)
		ddr_trim_capacity = TRIM_ddr_8T_capacity;
	else
		ddr_trim_capacity = TRIM_ddr_4T_capacity;

	ddr_trim_capacity = ddr_trim_capacity<<20;
#endif

#ifdef L2P_DDTAG_ALLOC	//20201029-Eddie
	 if ((ddr_capacity>>20) == DDR_4GB)
		ddr_l2p_capacity = L2P_ddr_4T_capacity;
	else	if ((ddr_capacity>>20) == DDR_8GB)
		ddr_l2p_capacity = L2P_ddr_8T_capacity;
	else
		ddr_l2p_capacity = L2P_ddr_4T_capacity;

	ddr_l2p_capacity = ddr_l2p_capacity<<20;

#ifdef OFFSET_DDR_DTAG
#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
	#ifdef EPM_DDTAG_ALLOC	//20210527-Eddie
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) -( ddr_epm_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_epm = ( ddr_epm_capacity / DTAG_SZE);
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x2b5c, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#else
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x588c, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_sect_cnt;
	ddr_dtag_next = max_ddr_sect_cnt;
	ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
	ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_sect_cnt;
	rtos_ddr_trace(LOG_ERR, 0xa8b9, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
		,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
#endif
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
	ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
	ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_dtag_cnt;
#endif
	rtos_ddr_trace(LOG_ERR, 0xe241, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
		,ddr_dtag_free,ddr_dtag_next,ddr_dtag_free_l2p,ddr_dtag_next_l2p);
	#ifdef L2P_FROM_DDREND
		ddr_dtag_next_from_end = (ddr_get_capapcity() / DTAG_SZE)-1;
		rtos_ddr_trace(LOG_ERR, 0xdd20, "ddr_dtag_next_from_end %d \n",ddr_dtag_next_from_end);
	#endif
#else
#ifdef OFFSET_DDR_DTAG
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - max_ddr_sect_cnt;
	ddr_dtag_next = max_ddr_sect_cnt;
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
#endif
#endif
	rtos_ddr_trace(LOG_ERR, 0x72db, "Total DDTAG Cnt : %d \n",(u32)(ddr_get_capapcity() / DTAG_SZE));

#if DDR_SCAN_TEST
	//mem_scan_test(DDR_BASE, DDR_BASE + ddr_get_capapcity(), "DDR");
	mem_scan_test(DDR_BASE, DDR_BASE + 0x20000, "DDR");
#endif
	l2cache_init(CACHE_LINE_32B, WRITE_BACK_AND_READ_WRITE_ALLOCATE);
#ifdef ENABLE_L2CACHE
	l2cache_enable(true);
#endif
	pmu_register_handler(SUSPEND_COOKIE_DDR, ddr_suspend,
		RESUME_COOKIE_DDR, ddr_resume);

	ddr_irq_enable();
	sirq_register(SYS_VID_MC, ddr_irq, true);
	misc_sys_isr_enable(SYS_VID_MC);
#endif
    fw_config_set_t *fwconfig_temp = (fw_config_set_t*) ddrcfg_buf_rom_addr;
    memcpy((void*)&DRAM_Train_Result, fwconfig_temp->board.dram_train_result, sizeof(tDRAM_Training_Result));

}

#ifdef BYPASS_DDRINIT_WARMBOOT
init_code void ddr_init_bypass_warmboot(void){
#ifdef GET_CFG	//20200822-Eddie
	Get_DDR_Size_From_LOADER_CFG();
#endif

	extern void *__ddr_dtag_start;
	extern void *__ddr_dtag_end;
#ifdef OFFSET_DDR_DTAG
	extern void *__ddr_sect_start;
	extern void *__ddr_sect_end;
#endif
#ifdef OFFSET_DDR_DTAG
	max_ddr_sect_cnt = ((((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) + (((u32) &__ddr_sect_end) - ((u32) &__ddr_sect_start))) / DTAG_SZE;
	max_ddr_dtag_cnt = (((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) / DTAG_SZE;
#else
	max_ddr_dtag_cnt = (((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) / DTAG_SZE;
#endif

#ifdef EPM_DDTAG_ALLOC	//20210527-Eddie
	ddr_epm_capacity = EPM_ddr_capacity;
	ddr_epm_capacity = ddr_epm_capacity<<20;
#endif

#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
	if ((ddr_capacity>>20) == DDR_4GB)
		ddr_trim_capacity = TRIM_ddr_4T_capacity;
	else	if ((ddr_capacity>>20) == DDR_8GB)
		ddr_trim_capacity = TRIM_ddr_8T_capacity;
	else
		ddr_trim_capacity = TRIM_ddr_4T_capacity;

	ddr_trim_capacity = ddr_trim_capacity<<20;
#endif

#ifdef L2P_DDTAG_ALLOC	//20201029-Eddie
 	if ((ddr_capacity>>20) == DDR_4GB)
		ddr_l2p_capacity = L2P_ddr_4T_capacity;
	else	if ((ddr_capacity>>20) == DDR_8GB)
		ddr_l2p_capacity = L2P_ddr_8T_capacity;

	else if ((ddr_capacity>>20) == DDR_2GB){
		ddr_l2p_capacity = L2P_ddr_2T_capacity;
		ddr_cache_cnt = DDR_DTAG_CNT_2T;
	}
	else {
		ddr_l2p_capacity = L2P_ddr_1T_capacity;
		ddr_cache_cnt = DDR_DTAG_CNT_1T;
	}
	
	ddr_l2p_capacity = ddr_l2p_capacity<<20;

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	extern void *__telemetry_start;
	extern void *__telemetry_end;
	ddr_dtag_next_telemetry = mem2ddtag(&__telemetry_start);
	ddr_dtag_free_telemetry = (((u32) &__telemetry_end) - ((u32) &__telemetry_start)) / DTAG_SZE;
#endif

#ifdef OFFSET_DDR_DTAG
#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
	#ifdef EPM_DDTAG_ALLOC	//20210527-Eddie
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) -( ddr_epm_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_epm = ( ddr_epm_capacity / DTAG_SZE);
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xd66d, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#else
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xe91e, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#else
	#ifdef EPM_DDTAG_ALLOC
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - ( ddr_epm_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x76ef, "ddr c = 0x%x, l2p c = 0x%x, epm c = 0x%x, max cnt = %d\n"
								,ddr_get_capapcity(), ddr_l2p_capacity, ddr_epm_capacity, max_ddr_sect_cnt);
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_epm = ( ddr_epm_capacity / DTAG_SZE);
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xe5bf, "DDR SECTION info.__ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
								,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
		rtos_ddr_trace(LOG_ERR, 0xb89f, "DDTAG info. __ddr_dtag_start 0x%x , __ddr_dtag_end 0x%x , max_ddr_dtag_cnt %d \n"
			, (u32) &__ddr_dtag_start, (u32) &__ddr_dtag_end, max_ddr_dtag_cnt);
		rtos_ddr_trace(LOG_ERR, 0xa953, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
								,ddr_dtag_free, ddr_dtag_next, ddr_dtag_free_l2p, ddr_dtag_next_l2p);
		rtos_ddr_trace(LOG_ERR, 0x441f, "DDR DTAG info. ddr_dtag_free_epm %d , ddr_dtag_next_epm %d", ddr_dtag_free_epm, ddr_dtag_next_epm);
	#else 
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x2f42, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#endif	
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
	ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
	ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_dtag_cnt;
#endif	
	rtos_ddr_trace(LOG_ERR, 0x621b, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
		,ddr_dtag_free,ddr_dtag_next,ddr_dtag_free_l2p,ddr_dtag_next_l2p);
	#ifdef L2P_FROM_DDREND
		ddr_dtag_next_from_end = (ddr_get_capapcity() / DTAG_SZE)-1;
		rtos_ddr_trace(LOG_ERR, 0x4c97, "ddr_dtag_next_from_end %d \n",ddr_dtag_next_from_end);
	#endif
#else
#ifdef OFFSET_DDR_DTAG
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - max_ddr_sect_cnt;
	ddr_dtag_next = max_ddr_sect_cnt;
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
#endif	
#endif
	fw_config_set_t *fwconfig_temp = (fw_config_set_t*) ddrcfg_buf_rom_addr;
	rtos_ddr_trace(LOG_INFO, 0x78c5, "[NP-2]  bypass 1 fwconfig_temp->board.ddr_info: %d", fwconfig_temp->board.ddr_info);
	rtos_ddr_trace(LOG_INFO, 0xb936, "[NP-2]  bypass 1 ddr_info_buf.info_need_update: %d", ddr_info_buf.info_need_update);
    memcpy((void*)&DRAM_Train_Result, fwconfig_temp->board.dram_train_result, sizeof(tDRAM_Training_Result));
    memcpy((void*)&ddr_info_buf, fwconfig_temp->board.ddr_info, sizeof(ddr_info_t)); 	
	rtos_ddr_trace(LOG_INFO, 0x7d6d, "[NP-2]  bypass 2 fwconfig_temp->board.ddr_info: %d", fwconfig_temp->board.ddr_info);
	rtos_ddr_trace(LOG_INFO, 0x85a6, "[NP-2]  bypass 2 ddr_info_buf.info_need_update: %d", ddr_info_buf.info_need_update);
}
#endif
 
#ifdef MOVE_DDRINIT_2_LOADER
init_code void ddr_init_complement(void)		//20200727-Eddie--From ddr_init() second half part
{
#ifdef GET_CFG	//20200822-Eddie
	Get_DDR_Size_From_LOADER_CFG();
#endif
 #if 1 //temporarily disabled--need comfirm later..
	extern void *__ddr_dtag_start;
	extern void *__ddr_dtag_end;
#ifdef OFFSET_DDR_DTAG
	extern void *__ddr_sect_start;
	extern void *__ddr_sect_end;
#endif
	
#ifdef OFFSET_DDR_DTAG
	max_ddr_sect_cnt = ((((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) + (((u32) &__ddr_sect_end) - ((u32) &__ddr_sect_start))) / DTAG_SZE;
	max_ddr_dtag_cnt = (((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) / DTAG_SZE;
#else
	max_ddr_dtag_cnt = (((u32) &__ddr_dtag_end) - ((u32) &__ddr_dtag_start)) / DTAG_SZE;
#endif

#ifdef EPM_DDTAG_ALLOC	//20210527-Eddie
	ddr_epm_capacity = EPM_ddr_capacity;
	ddr_epm_capacity = ddr_epm_capacity<<20;
#endif

#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
	if ((ddr_capacity>>20) == DDR_4GB)
		ddr_trim_capacity = TRIM_ddr_4T_capacity;
	else	if ((ddr_capacity>>20) == DDR_8GB)
		ddr_trim_capacity = TRIM_ddr_8T_capacity;
	else
		ddr_trim_capacity = TRIM_ddr_4T_capacity;
	
	ddr_trim_capacity = ddr_trim_capacity<<20;
#endif

#ifdef L2P_DDTAG_ALLOC	//20201029-Eddie
 	if ((ddr_capacity>>20) == DDR_4GB)
		ddr_l2p_capacity = L2P_ddr_4T_capacity;
	else if ((ddr_capacity>>20) == DDR_8GB)
		ddr_l2p_capacity = L2P_ddr_8T_capacity;

	else if ((ddr_capacity>>20) == DDR_2GB){
		ddr_l2p_capacity = L2P_ddr_2T_capacity;
		ddr_cache_cnt = DDR_DTAG_CNT_2T;
	}
	else {
		ddr_l2p_capacity = L2P_ddr_1T_capacity;
		ddr_cache_cnt = DDR_DTAG_CNT_1T;
	}
		

	ddr_l2p_capacity = ddr_l2p_capacity<<20;

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
	extern void *__telemetry_start;
	extern void *__telemetry_end;
	ddr_dtag_next_telemetry = mem2ddtag(&__telemetry_start);
	ddr_dtag_free_telemetry = (((u32) &__telemetry_end) - ((u32) &__telemetry_start)) / DTAG_SZE;
#endif

#ifdef OFFSET_DDR_DTAG
#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
	#ifdef EPM_DDTAG_ALLOC		//20210527-Eddie
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) -( ddr_epm_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_epm = ( ddr_epm_capacity / DTAG_SZE);
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0xee77, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#else
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) -( ddr_trim_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_trim = ( ddr_trim_capacity / DTAG_SZE);
		ddr_dtag_next_trim = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_trim + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x095c, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#else
	#ifdef EPM_DDTAG_ALLOC
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - ( ddr_epm_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x780b, "ddr c = 0x%x, l2p c = 0x%x, epm c = 0x%x, max cnt = %d\n"
								,ddr_get_capapcity(), ddr_l2p_capacity, ddr_epm_capacity, max_ddr_sect_cnt);
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_epm = ( ddr_epm_capacity / DTAG_SZE);
		ddr_dtag_next_epm = ddr_dtag_free + max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + ddr_dtag_free_epm + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x7b91, "DDR SECTION info.__ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
								,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
		rtos_ddr_trace(LOG_ERR, 0x9e71, "DDTAG info. __ddr_dtag_start 0x%x , __ddr_dtag_end 0x%x , max_ddr_dtag_cnt %d \n"
			, (u32) &__ddr_dtag_start, (u32) &__ddr_dtag_end, max_ddr_dtag_cnt);
		rtos_ddr_trace(LOG_ERR, 0xb9bf, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
								,ddr_dtag_free, ddr_dtag_next, ddr_dtag_free_l2p, ddr_dtag_next_l2p);
		rtos_ddr_trace(LOG_ERR, 0x18a7, "DDR DTAG info. ddr_dtag_free_epm %d , ddr_dtag_next_epm %d", ddr_dtag_free_epm, ddr_dtag_next_epm);
	#else 
		ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_sect_cnt;
		ddr_dtag_next = max_ddr_sect_cnt;
		ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
		ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_sect_cnt;
		rtos_ddr_trace(LOG_ERR, 0x8818, "DDR SECTION info. __ddr_sect_start 0x%x , __ddr_sect_end 0x%x , max_ddr_sect_cnt %d \n"
			,(u32) &__ddr_sect_start,(u32) &__ddr_sect_end,max_ddr_sect_cnt);
	#endif
#endif
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - ( ddr_l2p_capacity / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
	ddr_dtag_free_l2p = ( ddr_l2p_capacity / DTAG_SZE);
	ddr_dtag_next_l2p = ddr_dtag_free + max_ddr_dtag_cnt;
#endif
	rtos_ddr_trace(LOG_ERR, 0x3fce, "DDR DTAG info. ddr_dtag_free %d , ddr_dtag_next %d , \n ddr_dtag_free_l2p %d , ddr_dtag_next_l2p %d \n"
		,ddr_dtag_free,ddr_dtag_next,ddr_dtag_free_l2p,ddr_dtag_next_l2p);
	#ifdef L2P_FROM_DDREND
		ddr_dtag_next_from_end = (ddr_get_capapcity() / DTAG_SZE)-1;
		rtos_ddr_trace(LOG_ERR, 0x51de, "ddr_dtag_next_from_end %d \n",ddr_dtag_next_from_end);
	#endif
#else
#ifdef OFFSET_DDR_DTAG
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - max_ddr_sect_cnt;
	ddr_dtag_next = max_ddr_sect_cnt;
#else
	ddr_dtag_free = (ddr_get_capapcity() / DTAG_SZE) - max_ddr_dtag_cnt;
	ddr_dtag_next = max_ddr_dtag_cnt;
#endif
#endif

#if DDR_SCAN_TEST
	//mem_scan_test(DDR_BASE, DDR_BASE + ddr_get_capapcity(), "DDR");
	mem_scan_test(DDR_BASE, DDR_BASE+0x20000, "DDR");
#endif
	l2cache_init(CACHE_LINE_32B, WRITE_BACK_AND_READ_WRITE_ALLOCATE);
#ifdef ENABLE_L2CACHE
	l2cache_enable(true);
#endif
	pmu_register_handler(SUSPEND_COOKIE_DDR, ddr_suspend,
		RESUME_COOKIE_DDR, ddr_resume);

	ddr_irq_enable();
	sirq_register(SYS_VID_MC, ddr_irq, true);
	misc_sys_isr_enable(SYS_VID_MC);
    fw_config_set_t *fwconfig_temp = (fw_config_set_t*) ddrcfg_buf_rom_addr;
	rtos_ddr_trace(LOG_INFO, 0x2994, "[NP-2]  compl 1 fwconfig_temp->board.ddr_info: %d", fwconfig_temp->board.ddr_info);
	rtos_ddr_trace(LOG_INFO, 0xf0d7, "[NP-2]  compl 1 ddr_info_buf.info_need_update: %d", ddr_info_buf.info_need_update);
    memcpy((void*)&DRAM_Train_Result, fwconfig_temp->board.dram_train_result, sizeof(tDRAM_Training_Result));
    memcpy((void*)&ddr_info_buf, fwconfig_temp->board.ddr_info, sizeof(ddr_info_t));    
	rtos_ddr_trace(LOG_INFO, 0x555f, "[NP-2]  compl 2 fwconfig_temp->board.ddr_info: %d", fwconfig_temp->board.ddr_info);
	rtos_ddr_trace(LOG_INFO, 0x8627, "[NP-2]  compl 2 ddr_info_buf.info_need_update: %d", ddr_info_buf.info_need_update);
#endif
}
#endif

#endif

fast_code void ddr_dtag_register_lock(void)
{
	_ddr_dtag_free = ddr_dtag_free;
	rtos_ddr_trace(LOG_ALW, 0x8ec1, "ddr dtag register lock, free dtag left %d\n", ddr_dtag_free);
	ddr_dtag_free = 0;
	rtos_ddr_trace(LOG_ALW, 0x1688, "ddr dtag ackup check %d\n", _ddr_dtag_free);
}

fast_code u32 ddr_dtag_register(u32 dtag_num)
{
	u32 ret = ~0;

#ifdef MPC
	sys_assert(ddr_dtag_free != 0);
	spin_lock_take(SPIN_LOCK_KEY_DDR, CPU_ID, true);
#endif

	if (ddr_dtag_free < dtag_num) {
		rtos_ddr_trace(LOG_ERR, 0xa77f, "get %d ddr dtag fail\n", dtag_num);
	} else {
		ret = ddr_dtag_next;
		ddr_dtag_next += dtag_num;
		ddr_dtag_free -= dtag_num;
		//rtos_ddr_trace(LOG_ERR, 0, "get dtag %d from %d\n", dtag_num, ret);
		rtos_ddr_trace(LOG_ERR, 0xaf9f, "free dtag : %d, get dtag %d, from %d", ddr_dtag_free, dtag_num, ret);
	}
#ifdef MPC
	spin_lock_release(SPIN_LOCK_KEY_DDR);
#endif
	return ret;
}

#ifdef L2P_DDTAG_ALLOC
fast_code void ddr_dtag_l2p_register_lock(void)
{
	rtos_ddr_trace(LOG_ERR, 0xc471, "ddr dtag (l2p) register lock , free dtag left %d\n", ddr_dtag_free_l2p);
	//_ddr_dtag_free = ddr_dtag_free_l2p; No need to set tihs
	ddr_dtag_free_l2p = 0;
}

fast_code u32 ddr_dtag_l2p_register(u32 dtag_num)
{
	u32 ret = ~0;

#ifdef MPC
	sys_assert(ddr_dtag_free_l2p!= 0);
	spin_lock_take(SPIN_LOCK_KEY_DDR, CPU_ID, true);
#endif
	if (ddr_dtag_free_l2p < dtag_num) {
		rtos_ddr_trace(LOG_ERR, 0xf79a, "get %d ddr dtag (l2p) fail\n", dtag_num);
	} else {
	#ifdef L2P_FROM_DDREND
		ddr_dtag_next_from_end -= dtag_num;
		ret = ddr_dtag_next_from_end;
		ddr_dtag_free_l2p -= dtag_num;
		rtos_ddr_trace(LOG_ERR, 0xc28b, "get dtag (l2p) %d from %d free left %d\n", dtag_num, ret, ddr_dtag_free_l2p);
	#else
		ret = ddr_dtag_next_l2p;
		ddr_dtag_next_l2p += dtag_num;
		ddr_dtag_free_l2p -= dtag_num;
		rtos_ddr_trace(LOG_ERR, 0x804b, "get dtag (l2p) %d from %d\n", dtag_num, ret);
	#endif
	}

#ifdef MPC
	spin_lock_release(SPIN_LOCK_KEY_DDR);
#endif
	return ret;
}

#endif

#ifdef L2PnTRIM_DDTAG_ALLOC	//20201207-Eddie
fast_code void ddr_dtag_trim_register_lock(void)
{
	rtos_ddr_trace(LOG_ERR, 0x2acf, "ddr dtag (trim) register lock , free dtag left %d\n", ddr_dtag_free_trim);
	//_ddr_dtag_free = ddr_dtag_free_trim;	No need to set tihs
	ddr_dtag_free_trim = 0;
}

fast_code u32 ddr_dtag_trim_register(u32 dtag_num)
{
	u32 ret = ~0;

#ifdef MPC
	sys_assert(ddr_dtag_free_trim!= 0);
	spin_lock_take(SPIN_LOCK_KEY_DDR, CPU_ID, true);
#endif
	if (ddr_dtag_free_trim < dtag_num) {
		rtos_ddr_trace(LOG_ERR, 0x5cd0, "get %d ddr dtag (trim) fail\n", dtag_num);
	} else {
		ret = ddr_dtag_next_trim;
		ddr_dtag_next_trim += dtag_num;
		ddr_dtag_free_trim -= dtag_num;
		rtos_ddr_trace(LOG_ERR, 0x88fb, "get dtag (trim) %d from %d\n", dtag_num, ret);
	}
#ifdef MPC
	spin_lock_release(SPIN_LOCK_KEY_DDR);
#endif
	return ret;
}
#endif

#ifdef EPM_DDTAG_ALLOC
slow_code_ex void ddr_dtag_epm_register_lock(void)
{
	rtos_ddr_trace(LOG_ERR, 0x63a3, "ddr dtag (epm) register lock , free dtag left %d\n", ddr_dtag_free_epm);
	//_ddr_dtag_free = ddr_dtag_free_trim;	No need to set tihs
	ddr_dtag_free_epm = 0;
}
slow_code_ex u32 ddr_dtag_epm_register(u32 dtag_num)
{
	u32 ret = ~0;

#ifdef MPC
	sys_assert(ddr_dtag_free_epm!= 0);
	spin_lock_take(SPIN_LOCK_KEY_DDR, CPU_ID, true);
#endif
	if (ddr_dtag_free_epm < dtag_num) {
		rtos_ddr_trace(LOG_ERR, 0xc399, "get %d ddr dtag (epm) fail\n", dtag_num);
	} else {
		ret = ddr_dtag_next_epm;
		ddr_dtag_next_epm += dtag_num;
		ddr_dtag_free_epm -= dtag_num;
		rtos_ddr_trace(LOG_ERR, 0x676d, "get dtag (epm) %d from %d\n", dtag_num, ret);
	}
#ifdef MPC
	spin_lock_release(SPIN_LOCK_KEY_DDR);
#endif
	return ret;
}
#endif

#if NVME_TELEMETRY_LOG_PAGE_SUPPORT
slow_code u32 ddr_dtag_telemetry_register(u32 dtag_num)
{
	u32 ret = ~0;

	if (ddr_dtag_free_telemetry < dtag_num) {
		rtos_ddr_trace(LOG_ERR, 0xbfc4, "get %d ddr dtag (telemetry) fail\n", dtag_num);
	} else {
		ret = ddr_dtag_next_telemetry;
		ddr_dtag_next_telemetry += dtag_num;
		ddr_dtag_free_telemetry -= dtag_num;
		rtos_ddr_trace(LOG_ERR, 0xe5e3, "get dtag (telemetry) %d from %d\n", dtag_num, ret);
	}

	return ret;
}
#endif


ddr_code bool ddr_rw_device_self_test(void)
{
	extern volatile u8 plp_trigger;
	if(_ddr_dtag_free == 0)
	{
		rtos_ddr_trace(LOG_ERR, 0x3dd3, "[DST] DDR free DTAG is not enough to test _ddr_dtag_free: %x",_ddr_dtag_free);
		return true;
	}
	if(plp_trigger)
	{
		return false;
	}
 
//#ifdef MPC
	//spin_lock_take(SPIN_LOCK_KEY_DDR, CPU_ID, true);
//#endif
 
	dtag_t dtag;
	u16 *test_begin;
	u32 test_length;
	u32 pat_idx = get_tsc_lo();
	u16 pattern = (pat_idx & 0x1)?(0x5A5A):(0x7E7E);
 
	dtag.dtag = ddr_dtag_next;
	test_begin = (u16 *)ddtag2mem(dtag.dtag);
	test_length = (min(_ddr_dtag_free, DST_RAM_TEST_DU_CNT) << DTAG_SHF);
	rtos_ddr_trace(LOG_INFO, 0x7fed, "[DST] DDR test [s]:0x%x, len:0x%x, pat:0x%x", (u32)test_begin, test_length, pattern);
 
	for(u32 i=0; i<(test_length/sizeof(pattern)); i++)
	{
		if(plp_trigger)
		{
			return false;
		}
		test_begin[i] = pattern;
		if(test_begin[i] != pattern)
		{
			rtos_ddr_trace(LOG_ERR, 0x6fa4, "[DST] DDR test ERROR [addr]:0x%x, pat:0x%x", (u32)(&test_begin[i]), test_begin[i]);
			return true;
		}
	}
	memset((void *)test_begin, 0x00, test_length);
 
//#ifdef MPC
	//spin_lock_release(SPIN_LOCK_KEY_DDR);
//#endif
	return false;
}


/*! @} */
