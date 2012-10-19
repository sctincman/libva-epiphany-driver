# epiphany-driver package version number
m4_define([epiphany_driver_major_version], [1])
m4_define([epiphany_driver_minor_version], [0])
m4_define([epiphany_driver_micro_version], [0])
m4_define([epiphany_driver_pre_version],   [1])
m4_define([epiphany_driver_version],
          [epiphany_driver_major_version.epiphany_driver_minor_version.epiphany_driver_micro_version])
m4_if(epiphany_driver_pre_version, [0], [], [
m4_append([epiphany_driver_version], epiphany_driver_pre_version, [.pre])
])

# libva minimum version requirement
m4_define([libva_package_version], [1.0.14])
m4_define([va_api_version], [0.32.0])

AC_PREREQ([2.57])
AC_INIT([epiphany_driver], [epiphany_driver_version], [sctincman@gmail.com],
        [libva-epiphany-driver])
AC_CONFIG_SRCDIR([Makefile.am])
AM_INIT_AUTOMAKE([1.9 tar-ustar])

AM_CONFIG_HEADER([src/config.h])

EPIPHANY_DRIVER_MAJOR_VERSION=epiphany_driver_major_version
EPIPHANY_DRIVER_MINOR_VERSION=epiphany_driver_minor_version
EPIPHANY_DRIVER_MICRO_VERSION=epiphany_driver_micro_version
AC_DEFINE([EPIPHANY_DRIVER_MAJOR_VERSION], [epiphany_driver_major_version], [Major version of the driver])
AC_DEFINE([EPIPHANY_DRIVER_MINOR_VERSION], [epiphany_driver_minor_version], [Minor version of the driver])
AC_DEFINE([EPIPHANY_DRIVER_MICRO_VERSION], [epiphany_driver_micro_version], [Micro version of the driver])
AC_DEFINE([EPIPHANY_DRIVER_PRE_VERSION], [epiphany_driver_pre_version], [Preversion of the driver])

EPIPHANY_DRIVER_LT_LDFLAGS="-avoid-version"
AC_SUBST(EPIPHANY_DRIVER_LT_LDFLAGS)

dnl Use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])], [
    AM_DEFAULT_VERBOSITY=1
    AC_SUBST(AM_DEFAULT_VERBOSITY)
])

AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CC
AM_PROG_CC_C_O

AC_C_BIGENDIAN
AC_HEADER_STDC
AC_SYS_LARGEFILE

LIBVA_PACKAGE_VERSION=libva_package_version
AC_SUBST(LIBVA_PACKAGE_VERSION)

dnl Check for VA-API
PKG_CHECK_MODULES(LIBVA_DEPS,     [libva >= va_api_version])

VA_VERSION=`$PKG_CONFIG --modversion libva`
VA_MAJOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f1`
VA_MINOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f2`
VA_MICRO_VERSION=`echo "$VA_VERSION" | cut -d'.' -f3`
VA_VERSION_STR="$VA_VERSION"

va_full_version_int=`expr ${VA_MAJOR_VERSION:-0} "*" 1000000 + \
                          ${VA_MINOR_VERSION:-0} "*" 10000   + \
                          ${VA_MICRO_VERSION:-0} "*" 100     + \
                          0`
VA_DRIVER_INIT_FUNC="__vaDriverInit_${VA_MAJOR_VERSION}_${VA_MINOR_VERSION}"
AC_DEFINE_UNQUOTED([VA_DRIVER_INIT_FUNC], [$VA_DRIVER_INIT_FUNC],
    [Define driver entry-point])

dnl Check for VA-API drivers path
AC_MSG_CHECKING([for VA drivers path])
LIBVA_DRIVERS_PATH=`$PKG_CONFIG libva --variable driverdir`
if test -z "$LIBVA_DRIVERS_PATH"; then
    LIBVA_DRIVERS_PATH="/usr/lib/xorg/modules/drivers"
fi
AC_MSG_RESULT([$LIBVA_DRIVERS_PATH])
AC_SUBST(LIBVA_DRIVERS_PATH)

dnl Check for JPEG decoding API
AC_CACHE_CHECK([for JPEG decoding API], ac_cv_have_va_jpeg_decode, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_DEPS_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_DEPS_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <va/va.h>]],
            [[VAPictureParameterBufferJPEGBaseline pic_param;
              VASliceParameterBufferJPEGBaseline slice_param;
              VAHuffmanTableBufferJPEGBaseline huffman_table;
              VAIQMatrixBufferJPEGBaseline iq_matrix;]])],
            [ac_cv_have_va_jpeg_decode="yes"],
            [ac_cv_have_va_jpeg_decode="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_va_jpeg_decode" = "yes"; then
    AC_DEFINE(HAVE_VA_JPEG_DECODE, 1,
        [Defined to 1 if VA-API exposes JPEG decoding])
fi

AC_OUTPUT([
    Makefile
    src/Makefile
])


echo
echo $PACKAGE configuration summary:
echo
echo VA-API version ................... : $VA_VERSION_STR
echo VA-API drivers path .............. : $LIBVA_DRIVERS_PATH
echo
