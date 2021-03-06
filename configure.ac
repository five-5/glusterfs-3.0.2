dnl Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
dnl This file is part of GlusterFS.
dnl
dnl GlusterFS is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl GlusterFS is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_INIT([glusterfs],[3.0.0git],[gluster-users@gluster.org])

AM_INIT_AUTOMAKE

AM_CONFIG_HEADER([config.h])

AC_CONFIG_FILES([Makefile
		libglusterfs/Makefile 
		libglusterfs/src/Makefile
		libglusterfsclient/Makefile
		libglusterfsclient/src/Makefile
		glusterfsd/Makefile
		glusterfsd/src/Makefile
		booster/Makefile
		booster/src/Makefile
		xlators/Makefile
		xlators/mount/Makefile
		xlators/mount/fuse/Makefile
		xlators/mount/fuse/src/Makefile
		xlators/mount/fuse/utils/mount.glusterfs
		xlators/mount/fuse/utils/mount_glusterfs
		xlators/mount/fuse/utils/Makefile
		xlators/storage/Makefile
		xlators/storage/posix/Makefile
		xlators/storage/posix/src/Makefile
		xlators/cluster/Makefile
		xlators/cluster/unify/Makefile
		xlators/cluster/unify/src/Makefile
		xlators/cluster/afr/Makefile
		xlators/cluster/afr/src/Makefile
		xlators/cluster/stripe/Makefile
		xlators/cluster/stripe/src/Makefile
		xlators/cluster/dht/Makefile
		xlators/cluster/dht/src/Makefile
		xlators/cluster/ha/Makefile
		xlators/cluster/ha/src/Makefile
		xlators/cluster/map/Makefile
		xlators/cluster/map/src/Makefile
		xlators/performance/Makefile
		xlators/performance/write-behind/Makefile
		xlators/performance/write-behind/src/Makefile
		xlators/performance/read-ahead/Makefile
		xlators/performance/read-ahead/src/Makefile
		xlators/performance/io-threads/Makefile
		xlators/performance/io-threads/src/Makefile
		xlators/performance/io-cache/Makefile
		xlators/performance/io-cache/src/Makefile
		xlators/performance/symlink-cache/Makefile
		xlators/performance/symlink-cache/src/Makefile
		xlators/performance/quick-read/Makefile
		xlators/performance/quick-read/src/Makefile
                xlators/performance/stat-prefetch/Makefile
                xlators/performance/stat-prefetch/src/Makefile
		xlators/debug/Makefile
		xlators/debug/trace/Makefile
		xlators/debug/trace/src/Makefile
		xlators/debug/error-gen/Makefile
		xlators/debug/error-gen/src/Makefile
		xlators/debug/io-stats/Makefile
		xlators/debug/io-stats/src/Makefile
		xlators/debug/qos_monitor/Makefile
		xlators/debug/qos_monitor/src/Makefile
		xlators/protocol/Makefile
		xlators/protocol/client/Makefile
		xlators/protocol/client/src/Makefile
		xlators/protocol/server/Makefile
		xlators/protocol/server/src/Makefile
		xlators/features/Makefile
		xlators/features/locks/Makefile
		xlators/features/locks/src/Makefile
		xlators/features/path-convertor/Makefile
		xlators/features/path-convertor/src/Makefile
		xlators/features/trash/Makefile
		xlators/features/trash/src/Makefile
		xlators/features/filter/Makefile
		xlators/features/filter/src/Makefile
		xlators/features/quota/Makefile
		xlators/features/quota/src/Makefile
		xlators/encryption/Makefile
		xlators/encryption/rot-13/Makefile
		xlators/encryption/rot-13/src/Makefile
		scheduler/Makefile
		scheduler/alu/Makefile
		scheduler/alu/src/Makefile
		scheduler/random/Makefile
		scheduler/random/src/Makefile
		scheduler/nufa/Makefile
		scheduler/nufa/src/Makefile
		scheduler/rr/Makefile
		scheduler/rr/src/Makefile
		scheduler/switch/Makefile
		scheduler/switch/src/Makefile
		transport/Makefile
		transport/socket/Makefile
		transport/socket/src/Makefile
		transport/ib-verbs/Makefile
		transport/ib-verbs/src/Makefile
		auth/Makefile
		auth/addr/Makefile
		auth/addr/src/Makefile
		auth/login/Makefile
		auth/login/src/Makefile
		doc/Makefile
		doc/examples/Makefile
		doc/hacker-guide/Makefile
		extras/Makefile
		extras/init.d/Makefile
		extras/init.d/glusterfs-server.plist
                extras/init.d/glusterfsd-Debian
                extras/init.d/glusterfsd-Redhat
                extras/init.d/glusterfsd-SuSE
		extras/benchmarking/Makefile
                extras/volgen/Makefile
                extras/volgen/glusterfs-volgen
		contrib/Makefile
		contrib/fuse-util/Makefile
		glusterfs.spec])

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_LIBTOOL

AC_ARG_WITH(mountutildir,
            [  --with-mountutildir=DIR mount helper utility in DIR @<:@/sbin@:>@],
            [mountutildir=$withval],
            [mountutildir='/sbin'])
AC_SUBST(mountutildir)

AC_ARG_WITH(initdir,
            [  --with-initdir=DIR init.d scripts in DIR @<:@/etc/init.d@:>@],
            [initdir=$withval],
            [initdir='/etc/init.d'])
AC_SUBST(initdir)

# LEX needs a check
AC_PROG_LEX
if test  "x${LEX}" != "xflex" -a "x${FLEX}" != "xlex"; then
   AC_MSG_ERROR([Flex or lex required to build glusterfs.])
fi

# YACC needs a check
AC_PROG_YACC
if test "x${YACC}" = "xbyacc" -o "x${YACC}" = "xyacc" -o "x${YACC}" = "x"; then
   AC_MSG_ERROR([GNU Bison required to build glusterfs.])
fi

AC_CHECK_TOOL([LD],[ld])

AC_CHECK_LIB([pthread], [pthread_mutex_init], , AC_MSG_ERROR([Posix threads library is required to build glusterfs]))
		 
AC_CHECK_FUNC([dlopen], [has_dlopen=yes], AC_CHECK_LIB([dl], [dlopen], , AC_MSG_ERROR([Dynamic linking library required to build glusterfs])))


AC_CHECK_HEADERS([sys/xattr.h])

AC_CHECK_HEADERS([sys/extattr.h])

dnl Mac OS X does not have spinlocks
AC_CHECK_FUNC([pthread_spin_init], [have_spinlock=yes])
if test "x${have_spinlock}" = "xyes"; then
   AC_DEFINE(HAVE_SPINLOCK, 1, [define if found spinlock])
fi
AC_SUBST(HAVE_SPINLOCK)

dnl some os may not have GNU defined strnlen function
AC_CHECK_FUNC([strnlen], [have_strnlen=yes])
if test "x${have_strnlen}" = "xyes"; then
   AC_DEFINE(HAVE_STRNLEN, 1, [define if found strnlen])
fi
AC_SUBST(HAVE_STRNLEN)


AC_CHECK_FUNC([setfsuid], [have_setfsuid=yes])
AC_CHECK_FUNC([setfsgid], [have_setfsgid=yes])

if test "x${have_setfsuid}" = "xyes" -a "x${have_setfsgid}" = "xyes"; then
  AC_DEFINE(HAVE_SET_FSID, 1, [define if found setfsuid setfsgid])
fi


# LIBGLUSTERFSCLIENT section
AC_ARG_ENABLE([libglusterfsclient],
	      AC_HELP_STRING([--disable-libglusterfsclient],
			     [Do not build libglusterfsclient]))

BUILD_LIBGLUSTERFSCLIENT=no

if test "x$enable_libglusterfsclient" != "xno"; then
   LIBGLUSTERFSCLIENT_SUBDIR="libglusterfsclient"
   BUILD_LIBGLUSTERFSCLIENT="yes"
fi

AC_SUBST(LIBGLUSTERFSCLIENT_SUBDIR)
# end LIBGLUSTERFSCLIENT section




# FUSE section
AC_ARG_ENABLE([fuse-client],
	      AC_HELP_STRING([--disable-fuse-client],
			     [Do not build the fuse client. NOTE: you cannot mount glusterfs without the client]))

BUILD_FUSE_CLIENT=no
if test "x$enable_fuse_client" != "xno"; then
   FUSE_CLIENT_SUBDIR=fuse
   BUILD_FUSE_CLIENT="yes"
fi

AC_SUBST(FUSE_CLIENT_SUBDIR)
# end FUSE section


# FUSERMOUNT section
AC_ARG_ENABLE([fusermount],
              AC_HELP_STRING([--enable-fusermount],
                             [Build fusermount]))

BUILD_FUSERMOUNT="no"
if test "x$enable_fusermount" = "xyes"; then
  FUSERMOUNT_SUBDIR="contrib"
  BUILD_FUSERMOUNT="yes"
  AC_DEFINE(GF_FUSERMOUNT, 1, [Use our own fusermount])
fi

AC_SUBST(FUSERMOUNT_SUBDIR)
#end FUSERMOUNT section


# EPOLL section
AC_ARG_ENABLE([epoll],
	      AC_HELP_STRING([--disable-epoll],
			     [Use poll instead of epoll.]))

BUILD_EPOLL=no
if test "x$enable_epoll" != "xno"; then
   AC_CHECK_HEADERS([sys/epoll.h],
                    [BUILD_EPOLL=yes],
		    [BUILD_EPOLL=no])
fi
# end EPOLL section


# IBVERBS section
AC_ARG_ENABLE([ibverbs],
	      AC_HELP_STRING([--disable-ibverbs],
			     [Do not build the ibverbs transport]))

if test "x$enable_ibverbs" != "xno"; then
  AC_CHECK_LIB([ibverbs],
               [ibv_get_device_list],
	       [HAVE_LIBIBVERBS="yes"],
	       [HAVE_LIBIBVERBS="no"])
fi

if test "x$enable_ibverbs" = "xyes" -a "x$HAVE_LIBIBVERBS" = "xno"; then
   echo "ibverbs requested but not found."
   exit 1
fi


BUILD_IBVERBS=no
if test "x$enable_ibverbs" != "xno" -a "x$HAVE_LIBIBVERBS" = "xyes"; then
  IBVERBS_SUBDIR=ib-verbs
  BUILD_IBVERBS=yes
fi

AC_SUBST(IBVERBS_SUBDIR)
# end IBVERBS section


dnl FreeBSD > 5 has execinfo as a Ported library for giving a workaround
dnl solution to GCC backtrace functionality

AC_CHECK_HEADERS([execinfo.h], [have_backtrace=yes],
               AC_CHECK_LIB([execinfo], [backtrace], [have_backtrace=yes]))
dnl               AC_MSG_ERROR([libexecinfo not found libexecinfo required.])))

if test "x${have_backtrace}" = "xyes"; then
   AC_DEFINE(HAVE_BACKTRACE, 1, [define if found backtrace])
fi
AC_SUBST(HAVE_BACKTRACE)

dnl glusterfs prints memory usage to stderr by sending it SIGUSR1
AC_CHECK_FUNC([malloc_stats], [have_malloc_stats=yes])
if test "x${have_malloc_stats}" = "xyes"; then
   AC_DEFINE(HAVE_MALLOC_STATS, 1, [define if found malloc_stats])
fi
AC_SUBST(HAVE_MALLOC_STATS)

dnl Linux, Solaris, Cygwin
AC_CHECK_MEMBERS([struct stat.st_atim.tv_nsec])
dnl FreeBSD, NetBSD
AC_CHECK_MEMBERS([struct stat.st_atimespec.tv_nsec])

dnl Check for argp
AC_CHECK_HEADER([argp.h], AC_DEFINE(HAVE_ARGP, 1, [have argp]))
AC_CONFIG_SUBDIRS(argp-standalone)
BUILD_ARGP_STANDALONE=no
if test "x${ac_cv_header_argp_h}" = "xno"; then 
   BUILD_ARGP_STANDALONE=yes
   ARGP_STANDALONE_CPPFLAGS='-I${top_srcdir}/argp-standalone'
   ARGP_STANDALONE_LDADD='${top_builddir}/argp-standalone/libargp.a'
fi

AC_SUBST(ARGP_STANDALONE_CPPFLAGS)
AC_SUBST(ARGP_STANDALONE_LDADD)

AC_CHECK_HEADER([malloc.h], AC_DEFINE(HAVE_MALLOC_H, 1, [have malloc.h]))

AC_CHECK_FUNC([llistxattr], [have_llistxattr=yes])
if test "x${have_llistxattr}" = "xyes"; then
   AC_DEFINE(HAVE_LLISTXATTR, 1, [define if llistxattr exists])
fi

AC_CHECK_FUNC([fdatasync], [have_fdatasync=yes])
if test "x${have_fdatasync}" = "xyes"; then
   AC_DEFINE(HAVE_FDATASYNC, 1, [define if fdatasync exists])
fi

# Check the distribution where you are compiling glusterfs on 

GF_DISTRIBUTION=
AC_CHECK_FILE([/etc/debian_version])
AC_CHECK_FILE([/etc/SuSE-release])
AC_CHECK_FILE([/etc/redhat-release])

if test "x$ac_cv_file__etc_debian_version" = "xyes"; then
   GF_DISTRIBUTION=Debian
fi
if test "x$ac_cv_file__etc_SuSE_release" = "xyes"; then
   GF_DISTRIBUTION=SuSE
fi
if test "x$ac_cv_file__etc_redhat_release" = "xyes"; then
   GF_DISTRIBUTION=Redhat
fi

AC_SUBST(GF_DISTRIBUTION)

GF_HOST_OS=""
GF_LDFLAGS="-rdynamic"

if test "x$BUILD_LIBGLUSTERFSCLIENT" = "xyes"; then
   GF_BOOSTER_SUBDIR="booster"
fi

GF_FUSE_LDADD="-lfuse"
case $host_os in
     linux*)
     dnl	GF_LINUX_HOST_OS=1
        GF_HOST_OS="GF_LINUX_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS}"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	;;
     solaris*)
        GF_HOST_OS="GF_SOLARIS_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D_REENTRANT"
	GF_LDFLAGS=""
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_GLUSTERFS_LDFLAGS="-lnsl -lresolv -lsocket"
	GF_BOOSTER_SUBDIR=""
        BUILD_FUSE_CLIENT=no
        FUSE_CLIENT_SUBDIR=""
	;;
     *bsd*)
        GF_HOST_OS="GF_BSD_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS}"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	if test "x$ac_cv_header_execinfo_h" = "xyes"; then
	   GF_GLUSTERFS_LDFLAGS="-lexecinfo"
	fi				      
	GF_FUSE_LDADD="-liconv -lfuse"
	BUILD_LIBGLUSTERFSCLIENT=no
	LIBGLUSTERFSCLIENT_SUBDIR=""
	GF_BOOSTER_SUBDIR=""
	;;
     darwin*)
        GF_HOST_OS="GF_DARWIN_HOST_OS"
	LIBTOOL=glibtool
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D__DARWIN_64_BIT_INO_T -bundle -undefined suppress -flat_namespace"
	GF_GLUSTERFS_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D__DARWIN_64_BIT_INO_T -undefined suppress -flat_namespace"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_FUSE_LDADD="-liconv -lfuse_ino64"
	BUILD_LIBGLUSTERFSCLIENT=no
	LIBGLUSTERFSCLIENT_SUBDIR=""
	GF_BOOSTER_SUBDIR=""
	;;
esac

AC_SUBST(GF_HOST_OS)
AC_SUBST(GF_GLUSTERFS_LDFLAGS)
AC_SUBST(GF_GLUSTERFS_CFLAGS)
AC_SUBST(GF_CFLAGS)
AC_SUBST(GF_LDFLAGS)
AC_SUBST(GF_LDADD)
AC_SUBST(GF_FUSE_LDADD)
AC_SUBST(GF_BOOSTER_SUBDIR)

CONTRIBDIR='$(top_srcdir)/contrib'
AC_SUBST(CONTRIBDIR)

AM_CONDITIONAL([GF_DARWIN_HOST_OS], test "${GF_HOST_OS}" = "GF_DARWIN_HOST_OS")	

AC_OUTPUT

echo
echo "GlusterFS configure summary"
echo "==========================="
echo "FUSE client        : $BUILD_FUSE_CLIENT"
echo "Infiniband verbs   : $BUILD_IBVERBS"
echo "epoll IO multiplex : $BUILD_EPOLL"
echo "libglusterfsclient : $BUILD_LIBGLUSTERFSCLIENT"
echo "argp-standalone    : $BUILD_ARGP_STANDALONE"
echo "fusermount         : $BUILD_FUSERMOUNT"
echo
