import tiktoken

def count_tokens(text: str, encoding_name: str = "cl100k_base") -> int:
    enc = tiktoken.get_encoding(encoding_name)
    return len(enc.encode(text))

# 測試
text = ''''================================================================================
DEVICE INFO
================================================================================
Firmware Version : FG274060
PGR Version      : PGRMQ
Loader Version   : LDR0L
NAND Config      : 8CH*1CE*1LUN*4PL

================================================================================
SMART INFO
================================================================================
Smart Log for NVME device:nvme0 namespace-id:ffffffff
critical_warning						: 0
temperature								: 40 C
available_spare							: 100%
available_spare_threshold				: 10%
percentage_used							: 0%
endurance group critical warning summary: 0
data_units_read							: 5,609,296
data_units_written						: 3,081,483
host_read_commands						: 380,772,312
host_write_commands						: 349,863,898
controller_busy_time					: 2
power_cycles							: 3,190
power_on_hours							: 1,006
unsafe_shutdowns						: 3,188
media_errors							: 0
num_err_log_entries						: 0
Warning Temperature Time				: 0
Critical Composite Temperature Time		: 0
Temperature Sensor	1					: 40 C
[2x] Thermal Management T* Trans Count		: *
[2x] Thermal Management T* Total Time		: *

================================================================================
EVENT SUMMARY
================================================================================
enter read only mode : 3
Critical Warning : 3
leave read only mode : 3
Telemetry update : 1
nvme save log : 1
[2x] 
================================================================================
TEMPERATURE SUMMARY
================================================================================
Sample Count    : 24821
Temp Min / Max  : -2 / 249
Temp Avg        : 31.2
SOC Min / Max   : 12 / 60
SOC Avg         : 46.6
Below 0C Count : 10
Above 85C Count : 5

================================================================================
EVENT FLOW
================================================================================
Total lines    : 1704453
Total segments : 425
Unique paths   : 2

[3] enter read only mode → Critical Warning → leave read only mode

[1] Telemetry update → nvme save log

================================================================================
EVENT DETAIL
================================================================================
--------------------------------------------------------------------------------
FLOW : enter read only mode → Critical Warning → leave read only mode
COUNT: 3
SOURCE FILE : Hs00006381.log
LINE RANGE  : 150339 ~ 151121 (783 lines)
--------------------------------------------------------------------------------
[5x] spb_pool_table() - [FTL]Pool[*] H:[*] T:[*] Cnt:[*]
flush_st_flush_done() - initial gc_suspend_stop_next_spb 0 shr_shutdownflag 0
flush_fsm_done() - flush_fsm_done in 757 ms
_ipc_ftl_flush_done() - NORMAL_Flush
[5x] ftl_core_flush_shutdown() - ns:*(*), wreq_cnt:*
ftl_set_force_dump_pbt() - [PBT]set_force:0, force_mode:0, flush_io_each:1 loop_id
x:0
ftl_pbt_init() - [PBT]ttl_seg:9539, ratio:20, gap:120, res_cnt:1, l2pp_per_seg:12,
flushio:192
ftl_set_pbt_halt() - [PBT]halt:0
ftl_core_flush_shutdown() - system clean!
ftl_core_flush_shutdown() - [Pre]epm_ftl_data->spor_last_rec_blk_sn : 0x1b13
ftl_core_flush_shutdown() - [After]epm_ftl_data->spor_last_rec_blk_sn : 0x1b16
ftl_core_flush_shutdown() - save fake qbt done !!
flush_qbt_done() - fctx:0x9d324 done
smart_for_plp_not_done() - plp not done , loop:0 power cycle:0x00000000 m
ax_blk_sn:0x0
smart_for_plp_not_done() - plp not done , loop:1 power cycle:0x00000000 m
ax_blk_sn:0x0
smart_for_plp_not_done() - smart plp : 0 fail cnt : 0
main() - all init done\n
[2x] pcie_dbglog() - R*:*, R*:*, R*C:*, R*:*,
pcie_dbglog() - R301C:2008000, R303C:201eee0, R00A4:20000d, R30B4:11, R3004:800
004,
pcie_dbglog() - R3064:18000000, R3080:0, R3000:502041, R00A8:40004, R0094:0,
pcie_dbglog() - R0000:0, R3004:800004, R300C:0,
ts_timer_handler() - start thermal throttle, temp 30
[M_T]All_Init_Elapsed 10700554 us
[2x] dtag_add() - *: mem(*) size(*) #Dtags(*)
inflight_cmd_print() - inflight_cmd:0-0-0-0
pstream_supply() - total_bad_die_cnt:0 open_skip_cnt:0 defect_cnt:0, parity info(di
e:7 pln_pair 0)
pstream_supply() - spb:1(1/2), sn:0xfff6, tbl_cvr:0(0)
ftl_slc_pstream_enable() - open slc pstream , pre_wl:0,cur_wl:0 slc_times:0
[3x] dtag_add() - *: mem(*) size(*) #Dtags(*)
GetSensorTemp_check() - smb_intr_sts: 0x5
pcie_link_timer_handling() - PCIe gen 1 x 1
[4x] pcie_phy_leq_dfe
pcie_phy_cali_results() - PLL_BAND criteria : 150 < PLL_BAND < 400
pcie_phy_cali_results() - PLL_BAND [172]
pcie_phy_cali_results() - CDR_BAND criteria : 80 < CDR_BAND < 300
pcie_phy_cali_results() - CDR_BAND 0[113] 1[0] 2[0] 3[0]
pcie_phy_cali_results() - DAC criteria : -110 < DAC < 110
[4x] pcie_phy_cali_results() - DAC *[*]
pcie_link_timer_handling() - No clk: 0, No perst: 0, CE Cnt: 14
HalOtpValueConfirm() - [SOC] OTP val 0
[2x] pcie_dbglog() - R*:*, R*:*, R*C:*, R*:*,
pcie_dbglog() - R301C:2008000, R303C:201eee0, R00A4:20000d, R30B4:11, R3004:800
004,
pcie_dbglog() - R3064:18000000, R3080:0, R3000:502041, R00A8:40004, R0094:0,
pcie_dbglog() - R0000:0, R3004:800004, R300C:0,
pcie_link_timer_handling() - Enable RxErr interrupt
epm_update
trim_send_epm_update() - power on save trim table
epm_update
[2x] epm_header_update
plp_cap_check() - discharge h 0x0 l 0x17
plp_cap_check() - plp test esr status:0x0 time:46 STRPG_status:0
GC_Mode_Assert() - [GC] GC_Mode_Assert 0 FreeBlkCnt:15, BadBlockCnt:6
plp_cap_check() - spb 1 good_plane_ttl_cnt:32, esr_err_fua_flag:0
get_open_gc_blk_rd() - [GC]get_open_gc_blk_rd: 0
[2x] epm_update
[2x] epm_header_update
power_on_epm_flush_done() - power on epm call back
pcie_isr() - pcie int. status: 0x10, timestamp of cpu1: 0x0332c
[2x] pcie_dbglog() - R*:*, R*:*, R*C:*, R*:*,
pcie_dbglog() - R301C:2000000, R303C:201eee0, R00A4:20000d, R30B4:11, R3004:800
0f4,
pcie_dbglog() - R3064:18000000, R3080:0, R3000:502041, R00A8:40004, R0094:0,
pcie_dbglog() - R0000:100000, R3004:8000f4, R300C:30,
pcie_isr() - PCIe CFG MEM SPACE enable BAR0 00
pcie_isr() - pcie int. status: 0x20, timestamp of cpu1: 0x0332c
[2x] pcie_dbglog() - R*:*, R*:*, R*C:*, R*:*,
pcie_dbglog() - R301C:2000000, R303C:201eee0, R00A4:20000d, R30B4:11, R3004:800
0e4,
pcie_dbglog() - R3064:18000000, R3080:0, R3000:502041, R00A8:40004, R0094:0,
pcie_dbglog() - R0000:100000, R3004:8000e4, R300C:20,
pcie_isr() - PCIe CFG MEM SPACE disable
[evt by cpu1] PCIE Link Error
POH:0x000003DD, Power cycle cnt:0x00000C73
PGR Ver : PGRMQ
Loader Ver : LDR0L
SN: 0025511W0RQR
Rx Err Cnt: 0
Retrain(timer) Cnt: 0
cpu_msg_isr_cpu() - cpu msg time cost > 50,tx:1 msg:76 rptr:2 wptr:3
pcie_isr() - pcie int. status: 0x10, timestamp of cpu1: 0x042cc
[2x] pcie_dbglog() - R*:*, R*:*, R*C:*, R*:*,
pcie_dbglog() - R301C:2000000, R303C:2f1eee0, R00A4:20000d, R30B4:41, R3004:280
00d4,
pcie_dbglog() - R3064:18000000, R3080:fcc00000, R3000:502041, R00A8:40004, R009
4:0,
pcie_dbglog() - R0000:100000, R3004:28000d4, R300C:10,
pcie_isr() - PCIe CFG MEM SPACE enable BAR0 0fcc00000
nvmet_slow_isr() - [RSET] CC_EN SET
nvmet_config_xfer_payload() - PCIE mps 1 mrr 5
nvmet_reinit_feat() - reinit nvme feature (saved) -> (cur)
get_open_gc_blk_rd() - [GC]get_open_gc_blk_rd: 0
clear fc 0 wptr 1528 rptr 321 b_wptr_n 66 b_rptr 66
gc_action() - gc_suspend_stop_next_spb 0
nvmet_set_feature() - SetFeatures: FID(0x7) SV(0) DW11(0x10001) NSID
(0)
nvmet_feat_set_number_of_queues() - [Andy] SQ flag: 0, CQ flag: 0 ,IO
Q flag 0\n
nvmet_identify() - Identify: CNS(1) NSID(0) CNTID(0)
dump_idtfy_ctrlr() - tnvmcap: 6f-c86d6000
dump_idtfy_ctrlr() - unvmcap: 0-0
dump_idtfy_ctrlr() - fna(format_all_ns):0
nvmet_identify() - Identify: CNS(2) NSID(0) CNTID(0)
nvmet_create_io_cq() - Create I/O CQ: CQID(1) SIZE(63) PC(1)
nvmet_create_io_sq() - Create I/O SQ: SQID(1) CQID(1) SIZE(63) PRIO(0
)
nvmet_identify() - Identify: CNS(0) NSID(1) CNTID(0)
dump_idtfy_ns() - nsze: 0-37e436b0
dump_idtfy_ns() - ncap: 0-37e436b0
dump_idtfy_ns() - nuse: 0-37e436b0
dump_idtfy_ns() - nvmcap: 6f-c86d6000
dump_idtfy_ns() - sector bitz: 9
dump_idtfy_ns() - nlbaf: 1, flbas:(format)0
nvmet_security_cmd() - req(853b8) opcode(82) len 200
nvmet_security_cmd() - protocol(0) ComId(0)
nvmet_security_received_cmd() - nvmet_security_received_cmd, protoco
l 0 SP_Specific 0 dw11 200
nvmet_security_mem_free() - req(853b8) opcode(82)
nvmet_security_cmd() - req(853b8) opcode(82) len 200
nvmet_security_cmd() - protocol(0) ComId(0)
nvmet_security_received_cmd() - nvmet_security_received_cmd, protoco
l 0 SP_Specific 0 dw11 200
nvmet_security_mem_free() - req(853b8) opcode(82)
inflight_cmd_print() - inflight_cmd:0-0-0-0
patrol_read() - blank block flag 0x80, skip it
nvmet_security_cmd() - req(853b8) opcode(82) len 200
nvmet_security_cmd() - protocol(0) ComId(0)
nvmet_security_received_cmd() - nvmet_security_received_cmd, protoco
l 0 SP_Specific 0 dw11 200
nvmet_security_mem_free() - req(853b8) opcode(82)
nvmet_get_feature() - GetFeatures: FID(0x80) SEL(3)
nvmet_asq_handle() - req(853b8) Admin opcode(a) CE: SCT(0) SC(2)
nvmet_core_handle_cq() - sq 0 cid 9 err sct 0 sc 2
nvmet_identify() - Identify: CNS(1) NSID(0) CNTID(0)
dump_idtfy_ctrlr() - tnvmcap: 6f-c86d6000
dump_idtfy_ctrlr() - unvmcap: 0-0
dump_idtfy_ctrlr() - fna(format_all_ns):0
nvmet_security_cmd() - req(853b8) opcode(82) len 200
nvmet_security_cmd() - protocol(0) ComId(0)
nvmet_security_received_cmd() - nvmet_security_received_cmd, protoco
l 0 SP_Specific 0 dw11 200
nvmet_security_mem_free() - req(853b8) opcode(82)
nvmet_identify() - Identify: CNS(1) NSID(0) CNTID(0)
dump_idtfy_ctrlr() - tnvmcap: 6f-c86d6000
dump_idtfy_ctrlr() - unvmcap: 0-0
dump_idtfy_ctrlr() - fna(format_all_ns):0
nvmet_identify() - Identify: CNS(1) NSID(0) CNTID(0)
dump_idtfy_ctrlr() - tnvmcap: 6f-c86d6000
dump_idtfy_ctrlr() - unvmcap: 0-0
dump_idtfy_ctrlr() - fna(format_all_ns):0
nvmet_identify() - Identify: CNS(0) NSID(1) CNTID(0)
dump_idtfy_ns() - nsze: 0-37e436b0
dump_idtfy_ns() - ncap: 0-37e436b0
dump_idtfy_ns() - nuse: 0-37e436b0
dump_idtfy_ns() - nvmcap: 6f-c86d6000
dump_idtfy_ns() - sector bitz: 9
dump_idtfy_ns() - nlbaf: 1, flbas:(format)0
nvmet_get_log_page() - GetLogPage: LID(2) NUMD(127) PRP1(c8caf000) PRP2(0)
get_host_ns_spare_cnt() - y_avail:137 spare:95 host_need_spb_cnt:682 host op:0 un alloc pool cnt: 6
health_get_io_info() - controller_busy_time HW/SW 147/0
health_get_temperature() - Set critical warning bit[3][1] because temp > 8
5
[evt by cpu1] enter read only mode
POH:0x000003DD, Power cycle cnt:0x00000C73
PGR Ver : PGRMQ
Loader Ver : LDR0L
SN: 0025511W0RQR
Rx Err Cnt: 0
Retrain(timer) Cnt: 0
health_get_temperature() - temp 522, (273 343)
health_get_temperature() - set critical warning bit[1] because touch thres
hold
ipc_get_spare_avg_erase_cnt_done() - smart error a
AER_Polling_SMART_Critical_Warning_bit() - AER smarten: 63 smart warn
ing: 10
AER_Polling_SMART_Critical_Warning_bit() - sub type: 1\n
[evt by cpu1] Critical Warning
POH:0x000003DD, Power cycle cnt:0x00000C73
PGR Ver : PGRMQ
Loader Ver : LDR0L
SN: 0025511W0RQR
Rx Err Cnt: 0
Retrain(timer) Cnt: 0
nvmet_security_cmd() - req(853b8) opcode(82) len 200
nvmet_security_cmd() - protocol(0) ComId(0)
nvmet_security_received_cmd() - nvmet_security_received_cmd, protoco
l 0 SP_Specific 0 dw11 200
nvmet_security_mem_free() - req(853b8) opcode(82)
RO RETURN cmd->opc : 00000000, sts: 00000100
nvmet_core_handle_cq() - sq 1 cid 50 err sct 1 sc 130
RO RETURN cmd->opc : 00000000, sts: 00000100
nvmet_core_handle_cq() - sq 1 cid 51 err sct 1 sc 130
[7x] patrol_read() - blank block flag *, skip it
get_avg_erase_cnt() - A: 11, Max: 53, Min: 3, t: 9774
get_host_ns_spare_cnt() - y_avail:137 spare:95 host_need_spb_cnt:682 host op:0 un alloc pool cnt: 6
[3x] patrol_read() - blank block flag *, skip it
inflight_cmd_print() - inflight_cmd:0-0-0-0
[4x] patrol_read() - blank block flag *, skip it
[4x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
thermal_throttle_credit_training() - avg 249 prv 30 0
patrol_read() - blank block flag 0x81, skip it
thermal_throttle() - thermal throttle 0 -> 3
[8x] patrol_read() - blank block flag *, skip it
[4x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
inflight_cmd_print() - inflight_cmd:0-0-0-0
[8x] patrol_read() - blank block flag *, skip it
[8x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
inflight_cmd_print() - inflight_cmd:0-0-0-0
[9x] patrol_read() - blank block flag *, skip it
[7x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
inflight_cmd_print() - inflight_cmd:0-0-0-0
patrol_read() - blank block flag 0x81, skip it
[8x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
inflight_cmd_print() - inflight_cmd:0-0-0-0
[2x] patrol_read() - blank block flag *, skip it
[2x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
thermal_throttle_credit_training() - avg 252 prv 249 3
[4x] patrol_read() - blank block flag *, skip it
[2x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
thermal_throttle_credit_training() - avg 249 prv 252 3
[5x] patrol_read() - blank block flag *, skip it
[4x loop]
{
get_avg_erase_cnt() - A: *, Max: *, Min: *, t: *
get_host_ns_spare_cnt() - y_avail:* spare:* host_need_spb_cnt:* host op:* unalloc pool cnt: *
patrol_read() - blank block flag *, skip it
}
thermal_throttle_credit_training() - avg -2 prv 249 3
patrol_read() - blank block flag 0x81, skip it
thermal_throttle() - clear critical warning bit[3] -2 because temp <= 85
ipc_leave_read_only_handle() - OUT RO 0
[evt by cpu4] leave read only mode
'''
print("Token 數:", count_tokens(text))