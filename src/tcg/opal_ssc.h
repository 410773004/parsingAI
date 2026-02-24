//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#pragma once

//#include "security_cmd.h"

#ifndef _WITH_SED_UTILS_
#include "types.h"
#endif

/*!
 * @brief opal communication packet
 * core: Storage_Architecture_Core_Spec_v2-00_r2-00-Final-2-1.pdf
 * ssc : Opal_SSC_1.00_rev3.00-Final.pdf
 */
typedef struct opal_com_packet {
    /* core 3.2.3.2 */
    /* 0,3 */U8 rsvd[4];
    /* 4,5 */U8 comid[2];
    /* 6,7 */U8 comid_extension[2];
    /* 9,11*/U8 outstanding_data[4];
    /*12,15*/U8 min_transfer[4];
    /*16,19*/U8 length[4];
} PACKED opal_com_packet_t;

/*!
 * @brief opal packet
 * core: Storage_Architecture_Core_Spec_v2-00_r2-00-Final-2-1.pdf
 * ssc : Opal_SSC_1.00_rev3.00-Final.pdf
 */
typedef struct {
    /* core 3.2.3.3 */
    /* 0,7 */U8 session[8];
    /* 8,11*/U8 seq_number[4];
    /*12,13*/U8 rsvd[2];
    /*14,15*/U8 ack_type[2];
    /*16,19*/U8 acknowlegement[4];
    /*20,23*/U8 length[4];
} PACKED opal_packet_t;

/*!
 * @brief opal data sub packet
 * core: Storage_Architecture_Core_Spec_v2-00_r2-00-Final-2-1.pdf
 * ssc : Opal_SSC_1.00_rev3.00-Final.pdf
 */
typedef struct {
    /* core 3.2.3.4.1 */
    /* 0,5 */U8 rsvd[6];
    /* 6,7 */U8 kind[2];
    /* 8,11*/U8 length[4];
} PACKED opal_data_subpacket_t;

/*!
 * @brief opal header
 * core: Storage_Architecture_Core_Spec_v2-00_r2-00-Final-2-1.pdf
 * ssc : Opal_SSC_1.00_rev3.00-Final.pdf
 */
typedef struct {
    opal_com_packet_t com;
    opal_packet_t pkt;
    opal_data_subpacket_t datasub;
} PACKED opal_header_t;

/*!
 * @brief opal property entry
 * 4.1.1.1 Properties(M) - Table 12. Opal SSC Spec v2.01
 */
typedef struct property_entry {
    char name[32];
    U32 value;
}property_entry_t;

/*!
 * @brief Opal ACE Table
 * Admin SP:
 * Locking SP:
 * Core Spec v2.01, Table 177. ACE Table Description.
 * Opal SSC Spec v2.01, Table 18 Admin SP - ACE Table.
 * Opal SSC Spec v2.01, Table 30 Locking SP - ACE Table.
 */
typedef struct ace_table {
    U8        uid[8];
    const char    *name;
    char        *common_name;
    U32    boolean_expr; /* implementation specific */
    U32    columns; /* all or one or more columns */
} ace_table_t;

/*!
 * @brief opal SP Authority Table
 * Table 19. Opal SSC Spec v2.01, Admin SP - Authority Table.
 * Table 31. Opal SSC Spec v2.01, Locking SP - Authority Table.
 */
typedef struct sp_auth_table {
    U8        uid[8];
    const char    *name;
    char        *common_name;
    U8        is_class;
    const char    *class;
    U8        enabled;
    const char    *secure;
    const char    *hash_and_sign;
    U8        present_certificate;
    const char    *operation;
    const char    *credential;
    const char    *response_sign;
    const char    *response_exch;
} sp_auth_table_t;

/*!
 * @brief opal Admin SP Authority Table Index
 */
typedef enum {
    ASAT_SID_IDX,
    ASAT_ADMIN1_IDX,
    OPAL_ADMIN_SP_AUTH_UNKNOWN,
} asat_indx_t;

/*!
 * @brief opal Locking SP Authority Table Index
 */
typedef enum {
    LSAT_ADMIN1_IDX,
    LSAT_USER1_IDX,
    LSAT_USER2_IDX,
    LSAT_USER3_IDX,
    LSAT_USER4_IDX,
    LSAT_USER5_IDX,
    LSAT_USER6_IDX,
    LSAT_USER7_IDX,
    LSAT_USER8_IDX,
    OPAL_LOCKING_SP_AUTH_UNKNOWN,
} lsat_indx_t;

/*!
 * @brief opal encryption support
 * Core spec. v2.01, Table 82 enc_supported values
 */
typedef enum {
    ENC_SUP_NONE = 0,
    ENC_SUP_MED_ENCRYPT,
    ENC_SUP_RSVD,
} enc_support_t;

/*!
 * @brief opal key available conditions
 * Core spec. v2.01, Table 98 keys_avail_conds values
 */
typedef enum {
    KAC_NONE = 0,
    KAC_AUTHENTICATION,
    KAC_RSVD,
} keys_avail_conds_t;

struct date {
    U16 year;
    U8  month;
    U8  day;
};

/*!
 * @brief life_cycle_state Enumeration values
 * Core spec. v2.01, Table 103 &
 * Opal SSC v2.01, Table 40 LifeCycle Type Table Modification.
 */
typedef enum {
    LCS_ISSUED = 0,
    LCS_ISSUED_DISABLED,
    LCS_ISSUED_FROZEN,
    LCS_ISSUED_DISABLED_FROZEN,
    LCS_ISSUED_FAILED,
    LCS_MANUFACTURED_INACTIVE = 8,    /* Opal SSC */
    LCS_MANUFACTURED = 9,        /* Opal SSC */
} life_cycle_state_t;

/*!
 * @brief Admin SP - SP Table
 * Opal v2.01 Table 24 Admin SP - SP Table.
 * Core Spec v2.01, Table 215 SP Table Description.
 */
typedef struct admin_sp_table {
    U8        uid[8];
    const char    *name;
    U32    org;
    U8        effective_auth[32];
    struct date    date_of_issue;
    U8        bytes;
    life_cycle_state_t    life_cycle;
    U8        frozen;
} admin_sp_table_t;

/*!
 * @brief opal Locking Info Table
 * Locking SP - LockingInfo Object Table. References:
 * Core Spec v2.01, Table 225 LockingInfo Table Description.
 * Opal v2.01 Table 35 Locking SP - LockingInfo Table.
 */
typedef struct lock_info_table {
    U8        uid[8];            /* col 0 */
    const char    *name;            /* col 1 */
    U32    version;        /* col 2 */
    enc_support_t    encrypt_support;    /* col 3 */
    U32    max_ranges;        /* col 4 */
    U32    max_re_encryptions;    /* col 5 */
    keys_avail_conds_t keys_available_cfg;    /* col 6 */
    U8        alignment_required;    /* col 7 */
    U32    logical_block_size;    /* col 8 */
    U64    alignment_granularity;    /* col 9 */
    U64    lowest_aligned_lba;    /* col 10 */
} lock_info_table_t;

/*!
 * @brief opal Locking Object Table
 * Locking (Object Table). References:
 * Core Spec v2.01, section 5.7.2.2 (Table 226 Locking Table Description ) and
 * Opal v2.01 Table 36 Locking SP - Locking Table.
 */
typedef struct lock_table {
    U8        uid[8];            /* col 0 */
    const char    *name;            /* col 1 */
    char        *common_name;        /* col 2 */
    U64    range_start;        /* col 3 */
    U64    range_length;        /* col 4 */
    U8        read_lock_enabled;    /* col 5 */
    U8        write_lock_enabled;    /* col 6 */
    U8        read_locked;        /* col 7 */
    U8        write_locked;        /* col 8 */
    U32    lock_on_reset;        /* col 9 */
    U8        active_key[8];        /* col 10 */
    U8        next_key[8];        /* col 11 */
    U32    re_encrypt_state;    /* col 12 */
    U32    re_encrypt_request;    /* col 13 */
    U32    adv_key_mode;        /* col 14 */
    U32    verify_mode;        /* col 15 */
    U32    cont_on_reset;        /* col 16 */
    U8        last_re_encrypt_lba;    /* col 17 */
    U32    last_re_enc_stat;    /* col 18 */
    U32    general_status;        /* col 19 */
} lock_table_t;

/*!
 * @brief opal Locking Table Index
 * Index to locking_table (lt_idx)
 */
typedef enum {
    LT_IDX_GR = 0x00,
    LT_IDX_LR1,
    LT_IDX_LR2,
    LT_IDX_LR3,
    LT_IDX_LR4,
    LT_IDX_LR5,
    LT_IDX_LR6,
    LT_IDX_LR7,
    LT_IDX_LR8,
    LT_IDX_MAX,
} lt_indx_t;

/*!
 * @brief opal Locking Table General Status
 * Core spec ve2.01 Table 87 gen_status
 */
typedef enum {
    LTGS_NONE =            0,
    LTGS_PENDING_TPER_ERROR =    1,
    LTGS_ACTIVE_TPER_ERROR =    2,
    LTGS_ACTIVE_PAUSE_REQUESTED =    3,
    LTGS_PEND_PAUSE_REQUESTED =    4,
    LTGS_PEND_RESET_STOP_DETECT =    5,
    LTGS_KEY_ERROR =        6,
    LTGS_WAIT_AVAILABLE_KEYS =    32,
    LTGS_WAIT_FOR_TPER_RESOURCES =    33,
    LTGS_ACTIVE_RESET_STOP_DETECT = 34,
    LTGS_CONFIGURED =        35, /* use reserved for FW use */
} lt_gen_status_t;

/*!
 * @brief opal AES Key
 * Opal SSC ver 2.01 Table 38/39 Locking SP - K_AES_128/256 Table
 */
typedef struct key_aes {
    U8 uid[8];
    const char *name;
    char *common_name;
    U8 key;
    U8 mode;
} key_aes_t;

/*!
 * @brief Values for reset types
 * Core Spec v2.01, Table 128 reset_types values
 */
typedef enum {
    RST_TYPE_PWR_CYCLE = 0x00,
    RST_TYPE_HARDWARE,
    RST_TYPE_HOTPLUG,
} reset_types_t;

/*!
 * @brief MBRControl Table
 * Locking SP - MBRControl Table. References:
 * Core Spec v2.01, Table 229 MBRControl Table Description.
 * Opal SSC v2.01 Table 37 Locking SP - MBRControl Table.
 */
typedef struct mbr_control_table {
    U8        uid[8];            /* col 0 */
    U8        enable;            /* col 1 */
    U8        done;            /* col 2 */
    reset_types_t    mbr_done_on_reset;    /* col 3 */
} mbr_control_table_t;

/*!
 * @brief opal status
 */
typedef enum {
    NOT_ACTIVE,
    ACTIVE,
    NOT_SET = 0x00,
    READ_ONLY = 0x01,
    READ_WRITE = 0x02,
} opal_status_t;

/*!
 * @brief opal response status
 */
typedef enum {
    NOT_PENDING,
    PENDING,
} opal_resp_t;

/*!
 * @brief opal status codes
 * Core Spec. v2.01. Table 166 Status Codes
 */
typedef enum {
    OPAL_SC_SUCCESS            = 0x00,
    OPAL_SC_NOT_AUTHORIZED        = 0x01,
    OPAL_SC_SP_BUSY            = 0x03,
    OPAL_SC_SP_FAILED        = 0x04,
    OPAL_SC_SP_DISABLED        = 0x05,
    OPAL_SC_SP_FROZEN        = 0x06,
    OPAL_SC_NO_SESSIONS_AVAILABLE    = 0x07,
    OPAL_SC_UNIQUENESS_CONFLICT    = 0x08,
    OPAL_SC_INSUFFICIENT_SPACE    = 0x09,
    OPAL_SC_INSUFFICIENT_ROWS    = 0x0a,
    OPAL_SC_INVALID_PARAMETER    = 0x0c,
    OPAL_SC_TPER_MALFUNCTION    = 0x0f,
    OPAL_SC_TRANSACTION_FAILURE    = 0x10,
    OPAL_SC_RESPONSE_OVERFLOW    = 0x11,
    OPAL_SC_AUTHORITY_LOCKED_OUT    = 0x12,
    OPAL_SC_FAIL            = 0x3f,
} opal_status_code_t;

/*!
 * @brief opal security send response state
 */
typedef struct secu_send_rsp_state {
    U32    hsn;
    U32    tsn;
    U8        spid[8];
    U8        c_pin_uid[8];
    U8        lr_uid[8];    /* Locking Range UID */
    U8        linfo_uid[8];
    U8        opal_method;    /* property, start_session etc. */
    U8        state;        /* active/not_active */
    U8        resp;        /* pending/not_pending */
    U8        start_column;
    U8        end_column;
    U8        admin_sp;
    U8        locking_sp;
    U8        access_mode;
    U8        status_code;
    U8        lr_indx;    /* Locking Range number */
    U8        get_indx;    /* 1:c_pin, 2:lr, 3:linfo UIDs */
    U8        lr_key_idx_set;    /* Locking Range key index */
    U8        linfo_uid_set;    /* Locking info UID */
    U8        lr_values_get;
    U8        take_ownership_done;
    U8        sm4_enable_done;
    U8        opal_activate_done;
    U8        num_of_lr_configured;
    U8        lr_configured_indx[9];
    U8        opal_init_done;
    U64    dev_capacity;
}secu_send_rsp_state_t;

U32 session_tsn = 0x1234;
secu_send_rsp_state_t sec_send_rsp_state;

/*!
 * @brief opal SP C_PIN
 * Opal SSC version 2.01. Table 20 Admin SP - C_PIN Table and
 * Opal SSC ver 2.01 Table 32 Locking SP - C_PIN Table
 */
typedef struct sp_c_pin {
    U8 uid[8];
    const char *name;
    const char *common_name;
    char pin[33];
    U8 char_set[2]; /* place holder now */
    U32 try_limit;
    U32 tries;
    U32 persistence;
} sp_c_pin_t;

/*!
 * @brief Admin SP C_PIN Table index.
 */
enum asp_c_pin_tbl_idx{
    ASP_CPIN_SID_IDX = 0,
    ASP_CPIN_MSID_IDX,
    ASP_CPIN_ADMIN1_IDX,
};

/*!
 * @brief Locking SP C_PIN Table index.
 */
enum lsp_c_pin_tbl_idx{
    LSP_CPIN_ADMIN1_IDX = 0,
    LSP_CPIN_USR1_IDX,
    LSP_CPIN_USR2_IDX,
    LSP_CPIN_USR3_IDX,
    LSP_CPIN_USR4_IDX,
    LSP_CPIN_USR5_IDX,
    LSP_CPIN_USR6_IDX,
    LSP_CPIN_USR7_IDX,
    LSP_CPIN_USR8_IDX,
    LSP_CPIN_USR_IDX_MAX,
};

/*!
 * @brief MEK entry index
 */
enum mek_idx{
    MEK_GR_IDX = 0,
    MEK_LR1_IDX,
    MEK_LR2_IDX,
    MEK_LR3_IDX,
    MEK_LR4_IDX,
    MEK_LR5_IDX,
    MEK_LR6_IDX,
    MEK_LR7_IDX,
    MEK_LR8_IDX,
};

/*!
 * @brief KEK entry index
 */
enum kek_idx{
    KEK_GR_IDX = 0,
    KEK_LR1_IDX,
    KEK_LR2_IDX,
    KEK_LR3_IDX,
    KEK_LR4_IDX,
    KEK_LR5_IDX,
    KEK_LR6_IDX,
    KEK_LR7_IDX,
    KEK_LR8_IDX,
};

/*!
 * @brief opal session state
 * core 3.3.3
 */
enum session_state {
    INACTIVE,
    ISSUED,
    ASSOCIATED,
};

/*!
 * @brief opal security session state
 * core 3.3.3
 */
struct security_session {
    enum session_state state;
};

/*!
 * @brief Supported Crypto engine algorithms.
 */
typedef enum {
    CEA_AES_XTS_128 = 1,
    CEA_AES_XTS_256,
    CEA_SM4_128,
    CEA_AES_ECB_128,
    CEA_AES_ECB_256,
} crypto_eng_algo_t;

/*!
 * @brief opal host commands
 */
enum opal_cmd {
    IF_SEND = 0x81,
    IF_RECV = 0x82,
};

/*!
 * @brief opal short atom
 */
enum opal_short_atom {
    UINT_3      = 0x83,
    BYTESTRING4 = 0x84,
    BYTESTRING8 = 0x88,
};

/*!
 * @brief opal call token
 * core 3.2.2.3
 */
enum opal_token {
    TOKEN_CALL = 0xF8,
};

/*!
 * @brief opal invoking UID index
 */
typedef enum {
    OPAL_SMUID,      /* Session Manager Layer */
    OPAL_THIS_SP,    /* This SP methods */
    OPAL_ADMIN_SP,   /* Admin SP */
    OPAL_LOCKING_SP, /* Locking SP */

    OPAL_AUTHORITY,

    OPAL_CPIN_MSID,
    OPAL_CPIN_SID,

    /* From here onwards, DO NOT change the sequence */
    OPAL_LOCKING_GLOBAL_RANGE_UID,
    OPAL_LOCKING_RANGE1_UID,
    OPAL_LOCKING_RANGE2_UID,
    OPAL_LOCKING_RANGE3_UID,
    OPAL_LOCKING_RANGE4_UID,
    OPAL_LOCKING_RANGE5_UID,
    OPAL_LOCKING_RANGE6_UID,
    OPAL_LOCKING_RANGE7_UID,
    OPAL_LOCKING_RANGE8_UID,

    OPAL_MBRCONTROL_UID, /* DO NOT CHANGE its order */

    OPAL_K_AES_256_RANGE1_KEY_UID,

    OPAL_LOCKING_INFO_UID,

    /* Locking SP C_PIN_User1 to _User8 */
    OPAL_LSP_CPIN_USR1_UID,
    OPAL_LSP_CPIN_USR2_UID,
    OPAL_LSP_CPIN_USR3_UID,
    OPAL_LSP_CPIN_USR4_UID,
    OPAL_LSP_CPIN_USR5_UID,
    OPAL_LSP_CPIN_USR6_UID,
    OPAL_LSP_CPIN_USR7_UID,
    OPAL_LSP_CPIN_USR8_UID,

    OPAL_UID_UNKNOWN,
} opal_uid_t;

/*!
 * @brief opal method UID index
 */
typedef enum {
    OPAL_PROPERTIES,
    OPAL_START_SESSION,
    OPAL_SYNC_SESSION,
    OPAL_START_TRUSTED_SESSION,
    OPAL_SYNC_TRUSTED_SESSION,
    OPAL_CLOSE_SESSION,

    OPAL_GET,
    OPAL_SET,

    OPAL_REVERT,
    OPAL_ACTIVATE,
    OPAL_GEN_KEY,
    OPAL_REVERT_SP,
    OPAL_AUTHENTICATE,

    /* No method ID function call available */
    OPAL_GET_LOCKING_SP,

    OPAL_METHOD_UNKNOWN,
} opal_method_t;

/*!
 * @brief opal Level 0 Discovery Header
 * Opal core spec v2.01. Table 39 Discovery Header Format
 */
typedef struct {
    U8 length[4];
    U8 major_version[2];
    U8 minor_version[2];
    U8 rsvd[8];
    U8 vendor[32];
} PACKED opal_l0_hdr_t;

/*!
 * @brief opal Feature Header
 * Opal core spec v2.01. Table 41 Feature Descriptor Template Format
 */
typedef struct {
    /* core 3.3.6.3 */
    U8 feature_code[2];
    U8 rsvd:4;
    U8 version:4;
    U8 length;
} PACKED opal_feature_hdr_t;

/*!
 * @brief opal TPer Features
 * core 3.3.6.4
 */
typedef struct {
    U8 Sync:1; /* bit0 */
    U8 Async:1;
    U8 ACK_NACK:1;
    U8 Buffer_Mgmt:1;
    U8 Streaming:1;
    U8 rsvd1:1;
    U8 ComID_Mgmt:1;
    U8 rsvd0:1;
    U8 pad[11];
} PACKED opal_TPer_t;

/*!
 * @brief opal Locking Features
 * core 3.3.6.5
 */
typedef struct {
    U8 Locking_Supported:1; /* bit0 */
    U8 Locking_en:1;
    U8 Locked:1;
    U8 Media_Encryption:1;
    U8 MBR_Enable:1;
    U8 MBR_Done:1;
    U8 rsvd0:2;
    U8 pad[11];
} PACKED opal_Locking_t;

/*!
 * @brief Opal Geometry Reporting Feature
 * Opal SSC v2.01 3.1.1.4
 */
typedef struct {
    U8 align:1; /* bit0 */
    U8 rsvd:7;
    U8 rsvd1[7];
    U8 logical_block_size[4];
    U64 alignment_granularity;
    U64 lowest_aligned_lba;
} PACKED opal_geometry_reporting_t;


/*!
 * @brief opal Security Subsystem Class Features
 * Opal SSC v2.01 3.1.1.5
 */
typedef struct {
    U8 BaseComID[2];
    U8 NumberOfComIDs[2];
    U8 range_crossing:1;
    U8 rsvd:7;
    U8 locking_sp_admins_supported[2];
    U8 locking_sp_users_supported[2];
    U8 initial_c_pin_sid_indicator;
    U8 c_pin_sid_upon_tper_revert;
    U8 pad[5];
} PACKED opal_SSC_t;

static enum cmd_rslt_t uid_smuid_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_thissp_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_adminsp_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_lockingsp_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_authority_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_cpin_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_cpin_sid_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_locking_range_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_mbr_control_func(req_t *req, token_t *tk);
static enum cmd_rslt_t uid_locking_range_key_func(req_t *req, token_t *tk);
