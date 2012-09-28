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
#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include <glib.h>


/* see linux kernel doc Documentation/block/ioprio.txt */
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_CLASS_IDLE 3
#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_CLASS_DATA 7
#define IOPRIO_IDLE_LOWEST (IOPRIO_CLASS_DATA |  (IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT))

#include "corewatcher.h"

/*
 * rather than malloc() on each inotify event, preallocate a decent chunk
 * of memory so multiple events can be read in one go, trading a little
 * extra memory for less runtime overhead if/when multiple crashes happen
 * in short order.
 */
#include <sys/inotify.h>
#define BUF_LEN 2048

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

gboolean inotify_source_prepare(__unused GSource *source, gint *timeout_)
{
	*timeout_ = -1;
	fprintf(stderr, "+ inotification prepare\n");
	return FALSE;
}

gboolean inotify_source_check(__unused GSource *source)
{
	int fd, wd;
	char buffer[BUF_LEN];
	size_t len;

	fprintf(stderr, "+ inotification check\n");
	/* inotification of crashes */
	fd = inotify_init();
	if (fd < 0) {
		fprintf(stderr, "corewatcher inotify init failed.. exiting\n");
		return EXIT_FAILURE;
	}
	wd = inotify_add_watch(fd, core_folder, IN_CLOSE_WRITE);
	if (wd < 0) {
		fprintf(stderr, "corewatcher inotify add failed.. exiting\n");
		return EXIT_FAILURE;
	}
	fprintf(stderr, "+ awaiting inotification...\n");
	len = read(fd, buffer, BUF_LEN);
	if (len <=0 ) {
		fprintf(stderr, "corewatcher inotify read failed.. exiting\n");
		return FALSE;
	}
	fprintf(stderr, "+ inotification received!\n");
	/* for now simply ignore the actual crash files we've been notified of
	 * and let our callback be dispatched to go look for any/all crash
	 * files */

	/* slight delay to minimize storms of notifications (the inotify
	 * read() can return a batch of notifications*/
	sleep(5);
	return TRUE;
}

gboolean inotify_source_dispatch(__unused GSource *source,
                                 GSourceFunc callback, gpointer user_data)
{
	fprintf(stderr, "+ inotify dispatch\n");
	if(callback(user_data)) {
		fprintf(stderr, "+ inotify dispatch success\n");
		return TRUE;
	} else {
		//should not happen as our callback always returns 1
		fprintf(stderr, "+ inotify dispatch failed.\n");
		return FALSE;
	}
}

void *inotify_loop(void __unused *unused)
{
	/* inotification of crashes */
	GMainLoop *loop;
	GMainContext *context;
	GSource *source;
	GSourceFuncs InotifySourceFuncs = {
		inotify_source_prepare,
		inotify_source_check,
		inotify_source_dispatch,
		NULL,
		NULL,
		NULL,
	};

	context = g_main_context_new();
	loop = g_main_loop_new(context, FALSE);
	loop = g_main_loop_ref(loop);
	source = g_source_new(&InotifySourceFuncs, sizeof(GSource));
	g_source_attach(source, context);
	g_source_set_callback(source, scan_corefolders, NULL, NULL);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	return NULL;
}

int main(int argc, char**argv)
{
	GMainLoop *loop;
	int godaemon = 1;
	int debug = 0;
	int j = 0;
	DIR *dir = NULL;
	GThread *inotify_thread = NULL;

	g_thread_init (NULL);

	core_status.processing_oops = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
	core_status.queued_oops = g_hash_table_new(g_str_hash, g_str_equal);

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

	/* insure our directories exist */
	dir = opendir(core_folder);
	if (!dir) {
		mkdir(core_folder, S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX);
		dir = opendir(core_folder);
		if (!dir)
			return 1;
	}
	chmod(core_folder, S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX);
	closedir(dir);
	dir = opendir(processed_folder);
	if (!dir) {
		mkdir(processed_folder, S_IRWXU);
		chmod(processed_folder, S_IRWXU);
		dir = opendir(processed_folder);
		if (!dir)
			return 1;
	}
	chmod(processed_folder, S_IRWXU);
	closedir(dir);

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
	loop = g_main_loop_ref(loop);

	if (!debug)
		sleep(20);

	if (testmode) {
		scan_corefolders(NULL);
		fprintf(stderr, "+ Exiting from testmode\n");
		goto out;
	}

	inotify_thread = g_thread_new("corewatcher inotify", inotify_loop, NULL);
	if (inotify_thread == NULL)
		fprintf(stderr, "+ Unable to start inotify thread\n");

	/*
	 * TODO: add a thread / event source tied to a connmand plugin
	 *  o  network up: trigger scan_corefolders(), enables event sources
	 *  o  network down: disable sources (or allow them to run and create
	 *     a low quality crash reports?)
	 *  o  low bandwidth net up: allow transmitting of .txt crash
	 *     summaries (ie: no running gdb)
	 *  o  high bandwidth net up: look at existing cores vs .txt's for
	 *     quality and if debuginfo is retrievable, try to improve
	 *     the report quality and submit again
	 */

	/*
	 * long poll for crashes: at inotify time we might not have been
	 * able to fully process things, here we'd push those reports out
	 */
	g_timeout_add_seconds(900, scan_corefolders, NULL);

	g_main_loop_run(loop);
out:
	g_main_loop_unref(loop);

	for (j = 0; j < url_count; j++)
		free(submit_url[j]);

	g_hash_table_destroy(core_status.processing_oops);
	g_hash_table_destroy(core_status.queued_oops);

	return EXIT_SUCCESS;
}
