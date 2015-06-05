EXE := lm4flash

os := ${shell uname -s}

ifeq (${os},Darwin)
CC := clang
CFLAGS := -Wall -I/usr/local/include/libusb-1.0
LDFLAGS := -L/usr/local/lib -lusb-1.0
else
CC ?= gcc
CFLAGS := -Wall $(shell pkg-config --cflags libusb-1.0)
LDFLAGS := $(shell pkg-config --libs libusb-1.0)
endif

all: CFLAGS += -O2
all: $(EXE)

debug: CFLAGS += -g -DDEBUG
debug: $(EXE)

$(EXE): $(EXE).c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f *.o $(EXE)

.PHONY: all clean
