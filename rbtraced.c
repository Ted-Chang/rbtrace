#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include "rbtrace.h"
#include "rbtracedef.h"
#include "rbtrace_private.h"
#include "version.h"

#define RBTRACED_DFT_PID_FILE	"/var/log/rbtraced.pid"
#define RBTRACED_DFT_LOG_FILE	"/var/log/rbtraced.log"

struct rbtrace_server {
	char *pidfile;
	char *logfile;
	bool_t daemonize;
	bool_t terminate;
} server = {
	.pidfile = RBTRACED_DFT_PID_FILE,
	.logfile = RBTRACED_DFT_LOG_FILE,
	.daemonize = FALSE,
	.terminate = FALSE
};

static void usage(void);
static void version(void);

static void sig_handler(const int sig)
{
	if ((sig != SIGTERM) &&
	    (sig != SIGQUIT) &&
	    (sig != SIGINT)) {
		return;
	}

	rbtrace_daemon_exit();
	if (server.pidfile) {
		unlink(server.pidfile);
	}

	server.terminate = TRUE;
}

static void daemonize(void)
{
	int fd;

	if (fork() != 0) {
		exit(0);	// parent exits
	}
	umask(0);	// unmasking the file mode
	setsid();	// create a new session
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			close(fd);
		}
	}
}

static void create_pid_file(void)
{
	FILE *fp = NULL;
	if (server.pidfile == NULL) {
		server.pidfile = RBTRACED_DFT_PID_FILE;
	}
	fp = fopen(server.pidfile, "w");
	if (NULL != fp) {
		fprintf(fp, "%d\n", (int)getpid());
		fclose(fp);
	}
}

int main(int argc, char *argv[])
{
	int rc = 0;
	int ch;

	while ((ch = getopt(argc, argv, "dhp:l:v")) != -1) {
		switch (ch) {
		case 'd':
			server.daemonize = TRUE;
			break;
		case 'p':
			server.pidfile = optarg;
			break;
		case 'l':
			server.logfile = optarg;
			break;
		case 'v':
			version();
			break;
		case 'h':
		default:
			rc = -1;
			usage();
			goto out;
		}
	}

	if (signal(SIGTERM, sig_handler) == SIG_ERR) {
		;
	}
	if (signal(SIGQUIT, sig_handler) == SIG_ERR) {
		;
	}
	if (signal(SIGINT, sig_handler) == SIG_ERR) {
		;
	}

	if (server.daemonize) {
		daemonize();
	}

	if (server.daemonize || (NULL != server.pidfile)) {
		create_pid_file();
	}

	if (server.daemonize || (NULL != server.logfile)) {
		;
	}

	rc = rbtrace_daemon_init();
	if (rc != 0) {
		dprintf("daemon init failed, error:%d\n", rc);
		goto out;
	}

	while (!server.terminate) {
		sleep(1);
	}

 out:
	return rc;
}

static void version(void)
{
	printf("rbtrace daemon v=%s\n",
	       RBTRACE_VERSION);
}

static void usage(void)
{
	printf("Usage: ./rbtraced [options]\n"
	       "       [-d]            Run as a daemon\n"
	       "       [-p <pidfile>]  Specify pid file, default is %s\n"
	       "       [-l <logfile>]  Specify log file, default is %s\n"
	       "       [-h]            Display this help message\n",
	       RBTRACED_DFT_PID_FILE,
	       RBTRACED_DFT_LOG_FILE);
}
