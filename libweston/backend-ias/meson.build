if get_option('backend-ias')

	config_h.set('BUILD_IAS_COMPOSITOR', '1')

	srcs_ias = [
		'ias.c',
		backend_ias_h,
		'ias-sprite.c',
		'classic.c',
		'flexible.c',
		linux_dmabuf_unstable_v1_protocol_c,
		linux_dmabuf_unstable_v1_server_protocol_h,
		presentation_time_server_protocol_h,
		ias_backend_server_protocol_h,
		ias_backend_protocol_c,
		ias_shell_server_protocol_h,
		ias_shell_protocol_c,
		xdg_shell_client_protocol_h,
		xdg_shell_protocol_c,
	]

	deps_ias = [
		dep_libweston,
		dep_session_helper,
		dep_libdrm,
		dep_libinput_backend,
		dep_libshared,
		dep_ias_common,
		dependency('libudev', version: '>= 136'),
	]

	if get_option('enable-remote-display')
		srcs_ias += [
			'capture-proxy.c',
			'capture-proxy.h',
		]

		foreach name : [ 'libva', 'libva-drm' ]
			d = dependency(name, version: '>= 0.34.0', required: false)
			if not d.found()
				error('remote-display requires @0@ >= 0.34.0 which was not found. Or, you can use \'-Denable-remote-display=false\'.'.format(name))
			endif
			deps_ias += d
		endforeach		
	endif

	# XXX: Actually let IAS-backend build without GBM, it really should
	if get_option('renderer-gl')
		dep_gbm = dependency('gbm', required: false)
		if not dep_gbm.found()
			error('ias-backend + gl-renderer requires gbm which was not found. Or, you can use \'-Dbackend-ias=false\' or \'-Drenderer-gl=false\'.')
		endif
		if dep_gbm.version().version_compare('>= 17.1')
			config_h.set('HAVE_GBM_MODIFIERS', '1')
		endif
		if dep_gbm.version().version_compare('>= 17.2')
			config_h.set('HAVE_GBM_FD_IMPORT', '1')
		endif
		deps_ias += dep_gbm
	endif

	if dep_libdrm.version().version_compare('>= 2.4.71')
		config_h.set('HAVE_DRM_ADDFB2_MODIFIERS', '1')
	endif

	if dep_libdrm.version().version_compare('>= 2.4.78')
		config_h.set('HAVE_DRM_ATOMIC', '1')
	endif

	if dep_libdrm.version().version_compare('>= 2.4.83')
		config_h.set('HAVE_DRM_FORMATS_BLOB', '1')
	endif

	plugin_ias = shared_library(
		'ias-backend',
		srcs_ias,
		include_directories: include_directories('../..', '../../shared', '../../include/libias'),
		dependencies: deps_ias,
		name_prefix: '',
		install: true,
		install_dir: dir_module_libweston
	)
	env_modmap += 'ias-backend.so=@0@;'.format(plugin_ias.full_path())

endif

install_headers(
	backend_ias_h,
	subdir: dir_include_libweston_install
)
