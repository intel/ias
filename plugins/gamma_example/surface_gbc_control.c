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
#include <string.h>
#include "ias-plugin-framework.h"
#include "ias-spug.h"

static ias_identifier myid;
static spug_matrix projmat;
static spug_matrix *outmat;
static spug_matrix gl_matrix;
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

/*
 * Tile vertices.  First two components of each line are vertex x,y.
 * Second two are texture coordinates.
 *
 * These values never change; the vertex shader for tiles figures out which
 * tile number its drawing and offsets the vertices appropriately.
 */
static const GLfloat tile_verts[] = {
	   0.0,   0.0, /**/  0.0, 0.0,
	 400.0,   0.0, /**/  1.0, 0.0,
	   0.0, 400.0, /**/  0.0, 1.0,
	 400.0, 400.0, /**/  1.0, 1.0,
};

static GLuint pos_att, tex_att;
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
static int fullscreen_draw = 0;
static int i, current_tile;
static uint32_t frame = 0;
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
compositor_owns_view(const spug_view_id view)
{
	char *pname = NULL;
	int ret = 0;

	spug_get_owning_process_info(view, NULL, &pname);

	if(pname) {
		if(strcmp(pname, "weston") == 0) {
			ret = 1;
		}
		free(pname);
	}

	return ret;
}

static int
use_for_grid(const spug_view_id view)
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
	if (spug_get_zorder(view) == SHELL_SURFACE_ZORDER_BACKGROUND ||
			spug_get_zorder(view) == SHELL_SURFACE_ZORDER_POPUP ||
			compositor_owns_view(view) ||
			spug_view_is_sprite(view,(spug_seat_id)seat) ||
			spug_view_num_textures(view) > 1) {
		return 0;
	} else {
		return 1;
	}
}

static spug_bool
fullscreen_filter(	const spug_view_id view_id,
					spug_view_list all_views)
{
	if(spug_is_surface_flippable(view_id) &&
		spug_get_zorder(view_id) != SHELL_SURFACE_ZORDER_BACKGROUND) {
		/*
		 * Let's assign this surface to scanout buffer now, if we are
		 * unsuccessful, we will continue compositing
		 */
		if(!spug_assign_surface_to_scanout(view_id, 0, 0)) {
			return SPUG_TRUE;
		}
	}
	return SPUG_FALSE;
}

static void
view_draw_clean_state(void)
{
}

static void
view_draw_setup()
{
	glClearColor(0.2, 0.2, 0.2, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glUseProgram(program);

	/*
	 * Pass the output's projection matrix which converts us to the screen's
	 * coordinate system.
	 */
	glUniformMatrix4fv(proj_uniform, 1, GL_FALSE, projmat.d);

	/* Buffer format is vert_x, vert_y, tex_x, tex_y */
	glVertexAttribPointer(pos_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts[0]);
	glVertexAttribPointer(tex_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts[2]);
	glEnableVertexAttribArray(pos_att);
	glEnableVertexAttribArray(tex_att);

	/* reset the current tile. This is incremented and passed into the tile
	 * uniform in view_draw */
	current_tile = 0;
}
/* user created filter function. *all_views isn't used in this one, but in
 * more advanced filters the user might want to compare views to each other,
 * for example to get the n most X views.
 */
static int
view_filter(const spug_view_id view_id, spug_view_list all_views)
{
	if (!use_for_grid(view_id) || current_tile >= 4) {
		return SPUG_FALSE;
	}

	current_tile++;
	return SPUG_TRUE;
}
/* user created draw function */

static void
view_draw(	struct spug_view_draw_info *view_draw_info,
			struct spug_draw_info *draw_info)
{
		glBindTexture(GL_TEXTURE_2D, view_draw_info->texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glUniform1i(tile_uniform, current_tile);
		glUniform1i(gray_uniform, 0);
		glUniform1i(tex_uniform, 0);
		glUniform1i(timedout_uniform,
				spug_has_surface_timedout(view_draw_info->id));
		glUniform1f(opacity_uniform, (current_tile++ == selected_tile) ?
				abs(60.0 - (float)(frame%120)) / 60.0 :
				1.0);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindTexture(GL_TEXTURE_2D, 0);
}
static void
gray_box_draw(void)
{
		glUniform1i(tile_uniform, current_tile);
		glUniform1i(gray_uniform, 1);
		glUniform1i(timedout_uniform, 0);
		glUniform1f(opacity_uniform, (current_tile++ == selected_tile) ?
				abs(60.0 - (float)(frame%120)) / 60.0 :
				1.0);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static void
grid_draw(spug_view_list view_list)
{
	spug_view_list window_list, cursor_list;
	spug_view_id *sview;

	if(fullscreen_draw) {
		spug_view_list fullscreen_list;
		fullscreen_list = spug_filter_view_list(view_list,&fullscreen_filter, 1);
		if(spug_view_list_is_empty(&fullscreen_list)) {
			return;
		}
	}

	frame++;


	/* remove anything we don't want to draw from surface_list */
	current_tile = 0;
	window_list = spug_filter_view_list(view_list, &view_filter, 4);



	/* call surface_draw_setup, then call surface_draw on each
	 * surface still in surface list */
	spug_draw(window_list,&view_draw,&view_draw_setup,&view_draw_clean_state);

	/* draw grey boxes for the empty tiles. They aren't surfaces, so we don't
	 * need to use spug to draw them */
	while(current_tile < 4){
		gray_box_draw();
	}
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
				if (!use_for_grid((spug_view_id)view->surface)) {
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
				if (!use_for_grid((spug_view_id)view->surface)) {
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

