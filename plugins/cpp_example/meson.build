if get_option('ias-plugin-framework')
	
	r = run_command('cp', '../grid_layout.c', 'grid_layout.cc')
	r = run_command('patch', '\-p0', 'grid_layout.cc', 'cpp_example.diff')
	r = run_command('rm', '\-f', 'grid_layout.cc.orig')
	srcs_cpp_example = [
		'grid_layout.cc',
	]

	deps_cpp_example = [
		dep_libweston,
	]

	plugin_cpp_example = shared_library(
		'cpp_example',
		srcs_cpp_example,
		include_directories: include_directories('..', '../../shared'),
		dependencies: deps_cpp_example,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'cpp_example.so=@0@;'.format(plugin_cpp_example.full_path())
endif
