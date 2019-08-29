if get_option('ias-plugin-framework')

	srcs_ias_plugin_framework = [
		'ias-plugin-framework.c',
		'ias-spug.c',
		ias_layout_manager_protocol_c,
		ias_layout_manager_server_protocol_h,
		ias_input_manager_protocol_c,
		ias_input_manager_server_protocol_h,
	]

	deps_ias_plugin_framework = [
		dep_glib,
		dep_libshared,
		dep_libweston,
		dep_ias_common,
	]

	foreach name : [ 'egl', 'glesv2' ]
		d = dependency(name, required: true)
		if not d.found()
			error('ias-plugin-framework requires @0@ which was not found.'.format(name))
		endif
		deps_ias_plugin_framework += d
	endforeach

	plugin_ias_plugin_framework = shared_library(
		'ias_plugin_framework',
		srcs_ias_plugin_framework,
		include_directories: include_directories('../..', '../../shared', '../../ias-shell', '../../include/libias'),
		dependencies: deps_ias_plugin_framework,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'ias-plugin-framework.so=@0@;'.format(plugin_ias_plugin_framework.full_path())
endif

if get_option('ivi-plugin-framework')

	srcs_ivi_plugin_framework = [
		'ivi-plugin-framework.c',
		'ias-spug.c',
		ias_layout_manager_protocol_c,
		ias_layout_manager_server_protocol_h,
		ias_input_manager_protocol_c,
		ias_input_manager_server_protocol_h,
	]

	deps_ivi_plugin_framework = [
		dep_glib,
		dep_libshared,
		dep_libweston,
		dep_ias_common,
	]

	foreach name : [ 'egl', 'glesv2' ]
		d = dependency(name, required: true)
		if not d.found()
			error('ivi-plugin-framework requires @0@ which was not found.'.format(name))
		endif
		deps_ivi_plugin_framework += d
	endforeach

	inc_ivi_plugin_framework = [ '../..', 
		'../../shared',
		'../../ivi-shell',
		'../../ias-shell',
		 '../../include/libias',
	]

	plugin_ivi_plugin_framework = shared_library(
		'ivi_plugin_framework',
		srcs_ivi_plugin_framework,
		include_directories: include_directories(inc_ivi_plugin_framework),
		dependencies: deps_ivi_plugin_framework,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'ivi-plugin-framework.so=@0@;'.format(plugin_ivi_plugin_framework.full_path())
endif

install_headers(
	backend_ias_h,
	subdir: dir_include_libweston_install
)
