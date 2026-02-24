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

//-----------------------------------------------------------------------------
//  Macro & Constants definitions:
//-----------------------------------------------------------------------------

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

//-----------------------------------------------------------------------------
//  Imported data prototype without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
typedef enum
{
    c_SECURITY_INFO     = 0x00,  ///< Security protocol information
    c_TCG_PID_01        = 0x01,
    c_TCG_PID_02        = 0x02,

    c_NVME_PID_EA       = 0xEA,

    c_IEEE1667_PID      = 0xEE,
    c_ATA_SECURITY      = 0xEF,  ///< ATA Device Server Password Security

} SecurityProtocolID_t;

typedef enum
{
    c_SATA_CMD_SecurityInfo    = 0x0000,
    c_SATA_CMD_SetPassword     = 0x0001,
    c_SATA_CMD_Unlock          = 0x0002,
    c_SATA_CMD_ErasePrepare    = 0x0003,
    c_SATA_CMD_EraseUnit       = 0x0004,
    c_SATA_CMD_FreezeLock      = 0x0005,
    c_SATA_CMD_DisablePassword = 0x0006
} SecurityCmdSPSP_t;

typedef enum
{
   IN_NO_ERROR                 = 0x0,
   IN_ERROR                    = 0x01,
   IN_SECURITY_LOCK            = 0x02,
   IN_SECURITY_INVALID_COMMAND = 0x03,
   IN_SECURITY_FROZEN          = 0x04
} Inerror_state; //security internel error check

//-----------------------------------------------------------------------------
typedef  struct
{
    u8  reserve;
    u8  Length;
    u8 EraseTimeRequireMSB;
    u8 EraseTimeRequireLSB;
    u8 EnhEraseTimeRequireMSB;
    u8 EnhEraseTimeRequireLSB;
    u8 MasterPwdIdentifierMSB;
    u8 MasterPwdIdentifierLSB;
    u8  Maxset:1;
    u8  reserve8:7;

    union
    {
        u8 all;                                ///<  128
        struct
        {
            u8 securitySupported:1;                ///<  BIT0
            u8 securityEnabled:1;                  ///<  BIT1
            u8 securityLocked:1;                   ///<  BIT2
            u8 securityFrozen:1;                   ///<  BIT3
            u8 securityCountExpired:1;             ///<  BIT4
            u8 enhancedSecurityEraseSupported:1;   ///<  BIT5
            u8 reserved0:2;                        ///<  BIT[7:6]
        } b;
    } securityStatus;       ///< 128

    u8 reserve16[2];
} ATA_SecurityReceive_t;

typedef union
{
   u16 all;
    struct
    {
        u8 MSBID;                      ///MSBID
        u8 LSBID;                      ///LSBID
    } b;
} MPID;
typedef struct
{
    u8  SecLevel:1;                 // 0 = High, 1 = Maximum
    u8  reserved0:7;
    u8  Identify:1;                 // 0 = User PW, 1 = Master PW
    u8  reserved1:7;
    u16 PW[SEC_PW_SIZE];            // Byte 2~33
    MPID RevCode;                      //BYTE 34 ~ 35MASTER PASSWORD IDENTIFIER
    u8 reserve16[2];
} tSATA_PW_DATA;

typedef struct
{
    bool updateEE;
    bool updateTcgG3;
} ataSetPw_varsMgm_t;

typedef struct
{
    bool updateEE;
    bool updateTcgG3;
} ataDisPw_varsMgm_t;


//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
extern enum cmd_rslt_t  ATA_SecuritySend(tSATA_PW_DATA *pBuff, req_t *req);
extern void SATA_Security_Init(bool warm_reset);

//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Imported function proto-type without header include
//-----------------------------------------------------------------------------
