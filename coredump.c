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
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <signal.h>

#include "corewatcher.h"

int do_unlink = 0;
int uid;
int sig;
char *package;
char *component;
char *arch;

#define MAX(A,B) ((A) > (B) ? (A) : (B))

static char *get_release(void) {
	FILE *file;
	char *line = NULL;
	size_t dummy;

	file = fopen("/etc/issue", "r");
	if (!file) {
		line = strdup("Unknown");
		return line;
	}

	while (!feof(file)) {
		line = NULL;
		if (getline(&line, &dummy, file) <= 0)
			break;

		if (strstr(line, "release") != NULL) {
			char *c;

			c = strchr(line, '\n');
			if (c) *c = 0;

			fclose(file);
			return line;
		}
	}

	fclose(file);

	line = strdup("Unknown");

	return line;
}

void set_wrapped_app(char *line)
{
	char *dline = NULL, *app = NULL, *ret = NULL;
	char delim[] = " '";

	dline = strdup(line);

	app = strtok(dline, delim);
	while(app) {
		if (strcmp(app, "--app") == 0) {
			app = strtok(NULL, delim);
			break;
		}
		app = strtok(NULL, delim);
	}
	ret = find_executable(app);
	free(dline);
	/*
	 * May have got NULL from find_executable if app
	 * isn't on the path but it doesn't matter as we
	 * don't change the filename in that case.
	 */
}

void get_package_info(char *appfile) {
	char *command = NULL, *line = NULL;
	char *c;
	FILE *file;
	int ret = 0;
	size_t size = 0;

	if (asprintf(&command, "rpm -q --whatprovides %s --queryformat \"%%{NAME}-%%{VERSION}-%%{RELEASE}-%%{ARCH}\"", appfile) < 0) {
		package = strdup("Unknown");
	} else {
		file = popen(command, "r");
		free(command);
		ret = getline(&line, &size, file);
		if ((!size) || (ret < 0)) {
			package = strdup("Unknown");
		} else {
			c = strchr(line, '\n');
			if (c) *c = 0;
			package = strdup(line);
		}
		pclose(file);
	}

	if (asprintf(&command, "rpm -q --whatprovides %s --queryformat \"%%{NAME}\"", appfile) < 0) {
		component = strdup("Unknown");
	} else {
		file = popen(command, "r");
		free(command);
		ret = getline(&line, &size, file);
		if ((!size) || (ret < 0)) {
			component = strdup("Unknown");
		} else {
			c = strchr(line, '\n');
			if (c) *c = 0;
			component = strdup(line);
		}
		pclose(file);
	}

	if (asprintf(&command, "rpm -q --whatprovides %s --queryformat \"%%{ARCH}\"", appfile) < 0) {
		arch = strdup("Unknown");
	} else {
		file = popen(command, "r");
		free(command);
		ret = getline(&line, &size, file);
		if ((!size) || (ret < 0)) {
			arch = strdup("Unknown");
		} else {
			c = strchr(line, '\n');
			if (c) *c = 0;
			arch = strdup(line);
		}
		pclose(file);
	}
	free(line);
}

char *signame(int sig)
{
	switch(sig) {
	case SIGINT:  return "SIGINT";
	case SIGILL:  return "SIGILL";
	case SIGABRT: return "SIGABRT";
	case SIGFPE:  return "SIGFPE";
	case SIGSEGV: return "SIGSEGV";
	case SIGPIPE: return "SIGPIPE";
	case SIGBUS:  return "SIGBUS";
	default:      return strsignal(sig);
	}
	return NULL;
}

static char *get_kernel(void) {
	char *command = NULL, *line = NULL;
	FILE *file;
	int ret = 0;
	size_t size = 0;

	if (asprintf(&command, "uname -r") < 0) {
		line = strdup("Unknown");
		free(command);
		return line;
	}

	file = popen(command, "r");
	free(command);

	ret = getline(&line, &size, file);
	if ((!size) || (ret < 0)) {
		line = strdup("Unknown");
		return line;
	}

	size = strlen(line);
	line[size - 1] = '\0';

	pclose(file);
	return line;
}

char *build_core_header(char *appfile, char *corefile) {
	int ret = 0;
	char *result = NULL;
	char *release = get_release();
	char *kernel = get_kernel();
	struct timeval tv;

	gettimeofday(&tv, NULL);
	get_package_info(appfile);

	ret = asprintf(&result,
		       "analyzer: corewatcher-gdb\n"
		       "architecture: %s\n"
		       "component: %s\n"
		       "coredump: %s\n"
		       "executable: %s\n"
		       "kernel: %s\n"
		       "package: %s\n"
		       "reason: Process %s was killed by signal %d (%s)\n"
		       "release: %s\n"
		       "time: %lu\n"
		       "uid: %d\n"
		       "\nbacktrace\n-----\n",
		       arch,
		       component,
		       corefile,
		       appfile,
		       kernel,
		       package,
		       appfile, sig, signame(sig),
		       release,
		       tv.tv_sec,
		       uid);

	free(kernel);
	free(package);
	free(release);
	free(component);
	free(arch);

	if (ret < 0)
		result = strdup("Unknown");

	return result;
}

/*
 * Scan core dump in case a wrapper was used
 * to run the process and get the actual binary name
 */
void wrapper_scan(char *command)
{
	char *line = NULL;
	FILE *file;

	file = popen(command, "r");
	while (!feof(file)) {
		size_t size = 0;
		int ret;
		free(line);
		ret = getline(&line, &size, file);
		if (!size)
			break;
		if (ret < 0)
			break;

		if (strstr(line, "Core was generated by") &&
		    strstr(line, "--app")) {
			/* attempt to update appfile */
			set_wrapped_app(line);
			break;
		}
	}
	if (line)
		free(line);
	pclose(file);
}

char *extract_core(char *corefile)
{
	char *command = NULL, *c1 = NULL, *c2 = NULL, *line = NULL;
	char *coredump = NULL;
	char *appfile;
	FILE *file;

	coredump = find_coredump(corefile);
	if (!coredump)
		return NULL;

	appfile = find_executable(coredump);
	/* coredump no longer used, so free it as it was strdup'd */
	free(coredump);
	if (!appfile)
		return NULL;

	if (asprintf(&command, "LANG=C gdb --batch -f %s %s -x /var/lib/corewatcher/gdb.command 2> /dev/null", appfile, corefile) < 0) {
		free(command);
		return NULL;
	}

	wrapper_scan(command);
	c1 = build_core_header(appfile, corefile);

	file = popen(command, "r");
	while (!feof(file)) {
		size_t size = 0;
		int ret;

		c2 = c1;
		free(line);
		ret = getline(&line, &size, file);
		if (!size)
			break;
		if (ret < 0)
			break;

		if (strstr(line, "no debugging symbols found")) {
			continue;
		}
		if (strstr(line, "reason: ")) {
			continue;
		}

		if (c1) {
			c1 = NULL;
			if (asprintf(&c1, "%s%s", c2, line) < 0)
				continue;
			free(c2);
		} else {
			asprintf(&c1, "%s", line);
		}
	}
	if (line)
		free(line);
	pclose(file);
	free(command);
	return c1;
}

void write_core_detail_file(char *filename, char *text)
{
	int fd;
	char *pid;
	char detail_filename[8192];

	if (!(pid = strstr(filename, ".")))
		return;

	snprintf(detail_filename, 8192, "/tmp/%s.txt", ++pid);
	if ((fd = open(detail_filename, O_WRONLY | O_CREAT | O_TRUNC, 0)) != -1) {
		write(fd, text, strlen(text));
		fchmod(fd, 0644);
		close(fd);
	}
}

void process_corefile(char *filename)
{
	char *ptr;
	char newfile[8192];
	ptr = extract_core(filename);

	if (!ptr) {
		unlink(filename);
		free(ptr);
		return;
	}

	queue_backtrace(ptr);
	fprintf(stderr, "---[start of coredump]---\n%s\n---[end of coredump]---\n", ptr);

	/* try to write coredump text details to text file */
	write_core_detail_file(filename, ptr);

	sprintf(newfile,"%s.processed", filename);
	if (do_unlink)
		unlink(filename);
	else
		rename(filename, newfile);

	free(ptr);
}


int scan_dmesg(void __unused *unused)
{
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX*2];

	dir = opendir("/tmp/");
	if (!dir)
		return 1;

	fprintf(stderr, "+ scanning...\n");
	do {
		entry = readdir(dir);
		if (!entry)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (strstr(entry->d_name, "processed"))
			continue;
		if (strncmp(entry->d_name, "core.", 5))
			continue;
		sprintf(path, "/tmp/%s", entry->d_name);
		fprintf(stderr, "+ Looking at %s\n", path);
		process_corefile(path);
	} while (entry);

	if (opted_in >= 2)
		submit_queue();
	else if (opted_in >= 1)
		ask_permission();
	closedir(dir);
	return 1;
}
