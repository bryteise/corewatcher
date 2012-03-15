#define _GNU_SOURCE
/*
 * Copyright 2007, Intel Corporation
 *
 * This file is part of corewatcher.org
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * Authors:
 *	Arjan van de Ven <arjan@linux.intel.com>
 *	William Douglas <william.douglas@intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <syslog.h>
#include <getopt.h>
#include <sys/prctl.h>
#include <asm/unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <limits.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>


/* see linux kernel doc Documentation/block/ioprio.txt */
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_CLASS_IDLE 3
#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_CLASS_DATA 7
#define IOPRIO_IDLE_LOWEST (IOPRIO_CLASS_DATA | (IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT))

#include "corewatcher.h"


static struct option opts[] = {
	{ "nodaemon", 0, NULL, 'n' },
	{ "file",     1, NULL, 'f' },
	{ "debug",    0, NULL, 'd' },
	{ "always",   0, NULL, 'a' },
	{ "test",     0, NULL, 't' },
	{ "help",     0, NULL, 'h' },
	{ 0, 0, NULL, 0 }
};

static char corewatcher_mon_file[] = "/tmp/corewatcher";
static int corewatcher_fd;
struct core_status core_status;

int pinged;
int testmode = 0;

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
	fprintf(stderr, "  -n, --nodaemon  Do not daemonize, run in foreground\n");
	fprintf(stderr, "  -f, --file	   Don't poll for crash, run with file "
		"specified (only looks for file in /tmp)\n");
	fprintf(stderr, "  -d, --debug	   Enable debug mode\n");
	fprintf(stderr, "  -a, --always	   Always send core dumps\n");
	fprintf(stderr, "  -t, --test	   Do not send anything\n");
	fprintf(stderr, "  -h, --help	   Display this help message\n");
}

static int server_init(void) {
	int r;
	unsigned i;
	struct epoll_event ev;
	sigset_t mask;
	int epoll_fd;
	mode_t prev_mode;
	mode_t perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;


	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		r = -errno;
		goto fail;
	}

	prev_mode = umask(0);
	r = mkfifo(corewatcher_mon_file, perms);
	if (r == -1 && errno != EEXIST) {
		r  = -errno;
		goto fail;
	}
	(void)umask(prev_mode);

	corewatcher_fd = open(corewatcher_mon_file, O_RDONLY | O_NONBLOCK);

	if (corewatcher_fd == -1) {
		r  = -errno;
		goto fail;
	}

	ev.events = EPOLLIN;
	ev.data.fd = corewatcher_fd;

	r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, corewatcher_fd, &ev);
	if (r < 0) {
		r  = -errno;
		goto fail;
	}

	r = epoll_fd;

fail:
	return r;
}

static int find_invalid_chars(char *str, int size)
{
	int i;
	for (i = 0; i < size; i++)
		if (isspace(str[i]) || str[i] == '\0')
			return 1;

	return 0;
}

static int process_event(struct epoll_event *event)
{
	int r;
	char buf[PATH_MAX];
	struct stat st;

	bzero(buf, PATH_MAX);

	if(event->data.fd != corewatcher_fd) {
		r = 1;
		goto quit;
	}

	if (flock(corewatcher_fd, LOCK_EX)) {
		r = -errno;
		goto fail;
	}

	r = read(corewatcher_fd, buf, PATH_MAX-1);
	if (r < 0) {
		r = -errno;
		goto fail;
	} else if (r == 0) {
		r = 1;
		goto quit;
	}

	r = find_invalid_chars(buf, r);
	if (r) {
		r = 1;
		goto quit;
	}

	r = scan_corefolders(buf);

	if (r == -ENOENT) {
		r = 1;
		goto quit;
	}

fail:
	if (corewatcher_fd >= 0) {
		(void)flock(corewatcher_fd, LOCK_UN);
		(void)close(corewatcher_fd);
	}

quit:
	return r;
}

int main(int argc, char**argv)
{
	int epoll_fd;
	int godaemon = 1;
	int debug = 0;
	int j = 0;
	char *fname = NULL;

	core_status.asked_oops = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
	core_status.processing_oops = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
	core_status.queued_oops = g_hash_table_new(g_str_hash, g_str_equal);
	if (pthread_mutex_init(&core_status.asked_mtx, NULL))
		goto fail;
	if (pthread_mutex_init(&core_status.processing_mtx, NULL))
		goto fail;
	if (pthread_mutex_init(&core_status.queued_mtx, NULL))
		goto fail;

/*
 * Signal the kernel that we're not timing critical
 */
#ifdef PR_SET_TIMERSLACK
	prctl(PR_SET_TIMERSLACK,1000*1000*1000, 0, 0, 0);
#endif
	/* Be easier on the rest of the system */
	if (syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid(),
		    IOPRIO_IDLE_LOWEST) == -1)
		perror("Can not set IO priority to lowest IDLE class");
	if (nice(15) < 0)
		perror("Can not set schedule priority");

	while (1) {
		int c;
		int i;

		c = getopt_long(argc, argv, "adnf:th", opts, &i);
		if (c == -1)
			break;

		switch(c) {
		case 'n':
			fprintf(stderr, "+ Not running as daemon\n");
			godaemon = 0;
			break;
		case 'f':
			if (strlen(optarg) >= PATH_MAX) {
				usage(argv[0]);
				return EXIT_SUCCESS;
			}
			fname = strdup(optarg);
			if (!fname) {
				fprintf(stderr,
					"+ Couldn't allocate memory for file argument");
				goto fail;
			}
			fprintf(stderr, "+ Using file /tmp/%s\n", optarg);
			break;
		case 'd':
			fprintf(stderr, "+ Starting corewatcher in debug mode\n");
			debug = 1;
			break;
		case 'a':
			fprintf(stderr, "+ Sending All reports\n");
			opted_in = 2;
			break;
		case 't':
			fprintf(stderr, "+ Test mode enabled: not sending anything\n");
			testmode = 1;
			break;
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			break;
		}
	}

	/*
	 * the curl docs say that we "should" call curl_global_init early,
	 * even though it'll be called later on via curl_easy_init().
	 * We ignore this advice, since 99.99% of the time this program
	 * will not use http at all, but the curl code does consume
	 * memory.
	 */

/*
	curl_global_init(CURL_GLOBAL_ALL);
*/

	if (!fname && godaemon && daemon(0, 0)) {
		fprintf(stderr, "corewatcher failed to daemonize.. exiting \n");
		goto fail;
	}

	if (!debug && !fname)
		sleep(20);

	/* during boot... don't go too fast and slow the system down */

	if (testmode) {
		(void)scan_corefolders(fname);
		for (j = 0; j < url_count; j++)
			free(submit_url[j]);
		g_hash_table_destroy(core_status.asked_oops);
		g_hash_table_destroy(core_status.processing_oops);
		g_hash_table_destroy(core_status.queued_oops);
		pthread_mutex_destroy(&core_status.asked_mtx);
		pthread_mutex_destroy(&core_status.processing_mtx);
		pthread_mutex_destroy(&core_status.queued_mtx);
		fprintf(stderr, "+ Exiting from testmode\n");
		return EXIT_SUCCESS;
	}

	if (fname)
		return scan_corefolders(fname);

	/* now, start polling for oopses to occur */
	epoll_fd = server_init();
	if (epoll_fd < 0)
		goto fail;

	for (;;) {
		struct epoll_event event;
		int k;

		if ((k = epoll_wait(epoll_fd, &event, 1, -1)) < 0) {

			if (errno == EINTR)
				continue;

			goto fail;
		}

		if (k <= 0)
			break;

		if ((k = process_event(&event)) < 0) {
			goto fail;
		}

		if (k == 0)
			break;

		/* if k > 0, retry bad input */
	}

	for (j = 0; j < url_count; j++)
		free(submit_url[j]);

	g_hash_table_destroy(core_status.asked_oops);
	g_hash_table_destroy(core_status.processing_oops);
	g_hash_table_destroy(core_status.queued_oops);
	pthread_mutex_destroy(&core_status.asked_mtx);
	pthread_mutex_destroy(&core_status.processing_mtx);
	pthread_mutex_destroy(&core_status.queued_mtx);

	return EXIT_SUCCESS;

fail:
	return EXIT_FAILURE;
}
