srcs_wl_disp = [
	'display_wayland.cpp',
	'display_wayland.h',
	ias_shell_client_protocol_h,
	ias_shell_protocol_c,
	xdg_shell_client_protocol_h,
	xdg_shell_protocol_c,
]

dep_wayland_egl = dependency('wayland-egl', required: false)

deps_wl_disp = [
	dep_libdrm,
	dep_wl_base,
	dep_wayland_egl,
]

plugin_wl_disp = shared_library(
	'wl_disp',
	srcs_wl_disp,
	include_directories: include_directories('../..', '../wl_base'),
	dependencies: deps_wl_disp,
	name_prefix: 'lib',
	cpp_args: [ '-fPIC',
	'-DWL_EGL_PLATFORM'
	],
	install: true,
	install_dir: dir_module_weston
)
env_modmap += 'wl_disp.so=@0@;'.format(plugin_wl_disp.full_path())

dep_wl_disp = declare_dependency(link_with: plugin_wl_disp)
