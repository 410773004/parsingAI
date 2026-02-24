//=============================================================================
//
/*! \file
 * @brief trim function support
 *
 * \addtogroup dispatcher
 * \defgroup rainier
 * \ingroup dispatcher
 * @{
 * todo 1.NS condition
 */
//=============================================================================
#include "string.h"
#include "sect.h"
#include "stdio.h"
#include "stdlib.h"
#include "mpc.h"
#include "fc_export.h"
#include "trim.h"
#include "dtag.h"
#include "ddr.h"
#include "bg_training.h"
#include "ncl.h"
#include "eccu.h"
#include "ncl_cmd.h"
#include "epm.h"
#include "stdlib.h"
#include "nvme_spec.h"
#include "event.h"
#include "rdisk.h"
#include "ddr.h"

#define __FILEID__ trim
#include "trace.h"
#if (TRIM_SUPPORT == ENABLE)
sram_sh_data u8* TrimBlkBitamp;
sram_sh_data u8* TrimTable;
//share_data u8* TrimTblPtr;
fast_data_zi Trim_Info TrimInfo;
sram_sh_data volatile u32 Trim1bitmapSize;
fast_data_zi bool trim_host_write;
fast_data_zi bool Full_TRIM_IPC_SEND;
#if(BG_TRIM == ENABLE)
fast_data_zi bg_trim_mgr_t bg_trim;
sram_sh_data volatile trim_dtag_free_t bg_trim_free_dtag;
extern u8 evt_trim_range_issue;
extern volatile u32 shr_dtag_ins_cnt;
#ifdef BG_TRIM_ON_TIME
extern void ucache_trim_check();
#endif
fast_data_zi unalign_LDA TrimUnalignLDA;
#endif
extern u32 _max_capacity;
extern epm_info_t*  shr_epm_info;

#define NUM_DTAG_PER_SEGMENT  4
#define TOTAL_SEGMENT ((Trim1bitmapSize + NUM_DTAG_PER_SEGMENT - 1)/NUM_DTAG_PER_SEGMENT)

extern volatile ns_t ns[INT_NS_ID];
extern u16 host_sec_size;//joe add change sec size 20200817
extern u8 host_sec_bitz;//joe add change sec size  20200817

#ifdef ERRHANDLE_ECCT 
extern u16 ecct_cnt;  //ecct cnt in sram  //tony 20201228 
#endif
bool BgTrimIssue(u32 Range);

slow_code void InitTrimInfo(bool poweron, bool one_time_init)
{
    if(!poweron)
    {
        TrimInfo.RecordMaxLBA = InvalidU64;
        TrimInfo.RecordMinLBA = InvalidU64;
        TrimInfo.deallocated_cnt = 0;
        TrimInfo.IsValid = 0;
        TrimInfo.Dirty = 0;
        TrimInfo.TriminfoEndTag = 0x12345678;
        #if(FULL_TRIM == ENABLE)
        TrimInfo.FullTrimTriggered = 0;
        Full_TRIM_IPC_SEND = false;
        #endif
        #if 0//(TRIM_DEBUG == ENABLE)
        disp_apl_trace(LOG_ERR, 0x0c6a, "init trim info all\n");
        #endif
    }
    BgTrimInit(poweron|one_time_init);
    TrimInfo.TrimFlush = 0;
    TrimInfo.IsPowerLost = 0;
	trim_host_write = false;
}
#if 1
fast_data_zi u32 trim_fill_cnt;
static fast_code int TrimFilldone(void *ctx, dpe_rst_t *rst)
{
    trim_fill_cnt--;
    return 0;
}
#endif
fast_data u8 evt_trim_epm_update = 0xFF;
ddr_code void trim_send_epm_update(u32 unused1,u32 unused2,u32 unused3)
{
	extern volatile u8 plp_trigger;
	if(plp_trigger)
		return;
    //to avoid no PLP cause get the old trim table case 
    epm_trim_t* epm_trim_data = (epm_trim_t*)ddtag2mem(shr_epm_info->epm_trim.ddtag);
    Trim_Info * info = (Trim_Info *)(epm_trim_data->info);
    info->TriminfoEndTag = 0xFFFFFFFF;
	epm_update(TRIM_sign,(CPU_ID-1));
    disp_apl_trace(LOG_INFO, 0x4d82, "power on save trim table");
}

init_code void PowerOnInitTrimTable()
{
    // todo 1.read triminfo
    #if (TRIM_DEBUG == ENABLE)
    u64 time = get_tsc_64();
    #endif
    epm_trim_t* epm_trim_data = (epm_trim_t*)ddtag2mem(shr_epm_info->epm_trim.ddtag);
    epm_FTL_t* epm_ftl_data = (epm_FTL_t*)ddtag2mem(shr_epm_info->epm_ftl.ddtag);
    memcpy((void *)&TrimInfo,(void *)(epm_trim_data->info), sizeof(Trim_Info));
    sys_assert(Trim1bitmapSize << DTAG_SHF <= sizeof( epm_trim_data->TrimTable));
    TrimTable =(u8 *) epm_trim_data->TrimTable;
    TrimBlkBitamp =(u8 *) epm_trim_data->TrimBlkBitamp;
    sys_assert(((u32)TrimTable & 31) == 0);
    bg_trim_free_dtag.wptr = 0;
    bg_trim_free_dtag.rptr = 0;
    #if 1//(TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_INFO, 0xa454, "power on init trim table,triminfotag:%x, TrimInfo.IsValid %x",
        TrimInfo.TriminfoEndTag, TrimInfo.IsValid);
    TrimInfo.Dirty = TrimInfo.IsValid;
    #endif
    #if(BG_TRIM == ENABLE)
	evt_register(BgTrimevt, 0, &evt_trim_range_issue);
    #endif
    if(TrimInfo.TriminfoEndTag == 0x12345678)
    {
        InitTrimInfo(true, false);

        if(TrimInfo.FullTrimTriggered)
        {
            disp_apl_trace(LOG_ERR, 0x1e2d, "power on Full Trim, epm_fmt_not_finish:0x%x, format_tag:0x%x", epm_ftl_data->epm_fmt_not_finish, epm_ftl_data->format_tag);
//            if (epm_ftl_data->epm_fmt_not_finish == 0)
//            {
//                FullTrimHandle(NULL, true);//full trim + spor case
//            }
            ReinitTrimTable(false);
            return;
        }

        if(TrimInfo.IsValid){
            bg_trim.flags.b.clean = 0;
        }

        {
         //for epm not the last power lost one
            //Trim_Info * info = (Trim_Info *)(epm_trim_data->info);
            //info->TriminfoEndTag = 0xFFFFFFFF;
			//epm_update(TRIM_sign,(CPU_ID-1));
			evt_register(trim_send_epm_update, 0, &evt_trim_epm_update);
            if(evt_trim_epm_update != 0xFF){
                evt_set_cs(evt_trim_epm_update, 0, 0, CS_TASK);
            }
        }
    }
    else
    {
        ReinitTrimTable(true);
        //if(TrimInfo.TriminfoEndTag != 0x12345678){
        //    flush_to_nand(EVT_POWERON_NO_TRIM_TABLE);
        //}
    }
    #if(TRIM_DEBUG == ENABLE)
    time = (get_tsc_64()- time)/800;
    disp_apl_trace(LOG_ERR, 0xdce8, "Trim power on init take time:(%x)(%x) us",(u32)(time>>32),(u32)time);
    #endif
}
slow_code void ReinitTrimTable(bool one_time_init)
{
    InitTrimInfo(false, one_time_init);

#if 0
    //bm_data_fill(void *mem, u32 nbytes, u32 pat,dpe_cb_t callback, void *ctx);
    u32 total_bytes = Trim1bitmapSize << DTAG_SHF;
    u32 nbytes = 0;
    u8 *starAddr = TrimTable;

    #ifdef While_break
	u64 start = get_tsc_64();
    #endif	

    while(total_bytes){
        nbytes = min(total_bytes,MAX_BYTES_ONE_TIME);
        trim_fill_cnt++;
        #if (TRIM_DEBUG == ENABLE)
        disp_apl_trace(LOG_INFO, 0x1139, "dep fill Addr0x%x,bytes:0x%x",starAddr,nbytes);
        #endif
        bm_data_fill((void *)starAddr, nbytes, 0, TrimFilldone, NULL);
        total_bytes -= nbytes;
        starAddr += nbytes;

        #ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
        #endif		
    }

    #ifdef While_break
	start = get_tsc_64();
    #endif		

    while(trim_fill_cnt){
        dpe_isr();

        #ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
        #endif			
    }
#else
    memset((void *)TrimTable, 0, Trim1bitmapSize << DTAG_SHF);
#endif

    if(!one_time_init){
        epm_update(TRIM_sign,(CPU_ID-1));//for full trim preformat case save in epm
    }
    #if 1//(TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_ERR, 0xcbf9, "reinit trim table:addr:0x%x,size of %d ddtag\n",TrimTable,Trim1bitmapSize);
    #endif
}

#if 0
fast_code u32 CalTrimTblIdx(lda_t lda )
{
    return lda >> TrimTblShift;
}

fast_code u8 CalTrimTblIdxBit(lda_t lda )
{
    return BIT(lda & TrimTblShift);
}
#endif
fast_data static u8 bitmap[8][8] ={
    {0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF},
    {0x02,0x06,0x0E,0x1E,0x3E,0x7E,0xFE,0xFE},
    {0x04,0x0C,0x1C,0x3C,0x7C,0xFC,0xFC,0xFC},
    {0x08,0x18,0x38,0x78,0xF8,0xF8,0xF8,0xF8},
    {0x10,0x30,0x70,0xF0,0xF0,0xF0,0xF0,0xF0},
    {0x20,0x60,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0},
    {0x40,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0},
    {0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80},
};//bitmap[lda&TrimTblIdxBitMask][Count] = bit value write into Trim table

/*!
 * @brief register or unregister trim table
 * @input start lda , lda count and regop.
 *
 * @return  None
 */

fast_code void TrimTable_op_ByByte(lda_t slda, u32 count,RegOp op)
{
    u32 tblidx = 0;
    u32 bitcount = 0;
    while(count)
    {
        if(count >= (TrimTblBitPerIdx-(slda & TrimTblIdxBitMask)))
        {
            tblidx = slda >> TrimTblShift;
            bitcount = (TrimTblBitPerIdx - (slda & TrimTblIdxBitMask));
            TrimTable[tblidx] = (op ? (TrimTable[tblidx]|bitmap[slda & TrimTblIdxBitMask][bitcount-1])\
                :(TrimTable[tblidx]& (~bitmap[slda & TrimTblIdxBitMask][bitcount-1])));
            slda += bitcount;
            count -= bitcount;
        }
        else
        {
            tblidx = slda >> TrimTblShift;
            TrimTable[tblidx] = (op ? (TrimTable[tblidx]|bitmap[slda & TrimTblIdxBitMask][count-1])\
                :(TrimTable[tblidx]& ~bitmap[slda & TrimTblIdxBitMask][count-1]));
            count = 0;
        }
    }
}
/*
fast_code void TrimTable_op_ByByte2(lda_t slda, u32 count,RegOp op)
{
    u32 tblidx = 0;
    u32 bitcount = 0;
    while(count)
    {
        if(count >= (TrimTblBitPerIdx-(slda & TrimTblIdxBitMask)))
        {
            tblidx = slda >> TrimTblShift;
            bitcount = (TrimTblBitPerIdx - (slda & TrimTblIdxBitMask));
            TrimTable[tblidx] = (op ? (TrimTable[tblidx]|((1 << bitcount) - 1) << (slda & TrimTblIdxBitMask))\
                :(TrimTable[tblidx]& (~(((1 << bitcount) - 1) << (slda & TrimTblIdxBitMask)))));
            slda += bitcount;
            count -= bitcount;
        }
        else
        {
            tblidx = slda >> TrimTblShift;
            TrimTable[tblidx] = (op ? (TrimTable[tblidx]|((1 << count) - 1) << (slda & TrimTblIdxBitMask))\
                :(TrimTable[tblidx]& (~(((1 << count) - 1) << (slda & TrimTblIdxBitMask)))));
            count = 0;
        }
    }
}
*/
fast_code void TrimTable_op_By4Byte(lda_t slda, u32 count,RegOp op)
{
    u32* TrimTable32 = (u32*)TrimTable;
    u32 tblidx = 0;
    u32 bitcount = 0;
    while(count)
    {
        if(count >= (32-(slda & 0x1F)))
        {
            tblidx = slda >> 5;
            bitcount = (32 - (slda & 0x1F));

            TrimTable32[tblidx] = (op ? (TrimTable32[tblidx]|(((1 << bitcount) - 1) << (slda & 0x1F)))\
                :(TrimTable32[tblidx]& (~(((1 << bitcount) - 1) << (slda & 0x1F)))));
            slda += bitcount;
            count -= bitcount;
        }
        else
        {
            tblidx = slda >> 5;
            TrimTable32[tblidx] = (op ? (TrimTable32[tblidx]|(((1 << count) - 1) << (slda & 0x1F)))\
                :(TrimTable32[tblidx]& (~(((1 << count) - 1) << (slda & 0x1F)))));
            count = 0;
        }
    }
}

#define min_dpe_fill_bit (256)
#define min_dpe_fill_bit_mask (min_dpe_fill_bit - 1)
#define min_dpe_fill_bit_shift (8)
#define min_dpe_fill_byte_shift (5)
fast_code bool trim_data_check_clear_bit(u32 idx, u32 sbit, u32 ebit, bool flag_clear_bit){
    Host_Trim_Data *addr = (Host_Trim_Data *)ddtag2mem((idx + bg_trim.startdtag) & DDTAG_MASK);
    if(addr->Validtag == 0x12345678 && addr->valid_bmp){
        for(u32 i = 0;i < addr->Validcnt; i++){
            if (test_bit(i, &addr->valid_bmp)){
                u32 Range = addr->Ranges[i].sLDA >> LDA2TRIMBITSHIFT;
                if(Range >= sbit && Range <= ebit){
                    if(flag_clear_bit)
                        clear_bit(i, &addr->valid_bmp);
                    else
                        return true;
                }
            }
        }
    }
    return false;
}
fast_code void TrimTable_op(lda_t slda, u32 count, RegOp op)
{
    u32 elda = slda + count - 1;
	u32 sbit = LDA2Range_UP(slda, LDA2TRIMBITSHIFT);
	u32 ebit = LDA2Range_Down(elda, LDA2TRIMBITSHIFT);
	if(sbit>ebit || ebit > elda >> LDA2TRIMBITSHIFT){
		return;
	}

	count = ebit - sbit + 1;
	if (op){
        // clear valid_bmp in otf trim data
        if (bg_trim.dtagbmp_Align4M){
            u32 offset = find_next_bit(TrimTable, ebit+1, sbit);
            if(offset <= ebit){
                u32 idx = find_first_bit(&bg_trim.dtagbmp_Align4M, DDR_TRIM_RANGE_DTAG_CNT);
                while(idx < DDR_TRIM_RANGE_DTAG_CNT){
                    trim_data_check_clear_bit(idx, sbit, ebit, true);
                    idx = find_next_bit(&bg_trim.dtagbmp_Align4M, DDR_TRIM_RANGE_DTAG_CNT, idx+1);
                }
            }
        }

        TrimInfo.Dirty = 1;
        TrimInfo.IsValid = 1;
		count = ebit - sbit + 1;
		if(bg_trim.flags.b.clean){
            bg_trim.flags.b.clean = 0;
            bg_trim.timer = get_tsc_64();
            disp_apl_trace(LOG_ERR, 0xec10, "bg_trim trigger");
    			evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
        }else{
			bg_trim.flags.b.needcheck=1;
            bg_trim.flags.b.waitOtfDone = 0;
        }
	}
    u32 s32Byte = LDA2Range_UP(sbit, min_dpe_fill_bit_shift);
    u32 e32Byte = LDA2Range_Down(ebit, min_dpe_fill_bit_shift);
    u32 size = 0;
    if(s32Byte>e32Byte || e32Byte > ebit >> min_dpe_fill_bit_shift){
        TrimTable_op_ByByte(sbit, count, op);
    }
    else{ 
        if(s32Byte <= e32Byte){
            u32 nbytes = 0;//nbytes must 32 bytes align
            u32 starAddr = (u32)(TrimTable + ((s32Byte) << min_dpe_fill_byte_shift));
            size = (e32Byte - s32Byte + 1) << min_dpe_fill_byte_shift;
            while(size){
                nbytes = min(size, MAX_BYTES_ONE_TIME);
                trim_fill_cnt++;
                //sys_assert(starAddr > (u32)TrimTable);
                //sys_assert((starAddr + nbytes) < (u32)TrimTable + (Trim1bitmapSize<<DTAG_SHF));
                bm_data_fill((void *)(starAddr), nbytes, (op?INV_U32:0), TrimFilldone, NULL);
                size -= nbytes;
                starAddr += nbytes;
            }
            while(trim_fill_cnt){
                dpe_isr();
            }
        }

        if(sbit & (BIT(min_dpe_fill_bit_shift)-1)){
            TrimTable_op_ByByte(sbit, (min_dpe_fill_bit - (sbit & min_dpe_fill_bit_mask)), op);
        }
        if((ebit+1) & (BIT(min_dpe_fill_bit_shift)-1)){
            TrimTable_op_ByByte((ebit & ~(min_dpe_fill_bit_mask))  , ((ebit & min_dpe_fill_bit_mask) + 1), op);
        }
    }
}

fast_code void RegOrUnregTrimTable(lda_t slda, u32 count,RegOp op)
{
    if(count == 0) return;
    //sys_assert((slba <= MAXLBA)&&((slba+count-1) <= MAXLBA));
    #if(TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_INFO, 0x063e, "RegOrUnregTrimTable,slda:%x,count:%x,op:%x\n",slda,count,op);
    #endif

    TrimTable_op(slda, count, op);

    TrimInfo.IsPowerLost = 0; 
}


fast_code bool is_lda_in_otf_trimdata(lda_t slda, lda_t elda){

	 while(!CBF_EMPTY(&bg_trim_free_dtag)){
        dtag_t dtag = bg_trim_free_dtag.idx[bg_trim_free_dtag.rptr];
        put_trim_dtag(dtag);
        bg_trim_free_dtag.rptr = (bg_trim_free_dtag.rptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
	}

    if (get_trim_dtag_cnt() < DDR_TRIM_RANGE_DTAG_CNT){
        // u64 time = get_tsc_64();
        u32 idx = find_first_bit(&bg_trim.dtagbmp_NoAlign4M, DDR_TRIM_RANGE_DTAG_CNT);
        while(idx < DDR_TRIM_RANGE_DTAG_CNT){
            Host_Trim_Data *addr = (Host_Trim_Data *)ddtag2mem((idx + bg_trim.startdtag) & DDTAG_MASK);
            if(addr->Validtag == 0x12345678 && !addr->all){
                for(u32 i = 0;i < addr->Validcnt; i++){
                    u32 trim_slda = addr->Ranges[i].sLDA;
                    u32 trim_length = addr->Ranges[i].Length;
                    u32 trim_elda = trim_slda + trim_length;
                    if(trim_slda & (BIT(10)-1)){
                        u32 temp_sLda = trim_slda;
                        u32 temp_eLda = trim_slda + min((BIT(10)-(trim_slda & (BIT(10)-1))), trim_length) - 1;
                        if(max(temp_sLda, slda) <= min(temp_eLda, elda)){
                            return true;
                        }
                    }
                    if(trim_elda & (BIT(10)-1) && (!(trim_slda & (BIT(10)-1)) || (trim_slda>>10 != trim_elda>>10))){
                        u32 temp_sLda = (trim_elda - (trim_elda & (BIT(10)-1)));
                        u32 temp_eLda = temp_sLda + (trim_elda & (BIT(10)-1)) - 1;
                        if(max(temp_sLda, slda) <= min(temp_eLda, elda)){
                            return true;
                        }
                    }
                }
            }
            idx = find_next_bit(&bg_trim.dtagbmp_NoAlign4M, DDR_TRIM_RANGE_DTAG_CNT, idx+1);
        }
    }
    return false;
}

fast_code bool IsCMDinTrimTable(lda_t slda, lda_t elda,bool Iswrite)
{
    #if 0
    u32 stblidx = slda >> 5;
    u32 etblidx = elda >> 5;
    u32 * tbl = (u32 *) TrimTable;
    u32 i;
    if(TrimInfo.IsValid == 1)
    {
        if(slda == elda)

        {
            if((tbl[stblidx]&(~(BIT(slda&0x1F)- 1)))&((BIT((elda+1)&0x1F)- 1)))
                return true;
            else
                return false;
        }
        if(tbl[stblidx]&(~(BIT(slda&0x1F)- 1)))
            return true;
        if(tbl[etblidx]&((BIT((elda+1)&0x1F)- 1)))
            return true;
        for(i = stblidx+1;i < etblidx; i++)
        {
            if(tbl[i])
            {
                return true;
            }
        }
    }
    return false;
    #else

    if(TrimInfo.Dirty != 1)
        return false;

    u32 offset = find_next_bit(TrimTable,(elda>>LDA2TRIMBITSHIFT)+1,slda>>LDA2TRIMBITSHIFT);
    if(offset <= (elda>>LDA2TRIMBITSHIFT)){
        return true;
    }

    if(bg_trim.dtagbmp_NoAlign4M && is_lda_in_otf_trimdata(slda, elda)){
        return true;
    }

    return false;
    #endif
}
extern bool SrchTrimCache(lda_t lda);
fast_code u8 SrchTrimTable(lda_t lda)
{
    //sys_assert(lda <= MAXlda);
    #if(TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_INFO, 0x3a03, "search lda0x%x, result: %x\n", lda, test_bit(lda >> LDA2TRIMBITSHIFT, TrimTable));
    #endif

    if((test_bit(lda >> LDA2TRIMBITSHIFT, TrimTable) || is_lda_in_otf_trimdata(lda, lda)) && !SrchTrimCache(lda)){
        return true;
    }

    return false;
}

fast_code void UpdtTrimInfo(u64 lba, u64 count ,RegOp op)
{
    u64 elba = InvalidU64;
    u64 slba = InvalidU64;
    u64 device_cap = 0;
	if(host_sec_bitz ==9)
		device_cap = ns[0].cap;
	else
		device_cap = (ns[0].cap)>>3;
    if(op == Unregister){
        slba = lba << (LDA_SIZE_SHIFT - host_sec_bitz);
        elba = (((lba + count) << (LDA_SIZE_SHIFT - host_sec_bitz)) -1);
    }else{
        slba = lba;
        elba = slba + count - 1;
    }
    if(TrimInfo.IsValid == 0)
    {
        if(op == Unregister)
        {
            return;
        }
        TrimInfo.RecordMinLBA = slba;
        TrimInfo.RecordMaxLBA = elba;
        trim_host_write = false;
        TrimInfo.IsValid = 1;
        #if(BG_TRIM == ENABLE)
        bg_trim.RecordRangeS = INV_U32;
        bg_trim.RecordRangeE = INV_U32;
        #endif
    }
    else if((TrimInfo.RecordMaxLBA == INV_U64)||(TrimInfo.RecordMinLBA == INV_U64)){
        if(op){
            TrimInfo.RecordMinLBA = slba;
            TrimInfo.RecordMaxLBA = elba;
            trim_host_write = false;
        }
    }
    else
    {
        #if 0
        if(op && trim_host_write){

            TrimInfo.RecordMinLBA = slba;
            TrimInfo.RecordMaxLBA = elba;
            trim_host_write = false;
        }else 
        #endif
        if(slba < TrimInfo.RecordMinLBA){
            if(elba + 1 >= TrimInfo.RecordMinLBA){
                if(op){
                    TrimInfo.RecordMinLBA = slba;
                    TrimInfo.RecordMaxLBA = max(elba, TrimInfo.RecordMaxLBA);
                }else{
                    TrimInfo.RecordMinLBA = INV_U64;
                    TrimInfo.RecordMaxLBA = INV_U64;
                }
            }
            else{
                if(op){
                    TrimInfo.RecordMinLBA = slba;
                    TrimInfo.RecordMaxLBA = elba;
                }
            }
        }
        else{
            if(TrimInfo.RecordMaxLBA + 1 >= slba){
                if(op){
                    //TrimInfo.RecordMinLBA = slba;
                    TrimInfo.RecordMaxLBA = max(elba, TrimInfo.RecordMaxLBA);
                }else{
                    TrimInfo.RecordMinLBA = INV_U64;
                    TrimInfo.RecordMaxLBA = INV_U64;
                }
            }
            else {
                if(op){
                    TrimInfo.RecordMinLBA = slba;
                    TrimInfo.RecordMaxLBA = elba;
                }
            }
        }

    }
    if(op){
        TrimInfo.deallocated_cnt += count;
    }
    else{
        TrimInfo.deallocated_cnt -= count;
    }
    #if (FULL_TRIM == ENABLE)
    if(Full_TRIM_IPC_SEND){
        return;
    }
    if((TrimInfo.RecordMinLBA == 0)&&((TrimInfo.RecordMaxLBA+1) == device_cap))
    {
        //TrimInfo.FullTrimTriggered = 1;
        //goto bgtrim;
        #if 0//(TRIM_DEBUG == ENABLE)
        disp_apl_trace(LOG_INFO, 0x9a35, "Full Trim triggered");
        #endif
    }
    #endif
    #if 0//(BG_TRIM == ENABLE)
    else if(op&&(TrimInfo.RecordMinLBA != InvalidU64)&&(TrimInfo.RecordMaxLBA != InvalidU64)){
		bgtrim:
        bg_trim.flags.b.newin = 1;
        u32 lbashift = TOTAL_BG_TRIM_RANGE2LDA_SHIFT + (LDA_SIZE_SHIFT - host_sec_bitz);
        u32 sRange = LDA2Range_UP(TrimInfo.RecordMinLBA,lbashift);
        u32 eRange = 0;
        if(TrimInfo.RecordMaxLBA>>lbashift){
            eRange = LDA2Range_Down(TrimInfo.RecordMaxLBA,lbashift);
        }
        if(((eRange > sRange)&&(eRange - sRange) >= BG_TRIM_RANGE_TRIGGER_CNT)){
            BgTrimRangeCal(sRange,eRange);
        }
    }
    #endif
    #if 0//(TRIM_DEBUG == ENABLE)
    //disp_apl_trace(LOG_ERR, 0, "trim info,minlba:0x%x,maxlba:0x%x,islbaseq:%d\n",TrimInfo.HostTrimMinLBA,TrimInfo.HostTrimMaxLBA,TrimInfo.IsLBASeq);
    disp_apl_trace(LOG_ERR, 0xfa97, "recordMaxlda%x,recordMinlda%x\n",TrimInfo.RecordMaxLDA,TrimInfo.RecordMinLDA);
    #endif
}

share_data_zi u32 trim_data_send_cnt;
share_data_zi u32 trim_data_recev_cnt;

fast_code bool CommitTrimDtag(u32 dtag, u32 IsBG)
{
	u32 wptr = shr_dtag_comt.que.wptr;
	u32 size = shr_dtag_comt.que.size;
    u32 wptr1 = wptr;
    wptr1++;
    if (wptr1 == size) {
        wptr1 = 0;
    }
	if(!IsBG){
		trim_data_send_cnt++;
	}
    while (wptr1 == shr_dtag_comt.que.rptr){
        cpu_msg_isr();
    }
	shr_dtag_comt.que.buf[wptr].dtag.dtag = dtag|(IsBG<<31);
	shr_dtag_comt.lda[wptr] = TRIM_LDA;
	if (++wptr == size)
		wptr = 0;
    dmb();
	shr_dtag_ins_cnt += 1;
	shr_dtag_comt.que.wptr = wptr;
    dmb();
	bg_trim.timer = get_tsc_64();
	return true;
}

extern volatile u8 plp_trigger;
#ifdef NS_MANAGE
extern struct ns_section_id ns_sec_id[];//joe 20200721 for ftl core
extern struct ns_array_manage *ns_array_menu;
extern void cache_clear_trim_bit(Host_Trim_Data * trim_data);
extern void cache_check_trim_hit_par_data(Host_Trim_Data * trim_data);
fast_code bool Trim_handle(Host_Trim_Data * trim_data)
{
    u32 i = 0;
    u32 cnt = trim_data->Validcnt;
   u64 transfer_lda=0;//joe add 20200914
   u32 nsid = trim_data->nsid;
   u16 sec_id=0;
   u16 sec_id2=0;
   u64 slda_last;
   u64 slda_trim;
   u32 transfer_length=0;
   u32 length_record=0;
   //log_isr = LOG_IRQ_REST;
    #if 0//(TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_ERR, 0xfbcd, "Trim handle range cnt:%d\n",cnt);
    #endif
    for(;i < cnt; i++)
    {
        #if 1
        if(plp_trigger)
        {
            trim_data ->Validtag = INV_U32;
            return false;
        }
        #endif
        //joe add trim lda transfer 20200914
        length_record = trim_data->Ranges[i].Length;
        slda_trim = trim_data->Ranges[i].sLDA;
        // disp_apl_trace(LOG_DEBUG, 0x9de9, "trim_data->Ranges[i].sLDA1:0x%x   trim_data->Ranges[i].Length1:0x%x\n",slda_trim,length_record);	
        bool flag_append = false;
        while(length_record > 0)
		{
            if(plp_trigger)
            {
                trim_data->Validtag = INV_U32;
                return false;
            }
			sec_id = slda_trim / 0x200000;	
			slda_last = slda_trim - sec_id * 0x200000;
			sec_id2 = ns_array_menu->ns_array[nsid].sec_id[sec_id];
			transfer_lda = sec_id2 * 0x200000 + slda_last;
			transfer_length = min(length_record, (0x200000 - slda_last));
            if(flag_append && ((transfer_lda + transfer_length) & (BIT(10)-1))){
                trim_data->Ranges[trim_data->Validcnt].Length = transfer_length;
                trim_data->Ranges[trim_data->Validcnt].sLDA = transfer_lda;
                trim_data->Validcnt ++;
                sys_assert(trim_data->Validcnt <= 510);
            }else if(!flag_append){
                trim_data->Ranges[i].Length = transfer_length;
                trim_data->Ranges[i].sLDA = transfer_lda;
                flag_append = true;
            }

// 			disp_apl_trace(LOG_DEBUG, 0x42cd, "2sec_id:%d sec_id2:%d\n",sec_id,sec_id2);
// 			disp_apl_trace(LOG_DEBUG, 0xfeec, "transfer_lda1:0x%x  transfer_length1:0x%x\n",transfer_lda,transfer_length);
			RegOrUnregTrimTable(transfer_lda,transfer_length, Register);
			slda_trim += transfer_length;
			length_record -= transfer_length;
		}
	}

    trim_data->Validtag = 0x12345678;
    cache_clear_trim_bit(trim_data);
    cache_check_trim_hit_par_data(trim_data);
#if 1 // check if trim lda no-aligned 4MB
    bool flag_noAligned_4M = false;
    for(u32 k=0; k < trim_data->Validcnt; ++k){
        u32 slda = trim_data->Ranges[k].sLDA;
        u32 length = trim_data->Ranges[k].Length;
        u32 elda = slda + length;
        if(slda & (BIT(10)-1)){
            flag_noAligned_4M = true;
            break;
        }
        if(elda & (BIT(10)-1)){
            flag_noAligned_4M = true;
            break;
        }
    }
    if(!flag_noAligned_4M)
        return false;
#endif

    TrimInfo.Dirty = 1;
	dtag_t dtag={.dtag = 0};
	dtag.b.dtag=mem2ddtag(trim_data);
	dtag.b.in_ddr=1;
	CommitTrimDtag(dtag.dtag,0);
	return true;
}
#if 0
slow_code bool Trim_handle1(Host_Trim_Data  *trim_data)//joe test 20200923
{
	u32 i = 0;
	u32 cnt = trim_data->Validcnt;
	//u64 transfer_lda=0;//joe add 20200914
	u32 nsid = trim_data->nsid;
	// u16 sec_id=0;
	// u16 sec_id2=0;
	// u64 slda_last;
	u64 slda_trim;
	u32 transfer_length=0;
	//u32 length_record=0;
#if 0//(TRIM_DEBUG == ENABLE)
	disp_apl_trace(LOG_ERR, 0xbeec, "Trim handle range cnt:%d\n",cnt);
#endif
	for(;i < cnt; i++)
	{
#if (PLP == ENABLE)
		if(PLP)
		{
			trim_data ->Validtag = 0x12345678;
			return false;
		}
#endif
		//joe add trim lda transfer 20200914
		transfer_length = 0x200000;	
		// length_record = 0x200000;
		slda_trim = ns_array_menu->ns_array[nsid].sec_id[i]*0x200000;
		//   printk("trim_data->Ranges[%d].sLDA1:0x%x \n",i,slda_trim);	
		//printk("  trim_data->Ranges[%d].Length1:0x%x\n",i, transfer_length);
		// while(length_record > 0)
		//{
		//sec_id = slda_trim / 0x200000;	
		//slda_last = slda_trim - sec_id * 0x200000;
		//sec_id2 = ns_array_menu->ns_array[nsid].sec_id[sec_id];
		//transfer_lda = sec_id2 * 0x200000 + slda_last;

		//printk("2sec_id:%d sec_id2:%d\n",sec_id,sec_id2);
		//printk("transfer_lda1:0x%x  transfer_length1:0x%x\n",transfer_lda,transfer_length);
		RegOrUnregTrimTable(slda_trim,transfer_length, Register);	
		// UpdtTrimInfo(slda_trim,transfer_length,Register); 
		//slda_trim = slda_trim + transfer_length;	
		//length_record = length_record - transfer_length;	 	
		//}
	}
	return true;
}
#endif
#define NS_SECTION_BITS (33)
#define NS_SECTION_LBA_BITS (NS_SECTION_BITS - host_sec_bitz)
#define NS_SECTION_LBA_CNT (1 << NS_SECTION_LBA_BITS)
slow_data void UpdtTrimInfo_with_NS(u64 slba, u64 count, u32 nsid)
{
    u32 section_id;
    u64 update_size;
    u64 update_lba;
    u64 lba_offset;
    while(count){
        section_id = slba >> NS_SECTION_LBA_BITS;
        lba_offset = slba & (NS_SECTION_LBA_CNT - 1);
        update_lba = (((u64)(ns_array_menu->ns_array[nsid].sec_id[section_id])) << NS_SECTION_LBA_BITS) + lba_offset;
        update_size = min(count,(NS_SECTION_LBA_CNT - lba_offset));
        slba += update_size;
        count -= update_size;
        UpdtTrimInfo(update_lba, update_size, Register);
    }
}

#else
slow_code bool Trim_handle(Host_Trim_Data * trim_data)
{
    u32 i = 0;
    u32 cnt = trim_data->Validcnt;
    #if 0//(TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_ERR, 0xe5c8, "Trim handle range cnt:%d\n",cnt);
    #endif
    for(;i < cnt; i++)
    {
        #if (PLP == ENABLE)
        if(PLP)
        {
            trim_data ->Validtag = 0x12345678;
            return false;
        }
        #endif
        RegOrUnregTrimTable(trim_data->Ranges[i].sLDA,trim_data->Ranges[i].Length, Register);
    }
    return true;
}
#endif
fast_code void TrimPowerLost(bool savetable)
{

    //TODO: 1. save trim R2P // 64K/32K
    //      2. save trim info in emp//4k

    if(TrimInfo.IsPowerLost == 1)
        return;
    if(!savetable){
        TrimInfo.TriminfoEndTag = 0xFFFFFFFF;
        // no save trim table drop it all
    }

    epm_trim_t* epm_trim_data = (epm_trim_t*)ddtag2mem(shr_epm_info->epm_trim.ddtag);
    memcpy((void *)(epm_trim_data->info), (void *)&TrimInfo, sizeof(Trim_Info));
	//epm_update(TRIM_sign,(CPU_ID-1));

    //epm_update(TRIM_sign,(CPU_ID-1)); // change to epm_update(EPM_POR, (CPU_ID - 1)) in rdisk_shutdown

    //disp_apl_trace(LOG_ERR, 0, "trim power lost save info done\n");
    TrimInfo.IsPowerLost = 1;
}


            //memset(req->op_fields.trim.ddrdsmr, 0, 4096);
#if (FULL_TRIM == ENABLE)

extern void btn_de_wr_disable(void);
extern void btn_de_wr_enable(void);
extern void ucache_flush(ftl_flush_data_t *fctx);
//extern void bcmd_resume(void);
//extern void req_resume(void);

slow_code void TrimFlushDone(ftl_core_ctx_t *ctx)
{
	ftl_flush_data_t *fctx = (ftl_flush_data_t *) ctx;
	fctx->nsid = 0;
	//bcmd_resume();
	//req_resume();
	//btn_de_wr_enable();
	bg_enable();//bg can't be enabled after req_resume

	sys_free(FAST_DATA, fctx);
    TrimInfo.TrimFlush = 0;
}
//extern ftl_flush_data_t _flush_ctx;
extern void btn_data_in_isr(void); 
extern void bm_handle_rd_err(void);
extern void bm_isr_com_free(void);

slow_code void TrimFlush()
{
	//btn_de_wr_disable();

	ftl_flush_data_t *fctx = sys_malloc(FAST_DATA, sizeof(ftl_flush_data_t));
    sys_assert(fctx);

	fctx->ctx.caller = NULL;
	fctx->nsid = 1;
	fctx->flags.all = 0;
	fctx->ctx.cmpl = TrimFlushDone;
    TrimInfo.TrimFlush = 1;
    #if(BG_TRIM == ENABLE)
    SetBgTrimSuspend(true);
    #endif
	ucache_flush(fctx);
    #if (TRIM_DEBUG == ENABLE)
    disp_apl_trace(LOG_ERR, 0x6287, "trim Flush set\n");
    #endif

#ifdef While_break
	u64 start = get_tsc_64();
#endif	

    while(TrimInfo.TrimFlush == 1)//waite Flush done
    {
        btn_data_in_isr();//fill up read done handle 
        extern void l2p_isr_q0_srch(void);
        l2p_isr_q0_srch();//fill up l2p srch nomapping handle
        dpe_isr();//merger done handle
        cpu_msg_isr();//fill up get dtag done / flush done case handle
        evt_task_process_one();//ucache_flush_update return false need do evt case
		bm_handle_rd_err();
		bm_isr_com_free();
#ifdef While_break		
		if(Chk_break(start,__FUNCTION__, __LINE__))
			break;
#endif			
    }
}

ddr_code void ipc_ftl_full_trim_handle_done(volatile cpu_msg_req_t *req)
{
    req_t *cmd_req = (req_t *)req->pl;

    ReinitTrimTable(false);
    if(cmd_req && (cmd_req != (req_t *)INV_U32))
    {
        dtag_t dtag;
            if(cmd_req->completion)
        	{
        		cmd_req->completion(cmd_req);
        	}
    	dtag = mem2dtag(cmd_req->op_fields.trim.dsmr);
    	dtag_put(DTAG_T_SRAM, dtag);
            //nvmet_io_fetch_ctrl(false);
    }
}

#if(BG_TRIM == ENABLE)
ddr_code void chk_bg_trim()
{
    extern u8 evt_trim_range_issue;
    extern Trim_Info TrimInfo;
    extern bg_trim_mgr_t bg_trim;

#if (PLP_SLC_BUFFER_ENABLE  == mENABLE) 
    extern volatile u8 shr_slc_flush_state;
    extern u32	 shr_plp_slc_need_gc;
    if (shr_plp_slc_need_gc || shr_slc_flush_state == SLC_FLOW_GC_START){ // SLC GC not done
        disp_apl_trace(LOG_ERR, 0xa6aa, "shr_plp_slc_need_gc %u, shr_slc_flush_state %u",
            shr_plp_slc_need_gc, shr_slc_flush_state);
        return;
    }
#endif

#if (PLP_SUPPORT == 1)
    extern volatile bool plp_test_flag;
    if(plp_test_flag){ // PLP cap check not done
        disp_apl_trace(LOG_ERR, 0xc816, "plp chk not done, plp_test_flag %u", plp_test_flag);
        return;
    }
#endif

#if (SPOR_FTLINITDONE_SAVE_QBT == mENABLE) 
    extern volatile bool delay_flush_spor_qbt;  
    if(delay_flush_spor_qbt){ // Save QBT check not done
        disp_apl_trace(LOG_ERR, 0xfc96, "save BQT not done, delay_flush_spor_qbt %u", delay_flush_spor_qbt);
        return;
    }
#endif

    if(TrimInfo.IsValid){
        bg_trim.flags.b.clean = 0;
        bg_trim.timer = get_tsc_64();
        disp_apl_trace(LOG_ERR, 0x8027, "bg_trim trigger");
        evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
    }
}
#endif

#if (((PLP_SLC_BUFFER_ENABLE == mENABLE) || (SPOR_FTLINITDONE_SAVE_QBT == mENABLE)) && (BG_TRIM == ENABLE))
ddr_code void ipc_chk_bg_trim(volatile cpu_msg_req_t *req)
{
    chk_bg_trim();
}
#endif

ddr_code void FullTrimHandle(req_t *req, u8 need_flush)
{
        ///nvmet_io_fetch_ctrl(true);

    //todo      2.clear L2p table
        //disp_apl_trace(LOG_ERR, 0, "total_Part_bits:0x%x,Part_bits_sended:0x%x,req:0x%x",TrimInfo.total_Part_bits,TrimInfo.Part_bits_sended,req);
    
    epm_format_state_update(0xFFFFFFFF, FTL_FULL_TRIM_TAG);//update epm ftl parameter. 0:format finish; 0xFFFFFFFF:during processing; less than spb_cnt:next erased spb;
    if(req && need_flush){
    //       1.Flush cache
        TrimFlush();
    }
    Full_TRIM_IPC_SEND = true;
    cpu_msg_issue(CPU_FTL - 1, CPU_MSG_FTL_FULL_TRIM, 0, (u32) req);

    #if(BG_TRIM == ENABLE)
    if(!bg_trim.flags.b.clean){
        bg_trim.flags.b.abort = 1;
    }
    #endif
    #ifdef ERRHANDLE_ECCT 
        if(ecct_cnt)
        {
            rdisk_ECCT_op(0, 0, VSC_ECC_reset);
    }
    #endif

    //        3.Clear Fake Trim Table
    //disp_apl_trace(LOG_ERR, 0, "Full Trim handle done\n");
}
slow_code u32 IsFullTrimTriggered()
{
    return TrimInfo.FullTrimTriggered;
}
#endif
#if(BG_TRIM == ENABLE)
fast_data_zi u32 BG_TRIM_TRIGGER_PBT_CNT;
fast_data_zi bool BG_TRIM_PBT_TRIGGERED;
share_data_zi bool BG_TRIM_HANDERING;
fast_data_zi u32 BG_trim_timer;
fast_code void BgTrimForcePBT()
{
    cpu_msg_issue(1, CPU_MSG_FORCE_PBT, 0, DUMP_PBT_BGTRIM);
    BG_TRIM_PBT_TRIGGERED = 1;
    BG_TRIM_TRIGGER_PBT_CNT = 0;
    BG_trim_timer = jiffies;
}
fast_code void BgTrimForcePBTACK(volatile cpu_msg_req_t *req)
{
    disp_apl_trace(LOG_ERR, 0x7069, "clean BG_TRIM_TRIGGER_PBT_CNT:0x%x",BG_TRIM_TRIGGER_PBT_CNT);
    BG_TRIM_TRIGGER_PBT_CNT = 0;
    if(BG_TRIM_PBT_TRIGGERED){
        BG_TRIM_PBT_TRIGGERED = 0;
    }
}
fast_code bool JiffiesCal()
{
    u64 time = get_tsc_64();
    u64 th = 0;
    if(time > bg_trim.timer){
        time = time - bg_trim.timer;
    }else{
        time = INV_U64 - bg_trim.timer + time;
    }
    //if(bg_trim.totalcnt){
        th = CYCLE_PER_MS;
    //}else{
    //    th = 0;
    //}
    if(time>=th)
    {
        return true;
    }else{
        return false;
    }
}

fast_code void BgTrimevt(u32 nouse0, u32 nouse1, u32 nouse2) 
{
    while(!CBF_EMPTY(&bg_trim_free_dtag)){
        dtag_t dtag = bg_trim_free_dtag.idx[bg_trim_free_dtag.rptr];
        put_trim_dtag(dtag);
        bg_trim_free_dtag.rptr = (bg_trim_free_dtag.rptr + 1)&(DDR_TRIM_RANGE_DTAG_CNT);
    }

    // if(get_trim_dtag_cnt() < DDR_TRIM_RANGE_DTAG_CNT){
    //     evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
    // }
    if(bg_trim.flags.b.clean||plp_trigger)
        return;
    if(bg_trim.flags.b.abort){
        BgTrimInit(false);
    }
    if(bg_trim.flags.b.suspend){
        evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
        return;
    }
    #ifdef BG_TRIM_ON_TIME
   // ucache_trim_check();
    #endif
	if(bg_trim.RecordRangeS==INV_U32){
		bg_trim.RecordRangeS = 0;
	}
    if(JiffiesCal()){
		u32 size = ((bg_trim.RecordRangeS + 1024) > bg_trim.size) ? (bg_trim.size):(bg_trim.RecordRangeS + 1024);
        u32 Range = find_next_bit((void*)TrimTable, size, bg_trim.RecordRangeS);
        if(Range < size){
            if(((shr_dtag_comt.que.wptr + 1 ) % shr_dtag_comt.que.size) != shr_dtag_comt.que.rptr){
    // 		 	CommitTrimDtag(Range, 1);
                if(!BgTrimIssue(Range)){
                    evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
                    return;
                }
    			bg_trim.RecordRangeS = Range + 1;
     			// clear_bit(Range,(void*)TrimTable); // cler bit after trim done
            }
        }else{
			bg_trim.RecordRangeS = Range;
			if(Range==bg_trim.size){
				if(bg_trim.flags.b.needcheck==0){
                    if (get_trim_dtag_cnt() == DDR_TRIM_RANGE_DTAG_CNT){ // no otf trim data
                        disp_apl_trace(LOG_ERR, 0x0c0e, "Trim table clean");
                        TrimInfo.IsValid = 0;
                        TrimInfo.Dirty = 0;
                        bg_trim.flags.b.clean = 1;
                        BgTrimInit(false);
                    } else{
                        bg_trim.flags.b.waitOtfDone = 1;
                    }
                }else{
					bg_trim.RecordRangeS = 0;
					bg_trim.flags.b.needcheck = 0;
					bg_trim.flags.b.checks = 1;
                }
            }
        }
        //send in dtag cmmt
    }
    evt_set_cs(evt_trim_range_issue, 0, 0, CS_TASK);
}
fast_code void put_trim_dtag(dtag_t dtag)
{
    // clear trim bit.
    Host_Trim_Data * addr = (Host_Trim_Data *)ddtag2mem(dtag.b.dtag);
    if(addr->all && addr->Validtag == 0x12345678 && addr->valid_bmp){
        for(u32 i = 0;i < addr->Validcnt; i++){
            if (test_bit(i, &addr->valid_bmp)){
                u32 Range = addr->Ranges[i].sLDA >> LDA2TRIMBITSHIFT;
                clear_bit(Range,(void*)TrimTable);
            }
        }
    }

    u16 idx = dtag.b.dtag - bg_trim.startdtag;
    sys_assert(idx < DDR_TRIM_RANGE_DTAG_CNT);

    if (test_and_set_bit(idx, &bg_trim.dtagbmp_free)) {
        disp_apl_trace(LOG_ERR, 0xa10e, "panic idx %u", idx);
        panic("o");
    }else{
        bg_trim.dtagfreecnt++;
    }
    clear_bit(idx, &bg_trim.dtagbmp_Align4M);
    clear_bit(idx, &bg_trim.dtagbmp_NoAlign4M);
    addr->Validtag = 0;
    addr->valid_bmp = 0;
    if(get_trim_dtag_cnt() == DDR_TRIM_RANGE_DTAG_CNT){
        if(bg_trim.flags.b.needcheck==0 && bg_trim.flags.b.waitOtfDone){
            u32 Range = find_next_bit((void*)TrimTable, bg_trim.size, 0);
            if(Range > bg_trim.size){
                disp_apl_trace(LOG_ERR, 0x98b5, "Trim table clean, init bg trim");
                TrimInfo.IsValid = 0;
                TrimInfo.Dirty = 0;
                bg_trim.flags.b.clean = 1;
                BgTrimInit(false);
            }
        }
        TrimInfo.Dirty = TrimInfo.IsValid;
    }
}
inline u32 get_trim_dtag_cnt()
{
	return bg_trim.dtagfreecnt;
}
fast_code dtag_t get_trim_dtag(bool align4M)
{
    dtag_t dtag = {.dtag = 0};
    u32 idx = find_first_bit(&bg_trim.dtagbmp_free, DDR_TRIM_RANGE_DTAG_CNT);
    if(idx >= DDR_TRIM_RANGE_DTAG_CNT){
        dtag.dtag = INV_U32;
        return dtag;
    }
    clear_bit(idx, &bg_trim.dtagbmp_free);
    if(align4M){
        set_bit(idx, &bg_trim.dtagbmp_Align4M);
    }else{
        set_bit(idx, &bg_trim.dtagbmp_NoAlign4M);
    }
    bg_trim.dtagfreecnt--;
    dtag.b.dtag = idx + bg_trim.startdtag;
    dtag.b.in_ddr = 1;
    return dtag;
}
#if 1
fast_code bool PrepareTrimData(dtag_t dtag,u32 Range)
{
    Host_Trim_Data * addr = (Host_Trim_Data *)ddtag2mem(dtag.b.dtag);
    u32 slda = Range << LDA2TRIMBITSHIFT;
    addr->Validtag = 0x12345678;
    addr->Validcnt = 1;
    addr->all = 1;
    addr->Ranges[0].Length = (1 << LDA2TRIMBITSHIFT);
    addr->Ranges[0].sLDA = slda;
    addr->valid_bmp = BIT0;
    bg_trim.timer = get_tsc_64();
    return true;
}
fast_data u32 last_range = 0;
fast_code bool BgTrimIssue(u32 Range)
{
    bool flag_debug = false;
    bool flag_first = true;
debug:
    // check if this trim bit is otf, don't issue again
    if (bg_trim.dtagbmp_Align4M){
        u32 idx = find_first_bit(&bg_trim.dtagbmp_Align4M, DDR_TRIM_RANGE_DTAG_CNT);
        while(idx < DDR_TRIM_RANGE_DTAG_CNT){
            if(trim_data_check_clear_bit(idx, Range, Range, false))
                return true;
            idx = find_next_bit(&bg_trim.dtagbmp_Align4M, DDR_TRIM_RANGE_DTAG_CNT, idx+1);
        }
    }


    flag_debug = (last_range == Range);
    if (flag_debug && flag_first){
        flag_first = false;
        goto debug;
    }

    dtag_t dtag = get_trim_dtag(true); 
    if(dtag.dtag == INV_U32){ 
        return false; 
    } 
	u32 wptr = shr_dtag_comt.que.wptr;
	u32 size = shr_dtag_comt.que.size;
    u32 rptr = shr_dtag_comt.que.rptr;
    u32 wptr1 = wptr;
    wptr1++;
    if (wptr1 == size) {
        wptr1 = 0;
    }
    if(BG_TRIM_HANDERING == 0){
        BG_TRIM_HANDERING = 1;
        BG_trim_timer = jiffies;
    }
    if (wptr1 == rptr){
        put_trim_dtag(dtag); 
        return false;
    }
    last_range = Range;
    // disp_apl_trace(LOG_ERR, 0, "range 0x%x lda 0x%x, cnt 0x400, idx %u",Range, Range<<LDA2TRIMBITSHIFT,dtag.dtag - bg_trim.startdtag);
    if(!PrepareTrimData(dtag, Range)){
        put_trim_dtag(dtag);
        return false;
    }
	shr_dtag_comt.que.buf[wptr].dtag = dtag;
	shr_dtag_comt.lda[wptr] = TRIM_LDA;
	if (++wptr == size)
		wptr = 0;
    dmb();
	shr_dtag_ins_cnt += 1;
	shr_dtag_comt.que.wptr = wptr;
    dmb();
    // Host_Trim_Data * addr = (Host_Trim_Data *)ddtag2mem(dtag.b.dtag);
    // u32 slda = addr->Ranges[0].sLDA;
    // u32 length = addr->Ranges[addr->Validcnt - 1].Length + addr->Ranges[addr->Validcnt - 1].sLDA - slda;
    // RegOrUnregTrimTable(slda,length, Unregister);
    return true;
}
#endif
extern bool ucache_force_flush;

fast_code bool BgTrimUcacheCheck(u32 lda)
{
    if(bg_trim.flags.b.suspend && !ucache_force_flush){
        SetBgTrimSuspend(false);
    }
    #ifdef BG_TRIM_ON_TIME
    u32 range = (lda >> LDA2TRIMBITSHIFT);
    if (bitmap_check((u32 *)TrimTable, range)) {
        return BgTrimIssue(range);
	}
    #endif
    return true;
}
slow_code void BgTrimInit(bool poweron)
{
    if(poweron){

        //bg_trim.wbmp = sys_malloc(SLOW_DATA, occupied_by(bg_trim.size, 8));
        //sys_assert(bg_trim.wbmp);
        bg_trim.startdtag = DDR_TRIM_RANGE_START;
        bg_trim.dtagfreecnt = DDR_TRIM_RANGE_DTAG_CNT;
        memset(&bg_trim.dtagbmp_free,0xFF,sizeof(bg_trim.dtagbmp_free));
        memset(&bg_trim.dtagbmp_Align4M,0,sizeof(bg_trim.dtagbmp_Align4M));
        memset(&bg_trim.dtagbmp_NoAlign4M,0,sizeof(bg_trim.dtagbmp_NoAlign4M));
        bg_trim.flags.b.clean = 1;
        bg_trim.flags.b.waitOtfDone = 0;
        cpu_msg_register(CPU_MSG_FORCE_PBT_CPU1_ACK, BgTrimForcePBTACK);
    }
    //memset(bg_trim.wbmp,0,occupied_by(bg_trim.size, 8));
    bg_trim.size = LBA2LDA(ns[0].cap)>>LDA2TRIMBITSHIFT;
    // disp_apl_trace(LOG_ERR, 0, "ns[0].cap %x %x, bg_trim.size %u NR_LBA_PER_LDA_SHIFT %u LDA2TRIMBITSHIFT %u",
    //     (u32)ns[0].cap, ns[0].cap>>32, bg_trim.size, NR_LBA_PER_LDA_SHIFT, LDA2TRIMBITSHIFT);
    bg_trim.RecordRangeS = INV_U32;
    bg_trim.RecordRangeE = INV_U32;
    bg_trim.totalcnt = 0;
    bg_trim.flags.b.suspend = 0;
    bg_trim.flags.b.abort = 0;
    bg_trim.flags.b.checks = 0;
    bg_trim.flags.b.newin = 0;
    bg_trim.flags.b.needcheck = 0;
    TrimUnalignLDA.LBA = INV_U64;
    BG_TRIM_TRIGGER_PBT_CNT = 0;
    BG_TRIM_PBT_TRIGGERED = 0;
    BG_TRIM_HANDERING = 0;
}
fast_code void SetBgTrimSuspend(bool suspend)
{
    epm_trim_t* epm_trim_data = (epm_trim_t*)ddtag2mem(shr_epm_info->epm_trim.ddtag);
    Trim_Info * info = (Trim_Info *)(epm_trim_data->info);
    if(bg_trim.flags.b.suspend && !suspend && info->TriminfoEndTag != 0xFFFFFFFF){
        //to avoid no PLP cause get the old trim table case 
        info->TriminfoEndTag = 0xFFFFFFFF;
    	epm_update(TRIM_sign,(CPU_ID-1));
        disp_apl_trace(LOG_INFO, 0xd1b7, "set trim table invalid");
    }
    if(!bg_trim.flags.b.clean){
        bg_trim.flags.b.suspend = suspend;
    }
}
slow_code void SetBgTrimAbort()
{
    if(!bg_trim.flags.b.clean){
        bg_trim.flags.b.abort = 1;
    }
}
#endif
#endif
