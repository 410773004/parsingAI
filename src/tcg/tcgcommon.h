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
#ifndef tcgcommon_h
#define tcgcommon_h

#if _TCG_ // Jack Li

// [[[ NAND config parameter ]]]
#define CFG_UDATA_PER_PAGE          16384  //1page = 16K
#define mNUM_AU_PER_PAGE            CFG_UDATA_PER_PAGE/2048

#define L2P_PAGE_CNT                3
#define G4G5_LAA_AMOUNT_LIMIT       ((L2P_PAGE_CNT * CFG_UDATA_PER_PAGE - 0x400) / sizeof(u32)) // 0x4800  //18432

//-----------------------//

// @brief TCG (_TCG_)
#define TCG_NONE    0
#define TCG_OPAL    1
#define TCG_EDRV    2
#define TCG_PYRITE  3

#define SECURE_COM_BUF_SZ           0xC000                  // 48KB

//#define TCG_RNG_CHK_DEBUG     // Debug log enable for IO range chk

typedef enum
{
    TPER_GOOD = 0,
    TPER_INVALID_SEC_PID_PARAM,
    TPER_INVALID_TX_PARAM_SEND,
    TPER_OTHER_INVALID_CMD_PARAM,
    TPER_SYNC_PROTOCOL_VIOLATION,
    TPER_DATA_PROTECTION_ERROR,
    TPER_INVALID_SEC_STATE,
    TPER_OPERATION_DENIED,
    TPER_CONTINUE = 0x676f6f6e  // "goon"
} tper_status_t;

//Session Manager param
typedef enum
{
    SESSION_CLOSE,
    SESSION_START,
    SESSION_NG,
    SESSION_NAN = 0x7FFFFFFF
}session_state_t;

typedef enum
{
    TRNSCTN_ACTIVE,
    TRNSCTN_IDLE,
    TRNSCTN_NAN = 0x7FFFFFFF
}trnsctn_state_t;

typedef struct tcg_rdlk_list tcg_rdlk_list_t;
struct tcg_rdlk_list{
    tcg_rdlk_list_t* next;
    u32 cmd_slot;
};

//Token Definition
#define TOK_StartList           0xF0
#define TOK_EndList             0xF1
#define TOK_StartName           0xF2
#define TOK_EndName             0xF3
#define TOK_Call                0xF8
#define TOK_EndOfData           0xF9
#define TOK_EndOfSession        0xFA
#define TOK_StartTransaction    0xFB
#define TOK_EndTransaction      0xFC
#define TOK_Empty               0xFF


//Method Status Code Definition
#define STS_SUCCESS                     0x00
#define STS_NOT_AUTHORIZED              0x01
//#define STS_OBSOLETE                       0x02
#define STS_SP_BUSY                     0x03
#define STS_SP_FAILED                   0x04
#define STS_SP_DISABLED                 0x05
#define STS_SP_FROZEN                   0x06
#define STS_NO_SESSIONS_AVAILABLE       0x07
#define STS_UNIQUENESS_CONFLICT         0x08
#define STS_INSUFFICIENT_SPACE          0x09
#define STS_INSUFFICIENT_ROWS           0x0A
#define STS_INVALID_METHOD              0x0B
#define STS_INVALID_PARAMETER           0x0C
//0x0D
//0x0E
#define STS_TPER_MALFUNCTION            0x0F
#define STS_TRANSACTION_FAILURE         0x10
#define STS_RESPONSE_OVERFLOW           0x11
#define STS_AUTHORITY_LOCKED_OUT        0x12
#define STS_FAIL                        0x3F

#define STS_SUCCESS_THEN_ABORT          0xE0FF    //internal use only, status is success but need to close session (RevertSP)
#define STS_STAY_IN_IF_SEND             0xF0FF    //internal use only, it should keep gTcgCmdState at "ST_AWAIT_IF_SEND"
#define STS_SESSION_ABORT               0xFFFF    //internal use only
#define STS_1667_NO_DATA_RETURN         0xF8FF

#define zOK     0
#define zNG     -1


//-----------------------------------------------------------------------------
//  Imported data prototype without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
//#define LoByte(i) *((u8*)&i)
#define LoByte(i) (u8)((u16)i&0xFF)
//#define HiByte(i) *(((u8*)&i)+1)
#define HiByte(i) (u8)(((u16)i&0xFF00)>>8)
//#define LoWord(i) *((u16*)(&i))
#define LoWord(i) (u16)((u32)i&0xFFFF)
//#define HiWord(i) *(((u16*)&i)+1)
#define HiWord(i) (u16)(((u32)i&0xFFFF0000)>>16)
#define LoLong(i) *((u32*)&i)
#define HiLong(i) *(((u32*)&i)+1)

#define W_B1(a) *(((u8*)&a)+1)
#define W_B0(a) *(((u8*)&a)+0)

#define DW_W1(a) *(((u16*)&a)+1)
#define DW_W0(a) *(((u16*)&a)+0)

#define DW_B3(a) *(((u8*)&a)+3)
#define DW_B2(a) *(((u8*)&a)+2)
#define DW_B1(a) *(((u8*)&a)+1)
#define DW_B0(a) *(((u8*)&a)+0)

#define LL_DW1(a) *(((u32*)&a)+1)
#define LL_DW0(a) *(((u32*)&a)+0)

#define LL_B7(a) *(((u8*)&a)+7)
#define LL_B6(a) *(((u8*)&a)+6)
#define LL_B5(a) *(((u8*)&a)+5)
#define LL_B4(a) *(((u8*)&a)+4)
#define LL_B3(a) *(((u8*)&a)+3)
#define LL_B2(a) *(((u8*)&a)+2)
#define LL_B1(a) *(((u8*)&a)+1)
#define LL_B0(a) *(((u8*)&a)+0)

#define SHA256_DIGEST_SIZE ( 256 / 8)

//------------------------------------------------------

#define SupportDataRemovalMechanism      (BIT1)

//------------------------------------------------------

#define DATASTORE_LEN               0x00A20000      //10.28MB
#define MBR_LEN                     0x08000000      //128MB

#define DSTBL_ALIGNMENT             1    //byte
#define DSTBL_MAX_NUM               (LOCKING_RANGE_CNT + 1)

#if (CPU_ID==1)      // Jack Li, plz check neccessary or not

#if 1
#define DEFAULT_LOGICAL_BLOCK_SIZE  512
#define TCG_LogicalBlockSize        DEFAULT_LOGICAL_BLOCK_SIZE
#define TCG_AlignmentGranularity    (DTAG_SZE << 10)

#else
extern vu32                         smLogicBlockSize;

#define DEFAULT_LOGICAL_BLOCK_SIZE  512
#if (MUTI_LBAF == mTRUE)
  #define TCG_LogicalBlockSize      smLogicBlockSize
#else
  #define TCG_LogicalBlockSize      DEFAULT_LOGICAL_BLOCK_SIZE
#endif
//#define TCG_AlignmentGranularity    (CFG_UDATA_PER_PAGE/LBA_SIZE_IN_BYTE)  //8KB=16LBA, 16KB=32LBA
#define DEFAULT_AlignmentGranularity 8
#if (MUTI_LBAF == mTRUE)
  #define TCG_AlignmentGranularity  (0x1000/smLogicBlockSize)   //4KB/smLogicBlockSize
#else
  #define TCG_AlignmentGranularity    8   //4KB=8LBA
#endif

#endif

#endif               // Jack Li, plz check neccessary or not
//------------------------------------------------------

#define AUTHORITY_CNT			4	//For Muti Authticate within Session

#define TCG_G1_TAG                  0x11110000
#define TCG_G2_TAG                  0x22220000
#define TCG_G3_TAG                  0x33330000
#define TCG_END_TAG                 0x454E4454

#define SP_LOCKING_IDX          1   //UID_SP_Locking row index

#define CPIN_SID_IDX            0   //CPIN_SID row index
#define CPIN_MSID_IDX           1   //CPIN_MSID row index
#define CPIN_MSID_LEN          16   //CPIN_MSID row index

#define CPIN_PSID_IDX           (TCG_AdminCnt + 2)   //CPIN_PSID row index

#define LCK_CPIN_ADMIN1_IDX     0   //Lck CPIN_Admin1_IDX
#define SI_LOCKING_RANGE_CNT    LOCKING_RANGE_CNT   //total locking range: 9 (=1+8, including global range)

#define TPER_FW_VERSION             0x00        //uint32

//------------------------------------------------------
#define UID_Null                    (0x0000000000000000)

//UID for SessionManager
#define UID_ThisSP                  0x00000000000001
#define SMUID                       0x000000000000FF
//SessionManager Method UID
#define SM_MTD_Properties           0x000000000000FF01  // Properties
#define SM_MTD_StartSession         0x000000000000FF02  // StartSession
#define SM_MTD_SyncSession          0x000000000000FF03  // SyncSession
//#define SM_MTD_StartTrustedSession    0x000000000000FF04    // StartTrustedSession
//#define SM_MTD_SyncTrustedSession     0x000000000000FF05    // SyncTrustedSession
#define SM_MTD_CloseSession         0x000000000000FF06  // CloseSession

//-------------------------------------------------------

//UID for AdminSP:
//SPInfo Table
//#define UID_Admin_SPInfo_Admin   (0x0000000200000001)
#define UID_SPInfo_Null             (0x0000000200000000)
#define UID_SPInfo                  (0x0000000200000001)

//SPTemplate Table
#define UID_SPTemplate              (0x0000000300000000)
#define UID_SPTemplate_1            (0x0000000300000001)
#define UID_SPTemplate_2            (0x0000000300000002)

//UID for Column Type
#define UID_CT_Authority_object_ref (0x0000000500000C05)
#define UID_CT_boolean_ACE          (0x000000050000040E)

//------------------------------------------------------
//Table Table
#define UID_Table                   (0x0000000100000000)
#define UID_Table_Table             (0x0000000100000001)
#define UID_Table_SPInfo            (0x0000000100000002)
#define UID_Table_SPTemplates       (0x0000000100000003)
#define UID_Table_MethodID          (0x0000000100000006)
#define UID_Table_AccessControl     (0x0000000100000007)
#define UID_Table_ACE               (0x0000000100000008)
#define UID_Table_Authority         (0x0000000100000009)
#define UID_Table_CPIN              (0x000000010000000B)
#define UID_Table_TPerInfo          (0x0000000100000201)
#define UID_Table_Template          (0x0000000100000204)
#define UID_Table_SP                (0x0000000100000205)
#define UID_Table_RemovalMechanism  (0x0000000100001101)

//MethodID Table
#define UID_MethodID                (0x0000000600000000)
#define UID_MethodID_Next           (0x0000000600000008)
#define UID_MethodID_GetACL         (0x000000060000000D)
#define UID_MethodID_Get            (0x0000000600000016)
#define UID_MethodID_Set            (0x0000000600000017)
#define UID_MethodID_Authenticate   (0x000000060000001C)    //Opal2
#define UID_MethodID_Revert         (0x0000000600000202)
#define UID_MethodID_Activate       (0x0000000600000203)
#define UID_MethodID_Random         (0x0000000600000601)    //Opal2
//#define UID_MethodID_Next         (0x0000000600000008)
//#define UID_MethodID_GetACL   	(0x000000060000000D)
#define UID_MethodID_GenKey     	(0x0000000600000010)
#define UID_MethodID_RevertSP   	(0x0000000600000011)
//#define UID_MethodID_Get      	(0x0000000600000016)
//#define UID_MethodID_Set      	(0x0000000600000017)
#define UID_MethodID_Reactivate 	(0x0000000600000801)    //SingleUserMode FS
#define UID_MethodID_Erase      	(0x0000000600000803)    //SingleUserMode FS
#define UID_MethodID_Assign     	(0x0000000600000804)    //ConfigNS FS
#define UID_MethodID_Deassign   	(0x0000000600000805)    //ConfigNS FS


//AccessControl Table: x
#define UID_AccessControl           (0x0000000700000000)

//ACE Table
#define UID_ACE                     (0x0000000800000000)
#define UID_ACE_Anybody             (0x0000000800000001)
#define UID_ACE_Admin               (0x0000000800000002)
#define UID_ACE_Set_Enabled         (0x0000000800030001)
#define UID_ACE_CPIN_SID_Get_NOPIN  (0x0000000800008C02)
#define UID_ACE_CPIN_SID_Set_PIN    (0x0000000800008C03)
#define UID_ACE_CPIN_MSID_Get_PIN   (0x0000000800008C04)
#define UID_ACE_CPIN_Get_PSID_NoPIN (0x00000008000100e1)    //PSID
#define UID_ACE_CPIN_Admins_Set_PIN (0x000000080003A001)    //Opal2
#define UID_ACE_TPerInfo_Set_PReset (0x0000000800030003)    //Opal2
#define UID_ACE_SP_SID              (0x0000000800030002)
#define UID_ACE_SP_PSID             (0x00000008000100e0)
#define UID_ACE_RMMech_Set_RM       (0x0000000800050001)    //Pyrite v2.00_r1.16

//Authority Table
#define UID_Authority               (0x0000000900000000)
#define UID_Authority_Anybody       (0x0000000900000001)
#define UID_Authority_Admins        (0x0000000900000002)
#define UID_Authority_Makers        (0x0000000900000003)
#define UID_Authority_SID           (0x0000000900000006)
#define UID_Authority_PSID          (0x000000090001FF01)    //eDrive

#define UID_Authority_AdmAdmins     (0x0000000900000200)
#define UID_Authority_AdmAdmin1     (0x0000000900000201)
#define UID_Authority_AdmAdmin2     (0x0000000900000202)
#define UID_Authority_AdmAdmin3     (0x0000000900000203)
#define UID_Authority_AdmAdmin4     (0x0000000900000204)

//Authority Table
//#define UID_Authority_Anybody (0x0000000900000001)
//#define UID_Authority_Admins  (0x0000000900000002)
#define UID_Authority_AdminX    (0x0000000900010000)
#define UID_Authority_Admin1    (0x0000000900010001)
#define UID_Authority_Admin2    (0x0000000900010002)
#define UID_Authority_Admin3    (0x0000000900010003)
#define UID_Authority_Admin4    (0x0000000900010004)
#define UID_Authority_Users     (0x0000000900030000)
#define UID_Authority_User1     (0x0000000900030001)
#define UID_Authority_User2     (0x0000000900030002)
#define UID_Authority_User3     (0x0000000900030003)
#define UID_Authority_User4     (0x0000000900030004)
#define UID_Authority_User5     (0x0000000900030005)
#define UID_Authority_User6     (0x0000000900030006)
#define UID_Authority_User7     (0x0000000900030007)
#define UID_Authority_User8     (0x0000000900030008)
#define UID_Authority_User9     (0x0000000900030009)
#define UID_Authority_AtaMst    (0x0000000900050001)
#define UID_Authority_AtaUsr    (0x0000000900050002)

//C_PIN Table
#define UID_CPIN                    (0x0000000B00000000) //CPIN Table
#define UID_CPIN_SID                (0x0000000B00000001)
#define UID_CPIN_MSID               (0x0000000B00008402)

#define UID_CPIN_AdmAdmin1          (0x0000000B00000201)
#define UID_CPIN_AdmAdmin2          (0x0000000B00000202)
#define UID_CPIN_AdmAdmin3          (0x0000000B00000203)
#define UID_CPIN_AdmAdmin4          (0x0000000B00000204)

#define UID_CPIN_PSID               (0x0000000B0001FF01) //eDriver


//C_PIN Table
#define UID_CPIN_Admin1         (0x0000000B00010001)
#define UID_CPIN_Admin2         (0x0000000B00010002)
#define UID_CPIN_Admin3         (0x0000000B00010003)
#define UID_CPIN_Admin4         (0x0000000B00010004)
#define UID_CPIN_User1          (0x0000000B00030001)
#define UID_CPIN_User2          (0x0000000B00030002)
#define UID_CPIN_User3          (0x0000000B00030003)
#define UID_CPIN_User4          (0x0000000B00030004)
#define UID_CPIN_User5          (0x0000000B00030005)
#define UID_CPIN_User6          (0x0000000B00030006)
#define UID_CPIN_User7          (0x0000000B00030007)
#define UID_CPIN_User8          (0x0000000B00030008)
#define UID_CPIN_User9          (0x0000000B00030009)


//TPerInfo Table
#define UID_TPerInfo                (0x0000020100030001)

//Template Table
#define UID_Template                (0x0000020400000000)
#define UID_Template_Base           (0x0000020400000001)
#define UID_Template_Admin          (0x0000020400000002)
#define UID_Template_Locking        (0x0000020400000006)
#define UID_Template_InterfaceControl   (0x0000020400000007)

//SP Table
#define UID_SP                      (0x0000020500000000)
#define UID_SP_Admin                (0x0000020500000001)
#define UID_SP_Locking              (0x0000020500000002)

//UID for LockingSP:
//SPInfo Table
//#define UID_Locking_SPInfo_Locking      (0x0000000200000001)

//SPTemplate Table, ?? check Table 245 and Table 21 ??
//#define UID_Locking_SPTemplate_Base     (0x0000000300000001)
//#define UID_Locking_SPTemplate_Locking  (0x0000000300000002)
//#define UID_Locking_SPTemplate_InterfaceControl (0x0000000300000003)

//Table Table
//#define UID_Table_Table       (0x0000000100000001)
//#define UID_Table_SPInfo      (0x0000000100000002)
//#define UID_Table_SPTemplates (0x0000000100000003)
//#define UID_Table_MethodID    (0x0000000100000006)
//#define UID_Table_AccessControl   (0x0000000100000007)
//#define UID_Table_ACE         (0x0000000100000008)
//#define UID_Table_Authority   (0x0000000100000009)
//#define UID_Table_CPIN        (0x000000010000000B)
#define UID_Table_SecretProtect         (0x000000010000001D)    //Opal2
#define UID_Table_LockingInfo           (0x0000000100000801)
#define UID_Table_Locking               (0x0000000100000802)
#define UID_Table_MBRControl            (0x0000000100000803)
#define UID_Table_MBR                   (0x0000000100000804)
#define UID_Table_K_AES_128             (0x0000000100000805)
#define UID_Table_K_AES_256             (0x0000000100000806)
#define UID_Table_RestrictedCommands    (0x0000000100000C01)

#define UID_Table_DataStore     (0x0000000100001001)
#define UID_Table_DataStore2    (0x0000000100001002)    //FS: DataStore
#define UID_Table_DataStore3    (0x0000000100001003)
#define UID_Table_DataStore4    (0x0000000100001004)
#define UID_Table_DataStore5    (0x0000000100001005)
#define UID_Table_DataStore6    (0x0000000100001006)
#define UID_Table_DataStore7    (0x0000000100001007)
#define UID_Table_DataStore8    (0x0000000100001008)
#define UID_Table_DataStore9    (0x0000000100001009)

//SecretProtect
#define UID_SecretProtect       (0x0000001D00000000)
#define UID_SecretProtect_128   (0x0000001D0000001D)    //Opal2
#define UID_SecretProtect_256   (0x0000001D0000001E)    //Opal2

//LockingInfo Table
#define UID_LockingInfo         (0x0000080100000001)

//Locking Table
#define UID_Locking             (0x0000080200000000)
#define UID_Locking_GRange      (0x0000080200000001)
#define UID_Locking_Range1      (0x0000080200030001)
#define UID_Locking_Range2      (0x0000080200030002)
#define UID_Locking_Range3      (0x0000080200030003)
#define UID_Locking_Range4      (0x0000080200030004)
#define UID_Locking_Range5      (0x0000080200030005)
#define UID_Locking_Range6      (0x0000080200030006)
#define UID_Locking_Range7      (0x0000080200030007)
#define UID_Locking_Range8      (0x0000080200030008)
#define UID_Locking_Range       (0x0000080200030000)

//MBRControl Table
#define UID_MBRControl          (0x0000080300000001)

#define UID_MBR                 (0x0000080400000000)

//K_AES_256 Table
#define UID_K_AES_256               (0x0000000100000806)    //(0x0000080600000000)
#define UID_K_AES_256_TBL           (0x0000080600000000)
#define UID_K_AES_256_GRange_Key    (0x0000080600000001)
#define UID_K_AES_256_Range1_Key    (0x0000080600030001)
#define UID_K_AES_256_Range2_Key    (0x0000080600030002)
#define UID_K_AES_256_Range3_Key    (0x0000080600030003)
#define UID_K_AES_256_Range4_Key    (0x0000080600030004)
#define UID_K_AES_256_Range5_Key    (0x0000080600030005)
#define UID_K_AES_256_Range6_Key    (0x0000080600030006)
#define UID_K_AES_256_Range7_Key    (0x0000080600030007)
#define UID_K_AES_256_Range8_Key    (0x0000080600030008)

#define UID_DataStoreType       (0x0000100000000000)
#define UID_DataStore           (0x0000100100000000)
#define UID_DataStore2          (0x0000100200000000)    //FS: DataStore
#define UID_DataStore3          (0x0000100300000000)    //FS: DataStore
#define UID_DataStore4          (0x0000100400000000)    //FS: DataStore
#define UID_DataStore5          (0x0000100500000000)    //FS: DataStore
#define UID_DataStore6          (0x0000100600000000)    //FS: DataStore
#define UID_DataStore7          (0x0000100700000000)    //FS: DataStore
#define UID_DataStore8          (0x0000100800000000)    //FS: DataStore
#define UID_DataStore9          (0x0000100900000000)    //FS: DataStore

#define UID_FF                  (0xFFFFFFFF00000000)

//ACE Table
//#define UID_ACE_Anybody         (0x0000000800000001)
//#define UID_ACE_Admin           (0x0000000800000002)
#define UID_ACE_Anybody_Get_CommonName      (0x0000000800000003)    //Opal2
#define UID_ACE_Admins_Set_CommonName       (0x0000000800000004)    //Opal2
#define UID_ACE_ACE_Get_All                 (0x0000000800038000)
#define UID_ACE_ACE_Set_BooleanExpression   (0x0000000800038001)
#define UID_ACE_Authority_Get_All           (0x0000000800039000)
#define UID_ACE_Authority_Set_Enabled       (0x0000000800039001)
#define UID_ACE_User1_Set_CommonName        (0x0000000800044001)    //Opal2
#define UID_ACE_User2_Set_CommonName        (0x0000000800044002)    //Opal2
#define UID_ACE_User3_Set_CommonName        (0x0000000800044003)    //Opal2
#define UID_ACE_User4_Set_CommonName        (0x0000000800044004)    //Opal2
#define UID_ACE_User5_Set_CommonName        (0x0000000800044005)    //Opal2
#define UID_ACE_User6_Set_CommonName        (0x0000000800044006)    //Opal2
#define UID_ACE_User7_Set_CommonName        (0x0000000800044007)    //Opal2
#define UID_ACE_User8_Set_CommonName        (0x0000000800044008)    //Opal2
#define UID_ACE_User9_Set_CommonName        (0x0000000800044009)    //Opal2

#define UID_ACE_C_PIN_Admins_Get_All_NOPIN  (0x000000080003A000)
#define UID_ACE_C_PIN_Admins_Set_PIN        (0x000000080003A001)
#define UID_ACE_C_PIN_User_Set_PIN          (0x000000080003A800)
#define UID_ACE_C_PIN_User1_Set_PIN         (0x000000080003A801)
#define UID_ACE_C_PIN_User2_Set_PIN         (0x000000080003A802)
#define UID_ACE_C_PIN_User3_Set_PIN         (0x000000080003A803)
#define UID_ACE_C_PIN_User4_Set_PIN         (0x000000080003A804)
#define UID_ACE_C_PIN_User5_Set_PIN         (0x000000080003A805)
#define UID_ACE_C_PIN_User6_Set_PIN         (0x000000080003A806)
#define UID_ACE_C_PIN_User7_Set_PIN         (0x000000080003A807)
#define UID_ACE_C_PIN_User8_Set_PIN         (0x000000080003A808)
#define UID_ACE_C_PIN_User9_Set_PIN         (0x000000080003A809)

#define UID_ACE_K_AES_Mode                      (0x000000080003BFFF)
#define UID_ACE_K_AES_256_GlobalRange_GenKey    (0x000000080003B800)
#define UID_ACE_K_AES_256_Range1_GenKey         (0x000000080003B801)
#define UID_ACE_K_AES_256_Range2_GenKey         (0x000000080003B802)
#define UID_ACE_K_AES_256_Range3_GenKey         (0x000000080003B803)
#define UID_ACE_K_AES_256_Range4_GenKey         (0x000000080003B804)
#define UID_ACE_K_AES_256_Range5_GenKey         (0x000000080003B805)
#define UID_ACE_K_AES_256_Range6_GenKey         (0x000000080003B806)
#define UID_ACE_K_AES_256_Range7_GenKey         (0x000000080003B807)
#define UID_ACE_K_AES_256_Range8_GenKey         (0x000000080003B808)
#define UID_ACE_Locking_GRange_Get_RangeStartToActiveKey   (0x000000080003D000)
#define UID_ACE_Locking_Range1_Get_RangeStartToActiveKey   (0x000000080003D001)
#define UID_ACE_Locking_Range2_Get_RangeStartToActiveKey   (0x000000080003D002)
#define UID_ACE_Locking_Range3_Get_RangeStartToActiveKey   (0x000000080003D003)
#define UID_ACE_Locking_Range4_Get_RangeStartToActiveKey   (0x000000080003D004)
#define UID_ACE_Locking_Range5_Get_RangeStartToActiveKey   (0x000000080003D005)
#define UID_ACE_Locking_Range6_Get_RangeStartToActiveKey   (0x000000080003D006)
#define UID_ACE_Locking_Range7_Get_RangeStartToActiveKey   (0x000000080003D007)
#define UID_ACE_Locking_Range8_Get_RangeStartToActiveKey   (0x000000080003D008)
#define UID_ACE_Locking_GRange_Set_RdLocked         (0x000000080003E000)
#define UID_ACE_Locking_Range1_Set_RdLocked         (0x000000080003E001)
#define UID_ACE_Locking_Range2_Set_RdLocked         (0x000000080003E002)
#define UID_ACE_Locking_Range3_Set_RdLocked         (0x000000080003E003)
#define UID_ACE_Locking_Range4_Set_RdLocked         (0x000000080003E004)
#define UID_ACE_Locking_Range5_Set_RdLocked         (0x000000080003E005)
#define UID_ACE_Locking_Range6_Set_RdLocked         (0x000000080003E006)
#define UID_ACE_Locking_Range7_Set_RdLocked         (0x000000080003E007)
#define UID_ACE_Locking_Range8_Set_RdLocked         (0x000000080003E008)
#define UID_ACE_Locking_GRange_Set_WrLocked         (0x000000080003E800)
#define UID_ACE_Locking_Range1_Set_WrLocked         (0x000000080003E801)
#define UID_ACE_Locking_Range2_Set_WrLocked         (0x000000080003E802)
#define UID_ACE_Locking_Range3_Set_WrLocked         (0x000000080003E803)
#define UID_ACE_Locking_Range4_Set_WrLocked         (0x000000080003E804)
#define UID_ACE_Locking_Range5_Set_WrLocked         (0x000000080003E805)
#define UID_ACE_Locking_Range6_Set_WrLocked         (0x000000080003E806)
#define UID_ACE_Locking_Range7_Set_WrLocked         (0x000000080003E807)
#define UID_ACE_Locking_Range8_Set_WrLocked         (0x000000080003E808)
#define UID_ACE_Locking_GlblRng_Admins_Set          (0x000000080003F000)
#define UID_ACE_Locking_Admins_RangeStartToLocked   (0x000000080003F001)
#if TCG_FS_CONFIG_NS
  #define UID_ACE_Locking_Namespace_IdtoGlbRng      (0x000000080003F002)
#else
  #define UID_ACE_Locking_Namespace_IdtoGlbRng      (0)
#endif
#define UID_ACE_MBRControl_Admins_Set               (0x000000080003F800)
#define UID_ACE_MBRControl_Set_Done                 (0x000000080003F801)
#if TCG_FS_CONFIG_NS
  #define UID_ACE_Assign                            (0x000000080003F901)
  #define UID_ACE_Deassign                          (0x000000080003F902)
#endif
#define UID_ACE_DataStore_Get_All                   (0x000000080003FC00)
#define UID_ACE_DataStore_Set_All                   (0x000000080003FC01)
#define UID_ACE_DataStore2_Get_All                  (0x000000080003FC02)    //FS_DataStore
#define UID_ACE_DataStore2_Set_All                  (0x000000080003FC03)    //FS_DataStore
#define UID_ACE_DataStore3_Get_All                  (0x000000080003FC04)    //FS_DataStore
#define UID_ACE_DataStore3_Set_All                  (0x000000080003FC05)    //FS_DataStore
#define UID_ACE_DataStore4_Get_All                  (0x000000080003FC06)    //FS_DataStore
#define UID_ACE_DataStore4_Set_All                  (0x000000080003FC07)    //FS_DataStore
#define UID_ACE_DataStore5_Get_All                  (0x000000080003FC08)    //FS_DataStore
#define UID_ACE_DataStore5_Set_All                  (0x000000080003FC09)    //FS_DataStore
#define UID_ACE_DataStore6_Get_All                  (0x000000080003FC0A)    //FS_DataStore
#define UID_ACE_DataStore6_Set_All                  (0x000000080003FC0B)    //FS_DataStore
#define UID_ACE_DataStore7_Get_All                  (0x000000080003FC0C)    //FS_DataStore
#define UID_ACE_DataStore7_Set_All                  (0x000000080003FC0D)    //FS_DataStore
#define UID_ACE_DataStore8_Get_All                  (0x000000080003FC0E)    //FS_DataStore
#define UID_ACE_DataStore8_Set_All                  (0x000000080003FC0F)    //FS_DataStore
#define UID_ACE_DataStore9_Get_All                  (0x000000080003FC10)    //FS_DataStore
#define UID_ACE_DataStore9_Set_All                  (0x000000080003FC11)    //FS_DataStore

#define UID_ACE_Locking_GRange_Set_ReadToLOR        (0x0000000800040000)    //FS: SingleUserMode
#define UID_ACE_Locking_Range1_Set_ReadToLOR        (0x0000000800040001)    //FS: SingleUserMode
#define UID_ACE_Locking_Range2_Set_ReadToLOR        (0x0000000800040002)    //FS: SingleUserMode
#define UID_ACE_Locking_Range3_Set_ReadToLOR        (0x0000000800040003)    //FS: SingleUserMode
#define UID_ACE_Locking_Range4_Set_ReadToLOR        (0x0000000800040004)    //FS: SingleUserMode
#define UID_ACE_Locking_Range5_Set_ReadToLOR        (0x0000000800040005)    //FS: SingleUserMode
#define UID_ACE_Locking_Range6_Set_ReadToLOR        (0x0000000800040006)    //FS: SingleUserMode
#define UID_ACE_Locking_Range7_Set_ReadToLOR        (0x0000000800040007)    //FS: SingleUserMode
#define UID_ACE_Locking_Range8_Set_ReadToLOR        (0x0000000800040008)    //FS: SingleUserMode

#define UID_ACE_Locking_Range1_Set_Range            (0x0000000800041001)    //FS: SingleUserMode
#define UID_ACE_Locking_Range2_Set_Range            (0x0000000800041002)    //FS: SingleUserMode
#define UID_ACE_Locking_Range3_Set_Range            (0x0000000800041003)    //FS: SingleUserMode
#define UID_ACE_Locking_Range4_Set_Range            (0x0000000800041004)    //FS: SingleUserMode
#define UID_ACE_Locking_Range5_Set_Range            (0x0000000800041005)    //FS: SingleUserMode
#define UID_ACE_Locking_Range6_Set_Range            (0x0000000800041006)    //FS: SingleUserMode
#define UID_ACE_Locking_Range7_Set_Range            (0x0000000800041007)    //FS: SingleUserMode
#define UID_ACE_Locking_Range8_Set_Range            (0x0000000800041008)    //FS: SingleUserMode

#define UID_ACE_CPIN_Anybody_Get_NoPIN              (0x0000000800042000)    //FS: SingleUserMode
#define UID_ACE_SP_Reactivate_Admin                 (0x0000000800042001)    //FS: SingleUserMode

#define UID_ACE_Locking_GRange_Erase                (0x0000000800043000)    //FS: SingleUserMode
#define UID_ACE_Locking_Range1_Erase                (0x0000000800043001)    //FS: SingleUserMode
#define UID_ACE_Locking_Range2_Erase                (0x0000000800043002)    //FS: SingleUserMode
#define UID_ACE_Locking_Range3_Erase                (0x0000000800043003)    //FS: SingleUserMode
#define UID_ACE_Locking_Range4_Erase                (0x0000000800043004)    //FS: SingleUserMode
#define UID_ACE_Locking_Range5_Erase                (0x0000000800043005)    //FS: SingleUserMode
#define UID_ACE_Locking_Range6_Erase                (0x0000000800043006)    //FS: SingleUserMode
#define UID_ACE_Locking_Range7_Erase                (0x0000000800043007)    //FS: SingleUserMode
#define UID_ACE_Locking_Range8_Erase                (0x0000000800043008)    //FS: SingleUserMode


//-------------------------------------------------------


//typedef void*               PVOID;      ///< void pointer

typedef enum
{
    MSG_TCG_G1RDDEFAULT = 0,
    MSG_TCG_G2RDDEFAULT,        /// 01
    MSG_TCG_G3RDDEFAULT,        /// 02
    MSG_TCG_G4RDDEFAULT,        /// 03
    MSG_TCG_G5RDDEFAULT,        /// 04
    MSG_TCG_G4WRDEFAULT,        /// 05
    MSG_TCG_G5WRDEFAULT,        /// 06
    MSG_TCG_G4BUILDDEFECT,      /// 07
    MSG_TCG_G5BUILDDEFECT,      /// 08
    MSG_TCG_G1RD,               /// 09
    MSG_TCG_G1WR,               /// 10
    MSG_TCG_G2RD,               /// 11
    MSG_TCG_G2WR,               /// 12
    MSG_TCG_G3RD,               /// 13
    MSG_TCG_G3WR,               /// 14
    MSG_TCG_G4DMYRD,            /// 15
    MSG_TCG_G4DMYWR,            /// 16
    MSG_TCG_G5DMYRD,            /// 17
    MSG_TCG_G5DMYWR,            /// 18
    MSG_TCG_SMBRRD,             /// 19
    MSG_TCG_SMBRWR,             /// 20
    MSG_TCG_SMBRCOMMIT,         /// 21
    MSG_TCG_SMBRCLEAR,          /// 22
    MSG_TCG_TSMBRCLEAR,         /// 23
    MSG_TCG_DSRD,               /// 24
    MSG_TCG_DSWR,               /// 25
    MSG_TCG_DSCOMMIT,           /// 26
    MSG_TCG_DSCLEAR,            /// 27
    MSG_TCG_TDSCLEAR,           /// 28
    MSG_TCG_G4FTL,              /// 29
    MSG_TCG_G5FTL,              /// 30

    MSG_TCG_INIT_CACHE,         /// 31
    MSG_TCG_CLR_CACHE,          /// 32
    MSG_TCG_NOREEP_INIT,        /// 33
    MSG_TCG_NOREEP_RD,          /// 34
    MSG_TCG_NOREEP_WR,          /// 35
    MSG_TCG_NF_CPU_INIT,        /// 36
    MSG_TCG_SYNC_ZONE51_MEDIA,  /// 37    /* old = MSG_TCG_TBL_HIST_DEST */
    MSG_TCG_SECURE_BOOT_ENABLE, /// 38
    MSG_TCG_TBL_RECOVERY,       /// 39
    MSG_TCG_TBL_UPDATE,         /// 40
    MSG_TCG_G3WR_SYNC_ZONE51,   /// 41
    MSG_TCG_NF_MID_LAST,        /// 42
} MSG_TCG_SUBOP_t;

//-------------------------------------------------------
#define TCG_SSC_EDRIVE              0xEE
#define TCG_SSC_OPAL                0xDD
#define TCG_SSC_PYRITE              0xCC
#define TCG_SSC_PYRITE_AES          0xCE

#if _TCG_ != TCG_PYRITE
  #if _TCG_ == TCG_EDRV
    #define TCG_DEVICE_TYPE         TCG_SSC_EDRIVE
  #else
    #define TCG_DEVICE_TYPE         TCG_SSC_OPAL
  #endif

  #define TCG_FS_BLOCK_SID_AUTH     mFALSE  //Max modify mTrue -> mFalse
  #define TCG_FS_PSID               mTRUE
  #define TCG_FS_CONFIG_NS          mFALSE

  #define TCG_TBL_ID                0x4F70616C  //Opal
  #define TCG_TBL_VER               0x0202

  #define LOCKING_RANGE_CNT         8   // total locking range: 9 (=1+8, including global range)
  #define TCG_AdminCnt              4
  #define TCG_UserCnt               (LOCKING_RANGE_CNT+1)
  #if TCG_FS_CONFIG_NS
    #define TCG_MAX_KEY_CNT         16  // for CNS feature
  #else
    #ifdef NS_MANAGE
	  #define TCG_MAX_KEY_CNT         (LOCKING_RANGE_CNT+1+32)
	#else
      #define TCG_MAX_KEY_CNT         (LOCKING_RANGE_CNT+1)
	#endif
  #endif
#else
  #if CO_SUPPORT_AES
    #define TCG_DEVICE_TYPE         TCG_SSC_PYRITE_AES
    #define TCG_TBL_ID              0x50794165  //PyAe
    #define TCG_MAX_KEY_CNT         1
  #else
    #define TCG_DEVICE_TYPE         TCG_SSC_PYRITE
    #define TCG_TBL_ID              0x50797269  //Pyri
    #define TCG_MAX_KEY_CNT         1
  #endif

  #define TCG_FS_BLOCK_SID_AUTH     mTRUE
  #define TCG_FS_PSID               mTRUE
  #define TCG_FS_CONFIG_NS          mFALSE

  #define TCG_TBL_VER               0x0202      //v2.01

  #define LOCKING_RANGE_CNT         0   //GlobalRange only
  #define TCG_AdminCnt              2
  #define TCG_UserCnt               2
#endif

#if TCG_FS_PSID
//#define PSID_PRINT_CHK
#endif

//-------------------------------------------------------

typedef union
{
    u32 all32;
    struct
    {
        u32 nf_sync_req       :1;
        u32 if_sync_resp      :1;
        u32 if_sync_req       :1;
        u32 nf_sync_resp      :1;
        u32 nf_ps4_init_req   :1;
        u32 if_ps4_init_resp  :1;
    } __attribute__((packed)) b;
} __attribute__((packed)) tcg_sync_t;


typedef struct
{
    u32 otp_secure_enabled;
    u32 fw_secure_enable;
    u32 loader_policy;
    u32 maincode_policy;
} sh_secure_boot_info_t;

#pragma pack(1)
typedef union
{
    struct
    {
        u16 page;       // CA5 -> 1 block = 768 pages, so page bits is more than 8 bits.
        u16 blk;
    };
    u32  all;
} tcgLaa_t;

#pragma pack(4)
typedef union
{
    u8 all[CFG_UDATA_PER_PAGE * L2P_PAGE_CNT];          // ep2 = 48K, ep3 = 80K
    struct
    {
        u16               TcgG4Header;
        u16               Tcg4Gap[0x400/sizeof(u16)-1];           // 1K
        tcgLaa_t TcgMbrL2P[G4G5_LAA_AMOUNT_LIMIT];   // ep2 = 37K, ep3 = 74K bytes
    } b;
} tG4;
#pragma pack()

#pragma pack(4)
typedef union
{
    u8 all[CFG_UDATA_PER_PAGE * L2P_PAGE_CNT];              // ep2 = 48K, ep3 = 80K
    struct
    {
        u16               TcgG5Header;
        u16               TcgG5Gap[0x400/sizeof(u16)-1];          // 1K
        tcgLaa_t TcgTempMbrL2P[G4G5_LAA_AMOUNT_LIMIT];   // ep2 = 37K, ep3 = 74K bytes
    } b;
} tG5;
#pragma pack()

#define SP_LOCKING_IDX          1   //UID_SP_Locking row index

#define CPIN_SID_IDX            0   //CPIN_SID row index
#define CPIN_MSID_IDX           1   //CPIN_MSID row index
#define CPIN_MSID_LEN          16   //CPIN_MSID row index

#define CPIN_PSID_IDX           (TCG_AdminCnt + 2)   //CPIN_PSID row index

#define SID_BLOCKED         0x20
#define SID_HW_RESET        0x40

#define TCG_DOMAIN_NORMAL    0
#define TCG_DOMAIN_ERROR     1
#define TCG_DOMAIN_DUMMY     2
#define TCG_DOMAIN_SHADOW    3

#pragma pack(4)
typedef struct
{
    u32 aesKey[8];
    u32 icv1[2];
    u32 xtsKey[8];
    u32 icv2[2];
} sKeySet;
#pragma pack()

typedef struct
{
    s32 state;
    sKeySet dek;
} sRawKey;

typedef struct {
    u64 rangeStart;
    u64 rangeEnd;
    u32 rangeNo;
    u32 blkcnt;
    u32 readLocked;
    u32 writeLocked;
} __attribute__((packed)) enabledLockingTable_t;

typedef u8 (*tcg_io_chk_func_t)(u64, u64, u16);

//#define tcg_ee_PsidTag        (smSysInfo->d.TCGData.d.TCGUsed.psidTag)
//#define tcg_ee_Psid           (smSysInfo->d.TCGData.d.TCGUsed.ee2.cPinPSID)


//TCG Status Definition
#define TCG_ACTIVATED   0x01
#define MBR_SHADOW_MODE 0x02
#define TCG_TBL_ERR     0x08

extern u32 mTcgStatus;        //TCG status variable for others
extern u32 mTcgActivated;       //TCG status activaed or not, for IO only
extern u16 mReadLockedStatus;   // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
extern u16 mWriteLockedStatus;  // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.
extern tcg_io_chk_func_t tcg_io_chk_range;

#if (CPU_ID==1)
void LockingRangeTable_Update(void);
u8 TcgRangeCheck(u64 lbaStart, u64 len, u16 locked_status);
u8 TcgRangeCheck_SMBR(u64 lbaStart, u64 len, u16 locked_status);
void ipc_tcg_change_chkfunc_BTN_wr(u32 sts);
#endif
#if (CPU_ID==4)
u8 TcgRangeCheck_cpu4(u64 lbaStart, u64 len, u16 locked_status);
u8 TcgRangeCheck_SMBR_cpu4(u64 lbaStart, u64 len, u16 locked_status);
#endif

int  TcgPsidVerify(void);

//#define TCG_GENERAL_BUF_SIZE        (256 * 1000)  //(128 * KBYTE)
#define TCG_TEMP_BUF_SZ             TCG_GENERAL_BUF_SIZE    // 128KB

//--------------------------------------------------------------------------------------------//


#else


//--------------------------------------------------------------------------------------------//


#define _TCG_DEBUG                  FALSE
#if _TCG_DEBUG
    #define KW_DBG
    #if _TCG_!=TCG_NONE
        #define BCM_test
    #endif
#endif

//#define _ALEXDEBUG_PAA
#define TCG_COLORFUL
#define zBLACK                      "\033[7;30m"
#define zRED                        "\033[7;31m"
#define zGREEN                      "\033[7;32m"
#define zYELLOW                     "\033[7;33m"
#define zBLUE                       "\033[7;34m"
#define zMAGENTA                    "\033[7;35m"
#define zCYAN                       "\033[7;36m"
#define zWHITE                      "\033[7;37m"
#define zNONE                       "\033[m"

#define alexcheck
#ifndef alexcheck
  #ifdef  TCG_COLORFUL
    #define TCG_PRINTF(F,args...)       Debug_DbgPrintf(zGREEN##F##zNONE, ##args)
    #define HERE(a)                     a == NULL ? Debug_DbgPrintf(zYELLOW"[A]%s\n"zNONE,__FUNCTION__) : Debug_DbgPrintf(zYELLOW"[A]%s() %x\n"zNONE,__FUNCTION__, a)
    #define HERE2(a,b)                  Debug_DbgPrintf(zYELLOW"[A]%s() %4X %4X\n"zNONE,__FUNCTION__, a, b)
  #else
    #define TCG_PRINTF(F,args...)       Debug_DbgPrintf(F, ##args)    ///< Debug message print out
    #define HERE(a)                     a == NULL ? Debug_DbgPrintf("[A]%s\n",__FUNCTION__) : Debug_DbgPrintf("[A]%s() %x\n",__FUNCTION__, a)
    #define HERE2(a,b)                  Debug_DbgPrintf("[A]%s() %4X %4X\n",__FUNCTION__, a, b)
  #endif
#else
  #define TCG_PRINTF(F,args...)       {}
  #define HERE(a)                     {}
  #define HERE2(a,b)                  {}
#endif

#define COMPILE_ASSERT(exp,str) extern char __ct_[(exp) ? 1 : -1] ///< compile time assert
#define zOK     0
#define zNG     -1
#define TCG_EEP_NOR

#define DEFAULT_LBU_SIZE            512  //alexcheck
#define TCG_USED_BLKS               50
#define TCG_CFG_BLK_CNT_PER_GRP     (TCG_USED_BLKS / 2)
#define TCG_MBR_CELLS               TCG_CFG_BLK_CNT_PER_GRP     // alexcheck

#define TCG_TBL_HISTORY_DESTORY     FALSE

#define TCG_GENERAL_BUF_SIZE        (256 * KBYTE)  //(128 * KBYTE)
#define TCG_TEMP_BUF_SZ             TCG_GENERAL_BUF_SIZE    // 128KB


#define TPER_FW_VERSION             0x00        //uint32

#define u64_TO_U32_H(X)             (u32)((X >> 32))
#define u64_TO_U32_L(X)             (u32)(X)
//------------------------------------------------------
// [[[ NAND config parameter ]]]
#define CFG_UDATA_PER_PAGE          16384  //1page = 16K
#define mNUM_AU_PER_PAGE            CFG_UDATA_PER_PAGE/2048
#define CFG_PAGE_ADDR_BIT_CNT       8
#define CFG_CH_BIT_CNT              3  //CH
#define CFG_CE_BIT_CNT              2  //CE
#define CFG_CHANNEL_CNT             (1 << CFG_CH_BIT_CNT)
#define CFG_CE_CNT                  (1 << CFG_CE_BIT_CNT)
#define CFG_LBA_PER_LAA             (4096 / 512)

//alexcheck******************************************************************
#define OFF         (0)                 ///< OFF, numeric value is 0
#define ON          (1)                 ///<  ON, numeric value is 1
typedef u32 Lba_t;
typedef u32 Hlba_t;          ///< Host Logical Block Address (HOST Block size)
typedef u32 Laa_t;           ///< Logical Allocation Address
typedef u32 Lma_t;           ///< Logical Media Address

typedef u32 Paa_t;           ///< Physical Allocation Address (NAND)
typedef u32 Pma_t;           ///< Physical Media Address

typedef void*               PVOID;      ///< void pointer
typedef const char*         Cstr_t;     ///< constant string pointer


#define   Error_t tERROR

typedef enum
{
    MSG_TCG_G1RDDEFAULT = 0,
    MSG_TCG_G2RDDEFAULT,        /// 01
    MSG_TCG_G3RDDEFAULT,        /// 02
    MSG_TCG_G4RDDEFAULT,        /// 03
    MSG_TCG_G5RDDEFAULT,        /// 04
    MSG_TCG_G4WRDEFAULT,        /// 05
    MSG_TCG_G5WRDEFAULT,        /// 06
    MSG_TCG_G4BUILDDEFECT,      /// 07
    MSG_TCG_G5BUILDDEFECT,      /// 08
    MSG_TCG_G1RD,               /// 09
    MSG_TCG_G1WR,               /// 10
    MSG_TCG_G2RD,               /// 11
    MSG_TCG_G2WR,               /// 12
    MSG_TCG_G3RD,               /// 13
    MSG_TCG_G3WR,               /// 14
    MSG_TCG_G4DMYRD,            /// 15
    MSG_TCG_G4DMYWR,            /// 16
    MSG_TCG_G5DMYRD,            /// 17
    MSG_TCG_G5DMYWR,            /// 18
    MSG_TCG_SMBRRD,             /// 19
    MSG_TCG_SMBRWR,             /// 20
    MSG_TCG_SMBRCOMMIT,         /// 21
    MSG_TCG_SMBRCLEAR,          /// 22
    MSG_TCG_TSMBRCLEAR,         /// 23
    MSG_TCG_DSRD,               /// 24
    MSG_TCG_DSWR,               /// 25
    MSG_TCG_DSCOMMIT,           /// 26
    MSG_TCG_DSCLEAR,            /// 27
    MSG_TCG_TDSCLEAR,           /// 28
    MSG_TCG_G4FTL,              /// 29
    MSG_TCG_G5FTL,              /// 30

    MSG_TCG_INIT_CACHE,         /// 31
    MSG_TCG_CLR_CACHE,          /// 32
    MSG_TCG_NOREEP_INIT,        /// 33
    MSG_TCG_NOREEP_RD,          /// 34
    MSG_TCG_NOREEP_WR,          /// 35
    MSG_TCG_NF_CPU_INIT,        /// 36
    MSG_TCG_SYNC_ZONE51_MEDIA,  /// 37    /* old = MSG_TCG_TBL_HIST_DEST */
    MSG_TCG_SECURE_BOOT_ENABLE, /// 38
    MSG_TCG_TBL_RECOVERY,       /// 39
    MSG_TCG_TBL_UPDATE,         /// 40
    MSG_TCG_G3WR_SYNC_ZONE51,   /// 41
    MSG_TCG_NF_MID_LAST,        /// 42
} MSG_TCG_SUBOP_t;


typedef void (*tHOST_CMD_HANDLER)(PVOID);
typedef PVOID tMSG_HANDLE;                  ///< message handle
#define HOSTMSG_MAX_DEPTH_BY_BIT 14

/// @brief indirect message common header
typedef union
{
    u32 all;
    struct
    {
        u32 opCode:8;           ///< message group operation code
        u32 gpCode:4;           ///< message group code
        volatile u32 status:4;  ///< message transaction status
        u32 cq:1;               ///< use completion queueing
        u32 fourceAbort:1;      ///< force abort xfer
        //u32 subOpCode:6;        ///< message group sub-operation code(ex:VU flow)
        //u32 next:8;             ///< message next index for HldHostMsg linked-list
        u32 next:HOSTMSG_MAX_DEPTH_BY_BIT; ///< message next index for HldHostMsg linked-list
    } b;
} tMSG_HDR;
COMPILE_ASSERT(sizeof(tMSG_HDR)==4, "size must be 4 bytes");

/// @brief Host media access message structure (MG_HOST_PERF.*)
typedef struct
{
    tMSG_HDR hdr;       ///< message common header
    tERROR error;       ///< error code
    u32  laaStart;      ///< laaStart
    u32  laaEnd;        ///< laaEnd
    u16  ssdStartIdx;   ///< SSD Start index
    u16  ssdEndIdx;     ///< SSD End Index
    u16  ssdCount;      ///< SSD count (same as the LAA count)
    u16  css;           ///< cache list status
    union
    {
        u16 all;
        struct
        {
            u16 priority    :1;     ///< priority
            u16 fua         :1;     ///< force unit access
        } b;
    } attr;
    union
    {
        u16 all;
        struct
        {
            u16 stm         :1;     ///< streaming mode
            u16 hcmdidx     :8;     ///< Cmd Index for streaming mode
            u16 stmidx      :7;
        } b;
    } streaming;
    u16 laaStartBmp;   ///< start laa bmp
    u16 laaEndBmp;     ///< end laa bmp
} tMSG_HOST;
/// @brief Host media access message structure (MG_TCG_PERF.*)
typedef struct
{
    tMSG_HDR      hdr;           ///< message common header
    tERROR        error;         ///< error code
    u32           subOpCode;
    u16           laas;           ///< laaStart
    u16           laae;           ///< laaEnd
    PVOID         pBuffer;        ///< buffer pointer
    u32           param[4];       ///< test function specific parameter 1..6
} tMSG_TCG;

#if 0
// monitor command handler function proto-type
typedef Error_t  (*MonCmdFunc_t)(Cstr_t , U32, U32*);

/// @brief monitor command decoding table
typedef struct
{
    char* cmdStr;           ///< command string
    char* helpStr;          ///< command help string
    u8  minArgc;       ///< minimum argument count
    u8  autoDec;       ///< automatic decode argument (number only)
    u8  exeTimePrint;  ///< print command execution time
    MonCmdFunc_t pFunc;     ///< function pointer to service command
} MonCmdTbl_t;


typedef struct
{
    void* prev;     ///< prevoius pointer (tail pointer)
    void* next;     ///< next pointer (head pointer)
} DlinkEntry_t;
#endif  // #if 0

typedef struct
{
    union
    {
        Lba_t  lba;                 ///< LBA (fixed block size, always 512B unit)
        Hlba_t hlba;                ///< host LBA
        Lma_t  lma;                 ///< Logical Media LBA
        u32    laa;                 ///< LAA
        u32    psflush;             ///ps3/4,scp flush
        u32    CID;                 ///for AER backup
    };
    union
    {
        u32 allFlags;          ///< all flags
        struct
        {
            u32 nb:17;                 ///< number of block (unit size depends on address type)
            u32 nsId0:3;               ///< name space identifier (zero based)
            u32 fua:1;                 ///< Force Unit Access
            u32 limitedRetry:1;        ///< Limited Retry
            u32 accessFrequency:4;     ///< DSM Access Frequency
            u32 accessLatency:2;       ///< DSM Access Latency
            u32 sequentialRequest:1;   ///< indicates part of sequential access
            u32 incompressible:1;      ///< indicates data is not compressible
            u32 reserved:2;            ///< reserved
        };
    };
} AddressInfo_t;

typedef struct
{
    void*    pBuffer;               ///< buffer pointer (start address)
    u16 ssdIndex;              ///< SSD index
    union
    {
        u16 allFlags;          ///< all flags
        struct
        {
            u16 flowCtrl:4;    ///< XferFlowCtrl_t flow control (non-streaming, IDS, streamingId1..7)
            u16 callBack:1;    ///< buffer fill completion call back
            u16 xferSetup:2;   ///< data xfer (see XferSetup_t)
            u16 cacheAloc:1;   ///< Cache Allocate (0=Non cache, 1=Cache allocate by CoreMain)
            u16 cacheAbort:1;   ///< Cache Allocate (0=Non cache, 1=Cache allocate by CoreMain)
            u16 res:7;
        };
    };
} BufDesc_t;

typedef void (*HcmdHandler_t)(void*);   ///< Host command handler function pointer

/// @brief host data transfer direction
typedef enum
{
    cXferD2h=0,             ///< Device to host transfer (Host read, data in)
    cXferH2d=1              ///< Host to device transfer (Host write, data out)
} XferDir_t;

typedef union
{
    u16 all;
    struct
    {
        u16 perfCmd:1;             ///< performance command
        u16 autoEoC:1;             ///< auto EoC (End of Command by HW or Interrupt)
        u16 wholeSdb:1;            ///< update whole SDB
        XferDir_t xferDir:1;            ///< data transfer direction (0=D2H, 1=H2D)
        u16 xferMode:2;            ///< Interface dependant transfer mode
        u16 xferNotify:1;          ///< xfer done event set notification
        u16 overlapped:1;          ///< Overlapped command processing
        u16 wrReqXferDone:1;       ///< write request after data transfer done
        u16 reservedForBase:3;     ///< reserved for base class attributes
        u16 reservedForChild:4;    ///< reserved for child class attributes
    } b;
} HcmdAttr_t;

#if 0
/// @brief host command queue structure
typedef struct
{
    DlinkEntry_t dlink;     ///< doubly linked list for state
    u16 index;         ///< index to other data structure
    u16 cmdId;         ///< command identifier from HLD
    volatile    HcmdAttr_t attr;        ///< command attribute
    S16 adjVsbc;        ///< adjust Valid Streaming Block Count (512B unit)
    AddressInfo_t addrInfo; ///< address information
    BufDesc_t bufDesc;      ///< buffer descriptor
    HcmdHandler_t fptr;     ///< handler function pointer
    U32 asycType;      ///< Asyc Event report type
    U32 asycInfo;      ///< Asyc Event report Information
    U32 status;        ///< command status
    Error_t error;          ///< error code
} HcmdQ_t;
#endif

typedef union
{
    u32 all;
    struct
    {
        u32 opCode:8;                      ///< message group operation code
        u32 gpCode:4;                      ///< message group code
        volatile u32 status:4;             ///< message transaction status
        u32 cq:1;                          ///< use completion queueing
        u32 fourceAbort:1;                 ///< force abort xfer
        u32 next:HOSTMSG_MAX_DEPTH_BY_BIT; ///< message next index for HldHostMsg linked-list
    } b;
} MsgHdr_t;

typedef union
{
    u32 all;
    struct
    {
        u32 cmdId:16;          ///< command Id (unique identifier number from HAL)
        XferDir_t direction:1;      ///< data transfer direction (0=D2H, 1=H2D)
        u32 autoEoC:1;         ///< auto EoC
        u32 hostIrq:1;         ///< host interrupt request in case of autoEoC
        u32 mode:2;            ///< Transfer mode (Protocol specific)
        u32 chained:1;         ///< Transfer FIFO chained (HLD specific aggregation)
    } b;
} XferAttr_t;

typedef struct
{
    void*               pBuffer;               ///< buffer pointer (start address)
    AddressInfo_t       addrInfo;
    XferAttr_t          attrInfo;
} CmdInfo_t;

typedef struct
{
    u16 ssdIndex;              ///< SSD index
    u16 ssdEndIdx;             ///< 2byte
    u8  ssdCnt;                ///< 1byte
    u8  css;                   ///< 1byte
    u8  laaStartBmp;           ///< start laa bmp
    u8  laaEndBmp;             ///< end laa bmp
    union
    {
        u32 allFlags;          ///< all flags
        struct
        {
            u32 flowCtrl:4;    ///< XferFlowCtrl_t flow control (non-streaming, IDS, streamingId1..7)
            u32 callBack:1;    ///< buffer fill completion call back
            u32 xferSetup:2;   ///< data xfer (see XferSetup_t)
            u32 cacheAloc:1;   ///< Cache Allocate (0=Non cache, 1=Cache allocate by CoreMain)
            u32 cacheAbort:1;
            u32 SQID:10;       //LeePeng20180611
            u32 rsvd:13;
        };
    };
} BufDesc_t1;

/// @brief Host media read or write message structure (cMgHostPerf.*)
typedef struct
{
    MsgHdr_t        hdr;             ///< message common header   4byte
    Error_t         error;           ///< error code              4byte

    CmdInfo_t       cmdInfo;         ///< CMD Info                16byte
    BufDesc_t1      bufDesc;         ///< buffer Desc             12byte
    u32        cmdTime;         //LeePeng20180611
} MsgHostIO_t;

// #define DBG_P(var_cnt, args...)

// Security State define
#define SCU_SEC0                            0   ///< SEC0 : Power Down / No PW
#define SCU_SEC1                            1   ///< SEC1 : Power On / No PW / Unlock / No Frozen
#define SCU_SEC2                            2   ///< SEC2 : Power On / No PW / Unlock / Frozen
#define SCU_SEC3                            3   ///< SEC3 : Power Down PW
#define SCU_SEC4                            4   ///< SEC4 : Power On / PW / Lock / No Frozen
#define SCU_SEC5                            5   ///< SEC5 : Power On / PW / Unlock / No Frozen
#define SCU_SEC6                            6   ///< SEC6 : Power On / PW / Unlock / Frozen
#define SEC_PW_SIZE                         16  ///< 16 Word = 32 Bytes
#define SEC_PW_MAX_RETRY_CNT                5   ///< Max PW retry count
#define MAX_LEVEL                           1   ///< PW level
#define HIGH_LEVEL                          0   ///< PW level
#define MASTER_PW                           1   ///< PW Type
#define USER_PW                             0   ///< PW Type
#define SEC_ERASE_TIME                      1  // * 2 min
#define SEC_ENH_ERASE_TIME                  1  // * 2 min


typedef struct
{
    u32 OPC:8;         ///< Opcode (DW0 bits[7:0])
    u32 FUSE:2;        ///< Fused Operation (00=normal, 01=fused 1st command, 10=fused last command, 11=reserved) (DW0[9:8]
    u32 reserved0:5;   ///< Reserved (DW0[14:10]
    u32 PSDT:1;        ///< PRP or SGL for Data Transfer(PSDT) (DW0[bits(15)]
    u32 CID:16;        ///< Command Identifier (DW0[[bits(31:16)]

    u32 NSID;          ///< Name space Identifier (DW1)
    u32 reserved2[2];  ///< Reserved (DW3..2
    u64 MPTR;          ///< Meta data Pointer (DW5..4)
    u64 PRP1;          ///< PRP Entry 1 (DW7..6)
    u64 PRP2;          ///< PRP Entry 2 (DW9..8)

} AdminCommandCommon_t;


#if 0
typedef  struct
{
    AdminCommandCommon_t DW9_0;    ///< DW9..0

    u32 NSSF:8;          ///< NVMe Security Specific Field (DW10 bits[07:00])
    u32 SPSP:16;         ///< SP Specific (DW10 bits[23:08])
    u32 SECP:8;          ///< Security Protocol (DW10 bits[31:24])

    u32 AL;              ///< Allocation Length (DW11)
    u32 reserved12[4];   ///< DW15..12

} NvmeAdminSecurityReceiveCommand_t;

typedef  struct
{
    AdminCommandCommon_t DW9_0;    ///< DW9..0

    u32 NSSF:8;          ///< NVMe Security Specific Field (DW10 bits[07:00])
    u32 SPSP:16;         ///< SP Specific (DW10 bits[23:08])
    u32 SECP:8;          ///< Security Protocol (DW10 bits[31:24])

    u32 TL;              ///< Transfer Length (DW11)
    u32 reserved12[4];   ///< DW15..12

} NvmeAdminSecuritySendCommand_t;
#endif

#define IS_NVME_ERROR_CODE(EC)              (((EC)&0xFF000000)==cEcNvmeErrorBase)
// #define MK_NVME_ERROR_CODE(SC, SCT, M, DNR) (Error_t)(cEcNvmeErrorBase|(SC)|((SCT)<<8)|((M)<<14)|((DNR)<<15))
#define IS_NVME_ASYNC_ERROR_CODE(EC)        (((EC)&0xFF800000)==cEcNvmeAsyncErrorBase)


typedef enum
{
    cGenericCommandStatus = 0,
    cCommandSpecificStatus = 1,
    cMediaErrors = 2,
    cVendorSpecific = 7
} NvmeStatusCodeType_t;

/// @brief NVMe Status Code - Generic command
typedef enum
{
    /// Admin Command Set
    cSuccessfulCompletion = 0x00,
    cInvalidCommandOpcode = 0x01,
    cInvalidFieldIncommand = 0x02,
    cCommandIdConflict = 0x03,
    cDataTransferError = 0x04,
    cCommandsAbortedDueToPowerLossNotification = 0x05,
    cInternalDeviceError = 0x06,
    cCommandAbortRequested = 0x07,
    cCommandAbortedDueToSqDeletion = 0x08,
    cCommandAbortedDueToFailedFusedCommand = 0x09,
    cCommandAbortedDueToMissingFusedCommand = 0x0A,
    cInvalidNamespaceOrFormat = 0x0B,
    cCommandSequenceError = 0x0C,
    cInvalidSglLastSegmentDescriptor = 0x0D,
    cInvalidNumberOfSglDescriptors = 0x0E,
    cDataSglLengthInvalid = 0x0F,
    cMetadataSglLengthInvalid = 0x10,
    cSglDescriptorTypeInvalid = 0x11,
    cPRPOffsetInvalid=0x13,
    cSanitizeFailed=0x1C,
    cSanitizeInProgress=0x1D,
    /// NVM Command Set
    cLbaOutOfRange = 0x80,
    cCapacityExceeded = 0x81,
    cNamespaceNotReady = 0x82,
    cReservationConflict = 0x83,

    /// Vendor Specific Set

} NvmeGenericCommandStatusCode_t;

/// @brief NVMe Status Code - Command specific
typedef enum
{
    /// Admin Command Set
    cCompletionQueueInvalid = 0x00,
    cInvalidQueueIdentifier = 0x01,
    cMaximumQueueSizeExceeded = 0x02,
    cAbortCommandLimitExceeded = 0x03,
    cAsynchronousEventRequestLimitExceeded = 0x05,
    cInvalidFirmwareSlot = 0x06,
    cInvalidFirmwareImage = 0x07,
    cInvalidInterruptVector = 0x08,
    cInvalidLogPage = 0x09,
    cInvalidFormat = 0x0A,
    cFirmwareApplicationRequiresConventionalReset = 0x0B,
    cInvalidQueueDeletion = 0x0C,
    cFeatureIdentifierNotSaveable = 0x0D,
    cFeatureNotChangeable = 0x0E,
    cFeatureNotNamespaceSpecific = 0x0F,
    cFirmwareApplicationRequiresNvmSubsystemReset = 0x10,
    cNamespaceInsufficientCapacity = 0x15,   // Namespace Management
    cNamespaceIdentifierUnavailable = 0x16,  // Namespace Management
    cNamespaceAlreadyAttached = 0x18,        // Namespace Attachment
    cNamespaceIsPrivate = 0x19,              // Namespace Attachment
    cNamespaceNotAttached = 0x1A,            // Namespace Attachment
    cThinProvisioningNotSupported = 0x1B,    // Namespace Management
    cControllerListInvalid = 0x1C,           // Namespace Attachment
    cDeviceSelftestInProgress = 0x1D, //Device Self-test

    /// NVM Comand Set
    cConflictingAttributes = 0x80,
    cInvalidProtectionInformation = 0x81,
    cAttemptedWriteToReadOnlyRange = 0x82,

    /// Vendor Specific Set

} NvmeCommandSpecificStatusCode_t;


/// @brief NVMe Status Code - MEdia error
typedef enum
{
    /// Admin Command Set

    /// NVM Comand Sets
    cWriteFault = 0x80,
    cUnrecoveredReadError = 0x81,
    cEndToEndGuardCheckError = 0x82,
    cEndToEnApplicationTagCheckError = 0x83,
    cEndToEnReferenceTagCheckError = 0x84,
    cCompareFailure = 0x85,
    cAccessDenied = 0x86,

    /// Vendor Specific Set

} NvmeMediaErrorStatusCode_t;

/// @brief Admin Status Value definitions (5. Admin Command Set)
/// @brief Asynchronous Event Request CompletionQ Dword 0 - Asynchronous Event Type
typedef enum
{
    cErrorStatus = 0,
    cSmartHealthStatus = 1,
    cNotice = 2,
    cIoCommandSetSpecificStatus = 6,

} AsynchronousEventCompletionQEventType_t;

/// @brief Asynchronous Event Information Error Status
typedef enum
{
    cWriteToInvalidDoorbellRegister = 0,
    cInvalidDoorbellWriteValue = 1,
    cDiagnosticFailure = 2,
    cPersistentInternalDeviceError = 3,
    cTransientInternalDeviceError = 4,
    cFirmwareImageLoadError = 5,

} AsynchronousEventInformationErrorStatus_t;

/// @brief Asynchronous Event Information SMART / Health Status
typedef enum
{
    cDeviceReliability = 0,
    cTemperatureAboveThreshold = 1,
    cSpareBelowThreshold = 2,
    cReliability =3,
} AsynchronousEventInformationSMARTHealthStatus_t;

/// @brief Asynchronous Event Information NVM Command Notice
typedef enum
{
    cNamespaceAttributeChanged = 0,
    cFirmwareActivationStarting =1,
    cTelemetryLogChanged=2,

} AsynchronousEventInformationNVMCommandNotice_t;

/// @brief Asynchronous Event Information NVM Command Set Specific Status
typedef enum
{
    cReservationLogPageAvailable = 0,
    cSanitizeOperationCompleted = 1,           //sanitize
} AsynchronousEventInformationNVMCommandSetSpecificStatus_t;

// alexcheck *
typedef enum
{
    MRE_D_ENCRYPT       = 0,
    MRE_D_DECRYPT,
    MRE_D_LAST
} Mre_Define_Secuiry_t;

#define SSD_ADDR_SHIFT   0
// alexcheck &

static const bool cMayRetry = 0;                // NVMe status field (NDR)
static const bool cDoNotRetry = 1;

static const bool cNoMoreStatusInformation=0;   // NVMe status field (M)
static const bool cMoreStatusInformation=1;


#define TCG_SMBR_LAA_START          0x0000  // size = 0x2000  pages  = 0x8000000 bytes(128MB)
#define TCG_SMBR_LAA_END            0x1FFF  // size = 0x2000  pages  = 0x8000000 bytes(128MB)

#define TCG_G1_LAA0                 0x2000  // 1 laa = 16K for G1
#define TCG_G2_LAA0                 0x2001  // 2 laa = 32K for G2
#define TCG_G2_LAA1                 0x2002  //
#define TCG_G3_LAA0                 0x2003  // 2 laa = 32K for G3
#define TCG_G3_LAA1                 0x2004  //

#define TCG_DS_LAA_START            0x2005  // size = 0x280  pages  = 0xA00000 bytes(10MB)
#define TCG_DS_LAA_END              0x228C
#define TCG_DUMMY_LAA               0x228D  // this is for  TcgTempMbrClean()
#define TCG_DS_DUMMY_LAA            0x228E  // this is for  TcgTempDSClean()

#define TCG_G1_DEFAULT_LAA0         0x228F
#define TCG_G2_DEFAULT_LAA0         0x2290
#define TCG_G2_DEFAULT_LAA1         0x2291
#define TCG_G3_DEFAULT_LAA0         0x2292
#define TCG_G3_DEFAULT_LAA1         0x2293
#define TCG_LAST_USE_LAA            TCG_G3_DEFAULT_LAA1 + 1

#define SECURE_COM_BUF_SZ           0xD000                  // 52KB
#define TCG_TEMP_BUF_SZ             TCG_GENERAL_BUF_SIZE    // 128KB
#define LAA_TBL_SZ                  (CFG_UDATA_PER_PAGE * L2P_PAGE_CNT)     // 48KB

#define TCG_TMP_BUF_ALL_CNT         (TCG_TEMP_BUF_SZ >> 12)  //4k unit

//alexcheck&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&





#define TCG_SSC_EDRIVE              0xEE
#define TCG_SSC_OPAL                0xDD
#define TCG_SSC_PYRITE              0xCC
#define TCG_SSC_PYRITE_AES          0xCE

#if _TCG_ != TCG_PYRITE
  #if _TCG_ == TCG_EDRV
    #define TCG_DEVICE_TYPE         TCG_SSC_EDRIVE
  #else
    #define TCG_DEVICE_TYPE         TCG_SSC_OPAL
  #endif

  #define TCG_FS_BLOCK_SID_AUTH     TRUE
  #define TCG_FS_PSID               TRUE
  #define TCG_FS_CONFIG_NS          FALSE

  #define TCG_TBL_ID                0x4F70616C  //Opal
  #define TCG_TBL_VER               0x0202

  #define LOCKING_RANGE_CNT         8   // total locking range: 9 (=1+8, including global range)
  #define TCG_AdminCnt              4
  #define TCG_UserCnt               (LOCKING_RANGE_CNT+1)
  #if TCG_FS_CONFIG_NS
    #define TCG_MAX_KEY_CNT         16  // for CNS feature
  #else
    #define TCG_MAX_KEY_CNT         (LOCKING_RANGE_CNT+1)
  #endif
#else
  #if CO_SUPPORT_AES
    #define TCG_DEVICE_TYPE         TCG_SSC_PYRITE_AES
    #define TCG_TBL_ID              0x50794165  //PyAe
    #define TCG_MAX_KEY_CNT         1
  #else
    #define TCG_DEVICE_TYPE         TCG_SSC_PYRITE
    #define TCG_TBL_ID              0x50797269  //Pyri
    #define TCG_MAX_KEY_CNT         1
  #endif

  #define TCG_FS_BLOCK_SID_AUTH     TRUE
  #define TCG_FS_PSID               TRUE
  #define TCG_FS_CONFIG_NS          FALSE

  #define TCG_TBL_VER               0x0202      //v2.01

  #define LOCKING_RANGE_CNT         0   //GlobalRange only
  #define TCG_AdminCnt              2
  #define TCG_UserCnt               2
#endif

#define TCG_G1_TAG                  0x11110000
#define TCG_G2_TAG                  0x22220000
#define TCG_G3_TAG                  0x33330000
#define TCG_END_TAG                 0x454E4454

#define SP_LOCKING_IDX          1   //UID_SP_Locking row index

#define CPIN_SID_IDX            0   //CPIN_SID row index
#define CPIN_MSID_IDX           1   //CPIN_MSID row index
#define CPIN_MSID_LEN          16   //CPIN_MSID row index

#define CPIN_PSID_IDX           (TCG_AdminCnt + 2)   //CPIN_PSID row index

#define LCK_CPIN_ADMIN1_IDX     0   //Lck CPIN_Admin1_IDX
#define SI_LOCKING_RANGE_CNT    LOCKING_RANGE_CNT   //total locking range: 9 (=1+8, including global range)

#define TCG_EE_LAST_USED_PATTEN 0x4C415354 // "LAST"


#pragma pack(4)
typedef struct
{
    u32 aesKey[8];
    u32 icv1[2];
    u32 xtsKey[8];
    u32 icv2[2];
} sKeySet;
#pragma pack()

typedef struct
{
    s32 state;
    sKeySet dek;
} sRawKey;

/*
typedef struct
{
    u32 iv[4];
    u32 aesKey[8];
    u32 tag;
} sTcgKekInfo;

typedef struct
{
    u32 tag;
    u32 version;
    u32 history;
} sTcgKekHeader_Nor;

typedef struct
{
    sTcgKekHeader_Nor  header;
    sTcgKekInfo        kekInfoNew;
    sTcgKekInfo        kekInfoCur;
    u32                crc;
} sTcgKekData_Nor;
*/

#define TCG_KEK_NOR_VER 0x00010000
#define ROOTKEY_TAG             (0x524F4F54)    // "ROOT"

#if 0 //CO_SUPPORT_ROOTKEY
    #define ROOTKEY_OTP_ID          (OTP_RKEK1)
    #define ROOTKEY_TAG             (0x524F4F54)    // "ROOT"
    #define ROOTKEY_TAG_OTP_ID      (OTP_OEM_BLOCK1)
    #define ROOTKEY_TAG_OTP_OFFSET  (0)  // Must less than 6.

    typedef enum
    {
        ROOTKEY_CLEAN = 0,
        ROOTKEY_NORMAL,
        ROOTKEY_CMP_FAIL,
        ROOTKEY_OTP_ERROR,
        ROOTKEY_LAST
    } ROOTKEY_STATE_t;
#endif

#define DATASTORE_LEN               0x00A20000      //10.28MB
#define MBR_LEN                     0x08000000      //128MB

#define DEFAULT_LOGICAL_BLOCK_SIZE  512
#if (MUTI_LBAF == TRUE)
  #define TCG_LogicalBlockSize      smLogicBlockSize
#else
  #define TCG_LogicalBlockSize      DEFAULT_LOGICAL_BLOCK_SIZE
#endif
//#define TCG_AlignmentGranularity    (CFG_UDATA_PER_PAGE/LBA_SIZE_IN_BYTE)  //8KB=16LBA, 16KB=32LBA
#define DEFAULT_AlignmentGranularity 8
#if (MUTI_LBAF == TRUE)
  #define TCG_AlignmentGranularity  (0x1000/smLogicBlockSize)   //4KB/smLogicBlockSize
#else
  #define TCG_AlignmentGranularity    8   //4KB=8LBA
#endif

#define TCG_LowestAlignedLBA        0
#define L2P_PAGE_CNT                3
#define G4G5_LAA_AMOUNT_LIMIT       ((L2P_PAGE_CNT * CFG_UDATA_PER_PAGE - 0x400) / sizeof(u32)) // 0x4800  //18432

#define DSTBL_ALIGNMENT             1    //byte
#define DSTBL_MAX_NUM               (LOCKING_RANGE_CNT + 1)

//------------------------------------------------------
#define UID_Null                    (0x0000000000000000)

//UID for SessionManager
#define UID_ThisSP                  0x00000000000001
#define SMUID                       0x000000000000FF
//SessionManager Method UID
#define SM_MTD_Properties           0x000000000000FF01  // Properties
#define SM_MTD_StartSession         0x000000000000FF02  // StartSession
#define SM_MTD_SyncSession          0x000000000000FF03  // SyncSession
//#define SM_MTD_StartTrustedSession    0x000000000000FF04    // StartTrustedSession
//#define SM_MTD_SyncTrustedSession     0x000000000000FF05    // SyncTrustedSession
#define SM_MTD_CloseSession         0x000000000000FF06  // CloseSession

//UID for Column Type
#define UID_CT_Authority_object_ref (0x0000000500000C05)
#define UID_CT_boolean_ACE          (0x000000050000040E)

//UID for AdminSP:
//SPInfo Table
//#define UID_Admin_SPInfo_Admin   (0x0000000200000001)
#define UID_SPInfo_Null             (0x0000000200000000)
#define UID_SPInfo                  (0x0000000200000001)

//SPTemplate Table
#define UID_SPTemplate              (0x0000000300000000)
#define UID_SPTemplate_1            (0x0000000300000001)
#define UID_SPTemplate_2            (0x0000000300000002)

//Table Table
#define UID_Table                   (0x0000000100000000)
#define UID_Table_Table             (0x0000000100000001)
#define UID_Table_SPInfo            (0x0000000100000002)
#define UID_Table_SPTemplates       (0x0000000100000003)
#define UID_Table_MethodID          (0x0000000100000006)
#define UID_Table_AccessControl     (0x0000000100000007)
#define UID_Table_ACE               (0x0000000100000008)
#define UID_Table_Authority         (0x0000000100000009)
#define UID_Table_CPIN              (0x000000010000000B)
#define UID_Table_TPerInfo          (0x0000000100000201)
#define UID_Table_Template          (0x0000000100000204)
#define UID_Table_SP                (0x0000000100000205)
#define UID_Table_RemovalMechanism  (0x0000000100001101)

//MethodID Table
#define UID_MethodID                (0x0000000600000000)
#define UID_MethodID_Next           (0x0000000600000008)
#define UID_MethodID_GetACL         (0x000000060000000D)
#define UID_MethodID_Get            (0x0000000600000016)
#define UID_MethodID_Set            (0x0000000600000017)
#define UID_MethodID_Authenticate   (0x000000060000001C)    //Opal2
#define UID_MethodID_Revert         (0x0000000600000202)
#define UID_MethodID_Activate       (0x0000000600000203)
#define UID_MethodID_Random         (0x0000000600000601)    //Opal2

//AccessControl Table: x
#define UID_AccessControl           (0x0000000700000000)

//ACE Table
#define UID_ACE                     (0x0000000800000000)
#define UID_ACE_Anybody             (0x0000000800000001)
#define UID_ACE_Admin               (0x0000000800000002)
#define UID_ACE_Set_Enabled         (0x0000000800030001)
#define UID_ACE_CPIN_SID_Get_NOPIN  (0x0000000800008C02)
#define UID_ACE_CPIN_SID_Set_PIN    (0x0000000800008C03)
#define UID_ACE_CPIN_MSID_Get_PIN   (0x0000000800008C04)
#define UID_ACE_CPIN_Get_PSID_NoPIN (0x00000008000100e1)    //PSID
#define UID_ACE_CPIN_Admins_Set_PIN (0x000000080003A001)    //Opal2
#define UID_ACE_TPerInfo_Set_PReset (0x0000000800030003)    //Opal2
#define UID_ACE_SP_SID              (0x0000000800030002)
#define UID_ACE_SP_PSID             (0x00000008000100e0)
#define UID_ACE_RMMech_Set_RM       (0x0000000800050001)    //Pyrite v2.00_r1.16

//Authority Table
#define UID_Authority               (0x0000000900000000)
#define UID_Authority_Anybody       (0x0000000900000001)
#define UID_Authority_Admins        (0x0000000900000002)
#define UID_Authority_Makers        (0x0000000900000003)
#define UID_Authority_SID           (0x0000000900000006)
#define UID_Authority_PSID          (0x000000090001FF01)    //eDrive

#define UID_Authority_AdmAdmins     (0x0000000900000200)
#define UID_Authority_AdmAdmin1     (0x0000000900000201)
#define UID_Authority_AdmAdmin2     (0x0000000900000202)
#define UID_Authority_AdmAdmin3     (0x0000000900000203)
#define UID_Authority_AdmAdmin4     (0x0000000900000204)

//C_PIN Table
#define UID_CPIN                    (0x0000000B00000000) //CPIN Table
#define UID_CPIN_SID                (0x0000000B00000001)
#define UID_CPIN_MSID               (0x0000000B00008402)

#define UID_CPIN_AdmAdmin1          (0x0000000B00000201)
#define UID_CPIN_AdmAdmin2          (0x0000000B00000202)
#define UID_CPIN_AdmAdmin3          (0x0000000B00000203)
#define UID_CPIN_AdmAdmin4          (0x0000000B00000204)

#define UID_CPIN_PSID               (0x0000000B0001FF01) //eDriver

//TPerInfo Table
#define UID_TPerInfo                (0x0000020100030001)

//Template Table
#define UID_Template                (0x0000020400000000)
#define UID_Template_Base           (0x0000020400000001)
#define UID_Template_Admin          (0x0000020400000002)
#define UID_Template_Locking        (0x0000020400000006)
#define UID_Template_InterfaceControl   (0x0000020400000007)

//SP Table
#define UID_SP                      (0x0000020500000000)
#define UID_SP_Admin                (0x0000020500000001)
#define UID_SP_Locking              (0x0000020500000002)

#define UID_RemovalMechanism        (0x0000110100000001) //Pyrite v2.0

//UID for LockingSP:
//SPInfo Table
//#define UID_Locking_SPInfo_Locking      (0x0000000200000001)

//SPTemplate Table, ?? check Table 245 and Table 21 ??
//#define UID_Locking_SPTemplate_Base     (0x0000000300000001)
//#define UID_Locking_SPTemplate_Locking  (0x0000000300000002)
//#define UID_Locking_SPTemplate_InterfaceControl (0x0000000300000003)

//Table Table
//#define UID_Table_Table       (0x0000000100000001)
//#define UID_Table_SPInfo      (0x0000000100000002)
//#define UID_Table_SPTemplates (0x0000000100000003)
//#define UID_Table_MethodID    (0x0000000100000006)
//#define UID_Table_AccessControl   (0x0000000100000007)
//#define UID_Table_ACE         (0x0000000100000008)
//#define UID_Table_Authority   (0x0000000100000009)
//#define UID_Table_CPIN        (0x000000010000000B)
#define UID_Table_SecretProtect         (0x000000010000001D)    //Opal2
#define UID_Table_LockingInfo           (0x0000000100000801)
#define UID_Table_Locking               (0x0000000100000802)
#define UID_Table_MBRControl            (0x0000000100000803)
#define UID_Table_MBR                   (0x0000000100000804)
#define UID_Table_K_AES_128             (0x0000000100000805)
#define UID_Table_K_AES_256             (0x0000000100000806)
#define UID_Table_RestrictedCommands    (0x0000000100000C01)

#define UID_Table_DataStore     (0x0000000100001001)
#define UID_Table_DataStore2    (0x0000000100001002)    //FS: DataStore
#define UID_Table_DataStore3    (0x0000000100001003)
#define UID_Table_DataStore4    (0x0000000100001004)
#define UID_Table_DataStore5    (0x0000000100001005)
#define UID_Table_DataStore6    (0x0000000100001006)
#define UID_Table_DataStore7    (0x0000000100001007)
#define UID_Table_DataStore8    (0x0000000100001008)
#define UID_Table_DataStore9    (0x0000000100001009)


//MethodID Table
//#define UID_MethodID_Next     (0x0000000600000008)
//#define UID_MethodID_GetACL   (0x000000060000000D)
#define UID_MethodID_GenKey     (0x0000000600000010)
#define UID_MethodID_RevertSP   (0x0000000600000011)
//#define UID_MethodID_Get      (0x0000000600000016)
//#define UID_MethodID_Set      (0x0000000600000017)
#define UID_MethodID_Reactivate (0x0000000600000801)    //SingleUserMode FS
#define UID_MethodID_Erase      (0x0000000600000803)    //SingleUserMode FS
#define UID_MethodID_Assign     (0x0000000600000804)    //ConfigNS FS
#define UID_MethodID_Deassign   (0x0000000600000805)    //ConfigNS FS

//AccessControl Table: x

//ACE Table
//#define UID_ACE_Anybody         (0x0000000800000001)
//#define UID_ACE_Admin           (0x0000000800000002)
#define UID_ACE_Anybody_Get_CommonName      (0x0000000800000003)    //Opal2
#define UID_ACE_Admins_Set_CommonName       (0x0000000800000004)    //Opal2
#define UID_ACE_ACE_Get_All                 (0x0000000800038000)
#define UID_ACE_ACE_Set_BooleanExpression   (0x0000000800038001)
#define UID_ACE_Authority_Get_All           (0x0000000800039000)
#define UID_ACE_Authority_Set_Enabled       (0x0000000800039001)
#define UID_ACE_User1_Set_CommonName        (0x0000000800044001)    //Opal2
#define UID_ACE_User2_Set_CommonName        (0x0000000800044002)    //Opal2
#define UID_ACE_User3_Set_CommonName        (0x0000000800044003)    //Opal2
#define UID_ACE_User4_Set_CommonName        (0x0000000800044004)    //Opal2
#define UID_ACE_User5_Set_CommonName        (0x0000000800044005)    //Opal2
#define UID_ACE_User6_Set_CommonName        (0x0000000800044006)    //Opal2
#define UID_ACE_User7_Set_CommonName        (0x0000000800044007)    //Opal2
#define UID_ACE_User8_Set_CommonName        (0x0000000800044008)    //Opal2
#define UID_ACE_User9_Set_CommonName        (0x0000000800044009)    //Opal2

#define UID_ACE_C_PIN_Admins_Get_All_NOPIN  (0x000000080003A000)
#define UID_ACE_C_PIN_Admins_Set_PIN        (0x000000080003A001)
#define UID_ACE_C_PIN_User_Set_PIN          (0x000000080003A800)
#define UID_ACE_C_PIN_User1_Set_PIN         (0x000000080003A801)
#define UID_ACE_C_PIN_User2_Set_PIN         (0x000000080003A802)
#define UID_ACE_C_PIN_User3_Set_PIN         (0x000000080003A803)
#define UID_ACE_C_PIN_User4_Set_PIN         (0x000000080003A804)
#define UID_ACE_C_PIN_User5_Set_PIN         (0x000000080003A805)
#define UID_ACE_C_PIN_User6_Set_PIN         (0x000000080003A806)
#define UID_ACE_C_PIN_User7_Set_PIN         (0x000000080003A807)
#define UID_ACE_C_PIN_User8_Set_PIN         (0x000000080003A808)
#define UID_ACE_C_PIN_User9_Set_PIN         (0x000000080003A809)

#define UID_ACE_K_AES_Mode                      (0x000000080003BFFF)
#define UID_ACE_K_AES_256_GlobalRange_GenKey    (0x000000080003B800)
#define UID_ACE_K_AES_256_Range1_GenKey         (0x000000080003B801)
#define UID_ACE_K_AES_256_Range2_GenKey         (0x000000080003B802)
#define UID_ACE_K_AES_256_Range3_GenKey         (0x000000080003B803)
#define UID_ACE_K_AES_256_Range4_GenKey         (0x000000080003B804)
#define UID_ACE_K_AES_256_Range5_GenKey         (0x000000080003B805)
#define UID_ACE_K_AES_256_Range6_GenKey         (0x000000080003B806)
#define UID_ACE_K_AES_256_Range7_GenKey         (0x000000080003B807)
#define UID_ACE_K_AES_256_Range8_GenKey         (0x000000080003B808)
#define UID_ACE_Locking_GRange_Get_RangeStartToActiveKey   (0x000000080003D000)
#define UID_ACE_Locking_Range1_Get_RangeStartToActiveKey   (0x000000080003D001)
#define UID_ACE_Locking_Range2_Get_RangeStartToActiveKey   (0x000000080003D002)
#define UID_ACE_Locking_Range3_Get_RangeStartToActiveKey   (0x000000080003D003)
#define UID_ACE_Locking_Range4_Get_RangeStartToActiveKey   (0x000000080003D004)
#define UID_ACE_Locking_Range5_Get_RangeStartToActiveKey   (0x000000080003D005)
#define UID_ACE_Locking_Range6_Get_RangeStartToActiveKey   (0x000000080003D006)
#define UID_ACE_Locking_Range7_Get_RangeStartToActiveKey   (0x000000080003D007)
#define UID_ACE_Locking_Range8_Get_RangeStartToActiveKey   (0x000000080003D008)
#define UID_ACE_Locking_GRange_Set_RdLocked         (0x000000080003E000)
#define UID_ACE_Locking_Range1_Set_RdLocked         (0x000000080003E001)
#define UID_ACE_Locking_Range2_Set_RdLocked         (0x000000080003E002)
#define UID_ACE_Locking_Range3_Set_RdLocked         (0x000000080003E003)
#define UID_ACE_Locking_Range4_Set_RdLocked         (0x000000080003E004)
#define UID_ACE_Locking_Range5_Set_RdLocked         (0x000000080003E005)
#define UID_ACE_Locking_Range6_Set_RdLocked         (0x000000080003E006)
#define UID_ACE_Locking_Range7_Set_RdLocked         (0x000000080003E007)
#define UID_ACE_Locking_Range8_Set_RdLocked         (0x000000080003E008)
#define UID_ACE_Locking_GRange_Set_WrLocked         (0x000000080003E800)
#define UID_ACE_Locking_Range1_Set_WrLocked         (0x000000080003E801)
#define UID_ACE_Locking_Range2_Set_WrLocked         (0x000000080003E802)
#define UID_ACE_Locking_Range3_Set_WrLocked         (0x000000080003E803)
#define UID_ACE_Locking_Range4_Set_WrLocked         (0x000000080003E804)
#define UID_ACE_Locking_Range5_Set_WrLocked         (0x000000080003E805)
#define UID_ACE_Locking_Range6_Set_WrLocked         (0x000000080003E806)
#define UID_ACE_Locking_Range7_Set_WrLocked         (0x000000080003E807)
#define UID_ACE_Locking_Range8_Set_WrLocked         (0x000000080003E808)
#define UID_ACE_Locking_GlblRng_Admins_Set          (0x000000080003F000)
#define UID_ACE_Locking_Admins_RangeStartToLocked   (0x000000080003F001)
#if TCG_FS_CONFIG_NS
  #define UID_ACE_Locking_Namespace_IdtoGlbRng      (0x000000080003F002)
#else
  #define UID_ACE_Locking_Namespace_IdtoGlbRng      (0)
#endif
#define UID_ACE_MBRControl_Admins_Set               (0x000000080003F800)
#define UID_ACE_MBRControl_Set_Done                 (0x000000080003F801)
#if TCG_FS_CONFIG_NS
  #define UID_ACE_Assign                            (0x000000080003F901)
  #define UID_ACE_Deassign                          (0x000000080003F902)
#endif
#define UID_ACE_DataStore_Get_All                   (0x000000080003FC00)
#define UID_ACE_DataStore_Set_All                   (0x000000080003FC01)
#define UID_ACE_DataStore2_Get_All                  (0x000000080003FC02)    //FS_DataStore
#define UID_ACE_DataStore2_Set_All                  (0x000000080003FC03)    //FS_DataStore
#define UID_ACE_DataStore3_Get_All                  (0x000000080003FC04)    //FS_DataStore
#define UID_ACE_DataStore3_Set_All                  (0x000000080003FC05)    //FS_DataStore
#define UID_ACE_DataStore4_Get_All                  (0x000000080003FC06)    //FS_DataStore
#define UID_ACE_DataStore4_Set_All                  (0x000000080003FC07)    //FS_DataStore
#define UID_ACE_DataStore5_Get_All                  (0x000000080003FC08)    //FS_DataStore
#define UID_ACE_DataStore5_Set_All                  (0x000000080003FC09)    //FS_DataStore
#define UID_ACE_DataStore6_Get_All                  (0x000000080003FC0A)    //FS_DataStore
#define UID_ACE_DataStore6_Set_All                  (0x000000080003FC0B)    //FS_DataStore
#define UID_ACE_DataStore7_Get_All                  (0x000000080003FC0C)    //FS_DataStore
#define UID_ACE_DataStore7_Set_All                  (0x000000080003FC0D)    //FS_DataStore
#define UID_ACE_DataStore8_Get_All                  (0x000000080003FC0E)    //FS_DataStore
#define UID_ACE_DataStore8_Set_All                  (0x000000080003FC0F)    //FS_DataStore
#define UID_ACE_DataStore9_Get_All                  (0x000000080003FC10)    //FS_DataStore
#define UID_ACE_DataStore9_Set_All                  (0x000000080003FC11)    //FS_DataStore

#define UID_ACE_Locking_GRange_Set_ReadToLOR        (0x0000000800040000)    //FS: SingleUserMode
#define UID_ACE_Locking_Range1_Set_ReadToLOR        (0x0000000800040001)    //FS: SingleUserMode
#define UID_ACE_Locking_Range2_Set_ReadToLOR        (0x0000000800040002)    //FS: SingleUserMode
#define UID_ACE_Locking_Range3_Set_ReadToLOR        (0x0000000800040003)    //FS: SingleUserMode
#define UID_ACE_Locking_Range4_Set_ReadToLOR        (0x0000000800040004)    //FS: SingleUserMode
#define UID_ACE_Locking_Range5_Set_ReadToLOR        (0x0000000800040005)    //FS: SingleUserMode
#define UID_ACE_Locking_Range6_Set_ReadToLOR        (0x0000000800040006)    //FS: SingleUserMode
#define UID_ACE_Locking_Range7_Set_ReadToLOR        (0x0000000800040007)    //FS: SingleUserMode
#define UID_ACE_Locking_Range8_Set_ReadToLOR        (0x0000000800040008)    //FS: SingleUserMode

#define UID_ACE_Locking_Range1_Set_Range            (0x0000000800041001)    //FS: SingleUserMode
#define UID_ACE_Locking_Range2_Set_Range            (0x0000000800041002)    //FS: SingleUserMode
#define UID_ACE_Locking_Range3_Set_Range            (0x0000000800041003)    //FS: SingleUserMode
#define UID_ACE_Locking_Range4_Set_Range            (0x0000000800041004)    //FS: SingleUserMode
#define UID_ACE_Locking_Range5_Set_Range            (0x0000000800041005)    //FS: SingleUserMode
#define UID_ACE_Locking_Range6_Set_Range            (0x0000000800041006)    //FS: SingleUserMode
#define UID_ACE_Locking_Range7_Set_Range            (0x0000000800041007)    //FS: SingleUserMode
#define UID_ACE_Locking_Range8_Set_Range            (0x0000000800041008)    //FS: SingleUserMode

#define UID_ACE_CPIN_Anybody_Get_NoPIN              (0x0000000800042000)    //FS: SingleUserMode
#define UID_ACE_SP_Reactivate_Admin                 (0x0000000800042001)    //FS: SingleUserMode

#define UID_ACE_Locking_GRange_Erase                (0x0000000800043000)    //FS: SingleUserMode
#define UID_ACE_Locking_Range1_Erase                (0x0000000800043001)    //FS: SingleUserMode
#define UID_ACE_Locking_Range2_Erase                (0x0000000800043002)    //FS: SingleUserMode
#define UID_ACE_Locking_Range3_Erase                (0x0000000800043003)    //FS: SingleUserMode
#define UID_ACE_Locking_Range4_Erase                (0x0000000800043004)    //FS: SingleUserMode
#define UID_ACE_Locking_Range5_Erase                (0x0000000800043005)    //FS: SingleUserMode
#define UID_ACE_Locking_Range6_Erase                (0x0000000800043006)    //FS: SingleUserMode
#define UID_ACE_Locking_Range7_Erase                (0x0000000800043007)    //FS: SingleUserMode
#define UID_ACE_Locking_Range8_Erase                (0x0000000800043008)    //FS: SingleUserMode


//Authority Table
//#define UID_Authority_Anybody (0x0000000900000001)
//#define UID_Authority_Admins  (0x0000000900000002)
#define UID_Authority_AdminX    (0x0000000900010000)
#define UID_Authority_Admin1    (0x0000000900010001)
#define UID_Authority_Admin2    (0x0000000900010002)
#define UID_Authority_Admin3    (0x0000000900010003)
#define UID_Authority_Admin4    (0x0000000900010004)
#define UID_Authority_Users     (0x0000000900030000)
#define UID_Authority_User1     (0x0000000900030001)
#define UID_Authority_User2     (0x0000000900030002)
#define UID_Authority_User3     (0x0000000900030003)
#define UID_Authority_User4     (0x0000000900030004)
#define UID_Authority_User5     (0x0000000900030005)
#define UID_Authority_User6     (0x0000000900030006)
#define UID_Authority_User7     (0x0000000900030007)
#define UID_Authority_User8     (0x0000000900030008)
#define UID_Authority_User9     (0x0000000900030009)
#define UID_Authority_AtaMst    (0x0000000900050001)
#define UID_Authority_AtaUsr    (0x0000000900050002)

//C_PIN Table
#define UID_CPIN_Admin1         (0x0000000B00010001)
#define UID_CPIN_Admin2         (0x0000000B00010002)
#define UID_CPIN_Admin3         (0x0000000B00010003)
#define UID_CPIN_Admin4         (0x0000000B00010004)
#define UID_CPIN_User1          (0x0000000B00030001)
#define UID_CPIN_User2          (0x0000000B00030002)
#define UID_CPIN_User3          (0x0000000B00030003)
#define UID_CPIN_User4          (0x0000000B00030004)
#define UID_CPIN_User5          (0x0000000B00030005)
#define UID_CPIN_User6          (0x0000000B00030006)
#define UID_CPIN_User7          (0x0000000B00030007)
#define UID_CPIN_User8          (0x0000000B00030008)
#define UID_CPIN_User9          (0x0000000B00030009)

//SecretProtect
#define UID_SecretProtect       (0x0000001D00000000)
#define UID_SecretProtect_128   (0x0000001D0000001D)    //Opal2
#define UID_SecretProtect_256   (0x0000001D0000001E)    //Opal2

//LockingInfo Table
#define UID_LockingInfo         (0x0000080100000001)

//Locking Table
#define UID_Locking             (0x0000080200000000)
#define UID_Locking_GRange      (0x0000080200000001)
#define UID_Locking_Range1      (0x0000080200030001)
#define UID_Locking_Range2      (0x0000080200030002)
#define UID_Locking_Range3      (0x0000080200030003)
#define UID_Locking_Range4      (0x0000080200030004)
#define UID_Locking_Range5      (0x0000080200030005)
#define UID_Locking_Range6      (0x0000080200030006)
#define UID_Locking_Range7      (0x0000080200030007)
#define UID_Locking_Range8      (0x0000080200030008)
#define UID_Locking_Range       (0x0000080200030000)

//MBRControl Table
#define UID_MBRControl          (0x0000080300000001)

#define UID_MBR                 (0x0000080400000000)

//K_AES_256 Table
#define UID_K_AES_256               (0x0000000100000806)    //(0x0000080600000000)
#define UID_K_AES_256_TBL           (0x0000080600000000)
#define UID_K_AES_256_GRange_Key    (0x0000080600000001)
#define UID_K_AES_256_Range1_Key    (0x0000080600030001)
#define UID_K_AES_256_Range2_Key    (0x0000080600030002)
#define UID_K_AES_256_Range3_Key    (0x0000080600030003)
#define UID_K_AES_256_Range4_Key    (0x0000080600030004)
#define UID_K_AES_256_Range5_Key    (0x0000080600030005)
#define UID_K_AES_256_Range6_Key    (0x0000080600030006)
#define UID_K_AES_256_Range7_Key    (0x0000080600030007)
#define UID_K_AES_256_Range8_Key    (0x0000080600030008)

#define UID_DataStoreType       (0x0000100000000000)
#define UID_DataStore           (0x0000100100000000)
#define UID_DataStore2          (0x0000100200000000)    //FS: DataStore
#define UID_DataStore3          (0x0000100300000000)    //FS: DataStore
#define UID_DataStore4          (0x0000100400000000)    //FS: DataStore
#define UID_DataStore5          (0x0000100500000000)    //FS: DataStore
#define UID_DataStore6          (0x0000100600000000)    //FS: DataStore
#define UID_DataStore7          (0x0000100700000000)    //FS: DataStore
#define UID_DataStore8          (0x0000100800000000)    //FS: DataStore
#define UID_DataStore9          (0x0000100900000000)    //FS: DataStore
//#define UID_DSx                 (0x00FF000000000000)
//#define UID_DS2                 (0x00F1000000000000)
//#define UID_DS3                 (0x00F2000000000000)
//#define UID_DS4                 (0x00F3000000000000)
//#define UID_DS5                 (0x00F4000000000000)
//#define UID_DS6                 (0x00F5000000000000)
//#define UID_DS7                 (0x00F6000000000000)
//#define UID_DS8                 (0x00F7000000000000)
//#define UID_DS9                 (0x00F8000000000000)

//#define UID_SUR0                (0xF000000000000000)    //FS: SingleUserMode
//#define UID_SUR1                (0xF100000000000000)    //FS: SingleUserMode
//#define UID_SUR2                (0xF200000000000000)    //FS: SingleUserMode
//#define UID_SUR3                (0xF100000000000000)    //FS: SingleUserMode
//#define UID_SUR4                (0xF100000000000000)    //FS: SingleUserMode
//#define UID_SUR5                (0xF100000000000000)    //FS: SingleUserMode
//#define UID_SUR6                (0xF100000000000000)    //FS: SingleUserMode
//#define UID_SUR7                (0xF100000000000000)    //FS: SingleUserMode
//#define UID_SUR8                (0xF100000000000000)    //FS: SingleUserMode
#define UID_FF                  (0xFFFFFFFF00000000)

//Token Definition
#define TOK_StartList           0xF0
#define TOK_EndList             0xF1
#define TOK_StartName           0xF2
#define TOK_EndName             0xF3
#define TOK_Call                0xF8
#define TOK_EndOfData           0xF9
#define TOK_EndOfSession        0xFA
#define TOK_StartTransaction    0xFB
#define TOK_EndTransaction      0xFC
#define TOK_Empty               0xFF

//Method Status Code Definition
#define STS_SUCCESS                     0x00
#define STS_NOT_AUTHORIZED              0x01
//#define STS_OBSOLETE                       0x02
#define STS_SP_BUSY                     0x03
#define STS_SP_FAILED                   0x04
#define STS_SP_DISABLED                 0x05
#define STS_SP_FROZEN                   0x06
#define STS_NO_SESSIONS_AVAILABLE       0x07
#define STS_UNIQUENESS_CONFLICT         0x08
#define STS_INSUFFICIENT_SPACE          0x09
#define STS_INSUFFICIENT_ROWS           0x0A
#define STS_INVALID_METHOD              0x0B
#define STS_INVALID_PARAMETER           0x0C
//0x0D
//0x0E
#define STS_TPER_MALFUNCTION            0x0F
#define STS_TRANSACTION_FAILURE         0x10
#define STS_RESPONSE_OVERFLOW           0x11
#define STS_AUTHORITY_LOCKED_OUT        0x12
#define STS_FAIL                        0x3F

#define STS_SUCCESS_THEN_ABORT          0xE0FF    //internal use only, status is success but need to close session (RevertSP)
#define STS_STAY_IN_IF_SEND             0xF0FF    //internal use only, it should keep gTcgCmdState at "ST_AWAIT_IF_SEND"
#define STS_SESSION_ABORT               0xFFFF    //internal use only
#define STS_1667_NO_DATA_RETURN         0xF8FF

//#ifdef TCG_EDRIVE
#define TCG_1667_ProbeCmd               0x0001  //Index (HiByte) + Cmd (LoByte)
#define TCG_1667_GetSiloCap             0x0101
#define TCG_1667_Transfer               0x0102
#define TCG_1667_Reset                  0x0103
#define TCG_1667_GetResult              0x0104
#define TCG_1667_TPerReset              0x0105

#define STS_1667_SUCCESS                0x00
#define STS_1667_FAILURE                0x80
#define STS_1667_INV_PARAMETER_Combi    0xF7
#define STS_1667_INV_PARAMETER_LENGTH   0xF8
#define STS_1667_INCONSISTENT_PCLENGTH  0xF9
#define STS_1667_INV_SILO               0xFA
#define STS_1667_INCOMPLETE_COMMAND     0xFB
#define STS_1667_INV_PARAMETER          0xFC
#define STS_1667_SP_SEQUENCE_REJECTION  0xFD
#define STS_1667_NO_PROBE               0xFE
#define STS_1667_RESERVED_FUNCTION      0xFF

#define STS_1667_DEFAULT_BEHAVIOR       0x01    //Probe Silo
#define STS_1667_UNSUPPORTED_HOST_VER   0x81

#define STS_1667_INV_TX_LEN_ON_POUT     0x81    //TCG Silo
#define STS_1667_INV_TCG_COMID          0x82
#define STS_1667_TCG_SYNC_VIOLATION     0x83

//error code for TCG SIIS
/*
#define SIIS_TPER_SUCCESS                    STS_1667_SUCCESS
#define SIIS_TPER_INVALID_SEC_PID_PARAM      STS_1667_INV_TCG_COMID
#define STS_TPER_INVALID_TX_PARAM_SEND      STS_1667_INV_TX_LEN_ON_POUT
#define SIIS_TPER_OTHER_INVALID_CMD_PARAM    STS_1667_INV_TCG_COMID
#define STS_TPER_SYNC_PROTOCOL_VIOLATION    STS_1667_TCG_SYNC_VIOLATION
#define STS_TPER_DATA_PROTECTION_ERROR      //
#define STS_TPER_INVALID_SEC_STATE          //
#define STS_TPER_ACCESS_DENIED              //
 */
typedef enum
{
    TPER_GOOD = 0,
    TPER_INVALID_SEC_PID_PARAM,
    TPER_INVALID_TX_PARAM_SEND,
    TPER_OTHER_INVALID_CMD_PARAM,
    TPER_SYNC_PROTOCOL_VIOLATION,
    TPER_DATA_PROTECTION_ERROR,
    TPER_INVALID_SEC_STATE,
    TPER_OPERATION_DENIED,
    TPER_CONTINUE = 0x676f6f6e  // "goon"
} tper_status_t;

//-----------------------------------------------------------------------------
//  Imported data prototype without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define LoByte(i) *(((u8*)&i)+0)
#define HiByte(i) *(((u8*)&i)+1)
#define LoWord(i) *(((u16*)&i)+0)
#define HiWord(i) *(((u16*)&i)+1)
#define LoLong(i) *(((u32*)&i)+0)
#define HiLong(i) *(((u32*)&i)+1)

#define W_B1(a) *(((u8*)&a)+1)
#define W_B0(a) *(((u8*)&a)+0)

#define DW_W1(a) *(((u16*)&a)+1)
#define DW_W0(a) *(((u16*)&a)+0)

#define DW_B3(a) *(((u8*)&a)+3)
#define DW_B2(a) *(((u8*)&a)+2)
#define DW_B1(a) *(((u8*)&a)+1)
#define DW_B0(a) *(((u8*)&a)+0)

#define LL_DW1(a) *(((u32*)&a)+2)
#define LL_DW0(a) *(((u32*)&a)+1)

#define LL_B7(a) *(((u8*)&a)+7)
#define LL_B6(a) *(((u8*)&a)+6)
#define LL_B5(a) *(((u8*)&a)+5)
#define LL_B4(a) *(((u8*)&a)+4)
#define LL_B3(a) *(((u8*)&a)+3)
#define LL_B2(a) *(((u8*)&a)+2)
#define LL_B1(a) *(((u8*)&a)+1)
#define LL_B0(a) *(((u8*)&a)+0)


#define SHA256_DIGEST_SIZE ( 256 / 8)
#define mBIT(n)   (1 << (n))

#define tcgDevTyp           (smSysInfo->d.TCGData.d.TCGUsed.ee2.TCG_DEV_TYPE)

#define tcgG4Dft            (smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgG4NorCellDft)
#define tcgG5Dft            (smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgG5NorCellDft)
#define tcgG4EraCnt         (smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgG4NorBlkErasedCnt)
#define tcgG5EraCnt         (smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgG5NorBlkErasedCnt)
#define tcgDefectID         (smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgDefect_ID)
#define tcgErasedCntID      (smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgErasedCnt_ID)

#define tcg_mTbl_idStr      (smSysInfo->d.TCGData.d.TCGUsed.ee2.Tcg_mID_Text)
#define tcg_mTbl_id         (smSysInfo->d.TCGData.d.TCGUsed.ee2.mID)
#define tcg_mTbl_verStr     (smSysInfo->d.TCGData.d.TCGUsed.ee2.Tcg_mVer_Text)
#define tcg_mTbl_ver        (smSysInfo->d.TCGData.d.TCGUsed.ee2.mVer)
#define tcg_cTbl_idStr      (smSysInfo->d.TCGData.d.TCGUsed.ee2.Tcg_cID_Text)
#define tcg_cTbl_id         (smSysInfo->d.TCGData.d.TCGUsed.ee2.cID)
#define tcg_cTbl_verStr     (smSysInfo->d.TCGData.d.TCGUsed.ee2.Tcg_cVer_Text)
#define tcg_cTbl_ver        (smSysInfo->d.TCGData.d.TCGUsed.ee2.cVer)
#define tcg_prefmt_tag      (smSysInfo->d.TCGData.d.TCGUsed.ee2.Preformat_ID)
#define tcg_nontcg_switcher (smSysInfo->d.TCGData.d.TCGUsed.ee2.TCG_NonTCG_Switcher)

//#define tcg_ee_PsidTag      (smSysInfo->d.TCGData.d.TCGUsed.psidTag)
#define tcg_ee_Psid           (smSysInfo->d.TCGData.d.TCGUsed.ee2.cPinPSID)

#define tcg_G5_saved_page_idx (smSysInfo->d.TCGData.d.TCGUsed.ee2.Por_Saved.G5_Por_PageIdx)
#define tcg_G5_saved_cell_idx (smSysInfo->d.TCGData.d.TCGUsed.ee2.Por_Saved.G5_Por_CellIdx)
#define tcg_G5_saved_idx_tag  (smSysInfo->d.TCGData.d.TCGUsed.ee2.Por_Saved.G5_Por_Saved_Tag)
#define tcg_G4_saved_page_idx (smSysInfo->d.TCGData.d.TCGUsed.ee2.Por_Saved.G4_Por_PageIdx)
#define tcg_G4_saved_cell_idx (smSysInfo->d.TCGData.d.TCGUsed.ee2.Por_Saved.G4_Por_CellIdx)
#define tcg_G4_saved_idx_tag  (smSysInfo->d.TCGData.d.TCGUsed.ee2.Por_Saved.G4_Por_Saved_Tag)
#define tcg_phy_valid_blk_tbl_tag (smSysInfo->d.TCGData.d.TCGUsed.ee2.TCG_PHY_VALID_BLK_TBL_TAG)
#define tcg_phy_valid_blk_tbl (smSysInfo->d.TCGData.d.TCGUsed.ee2.TCG_PHY_VALID_BLK_TBL)
#define TCGBlockNo_ee2        (smSysInfo->d.TCGData.d.TCGUsed.ee2.TCGBlockNo_EE)

#define tcgL2pAssis           (smSysInfo->d.TCGData.d.TCGL2pAssis)


#define mTcgKekDataNor        (smSysInfo->d.TCGData.d.TcgKekDataNor)

#define ERASE_ALL_TAG       "erase all"
#define PREFORMAT_START_TAG "preformat start"
#define PREFORMAT_END_TAG   "preformat end  "
#define DEFECT_STRING       "DftTbl_04"
#define ERASED_CNT_STRING   "EraCntTbl_04"

#define PSID_TAG            0x50534944  //PSID
#define TCG_TAG             0x54434740  //"TCG@"
#define NONTCG_TAG          0x58544347  //"XTCG"
#define L2P_ASS_TAG         0x4C325041  // "L2PA"

#define SupportDataRemovalMechanism      (BIT1)

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef union
{
    u16    all;
    struct
    {
        u16 pageNo     : CFG_PAGE_ADDR_BIT_CNT;
        u16 cellNo     : 16-CFG_PAGE_ADDR_BIT_CNT;
    }pc;
    struct
    {
        u16 pageNo     : CFG_PAGE_ADDR_BIT_CNT;

        #if (CFG_CH_BIT_CNT)
        u16 chNo       : CFG_CH_BIT_CNT;
        #endif

        #if (CFG_CE_BIT_CNT)
        u16 ceNo       : CFG_CE_BIT_CNT;
        #endif

        u16 relblkNo   : 16 - (CFG_PAGE_ADDR_BIT_CNT + CFG_CH_BIT_CNT + CFG_CE_BIT_CNT);
    }pccb;
} TcgRowForm;

typedef struct
{
    u8      dev;
    u8      ch;
    u8      il;
    u16     page; //LeePeng20170209
    u8      row;
} tTcgAdrForm;

typedef enum{
    tIDLE,
    tREAD,
    tWRITE,
    tERASE
}sTcgNvOpType;

typedef enum{
    zGRP1,
    zGRP2,
    zGRP3,
    zGRP4,
    zDATASTORE,
    zMBR,
    zGRP5,
    zMBRTMP,
    zDATASTORETMP
}sTcgGroupZone;

typedef struct
{
    sTcgNvOpType    NvOpType;
    sTcgGroupZone   NvOpGrp;
    u32             NvOpLaaStartIdx;
    u16             NvOpPageCnt;
    u8*             NvOpBufAddr;
    u32             NvOpSeqRdIndex;
    u32             NvOpBaseBlkNo;
    u8              NvOpMultiPageRead;
    u32             NvOpSeqWrIndex;
} sTcg2RWtaskInfo;

//Session Manager param
typedef enum
{
    SESSION_CLOSE,
    SESSION_START,
    SESSION_NG,
    SESSION_NAN = 0x7FFFFFFF
}session_state_t;

typedef enum
{
    TRNSCTN_ACTIVE,
    TRNSCTN_IDLE,
    TRNSCTN_NAN = 0x7FFFFFFF
}trnsctn_state_t;

typedef enum
{
    BROP_FORMAT_BACKUP,     // Bridge Operater before tcg format and initial
    BROP_FORMAT_RESTORE,    // Bridge Operater after tcg format and initial
    BROP_FWUPDATE_BACKUP,   // Bridge Operater before FW Update
    BROP_FWUPDATE_RESTORE,  // Bridge Operater after FW Update
    BROP_FOB_FORMAT,        // Bridge Operater for FOB Format
    BROP_RAWKEY_CLEAR,      // Bridge Operator for Raw Key Clear
    BROP_NAN = 0x7FFFFFFF
} BridgeRoutineOP_t;



#pragma pack(1)
typedef union
{
    struct
    {
        u16 page;       // CA5 -> 1 block = 768 pages, so page bits is more than 8 bits.
        u16 blk;
    };
    u32  all;
} tcgLaa_t;


typedef union
{
        u32    all;
        struct
        {
            u16 page;
            u16 cell;
        }pc;
} tTcgLogAddr;
#pragma pack()

#pragma pack(4)
typedef union
{
    u8 all[CFG_UDATA_PER_PAGE * L2P_PAGE_CNT];          // ep2 = 48K, ep3 = 80K
    struct
    {
        u16               TcgG4Header;
        u16               Tcg4Gap[0x400/sizeof(u16)-1];           // 1K
        tcgLaa_t TcgMbrL2P[G4G5_LAA_AMOUNT_LIMIT];   // ep2 = 37K, ep3 = 74K bytes
    } b;
} tG4;
#pragma pack()

#pragma pack(4)
typedef union
{
    u8 all[CFG_UDATA_PER_PAGE * L2P_PAGE_CNT];              // ep2 = 48K, ep3 = 80K
    struct
    {
        u16               TcgG5Header;
        u16               TcgG5Gap[0x400/sizeof(u16)-1];          // 1K
        tcgLaa_t TcgTempMbrL2P[G4G5_LAA_AMOUNT_LIMIT];   // ep2 = 37K, ep3 = 74K bytes
    } b;
} tG5;
#pragma pack()

typedef struct {
    u64 rangeStart;
    u64 rangeEnd;
    u32 rangeNo;
    u32 blkcnt;
    u32 readLocked;
    u32 writeLocked;
} enabledLockingTable_t;
COMPILE_ASSERT(sizeof(enabledLockingTable_t) == 32, "S32 bytes!");

//TCG Status Definition
#define TCG_ACTIVATED   0x01
#define MBR_SHADOW_MODE 0x02
#define TCG_TBL_ERR     0x08

typedef enum
{
    TCG_SYNC_NoFTLSuBlk       = 0,
    TCG_CPU2_BUFADDR_SHARE_OK = 0x41444452,     //"ADDR"
    TCG_SYNC_OK               = 0x53594E43,     //"SYNC"
} TCG_SYNC_TAG_t;

typedef union
{
    u32 all32;
    struct
    {
        u32 nf_sync_req       :1;
        u32 if_sync_resp      :1;
        u32 if_sync_req       :1;
        u32 nf_sync_resp      :1;
        u32 nf_ps4_init_req   :1;
        u32 if_ps4_init_resp  :1;
    } __attribute__((packed)) b;
} __attribute__((packed)) tcg_sync_t;


#define ZONE51_CBCTBL_TAG       "SSSTC#SUN"
#define ZONE51_EBCKEY_TAG       "SSSTC@MON"
#define ZONE51_CBCFWIMAGE_TAG   "SSSTC$TUE"
typedef struct{
    struct{
        u32 tag[3];
        u32 writedCnt;
        u32 kek[8]; // table key must be linked with the root key
        u32 iv[4];
    } cbcTbl;
    struct{
        u32 tag[3];
        u32 writedCnt;
        u32 key[8];
    } ebcKey;
    struct{
        u32 tag[3];
        u32 writedCnt;
        u32 key[8];
        u32 iv[4];
    } cbcFwImage;
    u32     histCnt;
    u32     crc;
} SecretZone_t;

typedef union
{
    SecretZone_t sz;
    //
    struct{
        u32 all32[(CFG_UDATA_PER_PAGE * 4 /*PAGE_NUM_PER_STAGE*/) / sizeof(u32) - 1];  // PAGE_NUM_PER_STAGE = 4
        u32 crc;
    } dw;
} zone51_t;

typedef struct
{
    u32 otp_secure_enabled;
    u32 fw_secure_enable;
    u32 loader_policy;
    u32 maincode_policy;
} sh_secure_boot_info_t;

// Block SID Auth status
//#define SID_VALUE_CHANGED   0x10
#define SID_BLOCKED         0x20
#define SID_HW_RESET        0x40

#define TCG_DOMAIN_NORMAL    0
#define TCG_DOMAIN_SHADOW    1
#define TCG_DOMAIN_DUMMY     2
#define TCG_DOMAIN_ERROR     3

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
extern u32 mTcgStatus;        //TCG status variable for others
extern u16 mReadLockedStatus;   // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
extern u16 mWriteLockedStatus;  // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.

extern enabledLockingTable_t mLockingRangeTable[LOCKING_RANGE_CNT+1];

extern sRawKey mRawKey[LOCKING_RANGE_CNT + 1];

u16  TcgRangeCheck(u32 lba, u32 sc, bool writemode);
int  TcgInit_Cpu0(u32 initMode, bool bClearCache);
void TcgBackupVar2EEP(void);
void TcgRestoreVarFromEEP(void);
int  TcgPsidVerify(void);
void TcgPsidBackup(void);
void TcgPsidRestore(void);
int  TcgPsidVerify_Cpu4(void);
void TcgPsidBackup_Cpu2(void);
void TcgPsidRestore_Cpu2(void);
void LockingRangeTable_Update(void);

//void TcgHardReset(void);
u32 Bcm_Test(u32 argc, u32* argv);
void DumpTcgKeyInfo(void);
void DumpRangeInfo(void);

int  TcgAtaKEKState(u32 auth);
int  TcgAtaWrapKEK(u8* chanllege, u8 len, u32 auth, u32 *pKEK);
int  TcgAtaUnwrapKEK(u8* chanllege, u8 len, u32 auth, u32 *pKEK);
void TcgAtaEraseKEK(u32 auth);
void TcgAtaPlainKEK(u32 auth, u32 *pKEK, bool bToKeyTbl);
void TcgAtaNoKeyWrap(void);
void TcgAtaG3Synchronize(void);
void TcgAtaChangeKey(void);
void TcgAtaSetKey(void);
void TcgNvmFormat(req_t*);

#endif // Jack Li
#endif // #ifndef tcgcommon_h
