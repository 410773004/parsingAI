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
 * slow_code_ex	 .sram_code_ex
 *
 * init_code .init_code
 * init_data .init_data
 *
 * SRAM layout
 * [ SRB  ][ CPU 0  ][ CPU 1 ][ CPU 2 ][ CPU 3 ][ CMF ][SHARE SRAM]
 * [ 2.5K ][ 125.5K ][ 128K  ][ 96K   ][ 32K   ][ 64K ][ 64K      ]
 *
 * [ DTAG                                      ]
 * [ init0 ][ init1 ][ init2 ][ init3 ] ...
 * [ 32 K  ][ 32K   ][ 32 K  ][ 32K   ] ...
 * [ 512K                                      ]
 */
SECTIONS {
    /* 128k ATCM */
    . = atcm_base;
    .ATCM :
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

    .atcm_free : {
       __atcm_free_start = .;
       . = . + __atcm_free_size;
       __atcm_free_end   = .;
    } > ATCM

    /* 192K BTCM */
    . = btcm_base;
    .BTCM :
    {
        *(.tcm_data.nrm.*)
        __btcm_data_end = .;
    } > BTCM

    .BTCM_zi : ALIGN(4)
    {
        _start_bss = .;    /* clear in startup.s */
        *(.tcm_data.zi.*)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
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

	.BTCM_ni :
    {
        __btcm_data_ni_start = .;
        *(.tcm_data.ni.*)
        . = ALIGN(4);
        __btcm_data_ni_end = .;
    } > BTCM

    /* free space of BTCM */
    __btcm_free_size = btcm_size - (__btcm_data_ni_end - btcm_base);
    ASSERT( __btcm_free_size >  24, "btcm_free no enough!!! Check BTCM Data!!")

    .btcm_free : ALIGN(4)
    {
       __btcm_free_start = .;
       . = . + __btcm_free_size;
       __btcm_free_end   = .;
    } > BTCM

    .share_btcm :
    {
        KEEP(*(SORT(.tcm_sh_code.*)))
        KEEP(*(SORT(.tcm_sh_data.nrm.*)))
    } > BTCM_SH

    .share_btcm_zi :
    {
        KEEP(*(SORT(.tcm_sh_data.zi.*)))
    } > BTCM_SH

    .share_btcm_ni :
    {
        KEEP(*(SORT(.tcm_sh_data.ni.*)))
        . = ALIGN(4);
        __sh_btcm_free_start = .;
    } > BTCM_SH

    . = srb_base;
    .SRBH :
    {
        . = . + 4096; /* extra 512 bytes for firmware slot */
    } > SRBH

    /* PSRAM */
    . = psram_base;
    .ps_range : ALIGN(32)
    {
       *(.ps_code.*)
       *(.sram_code.*)
       . = ALIGN(4);
       *(.ps_data.*)
       . = ALIGN(4);
       __ps_data_end = .;
    } > PSRAM

    __ps_free_size = psram_size - (__ps_data_end - psram_base) - 4;
    ASSERT( __ps_free_size >  24, "ps_free no enough!!! Check SRAM Code!!")
    
    .ps_free : ALIGN(4)
    {
       __ps_free_start = .;
       . = . + __ps_free_size;
       __ps_free_end   = .;
    } > PSRAM

    . = cmf_base;
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
    } > CMF

    . = sram_sh_base;
    .share_btcm_sram :
    {
        KEEP(*(SORT(.sram_sh_code.*)))
        KEEP(*(SORT(.sram_sh_data.*)))
        KEEP(utils/CMakeFiles/libutils.dir/trace.c.obj(.rodata.*))
        KEEP(utils/CMakeFiles/libutils.dir/trace.c.obj(.rodata*))
    } > SRAM_SH

    /* SRAM */
    . = sram_base;
    .sram_data : ALIGN(32)
    {
        __ucmd_start = .;
        KEEP(*(.ucmd_tbl*))
        __ucmd_end = .;
        *(.sram_data.nrm.*)
        *(.data.*)
    } > SRAM

    .rostr_data : ALIGN(32)
    {
        __rostr_start = .;
        *(EXCLUDE_FILE (*utils/CMakeFiles/libutils.dir/trace.c.obj) .rodata*)  /* .rodata* section */
        __rostr_data_end = .;
    } > SRAM

    .sram_data_zi : ALIGN(32)
    {
        *(.sram_data.zi.*)
    } > SRAM

    .sram_data_ni :
    {
        *(.sram_data.ni.*)
        . = ALIGN(4);
        _sram_data_ni_end = .;
    } > SRAM

    .sram_code_ex :
    {
        *(.sram_code_ex*)
        . = ALIGN(4);
        _sram_code_ex_end = .;
    } > SRAM

    __sram_free_size = (sram_size) - (_sram_code_ex_end - sram_base) - 4;
    ASSERT( __sram_free_size >  24, "sram_free no enough!!! Check SRAM Code!!")
    
    .sram_free : ALIGN(4)
    {
       __sram_free_start = .;
       . = . + __sram_free_size;
       __sram_free_end   = .;
    } > SRAM

    . = dtag_base;
    .dtag_stream_write_ex :
    {
        __dtag_mem_start = .;
        __dtag_stream_write_ex_start = .;
        . = . + stream_write_ex_size;
       __dtag_stream_write_ex_end = .;
    } > DTAG

    .init_section0 :
    {
        __init_start_0 = .;

        *(.init_code.0*)
        *(.init_code.0.*)
        /* *(.*plt*) */

        __sysinit_start_0 = .;
        KEEP(*(SORT(.sysinit.0.*)))
        __sysinit_end_0   = .;
        *(.init_data.0.*)
    } > DTAG

    ASSERT(. <= __init_start_0 + init_size0, "init_section0 over size!!")
    . = __init_start_0 + init_size0;
    __init_end_0 = .;
    .init_section1 __init_end_0 :
    {
        __init_start_1 = .;
        *(.init_code.1*)
        *(.init_code.1.*)
        /* *(.*plt*) */

        __sysinit_start_1 = .;
        KEEP(*(SORT(.sysinit.1.*)))
        __sysinit_end_1   = .;
        *(.init_data.1.*)
    } > DTAG

    ASSERT(. <= __init_start_1 + init_size1, "init_section1 over size!!")
    . = __init_start_1 + init_size1;
    __init_end_1 = .;
    .init_section2 __init_end_1 :
    {
        __init_start_2 = .;

        *(.init_code.2*)
        *(.init_code.2.*)
        /* *(.*plt*) */

        __sysinit_start_2 = .;
        KEEP(*(SORT(.sysinit.2.*)))
        __sysinit_end_2   = .;
        *(.init_data.2.*)
    } > DTAG

    ASSERT(. <= __init_start_2 + init_size2, "init_section2 over size!!")
    . = __init_start_2 + init_size2;
    __init_end_2 = .;
    .init_section3 __init_end_2 :
    {
        __init_start_3 = .;

        *(.init_code.3*)
        *(.init_code.3.*)
        /* *(.*plt*) */

        __sysinit_start_3 = .;
        KEEP(*(SORT(.sysinit.3.*)))
        __sysinit_end_3   = .;
        *(.init_data.3.*)
    } > DTAG

    ASSERT(. <= __init_start_3 + init_size3, "init_section3 over size!!")
    . = __init_start_3 + init_size3;
    __init_end_3 = .;
    .dtag_section0 __init_end_3 :
    {
        . = ALIGN(4096);
        __rsvd_dtag_section_start_0 = .;
        . = __rsvd_dtag_section_start_0 + rsvd_dtag_size * 1 + rsvd_cpu0_dtag_size; /* TODO: (2)*8=16 for meta_dtag, fix to dynamically allocation*/
        __rsvd_dtag_section_end_0 = .;
    } > DTAG

    .dtag_section1 :
    {
        . = ALIGN(4096);
        __rsvd_dtag_section_start_1 = .;
        . = __rsvd_dtag_section_start_1 + rsvd_dtag_size + rsvd_cpu1_dtag_size;
        __rsvd_dtag_section_end_1 = .;
    } > DTAG

    .dtag_section2 :
    {
        . = ALIGN(4096);
        __rsvd_dtag_section_start_2 = .;
        . = __rsvd_dtag_section_start_2 + rsvd_dtag_size;
        __rsvd_dtag_section_end_2 = .;
    } > DTAG

    .dtag_section3 :
    {
        . = ALIGN(4096);
        __rsvd_dtag_section_start_3 = .;
        . = __rsvd_dtag_section_start_3 + rsvd_dtag_size;
        __rsvd_dtag_section_end_3 = .;
    } > DTAG

    .dtag_event_log :
    {
        . = ALIGN(4096);
        __evlog_dtag_start = .;
        . = __evlog_dtag_start + evlog_dtag_size;
        __evlog_dtag_end = .;
    } > DTAG

    .fwconifg :
    {
        . = ALIGN(4096);
        __dtag_fwconfig_start = .;
        . = . + 4096;
        __dtag_fwconfig_end = .;
    } > DTAG

    .dtag_stream_read :
    {
        . = __dtag_mem_start + dtag_mem_size - (stream_write_size + stream_read_size);
        __dtag_stream_read_start = .;
        . = . + stream_read_size;
        __dtag_stream_read_end = .;
    } > DTAG

    .dtag_stream_write :
    {
        . = __dtag_mem_start + dtag_mem_size - stream_write_size;
        __dtag_stream_write_start = .;
        . = . + stream_write_size;
        __dtag_stream_write_end = .;
    } > DTAG

    /* Data Area: End */
    .dtag_section : {
        . = __dtag_mem_start + dtag_mem_size;
        __dtag_mem_end = .;
    } > DTAG

    /* DDR dtag */
    . = ddtag_base;
    .ddtag_section : {
        __ddr_dtag_start = .;
        . = . + ddtag_size;
        __ddr_dtag_end = .;
    } > DDTAG

    /* 	DDR CODE*/
    . = ddr_code_base;
    .ddr_code_range : ALIGN(32)
    {
       *(.ddr_code.*)
    } > DDR_CODE

		/* 	DDR DATA*/
    . = ddr_data_base;
    .ddr_data_ni :
    {
	   *(.ddr_data.ni.*)
    } > DDR_DATA
    .ddr_data_range : ALIGN(32)
    {
       *(.ddr_data.*)
       *(.ddr_data.nrm.*)
    } > DDR_DATA

   /* 	DDR SHARE DATA*/
    . = ddr_sh_base;
    .share_btcm_ddr :
    {
      KEEP(*(SORT(.ddr_sh_code.*)))
      KEEP(*(SORT(.ddr_sh_data.*)))
      . = ALIGN(4);
      __sh_ddr_free_start = .;
    } > DDR_SH

    . = ddr_evlog_base;
    .ddr_evlog_flush_buf :
    {
        __ddr_sect_start = . - ddr_all_cpus_size;
        . = ALIGN(4);
        __evlog_flush_buf_start = .;
        . = __evlog_flush_buf_start + evlog_flush_buf_size;
        __evlog_flush_buf_end = .;
    } > DDR_EVLOG    

    .ddr_evlog_agt_buf :
    {
        . = ALIGN(4);
        __evlog_agt_buf_start = .;
        . = __evlog_agt_buf_start + evlog_agt_buf_size;
        __evlog_agt_buf_end = .;
    } > DDR_EVLOG    
    
    .ddr_evlog_log_tag :
    {
        . = ALIGN(4);
        __evlog_log_tag_start = .;
        . = __evlog_log_tag_start + evlog_log_tag_size;
        __evlog_log_tag_end = .;
    } > DDR_EVLOG 

	 .ddr_his_tab_base :
    {
        . = ALIGN(4);
        __his_tab_start = .;
        . = __his_tab_start + his_tab_size;
        __his_tab_end = .;
    } > DDR_HisTab 

    /*. = ddr_telemetry_base; */
    .ddr_telemetry :
    {
        . = ALIGN(4);
        __telemetry_start = .;
        . = __telemetry_start + ddr_telemetry_size;
        __telemetry_end = .;
        __ddr_sect_end = .;
    } > DDR_TELEMETRY

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
