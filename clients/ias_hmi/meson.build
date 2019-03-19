srcs_ias_hmi = [
	'ias_hmi_reg.cpp',
	'ias_hmi_reg.h',
	ias_shell_client_protocol_h,
	ias_shell_protocol_c,
]

deps_ias_hmi = [
	dep_libshared,
	dep_wl_disp,
	dep_libdrm,
]

plugin_ias_hmi = shared_library(
	'ias_hmi',
	srcs_ias_hmi,
	include_directories: include_directories('../..', 
		'../wl_base', '../wl_disp', '../../shared'),
	dependencies: deps_ias_hmi,
	name_prefix: 'lib',
	cpp_args: [ '-fPIC' ],
	install: true,
	install_dir: dir_module_weston
)
env_modmap += 'ias_hmi.so=@0@;'.format(plugin_ias_hmi.full_path())
	
