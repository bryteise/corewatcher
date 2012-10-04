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

#define MAX_PROCESSING_OOPS 10
#define MAX_URLS 2

#define FREE_OOPS(oops)					\
	do {						\
		if (oops) {				\
			free(oops->application);	\
			free(oops->text);		\
			free(oops->filename);		\
			free(oops->detail_filename);	\
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

/* Considering the static mutexes the total global order should be:
       processing_mtx -> gdb_mtx ->processing_queue_mtx */
struct core_status {
	GHashTable *processing_oops;
	GMutex processing_mtx;
};

/* inotification.c */
extern void *inotify_loop(void * unused);

/* submit.c */
extern void queue_backtrace(struct oops *oops);
extern char *replace_name(char *filename, char *replace, char *new);
extern void *submit_loop(void * unused);

/* coredump.c */
extern int move_core(char *fullpath, char *ext);
extern int scan_corefolders(void * unused);
extern char *strip_directories(char *fullpath);
extern char *get_core_filename(char *filename, char *ext);
extern void remove_name_from_hash(char *fullpath, GHashTable *ht);
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
extern char *find_executable(char *fragment);
extern char *find_coredump(char *fullpath);

#endif
