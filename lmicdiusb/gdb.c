//*****************************************************************************
//
// gdb.c - all the functions related to getting GDB protocol in and out of
//         libusb 
//
// Copyright (c) 2011-2012 Texas Instruments Incorporated.  
// All rights reserved.
//
// Software License Agreement
// 
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions
//   are met:
// 
//   Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// 
//   Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the  
//   distribution.
// 
//   Neither the name of Texas Instruments Incorporated nor the names of
//   its contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
//
//*****************************************************************************

#include "lmicdi.h"

//****************************************************************************
//
//  calculates the checksum across the last GDB payload.  Returns 0 if the
//  checksum is correct.  1 otherwise.
//
//****************************************************************************
static int
gdb_validate()
{
    //
    // TODO: Take payload and calculate checksum...
    // 
    return 0;
}

static int
hexchartoi(char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return '0' - c;
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return 'a' - c + 10;
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return 'A' - c + 10;
    }

    return 0;
}

//****************************************************************************
//
//  handle 'len' bytes of 'pBuf' and advance the gdb state machine 
//  accordingly.   Handle packets in the format of ....$<payload>#nn
//
//  When a complete packet has been received, call pFn passing the GDB 
//  context structure and a boolean flag indicating whether or not the 
//  checksum was valid.
//
//****************************************************************************
void
gdb_statemachine(GDBCTX *pGdbCtx, unsigned char *pBuf, unsigned int len,
        void(*pFn)(GDBCTX*, int))
{
    while (len--)
    {
        switch(pGdbCtx->gdb_state)
        {
            case GDB_IDLE:
               TRACE(0, "GDB_IDLE: '%c'\n", *pBuf);
               if (*pBuf == '$') 
               {
                    pGdbCtx->gdb_state = GDB_PAYLOAD;
               }
               if (*pBuf == '+')
               {
                    pGdbCtx->iAckCount++;
               } 
               if (*pBuf == '-')
               {
                    pGdbCtx->iNakCount++;
               }
               pGdbCtx->pResp[pGdbCtx->iRd++] = *pBuf;
               if (*pBuf == 0x03)
               {
                   /* GDB Ctrl-C */
                   if (pFn)
                   {
                       pFn(pGdbCtx, 1);
                       pGdbCtx->iRd = 0;
                   }
               }
               pBuf++;
               break;
            case GDB_PAYLOAD:
               TRACE(0, "GDB_PAYLOAD: '%c' 0x%02x\n", isprint(*pBuf) ? *pBuf : '.', *pBuf);
                   pGdbCtx->pResp[pGdbCtx->iRd++] = *pBuf;
               if (*pBuf == '#')
               {
                   pGdbCtx->gdb_state = GDB_CSUM1;
               }
               pBuf++;
               break;
            case GDB_CSUM1:
               TRACE(0, "GDB_CSUM1: '%c'\n", *pBuf);
               pGdbCtx->csum = hexchartoi(*pBuf) << 4;
               pGdbCtx->gdb_state = GDB_CSUM2;
               pGdbCtx->pResp[pGdbCtx->iRd++] = *pBuf;
               pBuf++;
               break;
            case GDB_CSUM2:
               TRACE(0, "GDB_CSUM2: '%c'\n", *pBuf);
               pGdbCtx->csum |= hexchartoi(*pBuf);
               pGdbCtx->pResp[pGdbCtx->iRd++] = *pBuf;
               if (pFn)
               {
                   if (gdb_validate(pGdbCtx->pResp, pGdbCtx->csum) == 0)
                   {
                       pFn(pGdbCtx, 1);
                   }
                   else
                   {
                       pFn(pGdbCtx, 0);
                   }
               }
               pGdbCtx->iRd = 0;
               pGdbCtx->gdb_state = GDB_IDLE;
               pBuf++;
               break;
        }
    }
}

//
// Grab the payload from the GDB context and call pass it over to the socket
// handling code.
//
static void gdb_packet_from_usb(GDBCTX *pGdbCtx, int bCsumValid)
{
    pGdbCtx->pResp[pGdbCtx->iRd] = 0;
    usbRxResp(pGdbCtx->pResp, pGdbCtx->iRd);
}

//****************************************************************************
//
//  This is the USB callback that gets called whenever our background read
//  operation RX'es anything from the USB devices (ie. the GDB server)
//
//****************************************************************************
void LIBUSB_CALL
usb_callback(struct libusb_transfer *pTrans)
{
    int rc;
    
    TRACE(1, "%s: enter\n", __FUNCTION__);
    //
    // Here we want to "digest" the received packet.  Then, hopefully,
    // we can resubmit this same pTrans structure for the next receive
    //
    switch(pTrans->status)
    {
        case LIBUSB_TRANSFER_COMPLETED:
            //
            // Process whatever data we've RX'ed by invoking the GDB state
            // machine.  When a complete GDB packet has been RX'ed the state
            // machine will call gdb_packet_from_usb.
            //
            gdb_statemachine(pTrans->user_data, pTrans->buffer,
                             pTrans->actual_length, gdb_packet_from_usb);

            //
            // Requeue the async RX operation so that we catch the next packet
            // from the USB device
            //
            rc = libusb_submit_transfer(pTrans);
            if (rc != 0)
            {
                TRACE(ALWAYS, "%s: submit_transfer: rc = 0x%08x\n", __FUNCTION__, rc);
            }
            break;

        default:
            TRACE(ALWAYS, "%s: status = 0x%08x\n", __FUNCTION__, pTrans->status);
            break;
    }
}
