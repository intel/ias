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
#include <string.h>
#include <GLES2/gl2.h>
#include "ias-plugin-framework.h"
#include "ias-spug.h"

#include "cursor_image.h"

#ifndef UNUSED
#define UNUSED(var)  (void)(var)
#endif

static ias_identifier myid;
static spug_seat_id seat;
static spug_matrix projmat;
static spug_matrix *outmat;
static spug_matrix gl_matrix;

/*
 * Vertex shader for grid tiles.  Each tile always has coordinates
 * (0,0) - (400,400), but we use the 'tilenum' uniform to figure out
 * how much it should be translated by.
 */
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
	"  p = pos + vec2(50.0 + 500.0*mod(t, 2.0),\n"
	"                 50.0 + 500.0*floor(t/2.0));\n"
	"  gl_Position = proj * vec4(p, 0.0, 1.0);\n"
	"  v_texcoord = texcoord;\n"
	"}\n";

/*
 * Fragment shader for grid tiles.  Either use the contents of the surface
 * specified by the 'tex' uniform or draw a gray box for empty tiles.
 * The 'opacity' uniform allows tiles to be faded out (for a flashing
 * animation effect) and the 'timedout' uniform specifies that a surface
 * should be drawn with a 50% white mix to represent a non-responsive app.
 */
static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"uniform highp int tilenum;\n"
	"uniform int graytile;\n"
	"uniform float opacity;\n"
	"uniform int timedout;\n"
	"void main() {\n"
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

/*
 * Vertex shader for the mouse cursor.  Vertex attributes provide the
 * position of the cursor (in screen coordinates) and texture
 * coordinates.
 */
static const char *cursor_vert_shader_text =
	"uniform mat4 proj;\n"
	"uniform int hotspot_x;\n"
	"uniform int hotspot_y;\n"
	"attribute vec2 pos;\n"
	"attribute vec2 texcoord;\n"
	"varying vec2 v_texcoord;\n"
	"\n"
	"void main() {\n"
	"  vec2 p;\n"
	"\n"
	"  p = pos - vec2(" CURSOR_HOTSPOT_X "," CURSOR_HOTSPOT_Y ");\n"
	"  gl_Position = proj * vec4(p, 0.0, 1.0);\n"
	"  v_texcoord = texcoord;\n"
	"}\n";

/*
 * Fragment shader for the mouse cursor.  Uses the texture specified
 * by the 'tex' uniform.
 */
static const char *cursor_frag_shader_text =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(tex, v_texcoord)\n;"
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

/*
 * Mouse cursor vertices.  First two components of each line are vertex x,y
 * and will be filled in during the drawing function.  The second two components
 * are texture coordinates.
 */
static GLfloat curs_v[] = {
	0.0, 0.0, /**/ 0.0, 0.0,
	0.0, 0.0, /**/ 1.0, 0.0,
	0.0, 0.0, /**/ 0.0, 1.0,
	0.0, 0.0, /**/ 1.0, 1.0,
};

static GLuint program, cursprogram;
static GLuint proj_uniform, tile_uniform, gray_uniform;
static GLuint tex_uniform, opacity_uniform, timedout_uniform;
static GLuint pos_att, tex_att;

static GLuint curs_proj_uniform, curs_tex_uniform;
static GLuint curs_pos_att, curs_tex_att;

static int mouse_x, mouse_y;
static GLuint mouse_texture;
static int selected_tile = -1;
static int always_redraw = 1;
static int fullscreen_draw = 0;
static int current_tile;
static uint32_t frame = 0;
static int update_input_focus = 0;

static enum {
	CURSOR_GRID, CURSOR_APP
} cursor;

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
	UNUSED(output_id);
	/* Set initial mouse position */
	spug_mouse_xy(&mouse_x, &mouse_y, seat);
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
custom_cursor_draw_clean_state()
{
	/* Unbind the cursor texture */
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void
custom_cursor_draw_setup()
{
	/* Set up vertex array for cursor */
	curs_v[0] = (GLfloat)mouse_x;
	curs_v[1] = (GLfloat)mouse_y;
	curs_v[4] = (GLfloat)mouse_x + CURSOR_WIDTH;
	curs_v[5] = (GLfloat)mouse_y;
	curs_v[8] = (GLfloat)mouse_x;
	curs_v[9] = (GLfloat)mouse_y + CURSOR_HEIGHT;
	curs_v[12]= (GLfloat)mouse_x + CURSOR_WIDTH;
	curs_v[13] = (GLfloat)mouse_y + CURSOR_HEIGHT;
	glActiveTexture(GL_TEXTURE0);

	/*
	* Make sure blending is turned on so that transparent parts of the cursor
	* are really transparent.
	*/
	glEnable(GL_BLEND);
	/*
	 * We're drawing our own cursor image.  Setup the shaders that we use for
	 * drawing and bind the cursor texture.
	 */
	glUseProgram(cursprogram);
	glUniformMatrix4fv(curs_proj_uniform, 1, GL_FALSE, gl_matrix.d);
	glUniform1i(curs_tex_uniform, 0);

	/* Provide our cursor position and texture coordinates */
	glVertexAttribPointer(curs_pos_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &curs_v[0]);
	glVertexAttribPointer(curs_tex_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &curs_v[2]);
	glEnableVertexAttribArray(curs_pos_att);
	glEnableVertexAttribArray(curs_tex_att);

}

static void
custom_cursor_draw(	struct spug_view_draw_info *view_draw_info,
					struct spug_draw_info *draw_info)
{
	UNUSED(draw_info);
	glBindTexture(GL_TEXTURE_2D, view_draw_info->texture);
	//glBindTexture(GL_TEXTURE_2D, surface->textures[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	/* Draw the cursor */
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(curs_pos_att);
	glDisableVertexAttribArray(curs_tex_att);
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
	UNUSED(all_views);
	if (!use_for_grid(view_id) || current_tile >= 4) {
		return SPUG_FALSE;
	}

	current_tile++;
	return SPUG_TRUE;
}

static int
cursor_filter(const spug_view_id view_id, spug_view_list all_views)
{
	spug_view_id seat_sprite = spug_get_seat_sprite(seat);
	UNUSED(all_views);

	if(seat_sprite == view_id) {
		return SPUG_TRUE;
	}

	return SPUG_FALSE;
}

/* user created draw function */
static void
gray_box_draw(void)
{
	glUniform1i(tile_uniform, current_tile);
	glUniform1i(gray_uniform, 1);
	glUniform1i(timedout_uniform, 0);
	glUniform1f(opacity_uniform, (current_tile++ == selected_tile) ?
			abs((int) (60.0 - (float)(frame%120) / 60.0)) :
			1.0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void
view_draw(	struct spug_view_draw_info *view_draw_info,
			struct spug_draw_info *draw_info)
{
	UNUSED(draw_info);
	glBindTexture(GL_TEXTURE_2D, view_draw_info->texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUniform1i(tile_uniform, current_tile);
	glUniform1i(gray_uniform, 0);
	glUniform1i(tex_uniform, 0);
	glUniform1i(timedout_uniform,
			spug_has_surface_timedout(view_draw_info->id));
	glUniform1f(opacity_uniform, (current_tile++ == selected_tile) ?
			abs((int)(60.0 - (float)(frame%120) / 60.0)) :
			1.0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static spug_bool
fullscreen_filter(	const spug_view_id view_id,
					spug_view_list all_views)
{
	UNUSED(all_views);
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
grid_draw(spug_view_list view_list)
{
	spug_view_list window_list, cursor_list;
	spug_seat_id *seat_list;
	spug_output_id output;
	int out_x, out_y, out_width, out_height;

	/* at the moment the spug view and spug seat lists are unintelligently
	 * destroyed and rebuilt every frame. This produces new ids for each object
	 * and breaks any ids we kept from the last frame (such as seat). This hack 
	 * works around that until it's fixed. */
	seat_list = spug_get_seat_list();
	if (seat_list) {
		seat = seat_list[0];
	}

	if(fullscreen_draw) {
		spug_view_list fullscreen_list;
		fullscreen_list = spug_filter_view_list(view_list,
												&fullscreen_filter, 1);
		if(spug_view_list_is_empty(&fullscreen_list)) {
			return;
		}
	}

	frame++;


	/* remove anything we don't want to draw from surface_list */
	current_tile = 0;
	window_list = spug_filter_view_list(view_list, &view_filter, 4);

	if(	update_input_focus && selected_tile >= 0 &&
				selected_tile < spug_view_list_length(window_list)) {
		ipug_set_input_focus((IPUG_EVENTS_ALL_BIT & ~IPUG_POINTER_ALL_BIT),
								window_list[selected_tile]);
		update_input_focus = 0;
	}

	/* get the output matrix. For this simple plugin we can just use any */
	output = spug_get_output_id(1, 1);
	outmat = spug_get_matrix(output);



	/*
         * Convert output's projection matrix, to get screen's coordinate system.
         */
        gl_matrix = *outmat;
        spug_get_output_region(output, &out_x, &out_y, &out_width, &out_height);
        spug_matrix_translate(&gl_matrix,
			      -(out_width / 2.0),
                              -(out_height / 2.0), 0);
        spug_matrix_scale(&gl_matrix,
                          2.0 / out_width,
                          -2.0 / out_height, 1);

	/* call surface_draw_setup, then call surface_draw on each
 	 * surface still in surface list */
	spug_draw(	window_list,
				&view_draw,
				&view_draw_setup,
				&view_draw_clean_state);

	/* draw grey boxes for the empty tiles. They aren't surfaces, so we don't
	 * need to use spug to draw them */
	while(current_tile < 4){
		gray_box_draw();
	}

	/*
	 * Are we drawing the grid-specific cursor, or whatever apps 
	 * have set? Note that we should still draw the mouse with output 
	 * projection here rather than projmat.
	 */
	if (cursor == CURSOR_GRID) {
		custom_cursor_draw_setup();
		/*
		 * We're drawing our own cursor image.  Setup the shaders that we use for
		 * drawing and bind the cursor texture.
		 */
		glUseProgram(cursprogram);
		glUniformMatrix4fv(curs_proj_uniform, 1, GL_FALSE, gl_matrix.d);
		glUniform1i(curs_tex_uniform, 0);
		glBindTexture(GL_TEXTURE_2D, mouse_texture);
		//glBindTexture(GL_TEXTURE_2D, surface->textures[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		/* Provide our cursor position and texture coordinates */
		glVertexAttribPointer(curs_pos_att, 2, GL_FLOAT, GL_FALSE,
				4 * sizeof(GLfloat), &curs_v[0]);
		glVertexAttribPointer(curs_tex_att, 2, GL_FLOAT, GL_FALSE,
				4 * sizeof(GLfloat), &curs_v[2]);
		glEnableVertexAttribArray(curs_pos_att);
		glEnableVertexAttribArray(curs_tex_att);

		/* Draw the cursor */
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glDisableVertexAttribArray(curs_pos_att);
		glDisableVertexAttribArray(curs_tex_att);
	} else if (cursor == CURSOR_APP && spug_seat_has_sprite(seat)) {
		cursor_list = spug_filter_view_list(view_list, &cursor_filter, 1);
		spug_draw(	cursor_list, 
					&custom_cursor_draw,
					&custom_cursor_draw_setup,
					&custom_cursor_draw_clean_state);
	}

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

	/*
	 * We want to make sure here that the ouput provided to this function
	 * is valid, if it is not then below call to spug_get_output_region
	 * will not fill in the variables we pass in, and when calculating the
	 * coords we will end up dividing by zero.
	 */
	if (output == 0x0) {
		return -1;
	}

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
	UNUSED(base);
}

static void
grid_grab_motion(spug_pointer_grab *grab,
			const struct timespec *time, struct weston_pointer_motion_event *event)
{
	int iX = 0;
	int iY = 0;

	if (event->mask & WESTON_POINTER_MOTION_ABS) {
		iX = (int)event->x;
		iY = (int)event->y;
	} else if (event->mask & WESTON_POINTER_MOTION_REL) {
		iX = wl_fixed_to_double(grab->pointer->x) + (int)event->dx;
		iY = wl_fixed_to_double(grab->pointer->y) + (int)event->dy;
	}

	int cell = select_cell(select_output(iX, iY), iX, iY);
	spug_view_list view_list = spug_get_view_list();

	/* Update our internal idea of where the mouse is */
	mouse_x = iX;
	mouse_y = iY;

	/* lets forward mouse movements to whichever view is under the cursor */
	if(cell != -1 && cell < spug_view_list_length(view_list)) {
		struct ipug_event_info_pointer_motion motion;

		motion.base.event_type = IPUG_POINTER_MOTION;
		motion.grab = grab;
		motion.time = time;
		motion.x = wl_fixed_from_double(event->dx);
		motion.y = wl_fixed_from_double(event->dy);

		ipug_send_event_to_view((struct ipug_event_info*)&motion, view_list[cell]);
	}
}

static void
grid_grab_button(spug_pointer_grab *grab,
		const struct timespec *time,
		uint32_t button,
		uint32_t state)
{
	spug_view_list view_list = spug_get_view_list();
	spug_output_id output;
	int this_tile;

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
	this_tile = select_cell(output, mouse_x, mouse_y);

	/* lets take the opportunity to demonstrate set_input_focus */
	update_input_focus = 1;

	/* if we've already selected this tile, pass it the button event */
	if(this_tile == selected_tile &&
					this_tile != -1 &&
					this_tile < spug_view_list_length(view_list)) {
		struct ipug_event_info_pointer_button button_event;

		button_event.base.event_type = IPUG_POINTER_BUTTON;
		button_event.grab = grab;
		button_event.time = time;
		button_event.button = button;
		button_event.state = state;

		ipug_send_event_to_view((struct ipug_event_info*)&button_event,
										view_list[this_tile]);
	}

	selected_tile = this_tile;
}

static void
grid_grab_pointer_cancel(spug_pointer_grab *grab)
{
	/* Noop */
	UNUSED(grab);
}

static void
grid_grab_axis(spug_pointer_grab *grab,
		  const struct timespec *time,
		  struct weston_pointer_axis_event *event)
{
    /* TODO */
	UNUSED(grab);
	UNUSED(time);
	UNUSED(event);
}

static void
grid_grab_axis_source(spug_pointer_grab *grab, uint32_t source)
{
    /* TODO */
	UNUSED(grab);
	UNUSED(source);
}

static void
grid_grab_frame(spug_pointer_grab *grab)
{
    /* TODO */
	UNUSED(grab);
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
grid_grab_touch_down(spug_touch_grab *grab,
		const struct timespec *time,
		int touch_id,
		spug_fixed_t sx,
		spug_fixed_t sy)
{
	spug_output_id output;
	int x = spug_fixed_to_int(grab->touch->grab_x);
	int y = spug_fixed_to_int(grab->touch->grab_y);

	UNUSED(time);
	UNUSED(touch_id);
	UNUSED(sx);
	UNUSED(sy);

	/*
	 * Grid might be active on multiple outputs at once.  Figure out which
	 * output the event button event happened on.
	 */
	output = select_output(x, y);

	/* Figure out which tile we clicked on, if any */
	selected_tile = select_cell(output, x, y);

	/* lets take the opportunity to demonstrate set_input_focus */
	update_input_focus = 1;

}

static void
grid_grab_touch_up(spug_touch_grab *grab,
		const struct timespec *time,
		int touch_id)
{
	/* noop */
	UNUSED(grab);
	UNUSED(time);
	UNUSED(touch_id);
}

static void
grid_grab_touch_motion(spug_touch_grab *grab,
		const struct timespec *time,
		int touch_id,
		int sx,
		int sy)
{
	/* noop */
	UNUSED(grab);
	UNUSED(time);
	UNUSED(touch_id);
	UNUSED(sx);
	UNUSED(sy);
}

static void
grid_grab_touch_frame(spug_touch_grab *grab)
{
	/* noop */
	UNUSED(grab);
}

static void
grid_grab_touch_cancel(spug_touch_grab *grab)
{
	/* noop */
	UNUSED(grab);
}


static spug_touch_grab_interface touch_grab_interface = {
	grid_grab_touch_down,
	grid_grab_touch_up,
	grid_grab_touch_motion,
	grid_grab_touch_frame,
	grid_grab_touch_cancel
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
	if (selected_tile == -1 && (key == KEY_LEFT || key == KEY_RIGHT ||
				key == KEY_DOWN || key == KEY_UP))
	{
		return;
	}

	/* send keypresses to the currently focused cell if there is one. Unless
	 * the key is ESC, in which case we unset the focus */
	if(key == KEY_ESC) {
		ipug_set_input_focus((IPUG_EVENTS_ALL_BIT & ~IPUG_POINTER_ALL_BIT),
				NULL);
	} else if(selected_tile >= 0){
		struct ipug_event_info_key_key event_info;

		event_info.base.event_type = IPUG_KEYBOARD_KEY;
		event_info.grab = grab;
		event_info.time = time;
		event_info.key = key;
		event_info.state = state;

		if(ipug_send_event_to_focus((struct ipug_event_info*)&event_info)) {
			/* if we succeeded in sending the event to a focused view then we're
			 * done here */
			return;
		}
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
	case KEY_M:
		/* Switch cursor type */
		cursor = (cursor == CURSOR_GRID) ? CURSOR_APP : CURSOR_GRID;
		break;
	case KEY_R:
		/* Toggle redraw behavior (every frame vs only on damage) */
		if (always_redraw) {
			while(outputs[i]) {
				spug_set_plugin_redraw_behavior(outputs[i++], PLUGIN_REDRAW_DAMAGE);
			}
			printf("Plugin will now only get redraws on damage.\n");
		} else {
			while(outputs[i]) {
				spug_set_plugin_redraw_behavior(outputs[i++], PLUGIN_REDRAW_ALWAYS);
			}
			printf("Plugin will now redraw every vblank.\n");
		}
		always_redraw = !always_redraw;
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
	case KEY_F:
		fullscreen_draw = !fullscreen_draw;
		break;
	default:
		/* Ignore other keys */
		;
	}

	switch(key) {
	case KEY_UP:
	case KEY_DOWN:
	case KEY_LEFT:
	case KEY_RIGHT:
	case KEY_TAB:
		/* lets take the opportunity to demonstrate set_input_focus */
		update_input_focus = 1;
	default:
		break;
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
	UNUSED(grab);
	UNUSED(serial);
	UNUSED(mods_depressed);
	UNUSED(mods_latched);
	UNUSED(mods_locked);
	UNUSED(group);
}

static void grid_grab_key_cancel(spug_keyboard_grab *grab)
{
	/* Noop */
	UNUSED(grab);
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
	GLuint cursfrag, cursvert;
	GLint status;
	spug_output_id output;

	UNUSED(version);
	myid = id;

	/*
	 * This plugin is written for inforec version 1, so that's all we fill
	 * in, regardless of what gets passed in for the version parameter.
	 */
	info->draw = grid_draw;
	info->mouse_grab.interface = &mouse_grab_interface;
	info->key_grab.interface = &key_grab_interface;
	info->touch_grab.interface = &touch_grab_interface;
	info->switch_to = grid_switch_to;

	/*
	 * Setup vertex and fragment shaders for grid cells
	 */

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

	pos_att = glGetAttribLocation(program, "pos");
	tex_att = glGetAttribLocation(program, "texcoord");
	proj_uniform = glGetUniformLocation(program, "proj");
	tile_uniform = glGetUniformLocation(program, "tilenum");
	gray_uniform = glGetUniformLocation(program, "graytile");
	tex_uniform = glGetUniformLocation(program, "tex");
	opacity_uniform = glGetUniformLocation(program, "opacity");
	timedout_uniform = glGetUniformLocation(program, "timedout");

	/*
	 * Setup vertex and fragment shaders for mouse cursor
	 */

	cursfrag = create_shader(cursor_frag_shader_text, GL_FRAGMENT_SHADER);
	cursvert = create_shader(cursor_vert_shader_text, GL_VERTEX_SHADER);

	cursprogram = glCreateProgram();
	glAttachShader(cursprogram, cursfrag);
	glAttachShader(cursprogram, cursvert);
	glLinkProgram(cursprogram);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		return 1;
	}

	curs_pos_att = glGetAttribLocation(cursprogram, "pos");
	curs_tex_att = glGetAttribLocation(cursprogram, "texcoord");
	curs_proj_uniform = glGetUniformLocation(cursprogram, "proj");
	curs_tex_uniform = glGetUniformLocation(cursprogram, "tex");

	/* Create texture for mouse pointer */
	glGenTextures(1, &mouse_texture);
	glBindTexture(GL_TEXTURE_2D, mouse_texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, CURSOR_WIDTH, CURSOR_HEIGHT, 0,
			GL_BGRA_EXT, GL_UNSIGNED_BYTE, pointer_image);
	glBindTexture(GL_TEXTURE_2D, 0);

	{
		spug_seat_id* seat_ids = spug_get_seat_list();

		/* save the first seat */
		if(seat_ids) {
			seat = seat_ids[0];
		}
	}

	/*
	 * Create a projection matrix for the screen.  For simplicity, we'll always
	 * treat the screen as having coordinates from (0,0)-(1000,1000).  Since
	 * OpenGL coordinates usually go from (-1, 1) to (1, -1), with (0, 0)
	 * being in the middle of the screen, the first step to converting a screen
	 * coordinate to an OpenGL coordinate is to shift it up/left by
	 * (-width/2, -height/2) so that it falls within a region from
	 * (-width/2, -height/2) to (width/2, height/2).  Then we scale it down by
	 * multiplying by 2/width and -2/height.  Note that the height coordinate
	 * is inverted to make the coordinates match OGL's y-axis orientation.
	 *
	 * Using weston's built-in matrix functionality makes these operations a
	 * bit simpler.
	 */
	spug_matrix_init(&projmat);
	output = spug_get_output_id(1, 1);

	spug_matrix_translate(&projmat, -1000.0 / 2.0, -1000.0 / 2.0, 0);

	/* apply any output transformation */
	switch(spug_get_output_transform(output)) {
		case SPUG_OUTPUT_TRANSFORM_FLIPPED:
			weston_matrix_scale(&projmat, -1.0f, 1.0f, 1.0f);
		case SPUG_OUTPUT_TRANSFORM_NORMAL:
			break;
		case SPUG_OUTPUT_TRANSFORM_FLIPPED_90:
			weston_matrix_scale(&projmat, -1.0f, 1.0f, 1.0f);
		case SPUG_OUTPUT_TRANSFORM_90:
			spug_matrix_rotate_xy(&projmat, 0.0f, 1.0f);
			break;
		case SPUG_OUTPUT_TRANSFORM_FLIPPED_180:
			weston_matrix_scale(&projmat, -1.0f, 1.0f, 1.0f);
		case SPUG_OUTPUT_TRANSFORM_180:
			spug_matrix_rotate_xy(&projmat, -1.0f, 0.0f);
			break;
		case SPUG_OUTPUT_TRANSFORM_FLIPPED_270:
			weston_matrix_scale(&projmat, -1.0f, 1.0f, 1.0f);
		case SPUG_OUTPUT_TRANSFORM_270:
			spug_matrix_rotate_xy(&projmat, 0.0f, -1.0f);
			break;
		default:
			break;
	}

	spug_matrix_scale(&projmat, 2.0 / 1000.0, -2.0 / 1000.0, 1);

	return 0;
}
