#define _GNU_SOURCE
/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "corewatcher.h"

char *find_executable(char *fragment)
{
	char *path, *c1, *c2;
	char *filename = NULL;

	fprintf(stderr, "+ Looking for %s\n", fragment);

	if (fragment == NULL || strlen(fragment) < 3)
		return NULL;

	/* Deal with absolute paths first */
	if (!access(fragment, X_OK)) {
		if (!(filename = strdup(fragment)))
			return NULL;
		return filename;
	}

	path = strdup(getenv("PATH"));

	c1 = path;
	while (c1 && strlen(c1)>0) {
		free(filename);
		filename = NULL;
		c2 = strchr(c1, ':');
		if (c2) *c2=0;
		if(asprintf(&filename, "%s/%s", c1, fragment) == -1)
			return NULL;
		if (!access(filename, X_OK)) {
			printf("+ Found %s\n", filename);
			free(path);
			return filename;
		}
		c1 = c2;
		if (c2) c1++;
	}
	free(path);
	free(filename);
	return NULL;
}

char *find_coredump(char *fullpath)
{
	char *line = NULL, *line_len = NULL, *c = NULL, *c2 = NULL;
	size_t size = 0;
	FILE *file = NULL;
	char *app = NULL, *command = NULL;

	if (asprintf(&command, "eu-readelf -n %s", fullpath) == -1)
		return NULL;

	file = popen(command, "r");
	if (!file) {
		free(command);
		return NULL;
	}
	free(command);

	while (!feof(file)) {
		if (getline(&line, &size, file) == -1)
			break;

		/* lines 4 chars and under won't have information we need */
		if (size < 5)
			continue;

		line_len = line + size;
		c = strstr(line,"psargs: ");
		if (c) {
			c += 8;
			if (c < line_len) {
				c2 = strchr(c, ' ');
				if (c2)
					*c2 = 0;
				c2 = strchr(c, '\n');
				if (c2)
					*c2 = 0;
				app = strdup(c);

				fprintf(stderr,"+ causing app: %s\n", app);
			}
		}

		c = strstr(line, "EUID: ");
		if (c) {
			c += 6;
			if (c < line_len) {
				sscanf(c, "%i", &uid);
				fprintf(stderr, "+ uid: %d\n", uid);
			}
		}

		c = strstr(line, "cursig: ");
		if (c) {
			c += 8;
			if (c < line_len) {
				sscanf(c, "%i", &sig);
				fprintf(stderr, "+ sig: %d\n", sig);
			}
		}
	}

	pclose(file);
	free(line);

	return app;
}
