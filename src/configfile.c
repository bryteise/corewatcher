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
#include <glib.h>

#include "corewatcher.h"

/* 0 =  No
   1 =  Ask
   2 =  Yes
 */
int opted_in = 0;
int allow_distro_to_pass_on = 0;
char *submit_url[MAX_URLS];
char *build_release = NULL;
char *core_folder = NULL;
int url_count = 0;
int do_unlink = 0;
int private_report = 0;

void read_config_file(char *filename)
{
	FILE *file = NULL;
	char *line = NULL, *line_len = NULL;
	size_t dummy = 0;

	file = fopen(filename, "r");
	if (!file)
		return;
	while (!feof(file)) {
		char *c = NULL;
		char *n = NULL;

		if (getline(&line, &dummy, file) == -1)
			break;

		if (line[0] == '#')
			continue;

		/* we don't care about any lines that are too short to have config options */
		if (dummy < 5)
			continue;

		/* remove trailing\n */
		n = strchr(line, '\n');
		if (n) *n = 0;

		line_len = line + dummy;
		c = strstr(line, "allow-submit");
		if (c) {
			c += 13;
			if (c < line_len) {
				if (strstr(c, "yes"))
					opted_in = 2;
				if (strstr(c, "ask"))
					opted_in = 1;
			}
		}
		c = strstr(line, "allow-pass-on");
		if (c) {
			c += 14;
			if (c < line_len) {
				if (strstr(c, "yes"))
					allow_distro_to_pass_on = 1;
			}
		}
		c = strstr(line, "unlink");
		if (c)
			if (strstr(c, "yes"))
				do_unlink = 1;

		c = strstr(line, "submit-url");
		if (c && url_count <= MAX_URLS) {
			c += 11;
			if (c < line_len) {
				c = strstr(c, "http:");
				if (c) {
					submit_url[url_count] = strdup(c);
					if (!submit_url[url_count])
						submit_url[url_count] = NULL;
					else
						url_count++;
				}
			}
		}
		c = strstr(line, "release-info");
		if (c) {
			c += 11;
			if (c < line_len) {
				c = strstr(c, "/");
				if (c)
					build_release = strdup(c);
			}
		}
		c = strstr(line, "core-folder");
		if (c) {
			c += 11;
			if (c < line_len) {
				c = strstr(c, "/");
				if (c)
					core_folder = strdup(c);
			}
		}
		c = strstr(line, "private");
		if (c)
			if (strstr(c, "yes"))
				private_report = 1;
	}

	fclose(file);
	free(line);

	if (!build_release)
		build_release = strdup("/etc/meego-release");
	if (!url_count) {
		submit_url[url_count] = strdup("http://crashdb.meego.com/submitbug.php");
		if (!submit_url[url_count])
			submit_url[url_count] = NULL;
		else
			url_count++;
	}
	if (!core_folder)
		core_folder = strdup("/tmp/corewatcher/");
}