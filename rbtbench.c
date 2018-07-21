#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>
#include <assert.h>
#include "rbtrace.h"
#include "rbtracedef.h"

#ifndef gettid
#define gettid()	syscall(__NR_gettid)
#endif

#define SHM_NAME	"rbtbench"

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
	int ready_threads;
	int x;
};

static void do_bench(struct bench_context *ctx)
{
	int x;
	pid_t pid = getpid();
	while ((x = __sync_add_and_fetch(&ctx->x, 1)) < opts.nr_traces) {
		rbtrace(RBTRACE_RING_IO, RBT_TRAFFIC_TEST,
			pid, x, x, x);
	}
}

static void usage(void);

static void *benchmark_thread(void *arg)
{
	int rc;
	struct bench_context *ctx;

	ctx = (struct bench_context *)arg;

	printf("thread:%ld created!\n", gettid());
	
	/* Mutex unlocked if condition signaled */
	rc = pthread_mutex_lock(&ctx->mutex);
	if (rc != 0) {
		fprintf(stderr, "Failed to lock mutex, error:%d\n", rc);
		goto out;
	}

	__sync_add_and_fetch(&ctx->ready_threads, 1);

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
	int shmfd = -1;
	bool_t rbtrace_inited = FALSE;
	bool_t is_parent = TRUE;
	pthread_mutexattr_t mutex_attr;
	pthread_condattr_t cond_attr;
	pthread_t *threads = NULL;
	struct bench_context *ctx = NULL;
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

	/* Create shared memory */
	shmfd = shm_open(SHM_NAME, O_RDWR|O_CREAT|O_EXCL, 0666);
	if (shmfd == -1) {
		goto out;
	}
	rc = ftruncate(shmfd, sizeof(*ctx));
	if (rc == -1) {
		goto out;
	}
	ctx = mmap(NULL, sizeof(*ctx), PROT_READ|PROT_WRITE,
		   MAP_SHARED, shmfd, 0);
	if (ctx == MAP_FAILED) {
		goto out;
	}

	/* Initialize cond var for inter process communicating */
	memset(ctx, 0, sizeof(*ctx));
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&ctx->mutex, &mutex_attr);
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
	pthread_cond_init(&ctx->cond, &cond_attr);

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

	/* Map ctx into child's address space */
	if (!is_parent) {
		shmfd = shm_open(SHM_NAME, O_RDWR, 0666);
		if (shmfd == -1) {
			goto out;
		}
		ctx = mmap(NULL, sizeof(*ctx), PROT_READ|PROT_WRITE,
			   MAP_SHARED, shmfd, 0);
		if (ctx == MAP_FAILED) {
			goto out;
		}
	}

	/* No matter you are parent or child you need to init rbtrace */
	rc = rbtrace_init();
	if (rc != 0) {
		fprintf(stderr, "rbtrace init failed, error:%d!\n", rc);
		goto out;
	}
	rbtrace_inited = TRUE;

	/* Create benchmark threads */
	if (opts.nr_threads > 1) {
		threads = malloc(sizeof(*threads) * opts.nr_threads);
		assert(threads != NULL);
		for (i = 0; i < (opts.nr_threads - 1); i++) {
			rc = pthread_create(&threads[i], NULL,
					    benchmark_thread,
					    ctx);
			assert(rc == 0);
		}
	}

	if (is_parent) {
		rc = pthread_mutex_lock(&ctx->mutex);
		if (rc != 0) {
			goto out;
		}

		/* Wait for all threads to be ready */
		while (ctx->ready_threads <
		       (opts.nr_processes * opts.nr_threads - 1)) {
			rc = pthread_mutex_unlock(&ctx->mutex);
			sleep(1);
			rc = pthread_mutex_lock(&ctx->mutex);
		}

		printf("Ready, GO!\n");

		/* Broadcast starting signal. Ready, GO! */
		rc = pthread_cond_broadcast(&ctx->cond);
		if (rc != 0) {
			goto out;
		}
		rc = pthread_mutex_unlock(&ctx->mutex);
		if (rc != 0) {
			goto out;
		}
	} else {
		rc = pthread_mutex_lock(&ctx->mutex);
		if (rc != 0) {
			goto out;
		}

		__sync_add_and_fetch(&ctx->ready_threads, 1);

		rc = pthread_cond_wait(&ctx->cond, &ctx->mutex);
		if (rc != 0) {
			goto out;
		}
		rc = pthread_mutex_unlock(&ctx->mutex);
		if (rc != 0) {
			goto out;
		}
	}

	printf("thread:%ld start benchmarking...\n", gettid());

	do_bench(ctx);

	printf("thread:%ld benmarking done.\n", gettid());

	/* Join all benchmark threads */
	if (opts.nr_threads > 1) {
		for (i = 0; i < (opts.nr_threads - 1); i++) {
			pthread_join(threads[i], NULL);
		}
	}

	if (is_parent) {
		while ((pid = wait(NULL)) != -1) {
			printf("process %d exited!\n", pid);
		}
		printf("benchmark done!\n");
	}

 out:
	if (shmfd != -1) {
		if (ctx != NULL) {
			munmap(ctx, sizeof(*ctx));
		}
		close(shmfd);
		if (is_parent) {
			shm_unlink(SHM_NAME);
		}
	}
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
