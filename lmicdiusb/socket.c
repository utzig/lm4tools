//*****************************************************************************
//
// socket.c - 
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
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

static unsigned char pGdbReq[MSGSIZE];

//
// This GDB context is for tracking the state of packets
// which come over a TCP socket.  That is to say, GDB 
// requests from the client.
static GDBCTX gdbCliCtx = 
{
    GDB_IDLE,
    pGdbReq,
    0, 0, 0, 0
};
        
static int sdAccept;
// static unsigned char endpOut;

static struct libusb_transfer *pTransReq;

//*****************************************************************************
//
//! This is the callback for handling the transfer completion of the GDB ack
//!
//! \param pTrans is pointer to the usb transaction structure in which we   
//! transmitted the [N]ACK packet.
//!
//! \return None
//
//*****************************************************************************
static void
usb_req_callback(struct libusb_transfer *pTrans)
{
    if (pTrans->status != LIBUSB_TRANSFER_COMPLETED)
    {
        TRACE(ALWAYS, "%s: Unable to send request (status = %d)\n", 
              __FUNCTION__, pTrans->status);
    }
    else
    {
        TRACE(1, "%s: GDB REQ sent successfully\n", __FUNCTION__);
    }
}

//
// After we receive a complete packet from a TCP socket we send it 
// off asynchronously.
//
static void
usbTxReq(GDBCTX *pGdbCtx, int ack)
{
    int rc;
    static char bInited = 0;

    if (!bInited)
    {
        //
        // Set up an async transfer to ack gdb responses...
        //
        pTransReq = libusb_alloc_transfer(0);
        ASSERT(pTransReq != NULL);
            
        pTransReq->dev_handle = phDev;
        pTransReq->flags = 0;
        pTransReq->endpoint = pdEndpOut->bEndpointAddress;
        pTransReq->type = LIBUSB_TRANSFER_TYPE_BULK;
        pTransReq->timeout = 1000;
        pTransReq->callback = usb_req_callback;
        pTransReq->num_iso_packets = 0;
        bInited = 1;
    }

    //
    // NOTE: There is a possible race condition here because USB may not
    // consume this buffer before we start writing to it.  BUT
    // since the GDB protocol is inherently syncrhonous,  I think 
    // we avoid that scenario.
    //
    pGdbCtx->pResp[pGdbCtx->iRd] = 0;
    pTransReq->buffer = pGdbCtx->pResp;
    pTransReq->length = pGdbCtx->iRd;

    TRACE(0, "%s: '%s'\n", __FUNCTION__, pGdbCtx->pResp);

    rc = libusb_submit_transfer(pTransReq);
    //
    // TODO: handle the case where we don't send the whole packet...
    //
    if (rc != 0)
    {
        TRACE(ALWAYS, "%s: ERROR rc = %d\n", __FUNCTION__, rc);
    }

    TRACE(1, "%s: ...done\n", __FUNCTION__);
 
}

//
// Whenever we receive a packet from USB (ie. the server)
// send it over the TCP socket.
//
void
usbRxResp(unsigned char *pResp, unsigned int len)
{
    pResp[len] = 0;
    TRACE(1, "%s: '%s'\n", __FUNCTION__, pResp);
    send(sdAccept, pResp, len, 0);
}

//
// Wait for a connection on iPort and return the 
// 
static int
Listen(unsigned int iPort)
{
    unsigned int addrlen;
   	struct   sockaddr_in sin;
	struct   sockaddr_in pin;
	int so_reuseaddr = 1;
    int sdListen;

    //
    // Open an internet socket 
    //
	if ((sdListen = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return(-1);
	}

	// Attempt to set SO_REUSEADDR to avoid "address already in use" errors
	setsockopt(sdListen,
		   SOL_SOCKET,
		   SO_REUSEADDR,
		   &so_reuseaddr,
		   sizeof(so_reuseaddr));

    //
    // Set up to listen on all interfaces/addresses and port iPort
    //
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(iPort);

    //
    // Bind out file descriptor to the port/address
    //
    TRACE(1, "bind to port %d\n", iPort);
	if (bind(sdListen, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
		perror("bind");
		return(-1);
	}

    //
    // Put the file descriptor in a state where it can listen for an 
    // incoming connection
    //
    TRACE(1, "listen\n");
	if (listen(sdListen, 1) == -1) {
		perror("listen");
		return(-1);
	} 

    // 
    // Accept() will block until someone connects.  In return it gives
    // us a new file descriptor back.
    //
    TRACE(1, "accept...\n");
    addrlen = sizeof(pin); 
	if ((sdAccept = accept(sdListen, (struct sockaddr *)  &pin, &addrlen)) == -1) {
		perror("accept");
		return(-1);
	}

    //
    // Close the file descriptor that we used for listening
    //
    close(sdListen);

    return sdAccept;
}

static int 
doGdb(int sdAccept, struct pollfd *pPollFds, int iFds)
{
    static unsigned char pMsg[MSGSIZE];
    ssize_t  rx;
    int      rc, iTimeoutms, i;
    struct   timeval tv;

    rc = 0;
    while(1) 
    {
        //
        // A bit of paranoia coding.  Clear out the old buffer before rx'ing
        // the next packet
        //
        memset(pMsg, 0, sizeof(pMsg));

        //
        // Figure out how long the timeout on our next poll will be.
        //
        rc = libusb_get_next_timeout(pCtx, &tv);
        if (rc == 0)
        {
            //
            // no timeouts are pending from usb.  Force to 1ms.
            //
            iTimeoutms = 10;
        }
        else
        {
            unsigned long long usecs;
            if ((tv.tv_sec == 0) && (tv.tv_usec == 0))
            {
                //
                // we were told we had a timeout with a 0 tv
                // that means something is already pending...
                //
                rc = libusb_handle_events(pCtx);
                ASSERT(rc == 0);
            }
            else
            {
                //
                // Convert the Secs + uSecs in the TV to millisecs.
                //
                usecs = tv.tv_sec * 1000000 + tv.tv_usec;
                iTimeoutms = usecs/1000;
            }
        }

        //
        // Here's the magic!  Wait for something interesting to happen!
        //
        rc = poll(pPollFds, iFds, iTimeoutms); 

        //
        // If we woke up because of a timeout, just go back to sleep...
        // (or if we have some other maintenance to do, here is the place
        // to do it)
        //
        if (rc == 0) continue;

        //
        // handle any errors
        //
        if (rc == -1) 
        {
            TRACE(ALWAYS, "%s: poll() returned error\n", __FUNCTION__);
            break;
        }

        //
        // If we get here then one or more FD's need servicing...
        //
        for (i = 0; i < iFds; i++)
        {
            // 
            // if revents is 0 then we don't care about this file descriptor
            //
            if (pPollFds[i].revents == 0)
            {
                // change to trace level 1
                TRACE(1,"%s: Fd[%d].revents = 0\n", __FUNCTION__, i);
                continue;
            }

            //
            // Was it our TCP socket the kicked poll() out of its block?
            //
            if (pPollFds[i].fd == sdAccept)
            {
                TRACE(1,"%s: socketFd.revents = 0x%08x\n", __FUNCTION__, 
                      pPollFds[i].revents);

                //
                // Grab the packet payload...
                //
                rx = recv(sdAccept, pMsg, MSGSIZE, 0);
                if (rx < 0)
                {
                    TRACE(ALWAYS, "%s: ERROR: recv()  returned %d\n", 
                          __FUNCTION__, (int)rx);
                    perror("recv() failed");
                }
                else
                {
                    TRACE(1, "%s: recv returned %d\n", __FUNCTION__, (int)rx);
                    if (rx == 0)
                    {
                        // 
                        // if we RX 0 bytes it usually means that the other 
                        // side closed the connection
                        //
                        rc = 0;
                        break;
                    }
                    gdb_statemachine(&gdbCliCtx, pMsg, rx, usbTxReq);
                }
            }
            //
            // If we get here it's because poll() kicked out, not because
            // of TCP activity, but because of USB activity.
            //
            else
            {
                TRACE(1, "%s: USB activity.  Calling handle_events()\n",
                       __FUNCTION__);
                rc = libusb_handle_events(pCtx);
                ASSERT(rc == 0);
            }
        } // (for all file descriptors)
    } // (while 1)

    //
    // We either encountered an error or the other side closed the connection.
    // Close our file handle and go back to listening for a new connection.
    //
    close(sdAccept);
    return rc;        
}

int SocketIO(int iPort, libusb_device_handle *phDev)
{
    int      iCount, iFds;

    const struct   libusb_pollfd **ppUsbFds;
    const struct   libusb_pollfd **ppTmpFd;

    struct   pollfd *pPollFds;

    ppTmpFd = ppUsbFds = libusb_get_pollfds(pCtx);

    //
    // find out how many USB FD's we have to poll
    //
    iCount = 0;
    while (*ppTmpFd++)
    {
        iCount++;
    }
    
    // 
    // allocate iCount + 1 pollfd's to account for the socket
    // that we'll open below
    //
    pPollFds = calloc(iCount + 1, sizeof(struct pollfd));
    ASSERT(pPollFds != NULL);
    
    for (iFds = 0; iFds < iCount; iFds++)
    {   
        pPollFds[iFds].fd = (*(ppUsbFds[iFds])).fd;
        pPollFds[iFds].events = (*(ppUsbFds[iFds])).events;
    }

    while (1)
    {   
        sdAccept = Listen(iPort);
        if (sdAccept > 0) 
        {
            //
            // Set our socket as the last filedescriptor to poll
            //
            pPollFds[iFds].fd = sdAccept;
            pPollFds[iFds].events = POLLIN | POLLPRI | POLLRDNORM; // | POLLRDHUP;
            iFds++;

            //
            // Do the bridging between the socket and the usb bulk device
            //
            doGdb(sdAccept, pPollFds, iFds);
        }
    }

    return(0);
}
