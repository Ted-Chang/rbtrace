#ifndef __RBTRACE_PVT_H__
#define __RBTRACE_PVT_H__

#include <stdint.h>
#include <semaphore.h>
#include <assert.h>
#include "rbtrace.h"

#ifndef DBG_ASSERT
#define DBG_ASSERT	assert
#endif

#ifndef dprintf
#define dprintf		printf
#endif

#define RBTRACE_SHM_NAME	"/rbtracebuf"
#define RBTRACE_SEM_NAME	"/rbtrace"

struct rbtrace_info {
	char ri_file_path[RBTRACE_MAX_PATH];// trace file path
	size_t ri_data_size;	// number of bytes in a buffer write
	uint64_t ri_seek;	// offset to seek to before write
	uint64_t ri_flags;	// attribute flags for this ring
	uint64_t ri_tflags;	// traffic flags for this ring
	uint32_t ri_size;	// number of records in single ring
	rbtrace_ring_t ri_ring;	// ring ID
	volatile size_t ri_cir_off;// offset to active ring buffer
	volatile size_t ri_alt_off;// offset to inactive ring buffer
	volatile int ri_slot;	// current position in ring
	volatile int ri_flush;	// flushing
	volatile int ri_lost;	// number of records lost
	volatile int ri_refcnt;	// reference count
};

/* Flags for ri_flags in rbtrace_info */
#define RBTRACE_DO_DISK		(1 << 1)
#define RBTRACE_DO_OPEN		(1 << 2)
#define RBTRACE_DO_WRAP		(1 << 3)
#define RBTRACE_DO_CLOSE	(1 << 4)
#define RBTRACE_DO_FLUSH	(1 << 5)
#define RBTRACE_DO_ZAP		(1 << 6)

struct rbtrace_config {
	rbtrace_ring_t rc_ring;
	char *rc_name;
	char *rc_desc;
	uint64_t rc_flags;
	uint32_t rc_size;
};

struct rbtrace_global_data {
	bool_t inited;
	int shm_fd;		// shared memory fd
	size_t shm_size;
	char *shm_base;
	sem_t *sem_ptr;
	uint64_t *fsize_ptr;
	rbtrace_ring_t *ring_ptr;
	struct rbtrace_info *ri_ptr;
	struct rbtrace_record *rr_base;
};

extern struct rbtrace_global_data rbtrace_globals;

typedef enum rbtrace_op {
	RBTRACE_OP_OPEN = 0,
	RBTRACE_OP_CLOSE,
	RBTRACE_OP_SIZE,
	RBTRACE_OP_WRAP,
	RBTRACE_OP_ZAP,
	RBTRACE_OP_INFO,
	RBTRACE_OP_MAX,
} rbtrace_op_t;

typedef int (*rbtrace_op_handler)(struct rbtrace_info *ri, void *argp);

int rbtrace_ctrl(rbtrace_ring_t ring, rbtrace_op_t op,
		 void *argp);
size_t rbtrace_calc_shm_size(void);
void rbtrace_signal_thread(struct rbtrace_info *ri);
void rbtrace_globals_init(int fd, char *shm_base,
			  size_t shm_size,
			  sem_t *sem_ptr);
void rbtrace_globals_cleanup(bool_t is_daemon);
int rbtrace_daemon_init(void);
void rbtrace_daemon_exit(void);

#endif	/* __RBTRACE_PVT_H__ */
