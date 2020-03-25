AC_INIT([democrit], [0.1])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AM_PROG_AR
AM_PATH_PYTHON
PKG_INSTALLDIR

LT_INIT

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX([14], [noext])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS+=" -Wall"])
AX_CHECK_COMPILE_FLAG([-Werror], [CXXFLAGS+=" -Werror"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAGS+=" -pedantic"])

# Windows defines ERROR, which requires us to tell glog to not define
# it as abbreviated log severity (LOG(ERROR) still works, though, and
# that is all that we actually use in the code).
# See https://hpc.nih.gov/development/glog.html.
CXXFLAGS+=" -DGLOG_NO_ABBREVIATED_SEVERITIES"

# Public dependencies exposed in the headers.
AX_PKG_CHECK_MODULES([JSON], [jsoncpp], [])

# Private dependencies for the library parts.
AX_PKG_CHECK_MODULES([XAYAGAME], [], [libxayautil libxayagame])
AX_PKG_CHECK_MODULES([SQLITE], [], [sqlite3])
AX_PKG_CHECK_MODULES([GLOG], [], [libglog])

# Private dependencies for tests and binaries only.
PKG_CHECK_MODULES([GFLAGS], [gflags])
PKG_CHECK_MODULES([GTEST], [gmock gtest_main])

AC_CONFIG_FILES([
  Makefile \
  gsp/Makefile
])
AC_OUTPUT

echo
echo "CXXFLAGS: ${CXXFLAGS}"
