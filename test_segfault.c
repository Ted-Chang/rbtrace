#include <stdio.h>
#include <unistd.h>
#include "rbtrace.h"

int main(int argc, char *argv[])
{
	int rc = 0;
	int x = 0;
	pid_t pid = getpid();
	int *ptr = NULL;

	rc = rbtrace_init();
	if (rc != 0) {
		fprintf(stderr, "rbtrace init failed, error:%d!\n", rc);
		goto out;
	}

	while (x++ < 100000) {
		rbtrace(RBTRACE_RING_IO, RBT_TRAFFIC_TEST, pid, x, x, x);
	}

	/* Trigger segment fault */
	*ptr = 0;

 out:
	return rc;
}
