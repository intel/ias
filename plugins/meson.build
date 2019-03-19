if get_option('ias-plugin-framework')
	
	srcs_grid_layout = [
		'grid_layout.c',
		'cursor_image.h',
	]

	deps_grid_layout = [
		dep_libweston,
	]

	plugin_grid_layout = shared_library(
		'grid-layout',
		srcs_grid_layout,
		include_directories: include_directories('..', '../shared'),
		dependencies: deps_grid_layout,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'grid_layout.so=@0@;'.format(plugin_grid_layout.full_path())

	subdir('cpp_example')
	subdir('extension_sample')
	subdir('gamma_example')
	subdir('input')
	subdir('sprite_example')
	subdir('thumbnail')
endif
