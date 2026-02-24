

#pragma once
#include "types.h"
#include "fc_export.h"
#include "cpu_msg.h"


typedef union _esr_err_flags_t{
    u8 all;
    struct{
        u8 plp_cap_err : 1;
        u8 strpg : 1;
        u8 esr_err : 1;
        u8 iic_read_err : 1;
		u8 strpg_bak : 1;
    } b;
}esr_err_flags_t;

extern u8 evt_mtp_check;
extern u8 evt_plp_set_ENA; 
extern u32 check_time;
extern volatile u64 plp_down_time;
extern volatile bool plp_test_flag;
extern u8 evt_plp_flush;
extern u8 evt_check_streaming;
void mtp_check_task(u32 param, u32 cap_type, u32 cmd_code);
void ipc_plp_flush_done(ftl_core_ctx_t *fctx);
void plp_one_time_init(void);
void ipc_plp_done_ack(volatile cpu_msg_req_t *req);
bool plp_init(u8 cap_type);
void plp_set_ENA(u32 param, u32 flag, u32 r1);
void plp_read_ENA();

void ipc_plp_set_ENA(volatile cpu_msg_req_t *req);


#if PLP_DEBUG
void ipc_plp_debug(volatile cpu_msg_req_t *req);
#endif



