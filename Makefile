EXE := lm4flash

CFLAGS := -Wall -g -O2 $(shell pkg-config --cflags libusb-1.0)
LDFLAGS := $(shell pkg-config --libs libusb-1.0)

all: $(EXE)

$(EXE): $(EXE).c
	gcc $(CFLAGS) $^ $(LDFLAGS) -o $@

clean:
	rm -f *.o $(EXE)

.PHONY: all clean
