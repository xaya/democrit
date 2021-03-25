lib_LTLIBRARIES = libdemocrit.la
democritdir = $(includedir)/democrit
protodir = $(democritdir)/proto
rpcstubdir = $(democritdir)/rpc-stubs

PROTOS = \
  proto/orders.proto \
  proto/processing.proto \
  proto/state.proto \
  proto/trades.proto
PROTOHEADERS = $(PROTOS:.proto=.pb.h)
PROTOSOURCES = $(PROTOS:.proto=.pb.cc)

EXTRA_DIST = \
  rpc-stubs/daemon.json \
  rpc-stubs/dem-gsp.json \
  rpc-stubs/test.json \
  rpc-stubs/xaya.json \
  $(PROTOS)

RPC_STUBS = \
  rpc-stubs/daemonrpcserverstub.h \
  rpc-stubs/demgsprpcclient.h \
  rpc-stubs/demgsprpcserverstub.h \
  rpc-stubs/testrpcclient.h \
  rpc-stubs/testrpcserverstub.h \
  rpc-stubs/xayarpcclient.h \
  rpc-stubs/xayarpcserverstub.h
BUILT_SOURCES = $(PROTOHEADERS) $(RPC_STUBS)
CLEANFILES = $(PROTOHEADERS) $(PROTOSOURCES) $(RPC_STUBS)

libdemocrit_la_CXXFLAGS = \
  $(CHARON_CFLAGS) $(XAYAGAME_CFLAGS) \
  $(JSON_CFLAGS) $(JSONRPCCPPCLIENT_CFLAGS) $(JSONRPCCPPSERVER_CFLAGS) \
  $(PROTOBUF_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS)
libdemocrit_la_LIBADD = \
  $(CHARON_LIBS) $(XAYAGAME_LIBS) \
  $(JSON_LIBS) $(JSONRPCCPPCLIENT_LIBS) $(JSONRPCCPPSERVER_LIBS) \
  $(PROTOBUF_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS)
libdemocrit_la_SOURCES = \
  authenticator.cpp \
  checker.cpp \
  daemon.cpp \
  intervaljob.cpp \
  json.cpp \
  mucclient.cpp \
  myorders.cpp \
  orderbook.cpp \
  rpcserver.cpp \
  stanzas.cpp \
  trades.cpp \
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
  private/checker.hpp \
  private/intervaljob.hpp \
  private/mucclient.hpp \
  private/myorders.hpp \
  private/orderbook.hpp \
  private/rpcclient.hpp private/rpcclient.tpp \
  private/stanzas.hpp stanzas.tpp \
  private/state.hpp \
  private/trades.hpp

check_PROGRAMS = tests
TESTS = tests

tests_CXXFLAGS = \
  -DCHARON_PREFIX="\"$(CHARON_PREFIX)\"" \
  $(CHARON_CFLAGS) $(XAYAGAME_CFLAGS) \
  $(JSON_CFLAGS) $(JSONRPCCPPCLIENT_CFLAGS) $(JSONRPCCPPSERVER_CFLAGS) \
  $(PROTOBUF_CFLAGS) $(GFLAGS_CFLAGS) $(GLOG_CFLAGS) $(GTEST_CFLAGS)
tests_LDADD = \
  $(builddir)/libdemocrit.la \
  $(CHARON_LIBS) $(XAYAGAME_LIBS) \
  $(JSON_LIBS) $(JSONRPCCPPCLIENT_LIBS) $(JSONRPCCPPSERVER_LIBS) \
  $(PROTOBUF_LIBS) $(GFLAGS_LIBS) $(GLOG_LIBS) $(GTEST_LIBS)
tests_SOURCES = \
  mockxaya.cpp \
  testutils.cpp \
  \
  authenticator_tests.cpp \
  checker_tests.cpp \
  daemon_tests.cpp \
  intervaljob_tests.cpp \
  json_tests.cpp \
  mucclient_tests.cpp \
  myorders_tests.cpp \
  orderbook_tests.cpp \
  rpcclient_tests.cpp \
  stanzas_tests.cpp \
  trades_tests.cpp
check_HEADERS = \
  mockxaya.hpp mockxaya.tpp \
  testutils.hpp

proto/%.pb.h proto/%.pb.cc: $(srcdir)/proto/%.proto
	protoc -I$(srcdir)/proto --cpp_out=proto "$<"

rpc-stubs/daemonrpcserverstub.h: $(srcdir)/rpc-stubs/daemon.json
	jsonrpcstub "$<" --cpp-server=DaemonRpcServerStub --cpp-server-file="$@"

rpc-stubs/demgsprpcclient.h: $(srcdir)/rpc-stubs/dem-gsp.json
	jsonrpcstub "$<" --cpp-client=DemGspRpcClient --cpp-client-file="$@"
rpc-stubs/demgsprpcserverstub.h: $(srcdir)/rpc-stubs/dem-gsp.json
	jsonrpcstub "$<" --cpp-server=DemGspRpcServerStub --cpp-server-file="$@"

rpc-stubs/testrpcclient.h: $(srcdir)/rpc-stubs/test.json
	jsonrpcstub "$<" --cpp-client=TestRpcClient --cpp-client-file="$@"
rpc-stubs/testrpcserverstub.h: $(srcdir)/rpc-stubs/test.json
	jsonrpcstub "$<" --cpp-server=TestRpcServerStub --cpp-server-file="$@"

rpc-stubs/xayarpcclient.h: $(srcdir)/rpc-stubs/xaya.json
	jsonrpcstub "$<" --cpp-client=XayaRpcClient --cpp-client-file="$@"
rpc-stubs/xayarpcserverstub.h: $(srcdir)/rpc-stubs/xaya.json
	jsonrpcstub "$<" --cpp-server=XayaRpcServerStub --cpp-server-file="$@"
