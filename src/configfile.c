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
#include <glib.h>

#include "corewatcher.h"

char *submit_url[MAX_URLS];
int url_count = 0;

void read_config_file(char *filename)
{
	FILE *file = NULL;
	char *line = NULL, *line_end = NULL;
	size_t line_len = 0;

	file = fopen(filename, "r");
	if (!file)
		return;
	while (!feof(file)) {
		char *c = NULL;
		char *n = NULL;

		if (getline(&line, &line_len, file) == -1)
			break;

		if (line[0] == '#')
			continue;

		/* we don't care about any lines that are too short to have config options */
		if (line_len < 5)
			continue;

		/* remove trailing\n */
		n = strchr(line, '\n');
		if (n) *n = 0;

		line_end = line + line_len;

		c = strstr(line, "allow-submit");
		if (c) {
			c+=13;
			if (c < line_end) {
				if (!strstr(c, "yes")) {
					printf("Error: allow-submit!=yes in config file.\n");
					printf("Corewatcher will not run.\n");
					exit(-1);
				}
			}
		}

		c = strstr(line, "allow-pass-on");
		if (c) {
			c+=14;
			if (c < line_end) {
				if (!strstr(c, "yes")) {
					printf("Error: allow-pass-on!=yes in config file.\n");
					printf("Corewatcher will not run.\n");
					exit(-1);
				}
			}
		}

		c = strstr(line, "submit-url");
		if (c && url_count <= MAX_URLS) {
			c += 11;
			if (c < line_end) {
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
	}

	fclose(file);
	free(line);

	if (!url_count) {
		submit_url[url_count] = strdup("http://kojibuild7.jf.intel.com/crash_submit/");
		if (!submit_url[url_count])
			submit_url[url_count] = NULL;
		else
			url_count++;
	}
}
