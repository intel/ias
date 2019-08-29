dep_libmediactl = cc.find_library('libmediactl',required : true, dirs: meson.current_source_dir())
dep_libv4l2subdev = cc.find_library('libv4l2subdev',required : true, dirs: meson.current_source_dir())

dep_dma_test = [
	dep_libdrm,
	dep_libmediactl,
	dep_libv4l2subdev,
	dep_wayland_client,
	dependency('threads'),
	dependency('libudev', version: '>= 136'),
]

foreach depname : [ 'egl', 'wayland-egl', 'glesv2', 'libdrm_intel' ]
	dep = dependency(depname, required: false)
	if not dep.found()
		error('@0@ requires @1@ which was not found.')
	endif
	dep_dma_test += dep
endforeach

executable(
	'dma_test',
	'dma_test.c',
	'mediactl.h',
	'v4l2subdev.h',
	ias_shell_protocol_c,
	ias_shell_client_protocol_h,
	ivi_application_protocol_c,	
	ivi_application_client_protocol_h,
	'../cmn/wayland-drm-protocol.c',
	include_directories: include_directories('.','../..','../wl_base','../wl_disp','../../shared', '../cmn'),
	dependencies: dep_dma_test,
	install: true,
	install_dir: join_paths(dir_data, 'ias/examples')
)
