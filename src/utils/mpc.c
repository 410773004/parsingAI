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

#include "types.h"
#include "queue.h"
#include "bf_mgr.h"
#include "sect.h"
#include "rainier_soc.h"
#if defined(MPC)
#include "ncl_exports.h"
#include "mpc.h"
#include "heap.h"
#include "fc_export.h"
#include "spin_lock.h"
#include "stdlib.h"
#include "ns.h"
#define __FILEID__ mpc
#include "trace.h"

/*! \file mpc.c
 * @brief multiple CPU build environment, add definitions which are used between CPUs
 *
 * \addtogroup utils
 * \defgroup mpc
 * \ingroup mpc
 * @{
 */
#if defined(SAVE_CDUMP)
share_data_zi volatile u32 sgexception_sp_cpu1;
share_data_zi volatile u32 sgexception_sp_cpu2;
share_data_zi volatile u32 sgexception_sp_cpu3;
share_data_zi volatile u32 sgexception_sp_cpu4;
share_data_zi volatile u8 sgfulink;
share_data_zi volatile u8 sgfulink_cpu1;
share_data_zi volatile u8 sgfulink_cpu2;
share_data_zi volatile u8 sgfulink_cpu3;
share_data_zi volatile u8 sgfulink_cpu4;
share_data_zi volatile u8 sgfulink_cpu_flag[MAX_MPC];
share_data_zi volatile u8 sgfulink_cpu_seq[MAX_MPC];
share_data_zi volatile u8 avoid_hang_CPU[MAX_MPC];//_GENE_0526
share_data_zi volatile u8 seq;
#endif

share_data_zi volatile mpc_init_bmp_t cpu_init_bmp[MAX_MPC];	///< cpu initialization bitmap for sync operation during initialization
share_data_zi heap_t *share_heap;					///< heap of shared TCM
share_data_zi ftl_remap_t ftl_remap;				///< ftl remap table which initialized in defect_mgr.c
share_data_zi volatile ns_share_t _ns_share[NVMET_NR_NS];	///< share namespace data structure
share_data_zi volatile u8 FTL_scandefect ; //AlanHuang
#ifdef SKIP_MODE
share_data_zi u8* gl_pt_defect_tbl;				///< global pointer to ftl defect table which initialized in defect_mgr.c
#endif
share_data_zi volatile u8* aging_pt_defect_tbl;
#ifdef Dynamic_OP_En
share_data_zi volatile u32 DYOPCapacity;
share_data_zi volatile u8 DYOP_FRB_Erase_flag;  //20210511 now only for rdisk_fe_ns_restore() use
share_data_zi volatile u32 DYOP_LBA_L;
share_data_zi volatile u32 DYOP_LBA_H;
#endif
#if EPM_NOT_SAVE_Again
share_data volatile u8 EPM_NorShutdown;
#endif
#if (UART_L2P_SEARCH == 1)
share_data_zi volatile u8 l2pSrchUart;
#endif

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
share_data volatile u32 shr_spb_info_ddtag;
#endif

init_code void share_heap_init(void)
{
	u32 start = (u32) &__sh_btcm_free_start;
	u32 end = BTCM_SH_BASE + BTCM_SH_SIZE;
	int size = end - start;

	sys_assert(size);

	// todo: if size is too small, return it
	share_heap = heap_init((void *) start, size);
}


#if defined(SAVE_CDUMP)
ddr_code void ulink_cpu_cmd(void)
{
    u8 i,j;

    for (i = 0; i < MAX_MPC; i++)
    {
        for(j = 0; j < MAX_MPC; j++)
        {
           // printk("11 Avoid ipc to hanging CPU%d\n", avoid_hang_CPU[j]);
            if (sgfulink_cpu_seq[i]==avoid_hang_CPU[j]) {
    			printk("Avoid ipc to hanging CPU%d\n", avoid_hang_CPU[j]);
    			sgfulink_cpu_flag[i] = 2;
    		}
        }
    }
    for (i = 0; i < MAX_MPC - 1; i++)
    {
        switch (sgfulink_cpu_seq[i]) {
        case 1:
            if ((sgfulink_cpu_flag[i] != 2)) {
                printk("sgfulink_cpu_flag = %d\n", sgfulink_cpu_flag[i]);
                printk("wait cpu1 entry data abort mode.\n");
                cpu_msg_issue(CPU_FE - 1, CPU_MSG_ULINK, 0, 0);
                do {
                    mdelay(10);
                } while(sgfulink_cpu_flag[i] != 2);
            }
        case 2:
            if ((sgfulink_cpu_flag[i] != 2)) {
                printk("sgfulink_cpu_flag = %d\n", sgfulink_cpu_flag[i]);
                printk("wait cpu2 entry data abort mode.\n");
                cpu_msg_issue(CPU_BE - 1, CPU_MSG_ULINK, 0, 0);
                do {
                    mdelay(10);
                } while(sgfulink_cpu_flag[i] != 2);
            }
        case 3:
            if ((sgfulink_cpu_flag[i] != 2)) {
                printk("sgfulink_cpu_flag = %d\n", sgfulink_cpu_flag[i]);
                printk("wait cpu3 entry data abort mode.\n");
                cpu_msg_issue(CPU_FTL - 1, CPU_MSG_ULINK, 0, 0);
                do {
                    mdelay(10);
                } while(sgfulink_cpu_flag[i] != 2);
            }
        case 4:
            if ((sgfulink_cpu_flag[i] != 2)) {
                printk("sgfulink_cpu_flag = %d\n", sgfulink_cpu_flag[i]);
                printk("wait cpu4 entry data abort mode.\n");
                cpu_msg_issue(CPU_BE_LITE - 1, CPU_MSG_ULINK, 0, 0);
                do {
                    mdelay(10);
                } while(sgfulink_cpu_flag[i] != 2);
            }
        default:
            break;
        }

   }

}
#endif

fast_code void *share_malloc(u32 size)
{
	void *ret;

	spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
	ret = heap_alloc(share_heap, size);
	spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
	if (ret == NULL) {
		ret = sys_malloc(SLOW_DATA, size);
		//utils_mpc_trace(LOG_ERR, 0, "fast -> slow %d\n", size);
		sys_assert(ret);
	}
	return ret;
}

fast_code void* share_malloc_aligned(u32 size, u32 aligned)
{
	void *mem;
	void **ret;
	size += sizeof(void*) + aligned;

	spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
	mem = heap_alloc(share_heap, size);
	spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
	if (mem == NULL) {
		mem = sys_malloc(SLOW_DATA, size);
		utils_mpc_trace(LOG_ERR, 0x0986, "fast -> slow %d\n", size);
	}

	ret = (void**) ((((u32) mem) + aligned + sizeof(void*)) & (~(aligned - 1)));
	ret[-1] = mem;

	return ret;
}

fast_code void share_free(void *ptr)
{
	if ((u32) ptr >= SRAM_BASE) {
		sys_free(SLOW_DATA, ptr);
		return;
	}
	spin_lock_take(SPIN_LOCK_KEY_SHARE_TCM, 0, true);
	heap_free(share_heap, ptr);
	spin_lock_release(SPIN_LOCK_KEY_SHARE_TCM);
}

/*!
 * @brief check if fwconfig was valid or not
 *
 * @return	return fwconfig pointer if it was valid
 */
init_code fw_config_set_t *fw_config_ver_chk(void)
{
	fw_config_set_t *fwconfig = (fw_config_set_t *) &__dtag_fwconfig_start;

	if (fwconfig->header.signature != IMAGE_CONFIG)
		return NULL;

	return fwconfig;
}

init_code ftl_cfg_t *fw_config_get_ftl(void)
{
	fw_config_set_t *fwconfig = fw_config_ver_chk();

	if (fwconfig == NULL)
		return NULL;

	return &fwconfig->ftl;
}

/*! @} */
#endif
