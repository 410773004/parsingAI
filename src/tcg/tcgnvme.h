/*
//-----------------------------------------------------------------------------
//       Copyright(c) 2019-2020 Solid State Storage Technology Corporation.
//                         All Rights reserved.
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from SSSTC.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from SSSTC.
//-----------------------------------------------------------------------------
*/
#if _TCG_ // Jack Li

#pragma pack(1)

//-----------------------------------------------------------------------------
//  Max add for compile
//-----------------------------------------------------------------------------




//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
//  Define & Macros :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type define
//-----------------------------------------------------------------------------
enum opal_cmd {
    IF_SEND = 0x81,
    IF_RECV = 0x82,
};

typedef union
{
    /// DW10
    u32 all;
	
    struct{
    u32 nssf        :  8;    ///< NVMe Security Specific Field (DW10 bits[07:00])
    u32 com_id      : 16;    ///< SP Specific (DW10 bits[23:08])
    u32 protocol_id :  8;    ///< Security Protocol (DW10 bits[31:24])
    }b;

} nvme_tcg_cmd_dw10_t;

typedef union
{
    u32 len_alloc;                 ///< Allocation Length (DW11)
    u32 len_trans;                 ///< Transfer Length   (DW11)    
    
} nvme_tcg_cmd_dw11_t;

typedef struct{
    u16  comId;                    /// 0
    u16  extendedComId;            /// 2
    u32  requestCode;              /// 4
    u8   reserved8;                /// 8
}TCG_Pid02_GetComId_req_t;

typedef struct{
    u16  comId;                    /// 0
    u16  extendedComId;            /// 2
    u32  requestCode;              /// 4
    u16  reserved8;                /// 8
    u16  availableDataLen;         /// 10
    u32  currentStatus;            /// 12
    u8   absTimeOfAllocation[10];  /// 16
    u8   absTimeOfExpiry[10];      /// 26
    u8   timeSinceLastReset[10];   /// 36
    u8   reserved46;               /// 46
}TCG_Pid02_GetComId_Resp_t;


//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//* L0 discovery data structure ----
typedef union{
    u8        Bdata;
    struct{
        u8    sync_support        : 1;   //bit0
        u8    async_support       : 1;   //bit1
        u8    ack_nak_support     : 1;   //bit2
        u8    buf_mgmt_support    : 1;   //bit3
        u8    streaming_support   : 1;   //bit4
        u8    bit5_rsv            : 1;   //bit5
        u8    comid_support       : 1;   //bit6
        u8    bit7_rsv            : 1;   //bit7
    }b;
}L0FEA0001_DATA_t;
typedef struct{
    u16               FeaCode;       // 0
    u8                rsv : 4;       // 2
    u8                ver : 4;       // 2
    u8                FeaCodeLen;    // 3
    L0FEA0001_DATA_t  fea0001;       // 4
    u8                byte5_rsv[11]; // 5
}L0FEA0001_t;

typedef union{
    u8        fea0002_Bdata;
    struct{
        u8    fea0002_locking_support : 1;   //bit0
        u8    fea0002_activated       : 1;   //bit1
        u8    fea0002_locked          : 1;   //bit2
        u8    fea0002_encryption      : 1;   //bit3
        u8    fea0002_mbr_enabled     : 1;   //bit4
        u8    fea0002_mbr_done        : 1;   //bit5
        u8    fea0002_mbr_not_support : 1;   //bit6
        u8    fea0002_b7              : 1;   //bit7
    }b;
}L0FEA0002_DATA_t;
typedef struct{
    u16               FeaCode;       // 0
    u8                rsv : 4;       // 2
    u8                ver : 4;       // 2
    u8                FeaCodeLen;    // 3
    L0FEA0002_DATA_t   fea0002;      // 4
    u8                byte5_rsv[11]; // 5
}L0FEA0002_t;

typedef struct{
    u16               FeaCode;       // 0
    u8                rsv : 4;       // 2
    u8                ver : 4;       // 2
    u8                FeaCodeLen;    // 3
    u8                align    : 1;  // 4
    u8                bit1_rsv : 7;  // 4
    u8                byte5_rsv[7];  // 5
    u32               logical_blk_sz;         // 12
    u64               alignment_granularity;  // 16
    u64               lowest_aligned_lba;     // 24
}L0FEA0003_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u32               num_of_lcking_obj_support;  // 4
    u8                any     : 1;    // 8
    u8                all     : 1;    // 8
    u8                policy  : 1;
    u8                bit3_7  : 5;    // 8
    u8                byte9_rsv[7];   // 9
}L0FEA0201_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               byte4_rsv;      // 4
    u16               max_num_of_datastore;       // 6
    u32               max_total_sz_of_datastore;  // 8
    u32               datastore_sz_alignment;     // 12 ~ 15
}L0FEA0202_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               base_comid;     // 4
    u16               num_of_comid;   // 6
    u8                rng_crossing_behavior : 1;  // 8
    u8                bit1_rsv              : 7;  // 8
    u16               num_of_adm_auth_support;    // 9
    u16               num_of_user_auth_support;   // 11
    u8                init_cpin_sid_indicator;    // 13
    u8                behavior_cpin_sid_upon_tper_revert; // 14
    u8                byte15_rsv[5];   // 15 ~ 19
}L0FEA0203_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               base_comid;     // 4
    u16               num_of_comid;   // 6
    u8                byte8_rsv[5];   // 8 ~ 12
    u8                init_cpin_sid_indicator;    // 13
    u8                behavior_cpin_sid_upon_tper_revert; // 14
    u8                byte15_rsv[5];   // 15 ~ 19
}L0FEA0302_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               base_comid;     // 4
    u16               num_of_comid;   // 6
    u8                byte8_rsv[5];   // 8 ~ 12
    u8                init_cpin_sid_indicator;    // 13
    u8                behavior_cpin_sid_upon_tper_revert; // 14
    u8                byte15_rsv[5];   // 15 ~ 19
}L0FEA0303_t;

typedef struct{
    u16               FeaCode;     // 0
    u8                rsv : 4;     // 2
    u8                ver : 4;     // 2
    u8                FeaCodeLen;  // 3
    u8                sid_st       : 1;   // 4
    u8                sid_block_st : 1;   // 4
    u8                bit2_rsv     : 6;   // 4
    u8                hard_reset   : 1;   // 5
    u8                bit1_rsv     : 7;   // 5
    u8                byte15_rsv[10];     // 6 ~ 15
}L0FEA0402_t;

typedef struct{
    u16               FeaCode;     // 0
    u8                rsv : 4;     // 2
    u8                ver : 4;     // 2
    u8                FeaCodeLen;  // 3
    u8                byte4_rsv;   // 4
    u8                data_removal_operation_processing : 1; // 5
    u8                bit1_rsv                          : 7; // 5
    u8                support_data_removal_mechanism;   // 6
    u8                data_removal_time_fm_bit0  : 1;   // 7
    u8                data_removal_time_fm_bit1  : 1;   // 7
    u8                data_removal_time_fm_bit2  : 1;   // 7
    u8                data_removal_time_fm_bit3  : 1;   // 7
    u8                data_removal_time_fm_bit4  : 1;   // 7
    u8                data_removal_time_fm_bit5  : 1;   // 7
    u8                bit6_rsv                   : 2;   // 7
    u16               data_removal_time_fm_bit0_data;   // 8
    u16               data_removal_time_fm_bit1_data;   // 10
    u16               data_removal_time_fm_bit2_data;   // 12
    u16               data_removal_time_fm_bit3_data;   // 14
    u16               data_removal_time_fm_bit4_data;   // 16
    u16               data_removal_time_fm_bit5_data;   // 18
    u8                byte20_rsv[16];     // 20 ~ 35
}L0FEA0404_t;



typedef struct{
    u32   len;                    // 0
    u32   Revision;               // 4
    u64   Reserved;               // 8
    u8    VendorSpecific[32];     // 16
}L0HEADER_t;
typedef struct{
    u16    FeaCode;               // 0
    u8    rsv : 4;                // 2
    u8    ver : 4;                // 2
    u8    FeaCodeLen;             // 3
    union{
        u16    Fea_Wdata;         // 4
        struct{
            u8    Fea_Bdata0;
            u8    Fea_Bdata1;
        }B;
    };
}L0FEACODE_t;

typedef struct{
    L0HEADER_t  Header;     // 0
    L0FEACODE_t FD;         // 16 feature descriptor
}L0DISCOVERY_t;
//& L0 discovery data structure ----


//-----------------------------------------------------------------------------
//* session data structure ---------
//-----------------------------------------------------------------------------

typedef struct{
    u32    Reserved;          /// 0
    u16    ComID;             /// 4
    u16    ExtendedComID;     /// 6
    u32    OutstandingData;   /// 8
    u32    MinTransfer;       /// 12
    u32    Length;            /// 16
}ComPacket_t;
typedef struct{
    u32    TSN;               /// 0
    u32    HSN;               /// 4
    u32    SeqNumber;         /// 8
    u16    Reserved;          /// 12
    u16    AckType;           /// 14
    u32    Acknowledgement;   /// 16
    u32    Length;            /// 20
}Packet_t;
typedef struct{
    u32    Reserved_DW;       /// 0
    u16    Reserved_W;        /// 4
    u16    Kind;              /// 6
    u32    Length;            /// 8
}DataSubPacket_t;

typedef struct{
    ComPacket_t     ComPacket;
    Packet_t        Packet;
    DataSubPacket_t DataSubPacket;
    u8              DataPayLoad;
}SDS_t;    //Session Data Structure

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken;
}Common_PayLoad_t;    //session start send payload

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken;
    u8    Atom_HostSessionID;
    u8    s_Atom2;
    u64   SPID;
    u8    Atom_Write;
    u8    StartNameToken;
    u8    Atom_Name;
    u8    s_Atom3;
    u8    SS_sHostChallenge;
}SS_sPayLoad_t;    //session start send payload

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken;
    u8    s_Atom2;
    u32   HSN;
    u8    s_Atom3;
    u32   TSN;
    u8    EndListToken;
    u8    EndOfDataToken;
    u8    MethodStatusList[5];
    u8    Pad;
}SS_rPayLoad_t;    //session start receive payload

//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Function Definitions
//-----------------------------------------------------------------------------
enum cmd_rslt_t tcg_cmd_handle(req_t *req);




//-----------------------------------------------------------------------------
//  Exported variable reference
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Exported function reference
//-----------------------------------------------------------------------------



#else//------------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Define & Macros :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type define
//-----------------------------------------------------------------------------
enum opal_cmd {
    IF_SEND = 0x81,
    IF_RECV = 0x82,
};

#pragma pack(1)
typedef  struct
{

} NvmeAdminSecurityReceiveCommand_t;

typedef  struct
{

} NvmeAdminSecuritySendCommand_t;

typedef union
{
    /// DW10
    u32 all;
	
    struct{
    u32 nssf        :  8;    ///< NVMe Security Specific Field (DW10 bits[07:00])
    u32 com_id      : 16;    ///< SP Specific (DW10 bits[23:08])
    u32 protocol_id :  8;    ///< Security Protocol (DW10 bits[31:24])
    }b;

} nvme_tcg_cmd_dw10_t;

typedef union
{
    u32 len_alloc;                 ///< Allocation Length (DW11)
    u32 len_trans;                 ///< Transfer Length   (DW11)    
    
} nvme_tcg_cmd_dw11_t;

typedef  struct
{
    AdminCommandCommon_t DW9_0;    ///< DW9..0

    /// DW10
    u32  nssf       :8;     ///< NVMe Security Specific Field (DW10 bits[07:00])
    u32  com_id     :16;    ///< SP Specific (DW10 bits[23:08])
    u32  protocol_id:8;     ///< Security Protocol (DW10 bits[31:24])

    /// DW11
    u32  len;               ///< Allocation Length (DW11)

    /// DW12~15
    u32  reserved12[4];     ///< DW15..12

} NvmeTcgCmd_t;


typedef struct{
    u16  comId;                    /// 0
    u16  extendedComId;            /// 2
    u32  requestCode;              /// 4
    u8   reserved8;                /// 8
}TCG_Pid02_GetComId_req_t;

typedef struct{
    u16  comId;                    /// 0
    u16  extendedComId;            /// 2
    u32  requestCode;              /// 4
    u16  reserved8;                /// 8
    u16  availableDataLen;         /// 10
    u32  currentStatus;            /// 12
    u8   absTimeOfAllocation[10];  /// 16
    u8   absTimeOfExpiry[10];      /// 26
    u8   timeSinceLastReset[10];   /// 36
    u8   reserved46;               /// 46
}TCG_Pid02_GetComId_Resp_t;

typedef struct{       //SPC-4 7.6.1.3 Supported security protocols list description
    u32  dword_rsv;
    u16  word_rsv;
    u16  SSPL_len;   // SUPPORTED SECURITY PROTOCOL LIST LENGTH
    u8   SSPL0;      // Supported security protocol list0
}SSPLD_t;             // Supported security protocols list description

//* L0 discovery data structure ----
typedef union{
    u8        Bdata;
    struct{
        u8    sync_support        : 1;   //bit0
        u8    async_support       : 1;   //bit1
        u8    ack_nak_support     : 1;   //bit2
        u8    buf_mgmt_support    : 1;   //bit3
        u8    streaming_support   : 1;   //bit4
        u8    bit5_rsv            : 1;   //bit5
        u8    comid_support       : 1;   //bit6
        u8    bit7_rsv            : 1;   //bit7
    }b;
}L0FEA0001_DATA_t;
typedef struct{
    u16               FeaCode;       // 0
    u8                rsv : 4;       // 2
    u8                ver : 4;       // 2
    u8                FeaCodeLen;    // 3
    L0FEA0001_DATA_t  fea0001;       // 4
    u8                byte5_rsv[11]; // 5
}L0FEA0001_t;

typedef union{
    u8        fea0002_Bdata;
    struct{
        u8    fea0002_locking_support : 1;   //bit0
        u8    fea0002_activated       : 1;   //bit1
        u8    fea0002_locked          : 1;   //bit2
        u8    fea0002_encryption      : 1;   //bit3
        u8    fea0002_mbr_enabled     : 1;   //bit4
        u8    fea0002_mbr_done        : 1;   //bit5
        u8    fea0002_mbr_not_support : 1;   //bit6
        u8    fea0002_b7              : 1;   //bit7
    }b;
}L0FEA0002_DATA_t;
typedef struct{
    u16               FeaCode;       // 0
    u8                rsv : 4;       // 2
    u8                ver : 4;       // 2
    u8                FeaCodeLen;    // 3
    L0FEA0002_DATA_t   fea0002;      // 4
    u8                byte5_rsv[11]; // 5
}L0FEA0002_t;

typedef struct{
    u16               FeaCode;       // 0
    u8                rsv : 4;       // 2
    u8                ver : 4;       // 2
    u8                FeaCodeLen;    // 3
    u8                align    : 1;  // 4
    u8                bit1_rsv : 7;  // 4
    u8                byte5_rsv[7];  // 5
    u32               logical_blk_sz;         // 12
    u64               alignment_granularity;  // 16
    u64               lowest_aligned_lba;     // 24
}L0FEA0003_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u32               num_of_lcking_obj_support;  // 4
    u8                any     : 1;    // 8
    u8                all     : 1;    // 8
    u8                policy  : 1;
    u8                bit3_7  : 5;    // 8
    u8                byte9_rsv[7];   // 9
}L0FEA0201_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               byte4_rsv;      // 4
    u16               max_num_of_datastore;       // 6
    u32               max_total_sz_of_datastore;  // 8
    u32               datastore_sz_alignment;     // 12 ~ 15
}L0FEA0202_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               base_comid;     // 4
    u16               num_of_comid;   // 6
    u8                rng_crossing_behavior : 1;  // 8
    u8                bit1_rsv              : 7;  // 8
    u16               num_of_adm_auth_support;    // 9
    u16               num_of_user_auth_support;   // 11
    u8                init_cpin_sid_indicator;    // 13
    u8                behavior_cpin_sid_upon_tper_revert; // 14
    u8                byte15_rsv[5];   // 15 ~ 19
}L0FEA0203_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               base_comid;     // 4
    u16               num_of_comid;   // 6
    u8                byte8_rsv[5];   // 8 ~ 12
    u8                init_cpin_sid_indicator;    // 13
    u8                behavior_cpin_sid_upon_tper_revert; // 14
    u8                byte15_rsv[5];   // 15 ~ 19
}L0FEA0302_t;

typedef struct{
    u16               FeaCode;        // 0
    u8                rsv : 4;        // 2
    u8                ver : 4;        // 2
    u8                FeaCodeLen;     // 3
    u16               base_comid;     // 4
    u16               num_of_comid;   // 6
    u8                byte8_rsv[5];   // 8 ~ 12
    u8                init_cpin_sid_indicator;    // 13
    u8                behavior_cpin_sid_upon_tper_revert; // 14
    u8                byte15_rsv[5];   // 15 ~ 19
}L0FEA0303_t;

typedef struct{
    u16               FeaCode;     // 0
    u8                rsv : 4;     // 2
    u8                ver : 4;     // 2
    u8                FeaCodeLen;  // 3
    u8                sid_st       : 1;   // 4
    u8                sid_block_st : 1;   // 4
    u8                bit2_rsv     : 6;   // 4
    u8                hard_reset   : 1;   // 5
    u8                bit1_rsv     : 7;   // 5
    u8                byte15_rsv[10];     // 6 ~ 15
}L0FEA0402_t;

typedef struct{
    u16               FeaCode;     // 0
    u8                rsv : 4;     // 2
    u8                ver : 4;     // 2
    u8                FeaCodeLen;  // 3
    u8                byte4_rsv;   // 4
    u8                data_removal_operation_processing : 1; // 5
    u8                bit1_rsv                          : 7; // 5
    u8                support_data_removal_mechanism;   // 6
    u8                data_removal_time_fm_bit0  : 1;   // 7
    u8                data_removal_time_fm_bit1  : 1;   // 7
    u8                data_removal_time_fm_bit2  : 1;   // 7
    u8                data_removal_time_fm_bit3  : 1;   // 7
    u8                data_removal_time_fm_bit4  : 1;   // 7
    u8                data_removal_time_fm_bit5  : 1;   // 7
    u8                bit6_rsv                   : 2;   // 7
    u16               data_removal_time_fm_bit0_data;   // 8
    u16               data_removal_time_fm_bit1_data;   // 10
    u16               data_removal_time_fm_bit2_data;   // 12
    u16               data_removal_time_fm_bit3_data;   // 14
    u16               data_removal_time_fm_bit4_data;   // 16
    u16               data_removal_time_fm_bit5_data;   // 18
    u8                byte20_rsv[16];     // 20 ~ 35
}L0FEA0404_t;



typedef struct{
    u32   len;                    // 0
    u32   Revision;               // 4
    u64   Reserved;               // 8
    u8    VendorSpecific[32];     // 16
}L0HEADER_t;
typedef struct{
    u16    FeaCode;               // 0
    u8    rsv : 4;                // 2
    u8    ver : 4;                // 2
    u8    FeaCodeLen;             // 3
    union{
        u16    Fea_Wdata;         // 4
        struct{
            u8    Fea_Bdata0;
            u8    Fea_Bdata1;
        }B;
    };
}L0FEACODE_t;

typedef struct{
    L0HEADER_t  Header;     // 0
    L0FEACODE_t FD;         // 16 feature descriptor
}L0DISCOVERY_t;
//& L0 discovery data structure ----



//* session data structure ---------
typedef struct{
    u32    Reserved;          /// 0
    u16    ComID;             /// 4
    u16    ExtendedComID;     /// 6
    u32    OutstandingData;   /// 8
    u32    MinTransfer;       /// 12
    u32    Length;            /// 16
}ComPacket_t;
typedef struct{
    u32    TSN;               /// 0
    u32    HSN;               /// 4
    u32    SeqNumber;         /// 8
    u16    Reserved;          /// 12
    u16    AckType;           /// 14
    u32    Acknowledgement;   /// 16
    u32    Length;            /// 20
}Packet_t;
typedef struct{
    u32    Reserved_DW;       /// 0
    u16    Reserved_W;        /// 4
    u16    Kind;              /// 6
    u32    Length;            /// 8
}DataSubPacket_t;

typedef struct{
    ComPacket_t     ComPacket;
    Packet_t        Packet;
    DataSubPacket_t DataSubPacket;
    u8              DataPayLoad;
}SDS_t;    //Session Data Structure

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken;
}Common_PayLoad_t;    //session start send payload


typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken;
    u8    Atom_HostSessionID;
    u8    s_Atom2;
    u64   SPID;
    u8    Atom_Write;
    u8    StartNameToken;
    u8    Atom_Name;
    u8    s_Atom3;
    u8    SS_sHostChallenge;
}SS_sPayLoad_t;    //session start send payload

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken;
    u8    s_Atom2;
    u32   HSN;
    u8    s_Atom3;
    u32   TSN;
    u8    EndListToken;
    u8    EndOfDataToken;
    u8    MethodStatusList[5];
    u8    Pad;
}SS_rPayLoad_t;    //session start receive payload

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken0;
    u8    StartListToken1;
    u8    StartNameToken0;
    u8    Atom_Name0;
    u8    Atom_Value0;
    u8    EndNameToken0;
    u8    StartNameToken1;
    u8    Atom_Name1;
    u8    Atom_Value1;
    u8    EndNameToken1;
    u8    EndListToken0;
    u8    EndListToken1;
    u8    EndOfDataToken;
    u8    MethodStatusList[5];
    u8    Pad;
}SM_sPayLoad_Get_value_t;    //session methold  send payload

typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken0;
    u8    StartListToken1;
    u8    StartNameToken0;
    u8    Atom_Name0;
    u8    m_AtomToken0;
    u32   start_row;
    u8    EndNameToken0;
    u8    StartNameToken1;
    u8    Atom_Name1;
    u8    m_AtomToken1;
    u32   end_row;
    u8    EndNameToken1;
    u8    EndListToken0;
    u8    EndListToken1;
    u8    EndOfDataToken;
    u8    MethodStatusList[5];
    u8    Pad;
}SM_sPayLoad_Get_where_t;    //session methold  send payload


typedef struct{
    u8    CallToken;
    u8    s_Atom0;
    u64   InvokingUID;
    u8    s_Atom1;
    u64   MethodUID;
    u8    StartListToken0;
    u8    StartNameToken0;
    u8    Atom_Name0;
    u8    StartListToken1;
    u8    StartNameToken1;
    u8    Atom_Name1;
    u16   m_Atom0;
    u8    SM_sValue;
}SM_sPayLoad_Set_t;    //session methold  send payload

typedef struct{
    u8    StartListToken;
    u8    EndListToken;
    u8    EndOfDataToken;
    u8    MethodStatusList[5];
}SM_rPayLoad_t;    //session methold  receive payload

typedef struct{
    u8    EndOfDataToken;
    u8    Pad;
}SC_sPayLoad_t;    //session close  send payload

typedef struct{
    u8    EndOfDataToken;
    u8    Pad;
}SC_rPayLoad_t;    //session close  receive payload

typedef struct{
    u8    CallToken;
    u8    s_AtomHeader0;
    u64   InvokingUID;
    u8    s_AtomHeader1;
    u64   MethodUID;
    u8    StartListToken0;
    u8    StartNameToken0;
    u8    t_Atom_Name0;
    u8    StartListToken1;

    u8    StartNameToken000;
    u16   m_AtomHeader000;
    u8    str_MaxComPacketSize[16];
    u8    s_AtomHeader000;
    u16   val_MaxComPacketSize;
    u8    EndNameToken000;

    u8    StartNameToken001;
    u16   m_AtomHeader001;
    u8    str_MaxResponeComPacketSize[24];
    u8    s_AtomHeader001;
    u16   val_MaxResponeComPacketSize;
    u8    EndNameToken001;

    u8    StartNameToken002;
    u8    s_AtomHeader002;
    u8    str_MaxPacketSize[13];
    u8    s_AtomHeader102;
    u16   val_MaxPacketSize;
    u8    EndNameToken002;

    u8    StartNameToken003;
    u8    s_AtomHeader003;
    u8    str_MaxIndTokenSize[15];
    u8    s_AtomHeader103;
    u16   val_MaxIndTokenSize;
    u8    EndNameToken003;

    u8    StartNameToken004;
    u8    s_AtomHeader004;
    u8    str_MaxPackets[10];
    u8    t_AtomHeader004;
    u8    EndNameToken004;

    u8    StartNameToken005;
    u8    s_AtomHeader005;
    u8    str_MaxSubPackets[13];
    u8    t_AtomHeader005;
    u8    EndNameToken005;

    u8    StartNameToken006;
    u8    s_AtomHeader006;
    u8    str_MaxMethods[10];
    u8    t_AtomHeader006;
    u8    EndNameToken006;

    u8    EndListToken1;
    u8    EndNameToken0;
    u8    EndListToken0;

    u8    EndOfDataToken;
    u8    MethodStatusList[5];
}Properity_sPayLoad_t;

//& session data structure ---------

//IEEE1667 ------------------------
typedef struct{
    u32       PaylaodLength;
    u32       Reserved04_DW;
    u8        HostMajorVersion;
    u8        HostMinorVersion;
    u16       Reserved0A_W;
    u8        HostOS;
    u8        HostOSSpecificationLength;
    u8        HostImplementMajorVersion;
    u8        HostImplementMinorVersion;
    u8        HostOSSpecification;
}IEEE1667_ProbeSiloProbe_Spout_t;

typedef struct{
    u8        rsv[28];
    u32       STID;                   /// Silo Type Identified
    u8        SiloTypeSpecMajVer;     /// silo type specification major version
    u8        SiloTypeSpecMinVer;     /// silo type specification minor version
    u8        SiloTypeImplMajVer;     /// silo type implementation minor version
    u8        SiloTypeImplMinVer;     /// silo type implementation minor version
}IEEE1667_ProbeSiloProbe_SiloList_t;

typedef struct{
    u32       PaylaodLength;
    u16       Reserved04_W;
    u8        Reserved06_B;
    u8        StatusCode;
    u32       AvailablePayloadLength;
    u16       SiloListLength;
    u8        SiloList;
}IEEE1667_ProbeSiloProbe_Spin_t;

typedef struct{
    u32       PaylaodLength;
    u32       Reserved04_DW;
}IEEE1667_GetSiloCapabilities_Spout_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u16       Reserved04_W;             /// 4
    u8        Reserved06_B;             /// 6
    u8        StatusCode;               /// 7
    u32       AvailablePayloadLength;   /// 8
    u16       ComId;                    /// 12
    u16       Reserved14_W;             /// 14
    u32       MaximumPoutTransferSize;  /// 16
    u64       Reserved20_QW;            /// 20
    u32       TcgL0DiscoveryDataLength; /// 28
    u8        TcgL0DiscoveryData;       /// 32
}IEEE1667_GetSiloCapabilities_Spin_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved[24];             /// 4
    u32       TCGComPacketLength;       /// 28
    u8        TCGComPacket;             /// 32
}IEEE1667_TransferSilo_Spout_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u16       Reserved04_W;             /// 4
    u8        Reserved06_B;             /// 6
    u8        StatusCode;               /// 7
    u32       AvailablePayloadLength;   /// 8
    u8        Reserved[16];             /// 12
    u32       TCGComPacketLength;       /// 28
    u8        TCGComPacket;             /// 32
}IEEE1667_TransferSilo_Spin_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved[4];              /// 4
}IEEE1667_Reset_Spout_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved[3];              /// 4
    u8        StatusCode;               /// 7
}IEEE1667_Reset_Spin_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved[4];              /// 4
}IEEE1667_GetResult_Spout_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved4[3];             /// 4
    u8        StatusCode;               /// 7
    u32       AvailablePayloadLength;   /// 8
    u8        Reserved12[16];           /// 12
    u32       TCGComPacketLength;       /// 28
    u8        TCGComPacket;             /// 32
}IEEE1667_GetResult_Spin_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved[4];              /// 4
}IEEE1667_TperReset_Spout_t;

typedef struct{
    u32       PaylaodLength;            /// 0
    u8        Reserved[3];              /// 4
    u8        StatusCode;               /// 7
}IEEE1667_TperReset_Spin_t;
#pragma pack()

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Function Definitions
//-----------------------------------------------------------------------------
u32  tcg_tper_handle(req_t *req);
void silo_cmd_handle(req_t *req);
u16  tcg_cmdPkt_abortSession(void);

tper_status_t  f_SendCmd_Pid00(req_t *req);
tper_status_t  f_SendCmd_Pid01(req_t *req);
tper_status_t  f_SendCmd_Pid02(req_t *req);
tper_status_t  f_RecvCmd_Pid00(req_t *req);
tper_status_t  f_RecvCmd_Pid01(req_t *req);
tper_status_t  f_RecvCmd_Pid02(req_t *req);

void f_TcgSilo_GetSiloCap(req_t *req);
void f_TcgSilo_Transfer(req_t *req);
void f_TcgSilo_Reset(req_t *req);
void f_TcgSilo_GetResult(req_t *req);
void f_TcgSilo_TPerReset(req_t *req);






//-----------------------------------------------------------------------------
//  Exported variable reference
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Exported function reference
//-----------------------------------------------------------------------------

#endif // Jack Li
