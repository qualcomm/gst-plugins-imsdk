# Note: plugindir is set in configure

## Plugin (QTI Screen Shot SRC)
plugin_LTLIBRARIES = libgstqscreencapsrc.la

# sources used to compile this plug-in
libgstqscreencapsrc_la_SOURCES = qscreencaputil.c \
	gstqscreencapsrc.c \
	gbm-buffer-backend-protocol.c \
	xdg-shell-protocol.c \
	ivi-application-protocol.c \
	screen-capture-protocol.c


# headers we need but don't want installed
noinst_HEADERS = \
	qscreencaputil.h \
	gstqscreencapsrc.h \
	xdg-shell-client-protocol.h \
	gbm-buffer-backend-client-protocol.h \
	ivi-application-client-protocol.h \
	screen-capture-client-protocol.h

EXTRA_DIST = ivi-application.xml gbm-buffer-backend.xml xdg-shell.xml screen-capture.xml
CLEANFILES = ivi-application-protocol.c ivi-application-client-protocol.h gbm-buffer-backend-protocol.c gbm-buffer-backend-client-protocol.h xdg-shell-protocol.c xdg-shell-client-protocol.h screen-capture-protocol.c  screen-capture-client-protocol.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstqscreencapsrc_la_CFLAGS = $(GST_CFLAGS) -DUSE_GBM
libgstqscreencapsrc_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lwayland-client -ldrm -lgstionbuf-1.0
libgstqscreencapsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstqscreencapsrc_la_LIBTOOLFLAGS = --tag=disable-static



%-protocol.c : %.xml
	$(wayland_scanner) code < $< > $@

%-client-protocol.h : %.xml
	$(wayland_scanner) client-header < $< > $@


