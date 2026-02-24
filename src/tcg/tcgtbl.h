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
#ifndef tcgtbl_h
#define tcgtbl_h


#if _TCG_ // Jack Li

//-----------------------------------------------------------------------------//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------//-----------------------------------------------------------------------------

//#else

//-----------------------------------------------------------------------------//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------//-----------------------------------------------------------------------------


//========================================================
// TCG Table Structure Definition and Declaration
//========================================================
#if _TCG_ != TCG_PYRITE         // Opal or eDrv, must include PSID
  #define ADM_ACE_TBLOBJ_CNT            (11)
  #define ADM_AUTHORITY_TBLOBJ_CNT      (5 + 4)     // admin1~4
  #define ADM_CPIN_TBLOBJ_CNT           (3 + 4)
  #define SSC_STRING                    "Opal\0"    // For TPerInfoTbl.SSC
#else
  #if TCG_FS_PSID
  #define ADM_ACE_TBLOBJ_CNT            (10 + 2)
  #define ADM_AUTHORITY_TBLOBJ_CNT      (4 + 2)     // admin1~2
  #define ADM_CPIN_TBLOBJ_CNT           (3 + 2)     // admin1~2
  #else
  #define ADM_ACE_TBLOBJ_CNT            (10)
  #define ADM_AUTHORITY_TBLOBJ_CNT      (3 + 2)     // admin1
  #define ADM_CPIN_TBLOBJ_CNT           (2 + 2)     // admin1
  #endif

  #define SSC_STRING                    "Pyrite\0"
#endif

//typedef enum  {READ_ONLY, WRITABLE, NO_DEF} eTblRW;      //Cell Property

//Column Type:
//      VALUE_TYPE: uinteger (get: value)
//      FBYTE_TYPE: byte sequence with fixed length (column size)  (get: Ax b1 ...)
//      VBYTE_TYPE: byte sequence with variable length, first byte is the length (Ax b1...)
//      LIST_TYPE: first byte is the length... (get: F0 b1 ... F1)
//      STRING_TYPE: string in char array (get: Ax ...)
//      STRINGLIST_TYPE: string list in char array, ending with double '/0' (get: F0 Ax ... F1)
typedef enum  {
    VALUE_TYPE = 0,
    FBYTE_TYPE,
    VBYTE_TYPE,
    UID_TYPE,
    LIST_TYPE,
    STRING_TYPE,
    STRINGLIST_TYPE,
    UIDLIST_TYPE,
    UID2_TYPE
} eColType;

#pragma pack(1)
//Column Property
typedef struct
{
    u32   colNo;    //column number
    u8    size;     //column byte count
    u8    colType;  //colType
    u8    dummy[2];
} sColPrty;
#pragma pack()

/* AdminSP Table Declaration */
#pragma pack(1)
typedef struct
{
    u32 ID;
    u32 ver;
} sTcgTblInfo;
#pragma pack()

#pragma pack(1)
typedef struct
{
    u32 tblSize;
    u32 colCnt;
    u32 maxCol;
    u16 rowCnt;
    u16 objSize;
} sTcgTblHdr;
#pragma pack()

//--------------------------------------------------------
// SPInfo Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64 uid;       //col#0, UID
    u64 spid;
    u8  name[8];
    //u8 size;
    //u8 sizeInUse;
    u32 spSessionTimeout;
    u32 enabled;   //bool
} sSPInfo_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr      hdr;
    sColPrty        pty[5];     //column property (# and bytes)
    sSPInfo_TblObj  val[1];     //row obj
} sSPInfo_Tbl;
#pragma pack()

//--------------------------------------------------------
// SPTemplates Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64 uid;           //col#0, UID
    u64 templateId;
    u8  name[8];
    u8  version[4];
} sSPTemplates_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr  hdr;
    sColPrty    pty[4];             //column property (# and bytes)
    sSPTemplates_TblObj val[2];     //row obj
} sSPTemplates_Tbl;
#pragma pack()

//--------------------------------------------------------
// Table Table prototype:
//--------------------------------------------------------
#define TBL_K_OBJECT 1
#define TBL_K_BYTE   2

#pragma pack(1)
typedef struct
{
    u64    uid;            //col#0, UID
    u8     name[24];
    u16    kind;           //col#4, Kind
    u8     mGranularity;   //col#13, manGranularity    //maxSize;
    u8     rGranularity;   //col#14
} sTbl_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr  hdr;
    sColPrty    pty[5];   //column property (# and bytes)
#if _TCG_==TCG_PYRITE
    sTbl_TblObj val[12];  //row obj
#else
    sTbl_TblObj val[11];  //row obj
#endif
} sTbl_Tbl;
#pragma pack()

//--------------------------------------------------------
// MethodID Table prototype, Read-Only
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64 uid;       //col#1, InvokingID
    u8 name[16];   //col#2, MethodID //cj: 12->16
} sMethod_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr     hdr;
    sColPrty       pty[2];  //column property (# and bytes)
    sMethod_TblObj val[8];  //row obj
} sMethod_Tbl;
#pragma pack()

//--------------------------------------------------------
// AccessControl Table prototype, Read-Only
//--------------------------------------------------------
#define ACCESSCTRL_ACL_CNT    4
#pragma pack(1)
typedef struct
{
    u64 invID;                          //col#1, InvokingID
    u64 mtdID;                          //col#2, MethodID
    u64 acl[ACCESSCTRL_ACL_CNT];        //col#4, it should be a ACL "UIDList_Type"
    u64 getAclAcl;                      //col#8, GetACLACL
}sAxsCtrl_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sAxsCtrl_TblObj thisSP[2];
#if _TCG_ == TCG_PYRITE
    sAxsCtrl_TblObj table[13];
#else
    sAxsCtrl_TblObj table[12];
#endif
    sAxsCtrl_TblObj spInfo[1];
    sAxsCtrl_TblObj spTemplate[3];
    sAxsCtrl_TblObj method[9];
#if _TCG_ == TCG_PYRITE
    sAxsCtrl_TblObj ace[13];
    sAxsCtrl_TblObj authority[10];   // pyrite v2.01 //6];
    sAxsCtrl_TblObj cpin[10];
#else
    sAxsCtrl_TblObj ace[12];
    sAxsCtrl_TblObj authority[16];
    sAxsCtrl_TblObj cpin[14];
#endif
    sAxsCtrl_TblObj tperInfo[2];
    sAxsCtrl_TblObj templateTbl[4];
    sAxsCtrl_TblObj sp[7];
#if _TCG_ == TCG_PYRITE
    sAxsCtrl_TblObj removalMsm[2];
#endif

    sColPrty    pty[4];             //column property (# and bytes)
    u8 colCnt;                      //=4;
    u8 rowCnt;                      //=4;
    u8 objSize;                     //table object size
    u8 maxCol;                      //maximum column number in TCG spec
}sAdmAxsCtrl_Tbl;
#pragma pack()

#define ADM_ACCESSCTRL_TBLOBJ_CNT       ((sizeof(sAdmAxsCtrl_Tbl)- sizeof(sColPrty)*4 - 8) / sizeof(sAxsCtrl_TblObj))

//--------------------------------------------------------
// ACE Table prototype:
//--------------------------------------------------------
#define ADM_ACE_BOOLEXPR_CNT    2
#define ACE_COLUMNS_CNT         12
#pragma pack(1)
typedef struct
{
    u64 uid;
    u64 booleanExpr[ADM_ACE_BOOLEXPR_CNT];  //5.1.3.3 ACE_expression,  uidref to an Authority object + boolean_ACE (AND = 0 and OR = 1)
                                            //for Opal drive, boolean_ACE should be "OR" only, so we only need to store lower 4B uid of an Authority object
                                            //UID List, Admins OR SID
    u8 col[ACE_COLUMNS_CNT];                //VBYTE_TYPE, ?? how to deal with?  bit# stands for col#?
} sACE_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct {
    sTcgTblHdr hdr;
    sColPrty   pty[3];        //column cnt
    sACE_TblObj val[ADM_ACE_TBLOBJ_CNT]; //row cnt
} sACE_Tbl;
#pragma pack()

//--------------------------------------------------------
// Authority Table prototype:
//--------------------------------------------------------
typedef enum  {AUTH_None=0, AUTH_Password, AUTH_Exchange, AUTH_Sign, AUTH_SymK, AUTh_NAN=0x7fffffff} auth_method;
typedef enum  {SECURE_None=0, HMAC_SHA_256, SECURE_NAN=0x7fffffff} secure_message ;
typedef enum  {HASH_None=0, SHA_1, SHA_256, SHA_384, SHA_512, HASH_NAN=0x7fffffff} hash_protocol;
typedef enum  {GRP_1=1, GRP_2=2, GRP_3=3, GRP_ALL=0xff} grp_define;

#pragma pack(1)
typedef struct {
    u64     uid;
    u8      name[8];
    u8      commonName[32];     //Opal2
    u32     isClass;     //bool
    u64     Class;
    u32     enabled;   //bool
    secure_message  secure;
    hash_protocol   HashAndSign;
    u32     presentCertificate;    //bool
    auth_method operation;
    u64     credential;
    u64     responseSign;
    u64     responseExch;
} sAuthority_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr        hdr;
    sColPrty          pty[13];       //column cnt
    sAuthority_TblObj val[ADM_AUTHORITY_TBLOBJ_CNT];     //row cnt, include PSID
} sAuthority_Tbl;
#pragma pack()

//--------------------------------------------------------
// CPIN and Salt Table prototype:
//--------------------------------------------------------
#define CPIN_NULL           0x00000000
#define CPIN_IN_RAW         0x11111111
#define CPIN_IN_DIGEST      0x22222222 // PSID is stored in Digest fmt 
#define CPIN_IN_PBKDF       0x4350494E // CPIN, pwd is stored in PBKDF2 fmt
#define CPIN_SALT_LEN       32
#define CPIN_LENGTH         32

#pragma pack(1)
typedef struct
{
    u32 cPin_Tag;
    u8  cPin_val[CPIN_LENGTH];      // 32
    u8  cPin_salt[CPIN_SALT_LEN];   // 32
}sCPin;
#pragma pack()

#pragma pack(1)
typedef struct
{
    u64     uid;
    u8      name[16];   //cj: 12->16
    sCPin   cPin;       // 32 + 32 + 4
    u64     charSet;
    u32     tryLimit;
    u32     tries;
    u32     persistence; //bool
}sCPin_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr hdr;
    sColPrty pty[7];        //column cnt
    sCPin_TblObj val[ADM_CPIN_TBLOBJ_CNT];  //include PSID
} sCPin_Tbl;
#pragma pack()

//--------------------------------------------------------
// TPerInfo Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64 uid;
    u32 firmwareVersion;
    u32 protocolVersion;
    u8  ssc[8];
    u32 preset;
} sTPerInfo_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr       hdr;
    sColPrty         pty[5];    //column cnt
    sTPerInfo_TblObj val[1];
} sTPerInfo_Tbl;
#pragma pack()

//--------------------------------------------------------
// Template Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64  uid;
    u8   name[12];
    u32  revision;
    u32  instances;
    u32  maxInstances;
} sTemplate_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr       hdr;
    sColPrty         pty[5];    //column cnt
    sTemplate_TblObj val[3];
} sTemplate_Tbl;
#pragma pack()

//--------------------------------------------------------
// SP Table prototype:
//--------------------------------------------------------
typedef enum
{
    issued=0,
    manufactured_inactive=8,
    manufactured=9,
    //manufactured_disabled=10
    LIFE_CYCLE_NAN = 0x7FFFFFFF
} life_cycle_state;

#pragma pack(1)
typedef struct
{
    u64              uid;
    u8               name[8];
    life_cycle_state lifeCycle;
    u32              frozen;    //bool
} sSP_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr hdr;
    sColPrty   pty[4];    //column cnt
    sSP_TblObj val[2];
} sSP_Tbl;
#pragma pack()

//--------------------------------------------------------
// RemovalMechanism Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct {
    u64 uid;
    u32 activeRM;    //bool
} sRemovalMsm_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct {
    sTcgTblHdr         hdr;
    sColPrty           pty[2];    //column cnt
    sRemovalMsm_TblObj val[1];
} sRemovalMsm_Tbl;
#pragma pack()

/* LockingSP Table Declaration */
#if _TCG_ != TCG_PYRITE
  #define LCK_TABLE_TBLOBJ_CNT          (15+DSTBL_MAX_NUM-1)
  #if TCG_FS_CONFIG_NS
    #define LCK_METHOD_TBLOBJ_CNT       (10 + 2)
    #define LCK_ACE_TBLOBJ_CNT          (69+((DSTBL_MAX_NUM-1)*2)+30 + 3)   //115
  #else
    #define LCK_METHOD_TBLOBJ_CNT       10
    #define LCK_ACE_TBLOBJ_CNT          (69+((DSTBL_MAX_NUM-1)*2)+30)   //115
  #endif
#else
  #define LCK_TABLE_TBLOBJ_CNT          13
  #define LCK_METHOD_TBLOBJ_CNT         (10 - 3)
  #define LCK_ACE_TBLOBJ_CNT            18
#endif
#define LCK_AUTHORITY_TBLOBJ_CNT        (TCG_AdminCnt+TCG_UserCnt+3)  // 16 or 6
#define LCK_CPIN_TBLOBJ_CNT             (TCG_AdminCnt+TCG_UserCnt)    // 13 or 3

//--------------------------------------------------------
// SPInfo Table prototype:
//--------------------------------------------------------
//const sSPInfo_Tbl cLckSPInfo_Tbl;

//--------------------------------------------------------
// SPTemplates Table prototype:
//--------------------------------------------------------
//const sSPTemplates_Tbl cLckSPTemplates_Tbl;

//--------------------------------------------------------
// Table Table prototype, Read-Only
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64    uid;            //col#0, UID
    u8     name[16];
    u32    kind;           //col#4, Kind
    u32    rows;           //col#7, Rows
    u16    mGranularity;   //col#13, manGranularity    //maxSize;
    u16    rGranularity;   //col#14
} sLckTbl_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr     hdr;
    sColPrty       pty[6];     //column property (# and bytes)
    sLckTbl_TblObj val[LCK_TABLE_TBLOBJ_CNT];     //row obj
} sLckTbl_Tbl;
#pragma pack()

//--------------------------------------------------------
// MethodID Table prototype, Read-Only
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    sTcgTblHdr     hdr;
    sColPrty       pty[2];         //column property (# and bytes)
    sMethod_TblObj val[LCK_METHOD_TBLOBJ_CNT];          //row obj
} sLckMethod_Tbl;
#pragma pack()

//--------------------------------------------------------
// AccessControl Table prototype, Read-Only
//--------------------------------------------------------
//#define LCK_ACCESSCTRL_ACL_CNT    4
//#pragma pack(1)
//typedef struct
//{
//    u64 invID;                 //col#1, InvokingID
//    u64 mtdID;                 //col#2, MethodID
//    u64 acl[ACCESSCTRL_ACL_CNT];      //col#4, it should be a ACL "UIDList_Type"
//    u64 getAclAcl;             //col#8, GetACLACL
//}sLckAxsCtrl_TblObj;
//#pragma pack()

#define LCK_ACCESSCTRL_ACL_CNT    4
#pragma pack(1)
typedef struct
{
    //sLckAccessCtrl_TblObj val[LCK_ACCESSCTRL_TBLOBJ_CNT]; //row obj
    sAxsCtrl_TblObj thisSP[4];
    sAxsCtrl_TblObj table[24];
    sAxsCtrl_TblObj spInfo[1];
    sAxsCtrl_TblObj spTemplate[3];
    sAxsCtrl_TblObj method[LCK_METHOD_TBLOBJ_CNT + 1];

#if _TCG_!=TCG_PYRITE
  #if TCG_FS_CONFIG_NS
    sAxsCtrl_TblObj ace[201 + 6];
  #else
    sAxsCtrl_TblObj ace[201];
  #endif
    sAxsCtrl_TblObj authority[30];
    sAxsCtrl_TblObj cpin[LCK_CPIN_TBLOBJ_CNT*2 + 1];    // Get/Set + Next
    sAxsCtrl_TblObj secretPrtct[2];
    sAxsCtrl_TblObj lckingInfo[1];
  #if TCG_FS_CONFIG_NS
    sAxsCtrl_TblObj lcking[30];
  #else
    sAxsCtrl_TblObj lcking[28];
   #endif
#else
    sAxsCtrl_TblObj ace[29];
    sAxsCtrl_TblObj authority[12];
    sAxsCtrl_TblObj cpin[LCK_CPIN_TBLOBJ_CNT*2 + 1];    // Get/Set + Next
    sAxsCtrl_TblObj lckingInfo[1];
    sAxsCtrl_TblObj lcking[3];
#endif

    sAxsCtrl_TblObj mbrCtrl[2];
    sAxsCtrl_TblObj mbr[2];
#if _TCG_!=TCG_PYRITE
    sAxsCtrl_TblObj kaes[18];
#endif
    sAxsCtrl_TblObj datastore[18];

    sColPrty    pty[4];             //column property (# and bytes)
    u16         colCnt;
    u16         rowCnt;
    u16         objSize;            //table object size
    u16         maxCol;             //maximum column number in TCG spec
} sLckAxsCtrl_Tbl;
#pragma pack()

#define LCK_ACCESSCTRL_TBLOBJ_CNT       ((sizeof(sLckAxsCtrl_Tbl)- sizeof(sColPrty)*4 - 8) / sizeof(sAxsCtrl_TblObj))

//--------------------------------------------------------
// ACE Table prototype:
//--------------------------------------------------------
#define LCK_ACE_BOOLEXPR_CNT    14  //(TCG_AdminCnt+TCG_UserCnt+1)
#pragma pack(1)
typedef struct
{
    u64 uid;
    u64 booleanExpr[LCK_ACE_BOOLEXPR_CNT]; //UID List,  AdminX (4) + UserY (8) + Admins + Users
    u8  col[ACE_COLUMNS_CNT];          //VBYTE_TYPE, ?? how to deal with?  bit# stands for col#?  //size 9->12
} sLckACE_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr     hdr;
    sColPrty       pty[3];            //column cnt
    sLckACE_TblObj val[LCK_ACE_TBLOBJ_CNT];  //row cnt  +25
} sLckACE_Tbl;
#pragma pack()

//--------------------------------------------------------
// Authority Table prototype:
//--------------------------------------------------------
//#pragma pack(1)
//typedef struct
//{
//    u64     uid;
//    u8      name[8];
//    u8      commonName[32];         //Opal2
//    u32     isClass;                //bool
//    u64     Class;
//    u32     enabled;                //bool
//    secure_message  secure;
//    hash_protocol   HashAndSign;
//    u32     presentCertificate;     //bool
//    auth_method operation;
//    u64     credential;
//    u64     responseSign;
//    u64     responseExch;
//} sLckAuthority_TblObj;
//#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr        hdr;
    sColPrty          pty[13];            //column cnt
    sAuthority_TblObj val[LCK_AUTHORITY_TBLOBJ_CNT];       //row cnt
} sLckAuthority_Tbl;
#pragma pack()

//--------------------------------------------------------
// CPIN Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    sTcgTblHdr   hdr;
    sColPrty     pty[7];             //column cnt
    sCPin_TblObj val[LCK_CPIN_TBLOBJ_CNT];
} sLckCPin_Tbl;
#pragma pack()

//--------------------------------------------------------
// SecretProtect Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64     uid;
    u64     table;
    u8      colNumber;
    u8      protectMechanism[3];
} sSecretProtect_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr              hdr;
    sColPrty                pty[4];                 //column cnt
    sSecretProtect_TblObj   val[1];
} sSecretProtect_Tbl;
#pragma pack()

//--------------------------------------------------------
// LockingInfo Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct
{
    u64     uid;
    u8      name[8];
    u8      version;
    u8      encryptSupport;
    u8      maxRanges;
    u8      alignentReuired;
    u16     logicalBlockSize;
    u64     alignmentGranularity;
    u8      lowestAlignedLBA;
    u64     singleUserModeRange[LOCKING_RANGE_CNT+1];   //Range N <-> User N+1
    u32     rangeStartLengthPolicy;
} sLockingInfo_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct
{
    sTcgTblHdr  hdr;
    sColPrty    pty[11];            //column cnt
    sLockingInfo_TblObj val[1];
} sLockingInfo_Tbl;
#pragma pack()

//--------------------------------------------------------
// Locking Table prototype:
//--------------------------------------------------------
typedef enum
{
    PowerCycle = 0,
    Hardware = 1,
    HotPlug = 2,
    Programmatic = 3
} reset_types;

#pragma pack(1)
typedef struct {
    u64    uid;
    u8     name[20];
    u8     commonName[32];
    u64    rangeStart;
    u64    rangeLength;
    u8     readLockEnabled;
    u8     writeLockEnabled;
    u8     readLocked;
    u8     writeLocked;
    u8     lockOnReset[4];
    u64    activeKey;
  #if TCG_FS_CONFIG_NS
    u32    namespaceId;
    u32    namespaceGRange;
  #endif
}sLocking_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct {
    sTcgTblHdr  hdr;
  #if TCG_FS_CONFIG_NS
    sColPrty    pty[11+2];          //column cnt
  #else
    sColPrty    pty[11];            //column cnt
  #endif
    sLocking_TblObj val[LOCKING_RANGE_CNT+1];
} sLocking_Tbl;
#pragma pack()

//--------------------------------------------------------
// MBRControl Table prototype:
//--------------------------------------------------------
#pragma pack(1)
typedef struct {
    u64    uid;
    u32    enable;
    u32    done;
    u8     doneOnReset[4];     //List, first byte is the element count
} sMbrCtrl_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct {
    sTcgTblHdr  hdr;
    sColPrty    pty[4];             //column cnt
    sMbrCtrl_TblObj val[1];
} sMbrCtrl_Tbl;
#pragma pack()

//--------------------------------------------------------
// K_AES Table prototype:
//--------------------------------------------------------
typedef enum
{
    AES_ECB=0,
    AES_CBC=1,
    AES_CTR=5,
    AES_XTS=7
} symmetric_mode_media;

#pragma pack(1)
typedef struct {
    u64  uid;
    //u8    name[32];
#if 0
	u32 key[8];   //moved to "sWrappedKey mWKey[LOCKING_RANGE_CNT + 1];"  //Max modify 
#else
    u32 key1[8];
	u32 icv1[2];
	u32 key2[8];
	u32 icv2[2];
#endif
    u32  mode; //symmetric_mode_media  mode;
} sKAES_TblObj;
#pragma pack()

#pragma pack(1)
typedef struct {
    sTcgTblHdr  hdr;
    sColPrty    pty[4];             //column cnt
    sKAES_TblObj val[TCG_MAX_KEY_CNT];
} sKAES_Tbl;
#pragma pack()

#define TCG_KEY_WRAPPED   0x57524150        // WRAP
#define TCG_KEY_UNWRAPPED 0x554e5752        // UNWR
#define TCG_KEY_NULL      0x00
#define TO_RAW_KEY_BUF    0x00      // to rawkey
#define TO_MTBL_KEYTBL    0x01      // to wkey

#define ATA_KEY_WRAP_ENABLED    0x80
#define ATA_MASTER_IN_HASH      0x01

#pragma pack(1)
typedef struct
{
    u16  idx;       
    u32  salt[8];
}sKEKsalt;
#pragma pack()

#pragma pack(1)
typedef struct
{
    u32  idx;
    s32  state; // WrappedKey State = Salt State
    u32  opalKEK[8];
    u32  icv[2];    // for keywrap, integrated check value  //it should be 64-bit
    u32  salt[8];   // salt is used to PBKDF2
} sWrappedOpalKey;
#pragma pack()

#pragma pack(4)
typedef union
{
    struct
    {
        sTcgTblInfo       mTcgTblInfo;          //c
        sSPInfo_Tbl       mAdmSPInfo_Tbl;       //c
        sSPTemplates_Tbl  mAdmSPTemplates_Tbl;  //c
        sTbl_Tbl          mAdmTbl_Tbl;          //c
        sMethod_Tbl       mAdmMethod_Tbl;       //c
        sAdmAxsCtrl_Tbl   mAdmAxsCtrl_Tbl;      //c
        sACE_Tbl          mAdmACE_Tbl;          //c
        sAuthority_Tbl    mAdmAuthority_Tbl;    //m
        sCPin_Tbl         mAdmCPin_Tbl;         //m
        sTPerInfo_Tbl     mAdmTPerInfo_Tbl;     //m
        sTemplate_Tbl     mAdmTemplate_Tbl;     //c
        sSP_Tbl           mAdmSP_Tbl;           //m
    #if (_TCG_ == TCG_PYRITE)
        sRemovalMsm_Tbl   mAdmRemovalMsm_Tbl;   //m
    #endif
        u32               mEndTag;
    } b;    // 1980h bytes(8K)
    //u8 all[CFG_UDATA_PER_PAGE * 1];     // buf size = 1 page = 16K
                                        ///< All bits.
} tG1;
#pragma pack()

#pragma pack(4)
typedef union
{
    struct
    {
        sTcgTblInfo             mTcgTblInfo;            //c
        sSPInfo_Tbl             mLckSPInfo_Tbl;         //c
        sSPTemplates_Tbl        mLckSPTemplates_Tbl;    //c
        sLckTbl_Tbl             mLckTbl_Tbl;            //m
        sLckMethod_Tbl          mLckMethod_Tbl;         //c
        sLckAxsCtrl_Tbl         mLckAxsCtrl_Tbl;        //m
#if _TCG_!=TCG_PYRITE
        sSecretProtect_Tbl      mLckSecretProtect_Tbl;  //c
#endif
        sLockingInfo_Tbl        mLckLockingInfo_Tbl;    //m
        u32                     mEndTag;
    } b;    // 57e4h bryts(32K)
    //u8 all[CFG_UDATA_PER_PAGE * 2];     // buf size = 2 page = 32K  ///< All bits.
} tG2;
#pragma pack()

#pragma pack(4)
typedef union
{
    struct
    {
        sTcgTblInfo         mTcgTblInfo;                    //c
        sLckACE_Tbl         mLckACE_Tbl;                    //m
        sLckAuthority_Tbl   mLckAuthority_Tbl;              //m
        sLckCPin_Tbl        mLckCPin_Tbl;                   //m
        sLocking_Tbl        mLckLocking_Tbl;                //m
        sMbrCtrl_Tbl        mLckMbrCtrl_Tbl;                //m
#if _TCG_ != TCG_PYRITE
        sKAES_Tbl           mLckKAES_256_Tbl;               //c
        sKEKsalt            mKEKsalt[TCG_MAX_KEY_CNT];         //m
#endif
#if CO_SUPPORT_AES
        
        sWrappedOpalKey     mOpalWrapKEK[TCG_AdminCnt + TCG_UserCnt + 1 + 2];
#endif
        u32                 mEndTag;
    } b;    // 4dc0h byte
    //u8 all[CFG_UDATA_PER_PAGE * 2];     // buf size = 2 page = 32K  ///< All bits.
} tG3;
#pragma pack()

extern  tG1     G1;
extern  tG2     G2;
extern  tG3     G3;
extern  tG3     *pG3;
extern  tG4     *pG4;
extern  tG5     *pG5;

#endif

#endif // #ifndef tcgtbl_h

