//*****************************************************************************
//
// lmicdi.c - 
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
#include <unistd.h>
#include <time.h>

struct libusb_context *pCtx;

#ifndef TRACE_LEVEL
#define TRACE_LEVEL 2
#endif
unsigned int gTraceLvl = TRACE_LEVEL;

const struct libusb_endpoint_descriptor *pdEndpIn, *pdEndpOut;
libusb_device_handle *phDev;

unsigned char pResp[MSGSIZE];


void
_dump_dev_strings(libusb_device_handle *phDev,
              struct libusb_device_descriptor *pdDev,
              const char *pIndent)
{
    unsigned char pStr[256];
    int rc;

    memset(pStr, 0, sizeof(pStr));

    rc = libusb_get_string_descriptor_ascii(phDev, pdDev->iManufacturer, 
            pStr, sizeof(pStr));
    TRACE(2, "%sMFG'r string (index %d, len = %d) = '%s'\n",
          pIndent,
          pdDev->iManufacturer, rc, pStr);

    rc = libusb_get_string_descriptor_ascii(phDev, pdDev->iProduct, 
            pStr, sizeof(pStr));
    TRACE(2, "%sProduct string (index %d, len = %d) = '%s'\n",
          pIndent,
          pdDev->iProduct, rc, pStr);

    rc = libusb_get_string_descriptor_ascii(phDev, pdDev->iSerialNumber, 
            pStr, sizeof(pStr));
    TRACE(2, "%sProduct serial number (index %d, len = %d) = '%s'\n",
          pIndent,
          pdDev->iSerialNumber, rc, pStr);
}

void
_dump_cfg_strings(libusb_device_handle *phDev,
                  struct  libusb_config_descriptor *pdCfg,
                  const char *pIndent)
{
    unsigned char pStr[256];
    int rc;

    memset(pStr, 0, sizeof(pStr));

    rc = libusb_get_string_descriptor_ascii(phDev, pdCfg->iConfiguration, 
            pStr, sizeof(pStr));
    TRACE(1, "%sConfig name (index %d, len = %d) = '%s'\n",
          pIndent,
          pdCfg->iConfiguration, rc, pStr);
}

void
_dump_if_strings(libusb_device_handle *phDev,
                 const struct libusb_interface_descriptor *pdIf,
                 const char *pIndent)

{
    unsigned char pStr[256];
    int rc;

    memset(pStr, 0, sizeof(pStr));

    rc = libusb_get_string_descriptor_ascii(phDev, pdIf->iInterface,
            pStr, sizeof(pStr));
    TRACE(1, "%sInterface name (index %d, len = %d) = '%s'\n",
          pIndent,
          pdIf->iInterface, rc, pStr);
}

//
// This is the GDB context for GDB responses from the USB target
//
unsigned char pUsbResp[MSGSIZE];
GDBCTX gdbUsbCtx =
{
    GDB_IDLE,                   // gdb_state
    pUsbResp,                   // pResp
    0,                          // iRd
    0,                          // csum
    0,                          // iAckCount
    0                           // iNakCount
};
//
// This is the transfer structure that maintains the USB read in 
// the background.
//
struct libusb_transfer *pTrans;

//*****************************************************************************
//
//! Initializes the sample API.
//!
//! This function prepares the sample API for use by the application.
//!
//! \return None.
//
//*****************************************************************************
int
main(int argc, char *argv[])
{
    int rc;
    unsigned int iDev, iCfg, iIf, iAlt, iEndp;
    ssize_t nDevs;

    libusb_device        **pDevices;
    libusb_device        *pDev;

    struct libusb_config_descriptor *pdCfg;
    struct libusb_device_descriptor dDev;

    rc = libusb_init(&pCtx);
    ASSERT(rc == 0);

    // libusb_set_debug(pCtx, 5);


    nDevs = libusb_get_device_list(pCtx, &pDevices);
    TRACE(0, "nDevs = %d\n", (int)nDevs);
    ASSERT(nDevs >= 0);

    pDev = NULL;
    for (iDev = 0; iDev < nDevs; iDev++)
    {
        TRACE(0, "Considering device %d\n", iDev);
        //
        // Get the device descriptor so we know how many configurations there are
        //
        rc = libusb_get_device_descriptor(pDevices[iDev], &dDev);
        ASSERT(rc == 0);
        if ((dDev.idVendor == LMICDI_VID) &&
            (dDev.idProduct == LMICDI_PID))
        {
            pDev = pDevices[iDev];
            TRACE(1, "Found device with matching VID and PID.  pDev = %p\n", pDev);
            break;
        }
    }

    if (NULL == pDev)
    {
        fprintf(stderr, "No ICDI device with USB VID:PID %04x:%04x found!\n",
                LMICDI_VID, LMICDI_PID);
        return EXIT_FAILURE;
    }

    rc = libusb_open(pDev, &phDev);
    if (rc != 0)
    {
        TRACE(ALWAYS, "Failed to open device.  rc = %d\n", rc);
        libusb_free_device_list(pDevices, 1);
        goto out;
    }

    //
    // NOTE: If/When we get here dDev should still be valid
    //

    // For each configuration... 
    //   for each interface...
    //     for each alternate config for the interface...
    //        for each endpoint...
    //
    for (iCfg = 0; iCfg < dDev.bNumConfigurations; iCfg++)
    {
        TRACE(1, D0 "iCfg = %d\n", iCfg);

        rc = libusb_get_config_descriptor(pDev, iCfg, &pdCfg);
        ASSERT(rc == 0);

        //
        // It seems as though we need to detach the kernel before we can read
        // strings for ourselves.
        //
#if 0
        for (iIf = 0; iIf < pdCfg->bNumInterfaces; iIf++)
        {
            rc = libusb_kernel_driver_active(phDev, iIf);
            TRACE(1, D1 "kernel_driver_active(if%d) = %d\n",
                  iIf, rc);
            if (rc)
            {
                TRACE(1, D1 "Attempting to detach...");
                rc = libusb_detach_kernel_driver(phDev, iIf);
                if (rc)
                {
                    TRACE(1, " failed (%d)\n", rc);
                }
                else
                {
                    bKernel[iIf] = 1;

                    TRACE(1, " OK\n");
                }
            }
        }
#endif
        //
        // if the MFGr string is coming back as 0 then the device is wedged.
        //
        if (dDev.iManufacturer == 0) exit(EXIT_FAILURE);

        //
        // TODO: Figure out why some string indexes are coming back as 0
        //
        // _dump_cfg_strings(phDev, pdCfg, D0);
        // _dump_dev_strings(phDev, &dDev, D0);

        for (iIf = 0; iIf < pdCfg->bNumInterfaces; iIf++)
        {
            const struct libusb_interface *pIf = &pdCfg->interface[iIf];
            TRACE(1, D1 "iIf = %d\n", iIf);

            for (iAlt = 0; iAlt < pIf->num_altsetting; iAlt++)
            {
                const struct libusb_interface_descriptor *pdIf = 
                    &pIf->altsetting[iAlt];

                TRACE(1, D2 "iAlt = %d\n", iAlt);
                _dump_if_strings(phDev, pdIf, D2);

                //
                // The "correct" thing to do would be to identify the 
                // interface based on its name and string index but, at 
                // the moment, the iInterface is coming back as 0. 
                // Instead, we'll grab the interface that uses the
                // VENDOR_SPECIFIC class 
                //
                if (pdIf->bInterfaceClass != LIBUSB_CLASS_VENDOR_SPEC)
                {
                    continue;
                }
                
                // 
                // The interface we're interested in should have two endpoints
                //
                if (pdIf->bNumEndpoints != 2)
                {
                    continue;
                }

                rc = libusb_claim_interface(phDev, iIf);
                ASSERT(rc == 0);
                
                for (iEndp = 0; iEndp < pdIf->bNumEndpoints; iEndp++)
                {
                    const struct libusb_endpoint_descriptor *pdEndp = 
                        &pdIf->endpoint[iEndp];

                    TRACE(1, D3 "iEndp = %d\n", iEndp);
                   if ((pdEndp->bmAttributes & 0x3) != 
                            LIBUSB_TRANSFER_TYPE_BULK)
                    {
                        continue;
                    }

                    if ((pdEndp->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == 
                            LIBUSB_ENDPOINT_IN)
                    {
                        TRACE(1, D3 "Found ENDPOINT_IN\n");
                        pdEndpIn = pdEndp;
                        continue;
                    }

                    if ((pdEndp->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == 
                            LIBUSB_ENDPOINT_OUT)
                    {
                        TRACE(1, D3 "Found ENDPOINT_OUT\n");
                        pdEndpOut = pdEndp;
                        continue;
                    }

                    //
                    // We should never get here.  An endpoint has to be either 
                    // in or out
                    //
                    TRACE(ALWAYS, "%s[%d]: Unexpected error\n", __FILE__, __LINE__);
                    ASSERT(0);
                }
                goto found;
            }    
        }
    }

found:
    libusb_free_device_list(pDevices, 1);

    //
    // EXPERIMENT:  Can we kick of an async receive and then do a 
    // synchronous transmit? ANSWER:  YES!
    //
    pTrans = libusb_alloc_transfer(0);
    ASSERT(pTrans != NULL);
    
    //
    // Set up a pending receive...
    //
    libusb_fill_bulk_transfer(pTrans, phDev, pdEndpIn->bEndpointAddress,
            pResp, sizeof(pResp), usb_callback, &gdbUsbCtx, 0);

    //
    // And wait for a RX operation to complete...
    //
    rc = libusb_submit_transfer(pTrans);
    if (rc != 0)
    {
        TRACE(ALWAYS, "%s: ERROR: submit_transfer rc = %d\n", __FUNCTION__, rc);
    }

    SocketIO(PORT, phDev);

    TRACE(1, "%s: libusb_release_interface\n", __FUNCTION__);
    libusb_release_interface(phDev, iIf);

    TRACE(1, "%s: libusb_close(phDev)\n", __FUNCTION__);
    libusb_close(phDev);

out:
    TRACE(1, "%s: libusb_exit\n", __FUNCTION__);
    libusb_exit(pCtx);

    return(EXIT_SUCCESS);
}
