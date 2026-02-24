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

sram_base = ORIGIN(SRAM);
sram_size = LENGTH(SRAM);

ddtag_base = ORIGIN(DDTAG);
ddtag_size = LENGTH(DDTAG);

stack_size = 4096;
heap_size_least  = 4096;
dtag_mem_size = (1280 * 1024);

ps_sram_mem_size = (4 * 1024);

