if get_option('ias-plugin-framework')
	
	srcs_input = [
		'input.c',
	]

	deps_input = [
		dep_libweston,
	]

	plugin_input = shared_library(
		'input',
		srcs_input,
		include_directories: include_directories('..', '../../shared', '../../include/libias'),
		dependencies: deps_input,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'input.so=@0@;'.format(plugin_input.full_path())
endif