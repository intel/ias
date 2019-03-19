if get_option('enable-remote-display')	

	depnames = [
		'gstreamer-1.0',
		'gstreamer-app-1.0'
	]
	
	deps_transport = [ dep_libshared, dep_libdrm ]
	deps_transport_udp = deps_transport

	foreach depname : depnames
		dep = dependency(depname, required: false)
		if not dep.found()
			error('Remoting plugin requires @0@ which was not found. '.format(depname) + user_hint)
		endif
		deps_transport_udp += dep
	endforeach
	
	plugin_transport_udp = shared_library(
		'transport_plugin_udp',
		'transport_plugin_udp.c',
		include_directories: include_directories('../..', '../../shared'),
		dependencies: deps_transport_udp,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)

	plugin_transport_avb = shared_library(
		'transport_plugin_avb',
		'transport_plugin_avb.c',
		include_directories: include_directories('../..', '../../shared'),
		dependencies: deps_transport_udp,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)

	plugin_transport_tcp = shared_library(
		'transport_plugin_tcp',
		'transport_plugin_tcp.c',
		include_directories: include_directories('../..', '../../shared'),
		dependencies: deps_transport,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)

	plugin_transport_file = shared_library(
		'transport_plugin_file',
		'transport_plugin_file.c',
		include_directories: include_directories('../..', '../../shared'),
		dependencies: deps_transport,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)

	thread_dep = dependency('threads')

	foreach name : [ 'libva', 'libva-drm' ]
		d = dependency(name, version: '>= 0.34.0', required: false)
		if not d.found()
			error('remote-display requires @0@ >= 0.34.0 which was not found. Or, you can use \'-Denable-remote-display=false\'.'.format(name))
		endif
		deps_drm += d
	endforeach
	
	dep = dependency('libdrm_intel', required: false)
	if dep.found()
		deps_drm += dep
	elif required
		error('remote-display is configured to use @0@ but it was not found. Or, you can use \'-Denable-remote-display=false\' list.'.format('libdrm_' + driver))
	endif	

	executable(
		'remote-display',
		'main.c',
		'main.h',
		'encoder.c',
		'encoder.h',
		'input_receiver.c',
		'input_receiver.h',
		'input_sender.h',
		ias_shell_client_protocol_h,
		ias_shell_protocol_c,
		include_directories: include_directories('../..', '../../shared'),
		dependencies: [
			dep_libshared,
			dep_libdrm,
			thread_dep,
			deps_drm,
		],
		install: true,
		install_dir: join_paths(dir_data, 'ias/examples')
	)

	dep_remote_display_show = [
			dep_libshared,
			dep_libdrm,
			thread_dep,
			deps_drm,
	]
	
	foreach depname : [ 'egl', 'wayland-egl', 'glesv2', 'wayland-cursor' ]
		dep = dependency(depname, required: false)
		if not dep.found()
			error('@0@ requires @1@ which was not found.')
		endif
		dep_remote_display_show += dep
	endforeach
		
	executable(
		'remote-display-show',
		'remote-display-show.c',
		ias_shell_client_protocol_h,
		ias_shell_protocol_c,
		ivi_application_client_protocol_h,
		ivi_application_protocol_c,
		include_directories: include_directories('../..', '../../shared'),
		dependencies: dep_remote_display_show,
		install: true,
		install_dir: join_paths(dir_data, 'ias/examples')
	)
	

endif

