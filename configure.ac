AC_PREREQ([2.68])
AC_INIT([corewatcher],[0.9.1],[timothy.c.pepper@linux.intel.com])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_SRCDIR([src/corewatcher.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_mutex_unlock], , AC_MSG_ERROR([libpthread is required but was not found]))

# PkgConfig tests
PKG_CHECK_MODULES([GLIB2], [glib-2.0])
PKG_CHECK_MODULES([LIBPROXY], [libproxy-1.0])
PKG_CHECK_MODULES([LIBNOTIFY], [libnotify])
PKG_CHECK_MODULES([LIBCURL], [libcurl])

# Checks for header files.
AC_CHECK_HEADERS([stdio.h assert.h sys/types.h sys/stat.h dirent.h signal.h errno.h sched.h fcntl.h stdlib.h string.h sys/time.h syslog.h unistd.h asm/unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_MALLOC

AC_OUTPUT(src/corewatcher.service)
AC_MSG_RESULT([
	corewatcher $VERSION
	========

	prefix:			${prefix}
	sysconfdir:		${sysconfdir}
	exec_prefix:		${exec_prefix}
	bindir:			${bindir}
	sbindir:		${sbindir}
	datarootdir:		${datarootdir}
	mandir:			${mandir}

	compiler:		${CC}
	cflags:			${CFLAGS}
	ldflags:		${LDFLAGS}
])
