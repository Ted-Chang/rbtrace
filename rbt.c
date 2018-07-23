#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rbtracedef.h"
#include "rbtrace_private.h"

#define ONE_MB	(1024UL * 1024UL)

struct rbtrace_option {
	rbtrace_ring_t ring;
	char *file;
	uint64_t size;
	uint64_t stflags;
	uint64_t ctflags;
	bool_t wrap;
	bool_t zap;
} opts = {
	.ring = RBTRACE_RING_IO,
	.file = NULL,
	.size = 0,
	.stflags = 0,
	.ctflags = 0,
	.wrap = FALSE,
	.zap = FALSE,
};

char *rbtrace_op_str[] = {
	"open",
	"close",
	"size",
	"wrap",
	"traffic-flags",
	"zap",
	"info",
};

STATIC_ASSERT(sizeof(rbtrace_op_str)/sizeof(rbtrace_op_str[0]) == RBTRACE_OP_MAX);

static char *rbtrace_op_to_str(rbtrace_op_t op)
{
	if (op < RBTRACE_OP_MAX) {
		return rbtrace_op_str[op];
	}
	return "unknown";
}

static char *flags_to_str(uint64_t flags)
{
	return NULL;
}

static uint64_t str_to_tflags(char *str)
{
	return 0;
}

static char *tflags_to_str(uint64_t tflags)
{
	return NULL;
}

static void dump_rbtrace_info(struct rbtrace_op_info_arg *info_arg)
{
	printf("flags    : %s\n", flags_to_str(info_arg->flags));
	printf("tflags   : %s\n", tflags_to_str(info_arg->tflags));
	printf("file size: %ld\n", info_arg->file_size / ONE_MB);
	printf("file path: %s\n", info_arg->file_path);
}

static void usage(void);

int main(int argc, char *argv[])
{
	int rc = 0;
	int ch = 0;
	char *endptr = NULL;
	rbtrace_op_t op = RBTRACE_OP_MAX;
	struct rbtrace_op_tflags_arg tflags_arg;
	struct rbtrace_op_info_arg info_arg;
	bool_t rbtrace_inited = FALSE;
	bool_t do_open = FALSE;
	bool_t do_close = FALSE;
	bool_t do_size = FALSE;
	bool_t do_wrap = FALSE;
	bool_t do_zap = FALSE;
	bool_t do_info = FALSE;
	bool_t do_set_tflags = FALSE;
	bool_t do_clear_tflags = FALSE;

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
			opts.stflags = str_to_tflags(optarg);
			if (opts.stflags == 0) {
				goto out;
			}
			do_set_tflags = TRUE;
			break;
		case 'C':
			opts.ctflags = str_to_tflags(optarg);
			if (opts.ctflags == 0) {
				goto out;
			}
			do_clear_tflags = TRUE;
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
		opts.size *= ONE_MB; // Transfer to bytes
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
	if (do_set_tflags) {
		tflags_arg.set = TRUE;
		tflags_arg.tflags = opts.stflags;
		op = RBTRACE_OP_TFLAGS;
		rc = rbtrace_ctrl(opts.ring, op, &tflags_arg);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_clear_tflags) {
		tflags_arg.set = FALSE;
		tflags_arg.tflags = opts.ctflags;
		op = RBTRACE_OP_TFLAGS;
		rc = rbtrace_ctrl(opts.ring, op, &tflags_arg);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_info) {
		op = RBTRACE_OP_INFO;
		rc = rbtrace_ctrl(opts.ring, op, &info_arg);
		if (rc != 0) {
			goto out;
		} else {
			dump_rbtrace_info(&info_arg);
		}
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
