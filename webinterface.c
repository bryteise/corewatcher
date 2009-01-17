/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * Authors:
 * 	Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <glib.h>
#include <asm/unistd.h>

#include <curl/curl.h>

#include "coredumper.h"

extern char *submit_url;

void submit_queue(void)
{
	int result;
	int count = 0;
	GList *entry, *next;

	entry = g_list_first(coredumps);

	while (entry) {
		char *backtrace;
		CURL *handle;

		backtrace = entry->data;
		next = g_list_next(entry);

		coredumps = g_list_delete_link(coredumps, entry);
		entry = next;

		struct curl_httppost *post = NULL;
		struct curl_httppost *last = NULL;

		handle = curl_easy_init();

		printf("DEBUG SUBMIT URL is %s \n", submit_url);
		curl_easy_setopt(handle, CURLOPT_URL, submit_url);

		/* set up the POST data */
		curl_formadd(&post, &last,
			CURLFORM_COPYNAME, "backtrace",
			CURLFORM_COPYCONTENTS, backtrace, CURLFORM_END);

		curl_easy_setopt(handle, CURLOPT_HTTPPOST, post);
		result = curl_easy_perform(handle);

		curl_formfree(post);
		curl_easy_cleanup(handle);
		free(backtrace);
		count++;
	}

	if (count)
		dbus_say_thanks();
}

void clear_queue(void)
{
	GList *entry, *next;

	entry = g_list_first(coredumps);

	while (entry) {
		next = g_list_next(entry);
		coredumps = g_list_delete_link(coredumps, entry);
		entry = next;
	}
}

