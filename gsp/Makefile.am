noinst_LTLIBRARIES = libgsp.la
bin_PROGRAMS = democrit-gsp

libgsp_la_CXXFLAGS = \
  $(JSON_CFLAGS) $(XAYAGAME_CFLAGS) $(SQLITE_CFLAGS) $(GLOG_CFLAGS)
libgsp_la_LIBADD = \
  $(JSON_LIBS) $(XAYAGAME_LIBS) $(SQLITE_LIBS) $(GLOG_LIBS)
libgsp_la_SOURCES = \
  game.cpp
noinst_HEADERS = \
  game.hpp

democrit_gsp_CXXFLAGS = \
  $(GLOGS_CFLAGS) $(GFLAGS_CFLAGS)
democrit_gsp_LDADD = \
  $(builddir)/libgsp.la \
  $(GLAGS_LIBS) $(GFLAGS_LIBS)
democrit_gsp_SOURCES = main.cpp
