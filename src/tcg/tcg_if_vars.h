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

#ifndef tcg_if_vars_h
#define tcg_if_vars_h


#if _TCG_ // Jack Li

#include "tcgtbl.h"
#include "tcgcommon.h"


/******************************************************
 * fast(DTCM) variables declare
 ******************************************************/
// extern enabledLockingTable_t mLockingRangeTable[LOCKING_RANGE_CNT+1];
//extern u32     mTcgStatus;                  //TCG status variable for others
extern volatile bool nonGR_set;
extern enabledLockingTable_t pLockingRangeTable[LOCKING_RANGE_CNT+1];
//extern u16          mReadLockedStatus;
//extern u16          mWriteLockedStatus;


/******************************************************
 * normal(DDR) variables declare
 ******************************************************/
extern u32     mRawKeyUpdateList;
extern bool    bKeyChanged;


extern bool tcg_ioCmd_inhibited;
extern tTCG_CMD_STATE   gTcgCmdState;
extern u8 *dataBuf;
extern sCmdPkt mCmdPkt;
extern sSgUser mSgUser;
extern sDataStoreAddr   mDataStoreAddr[DSTBL_MAX_NUM];

extern tper_status_t    tcg_tper_status;

extern tcgMtblChngFlg_t flgs_MChnged;
extern u16     keep_result;
extern u16     method_result;
extern bool    bLockingRangeChanged;   // range start or range length of locking table was changed
extern bool    bTcgKekUpdate;

extern u8      statusCode[3];       //status code list for method encoding (core 3.2.4.2)

extern u32     ploadLen;            //payload length in the subpacket
extern u16     iPload;              //index for payload access
extern u16     iDataBuf;            //index (pointer) for dataBuf
extern u16     rcvCmdPktLen;        //cmdPktLen for IF-RECV

extern u16     tgtComID;            //tgtComID from IF-Send, for MS eDrive requirement...

extern bool    bControlSession;     //InvokingUID should be Session Manager only...

extern UID64   invokingUID, methodUID;                        //InvokingID and MethodID from payload

extern u16     siloIdxCmd;          //HiByte: Index, LoByte: Cmd, this parameter is also used to identify whether the Silo command is processing or not!!

extern u32     mTcgStatus;          //TCG status variable for others

extern bool     warn_boot_restore_done;


// following is for Properties
// H2TP properties data struct configuration
extern char strMaxComPktSz[];
extern char strMaxRespComPktSz[];
extern char strMaxPktSz[];
extern char strMaxIndTknSz[];
extern char strMaxPkt[];
extern char strMaxSubPkt[];
extern char strMaxMtd[];
extern char strMaxSess[];
extern char strAuth[];
extern char strTranLmt[];
extern char strDefSessTmOut[];
extern char strContTkn[];
extern char strSeqNum[];
extern char strAckNak[];
extern char strAsync[];

extern sProperties mHostProperties[HostPropertiesCnt];
extern sProperties mTperProperties[TperPropertiesCnt];


extern sAxsCtrl_TblObj* pAxsCtrlTbl;
extern sColPrty*        pInvColPty;          // ColPty address for the Invoking Table, for "Method_Set()"
extern u8*              pInvokingTbl;        // address for Invoking TcgTbl

extern struct {
    u64   aclUid;
    u8    aceColumns[ACE_COLUMNS_CNT];
} aclBackup[ACCESSCTRL_ACL_CNT];

extern u64     aclUid[LCK_ACCESSCTRL_ACL_CNT], getAclAclUid;  //from AccessControl

extern req_t                  *tcg_req;
extern revertSp_varsMgm_t     revertSp_varsMgm;
extern revert_varsMgm_t       revert_varsMgm;
extern reactivate_varsMgm_t   reactivate_varsMgm;
extern mtdSetSmbr_varsMgm_t   mtdSetSmbr_varsMgm;
extern mtdSetDs_varsMgm_t     mtdSetDs_varsMgm;
extern mtdGetSmbr_varsMgm_t   mtdGetSmbr_varsMgm;
extern mtdGetDs_varsMgm_t     mtdGetDs_varsMgm;


extern u32 bak_AdmCpin_tries[sizeof(G1.b.mAdmCPin_Tbl.val) / sizeof(sCPin_TblObj)];
extern u32 bak_LckCpin_tries[sizeof(G3.b.mLckCPin_Tbl.val) / sizeof(sCPin_TblObj)];
extern u32 bak_mTcgStatus;
extern u32 bak_LckMbrCtrl_done;
extern u32 bak_LckLocking_readLocked[LOCKING_RANGE_CNT + 1];
extern u32 bak_LckLocking_writeLocked[LOCKING_RANGE_CNT + 1];
extern sKeySet bak_RawKey_dek[LOCKING_RANGE_CNT + 1];
extern s32 bak_RawKey_state[LOCKING_RANGE_CNT + 1];

extern u8      mHandleComIDRequest;    //for ProtocolID=2
extern u8      mHandleComIDResponse;


extern u32     WrapKEK[8];        // KEK for current authority


#else

/*************************************************************
 * share variables declare (It should keep data in sleep mode)
 **************************************************************/
extern sRawKey mRawKey_bak[LOCKING_RANGE_CNT + 1];

/******************************************************
 * fast(DTCM) variables declare
 ******************************************************/
// extern enabledLockingTable_t mLockingRangeTable[LOCKING_RANGE_CNT+1];
extern enabledLockingTable_t *pLockingRangeTable;
extern U32     mTcgStatus;                  //TCG status variable for others
extern U16     mReadLockedStatus;           // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
extern U16     mWriteLockedStatus;          // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.
extern bool    SMBR_ioCmdReq;       //Non-TrustCmd read TcgMBR area, 1-> Non-TrustCmd,  0->TrustCmd
// extern U32     bootMode;
extern bool     warn_boot_restore_done;


/******************************************************
 * normal(DDR) variables declare
 ******************************************************/
#if CO_SUPPORT_AES
extern U32     WrapKEK[8];        // KEK for current authority
extern U32     WrapBuf[20];       // AesKey[8], ICV1[2], Xtskey[8], ICV2[2]

extern U32     mRawKeyUpdateList;
extern bool    bKeyChanged;
#endif

#if _TCG_ == TCG_EDRV
extern bool    bEHddLogoTest;
extern U16     mPsidRevertCnt;
#endif

extern U8 dataBuf[TCG_BUF_LEN];                //buffer for IF-RECV data block (packet), temp only...
extern sCmdPkt mCmdPkt;
extern sSgUser mSgUser;
extern sDataStoreAddr       mDataStoreAddr[DSTBL_MAX_NUM];
extern tTCG_CMD_STATE       gTcgCmdState;
extern sessionManager_t         mSessionManager;
extern enabledLockingTable_t *pLockingRangeTable;

extern U32     SendHostSectorCnt;   //bytecnt for NVMe
extern U8      statusCode[3];       //status code list for method encoding (core 3.2.4.2)

extern U32     ploadLen;            //payload length in the subpacket
extern U16     iPload;              //index for payload access
extern U16     iDataBuf;            //index (pointer) for dataBuf
extern U16     rcvCmdPktLen;        //cmdPktLen for IF-RECV

extern U16     tgtComID;            //tgtComID from IF-Send, for MS eDrive requirement...

extern bool    bControlSession;     //InvokingUID should be Session Manager only...

extern UID64   invokingUID, methodUID;                        //InvokingID and MethodID from payload
extern U64     aclUid[LCK_ACCESSCTRL_ACL_CNT], getAclAclUid;  //from AccessControl

#if 0
extern bool    G11MtableChanged;        // group1  Mtable
extern bool    G21MtableChanged;        // group2  Mtable
extern bool    G31MtableChanged;        // group3  Mtable
extern bool    G41MBRChanged;           // group4  MBR
extern bool    G61DataStoreChanged;     // group6  Data Store
#endif
extern tcgMtblChngFlg_t flgs_MChnged;
extern U16     keep_result;
extern U16     method_result;
extern tper_status_t tcg_tper_status;
extern bool    bLockingRangeChanged;   // range start or range length of locking table was changed
extern bool    bTcgKekUpdate;

//bool    Tcg_RdWrProcessing;     //TCG RD/WR NAND processing

extern U32     mPOutLength;            // copied from mAtaCmd.length
extern U32     mPInLength;             // copied from mAtaCmd.length
extern U32     mPCLength;              //P_OUT Payload Content Length (B0~B3)
extern U16     siloIdxCmd;             //HiByte: Index, LoByte: Cmd, this parameter is also used to identify whether the Silo command is processing or not!!
extern U16     SiloComID;
extern U8      b1667Probed;
extern U8      m1667Status;
extern U8      mHandleComIDRequest;    //for ProtocolID=2
extern U8      mHandleComIDResponse;

// extern U8      tcgTmpBuf[1024];

//alexcheck**************
extern bool    bTcgTblErr;
// extern tG1     *pG1;
// extern tG2     *pG2;
// extern tG3     *pG3;
// extern U8      *tcgTmpBuf;
// extern tG4     *pG4;
// extern tG5     *pG5;

extern tcg_sync_t   tcg_sync;

//alexcheck&&&&&&&&&&&&&&

extern sAxsCtrl_TblObj* pAxsCtrlTbl;
extern sColPrty*        pInvColPty;          // ColPty address for the Invoking Table, for "Method_Set()"
extern U8*              pInvokingTbl;        // address for Invoking TcgTbl
extern struct {
    U64   aclUid;
    U8    aceColumns[ACE_COLUMNS_CNT];
} aclBackup[ACCESSCTRL_ACL_CNT];

extern req_t                  *tcg_req;
extern revertSp_varsMgm_t     revertSp_varsMgm;
extern revert_varsMgm_t       revert_varsMgm;
extern reactivate_varsMgm_t   reactivate_varsMgm;
extern mtdSetSmbr_varsMgm_t   mtdSetSmbr_varsMgm;
extern mtdSetDs_varsMgm_t     mtdSetDs_varsMgm;
extern mtdGetSmbr_varsMgm_t   mtdGetSmbr_varsMgm;
extern mtdGetDs_varsMgm_t     mtdGetDs_varsMgm;


// following is for Properties
// H2TP properties data struct configuration
extern char strMaxComPktSz[];
extern char strMaxRespComPktSz[];
extern char strMaxPktSz[];
extern char strMaxIndTknSz[];
extern char strMaxPkt[];
extern char strMaxSubPkt[];
extern char strMaxMtd[];
extern char strMaxSess[];
extern char strAuth[];
extern char strTranLmt[];
extern char strDefSessTmOut[];
extern char strContTkn[];
extern char strSeqNum[];
extern char strAckNak[];
extern char strAsync[];

extern sProperties mHostProperties[HostPropertiesCnt];
extern sProperties mTperProperties[TperPropertiesCnt];
extern const MonCmdTbl_t monTcgFuncCmdList[];

extern U32 bak_AdmCpin_tries[sizeof(G1.b.mAdmCPin_Tbl.val) / sizeof(sCPin_TblObj)];
extern U32 bak_LckCpin_tries[sizeof(G3.b.mLckCPin_Tbl.val) / sizeof(sCPin_TblObj)];
extern U32 bak_mTcgStatus;
extern U32 bak_LckMbrCtrl_done;
extern U32 bak_LckLocking_readLocked[LOCKING_RANGE_CNT + 1];
extern U32 bak_LckLocking_writeLocked[LOCKING_RANGE_CNT + 1];
extern sKeySet bak_RawKey_dek[LOCKING_RANGE_CNT + 1];
extern S32 bak_RawKey_state[LOCKING_RANGE_CNT + 1];
#endif // Jack Li

#endif

