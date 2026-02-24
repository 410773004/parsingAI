//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 SSSTC
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from SSSTC.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from SSSTC.
//-----------------------------------------------------------------------------
//#if defined(RAWDISK)
#if !defined(PROGRAMMER)
#pragma once

#include "nvme_precomp.h"
#include "req.h"
#include "nvmet.h"
#include "cpu_msg.h"
#include "spi.h"
#include "misc_register.h"
#include "misc.h"
#include "mpc.h"
#include "ddr.h"
//#include "sw_ipc.h"


typedef struct
{
	union {
	    u32 all;
        struct{
            u32  VscFunction :8;   ///< DW12 Byte 0 Function
            u32  VscMode :8;         ///< DW12 Byte 1 Mode
            u32  Dw12Byte2 :8;      ///< DW12 Byte 2 Other
            u32  Dw12Byte3 :8;      ///< DW12 Byte 3 Other
        }b;
	};
}tVSC_CMD_Mode;

typedef struct
{
	req_t *ec_req;
	struct nvme_cmd *ec_cmd;
	u16 ec_byte;
}tVSC_pl;


typedef struct
{
	union {
        u32 all;
        struct{
            u32  ch        :4;   ///< DW13 Byte 0 CECH
            u32  ce        :4;
            u32  lun       :4;   ///< DW13 Byte 1 Mode
            u32  plane     :4;
            u32  Dw12Byte2 :8;
            u32  Dw12Byte3 :8;
        }b;
        struct{
            u32  opt           :8;   
            u32  pages_cnt     :8;   
            u32  start_page    :16;
        }ev_log;
	};
}tVSC_DW13_Mode;//albert 20200622 for VU DW13

typedef struct
{
	union {
        u32 all;
        u32 ev_log_pda;
	};
}tVSC_DW14_Mode;


//-----------------------------------------------------------------------------
//  Vsc Feature define:
//-----------------------------------------------------------------------------
#define VSC_CUSTOMER_ENABLE	// FET, RelsP2AndGL

//-----------------------------------------------------------------------------
//  Vsc Function define:
//-----------------------------------------------------------------------------
#define Vsc_NonSuppor                    0x00
#define Vsc_DriverReg                    0x0F
#define Vsc_DeviceHWCheck                0x10
#define Vsc_DeviceFWCheck                0x11
#define Vsc_SysInfoOperation             0x12
#define Vsc_PurageOperation              0x13
#define Vsc_AgingOnlyOperation           0x14
#define Vsc_FTLRelatedOperation          0x15
#define Vsc_InternalTableOperation       0x16
#define Vsc_DebugOperation               0x17
#define Vsc_NVMeAlternative              0x18
#define Vsc_OEMSpecific_1                0x20
#define Vsc_DebugHandleOperation         0x7F

#if defined(DRAM_Error_injection)
#define Vsc_Error_injection_1bit             0x21
#define Vsc_Error_injection_2bit             0x22
#endif

//-----------------------------------------------------------------------------
//  Vsc Mode define:
//-----------------------------------------------------------------------------
#define VSC_READSOCReg              0x00
#define VSC_READNORData             0x10
//VscDeviceHWCheck
#define VSC_READFLASHID             0x00
#define VSC_READNORID               0x01
#define VSC_PCIE_CONFIG             0x10
#define VSC_PCIE_VID_DID            0x11
#define VSC_CHECK_PLP               0x20
#define VSC_CHECK_LED               0x22
#define VSC_READ_PCB                0x30
#define VSC_CHECK_SOC               0x40
#define VSC_VPD_INIT                0x50
#define VSC_Read_DramTag            0x51
#define VSC_VPD_WRITE               0x58
#define VSC_VPD_READ                0x59
#define VSC_READ_DRAM_DLL           0x60
#define VSC_CHECK_SCP               0x70
#define VSC_CHECK_Temperature       0x71
#define VSC_SOC_UID                 0x73
#define VSC_TEST_LED                0x90
//VscDeviceFWCheck
#define VSC_READFW_CONFIG           0x00
#define VSC_READ_FW_MODEL           0x40
#define VSC_READ_BOOT_VER           0x80
//SysInfo Read / Write Operation
#define VSC_SI_READ                 0x00
#define VSC_SI_WIRTE                0x01
#define VSC_SI_BACKUP_TO_NOR        0x02
//Purge Operation
#define VSC_PU_PREFORMAT            0x00
#define VSC_PU_FOBFORMAT            0x01
#define VSC_PU_SETTEMP              0x02  	// linchen20151130, for Thermal Profile Test
#define VSC_PU_PREFORMAT_DROP_P2GL  0x03  	// FET, RelsP2AndGL, Paul_20210413
#define VSC_GC_CONTROL              0x04
#define VSC_PU_NorWriteProtect      0x10
#define VSC_AG_ERASEALL             0x11
#define VSC_AG_ERASENAND            0x20
#define VSC_DENY_VSC           		0x30
#define VSC_UART_DISABLE_FAKE      	0x40
#define VSC_UART_DISABLE        	0x50
//VscAGINGOperation
#define VSC_ERASE_NANDFLASH         0x00
#define VSC_SCAN_DEFECT             0x01
#define VSC_AGING_BATCH             0x02
#define VSC_PLIST_OP                0x03
#define VSC_DRAM_DLL                0X04
#define VSC_PROG_OTP                0x05 //force prog to 0x0fffffff _GENE_20210906
#define VSC_CLEAR_DRAM		        0x0A
#define VSC_SRB_ERASE		        0x0B	//20201014-Eddie
#define VSC_DRAM_SET_CLK            0x0C
#define VSC_BUSYTIME_OP             0x10
#define VSC_SET_TMT                 0x12
#define VSC_CHANGE_ODT              0x13 //[20260127-Erin]for change odt
//FTL-Related Operation
#define VSC_DISK_LOCK               0x00
#define VSC_NAND_FLUSH_CNT          0x08
#define VSC_ECC_INSERT              0x10
#define VSC_ECC_DELETE              0x11
#define VSC_ECC_RESET               0x12
#define VSC_ECC_RC_REG              0X13  	//20210108 tony test
#define VSC_ECC_DUMP_TABLE          0X14  	//20210108 tony test
//VscInternalTableOperation
#define VSC_READ_ERASE_CNT          0x00
#define VSC_READ_VAILD_CNT          0x01
#define VSC_READ_GLIST              0x02
#define VSC_READ_ECCTB              0x03
#define VSC_CHECK_DEFECT	    	0x05
#define VSC_CLEAR_GLIST             0x10
#define VSC_CLEAR_SMART             0x12
#define VSC_REG_GLIST				0x13	// FET, VSCRegGL

//VscDebugOperation
#define VSC_PTF_INFO                0x00
#define VSC_EV_LOG                  0x01
#define VSC_SAVE_LOG                0x10
#define VSC_LOAD_LOG                0x11
#define VSC_PCIE_RX_EYE             0x12
#define VSC_PCIE_RX_Margin			0x13
#if(SPOR_L2P_VC_CHK == mENABLE)
#define VSC_L2P_VC_CHK	     		0x14
#define VSC_L2P_VC_CHK_RESULT		0x15
#endif
#define VSC_CHECK_OTP	     		0x16 //_GENE_20211007
#define VSC_PCIE_RETRAIN     		0x17 // Jack_20220504
#define VSC_SCAN_WRITTEN	     	0x20
#define VSC_LDR_UPDATE_ALWAYS       0x30

//VscNVMeAlternative
#define VSC_SECURITY_SEND           0x00
#define VSC_SECURITY_RECEIVE        0x01
#define VSC_Set_FW_CA               0x02
#define VSC_FLUSH                   0x10
#define VSC_NVMe_Format             0x20
#define VSC_NVMe_DynamicOP          0x21
#define VSC_FW_CHECK_DISABLE        0x40
//VscOEMSpecific_1
#define VSC_EN_DIS_TCG              0x00
#define VSC_Verify_Psid             0x01
#define VSC_ENABLE_SECURE_BOOT      0x02
#define VSC_SET_MTD_01_TO_38      	0x10
#define VSC_READ_MTD_01_TO_38      	0x11


//-----------------------------------------------------------------------------
//  Read EC table define:
//-----------------------------------------------------------------------------
#define Read_Payload 				0x00
#define Read_header 				0x01
#define HeaderSize 					0x1F
#define PayLoadSize 				0x17F4  //6byte*1022entry  //0xF4C

/*! \file smart.h
 * @brief ssstc command
 *
 * \addtogroup decoder
 * \defgroup smart
 * \ingroup decoder
 * @{
 */

#if !defined(PROGRAMMER)
enum cmd_rslt_t nvme_vsc_ev_log(req_t *req, struct nvme_cmd *cmd, u16 bytes);
enum cmd_rslt_t Read_Glist_Table(req_t *req, struct nvme_cmd *cmd, u16 byte);
#endif//dump/save log read glist Young add 20210714 

#ifdef VSC_CUSTOMER_ENABLE	// FET, RelsP2AndGL
/*!
 * @brief Vendor F0 command, 
 *	list only the VscFunc/ VscMode released to customer, and the listed cases here should be included in VSCCmd FC/ FD/ FE.
 *
 * @param param		NVMe cmd 16DW
 * @param sts		None
 *
 * @return		cmd_rslt_t
 */
enum cmd_rslt_t nvmet_ssstc_vsc_f0cmd(req_t *req, struct nvme_cmd *cmd);
#endif

/*!
 * @brief VSC FC command
 *
 * @param NVMe cmd 16DW
 *
 * @return	cmd_rslt
 */
enum cmd_rslt_t nvmet_ssstc_vsc_fccmd(req_t *req, struct nvme_cmd *cmd);

/*!
 * @brief VSC FD command
 *
 * @param NVMe cmd 16DW
 *
 * @return	cmd_rslt
 */
enum cmd_rslt_t nvmet_ssstc_vsc_fdcmd(req_t *req, struct nvme_cmd *cmd);

/*!
 * @brief VSC FE command
 *
 * @param req	request
 * @param NVMe cmd 16DW
 *
 * @return	cmd_rslt
 */
enum cmd_rslt_t nvmet_ssstc_vsc_fecmd(req_t *req, struct nvme_cmd *cmd); //20200526 Alan Lin Modify

extern void nvmet_alloc_admin_res(req_t *req, u32 size);
extern enum cmd_rslt_t nvmet_map_admin_prp(req_t *req, struct nvme_cmd *cmd, u32 len, void (*callback)(void *hcmd, bool error));
extern bool hal_nvmet_data_xfer(u64 prp, void *sram, u16 len, dir_t dir,
		    void *hcmd, void (*callback)(void *hcmd, bool error));
extern u32 ts_get(void);
extern u16 smb_tmp102_read(u8 addr_w, u8 addr_r);
//extern u32 VPD_blk_write(u8 cmd_code);
extern void Fake_table_init(void);
extern void Get_EC_Table(u32 flags, u32 vu_sm_pl);


//20200805 kevin add vsc preformat 
void vsc_preformat(req_t *rq, bool ns_reset);
fast_code bool vsc_preformat_done(req_t *req);
#ifdef TCG_NAND_BACKUP
void tcg_preformat_continue(void);
#endif
//20200616 Albert add to ssstc_cmd.h for dw13


//20200603 Albert Add for FW model

struct __attribute__((packed)) FWmodel_data
{
	//temporary ENZAG010
	u8			internal_modelname[8];
	// u8          sub_modelname; //default "0"
};

struct pcie_rx_eye_info_t {
    u8	x1[96];
    u8	x2[96];
    u8	x3[96];
    u8	x4[96];
};

struct __attribute__((packed)) flashid_data 
{  
	//20200525 Alan Lin Add for Flash ID
	//20200603 Albert add to NAND.C

	struct __attribute__((packed)) flashid_cnt_data
	{
 	//TODO: Modify the structure according to all CE and CH
		u8	channel;
		u8	device;
		u16 reserve0;
		u8	flashID[6];
		u16 reserve4;
		u8	UID;
		u8	reserve1[3];	
	}flashinfo[128];

	u32 reserve2[504];

	u8 total_ch;//address 0xFE0
    u8 total_dev;

    u16 reserve3[15];

};

//struct flashid_cnt_data *getflashid;

//20200616 Albert add to ssstc_cmd.h
struct __attribute__((packed)) PCIE_Status

{
	u32 GEN   :8;
	u32 LANES :8;
	u32 res   :16;

    	};

struct __attribute__((packed)) PCIE_ID

{
    union
    {
        u64 all;

        struct
        {
            u32 pVID    :16;
	        u32 pDID    :16;
			u32 sub_VID :16;
			u32 sub_ID  :16;
        }b;
    };

};

struct __attribute__((packed)) FW_Config 
{  

	//20200615 Albert add to ssstc_cmd.h
	u8 SSSTC_flag[6];
	u8 NVME_flag[4];
	u8 VSC_flag[4];
	u8 res[2];
	u8 ASIC_Nickname[16];
	u8 ASIC_Rev[16];
	u8 Aging_flag[16];
	u8 Aging_Rev[8];
	u8 Is_Aging_FW[8];

	u32 VSC_Buffer_Size[4]; //in Bytes	0x040
	u32 SysInfo_Buffer_Size[4]; //in Bytes

	u8 PCBA_SN[16];
	u8 Drive_SN[20];
	u8 reserve0[12];
	u8 FW_SVN_Rev[16];
	u8 reserve1[16];


	u8  FATAL_ERROR_flag[16];//0x0C0
	u32 ASIC_ErrCode;
	u32 NAND_ErrCode;
	u32 DRAM_ErrCode;
	u32 SPI_ErrCode;
	u32 Others_ErrCode;
	u8  reserve2[28];

	u8  Cluster_flag[16];//0x100
	u32 Cluster_Type;
	u8  reserve3[12];
	u32 Cluster_Data_CNT;
	u32 Cluster_Current_CNT;
	u32 Cluster_User_CNT;
	u32 Cluster_Size; //in MB
	u32 Erase_CNT_Entry_Size; //in Bytes
	u8  reserve4[12];
	u32 Valid_CNT_Entry_Size; //in Bytes
	u8  reserve5[60];

	u8  NAND_Flash_flag[16];//0x180
	u8  NAND_FlashID[16];
	u32 CH_CNT;
	u32 CE_CNT;
	u32 LUN_CNT;
	u32 DIE_CNT;
	u32 Write_Plane;
	u32 Read_Plane;
	u32 LAA_Size; //in Bytes
	u8  reserve6[4];
	u32 page_size;
	u32 page_cnt;
	u32 write_size;
	u8  reserve7[20];

	u8  DRAM_flag[4];
	u8  reserve8[12];
	u32 ddr_size;
	u32 cs;
};//1K

typedef struct BootCode{
	u8 Boot_version[8];
	u64 rsv;
	u8 Main_Version1[8];
	u8 Main_Version2[8];
}BootCode;

//for normal test VU
#if 1	
typedef struct NormalTest_Map{
	u8 pcba_serial_number[16];
	u8 drive_serial_number[20];

	//two base use for EC table read 
	u32 HEADER_BASE[4];
	u32 PAYLOAD_BASE[256];
}NormalTest_Map;
#endif

//for read system info

typedef union {
	u8			all[4];
	struct {
		u8 	ch;	//	  
		u8 	ce;	//	 
		u8 	lun; //	   
		u8 	cnt; //	?   

	} d;
} stNOR_SUBHEADER_DW4;

typedef union {
    u8          all[32];
    struct {
        u32     dwTag;          //     4B
        u32     dwVerNo;        //     4B
        u16     wSize;          //     2B sizeof(stNOR_MAINHEADER)
        u16     wSubHeaderSize; //     2B sizeof(stNOR_SUBHEADER)
        u16     wSubHeaderCnt;  //     2B
        u16     wReserved;      //     2B
        u32     dwAsicId;       //     4B
        u32     rev[3]; 		//     44B
    } d;
} stNOR_MAINHEADER;

typedef union {
    u8                      all[64];
    struct {
        u32                 dwTag;          //     4B
        u32                 dwVerNo;        //     4B
        u32                 dwDataSize;     //     4B
        u32                 dwDataOffset;   //     4B
        stNOR_SUBHEADER_DW4    dwDWord4;       //     4B
        u32                 dwDWord1[3];    //    12B
        u32                 dwDWord2[7];    //    28B
        u32                 dwValidTag;     //     4B
    }d;
} stNOR_SUBHEADER_D;

typedef union {
    stNOR_SUBHEADER_D        All[7];
    struct {
        stNOR_SUBHEADER_D    BitMap;
        stNOR_SUBHEADER_D    TestMap;
        stNOR_SUBHEADER_D    P1table;
        stNOR_SUBHEADER_D    P2table;
        stNOR_SUBHEADER_D    CTQmap;
        stNOR_SUBHEADER_D    AgingMap;
        stNOR_SUBHEADER_D    AgingECC;
    } d;
} stNOR_SUBHEADER;

//pochune
typedef union{

		struct{
			u16 b0 :8;
			u16 b1 :8;
		}b;
		u16 all;

}debug_log_t;
//pochune
typedef struct {
	debug_log_t log_seq[256];

}DebugLogHeader_t;


typedef union
{
	u8  all[4096];       //stNOR_HEADER => 4 KB
	struct {
        stNOR_MAINHEADER     MainHeader;
        stNOR_SUBHEADER      SubHeader;
        u8                FW_slot;
        u8                rev[3];
        u32               Fw_upd_flag;
        u32               failed_slot;
 	} d;

} stNOR_HEADER;

typedef union
{
    u8 btData;
    struct
    {
        u8 bAgingEnabled           : 1;    // 0: disable   1: enable
        u8 bAgingMode              : 3;    // mtMODE_AGING_RUN_MODE
        u8 bRsv                    : 1;
        u8 bEraseAllBlocksAtStart  : 1;
        u8 bEraseAllBlocksAtEnd    : 1;
        u8 bAgingIsComplete        : 1;
    } bits;
} mtAging_Mode;

typedef struct
{
    int pstr;
    int nstr;
}tPAD;//8B

typedef struct
{
    u16 level_delay[5];
    u16 WRLVL_rsv;
}tWRLVL;//12B

typedef struct
{
    u8 rdlat;
    u8 rcvlat;
    u16 RDLAT_rsv;
}tRDLAT;//4B

typedef struct
{
    u8 vgen_range_final;
    u8 vgen_vsel_final;
    u8 worst_range_all;
    u8 phase0_pre_final[5];
    u8 phase1_pre_final[5];
    u8 phase0_range[5];
    u8 phase1_range[5];
    u8 phase0_start[5];
    u8 phase1_start[5];
    u8 phase0_end[5];
    u8 phase1_end[5];
    u8 RDEYE_rsv;
}tRDEYE;//44B

typedef struct
{
    u8 vref_range:1;
    u8 vref_value:6;
    u8 rsv:1;
    u8 vref_norm;
    u8 range1_pass_start;
    u8 range1_fail_start;
    u8 range2_pass_start;
    u8 range2_fail_start;
    u16 WRVREF_rsv;
}tWRVREF;//8B

typedef struct
{
    int ByteX_Eye_Size_Byte[5];
    int ByteX_offset[5];
    u8  Right_start[5];
    u8  Right_end[5];
    u8  Left_start[5];
    u8  Left_end[5];
}tWRDESKEW;//60B

typedef struct
{
    u8 window_size;
    u8 is_push;
    u8 push_delay;
    u8 pull_delay;
    u8 phase0_post_final[5];
    u8 phase1_post_final[5];
    u16 RDLVLDPE_rsv;
}tRDLVLDPE;//16B


typedef union
{
    u8  bArray[256];
    struct
    {
        tPAD        pad;//8
        tWRLVL      wrlvl;//12
        tRDLAT      rdlat;//4
        tRDEYE      rdeye;//44
        tWRVREF     wrvref;//8
        tWRDESKEW   wrdeskew;//60
        tRDLVLDPE   rdlvldpe;//16 
        u8          rsvd[104];
    }element;    
}tDRAM_Training_Result;


typedef union
{
    u8 btData;
    struct
    {
        //u8 Reserved           : 5;
        u8 Reserved           : 4;
        u8 bLedOutfloating    : 1;
        u8 bRepeatPattern     : 1;
        u8 bLedOutEnabled     : 1;  // 0:debug message , 1:Led out
        u8 bAutoDisable       : 1;
    } bits;
} mtAging_Setting;

typedef union
{
    struct
    {
        u32    lba;
        u32    Reserved;     
    } lba;
    struct
    {
        u8     channel;
        u8     device;
        u16    block;
        u16    page;
        u8     ecccount;
        u8     loop;
    } cdb;
} mtAging_Location;

typedef struct
{
    u16        Loop;
    u16        BadBlockNo;
} mt_AGING_Count;

typedef enum{
    emAllError = 0,
    emEraseFail,
    emProgFail,
    emWLleakage,
    emUnCorrt,
    emECCOverLmt,
    emBlankPage,
    emWLShort,
    emWLOpen,
    emFakeBlank,
    emSignal,
    emCRCError,
    emENCOverlimit,
    emENCUncorr,
    emRsv0E,
    emRsv0F,
    AGING_ERR_TYPE_NUMBER
} emAgingErrType;

#define _SEPARATE_ERR_TYPE_FOR_GROWN_DEFECT
#ifdef _SEPARATE_ERR_TYPE_FOR_GROWN_DEFECT

#define I2C_TEMPTURE_DETECT
#define SAVE_DVIEW_LOG

#define SIGNATURE_AGING           ('A'+('G'<<8)+('I'<<16)+('N'<<24))
#define SIGNATURE_PLDS            ('P'+('L'<<8)+('D'<<16)+('S'<<24))
#define SIGNATURE_STATION1        ('T'+('S'<<8)+('T'<<16)+('1'<<24))
#define SIGNATURE_STATION2        ('T'+('S'<<8)+('T'<<16)+('2'<<24))
#define SIGNATURE_AGINGVER        ('A'+('V'<<8)+('E'<<16)+('R'<<24))
#define AGING_VERSION             ('L'+('9'<<8)+('0'<<16)+('J'<<24))
#define PLAIN_PSID                ('P'+('L'<<8)+('P'<<16)+('D'<<24))
#define DIGEST_PSID               ('D'+('G'<<8)+('P'<<16)+('D'<<24))

#define AGING_NORMALIZE_LOOP   64
typedef struct
{
    u16  wDefectCnt[AGING_NORMALIZE_LOOP];
}stGROWNDCFT_ERRTYPE;
#endif

typedef enum{
    emtPROG_AVG = 0,
    emtR_AVG,
    emtBERASE,
    emtPROG_MAX,
    emtR_MAX,
    emtBERASE_MAX,
    emtPROG_LOWER_AVG,
    emtPROG_EXTRA_AVG,
    emtPROG_UPPER_AVG,
    emtR_LOWER_AVG,
    emtR_MIDDLE_AVG,
    emtR_UPPER_AVG,
    emBusyTimeRsv0C,
    emBusyTimeRsv0D,
    emBusyTimeRsv0E,
    emBusyTimeRsv0F,
    NUMBER_OF_NAND_BUSY_TIME
} emNANDBusyTimeType;


typedef struct {		
	u8 lane0_leq;
	u8 lane1_leq;
	u8 lane2_leq;
	u8 lane3_leq;
	u8 lane0_dfe;
	u8 lane1_dfe;
	u8 lane2_dfe;
	u8 lane3_dfe;
	u16 pll_band;
	u16 lane0_cdr;
	u16 lane1_cdr;
	u16 lane2_cdr;
	u16 lane3_cdr;
	u8 lane0_dac;
	u8 lane1_dac;
	u8 lane2_dac;
	u8 lane3_dac;
	u8 rx_lane0_margin_right_x;
	u8 rx_lane0_margin_left_x;
	u8 rx_lane0_margin_up_y;
	u8 rx_lane0_margin_down_y;
	u8 rx_lane1_margin_right_x;
	u8 rx_lane1_margin_left_x;
	u8 rx_lane1_margin_up_y;
	u8 rx_lane1_margin_down_y;
	u8 rx_lane2_margin_right_x;
	u8 rx_lane2_margin_left_x;
	u8 rx_lane2_margin_up_y;
	u8 rx_lane2_margin_down_y;
	u8 rx_lane3_margin_right_x;
	u8 rx_lane3_margin_left_x;
	u8 rx_lane3_margin_up_y;
	u8 rx_lane3_margin_down_y;	
	u8 reserved_para[26];
} pcie_test_para;

typedef union {
    u32 all;
    struct {
        u32 ddr_test0_loop_done:1;
        u32 ddr_test1_loop_done:1;
        u32 rsvd_2:2;
        u32 ddr_test_loop_unit:3;
        u32 rsvd_7:5;
        u32 ddr_test0_err_up2_adr:2;
        u32 ddr_test1_err_up2_adr:2;
        u32 ddr_test0_err_count:8;
        u32 ddr_test1_err_count:8;
    } b;
} tDDR_Test_Result;

typedef struct {
    u32        aging_signature;                 //4B
    mtAging_Mode    mode;                       //1B
    mtAging_Setting settings;                   //1B
    u16        PLP_DischargeTime;               //2B
    u32        current_loops;                   //4B
    u32        loops_spec;                      //4B
    u8         reread_OK;                       //1B u32        max_retries; //0x10010
    u8         retry_1B_OK;                     //1B
    u8         retry_2B_OK;                     //1B
    u8         retry_2B_NG;                     //1B
    u8         PLP_enable;                      //1B
    u8         PLP_mode;                        //1B
    u16        PLP_TargetTime;                  //2B
    //u32        FakeBlankCnt;                    //4B //rsvd_x3;//power_cycles_spec;
    u16        reserved0;
	u16		   Vsc_tag;                        //2B control Vu cmd on/off
    u8         SOC_temperature;               //1B   aging record Max temperature
    //u8         Self_Run_Mode;                   //1B  0x1001D                           //accumulated 30B
    u8         sensor_temperature;
    struct
    {
        u8 enabled;
        u8 threshold;
    } ECC;                                                                              //accumulated 32B
    struct
    {
        u16    error_code;
        u16    current_stage;
        u32    elapsed_time;
    } result;                                   //accumulate 40B

    mtAging_Location location;                  //accumulate 48B

    struct
    {
        u32    write;
        u32    read;
        u32    erase;
        u32    erase_not_clean;
    } total_errors;
    struct
    {
        u32    written;    //unit : page
        u32    read;       //unit : page
        u32    erased;     //unit : block
        u32    erase_not_clean;   //unit : page
    } total_access;
    struct
    {
        u32    preliminary;
        u32    totaldefect;
        u32    grown;
    } defect_count;
    u32        Total_ECC_Count;                           //florence 8/4/2020 //accumulate 96B
    struct
    {
        u16     avg_preliminary;
        u16     avg_totaldefect;
        u16     avg_grown;
    } avg_defect_count;

    u16         maxDefectCount;
    u32        Power_Cycle; //0x10068             //florence 8/4/2020
    u32        Power_Cycle_Spec;                                               //accumulate 112B
    /*u8         Mode;
    u8         TrkID;
    u8         DescTot;
    u8         DescProc;
    u32        TrkStatus;
    u32        TrkStatus2;
    u32        TrkDesDone;*/
	err_t	   Pgr_err[8]; //reserved1[16]; // Aging Map 0x70 // Jimmitt 20'11/18'
    u16        AgMaxBlockCnt;
    u8         reAGINGcnt;
    u8         ScanDefectFlag;// 1st time ScanDefect
    u16        MaxBlockCnt;                         //accumulate 134B
    u8         DefectCHMap[8];
    u8         NANDTemper_check;
    u8         PD_LED_Flag;   // close log , use TX to show LED     //accumulate 144B
    //================================================================================//
#if 0
    //past project parameter. Not used(40B)
	// [Motivation1. 20161107] *
    #ifdef BC_DRAM_TEST
    u8         bDramExecuted;
    u8         bDramTestFailed;
    u16        wFailedCase;
    u32        dFailAddr;
    u32        dCorrPattern;
    u32        dReadPattern;
    #else
    u8         reserved7[16]; //
    #endif
	// [Motivation1. 20161107] &
    u32        rsvd_x7;
    //u16        rsvd_x8;
    u32        rsvd_x8;
    //u8         bWrScanFlag;
    //u8         bWrLimitTag;
/*
    u8         WLEnable;     // 0x100A0
    u8         E2P_CCT_EN;    // 0x100A1
    u16        CCT2_PEcnt;  //0x100A2~3
    u16        CCT3_PEcnt;  //0x100A4~5
    u8         CCT1ChkSumErr;  //100A6
    u8         CCT3ChkSumErr;  //100A7
*/
    // [Motivation.1&3 2017/03/01] *
    #if (defined(DLL_SELF_TUNING) && defined(CALIBRATION_PAD_CONTROL))
    u8         bDLLSignature; //0x100A8
    u8         bDLLPos_opt;
    u8         bDLLNeg_opt;
    u8         bDLLAvg;
    u8         bZPR;          //0x100AC
    u8         bZNR;
	//Andy 20190527 add"Peter Write margin check"
    u8         bWrMarginTag; //Peter add for DDR write margin test flow
    u8         bDLLMarginTag;
    #elif defined(DLL_SELF_TUNING)
    u8         bDLLSignature; //0x100A8
    u8         bDLLPos_opt;
    u8         bDLLNeg_opt;
    u8         bDLLAvg;
    u8         bDLLMarginTag;
    u8         reserved8[3];
    #elif defined(CALIBRATION_PAD_CONTROL)
    u8         reserved8[4]; //0x100A8
    u8         bZPR;         //0x100AC
    u8         bZNR;
	//Andy 20190527 add"Peter Write margin check"
    u8         bWrMarginTag; //Peter add for DDR write margin test flow
    u8         bDLLMarginTag;
    #else
    u8         reserved8[8];
    #endif
#endif
    //================================================================================//
    // [Motivation.1&3 2017/03/01] &
    u8         rework_ver[8]; //rework versio          accumulate 152B
    //u16        WWN[4];     // 2010-11-01 PM: For INTEL World Wide Name
    u32        soc_uid[2];
    u8		   NandFlashID[6];           // Aging Map 0xA0      //Albert 20201023
	u8         PLP_type;
	u8         reserved2[9];
    u8         EUI64[16];                // Aging Map 0xB0      //EUI64 16 bytes(including register format)
    u8         drive_serial_number[32];  // Aging Map 0xC0
    u8         pcba_serial_number[32];   // Aging Map 0xE0      //accumulate 256


    struct
    {
        struct
        {
            u32    signature;
            struct
            {
                u8 station[12];
            } error_code;
        } station1;
        struct
        {
            u32    signature;
            struct
            {
                u8 station[12];
            } error_code;
        } station2;
    } test_station;                                                         //accumulate 288B

    //u8       smart_data[64];
    u32    AgVersignature;       //AVER
    u16    PLPfailCurrent;            //0x00000000
    u16    LastBlockDefectIndex; //if last 2 block are defect, marked as 0x5A5A, 20121228 should not be used
    u32    AgingVer;             //0x00000001
    u16    AgingCollectDataTemp[1];           // 2 B
    u8     AgingOnlyDefectCH;
    u8     reserved912F;                                                    //accumulate 304B
    mt_AGING_Count  AgingData[436];     // 4B * 436 = 1744 B                //accumulate 2048B

    u32    PPID[120];                                                       //James_20240123 accumulate 4*120B
    u8     PSID[32];                                                        //James_20240123 accumulate 1*32B
    u32    DCCTrainFailCnt;
    u32    rsvd_x9_2;

    u32    dPlistFinalLoop;            // Final loop number with glist
	u32	   PSID_tag;				   //James_20240123 
    u16    uart_dis;
    u8     Disk_PhyOP;  // rsvd_x12 AlanHuang                                                   //accumulate 2547B

    u8     AgStressModeEnable;
    u32    dECVacAccess;               //    4B, For Get EC, Vac Vender Command Access Check.//accumulate 2584B   OK

    //================================================================================//
#if 0
    //past project parameter. Not used(8B)
    // Peter Add for saving ddr para
    u8 		bDLLPos_Max;
    u8 		bDLLPos_min;
    u8 		bDLLNeg_Max;
    u8 		bDLLNeg_min;
    // Peter change the place to here
    u8         bWrScanFlag;
    u8         bWrLimitTag;
    u16     reserved11;
    //u32     reserved11;
#endif
    //================================================================================//

/*
    u8         retry_AUX_OK;          //kuanyu
    u8         retry_AUX_NG;          //kuanyu
    u8     AgRelaxProgB9Enable;
*/
    u8     bPListFinalLoop[AGING_ERR_TYPE_NUMBER]; //0x9A20~0x9A2F: 16 bytes   (16 bytes * 1 Row)
    stGROWNDCFT_ERRTYPE  stGrnDfctErrType[8];                          //0x9A30~0x9E2F: 1024 bytes (16 bytes * 64 Rows)         //accumulate 3624B  checked
   #ifdef _HWCFG_BY_EEPROM
    u8     bCFG_CH;                 //0x9E30
    u8     bCFG_CE;                 //0x9E31
    u8     bCFG_FLASHIDBYTE;        //0x9E32
    u8     bCFG_FLASHID[6];         //0x9E33~0x9E38
    u8     bCFG_Rsv[7];             //0x9E39~0x9E3F
        #ifdef EN_PLP_DISCHARGE_TEST
            u8     reserved13[422];
            u16    CapacityDischargeEnable; //2B        0x9FE6
            u16    CapacityDischargeCounter;//2B        0x9FE8
            u16    CapacityDischargeTimeUs[3];//2B    0x9FEA~0x9FEF
        #else
            u8     reserved13[432];                                     //0x9E40~0x9FEF: 432 bytes  (16 bytes * 27 Rows)
        #endif
   #else
        #ifdef EN_PLP_DISCHARGE_TEST
            u8     reserved13[438];
            u16    CapacityDischargeEnable; //2B        0x9FE6
            u16    CapacityDischargeCounter;//2B        0x9FE8
            u16    CapacityDischargeTimeUs[3];//2B    0x9FEA~0x9FEF
        #else
            #ifdef SAVE_DVIEW_LOG
                        //Andy add busy time in EPM
              #if 1//def _MEASURE_NAND_BUSY_TIME
            u16    wNANDBusyTime[NUMBER_OF_NAND_BUSY_TIME];    //9E30~9E4F                  //accumulate 3656B
                        //Andy 20190812
                        ////Average tBErase;   //9E30~9E31
                        ////Average tPROG;     //9E32~9E33
                        ////Average TR_lower;  //9E34~9E35
                        ////Average TR_middle; //9E36~9E37
                        ////Average TR_upper;  //9E38~9E39
                        ////Reserve
                        ///////////////  old  /////////////////////////
                        ////tPROG_AVG;        //9E30~9E31
                        ////tR_AVG;           //9E32~9E33
                        ////tBERSE_AVG;       //9E34~9E35
                        ////tPROG_LOWER_AVG;  //9E36~9E37
                        ////tPROG_FOGGY_AVG;  //9E38~9E39
                        ////tPROG_FINE_AVG;   //9E3A~9E3B
                        ////tR_LOWER_AVG;     //9E3C~9E3D
                        ////tR_MIDDLE_AVG;    //9E3E~9E3F
                        ////tR_UPPER_AVG;     //9E40~9E41
                        ////Rsv;              //9E42~9E4F
              #else
            u16    reserved12[16];
              #endif
            u8     bPListFirstLoop[AGING_ERR_TYPE_NUMBER];     //9E50~9E5F: 16 bytes (16 bytes * 1 Row)               accumulate 3672B
            //u32    LogTotalData;     //9E60~0x9E63
            ////Peter Add DDR data////
            //u8     Wr_DQ_range;
            //u8     Wr_DQS_range;
            //u8     DLL_Pos_range;
            //u8     DLL_Neg_range;
            //Peter Add DDR data End//
            u32 MinDLL_Die;// = (min_ch << 16) | (min_ce << 8) | (min_lun);
            u32 MinDLL_Result;//20201116-Ma
            u16    PLP_inter_flag;
            u16    PLP_inter_target;
            u8     bSocType;          // 9E6C
            //u8     reserved_1[6];   //9E68~0x9E6F
            u8         Self_Run_Mode;      
#ifdef I2C_TEMPTURE_DETECT

//////Eric

            /*u8     sensor_temperature;     //9E6D                accumulate 3686B
            //......u8     reserved_2[114];     //9E6E~9EDF
			u8     sensor_Temperature_IC1;
			u8     sensor_Temperature_IC2;
			u8     sensor_Temperature_IC3;
			u8 reserved_2[15];*/
			u8 reserved_3[18];
			pcie_test_para PCIE_para;                    //accumulate 3768B
#else
            u8     reserved_3[115];     //9E6D~9EDF
#endif
            ////////////////Andy use for Sorting///////////////////////////
            u32    Worse_Die;     //Each bit means 1 die
            u8     Worse_Die1;     //CD1920A needs 35 bits
            u8     Sorting_flag;   //Start test SLC sorting, if have flag no sorting
            u8     Sorting_end;    //SLC sortint PE 1k end
            u8     Worse_Read_End;     //9EE0~0x9EE7
            u8     Aging_versiontag;
            u8     AgingFW_OEM5;
            u8     AgingFW_OEM6;
            u8     reserved4[5];




			u16    PRCS0;
			u16    PRCS1;
			u16    PRCS2;
			u16    PRCS3;


            u8    PRCS0_CornerType;
			u8    PRCS_reserve0[3];
            u8    PRCS1_CornerType;
			u8    PRCS_reserve1[3];
            u8    PRCS2_CornerType;
			u8    PRCS_reserve2[3];
            u8    PRCS3_CornerType;
			u8    PRCS_reserve3[3];
                                                   //accumulate 3796B
            /////////////////Andy use for sorting End/////////////////////
            tDRAM_Training_Result   dram_set;//LJ1 DDR training result     //accumulate 4052B
            tDDR_Test_Result        ddr_test;
            u8     ddr_test_block_pass;
            u8     ddr_test_block_fail;
            u8     reserved5[10];

            #else
            u8     reserved5[448];
            #endif
        #endif                                     //0x9E30~0x9FEF: 448 bytes  (16 bytes * 28 Rows)
   #endif
    u32    plds_signature;
    u32    HistoryNo;
    u32    HistoryNoInv;
    u32    dChecksum;                             //accumulate 4096
}AGING_TEST_MAP_t;


#define MAX_DEFECT_BLOCK_NUMBER 1104  //276perlun
#define FACTORY_DEFECT_DWORD_LEN	32

typedef struct {
    u32 AgingPlistBitmap[1980][FACTORY_DEFECT_DWORD_LEN]; //_GENE_20200716
    u32 AgingBitMap_Tag;
} AgingPlistBitmap_t;

typedef struct {
    u16 AgingPlistCount[8][8];
    u32 AgingPlistTable[8][8][MAX_DEFECT_BLOCK_NUMBER];
    u32 PlistCheckSum;
    u32 PlistTag;
    u16 Reserved[4];
} AgingPlistTable_t;

typedef struct {
    u32 CTQ_tag;
    u32 PCIe_TX_Impedance;
    u32 PCIe_RX_Impedance;             //..final station
    u32 PCIe_speed_PLL;               //..final station
    u32 PLP_discharge_Time;
	u32 SLC_Read_Time;
    u32 SLC_Program_Time;
    u32 SLC_Erase_Time;
    u32 TLC_Read_Time;
    u32 TLC_Program_Time;
    u32 TLC_Erase_Time;
    u32 Bad_Block_Count;
    u32 NAND_Plist;                   //....average per die
    u32 Max_NAND_Defect_per_Plane;     //....
    u32 NAND_Temperature;
    u32 DDR_Write_Eyes_Margin;        //DQ+DQS
    u32 DDR_Read_Margin_Positive_Range;
    u32 DDR_Read_Margin_Negative_Range;
    u32 DLL_Positive_Optimize_Point;
    u32 DLL_Negative_Optimize_Point;
    u32 DLL_Positive_Max_Position;
    u32 DLL_Negative_Max_Position;
    u32 DLL_Positive_Min_Position;
    u32 DLL_Negative_Min_Position;
    u32 SoC_Temperature;
    u32 SoC_Type;
    u32 CTQ_ZPR;
    u32 CTQ_ZPN;
	u32 Sensor_Temperature;
	u32 reserved;
	u32 ACTQ_tag;
    u32 rsd2[29];
}CTQ_t;

typedef struct {
    u8 loop;
    u8 type;
    u8 Ch;
    u8 CE;
    u32 Row;
	u8 subType;
    u8 Retable;
    u16 WL_num;
    u8 Prefix;
    u8 Retry;
    u16 ECC[4];
    u16 LogicBlk;
    u16 PhyBlk;
    u16 Toggle_ECC[4];   //u8 is not enough
    u32 PDA;
}AgingECC_t;


#define FLAG_AGINGBITMAP  1
#define FLAG_AGINGTESTMAP 2
#define FLAG_AGINGP1LISTTABLE 3
#define FLAG_AGINGP2LISTTABLE 4
#define FLAG_AGINGCTQMAP 5
#define FLAG_ECCTABLE 6
#define FLAG_AGINGHEADER 7

#define AGINGTESTMAP_STARTPAGE           0
#define AGINGBITMAP_STARTPAGE            AGINGTESTMAP_STARTPAGE + sizeof(AGING_TEST_MAP_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1
#define AGINGP1LISTTABLE_STARTPAGE       AGINGBITMAP_STARTPAGE + sizeof(AgingPlistBitmap_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1
#define AGINGP2LISTTABLE_STARTPAGE       AGINGP1LISTTABLE_STARTPAGE + sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1
#define AGINGCTQMAP_STARTPAGE            AGINGP2LISTTABLE_STARTPAGE + sizeof(AgingPlistTable_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1
#define AGINGHEADER_STARTPAGE	         AGINGCTQMAP_STARTPAGE + sizeof(CTQ_t) / (SRB_MR_DU_CNT_PAGE*SRB_MR_DU_SZE) + 1

typedef struct{//32 byte
	u32 tag;
	u16 Type;
	u16 Ver;
	u32 PayLoadLen;
	u32 res;
	u32 MaxEC;
	u32 AvgEC;
	u32 MinEC;
	u32 TotalEC;
}Header_Form;

typedef struct{
	Header_Form header;
	u16 EcCnt[1958];
}Ec_Table;

typedef struct{
	u32 tag;
	u16 Type;
	u16 Ver;
	u32 PayLoadLen;
	u32 res;
	u32 MaxVc;
	u32 AvgVc;
	u32 MinVc;
	u32 res1;
}Vc_header;

typedef struct{
	Vc_header header;
	u32 VcCnt[1958];
}Vc_Table;

typedef struct{
	u32 tag;
	u16 Type;
	u16 Ver;
	u32 PayLoadLen;
	u32 res;
	u16 DefErcnt;
	u16 DefWrcnt;
	u16 DefRdcnt;
	u8 res1[10];
}Glist_header;

typedef struct{
	u8 Type;
	u8 Die;
	u16 Blk;
}Glist_Payload;

typedef struct{
	Glist_Payload Payload_Cnt[2048];
}Glist_Table;

typedef struct{
	u32 tag;
	u8 res[12];
	u16 cnt;
	u8 res1[14];
}ECC_header;

typedef struct{
	u32 LAA;
	u8 BitMap;
	u8 Next;
}PayLoad;

typedef struct{
	u32 cnt1[4];
	PayLoad cnt[1022];
}ECC_Payload;

typedef struct{
	struct
	{
	   u32 lane_number;
	   u16 xleft;
	   u16 xright;
	   u16 rsd1;
	   u16 yleft;
	   u16 yright;
	   u16 rsd2;
	} margin[4];
}rx_lane_margin;

typedef struct{
	u64	TCLNFC;
	u64	SLCNFC;
	u64	TotalNFC;
	u64 res;
}NFCentry;

#endif


