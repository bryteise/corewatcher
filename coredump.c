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
 * 	Arjan van de Ven <arjan@linux.intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "corewatcher.h"


#define MAX(A,B) ((A) > (B) ? (A) : (B))


char *extract_core(char *corefile)
{
	char *command = NULL, *c1 = NULL, *c2 = NULL, *line, *c3;
	char *appfile;
	FILE *file;

	appfile = find_executable(find_coredump(corefile));
	if (!appfile)
		return NULL;

	if (asprintf(&c1, "Program: %s\n", appfile) < 0)
		return NULL;

	if (asprintf(&command, "LANG=C gdb --batch -f %s %s -x /var/lib/corewatcher/gdb.command 2> /dev/null", appfile, corefile) < 0)
		return NULL;
		
	file = popen(command, "r");
	while (!feof(file)) {
		size_t size = 0;
		int ret;

		c2 = c1;
		line = NULL;
		ret = getline(&line, &size, file);	
		if (!size)
			break;
		if (ret < 0)
			break;

		if (strstr(line, "no debugging symbols found")) {
			free(line);
			continue;
		}
		if (strstr(line, "Core was generated by `")) {
			free(line);
			continue;
		}
		if (strstr(line,              "Program terminated with signal")) {
			c3 = strchr(line, ',');
			if (c3)
				sprintf(line, "Type:%s", c3+1);
		}

		if (c1) {
			c1 = NULL;
			if (asprintf(&c1, "%s%s", c2, line) < 0)
				continue;
			free(c2);
		} else {
			c1 = NULL;
			if (asprintf(&c1, "%s", line) < 0)
				continue;
		}
		free(line);
	}
	pclose(file);
	free(command);
	return c1;
}

void process_corefile(char *filename)
{
	char *ptr;
	ptr = extract_core(filename);

	if (!ptr)
		return;

	queue_backtrace(ptr);
	printf("-%s-\n", ptr);
	unlink(filename);

	free(ptr);
}


int scan_dmesg(void __unused *unused)
{
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX*2];

	dir = opendir("/var/cores/");
	if (!dir)
		return 1;

	printf("Scanning..\n");
	do {
		entry = readdir(dir);
		if (!entry)
			break;
		if (entry->d_name[0] == '.')
			continue;
		sprintf(path, "/var/cores/%s", entry->d_name);
		printf("Looking at %s\n", path);
		process_corefile(path);
	} while (entry);	
	
	if (opted_in >= 2)
		submit_queue();
	else if (opted_in >= 1)
		ask_permission();
	return 1;
}

