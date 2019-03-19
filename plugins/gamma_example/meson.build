if get_option('ias-plugin-framework')
	
	srcs_gamma_example = [
		'surface_gbc_control.c',
	]

	deps_gamma_example = [
		dep_libweston,
	]

	plugin_gamma_example = shared_library(
		'gamma_example',
		srcs_gamma_example,
		include_directories: include_directories('..', '../../shared'),
		dependencies: deps_gamma_example,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'gamma_example.so=@0@;'.format(plugin_gamma_example.full_path())
endif
