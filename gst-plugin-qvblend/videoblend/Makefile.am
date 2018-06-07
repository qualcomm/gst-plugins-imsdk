plugin_LTLIBRARIES = libgstvideoblend.la

libgstvideoblend_la_SOURCES = \
							  ../c2d/c2d_blend.cpp \
							  gstvideoblend.cpp

nodist_libgstvideoblend_la_SOURCES =
libgstvideoblend_la_CFLAGS = \
							 -I ../c2d \
							 -DUSE_GBM \
							 -fpermissive \
							 $(GST_PLUGINS_BASE_CFLAGS) \
							 $(GST_BASE_CFLAGS) \
							 $(GST_CFLAGS)
libgstvideoblend_la_CXXFLAGS = \
							   -I ../c2d \
							   -DUSE_GBM \
							   -fpermissive \
							   $(GST_PLUGINS_BASE_CFLAGS) \
							   $(GST_BASE_CFLAGS) \
							   $(GST_CFLAGS)
libgstvideoblend_la_LIBADD = \
							 $(GST_PLUGINS_BASE_LIBS) \
							 $(GST_BASE_LIBS) \
							 $(GST_LIBS) \
							 $(LIBM)
libgstvideoblend_la_LDFLAGS = \
							  -lgstionbuf-${GST_API_VERSION} \
							  -lgstvideo-@GST_API_VERSION@ \
							  $(GST_PLUGIN_LDFLAGS)
libgstvideoblend_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
				 ../c2d/c2d_blend.h \
				 gstvideoblend.h \
				 gstvideoblendpad.h
