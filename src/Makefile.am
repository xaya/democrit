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
  $(CHARON_CFLAGS) $(XAYAGAME_CFLAGS) \
  $(JSON_CFLAGS) $(PROTOBUF_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libdemocrit_la_LIBADD = \
  $(CHARON_LIBS) $(XAYAGAME_LIBS) \
  $(JSON_LIBS) $(PROTOBUF_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS)
libdemocrit_la_SOURCES = \
  authenticator.cpp \
  intervaljob.cpp \
  mucclient.cpp \
  orderbook.cpp \
  $(PROTOSOURCES)
democrit_HEADERS = \
  assetspec.hpp
proto_HEADERS = $(PROTOHEADERS)
noinst_HEADERS = \
  private/authenticator.hpp \
  private/intervaljob.hpp \
  private/mucclient.hpp \
  private/orderbook.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSON_CFLAGS) $(CHARON_CFLAGS) $(GLOG_CFLAGS) \
  $(PROTOBUF_CFLAGS) $(GTEST_CFLAGS)
tests_LDADD = \
  $(builddir)/libdemocrit.la \
  $(JSON_LIBS) $(CHARON_LIBS) $(GLOG_LIBS) \
  $(PROTOBUF_LIBS) $(GTEST_LIBS)
tests_SOURCES = \
  testutils.cpp \
  \
  authenticator_tests.cpp \
  intervaljob_tests.cpp \
  mucclient_tests.cpp \
  orderbook_tests.cpp
check_HEADERS = \
  testutils.hpp

proto/%.pb.h proto/%.pb.cc: $(srcdir)/proto/%.proto
	protoc -I$(srcdir)/proto --cpp_out=proto "$<"
