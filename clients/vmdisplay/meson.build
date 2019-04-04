if get_option('enable-hyper-dmabuf')

	srcs_vmdisplaylib = [
		'vmdisplay.c',
		'vmdisplay-parser.c',
		'../cmn/wayland-drm-protocol.c',
		linux_dmabuf_unstable_v1_client_protocol_h,
		linux_dmabuf_unstable_v1_protocol_c,
	]
	
	deps_vmdisplaylib = [
		dep_wayland_client,
		dep_libdrm,
	]

	lib_vmdisplaylib = static_library(
		'vmdisplay',
		srcs_vmdisplaylib,
		include_directories: include_directories('../..', '../../shared', '../../libweston/renderer-gl', '../cmn'),
		dependencies: deps_vmdisplaylib,
		install: false,
	)

	dep_vmdisplaylib = declare_dependency(
		link_with: lib_vmdisplaylib,
		dependencies: deps_vmdisplaylib
	)

	thread_dep = dependency('threads')

	dep_vmdisplay_wayland = [
		dep_vmdisplaylib,
		dep_wayland_client,
		dep_libshared,
		thread_dep,
	]

	foreach depname : [ 'egl', 'wayland-egl', 'glesv2', 'wayland-cursor' ]
		dep = dependency(depname, required: false)
		if not dep.found()
			error('@0@ requires @1@ which was not found.')
		endif
		dep_vmdisplay_wayland += dep
	endforeach


	executable(
		'vmdisplay-wayland',
		'vmdisplay-wayland.c',
		ias_shell_client_protocol_h,
		ias_shell_protocol_c,
		include_directories: include_directories('../..', '../../libweston/renderer-gl', '../cmn'),
		dependencies: dep_vmdisplay_wayland,
		link_args: '-lm',
		install: true,
		install_dir: join_paths(dir_data, 'ias/examples')
	)

	executable(
		'vmdisplay-server',
		'vmdisplay-server.cpp',
		'vmdisplay-server-network.cpp',
		'vmdisplay-server-hyperdmabuf.cpp',
		include_directories: include_directories('../..', '../../libweston/renderer-gl'),
		dependencies: [
			dep_wayland_client,
			dep_libshared,
			thread_dep,
		],
		install: true,
		install_dir: join_paths(dir_data, 'ias/examples')
	)

	executable(
		'vmdisplay-input',
		'vmdisplay-input.cpp',
		'vmdisplay-server-network.cpp',
		include_directories: include_directories('../..', '../../libweston/renderer-gl'),
		dependencies: [
			dep_wayland_client,
			dep_libshared,
			thread_dep,
		],
		install: true,
		install_dir: join_paths(dir_data, 'ias/examples')
	)

endif
