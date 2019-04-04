/*
 *-----------------------------------------------------------------------------
 * Filename: remote-display-show.c
 *-----------------------------------------------------------------------------
 * Copyright 2012-2019 Intel Corporation
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
 *   This utilty transform into a texture the prime handle or shm buffer received by the
 *   remote-display technique. That one can be easily transformed (scaled, rotate, ..)
 *   Per implementation, the texure is 1 frame late vs what weston is showing.
 *
 * usage:
 *  start weston-simple-egl and weston-simple-shm so we have the 2 types (egl & shm)
 * then:
 *    remote-display-show --surfname=simple-shm --z=100 --x=100 --y=200 --w=640 --h=480
 *    remote-display-show --surfname=simple-egl --z=100 --x=300 --y=300 --w=800 --h=600
 * Show the fullscreen ouput=0 as a texture & rotate it by 180
 *    remote-display-show --output=0 --trsf=0.0,1.0,1.0,0.0
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/mman.h>
#include <assert.h>
#include <drm/drm_fourcc.h>
#include <sys/poll.h>
#include "ias-shell-client-protocol.h"
#include <libweston/config-parser.h>
#include "../shared/helpers.h"
#include "debug.h"

#define FREE_IF_NEEDED(X) if (X) free(X);

int debug_level = DBG_OFF;
GLfloat vVertices[] = {
	-1.0f,  1.0f, 0.0f,  // Position 0   A    A----D
	 0.0f,  0.0f,        // TexCoord 0        |    |
	-1.0f, -1.0f, 0.0f,  // Position 1   B    B----C
	 0.0f,  1.0f,        // TexCoord 1
	 1.0f, -1.0f, 0.0f,  // Position 2   C
	 1.0f,  1.0f,        // TexCoord 2
	 1.0f,  1.0f, 0.0f,  // Position 3   D
	 1.0f,  0.0f         // TexCoord 3
};

GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

PFNEGLCREATEIMAGEKHRPROC            create_image = NULL;
PFNEGLDESTROYIMAGEKHRPROC           destroy_image = NULL;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d = NULL;

struct display {
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig  egl_conf;
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_shm *shm;
	struct ias_hmi *hmi;
	struct window *window;
	int output_number;
	uint32_t surfid;
	uint32_t prime_w;
	uint32_t prime_h;
	int on;
	int profile;
	int verbose;
	int zorder;
	int px;
	int py;
	uint32_t pid;
	int configured;
	int running;
	uint32_t tracksurfid;
	char *surfname;
	char *pname;
	char *trsf;
	GLfloat tr;
	GLfloat tb;
	GLfloat tl;
	GLfloat tt;

};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;
	struct {
		GLuint pos;
		GLuint col;
		GLuint texture;
	} gl;
	struct wl_egl_window *native;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	EGLSurface egl_surface;
	struct wl_callback *callback;
	int fullscreen, configured, opaque;
	GLuint program;
	GLuint textureId;
	char *winname;
};

static const char *vert_shader_text =
	"attribute vec4 a_position;   \n"
	"attribute vec2 a_texCoord;   \n"
	"varying vec2 v_texCoord;     \n"
	"void main()                  \n"
	"{                            \n"
	"   gl_Position = a_position; \n"
	"   v_texCoord = a_texCoord;  \n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;                            \n"
	"varying vec2 v_texCoord;                            \n"
	"uniform sampler2D s_baseMap;                        \n"
	"void main()                                         \n"
	"{                                                   \n"
	"  gl_FragColor = texture2D( s_baseMap, v_texCoord );\n"
	"}\n";

struct display display = { 0 };
struct window  window  = { 0 };

static int
load_prime(struct display *disp, uint32_t surf_width, uint32_t surf_height,uint32_t  img_format, int32_t dmabuf_fd, uint32_t stride)
{
	EGLint imageAttributes[] = {
		EGL_WIDTH, surf_width,
		EGL_HEIGHT, surf_height,
		EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
		EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf_fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
		EGL_NONE
	};
	EGLImageKHR image  = (EGLImageKHR) (uintptr_t) create_image(disp->dpy,
			EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
			(EGLClientBuffer) NULL, imageAttributes);
	DBG("eglCreateImageKHR 0x%x [%dx%d - P=%d S=%d]\n",
			eglGetError (), surf_width, surf_height, dmabuf_fd, stride);
	image_target_texture_2d(GL_TEXTURE_2D, image);
	DBG("glEGLImageTargetTexture2DOES 0x%x\n", eglGetError ());
	destroy_image(disp->dpy, image);
	return 0;
}

static int
load_shm(struct display *disp,
		uint32_t surf_width,
		uint32_t surf_height,
		int32_t handle,
		uint32_t stride)
{
	EGLint imageAttributes[] = {
		EGL_WIDTH,                  surf_width,
		EGL_HEIGHT,                 surf_height,
		EGL_DRM_BUFFER_STRIDE_MESA, stride/4,
		EGL_DRM_BUFFER_FORMAT_MESA, EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
		EGL_NONE
	};
	EGLImageKHR image  = (EGLImageKHR) (uintptr_t) create_image(disp->dpy,
			EGL_NO_CONTEXT,
			EGL_DRM_BUFFER_MESA,
			(EGLClientBuffer)(uintptr_t) handle,
			imageAttributes);

	DBG("eglCreateImageKHR 0x%x [%dx%d - H=%d S=%d]\n",
			eglGetError (),
			surf_width,
			surf_height,
			handle,
			stride);
	image_target_texture_2d(GL_TEXTURE_2D, image);
	DBG("glEGLImageTargetTexture2DOES 0x%x\n", eglGetError ());
	destroy_image(disp->dpy, image);
	return 0;
}


static void
init_egl(struct display *display, int opaque)
{
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLint major, minor, n;
	EGLBoolean ret;
	display->dpy = eglGetDisplay(display->display);
	assert(display->dpy);
	ret = eglInitialize(display->dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);
	ret = eglChooseConfig(display->dpy, config_attribs,
			&display->egl_conf, 1, &n);
	assert(ret && n == 1);
	display->ctx = eglCreateContext(display->dpy,
			display->egl_conf,
			EGL_NO_CONTEXT, context_attribs);
	assert(display->ctx);
	if (!create_image) {
		create_image = (void *) eglGetProcAddress("eglCreateImageKHR");
		if (!create_image)
			ERROR("get eglCreateImageKHR error\n");
	}
	if (!image_target_texture_2d) {
		image_target_texture_2d = (void *) eglGetProcAddress("glEGLImageTargetTexture2DOES");
		if (!image_target_texture_2d)
			ERROR("get glEGLImageTargetTexture2DOES error\n");
	}
	if (!destroy_image) {
		destroy_image = (void *) eglGetProcAddress("eglDestroyImageKHR");
		if (!destroy_image)
			ERROR("get eglDestroyImageKHR error\n");
	}
}

static void
fini_egl(struct display *display)
{
	eglMakeCurrent(display->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(display->dpy);
	eglReleaseThread();
}

static GLuint
create_shader(struct window *window, const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;
	shader = glCreateShader(shader_type);
	assert(shader != 0);
	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		ERROR("Error: compiling %s: %*s\n",
				shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
				len, log);
		exit(1);
	}
	return shader;
}

static void
init_gl(struct window *window)
{
	GLuint frag, vert;
	GLint status;
	frag = create_shader(window, frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);
	window->program = glCreateProgram();
	glAttachShader(window->program, frag);
	glAttachShader(window->program, vert);
	glLinkProgram(window->program);
	glGetProgramiv(window->program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(window->program, 1000, &len, log);
		ERROR("Error: linking:\n%*s\n", len, log);
		exit(1);
	}
	glUseProgram(window->program);
	window->gl.pos = 0;
	window->gl.col = 1;
	glBindAttribLocation(window->program, window->gl.pos, "a_position");
	glBindAttribLocation(window->program, window->gl.col, "a_texCoord");
	glLinkProgram(window->program);
	window->gl.texture = glGetUniformLocation (window->program, "s_baseMap");
	glGenTextures(1, &window->textureId);
	glBindTexture(GL_TEXTURE_2D, window->textureId);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

static void
handle_ping(void *data, struct wl_shell_surface *shell_surface,
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
	DBG("handle_configure %dx%d\n", window->geometry.width, window->geometry.height);
	if (!window->fullscreen)
		window->window_size = window->geometry;
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_ping,
	handle_configure,
	handle_popup_done
};

static void
redraw(void *data, struct wl_callback *callback, uint32_t time);

static void
configure_callback(void *data, struct wl_callback *callback, uint32_t  time)
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

static void
toggle_fullscreen(struct window *window, int fullscreen)
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
create_surface(struct window *window)
{
	struct display *display = window->display;
	EGLBoolean ret;
	window->surface = wl_compositor_create_surface(display->compositor);
	window->shell_surface = wl_shell_get_shell_surface(display->shell,
			window->surface);
	wl_shell_surface_add_listener(window->shell_surface,
			&shell_surface_listener, window);
	window->native = wl_egl_window_create(window->surface,
			window->window_size.width,
			window->window_size.height);
	DBG("Create with: %dx%d\n", window->window_size.width, window->window_size.height);

	window->egl_surface = eglCreateWindowSurface(display->dpy, display->egl_conf, window->native, NULL);
	wl_shell_surface_set_title(window->shell_surface, window->winname);
	ret = eglMakeCurrent(display->dpy, window->egl_surface, window->egl_surface, display->ctx);
	assert(ret == EGL_TRUE);
	toggle_fullscreen(window, window->fullscreen);
	DBG("GL_RENDERER   = %s\n", (char *)glGetString(GL_RENDERER));
	DBG("GL_VERSION    = %s\n", (char *)glGetString(GL_VERSION));
	DBG("GL_VENDOR     = %s\n", (char *)glGetString(GL_VENDOR));
	DBG("GL_EXTENSIONS = %s\n", (char *)glGetString(GL_EXTENSIONS));
	DBG("EGL_VENDOR    = %s\n", (char *)eglQueryString(window->display->dpy, EGL_VENDOR));
	DBG("EGL_VERSION   = %s\n", (char *)eglQueryString(window->display->dpy, EGL_VERSION));
	DBG("EGL_EXTENSIONS= %s\n", (char *)eglQueryString(window->display->dpy, EGL_EXTENSIONS));
}

static void
destroy_surface(struct window *window)
{
	wl_egl_window_destroy(window->native);
	wl_shell_surface_destroy(window->shell_surface);
	wl_surface_destroy(window->surface);
	if (window->callback)
		wl_callback_destroy(window->callback);
}

static const struct wl_callback_listener frame_listener;


static void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *w = data;
	struct wl_region *region;
	assert(w->callback == callback);
	w->callback = NULL;
	if (callback)
		wl_callback_destroy(callback);
	if (!w->configured)
		return;
	if (w->opaque || w->fullscreen) {
		region = wl_compositor_create_region(w->display->compositor);
		wl_region_add(region, 0, 0, w->geometry.width, w->geometry.height);
		DBG("%d : geometry %i x %i \n" , __LINE__ , w->geometry.width , w->geometry.height );
		wl_surface_set_opaque_region(w->surface, region);
		wl_region_destroy(region);
	}
	else {
		wl_surface_set_opaque_region(w->surface, NULL);
	}
	w->callback = wl_surface_frame(w->surface);
	wl_callback_add_listener(w->callback, &frame_listener, w);
	glViewport(0, 0, w->geometry.width, w->geometry.height );
	glClearColor(0.0,0.0,0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(w->program);
	glVertexAttribPointer(w->gl.pos, 3, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), vVertices);
	glEnableVertexAttribArray(w->gl.pos);
	glVertexAttribPointer(w->gl.col, 2, GL_FLOAT, GL_FALSE, 5*sizeof(GLfloat), &vVertices[3]);
	glEnableVertexAttribArray(w->gl.col);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, w->textureId);
	glUniform1i(w->gl.texture, 0);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (debug_level == DBG_DBG) {
		static uint32_t x = 0;
		DBG("swap %u\n", x++);
	}
	eglSwapBuffers(w->display->dpy, w->egl_surface);
}


static const struct wl_callback_listener frame_listener = {
	redraw
};

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface)
{
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button,
		uint32_t state)
{
	struct display *display = data;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
		wl_shell_surface_move(display->window->shell_surface,
				display->seat, serial);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
		uint32_t format, int fd, uint32_t size)
{
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface,
		struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key,
		uint32_t state)
{
	struct display *d = data;

	if (key == KEY_F11 && state)
		toggle_fullscreen(d->window, d->window->fullscreen ^ 1);
	else if (key == KEY_ESC && state)
		d->running = 0;
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
              uint32_t serial, uint32_t mods_depressed,
              uint32_t mods_latched, uint32_t mods_locked,
              uint32_t group)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
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
}

static const struct wl_seat_listener seat_listener = {
    seat_handle_capabilities,
};


static void
handle_raw_buffer_handle(void *data,
		struct ias_hmi *ias_hmi,
		int32_t handle,
		uint32_t timestamp,
		uint32_t frame_number,
		uint32_t stride0,
		uint32_t stride1,
		uint32_t stride2,
		uint32_t format,
		uint32_t out_width,
		uint32_t out_height,
		uint32_t shm_surf_id,
		uint32_t buf_id,
		uint32_t image_id)
{
	struct display *d = data;
	DBG("ias_hmi_raw_buffer_handle:\n"
			"h: %d "
			"timestamp: %u "
			"frame_number: %u "
			"stride0: %u "
			"stride1: %u "
			"stride2: %u "
			"format: %u "
			"width: %u "
			"height: %u "
			"shm_surf_id: %u "
			"buf_id: %u "
			"image_id: %u\n",
			handle, timestamp, frame_number, stride0, stride1,
			stride2, format, out_width, out_height, shm_surf_id,
			buf_id, image_id);
	glBindTexture(GL_TEXTURE_2D, window.textureId);
	load_shm(d, out_width, out_height, handle, stride0);
	ias_hmi_release_buffer_handle(ias_hmi, shm_surf_id, buf_id, image_id,
			d->surfid, 0);
}

static void
handle_raw_buffer_fd(void *data,
              struct ias_hmi *ias_hmi,
              int32_t prime_fd,
              uint32_t timestamp,
              uint32_t frame_number,
              uint32_t stride0,
              uint32_t stride1,
              uint32_t stride2,
              uint32_t format,
              uint32_t out_width,
              uint32_t out_height)
{
	struct display *d = data;
	DBG("handle_raw_buffer_fd: %d "
			"ts: %u "
			"fn: %u "
			"str0: %u "
			"str1: %u "
			"str2: %u "
			"f: %u "
			"w: %u "
			"h: %u\n",
			prime_fd, timestamp, frame_number,
			stride0, stride1, stride2, format,
			out_width, out_height);
	glBindTexture(GL_TEXTURE_2D, window.textureId);
	if (d->surfid) {
		load_prime(d, d->prime_w,d->prime_h, format, prime_fd, stride0);
	} else {
		load_prime(d, out_width, out_height, format, prime_fd, stride0);
	}
	close(prime_fd);
	ias_hmi_release_buffer_handle(ias_hmi, 0, 0, 0, d->surfid, 0);

}

static void
handle_surface_destroyed(void *data,
		struct ias_hmi *hmi,
		uint32_t id,
		const char *name,
		uint32_t pid,
		const char *pname)
{
	struct display *d = data;
	INFO("Destroy '%s' id=%u P=%4d p=%s\n", name, id, pid, pname);
	if (id == d->surfid) {
		if (d->on) {
			d->on = 0;
			INFO("Stop\n");
			ias_hmi_stop_capture(d->hmi, d->surfid, d->output_number);
		}
	}
}

static void
handle_capture_error(void *data,
		struct ias_hmi *hmi,
		int32_t pid,
		int32_t error)
{
	struct display *d = data;
	if (d->pid == (uint32_t)pid) {
		ERROR("%d\n", error);
		int new_state = 1;
		switch(error) {
			case IAS_HMI_FCAP_ERROR_NO_CAPTURE_PROXY:
				ERROR("Capture proxy error: No proxy.\n");
				break;
			case IAS_HMI_FCAP_ERROR_DUPLICATE:
				ERROR("Capture error: Duplicate "
						"surface/output requested.\n");
				break;
			case IAS_HMI_FCAP_ERROR_NOT_BUILT_IN:
				ERROR("Capture proxy not built into Weston!\n");
				break;
			case IAS_HMI_FCAP_ERROR_INVALID:
				ERROR("Capture proxy error: Invalid parameter\n");
				break;
			case IAS_HMI_FCAP_ERROR_OK:
				/* No actual error. */
				new_state = 1; // ack
				break;
		}
		if (display.on) {
			display.running = new_state;
		}
	}
}


static void
handle_surface_info(void *data,
		struct ias_hmi *ias_hmi,
		uint32_t id,
		const char *name,
		uint32_t zorder,
		int32_t x,
		int32_t y,
		uint32_t width,
		uint32_t height,
		uint32_t alpha,
		uint32_t behavior_bits,
		uint32_t pid,
		const char *pname,
		uint32_t output,
		uint32_t flipped)
{
	struct display *d = data;
	int attempt_start = 0;
	DBG("S='%s' id=%u  %4dx%4d (%4d,%4d)"
			" Z=0x%02x a=%3d P=%4d N='%s' B=0x%02x f=%d\n",
			name,
			id, width, height, x, y, zorder, alpha, pid, pname, behavior_bits, flipped);
	if (id == d->tracksurfid) {
		attempt_start = 1;
		d->surfid = id;
	}
	if (d->surfname) {
		if (strcmp(d->surfname, name) == 0) {
			attempt_start = 1;
			d->surfid = id;

		}
	}
	if (d->pname) {
		if (strcmp(d->pname, pname) == 0) {
			attempt_start = 1;
			d->surfid = id;
		}
	}
	if (attempt_start) {
		if (d->on == 0 && width && height) {
			d->prime_w = width;
			d->prime_h = height;
			INFO("start\n");
			ias_hmi_start_capture(d->hmi, d->surfid,
					d->output_number, d->profile, d->verbose);
			d->on = 1;

		}
	}
	if (d->pid == pid) {
		if (width && !d->configured) {
			d->configured = 1;
			INFO("Initial config: %dx%d z=%d\n",  d->px, d->py, d->zorder);
			ias_hmi_zorder_surface(d->hmi, id, d->zorder);
			if (d->px || d->py) {
				ias_hmi_move_surface(d->hmi, id, d->px, d->py);
			}
		}
	}
}

static void
handle_surface_sharing_info(void *data,
		struct ias_hmi *ias_hmi,
		uint32_t id,
		const char *title,
		uint32_t shareable,
		uint32_t pid,
		const char *pname)
{

}


static const struct ias_hmi_listener hmi_listener = {
	handle_surface_info,
	handle_surface_destroyed,
	handle_surface_sharing_info,
	handle_raw_buffer_handle,
	handle_raw_buffer_fd,
	handle_capture_error,
};


static void
registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;
	if (strcmp(interface, "ias_hmi") == 0) {
		d->hmi = wl_registry_bind(registry, name, &ias_hmi_interface, 1);
		ias_hmi_add_listener(d->hmi, &hmi_listener, d);
	} else  if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor = wl_registry_bind(registry, name,
					&wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_shell") == 0) {
		d->shell = wl_registry_bind(registry, name,
				&wl_shell_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		d->seat = wl_registry_bind(registry, name,
				&wl_seat_interface, 1);
		wl_seat_add_listener(d->seat, &seat_listener, d);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry, name,
				&wl_shm_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
signal_int(int signum)
{
	display.running = 0;
}

static void
usage(int error_code)
{
	PRINT("Usage: [OPTIONS]\n\n"
			"  --winname=<ias surface name>\n"
			"  --surfid=<surfid> or --surfname=<surfname> or --pname=<process name>\n"
			"  --output=<output>\n"
			"  --winname=<window name> default is show_<surfid|pname|surfname|output>\n" 
			"  --x=<posx>\n"
			"  --y=<posy>\n"
			"  --z=<zorder>\n"
			"  --w=<width>\n"
			"  --h=<height>\n"
			"  --output=<output> If specified, it's based on output, surf* are ignored\n"
			"  --trsf=<l,r,t,b> Left, Right, Top, Bottom - float value default: <0.0,1.0,0.0,1.0>\n"
			"                    rotate: 0.0,1.0,1.0,0.0 crop: 0.1,0.9,0.2,0.8\n"
			"  --dgb=<level>\n"
			"  --help\tThis help text\n\n");
	exit(error_code);
}

int
main(int argc, char **argv)
{
	struct pollfd pfd;
	int ret = 0;
	int help = 0;
	window.display = &display;
	display.window = &window;
	display.output_number = -1;

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "dbg", 0, &debug_level},
		{ WESTON_OPTION_UNSIGNED_INTEGER, "surfid", 0, &display.tracksurfid},
		{ WESTON_OPTION_INTEGER, "output", 0, &display.output_number},
		{ WESTON_OPTION_STRING,  "surfname", 0, &display.surfname},
		{ WESTON_OPTION_STRING,  "pname", 0, &display.pname},
		{ WESTON_OPTION_STRING,  "winname", 0, &window.winname},
		{ WESTON_OPTION_INTEGER, "profile", 0, &display.profile},
		{ WESTON_OPTION_INTEGER, "z", 0, &display.zorder},
		{ WESTON_OPTION_INTEGER, "x", 0, &display.px},
		{ WESTON_OPTION_INTEGER, "y", 0, &display.py},
		{ WESTON_OPTION_INTEGER, "w", 0, &window.window_size.width},
		{ WESTON_OPTION_INTEGER, "h", 0, &window.window_size.height},
		{ WESTON_OPTION_STRING,  "trsf", 0, &display.trsf},
		{ WESTON_OPTION_BOOLEAN, "help", 0, &help },
	};

	if (argc<2)
		help=1;

	parse_options(options, ARRAY_LENGTH(options), &argc, argv);
	if ((!display.surfname && !display.pname &&
			(!display.tracksurfid)) && (display.output_number<0))
		help = 1;

	if (help) {
		usage(0);
	}

	if (!window.window_size.width)
		window.window_size.width = 960;
	if (!window.window_size.height)
		window.window_size.height = 540;

	display.running = 1;
	display.pid = getpid();

	if (!window.winname) {
		char tmp_name[256] = {"show"};
		if (display.tracksurfid) {
			sprintf(tmp_name, "show_%u", display.surfid);
		} else if (display.surfname) {
			sprintf(tmp_name, "show_%s", display.surfname);
		} else if (display.pname) {
			sprintf(tmp_name, "show_%s", display.pname);
		} else if (display.output_number != -1) {
			sprintf(tmp_name, "show_%u", display.output_number);
		}
		window.winname = strdup(tmp_name);
	}
	if (display.trsf) {
		if (sscanf(display.trsf,"%f,%f,%f,%f", &display.tl,&display.tr,&display.tt,&display.tb) == 4) {
			INFO("trsf: %.4f %.4f %.4f %.4f\n", display.tl, display.tr, display.tt, display.tb);
			vVertices[3] = display.tl;
			vVertices[4] = display.tt;
			vVertices[8] = display.tl;
			vVertices[9] = display.tb;
			vVertices[13] = display.tr;
			vVertices[14] = display.tb;
			vVertices[18] = display.tr;
			vVertices[19] = display.tt;
		} else {
			ERROR("Wrong trsf: '%s'\n", display.trsf);
			return -1;
		}
	}

	display.display = wl_display_connect(NULL);
	assert(display.display);

	init_egl(&display, window.opaque);
	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry, &registry_listener, &display);
	wl_display_dispatch(display.display);

	create_surface(&window);
	init_gl(&window);
	signal(SIGINT, signal_int);
	signal(SIGTERM, signal_int);

	INFO("surf=%u output=%d w=%d h=%d\n",
			display.tracksurfid,
			display.output_number,
			window.window_size.width,
			window.window_size.height);

	if (display.output_number>=0) {
		ias_hmi_start_capture(display.hmi,
				display.surfid,
				display.output_number,
				display.profile,
				display.verbose);
		display.on = 1;
	}

	while (display.running && ret != -1) {
		wl_display_roundtrip(display.display);
		pfd.fd =  wl_display_get_fd(display.display);
		pfd.events = POLLIN;
		pfd.revents = 0;
		poll(&pfd, 1, 500);
	}

	INFO("Stop\n");
	if (display.on) {
		ias_hmi_stop_capture(display.hmi, display.surfid, display.output_number);
	}
	if (window.textureId) {
		glDeleteTextures(1, &window.textureId);
	}
	destroy_surface(&window);
	fini_egl(&display);
	if (display.shell)
		wl_shell_destroy(display.shell);
	if (display.compositor)
		wl_compositor_destroy(display.compositor);
	wl_registry_destroy(display.registry);
	wl_display_flush(display.display);
	wl_display_disconnect(display.display);
	FREE_IF_NEEDED(display.trsf);
	FREE_IF_NEEDED(display.pname);
	FREE_IF_NEEDED(display.surfname);
	FREE_IF_NEEDED(window.winname);
	return 0;
}
