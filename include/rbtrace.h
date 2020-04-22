#ifndef __RBTRACE_H__
#define __RBTRACE_H__

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rbtrace_ring {
	RBTRACE_RING_IO = 0,
	RBTRACE_RING_MAX
} rbtrace_ring_t;

/* ring buffer trace ID
 */
#define RBT_LOST		0x0000
#define RBT_TRAFFIC_TEST	0x0001
#define RBT_LAST		(RBT_TRAFFIC_TEST+1)

/* Operation types, all types should be even
 */
#define RBT_NOOP		0x0000
#define RBT_READ		0x0002
#define RBT_WRITE		0x0004
#define RBT_PASSTHRU		0x0006

/* Operation states
 */
#define RBT_START		0x0000
#define RBT_DONE		0x0001

/* Composed operation definition */
#define RBT_TRAFFIC_READ_START	(RBT_READ|RBT_START)
#define RBT_TRAFFIC_READ_DONE	(RBT_READ|RBT_DONE)
#define RBT_TRAFFIC_WRITE_START	(RBT_WRITE|RBT_START)
#define RBT_TRAFFIC_WRITE_DONE	(RBT_WRITE|RBT_DONE)

#ifdef RBT_STR
const char *rbt_tid_str[] = {
	"LOST",
	"TEST",
};
const char *rbt_fmt_str[] = {
	"Lost %lld",
	"OFF %#16llX LEN %#6llx DEV %#4d OP %04x",
};

static uint64_t str_to_tflags(const char *str)
{
	char *pch;
	char *ptr;
	uint64_t tflags = 0;
	int i;

	ptr = strdup(str);
	if (ptr == NULL) {
		return 0;
	}

	pch = strtok(ptr, ",");
	while (pch != NULL) {
		for (i = RBT_TRAFFIC_TEST; i < RBT_LAST; i++) {
			if (strcmp(pch, rbt_tid_str[i]) == 0) {
				break;
			}
		}
		if (i >= RBT_LAST) {
			tflags = 0;
			goto out;
		}
		tflags |= (1 << i);
		pch = strtok(NULL, ",");
	}

 out:
	free(ptr);
	return tflags;
}

#define TFLAGS_ALL	(0xFFFFFFFFFFFFFFFFUL)
static char *tflags_to_str(uint64_t tflags)
{
	int i;
	int nchars;
	size_t bufsz;
	char *buf;
	static char _tflags_buf[512];

	buf = _tflags_buf;
	bufsz = sizeof(_tflags_buf);

	for (i = RBT_TRAFFIC_TEST;
	     i < sizeof(rbt_tid_str)/sizeof(rbt_tid_str[0]);
	     i++) {
		if (tflags & (1 << i)) {
			nchars = snprintf(buf, bufsz, "%s ",
					  rbt_tid_str[i]);
			if (nchars < 0) {
				break;
			}

			buf += nchars;
			bufsz -= nchars;
		}
	}

	return _tflags_buf;
}
#endif	/* RBT_STR */

extern int rbtrace(rbtrace_ring_t ring, uint8_t traceid, uint64_t a0,
		   uint64_t a1, uint64_t a2, uint64_t a3);
extern int rbtrace_traffic_enabled(rbtrace_ring_t ring, uint8_t traceid);
extern int rbtrace_init(void);
extern void rbtrace_exit(void);

#ifdef __cplusplus
}
#endif

#endif	/* __RBTRACE_H__ */
