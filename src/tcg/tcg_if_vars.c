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

#include "types.h"
#include "sect.h"

#include "tcgcommon.h"
#include "tcg.h"
//#include "tcg_if_vars.h" 
#include "tcg_sh_vars.h"

fast_data_zi volatile bool nonGR_set;
fast_data_zi  enabledLockingTable_t    pLockingRangeTable[LOCKING_RANGE_CNT + 1];

#if 1 //CO_SUPPORT_AES
ddr_data_ni u32     WrapKEK[8];        // KEK for current authority
ddr_data_ni u32     WrapBuf[20];       // AesKey[8], ICV1[2], Xtskey[8], ICV2[2]

//ddr_data u32     mRawKeyUpdateList;
ddr_data bool    bKeyChanged = mFALSE;
#endif

share_data_ni u8      mHandleComIDRequest;    //for ProtocolID=2
share_data_ni u8      mHandleComIDResponse;


ddr_data bool     warn_boot_restore_done = mFALSE;

ddr_data_ni u8      statusCode[3];       //status code list for method encoding (core 3.2.4.2)

ddr_data_ni sAxsCtrl_TblObj*   pAxsCtrlTbl;
ddr_data_ni sColPrty*          pInvColPty;          // ColPty address for the Invoking Table, for "Method_Set()"
ddr_data_ni u8*                pInvokingTbl;        // address for Invoking TcgTbl

ddr_data_ni struct {
    u64   aclUid;
    u8    aceColumns[ACE_COLUMNS_CNT];
} aclBackup[ACCESSCTRL_ACL_CNT];

#if _TCG_ == TCG_EDRV
ddr_data_ni bool    bEHddLogoTest;
ddr_data_ni u16     mPsidRevertCnt;
#endif

ddr_data_ni req_t                  *tcg_req;
ddr_data_ni revert_varsMgm_t       revert_varsMgm;
//ddr_data revertSp_varsMgm_t     revertSp_varsMgm;
//ddr_data reactivate_varsMgm_t   reactivate_varsMgm;
ddr_data_ni mtdSetSmbr_varsMgm_t   mtdSetSmbr_varsMgm;
ddr_data_ni mtdSetDs_varsMgm_t     mtdSetDs_varsMgm;
ddr_data_ni mtdGetSmbr_varsMgm_t   mtdGetSmbr_varsMgm;
ddr_data_ni mtdGetDs_varsMgm_t     mtdGetDs_varsMgm;


ddr_data_ni ALIGNED(4) u8 *dataBuf;                //buffer for IF-RECV data block (packet), temp only...
ddr_data_ni ALIGNED(4) sCmdPkt mCmdPkt;
ddr_data_ni ALIGNED(4) sDataStoreAddr    mDataStoreAddr[DSTBL_MAX_NUM];
ddr_data_ni ALIGNED(4) sSgUser mSgUser;
ddr_data_ni ALIGNED(4) tTCG_CMD_STATE      gTcgCmdState;


ddr_data_ni u32     ploadLen;            //payload length in the subpacket
ddr_data_ni u16     iPload;              //index for payload access
ddr_data_ni u16     iDataBuf;            //index (pointer) for dataBuf
ddr_data_ni u16     rcvCmdPktLen;        //cmdPktLen for IF-RECV

ddr_data u16 tgtComID = 0x0000;   //tgtComID from IF-Send, for MS eDrive requirement...

ddr_data_ni bool    bControlSession;     //InvokingUID should be Session Manager only...

ddr_data_ni ALIGNED(8) UID64   invokingUID, methodUID;                        //InvokingID and MethodID from payload
ddr_data_ni ALIGNED(8) u64     aclUid[LCK_ACCESSCTRL_ACL_CNT], getAclAclUid;  //from AccessControl


ddr_data bool tcg_ioCmd_inhibited = mFALSE;
ddr_data_ni tper_status_t tcg_tper_status;

ddr_sh_data tcgMtblChngFlg_t flgs_MChnged = {0};
ddr_data_ni u16     keep_result;
ddr_data_ni u16     method_result;
ddr_data_ni bool    bLockingRangeChanged;   // range start or range length of locking table was changed
ddr_data_ni bool    bTcgKekUpdate;


share_data_ni u16     siloIdxCmd;             //HiByte: Index, LoByte: Cmd, this parameter is also used to identify whether the Silo command is processing or not!!


// following is for Properties
// H2TP properties data struct configuration
ddr_data char strMaxComPktSz[]=      "MaxComPacketSize";
ddr_data char strMaxRespComPktSz[]=  "MaxResponseComPacketSize";
ddr_data char strMaxPktSz[]=         "MaxPacketSize";
ddr_data char strMaxIndTknSz[]=      "MaxIndTokenSize";
ddr_data char strMaxPkt[]=           "MaxPackets";
ddr_data char strMaxSubPkt[]=        "MaxSubpackets";
ddr_data char strMaxMtd[]=           "MaxMethods";
ddr_data char strMaxSess[]=          "MaxSessions";
ddr_data char strAuth[]=             "MaxAuthentications";
ddr_data char strTranLmt[]=          "MaxTransactionLimit";
ddr_data char strDefSessTmOut[]=     "DefSessionTimeout";
ddr_data char strMaxSessTmOut[] =    "MaxSessionTimeout";
ddr_data char strMinSessTmOut[] =    "MinSessionTimeout";
ddr_data char strContTkn[]=          "ContinuedTokens";
ddr_data char strSeqNum[]=           "SequenceNumbers";
ddr_data char strAckNak[]=           "AckNak";
ddr_data char strAsync[]=            "Asynchronous";

ddr_data ALIGNED(4) sProperties mHostProperties[HostPropertiesCnt] =
{
    {strMaxComPktSz,    HOST_MAX_COMPKT_SZ  },  //MaxComPacketSize
    {strMaxPktSz,       HOST_MAX_PKT_SZ     },  //MaxPacketSize
    {strMaxIndTknSz,    HOST_MAX_INDTKN_SZ  },  //MaxIndTokenSize
    {strMaxPkt,         1                   },  //MaxPackets
    {strMaxSubPkt,      1                   },  //MaxSubpackets
    {strMaxMtd,         1                   },  //MaxMethods
};

ddr_data ALIGNED(4) sProperties mTperProperties[TperPropertiesCnt]=
{
    {strMaxComPktSz,     MAX_COMPKT_SZ      },   //MaxComPacketSize
    {strMaxRespComPktSz, MAX_COMPKT_SZ      },
    {strMaxPktSz,        MAX_PKT_SZ         },   //MaxPacketSize
    {strMaxIndTknSz,     MAX_INDTKN_SZ      },   //MaxIndTokenSize
    {strMaxPkt,          1                  },   //MaxPackets
    {strMaxSubPkt,       1                  },   //MaxSubpackets
    {strMaxMtd,          1                  },   //MaxMethods
    {strContTkn,         0                  },
    {strSeqNum,          0                  },
    {strAckNak,          0                  },
    {strAsync,           0                  },
    {strMaxSess,         1                  },
    {strAuth,            (1+AUTHORITY_CNT)  },
    {strTranLmt,         1                  },
    { strDefSessTmOut,   DEF_SESSION_TIMEOUT },
    { strMaxSessTmOut,   MAX_SESSION_TIMEOUT },
    { strMinSessTmOut,   MIN_SESSION_TIMEOUT },
};

ddr_data_ni u32 bak_AdmCpin_tries[sizeof(G1.b.mAdmCPin_Tbl.val) / sizeof(sCPin_TblObj)];
ddr_data_ni u32 bak_LckCpin_tries[sizeof(G3.b.mLckCPin_Tbl.val) / sizeof(sCPin_TblObj)];
ddr_data_ni u32 bak_mTcgStatus;
ddr_data_ni u32 bak_LckMbrCtrl_done;
ddr_data_ni u32 bak_LckLocking_readLocked[LOCKING_RANGE_CNT + 1];
ddr_data_ni u32 bak_LckLocking_writeLocked[LOCKING_RANGE_CNT + 1];
ddr_data_ni sKeySet bak_RawKey_dek[LOCKING_RANGE_CNT + 1];
ddr_data_ni s32 bak_RawKey_state[LOCKING_RANGE_CNT + 1];


ddr_data_ni revertSp_varsMgm_t     revertSp_varsMgm;
ddr_data_ni reactivate_varsMgm_t   reactivate_varsMgm;


//---------------------------------------------------------------------------------//


#else


#include "sect.h"
#include "ipc.h"
#include "customer.h"
#include "FeaturesDef.h"
#include "nvme_spec.h"
#include "nvmet.h"
#include "MemAlloc.h"
#include "SharedVars.h"
#include "ErrorCodes.h"
#include "Monitor.h"
#include "tcgcommon.h"
#include "SysInfo.h"
#include "tcgtbl.h"
#include "tcg.h"

/*************************************************************
 * share variables declare (It should keep data in sleep mode)
 **************************************************************/
// sh_tcm_data ALIGNED(4) sRawKey mRawKey_bak[LOCKING_RANGE_CNT + 1];  // move to ShareVars.c

/******************************************************
 * fast(DTCM) variables declare
 ******************************************************/
// tcm_data ALIGNED(4) enabledLockingTable_t mLockingRangeTable[LOCKING_RANGE_CNT + 1];
// tcm_data u32     mTcgStatus;                     //TCG status variable for others
// tcm_data u16     mReadLockedStatus;              // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
// tcm_data u16     mWriteLockedStatus;             // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.
// tcm_data u32     bootMode = cInitBootCold;
ddr_data bool     warn_boot_restore_done = FALSE;
// tcm_data bool    SMBR_ioCmdReq = false;         //Non-TrustCmd read TcgMBR area, 1-> Non-TrustCmd,  0->TrustCmd

/******************************************************
 * normal(DDR) variables declare
 ******************************************************/
#if CO_SUPPORT_AES
ddr_data_ni u32     WrapKEK[8];        // KEK for current authority
ddr_data_ni u32     WrapBuf[20];       // AesKey[8], ICV1[2], Xtskey[8], ICV2[2]

ddr_data_ni u32     mRawKeyUpdateList;
ddr_data bool    bKeyChanged = false;
#endif

#if _TCG_ == TCG_EDRV
ddr_data_ni bool    bEHddLogoTest;
ddr_data_ni u16     mPsidRevertCnt;
#endif

ddr_data_ni ALIGNED(4) u8 dataBuf[TCG_BUF_LEN];                //buffer for IF-RECV data block (packet), temp only...
ddr_data_ni ALIGNED(4) sCmdPkt mCmdPkt;
ddr_data_ni ALIGNED(4) sDataStoreAddr    mDataStoreAddr[DSTBL_MAX_NUM];
ddr_data_ni ALIGNED(4) sSgUser mSgUser;
ddr_data_ni ALIGNED(4) tTCG_CMD_STATE      gTcgCmdState;

// ddr_data ALIGNED(4) sessionManager_t     mSessionManager;


ddr_data_ni u32     SendHostSectorCnt;   //bytecnt for NVMe
ddr_data_ni u8      statusCode[3];       //status code list for method encoding (core 3.2.4.2)

ddr_data_ni u32     ploadLen;            //payload length in the subpacket
ddr_data_ni u16     iPload;              //index for payload access
ddr_data_ni u16     iDataBuf;            //index (pointer) for dataBuf
ddr_data_ni u16     rcvCmdPktLen;        //cmdPktLen for IF-RECV

ddr_data u16     tgtComID = 0x0000;   //tgtComID from IF-Send, for MS eDrive requirement...

ddr_data_ni bool    bControlSession;     //InvokingUID should be Session Manager only...

ddr_data_ni ALIGNED(8) UID64   invokingUID, methodUID;                        //InvokingID and MethodID from payload
ddr_data_ni ALIGNED(8) U64     aclUid[LCK_ACCESSCTRL_ACL_CNT], getAclAclUid;  //from AccessControl

#if 0
tcg_data bool    G11MtableChanged;        // group1  Mtable
tcg_data bool    G21MtableChanged;        // group2  Mtable
tcg_data bool    G31MtableChanged;        // group3  Mtable
tcg_data bool    G41MBRChanged;           // group4  MBR
tcg_data bool    G61DataStoreChanged;     // group6  Data Store
#endif
ddr_sh_data tcg_Mtbl_chg_flag_t flgs_MChnged = {0};
ddr_data_ni u16     keep_result;
ddr_data_ni u16     method_result;
ddr_data_ni tper_status_t tcg_tper_status;
ddr_data_ni bool    bLockingRangeChanged;   // range start or range length of locking table was changed
ddr_data_ni bool    bTcgKekUpdate;

//bool    Tcg_RdWrProcessing;     //TCG RD/WR NAND processing
ddr_data_ni u32     mPOutLength;            // copied from mAtaCmd.length
ddr_data_ni u32     mPInLength;             // copied from mAtaCmd.length
ddr_data_ni u32     mPCLength;              //P_OUT Payload Content Length (B0~B3)
ddr_data_ni u16     siloIdxCmd;             //HiByte: Index, LoByte: Cmd, this parameter is also used to identify whether the Silo command is processing or not!!
ddr_data_ni u16     SiloComID;
ddr_data_ni u8      b1667Probed;
ddr_data_ni u8      m1667Status;
ddr_data_ni u8      mHandleComIDRequest;    //for ProtocolID=2
ddr_data_ni u8      mHandleComIDResponse;

ddr_data_ni sAxsCtrl_TblObj*   pAxsCtrlTbl;
ddr_data_ni sColPrty*          pInvColPty;          // ColPty address for the Invoking Table, for "Method_Set()"
ddr_data_ni u8*                pInvokingTbl;        // address for Invoking TcgTbl
ddr_data_ni struct {
    U64   aclUid;
    u8    aceColumns[ACE_COLUMNS_CNT];
} aclBackup[ACCESSCTRL_ACL_CNT];

ddr_data_ni req_t                  *tcg_req;
ddr_data_ni revert_varsMgm_t       revert_varsMgm;
ddr_data_ni revertSp_varsMgm_t     revertSp_varsMgm;
ddr_data_ni reactivate_varsMgm_t   reactivate_varsMgm;
ddr_data_ni mtdSetSmbr_varsMgm_t   mtdSetSmbr_varsMgm;
ddr_data_ni mtdSetDs_varsMgm_t     mtdSetDs_varsMgm;
ddr_data_ni mtdGetSmbr_varsMgm_t   mtdGetSmbr_varsMgm;
ddr_data_ni mtdGetDs_varsMgm_t     mtdGetDs_varsMgm;

// following is for Properties
// H2TP properties data struct configuration
ddr_data char strMaxComPktSz[]=      "MaxComPacketSize";
ddr_data char strMaxRespComPktSz[]=  "MaxResponseComPacketSize";
ddr_data char strMaxPktSz[]=         "MaxPacketSize";
ddr_data char strMaxIndTknSz[]=      "MaxIndTokenSize";
ddr_data char strMaxPkt[]=           "MaxPackets";
ddr_data char strMaxSubPkt[]=        "MaxSubpackets";
ddr_data char strMaxMtd[]=           "MaxMethods";
ddr_data char strMaxSess[]=          "MaxSessions";
ddr_data char strAuth[]=             "MaxAuthentications";
ddr_data char strTranLmt[]=          "MaxTransactionLimit";
ddr_data char strDefSessTmOut[]=     "DefSessionTimeout";
ddr_data char strMaxSessTmOut[] =    "MaxSessionTimeout";
ddr_data char strMinSessTmOut[] =    "MinSessionTimeout";
ddr_data char strContTkn[]=          "ContinuedTokens";
ddr_data char strSeqNum[]=           "SequenceNumbers";
ddr_data char strAckNak[]=           "AckNak";
ddr_data char strAsync[]=            "Asynchronous";

ddr_data ALIGNED(4) sProperties mHostProperties[HostPropertiesCnt] =
{
    {strMaxComPktSz,    HOST_MAX_COMPKT_SZ  },  //MaxComPacketSize
    {strMaxPktSz,       HOST_MAX_PKT_SZ     },  //MaxPacketSize
    {strMaxIndTknSz,    HOST_MAX_INDTKN_SZ  },  //MaxIndTokenSize
    {strMaxPkt,         1                   },  //MaxPackets
    {strMaxSubPkt,      1                   },  //MaxSubpackets
    {strMaxMtd,         1                   },  //MaxMethods
};

ddr_data ALIGNED(4) sProperties mTperProperties[TperPropertiesCnt]=
{
    {strMaxComPktSz,     MAX_COMPKT_SZ      },   //MaxComPacketSize
    {strMaxRespComPktSz, MAX_RESP_COMPKT_SZ },
    {strMaxPktSz,        MAX_PKT_SZ         },   //MaxPacketSize
    {strMaxIndTknSz,     MAX_INDTKN_SZ      },   //MaxIndTokenSize
    {strMaxPkt,          1                  },   //MaxPackets
    {strMaxSubPkt,       1                  },   //MaxSubpackets
    {strMaxMtd,          1                  },   //MaxMethods
    {strContTkn,         0                  },
    {strSeqNum,          0                  },
    {strAckNak,          0                  },
    {strAsync,           0                  },
    {strMaxSess,         1                  },
    {strAuth,            2                  },
    {strTranLmt,         1                  },
    { strDefSessTmOut,   DEF_SESSION_TIMEOUT },
    { strMaxSessTmOut,   MAX_SESSION_TIMEOUT },
    { strMinSessTmOut,   MIN_SESSION_TIMEOUT },
};

ddr_data const MonCmdTbl_t monTcgFuncCmdList[] =
{
//............................................................................................................
//   cmd string,  help string,                               min-argc, auto-dec, exe_time,     handeler
//............................................................................................................
    { "TCGDE",    "0.Show Era 1.Clr Era/Def",                 1,       ON,      OFF,     CmdTcg_OpEraDefTbl   },
    { "TCGIF",    "0.TcgInfo 1 ...",                          1,       ON,      OFF,     CmdTcg_OpTcgInfo     },
    { "TCGDV",  "0.DmpEEVidBlk 1.EraEEVidBlk 2.DmpBufVidBlk", 1,       ON,      OFF,     CmdTcg_OpTcgDmpVidBlk},
    { "TCGSS", "0.current ... ",                              1,       ON,      OFF,     CmdTcg_SwitcherSetting},
    { "",         "",                                         0,      OFF,      OFF,     NULL                 }    // EOT
};

// for PS4 backup using
ddr_data_ni u32 bak_AdmCpin_tries[sizeof(G1.b.mAdmCPin_Tbl.val) / sizeof(sCPin_TblObj)];
ddr_data_ni u32 bak_LckCpin_tries[sizeof(G3.b.mLckCPin_Tbl.val) / sizeof(sCPin_TblObj)];
ddr_data_ni u32 bak_mTcgStatus;
ddr_data_ni u32 bak_LckMbrCtrl_done;
ddr_data_ni u32 bak_LckLocking_readLocked[LOCKING_RANGE_CNT + 1];
ddr_data_ni u32 bak_LckLocking_writeLocked[LOCKING_RANGE_CNT + 1];
ddr_data_ni sKeySet bak_RawKey_dek[LOCKING_RANGE_CNT + 1];
ddr_data_ni s32 bak_RawKey_state[LOCKING_RANGE_CNT + 1];




#endif // Jack Li
