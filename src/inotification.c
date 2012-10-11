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
 *	Tim Pepper <timothy.c.pepper@linux.intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "corewatcher.h"

/*
 * rather than malloc() on each inotify event, preallocate a decent chunk
 * of memory so multiple events can be read in one go, trading a little
 * extra memory for less runtime overhead if/when multiple crashes happen
 * in short order.
 */
#include <sys/inotify.h>
#define BUF_LEN 2048

gboolean inotify_source_prepare(__unused GSource *source, gint *timeout_)
{
	*timeout_ = 0;
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
		fprintf(stderr, "corewatcher inotify init failed...exiting\n");
		return EXIT_FAILURE;
	}
	wd = inotify_add_watch(fd, core_folder, IN_CLOSE_WRITE);
	if (wd < 0) {
		fprintf(stderr, "corewatcher inotify add failed...exiting\n");
		return EXIT_FAILURE;
	}
	fprintf(stderr, "+ awaiting inotification...\n");
	len = read(fd, buffer, BUF_LEN);
	if (len <=0 ) {
		fprintf(stderr, "corewatcher inotify read failed...exiting\n");
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
		fprintf(stderr, "+ inotify dispatch failed\n");
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
	g_source_set_callback(source, scan_core_folder, NULL, NULL);

	fprintf(stderr, "+ inotify loop starting\n");
	g_main_loop_run(loop);
	fprintf(stderr, "+ inotify loop finished\n");

	return NULL;
}
