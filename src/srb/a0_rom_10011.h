#ifndef _ROM_H_
#define _ROM_H_
      
#define memcpy_rom_addr 	 0x100069cb
#define printk_rom_addr 	 0x100066bd
#define ndcu_dev_reset_rom_addr 	 0x10004651
#define ncl_submit_cmd_rom_addr 	 0x100030e9
#define ficu_mode_disable_rom_addr 	 0x10004e59
#define ndcu_enable_reg_control_mode_rom_addr 	 0x10004151
#define ndcu_disable_reg_control_mode_rom_addr 	 0x10004185
#define ficu_mode_toggle_rom_addr 	 0x10004ea1
#define ndcu_set_flash_type_rom_addr 	 0x100046e9
#define ndcu_set_feature_rom_addr 	 0x10004551
#define otp_get_pk_rom_addr 	 0x10000a69
#define bf_sm3_cmp_rom_addr 	 0x10002ca5
#define bf_sha3_cmp_rom_addr 	 0x10002c4d
#define bm_sha3_sm3_calc_sec_rom_addr 	 0x10002a31
#define bsec_sm2_verify_rom_addr 	 0x10005bd5
#define rsa_verify_rom_addr 	 0x10006db9
#define bm_compare_rom_addr 	 0x10002879
#define detected_nand_if_rom_addr 	 0x201f8de1
#define nal_ymtc_tlc_rom_addr 	 0x201f8df5
#define ncb_flash_type_rom_addr 	 0x201f8e1a
#if 1
#define ddrcfg_buf_rom_addr 	 0x2003d000 //0x201fe000
#define com_buf_rom_addr 	 0x2003e000	//0x201ef000
#else
#define ddrcfg_buf_rom_addr 	 0x201f6000
#define com_buf_rom_addr 	 0x201f7000
#endif
#define sec_buf_a_rom_addr 	 0x201fd000
#define pub_key_rom_addr 	 0x201fa000
#define pub_key1_otp_rom_addr 	 0x201f8ca0
#define pub_key2_otp_rom_addr 	 0x201f8cc0
#define bk_32b0_rom_addr 	 0x201f8c20
#define bk_32b1_rom_addr 	 0x201f8c40
#define bk_64b_rom_addr 	 0x201f8c60
#define fins_tmpl_rom_addr 	 0x201f8de4
#define memset_rom_addr 	 0x100069a7
#define ficu_get_fda_list_ptr_rom_addr 	 0x10004dd5
#define get_column_address_rom_addr 	 0x1000321d
#define ficu_update_rom_addr 	 0x10004db9
#define ficu_wait_fcmd_status_rom_addr 	 0x10004eb1
#define detected_CH_rom_addr 	 0x201f80c0
#define detected_CE_rom_addr 	 0x201f80bc
#define uart_print_disable_rom_addr 	 0x201f8e30
       
#endif
