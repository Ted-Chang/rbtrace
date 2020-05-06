#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include "rbtrace.h"

#define NR_THREADS	4

#define MAX_LEN		100

volatile int x = 0;

static void *print_thread(void *arg)
{
	while (1)  {
		useconds_t us;
		uint32_t v;
		uint8_t id;
		uint64_t a0;
		uint64_t a1;
		uint64_t a2;

		us = rand() % 100;
		usleep(us);

		id = rand() % RBT_TRAFFIC_LAST;
		a0 = rand() % MAX_LEN;
		a1 = rand();
		a2 = rand();
		v = __sync_add_and_fetch(&x, 1);

		rbtrace(RBTRACE_RING_IO, id, x, a0, a1, a2);
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
