#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "rbtracedef.h"

struct prbt_option {
	char *file_path;
	char *out_path;
	bool_t only_summary;
} opts = {
	.file_path = NULL,
	.out_path = NULL,
	.only_summary = FALSE,
};

static void usage(void);

static int parse_trace_header(int fd, FILE *fp,
			      union padded_rbtrace_fheader *prf)
{
	int rc = 0;
	uint64_t nbytes = 0;
	struct tm *gm = NULL;

	nbytes = pread(fd, prf, sizeof(*prf), 0);
	if (nbytes != sizeof(*prf)) {
		rc = errno;
		fprintf(stderr, "Failed to read trace header, error:%d\n", rc);
		goto out;
	}

	if (strcmp(prf->hdr.magic, RBTRACE_FHEADER_MAGIC) != 0) {
		rc = -1;
		fprintf(stderr, "Invalid header magic!\n");
		goto out;
	}

	if ((prf->hdr.major != RBTRACE_MAJOR) ||
	    (prf->hdr.minor != RBTRACE_MINOR)) {
		rc = -1;
		fprintf(stderr, "Revision %d.%d mismatch! %d.%d supported\n",
			prf->hdr.major, prf->hdr.minor,
			RBTRACE_MAJOR, RBTRACE_MINOR);
		goto out;
	} else {
		fprintf(fp, "REVISION: %d.%d\n", prf->hdr.major, prf->hdr.minor);
	}

	gm = localtime(&prf->hdr.timestamp.tv_sec);
	if (gm == NULL) {
		rc = -1;
		fprintf(stderr, "Invalid timestamp in trace header!\n");
	} else {
		fprintf(fp, "OPEN AT: %02d/%02d %02d:%02d:%02d.%06ld\n",
			gm->tm_mon + 1, gm->tm_mday, gm->tm_hour, gm->tm_min,
			gm->tm_sec, prf->hdr.timestamp.tv_nsec / 1000);
	}

	if (prf->hdr.wrap_pos != 0) {
		fprintf(fp, "wrap position: %ld\n", prf->hdr.wrap_pos);
	}

 out:
	return rc;
}

static void print_trace_summary(int fd, FILE *fp,
				union padded_rbtrace_fheader *prf)
{
	off_t off = 0;
	off_t fsize = 0;
	char buf[128];
	struct rbtrace_record rr;
	struct tm *gm = NULL;

	fsize = lseek(fd, 0, SEEK_END);
	if (fsize < (sizeof(*prf) + sizeof(rr))) {
		fprintf(stderr, "Empty trace file!\n");
		return;
	}

	if (prf->hdr.wrap_pos) {
		off = prf->hdr.wrap_pos;
	} else {
		off = sizeof(*prf);
	}

	if (pread(fd, &rr, sizeof(rr), off) != sizeof(rr)) {
		fprintf(stderr, "pread %ld bytes from off %ld failed, error:%d\n",
			sizeof(rr), off, errno);
		return;
	}

	gm = localtime(&rr.rr_timestamp.tv_sec);
	if (gm == NULL) {
		fprintf(stderr, "invalid timestamp %ld for first trace record!\n",
			rr.rr_timestamp.tv_sec);
		return;
	}

	strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", gm);
	fprintf(fp, "start time: %s\n", buf);

	/* Wrapped file, last trace record is just before current one */
	if (off >= (sizeof(*prf) + sizeof(rr))) {
		off -= sizeof(rr);
	}
	/* Last trace record is at file end */
	else {
		off = fsize - sizeof(rr);
	}

	if (pread(fd, &rr, sizeof(rr), off) != sizeof(rr)) {
		fprintf(stderr, "pread %ld bytes from off %ld failed, error:%d\n",
			sizeof(rr), off, errno);
		return;
	}

	gm = localtime(&rr.rr_timestamp.tv_sec);
	if (gm == NULL) {
		fprintf(stderr, "invalid timestamp %ld for last trace record!\n",
			rr.rr_timestamp.tv_sec);
		return;
	}

	strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", gm);
	fprintf(fp, "end time: %s\n", buf);
}

static size_t load_trace_page(int fd, uint64_t page_idx,
			      char *page, size_t page_size)
{
	size_t nbytes;
	off_t off;

	off = sizeof(union padded_rbtrace_fheader) + page_idx * page_size;
	nbytes = pread(fd, page, page_size, off);
	if (nbytes == -1) {
		fprintf(stderr, "pread %zu bytes from off %ld failed, error:%d\n",
			page_size, off, errno);
	} else if (nbytes % sizeof(struct rbtrace_record)) {
		fprintf(stderr, "non-aligned trace page, idx %ld, nbytes %zu\n",
			page_idx, nbytes);
	}

	return nbytes;
}

static bool_t trace_print_fn(uint64_t idx, FILE *fp,
			     struct rbtrace_record *rr)
{
	char record_buf[256];
	int nchars = 0;
	struct tm *gm = NULL;

	gm = localtime(&rr->rr_timestamp.tv_sec);
	if (gm == NULL) {
		fprintf(stderr, "idx:%ld, invalid timestamp %ld\n",
			idx, rr->rr_timestamp.tv_sec);
		goto out;
	}

	/* Format time stamp, cpu and thread ID */
	nchars = snprintf(record_buf, sizeof(record_buf),
			  "%02d-%02d %02d:%02d:%02d.%06ld %2d %8d ",
			  gm->tm_mon + 1, gm->tm_mday, gm->tm_hour,
			  gm->tm_min, gm->tm_sec,
			  rr->rr_timestamp.tv_nsec / 1000,
			  rr->rr_cpuid, rr->rr_thread);

	sprintf(record_buf + nchars, "%16lX %16lX %16lX %16lX\n",
		rr->rr_a0, rr->rr_a1, rr->rr_a2, rr->rr_a3);

	fprintf(fp, record_buf);

 out:
	return FALSE;
}

static void parse_trace_file(int fd, FILE *fp, struct rbtrace_fheader *rf,
			     bool_t (*print_fn)(uint64_t idx, FILE *fp,
						struct rbtrace_record *rr))
{
	bool_t again = TRUE;
	bool_t stop = FALSE;
	size_t nbytes = 0;
	size_t page_size = 0;
	uint64_t page_idx = 0;
	char *page = NULL;
	struct rbtrace_record *rr = NULL;
	uint64_t cnt = 0;

	page_size = sizeof(*rr) * rf->nr_records;
	page = malloc(page_size);
	if (page == NULL) {
		fprintf(stderr, "Failed to malloc %zu bytes for trace record!\n",
			page_size);
		goto out;
	}

	do {
		nbytes = load_trace_page(fd, page_idx, page, page_size);
		if (nbytes <= 0) {
			break;
		} else if (nbytes < page_size) {
			again = FALSE;
		}

		rr = (struct rbtrace_record *)page;
		while (nbytes >= sizeof(*rr)) {
			cnt++;
			stop = print_fn(cnt, fp, rr);
			if (stop) {
				goto out;
			}

			rr++;
			nbytes -= sizeof(*rr);
		}

		page_idx++;
	} while (again);

	if (rf->wrap_pos == 0) {
		goto out;
	}

	/* The file is wrapped, handle the rest of the records */
	page_idx = 0;

 out:
	if (page) {
		free(page);
	}
}

int main(int argc, char *argv[])
{
	int rc = 0;
	int ch = 0;
	int fd = -1;
	union padded_rbtrace_fheader prf;
	FILE *fp = NULL;

	while ((ch = getopt(argc, argv, "f:o:sh")) != -1) {
		switch (ch) {
		case 'f':
			opts.file_path = optarg;
			break;
		case 'o':
			opts.out_path = optarg;
			break;
		case 's':
			opts.only_summary = TRUE;
			break;
		case 'h':
		default:
			usage();
			goto out;
		}
	}

	if (opts.file_path == NULL) {
		fprintf(stderr, "Missing trace file path!\n");
		goto out;
	}

	fd = open(opts.file_path, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Failed to open trace file:%s, error:%d\n",
			opts.file_path, errno);
		goto out;
	}

	if (opts.out_path) {
		fp = fopen(opts.out_path, "a");
		if (fp == NULL) {
			fprintf(stderr, "Failed to open output file:%s, error:%d\n",
				opts.out_path, errno);
			goto out;
		}
	} else {
		fp = stdout;
	}

	rc = parse_trace_header(fd, fp, &prf);
	if (rc != 0) {
		fprintf(stderr, "Invalid trace header!\n");
		goto out;
	}

	if (opts.only_summary) {
		print_trace_summary(fd, fp, &prf);
		goto out;
	}

	parse_trace_file(fd, fp, &prf.hdr, trace_print_fn);

 out:
	if (fd != -1) {
		close(fd);
	}
	return rc;
}

static void usage(void)
{
	printf("Usage: ./prbt <options>\n"
	       "       [-f <trace-file>]  Specify trace file path\n"
	       "       [-o <output-file>] Specify output file path\n"
	       "       [-s]               Only show trace file summary\n"
	       "       [-h]               Display this help message\n");
}
