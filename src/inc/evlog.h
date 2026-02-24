//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------
#if defined(RDISK)
/*! \file evlog.h
 * @brief event log module, record event log and save/retrieve into/from nand
 *
 * \addtogroup utils
 * \defgroup evlog
 * \ingroup utils
 * @{
 */
#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "types.h"
#include "evt_trace_log.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------
/*!
 * @brief event log agent initialization
 *
 * @return	not used
 */
void evlog_agt_init(void);

/*! @} */
#endif

extern volatile u32 poh;
extern volatile u32 pc_cnt;

enum
{
	EVT_UART_SAVE_LOG,
	EVT_NVME_SAVE_LOG,
	EVT_PANIC,
	EVT_PANIC_BEFORE_EVLOG_INITIALIZED,
	EVT_SECURITY_PASSWD_CHKSUM_ERR,
	EVT_CMD_TIMEOUT,
	EVT_DATA_TRANSFER_TIMEOUT,
	EVT_WAIT_SOC_TEMP_SENSOR_DONE_TIMEOUT,
	EVT_SET_NAND_TOGGLE_MODE_FAIL,
	EVT_NO_BLOCK_FOR_SYSTEM_INFO,
	EVT_ERASE_CNT_GEQ_65535,
	EVT_TEMP_OVER_85_DEG_300_SEC,
	EVT_TEMP_OVER_90_DEG,
	EVT_INIT_ECU_FAIL,
	EVT_NO_BUF_WHEN_ALLOC,
	EVT_ThermalSensor_Read_Error,
	EVT_PLP_IC_ERR,
	EVT_RAID_TRIGGER,
	EVT_RAID_RECVOER_DONE,
	EVT_READ_ONLY_MODE_IN,
	EVT_READ_ONLY_MODE_OUT,
	EVT_PLP_HANDLE_DONE,
	EVT_MARK_GLIST,
	EVT_SRAM_PARITY_ERR,
	EVT_2BIT_DDR_ERR,
	EVT_AXI_PARITY_ERR,
	//EVT_POWERON_NO_TRIM_TABLE,
	EVT_while_break,
	EVT_DEL_SQ_ABNORMAL,
	EVT_DESC_MAX_CNT,
	EVT_PCIE_CPL_TO,
	EVT_PCIE_LK_DOWN,
	EVT_HOST_WUNC,
	EVT_CMD_ERROR,
	EVT_NVME_FORMATE,
	EVT_NVME_NS_CREATE,
	EVT_NVME_NS_DELETE,
    EVT_SPOR_UNEXPECT_ERR,
    EVT_GC_CANT_FREE_SPB,
    EVT_WHCRC_REG_ECCT,
    EVT_PCIE_STS_ERR,
	EVT_LINK_LOSS,
    EVT_PCIE_ABNORMAL,
    EVT_CRITICAL_WARNING,
#if defined(USE_CRYPTO_HW)
    EVT_CHANGE_KEY,
#endif
#ifdef TCG_NAND_BACKUP
	EVT_TCG_TBL_ERR,
#endif
	EVT_PLP_NOT_DONE,
	EVT_POWERONLOCK,
	EVT_CPU_FEEDBACK_FAIL,
	EVT_VAC_REBUILD,
    EVT_POWEROFF,
    EVT_POWERON,
    EVT_TELE,
};

enum
{
	 JNL_TAG_POWER_CYCLE   = 0xFB01,
	 JNL_TAG_REBOOT,
	 JNL_TAG_WARMBOOT,
     JNL_TAG_RETRIEVAL_LOG,
};

#ifdef EVLOG_C
typedef struct _evt_reason_t
{
	u16 evt_id;
	char reason[64];
}evt_reason_t;

static evt_reason_t evt_reason[] = {
	{EVT_UART_SAVE_LOG,                     "uart save log"},
	{EVT_NVME_SAVE_LOG,                     "nvme save log"},
	{EVT_PANIC,                             "panic"},
	{EVT_PANIC_BEFORE_EVLOG_INITIALIZED,    "panic occurred before evlog initialized"},
	{EVT_SECURITY_PASSWD_CHKSUM_ERR,        "security password checksum check error"},
	{EVT_CMD_TIMEOUT,                       "command timeout"},
	{EVT_DATA_TRANSFER_TIMEOUT,             "data transfer timeout"},
	{EVT_WAIT_SOC_TEMP_SENSOR_DONE_TIMEOUT, "wait soc temperature sensor done timeout"},
	{EVT_SET_NAND_TOGGLE_MODE_FAIL,         "set nand toggle mode fail"},
	{EVT_NO_BLOCK_FOR_SYSTEM_INFO,          "no block for system info"},
	{EVT_ERASE_CNT_GEQ_65535,               "erase count >= 65535"},
	{EVT_TEMP_OVER_85_DEG_300_SEC,          "temperature over 85deg last for_300 sec"},
	{EVT_TEMP_OVER_90_DEG,                  "temperature over 90deg"},
	{EVT_INIT_ECU_FAIL,                     "initialize ECU fail"},
	{EVT_NO_BUF_WHEN_ALLOC,                 "no buffer when allocate"},
	{EVT_ThermalSensor_Read_Error,          "Thermal IC i2c read err"},
	{EVT_PLP_IC_ERR,                        "i2c read err from plp ic"},
	{EVT_RAID_TRIGGER,                      "raid recover trigger start"},
	{EVT_RAID_RECVOER_DONE,                 "raid recover finished"},
	{EVT_READ_ONLY_MODE_IN,                 "enter read only mode"},
	{EVT_READ_ONLY_MODE_OUT,                "leave read only mode"},
	{EVT_PLP_HANDLE_DONE,                   "plp handle finished"},
    {EVT_MARK_GLIST,                        "find nand error mark glist"},
    {EVT_SRAM_PARITY_ERR,                   "Sram parity error"},
	{EVT_2BIT_DDR_ERR,                      "ddr 2 bit err"},
	{EVT_AXI_PARITY_ERR,                    "axi parity error"},
 	{EVT_while_break,                       "while loop break"},
	{EVT_DEL_SQ_ABNORMAL,                   "Delete SQ abnormal"},
    {EVT_PCIE_CPL_TO,                       "PCIe CPL TO"},
    {EVT_PCIE_LK_DOWN,                      "PCIe Link Down"},
	{EVT_HOST_WUNC,                         "Host WUNC CMD Done"},
	{EVT_CMD_ERROR,                         "Host CMD Error"},
	{EVT_NVME_FORMATE,	                    "Nvme format"},
	{EVT_NVME_NS_CREATE,                    "NS create"},
	{EVT_NVME_NS_DELETE,                    "NS delete"},
    {EVT_SPOR_UNEXPECT_ERR,                 "SPOR unexpected error"},
	{EVT_GC_CANT_FREE_SPB,					"GC cannot free spb"},
    {EVT_WHCRC_REG_ECCT,                    "WHCRC error register ECCT"},
    {EVT_PCIE_STS_ERR,                      "PCIE Link Error"},
    {EVT_LINK_LOSS,							"No perst or clk"},
	{EVT_PCIE_ABNORMAL,						"PCIe abnormal found"},
	{EVT_CRITICAL_WARNING,                  "Critical Warning"},
	{EVT_TELE,								"Telemetry update"},
#if defined(USE_CRYPTO_HW)
    {EVT_CHANGE_KEY,                        "Change Key(s) occurs"},
#endif
#ifdef TCG_NAND_BACKUP
	{EVT_TCG_TBL_ERR,                       "TCG Table error occurs"},
#endif
	{EVT_PLP_NOT_DONE,                      "PLP Not Done"},
	{EVT_POWERONLOCK,						"POWER ON DeadLock Break"},
	{EVT_CPU_FEEDBACK_FAIL,					"FEEDBACK_CHK_SAVE_LOG"},
	{EVT_VAC_REBUILD,						"VAC_TABLE_REBUILD"},
};
#endif

u32 evlog_printk(log_level_t log_level, const char *fmt, ...);
u32 evlog_save_encode(log_level_t log_level, log_buf_t *header, log_buf_t *buf1, log_buf_t *buf2, log_buf_t *buf3);
void flush_to_nand(u16 evt_reason_id);
void agt_buf_flush_to_uart_all2();
