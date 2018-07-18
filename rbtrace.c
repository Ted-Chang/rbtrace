#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sched.h>
#include <semaphore.h>
#include <assert.h>
#include "rbtracedef.h"
#include "rbtrace.h"
#include "rbtrace_private.h"

#ifndef gettid
#define gettid() syscall(__NR_gettid)
#endif

#ifndef pause
#define pause()	__asm __volatile("pause\n": : : "memory")
#endif

#define RBTRACE_IO_RING_SIZE	(64*1024)

#define RBTRACE_DEFAULT_FSIZE	(2048)

struct rbtrace_config rbtrace_cfgs[] = {
	{
		RBTRACE_RING_IO,
		"io",
		"I/O traffic",
		0,
		RBTRACE_IO_RING_SIZE,
	},
};

STATIC_ASSERT((sizeof(rbtrace_cfgs)/sizeof(rbtrace_cfgs[0])) == RBTRACE_RING_MAX);

struct rbtrace_global_data rbtrace_globals = {
	.inited = FALSE,
	.shm_fd = -1,
	.shm_size = 0,
	.shm_base = MAP_FAILED,
	.sem_ptr = SEM_FAILED,
	.fsize_ptr = NULL,
	.ring_ptr = NULL,
	.ri_ptr = NULL,
	.rr_base = NULL,
};

void rbtrace_signal_thread(struct rbtrace_info *ri)
{
	if (ri->ri_flags & (RBTRACE_DO_CLOSE | RBTRACE_DO_DISK)) {
		if (sem_post(rbtrace_globals.sem_ptr) == -1) {
			DBG_ASSERT(0);
		}
	}
}

static struct rbtrace_record *
ringwrap_slot(struct rbtrace_info *ri, uint32_t slot)
{
	struct rbtrace_record *rr;
	int lost;

	if (slot == ri->ri_size) {
		/* swap ring buffer */
		if (__sync_add_and_fetch(&ri->ri_flush, 1) == 1) {
			size_t temp;
			temp = ri->ri_cir_off;
			ri->ri_cir_off = ri->ri_alt_off;
			ri->ri_alt_off = temp;

			__sync_lock_test_and_set(&ri->ri_slot, -1);
			lost = __sync_lock_test_and_set(&ri->ri_lost, 0);
			if (lost) {
				rbtrace(RBTRACE_RING_IO, RBT_LOST, lost, 0, 0, 0);
				dprintf("ring:%d lost %d records\n",
					ri->ri_ring, lost);
			}
			rbtrace_signal_thread(ri);
		} else {
			__sync_lock_test_and_set(&ri->ri_slot, -1);
			rbtrace_signal_thread(ri);
		}
	} else {
		int cnt = 1024;
		while ((ri->ri_slot > ri->ri_size) && (--cnt > 0)) {
			pause();
		}
		slot = __sync_add_and_fetch(&ri->ri_slot, 1);
		if (slot < ri->ri_size) {
			rr = ((struct rbtrace_record *)
			      (rbtrace_globals.rr_base + ri->ri_cir_off)) + slot;
			clock_gettime(CLOCK_REALTIME, &rr->rr_timestamp);
			rr->rr_cpuid = (uint16_t)sched_getcpu();
			rr->rr_thread = gettid();
			return rr;
		}

		__sync_add_and_fetch(&ri->ri_lost, 1);

		dprintf("ring:%d slot:%d lost\n", ri->ri_ring, slot);
		return NULL;
	}

	slot = __sync_add_and_fetch(&ri->ri_slot, 1);
	if (slot < ri->ri_size) {
		rr = ((struct rbtrace_record *)
		      (rbtrace_globals.rr_base + ri->ri_cir_off)) + slot;
		clock_gettime(CLOCK_REALTIME, &rr->rr_timestamp);
		rr->rr_cpuid = (uint16_t)sched_getcpu();
		rr->rr_thread = gettid();
		return rr;
	}

	__sync_val_compare_and_swap(&ri->ri_slot, slot, -1);
	__sync_add_and_fetch(&ri->ri_lost, 1);
	dprintf("ring:%d slot:%d trace lost\n", ri->ri_ring, slot);
	return NULL;
}

static struct rbtrace_record *
ringwrap(struct rbtrace_info *ri)
{
	uint32_t slot;
	struct rbtrace_record *rr;

	slot = __sync_add_and_fetch(&ri->ri_slot, 1);
	if (slot < ri->ri_size) {
		rr = ((struct rbtrace_record *)
		      (rbtrace_globals.rr_base + ri->ri_cir_off)) + slot;
		clock_gettime(CLOCK_REALTIME, &rr->rr_timestamp);
		rr->rr_cpuid = (uint16_t)sched_getcpu();
		rr->rr_thread = gettid();
		return rr;
	}

	return ringwrap_slot(ri, slot);
}

int rbtrace(rbtrace_ring_t ring, uint16_t traceid, uint64_t a0,
	    uint64_t a1, uint64_t a2, uint64_t a3)
{
	int rc = 0;
	struct rbtrace_info *ri;
	struct rbtrace_record *rr;

	if ((ring >= RBTRACE_RING_MAX) ||
	    (NULL == rbtrace_globals.ri_ptr)) {
		return -1;
	}

	ri = rbtrace_globals.ri_ptr + ring;
	rr = ringwrap(ri);
	if (rr == NULL) {
		rc = -1;
	} else {
		rr->rr_traceid = traceid;
		rr->rr_a0 = a0;
		rr->rr_a1 = a1;
		rr->rr_a2 = a2;
		rr->rr_a3 = a3;
	}

	return rc;
}

inline int rbtrace_traffic_enabled(rbtrace_ring_t ring, uint16_t traceid)
{
	if ((ring >= RBTRACE_RING_MAX) ||
	    (traceid >= RBT_LAST) ||
	    (rbtrace_globals.ri_ptr == NULL)) {
		return FALSE;
	}

	return rbtrace_globals.ri_ptr[ring].ri_tflags & (1 << traceid);
}

static size_t rbtrace_calc_ring_size(struct rbtrace_config *cfg)
{
	size_t size = 0;

	/* Double ring buffer */
	size = cfg->rc_size * sizeof(struct rbtrace_record) * 2;
	return size;
}

size_t rbtrace_calc_shm_size(void)
{
	int i;
	size_t size = 0;

	size += sizeof(*rbtrace_globals.fsize_ptr);
	size += sizeof(*rbtrace_globals.ring_ptr);

	for (i = RBTRACE_RING_IO; i < RBTRACE_RING_MAX; i++) {
		size += rbtrace_calc_ring_size(&rbtrace_cfgs[i]);
	}

	size += (RBTRACE_RING_MAX * sizeof(*rbtrace_globals.ri_ptr));

	return size;
}

void rbtrace_globals_init(int shm_fd, char *shm_base,
			  size_t shm_size,
			  sem_t *sem_ptr)
{
	size_t offset = 0;

	rbtrace_globals.shm_fd = shm_fd;
	rbtrace_globals.sem_ptr = sem_ptr;
	rbtrace_globals.shm_base = shm_base;
	rbtrace_globals.shm_size = shm_size;

	rbtrace_globals.fsize_ptr = (uint64_t *)(shm_base + offset);
	offset += sizeof(uint64_t);
	rbtrace_globals.ring_ptr = (rbtrace_ring_t *)(shm_base + offset);
	offset += sizeof(rbtrace_ring_t);
	rbtrace_globals.ri_ptr = (struct rbtrace_info *)(shm_base + offset);
	offset += sizeof(struct rbtrace_info) * RBTRACE_RING_MAX;
	rbtrace_globals.rr_base = (struct rbtrace_record *)(shm_base + offset);
	rbtrace_globals.inited = TRUE;
}

void rbtrace_globals_cleanup(bool_t is_daemon)
{
	if ((rbtrace_globals.sem_ptr != SEM_FAILED) &&
	    (rbtrace_globals.sem_ptr != NULL)) {
		sem_close(rbtrace_globals.sem_ptr);
		if (is_daemon) {
			sem_unlink(RBTRACE_SEM_NAME);
		}
	}

	if (rbtrace_globals.shm_fd != -1) {
		if ((rbtrace_globals.shm_base != MAP_FAILED) &&
		    (rbtrace_globals.shm_base != NULL)) {
			if (is_daemon) {
				munlock(rbtrace_globals.shm_base,
					rbtrace_globals.shm_size);
			}
			munmap(rbtrace_globals.shm_base,
			       rbtrace_globals.shm_size);
		}

		close(rbtrace_globals.shm_fd);
		if (is_daemon) {
			shm_unlink(RBTRACE_SHM_NAME);
		}
	}

	rbtrace_globals.sem_ptr = SEM_FAILED;
	rbtrace_globals.shm_fd = -1;
	rbtrace_globals.shm_base = MAP_FAILED;
	rbtrace_globals.inited = FALSE;
}

void rbtrace_exit(void)
{
	rbtrace_globals_cleanup(FALSE);
}

int rbtrace_init(void)
{
	int rc = 0;
	int shm_fd = -1;
	size_t shm_size = 0;
	char *shm_base = NULL;
	sem_t *sem_ptr = SEM_FAILED;

	if (rbtrace_globals.inited) {
		rc = -1;
		goto out;
	}

	shm_size = rbtrace_calc_shm_size();

	shm_fd = shm_open(RBTRACE_SHM_NAME, O_RDWR, 0666);
	if (shm_fd == -1) {
		rc = errno;
		dprintf("shm_open failed, error:%d\n", errno);
		goto out;
	}

	shm_base = mmap(NULL, shm_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, shm_fd, 0);
	if (shm_base == MAP_FAILED) {
		rc = errno;
		dprintf("mmap failed, error:%d\n", errno);
		goto mmap_fail;
	}

	sem_ptr = sem_open(RBTRACE_SEM_NAME, O_RDWR);
	if (sem_ptr == SEM_FAILED) {
		rc = errno;
		dprintf("sem_open failed, error:%d\n", errno);
		goto sem_open_fail;
	}

	rbtrace_globals_init(shm_fd, shm_base, shm_size, sem_ptr);
	return rc;

 sem_open_fail:
	munmap(shm_base, shm_size);
 mmap_fail:
	close(shm_fd);
 out:
	return rc;
}

