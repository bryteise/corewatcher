/*
 * segfault.c - a program that is intended to create a segmentation
 *              fault (SIGSEGV).
 *
 * No license - this code originates from Public Domain examples
 * on the internet. Not that I'd want to claim copyright anyway
 * on a program that is intended to crash in the first place.
 */

int main(void)
{
	char *s = "hello world";
	int *ptr = 0;

	/* you can't write to the static variable */
	*s = 'H';

	/* NULL ptr dereference */
	*ptr = 1;

	/* recurse without base (stack overflow) */
	main();

	return 0;
}
