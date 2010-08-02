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
		sprintf(filename, "%s/%s", c1, fragment);
		if (!access(filename, X_OK)) {
			printf("Found %s\n", filename);
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
	static char core[PATH_MAX];

	memset(core, 0, sizeof(core));
	sprintf(command, "eu-readelf -n %s", corefile);
	file = popen(command, "r");
	if (!file)
		return NULL;

	while (!feof(file)) {
		if (getline(&line, &size, file) < 0)
			goto out;;
		if (strstr(line, "fname:"))
			break;
	}
	c = strstr(line,"psargs: ");
	if (!c)
		goto out;
	c += 8;
	c2 = strchr(c, ' ');
	if (c2)
		*c2 = 0;
	c2 = strchr(c, '\n');
	if (c2)
		*c2 = 0;
	strcpy(core, c);
	c2 = strchr(core, ' ');
	if (c2) *c2 = 0;
	fprintf(stderr,"+ causing app: %s\n", core);
out:
	pclose(file);
	free(line);
	return core;
}
