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
#include <syslog.h>
#include <sys/stat.h>
#include <glib.h>
#include <asm/unistd.h>
#include <pthread.h>
#include <proxy.h>
#include <curl/curl.h>

#include "corewatcher.h"

/* Always pick up the queued_mtx and then the
   queued_bt_mtx, reverse for setting down */
static pthread_mutex_t queued_bt_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct oops *queued_backtraces = NULL;
static char result_url[4096];

/*
 * Creates a duplicate of oops and adds it to
 * the submit queue if the oops isn't already
 * there.
 *
 * Expects the queued_mtx to be held
 * Picks up and sets down the queued_bt_mtx.
 */
void queue_backtrace(struct oops *oops)
{
	struct oops *new = NULL;

	if (!oops || !oops->filename)
		return;

	/* first, check if we haven't already submitted the oops */

	if (g_hash_table_lookup(core_status.queued_oops, oops->filename))
		return;

	new = malloc(sizeof(struct oops));
	if (!new)
		return;
	pthread_mutex_lock(&queued_bt_mtx);
	new->next = queued_backtraces;
	if (oops->application)
		new->application = strdup(oops->application);
	else
		new->application = NULL;
	if (oops->text)
		new->text = strdup(oops->text);
	else
		new->text = NULL;
	new->filename = strdup(oops->filename);
	if (oops->detail_filename)
		new->detail_filename = strdup(oops->detail_filename);
	else
		new->detail_filename = NULL;
	queued_backtraces = new;
	pthread_mutex_unlock(&queued_bt_mtx);
	g_hash_table_insert(core_status.queued_oops, new->filename, new->filename);
}

/*
 * For testmode to display all oops that would
 * be submitted.
 *
 * Picks up and sets down the processing_mtx.
 * Picks up and sets down the queued_bt_mtx.
 * Expects the queued_mtx to be held.
 */
static void print_queue(void)
{
	struct oops *oops = NULL, *next = NULL, *queue = NULL;
	int count = 0;

	pthread_mutex_lock(&queued_bt_mtx);
	queue = queued_backtraces;
	queued_backtraces = NULL;
	barrier();
	oops = queue;
	while (oops) {
		fprintf(stderr, "+ Submit text is:\n---[start of oops]---\n%s\n---[end of oops]---\n", oops->text);
		next = oops->next;
		FREE_OOPS(oops);
		oops = next;
		count++;
	}
	pthread_mutex_unlock(&queued_bt_mtx);
	pthread_mutex_lock(&core_status.processing_mtx);
	g_hash_table_remove_all(core_status.processing_oops);
	pthread_mutex_unlock(&core_status.processing_mtx);

	g_hash_table_remove_all(core_status.queued_oops);
}

static void write_logfile(int count, char *wsubmit_url)
{
	openlog("corewatcher", 0, LOG_KERN);
	syslog(LOG_WARNING, "Submitted %i coredump signatures to %s", count, wsubmit_url);
	closelog();
}

static size_t writefunction( void *ptr, size_t size, size_t nmemb, void __attribute((unused)) *stream)
{
	char *c = NULL, *c1 = NULL, *c2 = NULL;
	c = malloc(size * nmemb + 1);
	if (!c)
		return -1;

	memset(c, 0, size * nmemb + 1);
	memcpy(c, ptr, size * nmemb);
	printf("received %s \n", c);
	c1 = strstr(c, "201 ");
	if (c1) {
		c1 += 4;
		if (c1 >= c + strlen(c)) {
			free(c);
			return -1;
		}
		c2 = strchr(c1, '\n');
		if (c2) *c2 = 0;
		strncpy(result_url, c1, 4095);
	}
	free(c);
	return size * nmemb;
}

/*
 * Replace the extension of a file.
 * TODO: make search from end of string
 */
char *replace_name(char *filename, char *replace, char *new)
{
	char *newfile = NULL, *oldfile, *c = NULL;
	int r = 0;

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

	r = asprintf(&newfile, "%s%s",  oldfile, new);
	if(r == -1) {
		free(oldfile);
		return NULL;
	} else if (((unsigned int)r) != strlen(oldfile) + strlen(new)) {
		free(oldfile);
		free(newfile);
		return NULL;
	}

	free(oldfile);

	return newfile;
}

/*
 * Attempts to send the oops queue to the submission url wsubmit_url,
 * will use proxy if configured.
 *
 * Picks up and sets down the processing_mtx.
 * Expects queued_mtx to be held.
 */
static void submit_queue_with_url(struct oops *queue, char *wsubmit_url, char *proxy)
{
	int result = 0;
	struct oops *oops = NULL;
	CURL *handle = NULL;
	int count = 0;

	handle = curl_easy_init();
	curl_easy_setopt(handle, CURLOPT_URL, wsubmit_url);
	if (proxy)
		curl_easy_setopt(handle, CURLOPT_PROXY, proxy);

	oops = queue;
	while (oops) {
		struct curl_httppost *post = NULL;
		struct curl_httppost *last = NULL;

		/* set up the POST data */
		curl_formadd(&post, &last,
			CURLFORM_COPYNAME, "data",
			CURLFORM_COPYCONTENTS, oops->text, CURLFORM_END);

		if (allow_distro_to_pass_on) {
			curl_formadd(&post, &last,
				CURLFORM_COPYNAME, "pass_on_allowed",
				CURLFORM_COPYCONTENTS, "yes", CURLFORM_END);
		}

		curl_easy_setopt(handle, CURLOPT_HTTPPOST, post);
		curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunction);
		result = curl_easy_perform(handle);
		curl_formfree(post);

		if (!result) {
			char *nf = NULL;
			if (do_unlink || (!(nf = replace_name(oops->filename, ".processed", ".submitted")))) {
				unlink(oops->detail_filename);
				unlink(oops->filename);
			} else {
				rename(oops->filename, nf);
				pthread_mutex_lock(&core_status.processing_mtx);
				remove_pid_from_hash(oops->filename, core_status.processing_oops);
				pthread_mutex_unlock(&core_status.processing_mtx);
				free(nf);
			}

			g_hash_table_remove(core_status.queued_oops, oops->filename);
			dbus_say_thanks(result_url);
			count++;
		} else {
			g_hash_table_remove(core_status.queued_oops, oops->filename);
			queue_backtrace(oops);
		}
		oops = oops->next;
	}

	curl_easy_cleanup(handle);

	if (count && !testmode)
		write_logfile(count, wsubmit_url);
}

/*
 * Entry function for submitting oops data.
 *
 * Picks up and sets down the queued_mtx.
 * Picks up and sets down the queued_bt_mtx.
 */
void submit_queue(void)
{
	int i = 0, n = 0, submit = 0;
	struct oops *queue = NULL, *oops = NULL, *next = NULL;
	CURL *handle = NULL;
	pxProxyFactory *pf = NULL;
	char **proxies = NULL;
	char *proxy = NULL;

	pthread_mutex_lock(&core_status.queued_mtx);

	if (!g_hash_table_size(core_status.queued_oops)) {
		pthread_mutex_unlock(&core_status.queued_mtx);
		return;
	}

	memset(result_url, 0, 4096);

	if (testmode) {
		print_queue();
		pthread_mutex_unlock(&core_status.queued_mtx);
		return;
	}

	pthread_mutex_lock(&queued_bt_mtx);
	queue = queued_backtraces;
	queued_backtraces = NULL;
	barrier();
	pthread_mutex_unlock(&queued_bt_mtx);

	pf = px_proxy_factory_new();
	handle = curl_easy_init();
	curl_easy_setopt(handle, CURLOPT_NOBODY, 1);
	curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5);

	for (i = 0; i < url_count; i++) {
		curl_easy_setopt(handle, CURLOPT_URL, submit_url[i]);
		if (pf)
			proxies = px_proxy_factory_get_proxies(pf, submit_url[i]);
		if (proxies) {
			proxy = proxies[0];
			curl_easy_setopt(handle, CURLOPT_PROXY, proxy);
		} else {
			proxy = NULL;
		}
		if (!curl_easy_perform(handle)) {
			submit_queue_with_url(queue, submit_url[i], proxy);
			submit = 1;
			for (n = 0; proxies[n]; n++)
				free(proxies[n]);
			free(proxies);
			break;
		}
		for (n = 0; proxies[n]; n++)
			free(proxies[n]);
		free(proxies);
	}

	px_proxy_factory_free(pf);

	if (submit) {
		oops = queue;
		while (oops) {
			next = oops->next;
			FREE_OOPS(oops);
			oops = next;
		}
	} else {
		pthread_mutex_lock(&queued_bt_mtx);
		queued_backtraces = queue;
		pthread_mutex_unlock(&queued_bt_mtx);
	}

	curl_easy_cleanup(handle);
	curl_global_cleanup();
	pthread_mutex_unlock(&core_status.queued_mtx);
}
