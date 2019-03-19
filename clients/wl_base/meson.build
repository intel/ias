srcs_wl_base = [
	'global_wayland.cpp',
	'global_wayland.h',
	ias_backend_client_protocol_h,
	ias_backend_protocol_c,
	ias_shell_client_protocol_h,
	ias_shell_protocol_c,
	xdg_shell_client_protocol_h,
	xdg_shell_protocol_c,
]

deps_wl_base = [
	dep_wayland_client,
	dep_libshared,
	dep_libdrm,
]

plugin_wl_base = shared_library(
	'wl_base',
	srcs_wl_base,
	include_directories: include_directories('../..', '../../shared'),
	dependencies: deps_wl_base,
	name_prefix: 'lib',
	cpp_args: [ '-fPIC' ],
	install: true,
	install_dir: dir_module_weston
)
env_modmap += 'wl_base.so=@0@;'.format(plugin_wl_base.full_path())

dep_wl_base = declare_dependency(link_with: plugin_wl_base)

