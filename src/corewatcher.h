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


#ifndef __INCLUDE_GUARD_KERNELOOPS_H_
#define __INCLUDE_GUARD_KERNELOOPS_H_

/* borrowed from the kernel */
#define barrier() __asm__ __volatile__("": : :"memory")
#define __unused  __attribute__ ((__unused__))

#define MAX_PROCESSING_OOPS 10
#define MAX_URLS 9

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

/* Always pick up the queued_mtx and then the
   processing_mtx, reverse for setting down */
/* Considering the static mutexes the total global order should be:
   queued_mtx -> processing_mtx -> gdb_mtx ->processing_queue_mtx */
/* The asked_mtx doesn't overlap with any of these */
struct core_status {
	GHashTable *asked_oops;
	GHashTable *processing_oops;
	GHashTable *queued_oops;
	pthread_mutex_t asked_mtx;
	pthread_mutex_t processing_mtx;
	pthread_mutex_t queued_mtx;
};

/* submit.c */
extern void queue_backtrace(struct oops *oops);
extern void submit_queue(void);
extern char *replace_name(char *filename, char *replace, char *new);

/* coredump.c */
extern int move_core(char *fullpath, char *ext);
extern int scan_corefolders(void * unused);
extern char *strip_directories(char *fullpath);
extern char *get_core_filename(char *filename, char *ext);
extern void remove_pid_from_hash(char *fullpath, GHashTable *ht);
extern int uid;
extern int sig;

/* configfile.c */
extern void read_config_file(char *filename);
extern int opted_in;
extern int allow_distro_to_pass_on;
extern char *submit_url[MAX_URLS];
extern char *build_release;
extern char *core_folder;
extern int url_count;
extern int do_unlink;

/* corewatcher.c */
extern int testmode;
extern int pinged;
extern struct core_status core_status;

/* find_file.c */
extern char *find_executable(char *fragment);
extern char *find_coredump(char *fullpath);

#endif
