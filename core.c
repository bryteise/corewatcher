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