deps_libweston = [
	dep_wayland_server,
	dep_pixman,
	dep_libm,
	dep_libdl,
	dep_libdrm_headers,
	dep_xkbcommon,
]
srcs_libweston = [
	git_version_h,
	'animation.c',
	'bindings.c',
	'clipboard.c',
	'compositor.c',
	'data-device.c',
	'input.c',
	'linux-dmabuf.c',
	'linux-explicit-synchronization.c',
	'linux-sync-file.c',
	'log.c',
	'noop-renderer.c',
	'pixel-formats.c',
	'pixman-renderer.c',
	'plugin-registry.c',
	'screenshooter.c',
	'timeline.c',
	'touch-calibration.c',
	'weston-debug.c',
	'zoom.c',
	'../shared/matrix.c',
	linux_dmabuf_unstable_v1_protocol_c,
	linux_dmabuf_unstable_v1_server_protocol_h,
	linux_explicit_synchronization_unstable_v1_protocol_c,
	linux_explicit_synchronization_unstable_v1_server_protocol_h,
	input_method_unstable_v1_protocol_c,
	input_method_unstable_v1_server_protocol_h,
	input_timestamps_unstable_v1_protocol_c,
	input_timestamps_unstable_v1_server_protocol_h,
	presentation_time_protocol_c,
	presentation_time_server_protocol_h,
	pointer_constraints_unstable_v1_protocol_c,
	pointer_constraints_unstable_v1_server_protocol_h,
	relative_pointer_unstable_v1_protocol_c,
	relative_pointer_unstable_v1_server_protocol_h,
	weston_screenshooter_protocol_c,
	weston_screenshooter_server_protocol_h,
	text_cursor_position_protocol_c,
	text_cursor_position_server_protocol_h,
	text_input_unstable_v1_protocol_c,
	text_input_unstable_v1_server_protocol_h,
	weston_touch_calibration_protocol_c,
	weston_touch_calibration_server_protocol_h,
	viewporter_protocol_c,
	viewporter_server_protocol_h,
	xdg_output_unstable_v1_protocol_c,
	xdg_output_unstable_v1_server_protocol_h,
	weston_debug_protocol_c,
	weston_debug_server_protocol_h,
]

install_headers(
	'../shared/platform.h',
	'../shared/weston-egl-ext.h',
	subdir: dir_include_libweston_install
)

if get_option('renderer-gl')
	dep_egl = dependency('egl', required: false)
	if not dep_egl.found()
		error('libweston + gl-renderer requires egl which was not found. Or, you can use \'-Drenderer-gl=false\'.')
	endif
	deps_libweston += dep_egl
endif

lib_weston = shared_library(
	'ias-@0@'.format(libweston_major),
	srcs_libweston,
	include_directories: [ include_directories('..', '../shared'), public_inc ],
	install: true,
	version: '0.0.@0@'.format(libweston_revision),
	link_whole: lib_libshared,
	dependencies: deps_libweston
)

dep_libweston = declare_dependency(
	link_with: lib_weston,
	include_directories: [ include_directories('.'), public_inc ],
	dependencies: deps_libweston
)

pkgconfig.generate(
	lib_weston,
	filebase: 'libias-@0@'.format(libweston_major),
	name: 'libias API',
	version: version_weston,
	description: 'Header files for libias compositors development',
	requires_private: [ dep_wayland_server, dep_pixman, dep_xkbcommon ],
	subdirs: dir_include_libweston
)

pkgconfig.generate(
	filebase: 'libias-@0@-protocols'.format(libweston_major),
	name: 'libWeston Protocols',
	version: version_weston,
	description: 'libWeston protocol files',
	variables: [
		'datarootdir=' + join_paths('${prefix}', get_option('datadir')),
		'pkgdatadir=' + join_paths('${pc_sysrootdir}${datarootdir}', dir_protocol_libweston)
	],
	install_dir: dir_data_pc
)

srcs_session_helper = [
	'launcher-direct.c',
	'launcher-util.c',
	'launcher-weston-launch.c',
]
deps_session_helper = [ dep_libweston_h ]

if get_option('backend-drm')
	deps_session_helper += dep_libdrm
endif

if get_option('backend-ias')
	deps_session_helper += dep_libdrm
endif

systemd_dep = dependency('', required: false)
if get_option('launcher-logind')
	systemd_dep = dependency('libsystemd', version: '>= 209', required: false)
	if systemd_dep.found()
		config_h.set('HAVE_SYSTEMD_LOGIN_209', '1')
	else
		systemd_dep = dependency('libsystemd-login', version: '>= 198', required: false)
		if not systemd_dep.found()
			error('logind support requires libsystemd or libsystemd-login but neither was found. Or, you can use \'-Dlauncher-logind=false\'')
		endif
	endif

	dbus_dep = dependency('dbus-1', version: '>= 1.6', required: false)
	if not dbus_dep.found()
		error('logind support requires dbus-1 >= 1.6 which was not found. Or, you can use \'-Dlauncher-logind=false\'')
	endif

	config_h.set('HAVE_DBUS', '1')
	config_h.set('HAVE_SYSTEMD_LOGIN', '1')

	srcs_session_helper += [
		'dbus.c',
		'launcher-logind.c',
	]
	deps_session_helper += [
		dbus_dep,
		systemd_dep,
	]
endif

lib_session_helper = static_library(
	'session-helper',
	srcs_session_helper,
	include_directories: include_directories('..', '../shared'),
	dependencies: deps_session_helper,
	install: false
)
dep_session_helper = declare_dependency(link_with: lib_session_helper)


lib_libinput_backend = static_library(
	'libinput-backend',
	[
		'libinput-device.c',
		'libinput-seat.c'
	],
	dependencies: [
		dep_libweston,
		dep_libinput,
		dependency('libudev', version: '>= 136')
	],
	include_directories: include_directories('..'),
	install: false
)
dep_libinput_backend = declare_dependency(
	link_with: lib_libinput_backend,
	include_directories: include_directories('.')
)

dep_vertex_clipping = declare_dependency(
	sources: 'vertex-clipping.c',
	include_directories: include_directories('.')
)

if get_option('weston-launch')
	dep_pam = cc.find_library('pam')

	if not cc.has_function('pam_open_session', dependencies: dep_pam)
		error('pam_open_session not found for weston-launch')
	endif

	executable(
		'ias-weston-launch',
		'weston-launch.c',
		dependencies: [dep_pam, systemd_dep, dep_libdrm],
		include_directories: include_directories('..'),
		install: true
	)

	meson.add_install_script('echo', 'REMINDER: You are installing ias-weston-launch, please make it setuid-root.')
endif

if get_option('enable-shadergen')
	config_h.set('GENERATE_BINARY_SHADERS', '1')
else
	config_h.set('GENERATE_BINARY_SHADERS', '0')
endif

if not get_option('disable-xkbcommon')
	config_h.set('ENABLE_XKBCOMMON', '1')
endif

if get_option('enable-remote-display')
	config_h.set('BUILD_REMOTE_DISPLAY', '1')
endif

if get_option('profile-remote-display')
	if not get_option('enable-remote-display')
		error('''profile-remote-display requires enable-remote-display which
		 was not found. Or, you can use \'-Dprofile-remote-display=false\'.''')
	endif

	config_h.set('PROFILE_REMOTE_DISPLAY', '1')
endif

if get_option('enable-tracing')
	config_h.set('ENABLE_TRACING', '1')
	config_h.set('TRACE_BUFFER_SIZE', '100')
else
	config_h.set('ENABLE_TRACING', '0')
	config_h.set('TRACE_BUFFER_SIZE', '1')
endif

subdir('renderer-gl')
subdir('backend-ias')
subdir('backend-drm')
subdir('backend-fbdev')
subdir('backend-headless')
subdir('backend-rdp')
subdir('backend-wayland')
subdir('backend-x11')
subdir('plugin-framework')
