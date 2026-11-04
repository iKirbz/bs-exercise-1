# Program: Feedback Arc Set
# Author: Anton Prem, 12522014

CC = gcc
DEFS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE \
       -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -g -std=c99 -pedantic $(DEFS)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
LDLIBS += -pthread -lrt
endif

DEBUG ?= 0
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG
endif

.PHONY: all clean
all: generator supervisor

generator: generator.o shared_buffer.o
	$(CC) -o $@ $^ $(LDLIBS)

supervisor: supervisor.o shared_buffer.o
	$(CC) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

generator.o: generator.c generator.h shared_buffer.h
shared_buffer.o: shared_buffer.c shared_buffer.h generator.h
supervisor.o: supervisor.c shared_buffer.h generator.h

clean:
	rm -f *.o generator supervisor
