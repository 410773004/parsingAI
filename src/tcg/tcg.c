/*
//-----------------------------------------------------------------------------
//       Copyright(c) 2019-2020 Solid State Storage Technology Corporation.
//                         All Rights reserved.
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from SSSTC.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from SSSTC.
//-----------------------------------------------------------------------------
*/
#ifdef TCG_SUPPORT // Jack Li
#include "sect.h"
#include "string.h" 
#include "types.h"
#include "req.h"
#include "dtag.h"
#include "eccu.h"

#include "nvmet.h"
#include "crypto.h"
#include "tcgcommon.h"
#include "tcg.h"
#include "tcg_sh_vars.h"
#include "tcg_if_vars.h"
#include "tcg_nf_mid.h"
#include "tcg_if_nf_api.h"

#include "security_api.h"

#include "trim.h"


//#include "tcgtbl.h"
#include "tcgnvme.h"

#ifdef NS_MANAGE
#include "nvme_spec.h"
extern struct ns_array_manage *ns_array_menu;
#endif
//#include "tcgtbl.h"
#define __FILEID__ tcg
#include "trace.h"

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define TCG_ACT_IN_OPAL()           (mSgUser.range == 0)                // activated in Opal_Mode or not activated yet
#define TCG_ACT_IN_ALL_SU()         ((mSgUser.range & 0x1ff) == 0x1ff)  // activated in Entire_Single_User_Mode
#define WRAP_KEK_LEN                sizeof(WrapKEK)

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
static ddr_data u64 method_uid_lookup[] =
{
    UID_MethodID_Next,      UID_MethodID_GetACL,    UID_MethodID_GenKey,        UID_MethodID_RevertSP,
    UID_MethodID_Get,       UID_MethodID_Set,       UID_MethodID_Authenticate,  UID_MethodID_Revert,
    UID_MethodID_Activate,  UID_MethodID_Random,    UID_MethodID_Reactivate,    UID_MethodID_Erase,
};

typedef enum
{
    cMcMtd_Next = 0,    cMcMtd_GetACL,          cMcMtd_GenKey,          cMcMtd_RevertSP,
    cMcMtd_Get,         cMcMtd_Set,             cMcMtd_Authenticate,    cMcMtd_Revert,
    cMcMtd_Activate,    cMcMtd_Random,          cMcMtd_Reactivate,      cMcMtd_Erase,
    cMcMtd_illegal,     cMcMtd_Last,
} msgTcgMethod_t;

static ddr_data u16 (*gCbfProc_method_map[cMcMtd_Last])(req_t *req) =
{
    f_method_next,      f_method_getAcl,        f_method_genKey,        f_method_revertSp,
    f_method_get,       f_method_set,           f_method_authenticate,  f_method_revert,
    f_method_activate,  f_method_random,        f_method_reactivate,    f_method_erase,
    f_method_illegal,
};

//----Max add for compile----//

//#define CPIN_LENGTH         32 
//#define DTAG_SHF		(13)   
//#define DTAG_SZE		(1 << DTAG_SHF)	
extern tcg_sync_t   tcg_sync;  
extern volatile u8 host_sec_bitz;
extern tcg_mid_info_t* tcg_mid_info;

//---------------------------//


//extern u16     mReadLockedStatus;           // b0=1: GlobalRange is Read-Locked, b1~b8=1: RangeN is Read-Locked.
//extern u16     mWriteLockedStatus;          // b0=1: GlobalRange is Write-Locked, b1~b8=1: RangeN is Write-Locked.

extern  void host_Properties_Reset(void);

extern void sec_gen_sha3_256_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash);
extern void sec_gen_rand_number(u8 *buf, u8 rand_len);

#if 1  //PBKDF2

ddr_code void sha3_hmac_starts( sha3_context *ctx, unsigned char *key, u32 keylen)
{

    u32 i;
    unsigned char sum[32];
 

    if( keylen > 136 )
    {
    	sec_gen_sha3_256_hash(key, keylen, sum);
		keylen = 32;
        //sha2( key, keylen, sum, is224 );
        //keylen = ( is224 ) ? 28 : 32;
        key = sum;
     }
 
    memset( ctx->ipad, 0x36, 136);
    memset( ctx->opad, 0x5C, 136);
 
     for( i = 0; i < keylen; i++ )
     {
     	ctx->ipad[i] = (unsigned char)( ctx->ipad[i] ^ key[i] );
        ctx->opad[i] = (unsigned char)( ctx->opad[i] ^ key[i] );
     }
 
	memcpy((void *)ctx->buffer, (const void *)ctx->ipad, 136);
    //sha2_starts( ctx, is224 );
    //sha2_update( ctx, ctx->ipad, 64 );
 
    memset( sum, 0, sizeof( sum ) );
 }


/*
 * SHA-256 HMAC process buffer
 */

ddr_code void sha3_hmac_update( sha3_context *ctx, const unsigned char *input, u32 ilen )
{

	u8 tmpbuf[ilen+136];
	memcpy((void *)tmpbuf, ctx->buffer, 136);
	memcpy((void *)(tmpbuf + 136), input, ilen);
	sec_gen_sha3_256_hash(tmpbuf, ilen+136, ctx->buffer);
    //sha2_update( ctx, input, ilen );

	memset( tmpbuf, 0, sizeof( tmpbuf ) );
 }

ddr_code void sha3_hmac_finish( sha3_context *ctx, unsigned char output[32] )
 {

    //int is224, hlen;
    u8 tmpbuf[32+136];
 
	memcpy((void *)      tmpbuf, ctx->opad,  136);
	memcpy((void *)(tmpbuf+136), ctx->buffer, 32);
	sec_gen_sha3_256_hash(tmpbuf, (32+136), output);
 
    //is224 = ctx->is224;
    //hlen = ( is224 == 0 ) ? 32 : 28;

    //sha2_finish( ctx, tmpbuf );
    //sha2_starts( ctx, is224 );
    //sha2_update( ctx, ctx->opad, 64 );
    //sha2_update( ctx, tmpbuf, hlen );
    //sha2_finish( ctx, output );
 
     memset( tmpbuf, 0, sizeof( tmpbuf ) );
 }

ddr_code void PKCS5_PBKDF2_HMAC(u32 *password, u32 plen, u32 *salt, u32 slen,
     const unsigned long iteration_count, const unsigned long key_length, u32 *output)
{
	sha3_context ctx;
    //sha2_starts(&ctx, 0);
 
    // Size of the generated digest
    unsigned char md_size = 32;
    unsigned char md1[32];
    unsigned char work[32];
 
	sha3_hmac_starts(&ctx, (u8 *)password, plen);
	sha3_hmac_update(&ctx, (u8 *)salt, slen);
	sha3_hmac_finish(&ctx, md1);
	memcpy(work, md1, md_size);

	unsigned long ic = 1;
	for (ic = 1; ic < iteration_count; ic++)
	{
		// U2 ends up in md1
		sha3_hmac_starts(&ctx, (u8 *)password, plen);
		sha3_hmac_update(&ctx, md1, md_size);
		sha3_hmac_finish(&ctx, md1);
		// U1 xor U2
		unsigned long i = 0;
		for (i = 0; i < md_size; i++) {
			work[i] ^= md1[i];
		}
		// and so on until iteration_count
	}
	memcpy(output, work, 32);
/*
     unsigned long counter = 1;
     unsigned long generated_key_length = 0;
     while (generated_key_length < key_length) {
         // U1 ends up in md1 and work
        unsigned char c[4];
        c[0] = (counter >> 24) & 0xff;
        c[1] = (counter >> 16) & 0xff;
        c[2] = (counter >> 8) & 0xff;
        c[3] = (counter >> 0) & 0xff;

        sha2_hmac_starts(&ctx, (unsigned char *)password, plen, 0);
        sha2_hmac_update(&ctx, (unsigned char *)salt, slen);
        sha2_hmac_update(&ctx, c, 4);
        //unsigned char c[4];
        //c[0] = (counter >> 24) & 0xff;
        //c[1] = (counter >> 16) & 0xff;
        //c[2] = (counter >> 8) & 0xff;
        //c[3] = (counter >> 0) & 0xff;

        sha2_hmac_starts(&ctx, (u8 *)password, plen, 0);
        sha2_hmac_update(&ctx, (u8 *)salt, slen);
        //sha2_hmac_update(&ctx, c, 4);
         sha2_hmac_finish(&ctx, md1);
         memcpy(work, md1, md_size);
 
         unsigned long ic = 1;
         for (ic = 1; ic < iteration_count; ic++) {
             // U2 ends up in md1
            sha2_hmac_starts(&ctx, (unsigned char *)password, plen, 0);
            sha2_hmac_starts(&ctx, (u8 *)password, plen, 0);
             sha2_hmac_update(&ctx, md1, md_size);
             sha2_hmac_finish(&ctx, md1);
             // U1 xor U2

         generated_key_length += bytes_to_write;
         ++counter;
     }
*/
 }



#endif

ddr_code bool cb_tcg_tbl_recovery_complete(req_t *req)
{
	tcg_core_trace(LOG_INFO, 0xa4c8, "cb_tcg_tbl_recovery_complete not coding yet, Max Pan");
/*
    printk("cb_tcg_tbl_recovery_complete() is_cb_executed[%08x]\n", is_cb_executed);
    DBG_P(0x2, 0x03, 0x710122, 4, is_cb_executed);  // cb_tcg_tbl_recovery_complete() is_cb_executed[%08x]

    req->completion = nvmet_core_cmd_done;
    if(is_cb_executed){   // if FASLE then put DTAG & free MEM at NvmeCmd_security_send_XferDone()
        nvmet_evt_cmd_done(req);
    }
	*/

    return mTRUE;

}

ddr_code void tcg_tbl_recovery()
{
	tcg_nf_G1Rd(mTRUE,mFALSE);
	tcg_nf_G2Rd(mTRUE,mFALSE);
	tcg_nf_G3Rd(mTRUE,mFALSE);
	tcg_core_trace(LOG_INFO, 0x719e, "tcg_tbl_recovery done!");
}

ddr_code void ClearMtableChangedFlag(void)
{

    flgs_MChnged.b.G1    = mFALSE;
    flgs_MChnged.b.G2    = mFALSE;
    flgs_MChnged.b.G3    = mFALSE;
    flgs_MChnged.b.SMBR  = mFALSE;
    flgs_MChnged.b.DS    = mFALSE;
#if CO_SUPPORT_AES
    mRawKeyUpdateList = 0;
#endif
}

ddr_code u16 tcg_cmdPkt_abortSession(void)
{

    SDS_t *pbuf = (SDS_t *)dataBuf;
    SS_rPayLoad_t *ppl = (SS_rPayLoad_t *)(dataBuf + offsetof(SDS_t, DataPayLoad));  // point of payload

    memset(dataBuf, 0x00, sizeof(SDS_t) + sizeof(SS_rPayLoad_t) + sizeof(u32));      //clear Tcg Recv Buffer, sizeof(U32) is extra for multiple of 4
    pbuf->ComPacket.ComID = swap_u16(BASE_COMID);
    //Construct payload  for "Close Session"
    ppl->CallToken      = TOK_Call;
    ppl->s_Atom0        = 0xA8;
    ppl->InvokingUID    = swap_u64((u64)SMUID);
    ppl->s_Atom1        = 0xA8;
    ppl->MethodUID      = swap_u64((u64)SM_MTD_CloseSession);
    ppl->StartListToken = TOK_StartList;
    ppl->s_Atom2        = 0x84;
    ppl->HSN            = swap_u32(mSessionManager.HostSessionID);
    ppl->s_Atom3        = 0x84;
    ppl->TSN            = swap_u32(mSessionManager.SPSessionID);
    ppl->EndListToken   = TOK_EndList;
    ppl->EndOfDataToken = TOK_EndOfData;
    iDataBuf = offsetof(SDS_t, DataPayLoad) + offsetof(SS_rPayLoad_t, MethodStatusList[0]);

    set_status_code(0, 0, 0);
    tcg_prepare_respPacket_update(mTRUE);
    return STS_SUCCESS;

}

/***********************************************************
 *tcg_cmdPkt_extracter() <--- TcgCmdPktParser(BYTE* buf):
 *  IF-SEND data block is placed into structure "mCmdPkt"
 *  Packet format should be checked here.
 *
 *  If session has not started yet, HSN and TSN should be zero. HSN is initially
 *  transmitted in the StartSession method, and TSN is initially transmitted in
 *  the SyncSession method.
 *
 *  return STS_SUCCESS if no error. Check Test Cases 3.1.4
 ***********************************************************/
ddr_code u16 tcg_cmdPkt_extracter(req_t *req, u8* buf)
{
	struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;	
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
	nvme_tcg_cmd_dw11_t cmd_dw11 = (nvme_tcg_cmd_dw11_t)cmd->cdw11;

    SDS_t *pbuf = (SDS_t *)buf;
    u16 result  = STS_SUCCESS;
    u8  errcode = 0x00;

    *((u32*)mCmdPkt.rsv) = swap_u32(pbuf->ComPacket.Reserved);
    mCmdPkt.ComID        = swap_u16(pbuf->ComPacket.ComID);

    if(mCmdPkt.ComID != cmd_dw10.b.com_id)
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x10;     goto EXIT0; }

    mCmdPkt.ComIDExt    = swap_u16(pbuf->ComPacket.ExtendedComID);
    if(mCmdPkt.ComIDExt!=0x00)
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x20;     goto EXIT0; }

    mCmdPkt.Outstanding = swap_u32(pbuf->ComPacket.OutstandingData);
    mCmdPkt.MinTx       = swap_u32(pbuf->ComPacket.MinTransfer);
    mCmdPkt.length      = swap_u32(pbuf->ComPacket.Length);

	tcg_core_trace(LOG_INFO, 0x2b07, "mCmdPkt.ComID|%x, mCmdPkt.length|%x\n", mCmdPkt.ComID, mCmdPkt.length);

	if(mCmdPkt.length > cmd_dw11.len_alloc)
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x30;     goto EXIT0; }

    if(mCmdPkt.length < sizeof(Packet_t))
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x40;     goto EXIT0; }

    //PktFmt
    mCmdPkt.mPktFmt.TSN             = swap_u32(pbuf->Packet.TSN);
    mCmdPkt.mPktFmt.HSN             = swap_u32(pbuf->Packet.HSN);
    mCmdPkt.mPktFmt.SeqNo           = swap_u32(pbuf->Packet.SeqNumber);
    *((u16 *)mCmdPkt.mPktFmt.rsv)   = swap_u16(pbuf->Packet.Reserved);
    mCmdPkt.mPktFmt.AckType         = swap_u16(pbuf->Packet.AckType);
    mCmdPkt.mPktFmt.ack             = swap_u32(pbuf->Packet.Acknowledgement);
    mCmdPkt.mPktFmt.length          = swap_u32(pbuf->Packet.Length);

    //if length not fit or length<12, Regular Session aborted or packet discarded for Control Session...
    if ((mCmdPkt.mPktFmt.length > (mCmdPkt.length - sizeof(Packet_t))) ||
        (mCmdPkt.mPktFmt.length < sizeof(DataSubPacket_t)))
    {
        if(mSessionManager.state == SESSION_START)
        {   result = STS_SESSION_ABORT;     errcode = 0x50;     goto EXIT0; }
        else
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0x60;     goto EXIT0; }
    }

    //SubPktFmt
    *((u32*)&mCmdPkt.mSubPktFmt.rsv[0]) = swap_u32(pbuf->DataSubPacket.Reserved_DW);
    *((u16*)&mCmdPkt.mSubPktFmt.rsv[4]) = swap_u16(pbuf->DataSubPacket.Reserved_W);
    mCmdPkt.mSubPktFmt.kind             = swap_u16(pbuf->DataSubPacket.Kind);
    mCmdPkt.mSubPktFmt.length           = swap_u32(pbuf->DataSubPacket.Length);

    //if length exceeds the packet, Regular Session aborted or packet discarded for Control Session...
    if ((mCmdPkt.mSubPktFmt.length > (mCmdPkt.mPktFmt.length - sizeof(DataSubPacket_t))) ||
        ((mCmdPkt.mPktFmt.length - mCmdPkt.mSubPktFmt.length) > (12 * 2)))  // for Lenovo provide script (TCGMain_AutoBrief_PCIe.srt)
    {
        if(mSessionManager.state == SESSION_START)
        {   result = STS_SESSION_ABORT;     errcode = 0x70;     goto EXIT0; }
        else
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0x80;     goto EXIT0; }
    }
    ploadLen = mCmdPkt.mSubPktFmt.length;

    memcpy((void *)(&mCmdPkt.payload[0]), buf + offsetof(SDS_t, DataPayLoad), sizeof(mCmdPkt.payload));   //copy trusted send cmd buffer to mCmdPkt.payload buffer

    //CmdPkt format checking...

	//tcg_core_trace(LOG_INFO, 0, "(Max set) TSN|%x , HSN|%x",mCmdPkt.mPktFmt.TSN, mCmdPkt.mPktFmt.HSN);
	
    if((mCmdPkt.mPktFmt.TSN == 0) && (mCmdPkt.mPktFmt.HSN == 0))
    { //Session Manager call only, for Test Cases A11-3-5-6-1-1
        bControlSession = mTRUE;
        return STS_SUCCESS;
    }

    else if(mSessionManager.state == SESSION_START)
    {
        if((mCmdPkt.mPktFmt.TSN != mSessionManager.SPSessionID) ||
           (mCmdPkt.mPktFmt.HSN != mSessionManager.HostSessionID))
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0x90;     goto EXIT0; }

        bControlSession = mFALSE;   //Regular Session
    }
    else //if(mSessionManager.state==SESSION_CLOSE)
    {
        if((mCmdPkt.mPktFmt.TSN != 0) || (mCmdPkt.mPktFmt.HSN != 0))
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0xA0;     goto EXIT0; } //A8-1-1-1-1(3)

        bControlSession = mTRUE;
    }

    //Token checking...
EXIT0:
    if(result != STS_SUCCESS)
    {
        if(result == STS_STAY_IN_IF_SEND){
            // DBG_P(1, 3, 0x820168);  //82 01 68, "!!NG: CmdPktParser NG -> Stay in IF-SEND, errcode= %X", 1
        }else{
            // DBG_P(1, 3, 0x820169);  //82 01 69, "!!NG: CmdPktParser NG -> Abort Session, errcode= %X", 1
        }
        // DBG_P(1, 1, errcode);
		tcg_core_trace(LOG_INFO, 0x6266, "result|%x, errcode|%x\n", result, errcode);
    }
    // errcode++;  errcode--;  // alexcheck
    return result;

}


/****************************************************************************
 * AtomDecoding_ByteHdr():
 ****************************************************************************/
// check if the atom header is "byte sequence" type or not
// [OUT] *length: the length of the byte sequence
ddr_code u16 AtomDecoding_ByteHdr(u32* length)
{
    u16 result;
    u8 byte;
    u8 errCode = 0x00;  //no error

    for (;;)
    {
        if (iPload >= mCmdPkt.mSubPktFmt.length)
        {
            errCode = 0x10;
            result = STS_SESSION_ABORT;
            goto ChkByte_Err;
        }

        byte = mCmdPkt.payload[iPload++];
        if (byte != 0xff)
            break;
    }

    if ((byte & 0x80) == 0x00)
    {  // Tiny Atom
        errCode = 0x20;
        result = STS_INVALID_PARAMETER;
        goto ChkByte_Err;
    }
    else if ((byte & 0xC0) == 0x80)
    { // Short Atom, 0-15 bytes of data
        if (byte & 0x20)
        { // byte sequence
            if (byte & 0x10)
            { // TODO: continued into another atom
                errCode = 0x40;
                result = STS_INVALID_PARAMETER;
                goto ChkByte_Err;
            }

            *length = byte & 0x0f;
        }
        else
        { // integer
            errCode = 0x50;
            result = STS_INVALID_PARAMETER;
            goto ChkByte_Err;
        }
    }
    else if ((byte & 0xE0) == 0xC0)
    { // Medium Atom: 1-2047 bytes
        if (byte & 0x10)
        { // byte sequence
            if (byte & 0x08)
            { // TODO: Continued into another atom
                errCode = 0x70;
                result = STS_INVALID_PARAMETER;
                goto ChkByte_Err;
            }

            *length = (byte & 0x07) * 0x100 + mCmdPkt.payload[iPload++];
        }
        else
        { // integer
            errCode = 0x80;
            result = STS_INVALID_PARAMETER;
            goto ChkByte_Err;
        }
    }
    else if ((byte & 0xFC) == 0xe0)
    {  // Long Atom
        if (byte & 0x02)
        { // byte sequence
            u32 tmp32;

            if (byte & 0x01)
            { // TODO: Continued into another atom
                errCode = 0x90;
                result = STS_INVALID_PARAMETER;
                goto ChkByte_Err;
            }

            tmp32 = mCmdPkt.payload[iPload++];
            tmp32 = tmp32 * 0x100 + mCmdPkt.payload[iPload++];
            tmp32 = tmp32 * 0x100 + mCmdPkt.payload[iPload++];
            *length = tmp32;
        }
        else
        {
            errCode = 0xA0;
            result = STS_INVALID_PARAMETER;
            goto ChkByte_Err;
        }
    }
    else
    {
        errCode = 0xB0;    // unknow Token
        result = STS_INVALID_PARAMETER;
        goto ChkByte_Err;
    }
    return STS_SUCCESS;

ChkByte_Err:
    // TCG_PRINTF("AtomDecoding err %2X\n", errCode);
	tcg_core_trace(LOG_INFO, 0xe584, "AtomDecoding_ByteHdr() errCode|%x\n", errCode);
	//TCGPRN("AtomDecoding_ByteHdr() errCode|%x\n", errCode);
    //DBG_P(0x2, 0x03, 0x710000, 4, errCode);  // AtomDecoding_ByteHdr() errCode|%x
    // DBG_P(2, 3, 0x82002A, 1, errCode);  //82 00 2A, "AtomDecoding err[%X]", 1
    return result;
}


/****************************************************************************
 * AtomDecoding_Uid2():
 ****************************************************************************/
// Atom Decoding, only accept "Byte" and the byte length must be "8"
// output the result (U64) to *data
ddr_code u16 AtomDecoding_Uid2(u8* data)
{
    u8 j;
    u16 result;
    u32 len = 0;

    result = AtomDecoding_ByteHdr(&len);
    if ((result != STS_SUCCESS) || (len != sizeof(u64)))
        return result;

    for (j = 8; j>0; j--)
        *(data + j - 1) = mCmdPkt.payload[iPload++];

    return STS_SUCCESS;
}

/****************************************************************************
 * AtomDecoding_Uint():
 ****************************************************************************/
//
//Atom Decoding for unsigned integer
//    *data: integer buffer for output
//  datalen: buffer length
//
//  Return STS_INVALID_PARAMETER if the sizeof(unsigned integer) > buffer size (datalen)
//
ddr_code u16 AtomDecoding_Uint(u8 *data, u32 dataLen)
{
    u32 j, len = 0;
    u8 byte, err;

    for (;;)
    {
        if (iPload >= mCmdPkt.mSubPktFmt.length)
        {
            err = 0x10;
            goto AtomDecoding_Uint_Abort;
        }

        byte = mCmdPkt.payload[iPload++];
        if (byte != 0xff)
            break;
    }

    for (j = 0; j < dataLen; j++)
        data[j] = 0;

    if (byte<64)
    {  //Tiny atom, 0~63
        data[0] = byte;
        return STS_SUCCESS;
    }
    else if ((byte & 0xF0) == 0x80)
    { //Short Atom, unsigned integer
        len = byte & 0x0f;
    }
    else if ((byte & 0xF8) == 0xc0)
    { //Medium Atom, unsigned integer
        // HiByte(LoWord(len)) = byte & 0x07;
        *(((u8*)&len)+1) = byte & 0x07;
        // LoByte(LoWord(len)) = mCmdPkt.payload[iPload++];
        *(((u8*)&len)+0) = mCmdPkt.payload[iPload++];
    }
    else if (byte == 0xe0)
    {  //Long Atom, unsigned integer
        // LoByte(HiWord(len)) = mCmdPkt.payload[iPload++];
        *(((u8*)&len)+2) = mCmdPkt.payload[iPload++];
        // HiByte(LoWord(len)) = mCmdPkt.payload[iPload++];
        *(((u8*)&len)+1) = mCmdPkt.payload[iPload++];
        // LoByte(LoWord(len)) = mCmdPkt.payload[iPload++];
        *(((u8*)&len)+0) = mCmdPkt.payload[iPload++];
    }
    else
    { // unknow Token
        err = 0x20;
        goto AtomDecoding_Uint_Abort;
    }

    if (len == 0)
    {
        err = 0x30;
        goto AtomDecoding_Uint_Err2;
    }

    if (len>dataLen)
    { // check if the data outside the buffer are all zero
        for (j = 0; j<(len - dataLen); j++)
        {
            if (mCmdPkt.payload[iPload++] != 0)
            {
                byte = 0x50;
                goto AtomDecoding_Uint_Err2;
            }
        }
        len = dataLen;
    }

    for (j = len; j>0; j--)
        data[j - 1] = mCmdPkt.payload[iPload++];

    return STS_SUCCESS;

AtomDecoding_Uint_Abort:

	tcg_core_trace(LOG_INFO, 0x06fb, "AtomDecoding_Uint() err|%x\n", err);

    return STS_SESSION_ABORT;

AtomDecoding_Uint_Err2:

	tcg_core_trace(LOG_INFO, 0x6d67, "AtomDecoding_Uint() err2|%x\n", err);

    return STS_INVALID_PARAMETER;
}

/****************************************************************************
 * AtomDecoding_HUid2():
 ****************************************************************************/
// Atom Decoding, only accept "Byte" and the byte length must be "4"
// Output the result (U32) to data
ddr_code u16 AtomDecoding_HUid2(u8* data)
{
    u8 j;
    u16 result;
    u32 len = 0;

    result = AtomDecoding_ByteHdr(&len);
    if ((result != STS_SUCCESS) || (len != 4))
        return result;

    for (j = 4; j>0; j--)
        *(data + j - 1) = mCmdPkt.payload[iPload++];

    return STS_SUCCESS;
}


/****************************************************************************
 * AtomEncoding_ByteHdr():
 ****************************************************************************/
// Encode the Byte Sequence into Atom (Core 3.2.2.3.1)
// This function will only encode the Atom Header and write it directly to "dataBuf[iDataBuf++]".
//
// Return the Byte Count written to dataBuf[].
ddr_code int AtomEncoding_ByteHdr(u32 length)
{
    //Byte Sequence,  write Header only
    if (length <= 15)
    { //Short Atom, length=0~15
        dataBuf[iDataBuf++] = 0xA0 + (u8)length;
        return 1;
    }
    else if (length <= 2047)
    { //Medium Atom
        // dataBuf[iDataBuf++] = 0xD0 + HiByte(LoWord(length));
        dataBuf[iDataBuf++] = 0xD0 + *(((u8*)&length)+1);
        // dataBuf[iDataBuf++] = LoByte(LoWord(length));
        dataBuf[iDataBuf++] = *(((u8*)&length)+0);
        return 2;
    }
    else
    { //Long Atom
        dataBuf[iDataBuf++] = 0xE2;
        // dataBuf[iDataBuf++] = LoByte(HiWord(length));
        dataBuf[iDataBuf++] = *(((u8*)&length)+2);
        // dataBuf[iDataBuf++] = HiByte(LoWord(length));
        dataBuf[iDataBuf++] = *(((u8*)&length)+1);
        // dataBuf[iDataBuf++] = LoByte(LoWord(length));
        dataBuf[iDataBuf++] = *(((u8*)&length)+0);
        return 4;
    }
}

/****************************************************************************
 * AtomEncoding_Integer():
 ****************************************************************************/
//
// Encode the Integer Value into Atom
//
// This function will encode the Atom Header + Data, and write them directly to "dataBuf[iDataBuf++]".
// It only deals with the unsigned value.
//
// Return the Byte Count written to dataBuf[].
ddr_code int AtomEncoding_Integer(u8 *data, u8 size)
{
    //Integer value,  write Header + data
    u8 i;

    for (i = size; i>0; i--)
    {
        if (*(data + i - 1) != 0)
            break;
    }

    size = i;

    if (size <= 1)
    {
        if (*data <= 63) //Tiny Atom 0~63
            dataBuf[iDataBuf++] = *data;
        else
        {
            dataBuf[iDataBuf++] = 0x81;
            dataBuf[iDataBuf++] = *data;
        }
    }
    else if (size <= 15)
    { //size>=2, <=15
      //Short Atom, length=0~15
        dataBuf[iDataBuf++] = 0x80 + size;

        for (i = size; i>0; i--)
            dataBuf[iDataBuf++] = *(data + i - 1);
    }
    else
        return zNG;

    return zOK;
}

/****************************************************************************
 * AtomEncoding_ByteSeq():
 ****************************************************************************/
ddr_code int AtomEncoding_ByteSeq(u8* data, u32 len)
{
    u32 i32;
    int cnt = AtomEncoding_ByteHdr(len);

    for (i32 = 0; i32 < len; i32++) //UID
        dataBuf[iDataBuf++] = data[i32];
    return (cnt + len);
}

/****************************************************************************
 * AtomEncoding_Int2Byte():
 ****************************************************************************/
// Ex: encodes a U64 integer to the byte sequence on databuf[]
ddr_code int AtomEncoding_Int2Byte(u8* data, u32 byteSize)
{
    u32 i32;
    int cnt = AtomEncoding_ByteHdr(byteSize);

    for (i32 = 1; i32 <= byteSize; i32++) //UID
        dataBuf[iDataBuf++] = data[byteSize - i32];
    return (cnt + byteSize);
}


/****************************************************************************
 * ChkToken():
 ****************************************************************************/
// skip Empty Token (0xff), and return the next non-empty payload
// (iPload will be updated to the non-empty payload)
ddr_code u8 ChkToken(void)
{
    u8 data;
    for (;;)
    {
        data = mCmdPkt.payload[iPload++];
        if (data != 0xff)
            return data;

        if (iPload > mCmdPkt.mSubPktFmt.length)
            return 0xff;    //NG
    }
}

//
// update statusCode[] from payload and check if status = "F0 00 00 00 F1"
// return STS_SUCCESS if ok.
//
ddr_code u16 chk_method_status(void)
{
    if (ChkToken() != TOK_StartList) return STS_SESSION_ABORT;

    if (AtomDecoding_Uint(&statusCode[0], sizeof(statusCode[0])) != STS_SUCCESS) return STS_SESSION_ABORT;

    if (AtomDecoding_Uint(&statusCode[1], sizeof(statusCode[1])) != STS_SUCCESS) return STS_SESSION_ABORT;

    if (AtomDecoding_Uint(&statusCode[2], sizeof(statusCode[2])) != STS_SUCCESS) return STS_SESSION_ABORT;

    if(statusCode[0] != 0x00) return statusCode[0];//status NG, method call fail!

    if(ChkToken() != TOK_EndList) return STS_SESSION_ABORT;
    else return STS_SUCCESS;
}


ddr_code u16 TcgTperReset(req_t *req)
{

	tcg_core_trace(LOG_INFO, 0xf364, "TperReset(): Enable:%d", pG1->b.mAdmTPerInfo_Tbl.val[0].preset);

    if(pG1->b.mAdmTPerInfo_Tbl.val[0].preset==mTRUE)
    {
        if(mSessionManager.state==SESSION_START)
            ResetSessionManager();

        host_Properties_Reset();

		u32 old = mTcgStatus;
		
        MbrCtrlTbl_Reset(Programmatic);

        LockingTbl_Reset(Programmatic);     // LckLocking_Tbl "ProgrammaReset"
        LockingRangeTable_Update();         //Update Read/Write LockedTable for Media Read/Write control

		if(mTcgStatus!=old)
			tcg_init_aes_key_range();

		if(mTcgStatus & MBR_SHADOW_MODE)
			tcg_io_chk_range = TcgRangeCheck_SMBR;
		else
			tcg_io_chk_range = TcgRangeCheck;
		//ipc_tcg_change_chkfunc_BTN_wr(mTcgStatus);
		
        gTcgCmdState = ST_AWAIT_IF_SEND;

        return mTRUE;
    }
    else
    {
        tcg_core_trace(LOG_ERR, 0x24b4, "TperReset NG caused by disable");
        return mFALSE;
    }
}

ddr_code void TcgHardReset(void)
{
    if (mSessionManager.state == SESSION_START)
        ResetSessionManager();

    host_Properties_Reset();

    // *** [ No Lock State Reset for HardReset
    //LockingTbl_Reset(PowerCycle);       // LckLocking_Tbl "ProgrammaReset"
    //LockingRangeTable_Update();         //Update Read/Write LockedTable for Media Read/Write control

    //MbrCtrlTbl_Reset(PowerCycle);
    // ] &&&
#if TCG_FS_BLOCK_SID_AUTH
    if (mTcgStatus&SID_HW_RESET)
        mTcgStatus &= ~(SID_BLOCKED + SID_HW_RESET);      // Clear Events are reset when a Clear Event occurs
#endif
    gTcgCmdState = ST_AWAIT_IF_SEND;
	
	tcg_core_trace(LOG_INFO, 0x5e9a, "HardReset() - TcgStatus|%x",mTcgStatus);
}


//-------------------tcg_cmdPkt_payload_decoder-------------------------
ddr_code u32 tcg_cmdPkt_payload_decoder(req_t *req)
{
	u8  byte;
    u32 tmp32;  //for transaction
    u32 result = STS_SUCCESS;

    iPload = 0;   //reset payload index
    if (bControlSession)
    {   // Session is not started => Control Session, only accept SMUID
        // Control Session, TSN/HSN=0x00, A6-3-1...
        byte = ChkToken();
        if (byte == TOK_Call)
        {   //1. Call token, start of a method invocation...
            //2. check method header:

            // get Invoking UID
            if (AtomDecoding_Uid2(invokingUID.bytes) != STS_SUCCESS)
                return STS_STAY_IN_IF_SEND;

            if (invokingUID.all != SMUID)
                return STS_STAY_IN_IF_SEND;

            // get Method UID
            if (AtomDecoding_Uid2(methodUID.bytes) != STS_SUCCESS)
                return STS_STAY_IN_IF_SEND;

            result = invoking_session_manager(req);
            if (result != STS_SUCCESS)
            {
                if (result & 0xff00)
                    return result;  //STS_SESSION_ABORT or STS_STAY_IN_IF_SEND
            }
            //add status to reponse buffer and update length
            set_status_code(result, 0, 0);
            tcg_prepare_respPacket_update(mTRUE);
            return result;
        }
        else
        {   //Upexpected!
            return STS_STAY_IN_IF_SEND;
        }
    }
    else
    {   //TSN/HSN!=0x00, Session is started => Regular Session, A6-0-1...

        // 0. check Control Tokens first
        byte = ChkToken();
        if (byte == TOK_Call)
        {   //1. Call token, start of a method invocation...
            //2. check method header:
            // get Invoking UID
            if (AtomDecoding_Uid2(invokingUID.bytes) != STS_SUCCESS)
            {
                result = STS_NOT_AUTHORIZED;
                goto UPDATE_STATUS;
            }

            // get Method UID
            if (AtomDecoding_Uid2(methodUID.bytes) != STS_SUCCESS)
            {
                result = STS_NOT_AUTHORIZED;
                goto UPDATE_STATUS;
            }

            //3. parameters...
            //4. End of data
            //5. Status code list
            if(invokingUID.all == SMUID)
                result = invoking_session_manager(req);  // Session Manager
            else
                result = invoking_tcg_table(req);

UPDATE_STATUS:
            if(result != STS_SUCCESS)
            {
                if ((result == STS_SESSION_ABORT) || (result == STS_STAY_IN_IF_SEND))
                    return result;

				if(result == STS_AUTHORITY_LOCKED_OUT)
				{
                    goto UPDATE_RespPacket;
				}
				
				if (result == STS_SUCCESS_THEN_ABORT)
                { //??? D10-3-3-1-1
                    ResetSessionManager();
                    result = STS_SUCCESS;
                }

				fill_no_data_token_list();	  //cj: need these lines for RevertSP, 02/15/2013

            }

UPDATE_RespPacket:

			//add status to reponse buffer and update length
            set_status_code(result, 0, 0);

            if(req->completion == nvmet_core_cmd_done){
                result = tcg_prepare_respPacket_update(mTRUE);
            }

            return result;
        }
        else if (byte == TOK_EndOfSession)
        {
            //EOD, no status ...
            //for(j=0; j<3; j++)     mSessionManager.status[j] = mCmdPkt.payload[iPload++];
			tcg_core_trace(LOG_INFO, 0x2555, "<<< Close Session >>>");
			//DBG_P(1, 3, 0x710135);  //71 01 35, "Close Session >>>"
            invokingUID.all = SMUID;
            methodUID.all   = SM_MTD_CloseSession;
			
			if(mSessionManager.TransactionState == TRNSCTN_ACTIVE)
			{ //transaction abort
				tcg_core_trace(LOG_INFO, 0x48bd, "EndSession without EndTransaction : Abort!");

				if(flgs_MChnged.b.G1 == mTRUE){

					tcg_nf_G1Rd(mTRUE,mFALSE);
					tcg_nf_G1Wr(mTRUE);
				}
		
				if(flgs_MChnged.b.G2 == mTRUE){

					tcg_nf_G2Rd(mTRUE,mFALSE);
					tcg_nf_G2Wr(mTRUE);
				}
		
				if(flgs_MChnged.b.G3 == mTRUE){

					tcg_nf_G3Rd(mTRUE,mFALSE);
					tcg_nf_G3Wr(mTRUE);
				}
				if(flgs_MChnged.b.DS == mTRUE){
					
					tcg_nf_DSAbort(0x00,0x280);
				}
				if(flgs_MChnged.b.SMBR == mTRUE){
					
					tcg_nf_SMBRAbort(0x00,0x2000);
				}

				ClearMtableChangedFlag();
				bKeyChanged = mFALSE;
				
				mSessionManager.TransactionState = TRNSCTN_IDLE;		
            }

            ResetSessionManager();

            //prepare payload
            dataBuf[iDataBuf++] = TOK_EndOfSession;
            tcg_prepare_respPacket_update(mFALSE);

            return STS_SUCCESS;
        }	
        else if (byte==TOK_StartTransaction)
        {
            result = AtomDecoding_Uint((u8*)&tmp32, sizeof(tmp32));
			
            if (result != STS_SUCCESS) // no status code
                return STS_SESSION_ABORT;

            if (mSessionManager.TransactionState != TRNSCTN_IDLE)
                return STS_SESSION_ABORT;  // found Transaction token

            // Enable Transaction
            dataBuf[iDataBuf++] = TOK_StartTransaction;
            dataBuf[iDataBuf++] = 0;   //ok, for response Transaction status
            tcg_prepare_respPacket_update(mFALSE);
            mSessionManager.TransactionState = TRNSCTN_ACTIVE;

            bKeyChanged = mFALSE;

			memset(tcg_mid_info->trsac_bitmap, 0, sizeof(tcg_mid_info->trsac_bitmap));
			tcg_core_trace(LOG_INFO, 0xd956, "<<< Start Transaction >>>, clr bitmap sz: %d", sizeof(tcg_mid_info->trsac_bitmap));

            return STS_SUCCESS;
        }
        else if (byte == TOK_EndTransaction)
        {
            result = AtomDecoding_Uint((u8*)&tmp32, sizeof(tmp32));

			tcg_core_trace(LOG_INFO, 0xef59, "result|%x, TransactionState|%x", result, mSessionManager.TransactionState);

            if (result != STS_SUCCESS)
                return STS_SESSION_ABORT;  //no status code

            if (mSessionManager.TransactionState != TRNSCTN_ACTIVE)
                return STS_SESSION_ABORT;

            dataBuf[iDataBuf++] = TOK_EndTransaction;

            if (tmp32 == 0)
            { //transaction commit
				tcg_core_trace(LOG_INFO, 0x998f, "TransactionEnd OK: Commit~~");

                dataBuf[iDataBuf++] = 0;

            #if 1
			 if (mSessionManager.Write == 0x01){
			 	
				//---------------------------------//

				if(flgs_MChnged.b.G1 == mTRUE)
				{
					tcg_nf_G1Wr(mFALSE);
				}
		
				if(flgs_MChnged.b.G2 == mTRUE)
				{
					tcg_nf_G2Wr(mFALSE);
				}
		
				if(flgs_MChnged.b.G3 == mTRUE || bKeyChanged == mTRUE)
				{
					tcg_nf_G3Wr(mFALSE);

					u32 old = mTcgStatus;
					
					if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE) && (pG3->b.mLckMbrCtrl_Tbl.val[0].done  == FALSE))
        			{
        				mTcgStatus |= MBR_SHADOW_MODE;
    				}else{
        				mTcgStatus &= (~MBR_SHADOW_MODE);
					}
					
					if(mTcgStatus!=old)
						bKeyChanged = mTRUE;
					
					LockingRangeTable_Update();

				}
                
				if(flgs_MChnged.b.DS == mTRUE){
					
					tcg_nf_DSCommit(0x00,0x280);
				}
                
                if(flgs_MChnged.b.SMBR == mTRUE){
					
					tcg_nf_SMBRCommit(0x00,0x2000);
                }

				bLockingRangeChanged = mFALSE;
				ClearMtableChangedFlag();
				
   				mSessionManager.TransactionState = TRNSCTN_IDLE;
    			//method_complete_post(req, mFALSE);
				
				tcg_core_trace(LOG_INFO, 0x0def, "TransactionEnd OK: Commit Done!!");

			 }
			 	
                //cb_transaction_ok(Begin)(req);
            #else
                WriteMtable2NAND(req);

                if (flgs_MChnged.b.G3)
                {
                #if CO_SUPPORT_AES
                    // for method_set, method_genkey, method_erase
                    TcgUpdateRawKeyList(mRawKeyUpdateList);
                #endif
                    // D4-1-4-1-1: only update LockingRange after Transaction is done!
                    LockingRangeTable_Update(); // update AES key/range setting if changed.
                }

                ClearMtableChangedFlag();
                //D5-1-3-1-1
                if ((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE) &&
                    (pG3->b.mLckMbrCtrl_Tbl.val[0].done  == FALSE))
                    mTcgStatus |= MBR_SHADOW_MODE;
                else
                    mTcgStatus &= (~MBR_SHADOW_MODE);
            #endif
                //D8-1-3-1-1
            }
            else //if(tmp32)
            { //transaction abort
				tcg_core_trace(LOG_INFO, 0xa701, "TransactionEnd OK: Abort~~");
                dataBuf[iDataBuf++] = 1;

            #if 1
				if(flgs_MChnged.b.G1 == mTRUE){

					tcg_nf_G1Rd(mTRUE,mFALSE);
					tcg_nf_G1Wr(mTRUE);
				}
		
				if(flgs_MChnged.b.G2 == mTRUE){

					tcg_nf_G2Rd(mTRUE,mFALSE);
					tcg_nf_G2Wr(mTRUE);
				}
		
				if(flgs_MChnged.b.G3 == mTRUE){

					tcg_nf_G3Rd(mTRUE,mFALSE);
					tcg_nf_G3Wr(mTRUE);
					//LockingRangeTable_Update();
				}
				if(flgs_MChnged.b.DS == mTRUE){
					
					tcg_nf_DSAbort(0x00,0x280);
				}
				if(flgs_MChnged.b.SMBR == mTRUE){
					
					tcg_nf_SMBRAbort(0x00,0x2000);
				}

				 if (bKeyChanged)
				{
				/*
					if (TCG_ACT_IN_ALL_SU())
					{
						U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
						TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
					}
				*/
				//TcgUpdateRawKeyList(mRawKeyUpdateList);

				}

				ClearMtableChangedFlag();
				bKeyChanged = mFALSE;
				
				mSessionManager.TransactionState = TRNSCTN_IDLE;
				//method_complete_post(req, mFALSE);
				
				tcg_core_trace(LOG_INFO, 0xb520, "TransactionEnd OK: Abort Done!!");
                //cb_transaction_ng(Begin)(req);
            #else
                ReadNAND2Mtable(req);

              #if CO_SUPPORT_AES
                if (bKeyChanged)
                {
                    // set TRUE only method_genkey and method_erase
                    // Use Old KEK to Unwrap Old WKey.
                    //Get_KeyWrap_KEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], WrapKEK);
                    //if (TCG_ACT_IN_OPAL())
                    //{
                    //    TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
                    //}
                    //else
                    if (TCG_ACT_IN_ALL_SU())
                    {
                        U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
                        TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
                    }

                    TcgUpdateRawKeyList(mRawKeyUpdateList);
                }
                bKeyChanged = FALSE;
              #endif
                ClearMtableChangedFlag();

                //mTcgStatus = 0;
                //if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle==manufactured_inactive)
                //    mTcgStatus |= TCG_ACTIVATED;

                //if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable==TRUE)&&(pG3->b.mLckMbrCtrl_Tbl.val[0].done==FALSE))
                //    mTcgStatus |= MBR_SHADOW_MODE;

                //LockingRangeTable_Update();
            #endif
            }
            if(req->completion == nvmet_core_cmd_done){
                tcg_prepare_respPacket_update(mFALSE);
                mSessionManager.TransactionState = TRNSCTN_IDLE;
            }

            return STS_SUCCESS;
        }

        else
        {   //Invalid token, session abort! (Test Cases 3.1.5)
            return STS_SESSION_ABORT;
        }
        //return result;
    }

}

/***********************************************************
 * invoking_session_manager()  <--  InvokingSessionManager()
 * Decode and process Method invoked at SessionManager layer
 * return 0 if success
 ***********************************************************/
ddr_code u16 invoking_session_manager(req_t *req)
{
    u64 uid;
    u32 j;
    u16 result = STS_SUCCESS;
    u8 byte, errCode = 0;

    // Invoking UID: Session Manager
	tcg_core_trace(LOG_INFO, 0x8be6, "invoking_session_manager(), methodUID.all|%x %x\n", methodUID.dw[1], methodUID.dw[0]);
	
    switch (methodUID.all)
    {
        case SM_MTD_Properties:
            result = tcg_properties(req);
            return result;

        case SM_MTD_StartSession:
			tcg_core_trace(LOG_INFO, 0x29b0, "<<< Start Session...\n");

            if (mSessionManager.state == SESSION_START)
            {   /* session already started, error */
                if (bControlSession)
                {
                    errCode = 0x02;   result = STS_NO_SESSIONS_AVAILABLE;
                }
                else
                {
                    errCode = 0x04;   result = STS_NOT_AUTHORIZED;
                }     //;
                goto SYNC_REPLY;
            }

            // TODO: clear session manager first
            // ref: Core3.2.4.1, Parameters
            //1. Start List token
            if ((byte = ChkToken()) != TOK_StartList)
            {
                errCode = 0x10; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            //2. Required parameters: HostSessionID:uint, SPID:uid, write:bool
            if (AtomDecoding_Uint((u8*)&mSessionManager.HostSessionID, sizeof(mSessionManager.HostSessionID)) != STS_SUCCESS)
            {
                errCode = 0x12; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            if (AtomDecoding_Uid2(mSessionManager.SPID.bytes) != STS_SUCCESS)
            {
                errCode = 0x14; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            if ((mSessionManager.SPID.all != UID_SP_Admin) && (mSessionManager.SPID.all != UID_SP_Locking))
            {
                errCode = 0x18;   result = STS_INVALID_PARAMETER;   goto SYNC_REPLY;
            }
			
            if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                if (pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle != manufactured)
                {   //inactive
					tcg_core_trace(LOG_INFO, 0x732f, "!!NG: LockingSP not Activate Yet\n");
                    errCode = 0x1A;   result = STS_INVALID_PARAMETER;   goto SYNC_REPLY;
                }
            }

            // Write-enabled bit
            if (AtomDecoding_Uint(&mSessionManager.Write, sizeof(mSessionManager.Write)) != STS_SUCCESS)
            {
                errCode = 0x20;     result = STS_STAY_IN_IF_SEND;       goto MNG_EXIT;
            }

            if ((mSessionManager.Write != 0x00) && (mSessionManager.Write != 0x01))
            {
                errCode = 0x22;     result = STS_INVALID_PARAMETER;     goto SYNC_REPLY;
            }

            //3. Optional parameters, must be in order: [0]HostSessionID = byte, [3]HostSigningAuthority = uid, [5]SessionTimeout = uint
            mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all = UID_Authority_Anybody;
            mSessionManager.HtChallenge[0]	= 0;

            if (mSessionManager.SPID.all == UID_SP_Admin)
				mSessionManager.sessionTimeout = pG1->b.mAdmSPInfo_Tbl.val[0].spSessionTimeout;
			else
				mSessionManager.sessionTimeout = pG2->b.mLckSPInfo_Tbl.val[0].spSessionTimeout;
            if (mSessionManager.sessionTimeout == 0)
                mSessionManager.sessionTimeout = DEF_SESSION_TIMEOUT;

            if ((byte = ChkToken()) == TOK_StartName) //3.1 Start Name token
            {
                //3.2 encoded name, 3.3 encoded value, 3.4 End Name token
                //check Tiny Token
                if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                {
                    errCode = 0x24;  result = STS_INVALID_PARAMETER;  /* need to check ... */  goto SYNC_REPLY;
                }

                if (byte == 0x00)
                {   // HostChallenge
                    u32 len;

                    result = AtomDecoding_ByteHdr(&len);
                    if (result == STS_SUCCESS)
                    {
                        if(len > CPIN_LENGTH)
                        {
                            errCode = 0x25;  result = STS_INVALID_PARAMETER; /* need to check ... */  goto SYNC_REPLY;
                        }

                        memset(mSessionManager.HtChallenge, 0, sizeof(mSessionManager.HtChallenge));
                        mSessionManager.HtChallenge[0] = (u8)len;
                        for (j = 0; j < len; j++)
                            mSessionManager.HtChallenge[j + 1] = mCmdPkt.payload[iPload++];

                        if (ChkToken() != TOK_EndName)
                        {
                            errCode = 0x30; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
                        }

                        if ((byte = ChkToken()) != TOK_StartName)
                            goto END_LIST;

                        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                        {
                            errCode = 0x32;  result = STS_INVALID_PARAMETER; /*need to check ... */  goto SYNC_REPLY;
                        }
                    }
                }

                if (byte == 0x03)
                {   // HostSigningAuthority
                    if (AtomDecoding_Uid2(mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].bytes) != STS_SUCCESS)
                    {
                        errCode = 0x34; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
                    }

                    if (ChkToken() != TOK_EndName)
                    {
                        errCode = 0x35; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
                    }
                    if ((byte = ChkToken()) != TOK_StartName)
                        goto END_LIST;

                    if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                    {
                        errCode = 0x36;  result = STS_INVALID_PARAMETER;       //need to check ...
                        goto SYNC_REPLY;
                    }
                }

                if (byte == 0x05)
                { // SessionTimeout
                    if (AtomDecoding_Uint((u8*)&mSessionManager.sessionTimeout, sizeof(mSessionManager.sessionTimeout)) != STS_SUCCESS )
                    {
                        errCode = 0x38;  result = STS_STAY_IN_IF_SEND;  goto MNG_EXIT;
                    }

                    /* if (mSessionManager.sessionTimeout == 0)
                    {
                        if ((MAX_SESSION_TIMEOUT != 0) && (G1.b.mAdmSPInfo_Tbl.val[0].spSessionTimeout != 0))
                        {  errCode = 0x39; result = STS_INVALID_PARAMETER; goto SYNC_REPLY;  }
                    } */

                    if (mSessionManager.sessionTimeout)
                    {	
						tcg_core_trace(LOG_INFO, 0x9f1d, "SessTimeout|%lld\n", mSessionManager.sessionTimeout);
						
                        if ((mSessionManager.sessionTimeout < MIN_SESSION_TIMEOUT)
                        || ((MAX_SESSION_TIMEOUT != 0) && (mSessionManager.sessionTimeout > MAX_SESSION_TIMEOUT)))
                        {
                            errCode = 0x39;
                            result = STS_INVALID_PARAMETER;       //need to check ...
                            goto SYNC_REPLY;
                        }
                    }

                    if (ChkToken() != TOK_EndName)
                    {
                        errCode = 0x3B;  result = STS_STAY_IN_IF_SEND;  goto MNG_EXIT;
                    }

                    // get EndList
                    if ((byte = ChkToken()) == TOK_StartName)
                    { // more optional parameters..., A6-3-4-2-1(3)
                        errCode = 0x3C;
                        result = STS_INVALID_PARAMETER;
                        goto SYNC_REPLY;
                    }
                }
                else
                {
                    //more optional parameters...
                    errCode = 0x3A;   result = STS_INVALID_PARAMETER;   goto END_LIST;
                }
            }

        END_LIST:
            if (byte != TOK_EndList)
            {
                errCode = 0x40; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            //4. End of Data token
            if (ChkToken() != TOK_EndOfData)
            {
                errCode = 0x42; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            //5. Status Code list
            result = chk_method_status();
            if (result == STS_SESSION_ABORT)
            {   //Control Session, A6-3-8-6-1
                result = STS_STAY_IN_IF_SEND;   errCode = 0x44;  goto MNG_EXIT;
            }

			tcg_core_trace(LOG_INFO, 0xf07e, "SP|%x AUTH|%x\n", (u8)mSessionManager.SPID.all, (u32)mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all);
            
            if (result == STS_SUCCESS)
            { //papyload check ok, check authority...  prepare SyncSession data
                #if (TCG_FS_PSID == mFALSE)  // PSID is not supported!!
                if (mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all == UID_Authority_PSID)
                    result = STS_NOT_AUTHORIZED;
                else
                #endif
                #if TCG_FS_BLOCK_SID_AUTH
                if ((mTcgStatus & SID_BLOCKED) && (mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all == UID_Authority_SID)){
                    result = STS_NOT_AUTHORIZED;
					tcg_core_trace(LOG_INFO, 0x3c29, "!!BlkSID | mTcgStatus|%x",mTcgStatus);
				}
				else
                #endif
                result = host_signing_authority_check();				

                if (result == STS_SUCCESS)
                { // SessionStart OK!
                    mSessionManager.state = SESSION_START;
                    mSessionManager.SPSessionID += 1;   //start from 0x1001;   //assigned by device, add 1 per session start-up ok
                    
					tcg_core_trace(LOG_INFO, 0x3878, "Sync Session [%x]", mSessionManager.SPSessionID);

					if(mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all != UID_Authority_Anybody)
						mSessionManager.wptr_auth++;
                    //result =  STS_SUCCESS;
/*
					#if CO_SUPPORT_AES
                    if (mSessionManager.SPID.all == UID_SP_Locking)
                    {
                        if (TCG_ACT_IN_OPAL())
                        {
                            TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
                        }
                        else if (TCG_ACT_IN_ALL_SU())
                        {
                            U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
                            TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
                        }
                    }
                    #endif
*/
                    if (mSessionManager.sessionTimeout > 0){
                        mSessionManager.bWaitSessionStart = 1;
					}
				}
                else
                { // authority check NG!
					tcg_core_trace(LOG_INFO, 0x751a, "!!NG authority check, result|%x\n", result);
                    //errCode=0x10;
                    //result = STS_INVALID_PARAMETER;
                }
            }

        SYNC_REPLY:

            //prepare payload: SyncSession data
            dataBuf[iDataBuf++] = TOK_Call;
            AtomEncoding_Int2Byte(invokingUID.bytes, sizeof(u64));

            uid = SM_MTD_SyncSession;
            AtomEncoding_Int2Byte((u8*)&uid, sizeof(u64));

            dataBuf[iDataBuf++] = TOK_StartList;

            dataBuf[iDataBuf++] = 0x84; //short atom
            *((u32 *)&dataBuf[iDataBuf]) = swap_u32(mSessionManager.HostSessionID);
            iDataBuf += sizeof(u32);

            if (result == STS_SUCCESS)
            {
                dataBuf[iDataBuf++] = 0x84; //short atom
                *((u32 *)&dataBuf[iDataBuf]) = swap_u32(mSessionManager.SPSessionID);
                iDataBuf += sizeof(u32);
            }
            else
                dataBuf[iDataBuf++] = 0x00;

            dataBuf[iDataBuf++] = TOK_EndList;
            dataBuf[iDataBuf++] = TOK_EndOfData;

        MNG_EXIT:
            if (errCode) {				
				tcg_core_trace(LOG_INFO, 0x1c8e, "!!NG: SessionManager %02x\n", errCode);
            }

			if (result != STS_SUCCESS)
			{
				mSessionManager.HtSgnAuthority[0].all = UID_Null;
				mSessionManager.HtAuthorityClass[0] = UID_Null;
				mSessionManager.wptr_auth = 0;
			}
            return result;

        default:
            return  STS_STAY_IN_IF_SEND;
    }
}




/***********************************************************
 * host_signing_authority_check()  <--  HostSigningAuthorityCheck()
 ***********************************************************/
ddr_code u16 host_signing_authority_check(void)
{
	u64 tgtCPinUid;
    u16 j, i; //, k;
    u16 authRowCnt, cpinRowCnt;
    u8 digest[SHA256_DIGEST_SIZE] = {0};  //not coding yet just for compile
    sAuthority_TblObj *pAuthTblObj;
    sCPin_TblObj *pCPinTblObj;

    // *WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison
    bool bCpinChkFail;
    // *WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison

	tcg_core_trace(LOG_INFO, 0x6b0d, "host_signing_authority_check() SPID|%x%x", mSessionManager.SPID.dw[1], mSessionManager.SPID.dw[0]);

    if (mSessionManager.SPID.all == UID_SP_Admin)
    {
        authRowCnt = pG1->b.mAdmAuthority_Tbl.hdr.rowCnt;
        pAuthTblObj = pG1->b.mAdmAuthority_Tbl.val;

        cpinRowCnt = pG1->b.mAdmCPin_Tbl.hdr.rowCnt;
        pCPinTblObj = pG1->b.mAdmCPin_Tbl.val;
    }
    else
    {
        authRowCnt = pG3->b.mLckAuthority_Tbl.hdr.rowCnt;
        pAuthTblObj = pG3->b.mLckAuthority_Tbl.val;

        cpinRowCnt = pG3->b.mLckCPin_Tbl.hdr.rowCnt;
        pCPinTblObj = pG3->b.mLckCPin_Tbl.val;
    }

    for (j = 0; j < authRowCnt; j++)
    { // row# (4) can be acquired from Table tbl;
		tcg_core_trace(LOG_INFO, 0xd689, "@%x", &pAuthTblObj[j].uid);

		tcg_core_trace(LOG_INFO, 0x336d, "j|%x, HstAuth|%x%x, Auth|%x%x", j, 
						(u32)(mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all >> 32),
						(u32)(mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all),
            			(u32)(pAuthTblObj[j].uid >> 32), (u32)(pAuthTblObj[j].uid));

        if (mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all == pAuthTblObj[j].uid)
        {
               
            mSessionManager.HtAuthorityClass[mSessionManager.wptr_auth] = pAuthTblObj[j].Class;

			tcg_core_trace(LOG_INFO, 0xdde7, "j|%x, HstAuthClass|%x%x, AuthClass|%x%x", j, 
				(u32)(mSessionManager.HtAuthorityClass[mSessionManager.wptr_auth] >> 32), 
				(u32)(mSessionManager.HtAuthorityClass[mSessionManager.wptr_auth]),
                (u32)(pAuthTblObj[j].Class >> 32), (u32)(pAuthTblObj[j].Class));
			
            if (pAuthTblObj[j].isClass == mTRUE)
                return STS_INVALID_PARAMETER;   //UID is a class

            if (pAuthTblObj[j].enabled == mFALSE) //UID disabled!
                return STS_NOT_AUTHORIZED;      //core v2.0 r2.0 5.3.4.1.4
                //STS_INVALID_PARAMETER;        //test case v1.0

            if (pAuthTblObj[j].operation == AUTH_Password)
            {
                //if(mSessionManager.HtChallenge[0]==0) //lengh=0: no password
                //    return STS_INVALID_PARAMETER;

                tgtCPinUid = pAuthTblObj[j].credential;

                for (i = 0; i<cpinRowCnt; i++)
                {
                    if (tgtCPinUid == pCPinTblObj[i].uid)
                    {
                        if (pCPinTblObj[i].tryLimit != 0)
                        { // check tries count
                            if (pCPinTblObj[i].tries >= pCPinTblObj[i].tryLimit)
                            {							
								tcg_core_trace(LOG_INFO, 0x9eaf, "!!NG Lock out -> tries count : %x ; tries : %x ; try limit %x\n", i , pCPinTblObj[i].tries,pCPinTblObj[i].tryLimit);
                                return STS_AUTHORITY_LOCKED_OUT;
                            }
                        }

                        //password checking...
                        // DBG_P(2, 3,0x82017E, 4,*(U32*)mSessionManager.HtChallenge);  //82 01 7E, "p: %08X", 4

                        // *WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                        if ((mSessionManager.HtChallenge[0] == 0) && (pCPinTblObj[i].cPin.cPin_Tag == CPIN_NULL))
                        {
                            pCPinTblObj[i].tries = 0;
                            return STS_SUCCESS;
                        }

                        if (mSessionManager.HtChallenge[0] != 0) //password length
                        {
                            if (pCPinTblObj[i].cPin.cPin_Tag == CPIN_IN_PBKDF)
                            {                            
                                // &WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length

								
								PKCS5_PBKDF2_HMAC((u32*)(&mSessionManager.HtChallenge[1]), (u32)mSessionManager.HtChallenge[0], (u32 *)pCPinTblObj[i].cPin.cPin_salt,sizeof(pCPinTblObj[i].cPin.cPin_salt)
												  ,2500,32,(u32 *)digest);				   
								/*
								HAL_PBKDF2((u32 *)(&mSessionManager.HtChallenge[1]), (u32)mSessionManager.HtChallenge[0],
                                           (u32 *)pCPinTblObj[i].cPin.cPin_salt, sizeof(pCPinTblObj[i].cPin.cPin_salt), (u32 *)digest);
								*/
								tcg_core_trace(LOG_INFO, 0x6b55, "cpin_in_pbkdf , HtChallenge len : %x",mSessionManager.HtChallenge[0]);
								/*
								for(u8 c = 1; c < 36; c++){
									tcg_core_trace(LOG_INFO, 0, "[Max set] : %x",mSessionManager.HtChallenge[c]);

								}
								*/
								/*
								for(u32 k=0;k<CPIN_LENGTH;k++)
                                {
									tcg_core_trace(LOG_INFO, 0, "HtChallenge[%x] = %x",k, *((u8 *)mSessionManager.HtChallenge+k));
									tcg_core_trace(LOG_INFO, 0, "HtChallenge_addr[%x] = %x",k, (u8 *)mSessionManager.HtChallenge+k);
								}
								*/
								
								//sec_gen_sha3_256_hash((u8 *)mSessionManager.HtChallenge+1, mSessionManager.HtChallenge[0], digest);

								tcg_core_trace(LOG_INFO, 0x1c1c, "i|%x, cpin tbl|%x , cpin hst|%x", i, *((u32*)&pCPinTblObj[i].cPin.cPin_val[0]), *((u32*)&digest[0]));
								
								/*
								for(u32 k=0;k<SHA256_DIGEST_SIZE;k++)
                                {
									tcg_core_trace(LOG_INFO, 0, "digest[%x] = %x",k, digest[k]);
								}
								*/
								
                                // *WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison
                                bCpinChkFail = mFALSE;
                                for(u32 k=0;k<SHA256_DIGEST_SIZE;k++)
                                {
                                    if (pCPinTblObj[i].cPin.cPin_val[k] != digest[k]) //password compare NG
                                    {                                
										tcg_core_trace(LOG_INFO, 0x688e, "cpin check fail");
                                        bCpinChkFail = mTRUE;
                                        break;
                                    }
                                }
                                if (bCpinChkFail)
                                {
                                    #if 0
                                    TCG_ERR_PRN("NG@: ");
                                    DBG_P(0x01, 0x03, 0x7F7F03 );  // NG@:
                                    for(j=1;j<=4;j++)
                                        D_PRINTF("%2X", pCPinTblObj[i].cPin.cPin_val[j]);
                                    D_PRINTF("...\n");

                                    //TCG_PRINTF("digest@: ");
                                    //for(j=0;j<32;j++) TCG_PRINTF("%2X",digest[j]);  TCG_PRINTF("...\n");
                                    #endif
                                    goto CPin_NG;
                                }
                                else
                                {                                
									tcg_core_trace(LOG_INFO, 0x855e, "PASS\n");
                                }
                                // &WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison

                                pCPinTblObj[i].tries = 0;
                                return STS_SUCCESS;
                            }
                            else if (pCPinTblObj[i].cPin.cPin_Tag == CPIN_IN_RAW)
                            {
								
								tcg_core_trace(LOG_INFO, 0xd037, "cpin_in_raw");
								tcg_core_trace(LOG_INFO, 0x29b7, "i|%x, cpin tbl|%x , cpin hst|%x", i, *((u32*)&pCPinTblObj[i].cPin.cPin_val[0]), *((u32*)&mSessionManager.HtChallenge[1]));

                                bCpinChkFail = mFALSE;
                                for(u32 k=0;k<CPIN_LENGTH;k++)
                                {
                         
									//tcg_core_trace(LOG_INFO, 0, "Cpin in tbl [%x] : %x , Cpin in HtChallenge [%x] : %x ", k, pCPinTblObj[i].cPin.cPin_val[k], k, mSessionManager.HtChallenge[k + 1]);
									
									if (pCPinTblObj[i].cPin.cPin_val[k] != mSessionManager.HtChallenge[k + 1]) //password compare NG
                                    {	
										tcg_core_trace(LOG_INFO, 0xdcc0, "cpin check fail");
                                        bCpinChkFail = mTRUE;
                                        break;
                                    }
                                }
                                if (bCpinChkFail)
                                {
                                    goto CPin_NG;
                                }
                                else
                                {                            
									tcg_core_trace(LOG_INFO, 0x46cf, "PASS\n");
                                }
                                pCPinTblObj[i].tries = 0;
                                return STS_SUCCESS;
                            }
							//*******PSID only************//
							else if ((mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all == UID_Authority_PSID) && (pCPinTblObj[i].cPin.cPin_Tag == CPIN_IN_DIGEST))
							{
								tcg_core_trace(LOG_INFO, 0x3bde, "cpin_in_digest");

								bCpinChkFail = mFALSE;
								
								sec_gen_sha3_256_hash((u8 *)mSessionManager.HtChallenge+1, mSessionManager.HtChallenge[0], digest);

								for(u32 k=0; k<SHA256_DIGEST_SIZE; k++)
                                {
									//tcg_core_trace(LOG_INFO, 0x27c5, "Tbl|%x , input|%x",pCPinTblObj[i].cPin.cPin_val[k],digest[k]);

									if (pCPinTblObj[i].cPin.cPin_val[k] != digest[k]) //password compare NG
                                    {
                                        bCpinChkFail = mTRUE;
                                    }
									if (bCpinChkFail)
                                	{
                                    	goto CPin_NG;
                                	}
									else
                                	{                         
										tcg_core_trace(LOG_INFO, 0x727b, "PASS\n");
                                	}
									pCPinTblObj[i].tries = 0;
									return STS_SUCCESS;
                                }	
							}
                            else
                            {
                            	/*
								 for(u32 k=0;k<CPIN_LENGTH;k++)
								 {
									 tcg_core_trace(LOG_INFO, 0, "Cpin in tbl [%x] : %x , Cpin in HtChallenge [%x] : %x ", k, pCPinTblObj[i].cPin.cPin_val[k], k, mSessionManager.HtChallenge[k + 1]);
								 }									 
                              	 */
							     goto CPin_NG;
                            }
                        }
                        // *WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                        else
                        // &WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                        {
                            return STS_INVALID_PARAMETER;
                        }
CPin_NG:
                        if (pCPinTblObj[i].tries < pCPinTblObj[i].tryLimit){
                            pCPinTblObj[i].tries++;
                        }
						tcg_core_trace(LOG_INFO, 0x2ecc, "i|%x, tryLimit|%x, tries|%x", i, pCPinTblObj[i].tryLimit, pCPinTblObj[i].tries);

                        return STS_NOT_AUTHORIZED;
                    }
                }
            }
            else if (mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all == UID_Authority_Anybody)
            { //no password (ex. Anybody)
           		if(mSessionManager.wptr_auth > 0) //Remove HtSgnAuthority when Anybody auth second time
				{
					mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all = UID_Null;
					mSessionManager.HtAuthorityClass[mSessionManager.wptr_auth] = UID_Null;
               	}

				if (mSessionManager.HtChallenge[0] <= 32)   //password length<=32
                    return STS_SUCCESS;
            }

            return STS_INVALID_PARAMETER;
        }
    }

    return STS_INVALID_PARAMETER;  //HostSigningAuthority UID nonexistent

}




ddr_code u16 Level0_Discovery(u8* buf)
{
	L0DISCOVERY_t *pl0     = (L0DISCOVERY_t *)buf;   // point of Level0 Discovery
    void          *pl0_fea = (u8 *)&pl0->FD;
    L0FEA0002_DATA_t fea0002_dat;
    u8 *pSsstc = pl0->Header.VendorSpecific;        // vendor define // alexcheck
    u32 l0_fea_len = 0;
	
	tcg_core_trace(LOG_INFO, 0x65b8, "Level0_Discovery()\n");
    memset(buf, 0, sizeof(DTAG_SZE));

    //construct L0 Discovery Header
    pl0->Header.Revision = swap_u32((u32)1);
    #if 1   // alexcheck  
    *((u32*)pSsstc + 0)  = sh_secure_boot_info.otp_secure_enabled;    // 16
    *((u32*)pSsstc + 1)  = sh_secure_boot_info.fw_secure_enable;      // 20
    *((u32*)pSsstc + 2)  = sh_secure_boot_info.loader_policy;         // 24
    *((u32*)pSsstc + 3)  = sh_secure_boot_info.maincode_policy;       // 28
    #else
    *((u32*)pSsstc)     = swap_u32(tcg_ee_PsidTag);         // 16
    *((u16*)pSsstc + 2) = swap_u16(mSgUser.range);          // 20
    *((u8*)pSsstc + 16) = (u8)bcmClientVars.otpDeploy;      // 32
    *((u8*)pSsstc + 17) = (u8)bcmClientVars.otpLifeCycle;   // 33
    *((u32*)pSsstc + 5) = swap_u32((u32)POLICY_DEV_TAG);    // 36
    #endif

    // fea0001 construct TPer Feature Descriptor
    pl0_fea = (u8 *)&pl0->FD;
    ((L0FEA0001_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0001);
    ((L0FEA0001_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0001_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0001_t) - 4;
    ((L0FEA0001_t *)pl0_fea)->fea0001.b.sync_support      = 1;
    ((L0FEA0001_t *)pl0_fea)->fea0001.b.streaming_support = 1;
    l0_fea_len = sizeof(L0FEA0001_t);

    // fea0002 construct Locking Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0002_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0002);
    #if _TCG_ != TCG_PYRITE
    ((L0FEA0002_t *)pl0_fea)->ver           = 0x1;
    #else
    ((L0FEA0002_t *)pl0_fea)->ver           = 0x2;
    #endif
    ((L0FEA0002_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0002_t) - 4;
    fea0002_dat.b.fea0002_locking_support = 1;
    #if _TCG_ != TCG_PYRITE
    fea0002_dat.b.fea0002_encryption = 1;
    #else
    fea0002_dat.b.fea0002_encryption = 0;
    #endif
    //MBRControlSet case11(D5-1-2-2-4)check MBR_Done(b5), MBR_Enabled(b4), and Locked(b2) for B4
    #if 0 // (BUILD_SSD_CUSTOMER == SSD_CUSTOMER_DELL)
        fea0002_dat.b.fea0002_mbr_not_support = 1;
    #else
        if(pG3->b.mLckMbrCtrl_Tbl.val[0].enable){
            fea0002_dat.b.fea0002_mbr_enabled = 1;
            if(pG3->b.mLckMbrCtrl_Tbl.val[0].done)  //check MRB_Done(b5) only when "enable" is TRUE
                fea0002_dat.b.fea0002_mbr_done = 1;
        }
    #endif
    if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
        fea0002_dat.b.fea0002_activated = 1;
    if(mReadLockedStatus||mWriteLockedStatus)
        fea0002_dat.b.fea0002_locked = 1;
    ((L0FEA0002_t *)pl0_fea)->fea0002.fea0002_Bdata  = fea0002_dat.fea0002_Bdata;

	tcg_core_trace(LOG_INFO, 0xbe68, "Locking Feature|%x", fea0002_dat.fea0002_Bdata);

    l0_fea_len = sizeof(L0FEA0002_t);

    // fea0003 construct Geometry Reporting Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0003_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0003);
    ((L0FEA0003_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0003_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0003_t) - 4;
    ((L0FEA0003_t *)pl0_fea)->align         = 1;
    //((L0FEA0003_t *)pl0_fea)->logical_blk_sz          = swap_u32((u32)TCG_LogicalBlockSize);
    //((L0FEA0003_t *)pl0_fea)->alignment_granularity   = swap_u64((u64)TCG_AlignmentGranularity);
	((L0FEA0003_t *)pl0_fea)->logical_blk_sz          = swap_u32((u32)(1 << host_sec_bitz));
    ((L0FEA0003_t *)pl0_fea)->alignment_granularity   = swap_u64((u64)(TCG_AlignmentGranularity >> host_sec_bitz));
    l0_fea_len = sizeof(L0FEA0003_t);

#if _TCG_ != TCG_PYRITE
    // fea0201 construct Single User Mode Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0201_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0201);
    ((L0FEA0201_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0201_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0201_t) - 4;
    ((L0FEA0201_t *)pl0_fea)->num_of_lcking_obj_support = swap_u32((u32)LOCKING_RANGE_CNT+1);
    if(mSgUser.range)   //B8: b2~Policy, b1~All, b0~Any
        ((L0FEA0201_t *)pl0_fea)->any       = 1;
    else
        ((L0FEA0201_t *)pl0_fea)->any       = 0;
    if ((mSgUser.range & 0x1ff) == 0x1ff)   //EntireLocking
        ((L0FEA0201_t *)pl0_fea)->all       = 1;
    if (mSgUser.policy)
        ((L0FEA0201_t *)pl0_fea)->policy    = 1;
    l0_fea_len = sizeof(L0FEA0201_t);

    // fea0202 construct DataStore Table Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0202_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0202);
    ((L0FEA0202_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0202_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0202_t) - 4;
    ((L0FEA0202_t *)pl0_fea)->max_num_of_datastore        = swap_u16((u16)DSTBL_MAX_NUM);
    ((L0FEA0202_t *)pl0_fea)->max_total_sz_of_datastore   = swap_u32((u32)DATASTORE_LEN);
    ((L0FEA0202_t *)pl0_fea)->datastore_sz_alignment      = swap_u32((u32)DSTBL_ALIGNMENT);
    l0_fea_len = sizeof(L0FEA0202_t);

    // fea0203 construct Opal SSC Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0203_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0203);
    ((L0FEA0203_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0203_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0203_t) - 4;
    ((L0FEA0203_t *)pl0_fea)->base_comid    = swap_u16((u16)BASE_COMID);
    ((L0FEA0203_t *)pl0_fea)->num_of_comid  = swap_u16((u16)1);
    ((L0FEA0203_t *)pl0_fea)->rng_crossing_behavior       = 0;
    ((L0FEA0203_t *)pl0_fea)->num_of_adm_auth_support     = swap_u16((u16)TCG_AdminCnt);
    ((L0FEA0203_t *)pl0_fea)->num_of_user_auth_support    = swap_u16((u16)TCG_UserCnt);
    l0_fea_len = sizeof(L0FEA0203_t);

#else  // Pyrite  _TCG_ != TCG_PYRITE
    // fea0303 construct Pyrite SSC Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0303_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0303);
    ((L0FEA0303_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0303_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0303_t) - 4;
    ((L0FEA0303_t *)pl0_fea)->base_comid    = swap_u16((u16)BASE_COMID);
    ((L0FEA0303_t *)pl0_fea)->num_of_comid  = swap_u16((u16)1);
    l0_fea_len = sizeof(L0FEA0303_t);

    // fea0404 construct Data Removal Mechanism Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0404_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0404);
    ((L0FEA0404_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0404_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0404_t) - 4;

    ((L0FEA0404_t *)pl0_fea)->data_removal_operation_processing = 0;
    //------------
    	reference Pyrite spec 3.1.1.5.2 Table 8 Supported Data Removal Mechanism
        bit0: Overwrite Data Erase
        bit1: Block Erase
        bit2: Crypto Erase
        bit3: Unmap
        bit4: Reset Write Pointers
        bit5: Vendor Specific Erase
        bit6: Reserved
        bit7: Reserved
    //----------
    
    ((L0FEA0404_t *)pl0_fea)->support_data_removal_mechanism = SupportDataRemovalMechanism;
    ((L0FEA0404_t *)pl0_fea)->data_removal_time_fm_bit1_data = swap_u16((u16)30);
    l0_fea_len = sizeof(L0FEA0404_t);
#endif // _TCG_ != TCG_PYRITE

#if TCG_FS_BLOCK_SID_AUTH
    // fea0402 construct Block SID Authentication Feature Descriptor
    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    ((L0FEA0402_t *)pl0_fea)->FeaCode       = swap_u16((u16)0x0402);
    ((L0FEA0402_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0402_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0402_t) - 4;
    if(CPinMsidCompare(CPIN_SID_IDX))
        ((L0FEA0402_t *)pl0_fea)->sid_st    = 1;
    if(mTcgStatus & SID_BLOCKED)
        ((L0FEA0402_t *)pl0_fea)->sid_block_st = 1;
    if(mTcgStatus & SID_HW_RESET)
        ((L0FEA0402_t *)pl0_fea)->hard_reset = 1;
    l0_fea_len = sizeof(L0FEA0402_t);
#endif  // TCG_FS_BLOCK_SID_AUTH

    pl0_fea = (u8 *)pl0_fea + l0_fea_len;
    pl0->Header.len = swap_u32((u32)((u32)(pl0_fea) - (u32)(pl0) - sizeof(pl0->Header.len)));

    return (u16)((u32)(pl0_fea) - (u32)(pl0) + 1);

}


/****************************************************************************
 * host_properties_parse():  <--- host_properties_parse();
 ****************************************************************************/
ddr_code u16 host_properties_parse(req_t *req, u32 *tmpHostPty, u8 *hitHostPty, u8 *hostPtyCnt, u16 *result)
{
	u8  byte, idx, *pname;
    u32 len, value;

	tcg_core_trace(LOG_INFO, 0x5321, "host_properties_parse()\n");

    //parameter check
    if(ChkToken() != TOK_StartList) return PROPERTY_PARSE_TOK_ERR;     // List1, test cases 3.1.5 , STS_SESSION_ABORT

    byte = ChkToken();

    if (byte == TOK_StartName) //Name1
    { //check host properties
        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
        {
            *result = STS_INVALID_PARAMETER; return PROPERTY_PARSE_ST_ERR;
        }

        if (byte == 0x00)
        { // Host Properties
            if (ChkToken() == TOK_StartList)
            { //L2
                while ((byte = ChkToken()) != 0xff)
                {
                    if (byte == TOK_StartName)
                    {
                        if (AtomDecoding_ByteHdr(&len) != STS_SUCCESS)   // name
                        {
                            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                        }
                        pname = &mCmdPkt.payload[iPload];
                        iPload += len;

                        if (AtomDecoding_Uint((u8*)&value, sizeof(value)) != STS_SUCCESS) // value
                        {
                            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                        }

                        (*hostPtyCnt)++;

                        for (idx = 0; idx < HostPropertiesCnt; idx++)
                        {
                            if(memcmp(pname, mHostProperties[idx].name, len) == 0)
                            {
                                if(hitHostPty[idx] == 0)
                                    hitHostPty[idx]++;
                                else
                                {
                                    *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                                }   //A6-3-4-2-1(2)

                                if(idx == 0)
                                    tmpHostPty[0] = (value < HOST_MAX_COMPKT_SZ) ? HOST_MAX_COMPKT_SZ : value;

                                else if(idx == 1)
                                    tmpHostPty[1] = (value < HOST_MAX_PKT_SZ)    ? HOST_MAX_PKT_SZ    : value;

                                else if(idx == 2)
                                    tmpHostPty[2] = (value < HOST_MAX_INDTKN_SZ) ? HOST_MAX_INDTKN_SZ : value;

                                break;
                            }
                        }

                        if (ChkToken() != TOK_EndName) //EndName2
                        {
                            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                        }
                    }
                    else if (byte == TOK_EndList) //EndList2
                        break;
                    else
                    {
                        *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                    }
                }
            }
        }
        else
        {
            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
        }

        if((byte = ChkToken()) != TOK_EndName)   //EndName1
        {
            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
        }

        byte = ChkToken();
        if(byte == TOK_StartName)
        {
            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
        }   //A6-3-4-2-1(2), encoded twice!
    }

//END_LIST:
    if(byte != TOK_EndList) return PROPERTY_PARSE_TOK_ERR;  //EndList1
    if(ChkToken() != TOK_EndOfData) return PROPERTY_PARSE_TOK_ERR;

    //status list check
    *result = chk_method_status();
    if(*result == STS_SESSION_ABORT) return PROPERTY_PARSE_TOK_ERR;

    if(*result != STS_SUCCESS) return PROPERTY_PARSE_ST_ERR;    // return result;

//cj test: Subpackets > 1 [
    iPload = ((iPload + (sizeof(u32) - 1)) / sizeof(u32)) * sizeof(u32);   // align at 4

    if ((u32)(iPload + 12 + 12) <= mCmdPkt.mPktFmt.length)
    {
        iPload += 8; // reserved  + kind
        value = swap_u32((u32)(*((u32 *)&mCmdPkt.payload[iPload])));
        iPload += sizeof(u32);
        if(value) return PROPERTY_PARSE_TOK_ERR;
    }
// ]
    return PROPERTY_PARSE_OK;

}


/****************************************************************************
 * prepare_properties_response():  <--- ();
 ****************************************************************************/
ddr_code void prepare_properties_response(u8 hostPtyCnt)
{
	u16 idx, j;
    u8  slen;

	tcg_core_trace(LOG_INFO, 0x701e, "prepare_properties_response()\n");

    //prepare payload for reply: TperPerperties data
    dataBuf [iDataBuf++] = TOK_Call;
    dataBuf [iDataBuf++] = 0xA8;
    for(j = 8; j != 0; )
        dataBuf[iDataBuf++] = invokingUID.bytes[--j];   //SessionManagerUID

    dataBuf [iDataBuf++] = 0xA8;
    for(j = 8; j != 0; )
        dataBuf[iDataBuf++] = methodUID.bytes[--j];      //PropertiesUID

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_StartList;

    for(idx = 0; idx < TperPropertiesCnt; idx++) {
        dataBuf[iDataBuf++] = TOK_StartName;

        slen = (u8)strlen(mTperProperties[idx].name);

        if(slen < 0x10)                          //Short Atom
            dataBuf[iDataBuf++] = 0xA0 + slen;   
        else                                     //Medium Atom
        {
            dataBuf[iDataBuf++] = 0xD0;
            dataBuf[iDataBuf++] = slen;
        }
        memcpy(&dataBuf[iDataBuf], mTperProperties[idx].name, slen);
        iDataBuf += slen;

        AtomEncoding_Integer((u8 *)&mTperProperties[idx].val, sizeof(mTperProperties[0].val));
        //D_PRINTF("%02X %08X\n", idx, mTperProperties[idx].val);

        dataBuf[iDataBuf++] = TOK_EndName;
    }

    dataBuf[iDataBuf++] = TOK_EndList;

    if(hostPtyCnt)
    {
        dataBuf[iDataBuf++] = TOK_StartName;
        dataBuf[iDataBuf++] = 0x00;
        dataBuf[iDataBuf++] = TOK_StartList;

        for(idx = 0; idx < HostPropertiesCnt; idx++)
        {
            dataBuf[iDataBuf++] = TOK_StartName;

            slen = (u8)strlen(mHostProperties[idx].name);

            if(slen < 0x10)                          //Short Atom
                dataBuf[iDataBuf++] = 0xA0 + slen;
            else                                     //Medium Atom
            {
                dataBuf[iDataBuf++] = 0xD0;
                dataBuf[iDataBuf++] = slen;
            }
            memcpy(&dataBuf[iDataBuf], mHostProperties[idx].name, slen);
            iDataBuf += slen;

            AtomEncoding_Integer((u8 *)&mHostProperties[idx].val, sizeof(mHostProperties[0].val));
            //souts("HostPty:");
            //soutd(mHostProperties[idx].val);
            dataBuf[iDataBuf++] = TOK_EndName;
        }

        dataBuf[iDataBuf++] = TOK_EndList;
        dataBuf[iDataBuf++] = TOK_EndName;
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;
}


/****************************************************************************
 * tcg_properties():  <--- H2TP_Properties();
 ****************************************************************************/
ddr_code u16 tcg_properties(req_t *req)
{
	u32 tmpHostPty[HostPropertiesCnt];
    u16 st = 0 , result = 0, i = 0;
    u8  hitHostPty[HostPropertiesCnt];
    u8  hostPtyCnt = 0;

	tcg_core_trace(LOG_INFO, 0x063d, "tcg_properties()\n");

    for(i = 0; i < HostPropertiesCnt; i++)
    {
        tmpHostPty[i] = mHostProperties[i].val;
        hitHostPty[i] = 0;
    }

    st = host_properties_parse(req, tmpHostPty, hitHostPty, &hostPtyCnt, &result);
	
	tcg_core_trace(LOG_INFO, 0x768f, "host_properties_parse() return st=%x",st);
	
    if(st == PROPERTY_PARSE_TOK_ERR)        goto TOKEN_ERROR;
    else if(st == PROPERTY_PARSE_ST_ERR)    goto STATUS_ERROR;

    //Success!!
    for(i = 0; i < HostPropertiesCnt; i++)
        mHostProperties[i].val = tmpHostPty[i];

    prepare_properties_response(hostPtyCnt);

    return STS_SUCCESS;

STATUS_ERROR:
    fill_no_data_token_list();
    return result;

TOKEN_ERROR:
    if(bControlSession)
        return STS_STAY_IN_IF_SEND;
    else
        return STS_SESSION_ABORT;

}

ddr_code void Supported_Security_Protocol(u8* buf)
{
    u16 i, pt;

    pt = 0;
    //Reserved
    for (i = 0; i<6; i++)
        buf[pt++] = 0;

    //List Length
    buf[pt++] = 0x00;
#if _TCG_ == TCG_EDRV
    buf[pt++] = 0x04;

    //List
    buf[pt++] = 0x00;
    buf[pt++] = 0x01;
    buf[pt++] = 0x02;
    buf[pt++] = 0xEE;   //IEEE1667
#else
    buf[pt++] = 0x03;

    //List
    buf[pt++] = 0x00;
    buf[pt++] = 0x01;
    buf[pt++] = 0x02;
#endif

    return;

}


ddr_code void ResetSessionManager()
{
    u32 i;

    mSessionManager.HostSessionID = 0;
	
    for (i = 0; i <= CPIN_LENGTH; i++)   //{Max} CPIN_LENGTH TBC
        mSessionManager.HtChallenge[i] = 0;

    for(i=0; i<AUTHORITY_CNT; i++) 
	{
		mSessionManager.HtSgnAuthority[i].all = UID_Null;
		mSessionManager.HtAuthorityClass[i] = UID_Null;
	}
	mSessionManager.wptr_auth = 0;
    mSessionManager.SPID.all = UID_Null;

    //mSessionManager.SPSessionID = 0x1001;     //assigned by TPer

    mSessionManager.state = SESSION_CLOSE;
    mSessionManager.bWaitSessionStart = 0;
    mSessionManager.sessionTimeout = 0;
    mSessionManager.sessionStartTime = 0;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
    {   //Transaction abort!!
        // ReadNAND2Mtable(req);
        if(flgs_MChnged.all32)	
        {
            tcg_tbl_recovery();  //tcg_tbl_recovery not coding yet
        }
        ClearMtableChangedFlag();
    }
    mSessionManager.TransactionState = TRNSCTN_IDLE;

#if CO_SUPPORT_AES  && _TCG_DEBUG != mTRUE
    if (TCG_ACT_IN_OPAL())
    {
        memset(OpalKDF, 0, sizeof(OpalKDF));
        memset(WrapKEK, 0, sizeof(WrapKEK));
    }
#endif
    //TODO: construct payload for response
    //dataBuf[iDataBuf++] = TOK_EndOfSession;
    
}

/***********************************************************
* invoking_tcg_table()  <-- InvokingTcgTable()
***********************************************************/
ddr_code u16 invoking_tcg_table(req_t *req)
{
    u16 result = STS_SUCCESS;
    int status;
    bool invIdIsFound = mFALSE;

    if (methodUID.all != UID_MethodID_GetACL)
    {
        //1. AccessControl checking, get ACL here
        status = tcg_access_control_check(&invIdIsFound);
        if (status == zNG)
        {
            #if 0
            return STS_NOT_AUTHORIZED;   //0x01  // DM Script v7.0, InvID or MtdID not found!
            #else
            if ((methodUID.all != UID_MethodID_Get) || (invIdIsFound == mFALSE))     //test cases 3.1.5
                return STS_NOT_AUTHORIZED;   //0x01
            else
            { // MethodID_Get && invIdIsFound
                fill_no_data_token_list();
                return STS_NOT_AUTHORIZED;  //STS_SUCCESS;   // DM Tcg TestScript v7.5 A6-1-1-3-1(2)
            }
            #endif
        }

        //2. ACE BooleanExpr checking
        status = mSessionManager.SPID.all == UID_SP_Admin ? admin_aceBooleanExpr_chk(mTRUE) : locking_aceBooleanExpr_chk(mTRUE);

		tcg_core_trace(LOG_INFO, 0xe013, "AceBoolExpChk|%2X\n", status);

        if (status == zNG)
        {
            #if 0   //BID8392 for DM new script v6.5
            return STS_NOT_AUTHORIZED;   //0x01
            #else
            if (methodUID.all != UID_MethodID_Get)     //test cases 3.1.5
                return STS_NOT_AUTHORIZED;   //0x01
            else
            {
                fill_no_data_token_list();
                return STS_SUCCESS;
            }
            #endif
        }
    }

    //3. Authority checking
    //result=AdminAuthorityCheck();
    //if(result == zNG) return zNG;

    //4. Method Operation (decode the parameters, then do the required action!)
    result = gCbfProc_method_map[ locking_for_methodUid_index(methodUID.all) ](req);	
	tcg_core_trace(LOG_INFO, 0xb653, "invoking_tcg_table() result=%08x", result);

	return result;

}


/***********************************************************
* tcg_access_control_check()  <-- TcgAccessControlCheck()
***********************************************************/
ddr_code int tcg_access_control_check(bool* invIdIsFound)
{
    //sAxsCtrl_TblObj *pAxsCtrlTbl = NULL;
    int result = zNG;
    u16 rowCnt = 0, byteCnt, i;
    u8 j;
	
    memset(aclBackup, 0, sizeof(aclBackup));
    if (FetchAxsCtrlTbl(mSessionManager.SPID.all, &byteCnt, &rowCnt) != zOK)
        goto CHK_END;   // return zNG;
	
	tcg_core_trace(LOG_INFO, 0xa572, "inv|%08x%08x mtd|%08x%08x\n", invokingUID.dw[1], invokingUID.dw[0], methodUID.dw[1], methodUID.dw[0]);
    //pAxsCtrlTbl = (sAxsCtrl_TblObj*)tcgTblBuf;

    for (i = 0; i<rowCnt; i++)
    { // search for InvokingID
        if (pAxsCtrlTbl[i].invID == invokingUID.all)
        {

            *invIdIsFound = mTRUE;
            if (pAxsCtrlTbl[i].mtdID == methodUID.all)
            {    //InvokingID/MethodID ok, store ACL list and getAclAcl uid
				
                for (j = 0; j < ACCESSCTRL_ACL_CNT; j++){
                    aclBackup[j].aclUid = pAxsCtrlTbl[i].acl[j];
					tcg_core_trace(LOG_INFO, 0xcfdb, "tbl acl|%08x%08x",(u32)(pAxsCtrlTbl[i].acl[j]>>32), (u32)(pAxsCtrlTbl[i].acl[j]));
                }
                getAclAclUid = pAxsCtrlTbl[i].getAclAcl;

                result = zOK;   // return zOK;
                goto CHK_END;
            }
        }
        else if ((pAxsCtrlTbl[i].invID >> 32) > (invokingUID.all >> 32))
        {
            //if(invIdIsFound) //out of InvokingUID search, NG
            break;
        }
    }

CHK_END:
	tcg_core_trace(LOG_INFO, 0x14f6, "AccessCtrlChk|%x\n", result);
    return result;

}



// Fetch the target AccessCtrlTbl to tcgBuf @DTCM
ddr_code int FetchAxsCtrlTbl(u64 spid, u16 *pByteCnt, u16 *pRowCnt)
{
    u32 invID_H32 = (u32)(invokingUID.all >> 32);

    //memset(&invTblHdr, 0, sizeof(invTblHdr));
    pAxsCtrlTbl = NULL;
    pInvColPty = NULL;

    if (spid == UID_SP_Admin)
    {
        switch (invID_H32) //High DW
        {
        case 0: //UID_ThisSP >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            //pAxsCtrlTbl = offsetof(tG1, b.mAdmAxsCtrl_Tbl.thisSP);
            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.thisSP;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.thisSP);
            break;
        case UID_Table >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmTbl_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmTbl_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.table;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.table);
            break;
        case UID_SPInfo >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmSPInfo_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSPInfo_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.spInfo;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.spInfo);
            break;
        case UID_SPTemplate >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmSPTemplates_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSPTemplates_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.spTemplate;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.spTemplate);
            break;
        case UID_MethodID >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmMethod_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmMethod_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.method;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.method);
            break;
        case UID_ACE >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmACE_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmACE_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.ace;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.ace);
            break;
        case UID_Authority >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmAuthority_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmAuthority_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.authority;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.authority);
            break;
        case UID_CPIN >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmCPin_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmCPin_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.cpin;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.cpin);
            break;
        case UID_TPerInfo >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmTPerInfo_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmTPerInfo_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.tperInfo;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.tperInfo);
            break;
        case UID_Template >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmTemplate_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmTemplate_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.templateTbl;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.templateTbl);
            break;
        case UID_SP >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmSP_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSP_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.sp;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.sp);
            break;
#if (_TCG_==TCG_PYRITE)
        case UID_RemovalMechanism >> 32:
            pInvokingTbl = (u8*)&pG1->b.mAdmRemovalMsm_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSP_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.removalMsm;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.removalMsm);
            break;
#endif
        default:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;
			tcg_core_trace(LOG_INFO, 0xa576, "AxsCtrl: NO InvID!!\n");

			return zNG;
        }

        //FetchTcgTbl((U8*)pAxsCtrlTbl, *pByteCnt);        //Fetch the part of AxsCtrl table
        *pRowCnt = *pByteCnt / sizeof(sAxsCtrl_TblObj);
    }
    else // if (spid == UID_SP_Locking)
    {
        switch (invID_H32) //High DW
        {
        case 0:  //UID_ThisSP >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.thisSP;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.thisSP);
            break;
        case UID_Table >> 32:
            pInvokingTbl = (u8*)&pG2->b.mLckTbl_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckTbl_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.table;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.table);
            break;
        case UID_SPInfo >> 32:
            pInvokingTbl = (u8*)&pG2->b.mLckSPInfo_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckSPInfo_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.spInfo;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.spInfo);
            break;
        case UID_SPTemplate >> 32:
            pInvokingTbl = (u8*)&pG2->b.mLckSPTemplates_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckSPTemplates_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.spTemplate;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.spTemplate);
            break;
        case UID_MethodID >> 32:
            pInvokingTbl = (u8*)&pG2->b.mLckMethod_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckMethod_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.method;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.method);
            break;
        case UID_ACE >> 32:
            pInvokingTbl = (u8*)&pG3->b.mLckACE_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckACE_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.ace;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.ace);
            break;
        case UID_Authority >> 32:
            pInvokingTbl = (u8*)&pG3->b.mLckAuthority_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckAuthority_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.authority;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.authority);
            break;
        case UID_CPIN >> 32:
            pInvokingTbl = (u8*)&pG3->b.mLckCPin_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckCPin_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.cpin;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.cpin);
            break;
#if _TCG_ != TCG_PYRITE
        case UID_SecretProtect >> 32:
            pInvokingTbl = (u8*)&pG2->b.mLckSecretProtect_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckSecretProtect_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.secretPrtct;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.secretPrtct);
            break;
#endif
        case UID_LockingInfo >> 32:
            pInvokingTbl = (u8*)&pG2->b.mLckLockingInfo_Tbl;
			pG2->b.mLckLockingInfo_Tbl.val[0].logicalBlockSize = (1 << host_sec_bitz);
			pG2->b.mLckLockingInfo_Tbl.val[0].alignmentGranularity = (TCG_AlignmentGranularity >> host_sec_bitz);

            //invokingTblSize = sizeof(pG2->b.mLckLockingInfo_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.lckingInfo;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.lckingInfo);
            break;
        case UID_Locking >> 32:
            pInvokingTbl = (u8*)&pG3->b.mLckLocking_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckLocking_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.lcking;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.lcking);
            break;
        case UID_MBRControl >> 32:
            pInvokingTbl = (u8*)&pG3->b.mLckMbrCtrl_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckMbrCtrl_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.mbrCtrl;
			
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.mbrCtrl);
            break;
        case UID_MBR >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.mbr;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.mbr);
            break;
#if _TCG_ != TCG_PYRITE
        case UID_K_AES_256_GRange_Key >> 32:
            pInvokingTbl = (u8*)&pG3->b.mLckKAES_256_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckKAES_256_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.kaes;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.kaes);
            break;
#endif
        case UID_DataStore >> 32:
        case UID_DataStore2 >> 32:
        case UID_DataStore3 >> 32:
        case UID_DataStore4 >> 32:
        case UID_DataStore5 >> 32:
        case UID_DataStore6 >> 32:
        case UID_DataStore7 >> 32:
        case UID_DataStore8 >> 32:
        case UID_DataStore9 >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.datastore;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.datastore);
            break;
        default:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;
			tcg_core_trace(LOG_INFO, 0xf264, "AxsCtrl: NO InvID!!\n");
            return zNG;
        }

        //FetchTcgTbl((U8*)pAxsCtrlTbl, *pByteCnt);        //Fetch the part of AxsCtrl table
        *pRowCnt = *pByteCnt / sizeof(sAxsCtrl_TblObj);
    }

    if (pInvokingTbl)
    {
        //memcpy(&invTblHdr, pInvokingTbl, sizeof(invTblHdr));
        pInvColPty = (sColPrty*)(pInvokingTbl + sizeof(sTcgTblHdr));
    }

    return zOK;

}


ddr_code u32 locking_for_methodUid_index(u64 mtdUid)
{
    u32 i;

    for(i = 0; i < (sizeof(method_uid_lookup)/sizeof(u64)); i++){
        if(mtdUid == method_uid_lookup[i]) return i;
    }
    return (u32)cMcMtd_illegal;
}


/***********************************************************
* admin_aceBooleanExpr_chk()  <-- AdminAceBooleanExprChk()
***********************************************************/
//
// check BooleanExpr column in ACE table with the HostSigningAuthority
// Return zOK if the authority is passed, else return zNG.
//
//
// 1. look up ACE table object from aclUid
// 2. get the booleanExpr of that ACE table object for authority check
// 3. return OK if requested authority is "Anybody", or requested authority is the same as host signed in,
//
ddr_code int admin_aceBooleanExpr_chk(bool bNotGetACL)
{
	u64 tmp64;
    u16 i, j, iAcl, iAclBk = 0;
    int sts = zNG;

    if (bNotGetACL)
    {
        for (iAcl = 0; iAcl < ACCESSCTRL_ACL_CNT; iAcl++)
        {
            if (aclBackup[iAcl].aclUid == UID_Null)
                break;

            for (i = 0; i < pG1->b.mAdmACE_Tbl.hdr.rowCnt; i++)
            {
                if (pG1->b.mAdmACE_Tbl.val[i].uid == aclBackup[iAcl].aclUid)
                {
                    aclBackup[iAcl].aclUid = UID_Null;   // reset, back it up only if it passes the "BooleanExpr" check ...

                    //check BooleanExpr
                    for (j = 0; j < ADM_ACE_BOOLEXPR_CNT; j++)
                    {
                        tmp64 = pG1->b.mAdmACE_Tbl.val[i].booleanExpr[j];
						
                        if(tmp64==UID_Null)
                        { //no more BooleansExpr list
                            break;   //for j-loop
                        }
                        else if (chkMultiAuths(tmp64,true))
                        {						
                            // backup "uid" and "col" for Method_Get/Method_Set
                            aclBackup[iAclBk].aclUid = pG1->b.mAdmACE_Tbl.val[i].uid;
                            for (j = 0; j < ACE_COLUMNS_CNT; j++)
                               aclBackup[iAclBk].aceColumns[j] = pG1->b.mAdmACE_Tbl.val[i].col[j];
                            iAclBk++;
							
                            sts = zOK;
                            break;    //for j-loop
                        }
					}
                    break;  //for i-loop
                }
            }
        }
    }
    else
    { // MethodGetACL: GetACLACL
        for (i = 0; i < pG1->b.mAdmACE_Tbl.hdr.rowCnt; i++)
        {
            if (pG1->b.mAdmACE_Tbl.val[i].uid == getAclAclUid)
            {
                //check BooleanExpr
                for (j = 0; j < ADM_ACE_BOOLEXPR_CNT; j++)
                {
                    tmp64 = pG1->b.mAdmACE_Tbl.val[i].booleanExpr[j];
                    if(tmp64 == UID_Null)
                    { //no more BooleanExpr list
                        break;      //for j-loop
                    }
                    else if (chkMultiAuths(tmp64,true))
                    {
                         sts = zOK; //return zOK;     //at least one authority from ACL list is PASS!
                         goto CHK_END;
                    }
                }
                break;  //for i-loop
            }
        }
    }
CHK_END:
    return  sts;

}

/***********************************************************
* locking_aceBooleanExpr_chk()  <-- LockingAceBooleanExprChk()
***********************************************************/
// 1. look up ACE table object from aclUid
// 2. get the booleanExpr of that ACE table object for authority check
// 3. return OK if requested authority is "Anybody", or requested authority is the same as host signed in,
//    or requrested authority by the same class (Admins or Users).
ddr_code int locking_aceBooleanExpr_chk(bool bNotGetACL)
{
    u64 tmp64;
    u16 i, j, iAcl, iAclBk = 0;
    int sts = zNG;

    if (bNotGetACL)
    {
        for (iAcl = 0; iAcl < ACCESSCTRL_ACL_CNT; iAcl++)
        {
        	if (methodUID.all == ~UID_MethodID_Erase){
				aclBackup[iAcl].aclUid = aclBackup[iAcl].aclUid | UID_FF;
				//tcg_core_trace(LOG_INFO, 0, "[Max set]aclBackup %08x %08x",(u32)(aclBackup[iAcl].aclUid>>32),(u32)(aclBackup[iAcl].aclUid));
        	}
			
            if (aclBackup[iAcl].aclUid == UID_Null){
				//tcg_core_trace(LOG_INFO, 0, "[Max set]ace1");
				break;
            }
			
            for(i = 0; i < pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
            {
            	//tcg_core_trace(LOG_INFO, 0, "[Max set]ace2 %08x %08x",(u32)(pG3->b.mLckACE_Tbl.val[i].uid>>32),(u32)(pG3->b.mLckACE_Tbl.val[i].uid));
				
                if(pG3->b.mLckACE_Tbl.val[i].uid == aclBackup[iAcl].aclUid)
                {
                	//tcg_core_trace(LOG_INFO, 0, "[Max set]aclBackup %08x %08x",(u32)(aclBackup[iAcl].aclUid>>32),(u32)(aclBackup[iAcl].aclUid));
                    aclBackup[iAcl].aclUid = UID_Null;     // reset, back it up only if it passes the "BooleanExpr" check ...

                    for (j = 0; j < LCK_ACE_BOOLEXPR_CNT; j++)
                    {
                        tmp64 = pG3->b.mLckACE_Tbl.val[i].booleanExpr[j];
                        if(tmp64 == 0) //no more BooleanExpr list
                            break;          //for j-loop

                        if(chkMultiAuths(tmp64,true))
                        {
                           // back up "uid" and "col" for Method_Get/Method_Set
                           aclBackup[iAclBk].aclUid = pG3->b.mLckACE_Tbl.val[i].uid;
                           for (j = 0; j < ACE_COLUMNS_CNT; j++)
                               aclBackup[iAclBk].aceColumns[j] = pG3->b.mLckACE_Tbl.val[i].col[j];
                           iAclBk++;

                           sts = zOK;
                           break;    //for j-loop
                        }
                    }
                    break;  //for i-loop
                }
            }
        }
    }
    else
    { //MethodGetACL: GetACLACL
        for(i = 0; i < pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
        {
            if(pG3->b.mLckACE_Tbl.val[i].uid == getAclAclUid)
            {
                //check BooleanExpr
                for (j = 0; j < LCK_ACE_BOOLEXPR_CNT; j++)
                {
                    tmp64 = pG3->b.mLckACE_Tbl.val[i].booleanExpr[j];
                    if(tmp64 == 0) //no more BooleanExpr list
                        break;          //for j-loop
                    else if(chkMultiAuths(tmp64,true))
                    {
                        sts = zOK; //return zOK;     //at least one authority from ACL list is PASS!
                        goto CHK_END;
                    }
                }
                break;  //for i-loop
            }
        }
    }
CHK_END:
    return  sts;
}

//-------------------Method Get-------------------------//

ddr_code u16 f_method_get(req_t *req)
{
    return Method_Get(req);
}

//-------------------Method Set-------------------------//
ddr_code u16 f_method_set(req_t *req)
{
    u16 result = STS_SUCCESS;
	//u32 old_mTcgStatus = mTcgStatus;
	
    if (mSessionManager.Write == 0x00)   // check write bit in start session payload
    {
        return STS_INVALID_METHOD;
    }

    method_result = result = Method_Set(req);
    if (result == STS_SUCCESS)
    {
        if (mSessionManager.TransactionState == TRNSCTN_IDLE)
        {

			if(flgs_MChnged.b.G1 == mTRUE)
			{
				tcg_nf_G1Wr(mFALSE);
			}
		
			if(flgs_MChnged.b.G2 == mTRUE)
			{

				tcg_nf_G2Wr(mFALSE);
			}
		
			if(flgs_MChnged.b.G3 == mTRUE)
			{

				tcg_nf_G3Wr(mFALSE);
			} 

			if(flgs_MChnged.b.DS == mTRUE)
			{
				Set_DataStore();
			}

			if(flgs_MChnged.b.SMBR == mTRUE)
			{
				Set_SMBR();
			}

        	if ((u32)(invokingUID.all>>32) == (u32)(UID_Locking>>32))
        	{
            	LockingRangeTable_Update();
				bLockingRangeChanged = mFALSE;
        	}
		
			if ((u32)(invokingUID.all>>32) == (u32)(UID_MBRControl>>32))
        	{
            	if ((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == mTRUE)
                	&& (pG3->b.mLckMbrCtrl_Tbl.val[0].done == mFALSE))
                	mTcgStatus |= MBR_SHADOW_MODE;
            	else
                	mTcgStatus &= (~MBR_SHADOW_MODE);

				LockingRangeTable_Update();
				tcg_init_aes_key_range();
			
				tcg_core_trace(LOG_INFO, 0x49ca, "SMBR mode check - TcgStatus|%x , Enable|%x , Done|%x", mTcgStatus, pG3->b.mLckMbrCtrl_Tbl.val[0].enable, pG3->b.mLckMbrCtrl_Tbl.val[0].done);
        	} 

		// initialize io check function
		if(mTcgStatus & MBR_SHADOW_MODE)
			tcg_io_chk_range = TcgRangeCheck_SMBR;
		else
			tcg_io_chk_range = TcgRangeCheck;
		// if MBR mode bit changed, IPC to re-config chk func for BTN wr cmd
		//if((mTcgStatus & MBR_SHADOW_MODE) != (old_mTcgStatus & MBR_SHADOW_MODE))
			//ipc_tcg_change_chkfunc_BTN_wr(mTcgStatus);
		
		ClearMtableChangedFlag(); 

        //if(flgs_MChnged.b.SMBR_Cb_Acting == mFALSE && flgs_MChnged.b.DS_Cb_Acting == mFALSE)
        	//cb_set_ok(Begin)(req);

        }
    	else  //TRNSCTN_ACTIVE
    	{

			if(flgs_MChnged.b.G1 == mTRUE)
			{
				tcg_nf_G1Wr(mTRUE);
			}
		
			if(flgs_MChnged.b.G2 == mTRUE)
			{
				tcg_nf_G2Wr(mTRUE);
			}
		
			if(flgs_MChnged.b.G3 == mTRUE)
			{
				tcg_nf_G3Wr(mTRUE);
			}

			if(flgs_MChnged.b.DS == mTRUE)
			{
				Set_DataStore();
			}

			if(flgs_MChnged.b.SMBR == mTRUE)
			{
				Set_SMBR();
			}
    	}
    }
	else  //Method Set error -> Undo tbl
	{
		//if(flgs_MChnged.b.SMBR_Cb_Acting == mFALSE && flgs_MChnged.b.DS_Cb_Acting == mFALSE){
        //cb_set_ng(Begin)(req);
		if (mSessionManager.TransactionState == TRNSCTN_IDLE)
		{
        	if(flgs_MChnged.b.G1 == mTRUE)
			{
				tcg_nf_G1Rd(mFALSE,mFALSE);
			}
		
			if(flgs_MChnged.b.G2 == mTRUE)
			{
				tcg_nf_G2Rd(mFALSE,mFALSE);
			}
		
			if(flgs_MChnged.b.G3 == mTRUE)
			{
				tcg_nf_G3Rd(mFALSE,mFALSE);
				//LockingRangeTable_Update();
			}
			
		}
		else  //TRNSCTN_ACTIVE
		{
			if(flgs_MChnged.b.G1 == mTRUE)
			{
				tcg_nf_G1Rd(mFALSE,mTRUE);
			}
		
			if(flgs_MChnged.b.G2 == mTRUE)
			{
				tcg_nf_G2Rd(mFALSE,mTRUE);
			}
		
			if(flgs_MChnged.b.G3 == mTRUE)
			{
				tcg_nf_G3Rd(mFALSE,mTRUE);
				//LockingRangeTable_Update();
			}

		}
		bKeyChanged = mFALSE;
		bLockingRangeChanged = mFALSE;
		ClearMtableChangedFlag();
    }
    return result;
}

ddr_code u16 f_method_authenticate(req_t *req)
{
    return Method_Authenticate(req);
}

ddr_code u16 f_method_revert(req_t *req)
{
    return Method_Revert(req);
}

ddr_code u16 f_method_activate(req_t *req)
{
    return Method_Activate(req);
}

ddr_code u16 f_method_random(req_t *req)
{
    return Method_Random(req);
}

ddr_code u16 f_method_reactivate(req_t *req)
{
    #if _TCG_!=TCG_PYRITE
    return Method_Reactivate(req);
    #else
    return STS_SUCCESS;  // STS_INVALID_METHOD;  // alexcheck return STS_SUCCESS or STS_INVALID_METHOD
    #endif
}

ddr_code u16 f_method_erase(req_t *req)
{
	
    #if _TCG_!=TCG_PYRITE
    return Method_Erase(req);
    #else
    return STS_SUCCESS;  // STS_INVALID_METHOD;  // alexcheck return STS_SUCCESS or STS_INVALID_METHOD
    #endif
}

ddr_code u16 f_method_illegal(req_t *req)
{
    return STS_INVALID_METHOD;
}

ddr_code u16 f_method_next(req_t *req)
{
    return Method_Next(req);
}

ddr_code u16 f_method_getAcl(req_t *req)
{
    return Method_GetACL(req);
}

ddr_code u16 f_method_genKey(req_t *req)
{
    #if _TCG_ != TCG_PYRITE
    return Method_GenKey(req);
    #else
    return STS_SUCCESS;  // STS_INVALID_METHOD;  // alexcheck return STS_SUCCESS or STS_INVALID_METHOD
    #endif
}

ddr_code u16 f_method_revertSp(req_t *req)
{
    return Method_RevertSP(req);
}

ddr_code u16 Method_Get(req_t *req)
{
    u64 tmp64;
    u32 startCol = 0, endCol = 0xffffffff, maxCol = 0;
    u32 startRow = 0, endRow = 0xffffffff, i32 = 0, maxRow = 0;
    u32 colSize, tblKind;
    sColPrty *ptColPty = NULL;
    u8 *ptTblObj = NULL;

    u16 result;
    u16 rowCnt = 0, colCnt = 0, objSize = 0;
    u8  nameValue = 0, dataType, byte;  //boolExprCnt

    //retrieve the start/end column from the payload first...
    if (ChkToken() != TOK_StartList)
        return STS_SESSION_ABORT;
    if (ChkToken() != TOK_StartList)
        return STS_SESSION_ABORT;

    if((invokingUID.dw[1]&0xfffffff0)==(UID_DataStoreType>>32))
    {
        tblKind=TBL_K_BYTE;
        startCol = ((invokingUID.all>>32)&0xff) - 1;
        maxRow = mDataStoreAddr[startCol].length - 1;
    }
    else if(invokingUID.all==UID_MBR)
    {
        tblKind = TBL_K_BYTE;
        maxRow = MBR_LEN-1;
    }
    else
        tblKind = TBL_K_OBJECT;

    //Named Value: Table (0x00), startRow (0x01), endRow (0x02), startCol (0x03), endCol (0x04)

    byte = ChkToken();
    if (byte == TOK_StartName)
    { //retrieve start/end column parameters

        if (tblKind == TBL_K_OBJECT)
        { //Object Table, retrieve startCol or endCol only
            if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (nameValue == 0x03) //startCol
            {
                if (AtomDecoding_Uint((u8*)&startCol, sizeof(startCol)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                if (byte == TOK_EndList) //no more parameters
                    goto END_LIST;
                else if (byte != TOK_StartName)
                    return STS_SESSION_ABORT;   //A6-1-5-2-1    //STS_INVALID_PARAMETER;

                if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;
            }

            if (nameValue == 0x04) //endCol
            {
                if (AtomDecoding_Uint((u8*)&endCol, sizeof(endCol)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                goto END_LIST;
            }
            else
                return STS_INVALID_PARAMETER;
        }
        else  //TBL_K_BYTE
        { //retrieve startRow or endRow only
            if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (nameValue == 0x01) //startRow
            {
                if (AtomDecoding_Uint((u8*)&startRow, sizeof(startRow)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                if (byte == TOK_EndList) //no more parameters
                    goto END_LIST;
                else if (byte != TOK_StartName)
                    return STS_SESSION_ABORT;   //A6-1-5-2-1, STS_INVALID_PARAMETER;

                if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;
            }

            if (nameValue == 0x02) //endRow
            {
                if (AtomDecoding_Uint((u8*)&endRow, sizeof(endRow)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                //TODO: check max row value ...

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                goto END_LIST;
            }
            else
                return STS_INVALID_PARAMETER;

            //if(endRow>rowSize) return zNG;
        }
    }

END_LIST:
    if ((byte != TOK_EndList) ||
        (ChkToken() != TOK_EndList) ||
        (ChkToken() != TOK_EndOfData))
        return STS_SESSION_ABORT;

    //FetchTcgTbl(pInvokingTbl, invokingTblSize);

    colCnt   = ((sTcgTblHdr *)pInvokingTbl)->colCnt;
    maxCol   = ((sTcgTblHdr *)pInvokingTbl)->maxCol;
    rowCnt   = ((sTcgTblHdr *)pInvokingTbl)->rowCnt;
    objSize  = ((sTcgTblHdr *)pInvokingTbl)->objSize;

    ptColPty = (sColPrty*)((u8*)pInvokingTbl + sizeof(sTcgTblHdr));
    ptTblObj = (u8*)ptColPty + sizeof(sColPrty) * colCnt;

    if (tblKind == TBL_K_OBJECT)
    {
        if (endCol == 0xffffffff) //no end parameters
            endCol = maxCol;

        if (endCol < startCol)
            return STS_INVALID_PARAMETER;

        if (endCol > maxCol)
            return STS_INVALID_PARAMETER;
    }
    else
    {
        if (endRow == 0xffffffff)
            endRow = maxRow;

        if (endRow < startRow)
            return STS_INVALID_PARAMETER;

        if (endRow>maxRow)
            return STS_INVALID_PARAMETER;

        //check if row size is over the buffer length,  F0 82 xx yy F1 F9 F0 00 00 00 F1, , iDataBuf=0x38
        if ((endRow - startRow + 1) > (u32)(TCG_BUF_LEN - iDataBuf - 11))
            return STS_RESPONSE_OVERFLOW;
    }

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;
	
////--------------------------------------------------------------------------////

	//method execution
    //prepare payload for response
    dataBuf[iDataBuf++] = TOK_StartList;

    if (tblKind == TBL_K_OBJECT)
    {
        u32 iRow, iCol, iList;

		tcg_core_trace(LOG_INFO, 0xace5, "M_Get sCol|%X eCol|%X\n", startCol, endCol);

        dataBuf[iDataBuf++] = TOK_StartList;

        for (iRow = 0; iRow<rowCnt; iRow++)
        {
            tmp64 = *((u64 *)ptTblObj);
            if (tmp64 == invokingUID.all)
            { //the table object is found!
                for (iCol = 0; iCol < colCnt;)
                {
                    if ((ptColPty->colNo) >= startCol &&
                        (ptColPty->colNo) <= endCol)
                    {
                        if (aceColumns_chk(ptColPty->colNo) == zOK) //check ACE columns first
                        {
                            dataBuf[iDataBuf++] = TOK_StartName;
                            AtomEncoding_Integer((u8*)&ptColPty->colNo, sizeof(ptColPty->colNo));

                            dataType = ptColPty->colType;
                            colSize = ptColPty->size;
                            if (dataType == UID_TYPE)
                            {
                                AtomEncoding_Int2Byte(ptTblObj, colSize);
                            }
                            else if (dataType == FBYTE_TYPE)
                            {
                                AtomEncoding_ByteSeq(ptTblObj, colSize);
                            }
                            else if (dataType == VALUE_TYPE)
                            {
                                AtomEncoding_Integer(ptTblObj, colSize);
                            }
                            else if (dataType == VBYTE_TYPE)
                            {
#if 1
                                sCPin *pCPIN;
                                //cjdbg, ASSERT(invokingUID.all == UID_CPIN_MSID);
                                pCPIN = (sCPin*)ptTblObj;
                                AtomEncoding_ByteHdr(CPIN_MSID_LEN); //Get MSID lengh
                                for (i32 = 0; i32 < CPIN_MSID_LEN; i32++)
                                {
                                    dataBuf[iDataBuf++] = pCPIN->cPin_val[i32];
                                }
#else
                                colSize = *ptTblObj;     //variable byte sequence, check table cell[0]
                                AtomEncoding_ByteHdr(colSize);
                                for (i32 = 1; i32 <= colSize; i32++)
                                    dataBuf[iDataBuf++] = *(ptTblObj + i32);
#endif
                            }
                            else if (dataType == LIST_TYPE)
                            {   //TODO: how to deal with "BooleanExpr?
                                colSize = *ptTblObj;     //List sequence, check table cell[0]
                                dataBuf[iDataBuf++] = TOK_StartList;
                                for (i32 = 1; i32 <= colSize; i32++)
                                    dataBuf[iDataBuf++] = ptTblObj[i32];
                                dataBuf[iDataBuf++] = TOK_EndList;
                            }
                            else if (dataType == STRING_TYPE)
                            {
                                for (i32 = 0; i32<colSize; i32++)
                                {
                                    if (*(ptTblObj + i32) == 0x00)
                                    {//string end '\0'
                                        colSize = i32;
                                        break;
                                    }
                                }

                                AtomEncoding_ByteSeq(ptTblObj, colSize);
                            }
                            else if (dataType == STRINGLIST_TYPE)
                            {
                                //TODO: need to deal with more than one list ...
                                dataBuf[iDataBuf++] = TOK_StartList;
                                for (i32 = 0; i32<colSize; i32++)
                                {
                                    if (*(ptTblObj + i32) == 0x00)
                                    {//string end '\0'
                                        colSize = i32;
                                        break;
                                    }
                                }

                                AtomEncoding_ByteSeq(ptTblObj, colSize);
                                dataBuf[iDataBuf++] = TOK_EndList;
                            }
                            else if (dataType == UIDLIST_TYPE)
                            { //mainly for "BooleanExpr"
                                colSize = sizeof(u64);
                                dataBuf[iDataBuf++] = TOK_StartList;

                                //place half-UID first (Authority_object_ref), get 1st UID
                                dataBuf[iDataBuf++] = TOK_StartName;

                                tmp64 = UID_CT_Authority_object_ref;
                                AtomEncoding_Int2Byte((u8*)&tmp64, 4);
                                AtomEncoding_Int2Byte(ptTblObj, colSize);
                                dataBuf[iDataBuf++] = TOK_EndName;

                                //get next UID
                                u32 boolExpCnt;
                                if (mSessionManager.SPID.all == UID_SP_Admin)
                                    boolExpCnt = ADM_ACE_BOOLEXPR_CNT;
                                else
                                    boolExpCnt = LCK_ACE_BOOLEXPR_CNT;
                                for (iList = 1; iList<boolExpCnt; iList++)
                                {
                                    //check if next UID exists or not
                                    tmp64 = *((u64 *)(ptTblObj + colSize * iList));
                                    if (tmp64)
                                    {
                                        dataBuf[iDataBuf++] = TOK_StartName;
                                        tmp64 = UID_CT_Authority_object_ref;
                                        AtomEncoding_Int2Byte((u8*)&tmp64, 4);
                                        AtomEncoding_Int2Byte(ptTblObj+ colSize *iList, colSize);
                                        dataBuf[iDataBuf++] = TOK_EndName;

                                        dataBuf[iDataBuf++] = TOK_StartName;
                                        tmp64 = UID_CT_boolean_ACE;
                                        AtomEncoding_Int2Byte((u8*)&tmp64, 4);
                                        dataBuf[iDataBuf++] = 0x01; //"OR"
                                        dataBuf[iDataBuf++] = TOK_EndName;
                                    }
                                    else
                                        break;
                                }
                                dataBuf[iDataBuf++] = TOK_EndList;
                            }
                            else if (dataType == UID2_TYPE)
                            { // UID list, for LockingInfo_SingleUserRange

                                colSize = 8;
								
								u8 *tmpPtr = (u8 *)(&tmp64);
	
								//tmp64 = *((u64 *)ptTblObj);
								memcpy(tmpPtr,(u8 *)ptTblObj,8);
								
                                if (tmp64 == UID_Locking)
                                { //EntireLocking
                                    AtomEncoding_Int2Byte(ptTblObj, colSize);
                                }
                                else
                                { //UID list or Empty list, get next UID
                                    dataBuf[iDataBuf++] = TOK_StartList;

                                    for (iList = 0; iList <= LOCKING_RANGE_CNT; iList++)
                                    {
                                        //check if next UID exists or not
                                        ptTblObj = (u8 *)(ptTblObj + colSize * iList);
                                        memcpy(tmpPtr,(u8 *)ptTblObj,8);
										
                                        if (tmp64)
                                        {
                                            AtomEncoding_Int2Byte((u8*)&tmp64, colSize);
                                        }
                                        else
                                            break;
                                    }
                                    dataBuf[iDataBuf++] = TOK_EndList;
                                }
                            }
                            dataBuf[iDataBuf++] = TOK_EndName;
                        }
                    }
                    else if ((ptColPty->colNo) > endCol)
                        break;

                    iCol++;
                    ptTblObj += ptColPty->size;    // next table cell (column)
                    ++ptColPty;                    // next colPty
                }

                break;
            }
            else
                ptTblObj += objSize;
        }

        dataBuf[iDataBuf++] = TOK_EndList;
    }

    else //if(tblKind==Byte)
    {
    	
		tcg_core_trace(LOG_INFO, 0xbecd, "M_Get sRow|%x eRow|%x\n", startRow, endRow);

        //---- Data Store -------------------------------------------
        if (invokingUID.all != UID_MBR)
        {
            //encode Atom Token...
            AtomEncoding_ByteHdr(endRow - startRow + 1);    //dataBuf[iDataBuf]

            #if 1
            mtdGetDs_varsMgm.singleUserMode_startColumn = startCol;
            mtdGetDs_varsMgm.dsRdLen      = endRow - startRow + 1;
            if (mtdGetDs_varsMgm.dsRdLen > TCG_BUF_LEN) {
                return STS_INVALID_PARAMETER;
            }
            mtdGetDs_varsMgm.rowBeginAdr  = startRow;
			
			Get_DataStore();
			//cb_getDs(Begin)(req);


			//return STS_SUCCESS;

            #else
            {
                U32 rlen;
                U32 LaaAddr, LaaOffAddr, rdptr;
                U32 LaaCnt;
                U16 ii;
                U16 HasBlank;
                U8  *Desptr = (U8 *)tcgTmpBuf;

                rlen = endRow - startRow + 1;              //rlen = wr remain length
                if (rlen > TCG_BUF_LEN) {
                    return STS_INVALID_PARAMETER;
                }
                rdptr = startRow + mDataStoreAddr[startCol].offset;         //rd point
                LaaAddr = rdptr / LAA_LEN;
                LaaOffAddr = rdptr % LAA_LEN;

                LaaCnt = (LaaOffAddr + rlen) / LAA_LEN;
                if ((LaaOffAddr + rlen) % LAA_LEN) LaaCnt += 1;
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt*LAA_LEN);  //clr buffer
                                                                 //EP3 todo [
                HasBlank = FALSE;
                for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                    if (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + ii].blk >= TCG_MBR_CELLS) {
                        HasBlank = TRUE;
                        break;
                    }
                }
                if (HasBlank) {
                    for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                        if (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + ii].blk < TCG_MBR_CELLS) {  //blank ?
                            DS_Read(ii, ii + 1, (U8 *)tcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                        }
                    }
                }
                else {
                    DS_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)tcgTmpBuf);  // read all
                }
                // ]
                for (i32 = 0; i32<rlen; i32++) {
                    dataBuf[iDataBuf++] = Desptr[LaaOffAddr + i32];
                }
            }
            #endif
        }
        //----- SMBR -------------------------------------------
        else
        {
        
        #if 1
            mtdGetSmbr_varsMgm.smbrRdLen      = endRow - startRow + 1;
            if (mtdGetSmbr_varsMgm.smbrRdLen > TCG_BUF_LEN) {
                return STS_INVALID_PARAMETER;
            }
            mtdGetSmbr_varsMgm.rowBeginAdr  = startRow;

            if (mtdGetSmbr_varsMgm.smbrRdLen <= 15) //Short Atom, length=0~15
                dataBuf[iDataBuf++] = 0xA0 + (u8)mtdGetSmbr_varsMgm.smbrRdLen;
            else if (mtdGetSmbr_varsMgm.smbrRdLen <= 2047)
            { //Medium Atom
                dataBuf[iDataBuf++] = 0xD0 + DW_B1(mtdGetSmbr_varsMgm.smbrRdLen);
                dataBuf[iDataBuf++] = DW_B0(mtdGetSmbr_varsMgm.smbrRdLen);
            }
            else
            { //Long Atom
                dataBuf[iDataBuf++] = 0xE2;
                dataBuf[iDataBuf++] = DW_B2(mtdGetSmbr_varsMgm.smbrRdLen);
                dataBuf[iDataBuf++] = DW_B1(mtdGetSmbr_varsMgm.smbrRdLen);
                dataBuf[iDataBuf++] = DW_B0(mtdGetSmbr_varsMgm.smbrRdLen);
            }

            if(mtdGetSmbr_varsMgm.smbrRdLen > 0){

				Get_SMBR();
				//return STS_SUCCESS;
            }
          #else
            U32 LaaAddr, LaaOffAddr, rdptr;
            U32      rlen;

            rlen = endRow - startRow + 1;              //rlen = wr remain length
            if (rlen > TCG_BUF_LEN) {
                return STS_INVALID_PARAMETER;
            }

            if (rlen <= 15) //Short Atom, length=0~15
                dataBuf[iDataBuf++] = 0xA0 + (U8)rlen;
            else if (rlen <= 2047)
            { //Medium Atom
                // dataBuf[iDataBuf++] = 0xD0 + HiByte(LoWord(rlen));
                dataBuf[iDataBuf++] = 0xD0 + DW_B1(rlen);
                // dataBuf[iDataBuf++] = LoByte(LoWord(rlen));
                dataBuf[iDataBuf++] = DW_B0(rlen);
            }
            else
            { //Long Atom
                dataBuf[iDataBuf++] = 0xE2;
                // dataBuf[iDataBuf++] = LoByte(HiWord(rlen));
                dataBuf[iDataBuf++] = DW_B2(rlen);
                // dataBuf[iDataBuf++] = HiByte(LoWord(rlen));
                dataBuf[iDataBuf++] = DW_B1(rlen);
                // dataBuf[iDataBuf++] = LoByte(LoWord(rlen));
                dataBuf[iDataBuf++] = DW_B0(rlen);
            }

            rdptr = startRow;         //rd point
            if (rlen>0) {
                U32 LaaCnt;
                U8 *Desptr = (U8 *)tcgTmpBuf;

                LaaAddr = rdptr / LAA_LEN;
                LaaOffAddr = rdptr % LAA_LEN;
                SMBR_ioCmdReq = FALSE;

                LaaCnt = (LaaOffAddr + rlen) / LAA_LEN;
                if ((LaaOffAddr + rlen) % LAA_LEN) LaaCnt += 1;
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt*LAA_LEN);  //clr buffer

				#if 1
                {
                    U16 ii;
                    U16 HasBlank;

                    HasBlank = FALSE;

                    for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                        if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + ii].blk >= TCG_MBR_CELLS) {
                            HasBlank = TRUE;
                            break;
                        }
                    }
                    if (HasBlank) {
                        for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                            if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + ii].blk < TCG_MBR_CELLS) {  //blank ?
                                SMBR_Read(ii, ii + 1, (U8 *)tcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                            }
                        }
                    }
                    else {
                        SMBR_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)tcgTmpBuf);   //read all
                    }
                }
				#else

                SMBR_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)tcgTmpBuf, NULL);    //WaitMbrRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 6 page for max transfer buffer

				#endif

                for (i32 = 0; i32<rlen; i32++) {
                    dataBuf[iDataBuf++] = Desptr[LaaOffAddr + i32];
                }
            }
          #endif
        }
    }
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;

}
/***********************************************************
* Admin table set method.
* ref. core spec 5.3.3.7
***********************************************************/
ddr_code u16 Method_Set(req_t *req)
{
    u64 tmp64;
    u32 SetParm_ColNo[eMaxSetParamCnt];
    u32 decLen;
    u32 tmp32;
    u8  setParmCnt = 0;
    u8  Where_or_Value;  //0->Where, 1->Value, 0xff->illegal
    u8  errCode = 0;  //no error

    u8 colType=0, colSize=0;
    u8 i, y;
    u8 byte;
    int zSTS = STS_SUCCESS;

	tcg_core_trace(LOG_INFO, 0xedd5, "Method_Set()");

    memset(SetParm_ColNo, 0x00, sizeof(SetParm_ColNo));

    if (ChkToken() != TOK_StartList) {  //chk 0xF0
        zSTS = STS_SESSION_ABORT; errCode = 0x01; goto exit_Method_Set;
    }
    if (ChkToken() != TOK_StartName) {  //chk 0xF2
        zSTS = STS_SESSION_ABORT; errCode = 0x02; goto exit_Method_Set;
    }
    if ((zSTS = AtomDecoding_Uint(&Where_or_Value, sizeof(Where_or_Value))) != STS_SUCCESS)
    {
        errCode = 0x03; goto exit_Method_Set;
    }

    if ((invokingUID.all & 0xfffffff0ffffffff) == UID_DataStoreType || invokingUID.all == UID_MBR)
    { // Byte table
        tmp32 = 0;  // startRow
        if (Where_or_Value == 0x00)
        { //Where
            if ((zSTS = AtomDecoding_Uint((u8*)&tmp32, sizeof(tmp32))) != STS_SUCCESS) {
                errCode = 0x06; goto exit_Method_Set;
            }

            if (ChkToken() != TOK_EndName) {  //check 0xF3
                zSTS = STS_SESSION_ABORT; errCode = 0x08; goto exit_Method_Set;
            }

            if (ChkToken() != TOK_StartName) {  //check F2 for "value"
            	iPload--;
				if(ChkToken() != TOK_EndList){
                	zSTS = STS_SESSION_ABORT; errCode = 0x0A; goto exit_Method_Set;
				}
				else
					goto without_value_name;
            }
            if ((zSTS = AtomDecoding_Uint(&Where_or_Value, sizeof(Where_or_Value))) != STS_SUCCESS) {
                errCode = 0x0B; goto exit_Method_Set;
            }
        }

        if (Where_or_Value != 0x01) {  //Values ?
            zSTS = STS_INVALID_PARAMETER; errCode = 0x0C; goto exit_Method_Set;
        }

        if ((zSTS = AtomDecoding_ByteHdr(&decLen)) != STS_SUCCESS) {
            errCode = 0x10; goto exit_Method_Set;
        }

        if ((iPload+decLen) >= mCmdPkt.mSubPktFmt.length)
        {
            zSTS = STS_INVALID_PARAMETER; errCode = 0x0D; goto exit_Method_Set;
        }

        if (Write2Mtable(req, &mCmdPkt.payload[iPload], decLen, tmp32, 0) == zNG) {
            zSTS = STS_INVALID_PARAMETER; errCode = 0x0E; goto exit_Method_Set;
        }
        iPload += decLen;
    }
    else
    { // Object table, should omit "Where" for object table
        if (Where_or_Value != 0x01) {  //Values ?
            zSTS = STS_INVALID_PARAMETER; errCode = 0x10; goto exit_Method_Set;
        }

        if (ChkToken() != TOK_StartList) {
            zSTS = STS_SESSION_ABORT; errCode = 0x11; goto exit_Method_Set;
        }

        while (ChkToken() == TOK_StartName)
        { // ColNo + Values pairs

            if (setParmCnt >= eMaxSetParamCnt)
            {
                zSTS = STS_INVALID_PARAMETER;  errCode = 0x12; goto exit_Method_Set;
            }

            //get Column#
            if ((zSTS = AtomDecoding_Uint((u8*)&SetParm_ColNo[setParmCnt], sizeof(SetParm_ColNo[0]))) != STS_SUCCESS)
            {
                zSTS = STS_INVALID_PARAMETER;  errCode = 0x13; goto exit_Method_Set;
            }

            // test case A13-4-1-4-15, check if set the same column ?
            if (setParmCnt)
            {
                for (i = 0; i < setParmCnt; i++) {
                    if (SetParm_ColNo[setParmCnt] == SetParm_ColNo[i]) {
                        zSTS = STS_INVALID_PARAMETER; errCode = 0x14; goto exit_Method_Set;
                    }
                }
            }

            // search the column# and get its properties
            for (i = 0; i < ((sTcgTblHdr*)pInvokingTbl)->colCnt; i++)
            {
                if ((pInvColPty[i].colNo) == SetParm_ColNo[setParmCnt])
                {
                    colType = pInvColPty[i].colType;
                    colSize = pInvColPty[i].size;
                    break;
                }
            }
            if(i== ((sTcgTblHdr*)pInvokingTbl)->colCnt) // not found
            {    zSTS = STS_INVALID_PARAMETER; errCode = 0x15; goto exit_Method_Set;     }

            if (aceColumns_chk(SetParm_ColNo[setParmCnt])==zNG)
            {    zSTS = STS_NOT_AUTHORIZED; errCode = 0x16; goto exit_Method_Set;    }

            if (ChkToken() == TOK_StartList)
            { // LIST_TYPE | LISTUID_TYPE
                u8 listIdx = 0;

                if (colType == LIST_TYPE)
                { // LIST_TYPE (ex: "DoneOnReset" | "LockOnReset", ListType: F0, n1, n2, ... F1)
                    while (ChkToken() != TOK_EndList)
                    {
                        iPload--;
                        if (AtomDecoding_Uint(&byte, sizeof(byte)) == STS_SUCCESS)
                        {
                            if (Write2Mtable(req, &byte, 1, SetParm_ColNo[setParmCnt], listIdx) == zNG)
                            {
                                zSTS = STS_INVALID_PARAMETER; errCode = 0x17; goto exit_Method_Set;
                            }
                            ++listIdx;
                        }
                    }
                }
                else if (colType == UIDLIST_TYPE)
                { // UIDLIST_TYPE (ex. ACE_Locking_Range1_Set_RdLocked_UID.Set[Values = [BooleanExpr = [User1_UID | User2_UID]]])

                    listIdx = 0;

                    while ((byte = ChkToken()) == TOK_StartName)
                    {
                        if ((zSTS = AtomDecoding_HUid2((u8*)&tmp32)) != STS_SUCCESS)
                        {
                            errCode = 0x18; goto exit_Method_Set;
                        }

                        switch (tmp32)
                        {
                            case (u32)UID_CT_Authority_object_ref:
                                if ((zSTS = AtomDecoding_Uid2((u8*)&tmp64)) != STS_SUCCESS) {  //Authority_object
                                    zSTS = STS_SESSION_ABORT; errCode = 0x19; goto exit_Method_Set;
                                }

                                if (ChkToken() != TOK_EndName) {
                                    zSTS = STS_SESSION_ABORT; errCode = 0x1A; goto exit_Method_Set;
                                }

                                if (Write2Mtable(req, (u8*)&tmp64, sizeof(tmp64), SetParm_ColNo[setParmCnt], listIdx) == zNG)
                                {   zSTS = STS_INVALID_PARAMETER; errCode = 0x1B; goto exit_Method_Set;  }

                                listIdx++;
                                break;

                            case (u32)UID_CT_boolean_ACE:
                                if ((zSTS = AtomDecoding_Uint((u8*)&tmp32, sizeof(tmp32))) != STS_SUCCESS)
                                {  zSTS = STS_INVALID_PARAMETER; errCode = 0x1C; goto exit_Method_Set;  }
                                if (tmp32 != 1)
                                {  // ACE_boolean != OR
                                    zSTS = STS_INVALID_PARAMETER; errCode = 0x1D; goto exit_Method_Set;
                                }
                                if (ChkToken() != TOK_EndName)
                                {  zSTS = STS_SESSION_ABORT; errCode = 0x1E; goto exit_Method_Set; }
                                break;

                            default:
                                zSTS = STS_INVALID_PARAMETER; errCode = 0x1F; goto exit_Method_Set;
                        }
                    } // while(TOK_StartName)

                    if (byte != TOK_EndList)
                    {   zSTS = STS_SESSION_ABORT; errCode = 0x20; goto exit_Method_Set; }

                }
                else //if ((colType != UIDLIST_TYPE) && (colType != LIST_TYPE))
                {
                    zSTS = STS_INVALID_PARAMETER; errCode = 0x21; goto exit_Method_Set;
                }
            }
            else
            {  // Non-ListType value
                --iPload;

                if (colType == VALUE_TYPE)
                {
                    if ((zSTS = AtomDecoding_Uint((u8*)&tmp64, sizeof(tmp64))) != STS_SUCCESS) {
                        errCode = 0x22; goto exit_Method_Set;
                    }

                    if( ((colSize == 1) && (tmp64 > 0xff)) ||
                        ((colSize == 2) && (tmp64 > 0xffff)) ||
                        ((colSize == 4) && (tmp64 > 0xffffffff)))
                    {
                        errCode = 0x23; goto exit_Method_Set;
                    }

                    if (Write2Mtable(req, (u8*)&tmp64, colSize, SetParm_ColNo[setParmCnt], 0) == zNG) {
                        zSTS = STS_INVALID_PARAMETER; errCode = 0x24; goto exit_Method_Set;
                    }
                }
                else if ((colType == VBYTE_TYPE) || (colType == STRING_TYPE))
                {
                    if ((zSTS = AtomDecoding_ByteHdr(&decLen)) != STS_SUCCESS) {
                        errCode = 0x25; goto exit_Method_Set;
                    }

                    if (Write2Mtable(req, &mCmdPkt.payload[iPload], decLen, SetParm_ColNo[setParmCnt], 0) == zNG) {
                        zSTS = STS_INVALID_PARAMETER; errCode = 0x26; goto exit_Method_Set;
                    }

                    iPload += decLen;
                }
            }

            if (ChkToken() != TOK_EndName)
            {
                zSTS = STS_SESSION_ABORT; errCode = 0x28; goto exit_Method_Set;
            }

            setParmCnt++;
        }
        iPload--;
        if (ChkToken() != TOK_EndList) {
            zSTS = STS_SESSION_ABORT; errCode = 0x29; goto exit_Method_Set;
        }

        if (setParmCnt == 0) {  // no match column
            zSTS = STS_INVALID_PARAMETER; errCode = 0x2A; goto exit_Method_Set;
        }
    }
    //    else {   zSTS=STS_INVALID_PARAMETER; errCode=0x60; goto exit_Method_Set; }

    if (ChkToken() != TOK_EndName) {
        zSTS = STS_SESSION_ABORT; errCode = 0x2B; goto exit_Method_Set;
    }
    if (ChkToken() != TOK_EndList) {
        zSTS = STS_SESSION_ABORT; errCode = 0x2C; goto exit_Method_Set;
    }
without_value_name:
    if (ChkToken() != TOK_EndOfData) {
        zSTS = STS_SESSION_ABORT; errCode = 0x2D; goto exit_Method_Set;
    }

    //status list check
    u8 *p = &mCmdPkt.payload[iPload];

	tcg_core_trace(LOG_INFO, 0x9dd1, "status list = %08x, %08x, %08x, %08x, %08x, %08x" ,*(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5));

    zSTS = chk_method_status();
    if (zSTS != STS_SUCCESS)
    {
        errCode = 0x30;
        goto exit_Method_Set;
    }

    //check locking range start or range length whether has been changed
    if (bLockingRangeChanged)
    {
        //bLockingRangeChanged = mFALSE;
        for (y = 0; y < pG3->b.mLckLocking_Tbl.hdr.rowCnt; y++)  //search row
        {
            if (invokingUID.all == pG3->b.mLckLocking_Tbl.val[y].uid)
            {
                if (LockingTbl_RangeChk(pG3->b.mLckLocking_Tbl.val[y].uid, pG3->b.mLckLocking_Tbl.val[y].rangeStart, pG3->b.mLckLocking_Tbl.val[y].rangeLength) == zNG)
                {        	
                    zSTS = STS_INVALID_PARAMETER; errCode = 0x31; goto exit_Method_Set;
                }
                break;
            }
        }
    }

exit_Method_Set:
    if (!errCode)
    {
        fill_no_data_token_list();
        return STS_SUCCESS;
    }
    else
    {
		tcg_core_trace(LOG_INFO, 0xe728, "errCode|%x, zSTS|%08x\n",errCode, zSTS);
        return zSTS;   //or STS_SESSION_ABORT
    }

}

#if (TRIM_SUPPORT == ENABLE)
extern Trim_Info TrimInfo;
extern void TrimFlush();
extern bool shr_format_fulltrim_flag;
extern bool nvme_format_trim_handle_wait();
#endif

extern bool nvme_format_stop_gc_wait();

///------------------Method_Revert----------------------
//extern Error_t VscBlockErase(void);
ddr_code u16 Method_Revert(req_t *req)
{
    u16 result = STS_SUCCESS;
	bool trim_break = false;
	u32 tcg_active = pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle;

	tcg_core_trace(LOG_INFO, 0xa606, "Method_Revert()");

    revert_varsMgm.doBgTrim = mFALSE;
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
        return STS_SESSION_ABORT;   //no definition in Test Case!!

    if (invokingUID.all == UID_SP_Admin)
    {
		tcg_core_trace(LOG_INFO, 0x0a19, "M_Revert -> AdminSP");

#if _TCG_ == TCG_EDRV
        if (mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth-1].all == UID_Authority_PSID)
            mPsidRevertCnt++;   //for WHQL test
#endif

        // TODO:
        // 1. This session SHALL be aborted.
        // 2. if ATA security feature set is not diabled, then bit 1 in word 82 in IDENTIFY DEVICE SHALL be set to 1,
        //    and bit 1 in word 85 and all bits in word 89, 90, 92 and 128 SHALL be set to appropriate values.
        // 3. LockingEnbabled bit in Locking Feature Descriptor in Level 0 SHALL be set to 0. (v)
        // 4. LifeCycleState column of the Locking SP object in the SP table, SHALL be set to 0x08. (v)
        // 5. a startup of a session to the Locking SP SHALL fail. (v)
        // 6. The PIN value of SID object in the C_PIN table is set to the value of MSID credential.
        // 7. If the Locking SP is in Manufactured state:
        //       a. All the data in the User LBA Range SHALL be cryptographically erased.
        //       b. All the values in DataStore table SHALL be the value in its OFS.
        //       c. All the values in MBR table SHALL be the value in its OFS.
        // 8. If the Locking SP is in Manufactured-Inactive state:
        //       All the data in user LBA range SHALL NOT change.
        // ?  The entire TPer SHALL revert to its OFS.

        //copy MSID to SID ... (6)
#if 1 
	tcg_nf_G1RdDefault();
	if(tcg_active == manufactured)
	{

	#if 0 //TCG_FS_PSID
    	TcgPsidVerify();
	#endif
		
		//Revert Locking SP tables
		
		//----G2----//
		tcg_nf_G2RdDefault();
		//----------//
		
		//----G3----//
		tcg_nf_G3RdDefault();

		for (int j = 1; j < TCG_MAX_KEY_CNT; j++){
	        TcgEraseKey(j); //Erase Key (7a)
		}

		TcgChangeKey(0);

	#if (TRIM_SUPPORT == ENABLE)
		while(TrimInfo.FullTrimTriggered == 1){
			cpu_msg_isr();
		}

		// Full Trim //
		TrimFlush();
		shr_format_fulltrim_flag = true;

		if(nvme_format_stop_gc_wait())
		{
			tcg_core_trace(LOG_INFO, 0x9124, "{stop_gc_wait} power loss return");
			//return; //power loss return
		}

		if(nvme_format_trim_handle_wait())
		{
			tcg_core_trace(LOG_INFO, 0x6317, "{trim_handle_wait} break, [flag]fmt: %d", gFormatInProgress);
			if(gFormatInProgress)
				trim_break = true;
			//return; //power loss return
		}
	#endif
		
		//Need to do : Clr cache、SMBR、DTS 
		/*
		extern tcg_mid_info_t *tcg_mid_info;
		for(u32 i=TCG_SMBR_LAA_START; i<=TCG_SMBR_LAA_END; i++)
		{
			tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
			tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
		}
		for(u32 i=TCG_DS_LAA_START; i<=TCG_DS_LAA_END; i++)
		{
			tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
			tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
		}
		*/
		tcg_nf_params.updt_l2p_wr = true;
		//----------//

		//cb_revert(Adm, Begin)(req);
	}	

	//Revert Admin SP tables:

	//----G1----//
	tcg_nf_G1Wr(mFALSE);
	if(tcg_active == manufactured)
	{
		tcg_nf_G2Wr(mFALSE);
		tcg_nf_G3Wr(mFALSE);
	}
	
	//----------//
	
#if 0 //TCG_FS_PSID
        TcgPsidVerify();
        //toDO: TcgFuncRequest1(MSG_TCG_NOREEP_WR);
        //TcgPsidRestore();
#endif

		
#if 0 // CO_SUPPORT_AES
        if (doBgTrim)
        {
            TrimAndBGC();
            doBgTrim = mFALSE;
        }
#endif
		
        //Clear Events for a successful invocation of the Revert method on AdminSP
    	mTcgStatus = 0;
		mTcgActivated = 0;

        SingleUser_Update();
        LockingRangeTable_Update();  //Update RangeTbl and KeyRAM
        
        ResetSessionManager();  //D10-1-1-1-1

		//method_complete_post(req,mTRUE);
#else
        if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
        {
#if (_TCG_==TCG_PYRITE)
            VscBlockErase(); // debug
#endif
            // pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)

            //Revert LockingSP tables:
            // DBG_P(1, 3, 0x8201B5);  //82 01 B5, "w2/w3"

            // clear MBR table  (7c)
            TcgFuncRequest1(MSG_TCG_SMBRCLEAR);
            TcgFuncRequest1(MSG_TCG_DSCLEAR);     //clear DataStore table

            //Revert Locking SP tables (?)
            TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G2WR);

#if CO_SUPPORT_AES  //(_TCG_!=TCG_PYRITE)  //
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgChangeKey(0);
            for (j = 1; j <= LOCKING_RANGE_CNT; j++)
                TcgEraseKey(j); //Erase Key (7a)

            bTcgKekUpdate = mTRUE;
            TcgFuncRequest1(MSG_TCG_G3WR);
            TcgFuncRequest1(MSG_TCG_CLR_CACHE);
            TcgFuncRequest1(MSG_TCG_ZERO_REBUILD);   // rebuild zero pattern

            //TrimAndBGC();   //cj added
            doBgTrim = mTRUE;
#else
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G3WR);
#endif
        }

        //Revert Admin SP tables:
#if TCG_FS_PSID
        TcgPsidVerify();
        //toDO: TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#endif
        TcgFuncRequest1(MSG_TCG_G1RDDEFAULT);
#if TCG_FS_PSID
        TcgPsidRestore();
#endif
        TcgFuncRequest1(MSG_TCG_G1WR);
        //WaitDmyWr(6);  // write 6 dummy pages for open block
#if TCG_FS_PSID
        //memset(tcg_ee_Psid, 0, sizeof(...));
        //TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#endif

#if CO_SUPPORT_AES
        if (doBgTrim)
        {
            TrimAndBGC();
            doBgTrim = mFALSE;
        }
#endif

        //Clear Events for a successful invocation of the Revert method on AdminSP
        mTcgStatus = 0;
        SingleUser_Update();
        LockingRangeTable_Update();  //Update RangeTbl and KeyRAM
        ResetSessionManager(req);  //D10-1-1-1-1
#endif
        result = STS_SUCCESS;     // STS_SESSION_ABORT;
    }
    else if (invokingUID.all == UID_SP_Locking)
    {
    
		tcg_core_trace(LOG_INFO, 0x8bbe, "M_Revert -> LockingSP");

        //TODO:
        //1. This session remains open.
        //2. if ATA security feature set is not diabled, then bit 1 in word 82 in IDENTIFY DEVICE SHALL be set to 1,
        //   and bit 1 in word 85 and all bits in word 89, 90, 92 and 128 SHALL be set to appropriate values.
        //3. LockingEnbabled bit in Locking Feature Descriptor in Level 0 SHALL be set to 0. (v)
        //4. LifeCycleState column of the Locking SP object in the SP table, SHALL be set to 0x08. (v)
        //5. a startup of a session to the Locking SP SHALL fail. (v)
        //6. x
        //7. If the Locking SP is in Manufactured state:
        //      a. All the data in the User LBA Range SHALL be cryptographically erased.
        //      b. All the values in DataStore table SHALL be the value in its OFS.
        //      c. All the values in MBR table SHALL be the value in its OFS.
        //8. If the Locking SP is in Manufactured-Inactive state:
        //      a. All the data in user LBA range SHALL NOT change.
        //      b. All the values in DataStore table SHALL be the value in its OFS.
        //      c. All the values in the MBR table SHALL be the value in its OFS.
        //?  The SP itself SHALL revert to its OFS.
        
		if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured){

	#if TCG_FS_PSID
    	//TcgPsidVerify();
	#endif

		//Revert Locking SP tables
		
		//----G2----//
		tcg_nf_G2RdDefault();
		
		tcg_nf_G2Wr(mFALSE);
		//----------//

		//----G3----//
		tcg_nf_G3RdDefault();
		
		for (int j = 1; j < TCG_MAX_KEY_CNT; j++){
	        TcgEraseKey(j); //Erase Key (7a)
		}

		TcgChangeKey(0);

		//Need to do : Clr cache、SMBR、DTS
		/*
		extern tcg_mid_info_t *tcg_mid_info;
		for(u32 i=TCG_SMBR_LAA_START; i<=TCG_SMBR_LAA_END; i++)
		{
			tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
			tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
		}
		for(u32 i=TCG_DS_LAA_START; i<=TCG_DS_LAA_END; i++)
		{
			tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
			tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
		}
		*/
		tcg_nf_params.updt_l2p_wr = true;
		
		tcg_nf_G3Wr(mFALSE);
		//----------//
		
		//----G1----//
		pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;

		tcg_nf_G1Wr(mFALSE);
		//----------//
		
		//Clear Events for a successful invocation of the Revert method on AdminSP
		#if TCG_FS_BLOCK_SID_AUTH
			mTcgStatus = 0;
			mTcgActivated = 0;
    		mTcgStatus &= (SID_BLOCKED + SID_HW_RESET); //no Clear Events!
		#else
    		mTcgStatus = 0;
			mTcgActivated = 0;
		#endif
		
    	SingleUser_Update();
   		LockingRangeTable_Update();

		//ResetSessionManager();
		
		//method_complete_post(req,mTRUE);
		//cb_revert(Lck, Begin)(req);  //Max modify
		}

        

#if 0
        if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle==manufactured)
        {
#if (_TCG_==TCG_PYRITE)
            VscBlockErase(); // debug
#endif
            //Revert LockingSP tables:
            // DBG_P(1, 3, 0x8201B5);  //82 01 B5, "w2/w3"
#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE  //
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgChangeKey(0);
            for (j = 1; j <= LOCKING_RANGE_CNT; j++)
                TcgEraseKey(j); //Erase Keys (7a)

            TcgFuncRequest1(MSG_TCG_G3WR);
            TcgFuncRequest1(MSG_TCG_CLR_CACHE);
            TcgFuncRequest1(MSG_TCG_ZERO_REBUILD);   // rebuild zero pattern
#else
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G3WR);
#endif
            //clear MBR table  (7c)
            TcgFuncRequest1(MSG_TCG_SMBRCLEAR);
            TcgFuncRequest1(MSG_TCG_DSCLEAR);       //clear DataStore table (7b)

            TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G2WR);

            pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)
            TcgFuncRequest1(MSG_TCG_G1WR);
            //WaitDmyWr(6);  // write 6 dummy pages for open block
#if CO_SUPPORT_AES
            TrimAndBGC();
#endif
#if TCG_FS_BLOCK_SID_AUTH
            mTcgStatus &= (SID_BLOCKED + SID_HW_RESET);    //not Clear Events!
#else
            mTcgStatus = 0;
#endif
            SingleUser_Update();
            LockingRangeTable_Update();
            // this session remains open... x ResetSessionManager();  //D10-1-1-1-1
        }
#endif
        result = STS_SUCCESS;
    }
    else
        result = STS_INVALID_PARAMETER;

#if TCG_TBL_HISTORY_DESTORY
    if(result == STS_SUCCESS){
        TcgFuncRequest1(MSG_TCG_TBL_HIST_DEST);
    }
#endif

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

	if(trim_break)
		return STS_FAIL;
	else
    	return result;

}

///------------------Method_RevertSP----------------------
ddr_code u16 Method_RevertSP(req_t *req)
{
    //sWrappedKey tmpKey;
    u32 nameValue = 0;
    u16 result;
    u8  data;
	u8  i;
	bool trim_break = false;

    //TODO:
    //0. If 'KeepGlobalRangeKey' is 1, and Locking GlobalRange is both read-locked and write-locked, fail with FAIL.
    //1. This session SHALL be aborted.
    //2. if ATA security feature set is not diabled, then bit 1 in word 82 in IDENTIFY DEVICE SHALL be set to 1,
    //   and bit 1 in word 85 and all bits in word 89, 90, 92 and 128 SHALL be set to appropriate values.
    //3. LockingEnbabled bit in Locking Feature Descriptor in Level 0 SHALL be set to 0. (v)
    //4. LifeCycleState column of the Locking SP object in the SP table, SHALL be set to 0x08. (v)
    //5. a startup of a session to the Locking SP SHALL fail. (v)
    //6. x
    //7. except GlobalRange (if 'KeepGlobalRangeKey'=1), all of the user ranges SHALL be cryptographically erased.
    //8. Whichever 'KeepGlobalRangeKey' is, all the values in DataStore table SHALL be the value in its OFS.
    //9. Whichever 'KeepGlobalRangeKey' is, all the values in MBR table SHALL be the value in its OFS.
    //?  The SP itself SHALL revert to its OFS.
	
	tcg_core_trace(LOG_INFO, 0x117b, "Method_RevertSP()");

    // DBG_P(1, 3, 0x82012E);  //82 01 2E, "[F]Method_RevertSP"
    revertSp_varsMgm.keepGlobalRangeKey = 0;
    //parameter check
    if (ChkToken() != TOK_StartList)    // test cases 3.1.5
        return STS_SESSION_ABORT;

    //retrieve parameter 'KeepGlobalRangeKey'
    data = ChkToken();
    if (data == TOK_StartName)
    {
        if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            return STS_INVALID_PARAMETER;

        if (nameValue == 0x060000)
        {
            if (AtomDecoding_Uint(&revertSp_varsMgm.keepGlobalRangeKey, sizeof(revertSp_varsMgm.keepGlobalRangeKey)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if ((revertSp_varsMgm.keepGlobalRangeKey!=1)&&(revertSp_varsMgm.keepGlobalRangeKey!=0))
                return STS_INVALID_PARAMETER;

            if (revertSp_varsMgm.keepGlobalRangeKey == 1)
            {
                if(pG3->b.mLckLocking_Tbl.val[0].readLocked && pG3->b.mLckLocking_Tbl.val[0].writeLocked)
                    return STS_FAIL;    //(0)
                #if 0 //ndef alexcheck
                Nvme_Security_FlushAll();
                #endif
            }
        }

        if (ChkToken() != TOK_EndName)
            return STS_INVALID_PARAMETER;

        data = ChkToken();
    }

    if (data != TOK_EndList)            // test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    // test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if(result!=STS_SUCCESS)
        return result;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
        return STS_SESSION_ABORT;   //no definition in Test Case!!

    // DBG_P(2, 3, 0x8201B8, 1, revertSp_varsMgm.keepGlobalRangeKey);  //82 01 B8, "M_RevertSP: keepGlobalRangeKey[%X]", 1
#if 1

	//Revert Locking SP tables
	
	//----G2----//
	tcg_nf_G2RdDefault();
	
	tcg_nf_G2Wr(mFALSE);
	//----------//

	if (revertSp_varsMgm.keepGlobalRangeKey != 1){
		//Not thing to do
	}else{
		//backup Global Range key & KEK salt
    	memcpy(revertSp_varsMgm.backup_GR_key1, pG3->b.mLckKAES_256_Tbl.val[0].key1, 40);
    	memcpy(revertSp_varsMgm.backup_GR_key2, pG3->b.mLckKAES_256_Tbl.val[0].key2, 40);
		memcpy(revertSp_varsMgm.backup_GR_KEKsalt, pG3->b.mKEKsalt[0].salt, 32);
	}	
	
	//----G3----//
	tcg_nf_G3RdDefault();

	for (i = 1; i <TCG_MAX_KEY_CNT; i++){
		TcgEraseKey(i); //Erase Key (7a)
	}

	if (revertSp_varsMgm.keepGlobalRangeKey != 1){
		TcgChangeKey(0);
	}else{
		TcgRestoreGlobalKey();
		bKeyChanged = mTRUE;
	}


	//Need to do : Clr cache、SMBR、DTS 
	/*
	extern tcg_mid_info_t *tcg_mid_info;
	for(u32 i=TCG_SMBR_LAA_START; i<=TCG_SMBR_LAA_END; i++)
	{
		tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
		tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
	}
	for(u32 i=TCG_DS_LAA_START; i<=TCG_DS_LAA_END; i++)
	{
		tcg_mid_info->l2p_tbl[i] = UNMAP_PDA;
		tcg_mid_info->l2p_tbl[i + TCG_LAST_USE_LAA] = UNMAP_PDA;
	}
	*/
	tcg_nf_params.updt_l2p_wr = true;

	tcg_nf_G3Wr(mFALSE);
	
	//----------//
	
	//----G1----//
	pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;
	
	tcg_nf_G1Wr(mFALSE);
	//----------//

	if (revertSp_varsMgm.keepGlobalRangeKey != 1){
		
        #if (TRIM_SUPPORT == ENABLE)
		while(TrimInfo.FullTrimTriggered == 1){
			cpu_msg_isr();
		}

		// Full Trim //
			TrimFlush();
			shr_format_fulltrim_flag = true;

			if(nvme_format_stop_gc_wait())
			{
				tcg_core_trace(LOG_INFO, 0x91f5, "{stop_gc_wait} power loss return");
				//return; //power loss return
			}

			if(nvme_format_trim_handle_wait())
			{
				tcg_core_trace(LOG_INFO, 0xe69c, "{trim_handle_wait} break, [flag]fmt: %d", gFormatInProgress);
				if(gFormatInProgress)
					trim_break = true;
				//return; //power loss return
			}
		#endif

	}
	
	#if TCG_FS_BLOCK_SID_AUTH
		mTcgStatus = 0;
		mTcgActivated = 0;
    	mTcgStatus &= (SID_BLOCKED + SID_HW_RESET); //no Clear Events!
	#else
   		mTcgStatus = 0;
		mTcgActivated = 0;
	#endif

	SingleUser_Update();
    LockingRangeTable_Update();

	ResetSessionManager();  //D10-1-1-1-1

	//method_complete_post(req,mTRUE);
	
    //cb_revertsp(Begin)(req);
#else
#if (_TCG_==TCG_PYRITE)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        VscBlockErase();
#endif

    //Revert Locking SP tables (?)
    // DBG_P(1, 3, 0x8201B9);  //82 01 B9, "M_RevertSP: w2/w3"

#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE

    TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);

    //Erase Keys (7)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgChangeKey(0);
    else
    { //restore key 0
        //memcpy(&(pG3->b.mWKey[0]), &tmpKey, sizeof(sWrappedKey));
        Tcg_UnWrapDEK(0, WrapKEK, TO_MTBL_KEYTBL);

        // eDrive is at single user mode, global raneg should be always at unwrap state
        //memcpy(mRawKey[0].key, pG3->b.mWKey[0].key, sizeof(mRawKey[0].key));
        //mRawKey[0].state = pG3->b.mWKey[0].state;
    }
    for (U8 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j);

    bTcgKekUpdate = TRUE;
    TcgFuncRequest1(MSG_TCG_G3WR);

    if(revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgFuncRequest1(MSG_TCG_CLR_CACHE);

    TcgFuncRequest1(MSG_TCG_ZERO_REBUILD);   // rebuild zero pattern
#else
    TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
    TcgFuncRequest1(MSG_TCG_G3WR);
#endif

    //clear MBR table  (9)
    TcgFuncRequest1(MSG_TCG_SMBRCLEAR);
    TcgFuncRequest1(MSG_TCG_DSCLEAR);       //clear DataStore table (8)

    TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);
    TcgFuncRequest1(MSG_TCG_G2WR);

    pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)
    TcgFuncRequest1(MSG_TCG_G1WR);   //WaitG1Wr();
    //WaitDmyWr(6);  // write 6 dummy pages for open block
#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE  //
   if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TrimAndBGC();
#endif
#if TCG_FS_BLOCK_SID_AUTH
    mTcgStatus &= (SID_BLOCKED + SID_HW_RESET); //no Clear Events!
#else
    mTcgStatus = 0;
#endif
    SingleUser_Update();
    LockingRangeTable_Update();
    //this session is abort by the status code of "STS_SUCCESS_THEN_ABORT"
    // x ResetSessionManager();  //D10-1-1-1-1

    //dataBuf[iDataBuf++]=TOK_StartList;
    //dataBuf[iDataBuf++]=TOK_EndList;
    //dataBuf[iDataBuf++]=TOK_EndOfData;

#if TCG_TBL_HISTORY_DESTORY
    TcgFuncRequest1(MSG_TCG_TBL_HIST_DEST);
#endif
#endif  // #if 0
    if(trim_break)
		return STS_FAIL;
	else
    	return STS_SUCCESS_THEN_ABORT; //??? STS_SESSION_ABORT;   //D10-3-3-1-1

}

ddr_code bool chkMultiAuths(u64 auth, bool chkClass)
{
	u8 idx;
	if(UID_Authority_Anybody==auth)
		return true;
	for(idx=0; idx < mSessionManager.wptr_auth; idx++) 
	{
		if(mSessionManager.HtSgnAuthority[idx].all==auth)
			return true;
		if(chkClass)
			if(mSessionManager.HtAuthorityClass[idx]==auth)
				return true;
	}
	return false;
}


//ThisSP.Authenticate()
ddr_code u16 Method_Authenticate(req_t *req)
{
    UID64 authority;
    u32 len = 0;
    u16 result;
    u8  byte, j, *pt = NULL;
	u8  repeatFlag = 0;
	bool wptr_updt = false;

	tcg_core_trace(LOG_INFO, 0xa891, "Method_Authenticate()");

    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //retrieve parameter 'Authority'
    if (AtomDecoding_Uid2(authority.bytes) != STS_SUCCESS)
        return STS_INVALID_PARAMETER;

    byte = ChkToken();
    if (byte == TOK_StartName)
    {
        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
            return STS_INVALID_PARAMETER;

        if (byte == 0x00)
        {// param: Proof (HostChallenge)
            result = AtomDecoding_ByteHdr(&len);
            if (result == STS_SUCCESS)
            {
                if (len>CPIN_LENGTH)
                    return STS_INVALID_PARAMETER;

                pt = &mCmdPkt.payload[iPload];
                iPload += len;
                byte = ChkToken();
            }
        }

        if (byte != TOK_EndName)
            return STS_INVALID_PARAMETER;

        byte = ChkToken();
    }

    if (byte == TOK_EndList)
        goto END_LIST;
    else  //NG within parameters
        return STS_SESSION_ABORT;   //A6-1-5-2-1, STS_INVALID_PARAMETER;

END_LIST:
    if (ChkToken() != TOK_EndOfData)
        return STS_STAY_IN_IF_SEND;

    //status list check
    result = chk_method_status();
    if(result!=STS_SUCCESS)
        return result;

    //method execution:
    // only suuport "Anybody + one authority" in a session
    if (authority.all == UID_Authority_Anybody)
        result = STS_SUCCESS;
    else
    { //authority!=anybody
    #if 1 //check if authority is found and not "IsClass"
        if (mSessionManager.SPID.all == UID_SP_Admin)
        {
            for(j=0;j<pG1->b.mAdmAuthority_Tbl.hdr.rowCnt;j++)
            { // row# (4) can be acquired from Table tbl;
                if(authority.all == pG1->b.mAdmAuthority_Tbl.val[j].uid)
                {
                    if(pG1->b.mAdmAuthority_Tbl.val[j].isClass == mTRUE)
                        return STS_INVALID_PARAMETER;   //UID is a class
                    else
                        goto CHECK_SIGN;
                }
            }
            return STS_INVALID_PARAMETER;   //no authority found!
        }
        else
        {
            for(j=0;j<pG3->b.mLckAuthority_Tbl.hdr.rowCnt;j++)
            { // row# (4) can be acquired from Table tbl;
                if(authority.all==pG3->b.mLckAuthority_Tbl.val[j].uid)
                {
                    if(pG3->b.mLckAuthority_Tbl.val[j].isClass==mTRUE)
                        return STS_INVALID_PARAMETER;   //UID is a class
                    else
                        goto CHECK_SIGN;
                }
            }
            return STS_INVALID_PARAMETER;   //no authority found!
        }
    CHECK_SIGN:
    #endif
        //auth0 = mSessionManager.HtSgnAuthority.all;     //backup original authority
#if 1 //temperarily marked off for eDrive test ...
        //if ((auth0 != UID_Authority_Anybody) && (auth0 != authority.all))
        if(mSessionManager.wptr_auth >= AUTHORITY_CNT) 
		{
			tcg_core_trace(LOG_INFO, 0x9a6b,"[TCG] NG!! wptr_auth>AUTHORITY_CNT - wptr_auth|%x", mSessionManager.wptr_auth);	
            result = STS_NOT_AUTHORIZED;    //return STS_SUCCESS with result=FALSE
		}
		else
#endif
#if (TCG_FS_PSID == FALSE)  // PSID is not supported!!
        if (authority.all == UID_Authority_PSID)
            result = STS_NOT_AUTHORIZED;
        else
#endif
#if TCG_FS_BLOCK_SID_AUTH
        if ((mTcgStatus&SID_BLOCKED) && (authority.all == UID_Authority_SID))
            result = STS_NOT_AUTHORIZED;
        else
#endif
        {
			repeatFlag = chkMultiAuths(authority.all, true);
            mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth].all = authority.all;
            memset(mSessionManager.HtChallenge, 0, sizeof(mSessionManager.HtChallenge));
            mSessionManager.HtChallenge[0] = (u8)len;
            memcpy(&mSessionManager.HtChallenge[1], pt, len);
            result = host_signing_authority_check();
			mSessionManager.wptr_auth++;
			wptr_updt = true;
        }
    }

    dataBuf[iDataBuf++] = TOK_StartList;

    // DBG_P(3, 3, 0x8201BA, 4, (U32)authority.all, 2, result);  //82 01 BA, "M_Authenticate: %08X %04X", 4 2

    if (result == STS_SUCCESS)
    {
        dataBuf[iDataBuf++] = 0x01;   //TRUE;
#if 0 //CO_SUPPORT_AES
        if (mSessionManager.SPID.all == UID_SP_Locking)
        {
            if (TCG_ACT_IN_OPAL())
            {
                TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
            }
            else if (TCG_ACT_IN_ALL_SU())
            {
                if (mSessionManager.SPID.all == UID_SP_Locking)
                {
                    u8 rngNo = (u8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
                    TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
                }
            }
        }
#endif
		if(repeatFlag)
		{
			mSessionManager.HtSgnAuthority[--(mSessionManager.wptr_auth)].all = UID_Null;
			mSessionManager.HtAuthorityClass[mSessionManager.wptr_auth] = UID_Null;
		}
    }
    else
    { // failed, restore authority
		if(wptr_updt)
		{
			mSessionManager.HtSgnAuthority[--(mSessionManager.wptr_auth)].all = UID_Null;
			mSessionManager.HtAuthorityClass[mSessionManager.wptr_auth]	= UID_Null;
		}
        dataBuf[iDataBuf++] = 0x00;   //FALSE;
    }
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;


	if(result == STS_AUTHORITY_LOCKED_OUT)
	{	
		return result;
	}
	
    return STS_SUCCESS;
}

ddr_code u16 Method_Random(req_t *req)
{
    u32 count, i, tmp[32/4];
    u16 result;

	tcg_core_trace(LOG_INFO, 0x0ab8, "Method_Random()");
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //retrieve parameter 'Count'
    if (AtomDecoding_Uint((u8*)&count, sizeof(count)) != STS_SUCCESS)
        return STS_INVALID_PARAMETER;

    if (count > 0x20) {                   // OPAL spec 4.2.6.1 , The TPer SHALL support Count parameter values less than or equal to 32.
        return STS_INVALID_PARAMETER;
    }

    if (count>(u32)(TCG_BUF_LEN - iDataBuf - 11))     //buffersize limitation: F0 82 xx yy F1 F9 F0 00 00 00 F1, iDataBuf=0x38
        return STS_RESPONSE_OVERFLOW;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)  //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
        return STS_SESSION_ABORT;   //no definition in Test Case!!

    // DBG_P(2, 3, 0x8201BB, 4, count);  //82 01 BB, "M_RND: %X", 4

    dataBuf[iDataBuf++] = TOK_StartList;
    AtomEncoding_ByteHdr(count);

    sec_gen_rand_number((u8*)tmp,count);
    
    //HAL_Gen_Key((u32*)tmp, count);

    for (i = 0; i<count; i++)
        dataBuf[iDataBuf++] = ((u8*)tmp)[i];

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;

}

//-------------------Activate-------------------------
ddr_code u16 Method_Activate(req_t *req)
{
#if _TCG_ != TCG_PYRITE
    u64 tmp64;
    u32 nameValue, tmp32, DSTblSize[DSTBL_MAX_NUM];
    u32 totalsize = 0;
    u16 sgUserRange = 0;

    u8  sgUserCnt = 0, sgUserPolicy = 1;    //admin has ownership
    u8  i;
#endif
    u16 result = 0;
    u8  tmp8, cnt = 0, errCode = 0;

	tcg_core_trace(LOG_INFO, 0xc057, "Method_Activate()\n");

    //cj removed: redundent? [
    //if(mSessionManager.HtSgnAuthority.all==UID_Authority_PSID)
    //    return STS_NOT_AUTHORIZED;
    //]
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
    {
        errCode = 0x10;  result = STS_SESSION_ABORT;  goto MTD_EXIT;
    }

    //retrieve parameter 'DataStoreTblSizes'
    tmp8 = ChkToken();
#if _TCG_ != TCG_PYRITE    //SingleUserMode and Additional DataStore
    if (tmp8 == TOK_StartName)
    {
        if (invokingUID.all != UID_SP_Locking)   //cj added for DM test
        {
            errCode = 0x50;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
        }

        if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
        {
            errCode = 0x20;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
        }

        if (nameValue == 0x060000) //'SingleUserSelectionList'
        {
            if (ChkToken() == TOK_StartList)
            {   // Locking table objects list
                tmp8 = 0;
                while (ChkToken() != TOK_EndList)
                {
                    iPload--;

                    if (AtomDecoding_Uid2((u8 *)&tmp64) != STS_SUCCESS)
                    {
                        errCode = 0x30;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
                    }

                    //check if it is in the locking table
                    for (i = tmp8; i < pG3->b.mLckLocking_Tbl.hdr.rowCnt; i++)
                    {
                        if (pG3->b.mLckLocking_Tbl.val[i].uid == tmp64)
                        {
                            //mSgUser.range[mSgUser.cnt]=i;
                            sgUserRange |= (0x01 << i);
                            sgUserCnt++;
                            tmp8 = i;
                            // DBG_P(3, 3, 0x8201AB, 1, sgUserCnt, 1, i);  //82 01 AB, "Locking obj sgUserCnt[%X], i[%02X]", 1 1
                            break;
                        }
                    }

                    if (i >= pG3->b.mLckLocking_Tbl.hdr.rowCnt)
                    {
                        if ((pG3->b.mLckLocking_Tbl.val[0].uid == tmp64)
                            && ((sgUserRange & 0x01) == 0))
                        {
							
							tcg_core_trace(LOG_INFO, 0x899c, "* %x\n", (u32)tmp64);
                            // DBG_P(2, 3, 0x82021F, 4, (U32)tmp64); // 82 02 1F, "* %08X" 4
                            sgUserRange |= 0x01;
                            sgUserCnt++;
                            tmp8 = i;
                        }
                        else
                        {
                            errCode = 0x31;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                        }
                    }
                }
            }
            else
            { // check if entire Locking table
                iPload--;
                if (AtomDecoding_Uid2((u8 *)&tmp64) != STS_SUCCESS)
                {
                    errCode = 0x32;  result = STS_STAY_IN_IF_SEND;    goto MTD_EXIT;
                }

                if (tmp64 != UID_Locking)
                {
                    errCode = 0x33;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                sgUserCnt = LOCKING_RANGE_CNT + 1;
                sgUserRange = 0xffff;   //EntireLocking
                // DBG_P(2, 3, 0x8201AC, 1, sgUserCnt);  //82 01 AC, "Locking entire sgUserCnt[%X]", 1
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x34;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x35;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        if (sgUserCnt)
            sgUserPolicy = 0;   //User has ownership

        if (nameValue == 0x060001) //'RangePolicy'
        {
            if (AtomDecoding_Uint(&tmp8, sizeof(tmp8)) != STS_SUCCESS)
            {
                errCode = 0x38;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (tmp8 == 0)
            {
                if (sgUserCnt != 0) sgUserPolicy = 0;
            }
            else if (tmp8 == 1)
                sgUserPolicy = 1;
            else
            {
                errCode = 0x39;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x3A;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x3B;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        //check Additional DataStore Parameter
        if (nameValue == 0x060002) //'DataStoreTblSizes'
        {
            if (ChkToken() != TOK_StartList)
            {
                errCode = 0x21;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            while (ChkToken() != TOK_EndList)
            {
                iPload--;

                if (AtomDecoding_Uint((u8*)&tmp32, sizeof(tmp32)) != STS_SUCCESS)
                {
                    errCode = 0x22;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                if (cnt >= DSTBL_MAX_NUM)      //too many tables
                {
                    errCode = 0x23;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                if (tmp32 > DATASTORE_LEN)
                {
                    errCode = 0x24;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                totalsize += tmp32;
                if (totalsize>DATASTORE_LEN) //size is too large
                {
                    errCode = 0x24;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                if (tmp32%DSTBL_ALIGNMENT)   //not aligned
                {
                
					tcg_core_trace(LOG_INFO, 0xfc9b, "not aligned: %x",tmp32);
                    errCode = 0x25;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                DSTblSize[cnt] = tmp32;
				
				tcg_core_trace(LOG_INFO, 0x2f25, "cnt= %x, DSTblSize= %x",cnt, tmp32);
                cnt++;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x26;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            tmp8 = ChkToken();
        }
        else
        {
            errCode = 0x27;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
        }
    }
CHK_ENDLIST:
#endif

    if (tmp8 != TOK_EndList)      //test cases 3.1.5
    {
        errCode = 0x15;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
    {
        errCode = 0x16;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
    {
        errCode = 0x17;  goto MTD_EXIT;
    }

#if 0 // (CO_SUPPORT_ATA_SECURITY == mTRUE)
    // Opal 5.2.1: return fail with a status FAIL if ATA Security is enabled
    if (AatSecuriytActivated())
    {
        fill_no_data_token_list();
        //return STS_FAIL;
        {    errCode = 0x18;  result=STS_FAIL;    goto MTD_EXIT; }
    }
    else
#endif
    {
//        if((invokingUID.all==UID_SP_Locking) &&
        if (pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured_inactive)
        {
            //TODO:
            //  1. bit 1 of word 82, bit 1 of word 85 and all bits of word 89, 90, 92, 128 in the IDENTIFY DEVICE
            //     data SHALL be set to all-0.
            //  2. LockingEnabled bit in Locking Feature Descriptor in the Level 0 SHALL be set to 1. (v)
            //  3. LifeCycleState of Locking SP object in the SP table SHALL be set to 0x09. (v)
            //  4. A startup of a session to the Locking SP can succeed. (v)
            //  5. PIN for Admin1 in LockingSP should be set to SID PIN (Application Note)
            //  6. update table to NAND

            // copy SID pin to Admin pin
            memcpy((u8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin, (u8 *)&pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin, sizeof(pG3->b.mLckCPin_Tbl.val[0].cPin));
#if _TCG_ != TCG_PYRITE
            mSgUser.cnt = sgUserCnt;
            mSgUser.policy = sgUserPolicy;
            mSgUser.range = sgUserRange;

            DataStore_Setting(cnt, DSTblSize);
            SingleUser_Setting();
			
            //cjdbg, todo: genkey according to SingleUser count...
/*
			for (i = 0; i <= LOCKING_RANGE_CNT; i++){
				tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| LckKTblkey1|%x %x",i, pG3->b.mLckKAES_256_Tbl.val[i].key1[0], pG3->b.mLckKAES_256_Tbl.val[i].key1[1]);
				tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| LckKTblkey2|%x %x",i, pG3->b.mLckKAES_256_Tbl.val[i].key2[0], pG3->b.mLckKAES_256_Tbl.val[i].key2[1]);
				tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| Rawkey1|%x %x",i, mRawKey[i].dek.aesKey[0], mRawKey[i].dek.aesKey[1]);	
				tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| Rawkey2|%x %x",i, mRawKey[i].dek.xtsKey[0], mRawKey[i].dek.xtsKey[1]);	
			}
*/		
			for (i = 1; i <= LOCKING_RANGE_CNT; i++){

				TcgChangeKey(i);
            }
			
			bKeyChanged = mFALSE;
#endif
#if 0 //CO_SUPPORT_AES
            if (TCG_ACT_IN_OPAL())
            {
                //WrapKEK is a KEK used to wrap DEK. WrapKEK should be wrapped by some KDF
                HAL_Gen_Key(WrapKEK, sizeof(WrapKEK));
#if _TCG_DEBUG
                WrapKEK[0] = 0x12345678;  // cjdbg: test only,
#endif
                TcgWrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], (U32)UID_Authority_Admin1, WrapKEK);
            }
#endif
            tcg_nf_G2Wr(mFALSE);

            tcg_nf_G3Wr(mFALSE);

            #if 1  //Max modify
            
            pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured;
			
            tcg_nf_G1Wr(mFALSE);
			
			tcg_core_trace(LOG_INFO, 0x4f2f, "Method Activate done!");

			mTcgStatus |= TCG_ACTIVATED;
			mTcgActivated = TCG_ACTIVATED;

			//method_complete_post(req, mTRUE);
			
			//cb_activate(Begin)(req);

            #else
            TcgFuncRequest1(MSG_TCG_G2WR);   //WaitG2Wr();  //AccessCtrlTbl, cj: add LockingInfoTbl

            TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

            pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured;     // (3)
            TcgFuncRequest1(MSG_TCG_G1WR);   //WaitG1Wr();

            mTcgStatus |= TCG_ACTIVATED;
			mTcgActivated = TCG_ACTIVATED;
            #endif
        }
        else
        {
            errCode = 0xff;
        }
        fill_no_data_token_list();

        result = STS_SUCCESS;
    }

MTD_EXIT:
	
	tcg_core_trace(LOG_INFO, 0x622c, "errCode|%x, cnt|%x, mSgUser.range|%x\n", errCode, cnt, mSgUser.range);
    // DBG_P(4, 3, 0x8201AF, 1,  errCode, 1, cnt, 2, mSgUser.range);  //82 01 AF, "M_Activate: Err=%02X, DSCnt=%02X, SURx=%04X", 1 1 2
    return result;

}

//-------------------Reactivate-------------------------
ddr_code u16 Method_Reactivate(req_t *req)
{
    u64 tmp64;
    u32 nameValue, tmp32, totalsize = 0;
    u16 result = 0;
    u8  i, tmp8, errCode = 0;
    u8  *ptr = NULL;
	dtag_t dtag;

    // init reactivate_varsMgm
    reactivate_varsMgm.sgUserRange = 0;
    reactivate_varsMgm.sgUserCnt = 0;
    reactivate_varsMgm.sgUserPolicy = 1;
    reactivate_varsMgm.bAdmin1PIN = 0;
    reactivate_varsMgm.dsCnt = 0;

	tcg_core_trace(LOG_INFO, 0x83b5, "Method_Reactivate()");
	
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
    {
        errCode = 0x10;  result = STS_SESSION_ABORT;  goto MTD_EXIT;
    }

    //retrieve parameter 'DataStoreTblSizes'
    tmp8 = ChkToken();
    if (tmp8 == TOK_StartName)
    {
        if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
        {
            errCode = 0x20;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
        }

        if (nameValue == 0x060000) //'SingleUserSelectionList'
        {
            //mSgUser.cnt=0;
            //mSgUser.policy=0;
            //mSgUser.range=0;

            if (ChkToken() == TOK_StartList)
            { // Locking table objects list
                tmp8 = 0;
                while (ChkToken() != TOK_EndList)
                {
                    iPload--;

                    if (AtomDecoding_Uid2((u8 *)&tmp64) != STS_SUCCESS)
                    {
                        errCode = 0x30;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
                    }

                    //check if it is in the locking table
                    for(i=tmp8;i<pG3->b.mLckLocking_Tbl.hdr.rowCnt;i++)
                    {
                        if(pG3->b.mLckLocking_Tbl.val[i].uid==tmp64)
                        {
                            reactivate_varsMgm.sgUserRange |= (0x01<<i);
                            reactivate_varsMgm.sgUserCnt++;
                            tmp8 = i;
                            break;
                        }
                    }

                    if (i >= pG3->b.mLckLocking_Tbl.hdr.rowCnt)
                    {
                        if ((pG3->b.mLckLocking_Tbl.val[0].uid == tmp64)
                            && ((reactivate_varsMgm.sgUserRange & 0x01) == 0))
                        {
                            //TCG_PRINTF("* %08x\n", (U32)tmp64);
                            reactivate_varsMgm.sgUserRange |= 0x01;
                            reactivate_varsMgm.sgUserCnt++;
                            tmp8 = i;
                        }
                        else
                        {
                            errCode = 0x31;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                        }
                    }
                }
            }
            else
            { // check if entire Locking table
                iPload--;
                if (AtomDecoding_Uid2((u8 *)&tmp64) != STS_SUCCESS)
                {
                    errCode = 0x32;  result = STS_STAY_IN_IF_SEND;    goto MTD_EXIT;
                }

                if (tmp64 != UID_Locking)
                {
                    errCode = 0x33;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                reactivate_varsMgm.sgUserCnt = LOCKING_RANGE_CNT + 1;
                reactivate_varsMgm.sgUserRange = 0xffff;   //EntireLocking
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x34;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x35;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        if (reactivate_varsMgm.sgUserCnt)
            reactivate_varsMgm.sgUserPolicy = 0;   //User has ownership

        if (nameValue == 0x060001) //'RangePolicy'
        {
            if (AtomDecoding_Uint(&tmp8, sizeof(tmp8)) != STS_SUCCESS)
            {
                errCode = 0x38;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (tmp8 == 0)
            {
                if (reactivate_varsMgm.sgUserCnt != 0) reactivate_varsMgm.sgUserPolicy = 0;
            }
            else if (tmp8 == 1)
                reactivate_varsMgm.sgUserPolicy = 1;
            else
            {
                errCode = 0x39;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x3A;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x3B;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        if (nameValue == 0x060002) //'Admin1PIN'
        {
            //if(AtomDecoding_Uint(&tmp8, sizeof(tmp8))!=STS_SUCCESS)
            //{   errCode=0x3C;  result=STS_INVALID_PARAMETER;    goto MTD_EXIT; }

            result = AtomDecoding_ByteHdr(&tmp32);
            if (result == STS_SUCCESS)
            {
                if (tmp32>CPIN_LENGTH)
                {
                    result = STS_INVALID_PARAMETER;       //need to check ...
                    errCode = 0x3C;     goto MTD_EXIT;
                }

                reactivate_varsMgm.bAdmin1PIN = 1;
                memset(mSessionManager.HtChallenge, 0, sizeof(mSessionManager.HtChallenge));
                mSessionManager.HtChallenge[0] = (u8)tmp32;
                for (i = 0; i < tmp32; i++)
                    mSessionManager.HtChallenge[i + 1] = mCmdPkt.payload[iPload++];
            }
            else
            {
                errCode = 0x3D;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x3E;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((u8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x3F;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        //check Additional DataStore Parameter
        if (nameValue == 0x060003) //'DataStoreTblSizes'
        {
            if (ChkToken() != TOK_StartList)
            {
                errCode = 0x41;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            while (ChkToken() != TOK_EndList)
            {
                iPload--;

                if (AtomDecoding_Uint((u8*)&tmp32, sizeof(tmp32)) != STS_SUCCESS)
                {
                    errCode = 0x42;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                // *WCS TFS Item 168907 [Security] Incomplete length check
                if (tmp32 > DATASTORE_LEN) //size is too large
                {
                    errCode = 0x48;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }
                // &WCS TFS Item 168907 [Security] Incomplete length check

                if (reactivate_varsMgm.dsCnt >= DSTBL_MAX_NUM)      //too many tables
                {
                    errCode = 0x43;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                totalsize += tmp32;
                if (totalsize>DATASTORE_LEN) //size is too large
                {
                    errCode = 0x44;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                if (tmp32%DSTBL_ALIGNMENT)   //not aligned
                {
                    errCode = 0x45;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                reactivate_varsMgm.DSTblSize[reactivate_varsMgm.dsCnt] = tmp32;
                reactivate_varsMgm.dsCnt++;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x46;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            tmp8 = ChkToken();
        }
        else
        {
            errCode = 0x47;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
        }
    }

CHK_ENDLIST:
    if (tmp8 != TOK_EndList)      //test cases 3.1.5
    {
        errCode = 0x15;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
    {
        errCode = 0x16;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
    {
        errCode = 0x17;  goto MTD_EXIT;
    }

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
    {
        errCode = 0x18;   result = STS_SESSION_ABORT;   goto MTD_EXIT;
    } //no definition in Test Case!!


    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    { // return FAIL if any of the locking ranges is enabled
        if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled || pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled)
        {
            errCode = 0x19;   result = STS_FAIL;    goto MTD_EXIT;
        }
    }

    //Reactivate processing...
    //restore to OFS with some exceptions... C_PIN_Admin1.PIN, RangeStart/RangeLength, K_AES
	
    // Backup RangeTbl

    dtag = dtag_get(DTAG_T_SRAM,(void *)&tcgReactivate_buf);
	sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
	
	memset(tcgReactivate_buf, 0, DTAG_SZE);

    ptr = (u8*)tcgReactivate_buf;  //Max modify

	//tcg_core_trace(LOG_INFO, 0, "<1> tcgReactivate_buf = 0x%x , ptr = 0x%x",(u8 *)tcgReactivate_buf , ptr);
	
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {	
        memcpy(ptr, (u8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeStart, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart);
        memcpy(ptr, (u8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeLength, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength);
		//tcg_core_trace(LOG_INFO, 0, "<Backup> i=%d , ptr = 0x%x | %x",i ,ptr, *(u32 *)ptr);

	}

    if (!reactivate_varsMgm.bAdmin1PIN)
    {// Backup sCPin
        memcpy(ptr, (u8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag, sizeof(sCPin));
		//ptr += sizeof(sCPin);
    }
	
    memcpy(reactivate_varsMgm.bk_HtChallenge, mSessionManager.HtChallenge, sizeof(mSessionManager.HtChallenge));  // backup Challenge.	
    memcpy(reactivate_varsMgm.bk_HtChallenge_salt, (const void *)pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_salt , 32);  // backup CPIN salt.
	
	//tcg_core_trace(LOG_INFO, 0, "<2> tcgReactivate_buf = 0x%x , ptr = 0x%x",(u8 *)tcgReactivate_buf , ptr);

	// Backup AesKey (TblKey ->Unwrap-> mRawKey)
#ifdef NS_MANAGE

	u32 entry_id = 0, ns_id = 0;

	if(ns_array_menu->total_order_now > 1)
	{
		for(i=0; i<ns_array_menu->total_order_now; i++, entry_id++)
		{
			ns_id = ns_array_menu->array_order[i] + 1;

			PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt,sizeof(pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt)
						       ,1,32,(u32*)WrapKEK);		//Gen current kek
				
			Tcg_Key_wp_uwp(LOCKING_RANGE_CNT+ns_id, AES_256B_KUWP_NO_SECURE);        //unwrap Tbl key to mRawkey
		}
	}
	else
#endif
	{
		PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[0].salt,sizeof(pG3->b.mKEKsalt[0].salt)
						   ,1,32,(u32*)WrapKEK);	//Gen current kek
			
		Tcg_Key_wp_uwp(0, AES_256B_KUWP_NO_SECURE);
		
		for (i = 1; i <= LOCKING_RANGE_CNT; i++)
    	{
			if((pG3->b.mLckLocking_Tbl.val[i].rangeStart == 0) && (pG3->b.mLckLocking_Tbl.val[i].rangeLength == 0))
			{
				continue; 
			}

			PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[i].salt,sizeof(pG3->b.mKEKsalt[i].salt)
						      ,1,32,(u32*)WrapKEK);	//Gen current kek

			Tcg_Key_wp_uwp(i, AES_256B_KUWP_NO_SECURE);
    	}
	}

#if 1
    tcg_nf_G2RdDefault();

    tcg_nf_G3RdDefault();

    // Restore rangeStart/rangeLength
	for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
		//tcg_core_trace(LOG_INFO, 0, "<Restore> i=%d , buf = 0x%x | %x",i ,(u8 *)tcgReactivate_buf, *(u32 *)tcgReactivate_buf);

		memcpy((u8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeStart, (u8*)tcgReactivate_buf, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart));
        tcgReactivate_buf +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart);

        memcpy((u8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeLength, (u8*)tcgReactivate_buf, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength));
        tcgReactivate_buf +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength);
    }

    if (reactivate_varsMgm.bAdmin1PIN)
    {
		memset(&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin, 0, sizeof(pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin));
		
		PKCS5_PBKDF2_HMAC((u32*)(&reactivate_varsMgm.bk_HtChallenge[1]), (u32)reactivate_varsMgm.bk_HtChallenge[0], (u32*)reactivate_varsMgm.bk_HtChallenge_salt,
			               32,2500,32,(u32*)pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_val);				  
		
		memcpy(pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_salt,reactivate_varsMgm.bk_HtChallenge_salt , 32);	// restore CPIN salt.

						   
		//sec_gen_sha3_256_hash(&reactivate_varsMgm.bk_HtChallenge[1], reactivate_varsMgm.bk_HtChallenge[0], pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_val);
		pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag = CPIN_IN_PBKDF;

		//Tcg_GenCPinHash(&reactivate_varsMgm.bk_HtChallenge[1], reactivate_varsMgm.bk_HtChallenge[0], &pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin);
    }
    else
    {// Backup sCPin
        memcpy((u8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag, (u8*)tcgReactivate_buf, sizeof(sCPin));
        //ptr += sizeof(sCPin);
    }

	//tcg_core_trace(LOG_INFO, 0, "<3> tcgReactivate_buf = 0x%x , ptr = 0x%x",(u8 *)tcgReactivate_buf , ptr);

	// Restore AesKeyTbl (Gen new KEK)
#ifdef NS_MANAGE

	if(ns_array_menu->total_order_now > 1)
	{
		for(i=0; i<ns_array_menu->total_order_now; i++, entry_id++)
		{
			ns_id = ns_array_menu->array_order[i] + 1;
			
			sec_gen_rand_number((u8*)pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt , 32);    //gen kek_salt

			PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt,sizeof(pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt)
						       ,1,32,(u32*)WrapKEK);		//Gen new kek
				
			Tcg_Key_wp_uwp(LOCKING_RANGE_CNT+ns_id, AES_256B_KWP_NO_SECURE);        //unwrap Tbl key to mRawkey
		}
	}
	else
#endif
	{
    	for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    	{
    		if((pG3->b.mLckLocking_Tbl.val[i].rangeStart == 0) && (pG3->b.mLckLocking_Tbl.val[i].rangeLength == 0))
			{
				if(i == 0)  //Global Range key
				{
					sec_gen_rand_number((u8*)pG3->b.mKEKsalt[0].salt , 32);    //gen kek_salt
	
					tcg_core_trace(LOG_INFO, 0x7336, "[TCG]Gen new Salt|%x",pG3->b.mKEKsalt[0].salt);

					PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[0].salt,sizeof(pG3->b.mKEKsalt[0].salt)
								      ,1,32,(u32*)WrapKEK);	                 //Gen kek
				  
					tcg_core_trace(LOG_INFO, 0x9185, "[TCG]Gen new KEK|%x",*(u32*)WrapKEK);

					Tcg_Key_wp_uwp(0, AES_256B_KWP_NO_SECURE);
				}
				continue; 
			}
			sec_gen_rand_number((u8*)pG3->b.mKEKsalt[i].salt , 32);    //gen kek_salt
	
			tcg_core_trace(LOG_INFO, 0xd826, "[TCG]Gen new Salt|%x",pG3->b.mKEKsalt[i].salt);

			PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[i].salt,sizeof(pG3->b.mKEKsalt[i].salt)
						      ,1,32,(u32*)WrapKEK);	                 //Gen kek
				  
			tcg_core_trace(LOG_INFO, 0xfbaa, "[TCG]Gen new KEK|%x",*(u32*)WrapKEK);

			Tcg_Key_wp_uwp(i, AES_256B_KWP_NO_SECURE);
		}
		
		#ifdef NS_MANAGE
			if(ns_array_menu->total_order_now == 1)
			{
				u8 tar;

				tar = ns_array_menu->array_order[0] + 1 + LOCKING_RANGE_CNT;

				memcpy(&pG3->b.mLckKAES_256_Tbl.val[tar], &pG3->b.mLckKAES_256_Tbl.val[0], sizeof(pG3->b.mLckKAES_256_Tbl.val[0]));
				memcpy(&pG3->b.mKEKsalt[tar].salt, &pG3->b.mKEKsalt[0].salt, sizeof(pG3->b.mKEKsalt[0].salt));				
			}
		#endif

	}

	dtag_put(DTAG_T_SRAM, dtag);
	
    mSgUser.cnt = reactivate_varsMgm.sgUserCnt;
    mSgUser.policy = reactivate_varsMgm.sgUserPolicy;
    mSgUser.range = reactivate_varsMgm.sgUserRange;

    DataStore_Setting(reactivate_varsMgm.dsCnt, reactivate_varsMgm.DSTblSize);    //update group2
    SingleUser_Setting();                   

    tcg_nf_G2Wr(mFALSE);

    tcg_nf_G3Wr(mFALSE);

    LockingRangeTable_Update();

	ResetSessionManager();
	
	//method_complete_post(req,mTRUE);

    //cb_Reactivate(Begin)(req);

#else
    // restore mTbl to OFS
    TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);   //cTbl_2_mTbl_byGrp(GRP_2);
    TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);   //cTbl_2_mTbl_byGrp(GRP_3);
    // TCG_NewKeyTbl();

    // Restore AesKeyTbl
#if CO_SUPPORT_AES
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        pG3->b.mWKey[i].state = TCG_KEY_UNWRAPPED;
        pG3->b.mWKey[i].icv[0] = 0;
        memcpy(&pG3->b.mWKey[i].dek, &mRawKey[i].dek, sizeof(mRawKey[0].dek));
    }
#endif

    // Restore rangeStart/rangeLength
    ptr = (U8*)tcgTmpBuf;
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        memcpy((U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeStart, ptr, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart);

        memcpy((U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeLength, ptr, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength);
    }

    if (bAdmin1PIN)
    {
        Tcg_GenCPinHash(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], &pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin);
    }
    else
    {// Backup sCPin
        memcpy((U8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag, ptr, sizeof(sCPin));
        //ptr += sizeof(sCPin);
    }

    mSgUser.cnt = sgUserCnt;
    mSgUser.policy = sgUserPolicy;
    mSgUser.range = sgUserRange;

    // DBG_P(3, 3, 0x8201BE, 1, dsCnt, 2, mSgUser.range);  //82 01 BE, "M_ReAct: DSCnt=%02X, SURx=%04X", 1 2

    DataStore_Setting(dsCnt, DSTblSize);    //update group2
    SingleUser_Setting();                   //update group2/group3

//tcg_souts("w2/w3");
    TcgFuncRequest1(MSG_TCG_G2WR);   //WaitG2Wr();
    TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

    //cj: TcgStatus should keep the same, mTcgStatus = 0;
    LockingRangeTable_Update();

    //dataBuf[iDataBuf++]=TOK_StartList;
    //dataBuf[iDataBuf++]=TOK_EndList;
    //dataBuf[iDataBuf++]=TOK_EndOfData;
#endif
    result = STS_SUCCESS_THEN_ABORT;

MTD_EXIT:

	tcg_core_trace(LOG_INFO, 0xebc3, "Method_Reactivate() errCode|%x dsCnt|%x mSgUser|%x\n",errCode, reactivate_varsMgm.dsCnt, mSgUser.range);

    return result;
}


ddr_code u16 Method_Next(req_t *req)
{
    u64 where;
    u8 *ptTblObj = NULL;
    bool p1Found = mFALSE, p2Found = mFALSE;

    u16  rowCnt = 0, objSize = 0, result;
    u8   count = 0;
    u8   iRow, byte;

    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    where = 0x00;

    byte = ChkToken();
    if (byte == TOK_EndList)
        goto END_LIST;

    else if (byte == TOK_StartName)
    {//optional parameters: where / count
        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
            return STS_INVALID_PARAMETER;

        if (byte == 0x00)
        { // where
            if (AtomDecoding_Uid2((u8*)&where) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (ChkToken() != TOK_EndName)
                return STS_INVALID_PARAMETER;

            p1Found = TRUE;

            byte = ChkToken();
            if (byte == TOK_StartName) //next option parameter
            {
                if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;
            }
        }

        if (byte == 0x01)
        { // count
            if (AtomDecoding_Uint(&count, sizeof(count)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (ChkToken() != TOK_EndName)
                return STS_INVALID_PARAMETER;

            p2Found = mTRUE;

            byte = ChkToken();
        }

        if (byte != TOK_EndList)
            return STS_SESSION_ABORT;
    }
    else //NG within parameters
        return STS_SESSION_ABORT;   //A6-1-5-2-1, STS_INVALID_PARAMETER;

END_LIST:
    if (ChkToken() != TOK_EndOfData)
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //method execution:    
	tcg_core_trace(LOG_INFO, 0x9a2c, "Method->Next: %08x-%08x %02x", (u32)(where>>32),(u32)where, count);

    //FetchTcgTbl(pInvokingTbl, invokingTblSize);

    ptTblObj = (u8 *)(pInvokingTbl + sizeof(sTcgTblHdr) + sizeof(sColPrty) * ((sTcgTblHdr *)pInvokingTbl)->colCnt);
    rowCnt = ((sTcgTblHdr *)pInvokingTbl)->rowCnt;
    objSize = ((sTcgTblHdr *)pInvokingTbl)->objSize;

    if(p1Found==mFALSE)
    {
        where = *(u64*)ptTblObj;
        iRow = 0;
    }
    else
    { //p1Found==TRUE
        p1Found = mFALSE;
        for (iRow = 0; iRow<rowCnt; iRow++)
        {
            if (*(u64 *)ptTblObj == where)
            { // table object is found!
                p1Found = mTRUE;

                iRow++;
                ptTblObj += objSize;
                break;
            }
            ptTblObj += objSize;
        }

        if(p1Found == mFALSE)
        {
			tcg_core_trace(LOG_INFO, 0x4c4f, "!!NG Not found");
            return STS_INVALID_PARAMETER;
        }
    }

    if(p2Found==mFALSE)
        count = (u8)rowCnt;

    u64 tmp64;
    u32 tmp32 = invokingUID.all >>32;

    //prepare payload for response
    dataBuf[iDataBuf++]=TOK_StartList;
    dataBuf[iDataBuf++]=TOK_StartList;

    for(; iRow<rowCnt; iRow++)
    {
        tmp64 = *(u64*)ptTblObj;
        if(count)
        {
            if((u32)(tmp64>>32)==tmp32) // some rows might be disabled (ex, DataStoreN)
            {
                AtomEncoding_Int2Byte(ptTblObj, sizeof(u64));
                count--;
            }
        }
        else
            break;

        ptTblObj += objSize;
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;
}

/***********************************************************
* Admin table getACL method
* ref. core spec 5.3.3.13
***********************************************************/
ddr_code u16 Method_GetACL(req_t *req)
{
    int zSTS = STS_SUCCESS;
    u16 j;
    u8 errCode = 0x00;  //no error
    bool invIdIsFound = mFALSE;
    
	tcg_core_trace(LOG_INFO, 0x383f, "Method_GetACL()");

    // DBG_P(1, 3, 0x820127);  //82 01 27, "[F]Method_GetACL"
    if (invokingUID.all != UID_AccessControl){
        zSTS = STS_NOT_AUTHORIZED; errCode = 0x01;  goto exit_Method_GetACL;
    }

    if (ChkToken() != TOK_StartList) {
        zSTS = STS_SESSION_ABORT; errCode = 0x08; goto exit_Method_GetACL;
    }
    if (AtomDecoding_Uid2(invokingUID.bytes) != STS_SUCCESS)
    {
        zSTS = STS_INVALID_PARAMETER;
        errCode = 0x11; goto exit_Method_GetACL;
    }
    if (AtomDecoding_Uid2(methodUID.bytes) != STS_SUCCESS)
    {
        zSTS = STS_INVALID_PARAMETER;
        errCode = 0x21; goto exit_Method_GetACL;
    }

    if (ChkToken() != TOK_EndList) {
        zSTS = STS_SESSION_ABORT; errCode = 0x23; goto exit_Method_GetACL;
    }
    if (ChkToken() != TOK_EndOfData) {
        zSTS = STS_SESSION_ABORT; errCode = 0x24; goto exit_Method_GetACL;
    }

    //status list check
    zSTS = chk_method_status();
    if(zSTS != STS_SUCCESS)
    {
        errCode = 0x70;
        goto exit_Method_GetACL;
    }
    
	tcg_core_trace(LOG_INFO, 0xe622, "Method getACL => acl_Inv:acl_Mtd %08x-%08x : %08x-%08x",(u32)(invokingUID.dw[1]),(u32)(invokingUID.dw[0]),(u32)(methodUID.dw[1]),(u32)(methodUID.dw[0]));

    if (tcg_access_control_check(&invIdIsFound) == zNG)
    {
        zSTS = STS_NOT_AUTHORIZED;
        errCode = 0x31;
        goto exit_Method_GetACL;
    }

    zSTS = (mSessionManager.SPID.all == UID_SP_Admin) ? admin_aceBooleanExpr_chk(mFALSE) : locking_aceBooleanExpr_chk(mFALSE);
    if (zSTS == zNG)
    {
        errCode = 0x32;
        goto exit_Method_GetACL;
    }

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_StartList;

    for (j = 0; j<ACCESSCTRL_ACL_CNT; j++)
    {
        if (aclBackup[j].aclUid == UID_Null)
            break;

        AtomEncoding_Int2Byte((u8*)&aclBackup[j].aclUid, sizeof(u64));
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;
    set_status_code(0, 0, 0);

exit_Method_GetACL:
    
	tcg_core_trace(LOG_INFO, 0xe623, "Method_GetACL() errCode|%x\n", errCode);
    // DBG_P(2, 3, 0x8201AA, 1, errCode);  //82 01 AA, "Method_GetACL, err_code = %X", 1
    return zSTS;

}

//-------------------Activate-------------------------
//Test Cases D8: how to deal with "Transaction"?
ddr_code u16 Method_GenKey(req_t *req)
{
    u16 result;
    u8 keyNo = 0xff;

	tcg_core_trace(LOG_INFO, 0x7f6c, "Method_GenKey()");

	//parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //check the key range (invoking UID) and update key
    switch (invokingUID.all)
    {
        case UID_K_AES_256_GRange_Key:
            keyNo = 0;
            break;
        default:
            keyNo = (u8)invokingUID.all; // - ((U8)UID_K_AES_256_Range1_Key - 1);
            break;
    }

    if (keyNo <= LOCKING_RANGE_CNT)
    {
    	/*
    	tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-Grange| LckKTblkey1|%x %x", pG3->b.mLckKAES_256_Tbl.val[0].key1[0], pG3->b.mLckKAES_256_Tbl.val[0].key1[1]);
    	tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-Grange| LckKTblkey2|%x %x", pG3->b.mLckKAES_256_Tbl.val[0].key2[0], pG3->b.mLckKAES_256_Tbl.val[0].key2[1]);
    	tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-Grange| Rawkey1|%x %x", mRawKey[0].dek.aesKey[0], mRawKey[0].dek.aesKey[1]);		
    	tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-Grange| Rawkey2|%x %x", mRawKey[0].dek.xtsKey[0], mRawKey[0].dek.xtsKey[1]);	

		tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| LckKTblkey1|%x %x",keyNo, pG3->b.mLckKAES_256_Tbl.val[keyNo].key1[0], pG3->b.mLckKAES_256_Tbl.val[keyNo].key1[1]);
		tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| LckKTblkey2|%x %x",keyNo, pG3->b.mLckKAES_256_Tbl.val[keyNo].key2[0], pG3->b.mLckKAES_256_Tbl.val[keyNo].key2[1]);
		tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| Rawkey1|%x %x",keyNo, mRawKey[keyNo].dek.aesKey[0], mRawKey[keyNo].dek.aesKey[1]);	
		tcg_core_trace(LOG_INFO, 0, "[Max set]Orgine-range%x| Rawkey2|%x %x",keyNo, mRawKey[keyNo].dek.xtsKey[0], mRawKey[keyNo].dek.xtsKey[1]);	
		*/
		TcgChangeKey(keyNo);
		
        //update keys to KeyRAM and NAND
        if (mSessionManager.TransactionState == TRNSCTN_IDLE)
        {
#if 0 //CO_SUPPORT_AES
            if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
            {
                TcgUpdateWrapKey(keyNo); // determine if wrap new key or not
                TcgUpdateRawKey(keyNo);
            }
#endif
            #if 1 
			
			tcg_nf_G3Wr(mFALSE);
			LockingRangeTable_Update();
            //cb_genkey(Begin)(req);
            #else
            TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

            HAL_SEC_InitAesKeyRng();
            TcgFuncRequest1(MSG_TCG_CLR_CACHE);
            #endif
        }
        else
        {
#if 0 //CO_SUPPORT_AES
            if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
            {
                mRawKeyUpdateList |= (0x01<<keyNo);
            }
#endif
            flgs_MChnged.b.G3 = mTRUE;
            bKeyChanged = mTRUE;
        }
        result = STS_SUCCESS;
    }
    else
        result = STS_INVALID_PARAMETER;

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    //TODO: inform the kernel to update AES key setting if not in Transaction state
    return result;

}

//-------------------Method Erase-------------------------
ddr_code u16 Method_Erase(req_t *req)
{
    u64 tmp64;
    u16 result;
    u8 range = 0xFF, i;

	tcg_core_trace(LOG_INFO, 0xab0b, "Method_Erase()");

    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //cjdbg: need to check if singleuser mode or not?
    //get the locking range (invoking UID)
    if (invokingUID.all == UID_Locking_GRange)
        range = 0;
    else
        range = (u8)invokingUID.all;

    if (range <= LOCKING_RANGE_CNT)
    {
        TcgChangeKey(range);
        pG3->b.mLckLocking_Tbl.val[range].readLockEnabled=0x00;
        pG3->b.mLckLocking_Tbl.val[range].writeLockEnabled=0x00;
        pG3->b.mLckLocking_Tbl.val[range].readLocked=0x00;
        pG3->b.mLckLocking_Tbl.val[range].writeLocked=0x00;

        //search for corresponding CPIN
        tmp64=UID_CPIN_User1+range;
        for(i=0;i<pG3->b.mLckCPin_Tbl.hdr.rowCnt;i++)
        {
            if(tmp64==pG3->b.mLckCPin_Tbl.val[i].uid)
            {
                memset(&pG3->b.mLckCPin_Tbl.val[i].cPin, 0, sizeof(pG3->b.mLckCPin_Tbl.val[i].cPin));
                pG3->b.mLckCPin_Tbl.val[i].tries=0;
                break;
            }
        }
    }
    else
        result = STS_INVALID_PARAMETER;

    //update keys to Key RAM and NAND
    if (mSessionManager.TransactionState == TRNSCTN_IDLE)
    {
        #if 1
		tcg_nf_G3Wr(mFALSE);
		LockingRangeTable_Update();

        //cb_erase(Begin)(req);
        #else
        TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

        //update key and range
        LockingRangeTable_Update();

        TcgFuncRequest1(MSG_TCG_CLR_CACHE);
        #endif
    }
    else
    {
        flgs_MChnged.b.G3 = mTRUE;
        bKeyChanged = mTRUE;
    }

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return result;
}

#if TCG_FS_PSID

ddr_code int  TcgPsidVerify(void)
{
	tcg_core_trace(LOG_INFO, 0x8132, "TcgPsidVerify() table vs. EEPROM not coding yet, Max Pan");
/*
    if (*(u32*)tcg_ee_Psid == CPIN_IN_PBKDF)
    {   // table vs. EEPROM
        if (memcmp((u8*)tcg_ee_Psid, (u8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin)))
        {
            // DBG_P(1, 3, 0x820026);   // 82 00 26, "!!!Error, PSID tag exist but table PSID != EE PSID"
			tcg_core_trace(LOG_INFO, 0, "!!! table vs. EEPROM Err !!!\n");
            bTcgTblErr = mTRUE;
        }
    }
    else
*/  {   // table vs. MSID
		if (CPinMsidCompare(CPIN_PSID_IDX) == zNG){
            // DBG_P(1, 3, 0x820027);   // 82 00 27, "!!!Error, PSID tag doesn't exist but table PSID != MSID"
            // for Eric EL Lin requirement to reduce risks.
            // TcgForcePSIDSetToDefault();
			tcg_core_trace(LOG_INFO, 0x47c2, "!!! table vs. MSID Err !!!\n");
            bTcgTblErr = mTRUE;
        }
    }
    return zOK;

}

ddr_code void TcgPsidBackup(void)
{
	tcg_core_trace(LOG_INFO, 0x026b, "TcgEraseOpalKEK() not coding yet, Max Pan");
	return;
/*

    // Backup PSID
    if ((bTcgTblErr==mFALSE)
     && (pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_Tag==CPIN_IN_PBKDF))
    {
        memcpy((u8*)tcg_ee_Psid, (u8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));
    }
*/
}

ddr_code void TcgPsidRestore(void)
{
	tcg_core_trace(LOG_INFO, 0x0379, "TcgPsidRestore() not coding yet, Max Pan");
	return;
/*

    if (*(u32*)tcg_ee_Psid == CPIN_IN_PBKDF){
        memcpy((u8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, (u8*)tcg_ee_Psid, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));
    }else{
		tcg_core_trace(LOG_INFO, 0, "= Default PSID =");
    }
*/
}

#endif

//extern void crypto_change_mode_range(u8 crypto_type, u8 NS_ID, u8 change_key, u8 cryptoID);



// Processing Wrap/Unwrap
slow_data_ni ALIGNED(32) u8 wrap_buf[192]; 

ddr_code void Tcg_Key_wp_uwp(u8 idx , u8 case_select) 
{
	kwp_cfg_t kwp_cfg;
	enum spr_reg_idx spr_idx;
	//dtag_t dtag;
	//u32 size = 0;
	
	/* Initialize key wrap context */
	memset((void *)&kwp_cfg, 0x00, sizeof(kwp_cfg_t));
	bm_clear_secure_mode();
	spr_idx = SS_SPR_REG0;
	sec_ss_spr_prgm(spr_idx, WrapKEK);
	/*----------------------------------------------*/

	/* setup key wrap/unwrap configuration parameters */
	if(case_select == AES_256B_KWP_NO_SECURE)
	{
		void* Raw_Key1;    //Raw aes key
		void* Raw_Key2;    //Raw xts key
		void* WP_Key1;     //wrapped aes key
		void* WP_Key2;     //wrapped xts key

		//dtag = dtag_get(DTAG_T_SRAM, &Raw_Key1);
		//sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);

		memset(wrap_buf, 0, sizeof(wrap_buf));

		Raw_Key1 = wrap_buf;
        //sys_assert(Raw_Key1);

		WP_Key1 = Raw_Key1 + 32;

		Raw_Key2 = WP_Key1 + 64;
		
		WP_Key2 = Raw_Key2 + 32;
			
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Raw_Key1|%x , &WP_Key1|%x", Raw_Key1, WP_Key1);
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Raw_Key2|%x , &WP_Key2|%x", Raw_Key2, WP_Key2);

		//memset(Raw_Key1, 0, DTAG_SZE);
		
		memcpy(Raw_Key1, mRawKey[idx].dek.aesKey, 32);
		memcpy(Raw_Key2, mRawKey[idx].dek.xtsKey, 32);
		
		tcg_core_trace(LOG_INFO, 0xb938, "[Max set] Raw_Key1|%x , Raw_Key2|%x", *(u32 *)Raw_Key1, *(u32 *)Raw_Key2);

		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		//kwp_cfg.din_spr_idx = 1; /* SPR register index for data input 0 to 3 */  //non used
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_WRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */	
		
		sec_ss_key_wp_uwp(Raw_Key1, WP_Key1, &kwp_cfg);  //Wrap Key1(AES key)
		sec_ss_key_wp_uwp(Raw_Key2, WP_Key2, &kwp_cfg);  //Wrap Key2(XTS key)
	
		tcg_core_trace(LOG_INFO, 0xc0b2, "[TCG]Wrapped K1|%x K2|%x",*(u32 *)WP_Key1,*(u32 *)WP_Key2);

		memcpy(pG3->b.mLckKAES_256_Tbl.val[idx].key1, WP_Key1, 40);
		memcpy(pG3->b.mLckKAES_256_Tbl.val[idx].key2, WP_Key2, 40);
		
		//tcg_core_trace(LOG_INFO, 0, "[TCG]output icv|%x K1 icv|%x",*(((u32 *)WP_Key1)+8),pG3->b.mLckKAES_256_Tbl.val[idx].icv1[0]);

		memset(mRawKey[idx].dek.aesKey, 0, sizeof(mRawKey[idx].dek.aesKey));
		memset(mRawKey[idx].dek.xtsKey, 0, sizeof(mRawKey[idx].dek.xtsKey));

		/* Release dtags */
		//dtag_put(DTAG_T_SRAM, dtag);
		//sys_free_aligned(SLOW_DATA, Raw_Key1);
		memset(wrap_buf, 0, sizeof(wrap_buf));
		
		tcg_core_trace(LOG_INFO, 0x3b20, "[TCG]Range|%x key wrap done!",idx);
		tcg_core_trace(LOG_INFO, 0x6dc7, "[TCG]Raw|%x Tbl|%x",mRawKey[idx].dek.aesKey[0],pG3->b.mLckKAES_256_Tbl.val[idx].key1[0]);
		
	}
	else //AES_256B_KUWP_NO_SECURE
	{
		void* Tbl_Key1;     //wrapped aes key
		void* Tbl_Key2;     //wrapped xts key
		void* UWP_Key1;    //Unwrapped aes key
		void* UWP_Key2;    //Unwrapped xts key

		//dtag = dtag_get(DTAG_T_SRAM, &Tbl_Key1);
		//sys_assert(dtag.b.dtag != _inv_dtag.b.dtag);
		
		memset(wrap_buf, 0, sizeof(wrap_buf));

		Tbl_Key1 = wrap_buf;
        //sys_assert(Tbl_Key1);

		UWP_Key1 = Tbl_Key1 + 64;

		Tbl_Key2 = UWP_Key1 + 32;
		
		UWP_Key2 = Tbl_Key2 + 64;

		//memset(Tbl_Key1, 0, DTAG_SZE);
		
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Tbl_Key1|%x , &UWP_Key1|%x", Tbl_Key1, UWP_Key1);
		//tcg_core_trace(LOG_INFO, 0, "[Max set] &Tbl_Key2|%x , &UWP_Key2|%x", Tbl_Key2, UWP_Key2);

		memcpy(Tbl_Key1, pG3->b.mLckKAES_256_Tbl.val[idx].key1, 40);
		memcpy(Tbl_Key2, pG3->b.mLckKAES_256_Tbl.val[idx].key2, 40);
		
		tcg_core_trace(LOG_INFO, 0xa0f6, "[Max set] Tbl_Key1|%x , Tbl_Key2|%x", *(u32 *)Tbl_Key1, *(u32 *)Tbl_Key2);
		//tcg_core_trace(LOG_INFO, 0, "[TCG]Input icv|%x K1 icv|%x",*(((u32 *)Tbl_Key1)+8),pG3->b.mLckKAES_256_Tbl.val[idx].icv1[0]);
		
		kwp_cfg.cfp_en = 0; /* Chip Finger Print (cfp) enable/disable */
		kwp_cfg.aes_sm4_mode = 0; /* AES (0) or SM4 (1) mode */
		kwp_cfg.din_size = 1; /* din_size_select 0:128 bit, 1:256 bit */
		kwp_cfg.kek_size = 2; /* kek_size_select b'00:128 bit, b'10:256 bit */
		//kwp_cfg.din_spr_idx = SS_SPR_REG1; /* SPR register index for data input 0 to 3 */  
		kwp_cfg.kek_spr_idx = SS_SPR_REG0; /* SPR register 0 to 3 */
		kwp_cfg.wrap_op = SS_KEY_UWRAP; /* Operation Key Wrap/Unwrap, RAW Encrypt/Decrypt */
		//kwp_cfg.sec_mode_en = 0; /* secure mode enable(1)/disable(0) */
		
		sec_ss_key_wp_uwp(Tbl_Key1, UWP_Key1, &kwp_cfg);  //unwrap Key1(AES key)
				
		sec_ss_key_wp_uwp(Tbl_Key2, UWP_Key2, &kwp_cfg);  //unwrap Key2(XTS key)

		tcg_core_trace(LOG_INFO, 0x8cdf, "[TCG]Uwp K1|%x K2|%x",*(u32 *)UWP_Key1, *(u32 *)UWP_Key2);

		memcpy(mRawKey[idx].dek.aesKey, UWP_Key1, sizeof(mRawKey[idx].dek.aesKey));
		memcpy(mRawKey[idx].dek.xtsKey, UWP_Key2, sizeof(mRawKey[idx].dek.xtsKey));
		
		mRawKey[idx].state = TCG_KEY_UNWRAPPED;

		/* Release dtags */
		//dtag_put(DTAG_T_SRAM, dtag);
		//sys_free_aligned(SLOW_DATA, Tbl_Key1);
		memset(wrap_buf, 0, sizeof(wrap_buf));
		
		tcg_core_trace(LOG_INFO, 0xc109, "[TCG]Range|%x key unwrap done!",idx);		
		tcg_core_trace(LOG_INFO, 0xa1f1, "[TCG]Raw|%x Tbl|%x",mRawKey[idx].dek.aesKey[0],pG3->b.mLckKAES_256_Tbl.val[idx].key1[0]);
	}
	/*----------------------------------------------*/
	
	memset(WrapKEK, 0, sizeof(WrapKEK));
	
}


// Generate a new key ->Wrap ->update to G3.b.mLckKAES_256_Tbl[]

ddr_code void TcgChangeKey(u8 idx)  // if not CNL, idx = rangeNo
{
    if (idx >= TCG_MAX_KEY_CNT)
    {  
        tcg_core_trace(LOG_INFO, 0x6540, "!! TcgChangeKey err");
        return;
    }

	u32 cnt = 1;
#ifdef NS_MANAGE
	if((idx == 0) && (ns_array_menu->total_order_now > 1))
		cnt = ns_array_menu->total_order_now;
#endif

	for(u32 i=0; i<cnt; i++)
	{
#ifdef NS_MANAGE
		if(cnt > 1)
			idx = ns_array_menu->array_order[i] + 1 + LOCKING_RANGE_CNT;
#endif	
		sec_gen_rand_number((u8*)pG3->b.mKEKsalt[idx].salt , 32);    //gen kek_salt
	
		tcg_core_trace(LOG_INFO, 0x7a77, "[TCG]Gen new Salt|%x",pG3->b.mKEKsalt[idx].salt);

		PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[idx].salt,sizeof(pG3->b.mKEKsalt[idx].salt)
			,1,32,(u32*)WrapKEK);	            //Gen kek
				  
		tcg_core_trace(LOG_INFO, 0x3112, "[TCG]Gen new KEK|%x",*(u32*)WrapKEK);
	
    	sec_gen_rand_number((u8*)mRawKey[idx].dek.aesKey, 32);  //gen aes_key   
    	sec_gen_rand_number((u8*)mRawKey[idx].dek.xtsKey, 32);  //gen xts_key            

		tcg_core_trace(LOG_INFO, 0x8ffa, "[TCG]Gen new key K1|%x , K2|%x",mRawKey[idx].dek.aesKey[0],mRawKey[idx].dek.xtsKey[0]);
	
		Tcg_Key_wp_uwp(idx, AES_256B_KWP_NO_SECURE);
	}

#if 0
    sec_gen_rand_number((u8*)pG3->b.mLckKAES_256_Tbl.val[idx].key1, 32);  //aes_key   
    sec_gen_rand_number((u8*)pG3->b.mLckKAES_256_Tbl.val[idx].key2, 32);  //xts_key            

    memcpy(&mRawKey[idx].dek.aesKey, &pG3->b.mLckKAES_256_Tbl.val[idx].key1, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key1));
    memcpy(&mRawKey[idx].dek.xtsKey, &pG3->b.mLckKAES_256_Tbl.val[idx].key2, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key2));
	mRawKey[idx].state = TCG_KEY_UNWRAPPED;
#endif

#ifdef NS_MANAGE
	if(ns_array_menu->total_order_now == 1)
	{
		u8 tar;
		if(idx == 0)
			tar = ns_array_menu->array_order[0] + 1 + LOCKING_RANGE_CNT;
		else if(idx == (ns_array_menu->array_order[0] + 1 + LOCKING_RANGE_CNT))
			tar = 0;
		else
		{
			idx = ns_array_menu->array_order[0] + 1 + LOCKING_RANGE_CNT;
			tar = 0;
		}

		memcpy(&pG3->b.mLckKAES_256_Tbl.val[tar], &pG3->b.mLckKAES_256_Tbl.val[idx], sizeof(pG3->b.mLckKAES_256_Tbl.val[0]));
    	//memcpy(&pG3->b.mLckKAES_256_Tbl.val[tar].key2, &pG3->b.mLckKAES_256_Tbl.val[idx].key2, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key2));
    	memcpy(&pG3->b.mKEKsalt[tar].salt, &pG3->b.mKEKsalt[idx].salt, sizeof(pG3->b.mKEKsalt[idx].salt));

		//memcpy(&mRawKey[tar].dek.aesKey, &mRawKey[idx].dek.aesKey, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key1));
    	//memcpy(&mRawKey[tar].dek.xtsKey, &mRawKey[idx].dek.xtsKey, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key2));
		//mRawKey[idx].state = TCG_KEY_UNWRAPPED;
		
	}
#endif
	/*
	tcg_core_trace(LOG_INFO, 0, "[Max set]New-range%x| LckTblKey1|%x %x",idx, pG3->b.mLckKAES_256_Tbl.val[idx].key1[0], pG3->b.mLckKAES_256_Tbl.val[idx].key1[1]);	
	tcg_core_trace(LOG_INFO, 0, "[Max set]New-range%x| LckTblKet2|%x %x",idx, pG3->b.mLckKAES_256_Tbl.val[idx].key2[0], pG3->b.mLckKAES_256_Tbl.val[idx].key2[1]);
	tcg_core_trace(LOG_INFO, 0, "[Max set]New-range%x| Rawkey1|%x %x",idx, mRawKey[idx].dek.aesKey[0], mRawKey[idx].dek.aesKey[1]);	
	tcg_core_trace(LOG_INFO, 0, "[Max set]New-range%x| Rawkey2|%x %x",idx, mRawKey[idx].dek.xtsKey[0], mRawKey[idx].dek.xtsKey[1]);	
	*/
	bKeyChanged = mTRUE;

}

ddr_code void TcgRestoreGlobalKey() 
{
    
	tcg_core_trace(LOG_INFO, 0xb3eb, "TcgRestoreGlobalKey()");

    memcpy(&pG3->b.mLckKAES_256_Tbl.val[0].key1, &revertSp_varsMgm.backup_GR_key1, 40);
    memcpy(&pG3->b.mLckKAES_256_Tbl.val[0].key2, &revertSp_varsMgm.backup_GR_key2, 40);
    memcpy(&pG3->b.mKEKsalt[0].salt, &revertSp_varsMgm.backup_GR_KEKsalt, 32);

	#ifdef NS_MANAGE
	if(ns_array_menu->total_order_now == 1)
	{
		u8 tar;

		tar = ns_array_menu->array_order[0] + 1 + LOCKING_RANGE_CNT;

		memcpy(&pG3->b.mLckKAES_256_Tbl.val[tar], &pG3->b.mLckKAES_256_Tbl.val[0], sizeof(pG3->b.mLckKAES_256_Tbl.val[0]));
    	memcpy(&pG3->b.mKEKsalt[tar].salt, &pG3->b.mKEKsalt[0].salt, sizeof(pG3->b.mKEKsalt[0].salt));
		
	}
	#endif
		
	memset(revertSp_varsMgm.backup_GR_key1, 0, 40);
	memset(revertSp_varsMgm.backup_GR_key2, 0, 40);
	memset(revertSp_varsMgm.backup_GR_KEKsalt, 0, 32);
}


ddr_code void TcgEraseKey(u8 idx)
{
#if 1

	memset(&pG3->b.mLckKAES_256_Tbl.val[idx].key1, 0, sizeof(pG3->b.mLckKAES_256_Tbl.val[idx].key1));
	memset(&pG3->b.mLckKAES_256_Tbl.val[idx].key2, 0, sizeof(pG3->b.mLckKAES_256_Tbl.val[idx].key2));
	memset(&pG3->b.mKEKsalt[idx].salt, 0, sizeof(pG3->b.mKEKsalt[idx]));

	memset(&mRawKey[idx].dek, 0, sizeof(mRawKey[0].dek));

#else
	pG3->b.mWKey[idx].nsid = 0;
    pG3->b.mWKey[idx].range = 0;
    pG3->b.mWKey[idx].state = TCG_KEY_NULL; // Null Key
    memset(&pG3->b.mWKey[idx].dek, 0, sizeof(pG3->b.mWKey[0].dek));
    memset(&mRawKey[idx].dek, 0, sizeof(mRawKey[0].dek));
    mRawKey[idx].state = pG3->b.mWKey[idx].state;
    // DBG_P(3, 3, 0x82020D, 1, idx);
#endif
}


// Erase "pG3->b.mOpalWrapKEK[]"
ddr_code void TcgEraseOpalKEK(u32 auth)
{
	tcg_core_trace(LOG_INFO, 0xff11, "TcgEraseOpalKEK not coding yet, Max Pan");
	return;
	/*

    u32 y;
    TCGPRN("<ErOpalKEK>\n");
    DBG_P(0x01, 0x03, 0x71007E );  // <ErOpalKEK>
    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            break;
    }
    if (y == sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey))
    {
        TCGPRN("EraseKEK NG");
        DBG_P(0x01, 0x03, 0x71007F );  // EraseKEK NG
        return;
    }
    memset(pG3->b.mOpalWrapKEK[y].opalKEK, 0, sizeof(pG3->b.mOpalWrapKEK[0].opalKEK));
    memset(pG3->b.mOpalWrapKEK[y].icv, 0, sizeof(pG3->b.mOpalWrapKEK[0].icv));
    memset(pG3->b.mOpalWrapKEK[y].salt, 0, sizeof(pG3->b.mOpalWrapKEK[0].salt));
    pG3->b.mOpalWrapKEK[y].state = TCG_KEY_NULL;
*/
}

// Wrap "*pKEK" (OpalKEK or WrapKEK) by OpalKDF and stored in "pG3->b.mOpalWrapKEK[]"
ddr_code int TcgWrapOpalKEK(u8*chanllege, u8 len, u32 auth, u32* pKEK)
{	
	tcg_core_trace(LOG_INFO, 0x39ce, "TcgWrapOpalKEK not coding yet, Max Pan");
	return 0;
	/*
    u32 y;

    TCGPRN("<WrapOpalKEK> ");
    DBG_P(0x01, 0x03, 0x710076 );  // <WrapOpalKEK>

    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            break;
    }
    if (y == sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey))
    {
        TCG_ERR_PRN("!!NG\n");
        DBG_P(0x01, 0x03, 0x7F7F04 );  // !!NG
        return -1;
    }

    // Get_KeyWrap_KEK(chanllege, len, OpalKDF);
    HAL_Gen_Key(pG3->b.mOpalWrapKEK[y].salt, sizeof(pG3->b.mOpalWrapKEK[y].salt));      // generate new salt
    HAL_PBKDF2((U32 *)chanllege, len, pG3->b.mOpalWrapKEK[y].salt, sizeof(pG3->b.mOpalWrapKEK[y].salt), OpalKDF);

    // Start to wrap key
#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("<- %x ", (U32)pKEK[i]);
#endif
    aes_key_wrap(OpalKDF, pKEK, 0, WrapBuf);

    memcpy(pG3->b.mOpalWrapKEK[y].opalKEK, WrapBuf, sizeof(pG3->b.mOpalWrapKEK[0].opalKEK)+sizeof(pG3->b.mOpalWrapKEK[0].icv));
    pG3->b.mOpalWrapKEK[y].state = TCG_KEY_WRAPPED;

#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("-> %x ", WrapBuf[i]);
#endif

    TCGPRN("\n");
    DBG_P(0x01, 0x03, 0x710078 );  //
    memset(WrapBuf, 0, sizeof(WrapBuf));
    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif

    return 0;
*/
}

// Wrap the Range Key (pG3->b.mWKey[range].key) with KEK
ddr_code void Tcg_WrapDEK(u8 range, u32* pKEK)
{
	tcg_core_trace(LOG_INFO, 0xe865, "Tcg_WrapDEK not coding yet, Max Pan");
	return;
/*	

    if (pG3->b.mWKey[range].state != TCG_KEY_UNWRAPPED)
    {
        TCG_PRINTF("<WDEK> !!G3.WK[%2x].state = %x\n", range, pG3->b.mWKey[range].state);
        return;
    }

    TCG_PRINTF("<WDEK>G3Rng|%2x ", range);

    // Start to wrap key
#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("<- %x ", pG3->b.mWKey[range].dek.aesKey[i]);
#endif

    aes_key_wrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.aesKey, 0, (U32*)WrapBuf);
    memcpy(&pG3->b.mWKey[range].dek.aesKey, WrapBuf, sizeof(pG3->b.mWKey[0].dek.aesKey)+sizeof(pG3->b.mWKey[0].dek.icv1));
    aes_key_wrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.xtsKey, 0, (U32*)WrapBuf);
    memcpy(&pG3->b.mWKey[range].dek.xtsKey, WrapBuf, sizeof(pG3->b.mWKey[0].dek.xtsKey)+sizeof(pG3->b.mWKey[0].dek.icv2));

    pG3->b.mWKey[range].state = TCG_KEY_WRAPPED;

  #if _TCG_DEBUG
    //printk("-> Key0 : ");
    DBG_P(0x01, 0x03, 0x710080 );  // -> Key0 :
    for (int i = 0; i < 2; i++){
        //printk(" %x ", WrapBuf[i]);
        DBG_P(0x2, 0x03, 0x710081, 4, WrapBuf[i]);  //  %x
    }
    //printk("  -> Key1 : ");
    DBG_P(0x01, 0x03, 0x710082 );  //   -> Key1 :
    for (int i = 0; i < 2; i++){
        //printk(" %x ", WrapBuf[i + 10]);
        DBG_P(0x2, 0x03, 0x710083, 4, WrapBuf[i + 10]);  //  %x
    }
    //printk(" \n");
    DBG_P(0x01, 0x03, 0x710084 );  //
  #endif
    memset(WrapBuf, 0, sizeof(WrapBuf));
    //D_PRINTF("\n");
    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif
*/
}


// UnWrap the Range Key (pG3->b.mWKey[range].key) with KEK
ddr_code void Tcg_UnWrapDEK(u8 range, u32* pKEK, u8 target)
{
	tcg_core_trace(LOG_INFO, 0x5e74, "Tcg_UnWrapDEK not coding yet, Max Pan");
	return;
	/*

    TCGPRN("<UWDEK> %2x %2x:\n", range, target);
    DBG_P(0x3, 0x03, 0x710085, 1, range, 1, target);  // <UWDEK> %2x %2x:
    if ((pG3->b.mWKey[range].state == TCG_KEY_UNWRAPPED) && (target==TO_RAW_KEY_BUF))
    {
        TCGPRN("copy to RK\n");
        DBG_P(0x01, 0x03, 0x710086 );  // copy to RK
        memcpy(&mRawKey[range].dek, &pG3->b.mWKey[range].dek, sizeof(mRawKey[0].dek));
        mRawKey[range].state = (S32) TCG_KEY_UNWRAPPED;
        return;
    }

    if ((mRawKey[range].state == TCG_KEY_UNWRAPPED) && (target==TO_MTBL_KEYTBL))
    {
        TCGPRN("copy to G3\n");
        DBG_P(0x01, 0x03, 0x710087 );  // copy to G3
        memcpy(&pG3->b.mWKey[range].dek, &mRawKey[range].dek, sizeof(mRawKey[0].dek));
        memset(pG3->b.mWKey[range].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
        memset(pG3->b.mWKey[range].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
        pG3->b.mWKey[range].state = (S32) TCG_KEY_UNWRAPPED;
        return;
    }

    if (pG3->b.mWKey[range].state != TCG_KEY_WRAPPED)
    {
        TCGPRN("<UWDEK> !!G3.WK[%2x].state = %x\n", range, pG3->b.mWKey[range].state);
        DBG_P(0x3, 0x03, 0x710088, 1, range, 4, pG3->b.mWKey[range].state);  // <UWDEK> !!G3.WK[%2x].state = %x
        return;
    }

    // Start to Un-Wrap
    if (target == TO_MTBL_KEYTBL)
    {
        TCGPRN("<UWDEK> to G3 Rng=%2x\n", range);
        DBG_P(0x2, 0x03, 0x710089, 1, range);  // <UWDEK> to G3 Rng=%2x
    }
    else
    {
        TCGPRN("<UWDEK> to RK Rng=%2x\n", range);
        DBG_P(0x2, 0x03, 0x71008A, 1, range);  // <UWDEK> to RK Rng=%2x
    }

    aes_key_unwrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.aesKey, 0, (U32*)&WrapBuf[0]);

    aes_key_unwrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.xtsKey, 0, (U32*)&WrapBuf[10]);

#if _TCG_DEBUG
    for (int i = 0; i < 2; i++){
        //printk("-> %x ", WrapBuf[i]);
        DBG_P(0x2, 0x03, 0x71008B, 4, WrapBuf[i]);  // -> %x
    }
#endif
    // Un-Wrap process pass!
    if (target == TO_MTBL_KEYTBL)   // G3
    {
        memcpy(&pG3->b.mWKey[range].dek.aesKey, &WrapBuf[0], sizeof(pG3->b.mWKey[0].dek.aesKey));
        memcpy(&pG3->b.mWKey[range].dek.xtsKey, &WrapBuf[10], sizeof(pG3->b.mWKey[0].dek.xtsKey));
        memset(pG3->b.mWKey[range].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
        memset(pG3->b.mWKey[range].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
        pG3->b.mWKey[range].state = (S32)TCG_KEY_UNWRAPPED;
    }
    else
    {
        memcpy(&mRawKey[range].dek.aesKey, &WrapBuf[0], sizeof(mRawKey[0].dek.aesKey));
        memcpy(&mRawKey[range].dek.xtsKey, &WrapBuf[10], sizeof(mRawKey[0].dek.xtsKey));
        memset(mRawKey[range].dek.icv1, 0, sizeof(mRawKey[0].dek.icv1));
        memset(mRawKey[range].dek.icv2, 0, sizeof(mRawKey[0].dek.icv2));
        mRawKey[range].state = (S32)TCG_KEY_UNWRAPPED;
    }

    memset(WrapBuf, 0, sizeof(WrapBuf));
    #ifdef _TCG_DEBUG
    DumpTcgKeyInfo();
    #endif
*/
}

//*******************************************************************************************
// Update WKey only
//*******************************************************************************************
// 1. If readlockenabled=1 & writelockenabled=1 & G3 key is at unwrap state
//    -> wrap G3 key
// 2. else if the G3 key is at wrap state
//    -> unwrap G3 key
ddr_code void TcgUpdateWrapKey(u32 keyIdx)
{
	tcg_core_trace(LOG_INFO, 0x981a, "TcgUpdateWrapKey not coding yet, Max Pan");
	return;
	/*

    if ((pG3->b.mWKey[keyIdx].state == TCG_KEY_UNWRAPPED)
        && (pG3->b.mLckLocking_Tbl.val[keyIdx].writeLockEnabled)
        && (pG3->b.mLckLocking_Tbl.val[keyIdx].readLockEnabled))
    {
        Tcg_WrapDEK((u8)keyIdx, WrapKEK);
    }
    else if ((pG3->b.mWKey[keyIdx].state == TCG_KEY_WRAPPED)
        && (!pG3->b.mLckLocking_Tbl.val[keyIdx].writeLockEnabled
            || !pG3->b.mLckLocking_Tbl.val[keyIdx].readLockEnabled))
    {
        Tcg_UnWrapDEK(keyIdx, WrapKEK, TO_MTBL_KEYTBL);
    }
*/
}

ddr_code void TcgUpdateWrapKeyList(u32 keyList)
{
	tcg_core_trace(LOG_INFO, 0x25eb, "TcgUpdateWrapKeyList not coding yet, Max Pan");
	return;
/*
    if (keyList)
    {
        for (U32 i=0; i<=LOCKING_RANGE_CNT; i++)
        {
            if (keyList & (0x01<<i))
                TcgUpdateWrapKey(i);
        }
    }
*/
}

// fetch WKey to RawKey
//*******************************************************************************************
//1. If G3 key state is unwrap or null -> copy G3 key to RawKey Array (mRawKey[])
//2. If G3 key state is wrap state(LockEnableds are TRUE) ->
//      (1) readlocked=1 and writelocked=1 => clear raw key
//      (2) else => unwrap G3 key
ddr_code void TcgUpdateRawKey(u32 keyIdx)
{
	tcg_core_trace(LOG_INFO, 0xf7b0, "TcgUpdateRawKey not coding yet, Max Pan");
	return;
/*

    if (pG3->b.mWKey[keyIdx].state == TCG_KEY_WRAPPED)
    {
        // both write_lock_enable and read_lock_enable are TRUE
        if ((pG3->b.mLckLocking_Tbl.val[keyIdx].readLocked)
            && (pG3->b.mLckLocking_Tbl.val[keyIdx].writeLocked))
        {
            memset(&mRawKey[keyIdx].dek, 0, sizeof(mRawKey[0].dek));
            mRawKey[keyIdx].state = (s32)TCG_KEY_NULL;
        }
        else // readLocked or writeLocked is FALSE
        {
            Tcg_UnWrapDEK(keyIdx, WrapKEK, TO_RAW_KEY_BUF);// Un-Wrap to RawKey
        }
    }
    else // the G3 key of the range is in unwrap state or null key state
    {
        memcpy(&mRawKey[keyIdx].dek, &pG3->b.mWKey[keyIdx].dek, sizeof(pG3->b.mWKey[0].dek));
        mRawKey[keyIdx].state = pG3->b.mWKey[keyIdx].state;
    }
*/
}

ddr_code void TcgUpdateRawKeyList(u32 keyList)
{
	tcg_core_trace(LOG_INFO, 0xa870, "TcgUpdateRawKeyList not coding yet, Max Pan");
	return;
/*
    if (keyList)
    {
        for (U32 i=0; i<=LOCKING_RANGE_CNT; i++)
        {
            if (keyList & (0x01<<i))
                TcgUpdateRawKey(i);
        }
    }
*/
}


//check if column index "iCol" is in the ACE "Columns" column or not (Method_Get / Method_Set)
ddr_code int aceColumns_chk(u32 iCol)
{
    u16 i, k;

    for(i=0; i< ACCESSCTRL_ACL_CNT; i++)
    {
        if (aclBackup[i].aclUid == UID_Null)    return zNG;

        if (aclBackup[i].aceColumns[0] == 0x00) return zOK;  // All

        for (k = 1; k <= aclBackup[i].aceColumns[0]; k++)
        {
            if (aclBackup[i].aceColumns[k] == iCol) return zOK;
        }
    }

    return zNG;
}

//-------------------------------------------------------------------------------
// Set / Get SMBR
//-------------------------------------------------------------------------------
ddr_code void Get_SMBR()
{
    mtdGetSmbr_varsMgm_t *p = &mtdGetSmbr_varsMgm;
	u16 i = 0;	
	u32 laaEndAdr = 0;
	u64 Start_lba = 0;

	tcg_core_trace(LOG_INFO, 0x579d, "[TCG] Get_SMBR()");
	tcg_core_trace(LOG_INFO, 0xabe8, "columnBeginAdr|%x, dsRdLen|%x",p->rowBeginAdr,p->smbrRdLen);

	if(p->smbrRdLen == 0)
		return;

    p->laaBeginAdr       = p->rowBeginAdr / LAA_LEN;
	Start_lba			 = p->laaBeginAdr;
    laaEndAdr            = (p->rowBeginAdr + p->smbrRdLen - 1) / LAA_LEN;
    p->laaCnts           = laaEndAdr - p->laaBeginAdr + 1;
	
	tcg_core_trace(LOG_INFO, 0x6d9d, "laaBeginAdr|%x, laaCnts|%x",p->laaBeginAdr,p->laaCnts);

	//*******//
	tcg_nf_SMBRRd(Start_lba<<(DTAG_SHF + DU_CNT_PER_PAGE_SHIFT - host_sec_bitz), (p->laaCnts)<<DU_CNT_PER_PAGE_SHIFT, false);
	//*******//

    for (i = 0; i < p->smbrRdLen; i++) {
        dataBuf[iDataBuf++] = *((u8 *)tcgTmpBuf + (p->rowBeginAdr % LAA_LEN) + i);
	}

	//*******//
	//Set SMBR complete
    //method_complete_post(req, mTRUE);
	//*******//
	return;
}


ddr_code void Set_SMBR()
{
    mtdSetSmbr_varsMgm_t *p = &mtdSetSmbr_varsMgm;
	void * SMBR_buffer = tcgTmpBuf;	
	u64 Start_lba = 0;
	u32 laaEndAdr = 0;

	tcg_core_trace(LOG_INFO, 0xb777, "[TCG] Set_SMBR()");
	tcg_core_trace(LOG_INFO, 0x49b1, "columnBeginAdr|%x, dsWrLen|%x",p->columnBeginAdr,p->smbrWrLen);

	if(p->smbrWrLen == 0)
		return;

    //SMBR_ioCmdReq = mFALSE;
    p->laaBeginAdr       = p->columnBeginAdr / LAA_LEN;
	Start_lba            = p->laaBeginAdr;
    laaEndAdr            = (p->columnBeginAdr + p->smbrWrLen - 1) / LAA_LEN;
    p->laaCnts           = laaEndAdr - p->laaBeginAdr + 1;
	
	tcg_core_trace(LOG_INFO, 0x866c, "laaBeginAdr|%x, laaCnts|%x",p->laaBeginAdr,p->laaCnts);

	//*******//
	//SMBR Read api
	tcg_nf_SMBRRd(Start_lba<<(DTAG_SHF + DU_CNT_PER_PAGE_SHIFT - host_sec_bitz), (p->laaCnts)<<DU_CNT_PER_PAGE_SHIFT, false);
	//*******//

	SMBR_buffer =  tcgTmpBuf + (p->columnBeginAdr % LAA_LEN);
	memcpy((u8 *)SMBR_buffer, p->pbuf, p->smbrWrLen);

	//*******//
	//SMBR Write api 
	tcg_nf_SMBRWr(p->laaBeginAdr,p->laaCnts);
	//*******//

	if (mSessionManager.TransactionState == TRNSCTN_IDLE){
		
		tcg_core_trace(LOG_INFO, 0xe85c, "Shadow MBR commit start");
		tcg_nf_SMBRCommit(p->laaBeginAdr,p->laaCnts);
	}else{
		
		flgs_MChnged.b.SMBR = mTRUE;
	}

	//*******//
	//Set SMBR complete
	//method_complete_post(req, mTRUE);
	//*******//
	return ;
}

//-------------------------------------------------------------------------------
// Set / Get Datastore 
//-------------------------------------------------------------------------------
ddr_code void Get_DataStore()
{
    mtdGetDs_varsMgm_t *p = &mtdGetDs_varsMgm;
	//void * Dts_buffer = tcgTmpBuf;
	mUINT_16 i = 0;
	u32 laaEndAdr = 0;
	
	tcg_core_trace(LOG_INFO, 0xae88, "[TCG] Get_DataStore()");
	tcg_core_trace(LOG_INFO, 0xaf04, "columnBeginAdr|%x, dsRdLen|0x%x",p->rowBeginAdr,p->dsRdLen);

	if(p->dsRdLen == 0)
		return;

    p->laaBeginAdr       = p->rowBeginAdr / LAA_LEN;
    // SMBR_ioCmdReq = FALSE;
    laaEndAdr            = (p->rowBeginAdr + p->dsRdLen - 1) / LAA_LEN;
    p->laaCnts           = laaEndAdr - p->laaBeginAdr + 1;
	
	tcg_core_trace(LOG_INFO, 0x7666, "laaBeginAdr|%x, laaCnts|%x",p->laaBeginAdr,p->laaCnts);

	//*******//
	tcg_nf_DSRd(p->laaBeginAdr,p->laaCnts);
	//*******//

    for (i = 0; i < p->dsRdLen; i++) {
        dataBuf[iDataBuf++] = *((u8 *)tcgTmpBuf + (p->rowBeginAdr % LAA_LEN) + i);
    }

	//*******//
	//Set DTS complete
    //method_complete_post(req, mTRUE);
	//*******//
	return;
}

ddr_code void Set_DataStore()
{
    mtdSetDs_varsMgm_t *p = &mtdSetDs_varsMgm;
	void * Dts_buffer = tcgTmpBuf;	
	u32 laaEndAdr = 0;
	
	tcg_core_trace(LOG_INFO, 0xda09, "[TCG] Set_DataStore()");
	tcg_core_trace(LOG_INFO, 0x435e, "columnBeginAdr|%x, dsWrLen|0x%x",p->columnBeginAdr,p->dsWrLen);

	if(p->dsWrLen == 0)
		return;

    p->laaBeginAdr       = p->columnBeginAdr / LAA_LEN;
    // SMBR_ioCmdReq = FALSE;
    laaEndAdr            = (p->columnBeginAdr + p->dsWrLen - 1) / LAA_LEN;
    p->laaCnts           = laaEndAdr - p->laaBeginAdr + 1;
	
	tcg_core_trace(LOG_INFO, 0xac1d, "laaBeginAdr|%x, laaCnts|%x",p->laaBeginAdr,p->laaCnts);

	//*******//
	//Datastore Read api
	tcg_nf_DSRd(p->laaBeginAdr,p->laaCnts);
	//*******//

	Dts_buffer =  tcgTmpBuf + (p->columnBeginAdr % LAA_LEN);
	memcpy((u8 *)Dts_buffer, p->pbuf, p->dsWrLen);
	
	tcg_nf_DSWr(p->laaBeginAdr,p->laaCnts);
	
	//*******//
	//Datastore Write api 
	if (mSessionManager.TransactionState == TRNSCTN_IDLE){
		tcg_nf_DSCommit(p->laaBeginAdr,p->laaCnts);
	}else{
		
		flgs_MChnged.b.DS = mTRUE;
	}
	
	//*******//

	//*******//
	//Set DTS complete
	//method_complete_post(req, mTRUE);
	//*******//
	return ;

}


// -------------------------------------------------------------------------------
 
ddr_code int Write2Mtable(req_t *req, u8 *tBuf, u32 tLen, u32 setColNo, u8 listIdx)
{
#if 1
    u64 uidAuthClass;
    u8  errCode = 0;
    u8  i = 0;
    u8  y;

	tcg_core_trace(LOG_INFO, 0xd628, "Write2Mtable(), invokingUID|%08x%08x, setColNo|%x, tLen|%x", (u32)(invokingUID.all >> 32), (u32)invokingUID.all, setColNo, tLen);

    switch(invokingUID.all >> 32)
    {
        case UID_ACE>>32:
			
			tcg_core_trace(LOG_INFO, 0x594a, "Set ACE tbl \n");

			if (mSessionManager.SPID.all == UID_SP_Admin)
            {
                errCode = 0x80;
                goto Exit_Write2Mtable;
            }
            else if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                for (y = 0; y < pG3->b.mLckACE_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG3->b.mLckACE_Tbl.val[y].uid)
                        break;
                }
                if (y == pG3->b.mLckACE_Tbl.hdr.rowCnt)
                {
                    errCode = 0x81;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 3:  //BooleanExpr
                    	
                    	tcg_core_trace(LOG_INFO, 0x0509, "Set LK ACE tbl BooleanExpr \n");
                        //ACE_C_PIN_UserX_Set_PIN shall only accept "Admins" and "Admins OR UserX"
                        if ((invokingUID.all & 0xFFFFFFFFFFFFFF00) == UID_ACE_C_PIN_User_Set_PIN)
                        {
                            if (listIdx == 0)
                            {
                                // 1st Auth must be Admins
                                if (*(u64 *)tBuf != UID_Authority_Admins)
                                {
                                    errCode = 0x85;
                                    goto Exit_Write2Mtable;
                                }
                                else
                                {
                                    pG3->b.mLckACE_Tbl.val[y].booleanExpr[1] = 0; //clear next cell
                                }
                            }
                            else if (listIdx == 1)
                            {
                                // must be userMMMM
                                //User UID can only be UserX
                                uidAuthClass = *(u64 *)tBuf & 0xFFFFFFFFFFFFFF00;
                                if (uidAuthClass != UID_Authority_Users)
                                {
                                    errCode = 0x86;
                                    goto Exit_Write2Mtable;
                                }

                                i = *(u64 *)tBuf & 0xFF;
                                if (i != (invokingUID.all & 0xff))
                                {
                                    errCode = 0x87;
                                    goto Exit_Write2Mtable;
                                }
                            }
                            else //too many elements
                            {
                                errCode = 0x88;
                                goto Exit_Write2Mtable;
                            }
                        }
                        else
                        {
                            if (listIdx >= LCK_ACE_BOOLEXPR_CNT)
                            {
                                errCode = 0x8A;
                                goto Exit_Write2Mtable;
                            }

                            uidAuthClass = *(u64 *)tBuf & 0xFFFFFFFFFFFFFF00;
                            i = *(u64 *)tBuf & 0xFF;
                            if (uidAuthClass == UID_Authority_AdminX)
                            {
                                if (i > TCG_AdminCnt)
                                {
                                    errCode = 0x8B;
                                    goto Exit_Write2Mtable;
                                }
                            }
                            else if (uidAuthClass == UID_Authority_Users)
                            {
                                if (i > TCG_UserCnt)
                                {
                                    errCode = 0x8C;
                                    goto Exit_Write2Mtable;
                                }
                            }
                            else if ((*(u64 *)tBuf != UID_Authority_Admins)
                                 &&  (*(u64 *)tBuf != UID_Authority_Anybody))  // EHDD
                            {
                                errCode = 0x8D;
                                goto Exit_Write2Mtable;
                            }

                            // test case D1-1-1-1-13  , (n user_x, 1 admin_x)
                            if (listIdx)
                            {
                                uidAuthClass = *(u64 *)tBuf;
                                for (i = 0; i < listIdx; i++)
                                {
                                    /*if (uidAuthClass == (pG3->b.mLckACE_Tbl.val[y].booleanExpr[i]))
                                    {
                                        errCode = 0x8E;
                                        goto Exit_Write2Mtable;
                                    }*/
                                }
                            }
                            else
                            {
                                // (listIdx==0) // 1st element
                                for (i = 0; i < LCK_ACE_BOOLEXPR_CNT; i++)
                                {
                                    pG3->b.mLckACE_Tbl.val[y].booleanExpr[i] = 0;
                                }
                            }
                        }

                        pG3->b.mLckACE_Tbl.val[y].booleanExpr[listIdx] = *(u64 *)tBuf;
                        flgs_MChnged.b.G3 = mTRUE;
                        break;
                    default:
                        errCode = 0x8F;
                        goto Exit_Write2Mtable;
                }
            }
            break; // case 0x00000008 : ACE table

        case UID_Authority >> 32:


			if (mSessionManager.SPID.all == UID_SP_Admin)
            {
				tcg_core_trace(LOG_INFO, 0x4467, "Set AdmSP Authority tbl \n");
				
                for (y = 0; y < pG1->b.mAdmAuthority_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG1->b.mAdmAuthority_Tbl.val[y].uid)
                        break;
                }
                if (y == pG1->b.mAdmAuthority_Tbl.hdr.rowCnt)
                {
                    errCode = 0x90;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 5: //Enabled

						tcg_core_trace(LOG_INFO, 0xe042, "AdmAuthority_Wr_Enable[%x]\n",*(bool *)tBuf);

                        if(((*(bool *)tBuf) & 0xFFFFFFFE) != 0){
                            errCode = 0x61;
                            goto Exit_Write2Mtable;
                        }
                        pG1->b.mAdmAuthority_Tbl.val[y].enabled = *(bool *)tBuf;
                        flgs_MChnged.b.G1 = mTRUE;
                        break;

                    default:
                        errCode = 0x91;
                        goto Exit_Write2Mtable;
                }
            }
            else if (mSessionManager.SPID.all == UID_SP_Locking)
            {
            
				tcg_core_trace(LOG_INFO, 0xd7e7, "Set LckSP Authority tbl \n");

                for (y = 0; y < pG3->b.mLckAuthority_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG3->b.mLckAuthority_Tbl.val[y].uid)
                        break;
                }
                if (y == pG3->b.mLckAuthority_Tbl.hdr.rowCnt)
                {
                    errCode = 0x92;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 2: //CommonName
                    
                    	tcg_core_trace(LOG_INFO, 0xfa79, "Set LK Authority CommonName \n");

						memset(pG3->b.mLckAuthority_Tbl.val[y].commonName, 0, sizeof(pG3->b.mLckAuthority_Tbl.val[y].commonName));
                        memcpy(pG3->b.mLckAuthority_Tbl.val[y].commonName, tBuf, tLen);
                        flgs_MChnged.b.G3 = mTRUE;
                        break;

                    case 5: //Enabled
						
                    	tcg_core_trace(LOG_INFO, 0x8645, "Set LK Authority Enabled \n");

						if(((*(bool *)tBuf) & 0xFFFFFFFE) != 0){
                            errCode = 0x62;
                            goto Exit_Write2Mtable;
                        }
                        pG3->b.mLckAuthority_Tbl.val[y].enabled = *(bool *)tBuf;
                        flgs_MChnged.b.G3 = mTRUE;
                    #if CO_SUPPORT_AES
                        if (TCG_ACT_IN_OPAL())
                        {
                            uidAuthClass = UID_CPIN + invokingUID.dw[0];
                            for (y = 0; y < pG3->b.mLckCPin_Tbl.hdr.rowCnt; y++)
                            {
                                if (uidAuthClass == pG3->b.mLckCPin_Tbl.val[y].uid)
                                break;
                            }
                            if (pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_Tag == CPIN_NULL)
                            { // no password
                                if (tBuf == mFALSE)
                                {
                                    TcgEraseOpalKEK((u32)uidAuthClass);
                                }
                                else
                                {
                                    TcgWrapOpalKEK(NULL, 0, (u32)uidAuthClass, WrapKEK);
                                }
                            }
                        }
                    #endif
                        break;
                    default:
                        errCode = 0x94;
                        goto Exit_Write2Mtable;
                }
            }
            break;  // case 0x00000009 : Authority table

        case UID_CPIN>>32:
             
            if (mSessionManager.SPID.all == UID_SP_Admin)
            {
				tcg_core_trace(LOG_INFO, 0x3449, "Set AdmSP CPIN tbl \n");
				
                for (y = 0; y < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG1->b.mAdmCPin_Tbl.val[y].uid)
                        break;
                }
                if (y == pG1->b.mAdmCPin_Tbl.hdr.rowCnt)
                {
                    errCode = 0xB0;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 3: // PIN

                    	tcg_core_trace(LOG_INFO, 0xbeeb, "Set Adm CPIN {Pin} \n");
					
                        if (tLen > CPIN_LENGTH)
                        {
                            errCode = 0xB1;
                            goto Exit_Write2Mtable;
                        }
                    #if 0 //TCG_FS_PSID
                        if (pG1->b.mAdmCPin_Tbl.val[y].uid == UID_CPIN_PSID)
                        {    //check if PSID updated
                            if ((CPinMsidCompare(CPIN_PSID_IDX) == zNG) || (CPinMsidCompare(CPIN_SID_IDX) == zNG))
                            {
                                errCode = 0xB2;
                                goto Exit_Write2Mtable;
                            }
                        #if 0  //cjdbg, ToDO
                            else
                            { // copy G1 cTbl to mTbl, other G1 data might be reverted too.
                                TcgMedia_SyInReadTable(TCG_SYIN_G1, TRUE);
                            }
                        #endif
                        }
                    #endif
                    #if 0 //CO_SUPPORT_AES
                        if (pG1->b.mAdmCPin_Tbl.val[y].uid == UID_CPIN_SID)
                        {
                            mSessionManager.HtChallenge[0] = tLen;
                            for (i = 0; i< tLen; i++)
                                mSessionManager.HtChallenge[i+1] = tBuf[i];
                        }
                    #endif

						//************Unwrap Tbl Key to mRawKey*************//
					#ifdef NS_MANAGE
					u32 entry_id = 0, ns_id = 0;
					
						if(ns_array_menu->total_order_now > 1)
						{
							for(i=0; i<ns_array_menu->total_order_now; i++, entry_id++)
							{
								ns_id = ns_array_menu->array_order[i] + 1;

								PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt,sizeof(pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt)
						       					  ,1,32,(u32*)WrapKEK);		//Gen current kek
				
								Tcg_Key_wp_uwp(LOCKING_RANGE_CNT+ns_id, AES_256B_KUWP_NO_SECURE);        //unwrap Tbl key to mRawkey

							}
						}
						else
					#endif
						{
							for (i = 0; i <= LOCKING_RANGE_CNT; i++)
							{
								if((pG3->b.mLckKAES_256_Tbl.val[i].key1[0] == 0) && (pG3->b.mLckKAES_256_Tbl.val[i].key2[0] == 0))
								{
									//There is no key exist in Tbl
									continue;
								}
							
								PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[i].salt,sizeof(pG3->b.mKEKsalt[i].salt)
									              ,1,32,(u32*)WrapKEK);		//Gen current kek
				
								Tcg_Key_wp_uwp(i, AES_256B_KUWP_NO_SECURE);        //unwrap Tbl key to mRawkey
							
								tcg_core_trace(LOG_INFO, 0x55a2, "[Max set 1]mRaw[%d] = %x ",i , mRawKey[i].dek.aesKey[0]);
							}						
						}
						//*************************************************//
						
                        if (tLen)
                        {
							tcg_core_trace(LOG_INFO, 0x5294, "Adm CPIN : %x%x%x%x ... tlen : %x\n",tBuf[0], tBuf[1], tBuf[2], tBuf[3],tLen);

                            memset(&pG1->b.mAdmCPin_Tbl.val[y].cPin, 0, sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin));

						#if 0
							sec_gen_sha3_256_hash(tBuf, tLen, pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_val);
							pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_Tag = CPIN_IN_PBKDF;
						#endif

						//***Processing PBKDF2***//							

							//Tcg_GenCPinHash(tBuf, (u8)tLen, &pG1->b.mAdmCPin_Tbl.val[y].cPin);

							sec_gen_rand_number((u8*)pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_salt,sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin.cPin_salt));  //aes_key   
							
							PKCS5_PBKDF2_HMAC((u32*)tBuf, (u32)tLen, (u32*)pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_salt,sizeof(pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_salt)
												,2500,32,(u32*)pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_val);                 

							pG1->b.mAdmCPin_Tbl.val[y].cPin.cPin_Tag = CPIN_IN_PBKDF;
						
						#if 0 //TCG_FS_PSID
                            if (pG1->b.mAdmCPin_Tbl.val[y].uid == UID_CPIN_PSID)
                            {
                            #if 0 //cjdbg, ToDO
                                //update c-table
                                TcgMedia_SyInWriteTable(TCG_SYIN_G1, TRUE);
                                SyIn_Synchronize(SI_AREA_SECURITY, SYSINFO_WRITE, SI_SYNC_BY_TCG);

                                memcpy(smSysInfo->d.MPInfo.d.PSID, &pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val, CPIN_LENGTH + CPIN_SALT_LEN);
                                smSysInfo->d.MPInfo.d.TagPSID = SI_TAG_TCG;
                                SyIn_Synchronize(SI_AREA_NOR, SYSINFO_WRITE, SI_SYNC_BY_TCG);
                            #else
                                TcgPsidBackup();
                            #endif
                            }
                        #endif
                        }
                        else
                        {
                            memset(&pG1->b.mAdmCPin_Tbl.val[y].cPin, 0, sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin));
                        }
						
						//************Restore Key to Tbl****************//
					#ifdef NS_MANAGE
					
						if(ns_array_menu->total_order_now > 1)
						{
							for(i=0; i<ns_array_menu->total_order_now; i++, entry_id++)
							{
								ns_id = ns_array_menu->array_order[i] + 1;

								PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt,sizeof(pG3->b.mKEKsalt[LOCKING_RANGE_CNT+ns_id].salt)
						       					  ,1,32,(u32*)WrapKEK);		//Gen current kek
				
								Tcg_Key_wp_uwp(LOCKING_RANGE_CNT+ns_id, AES_256B_KWP_NO_SECURE);        //unwrap Tbl key to mRawkey

							}
						}
						else
					#endif
						{
							for (i = 0; i <= LOCKING_RANGE_CNT; i++)
							{
								if((pG3->b.mLckKAES_256_Tbl.val[i].key1[0] == 0) && (pG3->b.mLckKAES_256_Tbl.val[i].key2[0] == 0))
								{
									//There is no key exist in Tbl
									continue;
								}
						
							PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin.cPin_val, 32, (u32*)pG3->b.mKEKsalt[i].salt,sizeof(pG3->b.mKEKsalt[i].salt)
								              ,1,32,(u32*)WrapKEK);		//Gen new kek
				
							Tcg_Key_wp_uwp(i, AES_256B_KWP_NO_SECURE);         //wrap mRawkey to Tbl key
							
							tcg_core_trace(LOG_INFO, 0x19c9, "[Max set 1]mRaw[%d] = %x ",i , mRawKey[i].dek.aesKey[0]);
							}
							
						#ifdef NS_MANAGE
							if(ns_array_menu->total_order_now == 1)
							{
								u8 tar;

								tar = ns_array_menu->array_order[0] + 1 + LOCKING_RANGE_CNT;

								memcpy(&pG3->b.mLckKAES_256_Tbl.val[tar], &pG3->b.mLckKAES_256_Tbl.val[0], sizeof(pG3->b.mLckKAES_256_Tbl.val[0]));
								memcpy(&pG3->b.mKEKsalt[tar].salt, &pG3->b.mKEKsalt[0].salt, sizeof(pG3->b.mKEKsalt[0].salt));				
							}
						#endif
						
						}
						//*********************************************//						

						flgs_MChnged.b.G1 = mTRUE;
						flgs_MChnged.b.G3 = mTRUE;  //Update Tbl key
                        break; // case 3 : PIN

                    default:
                        errCode = 0xB3;
                        goto Exit_Write2Mtable;
                }
            }
            else if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                
                for (y = 0; y < pG3->b.mLckCPin_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG3->b.mLckCPin_Tbl.val[y].uid)
                        break;
                }
                if (y == pG3->b.mLckCPin_Tbl.hdr.rowCnt)
                {
                    errCode = 0xB4;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 3:
			
						tcg_core_trace(LOG_INFO, 0x3c41, "Set LK CPIN tbl Pin \n");
					
                        if (tLen > CPIN_LENGTH)
                        {
                            errCode = 0xB5;
                            goto Exit_Write2Mtable;
                        }

                        if (tLen)
                        {
                        
							tcg_core_trace(LOG_INFO, 0x40e9, "LK CPIN : %x%x%x%x ... \n",tBuf[0], tBuf[1], tBuf[2], tBuf[3]);
						    memset(&pG3->b.mLckCPin_Tbl.val[y].cPin, 0, sizeof(pG3->b.mLckCPin_Tbl.val[y].cPin));

							#if 0 //Processing PBKDF2
							sec_gen_sha3_256_hash(tBuf, tLen, pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_val);
							pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_Tag = CPIN_IN_PBKDF;
							#endif

                            //Tcg_GenCPinHash(tBuf, (u32)tLen, &pG3->b.mLckCPin_Tbl.val[y].cPin);
							sec_gen_rand_number((u8*)pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_salt , sizeof(pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_salt));  //aes_key   
							
							PKCS5_PBKDF2_HMAC((u32*)tBuf, (u32)tLen, (u32*)pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_salt,sizeof(pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_salt)
											    ,2500,32,(u32*)pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_val); 
																			
							pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_Tag = CPIN_IN_PBKDF;

						}
                        else
                        {
                            memset(&pG3->b.mLckCPin_Tbl.val[y].cPin, 0, sizeof(pG3->b.mLckCPin_Tbl.val[y].cPin));
                        }
                    // Change CPIN, Use Old KEK unwrap key and use new KEK wrap again.
                    #if CO_SUPPORT_AES
                        if (TCG_ACT_IN_OPAL())
                        {
                            uidAuthClass = UID_Authority + invokingUID.dw[0];
                            for (y = 0; y < pG3->b.mLckAuthority_Tbl.hdr.rowCnt; y++)
                            {
                                if (uidAuthClass == pG3->b.mLckAuthority_Tbl.val[y].uid)
                                    break;
                            }
                            if ((tLen==0) && (pG3->b.mLckAuthority_Tbl.val[y].enabled==0))
                            { // no password && disabled
                                TcgEraseOpalKEK((u32)uidAuthClass);
                            }
                            else
                            {
                                TcgWrapOpalKEK(tBuf, tLen, (u32)uidAuthClass, WrapKEK);
                            }
                        }
                        else if (TCG_ACT_IN_ALL_SU())   // entire ranges are at single user mode
                        {
                            if (invokingUID.all >= UID_CPIN_User1)
                            {
                                int j = (u8)(invokingUID.all - UID_CPIN_User1);
                                if ((pG3->b.mLckLocking_Tbl.val[j].writeLockEnabled)
                                    && (pG3->b.mLckLocking_Tbl.val[j].readLockEnabled))
                                {
                                    Tcg_UnWrapDEK(j, WrapKEK, TO_MTBL_KEYTBL);
//Not coding yet------------------------------------------------------------------------------
/*
                                    // Use New CPIN to Rewrap Key
                                    HAL_Gen_Key(pG3->b.mWKey[j].salt, sizeof(pG3->b.mWKey[j].salt));
                                    pG3->b.mWKey[j].state = TCG_KEY_UNWRAPPED;
                                    HAL_PBKDF2((U32 *)tBuf, (U32)tLen, pG3->b.mWKey[j].salt, sizeof(pG3->b.mWKey[j].salt), WrapKEK);
*/
//--------------------------------------------------------------------------------------------

                                    Tcg_WrapDEK(j, WrapKEK);
                                }
                            }
                        }
                    #endif
                        flgs_MChnged.b.G3 = mTRUE;
                        break; // case 3

                    default:
                        errCode = 0xB6;
                        goto Exit_Write2Mtable;
                }
            }
            break;  // case 0x0000000B : C_PIN table

        case UID_Locking >> 32:  // Locking

            for (y = 0; y < pG3->b.mLckLocking_Tbl.hdr.rowCnt; y++)
            {
                if (invokingUID.all == pG3->b.mLckLocking_Tbl.val[y].uid)
                    break;
            }
            if (y == pG3->b.mLckLocking_Tbl.hdr.rowCnt)
            {
                errCode = 0xC0;
                goto Exit_Write2Mtable;
            }
#ifdef NS_MANAGE
			if((ns_array_menu->total_order_now > 1) && y!=0)
			{
				errCode = 0xC3;
                goto Exit_Write2Mtable;
			}
#endif
            switch(setColNo)
            {
                case 2:  //CommonName
                	
					tcg_core_trace(LOG_INFO, 0x8c0a, "Set LK Locking tbl CommonName");

					memset(pG3->b.mLckLocking_Tbl.val[y].commonName, 0, sizeof(pG3->b.mLckLocking_Tbl.val[y].commonName));
                    memcpy(pG3->b.mLckLocking_Tbl.val[y].commonName, tBuf, tLen);

                    flgs_MChnged.b.G3 = mTRUE;
                    break;  // case 2 : CommonName

                case 3:  //RangeStart
                
					tcg_core_trace(LOG_INFO, 0xb0e9, "Set LK Locking tbl RangeStart[%x]" , *((u32 *)tBuf));

				#if 0  // boundary need check ULink
                    if (*((U64 *)tBuf) & 0x000000000000000F)
                    {
                        errCode = 0xC1;
                        goto Exit_Write2Mtable;
                    }
                #endif
                    pG3->b.mLckLocking_Tbl.val[y].rangeStart = *((u64 *)tBuf);
                    bLockingRangeChanged = mTRUE;
                    flgs_MChnged.b.G3 = mTRUE;
					bKeyChanged = mTRUE;
                    break;  // case 3 : RangeStart

                case 4:  //RangeLength

					tcg_core_trace(LOG_INFO, 0x412f, "Set LK Locking tbl RangeLength[%x]" , *((u32 *)tBuf));

                #if 0  // boundary need check ULink
                    if(*((U64 *)tBuf) & 0x000000000000000F)
                    {
                        errCode = 0xC2;
                        goto Exit_Write2Mtable;
                    }
                #endif
                    pG3->b.mLckLocking_Tbl.val[y].rangeLength = *((u64 *)tBuf);
                    bLockingRangeChanged = mTRUE;
                    flgs_MChnged.b.G3 = mTRUE;
					bKeyChanged = mTRUE;
                    break;  // case 4 : RangeLength

                case 5:  //ReadLockEnable

					tcg_core_trace(LOG_INFO, 0xd062, "Set LK Locking tbl ReadLockEnable[%x]" , tBuf[0]);

                    pG3->b.mLckLocking_Tbl.val[y].readLockEnabled = tBuf[0];

                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        TcgUpdateWrapKey(y);
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = mTRUE;
                    break;  // case 5 :ReadLockEnable

                case 6:  //WriteLockEnable
                
					tcg_core_trace(LOG_INFO, 0x852a, "Set LK Locking tbl WriteLockEnable[%x]" , tBuf[0]);

                    pG3->b.mLckLocking_Tbl.val[y].writeLockEnabled=tBuf[0];

                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        TcgUpdateWrapKey(y);
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = mTRUE;
                    break;  // case 6: WriteLockEnable

                case 7:  //ReadLock
                
					tcg_core_trace(LOG_INFO, 0x6928, "Set LK Locking tbl ReadLock[%x]" , tBuf[0]);

					pG3->b.mLckLocking_Tbl.val[y].readLocked=tBuf[0];

                    //no need to write ReadLock to NAND if lockOnReset is PowerCycle and readLockEnabled=1
                    //(since fw will auto set ReadLock=TRUE)
                    //if((pG3->b.mLckLocking_Tbl.val[y].readLockEnabled==0)
                    // ||(pG3->b.mLckLocking_Tbl.val[y].lockOnReset[1]!=PowerCycle))
                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = mTRUE;
                    break;  // case 7 : ReadLock

                case 8:  //WriteLock

					tcg_core_trace(LOG_INFO, 0x969a, "Set LK Locking tbl WriteLock[%x]" , tBuf[0]);

                    pG3->b.mLckLocking_Tbl.val[y].writeLocked=tBuf[0];

                    //if((pG3->b.mLckLocking_Tbl.val[y].writeLockEnabled==0)
                    // ||(pG3->b.mLckLocking_Tbl.val[y].lockOnReset[1]!=PowerCycle))
                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = mTRUE;
                    break;  // case 8: WriteLock

                case 9:  //LockOnReset, { PowerCycle } or { PowerCycle, Programmatic }
					
					tcg_core_trace(LOG_INFO, 0xf413, "Set LK Locking tbl LockOnReset[%x]",tBuf[0]);

					if (listIdx == 0)
                    {
                        if (*tBuf != PowerCycle)
                        {
                            errCode = 0xC7;
                            goto Exit_Write2Mtable;
                        }
                        memset(pG3->b.mLckLocking_Tbl.val[y].lockOnReset, 0, sizeof(pG3->b.mLckLocking_Tbl.val[y].lockOnReset));
                    }
                    else if (listIdx == 1)
                    {
                        if (*tBuf != Programmatic)
                        {
                            errCode = 0xC8;
                            goto Exit_Write2Mtable;
                        }
                    }
                    else
                    {
                        errCode = 0xC9; goto Exit_Write2Mtable;
                    }

                    i = listIdx +1;
                    pG3->b.mLckLocking_Tbl.val[y].lockOnReset[0] = i;
                    pG3->b.mLckLocking_Tbl.val[y].lockOnReset[i] = tBuf[0];
                    flgs_MChnged.b.G3 = mTRUE;
                    break;
                default:
                    errCode = 0xCA;
                    goto Exit_Write2Mtable;
            }   // switch (setColNo)
            break;  // case 0x00000802 : Locking
            
        case UID_MBRControl>>32:    // MBRControl

			tcg_core_trace(LOG_INFO, 0x7fee, "Set MBRctl tbl");

			for (y = 0; y < pG3->b.mLckMbrCtrl_Tbl.hdr.rowCnt; y++)
            {
                if (invokingUID.all == pG3->b.mLckMbrCtrl_Tbl.val[y].uid)
                    break;
            }
            if (y == pG3->b.mLckMbrCtrl_Tbl.hdr.rowCnt)
            {
                errCode = 0xD0;
                goto Exit_Write2Mtable;
            }
            switch(setColNo)
            {
                case 1:  //Enable

					tcg_core_trace(LOG_INFO, 0x0903, "Set MBRctl_Enable[%x]",tBuf[0]);

                    pG3->b.mLckMbrCtrl_Tbl.val[y].enable = tBuf[0];
                    flgs_MChnged.b.G3 = mTRUE;
                    #ifndef alexcheck 
					//Max Modify
					//ResetAllCache();       //note : ResetAllCache() = {}
                    #endif
                    break;

                case 2:  //Done
                    
					tcg_core_trace(LOG_INFO, 0x4cb2, "Set MBRctl_Done[%x]",tBuf[0]);

                    pG3->b.mLckMbrCtrl_Tbl.val[y].done = tBuf[0];

                    //no need to write "Done' to NAND if doneOnReset is PowerCycle and Enable=1
                    //(since fw will auto set Done=FALSE)
                    if((pG3->b.mLckMbrCtrl_Tbl.val[y].enable == 0))
                     //||(pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset!=PowerCycle))
                    {
                        flgs_MChnged.b.G3 = mTRUE;
                    }
                    #ifndef alexcheck
					//Max modify
					//ResetAllCache();       //note : ResetAllCache() = {}
                    #endif
                    break;

                case 3:  // DoneOnReset
                    if (listIdx == 0)
                    {
                        if (*tBuf != PowerCycle)
                        {
                            errCode = 0xD1;
                            goto Exit_Write2Mtable;
                        }
                    }
                    else if (listIdx == 1)
                    {
                        if (*tBuf != Programmatic)
                        {
                            errCode = 0xD2;
                            goto Exit_Write2Mtable;
                        }
                        memset(pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset, 0, sizeof(pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset));
                    }
                    else
                    {
                        errCode = 0xD3;
                        goto Exit_Write2Mtable;
                    }
                    i = listIdx + 1;
                    pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset[0] = i;
                    pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset[i] = tBuf[0];
                    flgs_MChnged.b.G3 = mTRUE;
                    break;  // case 3 : DoneOnReset

                default:
                    errCode = 0xD4;
                    goto Exit_Write2Mtable;
            }
            break;  // case 0x00000803 : MBRControl

        case UID_MBR>>32:  // @LockingSP
        {
#if 1
			tcg_core_trace(LOG_INFO, 0x5e2a, "Set MBR");

			#if 0
            U32 LaaAddr, LaaOffAddr, wrptr;
            int rlen;
            #endif

			tcg_core_trace(LOG_INFO, 0x1d90, "Set MBR_StartCol[%x] tLen[%x]",setColNo,tLen);

            if ((setColNo + tLen) > MBR_LEN)
            {
                errCode = 0xD5;
                goto Exit_Write2Mtable;
            }

            mtdSetSmbr_varsMgm.pbuf           = tBuf;
            mtdSetSmbr_varsMgm.smbrWrLen      = tLen;
            mtdSetSmbr_varsMgm.columnBeginAdr = setColNo;

            #if 1 //Max Modify

			flgs_MChnged.b.SMBR = mTRUE;
            
            //cb_setSmbr(Begin)(req);
            #else
            rlen = tLen;              //rlen = wr remain length
            wrptr = setColNo;         //wr point
            if (rlen > 0)
            {
                U32 LaaCnt;
                // U8 *Desptr = (U8 *)tcgTmpBuf;

                LaaAddr = wrptr / LAA_LEN;
                LaaOffAddr = wrptr % LAA_LEN;
                // DBG_P(3, 3, 0x8201A3, 4, LaaAddr, 4, LaaOffAddr);  //82 01 A3, "LaaAddr[%X] LaaOffAddr[%X]", 4 4
                SMBR_ioCmdReq = FALSE;

                LaaCnt = (LaaOffAddr+rlen) / LAA_LEN;
                if ((LaaOffAddr + rlen) % LAA_LEN) LaaCnt += 1;
                // DBG_P(3, 3, 0x8201A4, 4, (U32)rlen, 4, LaaCnt);  //82 01 A4, "rlen[%X] LaaCnt[%X]", 4 4

                //WaitMbrRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 6 page for max transfer buffer
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt * LAA_LEN /*sizeof(tcgTmpBuf)*/);
                if(LaaOffAddr || ((setColNo+tLen)%LAA_LEN))
                {
            #if 1
                    U16 ii;
                    bool HasBlank = FALSE;

                    for (ii = LaaAddr; ii < LaaAddr + LaaCnt; ii++)
                    {
                        if ((pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START+ii].blk) >= (TCG_MBR_CELLS)){
                            HasBlank = TRUE;
                            break;
                        }
                    }

                    if (HasBlank)
                    {
                        for (ii = LaaAddr; ii < LaaAddr + LaaCnt; ii++){
                            if ((pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START+ii].blk) < (TCG_MBR_CELLS)
                             || (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START+ii].blk) < (TCG_MBR_CELLS))   //blank ?
                            {
                                SMBR_Read(ii, ii+1, (U8 *)tcgTmpBuf + (ii-LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                            }
                        }
                    }
                    else
                    {
                        SMBR_Read(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   //read all
                    }
            #else
                    SMBR_Read(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   //WaitMbrRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 1 page
            #endif
                }
                //memcpy((U8 *)&Desptr[LaaOffAddr], &tBuf[srcptr], rlen);
                //TcgFuncRequest2(MSG_TCG_SMBRWR, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);    //WaitMbrWr(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);
                SMBR_Write(LaaAddr, LaaAddr+LaaCnt, tcgTmpBuf, LaaOffAddr, tLen, tBuf);
                flgs_MChnged.b.SMBR = TRUE;
            }
            #endif   // #if 1
        }
        break;  // case 0x00000804 : MBR

        case UID_DataStore >> 32:   // @LockingSP
        case UID_DataStore2 >> 32:
        case UID_DataStore3 >> 32:
        case UID_DataStore4 >> 32:
        case UID_DataStore5 >> 32:
        case UID_DataStore6 >> 32:
        case UID_DataStore7 >> 32:
        case UID_DataStore8 >> 32:
        case UID_DataStore9 >> 32:
        {
            y = ((invokingUID.all >> 32) & 0xff) - 1;
			
			tcg_core_trace(LOG_INFO, 0x5a83, "Set DTS : sCol[0x%x] Len[%x] UserX[%x] UserXOffset[%x] UserXLen[%x]",setColNo,tLen,mDataStoreAddr[y].offset,mDataStoreAddr[y].length);

            if ((setColNo + tLen) > mDataStoreAddr[y].length /*|| tLen > DECBUF_LEN*/)
            {
                errCode = 0xE0;
                goto Exit_Write2Mtable;
            }

            mtdSetDs_varsMgm.pbuf           = tBuf;
            mtdSetDs_varsMgm.dsWrLen        = tLen;
            mtdSetDs_varsMgm.columnBeginAdr = setColNo + mDataStoreAddr[y].offset;

            #if 1 //Max modify

			flgs_MChnged.b.DS = mTRUE;
			
            //cb_setDs(Begin)(req);
            #else
            // for test case A13-2-1-3-9
            if (tLen > 0)
            {
                U32 LaaAddr, LaaOffAddr;
                U32 LaaCnt;
                // U8 *Desptr=(U8 *)tcgTmpBuf;

                setColNo += mDataStoreAddr[y].offset;
                LaaAddr = setColNo / LAA_LEN;
                LaaOffAddr = setColNo % LAA_LEN;  //offset
                // DBG_P(3, 3, 0x8201A3, 4, LaaAddr, 4, LaaOffAddr);  //82 01 A3, "LaaAddr[%X] LaaOffAddr[%X]", 4 4

                LaaCnt = (LaaOffAddr+tLen)/LAA_LEN;
                if((LaaOffAddr + tLen) % LAA_LEN) LaaCnt += 1;
                // DBG_P(3, 3, 0x8201A6, 4, LaaAddr, 4, LaaAddr+LaaCnt-1);  //82 01 A6, "DS Wr slaa[%X] elaa[%X]", 4 4

                //WaitDSRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 6 page for max transfer buffer
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt * LAA_LEN /*sizeof(tcgTmpBuf)*/);
                if(LaaOffAddr || ((setColNo+tLen) % LAA_LEN))
                {
                #if 1
                    U16 ii;
                    bool HasBlank = FALSE;

                    for(ii = LaaAddr; ii < LaaAddr+LaaCnt; ii++)
                    {
                        if((pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START+ii].blk) >= (TCG_MBR_CELLS))
                        {
                            HasBlank = TRUE;
                            break;
                        }
                    }
                    if (HasBlank)
                    {
                        for(ii = LaaAddr; ii < LaaAddr + LaaCnt; ii++)
                        {
                            if((pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START+ii].blk) < (TCG_MBR_CELLS) ||
                               (pG4->b.TcgMbrL2P[TCG_DS_LAA_START+ii].blk) < (TCG_MBR_CELLS))   //blank ?
                            {
                                TcgFuncRequest2(MSG_TCG_DSRD, ii, ii+1, (U8 *)tcgTmpBuf + (ii-LaaAddr)*CFG_UDATA_PER_PAGE);    //read 1 page
                            }
                        }
                    }
                    else
                    {
                        TcgFuncRequest2(MSG_TCG_DSRD, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   // read all
                    }
                #else
                    TcgFuncRequest2(MSG_TCG_DSRD, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);    //WaitDSRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 1 page
                #endif
                }
                //memcpy((U8 *)&Desptr[LaaOffAddr], &tBuf[0], tLen);
                //TcgFuncRequest2(MSG_TCG_DSWR, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);    //WaitDSWr(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);
                DS_Write(LaaAddr, LaaAddr + LaaCnt, tcgTmpBuf, LaaOffAddr, tLen, tBuf);
                flgs_MChnged.b.DS = TRUE;
            }
            #endif // #if 1
        }
        break;  // case 0x00001009:
#endif
        case UID_TPerInfo >> 32:  // @AdminSP
            //if (invokingUID.all != 0x0000020100030001)
            //{
            //    errCode = 0x20;
            //    goto Exit_Write2Mtable;
            //}

            switch(setColNo)
            {
                case 0x08:  //ProgrammaticResetEnable
					
					tcg_core_trace(LOG_INFO, 0x32d0, "Set LK TperInfo tbl ProgrammaticResetEnable: %d", tBuf[0]);

                    pG1->b.mAdmTPerInfo_Tbl.val[0].preset = tBuf[0];
                    flgs_MChnged.b.G1 = mTRUE;
                    break;
                default:
                    errCode = 0xF0;
                    goto Exit_Write2Mtable;
            }
            break;
#if 0
		#if _TCG_ == TCG_PYRITE
		
        case UID_RemovalMechanism >> 32:
            sDBG2(setColNo, tBuf[0], UID_RemovalMechanism);
            switch(setColNo)
            {
                case 0x01: // data_removal_mechanism
                    if(tBuf[0] > 5){  // out of definition of spec.
                        errCode = 0x61;
                        goto Exit_Write2Mtable;
                    }

                    U32 set_act_rm_msm = 1 << tBuf[0];
                    if(set_act_rm_msm & SupportDataRemovalMechanism){
                        if(set_act_rm_msm != pG1->b.mAdmRemovalMsm_Tbl.val[0].activeRM){
                            pG1->b.mAdmRemovalMsm_Tbl.val[0].activeRM = (U32)tBuf[0];
                            flgs_MChnged.b.G1 = TRUE;
                        }

                    }else{
                        errCode = 0x63;
                        goto Exit_Write2Mtable;
                    }
                    break;

                default:
                    errCode = 0x64;
                    goto Exit_Write2Mtable;
            }
            break;
        #endif
#endif

        default:
            errCode = 0xF1;
            goto Exit_Write2Mtable;

	}

    return zOK;

Exit_Write2Mtable:
	
	tcg_core_trace(LOG_INFO, 0x6d2f, "Write2Mtable() errCode|%x\n" , errCode);

    return zNG;
#endif
}


//
// Compared the intended Locking Table uid/rangeStart/rangeLength with the other ranges.
// return zOK if there is no overlap with the other effective ranges (i.e., its rangeLength should not be zero),
// otherwise return zNG.
//
ddr_code int LockingTbl_RangeChk(u64 uid, u64 rangeStart, u64 rangeLength)
{
    u64 rangeEnd = rangeStart;
    u64 tmpRangeStart, tmpRangeLength, tmpRangeEnd;
    u8 i;
	u8 err_code = 0;
	u64 alignment = (TCG_AlignmentGranularity >> host_sec_bitz) - 1;
    // DBG_P(1, 3, 0x820139);  //82 01 39, "[F]LockingTbl_RangeChk"

    //if(rangeStart%(CFG_UDATA_PER_PAGE/LBA_SIZE_IN_BYTE))
    if (rangeStart & alignment)
    {
        // tcg_soutb(0xF0);    tcg_sout64(rangeStart); //tcg_souts("!!NG: StartAlignment");
#if _TCG_ == TCG_EDRV
        if (bEHddLogoTest==mFALSE)
        {
#if 1  // for new version HLK(1703) Fuzz fail || HLK1607 Crossing fail
            if ((mPsidRevertCnt > 0x10) || ((mPsidRevertCnt > 5) && (rangeStart == 0x14) && (rangeLength == 0x14)))
            {
                bEHddLogoTest = mTRUE;
            }
        #else  // old version HLK
            if ((mPsidRevertCnt > 5) && (rangeStart==0x14) && (rangeLength==0x14))
                bEHddLogoTest = TRUE;  //LogoTest, skip alignment check
        #endif
        }

        if (bEHddLogoTest == mFALSE)
#endif
		tcg_core_trace(LOG_ERR, 0x5491, "[TCG] start range: 0x%x_%x NOT align", (rangeStart>>32), rangeStart);
		return zNG; //not align
    }

    //if(rangeLength%(CFG_UDATA_PER_PAGE/LBA_SIZE_IN_BYTE))
    if(rangeLength & alignment)
    {
#if _TCG_ == TCG_EDRV
        if (bEHddLogoTest == mFALSE)
#endif
		tcg_core_trace(LOG_ERR, 0xeb88, "[TCG] range len: 0x%x_%x NOT align", (rangeLength>>32), rangeLength);
		return zNG; // not align
    }

    if (rangeLength) // !=0
        rangeEnd += (rangeLength - 1);
    else
    {
        return zOK; //TODO: need to check... No LBAs are covered by this range (please check Test Case 3.3.4, D4-1-3-3-1)
    }

    for (i = 1; i <= LOCKING_RANGE_CNT; i++)   // skip GloblaRange here
    {
        if(uid==pG3->b.mLckLocking_Tbl.val[i].uid)
            continue;

        tmpRangeStart = pG3->b.mLckLocking_Tbl.val[i].rangeStart;
        tmpRangeLength = pG3->b.mLckLocking_Tbl.val[i].rangeLength;

        if (tmpRangeLength == 0) // No LBAs are covered by this range (please check Test Case 3.3.4, D4-1-3-3-1)
            continue;

        tmpRangeEnd = tmpRangeStart + (tmpRangeLength - 1);

        // check if there is any overlap between these two ranges:
        //  a. rangeStart is in this range
        //  b. rangeEnd is in this range
        //  c. rangeStart is smaller than tmpRangeStart and rangeEnd is larger than tmpRangeEnd
        if ((rangeStart >= tmpRangeStart) && (rangeStart <= tmpRangeEnd))
        {		
			err_code = 0x10;
			goto ERR_INFO_DUMP;
        }
        if ((rangeEnd >= tmpRangeStart) && (rangeEnd <= tmpRangeEnd))
        {
			err_code = 0x11;
			goto ERR_INFO_DUMP;
        }
        if ((rangeStart <= tmpRangeStart) && (rangeEnd >= tmpRangeEnd))
        {
			err_code = 0x12;
			goto ERR_INFO_DUMP;
        }
    }

    return zOK;

ERR_INFO_DUMP:
	tcg_core_trace(LOG_ERR, 0xc8ba, "[TCG] range Err Code: 0x%x in R[%d]", err_code, i);
	tcg_core_trace(LOG_ERR, 0xed66, "[TCG] Check range start: 0x%x_%x", (tmpRangeStart  >>32), tmpRangeStart);
	tcg_core_trace(LOG_ERR, 0x88a9, "[TCG] Check range len:   0x%x_%x", (tmpRangeLength >>32), tmpRangeLength);
	tcg_core_trace(LOG_ERR, 0x39ee, "[TCG] Input range start: 0x%x_%x", (rangeStart  >>32), rangeStart);
	tcg_core_trace(LOG_ERR, 0x1673, "[TCG] Input range len:   0x%x_%x", (rangeLength >>32), rangeLength);
	return zNG;
}

ddr_code void DumpRangeInfo(void)
{
    u16 i;

    for(i=0; i<=LOCKING_RANGE_CNT; i++)
    {
        tcg_core_trace(LOG_INFO, 0x9be0, "%x R:%x W:%x *%x ~ &%x %x\n",
                pLockingRangeTable[i].rangeNo,
                pLockingRangeTable[i].readLocked,
                pLockingRangeTable[i].writeLocked,
                (u32)pLockingRangeTable[i].rangeStart, (u32)pLockingRangeTable[i].rangeEnd,
                pLockingRangeTable[i].blkcnt);

        if(pLockingRangeTable[i].rangeNo==0x00)
            break;
    }

    //crypto_dump_range();
}


ddr_code int CPinMsidCompare(u8 cpinIdx)
{
    u16 i;
    int result = zOK;
	
    if(pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_Tag == CPIN_IN_PBKDF)
    {
		tcg_core_trace(LOG_INFO, 0x597a, "Encrypted Cpin compare");
        u8 digest[CPIN_LENGTH] = { 0 };
/*		
		sec_gen_sha3_256_hash((u8 *)pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, digest);
*/
		PKCS5_PBKDF2_HMAC((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, (u32*)pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_salt,sizeof(pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_salt)
						  ,2500,32,(u32*)digest);				  

/*
        HAL_PBKDF2((u32*)pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val,
                    CPIN_MSID_LEN,
                    (u32*)pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_salt,
                    sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin.cPin_salt),
                    (u32 *)digest);
*/
        for (i = 0; i < CPIN_LENGTH; i++)
        {
            if (pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[i] != digest[i])
            {
                result = zNG;
            }
        }
    }
    else
    {
		tcg_core_trace(LOG_INFO, 0x9894, "Unencrypted Cpin compare");

        for (i = 0; i < CPIN_LENGTH; i++)
        {
            if (pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[i] != pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val[i])
            {
                result = zNG;
            }
        }
    }

    if (result == zNG)
    {
		tcg_core_trace(LOG_INFO, 0x5b90, "NG \n");
    }
    else{
		tcg_core_trace(LOG_INFO, 0xd7e0, "pass \n");
    }
    return result;

}


ddr_code void Tcg_GenCPinHash(u8 *pSrc, u8 srcLen, sCPin *pCPin)
{
	tcg_core_trace(LOG_INFO, 0xc903, "Tcg_GenCPinHash not coding yet, Max Pan");
	return;

/*
#if _TCG_DEBUG
    memset((u8 *)pCPin->cPin_salt, 0, sizeof(pCPin->cPin_salt));
#else
    HAL_Gen_Key((u32 *)pCPin->cPin_salt, sizeof(pCPin->cPin_salt));
#endif
    HAL_PBKDF2((u32 *)pSrc,                 // pwd src
                 (u32)srcLen,               // pwd len
                 (u32 *)pCPin->cPin_salt,   // Salt val
                 sizeof(pCPin->cPin_salt),
                 (u32 *)pCPin->cPin_val);   // dest

    pCPin->cPin_Tag = CPIN_IN_PBKDF;
*/
}




ddr_code void tcg_if_post_sync_response(void)
{
    // pLockingRangeTable      = (enabledLockingTable_t *)((U32)mLockingRangeTable + (U32)CPU1_BTCM_SYS_BASE - (U32)CPUx_BTCM_SYS_BASE);
    tcg_sync.b.nf_sync_req  = mFALSE;
    tcg_sync.b.if_sync_resp = mTRUE;
}

// reset CPin tries count
ddr_code void CPinTbl_Reset(void)
{
    u8 j;

    for (j = 0; j < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; j++)
    {
        pG1->b.mAdmCPin_Tbl.val[j].tries = 0;
    }

    for (j = 0; j < pG3->b.mLckCPin_Tbl.hdr.rowCnt; j++)
    {
        pG3->b.mLckCPin_Tbl.val[j].tries = 0;
    }
}

//
//MbrCtrlTbl Power-On or Porgrammatic Reset
//
//  set 'Done' bit according to reset type and DoneOnReset setting
//
ddr_code void MbrCtrlTbl_Reset(u8 type)
{
    u8 i;

    for(i=1;i<=pG3->b.mLckMbrCtrl_Tbl.val[0].doneOnReset[0];i++)
    {
        if(pG3->b.mLckMbrCtrl_Tbl.val[0].doneOnReset[i]==type)
        {
            pG3->b.mLckMbrCtrl_Tbl.val[0].done=mFALSE;
            break;
        }
    }

    if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable==mTRUE)
     &&(pG3->b.mLckMbrCtrl_Tbl.val[0].done==mFALSE)) 
    {    
    	mTcgStatus |= MBR_SHADOW_MODE;
    }else{
        mTcgStatus &= (~MBR_SHADOW_MODE);
	}

	tcg_core_trace(LOG_INFO, 0x2480, "MbrCtrlTbl_Reset() - TcgStatus|%x , Enable|%x , Done|%x", mTcgStatus, pG3->b.mLckMbrCtrl_Tbl.val[0].enable, pG3->b.mLckMbrCtrl_Tbl.val[0].done);
	
    return;

}

//
//LockingTbl Power-On Reset
//      It will check in each range and
// i) set 'ReadLocked' bit if 'ReadLockEnabled' is set,
// ii) set 'WriteLocked' bit if 'WriteLockEnabled' is set
//
ddr_code void LockingTbl_Reset(u8 type)
{
    u8 i, j;

   for(i=0;i<=LOCKING_RANGE_CNT;i++)
   {
       for(j=1;j<=pG3->b.mLckLocking_Tbl.val[i].lockOnReset[0];j++)
       {
           if(pG3->b.mLckLocking_Tbl.val[i].lockOnReset[j]==type)
           {
               if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled)
                   pG3->b.mLckLocking_Tbl.val[i].readLocked=mTRUE;

               if (pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled)
                   pG3->b.mLckLocking_Tbl.val[i].writeLocked=mTRUE;

#if 1  //Max modify
				// Fetch range key...

				//memcpy(&mRawKey[i].dek.aesKey, &pG3->b.mLckKAES_256_Tbl.val[i].key1, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key1));
				//memcpy(&mRawKey[i].dek.xtsKey, &pG3->b.mLckKAES_256_Tbl.val[i].key2, sizeof(pG3->b.mLckKAES_256_Tbl.val[0].key2));
				//mRawKey[i].state = TCG_KEY_UNWRAPPED;
				
				//tcg_core_trace(LOG_INFO, 0, "<mRawKey1> %x - %x", mRawKey[i].dek.aesKey[0], mRawKey[i].dek.aesKey[1]);
				//tcg_core_trace(LOG_INFO, 0, "<mRawKey2> %x - %x", mRawKey[i].dek.xtsKey[0], mRawKey[i].dek.xtsKey[1]);
#else	
	#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE
                // Fetch range key...
                if (pG3->b.mWKey[i].state == TCG_KEY_UNWRAPPED)
                {
                    memcpy(&mRawKey[i].dek, &pG3->b.mWKey[i].dek, sizeof(pG3->b.mWKey[0].dek));
                    mRawKey[i].state = pG3->b.mWKey[i].state;
                }
                else
                {
                    memset(&mRawKey[i].dek, 0, sizeof(pG3->b.mWKey[0].dek));
                    mRawKey[i].state = (s32)TCG_KEY_NULL;
                }	
				tcg_core_trace(LOG_INFO, 0xf24b, "<RK> %02x-%08x-%08x\n", (u8)mRawKey[i].state, mRawKey[i].dek.aesKey[0], mRawKey[i].dek.aesKey[1]);
	#endif
#endif
                break;
            }
        }
    }

}

ddr_code void tcg_disable_mbrshadow(void)
{
    u64 tmp64;
    u8  i, j;

    //1. update LckTableTbl, disable "UID_MBRControl" and "UID_MBR"
    for (i=0; i<pG2->b.mLckTbl_Tbl.hdr.rowCnt; i++)
    {
        tmp64 = pG2->b.mLckTbl_Tbl.val[i].uid;
        if ((tmp64 == UID_Table_MBRControl) || (tmp64 == UID_Table_MBR))
        {
            pG2->b.mLckTbl_Tbl.val[i].uid = tmp64 | UID_FF;
        }
    }

    //2. update LckAccessCtrlTbl,  disable UID_ACE_MBRContrl...,
    for (j=0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.ace)/sizeof(sAxsCtrl_TblObj); j++)
    {
        tmp64 = pG2->b.mLckAxsCtrl_Tbl.ace[j].invID;
        if ((tmp64==UID_ACE_MBRControl_Admins_Set) || (tmp64==UID_ACE_MBRControl_Set_Done))
        {
            pG2->b.mLckAxsCtrl_Tbl.ace[j].mtdID= ~UID_MethodID_Get;
        }
    }

    //3. update LckAccessCtrlTbl,  disable UID_MBRContrl...,
    for (j=0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.mbrCtrl)/sizeof(sAxsCtrl_TblObj); j++)
    {
        tmp64 = pG2->b.mLckAxsCtrl_Tbl.mbrCtrl[j].invID;
        if (tmp64==UID_MBRControl)
        {
            pG2->b.mLckAxsCtrl_Tbl.mbrCtrl[j].mtdID= ~UID_MethodID_Get;
        }
    }

    //4. update LckAccessCtrlTbl,  disable UID_MBR...,
    for (j=0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.mbr)/sizeof(sAxsCtrl_TblObj); j++)
    {
        tmp64 = pG2->b.mLckAxsCtrl_Tbl.mbr[j].invID;
        if (tmp64==UID_MBR)
        {
            pG2->b.mLckAxsCtrl_Tbl.mbr[j].mtdID= ~UID_MethodID_Get;
        }
    }

    //5. update LckAceTbl?
    for(i=0; i<pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
    {
        tmp64 = pG3->b.mLckACE_Tbl.val[i].uid;
        if ((tmp64 == UID_ACE_MBRControl_Admins_Set) || (tmp64 == UID_ACE_MBRControl_Set_Done))
        {
            pG3->b.mLckACE_Tbl.val[i].uid = tmp64|UID_FF;
        }
    }
}


ddr_code void tcg_restore_lockOnReset_params(void)
{
    u32 i;

    // CPIN
    for (i = 0; i < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; i++){
        pG1->b.mAdmCPin_Tbl.val[i].tries = bak_AdmCpin_tries[i];
    }
    for (i = 0; i < pG3->b.mLckCPin_Tbl.hdr.rowCnt; i++){
         pG3->b.mLckCPin_Tbl.val[i].tries = bak_LckCpin_tries[i];
    }
//Not coding yet--------------//
/*
	//MBR Control
    pG3->b.mLckMbrCtrl_Tbl.val[0].done = bak_LckMbrCtrl_done;
    mTcgStatus = bak_mTcgStatus;
*/
//----------------------------//

    // Locking
    for(i = 0; i <= LOCKING_RANGE_CNT; i++){
        pG3->b.mLckLocking_Tbl.val[i].readLocked = bak_LckLocking_readLocked[i];
        pG3->b.mLckLocking_Tbl.val[i].writeLocked = bak_LckLocking_writeLocked[i];
        #if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE
        memcpy(&mRawKey[i].dek, &bak_RawKey_dek[i], sizeof(pG3->b.mWKey[0].dek));
        mRawKey[i].state = bak_RawKey_state[i];
        #endif
    }
}

ddr_code void DataStore_Setting(u8 cnt, u32 * DSTblSize)
{
    u32 tmp32;
    u16 j, k = 0;
    u8 i, tmp8;
	
	tcg_core_trace(LOG_INFO, 0x8b0b, "DataStore_Setting(), cnt|%x",cnt);

    if (cnt)
    { //with additional DataStore
        //1. update TableTbl, some table objects are enabled
        for (i = 0; i<pG2->b.mLckTbl_Tbl.hdr.rowCnt; i++)
        {
            tmp32 = ((u32)pG2->b.mLckTbl_Tbl.val[i].uid) & 0xfffffff0;
            if(tmp32==0x1000)
            { //DataStore
                tmp8 = ((u8)pG2->b.mLckTbl_Tbl.val[i].uid) - 1;

                if (tmp8<cnt)
                {
                    pG2->b.mLckTbl_Tbl.val[i].uid = UID_Table_DataStore + tmp8;
                    pG2->b.mLckTbl_Tbl.val[i].rows = DSTblSize[tmp8];
                }
            }
        }

        if(cnt>1)
        {
            //2. update AccessCtrlTbl,  need to enable some rows, no need to update row count.
            for (j = 0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.table) / sizeof(sAxsCtrl_TblObj); j++)
            { // Table: DataStoreObj.Get
                if (pG2->b.mLckAxsCtrl_Tbl.table[j].invID == UID_Table_DataStore2)
                {
                    for (k = j; k<(j + cnt - 1); k++)
                    {
                        if ((u32)(pG2->b.mLckAxsCtrl_Tbl.table[k].mtdID >> 32) != (u32)(UID_MethodID >>32))
                            pG2->b.mLckAxsCtrl_Tbl.table[k].mtdID = ~pG2->b.mLckAxsCtrl_Tbl.table[k].mtdID;
                    }
                    break;
                }
            }
            for (j = 0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.ace) / sizeof(sAxsCtrl_TblObj); j++)
            { //ACE_DataStore2_Get_All.Get / ACE_DataStore2_Get_All.Set / ACE_DataStore2_Set_All.Get / ACE_DataStore2_Set_All.Set
                if (pG2->b.mLckAxsCtrl_Tbl.ace[j].invID == UID_ACE_DataStore2_Get_All)
                {
                    for (k = j; k<(j + (cnt - 1) * 4); k++)
                    {
                        if ((u32)(pG2->b.mLckAxsCtrl_Tbl.ace[k].mtdID >> 32) != (u32)(UID_MethodID >> 32))
                            pG2->b.mLckAxsCtrl_Tbl.ace[k].mtdID = ~pG2->b.mLckAxsCtrl_Tbl.ace[k].mtdID;
                    }
                    break;
                }
            }
            for (j = 0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.datastore) / sizeof(sAxsCtrl_TblObj); j++)
            { // DataStore2.Get / DataStore2.Set
                if (pG2->b.mLckAxsCtrl_Tbl.datastore[j].invID == UID_DataStore2)
                {
                    for (k = j; k<(j + (cnt - 1) * 2); k++)
                    {
                        if ((u32)(pG2->b.mLckAxsCtrl_Tbl.datastore[k].mtdID >> 32) != (u32)(UID_MethodID >> 32))
                            pG2->b.mLckAxsCtrl_Tbl.datastore[k].mtdID = ~pG2->b.mLckAxsCtrl_Tbl.datastore[k].mtdID;
                    }
                    break;
                }
            }

            //3. update AceTbl, some ACE objects are enabled
            tmp8 = cnt*2;
            for (i = 0; i<pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
            {
                tmp32 = (u32)pG3->b.mLckACE_Tbl.val[i].uid;
                if ((tmp32>(u32)0x03FC01) && tmp32<(u32)(0x03FC00 + cnt * 2))
                { //ACE_DataStoreX_Get_All / ACE_DataStoreX_Set_All
                    pG3->b.mLckACE_Tbl.val[i].uid = UID_ACE | (u64)tmp32 ;
                }
            }
        }

        DataStoreAddr_Update();
    }

}

ddr_code void DataStoreAddr_Update(void)
{
    u32 offset = 0;
    u8  i = 0, tblRow;

	tcg_core_trace(LOG_INFO, 0x28bf, "DataStoreAddr_Update()");

    for (tblRow = 0; tblRow<pG2->b.mLckTbl_Tbl.hdr.rowCnt; tblRow++)
    {
        if (pG2->b.mLckTbl_Tbl.val[tblRow].uid == UID_Table_DataStore)
            break;
    }

    mDataStoreAddr[0].offset= 0;
    mDataStoreAddr[0].length = pG2->b.mLckTbl_Tbl.val[tblRow].rows;  //1. Genral datastore is 10.28MB currently 2.Additional datastore is VU
	tcg_core_trace(LOG_INFO, 0x1195, "DS00: %08x %08x", mDataStoreAddr[0].offset, mDataStoreAddr[0].length);

    for (i = 1; i < DSTBL_MAX_NUM; i++)
    {
        offset += pG2->b.mLckTbl_Tbl.val[tblRow + i - 1].rows;

        mDataStoreAddr[i].offset=offset;
        mDataStoreAddr[i].length = pG2->b.mLckTbl_Tbl.val[tblRow + i].rows;

        if (mDataStoreAddr[i].length)
        {
			tcg_core_trace(LOG_INFO, 0x0b8e, "i|%x, mDataStoreAddr[i].offset|%x, mDataStoreAddr[i].length|%x", i, mDataStoreAddr[i].offset, mDataStoreAddr[i].length);
        }
    }

}

ddr_code void SingleUser_Update(void)
{

	tcg_core_trace(LOG_INFO, 0x9984, "SingleUser_Update()");

    if ((mTcgStatus&TCG_ACTIVATED) == 0)
    {
        pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy = 1; // set default as 1
        mSgUser.range = 0;
        mSgUser.cnt = 0;
        return;
    }

    mSgUser.policy = pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy;
    if (pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[0] == UID_Locking)
    {
        // EntireLocking
        mSgUser.range = 0xFFFF;
        mSgUser.cnt = LOCKING_RANGE_CNT + 1;
    }
    else
    {
        u64 tmp64;
        u8 i, j;

        mSgUser.range = 0;
        mSgUser.cnt = 0;
        for (i = 0; i < (LOCKING_RANGE_CNT + 1); i++)
        {
            tmp64 = pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[i];
            if(tmp64)
            {
                for (j = 0; j < (LOCKING_RANGE_CNT + 1); j++)
                {
                    if (tmp64 == pG3->b.mLckLocking_Tbl.val[j].uid)
                    {
                        mSgUser.range |= (0x01 << j);
                        mSgUser.cnt++;
                        break;
                    }
                }
            }
            else
            {
                break;
            }
        }
    }
}

#if _TCG_ != TCG_PYRITE
ddr_code void SingleUser_Setting(void)
{
    u16 row, k = 0;
    u8 i, step;

	tcg_core_trace(LOG_INFO, 0xb4c0, "SingleUser_Setting(), mSgUser.cnt|%x, mSgUser.policy|%x, mSgUser.range|%x", mSgUser.cnt, mSgUser.policy, mSgUser.range);

    if (mSgUser.cnt != 0)
    {
        if (mSgUser.range & 0x01)
        { 
			//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update AccessCtrl table");
			//GlobalRange -> SingerUser
            //1. update AccessCtrl table
            for (row = 0; row < sizeof(pG2->b.mLckAxsCtrl_Tbl.ace) / sizeof(sAxsCtrl_TblObj); row++)
            { // add ACE_Locking_GRange_Erase.Set() method
                if (pG2->b.mLckAxsCtrl_Tbl.ace[row].invID == UID_ACE_Locking_GRange_Erase)
                {                
					//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> add ACE_Locking_GRange_Erase.Set() method");
                    if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == ~UID_MethodID_Get)
                    {
                        pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Get;
                    }
                    else if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == ~UID_MethodID_Set)
                    {
                        pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Set;
                        break;
                    }
                }
            }

            for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.authority) / sizeof(sAxsCtrl_TblObj); row++)
            { // modify User1.Set() ACL
                if ((pG2->b.mLckAxsCtrl_Tbl.authority[row].invID == UID_Authority_User1) && (pG2->b.mLckAxsCtrl_Tbl.authority[row].mtdID == UID_MethodID_Set))
                {                
					//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> modify User1.Set() ACL");
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[0] = UID_ACE_User1_Set_CommonName;
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[1] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[2] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[3] = 0;
                    break;
                }
            }
            for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.cpin) / sizeof(sAxsCtrl_TblObj); row++)
            { // modify C_PIN_User1.Get() ACL
                if ((pG2->b.mLckAxsCtrl_Tbl.cpin[row].invID == UID_CPIN_User1) && (pG2->b.mLckAxsCtrl_Tbl.cpin[row].mtdID == UID_MethodID_Get))
                {
					//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> modify C_PIN_User1.Get() ACL");
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[0] = UID_ACE_CPIN_Anybody_Get_NoPIN;
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[1] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[2] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[3] = 0;
                    break;
                }
            }
            for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.lcking) / sizeof(sAxsCtrl_TblObj); row++)
            {
                if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].invID == UID_Locking_GRange)
                { // modify Locking_GRange.Set() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == UID_MethodID_Set)
                    {
                        if (mSgUser.policy)
                        {
							//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> P1 modify Locking_GRange.Set() ACL");
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_GRange_Set_ReadToLOR;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_Admins_Set_CommonName;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                        }
                        else
                        {
							//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> P0 modify Locking_GRange.Set() ACL");
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_GRange_Set_ReadToLOR;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_User1_Set_CommonName;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                        }
                    }
                    else if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == (u64)(~UID_MethodID_Erase))
                    { // add GRange.Erase                
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> add GRange.Erase");
                        pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID = UID_MethodID_Erase;
                        break;
                    }
                }
            }

            //2. update ACE table, need to follow the row sequence...
            step = 0;
            for (row = 0; row < pG3->b.mLckACE_Tbl.hdr.rowCnt; row++)
            { //modify
                if (step == 0)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_C_PIN_User1_Set_PIN)
                    {
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        step++;
                    }
                }
                else if (step == 1)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_K_AES_256_GlobalRange_GenKey)
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        step++;
                    }
                }
                else if (step == 2)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_Locking_GRange_Get_RangeStartToActiveKey)
                    {
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_Anybody;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        step++;
                    }
                }
                else if (step == 3)
                {
                    if ((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)UID_ACE_Locking_GRange_Set_ReadToLOR)
                    { //add                
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_GRange_Set_ReadToLOR;
                        step++;
                    }
                }
                else if (step == 4)
                {
                    if ((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)UID_ACE_CPIN_Anybody_Get_NoPIN)
                    { //add                 
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_CPIN_Anybody_Get_NoPIN;
                        step++;
                    }
                }
                else if (step == 5)
                {
                    if((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)UID_ACE_Locking_GRange_Erase)
                    {                 
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_GRange_Erase;
                        step++;
                    }
                }
                else if (step == 6)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_User1_Set_CommonName)
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update ACE table-step|%x",step);
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        break;    //step++;
                    }
                }
            }

            //3. update Authority table
            for (row = 0; row<pG3->b.mLckAuthority_Tbl.hdr.rowCnt; row++)
            {
                if (pG3->b.mLckAuthority_Tbl.val[row].uid == UID_Authority_User1)
                {            
					//tcg_core_trace(LOG_INFO, 0, "<GlobalRange -> SingerUser> update Authority table");
                    pG3->b.mLckAuthority_Tbl.val[row].enabled = 1;
                    break;
                }
            }
        }

        // for Range 1~8
        for (i = 1; i <= LOCKING_RANGE_CNT; i++)
        {
            if (mSgUser.range&(0x01 << i))
            { //RangeN -> SingerUser
                //1. update AccessCtrl table
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.ace) / sizeof(sAxsCtrl_TblObj); row++)
                { // add ACE_Locking_GRange_Erase.Set() method
                    if (pG2->b.mLckAxsCtrl_Tbl.ace[row].invID == (u64)(UID_ACE_Locking_Range1_Erase + i - 1))
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update AccessCtrl table",mSgUser.range);
                        if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == (u64)(~UID_MethodID_Get))
                        {
                            pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Get;
                        }
                        else if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == (u64)(~UID_MethodID_Set))
                        {
                            pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Set;
                            break;
                        }
                    }
                }
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.authority) / sizeof(sAxsCtrl_TblObj); row++)
                { //modify UserN+1.Set() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.authority[row].invID == (u64)(UID_Authority_User1 + i))
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> modify User%x.Set() ACL",mSgUser.range,i);
                        if (pG2->b.mLckAxsCtrl_Tbl.authority[row].mtdID == UID_MethodID_Set)
                        {
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[0] = UID_ACE_User1_Set_CommonName + i;
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[1] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[3] = 0;
                            break;
                        }
                    }
                }
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.cpin) / sizeof(sAxsCtrl_TblObj); row++)
                { // modify C_PIN_UserN+1.Get() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.cpin[row].invID == (u64)(UID_CPIN_User1 + i))
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> modify C_PIN_User%x.Get() ACL",mSgUser.range,i);
                        if (pG2->b.mLckAxsCtrl_Tbl.cpin[row].mtdID == UID_MethodID_Get)
                        {
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[0] = UID_ACE_CPIN_Anybody_Get_NoPIN;
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[1] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[3] = 0;
                            break;
                        }
                    }
                }
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.lcking) / sizeof(sAxsCtrl_TblObj); row++)
                { // modify RangeN.Set() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].invID == (u64)(UID_Locking_Range + i))
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> modify Range%x.Set() ACL",mSgUser.range,i);
                        if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == UID_MethodID_Set)
                        {
                            if (mSgUser.policy)
                            {                        
								//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> P1",mSgUser.range);
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_Locking_Range1_Set_Range + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = UID_ACE_Admins_Set_CommonName;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                            }
                            else
                            {                        
								//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> P0",mSgUser.range);
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_Locking_Range1_Set_Range + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = UID_ACE_User1_Set_CommonName + i;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                            }
                        }
                        else if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == (u64)(~UID_MethodID_Erase))
                        { // add RangeN Erase method
                        
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> add Range%x Erase method",mSgUser.range,i);
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID = UID_MethodID_Erase;
                            break;
                        }
                    }
                }

                //2. update ACE table
                step = 0;
                for (row = 0; row<pG3->b.mLckACE_Tbl.hdr.rowCnt; row++)
                { //modify
                    if (step == 0)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (u64)(UID_ACE_C_PIN_User1_Set_PIN + i))
                        {                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                        for(k=1;k<LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 1)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (u64)(UID_ACE_K_AES_256_Range1_GenKey + i - 1))
                        {                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 2)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (u64)(UID_ACE_Locking_Range1_Get_RangeStartToActiveKey + i - 1))
                        {                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_Anybody;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 3)
                    {
                        if ((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)(UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1))
                        { //add                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1;
                            step++;
                        }
                    }
                    else if (step == 4)
                    {
                        if ((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)(UID_ACE_Locking_Range1_Set_Range + i - 1))
                        { //add                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_Range1_Set_Range + i - 1;
                            if (mSgUser.policy)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_Admins;
                            else
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 5)
                    {
                        if ((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)UID_ACE_CPIN_Anybody_Get_NoPIN)
                        { //add                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_CPIN_Anybody_Get_NoPIN;
                            step++;
                        }
                    }
                    else if (step == 6)
                    {
                        if ((u32)pG3->b.mLckACE_Tbl.val[row].uid == (u32)(UID_ACE_Locking_Range1_Erase + i - 1))
                        { //add                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_Range1_Erase + i - 1;
                            step++;
                        }
                    }
                    else if (step == 7)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (u64)(UID_ACE_User1_Set_CommonName + i))
                        {                    
							//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update ACE table - step%x",mSgUser.range,step);
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            break;    //step++;
                        }
                    }
                }

                //3. update Authority table
                for (row = 0; row<pG3->b.mLckAuthority_Tbl.hdr.rowCnt; row++)
                {
                    if (pG3->b.mLckAuthority_Tbl.val[row].uid == (u64)(UID_Authority_User1 + i))
                    {                
						//tcg_core_trace(LOG_INFO, 0, "<Range%x -> SingerUser> update Authority table",mSgUser.range);
                        pG3->b.mLckAuthority_Tbl.val[row].enabled = 1;
                        break;
                    }
                }
            }
        }

        //update LockingInfo table
        memset(pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange, 0, sizeof(pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange));

        if (mSgUser.range == 0xffff) // EntireLocking
            pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[0] = UID_Locking;
        else
        {
            row = 0;
            if(mSgUser.range&0x01)
                pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[row++] = UID_Locking_GRange;

            for (i = 1; i<8; i++)
            {
                if(mSgUser.range&(0x01<<i))
                    pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[row++] = UID_Locking_Range + i;
            }
        }

        if(mSgUser.policy)
            pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy = 1;
        else
            pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy = 0;
    }

}
#endif

#if TCG_FS_BLOCK_SID_AUTH
ddr_code u16 TcgBlkSIDAuthentication(req_t *req)
{
	u16 status;
	
	tcg_core_trace(LOG_INFO, 0x2fc3, "TcgBlkSIDAuthentication() - HwRst|%x",*((u8 *)req->req_prp.sec_xfer_buf));

	//***//
    //SATA_CMD_FreezeLock();	//Max modify
	//***//

    if (CPinMsidCompare(CPIN_SID_IDX)) {
        status = STS_SUCCESS;
    }
    else if (mTcgStatus & SID_BLOCKED) {
		//tcg_core_trace(LOG_INFO, 0, "[Max set]4");
        status = STS_INVALID_PARAMETER;
    }
    else {
        mTcgStatus |= SID_BLOCKED;

        // if (siloIdxCmd == 0)   // normal IF-SEND
            // HostTx2TcgBuf(pHcmdQ, buf);

        if (*((u8 *)req->req_prp.sec_xfer_buf) & 0x01){  
			//tcg_core_trace(LOG_INFO, 0, "[Max set]5");
			mTcgStatus |= SID_HW_RESET;
        }
        status = STS_SUCCESS;
        //gTcgCmdState=ST_AWAIT_IF_SEND;
	}
	tcg_core_trace(LOG_INFO, 0x975d, "TcgBlkSIDAuthentication() - mTcgStatus|%x",mTcgStatus);

	return status;
}
#endif

//
// Update "mReadLockedTable[]" and "mWriteLockedTable[]" for easier/faster ATA Kernel access.
//
//      These two tables are extracted from "pG3->b.mLckLocking_Tbl" for Media Read/Write control.
//      The object sequence in these tables are sorted according to the "ragneStart" in "pG3->b.mLckLocking_Tbl".
//      LockingTbl_RangeChk() should be run to avoid range overlapping.
//
//      Table Object:     [rangeNo, rangeStart, rangeEnd]
//      rangeNo=0 for GlobalRange, and it should be located at the last effective row in the tables if it is locked.
//

// LBA moving version
fast_code u8 TcgRangeCheck(u64 lbaStart, u64 len, u16 locked_status)
{
#ifdef TCG_RNG_CHK_DEBUG
	tcg_core_trace(LOG_INFO, 0x5b91, "TcgRangeCheck() - lbaStart:0x%x , len:%x , locked:%x", lbaStart, len, locked_status);

	for(u8 rng_cnt=0; rng_cnt<LOCKING_RANGE_CNT; rng_cnt++){
		tcg_core_trace(LOG_INFO, 0x9df7, "pR[%d]: 0x%x ~ 0x%x", pLockingRangeTable[rng_cnt].rangeNo, pLockingRangeTable[rng_cnt].rangeStart, pLockingRangeTable[rng_cnt].rangeEnd);
		if(pLockingRangeTable[rng_cnt].rangeNo==0)
			break;
	}
	tcg_core_trace(LOG_INFO, 0xe0ee, "Tcg Sts: 0x%x, Act: 0x%x", mTcgStatus, mTcgActivated);
#endif

    //if(mTcgStatus & TCG_ACTIVATED)
    {
        u8  i;
	    u64 startLBA, endLBA;

		if(locked_status==0)
		{
			return TCG_DOMAIN_NORMAL;
		}
	    if(len==0)
	    {
			return TCG_DOMAIN_NORMAL;
	    }

		startLBA = lbaStart;
	    endLBA = lbaStart+len-1;

	    // Starting LBA is located at Global
	    if(startLBA < pLockingRangeTable[0].rangeStart){
		    if(locked_status & 0x1)
		    {		    
			    return TCG_DOMAIN_ERROR;
		    }
			startLBA = pLockingRangeTable[0].rangeStart;
	    }

	    // for-loop //
	    for(i=0; i<LOCKING_RANGE_CNT; i++){

		    if(startLBA>endLBA)
		   	{
				return TCG_DOMAIN_NORMAL;
		    }
		    if(pLockingRangeTable[i].rangeNo==0)
			    break;

		    // check one locking range
		    if((startLBA >= pLockingRangeTable[i].rangeStart) && (startLBA <= pLockingRangeTable[i].rangeEnd)){
			    if(locked_status & ((0x1)<<(pLockingRangeTable[i].rangeNo)) )
			    {
					return TCG_DOMAIN_ERROR;
			    }
				startLBA = pLockingRangeTable[i].rangeEnd+1;

			    // Gap between 2 locking ranges
			    if(startLBA!=pLockingRangeTable[i+1].rangeStart)
				    i--;
		    }
		    // check global range (Gap)
		    else if((startLBA < pLockingRangeTable[i+1].rangeStart) && (startLBA > pLockingRangeTable[i].rangeEnd)){
			    if(locked_status & 0x1)
			    {
					return TCG_DOMAIN_ERROR;
			    }
				else
				    startLBA = pLockingRangeTable[i+1].rangeStart;
		    }
	    }

	    // Ending LBA is located at the last segment of Global range
	    if(locked_status & 0x1)
	   	{
			return TCG_DOMAIN_ERROR;
	    }
	}
	return TCG_DOMAIN_NORMAL;
}

fast_code u8 TcgRangeCheck_SMBR(u64 lbaStart, u64 len, u16 locked_status)
{
	u8 cover_unlock = 0;
	u8 cover_locked = 0;
#ifdef TCG_RNG_CHK_DEBUG
	tcg_core_trace(LOG_INFO, 0x80a2, "TcgRangeCheck_SMBR() - lbaStart:0x%x , len:%x , locked_status:%x", lbaStart, len, locked_status);

	for(u8 rng_cnt=0; rng_cnt<LOCKING_RANGE_CNT; rng_cnt++){

		tcg_core_trace(LOG_INFO, 0x18c9, "pR[%d]: 0x%x ~ 0x%x", pLockingRangeTable[rng_cnt].rangeNo, pLockingRangeTable[rng_cnt].rangeStart, pLockingRangeTable[rng_cnt].rangeEnd);

		if(pLockingRangeTable[rng_cnt].rangeNo==0)
			break;
	}

	tcg_core_trace(LOG_INFO, 0xf5b8, "Tcg Sts: 0x%x, Act: 0x%x", mTcgStatus, mTcgActivated);
#endif	
	//tcg_core_trace(LOG_INFO, 0, "TcgRangeCheck_SMBR() - lbaStart:0x%x , len:%x , locked_status:%x",lbaStart,len,locked_status);
	
	if(len==0)
		return false;

	if(mTcgStatus & MBR_SHADOW_MODE)
	{
		u8  i;
		u64 smbr_size = (0x8000000 >> host_sec_bitz);
		u64 startLBA, endLBA;
		
	    startLBA = lbaStart;
	    endLBA = lbaStart+len-1;
		
		if(startLBA < smbr_size)
		{
			// in 128 MB
			if(endLBA < smbr_size)
				return TCG_DOMAIN_SHADOW;
			// cover 128 MB
			else
				return TCG_DOMAIN_ERROR;
		}

		// move LBA and assert flag
		if(locked_status)
		{
			// Starting LBA is located at Global
	    	if(startLBA < pLockingRangeTable[0].rangeStart){
		    	if(locked_status & 0x1)
			    	cover_locked = true;
				else
					cover_unlock = true;
				startLBA = pLockingRangeTable[0].rangeStart;
	    	}

	    	// for-loop //
	    	for(i=0; i<LOCKING_RANGE_CNT; i++){

		    	if(startLBA>endLBA)
			    	break;

		    	if(pLockingRangeTable[i].rangeNo==0)
		    	{
		    		if(locked_status & 0x1)	
						cover_locked = true;
					else
						cover_unlock = true;
			    	break;
		    	}

		    	// check one locking range
		    	if((startLBA >= pLockingRangeTable[i].rangeStart) && (startLBA <= pLockingRangeTable[i].rangeEnd)){
			    	if(locked_status & ((0x1)<<(pLockingRangeTable[i].rangeNo)) )
						cover_locked = true;
					else
		    			cover_unlock = true;
					startLBA = pLockingRangeTable[i].rangeEnd+1;

			    	// Gap between 2 locking ranges
			    	if(startLBA!=pLockingRangeTable[i+1].rangeStart)
				    	i--;
		    	}
		    	// check global range (Gap)
		    	else if((startLBA < pLockingRangeTable[i+1].rangeStart) && (startLBA > pLockingRangeTable[i].rangeEnd)){
			    	if(locked_status & 0x1)
						cover_locked = true;
			    	else
			    	{	
						cover_unlock = true;
						startLBA = pLockingRangeTable[i+1].rangeStart;
			    	}
		    	}
	    	}
		}
		else
		{
			cover_unlock = true;
		}

		if(cover_unlock)
		{
			if(cover_locked)
			{
				//tcg_core_trace(LOG_INFO, 0, "TCG_DOMAIN_ERROR");
				return TCG_DOMAIN_ERROR;     // unlocked & locked at the same time
			}
			else
			{	
				//tcg_core_trace(LOG_INFO, 0, "TCG_DOMAIN_NORMAL");
				return TCG_DOMAIN_NORMAL;    // all unlocked
			}
		}
		else if(cover_locked)
		{
			//tcg_core_trace(LOG_INFO, 0, "TCG_DOMAIN_DUMMY");
			return TCG_DOMAIN_DUMMY;         // all locked
		}
	}
	
	return TcgRangeCheck(lbaStart, len, locked_status);
}

ddr_code void LockingRangeTable_Update(void)
{
#if (_TCG_ != TCG_PYRITE)
    u64 tmpRangeStart, tmpRangeLength, tmpRangeEnd;
    u8 i, k, iSort = 0, rangeCnt = 0;

	tcg_core_trace(LOG_INFO, 0xfa5c, "LockingRangeTable_Update()");
    mReadLockedStatus = 0;
    mWriteLockedStatus = 0;
	nonGR_set = false;

#ifdef NS_MANAGE
	if(ns_array_menu->total_order_now > 1)
		goto ADD_GLOBAL;
#endif
    //establish the table ...
    for (i = 1; i <= LOCKING_RANGE_CNT; i++)   //skip GloblaRange here
    {
		//check if this is an effective range
        tmpRangeLength = pG3->b.mLckLocking_Tbl.val[i].rangeLength;

        if (tmpRangeLength == 0)    //not an effective range, skip
        {
        	if(pG3->b.mLckLocking_Tbl.val[i].rangeStart != 0)   	
				nonGR_set = true;
			continue;
        }
		else
			nonGR_set = true;
  
        tmpRangeStart = pG3->b.mLckLocking_Tbl.val[i].rangeStart;
        tmpRangeEnd =  tmpRangeStart + (tmpRangeLength-1);

		//skip range within MBR
		if(mTcgStatus & MBR_SHADOW_MODE)
		{
			u64 smbr_size = (0x8000000 >> host_sec_bitz);

			if((tmpRangeStart < smbr_size) && (tmpRangeEnd < smbr_size))
				continue;				
		}
	
        //sorting in "pLockingRangeTable[]"
        for (iSort = 0; iSort<rangeCnt; iSort++)
        {        
            if (tmpRangeStart<pLockingRangeTable[iSort].rangeStart)
				break;
		}

        if (rangeCnt) // previous range data existed in the table, update them first
        {
            for (k = rangeCnt; k>iSort; k--)
            {
                pLockingRangeTable[k].rangeNo = pLockingRangeTable[k - 1].rangeNo;
                pLockingRangeTable[k].rangeStart = pLockingRangeTable[k - 1].rangeStart;
                pLockingRangeTable[k].rangeEnd = pLockingRangeTable[k - 1].rangeEnd;
                pLockingRangeTable[k].blkcnt = pLockingRangeTable[k - 1].blkcnt;
                pLockingRangeTable[k].readLocked = pLockingRangeTable[k - 1].readLocked;
                pLockingRangeTable[k].writeLocked = pLockingRangeTable[k - 1].writeLocked;
            }
        }

        //add the new object
        pLockingRangeTable[iSort].rangeNo = i;
        pLockingRangeTable[iSort].rangeStart = tmpRangeStart;
        pLockingRangeTable[iSort].rangeEnd = tmpRangeEnd;
        pLockingRangeTable[iSort].blkcnt = (u32)tmpRangeLength;  //added for AltaPlus
				
        if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled && pG3->b.mLckLocking_Tbl.val[i].readLocked)
        { //this range is Read-Locked
            pLockingRangeTable[iSort].readLocked = 0x01;
            //mReadLockedStatus |= (0x01 << i);
        }
        else
            pLockingRangeTable[iSort].readLocked = 0x00;

        if(pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[i].writeLocked)
        { //this range is Write-Locked
            pLockingRangeTable[iSort].writeLocked = 0x01;
            //mWriteLockedStatus |= (0x01 << i);
        }
        else
            pLockingRangeTable[iSort].writeLocked = 0x00;

        rangeCnt++;
    }
	

ADD_GLOBAL:
    //add GlobalRange as the last object
    pLockingRangeTable[rangeCnt].rangeNo = 0;
    pLockingRangeTable[rangeCnt].rangeStart = 0;
    pLockingRangeTable[rangeCnt].rangeEnd = 0;
    pLockingRangeTable[rangeCnt].blkcnt = 0xffffffff;

	
	if(pG3->b.mLckLocking_Tbl.val[0].readLockEnabled && pG3->b.mLckLocking_Tbl.val[0].readLocked)
    { //global range is Read-Locked
        pLockingRangeTable[rangeCnt].readLocked = 0x01;
        //mReadLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[rangeCnt].readLocked = 0x00;


    if(pG3->b.mLckLocking_Tbl.val[0].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[0].writeLocked)
    { //global range is Write-Locked
        pLockingRangeTable[rangeCnt].writeLocked = 0x01;
        //mWriteLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[rangeCnt].writeLocked = 0x00;


    if (rangeCnt != LOCKING_RANGE_CNT)
    {
        // update the last row for TcgRangeCheck()
        pLockingRangeTable[LOCKING_RANGE_CNT].rangeNo = 0;
        pLockingRangeTable[LOCKING_RANGE_CNT].rangeStart = 0;
        pLockingRangeTable[LOCKING_RANGE_CNT].rangeEnd = 0;
        pLockingRangeTable[LOCKING_RANGE_CNT].readLocked = pLockingRangeTable[rangeCnt].readLocked;
        pLockingRangeTable[LOCKING_RANGE_CNT].writeLocked = pLockingRangeTable[rangeCnt].writeLocked;
	}

	// modify range which cross MBR
	if(mTcgStatus & MBR_SHADOW_MODE)
	{
		u64 smbr_size = (0x8000000 >> host_sec_bitz);
		if(pLockingRangeTable[0].rangeNo!=0)
			if(pLockingRangeTable[0].rangeStart < smbr_size)
				pLockingRangeTable[0].rangeStart = smbr_size;
	}

	for(i = 0; i <= LOCKING_RANGE_CNT; i++)
	{
		if(pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[i].writeLocked)
    	{ 
        	mWriteLockedStatus |= (0x01 << i);
    	}
		if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled && pG3->b.mLckLocking_Tbl.val[i].readLocked)
    	{ 
        	mReadLockedStatus |= (0x01 << i);
    	}
		
		#ifdef NS_MANAGE
		if(ns_array_menu->total_order_now > 1)
			break;
		#endif
	}
#else

	tcg_core_trace(LOG_INFO, 0xd6cd, "LockingRangeTable_Update()");

    mReadLockedStatus = 0;
    mWriteLockedStatus = 0;

    pLockingRangeTable[0].rangeNo = 0;
    pLockingRangeTable[0].rangeStart = 0;
    pLockingRangeTable[0].rangeEnd = 0;
    pLockingRangeTable[0].blkcnt= 0xffffffff;

    if(pG3->b.mLckLocking_Tbl.val[0].readLockEnabled && pG3->b.mLckLocking_Tbl.val[0].readLocked)
    { //global range is Read-Locked
        pLockingRangeTable[0].readLocked = 0x01;
        mReadLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[0].readLocked = 0x00;

    if(pG3->b.mLckLocking_Tbl.val[0].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[0].writeLocked)
    { //global range is Write-Locked
        pLockingRangeTable[0].writeLocked = 0x01;
        mWriteLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[0].writeLocked = 0x00;
#endif
    //++rangeCnt;
	if(bKeyChanged == mTRUE || bLockingRangeChanged == mTRUE){
		tcg_init_aes_key_range();
	}

    DumpRangeInfo();  //Max modify
    // Need to take care of MBR-S condition ...
}

#else

#define __TCG_IF__
#include <string.h>
#include "sect.h"
#include "ipc.h"
#include "customer.h"
#include "FeaturesDef.h"
#include "nvme_spec.h"
#include "nvmet.h"
#include "MemAlloc.h"
#include "SharedVars.h"
#include "ErrorCodes.h"
#include "Monitor.h"
#include "tcgcommon.h"
#include "tcgnvme.h"
#include "SysInfo.h"
#include "tcgtbl.h"
#include "tcg.h"
#include "tcg_sh_vars.h"
#include "tcg_if_vars.h"
#include "Debug.h"
#include "stdlib.h"
#include "dpe.h"
#include "Vendor_cmd.h"
#include "aes.h"
#include "NvmeSecurity.h"
#include "io.h"
#include "otp.h"

//-----------------------------------------------------------------------------
//  Constants definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Imported data prototype without header include
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define TCG_ACT_IN_OPAL()           (mSgUser.range == 0)                // activated in Opal_Mode or not activated yet
#define TCG_ACT_IN_ALL_SU()         ((mSgUser.range & 0x1ff) == 0x1ff)  // activated in Entire_Single_User_Mode
#define WRAP_KEK_LEN                sizeof(WrapKEK)

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
static tcg_data U64 method_uid_lookup[] =
{
    UID_MethodID_Next,      UID_MethodID_GetACL,    UID_MethodID_GenKey,        UID_MethodID_RevertSP,
    UID_MethodID_Get,       UID_MethodID_Set,       UID_MethodID_Authenticate,  UID_MethodID_Revert,
    UID_MethodID_Activate,  UID_MethodID_Random,    UID_MethodID_Reactivate,    UID_MethodID_Erase,
};

typedef enum
{
    cMcMtd_Next = 0,    cMcMtd_GetACL,          cMcMtd_GenKey,          cMcMtd_RevertSP,
    cMcMtd_Get,         cMcMtd_Set,             cMcMtd_Authenticate,    cMcMtd_Revert,
    cMcMtd_Activate,    cMcMtd_Random,          cMcMtd_Reactivate,      cMcMtd_Erase,
    cMcMtd_illegal,     cMcMtd_Last,
} msgTcgMethod_t;

static tcg_data U16 (*gCbfProc_method_map[cMcMtd_Last])(req_t *req) =
{
    f_method_next,      f_method_getAcl,        f_method_genKey,        f_method_revertSp,
    f_method_get,       f_method_set,           f_method_authenticate,  f_method_revert,
    f_method_activate,  f_method_random,        f_method_reactivate,    f_method_erase,
    f_method_illegal,
};


//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Private function proto-type definitions:
//-----------------------------------------------------------------------------
// static int TcgFuncRequest1_No_Waitting(MSG_TCG_SUBOP_t subOP);
static int TcgFuncRequest1(MSG_TCG_SUBOP_t subOP);
// static int TcgFuncRequest2(MSG_TCG_SUBOP_t subOP, U16 laas, U16 laae, PVOID pBuffer);
// static int TcgFuncRequest3(MSG_TCG_SUBOP_t subOP, tMSG_TCG* pMyTcgMsg);



//-----------------------------------------------------------------------------
//  Imported data proto-type without header include
//-----------------------------------------------------------------------------
extern SecretZone_t secretZone;

//-----------------------------------------------------------------------------
//  Imported function proto-type without header include
//-----------------------------------------------------------------------------
// extern void MediaReadRequestDone(HcmdQ_t* pHcmdQ);
// extern void SATA_ReadStreamingTransferDone(HcmdQ_t* pHcmdQ);
// extern void Host_ProcessDataXfer(MsgHostIO_t* pHcmdMsg);
extern  void Nvme_Security_FlushAll(void);
extern  void SATA_CMD_FreezeLock(void);
extern  void tcg_prepare_respPacket(void);
extern  U16  tcg_prepare_respPacket_update(bool addStatus);
extern  void host_Properties_Reset(void);
extern  U32 read_otp_data(U32);
extern  int program_otp_data(U32 , U32);
extern  void crypto_dump_range(void);

//-----------------------------------------------------------------------------
//  Codes    Codes    Codes    Codes    Codes    Codes    Codes    Codes
//-----------------------------------------------------------------------------

/****************************************************************************
 * AtomDecoding_ByteHdr():
 ****************************************************************************/
// check if the atom header is "byte sequence" type or not
// [OUT] *length: the length of the byte sequence
tcg_code U16 AtomDecoding_ByteHdr(U32* length)
{
    U16 result;
    U8 byte;
    U8 errCode = 0x00;  //no error

    for (;;)
    {
        if (iPload >= mCmdPkt.mSubPktFmt.length)
        {
            errCode = 0x10;
            result = STS_SESSION_ABORT;
            goto ChkByte_Err;
        }

        byte = mCmdPkt.payload[iPload++];
        if (byte != 0xff)
            break;
    }

    if ((byte & 0x80) == 0x00)
    {  // Tiny Atom
        errCode = 0x20;
        result = STS_INVALID_PARAMETER;
        goto ChkByte_Err;
    }
    else if ((byte & 0xC0) == 0x80)
    { // Short Atom, 0-15 bytes of data
        if (byte & 0x20)
        { // byte sequence
            if (byte & 0x10)
            { // TODO: continued into another atom
                errCode = 0x40;
                result = STS_INVALID_PARAMETER;
                goto ChkByte_Err;
            }

            *length = byte & 0x0f;
        }
        else
        { // integer
            errCode = 0x50;
            result = STS_INVALID_PARAMETER;
            goto ChkByte_Err;
        }
    }
    else if ((byte & 0xE0) == 0xC0)
    { // Medium Atom: 1-2047 bytes
        if (byte & 0x10)
        { // byte sequence
            if (byte & 0x08)
            { // TODO: Continued into another atom
                errCode = 0x70;
                result = STS_INVALID_PARAMETER;
                goto ChkByte_Err;
            }

            *length = (byte & 0x07) * 0x100 + mCmdPkt.payload[iPload++];
        }
        else
        { // integer
            errCode = 0x80;
            result = STS_INVALID_PARAMETER;
            goto ChkByte_Err;
        }
    }
    else if ((byte & 0xFC) == 0xe0)
    {  // Long Atom
        if (byte & 0x02)
        { // byte sequence
            U32 tmp32;

            if (byte & 0x01)
            { // TODO: Continued into another atom
                errCode = 0x90;
                result = STS_INVALID_PARAMETER;
                goto ChkByte_Err;
            }

            tmp32 = mCmdPkt.payload[iPload++];
            tmp32 = tmp32 * 0x100 + mCmdPkt.payload[iPload++];
            tmp32 = tmp32 * 0x100 + mCmdPkt.payload[iPload++];
            *length = tmp32;
        }
        else
        {
            errCode = 0xA0;
            result = STS_INVALID_PARAMETER;
            goto ChkByte_Err;
        }
    }
    else
    {
        errCode = 0xB0;    // unknow Token
        result = STS_INVALID_PARAMETER;
        goto ChkByte_Err;
    }
    return STS_SUCCESS;

ChkByte_Err:
    // TCG_PRINTF("AtomDecoding err %2X\n", errCode);
    TCGPRN("AtomDecoding_ByteHdr() errCode|%x\n", errCode);
    DBG_P(0x2, 0x03, 0x710000, 4, errCode);  // AtomDecoding_ByteHdr() errCode|%x
    // DBG_P(2, 3, 0x82002A, 1, errCode);  //82 00 2A, "AtomDecoding err[%X]", 1
    return result;
}

/****************************************************************************
 * AtomDecoding_Uid2():
 ****************************************************************************/
// Atom Decoding, only accept "Byte" and the byte length must be "8"
// output the result (U64) to *data
tcg_code U16 AtomDecoding_Uid2(U8* data)
{
    U8 j;
    U16 result;
    U32 len = 0;

    result = AtomDecoding_ByteHdr(&len);
    if ((result != STS_SUCCESS) || (len != sizeof(U64)))
        return result;

    for (j = 8; j>0; j--)
        *(data + j - 1) = mCmdPkt.payload[iPload++];

    return STS_SUCCESS;
}

/****************************************************************************
 * AtomDecoding_HUid2():
 ****************************************************************************/
// Atom Decoding, only accept "Byte" and the byte length must be "4"
// Output the result (U32) to data
tcg_code U16 AtomDecoding_HUid2(U8* data)
{
    U8 j;
    U16 result;
    U32 len = 0;

    result = AtomDecoding_ByteHdr(&len);
    if ((result != STS_SUCCESS) || (len != 4))
        return result;

    for (j = 4; j>0; j--)
        *(data + j - 1) = mCmdPkt.payload[iPload++];

    return STS_SUCCESS;
}

/****************************************************************************
 * AtomDecoding_Uint():
 ****************************************************************************/
//
//Atom Decoding for unsigned integer
//    *data: integer buffer for output
//  datalen: buffer length
//
//  Return STS_INVALID_PARAMETER if the sizeof(unsigned integer) > buffer size (datalen)
//
tcg_code U16 AtomDecoding_Uint(U8 *data, U32 dataLen)
{
    U32 j, len = 0;
    U8 byte, err;

    for (;;)
    {
        if (iPload >= mCmdPkt.mSubPktFmt.length)
        {
            err = 0x10;
            goto AtomDecoding_Uint_Abort;
        }

        byte = mCmdPkt.payload[iPload++];
        if (byte != 0xff)
            break;
    }

    for (j = 0; j < dataLen; j++)
        data[j] = 0;

    if (byte<64)
    {  //Tiny atom, 0~63
        data[0] = byte;
        return STS_SUCCESS;
    }
    else if ((byte & 0xF0) == 0x80)
    { //Short Atom, unsigned integer
        len = byte & 0x0f;
    }
    else if ((byte & 0xF8) == 0xc0)
    { //Medium Atom, unsigned integer
        // HiByte(LoWord(len)) = byte & 0x07;
        *(((U8*)&len)+1) = byte & 0x07;
        // LoByte(LoWord(len)) = mCmdPkt.payload[iPload++];
        *(((U8*)&len)+0) = mCmdPkt.payload[iPload++];
    }
    else if (byte == 0xe0)
    {  //Long Atom, unsigned integer
        // LoByte(HiWord(len)) = mCmdPkt.payload[iPload++];
        *(((U8*)&len)+2) = mCmdPkt.payload[iPload++];
        // HiByte(LoWord(len)) = mCmdPkt.payload[iPload++];
        *(((U8*)&len)+1) = mCmdPkt.payload[iPload++];
        // LoByte(LoWord(len)) = mCmdPkt.payload[iPload++];
        *(((U8*)&len)+0) = mCmdPkt.payload[iPload++];
    }
    else
    { // unknow Token
        err = 0x20;
        goto AtomDecoding_Uint_Abort;
    }

    if (len == 0)
    {
        err = 0x30;
        goto AtomDecoding_Uint_Err2;
    }

    if (len>dataLen)
    { // check if the data outside the buffer are all zero
        for (j = 0; j<(len - dataLen); j++)
        {
            if (mCmdPkt.payload[iPload++] != 0)
            {
                byte = 0x50;
                goto AtomDecoding_Uint_Err2;
            }
        }
        len = dataLen;
    }

    for (j = len; j>0; j--)
        data[j - 1] = mCmdPkt.payload[iPload++];

    return STS_SUCCESS;

AtomDecoding_Uint_Abort:
    // TCG_PRINTF("AtomDecoding_UInt Abt: %2X\n", err);
    TCGPRN("AtomDecoding_Uint() err|%x\n", err);
    DBG_P(0x2, 0x03, 0x710001, 4, err);  // AtomDecoding_Uint() err|%x
    // DBG_P(2, 3, 0x82002B, 1, err);  //82 02 2B, "AtomDecoding_UInt Abt[%X]", 1
    return STS_SESSION_ABORT;

AtomDecoding_Uint_Err2:
    // TCG_PRINTF("AtomDecoding_UInt Err: %2X\n", err);
    TCGPRN("AtomDecoding_Uint() err2|%x\n", err);
    DBG_P(0x2, 0x03, 0x710002, 4, err);  // AtomDecoding_Uint() err2|%x
    // DBG_P(2, 3, 0x82002C, 1, err);  //82 02 2C, "AtomDecoding_UInt Err[%X]", 1
    return STS_INVALID_PARAMETER;
}

/****************************************************************************
 * ChkToken():
 ****************************************************************************/
// skip Empty Token (0xff), and return the next non-empty payload
// (iPload will be updated to the non-empty payload)
tcg_code U8 ChkToken(void)
{
    U8 data;
    for (;;)
    {
        data = mCmdPkt.payload[iPload++];
        if (data != 0xff)
            return data;

        if (iPload > mCmdPkt.mSubPktFmt.length)
            return 0xff;    //NG
    }
}

/****************************************************************************
 * AtomEncoding_ByteHdr():
 ****************************************************************************/
// Encode the Byte Sequence into Atom (Core 3.2.2.3.1)
// This function will only encode the Atom Header and write it directly to "dataBuf[iDataBuf++]".
//
// Return the Byte Count written to dataBuf[].
tcg_code int AtomEncoding_ByteHdr(U32 length)
{
    //Byte Sequence,  write Header only
    if (length <= 15)
    { //Short Atom, length=0~15
        dataBuf[iDataBuf++] = 0xA0 + (U8)length;
        return 1;
    }
    else if (length <= 2047)
    { //Medium Atom
        // dataBuf[iDataBuf++] = 0xD0 + HiByte(LoWord(length));
        dataBuf[iDataBuf++] = 0xD0 + *(((U8*)&length)+1);
        // dataBuf[iDataBuf++] = LoByte(LoWord(length));
        dataBuf[iDataBuf++] = *(((U8*)&length)+0);
        return 2;
    }
    else
    { //Long Atom
        dataBuf[iDataBuf++] = 0xE2;
        // dataBuf[iDataBuf++] = LoByte(HiWord(length));
        dataBuf[iDataBuf++] = *(((U8*)&length)+2);
        // dataBuf[iDataBuf++] = HiByte(LoWord(length));
        dataBuf[iDataBuf++] = *(((U8*)&length)+1);
        // dataBuf[iDataBuf++] = LoByte(LoWord(length));
        dataBuf[iDataBuf++] = *(((U8*)&length)+0);
        return 4;
    }
}

/****************************************************************************
 * AtomEncoding_ByteSeq():
 ****************************************************************************/
tcg_code int AtomEncoding_ByteSeq(U8* data, U32 len)
{
    U32 i32;
    int cnt = AtomEncoding_ByteHdr(len);

    for (i32 = 0; i32 < len; i32++) //UID
        dataBuf[iDataBuf++] = data[i32];
    return (cnt + len);
}

/****************************************************************************
 * AtomEncoding_Int2Byte():
 ****************************************************************************/
// Ex: encodes a U64 integer to the byte sequence on databuf[]
tcg_code int AtomEncoding_Int2Byte(U8* data, U32 byteSize)
{
    U32 i32;
    int cnt = AtomEncoding_ByteHdr(byteSize);

    for (i32 = 1; i32 <= byteSize; i32++) //UID
        dataBuf[iDataBuf++] = data[byteSize - i32];
    return (cnt + byteSize);
}

/****************************************************************************
 * AtomEncoding_Integer():
 ****************************************************************************/
//
// Encode the Integer Value into Atom
//
// This function will encode the Atom Header + Data, and write them directly to "dataBuf[iDataBuf++]".
// It only deals with the unsigned value.
//
// Return the Byte Count written to dataBuf[].
tcg_code int AtomEncoding_Integer(U8 *data, U8 size)
{
    //Integer value,  write Header + data
    U8 i;

    for (i = size; i>0; i--)
    {
        if (*(data + i - 1) != 0)
            break;
    }

    size = i;

    if (size <= 1)
    {
        if (*data <= 63) //Tiny Atom 0~63
            dataBuf[iDataBuf++] = *data;
        else
        {
            dataBuf[iDataBuf++] = 0x81;
            dataBuf[iDataBuf++] = *data;
        }
    }
    else if (size <= 15)
    { //size>=2, <=15
      //Short Atom, length=0~15
        dataBuf[iDataBuf++] = 0x80 + size;

        for (i = size; i>0; i--)
            dataBuf[iDataBuf++] = *(data + i - 1);
    }
    else
        return zNG;

    return zOK;
}
/*
void TCG_NewKeyTbl(void)
{
    U8 y;

    // DBG_P(4, 3, 0x820220, 4, pG3->b.mKeyWrapTag, 4, pG3->b.mKeyWrapStatus,
             // 1, smSysInfo->d.NVMeData.d.SCU_CMD.d.KWState);  // <KW Tag> %08x %08x

    pG3->b.mKeyWrapTag = TCG_KEY_WRAP_SUPPORT_TAG;
    pG3->b.mKeyWrapStatus = TCG_KW_STATE_ENABLED_TAG;

    pG3->b.mWKey[0].idx = (U32)UID_K_AES_256_GRange_Key;
    pG3->b.mWKey[0].state = TCG_KEY_NULL;
    for (y = 1; y <= 8; y++)
    {
        pG3->b.mWKey[y].idx = (U32)UID_K_AES_256_Range1_Key + y - 1;
        pG3->b.mWKey[y].state = TCG_KEY_NULL;
    }

    pG3->b.mOpalWrapKEK[0].idx = (U32)UID_Authority_Anybody;
    for (y = 1; y <= 4; y++)
        pG3->b.mOpalWrapKEK[y].idx = (U32)UID_Authority_AdminX + y;
    for (y = 1; y <= 9; y++)
        pG3->b.mOpalWrapKEK[y+4].idx = (U32)UID_Authority_Users + y;
    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedKey); y++)
        pG3->b.mOpalWrapKEK[y].state = TCG_KEY_NULL;
}
*/


tcg_code U16 TcgTperReset(req_t *req)
{
    //printk("TperReset(): \n");
    DBG_P(0x01, 0x03, 0x710110 );  // TperReset():
    if(pG1->b.mAdmTPerInfo_Tbl.val[0].preset==TRUE)
    {
        // DBG_P(2, 3, 0x820110, 1, 0x00);  //OK  //82 01 10, "[F]TcgTperReset : %X", 1
        if(mSessionManager.state==SESSION_START)
            ResetSessionManager(req);

        host_Properties_Reset();

        LockingTbl_Reset(Programmatic);     // LckLocking_Tbl "ProgrammaReset"
        LockingRangeTable_Update();         //Update Read/Write LockedTable for Media Read/Write control

        MbrCtrlTbl_Reset(Programmatic);

        gTcgCmdState = ST_AWAIT_IF_SEND;

        return TRUE;
    }
    else
    {
        // DBG_P(2, 3, 0x820110, 1, 0xFF); //NG   //82 01 10, "[F]TcgTperReset : %X", 1
        return FALSE;
    }
}

tcg_code void TcgHardReset(void)
{
    //printk("HardReset(): \n");
    DBG_P(0x01, 0x03, 0x71011B );  // HardReset():

    req_t *req = MEM_AllocBuffer(sizeof(req_t), 32);
    if(req == NULL){
        TCG_ERR_PRN("Error!! HardReset() MEM_AllocBuffer Fail.\n");
        DBG_P(0x01, 0x03, 0x7F7F15);  // Error!! HardReset() MEM_AllocBuffer Fail.
    };

    if (mSessionManager.state == SESSION_START)
        ResetSessionManager(req);

    if(req != NULL){
        MEM_FreeBuffer(req);
    }

    host_Properties_Reset();

    // *** [ No Lock State Reset for HardReset
    //LockingTbl_Reset(PowerCycle);       // LckLocking_Tbl "ProgrammaReset"
    //LockingRangeTable_Update();         //Update Read/Write LockedTable for Media Read/Write control

    //MbrCtrlTbl_Reset(PowerCycle);
    // ] &&&
#if TCG_FS_BLOCK_SID_AUTH
    if (mTcgStatus&SID_HW_RESET)
        mTcgStatus &= ~(SID_BLOCKED + SID_HW_RESET);      // Clear Events are reset when a Clear Event occurs
#endif
    gTcgCmdState = ST_AWAIT_IF_SEND;
}

tcg_code void tcgSubSystemReset_Lenovo(void)  // Lenovo doesn't support subsystem reset but need lockOnReset
{
    LockingTbl_Reset(Hardware);         // LckLocking_Tbl "HardReset"
    LockingRangeTable_Update();         // Update Read/Write LockedTable for Media Read/Write control
    MbrCtrlTbl_Reset(Hardware);
}

tcg_code bool AatSecuriytActivated(void)
{
    if(gNvmeIfMgr.SecurityMgr.SecurityState == SCU_SEC1 || gNvmeIfMgr.SecurityMgr.SecurityState == SCU_SEC2)
        return FALSE;
    else
        return TRUE;
}

#if TCG_FS_BLOCK_SID_AUTH
tcg_code U16 TcgBlkSIDAuthentication(req_t *req)
{
    U16 status;
    // DBG_P(1, 3, 0x820111);   //82 01 11, "[F]TcgBlkSIDAuthentication"
    TCGPRN("TcgBlkSIDAuthentication() HwRst|%x\n", *((U8 *)req->req_prp.sec_xfer_buf));
    DBG_P(0x2, 0x03, 0x710003, 4, *((U8 *)req->req_prp.sec_xfer_buf));  // TcgBlkSIDAuthentication() HwRst|%x

    SATA_CMD_FreezeLock();

    if (CPinMsidCompare(CPIN_SID_IDX)) {
        status = STS_SUCCESS;
    }
    else if (mTcgStatus & SID_BLOCKED) {
        status = STS_INVALID_PARAMETER;
    }
    else {
        mTcgStatus |= SID_BLOCKED;

        // if (siloIdxCmd == 0)   // normal IF-SEND
            // HostTx2TcgBuf(pHcmdQ, buf);

        if (*((U8 *)req->req_prp.sec_xfer_buf) & 0x01)
            mTcgStatus |= SID_HW_RESET;

        status = STS_SUCCESS;
        //gTcgCmdState=ST_AWAIT_IF_SEND;
    }
    return status;
}
#endif

tcg_code void Tcg_GenCPinHash(U8 *pSrc, U8 srcLen, sCPin *pCPin)
{
#if _TCG_DEBUG
    memset((U8 *)pCPin->cPin_salt, 0, sizeof(pCPin->cPin_salt));
#else
    HAL_Gen_Key((U32 *)pCPin->cPin_salt, sizeof(pCPin->cPin_salt));
#endif
    HAL_PBKDF2((U32 *)pSrc,                 // pwd src
                 (U32)srcLen,               // pwd len
                 (U32 *)pCPin->cPin_salt,   // Salt val
                 sizeof(pCPin->cPin_salt),
                 (U32 *)pCPin->cPin_val);   // dest

    pCPin->cPin_Tag = CPIN_IN_PBKDF;
}


/***********************************************************
 *tcg_cmdPkt_extracter() <--- TcgCmdPktParser(BYTE* buf):
 *  IF-SEND data block is placed into structure "mCmdPkt"
 *  Packet format should be checked here.
 *
 *  If session has not started yet, HSN and TSN should be zero. HSN is initially
 *  transmitted in the StartSession method, and TSN is initially transmitted in
 *  the SyncSession method.
 *
 *  return STS_SUCCESS if no error. Check Test Cases 3.1.4
 ***********************************************************/
tcg_code U16 tcg_cmdPkt_extracter(req_t *req, U8* buf)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    SDS_t *pbuf = (SDS_t *)buf;
    U16 result  = STS_SUCCESS;
    U8  errcode = 0x00;

    *((U32*)mCmdPkt.rsv) = swap_u32(pbuf->ComPacket.Reserved);
    mCmdPkt.ComID        = swap_u16(pbuf->ComPacket.ComID);

    if(mCmdPkt.ComID != cmd->com_id)
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x10;     goto EXIT0; }

    mCmdPkt.ComIDExt    = swap_u16(pbuf->ComPacket.ExtendedComID);
    if(mCmdPkt.ComIDExt!=0x00)
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x20;     goto EXIT0; }

    mCmdPkt.Outstanding = swap_u32(pbuf->ComPacket.OutstandingData);
    mCmdPkt.MinTx       = swap_u32(pbuf->ComPacket.MinTransfer);
    mCmdPkt.length      = swap_u32(pbuf->ComPacket.Length);

    TCGPRN("tcg_cmdPkt_extracter() mCmdPkt.ComID|%x, mCmdPkt.length|%x\n", mCmdPkt.ComID, mCmdPkt.length);
    DBG_P(0x3, 0x03, 0x710004, 4, mCmdPkt.ComID, 4, mCmdPkt.length);  // tcg_cmdPkt_extracter() mCmdPkt.ComID|%x, mCmdPkt.length|%x
    if(mCmdPkt.length > cmd->len)
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x30;     goto EXIT0; }

    if(mCmdPkt.length < sizeof(Packet_t))
    {   result = STS_STAY_IN_IF_SEND;   errcode = 0x40;     goto EXIT0; }

    //PktFmt
    mCmdPkt.mPktFmt.TSN             = swap_u32(pbuf->Packet.TSN);
    mCmdPkt.mPktFmt.HSN             = swap_u32(pbuf->Packet.HSN);
    mCmdPkt.mPktFmt.SeqNo           = swap_u32(pbuf->Packet.SeqNumber);
    *((U16 *)mCmdPkt.mPktFmt.rsv)   = swap_u16(pbuf->Packet.Reserved);
    mCmdPkt.mPktFmt.AckType         = swap_u16(pbuf->Packet.AckType);
    mCmdPkt.mPktFmt.ack             = swap_u32(pbuf->Packet.Acknowledgement);
    mCmdPkt.mPktFmt.length          = swap_u32(pbuf->Packet.Length);

    //if length not fit or length<12, Regular Session aborted or packet discarded for Control Session...
    if ((mCmdPkt.mPktFmt.length > (mCmdPkt.length - sizeof(Packet_t))) ||
        (mCmdPkt.mPktFmt.length < sizeof(DataSubPacket_t)))
    {
        if(mSessionManager.state == SESSION_START)
        {   result = STS_SESSION_ABORT;     errcode = 0x50;     goto EXIT0; }
        else
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0x60;     goto EXIT0; }
    }

    //SubPktFmt
    *((U32*)&mCmdPkt.mSubPktFmt.rsv[0]) = swap_u32(pbuf->DataSubPacket.Reserved_DW);
    *((U16*)&mCmdPkt.mSubPktFmt.rsv[4]) = swap_u16(pbuf->DataSubPacket.Reserved_W);
    mCmdPkt.mSubPktFmt.kind             = swap_u16(pbuf->DataSubPacket.Kind);
    mCmdPkt.mSubPktFmt.length           = swap_u32(pbuf->DataSubPacket.Length);

    //if length exceeds the packet, Regular Session aborted or packet discarded for Control Session...
    if ((mCmdPkt.mSubPktFmt.length > (mCmdPkt.mPktFmt.length - sizeof(DataSubPacket_t))) ||
        ((mCmdPkt.mPktFmt.length - mCmdPkt.mSubPktFmt.length) > (12 * 2)))  // for Lenovo provide script (TCGMain_AutoBrief_PCIe.srt)
    {
        if(mSessionManager.state == SESSION_START)
        {   result = STS_SESSION_ABORT;     errcode = 0x70;     goto EXIT0; }
        else
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0x80;     goto EXIT0; }
    }
    ploadLen = mCmdPkt.mSubPktFmt.length;

    memcpy((void *)(&mCmdPkt.payload[0]), buf + offsetof(SDS_t, DataPayLoad), sizeof(mCmdPkt.payload));   //copy trusted send cmd buffer to mCmdPkt.payload buffer

    //CmdPkt format checking...
    if((mCmdPkt.mPktFmt.TSN == 0) && (mCmdPkt.mPktFmt.HSN == 0))
    { //Session Manager call only, for Test Cases A11-3-5-6-1-1
        bControlSession = TRUE;
        return STS_SUCCESS;
    }

    else if(mSessionManager.state == SESSION_START)
    {
        if((mCmdPkt.mPktFmt.TSN != mSessionManager.SPSessionID) ||
           (mCmdPkt.mPktFmt.HSN != mSessionManager.HostSessionID))
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0x90;     goto EXIT0; }

        bControlSession = FALSE;   //Regular Session
    }
    else //if(mSessionManager.state==SESSION_CLOSE)
    {
        if((mCmdPkt.mPktFmt.TSN != 0) || (mCmdPkt.mPktFmt.HSN != 0))
        {   result = STS_STAY_IN_IF_SEND;   errcode = 0xA0;     goto EXIT0; } //A8-1-1-1-1(3)

        bControlSession = TRUE;
    }

    //Token checking...
EXIT0:
    if(result != STS_SUCCESS)
    {
        if(result == STS_STAY_IN_IF_SEND){
            // DBG_P(1, 3, 0x820168);  //82 01 68, "!!NG: CmdPktParser NG -> Stay in IF-SEND, errcode= %X", 1
        }else{
            // DBG_P(1, 3, 0x820169);  //82 01 69, "!!NG: CmdPktParser NG -> Abort Session, errcode= %X", 1
        }
        // DBG_P(1, 1, errcode);
        TCGPRN("result|%x, errcode|%x\n", result, errcode);
        DBG_P(0x3, 0x03, 0x710005, 4, result, 4, errcode);  // result|%x, errcode|%x
    }
    // errcode++;  errcode--;  // alexcheck
    return result;

}

/***********************************************************
 *tcg_cmdPkt_payload_decoder() ---> TcgStreamDecode(void):
 *  Decode the subpacket payload here. See Core 3.2.4.2
 *  =>  Method Header + Parameters + Status
 *
 *  Payload is decoded from TOK_Call (0xF8) to MethodUID only in this subrourtine.
 *  The remaining payload decoding (TOK_StartList, ...) should be followed by the
 *  corresponding function call.
 *
 *  Response Data (dataBuf[]) should also be prepared here.
 *
 *  return 0 if no error.
 ***********************************************************/
//-------------------Transaction Ok-------------------------
tcg_code bool cb_transaction_ok(Complete)(req_t *req)
{
    TCGPRN("cb_transaction_ok_Complete()\n");
    DBG_P(0x01, 0x03, 0x710006 );  // cb_transaction_ok_Complete()

#if 1// CO_SUPPORT_AES
    // for method_set, method_genkey, method_erase
    TcgUpdateRawKeyList(mRawKeyUpdateList);
#endif

    if (flgs_MChnged.b.G3)
    {
        //D4-1-4-1-1: only update LockingRange after Transaction is done!
        LockingRangeTable_Update(); // update AES key/range setting if changed.
    }

    ClearMtableChangedFlag();
    //D5-1-3-1-1
    if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE) &&
        (pG3->b.mLckMbrCtrl_Tbl.val[0].done  == FALSE))
        mTcgStatus |= MBR_SHADOW_MODE;
    else
        mTcgStatus &= (~MBR_SHADOW_MODE);

    mSessionManager.TransactionState = TRNSCTN_IDLE;
    method_complete_post(req, FALSE);
    return TRUE;
}

#if 0
tcg_code bool cb_transaction_ok(DSCommit)(req_t *req)
{
    TCGPRN("cb_transaction_ok_DSCommit()\n");
    DBG_P(0x01, 0x03, 0x710007 );  // cb_transaction_ok_DSCommit()
    if(flgs_MChnged.b.DS)
        tcg_ipc_post(req, MSG_TCG_DSCOMMIT, cb_transaction_ok(Complete));
    else
        cb_transaction_ok(Complete)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ok(SMBRCommit)(req_t *req)
{
    TCGPRN("cb_transaction_ok_SMBRCommit()\n");
    DBG_P(0x01, 0x03, 0x710008 );  // cb_transaction_ok_SMBRCommit()
    if(flgs_MChnged.b.SMBR)
        tcg_ipc_post(req, MSG_TCG_SMBRCOMMIT, cb_transaction_ok(DSCommit));
    else
        cb_transaction_ok(DSCommit)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ok(G3Wr)(req_t *req)
{
    TCGPRN("cb_transaction_ok_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x710009 );  // cb_transaction_ok_G3Wr()
    if(flgs_MChnged.b.G3)
        tcg_ipc_post(req, MSG_TCG_G3WR, cb_transaction_ok(SMBRCommit));
    else
        cb_transaction_ok(SMBRCommit)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ok(G2Wr)(req_t *req)
{
    TCGPRN("cb_transaction_ok_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x71000A );  // cb_transaction_ok_G2Wr()
    if(flgs_MChnged.b.G2)
        tcg_ipc_post(req, MSG_TCG_G2WR, cb_transaction_ok(G3Wr));
    else
        cb_transaction_ok(G3Wr)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ok(G1Wr)(req_t *req)
{
    TCGPRN("cb_transaction_ok_G1Wr()\n");
    DBG_P(0x01, 0x03, 0x71000B );  // cb_transaction_ok_G1Wr()
    if(flgs_MChnged.b.G1)
        tcg_ipc_post(req, MSG_TCG_G1WR, cb_transaction_ok(G2Wr));
    else
        cb_transaction_ok(G2Wr)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ok(Begin)(req_t *req)
{
    TCGPRN("cb_transaction_ok_Begin()\n");
    DBG_P(0x01, 0x03, 0x71000C );  // cb_transaction_ok_Begin()
    if (mSessionManager.Write != 0x00){   // check write bit in start session payload
        cb_transaction_ok(G1Wr)(req);
    }
    return TRUE;
}
#else
tcg_code bool cb_transaction_ok(tblUpdate)(req_t *req)
{
    TCGPRN("cb_transaction_ok_tblUpdate()\n");
    DBG_P(0x01, 0x03, 0x710124 );  // cb_transaction_ok_tblUpdate()

    tcg_ipc_post_xx(req, MSG_TCG_TBL_UPDATE, cb_transaction_ok(Complete), flgs_MChnged.all32);
    return TRUE;
}

tcg_code bool cb_transaction_ok(Begin)(req_t *req)
{
    TCGPRN("cb_transaction_ok_Begin()\n");
    DBG_P(0x01, 0x03, 0x71000C );  // cb_transaction_ok_Begin()
    if (mSessionManager.Write != 0x00){   // check write bit in start session payload
        cb_transaction_ok(tblUpdate)(req);
    }
    return TRUE;
}
#endif

//-------------------Transaction Ng-------------------------
tcg_code bool cb_transaction_ng(Complete)(req_t *req)
{
    TCGPRN("cb_transaction_ng_Complete()\n");
    DBG_P(0x01, 0x03, 0x71000D );  // cb_transaction_ng_Complete()
#if CO_SUPPORT_AES
    if (bKeyChanged)
    {
        // set TRUE only method_genkey and method_erase
        // Use Old KEK to Unwrap Old WKey.
        //Get_KeyWrap_KEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], WrapKEK);
        //if (TCG_ACT_IN_OPAL())
        //{
        //    TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
        //}
        //else
        if (TCG_ACT_IN_ALL_SU())
        {
            U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
            TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
        }

        TcgUpdateRawKeyList(mRawKeyUpdateList);
    }
    bKeyChanged = FALSE;
#endif
    ClearMtableChangedFlag();

    mSessionManager.TransactionState = TRNSCTN_IDLE;
    method_complete_post(req, FALSE);
    return TRUE;
}

#if 0
tcg_code bool cb_transaction_ng(TDSClear)(req_t *req)
{
    TCGPRN("cb_transaction_ng_TDSClear()\n");
    DBG_P(0x01, 0x03, 0x71000E );  // cb_transaction_ng_TDSClear()
    if(flgs_MChnged.b.DS)
        tcg_ipc_post(req, MSG_TCG_TDSCLEAR, cb_transaction_ng(Complete));
    else
        cb_transaction_ng(Complete)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ng(TSMBRClear)(req_t *req)
{
    TCGPRN("cb_transaction_ng_TSMBRClear()\n");
    DBG_P(0x01, 0x03, 0x71000F );  // cb_transaction_ng_TSMBRClear()
    if(flgs_MChnged.b.SMBR)
        tcg_ipc_post(req, MSG_TCG_TSMBRCLEAR, cb_transaction_ng(TDSClear));
    else
        cb_transaction_ng(TDSClear)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ng(G3Rd)(req_t *req)
{
    TCGPRN("cb_transaction_ng_G3Rd()\n");
    DBG_P(0x01, 0x03, 0x710010 );  // cb_transaction_ng_G3Rd()
    if(flgs_MChnged.b.G3)
        tcg_ipc_post(req, MSG_TCG_G3RD, cb_transaction_ng(TSMBRClear));
    else
        cb_transaction_ng(TSMBRClear)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ng(G2Rd)(req_t *req)
{
    TCGPRN("cb_transaction_ng_G2Rd()\n");
    DBG_P(0x01, 0x03, 0x710011 );  // cb_transaction_ng_G2Rd()
    if(flgs_MChnged.b.G2)
        tcg_ipc_post(req, MSG_TCG_G2RD, cb_transaction_ng(G3Rd));
    else
        cb_transaction_ng(G3Rd)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ng(G1Rd)(req_t *req)
{
    TCGPRN("cb_transaction_ng_G1Rd()\n");
    DBG_P(0x01, 0x03, 0x710012 );  // cb_transaction_ng_G1Rd()
    if(flgs_MChnged.b.G1)
        tcg_ipc_post(req, MSG_TCG_G1RD, cb_transaction_ng(G2Rd));
    else
        cb_transaction_ng(G2Rd)(req);
    return TRUE;
}

tcg_code bool cb_transaction_ng(Begin)(req_t *req)
{
    TCGPRN("cb_transaction_ng_Begin()\n");
    DBG_P(0x01, 0x03, 0x710013 );  // cb_transaction_ng_Begin()
    cb_transaction_ng(G1Rd)(req);
    return TRUE;
}
#else

tcg_code bool cb_transaction_ng(tblRecovery)(req_t *req)
{
    TCGPRN("cb_transaction_ng_tblRecovery)\n");
    DBG_P(0x01, 0x03, 0x710125 );  // cb_transaction_ng_tblRecovery)

    tcg_ipc_post_xx(req, MSG_TCG_TBL_RECOVERY, cb_transaction_ng(Complete), flgs_MChnged.all32);
    return TRUE;
}

tcg_code bool cb_transaction_ng(Begin)(req_t *req)
{
    TCGPRN("cb_transaction_ng_Begin()\n");
    DBG_P(0x01, 0x03, 0x710126 );  // cb_transaction_ng_Begin()

    cb_transaction_ng(tblRecovery)(req);
    return TRUE;
}

#endif

//-------------------tcg_cmdPkt_payload_decoder-------------------------
tcg_code U32 tcg_cmdPkt_payload_decoder(req_t *req)
{
    U8  byte;
    U32 tmp32;
    U32 result = STS_SUCCESS;

    iPload = 0;   //reset payload index
    if (bControlSession)
    {   // Session is not started => Control Session, only accept SMUID
        // Control Session, TSN/HSN=0x00, A6-3-1...
        byte = ChkToken();
        if (byte == TOK_Call)
        {   //1. Call token, start of a method invocation...
            //2. check method header:

            // get Invoking UID
            if (AtomDecoding_Uid2(invokingUID.bytes) != STS_SUCCESS)
                return STS_STAY_IN_IF_SEND;

            if (invokingUID.all != SMUID)
                return STS_STAY_IN_IF_SEND;

            // get Method UID
            if (AtomDecoding_Uid2(methodUID.bytes) != STS_SUCCESS)
                return STS_STAY_IN_IF_SEND;

            result = invoking_session_manager(req);
            if (result != STS_SUCCESS)
            {
                if (result & 0xff00)
                    return result;  //STS_SESSION_ABORT or STS_STAY_IN_IF_SEND
            }
            //add status to reponse buffer and update length
            set_status_code(result, 0, 0);
            tcg_prepare_respPacket_update(TRUE);
            return result;
        }
        else
        {   //Upexpected!
            return STS_STAY_IN_IF_SEND;
        }
    }
    else
    {   //TSN/HSN!=0x00, Session is started => Regular Session, A6-0-1...

        // 0. check Control Tokens first
        byte = ChkToken();
        if (byte == TOK_Call)
        {   //1. Call token, start of a method invocation...
            //2. check method header:
            // get Invoking UID
            if (AtomDecoding_Uid2(invokingUID.bytes) != STS_SUCCESS)
            {
                result = STS_NOT_AUTHORIZED;
                goto UPDATE_STATUS;
            }

            // get Method UID
            if (AtomDecoding_Uid2(methodUID.bytes) != STS_SUCCESS)
            {
                result = STS_NOT_AUTHORIZED;
                goto UPDATE_STATUS;
            }

            //3. parameters...
            //4. End of data
            //5. Status code list
            if(invokingUID.all == SMUID)
                result = invoking_session_manager(req);  // Session Manager
            else
                result = invoking_tcg_table(req);

UPDATE_STATUS:
            if(result != STS_SUCCESS)
            {
                if ((result == STS_SESSION_ABORT) || (result == STS_STAY_IN_IF_SEND))
                    return result;

                if (result == STS_SUCCESS_THEN_ABORT)
                { //??? D10-3-3-1-1
                    ResetSessionManager(req);
                    result = STS_SUCCESS;
                }

                fill_no_data_token_list();    //cj: need these lines for RevertSP, 02/15/2013
            }

            //add status to reponse buffer and update length
            set_status_code(result, 0, 0);

            if(req->completion == nvmet_core_cmd_done){
                result = tcg_prepare_respPacket_update(TRUE);
            }

            return result;
        }
        else if (byte == TOK_EndOfSession)
        {
            //EOD, no status ...
            //for(j=0; j<3; j++)     mSessionManager.status[j] = mCmdPkt.payload[iPload++];
            DBG_P(1, 3, 0x710135);  //71 01 35, "Close Session >>>"
            invokingUID.all = SMUID;
            methodUID.all   = SM_MTD_CloseSession;

            ResetSessionManager(req);

            //prepare payload
            dataBuf[iDataBuf++] = TOK_EndOfSession;
            tcg_prepare_respPacket_update(FALSE);

            return STS_SUCCESS;
        }
        else if (byte==TOK_StartTransaction)
        {
            result = AtomDecoding_Uint((U8*)&tmp32, sizeof(tmp32));
            // DBG_P(3, 3, 0x82016B, 2, result, 4, mSessionManager.TransactionState);  //82 01 6B, "TransactionStart Status=%X, State=%X", 2 4
            if (result != STS_SUCCESS) // no status code
                return STS_SESSION_ABORT;

            if (mSessionManager.TransactionState != TRNSCTN_IDLE)
                return STS_SESSION_ABORT;  // found Transaction token

            // Enable Transaction
            dataBuf[iDataBuf++] = TOK_StartTransaction;
            dataBuf[iDataBuf++] = 0;   //ok, for response Transaction status
            tcg_prepare_respPacket_update(FALSE);
            mSessionManager.TransactionState = TRNSCTN_ACTIVE;
#if CO_SUPPORT_AES
            bKeyChanged = FALSE;
            //mRawKeyUpdateList = 0;
#endif
            // DBG_P(1, 3, 0x82016C);  //82 01 6C, "TransactionStart OK"
            return STS_SUCCESS;
        }
        else if (byte == TOK_EndTransaction)
        {
            result = AtomDecoding_Uint((U8*)&tmp32, sizeof(tmp32));
            TCGPRN("result|%x, TransactionState|%x", result, mSessionManager.TransactionState);
            DBG_P(0x3, 0x03, 0x710014, 4, result, 4, mSessionManager.TransactionState);  // result|%x, TransactionState|%x
            // DBG_P(3, 3, 0x82016D, 2, result, 4, mSessionManager.TransactionState);  //82 01 6D, "TransactionEnd Status=%X, State=%X", 2 4
            if (result != STS_SUCCESS)
                return STS_SESSION_ABORT;  //no status code

            if (mSessionManager.TransactionState != TRNSCTN_ACTIVE)
                return STS_SESSION_ABORT;

            dataBuf[iDataBuf++] = TOK_EndTransaction;

            if (tmp32 == 0)
            { //transaction commit
                TCGPRN("TransactionEnd OK: Commit~~");
                DBG_P(0x01, 0x03, 0x710015 );  // TransactionEnd OK: Commit~~
                // DBG_P(1, 3, 0x82016E);  //82 01 6E, "TransactionEnd OK: Commit!!"
                dataBuf[iDataBuf++] = 0;
            #if CO_SUPPORT_AES
                if (bKeyChanged) // set TRUE only method_genkey and method_erase
                {
                    //cjdbg, Fde_ResetCache(FDE_RST_SUBOP_REGEN);
                    // for method_genkey
                    TcgUpdateWrapKeyList(mRawKeyUpdateList);
                    bKeyChanged = FALSE;
                }
            #endif
            #if 1
                cb_transaction_ok(Begin)(req);
            #else
                WriteMtable2NAND(req);

                if (flgs_MChnged.b.G3)
                {
                #if CO_SUPPORT_AES
                    // for method_set, method_genkey, method_erase
                    TcgUpdateRawKeyList(mRawKeyUpdateList);
                #endif
                    // D4-1-4-1-1: only update LockingRange after Transaction is done!
                    LockingRangeTable_Update(); // update AES key/range setting if changed.
                }

                ClearMtableChangedFlag();
                //D5-1-3-1-1
                if ((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE) &&
                    (pG3->b.mLckMbrCtrl_Tbl.val[0].done  == FALSE))
                    mTcgStatus |= MBR_SHADOW_MODE;
                else
                    mTcgStatus &= (~MBR_SHADOW_MODE);
            #endif
                //D8-1-3-1-1
            }
            else //if(tmp32)
            { //transaction abort
                // DBG_P(1, 3, 0x82016F);  //82 01 6F, "TransactionEnd OK: Abort!!"
                dataBuf[iDataBuf++] = 1;

            #if 1
                cb_transaction_ng(Begin)(req);
            #else
                ReadNAND2Mtable(req);

              #if CO_SUPPORT_AES
                if (bKeyChanged)
                {
                    // set TRUE only method_genkey and method_erase
                    // Use Old KEK to Unwrap Old WKey.
                    //Get_KeyWrap_KEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], WrapKEK);
                    //if (TCG_ACT_IN_OPAL())
                    //{
                    //    TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
                    //}
                    //else
                    if (TCG_ACT_IN_ALL_SU())
                    {
                        U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
                        TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
                    }

                    TcgUpdateRawKeyList(mRawKeyUpdateList);
                }
                bKeyChanged = FALSE;
              #endif
                ClearMtableChangedFlag();

                //mTcgStatus = 0;
                //if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle==manufactured_inactive)
                //    mTcgStatus |= TCG_ACTIVATED;

                //if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable==TRUE)&&(pG3->b.mLckMbrCtrl_Tbl.val[0].done==FALSE))
                //    mTcgStatus |= MBR_SHADOW_MODE;

                //LockingRangeTable_Update();
            #endif
            }
            if(req->completion == nvmet_core_cmd_done){
                tcg_prepare_respPacket_update(FALSE);
                mSessionManager.TransactionState = TRNSCTN_IDLE;
            }

            return STS_SUCCESS;
        }
        else
        {   //Invalid token, session abort! (Test Cases 3.1.5)
            return STS_SESSION_ABORT;
        }
        //return result;
    }
}

#if _TCG_ == TCG_EDRV
/****************************************************************************
 * tcg_cmdPkt_closeSession():  <--- TcgCmdPkt4CloseSession()
 ****************************************************************************/
tcg_code void tcg_cmdPkt_closeSession(void)
{
    SDS_t *pbuf = (SDS_t *)dataBuf;
    SS_rPayLoad_t *ppl = (SS_rPayLoad_t *)(dataBuf + offsetof(SDS_t, DataPayLoad));  // point of payload

    memset(dataBuf, 0x00, sizeof(SDS_t) + sizeof(SS_rPayLoad_t) + sizeof(U32));      //clear Tcg Recv Buffer, sizeof(U32) is extra for multiple of 4
    ppl->CallToken      = TOK_Call;
    ppl->s_Atom0        = 0xA8;
    ppl->InvokingUID    = swap_u64((U64)SMUID);
    ppl->s_Atom1        = 0xA8;
    ppl->MethodUID      = swap_u64((U64)SM_MTD_CloseSession);
    ppl->StartListToken = TOK_StartList;
    ppl->s_Atom2        = 0x84;
    ppl->HSN            = swap_u32((U32)1);  // swap_u32(mSessionManager.HostSessionID);
    ppl->s_Atom3        = 0x84;
    ppl->TSN            = swap_u32(mCmdPkt.mPktFmt.TSN);
    ppl->EndListToken   = TOK_EndList;
    ppl->EndOfDataToken = TOK_EndOfData;
    set_status_with_token_list(0, 0, 0, ppl->MethodStatusList);

    rcvCmdPktLen = offsetof(SDS_t, DataPayLoad) + offsetof(SS_rPayLoad_t, Pad);

    pbuf->DataSubPacket.Length = swap_u32((U32)offsetof(SS_rPayLoad_t, Pad));
    pbuf->Packet.Length        = swap_u32(occupied_by(pbuf->DataSubPacket.Length + sizeof(DataSubPacket_t), sizeof(U32)) * sizeof(U32));  // length is multiple of U32
    pbuf->ComPacket.Length     = swap_u32(pbuf->Packet.Length + sizeof(Packet_t));
    pbuf->ComPacket.ComID      = swap_u16(BASE_COMID);
}
#endif


/***********************************************************
 *tcg_cmdPkt_abortSession(void): <--- TcgAbortSession()
 *
 *  dataBuffer for IF-RECV
 *  1. Add the Method Status List
 *  2. Update the payload length
 *
 *  return STS_SUCCESS if no error.
 ***********************************************************/
tcg_code U16 tcg_cmdPkt_abortSession(void)
{
    SDS_t *pbuf = (SDS_t *)dataBuf;
    SS_rPayLoad_t *ppl = (SS_rPayLoad_t *)(dataBuf + offsetof(SDS_t, DataPayLoad));  // point of payload

    memset(dataBuf, 0x00, sizeof(SDS_t) + sizeof(SS_rPayLoad_t) + sizeof(U32));      //clear Tcg Recv Buffer, sizeof(U32) is extra for multiple of 4
    pbuf->ComPacket.ComID = swap_u16(BASE_COMID);
    //Construct payload  for "Close Session"
    ppl->CallToken      = TOK_Call;
    ppl->s_Atom0        = 0xA8;
    ppl->InvokingUID    = swap_u64((U64)SMUID);
    ppl->s_Atom1        = 0xA8;
    ppl->MethodUID      = swap_u64((U64)SM_MTD_CloseSession);
    ppl->StartListToken = TOK_StartList;
    ppl->s_Atom2        = 0x84;
    ppl->HSN            = swap_u32(mSessionManager.HostSessionID);
    ppl->s_Atom3        = 0x84;
    ppl->TSN            = swap_u32(mSessionManager.SPSessionID);
    ppl->EndListToken   = TOK_EndList;
    ppl->EndOfDataToken = TOK_EndOfData;
    iDataBuf = offsetof(SDS_t, DataPayLoad) + offsetof(SS_rPayLoad_t, MethodStatusList[0]);

    set_status_code(0, 0, 0);
    tcg_prepare_respPacket_update(TRUE);
    return STS_SUCCESS;
}

/***********************************************************
 * invoking_session_manager()  <--  InvokingSessionManager()
 * Decode and process Method invoked at SessionManager layer
 * return 0 if success
 ***********************************************************/
tcg_code U16 invoking_session_manager(req_t *req)
{
    U64 uid;
    U32 j;
    U16 result = STS_SUCCESS;
    U8 byte, errCode = 0;

    // Invoking UID: Session Manager
    TCGPRN("invoking_session_manager(), methodUID.all|%08x%08x\n", methodUID.dw[1], methodUID.dw[0]);
    DBG_P(0x3, 0x03, 0x710016, 4, methodUID.dw[1], 4, methodUID.dw[0]);  // invoking_session_manager(), methodUID.all|%x%x
    switch (methodUID.all)
    {
        case SM_MTD_Properties:
            result = tcg_properties(req);
            return result;

        case SM_MTD_StartSession:
            TCGPRN("<<< Start Session...\n");
            DBG_P(0x01, 0x03, 0x710017 );  // <<< Start Session...

            if (mSessionManager.state == SESSION_START)
            {   /* session already started, error */
                if (bControlSession)
                {
                    errCode = 0x02;   result = STS_NO_SESSIONS_AVAILABLE;
                }
                else
                {
                    errCode = 0x04;   result = STS_NOT_AUTHORIZED;
                }     //;
                goto SYNC_REPLY;
            }

            // TODO: clear session manager first
            // ref: Core3.2.4.1, Parameters
            //1. Start List token
            if ((byte = ChkToken()) != TOK_StartList)
            {
                errCode = 0x10; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            //2. Required parameters: HostSessionID:uint, SPID:uid, write:bool
            if (AtomDecoding_Uint((U8*)&mSessionManager.HostSessionID, sizeof(mSessionManager.HostSessionID)) != STS_SUCCESS)
            {
                errCode = 0x12; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            if (AtomDecoding_Uid2(mSessionManager.SPID.bytes) != STS_SUCCESS)
            {
                errCode = 0x14; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            if ((mSessionManager.SPID.all != UID_SP_Admin) && (mSessionManager.SPID.all != UID_SP_Locking))
            {
                errCode = 0x18;   result = STS_INVALID_PARAMETER;   goto SYNC_REPLY;
            }

            if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                if (pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle != manufactured)
                {   //inactive
                    TCG_ERR_PRN("!!NG: Not Activate Yet\n");
                    DBG_P(0x01, 0x03, 0x7F7F00 );  // !!NG: Not Activate Yet
                    // DBG_P(1, 3, 0x820174);  //82 01 74, "!!NG: Not Activate Yet"
                    errCode = 0x1A;   result = STS_INVALID_PARAMETER;   goto SYNC_REPLY;
                }
            }

            // Write-enabled bit
            if (AtomDecoding_Uint(&mSessionManager.Write, sizeof(mSessionManager.Write)) != STS_SUCCESS)
            {
                errCode = 0x20;     result = STS_STAY_IN_IF_SEND;       goto MNG_EXIT;
            }

            if ((mSessionManager.Write != 0x00) && (mSessionManager.Write != 0x01))
            {
                errCode = 0x22;     result = STS_INVALID_PARAMETER;     goto SYNC_REPLY;
            }

            //3. Optional parameters, must be in order: [0]HostSessionID = byte, [3]HostSigningAuthority = uid, [5]SessionTimeout = uint
            mSessionManager.HtSgnAuthority[mSessionManager.wptr_auth++].all = UID_Authority_Anybody;
            mSessionManager.HtChallenge[0]     = 0;

            //mSessionManager.sessionTimeout = G1.b.mAdmSPInfo_Tbl.val[0].spSessionTimeout;
            //if (mSessionManager.sessionTimeout == 0)
                mSessionManager.sessionTimeout = DEF_SESSION_TIMEOUT;

            if ((byte = ChkToken()) == TOK_StartName) //3.1 Start Name token
            {
                //3.2 encoded name, 3.3 encoded value, 3.4 End Name token
                //check Tiny Token
                if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                {
                    errCode = 0x24;  result = STS_INVALID_PARAMETER;  /* need to check ... */  goto SYNC_REPLY;
                }

                if (byte == 0x00)
                {   // HostChallenge
                    U32 len;

                    result = AtomDecoding_ByteHdr(&len);
                    if (result == STS_SUCCESS)
                    {
                        if(len > CPIN_LENGTH)
                        {
                            errCode = 0x25;  result = STS_INVALID_PARAMETER; /* need to check ... */  goto SYNC_REPLY;
                        }

                        memset(mSessionManager.HtChallenge, 0, sizeof(mSessionManager.HtChallenge));
                        mSessionManager.HtChallenge[0] = (U8)len;
                        for (j = 0; j < len; j++)
                            mSessionManager.HtChallenge[j + 1] = mCmdPkt.payload[iPload++];

                        if (ChkToken() != TOK_EndName)
                        {
                            errCode = 0x30; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
                        }

                        if ((byte = ChkToken()) != TOK_StartName)
                            goto END_LIST;

                        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                        {
                            errCode = 0x32;  result = STS_INVALID_PARAMETER; /*need to check ... */  goto SYNC_REPLY;
                        }
                    }
                }

                if (byte == 0x03)
                {   // HostSigningAuthority
                    if (AtomDecoding_Uid2(mSessionManager.HtSgnAuthority.bytes) != STS_SUCCESS)
                    {
                        errCode = 0x34; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
                    }

                    if (ChkToken() != TOK_EndName)
                    {
                        errCode = 0x35; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
                    }
                    if ((byte = ChkToken()) != TOK_StartName)
                        goto END_LIST;

                    if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                    {
                        errCode = 0x36;  result = STS_INVALID_PARAMETER;       //need to check ...
                        goto SYNC_REPLY;
                    }
                }

                if (byte == 0x05)
                { // SessionTimeout
                    if (AtomDecoding_Uint((U8*)&mSessionManager.sessionTimeout, sizeof(mSessionManager.sessionTimeout)) != STS_SUCCESS )
                    {
                        errCode = 0x38;  result = STS_STAY_IN_IF_SEND;  goto MNG_EXIT;
                    }

                    /* if (mSessionManager.sessionTimeout == 0)
                    {
                        if ((MAX_SESSION_TIMEOUT != 0) && (G1.b.mAdmSPInfo_Tbl.val[0].spSessionTimeout != 0))
                        {  errCode = 0x39; result = STS_INVALID_PARAMETER; goto SYNC_REPLY;  }
                    } */

                    if (mSessionManager.sessionTimeout)
                    {
                        TCGPRN("SessTimeout|%lld\n", mSessionManager.sessionTimeout);
                        DBG_P(0x2, 0x03, 0x71011D, 4, mSessionManager.sessionTimeout);  // SessTimeout|%lld
                        if ((mSessionManager.sessionTimeout < MIN_SESSION_TIMEOUT)
                        || ((MAX_SESSION_TIMEOUT != 0) && (mSessionManager.sessionTimeout > MAX_SESSION_TIMEOUT)))
                        {
                            errCode = 0x39;
                            result = STS_INVALID_PARAMETER;       //need to check ...
                            goto SYNC_REPLY;
                        }
                    }

                    if (ChkToken() != TOK_EndName)
                    {
                        errCode = 0x3B;  result = STS_STAY_IN_IF_SEND;  goto MNG_EXIT;
                    }

                    // get EndList
                    if ((byte = ChkToken()) == TOK_StartName)
                    { // more optional parameters..., A6-3-4-2-1(3)
                        errCode = 0x3C;
                        result = STS_INVALID_PARAMETER;
                        goto SYNC_REPLY;
                    }
                }
                else
                {
                    //more optional parameters...
                    errCode = 0x3A;   result = STS_INVALID_PARAMETER;   goto END_LIST;
                }
            }

        END_LIST:
            if (byte != TOK_EndList)
            {
                errCode = 0x40; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            //4. End of Data token
            if (ChkToken() != TOK_EndOfData)
            {
                errCode = 0x42; result = STS_STAY_IN_IF_SEND; goto MNG_EXIT;
            }

            //5. Status Code list
            result = chk_method_status();
            if (result == STS_SESSION_ABORT)
            {   //Control Session, A6-3-8-6-1
                result = STS_STAY_IN_IF_SEND;   errCode = 0x44;  goto MNG_EXIT;
            }

            TCGPRN("... SP|%02x AUTH|%08x\n", (U8)mSessionManager.SPID.all, (U32)mSessionManager.HtSgnAuthority.all);
            DBG_P(0x3, 0x03, 0x710019, 1, (U8)mSessionManager.SPID.all, 4, (U32)mSessionManager.HtSgnAuthority.all);  // ... SP|%02x AUTH|%08x
            // DBG_P(3, 3, 0x820175, 1, (U8)mSessionManager.SPID.all, 4, (U32)mSessionManager.HtSgnAuthority.all);  //82 01 75, "... SP:[%02X] AUTH:[%08X]", 1 4

            if (result == STS_SUCCESS)
            { //papyload check ok, check authority...  prepare SyncSession data
                #if (TCG_FS_PSID == FALSE)  // PSID is not supported!!
                if (mSessionManager.HtSgnAuthority.all == UID_Authority_PSID)
                    result = STS_NOT_AUTHORIZED;
                else
                #endif
                #if TCG_FS_BLOCK_SID_AUTH
                if ((mTcgStatus & SID_BLOCKED) && (mSessionManager.HtSgnAuthority.all == UID_Authority_SID))
                    result = STS_NOT_AUTHORIZED;
                else
                #endif
                result = host_signing_authority_check();
                if (result == STS_SUCCESS)
                { // SessionStart OK!
                    mSessionManager.state = SESSION_START;
                    mSessionManager.SPSessionID += 1;   //start from 0x1001;   //assigned by device, add 1 per session start-up ok

                    // DBG_P(2, 3, 0x820176, 4,  mSessionManager.SPSessionID);  //82 01 76, "Sync Session [%X]", 4
                    //printk("Sync Session [%X]", mSessionManager.SPSessionID);
                    DBG_P(0x2, 0x03, 0x71001A, 4, mSessionManager.SPSessionID);  // Sync Session [%X]
                    //result =  STS_SUCCESS;
                    #if CO_SUPPORT_AES
                    if (mSessionManager.SPID.all == UID_SP_Locking)
                    {
                        if (TCG_ACT_IN_OPAL())
                        {
                            TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
                        }
                        else if (TCG_ACT_IN_ALL_SU())
                        {
                            U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
                            TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
                        }
                    }
                    #endif

                    if (mSessionManager.sessionTimeout > 0)
                        mSessionManager.bWaitSessionStart = 1;
                }
                else
                { // authority check NG!
                    TCG_ERR_PRN("!!NG authority check, result|%x\n", result);
                    DBG_P(0x2, 0x03, 0x7F7F01, 4, result);  // !!NG authority check, result|%x
                    // DBG_P(2, 3, 0x820177, 2,  result);  //82 01 77, "!!NG: AuthorityCheck : %04X", 2
                    //errCode=0x10;
                    //result = STS_INVALID_PARAMETER;
                }
            }

        SYNC_REPLY:

            //prepare payload: SyncSession data
            dataBuf[iDataBuf++] = TOK_Call;
            AtomEncoding_Int2Byte(invokingUID.bytes, sizeof(U64));

            uid = SM_MTD_SyncSession;
            AtomEncoding_Int2Byte((U8*)&uid, sizeof(U64));

            dataBuf[iDataBuf++] = TOK_StartList;

            dataBuf[iDataBuf++] = 0x84; //short atom
            *((U32 *)&dataBuf[iDataBuf]) = swap_u32(mSessionManager.HostSessionID);
            iDataBuf += sizeof(U32);

            if (result == STS_SUCCESS)
            {
                dataBuf[iDataBuf++] = 0x84; //short atom
                *((U32 *)&dataBuf[iDataBuf]) = swap_u32(mSessionManager.SPSessionID);
                iDataBuf += sizeof(U32);
            }
            else
                dataBuf[iDataBuf++] = 0x00;

            dataBuf[iDataBuf++] = TOK_EndList;
            dataBuf[iDataBuf++] = TOK_EndOfData;

        MNG_EXIT:
            if (errCode) {
                TCG_ERR_PRN("!!NG: SessionManager %02x\n", errCode);
                DBG_P(0x2, 0x03, 0x7F7F02, 1, errCode);  // !!NG: SessionManager %02x
                // DBG_P(2, 3, 0x820178, 1,  errCode);  //82 01 78, "!!NG: SessionManager, errCode = %02X", 1
            }
            return result;

        default:
            return  STS_STAY_IN_IF_SEND;
    }
}

/***********************************************************
 * host_signing_authority_check()  <--  HostSigningAuthorityCheck()
 ***********************************************************/
tcg_code U16 host_signing_authority_check(void)
{
    U64 tgtCPinUid;
    U16 j, i; //, k;
    U16 authRowCnt, cpinRowCnt;
    U8 digest[SHA256_DIGEST_SIZE];

    sAuthority_TblObj *pAuthTblObj;
    sCPin_TblObj *pCPinTblObj;


    // *WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison
    bool bCpinChkFail;
    // *WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison

    TCGPRN("host_signing_authority_check() SPID|%08x%08x\n", mSessionManager.SPID.dw[1], mSessionManager.SPID.dw[0]);
    DBG_P(0x3, 0x03, 0x71001D, 4, mSessionManager.SPID.dw[1], 4, mSessionManager.SPID.dw[0]);  // host_signing_authority_check() SPID|%08x%08x
    if (mSessionManager.SPID.all == UID_SP_Admin)
    {
        authRowCnt = pG1->b.mAdmAuthority_Tbl.hdr.rowCnt;
        pAuthTblObj = pG1->b.mAdmAuthority_Tbl.val;

        cpinRowCnt = pG1->b.mAdmCPin_Tbl.hdr.rowCnt;
        pCPinTblObj = pG1->b.mAdmCPin_Tbl.val;
    }
    else
    {
        authRowCnt = pG3->b.mLckAuthority_Tbl.hdr.rowCnt;
        pAuthTblObj = pG3->b.mLckAuthority_Tbl.val;

        cpinRowCnt = pG3->b.mLckCPin_Tbl.hdr.rowCnt;
        pCPinTblObj = pG3->b.mLckCPin_Tbl.val;
    }

    for (j = 0; j < authRowCnt; j++)
    { // row# (4) can be acquired from Table tbl;
    TCGPRN("@%x\n", &pAuthTblObj[j].uid);
    DBG_P(0x2, 0x03, 0x71001E, 4, &pAuthTblObj[j].uid);  // @%x
        TCGPRN("j|%02x, HstAuth|%08x%08x, Auth|%08x%08x\n", j, (U32)(mSessionManager.HtSgnAuthority.all >> 32), (U32)(mSessionManager.HtSgnAuthority.all),
            (U32)(pAuthTblObj[j].uid >> 32), (U32)(pAuthTblObj[j].uid));
        DBG_P(0x6, 0x03, 0x71001F, 1, j, 4, (U32)(mSessionManager.HtSgnAuthority.all >> 32), 4, (U32)(mSessionManager.HtSgnAuthority.all), 4,(U32)(pAuthTblObj[j].uid >> 32), 4, (U32)(pAuthTblObj[j].uid));  // j|%02x, HstAuth|%x%x, Auth|%x%x
        if (mSessionManager.HtSgnAuthority.all == pAuthTblObj[j].uid)
        {
            TCGPRN("j|%02x, HstAuthClass|%08x%08x, AuthClass|%08x%08x\n", j, (U32)(mSessionManager.HtAuthorityClass >> 32), (U32)(mSessionManager.HtAuthorityClass),
                (U32)(pAuthTblObj[j].Class >> 32), (U32)(pAuthTblObj[j].Class));
            DBG_P(0x6, 0x03, 0x710020, 1, j, 4, (U32)(mSessionManager.HtAuthorityClass >> 32), 4, (U32)(mSessionManager.HtAuthorityClass), 4,(U32)(pAuthTblObj[j].Class >> 32), 4, (U32)(pAuthTblObj[j].Class));  // j|%02x, HstAuthClass|%x%x, AuthClass|%x%x
            mSessionManager.HtAuthorityClass = pAuthTblObj[j].Class;
            if (pAuthTblObj[j].isClass == TRUE)
                return STS_INVALID_PARAMETER;   //UID is a class

            if (pAuthTblObj[j].enabled == FALSE) //UID disabled!
                return STS_NOT_AUTHORIZED;      //core v2.0 r2.0 5.3.4.1.4
                //STS_INVALID_PARAMETER;        //test case v1.0

            if (pAuthTblObj[j].operation == AUTH_Password)
            {
                //if(mSessionManager.HtChallenge[0]==0) //lengh=0: no password
                //    return STS_INVALID_PARAMETER;

                tgtCPinUid = pAuthTblObj[j].credential;

                for (i = 0; i<cpinRowCnt; i++)
                {
                    if (tgtCPinUid == pCPinTblObj[i].uid)
                    {
                        if (pCPinTblObj[i].tryLimit != 0)
                        { // check tries count
                            if (pCPinTblObj[i].tries >= pCPinTblObj[i].tryLimit)
                            {
                                //TCG_PRINTF("tryLimit %2X %2X %2X\n", i, pCPinTblObj[i].tryLimit, pCPinTblObj[i].tries);
                                // DBG_P(4, 3, 0x820179, 2, i, 1, pCPinTblObj[i].tryLimit, 1, pCPinTblObj[i].tries); // 82 01 79, "i[%04X] tryLimit[%08X] tries[%08X]", 2 4 4
                                return STS_AUTHORITY_LOCKED_OUT;
                            }
                        }

                        //password checking...
                        // DBG_P(2, 3,0x82017E, 4,*(U32*)mSessionManager.HtChallenge);  //82 01 7E, "p: %08X", 4

                        // *WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                        if ((mSessionManager.HtChallenge[0] == 0) && (pCPinTblObj[i].cPin.cPin_Tag == CPIN_NULL))
                        {
                            pCPinTblObj[i].tries = 0;
                            return STS_SUCCESS;
                        }

                        if (mSessionManager.HtChallenge[0] != 0) //password length
                        {
                            if (pCPinTblObj[i].cPin.cPin_Tag == CPIN_IN_PBKDF)
                            {
                                // &WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                                HAL_PBKDF2((U32 *)(&mSessionManager.HtChallenge[1]), (U32)mSessionManager.HtChallenge[0],
                                           (U32 *)pCPinTblObj[i].cPin.cPin_salt, sizeof(pCPinTblObj[i].cPin.cPin_salt), (U32 *)digest);

                                TCGPRN("i|%x, cpin tbl|%x , cpin hst|%x\n", i, *((U32*)&pCPinTblObj[i].cPin.cPin_val[0]), *((U32*)&digest[0]));
                                DBG_P(0x4, 0x03, 0x710021, 4, i, 4, *((U32*)&pCPinTblObj[i].cPin.cPin_val[0]), 4, *((U32*)&digest[0]));  // i|%x, cpin tbl|%x , cpin hst|%x

                                // *WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison
                                bCpinChkFail = FALSE;
                                for(U32 k=0;k<SHA256_DIGEST_SIZE;k++)
                                {
                                    if (pCPinTblObj[i].cPin.cPin_val[k] != digest[k]) //password compare NG
                                    {
                                        bCpinChkFail = TRUE;
                                        //break;
                                    }
                                }
                                if (bCpinChkFail)
                                {
                                    #if 0
                                    TCG_ERR_PRN("NG@: ");
                                    DBG_P(0x01, 0x03, 0x7F7F03 );  // NG@:
                                    for(j=1;j<=4;j++)
                                        D_PRINTF("%2X", pCPinTblObj[i].cPin.cPin_val[j]);
                                    D_PRINTF("...\n");

                                    //TCG_PRINTF("digest@: ");
                                    //for(j=0;j<32;j++) TCG_PRINTF("%2X",digest[j]);  TCG_PRINTF("...\n");
                                    #endif
                                    goto CPin_NG;
                                }
                                else
                                {
                                    TCGPRN("PASS\n");
                                    DBG_P(0x01, 0x03, 0x710023 );  // PASS
                                }
                                // &WCS TFS Item 168903 [Security] Timing Attacks on Hash Comparison

                                pCPinTblObj[i].tries = 0;
                                return STS_SUCCESS;
                            }
                            else if (pCPinTblObj[i].cPin.cPin_Tag == CPIN_IN_RAW)
                            {
                                TCGPRN("cpin_in_raw\n");
                                DBG_P(0x01, 0x03, 0x71011F );  // cpin_in_raw
                                TCGPRN("i|%x, cpin tbl|%x , cpin hst|%x\n", i, *((U32*)&pCPinTblObj[i].cPin.cPin_val[0]), *((U32*)&mSessionManager.HtChallenge[1]));
                                DBG_P(0x4, 0x03, 0x710021, 4, i, 4, *((U32*)&pCPinTblObj[i].cPin.cPin_val[0]), 4, *((U32*)&mSessionManager.HtChallenge[1]));  // i|%x, cpin tbl|%x , cpin hst|%x

                                bCpinChkFail = FALSE;
                                for(U32 k=0;k<CPIN_LENGTH;k++)
                                {
                                    if (pCPinTblObj[i].cPin.cPin_val[k] != mSessionManager.HtChallenge[k + 1]) //password compare NG
                                    {
                                        bCpinChkFail = TRUE;
                                        break;
                                    }
                                }
                                if (bCpinChkFail)
                                {
                                    goto CPin_NG;
                                }
                                else
                                {
                                    TCGPRN("PASS\n");
                                    DBG_P(0x01, 0x03, 0x710023 );  // PASS
                                }
                                pCPinTblObj[i].tries = 0;
                                return STS_SUCCESS;
                            }
                            else
                            {
                                 goto CPin_NG;
                            }
                        }
                        // *WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                        else
                        // &WCS TFS Item 168904 [Security] Current implementation disclosure the info about passwords length
                        {
                            return STS_INVALID_PARAMETER;
                        }
CPin_NG:
                        if (pCPinTblObj[i].tries < pCPinTblObj[i].tryLimit)
                            pCPinTblObj[i].tries++;

                        TCGPRN("i|%x, tryLimit|%x, tries|%x\n", i, pCPinTblObj[i].tryLimit, pCPinTblObj[i].tries);
                        DBG_P(0x4, 0x03, 0x710024, 4, i, 4, pCPinTblObj[i].tryLimit, 4, pCPinTblObj[i].tries);  // i|%x, tryLimit|%x, tries|%x
                        return STS_NOT_AUTHORIZED;
                    }
                }
            }
            else if (mSessionManager.HtSgnAuthority.all == UID_Authority_Anybody)
            { //no password (ex. Anybody)
                if (mSessionManager.HtChallenge[0] <= 32)   //password length<=32
                    return STS_SUCCESS;
            }

            return STS_INVALID_PARAMETER;
        }
    }

    return STS_INVALID_PARAMETER;  //HostSigningAuthority UID nonexistent
}

/***********************************************************
* TCG Table Method decodes and processes
* return 0 if success
***********************************************************/
tcg_code U16 f_method_next(req_t *req)
{
    return Method_Next(req);
}

tcg_code U16 f_method_getAcl(req_t *req)
{
    return Method_GetACL(req);
}

tcg_code U16 f_method_genKey(req_t *req)
{
    #if _TCG_ != TCG_PYRITE
    return Method_GenKey(req);
    #else
    return STS_SUCCESS;  // STS_INVALID_METHOD;  // alexcheck return STS_SUCCESS or STS_INVALID_METHOD
    #endif
}

tcg_code U16 f_method_revertSp(req_t *req)
{
    return Method_RevertSP(req);
}

tcg_code U16 f_method_get(req_t *req)
{
    return Method_Get(req);
}

/*****************************************************
 * Method set
 *****************************************************/
//-------------------Method Set Ok-------------------------
tcg_code bool cb_set_ ok(Complete)(req_t *req)
{
    TCGPRN("cb_set_ok_Complete()\n");
    DBG_P(0x01, 0x03, 0x710025 );  // cb_set_ok_Complete()

    #if CO_SUPPORT_AES
    TcgUpdateRawKeyList(mRawKeyUpdateList);
    #endif

    ClearMtableChangedFlag();
    //cj tested [
    if (U64_TO_U32_H(invokingUID.all) == U64_TO_U32_H(UID_Locking))
    {
        LockingRangeTable_Update();
    }

    if (U64_TO_U32_H(invokingUID.all) == U64_TO_U32_H(UID_MBRControl))
    {
        if ((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE)
            && (pG3->b.mLckMbrCtrl_Tbl.val[0].done == FALSE))
            mTcgStatus |= MBR_SHADOW_MODE;
        else
            mTcgStatus &= (~MBR_SHADOW_MODE);
    }

    if(req->completion != nvmet_core_cmd_done){
        method_complete_post(req, TRUE);
    }
    return TRUE;
}

#if 0
tcg_code bool cb_set_ok(DSCommit)(req_t *req)
{
    TCGPRN("cb_set_ok_DSCommit()\n");
    DBG_P(0x01, 0x03, 0x710026 );  // cb_set_ok_DSCommit()
    if(flgs_MChnged.b.DS)
        tcg_ipc_post(req, MSG_TCG_DSCOMMIT, cb_set_ok(Complete));
    else
        cb_set_ok(Complete)(req);
    return TRUE;
}

tcg_code bool cb_set_ok(SMBRCommit)(req_t *req)
{
    TCGPRN("cb_set_ok_SMBRCommit()\n");
    DBG_P(0x01, 0x03, 0x710027 );  // cb_set_ok_SMBRCommit()
    if(flgs_MChnged.b.SMBR)
        tcg_ipc_post(req, MSG_TCG_SMBRCOMMIT, cb_set_ok(DSCommit));
    else
        cb_set_ok(DSCommit)(req);
    return TRUE;
}

tcg_code bool cb_set_ok(G3Wr)(req_t *req)
{
    TCGPRN("cb_set_ok_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x710028 );  // cb_set_ok_G3Wr()
    if(flgs_MChnged.b.G3)
        tcg_ipc_post(req, MSG_TCG_G3WR, cb_set_ok(SMBRCommit));
    else
        cb_set_ok(SMBRCommit)(req);
    return TRUE;
}

tcg_code bool cb_set_ok(G2Wr)(req_t *req)
{
    TCGPRN("cb_set_ok_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x710029 );  // cb_set_ok_G2Wr()
    if(flgs_MChnged.b.G2)
        tcg_ipc_post(req, MSG_TCG_G2WR, cb_set_ok(G3Wr));
    else
        cb_set_ok(G3Wr)(req);
    return TRUE;
}

tcg_code bool cb_set_ok(G1Wr)(req_t *req)
{
    TCGPRN("cb_set_ok_G1Wr()\n");
    DBG_P(0x01, 0x03, 0x71002A );  // cb_set_ok_G1Wr()
    if(flgs_MChnged.b.G1)
        tcg_ipc_post(req, MSG_TCG_G1WR, cb_set_ok(G2Wr));
    else
        cb_set_ok(G2Wr)(req);
    return TRUE;
}

tcg_code bool cb_set_ok(Begin)(req_t *req)
{
    TCGPRN("cb_set_ok_Begin()\n");
    DBG_P(0x01, 0x03, 0x71002B );  // cb_set_ok_Begin()
    if (mSessionManager.Write != 0x00){   // check write bit in start session payload
        cb_set_ok(G1Wr)(req);
    }
    return TRUE;
}
#else
tcg_code bool cb_set_ok(tblUpdate)(req_t *req)
{
    TCGPRN("cb_set_ok_tblUpdate()\n");
    DBG_P(0x01, 0x03, 0x710127 );  // cb_set_ok_tblUpdate()

    tcg_ipc_post_xx(req, MSG_TCG_TBL_UPDATE, cb_set_ok(Complete), flgs_MChnged.all32);
    return TRUE;
}

tcg_code bool cb_set_ok(Begin)(req_t *req)
{
    TCGPRN("cb_set_ok_Begin()\n");
    DBG_P(0x01, 0x03, 0x71002B );  // cb_set_ok_Begin()
    if (mSessionManager.Write != 0x00){   // check write bit in start session payload
        cb_set_ok(tblUpdate)(req);
    }
    return TRUE;
}
#endif

//-------------------Method Set Ng-------------------------
tcg_code bool cb_set_ng(Complete)(req_t *req)
{
    TCGPRN("cb_set_ng_Complete()\n");
    DBG_P(0x01, 0x03, 0x71002C );  // cb_set_ng_Complete()

    ClearMtableChangedFlag();
    if(req->completion != nvmet_core_cmd_done){
        method_complete_post(req, TRUE);
    }
    return TRUE;
}

#if 0
tcg_code bool cb_set_ng(TDSClear)(req_t *req)
{
    TCGPRN("cb_set_ng_TDSClear()\n");
    DBG_P(0x01, 0x03, 0x71002D );  // cb_set_ng_TDSClear()
    if(flgs_MChnged.b.DS)
        tcg_ipc_post(req, MSG_TCG_TDSCLEAR, cb_set_ng(Complete));
    else
        cb_set_ng(Complete)(req);
    return TRUE;
}

tcg_code bool cb_set_ng(TSMBRClear)(req_t *req)
{
    TCGPRN("cb_set_ng_TSMBRClear()\n");
    DBG_P(0x01, 0x03, 0x71002E );  // cb_set_ng_TSMBRClear()
    if(flgs_MChnged.b.SMBR)
        tcg_ipc_post(req, MSG_TCG_TSMBRCLEAR, cb_set_ng(TDSClear));
    else
        cb_set_ng(TDSClear)(req);
    return TRUE;
}

tcg_code bool cb_set_ng(G3Rd)(req_t *req)
{
    TCGPRN("cb_set_ng_G3Rd()\n");
    DBG_P(0x01, 0x03, 0x71002F );  // cb_set_ng_G3Rd()
    if(flgs_MChnged.b.G3)
        tcg_ipc_post(req, MSG_TCG_G3RD, cb_set_ng(TSMBRClear));
    else
        cb_set_ng(TSMBRClear)(req);
    return TRUE;
}

tcg_code bool cb_set_ng(G2Rd)(req_t *req)
{
    TCGPRN("cb_set_ng_G2Rd()\n");
    DBG_P(0x01, 0x03, 0x710030 );  // cb_set_ng_G2Rd()
    if(flgs_MChnged.b.G2)
        tcg_ipc_post(req, MSG_TCG_G2RD, cb_set_ng(G3Rd));
    else
        cb_set_ng(G3Rd)(req);
    return TRUE;
}

tcg_code bool cb_set_ng(G1Rd)(req_t *req)
{
    TCGPRN("cb_set_ng_G1Rd()\n");
    DBG_P(0x01, 0x03, 0x710031 );  // cb_set_ng_G1Rd()
    if(flgs_MChnged.b.G1)
        tcg_ipc_post(req, MSG_TCG_G1RD, cb_set_ng(G2Rd));
    else
        cb_set_ng(G2Rd)(req);
    return TRUE;
}

tcg_code bool cb_set_ng(Begin)(req_t *req)
{
    TCGPRN("cb_set_ng_Begin()\n");
    DBG_P(0x01, 0x03, 0x710032 );  // cb_set_ng_Begin()
    cb_set_ng(G1Rd)(req);
    return TRUE;
}
#else
tcg_code bool cb_set_ng(tblRecovery)(req_t *req)
{
    TCGPRN("cb_set_ng_tblRecovery)\n");
    DBG_P(0x01, 0x03, 0x710128 );  // cb_set_ng_tblRecovery)

    tcg_ipc_post_xx(req, MSG_TCG_TBL_RECOVERY, cb_set_ng(Complete), flgs_MChnged.all32);
    return TRUE;
}

tcg_code bool cb_set_ng(Begin)(req_t *req)
{
    TCGPRN("cb_set_ng_Begin()\n");
    DBG_P(0x01, 0x03, 0x710032 );  // cb_set_ng_Begin()
    cb_set_ng(tblRecovery)(req);
    return TRUE;
}
#endif

//-------------------Method Set-------------------------
tcg_code U16 f_method_set(req_t *req)
{
    U16 result = STS_SUCCESS;

    if (mSessionManager.Write == 0x00)   // check write bit in start session payload
    {
        return STS_INVALID_METHOD;
    }

    method_result = result = Method_Set(req);
    if (result == STS_SUCCESS)
    {
        if (mSessionManager.TransactionState == TRNSCTN_IDLE)
        {
        #if 1
            if(flgs_MChnged.b.SMBR_Cb_Acting == FALSE && flgs_MChnged.b.DS_Cb_Acting == FALSE)
                cb_set_ok(Begin)(req);
        #else
            WriteMtable2NAND(req);

            #if CO_SUPPORT_AES
            TcgUpdateRawKeyList(mRawKeyUpdateList);
            #endif

            ClearMtableChangedFlag();
            //cj tested [
            if (U64_TO_U32_H(invokingUID.all) == U64_TO_U32_H(UID_Locking))
            {
                LockingRangeTable_Update();
            }

            if (U64_TO_U32_H(invokingUID.all) == U64_TO_U32_H(UID_MBRControl))
            {
                if ((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE)
                    && (pG3->b.mLckMbrCtrl_Tbl.val[0].done == FALSE))
                    mTcgStatus |= MBR_SHADOW_MODE;
                else
                    mTcgStatus &= (~MBR_SHADOW_MODE);
            }
        #endif
        }
    }
    else
    {
    #if 1
        if(flgs_MChnged.b.SMBR_Cb_Acting == FALSE && flgs_MChnged.b.DS_Cb_Acting == FALSE){
            cb_set_ng(Begin)(req);
        }
    #else
        ReadNAND2Mtable(req);
        ClearMtableChangedFlag();
    #endif
    }
    return result;
}

tcg_code U16 f_method_authenticate(req_t *req)
{
    return Method_Authenticate(req);
}

tcg_code U16 f_method_revert(req_t *req)
{
    return Method_Revert(req);
}

tcg_code U16 f_method_activate(req_t *req)
{
    return Method_Activate(req);
}

tcg_code U16 f_method_random(req_t *req)
{
    return Method_Random(req);
}

tcg_code U16 f_method_reactivate(req_t *req)
{
    #if _TCG_!=TCG_PYRITE
    return Method_Reactivate(req);
    #else
    return STS_SUCCESS;  // STS_INVALID_METHOD;  // alexcheck return STS_SUCCESS or STS_INVALID_METHOD
    #endif
}

tcg_code U16 f_method_erase(req_t *req)
{
    #if _TCG_!=TCG_PYRITE
    return Method_Erase(req);
    #else
    return STS_SUCCESS;  // STS_INVALID_METHOD;  // alexcheck return STS_SUCCESS or STS_INVALID_METHOD
    #endif
}

tcg_code U16 f_method_illegal(req_t *req)
{
    return STS_INVALID_METHOD;
}



tcg_code U32 locking_for_methodUid_index(U64 mtdUid)
{
    U32 i;

    for(i = 0; i < (sizeof(method_uid_lookup)/sizeof(U64)); i++){
        if(mtdUid == method_uid_lookup[i]) return i;
    }
    return (U32)cMcMtd_illegal;
}


/***********************************************************
* invoking_tcg_table()  <-- InvokingTcgTable()
***********************************************************/
tcg_code U16 invoking_tcg_table(req_t *req)
{
    U16 result = STS_SUCCESS;
    int status;
    bool invIdIsFound = FALSE;

    if (methodUID.all != UID_MethodID_GetACL)
    {
        //1. AccessControl checking, get ACL here
        status = tcg_access_control_check(&invIdIsFound);
        if (status == zNG)
        {
            #if 0
            return STS_NOT_AUTHORIZED;   //0x01  // DM Script v7.0, InvID or MtdID not found!
            #else
            if ((methodUID.all != UID_MethodID_Get) || (invIdIsFound == FALSE))     //test cases 3.1.5
                return STS_NOT_AUTHORIZED;   //0x01
            else
            { // MethodID_Get && invIdIsFound
                fill_no_data_token_list();
                return STS_NOT_AUTHORIZED;  //STS_SUCCESS;   // DM Tcg TestScript v7.5 A6-1-1-3-1(2)
            }
            #endif
        }

        //2. ACE BooleanExpr checking
        status = mSessionManager.SPID.all == UID_SP_Admin ? admin_aceBooleanExpr_chk(TRUE) : locking_aceBooleanExpr_chk(TRUE);

        TCG_PRINTF("AceBoolExpChk|%2X\n", status);
        // DBG_P(2, 3, 0x82018A, 1, (U8)status);  //82 01 8A, "Locking ACE BooleanExp Check: %02X", 1

        if (status == zNG)
        {
            #if 0   //BID8392 for DM new script v6.5
            return STS_NOT_AUTHORIZED;   //0x01
            #else
            if (methodUID.all != UID_MethodID_Get)     //test cases 3.1.5
                return STS_NOT_AUTHORIZED;   //0x01
            else
            {
                fill_no_data_token_list();
                return STS_SUCCESS;
            }
            #endif
        }
    }

    //3. Authority checking
    //result=AdminAuthorityCheck();
    //if(result == zNG) return zNG;

    //4. Method Operation (decode the parameters, then do the required action!)
    result = gCbfProc_method_map[ locking_for_methodUid_index(methodUID.all) ](req);
    //printk("invoking_tcg_table() result=%08x\n", result);
    DBG_P(0x2, 0x03, 0x71010F, 4, result);  // invoking_tcg_table() result=%08x
    return result;
}

/***********************************************************
* tcg_access_control_check()  <-- TcgAccessControlCheck()
***********************************************************/
tcg_code int tcg_access_control_check(bool* invIdIsFound)
{
    //sAxsCtrl_TblObj *pAxsCtrlTbl = NULL;
    int result = zNG;
    U16 rowCnt = 0, byteCnt, i;
    U8 j;

    TCGPRN("inv|%08x%08x mtd|%08x%08x\n", invokingUID.dw[1], invokingUID.dw[0], methodUID.dw[1], methodUID.dw[0]);
    DBG_P(0x5, 0x03, 0x710033, 4, invokingUID.dw[1], 4, invokingUID.dw[0], 4, methodUID.dw[1], 4, methodUID.dw[0]);  // inv|%x%x mtd|%x%x
    // DBG_P(5, 3, 0x820218, 4, (U32)(invokingUID.all >> 32), 4, (U32)(invokingUID.all), 4, (U32)(methodUID.all >> 32), 4, (U32)(methodUID.all)); // 82 02 18, "inv[%08X_%08X] mtd[%08X_%08X]\n" 4 4 4 4

    memset(aclBackup, 0, sizeof(aclBackup));
    if (FetchAxsCtrlTbl(mSessionManager.SPID.all, &byteCnt, &rowCnt) != zOK)
        goto CHK_END;   // return zNG;

    //pAxsCtrlTbl = (sAxsCtrl_TblObj*)tcgTblBuf;

    for (i = 0; i<rowCnt; i++)
    { // search for InvokingID
        if (pAxsCtrlTbl[i].invID == invokingUID.all)
        {
            *invIdIsFound = TRUE;
            if (pAxsCtrlTbl[i].mtdID == methodUID.all)
            {    //InvokingID/MethodID ok, store ACL list and getAclAcl uid
                for (j = 0; j < ACCESSCTRL_ACL_CNT; j++)
                    aclBackup[j].aclUid = pAxsCtrlTbl[i].acl[j];

                getAclAclUid = pAxsCtrlTbl[i].getAclAcl;

                result = zOK;   // return zOK;
                goto CHK_END;
            }
        }
        else if ((pAxsCtrlTbl[i].invID >> 32) >(invokingUID.all >> 32))
        {
            //if(invIdIsFound) //out of InvokingUID search, NG
            break;
        }
    }

CHK_END:
    TCGPRN("AccessCtrlChk|%x\n", result);
    DBG_P(0x2, 0x03, 0x710034, 4, result);  // AccessCtrlChk|%x
    // DBG_P(2, 3, 0x820219, 4, result);  // 82 02 19, "AccessCtrlChk|%2X" 4
    return result;
}

/***********************************************************
* admin_aceBooleanExpr_chk()  <-- AdminAceBooleanExprChk()
***********************************************************/
//
// check BooleanExpr column in ACE table with the HostSigningAuthority
// Return zOK if the authority is passed, else return zNG.
//
//
// 1. look up ACE table object from aclUid
// 2. get the booleanExpr of that ACE table object for authority check
// 3. return OK if requested authority is "Anybody", or requested authority is the same as host signed in,
//
tcg_code int admin_aceBooleanExpr_chk(bool bNotGetACL)
{
    U64 tmp64;
    U16 i, j, iAcl, iAclBk = 0;
    int sts = zNG;

    if (bNotGetACL)
    {
        for (iAcl = 0; iAcl < ACCESSCTRL_ACL_CNT; iAcl++)
        {
            if (aclBackup[iAcl].aclUid == UID_Null)
                break;

            for (i = 0; i < pG1->b.mAdmACE_Tbl.hdr.rowCnt; i++)
            {
                if (pG1->b.mAdmACE_Tbl.val[i].uid == aclBackup[iAcl].aclUid)
                {
                    aclBackup[iAcl].aclUid = UID_Null;   // reset, back it up only if it passes the "BooleanExpr" check ...

                    //check BooleanExpr
                    for (j = 0; j < ADM_ACE_BOOLEXPR_CNT; j++)
                    {
                        tmp64 = pG1->b.mAdmACE_Tbl.val[i].booleanExpr[j];
                        if(tmp64==UID_Null)
                        { //no more BooleansExpr list
                            break;   //for j-loop
                        }
                        else if ((tmp64 == UID_Authority_Anybody)
                              || (tmp64 == mSessionManager.HtSgnAuthority.all)
                              || (tmp64 == mSessionManager.HtAuthorityClass))
                        {
                            // backup "uid" and "col" for Method_Get/Method_Set
                            aclBackup[iAclBk].aclUid = pG1->b.mAdmACE_Tbl.val[i].uid;
                            for (j = 0; j < ACE_COLUMNS_CNT; j++)
                               aclBackup[iAclBk].aceColumns[j] = pG1->b.mAdmACE_Tbl.val[i].col[j];
                            iAclBk++;

                            sts = zOK;
                            break;    //for j-loop
                        }
                    }
                    break;  //for i-loop
                }
            }
        }
    }
    else
    { // MethodGetACL: GetACLACL
        for (i = 0; i < pG1->b.mAdmACE_Tbl.hdr.rowCnt; i++)
        {
            if (pG1->b.mAdmACE_Tbl.val[i].uid == getAclAclUid)
            {
                //check BooleanExpr
                for (j = 0; j < ADM_ACE_BOOLEXPR_CNT; j++)
                {
                    tmp64 = pG1->b.mAdmACE_Tbl.val[i].booleanExpr[j];
                    if(tmp64 == UID_Null)
                    { //no more BooleanExpr list
                        break;      //for j-loop
                    }
                    else if ((tmp64 == UID_Authority_Anybody)
                          || (tmp64 == mSessionManager.HtSgnAuthority.all)
                          || (tmp64 == mSessionManager.HtAuthorityClass))
                    {
                         sts = zOK; //return zOK;     //at least one authority from ACL list is PASS!
                         goto CHK_END;
                    }
                }
                break;  //for i-loop
            }
        }
    }
CHK_END:
    return  sts;
}

/***********************************************************
* locking_aceBooleanExpr_chk()  <-- LockingAceBooleanExprChk()
***********************************************************/
// 1. look up ACE table object from aclUid
// 2. get the booleanExpr of that ACE table object for authority check
// 3. return OK if requested authority is "Anybody", or requested authority is the same as host signed in,
//    or requrested authority by the same class (Admins or Users).
tcg_code int locking_aceBooleanExpr_chk(bool bNotGetACL)
{
    U64 tmp64;
    U16 i, j, iAcl, iAclBk = 0;
    int sts = zNG;

    if (bNotGetACL)
    {
        for (iAcl = 0; iAcl < ACCESSCTRL_ACL_CNT; iAcl++)
        {
            if (aclBackup[iAcl].aclUid == UID_Null)
                break;

            for(i = 0; i < pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
            {
                if(pG3->b.mLckACE_Tbl.val[i].uid == aclBackup[iAcl].aclUid)
                {
                    aclBackup[iAcl].aclUid = UID_Null;     // reset, back it up only if it passes the "BooleanExpr" check ...

                    for (j = 0; j < LCK_ACE_BOOLEXPR_CNT; j++)
                    {
                        tmp64 = pG3->b.mLckACE_Tbl.val[i].booleanExpr[j];
                        if(tmp64 == 0) //no more BooleanExpr list
                            break;          //for j-loop

                        if((tmp64 == UID_Authority_Anybody)||
                           (tmp64 == mSessionManager.HtSgnAuthority.all)||
                           (tmp64 == mSessionManager.HtAuthorityClass))
                        {
                           // back up "uid" and "col" for Method_Get/Method_Set
                           aclBackup[iAclBk].aclUid = pG3->b.mLckACE_Tbl.val[i].uid;
                           for (j = 0; j < ACE_COLUMNS_CNT; j++)
                               aclBackup[iAclBk].aceColumns[j] = pG3->b.mLckACE_Tbl.val[i].col[j];
                           iAclBk++;

                           sts = zOK;
                           break;    //for j-loop
                        }
                    }
                    break;  //for i-loop
                }
            }
        }
    }
    else
    { //MethodGetACL: GetACLACL
        for(i = 0; i < pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
        {
            if(pG3->b.mLckACE_Tbl.val[i].uid == getAclAclUid)
            {
                //check BooleanExpr
                for (j = 0; j < LCK_ACE_BOOLEXPR_CNT; j++)
                {
                    tmp64 = pG3->b.mLckACE_Tbl.val[i].booleanExpr[j];
                    if(tmp64 == 0) //no more BooleanExpr list
                        break;          //for j-loop
                    else if((tmp64 == UID_Authority_Anybody) ||
                            (tmp64 == mSessionManager.HtSgnAuthority.all) ||
                            (tmp64 == mSessionManager.HtAuthorityClass))
                    {
                        sts = zOK; //return zOK;     //at least one authority from ACL list is PASS!
                        goto CHK_END;
                    }
                }
                break;  //for i-loop
            }
        }
    }
CHK_END:
    return  sts;
}

//
// update statusCode[] from payload and check if status = "F0 00 00 00 F1"
// return STS_SUCCESS if ok.
//
tcg_code U16 chk_method_status(void)
{
    if (ChkToken() != TOK_StartList) return STS_SESSION_ABORT;

    if (AtomDecoding_Uint(&statusCode[0], sizeof(statusCode[0])) != STS_SUCCESS) return STS_SESSION_ABORT;

    if (AtomDecoding_Uint(&statusCode[1], sizeof(statusCode[1])) != STS_SUCCESS) return STS_SESSION_ABORT;

    if (AtomDecoding_Uint(&statusCode[2], sizeof(statusCode[2])) != STS_SUCCESS) return STS_SESSION_ABORT;

    if(statusCode[0] != 0x00) return statusCode[0];//status NG, method call fail!

    if(ChkToken() != TOK_EndList) return STS_SESSION_ABORT;
    else return STS_SUCCESS;
}

//check if column index "iCol" is in the ACE "Columns" column or not (Method_Get / Method_Set)
tcg_code int aceColumns_chk(U32 iCol)
{
    U16 i, k;

    for(i=0; i< ACCESSCTRL_ACL_CNT; i++)
    {
        if (aclBackup[i].aclUid == UID_Null)    return zNG;

        if (aclBackup[i].aceColumns[0] == 0x00) return zOK;  // All

        for (k = 1; k <= aclBackup[i].aceColumns[0]; k++)
        {
            if (aclBackup[i].aceColumns[k] == iCol) return zOK;
        }
    }

    return zNG;
}

tcg_code U16 Method_Next(req_t *req)
{
    U64 where;
    U8 *ptTblObj = NULL;
    bool p1Found = FALSE, p2Found = FALSE;

    U16  rowCnt = 0, objSize = 0, result;
    U8   count = 0;
    U8   iRow, byte;

    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    where = 0x00;

    byte = ChkToken();
    if (byte == TOK_EndList)
        goto END_LIST;

    else if (byte == TOK_StartName)
    {//optional parameters: where / count
        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
            return STS_INVALID_PARAMETER;

        if (byte == 0x00)
        { // where
            if (AtomDecoding_Uid2((U8*)&where) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (ChkToken() != TOK_EndName)
                return STS_INVALID_PARAMETER;

            p1Found = TRUE;

            byte = ChkToken();
            if (byte == TOK_StartName) //next option parameter
            {
                if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;
            }
        }

        if (byte == 0x01)
        { // count
            if (AtomDecoding_Uint(&count, sizeof(count)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (ChkToken() != TOK_EndName)
                return STS_INVALID_PARAMETER;

            p2Found = TRUE;

            byte = ChkToken();
        }

        if (byte != TOK_EndList)
            return STS_SESSION_ABORT;
    }
    else //NG within parameters
        return STS_SESSION_ABORT;   //A6-1-5-2-1, STS_INVALID_PARAMETER;

END_LIST:
    if (ChkToken() != TOK_EndOfData)
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //method execution:
    TCG_PRINTF("Method->Next: %08x-%08x %02x\n", (U32)(where>>32),(U32)where, count);
    // DBG_P(4, 3, 0x82021B, 4, (U32)(where>>32), 4, (U32)where, 1, count); // 82 02 1B, "Method->Next: %08x-%08x %02x" 4 4 1

    //FetchTcgTbl(pInvokingTbl, invokingTblSize);

    ptTblObj = (U8 *)(pInvokingTbl + sizeof(sTcgTblHdr) + sizeof(sColPrty) * ((sTcgTblHdr *)pInvokingTbl)->colCnt);
    rowCnt = ((sTcgTblHdr *)pInvokingTbl)->rowCnt;
    objSize = ((sTcgTblHdr *)pInvokingTbl)->objSize;

    if(p1Found==FALSE)
    {
        where = *(U64*)ptTblObj;
        iRow = 0;
    }
    else
    { //p1Found==TRUE
        p1Found = FALSE;
        for (iRow = 0; iRow<rowCnt; iRow++)
        {
            if (*(U64 *)ptTblObj == where)
            { // table object is found!
                p1Found = TRUE;

                iRow++;
                ptTblObj += objSize;
                break;
            }
            ptTblObj += objSize;
        }

        if(p1Found == FALSE)
        {
            TCG_PRINTF("not found!!\n");
            // DBG_P(1, 3, 0x82021C); // 82 02 1C, "not found!!"
            return STS_INVALID_PARAMETER;
        }
    }

    if(p2Found==FALSE)
        count = (U8)rowCnt;

    U64 tmp64;
    U32 tmp32 = invokingUID.all >>32;

    //prepare payload for response
    dataBuf[iDataBuf++]=TOK_StartList;
    dataBuf[iDataBuf++]=TOK_StartList;

    for(; iRow<rowCnt; iRow++)
    {
        tmp64 = *(U64*)ptTblObj;
        if(count)
        {
            if((U32)(tmp64>>32)==tmp32) // some rows might be disabled (ex, DataStoreN)
            {
                AtomEncoding_Int2Byte(ptTblObj, sizeof(U64));
                count--;
            }
        }
        else
            break;

        ptTblObj += objSize;
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;
}

// get SMBR method Callback ---------------------------------------------

tcg_code bool cb_getSmbr(Complete)(req_t *req)
{
    U32 i;
    mtdGetSmbr_varsMgm_t *p = &mtdGetSmbr_varsMgm;
    TCGPRN("cb_getSmbr_Complete()\n");
    DBG_P(0x01, 0x03, 0x710035 );  // cb_getSmbr_Complete()

    for (i = 0; i < p->smbrRdLen; i++) {
        dataBuf[iDataBuf++] = *((U8 *)tcgTmpBuf + p->laaOffsetBeginAdr + i);
    }
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_getSmbr(SmbrRdMulti)(req_t *req)
{
    TCGPRN("cb_getSmbr_SmbrRdMulti()\n");
    DBG_P(0x01, 0x03, 0x710036 );  // cb_getSmbr_SmbrRdMulti()
    mtdGetSmbr_varsMgm_t *p = &mtdGetSmbr_varsMgm;
    // SMBR_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)pTcgTmpBuf);   //read all
    tcg_ipc_post_ex(req, MSG_TCG_SMBRRD, cb_getSmbr(Complete), p->laaBeginAdr, p->laaBeginAdr + p->laaCnts, (U8 *)tcgTmpBuf);
    return TRUE;
}

tcg_code bool cb_getSmbr(SmbrRdSingle)(req_t *req)
{
    TCGPRN("cb_getSmbr_SmbrRdSingle()\n");
    DBG_P(0x01, 0x03, 0x710037 );  // cb_getSmbr_SmbrRdSingle()
    mtdGetSmbr_varsMgm_t *p = &mtdGetSmbr_varsMgm;

    while(p->idx < p->laaBeginAdr + p->laaCnts){
        if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + p->idx].blk < TCG_MBR_CELLS) {  //blank ?
            // SMBR_Read(ii, ii + 1, (U8 *)pTcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
            tcg_ipc_post_ex(req, MSG_TCG_SMBRRD, cb_getSmbr(SmbrRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
            p->idx++;
            return TRUE;
        }
        p->idx++;
    }

    cb_getSmbr(Complete)(req);
    return TRUE;
}

tcg_code bool cb_getSmbr(Begin)(req_t *req)
{
    mtdGetSmbr_varsMgm_t *p = &mtdGetSmbr_varsMgm;
    TCGPRN("cb_getSmbr_Begin() rowBeginAdr|%x, smbrRdLen|%x\n", p->rowBeginAdr, p->smbrRdLen);
    DBG_P(0x3, 0x03, 0x710038, 4, p->rowBeginAdr, 4, p->smbrRdLen);  // cb_getSmbr_Begin() rowBeginAdr|%x, smbrRdLen|%x

    p->laaBeginAdr       = p->rowBeginAdr / LAA_LEN;
    p->laaOffsetBeginAdr = p->rowBeginAdr % LAA_LEN;
    SMBR_ioCmdReq = FALSE;
    p->laaCnts           = (p->laaOffsetBeginAdr + p->smbrRdLen) / LAA_LEN;
    if((p->laaOffsetBeginAdr + p->smbrRdLen) % LAA_LEN) p->laaCnts += 1;
    memset((U8 *)tcgTmpBuf, 0x00,  mtdGetSmbr_varsMgm.laaCnts * LAA_LEN);

    p->hasBlank = FALSE;
    for(p->idx = p->laaBeginAdr; p->idx < p->laaBeginAdr + p->laaCnts; p->idx++){
        if ((pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + p->idx].blk >= TCG_MBR_CELLS) && \
           (pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START + p->idx].blk >= TCG_MBR_CELLS))
        {
            p->hasBlank = TRUE;
            break;
        }
    }

    if (p->hasBlank)
    {
        p->idx = p->laaBeginAdr;
        while (p->idx < p->laaBeginAdr + p->laaCnts){
            if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + p->idx].blk < TCG_MBR_CELLS) {  //blank ?
                // SMBR_Read(ii, ii + 1, (U8 *)pTcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                tcg_ipc_post_ex(req, MSG_TCG_SMBRRD, cb_getSmbr(SmbrRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
                p->idx++;
                return TRUE;
            }
            p->idx++;
        }
        memcpy(&dataBuf[iDataBuf], tcgTmpBuf,  p->smbrRdLen);
        iDataBuf += p->smbrRdLen;
        dataBuf[iDataBuf++] = TOK_EndList;
        dataBuf[iDataBuf++] = TOK_EndOfData;
    }
    else
    {
        // SMBR_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)pTcgTmpBuf);   //read all
        cb_getSmbr(SmbrRdMulti)(req);
        return TRUE;
    }

    return TRUE;
}

// set DS method Callback --------------------------------------------------

tcg_code bool cb_getDs(Complete)(req_t *req)
{
    U32 i;
    mtdGetDs_varsMgm_t *p = &mtdGetDs_varsMgm;
    TCGPRN("cb_getDsComplete()\n");
    DBG_P(0x01, 0x03, 0x710039 );  // cb_getDsComplete()

    for (i = 0; i < p->dsRdLen; i++) {
        dataBuf[iDataBuf++] = *((U8 *)tcgTmpBuf + p->laaOffsetBeginAdr + i);
    }
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    method_complete_post(req, TRUE);
    return TRUE;
}


tcg_code bool cb_getDs(DsRdMulti)(req_t *req)
{
    TCGPRN("cb_getDs_DsRdMulti()\n");
    DBG_P(0x01, 0x03, 0x71003A );  // cb_getDs_DsRdMulti()
    mtdGetDs_varsMgm_t *p = &mtdGetDs_varsMgm;
    // DS_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)pTcgTmpBuf);  // read all
    tcg_ipc_post_ex(req, MSG_TCG_DSRD, cb_getDs(Complete), p->laaBeginAdr, p->laaBeginAdr + p->laaCnts, (U8 *)tcgTmpBuf);
    return TRUE;
}


tcg_code bool cb_getDs(DsRdSingle)(req_t *req)
{
    TCGPRN("cb_getDs_DsRdSingle()\n");
    DBG_P(0x01, 0x03, 0x71003B );  // cb_getDs_DsRdSingle()
    mtdGetDs_varsMgm_t *p = &mtdGetDs_varsMgm;

    while(p->idx < p->laaBeginAdr + p->laaCnts){
        if (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + p->idx].blk < TCG_MBR_CELLS)    // blank ?
        {
            // DS_Read(ii, ii + 1, (U8 *)pTcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
            tcg_ipc_post_ex(req, MSG_TCG_DSRD, cb_getDs(DsRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
            p->idx++;
            return TRUE;
        }
        p->idx++;
    }

    cb_getDs(Complete)(req);
    return TRUE;
}

tcg_code bool cb_getDs(Begin)(req_t *req)
{
    U32 rowStart;
    mtdGetDs_varsMgm_t *p = &mtdGetDs_varsMgm;
    TCGPRN("cb_getDs_Begin() rowBeginAdr|%x, dsWrLen|%x\n", p->rowBeginAdr, p->dsRdLen);
    DBG_P(0x3, 0x03, 0x71003C, 4, p->rowBeginAdr, 4, p->dsRdLen);  // cb_getDs_Begin() rowBeginAdr|%x, dsWrLen|%x

    rowStart = p->rowBeginAdr + mDataStoreAddr[p->singleUserMode_startColumn].offset;


    p->laaBeginAdr       = rowStart / LAA_LEN;
    p->laaOffsetBeginAdr = rowStart % LAA_LEN;

    p->laaCnts           = (p->laaOffsetBeginAdr + p->dsRdLen) / LAA_LEN;
    if((p->laaOffsetBeginAdr + p->dsRdLen) % LAA_LEN) p->laaCnts += 1;
    TCGPRN("laaBeginAdr|%x, laaCnts|%x\n", p->laaBeginAdr, p->laaCnts);
    DBG_P(0x3, 0x03, 0x71003D, 4, p->laaBeginAdr, 4, p->laaCnts);  // laaBeginAdr|%x, laaCnts|%x
    memset((U8 *)tcgTmpBuf, 0x00,  mtdGetDs_varsMgm.laaCnts * LAA_LEN);

    p->hasBlank = FALSE;
    for(p->idx = p->laaBeginAdr; p->idx < p->laaBeginAdr + p->laaCnts; p->idx++){
        if ((pG4->b.TcgMbrL2P[TCG_DS_LAA_START + p->idx].blk >= TCG_MBR_CELLS) && \
           (pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START + p->idx].blk >= TCG_MBR_CELLS))
        {
            p->hasBlank = TRUE;
            break;
        }
    }

    if (p->hasBlank)
    {
        p->idx = p->laaBeginAdr;
        while (p->idx < p->laaBeginAdr + p->laaCnts){
            if (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + p->idx].blk < TCG_MBR_CELLS)    // blank ?
            {
                // DS_Read(ii, ii + 1, (U8 *)pTcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                tcg_ipc_post_ex(req, MSG_TCG_DSRD, cb_getDs(DsRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
                p->idx++;
                return TRUE;
            }
            p->idx++;
        }
        memcpy(&dataBuf[iDataBuf], tcgTmpBuf,  p->dsRdLen);
        iDataBuf += p->dsRdLen;
        dataBuf[iDataBuf++] = TOK_EndList;
        dataBuf[iDataBuf++] = TOK_EndOfData;
    }
    else
    {
        // DS_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)pTcgTmpBuf);  // read all
        cb_getDs(DsRdMulti)(req);
        return TRUE;
    }

    return TRUE;
}


tcg_code U16 Method_Get(req_t *req)
{
    U64 tmp64;
    U32 startCol = 0, endCol = 0xffffffff, maxCol = 0;
    U32 startRow = 0, endRow = 0xffffffff, i32 = 0, maxRow = 0;
    U32 colSize, tblKind;
    sColPrty *ptColPty = NULL;
    U8 *ptTblObj = NULL;

    U16 result;
    U16 rowCnt = 0, colCnt = 0, objSize = 0;
    U8  nameValue = 0, dataType, byte;  //boolExprCnt

    //retrieve the start/end column from the payload first...
    if (ChkToken() != TOK_StartList)
        return STS_SESSION_ABORT;
    if (ChkToken() != TOK_StartList)
        return STS_SESSION_ABORT;

    if((invokingUID.dw[1]&0xfffffff0)==(UID_DataStoreType>>32))
    {
        tblKind=TBL_K_BYTE;
        startCol = ((invokingUID.all>>32)&0xff) - 1;
        maxRow = mDataStoreAddr[startCol].length - 1;
    }
    else if(invokingUID.all==UID_MBR)
    {
        tblKind = TBL_K_BYTE;
        maxRow = MBR_LEN-1;
    }
    else
        tblKind = TBL_K_OBJECT;

    //Named Value: Table (0x00), startRow (0x01), endRow (0x02), startCol (0x03), endCol (0x04)

    byte = ChkToken();
    if (byte == TOK_StartName)
    { //retrieve start/end column parameters

        if (tblKind == TBL_K_OBJECT)
        { //Object Table, retrieve startCol or endCol only
            if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (nameValue == 0x03) //startCol
            {
                if (AtomDecoding_Uint((U8*)&startCol, sizeof(startCol)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                if (byte == TOK_EndList) //no more parameters
                    goto END_LIST;
                else if (byte != TOK_StartName)
                    return STS_SESSION_ABORT;   //A6-1-5-2-1    //STS_INVALID_PARAMETER;

                if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;
            }

            if (nameValue == 0x04) //endCol
            {
                if (AtomDecoding_Uint((U8*)&endCol, sizeof(endCol)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                goto END_LIST;
            }
            else
                return STS_INVALID_PARAMETER;
        }
        else
        { //retrieve startRow or endRow only
            if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if (nameValue == 0x01) //startRow
            {
                if (AtomDecoding_Uint((U8*)&startRow, sizeof(startRow)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                if (byte == TOK_EndList) //no more parameters
                    goto END_LIST;
                else if (byte != TOK_StartName)
                    return STS_SESSION_ABORT;   //A6-1-5-2-1, STS_INVALID_PARAMETER;

                if (AtomDecoding_Uint(&nameValue, sizeof(nameValue)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;
            }

            if (nameValue == 0x02) //endRow
            {
                if (AtomDecoding_Uint((U8*)&endRow, sizeof(endRow)) != STS_SUCCESS)
                    return STS_INVALID_PARAMETER;

                //TODO: check max row value ...

                if (ChkToken() != TOK_EndName)
                    return STS_INVALID_PARAMETER;

                byte = ChkToken();
                goto END_LIST;
            }
            else
                return STS_INVALID_PARAMETER;

            //if(endRow>rowSize) return zNG;
        }
    }

END_LIST:
    if ((byte != TOK_EndList) ||
        (ChkToken() != TOK_EndList) ||
        (ChkToken() != TOK_EndOfData))
        return STS_SESSION_ABORT;

    //FetchTcgTbl(pInvokingTbl, invokingTblSize);

    colCnt   = ((sTcgTblHdr *)pInvokingTbl)->colCnt;
    maxCol   = ((sTcgTblHdr *)pInvokingTbl)->maxCol;
    rowCnt   = ((sTcgTblHdr *)pInvokingTbl)->rowCnt;
    objSize  = ((sTcgTblHdr *)pInvokingTbl)->objSize;

    ptColPty = (sColPrty*)((U8*)pInvokingTbl + sizeof(sTcgTblHdr));
    ptTblObj = (U8*)ptColPty + sizeof(sColPrty) * colCnt;

    if (tblKind == TBL_K_OBJECT)
    {
        if (endCol == 0xffffffff) //no end parameters
            endCol = maxCol;

        if (endCol < startCol)
            return STS_INVALID_PARAMETER;

        if (endCol > maxCol)
            return STS_INVALID_PARAMETER;
    }
    else
    {
        if (endRow == 0xffffffff)
            endRow = maxRow;

        if (endRow < startRow)
            return STS_INVALID_PARAMETER;

        if (endRow>maxRow)
            return STS_INVALID_PARAMETER;

        //check if row size is over the buffer length,  F0 82 xx yy F1 F9 F0 00 00 00 F1, , iDataBuf=0x38
        if ((endRow - startRow + 1) > (U32)(TCG_BUF_LEN - iDataBuf - 11))
            return STS_RESPONSE_OVERFLOW;
    }

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //method execution
    //prepare payload for response
    dataBuf[iDataBuf++] = TOK_StartList;

    if (tblKind == TBL_K_OBJECT)
    {
        U32 iRow, iCol, iList;

        TCGPRN("M_Get sCol|%2X eCol|%2X\n", startCol, endCol);
        DBG_P(0x3, 0x03, 0x71003E, 1, startCol, 1, endCol);  // M_Get sCol|%2X eCol|%2X
        // DBG_P(3, 3, 0x82021D, 4, startCol, 4, endCol); // 82 02 1D, "M_Get sCol|%2X eCol|%2X" 4 4
        dataBuf[iDataBuf++] = TOK_StartList;

        for (iRow = 0; iRow<rowCnt; iRow++)
        {
            tmp64 = *((U64 *)ptTblObj);
            if (tmp64 == invokingUID.all)
            { //the table object is found!
                for (iCol = 0; iCol < colCnt;)
                {
                    if ((ptColPty->colNo) >= startCol &&
                        (ptColPty->colNo) <= endCol)
                    {
                        if (aceColumns_chk(ptColPty->colNo) == zOK) //check ACE columns first
                        {
                            dataBuf[iDataBuf++] = TOK_StartName;
                            AtomEncoding_Integer((U8*)&ptColPty->colNo, sizeof(ptColPty->colNo));

                            dataType = ptColPty->colType;
                            colSize = ptColPty->size;
                            if (dataType == UID_TYPE)
                            {
                                AtomEncoding_Int2Byte(ptTblObj, colSize);
                            }
                            else if (dataType == FBYTE_TYPE)
                            {
                                AtomEncoding_ByteSeq(ptTblObj, colSize);
                            }
                            else if (dataType == VALUE_TYPE)
                            {
                                AtomEncoding_Integer(ptTblObj, colSize);
                            }
                            else if (dataType == VBYTE_TYPE)
                            {
#if 1
                                sCPin *pCPIN;
                                //cjdbg, ASSERT(invokingUID.all == UID_CPIN_MSID);
                                pCPIN = (sCPin*)ptTblObj;
                                AtomEncoding_ByteHdr(CPIN_MSID_LEN); //Get MSID lengh
                                for (i32 = 0; i32 < CPIN_MSID_LEN; i32++)
                                {
                                    dataBuf[iDataBuf++] = pCPIN->cPin_val[i32];
                                }
#else
                                colSize = *ptTblObj;     //variable byte sequence, check table cell[0]
                                AtomEncoding_ByteHdr(colSize);
                                for (i32 = 1; i32 <= colSize; i32++)
                                    dataBuf[iDataBuf++] = *(ptTblObj + i32);
#endif
                            }
                            else if (dataType == LIST_TYPE)
                            {   //TODO: how to deal with "BooleanExpr?
                                colSize = *ptTblObj;     //List sequence, check table cell[0]
                                dataBuf[iDataBuf++] = TOK_StartList;
                                for (i32 = 1; i32 <= colSize; i32++)
                                    dataBuf[iDataBuf++] = ptTblObj[i32];
                                dataBuf[iDataBuf++] = TOK_EndList;
                            }
                            else if (dataType == STRING_TYPE)
                            {
                                for (i32 = 0; i32<colSize; i32++)
                                {
                                    if (*(ptTblObj + i32) == 0x00)
                                    {//string end '\0'
                                        colSize = i32;
                                        break;
                                    }
                                }

                                AtomEncoding_ByteSeq(ptTblObj, colSize);
                            }
                            else if (dataType == STRINGLIST_TYPE)
                            {
                                //TODO: need to deal with more than one list ...
                                dataBuf[iDataBuf++] = TOK_StartList;
                                for (i32 = 0; i32<colSize; i32++)
                                {
                                    if (*(ptTblObj + i32) == 0x00)
                                    {//string end '\0'
                                        colSize = i32;
                                        break;
                                    }
                                }

                                AtomEncoding_ByteSeq(ptTblObj, colSize);
                                dataBuf[iDataBuf++] = TOK_EndList;
                            }
                            else if (dataType == UIDLIST_TYPE)
                            { //mainly for "BooleanExpr"
                                colSize = sizeof(U64);
                                dataBuf[iDataBuf++] = TOK_StartList;

                                //place half-UID first (Authority_object_ref), get 1st UID
                                dataBuf[iDataBuf++] = TOK_StartName;

                                tmp64 = UID_CT_Authority_object_ref;
                                AtomEncoding_Int2Byte((U8*)&tmp64, 4);
                                AtomEncoding_Int2Byte(ptTblObj, colSize);
                                dataBuf[iDataBuf++] = TOK_EndName;

                                //get next UID
                                U32 boolExpCnt;
                                if (mSessionManager.SPID.all == UID_SP_Admin)
                                    boolExpCnt = ADM_ACE_BOOLEXPR_CNT;
                                else
                                    boolExpCnt = LCK_ACE_BOOLEXPR_CNT;
                                for (iList = 1; iList<boolExpCnt; iList++)
                                {
                                    //check if next UID exists or not
                                    tmp64 = *((U64 *)(ptTblObj + colSize * iList));
                                    if (tmp64)
                                    {
                                        dataBuf[iDataBuf++] = TOK_StartName;
                                        tmp64 = UID_CT_Authority_object_ref;
                                        AtomEncoding_Int2Byte((U8*)&tmp64, 4);
                                        AtomEncoding_Int2Byte(ptTblObj+ colSize *iList, colSize);
                                        dataBuf[iDataBuf++] = TOK_EndName;

                                        dataBuf[iDataBuf++] = TOK_StartName;
                                        tmp64 = UID_CT_boolean_ACE;
                                        AtomEncoding_Int2Byte((U8*)&tmp64, 4);
                                        dataBuf[iDataBuf++] = 0x01; //"OR"
                                        dataBuf[iDataBuf++] = TOK_EndName;
                                    }
                                    else
                                        break;
                                }
                                dataBuf[iDataBuf++] = TOK_EndList;
                            }
                            else if (dataType == UID2_TYPE)
                            { // UID list, for LockingInfo_SingleUserRange

                                colSize = 8;
                                tmp64 = *((U64 *)ptTblObj);
                                if (tmp64 == UID_Locking)
                                { //EntireLocking
                                    AtomEncoding_Int2Byte(ptTblObj, colSize);
                                }
                                else
                                { //UID list or Empty list, get next UID
                                    dataBuf[iDataBuf++] = TOK_StartList;

                                    for (iList = 0; iList <= LOCKING_RANGE_CNT; iList++)
                                    {
                                        //check if next UID exists or not
                                        tmp64 = *((U64 *)(ptTblObj + colSize * iList));
                                        if (tmp64)
                                        {
                                            AtomEncoding_Int2Byte((U8*)&tmp64, colSize);
                                        }
                                        else
                                            break;
                                    }
                                    dataBuf[iDataBuf++] = TOK_EndList;
                                }
                            }
                            dataBuf[iDataBuf++] = TOK_EndName;
                        }
                    }
                    else if ((ptColPty->colNo) > endCol)
                        break;

                    iCol++;
                    ptTblObj += ptColPty->size;    // next table cell (column)
                    ++ptColPty;                    // next colPty
                }

                break;
            }
            else
                ptTblObj += objSize;
        }

        dataBuf[iDataBuf++] = TOK_EndList;
    }

    else //if(tblKind==Byte)
    {
        TCGPRN("M_Get sRow|%x eRow|%x\n", startRow, endRow);
        DBG_P(0x3, 0x03, 0x71003F, 4, startRow, 4, endRow);  // M_Get sRow|%x eRow|%x
        // DBG_P(3, 3, 0x82021E, 4, startRow, 4, endRow); // 82 02 1E, "M_Get sRow|%02X eRow|%02X" 4 4
        //---- Data Store -------------------------------------------
        if (invokingUID.all != UID_MBR)
        {
            //encode Atom Token...
            AtomEncoding_ByteHdr(endRow - startRow + 1);    //dataBuf[iDataBuf]

            #if 1
            mtdGetDs_varsMgm.singleUserMode_startColumn = startCol;
            mtdGetDs_varsMgm.dsRdLen      = endRow - startRow + 1;
            if (mtdGetDs_varsMgm.dsRdLen > TCG_BUF_LEN) {
                return STS_INVALID_PARAMETER;
            }
            mtdGetDs_varsMgm.rowBeginAdr  = startRow;
            cb_getDs(Begin)(req);
            return STS_SUCCESS;

            #else
            {
                U32 rlen;
                U32 LaaAddr, LaaOffAddr, rdptr;
                U32 LaaCnt;
                U16 ii;
                U16 HasBlank;
                U8  *Desptr = (U8 *)tcgTmpBuf;

                rlen = endRow - startRow + 1;              //rlen = wr remain length
                if (rlen > TCG_BUF_LEN) {
                    return STS_INVALID_PARAMETER;
                }
                rdptr = startRow + mDataStoreAddr[startCol].offset;         //rd point
                LaaAddr = rdptr / LAA_LEN;
                LaaOffAddr = rdptr % LAA_LEN;

                LaaCnt = (LaaOffAddr + rlen) / LAA_LEN;
                if ((LaaOffAddr + rlen) % LAA_LEN) LaaCnt += 1;
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt*LAA_LEN);  //clr buffer
                                                                 //EP3 todo [
                HasBlank = FALSE;
                for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                    if (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + ii].blk >= TCG_MBR_CELLS) {
                        HasBlank = TRUE;
                        break;
                    }
                }
                if (HasBlank) {
                    for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                        if (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + ii].blk < TCG_MBR_CELLS) {  //blank ?
                            DS_Read(ii, ii + 1, (U8 *)tcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                        }
                    }
                }
                else {
                    DS_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)tcgTmpBuf);  // read all
                }
                // ]
                for (i32 = 0; i32<rlen; i32++) {
                    dataBuf[iDataBuf++] = Desptr[LaaOffAddr + i32];
                }
            }
            #endif
        }
        //----- SMBR -------------------------------------------
        else
        {
          #if 1
            mtdGetSmbr_varsMgm.smbrRdLen      = endRow - startRow + 1;
            if (mtdGetSmbr_varsMgm.smbrRdLen > TCG_BUF_LEN) {
                return STS_INVALID_PARAMETER;
            }
            mtdGetSmbr_varsMgm.rowBeginAdr  = startRow;

            if (mtdGetSmbr_varsMgm.smbrRdLen <= 15) //Short Atom, length=0~15
                dataBuf[iDataBuf++] = 0xA0 + (U8)mtdGetSmbr_varsMgm.smbrRdLen;
            else if (mtdGetSmbr_varsMgm.smbrRdLen <= 2047)
            { //Medium Atom
                dataBuf[iDataBuf++] = 0xD0 + DW_B1(mtdGetSmbr_varsMgm.smbrRdLen);
                dataBuf[iDataBuf++] = DW_B0(mtdGetSmbr_varsMgm.smbrRdLen);
            }
            else
            { //Long Atom
                dataBuf[iDataBuf++] = 0xE2;
                dataBuf[iDataBuf++] = DW_B2(mtdGetSmbr_varsMgm.smbrRdLen);
                dataBuf[iDataBuf++] = DW_B1(mtdGetSmbr_varsMgm.smbrRdLen);
                dataBuf[iDataBuf++] = DW_B0(mtdGetSmbr_varsMgm.smbrRdLen);
            }

            if(mtdGetSmbr_varsMgm.smbrRdLen > 0){
                cb_getSmbr(Begin)(req);
                return STS_SUCCESS;
            }
          #else
            U32 LaaAddr, LaaOffAddr, rdptr;
            U32      rlen;

            rlen = endRow - startRow + 1;              //rlen = wr remain length
            if (rlen > TCG_BUF_LEN) {
                return STS_INVALID_PARAMETER;
            }

            if (rlen <= 15) //Short Atom, length=0~15
                dataBuf[iDataBuf++] = 0xA0 + (U8)rlen;
            else if (rlen <= 2047)
            { //Medium Atom
                // dataBuf[iDataBuf++] = 0xD0 + HiByte(LoWord(rlen));
                dataBuf[iDataBuf++] = 0xD0 + DW_B1(rlen);
                // dataBuf[iDataBuf++] = LoByte(LoWord(rlen));
                dataBuf[iDataBuf++] = DW_B0(rlen);
            }
            else
            { //Long Atom
                dataBuf[iDataBuf++] = 0xE2;
                // dataBuf[iDataBuf++] = LoByte(HiWord(rlen));
                dataBuf[iDataBuf++] = DW_B2(rlen);
                // dataBuf[iDataBuf++] = HiByte(LoWord(rlen));
                dataBuf[iDataBuf++] = DW_B1(rlen);
                // dataBuf[iDataBuf++] = LoByte(LoWord(rlen));
                dataBuf[iDataBuf++] = DW_B0(rlen);
            }

            rdptr = startRow;         //rd point
            if (rlen>0) {
                U32 LaaCnt;
                U8 *Desptr = (U8 *)tcgTmpBuf;

                LaaAddr = rdptr / LAA_LEN;
                LaaOffAddr = rdptr % LAA_LEN;
                SMBR_ioCmdReq = FALSE;

                LaaCnt = (LaaOffAddr + rlen) / LAA_LEN;
                if ((LaaOffAddr + rlen) % LAA_LEN) LaaCnt += 1;
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt*LAA_LEN);  //clr buffer
#if 1
                {
                    U16 ii;
                    U16 HasBlank;

                    HasBlank = FALSE;

                    for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                        if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + ii].blk >= TCG_MBR_CELLS) {
                            HasBlank = TRUE;
                            break;
                        }
                    }
                    if (HasBlank) {
                        for (ii = LaaAddr; ii<LaaAddr + LaaCnt; ii++) {
                            if (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + ii].blk < TCG_MBR_CELLS) {  //blank ?
                                SMBR_Read(ii, ii + 1, (U8 *)tcgTmpBuf + (ii - LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                            }
                        }
                    }
                    else {
                        SMBR_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)tcgTmpBuf);   //read all
                    }
                }
#else
                SMBR_Read(LaaAddr, LaaAddr + LaaCnt, (U8 *)tcgTmpBuf, NULL);    //WaitMbrRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 6 page for max transfer buffer
#endif
                for (i32 = 0; i32<rlen; i32++) {
                    dataBuf[iDataBuf++] = Desptr[LaaOffAddr + i32];
                }
            }
          #endif
        }
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;
}

/***********************************************************
* Admin table set method.
* ref. core spec 5.3.3.7
***********************************************************/
tcg_code U16Method_Set(req_t *req)
{
    U64 tmp64;
    U32 SetParm_ColNo[eMaxSetParamCnt];
    U32 decLen;
    U32 tmp32;
    U8  setParmCnt = 0;
    U8  Where_or_Value;  //0->Where, 1->Value, 0xff->illegal
    U8  errCode = 0;  //no error

    U8 colType=0, colSize=0;
    U8 i, y;
    U8 byte;

    int zSTS = STS_SUCCESS;

    // DBG_P(1, 3, 0x820123);   //82 01 23, "[F]Method_Set"
    //printk("Method_Set()\n");
    DBG_P(0x01, 0x03, 0x710040 );  // Method_Set()
    memset(SetParm_ColNo, 0x00, sizeof(SetParm_ColNo));

    if (ChkToken() != TOK_StartList) {  //chk 0xF0
        zSTS = STS_SESSION_ABORT; errCode = 0x01; goto exit_Method_Set;
    }
    if (ChkToken() != TOK_StartName) {  //chk 0xF2
        zSTS = STS_SESSION_ABORT; errCode = 0x02; goto exit_Method_Set;
    }
    if ((zSTS = AtomDecoding_Uint(&Where_or_Value, sizeof(Where_or_Value))) != STS_SUCCESS)
    {
        errCode = 0x03; goto exit_Method_Set;
    }

    if ((invokingUID.all & 0xfffffff0ffffffff) == UID_DataStoreType || invokingUID.all == UID_MBR)
    { // Byte table
        tmp32 = 0;  // startRow
        if (Where_or_Value == 0x00)
        { //Where
            if ((zSTS = AtomDecoding_Uint((U8*)&tmp32, sizeof(tmp32))) != STS_SUCCESS) {
                errCode = 0x06; goto exit_Method_Set;
            }

            if (ChkToken() != TOK_EndName) {  //check 0xF3
                zSTS = STS_SESSION_ABORT; errCode = 0x08; goto exit_Method_Set;
            }

            if (ChkToken() != TOK_StartName) {  //check F2 for "value"
                zSTS = STS_SESSION_ABORT; errCode = 0x0A; goto exit_Method_Set;
            }
            if ((zSTS = AtomDecoding_Uint(&Where_or_Value, sizeof(Where_or_Value))) != STS_SUCCESS) {
                errCode = 0x0B; goto exit_Method_Set;
            }
        }

        if (Where_or_Value != 0x01) {  //Values ?
            zSTS = STS_INVALID_PARAMETER; errCode = 0x0C; goto exit_Method_Set;
        }

        if ((zSTS = AtomDecoding_ByteHdr(&decLen)) != STS_SUCCESS) {
            errCode = 0x10; goto exit_Method_Set;
        }

        if ((iPload+decLen) >= mCmdPkt.mSubPktFmt.length)
        {
            zSTS = STS_INVALID_PARAMETER; errCode = 0x0D; goto exit_Method_Set;
        }

        if (Write2Mtable(req, &mCmdPkt.payload[iPload], decLen, tmp32, 0) == zNG) {
            zSTS = STS_INVALID_PARAMETER; errCode = 0x0E; goto exit_Method_Set;
        }
        iPload += decLen;
    }
    else
    { // Object table, should omit "Where" for object table
        if (Where_or_Value != 0x01) {  //Values ?
            zSTS = STS_INVALID_PARAMETER; errCode = 0x10; goto exit_Method_Set;
        }

        if (ChkToken() != TOK_StartList) {
            zSTS = STS_SESSION_ABORT; errCode = 0x11; goto exit_Method_Set;
        }

        while (ChkToken() == TOK_StartName)
        { // ColNo + Values pairs

            if (setParmCnt >= eMaxSetParamCnt)
            {
                zSTS = STS_INVALID_PARAMETER;  errCode = 0x12; goto exit_Method_Set;
            }

            //get Column#
            if ((zSTS = AtomDecoding_Uint((U8*)&SetParm_ColNo[setParmCnt], sizeof(SetParm_ColNo[0]))) != STS_SUCCESS)
            {
                zSTS = STS_INVALID_PARAMETER;  errCode = 0x13; goto exit_Method_Set;
            }

            // test case A13-4-1-4-15, check if set the same column ?
            if (setParmCnt)
            {
                for (i = 0; i < setParmCnt; i++) {
                    if (SetParm_ColNo[setParmCnt] == SetParm_ColNo[i]) {
                        zSTS = STS_INVALID_PARAMETER; errCode = 0x14; goto exit_Method_Set;
                    }
                }
            }

            // search the column# and get its properties
            for (i = 0; i < ((sTcgTblHdr*)pInvokingTbl)->colCnt; i++)
            {
                if ((pInvColPty[i].colNo) == SetParm_ColNo[setParmCnt])
                {
                    colType = pInvColPty[i].colType;
                    colSize = pInvColPty[i].size;
                    break;
                }
            }
            if(i== ((sTcgTblHdr*)pInvokingTbl)->colCnt) // not found
            {    zSTS = STS_INVALID_PARAMETER; errCode = 0x15; goto exit_Method_Set;     }

            if (aceColumns_chk(SetParm_ColNo[setParmCnt])==zNG)
            {    zSTS = STS_INVALID_PARAMETER; errCode = 0x16; goto exit_Method_Set;    }

            if (ChkToken() == TOK_StartList)
            { // LIST_TYPE | LISTUID_TYPE
                U8 listIdx = 0;

                if (colType == LIST_TYPE)
                { // LIST_TYPE (ex: "DoneOnReset" | "LockOnReset", ListType: F0, n1, n2, ... F1)
                    while (ChkToken() != TOK_EndList)
                    {
                        iPload--;
                        if (AtomDecoding_Uint(&byte, sizeof(byte)) == STS_SUCCESS)
                        {
                            if (Write2Mtable(req, &byte, 1, SetParm_ColNo[setParmCnt], listIdx) == zNG)
                            {
                                zSTS = STS_INVALID_PARAMETER; errCode = 0x17; goto exit_Method_Set;
                            }
                            ++listIdx;
                        }
                    }
                }
                else if (colType == UIDLIST_TYPE)
                { // UIDLIST_TYPE (ex. ACE_Locking_Range1_Set_RdLocked_UID.Set[Values = [BooleanExpr = [User1_UID | User2_UID]]])

                    listIdx = 0;

                    while ((byte = ChkToken()) == TOK_StartName)
                    {
                        if ((zSTS = AtomDecoding_HUid2((U8*)&tmp32)) != STS_SUCCESS)
                        {
                            errCode = 0x18; goto exit_Method_Set;
                        }

                        switch (tmp32)
                        {
                            case (U32)UID_CT_Authority_object_ref:
                                if ((zSTS = AtomDecoding_Uid2((U8*)&tmp64)) != STS_SUCCESS) {  //Authority_object
                                    zSTS = STS_SESSION_ABORT; errCode = 0x19; goto exit_Method_Set;
                                }

                                if (ChkToken() != TOK_EndName) {
                                    zSTS = STS_SESSION_ABORT; errCode = 0x1A; goto exit_Method_Set;
                                }

                                if (Write2Mtable(req, (U8*)&tmp64, sizeof(tmp64), SetParm_ColNo[setParmCnt], listIdx) == zNG)
                                {   zSTS = STS_INVALID_PARAMETER; errCode = 0x1B; goto exit_Method_Set;  }

                                listIdx++;
                                break;

                            case (U32)UID_CT_boolean_ACE:
                                if ((zSTS = AtomDecoding_Uint((U8*)&tmp32, sizeof(tmp32))) != STS_SUCCESS)
                                {  zSTS = STS_INVALID_PARAMETER; errCode = 0x1C; goto exit_Method_Set;  }
                                if (tmp32 != 1)
                                {  // ACE_boolean != OR
                                    zSTS = STS_INVALID_PARAMETER; errCode = 0x1D; goto exit_Method_Set;
                                }
                                if (ChkToken() != TOK_EndName)
                                {  zSTS = STS_SESSION_ABORT; errCode = 0x1E; goto exit_Method_Set; }
                                break;

                            default:
                                zSTS = STS_INVALID_PARAMETER; errCode = 0x1F; goto exit_Method_Set;
                        }
                    } // while(TOK_StartName)

                    if (byte != TOK_EndList)
                    {   zSTS = STS_SESSION_ABORT; errCode = 0x20; goto exit_Method_Set; }

                }
                else //if ((colType != UIDLIST_TYPE) && (colType != LIST_TYPE))
                {
                    zSTS = STS_INVALID_PARAMETER; errCode = 0x21; goto exit_Method_Set;
                }
            }
            else
            {  // Non-ListType value
                --iPload;

                if (colType == VALUE_TYPE)
                {
                    if ((zSTS = AtomDecoding_Uint((U8*)&tmp64, sizeof(tmp64))) != STS_SUCCESS) {
                        errCode = 0x22; goto exit_Method_Set;
                    }

                    if( ((colSize == 1) && (tmp64 > 0xff)) ||
                        ((colSize == 2) && (tmp64 > 0xffff)) ||
                        ((colSize == 4) && (tmp64 > 0xffffffff)))
                    {
                        errCode = 0x23; goto exit_Method_Set;
                    }

                    if (Write2Mtable(req, (U8*)&tmp64, colSize, SetParm_ColNo[setParmCnt], 0) == zNG) {
                        zSTS = STS_INVALID_PARAMETER; errCode = 0x24; goto exit_Method_Set;
                    }
                }
                else if ((colType == VBYTE_TYPE) || (colType == STRING_TYPE))
                {
                    if ((zSTS = AtomDecoding_ByteHdr(&decLen)) != STS_SUCCESS) {
                        errCode = 0x25; goto exit_Method_Set;
                    }

                    if (Write2Mtable(req, &mCmdPkt.payload[iPload], decLen, SetParm_ColNo[setParmCnt], 0) == zNG) {
                        zSTS = STS_INVALID_PARAMETER; errCode = 0x26; goto exit_Method_Set;
                    }

                    iPload += decLen;
                }
            }

            if (ChkToken() != TOK_EndName)
            {
                zSTS = STS_SESSION_ABORT; errCode = 0x28; goto exit_Method_Set;
            }

            setParmCnt++;
        }
        iPload--;
        if (ChkToken() != TOK_EndList) {
            zSTS = STS_SESSION_ABORT; errCode = 0x29; goto exit_Method_Set;
        }

        if (setParmCnt == 0) {  // no match column
            zSTS = STS_INVALID_PARAMETER; errCode = 0x2A; goto exit_Method_Set;
        }
    }
    //    else {   zSTS=STS_INVALID_PARAMETER; errCode=0x60; goto exit_Method_Set; }

    if (ChkToken() != TOK_EndName) {
        zSTS = STS_SESSION_ABORT; errCode = 0x2B; goto exit_Method_Set;
    }
    if (ChkToken() != TOK_EndList) {
        zSTS = STS_SESSION_ABORT; errCode = 0x2C; goto exit_Method_Set;
    }
    if (ChkToken() != TOK_EndOfData) {
        zSTS = STS_SESSION_ABORT; errCode = 0x2D; goto exit_Method_Set;
    }

    //status list check
    U8 *p = &mCmdPkt.payload[iPload];
    //printk("status list = %08x, %08x, %08x, %08x, %08x, %08x\n", *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5));
    DBG_P(0x7, 0x03, 0x71010E, 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5));  // status list = %08x, %08x, %08x, %08x, %08x, %08x
    zSTS = chk_method_status();
    if (zSTS != STS_SUCCESS)
    {
        errCode = 0x30;
        goto exit_Method_Set;
    }

    //check locking range start or range length whether has been changed
    if (bLockingRangeChanged)
    {
        bLockingRangeChanged = FALSE;
        for (y = 0; y < pG3->b.mLckLocking_Tbl.hdr.rowCnt; y++)  //search row
        {
            if (invokingUID.all == pG3->b.mLckLocking_Tbl.val[y].uid)
            {
                if (LockingTbl_RangeChk(pG3->b.mLckLocking_Tbl.val[y].uid, pG3->b.mLckLocking_Tbl.val[y].rangeStart, pG3->b.mLckLocking_Tbl.val[y].rangeLength) == zNG)
                {
                    zSTS = STS_INVALID_PARAMETER; errCode = 0x31; goto exit_Method_Set;
                }
                break;
            }
        }
    }

exit_Method_Set:
    if (!errCode)
    {
        fill_no_data_token_list();
        return STS_SUCCESS;
    }
    else
    {
        TCGPRN("errCode|%x, zSTS|%08x\n",errCode, sSTS);
        DBG_P(0x3, 0x03, 0x71010D, 4,errCode, 4, zSTS);  // errCode|%x, zSTS|%08x

        // DBG_P(2, 3, 0x82018F, 1, errCode);  //82 01 8F, "Admin_Set err,  err_code= %X", 1
        return zSTS;   //or STS_SESSION_ABORT
    }
}

// set SMBR method Callback ---------------------------------------------
tcg_code bool cb_setSmbr(Complete)(req_t *req)
{
    TCGPRN("cb_setSmbr_Complete()\n");
    DBG_P(0x01, 0x03, 0x710042 );  // cb_setSmbr_Complete()
    flgs_MChnged.b.SMBR = TRUE;

    if (mSessionManager.TransactionState == TRNSCTN_IDLE){
        if(flgs_MChnged.b.SMBR_Cb_Acting){
            if(method_result == STS_SUCCESS){
                cb_set_ok(Begin)(req);
            }else{
                cb_set_ng(Begin)(req);
            }
            flgs_MChnged.b.SMBR_Cb_Acting = FALSE;
            return TRUE;
        }
    }
    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_setSmbr(SmbrWr)(req_t *req)
{
    TCGPRN("cb_setSmbr_SmbrWr()\n");
    DBG_P(0x01, 0x03, 0x710043 );  // cb_setSmbr_SmbrWr()
    mtdSetSmbr_varsMgm_t *p = &mtdSetSmbr_varsMgm;

    memcpy((U8 *)tcgTmpBuf + p->laaOffsetBeginAdr, p->pbuf, p->smbrWrLen);
    // SMBR_Write(LaaAddr, LaaAddr+LaaCnt, tcgTmpBuf, LaaOffAddr, tLen, tBuf);
    tcg_ipc_post_ex(req, MSG_TCG_SMBRWR, cb_setSmbr(Complete), p->laaBeginAdr, p->laaBeginAdr + p->laaCnts, (U8 *)tcgTmpBuf);
    return TRUE;
}

tcg_code bool cb_setSmbr(SmbrRdMulti)(req_t *req)
{
    TCGPRN("cb_setSmbr_SmbrRdMulti()\n");
    DBG_P(0x01, 0x03, 0x710044 );  // cb_setSmbr_SmbrRdMulti()
    mtdSetSmbr_varsMgm_t *p = &mtdSetSmbr_varsMgm;
    // SMBR_Read(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   //read all
    tcg_ipc_post_ex(req, MSG_TCG_SMBRRD, cb_setSmbr(SmbrWr), p->laaBeginAdr, p->laaBeginAdr + p->laaCnts, (U8 *)tcgTmpBuf);
    return TRUE;
}

tcg_code bool cb_setSmbr(SmbrRdSingle)(req_t *req)
{
    TCGPRN("cb_setSmbr_SmbrRdSingle()\n");
    DBG_P(0x01, 0x03, 0x710045 );  // cb_setSmbr_SmbrRdSingle()
    mtdSetSmbr_varsMgm_t *p = &mtdSetSmbr_varsMgm;

    while(p->idx < p->laaBeginAdr + p->laaCnts){
        if ((pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START + p->idx].blk < TCG_MBR_CELLS)
         || (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + p->idx].blk < TCG_MBR_CELLS))   //blank ?
        {
            // SMBR_Read(p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));   //read 1 page
            tcg_ipc_post_ex(req, MSG_TCG_SMBRRD, cb_setSmbr(SmbrRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
            p->idx++;
            return TRUE;
        }
        p->idx++;
    }

    cb_setSmbr(SmbrWr)(req);
    return TRUE;
}



tcg_code bool cb_setSmbr(Begin)(req_t *req)
{
    TCGPRN("cb_setSmbr_Begin()\n");
    DBG_P(0x01, 0x03, 0x710046 );  // cb_setSmbr_Begin()
    mtdSetSmbr_varsMgm_t *p = &mtdSetSmbr_varsMgm;

    p->laaBeginAdr       = p->columnBeginAdr / LAA_LEN;
    p->laaOffsetBeginAdr = p->columnBeginAdr % LAA_LEN;
    SMBR_ioCmdReq = FALSE;
    p->laaCnts           = (p->laaOffsetBeginAdr + p->smbrWrLen) / LAA_LEN;
    if((p->laaOffsetBeginAdr + p->smbrWrLen) % LAA_LEN) p->laaCnts += 1;
    // memset((U8 *)tcgTmpBuf, 0x00,  mtdSetSmbr_varsMgm.laaCnts * LAA_LEN);    // reduce time spend
    if(p->laaOffsetBeginAdr || ((p->columnBeginAdr + p->smbrWrLen) % LAA_LEN)){
        p->hasBlank = FALSE;
        for(p->idx = p->laaBeginAdr; p->idx < p->laaBeginAdr + p->laaCnts; p->idx++){
            if (pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START + p->idx].blk >= TCG_MBR_CELLS){
                p->hasBlank = TRUE;
                break;
            }
        }

        if (p->hasBlank)
        {
            p->idx = p->laaBeginAdr;
            while (p->idx < p->laaBeginAdr + p->laaCnts){
                if ((pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START + p->idx].blk < TCG_MBR_CELLS)
                 || (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START + p->idx].blk < TCG_MBR_CELLS))   //blank ?
                {
                    // SMBR_Read(p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));   //read 1 page
                    tcg_ipc_post_ex(req, MSG_TCG_SMBRRD, cb_setSmbr(SmbrRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
                    p->idx++;
                    flgs_MChnged.b.SMBR_Cb_Acting = TRUE;
                    return TRUE;
                }
                p->idx++;
            }
        }
        else
        {
            // SMBR_Read(p->laaBeginAdr, p->laaBeginAdr+p->laaCnts, (U8 *)tcgTmpBuf);   //read all
            cb_setSmbr(SmbrRdMulti)(req);
            flgs_MChnged.b.SMBR_Cb_Acting = TRUE;
            return TRUE;
        }

    }
    cb_setSmbr(SmbrWr)(req);
    flgs_MChnged.b.SMBR_Cb_Acting = TRUE;
    return TRUE;
}

// set DS method Callback --------------------------------------------------
tcg_code bool cb_setDs(Complete)(req_t *req)
{
    TCGPRN("cb_setDs_Complete()\n");
    DBG_P(0x01, 0x03, 0x710047 );  // cb_setDs_Complete()
    flgs_MChnged.b.DS = TRUE;

    if (mSessionManager.TransactionState == TRNSCTN_IDLE){
        if(flgs_MChnged.b.DS_Cb_Acting){
            if(method_result == STS_SUCCESS){
                cb_set_ok(Begin)(req);
            }else{
                cb_set_ng(Begin)(req);
            }
            flgs_MChnged.b.DS_Cb_Acting = FALSE;
            return TRUE;
        }
    }
    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_setDs(DsWr)(req_t *req)
{
    TCGPRN("cb_setDs_DsWr()\n");
    DBG_P(0x01, 0x03, 0x710048 );  // cb_setDs_DsWr()
    mtdSetDs_varsMgm_t *p = &mtdSetDs_varsMgm;

    memcpy((U8 *)tcgTmpBuf + p->laaOffsetBeginAdr, p->pbuf, p->dsWrLen);
    // DS_Write(LaaAddr, LaaAddr + LaaCnt, tcgTmpBuf, LaaOffAddr, tLen, tBuf);
    tcg_ipc_post_ex(req, MSG_TCG_DSWR, cb_setDs(Complete), p->laaBeginAdr, p->laaBeginAdr + p->laaCnts, (U8 *)tcgTmpBuf);
    return TRUE;
}

tcg_code bool cb_setDs(DsRdMulti)(req_t *req)
{
    TCGPRN("cb_setDs_DsRdMulti()\n");
    DBG_P(0x01, 0x03, 0x710049 );  // cb_setDs_DsRdMulti()
    mtdSetDs_varsMgm_t *p = &mtdSetDs_varsMgm;
    // TcgFuncRequest2(MSG_TCG_DSRD, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   // read all
    tcg_ipc_post_ex(req, MSG_TCG_DSRD, cb_setDs(DsWr), p->laaBeginAdr, p->laaBeginAdr + p->laaCnts, (U8 *)tcgTmpBuf);
    return TRUE;
}

tcg_code bool cb_setDs(DsRdSingle)(req_t *req)
{
    TCGPRN("cb_setDs_DsRdSingle()\n");
    DBG_P(0x01, 0x03, 0x71004A );  // cb_setDs_DsRdSingle()
    mtdSetDs_varsMgm_t *p = &mtdSetDs_varsMgm;

    while(p->idx < p->laaBeginAdr + p->laaCnts){
        if ((pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START + p->idx].blk < TCG_MBR_CELLS)
         || (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + p->idx].blk < TCG_MBR_CELLS))   //blank ?
        {
            // TcgFuncRequest2(MSG_TCG_DSRD, ii, ii+1, (U8 *)tcgTmpBuf + (ii-LaaAddr)*CFG_UDATA_PER_PAGE);    //read 1 page
            tcg_ipc_post_ex(req, MSG_TCG_DSRD, cb_setDs(DsRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
            p->idx++;
            return TRUE;
        }
        p->idx++;
    }

    cb_setDs(DsWr)(req);
    return TRUE;
}



tcg_code bool cb_setDs(Begin)(req_t *req)
{
    mtdSetDs_varsMgm_t *p = &mtdSetDs_varsMgm;
    TCGPRN("cb_setDs_Begin() columnBeginAdr|%x, dsWrLen|%x\n", p->columnBeginAdr, p->dsWrLen);
    DBG_P(0x3, 0x03, 0x71004B, 4, p->columnBeginAdr, 4, p->dsWrLen);  // cb_setDs_Begin() columnBeginAdr|%x, dsWrLen|%x

    p->laaBeginAdr       = p->columnBeginAdr / LAA_LEN;
    p->laaOffsetBeginAdr = p->columnBeginAdr % LAA_LEN;
    // SMBR_ioCmdReq = FALSE;
    p->laaCnts           = (p->laaOffsetBeginAdr + p->dsWrLen) / LAA_LEN;
    if((p->laaOffsetBeginAdr + p->dsWrLen) % LAA_LEN) p->laaCnts += 1;
    TCGPRN("laaBeginAdr|%x, laaCnts|%x\n", p->laaBeginAdr, p->laaCnts);
    DBG_P(0x3, 0x03, 0x71004C, 4, p->laaBeginAdr, 4, p->laaCnts);  // laaBeginAdr|%x, laaCnts|%x
    memset((U8 *)tcgTmpBuf, 0x00,  mtdSetDs_varsMgm.laaCnts * LAA_LEN);
    if(p->laaOffsetBeginAdr || ((p->columnBeginAdr + p->dsWrLen) % LAA_LEN)){
        p->hasBlank = FALSE;
        for(p->idx = p->laaBeginAdr; p->idx < p->laaBeginAdr + p->laaCnts; p->idx++){
            if (pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START + p->idx].blk >= TCG_MBR_CELLS){
                p->hasBlank = TRUE;
                break;
            }
        }

        if (p->hasBlank)
        {
            p->idx = p->laaBeginAdr;
            while (p->idx < p->laaBeginAdr + p->laaCnts){
                if ((pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START + p->idx].blk < TCG_MBR_CELLS)
                 || (pG4->b.TcgMbrL2P[TCG_DS_LAA_START + p->idx].blk < TCG_MBR_CELLS))   //blank ?
                {
                    // TcgFuncRequest2(MSG_TCG_DSRD, ii, ii+1, (U8 *)tcgTmpBuf + (ii-LaaAddr)*CFG_UDATA_PER_PAGE);    //read 1 page
                    tcg_ipc_post_ex(req, MSG_TCG_DSRD, cb_setDs(DsRdSingle), p->idx, p->idx + 1, (U8 *)tcgTmpBuf + ((p->idx - p->laaBeginAdr) * CFG_UDATA_PER_PAGE));
                    p->idx++;
                    flgs_MChnged.b.DS_Cb_Acting = TRUE;
                    return TRUE;
                }
                p->idx++;
            }
        }
        else
        {
            // TcgFuncRequest2(MSG_TCG_DSRD, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   // read all
            cb_setDs(DsRdMulti)(req);
            flgs_MChnged.b.DS_Cb_Acting = TRUE;
            return TRUE;
        }

    }
    cb_setDs(DsWr)(req);
    flgs_MChnged.b.DS_Cb_Acting = TRUE;
    return TRUE;
}

// -------------------------------------------------------------------------------

tcg_code int Write2Mtable(req_t *req, U8 *tBuf, U32 tLen, U32 setColNo, U8 listIdx)
{
    U64 uidAuthClass;
    U8  errCode = 0;
    U8  i = 0;
    U8  y;

    // DBG_P(4, 3, 0x820124, 4, (U32)(invokingUID.all >> 32), 4, setColNo, 4, tLen);  //82 01 24, "[F]Write2Mtable UID[%X] Col[%X] Len[%X]", 4 4 4

    TCGPRN("Write2Mtable(), invokingUID|%08x%08x, setColNo|%x, tLen|%x\n", (U32)(invokingUID.all >> 32), (U32)invokingUID.all, setColNo, tLen);
    DBG_P(0x5, 0x03, 0x71004D, 4, (U32)(invokingUID.all >> 32), 4, (U32)invokingUID.all, 4, setColNo, 4, tLen);  // Write2Mtable(), invokingUID|%x%x, setColNo|%x, tLen|%x
    switch(invokingUID.all >> 32)
    {
        case UID_ACE>>32:
            if (mSessionManager.SPID.all == UID_SP_Admin)
            {
                errCode = 0x80;
                goto Exit_Write2Mtable;
            }
            else if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                for (y = 0; y < pG3->b.mLckACE_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG3->b.mLckACE_Tbl.val[y].uid)
                        break;
                }
                if (y == pG3->b.mLckACE_Tbl.hdr.rowCnt)
                {
                    errCode = 0x81;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 3:  //BooleanExpr
                        // DBG_P(3, 3, 0x820190, 4, (U32)((*(U64 *)tBuf) >> 32), 4, (U32)(*(U64 *)tBuf));  //82 01 90, "ACE_Wr_BooleanExpr[%08X-%08X]", 4 4
                        //ACE_C_PIN_UserX_Set_PIN shall only accept "Admins" and "Admins OR UserX"
                        if ((invokingUID.all & 0xFFFFFFFFFFFFFF00) == UID_ACE_C_PIN_User_Set_PIN)
                        {
                            if (listIdx == 0)
                            {
                                // 1st Auth must be Admins
                                if (*(U64 *)tBuf != UID_Authority_Admins)
                                {
                                    errCode = 0x85;
                                    goto Exit_Write2Mtable;
                                }
                                else
                                {
                                    pG3->b.mLckACE_Tbl.val[y].booleanExpr[1] = 0; //clear next cell
                                }
                            }
                            else if (listIdx == 1)
                            {
                                // must be userMMMM
                                //User UID can only be UserX
                                uidAuthClass = *(U64 *)tBuf & 0xFFFFFFFFFFFFFF00;
                                if (uidAuthClass != UID_Authority_Users)
                                {
                                    errCode = 0x86;
                                    goto Exit_Write2Mtable;
                                }

                                i = *(U64 *)tBuf & 0xFF;
                                if (i != (invokingUID.all & 0xff))
                                {
                                    errCode = 0x87;
                                    goto Exit_Write2Mtable;
                                }
                            }
                            else //too many elements
                            {
                                errCode = 0x88;
                                goto Exit_Write2Mtable;
                            }
                        }
                        else
                        {
                            if (listIdx >= LCK_ACE_BOOLEXPR_CNT)
                            {
                                errCode = 0x8A;
                                goto Exit_Write2Mtable;
                            }

                            uidAuthClass = *(U64 *)tBuf & 0xFFFFFFFFFFFFFF00;
                            i = *(U64 *)tBuf & 0xFF;
                            if (uidAuthClass == UID_Authority_AdminX)
                            {
                                if (i > TCG_AdminCnt)
                                {
                                    errCode = 0x8B;
                                    goto Exit_Write2Mtable;
                                }
                            }
                            else if (uidAuthClass == UID_Authority_Users)
                            {
                                if (i > TCG_UserCnt)
                                {
                                    errCode = 0x8C;
                                    goto Exit_Write2Mtable;
                                }
                            }
                            else if ((*(U64 *)tBuf != UID_Authority_Admins)
                                 &&  (*(U64 *)tBuf != UID_Authority_Anybody))  // EHDD
                            {
                                errCode = 0x8D;
                                goto Exit_Write2Mtable;
                            }

                            // test case D1-1-1-1-13  , (n user_x, 1 admin_x)
                            if (listIdx)
                            {
                                uidAuthClass = *(U64 *)tBuf;
                                for (i = 0; i < listIdx; i++)
                                {
                                    if (uidAuthClass == (pG3->b.mLckACE_Tbl.val[y].booleanExpr[i]))
                                    {
                                        errCode = 0x8E;
                                        goto Exit_Write2Mtable;
                                    }
                                }
                            }
                            else
                            {
                                // (listIdx==0) // 1st element
                                for (i = 0; i < LCK_ACE_BOOLEXPR_CNT; i++)
                                {
                                    pG3->b.mLckACE_Tbl.val[y].booleanExpr[i] = 0;
                                }
                            }
                        }

                        pG3->b.mLckACE_Tbl.val[y].booleanExpr[listIdx] = *(U64 *)tBuf;
                        flgs_MChnged.b.G3 = TRUE;
                        break;
                    default:
                        errCode = 0x8F;
                        goto Exit_Write2Mtable;
                }
            }
            break; // case 0x00000008 : ACE table

        case UID_Authority >> 32:
            //printk("\nUID_Authority : ");
            DBG_P(0x01, 0x03, 0x71004E );  // UID_Authority :
            if (mSessionManager.SPID.all == UID_SP_Admin)
            {
                //printk("Admin SP\n");
                DBG_P(0x01, 0x03, 0x71004F );  // Admin SP
                for (y = 0; y < pG1->b.mAdmAuthority_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG1->b.mAdmAuthority_Tbl.val[y].uid)
                        break;
                }
                if (y == pG1->b.mAdmAuthority_Tbl.hdr.rowCnt)
                {
                    errCode = 0x90;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 5: //Enabled
                        // DBG_P(2, 3, 0x820191, 4, *(bool *)tBuf);  //82 01 91, "AdmAuthority_Wr_Enable[%08X]", 4
                        //printk("AdmAuthority_Wr_Enable[%x]\n", *(bool *)tBuf);
                        DBG_P(0x2, 0x03, 0x710050, 4, *(bool *)tBuf);  // AdmAuthority_Wr_Enable[%x]
                        if(((*(bool *)tBuf) & 0xFFFFFFFE) != 0){
                            errCode = 0x61;
                            goto Exit_Write2Mtable;
                        }
                        pG1->b.mAdmAuthority_Tbl.val[y].enabled = *(bool *)tBuf;
                        flgs_MChnged.b.G1 = TRUE;
                        break;

                    default:
                        errCode = 0x91;
                        goto Exit_Write2Mtable;
                }
            }
            else if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                //printk("Locking SP\n");
                DBG_P(0x01, 0x03, 0x710051 );  // Locking SP
                for (y = 0; y < pG3->b.mLckAuthority_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG3->b.mLckAuthority_Tbl.val[y].uid)
                        break;
                }
                if (y == pG3->b.mLckAuthority_Tbl.hdr.rowCnt)
                {
                    errCode = 0x92;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 2: //CommonName
                        memset(pG3->b.mLckAuthority_Tbl.val[y].commonName, 0, sizeof(pG3->b.mLckAuthority_Tbl.val[y].commonName));
                        memcpy(pG3->b.mLckAuthority_Tbl.val[y].commonName, tBuf, tLen);
                        flgs_MChnged.b.G3 = TRUE;
                        break;

                    case 5: //Enabled
                        if(((*(bool *)tBuf) & 0xFFFFFFFE) != 0){
                            errCode = 0x62;
                            goto Exit_Write2Mtable;
                        }
                        pG3->b.mLckAuthority_Tbl.val[y].enabled = *(bool *)tBuf;
                        flgs_MChnged.b.G3 = TRUE;
                    #if CO_SUPPORT_AES
                        if (TCG_ACT_IN_OPAL())
                        {
                            uidAuthClass = UID_CPIN + invokingUID.dw[0];
                            for (y = 0; y < pG3->b.mLckCPin_Tbl.hdr.rowCnt; y++)
                            {
                                if (uidAuthClass == pG3->b.mLckCPin_Tbl.val[y].uid)
                                break;
                            }
                            if (pG3->b.mLckCPin_Tbl.val[y].cPin.cPin_Tag == CPIN_NULL)
                            { // no password
                                if (tBuf == FALSE)
                                {
                                    TcgEraseOpalKEK((U32)uidAuthClass);
                                }
                                else
                                {
                                    TcgWrapOpalKEK(NULL, 0, (U32)uidAuthClass, WrapKEK);
                                }
                            }
                        }
                    #endif
                        break;
                    default:
                        errCode = 0x94;
                        goto Exit_Write2Mtable;
                }
            }
            break;  // case 0x00000009 : Authority table

        case UID_CPIN>>32:
            //printk("\nUID_CPIN : ");
            DBG_P(0x01, 0x03, 0x710052 );  // UID_CPIN :
            if (mSessionManager.SPID.all == UID_SP_Admin)
            {
                //printk("Admin SP\n");
                DBG_P(0x01, 0x03, 0x710053 );  // Admin SP
                for (y = 0; y < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG1->b.mAdmCPin_Tbl.val[y].uid)
                        break;
                }
                if (y == pG1->b.mAdmCPin_Tbl.hdr.rowCnt)
                {
                    errCode = 0xB0;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 3: // PIN
                        if (tLen > CPIN_LENGTH)
                        {
                            errCode = 0xB1;
                            goto Exit_Write2Mtable;
                        }
                    #if TCG_FS_PSID
                        if (pG1->b.mAdmCPin_Tbl.val[y].uid == UID_CPIN_PSID)
                        {    //check if PSID updated
                            if ((CPinMsidCompare(CPIN_PSID_IDX) == zNG) || (CPinMsidCompare(CPIN_SID_IDX) == zNG))
                            {
                                errCode = 0xB2;
                                goto Exit_Write2Mtable;
                            }
                        #if 0  //cjdbg, ToDO
                            else
                            { // copy G1 cTbl to mTbl, other G1 data might be reverted too.
                                TcgMedia_SyInReadTable(TCG_SYIN_G1, TRUE);
                            }
                        #endif
                        }
                    #endif
                    #if CO_SUPPORT_AES
                        if (pG1->b.mAdmCPin_Tbl.val[y].uid == UID_CPIN_SID)
                        {
                            mSessionManager.HtChallenge[0] = tLen;
                            for (i = 0; i< tLen; i++)
                                mSessionManager.HtChallenge[i+1] = tBuf[i];
                        }
                    #endif
                        if (tLen)
                        {
                            // TCG_DBG_P(5, 3, 0x820196, 1, tBuf[0], 1, tBuf[1], 1, tBuf[2], 1, tBuf[3]);   //82 01 96, "cpin Adm: %2X%2X%2X%2X ...", 1 1 1 1
                            //printk("cpin Adm: %x%x%x%x ...", tBuf[0], tBuf[1], tBuf[2], tBuf[3]);
                            DBG_P(0x5, 0x03, 0x710054, 4, tBuf[0], 4, tBuf[1], 4, tBuf[2], 4, tBuf[3]);  // cpin Adm: %x%x%x%x ...
                            Tcg_GenCPinHash(tBuf, (U8)tLen, &pG1->b.mAdmCPin_Tbl.val[y].cPin);

                        #if TCG_FS_PSID
                            if (pG1->b.mAdmCPin_Tbl.val[y].uid == UID_CPIN_PSID)
                            {
                            #if 0 //cjdbg, ToDO
                                //update c-table
                                TcgMedia_SyInWriteTable(TCG_SYIN_G1, TRUE);
                                SyIn_Synchronize(SI_AREA_SECURITY, SYSINFO_WRITE, SI_SYNC_BY_TCG);

                                memcpy(smSysInfo->d.MPInfo.d.PSID, &pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val, CPIN_LENGTH + CPIN_SALT_LEN);
                                smSysInfo->d.MPInfo.d.TagPSID = SI_TAG_TCG;
                                SyIn_Synchronize(SI_AREA_NOR, SYSINFO_WRITE, SI_SYNC_BY_TCG);
                            #else
                                TcgPsidBackup();
                            #endif
                            }
                        #endif
                        }
                        else
                        {
                            memset(&pG1->b.mAdmCPin_Tbl.val[y].cPin, 0, sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin));
                        }
                        flgs_MChnged.b.G1 = TRUE;
                        break; // case 3 : PIN

                    default:
                        errCode = 0xB3;
                        goto Exit_Write2Mtable;
                }
            }
            else if (mSessionManager.SPID.all == UID_SP_Locking)
            {
                //printk("Locking SP\n");
                DBG_P(0x01, 0x03, 0x710055 );  // Locking SP
                for (y = 0; y < pG3->b.mLckCPin_Tbl.hdr.rowCnt; y++)
                {
                    if (invokingUID.all == pG3->b.mLckCPin_Tbl.val[y].uid)
                        break;
                }
                if (y == pG3->b.mLckCPin_Tbl.hdr.rowCnt)
                {
                    errCode = 0xB4;
                    goto Exit_Write2Mtable;
                }
                switch(setColNo)
                {
                    case 3:
                        if (tLen > CPIN_LENGTH)
                        {
                            errCode = 0xB5;
                            goto Exit_Write2Mtable;
                        }

                        if (tLen)
                        {
                            // TCG_DBG_P(5, 3, 0x820197, 1, tBuf[0], 1, tBuf[1], 1, tBuf[2], 1, tBuf[3]);   //82 01 97, "cpin Lck: %2X%2X%2X%2X ...", 1 1 1 1
                            //printk("cpin Lck: %x%x%x%x ...", tBuf[0], tBuf[1], tBuf[2], tBuf[3]);
                            DBG_P(0x5, 0x03, 0x710056, 4, tBuf[0], 4, tBuf[1], 4, tBuf[2], 4, tBuf[3]);  // cpin Lck: %x%x%x%x ...
                            Tcg_GenCPinHash(tBuf, (U32)tLen, &pG3->b.mLckCPin_Tbl.val[y].cPin);
                        }
                        else
                        {
                            memset(&pG3->b.mLckCPin_Tbl.val[y].cPin, 0, sizeof(pG3->b.mLckCPin_Tbl.val[y].cPin));
                        }
                    // Change CPIN, Use Old KEK unwrap key and use new KEK wrap again.
                    #if CO_SUPPORT_AES
                        if (TCG_ACT_IN_OPAL())
                        {
                            uidAuthClass = UID_Authority + invokingUID.dw[0];
                            for (y = 0; y < pG3->b.mLckAuthority_Tbl.hdr.rowCnt; y++)
                            {
                                if (uidAuthClass == pG3->b.mLckAuthority_Tbl.val[y].uid)
                                    break;
                            }
                            if ((tLen==0) && (pG3->b.mLckAuthority_Tbl.val[y].enabled==0))
                            { // no password && disabled
                                TcgEraseOpalKEK((U32)uidAuthClass);
                            }
                            else
                            {
                                TcgWrapOpalKEK(tBuf, tLen, (U32)uidAuthClass, WrapKEK);
                            }
                        }
                        else if (TCG_ACT_IN_ALL_SU())   // entire ranges are at single user mode
                        {
                            if (invokingUID.all >= UID_CPIN_User1)
                            {
                                int j = (U8)(invokingUID.all - UID_CPIN_User1);
                                if ((pG3->b.mLckLocking_Tbl.val[j].writeLockEnabled)
                                    && (pG3->b.mLckLocking_Tbl.val[j].readLockEnabled))
                                {
                                    Tcg_UnWrapDEK(j, WrapKEK, TO_MTBL_KEYTBL);

                                    // Use New CPIN to Rewrap Key
                                    HAL_Gen_Key(pG3->b.mWKey[j].salt, sizeof(pG3->b.mWKey[j].salt));
                                    pG3->b.mWKey[j].state = TCG_KEY_UNWRAPPED;
                                    HAL_PBKDF2((U32 *)tBuf, (U32)tLen, pG3->b.mWKey[j].salt, sizeof(pG3->b.mWKey[j].salt), WrapKEK);

                                    Tcg_WrapDEK(j, WrapKEK);
                                }
                            }
                        }
                    #endif
                        flgs_MChnged.b.G3 = TRUE;
                        break; // case 3

                    default:
                        errCode = 0xB6;
                        goto Exit_Write2Mtable;
                }
            }
            break;  // case 0x0000000B : C_PIN table

        case UID_Locking >> 32:  // Locking
            //printk("\nUID_Locking : \n");
            DBG_P(0x01, 0x03, 0x710057 );  // UID_Locking :
            for (y = 0; y < pG3->b.mLckLocking_Tbl.hdr.rowCnt; y++)
            {
                if (invokingUID.all == pG3->b.mLckLocking_Tbl.val[y].uid)
                    break;
            }
            if (y == pG3->b.mLckLocking_Tbl.hdr.rowCnt)
            {
                errCode = 0xC0;
                goto Exit_Write2Mtable;
            }
            switch(setColNo)
            {
                case 2:  //CommonName
                    memset(pG3->b.mLckLocking_Tbl.val[y].commonName, 0, sizeof(pG3->b.mLckLocking_Tbl.val[y].commonName));
                    memcpy(pG3->b.mLckLocking_Tbl.val[y].commonName, tBuf, tLen);

                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 2 : CommonName

                case 3:  //RangeStart
                    // DBG_P(2, 3, 0x82019A, 4,*((U32 *)tBuf));  //82 01 9A, "RangeStart[%X]", 4
                    //printk("RangeStart[%x]", *((U32 *)tBuf));
                    DBG_P(0x2, 0x03, 0x710058, 4, *((U32 *)tBuf));  // RangeStart[%x]
                #if 0  // boundary need check ULink
                    if (*((U64 *)tBuf) & 0x000000000000000F)
                    {
                        errCode = 0xC1;
                        goto Exit_Write2Mtable;
                    }
                #endif
                    pG3->b.mLckLocking_Tbl.val[y].rangeStart = *((U64 *)tBuf);
                    bLockingRangeChanged = TRUE;
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 3 : RangeStart

                case 4:  //RangeLength
                    // DBG_P(2, 3, 0x82019B, 4,*((U32 *)tBuf));  //82 01 9B, "RangeLength[%X]", 4
                    //printk("RangeLength[%x]", *((U32 *)tBuf));
                    DBG_P(0x2, 0x03, 0x710059, 4, *((U32 *)tBuf));  // RangeLength[%x]
                #if 0  // boundary need check ULink
                    if(*((U64 *)tBuf) & 0x000000000000000F)
                    {
                        errCode = 0xC2;
                        goto Exit_Write2Mtable;
                    }
                #endif
                    pG3->b.mLckLocking_Tbl.val[y].rangeLength = *((U64 *)tBuf);
                    bLockingRangeChanged = TRUE;
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 4 : RangeLength

                case 5:  //ReadLockEnable
                    // DBG_P(2, 3, 0x82019C, 1, tBuf[0]);  //82 01 9C, "ReadLockEnable[%X]", 1
                    //printk("ReadLockEnable[%x]", tBuf[0]);
                    DBG_P(0x2, 0x03, 0x71005A, 4, tBuf[0]);  // ReadLockEnable[%x]
                    pG3->b.mLckLocking_Tbl.val[y].readLockEnabled = tBuf[0];

                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        TcgUpdateWrapKey(y);
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 5 :ReadLockEnable

                case 6:  //WriteLockEnable
                    // DBG_P(2, 3, 0x82019D, 1, tBuf[0]);  //82 01 9D, "WriteLockEnable[%X]", 1
                    //printk("WriteLockEnable[%x]", tBuf[0]);
                    DBG_P(0x2, 0x03, 0x71005B, 4, tBuf[0]);  // WriteLockEnable[%x]
                    pG3->b.mLckLocking_Tbl.val[y].writeLockEnabled=tBuf[0];

                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        TcgUpdateWrapKey(y);
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 6: WriteLockEnable

                case 7:  //ReadLock
                    // DBG_P(2, 3, 0x82019E, 1, tBuf[0]);  //82 01 9E, "ReadLock[%X]", 1
                    //printk("ReadLock[%x]", tBuf[0]);
                    DBG_P(0x2, 0x03, 0x71005C, 4, tBuf[0]);  // ReadLock[%x]
                    pG3->b.mLckLocking_Tbl.val[y].readLocked=tBuf[0];

                    //no need to write ReadLock to NAND if lockOnReset is PowerCycle and readLockEnabled=1
                    //(since fw will auto set ReadLock=TRUE)
                    //if((pG3->b.mLckLocking_Tbl.val[y].readLockEnabled==0)
                    // ||(pG3->b.mLckLocking_Tbl.val[y].lockOnReset[1]!=PowerCycle))
                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 7 : ReadLock

                case 8:  //WriteLock
                    // DBG_P(2, 3, 0x82019F, 1, tBuf[0]);  //82 01 9F, "WriteLock[%X]", 1
                    //printk("WriteLock[%x]", tBuf[0]);
                    DBG_P(0x2, 0x03, 0x71005D, 4, tBuf[0]);  // WriteLock[%x]
                    pG3->b.mLckLocking_Tbl.val[y].writeLocked=tBuf[0];

                    //if((pG3->b.mLckLocking_Tbl.val[y].writeLockEnabled==0)
                    // ||(pG3->b.mLckLocking_Tbl.val[y].lockOnReset[1]!=PowerCycle))
                #if CO_SUPPORT_AES
                    if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
                    {
                        mRawKeyUpdateList |= (0x01<<y);
                    }
                #endif
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 8: WriteLock

                case 9:  //LockOnReset, { PowerCycle } or { PowerCycle, Programmatic }
                    if (listIdx == 0)
                    {
                        if (*tBuf != PowerCycle)
                        {
                            errCode = 0xC7;
                            goto Exit_Write2Mtable;
                        }
                        memset(pG3->b.mLckLocking_Tbl.val[y].lockOnReset, 0, sizeof(pG3->b.mLckLocking_Tbl.val[y].lockOnReset));
                    }
                    else if (listIdx == 1)
                    {
                        if (*tBuf != Programmatic)
                        {
                            errCode = 0xC8;
                            goto Exit_Write2Mtable;
                        }
                    }
                    else
                    {
                        errCode = 0xC9; goto Exit_Write2Mtable;
                    }

                    i = listIdx +1;
                    pG3->b.mLckLocking_Tbl.val[y].lockOnReset[0] = i;
                    pG3->b.mLckLocking_Tbl.val[y].lockOnReset[i] = tBuf[0];
                    flgs_MChnged.b.G3 = TRUE;
                    break;
                default:
                    errCode = 0xCA;
                    goto Exit_Write2Mtable;
            }   // switch (setColNo)
            break;  // case 0x00000802 : Locking

        case UID_MBRControl>>32:    // MBRControl
            //printk("\nUID_MBRControl\n");
            DBG_P(0x01, 0x03, 0x71005E );  // UID_MBRControl
            for (y = 0; y < pG3->b.mLckMbrCtrl_Tbl.hdr.rowCnt; y++)
            {
                if (invokingUID.all == pG3->b.mLckMbrCtrl_Tbl.val[y].uid)
                    break;
            }
            if (y == pG3->b.mLckMbrCtrl_Tbl.hdr.rowCnt)
            {
                errCode = 0xD0;
                goto Exit_Write2Mtable;
            }
            switch(setColNo)
            {
                case 1:  //Enable
                    // DBG_P(2, 3, 0x8201A0, 1, tBuf[0]);  //82 01 A0, "MBRctl_Wr_Enable[%X]", 1
                    //printk("MBRctl_Wr_Enable[%x]\n", tBuf[0]);
                    DBG_P(0x2, 0x03, 0x71005F, 4, tBuf[0]);  // MBRctl_Wr_Enable[%x]
                    pG3->b.mLckMbrCtrl_Tbl.val[y].enable = tBuf[0];
                    flgs_MChnged.b.G3 = TRUE;
                    #ifndef alexcheck
                    ResetAllCache();       //note : ResetAllCache() = {}
                    #endif
                    break;

                case 2:  //Done
                    // DBG_P(2, 3, 0x8201A1, 1, tBuf[0]);  //82 01 A1, "MBRctl_Wr_Done[%X]", 1
                    //printk("MBRctl_Wr_Done[%x]\n", tBuf[0]);
                    DBG_P(0x2, 0x03, 0x710060, 4, tBuf[0]);  // MBRctl_Wr_Done[%x]
                    pG3->b.mLckMbrCtrl_Tbl.val[y].done = tBuf[0];

                    //no need to write "Done' to NAND if doneOnReset is PowerCycle and Enable=1
                    //(since fw will auto set Done=FALSE)
                    if((pG3->b.mLckMbrCtrl_Tbl.val[y].enable == 0))
                     //||(pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset!=PowerCycle))
                    {
                        flgs_MChnged.b.G3 = TRUE;
                    }
                    #ifndef alexcheck
                    ResetAllCache();       //note : ResetAllCache() = {}
                    #endif
                    break;

                case 3:  // DoneOnReset
                    if (listIdx == 0)
                    {
                        if (*tBuf != PowerCycle)
                        {
                            errCode = 0xD1;
                            goto Exit_Write2Mtable;
                        }
                    }
                    else if (listIdx == 1)
                    {
                        if (*tBuf != Programmatic)
                        {
                            errCode = 0xD2;
                            goto Exit_Write2Mtable;
                        }
                        memset(pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset, 0, sizeof(pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset));
                    }
                    else
                    {
                        errCode = 0xD3;
                        goto Exit_Write2Mtable;
                    }
                    i = listIdx + 1;
                    pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset[0] = i;
                    pG3->b.mLckMbrCtrl_Tbl.val[y].doneOnReset[i] = tBuf[0];
                    flgs_MChnged.b.G3 = TRUE;
                    break;  // case 3 : DoneOnReset

                default:
                    errCode = 0xD4;
                    goto Exit_Write2Mtable;
            }
            break;  // case 0x00000803 : MBRControl

        case UID_MBR>>32:  // @LockingSP
        {
            //printk("\nUID_MBR\n");
            DBG_P(0x01, 0x03, 0x710061 );  // UID_MBR
            #if 0
            U32 LaaAddr, LaaOffAddr, wrptr;
            int rlen;
            #endif

            // DBG_P(3, 3, 0x8201A2, 4, setColNo, 4, tLen);  //82 01 A2, "set MBR StartCol[%X] tLen[%X]", 4 4
            //printk("set MBR StartCol[%x] tLen[%x]", setColNo, tLen);
            DBG_P(0x3, 0x03, 0x710062, 4, setColNo, 4, tLen);  // set MBR StartCol[%x] tLen[%x]
            if ((setColNo + tLen) > MBR_LEN)
            {
                errCode = 0xD5;
                goto Exit_Write2Mtable;
            }

            mtdSetSmbr_varsMgm.pbuf           = tBuf;
            mtdSetSmbr_varsMgm.smbrWrLen      = tLen;
            mtdSetSmbr_varsMgm.columnBeginAdr = setColNo;

            #if 1
            cb_setSmbr(Begin)(req);
            #else
            rlen = tLen;              //rlen = wr remain length
            wrptr = setColNo;         //wr point
            if (rlen > 0)
            {
                U32 LaaCnt;
                // U8 *Desptr = (U8 *)tcgTmpBuf;

                LaaAddr = wrptr / LAA_LEN;
                LaaOffAddr = wrptr % LAA_LEN;
                // DBG_P(3, 3, 0x8201A3, 4, LaaAddr, 4, LaaOffAddr);  //82 01 A3, "LaaAddr[%X] LaaOffAddr[%X]", 4 4
                SMBR_ioCmdReq = FALSE;

                LaaCnt = (LaaOffAddr+rlen) / LAA_LEN;
                if ((LaaOffAddr + rlen) % LAA_LEN) LaaCnt += 1;
                // DBG_P(3, 3, 0x8201A4, 4, (U32)rlen, 4, LaaCnt);  //82 01 A4, "rlen[%X] LaaCnt[%X]", 4 4

                //WaitMbrRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 6 page for max transfer buffer
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt * LAA_LEN /*sizeof(tcgTmpBuf)*/);
                if(LaaOffAddr || ((setColNo+tLen)%LAA_LEN))
                {
            #if 1
                    U16 ii;
                    bool HasBlank = FALSE;

                    for (ii = LaaAddr; ii < LaaAddr + LaaCnt; ii++)
                    {
                        if ((pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START+ii].blk) >= (TCG_MBR_CELLS)){
                            HasBlank = TRUE;
                            break;
                        }
                    }

                    if (HasBlank)
                    {
                        for (ii = LaaAddr; ii < LaaAddr + LaaCnt; ii++){
                            if ((pG5->b.TcgTempMbrL2P[TCG_SMBR_LAA_START+ii].blk) < (TCG_MBR_CELLS)
                             || (pG4->b.TcgMbrL2P[TCG_SMBR_LAA_START+ii].blk) < (TCG_MBR_CELLS))   //blank ?
                            {
                                SMBR_Read(ii, ii+1, (U8 *)tcgTmpBuf + (ii-LaaAddr)*CFG_UDATA_PER_PAGE);   //read 1 page
                            }
                        }
                    }
                    else
                    {
                        SMBR_Read(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   //read all
                    }
            #else
                    SMBR_Read(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   //WaitMbrRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 1 page
            #endif
                }
                //memcpy((U8 *)&Desptr[LaaOffAddr], &tBuf[srcptr], rlen);
                //TcgFuncRequest2(MSG_TCG_SMBRWR, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);    //WaitMbrWr(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);
                SMBR_Write(LaaAddr, LaaAddr+LaaCnt, tcgTmpBuf, LaaOffAddr, tLen, tBuf);
                flgs_MChnged.b.SMBR = TRUE;
            }
            #endif   // #if 1
        }
        break;  // case 0x00000804 : MBR

        case UID_DataStore >> 32:   // @LockingSP
        case UID_DataStore2 >> 32:
        case UID_DataStore3 >> 32:
        case UID_DataStore4 >> 32:
        case UID_DataStore5 >> 32:
        case UID_DataStore6 >> 32:
        case UID_DataStore7 >> 32:
        case UID_DataStore8 >> 32:
        case UID_DataStore9 >> 32:
        {
            y = ((invokingUID.all >> 32) & 0xff) - 1;

            // DBG_P(6, 3, 0x8201A5, 4, setColNo, 4, tLen, 1, y, 4, mDataStoreAddr[y].offset, 4, mDataStoreAddr[y].length);  //82 01 A5, "sCol[%X] Len[%X] UserX[%X] UserXOffset[%X] UserXLen[%X]", 4 4 1 4 4

            if ((setColNo + tLen) > mDataStoreAddr[y].length /*|| tLen > DECBUF_LEN*/)
            {
                errCode = 0xE0;
                goto Exit_Write2Mtable;
            }

            mtdSetDs_varsMgm.pbuf           = tBuf;
            mtdSetDs_varsMgm.dsWrLen        = tLen;
            mtdSetDs_varsMgm.columnBeginAdr = setColNo + mDataStoreAddr[y].offset;

            #if 1
            cb_setDs(Begin)(req);
            #else
            // for test case A13-2-1-3-9
            if (tLen > 0)
            {
                U32 LaaAddr, LaaOffAddr;
                U32 LaaCnt;
                // U8 *Desptr=(U8 *)tcgTmpBuf;

                setColNo += mDataStoreAddr[y].offset;
                LaaAddr = setColNo / LAA_LEN;
                LaaOffAddr = setColNo % LAA_LEN;  //offset
                // DBG_P(3, 3, 0x8201A3, 4, LaaAddr, 4, LaaOffAddr);  //82 01 A3, "LaaAddr[%X] LaaOffAddr[%X]", 4 4

                LaaCnt = (LaaOffAddr+tLen)/LAA_LEN;
                if((LaaOffAddr + tLen) % LAA_LEN) LaaCnt += 1;
                // DBG_P(3, 3, 0x8201A6, 4, LaaAddr, 4, LaaAddr+LaaCnt-1);  //82 01 A6, "DS Wr slaa[%X] elaa[%X]", 4 4

                //WaitDSRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 6 page for max transfer buffer
                memset((U8 *)tcgTmpBuf, 0x00, LaaCnt * LAA_LEN /*sizeof(tcgTmpBuf)*/);
                if(LaaOffAddr || ((setColNo+tLen) % LAA_LEN))
                {
                #if 1
                    U16 ii;
                    bool HasBlank = FALSE;

                    for(ii = LaaAddr; ii < LaaAddr+LaaCnt; ii++)
                    {
                        if((pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START+ii].blk) >= (TCG_MBR_CELLS))
                        {
                            HasBlank = TRUE;
                            break;
                        }
                    }
                    if (HasBlank)
                    {
                        for(ii = LaaAddr; ii < LaaAddr + LaaCnt; ii++)
                        {
                            if((pG5->b.TcgTempMbrL2P[TCG_DS_LAA_START+ii].blk) < (TCG_MBR_CELLS) ||
                               (pG4->b.TcgMbrL2P[TCG_DS_LAA_START+ii].blk) < (TCG_MBR_CELLS))   //blank ?
                            {
                                TcgFuncRequest2(MSG_TCG_DSRD, ii, ii+1, (U8 *)tcgTmpBuf + (ii-LaaAddr)*CFG_UDATA_PER_PAGE);    //read 1 page
                            }
                        }
                    }
                    else
                    {
                        TcgFuncRequest2(MSG_TCG_DSRD, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);   // read all
                    }
                #else
                    TcgFuncRequest2(MSG_TCG_DSRD, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);    //WaitDSRd(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);  //read 1 page
                #endif
                }
                //memcpy((U8 *)&Desptr[LaaOffAddr], &tBuf[0], tLen);
                //TcgFuncRequest2(MSG_TCG_DSWR, LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);    //WaitDSWr(LaaAddr, LaaAddr+LaaCnt, (U8 *)tcgTmpBuf);
                DS_Write(LaaAddr, LaaAddr + LaaCnt, tcgTmpBuf, LaaOffAddr, tLen, tBuf);
                flgs_MChnged.b.DS = TRUE;
            }
            #endif // #if 1
        }
        break;  // case 0x00001009:

        case UID_TPerInfo >> 32:  // @AdminSP
            //if (invokingUID.all != 0x0000020100030001)
            //{
            //    errCode = 0x20;
            //    goto Exit_Write2Mtable;
            //}

            switch(setColNo)
            {
                case 0x08:  //ProgrammaticResetEnable
                    // DBG_P(2, 3, 0x8201A7, 1, tBuf[0]);  //82 01 A7, "TPerInfo_Wr_PReset[%X]", 1
                    pG1->b.mAdmTPerInfo_Tbl.val[0].preset = tBuf[0];
                    flgs_MChnged.b.G1 = mTRUE;
                    break;
                default:
                    errCode = 0xF0;
                    goto Exit_Write2Mtable;
            }
            break;
        #if _TCG_ == TCG_PYRITE
        case UID_RemovalMechanism >> 32:
            sDBG2(setColNo, tBuf[0], UID_RemovalMechanism);
            switch(setColNo)
            {
                case 0x01: // data_removal_mechanism
                    if(tBuf[0] > 5){  // out of definition of spec.
                        errCode = 0x61;
                        goto Exit_Write2Mtable;
                    }

                    U32 set_act_rm_msm = 1 << tBuf[0];
                    if(set_act_rm_msm & SupportDataRemovalMechanism){
                        if(set_act_rm_msm != pG1->b.mAdmRemovalMsm_Tbl.val[0].activeRM){
                            pG1->b.mAdmRemovalMsm_Tbl.val[0].activeRM = (U32)tBuf[0];
                            flgs_MChnged.b.G1 = TRUE;
                        }

                    }else{
                        errCode = 0x63;
                        goto Exit_Write2Mtable;
                    }
                    break;

                default:
                    errCode = 0x64;
                    goto Exit_Write2Mtable;
            }
            break;
        #endif

        default:
            errCode = 0xF1;
            goto Exit_Write2Mtable;
    }
    return zOK;

Exit_Write2Mtable:
    TCGPRN("Write2Mtable() errCode|%x\n", errCode);
    DBG_P(0x2, 0x03, 0x710063, 4, errCode);  // Write2Mtable() errCode|%x
    // DBG_P(2, 3, 0x8201A8, 1, errCode);  //82 01 A8, "Write2Mtable() ErrCode = %02X", 1
    return zNG;
}

tcg_code void WriteMtable2NAND(req_t *req)
{
    // DBG_P(1, 3, 0x820125);  //82 01 25, "[F]WriteMtable2NAND"
    if (mSessionManager.Write != 0x00){   // check write bit in start session payload
        if(flgs_MChnged.b.G1){
            TcgFuncRequest1(MSG_TCG_G1WR);   //WaitG1Wr();
        }
        if(flgs_MChnged.b.G2){
            TcgFuncRequest1(MSG_TCG_G2WR);   //WaitG2Wr();
        }

        if(flgs_MChnged.b.G3){
            TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();
            //LockingRangeTable_Update();  // move to other place
        }
        if(flgs_MChnged.b.SMBR){
            TcgFuncRequest1(MSG_TCG_SMBRCOMMIT);   //WaitMbrCommit();
        }
        if(flgs_MChnged.b.DS){
            TcgFuncRequest1(MSG_TCG_DSCOMMIT);   //WaitDSCommit();
        }
    }
}
#if 0
tcg_code void wait_be_done(void)
{
    req_t *req;
    while (1){
        if(IPC_MsgQFastPeek(cC2H_IpcQueue)){
            req = (req_t*)IPC_GetReqQ(cC2H_IpcQueue);

            if (req->opcode != cMcTcg){
                TCG_ERR_PRN("Error!!, It is not cMcTcg.");
                DBG_P(0x01, 0x03, 0x7F7F14);  // Error!!, It is not cMcTcg.
                ASSERT(0);
            }
            break;
        }
    }
}
#endif
tcg_code bool cb_tcg_tbl_recovery_complete(req_t *req)
{
    printk("cb_tcg_tbl_recovery_complete() is_cb_executed[%08x]\n", is_cb_executed);
    DBG_P(0x2, 0x03, 0x710122, 4, is_cb_executed);  // cb_tcg_tbl_recovery_complete() is_cb_executed[%08x]
    req->completion = nvmet_core_cmd_done;
    if(is_cb_executed){   // if FASLE then put DTAG & free MEM at NvmeCmd_security_send_XferDone()
        nvmet_evt_cmd_done(req);
    }
    return TRUE;
}

tcg_code void tcg_tbl_recovery(req_t *req)
{
    printk("tcg_tbl_recovery() flgs_MChnged[%08x]\n", flgs_MChnged.all32);
    DBG_P(0x2, 0x03, 0x710123, 4, flgs_MChnged.all32);  // tcg_tbl_recovery() flgs_MChnged[%08x]
    req->op_fields.TCG.subOpCode = MSG_TCG_TBL_RECOVERY;
    req->op_fields.TCG.param[0]  = flgs_MChnged.all32;
    req->completion = cb_tcg_tbl_recovery_complete;
    tcg_ipc_launch(req);
}
#if 0
tcg_code void ReadNAND2Mtable(req_t *req)
{
    //printk("ReadNAND2Mtable()\n");
    DBG_P(0x01, 0x03, 0x710112 );  // ReadNAND2Mtable()

#if 1
    if(flgs_MChnged.b.G1){
        req->op_fields.TCG.subOpCode = MSG_TCG_G1RD;
        tcg_ipc_launch(req);
        wait_be_done();
    }
    if(flgs_MChnged.b.G2){
        req->op_fields.TCG.subOpCode = MSG_TCG_G2RD;
        tcg_ipc_launch(req);
        wait_be_done();
    }
    if(flgs_MChnged.b.G3){
        req->op_fields.TCG.subOpCode = MSG_TCG_G3RD;
        tcg_ipc_launch(req);
        wait_be_done();
    }
    if(flgs_MChnged.b.SMBR){
        req->op_fields.TCG.subOpCode = MSG_TCG_TSMBRCLEAR;
        tcg_ipc_launch(req);
        wait_be_done();
    }
    if(flgs_MChnged.b.DS){
        req->op_fields.TCG.subOpCode = MSG_TCG_TDSCLEAR;
        tcg_ipc_launch(req);
        wait_be_done();
    }
#else
    req->op_fields.TCG.subOpCode = MSG_TCG_G1RD;
    tcg_ipc_launch(req);
    wait_be_done();

    req->op_fields.TCG.subOpCode = MSG_TCG_G2RD;
    tcg_ipc_launch(req);
    wait_be_done();

    req->op_fields.TCG.subOpCode = MSG_TCG_G3RD;
    tcg_ipc_launch(req);
    wait_be_done();

    req->op_fields.TCG.subOpCode = MSG_TCG_TSMBRCLEAR;
    tcg_ipc_launch(req);
    wait_be_done();

    req->op_fields.TCG.subOpCode = MSG_TCG_TDSCLEAR;
    tcg_ipc_launch(req);
    wait_be_done();
#endif
    //printk("ReadNAND2Mtable() done\n");
    DBG_P(0x01, 0x03, 0x710113 );  // ReadNAND2Mtable() done
}
#endif
tcg_code void ClearMtableChangedFlag(void)
{
    flgs_MChnged.b.G1    = FALSE;
    flgs_MChnged.b.G2    = FALSE;
    flgs_MChnged.b.G3    = FALSE;
    flgs_MChnged.b.SMBR  = FALSE;
    flgs_MChnged.b.DS    = FALSE;
#if CO_SUPPORT_AES
    mRawKeyUpdateList = 0;
#endif
}

tcg_code int CPinMsidCompare(U8 cpinIdx)
{
    U16 i;
    int result = zOK;

    if(pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_Tag == CPIN_IN_PBKDF)
    {
        //printk("Encrypted Cpin compare ");
        DBG_P(0x01, 0x03, 0x710064 );  // Encrypted Cpin compare
        U8 digest[CPIN_LENGTH] = { 0 };

        HAL_PBKDF2((U32*)pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val,
                    CPIN_MSID_LEN,
                    (U32*)pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_salt,
                    sizeof(pG1->b.mAdmCPin_Tbl.val[0].cPin.cPin_salt),
                    (U32 *)digest);
        for (i = 0; i < CPIN_LENGTH; i++)
        {
            if (pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[i] != digest[i])
            {
                result = zNG;
            }
        }
    }
    else
    {
        //printk("Unencrypted Cpin compare ");
        DBG_P(0x01, 0x03, 0x710067 );  // Unencrypted Cpin compare
        for (i = 0; i < CPIN_LENGTH; i++)
        {
            if (pG1->b.mAdmCPin_Tbl.val[cpinIdx].cPin.cPin_val[i] != pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val[i])
            {
                result = zNG;
            }
        }
    }

    if (result == zNG)
    {
        TCG_ERR_PRN("NG \n");
        DBG_P(0x01, 0x03, 0x7F7F04 );  // NG
    }
    else
    {
        //printk("pass \n");
        DBG_P(0x01, 0x03, 0x720058 );  // pass
    }
    return result;
}

/***********************************************************
* Admin table getACL method
* ref. core spec 5.3.3.13
***********************************************************/
tcg_code U16 Method_GetACL(req_t *req)
{
    int zSTS = STS_SUCCESS;
    U16 j;
    // U8 found = 0;
    U8 errCode = 0x00;  //no error
    bool invIdIsFound = FALSE;

    // DBG_P(1, 3, 0x820127);  //82 01 27, "[F]Method_GetACL"
    if (invokingUID.all != UID_AccessControl){
        zSTS = STS_NOT_AUTHORIZED; errCode = 0x01;  goto exit_Method_GetACL;
    }

    if (ChkToken() != TOK_StartList) {
        zSTS = STS_SESSION_ABORT; errCode = 0x08; goto exit_Method_GetACL;
    }
    if (AtomDecoding_Uid2(invokingUID.bytes) != STS_SUCCESS)
    {
        zSTS = STS_INVALID_PARAMETER;
        errCode = 0x11; goto exit_Method_GetACL;
    }
    if (AtomDecoding_Uid2(methodUID.bytes) != STS_SUCCESS)
    {
        zSTS = STS_INVALID_PARAMETER;
        errCode = 0x21; goto exit_Method_GetACL;
    }

    if (ChkToken() != TOK_EndList) {
        zSTS = STS_SESSION_ABORT; errCode = 0x23; goto exit_Method_GetACL;
    }
    if (ChkToken() != TOK_EndOfData) {
        zSTS = STS_SESSION_ABORT; errCode = 0x24; goto exit_Method_GetACL;
    }

    //status list check
    zSTS = chk_method_status();
    if(zSTS != STS_SUCCESS)
    {
        errCode = 0x70;
        goto exit_Method_GetACL;
    }

    // DBG_P(5, 3, 0x8201A9, 4, (U32)(invokingUID.dw[1]),  //82 01 A9, "Method getACL => acl_Inv:acl_Mtd %08X-%08X : %08X-%08X", 4 4 4 4
                          // 4, (U32)invokingUID.dw[0],
                          // 4, (U32)(methodUID.dw[1]),
                          // 4, (U32)methodUID.dw[0]);

    if (tcg_access_control_check(&invIdIsFound) == zNG)
    {
        zSTS = STS_NOT_AUTHORIZED;
        errCode = 0x31;
        goto exit_Method_GetACL;
    }

    zSTS = (mSessionManager.SPID.all == UID_SP_Admin) ? admin_aceBooleanExpr_chk(FALSE) : locking_aceBooleanExpr_chk(FALSE);
    if (zSTS == zNG)
    {
        errCode = 0x32;
        goto exit_Method_GetACL;
    }

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_StartList;

    for (j = 0; j<ACCESSCTRL_ACL_CNT; j++)
    {
        if (aclBackup[j].aclUid == UID_Null)
            break;

        AtomEncoding_Int2Byte((U8*)&aclBackup[j].aclUid, sizeof(U64));
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;
    set_status_code(0, 0, 0);

exit_Method_GetACL:
    TCGPRN("Method_GetACL() errCode|%x\n", errCode);
    DBG_P(0x2, 0x03, 0x71006A, 4, errCode);  // Method_GetACL() errCode|%x
    // DBG_P(2, 3, 0x8201AA, 1, errCode);  //82 01 AA, "Method_GetACL, err_code = %X", 1
    return zSTS;
}

/*****************************************************
 * Method Activate
 *****************************************************/
tcg_code bool cb_activate(Complete)(req_t *req)
{
    TCGPRN("cb_activate_Complete()\n");
    DBG_P(0x01, 0x03, 0x71006B );  // cb_activate_Complete()
    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_activate(G1Wr)(req_t *req)
{
    TCGPRN("cb_activate_G1Wr()\n");
    DBG_P(0x01, 0x03, 0x71006C );  // cb_activate_G1Wr()
    pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured;
    tcg_ipc_post(req, MSG_TCG_G1WR, cb_activate(Complete));
    mTcgStatus |= TCG_ACTIVATED;
    return TRUE;
}

#if 0
tcg_code bool cb_activate(G3Wr)(req_t *req)
{
    TCGPRN("cb_activate_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x71006D );  // cb_activate_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_activate(G1Wr));
    return TRUE;
}

tcg_code bool cb_activate(syncZone51)(req_t *req)
{
    TCGPRN("cb_activate_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71006E );  // cb_activate_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_activate(G3Wr));
    return TRUE;
}

tcg_code bool cb_activate(G2Wr)(req_t *req)
{
    TCGPRN("cb_activate_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x71006F );  // cb_activate_G2Wr()
    #if _TCG_ != TCG_PYRITE
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_activate(syncZone51));
    #else
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_activate(G3Wr));
    #endif
    return TRUE;
}
#else
tcg_code bool cb_activate(G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_activate_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x710129 );  // cb_activate_G3Wr_syncZone51()

    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_activate(G1Wr));
    return TRUE;
}


tcg_code bool cb_activate(G2Wr)(req_t *req)
{
    TCGPRN("cb_activate_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x71006F );  // cb_activate_G2Wr()
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_activate(G3Wr_syncZone51));
    return TRUE;
}
#endif

tcg_code bool cb_activate(Begin)(req_t *req)
{
    TCGPRN("cb_activate_Begin()\n");
    DBG_P(0x01, 0x03, 0x710070 );  // cb_activate_Begin()
    cb_activate(G2Wr)(req);
    return TRUE;
}

//-------------------Activate-------------------------
tcg_code U16 Method_Activate(req_t *req)
{
#if _TCG_ != TCG_PYRITE
    U64 tmp64;
    U32 nameValue, tmp32, DSTblSize[DSTBL_MAX_NUM];
    U32 totalsize = 0;
    U16 sgUserRange = 0;

    U8  sgUserCnt = 0, sgUserPolicy = 1;    //admin has ownership
    U8  i;
#endif
    U16 result = 0;
    U8  tmp8, cnt = 0, errCode = 0;

    TCGPRN("Method_Activate()\n");
    DBG_P(0x01, 0x03, 0x710071 );  // Method_Activate()
    // DBG_P(1, 3, 0x820128);  //82 01 28, "[F]Method_Activate"
    //cj removed: redundent? [
    //if(mSessionManager.HtSgnAuthority.all==UID_Authority_PSID)
    //    return STS_NOT_AUTHORIZED;
    //]
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
    {
        errCode = 0x10;  result = STS_SESSION_ABORT;  goto MTD_EXIT;
    }

    //retrieve parameter 'DataStoreTblSizes'
    tmp8 = ChkToken();
#if _TCG_ != TCG_PYRITE    //SingleUserMode and Additional DataStore
    if (tmp8 == TOK_StartName)
    {
        if (invokingUID.all != UID_SP_Locking)   //cj added for DM test
        {
            errCode = 0x50;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
        }

        if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
        {
            errCode = 0x20;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
        }

        if (nameValue == 0x060000) //'SingleUserSelectionList'
        {
            if (ChkToken() == TOK_StartList)
            {   // Locking table objects list
                tmp8 = 0;
                while (ChkToken() != TOK_EndList)
                {
                    iPload--;

                    if (AtomDecoding_Uid2((U8 *)&tmp64) != STS_SUCCESS)
                    {
                        errCode = 0x30;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
                    }

                    //check if it is in the locking table
                    for (i = tmp8; i < pG3->b.mLckLocking_Tbl.hdr.rowCnt; i++)
                    {
                        if (pG3->b.mLckLocking_Tbl.val[i].uid == tmp64)
                        {
                            //mSgUser.range[mSgUser.cnt]=i;
                            sgUserRange |= (0x01 << i);
                            sgUserCnt++;
                            tmp8 = i;
                            // DBG_P(3, 3, 0x8201AB, 1, sgUserCnt, 1, i);  //82 01 AB, "Locking obj sgUserCnt[%X], i[%02X]", 1 1
                            break;
                        }
                    }

                    if (i >= pG3->b.mLckLocking_Tbl.hdr.rowCnt)
                    {
                        if ((pG3->b.mLckLocking_Tbl.val[0].uid == tmp64)
                            && ((sgUserRange & 0x01) == 0))
                        {
                            TCG_PRINTF("* %08X\n", (U32)tmp64);
                            // DBG_P(2, 3, 0x82021F, 4, (U32)tmp64); // 82 02 1F, "* %08X" 4
                            sgUserRange |= 0x01;
                            sgUserCnt++;
                            tmp8 = i;
                        }
                        else
                        {
                            errCode = 0x31;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                        }
                    }
                }
            }
            else
            { // check if entire Locking table
                iPload--;
                if (AtomDecoding_Uid2((U8 *)&tmp64) != STS_SUCCESS)
                {
                    errCode = 0x32;  result = STS_STAY_IN_IF_SEND;    goto MTD_EXIT;
                }

                if (tmp64 != UID_Locking)
                {
                    errCode = 0x33;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                sgUserCnt = LOCKING_RANGE_CNT + 1;
                sgUserRange = 0xffff;   //EntireLocking
                // DBG_P(2, 3, 0x8201AC, 1, sgUserCnt);  //82 01 AC, "Locking entire sgUserCnt[%X]", 1
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x34;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x35;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        if (sgUserCnt)
            sgUserPolicy = 0;   //User has ownership

        if (nameValue == 0x060001) //'RangePolicy'
        {
            if (AtomDecoding_Uint(&tmp8, sizeof(tmp8)) != STS_SUCCESS)
            {
                errCode = 0x38;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (tmp8 == 0)
            {
                if (sgUserCnt != 0) sgUserPolicy = 0;
            }
            else if (tmp8 == 1)
                sgUserPolicy = 1;
            else
            {
                errCode = 0x39;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
            // DBG_P(2, 3, 0x8201AD, 1, sgUserPolicy);  //82 01 AD, "sgUserPolicy[%X]", 1

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x3A;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x3B;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        //check Additional DataStore Parameter
        if (nameValue == 0x060002) //'DataStoreTblSizes'
        {
            if (ChkToken() != TOK_StartList)
            {
                errCode = 0x21;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            while (ChkToken() != TOK_EndList)
            {
                iPload--;

                if (AtomDecoding_Uint((U8*)&tmp32, sizeof(tmp32)) != STS_SUCCESS)
                {
                    errCode = 0x22;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                if (cnt >= DSTBL_MAX_NUM)      //too many tables
                {
                    errCode = 0x23;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                if (tmp32 > DATASTORE_LEN)
                {
                    errCode = 0x24;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                totalsize += tmp32;
                if (totalsize>DATASTORE_LEN) //size is too large
                {
                    errCode = 0x24;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                if (tmp32%DSTBL_ALIGNMENT)   //not aligned
                {
                    // DBG_P(2, 3, 0x8201AE, 4, tmp32);  //82 01 AE, "not aligned: %X", 4
                    errCode = 0x25;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                DSTblSize[cnt] = tmp32;
                // DBG_P(3, 3, 0x8201B0, 1, cnt, 4, tmp32);  //82 01 B0, "cnt= %X, DSTblSize= %X", 1 4
                cnt++;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x26;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            tmp8 = ChkToken();
        }
        else
        {
            errCode = 0x27;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
        }
    }
CHK_ENDLIST:
#endif

    if (tmp8 != TOK_EndList)      //test cases 3.1.5
    {
        errCode = 0x15;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
    {
        errCode = 0x16;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
    {
        errCode = 0x17;  goto MTD_EXIT;
    }

#if (CO_SUPPORT_ATA_SECURITY == TRUE)
    // Opal 5.2.1: return fail with a status FAIL if ATA Security is enabled
    if (AatSecuriytActivated())
    {
        fill_no_data_token_list();
        //return STS_FAIL;
        {    errCode = 0x18;  result=STS_FAIL;    goto MTD_EXIT; }
    }
    else
#endif
    {
//        if((invokingUID.all==UID_SP_Locking) &&
        if (pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured_inactive)
        {
            //TODO:
            //  1. bit 1 of word 82, bit 1 of word 85 and all bits of word 89, 90, 92, 128 in the IDENTIFY DEVICE
            //     data SHALL be set to all-0.
            //  2. LockingEnabled bit in Locking Feature Descriptor in the Level 0 SHALL be set to 1. (v)
            //  3. LifeCycleState of Locking SP object in the SP table SHALL be set to 0x09. (v)
            //  4. A startup of a session to the Locking SP can succeed. (v)
            //  5. PIN for Admin1 in LockingSP should be set to SID PIN (Application Note)
            //  6. update table to NAND

            // copy SID pin to Admin pin
            memcpy((U8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin, (U8 *)&pG1->b.mAdmCPin_Tbl.val[CPIN_SID_IDX].cPin, sizeof(pG3->b.mLckCPin_Tbl.val[0].cPin));
#if _TCG_ != TCG_PYRITE
            mSgUser.cnt = sgUserCnt;
            mSgUser.policy = sgUserPolicy;
            mSgUser.range = sgUserRange;

            DataStore_Setting(cnt, DSTblSize);
            SingleUser_Setting();

            //cjdbg, todo: genkey according to SingleUser count...
            for (i = 1; i <= LOCKING_RANGE_CNT; i++)
                TcgChangeKey(i);
#endif
#if CO_SUPPORT_AES
            if (TCG_ACT_IN_OPAL())
            {
                //WrapKEK is a KEK used to wrap DEK. WrapKEK should be wrapped by some KDF
                HAL_Gen_Key(WrapKEK, sizeof(WrapKEK));
#if _TCG_DEBUG
                WrapKEK[0] = 0x12345678;  // cjdbg: test only,
#endif
                TcgWrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], (U32)UID_Authority_Admin1, WrapKEK);
            }
#endif
            #if 1
            cb_activate(Begin)(req);
            #else
            TcgFuncRequest1(MSG_TCG_G2WR);   //WaitG2Wr();  //AccessCtrlTbl, cj: add LockingInfoTbl

            TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

            pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured;     // (3)
            TcgFuncRequest1(MSG_TCG_G1WR);   //WaitG1Wr();

            mTcgStatus |= TCG_ACTIVATED;
            #endif
        }
        else
        {
            errCode = 0xff;
        }
        fill_no_data_token_list();

        result = STS_SUCCESS;
    }

MTD_EXIT:
    TCGPRN("errCode|%x, cnt|%x, mSgUser.range|%x\n", errCode, cnt, mSgUser.range);
    DBG_P(0x4, 0x03, 0x710072, 4, errCode, 4, cnt, 4, mSgUser.range);  // errCode|%x, cnt|%x, mSgUser.range|%x
    // DBG_P(4, 3, 0x8201AF, 1,  errCode, 1, cnt, 2, mSgUser.range);  //82 01 AF, "M_Activate: Err=%02X, DSCnt=%02X, SURx=%04X", 1 1 2
    return result;
}

#if _TCG_ != TCG_PYRITE
tcg_code void SingleUser_Setting(void)
{
    U16 row, k = 0;
    U8 i, step;

    TCGPRN("SingleUser_Setting(), mSgUser.cnt|%x, mSgUser.policy|%x, mSgUser.range|%x\n", mSgUser.cnt, mSgUser.policy, mSgUser.range);
    DBG_P(0x4, 0x03, 0x710073, 4, mSgUser.cnt, 4, mSgUser.policy, 4, mSgUser.range);  // SingleUser_Setting(), mSgUser.cnt|%x, mSgUser.policy|%x, mSgUser.range|%x
    // DBG_P(4, 3, 0x820129, 1, mSgUser.cnt, 1, mSgUser.policy, 2, mSgUser.range);   //82 01 29, "[F]SingleUser_Setting cnt[%X] policy[%X} range[%X]", 1 1 2

    if (mSgUser.cnt != 0)
    {
        if (mSgUser.range & 0x01)
        { //GlobalRange -> SingerUser
            //1. update AccessCtrl table
            for (row = 0; row < sizeof(pG2->b.mLckAxsCtrl_Tbl.ace) / sizeof(sAxsCtrl_TblObj); row++)
            { // add ACE_Locking_GRange_Erase.Set() method
                if (pG2->b.mLckAxsCtrl_Tbl.ace[row].invID == UID_ACE_Locking_GRange_Erase)
                {
                    if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == ~UID_MethodID_Get)
                    {
                        pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Get;
                    }
                    else if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == ~UID_MethodID_Set)
                    {
                        pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Set;
                        break;
                    }
                }
            }

            for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.authority) / sizeof(sAxsCtrl_TblObj); row++)
            { // modify User1.Set() ACL
                if ((pG2->b.mLckAxsCtrl_Tbl.authority[row].invID == UID_Authority_User1) && (pG2->b.mLckAxsCtrl_Tbl.authority[row].mtdID == UID_MethodID_Set))
                {
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[0] = UID_ACE_User1_Set_CommonName;
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[1] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[2] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[3] = 0;
                    break;
                }
            }
            for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.cpin) / sizeof(sAxsCtrl_TblObj); row++)
            { // modify C_PIN_User1.Get() ACL
                if ((pG2->b.mLckAxsCtrl_Tbl.cpin[row].invID == UID_CPIN_User1) && (pG2->b.mLckAxsCtrl_Tbl.cpin[row].mtdID == UID_MethodID_Get))
                {
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[0] = UID_ACE_CPIN_Anybody_Get_NoPIN;
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[1] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[2] = 0;
                    pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[3] = 0;
                    break;
                }
            }
            for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.lcking) / sizeof(sAxsCtrl_TblObj); row++)
            {
                if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].invID == UID_Locking_GRange)
                { // modify Locking_GRange.Set() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == UID_MethodID_Set)
                    {
                        if (mSgUser.policy)
                        {
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_GRange_Set_ReadToLOR;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_Admins_Set_CommonName;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                        }
                        else
                        {
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_GRange_Set_ReadToLOR;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_User1_Set_CommonName;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                        }
                    }
                    else if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == (U64)(~UID_MethodID_Erase))
                    { // add GRange.Erase
                        pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID = UID_MethodID_Erase;
                        break;
                    }
                }
            }

            //2. update ACE table, need to follow the row sequence...
            step = 0;
            for (row = 0; row < pG3->b.mLckACE_Tbl.hdr.rowCnt; row++)
            { //modify
                if (step == 0)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_C_PIN_User1_Set_PIN)
                    {
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        step++;
                    }
                }
                else if (step == 1)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_K_AES_256_GlobalRange_GenKey)
                    {
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        step++;
                    }
                }
                else if (step == 2)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_Locking_GRange_Get_RangeStartToActiveKey)
                    {
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_Anybody;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        step++;
                    }
                }
                else if (step == 3)
                {
                    if ((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)UID_ACE_Locking_GRange_Set_ReadToLOR)
                    { //add
                        pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_GRange_Set_ReadToLOR;
                        step++;
                    }
                }
                else if (step == 4)
                {
                    if ((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)UID_ACE_CPIN_Anybody_Get_NoPIN)
                    { //add
                        pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_CPIN_Anybody_Get_NoPIN;
                        step++;
                    }
                }
                else if (step == 5)
                {
                    if((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)UID_ACE_Locking_GRange_Erase)
                    {
                        pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_GRange_Erase;
                        step++;
                    }
                }
                else if (step == 6)
                {
                    if (pG3->b.mLckACE_Tbl.val[row].uid == UID_ACE_User1_Set_CommonName)
                    {
                        pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1;
                        for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                        break;    //step++;
                    }
                }
            }

            //3. update Authority table
            for (row = 0; row<pG3->b.mLckAuthority_Tbl.hdr.rowCnt; row++)
            {
                if (pG3->b.mLckAuthority_Tbl.val[row].uid == UID_Authority_User1)
                {
                    pG3->b.mLckAuthority_Tbl.val[row].enabled = 1;
                    break;
                }
            }
        }

        // for Range 1~8
        for (i = 1; i <= LOCKING_RANGE_CNT; i++)
        {
            if (mSgUser.range&(0x01 << i))
            { //RangeN -> SingerUser
                //1. update AccessCtrl table
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.ace) / sizeof(sAxsCtrl_TblObj); row++)
                { // add ACE_Locking_GRange_Erase.Set() method
                    if (pG2->b.mLckAxsCtrl_Tbl.ace[row].invID == (U64)(UID_ACE_Locking_Range1_Erase + i - 1))
                    {
                        if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == (U64)(~UID_MethodID_Get))
                        {
                            pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Get;
                        }
                        else if (pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID == (U64)(~UID_MethodID_Set))
                        {
                            pG2->b.mLckAxsCtrl_Tbl.ace[row].mtdID = UID_MethodID_Set;
                            break;
                        }
                    }
                }
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.authority) / sizeof(sAxsCtrl_TblObj); row++)
                { //modify UserN+1.Set() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.authority[row].invID == (U64)(UID_Authority_User1 + i))
                    {
                        if (pG2->b.mLckAxsCtrl_Tbl.authority[row].mtdID == UID_MethodID_Set)
                        {
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[0] = UID_ACE_User1_Set_CommonName + i;
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[1] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.authority[row].acl[3] = 0;
                            break;
                        }
                    }
                }
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.cpin) / sizeof(sAxsCtrl_TblObj); row++)
                { // modify C_PIN_UserN+1.Get() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.cpin[row].invID == (U64)(UID_CPIN_User1 + i))
                    {
                        if (pG2->b.mLckAxsCtrl_Tbl.cpin[row].mtdID == UID_MethodID_Get)
                        {
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[0] = UID_ACE_CPIN_Anybody_Get_NoPIN;
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[1] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[2] = 0;
                            pG2->b.mLckAxsCtrl_Tbl.cpin[row].acl[3] = 0;
                            break;
                        }
                    }
                }
                for (row = 0; row<sizeof(pG2->b.mLckAxsCtrl_Tbl.lcking) / sizeof(sAxsCtrl_TblObj); row++)
                { // modify RangeN.Set() ACL
                    if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].invID == (U64)(UID_Locking_Range + i))
                    {
                        if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == UID_MethodID_Set)
                        {
                            if (mSgUser.policy)
                            {
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_Locking_Range1_Set_Range + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = UID_ACE_Admins_Set_CommonName;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                            }
                            else
                            {
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[0] = UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[1] = UID_ACE_Locking_Range1_Set_Range + i - 1;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[2] = UID_ACE_User1_Set_CommonName + i;
                                pG2->b.mLckAxsCtrl_Tbl.lcking[row].acl[3] = 0;
                            }
                        }
                        else if (pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID == (U64)(~UID_MethodID_Erase))
                        { // add RangeN Erase method
                            pG2->b.mLckAxsCtrl_Tbl.lcking[row].mtdID = UID_MethodID_Erase;
                            break;
                        }
                    }
                }

                //2. update ACE table
                step = 0;
                for (row = 0; row<pG3->b.mLckACE_Tbl.hdr.rowCnt; row++)
                { //modify
                    if (step == 0)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (U64)(UID_ACE_C_PIN_User1_Set_PIN + i))
                        {
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                        for(k=1;k<LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 1)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (U64)(UID_ACE_K_AES_256_Range1_GenKey + i - 1))
                        {
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 2)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (U64)(UID_ACE_Locking_Range1_Get_RangeStartToActiveKey + i - 1))
                        {
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_Anybody;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 3)
                    {
                        if ((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)(UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1))
                        { //add
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_Range1_Set_ReadToLOR + i - 1;
                            step++;
                        }
                    }
                    else if (step == 4)
                    {
                        if ((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)(UID_ACE_Locking_Range1_Set_Range + i - 1))
                        { //add
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_Range1_Set_Range + i - 1;
                            if (mSgUser.policy)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_Admins;
                            else
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            step++;
                        }
                    }
                    else if (step == 5)
                    {
                        if ((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)UID_ACE_CPIN_Anybody_Get_NoPIN)
                        { //add
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_CPIN_Anybody_Get_NoPIN;
                            step++;
                        }
                    }
                    else if (step == 6)
                    {
                        if ((U32)pG3->b.mLckACE_Tbl.val[row].uid == (U32)(UID_ACE_Locking_Range1_Erase + i - 1))
                        { //add
                            pG3->b.mLckACE_Tbl.val[row].uid = UID_ACE_Locking_Range1_Erase + i - 1;
                            step++;
                        }
                    }
                    else if (step == 7)
                    {
                        if (pG3->b.mLckACE_Tbl.val[row].uid == (U64)(UID_ACE_User1_Set_CommonName + i))
                        {
                            pG3->b.mLckACE_Tbl.val[row].booleanExpr[0] = UID_Authority_User1 + i;
                            for (k = 1; k < LCK_ACE_BOOLEXPR_CNT; k++)
                                pG3->b.mLckACE_Tbl.val[row].booleanExpr[k] = 0;
                            break;    //step++;
                        }
                    }
                }

                //3. update Authority table
                for (row = 0; row<pG3->b.mLckAuthority_Tbl.hdr.rowCnt; row++)
                {
                    if (pG3->b.mLckAuthority_Tbl.val[row].uid == (U64)(UID_Authority_User1 + i))
                    {
                        pG3->b.mLckAuthority_Tbl.val[row].enabled = 1;
                        break;
                    }
                }
            }
        }

        //update LockingInfo table
        memset(pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange, 0, sizeof(pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange));

        if (mSgUser.range == 0xffff) // EntireLocking
            pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[0] = UID_Locking;
        else
        {
            row = 0;
            if(mSgUser.range&0x01)
                pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[row++] = UID_Locking_GRange;

            for (i = 1; i<8; i++)
            {
                if(mSgUser.range&(0x01<<i))
                    pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[row++] = UID_Locking_Range + i;
            }
        }

        if(mSgUser.policy)
            pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy = 1;
        else
            pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy = 0;
    }
}

tcg_code void DataStore_Setting(U8 cnt, U32 * DSTblSize)
{
    U32 tmp32;
    U16 j, k = 0;
    U8 i, tmp8;

    // DBG_P(2, 3, 0x82012A, 2, cnt);    //82 01 2A, "[F]DataStore_Setting cnt[%X]", 1
    if (cnt)
    { //with additional DataStore
        //1. update TableTbl, some table objects are enabled
        for (i = 0; i<pG2->b.mLckTbl_Tbl.hdr.rowCnt; i++)
        {
            tmp32 = ((U32)pG2->b.mLckTbl_Tbl.val[i].uid) & 0xfffffff0;
            if(tmp32==0x1000)
            { //DataStore
                tmp8 = ((U8)pG2->b.mLckTbl_Tbl.val[i].uid) - 1;

                if (tmp8<cnt)
                {
                    pG2->b.mLckTbl_Tbl.val[i].uid = UID_Table_DataStore + tmp8;
                    pG2->b.mLckTbl_Tbl.val[i].rows = DSTblSize[tmp8];
                }
            }
        }

        if(cnt>1)
        {
            //2. update AccessCtrlTbl,  need to enable some rows, no need to update row count.
            for (j = 0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.table) / sizeof(sAxsCtrl_TblObj); j++)
            { // Table: DataStoreObj.Get
                if (pG2->b.mLckAxsCtrl_Tbl.table[j].invID == UID_Table_DataStore2)
                {
                    for (k = j; k<(j + cnt - 1); k++)
                    {
                        if (U64_TO_U32_H(pG2->b.mLckAxsCtrl_Tbl.table[k].mtdID) != U64_TO_U32_H(UID_MethodID))
                            pG2->b.mLckAxsCtrl_Tbl.table[k].mtdID = ~pG2->b.mLckAxsCtrl_Tbl.table[k].mtdID;
                    }
                    break;
                }
            }
            for (j = 0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.ace) / sizeof(sAxsCtrl_TblObj); j++)
            { //ACE_DataStore2_Get_All.Get / ACE_DataStore2_Get_All.Set / ACE_DataStore2_Set_All.Get / ACE_DataStore2_Set_All.Set
                if (pG2->b.mLckAxsCtrl_Tbl.ace[j].invID == UID_ACE_DataStore2_Get_All)
                {
                    for (k = j; k<(j + (cnt - 1) * 4); k++)
                    {
                        if (U64_TO_U32_H(pG2->b.mLckAxsCtrl_Tbl.ace[k].mtdID) != U64_TO_U32_H(UID_MethodID))
                            pG2->b.mLckAxsCtrl_Tbl.ace[k].mtdID = ~pG2->b.mLckAxsCtrl_Tbl.ace[k].mtdID;
                    }
                    break;
                }
            }
            for (j = 0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.datastore) / sizeof(sAxsCtrl_TblObj); j++)
            { // DataStore2.Get / DataStore2.Set
                if (pG2->b.mLckAxsCtrl_Tbl.datastore[j].invID == UID_DataStore2)
                {
                    for (k = j; k<(j + (cnt - 1) * 2); k++)
                    {
                        if (U64_TO_U32_H(pG2->b.mLckAxsCtrl_Tbl.datastore[k].mtdID) != U64_TO_U32_H(UID_MethodID))
                            pG2->b.mLckAxsCtrl_Tbl.datastore[k].mtdID = ~pG2->b.mLckAxsCtrl_Tbl.datastore[k].mtdID;
                    }
                    break;
                }
            }

            //3. update AceTbl, some ACE objects are enabled
            tmp8 = cnt*2;
            for (i = 0; i<pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
            {
                tmp32 = (U32)pG3->b.mLckACE_Tbl.val[i].uid;
                if ((tmp32>(U32)0x03FC01) && tmp32<(U32)(0x03FC00 + cnt * 2))
                { //ACE_DataStoreX_Get_All / ACE_DataStoreX_Set_All
                    pG3->b.mLckACE_Tbl.val[i].uid = UID_ACE | (U64)tmp32 ;
                }
            }
        }

        DataStoreAddr_Update();
    }
}
#endif // #if _TCG_ != TCG_PYRITE

tcg_code void DataStoreAddr_Update(void)
{
    U32 offset = 0;
    U8  i = 0, tblRow;

    // DBG_P(1, 3, 0x82012B);  //82 01 2B, "[F]DataStoreAddr_Update"
    for (tblRow = 0; tblRow<pG2->b.mLckTbl_Tbl.hdr.rowCnt; tblRow++)
    {
        if (pG2->b.mLckTbl_Tbl.val[tblRow].uid == UID_Table_DataStore)
            break;
    }

    mDataStoreAddr[0].offset= 0;
    mDataStoreAddr[0].length = pG2->b.mLckTbl_Tbl.val[tblRow].rows;
    TCG_PRINTF("DS00: %08X %08X\n", mDataStoreAddr[0].offset, mDataStoreAddr[0].length);
    TCGPRN("DS00 : mDataStoreAddr[0].offset|%x, length|%x\n", mDataStoreAddr[0].offset, mDataStoreAddr[0].length);
    DBG_P(0x3, 0x03, 0x710074, 4, mDataStoreAddr[0].offset, 4, mDataStoreAddr[0].length);  // DS00 : mDataStoreAddr[0].offset|%x, length|%x
    // DBG_P(3, 3, 0x820224, 4, mDataStoreAddr[0].offset, 4, mDataStoreAddr[0].length);  //82 02 20, "DS00: %08X %08X", 4 4

    for (i = 1; i<DSTBL_MAX_NUM; i++)
    {
        offset += pG2->b.mLckTbl_Tbl.val[tblRow + i - 1].rows;

        mDataStoreAddr[i].offset=offset;
        mDataStoreAddr[i].length = pG2->b.mLckTbl_Tbl.val[tblRow + i].rows;

        if (mDataStoreAddr[i].length)
        {
            // DBG_P(4, 3, 0x8201B2, 1, i, 4, mDataStoreAddr[i].offset, 4, mDataStoreAddr[i].length);  //82 01 B2, "DS%2X: off[%X] len[%X]", 1 4 4
            TCGPRN("i|%x, mDataStoreAddr[i].offset|%x, mDataStoreAddr[i].length|%x\n", i, mDataStoreAddr[i].offset, mDataStoreAddr[i].length);
            DBG_P(0x4, 0x03, 0x710075, 4, i, 4, mDataStoreAddr[i].offset, 4, mDataStoreAddr[i].length);  // i|%x, mDataStoreAddr[i].offset|%x, mDataStoreAddr[i].length|%x
        }
    }
}

tcg_code void SingleUser_Update(void)
{
    if ((mTcgStatus&TCG_ACTIVATED) == 0)
    {
        pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy = 1; // set default as 1
        mSgUser.range = 0;
        mSgUser.cnt = 0;
        return;
    }

    mSgUser.policy = pG2->b.mLckLockingInfo_Tbl.val[0].rangeStartLengthPolicy;
    if (pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[0] == UID_Locking)
    {
        // EntireLocking
        mSgUser.range = 0xFFFF;
        mSgUser.cnt = LOCKING_RANGE_CNT + 1;
    }
    else
    {
        U64 tmp64;
        U8 i, j;

        mSgUser.range = 0;
        mSgUser.cnt = 0;
        for (i = 0; i < (LOCKING_RANGE_CNT + 1); i++)
        {
            tmp64 = pG2->b.mLckLockingInfo_Tbl.val[0].singleUserModeRange[i];
            if(tmp64)
            {
                for (j = 0; j < (LOCKING_RANGE_CNT + 1); j++)
                {
                    if (tmp64 == pG3->b.mLckLocking_Tbl.val[j].uid)
                    {
                        mSgUser.range |= (0x01 << j);
                        mSgUser.cnt++;
                        break;
                    }
                }
            }
            else
            {
                break;
            }
        }
    }
}

tcg_code void CPU1_chg_cbc_tbl_key(void)
{
    HAL_Gen_Key(secretZone.cbcTbl.kek, sizeof(secretZone.cbcTbl.kek));

}

tcg_code void CPU1_chg_ebc_key_key(void)
{
    HAL_Gen_Key(secretZone.ebcKey.key, sizeof(secretZone.ebcKey.key));
}

tcg_code void CPU1_chg_cbc_fwImage_key(void)
{
    HAL_Gen_Key(secretZone.cbcFwImage.key, sizeof(secretZone.cbcFwImage.key));
}

#if CO_SUPPORT_AES
//Trim all blocks and do Block Erase at background
tcg_code void TrimAndBGC(void)
{
#ifndef alexcheck
    if (CurrentTrack.all){
        ReqCloseBlock = TRUE;
        WaitSeqencerReady();
    } //Yenheng

    ForceCloseEngSeqencer();
    TrimAllBlk();

    L2PBlockNeedToWrite.bits.Immd = TRUE;//,trig update L2P,Yenheng
    mPmData.CheckDriveState |= PM_L2P_BUSY;
    mPmData.CheckDriveState |= PM_GC_BUSY;
#endif
    return;
}

// Get KDF from Chanllege
tcg_code void TcgGetEdrvKEK(U8* pPwd, U8 len, U8 range, U32* pKEK)
{
    HAL_PBKDF2((U32 *)pPwd, (U32)len, pG3->b.mWKey[range].salt, sizeof(pG3->b.mWKey[0].salt), pKEK);
}

#define KW_OPAL_ICV1    0xA6A6A6A6
#define KW_OPAL_ICV2    0xA6A6A6A6
tcm_data U32 OpalKDF[8];

// Wrap "*pKEK" (OpalKEK or WrapKEK) by OpalKDF and stored in "pG3->b.mOpalWrapKEK[]"
tcg_code int TcgWrapOpalKEK(U8*chanllege, U8 len, U32 auth, U32* pKEK)
{
    U32 y;

    TCGPRN("<WrapOpalKEK> ");
    DBG_P(0x01, 0x03, 0x710076 );  // <WrapOpalKEK>

    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            break;
    }
    if (y == sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey))
    {
        TCG_ERR_PRN("!!NG\n");
        DBG_P(0x01, 0x03, 0x7F7F04 );  // !!NG
        return -1;
    }

    // Get_KeyWrap_KEK(chanllege, len, OpalKDF);
    HAL_Gen_Key(pG3->b.mOpalWrapKEK[y].salt, sizeof(pG3->b.mOpalWrapKEK[y].salt));      // generate new salt
    HAL_PBKDF2((U32 *)chanllege, len, pG3->b.mOpalWrapKEK[y].salt, sizeof(pG3->b.mOpalWrapKEK[y].salt), OpalKDF);

    // Start to wrap key
#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("<- %x ", (U32)pKEK[i]);
#endif
    aes_key_wrap(OpalKDF, pKEK, 0, WrapBuf);

    memcpy(pG3->b.mOpalWrapKEK[y].opalKEK, WrapBuf, sizeof(pG3->b.mOpalWrapKEK[0].opalKEK)+sizeof(pG3->b.mOpalWrapKEK[0].icv));
    pG3->b.mOpalWrapKEK[y].state = TCG_KEY_WRAPPED;

#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("-> %x ", WrapBuf[i]);
#endif

    TCGPRN("\n");
    DBG_P(0x01, 0x03, 0x710078 );  //
    memset(WrapBuf, 0, sizeof(WrapBuf));
    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif

    return 0;
}

// Unwrap "pG3->b.mOpalWrapKEK[]" (by OpalKDF) to WrapKEK"
tcg_code int TcgUnwrapOpalKEK(U8*chanllege, U8 len, U32 auth, U32* pKEK)
{
    U32 y;

    TCGPRN("<UwOpalKEK> ");
    DBG_P(0x01, 0x03, 0x710079 );  // <UwOpalKEK>
    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            break;
    }
    if (y == sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey))
    {
        // DBG_P(2, 3, 0x820201, 1, y); //TCG_PRINTF("!!NG\n");
        TCG_ERR_PRN("Error!! Authority didn't find -> y|%x\n", y);
        DBG_P(0x2, 0x03, 0x7F7F05, 4, y);  // Error!! Authority didn't find -> y|%x
        return -1;
    }

    if (pG3->b.mOpalWrapKEK[y].state != TCG_KEY_WRAPPED)
    {
        TCG_PRINTF("%02x NULL NG\n", y);
        memset(OpalKDF, 0, sizeof(OpalKDF));            // clear OpalKEK
        // DBG_P(2, 3, 0x820201, 1, (U8)pG3->b.mOpalWrapKEK[y].state);
        TCG_ERR_PRN("Error!! y|%x mOpalWrapKEK[y].state|%x\n", y, (U8)pG3->b.mOpalWrapKEK[y].state);
        DBG_P(0x3, 0x03, 0x7F7F06, 4, y, 4, (U8)pG3->b.mOpalWrapKEK[y].state);  // Error!! y|%x mOpalWrapKEK[y].state|%x
        return -1;
    }

    //Get_KeyWrap_KEK(chanllege, len, OpalKDF);
    HAL_PBKDF2((U32 *)chanllege, len, pG3->b.mOpalWrapKEK[y].salt, sizeof(pG3->b.mOpalWrapKEK[y].salt), OpalKDF);

#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("<- %x ", pG3->b.mOpalWrapKEK[y].opalKEK[i]);
#endif

    aes_key_unwrap((U32*)OpalKDF, (U32*)pG3->b.mOpalWrapKEK[y].opalKEK, 0, (U32*)WrapBuf);

    memcpy(pKEK, WrapBuf, WRAP_KEK_LEN);
    memset(WrapBuf, 0, sizeof(WrapBuf));
    for (int i = 0; i < 2; i++){
        //printk("-> %x ", pKEK[i]);
        DBG_P(0x2, 0x03, 0x71007C, 4, pKEK[i]);  // -> %x
    }
    //printk("\n");
    DBG_P(0x01, 0x03, 0x71007D );  //
    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif

    return 0;
}

// Erase "pG3->b.mOpalWrapKEK[]"
tcg_code void TcgEraseOpalKEK(U32 auth)
{
    U32 y;
    TCGPRN("<ErOpalKEK>\n");
    DBG_P(0x01, 0x03, 0x71007E );  // <ErOpalKEK>
    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            break;
    }
    if (y == sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey))
    {
        TCGPRN("EraseKEK NG");
        DBG_P(0x01, 0x03, 0x71007F );  // EraseKEK NG
        return;
    }
    memset(pG3->b.mOpalWrapKEK[y].opalKEK, 0, sizeof(pG3->b.mOpalWrapKEK[0].opalKEK));
    memset(pG3->b.mOpalWrapKEK[y].icv, 0, sizeof(pG3->b.mOpalWrapKEK[0].icv));
    memset(pG3->b.mOpalWrapKEK[y].salt, 0, sizeof(pG3->b.mOpalWrapKEK[0].salt));
    pG3->b.mOpalWrapKEK[y].state = TCG_KEY_NULL;
}

// Wrap the Range Key (pG3->b.mWKey[range].key) with KEK
void Tcg_WrapDEK(U8 range, U32* pKEK)
{
    if (pG3->b.mWKey[range].state != TCG_KEY_UNWRAPPED)
    {
        TCG_PRINTF("<WDEK> !!G3.WK[%2x].state = %x\n", range, pG3->b.mWKey[range].state);
        return;
    }

    TCG_PRINTF("<WDEK>G3Rng|%2x ", range);

    // Start to wrap key
#if 0 //_TCG_DEBUG
    for (int i = 0; i < 2; i++)
        D_PRINTF("<- %x ", pG3->b.mWKey[range].dek.aesKey[i]);
#endif

    aes_key_wrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.aesKey, 0, (U32*)WrapBuf);
    memcpy(&pG3->b.mWKey[range].dek.aesKey, WrapBuf, sizeof(pG3->b.mWKey[0].dek.aesKey)+sizeof(pG3->b.mWKey[0].dek.icv1));
    aes_key_wrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.xtsKey, 0, (U32*)WrapBuf);
    memcpy(&pG3->b.mWKey[range].dek.xtsKey, WrapBuf, sizeof(pG3->b.mWKey[0].dek.xtsKey)+sizeof(pG3->b.mWKey[0].dek.icv2));

    pG3->b.mWKey[range].state = TCG_KEY_WRAPPED;

  #if _TCG_DEBUG
    //printk("-> Key0 : ");
    DBG_P(0x01, 0x03, 0x710080 );  // -> Key0 :
    for (int i = 0; i < 2; i++){
        //printk(" %x ", WrapBuf[i]);
        DBG_P(0x2, 0x03, 0x710081, 4, WrapBuf[i]);  //  %x
    }
    //printk("  -> Key1 : ");
    DBG_P(0x01, 0x03, 0x710082 );  //   -> Key1 :
    for (int i = 0; i < 2; i++){
        //printk(" %x ", WrapBuf[i + 10]);
        DBG_P(0x2, 0x03, 0x710083, 4, WrapBuf[i + 10]);  //  %x
    }
    //printk(" \n");
    DBG_P(0x01, 0x03, 0x710084 );  //
  #endif
    memset(WrapBuf, 0, sizeof(WrapBuf));
    //D_PRINTF("\n");
    #ifdef BCM_test
    DumpTcgKeyInfo();
    #endif
}

// UnWrap the Range Key (pG3->b.mWKey[range].key) with KEK
void Tcg_UnWrapDEK(U8 range, U32* pKEK, U8 target)
{
    TCGPRN("<UWDEK> %2x %2x:\n", range, target);
    DBG_P(0x3, 0x03, 0x710085, 1, range, 1, target);  // <UWDEK> %2x %2x:
    if ((pG3->b.mWKey[range].state == TCG_KEY_UNWRAPPED) && (target==TO_RAW_KEY_BUF))
    {
        TCGPRN("copy to RK\n");
        DBG_P(0x01, 0x03, 0x710086 );  // copy to RK
        memcpy(&mRawKey[range].dek, &pG3->b.mWKey[range].dek, sizeof(mRawKey[0].dek));
        mRawKey[range].state = (S32) TCG_KEY_UNWRAPPED;
        return;
    }

    if ((mRawKey[range].state == TCG_KEY_UNWRAPPED) && (target==TO_MTBL_KEYTBL))
    {
        TCGPRN("copy to G3\n");
        DBG_P(0x01, 0x03, 0x710087 );  // copy to G3
        memcpy(&pG3->b.mWKey[range].dek, &mRawKey[range].dek, sizeof(mRawKey[0].dek));
        memset(pG3->b.mWKey[range].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
        memset(pG3->b.mWKey[range].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
        pG3->b.mWKey[range].state = (S32) TCG_KEY_UNWRAPPED;
        return;
    }

    if (pG3->b.mWKey[range].state != TCG_KEY_WRAPPED)
    {
        TCGPRN("<UWDEK> !!G3.WK[%2x].state = %x\n", range, pG3->b.mWKey[range].state);
        DBG_P(0x3, 0x03, 0x710088, 1, range, 4, pG3->b.mWKey[range].state);  // <UWDEK> !!G3.WK[%2x].state = %x
        return;
    }

    // Start to Un-Wrap
    if (target == TO_MTBL_KEYTBL)
    {
        TCGPRN("<UWDEK> to G3 Rng=%2x\n", range);
        DBG_P(0x2, 0x03, 0x710089, 1, range);  // <UWDEK> to G3 Rng=%2x
    }
    else
    {
        TCGPRN("<UWDEK> to RK Rng=%2x\n", range);
        DBG_P(0x2, 0x03, 0x71008A, 1, range);  // <UWDEK> to RK Rng=%2x
    }

    aes_key_unwrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.aesKey, 0, (U32*)&WrapBuf[0]);

    aes_key_unwrap((U32*)pKEK, (U32*)&pG3->b.mWKey[range].dek.xtsKey, 0, (U32*)&WrapBuf[10]);

#if _TCG_DEBUG
    for (int i = 0; i < 2; i++){
        //printk("-> %x ", WrapBuf[i]);
        DBG_P(0x2, 0x03, 0x71008B, 4, WrapBuf[i]);  // -> %x
    }
#endif
    // Un-Wrap process pass!
    if (target == TO_MTBL_KEYTBL)   // G3
    {
        memcpy(&pG3->b.mWKey[range].dek.aesKey, &WrapBuf[0], sizeof(pG3->b.mWKey[0].dek.aesKey));
        memcpy(&pG3->b.mWKey[range].dek.xtsKey, &WrapBuf[10], sizeof(pG3->b.mWKey[0].dek.xtsKey));
        memset(pG3->b.mWKey[range].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
        memset(pG3->b.mWKey[range].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
        pG3->b.mWKey[range].state = (S32)TCG_KEY_UNWRAPPED;
    }
    else
    {
        memcpy(&mRawKey[range].dek.aesKey, &WrapBuf[0], sizeof(mRawKey[0].dek.aesKey));
        memcpy(&mRawKey[range].dek.xtsKey, &WrapBuf[10], sizeof(mRawKey[0].dek.xtsKey));
        memset(mRawKey[range].dek.icv1, 0, sizeof(mRawKey[0].dek.icv1));
        memset(mRawKey[range].dek.icv2, 0, sizeof(mRawKey[0].dek.icv2));
        mRawKey[range].state = (S32)TCG_KEY_UNWRAPPED;
    }

    memset(WrapBuf, 0, sizeof(WrapBuf));
    #ifdef _TCG_DEBUG
    DumpTcgKeyInfo();
    #endif
}

//*******************************************************************************************
// fetch WKey to RawKey
//*******************************************************************************************
//1. If G3 key state is unwrap or null -> copy G3 key to RawKey Array (mRawKey[])
//2. If G3 key state is wrap state(LockEnableds are TRUE) ->
//      (1) readlocked=1 and writelocked=1 => clear raw key
//      (2) else => unwrap G3 key
tcg_code void TcgUpdateRawKey(U32 keyIdx)
{
    if (pG3->b.mWKey[keyIdx].state == TCG_KEY_WRAPPED)
    {
        // both write_lock_enable and read_lock_enable are TRUE
        if ((pG3->b.mLckLocking_Tbl.val[keyIdx].readLocked)
            && (pG3->b.mLckLocking_Tbl.val[keyIdx].writeLocked))
        {
            memset(&mRawKey[keyIdx].dek, 0, sizeof(mRawKey[0].dek));
            mRawKey[keyIdx].state = (S32)TCG_KEY_NULL;
        }
        else // readLocked or writeLocked is FALSE
        {
            Tcg_UnWrapDEK(keyIdx, WrapKEK, TO_RAW_KEY_BUF);// Un-Wrap to RawKey
        }
    }
    else // the G3 key of the range is in unwrap state or null key state
    {
        memcpy(&mRawKey[keyIdx].dek, &pG3->b.mWKey[keyIdx].dek, sizeof(pG3->b.mWKey[0].dek));
        mRawKey[keyIdx].state = pG3->b.mWKey[keyIdx].state;
    }
}

//*******************************************************************************************
// Update WKey only
//*******************************************************************************************
// 1. If readlockenabled=1 & writelockenabled=1 & G3 key is at unwrap state
//    -> wrap G3 key
// 2. else if the G3 key is at wrap state
//    -> unwrap G3 key
tcg_code void TcgUpdateWrapKey(U32 keyIdx)
{
    if ((pG3->b.mWKey[keyIdx].state == TCG_KEY_UNWRAPPED)
        && (pG3->b.mLckLocking_Tbl.val[keyIdx].writeLockEnabled)
        && (pG3->b.mLckLocking_Tbl.val[keyIdx].readLockEnabled))
    {
        Tcg_WrapDEK((U8)keyIdx, WrapKEK);
    }
    else if ((pG3->b.mWKey[keyIdx].state == TCG_KEY_WRAPPED)
        && (!pG3->b.mLckLocking_Tbl.val[keyIdx].writeLockEnabled
            || !pG3->b.mLckLocking_Tbl.val[keyIdx].readLockEnabled))
    {
        Tcg_UnWrapDEK(keyIdx, WrapKEK, TO_MTBL_KEYTBL);
    }
}

tcg_code void TcgUpdateRawKeyList(U32 keyList)
{
    if (keyList)
    {
        for (U32 i=0; i<=LOCKING_RANGE_CNT; i++)
        {
            if (keyList & (0x01<<i))
                TcgUpdateRawKey(i);
        }
    }
}

tcg_code void TcgUpdateWrapKeyList(U32 keyList)
{
    if (keyList)
    {
        for (U32 i=0; i<=LOCKING_RANGE_CNT; i++)
        {
            if (keyList & (0x01<<i))
                TcgUpdateWrapKey(i);
        }
    }
}

// Generate a new key, and update to G3.b.mLckKAES_256_Tbl[]
void TcgChangeKey(U8 idx)  // if not CNL, idx = rangeNo
{
    if (idx > TCG_MAX_KEY_CNT)
    {
        TCG_ERR_PRN("!! TcgChangeKey err\n");
        DBG_P(0x01, 0x03, 0x7F7F07);  // !! TcgChangeKey err
        return;
    }

    pG3->b.mWKey[idx].nsid = 1;
    pG3->b.mWKey[idx].range = idx;
    pG3->b.mWKey[idx].state = TCG_KEY_UNWRAPPED;

    HAL_Gen_Key((U32*)&pG3->b.mWKey[idx].dek, sizeof(pG3->b.mWKey[0].dek));
    memset(pG3->b.mWKey[idx].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
    memset(pG3->b.mWKey[idx].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
#ifdef KW_DBG
    pG3->b.mWKey[idx].dek.aesKey[0] = (U32)0xAAAA0000 + (U32)0x1111 * idx;
    pG3->b.mWKey[idx].dek.xtsKey[0] = (U32)0xBBBB0000 + (U32)0x1111 * idx;
#endif
    memcpy(&mRawKey[idx].dek, &pG3->b.mWKey[idx].dek, sizeof(pG3->b.mWKey[0].dek));
    mRawKey[idx].state = pG3->b.mWKey[idx].state;

    TCGPRN("range|%x mWkey0|%x-mWkey1|%x\n", idx, G3.b.mWKey[idx].dek.aesKey[0], G3.b.mWKey[idx].dek.xtsKey[0]);
    DBG_P(0x4, 0x03, 0x71008D, 4, idx, 4, G3.b.mWKey[idx].dek.aesKey[0], 4, G3.b.mWKey[idx].dek.xtsKey[0]);  // range|%x mWkey0|%x-mWkey1|%x
    TCGPRN("range|%x Rawkey0|%x-Rawkey1|%x\n", idx, mRawKey[idx].dek.aesKey[0], mRawKey[idx].dek.xtsKey[0]);
    DBG_P(0x4, 0x03, 0x71008E, 4, idx, 4, mRawKey[idx].dek.aesKey[0], 4, mRawKey[idx].dek.xtsKey[0]);  // range|%x Rawkey0|%x-Rawkey1|%x

    // DBG_P(3, 3, 0x82020A, 1, rangeNo, 4, pG3->b.mWKey[rangeNo].dek.aesKey[0]); //D_PRINTF("CKey %02x %08x-%08x\n", rangeNo, pG3->b.mWKey[rangeNo].key[0], pG3->b.mWKey[rangeNo].key[1]);
}

/* tcg_code void TcgNoKeyWrap(U8 rangeNo)
{
    if (mRawKey[rangeNo].state != TCG_KEY_UNWRAPPED)
    {
        // DBG_P(2, 3, 0x820201, (U8)mRawKey[rangeNo].state);
        return;
    }

    memcpy(&pG3->b.mWKey[rangeNo].dek, &mRawKey[rangeNo].dek, sizeof(pG3->b.mWKey[0].dek));
    memset(pG3->b.mWKey[rangeNo].icv, 0, sizeof(pG3->b.mWKey[0].icv));
    pG3->b.mWKey[rangeNo].state = (S32)TCG_KEY_UNWRAPPED;
} */

//Generate new keys for all range, and update to pG3->b.mLckKAES_256_Tbl[]
/* void TcgChangeKeyAll(void)
{
    U32 i;

    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        TcgChangeKey(i);    // Global Key
    }
    // TCG_TBL_IN_SYIN
    TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

    HAL_SEC_InitAesKeyRng();

    TcgFuncRequest1(MSG_TCG_CLR_CACHE);

    return;
} */

tcg_code void TcgEraseKey(U8 idx)
{
    pG3->b.mWKey[idx].nsid = 0;
    pG3->b.mWKey[idx].range = 0;
    pG3->b.mWKey[idx].state = TCG_KEY_NULL; // Null Key
    memset(&pG3->b.mWKey[idx].dek, 0, sizeof(pG3->b.mWKey[0].dek));
    memset(&mRawKey[idx].dek, 0, sizeof(mRawKey[0].dek));
    mRawKey[idx].state = pG3->b.mWKey[idx].state;
    // DBG_P(3, 3, 0x82020D, 1, idx);
}

/****************************************************
 * nvme Sanitize change key call back
 ****************************************************/
// Generate and update Global AES key.
// This function is called by normal code only (ATA_Security or Sanitize command)
extern bool cb_tcg_sanitize_syncSysInfo(req_t *req);
tcg_code bool cb_tcg_sanitize_changeKey_ClrCache(req_t *req)
{
    TCGPRN("cb_tcg_sanitize_changeKey_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x71008F );  // cb_tcg_sanitize_changeKey_ClrCache()
    HAL_SEC_SetAesKey(0, pG3->b.mWKey[0].dek.aesKey);
    tcg_ipc_post(req, MSG_TCG_CLR_CACHE, cb_tcg_sanitize_syncSysInfo);
    return TRUE;
}

#if 0
tcg_code bool cb_tcg_sanitize_changeKey_G3Wr(req_t *req)
{
    TCGPRN("cb_tcg_sanitize_changeKey_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x710090 );  // cb_tcg_sanitize_changeKey_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_tcg_sanitize_changeKey_ClrCache);
    return TRUE;
}

tcg_code bool cb_tcg_sanitize_changeKey_syncZone51(req_t *req)
{
    TCGPRN("cb_tcg_sanitize_changeKey_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x710091 );  // cb_tcg_sanitize_changeKey_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_tcg_sanitize_changeKey_G3Wr);
    return TRUE;
}

tcg_code bool cb_tcg_sanitize_changeKey(req_t *req)
{
    TCGPRN("cb_tcg_sanitize_changeKey()\n");
    DBG_P(0x01, 0x03, 0x710092 );  // cb_tcg_sanitize_changeKey()
    TcgChangeKey(0);        // Global Key
    bTcgKekUpdate = TRUE;
    // cb_tcg_ataErase_changeKey_G3Wr(req);
    cb_tcg_sanitize_changeKey_syncZone51(req);
    return TRUE;
}
#else
tcg_code bool cb_tcg_sanitize_changeKey_G3Wr_syncZone51(req_t *req)
{
    TCGPRN("cb_tcg_sanitize_changeKey_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71012A );  // cb_tcg_sanitize_changeKey_G3Wr_syncZone51()

    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_tcg_sanitize_changeKey_ClrCache);
    return TRUE;
}

tcg_code bool cb_tcg_sanitize_changeKey(req_t *req)
{
    TCGPRN("cb_tcg_sanitize_changeKey()\n");
    DBG_P(0x01, 0x03, 0x710092 );  // cb_tcg_sanitize_changeKey()
    TcgChangeKey(0);        // Global Key
    bTcgKekUpdate = TRUE;
    // cb_tcg_ataErase_changeKey_G3Wr(req);
    cb_tcg_sanitize_changeKey_G3Wr_syncZone51(req);
    return TRUE;
}
#endif

/****************************************************
 * nvme ATA security change key call back
 ****************************************************/
// Generate and update Global AES key.
// This function is called by normal code only (ATA_Security or Sanitize command)
extern bool MediaEraseAllRequest(req_t *req);
extern bool CleanSecurityState(req_t *req);
extern bool IsSecurityErase;
tcg_code bool cb_tcg_ataErase_changeKey_ClrCache(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_changeKey_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x710093 );  // cb_tcg_ataErase_changeKey_ClrCache()
    HAL_SEC_SetAesKey(0, pG3->b.mWKey[0].dek.aesKey);
    tcg_ipc_post(req, MSG_TCG_CLR_CACHE, CleanSecurityState);
    IsSecurityErase = TRUE;
    return TRUE;
}

#if 0
tcg_code bool cb_tcg_ataErase_changeKey_G3Wr(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_changeKey_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x710094 );  // cb_tcg_ataErase_changeKey_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_tcg_ataErase_changeKey_ClrCache);
    return TRUE;
}

tcg_code bool cb_tcg_ataErase_changeKey_syncZone51(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_changeKey_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x710095 );  // cb_tcg_ataErase_changeKey_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_tcg_ataErase_changeKey_G3Wr);
    return TRUE;
}

tcg_code bool cb_tcg_ataErase_changeKey(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_changeKey()\n");
    DBG_P(0x01, 0x03, 0x710096 );  // cb_tcg_ataErase_changeKey()
    TcgChangeKey(0);        // Global Key
    bTcgKekUpdate = TRUE;
    // cb_tcg_ataErase_changeKey_G3Wr(req);
    cb_tcg_ataErase_changeKey_syncZone51(req);
    return TRUE;
}
#else
tcg_code bool cb_tcg_ataErase_changeKey_G3Wr_syncZone51(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_changeKey_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71012B );  // cb_tcg_ataErase_changeKey_G3Wr_syncZone51()

    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_tcg_ataErase_changeKey_ClrCache);
    return TRUE;
}

tcg_code bool cb_tcg_ataErase_changeKey(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_changeKey()\n");
    DBG_P(0x01, 0x03, 0x710096 );  // cb_tcg_ataErase_changeKey()
    TcgChangeKey(0);        // Global Key
    bTcgKekUpdate = TRUE;
    // cb_tcg_ataErase_changeKey_G3Wr(req);
    cb_tcg_ataErase_changeKey_G3Wr_syncZone51(req);
    return TRUE;
}
#endif
// Generate and update Global AES key.
// This function is called by normal code only (ATA_Security or Sanitize command)
tcg_code void TcgAtaChangeKey(void)
{
    // DBG_P(1, 3, 0x82012F);  //82 01 2F, "[F]TcgAtaChangeKey"

    TcgChangeKey(0);        // Global Key
    bTcgKekUpdate = TRUE;
    TcgFuncRequest1(MSG_TCG_G3WR);

    HAL_SEC_SetAesKey(0, pG3->b.mWKey[0].dek.aesKey);

    TcgFuncRequest1(MSG_TCG_CLR_CACHE);
}

tcg_code void TcgAtaSetKey(void)
{
    // DBG_P(1, 3, 0x82013C);  //82 01 3C, "[F]TcgAtaSetKey"
    #ifndef alexcheck
    HAL_SEC_SetAesKey(0, (U32*)&mRawKey[0].dek);
    #endif
    TcgFuncRequest1(MSG_TCG_CLR_CACHE);
}

tcg_code int TcgAtaKEKState(U32 auth)
{
    for (U8 y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            return pG3->b.mOpalWrapKEK[y].state;
    }

    return TCG_KEY_NULL;
}

// Wrap pKEK from PKey and store it to the corresponding mOpalWrapKEK[]
tcg_code int TcgAtaWrapKEK(U8*chanllege, U8 len, U32 auth, U32 *pKEK)
{
    memcpy(WrapKEK, pKEK, sizeof(WrapKEK));
    return(TcgWrapOpalKEK(chanllege, len, auth, WrapKEK));
}

// Unwrap KEK from mOpalWrapKEK[] and copy it to pKEK[]
tcg_code int TcgAtaUnwrapKEK(U8*chanllege, U8 len, U32 auth, U32 *pKEK)
{
    int result = TcgUnwrapOpalKEK(chanllege, len, auth, pKEK);
    return result;
}

tcg_code void TcgAtaEraseKEK(U32 auth)
{
    TcgEraseOpalKEK(auth);
}

tcg_code bool cb_tcg_ataErase_G3Wr(req_t *req)
{
    TCGPRN("cb_tcg_ataErase_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x710097 );  // cb_tcg_ataErase_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, MediaEraseAllRequest);
    return TRUE;
}

tcg_code void TcgAtaG3Synchronize(void)
{
    TcgFuncRequest1(MSG_TCG_G3WR);
}

// Copy KEK to mOpalWrapKEK table in plain text
tcg_code void TcgAtaPlainKEK(U32 auth, U32 *pKEK, bool bToKeyTbl)
{
    U32 y;
    // DBG_P(1, 3, 0x820204);  TCGPRN("<OpalErKEK>");
    for (y = 0; y < sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); y++)
    {
        if (auth == pG3->b.mOpalWrapKEK[y].idx)
            break;
    }
    if (y == sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey))
    {
        // DBG_P(1, 3, 0x820201); //TCG_PRINTF("!!NG");
        return;
    }

    if (bToKeyTbl)
    { // copy from pKEK to KeyTbl
        memcpy(pG3->b.mOpalWrapKEK[y].opalKEK, pKEK, sizeof(pG3->b.mOpalWrapKEK[y].opalKEK));
        memset(pG3->b.mOpalWrapKEK[y].icv, 0, sizeof(pG3->b.mOpalWrapKEK[y].icv));
        memset(pG3->b.mOpalWrapKEK[y].salt, 0, sizeof(pG3->b.mOpalWrapKEK[y].salt));
        pG3->b.mOpalWrapKEK[y].state = TCG_KEY_UNWRAPPED;
    }
    else
    { // Copy from KeyTbl to pKEK
        memcpy(pKEK, pG3->b.mOpalWrapKEK[y].opalKEK, sizeof(pG3->b.mOpalWrapKEK[y].opalKEK));
    }
}

tcg_code void TcgAtaNoKeyWrap(void)
{
    memcpy(&pG3->b.mWKey[0].dek, &mRawKey[0].dek, sizeof(pG3->b.mWKey[0].dek));
    memset(pG3->b.mWKey[0].dek.icv1, 0, sizeof(pG3->b.mWKey[0].dek.icv1));
    memset(pG3->b.mWKey[0].dek.icv2, 0, sizeof(pG3->b.mWKey[0].dek.icv2));
    memset(pG3->b.mWKey[0].salt, 0, sizeof(pG3->b.mWKey[0].salt));
    pG3->b.mWKey[0].state = (S32) TCG_KEY_UNWRAPPED;
}

extern bool cb_issueFormatIpcCmd(req_t*);
bool cb_tcgnvmformat(Complete)(req_t *req)
{
    TCGPRN("cb_tcgnvmformat_Complete()\n");
    DBG_P(0x01, 0x03, 0x710098 );  // cb_tcgnvmformat_Complete()
    LockingRangeTable_Update();
    cb_issueFormatIpcCmd(req);
    return TRUE;
}

#if 0
bool cb_tcgnvmformat(G3Wr)(req_t *req)
{
    TCGPRN("cb_tcgnvmformat_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x710099 );  // cb_tcgnvmformat_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_tcgnvmformat(Complete));
    return TRUE;
}

bool cb_tcgnvmformat(syncZone51)(req_t *req)
{
    TCGPRN("cb_tcgnvmformat_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71009A );  // cb_tcgnvmformat_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_tcgnvmformat(G3Wr));
    return TRUE;
}

bool cb_tcgnvmformat(Begin)(req_t *req)
{
    TCGPRN("cb_tcgnvmformat_Begin()\n");
    DBG_P(0x01, 0x03, 0x71009B );  // cb_tcgnvmformat_Begin()
    // cb_tcgnvmformat(G3Wr)(req);
    cb_tcgnvmformat(syncZone51)(req);
    return TRUE;
}
#else
bool cb_tcgnvmformat(G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_tcgnvmformat_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71012C );  // cb_tcgnvmformat_G3Wr_syncZone51()

    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_tcgnvmformat(Complete));
    return TRUE;
}

bool cb_tcgnvmformat(Begin)(req_t *req)
{
    TCGPRN("cb_tcgnvmformat_Begin()\n");
    DBG_P(0x01, 0x03, 0x71009B );  // cb_tcgnvmformat_Begin()
    cb_tcgnvmformat(G3Wr_syncZone51)(req);
    return TRUE;
}
#endif

tcg_code void TcgNvmFormat(req_t *req)
{
    U32 y;
    #if CO_SUPPORT_AES
    for (y = 0; y < sizeof(pG3->b.mWKey) / sizeof(sWrappedKey); y++)
    {
        if (AatSecuriytActivated() && (pG3->b.mWKey[y].state == TCG_KEY_WRAPPED))
        { // AtaSecurity is enabled && key is wrapped
            TcgChangeKey(y);
            Tcg_WrapDEK(y, WrapKEK);
        }
        else
        { // key is unwrapped after format
            if (mRawKey[y].state != TCG_KEY_NULL) //(pG3->b.mWKey[y].state!=TCG_KEY_NULL)
                TcgChangeKey(y);
        }
    }
    #else
    for (y = 0; y <= LOCKING_RANGE_CNT; y++)
    {
        TcgChangeKey(y); //Erase Key (7a)
    }
    #endif
    bTcgKekUpdate = TRUE;

    #if 1
    cb_tcgnvmformat(Begin)(req);
    #else
    TcgFuncRequest1(MSG_TCG_G3WR);
    LockingRangeTable_Update();
    #endif
}
#endif  //CO_SUPPORT_AES

extern bool cb_ataSetPw_updateEEChk(req_t *);
tcg_code bool cb_tcg_ataSetPw_G3Wr(req_t *req)
{
    TCGPRN("cb_tcg_ataSetPw_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x71009C );  // cb_tcg_ataSetPw_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_ataSetPw_updateEEChk);
    return TRUE;
}


extern bool cb_ataDisPw_updateEEChk(req_t *);
tcg_code bool cb_tcg_ataDisPw_G3Wr(req_t *req)
{
    TCGPRN("cb_tcg_ataDisPw_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x71009D );  // cb_tcg_ataDisPw_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_ataDisPw_updateEEChk);
    return TRUE;
}

/*****************************************************
 * Method Revert
 *****************************************************/
///------------------admin----------------------------
tcg_code bool cb_revert(Adm, Complete)(req_t *req)
{
    TCGPRN("cb_revert_Adm_Complete()\n");
    DBG_P(0x01, 0x03, 0x71009E );  // cb_revert_Adm_Complete()

#if CO_SUPPORT_AES
    if (revert_varsMgm.doBgTrim){
        TrimAndBGC();
        revert_varsMgm.doBgTrim = FALSE;
    }
#endif

    //Clear Events for a successful invocation of the Revert method on AdminSP
    mTcgStatus = 0;
    SingleUser_Update();
    LockingRangeTable_Update();     //Update RangeTbl and KeyRAM
    ResetSessionManager(req);       //D10-1-1-1-1

    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_revert(Adm, G1Wr)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G1Wr()\n");
    DBG_P(0x01, 0x03, 0x71009F );  // cb_revert_Adm_G1Wr()

#if TCG_FS_PSID
    TcgPsidRestore();
#endif
    tcg_ipc_post(req, MSG_TCG_G1WR, cb_revert(Adm, Complete));
    return TRUE;
}

tcg_code bool cb_revert(Adm, G1RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G1RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100A0 );  // cb_revert_Adm_G1RdDefault()
    //Revert Admin SP tables:
#if TCG_FS_PSID
    TcgPsidVerify();
    //toDO: TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#endif
    tcg_ipc_post(req, MSG_TCG_G1RDDEFAULT, cb_revert(Adm, G1Wr));
    return TRUE;
}

#if 0
tcg_code bool cb_revert(Adm, ZeroRebuild)(req_t *req)
{
    TCGPRN("%s()\n", __FUNCTION__);
    DBG_P(0x2, 0x03, 0x7100A1, 0xFF, __FUNCTION__);  // %s()
    tcg_ipc_post(req, MSG_TCG_ZERO_REBUILD, cb_revert(Adm, G1RdDefault));
    //TrimAndBGC();   //cj added
    revert_varsMgm.doBgTrim = TRUE;
    return TRUE;
}
#endif

tcg_code bool cb_revert(Adm, ClrCache)(req_t *req)
{
    TCGPRN("cb_revert_Adm_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x7100A2 );  // cb_revert_Adm_ClrCache()
    tcg_ipc_post(req, MSG_TCG_CLR_CACHE, cb_revert(Adm, G1RdDefault));
    return TRUE;
}

#if 0
tcg_code bool cb_revert(Adm, G3Wr)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100A3 );  // cb_revert_Adm_G3Wr()
#if CO_SUPPORT_AES
    TcgChangeKey(0);
    for (U32 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j); //Erase Key (7a)

    bTcgKekUpdate = TRUE;
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revert(Adm, ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revert(Adm, G1RdDefault));
#endif
    return TRUE;
}

tcg_code bool cb_revert(Adm, syncZone51)(req_t *req)
{
    TCGPRN("cb_revert_Adm_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x7100A4 );  // cb_revert_Adm_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_revert(Adm, G3Wr));
    return TRUE;
}

tcg_code bool cb_revert(Adm, G3RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100A5 );  // cb_revert_Adm_G3RdDefault()
    tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revert(Adm, syncZone51));
    return TRUE;
}
#else
tcg_code bool cb_revert(Adm, G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71012D );  // cb_revert_Adm_G3Wr_syncZone51()

#if CO_SUPPORT_AES
    TcgChangeKey(0);
    for (U32 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j); //Erase Key (7a)

    bTcgKekUpdate = TRUE;
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_revert(Adm, ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_revert(Adm, G1RdDefault));
#endif
    return TRUE;
}

tcg_code bool cb_revert(Adm, G3RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100A5 );  // cb_revert_Adm_G3RdDefault()
    tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revert(Adm, G3Wr_syncZone51));
    return TRUE;
}
#endif

tcg_code bool cb_revert(Adm, G2Wr)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x7100A6 );  // cb_revert_Adm_G2Wr()
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_revert(Adm, G3RdDefault));
    return TRUE;
}

tcg_code bool cb_revert(Adm, G2RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Adm_G2RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100A7 );  // cb_revert_Adm_G2RdDefault()
    tcg_ipc_post(req, MSG_TCG_G2RDDEFAULT, cb_revert(Adm, G2Wr));
    return TRUE;
}

tcg_code bool cb_revert(Adm, DSClr)(req_t *req)
{
    TCGPRN("cb_revert_Adm_DSClr()\n");
    DBG_P(0x01, 0x03, 0x7100A8 );  // cb_revert_Adm_DSClr()
    tcg_ipc_post(req, MSG_TCG_DSCLEAR, cb_revert(Adm, G2RdDefault));
    return TRUE;
}

tcg_code bool cb_revert(Adm, SMBRClr)(req_t *req)
{
    TCGPRN("cb_revert_Adm_SMBRClr()\n");
    DBG_P(0x01, 0x03, 0x7100A9 );  // cb_revert_Adm_SMBRClr()
    tcg_ipc_post(req, MSG_TCG_SMBRCLEAR, cb_revert(Adm, DSClr));
    return TRUE;
}

tcg_code bool cb_revert(Adm, BlkErase)(req_t *req)
{
    TCGPRN("cb_revert_Adm_BlkErase()\n");
    DBG_P(0x01, 0x03, 0x7100AA );  // cb_revert_Adm_BlkErase()

    req->completion = cb_revert(Adm, SMBRClr);
    issue_block_erase(req);
    return TRUE;
}

tcg_code bool cb_revert(Adm, Begin)(req_t *req)
{
    TCGPRN("cb_revert_Adm_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100AB );  // cb_revert_Adm_Begin()
    if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
    {
    #if (_TCG_ == TCG_PYRITE)
        cb_revert(Adm, BlkErase)(req);
        return TRUE;
    #endif
        cb_revert(Adm, SMBRClr)(req);
    }
    else
    {
        cb_revert(Adm, G1RdDefault)(req);
    }
    return TRUE;
}

///------------------locking----------------------------
tcg_code bool cb_revert(Lck, Complete)(req_t *req)
{
    TCGPRN("cb_revert_Lck_Complete()\n");
    DBG_P(0x01, 0x03, 0x7100AC );  // cb_revert_Lck_Complete()

#if CO_SUPPORT_AES
    TrimAndBGC();
#endif
#if TCG_FS_BLOCK_SID_AUTH
    mTcgStatus &= (SID_BLOCKED + SID_HW_RESET);    //not Clear Events!
#else
    mTcgStatus = 0;
#endif
    SingleUser_Update();
    LockingRangeTable_Update();
    // this session remains open... x ResetSessionManager();  //D10-1-1-1-1

    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_revert(Lck, G1Wr)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G1Wr()\n");
    DBG_P(0x01, 0x03, 0x7100AD );  // cb_revert_Lck_G1Wr()

    pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)
    tcg_ipc_post(req, MSG_TCG_G1WR, cb_revert(Lck, Complete));
    return TRUE;
}

tcg_code bool cb_revert(Lck, G2Wr)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x7100AE );  // cb_revert_Lck_G2Wr()
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_revert(Lck, G1Wr));
    return TRUE;
}

tcg_code bool cb_revert(Lck, G2RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G2RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100AF );  // cb_revert_Lck_G2RdDefault()
    tcg_ipc_post(req, MSG_TCG_G2RDDEFAULT, cb_revert(Lck, G2Wr));
    return TRUE;
}

tcg_code bool cb_revert(Lck, DSClr)(req_t *req)
{
    TCGPRN("cb_revert_Lck_DSClr()\n");
    DBG_P(0x01, 0x03, 0x7100B0 );  // cb_revert_Lck_DSClr()
    tcg_ipc_post(req, MSG_TCG_DSCLEAR, cb_revert(Lck, G2RdDefault));
    return TRUE;
}

tcg_code bool cb_revert(Lck, SMBRClr)(req_t *req)
{
    TCGPRN("cb_revert_Lck_SMBRClr()\n");
    DBG_P(0x01, 0x03, 0x7100B1 );  // cb_revert_Lck_SMBRClr()
    tcg_ipc_post(req, MSG_TCG_SMBRCLEAR, cb_revert(Lck, DSClr));
    return TRUE;
}

#if 0
tcg_code bool cb_revert(Lck, ZeroRebuild)(req_t *req)
{
    TCGPRN("%s()\n", __FUNCTION__);
    DBG_P(0x2, 0x03, 0x7100B2, 0xFF, __FUNCTION__);  // %s()
    tcg_ipc_post(req, MSG_TCG_ZERO_REBUILD, cb_revert(Lck, SMBRClr));
    //TrimAndBGC();   //cj added
    revert_varsMgm.doBgTrim = TRUE;
    return TRUE;
}
#endif

tcg_code bool cb_revert(Lck, ClrCache)(req_t *req)
{
    TCGPRN("cb_revert_Lck_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x7100B3 );  // cb_revert_Lck_ClrCache()
    tcg_ipc_post(req, MSG_TCG_CLR_CACHE, cb_revert(Lck, SMBRClr));
    return TRUE;
}

#if 0
tcg_code bool cb_revert(Lck, G3Wr)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100B4 );  // cb_revert_Lck_G3Wr()
#if CO_SUPPORT_AES
    TcgChangeKey(0);
    for (U32 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j); //Erase Key (7a)

    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revert(Lck, ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revert(Lck, SMBRClr));
#endif
    return TRUE;
}

tcg_code bool cb_revert(Lck, syncZone51)(req_t *req)
{
    TCGPRN("cb_revert_Lck_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x7100B5 );  // cb_revert_Lck_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_revert(Lck, G3Wr));
    return TRUE;
}

tcg_code bool cb_revert(Lck, G3RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100B6 );  // cb_revert_Lck_G3RdDefault()
    tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revert(Lck, syncZone51));
    return TRUE;
}
#else
tcg_code bool cb_revert(Lck, G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71012E );  // cb_revert_Lck_G3Wr_syncZone51()

#if CO_SUPPORT_AES
    TcgChangeKey(0);
    for (U32 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j); //Erase Key (7a)

    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_revert(Lck, ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_revert(Lck, SMBRClr));
#endif
    return TRUE;
}

tcg_code bool cb_revert(Lck, G3RdDefault)(req_t *req)
{
    TCGPRN("cb_revert_Lck_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100B6 );  // cb_revert_Lck_G3RdDefault()
    tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revert(Lck, G3Wr_syncZone51));
    return TRUE;
}
#endif

tcg_code bool cb_revert(Lck, BlkErase)(req_t *req)
{
    TCGPRN("cb_revert_Lck_BlkErase()\n");
    DBG_P(0x01, 0x03, 0x7100B7 );  // cb_revert_Lck_BlkErase()

    req->completion = cb_revert(Lck, G3RdDefault);
    issue_block_erase(req);
    return TRUE;
}

tcg_code bool cb_revert(Lck, Begin)(req_t *req)
{
    TCGPRN("cb_revert_Lck_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100B8 );  // cb_revert_Lck_Begin()

    if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured){
    #if (_TCG_ == TCG_PYRITE)
        cb_revert(Lck, BlkErase)(req);
    #else
        cb_revert(Lck, G3RdDefault)(req);
    #endif
    }
    return TRUE;
}

///------------------Method_Revert----------------------
//extern Error_t VscBlockErase(void);
tcg_code U16 Method_Revert(req_t *req)
{
    U16 result = STS_SUCCESS;

    // DBG_P(1, 3, 0x82012C);  //82 01 2C, "[F]Method_Revert"
    TCGPRN("Method_Revert()\n");
    DBG_P(0x01, 0x03, 0x7100B9 );  // Method_Revert()
    revert_varsMgm.doBgTrim = FALSE;
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
        return STS_SESSION_ABORT;   //no definition in Test Case!!

    if (invokingUID.all == UID_SP_Admin)
    {
        // DBG_P(1, 3, 0x8201B3);  //82 01 B3, "M_Revert: AdminSP"

#if _TCG_ == TCG_EDRV
        if (mSessionManager.HtSgnAuthority.all == UID_Authority_PSID)
            mPsidRevertCnt++;   //for WHQL test
#endif

        // TODO:
        // 1. This session SHALL be aborted.
        // 2. if ATA security feature set is not diabled, then bit 1 in word 82 in IDENTIFY DEVICE SHALL be set to 1,
        //    and bit 1 in word 85 and all bits in word 89, 90, 92 and 128 SHALL be set to appropriate values.
        // 3. LockingEnbabled bit in Locking Feature Descriptor in Level 0 SHALL be set to 0. (v)
        // 4. LifeCycleState column of the Locking SP object in the SP table, SHALL be set to 0x08. (v)
        // 5. a startup of a session to the Locking SP SHALL fail. (v)
        // 6. The PIN value of SID object in the C_PIN table is set to the value of MSID credential.
        // 7. If the Locking SP is in Manufactured state:
        //       a. All the data in the User LBA Range SHALL be cryptographically erased.
        //       b. All the values in DataStore table SHALL be the value in its OFS.
        //       c. All the values in MBR table SHALL be the value in its OFS.
        // 8. If the Locking SP is in Manufactured-Inactive state:
        //       All the data in user LBA range SHALL NOT change.
        // ?  The entire TPer SHALL revert to its OFS.

        //copy MSID to SID ... (6)
#if 1
        cb_revert(Adm, Begin)(req);
#else
        if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
        {
#if (_TCG_==TCG_PYRITE)
            VscBlockErase(); // debug
#endif
            // pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)

            //Revert LockingSP tables:
            // DBG_P(1, 3, 0x8201B5);  //82 01 B5, "w2/w3"

            // clear MBR table  (7c)
            TcgFuncRequest1(MSG_TCG_SMBRCLEAR);
            TcgFuncRequest1(MSG_TCG_DSCLEAR);     //clear DataStore table

            //Revert Locking SP tables (?)
            TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G2WR);

#if CO_SUPPORT_AES  //(_TCG_!=TCG_PYRITE)  //
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgChangeKey(0);
            for (j = 1; j <= LOCKING_RANGE_CNT; j++)
                TcgEraseKey(j); //Erase Key (7a)

            bTcgKekUpdate = TRUE;
            TcgFuncRequest1(MSG_TCG_G3WR);
            TcgFuncRequest1(MSG_TCG_CLR_CACHE);
            TcgFuncRequest1(MSG_TCG_ZERO_REBUILD);   // rebuild zero pattern

            //TrimAndBGC();   //cj added
            doBgTrim = TRUE;
#else
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G3WR);
#endif
        }

        //Revert Admin SP tables:
#if TCG_FS_PSID
        TcgPsidVerify();
        //toDO: TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#endif
        TcgFuncRequest1(MSG_TCG_G1RDDEFAULT);
#if TCG_FS_PSID
        TcgPsidRestore();
#endif
        TcgFuncRequest1(MSG_TCG_G1WR);
        //WaitDmyWr(6);  // write 6 dummy pages for open block
#if TCG_FS_PSID
        //memset(tcg_ee_Psid, 0, sizeof(...));
        //TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#endif

#if CO_SUPPORT_AES
        if (doBgTrim)
        {
            TrimAndBGC();
            doBgTrim = FALSE;
        }
#endif

        //Clear Events for a successful invocation of the Revert method on AdminSP
        mTcgStatus = 0;
        SingleUser_Update();
        LockingRangeTable_Update();  //Update RangeTbl and KeyRAM
        ResetSessionManager(req);  //D10-1-1-1-1
#endif
        result = STS_SUCCESS;     // STS_SESSION_ABORT;
    }
    else if (invokingUID.all == UID_SP_Locking)
    {
        // DBG_P(1, 3, 0x8201B4);  //82 01 B4, "M_Revert: LockingSP"
        //TODO:
        //1. This session remains open.
        //2. if ATA security feature set is not diabled, then bit 1 in word 82 in IDENTIFY DEVICE SHALL be set to 1,
        //   and bit 1 in word 85 and all bits in word 89, 90, 92 and 128 SHALL be set to appropriate values.
        //3. LockingEnbabled bit in Locking Feature Descriptor in Level 0 SHALL be set to 0. (v)
        //4. LifeCycleState column of the Locking SP object in the SP table, SHALL be set to 0x08. (v)
        //5. a startup of a session to the Locking SP SHALL fail. (v)
        //6. x
        //7. If the Locking SP is in Manufactured state:
        //      a. All the data in the User LBA Range SHALL be cryptographically erased.
        //      b. All the values in DataStore table SHALL be the value in its OFS.
        //      c. All the values in MBR table SHALL be the value in its OFS.
        //8. If the Locking SP is in Manufactured-Inactive state:
        //      a. All the data in user LBA range SHALL NOT change.
        //      b. All the values in DataStore table SHALL be the value in its OFS.
        //      c. All the values in the MBR table SHALL be the value in its OFS.
        //?  The SP itself SHALL revert to its OFS.

        cb_revert(Lck, Begin)(req);

#if 0
        if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle==manufactured)
        {
#if (_TCG_==TCG_PYRITE)
            VscBlockErase(); // debug
#endif
            //Revert LockingSP tables:
            // DBG_P(1, 3, 0x8201B5);  //82 01 B5, "w2/w3"
#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE  //
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgChangeKey(0);
            for (j = 1; j <= LOCKING_RANGE_CNT; j++)
                TcgEraseKey(j); //Erase Keys (7a)

            TcgFuncRequest1(MSG_TCG_G3WR);
            TcgFuncRequest1(MSG_TCG_CLR_CACHE);
            TcgFuncRequest1(MSG_TCG_ZERO_REBUILD);   // rebuild zero pattern
#else
            TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G3WR);
#endif
            //clear MBR table  (7c)
            TcgFuncRequest1(MSG_TCG_SMBRCLEAR);
            TcgFuncRequest1(MSG_TCG_DSCLEAR);       //clear DataStore table (7b)

            TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);
            TcgFuncRequest1(MSG_TCG_G2WR);

            pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)
            TcgFuncRequest1(MSG_TCG_G1WR);
            //WaitDmyWr(6);  // write 6 dummy pages for open block
#if CO_SUPPORT_AES
            TrimAndBGC();
#endif
#if TCG_FS_BLOCK_SID_AUTH
            mTcgStatus &= (SID_BLOCKED + SID_HW_RESET);    //not Clear Events!
#else
            mTcgStatus = 0;
#endif
            SingleUser_Update();
            LockingRangeTable_Update();
            // this session remains open... x ResetSessionManager();  //D10-1-1-1-1
        }
#endif
        result = STS_SUCCESS;
    }
    else
        result = STS_INVALID_PARAMETER;

#if TCG_TBL_HISTORY_DESTORY
    if(result == STS_SUCCESS){
        TcgFuncRequest1(MSG_TCG_TBL_HIST_DEST);
    }
#endif

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return result;
}

#if _TCG_ != TCG_PYRITE //CO_SUPPORT_AES
/*****************************************************
 * Method GenKey
 *****************************************************/
tcg_code bool cb_genkey(Complete)(req_t *req)
{
    TCGPRN("cb_genkey_Complete()\n");
    DBG_P(0x01, 0x03, 0x7100BA );  // cb_genkey_Complete()
    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_genkey(ClrCache)(req_t *req)
{
    TCGPRN("cb_genkey_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x7100BB );  // cb_genkey_ClrCache()
    HAL_SEC_InitAesKeyRng();
    tcg_ipc_post(req, MSG_TCG_CLR_CACHE, cb_genkey(Complete));
    return TRUE;
}

#if 0
tcg_code bool cb_genkey(G3Wr)(req_t *req)
{
    TCGPRN("cb_genkey_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100BC );  // cb_genkey_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_genkey(ClrCache));
    return TRUE;
}

tcg_code bool cb_genkey(syncZone51)(req_t *req)
{
    TCGPRN("cb_genkey_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x7100BD );  // cb_genkey_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_genkey(G3Wr));
    return TRUE;
}

tcg_code bool cb_genkey(Begin)(req_t *req)
{
    TCGPRN("cb_genkey_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100BE );  // cb_genkey_Begin()
    // cb_genkey(G3Wr)(req);
    cb_genkey(syncZone51)(req);
    return TRUE;
}
#else
tcg_code bool cb_genkey(G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_genkey_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x71012F );  // cb_genkey_G3Wr_syncZone51()
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_genkey(ClrCache));
    return TRUE;
}

tcg_code bool cb_genkey(Begin)(req_t *req)
{
    TCGPRN("cb_genkey_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100BE );  // cb_genkey_Begin()
    cb_genkey(G3Wr_syncZone51)(req);
    return TRUE;
}
#endif

//-------------------Activate-------------------------
//Test Cases D8: how to deal with "Transaction"?
tcg_code U16 Method_GenKey(req_t *req)
{
    U16 result;
    U8 keyNo = 0xff;

    // DBG_P(1, 3, 0x82012D);  //82 01 2D, "[F]Method_GenKey"
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //check the key range (invoking UID) and update key
    switch (invokingUID.all)
    {
        case UID_K_AES_256_GRange_Key:
            keyNo = 0;
            break;
        default:
            keyNo = (U8)invokingUID.all; // - ((U8)UID_K_AES_256_Range1_Key - 1);
            break;
    }

    if (keyNo <= LOCKING_RANGE_CNT)
    {
        // DBG_P(2, 3, 0x8201B7, 1, keyNo);  //82 01 B7, "M_GKey: %X", 1
        TcgChangeKey(keyNo);

        //update keys to KeyRAM and NAND
        if (mSessionManager.TransactionState == TRNSCTN_IDLE)
        {
#if CO_SUPPORT_AES
            if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
            {
                TcgUpdateWrapKey(keyNo); // determine if wrap new key or not
                TcgUpdateRawKey(keyNo);
            }
#endif
            #if 1
            cb_genkey(Begin)(req);
            #else
            TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

            HAL_SEC_InitAesKeyRng();
            TcgFuncRequest1(MSG_TCG_CLR_CACHE);
            #endif
        }
        else
        {
#if CO_SUPPORT_AES
            if (TCG_ACT_IN_OPAL() || TCG_ACT_IN_ALL_SU())
            {
                mRawKeyUpdateList |= (0x01<<keyNo);
            }
#endif
            flgs_MChnged.b.G3 = TRUE;
            bKeyChanged = TRUE;
        }
        result = STS_SUCCESS;
    }
    else
        result = STS_INVALID_PARAMETER;

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    //TODO: inform the kernel to update AES key setting if not in Transaction state
    return result;
}
#endif

//LockingSP.RevertSP()
//Check Test Cases D10-3-1-1-1
/*****************************************************
 * Method RevertSP
 *****************************************************/
tcg_code bool cb_revertsp(Complete)(req_t *req)
{
    TCGPRN("cb_revertsp_Complete()\n");
    DBG_P(0x01, 0x03, 0x7100BF );  // cb_revertsp_Complete()

#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE  //
   if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TrimAndBGC();
#endif
#if TCG_FS_BLOCK_SID_AUTH
    mTcgStatus &= (SID_BLOCKED + SID_HW_RESET); //no Clear Events!
#else
    mTcgStatus = 0;
#endif
    SingleUser_Update();
    LockingRangeTable_Update();
    //this session is abort by the status code of "STS_SUCCESS_THEN_ABORT"
    // x ResetSessionManager();  //D10-1-1-1-1

    //dataBuf[iDataBuf++]=TOK_StartList;
    //dataBuf[iDataBuf++]=TOK_EndList;
    //dataBuf[iDataBuf++]=TOK_EndOfData;

    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_revertsp(G1Wr)(req_t *req)
{
    TCGPRN("cb_revertsp_G1Wr()\n");
    DBG_P(0x01, 0x03, 0x7100C0 );  // cb_revertsp_G1Wr()
    pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)
    tcg_ipc_post(req, MSG_TCG_G1WR, cb_revertsp(Complete));
    return TRUE;
}

tcg_code bool cb_revertsp(G2Wr)(req_t *req)
{
    TCGPRN("cb_revertsp_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x7100C1 );  // cb_revertsp_G2Wr()
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_revertsp(G1Wr));
    return TRUE;
}

tcg_code bool cb_revertsp(G2RdDefault)(req_t *req)
{
    TCGPRN("cb_revertsp_G2RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100C2 );  // cb_revertsp_G2RdDefault()
    tcg_ipc_post(req, MSG_TCG_G2RDDEFAULT, cb_revertsp(G2Wr));
    return TRUE;
}

tcg_code bool cb_revertsp(DSClr)(req_t *req)
{
    TCGPRN("cb_revertsp_DSClr()\n");
    DBG_P(0x01, 0x03, 0x7100C3 );  // cb_revertsp_DSClr()
    tcg_ipc_post(req, MSG_TCG_DSCLEAR, cb_revertsp(G2RdDefault));
    return TRUE;
}

tcg_code bool cb_revertsp(SMBRClr)(req_t *req)
{
    TCGPRN("cb_revertsp_SMBRClr()\n");
    DBG_P(0x01, 0x03, 0x7100C4 );  // cb_revertsp_SMBRClr()
    tcg_ipc_post(req, MSG_TCG_SMBRCLEAR, cb_revertsp(DSClr));
    return TRUE;
}

#if 0
tcg_code bool cb_revertsp(ZeroRebuild)(req_t *req)
{
    TCGPRN("%s()\n", __FUNCTION__);
    DBG_P(0x2, 0x03, 0x7100C5, 0xFF, __FUNCTION__);  // %s()
    tcg_ipc_post(req, MSG_TCG_ZERO_REBUILD, cb_revertsp(SMBRClr));
    return TRUE;
}
#endif

tcg_code bool cb_revertsp(ClrCache)(req_t *req)
{
    TCGPRN("cb_revertsp_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x7100C6 );  // cb_revertsp_ClrCache()
    if(revertSp_varsMgm.keepGlobalRangeKey != 1)
        tcg_ipc_post(req, MSG_TCG_CLR_CACHE, cb_revertsp(SMBRClr));
    else
        // cb_revertsp(ZeroRebuild)(req);
        cb_revertsp(SMBRClr)(req);
    return TRUE;
}


#if 0
tcg_code bool cb_revertsp(G3Wr)(req_t *req)
{
    TCGPRN("cb_revertsp_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100C7 );  // cb_revertsp_G3Wr()
#if CO_SUPPORT_AES
    //Erase Keys (7)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgChangeKey(0);
    else
    { //restore key 0
        //memcpy(&(pG3->b.mWKey[0]), &tmpKey, sizeof(sWrappedKey));
        Tcg_UnWrapDEK(0, WrapKEK, TO_MTBL_KEYTBL);

        // eDrive is at single user mode, global raneg should be always at unwrap state
        //memcpy(mRawKey[0].key, pG3->b.mWKey[0].key, sizeof(mRawKey[0].key));
        //mRawKey[0].state = pG3->b.mWKey[0].state;
    }
    for (U8 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j);

    bTcgKekUpdate = TRUE;
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revertsp(ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revertsp(SMBRClr));
#endif
    return TRUE;
}

tcg_code bool cb_revertsp(syncZone51)(req_t *req)
{
    TCGPRN("cb_revertsp_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x7100C8 );  // cb_revertsp_syncZone51()
    if(revertSp_varsMgm.keepGlobalRangeKey != 1){
        CPU1_chg_ebc_key_key();
        tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_revertsp(G3Wr));
    }else{
        cb_revertsp(G3Wr)(req);
    }
    return TRUE;
}

tcg_code bool cb_revertsp(G3RdDefault)(req_t *req)
{
    TCGPRN("cb_revertsp_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100C9 );  // cb_revertsp_G3RdDefault()
    tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revertsp(syncZone51));
    return TRUE;
}
#else
tcg_code bool cb_revertsp(G3Wr)(req_t *req)
{
    TCGPRN("cb_revertsp_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100C7 );  // cb_revertsp_G3Wr()
#if CO_SUPPORT_AES
    //Erase Keys (7)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgChangeKey(0);
    else
    { //restore key 0
        //memcpy(&(pG3->b.mWKey[0]), &tmpKey, sizeof(sWrappedKey));
        Tcg_UnWrapDEK(0, WrapKEK, TO_MTBL_KEYTBL);

        // eDrive is at single user mode, global raneg should be always at unwrap state
        //memcpy(mRawKey[0].key, pG3->b.mWKey[0].key, sizeof(mRawKey[0].key));
        //mRawKey[0].state = pG3->b.mWKey[0].state;
    }
    for (U8 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j);

    bTcgKekUpdate = TRUE;
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revertsp(ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_revertsp(SMBRClr));
#endif
    return TRUE;
}

tcg_code bool cb_revertsp(G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_revertsp_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x710130 );  // cb_revertsp_G3Wr_syncZone51()
#if CO_SUPPORT_AES
    //Erase Keys (7)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgChangeKey(0);
    else
    { //restore key 0
        //memcpy(&(pG3->b.mWKey[0]), &tmpKey, sizeof(sWrappedKey));
        Tcg_UnWrapDEK(0, WrapKEK, TO_MTBL_KEYTBL);

        // eDrive is at single user mode, global raneg should be always at unwrap state
        //memcpy(mRawKey[0].key, pG3->b.mWKey[0].key, sizeof(mRawKey[0].key));
        //mRawKey[0].state = pG3->b.mWKey[0].state;
    }
    for (U8 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j);

    bTcgKekUpdate = TRUE;
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_revertsp(ClrCache));
#else
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_revertsp(SMBRClr));
#endif
    return TRUE;
}

tcg_code bool cb_revertsp(G3RdDefault)(req_t *req)
{
    TCGPRN("cb_revertsp_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100C9 );  // cb_revertsp_G3RdDefault()
    if(revertSp_varsMgm.keepGlobalRangeKey != 1){
        tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revertsp(G3Wr_syncZone51));
    }else{
        tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_revertsp(G3Wr));
    }
    return TRUE;
}
#endif

tcg_code bool cb_revertsp(BlkErase)(req_t *req)
{
    TCGPRN("cb_revertsp_BlkErase()\n");
    DBG_P(0x01, 0x03, 0x7100CA );  // cb_revertsp_BlkErase()

    req->completion = cb_revertsp(G3RdDefault);
    issue_block_erase(req);
    return TRUE;
}


tcg_code bool cb_revertsp(Begin)(req_t *req)
{
    TCGPRN("cb_revertsp_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100CB );  // cb_revertsp_Begin()

#if (_TCG_ == TCG_PYRITE)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        cb_revertsp(BlkErase)(req);
    else
#endif
    cb_revertsp(G3RdDefault)(req);

    return TRUE;
}

///------------------Method_RevertSP----------------------
tcg_code U16 Method_RevertSP(req_t *req)
{
    //sWrappedKey tmpKey;
    U32 nameValue = 0;
    U16 result;
    U8  data;


    //TODO:
    //0. If 'KeepGlobalRangeKey' is 1, and Locking GlobalRange is both read-locked and write-locked, fail with FAIL.
    //1. This session SHALL be aborted.
    //2. if ATA security feature set is not diabled, then bit 1 in word 82 in IDENTIFY DEVICE SHALL be set to 1,
    //   and bit 1 in word 85 and all bits in word 89, 90, 92 and 128 SHALL be set to appropriate values.
    //3. LockingEnbabled bit in Locking Feature Descriptor in Level 0 SHALL be set to 0. (v)
    //4. LifeCycleState column of the Locking SP object in the SP table, SHALL be set to 0x08. (v)
    //5. a startup of a session to the Locking SP SHALL fail. (v)
    //6. x
    //7. except GlobalRange (if 'KeepGlobalRangeKey'=1), all of the user ranges SHALL be cryptographically erased.
    //8. Whichever 'KeepGlobalRangeKey' is, all the values in DataStore table SHALL be the value in its OFS.
    //9. Whichever 'KeepGlobalRangeKey' is, all the values in MBR table SHALL be the value in its OFS.
    //?  The SP itself SHALL revert to its OFS.

    TCGPRN("Method_RevertSP()\n");
    DBG_P(0x01, 0x03, 0x7100CC );  // Method_RevertSP()
    // DBG_P(1, 3, 0x82012E);  //82 01 2E, "[F]Method_RevertSP"
    revertSp_varsMgm.keepGlobalRangeKey = 0;
    //parameter check
    if (ChkToken() != TOK_StartList)    // test cases 3.1.5
        return STS_SESSION_ABORT;

    //retrieve parameter 'KeepGlobalRangeKey'
    data = ChkToken();
    if (data == TOK_StartName)
    {
        if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            return STS_INVALID_PARAMETER;

        if (nameValue == 0x060000)
        {
            if (AtomDecoding_Uint(&revertSp_varsMgm.keepGlobalRangeKey, sizeof(revertSp_varsMgm.keepGlobalRangeKey)) != STS_SUCCESS)
                return STS_INVALID_PARAMETER;

            if ((revertSp_varsMgm.keepGlobalRangeKey!=1)&&(revertSp_varsMgm.keepGlobalRangeKey!=0))
                return STS_INVALID_PARAMETER;

            if (revertSp_varsMgm.keepGlobalRangeKey == 1)
            {
                if(pG3->b.mLckLocking_Tbl.val[0].readLocked && pG3->b.mLckLocking_Tbl.val[0].writeLocked)
                    return STS_FAIL;    //(0)
                #ifndef alexcheck
                Nvme_Security_FlushAll();
                #endif
            }
        }

        if (ChkToken() != TOK_EndName)
            return STS_INVALID_PARAMETER;

        data = ChkToken();
    }

    if (data != TOK_EndList)            // test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    // test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if(result!=STS_SUCCESS)
        return result;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
        return STS_SESSION_ABORT;   //no definition in Test Case!!

    // DBG_P(2, 3, 0x8201B8, 1, revertSp_varsMgm.keepGlobalRangeKey);  //82 01 B8, "M_RevertSP: keepGlobalRangeKey[%X]", 1
#if 1
    cb_revertsp(Begin)(req);
#else
#if (_TCG_==TCG_PYRITE)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        VscBlockErase();
#endif

    //Revert Locking SP tables (?)
    // DBG_P(1, 3, 0x8201B9);  //82 01 B9, "M_RevertSP: w2/w3"

#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE

    TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);

    //Erase Keys (7)
    if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgChangeKey(0);
    else
    { //restore key 0
        //memcpy(&(pG3->b.mWKey[0]), &tmpKey, sizeof(sWrappedKey));
        Tcg_UnWrapDEK(0, WrapKEK, TO_MTBL_KEYTBL);

        // eDrive is at single user mode, global raneg should be always at unwrap state
        //memcpy(mRawKey[0].key, pG3->b.mWKey[0].key, sizeof(mRawKey[0].key));
        //mRawKey[0].state = pG3->b.mWKey[0].state;
    }
    for (U8 j = 1; j <= LOCKING_RANGE_CNT; j++)
        TcgEraseKey(j);

    bTcgKekUpdate = TRUE;
    TcgFuncRequest1(MSG_TCG_G3WR);

    if(revertSp_varsMgm.keepGlobalRangeKey != 1)
        TcgFuncRequest1(MSG_TCG_CLR_CACHE);

    TcgFuncRequest1(MSG_TCG_ZERO_REBUILD);   // rebuild zero pattern
#else
    TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);
    TcgFuncRequest1(MSG_TCG_G3WR);
#endif

    //clear MBR table  (9)
    TcgFuncRequest1(MSG_TCG_SMBRCLEAR);
    TcgFuncRequest1(MSG_TCG_DSCLEAR);       //clear DataStore table (8)

    TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);
    TcgFuncRequest1(MSG_TCG_G2WR);

    pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle = manufactured_inactive;    // (3) (4)
    TcgFuncRequest1(MSG_TCG_G1WR);   //WaitG1Wr();
    //WaitDmyWr(6);  // write 6 dummy pages for open block
#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE  //
   if (revertSp_varsMgm.keepGlobalRangeKey != 1)
        TrimAndBGC();
#endif
#if TCG_FS_BLOCK_SID_AUTH
    mTcgStatus &= (SID_BLOCKED + SID_HW_RESET); //no Clear Events!
#else
    mTcgStatus = 0;
#endif
    SingleUser_Update();
    LockingRangeTable_Update();
    //this session is abort by the status code of "STS_SUCCESS_THEN_ABORT"
    // x ResetSessionManager();  //D10-1-1-1-1

    //dataBuf[iDataBuf++]=TOK_StartList;
    //dataBuf[iDataBuf++]=TOK_EndList;
    //dataBuf[iDataBuf++]=TOK_EndOfData;

#if TCG_TBL_HISTORY_DESTORY
    TcgFuncRequest1(MSG_TCG_TBL_HIST_DEST);
#endif
#endif  // #if 0
    return STS_SUCCESS_THEN_ABORT; //??? STS_SESSION_ABORT;   //D10-3-3-1-1
}

//ThisSP.Authenticate()
tcg_code U16 Method_Authenticate(req_t *req)
{
    UID64 authority;
    U64 auth0 = UID_Null;
    U32 len = 0;
    U16 result;
    U8  byte, j, *pt = NULL;

    // DBG_P(1, 3, 0x820130);  //82 01 30, "[F]Method_Authenticate"
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //retrieve parameter 'Authority'
    if (AtomDecoding_Uid2(authority.bytes) != STS_SUCCESS)
        return STS_INVALID_PARAMETER;

    byte = ChkToken();
    if (byte == TOK_StartName)
    {
        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
            return STS_INVALID_PARAMETER;

        if (byte == 0x00)
        {// param: Proof (HostChallenge)
            result = AtomDecoding_ByteHdr(&len);
            if (result == STS_SUCCESS)
            {
                if (len>CPIN_LENGTH)
                    return STS_INVALID_PARAMETER;

                pt = &mCmdPkt.payload[iPload];
                iPload += len;
                byte = ChkToken();
            }
        }

        if (byte != TOK_EndName)
            return STS_INVALID_PARAMETER;

        byte = ChkToken();
    }

    if (byte == TOK_EndList)
        goto END_LIST;
    else  //NG within parameters
        return STS_SESSION_ABORT;   //A6-1-5-2-1, STS_INVALID_PARAMETER;

END_LIST:
    if (ChkToken() != TOK_EndOfData)
        return STS_STAY_IN_IF_SEND;

    //status list check
    result = chk_method_status();
    if(result!=STS_SUCCESS)
        return result;

    //method execution:
    // only suuport "Anybody + one authority" in a session
    if (authority.all == UID_Authority_Anybody)
        result = STS_SUCCESS;
    else
    { //authority!=anybody
    #if 1 //check if authority is found and not "IsClass"
        if (mSessionManager.SPID.all == UID_SP_Admin)
        {
            for(j=0;j<pG1->b.mAdmAuthority_Tbl.hdr.rowCnt;j++)
            { // row# (4) can be acquired from Table tbl;
                if(authority.all == pG1->b.mAdmAuthority_Tbl.val[j].uid)
                {
                    if(pG1->b.mAdmAuthority_Tbl.val[j].isClass == TRUE)
                        return STS_INVALID_PARAMETER;   //UID is a class
                    else
                        goto CHECK_SIGN;
                }
            }
            return STS_INVALID_PARAMETER;   //no authority found!
        }
        else
        {
            for(j=0;j<pG3->b.mLckAuthority_Tbl.hdr.rowCnt;j++)
            { // row# (4) can be acquired from Table tbl;
                if(authority.all==pG3->b.mLckAuthority_Tbl.val[j].uid)
                {
                    if(pG3->b.mLckAuthority_Tbl.val[j].isClass==TRUE)
                        return STS_INVALID_PARAMETER;   //UID is a class
                    else
                        goto CHECK_SIGN;
                }
            }
            return STS_INVALID_PARAMETER;   //no authority found!
        }
    CHECK_SIGN:
    #endif
        auth0 = mSessionManager.HtSgnAuthority.all;     //backup original authority
#if 1 //temperarily marked off for eDrive test ...
        if ((auth0 != UID_Authority_Anybody) && (auth0 != authority.all))
            result = STS_NOT_AUTHORIZED;    //return STS_SUCCESS with result=FALSE
        else
#endif
#if (TCG_FS_PSID == FALSE)  // PSID is not supported!!
        if (authority.all == UID_Authority_PSID)
            result = STS_NOT_AUTHORIZED;
        else
#endif
#if TCG_FS_BLOCK_SID_AUTH
        if ((mTcgStatus&SID_BLOCKED) && (authority.all == UID_Authority_SID))
            result = STS_NOT_AUTHORIZED;
        else
#endif
        {
            mSessionManager.HtSgnAuthority.all = authority.all;
            memset(mSessionManager.HtChallenge, 0, sizeof(mSessionManager.HtChallenge));
            mSessionManager.HtChallenge[0] = (U8)len;
            memcpy(&mSessionManager.HtChallenge[1], pt, len);
            result = host_signing_authority_check();
        }
    }

    dataBuf[iDataBuf++] = TOK_StartList;

    // DBG_P(3, 3, 0x8201BA, 4, (U32)authority.all, 2, result);  //82 01 BA, "M_Authenticate: %08X %04X", 4 2

    if (result == STS_SUCCESS)
    {
        dataBuf[iDataBuf++] = 0x01;   //TRUE;
#if CO_SUPPORT_AES
        if (mSessionManager.SPID.all == UID_SP_Locking)
        {
            if (TCG_ACT_IN_OPAL())
            {
                TcgUnwrapOpalKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], mSessionManager.HtSgnAuthority.dw[0], WrapKEK);
            }
            else if (TCG_ACT_IN_ALL_SU())
            {
                if (mSessionManager.SPID.all == UID_SP_Locking)
                {
                    U8 rngNo = (U8)(mSessionManager.HtSgnAuthority.all - UID_Authority_User1);
                    TcgGetEdrvKEK(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], rngNo, WrapKEK);
                }
            }
        }
#endif
    }
    else
    { // failed, restore authority
        mSessionManager.HtSgnAuthority.all = auth0;
        dataBuf[iDataBuf++] = 0x00;   //FALSE;
    }
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;
}

tcg_code U16 Method_Random(req_t *req)
{
    U32 count, i, tmp[32/4];
    U16 result;

    // DBG_P(1, 3, 0x820131);  //82 01 31, "[F]Method_Random"
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //retrieve parameter 'Count'
    if (AtomDecoding_Uint((U8*)&count, sizeof(count)) != STS_SUCCESS)
        return STS_INVALID_PARAMETER;

    if (count > 0x20) {                   // OPAL spec 4.2.6.1 , The TPer SHALL support Count parameter values less than or equal to 32.
        return STS_INVALID_PARAMETER;
    }

    if (count>(U32)(TCG_BUF_LEN - iDataBuf - 11))     //buffersize limitation: F0 82 xx yy F1 F9 F0 00 00 00 F1, iDataBuf=0x38
        return STS_RESPONSE_OVERFLOW;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)  //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
        return STS_SESSION_ABORT;   //no definition in Test Case!!

    // DBG_P(2, 3, 0x8201BB, 4, count);  //82 01 BB, "M_RND: %X", 4

    dataBuf[iDataBuf++] = TOK_StartList;
    AtomEncoding_ByteHdr(count);

    HAL_Gen_Key((U32*)tmp, count);

    for (i = 0; i<count; i++)
        dataBuf[iDataBuf++] = ((U8*)tmp)[i];

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return STS_SUCCESS;
}

#if _TCG_ != TCG_PYRITE
/*****************************************************
 * Method Reactivate
 *****************************************************/
tcg_code bool cb_Reactivate(Complete)(req_t *req)
{
    TCGPRN("cb_Reactivate_Complete()\n");
    DBG_P(0x01, 0x03, 0x7100CD );  // cb_Reactivate_Complete()
    //cj: TcgStatus should keep the same, mTcgStatus = 0;
    LockingRangeTable_Update();
    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_Reactivate(G3Wr)(req_t *req)
{
    TCGPRN("cb_Reactivate_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100CE );  // cb_Reactivate_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_Reactivate(Complete));
    return TRUE;
}

tcg_code bool cb_Reactivate(G2Wr)(req_t *req)
{
    U8  i;
    U8  *ptr = NULL;

    TCGPRN("cb_Reactivate_G2Wr()\n");
    DBG_P(0x01, 0x03, 0x7100CF );  // cb_Reactivate_G2Wr()
    // Restore AesKeyTbl
#if CO_SUPPORT_AES
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        pG3->b.mWKey[i].state = TCG_KEY_UNWRAPPED;
        memcpy(&pG3->b.mWKey[i].dek, &mRawKey[i].dek, sizeof(mRawKey[0].dek));
        pG3->b.mWKey[i].dek.icv1[0] = 0;
        pG3->b.mWKey[i].dek.icv2[0] = 0;
    }
#endif

    // Restore rangeStart/rangeLength
    ptr = (U8*)tcgTmpBuf + (TCG_TEMP_BUF_SZ - DTAG_SZE);  // use last 8K;
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        memcpy((U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeStart, ptr, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart);

        memcpy((U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeLength, ptr, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength);
    }

    if (reactivate_varsMgm.bAdmin1PIN)
    {
        Tcg_GenCPinHash(&reactivate_varsMgm.bk_HtChallenge[1], reactivate_varsMgm.bk_HtChallenge[0], &pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin);
    }
    else
    {// Backup sCPin
        memcpy((U8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag, ptr, sizeof(sCPin));
        //ptr += sizeof(sCPin);
    }

    mSgUser.cnt = reactivate_varsMgm.sgUserCnt;
    mSgUser.policy = reactivate_varsMgm.sgUserPolicy;
    mSgUser.range = reactivate_varsMgm.sgUserRange;

    // DBG_P(3, 3, 0x8201BE, 1, reactivate_varsMgm.dsCnt, 2, mSgUser.range);  //82 01 BE, "M_ReAct: DSCnt=%02X, SURx=%04X", 1 2

    DataStore_Setting(reactivate_varsMgm.dsCnt, reactivate_varsMgm.DSTblSize);    //update group2
    SingleUser_Setting();                   //update group2/group3

//tcg_souts("w2/w3");
    tcg_ipc_post(req, MSG_TCG_G2WR, cb_Reactivate(G3Wr));

    return TRUE;
}

tcg_code bool cb_Reactivate(G3RdDefault)(req_t *req)
{
    TCGPRN("cb_Reactivate_G3RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100D0 );  // cb_Reactivate_G3RdDefault()
    tcg_ipc_post(req, MSG_TCG_G3RDDEFAULT, cb_Reactivate(G2Wr));
    return TRUE;
}

tcg_code bool cb_Reactivate(G2RdDefault)(req_t *req)
{
    TCGPRN("cb_Reactivate_G2RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100D1 );  // cb_Reactivate_G2RdDefault()
    tcg_ipc_post(req, MSG_TCG_G2RDDEFAULT, cb_Reactivate(G3RdDefault));
    return TRUE;
}

tcg_code bool cb_Reactivate(Begin)(req_t *req)
{
    TCGPRN("cb_Reactivate_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100D2 );  // cb_Reactivate_Begin()
    cb_Reactivate(G2RdDefault)(req);
    return TRUE;
}

//-------------------Reactivate-------------------------
tcg_code U16 Method_Reactivate(req_t *req)
{
    U64 tmp64;
    U32 nameValue, tmp32, totalsize = 0;
    U16 result = 0;
    U8  i, tmp8, errCode = 0;


    U8  *ptr = NULL;

    // init reactivate_varsMgm
    reactivate_varsMgm.sgUserRange = 0;
    reactivate_varsMgm.sgUserCnt = 0;
    reactivate_varsMgm.sgUserPolicy = 1;
    reactivate_varsMgm.bAdmin1PIN = 0;
    reactivate_varsMgm.dsCnt = 0;

    // DBG_P(1, 3, 0x820132);  //82 01 32, "[F]Method_Reactivate"
    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
    {
        errCode = 0x10;  result = STS_SESSION_ABORT;  goto MTD_EXIT;
    }

    //retrieve parameter 'DataStoreTblSizes'
    tmp8 = ChkToken();
    if (tmp8 == TOK_StartName)
    {
        if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
        {
            errCode = 0x20;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
        }

        if (nameValue == 0x060000) //'SingleUserSelectionList'
        {
            //mSgUser.cnt=0;
            //mSgUser.policy=0;
            //mSgUser.range=0;

            if (ChkToken() == TOK_StartList)
            { // Locking table objects list
                tmp8 = 0;
                while (ChkToken() != TOK_EndList)
                {
                    iPload--;

                    if (AtomDecoding_Uid2((U8 *)&tmp64) != STS_SUCCESS)
                    {
                        errCode = 0x30;  result = STS_INVALID_PARAMETER;  goto MTD_EXIT;
                    }

                    //check if it is in the locking table
                    for(i=tmp8;i<pG3->b.mLckLocking_Tbl.hdr.rowCnt;i++)
                    {
                        if(pG3->b.mLckLocking_Tbl.val[i].uid==tmp64)
                        {
                            reactivate_varsMgm.sgUserRange |= (0x01<<i);
                            reactivate_varsMgm.sgUserCnt++;
                            tmp8 = i;
                            break;
                        }
                    }

                    if (i >= pG3->b.mLckLocking_Tbl.hdr.rowCnt)
                    {
                        if ((pG3->b.mLckLocking_Tbl.val[0].uid == tmp64)
                            && ((reactivate_varsMgm.sgUserRange & 0x01) == 0))
                        {
                            //TCG_PRINTF("* %08x\n", (U32)tmp64);
                            reactivate_varsMgm.sgUserRange |= 0x01;
                            reactivate_varsMgm.sgUserCnt++;
                            tmp8 = i;
                        }
                        else
                        {
                            errCode = 0x31;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                        }
                    }
                }
            }
            else
            { // check if entire Locking table
                iPload--;
                if (AtomDecoding_Uid2((U8 *)&tmp64) != STS_SUCCESS)
                {
                    errCode = 0x32;  result = STS_STAY_IN_IF_SEND;    goto MTD_EXIT;
                }

                if (tmp64 != UID_Locking)
                {
                    errCode = 0x33;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                reactivate_varsMgm.sgUserCnt = LOCKING_RANGE_CNT + 1;
                reactivate_varsMgm.sgUserRange = 0xffff;   //EntireLocking
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x34;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x35;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        if (reactivate_varsMgm.sgUserCnt)
            reactivate_varsMgm.sgUserPolicy = 0;   //User has ownership

        if (nameValue == 0x060001) //'RangePolicy'
        {
            if (AtomDecoding_Uint(&tmp8, sizeof(tmp8)) != STS_SUCCESS)
            {
                errCode = 0x38;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (tmp8 == 0)
            {
                if (reactivate_varsMgm.sgUserCnt != 0) reactivate_varsMgm.sgUserPolicy = 0;
            }
            else if (tmp8 == 1)
                reactivate_varsMgm.sgUserPolicy = 1;
            else
            {
                errCode = 0x39;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x3A;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x3B;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        if (nameValue == 0x060002) //'Admin1PIN'
        {
            //if(AtomDecoding_Uint(&tmp8, sizeof(tmp8))!=STS_SUCCESS)
            //{   errCode=0x3C;  result=STS_INVALID_PARAMETER;    goto MTD_EXIT; }

            result = AtomDecoding_ByteHdr(&tmp32);
            if (result == STS_SUCCESS)
            {
                if (tmp32>CPIN_LENGTH)
                {
                    result = STS_INVALID_PARAMETER;       //need to check ...
                    errCode = 0x3C;     goto MTD_EXIT;
                }

                reactivate_varsMgm.bAdmin1PIN = 1;
                memset(mSessionManager.HtChallenge, 0, sizeof(mSessionManager.HtChallenge));
                mSessionManager.HtChallenge[0] = (U8)tmp32;
                for (i = 0; i < tmp32; i++)
                    mSessionManager.HtChallenge[i + 1] = mCmdPkt.payload[iPload++];
            }
            else
            {
                errCode = 0x3D;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x3E;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            if ((tmp8 = ChkToken()) != TOK_StartName)
                goto CHK_ENDLIST;

            if (AtomDecoding_Uint((U8*)&nameValue, sizeof(nameValue)) != STS_SUCCESS)
            {
                errCode = 0x3F;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }
        }

        //check Additional DataStore Parameter
        if (nameValue == 0x060003) //'DataStoreTblSizes'
        {
            if (ChkToken() != TOK_StartList)
            {
                errCode = 0x41;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            while (ChkToken() != TOK_EndList)
            {
                iPload--;

                if (AtomDecoding_Uint((U8*)&tmp32, sizeof(tmp32)) != STS_SUCCESS)
                {
                    errCode = 0x42;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                // *WCS TFS Item 168907 [Security] Incomplete length check
                if (tmp32 > DATASTORE_LEN) //size is too large
                {
                    errCode = 0x48;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }
                // &WCS TFS Item 168907 [Security] Incomplete length check

                if (reactivate_varsMgm.dsCnt >= DSTBL_MAX_NUM)      //too many tables
                {
                    errCode = 0x43;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                totalsize += tmp32;
                if (totalsize>DATASTORE_LEN) //size is too large
                {
                    errCode = 0x44;  result = STS_INSUFFICIENT_SPACE;    goto MTD_EXIT;
                }

                if (tmp32%DSTBL_ALIGNMENT)   //not aligned
                {
                    errCode = 0x45;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
                }

                reactivate_varsMgm.DSTblSize[reactivate_varsMgm.dsCnt] = tmp32;
                reactivate_varsMgm.dsCnt++;
            }

            if (ChkToken() != TOK_EndName)
            {
                errCode = 0x46;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
            }

            tmp8 = ChkToken();
        }
        else
        {
            errCode = 0x47;  result = STS_INVALID_PARAMETER;    goto MTD_EXIT;
        }
    }

CHK_ENDLIST:
    if (tmp8 != TOK_EndList)      //test cases 3.1.5
    {
        errCode = 0x15;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
    {
        errCode = 0x16;  result = STS_SESSION_ABORT;    goto MTD_EXIT;
    }

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
    {
        errCode = 0x17;  goto MTD_EXIT;
    }

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
    {
        errCode = 0x18;   result = STS_SESSION_ABORT;   goto MTD_EXIT;
    } //no definition in Test Case!!


    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    { // return FAIL if any of the locking ranges is enabled
        if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled || pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled)
        {
            errCode = 0x19;   result = STS_FAIL;    goto MTD_EXIT;
        }
    }

    //Reactivate processing...
    //restore to OFS with some exceptions... C_PIN_Admin1.PIN, RangeStart/RangeLength, K_AES

    // Backup RangeTbl
    ptr = (U8*)tcgTmpBuf + (TCG_TEMP_BUF_SZ - DTAG_SZE);  // use last 8K
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        memcpy(ptr, (U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeStart, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart);
        memcpy(ptr, (U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeLength, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength);
    }

    if (!reactivate_varsMgm.bAdmin1PIN)
    {// Backup sCPin
        memcpy(ptr, (U8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag, sizeof(sCPin));
        //ptr += sizeof(sCPin);
    }
    memcpy(reactivate_varsMgm.bk_HtChallenge, mSessionManager.HtChallenge, sizeof(mSessionManager.HtChallenge));  // backup for cb use.

#if 1
    cb_Reactivate(Begin)(req);
#else
    // restore mTbl to OFS
    TcgFuncRequest1(MSG_TCG_G2RDDEFAULT);   //cTbl_2_mTbl_byGrp(GRP_2);
    TcgFuncRequest1(MSG_TCG_G3RDDEFAULT);   //cTbl_2_mTbl_byGrp(GRP_3);
    // TCG_NewKeyTbl();

    // Restore AesKeyTbl
#if CO_SUPPORT_AES
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        pG3->b.mWKey[i].state = TCG_KEY_UNWRAPPED;
        pG3->b.mWKey[i].icv[0] = 0;
        memcpy(&pG3->b.mWKey[i].dek, &mRawKey[i].dek, sizeof(mRawKey[0].dek));
    }
#endif

    // Restore rangeStart/rangeLength
    ptr = (U8*)tcgTmpBuf;
    for (i = 0; i <= LOCKING_RANGE_CNT; i++)
    {
        memcpy((U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeStart, ptr, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeStart);

        memcpy((U8 *)&pG3->b.mLckLocking_Tbl.val[i].rangeLength, ptr, sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength));
        ptr +=  sizeof(pG3->b.mLckLocking_Tbl.val[0].rangeLength);
    }

    if (bAdmin1PIN)
    {
        Tcg_GenCPinHash(&mSessionManager.HtChallenge[1], mSessionManager.HtChallenge[0], &pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin);
    }
    else
    {// Backup sCPin
        memcpy((U8 *)&pG3->b.mLckCPin_Tbl.val[LCK_CPIN_ADMIN1_IDX].cPin.cPin_Tag, ptr, sizeof(sCPin));
        //ptr += sizeof(sCPin);
    }

    mSgUser.cnt = sgUserCnt;
    mSgUser.policy = sgUserPolicy;
    mSgUser.range = sgUserRange;

    // DBG_P(3, 3, 0x8201BE, 1, dsCnt, 2, mSgUser.range);  //82 01 BE, "M_ReAct: DSCnt=%02X, SURx=%04X", 1 2

    DataStore_Setting(dsCnt, DSTblSize);    //update group2
    SingleUser_Setting();                   //update group2/group3

//tcg_souts("w2/w3");
    TcgFuncRequest1(MSG_TCG_G2WR);   //WaitG2Wr();
    TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

    //cj: TcgStatus should keep the same, mTcgStatus = 0;
    LockingRangeTable_Update();

    //dataBuf[iDataBuf++]=TOK_StartList;
    //dataBuf[iDataBuf++]=TOK_EndList;
    //dataBuf[iDataBuf++]=TOK_EndOfData;
#endif
    result = STS_SUCCESS_THEN_ABORT;

MTD_EXIT:
    TCGPRN("Method_Reactivate() errCode|%x dsCnt|%x mSgUser|%x\n", errCode, reactivate_varsMgm.dsCnt, mSgUser.range);
    DBG_P(0x4, 0x03, 0x7100D3, 4, errCode, 4, reactivate_varsMgm.dsCnt, 4, mSgUser.range);  // Method_Reactivate() errCode|%x dsCnt|%x mSgUser|%x
    // DBG_P(4, 3, 0x8201BE, 1, errCode, 1, reactivate_varsMgm.dsCnt, 2, mSgUser.range);  //82 01 BE, "M_ReAct: DSCnt=%02X, SURx=%04X", 1 2

    return result;
}
#endif

#if _TCG_!=TCG_PYRITE  //CO_SUPPORT_AES
/*****************************************************
 * Method Erase
 *****************************************************/
tcg_code bool cb_erase(Complete)(req_t *req)
{
    TCGPRN("cb_erase_Complete()\n");
    DBG_P(0x01, 0x03, 0x7100D4 );  // cb_erase_Complete()
    method_complete_post(req, TRUE);
    return TRUE;
}

tcg_code bool cb_erase(ClrCache)(req_t *req)
{
    TCGPRN("cb_erase_ClrCache()\n");
    DBG_P(0x01, 0x03, 0x7100D5 );  // cb_erase_ClrCache()
    //update key and range
    LockingRangeTable_Update();
    tcg_ipc_post(req, MSG_TCG_CLR_CACHE, cb_erase(Complete));

    return TRUE;
}

#if 0
tcg_code bool cb_erase(G3Wr)(req_t *req)
{
    TCGPRN("cb_erase_G3Wr()\n");
    DBG_P(0x01, 0x03, 0x7100D6 );  // cb_erase_G3Wr()
    tcg_ipc_post(req, MSG_TCG_G3WR, cb_erase(ClrCache));
    return TRUE;
}

tcg_code bool cb_erase(syncZone51)(req_t *req)
{
    TCGPRN("cb_erase_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x7100D7 );  // cb_erase_syncZone51()
    CPU1_chg_ebc_key_key();
    tcg_ipc_post(req, MSG_TCG_SYNC_ZONE51_MEDIA, cb_erase(G3Wr));
    return TRUE;
}

tcg_code bool cb_erase(Begin)(req_t *req)
{
    TCGPRN("cb_erase_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100D8 );  // cb_erase_Begin()
    // cb_erase(G3Wr)(req);
    cb_erase(syncZone51)(req);
    return TRUE;
}
#else
tcg_code bool cb_erase(G3Wr_syncZone51)(req_t *req)
{
    TCGPRN("cb_erase_G3Wr_syncZone51()\n");
    DBG_P(0x01, 0x03, 0x710131 );  // cb_erase_G3Wr_syncZone51()
    tcg_ipc_post(req, MSG_TCG_G3WR_SYNC_ZONE51, cb_erase(ClrCache));
    return TRUE;
}

tcg_code bool cb_erase(Begin)(req_t *req)
{
    TCGPRN("cb_erase_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100D8 );  // cb_erase_Begin()
    cb_erase(G3Wr_syncZone51)(req);
    return TRUE;
}
#endif

//-------------------Method Erase-------------------------
tcg_code U16 Method_Erase(req_t *req)
{
    U64 tmp64;
    U16 result;
    U8 range = 0xFF, i;

    // DBG_P(1, 3, 0x820133);  //82 01 33, "[F]Method_Erase"

    //parameter check
    if (ChkToken() != TOK_StartList)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndList)      //test cases 3.1.5
        return STS_SESSION_ABORT;

    if (ChkToken() != TOK_EndOfData)    //test cases 3.1.5
        return STS_SESSION_ABORT;

    //status list check
    result = chk_method_status();
    if (result != STS_SUCCESS)
        return result;

    //cjdbg: need to check if singleuser mode or not?
    //get the locking range (invoking UID)
    if (invokingUID.all == UID_Locking_GRange)
        range = 0;
    else
        range = (U8)invokingUID.all;

    if (range <= LOCKING_RANGE_CNT)
    {
        // DBG_P(2, 3, 0x8201BF, 1, range);  //82 01 BF, "M_Erase: rng=%X", 1
        TcgChangeKey(range);
        pG3->b.mLckLocking_Tbl.val[range].readLockEnabled=0x00;
        pG3->b.mLckLocking_Tbl.val[range].writeLockEnabled=0x00;
        pG3->b.mLckLocking_Tbl.val[range].readLocked=0x00;
        pG3->b.mLckLocking_Tbl.val[range].writeLocked=0x00;

        //search for corresponding CPIN
        tmp64=UID_CPIN_User1+range;
        for(i=0;i<pG3->b.mLckCPin_Tbl.hdr.rowCnt;i++)
        {
            if(tmp64==pG3->b.mLckCPin_Tbl.val[i].uid)
            {
                memset(&pG3->b.mLckCPin_Tbl.val[i].cPin, 0, sizeof(pG3->b.mLckCPin_Tbl.val[i].cPin));
                pG3->b.mLckCPin_Tbl.val[i].tries=0;
                break;
            }
        }
    }
    else
        result = STS_INVALID_PARAMETER;

    //update keys to Key RAM and NAND
    if (mSessionManager.TransactionState == TRNSCTN_IDLE)
    {
        #if 1
        cb_erase(Begin)(req);
        #else
        TcgFuncRequest1(MSG_TCG_G3WR);   //WaitG3Wr();

        //update key and range
        LockingRangeTable_Update();

        TcgFuncRequest1(MSG_TCG_CLR_CACHE);
        #endif
    }
    else
    {
        flgs_MChnged.b.G3 = TRUE;
        bKeyChanged = TRUE;
    }

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;

    return result;
}
#endif

/***********************************************************
* Level0_Discovery
* Prepare Level 0 Discovery response data in *buf
* ref. core spec 3.3.6
***********************************************************/
tcg_code U16 Level0_Discovery(U8* buf)
{
    L0DISCOVERY_t *pl0     = (L0DISCOVERY_t *)buf;   // point of Level0 Discovery
    void          *pl0_fea = (U8 *)&pl0->FD;
    L0FEA0002_DATA_t fea0002_dat;
    U8 *pSsstc = pl0->Header.VendorSpecific;        // vendor define // alexcheck
    U32 l0_fea_len = 0;

    TCGPRN("Level0_Discovery()\n");
    DBG_P(0x01, 0x03, 0x7100D9 );  // Level0_Discovery()
    memset(buf, 0, sizeof(DTAG_SZE));

    //construct L0 Discovery Header
    pl0->Header.Revision = swap_u32((U32)1);
    #if 1   // alexcheck
    *((U32*)pSsstc + 0)  = sh_secure_boot_info.otp_secure_enabled;    // 16
    *((U32*)pSsstc + 1)  = sh_secure_boot_info.fw_secure_enable;      // 20
    *((U32*)pSsstc + 2)  = sh_secure_boot_info.loader_policy;         // 24
    *((U32*)pSsstc + 3)  = sh_secure_boot_info.maincode_policy;       // 28
    #else
    *((U32*)pSsstc)     = swap_u32(tcg_ee_PsidTag);         // 16
    *((U16*)pSsstc + 2) = swap_u16(mSgUser.range);          // 20
    *((U8*)pSsstc + 16) = (U8)bcmClientVars.otpDeploy;      // 32
    *((U8*)pSsstc + 17) = (U8)bcmClientVars.otpLifeCycle;   // 33
    *((U32*)pSsstc + 5) = swap_u32((U32)POLICY_DEV_TAG);    // 36
    #endif

    // fea0001 construct TPer Feature Descriptor
    pl0_fea = (U8 *)&pl0->FD;
    ((L0FEA0001_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0001);
    ((L0FEA0001_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0001_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0001_t) - 4;
    ((L0FEA0001_t *)pl0_fea)->fea0001.b.sync_support      = 1;
    ((L0FEA0001_t *)pl0_fea)->fea0001.b.streaming_support = 1;
    l0_fea_len = sizeof(L0FEA0001_t);

    // fea0002 construct Locking Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0002_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0002);
    #if _TCG_ != TCG_PYRITE
    ((L0FEA0002_t *)pl0_fea)->ver           = 0x1;
    #else
    ((L0FEA0002_t *)pl0_fea)->ver           = 0x2;
    #endif
    ((L0FEA0002_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0002_t) - 4;
    fea0002_dat.b.fea0002_locking_support = 1;
    #if _TCG_ != TCG_PYRITE
    fea0002_dat.b.fea0002_encryption = 1;
    #else
    fea0002_dat.b.fea0002_encryption = 0;
    #endif
    //MBRControlSet case11(D5-1-2-2-4)check MBR_Done(b5), MBR_Enabled(b4), and Locked(b2) for B4
    #if (BUILD_SSD_CUSTOMER == SSD_CUSTOMER_DELL)
        fea0002_dat.b.fea0002_mbr_not_support = 1;
    #else
        if(pG3->b.mLckMbrCtrl_Tbl.val[0].enable){
            fea0002_dat.b.fea0002_mbr_enabled = 1;
            if(pG3->b.mLckMbrCtrl_Tbl.val[0].done)  //check MRB_Done(b5) only when "enable" is TRUE
                fea0002_dat.b.fea0002_mbr_done = 1;
        }
    #endif
    if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
        fea0002_dat.b.fea0002_activated = 1;
    if(mReadLockedStatus||mWriteLockedStatus)
        fea0002_dat.b.fea0002_locked = 1;
    ((L0FEA0002_t *)pl0_fea)->fea0002.fea0002_Bdata  = fea0002_dat.fea0002_Bdata;
    TCGPRN("Locking Feature|%x\n", fea0002_dat.fea0002_Bdata);
    DBG_P(0x2, 0x03, 0x7100DA, 4, fea0002_dat.fea0002_Bdata);  // Locking Feature|%x
    l0_fea_len = sizeof(L0FEA0002_t);

    // fea0003 construct Geometry Reporting Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0003_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0003);
    ((L0FEA0003_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0003_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0003_t) - 4;
    ((L0FEA0003_t *)pl0_fea)->align         = 1;
    ((L0FEA0003_t *)pl0_fea)->logical_blk_sz          = swap_u32((U32)TCG_LogicalBlockSize);
    ((L0FEA0003_t *)pl0_fea)->alignment_granularity   = swap_u64((U64)TCG_AlignmentGranularity);
    l0_fea_len = sizeof(L0FEA0003_t);

#if _TCG_ != TCG_PYRITE
    // fea0201 construct Single User Mode Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0201_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0201);
    ((L0FEA0201_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0201_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0201_t) - 4;
    ((L0FEA0201_t *)pl0_fea)->num_of_lcking_obj_support = swap_u32((U32)LOCKING_RANGE_CNT+1);
    if(mSgUser.range)   //B8: b2~Policy, b1~All, b0~Any
        ((L0FEA0201_t *)pl0_fea)->any       = 1;
    else
        ((L0FEA0201_t *)pl0_fea)->any       = 0;
    if ((mSgUser.range & 0x1ff) == 0x1ff)   //EntireLocking
        ((L0FEA0201_t *)pl0_fea)->all       = 1;
    if (mSgUser.policy)
        ((L0FEA0201_t *)pl0_fea)->policy    = 1;
    l0_fea_len = sizeof(L0FEA0201_t);

    // fea0202 construct DataStore Table Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0202_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0202);
    ((L0FEA0202_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0202_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0202_t) - 4;
    ((L0FEA0202_t *)pl0_fea)->max_num_of_datastore        = swap_u16((U16)DSTBL_MAX_NUM);
    ((L0FEA0202_t *)pl0_fea)->max_total_sz_of_datastore   = swap_u32((U32)DATASTORE_LEN);
    ((L0FEA0202_t *)pl0_fea)->datastore_sz_alignment      = swap_u32((U32)DSTBL_ALIGNMENT);
    l0_fea_len = sizeof(L0FEA0202_t);

    // fea0203 construct Opal SSC Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0203_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0203);
    ((L0FEA0203_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0203_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0203_t) - 4;
    ((L0FEA0203_t *)pl0_fea)->base_comid    = swap_u16((U16)BASE_COMID);
    ((L0FEA0203_t *)pl0_fea)->num_of_comid  = swap_u16((U16)1);
    ((L0FEA0203_t *)pl0_fea)->rng_crossing_behavior       = 0;
    ((L0FEA0203_t *)pl0_fea)->num_of_adm_auth_support     = swap_u16((U16)TCG_AdminCnt);
    ((L0FEA0203_t *)pl0_fea)->num_of_user_auth_support    = swap_u16((U16)TCG_UserCnt);
    l0_fea_len = sizeof(L0FEA0203_t);

#else  // Pyrite  _TCG_ != TCG_PYRITE
    // fea0303 construct Pyrite SSC Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0303_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0303);
    ((L0FEA0303_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0303_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0303_t) - 4;
    ((L0FEA0303_t *)pl0_fea)->base_comid    = swap_u16((U16)BASE_COMID);
    ((L0FEA0303_t *)pl0_fea)->num_of_comid  = swap_u16((U16)1);
    l0_fea_len = sizeof(L0FEA0303_t);

    // fea0404 construct Data Removal Mechanism Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0404_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0404);
    ((L0FEA0404_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0404_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0404_t) - 4;

    ((L0FEA0404_t *)pl0_fea)->data_removal_operation_processing = 0;
    /*  reference Pyrite spec 3.1.1.5.2 Table 8 Supported Data Removal Mechanism
        bit0: Overwrite Data Erase
        bit1: Block Erase
        bit2: Crypto Erase
        bit3: Unmap
        bit4: Reset Write Pointers
        bit5: Vendor Specific Erase
        bit6: Reserved
        bit7: Reserved
    */
    ((L0FEA0404_t *)pl0_fea)->support_data_removal_mechanism = SupportDataRemovalMechanism;
    ((L0FEA0404_t *)pl0_fea)->data_removal_time_fm_bit1_data = swap_u16((U16)30);
    l0_fea_len = sizeof(L0FEA0404_t);
#endif // _TCG_ != TCG_PYRITE

#if TCG_FS_BLOCK_SID_AUTH
    // fea0402 construct Block SID Authentication Feature Descriptor
    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    ((L0FEA0402_t *)pl0_fea)->FeaCode       = swap_u16((U16)0x0402);
    ((L0FEA0402_t *)pl0_fea)->ver           = 0x1;
    ((L0FEA0402_t *)pl0_fea)->FeaCodeLen    = sizeof(L0FEA0402_t) - 4;
    if(CPinMsidCompare(CPIN_SID_IDX))
        ((L0FEA0402_t *)pl0_fea)->sid_st    = 1;
    if(mTcgStatus & SID_BLOCKED)
        ((L0FEA0402_t *)pl0_fea)->sid_block_st = 1;
    if(mTcgStatus & SID_HW_RESET)
        ((L0FEA0402_t *)pl0_fea)->hard_reset = 1;
    l0_fea_len = sizeof(L0FEA0402_t);
#endif  // TCG_FS_BLOCK_SID_AUTH

    pl0_fea = (U8 *)pl0_fea + l0_fea_len;
    pl0->Header.len = swap_u32((U32)((U32)(pl0_fea) - (U32)(pl0) - sizeof(pl0->Header.len)));

    return (U16)((U32)(pl0_fea) - (U32)(pl0) + 1);
}


/****************************************************************************
 * host_properties_parse():  <--- host_properties_parse();
 ****************************************************************************/
tcg_code U16 host_properties_parse(req_t *req, U32 *tmpHostPty, U8 *hitHostPty, U8 *hostPtyCnt, U16 *result)
{
    U8  byte,   idx,    *pname;
    U32 len,    value;

    TCGPRN("host_properties_parse()\n");
    DBG_P(0x01, 0x03, 0x7100DB );  // host_properties_parse()
    //parameter check
    if(ChkToken() != TOK_StartList) return PROPERTY_PARSE_TOK_ERR;     // List1, test cases 3.1.5 , STS_SESSION_ABORT

    byte = ChkToken();

    if (byte == TOK_StartName) //Name1
    { //check host properties
        if (AtomDecoding_Uint(&byte, sizeof(byte)) != STS_SUCCESS)
        {
            *result = STS_INVALID_PARAMETER; return PROPERTY_PARSE_ST_ERR;
        }

        if (byte == 0x00)
        { // Host Properties
            if (ChkToken() == TOK_StartList)
            { //L2
                while ((byte = ChkToken()) != 0xff)
                {
                    if (byte == TOK_StartName)
                    {
                        if (AtomDecoding_ByteHdr(&len) != STS_SUCCESS)   // name
                        {
                            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                        }
                        pname = &mCmdPkt.payload[iPload];
                        iPload += len;

                        if (AtomDecoding_Uint((U8*)&value, sizeof(value)) != STS_SUCCESS) // value
                        {
                            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                        }

                        (*hostPtyCnt)++;

                        for (idx = 0; idx < HostPropertiesCnt; idx++)
                        {
                            if(memcmp(pname, mHostProperties[idx].name, len) == 0)
                            {
                                if(hitHostPty[idx] == 0)
                                    hitHostPty[idx]++;
                                else
                                {
                                    *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                                }   //A6-3-4-2-1(2)

                                if(idx == 0)
                                    tmpHostPty[0] = (value < HOST_MAX_COMPKT_SZ) ? HOST_MAX_COMPKT_SZ : value;

                                else if(idx == 1)
                                    tmpHostPty[1] = (value < HOST_MAX_PKT_SZ)    ? HOST_MAX_PKT_SZ    : value;

                                else if(idx == 2)
                                    tmpHostPty[2] = (value < HOST_MAX_INDTKN_SZ) ? HOST_MAX_INDTKN_SZ : value;

                                break;
                            }
                        }

                        if (ChkToken() != TOK_EndName) //EndName2
                        {
                            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                        }
                    }
                    else if (byte == TOK_EndList) //EndList2
                        break;
                    else
                    {
                        *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
                    }
                }
            }
        }
        else
        {
            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
        }

        if((byte = ChkToken()) != TOK_EndName)   //EndName1
        {
            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
        }

        byte = ChkToken();
        if(byte == TOK_StartName)
        {
            *result = STS_INVALID_PARAMETER;  return PROPERTY_PARSE_ST_ERR;
        }   //A6-3-4-2-1(2), encoded twice!
    }

//END_LIST:
    if(byte != TOK_EndList) return PROPERTY_PARSE_TOK_ERR;  //EndList1
    if(ChkToken() != TOK_EndOfData) return PROPERTY_PARSE_TOK_ERR;

    //status list check
    *result = chk_method_status();
    if(*result == STS_SESSION_ABORT) return PROPERTY_PARSE_TOK_ERR;

    if(*result != STS_SUCCESS) return PROPERTY_PARSE_ST_ERR;    // return result;

//cj test: Subpackets > 1 [
    iPload = ((iPload + (sizeof(U32) - 1)) / sizeof(U32)) * sizeof(U32);   // align at 4

    if ((U32)(iPload + 12 + 12) <= mCmdPkt.mPktFmt.length)
    {
        iPload += 8; // reserved  + kind
        value = swap_u32((U32)(*((U32 *)&mCmdPkt.payload[iPload])));
        iPload += sizeof(U32);
        if(value) return PROPERTY_PARSE_TOK_ERR;
    }
// ]
    return PROPERTY_PARSE_OK;
}

/****************************************************************************
 * prepare_properties_response():  <--- ();
 ****************************************************************************/
tcg_code void prepare_properties_response(U8 hostPtyCnt)
{
    U16 idx,    j;
    U8  slen;

    TCGPRN("prepare_properties_response()\n");
    DBG_P(0x01, 0x03, 0x7100DC );  // prepare_properties_response()

    //prepare payload for reply: TperPerperties data
    dataBuf [iDataBuf++] = TOK_Call;
    dataBuf [iDataBuf++] = 0xA8;
    for(j = 8; j != 0; )
        dataBuf[iDataBuf++] = invokingUID.bytes[--j];   //SessionManagerUID

    dataBuf [iDataBuf++] = 0xA8;
    for(j = 8; j != 0; )
        dataBuf[iDataBuf++] = methodUID.bytes[--j];      //PropertiesUID

    dataBuf[iDataBuf++] = TOK_StartList;
    dataBuf[iDataBuf++] = TOK_StartList;

    for(idx = 0; idx < TperPropertiesCnt; idx++) {
        dataBuf[iDataBuf++] = TOK_StartName;

        slen = (U8)strlen(mTperProperties[idx].name);

        if(slen < 0x10)
            dataBuf[iDataBuf++] = 0xA0 + slen;
        else
        {
            dataBuf[iDataBuf++] = 0xD0;
            dataBuf[iDataBuf++] = slen;
        }
        memcpy(&dataBuf[iDataBuf], mTperProperties[idx].name, slen);
        iDataBuf += slen;

        AtomEncoding_Integer((U8 *)&mTperProperties[idx].val, sizeof(mTperProperties[0].val));
        //D_PRINTF("%02X %08X\n", idx, mTperProperties[idx].val);

        dataBuf[iDataBuf++] = TOK_EndName;
    }

    dataBuf[iDataBuf++] = TOK_EndList;

    if(hostPtyCnt)
    {
        dataBuf[iDataBuf++] = TOK_StartName;
        dataBuf[iDataBuf++] = 0x00;
        dataBuf[iDataBuf++] = TOK_StartList;

        for(idx = 0; idx < HostPropertiesCnt; idx++)
        {
            dataBuf[iDataBuf++] = TOK_StartName;

            slen = (U8)strlen(mHostProperties[idx].name);

            if(slen < 0x10)
                dataBuf[iDataBuf++] = 0xA0 + slen;
            else
            {
                dataBuf[iDataBuf++] = 0xD0;
                dataBuf[iDataBuf++] = slen;
            }
            memcpy(&dataBuf[iDataBuf], mHostProperties[idx].name, slen);
            iDataBuf += slen;

            AtomEncoding_Integer((U8 *)&mHostProperties[idx].val, sizeof(mHostProperties[0].val));
            //souts("HostPty:");
            //soutd(mHostProperties[idx].val);
            dataBuf[iDataBuf++] = TOK_EndName;
        }

        dataBuf[iDataBuf++] = TOK_EndList;
        dataBuf[iDataBuf++] = TOK_EndName;
    }

    dataBuf[iDataBuf++] = TOK_EndList;
    dataBuf[iDataBuf++] = TOK_EndOfData;
}


/****************************************************************************
 * tcg_properties():  <--- H2TP_Properties();
 ****************************************************************************/
tcg_code U16 tcg_properties(req_t *req)
{
    U32 tmpHostPty[HostPropertiesCnt];
    U16 st, result, i;
    U8  hitHostPty[HostPropertiesCnt];
    U8  hostPtyCnt = 0;

    // DBG_P(1, 3, 0x820136);  //82 01 36, "[F]H2TP_Properties"
    TCGPRN("tcg_properties()\n");
    DBG_P(0x01, 0x03, 0x7100DD );  // tcg_properties()
    for(i = 0; i < HostPropertiesCnt; i++)
    {
        tmpHostPty[i] = mHostProperties[i].val;
        hitHostPty[i] = 0;
    }

    st = host_properties_parse(req, tmpHostPty, hitHostPty, &hostPtyCnt, &result);
    //printk("host_properties_parse() return st=%04x\n", st);
    DBG_P(0x2, 0x03, 0x71011C, 2, st);  // host_properties_parse() return st=%04x
    if(st == PROPERTY_PARSE_TOK_ERR)        goto TOKEN_ERROR;
    else if(st == PROPERTY_PARSE_ST_ERR)    goto STATUS_ERROR;

    //Success!!
    for(i = 0; i < HostPropertiesCnt; i++)
        mHostProperties[i].val = tmpHostPty[i];

    prepare_properties_response(hostPtyCnt);

    return STS_SUCCESS;

STATUS_ERROR:
    fill_no_data_token_list();
    return result;

TOKEN_ERROR:
    if(bControlSession)
        return STS_STAY_IN_IF_SEND;
    else
        return STS_SESSION_ABORT;
}

/***********************************************************
* Level0_Discovery
* Prepare Level 0 Discovery response data in *buf
* ref. core spec 3.3.6
***********************************************************/
tcg_code void Supported_Security_Protocol(U8* buf)
{
    U16 i, pt;

    pt = 0;
    //Reserved
    for (i = 0; i<6; i++)
        buf[pt++] = 0;

    //List Length
    buf[pt++] = 0x00;
#if _TCG_ == TCG_EDRV
    buf[pt++] = 0x04;

    //List
    buf[pt++] = 0x00;
    buf[pt++] = 0x01;
    buf[pt++] = 0x02;
    buf[pt++] = 0xEE;   //IEEE1667
#else
    buf[pt++] = 0x03;

    //List
    buf[pt++] = 0x00;
    buf[pt++] = 0x01;
    buf[pt++] = 0x02;
#endif

    return;
}

tcg_code void ResetSessionManager(req_t *req)
{
    U32 i;
    // DBG_P(1, 3, 0x820137);  //82 01 37, "[F]ResetSessionManager"
    mSessionManager.HostSessionID = 0;

    //for(i=0;i<mSessionManager.HtChallenge[0]; i++)
    for (i = 0; i <= CPIN_LENGTH; i++)
        mSessionManager.HtChallenge[i] = 0;

    mSessionManager.HtSgnAuthority.all = UID_Null;
    mSessionManager.SPID.all = UID_Null;

    //mSessionManager.SPSessionID = 0x1001;     //assigned by TPer

    mSessionManager.state = SESSION_CLOSE;
    mSessionManager.bWaitSessionStart = 0;
    mSessionManager.sessionTimeout = 0;
    mSessionManager.sessionStartTime = 0;

    if (mSessionManager.TransactionState == TRNSCTN_ACTIVE)
    {   //Transaction abort!!
        // ReadNAND2Mtable(req);
        if(flgs_MChnged.all32)
        {
            tcg_tbl_recovery(req);
        }
        ClearMtableChangedFlag();
    }
    mSessionManager.TransactionState = TRNSCTN_IDLE;

#if CO_SUPPORT_AES  && _TCG_DEBUG != TRUE
    if (TCG_ACT_IN_OPAL())
    {
        memset(OpalKDF, 0, sizeof(OpalKDF));
        memset(WrapKEK, 0, sizeof(WrapKEK));
    }
#endif
    //TODO: construct payload for response
    //dataBuf[iDataBuf++] = TOK_EndOfSession;
}

//
//LockingTbl Power-On Reset
//      It will check in each range and
// i) set 'ReadLocked' bit if 'ReadLockEnabled' is set,
// ii) set 'WriteLocked' bit if 'WriteLockEnabled' is set
//
tcg_code void LockingTbl_Reset(U8 type)
{
    U8 i, j;

    // DBG_P(1, 3, 0x820138);  //82 01 38, "[F]LockingTbl_Reset"

   for(i=0;i<=LOCKING_RANGE_CNT;i++)
   {
       for(j=1;j<=pG3->b.mLckLocking_Tbl.val[i].lockOnReset[0];j++)
       {
           if(pG3->b.mLckLocking_Tbl.val[i].lockOnReset[j]==type)
           {
               if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled)
                   pG3->b.mLckLocking_Tbl.val[i].readLocked=TRUE;

               if (pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled)
                   pG3->b.mLckLocking_Tbl.val[i].writeLocked=TRUE;

#if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE
                // Fetch range key...
                if (pG3->b.mWKey[i].state == TCG_KEY_UNWRAPPED)
                {
                    memcpy(&mRawKey[i].dek, &pG3->b.mWKey[i].dek, sizeof(pG3->b.mWKey[0].dek));
                    mRawKey[i].state = pG3->b.mWKey[i].state;
                }
                else
                {
                    memset(&mRawKey[i].dek, 0, sizeof(pG3->b.mWKey[0].dek));
                    mRawKey[i].state = (S32)TCG_KEY_NULL;
                }
                TCG_PRINTF("<RK> %02x-%08x-%08x\n", (U8)mRawKey[i].state, mRawKey[i].dek.aesKey[0], mRawKey[i].dek.aesKey[1]);
                // DBG_P(4, 3,0x820221, 1,i,  1,(U8)mRawKey[i].state, 4, mRawKey[i].dek.aesKey[0]); // 82 02 21, "<RK> %02X-%08X-%08X" 1 4 4
#endif
                break;
            }
        }
    }
}

tcg_code void tcg_backup_lockOnReset_params(void)
{
    U32 i;

    // CPIN
    for (i = 0; i < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; i++){
        bak_AdmCpin_tries[i] = pG1->b.mAdmCPin_Tbl.val[i].tries;
    }
    for (i = 0; i < pG3->b.mLckCPin_Tbl.hdr.rowCnt; i++){
        bak_LckCpin_tries[i] = pG3->b.mLckCPin_Tbl.val[i].tries;
    }

    //MBR Control
    bak_LckMbrCtrl_done = pG3->b.mLckMbrCtrl_Tbl.val[0].done;
    bak_mTcgStatus = mTcgStatus;

    // Locking
    for(i = 0; i <= LOCKING_RANGE_CNT; i++){
        bak_LckLocking_readLocked[i] = pG3->b.mLckLocking_Tbl.val[i].readLocked;
        bak_LckLocking_writeLocked[i] = pG3->b.mLckLocking_Tbl.val[i].writeLocked;
        #if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE
        memcpy(&bak_RawKey_dek[i], &mRawKey[i].dek, sizeof(pG3->b.mWKey[0].dek));
        bak_RawKey_state[i] = mRawKey[i].state;
        #endif
    }
}

tcg_code void tcg_restore_lockOnReset_params(void)
{
    U32 i;

    // CPIN
    for (i = 0; i < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; i++){
        pG1->b.mAdmCPin_Tbl.val[i].tries = bak_AdmCpin_tries[i];
    }
    for (i = 0; i < pG3->b.mLckCPin_Tbl.hdr.rowCnt; i++){
         pG3->b.mLckCPin_Tbl.val[i].tries = bak_LckCpin_tries[i];
    }

    //MBR Control
    pG3->b.mLckMbrCtrl_Tbl.val[0].done = bak_LckMbrCtrl_done;
    mTcgStatus = bak_mTcgStatus;

    // Locking
    for(i = 0; i <= LOCKING_RANGE_CNT; i++){
        pG3->b.mLckLocking_Tbl.val[i].readLocked = bak_LckLocking_readLocked[i];
        pG3->b.mLckLocking_Tbl.val[i].writeLocked = bak_LckLocking_writeLocked[i];
        #if CO_SUPPORT_AES  //_TCG_!=TCG_PYRITE
        memcpy(&mRawKey[i].dek, &bak_RawKey_dek[i], sizeof(pG3->b.mWKey[0].dek));
        mRawKey[i].state = bak_RawKey_state[i];
        #endif
    }
}

//
// Compared the intended Locking Table uid/rangeStart/rangeLength with the other ranges.
// return zOK if there is no overlap with the other effective ranges (i.e., its rangeLength should not be zero),
// otherwise return zNG.
//
tcg_code int LockingTbl_RangeChk(U64 uid, U64 rangeStart, U64 rangeLength)
{
    U64 rangeEnd = rangeStart;
    U64 tmpRangeStart, tmpRangeLength, tmpRangeEnd;
    U8 i;

    // DBG_P(1, 3, 0x820139);  //82 01 39, "[F]LockingTbl_RangeChk"

    //if(rangeStart%(CFG_UDATA_PER_PAGE/LBA_SIZE_IN_BYTE))
    if (rangeStart%TCG_AlignmentGranularity)
    {
        // tcg_soutb(0xF0);    tcg_sout64(rangeStart); //tcg_souts("!!NG: StartAlignment");
#if _TCG_ == TCG_EDRV
        if (bEHddLogoTest==FALSE)
        {
#if 1  // for new version HLK(1703) Fuzz fail || HLK1607 Crossing fail
            if ((mPsidRevertCnt > 0x10) || ((mPsidRevertCnt > 5) && (rangeStart == 0x14) && (rangeLength == 0x14)))
            {
                bEHddLogoTest = TRUE;
            }
        #else  // old version HLK
            if ((mPsidRevertCnt > 5) && (rangeStart==0x14) && (rangeLength==0x14))
                bEHddLogoTest = TRUE;  //LogoTest, skip alignment check
        #endif
        }

        if (bEHddLogoTest == FALSE)
#endif
            return zNG; //not align
    }

    //if(rangeLength%(CFG_UDATA_PER_PAGE/LBA_SIZE_IN_BYTE))
    if(rangeLength%TCG_AlignmentGranularity)
    {
#if _TCG_ == TCG_EDRV
        if (bEHddLogoTest == FALSE)
#endif
        return zNG; // not align
    }

    if (rangeLength) // !=0
        rangeEnd += (rangeLength - 1);
    else
    {
        return zOK; //TODO: need to check... No LBAs are covered by this range (please check Test Case 3.3.4, D4-1-3-3-1)
    }

    for (i = 1; i <= LOCKING_RANGE_CNT; i++)   // skip GloblaRange here
    {
        if(uid==pG3->b.mLckLocking_Tbl.val[i].uid)
            continue;

        tmpRangeStart = pG3->b.mLckLocking_Tbl.val[i].rangeStart;
        tmpRangeLength = pG3->b.mLckLocking_Tbl.val[i].rangeLength;

        if (tmpRangeLength == 0) // No LBAs are covered by this range (please check Test Case 3.3.4, D4-1-3-3-1)
            continue;

        tmpRangeEnd = tmpRangeStart + (tmpRangeLength - 1);

        // check if there is any overlap between these two ranges:
        //  a. rangeStart is in this range
        //  b. rangeEnd is in this range
        //  c. rangeStart is smaller than tmpRangeStart and rangeEnd is larger than tmpRangeEnd
        if ((rangeStart >= tmpRangeStart) && (rangeStart <= tmpRangeEnd))
        {
            return zNG;
        }
        if ((rangeEnd >= tmpRangeStart) && (rangeEnd <= tmpRangeEnd))
        {
            return zNG;
        }
        if ((rangeStart <= tmpRangeStart) && (rangeEnd >= tmpRangeEnd))
        {
            return zNG;
        }
    }

    return zOK;
}

//
// Update "mReadLockedTable[]" and "mWriteLockedTable[]" for easier/faster ATA Kernel access.
//
//      These two tables are extracted from "pG3->b.mLckLocking_Tbl" for Media Read/Write control.
//      The object sequence in these tables are sorted according to the "ragneStart" in "pG3->b.mLckLocking_Tbl".
//      LockingTbl_RangeChk() should be run to avoid range overlapping.
//
//      Table Object:     [rangeNo, rangeStart, rangeEnd]
//      rangeNo=0 for GlobalRange, and it should be located at the last effective row in the tables if it is locked.
//
tcg_code void LockingRangeTable_Update(void)
{
#if (_TCG_ != TCG_PYRITE)
    U64 tmpRangeStart, tmpRangeLength, tmpRangeEnd;
    U8 i, k, iSort, rangeCnt = 0;

    // DBG_P(1, 3, 0x82013A);  //82 01 3A, "[F]LockingRangeTable_Update"
    //printk("LockingRangeTable_Update()\n");
    DBG_P(0x01, 0x03, 0x7100DE );  // LockingRangeTable_Update()
    mReadLockedStatus = 0;
    mWriteLockedStatus = 0;

    //establish the table ...
    for (i = 1; i <= LOCKING_RANGE_CNT; i++)   //skip GloblaRange here
    {
        //check if this is an effective range
        tmpRangeLength = pG3->b.mLckLocking_Tbl.val[i].rangeLength;
        //D_PRINTF("** %02X %08X\n", i, tmpRangeLength);
        if (tmpRangeLength == 0)    //not an effective range, skip
            continue;

        tmpRangeStart = pG3->b.mLckLocking_Tbl.val[i].rangeStart;
        tmpRangeEnd =  tmpRangeStart + (tmpRangeLength-1);

        //sorting in "pLockingRangeTable[]"
        for (iSort = 0; iSort<rangeCnt; iSort++)
        {
            if (tmpRangeStart<pLockingRangeTable[iSort].rangeStart)
                break;
        }

        if (rangeCnt) // previous range data existed in the table, update them first
        {
            for (k = rangeCnt; k>iSort; k--)
            {
                pLockingRangeTable[k].rangeNo = pLockingRangeTable[k - 1].rangeNo;
                pLockingRangeTable[k].rangeStart = pLockingRangeTable[k - 1].rangeStart;
                pLockingRangeTable[k].rangeEnd = pLockingRangeTable[k - 1].rangeEnd;
                pLockingRangeTable[k].blkcnt = pLockingRangeTable[k - 1].blkcnt;
                pLockingRangeTable[k].readLocked = pLockingRangeTable[k - 1].readLocked;
                pLockingRangeTable[k].writeLocked = pLockingRangeTable[k - 1].writeLocked;
            }
        }

        //add the new object
        pLockingRangeTable[iSort].rangeNo = i;
        pLockingRangeTable[iSort].rangeStart = tmpRangeStart;
        pLockingRangeTable[iSort].rangeEnd = tmpRangeEnd;
        pLockingRangeTable[iSort].blkcnt = (U32)tmpRangeLength;  //added for AltaPlus
        // DBG_P(3, 3, 0x820193, 1, (U8)i, 4, pLockingRangeTable[iSort].blkcnt);  //82 01 93, "bct: %X %X", 1 4

        if(pG3->b.mLckLocking_Tbl.val[i].readLockEnabled && pG3->b.mLckLocking_Tbl.val[i].readLocked)
        { //this range is Read-Locked
            pLockingRangeTable[iSort].readLocked = 0x01;
            mReadLockedStatus |= (0x01 << i);
        }
        else
            pLockingRangeTable[iSort].readLocked = 0x00;

        if(pG3->b.mLckLocking_Tbl.val[i].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[i].writeLocked)
        { //this range is Write-Locked
            pLockingRangeTable[iSort].writeLocked = 0x01;
            mWriteLockedStatus |= (0x01 << i);
        }
        else
            pLockingRangeTable[iSort].writeLocked = 0x00;

        rangeCnt++;
    }

    //add GlobalRange as the last object
    pLockingRangeTable[rangeCnt].rangeNo = 0;
    pLockingRangeTable[rangeCnt].rangeStart = 0;
    pLockingRangeTable[rangeCnt].rangeEnd = 0;
    pLockingRangeTable[rangeCnt].blkcnt = 0xffffffff;

    if(pG3->b.mLckLocking_Tbl.val[0].readLockEnabled && pG3->b.mLckLocking_Tbl.val[0].readLocked)
    { //global range is Read-Locked
        pLockingRangeTable[rangeCnt].readLocked = 0x01;
        mReadLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[rangeCnt].readLocked = 0x00;

    if(pG3->b.mLckLocking_Tbl.val[0].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[0].writeLocked)
    { //global range is Write-Locked
        pLockingRangeTable[rangeCnt].writeLocked = 0x01;
        mWriteLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[rangeCnt].writeLocked = 0x00;

    if (rangeCnt != LOCKING_RANGE_CNT)
    {
        // update the last row for TcgRangeCheck()
        pLockingRangeTable[LOCKING_RANGE_CNT].rangeNo = 0;
        pLockingRangeTable[LOCKING_RANGE_CNT].rangeStart = 0;
        pLockingRangeTable[LOCKING_RANGE_CNT].rangeEnd = 0;
        pLockingRangeTable[LOCKING_RANGE_CNT].readLocked = pLockingRangeTable[rangeCnt].readLocked;
        pLockingRangeTable[LOCKING_RANGE_CNT].writeLocked = pLockingRangeTable[rangeCnt].writeLocked;
    }
#else
    //printk("LockingRangeTable_Update()\n");
    DBG_P(0x01, 0x03, 0x7100DE );  // LockingRangeTable_Update()
    mReadLockedStatus = 0;
    mWriteLockedStatus = 0;

    pLockingRangeTable[0].rangeNo = 0;
    pLockingRangeTable[0].rangeStart = 0;
    pLockingRangeTable[0].rangeEnd = 0;
    pLockingRangeTable[0].blkcnt= 0xffffffff;

    if(pG3->b.mLckLocking_Tbl.val[0].readLockEnabled && pG3->b.mLckLocking_Tbl.val[0].readLocked)
    { //global range is Read-Locked
        pLockingRangeTable[0].readLocked = 0x01;
        mReadLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[0].readLocked = 0x00;

    if(pG3->b.mLckLocking_Tbl.val[0].writeLockEnabled && pG3->b.mLckLocking_Tbl.val[0].writeLocked)
    { //global range is Write-Locked
        pLockingRangeTable[0].writeLocked = 0x01;
        mWriteLockedStatus |= 0x01;
    }
    else
        pLockingRangeTable[0].writeLocked = 0x00;
#endif
    //++rangeCnt;
#if CO_SUPPORT_AES
    HAL_SEC_InitAesKeyRng();
#endif

    DumpRangeInfo();
    // Need to take care of MBR-S condition ...
}

// reset CPin tries count
void CPinTbl_Reset(void)
{
    U8 j;

    for (j = 0; j < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; j++)
    {
        pG1->b.mAdmCPin_Tbl.val[j].tries = 0;
    }

    for (j = 0; j < pG3->b.mLckCPin_Tbl.hdr.rowCnt; j++)
    {
        pG3->b.mLckCPin_Tbl.val[j].tries = 0;
    }
}

//
//MbrCtrlTbl Power-On or Porgrammatic Reset
//
//  set 'Done' bit according to reset type and DoneOnReset setting
//
tcg_code void MbrCtrlTbl_Reset(U8 type)
{
    U8 i;

    for(i=1;i<=pG3->b.mLckMbrCtrl_Tbl.val[0].doneOnReset[0];i++)
    {
        if(pG3->b.mLckMbrCtrl_Tbl.val[0].doneOnReset[i]==type)
        {
            pG3->b.mLckMbrCtrl_Tbl.val[0].done=FALSE;
            break;
        }
    }

    if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable==TRUE)
     &&(pG3->b.mLckMbrCtrl_Tbl.val[0].done==FALSE))
        mTcgStatus |= MBR_SHADOW_MODE;
    else
        mTcgStatus &= (~MBR_SHADOW_MODE);

    //DBG_P(2, 3, 0x8201C2, 4, mTcgStatus);  //82 01 C2, "mTcgStatus= %X", 4
    return;
}

/*****************************************************
 * preformat
 *****************************************************/
extern void cb_Purge_Ipc_Cmd_issue(req_t*);
extern int tcg_if_onetime_init(void);
tcg_code bool cb_tcgpreformat(Complete)(req_t *req)
{
    NvmeAdminVendorCommand_t *cmd = (NvmeAdminVendorCommand_t *)req->host_cmd;

    TCGPRN("cb_tcgpreformat_Complete()\n");
    DBG_P(0x01, 0x03, 0x7100DF );  // cb_tcgpreformat_Complete()
    tcg_if_onetime_init(); //reset and clear cache

    if(cmd->VscDW12.VscMode == VSC_PU_PREFORMAT){
         cb_Purge_Ipc_Cmd_issue(req);
         return TRUE;
    }
    return FALSE;
}

tcg_code bool cb_tcgpreformat(TcgNFInit)(req_t *req)
{
    TCGPRN("cb_tcgpreformat_TcgNFInit()\n");
    DBG_P(0x01, 0x03, 0x7100E0 );  // cb_tcgpreformat_TcgNFInit()
    if(ChkDefaultTblPattern() == zOK)
    {
        tcg_ipc_post(req, MSG_TCG_NF_CPU_INIT, cb_tcgpreformat(Complete));
    }
    else
    {
        TCG_ERR_PRN("Error!! Default Table Error, Power Off Please.\n");
        DBG_P(0x01, 0x03, 0x7F7F08);  // Error!! Default Table Error, Power Off Please.
        // DBG_P(1, 3, 0x820014);  //82 00 14, "!!!Error, Default table error, power off."
        cb_tcgpreformat(Complete)(req);
    }
    return TRUE;
}

tcg_code bool cb_tcgpreformat(TcgEEWr)(req_t *req)
{
    TCGPRN("cb_tcgpreformat_TcgEEWr()\n");
    DBG_P(0x01, 0x03, 0x7100E2 );  // cb_tcgpreformat_TcgEEWr()

    DumpG5DftAmount();
    // ID : tcg pre-format ID for tcgInit()
    // strcpy(preformat_id_string, PREFORMAT_STRING);
    // ID : record defected ID in NOR
    if(strcmp((char *)tcgDefectID, DEFECT_STRING) != 0)
    {   // first time ? , if so, then init defect table
        strcpy((char *)tcgDefectID,DEFECT_STRING);          // ID
    }
    // ID : record erased count ID in NOR
    if(strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0)
    {   // first time ? , if so, then init erased count table
        strcpy((char *)tcgErasedCntID,ERASED_CNT_STRING);   // ID
    }

    if((gTcgG4Defects > TCG_MBR_CELLS/2)
    || (gTcgG5Defects > TCG_MBR_CELLS/2))
    {
        //tcg_souts("Error!!, There are a lot of defect blocks. TCG function off.");
        // DBG_P(1, 3, 0x820013);  //82 00 13, "!!!Error, There are a lot of defect blocks. TCG function off."
        bTcgTblErr = TRUE;
        cb_tcgpreformat(Complete)(req);
        return FALSE;
    }

    // DBG_P(2, 3, 0x8201E2);  //82 01 E2, "Erase EEPROM G4 & G5 Valid Blk Table"
    memset(tcg_phy_valid_blk_tbl, 0xFF, sizeof(tcg_phy_valid_blk_tbl));
    memset(TCGBlockNo_ee2, 0xFF, sizeof(TCGBlockNo_ee2));
    tcg_phy_valid_blk_tbl_tag = 0xFFFFFFFF;

    strcpy((char *)tcg_prefmt_tag, PREFORMAT_END_TAG);

    SI_Synchronize_Externel(SI_AREA_BIT_TCG, SYSINFO_WRITE, SI_SYNC_BY_SYSINFO, req, cb_tcgpreformat(TcgNFInit));
    return TRUE;
}

tcg_code bool cb_tcgpreformat(G5BuildDefect)(req_t *req)
{
    TCGPRN("cb_tcgpreformat_G5BuildDefect()\n");
    DBG_P(0x01, 0x03, 0x7100E3 );  // cb_tcgpreformat_G5BuildDefect()
    DumpG4DftAmount();
    tcg_ipc_post(req, MSG_TCG_G5BUILDDEFECT, cb_tcgpreformat(TcgEEWr));
    return TRUE;
}

tcg_code bool cb_tcgpreformat(G4BuildDefect)(req_t *req)
{
    TCGPRN("cb_tcgpreformat_G4BuildDefect()\n");
    DBG_P(0x01, 0x03, 0x7100E4 );  // cb_tcgpreformat_G4BuildDefect()
    tcg_ipc_post(req, MSG_TCG_G4BUILDDEFECT, cb_tcgpreformat(G5BuildDefect));
    return TRUE;
}

tcg_code bool cb_tcgpreformat(G4RdDefault)(req_t *req)
{
    TCGPRN("cb_tcgpreformat_G4RdDefault()\n");
    DBG_P(0x01, 0x03, 0x7100E5 );  // cb_tcgpreformat_G4RdDefault()
    tcg_ipc_post(req, MSG_TCG_G4RDDEFAULT, cb_tcgpreformat(G4BuildDefect));
    return TRUE;
}

tcg_code bool cb_tcgpreformat(Begin)(req_t *req)
{
    TCGPRN("cb_tcgpreformat_Begin()\n");
    DBG_P(0x01, 0x03, 0x7100E6 );  // cb_tcgpreformat_Begin()
    cb_tcgpreformat(G4RdDefault)(req);
    return TRUE;
}


tcg_code void TcgPreformatAndInit(req_t *req)
{
    // U32 x;

    TCGPRN("TcgPreformatAndInit()\n");
    DBG_P(0x01, 0x03, 0x7100E7 );  // TcgPreformatAndInit()
    strcpy((char *)tcg_prefmt_tag, PREFORMAT_START_TAG);


    #ifdef FORCE_TO_CLEAR_ERASED_COUNT
    memset((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));   // force clear G4 erased count
    memset((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));   // force clear G5 erased count
    #endif

    if(strcmp((char *)tcgDefectID, DEFECT_STRING) != 0){   // first time ? , if so, then init defect table
        memset((void *)tcgG4Dft, 0, sizeof(tcgG4Dft));   // force clear G4 defect table
        memset((void *)tcgG5Dft, 0, sizeof(tcgG5Dft));   // force clear G5 defect table
        // DBG_P(1, 3, 0x8201C3);  //82 01 C3, ">>> Defect table is cleared."
    }

    if(strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0){   // first time ? , if so, then init erased count table
        memset((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));   // force clear G4 erased count table
        memset((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));   // force clear G5 erased count table
        // DBG_P(1, 3, 0x8201C4);  //82 01 C4, ">>> Erase count table is cleared."
    }

#if 1
    cb_tcgpreformat(Begin)(req);
#else
    TcgFuncRequest1(MSG_TCG_G4BUILDDEFECT);
    DumpG4DftAmount();

    TcgFuncRequest1(MSG_TCG_G5BUILDDEFECT);
    DumpG5DftAmount();

    // ID : tcg pre-format ID for tcgInit()
    strcpy(preformat_id_string,PREFORMAT_STRING);
    // ID : record defected ID in NOR
    if(strcmp((char *)tcgDefectID, DEFECT_STRING) != 0)
    {   // first time ? , if so, then init defect table
        strcpy((char *)tcgDefectID,DEFECT_STRING);          // ID
    }
    // ID : record erased count ID in NOR
    if(strcmp((char *)tcgErasedCntID, ERASED_CNT_STRING) != 0)
    {   // first time ? , if so, then init erased count table
        strcpy((char *)tcgErasedCntID,ERASED_CNT_STRING);   // ID
    }

    if((gTcgG4Defects > TCG_MBR_CELLS/2)
    || (gTcgG5Defects > TCG_MBR_CELLS/2))
    {
        //tcg_souts("Error!!, There are a lot of defect blocks. TCG function off.");
        // DBG_P(1, 3, 0x820013);  //82 00 13, "!!!Error, There are a lot of defect blocks. TCG function off."
        bTcgTblErr = TRUE;
        return;
    }

    // DBG_P(2, 3, 0x8201E2);  //82 01 E2, "Erase EEPROM G4 & G5 Valid Blk Table"
    memset(smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL, 0xFF, sizeof(smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL));
    memset(smSysInfo->d.MiscData.d.TCGUsed.TCGBlockNo_EE, 0xFF, sizeof(smSysInfo->d.MiscData.d.TCGUsed.TCGBlockNo_EE));
    smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL_TAG = 0xFFFFFFFF;

    strcpy((char *)tcg_prefmt_tag, PREFORMAT_END_TAG);
#ifdef TCG_EEP_NOR
    TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#else
    SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE_FORCE);
#endif

    if(IsTcgInit == TRUE)
    {
        if(ChkDefaultTblPattern() == zOK)
        {
            TcgFuncRequest1(MSG_TCG_CPU2_TCGINIT);
            TcgInit_Cpu0(cInitBootPowerDown, TRUE); //reset and clear cache
        }
        else
        {
            // DBG_P(1, 3, 0x820014);  //82 00 14, "!!!Error, Default table error, power off."
        }
    }
#endif
}

tcg_code void tcg_if_post_sync_request(void)
{
    tcg_sync.b.if_sync_req  = TRUE;
    tcg_sync.b.nf_sync_resp = FALSE;
}

tcg_code void tcg_if_post_sync_response(void)
{
    // pLockingRangeTable      = (enabledLockingTable_t *)((U32)mLockingRangeTable + (U32)CPU1_BTCM_SYS_BASE - (U32)CPUx_BTCM_SYS_BASE);
    tcg_sync.b.nf_sync_req  = FALSE;
    tcg_sync.b.if_sync_resp = TRUE;
}

tcm_code bool isTcgIfReady(void)
{
    if(tcg_sync.b.nf_sync_req == FALSE && tcg_sync.b.if_sync_resp == TRUE)
        return TRUE;
    else
        return FALSE;
}


// tcg_code int tcg_if_post_sync_sign(void)
// {
    // pLockingRangeTable      = (enabledLockingTable_t *)((U32)mLockingRangeTable | (U32)CPU1_BTCM_SYS_BASE);
    // if_cpu_sync_post_sign   = TCG_SYNC_OK;
    // return zOK;
// }

//-------------------------------------------------------------------
// Function     : U8 SYSINFO_Synchronize(U32 dwOperateArea, U8 bDir)
// Description  : Sysinfo Synchronize
// Input        : OperateArea , Dir
// return       : N/A
//-------------------------------------------------------------------
tcg_code static int TcgPrepareH2CMsgContainer(tMSG_TCG** pTcgMsg, tMSG_TCG* pMyTcgMsg)
{
#ifndef alexcheck
    tMSG_TCG* pTcgMsg_Tmp = (tMSG_TCG*)&smShareMsg[TCG_H2C_MSG_IDX];
    memset(pTcgMsg_Tmp, 0, sizeof(tMSG_TCG));
    if(pMyTcgMsg != NULL){
        memcpy((U8 *)pTcgMsg_Tmp, (U8 *)pMyTcgMsg, sizeof(tMSG_TCG));  //copy MSG
    }
    pTcgMsg_Tmp->hdr.b.opCode = cMcTcg;
    pTcgMsg_Tmp->hdr.b.status = cMsgPosted;
    pTcgMsg_Tmp->hdr.b.gpCode = cMgHostNorm;
    pTcgMsg_Tmp->hdr.b.cq     = 0;
    *pTcgMsg = pTcgMsg_Tmp;
#endif
    return zOK;
}



tcg_code static int TcgFuncRequest1(MSG_TCG_SUBOP_t subOP)
{
    tMSG_TCG* pTcgMsg = NULL;
#ifndef alexcheck
    if (subOP == MSG_TCG_CLR_CACHE || subOP == MSG_TCG_INIT_CACHE || subOP == MSG_TCG_ZERO_REBUILD){
        TcgPrepareH2CMsgContainer(&pTcgMsg, NULL);
        pTcgMsg->hdr.b.opCode = cMcResetCache;

        pTcgMsg->subOpCode = subOP;
        pTcgMsg->param[0]  = RST_CACHE_INIT;
        pTcgMsg->hdr.b.status = cMsgHighPrtyPosted;

        pTcgMsg->hdr.b.cq = 1;
        IPC_SendMsgQ(cH2cReqQueue, (Msg_t*) pTcgMsg);  //send MSG to CPU2(Core)

        while (pTcgMsg->hdr.b.status != cMsgCompleted);
        if(pTcgMsg->error == EC_NO_ERROR)
        {
            return zOK;
        }
        else
        {
            return zNG;
        }
    }

    TcgPrepareH2CMsgContainer(&pTcgMsg, NULL);

    pTcgMsg->subOpCode = subOP;
    pTcgMsg->hdr.b.status = cMsgHighPrtyPosted;

    pTcgMsg->hdr.b.cq = 1;
    if ((subOP==MSG_TCG_G3WR) && bTcgKekUpdate)
    {
        pTcgMsg->param[1] = 0xEE;
        bTcgKekUpdate = FALSE;
    }
    IPC_SendMsgQ(cH2cReqQueue, (Msg_t*) pTcgMsg);  //send MSG to CPU2(Core)

    while (pTcgMsg->hdr.b.status != cMsgCompleted);
    if(pTcgMsg->error == EC_NO_ERROR){
        return zOK;
    }else{
        return zNG;
    }
#else
    TcgPrepareH2CMsgContainer(&pTcgMsg, NULL);
    return zOK;
#endif
}


// #if _TCG_ != TCG_PYRITE
tcm_code U16 TcgRangeCheck(U32 lbaStart, U32 sc, bool writemode)
{
    if (mTcgStatus & TCG_ACTIVATED) //TCG is activated
    {
        bool   startChecked=FALSE, globalChecked=FALSE;
        U32    i;
        U32    lbaEnd;

        lbaEnd = lbaStart + sc - 1;
        if (mTcgStatus & MBR_SHADOW_MODE)
        {
        #if (MUTI_LBAF == TRUE)
            U32 SMBR128M = 0x8000000 / smLogicBlockSize;
        #else
            U32 SMBR128M = 0x40000;
        #endif
            if (lbaEnd >= SMBR128M) //TODO: data '0' for read
            {
                if(lbaStart < SMBR128M)
                {
                    // DBG_P(1, 3, 0x8201C7);  //82 01 C7, "!!MS X"
                    return TCG_DOMAIN_ERROR;
                }
                //else
                //{
                //    if(!writemode)
                //    {     return TCG_DOMAIN_DUMMY;    }
                //}
            }
            else //<128M
            {
                if(writemode)
                {
                    // DBG_P(1, 3, 0x8201C8);  //82 01 C8, "!!MS W"
                    return TCG_DOMAIN_ERROR;
                }
                else
                {
                    // DBG_P(1, 3, 0x8201C9);  //82 01 C9, "**MS"
                    return TCG_DOMAIN_SHADOW;
                }
            }
        }

        for (i=0; i <=LOCKING_RANGE_CNT; i++)
        {
            if(pLockingRangeTable[i].rangeNo == 0)
            { //Global Range: last row
                if(globalChecked==FALSE)
                {
                    if(((!writemode)&&pLockingRangeTable[LOCKING_RANGE_CNT].readLocked) || (writemode&&pLockingRangeTable[LOCKING_RANGE_CNT].writeLocked))
                    { //range is read / write locked!
                        // DBG_P(1, 3, 0x8201CA);  //82 01 CA, "!! R0 Lck"
                        if ((!writemode) && (mTcgStatus & MBR_SHADOW_MODE)){
                            return TCG_DOMAIN_DUMMY;
                        }
                        return TCG_DOMAIN_ERROR;
                    }
                    else
                        return TCG_DOMAIN_NORMAL;
                }
                else
                    return TCG_DOMAIN_NORMAL;
            }


            if(startChecked==FALSE)
            { // find startLBA range first
                if(lbaStart<=pLockingRangeTable[i].rangeEnd)
                { //startLBA range is found!
                    startChecked=TRUE;

                    if(lbaStart>=pLockingRangeTable[i].rangeStart)
                    { //startLBA at Tbl[i]
                        if(((!writemode)&&pLockingRangeTable[i].readLocked)|| (writemode&&pLockingRangeTable[i].writeLocked))
                        { //range is read / write locked!
                            // DBG_P(2, 3, 0x8201CB, 1, (U8)pLockingRangeTable[i].rangeNo);  //82 01 CB, "!! R%02X Lck", 1
                            if ((!writemode) && (mTcgStatus & MBR_SHADOW_MODE)){
                                return TCG_DOMAIN_DUMMY;
                            }
                            return TCG_DOMAIN_ERROR;
                        }
                    }

                    else
                    { //startLBA @ global range
                        globalChecked=TRUE;
                        if(((!writemode)&&pLockingRangeTable[LOCKING_RANGE_CNT].readLocked)|| (writemode&&pLockingRangeTable[LOCKING_RANGE_CNT].writeLocked))
                        { //range is read / write locked!
                            // DBG_P(1, 3, 0x8201CC);  //82 01 CC, "!! R00 Lck"
                            return TCG_DOMAIN_ERROR;
                        }
                    }

                    //check if lbaEnd is at this range or not
                    if(lbaEnd<=pLockingRangeTable[i].rangeEnd)
                    { // endLBA range is found!
                        //endChecked = TRUE;

                        if(lbaEnd>=pLockingRangeTable[i].rangeStart)
                        { //endLBA at Tbl[i]
                            if(globalChecked==TRUE)
                            { // startLBA is at global range
                                if(((!writemode)&&pLockingRangeTable[i].readLocked)|| (writemode&&pLockingRangeTable[i].writeLocked))
                                { //range is read / write locked!
                                    // DBG_P(2, 3, 0x8201CD, 1, (U8)pLockingRangeTable[i].rangeNo);  //82 01 CD, "!!@ R%02X Lck", 1
                                    return TCG_DOMAIN_ERROR;
                                }
                                else
                                    return TCG_DOMAIN_NORMAL;

                            }
                            //else startLBA is at Tbl[i] (already checked)
                        }
                        //else    endLBA at global range (already checked)

                        return TCG_DOMAIN_NORMAL;
                    }
                }
            }

#if 1 //def TCG_EDRIVE
            else //if(endChecked==FALSE)
            { // find endLBA range
                if(globalChecked==FALSE)
                { // check if there is gap between ranges...
                    if(pLockingRangeTable[i].rangeStart!=(pLockingRangeTable[i-1].rangeEnd+1))
                    {
                        globalChecked=TRUE;
                        if(((!writemode)&&pLockingRangeTable[LOCKING_RANGE_CNT].readLocked)|| (writemode&&pLockingRangeTable[LOCKING_RANGE_CNT].writeLocked))
                        { //range is read / write locked!
                            // DBG_P(1, 3, 0x8201CE);  //82 01 CE, "!!@ R00 Lck"
                            return TCG_DOMAIN_ERROR;
                        }
                    }
                }

                if(lbaEnd>=pLockingRangeTable[i].rangeStart)
                { // endLBA  passed Tbl[i]
                    if(((!writemode)&&pLockingRangeTable[i].readLocked)|| (writemode&&pLockingRangeTable[i].writeLocked))
                    { //range is read / write locked!
                        // DBG_P(2, 3, 0x8201CF, 1, (U8)pLockingRangeTable[i].rangeNo);  //82 01 CF, "!!# R%02X Lck", 1
                        return TCG_DOMAIN_ERROR;
                    }

                    if(lbaEnd<=pLockingRangeTable[i].rangeEnd)
                    {
                        //endChecked=TRUE;
                        return TCG_DOMAIN_NORMAL;
                    }

                }
            }
#endif
        }
    }

    return TCG_DOMAIN_NORMAL;
}
// #endif


#if TCG_FS_PSID
tcg_code int TcgForcePSIDSetToDefault(void)
{
    printk("[F]TcgForcePSIDSetToDefault");
    DBG_P(0x01, 0x03, 0x710121 );  // [F]TcgForcePSIDSetToDefault

    *(U32*)tcg_ee_Psid = CPIN_NULL;   //clear tag
    //memcpy((U8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, (U8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));

    // Gen PSID Salt & PBDKF
    //Tcg_GenCPinHash(pG1->b.mAdmCPin_Tbl.val[CPIN_MSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);

    return zOK;
}

tcg_code int  TcgPsidVerify(void)
{
    if (*(U32*)tcg_ee_Psid == CPIN_IN_PBKDF)
    {   // table vs. EEPROM
        if (memcmp((U8*)tcg_ee_Psid, (U8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin)))
        {
            // DBG_P(1, 3, 0x820026);   // 82 00 26, "!!!Error, PSID tag exist but table PSID != EE PSID"
            TCG_ERR_PRN("!!! table vs. EEPROM Err !!!\n");
            DBG_P(0x01, 0x03, 0x7F7F09);  // !!! table vs. EEPROM Err !!!
            bTcgTblErr = TRUE;
        }
    }
    else
    {   // table vs. MSID
        if (CPinMsidCompare(CPIN_PSID_IDX) == zNG){
            // DBG_P(1, 3, 0x820027);   // 82 00 27, "!!!Error, PSID tag doesn't exist but table PSID != MSID"
            // for Eric EL Lin requirement to reduce risks.
            // TcgForcePSIDSetToDefault();
            TCG_ERR_PRN("!!! table vs. MSID Err !!!\n");
            DBG_P(0x01, 0x03, 0x7F7F0A);  // !!! table vs. MSID Err !!!
            bTcgTblErr = TRUE;
        }
    }
    return zOK;
}

tcg_code void TcgPsidBackup(void)
{
    // Backup PSID
    if ((bTcgTblErr==FALSE)
     && (pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_Tag==CPIN_IN_PBKDF))
    {
        memcpy((U8*)tcg_ee_Psid, (U8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));
    }
}

tcg_code void TcgPsidRestore(void)
{
    if (*(U32*)tcg_ee_Psid == CPIN_IN_PBKDF){
        memcpy((U8*)&pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin, (U8*)tcg_ee_Psid, sizeof(pG1->b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin));
    }else{
        DBG_P(1, 3, 0x7200D6);   // 72 00 D6, "= Default PSID ="
    }
}
#endif

tcg_code void TcgBackup4Preformat(void)
{
#ifndef alexcheck
    if((TcgSuBlkExist) && (bTcgTblErr == FALSE))
        TcgFuncRequest1(MSG_TCG_G4RDDEFAULT);   //if TCG super is existed, G1,G2 & G3 should be C table.

    TcgFuncRequest1(MSG_TCG_NOREEP_INIT);

    // backup TcgEEP
    memcpy(tcgTmpBuf, &tcgDevTyp, sizeof(smSysInfo->d.MiscData.d.TCGUsed));
#endif
}

tcg_code void TcgRestoreFromPreformat(void)
{
#ifndef alexcheck
    ASSERT(smSysInfo->d.MiscData.d.TCGBlockTag == SI_MISC_TAG_TCGINFO);

    TcgSuBlkExist = TRUE;
    bTcgTblErr = FALSE;

    // restore TcgEEP
    memcpy(&tcgDevTyp, tcgTmpBuf, sizeof(smSysInfo->d.MiscData.d.TCGUsed));
    TcgFuncRequest1(MSG_TCG_NOREEP_WR);
#endif
}

#if (MUTI_LBAF == TRUE)
tcg_code int TcgUpdateLogicBlk2LockingTbl()
{
    if(pG2->b.mLckLockingInfo_Tbl.val[0].logicalBlockSize != smLogicBlockSize){
        // DBG_P(4, 3, 0x8201DF, 2, pG2->b.mLckLockingInfo_Tbl.val[0].logicalBlockSize, 4, smLogicBlockSize, 1, (U8)TCG_AlignmentGranularity);  //82 01 DF, "Fmt Multi LBA tblLogicBlockSize[%04X] smLogicBlockSize[%08X] TCG_AlignmentGranularity[%02X]", 2 4 1
        pG2->b.mLckLockingInfo_Tbl.val[0].logicalBlockSize     = smLogicBlockSize;    //LBU_SIZE = smLogicBlockSize
        pG2->b.mLckLockingInfo_Tbl.val[0].alignmentGranularity = TCG_AlignmentGranularity;
        return TcgFuncRequest1(MSG_TCG_G2WR);   //WaitG2Wr();
    }
    return zOK;
}
#endif

tcg_code void tcg_major_info_backup(void *backupAdr)
{
    TcgBkDevSlpDef *desAdr = (TcgBkDevSlpDef *)backupAdr;
    U32 i;

    memset(mRawKey_bak, 0, sizeof(mRawKey_bak));

    if(desAdr->SMBRDone != pG3->b.mLckMbrCtrl_Tbl.val[0].done)
        desAdr->SMBRDone = pG3->b.mLckMbrCtrl_Tbl.val[0].done;

    for(i=0; i <= LOCKING_RANGE_CNT; i++){
        if(desAdr->RdLocked[i] != pG3->b.mLckLocking_Tbl.val[i].readLocked)
            desAdr->RdLocked[i] = pG3->b.mLckLocking_Tbl.val[i].readLocked;

        if(desAdr->WrLocked[i] != pG3->b.mLckLocking_Tbl.val[i].writeLocked)
            desAdr->WrLocked[i] = pG3->b.mLckLocking_Tbl.val[i].writeLocked;
    }

    memcpy(mRawKey_bak,  mRawKey, sizeof(sRawKey));
    for(i = 0; i <= LOCKING_RANGE_CNT; i++){
        TCGPRN("range|%x Rawkey0|%x-Rawkey1|%x\n", i, mRawKey[i].dek.aesKey[0], mRawKey[i].dek.xtsKey[0]);
        DBG_P(0x4, 0x03, 0x7100EA, 4, i, 4, mRawKey[i].dek.aesKey[0], 4, mRawKey[i].dek.xtsKey[0]);  // range|%x Rawkey0|%x-Rawkey1|%x
        TCGPRN("range|%x Rawkey_bak0|%x-Rawkey_bak1|%x\n", i, mRawKey_bak[i].dek.aesKey[0], mRawKey_bak[i].dek.xtsKey[0]);
        DBG_P(0x4, 0x03, 0x7100EB, 4, i, 4, mRawKey_bak[i].dek.aesKey[0], 4, mRawKey_bak[i].dek.xtsKey[0]);  // range|%x Rawkey_bak0|%x-Rawkey_bak1|%x
    }

    #ifdef BCM_test
    //printk("backup!!!\n");
    DBG_P(0x01, 0x03, 0x7100EC );  // backup!!!
    DumpTcgKeyInfo();
    #endif
}

tcg_code void tcg_major_info_restore(void *restoreAdr)
{
    TcgBkDevSlpDef *srcAdr = (TcgBkDevSlpDef *)restoreAdr;
    TcgBkDevSlpDef *desAdr = &smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgBkDevSlpVar;
    memcpy(desAdr, srcAdr, sizeof(TcgBkDevSlpDef));
    warn_boot_restore_done = TRUE;  // cInitBootFwUpdated;     // warn boot occur
}


tcg_code void tcg_major_info_recover(void)
{
    U16 i;
    TcgBkDevSlpDef *srcAdr = &smSysInfo->d.TCGData.d.TCGUsed.ee2.TcgBkDevSlpVar;

    pG3->b.mLckMbrCtrl_Tbl.val[0].done = srcAdr->SMBRDone;
    if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE)&&(pG3->b.mLckMbrCtrl_Tbl.val[0].done == FALSE))
        mTcgStatus |= MBR_SHADOW_MODE;
    else
        mTcgStatus &= (~MBR_SHADOW_MODE);

    for(i = 0; i <= LOCKING_RANGE_CNT; i++){
        pG3->b.mLckLocking_Tbl.val[i].readLocked  = srcAdr->RdLocked[i];
        pG3->b.mLckLocking_Tbl.val[i].writeLocked = srcAdr->WrLocked[i];
    }

    memcpy(mRawKey,  mRawKey_bak, sizeof(mRawKey_bak));
    for(i = 0; i <= LOCKING_RANGE_CNT; i++){
        TCGPRN("range|%x Rawkey0|%x-Rawkey1|%x\n", i, mRawKey[i].dek.aesKey[0], mRawKey[i].dek.xtsKey[0]);
        DBG_P(0x4, 0x03, 0x7100ED, 4, i, 4, mRawKey[i].dek.aesKey[0], 4, mRawKey[i].dek.xtsKey[0]);  // range|%x Rawkey0|%x-Rawkey1|%x
        TCGPRN("range|%x Rawkey_bak0|%x-Rawkey_bak1|%x\n", i, mRawKey_bak[i].dek.aesKey[0], mRawKey_bak[i].dek.xtsKey[0]);
        DBG_P(0x4, 0x03, 0x7100EE, 4, i, 4, mRawKey_bak[i].dek.aesKey[0], 4, mRawKey_bak[i].dek.xtsKey[0]);  // range|%x Rawkey_bak0|%x-Rawkey_bak1|%x
    }
    #ifdef BCM_test
    //printk("restore\n");
    DBG_P(0x01, 0x03, 0x7100EF );  // restore
    DumpTcgKeyInfo();
    #endif
    bootMode = cInitBootCold;    // clear bootMode
}

#if 0
tcg_code void TcgBackupVar2EEP(void)
{
    U16  i;
    bool UpdateFlag = FALSE;

    if(smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.SMBRDone != pG3->b.mLckMbrCtrl_Tbl.val[0].done)
    {
        smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.SMBRDone = pG3->b.mLckMbrCtrl_Tbl.val[0].done;
        UpdateFlag = TRUE;
    }

    for(i=0; i<=LOCKING_RANGE_CNT; i++){
        if(smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.RdLocked[i] != pG3->b.mLckLocking_Tbl.val[i].readLocked)
        {
            smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.RdLocked[i] = pG3->b.mLckLocking_Tbl.val[i].readLocked;
            UpdateFlag = TRUE;
        }

        if(smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.WrLocked[i] != pG3->b.mLckLocking_Tbl.val[i].writeLocked)
        {
            smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.WrLocked[i] = pG3->b.mLckLocking_Tbl.val[i].writeLocked;
            UpdateFlag = TRUE;
        }
    }

    if(UpdateFlag)
        TcgFuncRequest1(MSG_TCG_NOREEP_WR);

    //DumpTcgKeyInfo();
    memcpy(mRawKey_bak,  mRawKey, sizeof(sRawKey));
    return;
}
#endif

#if 0
tcg_code void TcgRestoreVarFromEEP(void)
{
    U16 i;

    pG3->b.mLckMbrCtrl_Tbl.val[0].done = smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.SMBRDone;
    if((pG3->b.mLckMbrCtrl_Tbl.val[0].enable == TRUE)&&(pG3->b.mLckMbrCtrl_Tbl.val[0].done == FALSE))
        mTcgStatus |= MBR_SHADOW_MODE;
    else
        mTcgStatus &= (~MBR_SHADOW_MODE);

    for(i=0; i<=LOCKING_RANGE_CNT; i++){
        pG3->b.mLckLocking_Tbl.val[i].readLocked = smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.RdLocked[i];
        pG3->b.mLckLocking_Tbl.val[i].writeLocked = smSysInfo->d.MiscData.d.TCGUsed.ee2.TcgBkDevSlpVar.WrLocked[i];
    }
    //DumpTcgKeyInfo();
    memcpy(mRawKey,  mRawKey_bak, sizeof(sRawKey));
}
#endif

tcg_code int ChkDefaultTblPattern(void)
{
    if((pG1->b.mTcgTblInfo.ID != TCG_TBL_ID)
    || (pG1->b.mTcgTblInfo.ver != (TCG_G1_TAG + TCG_TBL_VER)))
    {
        TCG_ERR_PRN("Error!!, C tbl G1|%x %x FW G1|%x %x\n", pG1->b.mTcgTblInfo.ID, pG1->b.mTcgTblInfo.ver, TCG_TBL_ID, (TCG_G1_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0E, 4, pG1->b.mTcgTblInfo.ID, 4, pG1->b.mTcgTblInfo.ver, 4, TCG_TBL_ID, 4, (TCG_G1_TAG + TCG_TBL_VER));  // Error!!, C tbl G1|%x %x FW G1|%x %x
        //82 00 11, "!!!Error, G1 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820011, 4, TCG_TBL_ID, 4, pG1->b.mTcgTblInfo.ID, 4, (TCG_G1_TAG + TCG_TBL_VER), 4, pG1->b.mTcgTblInfo.ver);
        return zNG;
    }
    else if((pG2->b.mTcgTblInfo.ID != TCG_TBL_ID)
         || (pG2->b.mTcgTblInfo.ver != (TCG_G2_TAG + TCG_TBL_VER)))
    {
        TCG_ERR_PRN("Error!!, C tbl G2|%x %x FW G2|%x %x\n", pG2->b.mTcgTblInfo.ID, pG2->b.mTcgTblInfo.ver, TCG_TBL_ID, (TCG_G2_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F0F, 4, pG2->b.mTcgTblInfo.ID, 4, pG2->b.mTcgTblInfo.ver, 4, TCG_TBL_ID, 4, (TCG_G2_TAG + TCG_TBL_VER));  // Error!!, C tbl G2|%x %x FW G2|%x %x
        //82 00 12, "!!!Error, G2 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820012, 4, TCG_TBL_ID, 4, pG2->b.mTcgTblInfo.ID, 4, (TCG_G2_TAG + TCG_TBL_VER), 4, pG2->b.mTcgTblInfo.ver);
        return zNG;
    }
    else if((pG3->b.mTcgTblInfo.ID != TCG_TBL_ID)
         || (pG3->b.mTcgTblInfo.ver != (TCG_G3_TAG + TCG_TBL_VER)))
    {
        TCG_ERR_PRN("Error!!, C tbl G3|%x %x FW G3|%x %x\n", pG3->b.mTcgTblInfo.ID, pG3->b.mTcgTblInfo.ver, TCG_TBL_ID, (TCG_G3_TAG + TCG_TBL_VER));
        DBG_P(0x5, 0x03, 0x7F7F10, 4, pG3->b.mTcgTblInfo.ID, 4, pG3->b.mTcgTblInfo.ver, 4, TCG_TBL_ID, 4, (TCG_G3_TAG + TCG_TBL_VER));  // Error!!, C tbl G3|%x %x FW G3|%x %x
        //82 00 21, "!!!Error, G3 ID[%08X %08X] Ver[%08X %08X %08X]", 4 4 4 4 4
        // DBG_P(5, 3, 0x820021, 4, TCG_TBL_ID, 4, pG3->b.mTcgTblInfo.ID, 4, (TCG_G3_TAG + TCG_TBL_VER), 4, pG3->b.mTcgTblInfo.ver);
        return zNG;
    }
    else if ((pG1->b.mEndTag != TCG_END_TAG) || (pG2->b.mEndTag != TCG_END_TAG) || (pG3->b.mEndTag != TCG_END_TAG))
    {
        TCG_ERR_PRN("Error!!, C tbl End Tag|%x %x %x FW End Tag|%x\n", pG1->b.mEndTag, pG2->b.mEndTag, pG3->b.mEndTag, TCG_END_TAG);
        DBG_P(0x5, 0x03, 0x7F7F11, 4, pG1->b.mEndTag, 4, pG2->b.mEndTag, 4, pG3->b.mEndTag, 4, TCG_END_TAG);  // Error!!, C tbl End Tag|%x %x %x FW End Tag|%x
        // DBG_P(4, 3, 0x820029, 4, pG1->b.mEndTag, 4, pG2->b.mEndTag, 4, pG3->b.mEndTag);
        return zNG;
    }
    else
    {
        TCG_PRINTF("TcgTbl ver=%08x\n", TCG_TBL_VER);
        return zOK;
    }

    //DBG_P(1, 3, 0x820148);  //82 01 48, "Default Tbl OK"
    return zOK;
}

tcg_code void dump_G4_erased_count(void)
{
#ifndef alexcheck
#if 1
    U8   i,j;
    U8   cnt;
    U16  erase_cnt[15];

    cnt = 0;
    for(i = 0; i < TCG_MBR_CELLS / 15; i++)
    {
        for(j = 0; j < 15; j++)
        {
            if((tcgG4Dft[i * 15 + j] != 0) || ((i * 15 + j) >= TCG_MBR_CELLS))
            {      // is defect block ?
                erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
            }else
            {
                erase_cnt[j] = tcgG4EraCnt[i * 15 + j];  // show erased count
            }
            cnt++;
        }
        // DBG_P(17, 3, 0x820144, 1, i * 15, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
    }

    if (TCG_MBR_CELLS - cnt)
    {
        for(j = 0; j < 15; j++)
        {
            if(j < (TCG_MBR_CELLS - cnt)){
                if((tcgG4Dft[i * 15 + j] != 0) || ((i * 15 + j) >= TCG_MBR_CELLS))
                {      // is defect block ?
                    erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
                }else{
                    erase_cnt[j] = tcgG4EraCnt[i * 15 + j];  // show erased count
                }
            }else
            {
                erase_cnt[j] = 0;  // show 0x0000 if it is not exist.
            }
        }
        // DBG_P(17, 3, 0x820144, 1, i * 15, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
    }
    DumpG4DftAmount();
    // DBG_P(1, 3, 0x820000);  //82 00 00, "----------------------"
#else
    U8 i,j;

    for(i = 0; i < TCG_MBR_CELLS / 16; i++){
        TCG_PRINTF("G4 EraCnt=%2X :", i*16);
        for(j = 0; j < 16; j++){
            if(j == 8) TCG_PRINTF("- ");          //
            if((tcgG4Dft[i * 16 + j] != 0) || ((i * 16 + j) >= TCG_MBR_CELLS)){      // is defect block ?
                TCG_PRINTF("FFFF ");                          // show 0xFFFF if it is defect block
            }else{
                TCG_PRINTF("%4X ", tcgG4EraCnt[i * 16 + j]);   // show erased count
            }
        }
        TCG_PRINTF("\n");
    }
    DumpG4DftAmount();
    TCG_PRINTF("----------------------\n");  // CA A1 23,       "----------------------"
#endif
#endif
}

tcg_code void dump_G5_erased_count(void)
{
#ifndef alexcheck
#if 1

    U8   i,j;
    U8   cnt;
    U16  erase_cnt[15];

    cnt = 0;
    for(i = 0; i < TCG_MBR_CELLS / 15; i++)
    {
        for(j = 0; j < 15; j++)
        {
            if((tcgG5Dft[i * 15 + j] != 0) || ((i * 15 + j) >= TCG_MBR_CELLS))  // is defect block ?
            {
                erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
            }else
            {
                erase_cnt[j] = tcgG5EraCnt[i * 15 + j];  // show erased count
            }
            cnt++;
        }
        // DBG_P(17, 3, 0x820145, 1, i * 15, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
    }

    if (TCG_MBR_CELLS - cnt){
        for(j = 0; j < 15; j++)
        {
            if(j < (TCG_MBR_CELLS - cnt)){
                if((tcgG5Dft[i * 15 + j] != 0) || ((i * 15 + j) >= TCG_MBR_CELLS))  // is defect block ?
                {
                    erase_cnt[j] = 0xFFFF;  // show 0xFFFF if it is defect block
                }else
                {
                    erase_cnt[j] = tcgG5EraCnt[i * 15 + j];  // show erased count
                }
            }else
            {
                erase_cnt[j] = 0;  // show 0x0000 if it is not exist.
            }
        }
        // DBG_P(17, 3, 0x820145, 1, i * 15, 2, erase_cnt[0], 2, erase_cnt[1], 2, erase_cnt[2], 2, erase_cnt[3]
                                        // , 2, erase_cnt[4], 2, erase_cnt[5], 2, erase_cnt[6], 2, erase_cnt[7]
                                        // , 2, erase_cnt[8], 2, erase_cnt[9], 2, erase_cnt[10], 2, erase_cnt[11]
                                        // , 2, erase_cnt[12], 2, erase_cnt[13], 2, erase_cnt[14]);
    }
    DumpG5DftAmount();
    // DBG_P(1, 3, 0x820000);  //82 00 00, "----------------------"
#else
    U8 i,j;

    for(i = 0; i < TCG_MBR_CELLS / 16; i++){
        TCG_PRINTF("G5 EraCnt=%2X :", i*16);
        for(j = 0; j < 16; j++){
            if(j == 8) TCG_PRINTF("- ");          //
            if((tcgG5Dft[i * 16 + j] != 0) || ((i * 16 + j) >= TCG_MBR_CELLS)){      // is defect block ?
                TCG_PRINTF("FFFF ");                          // show 0xFFFF if it is defect block
            }else{
                TCG_PRINTF("%4X ", tcgG5EraCnt[i * 16 + j]);   // show erased count
            }
        }
        TCG_PRINTF("\n");
    }
    DumpG5DftAmount();
    TCG_PRINTF("----------------------\n");  // CA A1 23,       "----------------------"
#endif
#endif
}

tcg_code void DumpG4DftAmount(void)
{
    U8 i;
    U8 amount = 0;

    for(i = 0; i < TCG_MBR_CELLS; i++){
        if(tcgG4Dft[i] != 0) amount++;
    }
    gTcgG4Defects = amount;
    // DBG_P(2, 3, 0x820146, 1, amount);  //82 01 46, "==> G4 defect amount = %02X" 1
}

tcg_code void DumpG5DftAmount(void)
{
    U8 i;
    U8 amount = 0;

    for(i = 0; i < TCG_MBR_CELLS; i++){
        if(tcgG5Dft[i] != 0) amount++;
    }
    gTcgG5Defects = amount;
    // DBG_P(2, 3, 0x820147, 1, amount);  //82 01 47, "==> G5 defect amount = %02X" 1
}

tcg_code void DumpTcgTblInfo(void)
{
#if 0
    D_PRINTF("G1: %08x %08x\n", pG1->b.mTcgTblInfo.ID, pG1->b.mTcgTblInfo.ver);
    D_PRINTF("G2: %08x %08x\n", pG2->b.mTcgTblInfo.ID, pG2->b.mTcgTblInfo.ver);
    D_PRINTF("G3: %08x %08x\n", pG3->b.mTcgTblInfo.ID, pG3->b.mTcgTblInfo.ver);
#else
    DBG_P(3, 3, 0x71010A, 4, TCG_TBL_ID, 4, (TCG_G1_TAG + TCG_TBL_VER));
    DBG_P(3, 3, 0x71010B, 4, pG1->b.mTcgTblInfo.ID, 4, pG1->b.mTcgTblInfo.ver);
    DBG_P(3, 3, 0x71010B, 4, pG2->b.mTcgTblInfo.ID, 4, pG2->b.mTcgTblInfo.ver);
    DBG_P(3, 3, 0x71010B, 4, pG3->b.mTcgTblInfo.ID, 4, pG3->b.mTcgTblInfo.ver);
    //DBG_P(4, 3, 0x71010C, 4, pG3->b.mKeyWrapTag,    4, pG3->b.mKeyWrapStatus,
    //      1, smSysInfo->d.NVMeData.d.SCU_CMD.d.KWState);
    DBG_P(2, 3, 0x71010C, 1, smSysInfo->d.NVMeData.d.SCU_CMD.d.KWState);
    printk("mTcgSts:%08x\n", mTcgStatus);
    DBG_P(0x2, 0x03, 0x710132, 4, mTcgStatus);  // mTcgSts:%08x
#endif
}

tcg_code void DumpTcgKeyInfo(void)
{
#if CO_SUPPORT_AES
    U32 i = 0;

    printk("\n\nmWKey:\n");
    DBG_P(0x01, 0x03, 0x7100F4 );  // mWKey:
    for(i=0; i<=LOCKING_RANGE_CNT; i++)
    {
        if (pG3->b.mWKey[i].state != TCG_KEY_NULL)
        {
            printk(" [%x] N|%x R|%x s|%x k1|%x icv1|%x k2|%x icv2|%x\n", i,
                                                pG3->b.mWKey[i].nsid,
                                                pG3->b.mWKey[i].range,
                                                pG3->b.mWKey[i].state,
                                                pG3->b.mWKey[i].dek.aesKey[0],
                                                pG3->b.mWKey[i].dek.icv1[0],
                                                pG3->b.mWKey[i].dek.xtsKey[0],
                                                pG3->b.mWKey[i].dek.icv2[0]);
            DBG_P(0x9, 0x03, 0x7100F5, 4, i, 4,pG3->b.mWKey[i].nsid, 4,pG3->b.mWKey[i].range, 4,pG3->b.mWKey[i].state, 4,pG3->b.mWKey[i].dek.aesKey[0], 4,pG3->b.mWKey[i].dek.icv1[0], 4,pG3->b.mWKey[i].dek.xtsKey[0], 4,pG3->b.mWKey[i].dek.icv2[0]);  //  [%x] N|%x R|%x s|%x k1|%x icv1|%x k2|%x icv2|%x
        }
    }
    printk("\n\nmRKey:\n");
    DBG_P(0x01, 0x03, 0x7100F6 );  // mRKey:
    for(i=0; i<=LOCKING_RANGE_CNT; i++)
    {
        if (mRawKey[i].state != TCG_KEY_NULL)
        {
            printk(" [%x] s|%x k1|%x k2|%x\n", i, mRawKey[i].state,
                                            mRawKey[i].dek.aesKey[0],
                                            mRawKey[i].dek.xtsKey[0]);
            DBG_P(0x5, 0x03, 0x7100F7, 4, i, 4, mRawKey[i].state, 4,mRawKey[i].dek.aesKey[0], 4,mRawKey[i].dek.xtsKey[0]);  //  [%x] s|%x k1|%x k2|%x
        }
    }

    printk("\n\nmOpalKEK\n");
    DBG_P(0x01, 0x03, 0x7100F8 );  // mOpalKEK
    DBG_P(0x01, 0x03, 0x7100E4 );  // mOpalWrapKEK
    for (i = 0; i <sizeof(pG3->b.mOpalWrapKEK) / sizeof(sWrappedOpalKey); i++)
    {
        if (pG3->b.mOpalWrapKEK[i].state != TCG_KEY_NULL)
        {
            printk(" [%x] s|%x icv|%x kek|%x slt|%x\n",
                pG3->b.mOpalWrapKEK[i].idx,
                pG3->b.mOpalWrapKEK[i].state,
                pG3->b.mOpalWrapKEK[i].icv[0],
                pG3->b.mOpalWrapKEK[i].opalKEK[0],
                pG3->b.mOpalWrapKEK[i].salt[0]);
            DBG_P(0x6, 0x03, 0x7100F9, 4,pG3->b.mOpalWrapKEK[i].idx, 4,pG3->b.mOpalWrapKEK[i].state, 4,pG3->b.mOpalWrapKEK[i].icv[0], 4,pG3->b.mOpalWrapKEK[i].opalKEK[0], 4,pG3->b.mOpalWrapKEK[i].salt[0]);  //  [%x] s|%x icv|%x kek|%x slt|%x
        }
    }

    printk("WrapKEK: %x %x\n", WrapKEK[0], WrapKEK[1]); // "OpalKEK"
    DBG_P(0x3, 0x03, 0x7100FA, 4, WrapKEK[0], 4, WrapKEK[1]);  // WrapKEK: %x %x
#endif  // CO_SUPPORT_AES
}

tcg_code void DumpRangeInfo(void)
{
    U16 i;
    // DBG_P(6, 3, 0x8201D0, 2,  mReadLockedStatus, 2, mWriteLockedStatus, 1, (U8)mTcgStatus, 2, mSgUser.range, 1, mSgUser.policy);  //82 01 D0, "RdLck: %04X, WrLck: %04X, tcgStatus: %02X", 2 2 1
    for(i=0; i<LOCKING_RANGE_CNT+1; i++)
    {
        printk("%x R:%x W:%x *%x ~ &%x %x\n",
                pLockingRangeTable[i].rangeNo,
                pLockingRangeTable[i].readLocked,
                pLockingRangeTable[i].writeLocked,
                (U32)pLockingRangeTable[i].rangeStart, (U32)pLockingRangeTable[i].rangeEnd,
                pLockingRangeTable[i].blkcnt);
        DBG_P(0x7, 0x03, 0x7100FB, 4,pLockingRangeTable[i].rangeNo, 4,pLockingRangeTable[i].readLocked, 4,pLockingRangeTable[i].writeLocked, 4,(U32)pLockingRangeTable[i].rangeStart, 4, (U32)pLockingRangeTable[i].rangeEnd, 4,pLockingRangeTable[i].blkcnt);  // %x R:%x W:%x *%x ~ &%x %x

        if(pLockingRangeTable[i].rangeNo==0x00)
            break;
    }

    crypto_dump_range();
}

tcg_code void DumpTcgSysInfo(U32 offset, U8 bcnt)
{
#ifndef alexcheck
    U32 *ptr = (U32 *)&(smSysInfo->d.MiscData.d.TCGUsed) + offset/4;
    U8 i, dwcnt = bcnt/4;

    Cdbg1S("SysInf->TCGUsed:");
    for(i=0; i<dwcnt; i+=4) {
        // DBG_P(5,3,0xAC0A04, 4, ptr[i], 4,ptr[i+1], 4,ptr[i+2], 4,ptr[i+3]);
    }
#endif
}

tcg_code void DumpTcgEepInfo(void)
{
#ifndef alexcheck
    //TCG_PRINTF("M table IdText[%s] Id[%X], VerText[%s] Ver[%X]\n", tcg_mTbl_idStr, tcg_mTbl_id, tcg_mTbl_verStr, tcg_mTbl_ver);
    // DBG_P(5, 3, 0x8201D2,    4, tcg_mTbl_id,   //82 01 D2, "M table Id[%X] Ver[%X] IdText[%s], VerText[%s]", 4 4
                             // 4, tcg_mTbl_ver,
                          // 0xFF, tcg_mTbl_idStr,
                          // 0xFF, tcg_mTbl_verStr);
    //TCG_PRINTF("C table IdText[%s] Id[%X], VerText[%s] Ver[%X]\n", tcg_cTbl_idStr, tcg_cTbl_id, tcg_cTbl_verStr, tcg_cTbl_ver);
    // DBG_P(5, 3, 0x8201D3,    4, tcg_cTbl_id,   //82 01 D3, "C table Id[%X] Ver[%X] IdText[%s], VerText[%s]", 4 4
                             // 4, tcg_cTbl_ver,
                          // 0xFF, tcg_cTbl_idStr,
                          // 0xFF, tcg_cTbl_verStr);

    //TCG_PRINTF("FW Dft Tag[%s], EEP Def Tag[%s]\n", DEFECT_STRING, (char *)tcgDefectID);
    // DBG_P(5, 3, 0x8201D4, 0xFF, DEFECT_STRING,   //82 01 D4, "FW Dft Tag[%s], EEP Def Tag[%s]"
                          // 0xFF, (char *)tcgDefectID);
    //TCG_PRINTF("FW EraCnt Tag[%s], EEP EraCnt Tag[%s]\n", ERASED_CNT_STRING, (char *)tcgErasedCntID);
    // DBG_P(5, 3, 0x8201D5, 0xFF, ERASED_CNT_STRING,   //82 01 D5, "FW EraCnt Tag[%s], EEP EraCnt Tag[%s]"
                          // 0xFF, (char *)tcgErasedCntID);
    //TCG_PRINTF("EEP Prefmt Tag[%s]\n", tcg_prefmt_tag);
    // DBG_P(5, 3, 0x8201D6, 0xFF, tcg_prefmt_tag);   //82 01 D6, "EEP Prefmt Tag[%s]"
#endif
}

//-----------------------------------------------------------------------------
/**
    Test Monitor command handler - Operate TCG Defect/Erase count table

    @param[in]  pCmdStr     command line string
    @param[in]  argc        argument count if auto-decode is enabled
    @param[in]  argv        argument values if auto-decode is enabled
                            argv[0] : function(0=shor, 1=clear)
                            argv[1] : none
                            argv[2] : none
                            argv[3] : none

    @return   error code
**/
//-----------------------------------------------------------------------------
tcg_code Error_t CmdTcg_OpEraDefTbl(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
#ifndef alexcheck
#if 0
    tMSG_PHY_RWE msg;
    tPAA paa;

    memset(&msg, 0, sizeof(msg));
    memset(&paa, 0, sizeof(paa));

    MSG_SET_SERVICE_HEADER(msg, SEQCMD_TO_SVC_FN(argv[0]));

    paa.b.block = argc>1 ? argv[1] : 0;
    paa.b.ce    = argc>2 ? argv[2] : 0;
    paa.b.ch    = argc>3 ? argv[3] : 0;
    paa.b.page  = argc>4 ? argv[4] : 0;
    paa.b.lun   = argc>5 ? argv[5] : 0;
    msg.pBuffer  = gpTestBuffer;

    msg.param1  = paa.all32;

    IPC_SendWaitMsg(H2M_REQ_QUEUE, (tMSG *)&msg, (tMSG *)&msg, IPC_NO_TIMEOUT);
#endif

    switch(argv[0]){
        case 0 :
            // DBG_P(2, 3, 0x8201D7, 4, tcgEepProgIdx);  //82 01 D7, "tcgEepProgIdx[%X]", 4
            dump_G4_erased_count();
            dump_G5_erased_count();
            DumpTcgEepInfo();
            break;
        case 1 :
            memset((void *)tcgG4EraCnt, 0, sizeof(tcgG4EraCnt));   // force clear G4 erased count
            memset((void *)tcgG5EraCnt, 0, sizeof(tcgG5EraCnt));   // force clear G5 erased count

            memset((void *)tcgG4Dft, 0, sizeof(tcgG4Dft));   // force clear G4 defect table
            memset((void *)tcgG5Dft, 0, sizeof(tcgG5Dft));   // force clear G5 defect table
        #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
        #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
        #endif
            break;
        default:
            break;
    }
#endif
    // return cEcNoError;
    return EC_NO_ERROR;
}


tcg_code Error_t CmdTcg_OpTcgInfo(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    // int i;
    // U32 rdata;
    //U32 buf[8];
    sCPin *p = (sCPin*)(tcg_ee_Psid);

    switch(argv[0]){
        case 0:
            DumpTcgTblInfo();
            DumpRangeInfo();
            //  DBG_P(3, 3,0xAC0A1A, 4,bcmClientVars.otpDeploy, 4,bcmClientVars.otpLifeCycle);
            break;
#if 0 //_TCG_DEBUG
        case 1:
            if(argc!=3)
                return EC_NO_ERROR;

            if(argv[1]>2)
                return EC_NO_ERROR;

            //cjdbg, BcmRd(argv[1], argv[2], &rdata);

            //TCG_PRINTF("rd: %x\n", rdata);
            // DBG_P(2, 3, 0x8201D8, 4, rdata);  //82 01 D8, "rd:[%X]", 4
            break;

        case 2:
            if(argc!=4)
                return EC_NO_ERROR;

            if(argv[1]>2)
                return EC_NO_ERROR;

             //cjdbg, BcmWt(argv[1], argv[2], argv[3]);

             //TCG_PRINTF("wt: %x\n", argv[2]);
             // DBG_P(2, 3, 0x8201D9, 4, argv[3]);  //82 01 D9, "wt:[%X]", 4
             break;
#endif
        case 3:
            if(argc!=3)
                // return cEcNoError;
                return EC_NO_ERROR;
            DumpTcgSysInfo(argv[1], argv[2]); //offset, DwCnt
            break;
        case 4:  // clear tag
            if(argc != 2){
                printk("Invalid parameters, >>> ex. tcgif 4 0/1 [0:dump, 1:clear tag]");
                DBG_P(0x01, 0x03, 0x710133 );  // Invalid parameters, >>> ex. tcgif 4 0/1 [0:dump, 1:clear tag]
                return EC_NO_ERROR;
            }
            switch(argv[1]){
                case 0:
                    printk("tcg_ee_Psid tag [%08x] [%08x]", p->cPin_Tag, *((U32*)p->cPin_val));
                    DBG_P(0x3, 0x03, 0x710134, 4, p->cPin_Tag, 4, *((U32*)p->cPin_val));  // tcg_ee_Psid tag [%08x] [%08x]
                break;
                case 1:
                    TcgForcePSIDSetToDefault();
                break;
                default:
                break;
            }

            break;

        case 5:
            DumpTcgKeyInfo();
            break;

        default:
            break;
    }

    // return cEcNoError;
    return EC_NO_ERROR;
}


tcg_code void DumpFTLProvidedDftMapBlk(U16 *ptr, U32 size)
{
    U16 i,j;

    for(i = 0; i < size/16; i++){
        // DBG_P(2, 3, 0x8201EA, 1, i * 16);  //82 01 EA, "%02X: %04X %04X %04X %04X %04X %04X %04X %04X-%04X %04X %04X %04X %04X %04X %04X %04X", 1 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2
        for(j = 0; j < 16; j++){
            // DBG_P(1, 2, ptr[i * 16 + j]);   // show valid blk table
        }
    }
}

tcg_code void DumpValidBlk(U32 *ptr, U32 size)
{
    U8 i,j;

    // DBG_P(1, 3, 0x8201E6);  //"CH[b0~2] FRAG[b3~4] PLANE[b5] WLPAGE[b6~13] BLOCK[b14~25] subpage[b26~27] CE[b28~30] LUN[b31]"
    for(i = 0; i < size/8; i++){
        // DBG_P(2, 3, 0x8201E5, 1, i * 8);  //82 01 E5, "%02X: %08X %08X %08X %08X %08X %08X %08X %08X", 1 4 4 4 4 4 4 4 4
        for(j = 0; j < 8; j++){
            // DBG_P(1, 4, ptr[i * 8 + j]);   // show valid blk table
        }
    }
    // DBG_P(1, 3, 0x820000);  //82 00 00, "----------------------"
    // DBG_P(1, 3, 0x8201E7);  //82 01 E7, "| LUN CE BLOCK PLANE CH |"
    for(i = 0; i < size/4; i++){
        // DBG_P(2, 3, 0x8201E8, 1, i * 4);
        for(j = 0; j < 4; j++){
            // DBG_P(5, 1, ((tPAA *)(ptr + i * 4 + j))->b.lun, 1, ((tPAA *)(ptr + i * 4 + j))->b.ce, 2, ((tPAA *)(ptr + i * 4 + j))->b.block, 1, ((tPAA *)(ptr + i * 4 + j))->b.plane,  1, ((tPAA *)(ptr + i * 4 + j))->b.ch );   // show valid blk table
        }
    }
    // DBG_P(1, 3, 0x820000);  //82 00 00, "----------------------"

}

tcg_code Error_t CmdTcg_OpTcgDmpVidBlk(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
#ifndef alexcheck
    switch(argv[0]){
        case 0:
            // DBG_P(1, 3, 0x8201EC);  //82 01 EC, "tcgdv help : 0. DmpEEVidBlk 1. EraEEVidBlk 2. DmpBufVidBlk 3. Dmp TCGBlockNo[]"

            // DBG_P(1, 3, 0x8201E0);  //82 01 E0, "Dump EEPROM G4 Valid Blk Table"
            DumpValidBlk(smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL, TCG_MBR_CELLS); //DUMP EE G4
            // DBG_P(1, 3, 0x8201E1);  //82 01 E1, "Dump EEPROM G5 Valid Blk Table"
            DumpValidBlk(&smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL[TCG_MBR_CELLS], TCG_MBR_CELLS); //DUMP EE G4
            break;

        case 1:
            // DBG_P(1, 3, 0x8201E2);  //82 01 E2, "Erase EEPROM G4 & G5 Valid Blk Table"
            memset(smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL, 0xFF, sizeof(smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL));
            memset(smSysInfo->d.MiscData.d.TCGUsed.TCGBlockNo_EE, 0xFF, sizeof(smSysInfo->d.MiscData.d.TCGUsed.TCGBlockNo_EE));
            smSysInfo->d.MiscData.d.TCGUsed.TCG_PHY_VALID_BLK_TBL_TAG = 0xFFFFFFFF;
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif
            break;

        case 2:
            // DBG_P(1, 3, 0x8201E3);  //82 01 E3, "Dump Buf G4 Valid Blk Table"
            DumpValidBlk((U32 *)DR_G4PaaBuf, TCG_MBR_CELLS); //DUMP buf G4
            // DBG_P(1, 3, 0x8201E4);  //82 01 E4, "Dump Buf G5 Valid Blk Table"
            DumpValidBlk((U32 *)DR_G5PaaBuf, TCG_MBR_CELLS); //DUMP buf G4
            break;

        case 3:
            // DBG_P(1, 3, 0x8201EB);  //82 01 EB, "Dump EEPROM FTL provided TCGBlockNo_EE[0] ~ TCGBlockNo_EE[0x7F]"
            DumpFTLProvidedDftMapBlk(&smSysInfo->d.MiscData.d.TCGUsed.TCGBlockNo_EE[0], sizeof(smSysInfo->d.MiscData.d.TCGUsed.TCGBlockNo_EE)/sizeof(U16));
            // DBG_P(1, 3, 0x8201E9);  //82 01 E9, "Dump FTL provided TCGBlockNo[0] ~ TCGBlockNo[0xFF]"
            DumpFTLProvidedDftMapBlk(&smSysInfo->d.MiscData.d.TCGBlockNo[0], SI_MAX_TCGINFO_CNT);
            break;

       default:
            break;

    }
#endif
    // return cEcNoError;
    return EC_NO_ERROR;
}


tcg_code Error_t CmdTcg_SwitcherSetting(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
#ifndef alexcheck
    switch(argv[0]){
        //================= following is TCG/NonTCG operation ================
        case 0:   //show tcg_nontcg_switcher current setting.
            // DBG_P(3, 3, 0x820349, 4, tcg_nontcg_switcher, 0xFF, (tcg_nontcg_switcher == TCG_TAG) ? "TCG" : ((tcg_nontcg_switcher == NONTCG_TAG) ? "NONTCG" : "UNSETTING"));  //82 03 49, "tcg_nontcg_switcher = %08X [%s], 4
            break;

        case 1:
            if(mTcgStatus & TCG_ACTIVATED){
                // DBG_P(1, 3, 0x820024);  //82 00 24, "!!!Error, Activated, TCG disabled is inhibited"
            }else{
                // DBG_P(1, 3, 0x8201EF);  //82 01 EF, "*** TCG is disabled ***"
                tcg_nontcg_switcher = NONTCG_TAG;
            }
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif
            break;

        case 2:
            // DBG_P(1, 3, 0x8201EE);  //82 01 EE, "*** TCG is enabled ***"
            tcg_nontcg_switcher = TCG_TAG;
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif
            break;
        //================= following is PSID tag operation ================
        case 3:   //show tcg_ee_PsidTag current setting.
            // DBG_P(3, 3, 0x8201F0, 4, *(U32*)tcg_ee_Psid, 0xFF, (*(U32*)tcg_ee_Psid == CPIN_IN_PBKDF) ? "PSID" : "UNKNOW");  //82 01 F0, "PSID tag = %08X [%s]", 4
            break;

        case 4:  // clear tag
            // DBG_P(1, 3, 0x8201F1);  //82 01 F1, "*** PSID clear tag ***"
            *(U32*)tcg_ee_Psid = CPIN_NULL;   //clear tag
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif
            break;

        case 5:  // set tag
            // DBG_P(1, 3, 0x8201F2);  //82 01 F2, "*** PSID set tag ***"
            *(U32*)tcg_ee_Psid = CPIN_IN_PBKDF;   //set tag
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif
            break;

       default:
            break;

    }
#endif
    // return cEcNoError;
    return EC_NO_ERROR;
}

tcg_code int  TcgResetCacheAgency(MSG_TCG_SUBOP_t TCG_Cache_Rst_Type)
{
    // DBG_P(1, 3, 0x8201DE);  //82 01 DE, "[F]TcgResetCacheAgency"
    TcgFuncRequest1(TCG_Cache_Rst_Type);

    return zOK;
}

tcg_code int  TcgTblHistoryDestoryAgency(void)
{
#if TCG_TBL_HISTORY_DESTORY
    // DBG_P(1, 3, 0x8201F5);  //82 01 F5, "[F]TcgTblHistoryDestoryAgency"
    return TcgFuncRequest1(MSG_TCG_TBL_HIST_DEST);
#else
    return zOK;
#endif
}

tcg_code int  TcgEEPROMAgency(MSG_TCG_SUBOP_t op)
{
    switch(op){
        case MSG_TCG_NOREEP_INIT:
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_INIT);
          #endif
            break;

        case MSG_TCG_NOREEP_RD:
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_RD);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_READ);
          #endif
            break;

        case MSG_TCG_NOREEP_WR:
          #ifdef TCG_EEP_NOR
            TcgFuncRequest1(MSG_TCG_NOREEP_WR);
          #else
            SYSINFO_Synchronize(SYSINFO_MISC_AREA, SYSINFO_WRITE);
          #endif
            break;
         default :
            // DBG_P(1, 3, 0x820025);  //82 00 25, "!!!Error, Wrong function Parameter"
            break;
    }
    return zOK;
}

// Fetch the target AccessCtrlTbl to tcgBuf @DTCM
tcg_code int FetchAxsCtrlTbl(U64 spid, U16 *pByteCnt, U16 *pRowCnt)
{
    U32 invID_H32 = (U32)(invokingUID.all >> 32);

    //memset(&invTblHdr, 0, sizeof(invTblHdr));
    pAxsCtrlTbl = NULL;
    pInvColPty = NULL;

    if (spid == UID_SP_Admin)
    {
        switch (invID_H32) //High DW
        {
        case 0: //UID_ThisSP >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            //pAxsCtrlTbl = offsetof(tG1, b.mAdmAxsCtrl_Tbl.thisSP);
            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.thisSP;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.thisSP);
            break;
        case UID_Table >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmTbl_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmTbl_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.table;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.table);
            break;
        case UID_SPInfo >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmSPInfo_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSPInfo_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.spInfo;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.spInfo);
            break;
        case UID_SPTemplate >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmSPTemplates_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSPTemplates_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.spTemplate;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.spTemplate);
            break;
        case UID_MethodID >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmMethod_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmMethod_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.method;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.method);
            break;
        case UID_ACE >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmACE_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmACE_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.ace;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.ace);
            break;
        case UID_Authority >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmAuthority_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmAuthority_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.authority;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.authority);
            break;
        case UID_CPIN >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmCPin_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmCPin_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.cpin;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.cpin);
            break;
        case UID_TPerInfo >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmTPerInfo_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmTPerInfo_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.tperInfo;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.tperInfo);
            break;
        case UID_Template >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmTemplate_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmTemplate_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.templateTbl;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.templateTbl);
            break;
        case UID_SP >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmSP_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSP_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.sp;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.sp);
            break;
#if (_TCG_==TCG_PYRITE)
        case UID_RemovalMechanism >> 32:
            pInvokingTbl = (U8*)&pG1->b.mAdmRemovalMsm_Tbl;
            //invokingTblSize = sizeof(pG1->b.mAdmSP_Tbl);

            pAxsCtrlTbl = pG1->b.mAdmAxsCtrl_Tbl.removalMsm;
            *pByteCnt = sizeof(pG1->b.mAdmAxsCtrl_Tbl.removalMsm);
            break;
#endif
        default:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            TCG_PRINTF("AxsCtrl: NO InvID!!\n");
            // DBG_P(1, 3, 0x820222); // 82 02 22, "AxsCtrl: NO InvID!!"
            return zNG;
        }

        //FetchTcgTbl((U8*)pAxsCtrlTbl, *pByteCnt);        //Fetch the part of AxsCtrl table
        *pRowCnt = *pByteCnt / sizeof(sAxsCtrl_TblObj);
    }
    else // if (spid == UID_SP_Locking)
    {
        switch (invID_H32) //High DW
        {
        case 0:  //UID_ThisSP >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.thisSP;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.thisSP);
            break;
        case UID_Table >> 32:
            pInvokingTbl = (U8*)&pG2->b.mLckTbl_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckTbl_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.table;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.table);
            break;
        case UID_SPInfo >> 32:
            pInvokingTbl = (U8*)&pG2->b.mLckSPInfo_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckSPInfo_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.spInfo;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.spInfo);
            break;
        case UID_SPTemplate >> 32:
            pInvokingTbl = (U8*)&pG2->b.mLckSPTemplates_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckSPTemplates_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.spTemplate;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.spTemplate);
            break;
        case UID_MethodID >> 32:
            pInvokingTbl = (U8*)&pG2->b.mLckMethod_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckMethod_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.method;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.method);
            break;
        case UID_ACE >> 32:
            pInvokingTbl = (U8*)&pG3->b.mLckACE_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckACE_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.ace;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.ace);
            break;
        case UID_Authority >> 32:
            pInvokingTbl = (U8*)&pG3->b.mLckAuthority_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckAuthority_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.authority;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.authority);
            break;
        case UID_CPIN >> 32:
            pInvokingTbl = (U8*)&pG3->b.mLckCPin_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckCPin_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.cpin;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.cpin);
            break;
#if _TCG_ != TCG_PYRITE
        case UID_SecretProtect >> 32:
            pInvokingTbl = (U8*)&pG2->b.mLckSecretProtect_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckSecretProtect_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.secretPrtct;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.secretPrtct);
            break;
#endif
        case UID_LockingInfo >> 32:
            pInvokingTbl = (U8*)&pG2->b.mLckLockingInfo_Tbl;
            //invokingTblSize = sizeof(pG2->b.mLckLockingInfo_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.lckingInfo;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.lckingInfo);
            break;
        case UID_Locking >> 32:
            pInvokingTbl = (U8*)&pG3->b.mLckLocking_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckLocking_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.lcking;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.lcking);
            break;
        case UID_MBRControl >> 32:
            pInvokingTbl = (U8*)&pG3->b.mLckMbrCtrl_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckMbrCtrl_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.mbrCtrl;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.mbrCtrl);
            break;
        case UID_MBR >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.mbr;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.mbr);
            break;
#if _TCG_ != TCG_PYRITE
        case UID_K_AES_256_GRange_Key >> 32:
            pInvokingTbl = (U8*)&pG3->b.mLckKAES_256_Tbl;
            //invokingTblSize = sizeof(pG3->b.mLckKAES_256_Tbl);

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.kaes;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.kaes);
            break;
#endif
        case UID_DataStore >> 32:
        case UID_DataStore2 >> 32:
        case UID_DataStore3 >> 32:
        case UID_DataStore4 >> 32:
        case UID_DataStore5 >> 32:
        case UID_DataStore6 >> 32:
        case UID_DataStore7 >> 32:
        case UID_DataStore8 >> 32:
        case UID_DataStore9 >> 32:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            pAxsCtrlTbl = pG2->b.mLckAxsCtrl_Tbl.datastore;
            *pByteCnt = sizeof(pG2->b.mLckAxsCtrl_Tbl.datastore);
            break;
        default:
            pInvokingTbl = NULL;
            //invokingTblSize = 0;

            TCG_PRINTF("AxsCtrl: NO InvID!!\n");
            // DBG_P(1, 3, 0x820223); // 82 02 23, "AxsCtrl: NO InvID1 !!"
            return zNG;
        }

        //FetchTcgTbl((U8*)pAxsCtrlTbl, *pByteCnt);        //Fetch the part of AxsCtrl table
        *pRowCnt = *pByteCnt / sizeof(sAxsCtrl_TblObj);
    }

    if (pInvokingTbl)
    {
        //memcpy(&invTblHdr, pInvokingTbl, sizeof(invTblHdr));
        pInvColPty = (sColPrty*)(pInvokingTbl + sizeof(sTcgTblHdr));
    }

    return zOK;
}

#ifdef BCM_test // uart_cmd for bcm test
U32 Bcm_Test(U32 argc, U32* argv)
{
    //printk("\nbcm test\n");
    DBG_P(0x01, 0x03, 0x7100FC );  // bcm test

    switch(argv[0]){
        case 0:
            DumpTcgKeyInfo();
            break;
        case 1:
            DumpTcgTblInfo();
            DumpRangeInfo();
            break;
        case 2:
            crypto_init(0);
            break;
        case 3:
            LockingRangeTable_Update();
            break;
        default:
            //printk("Dump KeyInfo      : 0\n");
            DBG_P(0x01, 0x03, 0x7100FD );  // Dump KeyInfo      : 0
            //printk("Dump KeyRangeInfo : 1\n");
            DBG_P(0x01, 0x03, 0x7100FE );  // Dump KeyRangeInfo : 1
            break;
    }

    return 0;
}
#endif // uart_cmd for dpe test
// ]

/**************
 * memory dump
 **************/
tcm_code tERROR mem_dump(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 i ,addr, len;
    //printk("\nmem_dump : addr = %x ,len = %x\n", argv[0], argv[1]);
    DBG_P(0x3, 0x03, 0x7100FF, 4, argv[0], 4, argv[1]);  // mem_dump : addr = %x ,len = %x

    addr = argv[0];
    len  = argv[1];
    for(i = 0; i < len; i += 4){
        if((i % 16) == 0) //printk("\n%x - ", addr + i);
        DBG_P(0x2, 0x03, 0x710100, 4, addr + i);  // %x -
        //printk("%x ", swap_u32(*((U32*)(addr + i))));
        DBG_P(0x2, 0x03, 0x710101, 4, swap_u32(*((U32*)(addr + i))));  // %x
        // DBG_P(3, 3, 0x820109, 4, addr, 4, swap_u32(*((U32*)(addr + i))));  // temporary // alexcheck
    }
    //printk("\n");
    DBG_P(0x01, 0x03, 0x710102 );  //
    return 0;
}
/***************
 * otp operator
 ***************/
#define COLUMN_CNT      8
#define DOC_REG_GAP     0x100
tcg_code tERROR otp_dump(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 i, j, offset;
    U32 *p;

    for(i = 0; i < (512/sizeof(U32)/COLUMN_CNT); i++){
        for(j = 0; j < COLUMN_CNT; j++){
            offset = (i * COLUMN_CNT + j ) * sizeof(U32) + DOC_REG_GAP;
            *((U32 *)(tcgTmpBuf + (i * COLUMN_CNT + j ) * sizeof(U32))) = read_otp_data(offset);
        }
        p = (U32 *)(tcgTmpBuf + (i * COLUMN_CNT) * sizeof(U32));
        //printk("addr %04x :%08x %08x %08x %08x -  %08x %08x %08x %08x\n", (i * COLUMN_CNT) * sizeof(U32), *(p+0), *(p+1), *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
        DBG_P(0xa, 0x03, 0x710103, 2, (i * COLUMN_CNT) * sizeof(U32), 4, *(p+0), 4, *(p+1), 4, *(p+2), 4, *(p+3), 4, *(p+4), 4, *(p+5), 4, *(p+6), 4, *(p+7));  // addr %04x :%08x %08x %08x %08x -  %08x %08x %08x %08x
    }
    return 0;
}

tcg_code tERROR otp_write(Cstr_t pCmdStr, U32 argc, U32 argv[])
{
    U32 i, offset, data;

    if((argc < 2) || (argc > 9)){
        TCG_ERR_PRN("Wrong arguments amount!!\n");
        DBG_P(0x01, 0x03, 0x7F7F12);  // Wrong arguments amount!!
        return 0;
    }
    offset = argv[0];
    data   = argv[1];
    //printk("argc|%x argv[0]=%x argv[1]=%x argv[2]=%x argv[3]=%x argv[4]=%x argv[5]=%x\n", argc, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
    DBG_P(0x8, 0x03, 0x710105, 4, argc, 4, argv[0], 4, argv[1], 4, argv[2], 4, argv[3], 4, argv[4], 4, argv[5]);  // argc|%x argv[0]=%x argv[1]=%x argv[2]=%x argv[3]=%x argv[4]=%x argv[5]=%x
    for(i = 1; i < argc; i++){
        data = argv[i];
        if(program_otp_data(data, offset + DOC_REG_GAP) != 0){
            TCG_ERR_PRN("Error!! otp write fail ...\n");
            DBG_P(0x01, 0x03, 0x7F7F13);  // Error!! otp write fail ...
            return 0;
        }
        offset += 4;
    }
    //printk("otp write ok ...\n");
    DBG_P(0x01, 0x03, 0x710107 );  // otp write ok ...
    return 0;
}

int secure_boot_enable_agrent(req_t *req, bool first)
{
    U16 otp_selector_tag = 0;
    // otp_selector_tag = 0 --> first otp
    // otp_selector_tag = 0x55AA --> second otp
    if(first == FALSE){
        otp_selector_tag = 0x55AA;
    }
    tcg_ipc_post_ex(req, MSG_TCG_SECURE_BOOT_ENABLE, nvmet_core_cmd_done, otp_selector_tag, 0, NULL);
    return zOK;
}

tcg_code void tcg_disable_mbrshadow(void)
{
    U64 tmp64;
    U8  i, j;

    //1. update LckTableTbl, disable "UID_MBRControl" and "UID_MBR"
    for (i=0; i<pG2->b.mLckTbl_Tbl.hdr.rowCnt; i++)
    {
        tmp64 = pG2->b.mLckTbl_Tbl.val[i].uid;
        if ((tmp64 == UID_Table_MBRControl) || (tmp64 == UID_Table_MBR))
        {
            pG2->b.mLckTbl_Tbl.val[i].uid = tmp64 | UID_FF;
        }
    }

    //2. update LckAccessCtrlTbl,  disable UID_ACE_MBRContrl...,
    for (j=0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.ace)/sizeof(sAxsCtrl_TblObj); j++)
    {
        tmp64 = pG2->b.mLckAxsCtrl_Tbl.ace[j].invID;
        if ((tmp64==UID_ACE_MBRControl_Admins_Set) || (tmp64==UID_ACE_MBRControl_Set_Done))
        {
            pG2->b.mLckAxsCtrl_Tbl.ace[j].mtdID= ~UID_MethodID_Get;
        }
    }

    //3. update LckAccessCtrlTbl,  disable UID_MBRContrl...,
    for (j=0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.mbrCtrl)/sizeof(sAxsCtrl_TblObj); j++)
    {
        tmp64 = pG2->b.mLckAxsCtrl_Tbl.mbrCtrl[j].invID;
        if (tmp64==UID_MBRControl)
        {
            pG2->b.mLckAxsCtrl_Tbl.mbrCtrl[j].mtdID= ~UID_MethodID_Get;
        }
    }

    //4. update LckAccessCtrlTbl,  disable UID_MBR...,
    for (j=0; j<sizeof(pG2->b.mLckAxsCtrl_Tbl.mbr)/sizeof(sAxsCtrl_TblObj); j++)
    {
        tmp64 = pG2->b.mLckAxsCtrl_Tbl.mbr[j].invID;
        if (tmp64==UID_MBR)
        {
            pG2->b.mLckAxsCtrl_Tbl.mbr[j].mtdID= ~UID_MethodID_Get;
        }
    }

    //5. update LckAceTbl?
    for(i=0; i<pG3->b.mLckACE_Tbl.hdr.rowCnt; i++)
    {
        tmp64 = pG3->b.mLckACE_Tbl.val[i].uid;
        if ((tmp64 == UID_ACE_MBRControl_Admins_Set) || (tmp64 == UID_ACE_MBRControl_Set_Done))
        {
            pG3->b.mLckACE_Tbl.val[i].uid = tmp64|UID_FF;
        }
    }
}

tcg_code bool chkPsidAuthenticated(void)
{
    return mSessionManager.HtSgnAuthority.all == UID_Authority_PSID;
}

#endif // Jack Li
