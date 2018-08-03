/*
 *-----------------------------------------------------------------------------
 * Filename: surface_gbc_control.c
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
 *   Sample Surface Gamma, Brightness and Contrast Control plugin
 *-----------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <linux/input.h>
#include <GLES2/gl2.h>
#include "ias-plugin-framework.h"

static ias_identifier myid;
struct weston_seat *seat;

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
	"precision highp float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"uniform highp int tilenum;\n"
	"uniform int graytile;\n"
	"uniform float opacity;\n"
	"uniform int timedout;\n"
    "uniform float brightness;\n"
	"uniform float contrast;\n"
	"uniform float gamma;\n"
	"void main() {\n"
	"	vec4 color; \n"
	"  if (v_texcoord.x < 0.0 || v_texcoord.x > 1.0 ||\n"
	"      v_texcoord.y < 0.0 || v_texcoord.y > 1.0) {\n"
	"    discard;\n"
	"  }\n"
	"  if (graytile == 0) {\n"
	"    gl_FragColor = texture2D(tex, v_texcoord)\n;"
	"	 color = gl_FragColor; \n"
	"  	 // Brightness adjustment \n"
	"  	 color.xyz = color.xyz + brightness;\n"
	"    // Contrast adjustment \n"
	"    color.xyz = (color.xyz - vec3(0.5)) * vec3(contrast) + vec3(0.5);\n" 
	"    // Clamp the color \n"
	"  	 color.xyz = clamp(color.xyz, 0.0, 1.0);\n"
	"  	 // Apply gamma  \n"
	"    if (v_texcoord.x < 1.0) \n"
	"     	color.xyz = pow(color.xyz, vec3(gamma)); \n"
	"    gl_FragColor = color; \n"
	"  } else {\n"
	"    gl_FragColor = vec4(0.25, 0.25, 0.25, 1.0);\n"
	"  }\n"
	"  if (timedout == 1) {\n"
	"    gl_FragColor = mix(vec4(1.0, 1.0, 1.0, 1.0), gl_FragColor, 0.5);\n"
	"  }\n"
	"  gl_FragColor = mix(vec4(0.2, 0.2, 0.2, 1.0), gl_FragColor, opacity);\n"
	"}\n";

static GLuint program;
static GLuint proj_uniform, tile_uniform, gray_uniform, sprite_uniform;
static GLuint tex_uniform, opacity_uniform, timedout_uniform;
static GLuint brightness_uniform, contrast_uniform, gamma_uniform;
static int toggle_brightness, toggle_contrast, toggle_gamma;
static GLfloat brightness = 0.0, contrast = 1.0, gamma_correction = 1.0;
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
grid_switch_to(const spug_output_id output_id)
{
	/* Do nothing now */
}

static int
use_for_grid(struct weston_surface *surface)
{
	/*
	 * Ignore background surfaces, popups, solid color surfaces,
	 * software cursor surfaces, and YUV surfaces.
	 *
	 * Note that in a real-world plugin, some of these (especially, YUV)
	 * would actually be displayed; they're just omitted here to keep this
	 * sample simple.  YUV surfaces have multiple textures associated with
	 * them, so the fragment shader would have to be written to recognize
	 * this.
	 */
	if (ias_get_zorder(surface) == SHELL_SURFACE_ZORDER_BACKGROUND ||
			ias_get_zorder(surface) == SHELL_SURFACE_ZORDER_POPUP ||
			/* TODO removing these lines for now to fix a build error,
			 * this whole example will be rewritten in the near future */
			/*surface->shader == &compositor->solid_shader ||
			surface == seat->pointer->sprite ||
			surface->num_textures > 1*/ 0) {
		return 0;
	} else {
		return 1;
	}
}

static void
grid_draw(struct weston_output *output)
{
	struct weston_compositor *compositor = output->compositor;
	struct weston_view *view;
	int j;
	int i;
	static const GLfloat texv[] = {
		0.0, 0.0,
		0.0, 1.0,
		1.0, 1.0,
		1.0, 0.0
	};
	static uint32_t frame = 0;

	glViewport(0, 0, output->width, output->height);

	frame++;

	glClearColor(0.2, 0.2, 0.2, 1.0);

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
	j = 0;
	wl_list_for_each(view, &compositor->view_list, link) {
		if (!use_for_grid(view->surface)) {
			continue;
		}

		if (i >= 4) {
			break;
		}


		/* TODO removing this reference to surface->textures[0] for now to fix a
		 * build error. When porting this sample to spug, use spug_view_texture()
		 * to get the view's texture name */
		glBindTexture(GL_TEXTURE_2D, 0 /*view->surface->textures[0]*/);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glUniform1i(tile_uniform, i);
		glUniform1i(gray_uniform, 0);
		glUniform1i(tex_uniform, 0);
		glUniform1f(contrast_uniform, contrast);
		glUniform1f(brightness_uniform, brightness);
		glUniform1f(gamma_uniform, gamma_correction);
		glUniform1i(timedout_uniform, ias_has_surface_timedout(view->surface));
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
grid_grab_focus(struct weston_pointer_grab *base)
{

}

static void
grid_grab_motion(struct weston_pointer_grab *grab,
			const struct timespec *time,
			struct weston_pointer_motion_event * event)
{
	/* Update our internal idea of where the mouse is */
	mouse_x = wl_fixed_to_int(event->x);
	mouse_y = wl_fixed_to_int(event->y);
}

static void
grid_grab_button(struct weston_pointer_grab *grab,
		const struct timespec *time,
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
grid_grab_pointer_cancel(struct weston_pointer_grab *grab)
{
	/* Noop */
}

static void
grid_grab_axis(struct weston_pointer_grab *grab,
		  const struct timespec *time,
		  struct weston_pointer_axis_event *event)
{
    /* TODO */
}

static void
grid_grab_axis_source(struct weston_pointer_grab *grab, uint32_t source)
{
    /* TODO */
}

static void
grid_grab_frame(struct weston_pointer_grab *grab)
{
    /* TODO */
}

static struct weston_pointer_grab_interface mouse_grab_interface = {
	grid_grab_focus,
	grid_grab_motion,
	grid_grab_button,
	grid_grab_axis,
	grid_grab_axis_source,
	grid_grab_frame,
	grid_grab_pointer_cancel
};


static void
grid_grab_key(struct weston_keyboard_grab *grab,
		const struct timespec *time,
		uint32_t key,
		uint32_t state)
{
	int i = 0;
	struct weston_view *view;
	struct weston_output *output;

	/* If no tile is selected, then none of the arrow keys do anything */
	if (selected_tile == -1 && key != KEY_ENTER &&
			key != KEY_SPACE && key != KEY_S && 
			key != KEY_B && key != KEY_C && key != KEY_G &&
			key != KEY_KPPLUS && key != KEY_KPMINUS) 
	{
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
	case KEY_C:
		toggle_contrast ^= 1;
		if (toggle_contrast) {
			toggle_brightness = 0;
			toggle_gamma = 0;
		}
		break;
	case KEY_B:
		toggle_brightness ^= 1;
		if (toggle_brightness) {
			toggle_contrast = 0;
			toggle_gamma = 0;
		}
		break;
	case KEY_G:
		toggle_gamma ^= 1;
		if (toggle_gamma) {
			toggle_brightness = 0;
			toggle_contrast = 0;
		}
		break;
	case KEY_KPPLUS:
		if (toggle_contrast) {
			contrast += 0.1;
		} else if (toggle_brightness) {
			brightness += 0.1;
		} else if (toggle_gamma) {
			gamma_correction += 0.1;
		}
		break;
	case KEY_KPMINUS:
		if (toggle_contrast) {
			contrast -= 0.1;
		} else if (toggle_brightness) {
			brightness -= 0.1;
		} else if (toggle_gamma) {
			gamma_correction -= 0.1;
		}
		break;
	case KEY_ENTER:
		/* Set focus to selected window and deactivate plugin */
		if (selected_tile >= 0) {
			wl_list_for_each(view, &compositor->view_list, link) {
				if (!use_for_grid(view->surface)) {
					continue;
				}
				if (i++ == selected_tile) {
					weston_seat_set_keyboard_focus(seat, view->surface);
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
			wl_list_for_each(view, &compositor->view_list, link) {
				if (!use_for_grid(view->surface)) {
					continue;
				}
				if (i++ == selected_tile) {
					ias_toggle_frame_events(view->surface);
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
grid_grab_modifiers(struct weston_keyboard_grab *grab,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group)
{
	/* Noop */
}

static struct weston_keyboard_grab_interface key_grab_interface = {
	grid_grab_key,
	grid_grab_modifiers,
	NULL
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
	info->mouse_grab.interface = &mouse_grab_interface;
	info->key_grab.interface = &key_grab_interface;
	info->switch_to = grid_switch_to;

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
	brightness_uniform = glGetUniformLocation(program, "brightness");
	contrast_uniform = glGetUniformLocation(program, "contrast");
	gamma_uniform = glGetUniformLocation(program, "gamma");

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	wl_list_for_each(seat, &ec->seat_list, link) {
		/* Save first seat */
		break;
	}

	return 0;
}

