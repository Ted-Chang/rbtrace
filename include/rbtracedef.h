#ifndef __RBTRACEDEF_H__
#define __RBTRACEDEF_H__

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#ifndef bool_t
typedef int bool_t;
#endif

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(_expr_)					\
	typedef char __junk_##__LINE__[(_expr_) == 0 ? -1 : 1];
#endif

struct rbtrace_record {
	struct timespec rr_timestamp;
	uint32_t rr_thread;
	uint16_t rr_cpuid;
	uint16_t rr_traceid;
	uint64_t rr_a0;
	uint64_t rr_a1;
	uint64_t rr_a2;
	uint64_t rr_a3;
};

#define RBTRACE_FHEADER_MAGIC	"RBTRACE"

#define RBTRACE_MAJOR	1
#define RBTRACE_MINOR	0

struct rbtrace_fheader {
	char magic[8];		// Magic number
	uint16_t major;		// Major release
	uint16_t minor;		// Minor release
	uint32_t ring;		// ring ID
	uint64_t wrap_pos;	// Seek for oldest entry
	uint64_t hdr_size;	// Size of header
	uint64_t nr_records;	// Number of records
	struct timespec timestamp;
	uint32_t name_off;	// Offset in file to ring name
	uint32_t desc_off;	// Offset in file to ring desc
};

#define RBTRACE_FHEADER_SIZE	512
union padded_rbtrace_fheader {
	struct rbtrace_fheader hdr;
	char pad[RBTRACE_FHEADER_SIZE];
};

#ifdef __cplusplus
}
#endif

#endif	/* __RBTRACEDEF_H__ */
