/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-wayland.c
 *-----------------------------------------------------------------------------
 * Copyright 2012-2018 Intel Corporation
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
 *-----------------------------------------------------------------------------
 * Description:
 *   VMDisplay wayland client
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

#include <linux/input.h>
#include <drm/drm_fourcc.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

#include "vmdisplay.h"

#include "vmdisplay-parser.h"
#include "wayland-drm-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

#include "ias-shell-client-protocol.h"

struct drm_i915_gem_gvtbuffer *gvt_buffer = NULL;
uint32_t gvt_prime = 0;

GLuint program;

struct window;
struct seat;

int g_Dbg = 0;

PFNEGLCREATEIMAGEKHRPROC create_image = NULL;
PFNEGLDESTROYIMAGEKHRPROC destroy_image = NULL;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d = NULL;

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_seat *seat;
	struct wl_drm *wl_drm;
	struct zwp_linux_dmabuf_v1 *wl_dmabuf;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;
	struct wl_shm *shm;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *default_cursor;
	struct wl_surface *cursor_surface;
	struct {
		EGLConfig conf;
	} egl;
	struct window *window;
	struct ias_hmi *hmi;
};

struct wl_drm *wl_drm;
struct zwp_linux_dmabuf_v1 *wl_dmabuf;
struct wl_buffer *buf;

struct geometry {
	uint32_t width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;
	struct {
		GLuint pos;
		GLuint col;
		GLuint rotation_uniform;
		GLuint texture[2];
		GLuint color_format;
		GLuint texture_size;
	} gl;

	struct wl_egl_window *native;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	EGLSurface egl_surface;
	struct wl_callback *callback;
	int fullscreen, configured, opaque;
	int positioned;
	int x, y;
	char name[128];
};

extern struct egl_manager g_eman_common;

static const char *vert_shader_text =
    "uniform mat4 rotation;                  \n"
    "attribute vec4 a_position;              \n"
    "attribute vec2 a_texCoord;              \n"
    "varying vec2 v_texCoord;                \n"
    "void main()                             \n"
    "{                                       \n"
    "   gl_Position = rotation * a_position; \n"
    "   v_texCoord = a_texCoord;             \n"
    "}                                       \n";

static const char *frag_shader_text =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform vec2 s_texSize;                             \n"
    "uniform sampler2D s_baseMap0;                       \n"
    "uniform sampler2D s_baseMap1;                       \n"
    "uniform int s_colorFormat;                          \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  if (s_colorFormat == 0) {                         \n"
    "    gl_FragColor = texture2D( s_baseMap0, v_texCoord );   \n"
    "  } else {                                          \n"
    "    float y,u,v;                                    \n"
    "    if (s_colorFormat == 2) {                       \n"
    "      y = 1.16438356 * (texture2D(s_baseMap0, v_texCoord).x - 0.0625);\n"
    "      u = texture2D(s_baseMap1, v_texCoord).r - 0.5;\n"
    "      v = texture2D(s_baseMap1, v_texCoord).g - 0.5;\n"
    "    } else {                                         \n"
    "      vec4 raw = texture2D(s_baseMap0, v_texCoord);  \n"
    "      if (fract(v_texCoord.x * s_texSize.x) < 0.5)   \n"
    "        raw.b = raw.r;                               \n"
    "      u = raw.g-0.5;                                 \n"
    "      v = raw.a-0.5;                                 \n"
    "      y = 1.1643*(raw.b-0.0625);                     \n"
    "    }                                                \n"
    "    gl_FragColor.r = y + 1.59602678 * v;             \n"
    "    gl_FragColor.g = y - 0.39176229 * u - 0.81296764 * v;\n"
    "    gl_FragColor.b = y + 2.01723214 * u;             \n"
    "    gl_FragColor.a = 1.0;                            \n"
    "  }                                                  \n"
    "}                                                    \n";

static const char *frag_shader_text_bgra =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D s_baseMap;                        \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = texture2D( s_baseMap, v_texCoord ).bgra;   \n"
    "}                                                    \n";

static int running = 1;
int use_egl = 1;
int use_event_poll = 0;
int using_mesa = 0;

extern GLuint current_textureId[2];
extern uint32_t current_texture_sampler_format;
extern struct wl_buffer *current_buffer;
extern unsigned int pipe_id;
int surf_index = 0;
uint64_t surf_id = 0;
int enable_fps_info = 0;
int enable_fixed_size = 0;
extern vmdisplay_socket vmsocket;

/* Function Prototypes */
void ias_hmi_surface_destroyed(void *data,
			       struct ias_hmi *ias_hmi,
			       uint32_t id,
			       const char *title,
			       uint32_t pid, const char *pname);
void ias_hmi_surface_sharing_info(void *data,
				  struct ias_hmi *hmi,
				  uint32_t id,
				  const char *name,
				  uint32_t shareable,
				  uint32_t pid, const char *pname);

static void init_egl(struct display *display, int opaque)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint major, minor, n;
	EGLBoolean ret;

	if (opaque)
		config_attribs[9] = 0;

	g_eman_common.dpy = eglGetDisplay(display->display);
	assert(g_eman_common.dpy);

	ret = eglInitialize(g_eman_common.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	ret =
	    eglChooseConfig(g_eman_common.dpy, config_attribs,
			    &display->egl.conf, 1, &n);
	assert(ret && n == 1);

	g_eman_common.ctx = eglCreateContext(g_eman_common.dpy,
					     display->egl.conf, EGL_NO_CONTEXT,
					     context_attribs);
	assert(g_eman_common.ctx);

}

static void fini_egl(struct display *display)
{
	/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
	 * on eglReleaseThread(). */
	eglMakeCurrent(g_eman_common.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);

	eglTerminate(g_eman_common.dpy);
	eglReleaseThread();
}

static GLuint create_shader(struct window *window, const char *source,
			    GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	assert(shader != 0);

	glShaderSource(shader, 1, (const char **)&source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		exit(1);
	}

	return shader;
}

static void init_gl(struct window *window, uint32_t drm_format)
{
	GLuint frag, vert;
	GLint status;

	switch (drm_format) {
	case DRM_FORMAT_XBGR8888:
		frag =
		    create_shader(window, frag_shader_text_bgra,
				  GL_FRAGMENT_SHADER);
		break;
	default:
		frag =
		    create_shader(window, frag_shader_text, GL_FRAGMENT_SHADER);
		break;
	}

	vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);

	const GLubyte *version = glGetString(GL_VERSION);
	if (version == NULL || strstr((const char *)version, "Mesa") == NULL) {
		printf("VMdisplay requires Mesa driver. Some things (e.g. "
		       "tiling) may be broken.\n");
		using_mesa = 0;
	} else {
		using_mesa = 1;
	}

	program = glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		exit(1);
	}

	glUseProgram(program);

	window->gl.pos = 0;
	window->gl.col = 1;

	glBindAttribLocation(program, window->gl.pos, "a_position");
	glBindAttribLocation(program, window->gl.col, "a_texCoord");
	glLinkProgram(program);

	if (g_Dbg) {
		printf("GL_RENDERER   = %s\n",
		       (char *)glGetString(GL_RENDERER));
		printf("GL_VERSION    = %s\n", (char *)glGetString(GL_VERSION));
		printf("GL_VENDOR     = %s\n", (char *)glGetString(GL_VENDOR));
		printf("GL_EXTENSIONS = %s\n",
		       (char *)glGetString(GL_EXTENSIONS));
		printf("EGL_VENDOR    = %s\n",
		       (char *)eglQueryString(g_eman_common.dpy, EGL_VENDOR));
		printf("EGL_VERSION   = %s\n",
		       (char *)eglQueryString(g_eman_common.dpy, EGL_VERSION));
		printf("EGL_EXTENSIONS= %s\n",
		       (char *)eglQueryString(g_eman_common.dpy,
					      EGL_EXTENSIONS));
	}

	window->gl.texture[0] = glGetUniformLocation(program, "s_baseMap0");
	window->gl.texture[1] = glGetUniformLocation(program, "s_baseMap1");
	window->gl.color_format =
	    glGetUniformLocation(program, "s_colorFormat");
	window->gl.texture_size = glGetUniformLocation(program, "s_texSize");
	window->gl.rotation_uniform = glGetUniformLocation(program, "rotation");
}

static void handle_ping(void *data, struct wl_shell_surface *shell_surface,
			uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
		 uint32_t edges, int32_t width, int32_t height)
{
	struct window *window = data;

	if (window->native)
		wl_egl_window_resize(window->native, width, height, 0, 0);

	window->geometry.width = width;
	window->geometry.height = height;
	if (g_Dbg) {
		printf("handle_configure %dx%d\n", window->geometry.width,
		       window->geometry.height);
	}

	if (!window->fullscreen)
		window->window_size = window->geometry;
}

static void handle_popup_done(void *data,
			      struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_ping,
	handle_configure,
	handle_popup_done
};

static void redraw(void *data, struct wl_callback *callback, uint32_t time);
static void redraw_egl(void *data, struct wl_callback *callback, uint32_t time);
static void redraw_wl(void *data, struct wl_callback *callback, uint32_t time);

static void configure_callback(void *data, struct wl_callback *callback,
			       uint32_t time)
{
	struct window *window = data;

	wl_callback_destroy(callback);

	window->configured = 1;

	if (window->callback == NULL)
		redraw(data, NULL, time);
}

static struct wl_callback_listener configure_callback_listener = {
	configure_callback,
};

static void toggle_fullscreen(struct window *window, int fullscreen)
{
	struct wl_callback *callback;

	window->fullscreen = fullscreen;
	window->configured = 0;

	if (fullscreen) {
		wl_shell_surface_set_fullscreen(window->shell_surface,
						WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT,
						0, NULL);
	} else {
		wl_shell_surface_set_toplevel(window->shell_surface);
		handle_configure(window, window->shell_surface, 0,
				 window->window_size.width,
				 window->window_size.height);
	}

	callback = wl_display_sync(window->display->display);
	wl_callback_add_listener(callback, &configure_callback_listener,
				 window);
}

static void
ias_hmi_surface_info(void *data,
		     struct ias_hmi *hmi,
		     uint32_t id,
		     const char *name,
		     uint32_t zorder,
		     int32_t x, int32_t y,
		     uint32_t width, uint32_t height,
		     uint32_t alpha,
		     uint32_t behavior_bits,
		     uint32_t pid,
		     const char *pname, uint32_t dispno, uint32_t flipped)
{
	struct display *d = data;

	if (d->window->positioned == 1 || d->window->fullscreen == 1) {
		return;
	}

	if (!strcmp(name, d->window->name)) {
		ias_hmi_move_surface(d->hmi, id, d->window->x, d->window->y);
		d->window->positioned = 1;
	}
}

void ias_hmi_surface_destroyed(void *data,
			       struct ias_hmi *ias_hmi,
			       uint32_t id,
			       const char *title,
			       uint32_t pid, const char *pname)
{

}

void ias_hmi_surface_sharing_info(void *data,
				  struct ias_hmi *hmi,
				  uint32_t id,
				  const char *name,
				  uint32_t shareable,
				  uint32_t pid, const char *pname)
{
}

static const struct ias_hmi_listener hmi_listener = {
	ias_hmi_surface_info,
	ias_hmi_surface_destroyed,
	ias_hmi_surface_sharing_info
};

static void create_surface(struct window *window, const char *name)
{
	struct display *display = window->display;
	EGLBoolean ret;

	strcpy(window->name, name);

	window->surface = wl_compositor_create_surface(display->compositor);
	window->shell_surface =
	    wl_shell_get_shell_surface(display->shell, window->surface);

	wl_shell_surface_add_listener(window->shell_surface,
				      &shell_surface_listener, window);

	if (use_egl) {
		window->native = wl_egl_window_create(window->surface,
						      window->window_size.width,
						      window->window_size.
						      height);
		window->egl_surface =
		    eglCreateWindowSurface(g_eman_common.dpy, display->egl.conf,
					   window->native, NULL);
	}
	wl_shell_surface_set_title(window->shell_surface, name);

	if (use_egl) {
		ret =
		    eglMakeCurrent(g_eman_common.dpy, window->egl_surface,
				   window->egl_surface, g_eman_common.ctx);
		assert(ret == EGL_TRUE);
	}
	toggle_fullscreen(window, window->fullscreen);
}

static void destroy_surface(struct window *window)
{
	if (use_egl) {
		wl_egl_window_destroy(window->native);
	}

	wl_shell_surface_destroy(window->shell_surface);
	wl_surface_destroy(window->surface);

	if (window->callback)
		wl_callback_destroy(window->callback);
}

static const struct wl_callback_listener frame_listener;

static void redraw_egl(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *w = data;
	struct wl_region *region;
	int resize_needed = 0;
	double angle;
	int ret = 0;

	GLfloat vVertices[] = { -1.0f, 1.0f, 0.0f,	// Position 0
		0.0f, 0.0f,	// TexCoord 0

		-1.0f, -1.0f, 0.0f,	// Position 1
		0.0f, 1.0f,	// TexCoord 1

		1.0f, -1.0f, 0.0f,	// Position 2
		1.0f, 1.0f,	// TexCoord 2

		1.0f, 1.0f, 0.0f,	// Position 3
		1.0f, 0.0f	// TexCoord 3
	};
	GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

	GLfloat rotation_matrix[4][4] = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1}
	};

	assert(w->callback == callback);
	w->callback = NULL;

	if (callback)
		wl_callback_destroy(callback);

	if (!w->configured)
		return;

	if ((surf_rotation == 0 || surf_rotation == 180) &&
	    (w->window_size.width != surf_width ||
	     w->window_size.height != surf_height)) {
		resize_needed = 1;
	} else if ((surf_rotation == 90 || surf_rotation == 270) &&
		   (w->window_size.width != surf_height ||
		    w->window_size.height != surf_width)) {
		resize_needed = 1;
	}

	if (enable_fixed_size == 0 && resize_needed) {
		if (surf_rotation == 0 || surf_rotation == 180) {
			w->window_size.width = surf_width;
			w->window_size.height = surf_height;
		} else {
			w->window_size.width = surf_height;
			w->window_size.height = surf_width;
		}
		handle_configure(w, w->shell_surface, 0,
				 w->window_size.width, w->window_size.height);
	}

	if (w->opaque || w->fullscreen) {
		region = wl_compositor_create_region(w->display->compositor);
		wl_region_add(region, 0, 0, w->geometry.width,
			      w->geometry.height);
		// printf("%d : geometry %i x %i \n" , __LINE__ , w->geometry.width , w->geometry.height );
		wl_surface_set_opaque_region(w->surface, region);
		wl_region_destroy(region);
	} else {
		wl_surface_set_opaque_region(w->surface, NULL);
	}

	w->callback = wl_surface_frame(w->surface);
	wl_callback_add_listener(w->callback, &frame_listener, w);

	ret = check_for_new_buffer();

	// Set the viewport
	glViewport(0, 0, w->geometry.width, w->geometry.height);

	// Clear the color buffer
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (show_window == 0 || ret != 0) {
		eglSwapBuffers(g_eman_common.dpy, w->egl_surface);
		return;
	}
	// Use the program object
	glUseProgram(program);

	// CW rotation
	angle = -M_PI * surf_rotation / 180.0;

	rotation_matrix[0][0] = cos(angle);
	rotation_matrix[0][1] = sin(angle);
	rotation_matrix[1][0] = -sin(angle);
	rotation_matrix[1][1] = cos(angle);

	glUniformMatrix4fv(w->gl.rotation_uniform, 1, GL_FALSE,
			   (GLfloat *) rotation_matrix);

	// Load the vertex position
	glVertexAttribPointer(w->gl.pos, 3, GL_FLOAT, GL_FALSE,
			      5 * sizeof(GLfloat), vVertices);
	glEnableVertexAttribArray(w->gl.pos);

	// Load the texture coordinate
	glVertexAttribPointer(w->gl.col, 2, GL_FLOAT, GL_FALSE,
			      5 * sizeof(GLfloat), &vVertices[3]);
	glEnableVertexAttribArray(w->gl.col);

	// Bind the base map
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, current_textureId[0]);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, current_textureId[1]);

	if (enable_fps_info) {
		received_frames();
	}
	/* Set the base map samplers to texture units 0 and 1 */
	glUniform1i(w->gl.texture[0], 0);
	glUniform1i(w->gl.texture[1], 1);
	glUniform2f(w->gl.texture_size, surf_width / 2, surf_height);
	switch (current_texture_sampler_format) {
	default:
	case DRM_FORMAT_ARGB8888:
		glUniform1i(w->gl.color_format, 0);
		break;
	case DRM_FORMAT_YUYV:
		glUniform1i(w->gl.color_format, 1);
		break;
	case DRM_FORMAT_NV12:
		glUniform1i(w->gl.color_format, 2);
		break;
	}

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

	eglSwapBuffers(g_eman_common.dpy, w->egl_surface);

	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void redraw_wl(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *w = data;
	int ret;

	if (callback)
		wl_callback_destroy(callback);

	if (!w->configured)
		return;

	w->callback = wl_surface_frame(w->surface);
	wl_callback_add_listener(w->callback, &frame_listener, w);

	do {
		ret = check_for_new_buffer();
	} while (show_window == 0);

	if ((enable_fixed_size == 0) &&
	    (w->window_size.width != surf_width ||
	     w->window_size.height != surf_height)) {

		w->window_size.width = surf_width;
		w->window_size.height = surf_height;

		handle_configure(w, w->shell_surface, 0,
				 w->window_size.width, w->window_size.height);
	}

	if (enable_fps_info) {
		received_frames();
	}

	if (ret == 0) {
		wl_surface_attach(w->surface, current_buffer, 0, 0);
		wl_surface_damage(w->surface, 0, 0, w->window_size.width,
				  w->window_size.height);
	}
	wl_surface_commit(w->surface);
}

static void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	if (use_egl)
		redraw_egl(data, callback, time);
	else
		redraw_wl(data, callback, time);
}

static const struct wl_callback_listener frame_listener = {
	redraw
};

static
void translate_input(struct display *d,
		     wl_fixed_t fx, wl_fixed_t fy,
		     wl_fixed_t * ftx, wl_fixed_t * fty)
{
	double x, y, sw, sh;
	x = wl_fixed_to_double(fx);
	y = wl_fixed_to_double(fy);

	sw = (double)(surf_disp_w) / d->window->window_size.width;
	sh = (double)(surf_disp_h) / d->window->window_size.height;

	x *= sw;
	y *= sh;

	x += surf_disp_x;
	y += surf_disp_y;

	x *= 32767.0 / disp_w;
	y *= 32767.0 / disp_h;

	*ftx = wl_fixed_from_double(x);
	*fty = wl_fixed_from_double(y);
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
		     wl_fixed_t sy)
{
	struct display *display = data;
	struct wl_buffer *buffer;
	struct wl_cursor *cursor = display->default_cursor;
	struct wl_cursor_image *image;

	if (display->window->fullscreen)
		wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);
	else if (cursor) {
		image = display->default_cursor->images[0];
		buffer = wl_cursor_image_get_buffer(image);
		wl_pointer_set_cursor(pointer, serial, display->cursor_surface,
				      image->hotspot_x, image->hotspot_y);
		wl_surface_attach(display->cursor_surface, buffer, 0, 0);
		wl_surface_damage(display->cursor_surface, 0, 0, image->width,
				  image->height);
		wl_surface_commit(display->cursor_surface);
	}
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
				 uint32_t serial, struct wl_surface *surface)
{
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
				  uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_pointer_event event;
	struct display *display = data;
	wl_fixed_t trans_x, trans_y;

	translate_input(display, sx, sy, &trans_x, &trans_y);

	header.type = VMDISPLAY_POINTER_EVENT;
	header.size = sizeof(event);

	event.type = VMDISPLAY_POINTER_MOTION;
	event.time = time;
	event.x = trans_x;
	event.y = trans_y;

	send_input_event(&vmsocket, &header, &event);
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		      uint32_t serial, uint32_t time, uint32_t button,
		      uint32_t state)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_pointer_event event;
	struct display *display = data;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
		wl_shell_surface_move(display->window->shell_surface,
				      display->seat, serial);

	header.type = VMDISPLAY_POINTER_EVENT;
	header.size = sizeof(event);

	event.type = VMDISPLAY_POINTER_BUTTON;
	event.time = time;
	event.button = button;
	event.state = state;

	send_input_event(&vmsocket, &header, &event);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
		    uint32_t axis, wl_fixed_t value)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_pointer_event event;

	header.type = VMDISPLAY_POINTER_EVENT;
	header.size = sizeof(event);

	event.type = VMDISPLAY_POINTER_AXIS;
	event.time = time;
	event.axis = axis;
	event.value = value;

	send_input_event(&vmsocket, &header, &event);

}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
				   uint32_t format, int fd, uint32_t size)
{
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface,
		      struct wl_array *keys)
{
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
				  uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		    uint32_t serial, uint32_t time, uint32_t key,
		    uint32_t state)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_key_event event;
	struct display *d = data;

	if (key == KEY_F11 && state)
		toggle_fullscreen(d->window, d->window->fullscreen ^ 1);
	else if (key == KEY_ESC && state)
		running = 0;

	header.type = VMDISPLAY_KEY_EVENT;
	header.size = sizeof(event);

	event.type = VMDISPLAY_KEY_KEY;
	event.time = time;
	event.key = key;
	event.state = state;

	send_input_event(&vmsocket, &header, &event);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_key_event event;

	header.type = VMDISPLAY_KEY_EVENT;
	header.size = sizeof(event);

	event.type = VMDISPLAY_KEY_MODIFIERS;
	event.mods_depressed = mods_depressed;
	event.mods_latched = mods_latched;
	event.mods_locked = mods_locked;
	event.group = group;

	send_input_event(&vmsocket, &header, &event);
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
touch_handle_down(void *data, struct wl_touch *wl_touch,
		  uint32_t serial, uint32_t time, struct wl_surface *surface,
		  int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_touch_event event;
	header.type = VMDISPLAY_TOUCH_EVENT;
	header.size = sizeof(event);
	wl_fixed_t trans_x, trans_y;
	struct display *d = data;

	translate_input(d, x_w, y_w, &trans_x, &trans_y);

	event.type = VMDISPLAY_TOUCH_DOWN;
	event.id = id;
	event.x = trans_x;
	event.y = trans_y;

	send_input_event(&vmsocket, &header, &event);
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
		uint32_t serial, uint32_t time, int32_t id)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_touch_event event;
	header.type = VMDISPLAY_TOUCH_EVENT;
	header.size = sizeof(event);

	event.type = VMDISPLAY_TOUCH_UP;
	event.id = id;

	send_input_event(&vmsocket, &header, &event);
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
		    uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_touch_event event;
	header.type = VMDISPLAY_TOUCH_EVENT;
	header.size = sizeof(event);
	wl_fixed_t trans_x, trans_y;
	struct display *d = data;

	translate_input(d, x_w, y_w, &trans_x, &trans_y);

	event.type = VMDISPLAY_TOUCH_MOTION;
	event.id = id;
	event.x = trans_x;
	event.y = trans_y;

	send_input_event(&vmsocket, &header, &event);
}

static void touch_handle_frame(void *data, struct wl_touch *wl_touch)
{

}

static void touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_touch_event event;
	header.type = VMDISPLAY_TOUCH_EVENT;
	header.size = sizeof(event);
	event.type = VMDISPLAY_TOUCH_CANCEL;

	send_input_event(&vmsocket, &header, &event);
}

static const struct wl_touch_listener touch_listener = {
	touch_handle_down,
	touch_handle_up,
	touch_handle_motion,
	touch_handle_frame,
	touch_handle_cancel
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
				     enum wl_seat_capability caps)
{
	struct display *d = data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
		d->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(d->pointer, &pointer_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && d->pointer) {
		wl_pointer_destroy(d->pointer);
		d->pointer = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
		d->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && d->keyboard) {
		wl_keyboard_destroy(d->keyboard);
		d->keyboard = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !d->touch) {
		d->touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(d->touch, &touch_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && d->touch) {
		wl_touch_destroy(d->touch);
		d->touch = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
		       const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
		    wl_registry_bind(registry, name, &wl_compositor_interface,
				     1);
	} else if (strcmp(interface, "wl_shell") == 0) {
		d->shell =
		    wl_registry_bind(registry, name, &wl_shell_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		d->seat =
		    wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(d->seat, &seat_listener, d);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		d->cursor_theme = wl_cursor_theme_load(NULL, 32, d->shm);
		d->default_cursor =
		    wl_cursor_theme_get_cursor(d->cursor_theme, "left_ptr");
	} else if (strcmp(interface, "ias_hmi") == 0) {
		d->hmi =
		    wl_registry_bind(registry, name, &ias_hmi_interface, 1);
		ias_hmi_add_listener(d->hmi, &hmi_listener, d);
	} else if (strcmp(interface, "wl_drm") == 0) {
		d->wl_drm =
		    wl_registry_bind(registry, name, &wl_drm_interface, 2);
		wl_drm = d->wl_drm;
	} else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
		d->wl_dmabuf =
		    wl_registry_bind(registry, name,
				     &zwp_linux_dmabuf_v1_interface, 3);
		wl_dmabuf = d->wl_dmabuf;
	}
}

static void registry_handle_global_remove(void *data,
					  struct wl_registry *registry,
					  uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void signal_int(int signum)
{
	running = 0;
}

static void usage(int error_code)
{
	fprintf(stderr, "Usage: vmdisplay-wayland <dom_id> <Name> [OPTIONS]\n\n"
		"  -f\tRun in fullscreen mode\n"
		"  -o\tCreate an opaque surface\n"
		"  -p\tEnable buffer table content printing\n"
		"  -i\tApplication index from DomU for surface to be displayed\n"
		"  -I\tSurface ID to be displayed; takes precedence over -i; ID has to be different than 0\n"
		"  -b\tEnable received frames rate measurement\n"
		"  -X\tWindow X\n"
		"  -Y\tWindow Y\n"
		"  -H\tWindow height\n"
		"  -W\tWindow width\n"
		"  -d\tDisplay number\n"
		"  -w\tUse wl_drm for rendering\n" "  -h\tThis help text\n\n");
	exit(error_code);
}

int main(int argc, char **argv)
{
	eglBindAPI(EGL_OPENGL_API);

	struct sigaction sigint;
	struct display display = { 0 };
	struct window window = { 0 };
	int i, ret = 0;
	int domid;

	if (argc < 3) {
		usage(EXIT_SUCCESS);
	}

	window.display = &display;
	display.window = &window;
	window.window_size.width = 1920 / 2;
	window.window_size.height = 1080 / 2;

	for (i = 1; i < argc; i++) {
		if (strcmp("-f", argv[i]) == 0) {
			window.fullscreen = 1;
		} else if (strcmp("-o", argv[i]) == 0) {
			window.opaque = 1;
		} else if (strcmp("-X", argv[i]) == 0) {
			window.x = atoi(argv[i + 1]);
		} else if (strcmp("-Y", argv[i]) == 0) {
			window.y = atoi(argv[i + 1]);
		} else if (strcmp("-W", argv[i]) == 0) {
			window.window_size.width = atoi(argv[i + 1]);
			enable_fixed_size = 1;
		} else if (strcmp("-H", argv[i]) == 0) {
			window.window_size.height = atoi(argv[i + 1]);
			enable_fixed_size = 1;
		} else if (strcmp("-I", argv[i]) == 0) {
			surf_id = strtol(argv[i + 1], NULL, 0);
		} else if (strcmp("-i", argv[i]) == 0) {
			surf_index = atoi(argv[i + 1]);
		} else if (strcmp("-b", argv[i]) == 0) {
			enable_fps_info = 1;
		} else if (strcmp("-d", argv[i]) == 0) {
			pipe_id = atoi(argv[i + 1]);
		} else if (strcmp("-w", argv[i]) == 0) {
			use_egl = 0;
		} else if (strcmp("-e", argv[i]) == 0) {
			use_event_poll = 1;
		} else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS);
	}

	domid = atoi(argv[1]);
	open_drm();

#if 0
	init_buffers();
#endif

	if (g_Dbg) {
		printf("Debug:%d h:%d w:%d\n", g_Dbg, window.window_size.height,
		       window.window_size.width);
	}

	init_hyper_dmabuf(domid);

	display.display = wl_display_connect(NULL);
	assert(display.display);

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry, &registry_listener,
				 &display);

	wl_display_dispatch(display.display);

	if (use_egl)
		init_egl(&display, window.opaque);
	create_surface(&window, argv[2]);
	if (use_egl) {
		init_gl(&window, 0);
	}

	/* if none of other optional sharing methods, which don't require
	 * network connection, initialize socket for meta data exchange for
	 * hyper_dmabuf sharing
	 */
	if (!use_event_poll) {
		if (vmdisplay_socket_init(&vmsocket, domid)) {
			printf("Cannot connect to  domain %d\n", domid);
			return -1;
		}
	}

	display.cursor_surface =
	    wl_compositor_create_surface(display.compositor);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	while (running && ret != -1)
		ret = wl_display_dispatch(display.display);

	clear_hyper_dmabuf_list();

	destroy_surface(&window);
	if (use_egl)
		fini_egl(&display);

	wl_surface_destroy(display.cursor_surface);
	if (display.cursor_theme)
		wl_cursor_theme_destroy(display.cursor_theme);

	if (display.shell)
		wl_shell_destroy(display.shell);

	if (display.compositor)
		wl_compositor_destroy(display.compositor);

	if (display.hmi)
		ias_hmi_destroy(display.hmi);

	wl_registry_destroy(display.registry);
	wl_display_flush(display.display);
	wl_display_disconnect(display.display);

	vmdisplay_socket_cleanup(&vmsocket);
	return 0;
}
