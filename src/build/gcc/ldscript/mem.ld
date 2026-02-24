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

/*
 * 0x0000_0000 ATCM begin
 * 0x0000_ffff ATCM end
 *
 * 0x0004_0000 BTCM begin
 * 0x0007_ffff BTCM end
 *
 * 0x2000_0000 SRAM low begin - optimization for CPU access
 * 0x2003_ffff SRAM low end
 *
 * 0x2004_0000 SRAM high begin - optimization for data cache
 * 0x200b_ffff SRAM high end
 */
MEMORY
{
    ATCM    (rwx)  : ORIGIN = 0x00000000, LENGTH = 0x00018000 /* 96K  */
    BTCM    (rw)   : ORIGIN = 0x00080000, LENGTH = 0x00030000 /* 192K */
    BTCM_SH (rw)   : ORIGIN = 0x000B0000, LENGTH = 0x00020000 /* 128K  */
    SRAM    (rwx)  : ORIGIN = 0x20000000, LENGTH = 0x00200000 /* 2048K */
    DDTAG   (rw)   : ORIGIN = 0x40000000, LENGTH = 0x00800000 /* 8M */
    DDRSECT   (rw)   : ORIGIN = 0x40800000, LENGTH = 0x06400000 /* 100M */
}
