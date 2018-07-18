#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "rbtrace.h"
#include "rbtracedef.h"

int main(int argc, char *argv[])
{
	int rc = 0;
	bool_t rbtrace_inited = FALSE;
	uint64_t x = 0;

	rc = rbtrace_init();
	if (rc != 0) {
		fprintf(stderr, "rbtrace init failed, error:%d!\n", rc);
		goto out;
	}

	while (TRUE) {
		rc = rbtrace(RBTRACE_RING_IO, RBT_TRAFFIC_TEST, x, x, x, x);
		if (rc != 0) {
			fprintf(stderr, "rbtrace %lx failed!\n", x);
		} else {
			x++;
		}
	}

 out:
	if (rbtrace_inited) {
		rbtrace_exit();
	}
	return rc;
}
