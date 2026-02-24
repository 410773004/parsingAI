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

MEMORY
{
    ATCM    (rwx)  			: ORIGIN = 0x00000000, LENGTH = 0x00018000 /* 96K  */
    BTCM    (rw)   			: ORIGIN = 0x00080000, LENGTH = 0x00030000 /* 192K */
    BTCM_SH (rw)   			: ORIGIN = 0x000B0000, LENGTH = 0x00020000 /* 128K  */
    SRBH    (rw)   			: ORIGIN = 0x20000000, LENGTH = 0x00001000 /* 4K */
    PSRAM   (rw)   			: ORIGIN = 0x20006000, LENGTH = 0x00010000 /* 64K */
    CMF     (rw)   			: ORIGIN = 0x20020000, LENGTH = 0x00018000 /* CMF 96K. For Extra LDPC code */
    SRAM_SH (rwx)  			: ORIGIN = 0x20038000, LENGTH = 0x00004000 /* share 16K */
    SRAM_LOADER (rwx)                   : ORIGIN = 0x2003c000, LENGTH = 0x00004000 /* loader 16K */
    SRAM    (rwx)  			: ORIGIN = 0x2005a000, LENGTH = 0x00026000 /* 152K */
    DTAG    (rwx)  			: ORIGIN = 0x200aa000, LENGTH = 0x00156000 /* 1368K */
    DDTAG   (rw)   			: ORIGIN = 0x40000000, LENGTH = 0x01E00000 /* 30M */
    DDR_CODE     (rw)       : ORIGIN = 0x42000000, LENGTH = 0x00100000 /* 1M */
    DDR_DATA     (rw)       : ORIGIN = 0x42100000, LENGTH = 0x00100000 /* 1M */
    DDR_SH  (rw)   			: ORIGIN = 0x42600000, LENGTH = 0x00200000 /* 2M */
    DDR_EVLOG(rw)           : ORIGIN = 0x42800000, LENGTH = 0x00400000 /* 4M */
	DDR_HisTab(rw) 			: ORIGIN = 0x42c00000, LENGTH = 0x00500000 /* 5M */
    DDR_TELEMETRY(rw)       : ORIGIN = 0x43100000, LENGTH = 0x00040000 /* 256K */
}
