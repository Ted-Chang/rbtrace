#ifndef __RBTRACE_PRIVATE_H__
#define __RBTRACE_PRIVATE_H__

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>
#include <assert.h>
#include "rbtrace.h"
#include "rbtracedef.h"

#ifndef dprintf
#define dprintf		printf
#endif

#define RBTRACE_SHM_NAME	"/rbtracebuf"
#define RBTRACE_SEM_NAME	"/rbtrace"

struct ring_info {
	char ri_file_path[RBTRACE_MAX_PATH];// trace file path
	rbtrace_ring_t ri_ring;	// ring ID
	volatile uint64_t ri_flags;// attribute flags for this ring
	volatile uint64_t ri_tflags;// traffic flags for this ring
	volatile int ri_cir_off;// offset in trace records to active ring buffer
	volatile int ri_alt_off;// offset in trace records to inactive ring buffer
	volatile int ri_slot;	// current position in ring
	volatile int ri_flush;	// flushing
	volatile int ri_lost;	// number of records lost
};

/* Flags for ri_flags in ring_info */
#define RBTRACE_DO_DISK		(1 << 1)
#define RBTRACE_DO_OPEN		(1 << 2)
#define RBTRACE_DO_WRAP		(1 << 3)
#define RBTRACE_DO_ZAP		(1 << 4)
#define RBTRACE_DO_CLOSE	(1 << 5)
#define RBTRACE_DO_FLUSH	(1 << 6)

struct ring_config {
	rbtrace_ring_t rc_ring;
	char *rc_name;
	char *rc_desc;
	uint64_t rc_flags;
	uint32_t rc_size;	// number of trace records in a buffer
	uint32_t rc_data_size;	// number of bytes in a buffer write
};

struct rbtrace_global_data {
	bool inited;
	int shm_fd;		// shared memory fd
	size_t shm_size;
	char *shm_base;
	sem_t *sem_ptr;
	uint64_t *fsize_ptr;
	rbtrace_ring_t *ring_ptr;
	struct ring_info *ri_ptr;
	struct rbtrace_entry *re_base;
};

extern struct rbtrace_global_data rbt_globals;

typedef enum rbtrace_op {
	RBTRACE_OP_OPEN = 0,
	RBTRACE_OP_CLOSE,
	RBTRACE_OP_SIZE,
	RBTRACE_OP_WRAP,
	RBTRACE_OP_ZAP,
	RBTRACE_OP_TFLAGS,
	RBTRACE_OP_INFO,
	RBTRACE_OP_MAX,
} rbtrace_op_t;

struct rbtrace_op_tflags_arg {
	bool set;
	uint64_t tflags;
};

struct rbtrace_op_info_arg {
	char ring_name[RBTRACE_MAX_NAME];
	char ring_desc[RBTRACE_MAX_DESC];
	char file_path[RBTRACE_MAX_PATH];
	uint64_t flags;
	uint64_t tflags;
	uint64_t file_size;
	uint32_t trace_entry_size;
};

typedef int (*rbtrace_op_handler)(struct ring_info *ri, void *argp);

int rbtrace_ctrl(rbtrace_ring_t ring, rbtrace_op_t op, void *argp);
size_t rbtrace_calc_shm_size(void);
void rbtrace_signal_thread(struct ring_info *ri);
void rbtrace_globals_init(int fd, char *shm_base,
			  size_t shm_size,
			  sem_t *sem_ptr);
void rbtrace_globals_cleanup(bool do_unlink);
int rbtrace_daemon_init(void);
void rbtrace_daemon_exit(void);

#endif	/* __RBTRACE_PRIVATE_H__ */
