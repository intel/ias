/*
 *-----------------------------------------------------------------------------
 * Filename: sprite_example.c
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
 *   Slightly modified copy of grid plugin that illustrates use of sprite
 *   planes for display.
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <linux/input.h>
#include <string.h>
#include <GLES2/gl2.h>
#include "ias-plugin-framework.h"
#include "ias-spug.h"

static ias_identifier myid;
static spug_seat_id seat;
static spug_matrix *outmat;
static spug_output_id output;

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
	"uniform int sprite_on;\n"
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
	"  if (sprite_on ==1) {\n"
	"    gl_FragColor = vec4(0.25, 0.25, 0.25, 0.5);\n"
	"  }\n"
	"  if (timedout == 1) {\n"
	"    gl_FragColor = mix(vec4(1.0, 1.0, 1.0, 1.0), gl_FragColor, 0.5);\n"
	"  }\n"
	"  gl_FragColor = mix(vec4(0.2, 0.2, 0.2, 1.0), gl_FragColor, opacity);\n"
	"}\n";

static GLuint program;
static GLuint proj_uniform, tile_uniform, gray_uniform, sprite_uniform;
static GLuint tex_uniform, opacity_uniform, timedout_uniform;
static GLuint vbo;
static GLuint ibo;

static int mouse_x, mouse_y;
static int selected_tile = -1;

static int num_sprites;
static struct ias_sprite **sprite_list;
static int sprite_id[4];
static int show_sprite = 0;
static int change_zorder_sprite[2];
static int constant_alpha_on = 0;
static int constant_alpha_change = 0;
static int increase_constant_alpha;
static int decrease_constant_alpha;
static float current_constant_alpha = 1.0;
static int sprite_top[2]; 
static uint32_t frame = 0;
static int j;
static int i;

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
grid_switch_to(spug_output_id output_id)
{
	/* If we've already grabbed the sprite list, no need to do it again */
	if (sprite_list) {
		return;
	}

	/*
	 * For this example, we're going to assume this plugin is only used on a
	 * single output at a time.  A real output would want to put in more
	 * bookkeeping effort to ensure that sprites are tracked on a per-output
	 * basis.
	 */
	num_sprites = spug_get_sprite_list(output_id, &sprite_list);
	if (!num_sprites) {
		printf("No sprites available for use.\n");
	} else {
		printf("%d sprites detected\n", num_sprites);
	}
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
			spug_view_is_sprite(view, seat) ||
			spug_view_num_textures(view) > 1) {
		return 0;
	} else {
		return 1;
	}
}

static void
view_draw_setup(void)
{
	spug_matrix gl_matrix;
	int out_x, out_y, out_width, out_height;
	i = 0;
	j = 0;

	spug_get_output_region(output, &out_x, &out_y, &out_width, &out_height);
	glViewport(out_x, out_y, out_width, out_height);

	glClearColor(0.2, 0.2, 0.2, show_sprite? 0.5 : 1.0);

	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(program);

	/*
	 * Convert output's projection matrix, to get screen's coordinate system.
	 */
	gl_matrix = *outmat;
	spug_matrix_translate(&gl_matrix, -(out_width / 2.0),
				-(out_height / 2.0), 0);
	spug_matrix_scale(&gl_matrix,
			  2.0 / out_width,
			  -2.0 / out_height, 1);

	glUniformMatrix4fv(proj_uniform, 1, GL_FALSE, gl_matrix.d);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	/* Buffer format is vert_x, vert_y, tex_x, tex_y */
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), (GLvoid*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), (GLvoid*)2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

	glUniform1i(sprite_uniform, show_sprite? 1 : 0);
}

static int
view_filter(const spug_view_id view_id, spug_view_list all_views)
{
	if (!use_for_grid(view_id)) {
		return SPUG_FALSE;
	}

	return SPUG_TRUE;
}

static void
view_draw(	struct spug_view_draw_info *view_draw_info,
			struct spug_draw_info *draw_info)
{
		pixman_region32_t *boundingbox =
						spug_view_get_trans_boundingbox(view_draw_info->id);
		int zorder;

		/*
		 * If this is the first surface in the list, we'll stick it on a sprite
		 * in the middle of the screen in addition to putting it on the grid.
		 */
		if (j < num_sprites) {
			if (show_sprite) {
				
				spug_assign_surface_to_sprite(view_draw_info->id,
					output,
					&sprite_id[j],
					0, 0,
					boundingbox);
				
				j++;

				if (change_zorder_sprite[0]) {
					if (sprite_top[0] == 1) {
						zorder = SPRITE_SURFACE_ZORDER_TOP;	
					} else {
						zorder = SPRITE_SURFACE_ZORDER_BOTTOM;	
					}
				
					spug_assign_zorder_to_sprite(output,
						sprite_id[0], 
						zorder);
					change_zorder_sprite[0] = 0;
				} else if (change_zorder_sprite[1]) {
					if (sprite_top[1] == 1) {
						zorder = SPRITE_SURFACE_ZORDER_TOP;	
					} else {
						zorder = SPRITE_SURFACE_ZORDER_BOTTOM;	
					}
					spug_assign_zorder_to_sprite(output,
						sprite_id[1], 
						zorder);
					change_zorder_sprite[1] = 0;

				}

				if (increase_constant_alpha || decrease_constant_alpha ||
					constant_alpha_change) {
			
					if (decrease_constant_alpha) {
						current_constant_alpha -= 0.1;
					
						if (current_constant_alpha < 0) {
							current_constant_alpha = 0;
						}
						decrease_constant_alpha = 0;
					}

					if (increase_constant_alpha) {
						current_constant_alpha += 0.1;

						if (current_constant_alpha > 1.0) {
							current_constant_alpha = 1.0;
						}
						increase_constant_alpha = 0;

					}
					spug_assign_constant_alpha_to_sprite(output,
						sprite_id[0],
						current_constant_alpha,
						constant_alpha_on);
					
				}
			}
		}

		glBindTexture(GL_TEXTURE_2D, view_draw_info->texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glUniform1i(tile_uniform, i);
		glUniform1i(gray_uniform, 0);
		glUniform1i(tex_uniform, 0);
		glUniform1i(timedout_uniform, spug_has_surface_timedout(view_draw_info->id));
		glUniform1f(opacity_uniform, (i == selected_tile) ?
				abs((int)(60.0 - (float)(frame%120) / 60.0)) :
				1.0);
		
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		i++;
}

static void
grid_draw(spug_view_list view_list)
{
	spug_view_list window_list;

	frame++;
	i = 0;
	j = 0;

	window_list = spug_filter_view_list(view_list, &view_filter, 4);

	/* get the output matrix. For this simple plugin we can just use any */
	output = spug_get_output_id(1, 1);
	outmat = spug_get_matrix(output);

	spug_draw(window_list,
				&view_draw,
				&view_draw_setup,
				NULL);
	/*
	 * Draw gray boxes for any remaining tiles if we didn't have a full
	 * four clients.
	 */

		
	for ( ; i < 4; i++) {
		glUniform1i(tile_uniform, i);
		glUniform1i(gray_uniform, 1);
		glUniform1i(timedout_uniform, 0);
		glUniform1f(opacity_uniform, (i == selected_tile) ?
				abs((int)(60.0 - (float)(frame%120) / 60.0)) :
				1.0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
	}
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
}

/*
 * select_cell()
 *
 * Helper function that determines which grid cell was clicked/touched given
 * the real screen coordinates.  Cells are numbered 0-3 and -1 is returned
 * if the point selected is not inside a cell.
 */
static int
select_cell(spug_output_id output, int32_t x, int32_t y)
{
	int32_t coord_x, coord_y;
	int region_x, region_y, region_width, region_height;

	/* get the output's region extents */
	spug_get_output_region(	output,
							&region_x,
							&region_y,
							&region_width,
							&region_height);

	/*
	 * The grid is setup on a 1000 x 1000 coordinate system with each cell
	 * being 400 x 400 with a 50 buffer on either side.  Convert the
	 * real screen coordinates (which vary according to mode) into this
	 * coordinate system.
	 */
	coord_x = (x - region_x) * 1000 / region_width;
	coord_y = (y - region_y) * 1000 / region_height;

	if (coord_x >= 50 && coord_x <= 450 &&
			coord_y >= 50 && coord_y <= 450) {
		return 0;
	} else if (coord_x >= 550 && coord_x <= 950 &&
			coord_y >= 50 && coord_y <= 450) {
		return 1;
	} else if (coord_x >= 50 && coord_x <= 450 &&
			coord_y >= 550 && coord_y <= 950) {
		return 2;
	} else if (coord_x >= 550 && coord_x <= 950 &&
			coord_y >= 550 && coord_y <= 950) {
		return 3;
	} else {
		return -1;
	}

}

/*
 * select_output()
 *
 * Figures out which output a touch/click happened on.  Note that for
 * simplicity we're assuming that outputs aren't setup in an
 * overlapping configuration.
 */
static spug_output_id
select_output(int32_t x, int32_t y)
{
	return spug_get_output_id(x, y);
}

/***
 *** Input handlers
 ***/

static void
grid_grab_focus(spug_pointer_grab *base)
{

}

static void
grid_grab_motion(spug_pointer_grab *grab,
			const struct timespec *time,
			struct weston_pointer_motion_event *event)
{
	/* Update our internal idea of where the mouse is */
	mouse_x = spug_fixed_to_int(event->x);
	mouse_y = spug_fixed_to_int(event->y);
}

static void
grid_grab_button(spug_pointer_grab *grab,
		const struct timespec *time,
		uint32_t button,
		uint32_t state)
{
	/* Only pay attention to press events; ignore release */
	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}

	/*
	 * Grid might be active on multiple outputs at once.  Figure out which
	 * output the event button event happened on.
	 */
	output = select_output(mouse_x, mouse_y);

	/* Figure out which tile we clicked on, if any */
	selected_tile = select_cell(output, mouse_x, mouse_y);
}


static void
grid_grab_axis(spug_pointer_grab *grab,
		     const struct timespec *time,
		     struct weston_pointer_axis_event *event) {
}


static void
grid_grab_axis_source(spug_pointer_grab *grab, uint32_t source) {
}


static void
grid_grab_frame(spug_pointer_grab *grab) {
}


static void
grid_grab_pointer_cancel(spug_pointer_grab *grab)
{
	/* Noop */
}

static spug_pointer_grab_interface mouse_grab_interface = {
	grid_grab_focus,
	grid_grab_motion,
	grid_grab_button,
	grid_grab_axis,
	grid_grab_axis_source,
	grid_grab_frame,
	grid_grab_pointer_cancel
};


static void
grid_grab_key(spug_keyboard_grab *grab,
		const struct timespec *time,
		uint32_t key,
		uint32_t state)
{
	int i = 0;
	spug_output_list outputs = spug_get_output_list();

	/* If no tile is selected, then none of the arrow keys do anything */
	if (selected_tile == -1 && key != KEY_ENTER &&
			key != KEY_SPACE && key != KEY_S &&
			key != KEY_Z && key != KEY_X && 
			key != KEY_C && key != KEY_KPPLUS && key != KEY_KPMINUS)
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
	case KEY_S:
		/* Turn on sprite */
		show_sprite ^= 1;
		break;
	case KEY_Z:
		/* Turn sprite 0 to the top/bottom */
		sprite_top[0] ^= 1 ;
		change_zorder_sprite[0] = 1;
		break;
	case KEY_X:
		sprite_top[1] ^= 1;
		change_zorder_sprite[1] = 1;
		break;
	case KEY_C:
		constant_alpha_change = 1;
		constant_alpha_on ^= 1;
		break;
	case KEY_KPPLUS:
		increase_constant_alpha = 1;
		break;
	case KEY_KPMINUS:
		decrease_constant_alpha = 1;
		break;
	case KEY_ENTER:
		{
			spug_view_id *views = spug_get_view_list();
			/* Set focus to selected window and deactivate plugin */
			if (selected_tile >= 0) {
				while(views[i]) {
					if (!use_for_grid(views[i])) {
						continue;
					}
					if (i == selected_tile) {
						spug_surface_activate(views[i], seat);
						break;
					}
					i++;
				}
			}
		}

		/* Deactivate plugin on all outputs */
		while(outputs[i]) {
			spug_deactivate_plugin(outputs[i++]);
		}

		break;
	case KEY_SPACE:
		{
			spug_view_id *views = spug_get_view_list();
			/* Suspend/unsuspend frame events to this surface on spacebar */
			if (selected_tile >= 0) {
				while(views[i]) {
					if (!use_for_grid(views[i])) {
						continue;
					}
					if (i == selected_tile) {
						spug_toggle_frame_events(views[i]);
						break;
					}
					i++;
				}
			}
			break;
		}
	default:
		/* Ignore other keys */
		;
	}
}

static void
grid_grab_modifiers(spug_keyboard_grab *grab,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group)
{
	/* Noop */
}

static void grid_grab_key_cancel(spug_keyboard_grab *grab)
{
	/* Noop */
}

static spug_keyboard_grab_interface key_grab_interface = {
	grid_grab_key,
	grid_grab_modifiers,
	grid_grab_key_cancel
};


/***
 *** Plugin initialization
 ***/

WL_EXPORT int
ias_plugin_init(struct ias_plugin_info *info,
		ias_identifier id,
		uint32_t version)
{
	GLuint frag, vert;
	GLint status;

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
	sprite_uniform = glGetUniformLocation(program, "sprite_on");

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	{
		spug_seat_id* seat_ids = spug_get_seat_list();

		/* save the first seat */
		if(seat_ids) {
			seat = seat_ids[0];
		}
	}

	return 0;
}
