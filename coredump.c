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
 *	William Douglas <william.douglas@intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <signal.h>
#include <glib.h>
#include <errno.h>

#include "corewatcher.h"

int uid = 0;
int sig = 0;

/* Always pick up the processing_mtx and then the
   processing_queue_mtx, reverse for setting down */
/* Always pick up the gdb_mtx and then the
   processing_queue_mtx, reverse for setting down */
/* Always pick up the processing_mtx and then the
   gdb_mtx, reverse for setting down */
/* so order for pick up should be:
   processing_mtx -> gdb_mtx -> processing_queue_mtx
   and the reverse for setting down */
static pthread_mutex_t processing_queue_mtx = PTHREAD_MUTEX_INITIALIZER;
static char *processing_queue[MAX_PROCESSING_OOPS];
static pthread_mutex_t gdb_mtx = PTHREAD_MUTEX_INITIALIZER;
static int tail = 0;
static int head = 0;

static long int get_time(char *filename)
{
	struct stat st;
	if (stat(filename, &st)) {
		return 0;
	}
	return st.st_mtim.tv_sec;
}

static char *get_build(void)
{
	FILE *file = NULL;
	char *line = NULL, *c = NULL, *build = NULL;
	size_t dummy = 0;

	file = fopen(build_release, "r");
	if (!file) {
		line = strdup("Unknown");
		return line;
	}

	while (!feof(file)) {
		if (getline(&line, &dummy, file) == -1)
			break;
		if ((c = strstr(line, "BUILD"))) {
			c += 7;
			if (c >= line + strlen(line))
				break;

			/* glibc does things that scare valgrind */
			/* ignore valgrind error for the line below */
			build = strdup(c);
			if (!build)
				break;

			c = strchr(build, '\n');
			if (c) *c = 0;

			free(line);
			fclose(file);
			return build;
		}
	}

	fclose(file);
	free(line);

	line = strdup("Unknown");

	return line;
}

static char *get_release(void)
{
	FILE *file = NULL;
	char *line = NULL;
	size_t dummy = 0;

	file = fopen("/etc/issue", "r");
	if (!file) {
		line = strdup("Unknown");
		return line;
	}

	while (!feof(file)) {
		if (getline(&line, &dummy, file) == -1)
			break;
		if (strstr(line, "release")) {
			char *c = NULL;

			c = strchr(line, '\n');
			if (c) *c = 0;

			fclose(file);
			return line;
		}
	}

	fclose(file);
	free(line);

	line = strdup("Unknown");

	return line;
}

static char *set_wrapped_app(char *line)
{
	char *dline = NULL, *app = NULL, *appfile = NULL, *abs_path = NULL;
	char delim[] = " '";
	char app_folder[] = "/usr/share/";
	int r = 0;

	if (!line)
		return NULL;

	dline = strdup(line);

	app = strtok(dline, delim);
	while(app) {
		if (strcmp(app, "--app") == 0) {
			app = strtok(NULL, delim);
			break;
		}
		app = strtok(NULL, delim);
	}
	if (!app)
		goto cleanup_set_wrapped_app;
	r = asprintf(&abs_path, "%s%s", app_folder, app);
	if (r == -1) {
		abs_path = NULL;
		goto cleanup_set_wrapped_app;
	} else if (((unsigned int)r) != strlen(app_folder) + strlen(app)) {
		goto cleanup_set_wrapped_app;
	}

	appfile = find_executable(abs_path);

cleanup_set_wrapped_app:
	free(abs_path);
	free(dline);
	return appfile;
}

static GList *get_core_file_list(char *appfile, char *dump_text)
{
	char *txt = NULL, *part = NULL, *c = NULL;
	char delim[] = " ',`;\n\"";
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
	if ((c = strdup(appfile))) {
		files = g_list_prepend(files, c);
	}

	free(txt);
	return files;
}

static char *run_cmd(char *cmd)
{
	char *line = NULL, *str = NULL;
	char *c = NULL;
	FILE *file = NULL;
	size_t size = 0;

	if (!cmd)
		return NULL;
	file = popen(cmd, "r");
	if (!file)
		return NULL;

	if (getline(&line, &size, file) != -1) {
		c = strchr(line, '\n');
		if (c) *c = 0;
		str = strdup(line);
	}
	free(line);
	pclose(file);

	return str;
}

static char *lookup_part(char *check, char *line)
{
	char *c = NULL, *c1 = NULL, *c2 = NULL;

	if (!check || !line)
		return NULL;

	if (strncmp(check, line, strlen(check)))
		return NULL;
	if (!(c1 = strstr(line, ":")))
		return NULL;
	c1 += 2;
	if (c1 >= line + strlen(line))
		return NULL;
	if (!(c2 = strstr(c1, " ")))
		return NULL;
	*c2 = 0;
	if (!(c = strdup(c1)))
		return NULL;
	return c;
}

static int append(char **s, char *e, char *a)
{
	char *t = NULL;
	int r = 0;

	if (!s || !(*s) || !e || !a)
		return -1;
	t = *s;
	*s = NULL;
	r = asprintf(s, "%s%s%s", t, a, e);
	if (r == -1) {
		*s = t;
		return -1;
	} else if (((unsigned int)r) != strlen(t) + strlen(a) + strlen(e)) {
		free(*s);
		*s = t;
		return -1;
	}
	free(t);
	return 0;
}

static void build_times(char *cmd, GHashTable *ht_p2p, GHashTable *ht_p2d)
{
	FILE *file = NULL;
	int ret = 0, i = 0;
	char *line = NULL, *dline = NULL, *pack = NULL, *date = NULL;
	char *nm = NULL, *vr = NULL, *rl = NULL, *c = NULL, *p = NULL;
	size_t size = 0;
	char name[] = "Name";
	char version[] = "Version";
	char release[] = "Release";
	char delim[] = " ";

	file = popen(cmd, "r");
	if (!file)
		return;
	while (!feof(file)) {
		pack = nm = vr = rl = NULL;
		if (getline(&line, &size, file) == -1)
			goto cleanup;
		if (!(nm = lookup_part(name, line)))
			goto cleanup;
		if (getline(&line, &size, file) == -1)
			goto cleanup;
		if (!(vr = lookup_part(version, line)))
			goto cleanup;
		if (getline(&line, &size, file) == -1)
			goto cleanup;
		if (!(rl = lookup_part(release, line)))
			goto cleanup;
		ret = asprintf(&pack, "%s-%s-%s", nm, vr, rl);
		if (ret == -1)
			goto cleanup;
		else if (((unsigned int)ret) != strlen(nm) + strlen(vr) + strlen(rl) + 2)
			goto cleanup;
		/* using p instead of pack to keep freeing the hashtables uniform */
		if (!(p = g_hash_table_lookup(ht_p2p, pack)))
			goto cleanup;

		while (!feof(file)) {
			c = NULL;
			if (getline(&dline, &size, file) == -1)
				goto cleanup;
			if (strncmp("*", dline, 1))
				continue;
			/* twice to skip the leading '*' */
			c = strtok(dline, delim);
			if (!c)	continue;
			c = strtok(NULL, delim);
			if (!c) continue;

			if (!(date = strdup(c)))
				goto cleanup;

			for (i = 0; i < 3; i++) {
				c = strtok(NULL, delim);
				if (!c) goto cleanup;
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

static char *get_package_info(char *appfile, char *dump_text)
{
	GList *l = NULL, *files = NULL, *hfiles = NULL, *tmpl = NULL;
	GHashTable *ht_f2f = NULL, *ht_f2p = NULL, *ht_p2p = NULL, *ht_p2d = NULL;
	char *c1 = NULL, *cmd = NULL, *out = NULL;
	char find_pkg[] = "rpm -qf --queryformat \"%{NAME}-%{VERSION}-%{RELEASE}\" ";
	char find_date[] = "rpm -qi --changelog";
	char dev_null[] = "2>/dev/null";
	int r = 0;

	if (!(ht_f2f = g_hash_table_new(g_str_hash, g_str_equal)))
		goto clean_up;
	if (!(ht_f2p = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free)))
		goto clean_up;
	if (!(ht_p2p = g_hash_table_new(g_str_hash, g_str_equal)))
		goto clean_up;
	if (!(ht_p2d = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free)))
		goto clean_up;
	if (!(files = get_core_file_list(appfile, dump_text)))
		goto clean_up;

	/* get files in hash to remove duplicates */
	for (l = files; l; l = l->next) {
		if (!g_hash_table_lookup(ht_f2f, l->data))
			g_hash_table_insert(ht_f2f, l->data, l->data);
	}

	hfiles = g_hash_table_get_keys(ht_f2f);

	/* run through files one at a time in case some files don't have packages and it */
	/* isn't guaranteed we will see the error correspond with the file we are testing */
	for (l = hfiles; l; l = l->next) {
		r = asprintf(&cmd, "%s%s %s", find_pkg, (char *)l->data, dev_null);
		if (r == -1)
			goto clean_up;
		else if (((unsigned int)r) != sizeof(find_pkg) + sizeof((char *)l->data) + sizeof(dev_null) + 1) {
			free(cmd);
			goto clean_up;
		}
		c1 = run_cmd(cmd);
		free(cmd);
		cmd = NULL;

		if (c1 && strlen(c1) > 0) {
			g_hash_table_insert(ht_f2p, l->data, c1);
		} else {
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
	tmpl = NULL;
	tmpl = g_hash_table_get_keys(ht_p2p);
	cmd = strdup(find_date);
	if (!cmd)
		goto clean_up;
	for (l = tmpl; l; l = l->next) {
		append(&cmd, l->data, " ");
	}
	g_list_free(tmpl);
	tmpl = NULL;
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
	if (ht_p2d)
		g_hash_table_destroy(ht_p2d);
	if (ht_p2p)
		g_hash_table_destroy(ht_p2p);
	if (ht_f2p)
		g_hash_table_destroy(ht_f2p);
	if (ht_f2f)
		g_hash_table_destroy(ht_f2f);
	if (files)
		g_list_free_full(files, free);
	if (hfiles)
		g_list_free(hfiles);
	if (tmpl)
		g_list_free(tmpl);

	return out;
}

static char *signame(int sig)
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

static char *get_kernel(void)
{
	char *line = NULL;
	FILE *file = NULL;
	int ret = 0;
	size_t size = 0;
	char command[] = "uname -r";

	file = popen(command, "r");

	if (!file) {
		line = strdup("Unknown");
		return line;
	}

	ret = getline(&line, &size, file);
	if (!size || ret <= 0) {
		pclose(file);
		line = strdup("Unknown");
		return line;
	}

	size = strlen(line);
	line[size - 1] = '\0';

	pclose(file);
	return line;
}

static char *build_core_header(char *appfile, char *corefile, char * processed_fullpath)
{
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
		       processed_fullpath,
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

	if (ret == -1)
		result = strdup("Unknown");

	return result;
}

/*
 * Scan core dump in case a wrapper was used
 * to run the process and get the actual binary name
 */
static char *wrapper_scan(char *command)
{
	char *line = NULL, *appfile = NULL;
	FILE *file = NULL;

	file = popen(command, "r");
	if (!file)
		return NULL;

	while (!feof(file)) {
		size_t size = 0;
		int ret = 0;
		free(line);
		ret = getline(&line, &size, file);
		if (!size)
			break;
		if (ret < 0)
			break;

		if (strstr(line, "Core was generated by") &&
		    strstr(line, "--app")) {
			/* attempt to update appfile */
			appfile = set_wrapped_app(line);
			break;
		}
	}
	if (line)
		free(line);
	pclose(file);

	return appfile;
}

/*
 * Strip the directories from the path
 * given by fullname
 */
char *strip_directories(char *fullpath)
{
	char *dfile = NULL, *c1 = NULL, *c2 = NULL, *r = NULL;
	char delim[] = "/";

	if (!fullpath)
		return NULL;

	dfile = strdup(fullpath);
	if (!dfile)
		return NULL;

	c1 = strtok(dfile, delim);
	while(c1) {
		c2 = c1;
		c1 = strtok(NULL, delim);
	}

	if (c2)
		r = strdup(c2);
	free(dfile);

	return r;
}

/*
 * Move corefile from /tmp to core_folder.
 * Add extension if given and attempt to create core_folder.
 */
int move_core(char *fullpath, char *extension)
{
	char *corefn = NULL, newpath[8192];

	if (!core_folder || !fullpath)
		return -1;

	if (!(corefn = strip_directories(fullpath))) {
		unlink(fullpath);
		return -ENOMEM;
	}

	if (!mkdir(core_folder, S_IRWXU | S_IRWXG | S_IRWXO)
	    && errno != EEXIST) {
		free(corefn);
		return -errno;
	}

	if (extension)
		snprintf(newpath, 8192, "%s%s.%s", core_folder, corefn, extension);
	else
		snprintf(newpath, 8192, "%s%s", core_folder, corefn);

	free(corefn);
	rename(fullpath, newpath);

	return 0;
}

/*
 * Finds the full path for the application that crashed,
 * and depending on what opted_in was configured as will:
 * opted_in 2 (always submit) -> move file to core_folder
 * to be processed further
 * opted_in 1 (ask user) -> ask user if we should submit
 * the crash and add to asked_oops hash so we don't get
 * called again for this corefile
 * opted_in 0 (don't submit) -> do nothing
 *
 * Picks up and sets down the asked_mtx.
 */
static char *get_appfile(char *fullpath)
{
	char *appname = NULL, *appfile = NULL;

	if (!fullpath)
		return NULL;

	appname = find_coredump(fullpath);
	if (!appname)
		return NULL;

	/* don't try and do anything for rpm, gdb or corewatcher crashes */
	if (!(strcmp(appname, "rpm") && strcmp(appname, "gdb") && strcmp(appname, "corewatcher")))
		return NULL;

	appfile = find_executable(appname);
	/* appname no longer used, so free it as it was strdup'd */
	free(appname);
	if (!appfile)
		return NULL;

	if (opted_in == 2) {
		dbus_say_found(fullpath, appfile);
		move_core(fullpath, "to-process");
	} else if (opted_in == 1) {
		char *fp = NULL, *af = NULL;
		if (!(fp = strdup(fullpath))) {
			free(appfile);
			return NULL;
		}
		if (!(af = strdup(appfile))) {
			free(fp);
			free(appfile);
			return NULL;
		}
		dbus_ask_permission(fullpath, appfile);
		/* If we got here the oops wasn't in the hash so add it */
		pthread_mutex_lock(&core_status.asked_mtx);
		g_hash_table_insert(core_status.asked_oops, fp, af);
		pthread_mutex_unlock(&core_status.asked_mtx);
	} else {
		free(appfile);
		return NULL;
	}

	return appfile;
}

/*
 * Use GDB to extract backtrace information from corefile
 */
static struct oops *extract_core(char *fullpath, char *appfile, char *processed_fullpath)
{
	struct oops *oops = NULL;
	int ret = 0;
	char *command = NULL, *h1 = NULL, *h2 = NULL, *c1 = NULL, *c2 = NULL, *line = NULL, *text = NULL, *at = NULL;
	FILE *file = NULL;
	char *private = private_report ? "private: yes\n" : "";

	if (asprintf(&command, "LANG=C gdb --batch -f %s %s -x /var/lib/corewatcher/gdb.command 2> /dev/null", appfile, fullpath) == -1)
		return NULL;

	if ((at = wrapper_scan(command))) {
		free(appfile);
		appfile = at;
	}

	h1 = build_core_header(appfile, fullpath, processed_fullpath);

	file = popen(command, "r");

	while (file && !feof(file)) {
		size_t size = 0;

		c2 = c1;
		free(line);
		ret = getline(&line, &size, file);
		if (!size)
			break;
		if (ret == -1)
			break;

		if (strstr(line, "no debugging symbols found")) {
			continue;
		}
		if (strstr(line, "reason: ")) {
			continue;
		}

		if (c1) {
			c1 = NULL;
			if (asprintf(&c1, "%s%s", c2, line) == -1)
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
	if (ret == -1)
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
	oops->filename = strdup(fullpath);
	return oops;
}

/*
 * filename is of the form core.XXXX[.blah]
 * we need to get the pid out as we want
 * output of the form XXXX[.ext]
 */
char *get_core_filename(char *filename, char *ext)
{
	char *pid = NULL, *c = NULL, *s = NULL, *detail_filename = NULL;

	if (!filename)
		return NULL;

	if (!(s = strstr(filename, ".")))
		return NULL;

	if (!(++s))
		return NULL;
	/* causes valgrind whining because we copy from middle of a string */
	if (!(pid = strdup(s)))
		return NULL;

	c = strstr(pid, ".");

	if (c)
		*c = '\0';

	if (ext) {
		/* causes valgrind whining because we copy from middle of a string */
		if ((asprintf(&detail_filename, "%s%s.%s", core_folder, pid, ext)) == -1) {
			free(pid);
			return NULL;
		}
	} else {
		/* causes valgrind whining because we copy from middle of a string */
		if ((asprintf(&detail_filename, "%s%s", core_folder, pid)) == -1) {
			free(pid);
			return NULL;
		}
	}

	free(pid);
	return detail_filename;
}

/*
 * Write the backtrace from the core file into a text
 * file named after the pid
 */
static void write_core_detail_file(char *filename, char *text)
{
	int fd = 0;
	char *detail_filename = NULL;

	if (!filename || !text)
		return;

	if (!(detail_filename = get_core_filename(filename, "txt")))
		return;

	if ((fd = open(detail_filename, O_WRONLY | O_CREAT | O_TRUNC, 0)) != -1) {
		if(write(fd, text, strlen(text)) >= 0)
			fchmod(fd, 0644);
		else
			unlink(detail_filename);
		close(fd);
	}
	free(detail_filename);
}

/*
 * Removes corefile (core.XXXX) from the processing_queue.
 *
 * Expects the processing_queue_mtx to be held.
 */
static void remove_from_processing_queue(void)
{
	free(processing_queue[head]);
	processing_queue[head++] = NULL;

	if (head == 100)
		head = 0;
}

/*
 * Removes file from processing_oops hash based on pid.
 * Extracts pid from the fullpath such that
 * /home/user/core.pid will be tranformed into just the pid.
 *
 * Expects the lock on the given hash to be held.
 */
void remove_pid_from_hash(char *fullpath, GHashTable *ht)
{
	char *c1 = NULL, *c2 = NULL;

	if (!(c1 = strip_directories(fullpath)))
		return;

	if (!(c2 = get_core_filename(c1, NULL))) {
		free(c1);
		return;
	}

	free(c1);

	g_hash_table_remove(ht, c2);

	free(c2);
}

/*
 * Common function for processing core
 * files to generate oops structures
 */
static struct oops *process_common(char *fullpath, char *processed_fullpath)
{
	struct oops *oops = NULL;
	char *appname = NULL, *appfile = NULL;

	if(!(appname = find_coredump(fullpath))) {
		return NULL;
	}

	if (!(appfile = find_executable(appname))) {
		free(appname);
		return NULL;
	}
	free(appname);

	if (!(oops = extract_core(fullpath, appfile, processed_fullpath))) {
		free(appfile);
		return NULL;
	}
	free(appfile);

	return oops;
}


/*
 * Processes .to-process core files.
 * Also creates the pid.txt file and adds
 * the oops struct to the submit queue
 *
 * Picks up and sets down the gdb_mtx and
 * picks up and sets down the processing_queue_mtx.
 * (held at the same time in that order)
 * Also will pick up and sets down the queued_mtx.
 */
static void *process_new(void __unused *vp)
{
	struct oops *oops = NULL;
	char *procfn = NULL, *corefn = NULL, *fullpath = NULL;

	pthread_mutex_lock(&core_status.processing_mtx);
	pthread_mutex_lock(&gdb_mtx);
	pthread_mutex_lock(&processing_queue_mtx);

	if (!(fullpath = processing_queue[head])) {
		/* something went quite wrong */
		pthread_mutex_unlock(&processing_queue_mtx);
		pthread_mutex_unlock(&gdb_mtx);
		pthread_mutex_unlock(&core_status.processing_mtx);
		return NULL;
	}

	if (!(corefn = strip_directories(fullpath)))
		goto clean_process_new;

	if (!(procfn = replace_name(fullpath, ".to-process", ".processed")))
		goto clean_process_new;

	if (!(oops = process_common(fullpath, procfn)))
		goto clean_process_new;

	if (!(oops->detail_filename = get_core_filename(corefn, "txt")))
		goto clean_process_new;

	if (rename(fullpath, procfn))
		goto clean_process_new;

	free(oops->filename);
	oops->filename = procfn;

	remove_from_processing_queue();

	pthread_mutex_unlock(&processing_queue_mtx);
	pthread_mutex_unlock(&gdb_mtx);
	pthread_mutex_unlock(&core_status.processing_mtx);

	write_core_detail_file(corefn, oops->text);

	pthread_mutex_lock(&core_status.queued_mtx);
	queue_backtrace(oops);
	pthread_mutex_unlock(&core_status.queued_mtx);

	/* don't need to free procfn because was set to oops->filename and that gets free'd */
	free(corefn);
	FREE_OOPS(oops);
	return NULL;

clean_process_new:
	remove_pid_from_hash(fullpath, core_status.processing_oops);
	remove_from_processing_queue();
	free(procfn);
	procfn = NULL; /* don't know if oops->filename == procfn so be safe */
	free(corefn);
	FREE_OOPS(oops);
	pthread_mutex_unlock(&processing_queue_mtx);
	pthread_mutex_unlock(&gdb_mtx);
	pthread_mutex_unlock(&core_status.processing_mtx);
	return NULL;
}

/*
 * Reprocesses .processed core files.
 *
 * Picks up and sets down the gdb_mtx.
 * Picks up and sets down the processing_queue_mtx.
 * (held at the same time in that order)
 * Also will pick up and sets down the queued_mtx.
 */
static void *process_old(void __unused *vp)
{
	struct oops *oops = NULL;
	char *corefn = NULL, *fullpath = NULL;

	pthread_mutex_lock(&gdb_mtx);
	pthread_mutex_lock(&processing_queue_mtx);

	if (!(fullpath = processing_queue[head])) {
		/* something went quite wrong */
		pthread_mutex_unlock(&processing_queue_mtx);
		pthread_mutex_unlock(&gdb_mtx);
		return NULL;
	}

	if (!(corefn = strip_directories(fullpath)))
		goto clean_process_old;

	if (!(oops = process_common(fullpath, fullpath)))
		goto clean_process_old;

	if (!(oops->detail_filename = get_core_filename(corefn, "txt")))
		goto clean_process_old;

	remove_from_processing_queue();

	pthread_mutex_unlock(&processing_queue_mtx);
	pthread_mutex_unlock(&gdb_mtx);

	pthread_mutex_lock(&core_status.queued_mtx);
	queue_backtrace(oops);
	pthread_mutex_unlock(&core_status.queued_mtx);

	free(corefn);
	FREE_OOPS(oops);
	return NULL;

clean_process_old:
	remove_pid_from_hash(fullpath, core_status.processing_oops);
	remove_from_processing_queue();
	free(corefn);
	FREE_OOPS(oops);
	pthread_mutex_unlock(&processing_queue_mtx);
	pthread_mutex_unlock(&gdb_mtx);
	return NULL;
}

/*
 * Adds corefile (based on pid) to the processing_oops
 * hash table if it is not already there, then
 * tries to add to the processing_queue.
 *
 * Picks up and sets down the processing_mtx.
 * Picks up and sets down the processing_queue_mtx.
 */
static int add_to_processing(char *fullpath)
{
	char *c1 = NULL, *c2 = NULL, *fp = NULL;

	if (!fullpath)
		return -1;

	if (!(fp = strdup(fullpath)))
		goto clean_add_to_processing;

	if (!(c1 = get_core_filename(fp, NULL)))
		goto clean_add_to_processing;

	if (!(c2 = strip_directories(c1)))
		goto clean_add_to_processing;

	free(c1);
	c1 = NULL;

	pthread_mutex_lock(&core_status.processing_mtx);
	if (g_hash_table_lookup(core_status.processing_oops, c2)) {
		pthread_mutex_unlock(&core_status.processing_mtx);
		goto clean_add_to_processing;
	}

	pthread_mutex_lock(&processing_queue_mtx);
	if (processing_queue[tail]) {
		pthread_mutex_unlock(&processing_queue_mtx);
		pthread_mutex_unlock(&core_status.processing_mtx);
		goto clean_add_to_processing;
	}

	g_hash_table_insert(core_status.processing_oops, c2, c2);
	processing_queue[tail++] = fp;
	if (tail == 100)
		tail = 0;

	pthread_mutex_unlock(&processing_queue_mtx);
	pthread_mutex_unlock(&core_status.processing_mtx);
	return 0;
clean_add_to_processing:
	free(fp);
	free(c1);
	free(c2);
	return -1;
}

/*
 * Entry for processing new core files.
 */
static void process_corefile(char *fullpath)
{
	pthread_t thrd;
	int r = 1;

	r = add_to_processing(fullpath);

	if (r)
		return;

	if (pthread_create(&thrd, NULL, process_new, NULL))
		fprintf(stderr, "Couldn't start up gdb extract core thread\n");
}

/*
 * Entry for processing already seen core files.
 */
static void reprocess_corefile(char *fullpath)
{
	pthread_t thrd;
	int r = 0;

	r = add_to_processing(fullpath);

	if (r)
		return;

	if (pthread_create(&thrd, NULL, process_old, NULL))
		fprintf(stderr, "Couldn't start up gdb extract core thread\n");
}

int scan_dmesg(void __unused *unused)
{
	DIR *dir = NULL;
	struct dirent *entry = NULL;
	char *fullpath = NULL, *appfile = NULL;
	char tmp_folder[] = "/tmp/";
	int r = 0;

	dir = opendir(tmp_folder);
	if (!dir)
		return 1;

	fprintf(stderr, "+ scanning %s...\n", tmp_folder);
	while(1) {
		free(fullpath);
		fullpath = NULL;

		entry = readdir(dir);
		if (!entry || !entry->d_name)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (strncmp(entry->d_name, "core.", 5))
			continue;

		/* matched core.#### where #### is the processes pid */
		r = asprintf(&fullpath, "%s%s", tmp_folder, entry->d_name);
		if (r == -1) {
			fullpath = NULL;
			continue;
		} else if (((unsigned int)r) != strlen(tmp_folder) + strlen(entry->d_name)) {
			continue;
		}
		/* already found, waiting for response from user */
		pthread_mutex_lock(&core_status.asked_mtx);
		if (g_hash_table_lookup(core_status.asked_oops, fullpath)) {
			pthread_mutex_unlock(&core_status.asked_mtx);
			continue;
		}
		pthread_mutex_unlock(&core_status.asked_mtx);
		fprintf(stderr, "+ Looking at %s\n", fullpath);
		appfile = get_appfile(fullpath);

		if (!appfile) {
			unlink(fullpath);
		} else {
			free(appfile);
			appfile = NULL;
		}
	}
	closedir(dir);

	if (!core_folder)
		return 1;
	dir = opendir(core_folder);
	if (!dir)
		return 1;

	fprintf(stderr, "+ scanning %s...\n", core_folder);
	while(1) {
		free(fullpath);
		fullpath = NULL;

		entry = readdir(dir);
		if (!entry || !entry->d_name)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (!strstr(entry->d_name, "process"))
			continue;

		r = asprintf(&fullpath, "%s%s", core_folder, entry->d_name);
		if (r == -1) {
			fullpath = NULL;
			continue;
		} else if (((unsigned int)r) != strlen(core_folder) + strlen(entry->d_name)) {
			continue;
		}

		fprintf(stderr, "+ Looking at %s\n", fullpath);
		if (strstr(fullpath, "to-process"))
			process_corefile(fullpath);
		else
			reprocess_corefile(fullpath);
	}
	closedir(dir);

	submit_queue();

	return 1;
}
