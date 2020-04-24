#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include "rbtrace.h"

#define NR_THREADS	2

volatile int x = 0;

static void *print_thread(void *arg)
{
	while (1) {
		useconds_t us;
		uint32_t v;

		us = rand() % 1000;
		us *= 10;
		usleep(us);

		v = __sync_add_and_fetch(&x, 1);
		rbtrace(RBTRACE_RING_IO, RBT_TRAFFIC_TEST, x, 0, 0, 0);
		if ((v % 1000) == 0) {
			printf("printed %u traces\n", v);
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	int rc = 0;
	int i = 0;
	pthread_t threads[NR_THREADS];

	srand((int)time(NULL));

	rc = rbtrace_init();
	if (rc != 0) {
		fprintf(stderr, "rbtrace init failed, error:%d!\n", rc);
		goto out;
	}

	for (i = 0; i < NR_THREADS; i++) {
		pthread_create(&threads[i], NULL, print_thread, NULL);
	}

	for (i = 0; i < NR_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

 out:
	return rc;
}
