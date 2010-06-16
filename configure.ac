AC_PREREQ([2.64])
AC_INIT([arsenal], [dev], [jdegges@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([src/arsenal.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
ACX_PTHREAD([AC_SUBST([CC], ["${PTHREAD_CC}"])],
  [AC_MSG_ERROR(['pthreads' not found])])

PKG_CHECK_MODULES([LIBSSH2], [libssh2])
PKG_CHECK_MODULES([FUSE], [fuse])
PKG_CHECK_MODULES([LIBXML], [libxml-2.0])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h netdb.h stddef.h stdint.h stdlib.h string.h sys/param.h sys/socket.h sys/statvfs.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset socket strerror])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
