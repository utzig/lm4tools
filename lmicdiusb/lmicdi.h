//*****************************************************************************
//
// lmicdi.h - 
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
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

/* LIBUSB_CALL was introduced in libusb-1.0.9, which also fixes many bugs.
 * It's strongly recommended to not use any older version, but to be more
 * compatible let's include the LIBUSB_CALL definition here too.
 */
#ifndef LIBUSB_CALL
#if defined(_WIN32) || defined(__CYGWIN__)
#define LIBUSB_CALL WINAPI
#else
#define LIBUSB_CALL
#endif
#endif /* LIBUSB_CALL */

//*****************************************************************************
//
//   MACROS 
//
//*****************************************************************************

//
// Behavior related
//
#define PORT 					7777
#define MSGSIZE 8192

//
// Debug related
//
#define ASSERT                  assert
#define LMICDI_VID              0x1cbe
#define LMICDI_PID              0x00fd

#define TRACE(LVL, ...)         do { if ((LVL) >= gTraceLvl) { \
                                fprintf(stderr, __VA_ARGS__);} \
                                } while(0)

#define ALWAYS                  -1

#define D0                      ""
#define D1                      "\t"
#define D2                      "\t\t"
#define D3                      "\t\t\t"
#define D4                      "\t\t\t\t"


//*****************************************************************************
//
//   DATA TYPES
//
//*****************************************************************************
typedef enum { GDB_IDLE, GDB_PAYLOAD, GDB_CSUM1, GDB_CSUM2 } GDB_STATE;
typedef struct _GDBCTX
{
	GDB_STATE gdb_state;
	unsigned char *pResp;
	unsigned int iRd;
	unsigned char csum;
	unsigned int iAckCount;
	unsigned int iNakCount;
} GDBCTX;

//*****************************************************************************
//
//   GLOBALS
//
//*****************************************************************************
extern struct libusb_context *pCtx;
extern unsigned int gTraceLvl;

extern const struct libusb_endpoint_descriptor *pdEndpIn, *pdEndpOut;
extern libusb_device_handle *phDev;


//*****************************************************************************
//
//   FUNCTION PROTOTYPES
//
//*****************************************************************************
extern int
SocketIO(int iPort, libusb_device_handle *phDev);

void LIBUSB_CALL usb_callback
(struct libusb_transfer *pTrans);

void
gdb_send_ack(const char cAck);

void
usbRxResp(unsigned char *pResp, unsigned int len);

void
gdb_statemachine(GDBCTX *pGdbCtx, unsigned char *pBuf, unsigned int len,
        void(*pFn)(GDBCTX*, int));

