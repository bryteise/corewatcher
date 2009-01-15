#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "coredumper.h"

char *extract_core(char *corefile)
{
	char *command = NULL, *c1 = NULL, *c2 = NULL, *line;
	char *appfile;
	FILE *file;

	appfile = find_executable(find_coredump(corefile));
	if (!appfile)
		return NULL;

	if (asprintf(&command, "gdb --batch -f %s %s -x gdb.command 2> /dev/null", appfile, corefile) < 0)
		return NULL;
		
	file = popen(command, "r");
	while (!feof(file)) {
		size_t size = 0;
		int ret;

		c2 = c1;
		line = NULL;
		ret = getline(&line, &size, file);	
		if (!size)
			break;
		if (ret < 0)
			break;

		if (strstr(line, "no debugging symbols found")) {
			free(line);
			continue;
		}

		if (c1) {
			c1 = NULL;
			if (asprintf(&c1, "%s%s", c2, line) < 0)
				continue;
			free(c2);
		} else {
			c1 = NULL;
			if (asprintf(&c1, "%s", line) < 0)
				continue;
		}
		free(line);
	}
	pclose(file);
	free(command);
	return c1;
}

int main(int argc, char **argv)
{
	if (argc <= 1) {
		printf("Usage:\n\textract_core <core file>\n");
		return EXIT_FAILURE;
	}
	printf("%s\n", extract_core(argv[1]));
	return EXIT_SUCCESS;
}