#ifndef __RBTRACEDEF_H__
#define __RBTRACEDEF_H__

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(_expr_)					\
	typedef char __junk_##__LINE__[(_expr_) == 0 ? -1 : 1];
#endif

/* Max length of trace file path */
#define RBTRACE_MAX_PATH	(256)

/* Max length of trace name and description */
#define RBTRACE_MAX_NAME	(64)
#define RBTRACE_MAX_DESC	(128)

/* Format of a trace record */
struct rbtrace_record {
	struct timespec rr_timestamp;
	uint32_t rr_thread;
	uint32_t rr_cpuid:8;
	uint32_t rr_traceid:24;
	uint64_t rr_a0;
	uint64_t rr_a1;
	uint64_t rr_a2;
	uint64_t rr_a3;
};

#define RBTRACE_FHEADER_MAGIC	"RBTRACE"

#define RBTRACE_MAJOR	1
#define RBTRACE_MINOR	0

/* Format of a trace file header */
struct rbtrace_fheader {
	char magic[8];		// Magic number
	uint16_t major;		// Major release
	uint16_t minor;		// Minor release
	uint32_t ring;		// ring ID
	uint64_t wrap_pos;	// Seek for oldest entry
	uint64_t hdr_size;	// Size of header
	uint64_t nr_records;	// Number of records
	struct timespec timestamp;// Timestamp when the file was created
	uint64_t gmtoff;	// GMT time offset in seconds
	uint32_t tz_off;        // Offset in file to time zone
	uint32_t name_off;	// Offset in file to ring name
	uint32_t desc_off;	// Offset in file to ring desc
};

#define RBTRACE_FHEADER_SIZE	512

union padded_rbtrace_fheader {
	struct rbtrace_fheader hdr;
	char pad[RBTRACE_FHEADER_SIZE];
};

#ifdef RBT_FMT_STR
const char *rbt_fmt_str[] = {
	"NULL trace entry\n",
	"Lost %lld\n",
	"TEST: OFF %#16llX LEN %#6llx DEV %#4lld OP %04llx\n",
};
#endif	/* RBT_FMT_STR */

#ifdef __cplusplus
}
#endif

#endif	/* __RBTRACEDEF_H__ */
