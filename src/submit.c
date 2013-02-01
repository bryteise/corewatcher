#define _GNU_SOURCE
/*
 * Copyright 2007,2012 Intel Corporation
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
#include <syslog.h>
#include <sys/stat.h>
#include <glib.h>
#include <asm/unistd.h>
#include <curl/curl.h>

#include "corewatcher.h"

GMutex *bt_mtx;
GCond *bt_work;
GHashTable *bt_hash;
static struct oops *bt_list = NULL;

/*
 * Adds an oops to the work queue if the oops
 * isn't already there.
 */
void queue_backtrace(struct oops *oops)
{
	if (!oops || !oops->filename)
		return;

	g_mutex_lock(bt_mtx);

	/* if this is already on bt_list / bt_hash, free and done */
	if (g_hash_table_lookup(bt_hash, oops->filename)) {
		FREE_OOPS(oops);
		g_mutex_unlock(bt_mtx);
		return;
	}

	/* otherwise add to bt_list / bt_hash, signal work */
	oops->next = bt_list;
	bt_list = oops;
	g_hash_table_insert(bt_hash, oops->filename, oops->filename);
	g_cond_signal(bt_work);
	g_mutex_unlock(bt_mtx);
}

/*
 * For testmode to display all oops that would
 * be submitted.
 *
 * Picks up and sets down the bt_mtx.
 */
static void print_queue(void)
{
	struct oops *oops = NULL, *next = NULL;
	int count = 0;

	g_mutex_lock(bt_mtx);
	oops = bt_list;
	while (oops) {
		fprintf(stderr, "+ Submit text is:\n---[start of oops]---\n%s\n---[end of oops]---\n", oops->text);
		next = oops->next;
		FREE_OOPS(oops);
		oops = next;
		count++;
	}
	g_hash_table_remove_all(bt_hash);
	g_mutex_unlock(bt_mtx);
}

static size_t writefunction(void *ptr, size_t size, size_t nmemb, void __attribute((unused)) *stream)
{
	char *httppost_ret = NULL;
	int ret = 0;

	httppost_ret = malloc(size * nmemb + 1);
	if (!httppost_ret)
		return -1;

	memset(httppost_ret, 0, size * nmemb + 1);
	memcpy(httppost_ret, ptr, size * nmemb);

	fprintf(stderr, "+ received:\n");
	fprintf(stderr, "%s", httppost_ret);
	fprintf(stderr, "\n\n");

	if (strstr(httppost_ret, "the server encountered an error") != NULL) {
		ret = -1;
		goto err;
	}
	if (strstr(httppost_ret, "ScannerError at /crash_submit/") != NULL) {
		ret = -1;
		goto err;
	}
	if (strstr(httppost_ret, "was not found on this server") != NULL) {
		ret = -1;
		goto err;
	}

	ret = size * nmemb;
err:
	free(httppost_ret);
	return ret;
}

/*
 * Replace the extension of a file.
 * TODO: make search from end of string
 */
char *replace_name(char *filename, char *replace, char *new)
{
	char *newfile = NULL, *oldfile, *c = NULL;

	if (!filename || !replace || !new)
		return NULL;

	oldfile = strdup(filename);
	if (!oldfile)
		return NULL;

	c = strstr(oldfile, replace);
	if (!c) {
		free(oldfile);
		return NULL;
	}

	oldfile[strlen(oldfile) - strlen(c)] = '\0';

	if (asprintf(&newfile, "%s%s",  oldfile, new) == -1) {
		free(oldfile);
		return NULL;
	}
	free(oldfile);

	return newfile;
}

void report_good_send(int *sentcount, struct oops *oops)
{
	char *newfilename = NULL;

	fprintf(stderr, "+ successfully sent %s\n", oops->detail_filename);
	sentcount++;

	newfilename = replace_name(oops->filename, ".processed", ".submitted");
	rename(oops->filename, newfilename);
	free(newfilename);

	g_mutex_lock(bt_mtx);
	g_hash_table_remove(bt_hash, oops->filename);
	g_mutex_unlock(bt_mtx);
	FREE_OOPS(oops);
}

void report_fail_send(int *failcount, struct oops *oops, struct oops *requeue_list)
{
	fprintf(stderr, "+ requeuing %s\n", oops->detail_filename);
	failcount++;

	oops->next = requeue_list;
	requeue_list = oops;
}

/*
 * Worker thread for submitting backtraces
 *
 * Picks up and sets down the bt_mtx.
 */
void *submit_loop(void __unused *unused)
{
	int i = 0, sentcount, failcount;
	struct oops *oops = NULL;
	struct oops *work_list = NULL;
	struct oops *requeue_list = NULL;
	int result = 0;
	CURL *handle = NULL;
	struct curl_httppost *post;
	struct curl_httppost *last;

	fprintf(stderr, "+ Begin submit_loop()\n");

	if (testmode) {
		fprintf(stderr, "+ The queue contains:\n");
		print_queue();
		fprintf(stderr, "+ Leaving submit_loop(), testmode\n");
		return NULL;
	}

	while (1) {
		g_mutex_lock(bt_mtx);
		while (!bt_list) {
			if (requeue_list) {
				bt_list = requeue_list;
				requeue_list = NULL;
				fprintf(stderr, "+ submit_loop() requeued old work, awaiting new work\n");
			} else {
				fprintf(stderr, "+ submit_loop() queue empty, awaiting new work\n");
			}
			g_cond_wait(bt_work, bt_mtx);
		}
		fprintf(stderr, "+ submit_loop() checking for work\n");
		/* pull out current work and release the mutex */
		work_list = bt_list;
		bt_list = NULL;
		g_mutex_unlock(bt_mtx);

		/* net init */
		handle = curl_easy_init();
		curl_easy_setopt(handle, CURLOPT_NOBODY, 1);
		curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5);

		/* try to find a good url (curl automagically will use config'd proxies */
		for (i = 0; i < url_count; i++) {
			sentcount = 0;
			failcount = 0;

			curl_easy_setopt(handle, CURLOPT_URL, submit_url[i]);
			curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);

			/* check the connection before POSTing form */
			result = curl_easy_perform(handle);
			if (result) {
				fprintf(stderr, "+ unable to contact %s\n", submit_url[i]);
				continue;
			}
			fprintf(stderr, "+ Draining work_list to %s\n", submit_url[i]);

			/* have a good url/proxy now...attempt sending all reports there */
			while (work_list) {
				oops = work_list;
				work_list = oops->next;
				oops->next = NULL;

				fprintf(stderr, "+ attempting to POST %s\n", oops->detail_filename);

				/* set up the POST data */
				post = NULL;
				last = NULL;
				curl_formadd(&post, &last,
					CURLFORM_COPYNAME, "crash",
					CURLFORM_COPYCONTENTS, oops->text, CURLFORM_END);
				curl_easy_setopt(handle, CURLOPT_HTTPPOST, post);
				curl_easy_setopt(handle, CURLOPT_POSTREDIR, 0L);
				curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunction);
				result = curl_easy_perform(handle);
				curl_formfree(post);

				if (!result) {
					report_good_send(&sentcount, oops);
				} else {
					report_fail_send(&failcount, oops, requeue_list);
				}
			}

			if (sentcount)
				syslog(LOG_INFO, "corewatcher: Successfully sent %d coredump signatures to %s", sentcount, submit_url[i]);
			if (failcount)
				syslog(LOG_INFO, "corewatcher: Failed to send %d coredump signatures to %s", failcount, submit_url[i]);

			break;
		}
		if (work_list) {
			fprintf(stderr, "+ No urls worked, requeueing all work\n");
			requeue_list = work_list;
		}

		curl_easy_cleanup(handle);
	}

	fprintf(stderr, "+ End submit_loop()\n");

	/* curl docs say this is not thread safe...but we never get here*/
	curl_global_cleanup();

	return NULL;
}
