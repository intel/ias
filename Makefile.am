ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS =
noinst_PROGRAMS =
libexec_PROGRAMS =
moduledir = $(libdir)/ias
module_LTLIBRARIES =
libweston_moduledir = $(libdir)/libias-$(LIBWESTON_MAJOR)
libweston_module_LTLIBRARIES =
noinst_LTLIBRARIES =
plugindir = $(libdir)/ias
plugin_LTLIBRARIES =
BUILT_SOURCES = plugins
exampledir = $(bindir)/../share/ias/examples
example_PROGRAMS =

AM_DISTCHECK_CONFIGURE_FLAGS = --disable-setuid-install

EXTRA_DIST = weston.ini.in ivi-shell/weston.ini.in

weston.ini : $(srcdir)/weston.ini.in
	$(AM_V_GEN)$(SED) \
		-e 's|@bindir[@]|$(bindir)|g' \
		-e 's|@abs_top_builddir[@]|$(abs_top_builddir)|g' \
		-e 's|@libexecdir[@]|$(libexecdir)|g' \
		$< > $@

ivi-shell/weston.ini : $(srcdir)/ivi-shell/weston.ini.in
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(SED) \
		-e 's|@bindir[@]|$(bindir)|g' \
		-e 's|@libexecdir[@]|$(libexecdir)|g' \
		-e 's|@westondatadir[@]|$(westondatadir)|g' \
		$< > $@

all-local : weston.ini ivi-shell/weston.ini

AM_CFLAGS = $(GCC_CFLAGS)

AM_CPPFLAGS = 					\
	-I$(top_builddir)/libweston		\
	-I$(top_srcdir)/libweston		\
	-I$(top_builddir)/clients		\
	-I$(top_builddir)/tests			\
	-I$(top_srcdir)/shared			\
	-I$(top_builddir)/protocol		\
	-DLIBWESTON_MODULEDIR='"$(libweston_moduledir)"' \
	-DLIBEXECDIR='"$(libexecdir)"'		\
	-DBINDIR='"$(bindir)"'

CLEANFILES = weston.ini				\
	ivi-shell/weston.ini			\
	internal-screenshot-00.png		\
	$(BUILT_SOURCES)

# Libtool race fix
# libweston-desktop depends on libweston, and desktop-shell depends on both.
# This leads to a libtool race at installation, because libtool re-links
# everything.
# If you add more fixes, you may need a workaround to keep automake generated
# targets. See http://debbugs.gnu.org/cgi/bugreport.cgi?bug=7328
install-libweston_moduleLTLIBRARIES install-moduleLTLIBRARIES: install-libLTLIBRARIES

lib_LTLIBRARIES = libias-@LIBWESTON_MAJOR@.la
libias_@LIBWESTON_MAJOR@_la_CPPFLAGS = $(AM_CPPFLAGS)
libias_@LIBWESTON_MAJOR@_la_CFLAGS = $(AM_CFLAGS) \
	$(COMPOSITOR_CFLAGS) $(EGL_CFLAGS) $(LIBDRM_CFLAGS)
libias_@LIBWESTON_MAJOR@_la_LIBADD = $(COMPOSITOR_LIBS) \
	$(DL_LIBS) -lm $(CLOCK_GETTIME_LIBS) \
	$(LIBINPUT_BACKEND_LIBS) libshared.la
libias_@LIBWESTON_MAJOR@_la_LDFLAGS = -version-info $(LT_VERSION_INFO)

libias_@LIBWESTON_MAJOR@_la_SOURCES =			\
	libweston/git-version.h				\
	libweston/log.c					\
	libweston/compositor.c				\
	libweston/compositor.h				\
	libweston/compositor-drm.h			\
	libweston/compositor-fbdev.h			\
	libweston/compositor-headless.h			\
	libweston/compositor-rdp.h			\
	libweston/compositor-wayland.h			\
	libweston/compositor-x11.h			\
	libweston/input.c				\
	libweston/data-device.c				\
	libweston/screenshooter.c			\
	libweston/touch-calibration.c			\
	libweston/clipboard.c				\
	libweston/zoom.c				\
	libweston/bindings.c				\
	libweston/animation.c				\
	libweston/noop-renderer.c			\
	libweston/pixman-renderer.c			\
	libweston/pixman-renderer.h			\
	libweston/plugin-registry.c				\
	libweston/plugin-registry.h				\
	libweston/timeline.c				\
	libweston/timeline.h				\
	libweston/timeline-object.h			\
	libweston/linux-dmabuf.c			\
	libweston/linux-dmabuf.h			\
	libweston/linux-explicit-synchronization.c	\
	libweston/linux-explicit-synchronization.h	\
	libweston/pixel-formats.c			\
	libweston/pixel-formats.h			\
	libweston/weston-debug.c			\
	libweston/weston-debug.h			\
	shared/helpers.h				\
	shared/matrix.c					\
	shared/matrix.h					\
	shared/timespec-util.h				\
	shared/zalloc.h					\
	shared/platform.h				\
	shared/weston-egl-ext.h

libias_@LIBWESTON_MAJOR@_datadir = $(datadir)/weston/protocols
dist_libias_@LIBWESTON_MAJOR@_data_DATA =	\
	protocol/weston-debug.xml

lib_LTLIBRARIES += libias-desktop-@LIBWESTON_MAJOR@.la
libias_desktop_@LIBWESTON_MAJOR@_la_CPPFLAGS = $(AM_CPPFLAGS)
libias_desktop_@LIBWESTON_MAJOR@_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
libias_desktop_@LIBWESTON_MAJOR@_la_LIBADD =		\
	libias-@LIBWESTON_MAJOR@.la			\
	$(COMPOSITOR_LIBS)
libias_desktop_@LIBWESTON_MAJOR@_la_LDFLAGS = -version-info $(LT_VERSION_INFO)

libias_desktop_@LIBWESTON_MAJOR@_la_SOURCES =	\
	libweston-desktop/client.c			\
	libweston-desktop/internal.h			\
	libweston-desktop/libweston-desktop.c		\
	libweston-desktop/libweston-desktop.h		\
	libweston-desktop/seat.c			\
	libweston-desktop/surface.c			\
	libweston-desktop/wl-shell.c			\
	libweston-desktop/xdg-shell-v6.c		\
	libweston-desktop/xwayland.c

nodist_libias_desktop_@LIBWESTON_MAJOR@_la_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-server-protocol.h

BUILT_SOURCES += $(nodist_libias_desktop_@LIBWESTON_MAJOR@_la_SOURCES)

libias-desktop-@LIBWESTON_MAJOR@.la libweston-desktop/libias_desktop_@LIBWESTON_MAJOR@_la-xdg-shell-v6.lo: protocol/xdg-shell-unstable-v6-server-protocol.h

if SYSTEMD_NOTIFY_SUPPORT
module_LTLIBRARIES += systemd-notify.la
systemd_notify_la_LDFLAGS = -module -avoid-version
systemd_notify_la_LIBADD = libias-@LIBWESTON_MAJOR@.la $(SYSTEMD_DAEMON_LIBS)
systemd_notify_la_CFLAGS =			\
	$(COMPOSITOR_CFLAGS)			\
	$(SYSTEMD_DAEMON_CFLAGS)		\
	$(PIXMAN_CFLAGS)			\
	$(AM_CFLAGS)
systemd_notify_la_SOURCES =			\
	compositor/systemd-notify.c		\
	shared/helpers.h			\
	shared/zalloc.h				\
	libweston/compositor.h
endif

nodist_libias_@LIBWESTON_MAJOR@_la_SOURCES =				\
	protocol/weston-screenshooter-protocol.c			\
	protocol/weston-screenshooter-server-protocol.h			\
	protocol/weston-debug-protocol.c				\
	protocol/weston-debug-server-protocol.h				\
	protocol/text-cursor-position-protocol.c	\
	protocol/text-cursor-position-server-protocol.h	\
	protocol/text-input-unstable-v1-protocol.c			\
	protocol/text-input-unstable-v1-server-protocol.h		\
	protocol/input-method-unstable-v1-protocol.c			\
	protocol/input-method-unstable-v1-server-protocol.h		\
	protocol/presentation-time-protocol.c		\
	protocol/presentation-time-server-protocol.h	\
	protocol/viewporter-protocol.c			\
	protocol/viewporter-server-protocol.h		\
	protocol/linux-dmabuf-unstable-v1-protocol.c	\
	protocol/linux-dmabuf-unstable-v1-server-protocol.h		\
	protocol/relative-pointer-unstable-v1-protocol.c		\
	protocol/relative-pointer-unstable-v1-server-protocol.h		\
	protocol/pointer-constraints-unstable-v1-protocol.c		\
	protocol/pointer-constraints-unstable-v1-server-protocol.h      \
	protocol/input-timestamps-unstable-v1-protocol.c		\
	protocol/input-timestamps-unstable-v1-server-protocol.h		\
	protocol/weston-touch-calibration-protocol.c			\
	protocol/weston-touch-calibration-server-protocol.h		\
	protocol/linux-explicit-synchronization-unstable-v1-protocol.c	\
	protocol/linux-explicit-synchronization-unstable-v1-server-protocol.h

BUILT_SOURCES += $(nodist_libias_@LIBWESTON_MAJOR@_la_SOURCES)

bin_PROGRAMS += ias-weston

ias_weston_LDFLAGS = -export-dynamic -pie
ias_weston_CPPFLAGS = $(AM_CPPFLAGS) -DMODULEDIR='"$(moduledir)"' \
				 -DXSERVER_PATH='"@XSERVER_PATH@"'
ias_weston_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBINPUT_BACKEND_CFLAGS) \
					$(PTHREAD_CFLAGS) $(LIBEVDEV_CFLAGS) -fPIE
ias_weston_LDADD = libshared.la libias-@LIBWESTON_MAJOR@.la \
	$(COMPOSITOR_LIBS) \
	$(DL_LIBS) $(LIBINPUT_BACKEND_LIBS) \
	$(CLOCK_GETRES_LIBS) \
	$(PTHREAD_LIBS) \
	$(LIBEVDEV_LIBS) \
	-lm

ias_weston_SOURCES = 					\
	compositor/main.c				\
	compositor/weston-screenshooter.c		\
	compositor/text-backend.c			\
	compositor/xwayland.c

# Track this dependency explicitly instead of using BUILT_SOURCES.  We
# add BUILT_SOURCES to CLEANFILES, but we want to keep git-version.h
# in case we're building from tarballs.

compositor/main.c : $(top_builddir)/libweston/git-version.h
libweston/compositor.c : $(top_builddir)/libweston/git-version.h

noinst_LTLIBRARIES +=				\
	libsession-helper.la

libsession_helper_la_SOURCES =			\
	libweston/launcher-util.c		\
	libweston/launcher-util.h		\
	libweston/launcher-impl.h		\
	libweston/weston-launch.h		\
	libweston/launcher-weston-launch.c	\
	libweston/launcher-direct.c
libsession_helper_la_CFLAGS = $(AM_CFLAGS) $(LIBDRM_CFLAGS) $(PIXMAN_CFLAGS) $(COMPOSITOR_CFLAGS)
libsession_helper_la_LIBADD = libias-@LIBWESTON_MAJOR@.la

if ENABLE_DRM_COMPOSITOR
libsession_helper_la_LIBADD += $(LIBDRM_LIBS)
endif

if ENABLE_DBUS
if HAVE_SYSTEMD_LOGIN
libsession_helper_la_SOURCES +=			\
	libweston/dbus.h			\
	libweston/dbus.c			\
	libweston/launcher-logind.c
libsession_helper_la_CFLAGS += $(SYSTEMD_LOGIN_CFLAGS) $(DBUS_CFLAGS)
libsession_helper_la_LIBADD += $(SYSTEMD_LOGIN_LIBS) $(DBUS_LIBS)
endif
endif

if HAVE_GIT_REPO
libweston/git-version.h : $(top_srcdir)/.git/logs/HEAD
	$(AM_V_GEN)echo "#define BUILD_ID \"$(shell git --git-dir=$(top_srcdir)/.git describe --always --dirty) $(shell git --git-dir=$(top_srcdir)/.git log -1 --format='%s (%ci)')\"" > $@
else
libweston/git-version.h :
	$(AM_V_GEN)echo "#define BUILD_ID \"unknown (not built from git or tarball)\"" > $@

endif

.FORCE :

if BUILD_WESTON_LAUNCH
bin_PROGRAMS += ias-weston-launch
ias_weston_launch_SOURCES = libweston/weston-launch.c libweston/weston-launch.h
ias_weston_launch_CPPFLAGS = -DBINDIR='"$(bindir)"'
ias_weston_launch_CFLAGS=				\
	$(AM_CFLAGS)				\
	$(PAM_CFLAGS)				\
	$(SYSTEMD_LOGIN_CFLAGS)			\
	$(LIBDRM_CFLAGS)			\
	-fPIE
ias_weston_launch_LDADD = $(PAM_LIBS) $(SYSTEMD_LOGIN_LIBS)
ias_weston_launch_LDFLAGS = -pie

if ENABLE_DRM_COMPOSITOR
ias_weston_launch_LDADD += $(LIBDRM_LIBS)
endif

if ENABLE_SETUID_INSTALL
install-exec-hook:
	can_suid_files=no; \
	chown root $(DESTDIR)$(bindir)/weston-launch \
		&& chmod u+s $(DESTDIR)$(bindir)/weston-launch \
		&& can_suid_files=yes;\
	if test $$can_suid_files = no; then \
		echo 'Error: unable to unable to change ownership/setuid on weston-launch.'; \
		echo 'To skip this step, re-run ./configure using --disable-setuid-install'; \
		false; \
	fi
endif

endif # BUILD_WESTON_LAUNCH

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	libweston/libweston-${LIBWESTON_MAJOR}.pc \
	libweston-desktop/libweston-desktop-${LIBWESTON_MAJOR}.pc \
	compositor/weston.pc

noarch_pkgconfigdir = $(datadir)/pkgconfig
noarch_pkgconfig_DATA = \
	libweston/libweston-${LIBWESTON_MAJOR}-protocols.pc

wayland_sessiondir = $(datadir)/wayland-sessions
dist_wayland_session_DATA = compositor/ias-weston.desktop

libwestonincludedir = $(includedir)/libias-${LIBWESTON_MAJOR}
libwestoninclude_HEADERS =				\
	libweston/version.h				\
	libweston/compositor.h			\
	libweston/ias-plugin-framework-definitions.h \
	libweston/ias-spug.h				\
	libweston/ias-common.h \
	libweston/compositor-ias.h			\
	libweston/compositor-drm.h			\
	libweston/compositor-fbdev.h			\
	libweston/compositor-headless.h		\
	libweston/compositor-rdp.h			\
	libweston/compositor-wayland.h		\
	libweston/compositor-x11.h		\
	libweston/windowed-output-api.h		\
	libweston/plugin-registry.h		\
	libweston/timeline-object.h		\
	shared/platform.h			\
	shared/weston-egl-ext.h \
	shared/matrix.h				\
	shared/config-parser.h			\
	shared/zalloc.h

libwestoninclude_HEADERS +=			\
	libweston-desktop/libweston-desktop.h

westonincludedir = $(includedir)/weston
westoninclude_HEADERS = compositor/weston.h

if ENABLE_IVI_SHELL
westoninclude_HEADERS +=				\
	ivi-shell/ivi-layout-export.h
endif

if ENABLE_EGL
libweston_module_LTLIBRARIES += gl-renderer.la
gl_renderer_la_LDFLAGS = -module -avoid-version
gl_renderer_la_LIBADD =				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(EGL_LIBS)				\
	$(COMPOSITOR_LIBS)
gl_renderer_la_CFLAGS =				\
	$(COMPOSITOR_CFLAGS)			\
	$(EGL_CFLAGS)				\
	$(LIBDRM_CFLAGS)			\
	$(AM_CFLAGS)
gl_renderer_la_SOURCES =			\
	libweston/gl-renderer.h			\
	libweston/gl-renderer.c			\
	libweston/vertex-clipping.c		\
	libweston/vertex-clipping.h		\
	libweston/weston-sync-file.h
if ENABLE_VM
gl_renderer_la_SOURCES +=			\
	libweston/vm-shared.h				\
	libweston/vm.h				\
	libweston/vm.c

if ENABLE_XEN_COMM_CHANNEL
module_LTLIBRARIES += vm-comm-xen.la
vm_comm_xen_la_LDFLAGS = -module -avoid-version
vm_comm_xen_la_LIBADD = -lxenvchan -lxenstore -lxenctrl
vm_comm_xen_la_SOURCES = libweston/vm_xen.c
endif

module_LTLIBRARIES += vm-comm-network.la
vm_comm_network_la_LDFLAGS = -module -avoid-version -pthread
vm_comm_network_la_SOURCES = libweston/vm_network.c
endif

endif

if ENABLE_X11_COMPOSITOR
libweston_module_LTLIBRARIES += x11-backend.la
x11_backend_la_LDFLAGS = -module -avoid-version
x11_backend_la_LIBADD =				\
	libshared-cairo.la			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(X11_COMPOSITOR_LIBS)			\
	$(COMPOSITOR_LIBS)
x11_backend_la_CFLAGS =				\
	$(AM_CFLAGS)				\
	$(COMPOSITOR_CFLAGS)			\
	$(EGL_CFLAGS)				\
	$(PIXMAN_CFLAGS)			\
	$(CAIRO_CFLAGS)				\
	$(X11_COMPOSITOR_CFLAGS)
x11_backend_la_SOURCES = 			\
	libweston/compositor-x11.c		\
	libweston/compositor-x11.h		\
	shared/helpers.h
endif

INPUT_BACKEND_CFLAGS = $(LIBINPUT_BACKEND_CFLAGS)
INPUT_BACKEND_LIBS = $(LIBINPUT_BACKEND_LIBS)
INPUT_BACKEND_SOURCES =				\
	libweston/libinput-seat.c		\
	libweston/libinput-seat.h		\
	libweston/libinput-device.c		\
	libweston/libinput-device.h		\
	shared/helpers.h

if ENABLE_DRM_COMPOSITOR
libweston_module_LTLIBRARIES += drm-backend.la
drm_backend_la_LDFLAGS = -module -avoid-version
drm_backend_la_LIBADD =				\
	libsession-helper.la			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)			\
	$(DRM_COMPOSITOR_LIBS)			\
	$(INPUT_BACKEND_LIBS)			\
	libshared.la				\
	$(CLOCK_GETTIME_LIBS)
drm_backend_la_CFLAGS =				\
	$(COMPOSITOR_CFLAGS)			\
	$(EGL_CFLAGS)				\
	$(DRM_COMPOSITOR_CFLAGS)		\
	$(INPUT_BACKEND_CFLAGS)			\
	$(AM_CFLAGS)
drm_backend_la_SOURCES =			\
	libweston/compositor-drm.c		\
	libweston/compositor-drm.h		\
	$(INPUT_BACKEND_SOURCES)		\
	shared/helpers.h			\
	shared/timespec-util.h			\
	libweston/libbacklight.c		\
	libweston/libbacklight.h

if ENABLE_VAAPI_RECORDER
drm_backend_la_SOURCES += libweston/vaapi-recorder.c libweston/vaapi-recorder.h
drm_backend_la_LIBADD += $(LIBVA_LIBS)
drm_backend_la_LDFLAGS += -pthread
drm_backend_la_CFLAGS += $(LIBVA_CFLAGS)
endif

# remoting
if ENABLE_REMOTING
libweston_module_LTLIBRARIES += remoting-plugin.la
remoting_plugin_la_LDFLAGS = -module -avoid-version
remoting_plugin_la_LIBADD =			\
	$(COMPOSITOR_LIBS)			\
	$(REMOTING_GST_LIBS)
remoting_plugin_la_CFLAGS =			\
	$(COMPOSITOR_CFLAGS)			\
	$(REMOTING_GST_CFLAGS)			\
	$(AM_CFLAGS)
remoting_plugin_la_SOURCES =			\
	remoting/remoting-plugin.c		\
	remoting/remoting-plugin.h
endif

endif

if ENABLE_IAS_COMPOSITOR
libweston_module_LTLIBRARIES += ias-backend.la
ias_backend_la_LDFLAGS = -module -avoid-version
ias_backend_la_LIBADD =                        \
       $(COMPOSITOR_LIBS)                      \
       $(IAS_COMPOSITOR_LIBS)                  \
	   $(INPUT_BACKEND_LIBS)				\
       libshared.la  $(CLOCK_GETTIME_LIBS)			\
       libsession-helper.la                    \
       -lexpat
ias_backend_la_CFLAGS =                                \
       $(COMPOSITOR_CFLAGS)                    \
       $(EGL_CFLAGS)                                   \
       $(IAS_COMPOSITOR_CFLAGS)                \
       $(GCC_CFLAGS)
ias_backend_la_SOURCES =                       \
       libweston/compositor-ias.c                                \
       libweston/compositor-ias.h			\
	   $(INPUT_BACKEND_SOURCES)					\
       protocol/ias-backend-protocol.c                  \
       protocol/ias-backend-server-protocol.h   \
       libweston/ias-config.c							\
       libweston/ias-common.c							\
       libweston/ias-common.h							\
       libweston/ias-sprite.c							\
       libweston/ias-sprite.h							\
       libweston/ias-backend.h						\
       libweston/backend-classic.c					\
       libweston/backend-flexible.c
if ENABLE_FRAME_CAPTURE
ias_backend_la_SOURCES +=  \
       libweston/capture-proxy.c						\
       libweston/capture-proxy.h						\
       protocol/ias-shell-protocol.c			\
       protocol/ias-shell-server-protocol.h
ias_backend_la_LIBADD += $(LIBVA_LIBS)
ias_backend_la_CFLAGS += $(LIBVA_CFLAGS)
endif
endif

if ENABLE_WAYLAND_COMPOSITOR
libweston_module_LTLIBRARIES += wayland-backend.la
wayland_backend_la_LDFLAGS = -module -avoid-version
wayland_backend_la_LIBADD =			\
	libshared-cairo.la			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)			\
	$(WAYLAND_COMPOSITOR_EGL_LIBS)		\
	$(WAYLAND_COMPOSITOR_LIBS)
wayland_backend_la_CFLAGS =			\
	$(COMPOSITOR_CFLAGS)			\
	$(EGL_CFLAGS)				\
	$(PIXMAN_CFLAGS)			\
	$(CAIRO_CFLAGS)				\
	$(WAYLAND_COMPOSITOR_CFLAGS)		\
	$(AM_CFLAGS)
wayland_backend_la_SOURCES = 					\
	libweston/compositor-wayland.c				\
	libweston/compositor-wayland.h				\
	shared/helpers.h
nodist_wayland_backend_la_SOURCES =				\
	protocol/fullscreen-shell-unstable-v1-protocol.c	\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h	\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h
endif

if ENABLE_HEADLESS_COMPOSITOR
libweston_module_LTLIBRARIES += headless-backend.la
headless_backend_la_LDFLAGS = -module -avoid-version
headless_backend_la_LIBADD =			\
	libshared.la				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)
headless_backend_la_CFLAGS = $(COMPOSITOR_CFLAGS) $(AM_CFLAGS)
headless_backend_la_SOURCES = 			\
	libweston/compositor-headless.c		\
	libweston/compositor-headless.h		\
	shared/helpers.h
endif

if ENABLE_FBDEV_COMPOSITOR
libweston_module_LTLIBRARIES += fbdev-backend.la
fbdev_backend_la_LDFLAGS = -module -avoid-version
fbdev_backend_la_LIBADD =			\
	libshared.la				\
	libsession-helper.la			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)			\
	$(FBDEV_COMPOSITOR_LIBS)		\
	$(INPUT_BACKEND_LIBS)
fbdev_backend_la_CFLAGS =			\
	$(COMPOSITOR_CFLAGS)			\
	$(EGL_CFLAGS)				\
	$(FBDEV_COMPOSITOR_CFLAGS)		\
	$(PIXMAN_CFLAGS)			\
	$(INPUT_BACKEND_CFLAGS)			\
	$(AM_CFLAGS)
fbdev_backend_la_SOURCES =			\
	libweston/compositor-fbdev.c		\
	libweston/compositor-fbdev.h		\
	shared/helpers.h			\
	$(INPUT_BACKEND_SOURCES)
endif

if ENABLE_RDP_COMPOSITOR
libweston_module_LTLIBRARIES += rdp-backend.la
rdp_backend_la_LDFLAGS = -module -avoid-version
rdp_backend_la_LIBADD =				\
	libshared.la				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)		\
	$(RDP_COMPOSITOR_LIBS)
rdp_backend_la_CFLAGS =				\
	$(COMPOSITOR_CFLAGS)			\
	$(RDP_COMPOSITOR_CFLAGS)		\
	$(AM_CFLAGS)
rdp_backend_la_SOURCES = 			\
	libweston/compositor-rdp.c		\
	libweston/compositor-rdp.h		\
	shared/helpers.h
endif

if HAVE_LCMS
module_LTLIBRARIES += cms-static.la
cms_static_la_LDFLAGS = -module -avoid-version
cms_static_la_LIBADD =				\
	libshared.la				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(LCMS_LIBS)				\
	$(COMPOSITOR_LIBS)
cms_static_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS) $(LCMS_CFLAGS)
cms_static_la_SOURCES =				\
	compositor/cms-static.c			\
	compositor/cms-helper.c			\
	compositor/cms-helper.h			\
	shared/helpers.h
if ENABLE_COLORD
module_LTLIBRARIES += cms-colord.la
cms_colord_la_LDFLAGS = -module -avoid-version
cms_colord_la_LIBADD =				\
	libshared.la				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COLORD_LIBS)				\
	$(LCMS_LIBS)				\
	$(COMPOSITOR_LIBS)
cms_colord_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS) $(COLORD_CFLAGS)
cms_colord_la_SOURCES =				\
	compositor/cms-colord.c			\
	compositor/cms-helper.c			\
	compositor/cms-helper.h			\
	shared/helpers.h
endif
endif

if ENABLE_IAS_SHELL

module_LTLIBRARIES += ias-shell.la

ias_shell_la_LDFLAGS = -module -avoid-version
ias_shell_la_LIBADD = $(COMPOSITOR_LIBS) -lexpat libshared.la
ias_shell_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS)
ias_shell_la_SOURCES =                 	\
    libweston/ias-shell.c                     \
    libweston/ias-relay-input.h               \
    libweston/ias-relay-input.c               \
    libweston/ias-hmi.c                       \
    libweston/ias-shell-config.c              \
    libweston/ias-shell.h                     \
    libweston/ias-hmi.h                       \
    libweston/ias-common.h                    \
    libweston/ias-common.c                    \
    libweston/ias-config.c
nodist_ias_shell_la_SOURCES =				\
	protocol/ias-shell-server-protocol.h    \
	protocol/ias-shell-protocol.c

BUILT_SOURCES += $(nodist_ias_shell_la_SOURCES)
endif

if ENABLE_IAS_PLUGIN_MANAGER
module_LTLIBRARIES += ias_plugin_framework.la

ias_plugin_framework_la_LDFLAGS = -module -avoid-version
ias_plugin_framework_la_LIBADD = $(COMPOSITOR_LIBS) \
					$(EGL_LIBS) \
					$(GLIB_LIBS) \
			       -lexpat \
			       libshared.la
ias_plugin_framework_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS) $(GLIB_CFLAGS)
ias_plugin_framework_la_SOURCES = libweston/ias-plugin-framework.c \
				libweston/ias-plugin-framework.h \
				libweston/ias-spug.c \
				libweston/ias-config.c
nodist_ias_plugin_framework_la_SOURCES =	protocol/ias-layout-manager-protocol.c \
				protocol/ias-layout-manager-server-protocol.h\
				protocol/ias-input-manager-protocol.c \
				protocol/ias-input-manager-server-protocol.h

# Stick the IAS protocol XMLs under {datadir}/doc/ias
protocol_doc_DATA = protocol/ias-shell.xml \
	protocol/ias-backend.xml \
	protocol/ias-layout-manager.xml \
	protocol/ivi-application.xml \
	protocol/ivi-controller.xml \
	protocol/ivi-hmi-controller.xml \
	protocol/ivi-input.xml
protocol_docdir = ${datarootdir}/doc/ias

BUILT_SOURCES += $(nodist_ias_plugin_framework_la_SOURCES)
endif

if ENABLE_IVI_PLUGIN_MANAGER
module_LTLIBRARIES += ivi_plugin_framework.la

ivi_plugin_framework_la_LDFLAGS = -module -avoid-version
ivi_plugin_framework_la_LIBADD = $(COMPOSITOR_LIBS) \
					$(EGL_LIBS) \
					$(GLIB_LIBS) \
			       -lexpat \
			       libshared.la
ivi_plugin_framework_la_CFLAGS = $(GCC_CFLAGS) \
				$(COMPOSITOR_CFLAGS) \
				$(LIBDRM_CFLAGS) \
				$(GLIB_CFLAGS) \
				-Iivi-shell
ivi_plugin_framework_la_SOURCES = libweston/ivi-plugin-framework.c \
				libweston/ias-plugin-framework.h \
				libweston/ias-spug.c \
				libweston/ias-config.c
nodist_ivi_plugin_framework_la_SOURCES =	protocol/ias-layout-manager-protocol.c \
				protocol/ias-layout-manager-server-protocol.h\
				protocol/ias-input-manager-protocol.c \
				protocol/ias-input-manager-server-protocol.h

BUILT_SOURCES += $(nodist_ivi_plugin_framework_la_SOURCES)
endif


if ENABLE_TRACE_REPORTER

module_LTLIBRARIES += trace-reporter.la

trace_reporter_la_LDFLAGS = -module -avoid-version
trace_reporter_la_LIBADD = $(COMPOSITOR_LIBS)
trace_reporter_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS)
trace_reporter_la_SOURCES = libweston/trace-reporter.c
nodist_trace_reporter_la_SOURCES =				 \
	protocol/trace-reporter-protocol.c \
	protocol/trace-reporter-server-protocol.h

BUILT_SOURCES += $(nodist_trace_reporter_la_SOURCES)
endif

if ENABLE_REMOTE_DISPLAY

module_LTLIBRARIES += transport_plugin_stub.la
transport_plugin_stub_la_LDFLAGS = -module -avoid-version
transport_plugin_stub_la_LIBADD =
transport_plugin_stub_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS)
transport_plugin_stub_la_SOURCES = clients/RemoteDisplay/transport_plugin_stub.c

module_LTLIBRARIES += transport_plugin_file.la
transport_plugin_file_la_LDFLAGS = -module -avoid-version
transport_plugin_file_la_LIBADD =  $(LIBDRM_LIBS) $(SIMPLE_CLIENT_LIBS) libshared.la -lm -ldrm_intel
transport_plugin_file_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS)
transport_plugin_file_la_SOURCES = clients/RemoteDisplay/transport_plugin_file.c

module_LTLIBRARIES += transport_plugin_avb.la
transport_plugin_avb_la_LDFLAGS = -module -avoid-version
transport_plugin_avb_la_LIBADD = $(LIBDRM_LIBS) $(SIMPLE_CLIENT_LIBS) libshared.la -lm -ldrm_intel \
								 -lgstreamer-1.0 -lgstbase-1.0 -lgstapp-1.0
transport_plugin_avb_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS) -I/usr/include/glib-2.0 \
								 -I/usr/include/gstreamer-1.0 -I/usr/lib/glib-2.0/include -I/usr/lib64/glib-2.0/include
transport_plugin_avb_la_SOURCES = clients/RemoteDisplay/transport_plugin_avb.c

module_LTLIBRARIES += transport_plugin_tcp.la
transport_plugin_tcp_la_LDFLAGS = -module -avoid-version
transport_plugin_tcp_la_LIBADD =  $(LIBDRM_LIBS) $(SIMPLE_CLIENT_LIBS) libshared.la -lm -ldrm_intel
transport_plugin_tcp_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS)
transport_plugin_tcp_la_SOURCES = clients/RemoteDisplay/transport_plugin_tcp.c

module_LTLIBRARIES += transport_plugin_udp.la
transport_plugin_udp_la_LDFLAGS = -module -avoid-version
transport_plugin_udp_la_LIBADD =  $(LIBDRM_LIBS) $(SIMPLE_CLIENT_LIBS) libshared.la -lm -ldrm_intel -lgstreamer-1.0 -lgstbase-1.0 -lgstapp-1.0
transport_plugin_udp_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS) -I/usr/include/glib-2.0 -I/usr/include/gstreamer-1.0 -I/usr/lib/glib-2.0/include -I/usr/lib64/glib-2.0/include
transport_plugin_udp_la_SOURCES = clients/RemoteDisplay/transport_plugin_udp.c

endif

if ENABLE_VMDISPLAY
noinst_LTLIBRARIES += libvmdisplay.la

libvmdisplay_la_LIBADD = $(COMPOSITOR_LIBS)	\
			$(EGL_LIBS)		\
			$(GLIB_LIBS)		\
			$(LIBDRM_LIBS)		\
			libshared.la -lm -ldrm_intel
libvmdisplay_la_CFLAGS = $(GCC_CFLAGS)		\
			$(COMPOSITOR_CFLAGS)	\
			$(LIBDRM_CFLAGS)	\
			$(GLIB_CFLAGS)		\
			$(AM_CFLAGS)
libvmdisplay_la_SOURCES = clients/vmdisplay/vmdisplay-parser.c \
		clients/vmdisplay/vmdisplay.c		\
		clients/vmdisplay/wayland-drm-protocol.c

vmdisplay_wayland_SOURCES =	clients/vmdisplay/vmdisplay-wayland.c
nodist_vmdisplay_wayland_SOURCES =				\
	protocol/ias-shell-server-protocol.h    \
	protocol/ias-shell-protocol.c
vmdisplay_wayland_CFLAGS = $(AM_CFLAGS) $(GCC_CFLAGS) $(LIBDRM_CFLAGS) $(COMPOSITOR_CFLAGS) $(SIMPLE_CLIENT_CFLAGS)
vmdisplay_wayland_LDADD = $(COMPOSITOR_LIBS) $(GLIB_LIBS) $(EGL_LIBS) $(SIMPLE_CLIENT_LIBS) $(LIBDRM_LIBS) \
	libshared.la libvmdisplay.la -lm -ldrm_intel \
	$(WAYLAND_COMPOSITOR_EGL_LIBS)		\
	$(WAYLAND_COMPOSITOR_LIBS)		\
	$(SIMPLE_EGL_CLIENT_LIBS)               \
	libias-@LIBWESTON_MAJOR@.la

vmdisplay_server_SOURCES = clients/vmdisplay/vmdisplay-server.cpp	\
		clients/vmdisplay/vmdisplay-server-network.cpp		\
		clients/vmdisplay/vmdisplay-server-hyperdmabuf.cpp
vmdisplay_server_CFLAGS = $(AM_CFLAGS) $(GCC_CFLAGS)
vmdisplay_server_LDADD =  $(GLIB_LIBS) \
	libshared.la libvmdisplay.la -lm -lpthread

vmdisplay_input_SOURCES = clients/vmdisplay/vmdisplay-input.cpp
nodist_vmdisplay_input_SOURCES = clients/vmdisplay/vmdisplay-server-network.cpp
vmdisplay_input_CFLAGS = $(AM_CFLAGS) $(GCC_CFLAGS)
vmdisplay_input_LDADD =  $(GLIB_LIBS) \
	libshared.la libvmdisplay.la -lm -lpthread
endif

# Stick a copy of compositor.h and the public plugin framework headers under
# {datadir}/doc/ias
ias_doc_DATA = libweston/ias-plugin-framework.h libweston/compositor.h
ias_docdir = ${datarootdir}/doc/ias

noinst_PROGRAMS += spring-tool
spring_tool_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
spring_tool_LDADD = $(COMPOSITOR_LIBS) -lm
spring_tool_SOURCES =				\
	libweston/spring-tool.c			\
	libweston/animation.c			\
	shared/matrix.c				\
	shared/matrix.h				\
	libweston/compositor.h

plugin_LTLIBRARIES += ias-shell-protocol.la
ias_shell_protocol_la_LDFLAGS = -module -avoid-version -shared
ias_shell_protocol_la_LIBADD = $(COMPOSITOR_LIBS) -lexpat libshared.la
ias_shell_protocol_la_CFLAGS = $(GCC_CFLAGS) $(COMPOSITOR_CFLAGS) $(LIBDRM_CFLAGS)
ias_shell_protocol_la_SOURCES = protocol/ias-shell-protocol.c
include_HEADERS = protocol/ias-shell-client-protocol.h

plugin_LTLIBRARIES += grid_layout.la
grid_layout_la_CFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared
grid_layout_la_SOURCES = plugins/grid_layout.c plugins/cursor_image.h
grid_layout_la_LDFLAGS = -module

# Stick a copy of the grid plugin source under {datadir}/doc/ias
grid_layout_doc_DATA = plugins/grid_layout.c
grid_layout_docdir = ${datarootdir}/doc/ias

plugin_LTLIBRARIES += cpp_example.la
cpp_example_la_CXXFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared
cpp_example_la_SOURCES = plugins/cpp_example/cursor_image.h plugins/cpp_example/grid_layout.cc
cpp_example_la_LDFLAGS = -module

# Stick a copy of the grid plugin source under {datadir}/doc/ias/plugin_samples/cpp_example
cpp_example_doc_DATA = plugins/cpp_example/grid_layout.cc
cpp_example_docdir = ${datarootdir}/doc/ias/plugin_samples/cpp_example

BUILT_SOURCES += plugins/cpp_example/cursor_image.h \
				plugins/cpp_example/grid_layout.cc

EXTRA_DIST += cpp_example.diff

# Explicitly pass the name of file to be patched to avoid race condition when
# building weston with multiple worker threads
plugins/cpp_example/grid_layout.cc: plugins/grid_layout.c
	cp plugins/grid_layout.c grid_layout_cpp_example.c
	patch -p0 grid_layout_cpp_example.c plugins/cpp_example/cpp_example.diff
	mv grid_layout_cpp_example.c plugins/cpp_example/grid_layout.cc
	rm -f grid_layout_cpp_example.c.orig

plugins/cpp_example/cursor_image.h: plugins/cursor_image.h
	cp plugins/cursor_image.h plugins/cpp_example/


plugin_LTLIBRARIES += extension_sample.la

extension_sample_la_SOURCES = plugins/extension_sample/cursor_image.h \
							  plugins/extension_sample/extension_sample.c \
							  plugins/extension_sample/new-extension-protocol.c \
							  plugins/extension_sample/new-extension-server-protocol.h

example_PROGRAMS += extension_test_client

extension_test_client_SOURCES = plugins/extension_sample/extension_test_client.c \
				plugins/extension_sample/new-extension-protocol.c \
				plugins/extension_sample/new-extension-client-protocol.h
extension_test_client_CFLAGS = $(CLIENT_CFLAGS) -fPIE
extension_test_client_LDADD = $(SIMPLE_CLIENT_LIBS)
extension_test_client_LDFLAGS = -pie

BUILT_SOURCES += plugins/extension_sample/new-extension-server-protocol.h \
	       plugins/extension_sample/new-extension-client-protocol.h \
	       plugins/extension_sample/new-extension-protocol.c \
		   plugins/extension_sample/cursor_image.h \
		   plugins/extension_sample/extension_sample.c

EXTRA_DIST += plugins/extension_sample/new-extension.xml plugins/extension_sample/extension_sample.diff

extension_sample_la_CFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared
extension_sample_la_LDFLAGS = -module

# Stick a copy of the plugin source and client source under
# {datadir}/doc/ias/plugin_samples/extension_sample
extension_sample_doc_DATA = plugins/extension_sample/extension_sample.c \
	plugins/extension_sample/extension_test_client.c \
	plugins/extension_sample/new-extension.xml
extension_sample_docdir = ${datarootdir}/doc/ias/plugin_samples/extension_sample

plugins/extension_sample/new-extension-server-protocol.h: plugins/extension_sample/new-extension.xml
	@wayland_scanner@ server-header < $< > $@

plugins/extension_sample/new-extension-client-protocol.h: plugins/extension_sample/new-extension.xml
	@wayland_scanner@ client-header < $< > $@

plugins/extension_sample/new-extension-protocol.c: plugins/extension_sample/new-extension.xml
	@wayland_scanner@ code < $< > $@

# Explicitly pass the name of file to be patched to avoid race condition when
# building weston with multiple worker threads
plugins/extension_sample/extension_sample.c: plugins/grid_layout.c
	cp plugins/grid_layout.c grid_layout_extension_sample.c
	patch -p0 grid_layout_extension_sample.c plugins/extension_sample/extension_sample.diff
	mv grid_layout_extension_sample.c plugins/extension_sample/extension_sample.c
	rm -f grid_layout_extension_sample.c.orig

plugins/extension_sample/cursor_image.h: plugins/cursor_image.h
	cp plugins/cursor_image.h plugins/extension_sample/

plugin_LTLIBRARIES += surface_gbc_control.la

surface_gbc_control_la_SOURCES = plugins/gamma_example/surface_gbc_control.c

surface_gbc_control_la_CFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared 

surface_gbc_control_la_LDFLAGS = -module -L$(WLD)/lib

# Stick a copy of the surface_gbc_control plugin source under {datadir}/doc/ias/plugin_samples/gamma_example
surface_gbc_control_doc_DATA = plugins/gamma_example/surface_gbc_control.c
surface_gbc_control_docdir = ${datarootdir}/doc/ias/plugin_samples/gamma_example

plugin_LTLIBRARIES += input.la

input_la_SOURCES = plugins/input/input.c

input_la_CFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared
input_la_LDFLAGS = -module -L$(WLD)/lib

# Stick a copy of the input plugin source under {datadir}/doc/ias/plugin_samples/input
input_doc_DATA = plugins/input/input.c
input_docdir = ${datarootdir}/doc/ias/plugin_samples/input

plugin_LTLIBRARIES += sprite_example.la

sprite_example_la_SOURCES = plugins/sprite_example/sprite_example.c

sprite_example_la_CFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared

sprite_example_la_LDFLAGS = -module

# Stick a copy of the grid plugin source under {datadir}/doc/ias/plugin_samples/sprite_example
sprite_example_doc_DATA = plugins/sprite_example/sprite_example.c
sprite_example_docdir = ${datarootdir}/doc/ias/plugin_samples/sprite_example

plugin_LTLIBRARIES += thumbnail_layout.la

thumbnail_layout_la_SOURCES = plugins/thumbnail/thumbnail_layout.c

thumbnail_layout_la_CFLAGS = $(COMPOSITOR_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/shared
thumbnail_layout_la_LDFLAGS = -module -L$(WLD)/lib

if BUILD_CLIENTS

example_PROGRAMS += weston-terminal weston-info weston-debug weston-screenshooter

libexec_PROGRAMS +=				\
	weston-desktop-shell			\
	weston-keyboard				\
	weston-simple-im			\
	ias-test-hmi

if ENABLE_IVI_SHELL
libexec_PROGRAMS +=				\
	weston-ivi-shell-user-interface
endif
if ENABLE_VMDISPLAY
libexec_PROGRAMS += vmdisplay-wayland		\
	vmdisplay-server			\
	vmdisplay-input
endif

demo_clients =					\
	weston-flower				\
	weston-image				\
	weston-cliptest				\
	weston-dnd				\
	weston-smoke				\
	weston-resizor				\
	weston-eventdemo			\
	weston-clickdot				\
	weston-confine				\
	weston-transformed			\
	weston-fullscreen			\
	weston-stacking				\
	weston-calibrator			\
	weston-touch-calibrator			\
	weston-scaler				\
	weston-simple-clock

if INSTALL_DEMO_CLIENTS
example_PROGRAMS += $(demo_clients)
else
noinst_PROGRAMS += $(demo_clients)
endif


if BUILD_SIMPLE_CLIENTS
demo_clients +=					\
	weston-simple-shm			\
	weston-simple-damage			\
	weston-simple-touch			\
	weston-presentation-shm			\
	weston-multi-resource		\
	wrandr						\
	surfctrl					\
	layoutctrl					\
	inputctrl 					\
	traceinfo

if ENABLE_REMOTE_DISPLAY
demo_clients += \
	remote-display \
	mediaplayer \
	remote-display-show
endif

weston_simple_shm_SOURCES = clients/simple-shm.c
nodist_weston_simple_shm_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/ias-shell-protocol.c		\
	protocol/ias-shell-client-protocol.h		\
	protocol/fullscreen-shell-unstable-v1-protocol.c	\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h	\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h
weston_simple_shm_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
weston_simple_shm_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la
weston_simple_shm_LDFLAGS = -pie

weston_simple_damage_SOURCES = clients/simple-damage.c
nodist_weston_simple_damage_SOURCES =		\
	protocol/viewporter-protocol.c		\
	protocol/viewporter-client-protocol.h	\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/fullscreen-shell-unstable-v1-protocol.c	\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h
weston_simple_damage_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
weston_simple_damage_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la
weston_simple_damage_LDFLAGS = -pie

weston_simple_touch_SOURCES = 			\
	clients/simple-touch.c			\
	shared/helpers.h
nodist_weston_simple_touch_SOURCES =			\
	protocol/ias-shell-protocol.c			\
	protocol/ias-shell-client-protocol.h 		\
	protocol/ivi-application-protocol.c             \
	protocol/ivi-application-client-protocol.h
weston_simple_touch_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
weston_simple_touch_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la
weston_simple_touch_LDFLAGS = -pie

weston_presentation_shm_SOURCES = 			\
	clients/presentation-shm.c			\
	shared/helpers.h
nodist_weston_presentation_shm_SOURCES =		\
	protocol/presentation-time-protocol.c		\
	protocol/presentation-time-client-protocol.h
weston_presentation_shm_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
weston_presentation_shm_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la -lm $(CLOCK_GETTIME_LIBS)
weston_presentation_shm_LDFLAGS = -pie

weston_multi_resource_SOURCES = clients/multi-resource.c
weston_multi_resource_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
weston_multi_resource_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la $(CLOCK_GETTIME_LIBS) -lm
weston_multi_resource_LDFLAGS = -pie

wrandr_SOURCES = clients/wrandr.c
nodist_wrandr_SOURCES =		\
       protocol/ias-backend-protocol.c		\
       protocol/ias-backend-client-protocol.h
wrandr_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
wrandr_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la
wrandr_LDFLAGS = -pie

surfctrl_SOURCES = clients/surfctrl.c
nodist_surfctrl_SOURCES =		\
       protocol/ias-shell-protocol.c		\
       protocol/ias-shell-client-protocol.h \
       protocol/ivi-application-protocol.c  \
       protocol/ivi-application-client-protocol.h
surfctrl_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
surfctrl_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la -lm
surfctrl_LDFLAGS = -pie

# Stick a copy of surfctrl.c under {datadir}/doc/ias
surfctrl_doc_DATA = clients/surfctrl.c
surfctrl_docdir = ${datarootdir}/doc/ias

layoutctrl_SOURCES = clients/layoutctrl.c
nodist_layoutctrl_SOURCES =		\
       protocol/ias-layout-manager-protocol.c		\
       protocol/ias-layout-manager-client-protocol.h
layoutctrl_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
layoutctrl_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la -lm
layoutctrl_LDFLAGS = -pie

# Stick a copy of layoutctrl.c under {datadir}/doc/ias
layoutctrl_doc_DATA = clients/layoutctrl.c
layoutctrl_docdir = ${datarootdir}/doc/ias

remote_display_SOURCES = 		\
	clients/RemoteDisplay/main.c	\
	clients/RemoteDisplay/main.h	\
	clients/RemoteDisplay/encoder.c \
	clients/RemoteDisplay/encoder.h \
	clients/RemoteDisplay/input_receiver.c \
	clients/RemoteDisplay/input_receiver.h \
	clients/RemoteDisplay/input_sender.h
nodist_remote_display_SOURCES =		\
		protocol/ias-shell-protocol.c		\
		protocol/ias-shell-client-protocol.h
remote_display_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) $(LIBVA_CFLAGS)
remote_display_LDADD = $(LIBDRM_LIBS) $(SIMPLE_CLIENT_LIBS) $(LIBVA_LIBS) libshared.la -lm -ldrm_intel -ldl -lpthread

remote_display_show_SOURCES = \
	clients/RemoteDisplay/remote-display-show.c
nodist_remote_display_show_SOURCES = \
	protocol/ias-shell-protocol.c \
	protocol/ias-shell-server-protocol.h \
	protocol/ivi-application-protocol.c \
	protocol/ivi-application-client-protocol.h
remote_display_show_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS)
remote_display_show_LDADD = $(LIBDRM_LIBS) $(SIMPLE_EGL_CLIENT_LIBS) libshared.la -lm -ldrm_intel -ldl -lpthread

mediaplayer_SOURCES = 		\
	clients/mediaplayer.c
mediaplayer_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -I/usr/include/glib-2.0 \
			-I/usr/include/gstreamer-1.0 -I/usr/lib/glib-2.0/include \
			-I/usr/lib64/glib-2.0/include
mediaplayer_LDADD = $(LIBDRM_LIBS) $(SIMPLE_CLIENT_LIBS) libshared.la -lm -lpthread \
			-lgstreamer-1.0 -lgstbase-1.0 -lgstapp-1.0 -lgstvideo-1.0 \
			-lgobject-2.0 -lglib-2.0

weston_clock_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS)

weston_simple_clock_SOURCES = clients/simple-clock.c
weston_simple_clock_LDADD = libtoytoolkit.la
weston_simple_clock_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_simple_clock_LDFLAGS = -pie

inputctrl_SOURCES = clients/inputctrl.c
nodist_inputctrl_SOURCES =		\
       protocol/ias-input-manager-protocol.c		\
       protocol/ias-input-manager-client-protocol.h
inputctrl_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
inputctrl_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la -lm
inputctrl_LDFLAGS = -pie


traceinfo_SOURCES = clients/traceinfo.c
nodist_traceinfo_SOURCES =		\
       protocol/trace-reporter-protocol.c	\
       protocol/trace-reporter-client-protocol.h
traceinfo_CFLAGS = $(AM_CFLAGS) $(SIMPLE_CLIENT_CFLAGS) -fPIE
traceinfo_LDADD = $(SIMPLE_CLIENT_LIBS) libshared.la
traceinfo_LDFLAGS = -pie

endif

if BUILD_SIMPLE_EGL_CLIENTS
demo_clients += weston-simple-egl
weston_simple_egl_SOURCES = clients/simple-egl.c
nodist_weston_simple_egl_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h	\
	protocol/ias-shell-protocol.c		\
	protocol/ias-shell-client-protocol.h
weston_simple_egl_CFLAGS = $(AM_CFLAGS) $(SIMPLE_EGL_CLIENT_CFLAGS) -fPIE
weston_simple_egl_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm
weston_simple_egl_LDFLAGS = -pie

demo_clients += weston-simple-egl-ms
weston_simple_egl_ms_SOURCES = clients/simple-egl-ms.c
nodist_weston_simple_egl_ms_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h	\
	protocol/ias-shell-protocol.c		\
	protocol/ias-shell-client-protocol.h
weston_simple_egl_ms_CFLAGS = $(AM_CFLAGS) $(SIMPLE_EGL_CLIENT_CFLAGS) -fPIE
weston_simple_egl_ms_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm
weston_simple_egl_ms_LDFLAGS = -pie
endif

demo_clients += weston-es2gears
weston_es2gears_SOURCES = clients/es2gears.c
weston_es2gears_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_es2gears_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm libtoytoolkit.la
weston_es2gears_LDFLAGS = -pie

demo_clients += eglinfo
eglinfo_SOURCES = clients/eglinfo.c
eglinfo_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
eglinfo_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm
eglinfo_LDFLAGS = -pie

if BUILD_IVI_SHARE_ENABLE
demo_clients += weston-simple-ivi-share
weston_simple_ivi_share_SOURCES = clients/simple-ivi-share.c
nodist_weston_simple_ivi_share_SOURCES =		\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h	\
	protocol/ivi-share-protocol.c		\
	protocol/ivi-shell-client-protocol.h
weston_simple_ivi_share_CFLAGS = $(AM_CFLAGS) $(SIMPLE_EGL_CLIENT_CFLAGS) -fPIE
weston_simple_ivi_share_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm
weston_simple_ivi_share_LDFLAGS = -pie

endif

if BUILD_SIMPLE_DMABUF_DRM_CLIENT
demo_clients += weston-simple-dmabuf-drm
weston_simple_dmabuf_drm_SOURCES =			\
	clients/simple-dmabuf-drm.c			\
	clients/simple-dmabuf-drm-data.h
nodist_weston_simple_dmabuf_drm_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/fullscreen-shell-unstable-v1-protocol.c	\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h \
	protocol/linux-dmabuf-unstable-v1-protocol.c \
	protocol/linux-dmabuf-unstable-v1-client-protocol.h
weston_simple_dmabuf_drm_CFLAGS = $(AM_CFLAGS) $(SIMPLE_DMABUF_DRM_CLIENT_CFLAGS) -fPIE
weston_simple_dmabuf_drm_LDADD = $(SIMPLE_DMABUF_DRM_CLIENT_LIBS) \
	$(LIBDRM_PLATFORM_FREEDRENO_LIBS) \
	$(LIBDRM_PLATFORM_ETNAVIV_LIBS)   \
	$(LIBDRM_PLATFORM_INTEL_LIBS)     \
	libshared.la
weston_simple_dmabuf_drm_LDFLAGS = -pie
endif

if BUILD_SIMPLE_DMABUF_V4L_CLIENT
demo_clients += weston-simple-dmabuf-v4l
weston_simple_dmabuf_v4l_SOURCES = clients/simple-dmabuf-v4l.c
nodist_weston_simple_dmabuf_v4l_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/fullscreen-shell-unstable-v1-protocol.c	\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h \
	protocol/linux-dmabuf-unstable-v1-protocol.c \
	protocol/linux-dmabuf-unstable-v1-client-protocol.h
weston_simple_dmabuf_v4l_CFLAGS = $(AM_CFLAGS) $(SIMPLE_DMABUF_V4L_CLIENT_CFLAGS) -fPIE
weston_simple_dmabuf_v4l_LDADD = $(SIMPLE_DMABUF_V4L_CLIENT_LIBS) libshared.la
weston_simple_dmabuf_v4l_LDFLAGS = -pie
endif

module_LTLIBRARIES += libwl_base.la

libwl_base_la_SOURCES =				\
	clients/wl_base/global_wayland.cpp		\
	clients/wl_base/global_wayland.h

nodist_libwl_base_la_SOURCES = \
	protocol/ias-backend-client-protocol.h		\
	protocol/ias-backend-protocol.c			\
	protocol/ias-shell-protocol.c			\
	protocol/ias-shell-client-protocol.h	\
	protocol/xdg-shell-unstable-v6-protocol.c			\
	protocol/xdg-shell-unstable-v6-client-protocol.h		\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h

BUILT_SOURCES += $(nodist_libwl_base_la_SOURCES)

libwl_base_la_LDFLAGS = -module -avoid-version
libwl_base_la_LIBADD =  	\
			libshared.la -lm -ldrm_intel -lwayland-client
libwl_base_la_CPPFLAGS = 	\
			$(LIBDRM_CFLAGS)	\
			$(AM_CFLAGS) -fPIC -fvisibility=default

module_LTLIBRARIES += libwl_disp.la

libwl_disp_la_SOURCES =				\
	clients/wl_disp/display_wayland.cpp		\
	clients/wl_disp/display_wayland.h

libwl_disp_la_LDFLAGS = -module -avoid-version
libwl_disp_la_LIBADD =  $(GLIB_LIBS)		\
			$(LIBDRM_LIBS)		\
			libshared.la
libwl_disp_la_CPPFLAGS = $(GCC_CFLAGS)		\
			$(LIBDRM_CFLAGS)	\
			$(GLIB_CFLAGS)		\
			$(AM_CFLAGS) -I$(top_srcdir)/clients/wl_base -DWL_EGL_PLATFORM \
			-fPIC -fvisibility=default

module_LTLIBRARIES += libias_hmi.la

libias_hmi_la_SOURCES =				\
	clients/ias_hmi/ias_hmi_reg.cpp		\
	clients/ias_hmi/ias_hmi_reg.h

libias_hmi_la_LDFLAGS = -module -avoid-version
libias_hmi_la_LIBADD =  $(GLIB_LIBS)		\
			$(LIBDRM_LIBS)		\
			libshared.la
libias_hmi_la_CPPFLAGS = $(GCC_CFLAGS)		\
			$(LIBDRM_CFLAGS)	\
			$(GLIB_CFLAGS)		\
			$(AM_CFLAGS) -I$(top_srcdir)/clients/wl_disp -I$(top_srcdir)/clients/wl_base \
			-fPIC -fvisibility=default

if BUILD_SIMPLE_DMABUF_EGL_CLIENT
demo_clients += weston-simple-dmabuf-egl
weston_simple_dmabuf_egl_SOURCES = clients/simple-dmabuf-egl.c
nodist_weston_simple_dmabuf_egl_SOURCES =		\
	protocol/xdg-shell-unstable-v6-protocol.c		\
	protocol/xdg-shell-unstable-v6-client-protocol.h	\
	protocol/fullscreen-shell-unstable-v1-protocol.c	\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h \
	protocol/linux-dmabuf-unstable-v1-protocol.c \
	protocol/linux-dmabuf-unstable-v1-client-protocol.h
weston_simple_dmabuf_egl_CFLAGS = $(AM_CFLAGS) $(SIMPLE_DMABUF_EGL_CLIENT_CFLAGS)
weston_simple_dmabuf_egl_LDADD = $(SIMPLE_DMABUF_EGL_CLIENT_LIBS) libshared.la -lm
endif

noinst_LTLIBRARIES += libtoytoolkit.la

libtoytoolkit_la_SOURCES =				\
	clients/window.c				\
	clients/window.h				\
	shared/helpers.h

nodist_libtoytoolkit_la_SOURCES =			\
	protocol/text-cursor-position-protocol.c	\
	protocol/text-cursor-position-client-protocol.h	\
	protocol/ias-layout-manager-protocol.c			\
	protocol/ias-layout-manager-client-protocol.h	\
	protocol/ias-input-manager-protocol.c			\
	protocol/ias-input-manager-client-protocol.h	\
	protocol/ias-shell-protocol.c			\
	protocol/ias-shell-client-protocol.h	\
	protocol/viewporter-protocol.c			\
	protocol/viewporter-client-protocol.h		\
	protocol/xdg-shell-unstable-v6-protocol.c			\
	protocol/xdg-shell-unstable-v6-client-protocol.h		\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h	\
	protocol/pointer-constraints-unstable-v1-protocol.c		\
	protocol/pointer-constraints-unstable-v1-client-protocol.h	\
	protocol/relative-pointer-unstable-v1-protocol.c		\
	protocol/relative-pointer-unstable-v1-client-protocol.h

BUILT_SOURCES += $(nodist_libtoytoolkit_la_SOURCES)


libtoytoolkit_la_LIBADD =			\
	$(CLIENT_LIBS)				\
	$(CAIRO_EGL_LIBS)			\
	libshared-cairo.la $(CLOCK_GETTIME_LIBS) -lm
libtoytoolkit_la_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) $(CAIRO_EGL_CFLAGS)

weston_flower_SOURCES = clients/flower.c
weston_flower_LDADD = libtoytoolkit.la
weston_flower_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_flower_LDFLAGS = -pie

weston_screenshooter_SOURCES =				\
	clients/screenshot.c
nodist_weston_screenshooter_SOURCES =			\
	protocol/weston-screenshooter-protocol.c	\
	protocol/weston-screenshooter-client-protocol.h
weston_screenshooter_LDADD = $(CLIENT_LIBS) libshared.la
weston_screenshooter_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_screenshooter_LDFLAGS = -pie

weston_terminal_SOURCES = 				\
	clients/terminal.c				\
	shared/helpers.h
weston_terminal_LDADD = libtoytoolkit.la -lutil
weston_terminal_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_terminal_LDFLAGS = -pie

weston_image_SOURCES = clients/image.c
weston_image_LDADD = libtoytoolkit.la
weston_image_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_image_LDFLAGS = -pie

weston_cliptest_SOURCES =				\
	clients/cliptest.c				\
	libweston/vertex-clipping.c			\
	libweston/vertex-clipping.h
weston_cliptest_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_cliptest_LDADD = libtoytoolkit.la
weston_cliptest_LDFLAGS = -pie

weston_dnd_SOURCES = 					\
	clients/dnd.c					\
	shared/helpers.h
weston_dnd_LDADD = libtoytoolkit.la
weston_dnd_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_dnd_LDFLAGS = -pie

weston_smoke_SOURCES = clients/smoke.c
weston_smoke_LDADD = libtoytoolkit.la
weston_smoke_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_smoke_LDFLAGS = -pie

weston_resizor_SOURCES = clients/resizor.c
weston_resizor_LDADD = libtoytoolkit.la
weston_resizor_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_resizor_LDFLAGS = -pie

weston_scaler_SOURCES = clients/scaler.c
weston_scaler_LDADD = libtoytoolkit.la
weston_scaler_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_scaler_LDFLAGS = -pie

if HAVE_CAIRO_GLESV2
demo_clients += weston-nested weston-nested-client

weston_nested_SOURCES = 				\
	clients/nested.c				\
	shared/helpers.h
weston_nested_LDADD = libtoytoolkit.la $(SERVER_LIBS)
weston_nested_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_nested_LDFLAGS = -pie

weston_nested_client_SOURCES = clients/nested-client.c
weston_nested_client_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm
weston_nested_client_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_nested_client_LDFLAGS = -pie
endif

weston_eventdemo_SOURCES = 				\
	clients/eventdemo.c				\
	shared/helpers.h
weston_eventdemo_LDADD = libtoytoolkit.la
weston_eventdemo_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_eventdemo_LDFLAGS = -pie

weston_clickdot_SOURCES = 				\
	clients/clickdot.c				\
	shared/helpers.h
weston_clickdot_LDADD = libtoytoolkit.la
weston_clickdot_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_clickdot_LDFLAGS = -pie

weston_confine_SOURCES = 				\
	clients/confine.c				\
	shared/helpers.h
weston_confine_LDADD = libtoytoolkit.la
weston_confine_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_confine_LDFLAGS = -pie

weston_transformed_SOURCES = clients/transformed.c
weston_transformed_LDADD = libtoytoolkit.la
weston_transformed_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_transformed_LDFLAGS = -pie

weston_fullscreen_SOURCES = clients/fullscreen.c
nodist_weston_fullscreen_SOURCES =			\
	protocol/fullscreen-shell-unstable-v1-protocol.c		\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h
weston_fullscreen_LDADD = libtoytoolkit.la
weston_fullscreen_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_fullscreen_LDFLAGS = -pie

weston_stacking_SOURCES = 				\
	clients/stacking.c				\
	shared/helpers.h
weston_stacking_LDADD = libtoytoolkit.la
weston_stacking_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_stacking_LDFLAGS = -pie

weston_calibrator_SOURCES = 				\
	clients/calibrator.c				\
	shared/helpers.h				\
	shared/matrix.c					\
	shared/matrix.h
weston_calibrator_LDADD = libtoytoolkit.la
weston_calibrator_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_calibrator_LDFLAGS = -pie

weston_touch_calibrator_SOURCES = 			\
	clients/touch-calibrator.c			\
	shared/helpers.h				\
	shared/matrix.c					\
	shared/matrix.h
nodist_weston_touch_calibrator_SOURCES =		\
	protocol/weston-touch-calibration-protocol.c	\
	protocol/weston-touch-calibration-client-protocol.h
weston_touch_calibrator_LDADD = libtoytoolkit.la
weston_touch_calibrator_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS)

if BUILD_SUBSURFACES_CLIENT
demo_clients += weston-subsurfaces
weston_subsurfaces_SOURCES = 			\
	clients/subsurfaces.c			\
	shared/helpers.h
weston_subsurfaces_CFLAGS =			\
	$(AM_CFLAGS)				\
	$(SIMPLE_EGL_CLIENT_CFLAGS)		\
	$(CLIENT_CFLAGS)			\
	-fPIE
weston_subsurfaces_LDADD = libtoytoolkit.la $(SIMPLE_EGL_CLIENT_LIBS) -lm
weston_subsurfaces_LDFLAGS = -pie
endif

if HAVE_PANGO
demo_clients += weston-editor
weston_editor_SOURCES = 			\
	clients/editor.c			\
	shared/helpers.h
nodist_weston_editor_SOURCES =			\
	protocol/text-input-unstable-v1-protocol.c		\
	protocol/text-input-unstable-v1-client-protocol.h
weston_editor_LDADD = libtoytoolkit.la $(PANGO_LIBS)
weston_editor_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) $(PANGO_CFLAGS) -fPIE
weston_editor_LDFLAGS = -pie
endif

weston_keyboard_SOURCES = clients/keyboard.c
nodist_weston_keyboard_SOURCES =				\
	protocol/weston-desktop-shell-client-protocol.h		\
	protocol/weston-desktop-shell-protocol.c		\
	protocol/input-method-unstable-v1-protocol.c		\
	protocol/input-method-unstable-v1-client-protocol.h
weston_keyboard_LDADD = libtoytoolkit.la
weston_keyboard_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_keyboard_LDFLAGS = -pie

weston_simple_im_SOURCES = clients/simple-im.c
nodist_weston_simple_im_SOURCES =		\
	protocol/input-method-unstable-v1-protocol.c		\
	protocol/input-method-unstable-v1-client-protocol.h
weston_simple_im_LDADD = $(CLIENT_LIBS)
weston_simple_im_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_simple_im_LDFLAGS = -pie

ias_test_hmi_SOURCES = clients/ias-test-hmi.c
nodist_ias_test_hmi_SOURCES =		\
       protocol/ias-shell-protocol.c            \
       protocol/ias-shell-client-protocol.h
ias_test_hmi_LDADD = $(CLIENT_LIBS) libtoytoolkit.la
ias_test_hmi_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -DDATADIR='"$(datadir)"' -fPIE
ias_test_hmi_LDFLAGS = -pie

weston_info_SOURCES = 					\
	clients/weston-info.c				\
	shared/helpers.h
nodist_weston_info_SOURCES =					\
	protocol/presentation-time-protocol.c			\
	protocol/presentation-time-client-protocol.h		\
	protocol/linux-dmabuf-unstable-v1-protocol.c		\
	protocol/linux-dmabuf-unstable-v1-client-protocol.h	\
	protocol/tablet-unstable-v2-protocol.c			\
	protocol/tablet-unstable-v2-client-protocol.h		\
	protocol/xdg-output-unstable-v1-protocol.c		\
	protocol/xdg-output-unstable-v1-client-protocol.h
weston_info_LDADD = $(WESTON_INFO_LIBS) libshared.la
weston_info_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_info_LDFLAGS = -pie

weston_debug_SOURCES = 					\
	clients/weston-debug.c				\
	shared/helpers.h
nodist_weston_debug_SOURCES =				\
	protocol/weston-debug-protocol.c		\
	protocol/weston-debug-client-protocol.h
weston_debug_LDADD = $(WESTON_INFO_LIBS) libshared.la
weston_debug_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS)

weston_desktop_shell_SOURCES = 				\
	clients/desktop-shell.c				\
	shared/helpers.h
nodist_weston_desktop_shell_SOURCES =			\
	protocol/weston-desktop-shell-client-protocol.h	\
	protocol/weston-desktop-shell-protocol.c
weston_desktop_shell_LDADD = libtoytoolkit.la
weston_desktop_shell_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_desktop_shell_LDFLAGS = -pie

if ENABLE_IVI_SHELL
weston_ivi_shell_user_interface_SOURCES = 				\
	clients/ivi-shell-user-interface.c				\
	shared/helpers.h
nodist_weston_ivi_shell_user_interface_SOURCES =			\
	protocol/ivi-hmi-controller-client-protocol.h			\
	protocol/ivi-hmi-controller-protocol.c				\
	protocol/ivi-application-client-protocol.h			\
        protocol/ivi-application-protocol.c
weston_ivi_shell_user_interface_LDADD = libtoytoolkit.la
weston_ivi_shell_user_interface_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_ivi_shell_user_interface_LDFLAGS = -pie
endif

if BUILD_FULL_GL_CLIENTS
demo_clients += weston-gears
weston_gears_SOURCES = clients/gears.c
weston_gears_LDADD = libtoytoolkit.la
weston_gears_CFLAGS = $(AM_CFLAGS) $(CLIENT_CFLAGS) -fPIE
weston_gears_LDFLAGS = -pie
endif

endif

BUILT_SOURCES +=					\
	protocol/ias-backend-server-protocol.h		\
	protocol/ias-backend-client-protocol.h		\
	protocol/weston-screenshooter-protocol.c			\
	protocol/weston-screenshooter-client-protocol.h			\
	protocol/weston-touch-calibration-protocol.c			\
	protocol/weston-touch-calibration-client-protocol.h		\
	protocol/weston-debug-protocol.c				\
	protocol/weston-debug-client-protocol.h				\
	protocol/ias-backend-protocol.c				\
	protocol/ias-shell-server-protocol.h	\
	protocol/ias-shell-protocol.c		\
	protocol/ias-layout-manager-client-protocol.h	\
	protocol/ias-layout-manager-protocol.c		\
	protocol/ias-input-manager-client-protocol.h	\
	protocol/ias-input-manager-protocol.c		\
	protocol/text-cursor-position-client-protocol.h	\
	protocol/text-cursor-position-protocol.c	\
	protocol/text-input-unstable-v1-protocol.c			\
	protocol/text-input-unstable-v1-client-protocol.h		\
	protocol/input-method-unstable-v1-protocol.c			\
	protocol/input-method-unstable-v1-client-protocol.h		\
	protocol/weston-desktop-shell-client-protocol.h			\
	protocol/weston-desktop-shell-protocol.c			\
	protocol/viewporter-client-protocol.h		\
	protocol/viewporter-protocol.c			\
	protocol/presentation-time-protocol.c				\
	protocol/presentation-time-client-protocol.h			\
	protocol/fullscreen-shell-unstable-v1-protocol.c		\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h	\
	protocol/xdg-shell-unstable-v6-protocol.c			\
	protocol/xdg-shell-unstable-v6-client-protocol.h		\
	protocol/trace-reporter-protocol.c		\
	protocol/trace-reporter-client-protocol.h	\
	protocol/ivi-hmi-controller-protocol.c		\
	protocol/ivi-hmi-controller-client-protocol.h	\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-client-protocol.h	\
	protocol/linux-dmabuf-unstable-v1-protocol.c	\
	protocol/linux-dmabuf-unstable-v1-client-protocol.h		\
	protocol/tablet-unstable-v2-protocol.c		\
	protocol/tablet-unstable-v2-client-protocol.h			\
	protocol/input-timestamps-unstable-v1-protocol.c		\
	protocol/input-timestamps-unstable-v1-client-protocol.h		\
	protocol/xdg-output-unstable-v1-protocol.c			\
	protocol/xdg-output-unstable-v1-client-protocol.h		\
	protocol/linux-explicit-synchronization-unstable-v1-protocol.c	\
	protocol/linux-explicit-synchronization-unstable-v1-client-protocol.h

westondatadir = $(datadir)/ias
dist_westondata_DATA =				\
	data/wayland.svg			\
	data/wayland.png			\
	data/pattern.png			\
	data/terminal.png			\
	data/intel.png			\
	data/border.png				\
	data/icon_editor.png			\
	data/icon_flower.png			\
	data/icon_terminal.png			\
	data/icon_window.png			\
	data/sign_close.png			\
	data/sign_maximize.png			\
	data/sign_minimize.png

if ENABLE_IVI_SHELL
dist_westondata_DATA +=				\
	data/background.png                     \
	data/tiling.png                         \
	data/fullscreen.png                     \
	data/panel.png                          \
	data/random.png                         \
	data/sidebyside.png                     \
	data/home.png                           \
	data/icon_ivi_clickdot.png              \
	data/icon_ivi_flower.png                \
	data/icon_ivi_simple-egl.png            \
	data/icon_ivi_simple-shm.png            \
	data/icon_ivi_smoke.png
endif


if BUILD_WCAP_TOOLS
example_PROGRAMS += wcap-decode

wcap_decode_SOURCES =				\
	wcap/main.c				\
	wcap/wcap-decode.c			\
	wcap/wcap-decode.h

wcap_decode_CFLAGS = $(AM_CFLAGS) $(WCAP_CFLAGS) -fPIE
wcap_decode_LDADD = $(WCAP_LIBS)
wcap_decode_LDFLAGS = -pie
endif


if ENABLE_DESKTOP_SHELL

module_LTLIBRARIES += desktop-shell.la

desktop_shell_la_CPPFLAGS =			\
	-I$(top_builddir)/protocol		\
	-I$(top_srcdir)/shared			\
	-I$(top_builddir)/libweston		\
	-I$(top_srcdir)/libweston		\
	-I$(top_builddir)/desktop-shell		\
	-DMODULEDIR='"$(moduledir)"'		\
	-DLIBEXECDIR='"$(libexecdir)"'

desktop_shell_la_LDFLAGS = -module -avoid-version
desktop_shell_la_LIBADD = libshared.la libias-desktop-@LIBWESTON_MAJOR@.la $(COMPOSITOR_LIBS)
desktop_shell_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
desktop_shell_la_SOURCES =				\
	desktop-shell/shell.h				\
	desktop-shell/shell.c				\
	desktop-shell/exposay.c				\
	desktop-shell/input-panel.c			\
	shared/helpers.h
nodist_desktop_shell_la_SOURCES =			\
	protocol/weston-desktop-shell-protocol.c	\
	protocol/weston-desktop-shell-server-protocol.h

BUILT_SOURCES += $(nodist_desktop_shell_la_SOURCES)
endif

if ENABLE_FULLSCREEN_SHELL

module_LTLIBRARIES += fullscreen-shell.la

fullscreen_shell_la_CPPFLAGS =			\
	-I$(top_builddir)/protocol		\
	-I$(top_srcdir)/shared			\
	-I$(top_builddir)/libweston		\
	-I$(top_srcdir)/libweston

fullscreen_shell_la_LDFLAGS = -module -avoid-version
fullscreen_shell_la_LIBADD =			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)
fullscreen_shell_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
fullscreen_shell_la_SOURCES =			\
	fullscreen-shell/fullscreen-shell.c	\
	shared/helpers.h
nodist_fullscreen_shell_la_SOURCES =			\
	protocol/fullscreen-shell-unstable-v1-protocol.c		\
	protocol/fullscreen-shell-unstable-v1-server-protocol.h

BUILT_SOURCES += $(nodist_fullscreen_shell_la_SOURCES)
endif

if ENABLE_IVI_SHELL

module_LTLIBRARIES +=				\
	$(ivi_shell)				\
	$(hmi_controller)			\
	$(ivi_input_controller)		\
	$(ivi_controller)			\
	$(ilmCommon)				\
	$(ilmControl)				\
	$(ilmClient)

ivi_shell = ivi-shell.la
ivi_shell_la_LDFLAGS = -module -avoid-version
ivi_shell_la_LIBADD =				\
	libshared.la				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)
ivi_shell_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
ivi_shell_la_SOURCES =				\
	ivi-shell/ivi-layout-export.h		\
	ivi-shell/ivi-layout-private.h		\
	ivi-shell/ivi-layout-shell.h		\
	ivi-shell/ivi-layout.c			\
	ivi-shell/ivi-layout-transition.c	\
	ivi-shell/ivi-shell.h			\
	ivi-shell/ivi-shell.c			\
	shared/helpers.h
nodist_ivi_shell_la_SOURCES =			\
	protocol/ivi-application-protocol.c		\
	protocol/ivi-application-server-protocol.h

BUILT_SOURCES += $(nodist_ivi_shell_la_SOURCES)

hmi_controller = hmi-controller.la
hmi_controller_la_LDFLAGS = -module -avoid-version
hmi_controller_la_LIBADD =			\
	libshared.la				\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)
hmi_controller_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
hmi_controller_la_SOURCES =				\
	ivi-shell/ivi-layout-export.h			\
	ivi-shell/hmi-controller.c			\
	shared/helpers.h
nodist_hmi_controller_la_SOURCES =			\
	protocol/ivi-hmi-controller-protocol.c		\
	protocol/ivi-hmi-controller-server-protocol.h

BUILT_SOURCES += $(nodist_hmi_controller_la_SOURCES)

endif


if ENABLE_SCREEN_SHARING

module_LTLIBRARIES += screen-share.la

screen_share_la_CPPFLAGS = $(AM_CPPFLAGS) -DBINDIR='"$(bindir)"'
screen_share_la_LDFLAGS = -module -avoid-version
screen_share_la_LIBADD =			\
	libshared-cairo.la			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(COMPOSITOR_LIBS)			\
	$(SCREEN_SHARE_LIBS)
screen_share_la_CFLAGS =			\
	$(COMPOSITOR_CFLAGS)			\
	$(SCREEN_SHARE_CFLAGS)			\
	$(AM_CFLAGS)
screen_share_la_SOURCES =			\
	compositor/screen-share.c		\
	shared/helpers.h
nodist_screen_share_la_SOURCES =			\
	protocol/fullscreen-shell-unstable-v1-protocol.c		\
	protocol/fullscreen-shell-unstable-v1-client-protocol.h

endif

if ENABLE_XWAYLAND

libweston_module_LTLIBRARIES += xwayland.la

xwayland_la_CPPFLAGS =				\
	-I$(top_builddir)/protocol		\
	-I$(top_srcdir)/shared			\
	-I$(top_builddir)/libweston		\
	-I$(top_srcdir)/libweston		\
	-I$(top_builddir)/xwayland		\
	-DMODULEDIR='"$(moduledir)"'		\
	-DLIBEXECDIR='"$(libexecdir)"'

xwayland_la_LDFLAGS = -module -avoid-version
xwayland_la_LIBADD =				\
	libshared-cairo.la			\
	libias-@LIBWESTON_MAJOR@.la		\
	$(XWAYLAND_LIBS)
xwayland_la_CFLAGS =				\
	$(AM_CFLAGS)				\
	$(COMPOSITOR_CFLAGS)			\
	$(PIXMAN_CFLAGS)			\
	$(CAIRO_CFLAGS)
xwayland_la_SOURCES =				\
	xwayland/xwayland.h			\
	xwayland/xwayland-internal-interface.h	\
	xwayland/window-manager.c		\
	xwayland/selection.c			\
	xwayland/dnd.c				\
	xwayland/launcher.c			\
	xwayland/hash.c				\
	xwayland/hash.h				\
	shared/helpers.h

libwestoninclude_HEADERS += xwayland/xwayland-api.h

endif


#
# Shared utilities
#

noinst_LTLIBRARIES += libshared.la libshared-cairo.la \
	libzunitc.la libzunitcmain.la

libshared_la_CPPFLAGS = \
	-DDATADIR='"$(datadir)"' \
	$(AM_CPPFLAGS)

libshared_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)

libshared_la_SOURCES =				\
	shared/config-parser.c			\
	shared/option-parser.c			\
	shared/config-parser.h			\
	shared/file-util.c			\
	shared/file-util.h			\
	shared/helpers.h			\
	shared/os-compatibility.c		\
	shared/os-compatibility.h		\
	shared/xalloc.c			\
	shared/xalloc.h

libshared_cairo_la_CPPFLAGS = $(libshared_la_CPPFLAGS)

libshared_cairo_la_CFLAGS =			\
	$(AM_CFLAGS)				\
	$(COMPOSITOR_CFLAGS)			\
	$(PIXMAN_CFLAGS)			\
	$(CAIRO_CFLAGS)				\
	$(PANGO_CFLAGS)				\
	$(PNG_CFLAGS)				\
	$(WEBP_CFLAGS)

libshared_cairo_la_LIBADD =			\
	$(PIXMAN_LIBS)				\
	$(CAIRO_LIBS)				\
	$(PANGO_LIBS)				\
	$(PNG_LIBS)				\
	$(WEBP_LIBS)				\
	$(JPEG_LIBS)

libshared_cairo_la_SOURCES =			\
	$(libshared_la_SOURCES)			\
	shared/helpers.h			\
	shared/image-loader.c			\
	shared/image-loader.h			\
	shared/cairo-util.c			\
	shared/frame.c				\
	shared/cairo-util.h

libzunitc_la_SOURCES = \
	tools/zunitc/inc/zunitc/zunitc.h	\
	tools/zunitc/inc/zunitc/zunitc_impl.h	\
	tools/zunitc/src/zuc_base_logger.c	\
	tools/zunitc/src/zuc_base_logger.h	\
	tools/zunitc/src/zuc_collector.c	\
	tools/zunitc/src/zuc_collector.h	\
	tools/zunitc/src/zuc_context.h		\
	tools/zunitc/src/zuc_event.h		\
	tools/zunitc/src/zuc_event_listener.h	\
	tools/zunitc/src/zuc_junit_reporter.c	\
	tools/zunitc/src/zuc_junit_reporter.h	\
	tools/zunitc/src/zuc_types.h		\
	tools/zunitc/src/zunitc_impl.c		\
	shared/helpers.h

libzunitc_la_CFLAGS = \
	$(AM_CFLAGS)				\
	-I$(top_srcdir)/tools/zunitc/inc

libzunitc_la_LIBADD = \
	libshared.la \
	$(CLOCK_GETTIME_LIBS)

if ENABLE_JUNIT_XML
libzunitc_la_CFLAGS += \
	$(LIBXML2_CFLAGS)
libzunitc_la_LIBADD += \
	$(LIBXML2_LIBS)
endif

libzunitcmain_la_SOURCES = \
	tools/zunitc/src/main.c

libzunitcmain_la_CFLAGS = \
	$(AM_CFLAGS)				\
	-I$(top_srcdir)/tools/zunitc/inc

libzunitcmain_la_LIBADD =	\
	libzunitc.la		\
	libshared.la

#
# tests subdirectory
#

TESTS = $(internal_tests) $(shared_tests) $(module_tests) $(weston_tests) $(ivi_tests)

internal_tests = 				\
	internal-screenshot.weston

shared_tests =					\
	config-parser.test			\
	timespec.test				\
	string.test					\
	vertex-clip.test			\
	zuctest

module_tests =					\
	plugin-registry-test.la			\
	surface-test.la				\
	surface-global-test.la

weston_tests =					\
	bad_buffer.weston			\
	keyboard.weston				\
	event.weston				\
	pointer.weston				\
	text.weston				\
	presentation.weston			\
	viewporter.weston			\
	roles.weston				\
	subsurface.weston			\
	subsurface-shot.weston			\
	devices.weston				\
	touch.weston				\
	linux-explicit-synchronization.weston

AM_TESTS_ENVIRONMENT = \
	abs_builddir='$(abs_builddir)'; export abs_builddir; \
	abs_top_srcdir='$(abs_top_srcdir)'; export abs_top_srcdir;

TEST_EXTENSIONS = .la .weston
LA_LOG_COMPILER = $(srcdir)/tests/weston-tests-env
WESTON_LOG_COMPILER = $(srcdir)/tests/weston-tests-env

clean-local:
	-rm -rf logs
	-rm -rf $(DOCDIRS)

# To remove when automake 1.11 support is dropped
export abs_builddir

noinst_LTLIBRARIES +=			\
	weston-test.la			\
	weston-test-desktop-shell.la	\
	$(module_tests)			\
	libtest-runner.la		\
	libtest-client.la

noinst_PROGRAMS +=			\
	$(setbacklight)			\
	$(internal_tests)		\
	$(shared_tests)			\
	$(weston_tests)			\
	$(ivi_tests)			\
	matrix-test

test_module_ldflags = -module -avoid-version -rpath $(libdir)
test_module_libadd =			\
	libias-@LIBWESTON_MAJOR@.la	\
	$(COMPOSITOR_LIBS)

plugin_registry_test_la_SOURCES = tests/plugin-registry-test.c
plugin_registry_test_la_LIBADD = $(test_module_libadd)
plugin_registry_test_la_LDFLAGS = $(test_module_ldflags)
plugin_registry_test_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)

surface_global_test_la_SOURCES = tests/surface-global-test.c
surface_global_test_la_LIBADD = $(test_module_libadd)
surface_global_test_la_LDFLAGS = $(test_module_ldflags)
surface_global_test_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)

surface_test_la_SOURCES = tests/surface-test.c
surface_test_la_LIBADD = $(test_module_libadd)
surface_test_la_LDFLAGS = $(test_module_ldflags)
surface_test_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)

weston_test_la_LIBADD = libshared.la $(test_module_libadd)
weston_test_la_LDFLAGS = $(test_module_ldflags)
weston_test_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
weston_test_la_SOURCES = 			\
	tests/weston-test.c			\
	shared/helpers.h
nodist_weston_test_la_SOURCES =			\
	protocol/weston-test-protocol.c	\
	protocol/weston-test-server-protocol.h

weston_test_desktop_shell_la_LIBADD = libshared.la libias-desktop-@LIBWESTON_MAJOR@.la $(COMPOSITOR_LIBS)
weston_test_desktop_shell_la_LDFLAGS = $(test_module_ldflags)
weston_test_desktop_shell_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
weston_test_desktop_shell_la_SOURCES = 			\
	tests/weston-test-desktop-shell.c		\
	shared/helpers.h

libtest_runner_la_SOURCES =			\
	tests/weston-test-runner.c		\
	tests/weston-test-runner.h
libtest_runner_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)

config_parser_test_SOURCES = tests/config-parser-test.c
config_parser_test_LDADD =	\
	libshared.la		\
	$(COMPOSITOR_LIBS)	\
	libzunitc.la		\
	libzunitcmain.la
config_parser_test_CFLAGS =			\
	$(AM_CFLAGS)				\
	-I$(top_srcdir)/tools/zunitc/inc

timespec_test_SOURCES = tests/timespec-test.c
timespec_test_LDADD =	\
	libshared.la		\
	libzunitc.la		\
	libzunitcmain.la
timespec_test_CFLAGS =			\
	$(AM_CFLAGS)				\
	-I$(top_srcdir)/tools/zunitc/inc

string_test_SOURCES = \
	tests/string-test.c \
	shared/string-helpers.h
string_test_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
string_test_LDADD =	libtest-client.la

vertex_clip_test_SOURCES =			\
	tests/vertex-clip-test.c		\
	shared/helpers.h			\
	libweston/vertex-clipping.c		\
	libweston/vertex-clipping.h
vertex_clip_test_LDADD = libtest-runner.la -lm $(CLOCK_GETTIME_LIBS)

libtest_client_la_SOURCES =			\
	tests/weston-test-client-helper.c	\
	tests/weston-test-client-helper.h	\
	tests/input-timestamps-helper.c		\
	tests/input-timestamps-helper.h
nodist_libtest_client_la_SOURCES =				\
	protocol/weston-test-protocol.c				\
	protocol/weston-test-client-protocol.h			\
	protocol/input-timestamps-unstable-v1-protocol.c	\
	protocol/input-timestamps-unstable-v1-client-protocol.h
libtest_client_la_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS) $(CAIRO_CFLAGS)
libtest_client_la_LIBADD = libshared.la libtest-runner.la $(TEST_CLIENT_LIBS) $(CAIRO_LIBS)


#
# Internal tests - tests functionality of the testsuite itself
#

internal_screenshot_weston_SOURCES = tests/internal-screenshot-test.c
internal_screenshot_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
internal_screenshot_weston_LDADD = libtest-client.la


#
# Weston Tests
#

bad_buffer_weston_SOURCES = tests/bad-buffer-test.c
bad_buffer_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
bad_buffer_weston_LDADD = libtest-client.la

keyboard_weston_SOURCES = tests/keyboard-test.c
keyboard_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
keyboard_weston_LDADD = libtest-client.la

event_weston_SOURCES = tests/event-test.c
event_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
event_weston_LDADD = libtest-client.la

pointer_weston_SOURCES = tests/pointer-test.c
pointer_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
pointer_weston_LDADD = libtest-client.la

devices_weston_SOURCES = tests/devices-test.c
devices_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
devices_weston_LDADD = libtest-client.la

text_weston_SOURCES = tests/text-test.c
nodist_text_weston_SOURCES =			\
	protocol/text-input-unstable-v1-protocol.c		\
	protocol/text-input-unstable-v1-client-protocol.h
text_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
text_weston_LDADD = libtest-client.la

subsurface_weston_SOURCES = tests/subsurface-test.c
subsurface_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
subsurface_weston_LDADD = libtest-client.la

subsurface_shot_weston_SOURCES = tests/subsurface-shot-test.c
subsurface_shot_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
subsurface_shot_weston_LDADD = libtest-client.la

presentation_weston_SOURCES = 			\
	tests/presentation-test.c		\
	shared/helpers.h
nodist_presentation_weston_SOURCES =		\
	protocol/presentation-time-protocol.c	\
	protocol/presentation-time-client-protocol.h
presentation_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
presentation_weston_LDADD = libtest-client.la

roles_weston_SOURCES = tests/roles-test.c
roles_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
roles_weston_LDADD = libtest-client.la

viewporter_weston_SOURCES = 			\
	tests/viewporter-test.c		\
	shared/helpers.h
nodist_viewporter_weston_SOURCES =		\
	protocol/viewporter-protocol.c	\
	protocol/viewporter-client-protocol.h
viewporter_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
viewporter_weston_LDADD = libtest-client.la

touch_weston_SOURCES = tests/touch-test.c
touch_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
touch_weston_LDADD = libtest-client.la

linux_explicit_synchronization_weston_SOURCES = \
	tests/linux-explicit-synchronization-test.c
nodist_linux_explicit_synchronization_weston_SOURCES = \
	protocol/linux-explicit-synchronization-unstable-v1-protocol.c	\
	protocol/linux-explicit-synchronization-unstable-v1-client-protocol.h
linux_explicit_synchronization_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
linux_explicit_synchronization_weston_LDADD = libtest-client.la

if ENABLE_XWAYLAND_TEST
weston_tests +=	xwayland-test.weston
xwayland_test_weston_SOURCES = tests/xwayland-test.c
xwayland_test_weston_CFLAGS = \
	$(AM_CFLAGS) $(XWAYLAND_TEST_CFLAGS) -DXSERVER_PATH='"@XSERVER_PATH@"'
xwayland_test_weston_LDADD = libtest-client.la $(XWAYLAND_TEST_LIBS)
endif

matrix_test_SOURCES =				\
	tests/matrix-test.c			\
	shared/matrix.c				\
	shared/matrix.h
matrix_test_CPPFLAGS = -DUNIT_TEST
matrix_test_LDADD = -lm $(CLOCK_GETTIME_LIBS)

if ENABLE_IVI_SHELL
module_tests += 				\
	ivi-layout-internal-test.la		\
	ivi-layout-test.la

ivi_layout_internal_test_la_LIBADD = $(test_module_libadd)
ivi_layout_internal_test_la_LDFLAGS = $(test_module_ldflags)
ivi_layout_internal_test_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
ivi_layout_internal_test_la_SOURCES =			\
	tests/ivi-layout-internal-test.c

ivi_layout_test_la_LIBADD = $(test_module_libadd)
ivi_layout_test_la_LDFLAGS = $(test_module_ldflags)
ivi_layout_test_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
ivi_layout_test_la_SOURCES =			\
	tests/ivi-layout-test-plugin.c		\
	tests/ivi-test.h			\
	shared/helpers.h
nodist_ivi_layout_test_la_SOURCES =		\
	protocol/weston-test-protocol.c		\
	protocol/weston-test-server-protocol.h

ivi_tests =					\
	ivi-shell-app.weston

ivi_shell_app_weston_SOURCES = tests/ivi-shell-app-test.c
nodist_ivi_shell_app_weston_SOURCES =		\
	protocol/ivi-application-protocol.c	\
	protocol/ivi-application-client-protocol.h
ivi_shell_app_weston_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
ivi_shell_app_weston_LDADD = libtest-client.la

noinst_PROGRAMS += ivi-layout-test-client.ivi

ivi_layout_test_client_ivi_SOURCES =			\
	tests/ivi-layout-test-client.c 		\
	tests/ivi-test.h			\
	shared/helpers.h
nodist_ivi_layout_test_client_ivi_SOURCES = 		\
	protocol/ivi-application-protocol.c	\
	protocol/ivi-application-client-protocol.h
ivi_layout_test_client_ivi_CFLAGS = $(AM_CFLAGS) $(TEST_CLIENT_CFLAGS)
ivi_layout_test_client_ivi_LDADD = libtest-client.la
endif

if BUILD_SETBACKLIGHT
noinst_PROGRAMS += setbacklight
setbacklight_SOURCES =				\
	tests/setbacklight.c			\
	libweston/libbacklight.c		\
	libweston/libbacklight.h
setbacklight_CFLAGS = $(AM_CFLAGS) $(SETBACKLIGHT_CFLAGS)
setbacklight_LDADD = $(SETBACKLIGHT_LIBS)
endif

all-local: zuctest$(EXEEXT)

noinst_PROGRAMS += zuctest$(EXEEXT)

zuctest_LDADD =			\
	libzunitc.la		\
	libzunitcmain.la

zuctest_CFLAGS =				\
	$(AM_CFLAGS)				\
	-I$(top_srcdir)/tools/zunitc/inc

zuctest_SOURCES =				\
	tools/zunitc/test/fixtures_test.c	\
	tools/zunitc/test/zunitc_test.c

EXTRA_DIST +=							\
	tests/internal-screenshot.ini				\
	tests/reference/internal-screenshot-bad-00.png		\
	tests/reference/internal-screenshot-good-00.png		\
	tests/reference/subsurface_z_order-00.png		\
	tests/reference/subsurface_z_order-01.png		\
	tests/reference/subsurface_z_order-02.png		\
	tests/reference/subsurface_z_order-03.png		\
	tests/reference/subsurface_z_order-04.png		\
	tests/weston-tests-env

BUILT_SOURCES +=				\
	protocol/weston-test-protocol.c	\
	protocol/weston-test-server-protocol.h	\
	protocol/weston-test-client-protocol.h	\
	protocol/text-input-unstable-v1-protocol.c		\
	protocol/text-input-unstable-v1-client-protocol.h

EXTRA_DIST +=					\
	protocol/weston-desktop-shell.xml	\
	protocol/ias-shell.xml		\
	protocol/weston-screenshooter.xml	\
	protocol/text-cursor-position.xml	\
	protocol/weston-test.xml		\
	protocol/weston-touch-calibration.xml	\
	protocol/ias-backend.xml		\
	protocol/ias-layout-manager.xml		\
	protocol/ias-input-manager.xml		\
	protocol/trace-reporter.xml		\
	protocol/ivi-application.xml		\
	protocol/ivi-hmi-controller.xml

#
# manual test modules in tests subdirectory
#

noinst_LTLIBRARIES +=				\
	surface-screenshot.la

surface_screenshot_la_LIBADD = libshared.la $(test_module_libadd)
surface_screenshot_la_LDFLAGS = $(test_module_ldflags)
surface_screenshot_la_CFLAGS = $(AM_CFLAGS) $(COMPOSITOR_CFLAGS)
surface_screenshot_la_SOURCES = tests/surface-screenshot-test.c


#
# Documentation
#

man_MANS = weston.1 weston.ini.5 weston-debug.1

if ENABLE_DRM_COMPOSITOR
man_MANS += weston-drm.7
endif

if ENABLE_RDP_COMPOSITOR
man_MANS += weston-rdp.7
endif

MAN_SUBSTS =								\
	-e 's|@weston_native_backend@|$(WESTON_NATIVE_BACKEND)|g'	\
	-e 's|@weston_modules_dir@|$(moduledir)|g'			\
	-e 's|@libweston_modules_dir@|$(libweston_moduledir)|g'	\
	-e 's|@weston_shell_client@|$(WESTON_SHELL_CLIENT)|g'		\
	-e 's|@weston_libexecdir@|$(libexecdir)|g' 			\
	-e 's|@weston_bindir@|$(bindir)|g' 				\
	-e 's|@xserver_path@|$(XSERVER_PATH)|g'			\
	-e 's|@version@|$(PACKAGE_VERSION)|g'

SUFFIXES = .1 .5 .7 .man

%.1 %.5 %.7 : man/%.man
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@

EXTRA_DIST +=					\
	CONTRIBUTING.md				\
	README.md				\
	doc/wayland-screenshot.jpg		\
	doc/calibration-helper.bash		\
	man/weston.man				\
	man/weston-debug.man			\
	man/weston-drm.man			\
	man/weston-rdp.man			\
	man/weston.ini.man

CLEANFILES += $(man_MANS)

if ENABLE_DEVDOCS
DOXYGEN_INDICES = docs/developer/html/index.html docs/tools/html/index.html

docs/developer/html/index.html: doc/doxygen/tooldev.doxygen | docs/developer
	cd doc/doxygen && $(DOXYGEN) tooldev.doxygen

docs/tools/html/index.html: doc/doxygen/tools.doxygen | docs/tools
	cd doc/doxygen && $(DOXYGEN) tools.doxygen
endif

DOCDIRS =		\
	docs/developer	\
	docs/tools

$(DOCDIRS):
	$(MKDIR_P) $@

.PHONY: doc $(DOXYGEN_INDICES)

doc: $(DOXYGEN_INDICES)

.SECONDEXPANSION:

define protostability
$(if $(findstring unstable,$1),unstable,stable)
endef

define protoname
$(shell echo $1 | $(SED) 's/\([a-z\-]\+\)-[a-z]\+-v[0-9]\+/\1/')
endef

protocol/%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

protocol/%-server-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) server-header < $< > $@

protocol/%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@

protocol/%-protocol.c : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

protocol/%-server-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) server-header < $< > $@

protocol/%-client-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@

EXTRA_DIST +=					\
	clients/meson.build			\
	compositor/meson.build			\
	data/meson.build			\
	desktop-shell/meson.build		\
	fullscreen-shell/meson.build		\
	ivi-shell/meson.build			\
	libweston-desktop/meson.build		\
	libweston/git-version.h.meson		\
	libweston/meson.build			\
	man/meson.build				\
	meson.build				\
	meson_options.txt			\
	protocol/meson.build			\
	remoting/meson.build			\
	shared/meson.build			\
	tests/meson.build			\
	wcap/meson.build			\
	xwayland/meson.build
