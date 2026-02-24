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

#include "sect.h"
#include "types.h"
#include "dtag.h"
#include "tcgcommon.h"
#include "tcgtbl.h"



/// ********************************************************///
///                     Admin Table Init                    ///
/// ********************************************************///
ddr_data ALIGNED(16) tG1 G1= { {

//__align(4) const sTcgTblInfo cTcgTblInfo =
{ TCG_TBL_ID, TCG_G1_TAG + TCG_TBL_VER },

/* AdminSP Tables */

//__align(4) const sSPInfo_Tbl cAdmSPInfo_Tbl =
{
    { // hdr
        sizeof(sSPInfo_Tbl),
        sizeof(G1.b.mAdmSPInfo_Tbl.pty) / sizeof(sColPrty),         // ColCnt
        6,                                                          // maxCol
        sizeof(G1.b.mAdmSPInfo_Tbl.val) / sizeof(sSPInfo_TblObj),   // RowCnt
        sizeof(sSPInfo_TblObj),                                     // TblObj size
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },      // UID
        { 0x01,8,UID_TYPE,{0} },      // SPID
        { 0x02,sizeof(G1.b.mAdmSPInfo_Tbl.val[0].name),STRING_TYPE,{0} },   // Name, cannot use "sizeof(sSPInfo_TblObj.name)"
        { 0x05,sizeof(G1.b.mAdmSPInfo_Tbl.val[0].spSessionTimeout),VALUE_TYPE,{0}},
        { 0x06,sizeof(G1.b.mAdmSPInfo_Tbl.val[0].enabled),VALUE_TYPE,{0} }  // Enabled (bool)
    },
    { // val
        {UID_SPInfo, UID_SP_Admin, "Admin", 0, true}
    }
},

//__align(4) const sSPTemplates_Tbl cAdmSPTemplates_Tbl =
{
    { // hdr
        sizeof(sSPTemplates_Tbl),
        sizeof(G1.b.mAdmSPTemplates_Tbl.pty) / sizeof(sColPrty),                    // colCnt
        3,                                                                          // maxCol
        sizeof(G1.b.mAdmSPTemplates_Tbl.val) / sizeof(sSPTemplates_TblObj),         // rowCnt
        sizeof(sSPTemplates_TblObj),                                                // objSize
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },     //UID
        { 0x01,8,UID_TYPE,{0} },     //TemplateID
        { 0x02,sizeof(G1.b.mAdmSPTemplates_Tbl.val[0].name),STRING_TYPE,{0} },    // Name
        { 0x03,sizeof(G1.b.mAdmSPTemplates_Tbl.val[0].version),FBYTE_TYPE,{0} }   // Enabled
    },
    { // val
        {UID_SPTemplate_1, UID_Template_Base,  "Base",  {0x00,0x00,0x00,0x02}},
        {UID_SPTemplate_2, UID_Template_Admin, "Admin", {0x00,0x00,0x00,0x02}}
    }
},

//__align(4) const sTbl_Tbl cAdmTbl_Tbl =
{
    { // hdr
        sizeof(sTbl_Tbl),
        sizeof(G1.b.mAdmTbl_Tbl.pty) / sizeof(sColPrty),                        // colCnt
        0x0e,                                                                   // maxCol
        sizeof(G1.b.mAdmTbl_Tbl.val) / sizeof(sTbl_TblObj),                     // rowCnt
        sizeof(sTbl_TblObj),                                                    // objSize
    },
    { // pty
        { 0x00, 8,                                            UID_TYPE,{0} },     // UID
        { 0x01, sizeof(G1.b.mAdmTbl_Tbl.val[0].name),         STRING_TYPE,{0} },  // Name
        { 0x04, sizeof(G1.b.mAdmTbl_Tbl.val[0].kind),         VALUE_TYPE,{0} },   // Kind (Object or Byte)
        { 0x0D, sizeof(G1.b.mAdmTbl_Tbl.val[0].mGranularity), VALUE_TYPE,{0} },   // MaxSize
        { 0x0E, sizeof(G1.b.mAdmTbl_Tbl.val[0].rGranularity), VALUE_TYPE,{0} }    // MaxSize
    },
    { // val
        {UID_Table_Table,           "Table",            TBL_K_OBJECT, 0x00, 0x00},  // R1: Table
        {UID_Table_SPInfo,          "SPInfo",           TBL_K_OBJECT, 0x00, 0x00},  // R2: SPInfo
        {UID_Table_SPTemplates,     "SPTemplates",      TBL_K_OBJECT, 0x00, 0x00},  // R3:
        {UID_Table_MethodID,        "MethodID",         TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_AccessControl,   "AccessControl",    TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_ACE,             "ACE",              TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_Authority,       "Authority",        TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_CPIN,            "C_Pin",            TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_TPerInfo,        "TPerInfo",         TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_Template,        "Template",         TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_SP,              "SP",               TBL_K_OBJECT, 0x00, 0x00},
#if (_TCG_ == TCG_PYRITE)
        {UID_Table_RemovalMechanism, "DataRemovalMechanism", TBL_K_OBJECT, 0x00, 0x00},
#endif
    },
},

//__align(4) const sMethod_Tbl cAdmMethod_Tbl =
{
    { // hdr
        sizeof(sMethod_Tbl),
        sizeof(G1.b.mAdmMethod_Tbl.pty) / sizeof(sColPrty),                 // colCnt
        3,                                                                  // maxCol
        sizeof(G1.b.mAdmMethod_Tbl.val) / sizeof(sMethod_TblObj),           // rowCnt
        sizeof(sMethod_TblObj),                                             // objSize
    },
    { // pty
        { 0x00, 8,                                       UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmMethod_Tbl.val[0].name), STRING_TYPE,{0} }    // Kind (Object or Byte)
    },
    { // val
        {UID_MethodID_Next,         "Next"},            // R1:
        {UID_MethodID_GetACL,       "GetACL"},
        {UID_MethodID_Get,          "Get"},
        {UID_MethodID_Set,          "Set"},
        {UID_MethodID_Authenticate, "Authenticate"},
        {UID_MethodID_Revert,       "Revert"},
        {UID_MethodID_Activate,     "Activate"},
        {UID_MethodID_Random,       "Random"}
    }
},

//__align(4) const sAccessCtrl_Tbl cAdmAccessCtrl_Tbl =
{
    { // ThisSP: + 2 rows
        {UID_ThisSP,    UID_MethodID_Authenticate,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ThisSP,    UID_MethodID_Random,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    { // Table: 12 rows
        {UID_Table,     UID_MethodID_Next,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R1: Table.Next
        {UID_Table_Table, UID_MethodID_Get,         {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R2: TableObj.Get
        {UID_Table_SPInfo, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R3: TableObj.Get
        {UID_Table_SPTemplates, UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R4: TableObj.Get
        {UID_Table_MethodID, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R5: TableObj.Get
        {UID_Table_AccessControl, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R6: TableObj.Get
        {UID_Table_ACE, UID_MethodID_Get,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R7: TableObj.Get
        {UID_Table_Authority, UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R8: TableObj.Get
        {UID_Table_CPIN, UID_MethodID_Get,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R9: TableObj.Get
        {UID_Table_TPerInfo, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R10: TableObj.Get
        {UID_Table_Template, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R11: TableObj.Get
        {UID_Table_SP, UID_MethodID_Get,            {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R12: TableObj.Get
    #if (_TCG_ == TCG_PYRITE)
        {UID_Table_RemovalMechanism, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // SPInfo: 1 row
        {UID_SPInfo,    UID_MethodID_Get,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    { // SPTemplates: 3 row
        {UID_SPTemplate,  UID_MethodID_Next,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_1, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_2, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    { // MethodID: 9 rows
        {UID_MethodID, UID_MethodID_Next,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},    // R1: Table.Next
        {UID_MethodID_Next, UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_GetACL,UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Get, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Set, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Authenticate,UID_MethodID_Get,{UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Revert, UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Activate,UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Random,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    {   // ACE: 10 rows + 2 rows
        {UID_ACE, UID_MethodID_Next,                {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_Anybody, UID_MethodID_Get,         {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_Admin, UID_MethodID_Get,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_Set_Enabled, UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_SID_Get_NOPIN, UID_MethodID_Get,{UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_SID_Set_PIN, UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_MSID_Get_PIN, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_Admins_Set_PIN,UID_MethodID_Get,{UID_ACE_Anybody,0,0},UID_ACE_Anybody},
        {UID_ACE_TPerInfo_Set_PReset,UID_MethodID_Get,{UID_ACE_Anybody,0,0},UID_ACE_Anybody},
        {UID_ACE_SP_SID, UID_MethodID_Get,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #if TCG_FS_PSID
        {UID_ACE_CPIN_Get_PSID_NoPIN, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_SP_PSID,       UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #endif
    #if (_TCG_ == TCG_PYRITE)
        {UID_ACE_RMMech_Set_RM, UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #endif
    },
    {   // Authority: 6 rows + 2 rows
        {UID_Authority, UID_MethodID_Next,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_Anybody,UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_Admins, UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Makers, UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_Makers, UID_MethodID_Set,    {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
    #endif
        {UID_Authority_SID,     UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_SID,     UID_MethodID_Set,   {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin1,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin1,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin2,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin2,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_AdmAdmin3,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin3,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin4,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin4,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
    #endif
    #if TCG_FS_PSID
        {UID_Authority_PSID,    UID_MethodID_Get,   {UID_ACE_Anybody,0,0},            UID_ACE_Anybody},
    #endif
    },
    {   // CPIN: 4 rows + 2 rows
        {UID_CPIN,              UID_MethodID_Next,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},             // R1: CPIN.Next
        {UID_CPIN_SID,          UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},  // R2: CPIN_SID.Get
        {UID_CPIN_SID,          UID_MethodID_Set,   {UID_ACE_CPIN_SID_Set_PIN,0,0}, UID_ACE_Anybody},    // R3: CPIN_SID.Set
        {UID_CPIN_MSID,         UID_MethodID_Get,   {UID_ACE_CPIN_MSID_Get_PIN,0,0}, UID_ACE_Anybody},   // R4: CPIN_MSID.Get
        {UID_CPIN_AdmAdmin1,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin1,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin2,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin2,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_CPIN_AdmAdmin3,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin3,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin4,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin4,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
    #endif
    #if TCG_FS_PSID
        {UID_CPIN_PSID,         UID_MethodID_Get,   {UID_ACE_CPIN_Get_PSID_NoPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_PSID,         UID_MethodID_Set,   {UID_ACE_CPIN_SID_Set_PIN,0,0},   UID_ACE_Anybody},
    #endif
    },
    {   // TPerInfo: 1 row + 1 row
        {UID_TPerInfo,      UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_TPerInfo,      UID_MethodID_Set,       {UID_ACE_TPerInfo_Set_PReset,0,0}, UID_ACE_Anybody},
    },
    {   // Template: 4 rows
        {UID_Template,      UID_MethodID_Next,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Template_Base, UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Template_Admin, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Template_Locking,UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    {   // SP: 7 rows
        {UID_SP,            UID_MethodID_Next,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SP_Admin,      UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SP_Locking,    UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SP_Admin,      UID_MethodID_Revert,    {UID_ACE_SP_SID,UID_ACE_Admin,UID_ACE_SP_PSID},  UID_ACE_Anybody},
        {UID_SP_Locking,    UID_MethodID_Revert,    {UID_ACE_SP_SID,UID_ACE_Admin,0},  UID_ACE_Anybody},
        {UID_SP_Admin,      UID_MethodID_Activate,  {UID_ACE_SP_SID,0,0},  UID_ACE_Anybody},
        {UID_SP_Locking,    UID_MethodID_Activate,  {UID_ACE_SP_SID,0,0}, UID_ACE_Anybody},
    },
    #if (_TCG_ == TCG_PYRITE)
    { // RemovalMechanism:
        {UID_RemovalMechanism, UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_RemovalMechanism, UID_MethodID_Set,    {UID_ACE_RMMech_Set_RM,0,0}, UID_ACE_Anybody},
    },
    #endif
    { // ColPty
        {0x01, 8,                                           UID_TYPE,{0}},      // InvokingID
        {0x02, 8,                                           UID_TYPE,{0}},      // MethodID
        {0x04, sizeof(G1.b.mAdmAxsCtrl_Tbl.thisSP[0].acl),  UIDLIST_TYPE,{0}},  // ACL
        {0x08, 8,                                           UID_TYPE,{0}}       // GetACLACL
    },
    #if _TCG_==TCG_PYRITE
        sizeof(G1.b.mAdmAxsCtrl_Tbl.pty) / sizeof(sColPrty),                 // ColCnt
        (sizeof(G1.b.mAdmAxsCtrl_Tbl.thisSP) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.table) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spInfo) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spTemplate) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.method) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.ace) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.authority) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.cpin) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.tperInfo) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.templateTbl) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.sp) +   \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.removalMsm)) \
        / sizeof(sAxsCtrl_TblObj),       // RowCnt
    #else
        sizeof(G1.b.mAdmAxsCtrl_Tbl.pty) / sizeof(sColPrty),                 // ColCnt
        (sizeof(G1.b.mAdmAxsCtrl_Tbl.thisSP) +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.table)  +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spInfo) +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spTemplate) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.method) +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.ace) +       \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.authority) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.cpin) +      \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.tperInfo) +  \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.templateTbl) +  \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.sp) )        \
        / sizeof(sAxsCtrl_TblObj),       // RowCnt
    #endif
        sizeof(sAxsCtrl_TblObj),
        0x0e
},

//__align(4) const sACE_Tbl cAdmACE_Tbl =
{
    { // hdr
        sizeof(sACE_Tbl),
        sizeof(G1.b.mAdmACE_Tbl.pty) / sizeof(sColPrty),                        // colCnt
        0x04,                                                                   // maxCol
        sizeof(G1.b.mAdmACE_Tbl.val) / sizeof(sACE_TblObj),                     // RowCnt
        sizeof(sACE_TblObj),
    },
    { // pty
        { 0x00, 8,                                           UID_TYPE,{0} },      // UID
        { 0x03, sizeof(G1.b.mAdmACE_Tbl.val[0].booleanExpr), UIDLIST_TYPE,{0} },  // BooleanExpr
        { 0x04, sizeof(G1.b.mAdmACE_Tbl.val[0].col),         LIST_TYPE,{0} }      // Columns
    },
    {
        {UID_ACE_Anybody,           {UID_Authority_Anybody,0},   {0,}},    // ACE_Anybody, col(All)
        {UID_ACE_Admin,             {UID_Authority_Admins,0},    {0,}},    // ACE_Admin, col(All)
        {UID_ACE_Set_Enabled,       {UID_Authority_SID,0},       {1,5,}},    // ACE_Makers..., b5->col5 (col(Enabled))
        {UID_ACE_CPIN_SID_Get_NOPIN,{UID_Authority_Admins,UID_Authority_SID}, {5,0,4,5,6,7,}}, // ACE_CPIN_SID_Get..., col(UID, CharSet, TryLimit, Tries, Persistence)
        {UID_ACE_CPIN_SID_Set_PIN,  {UID_Authority_SID,0},       {1,3,}},    // ACE_CPIN_SID_Set..., col(PIN)
        {UID_ACE_CPIN_MSID_Get_PIN, {UID_Authority_Anybody,0},   {2,0,3,}},    // ACE_CPIN_MSID_Get..., col(UID,PIN)
        {UID_ACE_CPIN_Admins_Set_PIN,{UID_Authority_Admins,UID_Authority_SID},{1,3,}},
        {UID_ACE_TPerInfo_Set_PReset,{UID_Authority_SID,0},      {1,8,}},
    #if TCG_FS_PSID
        {UID_ACE_CPIN_Get_PSID_NoPIN,{UID_Authority_Anybody,0},  {5,0,4,5,6,7,}},
        {UID_ACE_SP_PSID,           {UID_Authority_PSID,0}, {0,}},
    #endif
        {UID_ACE_SP_SID,            {UID_Authority_SID,0}, {0,}},          // ACE_SP_SID, col(All)
    #if (_TCG_ == TCG_PYRITE)
        {UID_ACE_RMMech_Set_RM,     {UID_Authority_Admins,UID_Authority_SID}, {1,1,}},
    #endif
    // ]
    }
},

//__align(4) const sAuthority_Tbl cAdmAuthority_Tbl =
{
    { // hdr
        sizeof(sAuthority_Tbl),
        sizeof(G1.b.mAdmAuthority_Tbl.pty) / sizeof(sColPrty),                              // colCnt
        0x12,                                                                               // maxCol
        sizeof(G1.b.mAdmAuthority_Tbl.val) / sizeof(sAuthority_TblObj),                     // rowCnt
        sizeof(sAuthority_TblObj),
    },
    { // pty
        { 0x00, sizeof(u64),                                                 UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmAuthority_Tbl.val[0].name),                  STRING_TYPE,{0} },
        { 0x02, sizeof(G1.b.mAdmAuthority_Tbl.val[0].commonName),            STRING_TYPE,{0} },
        { 0x03, sizeof(G1.b.mAdmAuthority_Tbl.val[0].isClass),               VALUE_TYPE,{0} },    // IsClass
        { 0x04, 8,                                                           UID_TYPE,{0} },      // Class
        { 0x05, sizeof(G1.b.mAdmAuthority_Tbl.val[0].enabled),               VALUE_TYPE,{0} },    // Enabled
        { 0x06, sizeof(secure_message),                                      VALUE_TYPE,{0} },    // Secure
        { 0x07, sizeof(hash_protocol),                                       VALUE_TYPE,{0} },    // HashAndSign
        { 0x08, sizeof(G1.b.mAdmAuthority_Tbl.val[0].presentCertificate),    VALUE_TYPE,{0} },    // PresentCertificate (bool)
        { 0x09, sizeof(auth_method),                                         VALUE_TYPE,{0} },    // Operation
        { 0x0A, 8,                                                           UID_TYPE,{0} },      // Credential (UID)
        { 0x0B, 8,                                                           UID_TYPE,{0} },      // ResponseSign
        { 0x0C, 8,                                                           UID_TYPE,{0} }       // ResponseExch
    },
    {
        {UID_Authority_Anybody, "Anybody", "", false,UID_Null, true, SECURE_None, HASH_None, false, AUTH_None,    UID_Null, UID_Null, UID_Null},
        {UID_Authority_Admins,  "Admins",  "", true, UID_Null, true, SECURE_None, HASH_None, false, AUTH_None,    UID_Null, UID_Null, UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Makers,  "Makers",  "", true, UID_Null, true, SECURE_None, HASH_None, false, AUTH_None,    UID_Null, UID_Null, UID_Null},
    #endif
        {UID_Authority_SID,     "SID",     "", false,UID_Null, true, SECURE_None, HASH_None, false, AUTH_Password,UID_CPIN_SID, UID_Null, UID_Null},
        {UID_Authority_AdmAdmin1,"Admin1", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin1,UID_Null,UID_Null},
        {UID_Authority_AdmAdmin2,"Admin2", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin2,UID_Null,UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_AdmAdmin3,"Admin3", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin3,UID_Null,UID_Null},
        {UID_Authority_AdmAdmin4,"Admin4", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin4,UID_Null,UID_Null},
    #endif
    #if TCG_FS_PSID
        {UID_Authority_PSID,    "PSID",    "", false,UID_Null, true, SECURE_None, HASH_None, false, AUTH_Password,UID_CPIN_PSID, UID_Null, UID_Null}
    #endif
    }
},

//__align(4) const sCPin_Tbl cAdmCPin_Tbl =
{
    { // hdr
        sizeof(sCPin_Tbl),
        sizeof(G1.b.mAdmCPin_Tbl.pty) / sizeof(sColPrty),        // colCnt
        0x07,                                                    // maxCol
        sizeof(G1.b.mAdmCPin_Tbl.val) / sizeof(sCPin_TblObj),    // rowCnt
        sizeof(sCPin_TblObj),                                    // objSize
    },
    { // pty
        { 0x00, sizeof(G1.b.mAdmCPin_Tbl.val[0].uid),        UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmCPin_Tbl.val[0].name),       STRING_TYPE,{0} },
        { 0x03, sizeof(G1.b.mAdmCPin_Tbl.val[0].cPin),       VBYTE_TYPE,{0} },    // PIN
        { 0x04, sizeof(G1.b.mAdmCPin_Tbl.val[0].charSet),    UID_TYPE,{0} },      // CharSet
        { 0x05, sizeof(G1.b.mAdmCPin_Tbl.val[0].tryLimit),   VALUE_TYPE,{0} },    // TryLimit     --> set to 50 for OCP spec SEC-37
        { 0x06, sizeof(G1.b.mAdmCPin_Tbl.val[0].tries),      VALUE_TYPE,{0} },    // Tries
        { 0x07, sizeof(G1.b.mAdmCPin_Tbl.val[0].persistence),VALUE_TYPE,{0} }     // Persistence
    },
    { // val
        {   UID_CPIN_SID, "C_PIN_SID", // uid, name
            { CPIN_IN_RAW, { 'm', 'y', '_', 'M', 'S', 'I', 'D', '_', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' }, { 0 }},
              UID_Null, 50, 0, false }, // charSet, tryLimit, tries, persistence

        {   UID_CPIN_MSID, "C_PIN_MSID",
            { CPIN_IN_RAW, { 'm', 'y', '_', 'M', 'S', 'I', 'D', '_', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' }, { 0 }},
              UID_Null, 0, 0, false },

        {   UID_CPIN_AdmAdmin1, "C_PIN_Admin1", { 0, { 0 }, { 0 }}, UID_Null, 50, 0, false },
        {   UID_CPIN_AdmAdmin2, "C_PIN_Admin2", { 0, { 0 }, { 0 }}, UID_Null, 50, 0, false },
    #if _TCG_ != TCG_PYRITE
        {   UID_CPIN_AdmAdmin3, "C_PIN_Admin3", { 0, { 0 }, { 0 }}, UID_Null, 50, 0, false },
        {   UID_CPIN_AdmAdmin4, "C_PIN_Admin4", { 0, { 0 }, { 0 }}, UID_Null, 50, 0, false },
    #endif
    #if TCG_FS_PSID
        {   UID_CPIN_PSID, "C_PIN_PSID",
            { CPIN_IN_RAW, { 'm', 'y', '_', 'M', 'S', 'I', 'D', '_', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' }, { 0 }},
              UID_Null, 50, 0, false },
    #endif
    },
},

//__align(4) const sTPerInfo_Tbl cAdmTPerInfo_Tbl =
{
    { // hdr
        sizeof(sTPerInfo_Tbl),
        sizeof(G1.b.mAdmTPerInfo_Tbl.pty) / sizeof(sColPrty),                               // colCnt
        0x08,                                                                               // maxCol
        sizeof(G1.b.mAdmTPerInfo_Tbl.val) / sizeof(sTPerInfo_TblObj),                       // rowCnt
        sizeof(sTPerInfo_TblObj),                                                           // objSize
    },
    { // ColPty
        {0x00, 8,                                                       UID_TYPE, {0}},     // UID
        {0x04, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].firmwareVersion),    VALUE_TYPE, {0}},   // UID
        {0x05, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].protocolVersion),    VALUE_TYPE, {0}},   // ProtocolVersion
        {0x07, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].ssc),                STRINGLIST_TYPE, {0}}, // SSC
        {0x08, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].preset),             VALUE_TYPE, {0}},   //programmaticResetEnable (if 1 , shall support TperReset cmd)
    },
    { // val
        {UID_TPerInfo, TPER_FW_VERSION, 0x01, SSC_STRING, false}                            // double '\0' for String List ends.
    }
},

//__align(4) const sTemplate_Tbl cAdmTemplate_Tbl =
{
    { // hdr
        sizeof(sTemplate_Tbl),
        sizeof(G1.b.mAdmTemplate_Tbl.pty) / sizeof(sColPrty),                       // colCnt
        0x04,                                                                       // maxCol
        sizeof(G1.b.mAdmTemplate_Tbl.val) / sizeof(sTemplate_TblObj),               // rowCnt
        sizeof(sTemplate_TblObj),
    },
    { // pty
        { 0x00, 8,                                                   UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmTemplate_Tbl.val[0].name),           STRING_TYPE,{0} },   // Name
        { 0x02, sizeof(G1.b.mAdmTemplate_Tbl.val[0].revision),       VALUE_TYPE,{0} },    // Revision Number
        { 0x03, sizeof(G1.b.mAdmTemplate_Tbl.val[0].instances),      VALUE_TYPE,{0} },    // Instances
        { 0x04, sizeof(G1.b.mAdmTemplate_Tbl.val[0].maxInstances),   VALUE_TYPE,{0} }     // Max Instances
    },
    { // val
        {UID_Template_Base,   "Base",    1, 2, 2},
        {UID_Template_Admin,  "Admin",   1, 1, 1},
        {UID_Template_Locking,"Locking", 1, 1, 1}
    }
},

//__align(4) const sSP_Tbl cAdmSP_Tbl =
{
    { // hdr
        sizeof(sSP_Tbl),
        sizeof(G1.b.mAdmSP_Tbl.pty) / sizeof(sColPrty),                 // colCnt
        0x07,                                                           // maxCol
        sizeof(G1.b.mAdmSP_Tbl.val) / sizeof(sSP_TblObj),               // RowCnt
        sizeof(sSP_TblObj),
    },
    { // pty
        { 0x00, 8,                                       UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmSP_Tbl.val[0].name),     STRING_TYPE,{0} },
        { 0x06, sizeof(life_cycle_state),                VALUE_TYPE,{0} },    // LifeCycle
        { 0x07, sizeof(G1.b.mAdmSP_Tbl.val[0].frozen),   VALUE_TYPE,{0} }     // Frozen (bool)
    },
    {
        {UID_SP_Admin,   "Admin",   manufactured,          false},
        {UID_SP_Locking, "Locking", manufactured_inactive, false}
    }
},

#if (_TCG_ == TCG_PYRITE)
//__align(4) const sRemovalMechanism_Tbl cRemovalMechanism_Tbl =
{
    { // hdr
        sizeof(sRemovalMsm_Tbl),
        sizeof(G1.b.mAdmRemovalMsm_Tbl.pty) / sizeof(sColPrty),             // colCnt
        0x01,                                                               // maxCol
        sizeof(G1.b.mAdmRemovalMsm_Tbl.val) / sizeof(sRemovalMsm_TblObj),   // RowCnt
        sizeof(sRemovalMsm_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },     //UID
        { 0x01,sizeof(G1.b.mAdmRemovalMsm_Tbl.val[0].activeRM),VALUE_TYPE,{0} },   //bool
    },
    {
        {UID_RemovalMechanism, 0x01 }
    },
},
#endif

    TCG_END_TAG

} };

/// ********************************************************///
///                     Locking Table Init                  ///
/// ********************************************************///
/* LockingSP Tables */
ddr_data ALIGNED(16) tG2 G2={ {

{ TCG_TBL_ID, TCG_G2_TAG + TCG_TBL_VER },

//__align(4) const sSPInfo_Tbl cLckSPInfo_Tbl =
{
    { // hdr
        sizeof(sSPInfo_Tbl),
        sizeof(G2.b.mLckSPInfo_Tbl.pty) / sizeof(sColPrty),               // colCnt
        0x06,                                                             // maxCol
        sizeof(G2.b.mLckSPInfo_Tbl.val) / sizeof(sSPInfo_TblObj),         // rowCnt
        sizeof(sSPInfo_TblObj),                                           // objSize
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                              // UID
        { 0x01,8,UID_TYPE,{0} },                                              // SPID
        { 0x02,sizeof(G2.b.mLckSPInfo_Tbl.val[0].name),STRING_TYPE,{0} },     // Name, cannot use "sizeof(sSPInfo_TblObj.name)"
        { 0x05,sizeof(G2.b.mLckSPInfo_Tbl.val[0].spSessionTimeout),VALUE_TYPE,{0} },
        { 0x06,sizeof(G2.b.mLckSPInfo_Tbl.val[0].enabled),VALUE_TYPE,{0} }    // Enabled
    },
    {
        {UID_SPInfo, UID_SP_Locking, "Locking", 0, true}
    }
},

//__align(4) const sSPTemplates_Tbl cLckSPTemplates_Tbl =
{
    { // hdr
        sizeof(sSPTemplates_Tbl),
        sizeof(G2.b.mLckSPTemplates_Tbl.pty) / sizeof(sColPrty),              // colCnt
        0x03,                                                                 // maxCol
        sizeof(G2.b.mLckSPTemplates_Tbl.val) / sizeof(sSPTemplates_TblObj),   // owCnt
        sizeof(sSPTemplates_TblObj),                                          // objSize
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x01,8,UID_TYPE,{0} },                                                  // TemplateID
        { 0x02,sizeof(G2.b.mLckSPTemplates_Tbl.val[0].name),STRING_TYPE,{0} },    // Name
        { 0x03,sizeof(G2.b.mLckSPTemplates_Tbl.val[0].version),FBYTE_TYPE,{0} }   // Version
    },
    {
        { UID_SPTemplate_1, UID_Template_Base,   "Base",   {0x00,0x00,0x00,0x02} },
        { UID_SPTemplate_2, UID_Template_Locking,"Locking",{0x00,0x00,0x00,0x02} }
    }
},


//__align(4) const sLckTbl_Tbl cLckTbl_Tbl =
{
    { // hdr
        sizeof(sLckTbl_Tbl),
        sizeof(G2.b.mLckTbl_Tbl.pty) / sizeof(sColPrty),                      // colCnt
        0x0E,                                                                 // maxCol
        sizeof(G2.b.mLckTbl_Tbl.val) / sizeof(sLckTbl_TblObj),                // rowCnt
        sizeof(sLckTbl_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE, {0} },                                              // UID
        { 0x01,sizeof(G2.b.mLckTbl_Tbl.val[0].name),STRING_TYPE,{0} },        // Name
        { 0x04,sizeof(G2.b.mLckTbl_Tbl.val[0].kind),VALUE_TYPE,{0} },         // Kind (Object or Byte)
        { 0x07,sizeof(G2.b.mLckTbl_Tbl.val[0].rows),VALUE_TYPE,{0} },         // Rows
        { 0x0D,sizeof(G2.b.mLckTbl_Tbl.val[0].mGranularity),VALUE_TYPE,{0} },
        { 0x0E,sizeof(G2.b.mLckTbl_Tbl.val[0].rGranularity),VALUE_TYPE,{0} }  // MaxSize
    },
    {
    #if _TCG_ != TCG_PYRITE
        {UID_Table_Table,       "Table",        TBL_K_OBJECT, 15+DSTBL_MAX_NUM-1,     0x00,   0x00},  // R1: Table
        {UID_Table_SPInfo,      "SPInfo",       TBL_K_OBJECT, 1,                      0x00,   0x00},  // R2:
        {UID_Table_SPTemplates, "SPTemplates",  TBL_K_OBJECT, 2,                      0x00,   0x00},  // R3:
        {UID_Table_MethodID,    "MethodID",     TBL_K_OBJECT, LCK_METHOD_TBLOBJ_CNT,  0x00,   0x00},
        {UID_Table_AccessControl,"AccessControl",TBL_K_OBJECT,LCK_ACCESSCTRL_TBLOBJ_CNT,0x00,0x00},
        {UID_Table_ACE,         "ACE",          TBL_K_OBJECT, LCK_ACE_TBLOBJ_CNT,     0x00,   0x00},
        {UID_Table_Authority,   "Authority",    TBL_K_OBJECT, LCK_AUTHORITY_TBLOBJ_CNT,0x00,   0x00},
        {UID_Table_CPIN,        "C_Pin",        TBL_K_OBJECT, LCK_CPIN_TBLOBJ_CNT,    0x00,   0x00},  // row
        {UID_Table_SecretProtect,"SecretProtect",TBL_K_OBJECT,1,                      0x00,   0x00},
        {UID_Table_LockingInfo, "LockingInfo",  TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_Locking,     "Locking",      TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_MBRControl,  "MBRControl",   TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_MBR,         "MBR",          TBL_K_BYTE,   MBR_LEN,                0x01,   0x01},  // VU/VU
        {UID_Table_K_AES_256,   "K_AES_256",    TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_DataStore,   "DataStore",    TBL_K_BYTE,   DATASTORE_LEN,          0x01,   0x01},  // VU/VU
        {UID_Table_DataStore2|UID_FF,  "DataStore2",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore3|UID_FF,  "DataStore3",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore4|UID_FF,  "DataStore4",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore5|UID_FF,  "DataStore5",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore6|UID_FF,  "DataStore6",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore7|UID_FF,  "DataStore7",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore8|UID_FF,  "DataStore8",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore9|UID_FF,  "DataStore9",   TBL_K_BYTE,   0x00,            0x01,   0x01}   // VU/VU
    #else
        {UID_Table_Table,       "Table",        TBL_K_OBJECT, 13,                     0x00,   0x00},  // R1: Table
        {UID_Table_SPInfo,      "SPInfo",       TBL_K_OBJECT, 1,                      0x00,   0x00},  // R2:
        {UID_Table_SPTemplates, "SPTemplates",  TBL_K_OBJECT, 2,                      0x00,   0x00},  // R3:
        {UID_Table_MethodID,    "MethodID",     TBL_K_OBJECT, LCK_METHOD_TBLOBJ_CNT,  0x00,   0x00},
        {UID_Table_AccessControl,"AccessControl",TBL_K_OBJECT,LCK_ACCESSCTRL_TBLOBJ_CNT,0x00, 0x00},
        {UID_Table_ACE,         "ACE",          TBL_K_OBJECT, LCK_ACE_TBLOBJ_CNT,     0x00,   0x00},
        {UID_Table_Authority,   "Authority",    TBL_K_OBJECT, LCK_AUTHORITY_TBLOBJ_CNT,0x00,  0x00},
        {UID_Table_CPIN,        "C_Pin",        TBL_K_OBJECT, LCK_CPIN_TBLOBJ_CNT,    0x00,   0x00},  // row
      //{UID_Table_SecretProtect,"SecretProtect",TBL_K_OBJECT,1,                      0x00,   0x00},
        {UID_Table_LockingInfo, "LockingInfo",  TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_Locking,     "Locking",      TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_MBRControl,  "MBRControl",   TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_MBR,         "MBR",          TBL_K_BYTE,   MBR_LEN,                0x01,   0x01},  // VU/VU
      //{UID_Table_K_AES_256,   "K_AES_256",    TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_DataStore,   "DataStore",    TBL_K_BYTE,   DATASTORE_LEN,          0x01,   0x01},  // VU/VU
    #endif
    }
},

//__align(4) const sLckMethod_Tbl cLckMethod_Tbl =
{
    { // hdr
        sizeof(sLckMethod_Tbl),
        sizeof(G2.b.mLckMethod_Tbl.pty) / sizeof(sColPrty),               // colCnt
        0x03,                                                             // maxCol
        sizeof(G2.b.mLckMethod_Tbl.val) / sizeof(sMethod_TblObj),         // rowCnt
        sizeof(sMethod_TblObj),
    },
    { // ColPty
        { 0x00,8,UID_TYPE,{0} },                                          // UID
        { 0x01,sizeof(G2.b.mLckMethod_Tbl.val[0].name),STRING_TYPE,{0} }  // Kind (Object or Byte)
    },
    { // val
        { UID_MethodID_Next,         "Next"},        // R1:
        { UID_MethodID_GetACL,       "GetACL"},
    #if _TCG_ != TCG_PYRITE
        { UID_MethodID_GenKey,       "GenKey"},
    #endif
        { UID_MethodID_RevertSP,     "RevertSP"},
        { UID_MethodID_Get,          "Get"},
        { UID_MethodID_Set,          "Set"},
        { UID_MethodID_Authenticate, "Authenticate"},
        { UID_MethodID_Random,       "Random"},
    #if _TCG_ != TCG_PYRITE
        { UID_MethodID_Reactivate,   "Reactivate"},
        { UID_MethodID_Erase,        "Erase"},
      #if TCG_FS_CONFIG_NS
        { UID_MethodID_Assign,       "Assign" },
        { UID_MethodID_Deassign,     "Deassign"},
      #endif
    #endif
    }
},

//__align(4) const sLckAccessCtrl_Tbl cLckAccessCtrl_Tbl =
{
    { // SP: 3 row + 1 rows
        {UID_ThisSP,    UID_MethodID_RevertSP,      {UID_ACE_Admin,  0,0,0},UID_ACE_Anybody},
        {UID_ThisSP,    UID_MethodID_Authenticate,  {UID_ACE_Anybody,0,0,0},UID_ACE_Anybody},
        {UID_ThisSP,    UID_MethodID_Random,        {UID_ACE_Anybody,0,0,0},UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ThisSP,    UID_MethodID_Reactivate,    {UID_ACE_SP_Reactivate_Admin,0,0,0},UID_ACE_Anybody},
    #endif
    },
    { // Table: 15 rows + 1 row
        {UID_Table,             UID_MethodID_Next,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},  // R1: Table.Next
        {UID_Table_Table,       UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},  // R2: TableObj.Get
        {UID_Table_SPInfo,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},  // R3: TableObj.Get
        {UID_Table_SPTemplates, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_MethodID,    UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_AccessControl,UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_ACE,         UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_Authority,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_CPIN,        UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Table_SecretProtect,UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_Table_LockingInfo, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_Locking,     UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_MBRControl,  UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_MBR,         UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Table_K_AES_256,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_Table_DataStore,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Table_DataStore2,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore3,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore4,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore5,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore6,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore7,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore8,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore9,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // SPInfo: 1 row
        {UID_SPInfo,            UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    },
    { // SPTemplates: 3 row
        {UID_SPTemplate,        UID_MethodID_Next,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_1,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_2,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    },
    { // MethodID: 7 rows + 2 rows
        {UID_MethodID,          UID_MethodID_Next,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},   // R1: Method.Next
        {UID_MethodID_Next,     UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_GetACL,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_MethodID_GenKey,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_MethodID_RevertSP, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Get,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Set,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Authenticate,UID_MethodID_Get,{UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Random,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_MethodID_Reactivate,UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Erase,    UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
      #if TCG_FS_CONFIG_NS
        {UID_MethodID_Assign,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Deassign, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
      #endif
    #endif
    },
    { // ACE: 65 rows + 58 rows
        {UID_ACE,               UID_MethodID_Next,          {UID_ACE_Anybody,    0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Anybody,       UID_MethodID_Get,           {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Admin,         UID_MethodID_Get,           {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Anybody_Get_CommonName,UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Admins_Set_CommonName, UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #endif
        {UID_ACE_ACE_Get_All, UID_MethodID_Get,             {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_ACE_Get_All, UID_MethodID_Set,             {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_ACE_Set_BooleanExpression,UID_MethodID_Get,{UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Authority_Get_All, UID_MethodID_Get,       {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Authority_Get_All, UID_MethodID_Set,       {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Authority_Set_Enabled,UID_MethodID_Get,    {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},

        {UID_ACE_C_PIN_Admins_Get_All_NOPIN,UID_MethodID_Get,{UID_ACE_ACE_Get_All,0,0,0},   UID_ACE_Anybody},
        {UID_ACE_C_PIN_Admins_Set_PIN,  UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User1_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User2_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_C_PIN_User3_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User4_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User5_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User6_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User7_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User8_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User9_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #endif
        {UID_ACE_C_PIN_User1_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_C_PIN_User2_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_C_PIN_User3_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_C_PIN_User4_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User5_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User6_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User7_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User8_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User9_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2

        {UID_ACE_K_AES_256_GlobalRange_GenKey,UID_MethodID_Get,{UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_GlobalRange_GenKey,UID_MethodID_Set,{UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range1_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range1_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range2_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range2_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range3_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range3_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range4_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range4_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range5_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range5_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range6_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range6_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range7_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range7_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range8_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},  // to Range8 for opal2
        {UID_ACE_K_AES_256_Range8_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_Mode,            UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Range1_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GRange_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Range1_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GRange_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Range1_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GlblRng_Admins_Set, UID_MethodID_Get,  {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Admins_RangeStartToLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
      #if TCG_FS_CONFIG_NS
        {UID_ACE_Locking_Namespace_IdtoGlbRng, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Namespace_IdtoGlbRng, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
      #endif
    #endif
        {UID_ACE_MBRControl_Admins_Set, UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_MBRControl_Set_Done,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_MBRControl_Set_Done,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if (_TCG_ != TCG_PYRITE) && TCG_FS_CONFIG_NS
        {UID_ACE_Assign,                UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Assign,                UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Deassign,              UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Deassign,              UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_DataStore_Get_All,     UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore_Get_All,     UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore_Set_All,     UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore_Set_All,     UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE // *** 32 rows
        {UID_ACE_DataStore2_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore2_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore2_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore2_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},

        {UID_ACE_Locking_GRange_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},

        {UID_ACE_CPIN_Anybody_Get_NoPIN,        ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        { UID_ACE_SP_Reactivate_Admin,          UID_MethodID_Get,   { UID_ACE_Anybody,0,0,0 }, UID_ACE_Anybody },
        { UID_ACE_SP_Reactivate_Admin,          UID_MethodID_Set,   { UID_ACE_ACE_Set_BooleanExpression,0,0,0 }, UID_ACE_Anybody },

        {UID_ACE_Locking_GRange_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    //#if _TCG_ != TCG_PYRITE
        { UID_ACE_User1_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User2_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User3_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User4_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User5_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User6_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User7_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User8_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User9_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User1_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User2_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User3_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User4_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User5_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User6_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User7_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User8_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User9_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
    //#endif
    #endif
    },
    { // Authority: 28 rows
        {UID_Authority,         UID_MethodID_Next,      {UID_ACE_Anybody,0,0,0},               UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Anybody, UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admins,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin3,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin4,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Users,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User3,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User4,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User5,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User6,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User7,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User8,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User9,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_Admin3,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_Admin4,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User1_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User2_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User3,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User3_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User4,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User4_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User5,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User5_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User6,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User6_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User7,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User7_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User8,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User8_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User9,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User9_Set_CommonName,0,0},   UID_ACE_Anybody},
    #else
        {UID_Authority_Anybody, UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admins,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Users,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // CPIN: 25 rows
        {UID_CPIN,       UID_MethodID_Next, {UID_ACE_Anybody,0,0,0},            UID_ACE_Anybody},           // R1: CPIN.Next
    #if _TCG_ != TCG_PYRITE
        {UID_CPIN_Admin1, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},   // R2:
        {UID_CPIN_Admin1, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin3, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin3, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin4, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin4, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Set, {UID_ACE_C_PIN_User1_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Set, {UID_ACE_C_PIN_User2_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User3,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User3,  UID_MethodID_Set, {UID_ACE_C_PIN_User3_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User4,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User4,  UID_MethodID_Set, {UID_ACE_C_PIN_User4_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User5,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User5,  UID_MethodID_Set, {UID_ACE_C_PIN_User5_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User6,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User6,  UID_MethodID_Set, {UID_ACE_C_PIN_User6_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User7,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User7,  UID_MethodID_Set, {UID_ACE_C_PIN_User7_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User8,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User8,  UID_MethodID_Set, {UID_ACE_C_PIN_User8_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User9,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User9,  UID_MethodID_Set, {UID_ACE_C_PIN_User9_Set_PIN,0,0,0},        UID_ACE_Anybody},
    #else
        {UID_CPIN_Admin1, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin1, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Set, {UID_ACE_C_PIN_User1_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Set, {UID_ACE_C_PIN_User2_Set_PIN,0,0,0},        UID_ACE_Anybody},
    #endif
    },
    #if _TCG_ != TCG_PYRITE    // SecretPortect: +2 rows
    {
        {UID_SecretProtect,     UID_MethodID_Next,  { UID_ACE_Anybody,0 }, UID_ACE_Anybody},
        {UID_SecretProtect_256, UID_MethodID_Get,   { UID_ACE_Anybody,0 }, UID_ACE_Anybody},
    },
    #endif

    { // LockingInfo: 1 row
        { UID_LockingInfo,       UID_MethodID_Get,  { UID_ACE_Anybody,0 }, UID_ACE_Anybody },
    },
    { // Locking: 11 rows + 8 rows
        { UID_Locking,           UID_MethodID_Next, { UID_ACE_Anybody,0 }, UID_ACE_Anybody },
    #if _TCG_ != TCG_PYRITE
      #if TCG_FS_CONFIG_NS
        { UID_Locking,           UID_MethodID_Assign,   { UID_ACE_Assign,0 }, UID_ACE_Anybody },
        { UID_Locking,           UID_MethodID_Deassign, { UID_ACE_Assign,0 }, UID_ACE_Anybody },
      #endif
        { UID_Locking_GRange,    UID_MethodID_Get, { UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,0,0}, UID_ACE_Anybody},
        { UID_Locking_GRange,    UID_MethodID_Set, { UID_ACE_Locking_GlblRng_Admins_Set,UID_ACE_Locking_GRange_Set_RdLocked,UID_ACE_Locking_GRange_Set_WrLocked,UID_ACE_Admins_Set_CommonName }, UID_ACE_Anybody },           //ACL: >1
        { UID_Locking_GRange,    ~UID_MethodID_Erase,{ UID_ACE_Locking_GRange_Erase,0 }, UID_ACE_Anybody },

        { UID_Locking_Range1,    UID_MethodID_Get, { UID_ACE_Locking_Range1_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range1,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range1_Set_RdLocked,UID_ACE_Locking_Range1_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range1,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range1_Erase,0 }, UID_ACE_Anybody },

        { UID_Locking_Range2,    UID_MethodID_Get, { UID_ACE_Locking_Range2_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range2,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range2_Set_RdLocked,UID_ACE_Locking_Range2_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range2,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range2_Erase,0 }, UID_ACE_Anybody },

        { UID_Locking_Range3,    UID_MethodID_Get, { UID_ACE_Locking_Range3_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range3,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range3_Set_RdLocked,UID_ACE_Locking_Range3_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range3,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range3_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range4,    UID_MethodID_Get, { UID_ACE_Locking_Range4_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range4,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range4_Set_RdLocked,UID_ACE_Locking_Range4_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range4,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range4_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range5,    UID_MethodID_Get, { UID_ACE_Locking_Range5_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range5,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range5_Set_RdLocked,UID_ACE_Locking_Range5_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range5,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range5_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range6,    UID_MethodID_Get, { UID_ACE_Locking_Range6_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range6,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range6_Set_RdLocked,UID_ACE_Locking_Range6_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range6,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range6_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range7,    UID_MethodID_Get, { UID_ACE_Locking_Range7_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range7,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range7_Set_RdLocked,UID_ACE_Locking_Range7_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range7,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range7_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range8,    UID_MethodID_Get, { UID_ACE_Locking_Range8_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        {UID_Locking_Range8,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range8_Set_RdLocked,UID_ACE_Locking_Range8_Set_WrLocked,UID_ACE_Admins_Set_CommonName},    UID_ACE_Anybody}, //ACL: >1
        {UID_Locking_Range8,    ~UID_MethodID_Erase, { UID_ACE_Locking_Range8_Erase,0 }, UID_ACE_Anybody},
    #else
        {UID_Locking_GRange,    UID_MethodID_Get, {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,0,0,0}, UID_ACE_Anybody},
        {UID_Locking_GRange,    UID_MethodID_Set, {UID_ACE_Locking_GlblRng_Admins_Set,UID_ACE_Locking_GRange_Set_RdLocked,UID_ACE_Locking_GRange_Set_WrLocked,0}, UID_ACE_Anybody}, //ACL: >1
    #endif
    },
    { // MBRControl: 2 rows
        {UID_MBRControl,        UID_MethodID_Get, {UID_ACE_Anybody,0,0,0},               UID_ACE_Anybody},
        {UID_MBRControl,        UID_MethodID_Set, {UID_ACE_MBRControl_Admins_Set,UID_ACE_MBRControl_Set_Done,0,0}, UID_ACE_Anybody}, //ACL>1
    },
    { // MBR: 2 rows
        {UID_MBR,               UID_MethodID_Get, {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MBR,               UID_MethodID_Set, {UID_ACE_Admin,0,0,0},   UID_ACE_Anybody},
    },
    #if _TCG_ != TCG_PYRITE // K_AES: 10 rows + 8 rows
    {
        {UID_K_AES_256_GRange_Key, UID_MethodID_GenKey,{UID_ACE_K_AES_256_GlobalRange_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_GRange_Key, UID_MethodID_Get,   {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range1_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range1_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range1_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range2_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range2_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range2_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range3_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range3_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range3_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range4_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range4_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range4_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range5_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range5_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range5_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range6_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range6_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range6_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range7_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range7_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range7_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range8_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range8_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range8_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
    },
    #endif

    { // DataStore: 2 rows
        {UID_DataStore,     UID_MethodID_Get,   {UID_ACE_DataStore_Get_All,0,0,0},  UID_ACE_Anybody},
        {UID_DataStore,     UID_MethodID_Set,   {UID_ACE_DataStore_Set_All,0,0,0},  UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_DataStore2,    ~UID_MethodID_Get,  {UID_ACE_DataStore2_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore2,    ~UID_MethodID_Set,  {UID_ACE_DataStore2_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore3,    ~UID_MethodID_Get,  {UID_ACE_DataStore3_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore3,    ~UID_MethodID_Set,  {UID_ACE_DataStore3_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore4,    ~UID_MethodID_Get,  {UID_ACE_DataStore4_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore4,    ~UID_MethodID_Set,  {UID_ACE_DataStore4_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore5,    ~UID_MethodID_Get,  {UID_ACE_DataStore5_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore5,    ~UID_MethodID_Set,  {UID_ACE_DataStore5_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore6,    ~UID_MethodID_Get,  {UID_ACE_DataStore6_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore6,    ~UID_MethodID_Set,  {UID_ACE_DataStore6_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore7,    ~UID_MethodID_Get,  {UID_ACE_DataStore7_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore7,    ~UID_MethodID_Set,  {UID_ACE_DataStore7_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore8,    ~UID_MethodID_Get,  {UID_ACE_DataStore8_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore8,    ~UID_MethodID_Set,  {UID_ACE_DataStore8_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore9,    ~UID_MethodID_Get,  {UID_ACE_DataStore9_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore9,    ~UID_MethodID_Set,  {UID_ACE_DataStore9_Set_All,0,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // ColPty
        {0x01,8,UID_TYPE,{0}},                                               // InvokingID
        {0x02,8,UID_TYPE,{0}},                                               // MethodID
        {0x04,sizeof(G2.b.mLckAxsCtrl_Tbl.thisSP[0].acl),UIDLIST_TYPE,{0}},  // ACL
        {0x08,8,UID_TYPE,{0}}           //GetACLACL
    },
    sizeof(G2.b.mLckAxsCtrl_Tbl.pty)/sizeof(sColPrty),                   // colCnt
    #if _TCG_==TCG_PYRITE
        (sizeof(G2.b.mLckAxsCtrl_Tbl.thisSP) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.table) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spInfo) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spTemplate) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.method) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.ace) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.authority) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.cpin) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lckingInfo) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lcking) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbrCtrl) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbr) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.datastore))   \
            / sizeof(sAxsCtrl_TblObj),       // rowCnt
    #else
        (sizeof(G2.b.mLckAxsCtrl_Tbl.thisSP) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.table) +      \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spInfo) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spTemplate) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.method) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.ace) +        \
        sizeof(G2.b.mLckAxsCtrl_Tbl.authority) +  \
        sizeof(G2.b.mLckAxsCtrl_Tbl.cpin) +       \
        sizeof(G2.b.mLckAxsCtrl_Tbl.secretPrtct) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lckingInfo) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lcking) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbrCtrl) +    \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbr) +        \
        sizeof(G2.b.mLckAxsCtrl_Tbl.kaes) +       \
        sizeof(G2.b.mLckAxsCtrl_Tbl.datastore))   \
        / sizeof(sAxsCtrl_TblObj),       // rowCnt
    #endif
        sizeof(sAxsCtrl_TblObj),       // objSize
        0x0e
},

#if _TCG_!=TCG_PYRITE
//__align(4) const sSecretProtect_Tbl cLckSecretProtect_Tbl =
{
    { // hdr
        sizeof(sSecretProtect_Tbl),
        sizeof(G2.b.mLckSecretProtect_Tbl.pty) / sizeof(sColPrty),                     // colCnt
        0x03,                                                                          // maxCol
        sizeof(G2.b.mLckSecretProtect_Tbl.val) / sizeof(sSecretProtect_TblObj),        // RowCnt
        sizeof(sSecretProtect_Tbl),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                           // UID
        { 0x01,8,UID_TYPE,{0} },                                                           // table
        { 0x02,sizeof(G2.b.mLckSecretProtect_Tbl.val[0].colNumber),VALUE_TYPE,{0} },       // colNumber
        { 0x03,sizeof(G2.b.mLckSecretProtect_Tbl.val[0].protectMechanism),LIST_TYPE,{0} }, // protectMechanisms
    },
    {
        { UID_SecretProtect_256, UID_K_AES_256, 0x03, {0x01,0x01,0x00}}      // ProtectMechanisms=1 for eDrive
    }
},
#endif

//__align(4) const sLockingInfo_Tbl cLckLockingInfo_Tbl =
{
    { // hdr
        sizeof(sLockingInfo_Tbl),
        sizeof(G2.b.mLckLockingInfo_Tbl.pty) / sizeof(sColPrty),                       // colCnt
        0x60001,                                                                       // maxCol
        sizeof(G2.b.mLckLockingInfo_Tbl.val) / sizeof(sLockingInfo_TblObj),            // rowCnt
        sizeof(sLockingInfo_TblObj),
    },
    { // ColPty
        { 0x00,8,UID_TYPE,{0} },                                                                  // UID
        { 0x01,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].name),STRING_TYPE,{0} },                    // name
        { 0x02,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].version),VALUE_TYPE,{0} },                  // version
        { 0x03,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].encryptSupport),VALUE_TYPE,{0} },           // encryptSupport
        { 0x04,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].maxRanges),VALUE_TYPE,{0} },                // maxRange
        { 0x07,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].alignentReuired),VALUE_TYPE,{0} },          // AlignmentRequired
        { 0x08,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].logicalBlockSize),VALUE_TYPE,{0} },         // LogicalBlcokSize
        { 0x09,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].alignmentGranularity),VALUE_TYPE,{0} },     // AlignmentGranularity
        { 0x0A,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].lowestAlignedLBA),VALUE_TYPE,{0} },         // LowestAlignedLBA
        { 0x60000,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].singleUserModeRange),UID2_TYPE,{0} },       // 0x60 -> 0x600000
        { 0x60001,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy),VALUE_TYPE,{0} }    // RangeStartLengthPolicy
    },
    {
    #if _TCG_ != TCG_PYRITE
        {UID_LockingInfo, "", 0x01, 0x01, LOCKING_RANGE_CNT, true, LBA_SIZE, TCG_AlignmentGranularity, 0, {0}, 1}  // 512B, 8KB/page
    #else
        {UID_LockingInfo, "", 0x01, 0x00, LOCKING_RANGE_CNT, true, LBA_SIZE, TCG_AlignmentGranularity, 0, {0}, 1 } // Max modify LBU_SIZE?? -> LBA_SIZE 
    #endif
    }
},

    TCG_END_TAG
} };


ddr_data ALIGNED(16) tG3 G3 ={ {
{    TCG_TBL_ID, TCG_G3_TAG + TCG_TBL_VER    },

//__align(4) const sLckACE_Tbl cLckACE_Tbl =
{
    { // hdr
        sizeof(sLckACE_Tbl),
        sizeof(G3.b.mLckACE_Tbl.pty) / sizeof(sColPrty),                          // colCnt
        0x04,                                                                     // maxCol
        sizeof(G3.b.mLckACE_Tbl.val) / sizeof(sLckACE_TblObj),                    // rowCnt
        sizeof(sLckACE_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x03,sizeof(G3.b.mLckACE_Tbl.val[0].booleanExpr),UIDLIST_TYPE,{0} },    // BooleanExpr
        { 0x04,sizeof(G3.b.mLckACE_Tbl.val[0].col),LIST_TYPE,{0} }                // Columns
    },
    {
        {UID_ACE_Anybody,               { UID_Authority_Anybody, }, {0} }, //ACE_Anybody, col(All)
        {UID_ACE_Admin,                 { UID_Authority_Admins, },  {0} }, //ACE_Admin, col(All)
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Anybody_Get_CommonName,{UID_Authority_Anybody, }, {2,0,2,}},
        {UID_ACE_Admins_Set_CommonName, {UID_Authority_Admins, },  {1,2,}}, //CommonName
    #endif
        {UID_ACE_ACE_Get_All,           {UID_Authority_Admins, },  {0,}},
        {UID_ACE_ACE_Set_BooleanExpression,{UID_Authority_Admins, }, {1,3,}}, //BooleanExpr
        {UID_ACE_Authority_Get_All,     {UID_Authority_Admins, },  {0,}},
        {UID_ACE_Authority_Set_Enabled, {UID_Authority_Admins, },  {1,5,}}, //Enabled

        {UID_ACE_C_PIN_Admins_Get_All_NOPIN,{UID_Authority_Admins, },{5,0,4,5,6,7,}},    // ACE_CPIN_SID_Get..., col(UID, CharSet, TryLimit, Tries, Persistence)
        {UID_ACE_C_PIN_Admins_Set_PIN,  {UID_Authority_Admins, },  {1,3,}},     // ACE_CPIN_SID_Set..., col(PIN)
        {UID_ACE_C_PIN_User1_Set_PIN,   {UID_Authority_Admins,UID_Authority_User1, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User2_Set_PIN,   {UID_Authority_Admins,UID_Authority_User2, },  {1,3,}},   //Boolean>1
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_C_PIN_User3_Set_PIN,   {UID_Authority_Admins,UID_Authority_User3, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User4_Set_PIN,   {UID_Authority_Admins,UID_Authority_User4, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User5_Set_PIN,   {UID_Authority_Admins,UID_Authority_User5, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User6_Set_PIN,   {UID_Authority_Admins,UID_Authority_User6, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User7_Set_PIN,   {UID_Authority_Admins,UID_Authority_User7, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User8_Set_PIN,   {UID_Authority_Admins,UID_Authority_User8, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User9_Set_PIN,   {UID_Authority_Admins,UID_Authority_User9, },  {1,3,}},   //Boolean>1

        {UID_ACE_K_AES_256_GlobalRange_GenKey, {UID_Authority_Admins, }, {0,}},
        {UID_ACE_K_AES_256_Range1_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range2_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range3_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range4_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range5_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range6_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range7_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range8_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_Mode,              {UID_Authority_Anybody, },{1,4,}},
    #endif

    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range1_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range2_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range3_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range4_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range5_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range6_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range7_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range8_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_GRange_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range1_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range2_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range3_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range4_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range5_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range6_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range7_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range8_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_GRange_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range1_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range2_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range3_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range4_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range5_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range6_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range7_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range8_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_GlblRng_Admins_Set,        {UID_Authority_Admins, }, {5,5,6,7,8,9,}},
        {UID_ACE_Locking_Admins_RangeStartToLocked, {UID_Authority_Admins, },{7,3,4,5,6,7,8,9,}},
      #if TCG_FS_CONFIG_NS
        {UID_ACE_Locking_Namespace_IdtoGlbRng, {UID_Authority_Admins, },    {2, 0x14, 0x15,}},
      #endif
    #else
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,0,0,0}},
        {UID_ACE_Locking_GRange_Set_RdLocked,       {UID_Authority_Admins, }, {1,7,}},
        {UID_ACE_Locking_GRange_Set_WrLocked,       {UID_Authority_Admins, },{1,8,}},
        {UID_ACE_Locking_GlblRng_Admins_Set,        {UID_Authority_Admins, }, {5,5,6,7,8,9,}},
    #endif

        {UID_ACE_MBRControl_Admins_Set,             {UID_Authority_Admins, },{3,1,2,3,}},
        {UID_ACE_MBRControl_Set_Done,               {UID_Authority_Admins, },{2,2,3,}},
    #if TCG_FS_CONFIG_NS
        {UID_ACE_Assign,                            {UID_Authority_Admins, }, {0,}},
        {UID_ACE_Deassign,                          {UID_Authority_Admins, }, {0,}},
    #endif
        {UID_ACE_DataStore_Get_All,                 {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore_Set_All,                 {UID_Authority_Admins, },{0,}},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_DataStore2_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore2_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore3_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore3_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore4_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore4_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore5_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore5_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore6_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore6_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore7_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore7_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore8_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore8_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore9_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore9_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_Locking_GRange_Set_ReadToLOR|UID_FF,{UID_Authority_User1, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range1_Set_ReadToLOR|UID_FF,{UID_Authority_User2, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range2_Set_ReadToLOR|UID_FF,{UID_Authority_User3, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range3_Set_ReadToLOR|UID_FF,{UID_Authority_User4, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range4_Set_ReadToLOR|UID_FF,{UID_Authority_User5, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range5_Set_ReadToLOR|UID_FF,{UID_Authority_User6, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range6_Set_ReadToLOR|UID_FF,{UID_Authority_User7, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range7_Set_ReadToLOR|UID_FF,{UID_Authority_User8, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range8_Set_ReadToLOR|UID_FF,{UID_Authority_User9, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range1_Set_Range|UID_FF,    {UID_Authority_User2, },{2,3,4,}},
        {UID_ACE_Locking_Range2_Set_Range|UID_FF,    {UID_Authority_User3, },{2,3,4,}},
        {UID_ACE_Locking_Range3_Set_Range|UID_FF,    {UID_Authority_User4, },{2,3,4,}},
        {UID_ACE_Locking_Range4_Set_Range|UID_FF,    {UID_Authority_User5, },{2,3,4,}},
        {UID_ACE_Locking_Range5_Set_Range|UID_FF,    {UID_Authority_User6, },{2,3,4,}},
        {UID_ACE_Locking_Range6_Set_Range|UID_FF,    {UID_Authority_User7, },{2,3,4,}},
        {UID_ACE_Locking_Range7_Set_Range|UID_FF,    {UID_Authority_User8, },{2,3,4,}},
        {UID_ACE_Locking_Range8_Set_Range|UID_FF,    {UID_Authority_User9, },{2,3,4,}},
        { UID_ACE_CPIN_Anybody_Get_NoPIN|UID_FF,     { UID_Authority_Anybody, },{5,0,4,5,6,7,} },
        { UID_ACE_SP_Reactivate_Admin,               { UID_Authority_Admins, },{0} },
        { UID_ACE_Locking_GRange_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User1},{0} },
        { UID_ACE_Locking_Range1_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User2},{0} },
        { UID_ACE_Locking_Range2_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User3},{0} },
        { UID_ACE_Locking_Range3_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User4},{0} },
        { UID_ACE_Locking_Range4_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User5},{0} },
        { UID_ACE_Locking_Range5_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User6},{0} },
        { UID_ACE_Locking_Range6_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User7},{0} },
        { UID_ACE_Locking_Range7_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User8},{0} },
        { UID_ACE_Locking_Range8_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User9},{0} },

        { UID_ACE_User1_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User2_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User3_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User4_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User5_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User6_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User7_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User8_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User9_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
    #endif
    }
},

//__align(4) const sLckAuthority_Tbl cLckAuthority_Tbl =
{
    { // hdr
        sizeof(sLckAuthority_Tbl),
        sizeof(G3.b.mLckAuthority_Tbl.pty) / sizeof(sColPrty),              // colCnt
        0x12,                                                               // maxCol
        sizeof(G3.b.mLckAuthority_Tbl.val) / sizeof(sAuthority_TblObj),     // RowCnt
        sizeof(sAuthority_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                          // UID
        { 0x01,sizeof(G3.b.mLckAuthority_Tbl.val[0].name),STRING_TYPE,{0} },
        { 0x02,sizeof(G3.b.mLckAuthority_Tbl.val[0].commonName),STRING_TYPE,{0} },
        { 0x03,sizeof(G3.b.mLckAuthority_Tbl.val[0].isClass),VALUE_TYPE,{0} },            // IsClass (bool)
        { 0x04,8,UID_TYPE,{0} },                                                          // Class
        { 0x05,sizeof(G3.b.mLckAuthority_Tbl.val[0].enabled),VALUE_TYPE,{0} },            // Enabled (bool)
        { 0x06,sizeof(secure_message),VALUE_TYPE,{0} },                                   // Secure
        { 0x07,sizeof(hash_protocol),VALUE_TYPE,{0} },                                    // HashAndSign
        { 0x08,sizeof(G3.b.mLckAuthority_Tbl.val[0].presentCertificate),VALUE_TYPE,{0} }, // PresentCertificate (bool)
        { 0x09,sizeof(auth_method),VALUE_TYPE,{0} },                                      // Operation
        { 0x0A,8,UID_TYPE,{0} },      //Credential (UID)
        { 0x0B,8,UID_TYPE,{0} },      //ResponseSign
        { 0x0C,8,UID_TYPE,{0} }       //ResponseExch
    },
    {
        {UID_Authority_Anybody,"Anybody",   "", false,UID_Null,            true, SECURE_None, HASH_None, false, AUTH_None,     UID_Null,        UID_Null, UID_Null},
        {UID_Authority_Admins, "Admins",    "", true, UID_Null,            true, SECURE_None, HASH_None, false, AUTH_None,     UID_Null,        UID_Null, UID_Null},
        {UID_Authority_Admin1, "Admin1",    "", false,UID_Authority_Admins,true, SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin1, UID_Null, UID_Null},
        {UID_Authority_Admin2, "Admin2",    "", false,UID_Authority_Admins,false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin2, UID_Null, UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Admin3, "Admin3",    "", false,UID_Authority_Admins,false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin3, UID_Null, UID_Null},
        {UID_Authority_Admin4, "Admin4",    "", false,UID_Authority_Admins,false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin4, UID_Null, UID_Null},
    #endif
        {UID_Authority_Users,  "Users ",    "", true, UID_Null,            true, SECURE_None, HASH_None, false, AUTH_None,     UID_Null,        UID_Null, UID_Null},
        {UID_Authority_User1,  "User1",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User1,  UID_Null, UID_Null},
        {UID_Authority_User2,  "User2",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User2,  UID_Null, UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_User3,  "User3",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User3,  UID_Null, UID_Null},
        {UID_Authority_User4,  "User4",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User4,  UID_Null, UID_Null},
        {UID_Authority_User5,  "User5",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User5,  UID_Null, UID_Null},
        {UID_Authority_User6,  "User6",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User6,  UID_Null, UID_Null},
        {UID_Authority_User7,  "User7",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User7,  UID_Null, UID_Null},
        {UID_Authority_User8,  "User8",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User8,  UID_Null, UID_Null},
        {UID_Authority_User9,  "User9",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User9,  UID_Null, UID_Null}
    #endif
    }
},

//__align(4) const sLckCPin_Tbl cLckCPin_Tbl =
{
    { // hdr
        sizeof(sLckCPin_Tbl),
        sizeof(G3.b.mLckCPin_Tbl.pty) / sizeof(sColPrty),       // colCnt
        0x07,                                                   // maxCol
        sizeof(G3.b.mLckCPin_Tbl.val) / sizeof(sCPin_TblObj),   // rowCnt
        sizeof(sCPin_TblObj),
    },
    { // ColPty
        { 0x00,sizeof(G3.b.mLckCPin_Tbl.val[0].uid),UID_TYPE,{0} },           // UID
        { 0x01,sizeof(G3.b.mLckCPin_Tbl.val[0].name),STRING_TYPE,{0} },
        //{ 0x03,sizeof(U8*),VALUE_TYPE,{0} },                                 // PIN, check later ...
        { 0x03,sizeof(G3.b.mLckCPin_Tbl.val[0].cPin),VBYTE_TYPE,{0} },         // PIN
        { 0x04,sizeof(G3.b.mLckCPin_Tbl.val[0].charSet),UID_TYPE,{0} },       // CharSet
        { 0x05,sizeof(G3.b.mLckCPin_Tbl.val[0].tryLimit),VALUE_TYPE,{0} },    // TryLimit           --> set to 50 for OCP spec SEC-37
        { 0x06,sizeof(G3.b.mLckCPin_Tbl.val[0].tries),VALUE_TYPE,{0} },       // Tries
        { 0x07,sizeof(G3.b.mLckCPin_Tbl.val[0].persistence),VALUE_TYPE,{0} }  // Persistence (bool)
    },
    {
        { UID_CPIN_Admin1, "C_PIN_Admin1", { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_Admin2, "C_PIN_Admin2", { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
    #if _TCG_ != TCG_PYRITE
        { UID_CPIN_Admin3, "C_PIN_Admin3", { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_Admin4, "C_PIN_Admin4", { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
    #endif
        { UID_CPIN_User1, "C_PIN_User1",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User2, "C_PIN_User2",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
    #if _TCG_ != TCG_PYRITE
        { UID_CPIN_User3, "C_PIN_User3",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User4, "C_PIN_User4",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User5, "C_PIN_User5",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User6, "C_PIN_User6",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User7, "C_PIN_User7",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User8, "C_PIN_User8",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
        { UID_CPIN_User9, "C_PIN_User9",   { 0, { 0 }, { 0 } }, UID_Null, 50, 0, false },
    #endif
    }
},

//__align(4) const sLocking_Tbl cLckLocking_Tbl =
{
    { // hdr
        sizeof(sLocking_Tbl),
        sizeof(G3.b.mLckLocking_Tbl.pty) / sizeof(sColPrty),          // colCnt
        0x13,                                                         // maxCol
        sizeof(G3.b.mLckLocking_Tbl.val) / sizeof(sLocking_TblObj),   // rowCnt
         sizeof(sLocking_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x01,sizeof(G3.b.mLckLocking_Tbl.val[0].name),STRING_TYPE,{0} },        // name
        { 0x02,sizeof(G3.b.mLckLocking_Tbl.val[0].commonName),STRING_TYPE,{0} },
        { 0x03,sizeof(G3.b.mLckLocking_Tbl.val[0].rangeStart),VALUE_TYPE,{0} },   // rangeStart
        { 0x04,sizeof(G3.b.mLckLocking_Tbl.val[0].rangeLength),VALUE_TYPE,{0} },
        { 0x05,1,VALUE_TYPE,{0} },                                                // readLockEnabled
        { 0x06,1,VALUE_TYPE,{0} },                                                // writeLockEnabled
        { 0x07,1,VALUE_TYPE,{0} },                                                // readLocked
        { 0x08,1,VALUE_TYPE,{0} },                                                // writeLocked
        { 0x09,sizeof(G3.b.mLckLocking_Tbl.val[0].lockOnReset),LIST_TYPE,{0} },   // LockOnReset
        { 0x0A,8,UID_TYPE,{0} },                                                  // ActiveKey
    #if TCG_FS_CONFIG_NS
        { 0x14,sizeof(G3.b.mLckLocking_Tbl.val[0].namespaceId),VALUE_TYPE,{0} },  // NamespaceID
        { 0x15,sizeof(G3.b.mLckLocking_Tbl.val[0].namespaceGRange),VALUE_TYPE,{0} },  // NamespaceGlobalRange
    #endif
    },
    {
    #if _TCG_ != TCG_PYRITE
      #if TCG_FS_CONFIG_NS
        {UID_Locking_GRange, "Locking_GlobalRange", "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_GRange_Key, 0, 1},
        {UID_Locking_Range1, "Locking_Range1",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range1_Key, 0, 0},
        {UID_Locking_Range2, "Locking_Range2",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range2_Key, 0, 0},
        {UID_Locking_Range3, "Locking_Range3",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range3_Key, 0, 0},
        {UID_Locking_Range4, "Locking_Range4",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range4_Key, 0, 0},
        {UID_Locking_Range5, "Locking_Range5",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range5_Key, 0, 0},
        {UID_Locking_Range6, "Locking_Range6",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range6_Key, 0, 0},
        {UID_Locking_Range7, "Locking_Range7",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range7_Key, 0, 0},
        {UID_Locking_Range8, "Locking_Range8",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range8_Key, 0, 0}
      #else
        {UID_Locking_GRange, "Locking_GlobalRange", "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_GRange_Key},
        {UID_Locking_Range1, "Locking_Range1",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range1_Key},
        {UID_Locking_Range2, "Locking_Range2",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range2_Key},
        {UID_Locking_Range3, "Locking_Range3",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range3_Key},
        {UID_Locking_Range4, "Locking_Range4",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range4_Key},
        {UID_Locking_Range5, "Locking_Range5",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range5_Key},
        {UID_Locking_Range6, "Locking_Range6",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range6_Key},
        {UID_Locking_Range7, "Locking_Range7",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range7_Key},
        {UID_Locking_Range8, "Locking_Range8",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range8_Key}
      #endif
    #else
        {UID_Locking_GRange,      "Locking_GlobalRange", "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0},UID_Null},
    #endif
    }
},


//__align(4) const sMbrCtrl_Tbl cLckMbrCtrl_Tbl=
{
    { // hdr
        sizeof(sMbrCtrl_Tbl),
        sizeof(G3.b.mLckMbrCtrl_Tbl.pty) / sizeof(sColPrty),                  // colCnt
        0x03,                                                                 // maxCol
        sizeof(G3.b.mLckMbrCtrl_Tbl.val) / sizeof(sMbrCtrl_TblObj),           // rowCnt
        sizeof(sMbrCtrl_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x01,sizeof(G3.b.mLckMbrCtrl_Tbl.val[0].enable),VALUE_TYPE,{0} },       // Enable
        { 0x02,sizeof(G3.b.mLckMbrCtrl_Tbl.val[0].done),VALUE_TYPE,{0} },         // Done
        { 0x03,sizeof(G3.b.mLckMbrCtrl_Tbl.val[0].doneOnReset),LIST_TYPE,{0} }    // DoneOnReset
    },
    {
        {UID_MBRControl, 0, 0, {1,PowerCycle,0,0}}
    }

},

#if _TCG_ != TCG_PYRITE
//__align(4) const sKAES_Tbl cLckKAES_256_Tbl =
{
    { // hdr
        sizeof(sKAES_Tbl),
        sizeof(G3.b.mLckKAES_256_Tbl.pty) / sizeof(sColPrty),                 // colCnt
        0x05,                                                                 // maxCol
        sizeof(G3.b.mLckKAES_256_Tbl.val) / sizeof(sKAES_TblObj),             // rowCnt
        sizeof(sKAES_TblObj),
    },
    { // ColPty
        { 0x00,8,UID_TYPE,{0} },                                              // UID
    #if 0    
        { 0x03,sizeof(G3.b.mLckKAES_256_Tbl.val[0].key),FBYTE_TYPE,{0} },
        { 0x04,sizeof(G3.b.mLckKAES_256_Tbl.val[0].mode),VALUE_TYPE,{0} }     // mode
	#else
		{ 0x03,sizeof(G3.b.mLckKAES_256_Tbl.val[0].key1),FBYTE_TYPE,{0} },
		{ 0x04,sizeof(G3.b.mLckKAES_256_Tbl.val[0].key2),FBYTE_TYPE,{0} },
		{ 0x05,sizeof(G3.b.mLckKAES_256_Tbl.val[0].mode),VALUE_TYPE,{0} }     // mode
	#endif
		
    },
	#if 0 //ECB
	{
        {UID_K_AES_256_GRange_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range1_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range2_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range3_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range4_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range5_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range6_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range7_Key, {0,0,0,0,0,0,0,0},  AES_ECB},
        {UID_K_AES_256_Range8_Key, {0,0,0,0,0,0,0,0},  AES_ECB}
    }
	#else
	{
        {UID_K_AES_256_GRange_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range1_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range2_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range3_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range4_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range5_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range6_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range7_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS},
        {UID_K_AES_256_Range8_Key, {0,0,0,0,0,0,0,0}, {0,0}, {0,0,0,0,0,0,0,0}, {0,0},   AES_XTS}
    }
	#endif
},

//__align(4) const sWrappedKey mWrappedKey[LOCKING_RANGE_CNT+1]=
	{
		//	 (U32)idx, (U32)state, (U32)icv[2], (U32)sDEK[LOCKING_RANGE_CNT+1]
		// modified for CNL: (U16)nsid, (U16)range, (U32)state...  // (U64)nsze,
		{ 0x00, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x01, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x02, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x03, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x04, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x05, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x06, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x07, { 0, 0, 0, 0, 0, 0, 0, 0 } },
		{ 0x08, { 0, 0, 0, 0, 0, 0, 0, 0 } },
#if TCG_FS_CONFIG_NS
		{ 0x0000, 		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
		{ 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
		{ 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
		{ 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
		{ 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
		{ 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
		{ 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
#endif
	},


/*
{
	//	 (U32)idx, (U32)state, (U32)icv[2], (U32)sDEK[LOCKING_RANGE_CNT+1]
	// modified for CNL: (U16)nsid, (U16)range, (U32)state...  // (U64)nsze,
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
#if TCG_FS_CONFIG_NS
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
	{ 0x0000, 0x0000, 0x0000,		{ { 0 }, { 0 }, { 0 }, { 0 } }, 	   { 0 } },
#endif
}


//__align(4) const sWrappedKey mWrappedKey[LOCKING_RANGE_CNT+1]=
{
    //   (U32)idx, (U32)state, (U32)icv[2], (U32)sDEK[LOCKING_RANGE_CNT+1]
    // modified for CNL: (U16)nsid, (U16)range, (U32)state...  // (U64)nsze,
    { 0x01, 0x00, 0x0000, {
                            { 0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888 },   // (U32)AES_XTS[8]
                            { 0 },                                                                                                // (U32)icv1[2]
                            { 0x12345678, 0x23456781, 0x34567812, 0x45678123, 0x56781234, 0x67812345, 0x78123456, 0x81234567 },   // (U32)XTS_Key[8]
                            { 0 }                                                                                                 // (U32)icv2[2]
                          },
                          { 0 } // (U32)Salt[8]
    },

    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
#if TCG_FS_CONFIG_NS
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
#endif
},

//__align(4) const sWrappedKey mOpalWrapKEK[TCG_AdminCnt + TCG_UserCnt + 1]=
{
    //                  (U32)idx, (S32)state, (U32)OpalKEK[8], (U32)icv[2], (U32)Salt[8]
    { (u32)UID_Authority_Anybody, 0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_Admin1,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_Admin2,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_Admin3,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_Admin4,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User1,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User2,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User3,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User4,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User5,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User6,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User7,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User8,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_User9,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_AtaMst,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (u32)UID_Authority_AtaUsr,  0x0000,  { 0 },           { 0 },           { 0 } },
},
#elif (_TCG_ == TCG_PYRITE) && CO_SUPPORT_AES
//__align(4) const sWrappedKey mWrappedKey[LOCKING_RANGE_CNT+1]=
{
    //                     (U32)idx, (S32)state, (U32)icv[4], (U32)sDEK[LOCKING_RANGE_CNT+1]
    // modified for CNL: (U16)nsid, (U16)range,  (U32)state...  //
    { 0x01, 0x00, 0x0000, {
                            { 0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888 },   // (U32)AES_XTS[8]
                            { 0 },                                                                                                // (U32)icv1[2]
                            { 0x12345678, 0x23456781, 0x34567812, 0x45678123, 0x56781234, 0x67812345, 0x78123456, 0x81234567 },   // (U32)XTS_Key[8]
                            { 0 }                                                                                                 // (U32)icv2[2]
                          },
                          { 0 } // (U32)Salt[8]
    },
},

//__align(4) const sWrappedKey mOpalWrapKEK[TCG_AdminCnt + TCG_UserCnt + 1]=
{
    //                  (U32)idx, (S32)state, (U32)OpalKEK[8] (U32)icv[2]; (U32)Salt[8]
    { (u32)UID_Authority_Anybody, 0x0000,  { 0 },          { 0 },       { 0 } },
    { (u32)UID_Authority_Admin1,  0x0000,  { 0 },          { 0 },       { 0 } },
    { (u32)UID_Authority_User1,   0x0000,  { 0 },          { 0 },       { 0 } },
    { (u32)UID_Authority_User2,   0x0000,  { 0 },          { 0 },       { 0 } },
    { (u32)UID_Authority_AtaMst,  0x0000,  { 0 },          { 0 },       { 0 } },
    { (u32)UID_Authority_AtaUsr,  0x0000,  { 0 },          { 0 },       { 0 } },
},
*/
#endif

    TCG_END_TAG
} };





#else

#include "sect.h"
#include "ipc.h"
#include "customer.h"
#include "FeaturesDef.h"
#include "nvme_spec.h"
#include "MemAlloc.h"
#include "SharedVars.h"
#include "ErrorCodes.h"
// #include "tcg_sys_info.h"
#include "tcgcommon.h"
#include "SysInfo.h"
#include "tcgtbl.h"

/// ********************************************************///
///                     Admin Table Init                    ///
/// ********************************************************///
tcg_tbl ALIGNED(16) tG1 G1= { {

//__align(4) const sTcgTblInfo cTcgTblInfo =
{ TCG_TBL_ID, TCG_G1_TAG + TCG_TBL_VER },

/* AdminSP Tables */

//__align(4) const sSPInfo_Tbl cAdmSPInfo_Tbl =
{
    { // hdr
        sizeof(sSPInfo_Tbl),
        sizeof(G1.b.mAdmSPInfo_Tbl.pty) / sizeof(sColPrty),         // ColCnt
        6,                                                          // maxCol
        sizeof(G1.b.mAdmSPInfo_Tbl.val) / sizeof(sSPInfo_TblObj),   // RowCnt
        sizeof(sSPInfo_TblObj),                                     // TblObj size
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },      // UID
        { 0x01,8,UID_TYPE,{0} },      // SPID
        { 0x02,sizeof(G1.b.mAdmSPInfo_Tbl.val[0].name),STRING_TYPE,{0} },   // Name, cannot use "sizeof(sSPInfo_TblObj.name)"
        { 0x05,sizeof(G1.b.mAdmSPInfo_Tbl.val[0].spSessionTimeout),VALUE_TYPE,{0}},
        { 0x06,sizeof(G1.b.mAdmSPInfo_Tbl.val[0].enabled),VALUE_TYPE,{0} }  // Enabled (bool)
    },
    { // val
        {UID_SPInfo, UID_SP_Admin, "Admin", 0, true}
    }
},

//__align(4) const sSPTemplates_Tbl cAdmSPTemplates_Tbl =
{
    { // hdr
        sizeof(sSPTemplates_Tbl),
        sizeof(G1.b.mAdmSPTemplates_Tbl.pty) / sizeof(sColPrty),                    // colCnt
        3,                                                                          // maxCol
        sizeof(G1.b.mAdmSPTemplates_Tbl.val) / sizeof(sSPTemplates_TblObj),         // rowCnt
        sizeof(sSPTemplates_TblObj),                                                // objSize
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },     //UID
        { 0x01,8,UID_TYPE,{0} },     //TemplateID
        { 0x02,sizeof(G1.b.mAdmSPTemplates_Tbl.val[0].name),STRING_TYPE,{0} },    // Name
        { 0x03,sizeof(G1.b.mAdmSPTemplates_Tbl.val[0].version),FBYTE_TYPE,{0} }   // Enabled
    },
    { // val
        {UID_SPTemplate_1, UID_Template_Base,  "Base",  {0x00,0x00,0x00,0x02}},
        {UID_SPTemplate_2, UID_Template_Admin, "Admin", {0x00,0x00,0x00,0x02}}
    }
},

//__align(4) const sTbl_Tbl cAdmTbl_Tbl =
{
    { // hdr
        sizeof(sTbl_Tbl),
        sizeof(G1.b.mAdmTbl_Tbl.pty) / sizeof(sColPrty),                        // colCnt
        0x0e,                                                                   // maxCol
        sizeof(G1.b.mAdmTbl_Tbl.val) / sizeof(sTbl_TblObj),                     // rowCnt
        sizeof(sTbl_TblObj),                                                    // objSize
    },
    { // pty
        { 0x00, 8,                                            UID_TYPE,{0} },     // UID
        { 0x01, sizeof(G1.b.mAdmTbl_Tbl.val[0].name),         STRING_TYPE,{0} },  // Name
        { 0x04, sizeof(G1.b.mAdmTbl_Tbl.val[0].kind),         VALUE_TYPE,{0} },   // Kind (Object or Byte)
        { 0x0D, sizeof(G1.b.mAdmTbl_Tbl.val[0].mGranularity), VALUE_TYPE,{0} },   // MaxSize
        { 0x0E, sizeof(G1.b.mAdmTbl_Tbl.val[0].rGranularity), VALUE_TYPE,{0} }    // MaxSize
    },
    { // val
        {UID_Table_Table,           "Table",            TBL_K_OBJECT, 0x00, 0x00},  // R1: Table
        {UID_Table_SPInfo,          "SPInfo",           TBL_K_OBJECT, 0x00, 0x00},  // R2: SPInfo
        {UID_Table_SPTemplates,     "SPTemplates",      TBL_K_OBJECT, 0x00, 0x00},  // R3:
        {UID_Table_MethodID,        "MethodID",         TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_AccessControl,   "AccessControl",    TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_ACE,             "ACE",              TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_Authority,       "Authority",        TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_CPIN,            "C_Pin",            TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_TPerInfo,        "TPerInfo",         TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_Template,        "Template",         TBL_K_OBJECT, 0x00, 0x00},
        {UID_Table_SP,              "SP",               TBL_K_OBJECT, 0x00, 0x00},
#if (_TCG_ == TCG_PYRITE)
        {UID_Table_RemovalMechanism, "DataRemovalMechanism", TBL_K_OBJECT, 0x00, 0x00},
#endif
    },
},

//__align(4) const sMethod_Tbl cAdmMethod_Tbl =
{
    { // hdr
        sizeof(sMethod_Tbl),
        sizeof(G1.b.mAdmMethod_Tbl.pty) / sizeof(sColPrty),                 // colCnt
        3,                                                                  // maxCol
        sizeof(G1.b.mAdmMethod_Tbl.val) / sizeof(sMethod_TblObj),           // rowCnt
        sizeof(sMethod_TblObj),                                             // objSize
    },
    { // pty
        { 0x00, 8,                                       UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmMethod_Tbl.val[0].name), STRING_TYPE,{0} }    // Kind (Object or Byte)
    },
    { // val
        {UID_MethodID_Next,         "Next"},            // R1:
        {UID_MethodID_GetACL,       "GetACL"},
        {UID_MethodID_Get,          "Get"},
        {UID_MethodID_Set,          "Set"},
        {UID_MethodID_Authenticate, "Authenticate"},
        {UID_MethodID_Revert,       "Revert"},
        {UID_MethodID_Activate,     "Activate"},
        {UID_MethodID_Random,       "Random"}
    }
},

//__align(4) const sAccessCtrl_Tbl cAdmAccessCtrl_Tbl =
{
    { // ThisSP: + 2 rows
        {UID_ThisSP,    UID_MethodID_Authenticate,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ThisSP,    UID_MethodID_Random,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    { // Table: 12 rows
        {UID_Table,     UID_MethodID_Next,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R1: Table.Next
        {UID_Table_Table, UID_MethodID_Get,         {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R2: TableObj.Get
        {UID_Table_SPInfo, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R3: TableObj.Get
        {UID_Table_SPTemplates, UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R4: TableObj.Get
        {UID_Table_MethodID, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R5: TableObj.Get
        {UID_Table_AccessControl, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R6: TableObj.Get
        {UID_Table_ACE, UID_MethodID_Get,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R7: TableObj.Get
        {UID_Table_Authority, UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R8: TableObj.Get
        {UID_Table_CPIN, UID_MethodID_Get,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R9: TableObj.Get
        {UID_Table_TPerInfo, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R10: TableObj.Get
        {UID_Table_Template, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R11: TableObj.Get
        {UID_Table_SP, UID_MethodID_Get,            {UID_ACE_Anybody,0,0}, UID_ACE_Anybody}, // R12: TableObj.Get
    #if (_TCG_ == TCG_PYRITE)
        {UID_Table_RemovalMechanism, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // SPInfo: 1 row
        {UID_SPInfo,    UID_MethodID_Get,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    { // SPTemplates: 3 row
        {UID_SPTemplate,  UID_MethodID_Next,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_1, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_2, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    { // MethodID: 9 rows
        {UID_MethodID, UID_MethodID_Next,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},    // R1: Table.Next
        {UID_MethodID_Next, UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_GetACL,UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Get, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Set, UID_MethodID_Get,        {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Authenticate,UID_MethodID_Get,{UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Revert, UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Activate,UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Random,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    {   // ACE: 10 rows + 2 rows
        {UID_ACE, UID_MethodID_Next,                {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_Anybody, UID_MethodID_Get,         {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_Admin, UID_MethodID_Get,           {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_Set_Enabled, UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_SID_Get_NOPIN, UID_MethodID_Get,{UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_SID_Set_PIN, UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_MSID_Get_PIN, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_CPIN_Admins_Set_PIN,UID_MethodID_Get,{UID_ACE_Anybody,0,0},UID_ACE_Anybody},
        {UID_ACE_TPerInfo_Set_PReset,UID_MethodID_Get,{UID_ACE_Anybody,0,0},UID_ACE_Anybody},
        {UID_ACE_SP_SID, UID_MethodID_Get,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #if TCG_FS_PSID
        {UID_ACE_CPIN_Get_PSID_NoPIN, UID_MethodID_Get, {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_ACE_SP_PSID,       UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #endif
    #if (_TCG_ == TCG_PYRITE)
        {UID_ACE_RMMech_Set_RM, UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #endif
    },
    {   // Authority: 6 rows + 2 rows
        {UID_Authority, UID_MethodID_Next,          {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_Anybody,UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_Admins, UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Makers, UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_Makers, UID_MethodID_Set,    {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
    #endif
        {UID_Authority_SID,     UID_MethodID_Get,   {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_SID,     UID_MethodID_Set,   {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin1,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin1,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin2,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin2,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_AdmAdmin3,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin3,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin4,UID_MethodID_Get,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Authority_AdmAdmin4,UID_MethodID_Set,  {UID_ACE_Set_Enabled,0,0}, UID_ACE_Anybody},
    #endif
    #if TCG_FS_PSID
        {UID_Authority_PSID,    UID_MethodID_Get,   {UID_ACE_Anybody,0,0},            UID_ACE_Anybody},
    #endif
    },
    {   // CPIN: 4 rows + 2 rows
        {UID_CPIN,              UID_MethodID_Next,  {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},             // R1: CPIN.Next
        {UID_CPIN_SID,          UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},  // R2: CPIN_SID.Get
        {UID_CPIN_SID,          UID_MethodID_Set,   {UID_ACE_CPIN_SID_Set_PIN,0,0}, UID_ACE_Anybody},    // R3: CPIN_SID.Set
        {UID_CPIN_MSID,         UID_MethodID_Get,   {UID_ACE_CPIN_MSID_Get_PIN,0,0}, UID_ACE_Anybody},   // R4: CPIN_MSID.Get
        {UID_CPIN_AdmAdmin1,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin1,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin2,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin2,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_CPIN_AdmAdmin3,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin3,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin4,    UID_MethodID_Get,   {UID_ACE_CPIN_SID_Get_NOPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_AdmAdmin4,    UID_MethodID_Set,   {UID_ACE_CPIN_Admins_Set_PIN,0,0},UID_ACE_Anybody},
    #endif
    #if TCG_FS_PSID
        {UID_CPIN_PSID,         UID_MethodID_Get,   {UID_ACE_CPIN_Get_PSID_NoPIN,0,0}, UID_ACE_Anybody},
        {UID_CPIN_PSID,         UID_MethodID_Set,   {UID_ACE_CPIN_SID_Set_PIN,0,0},   UID_ACE_Anybody},
    #endif
    },
    {   // TPerInfo: 1 row + 1 row
        {UID_TPerInfo,      UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_TPerInfo,      UID_MethodID_Set,       {UID_ACE_TPerInfo_Set_PReset,0,0}, UID_ACE_Anybody},
    },
    {   // Template: 4 rows
        {UID_Template,      UID_MethodID_Next,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Template_Base, UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Template_Admin, UID_MethodID_Get,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_Template_Locking,UID_MethodID_Get,     {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
    },
    {   // SP: 7 rows
        {UID_SP,            UID_MethodID_Next,      {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SP_Admin,      UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SP_Locking,    UID_MethodID_Get,       {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_SP_Admin,      UID_MethodID_Revert,    {UID_ACE_SP_SID,UID_ACE_Admin,UID_ACE_SP_PSID},  UID_ACE_Anybody},
        {UID_SP_Locking,    UID_MethodID_Revert,    {UID_ACE_SP_SID,UID_ACE_Admin,0},  UID_ACE_Anybody},
        {UID_SP_Admin,      UID_MethodID_Activate,  {UID_ACE_SP_SID,0,0},  UID_ACE_Anybody},
        {UID_SP_Locking,    UID_MethodID_Activate,  {UID_ACE_SP_SID,0,0}, UID_ACE_Anybody},
    },
    #if (_TCG_ == TCG_PYRITE)
    { // RemovalMechanism:
        {UID_RemovalMechanism, UID_MethodID_Get,    {UID_ACE_Anybody,0,0}, UID_ACE_Anybody},
        {UID_RemovalMechanism, UID_MethodID_Set,    {UID_ACE_RMMech_Set_RM,0,0}, UID_ACE_Anybody},
    },
    #endif
    { // ColPty
        {0x01, 8,                                           UID_TYPE,{0}},      // InvokingID
        {0x02, 8,                                           UID_TYPE,{0}},      // MethodID
        {0x04, sizeof(G1.b.mAdmAxsCtrl_Tbl.thisSP[0].acl),  UIDLIST_TYPE,{0}},  // ACL
        {0x08, 8,                                           UID_TYPE,{0}}       // GetACLACL
    },
    #if _TCG_==TCG_PYRITE
        sizeof(G1.b.mAdmAxsCtrl_Tbl.pty) / sizeof(sColPrty),                 // ColCnt
        (sizeof(G1.b.mAdmAxsCtrl_Tbl.thisSP) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.table) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spInfo) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spTemplate) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.method) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.ace) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.authority) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.cpin) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.tperInfo) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.templateTbl) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.sp) +   \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.removalMsm)) \
        / sizeof(sAxsCtrl_TblObj),       // RowCnt
    #else
        sizeof(G1.b.mAdmAxsCtrl_Tbl.pty) / sizeof(sColPrty),                 // ColCnt
        (sizeof(G1.b.mAdmAxsCtrl_Tbl.thisSP) +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.table)  +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spInfo) +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.spTemplate) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.method) +    \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.ace) +       \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.authority) + \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.cpin) +      \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.tperInfo) +  \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.templateTbl) +  \
        sizeof(G1.b.mAdmAxsCtrl_Tbl.sp) )        \
        / sizeof(sAxsCtrl_TblObj),       // RowCnt
    #endif
        sizeof(sAxsCtrl_TblObj),
        0x0e
},

//__align(4) const sACE_Tbl cAdmACE_Tbl =
{
    { // hdr
        sizeof(sACE_Tbl),
        sizeof(G1.b.mAdmACE_Tbl.pty) / sizeof(sColPrty),                        // colCnt
        0x04,                                                                   // maxCol
        sizeof(G1.b.mAdmACE_Tbl.val) / sizeof(sACE_TblObj),                     // RowCnt
        sizeof(sACE_TblObj),
    },
    { // pty
        { 0x00, 8,                                           UID_TYPE,{0} },      // UID
        { 0x03, sizeof(G1.b.mAdmACE_Tbl.val[0].booleanExpr), UIDLIST_TYPE,{0} },  // BooleanExpr
        { 0x04, sizeof(G1.b.mAdmACE_Tbl.val[0].col),         LIST_TYPE,{0} }      // Columns
    },
    {
        {UID_ACE_Anybody,           {UID_Authority_Anybody,0},   {0,}},    // ACE_Anybody, col(All)
        {UID_ACE_Admin,             {UID_Authority_Admins,0},    {0,}},    // ACE_Admin, col(All)
        {UID_ACE_Set_Enabled,       {UID_Authority_SID,0},       {1,5,}},    // ACE_Makers..., b5->col5 (col(Enabled))
        {UID_ACE_CPIN_SID_Get_NOPIN,{UID_Authority_Admins,UID_Authority_SID}, {5,0,4,5,6,7,}}, // ACE_CPIN_SID_Get..., col(UID, CharSet, TryLimit, Tries, Persistence)
        {UID_ACE_CPIN_SID_Set_PIN,  {UID_Authority_SID,0},       {1,3,}},    // ACE_CPIN_SID_Set..., col(PIN)
        {UID_ACE_CPIN_MSID_Get_PIN, {UID_Authority_Anybody,0},   {2,0,3,}},    // ACE_CPIN_MSID_Get..., col(UID,PIN)
        {UID_ACE_CPIN_Admins_Set_PIN,{UID_Authority_Admins,UID_Authority_SID},{1,3,}},
        {UID_ACE_TPerInfo_Set_PReset,{UID_Authority_SID,0},      {1,8,}},
    #if TCG_FS_PSID
        {UID_ACE_CPIN_Get_PSID_NoPIN,{UID_Authority_Anybody,0},  {5,0,4,5,6,7,}},
        {UID_ACE_SP_PSID,           {UID_Authority_PSID,0}, {0,}},
    #endif
        {UID_ACE_SP_SID,            {UID_Authority_SID,0}, {0,}},          // ACE_SP_SID, col(All)
    #if (_TCG_ == TCG_PYRITE)
        {UID_ACE_RMMech_Set_RM,     {UID_Authority_Admins,UID_Authority_SID}, {1,1,}},
    #endif
    // ]
    }
},

//__align(4) const sAuthority_Tbl cAdmAuthority_Tbl =
{
    { // hdr
        sizeof(sAuthority_Tbl),
        sizeof(G1.b.mAdmAuthority_Tbl.pty) / sizeof(sColPrty),                              // colCnt
        0x12,                                                                               // maxCol
        sizeof(G1.b.mAdmAuthority_Tbl.val) / sizeof(sAuthority_TblObj),                     // rowCnt
        sizeof(sAuthority_TblObj),
    },
    { // pty
        { 0x00, sizeof(U64),                                                 UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmAuthority_Tbl.val[0].name),                  STRING_TYPE,{0} },
        { 0x02, sizeof(G1.b.mAdmAuthority_Tbl.val[0].commonName),            STRING_TYPE,{0} },
        { 0x03, sizeof(G1.b.mAdmAuthority_Tbl.val[0].isClass),               VALUE_TYPE,{0} },    // IsClass
        { 0x04, 8,                                                           UID_TYPE,{0} },      // Class
        { 0x05, sizeof(G1.b.mAdmAuthority_Tbl.val[0].enabled),               VALUE_TYPE,{0} },    // Enabled
        { 0x06, sizeof(secure_message),                                      VALUE_TYPE,{0} },    // Secure
        { 0x07, sizeof(hash_protocol),                                       VALUE_TYPE,{0} },    // HashAndSign
        { 0x08, sizeof(G1.b.mAdmAuthority_Tbl.val[0].presentCertificate),    VALUE_TYPE,{0} },    // PresentCertificate (bool)
        { 0x09, sizeof(auth_method),                                         VALUE_TYPE,{0} },    // Operation
        { 0x0A, 8,                                                           UID_TYPE,{0} },      // Credential (UID)
        { 0x0B, 8,                                                           UID_TYPE,{0} },      // ResponseSign
        { 0x0C, 8,                                                           UID_TYPE,{0} }       // ResponseExch
    },
    {
        {UID_Authority_Anybody, "Anybody", "", false,UID_Null, true, SECURE_None, HASH_None, false, AUTH_None,    UID_Null, UID_Null, UID_Null},
        {UID_Authority_Admins,  "Admins",  "", true, UID_Null, true, SECURE_None, HASH_None, false, AUTH_None,    UID_Null, UID_Null, UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Makers,  "Makers",  "", true, UID_Null, true, SECURE_None, HASH_None, false, AUTH_None,    UID_Null, UID_Null, UID_Null},
    #endif
        {UID_Authority_SID,     "SID",     "", false,UID_Null, true, SECURE_None, HASH_None, false, AUTH_Password,UID_CPIN_SID, UID_Null, UID_Null},
        {UID_Authority_AdmAdmin1,"Admin1", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin1,UID_Null,UID_Null},
        {UID_Authority_AdmAdmin2,"Admin2", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin2,UID_Null,UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_AdmAdmin3,"Admin3", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin3,UID_Null,UID_Null},
        {UID_Authority_AdmAdmin4,"Admin4", "", false,UID_Authority_Admins,false,SECURE_None, HASH_None,false,AUTH_Password,UID_CPIN_AdmAdmin4,UID_Null,UID_Null},
    #endif
    #if TCG_FS_PSID
        {UID_Authority_PSID,    "PSID",    "", false,UID_Null, true, SECURE_None, HASH_None, false, AUTH_Password,UID_CPIN_PSID, UID_Null, UID_Null}
    #endif
    }
},

//__align(4) const sCPin_Tbl cAdmCPin_Tbl =
{
    { // hdr
        sizeof(sCPin_Tbl),
        sizeof(G1.b.mAdmCPin_Tbl.pty) / sizeof(sColPrty),        // colCnt
        0x07,                                                    // maxCol
        sizeof(G1.b.mAdmCPin_Tbl.val) / sizeof(sCPin_TblObj),    // rowCnt
        sizeof(sCPin_TblObj),                                    // objSize
    },
    { // pty
        { 0x00, sizeof(G1.b.mAdmCPin_Tbl.val[0].uid),        UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmCPin_Tbl.val[0].name),       STRING_TYPE,{0} },
        { 0x03, sizeof(G1.b.mAdmCPin_Tbl.val[0].cPin),       VBYTE_TYPE,{0} },    // PIN
        { 0x04, sizeof(G1.b.mAdmCPin_Tbl.val[0].charSet),    UID_TYPE,{0} },      // CharSet
        { 0x05, sizeof(G1.b.mAdmCPin_Tbl.val[0].tryLimit),   VALUE_TYPE,{0} },    // TryLimit
        { 0x06, sizeof(G1.b.mAdmCPin_Tbl.val[0].tries),      VALUE_TYPE,{0} },    // Tries
        { 0x07, sizeof(G1.b.mAdmCPin_Tbl.val[0].persistence),VALUE_TYPE,{0} }     // Persistence
    },
    { // val
        {   UID_CPIN_SID, "C_PIN_SID", // uid, name
            { CPIN_IN_RAW, { 'm', 'y', '_', 'M', 'S', 'I', 'D', '_', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' }, { 0 }},
              UID_Null, 5, 0, false }, // charSet, tryLimit, tries, persistence

        {   UID_CPIN_MSID, "C_PIN_MSID",
            { CPIN_IN_RAW, { 'm', 'y', '_', 'M', 'S', 'I', 'D', '_', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' }, { 0 }},
              UID_Null, 0, 0, false },

        {   UID_CPIN_AdmAdmin1, "C_PIN_Admin1", { 0, { 0 }, { 0 }}, UID_Null, 5, 0, false },
        {   UID_CPIN_AdmAdmin2, "C_PIN_Admin2", { 0, { 0 }, { 0 }}, UID_Null, 5, 0, false },
    #if _TCG_ != TCG_PYRITE
        {   UID_CPIN_AdmAdmin3, "C_PIN_Admin3", { 0, { 0 }, { 0 }}, UID_Null, 5, 0, false },
        {   UID_CPIN_AdmAdmin4, "C_PIN_Admin4", { 0, { 0 }, { 0 }}, UID_Null, 5, 0, false },
    #endif
    #if TCG_FS_PSID
        {   UID_CPIN_PSID, "C_PIN_PSID",
            { CPIN_IN_RAW, { 'm', 'y', '_', 'M', 'S', 'I', 'D', '_', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' }, { 0 }},
              UID_Null, 5, 0, false },
    #endif
    },
},

//__align(4) const sTPerInfo_Tbl cAdmTPerInfo_Tbl =
{
    { // hdr
        sizeof(sTPerInfo_Tbl),
        sizeof(G1.b.mAdmTPerInfo_Tbl.pty) / sizeof(sColPrty),                               // colCnt
        0x08,                                                                               // maxCol
        sizeof(G1.b.mAdmTPerInfo_Tbl.val) / sizeof(sTPerInfo_TblObj),                       // rowCnt
        sizeof(sTPerInfo_TblObj),                                                           // objSize
    },
    { // ColPty
        {0x00, 8,                                                       UID_TYPE, {0}},     // UID
        {0x04, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].firmwareVersion),    VALUE_TYPE, {0}},   // UID
        {0x05, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].protocolVersion),    VALUE_TYPE, {0}},   // ProtocolVersion
        {0x07, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].ssc),                STRINGLIST_TYPE, {0}}, // SSC
        {0x08, sizeof(G1.b.mAdmTPerInfo_Tbl.val[0].preset),             VALUE_TYPE, {0}},
    },
    { // val
        {UID_TPerInfo, TPER_FW_VERSION, 0x01, SSC_STRING, false}                            // double '\0' for String List ends.
    }
},

//__align(4) const sTemplate_Tbl cAdmTemplate_Tbl =
{
    { // hdr
        sizeof(sTemplate_Tbl),
        sizeof(G1.b.mAdmTemplate_Tbl.pty) / sizeof(sColPrty),                       // colCnt
        0x04,                                                                       // maxCol
        sizeof(G1.b.mAdmTemplate_Tbl.val) / sizeof(sTemplate_TblObj),               // rowCnt
        sizeof(sTemplate_TblObj),
    },
    { // pty
        { 0x00, 8,                                                   UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmTemplate_Tbl.val[0].name),           STRING_TYPE,{0} },   // Name
        { 0x02, sizeof(G1.b.mAdmTemplate_Tbl.val[0].revision),       VALUE_TYPE,{0} },    // Revision Number
        { 0x03, sizeof(G1.b.mAdmTemplate_Tbl.val[0].instances),      VALUE_TYPE,{0} },    // Instances
        { 0x04, sizeof(G1.b.mAdmTemplate_Tbl.val[0].maxInstances),   VALUE_TYPE,{0} }     // Max Instances
    },
    { // val
        {UID_Template_Base,   "Base",    1, 2, 2},
        {UID_Template_Admin,  "Admin",   1, 1, 1},
        {UID_Template_Locking,"Locking", 1, 1, 1}
    }
},

//__align(4) const sSP_Tbl cAdmSP_Tbl =
{
    { // hdr
        sizeof(sSP_Tbl),
        sizeof(G1.b.mAdmSP_Tbl.pty) / sizeof(sColPrty),                 // colCnt
        0x07,                                                           // maxCol
        sizeof(G1.b.mAdmSP_Tbl.val) / sizeof(sSP_TblObj),               // RowCnt
        sizeof(sSP_TblObj),
    },
    { // pty
        { 0x00, 8,                                       UID_TYPE,{0} },      // UID
        { 0x01, sizeof(G1.b.mAdmSP_Tbl.val[0].name),     STRING_TYPE,{0} },
        { 0x06, sizeof(life_cycle_state),                VALUE_TYPE,{0} },    // LifeCycle
        { 0x07, sizeof(G1.b.mAdmSP_Tbl.val[0].frozen),   VALUE_TYPE,{0} }     // Frozen (bool)
    },
    {
        {UID_SP_Admin,   "Admin",   manufactured,          false},
        {UID_SP_Locking, "Locking", manufactured_inactive, false}
    }
},

#if (_TCG_ == TCG_PYRITE)
//__align(4) const sRemovalMechanism_Tbl cRemovalMechanism_Tbl =
{
    { // hdr
        sizeof(sRemovalMsm_Tbl),
        sizeof(G1.b.mAdmRemovalMsm_Tbl.pty) / sizeof(sColPrty),             // colCnt
        0x01,                                                               // maxCol
        sizeof(G1.b.mAdmRemovalMsm_Tbl.val) / sizeof(sRemovalMsm_TblObj),   // RowCnt
        sizeof(sRemovalMsm_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },     //UID
        { 0x01,sizeof(G1.b.mAdmRemovalMsm_Tbl.val[0].activeRM),VALUE_TYPE,{0} },   //bool
    },
    {
        {UID_RemovalMechanism, 0x01 }
    },
},
#endif

    TCG_END_TAG

} };

/// ********************************************************///
///                     Locking Table Init                  ///
/// ********************************************************///
/* LockingSP Tables */
tcg_tbl ALIGNED(16) tG2 G2={ {

{ TCG_TBL_ID, TCG_G2_TAG + TCG_TBL_VER },

//__align(4) const sSPInfo_Tbl cLckSPInfo_Tbl =
{
    { // hdr
        sizeof(sSPInfo_Tbl),
        sizeof(G2.b.mLckSPInfo_Tbl.pty) / sizeof(sColPrty),               // colCnt
        0x06,                                                             // maxCol
        sizeof(G2.b.mLckSPInfo_Tbl.val) / sizeof(sSPInfo_TblObj),         // rowCnt
        sizeof(sSPInfo_TblObj),                                           // objSize
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                              // UID
        { 0x01,8,UID_TYPE,{0} },                                              // SPID
        { 0x02,sizeof(G2.b.mLckSPInfo_Tbl.val[0].name),STRING_TYPE,{0} },     // Name, cannot use "sizeof(sSPInfo_TblObj.name)"
        { 0x05,sizeof(G2.b.mLckSPInfo_Tbl.val[0].spSessionTimeout),VALUE_TYPE,{0} },
        { 0x06,sizeof(G2.b.mLckSPInfo_Tbl.val[0].enabled),VALUE_TYPE,{0} }    // Enabled
    },
    {
        {UID_SPInfo, UID_SP_Locking, "Locking", 0, true}
    }
},

//__align(4) const sSPTemplates_Tbl cLckSPTemplates_Tbl =
{
    { // hdr
        sizeof(sSPTemplates_Tbl),
        sizeof(G2.b.mLckSPTemplates_Tbl.pty) / sizeof(sColPrty),              // colCnt
        0x03,                                                                 // maxCol
        sizeof(G2.b.mLckSPTemplates_Tbl.val) / sizeof(sSPTemplates_TblObj),   // owCnt
        sizeof(sSPTemplates_TblObj),                                          // objSize
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x01,8,UID_TYPE,{0} },                                                  // TemplateID
        { 0x02,sizeof(G2.b.mLckSPTemplates_Tbl.val[0].name),STRING_TYPE,{0} },    // Name
        { 0x03,sizeof(G2.b.mLckSPTemplates_Tbl.val[0].version),FBYTE_TYPE,{0} }   // Version
    },
    {
        { UID_SPTemplate_1, UID_Template_Base,   "Base",   {0x00,0x00,0x00,0x02} },
        { UID_SPTemplate_2, UID_Template_Locking,"Locking",{0x00,0x00,0x00,0x02} }
    }
},


//__align(4) const sLckTbl_Tbl cLckTbl_Tbl =
{
    { // hdr
        sizeof(sLckTbl_Tbl),
        sizeof(G2.b.mLckTbl_Tbl.pty) / sizeof(sColPrty),                      // colCnt
        0x0E,                                                                 // maxCol
        sizeof(G2.b.mLckTbl_Tbl.val) / sizeof(sLckTbl_TblObj),                // rowCnt
        sizeof(sLckTbl_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE, {0} },                                              // UID
        { 0x01,sizeof(G2.b.mLckTbl_Tbl.val[0].name),STRING_TYPE,{0} },        // Name
        { 0x04,sizeof(G2.b.mLckTbl_Tbl.val[0].kind),VALUE_TYPE,{0} },         // Kind (Object or Byte)
        { 0x07,sizeof(G2.b.mLckTbl_Tbl.val[0].rows),VALUE_TYPE,{0} },         // Rows
        { 0x0D,sizeof(G2.b.mLckTbl_Tbl.val[0].mGranularity),VALUE_TYPE,{0} },
        { 0x0E,sizeof(G2.b.mLckTbl_Tbl.val[0].rGranularity),VALUE_TYPE,{0} }  // MaxSize
    },
    {
    #if _TCG_ != TCG_PYRITE
        {UID_Table_Table,       "Table",        TBL_K_OBJECT, 15+DSTBL_MAX_NUM-1,     0x00,   0x00},  // R1: Table
        {UID_Table_SPInfo,      "SPInfo",       TBL_K_OBJECT, 1,                      0x00,   0x00},  // R2:
        {UID_Table_SPTemplates, "SPTemplates",  TBL_K_OBJECT, 2,                      0x00,   0x00},  // R3:
        {UID_Table_MethodID,    "MethodID",     TBL_K_OBJECT, LCK_METHOD_TBLOBJ_CNT,  0x00,   0x00},
        {UID_Table_AccessControl,"AccessControl",TBL_K_OBJECT,LCK_ACCESSCTRL_TBLOBJ_CNT,0x00,0x00},
        {UID_Table_ACE,         "ACE",          TBL_K_OBJECT, LCK_ACE_TBLOBJ_CNT,     0x00,   0x00},
        {UID_Table_Authority,   "Authority",    TBL_K_OBJECT, LCK_AUTHORITY_TBLOBJ_CNT,0x00,   0x00},
        {UID_Table_CPIN,        "C_Pin",        TBL_K_OBJECT, LCK_CPIN_TBLOBJ_CNT,    0x00,   0x00},  // row
        {UID_Table_SecretProtect,"SecretProtect",TBL_K_OBJECT,1,                      0x00,   0x00},
        {UID_Table_LockingInfo, "LockingInfo",  TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_Locking,     "Locking",      TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_MBRControl,  "MBRControl",   TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_MBR,         "MBR",          TBL_K_BYTE,   MBR_LEN,                0x01,   0x01},  // VU/VU
        {UID_Table_K_AES_256,   "K_AES_256",    TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_DataStore,   "DataStore",    TBL_K_BYTE,   DATASTORE_LEN,          0x01,   0x01},  // VU/VU
        {UID_Table_DataStore2|UID_FF,  "DataStore2",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore3|UID_FF,  "DataStore3",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore4|UID_FF,  "DataStore4",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore5|UID_FF,  "DataStore5",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore6|UID_FF,  "DataStore6",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore7|UID_FF,  "DataStore7",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore8|UID_FF,  "DataStore8",   TBL_K_BYTE,   0x00,            0x01,   0x01},  // VU/VU
        {UID_Table_DataStore9|UID_FF,  "DataStore9",   TBL_K_BYTE,   0x00,            0x01,   0x01}   // VU/VU
    #else
        {UID_Table_Table,       "Table",        TBL_K_OBJECT, 13,                     0x00,   0x00},  // R1: Table
        {UID_Table_SPInfo,      "SPInfo",       TBL_K_OBJECT, 1,                      0x00,   0x00},  // R2:
        {UID_Table_SPTemplates, "SPTemplates",  TBL_K_OBJECT, 2,                      0x00,   0x00},  // R3:
        {UID_Table_MethodID,    "MethodID",     TBL_K_OBJECT, LCK_METHOD_TBLOBJ_CNT,  0x00,   0x00},
        {UID_Table_AccessControl,"AccessControl",TBL_K_OBJECT,LCK_ACCESSCTRL_TBLOBJ_CNT,0x00, 0x00},
        {UID_Table_ACE,         "ACE",          TBL_K_OBJECT, LCK_ACE_TBLOBJ_CNT,     0x00,   0x00},
        {UID_Table_Authority,   "Authority",    TBL_K_OBJECT, LCK_AUTHORITY_TBLOBJ_CNT,0x00,  0x00},
        {UID_Table_CPIN,        "C_Pin",        TBL_K_OBJECT, LCK_CPIN_TBLOBJ_CNT,    0x00,   0x00},  // row
      //{UID_Table_SecretProtect,"SecretProtect",TBL_K_OBJECT,1,                      0x00,   0x00},
        {UID_Table_LockingInfo, "LockingInfo",  TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_Locking,     "Locking",      TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_MBRControl,  "MBRControl",   TBL_K_OBJECT, 1,                      0x00,   0x00},
        {UID_Table_MBR,         "MBR",          TBL_K_BYTE,   MBR_LEN,                0x01,   0x01},  // VU/VU
      //{UID_Table_K_AES_256,   "K_AES_256",    TBL_K_OBJECT, LOCKING_RANGE_CNT+1,    0x00,   0x00},  // row
        {UID_Table_DataStore,   "DataStore",    TBL_K_BYTE,   DATASTORE_LEN,          0x01,   0x01},  // VU/VU
    #endif
    }
},

//__align(4) const sLckMethod_Tbl cLckMethod_Tbl =
{
    { // hdr
        sizeof(sLckMethod_Tbl),
        sizeof(G2.b.mLckMethod_Tbl.pty) / sizeof(sColPrty),               // colCnt
        0x03,                                                             // maxCol
        sizeof(G2.b.mLckMethod_Tbl.val) / sizeof(sMethod_TblObj),         // rowCnt
        sizeof(sMethod_TblObj),
    },
    { // ColPty
        { 0x00,8,UID_TYPE,{0} },                                          // UID
        { 0x01,sizeof(G2.b.mLckMethod_Tbl.val[0].name),STRING_TYPE,{0} }  // Kind (Object or Byte)
    },
    { // val
        { UID_MethodID_Next,         "Next"},        // R1:
        { UID_MethodID_GetACL,       "GetACL"},
    #if _TCG_ != TCG_PYRITE
        { UID_MethodID_GenKey,       "GenKey"},
    #endif
        { UID_MethodID_RevertSP,     "RevertSP"},
        { UID_MethodID_Get,          "Get"},
        { UID_MethodID_Set,          "Set"},
        { UID_MethodID_Authenticate, "Authenticate"},
        { UID_MethodID_Random,       "Random"},
    #if _TCG_ != TCG_PYRITE
        { UID_MethodID_Reactivate,   "Reactivate"},
        { UID_MethodID_Erase,        "Erase"},
      #if TCG_FS_CONFIG_NS
        { UID_MethodID_Assign,       "Assign" },
        { UID_MethodID_Deassign,     "Deassign"},
      #endif
    #endif
    }
},

//__align(4) const sLckAccessCtrl_Tbl cLckAccessCtrl_Tbl =
{
    { // SP: 3 row + 1 rows
        {UID_ThisSP,    UID_MethodID_RevertSP,      {UID_ACE_Admin,  0,0,0},UID_ACE_Anybody},
        {UID_ThisSP,    UID_MethodID_Authenticate,  {UID_ACE_Anybody,0,0,0},UID_ACE_Anybody},
        {UID_ThisSP,    UID_MethodID_Random,        {UID_ACE_Anybody,0,0,0},UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ThisSP,    UID_MethodID_Reactivate,    {UID_ACE_SP_Reactivate_Admin,0,0,0},UID_ACE_Anybody},
    #endif
    },
    { // Table: 15 rows + 1 row
        {UID_Table,             UID_MethodID_Next,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},  // R1: Table.Next
        {UID_Table_Table,       UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},  // R2: TableObj.Get
        {UID_Table_SPInfo,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},  // R3: TableObj.Get
        {UID_Table_SPTemplates, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_MethodID,    UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_AccessControl,UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_ACE,         UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_Authority,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_CPIN,        UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Table_SecretProtect,UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_Table_LockingInfo, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_Locking,     UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_MBRControl,  UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_MBR,         UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Table_K_AES_256,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_Table_DataStore,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Table_DataStore2,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore3,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore4,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore5,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore6,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore7,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore8,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_Table_DataStore9,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // SPInfo: 1 row
        {UID_SPInfo,            UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    },
    { // SPTemplates: 3 row
        {UID_SPTemplate,        UID_MethodID_Next,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_1,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_SPTemplate_2,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    },
    { // MethodID: 7 rows + 2 rows
        {UID_MethodID,          UID_MethodID_Next,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},   // R1: Method.Next
        {UID_MethodID_Next,     UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_GetACL,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_MethodID_GenKey,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_MethodID_RevertSP, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Get,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Set,      UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Authenticate,UID_MethodID_Get,{UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Random,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_MethodID_Reactivate,UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Erase,    UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
      #if TCG_FS_CONFIG_NS
        {UID_MethodID_Assign,   UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MethodID_Deassign, UID_MethodID_Get,   {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
      #endif
    #endif
    },
    { // ACE: 65 rows + 58 rows
        {UID_ACE,               UID_MethodID_Next,          {UID_ACE_Anybody,    0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Anybody,       UID_MethodID_Get,           {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Admin,         UID_MethodID_Get,           {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Anybody_Get_CommonName,UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Admins_Set_CommonName, UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #endif
        {UID_ACE_ACE_Get_All, UID_MethodID_Get,             {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_ACE_Get_All, UID_MethodID_Set,             {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_ACE_Set_BooleanExpression,UID_MethodID_Get,{UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Authority_Get_All, UID_MethodID_Get,       {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_Authority_Get_All, UID_MethodID_Set,       {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Authority_Set_Enabled,UID_MethodID_Get,    {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},

        {UID_ACE_C_PIN_Admins_Get_All_NOPIN,UID_MethodID_Get,{UID_ACE_ACE_Get_All,0,0,0},   UID_ACE_Anybody},
        {UID_ACE_C_PIN_Admins_Set_PIN,  UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User1_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User2_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_C_PIN_User3_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User4_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User5_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User6_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User7_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User8_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_C_PIN_User9_Set_PIN,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
    #endif
        {UID_ACE_C_PIN_User1_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_C_PIN_User2_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_C_PIN_User3_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_C_PIN_User4_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User5_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User6_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User7_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User8_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2
        {UID_ACE_C_PIN_User9_Set_PIN,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},      // to User8 for Opal2

        {UID_ACE_K_AES_256_GlobalRange_GenKey,UID_MethodID_Get,{UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_GlobalRange_GenKey,UID_MethodID_Set,{UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range1_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range1_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range2_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range2_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range3_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range3_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range4_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range4_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range5_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range5_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range6_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range6_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range7_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range7_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_256_Range8_GenKey, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0},    UID_ACE_Anybody},  // to Range8 for opal2
        {UID_ACE_K_AES_256_Range8_GenKey, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_K_AES_Mode,            UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Range1_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Get_RangeStartToActiveKey,UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Get_RangeStartToActiveKey,UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GRange_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Range1_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_RdLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_RdLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GRange_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Range1_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_WrLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_WrLocked, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_Locking_GlblRng_Admins_Set, UID_MethodID_Get,  {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_Admins_RangeStartToLocked, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
      #if TCG_FS_CONFIG_NS
        {UID_ACE_Locking_Namespace_IdtoGlbRng, UID_MethodID_Get, {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Namespace_IdtoGlbRng, UID_MethodID_Set, {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
      #endif
    #endif
        {UID_ACE_MBRControl_Admins_Set, UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_MBRControl_Set_Done,   UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_MBRControl_Set_Done,   UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if (_TCG_ != TCG_PYRITE) && TCG_FS_CONFIG_NS
        {UID_ACE_Assign,                UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Assign,                UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Deassign,              UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Deassign,              UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #endif
        {UID_ACE_DataStore_Get_All,     UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore_Get_All,     UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore_Set_All,     UID_MethodID_Get,   {UID_ACE_ACE_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore_Set_All,     UID_MethodID_Set,   {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE // *** 32 rows
        {UID_ACE_DataStore2_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore2_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore2_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore2_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore3_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore4_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore5_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore6_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore7_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore8_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Get_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Get_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Set_All,    ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_DataStore9_Set_All,    ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},

        {UID_ACE_Locking_GRange_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_ReadToLOR,  ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Set_Range,      ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},

        {UID_ACE_CPIN_Anybody_Get_NoPIN,        ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        { UID_ACE_SP_Reactivate_Admin,          UID_MethodID_Get,   { UID_ACE_Anybody,0,0,0 }, UID_ACE_Anybody },
        { UID_ACE_SP_Reactivate_Admin,          UID_MethodID_Set,   { UID_ACE_ACE_Set_BooleanExpression,0,0,0 }, UID_ACE_Anybody },

        {UID_ACE_Locking_GRange_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_GRange_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range1_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range2_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range3_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range4_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range5_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range6_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range7_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Erase,          ~UID_MethodID_Get,  {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_ACE_Locking_Range8_Erase,          ~UID_MethodID_Set,  {UID_ACE_ACE_Set_BooleanExpression,0,0,0}, UID_ACE_Anybody},
    //#if _TCG_ != TCG_PYRITE
        { UID_ACE_User1_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User2_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User3_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User4_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User5_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User6_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User7_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User8_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User9_Set_CommonName,  UID_MethodID_Get,{ UID_ACE_ACE_Get_All,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User1_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User2_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User3_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User4_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User5_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User6_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User7_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User8_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
        { UID_ACE_User9_Set_CommonName,  UID_MethodID_Set,{ UID_ACE_ACE_Set_BooleanExpression,0,0,0 },    UID_ACE_Anybody },
    //#endif
    #endif
    },
    { // Authority: 28 rows
        {UID_Authority,         UID_MethodID_Next,      {UID_ACE_Anybody,0,0,0},               UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Anybody, UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admins,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin3,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin4,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Users,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User3,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User4,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User5,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User6,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User7,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User8,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_User9,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,UID_ACE_Anybody_Get_CommonName,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_Admin3,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_Admin4,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_Admins_Set_CommonName,0,0},  UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User1_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User2_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User3,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User3_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User4,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User4_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User5,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User5_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User6,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User6_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User7,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User7_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User8,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User8_Set_CommonName,0,0},   UID_ACE_Anybody},
        {UID_Authority_User9,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,UID_ACE_User9_Set_CommonName,0,0},   UID_ACE_Anybody},
    #else
        {UID_Authority_Anybody, UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admins,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Users,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Get,       {UID_ACE_Authority_Get_All,0,0,0},     UID_ACE_Anybody},
        {UID_Authority_Admin1,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
        {UID_Authority_Admin2,  UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
        {UID_Authority_User1,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
        {UID_Authority_User2,   UID_MethodID_Set,       {UID_ACE_Authority_Set_Enabled,0,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // CPIN: 25 rows
        {UID_CPIN,       UID_MethodID_Next, {UID_ACE_Anybody,0,0,0},            UID_ACE_Anybody},           // R1: CPIN.Next
    #if _TCG_ != TCG_PYRITE
        {UID_CPIN_Admin1, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},   // R2:
        {UID_CPIN_Admin1, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin3, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin3, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin4, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin4, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Set, {UID_ACE_C_PIN_User1_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Set, {UID_ACE_C_PIN_User2_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User3,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User3,  UID_MethodID_Set, {UID_ACE_C_PIN_User3_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User4,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User4,  UID_MethodID_Set, {UID_ACE_C_PIN_User4_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User5,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User5,  UID_MethodID_Set, {UID_ACE_C_PIN_User5_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User6,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User6,  UID_MethodID_Set, {UID_ACE_C_PIN_User6_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User7,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User7,  UID_MethodID_Set, {UID_ACE_C_PIN_User7_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User8,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User8,  UID_MethodID_Set, {UID_ACE_C_PIN_User8_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User9,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User9,  UID_MethodID_Set, {UID_ACE_C_PIN_User9_Set_PIN,0,0,0},        UID_ACE_Anybody},
    #else
        {UID_CPIN_Admin1, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin1, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_Admin2, UID_MethodID_Set, {UID_ACE_C_PIN_Admins_Set_PIN,0,0,0},       UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User1,  UID_MethodID_Set, {UID_ACE_C_PIN_User1_Set_PIN,0,0,0},        UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Get, {UID_ACE_C_PIN_Admins_Get_All_NOPIN,0,0,0}, UID_ACE_Anybody},
        {UID_CPIN_User2,  UID_MethodID_Set, {UID_ACE_C_PIN_User2_Set_PIN,0,0,0},        UID_ACE_Anybody},
    #endif
    },
    #if _TCG_ != TCG_PYRITE    // SecretPortect: +2 rows
    {
        {UID_SecretProtect,     UID_MethodID_Next,  { UID_ACE_Anybody,0 }, UID_ACE_Anybody},
        {UID_SecretProtect_256, UID_MethodID_Get,   { UID_ACE_Anybody,0 }, UID_ACE_Anybody},
    },
    #endif

    { // LockingInfo: 1 row
        { UID_LockingInfo,       UID_MethodID_Get,  { UID_ACE_Anybody,0 }, UID_ACE_Anybody },
    },
    { // Locking: 11 rows + 8 rows
        { UID_Locking,           UID_MethodID_Next, { UID_ACE_Anybody,0 }, UID_ACE_Anybody },
    #if _TCG_ != TCG_PYRITE
      #if TCG_FS_CONFIG_NS
        { UID_Locking,           UID_MethodID_Assign,   { UID_ACE_Assign,0 }, UID_ACE_Anybody },
        { UID_Locking,           UID_MethodID_Deassign, { UID_ACE_Assign,0 }, UID_ACE_Anybody },
      #endif
        { UID_Locking_GRange,    UID_MethodID_Get, { UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,0,0}, UID_ACE_Anybody},
        { UID_Locking_GRange,    UID_MethodID_Set, { UID_ACE_Locking_GlblRng_Admins_Set,UID_ACE_Locking_GRange_Set_RdLocked,UID_ACE_Locking_GRange_Set_WrLocked,UID_ACE_Admins_Set_CommonName }, UID_ACE_Anybody },           //ACL: >1
        { UID_Locking_GRange,    ~UID_MethodID_Erase,{ UID_ACE_Locking_GRange_Erase,0 }, UID_ACE_Anybody },

        { UID_Locking_Range1,    UID_MethodID_Get, { UID_ACE_Locking_Range1_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range1,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range1_Set_RdLocked,UID_ACE_Locking_Range1_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range1,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range1_Erase,0 }, UID_ACE_Anybody },

        { UID_Locking_Range2,    UID_MethodID_Get, { UID_ACE_Locking_Range2_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range2,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range2_Set_RdLocked,UID_ACE_Locking_Range2_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range2,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range2_Erase,0 }, UID_ACE_Anybody },

        { UID_Locking_Range3,    UID_MethodID_Get, { UID_ACE_Locking_Range3_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range3,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range3_Set_RdLocked,UID_ACE_Locking_Range3_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range3,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range3_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range4,    UID_MethodID_Get, { UID_ACE_Locking_Range4_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range4,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range4_Set_RdLocked,UID_ACE_Locking_Range4_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range4,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range4_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range5,    UID_MethodID_Get, { UID_ACE_Locking_Range5_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range5,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range5_Set_RdLocked,UID_ACE_Locking_Range5_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range5,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range5_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range6,    UID_MethodID_Get, { UID_ACE_Locking_Range6_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range6,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range6_Set_RdLocked,UID_ACE_Locking_Range6_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range6,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range6_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range7,    UID_MethodID_Get, { UID_ACE_Locking_Range7_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        { UID_Locking_Range7,    UID_MethodID_Set,{ UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range7_Set_RdLocked,UID_ACE_Locking_Range7_Set_WrLocked,UID_ACE_Admins_Set_CommonName },    UID_ACE_Anybody }, //ACL: >1
        { UID_Locking_Range7,    ~UID_MethodID_Erase,{ UID_ACE_Locking_Range7_Erase,0 }, UID_ACE_Anybody },

        {UID_Locking_Range8,    UID_MethodID_Get, { UID_ACE_Locking_Range8_Get_RangeStartToActiveKey,UID_ACE_Anybody_Get_CommonName,UID_ACE_Locking_Namespace_IdtoGlbRng,0}, UID_ACE_Anybody},
        {UID_Locking_Range8,    UID_MethodID_Set, { UID_ACE_Locking_Admins_RangeStartToLocked,UID_ACE_Locking_Range8_Set_RdLocked,UID_ACE_Locking_Range8_Set_WrLocked,UID_ACE_Admins_Set_CommonName},    UID_ACE_Anybody}, //ACL: >1
        {UID_Locking_Range8,    ~UID_MethodID_Erase, { UID_ACE_Locking_Range8_Erase,0 }, UID_ACE_Anybody},
    #else
        {UID_Locking_GRange,    UID_MethodID_Get, {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey,0,0,0}, UID_ACE_Anybody},
        {UID_Locking_GRange,    UID_MethodID_Set, {UID_ACE_Locking_GlblRng_Admins_Set,UID_ACE_Locking_GRange_Set_RdLocked,UID_ACE_Locking_GRange_Set_WrLocked,0}, UID_ACE_Anybody}, //ACL: >1
    #endif
    },
    { // MBRControl: 2 rows
        {UID_MBRControl,        UID_MethodID_Get, {UID_ACE_Anybody,0,0,0},               UID_ACE_Anybody},
        {UID_MBRControl,        UID_MethodID_Set, {UID_ACE_MBRControl_Admins_Set,UID_ACE_MBRControl_Set_Done,0,0}, UID_ACE_Anybody}, //ACL>1
    },
    { // MBR: 2 rows
        {UID_MBR,               UID_MethodID_Get, {UID_ACE_Anybody,0,0,0}, UID_ACE_Anybody},
        {UID_MBR,               UID_MethodID_Set, {UID_ACE_Admin,0,0,0},   UID_ACE_Anybody},
    },
    #if _TCG_ != TCG_PYRITE // K_AES: 10 rows + 8 rows
    {
        {UID_K_AES_256_GRange_Key, UID_MethodID_GenKey,{UID_ACE_K_AES_256_GlobalRange_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_GRange_Key, UID_MethodID_Get,   {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range1_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range1_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range1_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range2_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range2_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range2_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range3_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range3_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range3_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range4_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range4_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range4_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range5_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range5_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range5_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range6_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range6_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range6_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range7_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range7_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range7_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
        {UID_K_AES_256_Range8_Key, UID_MethodID_GenKey,     {UID_ACE_K_AES_256_Range8_GenKey,0,0,0}, UID_ACE_Anybody},
        {UID_K_AES_256_Range8_Key, UID_MethodID_Get,        {UID_ACE_K_AES_Mode,0,0,0},              UID_ACE_Anybody},
    },
    #endif

    { // DataStore: 2 rows
        {UID_DataStore,     UID_MethodID_Get,   {UID_ACE_DataStore_Get_All,0,0,0},  UID_ACE_Anybody},
        {UID_DataStore,     UID_MethodID_Set,   {UID_ACE_DataStore_Set_All,0,0,0},  UID_ACE_Anybody},
    #if _TCG_ != TCG_PYRITE
        {UID_DataStore2,    ~UID_MethodID_Get,  {UID_ACE_DataStore2_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore2,    ~UID_MethodID_Set,  {UID_ACE_DataStore2_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore3,    ~UID_MethodID_Get,  {UID_ACE_DataStore3_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore3,    ~UID_MethodID_Set,  {UID_ACE_DataStore3_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore4,    ~UID_MethodID_Get,  {UID_ACE_DataStore4_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore4,    ~UID_MethodID_Set,  {UID_ACE_DataStore4_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore5,    ~UID_MethodID_Get,  {UID_ACE_DataStore5_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore5,    ~UID_MethodID_Set,  {UID_ACE_DataStore5_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore6,    ~UID_MethodID_Get,  {UID_ACE_DataStore6_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore6,    ~UID_MethodID_Set,  {UID_ACE_DataStore6_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore7,    ~UID_MethodID_Get,  {UID_ACE_DataStore7_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore7,    ~UID_MethodID_Set,  {UID_ACE_DataStore7_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore8,    ~UID_MethodID_Get,  {UID_ACE_DataStore8_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore8,    ~UID_MethodID_Set,  {UID_ACE_DataStore8_Set_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore9,    ~UID_MethodID_Get,  {UID_ACE_DataStore9_Get_All,0,0,0}, UID_ACE_Anybody},
        {UID_DataStore9,    ~UID_MethodID_Set,  {UID_ACE_DataStore9_Set_All,0,0,0}, UID_ACE_Anybody},
    #endif
    },
    { // ColPty
        {0x01,8,UID_TYPE,{0}},                                               // InvokingID
        {0x02,8,UID_TYPE,{0}},                                               // MethodID
        {0x04,sizeof(G2.b.mLckAxsCtrl_Tbl.thisSP[0].acl),UIDLIST_TYPE,{0}},  // ACL
        {0x08,8,UID_TYPE,{0}}           //GetACLACL
    },
    sizeof(G2.b.mLckAxsCtrl_Tbl.pty)/sizeof(sColPrty),                   // colCnt
    #if _TCG_==TCG_PYRITE
        (sizeof(G2.b.mLckAxsCtrl_Tbl.thisSP) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.table) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spInfo) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spTemplate) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.method) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.ace) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.authority) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.cpin) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lckingInfo) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lcking) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbrCtrl) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbr) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.datastore))   \
            / sizeof(sAxsCtrl_TblObj),       // rowCnt
    #else
        (sizeof(G2.b.mLckAxsCtrl_Tbl.thisSP) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.table) +      \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spInfo) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.spTemplate) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.method) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.ace) +        \
        sizeof(G2.b.mLckAxsCtrl_Tbl.authority) +  \
        sizeof(G2.b.mLckAxsCtrl_Tbl.cpin) +       \
        sizeof(G2.b.mLckAxsCtrl_Tbl.secretPrtct) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lckingInfo) + \
        sizeof(G2.b.mLckAxsCtrl_Tbl.lcking) +     \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbrCtrl) +    \
        sizeof(G2.b.mLckAxsCtrl_Tbl.mbr) +        \
        sizeof(G2.b.mLckAxsCtrl_Tbl.kaes) +       \
        sizeof(G2.b.mLckAxsCtrl_Tbl.datastore))   \
        / sizeof(sAxsCtrl_TblObj),       // rowCnt
    #endif
        sizeof(sAxsCtrl_TblObj),       // objSize
        0x0e
},

#if _TCG_!=TCG_PYRITE
//__align(4) const sSecretProtect_Tbl cLckSecretProtect_Tbl =
{
    { // hdr
        sizeof(sSecretProtect_Tbl),
        sizeof(G2.b.mLckSecretProtect_Tbl.pty) / sizeof(sColPrty),                     // colCnt
        0x03,                                                                          // maxCol
        sizeof(G2.b.mLckSecretProtect_Tbl.val) / sizeof(sSecretProtect_TblObj),        // RowCnt
        sizeof(sSecretProtect_Tbl),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                           // UID
        { 0x01,8,UID_TYPE,{0} },                                                           // table
        { 0x02,sizeof(G2.b.mLckSecretProtect_Tbl.val[0].colNumber),VALUE_TYPE,{0} },       // colNumber
        { 0x03,sizeof(G2.b.mLckSecretProtect_Tbl.val[0].protectMechanism),LIST_TYPE,{0} }, // protectMechanisms
    },
    {
        { UID_SecretProtect_256, UID_K_AES_256, 0x03, {0x01,0x01,0x00}}      // ProtectMechanisms=1 for eDrive
    }
},
#endif

//__align(4) const sLockingInfo_Tbl cLckLockingInfo_Tbl =
{
    { // hdr
        sizeof(sLockingInfo_Tbl),
        sizeof(G2.b.mLckLockingInfo_Tbl.pty) / sizeof(sColPrty),                       // colCnt
        0x60001,                                                                       // maxCol
        sizeof(G2.b.mLckLockingInfo_Tbl.val) / sizeof(sLockingInfo_TblObj),            // rowCnt
        sizeof(sLockingInfo_TblObj),
    },
    { // ColPty
        { 0x00,8,UID_TYPE,{0} },                                                                  // UID
        { 0x01,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].name),STRING_TYPE,{0} },                    // name
        { 0x02,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].version),VALUE_TYPE,{0} },                  // version
        { 0x03,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].encryptSupport),VALUE_TYPE,{0} },           // encryptSupport
        { 0x04,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].maxRanges),VALUE_TYPE,{0} },                // maxRange
        { 0x07,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].alignentReuired),VALUE_TYPE,{0} },          // AlignmentRequired
        { 0x08,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].logicalBlockSize),VALUE_TYPE,{0} },         // LogicalBlcokSize
        { 0x09,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].alignmentGranularity),VALUE_TYPE,{0} },     // AlignmentGranularity
        { 0x0A,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].lowestAlignedLBA),VALUE_TYPE,{0} },         // LowestAlignedLBA
        { 0x60000,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].singleUserModeRange),UID2_TYPE,{0} },       // 0x60 -> 0x600000
        { 0x60001,sizeof(G2.b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy),VALUE_TYPE,{0} }    // RangeStartLengthPolicy
    },
    {
    #if _TCG_ != TCG_PYRITE
        {UID_LockingInfo, "", 0x01, 0x01, LOCKING_RANGE_CNT, true, LBA_SIZE, TCG_AlignmentGranularity, 0, {0}, 1}  // 512B, 8KB/page
    #else
        {UID_LockingInfo, "", 0x01, 0x00, LOCKING_RANGE_CNT, true, LBA_SIZE, TCG_AlignmentGranularity, 0, {0}, 1 }
    #endif
    }
},

    TCG_END_TAG
} };  // G2 init end

tcg_tbl ALIGNED(16) tG3 G3 ={ {
{    TCG_TBL_ID, TCG_G3_TAG + TCG_TBL_VER    },

//__align(4) const sLckACE_Tbl cLckACE_Tbl =
{
    { // hdr
        sizeof(sLckACE_Tbl),
        sizeof(G3.b.mLckACE_Tbl.pty) / sizeof(sColPrty),                          // colCnt
        0x04,                                                                     // maxCol
        sizeof(G3.b.mLckACE_Tbl.val) / sizeof(sLckACE_TblObj),                    // rowCnt
        sizeof(sLckACE_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x03,sizeof(G3.b.mLckACE_Tbl.val[0].booleanExpr),UIDLIST_TYPE,{0} },    // BooleanExpr
        { 0x04,sizeof(G3.b.mLckACE_Tbl.val[0].col),LIST_TYPE,{0} }                // Columns
    },
    {
        {UID_ACE_Anybody,               { UID_Authority_Anybody, }, {0} }, //ACE_Anybody, col(All)
        {UID_ACE_Admin,                 { UID_Authority_Admins, },  {0} }, //ACE_Admin, col(All)
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Anybody_Get_CommonName,{UID_Authority_Anybody, }, {2,0,2,}},
        {UID_ACE_Admins_Set_CommonName, {UID_Authority_Admins, },  {1,2,}}, //CommonName
    #endif
        {UID_ACE_ACE_Get_All,           {UID_Authority_Admins, },  {0,}},
        {UID_ACE_ACE_Set_BooleanExpression,{UID_Authority_Admins, }, {1,3,}}, //BooleanExpr
        {UID_ACE_Authority_Get_All,     {UID_Authority_Admins, },  {0,}},
        {UID_ACE_Authority_Set_Enabled, {UID_Authority_Admins, },  {1,5,}}, //Enabled

        {UID_ACE_C_PIN_Admins_Get_All_NOPIN,{UID_Authority_Admins, },{5,0,4,5,6,7,}},    // ACE_CPIN_SID_Get..., col(UID, CharSet, TryLimit, Tries, Persistence)
        {UID_ACE_C_PIN_Admins_Set_PIN,  {UID_Authority_Admins, },  {1,3,}},     // ACE_CPIN_SID_Set..., col(PIN)
        {UID_ACE_C_PIN_User1_Set_PIN,   {UID_Authority_Admins,UID_Authority_User1, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User2_Set_PIN,   {UID_Authority_Admins,UID_Authority_User2, },  {1,3,}},   //Boolean>1
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_C_PIN_User3_Set_PIN,   {UID_Authority_Admins,UID_Authority_User3, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User4_Set_PIN,   {UID_Authority_Admins,UID_Authority_User4, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User5_Set_PIN,   {UID_Authority_Admins,UID_Authority_User5, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User6_Set_PIN,   {UID_Authority_Admins,UID_Authority_User6, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User7_Set_PIN,   {UID_Authority_Admins,UID_Authority_User7, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User8_Set_PIN,   {UID_Authority_Admins,UID_Authority_User8, },  {1,3,}},   //Boolean>1
        {UID_ACE_C_PIN_User9_Set_PIN,   {UID_Authority_Admins,UID_Authority_User9, },  {1,3,}},   //Boolean>1

        {UID_ACE_K_AES_256_GlobalRange_GenKey, {UID_Authority_Admins, }, {0,}},
        {UID_ACE_K_AES_256_Range1_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range2_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range3_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range4_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range5_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range6_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range7_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_256_Range8_GenKey, {UID_Authority_Admins, },{0,}},
        {UID_ACE_K_AES_Mode,              {UID_Authority_Anybody, },{1,4,}},
    #endif

    #if _TCG_ != TCG_PYRITE
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range1_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range2_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range3_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range4_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range5_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range6_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range7_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_Range8_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,}},
        {UID_ACE_Locking_GRange_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range1_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range2_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range3_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range4_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range5_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range6_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range7_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_Range8_Set_RdLocked, {UID_Authority_Admins, },    {1,7,}},
        {UID_ACE_Locking_GRange_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range1_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range2_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range3_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range4_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range5_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range6_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range7_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_Range8_Set_WrLocked, {UID_Authority_Admins, },    {1,8,}},
        {UID_ACE_Locking_GlblRng_Admins_Set,        {UID_Authority_Admins, }, {5,5,6,7,8,9,}},
        {UID_ACE_Locking_Admins_RangeStartToLocked, {UID_Authority_Admins, },{7,3,4,5,6,7,8,9,}},
      #if TCG_FS_CONFIG_NS
        {UID_ACE_Locking_Namespace_IdtoGlbRng, {UID_Authority_Admins, },    {2, 0x14, 0x15,}},
      #endif
    #else
        {UID_ACE_Locking_GRange_Get_RangeStartToActiveKey, {UID_Authority_Admins, }, {8,3,4,5,6,7,8,9,10,0,0,0}},
        {UID_ACE_Locking_GRange_Set_RdLocked,       {UID_Authority_Admins, }, {1,7,}},
        {UID_ACE_Locking_GRange_Set_WrLocked,       {UID_Authority_Admins, },{1,8,}},
        {UID_ACE_Locking_GlblRng_Admins_Set,        {UID_Authority_Admins, }, {5,5,6,7,8,9,}},
    #endif

        {UID_ACE_MBRControl_Admins_Set,             {UID_Authority_Admins, },{3,1,2,3,}},
        {UID_ACE_MBRControl_Set_Done,               {UID_Authority_Admins, },{2,2,3,}},
    #if TCG_FS_CONFIG_NS
        {UID_ACE_Assign,                            {UID_Authority_Admins, }, {0,}},
        {UID_ACE_Deassign,                          {UID_Authority_Admins, }, {0,}},
    #endif
        {UID_ACE_DataStore_Get_All,                 {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore_Set_All,                 {UID_Authority_Admins, },{0,}},
    #if _TCG_ != TCG_PYRITE
        {UID_ACE_DataStore2_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore2_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore3_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore3_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore4_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore4_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore5_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore5_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore6_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore6_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore7_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore7_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore8_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore8_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore9_Get_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_DataStore9_Set_All|UID_FF,        {UID_Authority_Admins, },{0,}},
        {UID_ACE_Locking_GRange_Set_ReadToLOR|UID_FF,{UID_Authority_User1, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range1_Set_ReadToLOR|UID_FF,{UID_Authority_User2, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range2_Set_ReadToLOR|UID_FF,{UID_Authority_User3, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range3_Set_ReadToLOR|UID_FF,{UID_Authority_User4, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range4_Set_ReadToLOR|UID_FF,{UID_Authority_User5, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range5_Set_ReadToLOR|UID_FF,{UID_Authority_User6, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range6_Set_ReadToLOR|UID_FF,{UID_Authority_User7, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range7_Set_ReadToLOR|UID_FF,{UID_Authority_User8, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range8_Set_ReadToLOR|UID_FF,{UID_Authority_User9, },{5,5,6,7,8,9,}},
        {UID_ACE_Locking_Range1_Set_Range|UID_FF,    {UID_Authority_User2, },{2,3,4,}},
        {UID_ACE_Locking_Range2_Set_Range|UID_FF,    {UID_Authority_User3, },{2,3,4,}},
        {UID_ACE_Locking_Range3_Set_Range|UID_FF,    {UID_Authority_User4, },{2,3,4,}},
        {UID_ACE_Locking_Range4_Set_Range|UID_FF,    {UID_Authority_User5, },{2,3,4,}},
        {UID_ACE_Locking_Range5_Set_Range|UID_FF,    {UID_Authority_User6, },{2,3,4,}},
        {UID_ACE_Locking_Range6_Set_Range|UID_FF,    {UID_Authority_User7, },{2,3,4,}},
        {UID_ACE_Locking_Range7_Set_Range|UID_FF,    {UID_Authority_User8, },{2,3,4,}},
        {UID_ACE_Locking_Range8_Set_Range|UID_FF,    {UID_Authority_User9, },{2,3,4,}},

        { UID_ACE_CPIN_Anybody_Get_NoPIN|UID_FF,     { UID_Authority_Anybody, },{5,0,4,5,6,7,} },
        { UID_ACE_SP_Reactivate_Admin,               { UID_Authority_Admins, },{0} },

        { UID_ACE_Locking_GRange_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User1},{0} },
        { UID_ACE_Locking_Range1_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User2},{0} },
        { UID_ACE_Locking_Range2_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User3},{0} },
        { UID_ACE_Locking_Range3_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User4},{0} },
        { UID_ACE_Locking_Range4_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User5},{0} },
        { UID_ACE_Locking_Range5_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User6},{0} },
        { UID_ACE_Locking_Range6_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User7},{0} },
        { UID_ACE_Locking_Range7_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User8},{0} },
        { UID_ACE_Locking_Range8_Erase|UID_FF,       { UID_Authority_Admins, UID_Authority_User9},{0} },

        { UID_ACE_User1_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User2_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User3_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User4_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User5_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User6_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User7_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User8_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
        { UID_ACE_User9_Set_CommonName,{ UID_Authority_Admins },{ 1,2 } }, //CommonName
    #endif
    }
},

//__align(4) const sLckAuthority_Tbl cLckAuthority_Tbl =
{
    { // hdr
        sizeof(sLckAuthority_Tbl),
        sizeof(G3.b.mLckAuthority_Tbl.pty) / sizeof(sColPrty),              // colCnt
        0x12,                                                               // maxCol
        sizeof(G3.b.mLckAuthority_Tbl.val) / sizeof(sAuthority_TblObj),     // RowCnt
        sizeof(sAuthority_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                          // UID
        { 0x01,sizeof(G3.b.mLckAuthority_Tbl.val[0].name),STRING_TYPE,{0} },
        { 0x02,sizeof(G3.b.mLckAuthority_Tbl.val[0].commonName),STRING_TYPE,{0} },
        { 0x03,sizeof(G3.b.mLckAuthority_Tbl.val[0].isClass),VALUE_TYPE,{0} },            // IsClass (bool)
        { 0x04,8,UID_TYPE,{0} },                                                          // Class
        { 0x05,sizeof(G3.b.mLckAuthority_Tbl.val[0].enabled),VALUE_TYPE,{0} },            // Enabled (bool)
        { 0x06,sizeof(secure_message),VALUE_TYPE,{0} },                                   // Secure
        { 0x07,sizeof(hash_protocol),VALUE_TYPE,{0} },                                    // HashAndSign
        { 0x08,sizeof(G3.b.mLckAuthority_Tbl.val[0].presentCertificate),VALUE_TYPE,{0} }, // PresentCertificate (bool)
        { 0x09,sizeof(auth_method),VALUE_TYPE,{0} },                                      // Operation
        { 0x0A,8,UID_TYPE,{0} },      //Credential (UID)
        { 0x0B,8,UID_TYPE,{0} },      //ResponseSign
        { 0x0C,8,UID_TYPE,{0} }       //ResponseExch
    },
    {
        {UID_Authority_Anybody,"Anybody",   "", false,UID_Null,            true, SECURE_None, HASH_None, false, AUTH_None,     UID_Null,        UID_Null, UID_Null},
        {UID_Authority_Admins, "Admins",    "", true, UID_Null,            true, SECURE_None, HASH_None, false, AUTH_None,     UID_Null,        UID_Null, UID_Null},
        {UID_Authority_Admin1, "Admin1",    "", false,UID_Authority_Admins,true, SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin1, UID_Null, UID_Null},
        {UID_Authority_Admin2, "Admin2",    "", false,UID_Authority_Admins,false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin2, UID_Null, UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_Admin3, "Admin3",    "", false,UID_Authority_Admins,false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin3, UID_Null, UID_Null},
        {UID_Authority_Admin4, "Admin4",    "", false,UID_Authority_Admins,false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_Admin4, UID_Null, UID_Null},
    #endif
        {UID_Authority_Users,  "Users ",    "", true, UID_Null,            true, SECURE_None, HASH_None, false, AUTH_None,     UID_Null,        UID_Null, UID_Null},
        {UID_Authority_User1,  "User1",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User1,  UID_Null, UID_Null},
        {UID_Authority_User2,  "User2",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User2,  UID_Null, UID_Null},
    #if _TCG_ != TCG_PYRITE
        {UID_Authority_User3,  "User3",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User3,  UID_Null, UID_Null},
        {UID_Authority_User4,  "User4",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User4,  UID_Null, UID_Null},
        {UID_Authority_User5,  "User5",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User5,  UID_Null, UID_Null},
        {UID_Authority_User6,  "User6",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User6,  UID_Null, UID_Null},
        {UID_Authority_User7,  "User7",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User7,  UID_Null, UID_Null},
        {UID_Authority_User8,  "User8",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User8,  UID_Null, UID_Null},
        {UID_Authority_User9,  "User9",     "", false,UID_Authority_Users, false,SECURE_None, HASH_None, false, AUTH_Password, UID_CPIN_User9,  UID_Null, UID_Null}
    #endif
    }
},

//__align(4) const sLckCPin_Tbl cLckCPin_Tbl =
{
    { // hdr
        sizeof(sLckAuthority_Tbl),
        sizeof(G3.b.mLckCPin_Tbl.pty) / sizeof(sColPrty),       // colCnt
        0x07,                                                   // maxCol
        sizeof(G3.b.mLckCPin_Tbl.val) / sizeof(sCPin_TblObj),   // rowCnt
        sizeof(sCPin_TblObj),
    },
    { // ColPty
        { 0x00,sizeof(G3.b.mLckCPin_Tbl.val[0].uid),UID_TYPE,{0} },           // UID
        { 0x01,sizeof(G3.b.mLckCPin_Tbl.val[0].name),STRING_TYPE,{0} },
        //{ 0x03,sizeof(U8*),VALUE_TYPE,{0} },                                 // PIN, check later ...
        { 0x03,sizeof(G3.b.mLckCPin_Tbl.val[0].cPin),VBYTE_TYPE,{0} },         // PIN
        { 0x04,sizeof(G3.b.mLckCPin_Tbl.val[0].charSet),UID_TYPE,{0} },       // CharSet
        { 0x05,sizeof(G3.b.mLckCPin_Tbl.val[0].tryLimit),VALUE_TYPE,{0} },    // TryLimit
        { 0x06,sizeof(G3.b.mLckCPin_Tbl.val[0].tries),VALUE_TYPE,{0} },       // Tries
        { 0x07,sizeof(G3.b.mLckCPin_Tbl.val[0].persistence),VALUE_TYPE,{0} }  // Persistence (bool)
    },
    {
        { UID_CPIN_Admin1, "C_PIN_Admin1", { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_Admin2, "C_PIN_Admin2", { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
    #if _TCG_ != TCG_PYRITE
        { UID_CPIN_Admin3, "C_PIN_Admin3", { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_Admin4, "C_PIN_Admin4", { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
    #endif
        { UID_CPIN_User1, "C_PIN_User1",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User2, "C_PIN_User2",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
    #if _TCG_ != TCG_PYRITE
        { UID_CPIN_User3, "C_PIN_User3",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User4, "C_PIN_User4",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User5, "C_PIN_User5",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User6, "C_PIN_User6",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User7, "C_PIN_User7",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User8, "C_PIN_User8",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
        { UID_CPIN_User9, "C_PIN_User9",   { 0, { 0 }, { 0 } }, UID_Null, 5, 0, false },
    #endif
    }
},

//__align(4) const sLocking_Tbl cLckLocking_Tbl =
{
    { // hdr
        sizeof(sLocking_Tbl),
        sizeof(G3.b.mLckLocking_Tbl.pty) / sizeof(sColPrty),          // colCnt
        0x13,                                                         // maxCol
        sizeof(G3.b.mLckLocking_Tbl.val) / sizeof(sLocking_TblObj),   // rowCnt
         sizeof(sLocking_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x01,sizeof(G3.b.mLckLocking_Tbl.val[0].name),STRING_TYPE,{0} },        // name
        { 0x02,sizeof(G3.b.mLckLocking_Tbl.val[0].commonName),STRING_TYPE,{0} },
        { 0x03,sizeof(G3.b.mLckLocking_Tbl.val[0].rangeStart),VALUE_TYPE,{0} },   // rangeStart
        { 0x04,sizeof(G3.b.mLckLocking_Tbl.val[0].rangeLength),VALUE_TYPE,{0} },
        { 0x05,1,VALUE_TYPE,{0} },                                                // readLockEnabled
        { 0x06,1,VALUE_TYPE,{0} },                                                // writeLockEnabled
        { 0x07,1,VALUE_TYPE,{0} },                                                // readLocked
        { 0x08,1,VALUE_TYPE,{0} },                                                // writeLocked
        { 0x09,sizeof(G3.b.mLckLocking_Tbl.val[0].lockOnReset),LIST_TYPE,{0} },   // LockOnReset
        { 0x0A,8,UID_TYPE,{0} },                                                  // ActiveKey
    #if TCG_FS_CONFIG_NS
        { 0x14,sizeof(G3.b.mLckLocking_Tbl.val[0].namespaceId),VALUE_TYPE,{0} },  // NamespaceID
        { 0x15,sizeof(G3.b.mLckLocking_Tbl.val[0].namespaceGRange),VALUE_TYPE,{0} },  // NamespaceGlobalRange
    #endif
    },
    {
    #if _TCG_ != TCG_PYRITE
      #if TCG_FS_CONFIG_NS
        {UID_Locking_GRange, "Locking_GlobalRange", "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_GRange_Key, 0, 1},
        {UID_Locking_Range1, "Locking_Range1",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range1_Key, 0, 0},
        {UID_Locking_Range2, "Locking_Range2",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range2_Key, 0, 0},
        {UID_Locking_Range3, "Locking_Range3",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range3_Key, 0, 0},
        {UID_Locking_Range4, "Locking_Range4",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range4_Key, 0, 0},
        {UID_Locking_Range5, "Locking_Range5",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range5_Key, 0, 0},
        {UID_Locking_Range6, "Locking_Range6",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range6_Key, 0, 0},
        {UID_Locking_Range7, "Locking_Range7",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range7_Key, 0, 0},
        {UID_Locking_Range8, "Locking_Range8",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range8_Key, 0, 0}
      #else
        {UID_Locking_GRange, "Locking_GlobalRange", "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_GRange_Key},
        {UID_Locking_Range1, "Locking_Range1",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range1_Key},
        {UID_Locking_Range2, "Locking_Range2",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range2_Key},
        {UID_Locking_Range3, "Locking_Range3",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range3_Key},
        {UID_Locking_Range4, "Locking_Range4",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range4_Key},
        {UID_Locking_Range5, "Locking_Range5",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range5_Key},
        {UID_Locking_Range6, "Locking_Range6",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range6_Key},
        {UID_Locking_Range7, "Locking_Range7",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range7_Key},
        {UID_Locking_Range8, "Locking_Range8",      "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0}, UID_K_AES_256_Range8_Key}
      #endif
    #else
        {UID_Locking_GRange,      "Locking_GlobalRange", "", 0x00,0x00, 0,0,0,0, {1,PowerCycle,0,0},UID_Null},
    #endif
    }
},


//__align(4) const sMbrCtrl_Tbl cLckMbrCtrl_Tbl=
{
    { // hdr
        sizeof(sMbrCtrl_Tbl),
        sizeof(G3.b.mLckMbrCtrl_Tbl.pty) / sizeof(sColPrty),                  // colCnt
        0x03,                                                                 // maxCol
        sizeof(G3.b.mLckMbrCtrl_Tbl.val) / sizeof(sMbrCtrl_TblObj),           // rowCnt
        sizeof(sMbrCtrl_TblObj),
    },
    { // pty
        { 0x00,8,UID_TYPE,{0} },                                                  // UID
        { 0x01,sizeof(G3.b.mLckMbrCtrl_Tbl.val[0].enable),VALUE_TYPE,{0} },       // Enable
        { 0x02,sizeof(G3.b.mLckMbrCtrl_Tbl.val[0].done),VALUE_TYPE,{0} },         // Done
        { 0x03,sizeof(G3.b.mLckMbrCtrl_Tbl.val[0].doneOnReset),LIST_TYPE,{0} }    // DoneOnReset
    },
    {
        {UID_MBRControl, 0, 0, {1,PowerCycle,0,0}}
    }

},

#if _TCG_ != TCG_PYRITE
//__align(4) const sKAES_Tbl cLckKAES_256_Tbl =
{
    { // hdr
        sizeof(sKAES_Tbl),
        sizeof(G3.b.mLckKAES_256_Tbl.pty) / sizeof(sColPrty),                 // colCnt
        0x04,                                                                 // maxCol
        sizeof(G3.b.mLckKAES_256_Tbl.val) / sizeof(sKAES_TblObj),             // rowCnt
        sizeof(sKAES_TblObj),
    },
    { // ColPty
        { 0x00,8,UID_TYPE,{0} },                                              // UID
        //{ 0x03,sizeof(G3.b.mLckKAES_256_Tbl.val[0].key),FBYTE_TYPE,{0} },
        { 0x04,sizeof(G3.b.mLckKAES_256_Tbl.val[0].mode),VALUE_TYPE,{0} }     // mode
    },
    {
        {UID_K_AES_256_GRange_Key,  AES_XTS},
        {UID_K_AES_256_Range1_Key,  AES_XTS},
        {UID_K_AES_256_Range2_Key,  AES_XTS},
        {UID_K_AES_256_Range3_Key,  AES_XTS},
        {UID_K_AES_256_Range4_Key,  AES_XTS},
        {UID_K_AES_256_Range5_Key,  AES_XTS},
        {UID_K_AES_256_Range6_Key,  AES_XTS},
        {UID_K_AES_256_Range7_Key,  AES_XTS},
        {UID_K_AES_256_Range8_Key,  AES_XTS}
    }
},

//__align(4) const sWrappedKey mWrappedKey[LOCKING_RANGE_CNT+1]=
{
    //   (U32)idx, (U32)state, (U32)icv[2], (U32)sDEK[LOCKING_RANGE_CNT+1]
    // modified for CNL: (U16)nsid, (U16)range, (U32)state...  // (U64)nsze,
    { 0x01, 0x00, 0x0000, {
                            { 0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888 },   // (U32)AES_XTS[8]
                            { 0 },                                                                                                // (U32)icv1[2]
                            { 0x12345678, 0x23456781, 0x34567812, 0x45678123, 0x56781234, 0x67812345, 0x78123456, 0x81234567 },   // (U32)XTS_Key[8]
                            { 0 }                                                                                                 // (U32)icv2[2]
                          },
                          { 0 } // (U32)Salt[8]
    },

    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
#if TCG_FS_CONFIG_NS
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
    { 0x0000, 0x0000, 0x0000,       { { 0 }, { 0 }, { 0 }, { 0 } },        { 0 } },
#endif
},

//__align(4) const sWrappedKey mOpalWrapKEK[TCG_AdminCnt + TCG_UserCnt + 1]=
{
    //                  (U32)idx, (S32)state, (U32)OpalKEK[8], (U32)icv[2], (U32)Salt[8]
    { (U32)UID_Authority_Anybody, 0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_Admin1,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_Admin2,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_Admin3,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_Admin4,  0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User1,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User2,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User3,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User4,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User5,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User6,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User7,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User8,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_User9,   0x0000,  { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_AtaMst, 0x0000,   { 0 },           { 0 },           { 0 } },
    { (U32)UID_Authority_AtaUsr, 0x0000,   { 0 },           { 0 },           { 0 } },
},
#elif (_TCG_ == TCG_PYRITE) && CO_SUPPORT_AES
//__align(4) const sWrappedKey mWrappedKey[LOCKING_RANGE_CNT+1]=
{
    //                     (U32)idx, (S32)state, (U32)icv[4], (U32)sDEK[LOCKING_RANGE_CNT+1]
    // modified for CNL: (U16)nsid, (U16)range,  (U32)state...  //
    { 0x01, 0x00, 0x0000, {
                            { 0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888 },   // (U32)AES_XTS[8]
                            { 0 },                                                                                                // (U32)icv1[2]
                            { 0x12345678, 0x23456781, 0x34567812, 0x45678123, 0x56781234, 0x67812345, 0x78123456, 0x81234567 },   // (U32)XTS_Key[8]
                            { 0 }                                                                                                 // (U32)icv2[2]
                          },
                          { 0 } // (U32)Salt[8]
    },
},

//__align(4) const sWrappedKey mOpalWrapKEK[TCG_AdminCnt + TCG_UserCnt + 1]=
{
    //                  (U32)idx, (S32)state, (U32)OpalKEK[8] (U32)icv[2]; (U32)Salt[8]
    { (U32)UID_Authority_Anybody, 0x0000,  { 0 },          { 0 },       { 0 } },
    { (U32)UID_Authority_Admin1,  0x0000,  { 0 },          { 0 },       { 0 } },
    { (U32)UID_Authority_User1,   0x0000,  { 0 },          { 0 },       { 0 } },
    { (U32)UID_Authority_User2,   0x0000,  { 0 },          { 0 },       { 0 } },
    { (U32)UID_Authority_AtaMst,  0x0000,  { 0 },          { 0 },       { 0 } },
    { (U32)UID_Authority_AtaUsr,  0x0000,  { 0 },          { 0 },       { 0 } },
},
#endif

    TCG_END_TAG
} };    // G3 init end
#endif
/* End of File */
