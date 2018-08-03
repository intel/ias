#include "config.h"
#include "vm.h"
#include <sched.h>
#ifdef HYPER_DMABUF
#include <xen/hyper_dmabuf.h>
#endif

static struct gl_shader std_shader;
static GLuint pos_att, tex_att;
static GLuint tex_uniform, bufpos_uniform;

char vm_data[METADATA_BUFFER_SIZE];
int vm_data_offset;

#define METADATA_SEND_RETRIES 10
#define METADATA_SEND_SLEEP 1000

#define VBT_VERSION 3

static struct hyper_communication_interface comm_interface;
static void *comm_module;

static const char *vertex_shader =
	"uniform mat4 proj;\n"
	"uniform int bufpos;\n"
	"attribute vec2 position;\n"
	"attribute vec2 texcoord;\n"
	"varying vec2 v_texcoord;\n"
	"\n"
	"void main() {\n"
	"  vec2 p;\n"
	"  p = position + vec2(bufpos, 0.0);\n"
	"  gl_Position = proj * vec4(p, 0.0, 1.0);\n"
	"  v_texcoord = texcoord;\n"
	"}\n";

static const char *solid_fragment_shader =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(tex, v_texcoord)\n;"
	;

int vm_init(struct gl_renderer *gr)
{
	struct vm_buffer_table *vbt;
	char *err;

	gr->vm_buffer_table = (struct vm_buffer_table *) zalloc(
			sizeof(struct vm_buffer_table));
	vbt = gr->vm_buffer_table;
	vbt->h.version = VBT_VERSION;
	vbt->h.counter = 0;
	vbt->h.n_buffers = 0;

	wl_list_init(&vbt->vm_buffer_info_list);
	wl_list_init(&gr->vm_bufs_pinned_list);

	if (!gl_renderer_interface.vm_use_ggtt) {
		if (!gl_renderer_interface.vm_plugin_path || strlen(gl_renderer_interface.vm_plugin_path) == 0) {
			weston_log("No VM plugin provided\n");
			return -1;
		}

		weston_log("Loading %s\n", gl_renderer_interface.vm_plugin_path);

		comm_module = dlopen(gl_renderer_interface.vm_plugin_path,
				     RTLD_NOW | RTLD_LOCAL);
		err = dlerror();

		if (comm_module == NULL) {
			weston_log("Failed to load communications library '%s': %s\n",
				   gl_renderer_interface.vm_plugin_path, err);
			return -2;
		}

		init_comm_interface comm_init = dlsym(comm_module, "init_comm");

		if (comm_init == NULL) {
			weston_log("find init_comm function == NULL\n");
			return -2;
		}

		if ((err = dlerror()) != NULL) {
			weston_log("Failed to find init_comm function: %s\n", err);
			return -2;
		}
		if (comm_init(&comm_interface, 0,
			      METADATA_BUFFER_SIZE,
			      gl_renderer_interface.vm_plugin_args)) {
			weston_log("hypervisor communication channel initialization failed\n");
			return -1;
		}
		weston_log("Succesfully loaded hypervisor communication channel\n");
	}
	return 0;
}

static void unpin(struct gr_buffer_ref *gr_buf)
{
	if(!gr_buf) {
		return;
	}

	if (gl_renderer_interface.vm_use_ggtt) {
		if ((gr_buf->buffer != NULL) && (gr_buf->buffer->priv_buffer != NULL)) {
			unpin_bo(gr_buf->gr, gr_buf->buffer->legacy_buffer);
		}
	}

	wl_list_remove(&gr_buf->elm);

	if(gl_renderer_interface.vm_dbg &&
	   gl_renderer_interface.vm_use_ggtt) {
		weston_log("UnPinned ggtt_offset = 0x%lX\n",
			gr_buf->vm_buffer_info.ggtt_offset);
	}
}

static void add_marker_to_vm_data(int val)
{
	memcpy(&vm_data[vm_data_offset], &val, sizeof(val));
	vm_data_offset += sizeof(val);
}

static void add_to_vm_data(void *buf, int len)
{
	memcpy(&vm_data[vm_data_offset], buf, len);
	vm_data_offset += len;
}

void vm_table_clean(struct gl_renderer *gr)
{
	struct gr_buffer_ref *gr_buf, *tmp;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	int i = 0, rc;
	int retries = METADATA_SEND_RETRIES;

	if (!gl_renderer_interface.vm_use_ggtt) {
		while (comm_interface.available_space() < vm_data_offset && retries--) {
			usleep(METADATA_SEND_SLEEP);
		}

		if (comm_interface.available_space() >= vm_data_offset) {
			if (comm_interface.send_data != NULL) {
				add_marker_to_vm_data(METADATA_STREAM_END);
				do {
					rc = comm_interface.send_data(&vm_data[i], vm_data_offset - i);
					i += rc;
				} while (i != vm_data_offset && rc >= 0);
			}
		} else {
			printf("No space in comm channel - skipping frame %d < %d\n",
				comm_interface.available_space(), vm_data_offset);
		}
		vm_data_offset = 0;
		memset(vm_data, 0, METADATA_BUFFER_SIZE);
	}

	/* remove any buffer refs inside this table */
	wl_list_for_each_safe(gr_buf, tmp, &vbt->vm_buffer_info_list, elm) {
		if (!gl_renderer_interface.vm_pin) {
			unpin(gr_buf);
		} else {
			/* don't unpin buffer, only remove it from the list */
			wl_list_remove(&gr_buf->elm);
		}
		gr_buf->cleanup_required = 0;
	}
}

static void vm_buf_unpin(struct gl_renderer *gr, unsigned long int ggtt_offset)
{
	struct gr_buffer_ref *gr_buf, *tmp;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;

	/* remove one buffer ref inside this table */
	wl_list_for_each_safe(gr_buf, tmp, &vbt->vm_buffer_info_list, elm) {
		if (gr_buf->vm_buffer_info.ggtt_offset == ggtt_offset) {
			unpin(gr_buf);
			gr_buf->cleanup_required = 0;
			break;
		}
	}
}

static void buffer_destroy(struct gr_buffer_ref *gr_buf)
{
	struct vm_bufs_pinned* pinned;
	int i;
	const int bufs_number = gl_renderer_interface.vm_bufs;


	if (!gl_renderer_interface.vm_use_ggtt) {
		unexport_bo(gr_buf->backend, &(gr_buf->vm_buffer_info));
		if (gr_buf->elm.next || gr_buf->elm.prev) {
			wl_list_remove(&gr_buf->elm);
		}
	} else {
		if (gl_renderer_interface.vm_pin) {
			/* when buffers are constantly pinned we must also remove its ggtt_offset
			 * from vm_bufs_pinned_list
			 */
			wl_list_for_each(pinned, &gr_buf->gr->vm_bufs_pinned_list, elm) {
				if (pinned->len == 0 ||
					pinned->surface != gr_buf->surface) {
					continue;
				}

				/* find ggtt_offset in offstes table */
				for (i = 0; i < bufs_number; i++) {
					if (pinned->bufs[i].ggtt_offset != gr_buf->vm_buffer_info.ggtt_offset) {
						continue;
					}

					if(gl_renderer_interface.vm_dbg) {
						weston_log("Remove offset 0x%lX\n", pinned->bufs[i].ggtt_offset);
					}

					pinned->bufs[i].ggtt_offset = 0;
					pinned->len--;

					/* when this is last ggtt_offset for the surface,
					 * remove the surface entry from the list */
					if(!pinned->len) {
						if(gl_renderer_interface.vm_dbg) {
							weston_log("Remove surface 0x%lX\n", (unsigned long int)pinned->surface);
						}
						pinned->idx = 0;
						pinned->surface = 0;
						wl_list_remove(&pinned->elm);
						free(pinned);
					}
					break;
				}
				break;
			}
		}

		if(gr_buf->cleanup_required) {
			unpin(gr_buf);
		}
	}
	gr_buf->buffer->priv_buffer = NULL;
	free(gr_buf);
}

void vm_destroy(struct gl_renderer * gr)
{
	struct gr_buffer_ref *gr_buf, *tmp;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;

	/* free any buffers inside this table */
	wl_list_for_each_safe(gr_buf, tmp, &vbt->vm_buffer_info_list, elm) {
		buffer_destroy(gr_buf);
	}

	free(gr->vm_buffer_table);

	if (!gl_renderer_interface.vm_use_ggtt) {
		if (comm_interface.cleanup != NULL) {
			comm_interface.cleanup();
		}
		if (comm_module != NULL) {
			dlclose(comm_module);
		}
	}
}

static void
gr_buffer_destroy_handler(struct wl_listener *listener,
				       void *data)
{
	struct gr_buffer_ref *gr_buf =
		container_of(listener, struct gr_buffer_ref, buffer_destroy_listener);

	buffer_destroy(gr_buf);
}

static void print_vm_buf_list(struct gl_renderer *gr)
{
	struct gr_buffer_ref *gr_buf;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;

	weston_log("+++++++++++++++++++++++++\n");

	weston_log("version = %d\n", vbt->h.version);
	weston_log("counter = %d\n", vbt->h.counter);
	weston_log("n_buffers = %d\n\n", vbt->h.n_buffers);


	wl_list_for_each(gr_buf, &vbt->vm_buffer_info_list, elm) {
		weston_log("Surface name = %s\n", gr_buf->vm_buffer_info.surface_name);
		weston_log("Surface id = %ld\n", gr_buf->vm_buffer_info.surface_id);
		weston_log("Width = %d\n", gr_buf->vm_buffer_info.width);
		weston_log("Height = %d\n", gr_buf->vm_buffer_info.height);
		weston_log("Pitch[0] = %d offset[0] = %d\n",
				 gr_buf->vm_buffer_info.pitch[0],
				 gr_buf->vm_buffer_info.offset[0]);
		weston_log("Pitch[1] = %d offset[1] = %d\n",
				 gr_buf->vm_buffer_info.pitch[1],
				 gr_buf->vm_buffer_info.offset[1]);
		weston_log("Pitch[2] = %d offset[2] = %d\n",
				 gr_buf->vm_buffer_info.pitch[2],
				 gr_buf->vm_buffer_info.offset[2]);
		weston_log("Tiling= %d\n", gr_buf->vm_buffer_info.tile_format);
		weston_log("Pixel format = 0x%X\n", gr_buf->vm_buffer_info.format);
		if (gl_renderer_interface.vm_use_ggtt) {
			weston_log("ggtt_offset = 0x%lX\n", gr_buf->vm_buffer_info.ggtt_offset);
		} else {
			weston_log("hyper_dmabuf id = %X\n", gr_buf->vm_buffer_info.hyper_dmabuf_id.id);
		}
		weston_log("counter = %d\n", gr_buf->vm_buffer_info.counter);
		weston_log("%s\n\n", gr_buf->vm_buffer_info.status & UPDATED ? "Updated" : "Not Updated");
	}
	weston_log("-------------------------\n");
}

static int get_format(struct ias_backend *bc, struct weston_buffer *buffer)
{
	struct gbm_bo *bo;
	uint32_t format;

	bo = gbm_bo_import(bc->gbm, GBM_BO_IMPORT_WL_BUFFER, buffer->resource, GBM_BO_USE_SCANOUT);
	if (!bo) {
		return 0;
	}

	format = gbm_bo_get_format(bo);
	gbm_bo_destroy(bo);

	return format;
}

static void vm_print_gtt_offset_list(struct gl_renderer *gr)
{
	int i;
	struct vm_bufs_pinned* pinned;
	const int bufs_number = gl_renderer_interface.vm_bufs;

	wl_list_for_each(pinned, &gr->vm_bufs_pinned_list, elm) {
		weston_log("-----------------------------------------------------------\n");
		weston_log("surface: 0x%lX\n",(unsigned long int)pinned->surface);
		weston_log("len: %d\n",pinned->len);
		weston_log("idx: %d\n",pinned->idx);
		weston_log("bufs: [");
		for (i = 0; i < bufs_number; i++) {
			printf("0x%lX", (unsigned long int)pinned->bufs[i].ggtt_offset);
			if (i < bufs_number -1) weston_log(" ,");
		}
		weston_log("]\n");
	}
}

static int vm_save_current_ggtt_offset(struct gl_renderer *gr, struct weston_surface *surface,
										struct weston_buffer *buffer)
{
	struct vm_bufs_pinned* pinned;
	int surf_found = 0;
	unsigned long ggtt_offset = 0;
	const int bufs_number = gl_renderer_interface.vm_bufs;
	int i;

	wl_list_for_each(pinned, &gr->vm_bufs_pinned_list, elm) {
		/* Find entry related to the surface */
		if (pinned->surface != surface) {
			continue;
		}
		surf_found = 1;
		/* When existing surface is resized, its pinned buffers
		 * should be unpinned and pinned again
		 */
		if ((unsigned int)pinned->width != (unsigned int)buffer->width ||
			(unsigned int)pinned->height != (unsigned int)buffer->height) {
			for (i = 0; i < bufs_number; i++) {
				vm_buf_unpin(gr, pinned->bufs[i].ggtt_offset);
				pinned->bufs[i].ggtt_offset = 0;
				pinned->bufs[i].buffer = NULL;
			}
			pinned->len = 0;
		}
		/* pin all bufs related to the surface */
		if (pinned->len < bufs_number) {
			int ret= gr->pin_bo(gr->egl_display, buffer->legacy_buffer, &ggtt_offset);
			if (ret && ggtt_offset) {
				int free = -1;
				int exist = 0;
				/* Find free place in bufs array */
				for (i = 0; i < bufs_number; i++) {
					/* if ggtt_offset was previously added to the array
					 * don't add it again
					 */
					if (pinned->bufs[i].ggtt_offset == ggtt_offset) {
						exist=1;
						break;
					}
					if (!pinned->bufs[i].ggtt_offset && free == -1) {
						free=i;
					}
				}

				if (exist == 0 && free != -1) {
					pinned->bufs[free].ggtt_offset = ggtt_offset;
					pinned->bufs[free].buffer =  buffer->legacy_buffer;
					pinned->width = buffer->width;
					pinned->height = buffer->height;
					pinned->len++;
				}

				if (gl_renderer_interface.vm_dbg) {
					weston_log("Pinned ggtt_offset = 0x%lX\n", ggtt_offset);
				}
			}
		}
		break;
	}

	/* when the surface is new, create entry and add it to the list*/
	if (!surf_found) {
		int ret= gr->pin_bo(gr->egl_display, buffer->legacy_buffer, &ggtt_offset);
		if (ret && ggtt_offset) {
			pinned = zalloc(sizeof(struct vm_bufs_pinned));
			if (!pinned) {
				weston_log("Cannot allocate memory\n");
				return -1;
			}

			pinned->surface = surface;
			pinned->bufs[0].ggtt_offset = ggtt_offset;
			pinned->bufs[0].buffer =  buffer->legacy_buffer;
			pinned->width = buffer->width;
			pinned->height = buffer->height;
			pinned->len = 1;
			wl_list_insert(&gr->vm_bufs_pinned_list, &pinned->elm);

			if (gl_renderer_interface.vm_dbg) {
				weston_log("Pinned ggtt_offset = 0x%lX\n", ggtt_offset);
			}
		}
	}
	return 0;
}

static unsigned long vm_get_current_ggtt_offset(struct gl_renderer *gr, struct weston_surface *surface, struct weston_buffer *buffer)
{
	struct vm_bufs_pinned* pinned;
	struct pinned_info* pi;
	unsigned long ggtt_offset = 0;
	const int bufs_number = gl_renderer_interface.vm_bufs;
	int i;

	wl_list_for_each(pinned, &gr->vm_bufs_pinned_list, elm) {
		/* Find entry related to the surface */
		if (pinned->surface != surface) {
			continue;
		}
		/* get ggtt_offset that should be passed to the buffer table */
		for (i = 0; i < bufs_number; i++) {
			pi = &pinned->bufs[pinned->idx];
			pinned->idx = (pinned->idx < bufs_number -1) ? pinned->idx + 1: 0;
			/* current ggtt_offset passed to buffer table
			 * should be in sync with the current buffer used by mesa,
			 * so check if buffer->legacy_buffer is the one pinned to
			 * proper ggtt_offset, if not check next item from bufs[]
			 * to find proper ggtt_offset.
			 * This is important when for example mesa loses frames
			 */
			if (pi &&
				pi->ggtt_offset &&
				pi->buffer == buffer->legacy_buffer) {
				ggtt_offset = pi->ggtt_offset;
				break;
			}
		}
	}

	if (gl_renderer_interface.vm_dbg) {
		vm_print_gtt_offset_list(gr);
	}

	return ggtt_offset;
}

void vm_add_buf(struct weston_compositor *ec, struct gl_renderer *gr,
		struct gl_surface_state *gs, struct ias_backend *bc, struct weston_buffer *buffer,
		struct weston_view *view, int buf_ind)
{
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	struct gr_buffer_ref *gr_buffer_ref_ptr;
	int32_t tiling_format[2];
	unsigned long ggtt_offset;
	struct gbm_bo *bo = NULL;
	struct weston_surface *surface = view->surface;

	if(!buffer->priv_buffer) {
		gr_buffer_ref_ptr = zalloc(sizeof(struct gr_buffer_ref));
		gr_buffer_ref_ptr->buffer = buffer;
		gr_buffer_ref_ptr->backend = bc;
		gr_buffer_ref_ptr->gr = gr;
		gr_buffer_ref_ptr->surface = surface;
		gr_buffer_ref_ptr->buffer_destroy_listener.notify =
				gr_buffer_destroy_handler;
		buffer->priv_buffer = gr_buffer_ref_ptr;
		wl_list_init(&gr_buffer_ref_ptr->elm);
		wl_signal_add(&buffer->destroy_signal,
				&gr_buffer_ref_ptr->buffer_destroy_listener);
	} else {
		gr_buffer_ref_ptr = buffer->priv_buffer;
	}

	wl_list_insert(&vbt->vm_buffer_info_list, &gr_buffer_ref_ptr->elm);

	/* a surface may use multiple buffers, so it isn't ok to just increment the
	 * buffer's counter. Instead, make sure that the buffer we're about to show
	 * share has the surface's up-to-date frame count */
	gr_buffer_ref_ptr->vm_buffer_info.surf_index = buf_ind;
	gr_buffer_ref_ptr->vm_buffer_info.counter = gs->frame_count;
	gr_buffer_ref_ptr->vm_buffer_info.width = buffer->width;
	gr_buffer_ref_ptr->vm_buffer_info.height = buffer->height;
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_TEXTURE_FORMAT,
			  &(gr_buffer_ref_ptr->vm_buffer_info.format));
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_STRIDE,
			  gr_buffer_ref_ptr->vm_buffer_info.pitch);
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_OFFSET,
			  gr_buffer_ref_ptr->vm_buffer_info.offset);
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_TILING,
			  tiling_format);
	gr_buffer_ref_ptr->vm_buffer_info.format = get_format(bc, buffer);

	gr_buffer_ref_ptr->vm_buffer_info.status |= UPDATED;
	gr_buffer_ref_ptr->vm_buffer_info.tile_format = tiling_format[0];

	if(ec->renderer->get_surf_id) {
		gr_buffer_ref_ptr->vm_buffer_info.surface_id =
			ec->renderer->get_surf_id(gs->surface);
	}

	gr_buffer_ref_ptr->vm_buffer_info.bbox[0] =
		view->transform.boundingbox.extents.x1;
	gr_buffer_ref_ptr->vm_buffer_info.bbox[1] =
		view->transform.boundingbox.extents.y1;
	gr_buffer_ref_ptr->vm_buffer_info.bbox[2] =
		view->transform.boundingbox.extents.x2 - gr_buffer_ref_ptr->vm_buffer_info.bbox[0];
	gr_buffer_ref_ptr->vm_buffer_info.bbox[3] =
		view->transform.boundingbox.extents.y2 - gr_buffer_ref_ptr->vm_buffer_info.bbox[1];

	if(ec->renderer->fill_surf_name) {
		ec->renderer->fill_surf_name(gs->surface, SURFACE_NAME_LENGTH - 1,
				gr_buffer_ref_ptr->vm_buffer_info.surface_name);
	}

	if (ec->renderer->get_surf_rotation) {
		gr_buffer_ref_ptr->vm_buffer_info.rotation =
				ec->renderer->get_surf_rotation(gs->surface);
	}

	if(gl_renderer_interface.vm_dbg) {
		print_vm_buf_list(gr);
	}

	gr_buffer_ref_ptr->cleanup_required = 1;
	if (!gl_renderer_interface.vm_use_ggtt) {
		bo = gbm_bo_import(bc->gbm, GBM_BO_IMPORT_WL_BUFFER, buffer->resource, GBM_BO_USE_SCANOUT);
		gr_buffer_ref_ptr->bo = bo;
		if (!bo) {
			return;
		}
	} else {
		if (!gl_renderer_interface.vm_pin) {
			pin_bo(gr, buffer->legacy_buffer,
					&(gr_buffer_ref_ptr->vm_buffer_info));
		} else {
			/* if necessary, pin buffer and save its ggtt_offset */
			if(vm_save_current_ggtt_offset(gr, surface, buffer) != 0) {
				return;
			}

			/* get current bufs from the offset list */
			ggtt_offset = vm_get_current_ggtt_offset(gr, surface, buffer);
			if (ggtt_offset) {
				gr_buffer_ref_ptr->vm_buffer_info.ggtt_offset = ggtt_offset;
			} else {
				return;
			}
		}
	}

}

static int wait_for_gpu(const struct wl_list * list, int drm_fd)
{
	struct gr_buffer_ref *gr_buffer_ref_ptr;
	int result = 0;
	wl_list_for_each_reverse(gr_buffer_ref_ptr, list, elm) {
		if (gr_buffer_ref_ptr->bo != NULL) {
			struct drm_i915_gem_busy busy = {};
			busy.handle = gbm_bo_get_handle(gr_buffer_ref_ptr->bo).u32;
			drmIoctl(drm_fd, DRM_IOCTL_I915_GEM_BUSY, &busy);
			while (busy.busy != 0) {
				result = 1;
				sched_yield();
				drmIoctl(drm_fd, DRM_IOCTL_I915_GEM_BUSY, &busy);
			}
			gbm_bo_destroy(gr_buffer_ref_ptr->bo);
		}
	}
	return result;
}

int vm_table_expose(struct weston_output *output, struct gl_output_state *go,
		    struct gl_renderer *gr)
{
	struct weston_compositor *ec = output->compositor;
	struct ias_output *ias_output = (struct ias_output *) output;
	struct ias_backend *bc = ias_output->ias_crtc->backend;
	struct weston_view *view;
	struct gl_surface_state *gs;
	struct gr_buffer_ref *gr_buf;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	EGLint buffer_age = 0;
	EGLBoolean ret;
	int drm_fd = bc->drm.fd;
	int shareable = 0;
	struct weston_output *o;
	int output_num = 0;
	int buf_ind = 0;
	struct gl_border_image *top, *bottom, *left, *right;

	if (gl_renderer_interface.vm_use_ggtt) {
		/* Fallback to old metadata sharing way*/
		return vm_table_draw(output, go, gr);
	}

	wl_list_for_each(o, &ec->output_list, link) {
		if (output == o)
			break;
		output_num++;
	}

	if (output_num > VM_MAX_OUTPUTS) {
		weston_log("Exceeding maximum number of outputs supported by vm\n");
		return 1;
	}

	if (gr->has_egl_buffer_age) {
		ret = eglQuerySurface(gr->egl_display, go->egl_surface,
				      EGL_BUFFER_AGE_EXT, &buffer_age);
		if (ret == EGL_FALSE) {
			weston_log("buffer age query failed.\n");
		}
	}

	wl_list_for_each_reverse(view, &ec->view_list, link) {
		pixman_region32_t output_overlap;
		pixman_region32_init(&output_overlap);
		pixman_region32_intersect(&output_overlap, &view->transform.boundingbox, &output->region);
		if (!pixman_region32_not_empty(&output_overlap)) {
			continue;
		}

		if (ias_output->vm && view->plane == &ec->primary_plane) {
			gs = get_surface_state(view->surface);
			shareable = ec->renderer->get_shareable_flag(view->surface);
			if((gs->buffer_ref.buffer != NULL) && (shareable!=0)) {
				vm_add_buf(ec, gr, gs, bc, gs->buffer_ref.buffer, view, buf_ind++);
			}
		}
	}

	top = &go->borders[GL_RENDERER_BORDER_TOP];
	bottom = &go->borders[GL_RENDERER_BORDER_BOTTOM];
	left = &go->borders[GL_RENDERER_BORDER_LEFT];
	right = &go->borders[GL_RENDERER_BORDER_RIGHT];

	/*
	 * Update the counter to be +1 each time before we flip and n_buffers to be
	 * the total number of buffers that we are providing to the host.
	 */
	vbt->h.output = output_num;
	vbt->h.counter++;
	vbt->h.disp_w = output->current_mode->width + left->width + right->width;
	vbt->h.disp_h = output->current_mode->height + top->height + bottom->height;
	vbt->h.n_buffers = wl_list_length(&vbt->vm_buffer_info_list);;

	/* No buffers */
	if(vbt->h.n_buffers == 0) {
		wait_for_gpu(&vbt->vm_buffer_info_list, drm_fd);
		return 1;
	}

	add_marker_to_vm_data(METADATA_STREAM_START);
	add_to_vm_data(&vbt->h, sizeof(struct vm_header));

	/* Write individual buffers */
	wl_list_for_each(gr_buf, &vbt->vm_buffer_info_list, elm) {
		hyper_dmabuf_id_t old_hyper_dmabuf;

		/* export bo */
		old_hyper_dmabuf = gr_buf->vm_buffer_info.hyper_dmabuf_id;
		export_bo(gr_buf->backend, gr_buf->bo, &vbt->h, &gr_buf->vm_buffer_info);

		/*
		 * If previous contents of buffer were exported using different
		 * hyper_dmabuf, unexport it know, as we will lost reference
		 * to that old id.
		 */
		if (old_hyper_dmabuf.id != 0 &&
		    old_hyper_dmabuf.id != gr_buf->vm_buffer_info.hyper_dmabuf_id.id) {
			unexport_bo(gr_buf->backend, &gr_buf->vm_buffer_info);
		}

		add_to_vm_data(&gr_buf->vm_buffer_info, sizeof(struct vm_buffer_info));
	}

	wait_for_gpu(&vbt->vm_buffer_info_list, drm_fd);
	return 1;
}

int vm_table_draw(struct weston_output *output, struct gl_output_state *go,
		struct gl_renderer *gr)
{
	struct weston_compositor *ec = output->compositor;
	struct ias_output *ias_output = (struct ias_output *) output;
	struct ias_backend *bc = ias_output->ias_crtc->backend;
	struct weston_view *view;
	struct gl_surface_state *gs;
	struct gr_buffer_ref *gr_buf;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	int num_textures;
	GLuint *textures;
	int i = 0;
	EGLint buffer_age = 0;
	EGLBoolean ret;
	int full_width, full_height;
	struct weston_matrix matrix;
	struct gl_border_image *top, *bottom, *left, *right;
	int shareable = 0;
	struct weston_output *o;
	int output_num = 0;
	int drm_fd = bc->drm.fd;
	int buf_ind = 0;

	static const GLfloat tile_verts[] = {
		0.0, 0.0,                           /**/  0.0, 0.0,
		sizeof(struct vm_header), 0.0, /**/  1.0, 0.0,
		0.0, 1.0,                           /**/  0.0, 1.0,
		sizeof(struct vm_header), 1.0, /**/  1.0, 1.0,
	};

	static const GLfloat tile_verts1[] = {
		0.0, 0.0,                           /**/  0.0, 0.0,
		sizeof(struct vm_buffer_info), 0.0, /**/  1.0, 0.0,
		0.0, 1.0,                           /**/  0.0, 1.0,
		sizeof(struct vm_buffer_info), 1.0, /**/  1.0, 1.0,
	};

	wl_list_for_each(o, &ec->output_list, link) {
		if (output == o)
			break;
		output_num++;
	}

	/*
	 * eglQuerySurface has to be called in order for the back
	 * pointer in the DRI to not be NULL, or else eglSwapBuffers
	 * will segfault when there are no client buffers.
	 */
	if (gr->has_egl_buffer_age) {
		ret = eglQuerySurface(gr->egl_display, go->egl_surface,
				      EGL_BUFFER_AGE_EXT, &buffer_age);
		if (ret == EGL_FALSE) {
			weston_log("buffer age query failed.\n");
		}
	}

	wl_list_for_each_reverse(view, &ec->view_list, link) {
		pixman_region32_t output_overlap;
		pixman_region32_init(&output_overlap);
		pixman_region32_intersect(&output_overlap, &view->transform.boundingbox, &output->region);
		if (!pixman_region32_not_empty(&output_overlap)) {
			continue;
		}

		if (ias_output->vm && view->plane == &ec->primary_plane) {
			gs = get_surface_state(view->surface);
			shareable = ec->renderer->get_shareable_flag(view->surface);
			if((gs->buffer_ref.buffer != NULL) && (shareable!=0)) {
				vm_add_buf(ec, gr, gs, bc, gs->buffer_ref.buffer, view, buf_ind++);
			}
		}
	}

	num_textures = wl_list_length(&vbt->vm_buffer_info_list);

	std_shader.vertex_source = vertex_shader;
	std_shader.fragment_source = solid_fragment_shader;
	use_shader(gr, &std_shader);
	pos_att = glGetAttribLocation(std_shader.program, "position");
	tex_att = glGetAttribLocation(std_shader.program, "texcoord");
	std_shader.proj_uniform = glGetUniformLocation(std_shader.program, "proj");
	tex_uniform = glGetUniformLocation(std_shader.program, "tex");
	bufpos_uniform = glGetUniformLocation(std_shader.program, "bufpos");

	top = &go->borders[GL_RENDERER_BORDER_TOP];
	bottom = &go->borders[GL_RENDERER_BORDER_BOTTOM];
	left = &go->borders[GL_RENDERER_BORDER_LEFT];
	right = &go->borders[GL_RENDERER_BORDER_RIGHT];

	full_width = output->current_mode->width + left->width + right->width;
	full_height = output->current_mode->height + top->height + bottom->height;

	glDisable(GL_BLEND);

	glViewport(0, 0, full_width, full_height);

	weston_matrix_init(&matrix);
	weston_matrix_translate(&matrix, -full_width/2.0, -full_height/2.0, 0);
	weston_matrix_scale(&matrix, 2.0/full_width, -2.0/full_height, 1);
	glUniformMatrix4fv(std_shader.proj_uniform, 1, GL_FALSE, matrix.d);

	/* Buffer format is vert_x, vert_y, tex_x, tex_y */
	glVertexAttribPointer(pos_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts[0]);
	glVertexAttribPointer(tex_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts[2]);
	glEnableVertexAttribArray(pos_att);
	glEnableVertexAttribArray(tex_att);

	/*
	 * Allocate num_textures + 1. The extra 1 is for the header info that
	 * also needs to be flipped.
	 */
	textures = zalloc(sizeof(GLuint) * (num_textures + 1));

	/*
	 * Update the counter to be +1 each time before we flip and n_buffers to be
	 * the total number of buffers that we are providing to the host.
	 */
	vbt->h.counter++;
	vbt->h.output = output_num;
	vbt->h.n_buffers = num_textures;
	vbt->h.disp_w = full_width;
	vbt->h.disp_h = full_height;

	/* number of textures should be the number of buffers + 1 for the buffer table header */
	num_textures++;

	glGenTextures(num_textures, textures);

	glActiveTexture(GL_TEXTURE0);

	/* Write the header */
	glBindTexture(GL_TEXTURE_2D, textures[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, sizeof(struct vm_header), 1, 0,
			GL_BGRA_EXT, GL_UNSIGNED_BYTE, &vbt->h);
	glUniform1i(bufpos_uniform, 0);
	glUniform1i(tex_uniform, 0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(pos_att);
	glDisableVertexAttribArray(tex_att);

	/* No buffers */
	if(num_textures == 1) {
		glDeleteTextures(1, textures);
		free(textures);
		// Still have to call to release all buffers
		wait_for_gpu(&vbt->vm_buffer_info_list, drm_fd);
		return 1;
	}

	/*
	 * We start writing individual buffers from position 1 because 0 was used
	 * already to write the header
	 */
	glVertexAttribPointer(pos_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts1[0]);
	glVertexAttribPointer(tex_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts1[2]);
	glEnableVertexAttribArray(pos_att);
	glEnableVertexAttribArray(tex_att);

	/* Write individual buffers */
	wl_list_for_each(gr_buf, &vbt->vm_buffer_info_list, elm) {
		glBindTexture(GL_TEXTURE_2D, textures[i+1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT,
				sizeof(struct vm_buffer_info), 1, 0,
				GL_BGRA_EXT, GL_UNSIGNED_BYTE, &gr_buf->vm_buffer_info);
		glUniform1i(bufpos_uniform,
				(sizeof(struct vm_header) + (i*sizeof(struct vm_buffer_info)))/4);
		glUniform1i(tex_uniform, 0);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		i++;
	}

	/* Make sure we delete the textures */
	glDeleteTextures(num_textures, textures);
	free(textures);
	wait_for_gpu(&vbt->vm_buffer_info_list, drm_fd);
	return 1;
}

void vm_output_init(struct weston_output *output)
{
	output->disable_planes++;
}

void export_bo(struct ias_backend *bk, struct gbm_bo *bo, struct vm_header *v_hdr, struct vm_buffer_info *vb)
{
#ifdef HYPER_DMABUF
	struct ioctl_hyper_dmabuf_export_remote msg;
	int ret;
	char *meta_ptr;

	if (!bo) {
		weston_log("bo is empty. Can't export bo\n");
		return;
	}

	msg.sz_priv = sizeof(*v_hdr) + sizeof(*vb);
	msg.priv = (char*)malloc(msg.sz_priv);

	if (!msg.priv) {
		weston_log("failed to allocate space for meta info\n");
		return;
	}

	/* TODO: add more flexibility here, instead of hardcoded domain 0*/
	msg.remote_domain = 0;
	msg.dmabuf_fd = gbm_bo_get_fd(bo);

	meta_ptr = msg.priv;

	memcpy(meta_ptr, v_hdr, sizeof(*v_hdr));
	meta_ptr += sizeof(*v_hdr);
	memcpy(meta_ptr, vb, sizeof(*vb));

	/* invalidating old hyper_dmabuf, it will be udpated by importer
	 * with newly generated one. */
	vb->hyper_dmabuf_id = (hyper_dmabuf_id_t){-1, {-1, -1, -1}};

        ret = ioctl(bk->hyper_dmabuf_fd, IOCTL_HYPER_DMABUF_EXPORT_REMOTE, &msg);

	close(msg.dmabuf_fd);
	free(msg.priv);

	if (ret) {
                weston_log("Exporting hyper_dmabuf failed with error %d\n", ret);
                return;
        }

	vb->hyper_dmabuf_id = msg.hid;

	if(gl_renderer_interface.vm_dbg) {
		weston_log("Exported hyperdmabuf = 0x%X\n", vb->hyper_dmabuf_id.id);
	}
#endif
}

int unexport_bo(struct ias_backend *bk, struct vm_buffer_info *vb)
{
#ifdef HYPER_DMABUF
	struct ioctl_hyper_dmabuf_unexport msg;
	int ret;

	msg.hid = vb->hyper_dmabuf_id;
	msg.delay_ms = gl_renderer_interface.vm_unexport_delay;
	ret = ioctl(bk->hyper_dmabuf_fd, IOCTL_HYPER_DMABUF_UNEXPORT, &msg);
	if (ret) {
		weston_log("%s: ioctl failed\n", __func__);
	}

	return ret;
#else
	return 0;
#endif
}

void pin_bo(struct gl_renderer *gr, void *buf, struct vm_buffer_info *vb)
{
	int ret;

	if(!gr->pin_bo) {
		return;
	}

	ret = gr->pin_bo(gr->egl_display, buf, &vb->ggtt_offset);
	if(!ret) {
		return;
	}

	if(gl_renderer_interface.vm_dbg) {
		weston_log("Pinned ggtt_offset = 0x%lX\n", vb->ggtt_offset);
	}
}


void unpin_bo(struct gl_renderer *gr, void *buf)
{
	int ret;

	if(!gr->unpin_bo) {
		return;
	}

	ret = gr->unpin_bo(gr->egl_display, buf);
	if(!ret) {
		return;
	}
}
