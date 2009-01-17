/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License.
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