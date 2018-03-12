#//*****************************************************************************
#//
#// Makefile - to build lmicdiusb utility
#//
#// Copyright (c) 2011-2012 Texas Instruments Incorporated.
#// All rights reserved.
#//
#// Software License Agreement
#//
#//   Redistribution and use in source and binary forms, with or without
#//   modification, are permitted provided that the following conditions
#//   are met:
#//
#//   Redistributions of source code must retain the above copyright
#//   notice, this list of conditions and the following disclaimer.
#//
#//   Redistributions in binary form must reproduce the above copyright
#//   notice, this list of conditions and the following disclaimer in the
#//   documentation and/or other materials provided with the
#//   distribution.
#//
#//   Neither the name of Texas Instruments Incorporated nor the names of
#//   its contributors may be used to endorse or promote products derived
#//   from this software without specific prior written permission.
#//
#// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
#//
#//*****************************************************************************
ifeq ($(shell uname),Darwin)
	LIBUSB_CFLAGS := -I/usr/local/include/libusb-1.0
	LIBUSB_LIBDIR := /usr/local/lib
	LIBUSB_LIBS := -lusb-1.0
	LDFLAGS += -framework AppKit -framework Carbon -framework IOKit
endif

ifeq ($(shell uname),FreeBSD)
	LIBUSB_CFLAGS :=
	LIBUSB_LIBDIR := /usr/lib
	LIBUSB_LIBS := -lusb
endif

ifeq ($(shell uname),Linux)
	LIBUSB_CFLAGS ?= $(shell pkg-config --cflags libusb-1.0)
	LIBUSB_LIBDIR ?= $(shell pkg-config --variable=libdir libusb-1.0)
	LIBUSB_LIBS ?= $(shell pkg-config --libs-only-l libusb-1.0)
endif

VPATH += $(LIBUSB_LIBDIR)

LDFLAGS += -L$(LIBUSB_LIBDIR) -g

CFLAGS += -Wall -g $(LIBUSB_CFLAGS)

all: lmicdi

lmicdi: lmicdi.o socket.o gdb.o $(LIBUSB_LIBS)

lmicdi.o socket.o: lmicdi.h

install: lmicdi
ifndef PREFIX
	$(error PREFIX is not set)
endif
	mkdir -p $(PREFIX)/bin
	install $^ $(PREFIX)/bin/

.PHONY: all clean

clean:
	rm -rf lmicdi lmicdi.o socket.o gdb.o

