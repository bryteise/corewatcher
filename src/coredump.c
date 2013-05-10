#define _GNU_SOURCE
/*
 * Copyright 2007,2012 Intel Corporation
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
 *	Tim Pepper <timothy.c.pepper@linux.intel.com>
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
#include <sys/statvfs.h>
#include <syslog.h>
#include <dirent.h>
#include <glib.h>
#include <errno.h>

#include "corewatcher.h"

/*
 * processing "queue" loop's condition variable and associated
 * lock.  Note the queue is an implicit data structure consisting
 * of the non-submitted core files in the filesystem, but the bool pq is
 * used to mark whether the "queue" holds something to prevent the possible
 * race where the condition is set before the thread is awaiting it and
 * thus is not woken.
 */
GMutex *pq_mtx;
static gboolean pq = FALSE;
GCond *pq_work;

static int diskfree = 100;

static char *get_release(void)
{
	FILE *file = NULL;
	char *release = NULL;
	char *buf = NULL;
	char *tbuf = NULL;
	long bufsize;
	int r;
	int unused  __attribute__((unused));

	file = fopen("/etc/os-release", "r");
	if (!file)
		return NULL;

	r = fseek(file, 0L, SEEK_END);
	if (r == -1)
		goto out;

	bufsize = ftell(file);
	if (bufsize == -1)
		goto out;

	buf = calloc(bufsize + 1, sizeof(char));
	if (!buf)
		goto out;

	r = fseek(file, 0L, SEEK_SET);
	if (r == -1)
		goto out;

	unused = fread(buf, sizeof(char), bufsize, file);
	if (ferror(file))
		goto out;

	tbuf = buf;
	while(1) {
		char *c = strchr(tbuf, '\n');

		if (!c)
			break;
		*c = 0;

		if (release) {
			char *t = release;
			release = NULL;
			r = asprintf(&release, "%s        %s\n", t, tbuf);
			free(t);
			if (r == -1) {
				release = NULL;
				goto out;
			}
		} else {
			r = asprintf(&release, "        %s\n", tbuf);
			if (r == -1) {
				release = NULL;
				goto out;
			}
		}
		/*
		 * os-release ends with a '\n', buf adds an extra NUL
		 * char after that last '\n' so if the next char after
		 * a '\n' is NUL we are done
		 */
		if (!*(c + 1))
			break;

		tbuf = c + 1;
	}

out:
	fclose(file);
	if (buf)
		free(buf);

	return release;
}

/*
 * Strip the directories from the path
 * given by fullname
 */
char *strip_directories(char *fullpath)
{
	char *dfile = NULL, *c1 = NULL, *c2 = NULL, *r = NULL;
	char delim[] = "/";
	char *saveptr;

	if (!fullpath)
		return NULL;

	dfile = strdup(fullpath);
	if (!dfile)
		return NULL;

	c1 = strtok_r(dfile, delim, &saveptr);
	while(c1) {
		c2 = c1;
		c1 = strtok_r(NULL, delim, &saveptr);
	}

	if (c2)
		r = strdup(c2);

	free(dfile);

	return r;
}

/*
 * Move corefile from core_folder to processed_folder subdir.
 * If this type of core has recently been seen, unlink this more recent
 * example in order to rate limit submissions of extremely crashy
 * applications.
 * Add extension and attempt to create directories if needed.
 */
static int move_core(char *fullpath, char *extension)
{
	char *corefilename = NULL, *newpath = NULL, *coreprefix = NULL;
	char *s = NULL;
	size_t prefix_len;
	DIR *dir = NULL;
	struct dirent *entry = NULL;
	int ret = 0;

	if (!fullpath)
		return -1;

	corefilename = strip_directories(fullpath);
	if (!corefilename)
		return -ENOMEM;

	/* if the corefile's name minus any suffixes (such as .$PID) and
	 * minus two additional characters (ie: last two digits of
	 * timestamp assuming core_%e_%t) matches another core file in the
	 * processed_folder, simply unlink it instead of processing it for
	 * submission.  TODO: consider a (configurable) time delta greater
	 * than which the cores must be separated, stat'ing the files, etc.
	 */
	coreprefix = strdup(corefilename);
	if (!coreprefix) {
		free(corefilename);
		return -ENOMEM;
	}
	s = strstr(coreprefix, ".");
	if (!s) {
		free(coreprefix);
		free(corefilename);
		return -1;
	}
	*s = '\0';
	prefix_len = strlen(coreprefix);
	if (prefix_len > 2) {
		s = strndup(coreprefix, prefix_len - 2);
		free(coreprefix);
		coreprefix = s;
	} else {
		ret = -1;
		goto out;
	}
	dir = opendir(processed_folder);
	if (!dir) {
		ret = -1;
		goto out;
	}

	while(1) {
		entry = readdir(dir);
		if (!entry || !entry->d_name)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (!strstr(entry->d_name, coreprefix))
			continue;
		break;
	}
	closedir(dir);
	free(coreprefix);

	if (asprintf(&newpath, "%s%s.%s", processed_folder, corefilename, extension) == -1) {
		ret = -1;
		goto out;
	}
	free(corefilename);

	rename(fullpath, newpath);
	free(newpath);
	return 0;

out:
	free(coreprefix);
	free(corefilename);
	fprintf(stderr, "+ ...move failed, ignoring/unlinking %s\n", fullpath);
	unlink(fullpath);
	return ret;
}

static void skip_core(char *fullpath, char *extension)
{
	char *procfn;
	int ret;

	procfn = replace_name(fullpath, extension, ".skipped");
	if (!procfn) {
		fprintf(stderr, "+  Problems with filename manipulation for %s\n", fullpath);
		return;
	}

	ret = rename(fullpath, procfn);
	if (ret) {
		fprintf(stderr, "+  Unable to move %s to %s\n", fullpath, procfn);
		free(procfn);
		return;
	}

	fprintf(stderr, "+  Moved %s to %s\n", fullpath, procfn);
	free(procfn);
	return;
}

/*
 * Use GDB to extract backtrace information from corefile
 */
static struct oops *extract_core(char *fullpath, char *appfile, char *reportname)
{
	struct oops *oops = NULL;
	int ret = 0;
	char *command = NULL, *h1 = NULL, *c1 = NULL, *c2 = NULL, *line = NULL;
	char *text = NULL, *coretime = NULL;
	char *m1 = NULL, *m2 = NULL;
	int bt_lines = 0, maps_lines = 0;
	FILE *file = NULL;
	char *badchar = NULL;
	char *release = get_release();
	int parsing_maps = 0;
	struct stat stat_buf;
	size_t size = 0;
	ssize_t bytesread = 0;

	fprintf(stderr, "+ extract_core() called for %s\n", fullpath);

	if (asprintf(&command, "LANG=C gdb --batch -f '%s' '%s' -x /etc/corewatcher/gdb.command 2>&1", appfile, fullpath) == -1)
		return NULL;

	file = popen(command, "r");
	free(command);
	if (!file)
		fprintf(stderr, "+ gdb failed for %s\n", fullpath);

	if (stat(fullpath, &stat_buf) != -1) {
		coretime = malloc(26);
		if (coretime)
			ctime_r(&stat_buf.st_mtime, coretime);
	}

	ret = asprintf(&h1,
		       "cmdline: %s\n"
		       "time: %s",
		       appfile,
		       coretime ? coretime : "Unknown");
	if (coretime)
		free(coretime);
	if (ret == -1)
		return NULL;

	while (file && !feof(file)) {
		bytesread = getline(&line, &size, file);
		if (!size)
			break;
		if (bytesread == -1)
			break;

		/* gdb outputs (to stderr) many "warning: " strings
		 * We can't trust the gdb 'bt' if these two are seen.
		 */
		if ((strncmp(line, "warning: core file may not match specified executable file.", 59) == 0) ||
		    (strncmp(line, "warning: exec file is newer than core file.", 43) == 0)) {
			free(line);
			pclose(file);
			free(h1);
			fprintf(stderr, "+ core/executable mismatch for %s\n", fullpath);
			return NULL;
		}

		/* try to figure out if we're onto the maps output yet */
		if (strncmp(line, "From", 4) == 0) {
			parsing_maps = 1;
		}
		/* maps might not be present */
		if (strncmp(line, "No shared libraries", 19) == 0) {
			break;
		}

		if (!parsing_maps) { /* parsing backtrace */
			c2 = c1;

			/* gdb's backtrace lines start with a line number */
			if (line[0] != '#')
				continue;

			/* gdb prints some initial info which may include the
			 * "#0" line of the backtrace, then prints the
			 * backtrace in its entirety, leading to a
			 * duplicate "#0" in our summary if we do do: */
			if ((bt_lines == 1) && (strncmp(line, "#0 ", 3) == 0))
				continue;
			bt_lines++;

			/* gdb outputs some 0x1a's which break XML */
			do {
				badchar = memchr(line, 0x1a, bytesread);
				if (badchar)
					*badchar = ' ';
			} while (badchar);

			if (c1) {
				c1 = NULL;
				if (asprintf(&c1, "%s        %s", c2, line) == -1)
					continue;
				free(c2);
			} else {
				/* keep going even if asprintf has errors */
				ret = asprintf(&c1, "        %s", line);
			}
		} else { /* parsing maps */
			m2 = m1;
			maps_lines++;
			if (m1) {
				m1 = NULL;
				if (asprintf(&m1, "%s        %s", m2, line) == -1)
					continue;
				free(m2);
			} else {
				/* keep going even if asprintf has errors */
				ret = asprintf(&m1, "        %s", line);
			}
		}
	}
	if (line)
		free(line);
	if (file)
		pclose(file);

	ret = asprintf(&text,
		       "%s"
		       "release: |\n"
		       "%s"
		       "backtrace: |\n"
		       "%s"
		       "maps: |\n"
		       "%s",
		       h1,
		       release ? release : "        Unknown\n",
		       c1 ? c1 : "        Unknown\n",
		       m1 ? m1 : "        Unknown\n");
	free(h1);
	if (c1)
		free(c1);
	if (m1)
		free(m1);
	if (release)
		free(release);

	if (ret == -1)
		return NULL;

	oops = malloc(sizeof(struct oops));
	if (!oops) {
		free(text);
		return NULL;
	}
	memset(oops, 0, sizeof(struct oops));
	oops->next = NULL;
	oops->application = strdup(appfile);
	oops->text = text;
	oops->filename = strdup(fullpath);
	oops->detail_filename = strdup(reportname);
	return oops;
}

/*
 * input filename has the form: core_$APP_$TIMESTAMP[.$PID]
 * output filename has form of: $APP_$TIMESTAMP.txt
 */
static char *make_report_filename(char *filename)
{
	char *name = NULL, *dotpid = NULL, *stamp = NULL, *detail_filename = NULL;

	if (!filename)
		return NULL;

	if (!(stamp = strstr(filename, "_")))
		return NULL;

	if (!(++stamp))
		return NULL;

	if (!(name = strdup(stamp)))
		return NULL;

	/* strip trailing .PID if present */
	dotpid = strstr(name, ".");
	if (dotpid)
		*dotpid = '\0';

	if ((asprintf(&detail_filename, "%s%s.txt", processed_folder, name)) == -1) {
		free(name);
		return NULL;
	}
	free(name);

	return detail_filename;
}

/*
 * Write the backtrace from the core file into a text
 * file named as $APP_$TIMESTAMP.txt
 */
static void write_core_detail_file(struct oops *oops)
{
	int fd = 0;

	if (!oops->detail_filename)
		return;

	fd = open(oops->detail_filename, O_WRONLY | O_CREAT | O_TRUNC, 0);
	if (fd == -1) {
		fprintf(stderr, "+ Error creating/opening %s for write\n", oops->detail_filename);
		return;
	}

	if(write(fd, oops->text, strlen(oops->text)) >= 0) {
		fprintf(stderr, "+ Wrote %s\n", oops->detail_filename);
		fchmod(fd, 0644);
	} else {
		fprintf(stderr, "+ Error writing %s\n", oops->detail_filename);
		unlink(oops->detail_filename);
	}
	close(fd);
}



/*
 * Creates $APP_$TIMESTAMP.txt report summaries if they don't exist and
 * adds the oops struct to the submit queue
 */
static void *create_report(char *fullpath)
{
	struct oops *oops = NULL;
	char *procfn = NULL;
	char *app = NULL, *appname = NULL, *appfile = NULL, *corefn = NULL, *reportname = NULL;
	char *old_ext = ".processed";
	char *new_ext = ".to-process";
	char *ext = NULL;
	struct stat stat_buf;
	int new = 0, ret;

	fprintf(stderr, "+ Entered create_report() for %s\n", fullpath);

	if (strstr(fullpath, ".to-process")) {
		new = 1;
		ext = new_ext;
	} else if (strstr(fullpath, ".processed")) {
		ext = old_ext;
	} else if (strstr(fullpath, ".skipped")) {
		fprintf(stderr, "+  Already skipped\n");
		return NULL;
	} else { /* bad state */
		fprintf(stderr, "+  Missing extension? (%s)\n", fullpath);
		unlink(fullpath);
		return NULL;
	}

	corefn = strip_directories(fullpath);
	if (!corefn) {
		fprintf(stderr, "+  No corefile? (%s)\n", fullpath);
		return NULL;
	}

	/* don't process rpm, gdb or corewatcher crashes */
	appname = find_causingapp(fullpath);
	if (!appname) {
		fprintf(stderr, "+  No appname in %s\n", corefn);
		skip_core(fullpath, ext);
		free(corefn);
		return NULL;
	}
	app = strip_directories(appname);
	if (!app ||
	    !strncmp(app, "rpm", 3) ||
	    !strncmp(app, "gdb", 3) ||
	    !strncmp(app, "corewatcher", 11)) {
		fprintf(stderr, "+  ...skipping %s's %s\n", app, corefn);
		skip_core(fullpath, ext);
		free(corefn);
		free(appname);
		free(app);
		return NULL;
	}

	/* also skip apps which don't appear to be part of the OS */
	appfile = find_apppath(appname);
	if (!appfile) {
		fprintf(stderr, "+  ...skipping %s's %s\n", appname, corefn);
		skip_core(fullpath, ext);
		free(corefn);
		free(appname);
		free(app);
		return NULL;
	}
	free(appname);
	free(app);

	reportname = make_report_filename(corefn);
	if (!reportname) {
		fprintf(stderr, "+  Couldn't make report name for %s\n", corefn);
		free(corefn);
		free(appfile);
		return NULL;
	}
	free(corefn);
	if (stat(reportname, &stat_buf) == 0) {
		int fd, ret;
		/*
		 * TODO:
		 *   If the file already had trailing ".processed" but the txt file
		 *   is a low quality report, then create a new report.
		 */
		fprintf(stderr, "+  Report already exists in %s\n", reportname);

		oops = malloc(sizeof(struct oops));
		if (!oops) {
			fprintf(stderr, "+  Malloc failed for struct oops\n");
			free(reportname);
			free(appfile);
			return NULL;
		}
		memset(oops, 0, sizeof(struct oops));

		oops->next = NULL;
		oops->application = strdup(appfile);
		oops->filename = strdup(fullpath);
		oops->detail_filename = strdup(reportname);
		free(reportname);
		free(appfile);

		oops->text = malloc(stat_buf.st_size + 1);
		if (!oops->text) {
			fprintf(stderr, "+  Malloc failed for oops text\n");
			goto err;
		}
		fd = open(oops->detail_filename, O_RDONLY);
		if (fd == -1) {
			fprintf(stderr, "+  Open failed for oops text\n");
			goto err;
		}
		ret = read(fd, oops->text, stat_buf.st_size);
		close(fd);
		if (ret != stat_buf.st_size) {
			fprintf(stderr, "+  Read failed for oops text\n");
			goto err;
		}
		oops->text[stat_buf.st_size] = '\0';
	} else {
		oops = extract_core(fullpath, appfile, reportname);
		free(reportname);
		free(appfile);

		if (!oops) {
			fprintf(stderr, "+  Did not generate struct oops for %s\n", fullpath);
			skip_core(fullpath, ext);
			return NULL;
		}
		write_core_detail_file(oops);
	}

	if (new) {
		fprintf(stderr, "+  Renaming %s (%s -> %s)\n", fullpath, new_ext, old_ext);
		procfn = replace_name(fullpath, new_ext, old_ext);
		if (!procfn) {
			fprintf(stderr, "+  Problems with filename manipulation for %s\n", fullpath);
			return oops;
		}
		ret = rename(fullpath, procfn);
		if (ret) {
			fprintf(stderr, "+  Unable to move %s to %s\n", fullpath, procfn);
			free(procfn);
			return oops;
		}
		free(oops->filename);
		oops->filename = strdup(procfn);
		free(procfn);
	}

	return oops;
err:
	FREE_OOPS(oops);
	return NULL;
}

/*
 * scan once for core files in core_folder, moving any to the
 * processed_folder with ".to-process" appended to their name
 */
int scan_core_folder(void __unused *unused)
{
	DIR *dir = NULL;
	struct dirent *entry = NULL;
	char *fullpath = NULL;
	int ret, work = 0;

	dir = opendir(core_folder);
	if (!dir) {
		fprintf(stderr, "+ Unable to open %s\n", core_folder);
		return -1;
	}
	fprintf(stderr, "+ Begin scanning %s...\n", core_folder);
	while(1) {
		entry = readdir(dir);
		if (!entry || !entry->d_name)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (strncmp(entry->d_name, "core_", 5))
			continue;

		/* matched core_#### */
		if (asprintf(&fullpath, "%s%s", core_folder, entry->d_name) == -1) {
			fullpath = NULL;
			continue;
		}

		/* If one were to prompt the user before submitting, that
		 * might happen here.  */

		fprintf(stderr, "+ Looking at %s\n", fullpath);

		ret = move_core(fullpath, "to-process");
		if (ret == 0)
			work++;

		free(fullpath);
		fullpath = NULL;
	}
	closedir(dir);

	if (work) {
		fprintf(stderr, "+ Found %d files, setting pq_work condition\n", work);
		g_mutex_lock(pq_mtx);
		g_cond_signal(pq_work);
		pq = TRUE;
		g_mutex_unlock(pq_mtx);
	}

	fprintf(stderr, "+ End scanning %s...\n", core_folder);
	return TRUE;
}

/*
 * scan for core_*.to-process and core_*.processed,
 * insure a summary *.txt report exists, then queue it
 */
void *scan_processed_folder(void __unused *unused)
{
	DIR *dir = NULL;
	struct dirent *entry = NULL;
	char *fullpath = NULL;
	struct oops *oops = NULL;

	while(1) {
		g_mutex_lock(pq_mtx);
		while (pq != TRUE) {
			fprintf(stderr, "+ Awaiting work in %s...\n", processed_folder);
			g_cond_wait(pq_work, pq_mtx);
		}
		pq = FALSE;
		g_mutex_unlock(pq_mtx);

		fprintf(stderr, "+ Begin scanning %s...\n", processed_folder);

		dir = opendir(processed_folder);
		if (!dir) {
			fprintf(stderr, "+ Unable to open %s\n", processed_folder);
			continue;
		}
		while(1) {
			entry = readdir(dir);
			if (!entry || !entry->d_name)
				break;
			if (entry->d_name[0] == '.')
				continue;

			/* files with trailing ".to-process" or "processed" represent new work */
			if (!strstr(entry->d_name, "process"))
				continue;

			if (asprintf(&fullpath, "%s%s", processed_folder, entry->d_name) == -1) {
				fullpath = NULL;
				continue;
			}

			fprintf(stderr, "+ Looking at %s\n", fullpath);

			oops = create_report(fullpath);

			if (oops) {
				fprintf(stderr, "+ Queued backtrace from %s\n", oops->detail_filename);
				queue_backtrace(oops);
			}

			free(fullpath);
			fullpath = NULL;
		}
		closedir(dir);
		fprintf(stderr, "+ End scanning %s...\n", processed_folder);
	}

	return NULL;
}

static void disable_corefiles(int diskfree)
{
	int ret;
	ret = system("echo \"\" > /proc/sys/kernel/core_pattern");
	if (ret != -1) {
		fprintf(stderr, "+ disabled core pattern, disk low %d%%\n", diskfree);
		syslog(LOG_WARNING,
			"corewatcher: disabled kernel core_pattern, %s only has %d%% available",
			core_folder, diskfree);
	}
}

void enable_corefiles(int diskfree)
{
	int ret;
	char * proc_core_string = NULL;
	ret = asprintf(&proc_core_string,
			"echo \"%score_%%e_%%t\" > /proc/sys/kernel/core_pattern",
			core_folder);
	if (ret == -1)
		goto err;

	ret = system(proc_core_string);
	free(proc_core_string);
	if (ret == -1)
		goto err;

	proc_core_string = NULL;
	ret = asprintf(&proc_core_string,
			"echo 1 > /proc/sys/kernel/core_uses_pid");
	if (ret == -1)
		goto err;

	ret = system(proc_core_string);
	free(proc_core_string);
	if (ret == -1)
		goto err;

	if (diskfree == -1) {
		fprintf(stderr, "+ enabled core pattern\n");
		syslog(LOG_INFO, "corewatcher: enabled kernel core_pattern\n");
	} else {
		fprintf(stderr, "+ reenabled core pattern, disk %d%%", diskfree);
		syslog(LOG_WARNING,
			"corewatcher: reenabled kernel core_pattern, %s now has %d%% available",
			core_folder, diskfree);
	}
	return;
err:
	fprintf(stderr, "+ unable to enable core pattern\n");
	syslog(LOG_WARNING, "corewatcher: unable to enable kernel core_pattern\n");
	return;
}

/* do everything, called from timer event */
int scan_folders(void __unused *unused)
{
	struct statvfs stat;
	int newdiskfree;

	if (statvfs(core_folder, &stat) == 0) {
		newdiskfree = (int)(100 * stat.f_bavail / stat.f_blocks);

		if ((newdiskfree < 10) && (diskfree >= 10))
			disable_corefiles(newdiskfree);
		if ((newdiskfree > 12) && (diskfree <= 12))
			enable_corefiles(newdiskfree);

		diskfree = newdiskfree;
	}

	scan_core_folder(NULL);

	g_mutex_lock(pq_mtx);
	g_cond_signal(pq_work);
	pq = TRUE;
	g_mutex_unlock(pq_mtx);

	return TRUE;
}
