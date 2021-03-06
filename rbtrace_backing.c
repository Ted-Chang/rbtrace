#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include "rbtrace.h"
#include "rbtracedef.h"
#include "rbtrace_private.h"

STATIC_ASSERT(sizeof(struct rbtrace_fheader) < RBTRACE_FHEADER_SIZE);

#define RBTRACE_THREAD_NAME		"rbtrace-flush"
#define RBTRACE_THREAD_WAIT_SECS	(5)
#define RBTRACE_FLUSH_WAIT_USECS	(5000)

#define RBTRACE_DFT_FILE_SIZE		(2048ULL*1024ULL*1024ULL)

struct rbtrace_thread_data {
	pthread_t thread;
	sem_t sem;
	volatile bool active;
	volatile bool terminate;
} rbt_thread = {
	.active = false,
	.terminate = false,
};

struct ring_file_data {
	int fd;
	uint64_t seek;	// offset to seek before write
};

extern struct ring_config ring_cfgs[];

struct ring_file_data rbt_rfd[RBTRACE_RING_MAX];
union padded_rbtrace_fheader rbt_hdrs[RBTRACE_RING_MAX];
uint64_t total_buffers = 0;

static ssize_t safe_pwrite(int fd, const void *buf,
			   size_t count, off_t offset)
{
	while (count > 0) {
		ssize_t r = pwrite(fd, buf, count, offset);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -errno;
		}
		count -= r;
		buf = (char *)buf + r;
		offset += r;
	}

	return 0;
}

static void rbtrace_format_header(rbtrace_ring_t ring,
				  struct timespec ts,
				  struct tm *tm)
{
	struct rbtrace_fheader *rf;
	struct ring_config *rc;
	char *ptr;

	rf = &rbt_hdrs[ring].hdr;
	rc = &ring_cfgs[ring];
	memset(&rbt_hdrs[ring], 0, sizeof(rbt_hdrs[ring]));
	strcpy(rf->magic, RBTRACE_FHEADER_MAGIC);
	rf->major = RBTRACE_MAJOR;
	rf->minor = RBTRACE_MINOR;
	rf->ring = ring;
	rf->wrap_pos = 0;
	rf->hdr_size = sizeof(rbt_hdrs[ring]);
	rf->nr_records = rc->rc_size;
	rf->timestamp = ts;
	rf->gmtoff = tm->tm_gmtoff;

	ptr = ((char *)rf) + sizeof(*rf);
	rf->tz_off = (uint32_t)(ptr - (char *)rf);
	strcpy(ptr, tm->tm_zone);
	ptr += (strlen(tm->tm_zone) + 1);
	rf->name_off = (uint32_t)(ptr - (char *)rf);
	strcpy(ptr, rc->rc_name);
	ptr += (strlen(rc->rc_name) + 1);
	rf->desc_off = (uint32_t)(ptr - (char *)rf);
	strcpy(ptr, rc->rc_desc);
}

static void rbtrace_write_header(rbtrace_ring_t ring)
{
	ssize_t ret = 0;
	struct ring_info *ri;
	struct ring_file_data *rfd;
	char path[2048];
	struct timespec ts;
	struct tm *gm = NULL;

	ri = &rbt_globals.ri_ptr[ring];
	rfd = &rbt_rfd[ring];

	if (rfd->fd != -1) {
		dprintf("ring:%d file already open!\n", ring);
		goto out;
	}

	clock_gettime(CLOCK_REALTIME, &ts);
	gm = localtime(&ts.tv_sec);
	assert(gm != NULL);

	if (ri->ri_flags & RBTRACE_DO_ZAP) {
		snprintf(path, sizeof(path),
			 "%s.%02d-%02d_%02d-%02d-%02d_%03ld",
			 ri->ri_file_path, gm->tm_mon + 1,
			 gm->tm_mday, gm->tm_hour, gm->tm_min,
			 gm->tm_sec, ts.tv_nsec / 1000000);
	} else {
		strcpy(path, ri->ri_file_path);
	}

	rfd->fd = open(path, O_RDWR|O_CREAT, 0666);
	if (rfd->fd == -1) {
		dprintf("ring:%d open %s failed, error:%d\n",
			ring, path, errno);
		goto out;
	}

	dprintf("ring:%d file %s open\n", ring, path);

	/* Format the trace header */
	rbtrace_format_header(ring, ts, gm);

	/* Write trace header to trace file */
	ret = safe_pwrite(rfd->fd, &rbt_hdrs[ring],
			  sizeof(rbt_hdrs[ring]), 0);
	if (ret) {
		dprintf("ring:%d pwrite header failed, error:%zd\n",
			ring, ret);
		goto out;
	}

	rfd->seek = sizeof(rbt_hdrs[ring]);
	/* Set flag to indicate file is open for business */
	ri->ri_flags |= RBTRACE_DO_DISK;

	/* Clear open flag since we have open it */
	ri->ri_flags &= ~RBTRACE_DO_OPEN;
	return;

 out:
	ri->ri_flags &= ~RBTRACE_DO_OPEN;
	rfd->seek = 0;
	if (rfd->fd != -1) {
		close(rfd->fd);
		rfd->fd = -1;
	}
}

static void rbtrace_write_data(rbtrace_ring_t ring,
			       bool do_flush)
{
	struct ring_config *cfg = NULL;
	struct ring_info *ri = NULL;
	struct ring_file_data *rfd = NULL;
	union padded_rbtrace_fheader *prf = NULL;
	char *buf = NULL;
	ssize_t buf_size = 0;
	ssize_t ret = 0;
	int flush = 0;
	int lost = 0;
	bool update_hdr = false;

	rfd = &rbt_rfd[ring];
	if (rfd->fd == -1) {
		dprintf("ring:%d invalid file descriptor!\n", ring);
		goto end;
	}

	cfg = &ring_cfgs[ring];
	ri = &rbt_globals.ri_ptr[ring];
	prf = &rbt_hdrs[ring];

	if (do_flush) {
		buf = (char *)(rbt_globals.re_base + ri->ri_cir_off);
		buf_size = (ri->ri_slot + 1) * sizeof(struct rbtrace_entry);
	} else {
		buf = (char *)(rbt_globals.re_base + ri->ri_alt_off);
		buf_size = cfg->rc_data_size;
	}

	/* Update file header if this ring is wrapped */
	if (prf->hdr.wrap_pos && (ri->ri_flags & RBTRACE_DO_WRAP)) {
		rfd->seek = prf->hdr.wrap_pos;
		prf->hdr.wrap_pos += buf_size;
		update_hdr = true;
	}

	/* Write buffer content to file */
	ret = safe_pwrite(rfd->fd, buf, buf_size, rfd->seek);
	if (ret) {
		dprintf("ring:%d write trace failed, error:%zd\n",
			ring, ret);
		goto end;
	} else {
		/* Clear the buffer to avoid poison data */
		memset(buf, 0, buf_size);
	}

	rfd->seek += buf_size;

	/* Close or wrap the file if buffer limit reached */
	if (rfd->seek >= *rbt_globals.fsize_ptr) {
		if (ri->ri_flags & RBTRACE_DO_CLOSE) {
			/* Flush will be done in thread_fn */
			dprintf("ring:%d user specified close.\n", ring);
		} else if (ri->ri_flags & RBTRACE_DO_WRAP) {
			/* Reset wrap position */
			prf->hdr.wrap_pos = sizeof(*prf);
			update_hdr = true;
		} else if (ri->ri_flags & RBTRACE_DO_ZAP) {
			/* Close current and open a new trace file */
			close(rfd->fd);
			rfd->fd = -1;
			rbtrace_write_header(ring);
		} else {
			/* Close file and stop tracing */
			close(rfd->fd);
			rfd->fd = -1;
			ri->ri_flags &= ~RBTRACE_DO_DISK;
		}
	}

	if (update_hdr) {
		ret = safe_pwrite(rfd->fd, prf, sizeof(*prf), 0);
		if (ret) {
			dprintf("ring:%d update hdr failed, error:%zd\n",
				ring, ret);
		}
	}

 end:
	lost = __sync_lock_test_and_set(&ri->ri_lost, 0);
	flush = __sync_lock_test_and_set(&ri->ri_flush, 0);

	if ((flush > 1) || lost) {
		if (flush > 1) {
			lost += ((flush - 1) * cfg->rc_size);
		}
		dprintf("ring:%d trace buffer:%lx lost %d records\n",
			ring, ++total_buffers, lost);
		rbtrace(RBTRACE_RING_IO, RBT_LOST, lost, 0, 0, 0);
	} else {
		++total_buffers;
	}
}

static void *rbtrace_thread_fn(void *arg)
{
	int rc = 0;
	rbtrace_ring_t ring;
	struct timespec wait_ts;
	struct ring_info *ri = NULL;
	struct ring_file_data *rfd = NULL;
	struct rbtrace_thread_data *thread = NULL;

	thread = (struct rbtrace_thread_data *)arg;
	thread->active = true;
	sem_post(&thread->sem);

	while (!thread->terminate) {
		clock_gettime(CLOCK_REALTIME, &wait_ts);
		wait_ts.tv_sec += RBTRACE_THREAD_WAIT_SECS;
		rc = sem_timedwait(rbt_globals.sem_ptr, &wait_ts);
		if ((rc == -1) && (errno != ETIMEDOUT)) {
			break;
		}

		ring = *(rbt_globals.ring_ptr);
		if (ring >= RBTRACE_RING_MAX) {
			dprintf("ring:%d invalid ring number!\n", ring);
			continue;
		}

		ri = &rbt_globals.ri_ptr[ring];
		rfd = &rbt_rfd[ring];

		/* We are about to closing the trace file? */
		if (ri->ri_flags & RBTRACE_DO_CLOSE) {
			/* Flush inactive buffer */
			if (ri->ri_flush) {
				rbtrace_write_data(ring, false);
			}

			/* We were asked to flush all trace records */
			if (ri->ri_flags & RBTRACE_DO_FLUSH) {
				rbtrace_write_data(ring, true);
				ri->ri_flags &= ~RBTRACE_DO_FLUSH;
			}

			/* Close file descriptor */
			if (rfd->fd != -1) {
				//fsync(rbt_fds[ring]);
				close(rfd->fd);
				rfd->fd = -1;
				dprintf("ring:%d file %s closed!\n",
					ring, ri->ri_file_path);
				memset(ri->ri_file_path, 0,
				       sizeof(ri->ri_file_path));
			}

			ri->ri_flags &= ~RBTRACE_DO_CLOSE;
			rfd->seek = 0;
		}
		/* Open a new file? */
		else if (ri->ri_flags & RBTRACE_DO_OPEN) {
			rbtrace_write_header(ring);
		}
		/* Normal write or flush */
		else if (ri->ri_flags & RBTRACE_DO_DISK) {
			if (ri->ri_flush) {
				rbtrace_write_data(ring, false);
			}
			if (ri->ri_flags & RBTRACE_DO_FLUSH) {
				rbtrace_write_data(ring, true);
				ri->ri_flags &= ~RBTRACE_DO_FLUSH;
			}
		}
	}

	return NULL;
}

static size_t rbtrace_init_trace_info(struct ring_config *cfg,
				      struct ring_info *ri,
				      size_t offset)
{
	ri->ri_ring = cfg->rc_ring;
	ri->ri_flags = cfg->rc_flags;
	ri->ri_tflags = 0;
	ri->ri_cir_off = offset;
	ri->ri_alt_off = ri->ri_cir_off + cfg->rc_size;

	return cfg->rc_size * 2;// all rings are double buffered
}

int rbtrace_daemon_init(void)
{
	int rc = 0;
	int shm_fd = -1;
	size_t shm_size = 0;
	size_t off = 0;
	char *shm_base = NULL;
	sem_t *sem_ptr = SEM_FAILED;
	int i;

	/* Cleanup garbage of previous run */
	shm_unlink(RBTRACE_SHM_NAME);
	sem_unlink(RBTRACE_SEM_NAME);

	shm_size = rbtrace_calc_shm_size();

	/* Create shared memory for ring buffer */
	shm_fd = shm_open(RBTRACE_SHM_NAME,
			  O_RDWR|O_CREAT|O_EXCL, 0666);
	if (shm_fd == -1) {
		rc = errno;
		goto out;
	}
	rc = ftruncate(shm_fd, shm_size);
	if (rc == -1) {
		rc = errno;
		goto ftruncate_fail;
	}
	shm_base = mmap(NULL, shm_size, PROT_READ|PROT_WRITE,
			MAP_SHARED, shm_fd, 0);
	if (shm_base == MAP_FAILED) {
		rc = errno;
		goto mmap_fail;
	}
	rc = mlock(shm_base, shm_size);
	if (rc == -1) {
		/* Failed to lock memory, but it's non-fatal error */
		dprintf("mlock failed, error:%d\n", errno);
	}
	memset(shm_base, 0, shm_size);

	/* Create semaphore for IPC */
	sem_ptr = sem_open(RBTRACE_SEM_NAME,
			   O_RDWR|O_CREAT|O_EXCL,
			   0666, 0);
	if (sem_ptr == SEM_FAILED) {
		rc = errno;
		goto sem_open_fail;
	}

	/* Dump shared memory region if cored */
	update_coredump_filter();

	/* Initialize global pointers */
	rbtrace_globals_init(shm_fd, shm_base, shm_size, sem_ptr);
	(*rbt_globals.fsize_ptr) = RBTRACE_DFT_FILE_SIZE;

	/* Initialize each trace info */
	for (i = RBTRACE_RING_IO, off = 0; i < RBTRACE_RING_MAX; i++) {
		off += rbtrace_init_trace_info(&ring_cfgs[i],
					       &rbt_globals.ri_ptr[i],
					       off);
		rbt_rfd[i].fd = -1;
		rbt_rfd[i].seek = 0;
	}

	sem_init(&rbt_thread.sem, 0, 0);
	rc = pthread_create(&rbt_thread.thread, NULL,
			    rbtrace_thread_fn, &rbt_thread);
	if (rc != 0) {
		goto pthread_fail;
	}
	rc = pthread_setname_np(rbt_thread.thread,
				RBTRACE_THREAD_NAME);
	if (rc != 0) {
		/* Non-fatal error, go on working */
		dprintf("set rbt thread name failed\n");
	}

	sem_wait(&rbt_thread.sem);
	sem_destroy(&rbt_thread.sem);

	return 0;

 pthread_fail:
	rbtrace_globals_cleanup(true);
	goto out;
 sem_open_fail:
	munmap(shm_base, shm_size);
 mmap_fail:
 ftruncate_fail:
	close(shm_fd);
	shm_unlink(RBTRACE_SHM_NAME);
 out:
	return rc;
}

void rbtrace_daemon_exit(void)
{
	int i;
	struct ring_file_data *rfd;

	/* Terminate rbtrace thread */
	rbt_thread.terminate = true;

	if (rbt_globals.sem_ptr != SEM_FAILED) {
		sem_post(rbt_globals.sem_ptr);
	}

	if (rbt_thread.active) {
		pthread_join(rbt_thread.thread, NULL);
	}

	/* Close all file descriptors */
	for (i = 0; i < RBTRACE_RING_MAX; i++) {
		rfd = &rbt_rfd[i];
		if (rfd->fd != -1) {
			close(rfd->fd);
			rfd->fd = -1;
		}
	}

	/* Cleanup global data */
	rbtrace_globals_cleanup(true);
}

static int rbtrace_ctrl_open(struct ring_info *ri, void *argp)
{
	int rc = 0;
	char *path = NULL;

	if ((ri->ri_flags & (RBTRACE_DO_OPEN|RBTRACE_DO_DISK)) ||
	    (argp == NULL)) {
		rc = -1;
		goto out;
	}

	path = (char *)argp;
	if (strlen(path) >= RBTRACE_MAX_PATH) {
		rc = -1;
		goto out;
	}

	strcpy(ri->ri_file_path, path);

	ri->ri_slot = -1;
	ri->ri_flush = 0;
	ri->ri_lost = 0;

	ri->ri_flags |= RBTRACE_DO_OPEN;

	rbtrace_signal_thread(ri);

 out:
	return rc;
}

static int rbtrace_ctrl_close(struct ring_info *ri, void *argp)
{
	int rc = -1;

	if (ri->ri_flags & RBTRACE_DO_DISK) {
		/* Stop any writing to disk */
		ri->ri_flags &= ~RBTRACE_DO_DISK;
		ri->ri_flags |= (RBTRACE_DO_FLUSH|RBTRACE_DO_CLOSE);

		rbtrace_signal_thread(ri);
		rc = 0;
	}

	return rc;
}

static int rbtrace_ctrl_size(struct ring_info *ri, void *argp)
{
	int rc = -1;

	if (argp != NULL) {
		uint64_t size = *((uint64_t *)argp);
		(*rbt_globals.fsize_ptr) = size;
		rc = 0;
	}

	return rc;
}

static int rbtrace_ctrl_wrap(struct ring_info *ri, void *argp)
{
	int rc = -1;

	if (!(ri->ri_flags & RBTRACE_DO_ZAP) &&	(argp != NULL)) {
		bool enable = *((bool *)argp);
		if (enable) {
			ri->ri_flags |= RBTRACE_DO_WRAP;
		} else {
			ri->ri_flags &= ~RBTRACE_DO_WRAP;
		}
		rc = 0;
	}

	return rc;
}

static int rbtrace_ctrl_zap(struct ring_info *ri, void *argp)
{
	int rc = -1;

	if (!(ri->ri_flags & RBTRACE_DO_WRAP) && (argp != NULL)) {
		bool enable = *((bool *)argp);
		if (enable) {
			ri->ri_flags |= RBTRACE_DO_ZAP;
		} else {
			ri->ri_flags &= ~RBTRACE_DO_ZAP;
		}
		rc = 0;
	}

	return rc;
}

static int rbtrace_ctrl_tflags(struct ring_info *ri, void *argp)
{
	int rc = -1;
	struct rbtrace_op_tflags_arg *tflags_arg;

	if (argp != NULL) {
		tflags_arg = (struct rbtrace_op_tflags_arg *)argp;
		if (tflags_arg->set) {
			ri->ri_tflags |= tflags_arg->tflags;
		} else {
			ri->ri_tflags &= ~tflags_arg->tflags;
		}
		rc = 0;
	}

	return rc;
}

static int rbtrace_ctrl_info(struct ring_info *ri, void *argp)
{
	int rc = -1;
	struct rbtrace_op_info_arg *info_arg;

	if (argp != NULL) {
		info_arg = (struct rbtrace_op_info_arg *)argp;
		info_arg->flags = ri->ri_flags;
		info_arg->tflags = ri->ri_tflags;
		info_arg->file_size = *(rbt_globals.fsize_ptr);
		info_arg->trace_entry_size = sizeof(struct rbtrace_entry);
		strcpy(info_arg->file_path, ri->ri_file_path);

		strncpy(info_arg->ring_name, ring_cfgs[ri->ri_ring].rc_name,
			sizeof(info_arg->ring_name) - 1);
		strncpy(info_arg->ring_desc, ring_cfgs[ri->ri_ring].rc_desc,
			sizeof(info_arg->ring_desc) - 1);
		rc = 0;
	}

	return rc;
}

rbtrace_op_handler rbt_ops[] = {
	rbtrace_ctrl_open,
	rbtrace_ctrl_close,
	rbtrace_ctrl_size,
	rbtrace_ctrl_wrap,
	rbtrace_ctrl_zap,
	rbtrace_ctrl_tflags,
	rbtrace_ctrl_info,
};

STATIC_ASSERT(sizeof(rbt_ops)/sizeof(rbt_ops[0]) == RBTRACE_OP_MAX);

int rbtrace_ctrl(rbtrace_ring_t ring, rbtrace_op_t op, void *argp)
{
	int rc = 0;
	struct ring_info *ri;

	if ((ring >= RBTRACE_RING_MAX) || (op >= RBTRACE_OP_MAX)) {
		dprintf("invalid parameter, ring:%d, op:%d\n", ring, op);
		goto out;
	}

	ri = &rbt_globals.ri_ptr[ring];
	if (rbt_ops[op]) {
		rc = rbt_ops[op](ri, argp);
	} else {
		dprintf("op:%d not supported\n", op);
	}

 out:
	return rc;
}

