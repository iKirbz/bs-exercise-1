CC = gcc
DEFS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE \
       -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -g -std=c99 -pedantic $(DEFS)

DEBUG ?= 0
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG
endif

.PHONY: all clean
all: generator supervisor

generator: generator.o
	$(CC) -o $@ $^

supervisor: supervisor.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# generator.o: generator.c generator.h
# supervisor.o: supervisor.c supervisor.h

clean:
	rm -f *.o generator supervisor