#include "config.h"
#include "vm.h"
#include "linux-dmabuf.h"
#include <sched.h>
#ifdef HYPER_DMABUF
#include <hyper_dmabuf.h>
#endif

char vm_data[METADATA_BUFFER_SIZE];
int vm_data_offset;

#define METADATA_SEND_RETRIES 10
#define METADATA_SEND_SLEEP 1000

#define VBT_VERSION 3

static struct hyper_communication_interface comm_interface;
static void *comm_module;

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

	return 0;
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

	/* remove any buffer refs inside this table */
	wl_list_for_each_safe(gr_buf, tmp, &vbt->vm_buffer_info_list, elm) {
		/* don't unpin buffer, only remove it from the list */
		wl_list_remove(&gr_buf->elm);
		gr_buf->cleanup_required = 0;
	}
}

static void buffer_destroy(struct gr_buffer_ref *gr_buf)
{
	unexport_bo(gr_buf->backend, &(gr_buf->vm_buffer_info));
	if (gr_buf->elm.next || gr_buf->elm.prev) {
		wl_list_remove(&gr_buf->elm);
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

	if (comm_interface.cleanup != NULL) {
		comm_interface.cleanup();
	}
	if (comm_module != NULL) {
		dlclose(comm_module);
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
		weston_log("hyper_dmabuf id = %X\n", gr_buf->vm_buffer_info.hyper_dmabuf_id.id);
		weston_log("counter = %d\n", gr_buf->vm_buffer_info.counter);
		weston_log("%s\n\n", gr_buf->vm_buffer_info.status & UPDATED ? "Updated" : "Not Updated");
	}
	weston_log("-------------------------\n");
}

static void get_buffer_details(struct ias_backend *bc,
			       struct gl_renderer *gr,
			       struct weston_buffer *buffer,
			       struct gr_buffer_ref *buffer_ref_ptr)
{
	struct linux_dmabuf_buffer *dmabuf = NULL;

	if ((dmabuf = linux_dmabuf_buffer_get(buffer->resource))) {
		buffer_ref_ptr->vm_buffer_info.format = dmabuf->attributes.format;
		buffer_ref_ptr->vm_buffer_info.offset[0] = dmabuf->attributes.offset[0];
		buffer_ref_ptr->vm_buffer_info.offset[1] = dmabuf->attributes.offset[1];
		buffer_ref_ptr->vm_buffer_info.offset[2] = dmabuf->attributes.offset[2];
		buffer_ref_ptr->vm_buffer_info.pitch[0] = dmabuf->attributes.stride[0];
		buffer_ref_ptr->vm_buffer_info.pitch[1] = dmabuf->attributes.stride[1];
		buffer_ref_ptr->vm_buffer_info.pitch[2] = dmabuf->attributes.stride[2];

		switch(dmabuf->attributes.modifier[0])
		{
			case I915_FORMAT_MOD_X_TILED:
			buffer_ref_ptr->vm_buffer_info.tile_format = 1;
			break;
			case I915_FORMAT_MOD_Y_TILED:
			buffer_ref_ptr->vm_buffer_info.tile_format = 2;
			break;
			case I915_FORMAT_MOD_Y_TILED_CCS:
			buffer_ref_ptr->vm_buffer_info.tile_format = 4;
			break;
			default:
			buffer_ref_ptr->vm_buffer_info.tile_format = 0;
		}
	}
}

void vm_add_buf(struct weston_compositor *ec, struct gl_renderer *gr,
		struct gl_surface_state *gs, struct ias_backend *bc, struct weston_buffer *buffer,
		struct weston_view *view, int buf_ind)
{
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	struct gr_buffer_ref *gr_buffer_ref_ptr;
	struct gbm_bo *bo = NULL;
	struct linux_dmabuf_buffer *dmabuf = NULL;
	struct weston_surface *surface = view->surface;

	dmabuf = linux_dmabuf_buffer_get(buffer->resource);
	if (!dmabuf)
		return;

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

	get_buffer_details(bc, gr, buffer, gr_buffer_ref_ptr);

	gr_buffer_ref_ptr->vm_buffer_info.status |= UPDATED;

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
	struct gbm_import_fd_data gbm_dmabuf = {
		.fd = dmabuf->attributes.fd[0],
		.width = dmabuf->attributes.width,
		.height = dmabuf->attributes.height,
		.stride = dmabuf->attributes.stride[0],
		.format = dmabuf->attributes.format
	};

	bo = gbm_bo_import(bc->gbm, GBM_BO_IMPORT_FD,
			&gbm_dmabuf, GBM_BO_USE_SCANOUT);

	gr_buffer_ref_ptr->bo = bo;
	if (!bo) {
		return;
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
