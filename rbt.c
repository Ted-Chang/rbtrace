#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define RBT_STR
#include "rbtrace_private.h"
#include "version.h"

#define ONE_MB		(1024UL * 1024UL)

struct rbtrace_option {
	rbtrace_ring_t ring;
	char *file;
	uint64_t size;
	uint64_t stflags;
	uint64_t ctflags;
	bool wrap;
	bool zap;
} opts = {
	.ring = RBTRACE_RING_IO,
	.file = NULL,
	.size = 0,
	.stflags = 0,
	.ctflags = 0,
	.wrap = false,
	.zap = false,
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

struct flag_name {
	uint64_t flag;
	char *name;
} flg_names[] = {
	{
		.flag = RBTRACE_DO_DISK,
		.name = "DISK",
	},
	{
		.flag = RBTRACE_DO_OPEN,
		.name = "OPEN",
	},
	{
		.flag = RBTRACE_DO_WRAP,
		.name = "WRAP",
	},
	{
		.flag = RBTRACE_DO_ZAP,
		.name = "ZAP",
	},
	{
		.flag = RBTRACE_DO_CLOSE,
		.name = "CLOSE",
	},
	{
		.flag = RBTRACE_DO_FLUSH,
		.name = "FLUSH",
	},
};

static char *rbtrace_op_to_str(rbtrace_op_t op)
{
	if (op < RBTRACE_OP_MAX) {
		return rbtrace_op_str[op];
	}
	return "unknown";
}

static char *flags_to_str(uint64_t flags)
{
	int i;
	int nchars;
	size_t bufsz;
	char *buf;
	static char _flags_buf[512];

	buf = _flags_buf;
	bufsz = sizeof(_flags_buf);

	for (i = 0; i < sizeof(flg_names)/sizeof(flg_names[0]); i++) {
		if (flags & flg_names[i].flag) {
			nchars = snprintf(buf, bufsz, "%s ",
					  flg_names[i].name);
			if (nchars < 0) {
				break;
			}

			buf += nchars;
			bufsz -= nchars;
		}
	}

	return _flags_buf;
}

static void dump_ring_info(struct rbtrace_op_info_arg *info_arg)
{
	printf("name             : %s\n", info_arg->ring_name);
	printf("desc             : %s\n", info_arg->ring_desc);
	printf("flags            : %s\n", flags_to_str(info_arg->flags));
	printf("tflags           : %s\n", tflags_to_str(info_arg->tflags));
	printf("file size(MB)    : %ld\n", info_arg->file_size / ONE_MB);
	printf("file path        : %s\n", info_arg->file_path);
	printf("trace entry size : %d\n", info_arg->trace_entry_size);
}

static void usage(void);
static void version(void);

int main(int argc, char *argv[])
{
	int rc = -1;
	int ch = 0;
	char *endptr = NULL;
	rbtrace_op_t op = RBTRACE_OP_MAX;
	struct rbtrace_op_tflags_arg tflags_arg;
	struct rbtrace_op_info_arg info_arg;
	bool rbtrace_inited = false;
	bool do_open = false;
	bool do_close = false;
	bool do_size = false;
	bool do_wrap = false;
	bool do_zap = false;
	bool do_info = false;
	bool do_set_tflags = false;
	bool do_clear_tflags = false;

	while ((ch = getopt(argc, argv, "vhcir:o:w:z:s:S:C:")) != -1) {
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
			do_open = true;
			break;
		case 'c':
			do_close = true;
			break;
		case 'w':
			if (strcmp(optarg, "on") == 0) {
				opts.wrap = true;
			} else if (strcmp(optarg, "off") == 0) {
				opts.wrap = false;
			} else {
				fprintf(stderr, "Invalid option:%s\n", optarg);
				goto out;
			}
			do_wrap = true;
			break;
		case 'z':
			if (strcmp(optarg, "on") == 0) {
				opts.zap = true;
			} else if (strcmp(optarg, "off") == 0) {
				opts.zap = false;
			} else {
				fprintf(stderr, "Invalid option:%s\n", optarg);
				goto out;
			}
			do_zap = true;
			break;
		case 'i':
			do_info = true;
			break;
		case 's':
			opts.size = strtoull(optarg, &endptr, 10);
			if ((endptr == optarg) || (*endptr != '\0')) {
				fprintf(stderr, "Invalid argment size!\n");
				goto out;
			}
			do_size = true;
			break;
		case 'S':
			opts.stflags = str_to_tflags(optarg);
			if (opts.stflags == 0) {
				fprintf(stderr, "Invalid traffic flags\n");
				goto out;
			}
			do_set_tflags = true;
			break;
		case 'C':
			opts.ctflags = str_to_tflags(optarg);
			if (opts.ctflags == 0) {
				fprintf(stderr, "Invalid traffic flags\n");
				goto out;
			}
			do_clear_tflags = true;
			break;
		case 'v':
			version();
			goto out;
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
	rbtrace_inited = true;

	if (do_wrap) {
		op = RBTRACE_OP_WRAP;
		rc = rbtrace_ctrl(opts.ring, op, &opts.wrap);
		if (rc != 0) {
			goto out;
		}
	} else if (do_zap) {
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
	} else if (do_close) {
		op = RBTRACE_OP_CLOSE;
		rc = rbtrace_ctrl(opts.ring, op, NULL);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_set_tflags) {
		tflags_arg.set = true;
		tflags_arg.tflags = opts.stflags;
		op = RBTRACE_OP_TFLAGS;
		rc = rbtrace_ctrl(opts.ring, op, &tflags_arg);
		if (rc != 0) {
			goto out;
		}
	}
	if (do_clear_tflags) {
		tflags_arg.set = false;
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
			dump_ring_info(&info_arg);
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
	       "       [-v]             Display the version information\n"
	       "       [-h]             Display this help message\n\n"
	       "Available trace IDs:\n%s\n", tflags_to_str(TFLAGS_ALL));
}

static void version(void)
{
	printf("rbtrace cli tool v=%s\n", RBTRACE_VERSION);
}
