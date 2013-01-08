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

/*
 * Attempt to find a file which would match the application name and
 * possibly represent a system binary.  We'll get more checking later when
 * we call gdb.  This is just a best effort candidate executable file to
 * hand to gdb along with a core.
 */
char *find_apppath(char *appname)
{
	/* ':' sep'd system path */
	char *path = "/usr/bin:/usr/sbin:/bin:/sbin";
	char *c1, *c2;
	char *filename = NULL;
	char *apppath = NULL;

	fprintf(stderr, "+ Looking for %s\n", appname);

	/* search the path */
	c1 = path;
	while (c1 && strlen(c1)>0) {
		free(filename);
		filename = NULL;
		c2 = strchr(c1, ':');
		if (c2) *c2=0;
		fprintf(stderr, "+  looking in %s\n", c1);
		if(asprintf(&filename, "%s/%s", c1, appname) == -1) {
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
	return apppath;
}

/*
 * Attempt to find application name from the core file name.
 */
char *find_causingapp(char *fullpath)
{
	char *fp = NULL, *c1 = NULL, *c2 = NULL;
	char *app = NULL;

	fp = strdup(fullpath);
	if (!fp)
		goto no_app_name;

	/*
	 * looking for application name from a string of the form:
	 * "/path/to/core_appname_timestamp.pid.extension"
	 */
	c1 = strrchr(fp, '_');
	if (!c1)
		goto no_app_name;
	*c1 = 0;
	c1 = strrchr(fp, '/');
	if (!c1)
		goto no_app_name;
	c2 = c1 + 1;
	if (!c2)
		goto no_app_name;
	c1 = strchr(c2, '_');
	if (!c1)
		goto no_app_name;
	*c1 = 0;
	app = strdup(c1+1);

no_app_name:
	free(fp);
	return app;
}
