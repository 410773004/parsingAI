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
#if _TCG_ //Jack Li

#include "sect.h"
#include "assert.h"
#include "req.h"
#include "nvmet.h"
#include "misc.h"
#include "string.h"
#include "dtag.h"
#include "dma.h"

//#include "btn_export.h"

#include "tcgnvme.h"
#include "tcgcommon.h"
#include "tcg.h"
#include "tcg_sh_vars.h"
#include "tcg_if_vars.h"

#ifdef TCG_NAND_BACKUP
#include "bf_mgr.h"
#include "ncl.h"
#include "eccu.h"
#include "tcg_nf_mid.h"
#include "tcg_if_nf_api.h"
#include "epm.h"
#include "ssstc_cmd.h"
extern epm_info_t* shr_epm_info;

#endif

#if(degrade_mode == ENABLE)
#include "cmd_proc.h"
#endif

#define __FILEID__ tcgnvme
#include "trace.h"

#if _TCG_
#include "crypto.h"

#endif
extern bool btn_wr_cmd_idle(void);
extern bool btn_rd_cmd_idle(void);

tper_status_t  f_SendCmd_Pid00(req_t *req);
tper_status_t  f_SendCmd_Pid01(req_t *req);
tper_status_t  f_SendCmd_Pid02(req_t *req);
tper_status_t  f_RecvCmd_Pid00(req_t *req);
tper_status_t  f_RecvCmd_Pid01(req_t *req);
tper_status_t  f_RecvCmd_Pid02(req_t *req);

extern u16 swap_u16(u16 data);
extern u32 swap_u32(u32 data);
extern u64 swap_u64(u64 data);
extern void fill_staus_token_list(void);
extern void fill_no_data_token_list(void);

extern void tcg_restore_lockOnReset_params(void);

extern u32 mTcgStatus;        //TCG status variable for others


typedef enum
{
    cMcPid00 = 0,       ///< 00
    cMcPid01,           ///< 01
    cMcPid02,           ///< 02
    cMcPidLast,         ///< Last
} MsgPid_t;

typedef enum
{
    cMcCmdOpc81 = 0,    ///< 00
    cMcCmdOpc82,        ///< 01
    cMcCmdOpcLast,      ///< Last
} MsgCmdOpc_t;

fast_data u8 evt_admin_sec_tcg_init = 0xff;
fast_data_zi tcg_io_chk_func_t tcg_io_chk_range;
fast_data_zi tcg_rdlk_list_t* tcg_rdcmd_list;

//TPER only support IF-SEND commands with security protocol ID (Pid) of 0x01 & 0x02 ,
//IF-Recieve commands with Pid of 0x00 & 0x01 & 0x02 

static ddr_data tper_status_t (*gCbfProc_Cmd_Pid_Map[cMcCmdOpcLast][cMcPidLast])(req_t *req) =
{
    {f_SendCmd_Pid00,  f_SendCmd_Pid01, f_SendCmd_Pid02},
    {f_RecvCmd_Pid00,  f_RecvCmd_Pid01, f_RecvCmd_Pid02}
};

/****************************************************************************
 * host_Properties_Reset():  <--- HostPropertiesReset();
 ****************************************************************************/
//reset TPer's knowledge of Host Properties...
ddr_code void host_Properties_Reset(void)
{
    mHostProperties[0].val = HOST_MAX_COMPKT_SZ;
    mHostProperties[1].val = HOST_MAX_PKT_SZ;
    mHostProperties[2].val = HOST_MAX_INDTKN_SZ;
    mHostProperties[3].val = 1;
    mHostProperties[4].val = 1;
    mHostProperties[5].val = 1;
}

/****************************************************************************
 * tcgStackReset():  <--- TcgStackReset();
 ****************************************************************************/
ddr_code void tcgStackReset(req_t *req)
{
	tcg_core_trace(LOG_INFO, 0xe1fe, "TCG StackReset()");

	if(mSessionManager.state == SESSION_START)
        ResetSessionManager();

    host_Properties_Reset();

    mHandleComIDRequest = 0x02;
    mHandleComIDResponse = 0x00;

    gTcgCmdState = ST_AWAIT_IF_SEND;       //need to reply status...
}


ddr_code void prepare_Pid02_GetComId_resp(req_t *req, u8* buf)
{
	struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
    TCG_Pid02_GetComId_Resp_t *pbuf = (TCG_Pid02_GetComId_Resp_t *)buf;
    // DBG_P(3, 3, 0x82010F, 1, mHandleComIDRequest, 1, mHandleComIDResponse); //82 01 0F, "[F]HandleComIDResponse Stack_Reset Request[%X] Response[%X]", 1 1

	tcg_core_trace(LOG_INFO, 0x9988, "prepare_Pid02_GetComId_resp()");

    //cj ToDO: return NG if mHandleComIDRequest==0? (no request, no response?) 2016/05/17
    memset(buf, 0, sizeof(TCG_Pid02_GetComId_Resp_t));
    pbuf->comId = swap_u16(cmd_dw10.b.com_id);
    pbuf->extendedComId = 0;

    if(mHandleComIDRequest == 2)
    {
        pbuf->requestCode        = swap_u32((u32)2);
        pbuf->availableDataLen   = swap_u16((u16)4);
        if(mHandleComIDResponse != 0x00)  pbuf->currentStatus = swap_u32((u32)1);   // Failure
    }
    else
    { //No Response Available
        pbuf->comId = swap_u16(cmd_dw10.b.com_id);
    }

    mHandleComIDRequest = 0;
	
}

ddr_code u16 process_Pid02_GetComId_req(req_t *req, u8* buf)
{
	TCG_Pid02_GetComId_req_t *pbuf = (TCG_Pid02_GetComId_req_t *)buf;
    u8  errCode = 0;

    // Get data from host and store in ATACmdBuf
    // if(siloIdxCmd==0)   //normal IF-SEND
        // HostTx2TcgBuf(pHcmdQ, buf);

    // TCG_PRINTF("Stack_Reset Request: ");
    if(swap_u16(pbuf->comId) != BASE_COMID){
        errCode = 0xF0;   goto REQUEST_NG;
    }

    if(swap_u16(pbuf->extendedComId) != 0){
        errCode = 0xF1;   goto REQUEST_NG;
    }

    if(swap_u32(pbuf->requestCode) != 2){   // TODO: how to reponse if not STACK_RESET request?
        errCode = 0xF2;   goto REQUEST_NG;
    }

    tcgStackReset(req);

    // DBG_P(3, 3, 0x82010E, 1, 0x00, 1, errCode); //OK  //82 01 0E, "[F]HandleComIDRequest(Stack_Reset) : %X errCode = %X", 1 1
    return mTRUE;

REQUEST_NG:
    mHandleComIDRequest  = 0x02;       //TODO: need to check...
    mHandleComIDResponse = 0x01;
	tcg_core_trace(LOG_INFO, 0xcfc4, "Error!! StackReset errCode|%x",errCode);

	return mTRUE;

}


/****************************************************************************
 * tcg_prepare_ComPacket():  <--- TcgCmdPkt4NoData();
 ****************************************************************************/
ddr_code void tcg_prepare_ComPacket(void)
{
	ComPacket_t *pbuf = (ComPacket_t *)dataBuf;

    memset(dataBuf, 0x00, DTAG_SZE);   //clear Tcg Recv Buffer
    pbuf->Reserved        = 0;
    pbuf->ComID           = swap_u16((u16)BASE_COMID);
    pbuf->ExtendedComID   = 0;
    pbuf->OutstandingData = 0;
    pbuf->MinTransfer     = 0;
    pbuf->Length          = 0;

}


/****************************************************************************
 * f_SendCmd_Pid00():  
 //TPER only support IF-SEND commands with security protocol ID (Pid) of 0x01 & 0x02 
 ****************************************************************************/

ddr_code tper_status_t f_SendCmd_Pid00(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
	nvme_tcg_cmd_dw11_t cmd_dw11 = (nvme_tcg_cmd_dw11_t)cmd->cdw11;
	
	nvme_tcg_trace(LOG_INFO, 0x0615, "f_SendCmd_Pid00() comid|%x tgtComid|%x", cmd_dw10.b.com_id, tgtComID);

	if(gTcgCmdState == ST_AWAIT_IF_RECV)
    {
        if(cmd_dw11.len_alloc) return TPER_SYNC_PROTOCOL_VIOLATION;
    }
    
    if(cmd_dw11.len_alloc > MAX_COMPKT_SZ)   //add one sector for eDrive  //(MAX_COMPKT_SZ/512)))
    {
#if (_TCG_ == TCG_EDRV)
        if(siloIdxCmd)  //1667 protocol
        {
            if(cmd_dw11.len_alloc > (MAX_COMPKT_SZ+512)) return TPER_INVALID_TX_PARAM_SEND;
        }
        else   //TCpG2->0 non-eDriver
#endif
        {
            return  TPER_INVALID_TX_PARAM_SEND;
        }
    }

    return TPER_INVALID_SEC_PID_PARAM;
}

/****************************************************************************
 * f_SendCmd_Pid01():
 ****************************************************************************/

ddr_code tper_status_t f_SendCmd_Pid01(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
	nvme_tcg_cmd_dw11_t cmd_dw11 = (nvme_tcg_cmd_dw11_t)cmd->cdw11;
	

    nvme_tcg_trace(LOG_INFO, 0xe123, "f_SendCmd_Pid01() comid|%x tgtComid|%x", cmd_dw10.b.com_id, tgtComID);

	if(gTcgCmdState == ST_AWAIT_IF_RECV)
    {
        if(cmd_dw11.len_alloc)
            return TPER_SYNC_PROTOCOL_VIOLATION;
    }

    if(cmd_dw11.len_alloc > MAX_COMPKT_SZ)   //add one sector for eDrive  //(MAX_COMPKT_SZ/512)))
    {
#if (_TCG_ == TCG_EDRV)
        if(siloIdxCmd)  //1667 protocol
        {
        	nvme_tcg_trace(LOG_ERR, 0x9516, "eDrive Len Error: 0x%x", cmd_dw11.len_alloc);
            if(cmd_dw11.len_alloc > (MAX_COMPKT_SZ+512))
                return TPER_INVALID_TX_PARAM_SEND;
        }
        else   //TCpG2->0 non-eDriver
#endif
        {
        	nvme_tcg_trace(LOG_ERR, 0xb6a7, "non-eDrive Len Error: 0x%x", cmd_dw11.len_alloc);
            return  TPER_INVALID_TX_PARAM_SEND;
        }
    }

    if(cmd_dw10.b.com_id == 0x0001)
    {
    	nvme_tcg_trace(LOG_ERR, 0x5a51, "COM ID 0x0001");
    	//L0_Discovery for IF-Send: do nothing
        // HostTx2TcgBuf(pHcmdQ, TcgHstTxRxBuf);
        tgtComID = 0x0001;
        //commonTP A1_4_1, gTcgCmdState = ST_AWAIT_IF_RECV;
        return TPER_GOOD;
    }

    if(cmd_dw10.b.com_id != BASE_COMID)
    {
    	nvme_tcg_trace(LOG_ERR, 0xad04, "COM ID NG !!!");
        return TPER_OTHER_INVALID_CMD_PARAM;
    }

    tgtComID = BASE_COMID;
    return TPER_CONTINUE;
}


/****************************************************************************
 * f_SendCmd_Pid02():
 ****************************************************************************/
ddr_code tper_status_t f_SendCmd_Pid02(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
	nvme_tcg_cmd_dw11_t cmd_dw11 = (nvme_tcg_cmd_dw11_t)cmd->cdw11;
    void *xfer_buf = req->req_prp.sec_xfer_buf;
	tper_status_t tperStatus = TPER_GOOD;

	nvme_tcg_trace(LOG_INFO, 0x30da, "f_SendCmd_Pid02() comid|%x tgtComid|%x xfer_buf|%x\n", cmd_dw10.b.com_id, tgtComID, (u32)xfer_buf);
	
    if ((cmd_dw10.b.com_id == BASE_COMID) && (cmd_dw11.len_alloc))  //(cmd->len==1)??
    { //deal with STACK_RESET only, Core 3.3.4.7.5
        process_Pid02_GetComId_req(req, xfer_buf);
    }
    else if((cmd_dw10.b.com_id == 0x0004) && cmd_dw11.len_alloc)
    { //TPer reset, Opal2 3.2.3
        if (TcgTperReset(req) == mFALSE)
            tperStatus = TPER_OTHER_INVALID_CMD_PARAM;
    }

    #if TCG_FS_BLOCK_SID_AUTH
    else if((cmd_dw10.b.com_id == 0x0005) && cmd_dw11.len_alloc)
    { 
        if(TcgBlkSIDAuthentication(req) != STS_SUCCESS){		
			tperStatus = TPER_OTHER_INVALID_CMD_PARAM;
        }
	}
    #endif

    else
        tperStatus = TPER_OTHER_INVALID_CMD_PARAM;

    return tperStatus;
}

/****************************************************************************
 * f_RecvCmd_Pid00():
 ****************************************************************************/
ddr_code tper_status_t f_RecvCmd_Pid00(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;	
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
    void *xfer_buf = req->req_prp.sec_xfer_buf;

	tcg_core_trace(LOG_INFO, 0xb293, "f_RecvCmd_Pid00() comid|%x tgtComid|%x xfer_buf|%x\n", cmd_dw10.b.com_id, tgtComID, (u32)xfer_buf);

    if (cmd_dw10.b.com_id == 0x0000)
    {   // Supported Protocols...
        Supported_Security_Protocol(xfer_buf);
        return TPER_GOOD;
    }
    else if (cmd_dw10.b.com_id == 0x0001)
    {   // Certificate Data... no data
        return TPER_GOOD;
    }
    return TPER_OTHER_INVALID_CMD_PARAM;
}

/****************************************************************************
 * f_RecvCmd_Pid01():
 ****************************************************************************/
ddr_code tper_status_t f_RecvCmd_Pid01(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
    void *xfer_buf = req->req_prp.sec_xfer_buf;

	tcg_core_trace(LOG_INFO, 0xcc82, "f_RecvCmd_Pid01() comid|%x tgtComid|%x xfer_buf|%x\n", cmd_dw10.b.com_id, tgtComID, (u32)xfer_buf);

    if(cmd_dw10.b.com_id == 0x0001)
    {
        Level0_Discovery(xfer_buf);

        if(tgtComID == 0x0001) // SEND-RECV pair
            gTcgCmdState = ST_AWAIT_IF_SEND;

        return TPER_GOOD;
    }
    else if(cmd_dw10.b.com_id != tgtComID)
    {
        return TPER_OTHER_INVALID_CMD_PARAM;
    }

    return TPER_CONTINUE;
    //TODO: if gTcgCmdState=ST_AWAIT_IF_SEND, return "no further data"
}

/****************************************************************************
 * f_RecvCmd_Pid02():
 ****************************************************************************/
ddr_code tper_status_t f_RecvCmd_Pid02(req_t *req)
{ 	
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
	nvme_tcg_cmd_dw11_t cmd_dw11 = (nvme_tcg_cmd_dw11_t)cmd->cdw11;
    void *xfer_buf = req->req_prp.sec_xfer_buf;

	tcg_core_trace(LOG_INFO, 0x3bd6, "f_RecvCmd_Pid02() comid|%x tgtComid|%x xfer_buf|%x\n", cmd_dw10.b.com_id, tgtComID, (u32)xfer_buf);

    if ((cmd_dw10.b.com_id == BASE_COMID) && (cmd_dw11.len_alloc))  //(cmd->len==1)??
    {
        prepare_Pid02_GetComId_resp(req, xfer_buf);     //deal with STACK_RESET only, Core 3.3.4.7.5
        return TPER_GOOD;
    }
    // GetComID, not supported
    return TPER_OTHER_INVALID_CMD_PARAM;
}

/***********************************************************
 *tcg_prepare_respPacket_update() --> TcgRespPktUpdate(void):
 *
 *  dataBuffer for IF-RECV
 *  1. Add the Method Status List
 *  2. Update the payload length
 *
 *  return 0 if no error.
 ***********************************************************/
ddr_code u16 tcg_prepare_respPacket_update(bool addStatus)
{
	SDS_t *pbuf = (SDS_t *)dataBuf;
    // U16 j;
	
	tcg_core_trace(LOG_INFO, 0xfcf0, "tcg_prepare_respPacket_update()");

    //add Method Status List
    if(addStatus == mTRUE)
    { //No method status fo CloseSession
        //Status list
        fill_staus_token_list();
    }

    iDataBuf -= offsetof(SDS_t, DataPayLoad);
    pbuf->DataSubPacket.Length = swap_u32((u32)iDataBuf);

    iDataBuf = ((iDataBuf + sizeof(u32) - 1) / sizeof(u32)) * sizeof(u32); // multiple of 4
    iDataBuf += sizeof(DataSubPacket_t);
    pbuf->Packet.Length = swap_u32((u32)iDataBuf);

    iDataBuf += sizeof(Packet_t);
    pbuf->ComPacket.Length = swap_u32((u32)iDataBuf);

    // rcvCmdPktLen = iDataBuf + 20;
    rcvCmdPktLen = iDataBuf + sizeof(ComPacket_t);

    if(iDataBuf > mHostProperties[1].val)   //hostMaxPacketSize)
    {
        // DBG_P(3, 3, 0x820171, 2, rcvCmdPktLen, 4, mHostProperties[1].val);  //82 01 71, "pktSz > MaxPktSz %4X %X", 2 4
        return  STS_RESPONSE_OVERFLOW;
    }

    if(rcvCmdPktLen > mHostProperties[0].val)   //hostMaxComPacketSize)
    {
        // DBG_P(3, 3, 0x820172, 2, rcvCmdPktLen, 4, mHostProperties[0].val);  //82 01 72, "pktSz > MaxComPktSz %4X %X", 2 4
        return  STS_RESPONSE_OVERFLOW;
    }

    if((iDataBuf - (sizeof(Packet_t) + sizeof(DataSubPacket_t))) > mHostProperties[2].val)   //hostIndTokenSize)
    {
        // DBG_P(3, 3, 0x820173, 2, rcvCmdPktLen, 4, mHostProperties[2].val);  //82 01 73, "indTokenSz > MaxPktSz %4X %X", 2 4
        return  STS_SESSION_ABORT;  //STS_RESPONSE_OVERFLOW;
    }
    return STS_SUCCESS;
}


/***********************************************************
 *TcgCmdPkt4Response(void):
 *
 *  Only prepare the Command Response Packet in Buffer, Payload and Status
 *  should be prepared in TcgStreamDecode
 *
 *  return 0 if no error.
 ***********************************************************/
ddr_code void tcg_prepare_respPacket(void)
{
	SDS_t *pbuf = (SDS_t *)dataBuf;

    tcg_prepare_ComPacket();   // TcgCmdPkt4NoData();
    pbuf->Packet.TSN       = swap_u32(mCmdPkt.mPktFmt.TSN);
    pbuf->Packet.HSN       = swap_u32(mCmdPkt.mPktFmt.HSN);
    pbuf->Packet.SeqNumber = swap_u32(mCmdPkt.mPktFmt.SeqNo);

    // iDataBuf = sizeof(SDS_t);
    iDataBuf = offsetof(SDS_t, DataPayLoad);

}

ddr_code tper_status_t tcg_tper_handle(req_t *req)
{
	struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
	nvme_tcg_cmd_dw11_t cmd_dw11 = (nvme_tcg_cmd_dw11_t)cmd->cdw11;
    void *xfer_buf = req->req_prp.sec_xfer_buf;
    tper_status_t tperStatus = TPER_GOOD;
    
	tcg_core_trace(LOG_INFO, 0xd543, "tcg_tper_handle()");

    if (cmd_dw10.b.protocol_id > 2)
        return TPER_OTHER_INVALID_CMD_PARAM;

    tperStatus = gCbfProc_Cmd_Pid_Map[cmd->opc - IF_SEND][cmd_dw10.b.protocol_id](req);

	tcg_core_trace(LOG_INFO, 0xf373, "tcg_tper_handle() -> result|%x, gTcgCmdState|%x",tperStatus,gTcgCmdState);

	if (tperStatus != TPER_CONTINUE){
        return tperStatus;
	}

	/* ProtocolID==1 for the following state processing : */
    switch(gTcgCmdState)
    {
        u16 result;
        case ST_AWAIT_IF_SEND:
            if(cmd->opc == IF_SEND)
            {   // security send
                // Get data from host and store in xfer_buf
                gTcgCmdState = ST_PROCESSING;
            }
            else
            {   // security receive
                tcg_prepare_ComPacket();  // TcgCmdPkt4NoData();
                #if _TCG_ == TCG_EDRV
                if(siloIdxCmd) { //1667 format
                    rcvCmdPktLen = 56;
                    memcpy(xfer_buf + 0x20, (void*)dataBuf, TCG_BUF_LEN);    //sector count?
                }
                else
                #endif
                memcpy(xfer_buf, (void*)dataBuf, TCG_BUF_LEN);    //sector count?
                break;
            }

        case ST_PROCESSING:
            #if _TCG_ == TCG_EDRV
            if(siloIdxCmd){   //1667 format
                result = tcg_cmdPkt_extracter(req, xfer_buf + 0x20);
            }
            else
            #endif
            result = tcg_cmdPkt_extracter(req, xfer_buf);

            if(result == STS_SESSION_ABORT)
            {
                ResetSessionManager();
                tcg_cmdPkt_abortSession();
                #if _TCG_ == TCG_EDRV
                if(siloIdxCmd) //&&(ErrStatus1667!=STS_1667_SUCCESS))
                    tcg_cmdPkt_closeSession();   // TcgCmdPkt4CloseSession();
                #endif
                gTcgCmdState = ST_AWAIT_IF_RECV;
                break;
            }
            else if(result == STS_STAY_IN_IF_SEND)
            {
                gTcgCmdState = ST_AWAIT_IF_SEND;
                break;
            }

            //prepare CmdPacket in dataBuffer[] (for IF-RECV)
            tcg_prepare_respPacket(); // TcgCmdPkt4Response();

            if(result != STS_SUCCESS)
            {
                fill_no_data_token_list();
                //add status to reponse buffer and update length
                set_status_code(result, 0, 0);
                tcg_prepare_respPacket_update(mTRUE);   // TcgRespPktUpdate();

                gTcgCmdState = ST_AWAIT_IF_RECV;
                break;
            }

            // Decode the payload, process the data, and prepare the response payload ...
            // ex: SessionManager, SP, Table processing ...
            keep_result = result = tcg_cmdPkt_payload_decoder(req);

			tcg_core_trace(LOG_INFO, 0x8b04, "cg_cmdPkt_payload_decoder keep_result=%x",keep_result);

            if(req->completion == nvmet_core_cmd_done){
                if(result == STS_STAY_IN_IF_SEND)
                {
                    // DBG_P(1, 3, 0x820166);  //82 01 66, "!!NG: StreamDecode NG -> Stay in IF-SEND"
                    gTcgCmdState = ST_AWAIT_IF_SEND;
                    break;
                }

                if(result == STS_SESSION_ABORT)
                { //prepare payload for "Close Session"
                    // DBG_P(1, 3, 0x820167);  //82 01 67, "!!NG: StreamDecode NG -> Abort Session"
                    ResetSessionManager();
                    tcg_cmdPkt_abortSession();
                }
                else if(result == STS_RESPONSE_OVERFLOW)
                {
                    tcg_prepare_respPacket();  //TcgCmdPkt4Response();
                    fill_no_data_token_list();

                    //add status to reponse buffer and update length
                    set_status_code(result, 0, 0);
                    tcg_prepare_respPacket_update(mTRUE);  // TcgRespPktUpdate();
                }

                gTcgCmdState = ST_AWAIT_IF_RECV;
            }
            break;

        case ST_AWAIT_IF_RECV:
            if(cmd->opc == IF_RECV)
            { //if(IF-RECV is able to retrieve the entire response resulting from the IF-SEND ...
                if(cmd_dw11.len_trans < rcvCmdPktLen)   //if xfer length is not enough...  //Max TBC
                {
                    #if _TCG_ == TCG_EDRV
                    if(siloIdxCmd)
                    {  //1667 format
                        memcpy(xfer_buf + 0x20, (void*)dataBuf, 8);
                        fill_u16((u8 *)xfer_buf + 10 + 0x20, rcvCmdPktLen);    //OutstandingData
                        fill_u16((u8 *)xfer_buf + 14 + 0x20, rcvCmdPktLen);    //MinTransfer
                        rcvCmdPktLen = 0x38;
                    }
                    else
                    #endif
                    {
                        memcpy(xfer_buf, (void*)dataBuf, 8);
                        fill_u16((u8 *)xfer_buf + 10, rcvCmdPktLen);    //OutstandingData
                        fill_u16((u8 *)xfer_buf + 14, rcvCmdPktLen);    //MinTransfer
                    }
                    gTcgCmdState = ST_AWAIT_IF_RECV;
                    break;
                }

                //put response data to ATA buffer...
                #if _TCG_ == TCG_EDRV
                if(siloIdxCmd)  //1667 format
                    memcpy(xfer_buf + 0x20, (void*)dataBuf, TCG_BUF_LEN);     //sector count?
                else
                #endif
                memcpy(xfer_buf, (void*)dataBuf, TCG_BUF_LEN);    //sector count?

                gTcgCmdState = ST_AWAIT_IF_SEND;
            }
            else
            {   // IF_SEND
                return TPER_SYNC_PROTOCOL_VIOLATION; // for DM test
            }
            break;

        default:
            break;
    }

    return TPER_GOOD; // zOK;
}


ddr_code enum cmd_rslt_t tcg_cmd_handle(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
	//nvme_tcg_cmd_dw10_t cmd_dw10 = (nvme_tcg_cmd_dw10_t)cmd->cdw10;
    void *xfer_buf = req->req_prp.sec_xfer_buf;
    u32 xfer_buf_size = SECURE_COM_BUF_SZ;
    enum cmd_rslt_t res = HANDLE_RESULT_FAILURE;
	u8 wait_io_counter = 0;

    sys_assert(xfer_buf != NULL);
    nvme_tcg_trace(LOG_INFO, 0x6c1e, "opcode(0x%x) pid(0x%x) comID(0x%x) len(0x%x) buf(0x%x)", cmd->opc, ((nvme_tcg_cmd_dw10_t)(cmd->cdw10)).b.protocol_id, ((nvme_tcg_cmd_dw10_t)(cmd->cdw10)).b.com_id, cmd->cdw11, (u32)xfer_buf);
	
    // check invalid cmd, length & PID
    if(((cmd->opc != IF_SEND) && (cmd->opc != IF_RECV)) ||
        (cmd->cdw11 == 0) || (cmd->cdw11 > xfer_buf_size) )
    {
        return HANDLE_RESULT_FAILURE;
    }

    // memset(xfer_buf, 0x00, sizeof(DTAG_SZE));
    tcg_ioCmd_inhibited = mTRUE;
    nvmet_io_fetch_ctrl(tcg_ioCmd_inhibited);  // disable IO new cmd in
    // --- wait exist io cmd finish ---
    while((!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle())){
		mdelay(100);
		if (wait_io_counter++ > 10)
			break;
    }

    #if _TCG_ == TCG_EDRV
    if (cmd_dw10.b.protocol_id == TCG_SSC_EDRIVE) {
        silo_cmd_handle(req);
    }
    else
    #endif
    {
        if (mSessionManager.sessionStartTime)
        {
            // get current time
            u32 elapsed = time_elapsed_in_ms(mSessionManager.sessionStartTime);
            nvme_tcg_trace(LOG_INFO, 0x9aad, "Session start time: 0x%x, elapsed time: %d ms", mSessionManager.sessionStartTime, elapsed);
			
            if (elapsed > mSessionManager.sessionTimeout)
            {
                nvme_tcg_trace(LOG_INFO, 0x3b9b, "Error!! Session Timeout [0x%x]", mSessionManager.sessionTimeout);
                ResetSessionManager();
            }
        }

        tcg_tper_status = tcg_tper_handle(req);
        if(tcg_tper_status != TPER_GOOD) {
            switch(tcg_tper_status) {
                case TPER_SYNC_PROTOCOL_VIOLATION:
                    res = HANDLE_RESULT_FAILURE;
            		break;

                //case TPER_INVALID_SEC_PID_PARAM:
                //case TPER_OTHER_INVALID_CMD_PARAM:
                //case TPER_INVALID_TX_PARAM_SEND:
                default:
                    res = HANDLE_RESULT_FAILURE;
            		break;

                //case TPER_DATA_PROTECTION_ERROR:
                //case TPER_INVALID_SEC_STATE:
                //case TPER_OPERATION_DENIED:
            }
        }else{   // TPER_GOOD
            if(cmd->opc == IF_RECV){
                res = HANDLE_RESULT_DATA_XFER;

                if (mSessionManager.bWaitSessionStart)
                {
                	// start counter
                    mSessionManager.bWaitSessionStart = 0;
                    mSessionManager.sessionStartTime = get_tsc_64();
					
                    nvme_tcg_trace(LOG_INFO, 0xe4e8, "Session start time: 0x%x", mSessionManager.sessionStartTime);
                }
            }else if(cmd->opc == IF_SEND){
                if(req->completion == nvmet_core_cmd_done){
                    res = HANDLE_RESULT_FINISHED;
                }else{
                    res = HANDLE_RESULT_PENDING_BE;
                }
            }
        }
    }
    return res;
}

ddr_code void tcg_status_resume(void)
{
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);

	if(mTcgStatus != epm_aes_data->tcg_sts)
	{
		mTcgStatus = epm_aes_data->tcg_sts;

		if(mTcgStatus & MBR_SHADOW_MODE)
		{
			tcg_io_chk_range = TcgRangeCheck_SMBR;
			pG3->b.mLckMbrCtrl_Tbl.val[0].enable = true;
			pG3->b.mLckMbrCtrl_Tbl.val[0].done   = false;
		}
		else
		{
			tcg_io_chk_range = TcgRangeCheck;
			if(pG3->b.mLckMbrCtrl_Tbl.val[0].enable)
				pG3->b.mLckMbrCtrl_Tbl.val[0].done = true;
			else
				pG3->b.mLckMbrCtrl_Tbl.val[0].done = false;
		}
		
		//ipc_tcg_change_chkfunc_BTN_wr(mTcgStatus);
	}
	
	mReadLockedStatus  = epm_aes_data->readlocked;
	mWriteLockedStatus = epm_aes_data->writelocked;

	for(u8 i=0; i<=LOCKING_RANGE_CNT; i++)
	{
		if((mReadLockedStatus & BIT(i)) == 0)
			pG3->b.mLckLocking_Tbl.val[i].readLocked = 0;
		if((mWriteLockedStatus & BIT(i)) == 0)
			pG3->b.mLckLocking_Tbl.val[i].writeLocked = 0;
	}
}

ddr_code void tcg_rdcmd_list_init(void)
{
    tcg_rdcmd_list = NULL;
}

ddr_code void tcg_rdcmd_list_add(u32 val)
{
    tcg_rdlk_list_t* entry = sys_malloc(SLOW_DATA, sizeof(tcg_rdlk_list_t));
    entry->cmd_slot = val;
    entry->next = tcg_rdcmd_list;
    tcg_rdcmd_list = entry;
}

ddr_code bool tcg_rdcmd_list_search_del(u32 val)
{
    tcg_rdlk_list_t* tmpPtr = tcg_rdcmd_list;
    tcg_rdlk_list_t* prevPtr = NULL;
    while(tmpPtr!=NULL)
    {
        if(tmpPtr->cmd_slot == val)
        {
            if(prevPtr == NULL)
                tcg_rdcmd_list = NULL;
            else
                prevPtr->next = tmpPtr->next;
            sys_free(SLOW_DATA, tmpPtr);
            return true;
        }
        else
        {
            prevPtr = tmpPtr;
            tmpPtr = tmpPtr->next;
        }
    }
    return false;
}

/****************************************************************************
 * tcg_if_onetime_init():  <--- TcgInit_Cpu0();
 ****************************************************************************/
#if 1
ddr_code void tcg_if_onetime_init(bool bootup, bool buf_to_dram)
{
    //int st = zOK;

    //if (bootMode == cInitBootDeepPowerDown)
    //{
    //    pmu_restore(PM_CPU1_TCG_IDX);
    //}
	
#if 0//def TCG_NAND_BACKUP
	if(bTcgTblErr)
		return;
#endif

	//u32 old_mTcgStatus = mTcgStatus;

	if(pG1 == NULL) pG1 = (tG1 *)&G1;
    if(pG2 == NULL) pG2 = (tG2 *)&G2;
    if(pG3 == NULL) pG3 = (tG3 *)&G3;

	extern void *secure_com_buf;
	secure_com_buf = tcgTmpBuf + TCG_BE_BUF_SIZE;
	dataBuf        = secure_com_buf + SECURE_COM_BUF_SZ;

	tcg_req = nvmet_get_req();

    nvme_tcg_trace(LOG_INFO, 0xf13b, "tcg_if_onetime_init() bootMode|%x, bTcgTblErr|%x", bootMode, bTcgTblErr);
	//if(tcg_req == NULL) 
		//return zNG;
	INIT_LIST_HEAD(&tcg_req->entry);
    //INIT_LIST_HEAD(&tcg_req->inentry);  //Max modify

    // DBG_P(1, 3, 0x82010A);   //82 01 0A, "[F]TcgInit_CPU0"

    tcg_if_post_sync_response();
    // tcg_if_post_sync_sign();
    // following 4 function should be performed first.

	//SMBR_ioCmdReq = FALSE;    //Max modify
    tcg_ioCmd_inhibited = mFALSE;

    gTcgCmdState = ST_AWAIT_IF_SEND;
    mSessionManager.SPSessionID = BASE_SPSESSION_ID-1;    //0x1001
    mSessionManager.TransactionState = TRNSCTN_IDLE;
    ClearMtableChangedFlag();
    ResetSessionManager();

	memset(&mRawKey, 0, sizeof(mRawKey[TCG_MAX_KEY_CNT]));
	
    //Tcg_RdWrProcessing = FALSE;
    bTcgKekUpdate       = mFALSE;
    mHandleComIDRequest = 0x00;

#if _TCG_ == TCG_EDRV
    //b1667Probed = 0x00;
    siloIdxCmd = 0x00;
    SiloComID = BASE_COMID; //0x00;     //eDrive won't call GetSiloCap to get ComID @ Power-On

    bEHddLogoTest = mFALSE; //for WHQL test
    mPsidRevertCnt = 0;
#endif

    if (bTcgTblErr)
    {
#ifdef TCG_NAND_BACKUP
		epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		if(epm_aes_data->tcg_en_dis_tag != TCG_TAG)
			return;
#endif

        mTcgStatus = (TCG_TBL_ERR + TCG_ACTIVATED);
		mTcgActivated = TCG_ACTIVATED;

		#if(degrade_mode == ENABLE)
		extern none_access_mode_t noneaccess_mode_flags;
		//extern void cmd_disable_btn()
		extern smart_statistics_t *smart_stat;
		extern void cmd_proc_read_only_setting(u8 setting);
		

		smart_stat->critical_warning.bits.device_reliability = 1;
		noneaccess_mode_flags.b.tcg_key_table_fail = 1;
		cmd_proc_read_only_setting(true);
		cmd_disable_btn(-1,1);
		#endif
		
		memset(&mRawKey, 0, sizeof(mRawKey[TCG_MAX_KEY_CNT]));

		TcgChangeKey(0);

		tcg_set_aes_range(AES_XTS_256B_KEY, 0, 1, true, 0);
		
		crypto_hw_prgm_one_key((u8 *)&mRawKey[0].dek.aesKey, (u8 *)&mRawKey[0].dek.xtsKey, 0, AES_XTS_256B_KEY);
			
		crypto_mek_refresh_trigger();

		mReadLockedStatus = 0xffff;
   		mWriteLockedStatus = 0xffff;

#ifdef TCG_NAND_BACKUP
		//epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
		if(((epm_aes_data->tcg_err_flag & BIT0) == 0) && (epm_aes_data->prefmtted == TCG_INIT_TAG))
		{
			tcg_nf_allErase(false);
		}
#endif
		
		nvme_tcg_trace(LOG_INFO, 0x8544, "[TCG] !! Table Error R/W Lock !!");
		
    }
    else
    {
#ifdef TCG_NAND_BACKUP
		if(bootup)  //<<PowerOn init>>
		{
			nvme_tcg_trace(LOG_INFO, 0x1042, "[TCG] check in bootup");
	#ifndef FW_UPDT_TCG_SWITCH
			if(buf_to_dram){
				memcpy((void *)pG1, (const void *)(tcgTmpBuf)                                                                                                                       , sizeof(tG1));
				memcpy((void *)pG2, (const void *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE))                                                             , sizeof(tG2));
				memcpy((void *)pG3, (const void *)(tcgTmpBuf + NAND_PAGE_SIZE*occupied_by(sizeof(tG1), NAND_PAGE_SIZE) + NAND_PAGE_SIZE*occupied_by(sizeof(tG2), NAND_PAGE_SIZE)), sizeof(tG3));
			}
	#endif
			LockingTbl_Reset(PowerCycle);		// Sync Tbl r/w lock (2.)Tbl key unwrap to mRawKey

			MbrCtrlTbl_Reset(PowerCycle);		// pG3->b.mLckMbrCtrl_Tbl.val[0].done should be zero after power on reset. (POWER CYCLE)

			bKeyChanged = mFALSE;
			
			LockingRangeTable_Update();         // (1.)Sync pLockingRangetbl (2.)tcg_init_aes_key_range() -> Using mRawKey initial AES register  (3.)Clear mRawKey

			//crypto_init();

			tcg_init_aes_key_range();			// initial AES register (key setting)
			
			//crypto_lba_map_enable(CRYPTO_LBA_DEC_EN | CRYPTO_LBA_ENC_EN);
		}
#endif

        //if(ChkDefaultTblPattern()==zOK) //todo: already done in CPU4
        {
            mTcgStatus = 0;
			mTcgActivated = 0;

			//reset CPin tries count
			CPinTbl_Reset();
			/*
            for(j = 0; j < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; j++)
                pG1->b.mAdmCPin_Tbl.val[j].tries = 0;

            for(j = 0; j < pG3->b.mLckCPin_Tbl.hdr.rowCnt; j++)
                pG3->b.mLckCPin_Tbl.val[j].tries = 0;
			*/
			
            if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
            {
                 mTcgStatus |= TCG_ACTIVATED;
				 mTcgActivated = TCG_ACTIVATED;
            }

            // if (initMode == cInitBootFwUpdated)
            // {
                // TcgRestoreVarFromEEP();
            // }
            #ifdef BCM_test
            //DumpTcgKeyInfo();  //Max modify
            #endif

            #if 0
            if (bootMode == cInitBootFwUpdated)
            {
                tcg_major_info_recover();
            }
            else
            {
                CPinTbl_Reset();

                //Locking Table Initialization
                MbrCtrlTbl_Reset(PowerCycle);       // pG3->b.mLckMbrCtrl_Tbl.val[0].done should be zero after power on reset. (POWER CYCLE)
                LockingTbl_Reset(PowerCycle);       // LckLocking_Tbl "PowerOnReset"
            }
            #else
			
			if((bootMode != cInitBootDeepPowerDown) && (warn_boot_restore_done == mFALSE)){
				//CPinTbl_Reset();

                //Locking Table Initialization
                //MbrCtrlTbl_Reset(PowerCycle);       // pG3->b.mLckMbrCtrl_Tbl.val[0].done should be zero after power on reset. (POWER CYCLE)
                
				
            }else{
                warn_boot_restore_done = mFALSE;
				nvme_tcg_trace(LOG_INFO, 0x9b80, "[X]LckOnRst bootMode[%08x] warn_boot_restore_done[%08x]\n", bootMode, warn_boot_restore_done);
                tcg_restore_lockOnReset_params();
            }
            #endif
			
            #ifdef BCM_test
            //printk("1111");
            //DBG_P(0x01, 0x03, 0x70000D );  // 1111
            //DumpTcgKeyInfo();  //Max modify
            #endif
			
			MbrCtrlTbl_Reset(PowerCycle);		// pG3->b.mLckMbrCtrl_Tbl.val[0].done should be zero after power on reset. (POWER CYCLE)
			
            SingleUser_Update();
			
            DataStoreAddr_Update();

			
			if(bKeyChanged == mTRUE) //bKeyChanged == mTRUE
			{ 
				LockingRangeTable_Update();  //<<for Preformat>> (1.)Sync pLockingRangetbl (2.)tcg_init_aes_key_range() -> Using mRawKey initial AES register  (3.)Clear mRawKey
			}
			#if 0 // (BUILD_SSD_CUSTOMER == SSD_CUSTOMER_DELL)
                tcg_disable_mbrshadow();
            #endif
		}
	}

    nvme_tcg_trace(LOG_INFO, 0x524a, "mTcgStatus|%x bTcgTblErr|%x\n", mTcgStatus, bTcgTblErr);

#if CO_SUPPORT_AES
    if(mTcgStatus&TCG_TBL_ERR)
        HAL_SEC_AesDummyReadMode();
#endif

    ClearMtableChangedFlag();
    bLockingRangeChanged = mFALSE;		
	
#ifdef _TCG_RESET_PSID
    {   //Copy PBKDF(MSID) to PSID
		
		Tcg_GenCPinHash(G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);
		
        //if(RW_WaitG1Wr()==zOK)
        if(TcgFuncRequest1(MSG_TCG_G1WR) == zOK)
            // DBG_P(1, 3, 0x820140);  //82 01 40, "reset PSID OK"
        else
            // DBG_P(1, 3, 0x820141);  //82 01 41, "reset PSID NG"
    }
#endif
	
    //TCGPRN("tcg_req|%x EE_Zerotag|%x\n", tcg_req, smSysInfo->d.FtlData.d.zeroTag);
	//nvme_tcg_trace(LOG_INFO, 0, "tcg_req|%x EE_Zerotag|%x\n", tcg_req, smSysInfo->d.FtlData.d.zeroTag);

	//DBG_P(0x3, 0x03, 0x70000F, 4, tcg_req, 4, smSysInfo->d.FtlData.d.zeroTag);  // tcg_req|%x EE_Zerotag|%x
    #if 1
	tcg_req->completion = NULL;
    nvmet_put_req(tcg_req);
	//cb_init_cache(tcg_req);  //Max modify
    #else
    if((st == zOK) && (smSysInfo->d.FtlData.d.zeroTag != SI_FTLDATA_TAG_ZERO)){
        st = init_cache(req);
    }
    nvmet_put_req(req);
    #endif

	// initialize io check function
	if(mTcgStatus & MBR_SHADOW_MODE)
		tcg_io_chk_range = TcgRangeCheck_SMBR;
	else
		tcg_io_chk_range = TcgRangeCheck;

	//if((mTcgStatus & MBR_SHADOW_MODE) != (old_mTcgStatus & MBR_SHADOW_MODE))
		//ipc_tcg_change_chkfunc_BTN_wr(mTcgStatus);
	
	globla_pLockingRangeTable = tcm_local_to_share(pLockingRangeTable);

	tcg_rdcmd_list_init();
    //return st;


}
#endif

#ifdef TCG_NAND_BACKUP

extern void sec_gen_sha3_256_hash(unsigned char *in_msg, unsigned int msg_len, unsigned char *hash);
extern AGING_TEST_MAP_t *MPIN;

ddr_code void tcg_preformat_init(void)
{
	u8 PSID_tmp[32] = {0};  //mp_info
	u8 digest[32] = {0};  //mp_info

	tcg_nf_allErase(true);

	//memset(&mRawKey, 0, sizeof(mRawKey));

	TcgChangeKey(0);  //<<crypto erase>> (1.)change global key to TCGTbl (2.)Sync mRawkey (3.)bKeyChanged = tuue

#if TCG_FS_PSID
	if(bTcgTblErr)
	{
		//handle PSID
		switch(MPIN->PSID_tag)
		{
			case PLAIN_PSID:
				memcpy(PSID_tmp, MPIN->PSID, sizeof(PSID_tmp));
				sec_gen_sha3_256_hash((u8 *)PSID_tmp, 32, digest);
				memcpy(MPIN->PSID, digest, sizeof(MPIN->PSID)); 
				MPIN->PSID_tag = DIGEST_PSID;
				nvme_tcg_trace(LOG_INFO, 0x2e01, "PSID CpoyHash from Mp Info to itself");
			case DIGEST_PSID:
				memcpy(pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_val, MPIN->PSID, sizeof(pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_val));
				pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_Tag = CPIN_IN_DIGEST;
				nvme_tcg_trace(LOG_INFO, 0x47f7, "PSID Cpoy from Mp Info to TCG Tbl");
				break;
			default:
				nvme_tcg_trace(LOG_INFO, 0xac70, "PSID is not except , back to default value!!");
				break;
		}
#ifdef PSID_PRINT_CHK
		for(u8 i = 0; i<32; i++)
		{
			nvme_tcg_trace(LOG_INFO, 0x523a, "[Max debug]MP_info[%x]|%x", i, MPIN->PSID[i]);
		}
		
		for(u8 i = 0; i<32; i++)
		{
			nvme_tcg_trace(LOG_INFO, 0xfaf9, "[Max debug]TCG tbl[%x]|%x", i, pG1->b.mAdmCPin_Tbl.val[6].cPin.cPin_val[i]);
		}
#endif
	}
#endif

	tcg_nf_G4WrDefault();

	// update EPM
	epm_aes_t* epm_aes_data = (epm_aes_t *)ddtag2mem(shr_epm_info->epm_aes.ddtag);
	if((epm_aes_data->tcg_en_dis_tag != TCG_TAG) && (epm_aes_data->tcg_en_dis_tag != NONTCG_TAG))
#if defined(E1S) || defined(UDOT2_SUPPORT) || defined(Mdot2_22110)
		epm_aes_data->tcg_en_dis_tag = TCG_TAG;
#else
		epm_aes_data->tcg_en_dis_tag = NONTCG_TAG;
#endif

	epm_aes_data->prefmtted = TCG_INIT_TAG;
	epm_aes_data->tcg_err_flag = 0;
	epm_aes_data->tcg_sts = 0;
	epm_aes_data->readlocked = 0;
	epm_aes_data->writelocked = 0;
	
	// IF init
	tcg_if_onetime_init(true, false);
	
	epm_update(AES_sign, (CPU_ID - 1));
}
#endif

#else


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
#include "NvmeSecurity.h"
#include "tcgcommon.h"
#include "tcgtbl.h"
#include "tcg.h"
#include "tcgnvme.h"
#include "dtag.h"
#include "tcg_if_vars.h"
#include "tcg_sh_vars.h"
#include "dpe.h"
#include "misc.h"

//-----------------------------------------------------------------------------
//  Define & Macros :
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type define :
//-----------------------------------------------------------------------------

typedef enum
{
    cMcPid00 = 0,       ///< 00
    cMcPid01,           ///< 01
    cMcPid02,           ///< 02
    cMcPidLast,         ///< Last
} MsgPid_t;

typedef enum
{
    cMcCmdOpc81 = 0,    ///< 00
    cMcCmdOpc82,        ///< 01
    cMcCmdOpcLast,      ///< Last
} MsgCmdOpc_t;

static tcm_data tper_status_t (*gCbfProc_Cmd_Pid_Map[cMcCmdOpcLast][cMcPidLast])(req_t *req) =
{
    {f_SendCmd_Pid00,  f_SendCmd_Pid01, f_SendCmd_Pid02},
    {f_RecvCmd_Pid00,  f_RecvCmd_Pid01, f_RecvCmd_Pid02}
};

typedef enum
{
    cMcTcgSilo_GetSiloCap = 0,
    cMcTcgSilo_Transfer,
    cMcTcgSilo_Reset,
    cMcTcgSilo_GetResult,
    cMcTcgSilo_TperReset,
    cMcTcgSilo_Last,
} msgTcgSilo_t;

#if _TCG_ == TCG_EDRV
static tcg_data void (*gCbfProc_TcgSilo_Map[cMcTcgSilo_Last])(req_t *req) =
{
    f_TcgSilo_GetSiloCap, f_TcgSilo_Transfer, f_TcgSilo_Reset, f_TcgSilo_GetResult, f_TcgSilo_TPerReset,
};
#endif

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//  Function Definitions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Exported variable reference
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Exported function reference
//-----------------------------------------------------------------------------
extern void HandleComIDResponse(U8* buf);
extern U32  tcg_cmdPkt_payload_decoder(req_t *req);
// extern void TcgStackReset(void);
extern void ResetSessionManager(req_t *req);
extern U16  tcg_cmdPkt_extracter(req_t *req, U8* buf);
extern int  init_cache(req_t *);
extern void tcg_cmdPkt_closeSession(void);
extern int  tcg_if_post_sync_sign(void);
extern void tcg_major_info_recover(void);
extern void cmd_proc_enable_fetch_IO(U8 IO_Enable, U8 AD_Enable);
extern U16 swap_u16(U16 data);
extern U32 swap_u32(U32 data);
extern U64 swap_u64(U64 data);
extern void fill_staus_token_list(void);
extern void fill_no_data_token_list(void);
extern void tcg_restore_lockOnReset_params(void);

ddr_data bool tcg_ioCmd_inhibited = mFALSE;

//-----------------------------------------------------------------------------
//
//  == Code trunk ==  == Code trunk ==  == Code trunk ==  == Code trunk ==
//
//-----------------------------------------------------------------------------
/****************************************************************************
 * Nvme_AdmCmd_TcgChk:
 ****************************************************************************/
tcm_code bool Nvme_AdmCmd_TcgChk(req_t *req)
{
    bool st = TRUE;   // OK
    struct nvme_cmd *cmd = req->host_cmd;
    printk("Nvme_AdmCmd_TcgChk():\n");
    DBG_P(0x01, 0x03, 0x700014 );  // Nvme_AdmCmd_TcgChk():

    switch (cmd->opc)
    {
        case NVME_OPC_FIRMWARE_COMMIT:
        case NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD:
            if(mWriteLockedStatus || mReadLockedStatus || (mTcgStatus & MBR_SHADOW_MODE))
            { // if drive is locked or in MBR-S mode, FW update is not allowed!
               nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
               st = FALSE;
               TCG_ERR_PRN("FW update is not allowed : Tcg Locked!!\n");
               DBG_P(0x01, 0x03, 0x7F7F44 );  // FW update is not allowed : Tcg Locked!!
            }
            break;

        case NVME_OPC_SANITIZE:
            if(mTcgStatus & TCG_ACTIVATED)
            {
                nvmet_set_status(&req->fe, NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
                st = FALSE;
                TCG_ERR_PRN("Tcg Invalid Security State!!\n");
                DBG_P(0x01, 0x03, 0x7F7F45 );  // Tcg Invalid Security State!!
            }
            break;

        case NVME_OPC_FORMAT_NVM:
            //if(mTcgStatus&TCG_ACTIVATED)    //SIIS_v1.04
            if(mWriteLockedStatus)        //SIIS_v1.05,
            {
                nvmet_set_status(&req->fe, NVME_SCT_COMMAND_SPECIFIC, NVME_SC_INVALID_FORMAT);
                st = FALSE;
                TCG_ERR_PRN("FW format is not allowed : Tcg Invalid Security State!!\n");
                DBG_P(0x01, 0x03, 0x7F7F46 );  // FW format is not allowed : Tcg Invalid Security State!!
            }
            break;
    }

    return st;
}
/****************************************************************************
 * Nvme_IOCmd_TcgChk():
 ****************************************************************************/
// Error_t Nvme_IOCmd_TcgChk(uint8_t IOCmd_Opc, NvmeIoRwCommand_t* pCdb_rw)
tcm_code bool Nvme_IOCmd_TcgChk(req_t *req)
{
    // Error_t error = cEcNoError;
    bool st = TRUE;   // OK
    struct nvme_cmd *cmd = req->host_cmd;
    U64 slba = (((U64)cmd->cdw11) << 32) | cmd->cdw10;
    U32 nlb = (cmd->cdw12 & 0xffff) + 1;

    //if(mTcgStatus&TCG_ACTIVATED)
    {
        switch (cmd->opc)
        {
        //case cRead:  break;    //deal later in NvmeRead for MBR-S reading
        case NVME_OPC_COMPARE:
        case NVME_OPC_VERIFY:
            if(mTcgStatus & MBR_SHADOW_MODE){
                st = FALSE;
            } else if (mReadLockedStatus){ // readlocked
                if(TcgRangeCheck(slba, nlb, FALSE)){ //!=TCG_DOMAIN_NORMAL
                    st = FALSE;
                }
            }
            break;

        case NVME_OPC_WRITE:
        case NVME_OPC_WRITE_UNCORRECTABLE:
        case NVME_OPC_WRITE_ZEROES:
        case NVME_OPC_FLUSH:
      #if (_TCG_!=TCG_OPAL)
        case NVME_OPC_DATASET_MANAGEMENT:
      #endif
            if (mWriteLockedStatus || (mTcgStatus&MBR_SHADOW_MODE)){
                if(TcgRangeCheck(slba, nlb + 1, TRUE)) { // return error when tcgRangeStatus != TCG_DOMAIN_NORMAL.
                    st = FALSE;
                }
            }
            break;

    #if _TCG_==TCG_OPAL  // OPAL only, TODO: eDrv
        case NVME_OPC_DATASET_MANAGEMENT:   // for Lenovo provide script (TCGMain_AutoBrief_PCIe.srt)
            if (mWriteLockedStatus || (mTcgStatus&MBR_SHADOW_MODE)){
                st = FALSE;
            }
            break;
    #endif
        }
    }

    return (st);
}

/****************************************************************************
 * host_Properties_Reset():  <--- HostPropertiesReset();
 ****************************************************************************/
//reset TPer's knowledge of Host Properties...
tcg_code void host_Properties_Reset(void)
{
    // DBG_P(1, 3, 0x820112);   //82 01 12, "[F]HostPropertiesReset"
    //reset TPer's knowledge of Host Properties...
    mHostProperties[0].val = HOST_MAX_COMPKT_SZ;
    mHostProperties[1].val = HOST_MAX_PKT_SZ;
    mHostProperties[2].val = HOST_MAX_INDTKN_SZ;
    mHostProperties[3].val = 1;
    mHostProperties[4].val = 1;
    mHostProperties[5].val = 1;
}


/****************************************************************************
 * tcgStackReset():  <--- TcgStackReset();
 ****************************************************************************/
tcg_code void tcgStackReset(req_t *req)
{
    // DBG_P(1, 3, 0x82010D);   //82 01 0D, "[F]TcgStackReset"
    //STACT_RESET processing
    //printk("StackReset():\n");
    DBG_P(0x01, 0x03, 0x700011 );  // StackReset():
    if(mSessionManager.state == SESSION_START)
        ResetSessionManager(req);

    host_Properties_Reset();

    mHandleComIDRequest = 0x02;
    mHandleComIDResponse = 0x00;

    gTcgCmdState = ST_AWAIT_IF_SEND;       //need to reply status...
}

/****************************************************************************
 * process_Pid02_GetComId_req():  <--- HandleComIDRequest();
 ****************************************************************************/
//deal with STACK_RESET only, Core 3.3.4.7.5
tcg_code U16 process_Pid02_GetComId_req(req_t *req, U8* buf)
{
    TCG_Pid02_GetComId_req_t *pbuf = (TCG_Pid02_GetComId_req_t *)buf;
    U8  errCode = 0;

    // Get data from host and store in ATACmdBuf
    // if(siloIdxCmd==0)   //normal IF-SEND
        // HostTx2TcgBuf(pHcmdQ, buf);

    // TCG_PRINTF("Stack_Reset Request: ");
    if(swap_u16(pbuf->comId) != BASE_COMID){
        errCode = 0xF0;   goto REQUEST_NG;
    }

    if(swap_u16(pbuf->extendedComId) != 0){
        errCode = 0xF1;   goto REQUEST_NG;
    }

    if(swap_u32(pbuf->requestCode) != 2){   // TODO: how to reponse if not STACK_RESET request?
        errCode = 0xF2;   goto REQUEST_NG;
    }

    tcgStackReset(req);

    // DBG_P(3, 3, 0x82010E, 1, 0x00, 1, errCode); //OK  //82 01 0E, "[F]HandleComIDRequest(Stack_Reset) : %X errCode = %X", 1 1
    return TRUE;

REQUEST_NG:
    mHandleComIDRequest  = 0x02;       //TODO: need to check...
    mHandleComIDResponse = 0x01;
    // errCode++;  errCode--;  // alexcheck
    //TCGPRN("Error!! StackReset errCode|%x\n", errCode);
    DBG_P(0x2, 0x03, 0x700000, 4, errCode);  // Error!! StackReset errCode|%x
    // DBG_P(3, 3, 0x82010E, 1, 0xFF, 1, errCode); //NG  //82 01 0E, "[F]HandleComIDRequest(Stack_Reset) : %X errCode = %X", 1 1
    return TRUE;
}


/****************************************************************************
 * prepare_Pid02_GetComId_resp():  <--- HandleComIDResponse();
 ****************************************************************************/
tcg_code void prepare_Pid02_GetComId_resp(req_t *req, U8* buf)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    TCG_Pid02_GetComId_Resp_t *pbuf = (TCG_Pid02_GetComId_Resp_t *)buf;
    // DBG_P(3, 3, 0x82010F, 1, mHandleComIDRequest, 1, mHandleComIDResponse); //82 01 0F, "[F]HandleComIDResponse Stack_Reset Request[%X] Response[%X]", 1 1

    //cj ToDO: return NG if mHandleComIDRequest==0? (no request, no response?) 2016/05/17
    memset(buf, 0, sizeof(TCG_Pid02_GetComId_Resp_t));
    pbuf->comId = swap_u16(cmd->com_id);
    pbuf->extendedComId = 0;

    if(mHandleComIDRequest == 2)
    {
        pbuf->requestCode        = swap_u32((U32)2);
        pbuf->availableDataLen   = swap_u16((U16)4);
        if(mHandleComIDResponse != 0x00)  pbuf->currentStatus = swap_u32((U32)1);   // Failure
    }
    else
    { //No Response Available
        pbuf->comId = swap_u16(cmd->com_id);
    }

    mHandleComIDRequest = 0;
}


/****************************************************************************
 * tcg_prepare_ComPacket():  <--- TcgCmdPkt4NoData();
 ****************************************************************************/
tcg_code void tcg_prepare_ComPacket(void)
{
    ComPacket_t *pbuf = (ComPacket_t *)dataBuf;

    memset(dataBuf, 0x00, DTAG_SZE);   //clear Tcg Recv Buffer
    pbuf->Reserved        = 0;
    pbuf->ComID           = swap_u16((U16)BASE_COMID);
    pbuf->ExtendedComID   = 0;
    pbuf->OutstandingData = 0;
    pbuf->MinTransfer     = 0;
    pbuf->Length          = 0;
}


/***********************************************************
 *TcgCmdPkt4Response(void):
 *
 *  Only prepare the Command Response Packet in Buffer, Payload and Status
 *  should be prepared in TcgStreamDecode
 *
 *  return 0 if no error.
 ***********************************************************/
tcg_code void tcg_prepare_respPacket(void)
{
    SDS_t *pbuf = (SDS_t *)dataBuf;

    tcg_prepare_ComPacket();   // TcgCmdPkt4NoData();
    pbuf->Packet.TSN       = swap_u32(mCmdPkt.mPktFmt.TSN);
    pbuf->Packet.HSN       = swap_u32(mCmdPkt.mPktFmt.HSN);
    pbuf->Packet.SeqNumber = swap_u32(mCmdPkt.mPktFmt.SeqNo);

    // iDataBuf = sizeof(SDS_t);
    iDataBuf = offsetof(SDS_t, DataPayLoad);
}


/***********************************************************
 *tcg_prepare_respPacket_update() --> TcgRespPktUpdate(void):
 *
 *  dataBuffer for IF-RECV
 *  1. Add the Method Status List
 *  2. Update the payload length
 *
 *  return 0 if no error.
 ***********************************************************/
tcg_code U16 tcg_prepare_respPacket_update(bool addStatus)
{
    SDS_t *pbuf = (SDS_t *)dataBuf;
    // U16 j;

    //add Method Status List
    if(addStatus == TRUE)
    { //No method status fo CloseSession
        //Status list
        fill_staus_token_list();
    }

    iDataBuf -= offsetof(SDS_t, DataPayLoad);
    pbuf->DataSubPacket.Length = swap_u32((U32)iDataBuf);

    iDataBuf = ((iDataBuf + sizeof(U32) - 1) / sizeof(U32)) * sizeof(U32); // multiple of 4
    iDataBuf += sizeof(DataSubPacket_t);
    pbuf->Packet.Length = swap_u32((U32)iDataBuf);

    iDataBuf += sizeof(Packet_t);
    pbuf->ComPacket.Length = swap_u32((U32)iDataBuf);

    // rcvCmdPktLen = iDataBuf + 20;
    rcvCmdPktLen = iDataBuf + sizeof(ComPacket_t);

    if(iDataBuf > mHostProperties[1].val)   //hostMaxPacketSize)
    {
        // DBG_P(3, 3, 0x820171, 2, rcvCmdPktLen, 4, mHostProperties[1].val);  //82 01 71, "pktSz > MaxPktSz %4X %X", 2 4
        return  STS_RESPONSE_OVERFLOW;
    }

    if(rcvCmdPktLen > mHostProperties[0].val)   //hostMaxComPacketSize)
    {
        // DBG_P(3, 3, 0x820172, 2, rcvCmdPktLen, 4, mHostProperties[0].val);  //82 01 72, "pktSz > MaxComPktSz %4X %X", 2 4
        return  STS_RESPONSE_OVERFLOW;
    }

    if((iDataBuf - (sizeof(Packet_t) + sizeof(DataSubPacket_t))) > mHostProperties[2].val)   //hostIndTokenSize)
    {
        // DBG_P(3, 3, 0x820173, 2, rcvCmdPktLen, 4, mHostProperties[2].val);  //82 01 73, "indTokenSz > MaxPktSz %4X %X", 2 4
        return  STS_SESSION_ABORT;  //STS_RESPONSE_OVERFLOW;
    }
    return STS_SUCCESS;
}


/****************************************************************************
 * f_SendCmd_Pid00():
 ****************************************************************************/
tper_status_t f_SendCmd_Pid00(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    //TCGPRN("f_SendCmd_Pid00() comid|%x tgtComid|%x\n", cmd->com_id, tgtComID);
    DBG_P(0x3, 0x03, 0x700001, 4, cmd->com_id, 4, tgtComID);  // f_SendCmd_Pid00() comid|%x tgtComid|%x
    if(gTcgCmdState == ST_AWAIT_IF_RECV)
    {
        if(cmd->len) return TPER_SYNC_PROTOCOL_VIOLATION;
    }

    if(cmd->len > MAX_COMPKT_SZ)   //add one sector for eDrive  //(MAX_COMPKT_SZ/512)))
    {
        if(siloIdxCmd)  //1667 protocol
        {
            if(cmd->len > (MAX_COMPKT_SZ+512)) return TPER_INVALID_TX_PARAM_SEND;
        }
        else   //TCpG2->0 non-eDriver
        {
            return  TPER_INVALID_TX_PARAM_SEND;
        }
    }

    return TPER_INVALID_SEC_PID_PARAM;
}

/****************************************************************************
 * f_SendCmd_Pid01():
 ****************************************************************************/
tper_status_t f_SendCmd_Pid01(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    //TCGPRN("f_SendCmd_Pid01() comid|%x tgtComid|%x\n", cmd->com_id, tgtComID);
    DBG_P(0x3, 0x03, 0x700002, 4, cmd->com_id, 4, tgtComID);  // f_SendCmd_Pid01() comid|%x tgtComid|%x
    if(gTcgCmdState == ST_AWAIT_IF_RECV)
    {
        if(cmd->len)
            return TPER_SYNC_PROTOCOL_VIOLATION;
    }

    if(cmd->len > MAX_COMPKT_SZ)   //add one sector for eDrive  //(MAX_COMPKT_SZ/512)))
    {
        if(siloIdxCmd)  //1667 protocol
        {
            if(cmd->len > (MAX_COMPKT_SZ+512))
                return TPER_INVALID_TX_PARAM_SEND;
        }
        else   //TCpG2->0 non-eDriver
        {
            return  TPER_INVALID_TX_PARAM_SEND;
        }
    }

    if(cmd->com_id == 0x0001)
    {   //L0_Discovery for IF-Send: do nothing
        // HostTx2TcgBuf(pHcmdQ, TcgHstTxRxBuf);
        tgtComID = 0x0001;
        //commonTP A1_4_1, gTcgCmdState = ST_AWAIT_IF_RECV;
        return TPER_GOOD;
    }

    if(cmd->com_id != BASE_COMID)
    {
        return TPER_OTHER_INVALID_CMD_PARAM;
    }

    tgtComID = BASE_COMID;
    return TPER_CONTINUE;
}

/****************************************************************************
 * f_SendCmd_Pid02():
 ****************************************************************************/
tper_status_t f_SendCmd_Pid02(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    //TCGPRN("f_SendCmd_Pid02() comid|%x tgtComid|%x xfer_buf|%x\n", cmd->com_id, tgtComID, (U32)xfer_buf);
    DBG_P(0x4, 0x03, 0x700003, 4, cmd->com_id, 4, tgtComID, 4, (U32)xfer_buf);  // f_SendCmd_Pid02() comid|%x tgtComid|%x xfer_buf|%x
    tper_status_t tperStatus = TPER_GOOD;

    if ((cmd->com_id == BASE_COMID) && (cmd->len))  //(cmd->len==1)??
    { //deal with STACK_RESET only, Core 3.3.4.7.5
        process_Pid02_GetComId_req(req, xfer_buf);
    }
    else if((cmd->com_id == 0x0004) && cmd->len)
    { //TPer reset, Opal2 3.2.3
        if (TcgTperReset(req) == FALSE)
            tperStatus = TPER_OTHER_INVALID_CMD_PARAM;
    }

    #if TCG_FS_BLOCK_SID_AUTH
    else if((cmd->com_id == 0x0005) && cmd->len)
    {
        if(TcgBlkSIDAuthentication(req) != STS_SUCCESS)
            tperStatus = TPER_OTHER_INVALID_CMD_PARAM;
    }
    #endif

    else
        tperStatus = TPER_OTHER_INVALID_CMD_PARAM;

    return tperStatus;
}

/****************************************************************************
 * f_RecvCmd_Pid00():
 ****************************************************************************/
tper_status_t f_RecvCmd_Pid00(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    //TCGPRN("f_RecvCmd_Pid00() comid|%x tgtComid|%x xfer_buf|%x\n", cmd->com_id, tgtComID, (U32)xfer_buf);
    DBG_P(0x4, 0x03, 0x700004, 4, cmd->com_id, 4, tgtComID, 4, (U32)xfer_buf);  // f_RecvCmd_Pid00() comid|%x tgtComid|%x xfer_buf|%x
    if (cmd->com_id == 0x0000)
    {   // Supported Protocols...
        Supported_Security_Protocol(xfer_buf);
        return TPER_GOOD;
    }
    else if (cmd->com_id == 0x0001)
    {   // Certificate Data... no data
        return TPER_GOOD;
    }
    return TPER_OTHER_INVALID_CMD_PARAM;
}

/****************************************************************************
 * f_RecvCmd_Pid01():
 ****************************************************************************/
tper_status_t f_RecvCmd_Pid01(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    //TCGPRN("f_RecvCmd_Pid01() comid|%x tgtComid|%x xfer_buf|%x\n", cmd->com_id, tgtComID, (U32)xfer_buf);
    DBG_P(0x4, 0x03, 0x700005, 4, cmd->com_id, 4, tgtComID, 4, (U32)xfer_buf);  // f_RecvCmd_Pid01() comid|%x tgtComid|%x xfer_buf|%x
    if(cmd->com_id == 0x0001)
    {
        Level0_Discovery(xfer_buf);

        if(tgtComID == 0x0001) // SEND-RECV pair
            gTcgCmdState = ST_AWAIT_IF_SEND;

        return TPER_GOOD;
    }
    else if(cmd->com_id != tgtComID)
    {
        return TPER_OTHER_INVALID_CMD_PARAM;
    }

    return TPER_CONTINUE;
    //TODO: if gTcgCmdState=ST_AWAIT_IF_SEND, return "no further data"
}

/****************************************************************************
 * f_RecvCmd_Pid02():
 ****************************************************************************/
tper_status_t f_RecvCmd_Pid02(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    //TCGPRN("f_RecvCmd_Pid02() comid|%x tgtComid|%x xfer_buf|%x\n", cmd->com_id, tgtComID, (U32)xfer_buf);
    DBG_P(0x4, 0x03, 0x700006, 4, cmd->com_id, 4, tgtComID, 4, (U32)xfer_buf);  // f_RecvCmd_Pid02() comid|%x tgtComid|%x xfer_buf|%x
    if ((cmd->com_id == BASE_COMID) && (cmd->len))  //(cmd->len==1)??
    {
        prepare_Pid02_GetComId_resp(req, xfer_buf);     //deal with STACK_RESET only, Core 3.3.4.7.5
        return TPER_GOOD;
    }
    // GetComID, not supported
    return TPER_OTHER_INVALID_CMD_PARAM;
}


//=============================================================================================
// TperProcessing() is splitted from original TcgCmdProcessing() due to 1667 requirement.
//
// It will handle TCG defined IF-SEND/IF-RECV commands, and return TCG command processing
// result according to the 1667 definition.
//
// For host payload from IF-SEND, if it is 1667 (0xEE) protocol, then it is transferred in
// SiloCmdProcessing() and the TCG ComPkt is decoded here with 0x20 byte offset (1667 header size).
//
// For TPer response data (for IF-RECV), if it is 1667 (0xEE) protocol, then it is encoded with
// 0x20 byte offset.
//==============================================================================================
// U16 tcg_tper_handle(req_t* req)
tcg_code tper_status_t tcg_tper_handle(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    tper_status_t tperStatus = TPER_GOOD;

    //TCGPRN("tcg_tper_handle()\n");
    DBG_P(0x01, 0x03, 0x700007 );  // tcg_tper_handle()
    if (cmd->protocol_id > 2)
        return TPER_OTHER_INVALID_CMD_PARAM;

    tperStatus = gCbfProc_Cmd_Pid_Map[cmd->DW9_0.OPC - IF_SEND][cmd->protocol_id](req);
    //TCGPRN("tcg_tper_handle() result|%x, gTcgCmdState|%x\n", tperStatus, gTcgCmdState);
    DBG_P(0x3, 0x03, 0x700008, 4, tperStatus, 4, gTcgCmdState);  // tcg_tper_handle() result|%x, gTcgCmdState|%x
    if (tperStatus != TPER_CONTINUE)
        return tperStatus;

    /* ProtocolID==1 for the following state processing : */
    switch(gTcgCmdState)
    {
        U16 result;
        case ST_AWAIT_IF_SEND:
            if(cmd->DW9_0.OPC == IF_SEND)
            {   // security send
                // Get data from host and store in xfer_buf
                gTcgCmdState = ST_PROCESSING;
            }
            else
            {   // security receive
                tcg_prepare_ComPacket();  // TcgCmdPkt4NoData();
                #if _TCG_ == TCG_EDRV
                if(siloIdxCmd) { //1667 format
                    rcvCmdPktLen = 56;
                    memcpy(xfer_buf + 0x20, (void*)dataBuf, TCG_BUF_LEN);    //sector count?
                }
                else
                #endif
                memcpy(xfer_buf, (void*)dataBuf, TCG_BUF_LEN);    //sector count?
                break;
            }

        case ST_PROCESSING:
            #if _TCG_ == TCG_EDRV
            if(siloIdxCmd){   //1667 format
                result = tcg_cmdPkt_extracter(req, xfer_buf + 0x20);
            }
            else
            #endif
            result = tcg_cmdPkt_extracter(req, xfer_buf);

            if(result == STS_SESSION_ABORT)
            {
                ResetSessionManager(req);
                tcg_cmdPkt_abortSession();
                #if _TCG_ == TCG_EDRV
                if(siloIdxCmd) //&&(ErrStatus1667!=STS_1667_SUCCESS))
                    tcg_cmdPkt_closeSession();   // TcgCmdPkt4CloseSession();
                #endif
                gTcgCmdState = ST_AWAIT_IF_RECV;
                break;
            }
            else if(result == STS_STAY_IN_IF_SEND)
            {
                gTcgCmdState = ST_AWAIT_IF_SEND;
                break;
            }

            //prepare CmdPacket in dataBuffer[] (for IF-RECV)
            tcg_prepare_respPacket(); // TcgCmdPkt4Response();

            if(result != STS_SUCCESS)
            {
                fill_no_data_token_list();
                //add status to reponse buffer and update length
                set_status_code(result, 0, 0);
                tcg_prepare_respPacket_update(TRUE);   // TcgRespPktUpdate();

                gTcgCmdState = ST_AWAIT_IF_RECV;
                break;
            }

            // Decode the payload, process the data, and prepare the response payload ...
            // ex: SessionManager, SP, Table processing ...
            keep_result = result = tcg_cmdPkt_payload_decoder(req);
            //printk("tcg_cmdPkt_payload_decoder keep_result=%08x\n", keep_result);
            DBG_P(0x2, 0x03, 0x700010, 4, keep_result);  // tcg_cmdPkt_payload_decoder keep_result=%08x

            if(req->completion == nvmet_core_cmd_done){
                if(result == STS_STAY_IN_IF_SEND)
                {
                    // DBG_P(1, 3, 0x820166);  //82 01 66, "!!NG: StreamDecode NG -> Stay in IF-SEND"
                    gTcgCmdState = ST_AWAIT_IF_SEND;
                    break;
                }

                if(result == STS_SESSION_ABORT)
                { //prepare payload for "Close Session"
                    // DBG_P(1, 3, 0x820167);  //82 01 67, "!!NG: StreamDecode NG -> Abort Session"
                    ResetSessionManager(req);
                    tcg_cmdPkt_abortSession();
                }
                else if(result == STS_RESPONSE_OVERFLOW)
                {
                    tcg_prepare_respPacket();  //TcgCmdPkt4Response();
                    fill_no_data_token_list();

                    //add status to reponse buffer and update length
                    set_status_code(result, 0, 0);
                    tcg_prepare_respPacket_update(TRUE);  // TcgRespPktUpdate();
                }

                gTcgCmdState = ST_AWAIT_IF_RECV;
            }
            break;

        case ST_AWAIT_IF_RECV:
            if(cmd->DW9_0.OPC == IF_RECV)
            { //if(IF-RECV is able to retrieve the entire response resulting from the IF-SEND ...
                if(cmd->len < rcvCmdPktLen)   //if xfer length is not enough...
                {
                    #if _TCG_ == TCG_EDRV
                    if(siloIdxCmd)
                    {  //1667 format
                        memcpy(xfer_buf + 0x20, (void*)dataBuf, 8);
                        fill_u16((U8 *)xfer_buf + 10 + 0x20, rcvCmdPktLen);    //OutstandingData
                        fill_u16((U8 *)xfer_buf + 14 + 0x20, rcvCmdPktLen);    //MinTransfer
                        rcvCmdPktLen = 0x38;
                    }
                    else
                    #endif
                    {
                        memcpy(xfer_buf, (void*)dataBuf, 8);
                        fill_u16((U8 *)xfer_buf + 10, rcvCmdPktLen);    //OutstandingData
                        fill_u16((U8 *)xfer_buf + 14, rcvCmdPktLen);    //MinTransfer
                    }
                    gTcgCmdState = ST_AWAIT_IF_RECV;
                    break;
                }

                //put response data to ATA buffer...
                #if _TCG_ == TCG_EDRV
                if(siloIdxCmd)  //1667 format
                    memcpy(xfer_buf + 0x20, (void*)&dataBuf, TCG_BUF_LEN);     //sector count?
                else
                #endif
                memcpy(xfer_buf, (void*)&dataBuf, TCG_BUF_LEN);    //sector count?

                gTcgCmdState = ST_AWAIT_IF_SEND;
            }
            else
            {   // IF_SEND
                // nothing to do....
            }
            break;

        default:
            break;
    }

    return TPER_GOOD; // zOK;
}


/****************************************************************************
 * tcg_cmd_handle():   <--  TcgCmdProcessing()
 *   Trusted Send/Receive command blocks are handled here.
 *   They are processed in 3 + 1 (power off) State.
 *   Check Core 3.3.10.4 for Command State Transition.
 *
 * return 0 if success
 *
 * TODO: error handling, reset handling, process for different ProtocolID ...
 ****************************************************************************/
extern U32 btn_cmd_idle(void);
tcg_code enum cmd_rslt_t tcg_cmd_handle(req_t *req)
{
    struct nvme_cmd *cmd = (struct nvme_cmd *)req->host_cmd;
    void *xfer_buf = req->req_prp.sec_xfer_buf;
    u32 xfer_buf_size = SECURE_COM_BUF_SZ;
    enum cmd_rslt_t res = HANDLE_RESULT_FAILURE;
	u8 wait_io_counter = 0;

    sys_assert(xfer_buf != NULL);
    nvme_tcg_trace(LOG_DEBUG, 0x80ec, "opcode(0x%x) pid(0x%x) comID(0x%x) len(0x%x) buf(0x%x)", cmd->opc, (nvme_tcg_cmd_dw10_t)(cmd->cdw10).protocol_id, (nvme_tcg_cmd_dw10_t)(cmd->cdw10).com_id, cmd->cdw11, (u32)xfer_buf);
	
    // check invalid cmd, length & PID
    if(((cmd->opc != IF_SEND) && (cmd->opc != IF_RECV)) ||
        (cmd->cdw11 == 0) || (cmd->cdw11 > xfer_buf_size) )
    {
        return HANDLE_RESULT_FAILURE;
    }

    // memset(xfer_buf, 0x00, sizeof(DTAG_SZE));
    tcg_ioCmd_inhibited = true;
    nvmet_io_fetch_ctrl(tcg_ioCmd_inhibited);  // disable IO new cmd in
    // --- wait exist io cmd finish ---
    while((!btn_wr_cmd_idle()) || (!btn_rd_cmd_idle())){
		mdelay(100);
		if (wait_io_counter++ > 10)
			break;
    }

    #if _TCG_ == TCG_EDRV
    if (((nvme_tcg_cmd_dw10_t)cmd->cdw10).protocol_id == TCG_SSC_EDRIVE) {
        silo_cmd_handle(req);
    }
    else
    #endif
    {
        if (mSessionManager.sessionStartTime)
        {
            // get current time
            u32 elapsed = time_elapsed_in_ms(mSessionManager.sessionStartTime);
            nvme_tcg_trace(LOG_INFO, 0x9247, "Session start time: 0x%x, elapsed time: %d ms", mSessionManager.sessionStartTime, elapsed);
			
            if (elapsed > mSessionManager.sessionTimeout)
            {
                nvme_tcg_trace(LOG_INFO, 0xef1d, "Error!! Session Timeout [0x%x]", mSessionManager.sessionTimeout);
                ResetSessionManager(req);
            }
        }

        tcg_tper_status = tcg_tper_handle(req);
        if(tcg_tper_status != TPER_GOOD) {
            switch(tcg_tper_status) {
                case TPER_SYNC_PROTOCOL_VIOLATION:
                    res = HANDLE_RESULT_FAILURE;
            		break;

                //case TPER_INVALID_SEC_PID_PARAM:
                //case TPER_OTHER_INVALID_CMD_PARAM:
                //case TPER_INVALID_TX_PARAM_SEND:
                default:
                    res = HANDLE_RESULT_FAILURE;
            		break;

                //case TPER_DATA_PROTECTION_ERROR:
                //case TPER_INVALID_SEC_STATE:
                //case TPER_OPERATION_DENIED:
            }
        }else{   // TPER_GOOD
            if(cmd->opc == IF_RECV){
                res = HANDLE_RESULT_DATA_XFER;

                if (mSessionManager.bWaitSessionStart)
                {
                	// start counter
                    mSessionManager.bWaitSessionStart = 0;
                    mSessionManager.sessionStartTime = get_tsc_64();
					
                    nvme_tcg_trace(LOG_INFO, 0xd3a3, "Session start time: 0x%x", mSessionManager.sessionStartTime);
                }
            }else if(cmd->opc == IF_SEND){
                if(req->completion == nvmet_core_cmd_done){
                    res = HANDLE_RESULT_FINISHED;
                }else{
                    res = HANDLE_RESULT_PENDING_BE;
                }
            }
        }
    }
    return res;
}


/*****************************************************************************
 * = EDRV = = EDRV = = EDRV = = EDRV == EDRV == EDRV == EDRV == EDRV == EDRV =
 *****************************************************************************/

#if _TCG_ == TCG_EDRV
/****************************************************************************
 * ProbeSilo_Probe():
 ****************************************************************************/
tcg_code void ProbeSilo_Probe(U8* buf, U32 len)
{
    IEEE1667_ProbeSiloProbe_Spin_t *pbuf1     = (IEEE1667_ProbeSiloProbe_Spin_t *)buf;
    IEEE1667_ProbeSiloProbe_SiloList_t *pbuf2 = \
        (IEEE1667_ProbeSiloProbe_SiloList_t *)(buf + offsetof(IEEE1667_ProbeSiloProbe_Spin_t, SiloList));
    U32 silo_List_cnt = 0;

    memset(buf, 0x00, len);             //clear send buffer
    pbuf1->StatusCode = STS_1667_SUCCESS;

    silo_List_cnt++;
    pbuf2->STID = swap_u32(0x00000100);
    pbuf2->SiloTypeSpecMajVer = 0x01;
    pbuf2->SiloTypeSpecMinVer = 0x00;
    pbuf2->SiloTypeImplMajVer = 0x01;
    pbuf2->SiloTypeImplMinVer = 0x00;

    silo_List_cnt++;
    pbuf2 += sizeof(IEEE1667_ProbeSiloProbe_SiloList_t);
    pbuf2->STID = swap_u32(0x00000104);
    pbuf2->SiloTypeSpecMajVer = 0x01;
    pbuf2->SiloTypeSpecMinVer = 0x00;
    pbuf2->SiloTypeImplMajVer = 0x01;
    pbuf2->SiloTypeImplMinVer = 0x00;

    pbuf2 += sizeof(IEEE1667_ProbeSiloProbe_SiloList_t);
    pbuf1->SiloListLength = swap_u16(silo_List_cnt * sizeof(IEEE1667_ProbeSiloProbe_SiloList_t));
    pbuf1->PaylaodLength = swap_u32((U32)((U32)pbuf2 - (U32)pbuf1));
    b1667Probed = 1;
}

/****************************************************************************
 * f_TcgSilo_GetSiloCap():
 ****************************************************************************/
tcg_code void f_TcgSilo_GetSiloCap(req_t *req)
{
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    IEEE1667_GetSiloCapabilities_Spin_t *pbuf = (IEEE1667_GetSiloCapabilities_Spin_t *)xfer_buf;
    U16 len16;

    // DBG_P(1, 3, 0x820119);   //82 01 19, "[F]TcgSilo_GetSiloCap"
    memset(xfer_buf, 0x00, DTAG_SZE);   //clear send buffer
    if(mPCLength != 8)
    {
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode    = STS_1667_INCONSISTENT_PCLENGTH;
        return;
    }

    SiloComID = BASE_COMID;
    pbuf->StatusCode = STS_1667_SUCCESS;

    pbuf->ComId = swap_u16((U16)BASE_COMID);
    pbuf->MaximumPoutTransferSize = swap_u32((U32)MAX_COMPKT_SZ);

    len16 = Level0_Discovery((U8 *)pbuf + offsetof(IEEE1667_GetSiloCapabilities_Spin_t, TcgL0DiscoveryData));
    pbuf->TcgL0DiscoveryDataLength = swap_u32((U32)len16);

    len16 += (sizeof(IEEE1667_GetSiloCapabilities_Spin_t) - 1);

    if (len16 > mPInLength) pbuf->AvailablePayloadLength = swap_u32((U32)len16);

    pbuf->PaylaodLength = swap_u32((U32)len16);
    return ;
}

/****************************************************************************
 * f_TcgSilo_Transfer():
 ****************************************************************************/
U8 TperStatus_To_SiloStatusCode(tper_status_t tperStatus)
{
    U8 statusCode;

    if (tperStatus==TPER_GOOD)
        statusCode = STS_1667_SUCCESS;
    else if (tperStatus==TPER_INVALID_TX_PARAM_SEND)
        statusCode = STS_1667_INV_TX_LEN_ON_POUT;
    else if (tperStatus==TPER_SYNC_PROTOCOL_VIOLATION)
        statusCode = STS_1667_TCG_SYNC_VIOLATION;
    else // if (tperStatus==TPER_INVALID_SEC_PID_PARAM) || (tperStatus==TPER_OTHER_INVALID_CMD_PARAM) || others
        statusCode = STS_1667_INV_TCG_COMID;

    return statusCode;
}

tcg_code void f_TcgSilo_Transfer(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    IEEE1667_TransferSilo_Spin_t *pbuf1 = (IEEE1667_TransferSilo_Spin_t *)xfer_buf;
    ComPacket_t *pbuf2 = (ComPacket_t *)(pbuf1 + offsetof(IEEE1667_TransferSilo_Spin_t, TCGComPacket));
    U8  errorCode  = 0;
    U16 chkItem    = 0;
    U32 tmp1, tmp2 = 0;

    // DBG_P(1, 3, 0x82011A);   //82 01 1A, "[F]TcgSilo_Transfer"
    // TODO: check A.X.5.4 item 1,2,3,4 first...
    // 1. contain a Complet ComPacket... check outstanding data??
    //    buf[7]= STS_1667_INCOMPLETE_COMMAND;

    //2. ComID check, should be the same as the ComID @ ATA register
    chkItem = swap_u16(pbuf2->ComID);
    if (chkItem != SiloComID)
    {
        errorCode = STS_1667_INV_TCG_COMID;
        // DBG_P(2, 3, 0x820156, 2, chkItem);   //82 01 56, "!!NG Invalid ComID: %X", 2
        goto ERROR_STATUS;
    }
    // ComID Ext = 0
    chkItem = swap_u16(pbuf2->ExtendedComID);
    if(chkItem) //!=0
    {
        errorCode=STS_1667_INV_TCG_COMID;
        // DBG_P(2, 3, 0x820157, 2, chkItem);   //82 01 57, "!!NG ComIDExt!=0 %X", 2
        goto ERROR_STATUS;
    }

    //3. Length check
    tmp1 = swap_u32(pbuf1->TCGComPacketLength); // Length of TCGComPacket:
    tmp2 = swap_u32(pbuf2->Length);             // ComPkt.length:

    if((tmp2 + offsetof(IEEE1667_TransferSilo_Spin_t, TCGComPacket)) > mPOutLength)
    {
        //ComPacket Length > Xfer data length
        errorCode = STS_1667_INCOMPLETE_COMMAND;
        // DBG_P(3, 3, 0x820158, 4, tmp2, 4, tmp1);   //82 01 58, "!!NG ComPkt.len (%X) > mPOutLen (%X)", 4 4
        goto ERROR_STATUS;
    }
    else if(tmp2 > MAX_COMPKT_SZ)
    {
        errorCode = STS_1667_INV_TX_LEN_ON_POUT;
        // DBG_P(2, 3, 0x820159, 4, tmp2);   //82 01 59, "!!NG ComPkt.len (%X)", 4
        goto ERROR_STATUS;
    }

    if((mPCLength != (tmp1 + offsetof(IEEE1667_TransferSilo_Spin_t, TCGComPacket))) ||
       (tmp1 != (tmp2 + sizeof(ComPacket_t))))
    {
        // if(mPCLengh>mPOutLength) ??
        // errorCode=STS_1667_INV_PARAMETER;
        // goto ERROR_STATUS;
    }

    // DBG_P(4, 3, 0x82015A, 4, mPCLength, 4, tmp1, 4, tmp2);  //82 01 5A, "PCLen: %X LenOfTcgComPkt: %X ComPkt.len: %X", 4 4 4

    //4. P_IN Transfer Length check
    if (mPInLength < (sizeof(IEEE1667_TransferSilo_Spin_t) - 1 + sizeof(ComPacket_t))) //if ((mAtaCmd.length > (MAX_COMPKT_SZ + 0x20)))
    {
        errorCode=STS_1667_INV_PARAMETER_LENGTH;
        // DBG_P(1, 3, 0x82015B);   //82 01 5B, "!!NG P_IN TxLen < 52"
        goto ERROR_STATUS;
    }

    //5. IF-SEND
    cmd->DW9_0.OPC      = IF_SEND;
    cmd->protocol_id    = c_TCG_PID_01;
    cmd->com_id         = BASE_COMID;
    cmd->len            = mPOutLength;

    errorCode = TperStatus_To_SiloStatusCode(tcg_tper_handle(req));
    if(errorCode != STS_1667_SUCCESS)
    {
        //6. TPer Error
        if((errorCode != STS_1667_TCG_SYNC_VIOLATION) && (errorCode != STS_1667_INV_TX_LEN_ON_POUT))
        {
            errorCode = STS_1667_INV_TCG_COMID;
        }
        // DBG_P(2, 3, 0x82015C, 1, errorCode);   //82 01 5C, "!!NG Send: %X", 1
        goto ERROR_STATUS;
    }

    //7. IF-RECV
    cmd->DW9_0.OPC      = IF_RECV;
    cmd->protocol_id    = c_TCG_PID_01;
    cmd->com_id         = BASE_COMID;
    cmd->len            = mPInLength;

    errorCode = TperStatus_To_SiloStatusCode(tcg_tper_handle(req));     //cj:  "mAtaCmd.length -= 32; " ??
    if(errorCode!=STS_1667_SUCCESS)
    {
        //8. TPer Error
        if((errorCode != STS_1667_TCG_SYNC_VIOLATION) && (errorCode != STS_1667_INV_TX_LEN_ON_POUT))
        {
            errorCode=STS_1667_INV_TCG_COMID;
        }
        // DBG_P(2, 3, 0x82015D, 1, errorCode);   //82 01 5D, "!!NG Recv: %X", 1
        goto ERROR_STATUS;
    }
    else
    { //9. successfully return RespComPkt
        U32 pcLen;

        pcLen = rcvCmdPktLen + offsetof(IEEE1667_TransferSilo_Spin_t, TCGComPacket);
        if(pcLen > cmd->len)
        { //c. packet size is larger than host request...
            pbuf1->PaylaodLength = swap_u32(cmd->len);
            // set Available Payload Length to data bytes available
            pbuf1->AvailablePayloadLength = swap_u32(pcLen);
        }
        else
        { //d.
            pbuf1->PaylaodLength = swap_u32(pcLen);
            pbuf1->AvailablePayloadLength = 0;
        }

        pbuf1->TCGComPacketLength = swap_u32((U32)rcvCmdPktLen);
        pbuf1->StatusCode = STS_1667_SUCCESS;
    }
    return;

ERROR_STATUS:
    memset(xfer_buf, 0x00, sizeof(IEEE1667_TransferSilo_Spin_t));   //clear send buffer
    // set Payload Content Length
    if (mPInLength < 8) pbuf1->PaylaodLength = swap_u32(mPInLength);
    else                pbuf1->PaylaodLength = swap_u32((U32)8);

    pbuf1->StatusCode = errorCode;
    return;
}

/****************************************************************************
 * f_TcgSilo_Reset():
 ****************************************************************************/
tcg_code void f_TcgSilo_Reset(req_t *req)
{
    void *xfer_buf              = req->req_prp.sec_xfer_buf;
    IEEE1667_Reset_Spin_t *pbuf = (IEEE1667_Reset_Spin_t *) xfer_buf;
    // DBG_P(1, 3, 0x82011B);   //82 01 1B, "[F]TcgSilo_Reset"
    memset(xfer_buf, 0x00, sizeof(IEEE1667_Reset_Spin_t));   //clear send buffer

    if(mPCLength != sizeof(IEEE1667_Reset_Spout_t))
    {
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode = STS_1667_INCONSISTENT_PCLENGTH;
    }
    else
    {
        tcgStackReset(req);
        mHandleComIDRequest = 0;
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode = STS_1667_SUCCESS;
    }

    return ;
}

/****************************************************************************
 * f_TcgSilo_GetResult():
 ****************************************************************************/
tcg_code void f_TcgSilo_GetResult(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    IEEE1667_GetResult_Spin_t *pbuf = (IEEE1667_GetResult_Spin_t *) xfer_buf;

    // DBG_P(1, 3, 0x82011D);   //82 01 1D, "[F]TcgSilo_GetResult"
    if(mPCLength != sizeof(IEEE1667_GetResult_Spout_t))
    {
        memset(xfer_buf, 0x00, sizeof(IEEE1667_GetResult_Spin_t));   //clear send buffer
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode = STS_1667_INCONSISTENT_PCLENGTH;
        return;
    }

    if(mPInLength <= (offsetof(IEEE1667_GetResult_Spin_t, TCGComPacket) + sizeof(ComPacket_t)))  //i.e., length=0
    {
        memset(xfer_buf, 0x00, sizeof(IEEE1667_GetResult_Spin_t));   //clear send buffer
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode = STS_1667_INV_PARAMETER_LENGTH;
        return;
    }

    cmd->DW9_0.OPC      = IF_RECV;
    cmd->protocol_id    = c_TCG_PID_01;
    cmd->com_id         = SiloComID;
    cmd->len            = mPInLength;

    U8 errCode = TperStatus_To_SiloStatusCode(tcg_tper_handle(req));
    if(errCode != STS_1667_SUCCESS)
    {
        // return 8 byte status header
        if((errCode != STS_1667_TCG_SYNC_VIOLATION) && (errCode != STS_1667_INV_TX_LEN_ON_POUT))
        {
            errCode = STS_1667_INV_TCG_COMID;
        }
        memset(xfer_buf, 0x00, sizeof(IEEE1667_GetResult_Spin_t));   //clear send buffer
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode = errCode;
    }
    else
    {
        // successfully return RespComPkt
        U32 pcLen;

        pcLen = rcvCmdPktLen + offsetof(IEEE1667_GetResult_Spin_t, TCGComPacket);
        if(pcLen > cmd->len)
        { //packet size is larger than host request...
            pbuf->PaylaodLength = swap_u32((U32)cmd->len);
            pbuf->AvailablePayloadLength = swap_u32((U32)pcLen);
        }
        else
        {
            pbuf->PaylaodLength = swap_u32((U32)pcLen);
            pbuf->AvailablePayloadLength = 0;
        }

        pbuf->TCGComPacketLength = swap_u32((U32)rcvCmdPktLen);
        pbuf->StatusCode = STS_1667_SUCCESS;
    }

    return ;
}

/****************************************************************************
 * f_TcgSilo_TPerReset():
 ****************************************************************************/
tcg_code void f_TcgSilo_TPerReset(req_t *req)
{
    void *xfer_buf                  = req->req_prp.sec_xfer_buf;
    IEEE1667_TperReset_Spin_t *pbuf = (IEEE1667_TperReset_Spin_t *)xfer_buf;
    // DBG_P(1, 3, 0x82011C);   //82 01 1C, "[F]TcgSilo_TPerReset"
    memset(xfer_buf, 0x00, sizeof(IEEE1667_TperReset_Spin_t));   //clear send buffer

    if(mPCLength != 8)
    {
        pbuf->PaylaodLength = swap_u32((U32)8);
        pbuf->StatusCode    = STS_1667_INCONSISTENT_PCLENGTH;
    }
    else
    {
        fill_u16((U8 *)xfer_buf, (U16)BASE_COMID);
        fill_u16((U8 *)xfer_buf + 2, (U16)8);
        if(TcgTperReset(req) == FALSE) pbuf->StatusCode = STS_1667_FAILURE;
        else                        pbuf->StatusCode = STS_1667_SUCCESS;
    }
    return ;
}

/****************************************************************************
 * silo_cmd_handle() : SiloCmdProcessing() will deal with 1667 commands (protocol 0xEE)
 *
 * It supports:
 * (1) Probe Silo with ProbeCmd, and
 * (2) TCG Silo with (a) GetSiloCap cmd, (b) Transfer cmd, (c) Reset cmd, and (4) GetTransferResult cmd.
 ****************************************************************************/
tcg_code void silo_cmd_handle(req_t *req)
{
    NvmeTcgCmd_t *cmd = (NvmeTcgCmd_t *)req->host_cmd;
    void *xfer_buf    = req->req_prp.sec_xfer_buf;
    U8 index;

    //P-OUT (IF-SEND)
    if (cmd->DW9_0.OPC == IF_SEND)
    {
        //if(mAtaCmd.length>(MAX_COMPKT_SZ/512))
            //P_LENGTH > max buffer size
            // 1. receive and discard, 2. status code
            //m1667Status=STS_1667_INV_TX_LEN_ON_POUT;
        //else
        // {
            siloIdxCmd = cmd->com_id;
            mPOutLength = cmd->len;

            // if (mPOutLength < sizeof(TcgHstTxRxBuf))
            // {
                // HostTx2TcgBuf(pHcmdQ, TcgHstTxRxBuf);
            // }
        // }
        // DBG_P(4, 3, 0x820153, 1, mAtaCmd.cmd, 2, mAtaCmd.length, 2, siloIdxCmd);  //82 01 53, ">> P_OUT Cmd[%X] BC[%X] siloIdxCmd[%X]", 1 2 2
        return;
    }

    //processing IF-RECV (P_IN) ...
    index = (U8)(cmd->com_id >> 8);
    m1667Status = STS_1667_SUCCESS;
    mPInLength = cmd->len;

    // DBG_P(4, 3, 0x820154, 1, cmd->DW9_0.OPC, 2, cmd->len, 2, siloIdxCmd);  //82 01 54, "<< P_IN Cmd[%X] BC[%X] siloIdxCmd[%X]", 1 2 2

    if (index == 0x00)
    { // Probe Silo
        if(siloIdxCmd != cmd->com_id)                   m1667Status = STS_1667_SP_SEQUENCE_REJECTION;
        else if(siloIdxCmd != TCG_1667_ProbeCmd)        m1667Status = STS_1667_RESERVED_FUNCTION;
        else if(mPOutLength > (MAX_COMPKT_SZ + 0x200))  m1667Status = STS_1667_INV_TX_LEN_ON_POUT;
        else
        {
            mPCLength = swap_u32(((IEEE1667_ProbeSiloProbe_Spout_t *)xfer_buf)->PaylaodLength);
            if(mPOutLength < mPCLength) m1667Status = STS_1667_INCOMPLETE_COMMAND;
        }

        if(m1667Status == STS_1667_SUCCESS)
        {
            ProbeSilo_Probe(xfer_buf, mPInLength);    //Probe cmd processing
            return;
        }
    }
    else if(index == 0x01)
    { // TCG Silo
        //check if ProbeCmd is run first...
        //mPLength = mAtaCmd.length;

        if(b1667Probed == 0)
            m1667Status = STS_1667_NO_PROBE;
        else if(siloIdxCmd != cmd->com_id)  // 2
            m1667Status = STS_1667_SP_SEQUENCE_REJECTION;
        else if((siloIdxCmd < TCG_1667_GetSiloCap) || (siloIdxCmd > TCG_1667_TPerReset))
            m1667Status = STS_1667_RESERVED_FUNCTION;    // 3
        else if (mPOutLength > (MAX_COMPKT_SZ + 0x200))  // 4, MAX_COMPKT_SZ + 0x200 for ehdd
            m1667Status = STS_1667_INV_TX_LEN_ON_POUT;
        else // 5
        {
            mPCLength = swap_u32(((IEEE1667_ProbeSiloProbe_Spout_t *)xfer_buf)->PaylaodLength);
            if(mPOutLength < mPCLength) m1667Status = STS_1667_INCOMPLETE_COMMAND;
        }

        if(m1667Status == STS_1667_SUCCESS)
        {
            if((siloIdxCmd >= TCG_1667_GetSiloCap) && ((siloIdxCmd - TCG_1667_GetSiloCap) < cMcTcgSilo_Last)){
                gCbfProc_TcgSilo_Map[siloIdxCmd - TCG_1667_GetSiloCap](req);
                return;
            }
        }
    }
    else
    { // Error Silo
        m1667Status = STS_1667_INV_SILO;
    }

    //return error status
    if(m1667Status != STS_1667_SUCCESS)
    {
        IEEE1667_ProbeSiloProbe_Spin_t *pbuf = (IEEE1667_ProbeSiloProbe_Spin_t *)xfer_buf;

        // DBG_P(2, 3, 0x820155, 1, m1667Status);  //82 01 55, "!!P_IN NG: %X", 1
        memset(xfer_buf, 0x00, DTAG_SZE);       //clear send buffer

        // set Payload Content Length
        if(mPInLength < 8) pbuf->PaylaodLength = swap_u32(mPInLength);
        else               pbuf->PaylaodLength = swap_u32((U32)8);

        pbuf->StatusCode = m1667Status;
    }

    return;
}

#endif   // _TCG_ == TCG_EDRV

//=============================================================================
// tcg_if_ps4_init_agent():
//=============================================================================
tcg_code int tcg_if_ps4_init_agent(void)
{
    tcg_req = nvmet_get_req();
    if(tcg_req == NULL){
        printk("Error!! tcg_if_ps4_init_agent() tcg_req = NULL\n");
        DBG_P(0x01, 0x03, 0x7F7F49 );  // Error!! tcg_if_ps4_init_agent() tcg_req = NULL
        return zNG;
    }
    INIT_LIST_HEAD(&tcg_req->entry);
    INIT_LIST_HEAD(&tcg_req->inentry);
    tcg_ipc_post(tcg_req, MSG_TCG_NF_CPU_INIT, NULL);
    return zOK;
}
//=============================================================================

tcg_code bool cb_init_cache_done(req_t *req)
{
    //TCGPRN("cb_init_cache_done()\n");
    DBG_P(0x01, 0x03, 0x70000A );  // cb_init_cache_done()
    req->completion = NULL;
    nvmet_put_req(req);
    return TRUE;
}


tcg_code bool cb_init_cache(req_t *req)
{
    // return TcgFuncRequest1(MSG_TCG_INIT_CACHE);
    //TCGPRN("cb_init_cache()\n");
    DBG_P(0x01, 0x03, 0x70000B );  // cb_init_cache()
    if(smSysInfo->d.FtlData.d.zeroTag != SI_FTLDATA_TAG_ZERO){
        tcg_ipc_post(req, MSG_TCG_INIT_CACHE, cb_init_cache_done);
    }else{
        cb_init_cache_done(req);
    }

    return TRUE;
}

/****************************************************************************
 * tcg_if_onetime_init():  <--- TcgInit_Cpu0();
 ****************************************************************************/
tcg_code int tcg_if_onetime_init(void)
{
    U8 j=0;
    int st = zOK;

    //if (bootMode == cInitBootDeepPowerDown)
    //{
    //    pmu_restore(PM_CPU1_TCG_IDX);
    //}
    tcg_req = nvmet_get_req();

    //TCGPRN("tcg_if_onetime_init() req|%x, bootMode|%x, bTcgTblErr|%x\n", tcg_req, bootMode, bTcgTblErr);
    DBG_P(0x4, 0x03, 0x70000C, 4, tcg_req, 4, bootMode, 4, bTcgTblErr);  // tcg_if_onetime_init() req|%x, bootMode|%x, bTcgTblErr|%x
    if(tcg_req == NULL) return zNG;
    INIT_LIST_HEAD(&tcg_req->entry);
    INIT_LIST_HEAD(&tcg_req->inentry);

    // DBG_P(1, 3, 0x82010A);   //82 01 0A, "[F]TcgInit_CPU0"

    tcg_if_post_sync_response();
    // tcg_if_post_sync_sign();
    // following 4 function should be performed first.
    SMBR_ioCmdReq = FALSE;
    tcg_ioCmd_inhibited = FALSE;

    gTcgCmdState = ST_AWAIT_IF_SEND;
    mSessionManager.SPSessionID = BASE_SPSESSION_ID-1;    //0x1001
    mSessionManager.TransactionState = TRNSCTN_IDLE;
    ClearMtableChangedFlag();
    ResetSessionManager(tcg_req);

    //Tcg_RdWrProcessing = FALSE;
    bTcgKekUpdate       = FALSE;
    mHandleComIDRequest = 0x00;

#if _TCG_ == TCG_EDRV
    //b1667Probed = 0x00;
    siloIdxCmd = 0x00;
    SiloComID = BASE_COMID; //0x00;     //eDrive won't call GetSiloCap to get ComID @ Power-On

    bEHddLogoTest = FALSE; //for WHQL test
    mPsidRevertCnt = 0;
#endif

    if (bTcgTblErr)
    {
        mTcgStatus = TCG_TBL_ERR;
    }
    else
    {
        //if(ChkDefaultTblPattern()==zOK) //todo: already done in CPU4
        {
            mTcgStatus = 0;

            //reset CPin tries count
            for(j = 0; j < pG1->b.mAdmCPin_Tbl.hdr.rowCnt; j++)
                pG1->b.mAdmCPin_Tbl.val[j].tries = 0;

            for(j = 0; j < pG3->b.mLckCPin_Tbl.hdr.rowCnt; j++)
                pG3->b.mLckCPin_Tbl.val[j].tries = 0;

            if(pG1->b.mAdmSP_Tbl.val[SP_LOCKING_IDX].lifeCycle == manufactured)
            {
                 mTcgStatus |= TCG_ACTIVATED;
            }

            // if (initMode == cInitBootFwUpdated)
            // {
                // TcgRestoreVarFromEEP();
            // }
            #ifdef BCM_test
            DumpTcgKeyInfo();
            #endif

            #if 0
            if (bootMode == cInitBootFwUpdated)
            {
                tcg_major_info_recover();
            }
            else
            {
                CPinTbl_Reset();

                //Locking Table Initialization
                MbrCtrlTbl_Reset(PowerCycle);       // pG3->b.mLckMbrCtrl_Tbl.val[0].done should be zero after power on reset. (POWER CYCLE)
                LockingTbl_Reset(PowerCycle);       // LckLocking_Tbl "PowerOnReset"
            }
            #else
            if((bootMode != cInitBootDeepPowerDown) && (warn_boot_restore_done == FALSE)){
                CPinTbl_Reset();

                //Locking Table Initialization
                MbrCtrlTbl_Reset(PowerCycle);       // pG3->b.mLckMbrCtrl_Tbl.val[0].done should be zero after power on reset. (POWER CYCLE)
                LockingTbl_Reset(PowerCycle);       // LckLocking_Tbl "PowerOnReset"
            }else{
                warn_boot_restore_done = FALSE;
                printk("[X]LckOnRst bootMode[%08x] warn_boot_restore_done[%08x]\n", bootMode, warn_boot_restore_done);
                DBG_P(0x3, 0x03, 0x700016, 4, bootMode, 1, warn_boot_restore_done);  // o set LockOnReset %08x
                tcg_restore_lockOnReset_params();
            }
            #endif

            #ifdef BCM_test
            //printk("1111");
            DBG_P(0x01, 0x03, 0x70000D );  // 1111
            DumpTcgKeyInfo();
            #endif

            SingleUser_Update();
            DataStoreAddr_Update();
            LockingRangeTable_Update();         // Update ranges, keys, and Read/Write LockedTable for Media Read/Write control

            #if (BUILD_SSD_CUSTOMER == SSD_CUSTOMER_DELL)
                tcg_disable_mbrshadow();
            #endif
        }
        /* else
        {
            mTcgStatus = TCG_TBL_ERR;
            bTcgTblErr = TRUE;
        } */
    }
    //TCGPRN("mTcgStatus|%x bTcgTblErr|%x\n", mTcgStatus, bTcgTblErr);
    DBG_P(0x3, 0x03, 0x70000E, 4, mTcgStatus, 4, bTcgTblErr);  // mTcgStatus|%x bTcgTblErr|%x

#if CO_SUPPORT_AES
    if(mTcgStatus&TCG_TBL_ERR)
        HAL_SEC_AesDummyReadMode();
#endif

    ClearMtableChangedFlag();
    bLockingRangeChanged = FALSE;

#ifdef _TCG_RESET_PSID
    {   //Copy PBKDF(MSID) to PSID
        Tcg_GenCPinHash(G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin.cPin_val, CPIN_MSID_LEN, &G1.b.mAdmCPin_Tbl.val[CPIN_PSID_IDX].cPin);

        //if(RW_WaitG1Wr()==zOK)
        if(TcgFuncRequest1(MSG_TCG_G1WR) == zOK)
            // DBG_P(1, 3, 0x820140);  //82 01 40, "reset PSID OK"
        else
            // DBG_P(1, 3, 0x820141);  //82 01 41, "reset PSID NG"
    }
#endif
    //TCGPRN("tcg_req|%x EE_Zerotag|%x\n", tcg_req, smSysInfo->d.FtlData.d.zeroTag);
    DBG_P(0x3, 0x03, 0x70000F, 4, tcg_req, 4, smSysInfo->d.FtlData.d.zeroTag);  // tcg_req|%x EE_Zerotag|%x
    #if 1
    cb_init_cache(tcg_req);
    #else
    if((st == zOK) && (smSysInfo->d.FtlData.d.zeroTag != SI_FTLDATA_TAG_ZERO)){
        st = init_cache(req);
    }
    nvmet_put_req(req);
    #endif
    return st;
}
#endif //Jack Li
