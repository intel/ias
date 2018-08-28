/*
 * Copyright © 2012 John Kåre Alsaker
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __GL_RENDERER_H__
#define __GL_RENDERER_H__

#include "config.h"

#include <stdint.h>

#include "compositor.h"

#ifdef ENABLE_EGL

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "weston-egl-ext.h"
#else

typedef int EGLint;
typedef int EGLenum;
typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLConfig;
typedef intptr_t EGLNativeDisplayType;
typedef intptr_t EGLNativeWindowType;
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)

#endif /* ENABLE_EGL */

#define NO_EGL_PLATFORM 0

#define GR_GL_VERSION(major, minor) \
	(((uint32_t)(major) << 16) | (uint32_t)(minor))

#define GR_GL_VERSION_INVALID \
	GR_GL_VERSION(0, 0)

extern struct gl_renderer_interface gl_renderer_interface;

enum gl_renderer_border_side {
	GL_RENDERER_BORDER_TOP = 0,
	GL_RENDERER_BORDER_LEFT = 1,
	GL_RENDERER_BORDER_RIGHT = 2,
	GL_RENDERER_BORDER_BOTTOM = 3,
};

struct gl_renderer_interface {
	const EGLint *opaque_attribs;
	const EGLint *alpha_attribs;

	int (*display_create)(struct weston_compositor *ec,
			      EGLenum platform,
			      void *native_window,
			      const EGLint *platform_attribs,
			      const EGLint *config_attribs,
			      const EGLint *visual_id,
			      const int n_ids);

	EGLDisplay (*display)(struct weston_compositor *ec);

	int (*output_window_create)(struct weston_output *output,
				    EGLNativeWindowType window_for_legacy,
				    void *window_for_platform,
				    const EGLint *config_attribs,
				    const EGLint *visual_id,
				    const int n_ids);

	void (*output_destroy)(struct weston_output *output);

	EGLSurface (*output_surface)(struct weston_output *output);

	/* Sets the output border.
	 *
	 * The side specifies the side for which we are setting the border.
	 * The width and height are the width and height of the border.
	 * The tex_width patemeter specifies the width of the actual
	 * texture; this may be larger than width if the data is not
	 * tightly packed.
	 *
	 * The top and bottom textures will extend over the sides to the
	 * full width of the bordered window.  The right and left edges,
	 * however, will extend only to the top and bottom of the
	 * compositor surface.  This is demonstrated by the picture below:
	 *
	 * +-----------------------+
	 * |          TOP          |
	 * +-+-------------------+-+
	 * | |                   | |
	 * |L|                   |R|
	 * |E|                   |I|
	 * |F|                   |G|
	 * |T|                   |H|
	 * | |                   |T|
	 * | |                   | |
	 * +-+-------------------+-+
	 * |        BOTTOM         |
	 * +-----------------------+
	 */
	void (*output_set_border)(struct weston_output *output,
				  enum gl_renderer_border_side side,
				  int32_t width, int32_t height,
				  int32_t tex_width, unsigned char *data);

	void (*print_egl_error_state)(void);

	/***
	 * IAS specific extensions
	 */

	int (*use_output)(struct weston_output *output);
	void (*swap_output_buffers)(struct weston_output *output);
	int (*get_num_textures)(struct weston_surface *surface);
	GLuint (*get_texture_name)(struct weston_surface *surface, int num);
	int (*get_num_egl_images)(struct weston_surface *surface);
	EGLImageKHR (*get_egl_image_name)(struct weston_surface *surface, int num);
	void (*set_viewport)(int x, int y, int width, int height);
	EGLBoolean (*query_buffer)(struct weston_compositor *ec,
			           struct wl_resource *buffer,
				   EGLint attribute, EGLint *value);

#ifdef USE_VM
	int vm_exec;
	int vm_dbg;
	int vm_unexport_delay;
	int vm_share_only;
	const char* vm_plugin_path;
	const char* vm_plugin_args;
#endif // USE_VM
};

struct gl_shader {
	GLuint program;
	GLuint vertex_shader, fragment_shader;
	GLint proj_uniform;
	GLint tex_uniforms[3];
	GLint alpha_uniform;
	GLint color_uniform;
	const char *vertex_source, *fragment_source;
	const char *binary_name;
};


struct gl_renderer {
	struct weston_renderer base;
	int fragment_shader_debug;
	int fan_debug;
	struct weston_binding *fragment_binding;
	struct weston_binding *fan_binding;

	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLConfig egl_config;

	EGLSurface dummy_surface;

	uint32_t gl_version;

	struct wl_array vertices;
	struct wl_array vtxcnt;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;

#ifdef EGL_EXT_swap_buffers_with_damage
	PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;
#endif

	PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window;

	int has_unpack_subimage;

	PFNEGLBINDWAYLANDDISPLAYWL bind_display;
	PFNEGLUNBINDWAYLANDDISPLAYWL unbind_display;
	PFNEGLQUERYWAYLANDBUFFERWL query_buffer;
	int has_bind_display;

	int has_context_priority;

	int has_egl_image_external;

	int has_egl_buffer_age;

	int has_configless_context;

	int has_surfaceless_context;

	int has_dmabuf_import;
	struct wl_list dmabuf_images;

	int has_gl_texture_rg;

	struct gl_shader texture_shader_rgba;
	struct gl_shader texture_shader_rgbx;
	struct gl_shader texture_shader_egl_external;
	struct gl_shader texture_shader_y_uv;
	struct gl_shader texture_shader_y_u_v;
	struct gl_shader texture_shader_y_xuxv;
	struct gl_shader invert_color_shader;
	struct gl_shader solid_shader;
	struct gl_shader *current_shader;

	struct wl_signal destroy_signal;

	struct wl_listener output_destroy_listener;

	int has_dmabuf_import_modifiers;
	PFNEGLQUERYDMABUFFORMATSEXTPROC query_dmabuf_formats;
	PFNEGLQUERYDMABUFMODIFIERSEXTPROC query_dmabuf_modifiers;

	int has_native_fence_sync;
	PFNEGLCREATESYNCKHRPROC create_sync;
	PFNEGLDESTROYSYNCKHRPROC destroy_sync;
	PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_native_fence_fd;

#ifdef USE_VM
	void *vm_buffer_table;
#endif // USE_VM
};

enum timeline_render_point_type {
	TIMELINE_RENDER_POINT_TYPE_BEGIN,
	TIMELINE_RENDER_POINT_TYPE_END
};

struct timeline_render_point {
	struct wl_list link; /* gl_output_state::timeline_render_point_list */

	enum timeline_render_point_type type;
	int fd;
	struct weston_output *output;
	struct wl_event_source *event_source;
};

enum buffer_type {
	BUFFER_TYPE_NULL,
	BUFFER_TYPE_SOLID, /* internal solid color surfaces without a buffer */
	BUFFER_TYPE_SHM,
	BUFFER_TYPE_EGL
};

struct gl_surface_state {
	GLfloat color[4];
	struct gl_shader *shader;

	GLuint textures[3];
	int num_textures;
	bool needs_full_upload;
	pixman_region32_t texture_damage;

	/* These are only used by SHM surfaces to detect when we need
	 * to do a full upload to specify a new internal texture
	 * format */
	GLenum gl_format[3];
	GLenum gl_pixel_type;

	struct egl_image* images[3];
	GLenum target;
	int num_images;

	struct weston_buffer_reference buffer_ref;
	enum buffer_type buffer_type;
	int pitch; /* in pixels */
	int height; /* in pixels */
	int y_inverted;

	/* Extension needed for SHM YUV texture */
	int offset[3]; /* offset per plane */
	int hsub[3];  /* horizontal subsampling per plane */
	int vsub[3];  /* vertical subsampling per plane */

	struct weston_surface *surface;

	struct wl_listener surface_destroy_listener;
	struct wl_listener renderer_destroy_listener;

	int frame_count;
};

#define BUFFER_DAMAGE_COUNT 2

enum gl_border_status {
	BORDER_STATUS_CLEAN = 0,
	BORDER_TOP_DIRTY = 1 << GL_RENDERER_BORDER_TOP,
	BORDER_LEFT_DIRTY = 1 << GL_RENDERER_BORDER_LEFT,
	BORDER_RIGHT_DIRTY = 1 << GL_RENDERER_BORDER_RIGHT,
	BORDER_BOTTOM_DIRTY = 1 << GL_RENDERER_BORDER_BOTTOM,
	BORDER_ALL_DIRTY = 0xf,
	BORDER_SIZE_CHANGED = 0x10
};

struct gl_border_image {
	GLuint tex;
	int32_t width, height;
	int32_t tex_width;
	void *data;
};

struct gl_output_state {
	EGLSurface egl_surface;
	pixman_region32_t buffer_damage[BUFFER_DAMAGE_COUNT];
	int buffer_damage_index;
	enum gl_border_status border_damage[BUFFER_DAMAGE_COUNT];
	struct gl_border_image borders[4];
	enum gl_border_status border_status;

	struct weston_matrix output_matrix;

	/* struct timeline_render_point::link */
	struct wl_list timeline_render_point_list;

	int alpha_available;
};

void
use_shader(struct gl_renderer *gr, struct gl_shader *shader);

struct gl_surface_state *
get_surface_state(struct weston_surface *surface);
struct gl_renderer *
get_renderer(struct weston_compositor *ec);

#endif
