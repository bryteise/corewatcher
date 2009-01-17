/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License.
 */

#ifndef __INCLUDE_GUARD_COREDUMPER_H__
#define __INCLUDE_GUARD_COREDUMPER_H__
extern char *find_executable(char *fragment);
extern char *find_coredump(char *corefile);
#endif
