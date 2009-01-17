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
 * the Free Software Foundation; either version 3 of the License.
 */


#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/inotify.h>
#include <sys/types.h>

#include <dirent.h>


#include "coredumper.h"

int inotifd, inotify_descriptor;


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

	if (asprintf(&command, "LANG=C gdb --batch -f %s %s -x gdb.command 2> /dev/null", appfile, corefile) < 0)
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

	printf("-%s-\n", ptr);
	unlink(filename);

	free(ptr);
}

void wait_for_corefile(void)
{
	char buffer[8192];
	char fullpath[8192];
	struct inotify_event *event = (struct inotify_event *)&buffer;

	while (1) {
		if (read(inotifd, &buffer, 8192) < 0)
			return;
		sprintf(fullpath,"/var/cores/%s", event->name);
		process_corefile(fullpath);
	}

}

void clean_directory(void)
{
	DIR *dir;
	struct dirent *entry;
	char fullpath[8192];
 
	dir = opendir("/var/cores/");
	do {
		entry = readdir(dir);
		if (!entry)
			break;
		sprintf(fullpath, "/var/cores/%s", entry->d_name);
		process_corefile(fullpath);
	} while (entry);

	closedir(dir);
}

int main(int argc, char **argv)
{
	inotifd = inotify_init();
	if (inotifd < 0) {
		printf("No inotify support in the kernel... aborting\n");
		return EXIT_FAILURE;
	}
	inotify_descriptor = inotify_add_watch(inotifd, "/var/cores/", IN_CLOSE_WRITE);

	if (argc > 1) {
		process_corefile(argv[1]);
	} else {
		clean_directory();
		wait_for_corefile();
	}


	inotify_rm_watch(inotifd, inotify_descriptor);
	close(inotifd);
	return EXIT_SUCCESS;
}