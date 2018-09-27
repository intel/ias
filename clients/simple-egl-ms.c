/*
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "ias-shell-client-protocol.h"
#include "xdg-shell-unstable-v6-client-protocol.h"

#include <sys/types.h>
#include <unistd.h>
#include "ivi-application-client-protocol.h"
#define IVI_SURFACE_ID 9000

#include "shared/helpers.h"
#include "shared/platform.h"
#include "weston-egl-ext.h"

GLuint g_fbo_id = 0, g_tex_id = 0;
GLuint program;
GLuint texture_uniform, if_triangle_uniform;

struct window;
struct seat;

struct output {
	struct display *display;
	struct wl_output *output;
	struct wl_list link;
};

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct ias_shell *ias_shell;
	struct zxdg_shell_v6 *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_touch *touch;
	struct wl_keyboard *keyboard;
	struct wl_shm *shm;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *default_cursor;
	struct wl_surface *cursor_surface;
	struct {
		EGLDisplay dpy;
		EGLContext ctx;
		EGLConfig conf;
	} egl;
	struct window *window;
	struct ivi_application *ivi_application;
	struct wl_list output_list;

	PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;
	struct {
		GLuint rotation_uniform;
		GLuint pos;
		GLuint col;
		GLuint tex;
		GLuint position;
	} gl;

	uint32_t benchmark_time, frames;
	struct wl_egl_window *native, *native2;
	struct wl_surface *surface, *surface2;
	struct ias_surface *shell_surface, *shell_surface2;
	struct zxdg_surface_v6 *xdg_surface, *xdg_surface2;
	struct zxdg_toplevel_v6 *xdg_toplevel, *xdg_toplevel2;
	struct ivi_surface *ivi_surface, *ivi_surface2;
	EGLSurface egl_surface, egl_surface2;
	struct wl_callback *callback;
	int fullscreen, maximized, opaque, buffer_size, frame_sync, output, delay;
	bool wait_for_configure;
};

static const char *vert_shader_text =
	"uniform mat4 rotation;\n"
	"uniform bool if_triangle;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"attribute vec2 rectangle_pos;\n"
	"attribute vec2 tex_pos;"
	"varying vec4 v_color;\n"
	"varying vec2 vTexCoord;"
	"void main() {\n"
	"  if (if_triangle == true) {\n"
	"  gl_Position = rotation * pos;\n"
	"  v_color = color;\n"
	"  } else {\n"
	"    vTexCoord = tex_pos;\n"
	"    gl_Position = vec4(rectangle_pos, 0.0, 1.0);\n"
	"  }\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"uniform bool if_triangle;\n"
	"uniform sampler2D texture;\n"
	"varying vec4 v_color;\n"
	"varying vec2 vTexCoord;\n"
	"void main() {\n"
	"  if (if_triangle == true) {\n"
	"    gl_FragColor = v_color;\n"
	"   } else {\n"
    "    gl_FragColor = texture2D(texture, vTexCoord);\n"
	"  }\n"
	"}\n";

static int running = 1;


static struct output *
get_default_output(struct display *display)
{
	struct output *iter;
	int counter = 0;
	wl_list_for_each(iter, &display->output_list, link) {
		if(counter++ == display->window->output)
			return iter;
	}

	// Unreachable, but avoids compiler warning
	return NULL;
}

static void
init_egl(struct display *display, struct window *window)
{
	static const struct {
		char *extension, *entrypoint;
	} swap_damage_ext_to_entrypoint[] = {
		{
			.extension = "EGL_EXT_swap_buffers_with_damage",
			.entrypoint = "eglSwapBuffersWithDamageEXT",
		},
		{
			.extension = "EGL_KHR_swap_buffers_with_damage",
			.entrypoint = "eglSwapBuffersWithDamageKHR",
		},
	};

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	const char *extensions;

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint major, minor, n, count, i, size;
	EGLConfig *configs;
	EGLBoolean ret;

	if (window->opaque || window->buffer_size == 16)
		config_attribs[9] = 0;

	display->egl.dpy =
		weston_platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR,
						display->display, NULL);
	assert(display->egl.dpy);

	ret = eglInitialize(display->egl.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	if (!eglGetConfigs(display->egl.dpy, NULL, 0, &count) || count < 1)
		assert(0);

	configs = calloc(count, sizeof *configs);
	assert(configs);

	ret = eglChooseConfig(display->egl.dpy, config_attribs,
			      configs, count, &n);
	assert(ret && n >= 1);

	for (i = 0; i < n; i++) {
		eglGetConfigAttrib(display->egl.dpy,
				   configs[i], EGL_BUFFER_SIZE, &size);
		if (window->buffer_size == size) {
			display->egl.conf = configs[i];
			break;
		}
	}
	free(configs);
	if (display->egl.conf == NULL) {
		fprintf(stderr, "did not find config with buffer size %d\n",
			window->buffer_size);
		exit(EXIT_FAILURE);
	}

	display->egl.ctx = eglCreateContext(display->egl.dpy,
					    display->egl.conf,
					    EGL_NO_CONTEXT, context_attribs);
	assert(display->egl.ctx);

	display->swap_buffers_with_damage = NULL;
	extensions = eglQueryString(display->egl.dpy, EGL_EXTENSIONS);
	if (extensions &&
	    weston_check_egl_extension(extensions, "EGL_EXT_buffer_age")) {
		for (i = 0; i < (int) ARRAY_LENGTH(swap_damage_ext_to_entrypoint); i++) {
			if (weston_check_egl_extension(extensions,
						       swap_damage_ext_to_entrypoint[i].extension)) {
				/* The EXTPROC is identical to the KHR one */
				display->swap_buffers_with_damage =
					(PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)
					eglGetProcAddress(swap_damage_ext_to_entrypoint[i].entrypoint);
				break;
			}
		}
	}

	if (display->swap_buffers_with_damage)
		printf("has EGL_EXT_buffer_age and %s\n", swap_damage_ext_to_entrypoint[i].extension);

}

static void
fini_egl(struct display *display)
{
	eglTerminate(display->egl.dpy);
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
		fprintf(stderr, "Error: compiling %s: %*s\n",
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
	GLuint program;
	GLint status;

	frag = create_shader(window, frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);

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
	window->gl.tex = 2;
	window->gl.position = 3;

	glBindAttribLocation(program, window->gl.pos, "pos");
	glBindAttribLocation(program, window->gl.col, "color");
	glBindAttribLocation(program, window->gl.tex, "tex_pos");
	glBindAttribLocation(program, window->gl.position, "rectangle_pos");
	glLinkProgram(program);

	window->gl.rotation_uniform =
		glGetUniformLocation(program, "rotation");

	if_triangle_uniform = glGetUniformLocation(program, "if_triangle");
	texture_uniform = glGetUniformLocation(program, "texture");
    glUniform1i(texture_uniform, 0);

    glGenFramebuffers(1, &g_fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo_id);

    glGenTextures(1, &g_tex_id);
	//glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		250, 250, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    // Attach texture buffer to COLOR buffer 0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, g_tex_id, 0);

    // Check everything
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("Framebuffer Object not set up\n");
    }


    glBindFramebuffer(GL_FRAMEBUFFER, 0);


}

static void
handle_surface_configure(void *data, struct zxdg_surface_v6 *surface,
			 uint32_t serial)
{
	struct window *window = data;

	zxdg_surface_v6_ack_configure(surface, serial);

	window->wait_for_configure = false;
}

static const struct zxdg_surface_v6_listener xdg_surface_listener = {
	handle_surface_configure
};

static void
handle_toplevel_configure(void *data, struct zxdg_toplevel_v6 *toplevel,
			  int32_t width, int32_t height,
			  struct wl_array *states)
{
	struct window *window = data;
	uint32_t *p;

	window->fullscreen = 0;
	window->maximized = 0;
	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN:
			window->fullscreen = 1;
			break;
		case ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED:
			window->maximized = 1;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!window->fullscreen && !window->maximized) {
			window->window_size.width = width;
			window->window_size.height = height;
		}
		window->geometry.width = width;
		window->geometry.height = height;
	} else if (!window->fullscreen && !window->maximized) {
		window->geometry = window->window_size;
	}

	if (window->native)
		wl_egl_window_resize(window->native,
				     window->geometry.width,
				     window->geometry.height, 0, 0);
}

static void
handle_toplevel_close(void *data, struct zxdg_toplevel_v6 *xdg_toplevel)
{
	running = 0;
}

static const struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
};

static void
ias_handle_ping(void *data, struct ias_surface *ias_surface,
	    uint32_t serial)
{
	ias_surface_pong(ias_surface, serial);
}

static void
ias_handle_configure(void *data, struct ias_surface *ias_surface,
		 int32_t width, int32_t height)
{
	struct window *window = data;

	if (window->native)
		wl_egl_window_resize(window->native, width, height, 0, 0);

	window->geometry.width = width;
	window->geometry.height = height;

	if (!window->fullscreen)
		window->window_size = window->geometry;
}

static struct ias_surface_listener ias_surface_listener = {
	ias_handle_ping,
	ias_handle_configure,
};

static void
handle_ivi_surface_configure(void *data, struct ivi_surface *ivi_surface,
                             int32_t width, int32_t height)
{
	struct window *window = data;

	wl_egl_window_resize(window->native, width, height, 0, 0);

	window->geometry.width = width;
	window->geometry.height = height;

	if (!window->fullscreen)
		window->window_size = window->geometry;
}

static const struct ivi_surface_listener ivi_surface_listener = {
	handle_ivi_surface_configure,
};

static void
create_xdg_surface(struct window *window, struct display *display)
{
	window->xdg_surface = zxdg_shell_v6_get_xdg_surface(display->shell,
							    window->surface);
	zxdg_surface_v6_add_listener(window->xdg_surface,
				     &xdg_surface_listener, window);

	window->xdg_toplevel =
		zxdg_surface_v6_get_toplevel(window->xdg_surface);
	zxdg_toplevel_v6_add_listener(window->xdg_toplevel,
				      &xdg_toplevel_listener, window);

	zxdg_toplevel_v6_set_title(window->xdg_toplevel, "simple-egl");

	window->wait_for_configure = true;
	wl_surface_commit(window->surface);
}

static void
create_ivi_surface(struct window *window, struct display *display)
{
	uint32_t id_ivisurf = IVI_SURFACE_ID + (uint32_t)getpid();
	window->ivi_surface =
		ivi_application_surface_create(display->ivi_application,
					       id_ivisurf, window->surface);

	if (window->ivi_surface == NULL) {
		fprintf(stderr, "Failed to create ivi_client_surface\n");
		abort();
	}

	ivi_surface_add_listener(window->ivi_surface,
				 &ivi_surface_listener, window);
}

static void
create_ias_surface(struct window *window, struct display *display)
{
	window->shell_surface = ias_shell_get_ias_surface(display->ias_shell,
			window->surface, "simple-egl");
	ias_surface_add_listener(window->shell_surface,
			&ias_surface_listener, window);
	window->shell_surface2 = ias_shell_get_ias_surface(display->ias_shell,
			window->surface2, "simple-egl2");
	ias_surface_add_listener(window->shell_surface2,
			&ias_surface_listener, window);

    if (window->fullscreen) {
 	   ias_surface_set_fullscreen(window->shell_surface,
    		   get_default_output(display)->output);
    } else {
		ias_surface_unset_fullscreen(display->window->shell_surface, 250, 250);
		ias_shell_set_zorder(display->ias_shell,
			window->shell_surface, 0);
		ias_surface_unset_fullscreen(window->shell_surface2, 250, 250);
		ias_shell_set_zorder(display->ias_shell,
			window->shell_surface2, 0);
	}
}

static void
create_surface(struct window *window)
{
	struct display *display = window->display;
	EGLBoolean ret;

	window->surface = wl_compositor_create_surface(display->compositor);
	window->surface2 = wl_compositor_create_surface(display->compositor);

	window->native =
		wl_egl_window_create(window->surface,
				     window->geometry.width,
				     window->geometry.height);
	window->egl_surface =
		weston_platform_create_egl_surface(display->egl.dpy,
						   display->egl.conf,
						   window->native, NULL);

	window->native2 =
		wl_egl_window_create(window->surface2,
				     window->window_size.width,
				     window->window_size.height);
	window->egl_surface2 =
		weston_platform_create_egl_surface(display->egl.dpy,
				       display->egl.conf,
				       window->native2, NULL);

	if (display->shell) {
		create_xdg_surface(window, display);
	} else if (display->ivi_application ) {
		create_ivi_surface(window, display);
	} else if (display->ias_shell) {
		create_ias_surface(window, display);
	} else {
		assert(0);
	}

	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
			     window->egl_surface, window->display->egl.ctx);
	assert(ret == EGL_TRUE);

	if (!window->frame_sync)
		eglSwapInterval(display->egl.dpy, 0);

	if (!display->shell)
		return;

	if (window->fullscreen)
		zxdg_toplevel_v6_set_fullscreen(window->xdg_toplevel, NULL);
}

static void
destroy_surface(struct window *window)
{
	/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
	 * on eglReleaseThread(). */
	eglMakeCurrent(window->display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);

	weston_platform_destroy_egl_surface(window->display->egl.dpy,
					    window->egl_surface);
	wl_egl_window_destroy(window->native);
	weston_platform_destroy_egl_surface(window->display->egl.dpy,
					    window->egl_surface2);
	wl_egl_window_destroy(window->native2);

	if (window->xdg_toplevel)
		zxdg_toplevel_v6_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		zxdg_surface_v6_destroy(window->xdg_surface);
	if (window->display->ivi_application)
		ivi_surface_destroy(window->ivi_surface);
	if (window->display->ias_shell) {
		ias_surface_destroy(window->shell_surface);
		ias_surface_destroy(window->shell_surface2);
	}
	wl_surface_destroy(window->surface);
	wl_surface_destroy(window->surface2);

	if (window->callback)
		wl_callback_destroy(window->callback);
}

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct display *display = window->display;
	EGLBoolean ret;
	static const GLfloat verts[3][2] = {
		{ -0.5, -0.5 },
		{  0.5, -0.5 },
		{  0,    0.5 }
	};
	static const GLfloat colors[3][3] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 }
	};
	GLfloat angle;
	GLfloat rotation[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	static const GLfloat rect_pos[4][2] = {
        {-1.0,  1.0},
        {-1.0, -1.0},
        { 1.0, -1.0},
        { 1.0,  1.0}
    };

	static const GLfloat rect_tex[4][2] = {
        {0.0, 1.0},
        {0.0, 0.0},
        {1.0, 0.0},
        {1.0, 1.0}
    };

	static const uint32_t speed_div = 5, benchmark_interval = 5;
	struct wl_region *region;
	EGLint rect[4];
	EGLint buffer_age = 0;
	struct timeval tv;

	assert(window->callback == callback);
	window->callback = NULL;

	if (callback)
		wl_callback_destroy(callback);

	gettimeofday(&tv, NULL);
	time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (window->frames == 0)
		window->benchmark_time = time;
	if (time - window->benchmark_time > (benchmark_interval * 1000)) {
		printf("%d frames in %d seconds: %f fps\n",
		       window->frames,
		       benchmark_interval,
		       (float) window->frames / benchmark_interval);
		window->benchmark_time = time;
		window->frames = 0;
	}

	angle = (time / speed_div) % 360 * M_PI / 180.0;
	rotation[0][0] =  cos(angle);
	rotation[0][2] =  sin(angle);
	rotation[2][0] = -sin(angle);
	rotation[2][2] =  cos(angle);

	if (display->swap_buffers_with_damage)
		eglQuerySurface(display->egl.dpy, window->egl_surface,
				EGL_BUFFER_AGE_EXT, &buffer_age);
	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
			     window->egl_surface, window->display->egl.ctx);
	assert(ret == EGL_TRUE);

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo_id);

    glUniform1i(if_triangle_uniform, 1);

	glViewport(0, 0, window->geometry.width, window->geometry.height);

	glUniformMatrix4fv(window->gl.rotation_uniform, 1, GL_FALSE,
			   (GLfloat *) rotation);

	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(window->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(window->gl.col, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.col);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.col);

	usleep(window->delay);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUniform1i(if_triangle_uniform, 0);

	glVertexAttribPointer(window->gl.position, 2, GL_FLOAT, GL_FALSE,  2 * sizeof(GLfloat), &rect_pos[0]);
	glVertexAttribPointer(window->gl.tex, 2, GL_FLOAT, GL_FALSE,  2 * sizeof(GLfloat), &rect_tex[0]);
	glEnableVertexAttribArray(window->gl.position);
	glEnableVertexAttribArray(window->gl.tex);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glDisableVertexAttribArray(window->gl.position);
	glDisableVertexAttribArray(window->gl.tex);

	if (window->opaque || window->fullscreen) {
		region = wl_compositor_create_region(window->display->compositor);
		wl_region_add(region, 0, 0,
			      window->geometry.width,
			      window->geometry.height);
		wl_surface_set_opaque_region(window->surface, region);
		wl_surface_set_opaque_region(window->surface2, region);
		wl_region_destroy(region);
	} else {
		wl_surface_set_opaque_region(window->surface, NULL);
		wl_surface_set_opaque_region(window->surface2, NULL);
	}

	if (display->swap_buffers_with_damage && buffer_age > 0) {
		rect[0] = window->geometry.width / 4 - 1;
		rect[1] = window->geometry.height / 4 - 1;
		rect[2] = window->geometry.width / 2 + 2;
		rect[3] = window->geometry.height / 2 + 2;
		display->swap_buffers_with_damage(display->egl.dpy,
						  window->egl_surface,
						  rect, 1);
	} else {
		eglSwapBuffers(display->egl.dpy, window->egl_surface);
	}
	window->frames++;

	/* surface 2 */
	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface2,
			     window->egl_surface2, window->display->egl.ctx);
	assert(ret == EGL_TRUE);

	glUniform1i(if_triangle_uniform, 0);

	glVertexAttribPointer(window->gl.position, 2, GL_FLOAT, GL_FALSE,  2 * sizeof(GLfloat), &rect_pos[0]);
	glVertexAttribPointer(window->gl.tex, 2, GL_FLOAT, GL_FALSE,  2 * sizeof(GLfloat), &rect_tex[0]);
	glEnableVertexAttribArray(window->gl.position);
	glEnableVertexAttribArray(window->gl.tex);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glDisableVertexAttribArray(window->gl.position);
	glDisableVertexAttribArray(window->gl.tex);

	if (display->swap_buffers_with_damage && buffer_age > 0) {
		rect[0] = window->geometry.width / 4 - 1;
		rect[1] = window->geometry.height / 4 - 1;
		rect[2] = window->geometry.width / 2 + 2;
		rect[3] = window->geometry.height / 2 + 2;
		display->swap_buffers_with_damage(display->egl.dpy,
						  window->egl_surface2,
						  rect, 1);
	} else {
		eglSwapBuffers(display->egl.dpy, window->egl_surface2);
	}
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface,
		     wl_fixed_t sx, wl_fixed_t sy)
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
		if (!buffer)
			return;
		wl_pointer_set_cursor(pointer, serial,
				      display->cursor_surface,
				      image->hotspot_x,
				      image->hotspot_y);
		wl_surface_attach(display->cursor_surface, buffer, 0, 0);
		wl_surface_damage(display->cursor_surface, 0, 0,
				  image->width, image->height);
		wl_surface_commit(display->cursor_surface);
	}
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

	if (!display->window->xdg_toplevel)
		return;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
		zxdg_toplevel_v6_move(display->window->xdg_toplevel,
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
touch_handle_down(void *data, struct wl_touch *wl_touch,
		  uint32_t serial, uint32_t time, struct wl_surface *surface,
		  int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct display *d = (struct display *)data;

	if (!d->shell)
		return;

	zxdg_toplevel_v6_move(d->window->xdg_toplevel, d->seat, serial);
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
		uint32_t serial, uint32_t time, int32_t id)
{
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
		    uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
}

static void
touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
}

static void
touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
}

static const struct wl_touch_listener touch_listener = {
	touch_handle_down,
	touch_handle_up,
	touch_handle_motion,
	touch_handle_frame,
	touch_handle_cancel,
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

	if (key == KEY_F11 && state) {
		if (d->shell) {
			if (d->window->fullscreen)
				zxdg_toplevel_v6_unset_fullscreen(d->window->xdg_toplevel);
			else
				zxdg_toplevel_v6_set_fullscreen(d->window->xdg_toplevel,
							NULL);
		} else if (d->ias_shell) {
			if (d->window->fullscreen) {
				ias_surface_unset_fullscreen(d->window->shell_surface,
						250, 250);
				ias_shell_set_zorder(d->ias_shell,
						d->window->shell_surface, 0);
				d->window->fullscreen = 0;
			} else {
				ias_surface_set_fullscreen(d->window->shell_surface,
						get_default_output(d)->output);
				d->window->fullscreen = 1;
			}
		}
	} else if (key == KEY_ESC && state) {
		running = 0;
	}
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

	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !d->touch) {
		d->touch = wl_seat_get_touch(seat);
		wl_touch_set_user_data(d->touch, d);
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
xdg_shell_ping(void *data, struct zxdg_shell_v6 *shell, uint32_t serial)
{
	zxdg_shell_v6_pong(shell, serial);
}

static const struct zxdg_shell_v6_listener xdg_shell_listener = {
	xdg_shell_ping,
};

static void
display_add_output(struct display *d, uint32_t id)
{
	struct output *output;

	output = malloc(sizeof *output);
	if (output == NULL)
		return;

	memset(output, 0, sizeof *output);
	output->display = d;
	output->output =
		wl_registry_bind(d->registry, id, &wl_output_interface, 1);
	wl_list_insert(d->output_list.prev, &output->link);
}

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface,
					 MIN(version, 4));
	} else if (strcmp(interface, "zxdg_shell_v6") == 0) {
		if (!d->ias_shell) {
			d->shell = wl_registry_bind(registry, name,
					    &zxdg_shell_v6_interface, 1);
			zxdg_shell_v6_add_listener(d->shell, &xdg_shell_listener, d);
		}
	} else if (strcmp(interface, "ias_shell") == 0) {
		if (!d->shell) {
			d->ias_shell = wl_registry_bind(registry, name,
					&ias_shell_interface, 1);
		}
	} else if (strcmp(interface, "wl_seat") == 0) {
		d->seat = wl_registry_bind(registry, name,
					   &wl_seat_interface, 1);
		wl_seat_add_listener(d->seat, &seat_listener, d);
	} else if (strcmp(interface, "wl_output") == 0) {
		display_add_output(d, name);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry, name,
					  &wl_shm_interface, 1);
		d->cursor_theme = wl_cursor_theme_load(NULL, 32, d->shm);
		if (!d->cursor_theme) {
			fprintf(stderr, "unable to load default theme\n");
			return;
		}
		d->default_cursor =
			wl_cursor_theme_get_cursor(d->cursor_theme, "left_ptr");
		if (!d->default_cursor) {
			fprintf(stderr, "unable to load default left pointer\n");
			// TODO: abort ?
		}
	} else if (strcmp(interface, "ivi_application") == 0) {
		d->ivi_application =
			wl_registry_bind(registry, name,
					 &ivi_application_interface, 1);
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
	running = 0;
}

static void
usage(int error_code)
{
	fprintf(stderr, "Usage: simple-egl [OPTIONS]\n\n"
		"  -d <us>\tBuffer swap delay in microseconds\n"
		"  -f\tRun in fullscreen mode\n"
		"  -o\tCreate an opaque surface\n"
		"  -s\tUse a 16 bpp EGL config\n"
		"  -b\tDon't sync to compositor redraw (eglSwapInterval 0)\n"
		"  -h\tThis help text\n\n");

	exit(error_code);
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display display = { 0 };
	struct window  window  = { 0 };
	int i, ret = 0;
	struct output *iter, *next;

	window.display = &display;
	display.window = &window;
	window.geometry.width  = 250;
	window.geometry.height = 250;
	window.window_size = window.geometry;
	window.buffer_size = 32;
	window.frame_sync = 1;
	window.delay = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp("-d", argv[i]) == 0 && i+1 < argc)
			window.delay = atoi(argv[++i]);
		else if (strcmp("-f", argv[i]) == 0)
			window.fullscreen = 1;
		else if (strcmp("-o", argv[i]) == 0)
			window.opaque = 1;
		else if (strcmp("-out", argv[i]) == 0)
			window.output = atoi(argv[++i]);
		else if (strcmp("-s", argv[i]) == 0)
			window.buffer_size = 16;
		else if (strcmp("-b", argv[i]) == 0)
			window.frame_sync = 0;
		else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS);
		else
			usage(EXIT_FAILURE);
	}

	display.display = wl_display_connect(NULL);
	assert(display.display);
	wl_list_init(&display.output_list);

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry,
				 &registry_listener, &display);

	wl_display_roundtrip(display.display);

	init_egl(&display, &window);
	create_surface(&window);
	init_gl(&window);

	display.cursor_surface =
		wl_compositor_create_surface(display.compositor);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	/* The mainloop here is a little subtle.  Redrawing will cause
	 * EGL to read events so we can just call
	 * wl_display_dispatch_pending() to handle any events that got
	 * queued up as a side effect. */
	while (running && ret != -1) {
		if (window.wait_for_configure) {
			wl_display_dispatch(display.display);
		} else {
			wl_display_dispatch_pending(display.display);
			redraw(&window, NULL, 0);
		}
	}

	fprintf(stderr, "simple-egl exiting\n");

	destroy_surface(&window);
	fini_egl(&display);

	wl_surface_destroy(display.cursor_surface);
	if (display.cursor_theme)
		wl_cursor_theme_destroy(display.cursor_theme);

	if (display.shell)
		zxdg_shell_v6_destroy(display.shell);

	if (display.ivi_application)
		ivi_application_destroy(display.ivi_application);

	if (display.ias_shell) {
		ias_shell_destroy(display.ias_shell);
	}

	if (display.compositor)
		wl_compositor_destroy(display.compositor);

	wl_registry_destroy(display.registry);

	wl_list_for_each_safe(iter, next, &display.output_list, link) {
		wl_list_remove(&iter->link);
		free(iter);
	}

	wl_display_flush(display.display);
	wl_display_disconnect(display.display);

	return 0;
}
