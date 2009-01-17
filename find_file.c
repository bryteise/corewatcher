/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "coredumper.h"

char *find_executable(char *fragment)
{
	char *path, *c1, *c2;
	path = strdup(getenv("PATH"));
 	static char filename[PATH_MAX*2];

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
		if (c2) c2=0;
		sprintf(filename, "%s/%s", c1, fragment);
		if (!access(filename, X_OK))
			return filename;
		c1 = c2;
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
	sprintf(command, "file %s", corefile);
	file = popen(command, "r");
	if (!file)
		return NULL;
	if (getline(&line, &size, file) < 0)
		goto out;;
	c = strstr(line,"from '");
	if (!c)
		goto out;
	c += 6;
	c2 = strchr(c, '\'');
	if (!c2)
		goto out;
	*c2 = 0;
	strcpy(core, c);
out:
	pclose(file);
	free(line);
	return core;
}

