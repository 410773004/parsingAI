#pragma once
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

#include "req.h"
#include "types.h"
#include "tcgcommon.h"
#include "tcgtbl.h"


#define eMaxSetParamCnt             16 //32

#define HostPropertiesCnt           6
#define TperPropertiesCnt           17

//H2TP properties table
#define HOST_MAX_COMPKT_SZ          0x800
#define HOST_MAX_PKT_SZ             (HOST_MAX_COMPKT_SZ-20)
#define HOST_MAX_INDTKN_SZ          (HOST_MAX_COMPKT_SZ-56)

// TP2H properties table
#define MAX_COMPKT_SZ               (0xC000)  //change from 0xC000 to 0x8000, Jack Li   //change from 0x2000 to 0x2494 for WareSystem AP
#define MAX_PKT_SZ                  (MAX_COMPKT_SZ-20)
#define MAX_INDTKN_SZ               (MAX_COMPKT_SZ-56)
#define MAX_RESP_COMPKT_SZ          (0xC000)  //change from 0xC000 to 0x8000, Jack Li   //change from 0x2000 to 0x2494 for WareSystem AP

// timeout setting for OCP spec SEC-45
//    |--> def: 120,000 ms
//    |--> max:       0 ms
//    '--> min:     500 ms
#define DEF_SESSION_TIMEOUT         120000
#define MAX_SESSION_TIMEOUT         0
#define MIN_SESSION_TIMEOUT         500

/*
#define transaction_ok_s            cb_Transaction_Ok_
#define cb_transaction_ok(a)        glue(transaction_ok_s, a)
#define transaction_ng_s            cb_Transaction_Ng_
#define cb_transaction_ng(a)        glue(transaction_ng_s, a)
*/

//TPer Definition
#if _TCG_==TCG_OPAL
    #define BASE_COMID              0x2000 // 0x1001
#elif _TCG_==TCG_EDRV
    #define BASE_COMID              0x1002
#elif _TCG_==TCG_PYRITE
    #define BASE_COMID              0x1003
#endif
#define BASE_SPSESSION_ID           0x1001

#define TCG_BUF_LEN MAX_RESP_COMPKT_SZ     // size=16K
#define LAA_LEN     CFG_UDATA_PER_PAGE

//ddr_sh_data tcg_sync_t tcg_sync = {0};

typedef union
{
    u64  all;
    u32  dw[2];
    u8   bytes[8];
} UID64;


typedef struct
{
    u8   rsv[6];
    u16  kind;
    u32  length;
} sSubPktFmt;

typedef struct
{
    u32  TSN;
    u32  HSN;
    u32  SeqNo;
    u8   rsv[2];
    u16  AckType;
    u32  ack;
    u32  length;
} sPktFmt;

typedef struct
{
    u8     rsv[4];
    u16    ComID;
    u16    ComIDExt;
    u32    Outstanding;
    u32    MinTx;
    u32    length;
    sPktFmt     mPktFmt;
    sSubPktFmt  mSubPktFmt;
    //u8      payload[456];   //payload, b56~b511
    u8      payload[TCG_BUF_LEN \
                        -sizeof(u8)*4 \
                        -sizeof(u16) \
                        -sizeof(u16) \
                        -sizeof(u32) \
                        -sizeof(u32) \
                        -sizeof(u32) \
                        -sizeof(sPktFmt)  \
                        -sizeof(sSubPktFmt)];
    //u16        idx;
} sCmdPkt;

typedef struct
{
    u8  cmd;
    u8  ProtocolID;
    u8  Len;
    u16 ComID;
} sTcgCmd;

typedef struct
{
  u8     cnt;
  u8     policy;
  u16    range;    //b0=1 for GRange is SingleUser, bN=1 for RangeN is SingleUser
} sSgUser;


typedef  union
{
    u32 all32;
    struct
    {
        u32 G1              :1;
        u32 G2              :1;
        u32 G3              :1;
        u32 SMBR            :1;
        u32 DS              :1;
        u32 EE              :1;
        u32 SMBR_Cb_Acting  :1;
        u32 DS_Cb_Acting    :1;

        u32 rsv      :1;
    }__attribute__((packed)) b;
}__attribute__((packed)) tcgMtblChngFlg_t;

extern void ResetSessionManager();

typedef enum
{
    ST_POWER_OFF,
    ST_AWAIT_IF_SEND,
    ST_PROCESSING,
    ST_AWAIT_IF_RECV,
    ST_NAN = 0x7FFFFFFF
} tTCG_CMD_STATE;

typedef struct
{
    char *name;
    u32 val;
} sProperties;

typedef enum
{
    PROPERTY_PARSE_OK =     0,
    PROPERTY_PARSE_TOK_ERR,
    PROPERTY_PARSE_ST_ERR,
} PROPERTY_PARSE_t;


typedef struct
{
    u64     HtAuthorityClass[AUTHORITY_CNT];   //added for Authority Class: Null / Admins / Users
    UID64   SPID;               //AdminSP or LockingSP
    UID64   HtSgnAuthority[AUTHORITY_CNT];     //3, Authority obj
	u8      wptr_auth;
    u32     sessionTimeout;     // in millisecond
    u64     sessionStartTime;   //
    u32     bWaitSessionStart;

    u32     HostSessionID;
    u32     SPSessionID;        //assigned by TPer
    session_state_t state;
    trnsctn_state_t TransactionState;
    u8      HtChallenge[36];    //0
    u8      Write;
    u8      status[3];
} sessionManager_t;

typedef struct
{
  u32    offset;
  u32    length;
} sDataStoreAddr;

typedef struct
{
    bool doBgTrim;
} revert_varsMgm_t;

typedef struct
{
    u8  keepGlobalRangeKey;
	u32 backup_GR_key1[10];  //aes key + icv1
 	u32 backup_GR_key2[10];  //xts key + icv2
	u32 backup_GR_KEKsalt[8];
} revertSp_varsMgm_t;

typedef struct
{
    u8  bAdmin1PIN;
    u8  sgUserCnt;
    u8  sgUserPolicy;
    u8  dsCnt;
    u16 sgUserRange;
    u32 DSTblSize[DSTBL_MAX_NUM];
    u8  bk_HtChallenge[36];
	u8  bk_HtChallenge_salt[32];
} reactivate_varsMgm_t;

typedef struct
{
    u8*  pbuf;
    u32  smbrWrLen;
    u32  columnBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdSetSmbr_varsMgm_t;

typedef struct
{
    u8*  pbuf;
    u32  dsWrLen;
    u32  columnBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdSetDs_varsMgm_t;


typedef struct
{
    u32  smbrRdLen;
    u32  rowBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdGetSmbr_varsMgm_t;

typedef struct
{
    u32  singleUserMode_startColumn;
    u32  dsRdLen;
    u32  rowBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdGetDs_varsMgm_t;


typedef struct {
    unsigned long total[2]; /*!< number of bytes processed  */
    unsigned long state[8]; /*!< intermediate digest state  */
    unsigned char buffer[136];   /*!< data block being processed */

    unsigned char ipad[136]; /*!< HMAC: inner padding        */
    unsigned char opad[136]; /*!< HMAC: outer padding        */
} sha3_context;

//-----------------------------------------------------------------------------
//  Public function prototype definitions:
//-----------------------------------------------------------------------------

void tcg_if_post_sync_request(void);
void tcg_if_post_sync_response(void);
bool isTcgIfReady(void);

	
//TCG Function Declaration:
u16  tcg_properties(req_t *req);
void InitTbl(void);

void LockingTbl_Reset(u8 type);
void DataStoreAddr_Update(void);
void SingleUser_Update(void);
//void LockingRangeTable_Update(void);
void MbrCtrlTbl_Reset(u8 type);

void ClearMtableChangedFlag(void);

u16  Level0_Discovery(u8 *);
void Supported_Security_Protocol(u8 *);

u16  invoking_session_manager(req_t *req);
u16  invoking_tcg_table(req_t *req);
u16  AtomDecoding_Uid2(u8 *);
u16  AtomDecoding_Uint8(u8 *);
u16  AtomDecoding_Uint32(u32 *);
u16  AtomDecoding_u64(u32 *);
u16  AtomDecoding_Uint(u8 *, u32);
int  AtomEncoding_ByteHdr(u32);
int  AtomEncoding_ByteSeq(u8*, u32);
int  AtomEncoding_Int2Byte(u8*, u32);
int  AtomEncoding_Integer(u8*, u8);
u8   ChkToken(void);

u16  host_signing_authority_check(void);
u16  chk_method_status(void);
void ResetSessionManager();

int  tcg_access_control_check(bool* invIdIsFound);
int  admin_aceBooleanExpr_chk(bool bNotGetACL);
int  locking_aceBooleanExpr_chk(bool bNotGetACL);

extern u16 tcg_cmdPkt_abortSession(void);
extern u32 tcg_cmdPkt_payload_decoder(req_t *req);
extern u16 tcg_cmdPkt_extracter(req_t *req, u8* buf);	

u32 locking_for_methodUid_index(u64 mtdUid);

u16  f_method_next(req_t *req);
u16  f_method_getAcl(req_t *req);
u16  f_method_genKey(req_t *req);
u16  f_method_revertSp(req_t *req);
u16  f_method_get(req_t *req);
u16  f_method_set(req_t *req);
u16  f_method_authenticate(req_t *req);
u16  f_method_revert(req_t *req);
u16  f_method_activate(req_t *req);
u16  f_method_random(req_t *req);
u16  f_method_reactivate(req_t *req);
u16  f_method_erase(req_t *req);
u16  f_method_illegal(req_t *req);

u16  Method_Next(req_t *req);
u16  Method_Get(req_t *req);
u16  Method_Set(req_t *req);
u16  Method_GetACL(req_t *req);
u16  Method_Activate(req_t *req);
u16  Method_Revert(req_t *req);
u16  Method_GenKey(req_t *req);
u16  Method_RevertSP(req_t *req);
u16  Method_Authenticate(req_t *req);
u16  Method_Random(req_t *req);
u16  Method_Reactivate(req_t *req);
u16  Method_Erase(req_t *req);

bool chkMultiAuths(u64 auth, bool chkClass);
int  aceColumns_chk(u32);
int  Write2Mtable(req_t*,u8 *tBuf, u32 tLen, u32 setColNo, u8 MultiAuthority_idx);
int  LockingTbl_RangeChk(u64, u64, u64);

int  CPinMsidCompare(u8);
void CPinTbl_Reset(void);
void Tcg_GenCPinHash(u8 *pSrc, u8 srcLen, sCPin *pCPin);

void TcgChangeKey(u8 idx);
void Tcg_Key_wp_uwp(u8 idx , u8 case_select); 

void TcgRestoreGlobalKey(void);
void TcgEraseOpalKEK(u32 auth);
int  TcgWrapOpalKEK(u8*, u8, u32 auth, u32* pKEK);
void Tcg_WrapDEK(u8 range, u32* pKEK);
void Tcg_UnWrapDEK(u8 range, u32* pKEK, u8 target);

void TcgUpdateRawKey(u32);
void TcgUpdateWrapKey(u32);
void TcgUpdateRawKeyList(u32);
void TcgUpdateWrapKeyList(u32);

void TcgEraseKey(u8 rangeNo);


u16  TcgTperReset(req_t *req);
void TcgHardReset(void);

u16 TcgBlkSIDAuthentication(req_t *req);

extern tTCG_CMD_STATE    gTcgCmdState;
extern u16  keep_result;
extern u16  tcg_prepare_respPacket_update(bool);

void DataStore_Setting(u8 cnt, u32 * DSTblSize);
void DataStoreAddr_Update(void);
void Get_DataStore();
void Set_DataStore();

void Get_SMBR();
void Set_SMBR();

void SingleUser_Update(void);
void SingleUser_Setting(void);


void FetchTcgTbl(u8 *src, u32 size);
int  FetchAxsCtrlTbl(u64 spid, u16 *pByteCnt, u16 *pRowCnt);
void tcg_disable_mbrshadow(void);


//void HAL_PBKDF2(u32* src, u32 srcLen, u32* salt, u32 saltLen, u32* dest);
void PKCS5_PBKDF2_HMAC(u32 *password, u32 plen, u32 *salt, u32 slen,
     const unsigned long iteration_count, const unsigned long key_length, u32 *output);

enum {
	AES_256B_KWP_NO_SECURE = 0,
	AES_256B_KUWP_NO_SECURE,
};


/******************************************************
 *            = ^_^ =  inline = ^_^ =
 ******************************************************/
extern u8 statusCode[];
extern u8 *dataBuf;
extern u16 iDataBuf;

ddr_code inline void set_status_code(u8 s0, u8 s1, u8 s2)
{
    statusCode[0] = s0;
    statusCode[1] = s1;
    statusCode[2] = s2;
}

ddr_code inline void fill_staus_token_list(void)
{
    dataBuf[iDataBuf++] = TOK_StartList;    //0xF0
    dataBuf[iDataBuf++] = statusCode[0];    //Status: OK, TODO: update error code here...
    dataBuf[iDataBuf++] = statusCode[1];
    dataBuf[iDataBuf++] = statusCode[2];
    dataBuf[iDataBuf++] = TOK_EndList;      //0xF1
}

ddr_code inline void fill_no_data_token_list(void)
{
    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;
}

ddr_code inline void fill_u16(u8 *buf, u16 data)
{
    *((u8 *)buf)   = HiByte(data);
    *((u8 *)buf+1) = LoByte(data);
}


ddr_code inline void set_status_with_token_list(u8 s0, u8 s1, u8 s2, u8 *pbuf)
{
    *pbuf++ = TOK_StartList;
    *pbuf++ = s0;
    *pbuf++ = s1;
    *pbuf++ = s2;
    *pbuf++ = TOK_EndList;
}

ddr_code inline void fill_u32(u8 *buf, u32 data)
{
    *((u8 *)buf)   = HiWord(HiByte(data));
    *((u8 *)buf+1) = HiWord(LoByte(data));
    *((u8 *)buf+2) = LoWord(HiByte(data));
    *((u8 *)buf+3) = LoWord(LoByte(data));
}

ddr_code u16 inline swap_u16(u16 data)
{
    Packedu16_t tmp;

    tmp.byte.low  = ((Packedu16_t)data).byte.high;
    tmp.byte.high = ((Packedu16_t)data).byte.low;
    return tmp.word;
}

ddr_code inline u32 swap_u32(u32 data)
{
    Packedu32_t tmp;

    tmp.byte.b0 = ((Packedu32_t)data).byte.b3;
    tmp.byte.b1 = ((Packedu32_t)data).byte.b2;
    tmp.byte.b2 = ((Packedu32_t)data).byte.b1;
    tmp.byte.b3 = ((Packedu32_t)data).byte.b0;
    return tmp.dword;
}

ddr_code inline u64 swap_u64(u64 data)
{
    Packedu64_t tmp;

    tmp.byte.b0 = ((Packedu64_t)data).byte.b7;
    tmp.byte.b1 = ((Packedu64_t)data).byte.b6;
    tmp.byte.b2 = ((Packedu64_t)data).byte.b5;
    tmp.byte.b3 = ((Packedu64_t)data).byte.b4;
    tmp.byte.b4 = ((Packedu64_t)data).byte.b3;
    tmp.byte.b5 = ((Packedu64_t)data).byte.b2;
    tmp.byte.b6 = ((Packedu64_t)data).byte.b1;
    tmp.byte.b7 = ((Packedu64_t)data).byte.b0;
    return tmp.qword;
}
extern tTCG_CMD_STATE    gTcgCmdState;
extern bool is_cb_executed;
extern u16  keep_result;
extern u16  tcg_prepare_respPacket_update(bool);
extern u16  tcg_cmdPkt_abortSession(void);
extern void tcg_prepare_respPacket(void);

//extern void nvmet_evt_cmd_done(req_t *req);
extern bool nvmet_core_cmd_done(req_t *req);

static ddr_code inline void method_complete_post(req_t *req, bool addStatus)
{
    u32 result;
    //DBG_P(3, 3, 0x750000, 4, addStatus, 4, is_cb_executed);
	
    if((keep_result != STS_SESSION_ABORT) && (keep_result != STS_STAY_IN_IF_SEND)){
        result = tcg_prepare_respPacket_update(addStatus);
    }else{
        result = keep_result;
    }

    // result = tcg_prepare_respPacket_update(addStatus);
    if(result == STS_STAY_IN_IF_SEND)
    {
        //DBG_P(1, 3, 0x820166);  //82 01 66, "!!NG: StreamDecode NG -> Stay in IF-SEND"
		//printk("!!NG: StreamDecode NG -> Stay in IF-SEND");
		//TCGPRN("Error! StreamDecode NG -> Stay in IF-SEND\n");
        gTcgCmdState = ST_AWAIT_IF_SEND;
    }

    if(result == STS_SESSION_ABORT)
    { //prepare payload for "Close Session"
        //DBG_P(1, 3, 0x820167);  //82 01 67, "!!NG: StreamDecode NG -> Abort Session"
		//printk("!!NG: StreamDecode NG -> Abort Session");
        //TCGPRN("Error! StreamDecode NG -> Abort Session\n");
        ResetSessionManager();
        tcg_cmdPkt_abortSession();
    }
    else if(result == STS_RESPONSE_OVERFLOW)
    {
        tcg_prepare_respPacket();  //TcgCmdPkt4Response();
        fill_no_data_token_list();

        //add status to reponse buffer and update length
        set_status_code(result, 0, 0);
        tcg_prepare_respPacket_update(mTRUE);  // TcgRespPktUpdate();
    }

    gTcgCmdState = ST_AWAIT_IF_RECV;
/*
    req->completion = nvmet_core_cmd_done;

	if(is_cb_executed){   // if FASLE then put DTAG & free MEM at NvmeCmd_security_send_XferDone()
        nvmet_evt_cmd_done(req);
        is_cb_executed = mFALSE;
    }
*/
}


//extern tSYSINFO_PAYLOAD*    smSysInfo;


#else //----------------------------------------------------------------------------------------------------------------------

#define TCG_BUF_LEN (16*1024)*3             // size=16K
#define LAA_LEN     CFG_UDATA_PER_PAGE

#define TCG_OVLIM_DISABLED

//#define FORCE_TO_CLEAR_ERASED_COUNT

// KEY_NO 0 is for FTL etc...
// KEY_NO 1 is for user data non-specific region (global)
// KEY_NO 2~9 for user data specific regions
#define GLOBAL_AES_KEY_NO           1
//#define TCG_REGION_TEST

#define TCG_RD_ERR_DESC_NO_MAX_SIZE  CFG_CHANNEL_CNT * CFG_CE_CNT * TCG_DISC_CNT_PER_PAGE

#define TCG_SILO_TXH_LEN            0x20    //TCG Silo transfer command header length

#define HeadOfEachBlk_Cnt           3

#define TCG_GROUP4_MARK             0x00434734

#define START_TCG_DS_IDX            START_TCG_MBR_IDX

#define TCG_GROUP5_MARK             0x00434735

#define TCG_GROUP1_MARK             0x54434731
#define TCG_GROUP1_START_CELL_NO    0
#define TCG_GROUP1_CELLS            1
#define TCG_GROUP1_END_CELL_NO      (TCG_GROUP1_START_CELL_NO+TCG_GROUP1_CELLS)
#define TCG_GROUP2_MARK             0x54434732
#define TCG_GROUP2_START_CELL_NO    TCG_GROUP1_END_CELL_NO
#define TCG_GROUP2_CELLS            2
#define TCG_GROUP2_END_CELL_NO      (TCG_GROUP2_START_CELL_NO+TCG_GROUP2_CELLS)
#define TCG_GROUP3_MARK             0x54434733
#define TCG_GROUP3_START_CELL_NO    TCG_GROUP2_END_CELL_NO
#define TCG_GROUP3_CELLS            2
#define TCG_GROUP3_END_CELL_NO      (TCG_GROUP3_START_CELL_NO+TCG_GROUP3_CELLS)

#define TCG_DISC_CNT_PER_PAGE       2
#define TCG_LAA_CNT_PER_DESC        2


//TPer Definition
#if _TCG_==TCG_OPAL
    #define BASE_COMID              0x2000 // 0x1001
#elif _TCG_==TCG_EDRV
    #define BASE_COMID              0x1002
#elif _TCG_==TCG_PYRITE
    #define BASE_COMID              0x1003
#endif
#define BASE_SPSESSION_ID           0x1001

#define PropertiesAmount            25
#define HostPropertiesCnt           6
#define TperPropertiesCnt           17

//H2TP properties table
#define HOST_MAX_COMPKT_SZ          0x800
#define HOST_MAX_PKT_SZ             0x7EC
#define HOST_MAX_INDTKN_SZ          0x7C8

// TP2H properties table
#define MAX_COMPKT_SZ               (0xC000)  //change from 0x2000 to 0x2494 for WareSystem AP
#define MAX_PKT_SZ                  (MAX_COMPKT_SZ-20)
#define MAX_INDTKN_SZ               (MAX_COMPKT_SZ-56)
#define MAX_RESP_COMPKT_SZ          0xC000  //change from 0x2000 to 0x2494 for WareSystem AP

#define DEF_SESSION_TIMEOUT         (15*60000)
#define MAX_SESSION_TIMEOUT         0
#define MIN_SESSION_TIMEOUT         60000

#define eMaxSetParamCnt             16 //32

#define revert_s                    cb_Revert_
#define cb_revert(a, b)             glue(revert_s, glue(a, b))
#define revertsp_s                  cb_RevertSP_
#define cb_revertsp(a)              glue(revertsp_s, a)
#define activate_s                  cb_Activate_
#define cb_activate(a)              glue(activate_s, a)
#define genkey_s                    cb_Genkey_
#define cb_genkey(a)                glue(genkey_s, a)
#define Reactivate_s                cb_Reactivate_
#define cb_Reactivate(a)            glue(Reactivate_s, a)
#define erase_s                     cb_Erase_
#define cb_erase(a)                 glue(erase_s, a)
#define set_ok_s                    cb_Set_Ok_
#define cb_set_ok(a)                glue(set_ok_s, a)
#define set_ng_s                    cb_Set_Ng_
#define cb_set_ng(a)                glue(set_ng_s, a)
#define transaction_ok_s            cb_Transaction_Ok_
#define cb_transaction_ok(a)        glue(transaction_ok_s, a)
#define transaction_ng_s            cb_Transaction_Ng_
#define cb_transaction_ng(a)        glue(transaction_ng_s, a)
#define tcgnvmformat_s              cb_TcgNvmFormat_
#define cb_tcgnvmformat(a)          glue(tcgnvmformat_s, a)
#define tcgpreformat_s              cb_TcgPreFormat_
#define cb_tcgpreformat(a)          glue(tcgpreformat_s, a)
#define setSmbr_s                   cb_SetSMBR_
#define cb_setSmbr(a)               glue(setSmbr_s, a)
#define setDs_s                     cb_SetDS_
#define cb_setDs(a)                 glue(setDs_s, a)
#define getSmbr_s                   cb_GetSMBR_
#define cb_getSmbr(a)               glue(getSmbr_s, a)
#define getDs_s                     cb_GetDS_
#define cb_getDs(a)                 glue(getDs_s, a)

typedef union
{
    u64  all;
    u32  dw[2];
    u8   bytes[8];
} UID64;

typedef struct
{
    u8   rsv[6];
    u16  kind;
    u32  length;
} sSubPktFmt;

typedef struct
{
    u32  TSN;
    u32  HSN;
    u32  SeqNo;
    u8   rsv[2];
    u16  AckType;
    u32  ack;
    u32  length;
} sPktFmt;

typedef struct
{
    u8     rsv[4];
    u16    ComID;
    u16    ComIDExt;
    u32    Outstanding;
    u32    MinTx;
    u32    length;
    sPktFmt     mPktFmt;
    sSubPktFmt  mSubPktFmt;
    //u8      payload[456];   //payload, b56~b511
    u8      payload[TCG_BUF_LEN \
                        -sizeof(u8)*4 \
                        -sizeof(u16) \
                        -sizeof(u16) \
                        -sizeof(u32) \
                        -sizeof(u32) \
                        -sizeof(u32) \
                        -sizeof(sPktFmt)  \
                        -sizeof(sSubPktFmt)];
    //u16        idx;
} sCmdPkt;

typedef struct
{
    u8  cmd;
    u8  ProtocolID;
    u8  Len;
    u16 ComID;
} sTcgCmd;

/* typedef struct
{
    u8 InvokingUID[8];
    u8 MethodUID[8];
    ...
} sTcgCmdParam; */

typedef struct
{
    u64     HtAuthorityClass;   //added for Authority Class: Null / Admins / Users
    UID64   SPID;               //AdminSP or LockingSP
    UID64   HtSgnAuthority;     //3, Authority obj

    u32     sessionTimeout;     // in millisecond
    u32     sessionStartTime;   //
    u32     bWaitSessionStart;

    u32     HostSessionID;
    u32     SPSessionID;        //assigned by TPer
    session_state_t state;
    trnsctn_state_t TransactionState;
    u8      HtChallenge[36];    //0
    u8      Write;
    u8      status[3];
} sessionManager_t;
//COMPILE_ASSERT((sizeof(sessionManager_t)%4) == 0, "please align to 4 bytes!"); // marked by Jack

typedef struct
{
  u32    offset;
  u32    length;
} sDataStoreAddr;

typedef struct
{
  u8     cnt;
  u8     policy;
  u16    range;    //b0=1 for GRange is SingleUser, bN=1 for RangeN is SingleUser
} sSgUser;


typedef enum
{
    ST_POWER_OFF,
    ST_AWAIT_IF_SEND,
    ST_PROCESSING,
    ST_AWAIT_IF_RECV,
    ST_NAN = 0x7FFFFFFF
} tTCG_CMD_STATE;

typedef enum
{
    PROPERTY_PARSE_OK =     0,
    PROPERTY_PARSE_TOK_ERR,
    PROPERTY_PARSE_ST_ERR,
} PROPERTY_PARSE_t;

typedef struct
{
    char *name;
    u32 val;
} sProperties;

typedef  union
{
    u32 all32;
    struct
    {
        u32 g1_wr_req       :1;
        u32 g2_wr_req       :1;
        u32 g3_wr_req       :1;
        u32 ds_wr_req       :1;
        u32 smbr_wr_req     :1;
        u32 ee_wr_req       :1;

        u32 rsv             :1;
    }__attribute__((packed)) b;
}__attribute__((packed)) tcgHalWrFlg_t;

typedef  union
{
    u32 all32;
    struct
    {
        u32 g1_rd_req       :1;
        u32 g2_rd_req       :1;
        u32 g3_rd_req       :1;
        u32 ds_rd_req       :1;
        u32 smbr_rd_req     :1;
        u32 ee_rd_req       :1;

        u32 rsv             :1;
    }__attribute__((packed)) b;
}__attribute__((packed)) tcgHalRdFlg_t;

typedef struct
{
    tcgHalWrFlg_t   hal_wr;
    tcgHalRdFlg_t   hal_rd;
} tcgHalActFlg_t;

typedef  union
{
    u32 all32;
    struct
    {
        u32 G1              :1;
        u32 G2              :1;
        u32 G3              :1;
        u32 SMBR            :1;
        u32 DS              :1;
        u32 EE              :1;
        u32 SMBR_Cb_Acting  :1;
        u32 DS_Cb_Acting    :1;

        u32 rsv      :1;
    }__attribute__((packed)) b;
}__attribute__((packed)) tcgMtblChngFlg_t;

typedef struct
{
    bool doBgTrim;
} revert_varsMgm_t;

typedef struct
{
    u8  keepGlobalRangeKey;
} revertSp_varsMgm_t;

typedef struct
{
    u8  bAdmin1PIN;
    u8  sgUserCnt;
    u8  sgUserPolicy;
    u8  dsCnt;
    u16 sgUserRange;
    u32 DSTblSize[DSTBL_MAX_NUM];
    u8  bk_HtChallenge[36];
} reactivate_varsMgm_t;

typedef struct
{
    u8*  pbuf;
    u32  smbrWrLen;
    u32  columnBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdSetSmbr_varsMgm_t;

typedef struct
{
    u8*  pbuf;
    u32  dsWrLen;
    u32  columnBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdSetDs_varsMgm_t;

typedef struct
{
    u32  smbrRdLen;
    u32  rowBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdGetSmbr_varsMgm_t;

typedef struct
{
    u32  singleUserMode_startColumn;
    u32  dsRdLen;
    u32  rowBeginAdr;
    u32  laaBeginAdr;
    u32  laaOffsetBeginAdr;
    u32  laaCnts;
    u32  idx;
    bool hasBlank;
} mtdGetDs_varsMgm_t;

// extern tcgLaa_t  MBR_TEMPL2PTBL[MBR_LEN / CFG_UDATA_PER_PAGE];
// extern tcgLaa_t  DS_TEMPL2PTBL[DATASTORE_LEN / CFG_UDATA_PER_PAGE];

extern u16  gTcgMbrCellVac[TCG_MBR_CELLS];              // 256
extern u32  gTcgMbrCellValidMap[TCG_MBR_CELLS][16];     // 4K
extern u16  gTcgTempMbrCellVac[TCG_MBR_CELLS];          // 256
extern u32  gTcgTempMbrCellValidMap[TCG_MBR_CELLS][16]; // 4K
extern bool SMBR_ioCmdReq;                              // Non-TrustCmd read TcgMBR area, 1-> Non-TrustCmd,  0->TrustCmd


//TCG Function Declaration:
u16  tcg_properties(req_t *req);
void InitTbl(void);

u16  Level0_Discovery(u8 *);
void Supported_Security_Protocol(u8 *);

u16  invoking_session_manager(req_t *req);
u16  invoking_tcg_table(req_t *req);
u16  AtomDecoding_Uid2(u8 *);
u16  AtomDecoding_Uint8(u8 *);
u16  AtomDecoding_Uint32(u32 *);
u16  AtomDecoding_u64(u32 *);
u16  AtomDecoding_Uint(u8 *, u32);
u8   ChkToken(void);

u16  host_signing_authority_check(void);
u16  chk_method_status(void);
void ResetSessionManager(req_t *req);

int  tcg_access_control_check(bool* invIdIsFound);
int  admin_aceBooleanExpr_chk(bool bNotGetACL);
int  locking_aceBooleanExpr_chk(bool bNotGetACL);

u16  f_method_next(req_t *req);
u16  f_method_getAcl(req_t *req);
u16  f_method_genKey(req_t *req);
u16  f_method_revertSp(req_t *req);
u16  f_method_get(req_t *req);
u16  f_method_set(req_t *req);
u16  f_method_authenticate(req_t *req);
u16  f_method_revert(req_t *req);
u16  f_method_activate(req_t *req);
u16  f_method_random(req_t *req);
u16  f_method_reactivate(req_t *req);
u16  f_method_erase(req_t *req);
u16  f_method_illegal(req_t *req);

u16  Method_Next(req_t *req);
u16  Method_Get(req_t *req);
u16  Method_Set(req_t *req);
int  aceColumns_chk(u32);
int  AtomEncoding_ByteHdr(u32);
int  AtomEncoding_ByteSeq(u8*, u32);
int  AtomEncoding_Int2Byte(u8*, u32);
int  AtomEncoding_Integer(u8*, u8);
u16  Method_GetACL(req_t *req);
void WriteMtable2NAND(req_t *req);
void ReadNAND2Mtable(req_t *req);
int  Write2Mtable(req_t*,u8 *tBuf, u32 tLen, u32 setColNo, u8 MultiAuthority_idx);
void ClearMtableChangedFlag(void);
int  CPinMsidCompare(u8);

u16  Method_Activate(req_t *req);
u16  Method_Revert(req_t *req);
u16  Method_GenKey(req_t *req);
u16  Method_RevertSP(req_t *req);

void LockingTbl_Reset(u8 type);
int  LockingTbl_RangeChk(u64, u64, u64);
//void LockingRangeTable_Update(void);

void TcgPreformatAndInit(req_t*);
void TcgKeepGroupDataBridge(u32 GrpNum);

void TcgAllClean(void);
void CPinTbl_Reset(void);
void MbrCtrlTbl_Reset(u8 type);

// u16  HandleComIDRequest(HcmdQ_t* pHcmdQ, u8* buf);
void HandleComIDResponse(u8 * buf);
u16  TcgTperReset(req_t *req);

u16  Method_Authenticate(req_t *req);
u16  Method_Random(req_t *req);
void SingleUser_Setting(void);
void DataStore_Setting(u8 cnt, u32 * DSTblSize);
void DataStoreAddr_Update(void);


void SingleUser_Update(void);
u16  Method_Reactivate(req_t *req);
u16  Method_Erase(req_t *req);
// void HostTx2TcgBuf (HcmdQ_t* pHcmdQ, u8* buf);
u16 TcgBlkSIDAuthentication(req_t *req);

#if _TCG_ == TCG_EDRV
// void SiloCmdProcessing(HcmdQ_t* pHcmdQ);
void ProbeSilo_Probe(u8* buf, u32 len);
void TcgSilo_GetSiloCap(u8* buf, u32 len);
// void TcgSilo_Transfer(HcmdQ_t* pHcmdQ, u8* buf, u32 len);
// void TcgSilo_Reset(HcmdQ_t* pHcmdQ, u8* buf, u32 len);
void TcgSilo_TPerReset(u8* buf, u32 len);
// void TcgSilo_GetResult(HcmdQ_t* pHcmdQ, u8* buf, u32 len);
#endif

int ChkDefaultTblPattern(void);

void dump_G4_erased_count(void);
void dump_G5_erased_count(void);
void DumpG4DftAmount(void);
void DumpG5DftAmount(void);
void DumpRangeInfo(void);
void DumpTcgEepInfo(void);
void DumpTcgKeyInfo(void);
void crypto_dump_range(void);

void Tcg_GenCPinHash(u8 *pSrc, u8 srcLen, sCPin *pCPin);

void TcgGetEdrvKEK(u8* pSrc, u8 len, u8 range, u32* pKEK);
int  TcgWrapOpalKEK(u8*, u8, u32 auth, u32* pKEK);
int  TcgUnwrapOpalKEK(u8*, u8, u32 auth, u32* pKEK);
void TcgEraseOpalKEK(u32 auth);

void Tcg_WrapDEK(u8 range, u32* pKEK);
void Tcg_UnWrapDEK(u8 range, u32* pKEK, u8 target);

void TcgUpdateRawKey(u32);
void TcgUpdateWrapKey(u32);
void TcgUpdateRawKeyList(u32);
void TcgUpdateWrapKeyList(u32);

void TcgChangeKey(u8 rangeNo);
void CPU1_chg_cbc_tbl_key(void);
void CPU1_chg_ebc_key_key(void);
void CPU1_chg_cbc_fwImage_key(void);
//void TcgNoKeyWrap(u8 rangeNo);
void TcgEraseKey(u8 rangeNo);

void FetchTcgTbl(u8 *src, u32 size);
int  FetchAxsCtrlTbl(u64 spid, u16 *pByteCnt, u16 *pRowCnt);
void tcg_disable_mbrshadow(void);

//-----------------------------------------------------------------------------
//  Public function prototype definitions:
//-----------------------------------------------------------------------------
//void TcgInit_CPU0_PwrCycle(uint32_t);
// void SATA_CMD_TrustedNonData(HcmdQ_t* pHcmdQ);

// int  tcg_if_post_sync_sign(void);
void tcg_if_post_sync_request(void);
void tcg_if_post_sync_response(void);
bool isTcgIfReady(void);

int  SMBR_Read(u16 laas, u16 laae, PVOID pBuffer);
int  SMBR_Write(u16 laas, u16 laae, PVOID pBuffer, u16 DesOffset, u16 SrcLen, u8* SrcBuf);
int  DS_Read(u16 laas, u16 laae, PVOID pBuffer);
int  DS_Write(u16 laas, u16 laae, PVOID pBuffer, u16 DesOffset, u16 SrcLen, u8* SrcBuf);

Error_t CmdTcg_OpEraDefTbl(Cstr_t pCmdStr, u32 argc, u32 argv[]);
Error_t CmdTcg_OpTcgInfo(Cstr_t pCmdStr, u32 argc, u32 argv[]);

Error_t CmdTcg_OpTcgDmpVidBlk(Cstr_t pCmdStr, u32 argc, u32 argv[]);

Error_t CmdTcg_SwitcherSetting(Cstr_t pCmdStr, u32 argc, u32 argv[]);


/******************************************************
 *            = ^_^ =  inline = ^_^ =
 ******************************************************/
extern u8 statusCode[];
extern u8 dataBuf[];
extern u16 iDataBuf;
tcg_code inline void set_status_code(u8 s0, u8 s1, u8 s2)
{
    statusCode[0] = s0;
    statusCode[1] = s1;
    statusCode[2] = s2;
}

tcg_code inline void set_status_with_token_list(u8 s0, u8 s1, u8 s2, u8 *pbuf)
{
    *pbuf++ = TOK_StartList;
    *pbuf++ = s0;
    *pbuf++ = s1;
    *pbuf++ = s2;
    *pbuf++ = TOK_EndList;
}

tcg_code inline void fill_staus_token_list(void)
{
    dataBuf[iDataBuf++] = TOK_StartList;    //0xF0
    dataBuf[iDataBuf++] = statusCode[0];    //Status: OK, TODO: update error code here...
    dataBuf[iDataBuf++] = statusCode[1];
    dataBuf[iDataBuf++] = statusCode[2];
    dataBuf[iDataBuf++] = TOK_EndList;      //0xF1
}

tcg_code inline void fill_no_data_token_list(void)
{
    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;
}

tcg_code inline void fill_u16(u8 *buf, u16 data)
{
    *((u8 *)buf)   = HiByte(data);
    *((u8 *)buf+1) = LoByte(data);
}

tcg_code inline void fill_u32(u8 *buf, u32 data)
{
    *((u8 *)buf)   = HiWord(HiByte(data));
    *((u8 *)buf+1) = HiWord(LoByte(data));
    *((u8 *)buf+2) = LoWord(HiByte(data));
    *((u8 *)buf+3) = LoWord(LoByte(data));
}

tcg_code inline u16 swap_u16(u16 data)
{
    Packedu16_t tmp;

    tmp.byte.low  = ((Packedu16_t)data).byte.high;
    tmp.byte.high = ((Packedu16_t)data).byte.low;
    return tmp.word;
}

tcg_code inline u32 swap_u32(u32 data)
{
    Packedu32_t tmp;

    tmp.byte.b0 = ((Packedu32_t)data).byte.b3;
    tmp.byte.b1 = ((Packedu32_t)data).byte.b2;
    tmp.byte.b2 = ((Packedu32_t)data).byte.b1;
    tmp.byte.b3 = ((Packedu32_t)data).byte.b0;
    return tmp.dword;
}

tcg_code inline u64 swap_u64(u64 data)
{
    Packedu64_t tmp;

    tmp.byte.b0 = ((Packedu64_t)data).byte.b7;
    tmp.byte.b1 = ((Packedu64_t)data).byte.b6;
    tmp.byte.b2 = ((Packedu64_t)data).byte.b5;
    tmp.byte.b3 = ((Packedu64_t)data).byte.b4;
    tmp.byte.b4 = ((Packedu64_t)data).byte.b3;
    tmp.byte.b5 = ((Packedu64_t)data).byte.b2;
    tmp.byte.b6 = ((Packedu64_t)data).byte.b1;
    tmp.byte.b7 = ((Packedu64_t)data).byte.b0;
    return tmp.qword;
}

// alexcheck
extern struct nvmet_ctrlr *ctrlr;
extern void   *tcgTmpBuf;
static tcg_code inline void tcg_ipc_launch(req_t *req)
{
    if(req->op_fields.TCG.subOpCode == MSG_TCG_INIT_CACHE || req->op_fields.TCG.subOpCode == MSG_TCG_CLR_CACHE){
        req->opcode                 = cMcResetCache;
        req->op_fields.TCG.param[0] = RST_CACHE_INIT;
    }else{
        req->opcode = cMcTcg;
    }
    req->state      = REQ_ST_TO_MIDDLE;

    list_add_tail(&req->inentry, &ctrlr->internal_reqs);

    //TCGPRN("subOP|%8x, pbuf|%08x, p0|%08x, p1|%08x\n",req->op_fields.TCG.subOpCode,
    //    (u32)req->op_fields.TCG.pBuffer, req->op_fields.TCG.param[0], req->op_fields.TCG.param[1]);
    IPC_SendMsgQ(cH2C_IpcQueue, (req_t*) req);
}

static tcg_code inline void tcg_ipc_post(req_t *req, MSG_TCG_SUBOP_t sub_op, bool (*cb_func)(req_t *))
{
    req->completion               = cb_func;  // callback function
    req->op_fields.TCG.subOpCode  = sub_op;
    // req->op_fields.TCG.pBuffer    = tcgTmpBuf;
    tcg_ipc_launch(req);
}

static tcg_code inline void tcg_ipc_post_ex(req_t *req, MSG_TCG_SUBOP_t sub_op, bool (*cb_func)(req_t *), u16 laas, u16 laae, PVOID pbuf)
{
    req->op_fields.TCG.laas     = laas;
    req->op_fields.TCG.laae     = laae;
    req->op_fields.TCG.pBuffer  = pbuf;
    tcg_ipc_post(req, sub_op, cb_func);
}

static tcg_code inline void tcg_ipc_post_ex2(req_t *req, MSG_TCG_SUBOP_t sub_op, bool (*cb_func)(req_t *), u16 laas, u16 laae, PVOID pbuf, u32 argv1, u32 argv2)
{
    req->op_fields.TCG.argv1    = argv1;
    req->op_fields.TCG.argv2    = argv2;
    tcg_ipc_post_ex(req, sub_op, cb_func, laas, laae, pbuf);
}

static tcg_code inline void tcg_ipc_post_ex3(req_t *req, MSG_TCG_SUBOP_t sub_op, bool (*cb_func)(req_t *), u16 laas, u16 laae, PVOID pbuf, u32 argv1, u32 argv2, u32 argv3)
{
    req->op_fields.TCG.argv1    = argv1;
    req->op_fields.TCG.argv2    = argv2;
    req->op_fields.TCG.argv3    = argv3;
    tcg_ipc_post_ex(req, sub_op, cb_func, laas, laae, pbuf);
}

static tcg_code inline void tcg_ipc_post_xx(req_t *req, MSG_TCG_SUBOP_t sub_op, bool (*cb_func)(req_t *), u32 parm0)
{
    req->op_fields.TCG.param[0] = parm0;
    tcg_ipc_post(req, sub_op, cb_func);
}

#include <string.h>
#include "SysInfo.h"
extern smart_statistics_t   smart_stat;
extern tSYSINFO_PAYLOAD*    smSysInfo;
static tcg_code inline void issue_block_erase(req_t *req)
{
    list_add_tail(&req->inentry, &ctrlr->internal_reqs);
    req->opcode = cMcVendor;
    req->state = REQ_ST_TO_MIDDLE;
    req->op_fields.vendor.param2      = MAKE_u32(1, cSfnVscPreformat);   // keep EC count
    req->op_fields.vendor.param1      = 0;

    //Clear CPU0 SMART value
    smart_stat.controller_busy_time   = 0;
    smart_stat.power_on_minutes       = 0;
    smart_stat.data_units_read        = 0;
    smart_stat.data_units_written     = 0;
    smart_stat.host_read_commands     = 0;
    smart_stat.host_write_commands    = 0;
    //gNvmeIfMgr.UnaligenCnt          = 0;
    //MEM_CLR(&smTTMgr.tempWarnTimeInfo, sizeof(tWARN_TIME_Info));
    memset(&smTTMgr.tempWarnTimeInfo, 0,sizeof(tWARN_TIME_Info));
    smFioInfo.flushPaaCnt = 0;
    smFioInfo.slcflushPaaCnt = 0;
    //MEM_CLR(&smSysInfo->d.LogInfo.d.SmrLog, sizeof(tSMR_LOG));
    //MEM_CLR((u8*)&smSysInfo->d.LogInfo.all[0], sizeof(tSI_PAYLOAD_LOGINFO));     //shall clean all log
    memset((u8*)&smSysInfo->d.LogInfo.all[0], 0,sizeof(tSI_PAYLOAD_LOGINFO));     //shall clean all log

    smSysInfo->d.Header.d.SubHeader.d.LogInfo.d.dwValidTag = SI_TAG_LOGINFO;
    //SI_Synchronize_Externel(SI_AREA_BIT_HEADER | SI_AREA_BIT_LOGINFO, SYSINFO_WRITE, SI_SYNC_BY_SYSINFO);//move to core

    IPC_SendMsgQ(cH2C_IpcQueue ,(req_t *) req);
}

extern tTCG_CMD_STATE    gTcgCmdState;
extern bool is_cb_executed;
extern u16  keep_result;
extern u16  tcg_prepare_respPacket_update(bool);
extern u16  tcg_cmdPkt_abortSession(void);
extern void tcg_prepare_respPacket(void);
extern void nvmet_evt_cmd_done(req_t *req);
static tcg_code inline void method_complete_post(req_t *req, bool addStatus)
{
    u32 result;
    DBG_P(3, 3, 0x750000, 4, addStatus, 4, is_cb_executed);

    if((keep_result != STS_SESSION_ABORT) && (keep_result != STS_STAY_IN_IF_SEND)){
        result = tcg_prepare_respPacket_update(addStatus);
    }else{
        result = keep_result;
    }

    // result = tcg_prepare_respPacket_update(addStatus);
    if(result == STS_STAY_IN_IF_SEND)
    {
        DBG_P(1, 3, 0x820166);  //82 01 66, "!!NG: StreamDecode NG -> Stay in IF-SEND"
        //TCGPRN("Error! StreamDecode NG -> Stay in IF-SEND\n");
        gTcgCmdState = ST_AWAIT_IF_SEND;
    }

    if(result == STS_SESSION_ABORT)
    { //prepare payload for "Close Session"
        DBG_P(1, 3, 0x820167);  //82 01 67, "!!NG: StreamDecode NG -> Abort Session"
        //TCGPRN("Error! StreamDecode NG -> Abort Session\n");
        ResetSessionManager(req);
        tcg_cmdPkt_abortSession();
    }
    else if(result == STS_RESPONSE_OVERFLOW)
    {
        tcg_prepare_respPacket();  //TcgCmdPkt4Response();
        fill_no_data_token_list();

        //add status to reponse buffer and update length
        set_status_code(result, 0, 0);
        tcg_prepare_respPacket_update(true);  // TcgRespPktUpdate();
    }

    gTcgCmdState = ST_AWAIT_IF_RECV;

    req->completion = nvmet_core_cmd_done;
    if(is_cb_executed){   // if FASLE then put DTAG & free MEM at NvmeCmd_security_send_XferDone()
        nvmet_evt_cmd_done(req);
        is_cb_executed = FALSE;
    }
}

void HAL_SEC_InitAesKeyRng(void);
void HAL_SEC_SetAesKey(u8 RangeNo, u32 *key1ValPtr);
void HAL_SEC_SetAesRange(void);

void aes_key_wrap(u32 *d_pKEK, u32 *d_src, u8 spr_idx, u32 *d_dest);
void aes_key_unwrap(u32 *d_pKEK, u32 *d_src, u8 spr_idx, u32 *d_dest);

void HAL_PBKDF2(u32* src, u32 srcLen, u32* salt, u32 saltLen, u32* dest);
void HAL_Gen_Key(u32 * dest, u32 bytelen);
// ]
#endif // Jack Li
