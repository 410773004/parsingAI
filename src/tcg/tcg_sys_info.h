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

typedef struct
{
    U8     SMBRDone;
    U8     DummyZZ;
    U8     RdLocked[SI_LOCKING_RANGE_CNT+1];  // 8+1 ranges
    U8     WrLocked[SI_LOCKING_RANGE_CNT+1];  // 8+1 ranges
} TcgBkDevSlpDef;

typedef struct
{
    U32         G5_Por_PageIdx;
    U32         G5_Por_CellIdx;
    U32         G5_Por_Saved_Tag;
    U32         G4_Por_PageIdx;
    U32         G4_Por_CellIdx;
    U32         G4_Por_Saved_Tag;
} Por_Saved_t;

typedef struct {
    U8          TCG_DEV_TYPE;                           // 0x0000
    U8          TCG_RSV_ZONE[15];                       // 0x0001
    char        TcgDefect_ID[16];                       // 0x0010
    U8          TcgG4NorCellDft[TCG_MBR_CELLS];         // 0x0020   less 0x40 than CA1
    U8          TcgG5NorCellDft[TCG_MBR_CELLS];         // 0x0060   less 0x40 than CA1
    char        TcgErasedCnt_ID[16];                    // 0x00A0
    U16         TcgG4NorBlkErasedCnt[TCG_MBR_CELLS];    // 0x00B0   less 0x80 than CA1
    U16         TcgG5NorBlkErasedCnt[TCG_MBR_CELLS];    // 0x0130   less 0x80 than CA1
    char        Tcg_mID_Text[4];                        // 0x01B0
    U32         mID;                                    // 0x01B4
    char        Tcg_mVer_Text[4];                       // 0x01B8
    U32         mVer;                                   // 0x01BC
    char        Tcg_cID_Text[4];                        // 0x01C0
    U32         cID;                                    // 0x01C4
    char        Tcg_cVer_Text[4];                       // 0x01C8
    U32         cVer;                                   // 0x01CC
    char        Preformat_ID[16];                       // 0x01D0
    U8          cPinPSID[68];                           // 0x01E0
    TcgBkDevSlpDef  TcgBkDevSlpVar;                     // 0x0224, 0x0208
    U32         TCG_NonTCG_Switcher;                    // 0x021C

    U32         TCG_PHY_VALID_BLK_TBL_TAG;              // 0x0224
    U32         TCG_PHY_VALID_BLK_TBL[256];             // 0x0228
    U16         TCGBlockNo_EE[128];                     // 0x0628  //Due to 2K size limit, just save 128 word (256 bytes)
    Por_Saved_t Por_Saved;                              // 0x0728
}tcg_ee2_t;
COMPILE_ASSERT(sizeof(tcg_ee2_t) <= 2048, "Error!!!, tcg_ee2_t over 2K bytes.");

typedef struct
{
    tcg_ee2_t   ee2;
    U8          ee2_rsv[2*KBYTE - sizeof(tcg_ee2_t)];
}tSYSINFO_TCG;

COMPILE_ASSERT(sizeof(tSYSINFO_TCG) <= 2048, "Error!!!, tSYSINFO_TCG too large.");

#pragma pack(1)
typedef struct
{
    U32 tag;
    U16 last_sync_cell_no;
    U16 cell_no;
    U16 last_sync_tbl_idx;
    U16 tbl_idx;
    U16 laa_no[384 - L2P_PAGE_CNT];
} paa2laa_partial_tbl_t;   // 1 + 2 + 381*2

typedef struct
{
    paa2laa_partial_tbl_t g4;
    paa2laa_partial_tbl_t g5;
    U16                   chksum;
} tcg_l2p_tbl_assistor_t;
#pragma pack()

