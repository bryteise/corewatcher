/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "corewatcher.h"

char *find_executable(char *fragment)
{
	/*
	 * Added path_filename to avoid writing over
	 * filename if this function is called twice.
	 * (When meego-tablet-wrapper is used)
	 */
	char path_filename[PATH_MAX*2];
	char *path, *c1, *c2;
	static char filename[PATH_MAX*2];

	fprintf(stderr, "+ Looking for %s\n", fragment);

	path = strdup(getenv("PATH"));

	if (strlen(fragment) < 3)
		return NULL;

	/* Deal with absolute paths first */
	if (!access(fragment, X_OK)) {
		strcpy(filename, fragment);
		return filename;
	}

	c1 = path;
	while (c1 && strlen(c1)>0) {
		c2 = strchr(c1, ':');
		if (c2) *c2=0;
		sprintf(path_filename, "%s/%s", c1, fragment);
		if (!access(path_filename, X_OK)) {
			printf("Found %s\n", path_filename);
			strcpy(filename, path_filename);
			return filename;
		}
		c1 = c2;
		if (c2) c1++;
	}
	return NULL;
}

char *find_coredump(char *corefile)
{
	char *line, *c, *c2;
	size_t size = 0;
	FILE *file = NULL;
	char command[PATH_MAX*2];
	char *app = NULL;

	sprintf(command, "eu-readelf -n %s", corefile);
	file = popen(command, "r");
	if (!file)
		return NULL;

	while (!feof(file)) {
		if (getline(&line, &size, file) < 0)
			break;

		c = strstr(line,"psargs: ");
		if (c) {
			c += 8;
			c2 = strchr(c, ' ');
			if (c2)
				*c2 = 0;
			c2 = strchr(c, '\n');
			if (c2)
				*c2 = 0;
			app = strdup(c);

			fprintf(stderr,"+ causing app: %s\n", app);
		}

		c = strstr(line, "EUID: ");
		if (c) {
			c += 6;
			sscanf(c, "%i", &uid);
			fprintf(stderr, "+ uid: %d\n", uid);
		}

		c = strstr(line, "cursig: ");
		if (c) {
			c += 8;
			sscanf(c, "%i", &sig);
			fprintf(stderr, "+ sig: %d\n", sig);
		}
	}

	pclose(file);
	free(line);

	return app;
}
