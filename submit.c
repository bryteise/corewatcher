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
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>

#include <asm/unistd.h>

#include <proxy.h>
#include <curl/curl.h>

#include "corewatcher.h"

extern int do_unlink;


/*
 * we keep track of 16 checksums of the last submitted oopses; this allows us to
 * make sure we don't submit the same oops twice (this allows us to not have to do
 * really expensive things during non-destructive dmesg-scanning)
 *
 * This also limits the number of oopses we'll submit per session;
 * it's important that this is bounded to avoid feedback loops
 * for the scenario where submitting an oopses causes a warning/oops
 */
#define MAX_CHECKSUMS 16
static unsigned int checksums[MAX_CHECKSUMS];
static int submitted;

/* we queue up oopses, and then submit in a batch.
 * This is useful to be able to cancel all submissions, in case
 * we later find our marker indicating we submitted everything so far already
 * previously.
 */
static struct oops *queued_backtraces;
static int newoops;
static int unsubmittedoops;

struct oops *get_oops_queue(void)
{
	return queued_backtraces;
}

static unsigned int checksum(char *ptr)
{
	unsigned int temp = 0;
	unsigned char *c;
	c = (unsigned char *) ptr;
	while (c && *c) {
		temp = (temp) + *c;
		c++;
	}
	return temp;
}

void queue_backtrace(struct oops *oops)
{
	int i;
	unsigned int sum;
	struct oops *new;

	if (submitted >= MAX_CHECKSUMS-1)
		return;
	/* first, check if we haven't already submitted the oops */
	sum = checksum(oops->text);
	for (i = 0; i < submitted; i++) {
		if (checksums[i] == sum) {
			printf("Match with oops %i (%d)\n", i, sum);
			unlink(oops->filename);
			return;
		}
	}

	new = malloc(sizeof(struct oops));
	memset(new, 0, sizeof(struct oops));
	new->next = queued_backtraces;
	new->checksum = sum;
	new->application = strdup(oops->application);
	new->text = strdup(oops->text);
	new->filename = strdup(oops->filename);
	new->detail_filename = strdup(oops->detail_filename);
	queued_backtraces = new;
	unsubmittedoops = 1;
}

static void print_queue(void)
{
	struct oops *oops, *next;
	struct oops *queue;
	int count = 0;

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
}

static void write_logfile(int count, char *wsubmit_url)
{
	openlog("corewatcher", 0, LOG_KERN);
	syslog(LOG_WARNING, "Submitted %i coredump signatures to %s", count, wsubmit_url);
	closelog();
}

char result_url[4096];

size_t writefunction( void *ptr, size_t size, size_t nmemb, void __attribute((unused)) *stream)
{
	char *c, *c1, *c2;
	c = malloc(size*nmemb + 1);
	memset(c, 0, size*nmemb + 1);
	memcpy(c, ptr, size*nmemb);
	printf("received %s \n", c);
	c1 = strstr(c, "201 ");
	if (c1) {
		c1+=4;
		c2 = strchr(c1, '\n');
		if (c2) *c2 = 0;
		strncpy(result_url, c1, 4095);
	}
	free(c);
	return size * nmemb;
}

void submit_queue_with_url(struct oops *queue, char *wsubmit_url, char *proxy)
{
	int result;
	struct oops *oops;
	CURL *handle;
	int count = 0;

	handle = curl_easy_init();
	curl_easy_setopt(handle, CURLOPT_URL, wsubmit_url);
	if (proxy)
		curl_easy_setopt(handle, CURLOPT_PROXY, proxy);

	oops = queue;
	while (oops) {
		struct curl_httppost *post = NULL;
		struct curl_httppost *last = NULL;
		unsigned int sum;
		int i;

		sum = oops->checksum;
		for (i = 0; i < submitted; i++) {
			if (checksums[i] == sum) {
				printf("Match with oops %i (%d)\n", i, sum);
				unlink(oops->filename);
				goto dup;
			}
		}

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
			char newfile[8192];
			char oldfile[8192];
			char *c;

			memset(newfile, 0, sizeof(newfile));
			memset(oldfile, 0, sizeof(oldfile));

			strncpy(oldfile, oops->filename, 8192);
			oldfile[8191] = '\0';
			c = strstr(oldfile, ".processed");
			if (c) {
				oldfile[strlen(oldfile) - strlen(c)] = '\0';
			}

			sprintf(newfile,"%s.submitted",  oldfile);

			if (do_unlink) {
				unlink(oops->detail_filename);
				unlink(oops->filename);
			}
			else
				rename(oops->filename, newfile);

			checksums[submitted++] = oops->checksum;
			dbus_say_thanks(result_url);
		} else
			queue_backtrace(oops);

		count++;
	dup:
		oops = oops->next;
	}

	curl_easy_cleanup(handle);

	if (count && !testmode)
		write_logfile(count, wsubmit_url);

	/*
	 * If we've reached the maximum count, we'll exit the program,
	 * the program won't do any useful work anymore going forward.
	 */
	if (submitted >= MAX_CHECKSUMS-1) {
		exit(EXIT_SUCCESS);
	}
}

void submit_queue(void)
{
	int i, n, submitted = 0;
	struct oops *queue, *oops, *next;
	CURL *handle;
	pxProxyFactory *pf;
	char **proxies = NULL;
	char *proxy = NULL;

	memset(result_url, 0, 4096);

	if (testmode) {
		print_queue();
		return;
	}

	queue = queued_backtraces;
	queued_backtraces = NULL;
	barrier();

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
			submitted = 1;
			break;
		}
		for (n = 0; proxies[n]; n++)
			free(proxies[n]);
		free(proxies);
	}

	px_proxy_factory_free(pf);

	if (submitted) {
		oops = queue;
		while (oops) {
			next = oops->next;
			FREE_OOPS(oops);
			oops = next;
		}
	} else
		queued_backtraces = queue;

	curl_easy_cleanup(handle);
}

void clear_queue(void)
{
	struct oops *oops, *next;
	struct oops *queue;

	queue = queued_backtraces;
	queued_backtraces = NULL;
	barrier();
	oops = queue;
	while (oops) {
		next = oops->next;
		FREE_OOPS(oops);
		oops = next;
	}
	write_logfile(0, "Unknown");
}

void ask_permission(char *detail_folder)
{
	if (!newoops && !pinged && !unsubmittedoops)
		return;
	pinged = 0;
	newoops = 0;
	unsubmittedoops = 0;
	if (queued_backtraces) {
		dbus_ask_permission(detail_folder);
	}
}
