#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rbtracedef.h"
#include "rbtrace_private.h"

struct rbtrace_option {
	rbtrace_ring_t ring;
	char *file;
	uint64_t size;
	uint64_t tflags;
	bool_t wrap;
	bool_t zap;
} opts = {
	.ring = RBTRACE_RING_IO,
	.file = NULL,
	.size = 0,
	.tflags = 0,
	.wrap = FALSE,
	.zap = FALSE,
};

char *rbtrace_op_str[] = {
	"open",
	"close",
	"size",
	"wrap",
	"zap",
	"info",
};

static char *rbtrace_op_to_str(rbtrace_op_t op)
{
	if (op < RBTRACE_OP_MAX) {
		return rbtrace_op_str[op];
	}
	return "unknown";
}

static void usage(void);

int main(int argc, char *argv[])
{
	int rc = 0;
	int ch = 0;
	char *endptr = NULL;
	rbtrace_op_t op = RBTRACE_OP_MAX;
	bool_t rbtrace_inited = FALSE;
	bool_t do_open = FALSE;
	bool_t do_close = FALSE;
	bool_t do_size = FALSE;
	bool_t do_wrap = FALSE;
	bool_t do_zap = FALSE;
	bool_t do_info = FALSE;

	while ((ch = getopt(argc, argv, "r:o:cw:z:is:S:C:h")) != -1) {
		switch (ch) {
		case 'r':
			if (strcmp(optarg, "io") == 0) {
				opts.ring = RBTRACE_RING_IO;
			} else {
				fprintf(stderr, "Illegal trace ring ID!\n");
				goto out;
			}
			break;
		case 'o':
			opts.file = optarg;
			if (strlen(opts.file) > RBTRACE_MAX_PATH) {
				fprintf(stderr, "Path too long! Must be less than %d characters!\n",
					RBTRACE_MAX_PATH);
				goto out;
			}
			do_open = TRUE;
			break;
		case 'c':
			do_close = TRUE;
			break;
		case 'w':
			if (strcmp(optarg, "on") == 0) {
				opts.wrap = TRUE;
			} else if (strcmp(optarg, "off") == 0) {
				opts.wrap = FALSE;
			} else {
				fprintf(stderr, "Invalid option:%s\n", optarg);
				goto out;
			}
			do_wrap = TRUE;
			break;
		case 'z':
			if (strcmp(optarg, "on") == 0) {
				opts.zap = TRUE;
			} else if (strcmp(optarg, "off") == 0) {
				opts.zap = FALSE;
			} else {
				fprintf(stderr, "Invalid option:%s\n", optarg);
				goto out;
			}
			do_zap = TRUE;
			break;
		case 'i':
			do_info = TRUE;
			break;
		case 's':
			opts.size = strtoull(optarg, &endptr, 10);
			if ((endptr == optarg) || (*endptr != '\0')) {
				fprintf(stderr, "Invalid argment size!\n");
				goto out;
			}
			do_size = TRUE;
			break;
		case 'S':
			break;
		case 'C':
			break;
		case 'h':	// Fall through
		default:
			usage();
			goto out;
		}
	}

	rc = rbtrace_init();
	if (rc != 0) {
		fprintf(stderr, "rbtrace init failed, error:%d!\n", rc);
		goto out;
	}
	rbtrace_inited = TRUE;

	if (do_wrap) {
		op = RBTRACE_OP_WRAP;
		rc = rbtrace_ctrl(opts.ring, op, &opts.wrap);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_zap) {
		op = RBTRACE_OP_ZAP;
		rc = rbtrace_ctrl(opts.ring, op, &opts.zap);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_size) {
		op = RBTRACE_OP_SIZE;
		opts.size *= (1024*1024); // Transfer to bytes
		rc = rbtrace_ctrl(opts.ring, op, &opts.size);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_open) {
		op = RBTRACE_OP_OPEN;
		rc = rbtrace_ctrl(opts.ring, op, opts.file);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_close) {
		op = RBTRACE_OP_CLOSE;
		rc = rbtrace_ctrl(opts.ring, op, NULL);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_info) {
		op = RBTRACE_OP_INFO;
	}

 out:
	if (rc != 0) {
		fprintf(stderr, "op:%s failed, error:%d\n",
			rbtrace_op_to_str(op), rc);
	}
	if (rbtrace_inited) {
		rbtrace_exit();
	}
	return rc;
}

static void usage(void)
{
	printf("Usage: ./rbt <options>\n"
	       "       [-r <ring-id>]   Specify trace ring, io by default\n"
	       "       [-o <tracefile>] Open trace file\n"
	       "       [-c]             Flush and close trace file\n"
	       "       [-s <size-MB>]   Specify trace file size in MB\n"
	       "       [-w on|off]      Enable/disable wrap, exclusive with zap\n"
	       "       [-z on|off]      Enable/disable zap, exclusive with wrap\n"
	       "       [-S <trace-id>]  Set trace ID to be enabled\n"
	       "       [-C <trace-id>]  Clear trace ID to be disabled\n"
	       "       [-h]             Display this help message\n");
}
