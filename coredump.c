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
#include <glib.h>
#include <errno.h>

#include "corewatcher.h"

int do_unlink = 0;
int uid;
int sig;

#define MAX(A,B) ((A) > (B) ? (A) : (B))

long int get_time(char *filename)
{
	struct stat st;
	if (stat(filename, &st)) {
		return 0;
	}
	return st.st_mtim.tv_sec;
}

char *get_build(void) {
	FILE *file;
	char *line = NULL;
	size_t dummy;

	file = fopen(build_release, "r");
	if (!file) {
		line = strdup("Unknown");
		return line;
	}
	while (!feof(file)) {
		line = NULL;
		if (getline(&line, &dummy, file) <= 0)
			break;
		if (strstr(line, "BUILD") != NULL) {
			char *c, *build;

			c = strstr(line, "BUILD");
			c += 7;
			build = strdup(c);

			c = strchr(build, '\n');
			if (c) *c = 0;

			free(line);
			fclose(file);
			return build;
		} else {
			free(line);
		}
	}

	fclose(file);

	line = strdup("Unknown");

	return line;
}

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
	char *dline = NULL, *app = NULL, *ret = NULL, *abs_path = NULL;
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
	if (asprintf(&abs_path, "/usr/share/%s", app) < 0) {
		free(dline);
		return;
	}
	ret = find_executable(abs_path);
	free(dline);
	/*
	 * May have got NULL from find_executable if app
	 * isn't in /usr/share but that's okay as we
	 * don't change the filename in that case.
	 */
}

GList *get_core_file_list(char *appfile, char *dump_text)
{
	char *txt = NULL, *part = NULL;
	char delim[] = " ',`;\n\"";
	char *c;
	GList *files = NULL;

	if (!(txt = strdup(dump_text)))
		return NULL;

	part = strtok(txt, delim);
	while(part) {
		if (strstr(part, "/")) {
			if (!(c = strdup(part)))
				continue;
			files = g_list_prepend(files, c);
		}
		part = strtok(NULL, delim);
	}
	if ((c = strdup(appfile)))
		files = g_list_prepend(files, c);

	return files;
}

char *run_cmd(char *cmd)
{
	char *line = NULL, *str = NULL;
	char *c;
	FILE *file;
	int ret = 0;
	size_t size = 0;

	file = popen(cmd, "r");
	ret = getline(&line, &size, file);
	if (size || ret >= 0) {
		c = strchr(line, '\n');
		if (c) *c = 0;
		str = strdup(line);
	}
	free(line);
	pclose(file);

	return str;
}

char *lookup_part(char *check, char *line)
{
        char *c1, *c2;
        char *c = NULL;
        if (strncmp(check, line, strlen(check)))
                return NULL;
        if (!(c1 = strstr(line, ":")))
                return NULL;
        c1 += 2;
        if (!(c2 = strstr(c1, " ")))
                return NULL;
        *c2 = 0;
        if (!(c = strdup(c1)))
                return NULL;
        return c;
}

int append(char **s, char *e, char *o)
{
        char *t;
	if (!s || !(*s))
		return -1;
        t = *s;
        *s = NULL;
        if (asprintf(s, "%s%s%s", t, o, e) < 0) {
                *s = t;
                return -1;
        }
        free(t);
        return 0;
}

void build_times(char *cmd, GHashTable *ht_p2p, GHashTable *ht_p2d)
{
        FILE *file;
        int ret, i;
        char *line = NULL, *dline = NULL, *pack = NULL, *date = NULL;
        char *nm, *vr, *rl, *c, *p;
        size_t size = 0;
        char name[] = "Name";
        char version[] = "Version";
        char release[] = "Release";
        char delim[] = " ";

        file = popen(cmd, "r");
        while ((ret = getline(&line, &size, file)) >= 0 && size) {
                pack = nm = vr = rl = NULL;
                if (!(nm = lookup_part(name, line)))
                        goto cleanup;
                if ((ret = getline(&line, &size, file)) < 0 && !size) {
                        goto cleanup;
                }
                if (!(vr = lookup_part(version, line))) {
                        goto cleanup;
                }
                if ((ret = getline(&line, &size, file)) < 0 && !size) {
                        goto cleanup;
                }
                if (!(rl = lookup_part(release, line))) {
                        goto cleanup;
                }
                if (asprintf(&pack, "%s-%s-%s", nm, vr, rl) < 0) {
                        goto cleanup;
                }
		/* using p instead of pack to keep freeing the hashtables uniform */
		if (!(p = g_hash_table_lookup(ht_p2p, pack)))
			goto cleanup;

                while ((ret = getline(&dline, &size, file)) >= 0 && size) {
                        if (strncmp("*", dline, 1))
                                continue;
			/* twice to skip the leading '*' */
                        c = strtok(dline, delim);
			c = strtok(NULL, delim);
                        if (!(date = strdup(c))) {
                                goto cleanup;
			}
                        for (i = 0; i < 3; i++) {
                                c = strtok(NULL, delim);
                                if ((ret = append(&date, c, " ")) < 0)
                                        goto cleanup;
                        }
			g_hash_table_insert(ht_p2d, p, date);
			date = NULL;
			break;
                }
	cleanup:
                free(nm);
                free(vr);
                free(rl);
                free(pack);
		free(date);
        }
	pclose(file);
	free(dline);
	free(line);

        return;
}

char *get_package_info(char *appfile, char *dump_text)
{
	GList *l, *files = NULL, *hfiles = NULL, *tmpl = NULL;
	GHashTable *ht_f2f, *ht_f2p, *ht_p2p, *ht_p2d;
	char *c1, *cmd = NULL, *out = NULL;
	char find_pkg[] = "rpm -qf --queryformat \"%{NAME}-%{VERSION}-%{RELEASE}\" ";
	char find_date[] = "rpm -qi --changelog";
	char dev_null[] = "2>/dev/null";

	if (!(ht_f2f = g_hash_table_new(g_str_hash, g_str_equal)))
		return NULL;
	if (!(ht_f2p = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free)))
		return NULL;
	if (!(ht_p2p = g_hash_table_new(g_str_hash, g_str_equal)))
		return NULL;
	if (!(ht_p2d = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free)))
		return NULL;
	if (!(files = get_core_file_list(appfile, dump_text)))
		return NULL;

	/* get files in hash to remove duplicates */
	for (l = files; l; l = l->next)
		if (!g_hash_table_lookup(ht_f2f, l->data))
			g_hash_table_insert(ht_f2f, l->data, l->data);

	hfiles = g_hash_table_get_keys(ht_f2f);

	/* run through files one at a time in case some files don't have packages and it */
	/* isn't guaranteed we will see the error correspond with the file we are testing */
	for (l = hfiles; l; l = l->next) {
		if (asprintf(&cmd, "%s%s %s", find_pkg, (char *) l->data, dev_null) < 0)
			goto clean_up;
		c1 = run_cmd(cmd);
		free(cmd);
		cmd = NULL;

		if (c1 && strlen(c1) > 0)
			g_hash_table_insert(ht_f2p, l->data, c1);
		else {
			g_hash_table_insert(ht_f2p, l->data, NULL);
			free(c1);
		}
	}

	tmpl = g_hash_table_get_values(ht_f2p);
	for (l = tmpl; l; l = l->next) {
		if (l->data && !g_hash_table_lookup(ht_p2p, l->data))
			g_hash_table_insert(ht_p2p, l->data, l->data);
	}

	g_list_free(tmpl);
	tmpl = g_hash_table_get_keys(ht_p2p);
	if (asprintf(&cmd, "%s", find_date) < 0)
		goto clean_up;
	for (l = tmpl; l; l = l->next) {
		append(&cmd, l->data, " ");
	}
	g_list_free(tmpl);
	build_times(cmd, ht_p2p, ht_p2d);
	free(cmd);

	if (!(out = strdup("")))
		goto clean_up;
	for (l = hfiles; l; l = l->next) {
		if (append(&out, l->data, "") < 0)
			continue;

		if (!(c1 = g_hash_table_lookup(ht_f2p, l->data))) {
			if (append(&out, "\n", "") < 0)
				goto clean_out;
			continue;
		} else
			if (append(&out, c1, ":") < 0)
				goto clean_out;

		if (!(c1 = g_hash_table_lookup(ht_p2d, c1))) {
			if (append(&out, "\n", "") < 0)
				goto clean_out;
			continue;
		} else
			if (append(&out, c1, ":") < 0)
				goto clean_out;

		if (append(&out, "\n", "") < 0)
			goto clean_out;
	}
	goto clean_up;

clean_out:
	free(out);
	out = NULL;

clean_up:
	g_hash_table_destroy(ht_p2d);
	g_hash_table_destroy(ht_p2p);
	g_hash_table_destroy(ht_f2p);
	g_hash_table_destroy(ht_f2f);
	g_list_free_full(files, free);
	g_list_free(hfiles);
	g_list_free(tmpl);

	return out;
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
	char *build = get_build();
	char *release = get_release();
	char *kernel = get_kernel();
	long int time = get_time(corefile);

	ret = asprintf(&result,
		       "analyzer: corewatcher-gdb\n"
		       "coredump: %s\n"
		       "executable: %s\n"
		       "kernel: %s\n"
		       "reason: Process %s was killed by signal %d (%s)\n"
		       "release: %s\n"
		       "build: %s\n"
		       "time: %lu\n"
		       "uid: %d\n",
		       corefile,
		       appfile,
		       kernel,
		       appfile, sig, signame(sig),
		       release,
		       build,
		       time,
		       uid);

	free(kernel);
	free(release);
	free(build);

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

struct oops *extract_core(char *corefile)
{
	struct oops *oops;
	int ret;
	char *command = NULL, *h1 = NULL, *h2 = NULL, *c1 = NULL, *c2 = NULL, *line = NULL;
	char *coredump = NULL, *text = NULL;
	char *appfile;
	FILE *file;
	char *private = private_report ? "private: yes\n" : "";


	coredump = find_coredump(corefile);
	if (!coredump)
		return NULL;

	if (!(strcmp(coredump, "rpm") && strcmp(coredump, "gdb") && strcmp(coredump, "corewatcher")))
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
	h1 = build_core_header(appfile, corefile);

	file = popen(command, "r");
	while (!feof(file)) {
		size_t size = 0;

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
			/* keep going even if asprintf has errors */
			ret = asprintf(&c1, "%s", line);
		}
	}
	if (line)
		free(line);
	pclose(file);
	free(command);

	if (!(h2 = get_package_info(appfile, c1)))
		h2 = strdup("Unknown");

	ret = asprintf(&text,
		       "%s"
		       "package-info\n-----\n"
		       "%s"
		       "\n-----\n"
		       "%s"
		       "\nbacktrace\n-----\n"
		       "%s",
		       h1, h2, private, c1);
	if (ret < 0)
		text = NULL;
	free(h1);
	free(h2);
	free(c1);

	oops = malloc(sizeof(struct oops));
	if (!oops) {
		free(text);
		return NULL;
	}
	memset(oops, 0, sizeof(struct oops));
	oops->application = strdup(appfile);
	oops->text = text;
	oops->filename = strdup(corefile);
	return oops;
}

void write_core_detail_file(char *filename, char *text)
{
	int fd;
	char *pid;
	char detail_filename[8192];

	if (!(pid = strstr(filename, ".")))
		return;

	snprintf(detail_filename, 8192, "%s%s.txt", core_folder, ++pid);
	if ((fd = open(detail_filename, O_WRONLY | O_CREAT | O_TRUNC, 0)) != -1) {
		write(fd, text, strlen(text));
		fchmod(fd, 0644);
		close(fd);
	}
}

char *get_core_detail_filename(char *filename)
{
	char *pid, *c, *s;
	char *detail_filename;

	if (!(s = strstr(filename, ".")))
		return NULL;

	pid = strdup(++s);
	if (!(c = strstr(pid, "."))) {
		free(pid);
		return NULL;
	}

	*c = '\0';

	if ((asprintf(&detail_filename, "%s%s.txt", core_folder, pid)) < 0) {
		free(pid);
		return NULL;
	}

	free(pid);
	return detail_filename;
}

char *remove_directories(char *fullname)
{
	char *dfile = NULL, *c = NULL, *d = NULL;
	char delim[] = "/";

	dfile = strdup(fullname);
	if (!dfile)
		return NULL;

	c = strtok(dfile, delim);
	while(c) {
		d = c;
		c = strtok(NULL, delim);
	}
	d = strdup(d);
	free(dfile);

	return d;
}

void process_corefile(char *filename)
{
	struct oops *oops;
	char newfile[8192], *fn = NULL;

	oops = extract_core(filename);

	if (!oops) {
		unlink(filename);
		return;
	}

	if (!(fn = remove_directories(filename))) {
		FREE_OOPS(oops);
		return;
	}

	/* if this oops hasn't been processed before need to write details out and rename */
	if (!strstr(fn, ".processed")) {
		fprintf(stderr, "---[start of coredump]---\n%s\n---[end of coredump]---\n", oops->text);
		dbus_say_found(oops);

		if (!mkdir(core_folder, S_IRWXU | S_IRWXG | S_IRWXO) && errno != EEXIST) {
			free(fn);
			FREE_OOPS(oops);
			return;
		}
		/* try to write coredump text details to text file */
		write_core_detail_file(filename, oops->text);
		sprintf(newfile,"%s%s.processed", core_folder, fn);
		rename(filename, newfile);

	} else {
		/* backtrace queued only if files have been processed
		   to avoid putting a new coredump in twice */
		oops->detail_filename = get_core_detail_filename(fn);
		queue_backtrace(oops);
	}

	free(fn);
	FREE_OOPS(oops);
}


int scan_dmesg(void __unused *unused)
{
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX*2];
	struct oops *oq;

	dir = opendir("/tmp/");
	if (!dir)
		return 1;

	fprintf(stderr, "+ scanning /tmp/...\n");
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
	closedir(dir);

	dir = opendir(core_folder);
	if (!dir)
		return 1;

	fprintf(stderr, "+ scanning %s...\n", core_folder);
	do {
	skip:
		memset(path, 0, PATH_MAX*2);
		oq = get_oops_queue();
		entry = readdir(dir);
		if (!entry)
			break;
		if (!strstr(entry->d_name, ".processed"))
			continue;
		sprintf(path, "%s%s", core_folder, entry->d_name);
		while (oq) {
			if (!(strcmp(oq->filename, path)))
				goto skip;
			oq = oq->next;
		}
		fprintf(stderr, "+ Looking at %s\n", path);
		process_corefile(path);
	} while(1);
	closedir(dir);

	if (opted_in >= 2)
		submit_queue();
	else if (opted_in >= 1)
		ask_permission(core_folder);
	return 1;
}
