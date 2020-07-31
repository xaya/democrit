lib_LTLIBRARIES = libdemocrit.la
democritdir = $(includedir)/democrit

libdemocrit_la_CXXFLAGS = \
  $(JSON_CFLAGS) $(CHARON_CFLAGS) $(XAYAGAME_CFLAGS) $(GLOG_CFLAGS)
libdemocrit_la_LIBADD = \
  $(JSON_LIBS) $(CHARON_LIBS) $(XAYAGAME_LIBS) $(GLOG_LIBS)
libdemocrit_la_SOURCES = \
  mucclient.cpp
democrit_HEADERS =
noinst_HEADERS = \
  private/mucclient.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSON_CFLAGS) $(CHARON_CFLAGS) $(GLOG_CFLAGS) \
  $(GTEST_CFLAGS)
tests_LDADD = \
  $(builddir)/libdemocrit.la \
  $(JSON_LIBS) $(CHARON_LIBS) $(GLOG_LIBS) \
  $(GTEST_LIBS)
tests_SOURCES = \
  testutils.cpp \
  \
  mucclient_tests.cpp
check_HEADERS = \
  testutils.hpp
