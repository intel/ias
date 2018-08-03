/*
 *-----------------------------------------------------------------------------
 * Filename: grid-layout.c
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
 *
 *-----------------------------------------------------------------------------
 * Description:
 *   Grid layout plugin for the Intel Automotive Solutions shell.
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <linux/input.h>
#include <GLES2/gl2.h>
#include "ias-plugin-manager.h"

static ias_identifier myid;

static const char *vert_shader_text =
    "uniform mat4 proj;\n"
	"uniform highp int tilenum;\n"
	"attribute vec2 pos;\n"
	"attribute vec2 texcoord;\n"
	"varying vec2 v_texcoord;\n"
	"\n"
	"void main() {\n"
	"  float t = float(tilenum);\n"
	"  vec2 p;\n"
	"  p = pos + vec2(100.0 + 450.0*mod(t, 2.0),\n"
	"                 100.0 + 450.0*floor(t/2.0));\n"
	"  gl_Position = proj * vec4(p, 0.0, 1.0);\n"
	"  //v_texcoord = texcoord;\n"
	"  v_texcoord = vec2(pos.x / 400.0, pos.y / 400.0);\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"uniform highp int tilenum;\n"
	"uniform int graytile;\n"
	"uniform float opacity;\n"
	"uniform int timedout;\n"
	"void main() {\n"
	"  if (v_texcoord.x < 0.0 || v_texcoord.x > 1.0 ||\n"
	"      v_texcoord.y < 0.0 || v_texcoord.y > 1.0) {\n"
	"    discard;\n"
	"  }\n"
	"  if (graytile == 0) {\n"
	"    gl_FragColor = texture2D(tex, v_texcoord)\n;"
	"  } else {\n"
	"    gl_FragColor = vec4(0.25, 0.25, 0.25, 1.0);\n"
	"  }\n"
	"  if (timedout == 1) {\n"
	"    gl_FragColor = mix(vec4(1.0, 1.0, 1.0, 1.0), gl_FragColor, 0.5);\n"
	"  }\n"
	"  gl_FragColor = mix(vec4(0.2, 0.2, 0.2, 1.0), gl_FragColor, opacity);\n"
	"}\n";

static GLuint program;
static GLuint proj_uniform, tile_uniform, gray_uniform;
static GLuint tex_uniform, opacity_uniform, timedout_uniform;
static GLuint vbo;
static GLuint ibo;

static int mouse_x, mouse_y;
static int selected_tile = -1;

static struct weston_compositor *compositor;

static GLuint
create_shader(const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);

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
		return 0;
	}

	return shader;
}

static void
camera_switch_to(struct weston_output *output)
{
	/* Lock a sprite */
	/* Turn on FbBlendOvl sysfs */ 
	/* save the current surface/egl_surface */
	/* allocate ARGB surface and wrap a egl surface */

	/* lock a sprite */
	/* get the list of ias_sprite, based on the crtc, 
 	 * how to check if it is available */
	

}

static void
camera_switch_from(struct weston_output *output)
{
   /* Unlock a sprite */

}



static int
use_for_camera(struct weston_surface *surface)
{
	/*
	 * camera application/this plugin will set
	 * surface to CAMERA_SURFACE.  Only process surface with this 
	 * behavior and ignore all others
	 *
	 */

	if (ias_get_behavior_bits(surface) == CAMERA_SURFACE) {
		return 1;
	} else {
		return 0;
	}
}

static void
grid_draw(struct weston_output *output)
{
	struct weston_compositor *compositor = output->compositor;
	struct weston_surface *surface;
	int i;
	static const GLfloat texv[] = {
		0.0, 0.0,
		0.0, 1.0,
		1.0, 1.0,
		1.0, 0.0
	};

	static uint32_t frame = 0;

	glViewport(0, 0, output->current->width, output->current->height);

	frame++;

	glClearColor(0.2, 0.2, 0.2, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(program);

	/*
	 * Pass the output's projection matrix which converts us to the screen's
	 * coordinate system.
	 */
	glUniformMatrix4fv(proj_uniform, 1, GL_FALSE, output->matrix.d);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	/* Buffer format is vert_x, vert_y, tex_x, tex_y */
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), (GLvoid*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), (GLvoid*)2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	i = 0;
	wl_list_for_each(surface, &compositor->surface_list, link) {
		if (!use_for_camera(surface)) {
			continue;
		}

		if (i >= 4) {
			break;
		}

		/* Draw the reverse guide */


		glBindTexture(GL_TEXTURE_2D, surface->textures[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glUniform1i(tile_uniform, i);
		glUniform1i(gray_uniform, 0);
		glUniform1i(tex_uniform, 0);
		glUniform1i(timedout_uniform, ias_has_surface_timedout(surface));
		glUniform1f(opacity_uniform, (i == selected_tile) ?
				abs(60.0 - (float)(frame%120)) / 60.0 :
				1.0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		i++;
	}

	/*
	 * Draw gray boxes for any remaining tiles if we didn't have a full
	 * four clients.
	 */
	for ( ; i < 4; i++) {
		glUniform1i(tile_uniform, i);
		glUniform1i(gray_uniform, 1);
		glUniform1i(timedout_uniform, 0);
		glUniform1f(opacity_uniform, (i == selected_tile) ?
				abs(60.0 - (float)(frame%120)) / 60.0 :
				1.0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
	}

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
}

/***
 *** Input handlers
 ***/

static void
grid_grab_focus(struct wl_pointer_grab *base,
		struct wl_surface *surface,
		int32_t x,
		int32_t y)
{

}

static void
grid_grab_motion(struct wl_pointer_grab *grab,
			uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	/* Update our internal idea of where the mouse is */
	mouse_x = wl_fixed_to_int(x);
	mouse_y = wl_fixed_to_int(y);
}

static void
grid_grab_button(struct wl_pointer_grab *grab,
		uint32_t time,
		uint32_t button,
		uint32_t state)
{
	/* Only pay attention to press events; ignore release */
	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}

	/* Figure out which tile we clicked on, if any */
	if (mouse_x >= 100 && mouse_x <= 500 &&
			mouse_y >= 100 && mouse_y <= 500)
	{
		selected_tile = 0;
	} else if (mouse_x >= 550 && mouse_x <= 950 &&
			mouse_y >= 100 && mouse_y <= 500)
	{
		selected_tile = 1;
	} else if (mouse_x >= 100 && mouse_x <= 500 &&
			mouse_y >= 550 && mouse_y <= 950)
	{
		selected_tile = 2;
	} else if (mouse_x >= 550 && mouse_x <= 950 &&
			mouse_y >= 550 && mouse_y <= 950)
	{
		selected_tile = 3;
	} else {
		selected_tile = -1;
	}
}

static void
grid_grab_pointer_cancel(spug_pointer_grab *grab)
{
	/* Noop */
}

static void
grid_grab_axis(struct spug_pointer_grab *grab,
		  uint32_t time,
		  struct weston_pointer_axis_event *event)
{
    /* TODO */
}

static void
grid_grab_axis_source(struct spug_pointer_grab *grab, uint32_t source)
{
    /* TODO */
}

static void
grid_grab_frame(struct spug_pointer_grab *grab)
{
    /* TODO */
}

static struct wl_pointer_grab_interface mouse_grab_interface = {
	grid_grab_focus,
	grid_grab_motion,
	grid_grab_button,
	grid_grab_axis,
	grid_grab_axis_source,
	grid_grab_frame,
	grid_grab_pointer_cancel
};


static void
grid_grab_key(struct wl_keyboard_grab *grab,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	int i = 0;
	struct weston_surface *surface;
	struct weston_output *output;

	/* If no tile is selected, then none of the arrow keys do anything */
	if (selected_tile == -1 && key != KEY_ENTER && key != KEY_SPACE) {
		return;
	}

	/* Only deal with key down events.  Ignore repeat & up events */
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}

	switch (key) {
	case KEY_LEFT:
	case KEY_RIGHT:
		selected_tile ^= 1;
		break;
	case KEY_UP:
	case KEY_DOWN:
		selected_tile ^= 2;
		break;
	case KEY_TAB:
		selected_tile++;
		selected_tile %= 4;
		break;
	case KEY_ENTER:
		/* Set focus to selected window and deactivate plugin */
		if (selected_tile >= 0) {
			wl_list_for_each(surface, &compositor->surface_list, link) {
				if (!use_for_camera(surface)) {
					continue;
				}
				if (i++ == selected_tile) {
					weston_seat_set_keyboard_focus(compositor->seat, surface);
					break;
				}
			}
		}

		/* Deactivate plugin on all outputs */
		wl_list_for_each(output, &compositor->output_list, link) {
			ias_deactivate_plugin(output);
		}

		break;
	case KEY_SPACE:
		/* Suspend/unsuspend frame events to this surface on spacebar */
		if (selected_tile >= 0) {
			wl_list_for_each(surface, &compositor->surface_list, link) {
				if (!use_for_camera(surface)) {
					continue;
				}
				if (i++ == selected_tile) {
					ias_toggle_frame_events(surface);
					break;
				}
			}
		}

		break;
	default:
		/* Ignore other keys */
		;
	}
}

static void
grid_grab_modifiers(struct wl_keyboard_grab *grab,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group)
{
	/* Noop */
}

static struct wl_keyboard_grab_interface key_grab_interface = {
	grid_grab_key,
	grid_grab_modifiers,
};


/***
 *** Plugin initialization
 ***/

WL_EXPORT int
ias_plugin_init(struct ias_plugin_info *info,
		ias_identifier id,
		uint32_t version,
		struct weston_compositor *ec)
{
	GLuint frag, vert;
	GLint status;

	/* Save compositor reference */
	compositor = ec;

	/* First two components of each line are vertex x,y.  Second two are tex */
	static const GLfloat verts[][4] = {
		{ 0,     0,  /**/  0.0, 0.0 },
		{ 0,   400,  /**/  0.0, 1.0 },
		{ 400, 400,  /**/  1.0, 1.0 },
		{ 400,   0,  /**/  1.0, 0.0 },
	};
	static const GLushort indices[] = {
		0, 1, 2,
		2, 3, 0,
	};

	myid = id;

	/*
	 * This plugin is written for inforec version 1, so that's all we fill
	 * in, regardless of what gets passed in for the version parameter.
	 */
	info->draw = grid_draw;
	info->mouse_grab = &mouse_grab_interface;
	info->key_grab = &key_grab_interface;
	info->switch_to = camera_switch_to;
	info->switch_from = camera_switch_from;

	frag = create_shader(frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(vert_shader_text, GL_VERTEX_SHADER);

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
		return 1;
	}

	glBindAttribLocation(program, 0, "pos");

	proj_uniform = glGetUniformLocation(program, "proj");
	tile_uniform = glGetUniformLocation(program, "tilenum");
	gray_uniform = glGetUniformLocation(program, "graytile");
	tex_uniform = glGetUniformLocation(program, "tex");
	opacity_uniform = glGetUniformLocation(program, "opacity");
	timedout_uniform = glGetUniformLocation(program, "timedout");

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	return 0;
}
