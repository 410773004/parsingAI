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

/*! \file smart.h
 * @brief rainier nvme smart header
 *
 * \addtogroup decoder
 * \defgroup smart
 * \ingroup decoder
 * @{
 */

extern smart_statistics_t *smart_stat;
extern volatile u32 resver_blk;
extern bool first_usr_open;

struct health_ipc
{
    u32 avg_erase;
    u32 max_erase;
    u32 min_erase;
    u32 total_ec;
    u32 avail;
    u32 spare;
    u32 thr;
    void *cmdreq;
    u32 bytes;
} health_ipc;

struct ex_health_ipc
{
    u32 avg_erase;
    u32 max_erase;
    u32 min_erase;
    u32 total_ec;
    void *cmdreq;
    u32 bytes;
} ex_health_ipc;

typedef enum
{
    cLogSmartBackupOTF = 0,
    cLogSmartBackupPOR,
} LogSmartBackupType_t;

/*!
 * @brief Get data units read/write counter in 64 bits
 *
 * @param count	original data units counter of read/write
 *
 * @return	count divided by 1000
 */
u64 smart_calcuate_dus_access(u64 count);

/*!
 * @brief update thermal menagement time in smart statistics information
 *
 * @return	not used
 */
void smart_update_thermal_time(void);

/*!
 * @brief calculate power on time per second and update to smart
 * statistics information
 *
 * @return	not used
 */
void smart_update_power_on_time(void);

/*!
 * @brief increase error info log entry number in smart statistics information
 *
 * @return	not used
 */
void smart_inc_err_cnt(void);

/*!
 * @brief get estimated endurance, spare capacity and read only state
 *
 * @param health	health information
 *
 * @return		not used
 */
 #if 0
void health_get_nvm_status(struct nvme_health_information_page *health);
#endif

/*!
 * @brief get read/write data unit size, host read/write command count
 * and controller busy time
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_io_info(struct nvme_health_information_page *health);

/*!
 * @brief get power-on count, unsafe shutdown count and power-on time
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_power_info(struct nvme_health_information_page *health);

/*!
 * @brief get Media err count and number of err info log entries
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_errors(struct nvme_health_information_page *health);

/*!
 * @brief get sensor temperature info and thermal management info
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_temperature(struct nvme_health_information_page *health);

/*!
 * @brief get gc count info
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_gc_count(struct nvme_additional_health_information_page *health);

/*!
 * @brief get inflight command
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_inflight_command(struct nvme_additional_health_information_page *health);

/*!
 * @brief get hcrc detection command
 *
 * @param health	health information
 *
 * @return		not used
 */
void health_get_hcrc_detection_count(struct nvme_additional_health_information_page *health);

/*!
 * @brief clear smart statistics information
 *
 * @return	not used
 */
void smart_stat_clean(void);

/*!
 * @brief statistics for data unit write and write commands
 *
 * @param count		data units counter of write 512B
 *
 * @return		not used
 */
static void inline __attribute__((always_inline)) fast_code
smart_stat_inc_duw(u16 count)
{
	smart_stat->host_write_commands++;
	smart_stat->data_units_written += count;
}

/*!
 * @brief statistics for data unit read and read commands
 *
 * @param count		data units counter of read 512B
 *
 * @return		not used
 */
static void inline __attribute__((always_inline)) fast_code
smart_stat_inc_dur(u16 count)
{
	smart_stat->host_read_commands++;
	smart_stat->data_units_read += count;
}

/*!
 * @brief initialize smart data structure
 *
 * @return	not used
 */
void smart_stat_init(void);

/*
 *@for controller busy time use
 *
 */
void smart_stat_init_ctrl_busy_time(u32 sys_clk);

/*!
 * @brief Backup SMART attribute data
 *
 * @return	not used
 */
void SmartDataBackup(LogSmartBackupType_t logSmartBackup);

/*! @} */
