if get_option('ias-plugin-framework')
	
	r = run_command('cp', '../grid_layout.c', 'extension_sample.c')
	r = run_command('patch', '\-p0', 'extension_sample.c', 'extension_sample.diff')
	r = run_command('rm', '\-f', 'extension_sample.c.orig')

	dep_scanner = dependency('wayland-scanner', native: true)
	prog_scanner = find_program(dep_scanner.get_pkgconfig_variable('wayland_scanner'))
	
	generated_protocols = [
		[ 'new-extension', 'internal' ],
	]
	
	foreach proto: generated_protocols
		proto_name = proto[0]
		if proto[1] == 'internal'
			base_file = proto_name
			xml_path = '@0@.xml'.format(proto_name)
		elif proto[1] == 'stable'
			base_file = proto_name
			xml_path = '@0@/stable/@1@/@1@.xml'.format(dir_wp_base, base_file)
		else
			base_file = '@0@-unstable-@1@'.format(proto_name, proto[1])
			xml_path = '@0@/unstable/@1@/@2@.xml'.format(dir_wp_base, proto_name, base_file)
		endif
	
		foreach output_type: [ 'client-header', 'server-header', 'private-code' ]
			if output_type == 'client-header'
				output_file = '@0@-client-protocol.h'.format(base_file)
			elif output_type == 'server-header'
				output_file = '@0@-server-protocol.h'.format(base_file)
			else
				output_file = '@0@-protocol.c'.format(base_file)
				if dep_scanner.version().version_compare('< 1.14.91')
					output_type = 'code'
				endif
			endif
	
			var_name = output_file.underscorify()
			target = custom_target(
				'@0@ @1@'.format(base_file, output_type),
				command: [ prog_scanner, output_type, '@INPUT@', '@OUTPUT@' ],
				input: xml_path,
				output: output_file,
			)
	
			set_variable(var_name, target)
		endforeach
	endforeach

	srcs_extension_sample = [
		'extension_sample.c',
		new_extension_server_protocol_h,
		new_extension_protocol_c,
	]

	deps_extension_sample = [
		dep_libshared,
		dep_libweston,
	]

	plugin_extension_sample = shared_library(
		'extension-sample',
		srcs_extension_sample,
		include_directories: include_directories('..', '../../shared'),
		dependencies: deps_extension_sample,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'extension-sample.so=@0@;'.format(plugin_extension_sample.full_path())

	executable(
		'extension-test-client', 
		[
			'extension_test_client.c',
			new_extension_client_protocol_h,
			new_extension_protocol_c,
		],
		include_directories: include_directories('..'),
		dependencies: [
			dep_libshared,
			dep_wayland_client,
		],
		install: true,
		install_dir: join_paths(dir_data, 'ias/examples')
	)
endif
