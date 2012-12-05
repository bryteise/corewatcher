#define _GNU_SOURCE
/*
 * Core dump watcher & collector
 *
 * (C) 2009-2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * Authors:
 *	Arjan van de Ven <arjan@linux.intel.com>
 *	Auke Kok <auke-jan.h.kok@intel.com>
 *	William Douglas <william.douglas@intel.com>
 *	Tim Pepper <timothy.c.pepper@linux.intel.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>

#include "corewatcher.h"

/* Attempt to find a file which would match the path/file fragment and
 * possibly represent a system binary.  We'll get more checking later when
 * we call gdb.  This is just a best effort candidate executable file to
 * hand to gdb along with a core. */
char *find_apppath(char *fragment)
{
	char *path = "/usr/bin";         /* ':' sep'd system path */
	char *c1, *c2;
	char *filename = NULL;
	char *candidate = NULL;
	char *apppath = NULL;
	int stripped = 0;

	fprintf(stderr, "+ Looking for %s\n", fragment);

	/* explicit absolute path in the standard directory */
	if (!strncmp(fragment, "/usr/bin", 8)) {
		if (!access(fragment, X_OK)) {
			fprintf(stderr, "+  found system executable %s\n", fragment);
			return strdup(fragment);
		}
		fprintf(stderr, "+  can't access system executable %s\n", fragment);
		return NULL;
	}

	/* explicit absolute path not in the standard directory */
	if (!strncmp(fragment, "/", 1)) {
		fprintf(stderr, "+  bad absolute path %s\n", fragment);
		return NULL;
	}

        /* relative path: problematic as we cannot at this point know the
	 * $PATH from the environment when the crash'd program started */
	if (strchr(fragment, '/')) {
	        candidate = strip_directories(fragment);
		fprintf(stderr, "+  stripped %s to %s\n", fragment, candidate);
		if (candidate == NULL)
			return NULL;
		stripped = 1;
	} else {
		/* fragment is just an executable's name */
		candidate = fragment;
	}

	/* search the path */
	c1 = path;
	while (c1 && strlen(c1)>0) {
		free(filename);
		filename = NULL;
		c2 = strchr(c1, ':');
		if (c2) *c2=0;
		fprintf(stderr, "+  looking in %s\n", c1);
		if(asprintf(&filename, "%s/%s", c1, candidate) == -1) {
			apppath = NULL;
			free(filename);
			goto out;
		}
		if (!access(filename, X_OK)) {
			printf("+  found %s\n", filename);
			apppath = filename;
			goto out;
		}
		c1 = c2;
		if (c2) c1++;
	}
out:
	if (stripped)
		free(candidate);
	return apppath;
}

char *find_causingapp(char *fullpath)
{
	char *line = NULL, *line_len = NULL, *c = NULL, *c2 = NULL;
	size_t size = 0;
	FILE *file = NULL;
	char *app = NULL, *command = NULL;

	if (asprintf(&command, "eu-readelf -n '%s'", fullpath) == -1)
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
				int uid;
				sscanf(c, "%i", &uid);
				fprintf(stderr, "+ uid: %d\n", uid);
			}
		}

		c = strstr(line, "cursig: ");
		if (c) {
			c += 8;
			if (c < line_len) {
				int sig;
				sscanf(c, "%i", &sig);
				fprintf(stderr, "+ sig: %d\n", sig);
			}
		}
	}

	pclose(file);
	free(line);

	return app;
}
