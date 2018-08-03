/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Ported to GLES2.
 * Kristian Høgsberg <krh@bitplanet.net>
 * May 3, 2010
 * 
 * Improve GLES2 port:
 *   * Refactor gear drawing.
 *   * Use correct normals for surfaces.
 *   * Improve shader.
 *   * Use perspective projection transformation.
 *   * Add FPS count.
 *   * Add comments.
 * Alexandros Frantzis <alexandros.frantzis@linaro.org>
 * Jul 13, 2010
 */

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
#include "protocol/ivi-application-client-protocol.h"
#define IVI_SURFACE_ID 9000

#include "shared/platform.h"

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6

#ifndef EGL_EXT_swap_buffers_with_damage
#define EGL_EXT_swap_buffers_with_damage 1
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)(EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects);
#endif

#ifndef EGL_EXT_buffer_age
#define EGL_EXT_buffer_age 1
#define EGL_BUFFER_AGE_EXT         0x313D
#endif

/**
 * Struct describing the vertices in triangle strip
 */
struct vertex_strip {
   /** The first vertex in the strip */
   GLint first;
   /** The number of consecutive vertices in the strip after the first */
   GLint count;
};

/* Each vertex consist of GEAR_VERTEX_STRIDE GLfloat attributes */
typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

/**
 * Struct representing a gear.
 */
struct gear {
   /** The array of vertices comprising the gear */
   GearVertex *vertices;
   /** The number of vertices comprising the gear */
   int nvertices;
   /** The array of triangle strips comprising the gear */
   struct vertex_strip *strips;
   /** The number of triangle strips comprising the gear */
   int nstrips;
   /** The Vertex Buffer Object holding the vertices in the graphics card */
   GLuint vbo;
};

struct gears_data;

struct output {
   struct gears_data *gears;
   struct wl_output *output;
   struct wl_list link;
};

struct gears_data {
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;

   struct wl_egl_window *native;
   struct wl_surface *surface;

   struct ias_shell *ias_shell;
   struct zxdg_shell_v6 *shell;
   struct wl_seat *seat;
   struct wl_keyboard *keyboard;
   struct wl_shm *shm;
   struct wl_cursor_theme *cursor_theme;
   struct wl_cursor *default_cursor;
   EGLSurface egl_surface;
   struct ivi_application *ivi_application;
   struct wl_list output_list;
   struct ias_surface *shell_surface;
   struct zxdg_surface_v6 *xdg_surface;
   struct zxdg_toplevel_v6 *xdg_toplevel;
   struct ivi_surface *ivi_surface;
   bool wait_for_configure;

   PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;

   struct {
      EGLDisplay dpy;
      EGLContext ctx;
      EGLConfig conf;
   } egl;

   int32_t window_width;
   int32_t window_height;
   int32_t geometry_width;
   int32_t geometry_height;
   
   int button_down;
   int last_x, last_y;

   int fullscreen;
   int output;
   int frames;
   uint32_t last_fps;
   int buffer_size;
   int frame_sync;
   int opaque;
};

static int running = 1;

/** The view rotation [x, y, z] */
static GLfloat view_rot[3] = { 20.0, 30.0, 0.0 };
/** The gears */
static struct gear *gear1, *gear2, *gear3;
/** The current gear rotation angle */
static GLfloat angle = 0.0;
/** The location of the shader uniforms */
static GLuint ModelViewProjectionMatrix_location,
              NormalMatrix_location,
              LightSourcePosition_location,
              MaterialColor_location;
/** The projection matrix */
static GLfloat ProjectionMatrix[16];
/** The direction of the directional light for the scene */
static const GLfloat LightSourcePosition[4] = { 5.0, 5.0, 10.0, 1.0};

/** 
 * Fills a gear vertex.
 * 
 * @param v the vertex to fill
 * @param x the x coordinate
 * @param y the y coordinate
 * @param z the z coortinate
 * @param n pointer to the normal table 
 * 
 * @return the operation error code
 */
static GearVertex *
vert(GearVertex *v, GLfloat x, GLfloat y, GLfloat z, GLfloat n[3])
{
   v[0][0] = x;
   v[0][1] = y;
   v[0][2] = z;
   v[0][3] = n[0];
   v[0][4] = n[1];
   v[0][5] = n[2];

   return v + 1;
}

/**
 *  Create a gear wheel.
 * 
 *  @param inner_radius radius of hole at center
 *  @param outer_radius radius at center of teeth
 *  @param width width of gear
 *  @param teeth number of teeth
 *  @param tooth_depth depth of tooth
 *  
 *  @return pointer to the constructed struct gear
 */
static struct gear *
create_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
      GLint teeth, GLfloat tooth_depth)
{
   GLfloat r0, r1, r2;
   GLfloat da;
   GearVertex *v;
   struct gear *gear;
   double s[5], c[5];
   GLfloat normal[3];
   int cur_strip = 0;
   int i;

   /* Allocate memory for the gear */
   gear = malloc(sizeof *gear);
   if (gear == NULL)
      return NULL;

   /* Calculate the radii used in the gear */
   r0 = inner_radius;
   r1 = outer_radius - tooth_depth / 2.0;
   r2 = outer_radius + tooth_depth / 2.0;

   da = 2.0 * M_PI / teeth / 4.0;

   /* Allocate memory for the triangle strip information */
   gear->nstrips = STRIPS_PER_TOOTH * teeth;
   gear->strips = calloc(gear->nstrips, sizeof (*gear->strips));

   /* Allocate memory for the vertices */
   gear->vertices = calloc(VERTICES_PER_TOOTH * teeth, sizeof(*gear->vertices));
   v = gear->vertices;

   for (i = 0; i < teeth; i++) {
      /* Calculate needed sin/cos for varius angles */
      sincos(i * 2.0 * M_PI / teeth, &s[0], &c[0]);
      sincos(i * 2.0 * M_PI / teeth + da, &s[1], &c[1]);
      sincos(i * 2.0 * M_PI / teeth + da * 2, &s[2], &c[2]);
      sincos(i * 2.0 * M_PI / teeth + da * 3, &s[3], &c[3]);
      sincos(i * 2.0 * M_PI / teeth + da * 4, &s[4], &c[4]);

      /* A set of macros for making the creation of the gears easier */
#define  GEAR_POINT(r, da) { (r) * c[(da)], (r) * s[(da)] }
#define  SET_NORMAL(x, y, z) do { \
   normal[0] = (x); normal[1] = (y); normal[2] = (z); \
} while(0)

#define  GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign) * width * 0.5, normal)

#define START_STRIP do { \
   gear->strips[cur_strip].first = v - gear->vertices; \
} while(0);

#define END_STRIP do { \
   int _tmp = (v - gear->vertices); \
   gear->strips[cur_strip].count = _tmp - gear->strips[cur_strip].first; \
   cur_strip++; \
} while (0)

#define QUAD_WITH_NORMAL(p1, p2) do { \
   SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
   v = GEAR_VERT(v, (p1), -1); \
   v = GEAR_VERT(v, (p1), 1); \
   v = GEAR_VERT(v, (p2), -1); \
   v = GEAR_VERT(v, (p2), 1); \
} while(0)

      struct point {
         GLfloat x;
         GLfloat y;
      };

      /* Create the 7 points (only x,y coords) used to draw a tooth */
      struct point p[7] = {
         GEAR_POINT(r2, 1), // 0
         GEAR_POINT(r2, 2), // 1
         GEAR_POINT(r1, 0), // 2
         GEAR_POINT(r1, 3), // 3
         GEAR_POINT(r0, 0), // 4
         GEAR_POINT(r1, 4), // 5
         GEAR_POINT(r0, 4), // 6
      };

      /* Front face */
      START_STRIP;
      SET_NORMAL(0, 0, 1.0);
      v = GEAR_VERT(v, 0, +1);
      v = GEAR_VERT(v, 1, +1);
      v = GEAR_VERT(v, 2, +1);
      v = GEAR_VERT(v, 3, +1);
      v = GEAR_VERT(v, 4, +1);
      v = GEAR_VERT(v, 5, +1);
      v = GEAR_VERT(v, 6, +1);
      END_STRIP;

      /* Inner face */
      START_STRIP;
      QUAD_WITH_NORMAL(4, 6);
      END_STRIP;

      /* Back face */
      START_STRIP;
      SET_NORMAL(0, 0, -1.0);
      v = GEAR_VERT(v, 6, -1);
      v = GEAR_VERT(v, 5, -1);
      v = GEAR_VERT(v, 4, -1);
      v = GEAR_VERT(v, 3, -1);
      v = GEAR_VERT(v, 2, -1);
      v = GEAR_VERT(v, 1, -1);
      v = GEAR_VERT(v, 0, -1);
      END_STRIP;

      /* Outer face */
      START_STRIP;
      QUAD_WITH_NORMAL(0, 2);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(1, 0);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(3, 1);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(5, 3);
      END_STRIP;
   }

   gear->nvertices = (v - gear->vertices);

   /* Store the vertices in a vertex buffer object (VBO) */
   glGenBuffers(1, &gear->vbo);
   glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
   glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex),
         gear->vertices, GL_STATIC_DRAW);

   return gear;
}

/** 
 * Multiplies two 4x4 matrices.
 * 
 * The result is stored in matrix m.
 * 
 * @param m the first matrix to multiply
 * @param n the second matrix to multiply
 */
static void
multiply(GLfloat *m, const GLfloat *n)
{
   GLfloat tmp[16];
   const GLfloat *row, *column;
   div_t d;
   int i, j;

   for (i = 0; i < 16; i++) {
      tmp[i] = 0;
      d = div(i, 4);
      row = n + d.quot * 4;
      column = m + d.rem;
      for (j = 0; j < 4; j++)
         tmp[i] += row[j] * column[j * 4];
   }
   memcpy(m, &tmp, sizeof tmp);
}

/** 
 * Rotates a 4x4 matrix.
 * 
 * @param[in,out] m the matrix to rotate
 * @param angle the angle to rotate
 * @param x the x component of the direction to rotate to
 * @param y the y component of the direction to rotate to
 * @param z the z component of the direction to rotate to
 */
static void
rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
   double s, c;

   sincos(angle, &s, &c);
   GLfloat r[16] = {
      x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
      x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0, 
      x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
      0, 0, 0, 1
   };

   multiply(m, r);
}


/** 
 * Translates a 4x4 matrix.
 * 
 * @param[in,out] m the matrix to translate
 * @param x the x component of the direction to translate to
 * @param y the y component of the direction to translate to
 * @param z the z component of the direction to translate to
 */
static void
translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
   GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  x, y, z, 1 };

   multiply(m, t);
}

/** 
 * Creates an identity 4x4 matrix.
 * 
 * @param m the matrix make an identity matrix
 */
static void
identity(GLfloat *m)
{
   GLfloat t[16] = {
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 1.0,
   };

   memcpy(m, t, sizeof(t));
}

/** 
 * Transposes a 4x4 matrix.
 *
 * @param m the matrix to transpose
 */
static void 
transpose(GLfloat *m)
{
   GLfloat t[16] = {
      m[0], m[4], m[8],  m[12],
      m[1], m[5], m[9],  m[13],
      m[2], m[6], m[10], m[14],
      m[3], m[7], m[11], m[15]};

   memcpy(m, t, sizeof(t));
}

/**
 * Inverts a 4x4 matrix.
 *
 * This function can currently handle only pure translation-rotation matrices.
 * Read http://www.gamedev.net/community/forums/topic.asp?topic_id=425118
 * for an explanation.
 */
static void
invert(GLfloat *m)
{
   GLfloat t[16];
   identity(t);

   // Extract and invert the translation part 't'. The inverse of a
   // translation matrix can be calculated by negating the translation
   // coordinates.
   t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];

   // Invert the rotation part 'r'. The inverse of a rotation matrix is
   // equal to its transpose.
   m[12] = m[13] = m[14] = 0;
   transpose(m);

   // inv(m) = inv(r) * inv(t)
   multiply(m, t);
}

/** 
 * Calculate a perspective projection transformation.
 * 
 * @param m the matrix to save the transformation in
 * @param fovy the field of view in the y direction
 * @param aspect the view aspect ratio
 * @param zNear the near clipping plane
 * @param zFar the far clipping plane
 */
static void
perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
   GLfloat tmp[16];
   identity(tmp);

   double sine, cosine, cotangent, deltaZ;
   GLfloat radians = fovy / 2 * M_PI / 180;

   deltaZ = zFar - zNear;
   sincos(radians, &sine, &cosine);

   if ((deltaZ == 0) || (sine == 0) || (aspect == 0))
      return;

   cotangent = cosine / sine;

   tmp[0] = cotangent / aspect;
   tmp[5] = cotangent;
   tmp[10] = -(zFar + zNear) / deltaZ;
   tmp[11] = -1;
   tmp[14] = -2 * zNear * zFar / deltaZ;
   tmp[15] = 0;

   memcpy(m, tmp, sizeof(tmp));
}

/**
 * Draws a gear.
 *
 * @param gear the gear to draw
 * @param transform the current transformation matrix
 * @param x the x position to draw the gear at
 * @param y the y position to draw the gear at
 * @param angle the rotation angle of the gear
 * @param color the color of the gear
 */
static void
draw_gear(struct gear *gear, GLfloat *transform,
      GLfloat x, GLfloat y, GLfloat angle, const GLfloat color[4])
{
   GLfloat model_view[16];
   GLfloat normal_matrix[16];
   GLfloat model_view_projection[16];

   /* Translate and rotate the gear */
   memcpy(model_view, transform, sizeof (model_view));
   translate(model_view, x, y, 0);
   rotate(model_view, 2 * M_PI * angle / 360.0, 0, 0, 1);

   /* Create and set the ModelViewProjectionMatrix */
   memcpy(model_view_projection, ProjectionMatrix, sizeof(model_view_projection));
   multiply(model_view_projection, model_view);

   glUniformMatrix4fv(ModelViewProjectionMatrix_location, 1, GL_FALSE,
                      model_view_projection);

   /* 
    * Create and set the NormalMatrix. It's the inverse transpose of the
    * ModelView matrix.
    */
   memcpy(normal_matrix, model_view, sizeof (normal_matrix));
   invert(normal_matrix);
   transpose(normal_matrix);
   glUniformMatrix4fv(NormalMatrix_location, 1, GL_FALSE, normal_matrix);

   /* Set the gear color */
   glUniform4fv(MaterialColor_location, 1, color);

   /* Set the vertex buffer object to use */
   glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);

   /* Set up the position of the attributes in the vertex buffer object */
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
         6 * sizeof(GLfloat), NULL);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
         6 * sizeof(GLfloat), (GLfloat *) 0 + 3);

   /* Enable the attributes */
   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);

   /* Draw the triangle strips that comprise the gear */
   int n;
   for (n = 0; n < gear->nstrips; n++)
      glDrawArrays(GL_TRIANGLE_STRIP, gear->strips[n].first, gear->strips[n].count);

   /* Disable the attributes */
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(0);
}

/** 
 * Draws the gears.
 */
static void
gears_draw(struct gears_data* gears)
{
   static const GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
   static const GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
   static const GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };
   GLfloat transform[16];
   identity(transform);
   EGLint rect[4];
   EGLint buffer_age = 0;
	struct wl_region *region;

   if (gears->swap_buffers_with_damage)
      eglQuerySurface(gears->egl.dpy, gears->egl_surface,
         EGL_BUFFER_AGE_EXT, &buffer_age);

   glClearColor(0.0, 0.0, 0.0, 0.5);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   /* Translate and rotate the view */
   translate(transform, 0, 0, -20);
   rotate(transform, 2 * M_PI * view_rot[0] / 360.0, 1, 0, 0);
   rotate(transform, 2 * M_PI * view_rot[1] / 360.0, 0, 1, 0);
   rotate(transform, 2 * M_PI * view_rot[2] / 360.0, 0, 0, 1);

   /* Draw the gears */
   draw_gear(gear1, transform, -3.0, -2.0, angle, red);
   draw_gear(gear2, transform, 3.1, -2.0, -2 * angle - 9.0, green);
   draw_gear(gear3, transform, -3.1, 4.2, -2 * angle - 25.0, blue);

	if (gears->opaque || gears->fullscreen) {
      region = wl_compositor_create_region(gears->compositor);
      wl_region_add(region, 0, 0,
               gears->geometry_width,
               gears->geometry_height);
      wl_surface_set_opaque_region(gears->surface, region);
      wl_region_destroy(region);
      region = NULL;
   } else {
		wl_surface_set_opaque_region(gears->surface, NULL);
   }
   
   if (gears->swap_buffers_with_damage && buffer_age > 0) {
      rect[0] = gears->geometry_width / 4 - 1;
      rect[1] = gears->geometry_height / 4 - 1;
      rect[2] = gears->geometry_width / 2 + 2;
      rect[3] = gears->geometry_height / 2 + 2;
      gears->swap_buffers_with_damage(gears->egl.dpy,
                    gears->egl_surface,
                    rect, 1);
   } else {
      eglSwapBuffers(gears->egl.dpy, gears->egl_surface);
   }
}

/** 
 * Handles a new window size or exposure.
 * 
 * @param width the window width
 * @param height the window height
 */
static void
gears_reshape(int width, int height)
{
   /* Update the projection matrix */
   perspective(ProjectionMatrix, 60.0, width / (float)height, 1.0, 1024.0);

   /* Set the viewport */
   glViewport(0, 0, (GLint) width, (GLint) height);
   printf("Width = %d, Height = %d\n", width, height);
}

/** 
 * Handles special eglut events.
 * 
 * @param special the event to handle.
 */
static void
gears_special(uint32_t key)
{
  switch (key) {
      case KEY_LEFT:
         view_rot[1] += 5.0;
         break;
      case KEY_RIGHT:
         view_rot[1] -= 5.0;
         break;
      case KEY_UP:
         view_rot[0] += 5.0;
         break;
      case KEY_DOWN:
         view_rot[0] -= 5.0;
         break;
   }
}

static void
gears_idle(struct gears_data* gears)
{
   struct timeval tv;
   static double tRot0 = -1.0, tRate0 = -1.0;
   double dt, t;

   gettimeofday(&tv, NULL);
   t = tv.tv_sec + tv.tv_usec / 1000000.0;

   if (tRot0 < 0.0)
      tRot0 = t;
   dt = t - tRot0;
   tRot0 = t;

   /* advance rotation for next frame */
   angle += 70.0 * dt;  /* 70 degrees per second */
   if (angle > 3600.0)
      angle -= 3600.0;

   gears->frames++;

   if (tRate0 < 0.0)
      tRate0 = t;
   if (t - tRate0 >= 5.0) {
      GLfloat seconds = t - tRate0;
      GLfloat fps = gears->frames / seconds;
      printf("%d frames in %3.1f seconds = %6.3f FPS\n", gears->frames, seconds,
            fps);
      tRate0 = t;
      gears->frames = 0;
   }
}

static const char vertex_shader[] =
"attribute vec3 position;\n"
"attribute vec3 normal;\n"
"\n"
"uniform mat4 ModelViewProjectionMatrix;\n"
"uniform mat4 NormalMatrix;\n"
"uniform vec4 LightSourcePosition;\n"
"uniform vec4 MaterialColor;\n"
"\n"
"varying vec4 Color;\n"
"\n"
"void main(void)\n"
"{\n"
"    // Transform the normal to eye coordinates\n"
"    vec3 N = normalize(vec3(NormalMatrix * vec4(normal, 1.0)));\n"
"\n"
"    // The LightSourcePosition is actually its direction for directional light\n"
"    vec3 L = normalize(LightSourcePosition.xyz);\n"
"\n"
"    // Multiply the diffuse value by the vertex color (which is fixed in this case)\n"
"    // to get the actual color that we will use to draw this vertex with\n"
"    float diffuse = max(dot(N, L), 0.0);\n"
"    Color = diffuse * MaterialColor;\n"
"\n"
"    // Transform the position to clip coordinates\n"
"    gl_Position = ModelViewProjectionMatrix * vec4(position, 1.0);\n"
"}";

static const char fragment_shader[] =
"precision mediump float;\n"
"varying vec4 Color;\n"
"\n"
"void main(void)\n"
"{\n"
"    gl_FragColor = Color;\n"
"}";

static void
gears_init(struct gears_data* gears)
{
   GLuint v, f, program;
   const char *p;
   char msg[512];

   glEnable(GL_CULL_FACE);
   glEnable(GL_DEPTH_TEST);

   /* Compile the vertex shader */
   p = vertex_shader;
   v = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(v, 1, &p, NULL);
   glCompileShader(v);
   glGetShaderInfoLog(v, sizeof msg, NULL, msg);
   printf("vertex shader info: %s\n", msg);

   /* Compile the fragment shader */
   p = fragment_shader;
   f = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(f, 1, &p, NULL);
   glCompileShader(f);
   glGetShaderInfoLog(f, sizeof msg, NULL, msg);
   printf("fragment shader info: %s\n", msg);

   /* Create and link the shader program */
   program = glCreateProgram();
   glAttachShader(program, v);
   glAttachShader(program, f);
   glBindAttribLocation(program, 0, "position");
   glBindAttribLocation(program, 1, "normal");

   glLinkProgram(program);
   glGetProgramInfoLog(program, sizeof msg, NULL, msg);
   printf("info: %s\n", msg);

   /* Enable the shaders */
   glUseProgram(program);

   /* Get the locations of the uniforms so we can access them */
   ModelViewProjectionMatrix_location = glGetUniformLocation(program, "ModelViewProjectionMatrix");
   NormalMatrix_location = glGetUniformLocation(program, "NormalMatrix");
   LightSourcePosition_location = glGetUniformLocation(program, "LightSourcePosition");
   MaterialColor_location = glGetUniformLocation(program, "MaterialColor");

   /* Set the LightSourcePosition uniform which is constant throught the program */
   glUniform4fv(LightSourcePosition_location, 1, LightSourcePosition);

   /* make the gears */
   gear1 = create_gear(1.0, 4.0, 1.0, 20, 0.7);
   gear2 = create_gear(0.5, 2.0, 2.0, 10, 0.7);
   gear3 = create_gear(1.3, 2.0, 0.5, 10, 0.7);
}

static struct output *
get_default_output(struct gears_data *gears)
{
   struct output *iter;
   int counter = 0;
   wl_list_for_each(iter, &gears->output_list, link) {
      if(counter++ == gears->output)
         return iter;
   }

   // Unreachable, but avoids compiler warning
   return NULL;
}

static void
init_egl(struct gears_data *gears)
{
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
      EGL_DEPTH_SIZE, 24,
      EGL_NONE
   };

   EGLint major, minor, n, count, i, size;
   EGLConfig *configs;
   EGLBoolean ret;

   if (gears->opaque || gears->buffer_size == 16)
      config_attribs[9] = 0;

   gears->egl.dpy =
      weston_platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR,
                  gears->display, NULL);
   assert(gears->egl.dpy);

   ret = eglInitialize(gears->egl.dpy, &major, &minor);
   assert(ret == EGL_TRUE);
   ret = eglBindAPI(EGL_OPENGL_ES_API);
   assert(ret == EGL_TRUE);

   if (!eglGetConfigs(gears->egl.dpy, NULL, 0, &count) || count < 1)
      assert(0);

   configs = calloc(count, sizeof *configs);
   assert(configs);

   ret = eglChooseConfig(gears->egl.dpy, config_attribs,
               configs, count, &n);
   assert(ret && n >= 1);

   for (i = 0; i < n; i++) {
      eglGetConfigAttrib(gears->egl.dpy,
               configs[i], EGL_BUFFER_SIZE, &size);
      if (gears->buffer_size == size) {
         gears->egl.conf = configs[i];
         break;
      }
   }
   free(configs);
   if (gears->egl.conf == NULL) {
      fprintf(stderr, "did not find config with buffer size %d\n",
         gears->buffer_size);
      exit(EXIT_FAILURE);
   }

   gears->egl.ctx = eglCreateContext(gears->egl.dpy,
                   gears->egl.conf,
                   EGL_NO_CONTEXT, context_attribs);
   assert(gears->egl.ctx);

   gears->swap_buffers_with_damage = NULL;
   extensions = eglQueryString(gears->egl.dpy, EGL_EXTENSIONS);
   if (extensions &&
       strstr(extensions, "EGL_EXT_swap_buffers_with_damage") &&
       strstr(extensions, "EGL_EXT_buffer_age"))
      gears->swap_buffers_with_damage =
         (PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)
         eglGetProcAddress("eglSwapBuffersWithDamageEXT");

   if (gears->swap_buffers_with_damage)
      printf("has EGL_EXT_buffer_age and EGL_EXT_swap_buffers_with_damage\n");

}

static void
fini_egl(struct gears_data *gears)
{
   eglTerminate(gears->egl.dpy);
   eglReleaseThread();
}

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
   struct gears_data *gears = data;

   if (key == KEY_F11 && state) {
      if (gears->shell) {
         if (gears->fullscreen)
            zxdg_toplevel_v6_unset_fullscreen(gears->xdg_toplevel);
         else
            zxdg_toplevel_v6_set_fullscreen(gears->xdg_toplevel, NULL);
      } else if (gears->ias_shell) {
         if (gears->fullscreen) {
            ias_surface_set_fullscreen(gears->shell_surface,
                  get_default_output(gears)->output);
         } else {
            ias_surface_unset_fullscreen(gears->shell_surface,
               gears->window_width, gears->window_height);
            ias_shell_set_zorder(gears->ias_shell,
                  gears->shell_surface, 0);
         }
      }
   } else if (key == KEY_ESC && state) {
      running = 0;
   } else {
      gears_special(key);
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
handle_surface_configure(void *data, struct zxdg_surface_v6 *surface,
			 uint32_t serial)
{

   struct gears_data *gears = data;

   zxdg_surface_v6_ack_configure(surface, serial);

   gears->wait_for_configure = false;
}

static const struct zxdg_surface_v6_listener xdg_surface_listener = {
   handle_surface_configure,
};

static void
handle_toplevel_configure(void *data, struct zxdg_toplevel_v6 *surface,
          int32_t width, int32_t height,
          struct wl_array *states)
{
   struct gears_data *gears = data;
   uint32_t *p;

   gears->fullscreen = 0;
   wl_array_for_each(p, states) {
      uint32_t state = *p;
      switch (state) {
      case ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN:
         gears->fullscreen = 1;
         break;
      }
   }

   if (width > 0 && height > 0) {
		if (!gears->fullscreen) {
         gears->window_width = width;
         gears->window_height = height;
      }
		gears->geometry_width = width;
		gears->geometry_height = height;
	} else if (!gears->fullscreen) {
		gears->geometry_width = gears->window_width;
		gears->geometry_height = gears->window_height;
   }

   if (gears->native)
      wl_egl_window_resize(gears->native,
                 gears->geometry_width,
                 gears->geometry_height, 0, 0);

   gears_reshape(gears->geometry_width, gears->geometry_height);
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
   struct gears_data *gears = data;

   if (gears->native)
      wl_egl_window_resize(gears->native, width, height, 0, 0);

   gears->geometry_width = width;
   gears->geometry_height = height;
	if (!gears->fullscreen) {
      gears->window_width = gears->geometry_width;
      gears->window_height = gears->geometry_height;
   }
   gears_reshape(gears->geometry_width, gears->geometry_height);
}

static struct ias_surface_listener ias_surface_listener = {
   ias_handle_ping,
   ias_handle_configure,
};

static void
handle_ivi_surface_configure(void *data, struct ivi_surface *ivi_surface,
                             int32_t width, int32_t height)
{
   struct gears_data *gears = data;

   wl_egl_window_resize(gears->native, width, height, 0, 0);

   gears->geometry_width = width;
   gears->geometry_height = height;
	if (!gears->fullscreen) {
      gears->window_width = gears->geometry_width;
      gears->window_height = gears->geometry_height;
   }
   gears_reshape(width, height);
}

static const struct ivi_surface_listener ivi_surface_listener = {
   handle_ivi_surface_configure,
};

static void
create_xdg_surface(struct gears_data *gears)
{
   printf("create_xdg_surface\n");
   gears->xdg_surface = zxdg_shell_v6_get_xdg_surface(gears->shell,
                     gears->surface);

   zxdg_surface_v6_add_listener(gears->xdg_surface,
             &xdg_surface_listener, gears);

   gears->xdg_toplevel =
	zxdg_surface_v6_get_toplevel(gears->xdg_surface);
   zxdg_toplevel_v6_add_listener(gears->xdg_toplevel,
				 &xdg_toplevel_listener, gears);

   zxdg_toplevel_v6_set_title(gears->xdg_toplevel, "es2gears");

   gears->wait_for_configure = true;
   wl_surface_commit(gears->surface);
}

static void
create_ivi_surface(struct gears_data *gears)
{
   uint32_t id_ivisurf = IVI_SURFACE_ID + (uint32_t)getpid();
   printf("create_ivi_surface\n");
   gears->ivi_surface =
      ivi_application_surface_create(gears->ivi_application,
                      id_ivisurf, gears->surface);

   if (gears->ivi_surface == NULL) {
      fprintf(stderr, "Failed to create ivi_client_surface\n");
      abort();
   }

   ivi_surface_add_listener(gears->ivi_surface,
             &ivi_surface_listener, gears);
}

static void
create_ias_surface(struct gears_data *gears)
{
   printf("create_ias_surface\n");
   gears->shell_surface = ias_shell_get_ias_surface(gears->ias_shell,
         gears->surface, "es2gears");
   ias_surface_add_listener(gears->shell_surface,
         &ias_surface_listener, gears);

    if (gears->fullscreen) {
       ias_surface_set_fullscreen(gears->shell_surface,
             get_default_output(gears)->output);
    } else {
      ias_surface_unset_fullscreen(gears->shell_surface,
         gears->window_width, gears->window_height);
      ias_shell_set_zorder(gears->ias_shell,
         gears->shell_surface, 0);
   }
}

static void
create_surface(struct gears_data *gears)
{
   EGLBoolean ret;

   gears->surface = wl_compositor_create_surface(gears->compositor);

   gears->native =
      wl_egl_window_create(gears->surface,
                 gears->geometry_width,
                 gears->geometry_height);
   gears->egl_surface =
      weston_platform_create_egl_surface(gears->egl.dpy,
                     gears->egl.conf,
                     gears->native, NULL);


   if (gears->shell) {
      create_xdg_surface(gears);
   } else if (gears->ivi_application ) {
      create_ivi_surface(gears);
   } else if (gears->ias_shell) {
      create_ias_surface(gears);
   }
   else {
      assert(0);
   }

   ret = eglMakeCurrent(gears->egl.dpy, gears->egl_surface,
              gears->egl_surface, gears->egl.ctx);
   assert(ret == EGL_TRUE);

   if (!gears->frame_sync)
      eglSwapInterval(gears->egl.dpy, 0);
}

static void
destroy_surface(struct gears_data *gears)
{
   /* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
    * on eglReleaseThread(). */
   eglMakeCurrent(gears->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
             EGL_NO_CONTEXT);

   eglDestroySurface(gears->egl.dpy, gears->egl_surface);
   wl_egl_window_destroy(gears->native);

   if (gears->xdg_toplevel)
      zxdg_toplevel_v6_destroy(gears->xdg_toplevel);
   if (gears->xdg_surface)
      zxdg_surface_v6_destroy(gears->xdg_surface);
   if (gears->ivi_application)
      ivi_surface_destroy(gears->ivi_surface);
   if (gears->ias_shell) {
      ias_surface_destroy(gears->shell_surface);
   }
   wl_surface_destroy(gears->surface);
}


static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
          enum wl_seat_capability caps)
{
   struct gears_data *gears = data;
   if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !gears->keyboard) {
      gears->keyboard = wl_seat_get_keyboard(seat);
      wl_keyboard_add_listener(gears->keyboard, &keyboard_listener,
         gears);
   } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) &&
      gears->keyboard) {
      wl_keyboard_destroy(gears->keyboard);
      gears->keyboard = NULL;
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
display_add_output(struct gears_data *gears, uint32_t id)
{
   struct output *output;

   output = malloc(sizeof *output);
   if (output == NULL)
      return;

   memset(output, 0, sizeof *output);
   output->gears = gears;
   output->output =
      wl_registry_bind(gears->registry, id, &wl_output_interface, 1);
   wl_list_insert(gears->output_list.prev, &output->link);
}


static void
registry_handle_global(void *data, struct wl_registry *registry,
             uint32_t name, const char *interface, uint32_t version)
{
   struct gears_data *gears = data;

   if (strcmp(interface, "wl_compositor") == 0) {
      gears->compositor =
         wl_registry_bind(registry, name,
                &wl_compositor_interface, 1);
   } else if (strcmp(interface, "zxdg_shell_v6") == 0) {
      if (!gears->ias_shell) {
         gears->shell = wl_registry_bind(registry, name,
                     &zxdg_shell_v6_interface, 1);
         zxdg_shell_v6_add_listener(gears->shell, &xdg_shell_listener,
            gears);
      }
   } else if (strcmp(interface, "ias_shell") == 0) {
      if (!gears->shell) {
         gears->ias_shell = wl_registry_bind(registry, name,
               &ias_shell_interface, 1);
      }
   } else if (strcmp(interface, "wl_seat") == 0) {
      gears->seat = wl_registry_bind(registry, name,
                  &wl_seat_interface, 1);
      wl_seat_add_listener(gears->seat, &seat_listener, gears);
   } else if (strcmp(interface, "wl_output") == 0) {
      display_add_output(gears, name);
   } else if (strcmp(interface, "wl_shm") == 0) {
      gears->shm = wl_registry_bind(registry, name,
                 &wl_shm_interface, 1);
      gears->cursor_theme = wl_cursor_theme_load(NULL, 32, gears->shm);
      if (!gears->cursor_theme) {
         fprintf(stderr, "unable to load default theme\n");
         return;
      }
      gears->default_cursor =
         wl_cursor_theme_get_cursor(gears->cursor_theme, "left_ptr");
      if (!gears->default_cursor) {
         fprintf(stderr, "unable to load default left pointer\n");
         // TODO: abort ?
      }
   } else if (strcmp(interface, "ivi_application") == 0) {
      gears->ivi_application =
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
usage(int error_code)
{
   fprintf(stderr, "Usage: es2gears [OPTIONS]\n\n"
      "  -f\tRun in fullscreen mode\n"
		"  -o\tCreate an opaque surface\n"
		"  -out <n>\tChoose output n\n"
      "  -b\tDon't sync to compositor redraw (eglSwapInterval 0)\n"
      "  -h\tThis help text\n\n");

   exit(error_code);
}

int
main(int argc, char **argv)
{
   int i;
   struct gears_data gears = {0};
   int ret = 0;
   struct output *iter, *next;
   
   gears.geometry_width = 450;
   gears.geometry_height = 500;
   gears.buffer_size = 32;
   gears.frame_sync = 1;
   gears.window_width = gears.geometry_width;
   gears.window_height = gears.geometry_height;
   
   for (i = 1; i < argc; i++) {
      if (strcmp("-f", argv[i]) == 0)
         gears.fullscreen = 1;
		else if (strcmp("-o", argv[i]) == 0)
			gears.opaque = 1;
      else if (strcmp("-out", argv[i]) == 0)
         gears.output = atoi(argv[++i]);
      else if (strcmp("-b", argv[i]) == 0)
         gears.frame_sync = 0;
      else if (strcmp("-h", argv[i]) == 0)
         usage(EXIT_SUCCESS);
      else
         usage(EXIT_FAILURE);
   }

   gears.display = wl_display_connect(NULL);
   assert(gears.display);
   wl_list_init(&gears.output_list);

   gears.registry = wl_display_get_registry(gears.display);
   wl_registry_add_listener(gears.registry,
             &registry_listener, &gears);

   wl_display_dispatch(gears.display);

   init_egl(&gears);
   create_surface(&gears);

   gears_init(&gears);

   /* The mainloop here is a little subtle.  Redrawing will cause
    * EGL to read events so we can just call
    * wl_display_dispatch_pending() to handle any events that got
    * queued up as a side effect. */
   while (running && ret != -1) {
      gears_idle(&gears);
      wl_display_dispatch_pending(gears.display);
      gears_draw(&gears);
   }

   printf("es2gears exiting\n");

   destroy_surface(&gears);
   fini_egl(&gears);

   wl_registry_destroy(gears.registry);

   wl_list_for_each_safe(iter, next, &gears.output_list, link) {
      wl_list_remove(&iter->link);
      free(iter);
   }

   wl_display_flush(gears.display);
   wl_display_disconnect(gears.display);

   return 0;
}
