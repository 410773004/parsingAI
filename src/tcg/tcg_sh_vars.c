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
#include "tcgtbl.h"

slow_data_zi void* tcgReactivate_buf = NULL;

#ifdef TCG_NAND_BACKUP
#include "tcg_nf_mid.h"

share_data_zi volatile u8 isTcgNfBusy;
share_data_zi void *tcgTmpBuf;
share_data_zi void *tcgTmpBuf_gc;
share_data_zi tcg_mid_info_t* tcg_mid_info;

#endif

slow_data_zi ALIGNED(4) sessionManager_t mSessionManager;

ddr_sh_data tG1          *pG1 = NULL; //(tG1 *)&G1;
ddr_sh_data tG2          *pG2 = NULL; //(tG2 *)&G2;
ddr_sh_data tG3          *pG3 = NULL; //(tG3 *)&G3;
ddr_sh_data tG4          *pG4 = NULL;
ddr_sh_data tG5          *pG5 = NULL;

ddr_sh_data bool         bTcgTblErr    = mFALSE;
ddr_sh_data bool         SMBR_ioCmdReq = mFALSE; 


ddr_sh_data InitBootMode_t bootMode = cInitBootCold;

ddr_sh_data sh_secure_boot_info_t sh_secure_boot_info = {0};

slow_data_zi sRawKey      mRawKey[TCG_MAX_KEY_CNT];     // RawKey Array for AES engine usage, range 0~8

ddr_data tcg_sync_t                  tcg_sync = {0};
share_data_zi u32                     mTcgStatus;           //TCG status variable for others
share_data_zi u32                     mTcgActivated;        //TCG status activaed or not, for IO only
share_data_zi u16                     mReadLockedStatus;    // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
share_data_zi u16                     mWriteLockedStatus;   // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.

share_data_zi enabledLockingTable_t    *globla_pLockingRangeTable;

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
// #include "tcg_sys_info.h"
#include "tcgcommon.h"
#include "SysInfo.h"
#include "tcgtbl.h"
#include "tcg.h"

tcg_sh sRawKey      mRawKey[LOCKING_RANGE_CNT + 1];     // RawKey Array for AES engine usage, range 0~8
tcg_sh void         *tcgTmpBuf = NULL;
tcg_sh VU32         tcgTmpBufCnt;
tcg_sh U32          tcgTblkeyBuf[8];
// tcg_sh U32          nf_cpu_sync_post_sign;
// tcg_sh U32          if_cpu_sync_post_sign;
// tcg_sh bool         TcgSuBlkExist;
tcg_sh bool         bTcgTblErr    = FALSE;
tcg_sh bool         SMBR_ioCmdReq = FALSE;              //Non-TrustCmd read TcgMBR area, 1-> Non-TrustCmd,  0->TrustCmd
// tcg_sh tPAA         *DR_G4PaaBuf;
// tcg_sh tPAA         *DR_G5PaaBuf;
// tcg_sh tcgLaa_t     *pMBR_TEMPL2PTBL = NULL;
// tcg_sh tcgLaa_t     *pDS_TEMPL2PTBL  = NULL;
tcg_sh U32          gTcgG4Defects = 0;
tcg_sh U32          gTcgG5Defects = 0;

tcg_sh tG1          *pG1 = NULL;
tcg_sh tG2          *pG2 = NULL;
tcg_sh tG3          *pG3 = NULL;
tcg_sh tG4          *pG4 = NULL;
tcg_sh tG5          *pG5 = NULL;

tcg_sh InitBootMode_t bootMode = cInitBootCold;

ddr_sh_data ALIGNED(4) sessionManager_t mSessionManager;
// tcg_sh ALIGNED(4)   enabledLockingTable_t   *pLockingRangeTable = NULL;
// tcg_sh tcgLaa_t     *MBR_TEMPL2PTBL = NULL;
// tcg_sh tcgLaa_t     *DS_TEMPL2PTBL = NULL;

/*
tcg_sh U8 cbc_image_key[] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};
tcg_sh U8 cbc_image_iv[]  = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

tcg_data U8 cbc_tbl_key[] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};
tcg_data U8 cbc_tbl_iv[]  = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

tcg_data U8 ebc_tbl_key[] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};
*/

tcg_sh SecretZone_t secretZone = {
    /* cbc tbl */
    {
        {0x54535353, 0x55532343, 0x0000004E}, /* tag */
        0x00000001, /* writedCnt */
        {0x10EB3D60, 0xBE71CA15, 0xf0ae732b, 0x81777d85, 0x072c351f, 0xd708613b, 0xa310982d, 0xf4df1409}, /* CBC */
        {0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c}, /* IV */
    },
    /* ebc key */
    {
        {0x54535353, 0x4F4D4043, 0x0000004E}, /* tag */
        0x00000001,  /* writedCnt */
        {0x10EB3D60, 0xBE71CA15, 0xf0ae732b, 0x81777d85, 0x072c351f, 0xd708613b, 0xa310982d, 0xf4df1409}, /* EBC */
    },
    /* cbc image */
    {
        {0x54535353, 0x55542443, 0x00000045}, /* tag */
        0x00000001,  /* writedCnt */
        {0x10EB3D60, 0xBE71CA15, 0xf0ae732b, 0x81777d85, 0x072c351f, 0xd708613b, 0xa310982d, 0xf4df1409}, /* CBC */
        {0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c}, /* IV */
    },
    0x00000000, /* histCnt */
    0x00000000, /* crc */
};

tcg_sh sh_secure_boot_info_t sh_secure_boot_info = {0};

#endif  // Jack Li
