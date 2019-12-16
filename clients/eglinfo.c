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
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libudev.h>
#include <fcntl.h>
#include <wayland-client.h>

#define MAX_CONFIGS 1000
#define OGLES 1

/**
 * Print table of all available configurations.
 */
static void
print_configs(EGLDisplay d)
{
   EGLConfig configs[MAX_CONFIGS];
   EGLint numConfigs, i;

   eglGetConfigs(d, configs, MAX_CONFIGS, &numConfigs);

   printf("Configurations:\n");
   printf("     bf lv d st colorbuffer dp st  ms    vis   supported\n");
   printf("  id sz  l b ro  r  g  b  a th cl ns b    id   surfaces \n");
   printf("--------------------------------------------------------\n");
   for (i = 0; i < numConfigs; i++) {
      EGLint id, size, level;
      EGLint red, green, blue, alpha;
      EGLint depth, stencil;
      EGLint surfaces;
      EGLint doubleBuf = 1, stereo = 0;
      EGLint vid;
      EGLint samples, sampleBuffers;
      char surfString[100] = "";

      eglGetConfigAttrib(d, configs[i], EGL_CONFIG_ID, &id);
      eglGetConfigAttrib(d, configs[i], EGL_BUFFER_SIZE, &size);
      eglGetConfigAttrib(d, configs[i], EGL_LEVEL, &level);

      eglGetConfigAttrib(d, configs[i], EGL_RED_SIZE, &red);
      eglGetConfigAttrib(d, configs[i], EGL_GREEN_SIZE, &green);
      eglGetConfigAttrib(d, configs[i], EGL_BLUE_SIZE, &blue);
      eglGetConfigAttrib(d, configs[i], EGL_ALPHA_SIZE, &alpha);
      eglGetConfigAttrib(d, configs[i], EGL_DEPTH_SIZE, &depth);
      eglGetConfigAttrib(d, configs[i], EGL_STENCIL_SIZE, &stencil);
      eglGetConfigAttrib(d, configs[i], EGL_NATIVE_VISUAL_ID, &vid);
      eglGetConfigAttrib(d, configs[i], EGL_SURFACE_TYPE, &surfaces);

      eglGetConfigAttrib(d, configs[i], EGL_SAMPLES, &samples);
      eglGetConfigAttrib(d, configs[i], EGL_SAMPLE_BUFFERS, &sampleBuffers);

      if (surfaces & EGL_WINDOW_BIT)
         strcat(surfString, "win,");
      if (surfaces & EGL_PBUFFER_BIT)
         strcat(surfString, "pb,");
      if (surfaces & EGL_PIXMAP_BIT)
         strcat(surfString, "pix,");
      if (strlen(surfString) > 0)
         surfString[strlen(surfString) - 1] = 0;

      printf("0x%02x %2d %2d %c  %c %2d %2d %2d %2d %2d %2d %2d%2d  0x%02x   %-12s\n",
             id, size, level,
             doubleBuf ? 'y' : '.',
             stereo ? 'y' : '.',
             red, green, blue, alpha,
             depth, stencil,
             samples, sampleBuffers, vid, surfString);
   }
}

static char *
strchrnul(const char *s, int c)
{
	while (*s && *s != c)
		s++;
	return (char *)s;
}

/* String literal of spaces, the same width as the timestamp. */
#define STAMP_SPACE "               "

int log_continue(const char *fmt, ...)
{
	int l;
	va_list argp;

	va_start(argp, fmt);
	l = vfprintf(stdout, fmt, argp);
	va_end(argp);

	return l;
}

static void
log_extensions(const char *name, const char *extensions)
{
	const char *p, *end;
	int l;
	int len;

	l = printf("%s:", name);
	p = extensions;
	while (*p) {
		end = strchrnul(p, ' ');
		len = end - p;
		if (l + len > 78)
			l = log_continue("\n" STAMP_SPACE "%.*s",
						len, p);
		else
			l += log_continue(" %.*s", len, p);
		for (p = end; isspace(*p); p++)
			;
	}
	printf("\n");
}


int
main(int argc, char *argv[])
{
	int maj, min;
	EGLDisplay d;
	struct wl_display *display;
	const char *str;
	EGLint n, count, i, size;
	EGLConfig *configs;
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLConfig conf = NULL;
	EGLContext ctx;
	EGLSurface egl_surface;

	display = wl_display_connect(NULL);
	assert(display);

	d = eglGetDisplay((EGLNativeDisplayType)display);

	if (!eglInitialize(d, &maj, &min)) {
		printf("eglinfo: eglInitialize failed\n");
		return 1;
	}

	printf("EGL API version: %d.%d\n", maj, min);
	printf("EGL vendor string: %s\n", eglQueryString(d, EGL_VENDOR));
	printf("EGL version string: %s\n", eglQueryString(d, EGL_VERSION));
	printf("EGL client APIs: %s\n", eglQueryString(d, EGL_CLIENT_APIS));
	printf("EGL extensions string:\n");
	str = eglQueryString(d, EGL_EXTENSIONS);
	log_extensions("EGL extensions", str ? str : "(null)");

	print_configs(d);

#if OGLES
	if(!eglBindAPI(EGL_OPENGL_ES_API)) {
		printf("eglinfo: eglBindAPI failed\n");
		return 1;
	}
#else
	if(!eglBindAPI(EGL_OPENGL_API)) {
		printf("eglinfo: eglBindAPI failed\n");
		return 1;
	}
#endif

	if (!eglGetConfigs(d, NULL, 0, &count) || count < 1) {
		printf("eglinfo: eglBindAPI failed\n");
		exit(1);
	}

	configs = calloc(count, sizeof *configs);

	if (configs == NULL)
	{
	    printf("Memory allocation failed ... \n");
	    return 1;
	}
	eglChooseConfig(d, config_attribs,
			      configs, count, &n);

	for (i = 0; i < n; i++) {
		eglGetConfigAttrib(d, configs[i], EGL_BUFFER_SIZE, &size);
		if (i == 0) {
			conf = configs[i];
			break;
		}
	}
	free(configs);
	if (conf == NULL) {
		printf("Did not find config\n");
		return 1;
	}

	egl_surface =
		eglCreateWindowSurface(d, conf, 0, NULL);

	ctx = eglCreateContext(d, conf, EGL_NO_CONTEXT, context_attribs);
	eglMakeCurrent(d, egl_surface, egl_surface, ctx);

	str = (char *)glGetString(GL_VERSION);
	printf("\nGL version: %s\n", str ? str : "(null)");

	str = (char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	printf("GLSL version: %s\n", str ? str : "(null)");

	str = (char *)glGetString(GL_VENDOR);
	printf("GL vendor: %s\n", str ? str : "(null)");

	str = (char *)glGetString(GL_RENDERER);
	printf("GL renderer: %s\n", str ? str : "(null)");

	str = (char *)glGetString(GL_EXTENSIONS);
	log_extensions("GL extensions", str ? str : "(null)");

	eglTerminate(d);
	wl_display_disconnect(display);

	return 0;
}
