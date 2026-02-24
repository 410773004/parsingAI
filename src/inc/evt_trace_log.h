#pragma once

#include "types.h"

typedef struct __attribute__((packed))
{
    union 
    {
        struct 
        {
            u32 log_number;
            union 
            {
                u32 all;
                struct 
                {
                    u32 event_id:16;
                    u32 length:8;
                    u32 log_level:4;
                    u32 cpu_id:3;
                    u32 is_encoded:1;
                };
            }info;            
        }encode;

        struct 
        {
            u32 vp[2];
        }encode_ex;
        
        struct 
        {
            u32 log_number;
            union 
            {
                u32 all;
                struct 
                {
                    u32 rsvd:16;
                    u32 length:8;
                    u32 log_level:4;
                    u32 cpu_id:3;
                    u32 is_encoded:1;
                };
            }info;
        }string;
        
        struct 
        {
            char msg8bytes[8];
        }string_ex;
    } ;
} log_buf_t;

typedef struct __attribute__((packed)) _ev_log_desc
{
    u32 pda;
    u32 flush_id_lo; 
    u32 flush_id_hi; 
    u32 data_page_cnt;
}ev_log_desc_t;

typedef enum {
    LOG_DEBUG    = 0x0,
    LOG_INFO     = 0x1,
    LOG_WARNING  = 0x2,
    LOG_ERR      = 0x3,
    LOG_ALW      = 0x4,
    LOG_PANIC    = LOG_ALW,
    LOG_TENCENT  = 0x5
    //LOG_ISR  	 = 0xF, //Albert 20210204 temporary for ISR
} log_level_t;

typedef enum {
    LOG_IRQ_DOWN  = 0x0,
    LOG_IRQ_DO    = 0x1,
    LOG_IRQ_REST  = 0x2,
} log_stat_t;

 typedef enum
{
	RO_MD_OUT = 0,
	RO_MD_IN,
	RO_MD_WAIT_RX_FINISH,
	RO_MD_NULL = 255,
}stRO_MODE;


// common
#define LOG_IS_ENCODED(x)     (!!((x) & 0x80000000)      )
#define LOG_WHICH_CPU(x)      (  ((x) & 0x70000000) >> 28)
#define LOG_GET_LOG_LEVEL(x)  (  ((x) & 0x0F000000) >> 24)
#define LOG_GET_LOG_LENGTH(x) (  ((x) & 0x00FF0000) >> 16)
// for encode
#define LOG_GET_MODID(x)      (  ((x) & 0x0000F000) >> 12)
#define LOG_GET_EVTID(x)      (   (x) & 0x00000FFF       )

#define LOG_SET_ENCODED_INFO(cpu_id,log_level,length,mod_id,evt_id) ((1 << 31) | (((cpu_id) & 0x00000007) << 28) | (((log_level) & 0x0000000F) << 24) | (((length) & 0x000000FF) << 16) | (((mod_id) & 0x0000000F) << 12) | ((evt_id) & 0x00000FFF))
#define LOG_SET_STRING_INFO(cpu_id,log_level,length)                ((0 << 31) | (((cpu_id) & 0x00000007) << 28) | (((log_level) & 0x0000000F) << 24) | (((length) & 0x000000FF) << 16))

#define LOG_FLUSH_EXPECT   1
#define LOG_FLUSH_ABSOLUTE 2

#define LOG_FLUSH_OPTION_UART_ONLY     1
#define LOG_FLUSH_OPTION_NAND_ONLY     2
#define LOG_FLUSH_OPTION_UART_AND_NAND 3

#define FLUSH_REASON_JIST_DO_IT  0
#define FLUSH_REASON_EVT_TRIGGER 1

#define FLUSH_ALL_LOG       0xFFFF

#define CACHE_IF_NOT_FULL  0
#define MUST_TO_NAND       1 

#define ONLY_THIS_CPU      0
#define ALL_CPU            1

#define EVT_TRIGGER_WITH_UART    1
#define EVT_TRIGGER_WITHOUT_UART 2

// ----------------------------------------------------------------------------
// Export function
// ----------------------------------------------------------------------------
void flush_to_nand(u16 evt_desc_id);
void evlog_clear_nand_log_block_and_reset();
pda_t idx_to_pda(int idx);

typedef u8 nal_status_t;	///< Set by enum ficu_du_status

nal_status_t read_one_evtb_page(pda_t pda, u32 dtag);
pda_t get_evtb_page_pda(pda_t base, u32 page_idx);
void get_ev_log_desc(ev_log_desc_t *desc, u32 elem_cnt);


