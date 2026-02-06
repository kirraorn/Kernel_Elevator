PROGRAMS := empty part1
TRACES := $(addsuffix .trace, $(PROGRAMS))

all: $(TRACES)

%: %.c
	gcc -o $@ $< 

%.trace: %
	strace -o $@ ./$<

empty: empty.c
part1: part1.c

.PHONY: all clean

clean:
	rm -f $(PROGRAMS) $(TRACES)
