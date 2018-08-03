/*
 *-----------------------------------------------------------------------------
 * Filename: thumbnail_layout.c
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
 */

#include <stdlib.h>
#include <linux/input.h>
#include <GLES2/gl2.h>
#include <wayland-server.h>
#include <ias-plugin-framework.h>

#include <unistd.h>

#define SCREEN_H 1024
#define SCREEN_W 768
#define S_MAIN 0x01
#define S_AUX1 0x02
#define S_AUX2 0x04
#define S_POP  0x08
#define MAX_THUMBS 10

static GLuint program;
static GLuint pos_atr, col_atr, tex, grey_uniform;

typedef struct thumb_obj {
	struct weston_compositor *compositor;
	int show;
	int thumbs;
	const struct timespec *pressed;
	float touch_x;
	float touch_y;
	const struct timespec *swiped;
	float swipe_x;
	float swipe_y;
	float swipe_end_x;
	float swipe_end_y;
	struct weston_seat *seat;
	/*
	 * When the surface list is walked, fill out
	 * the apps list.
	 * main points to the app that is on the main panel
	 * aux1 points to the app that is on the upper right panel
	 * aux2 points to the app that is on the lower right panel
	 * thumb_list holds the apps that are in the thumnail containers
	 */
	struct weston_surface *background;
	int appcnt;
	struct weston_surface *apps[MAX_THUMBS];
	int thumb_list[MAX_THUMBS];
	int main;
	int aux1;
	int aux2;
} thumb_obj_t;

static thumb_obj_t thumb;


/*
    ┌───────────────┐
    │  ┌─────┬────┐ │
    │  │     │    │ │
    │  │     ├────┤ │
    │  │     │    │ │
    │  └─────┴────┘ │
    │   ░ ░ ░ ░ ░   │
    └───────────────┘
 */
GLfloat vert[] = {
	/*back ground*/
	-1.0, 1.0,
	-1.0, -1.0,
	1.0, -1.0,
	1.0, 1.0,
	/*main pane*/
	-0.95, 0.85,
	-0.95, -0.55,
	0.45, -0.55,
	0.45, 0.85,
	/*secondary pane 1*/
	0.46, 0.85,
	0.46, -0.03,
	0.95, -0.03,
	0.95, 0.85,
	/*secondary pane 2*/
	0.46, -0.05,
	0.46, -0.55,
	0.95, -0.55,
	0.95, -0.05,
	/*pop-up pane*/
	-0.3, 0.3,
	-0.3, -0.3,
	0.5, -0.3,
	0.5, 0.3,
	/*thumbnail pane 1*/
	-0.95, -0.65,
	-0.95, -0.95,
	-0.6, -0.95,
	-0.6, -0.65,

	/*thumbnail pane 2*/
	-0.55, -0.65,
	-0.55, -0.95,
	-0.2, -0.95,
	-0.2, -0.65,
	/*thumbnail pane 3*/
	-0.15, -0.65,
	-0.15, -0.95,
	0.2, -0.95,
	0.2, -0.65,
	/*thumbnail pane 4*/
	0.25, -0.65,
	0.25, -0.95,
	0.6, -0.95,
	0.6, -0.65,
	/*thumbnail pane 5*/
	0.65, -0.65,
	0.65, -0.95,
	0.99, -0.95,
	0.99, -0.65
};

GLushort indices[] = {
	0, 1, 2,
	2, 3, 0,

	4, 5, 6,
	6, 7, 4,

	8, 9, 10,
	10, 11, 8,

	12, 13, 14,
	14, 15, 12,

	16, 17, 18,
	18, 19, 16,

	/*thumbs*/
	20, 21, 22,
	22, 23, 20,

	24, 25, 26,
	26, 27, 24,

	28, 29, 30,
	30, 31, 28,

	32, 33, 34,
	34, 35, 32,

	36, 37, 38,
	38, 39, 36,
};

GLfloat texcoord[] = {
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,

	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
	0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0
};

GLfloat texcoord2[] = {
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,

	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0,
	0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0
};


GLfloat color[][4] = {
	/*bg*/
	{1, 1, 1, 1},
	{1, 1, 1, 1},
	{1, 1, 1, 1},
	{1, 1, 1, 1},
	/*mian*/
	{1, 1, 0, 1},
	{1, 1, 0, 1},
	{1, 1, 0, 1},
	{1, 1, 0, 1},
	/*aux1*/
	{0, 1, 0, 1},
	{0, 1, 0, 1},
	{0, 1, 0, 1},
	{0, 1, 0, 1},
	/*aux2*/
	{1, 0, 0, 1},
	{1, 0, 0, 1},
	{1, 0, 0, 1},
	{1, 0, 0, 1},
	/*POP-UP*/
	{0, 0, 0.5, 0.1},
	{0, 0, 0.5, 0.1},
	{0, 0, 0.5, 0.1},
	{0, 0, 0.5, 0.1},
	/*thumbs */
	{1, 0, 1, 1},
	{1, 0, 1, 1},
	{1, 0, 1, 1},
	{1, 0, 1, 1},

	{1, 0, 0.8, 0.8},
	{1, 0, 0.8, 0.8},
	{1, 0, 0.8, 0.8},
	{1, 0, 0.8, 0.8},

	{1, 0, 0.6, 0.6},
	{1, 0, 0.6, 0.6},
	{1, 0, 0.6, 0.6},
	{1, 0, 0.6, 0.6},

	{1, 0, 0.4, 0.4},
	{1, 0, 0.4, 0.4},
	{1, 0, 0.4, 0.4},
	{1, 0, 0.4, 0.4},

	{1, 0, 0.2, 0.2},
	{1, 0, 0.2, 0.2},
	{1, 0, 0.2, 0.2},
	{1, 0, 0.2, 0.2}
};

static const char *vert_shader_text =
		"attribute vec4 pos;\n"
		"attribute vec4 col;\n"
		"varying vec4 col_v;\n"
		"attribute vec2 texcoord;\n"
		"varying vec2 v_texcoord;\n"
		"void main() {\n"
		"  col_v = col;\n"
		"  gl_Position = pos;\n"
		"  v_texcoord=texcoord;\n"
		"}\n";


static const char *frag_shader_text =
		"precision mediump float;\n"
		"varying vec4 col_v;\n"
		"uniform int graytile;\n"
		"varying vec2 v_texcoord;\n"
		"uniform sampler2D textures;\n"
		"void main() {\n"
		"  if (graytile == 0) {\n"
		"    gl_FragColor = texture2D(textures, v_texcoord)\n;"
		"  } else {\n"
		"    gl_FragColor = col_v;\n"
		"       }\n"
		"}\n";

static GLuint create_shader(const char *source, GLenum shader_type)
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


static int
is_sprite(struct weston_surface *surface)
{
	struct weston_seat *seat;
	struct weston_pointer *pointer = NULL;

	wl_list_for_each(seat, &thumb.compositor->seat_list, link) {
		pointer = weston_seat_get_pointer(seat);
		/* Skip  cursor sprites */
		if (surface == pointer->sprite->surface) {
			return 1;
		}
	}

	return 0;
}



/*
 * Build the surface list. This walks the surface list and figures out
 * where each surface should be displayed. 
 */
static void
build_surface_list(struct weston_compositor *compositor)
{
	struct weston_view *view;
	int i;

	thumb.thumbs = 0;
	thumb.appcnt = 0;
	thumb.background = NULL;

	/*
	 * Walk the surface list and determine where each surface should go
	 *
	 * Background, popup, cursor surfaces are ignored.
	 */
	wl_list_for_each(view, &compositor->view_list, link)
	{
		/* Ignore surfaces we don't handle */
		if (ias_get_zorder(view->surface) == SHELL_SURFACE_ZORDER_POPUP ||
				/* TODO removing these lines for now to fix a build error,
				 * this whole example will be rewritten in the near future */
				/*surface->shader == &compositor->solid_shader ||*/
				is_sprite(view->surface) ||
				/*surface->num_textures > 1*/ 0) {
			continue;
		}

		if (ias_get_zorder(view->surface) == SHELL_SURFACE_ZORDER_BACKGROUND) {
			thumb.background = view->surface;
			continue;
		}

		/*
		 * Use the behavior bits to route applications to specific areas
		 * on the screen.
		 *   bit 1 = upper right area
		 *   bit 2 = lower right area
		 *   bit 3 = main area
		 */
		if (ias_get_behavior_bits(view->surface) == 1) {
			thumb.show |= S_AUX1;
			thumb.apps[thumb.appcnt] = view->surface;
			thumb.aux1 = thumb.appcnt++;
			continue;
		}

		if (ias_get_behavior_bits(view->surface) == 2) {
			thumb.show |= S_AUX2;
			thumb.apps[thumb.appcnt] = view->surface;
			thumb.aux2 = thumb.appcnt++;
			continue;
		}

		if (ias_get_behavior_bits(view->surface) == 4) {
			thumb.show |= S_MAIN;
			thumb.apps[thumb.appcnt] = view->surface;
			thumb.main = thumb.appcnt++;
			continue;
		}

		if (thumb.appcnt < MAX_THUMBS) {
			thumb.apps[thumb.appcnt++] = view->surface;
		}
	}

	/* Fill in thumbnail containers */
	thumb.thumbs = 0;
	for (i = 0; i < thumb.appcnt; i++) {
		/*
		 * This makes the thumb nail list dynamic so that only those
		 * applications that are not being displayed on one of the
		 * panels show up in the list.
		 *
		 * Is this a good UI?  Probably not.  Right now, main and aux2
		 * are selectable so removing those conditions below will
		 * likely improve the UI (non-dynamic thumbnail list.
		 */
		if ((thumb.show & S_MAIN) && (i == thumb.main)) {
			continue;
		}
		if ((thumb.show & S_AUX1) && (i == thumb.aux1)) {
			continue;
		}
		if ((thumb.show & S_AUX2) && (i == thumb.aux2)) {
			continue;
		}
		thumb.thumb_list[thumb.thumbs++] = i;
	}
}


void thumb_draw(struct weston_output *output)
{
	struct weston_compositor *compositor = output->compositor;
	int i = 0;

	glViewport(0, 0, output->width, output->height);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	build_surface_list(compositor);

	glUseProgram(program);
	glVertexAttribPointer(pos_atr, 2, GL_FLOAT, GL_FALSE, 0, vert);
	glVertexAttribPointer(tex, 2, GL_FLOAT, GL_FALSE, 0, texcoord);
	glVertexAttribPointer(col_atr, 4, GL_FLOAT, GL_FALSE, 0, color);
	glEnableVertexAttribArray(pos_atr);
	glEnableVertexAttribArray(tex);
	glEnableVertexAttribArray(col_atr);

	if (thumb.background != NULL) {
		/* TODO removing this reference to thumb.background->textures[0] for now
		 * to fix a build error. When porting this sample to spug, use
		 * spug_view_texture() to get the view's texture name */
		glBindTexture(GL_TEXTURE_2D, 0 /*thumb.background->textures[0]*/);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glUniform1i(grey_uniform, 0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	if (thumb.show & S_MAIN) {
		/* TODO removing this reference to thumb.apps[thumb.main]->textures[0]
		 * for now to fix a build error. When porting this sample to spug, use
		 * spug_view_texture() to get the view's texture name */
		glBindTexture(GL_TEXTURE_2D, 0 /*thumb.apps[thumb.main]->textures[0]*/);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glUniform1i(grey_uniform, 0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 6);
		glBindTexture(GL_TEXTURE_2D, 0);
	} else {
		glUniform1i(grey_uniform, 1);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 6);
	}

	/* Draw upper right area */
	if (thumb.show & S_AUX1) {
		/* TODO removing this reference to thumb.apps[thumb.aux1]->textures[0]
		 * for now to fix a build error. When porting this sample to spug, use
		 * spug_view_texture() to get the view's texture name */
		glBindTexture(GL_TEXTURE_2D, 0 /*thumb.apps[thumb.aux1]->textures[0]*/);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glUniform1i(grey_uniform, 0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 12);
		glBindTexture(GL_TEXTURE_2D, 0);
	} else {
		glUniform1i(grey_uniform, 1);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 12);
	}

	/* Draw lower right area */
	if (thumb.show & S_AUX2) {
		/* TODO removing this reference to thumb.apps[thumb.aux2]->textures[0]
		 * for now to fix a build error. When porting this sample to spug, use
		 * spug_view_texture() to get the view's texture name */
		glBindTexture(GL_TEXTURE_2D, 0 /*thumb.apps[thumb.aux2]->textures[0]*/);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glUniform1i(grey_uniform, 0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 18);
		glBindTexture(GL_TEXTURE_2D, 0);
	} else {
		glUniform1i(grey_uniform, 1);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 18);
	}

	/* Draw centered overlay */
#if 0
	if (isPop) {
		glBindTexture(GL_TEXTURE_2D, panel.popup->textures[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glUniform1i(grey_uniform, 0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 24);
		glBindTexture(GL_TEXTURE_2D, 0);
	} else {
		glUniform1i(grey_uniform, 1);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices + 24);
	}
#endif

	/*DRAW THUMBS*/
	for (i = 0; i < thumb.thumbs; i++) {
		/* TODO removing this reference to thumb.apps[thumb.thumb_list[i]]->textures[0]
		 * for now to fix a build error. When porting this sample to spug, use
		 * spug_view_texture() to get the view's texture name */
		glBindTexture(GL_TEXTURE_2D, 0/*thumb.apps[thumb.thumb_list[i]]->textures[0]*/);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glUniform1i(grey_uniform, 0);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT,
				(indices + (30 + (i * 6))));
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glDisableVertexAttribArray(pos_atr);
	glDisableVertexAttribArray(tex);
	glDisableVertexAttribArray(col_atr);
}


/*
 * Called when mouse pointer focus changes to ??
 */
static void
thumb_grab_focus(struct weston_pointer_grab *grab)
{
	/* printf("THUMB:  Focus change\n"); */
}

static void
thumb_grab_motion(struct weston_pointer_grab *grab,
		const struct timespec *time,
		struct weston_pointer_motion_event *event)
{
	/*
	 * The mouse isn't supported in this version. Only touch input
	 * is being used.
	 */
	/*
	mouse_x = wl_fixed_to_int(x);
	mouse_y = wl_fixed_to_int(y);
	*/
}

static void
thumb_grab_button(struct weston_pointer_grab *grab,
	       const struct timespec *time, uint32_t button, uint32_t state)
{
}

static void
thumb_grab_axis(spug_pointer_grab *grab,
		     const struct timespec *time,
		     struct weston_pointer_axis_event *event) {
}


static void
thumb_grab_axis_source(spug_pointer_grab *grab, uint32_t source) {
}


static void
thumb_grab_frame(spug_pointer_grab *grab) {
}


static void
thumb_grab_cancel(spug_pointer_grab *grab)
{
	/* Noop */
}

static struct weston_pointer_grab_interface mouse_grab_interface = {
	thumb_grab_focus,
	thumb_grab_motion,
	thumb_grab_button,
	thumb_grab_axis,
	thumb_grab_axis_source,
	thumb_grab_frame,
	thumb_grab_cancel
};

static void
thumb_grab_key(struct weston_keyboard_grab *grab, const struct timespec *time,
		    uint32_t key, uint32_t state)
{
	struct weston_output *output;

	/*
	 * Only deal with key down events.  Ignore repeat & up events
	 * if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
	 * return;
	 * }
	 */
	switch (key) {
		case KEY_Q:
			//stateX = state;
			break;
		case KEY_W:
			//stateY = state;
			break;
		case KEY_E:
			//stateZ = state;
			break;

		case KEY_ENTER:
			//TODO: Something smart to maximize the selected application

			// Deactivate plugin on all outputs
			wl_list_for_each(output, &thumb.compositor->output_list, link) {
				ias_deactivate_plugin(output);
			}

			break;
		case KEY_SPACE:
			break;
		default:
			break;
	}
}

static void
thumb_grab_modifiers(struct weston_keyboard_grab *grab, uint32_t serial,
			  uint32_t mods_depressed, uint32_t mods_latched,
			  uint32_t mods_locked, uint32_t group)
{
	/* Noop */
}

static struct weston_keyboard_grab_interface key_grab_interface = {
	thumb_grab_key,
	thumb_grab_modifiers,
};


/*
 * Called when the layout plug-in is activated.
 *
 * Allocate and initialize our plug-in object.
 */
static void
thumb_switch_to(struct weston_output * output)
{
	struct weston_pointer *pointer = weston_seat_get_pointer(thumb.seat);
	thumb.compositor = output->compositor;
	thumb.touch_x = wl_fixed_to_double(pointer->x);
	thumb.touch_y = wl_fixed_to_double(pointer->y);

	thumb.thumbs = 0;
	thumb.main = -1;
	thumb.aux1 = -1;
	thumb.aux2 = -1;
	thumb.show = 0;
}


static void
thumb_grab_touch_down(struct weston_touch_grab *grab,
		const struct timespec *time,
		int id,
		wl_fixed_t sx,
		wl_fixed_t sy)
{
	thumb.touch_x = wl_fixed_to_double(grab->touch->grab_x);
	thumb.touch_y = wl_fixed_to_double(grab->touch->grab_y);
	thumb.pressed = time;
}

static void
thumb_grab_touch_up(struct weston_touch_grab *grab, const struct timespec *time, int id)
{
	int which_thumb = -1;

	if (thumb.swiped) {
		if ((abs(thumb.swipe_x - thumb.swipe_end_x) > 100) ||
				(abs(thumb.swipe_y - thumb.swipe_end_y) > 80)) {
			/*
			 * Attempting to make the interface respond to swipe/
			 * drag type events. The difference between a swipe
			 * and a drag is how long the finger is on the screen.
			 * Swipes are much shorter than drags.
			 */
			if ((time - thumb.swiped) < 1000) {
				printf("MOTION swipe event!!!\n");
			} else {
				printf("MOTION drag event!!!\n");
			}
		}
		thumb.swiped = 0;
	}


	if (thumb.touch_y > (SCREEN_H / 1.6)) {
		which_thumb = trunc(thumb.touch_x / 204);
		printf("Thumb %d touched\n", which_thumb);
	} else {
		/* Not on a thumb, maybe in area 2? */
		if ((thumb.touch_y > 404) && (thumb.touch_y < 596) &&
				(thumb.touch_x > 743) && (thumb.touch_x < 1000)) {
			thumb.show &= ~ S_AUX2;
			thumb.aux2 = -1;
		}
	}

	if (which_thumb >= 0 && which_thumb < thumb.thumbs) {
		if ((time - thumb.pressed) > 900) {  /* almost 1 second */
			/* Hold and press moves thumb to aux area */
			thumb.aux2 = thumb.thumb_list[which_thumb];
			thumb.show |= S_AUX2;
		} else {
			/* press moves thumb to main area */
			thumb.main = thumb.thumb_list[which_thumb];
			thumb.show |= S_MAIN;
		}
	}
}

/*
 * Keep track of the start and end times that the finger is on
 * the screen. This is used to determine if the user swiped,
 * dragged, or pressed.
 */
static void
thumb_grab_touch_motion(struct weston_touch_grab *grab,
		const struct timespec *time, int id,
		wl_fixed_t sx, wl_fixed_t sy)
{
	if (thumb.swiped == 0) {
		thumb.swipe_x = wl_fixed_to_double(sx);
		thumb.swipe_y = wl_fixed_to_double(sy);
		thumb.swiped = time;
	} else {
		thumb.swipe_end_x = wl_fixed_to_double(sx);
		thumb.swipe_end_y = wl_fixed_to_double(sy);
	}
}


static struct weston_touch_grab_interface touch_grab_interface = {
	thumb_grab_touch_down,
	thumb_grab_touch_up,
	thumb_grab_touch_motion,
};




WL_EXPORT int
ias_plugin_init(struct ias_plugin_info *info,
		ias_identifier id,
		uint32_t version,
		struct weston_compositor *ec)
{
	struct weston_seat *seat;
	GLuint frag, vert;
	GLint status;

	/*
	 * This plugin is written for inforec version 1, so that's all we fill
	 * in, regardless of what gets passed in for the version parameter.
	 */
	info->draw = (ias_draw_fn)thumb_draw;
	info->mouse_grab.interface = &mouse_grab_interface;
	info->key_grab.interface = &key_grab_interface;
	info->touch_grab.interface = &touch_grab_interface;
	info->switch_to = (ias_switchto_fn)thumb_switch_to;

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

	//TODO add bind to attributes
	pos_atr = glGetAttribLocation(program, "pos");
	col_atr = glGetAttribLocation(program, "col");
	tex = glGetAttribLocation(program, "texcoord");
	grey_uniform = glGetUniformLocation(program, "graytile");

	/* Save off seat. */
	wl_list_for_each(seat, &ec->seat_list, link) {
		thumb.seat = seat;
	}

	thumb.compositor = ec;

	return 0;
}
