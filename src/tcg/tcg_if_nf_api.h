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

#ifdef TCG_NAND_BACKUP // Jack Li

extern volatile tcg_nf_params_t tcg_nf_params;

u8 tcg_nf_G1RdDefault(void);
u8 tcg_nf_G2RdDefault(void);
u8 tcg_nf_G3RdDefault(void);
u8 tcg_nf_G4RdDefault(void);
u8 tcg_nf_G5RdDefault(void);

u8 tcg_nf_G4WrDefault(void);
u8 tcg_nf_G5WrDefault(void);

u8 tcg_nf_G1Rd(bool G4_only, bool G5_only); // both True is inhibittied
u8 tcg_nf_G2Rd(bool G4_only, bool G5_only); // both True is inhibittied
u8 tcg_nf_G3Rd(bool G4_only, bool G5_only); // both True is inhibittied

u8 tcg_nf_G1Wr(bool G5_only);
u8 tcg_nf_G2Wr(bool G5_only);
u8 tcg_nf_G3Wr(bool G5_only);

u8 tcg_nf_SMBRRd(u64 slba, u16 du_cnt, bool from_io);
u8 tcg_nf_SMBRWr(u16 laas, u16 laacnt);
u8 tcg_nf_SMBRCommit(u16 laas, u16 laacnt);
u8 tcg_nf_SMBRAbort(u16 laas, u16 laacnt);

u8 tcg_nf_DSRd(u16 laas, u16 laacnt);
u8 tcg_nf_DSWr(u16 laas, u16 laacnt);
u8 tcg_nf_DSCommit(u16 laas, u16 laacnt);
u8 tcg_nf_DSAbort(u16 laas, u16 laacnt);

u8 tcg_nf_allErase(u8 sync);

#endif // Jack Li
