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

#include "a0_svn784/SEED_TABLE_ver01.h"
#if HAVE_MICRON_SUPPORT
#if defined(HMETA_SIZE)
#include "a0_svn784/ENC_CMF_code64_ver01.h"
#include "a0_svn784/DEC_CMF_code64_ver01.h"
#define cmf_enc_code1	cmf_enc_code64
#define cmf_dec_code1	cmf_dec_code64
#else // HMETA_SIZE
#include "a0_svn784/ENC_CMF_code63_ver01.h"
#include "a0_svn784/DEC_CMF_code63_ver01.h"
#define cmf_enc_code1	cmf_enc_code63
#define cmf_dec_code1	cmf_dec_code63
#endif
#else
#if defined(HMETA_SIZE)
// Jamie 20210108 Multi-CMF code61
#include "a0_svn784/ENC_CMF_code61_ver01.h"
#include "a0_svn784/DEC_CMF_code61_ver01.h"
// Jamie 20210108 Multi-CMF code62
#include "a0_svn784/ENC_CMF_code62_ver01.h"
#include "a0_svn784/DEC_CMF_code62_ver01.h"
// Jamie 20210108 Multi-CMF code291
#include "a0_svn784/ENC_CMF_code291_ver01.h"
#include "a0_svn784/DEC_CMF_code291_ver01.h"
#include "a0_svn784/ENC_CMF_codeffff_ver01.h"
// default with 512B w/o PI
#define cmf_enc_code1	cmf_enc_code61
#define cmf_dec_code1	cmf_dec_code61

#define _cmf_enc_code62 cmf_enc_code62
#define _cmf_dec_code62 cmf_dec_code62

#define _cmf_enc_code291 cmf_enc_code291
#define _cmf_dec_code291 cmf_dec_code291

#define _cmf_enc_codeffff cmf_enc_codeffff
#else // HMETA_SIZE
#include "a0_svn784/ENC_CMF_code61_ver01.h"
#include "a0_svn784/DEC_CMF_code61_ver01.h"
#define cmf_enc_code1	cmf_enc_code61
#define cmf_dec_code1	cmf_dec_code61
#endif
#endif

#if ENABLE_2_CMF
#include "a0_svn784/ENC_CMF_code62_ver01.h"
#include "a0_svn784/DEC_CMF_code62_ver01.h"
#define cmf_enc_code2	cmf_enc_code62
#define cmf_dec_code2	cmf_dec_code62
#else
#define cmf_enc_code2	cmf_enc_code1
#define cmf_dec_code2	cmf_dec_code1
#endif
#define cmf_enc_rom	cmf_enc_code65
#define cmf_dec_rom	cmf_dec_code65
