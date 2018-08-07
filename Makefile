# Author: Ted Zhang
CFLAGS = -Wall -g -fstack-protector -I./include -D_GNU_SOURCE -lrt -lpthread
CC = gcc
AR = ar

all: librbtrace rbtraced rbt prbt rbtbench

librbtrace:
	$(CC) $(CFLAGS) -O2 -c -o rbtrace.o rbtrace.c
	$(AR) rcs librbtrace.a rbtrace.o

rbtraced:
	$(CC) $(CFLAGS) -O2 rbtraced.c rbtrace_backing.c librbtrace.a -o rbtraced

rbt:
	$(CC) $(CFLAGS) rbt.c rbtrace_backing.c librbtrace.a -o rbt

prbt:
	$(CC) $(CFLAGS) prbt.c -o prbt

rbtbench:
	$(CC) $(CFLAGS) -O2 rbtbench.c librbtrace.a -o rbtbench

clean:
	rm -rf *.o librbtrace.a rbt prbt rbtraced rbtbench
