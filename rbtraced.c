#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <pthread.h>
#include "rbtrace.h"
#include "rbtracedef.h"
#include "rbtrace_private.h"
#include "version.h"

#define RBTRACED_DFT_PID_FILE	"rbtraced.pid"
#define RBTRACED_DFT_LOG_FILE	"rbtraced.log"

struct rbtrace_server {
	char *pidfile;
	char *logfile;
	bool daemonize;
	volatile sig_atomic_t terminate;
} server = {
	.pidfile = RBTRACED_DFT_PID_FILE,
	.logfile = RBTRACED_DFT_LOG_FILE,
	.daemonize = false,
	.terminate = 0,
};

static void usage(void);
static void version(void);

static void sig_handler(const int sig)
{
	if ((sig != SIGTERM) &&
	    (sig != SIGQUIT)) {
		return;
	}

	server.terminate = 1;
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
	int fd = -1;
	char buf[256];

	if (server.pidfile == NULL) {
		server.pidfile = RBTRACED_DFT_PID_FILE;
	}
	fd = open(server.pidfile, O_RDWR|O_CREAT, 0660);
	if (fd < 0) {
		fprintf(stderr, "open pid file:%s failed, %s\n",
			server.pidfile,
			strerror_r(errno, buf, sizeof(buf)));
		exit(1);
	}
	if (lockf(fd, F_TLOCK, 0) < 0) {
		fprintf(stderr, "lock pid file:%s failed, %s\n",
			server.pidfile,
			strerror_r(errno, buf, sizeof(buf)));
		exit(0);
	}
	sprintf(buf, "%d\n", getpid());
	write(fd, buf, strlen(buf));
}

int main(int argc, char *argv[])
{
	int rc = 0;
	int ch;
	int sig;
	sigset_t set;

	while ((ch = getopt(argc, argv, "dhp:l:v")) != -1) {
		switch (ch) {
		case 'd':
			server.daemonize = true;
			break;
		case 'p':
			server.pidfile = optarg;
			break;
		case 'l':
			server.logfile = optarg;
			break;
		case 'v':
			version();
			goto out;
		case 'h':
		default:
			rc = -1;
			usage();
			goto out;
		}
	}

	(void)signal(SIGTERM, sig_handler);
	(void)signal(SIGQUIT, sig_handler);

	if (server.daemonize) {
		daemonize();
	}

	create_pid_file();

	rc = rbtrace_daemon_init();
	if (rc != 0) {
		fprintf(stderr, "daemon init failed, error:%d\n", rc);
		goto out;
	}

	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGINT);

	while (!server.terminate) {
		sig = 0;
		sigwait(&set, &sig);
		if ((sig == SIGTERM) ||
		    (sig == SIGQUIT) ||
		    (sig == SIGINT)) {
			break;
		}
	}

	if (server.pidfile) {
		unlink(server.pidfile);
	}

	rbtrace_daemon_exit();
	
 out:
	return rc;
}

static void version(void)
{
	printf("rbtrace daemon v=%s\n", RBTRACE_VERSION);
}

static void usage(void)
{
	printf("Usage: ./rbtraced [options]\n"
	       "       [-d]            Run as a daemon\n"
	       "       [-p <pidfile>]  Specify pid file, default is %s\n"
	       "       [-l <logfile>]  Specify log file, default is %s\n"
	       "       [-v]            Display the version information\n"
	       "       [-h]            Display this help message\n",
	       RBTRACED_DFT_PID_FILE,
	       RBTRACED_DFT_LOG_FILE);
}
