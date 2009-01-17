/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the license.
 */
#include <stdlib.h>

int bar(char *foo)
{
	*foo = 0;	
}
void main()
{
	char *foo = NULL;
	bar(foo);
}