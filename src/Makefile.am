lib_LTLIBRARIES = libdemocrit.la
democritdir = $(includedir)/democrit
protodir = $(democritdir)/proto

PROTOS = \
  proto/orders.proto \
  proto/state.proto
PROTOHEADERS = $(PROTOS:.proto=.pb.h)
PROTOSOURCES = $(PROTOS:.proto=.pb.cc)

EXTRA_DIST = $(PROTOS)

BUILT_SOURCES = $(PROTOHEADERS)
CLEANFILES = $(PROTOHEADERS) $(PROTOSOURCES)

libdemocrit_la_CXXFLAGS = \
  $(JSON_CFLAGS) $(CHARON_CFLAGS) $(XAYAGAME_CFLAGS) $(GLOG_CFLAGS)
libdemocrit_la_LIBADD = \
  $(JSON_LIBS) $(CHARON_LIBS) $(XAYAGAME_LIBS) $(GLOG_LIBS)
libdemocrit_la_SOURCES = \
  mucclient.cpp \
  $(PROTOSOURCES)
democrit_HEADERS = \
  assetspec.hpp
proto_HEADERS = $(PROTOHEADERS)
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

proto/%.pb.h proto/%.pb.cc: $(srcdir)/proto/%.proto
	protoc -I$(srcdir)/proto --cpp_out=proto "$<"
