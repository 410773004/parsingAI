//=============================================================================

#pragma once
#include "types.h"
#include "../nvme/inc/req.h"
#include "ddr.h"
#if (TRIM_SUPPORT == ENABLE)
#define TRIM_DEBUG          DISABLE
#define LBA_TRIM            DISABLE//Not supported
#if defined(USE_8K_DU)
#define LDA_SIZE_SHIFT		(13)
#else
#define LDA_SIZE_SHIFT		(12)		///< bit shift that represent lda size
#endif
#define LDA_SIZE		(1 << LDA_SIZE_SHIFT) 			///< lda size
#define SLC_BLK_SIZE    (6*1024*1024) //6M

#define LDA2TRIMBITSHIFT 10 ///<means 4M 1bit
#define TrimTblShift 3
#define TrimTblIdxBitMask 7
#define BIT(x)  (1UL<<(x))
#define TrimTblBitPerIdx  8 
#define TRIM_R2P_BLK_SHIFT 16
#define TRIM_R2P_PAGE_MASK 0x0000FFFF

#define LDANumPerRange    (8*1024*1024*16)
#define LDA2RangeMapMask  (LDANumPerRange - 1)
#define LDA2RangeMapShift  17
#define LDA2Range_UP(x,y)   ((x + (1<<y) -1) >> y)
#define LDA2Range_Down(x,y) ((x - (1<<y) + 1) >> y)

#define TRIM_CH_BASE 4 // SRB used CH4-7 CE2-7 Lun0-1
#define TRIM_CE_BASE 2 //
#define TRIM_LUN_BASE 0 //
#if MAX_TARGET >=2
#define TOTAL_TRIM_BLK_CNT ((shr_nand_info.geo.nr_channels-TRIM_CH_BASE)*(shr_nand_info.geo.nr_targets-TRIM_CE_BASE)*(shr_nand_info.geo.nr_luns-TRIM_LUN_BASE)*(shr_nand_info.geo.nr_planes))   ///<128 for 4 CE,64 for 2 CE
#else
#define TOTAL_TRIM_BLK_CNT 1
#endif

#define InvalidU64    0xFFFFFFFFFFFFFFFF
#define InvalidU32    0xFFFFFFFF

#define TrimTableMetaTag 0xA5A5A5
//#define TrimR2PMetaTag   0x5A5A5A
#define MAX_BYTES_ONE_TIME (0x1FFFFE0)


#define TOTAL_BG_TRIM_RANGE2LDA_SHIFT (10)
#define TOTAL_BG_TRIM_RANGE2LBA_SHIFT (TOTAL_BG_TRIM_RANGE2LDA_SHIFT + 3)
#define TOTAL_BG_TRIM_RANGE_CNT  (_max_capacity >> TOTAL_BG_TRIM_RANGE2LDA_SHIFT)
#define TOTAL_BG_TRIM_PART_SHIFT (6)
#define TOTAL_BG_TRIM_PART_CNT (BIT(TOTAL_BG_TRIM_PART_SHIFT))
#define BG_TRIM_RANGE_TRIGGER_CNT ((ns[0].cap >> TOTAL_BG_TRIM_RANGE2LBA_SHIFT) >> TOTAL_BG_TRIM_PART_SHIFT)

#define BG_TRIM_TRIGGER_PBT_MAX_CNT (0x6000000)
typedef enum RegOp{
    Unregister = 0,
    Register,
}RegOp;
#if 0
typedef union B256{
    struct{
     u64 DW[4];
    }DWs;
    struct{
     u32 word[8];
    }WDs;
    struct{
     u8 Byte[258];
    }Bs;
}B256;
typedef struct RangeTobitMap{
    B256 Range[8];
}RangeTobitMap;
#endif
typedef union trimblk
{
    u32 all;
    struct{
        u32 vac:31;
        u32 defect:1;
    }b;
}trimblk;
#if 0
typedef struct R2P
{
    u16 blk;
    u16 page;
}R2P;
#endif
typedef struct Trim_Info{
    u64 RecordMinLBA;
    u64 RecordMaxLBA;
    u64 deallocated_cnt;
    u32  IsValid;
    u32  PartitionTrimTriggered;
    u32  FullTrimTriggered;
    u32 volatile  TrimFlush;
    u32  IsPowerLost;
    u32  TriminfoEndTag;
    u32  Dirty;
}Trim_Info; //Now total 584 B,remember to add the value if struct change,shall no over 1KB.

typedef struct bg_trim_mgr_t
{
    u32 RecordRangeS;
    u32 RecordRangeE;
    u32 size;
    u32 totalcnt;
    u32 dtagbmp_free[occupied_by(DDR_TRIM_RANGE_DTAG_CNT, 32)];
    u32 dtagbmp_Align4M[occupied_by(DDR_TRIM_RANGE_DTAG_CNT, 32)];
    u32 dtagbmp_NoAlign4M[occupied_by(DDR_TRIM_RANGE_DTAG_CNT, 32)];
    u32 startdtag;
    u32 dtagfreecnt;
    u64 timer;
    u32 tigger_pbt_cnt;
    union{
        u32 all;
        struct{
            u32 clean   :1;
            u32 suspend :1;
            u32 abort   :1;
            u32 newin   :1;
            u32 checks  :1;
            u32 needcheck:1;
            u32 waitOtfDone:1;
            u32 reserved:25;
        }b;
    }flags;
}bg_trim_mgr_t;
typedef  struct{
    volatile u16 rptr;
    volatile u16 wptr;
    dtag_t idx[DDR_TRIM_RANGE_DTAG_CNT+1];
}trim_dtag_free_t;
typedef  struct{
    u64 LBA;
}unalign_LDA;

u8 SrchTrimTable(lda_t lda );
bool IsCMDinTrimTable(lda_t slda, lda_t elda,bool Iswrite);
void RegOrUnregTrimTable(lda_t slda, u32 count,RegOp op);
void PowerOnInitTrimTable(void);
void ReinitTrimTable(bool one_time_init);
void UpdtTrimInfo(u64 slda, u64 count ,RegOp op);
//bool IsCMDinTrimTable(lda_t slda, lda_t elda);
bool Trim_handle(Host_Trim_Data * trim_data);

#if NS_MANAGE
void UpdtTrimInfo_with_NS(u64 lba, u64 count, u32 nsid);
#endif
void TrimPowerLost(bool savetable);

#if (FULL_TRIM == ENABLE)
void FullTrimHandle(req_t *req, u8 need_flush);
u32 IsFullTrimTriggered(void);
void ipc_ftl_full_trim_handle_done(volatile cpu_msg_req_t *req);
void ipc_chk_bg_trim(volatile cpu_msg_req_t *req);
void chk_bg_trim();

#endif
#if (BG_TRIM == ENABLE)
void BgTrimInit(bool poweron);
u32 get_trim_dtag_cnt();

void put_trim_dtag(dtag_t dtag);
dtag_t get_trim_dtag(bool align4M);
void BgTrimevt(u32 nouse0, u32 nouse1, u32 nouse2);
void SetBgTrimAbort();
void SetBgTrimSuspend(bool suspend);
//bool BgTrimIssue(u32 Range,bool check);
bool BgTrimUcacheCheck(u32 lda);
#endif
#endif

