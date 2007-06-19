
dnl osmvsel.m4: an autoconf for OpenSM Vendor Selection option
dnl
dnl To use this macro, just do OPENIB_APP_OSMV_SEL.  
dnl the new configure option --with-osmv will be defined.
dnl current supported values are: openib(default),sim,gen1
dnl The following variables are defined:
dnl with-osmv - the osm vendor prefix
dnl OSMV_CFLAGS - CFLAGS additions required to define the vendor type
dnl OSMV_LDADD - LDADD additional libs for linking the vendor lib
AC_DEFUN([OPENIB_APP_OSMV_SEL], [
# --- BEGIN OPENIB_APP_OSMV_SEL ---

dnl Define a way for the user to provide the osm vendor type
AC_ARG_WITH(osmv,
[  --with-osmv=<type> define the osm vendor type to build],
AC_MSG_NOTICE(Using OSM Vendor Type:$with_osmv),
with_osmv="openib")

dnl Define a way for the user to provide the path to the ibumad installation
AC_ARG_WITH(umad-prefix,
[  --with-umad-prefix=<dir> define the dir used as prefix for ibumad installation],
AC_MSG_NOTICE(Using ibumad installation prefix:$with_umad_prefix),
with_umad_prefix="")

dnl Define a way for the user to provide the path to the ibumad includes
AC_ARG_WITH(umad-includes,
[  --with-umad-includes=<dir> define the dir where ibumad includes are installed],
AC_MSG_NOTICE(Using ibumad includes from:$with_umad_includes),
with_umad_includes="")

if test x$with_umad_includes = x; then 
   if test x$with_umad_prefix != x; then
        with_umad_includes=$with_umad_prefix/include
   fi
fi

dnl Define a way for the user to provide the path to the ibumad libs
AC_ARG_WITH(umad-libs,
[  --with-umad-libs=<dir> define the dir where ibumad libs are installed],
AC_MSG_NOTICE(Using ibumad libs from:$with_umad_libs),
with_umad_libs="")

if test x$with_umad_libs = x; then 
   if test x$with_umad_prefix != x; then
dnl Should we use lib64 or lib
if test "$(uname -m)" = "x86_64" -o "$(uname -m)" = "ppc64"; then
        with_umad_libs=$with_umad_prefix/lib64
else
        with_umad_libs=$with_umad_prefix/lib
      fi
fi
fi

dnl Define a way for the user to provide the path to the simulator installation
AC_ARG_WITH(sim,
[  --with-sim=<dir> define the simulator prefix for building sim vendor (/usr)],
AC_MSG_NOTICE(Using Simulator from:$with_sim),
with_sim="/usr")

dnl based on the with_osmv we can try the vendor flag
if test $with_osmv = "openib"; then
   OSMV_CFLAGS="-DOSM_VENDOR_INTF_OPENIB"
   OSMV_INCLUDES="-I\$(srcdir)/../include -I\$(srcdir)/../../libibcommon/include/infiniband -I\$(srcdir)/../../libibumad/include/infiniband"
   if test "x$with_umad_libs" = "x"; then
     OSMV_LDADD="-libumad"
   else
     OSMV_LDADD="-L$with_umad_libs -libumad"
   fi

   if test "x$with_umad_includes" != "x"; then 
     OSMV_INCLUDES="-I$with_umad_includes $OSMV_INCLUDES"
   fi
elif test $with_osmv = "sim" ; then
   OSMV_CFLAGS="-DOSM_VENDOR_INTF_SIM"
   OSMV_INCLUDES="-I$with_sim/include -I\$(srcdir)/../include"
   OSMV_LDADD="-L$with_sim/lib -libmscli"
elif test $with_osmv = "gen1"; then
   OSMV_CFLAGS="-DOSM_VENDOR_INTF_TS"

   if test -z $MTHOME; then
      MTHOME=/usr/local/ibgd/driver/infinihost
   fi

   OSMV_INCLUDES="-I$MTHOME/include -I\$(srcdir)/../include"

   dnl we need to find the TS includes somewhere...
   osmv_found=0
   if test -z $TSHOME; then 
      osmv_dir=`uname -r|sed 's/-smp//'`
      osmv_dir_smp=`uname -r`
      for d in /usr/src/linux-$osmv_dir /usr/src/linux-$osmv_dir_smp /lib/modules/$osmv_dir/build /lib/modules/$osmv_dir_smp/build/; do
         if test -f $d/drivers/infiniband/include/ts_ib_useraccess.h; then
       OSMV_INCLUDES="$OSMV_INCLUDES -I$d/drivers/infiniband/include"
       osmv_found=1
      fi
   done
   else
      if test -f  $TSHOME/ts_ib_useraccess.h; then
         OSMV_INCLUDES="$OSMV_INCLUDES -I$TSHOME"
         osmv_found=1
      fi
   fi      
   if test $osmv_found = 0; then
      AC_MSG_ERROR([Fail to find gen1 include files dir])
   fi
   OSMV_LDADD="-L/usr/local/ibgd/driver/infinihost/lib -lvapi -lmosal -lmtl_common -lmpga"
elif test $with_osmv = "vapi"; then
   OSMV_CFLAGS="-DOSM_VENDOR_INTF_MTL"
   OSMV_INCLUDES="-I/usr/mellanox/include -I/usr/include -I\$(srcdir)/../include"
   OSMV_LDADD="-L/usr/lib -L/usr/mellanox/lib -lib_mgt -lvapi -lmosal -lmtl_common -lmpga"
else
   AC_MSG_ERROR([Invalid Vendor Type provided:$with_osmv should be either openib,sim,gen1])
fi

AM_CONDITIONAL(OSMV_VAPI, test $with_osmv = "vapi")
AM_CONDITIONAL(OSMV_GEN1, test $with_osmv = "gen1")
AM_CONDITIONAL(OSMV_SIM, test $with_osmv = "sim")
AM_CONDITIONAL(OSMV_OPENIB, test $with_osmv = "openib")

AC_SUBST(with_osmv)
AC_SUBST(OSMV_CFLAGS)
AC_SUBST(OSMV_LDADD)
AC_SUBST(OSMV_INCLUDES)

# --- END OPENIB_APP_OSMV_SEL ---
]) dnl OPENIB_APP_OSMV_SEL

dnl Check for the vendor lib dependency 
AC_DEFUN([OPENIB_APP_OSMV_CHECK_LIB], [
# --- BEGIN OPENIB_APP_OSMV_CHECK_LIB ---
if test "$disable_libcheck" != "yes"; then

 dnl based on the with_osmv we can try the vendor flag
 if test $with_osmv = "openib"; then
   osmv_save_ldflags=$LDFLAGS
   LDFLAGS="$LDFLAGS $OSMV_LDADD"
   AC_CHECK_LIB(ibumad, umad_init, [],
	 AC_MSG_ERROR([umad_init() not found. libosmvendor of type openib requires libibumad.]))
   LD_FLAGS=$osmv_save_ldflags
 elif test $with_osmv = "sim" ; then
   LDFLAGS="$LDFLAGS -L$with_sim/lib"
   AC_CHECK_FILE([$with_sim/lib/libibmscli.a], [], 
    AC_MSG_ERROR([ibms_bind() not found. libosmvendor of type sim requires libibmscli.]))
 elif test $with_osmv = "gen1"; then
   osmv_save_ldflags=$LDFLAGS
   LDFLAGS="$LDFLAGS -L$MTHOME/lib -L$MTHOME/lib64 -lmosal -lmtl_common -lmpga"
   AC_CHECK_LIB(vapi, vipul_init, [],
    AC_MSG_ERROR([vipul_init() not found. libosmvendor of type gen1 requires libvapi.]))
   LD_FLAGS=$osmv_save_ldflags
 elif test $with_osmv = "vapi"; then
   osmv_save_ldflags=$LDFLAGS
 else
   AC_MSG_ERROR([OSM Vendor Type not defined: please make sure OPENIB_APP_OSMV SEL is run before CHECK_LIB])
 fi
fi
# --- END OPENIB_APP_OSMV_CHECK_LIB ---
]) dnl OPENIB_APP_OSMV_CHECK_LIB

dnl Check for the vendor lib dependency 
AC_DEFUN([OPENIB_APP_OSMV_CHECK_HEADER], [
# --- BEGIN OPENIB_APP_OSMV_CHECK_HEADER ---

dnl we might be required to ignore this check
if test "$disable_libcheck" != "yes"; then
 if test $with_osmv = "openib"; then
   osmv_headers=infiniband/umad.h
 elif test $with_osmv = "sim" ; then
   osmv_headers=ibmgtsim/ibms_client_api.h
 elif test $with_osmv = "gen1"; then
   osmv_headers=
 elif test $with_osmv = "vapi"; then
   osmv_headers=vapi.h
 else
   AC_MSG_ERROR([OSM Vendor Type not defined: please make sure OPENIB_APP_OSMV SEL is run before CHECK_HEADER])
 fi
 if test "x$osmv_headers" != "x"; then
  AC_CHECK_HEADERS($osmv_headers)
 fi
fi
# --- END OPENIB_APP_OSMV_CHECK_HEADER ---
]) dnl OPENIB_APP_OSMV_CHECK_HEADER

dnl Check if they want the socket console
AC_DEFUN([OPENIB_OSM_CONSOLE_SOCKET_SEL], [
# --- BEGIN OPENIB_OSM_CONSOLE_SOCKET_SEL ---

dnl Console over a socket connection
AC_ARG_ENABLE(console-socket,
[  --enable-console-socket Enable a console socket, requires tcp_wrappers (default no)],
[case $enableval in
     yes) console_socket=yes ;;
     no)  console_socket=no ;;
   esac],
   console_socket=no)
if test $console_socket = yes; then
  AC_CHECK_LIB(wrap, request_init, [],
 	AC_MSG_ERROR([request_init() not found. console-socket requires libwrap.]))
  AC_DEFINE(ENABLE_OSM_CONSOLE_SOCKET,
	    1,
	    [Define as 1 if you want to enable a console on a socket connection])
fi
# --- END OPENIB_OSM_CONSOLE_SOCKET_SEL ---
]) dnl OPENIB_OSM_CONSOLE_SOCKET_SEL

dnl Check if they want the PerfMgr
AC_DEFUN([OPENIB_OSM_PERF_MGR_SEL], [
# --- BEGIN OPENIB_OSM_PERF_MGR_SEL ---

dnl enable the perf-mgr
AC_ARG_ENABLE(perf-mgr,
[  --enable-perf-mgr Enable the performance manager (default no)],
   [case $enableval in
     yes) perf_mgr=yes ;;
     no)  perf_mgr=no ;;
   esac],
   perf_mgr=no)
if test $perf_mgr = yes; then
  AC_DEFINE(ENABLE_OSM_PERF_MGR,
	    1,
	    [Define as 1 if you want to enable the performance manager])
fi
# --- END OPENIB_OSM_PERF_MGR_SEL ---
]) dnl OPENIB_OSM_PERF_MGR_SEL


dnl Check if they want the event plugin
AC_DEFUN([OPENIB_OSM_DEFAULT_EVENT_PLUGIN_SEL], [
# --- BEGIN OPENIB_OSM_DEFAULT_EVENT_PLUGIN_SEL ---

dnl enable the default-event-plugin
AC_ARG_ENABLE(default-event-plugin,
[  --enable-default-event-plugin  Enable a default event plugin "osmeventplugin" (default no)],
   [case $enableval in
     yes) default_event_plugin=yes ;;
     no)  default_event_plugin=no ;;
   esac],
   default_event_plugin=no)
if test $default_event_plugin = yes; then
  AC_DEFINE(ENABLE_OSM_DEFAULT_EVENT_PLUGIN,
	    1,
	    [Define as 1 if you want to enable the event plugin])
  DEFAULT_EVENT_PLUGIN=osmeventplugin
else
  DEFAULT_EVENT_PLUGIN=
fi
AC_SUBST([DEFAULT_EVENT_PLUGIN])

# --- END OPENIB_OSM_DEFAULT_EVENT_PLUGIN_SEL ---
]) dnl OPENIB_OSM_DEFAULT_EVENT_PLUGIN_SEL

