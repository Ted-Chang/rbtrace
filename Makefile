# Author: Ted Zhang
CFLAGS = -Wall -g -O2 -fstack-protector -I./include -D_GNU_SOURCE -lrt -lpthread
CC = gcc
AR = ar

all: librbtrace rbtraced rbt rbtbench

librbtrace:
	$(CC) $(CFLAGS) -c -o rbtrace.o rbtrace.c
	$(AR) rcs librbtrace.a rbtrace.o

rbtraced:
	$(CC) $(CFLAGS) rbtraced.c rbtrace_backing.c librbtrace.a -o rbtraced

rbt:
	$(CC) $(CFLAGS) rbt.c rbtrace_backing.c librbtrace.a -o rbt

rbtbench:
	$(CC) $(CFLAGS) rbtbench.c librbtrace.a -o rbtbench

clean:
	rm *.o librbtrace.a rbt rbtraced rbtbench
