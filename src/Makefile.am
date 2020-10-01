lib_LTLIBRARIES = libdemocrit.la
democritdir = $(includedir)/democrit
protodir = $(democritdir)/proto
rpcstubdir = $(democritdir)/rpc-stubs

PROTOS = \
  proto/orders.proto \
  proto/state.proto
PROTOHEADERS = $(PROTOS:.proto=.pb.h)
PROTOSOURCES = $(PROTOS:.proto=.pb.cc)

EXTRA_DIST = \
  rpc-stubs/daemon.json \
  $(PROTOS)

RPC_STUBS = rpc-stubs/daemonrpcserverstub.h
BUILT_SOURCES = $(PROTOHEADERS) $(RPC_STUBS)
CLEANFILES = $(PROTOHEADERS) $(PROTOSOURCES) $(RPC_STUBS)

libdemocrit_la_CXXFLAGS = \
  $(CHARON_CFLAGS) $(XAYAGAME_CFLAGS) \
  $(JSON_CFLAGS) $(JSONRPCCPPSERVER_CFLAGS) \
  $(PROTOBUF_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libdemocrit_la_LIBADD = \
  $(CHARON_LIBS) $(XAYAGAME_LIBS) \
  $(JSON_LIBS) $(JSONRPCCPPSERVER_LIBS) \
  $(PROTOBUF_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS)
libdemocrit_la_SOURCES = \
  authenticator.cpp \
  daemon.cpp \
  intervaljob.cpp \
  json.cpp \
  mucclient.cpp \
  myorders.cpp \
  orderbook.cpp \
  rpcserver.cpp \
  stanzas.cpp \
  $(PROTOSOURCES)
democrit_HEADERS = \
  assetspec.hpp \
  daemon.hpp \
  json.hpp \
  rpcserver.hpp
proto_HEADERS = $(PROTOHEADERS)
rpcstub_HEADERS = $(RPC_STUBS)
noinst_HEADERS = \
  private/authenticator.hpp \
  private/intervaljob.hpp \
  private/mucclient.hpp \
  private/myorders.hpp \
  private/orderbook.hpp \
  private/stanzas.hpp stanzas.tpp \
  private/state.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  $(JSON_CFLAGS) $(CHARON_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS) \
  $(PROTOBUF_CFLAGS) $(GTEST_CFLAGS)
tests_LDADD = \
  $(builddir)/libdemocrit.la \
  $(JSON_LIBS) $(CHARON_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS) \
  $(PROTOBUF_LIBS) $(GTEST_LIBS)
tests_SOURCES = \
  testutils.cpp \
  \
  authenticator_tests.cpp \
  daemon_tests.cpp \
  intervaljob_tests.cpp \
  json_tests.cpp \
  mucclient_tests.cpp \
  myorders_tests.cpp \
  orderbook_tests.cpp \
  stanzas_tests.cpp
check_HEADERS = \
  testutils.hpp

proto/%.pb.h proto/%.pb.cc: $(srcdir)/proto/%.proto
	protoc -I$(srcdir)/proto --cpp_out=proto "$<"

rpc-stubs/daemonrpcserverstub.h: $(srcdir)/rpc-stubs/daemon.json
	jsonrpcstub "$<" --cpp-server=DaemonRpcServerStub --cpp-server-file="$@"
