#ifndef __RBTRACE_H__
#define __RBTRACE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rbtrace_ring {
	RBTRACE_RING_IO = 0,
	RBTRACE_RING_MAX
} rbtrace_ring_t;

/* ring buffer trace ID
 */
#define RBT_NULL		0x0000
#define RBT_LOST		0x0001
#define RBT_TRAFFIC_TEST	0x0002
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

extern int rbtrace(rbtrace_ring_t ring, uint16_t traceid, uint64_t a0,
		   uint64_t a1, uint64_t a2, uint64_t a3);
extern inline int rbtrace_traffic_enabled(rbtrace_ring_t ring,
					  uint16_t traceid);
extern int rbtrace_init(void);
extern void rbtrace_exit(void);

#ifdef __cplusplus
}
#endif

#endif	/* __RBTRACE_H__ */
