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
#define BEGIN_ENUM typedef enum {
#define ENUM_VAL(name, value) name = value,
#define END_ENUM(enum_name)  } enum_name;

/*
interrupt source:
   23  i_pcie_int[1],
   22  i_sys_int,
   21  w_peri_int,
   20  w_timer_int,
   19  w_ncb_other_int,
   18  w_bm_other_int,
   17  w_cmd_proc_int1,
   16  w_nvmctr_other_int,
   15  i_pwr_loss_int,
   14  i_pcie_int[0],
   13  w_ficu_fcmd_done,
   12  w_cmd_proc_int0,
   2-11 w_bm_mk_int[9:0],
   1  w_hmb_alkup_dn_int,
   0  w_io_cmd_fthed_int};
 */
BEGIN_ENUM
/* rainier intr mapping */
//ENUM_VAL(VID_HMB_LKPT_DONE,   1)
//ENUM_VAL(VID_FICU_FCMD_DONE,  13)
//ENUM_VAL(VID_PCIE_INT0,       14)
//ENUM_VAL(VID_PWR_LOSS_ASSERT, 15)
//ENUM_VAL(VID_NVME_CTRLR_MISC, 16)
//ENUM_VAL(VID_NCB_MISC,        19)
//ENUM_VAL(VID_TIMER,           20)
//ENUM_VAL(VID_PERIPHERAL,      21)  /* UART/I2C, no SPI */
//ENUM_VAL(VID_SYSTEM_MISC,     22)
//ENUM_VAL(VID_PCIE_INT1,       23)
//ENUM_VAL(VID_IPC_RX,          25)  /* IPC RX */

/* rainier intr mapping */
ENUM_VAL(VID_NVME_P0,         0)
ENUM_VAL(VID_L2P_Q0_SRCH_RSLT,1)

ENUM_VAL(VID_NCMD_RECV_Q0, (2+0))
ENUM_VAL(VID_NCMD_RECV_Q1, (2+1))
ENUM_VAL(VID_NCMD_RECV_Q2, (2+2))
ENUM_VAL(VID_NCMD_RECV_Q3, (2+3))

ENUM_VAL(VID_WR_DATA_GRP0, (2+4))
ENUM_VAL(VID_WR_DATA_GRP1, (2+5))
ENUM_VAL(VID_RD_DATA_GRP0, (2+6))
ENUM_VAL(VID_RD_DATA_GRP1, (2+7))
ENUM_VAL(VID_FW_RD_GRP0,   (2+8))
ENUM_VAL(VID_FW_RD_GRP1,   (2+9))

ENUM_VAL(VID_COM_FREE,     (2+10))
ENUM_VAL(VID_PROC_DONE,    (2+11))
ENUM_VAL(VID_RLS_RCMD,     (2+12))
ENUM_VAL(VID_RLS_WCMD,     (2+13))

ENUM_VAL(VID_BTN_OTHER,       17)
ENUM_VAL(VID_L2P_Q01_UPDT_RSLT,18)
ENUM_VAL(VID_CPU_MSG_INT,     19)
ENUM_VAL(VID_NCB0_INT,        20)
ENUM_VAL(VID_NCB1_INT,	      21)
ENUM_VAL(VID_GRP0_FCMD_CPL,   22)
ENUM_VAL(VID_L2P_Q1_SRCH_RSLT,23)
ENUM_VAL(VID_L2P_Q23_UPDT_RSLT,24)
ENUM_VAL(VID_CMDPROC_P1,      25)
ENUM_VAL(VID_CMDPROC_P0,      26)
ENUM_VAL(VID_GRP1_FCMD_CPL,   27)
ENUM_VAL(VID_NCL_FTL_OTHERS,  28)
ENUM_VAL(VID_IPC,             29)
ENUM_VAL(VID_TIMER,           30)
ENUM_VAL(VID_SYSTEM_MISC,     31)

END_ENUM(vic_vid_t)

BEGIN_ENUM
ENUM_VAL(SYS_VID_CHIP_RST,             0)
ENUM_VAL(SYS_VID_PRESET_ASEERT,        1)
ENUM_VAL(SYS_VID_PRESET_DEASSERT,      2)
ENUM_VAL(SYS_VID_WARM_RESET,           3)
ENUM_VAL(SYS_VID_WDT_1,                4)
ENUM_VAL(SYS_VID_WDT_2,                5)
ENUM_VAL(SYS_VID_WDT_3,                6)
ENUM_VAL(SYS_VID_WDT_4,                7)
ENUM_VAL(SYS_VID_WDT_5,                8)
ENUM_VAL(SYS_VID_WDT_6,                9)
ENUM_VAL(SYS_VID_WDT_7,                10)
ENUM_VAL(SYS_VID_WDT_8,                11)
ENUM_VAL(SYS_VID_OTP,                  12)
ENUM_VAL(SYS_VID_GPIO,                 13)
ENUM_VAL(SYS_VID_TRNG,                 14)
ENUM_VAL(SYS_VID_TS,                   15)
ENUM_VAL(SYS_VID_PWR_LOSS,             16)
ENUM_VAL(SYS_VID_FATAL_ERR,            17)

ENUM_VAL(SYS_VID_PCIE_INT0,            20)
ENUM_VAL(SYS_VID_PCIE_INT1,            21)
ENUM_VAL(SYS_VID_MC,                   22)
ENUM_VAL(SYS_VID_UART,                 23)
ENUM_VAL(SYS_VID_SPI,                  24)
ENUM_VAL(SYS_VID_SMB0,                 25)
ENUM_VAL(SYS_VID_SMB1,                 26)

ENUM_VAL(SYS_VID_ST,                   30)
ENUM_VAL(SYS_VID_WDT_RST,              31)

END_ENUM(vic_svid_t)
