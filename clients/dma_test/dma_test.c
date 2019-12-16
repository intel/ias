/*
 *-----------------------------------------------------------------------------
 * Filename: dma-test.c
 *-----------------------------------------------------------------------------
 *-----------------------------------------------------------------------------
 * Description:
 *   This is a demo program for showing dma-buffer way of getting frames from
 *   IPU driver, color converting them from UYVY/RGB888/RGB565 to ARGB and then
 *   displaying them on the screen using Wayland.
 *-----------------------------------------------------------------------------
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <linux/input.h>
#include <time.h>


#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <wayland-egl.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <linux/media.h>
#include "mediactl.h"
#include "v4l2subdev.h"
#include <pthread.h>

#include "ias-shell-client-protocol.h"
#include "ivi-application-client-protocol.h"
#include "wayland-drm-client-protocol.h"

/* For GEM */
#include <libdrm/intel_bufmgr.h>
#include <xf86drm.h>

#define BATCH_SIZE 0x80000
#define TARGET_NUM_SECONDS 5
#define BUFFER_COUNT 4

/* UYVY */
static const char *frag_shader_text_UYVY  =
  "uniform sampler2D u_texture_top;"\
  "uniform sampler2D u_texture_bottom;"\
  "uniform bool swap_rb;"\
  "uniform bool interlaced;"\
  "varying highp vec2 texcoord;"\
  "varying mediump vec2 texsize;"\
  "void main(void) {"\
  "  mediump float y, u, v, tmp;"\
  "  mediump vec4 resultcolor;"\
  "  mediump vec4 raw;"\
  "  if (interlaced && fract(texcoord.y * texsize.y) < 0.5) {"\
  "    raw = texture2D(u_texture_bottom, texcoord);"\
  "  } else {"\
  "    raw = texture2D(u_texture_top, texcoord);"\
  "  }"\
  "  if (fract(texcoord.x * texsize.x) < 0.5)"\
  "    raw.a = raw.g;"\
  "  u = raw.b-0.5;"\
  "  v = raw.r-0.5;"\
  "  if (swap_rb) {"\
  "    tmp = u;"\
  "    u = v;"\
  "    v = tmp;"\
  "  }"\
  "  y = 1.1643*(raw.a-0.0625);"\
  "  resultcolor.r = (y+1.5958*(v));"\
  "  resultcolor.g = (y-0.39173*(u)-0.81290*(v));"\
  "  resultcolor.b = (y+2.017*(u));"\
  "  resultcolor.a = 1.0;"\
  "  gl_FragColor=resultcolor;"\
  "}";

/* YUYV */
static const char *frag_shader_text_YUYV =
  "uniform sampler2D u_texture_top;"\
  "uniform sampler2D u_texture_bottom;"\
  "uniform bool swap_rb;"\
  "uniform bool interlaced;"\
  "varying highp vec2 texcoord;"\
  "varying mediump vec2 texsize;"\
  "void main(void) {"\
  "  mediump float y, u, v, tmp;"\
  "  mediump vec4 resultcolor;"\
  "  mediump vec4 raw;"\
  "  if((fract(texcoord.y * texsize.y) < 0.5) && interlaced) {"\
  "    raw = texture2D(u_texture_bottom, texcoord);"\
  "  } else {"\
  "    raw = texture2D(u_texture_top, texcoord);"\
  "  }"\
  "  if (fract(texcoord.x * texsize.x) < 0.5)"\
  "    raw.b = raw.r;"\
  "  u = raw.g-0.5;"\
  "  v = raw.a-0.5;"\
  "  y = 1.1643*(raw.b-0.0625);"\
  "  resultcolor.r = (y+1.5958*(v));"\
  "  resultcolor.g = (y-0.39173*(u)-0.81290*(v));"\
  "  resultcolor.b = (y+2.017*(u));"\
  "  resultcolor.a = 1.0;"\
  "  gl_FragColor=resultcolor;"\
  "}";

/* RGB565 and RGB888 */
static const char *frag_shader_text_RGB = 
  "uniform sampler2D u_texture_top;"\
  "uniform sampler2D u_texture_bottom;"\
  "uniform bool rgb565;"\
  "uniform bool swap_rb;"\
  "uniform bool interlaced;"\
  "varying highp vec2 texcoord;"\
  "varying mediump vec2 texsize;"\
  "void main(void) {"\
  "  highp vec4 resultcolor;"\
  "  highp vec4 raw;"\
  "  if (interlaced && fract(texcoord.y * texsize.y) < 0.5)"\
  "     raw = texture2D(u_texture_bottom, texcoord);"\
  "  else"\
  "     raw = texture2D(u_texture_top, texcoord);"\
  "  if(rgb565) raw *= vec4(255.0/32.0, 255.0/64.0, 255.0/32.0, 1.0);"\
  "  if (swap_rb) resultcolor.rgb = raw.bgr;"\
  "  else resultcolor.rgb = raw.rgb;"\
  "  resultcolor.a = 1.0;"\
  "  gl_FragColor = resultcolor;"\
  "}";

/**
 * @brief vertex shader for displaying the texture
 */
static const char *vert_shader_text =
  "varying  highp vec2 texcoord; "\
  "varying  mediump vec2 texsize; "\
  "attribute vec4 pos; "\
  "attribute highp vec2 itexcoord; "\
  "uniform mat4 modelviewProjection; "\
  "uniform mediump vec2 u_texsize; "\
  "void main(void) "\
  "{ "\
  " texcoord = itexcoord; "\
  " texsize = u_texsize; "\
  " gl_Position = modelviewProjection * pos; "\
  "}";


static struct timeval *curr_time, *prev_time;

struct window;

PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

PFNGLPROGRAMBINARYOESPROC glProgramBinaryOES = NULL;
PFNGLGETPROGRAMBINARYOESPROC glGetProgramBinaryOES = NULL;

#define ARRAY_SIZE(a)   	(sizeof(a)/sizeof((a)[0]))
#define OPT_STRIDE              263
#define OPT_BUFFER_SIZE         268

#define _ISP_MODE_PREVIEW       0x8000
#define _ISP_MODE_STILL         0x2000
#define _ISP_MODE_VIDEO         0x4000

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define ERRSTR strerror(errno)

#define BYE_ON(cond, ...) \
	do { \
		if (cond) { \
			int errsv = errno; \
			fprintf(stderr, "ERROR(%s:%d) : ", \
					__FILE__, __LINE__); \
			errno = errsv; \
			fprintf(stderr,  __VA_ARGS__); \
			abort(); \
		} \
	} while(0)

static inline int warn(const char *file, int line, const char *fmt, ...)
{
	int errsv = errno;
	va_list va;
	va_start(va, fmt);
	fprintf(stderr, "WARN(%s:%d): ", file, line);
	vfprintf(stderr, fmt, va);
	va_end(va);
	errno = errsv;
	return 1;
}

#define WARN_ON(cond, ...) \
	((cond) ? warn(__FILE__, __LINE__, __VA_ARGS__) : 0)

enum render_type {
	RENDER_TYPE_WL,
	RENDER_TYPE_GL,
	RENDER_TYPE_GL_DMA,
};

struct setup {
	char video[32];
	unsigned int iw, ih, original_iw;
	unsigned int ow, oh;
	unsigned int use_wh : 1;
	unsigned int in_fourcc;
	unsigned int buffer_count;
	unsigned int port;
	unsigned int fullscreen;
	unsigned int exporter;
	unsigned int interlaced;
	enum render_type render_type;
	unsigned int frames_count;
	unsigned int loops_count;
	unsigned int skip_media_controller_setup;
	unsigned int mplane_type;
};

struct v4l2_device {
	const char *devname;
	int fd;
	struct v4l2_pix_format format;
	int is_exporter;
	enum v4l2_buf_type type;
	unsigned char num_planes;
	struct v4l2_plane_pix_format plane_fmt[VIDEO_MAX_PLANES];
	void *pattern[VIDEO_MAX_PLANES];
	unsigned int patternsize[VIDEO_MAX_PLANES];
};

enum field_type {
	FIELD_TYPE_NONE,
	FIELD_TYPE_TOP,
	FIELD_TYPE_BOTTOM
};

static struct {
	enum v4l2_buf_type type;
	bool supported;
	const char *name;
	const char *string;
} buf_types[] = {
	{ V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, "Video capture mplanes", "capture-mplane", },
	{ V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 1, "Video output", "output-mplane", },
	{ V4L2_BUF_TYPE_VIDEO_CAPTURE, 1, "Video capture", "capture", },
	{ V4L2_BUF_TYPE_VIDEO_OUTPUT, 1, "Video output mplanes", "output", },
	{ V4L2_BUF_TYPE_VIDEO_OVERLAY, 0, "Video overlay", "overlay" },
};

struct buffer {
	drm_intel_bo *bo;
	unsigned int index;
	unsigned int fb_handle;
	enum field_type field_type;
	int dbuf_fd;
	uint32_t flink_name;
	struct wl_buffer *buf;
	EGLImageKHR khrImage;
};

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
	struct wl_shell *wl_shell;
	struct ivi_application *ivi_application;
	struct wl_drm *wl_drm;
	struct window *window;
	struct wl_list output_list;
	int	   fd;
	dri_bufmgr *bufmgr;
	struct buffer *buffers;
	struct v4l2_device *v4l2;
	struct setup *s;
	struct {
		EGLDisplay dpy;
		EGLContext ctx;
		EGLConfig conf;
	} egl;
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;
	struct wl_surface *surface;
	void *shell_surface;
	struct ivi_surface *ivi_surface;
	struct wl_egl_window *native;
	EGLSurface egl_surface;
	struct wl_callback *callback;
	int fullscreen, opaque, configured, output;
	int print_fps, frame_count;
	struct {
		GLuint fbo;
		GLuint color_rbo;

		GLuint modelview_uniform;
		GLuint gl_texture_size;
		GLuint gl_texture[2];

		GLuint tex_top;
		GLuint tex_bottom;
		GLuint rgb565;
		GLuint swap_rb;
		GLuint interlaced;

		GLuint pos;
		GLuint col;
		GLuint attr_tex;

		GLuint program;

		GLfloat hmi_vtx[12u];          //!< hold coordinates of vertices for texture
		GLfloat hmi_tex[8u];           //!< hold indices of vertices for texture
		GLubyte hmi_ind[6u];           //!< hold coordinates for texture sample (conversion to rgba)

		GLfloat model_view[16u];
	} gl;
};


struct time_measurements
{
	struct timespec app_start_time;
	struct timespec before_md_init_time;
	struct timespec md_init_time;
	struct timespec weston_init_time;
	struct timespec v4l2_init_time;
	struct timespec rendering_init_time;
	struct timespec streamon_time;
	struct timespec first_frame_time;
	struct timespec first_frame_rendered_time;
} time_measurements;

static void signal_int(int signum);

static void video_set_buf_type(struct v4l2_device *dev, enum v4l2_buf_type type)
{
	dev->type = type;
}

static int v4l2_buf_type_from_string(const char *str)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(buf_types); i++) {
		if (!buf_types[i].supported)
			continue;

		if (strcmp(buf_types[i].string, str))
			continue;

		return buf_types[i].type;
	}

	return -1;
}

static bool video_is_mplane(struct v4l2_device *dev)
{
	return (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
			dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
}

double clock_diff(struct timespec startTime, struct timespec endTime)
{
	struct timespec diff;
	if ((endTime.tv_nsec - startTime.tv_nsec) >= 0)
	{
		diff.tv_sec = endTime.tv_sec - startTime.tv_sec;
		diff.tv_nsec = endTime.tv_nsec - startTime.tv_nsec;
	} else {
		diff.tv_sec = endTime.tv_sec - startTime.tv_sec - 1;
		diff.tv_nsec = 1000000000 + endTime.tv_nsec - startTime.tv_nsec;
	}
	return diff.tv_sec * 1000 + (double)(diff.tv_nsec) / 1000000;;
}

void print_time_measurement(const char* name, struct timespec start, struct timespec end)
{
	double diff;
	double ts;
	diff = clock_diff(start, end);
	ts = end.tv_sec + (double)end.tv_nsec/1000000000;

	printf("%-25s | %6.03f s | %6.02f ms\n", name, ts, diff);
}

void print_time_measurements()
{
	printf("DMA TEST TIME STATS\n");
	printf("%-25s | %-10s | %-6s\n", "Tracepoint", "System ts", "Time since app start");

	print_time_measurement("App start", time_measurements.app_start_time, time_measurements.app_start_time);
	print_time_measurement("Media ctl setup", time_measurements.app_start_time, time_measurements.md_init_time);
	print_time_measurement("V4L2 setup", time_measurements.app_start_time, time_measurements.v4l2_init_time);
	print_time_measurement("IPU streamon", time_measurements.app_start_time, time_measurements.streamon_time);
	print_time_measurement("Weston ready", time_measurements.app_start_time, time_measurements.weston_init_time);
	print_time_measurement("EGL/GL setup", time_measurements.app_start_time, time_measurements.rendering_init_time);
	print_time_measurement("First frame received", time_measurements.app_start_time, time_measurements.first_frame_time);
	print_time_measurement("First frame displayed", time_measurements.app_start_time, time_measurements.first_frame_rendered_time);
}

int first_frame_received = 0;
int first_frame_rendered = 0;
#define GET_TS(t) clock_gettime(CLOCK_MONOTONIC, &t)

static int running = 1;
static int error_recovery = 0;
struct buffer *cur_top_buffer = NULL;
struct buffer *cur_bottom_buffer = NULL;

static struct output *
get_default_output(struct display *display)
{
	struct output *iter;
	int counter = 0;
	wl_list_for_each(iter, &display->output_list, link) {
		if(counter++ == display->window->output)
			return iter;
	}

	/* Unreachable, but avoids compiler warning */
	return NULL;
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

	window->geometry.width = width;
	window->geometry.height = height;

	if (!window->fullscreen)
		window->window_size = window->geometry;
}


static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static struct wl_shell_surface_listener wl_shell_surface_listener = {
	handle_ping,
	handle_configure,
	handle_popup_done
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
ivi_handle_configure(void *data, struct ivi_surface *ivi_surface,
		     int32_t width, int32_t height) {
	struct window *window = data;

	wl_egl_window_resize(window->native, width, height, 0, 0);

	window->geometry.width = width;
	window->geometry.height = height;

	if (!window->fullscreen)
		window->window_size = window->geometry;
}

static const struct ivi_surface_listener ivi_surface_listener = {
	ivi_handle_configure,
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
	struct display *display = window->display;

	window->fullscreen = fullscreen;
	window->configured = 0;

	if (fullscreen) {
		if (display->ias_shell) {
			ias_surface_set_fullscreen(window->shell_surface,
					get_default_output(display)->output);
		}
		if (display->wl_shell) {
			wl_shell_surface_set_fullscreen(window->shell_surface,
					WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT,
					0, NULL);
		}
	} else {
		if (display->ias_shell) {
			ias_surface_unset_fullscreen(window->shell_surface, window->window_size.width, window->window_size.height);
			ias_shell_set_zorder(display->ias_shell,
					window->shell_surface, 0);
		}
		if (display->wl_shell) {
			wl_shell_surface_set_toplevel(window->shell_surface);
		}
		handle_configure(window, window->shell_surface, 0,
				window->window_size.width,
				window->window_size.height);

	}

	callback = wl_display_sync(window->display->display);
	wl_callback_add_listener(callback, &configure_callback_listener,
			window);
}

static void
destroy_surface(struct window *window)
{
	struct display *display = window->display;

	if (display->s->render_type != RENDER_TYPE_WL) {
		/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
		 * on eglReleaseThread(). */
		eglMakeCurrent(window->display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
			       EGL_NO_CONTEXT);

		eglDestroySurface(window->display->egl.dpy, window->egl_surface);
		wl_egl_window_destroy(window->native);
	}

	if (display->ias_shell) {
		ias_surface_destroy(window->shell_surface);
	}
	if (display->wl_shell) {
		wl_shell_surface_destroy(window->shell_surface);
	}

	wl_surface_destroy(window->surface);

	if (window->callback)
		wl_callback_destroy(window->callback);
}

static const struct wl_callback_listener frame_listener;

static void
update_fps(struct window *window)
{
	float time_diff_secs;
	struct timeval time_diff;
	struct timeval *tmp;

	if (window->print_fps) {
		window->frame_count++;

		gettimeofday(curr_time, NULL);

		timersub(curr_time, prev_time, &time_diff);
		time_diff_secs = (time_diff.tv_sec * 1000 + time_diff.tv_usec / 1000) / 1000;

		if (time_diff_secs >= TARGET_NUM_SECONDS) {
			fprintf(stdout, "Rendered %d frames in %6.3f seconds = %6.3f FPS\n",
				window->frame_count, time_diff_secs, window->frame_count / time_diff_secs);
			fflush(stdout);

			window->frame_count = 0;

			tmp = prev_time;
			prev_time = curr_time;
			curr_time = tmp;
		}
	}
}

static void v4l2_queue_buffer(struct v4l2_device *dev, const struct buffer *buffer)
{
	struct v4l2_buffer buf;
	int ret;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];

	memset(&planes, 0, sizeof planes);
	memset(&buf, 0, sizeof buf);

	if (video_is_mplane(dev)) {
		buf.m.planes = planes;
		buf.length = dev->num_planes;
	}

	buf.type = dev->type;
	if(dev->is_exporter) {
		buf.memory = V4L2_MEMORY_MMAP;
	} else {
		buf.memory = V4L2_MEMORY_DMABUF;
		buf.m.fd = buffer->dbuf_fd;
	}
	buf.index = buffer->index;

	ret = ioctl(dev->fd, VIDIOC_QBUF, &buf);
	if (ret) {
		error_recovery = 1;
		signal_int(0);
	}
}

static struct buffer *v4l2_dequeue_buffer(struct v4l2_device *dev, struct buffer *buffers)
{
	struct v4l2_buffer buf;
	int ret;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];

	memset(&buf, 0, sizeof buf);
	memset(planes, 0, sizeof planes);
	buf.type = dev->type;
	buf.length = VIDEO_MAX_PLANES;
	buf.m.planes = planes;

	if(dev->is_exporter) {
		buf.memory = V4L2_MEMORY_MMAP;
	} else {
		buf.memory = V4L2_MEMORY_DMABUF;
	}
	ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf);
	if (ret)
		return NULL;

	if(buf.field == V4L2_FIELD_TOP) {
		buffers[buf.index].field_type = FIELD_TYPE_TOP;
	} else if(buf.field == V4L2_FIELD_BOTTOM) {
		buffers[buf.index].field_type = FIELD_TYPE_BOTTOM;
	} else {
		buffers[buf.index].field_type = FIELD_TYPE_NONE;
	}

	return &buffers[buf.index];
}

static void make_orth_matrix(GLfloat *data, GLfloat left, GLfloat right,
		GLfloat bottom, GLfloat top,
		GLfloat znear, GLfloat zfar)
{
	data[0] = 2.0/(right-left);
	data[5] = 2.0/(top-bottom);
	data[10] = -2.0/(zfar-znear);
	data[15] = 1.0;
	data[12] = (right+left)/(right-left);
	data[13] = (top+bottom)/(top-bottom);
	data[14] = (zfar+znear)/(zfar-znear);
}

static void make_matrix(GLfloat *data, GLfloat v)
{
	make_orth_matrix(data, -v, v, -v, v, -v, v);
}

static void redraw_wl_way(struct window *window, struct wl_buffer *buf, uint32_t time)
{
	wl_surface_attach(window->surface, buf, 0, 0);
	wl_surface_damage(window->surface, 0, 0, window->display->s->iw, window->display->s->ih);
	wl_surface_commit(window->surface);

	if (first_frame_received == 1 && first_frame_rendered == 0) {
		first_frame_rendered = 1;
		GET_TS(time_measurements.first_frame_rendered_time);
		print_time_measurements();
	}
}

static void redraw_egl_way(struct window *window, struct buffer *top_buf,
		struct buffer *bottom_buf, unsigned char *start_top, unsigned char *start_bottom)
{
	int width, height;
	glViewport(0, 0, window->geometry.width, window->geometry.height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.0, 0.0, 0.0, 0.0); // full transparency

	glActiveTexture(GL_TEXTURE0);
	width = window->display->s->iw;
	height = window->display->s->ih;
	if (window->display->s->in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16 ||
			window->display->s->in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
		width = window->display->s->iw/2;
	}

	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, window->gl.gl_texture[0]);
	if (window->display->s->render_type == RENDER_TYPE_GL_DMA) {
		glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, top_buf->khrImage);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
				GL_RGBA, GL_UNSIGNED_BYTE, start_top);
	}
	
	if (window->display->s->interlaced) {
		glActiveTexture(GL_TEXTURE1);

		glBindTexture(GL_TEXTURE_2D, window->gl.gl_texture[1]);
		if (window->display->s->render_type == RENDER_TYPE_GL_DMA) {
			glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, bottom_buf->khrImage);
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
					GL_RGBA, GL_UNSIGNED_BYTE, start_bottom);
		}
		glActiveTexture(GL_TEXTURE0);
	}

	glUseProgram(window->gl.program);

	glUniformMatrix4fv(window->gl.modelview_uniform, 1, GL_FALSE, window->gl.model_view);

	glVertexAttribPointer(window->gl.pos, 3, GL_FLOAT, GL_FALSE, 0, window->gl.hmi_vtx);
	glVertexAttribPointer(window->gl.attr_tex, 2, GL_FLOAT, GL_FALSE, 0,
			window->gl.hmi_tex);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.attr_tex);
	glDrawElements(GL_TRIANGLES, 2*3, GL_UNSIGNED_BYTE, window->gl.hmi_ind);
	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.attr_tex);
	glBindTexture(GL_TEXTURE_2D, 0);

	wl_surface_set_opaque_region(window->surface, NULL);
	eglSwapBuffers(window->display->egl.dpy, window->egl_surface);
	if (first_frame_received == 1 && first_frame_rendered == 0) {
		first_frame_rendered = 1;
		GET_TS(time_measurements.first_frame_rendered_time);
		print_time_measurements();
	}
}

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct buffer * buf_top = (cur_top_buffer)
		? cur_top_buffer
		: &(window->display->buffers[0]);

	struct buffer * buf_bottom = (cur_bottom_buffer)
		? cur_bottom_buffer
		: &(window->display->buffers[0]);

	unsigned char *start_top;
	unsigned char *start_bottom;

	start_top = (unsigned char *) buf_top->bo->virtual;
	start_bottom = (unsigned char *) buf_bottom->bo->virtual;

	if (callback)
		wl_callback_destroy(callback);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);

	update_fps(window);

	if (window->display->s->render_type == RENDER_TYPE_WL) {
		redraw_wl_way(window, buf_top->buf, time);
	} else {
		redraw_egl_way(window, buf_top, buf_bottom, start_top, start_bottom);
	}
}

static const struct wl_callback_listener frame_listener = {
	redraw
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
					&wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_shell") == 0) {
		if (!d->ias_shell && !d->ivi_application) {
			d->wl_shell = wl_registry_bind(registry, name,
					&wl_shell_interface, 1);
		}
	} else if (strcmp(interface, "ias_shell") == 0) {
		if (!d->wl_shell && !d->ivi_application) {
			d->ias_shell = wl_registry_bind(registry, name,
					&ias_shell_interface, 1);
		}
	} else if (strcmp(interface, "ivi_application") == 0) {
		if (!d->ias_shell && !d->wl_shell) {
			d->ivi_application = wl_registry_bind(registry, name,
						&ivi_application_interface, 1);
		}
	} else if (strcmp(interface, "wl_output") == 0) {
		display_add_output(d, name);
	} else if (!strcmp(interface, "wl_drm")) {
		d->wl_drm =
			wl_registry_bind(registry, name, &wl_drm_interface, 1);
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global
};

static void
signal_int(int signum)
{
	running = 0;
}

static int
init_gem(struct display *display)
{
	/* Init GEM */
	display->fd = drmOpen("i915", NULL);
	if (display->fd < 0)
		return -1;

	/* In case that drm will be opened before weston will do it, 
	 * master mode needs to be released otherwiser weston won't initialize
	 */
	drmDropMaster(display->fd);

	display->bufmgr = intel_bufmgr_gem_init(display->fd, BATCH_SIZE);

	if (display->bufmgr == NULL)
		return -1;

	return 0;
}

static void
destroy_gem(struct display *display)
{
	/* Free the GEM buffer */
	drm_intel_bufmgr_destroy(display->bufmgr);
	drmClose(display->fd);
}

static int
drm_buffer_to_prime(struct display *display, struct buffer *buffer, unsigned int size)
{
	int ret;
	buffer->bo = drm_intel_bo_gem_create_from_prime(display->bufmgr,
			buffer->dbuf_fd, (int) size);
	if(!buffer->bo) {
		printf("ERROR: Couldn't create from prime\n");
		return -1;
	}

	/* Do a mmap once */
	ret = drm_intel_gem_bo_map_gtt(buffer->bo);
	if(ret) {
		printf("ERROR: Couldn't map buffer->bo\n");
		return -1;
	}

	ret = drm_intel_bo_flink(buffer->bo, &buffer->flink_name);
	if (ret) {
		printf("ERROR: Couldn't flink buffer\n");
		return -1;
	}

	return 0;
}

static int
create_buffer(struct display *display, struct buffer *buffer, unsigned int size)
{
	int ret;

	buffer->bo =  drm_intel_bo_alloc_for_render(display->bufmgr,
			"display surface",
			size,
			0);

	if (buffer->bo == NULL)
		return -1;

	struct drm_prime_handle prime;
	memset(&prime, 0, sizeof prime);
	prime.handle = buffer->bo->handle;

	ret = ioctl(display->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime);
	if (WARN_ON(ret, "PRIME_HANDLE_TO_FD failed: %s\n", ERRSTR))
		return -1;
	buffer->dbuf_fd = prime.fd;

	ret = drm_intel_bo_flink(buffer->bo, &buffer->flink_name);
	if (ret) {
		printf("ERROR: Couldn't flink buffer\n");
		return -1;
	}

	/* Do a mmap once */
	ret = drm_intel_bo_map(buffer->bo, 1);
	if(ret) {
		printf("ERROR: Couldn't map buf->bo\n");
		return -1;
	}	/* Do a mmap once */


	return 0;
}

static void usage(char *name)
{
	fprintf(stderr, "usage: %s [-bFfhidMopSst]\n", name);

	fprintf(stderr, "\nCapture options:\n\n");
	fprintf(stderr, "\t-d <video-node>\tset video node (default: auto detect)\n");
	fprintf(stderr, "\t-I <width,height>\tset input resolution\n");
	fprintf(stderr, "\t-O <width,height>\tset output resolution\n");
	fprintf(stderr, "\t-i\tinterlace\n");
	fprintf(stderr, "\t-n\tport number (0 for HDMI, 4 for camera)\n");
	fprintf(stderr, "\t-p\tBuffer type (\"capture\", \"output\", \"capture-mplane\" or \"output-mplane\")\n");
	fprintf(stderr, "\nGeneric options:\n\n");
	fprintf(stderr, "\t-b buffer_count\tset number of buffers\n");
	fprintf(stderr, "\t-N frames_count\tnumber of frames to display (0 = no limit)\n");
	fprintf(stderr, "\t-l loops\tnumber of loops to be run (0 = no limit)\n");
	fprintf(stderr, "\t-m\tskips media controller setup\n");
	fprintf(stderr, "\t-h\tshow this help\n");
}

static inline int parse_rect(char *s, struct v4l2_rect *r)
{
	return sscanf(s, "%d,%d@%d,%d", &r->width, &r->height,
			&r->left, &r->top) != 4;
}

static int parse_args(int argc, char *argv[], struct setup *s)
{
	if(argc <= 1) {
		usage(argv[0]);
		return -1;
	}

	int c, ret;
	memset(s, 0, sizeof(*s));

	s->mplane_type=ret=V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while((c = getopt(argc, argv, "b:f:h:d:iI:O:n:Er:p:N:ml:")) != -1) {
		switch (c) {
			case 'b':
				ret = sscanf(optarg, "%u", &s->buffer_count);
				if (WARN_ON(ret != 1, "incorrect buffer count\n"))
					return -1;
				break;
			case 'f':
				if (WARN_ON(strlen(optarg) != 4, "invalid fourcc\n"))
					return -1;
				if (strncmp(optarg, "UYVY", 4) == 0) {
					s->in_fourcc = V4L2_MBUS_FMT_UYVY8_1X16;
				} else if (strncmp(optarg, "YUYV", 4) == 0) {
					s->in_fourcc = V4L2_MBUS_FMT_YUYV8_1X16;
				} else if (strncmp(optarg, "RGB3", 4) == 0) {
					s->in_fourcc = V4L2_MBUS_FMT_RGB888_1X24;
				} else {
					/*By default fallback to RGB565 */
					s->in_fourcc = MEDIA_BUS_FMT_RGB565_1X16;
				}
				break;
			case '?':
			case 'h':
				usage(argv[0]);
				return -1;
			case 'd':
				strncpy(s->video, optarg, 31);
				break;
			case 'I':
				ret = sscanf(optarg, "%u,%u", &s->iw, &s->ih);
				if (WARN_ON(ret != 2, "incorrect input size\n"))
					return -1;
				s->use_wh = 1;
				s->original_iw = s->iw;
				break;
			case 'O':
				ret = sscanf(optarg, "%u,%u", &s->ow, &s->oh);
				if (WARN_ON(ret != 2, "incorrect output size\n"))
					return -1;
				break;
			case 'p':
				ret = v4l2_buf_type_from_string(optarg);
				if (ret == -1) {
					printf("Bad buffer type \"%s\"\n", optarg);
					return ret;
				}
				s->mplane_type=ret;
				break;
			case 'n':
				ret = sscanf(optarg, "%u", &s->port);
				break;
			case 'E':
				s->exporter = 1;
				break;
			case 'i':
				s->interlaced = 1;
				break;
			case 'r':
				if (strncmp(optarg, "GL_DMA", 6) == 0) {
					s->render_type = RENDER_TYPE_GL_DMA;
				} else if (strncmp(optarg, "GL", 2) == 0 ){
					s->render_type = RENDER_TYPE_GL;
				} else {
					s->render_type = RENDER_TYPE_WL;
				}
				break;
			case 'N':
				s->frames_count = atoi(optarg);
				break;
			case 'l':
				s->loops_count = atoi(optarg);
				break;
			case 'm':
				s->skip_media_controller_setup = 1;
				break;
		}
	}

	return 0;
}

#define V4L2_SUBDEV_ROUTE_FL_ACTIVE    (1 << 0)
#define V4L2_SUBDEV_ROUTE_FL_IMMUTABLE (1 << 1)
#define V4L2_SUBDEV_ROUTE_FL_SOURCE    (1 << 2)
/**
 * struct v4l2_subdev_route - A signal route inside a subdev
 * @sink_pad: the sink pad
 * @sink_stream: the sink stream
 * @source_pad: the source pad
 * @source_stream: the source stream
 * @flags: route flags:
 *
 *     V4L2_SUBDEV_ROUTE_FL_ACTIVE: Is the stream in use or not? An
 *     active stream will start when streaming is enabled on a video
 *     node. Set by the user.
 *
 *     V4L2_SUBDEV_ROUTE_FL_SOURCE: Is the sub-device the source of a
 *     stream? In this case the sink information is unused (and
 *     zero). Set by the driver.
 *
 *     V4L2_SUBDEV_ROUTE_FL_IMMUTABLE: Is the stream immutable, i.e.
 *     can it be activated and inactivated? Set by the driver.
 */
struct v4l2_subdev_route {
	__u32 sink_pad;
	__u32 sink_stream;
	__u32 source_pad;
	__u32 source_stream;
	__u32 flags;
	__u32 reserved[5];
};

/**
 * struct v4l2_subdev_routing - Routing information
 * @routes: the routes array
 * @num_routes: the total number of routes in the routes array
 */
struct v4l2_subdev_routing {
	struct v4l2_subdev_route *routes;
	__u32 num_routes;
	__u32 reserved[5];
};

#define VIDIOC_SUBDEV_G_ROUTING                        _IOWR('V', 38, struct v4l2_subdev_routing)
#define VIDIOC_SUBDEV_S_ROUTING                        _IOWR('V', 39, struct v4l2_subdev_routing)

char* find_entity(struct media_device* md, const char* entity_name)
{
	struct media_entity *entity = NULL;
	const struct media_entity_desc *entity_desc = NULL;
	int entities_count = media_get_entities_count(md);
	int i;
	char *full_entity_name;

	for (i = 0; i < entities_count; i++) {
		entity = media_get_entity(md, i);
		entity_desc = media_entity_get_info(entity);
		if (strncmp(entity_name, entity_desc->name, strlen(entity_name)) == 0) {
			full_entity_name = strdup(entity_desc->name);
			return full_entity_name;
		}
	}
	return NULL;
}

const char* get_entity_devname(struct media_device* md, const char* entity_name)
{
	struct media_entity * entity = NULL;
	entity = media_get_entity_by_name(md, entity_name);
	if (!entity) {
		printf("Cannot find entity %s\n", entity_name);
		return NULL;
	}
	return media_entity_get_devname(entity);
}

int set_fmt(struct media_device* md, const char* entity_name, unsigned int width, unsigned int height, int fmt, int interlaced, unsigned int pad)
{
	struct media_entity * entity = NULL;
	entity = media_get_entity_by_name(md, entity_name);
	if (!entity) {
		printf("Cannot find entity %s\n", entity_name);
		return -1;
	}

	int ret = v4l2_subdev_open(entity);
	if (ret < 0) {
		printf("Cannot open subdev\n");
		return -1;
	}

	struct v4l2_mbus_framefmt format;
	format.width = width;
	format.height = height;
	format.code = fmt;
	format.field = V4L2_FIELD_NONE;
	if (interlaced) {
		format.field = V4L2_FIELD_ALTERNATE;
	}

	ret = v4l2_subdev_set_format(entity, &format, pad, V4L2_SUBDEV_FORMAT_ACTIVE);
	if (ret < 0) {
		printf("Cannot set format\n");
		return -1;
	}

	v4l2_subdev_close(entity);
	return 0;
}

int set_ctrl(struct media_device *md, const char* entity_name, int ctrl_id, int ctrl_value)
{
	int ret;
	struct media_entity * entity = NULL;
	entity = media_get_entity_by_name(md, entity_name);
	if (!entity) {
		printf("Cannot find entity %s\n", entity_name);
		return -1;
	}

	const char* subdev_node = media_entity_get_devname(entity);
	int subdev_fd = open(subdev_node, O_RDWR);
	if (subdev_fd < 0) {
		printf("Cannot open subdev\n");
		return -1;
	}

	struct v4l2_control ctrl;
	ctrl.id = ctrl_id;
	ctrl.value = ctrl_value;
	ret = ioctl(subdev_fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		printf("unable to set control %s %d\n", strerror(ret), ret);
		return -1;
	}

	close(subdev_fd);
	return 0;

}

int setup_routing(struct media_device *md, const char* entity_name)
{
	int ret;
	struct media_entity * entity = NULL;
	entity = media_get_entity_by_name(md, entity_name);
	if (!entity) {
		printf("Cannot find entity %s\n", entity_name);
		return -1;
	}

	const char* subdev_node = media_entity_get_devname(entity);
	int subdev_fd = open(subdev_node, O_RDWR);
	if (subdev_fd < 0) {
		printf("Cannot open subdev\n");
		return -1;
	}

	struct v4l2_subdev_routing routing;
	struct v4l2_subdev_route route[2];

	route[0].sink_pad = 0;
	route[0].source_pad = 8;
	route[0].sink_stream = 0;
	route[0].source_stream = 0;
	route[0].flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE;

	route[1].sink_pad = 4;
	route[1].source_pad = 12;
	route[1].sink_stream = 0;
	route[1].source_stream = 0;
	route[1].flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE;

	routing.routes = &route[0];
	routing.num_routes = 2;

	ret = ioctl(subdev_fd, VIDIOC_SUBDEV_S_ROUTING, &routing);
	if (ret < -1) {
		printf("unable to set routing %s %d\n", strerror(ret), ret);
		close(subdev_fd);
		return -1;
	}

	close(subdev_fd);
	return 0;
}

int set_compose(struct media_device *md, const char* entity_name, int width, int height, int pad)
{
	struct media_entity * entity = NULL;
	entity = media_get_entity_by_name(md, entity_name);
	if (!entity) {
		printf("Cannot find entity %s\n", entity_name);
		return -1;
	}

	int ret = v4l2_subdev_open(entity);
	if (ret < 0) {
		printf("Cannot open subdev\n");
		return -1;
	}

	struct v4l2_rect rect;
	rect.left = 0;
	rect.top = 0;
	rect.width = width;
	rect.height = height;

	ret = v4l2_subdev_set_selection(entity, &rect, pad, V4L2_SEL_TGT_COMPOSE, V4L2_SUBDEV_FORMAT_ACTIVE);
	if (ret < 0) {
		printf("Cannot set crop\n");
		return -1;
	}

	v4l2_subdev_close(entity);
	return 0;
}

int setup_link(struct media_device* md, const char* source_entity_name, int source_pad_number,
		const char* sink_entity_name, int sink_pad_number,
		int flags)
{
	struct media_entity * source_entity = NULL;
	struct media_entity * sink_entity = NULL;
	struct media_pad* source_pad = NULL;
	struct media_pad* sink_pad = NULL;

	source_entity = media_get_entity_by_name(md, source_entity_name);
	if (!source_entity) {
		printf("Cannot find entity %s\n", source_entity_name);
		return -1;
	}

	source_pad = (struct media_pad*)media_entity_get_pad(source_entity, source_pad_number);
	if (!source_pad) {
		printf("Cannot find pad %d of entity %s\n", source_pad_number, source_entity_name);
		return -1;
	}

	sink_entity = media_get_entity_by_name(md, sink_entity_name);
	if (!sink_entity) {
		printf("Cannot find entity %s\n", sink_entity_name);
		return -1;
	}

	sink_pad = (struct media_pad*)media_entity_get_pad(sink_entity, sink_pad_number);
	if (!sink_pad) {
		printf("Cannot find pad %d of entity %s\n", sink_pad_number, sink_entity_name);
		return -1;
	}

	int ret = media_setup_link(md, source_pad, sink_pad, MEDIA_LNK_FL_ENABLED | (flags));
	if (ret < 0) {
		printf("Cannot setup link %d\n", ret);
		return -1;
	}
	return 0;
}

static void media_controller_init(struct setup* s)
{
	int ret;
	struct media_device* md = NULL;
	md = media_device_new("/dev/media0");
	BYE_ON(!md, "Cannot create media device\n");

	ret = media_device_enumerate(md);
	BYE_ON(ret, "Cannot enumerate media device\n");

	char* adv_pa = NULL;
	char* adv_binner = NULL;
	const char* ipu4_csi = "Intel IPU4 CSI-2 0";

	ret = setup_routing(md, "Intel IPU4 CSI2 BE SOC");
	BYE_ON(ret, "Cannot setup routing\n");

	if (s->port == 0) {
		adv_pa = find_entity(md, "adv7481-hdmi pixel array a");
		adv_binner = find_entity(md, "adv7481-hdmi binner a");
	} else if (s->port == 4) {
		adv_pa = find_entity(md, "adv7481-cvbs pixel array a");
		adv_binner = find_entity(md, "adv7481-cvbs binner a");
		ipu4_csi = "Intel IPU4 CSI-2 4";
	}
	BYE_ON((adv_pa == NULL || adv_binner == NULL), "Cannot find pixel array and binner entities\n");

	ret = set_fmt(md, ipu4_csi, s->iw, s->ih, s->in_fourcc, s->interlaced, 0);
	BYE_ON(ret, "Cannot set format for entity %s[%d]\n", ipu4_csi, 0);
	if (s->port == 0) {   
		ret = set_fmt(md, adv_pa, 1920, 1080, s->in_fourcc, s->interlaced, 0);
		BYE_ON(ret, "Cannot set format for entity %s[%d]\n", adv_pa, 0);

		ret = set_fmt(md, adv_binner, 1920, 1080, s->in_fourcc, s->interlaced, 0);
		BYE_ON(ret, "Cannot set format for entity %s[%d]\n", adv_binner, 0);
	
		ret = set_fmt(md, "Intel IPU4 CSI2 BE SOC", s->iw, s->ih, s->in_fourcc, s->interlaced,  0);
		BYE_ON(ret, "Cannot set format for entity Intel IPU4 CSI2 BE SOC[0]\n");

		ret = set_fmt(md, "Intel IPU4 CSI2 BE SOC", s->iw, s->ih, s->in_fourcc, s->interlaced,  8);
		BYE_ON(ret, "Cannot set format for entity Intel IPU4 CSI2 BE SOC[8]\n");

	} else {
		ret = set_fmt(md, adv_pa, s->iw, s->ih, s->in_fourcc, s->interlaced, 0);
		BYE_ON(ret, "Cannot set format for entity %s[%d]\n", adv_pa, 0);

		ret = set_fmt(md, adv_binner, s->iw, s->ih, s->in_fourcc, s->interlaced, 0);
		BYE_ON(ret, "Cannot set format for entity %s[%d]\n", adv_binner, 0);

		ret = set_fmt(md, "Intel IPU4 CSI2 BE SOC", s->iw, s->ih, s->in_fourcc, s->interlaced,  4);
		BYE_ON(ret, "Cannot set format for entity Intel IPU4 CSI2 BE SOC[4]\n");

		ret = set_fmt(md, "Intel IPU4 CSI2 BE SOC", s->iw, s->ih, s->in_fourcc, s->interlaced,  12);
		BYE_ON(ret, "Cannot set format for entity Intel IPU4 CSI2 BE SOC[12]\n");
	}

	ret = set_compose(md, adv_binner, s->iw, s->ih, 0);
	BYE_ON(ret, "Cannot set compose for entity %s[%d]\n", adv_binner, 0);

	ret = set_fmt(md, adv_binner, s->iw, s->ih, s->in_fourcc, s->interlaced, 1); 
	BYE_ON(ret, "Cannot set format for entity %s[%d]\n", adv_binner, 1);

	/* ADV7481 HDMI input provides two link frequencies,
	 * in such case correct one for given color format needs
	 * to be specified by application for YUV/RGB565 link frequency
	 * with index 0 needs to be used for RGB888 with index 1*/
	if (s->in_fourcc == V4L2_MBUS_FMT_RGB888_1X24) {
		ret = set_ctrl(md, adv_binner, V4L2_CID_LINK_FREQ, 1);
	} else {
		ret = set_ctrl(md, adv_binner, V4L2_CID_LINK_FREQ, 0);
	}
	BYE_ON(ret, "Cannot set LINK FREQ ctrl for entity %s\n", adv_binner);

	ret = setup_link(md, adv_pa, 0, adv_binner, 0, 0);
	BYE_ON(ret, "Cannot settup link between %s[%d] -> %s[%d]\n", adv_pa, 0, adv_binner, 0);

	ret = setup_link(md, adv_binner, 1, ipu4_csi, 0, 0);
	BYE_ON(ret, "Cannot settup link between %s[%d] -> %s[%d]\n", adv_binner, 1, ipu4_csi, 0);

	if (s->port == 0) {
		ret = setup_link(md, ipu4_csi, 1, "Intel IPU4 CSI2 BE SOC", 0, MEDIA_LNK_FL_DYNAMIC);
		BYE_ON(ret, "Cannot settup link between %s[%d] -> %s[%d]\n", ipu4_csi, 1, "Intel IPU4 CSI2 BE SOC", 4);

		ret = setup_link(md, "Intel IPU4 CSI2 BE SOC", 8, "Intel IPU4 BE SOC capture 0", 0, MEDIA_LNK_FL_DYNAMIC );
		BYE_ON(ret, "Cannot settup link between %s[%d] -> %s[%d]\n", "Intel IPU4 CSI2 BE SOC", 8, "Intel IPU4 BE SOC capture 0", 0);

		if (strlen(s->video) == 0) {
			strncpy(s->video, get_entity_devname(md, "Intel IPU4 BE SOC capture 0"), 31);
		}
	} else {
		ret = setup_link(md, ipu4_csi, 1, "Intel IPU4 CSI2 BE SOC", 4, MEDIA_LNK_FL_DYNAMIC);
		BYE_ON(ret, "Cannot settup link between %s[%d] -> %s[%d]\n", ipu4_csi, 1, "Intel IPU4 CSI2 BE SOC", 4);

		ret = setup_link(md, "Intel IPU4 CSI2 BE SOC", 12, "Intel IPU4 BE SOC capture 4", 0, MEDIA_LNK_FL_DYNAMIC );
		BYE_ON(ret, "Cannot settup link between %s[%d] -> %s[%d]\n", "Intel IPU4 CSI2 BE SOC", 12, "Intel IPU4 BE SOC capture 4", 0);
	
		if (strlen(s->video) == 0) {
			strncpy(s->video, get_entity_devname(md, "Intel IPU4 BE SOC capture 4"), 31);
		}
	}

	media_device_unref(md);
	free(adv_pa);
	free(adv_binner);
}

static int video_querycap(struct v4l2_device *dev, unsigned int *capabilities)
{
	struct v4l2_capability cap;
	unsigned int caps;
	int ret;

	CLEAR(cap);
	ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
	BYE_ON(ret, "VIDIOC_QUERYCAP failed: %s\n", ERRSTR);

	caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
		? cap.device_caps : cap.capabilities;

	printf("Device `%s' on `%s' is a video %s (%s mplanes) device.\n",
			cap.card, cap.bus_info,
			caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_CAPTURE) ? "capture" : "output",
			caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE) ? "with" : "without");

	*capabilities = caps;

	return 0;
}

static int cap_get_buf_type(unsigned int capabilities)
{
	if (capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
		return V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	} else if (capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) {
		return V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else if (capabilities & V4L2_CAP_VIDEO_CAPTURE) {
		return  V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else if (capabilities & V4L2_CAP_VIDEO_OUTPUT) {
		return V4L2_BUF_TYPE_VIDEO_OUTPUT;
	} else {
		printf("Device supports neither capture nor output.\n");
		return -EINVAL;
	}

	return 0;
}

static int video_set_format(struct v4l2_device *dev, unsigned int w,
		unsigned int h, unsigned int format, enum v4l2_field field,
		unsigned int stride, unsigned int buffer_size)
{
	struct v4l2_format fmt;
	unsigned int i;
	int ret;

	CLEAR(fmt);
	fmt.type = dev->type;

	if (video_is_mplane(dev)) {
		fmt.fmt.pix_mp.width = w;
		fmt.fmt.pix_mp.height = h;
		fmt.fmt.pix_mp.pixelformat = format;
		fmt.fmt.pix_mp.field = field;
		fmt.fmt.pix_mp.num_planes = 1;

		for (i = 0; i < fmt.fmt.pix_mp.num_planes; i++) {
			fmt.fmt.pix_mp.plane_fmt[i].bytesperline = w*4;
			fmt.fmt.pix_mp.plane_fmt[i].sizeimage = buffer_size;
		}
	} else {
		fmt.fmt.pix.width = w;
		fmt.fmt.pix.height = h;
		fmt.fmt.pix.pixelformat = format;
		fmt.fmt.pix.field = field;
		fmt.fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
	}
	ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to set format: %s (%d).\n", strerror(errno),
				errno);
		return ret;
	}

	if (video_is_mplane(dev)) {
		for (i = 0; i < fmt.fmt.pix_mp.num_planes; i++) {
			printf(" * Stride %u, buffer size %u\n",
					fmt.fmt.pix_mp.plane_fmt[i].bytesperline,
					fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
		}
	} 

	return 0;
}

static int video_get_format(struct v4l2_device *dev)
{
	struct v4l2_format fmt;
	unsigned int i;
	int ret;

	CLEAR(fmt);
	fmt.type = dev->type;

	ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to get format: %s (%d).\n", strerror(errno),
				errno);
		return ret;
	}

	if (video_is_mplane(dev)) {
		dev->num_planes = fmt.fmt.pix_mp.num_planes;

		for (i = 0; i < fmt.fmt.pix_mp.num_planes; i++) {
			dev->plane_fmt[i].bytesperline =
				fmt.fmt.pix_mp.plane_fmt[i].bytesperline;
			dev->plane_fmt[i].sizeimage =
				fmt.fmt.pix_mp.plane_fmt[i].bytesperline ?
				fmt.fmt.pix_mp.plane_fmt[i].sizeimage : 0;

			printf(" * Stride %u, buffer size %u\n",
					fmt.fmt.pix_mp.plane_fmt[i].bytesperline,
					fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
		}
	} else {
		dev->num_planes = 1;

		dev->plane_fmt[0].bytesperline = fmt.fmt.pix.bytesperline;
		dev->plane_fmt[0].sizeimage = fmt.fmt.pix.bytesperline ? fmt.fmt.pix.sizeimage : 0;
	}

	return 0;
}
static void v4l2_init(struct v4l2_device *dev, struct setup s)
{
	int ret;
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	struct v4l2_requestbuffers rqbufs;

	/* Use video capture by default if query isn't done. */
    	unsigned int capabilities = V4L2_CAP_VIDEO_CAPTURE;

    	video_set_buf_type(dev, s.mplane_type);
	CLEAR(parm);
	parm.parm.capture.capturemode = _ISP_MODE_STILL;

	dev->fd = open(dev->devname, O_RDONLY);
	BYE_ON(dev->fd < 0, "failed to open %s: %s\n", dev->devname, ERRSTR);

	ret = video_querycap(dev, &capabilities);
	BYE_ON(ret, "VIDIOC_QUERYCAP failed: %s\n", ERRSTR);

	CLEAR(fmt);
	fmt.type = cap_get_buf_type(capabilities);

	ret = video_get_format(dev);
	BYE_ON(ret < 0, "VIDIOC_G_FMT failed: %s, %s\n", dev->devname, ERRSTR);
	printf("G_FMT(start): width = %u, height = %u, 4cc = %.4s\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			(char*)&fmt.fmt.pix.pixelformat);

	fmt.fmt.pix.width = s.iw;
	fmt.fmt.pix.height = s.ih;
	fmt.fmt.pix.pixelformat = s.in_fourcc;

	if (s.in_fourcc == V4L2_MBUS_FMT_RGB888_1X24) {
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_XBGR32;
	} else if (s.in_fourcc == MEDIA_BUS_FMT_RGB565_1X16) {
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_XRGB32;
	} else if (s.in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16) {
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	} else if (s.in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	}

	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (s.interlaced) {
		fmt.fmt.pix.field = V4L2_FIELD_ALTERNATE;
	}

	ret = video_set_format(dev, fmt.fmt.pix.width, fmt.fmt.pix.height,
			 fmt.fmt.pix.pixelformat, fmt.fmt.pix.field,
			 OPT_STRIDE,OPT_BUFFER_SIZE);
	BYE_ON(ret < 0, "VIDIOC_S_FMT failed: %s\n", ERRSTR);

	ret = video_get_format(dev);
	printf("G_FMT(final): width = %u, height = %u, 4cc = %.4s\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			(char*)&fmt.fmt.pix.pixelformat);

	CLEAR(rqbufs);
	rqbufs.count = s.buffer_count;
	rqbufs.type = dev->type;
	if(dev->is_exporter) {
		rqbufs.memory = V4L2_MEMORY_MMAP;
	} else {
		rqbufs.memory = V4L2_MEMORY_DMABUF;
	}

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rqbufs);
	BYE_ON(ret < 0, "VIDIOC_REQBUFS failed: %s\n", ERRSTR);
	BYE_ON(rqbufs.count < s.buffer_count, "video node allocated only "
			"%u of %u buffers\n", rqbufs.count, s.buffer_count);

	dev->format = fmt.fmt.pix;
}

static void polling_thread(void *data)
{
	struct display *display = (struct display *)data;
	struct pollfd fd;
	unsigned int received_frames;
	unsigned int total_received_frames;
	struct timeval prev_time, curr_time;
	struct timeval time_diff;
	struct timeval tmp;
	float time_diff_secs;
	int poll_res;

	fd.fd = display->v4l2->fd;
	fd.events = POLLIN;

	gettimeofday(&prev_time, NULL);
	received_frames = 0;
	total_received_frames = 0;
	while(running) {
		poll_res = poll(&fd, 1, 500);
		if (poll_res < 0) {
			signal_int(0);
			return;
		} else if (poll_res > 0) {
			if (fd.revents & POLLERR){
				printf("Received IPU error - recovering\n");
				error_recovery = 1;
				signal_int(0);
				return;
			} else if(fd.revents & POLLIN) {
				struct buffer *buf = v4l2_dequeue_buffer(display->v4l2, display->buffers);
				if(buf) {
					v4l2_queue_buffer(display->v4l2,
							&display->buffers[buf->index]);

					if (buf->field_type == FIELD_TYPE_BOTTOM) {
						cur_bottom_buffer = buf;
					} else {
						cur_top_buffer = buf;
					}
				}
				if (first_frame_received == 0) {
					first_frame_received = 1;
					GET_TS(time_measurements.first_frame_time);
				}

				received_frames++;
				total_received_frames++;
				if (display->s->frames_count != 0 && total_received_frames >= display->s->frames_count) {
					running = 0;
				}
				gettimeofday(&curr_time, NULL);
				timersub(&curr_time, &prev_time, &time_diff);
				time_diff_secs = (time_diff.tv_sec * 1000 + time_diff.tv_usec / 1000) / 1000;

				if (time_diff_secs >= TARGET_NUM_SECONDS) {
					fprintf(stdout, "Received %d frames from IPU in %6.3f seconds = %6.3f FPS\n",
							received_frames, time_diff_secs, received_frames / time_diff_secs);
					fflush(stdout);

					received_frames = 0;

					tmp = prev_time;
					prev_time = curr_time;
					curr_time = tmp;
				}
			}
		}
	}
}

static void
init_egl(struct display *display, int opaque)
{
	const char* egl_extensions = NULL;

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

	display->egl.dpy = eglGetDisplay((EGLNativeDisplayType) display->display);
	assert(display->egl.dpy);

	ret = eglInitialize(display->egl.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	ret = eglChooseConfig(display->egl.dpy, config_attribs,
			&display->egl.conf, 1, &n);
	assert(ret && n == 1);

	display->egl.ctx = eglCreateContext(display->egl.dpy,
			display->egl.conf,
			EGL_NO_CONTEXT, context_attribs);
	assert(display->egl.ctx);

	egl_extensions = eglQueryString(display->egl.dpy, EGL_EXTENSIONS);
	if (strstr(egl_extensions, "EGL_KHR_image_base") &&
	    strstr(egl_extensions, "EXT_image_dma_buf_import")) {
		eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
		eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
	}
	BYE_ON(eglCreateImageKHR == NULL, "EGL_KHR_image_base and EXT_image_dma_buf_import not supported\n");
	BYE_ON(eglDestroyImageKHR == NULL, "EGL_KHR_image_base and EXT_image_dma_buf_import not supported\n");
}

static void
create_surface(struct window *window)
{
	struct display *display = window->display;
	EGLBoolean ret;
	uint32_t ivi_surf_id;

	window->surface = wl_compositor_create_surface(display->compositor);
	if (display->ias_shell) {
		window->shell_surface = ias_shell_get_ias_surface(display->ias_shell,
				window->surface, "DMA Test");
		ias_surface_add_listener(window->shell_surface,
				&ias_surface_listener, window);
	}
	if (display->wl_shell) {
		window->shell_surface = wl_shell_get_shell_surface(display->wl_shell,
				window->surface);
		wl_shell_surface_add_listener(window->shell_surface,
				&wl_shell_surface_listener, window);
	}
	if (display->ivi_application) {
		ivi_surf_id = (uint32_t) getpid();
		window->ivi_surface = 
			ivi_application_surface_create(display->ivi_application,
						       ivi_surf_id, window->surface);

		ivi_surface_add_listener(window->ivi_surface,
					 &ivi_surface_listener, window);
	}

	if (display->wl_shell) {
		wl_shell_surface_set_title(window->shell_surface, "dma-test");
	}

	if (display->s->render_type != RENDER_TYPE_WL) {
		window->native =
			wl_egl_window_create(window->surface,
					window->window_size.width,
					window->window_size.height);
		window->egl_surface =
			eglCreateWindowSurface(display->egl.dpy,
					display->egl.conf,
					(EGLNativeWindowType) window->native, NULL);

		ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
				     window->egl_surface, window->display->egl.ctx);
		assert(ret == EGL_TRUE);
	}

	toggle_fullscreen(window, window->fullscreen);
}

static void
init_gl_shaders(struct window *window)
{
	GLint status;
	GLuint frag, vert;
	FILE* pf;
	GLint shader_size;
	GLenum shader_format;
	char *shader_binary;
	unsigned int got_binary_shader = 0;
	unsigned int color_format;

	window->gl.program = glCreateProgram();

	if (glProgramBinaryOES) {
		pf = fopen("shader.bin", "rb");
		if (pf) {
			size_t result_sz = fread(&shader_size, sizeof(shader_size), 1, pf);
			if(result_sz != 1) {
				BYE_ON(pf, "Failed to fopen shader file due to invalid shader size\n");
			}

			size_t result_sf = fread(&shader_format, sizeof(shader_format), 1, pf);
			if(result_sf != 1) {
				BYE_ON(pf, "Failed to fopen shader file due to invalid shader format\n");
			}

			size_t result_cf = fread(&color_format, sizeof(color_format), 1, pf);
			if(result_cf != 1) {
				BYE_ON(pf, "Failed to fopen shader file due to invalid color format\n");
			}

			if (color_format == window->display->s->in_fourcc) {
				shader_binary = malloc(shader_size);
				if (shader_binary == NULL)
				{
				    BYE_ON(pf, "Failed to allocate memory for shader_binary\n");
				}

				size_t result_ss = fread(shader_binary, shader_size, 1, pf);
				if(result_ss != 1) {
					BYE_ON(pf, "Failed to fopen shader file due to invalid shader file malloc.\n");
				}

				glProgramBinaryOES(window->gl.program,
						shader_format,
						shader_binary,
						shader_size);

				free(shader_binary);
				got_binary_shader = 1;
			}
			fclose(pf);
		};
	}
	if (!got_binary_shader) {
		vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);
		if (window->display->s->in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16) {
			frag = create_shader(window, frag_shader_text_UYVY, GL_FRAGMENT_SHADER);
		} else if (window->display->s->in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16 &&
				window->display->s->render_type != RENDER_TYPE_GL_DMA) {
			/* Use YUVY shader only when imporing data as RGBA888
			 * texuture (ie. using RENDER_TYPE_GL), if texture is
			 * being created directly from DMA buffer,
			 * UFO will automatically convert YUYV into RGB when sampling,
			 * so in that case regular RGB shader needs to be used
			 */
			frag = create_shader(window, frag_shader_text_YUYV, GL_FRAGMENT_SHADER);
		} else {
			frag = create_shader(window, frag_shader_text_RGB, GL_FRAGMENT_SHADER);
		}

		glAttachShader(window->gl.program, frag);
		glAttachShader(window->gl.program, vert);
		glLinkProgram(window->gl.program);
	}

	glGetProgramiv(window->gl.program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(window->gl.program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		exit(1);
	}

	if (glProgramBinaryOES && !got_binary_shader) {
		glGetProgramiv(window->gl.program, GL_PROGRAM_BINARY_LENGTH_OES,
				&shader_size);

		shader_binary = malloc(shader_size);

		glGetProgramBinaryOES(window->gl.program, shader_size, NULL,
				&shader_format, shader_binary);

		pf = fopen("shader.bin", "wb");
		if (pf) {
			fwrite(&shader_size, sizeof(shader_size), 1, pf);
			fwrite(&shader_format, sizeof(shader_format), 1, pf);
			fwrite(&window->display->s->in_fourcc, sizeof(window->display->s->in_fourcc), 1, pf);
			fwrite(shader_binary, 1, shader_size, pf);

			fclose(pf);
		}
		free(shader_binary);
	}
}

static void
init_gl(struct window *window)
{
	GLsizei texture_width = window->display->s->iw >> 1;
	const GLfloat HMI_W = 1.f;
	const GLfloat HMI_H = 1.f;
	const GLfloat HMI_Z = 0.f;
	const char* gl_extensions = NULL;
	GLint num_binary_program_formats = 0;

	/*
	 * If input stream width was changed becasue it was not multiply of 32, crop additionaly added pixels
	 * that will be filled by IPU with padding
	 */
	float width_correction = (float)(window->display->s->original_iw) / window->display->s->iw;

	gl_extensions = (const char *) glGetString(GL_EXTENSIONS);

	if (strstr(gl_extensions, "GL_OES_get_program_binary")) {
		glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, &num_binary_program_formats);
		if (num_binary_program_formats) {
			glProgramBinaryOES = (PFNGLPROGRAMBINARYOESPROC) eglGetProcAddress("glProgramBinaryOES");
			glGetProgramBinaryOES = (PFNGLGETPROGRAMBINARYOESPROC) eglGetProcAddress("glGetProgramBinaryOES");
		}
	}

	if (strstr(gl_extensions, "GL_OES_EGL_image_external")) {
		glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
	}

	BYE_ON(glEGLImageTargetTexture2DOES == NULL, "glEGLImageTargetTexture2DOES not supported\n");

	init_gl_shaders(window);

	glUseProgram(window->gl.program);

	window->gl.pos = glGetAttribLocation(window->gl.program, "pos");
	window->gl.col = glGetAttribLocation(window->gl.program, "color");
	window->gl.attr_tex = glGetAttribLocation(window->gl.program, "itexcoord");

	window->gl.modelview_uniform =
		glGetUniformLocation(window->gl.program, "modelviewProjection");
	window->gl.gl_texture_size = glGetUniformLocation(window->gl.program, "u_texsize");

	glUniform2f(window->gl.gl_texture_size,
			(float)texture_width,
			(float)window->display->s->ih);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	window->gl.tex_top = glGetUniformLocation(window->gl.program, "u_texture_top");
	window->gl.tex_bottom = glGetUniformLocation(window->gl.program, "u_texture_bottom");

	glGenTextures(2, window->gl.gl_texture);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, window->gl.gl_texture[0]);

	if (window->display->s->in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16 ||
			window->display->s->in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, window->display->s->iw/2,
				window->display->s->ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, window->display->s->iw,
				window->display->s->ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		window->gl.rgb565 = glGetUniformLocation(window->gl.program, "rgb565");
		glUniform1i(window->gl.rgb565, 0);
		if (window->display->s->in_fourcc == MEDIA_BUS_FMT_RGB565_1X16)
			glUniform1i(window->gl.rgb565, 1);
	}

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, window->gl.gl_texture[1]);

	if (window->display->s->in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16 ||
			window->display->s->in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, window->display->s->iw/2,
				window->display->s->ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, window->display->s->iw,
				window->display->s->ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		window->gl.rgb565 = glGetUniformLocation(window->gl.program, "rgb565");
		glUniform1i(window->gl.rgb565, 0);
		if (window->display->s->in_fourcc == MEDIA_BUS_FMT_RGB565_1X16)
			glUniform1i(window->gl.rgb565, 1);
	}

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glActiveTexture(GL_TEXTURE0);

	glUniform1i(window->gl.tex_top, 0);
	glUniform1i(window->gl.tex_bottom, 1);

	window->gl.swap_rb = glGetUniformLocation(window->gl.program, "swap_rb");
	/*
	 * Because GLES does not support BGRA format, red and blue components must
	 * be swapped in shader, when GL_DMA rendering method is used, texture is
	 * created using BGRA layout and swap is not required
	 */
	glUniform1i(window->gl.swap_rb, window->display->s->render_type == RENDER_TYPE_GL);

	window->gl.interlaced = glGetUniformLocation(window->gl.program, "interlaced");
	glUniform1i(window->gl.interlaced, window->display->s->interlaced);

	glClearColor(.5, .5, .5, .20);

	make_matrix(window->gl.model_view, 1.0);
	window->gl.hmi_vtx[0] =  -HMI_W;
	window->gl.hmi_vtx[1] =   HMI_H;
	window->gl.hmi_vtx[2] =   HMI_Z;

	window->gl.hmi_vtx[3] =  -HMI_W;
	window->gl.hmi_vtx[4] =  -HMI_H;
	window->gl.hmi_vtx[5] =   HMI_Z;

	window->gl.hmi_vtx[6] =   HMI_W;
	window->gl.hmi_vtx[7] =   HMI_H;
	window->gl.hmi_vtx[8] =   HMI_Z;

	window->gl.hmi_vtx[9]  =  HMI_W;
	window->gl.hmi_vtx[10] = -HMI_H;
	window->gl.hmi_vtx[11] =  HMI_Z;

	window->gl.hmi_tex[0] = 0.0f;
	window->gl.hmi_tex[1] = 0.0f;
	window->gl.hmi_tex[2] = 0.0f;
	window->gl.hmi_tex[3] = 1.0f;
	window->gl.hmi_tex[4] = width_correction;
	window->gl.hmi_tex[5] = 0.0f;
	window->gl.hmi_tex[6] = width_correction;
	window->gl.hmi_tex[7] = 1.0f;

	window->gl.hmi_ind[0] = 0;
	window->gl.hmi_ind[1] = 1;
	window->gl.hmi_ind[2] = 3;
	window->gl.hmi_ind[3] = 0;
	window->gl.hmi_ind[4] = 3;
	window->gl.hmi_ind[5] = 2;
}

static struct buffer *v4l2_expbuffer(
		struct v4l2_device *dev, unsigned int index, struct buffer *buf)
{
	int ret = 0;

	struct v4l2_exportbuffer expbuf;
	memset(&expbuf,0,sizeof(expbuf));
	expbuf.type = dev->type;
	expbuf.index = buf->index;

	ret = ioctl(dev->fd, VIDIOC_EXPBUF, &expbuf);
	BYE_ON(ret < 0, "VIDIOC_EXPBUF failed: %s\n", ERRSTR);
	buf->dbuf_fd = expbuf.fd;

	return buf;
}

int
main(int argc, char **argv)
{
	GET_TS(time_measurements.app_start_time);
	struct v4l2_device v4l2;
	struct setup s;
	struct sigaction sigint;
	struct display display = { 0 };
	struct window  window  = { 0 };
	int i, ret = 0;
	unsigned int src_size;
	pthread_t poll_thread;
	struct stat tmp;
	char wayland_path[255];

	ret = parse_args(argc, argv, &s);
	BYE_ON(ret, "failed to parse arguments\n");

	GET_TS(time_measurements.before_md_init_time);
	if (!s.skip_media_controller_setup) {
		media_controller_init(&s);
	}

	memset(&v4l2, 0, sizeof v4l2);
	v4l2.devname = s.video;

	if (s.use_wh) {
		v4l2.format.width = s.iw;
		v4l2.format.height = s.ih;
	}
	if(!s.ow || !s.oh) {
		s.ow = s.iw;
		s.oh = s.ih;
	}
	if (s.in_fourcc)
		v4l2.format.pixelformat = s.in_fourcc;

	if(s.exporter) {
		v4l2.is_exporter = 1;
	} else {
		v4l2.is_exporter = 0;
	}

	GET_TS(time_measurements.md_init_time);
	v4l2_init(&v4l2, s);

	GET_TS(time_measurements.v4l2_init_time);
	/* width for IPU must be multiply of 32, currently driver has a bug 
	 * and it's not returning updated resolution of stream after S_FMT ioctl,
	 * it has to be modified here manually
	 */
	if (s.iw % 32 != 0) {
		s.iw += (s.iw % 32);
	}

	struct buffer buffers[s.buffer_count];

	for(i = 0; i < (int) s.buffer_count; i++) {
		buffers[i].index = i;
	}

	window.display = &display;
	display.window = &window;
	window.window_size.width  = s.ow;
	window.window_size.height = s.oh;
	window.fullscreen = s.fullscreen;
	window.output = 0;
	window.print_fps = 1;
	display.s = &s;

	display.buffers = buffers;
	display.v4l2 = &v4l2;

	if (s.in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16 ||
			s.in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
		src_size = s.iw * s.ih * 2;
	} else {
		src_size = s.iw * s.ih * 4;
	}

	ret = init_gem(&display);
	if(ret < 0) {
	    close(v4l2.fd);
	    return ret;
	}

	for(i = 0; i < (int) s.buffer_count; i++) {
		if(v4l2.is_exporter) {
			v4l2_expbuffer(&v4l2, i, &buffers[i]);
			ret = drm_buffer_to_prime(&display, &buffers[i], src_size);
			if(ret < 0) {
			    destroy_gem(&display);
			    close(v4l2.fd);
			    return ret;

			}
		} else {
			ret = create_buffer(&display, &buffers[i], src_size);
			if(ret < 0) {
				destroy_gem(&display);
				close(v4l2.fd);
				return ret;
			}
		}

		v4l2_queue_buffer(&v4l2, &buffers[i]);
	}
	int type = s.mplane_type;
	ret = ioctl(v4l2.fd, VIDIOC_STREAMON, &type);
	BYE_ON(ret < 0, "STREAMON failed: %s\n", ERRSTR);
	GET_TS(time_measurements.streamon_time);

	if(pthread_create(&poll_thread, NULL,
				(void *) &polling_thread, (void *) &display)) {
		printf("Couldn't create polling thread\n");
	}

	snprintf(wayland_path, 255, "%s/wayland-0", getenv("XDG_RUNTIME_DIR"));
	while (stat(wayland_path, &tmp) != 0) {
		usleep(100);
	}

	GET_TS(time_measurements.weston_init_time);

	display.display = wl_display_connect(NULL);
	assert(display.display);
	wl_list_init(&display.output_list);

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry,
			&registry_listener, &display);

	wl_display_dispatch(display.display);
	wl_display_roundtrip(display.display);
	if (s.render_type != RENDER_TYPE_WL) {
		init_egl(&display, window.opaque);
	}

	create_surface(&window);

	if (s.render_type != RENDER_TYPE_WL) {
		init_gl(&window);
	}
restart:
	for(i = 0; i < (int) s.buffer_count; i++) {
		if (s.render_type == RENDER_TYPE_WL) {
			if (s.in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16 ||
                            s.in_fourcc == MEDIA_BUS_FMT_RGB565_1X16) {
				//UYVY cannot be displayed using direct fliping it is possible only with YUYV
				//RGB565 can be displayed but as data is mapped to RGB888 it will have wrong color ie. image will have green tint
				BYE_ON(1, "RGB565 and UYUV formats are not supported with RENDER_TYPE_WL\n");
			} else if (s.in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
				buffers[i].buf = wl_drm_create_buffer(display.wl_drm, buffers[i].flink_name, s.iw, s.ih,
					                              s.iw*2, WL_DRM_FORMAT_YUYV);
			} else {
				buffers[i].buf = wl_drm_create_buffer(display.wl_drm, buffers[i].flink_name, s.iw, s.ih,
					                              s.iw*4, WL_DRM_FORMAT_XRGB8888);
			}
		} else if (s.render_type == RENDER_TYPE_GL_DMA) {
			if (s.in_fourcc == V4L2_MBUS_FMT_YUYV8_1X16) {
				EGLint imageAttributes[] = {
				  EGL_WIDTH, s.iw,
				  EGL_HEIGHT, s.ih,
				  EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_YUYV,
				  EGL_DMA_BUF_PLANE0_FD_EXT, buffers[i].dbuf_fd,
				  EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
				  EGL_DMA_BUF_PLANE0_PITCH_EXT, s.iw*2,
				  EGL_NONE
				};

				buffers[i].khrImage = eglCreateImageKHR(display.egl.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
						(EGLClientBuffer) NULL, imageAttributes);
			} else if (s.in_fourcc == V4L2_MBUS_FMT_UYVY8_1X16) {
				EGLint imageAttributes[] = {
					EGL_WIDTH, s.iw/2,
					EGL_HEIGHT, s.ih,
					EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
					EGL_DMA_BUF_PLANE0_FD_EXT, buffers[i].dbuf_fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, s.iw*2,
					EGL_NONE
				};

				buffers[i].khrImage = eglCreateImageKHR(display.egl.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
						(EGLClientBuffer) NULL, imageAttributes);
			} else {
				EGLint imageAttributes[] = {
					EGL_WIDTH, s.iw,
					EGL_HEIGHT, s.ih,
					EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ARGB8888,
					EGL_DMA_BUF_PLANE0_FD_EXT, buffers[i].dbuf_fd,
					EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
					EGL_DMA_BUF_PLANE0_PITCH_EXT, s.iw*4,
					EGL_NONE
				};

				buffers[i].khrImage = eglCreateImageKHR(display.egl.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
						(EGLClientBuffer) NULL, imageAttributes);
			}
			BYE_ON(buffers[i].khrImage == 0, "Cannot create texture from DMA buffer\n");
		}

	}

	GET_TS(time_measurements.rendering_init_time);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	if (prev_time)
		free(prev_time);

	prev_time = calloc(1, sizeof(struct timeval));
	if(!prev_time) {
		fprintf(stderr, "Cannot allocate memory: %m\n");
		exit(EXIT_FAILURE);
	}

	if (curr_time)
		free(curr_time);

	curr_time = calloc(1, sizeof(struct timeval));
	if(!curr_time) {
		fprintf(stderr, "Cannot allocate memory: %m\n");
		exit(EXIT_FAILURE);
	}

	gettimeofday(prev_time, NULL);

	while (running && ret != -1) {
		ret = wl_display_dispatch(display.display);
	}

	fprintf(stderr, "\ndma-test finishing loop\n");

	pthread_join(poll_thread, NULL);

	if (error_recovery) {
		for(i = 0; i < (int) s.buffer_count; i++) {
			if (s.render_type == RENDER_TYPE_WL) {
				wl_buffer_destroy(buffers[i].buf);
			} else if (s.render_type == RENDER_TYPE_GL_DMA) {
				eglDestroyImageKHR(display.egl.dpy, buffers[i].khrImage);
			}
			ret = drm_intel_bo_unmap(buffers[i].bo);
			close(buffers[i].dbuf_fd);
			drm_intel_bo_unreference(buffers[i].bo);
		}

		/* Close and reopen IPU device */
		close(v4l2.fd);
		s.iw = s.original_iw;
		v4l2_init(&v4l2, s);
		running = 1;
		error_recovery = 0;

		if (s.iw % 32 != 0) {
			s.iw += (s.iw % 32);
		}

		for(i = 0; i < (int) s.buffer_count; i++) {
			if(v4l2.is_exporter) {
				v4l2_expbuffer(&v4l2, i, &buffers[i]);
				ret = drm_buffer_to_prime(&display, &buffers[i], src_size);
				if(ret < 0) {
					return ret;
				}
			} else {
				ret = create_buffer(&display, &buffers[i], src_size);
				if(ret < 0) {
					destroy_gem(&display);
					return ret;
				}
			}

			v4l2_queue_buffer(&v4l2, &buffers[i]);
		}
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ret = ioctl(v4l2.fd, VIDIOC_STREAMON, &type);
		BYE_ON(ret < 0, "STREAMON failed: %s\n", ERRSTR);
		GET_TS(time_measurements.streamon_time);

		if(pthread_create(&poll_thread, NULL,
					(void *) &polling_thread, (void *) &display)) {
			printf("Couldn't create polling thread\n");
		}

		goto restart;
	} else if (s.loops_count--) {
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		ret = ioctl(v4l2.fd, VIDIOC_STREAMOFF, &type);
		BYE_ON(ret < 0, "STREAMOFF failed: %s\n", ERRSTR);
		running = 1;

		for(i = 0; i < (int) s.buffer_count; i++) {
			v4l2_queue_buffer(&v4l2, &buffers[i]);
		}

		ret = ioctl(v4l2.fd, VIDIOC_STREAMON, &type);
		if (ret < 0) {
			printf("STREAMON ERROR\n");
			running = 0;
			error_recovery=1;
			goto restart;
		}

		if(pthread_create(&poll_thread, NULL,
					(void *) &polling_thread, (void *) &display)) {
			printf("Couldn't create polling thread\n");
		}

		goto restart;
	}
	destroy_gem(&display);
	destroy_surface(&window);
	close(v4l2.fd);

	free(curr_time);
	free(prev_time);

	if(display.ias_shell) {
		ias_shell_destroy(display.ias_shell);
	}
	if(display.wl_shell) {
		wl_shell_destroy(display.wl_shell);
	}
	if(display.ivi_application) {
		ivi_application_destroy(display.ivi_application);
	}

	if(display.compositor) {
		wl_compositor_destroy(display.compositor);
	}

	wl_display_flush(display.display);
	wl_display_disconnect(display.display);

	return 0;
}
