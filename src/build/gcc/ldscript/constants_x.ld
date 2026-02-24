/*-----------------------------------------------------------------------------
 *                 Copyright(c) 2016-2019 Innogrit Corporation
 *                             All Rights reserved.
 *
 * The confidential and proprietary information contained in this file may
 * only be used by a person authorized under and to the extent permitted
 * by a subsisting licensing agreement from Innogrit Corporation.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from Innogrit Corporation.
 *-----------------------------------------------------------------------------
 */

atcm_base = ORIGIN(ATCM);
atcm_size = LENGTH(ATCM);

btcm_base = ORIGIN(BTCM);
btcm_size = LENGTH(BTCM);

btcm_sh_base = ORIGIN(BTCM_SH);
btcm_sh_size = LENGTH(BTCM_SH);

psram_base = ORIGIN(PSRAM);
psram_size = LENGTH(PSRAM);

sram_base = ORIGIN(SRAM);
sram_size = LENGTH(SRAM);

srb_base = ORIGIN(SRBH);
srb_size = LENGTH(SRBH);

cmf_base = ORIGIN(CMF);
cmf_size = LENGTH(CMF);

sram_sh_base = ORIGIN(SRAM_SH);
sram_sh_size = LENGTH(SRAM_SH);

dtag_base = ORIGIN(DTAG);
dtag_size = LENGTH(DTAG);

ddtag_base = ORIGIN(DDTAG);
ddtag_size = LENGTH(DDTAG);

ddr_code_base = ORIGIN(DDR_CODE);
ddr_code_size = LENGTH(DDR_CODE);

ddr_data_base = ORIGIN(DDR_DATA);
ddr_data_size = LENGTH(DDR_DATA);

ddr_sh_base = ORIGIN(DDR_SH);
ddr_sh_size = LENGTH(DDR_SH);

ddr_evlog_base = ORIGIN(DDR_EVLOG);
ddr_evlog_size = LENGTH(DDR_EVLOG);

ddr_his_tab_base = ORIGIN(DDR_HisTab);
ddr_his_tab_size = LENGTH(DDR_HisTab);

ddr_telemetry_base = ORIGIN(DDR_TELEMETRY);
ddr_telemetry_size = LENGTH(DDR_TELEMETRY);

ddr_all_cpus_size = (4 * (ddr_code_size + ddr_data_size) + ddr_sh_size);

stack_size = 4096;
dtag_mem_size = (1368 * 1024);
stream_write_size = (512 * 1024);
stream_write_ex_size = (256 * 1024);
stream_read_size = 0;

init_size0 = (20 * 1024);
init_size1 = (24 * 1024);
init_size2 = (40 * 1024);
init_size3 = (12 * 1024);
rsvd_dtag_size = (20 * 4096);
/*rsvd_cpu0_dtag_size = (14 * 4096); 20210510-Eddie*/
rsvd_cpu0_dtag_size = (18 * 4096);
rsvd_cpu1_dtag_size = (6 * 4096);

evlog_dtag_size = (4 * 4096);
evlog_flush_buf_size = (78 * 4 * 4096);
evlog_agt_buf_size = (78 * 8 * 4096);
evlog_log_tag_size = (78 * 4096);

his_tab_size = (5120 * 1024);