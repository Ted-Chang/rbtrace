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

struct rbtrace_config rbt_cfgs[] = {
	{
		RBTRACE_RING_IO,
		"io",
		"I/O traffic",
		0,
		RBTRACE_IO_RING_SIZE,
	},
};

STATIC_ASSERT((sizeof(rbt_cfgs)/sizeof(rbt_cfgs[0])) == RBTRACE_RING_MAX);

struct rbtrace_global_data rbt_globals = {
	.inited = false,
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
	(*rbt_globals.ring_ptr) = ri->ri_ring;
	if (sem_post(rbt_globals.sem_ptr) == -1) {
		dprintf("ring:%d sem_post failed, error:%d\n",
			ri->ri_ring, errno);
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
			/* No buffer flush in progress, swap cir_off & alt_off,
			 * guarded by ri_flush and CMPXCHG
			 */
			size_t temp;
			temp = ri->ri_cir_off;
			ri->ri_cir_off = ri->ri_alt_off;
			ri->ri_alt_off = temp;

			__sync_lock_test_and_set(&ri->ri_slot, -1);

			/* Prior buffer flush (if any) has done,
			 * reset lost statistics
			 */
			lost = __sync_lock_test_and_set(&ri->ri_lost, 0);
			if (lost) {
				rbtrace(RBTRACE_RING_IO, RBT_LOST, lost, 0, 0, 0);
				dprintf("ring:%d lost %d records\n",
					ri->ri_ring, lost);
			}
			rbtrace_signal_thread(ri);
		} else {
			/* The last buffer flush is still in progress,
			 * the records in current buffer will be
			 * discarded
			 */
			__sync_lock_test_and_set(&ri->ri_slot, -1);

			/* Wake if missed or still processing prior
			 * flush to disk
			 */
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
			      (rbt_globals.rr_base + ri->ri_cir_off)) + slot;
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
		      (rbt_globals.rr_base + ri->ri_cir_off)) + slot;
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
		      (rbt_globals.rr_base + ri->ri_cir_off)) + slot;
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
	struct rbtrace_info *ri;
	struct rbtrace_record *rr;

	if ((ring >= RBTRACE_RING_MAX) ||
	    (NULL == rbt_globals.ri_ptr)) {
		return -1;
	}

	ri = rbt_globals.ri_ptr + ring;
	rr = ringwrap(ri);
	if (rr == NULL) {
		return -1;
	} else {
		rr->rr_traceid = traceid;
		rr->rr_a0 = a0;
		rr->rr_a1 = a1;
		rr->rr_a2 = a2;
		rr->rr_a3 = a3;
	}

	return 0;
}

inline int rbtrace_traffic_enabled(rbtrace_ring_t ring, uint16_t traceid)
{
	if ((ring >= RBTRACE_RING_MAX) ||
	    (traceid >= RBT_LAST) ||
	    (rbt_globals.ri_ptr == NULL)) {
		return false;
	}

	return rbt_globals.ri_ptr[ring].ri_tflags & (1 << traceid);
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

	size += sizeof(*rbt_globals.fsize_ptr);
	size += sizeof(*rbt_globals.ring_ptr);

	for (i = RBTRACE_RING_IO; i < RBTRACE_RING_MAX; i++) {
		size += rbtrace_calc_ring_size(&rbt_cfgs[i]);
	}

	size += (RBTRACE_RING_MAX * sizeof(*rbt_globals.ri_ptr));

	return size;
}

void rbtrace_globals_init(int shm_fd, char *shm_base,
			  size_t shm_size,
			  sem_t *sem_ptr)
{
	size_t offset = 0;

	rbt_globals.shm_fd = shm_fd;
	rbt_globals.sem_ptr = sem_ptr;
	rbt_globals.shm_base = shm_base;
	rbt_globals.shm_size = shm_size;

	rbt_globals.fsize_ptr = (uint64_t *)(shm_base + offset);
	offset += sizeof(uint64_t);
	rbt_globals.ring_ptr = (rbtrace_ring_t *)(shm_base + offset);
	offset += sizeof(rbtrace_ring_t);
	rbt_globals.ri_ptr = (struct rbtrace_info *)(shm_base + offset);
	offset += sizeof(struct rbtrace_info) * RBTRACE_RING_MAX;
	rbt_globals.rr_base = (struct rbtrace_record *)(shm_base + offset);

	rbt_globals.inited = true;
}

void rbtrace_globals_cleanup(bool is_daemon)
{
	if ((rbt_globals.sem_ptr != SEM_FAILED) &&
	    (rbt_globals.sem_ptr != NULL)) {
		sem_close(rbt_globals.sem_ptr);
		if (is_daemon) {
			sem_unlink(RBTRACE_SEM_NAME);
		}
	}

	if (rbt_globals.shm_fd != -1) {
		if ((rbt_globals.shm_base != MAP_FAILED) &&
		    (rbt_globals.shm_base != NULL)) {
			if (is_daemon) {
				munlock(rbt_globals.shm_base,
					rbt_globals.shm_size);
			}
			munmap(rbt_globals.shm_base,
			       rbt_globals.shm_size);
		}

		close(rbt_globals.shm_fd);
		if (is_daemon) {
			shm_unlink(RBTRACE_SHM_NAME);
		}
	}

	rbt_globals.sem_ptr = SEM_FAILED;
	rbt_globals.shm_fd = -1;
	rbt_globals.shm_base = MAP_FAILED;
	rbt_globals.inited = false;
}

void rbtrace_exit(void)
{
	rbtrace_globals_cleanup(false);
}

int rbtrace_init(void)
{
	int rc = 0;
	int shm_fd = -1;
	size_t shm_size = 0;
	char *shm_base = NULL;
	sem_t *sem_ptr = SEM_FAILED;

	if (rbt_globals.inited) {
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

