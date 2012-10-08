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
 *	Tim Pepper <timothy.c.pepper@linux.intel.com>
 */


#ifndef __INCLUDE_GUARD_KERNELOOPS_H_
#define __INCLUDE_GUARD_KERNELOOPS_H_

/* borrowed from the kernel */
#define __unused  __attribute__ ((__unused__))

#define MAX_URLS 2

#define FREE_OOPS(oops)					\
	do {						\
		if (oops) {				\
			if (oops->application) free(oops->application);	        \
			if (oops->text) free(oops->text);		        \
			if (oops->filename) free(oops->filename);		\
			if (oops->detail_filename) free(oops->detail_filename);	\
			free(oops);			\
		}					\
	} while(0)

struct oops {
	struct oops *next;
	char *application;
	char *text;
	char *filename;
	char *detail_filename;
};

/* inotification.c */
extern void *inotify_loop(void __unused *unused);

/* submit.c */
extern GMutex bt_mtx;
extern GHashTable *bt_hash;
extern void queue_backtrace(struct oops *oops);
extern char *replace_name(char *filename, char *replace, char *new);
extern void *submit_loop(void __unused *unused);

/* coredump.c */
extern int scan_folders(void __unused *unused);
extern int scan_core_folder(void __unused *unused);
extern void *scan_processed_folder(void __unused *unused);
extern const char *core_folder;
extern const char *processed_folder;

/* configfile.c */
extern void read_config_file(char *filename);
extern int allow_distro_to_pass_on;
extern char *submit_url[MAX_URLS];
extern int url_count;

/* corewatcher.c */
extern int testmode;
extern int pinged;
extern struct core_status core_status;

/* find_file.c */
extern char *find_apppath(char *fragment);
extern char *find_causingapp(char *fullpath);

#endif
