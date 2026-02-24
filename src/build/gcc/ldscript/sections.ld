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

ENTRY(_start)

/*
 * fast_code .tcm_code
 * fast_data .tcm_data
 * ps_code   .ps_code
 *
 * slow_code .sram_code
 * slow_data .sram_data
 *
 * init_code .init_code
 * init_data .init_data
 *
 */
SECTIONS {
    /* 128k ATCM */
    . = atcm_base;
    .ATCM : ALIGN(4)
    {
        *(.vectors) /* startup code */
        *(.text)
        *(.text*)
        *(.tcm_code.*)
        *(.glue_7)  /* glue arm to thumb code */
        *(.glue_7t) /* glue thumb to arm code */
        . = ALIGN(4);
        __atcm_end = .;
    } > ATCM

    /* free space of ATCM */
    __atcm_free_size = atcm_size - __atcm_end;

    .atcm_free :
    {
       __atcm_free_start = .;
       . = . + __atcm_free_size;
       __atcm_free_end   = .;
    } > ATCM

    /* 192k BTCM */
    . = btcm_base;
    .BTCM :
    {
        *(.tcm_data.*)
    } > BTCM

    .BTCM_zi : ALIGN(4)
    {
        _start_bss = .; /* clear in startup.s */
        *(.tcm_data.zi.*)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
    } > BTCM

    .BTCM_ni :
    {
        *(.tcm_data.ni.*)
    } > BTCM

    .stack : ALIGN(8)
    {
        __svc_stack_start__ = .;
        . = . + ((stack_size * 3) >> 2);
        __svc_stack_base  = .;

        . = . + ((stack_size * 3) >> 2);
        __irq_stack_base  = .;

        __stack_end  = .;
    } > BTCM

    /* free space of BTCM */
    __btcm_free_size = btcm_size - (__stack_end - btcm_base);
    .btcm_free : ALIGN(4)
    {
       __btcm_free_start = .;
       . = . + __btcm_free_size;
       __btcm_free_end   = .;
    } > BTCM

    /* SRAM ---  power saving section */
    . = sram_base;
    .SRBH :
    {
        . = . + 4096;
    } > SRAM

    .ps_range : ALIGN(32)
    {
       *(.ps_code.*)
       *(.ps_data.*)
       __ucmd_start = .;
       KEEP(*(.ucmd_tbl*))
       __ucmd_end = .;
    } > SRAM

    .cmf_section : ALIGN(4096)
    {
       __cmf_data_start = .;
       *(.cmf_enc_data.*)
       . = ALIGN(4096);
       *(.cmf_dec_data.*)
       . = ALIGN(4096);
       __cmf_data_end = .;
       . = ALIGN(4096);
       __cmf_lut_start = .;
       *(.seed_lut_data.*)
       . = ALIGN(4096);
       __cmf_lut_data_end = .;
    } > SRAM

    /* we just make compiler happy, SDK didn't use this so far */
    __ps_free_size = ps_sram_mem_size;
    .ps_free : ALIGN(4)
    {
       __ps_free_start = .;
       . = . + __ps_free_size;
       __ps_free_end   = .;
    } > SRAM

    /* Other SRAM */
    .sram_data : ALIGN(32)
    {
        *(.sram_code.*)
        . = ALIGN(4);
        *(.sram_data.*)
        *(.data.*)
    } > SRAM

    .rostr_data : ALIGN(32)
    {
        __rostr_start = .;
        *(.rodata)   /* .rodata section */
        *(.rodata.*)  /* .rodata* section */
        __rostr_data_end = .;
    } > SRAM

    .sram_zi : ALIGN(32)
    {
        *(.sram_data.zi.*)
    } > SRAM

    .sram_ni :
    {
        *(.sram_data.ni.*)
        . = ALIGN(4);
        _sram_data_ni_end = .;
    } > SRAM

    __sram_free_size = sram_size - dtag_mem_size - (_sram_data_ni_end - sram_base) - 4;
    .sram_free : {
       __sram_free_start = .;
       . = . + __sram_free_size;
       __sram_free_end   = .;
    } > SRAM

    .init_section :
    {
        . = ALIGN(8192);
        __dtag_mem_start = .;

       __init_start = .;

       *(.init_code)
       *(.init_code.*)
       *(.*plt*)

       __sysinit_start = .;
       KEEP(*(SORT(.sysinit*)))
       __sysinit_end   = .;

       *(.init_data)
       *(.init_data.*)
        . = ALIGN(8192);
       __init_end = .;
    } > SRAM

    .fwconifg :
    {
        . = ALIGN(4096);
        __dtag_fwconfig_start = .;
        . = . + 4096;
        __dtag_fwconfig_end = .;
    } > SRAM

    /* Data Area: End */
    .dtag_section : {
        . = __dtag_mem_start + dtag_mem_size;
        __dtag_mem_end   = .;
    } > SRAM

    /* DDR dtag */
    . = ddtag_base;
    .ddtag_section : {
        __ddr_dtag_start = .;
        . = . + ddtag_size;
        __ddr_dtag_end = .;
    } > DDTAG

    /* .dynsym _image_binary_end : { *(.dynsym) } */
    .dynbss : { *(.dynbss) }
    .dynstr : { *(.dynstr*) }
    .dynamic : { *(.dynamic*) }
    .plt : { *(.plt*) }
    .interp : { *(.interp*) }
    .gnu.hash : { *(.gnu.hash) }
    .gnu : { *(.gnu*) }
    .ARM.exidx : { *(.ARM.exidx*) }
    .gnu.linkonce.armexidx : { *(.gnu.linkonce.armexidx.*) }
}
