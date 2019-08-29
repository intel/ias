srcs_ias_common = [
	'ias-common.c',
	'ias-config.c',
]

dep_libias_common = [
	dep_libweston_h,
	dep_libdrm,
	dependency('libudev'),
]

lib_ias_common = static_library(
	'ias-common',
	srcs_ias_common,
	include_directories: include_directories('..', '../shared', '../include', '../include/libias', '../libweston'),
	dependencies: dep_libias_common,
	install: false
)

dep_ias_common = declare_dependency(
	link_with: lib_ias_common,
	include_directories: include_directories('.')
)


install_headers(
	'ias-common.h',
	subdir: dir_include_libweston_install
)
