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
#if _TCG_

extern tG1          *pG1;
extern tG2          *pG2;
extern tG3          *pG3;

extern sessionManager_t mSessionManager;
extern sh_secure_boot_info_t sh_secure_boot_info;

extern sRawKey      mRawKey[];

extern bool         bTcgTblErr;
extern bool         SMBR_ioCmdReq;
extern InitBootMode_t bootMode;

extern tcg_sync_t   tcg_sync;
extern u32          mTcgStatus;
//extern enabledLockingTable_t  pLockingRangeTable[9];
extern enabledLockingTable_t* globla_pLockingRangeTable;
extern u16          mReadLockedStatus;    // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
extern u16          mWriteLockedStatus;    // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.

extern u32 tcg_df_map[8];
extern void *tcgTmpBuf;

extern void *tcgReactivate_buf;


#else

extern sRawKey      mRawKey[];
extern void         *tcgTmpBuf;
extern U32          tcgTmpBufCnt;
extern U32          tcgTblkeyBuf[8];
extern U32          nf_cpu_sync_post_sign;
extern U32          if_cpu_sync_post_sign;
extern bool         TcgSuBlkExist;
extern bool         bTcgTblErr;
extern bool         SMBR_ioCmdReq;
extern tPAA         *DR_G4PaaBuf;
extern tPAA         *DR_G5PaaBuf;
extern tcgLaa_t     *pMBR_TEMPL2PTBL;
extern tcgLaa_t     *pDS_TEMPL2PTBL;
extern U32          gTcgG4Defects;
extern U32          gTcgG5Defects;
extern tG1          *pG1;
extern tG2          *pG2;
extern tG3          *pG3;
extern sessionManager_t     mSessionManager;
extern sh_secure_boot_info_t sh_secure_boot_info;
extern InitBootMode_t bootMode;
// extern tcg_sh tcgLaa_t      *MBR_TEMPL2PTBL;
// extern tcg_sh tcgLaa_t      *DS_TEMPL2PTBL;

#endif // Jack Li
