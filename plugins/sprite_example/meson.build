if get_option('ias-plugin-framework')
	
	srcs_sprite_example = [
		'sprite_example.c',
	]

	deps_sprite_example = [
		dep_libweston,
	]

	plugin_sprite_example = shared_library(
		'sprite_example',
		srcs_sprite_example,
		include_directories: include_directories('..', '../../shared', '../../include/libias'),
		dependencies: deps_sprite_example,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'sprite_example.so=@0@;'.format(plugin_sprite_example.full_path())
endif