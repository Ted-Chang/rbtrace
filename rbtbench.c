#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <pthread.h>
#include <assert.h>
#include "rbtrace.h"
#include "rbtracedef.h"

#ifndef gettid
#define gettid()	syscall(__NR_gettid)
#endif

struct bench_option {
	int nr_processes;
	int nr_threads;
	int nr_traces;
} opts = {
	.nr_processes = 1,
	.nr_threads = 1,
	.nr_traces = 128 * 1024,
};

struct bench_context {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pid_t pid;
	int x;
};

static void do_bench(struct bench_context *ctx)
{
	int x;
	ctx->pid = getpid();
	while ((x = __sync_add_and_fetch(&ctx->x, 1)) < opts.nr_traces) {
		rbtrace(RBTRACE_RING_IO, RBT_TRAFFIC_TEST,
			ctx->pid, x, x, x);
	}
}

static void usage(void);

static void *benchmark_thread(void *arg)
{
	int rc;
	struct bench_context *ctx;

	ctx = (struct bench_context *)arg;
	
	/* Mutex unlocked if condition signaled */
	rc = pthread_mutex_lock(&ctx->mutex);
	if (rc != 0) {
		fprintf(stderr, "Failed to lock mutex, error:%d\n", rc);
		goto out;
	}

	rc = pthread_cond_wait(&ctx->cond, &ctx->mutex);
	if (rc != 0) {
		fprintf(stderr, "Failed to wait cond, error:%d\n", rc);
		goto out;
	}

	rc = pthread_mutex_unlock(&ctx->mutex);
	if (rc != 0) {
		fprintf(stderr, "Failed to unlock mutex, error:%d\n", rc);
		goto out;
	}

	printf("thread:%ld start benchmarking...\n", gettid());

	do_bench(ctx);

	printf("thread:%ld benmarking done.\n", gettid());

 out:
	return NULL;
}

int main(int argc, char *argv[])
{
	int rc = 0;
	int ch = 0;
	bool_t rbtrace_inited = FALSE;
	bool_t is_parent = TRUE;
	pthread_mutexattr_t mutex_attr;
	pthread_condattr_t cond_attr;
	pthread_t *threads = NULL;
	struct bench_context ctx;
	pid_t pid = -1;
	int i;

	while ((ch = getopt(argc, argv, "p:t:n:h")) != -1) {
		switch (ch) {
		case 'p':
			opts.nr_processes = atoi(optarg);
			if (opts.nr_processes <= 0) {
				fprintf(stderr, "Invalid number of process\n");
				goto out;
			}
			break;
		case 't':
			opts.nr_threads = atoi(optarg);
			if (opts.nr_threads <= 0) {
				fprintf(stderr, "Invalid number of threads\n");
				goto out;
			}
			break;
		case 'n':
			opts.nr_traces = atoi(optarg);
			if (opts.nr_traces <= 0) {
				fprintf(stderr, "Invalid number of traces\n");
				goto out;
			}
			break;
		case 'h':
		default:
			usage();
			goto out;
		}
	}

	/* Initialize cond var for inter process communicating */
	memset(&ctx, 0, sizeof(ctx));
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&ctx.mutex, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
	pthread_cond_init(&ctx.cond, &cond_attr);
	pthread_condattr_destroy(&cond_attr);

	/* Initialize rbtrace */
	rc = rbtrace_init();
	if (rc != 0) {
		fprintf(stderr, "rbtrace init failed, error:%d!\n", rc);
		goto out;
	}
	rbtrace_inited = TRUE;

	/* Create benchmark processes */
	for (i = 0; i < (opts.nr_processes - 1); i++) {
		pid = fork();
		assert(pid != -1);
		if (pid == 0) {
			is_parent = FALSE;
			break;
		} else {
			printf("forked process %d!\n", pid);
		}
	}

	/* Create benchmark threads */
	if (opts.nr_threads > 1) {
		threads = malloc(sizeof(*threads) * opts.nr_threads);
		assert(threads != NULL);
		for (i = 0; i < (opts.nr_threads - 1); i++) {
			rc = pthread_create(&threads[i], NULL,
					    benchmark_thread,
					    &ctx);
			assert(rc == 0);
		}
	}

	if (is_parent) {
		/* Wait for all threads to be ready */
		sleep(3);

		/* Broadcast starting signal. Ready, GO! */
		rc = pthread_mutex_lock(&ctx.mutex);
		if (rc != 0) {
			goto out;
		}
		rc = pthread_cond_broadcast(&ctx.cond);
		if (rc != 0) {
			goto out;
		}
		rc = pthread_mutex_unlock(&ctx.mutex);
		if (rc != 0) {
			goto out;
		}
	} else {
		rc = pthread_mutex_lock(&ctx.mutex);
		if (rc != 0) {
			goto out;
		}
		rc = pthread_cond_wait(&ctx.cond, &ctx.mutex);
		if (rc != 0) {
			goto out;
		}
		rc = pthread_mutex_lock(&ctx.mutex);
		if (rc != 0) {
			goto out;
		}
	}

	printf("thread:%ld start benchmarking...\n", gettid());

	do_bench(&ctx);

	printf("thread:%ld benmarking done.\n", gettid());

	/* Join all benchmark threads */
	if (opts.nr_threads > 1) {
		for (i = 0; i < (opts.nr_threads - 1); i++) {
			pthread_join(threads[i], NULL);
		}
	}

 out:
	if (threads) {
		free(threads);
	}
	if (rbtrace_inited) {
		rbtrace_exit();
	}
	return rc;
}

static void usage(void)
{
	printf("Usage: ./rbtbench -p #processes -t #threads -n #traces\n");
}
