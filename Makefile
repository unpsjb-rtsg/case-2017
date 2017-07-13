CC=gcc

CFLAGS += -Wall -g

CLIBS += -L/usr/lib -lxml2 -lm
CLIBS += -lgsl -lgslcblas

INCLUDE_PATHS += -I/usr/include/libxml2

all: wcrt-test-sim

wcrt-test-sim: wcrt-test-sim.c
	$(CC) -o $@ $< $(CFLAGS) $(CLIBS) $(INCLUDE_PATHS) 

clean:
	rm wcrt-test-sim.o wcrt-test-sim.exe wcrt-test-sim
