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

#include <glib.h>


/* see linux kernel doc Documentation/block/ioprio.txt */
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_CLASS_IDLE 3
#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_CLASS_DATA 7
#define IOPRIO_IDLE_LOWEST (IOPRIO_CLASS_DATA |  (IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT))

#include "corewatcher.h"


static struct option opts[] = {
	{ "nodaemon", 0, NULL, 'n' },
	{ "debug",    0, NULL, 'd' },
	{ "always",   0, NULL, 'a' },
	{ "test",     0, NULL, 't' },
	{ "help",     0, NULL, 'h' },
	{ 0, 0, NULL, 0 }
};

struct core_status core_status;

int testmode = 0;

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
	fprintf(stderr, "  -n, --nodaemon  Do not daemonize, run in foreground\n");
	fprintf(stderr, "  -d, --debug     Enable debug mode\n");
	fprintf(stderr, "  -t, --test      Do not send anything\n");
	fprintf(stderr, "  -h, --help      Display this help message\n");
}

int main(int argc, char**argv)
{
	GMainLoop *loop;
	int godaemon = 1;
	int debug = 0;
	int j = 0;

	core_status.asked_oops = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
	core_status.processing_oops = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
	core_status.queued_oops = g_hash_table_new(g_str_hash, g_str_equal);
	if (pthread_mutex_init(&core_status.asked_mtx, NULL))
		return EXIT_FAILURE;
	if (pthread_mutex_init(&core_status.processing_mtx, NULL))
		return EXIT_FAILURE;
	if (pthread_mutex_init(&core_status.queued_mtx, NULL))
		return EXIT_FAILURE;

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

	read_config_file("/etc/corewatcher/corewatcher.conf");

	while (1) {
		int c;
		int i;

		c = getopt_long(argc, argv, "adnth", opts, &i);
		if (c == -1)
			break;

		switch(c) {
		case 'n':
			fprintf(stderr, "+ Not running as daemon\n");
			godaemon = 0;
			break;
		case 'd':
			fprintf(stderr, "+ Starting corewatcher in debug mode\n");
			debug = 1;
			break;
		case 't':
			testmode = 1;
			fprintf(stderr, "+ Test mode enabled: not sending anything\n");
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

	if (godaemon && daemon(0, 0)) {
		fprintf(stderr, "corewatcher failed to daemonize.. exiting \n");
		return EXIT_FAILURE;
	}
	sched_yield();

	loop = g_main_loop_new(NULL, FALSE);

	if (!debug)
		sleep(20);

	scan_corefolders(NULL);

	if (testmode) {
		g_main_loop_unref(loop);
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

	/* now, start polling for oopses to occur */

	g_timeout_add_seconds(10, scan_corefolders, NULL);

	g_main_loop_run(loop);

	g_main_loop_unref(loop);
	for (j = 0; j < url_count; j++)
		free(submit_url[j]);

	g_hash_table_destroy(core_status.asked_oops);
	g_hash_table_destroy(core_status.processing_oops);
	g_hash_table_destroy(core_status.queued_oops);
	pthread_mutex_destroy(&core_status.asked_mtx);
	pthread_mutex_destroy(&core_status.processing_mtx);
	pthread_mutex_destroy(&core_status.queued_mtx);

	return EXIT_SUCCESS;
}
