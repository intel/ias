if get_option('shell-ias')

	srcs_shell_ias = [
		'ias-shell.c',
		'ias-shell.h',
		'ias-relay-input.h',
		'ias-relay-input.c',
		'ias-hmi.c',
		'ias-hmi.h',
		'ias-shell-config.c',
		ias_shell_server_protocol_h,
		ias_shell_protocol_c,
	]

	deps_shell_ias = [
		dep_libshared,
		dep_libweston,
		dep_ias_common,
	]

	plugin_shell_ias = shared_library(
		'ias-shell',
		srcs_shell_ias,
		include_directories: include_directories('..', '../shared', '../include/libias'),
		dependencies: deps_shell_ias,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'ias-shell.so=@0@;'.format(plugin_shell_ias.full_path())

	srcs_ias_shell_protocol = [
		ias_shell_client_protocol_h,
		ias_shell_protocol_c,
	]

	plugin_ias_shell_protocol = shared_library(
		'ias-shell-protocol',
		srcs_ias_shell_protocol,
		include_directories: include_directories('..', '../shared'),
		dependencies: deps_shell_ias,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'ias-shell-protocol.so=@0@;'.format(plugin_ias_shell_protocol.full_path())
	
endif
