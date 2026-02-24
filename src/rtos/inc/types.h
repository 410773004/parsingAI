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
/*!
 * \file types.h
 *
 */

#define mFALSE          0
#define mTRUE           1

#define mENABLE         1
#define mDISABLE        0

typedef signed char     mINT_8;         // signed types 1 byte
typedef signed char     INT8;

typedef unsigned char   mUINT_8;        // unsigned types 1 byte
typedef unsigned char   UINT8;

typedef short           mINT_16;        // 2 bytes
typedef short           INT16;

typedef unsigned short  mUINT_16;       // 2 bytes
typedef unsigned short  UINT16;

typedef int             mINT_32;        // 4 bytes
typedef int             INT32;

typedef unsigned int    mUINT_32;       // 4 bytes
typedef unsigned int    UINT32;

typedef long long       mINT_64;        // 8 bytes
typedef long long       INT64;

typedef unsigned long long  mUINT_64;   // 8 bytes
typedef unsigned long long  UINT64;


#define EN_NVME_14

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

typedef char  s8;
typedef short s16;
typedef int   s32;
typedef long long s64;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;

typedef u16 __le16;
typedef u32 __le32;

typedef void* PVOID;  


typedef union {
    u32 all;
    struct {
        u16 l;
        u16 h;
    }w;
}dw_t;

typedef union{
    u64 all;
    struct{
        u32 l;
        u32 h;
    }dw;
}uint64;


typedef enum { false, true } bool;

#define NULL ((void *)0)

#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

/* compiler related helper */
#define offsetof(TYPE, MEMBER) ((int) &((TYPE *)0)->MEMBER)
#define member_size(TYPE, MEMBER) sizeof(((TYPE *)0)->MEMBER)

#ifdef __GNUC__
#define member_type(type, member) __typeof__ (((type *)0)->member)
#else
#define member_type(type, member) const void
#endif

#define container_of(ptr, type, member) ((type *)( \
    (char *)(member_type(type, member) *){ ptr } - offsetof(type, member)))

#ifndef glue
 #define xglue(x, y)     x ## y
 #define xglue3(x, y, z)     x ## y ## z
 #define glue(x, y)      xglue(x, y)
 #define stringify(s)    tostring(s)
 #define tostring(s)     #s
 #define glue2(x, y, z, cnt)  x ## y ##_ ## z ##_ ## cnt
 #define glue3(x, y, z)  xglue3(x, y, z)
#endif

#define BUILD_BUG_ON(x) \
    typedef char glue(build_bug_on__, __LINE__)[(x) ?  -1:1] __attribute__((unused))

#define PACKED __attribute__((packed))
#define ALIGNED(x) __attribute__ ((aligned(x)))
#define UNUSED __attribute__((unused))
#define NOINLINE __attribute__((noinline))
#define ALWAYS_INLINE __attribute__((always_inline)) inline

static void inline _size_check(void)
{
    BUILD_BUG_ON(sizeof(u8)  != 1);
    BUILD_BUG_ON(sizeof(u16) != 2);
    BUILD_BUG_ON(sizeof(u32) != 4);
    BUILD_BUG_ON(sizeof(u64) != 8);

    BUILD_BUG_ON(sizeof(s8)  != 1);
    BUILD_BUG_ON(sizeof(s16) != 2);
    BUILD_BUG_ON(sizeof(s32) != 4);
    BUILD_BUG_ON(sizeof(s64) != 8);
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Bit fields */
#define BIT0    0x00000001
#define BIT1    0x00000002
#define BIT2    0x00000004
#define BIT3    0x00000008
#define BIT4    0x00000010
#define BIT5    0x00000020
#define BIT6    0x00000040
#define BIT7    0x00000080
#define BIT8    0x00000100
#define BIT9    0x00000200
#define BIT10   0x00000400
#define BIT11   0x00000800
#define BIT12   0x00001000
#define BIT13   0x00002000
#define BIT14   0x00004000
#define BIT15   0x00008000
#define BIT16   0x00010000
#define BIT17   0x00020000
#define BIT18   0x00040000
#define BIT19   0x00080000
#define BIT20   0x00100000
#define BIT21   0x00200000
#define BIT22   0x00400000
#define BIT23   0x00800000
#define BIT24   0x01000000
#define BIT25   0x02000000
#define BIT26   0x04000000
#define BIT27   0x08000000
#define BIT28   0x10000000
#define BIT29   0x20000000
#define BIT30   0x40000000
#define BIT31   0x80000000

#define BIT(x)  (1UL<<(x))
#define BITS_PER_ULONG  (sizeof(unsigned long) << 3)

#define INT_CHR(_a, _b, _c, _d) \
        (((_a) << 24) | ((_b) << 16) | ((_c) << 8) | (_d))

#define _MAKE_CPU_ID(x) CPU ## x
#define STR_CPU_ID(x) _MAKE_CPU_ID(x)

//#define CPU_ID_SUFFIX(x) glue(x, STR_CPU_ID(CPU_ID))
#define CPU_ID_PREFIX(x)     glue(STR_CPU_ID(CPU_ID), x)
#define     ID_SUFFIX(x)     glue(x, CPU_ID)
#define     ID_SUFFIX_0(x)   glue(x, CPU_ID_0) // 0 based id
#define     ID_INFIX(x, z)   glue3(x, CPU_ID, z)
#define     ID_INFIX_0(x, z) glue3(x, CPU_ID_0, z)

#if defined(USE_512B_HOST_SECTOR_SIZE)
#define HLBASZ     (9)
#define LBA_SIZE_SHIFT      HLBASZ          		///< bit shift that represent lba size
#define LBA_SIZE            (1 << LBA_SIZE_SHIFT)       ///< lba size
#else
#define HLBASZ     (12)
#define LBA_SIZE_SHIFT      HLBASZ         		///< bit shift that represent lba size
#define LBA_SIZE            (1 << LBA_SIZE_SHIFT) 	///< lba size
#endif

#define NR_LBA_PER_LDA		(LDA_SIZE / LBA_SIZE)   ///< lda number in a lba
#define NR_LBA_PER_LDA_SHIFT	(LDA_SIZE_SHIFT - LBA_SIZE_SHIFT)       ///< bit shift that represent lda number in a lba
#define NR_LBA_PER_LDA_MASK	(NR_LBA_PER_LDA - 1)		///< lba mask in lda
#define LBA_OFST_LDA(lba)	((lba) & NR_LBA_PER_LDA_MASK)	///< get lba offset
#define LBA2LDA(lba) 		((lba) >> NR_LBA_PER_LDA_SHIFT)
#define LBA2ULBA(lba) 		((LBA2LDA(lba)) << NR_LBA_PER_LDA_SHIFT)

//joe add 20200817
#define HLBASZ1     (9)
#define LBA_SIZE_SHIFT1      HLBASZ1         		///< bit shift that represent lba size
#define LBA_SIZE1            (1 << LBA_SIZE_SHIFT1) 	///< lba size

#define HLBASZ2     (12)
#define LBA_SIZE_SHIFT2      HLBASZ2        		///< bit shift that represent lba size
#define LBA_SIZE2            (1 << LBA_SIZE_SHIFT2) 	///< lba size
//joe 20200817
typedef u32 pda_t;
typedef u32 lda_t;




#define INV_LDA             0xFFFFFFFF                      // value that represent this lda is invalid
#define P2L_LDA             (INV_LDA - 1)   //0xFFFFFFFE
#define PARITY_LDA          P2L_LDA
#define BLIST_LDA           (INV_LDA - 2)   //0xFFFFFFFD
#define TRIM_LDA		    (INV_LDA - 3)   //0xFFFFFFFC    // value that represent this lda is trimed
#define ERR_LDA		        (INV_LDA - 4)   //0xFFFFFFFB    // if read error, update this LDA in LDA field
#define DONE_LDA		    (INV_LDA - 5)   //0xFFFFFFFA
#define DUMMY_LDA		    (INV_LDA - 6)   //0xFFFFFFF9    // for dummy data usage
#define PBT_LDA		        (INV_LDA - 7)   //0xFFFFFFF8
#define QBT_LDA		        (INV_LDA - 8)   //0xFFFFFFF7
#define SPOR_DUMMY_LDA	    (INV_LDA - 9)   //0xFFFFFFF6    // for SPOR dummy data usage
#define P2L_INV_LDA         (INV_LDA - 10)  //0xFFFFFFF5
#define FTL_BLIST_TAG       (INV_LDA - 11)  //0xFFFFFFF4
#define FTL_PLP_TAG         (INV_LDA - 12)  //0xFFFFFFF3    // 'PLP'

#define FTL_EPM_SPOR_TAG    (INV_LDA - 13)  //0xFFFFFFF2    // 'SPOR'
#define FTL_EPM_POR_TAG     (INV_LDA - 14)  //0xFFFFFFF1    // 'POR'

#define DUMMY_PLP_LDA		(INV_LDA - 15)  //0xFFFFFFF0    // 'PLP force DUMMY LDA'  for scan last page

#define FTL_PLP_FORCE_TAG    0xFFFFFA00      //0xFFFFFA00    // 'PLP force flush p2l'
#define FTL_PLP_GROUP_MASK   0x000000FF		 //PLP force group id for gc scan

#define FTL_PLP_SLC_NEED_GC_TAG   0xFFFFF900		 //slc block 1 need gc 
#define SLC_ERASE_FORMAT_TAG      0xFFFFFA00		 //slc need erase 

#define FTL_NON_PLP_NEED_GC_TAG   0xFFFFFB00		 //NON-PLP blk need gc
#define FTL_PREFORMAT_TAG         0xFFF01234
#define FTL_FULL_TRIM_TAG         0xFFF05678


#define SLC_FLOW_DONE      	     	(0)
#define SLC_FLOW_FQBT_FLUSH      	(1)
#define SLC_FLOW_GC_START	 	 	(2)
#define SLC_FLOW_GC_DONE		 	(3)
#define SLC_FLOW_GC_OPEN_PAD_START 	(4)
#define SLC_FLOW_GC_OPEN_PAD_DONE 	(5)
#define SLC_FLOW_START_ERASE    	(6)

#define NON_PLP_FORMAT			 (1)
#define NON_PLP_PREFORMAT		 (2)


#define FTL_QBT_TABLE_TAG    0xFFFFFE00
#define FTL_FQBT_TABLE_TAG   0xFFFFFD00
#define FTL_PBT_TABLE_TAG    0xFFFFFC00
#define FTL_TABLE_TAG_MASK   0x000000FF


#define INV_PDA         0xFFFFFFFF      ///< value that represent this pda is invalid
#define UNMAP_PDA		(INV_PDA - 1)   ///< value that represent this lda is unmap
#define ERR_PDA			(INV_PDA - 2)	///< value that represent this lda is error
#define ZERO_PDA		(INV_PDA - 3)	///< value that represent this lda is zero

#define INV_U64     0xFFFFFFFFFFFFFFFF
#define INV_U32                 0xFFFFFFFF
#define INV_U16                 0xFFFF
#define INV_U8                  0xFF

#define is_normal_pda(pda)      (pda < UNMAP_PDA)       ///< determine this pda is normal or not
#define FAKE_PDA    0x12345678          ///< fake pda value

#define GET_B31_00(X)  ((u32)(X))
#define GET_B63_32(X)  ((u32)((X)>>32))

typedef u32 row_t;

/*! \brief RDA is used publicly, it may for RDA to PDA */
typedef struct _rda_t {
	row_t row;
	u8 ch;			///< Channel ID
	u8 dev;			///< CE ID in on channel
	u8 du_off;		///< DU offset in one page
	u8 pb_type;		///< Record PB type, 0 is SLC or (1 - 4) is XLC for Low/Mid/Upper/Top type
} rda_t;

typedef struct _err_t{	//20202028-Eddie
	u8 stage;
	u8 error_code;
}err_t;

typedef union {
	struct {
		u8 sqid;
		u8 cntlid;
		u16 cid;
		u16 nvme_status;
		u32 cmd_spec;
	} nvme;
} fe_t;
#define ENABLE              1
#define DISABLE             0
#define TRIM_SUPPORT        ENABLE

#ifndef PLP_DEBUG
#define PLP_DEBUG           0
#define PLP_DEBUG_GPIO      0
#else
#define PLP_DEBUG_GPIO      1
#endif

#define ASSERT_LVL_RD       0
#define ASSERT_LVL_DQA      1
#define ASSERT_LVL_CUST     2

#ifndef ASSERT_LVL
#define ASSERT_LVL       ASSERT_LVL_RD
#endif
#define FULL_TRIM           ENABLE
#define BG_TRIM             ENABLE
#define BG_TRIM_ON_TIME     ENABLE

#define FALSE       (0)                     ///< FALSE, numeric value is 0
#define TRUE        (1)                     ///< TRUE,  numeric value is 1

#define OFF         (0)                     ///< OFF, numeric value is 0
#define ON          (1)                     ///<  ON, numeric value is 1

/// @brief packed structure to access unaligned word(16bit) data
typedef  union
{
    u16 word;
    struct
    {
        u8 low;
        u8 high;
    } __attribute__((packed)) byte;
} __attribute__((packed)) Packedu16_t;

/// @brief packed structure to access unaligned dword(32bit) data
typedef  union
{
    u32 dword;
    struct
    {
        u16 low;
        u16 high;
    }__attribute__((packed))  word;
    struct
    {
        u8 b0;
        u8 b1;
        u8 b2;
        u8 b3;
    } __attribute__((packed)) byte;
} __attribute__((packed)) Packedu32_t;

/// @brief packed structure to access unaligned dword(64bit) data
typedef  union
{
    u64 qword;
    struct
    {
        u32 low;
        u32 high;
    }__attribute__((packed))  dword;
    struct
    {
        u16 w0;
        u16 w1;
        u16 w2;
        u16 w3;
    }__attribute__((packed))  word;
    struct
    {
        u8 b0;
        u8 b1;
        u8 b2;
        u8 b3;
        u8 b4;
        u8 b5;
        u8 b6;
        u8 b7;
    } __attribute__((packed)) byte;
} __attribute__((packed)) Packedu64_t;

//
typedef enum
{
    cInitBootCold=1,         ///< cold boot (power on)
    cInitBootFwUpdated,      ///< Firmware is updated (preserve all host state)
    cInitBootPowerDown,      ///< exit from Power Down mode
    cInitBootDeepPowerDown,  ///< exit from Deep Power Down mode
    cInitRunTime,            ///< run time rebuild FTL (for partial load l2p fail in run time)
    cInitReset,              ///< host reset
    cInitBootNA = 0x7FFFFFFF ///
} InitBootMode_t;


/// MACRO related algined/unaligned

#define PACKED_U16(addr)        (((Packedu16_t *)addr)->word)   ///< Unaligned 16bit data I/O
#define PACKED_U32(addr)        (((Packedu32_t *)addr)->dword)  ///< unaligned 32bit data I/O

