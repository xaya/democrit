noinst_LTLIBRARIES = libgsp.la
bin_PROGRAMS = democrit-gsp

EXTRA_DIST = \
  rpc-stubs/gsp.json \
  test.py
TESTS = test.py

RPC_STUBS = rpc-stubs/gsprpcserverstub.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

libgsp_la_CXXFLAGS = \
  $(JSON_CFLAGS) $(XAYAGAME_CFLAGS) $(SQLITE_CFLAGS) $(GLOG_CFLAGS)
libgsp_la_LIBADD = \
  $(JSON_LIBS) $(XAYAGAME_LIBS) $(SQLITE_LIBS) $(GLOG_LIBS)
libgsp_la_SOURCES = \
  game.cpp \
  pending.cpp
LIBHEADERS = \
  game.hpp \
  pending.hpp

democrit_gsp_CXXFLAGS = \
  $(JSON_CFLAGS) $(JSONRPCCPPSERVER_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
democrit_gsp_LDADD = \
  $(builddir)/libgsp.la \
  $(JSON_LIBS) $(JSONRPCCPPSERVER_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
democrit_gsp_SOURCES = main.cpp \
  rpcserver.cpp
BINHEADERS = \
  rpcserver.hpp \
  \
  $(RPC_STUBS)

noinst_HEADERS = $(LIBHEADERS) $(BINHEADERS)

rpc-stubs/gsprpcserverstub.h: $(srcdir)/rpc-stubs/gsp.json
	jsonrpcstub "$<" --cpp-server=GspRpcServerStub --cpp-server-file="$@"
