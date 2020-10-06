bin_PROGRAMS = nonfungible-democrit

EXTRA_DIST = rpc-stubs/nf.json

RPC_STUBS = rpc-stubs/nfrpcclient.h
BUILT_SOURCES = $(RPC_STUBS)
CLEANFILES = $(RPC_STUBS)

nonfungible_democrit_CXXFLAGS = \
  -I$(top_srcdir)/src \
  $(JSON_CFLAGS) $(JSONRPCCPPSERVER_CFLAGS) $(JSONRPCCPPCLIENT_CFLAGS) \
  $(GLOG_CFLAGS) $(GFLAGS_CFLAGS)
nonfungible_democrit_LDADD = \
  $(top_builddir)/src/libdemocrit.la \
  $(JSON_LIBS) $(JSONRPCCPPSERVER_LIBS) $(JSONRPCCPPCLIENT_LIBS) \
  $(GLOG_LIBS) $(GFLAGS_LIBS)
nonfungible_democrit_SOURCES = main.cpp
noinst_HEADERS = $(RPC_STUBS)

rpc-stubs/nfrpcclient.h: $(srcdir)/rpc-stubs/nf.json
	jsonrpcstub "$<" --cpp-client=NfRpcClient --cpp-client-file="$@"
