
dnl metis.m4: an autoconf for OpenSM Vendor Selection option
dnl
dnl To use this macro, just do METIS_SEL.
dnl the new configure option --enable-metis will be defined.
dnl The following variables are defined:
dnl METIS_LDADD - LDADD additional libs for linking the vendor lib
AC_DEFUN([METIS_SEL], [
# --- BEGIN METIS_SEL ---

dnl Check if they want the metis support
AC_MSG_CHECKING([to enable metis support for nue routing])
AC_ARG_ENABLE(metis,
[  --enable-metis          Enable the metis support for nue routing (default no)],
        [case $enableval in
        yes) metis_support=yes ;;
        no)  metis_support=no ;;
        esac],
        metis_support=no)
AC_MSG_RESULT([$metis_support])

if test "x$metis_support" = "xyes"; then
   METIS_LDADD="-lmetis"
fi

dnl Define a way for the user to provide the path to the metis includes
AC_ARG_WITH(metis-includes,
    AC_HELP_STRING([--with-metis-includes=<dir>],
                   [define the dir where metis includes are installed]),
AC_MSG_NOTICE(Using metis includes from:$with_metis_includes),
with_metis_includes="")

if test "x$with_metis_includes" != "x"; then
   METIS_INCLUDES="-I$with_metis_includes"
fi

dnl Define a way for the user to provide the path to the metis libs
AC_ARG_WITH(metis-libs,
    AC_HELP_STRING([--with-metis-libs=<dir>],
                   [define the dir where metis libs are installed]),
AC_MSG_NOTICE(Using metis libs from:$with_metis_libs),
with_metis_libs="")

if test "x$with_metis_libs" != "x"; then
   METIS_LDADD="-L$with_metis_libs $METIS_LDADD"
fi

AC_SUBST(METIS_LDADD)
AC_SUBST(METIS_INCLUDES)

# --- END METIS_SEL ---
]) dnl METIS_SEL

dnl Check for the metis lib dependency
AC_DEFUN([METIS_CHECK_LIB], [
# --- BEGIN METIS_CHECK_LIB ---
if test "$metis_support" != "no"; then
   if test "$disable_libcheck" != "yes"; then
      sav_LDFLAGS=$LDFLAGS
      LDFLAGS="$LDFLAGS $METIS_LDADD"
      AC_CHECK_LIB(metis, METIS_PartGraphKway,
            AC_DEFINE(ENABLE_METIS_FOR_NUE,
                      1, [Define as 1 if you want to enable metis support for nue routing]),
	    AC_MSG_ERROR([METIS_PartGraphKway() not found.]))
      LDFLAGS=$sav_LDFLAGS
   else
      AC_DEFINE(ENABLE_METIS_FOR_NUE,
                1, [Define as 1 if you want to enable metis support for nue routing])
   fi
fi
# --- END METIS_CHECK_LIB ---
]) dnl METIS_CHECK_LIB

dnl Check for the vendor lib dependency
AC_DEFUN([METIS_CHECK_HEADER], [
# --- BEGIN METIS_CHECK_HEADER ---

dnl we might be required to ignore this check
if test "$metis_support" != "no"; then
   if test "$disable_libcheck" != "yes"; then
      sav_CPPFLAGS=$CPPFLAGS
      CPPFLAGS="$CPPFLAGS $METIS_INCLUDES"
      AC_CHECK_HEADERS(metis.h)
      CPPFLAGS=$sav_CPPFLAGS
   fi
fi
# --- END METIS_CHECK_HEADER ---
]) dnl METIS_CHECK_HEADER
