EXE := icdiflasher

CFLAGS := -g -O2
LDFLAGS := -lusb-1.0

all: $(EXE)

$(EXE): $(EXE).c
	gcc $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f *.o $(EXE)

.PHONY: all clean
