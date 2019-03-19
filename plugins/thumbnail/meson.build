if get_option('ias-plugin-framework')
	
	srcs_thumbnail = [
		'thumbnail_layout.c',
	]

	deps_thumbnail = [
		dep_libweston,
	]

	plugin_thumbnail = shared_library(
		'thumbnail',
		srcs_thumbnail,
		include_directories: include_directories('..', '../../shared'),
		dependencies: deps_thumbnail,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'thumbnail.so=@0@;'.format(plugin_thumbnail.full_path())
endif
