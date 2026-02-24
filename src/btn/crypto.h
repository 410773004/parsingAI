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

#include "btn_cmd_data_reg.h"

#if defined(USE_CRYPTO_HW)
enum {
	CRYPTO_BYPASS_MODE = 0,
	AES_ECB_256B_MODE  = 1,
	AES_XTS_128B_MODE  = 2,
	AES_XTS_256B_MODE  = 3,
	SM4_128B_MODE	   = 4,
	SM4_128B_MODE_01   = 1,
};

enum crypto_key_type {
//Andy_Crypto
	///Andy chage order
	#if 1
	Disable_crypto = 0,
	AES_ECB_256B_KEY = 1,
	AES_XTS_128B_KEY = 2,
	AES_XTS_256B_KEY = 3,
	SM4_128B_KEY	 = 4,
	AES_ECB_128B_KEY = 5,
	#else
	AES_XTS_256B_KEY = 0,
	AES_XTS_128B_KEY = 1,
	AES_ECB_256B_KEY = 2,
	AES_ECB_128B_KEY = 3,
	SM4_128B_KEY	 = 4,
	#endif
};

enum {
	CRYPTO_ALGO_AES = 0,
	CRYPTO_ALGO_SM4 = 1,
};

enum hash_algo {
	HASH_SHA3_256 = 0,
	HASH_SM3      = 1,
};

typedef enum {
	CRYPTO_LBA_MAP_ENTRY_00 = 0,
	CRYPTO_LBA_MAP_ENTRY_01,
	CRYPTO_LBA_MAP_ENTRY_02,
	CRYPTO_LBA_MAP_ENTRY_03,
	CRYPTO_LBA_MAP_ENTRY_04,
	CRYPTO_LBA_MAP_ENTRY_05,
	CRYPTO_LBA_MAP_ENTRY_06,
	CRYPTO_LBA_MAP_ENTRY_07,
	CRYPTO_LBA_MAP_ENTRY_08,
	CRYPTO_LBA_MAP_ENTRY_09,
	CRYPTO_LBA_MAP_ENTRY_10,
	CRYPTO_LBA_MAP_ENTRY_11,
	CRYPTO_LBA_MAP_ENTRY_12,
	CRYPTO_LBA_MAP_ENTRY_13,
	CRYPTO_LBA_MAP_ENTRY_14,
	CRYPTO_LBA_MAP_ENTRY_15,
	CRYPTO_LBA_MAP_ENTRY_16,
	CRYPTO_LBA_MAP_ENTRY_17,
	CRYPTO_LBA_MAP_ENTRY_18,
	CRYPTO_LBA_MAP_ENTRY_19,
	CRYPTO_LBA_MAP_ENTRY_20,
	CRYPTO_LBA_MAP_ENTRY_21,
	CRYPTO_LBA_MAP_ENTRY_22,
	CRYPTO_LBA_MAP_ENTRY_23,
	CRYPTO_LBA_MAP_ENTRY_24,
	CRYPTO_LBA_MAP_ENTRY_25,
	CRYPTO_LBA_MAP_ENTRY_26,
	CRYPTO_LBA_MAP_ENTRY_27,
	CRYPTO_LBA_MAP_ENTRY_28,
	CRYPTO_LBA_MAP_ENTRY_29,
	CRYPTO_LBA_MAP_ENTRY_30,
	CRYPTO_LBA_MAP_ENTRY_31,
	CRYPTO_LBA_MAP_ENTRY_32,
	CRYPTO_LBA_MAP_ENTRY_33,
	CRYPTO_LBA_MAP_ENTRY_34,
	CRYPTO_LBA_MAP_ENTRY_35,
	CRYPTO_LBA_MAP_ENTRY_36,
	CRYPTO_LBA_MAP_ENTRY_37,
	CRYPTO_LBA_MAP_ENTRY_38,
	CRYPTO_LBA_MAP_ENTRY_39,
	CRYPTO_LBA_MAP_ENTRY_40,
	CRYPTO_LBA_MAP_ENTRY_41,
	CRYPTO_LBA_MAP_ENTRY_42,
	CRYPTO_LBA_MAP_ENTRY_43,
	CRYPTO_LBA_MAP_ENTRY_44,
	CRYPTO_LBA_MAP_ENTRY_45,
	CRYPTO_LBA_MAP_ENTRY_46,
	CRYPTO_LBA_MAP_ENTRY_47,
	CRYPTO_LBA_MAP_ENTRY_48,
	CRYPTO_LBA_MAP_ENTRY_49,
	CRYPTO_LBA_MAP_ENTRY_50,
	CRYPTO_LBA_MAP_ENTRY_51,
	CRYPTO_LBA_MAP_ENTRY_52,
	CRYPTO_LBA_MAP_ENTRY_53,
	CRYPTO_LBA_MAP_ENTRY_54,
	CRYPTO_LBA_MAP_ENTRY_55,
	CRYPTO_LBA_MAP_ENTRY_56,
	CRYPTO_LBA_MAP_ENTRY_57,
	CRYPTO_LBA_MAP_ENTRY_58,
	CRYPTO_LBA_MAP_ENTRY_59,
	CRYPTO_LBA_MAP_ENTRY_60,
	CRYPTO_LBA_MAP_ENTRY_61,
	CRYPTO_LBA_MAP_ENTRY_62,
	CRYPTO_LBA_MAP_ENTRY_63,
	CRYPTO_LBA_MAP_ENTRY_MAX
} crypto_entry_idx_t;

typedef enum {
	CRYPTO_KEY_00 = 0,
	CRYPTO_KEY_01,
	CRYPTO_KEY_02,
	CRYPTO_KEY_03,
	CRYPTO_KEY_04,
	CRYPTO_KEY_05,
	CRYPTO_KEY_06,
	CRYPTO_KEY_07,
	CRYPTO_KEY_08,
	CRYPTO_KEY_09,
	CRYPTO_KEY_10,
	CRYPTO_KEY_11,
	CRYPTO_KEY_12,
	CRYPTO_KEY_13,
	CRYPTO_KEY_14,
	CRYPTO_KEY_15,
	CRYPTO_KEY_16,
	CRYPTO_KEY_17,
	CRYPTO_KEY_18,
	CRYPTO_KEY_19,
	CRYPTO_KEY_20,
	CRYPTO_KEY_21,
	CRYPTO_KEY_22,
	CRYPTO_KEY_23,
	CRYPTO_KEY_24,
	CRYPTO_KEY_25,
	CRYPTO_KEY_26,
	CRYPTO_KEY_27,
	CRYPTO_KEY_28,
	CRYPTO_KEY_29,
	CRYPTO_KEY_30,
	CRYPTO_KEY_31,
	CRYPTO_KEY_32,
	CRYPTO_KEY_33,
	CRYPTO_KEY_34,
	CRYPTO_KEY_35,
	CRYPTO_KEY_36,
	CRYPTO_KEY_37,
	CRYPTO_KEY_38,
	CRYPTO_KEY_39,
	CRYPTO_KEY_40,
	CRYPTO_KEY_41,
	CRYPTO_KEY_42,
	CRYPTO_KEY_43,
	CRYPTO_KEY_44,
	CRYPTO_KEY_45,
	CRYPTO_KEY_46,
	CRYPTO_KEY_47,
	CRYPTO_KEY_48,
	CRYPTO_KEY_49,
	CRYPTO_KEY_50,
	CRYPTO_KEY_51,
	CRYPTO_KEY_52,
	CRYPTO_KEY_53,
	CRYPTO_KEY_54,
	CRYPTO_KEY_55,
	CRYPTO_KEY_56,
	CRYPTO_KEY_57,
	CRYPTO_KEY_58,
	CRYPTO_KEY_59,
	CRYPTO_KEY_60,
	CRYPTO_KEY_61,
	CRYPTO_KEY_62,
	CRYPTO_KEY_63,
	CRYPTO_KEY_MAX,
} crypto_key_idx_t;

typedef struct crypto_entry {
	crypto_lba_map_nsid_misc_t lm_nsid_misc;
	crypto_lba_map_range_start_t lm_range_start;
	crypto_lba_map_range_end_t lm_range_end;
} crypto_entry_t;
//Andy_Crypto
///Andy add crypto struct in EPM
typedef struct EPM_crypto_info {
	u32 Crypt_FirstTime;
	u8 crypto_type;
	u8 NameSpace_ID;
	u8 key_valid;
	u8 crypt_entry;
	u32 key1[8];
	u32 key2[8];
	u32 reserve[20];
} EPM_crypto_info_t;
#if 1
///ipc use
///chage mode use
typedef struct crypto_select {
	u8 crypto_config;    //mode 0,1,2,3
	u8 change_key:4;    // generate new key
	u8 key_size:4;
	u8 NSID;	
	u8 cryptID;
} crypto_select_t;
///Load crypto mode use
typedef struct crypto_update {
	u8 crypto_entry;    
	u8 mode;
	u8 reserve[2];
} crypto_update_t;

#endif


/*!
 * @brief program crypto entry
 *
 * @param entry_idx entry index(0 - 63)
 * @param entry this is the structure{ lba map nsid, lba map range start, lba map range end}
 *
 * Note:  When enable Crypto engine, please be sure you have programmed one Gobal entry,
 *        HW always retrieves these entries starting with entry 0, until HW hit the Global entry
 *        or the range start, range end match. So I recommand you always put the Global entry
 *        on the index 63
 *
 * @return none
 */
void crypto_prog_entry(crypto_entry_idx_t entry_idx, crypto_entry_t entry);

/*!
 * @brief Program one encryption key
 *
 * AES-XTS algorithm requires key1 and key2. However, AES-ECB and SM4 requires only
 * one key - key1. SM4 supports ONLY 128 bit keys. For all algorithms, two 32 byte
 * entries must be programmed. If the key size equal to 128 bits (16 bytes),
 * the remaining bits will not be used. But those bits needs to be programmed.
 *
 * @param	key1_array - depending on key size 16 or 32 bytes are valid.
 * @param	key2_array - depending on key size 16 or 32 bytes are valid.
 * @param	key_index - index of key.
 * @param	key_type - type of key (aes-xts/ecb-128/256, sm4-128).
 *
 * Notes:
 * 1. The key details are tabulated below
 * 2. Function will read only the required bytes of keys.
 * --------------------------------------------------------------------------------
 *  algorithm   key size    key1     key2            Comment
 * --------------------------------------------------------------------------------
 * aes-xts-256     32B       32B     32B
 * --------------------------------------------------------------------------------
 * aes-xts-128     16B       16B     16B    Program remaining 16B of key1 and
 *                                          key2 with zeroes
 * --------------------------------------------------------------------------------
 * aes-ecb-256     32B       32B     00B    Program remaining 32B of key2
 *                                          with zeroes
 * --------------------------------------------------------------------------------
 * aes-ecb-128     16B       16B     00B    Program remaining 16B of key1 and
 *                                          32B of key2 with zeroes
 * --------------------------------------------------------------------------------
 *    sm4-128      16B       16B     00B    Program remaining 16B of key1 and
 *                                          32B of key2 with zeroes
 * --------------------------------------------------------------------------------
 *
 * @return	None
 */
void crypto_hw_prgm_one_key(u8 *key1_array, u8 *key2_array,
	u8 key_index, enum crypto_key_type key_type);

/*!
 * @brief Trigger Crypto Self test procedure
 *
 * Trigger Crypto Self test procedure
 *
 * @param	data unit number(maximum 0xFFFFh)
 *
 * @return	true means Crypto selftest failed
 */
bool crypto_hw_selftest(u32 du_cnt);

/*!
 * @brief Trigger MEK refresh request
 *
 * @param none
 *
 * @return none
 */
void crypto_mek_refresh_trigger(void);

/*!
 * @brief change the range start of the specific crypto entry
 *
 * @param entry_idx the entry index(0 - 63)
 * @param range_start the start LBA of this Locking Range
 *
 * @return none
 */
void crypto_entry_chg_range_start(u32 entry_idx, u32 range_start);

/*!
 * @brief change the range end of the specific crypto entry
 *
 * @param entry_idx the entry index(0 - 63)
 * @param range_end the end LBA of this Locking Range
 *
 * @return none
 */
void crypto_entry_chg_range_end(u32 entry_idx, u32 range_end);

/*!
 * @brief change the configuration select of the specific crypto entry
 *
 * @param entry_idx the entry index(0 - 63)
 * @param cfg_sel configuration select(0 - 3)
 *
 * @return none
 */
void crypto_entry_chg_cfg_sel(u32 entry_idx, u32 cfg_sel);

/*!
 * @brief crypto init(Configurations, MEK SRAM init, LBA map enable etc)
 *
 * @param none
 *
 * @return none
 */
void crypto_init(void);

#if _TCG_

/*!
 * @brief set TCG AES range
 *
 * @param none
 *
 * @return none
 */

void tcg_set_aes_range(u8 crypto_type, u8 cryptoID, u8 nsid, u8 isGlobal, u8 arr_id);

/*!
 * @brief set TCG AES keys
 *
 * @param crypto_type
 * @param cryptoID     entry_id @ pLockingRangeTable (for single NS)   -> equals to LOCKING_RANGE_CNT means "Global Range"
 *                     NS_ID                         (for multiple NS)
 * @param isGlobal
 *
 * @return none
 */

void tcg_init_aes_key_range(void);

#endif

#endif //USE_CRYPTO_HW
