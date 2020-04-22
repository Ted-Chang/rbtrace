# Author: Ted Zhang
CFLAGS = -Wall -g -fstack-protector -I./include -D_GNU_SOURCE -lrt -lpthread
CC = gcc
AR = ar

all: librbtrace rbtraced rbt prbt rbtbench test_segfault

librbtrace:
	$(CC) $(CFLAGS) -c -o rbtrace.o rbtrace.c
	$(AR) rcs librbtrace.a rbtrace.o

rbtraced: librbtrace
	$(CC) $(CFLAGS) rbtraced.c rbtrace_backing.c librbtrace.a -o rbtraced

rbt: librbtrace
	$(CC) $(CFLAGS) rbt.c rbtrace_backing.c librbtrace.a -o rbt

prbt:
	$(CC) $(CFLAGS) prbt.c -o prbt

rbtbench: librbtrace
	$(CC) $(CFLAGS) rbtbench.c librbtrace.a -o rbtbench

test_segfault: librbtrace
	$(CC) $(CFLAGS) test_segfault.c librbtrace.a -o test_segfault

clean:
	rm -rf *.o librbtrace.a rbt prbt rbtraced rbtbench test_segfault

check:
	./autotest.sh
