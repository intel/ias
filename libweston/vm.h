#ifndef __VM_H__
#define __VM_H__

#ifndef USE_VM

#define VM_INIT(gr, res) res=0
#define VM_ADD_BUF(es, gr, gs, bc, buf, surf, idx)
#define VM_TABLE_DRAW(o, go, gr)
#define VM_TABLE_EXPOSE(o, go, gr)
#define VM_TABLE_CLEAN(o, gr)
#define VM_OUTPUT_INIT(o)
#define VM_DESTROY(gr)

#else // USE_VM

#include "vm-shared.h"
#include "gl-renderer.h"
#include "ias-backend.h"
#include "vm_comm.h"

#define VM_INIT(gr, res)            if(gl_renderer_interface.vm_exec) \
										{ res = vm_init(gr); } else { res = 0; }
#define VM_ADD_BUF(es, gr, gs, bc, buf, surf, idx) if(gl_renderer_interface.vm_exec) \
										{ vm_add_buf(es, gr, gs, bc, buf, surf, idx); }
#define VM_TABLE_EXPOSE(o, go, gr)    if(gl_renderer_interface.vm_exec) \
										{ struct ias_output *io = (struct ias_output *) o; \
										  if(io->vm) \
											{ if(vm_table_expose(o, go, gr)) \
												if (gl_renderer_interface.vm_share_only) \
													{ return; } \
											} \
										}

#define VM_TABLE_CLEAN(o, gr)       if(gl_renderer_interface.vm_exec) \
										{ struct ias_output *io = (struct ias_output *) o; \
										  if(io->vm) \
											{ vm_table_clean(gr); } \
										}
#define VM_OUTPUT_INIT(o)           if(gl_renderer_interface.vm_exec) \
										{ struct ias_output *io = (struct ias_output *) o; \
										  if(io->vm) \
											{ vm_output_init(o); } \
										}
#define VM_DESTROY(gr)              if(gl_renderer_interface.vm_exec) \
										{ vm_destroy(gr); }

#define VM_MAX_BUFFERS_NUMBER 4

struct vm_buffer_table {
	struct vm_header h;
	struct wl_list vm_buffer_info_list;
};

struct gr_buffer_ref {
	struct vm_buffer_info vm_buffer_info;
	struct weston_buffer *buffer;
	struct ias_backend *backend;
	struct gl_renderer *gr;
	struct wl_listener buffer_destroy_listener;
	struct wl_list elm;
	int cleanup_required;
	struct weston_surface* surface;
	struct gbm_bo *bo;
};

int vm_init(struct gl_renderer *gr);
void vm_add_buf(struct weston_compositor *ec, struct gl_renderer *gr,
		struct gl_surface_state *gs, struct ias_backend *bc, struct weston_buffer *buffer,
		struct weston_view *view, int buf_ind);
int vm_table_expose(struct weston_output *output, struct gl_output_state *go,
		struct gl_renderer *gr);
int vm_table_draw(struct weston_output *output, struct gl_output_state *go,
		struct gl_renderer *gr);
void vm_table_clean(struct gl_renderer *gr);
void vm_output_init(struct weston_output *output);
void pin_bo(struct gl_renderer *gr, void *buf, struct vm_buffer_info *vb);
void export_bo(struct ias_backend *bk, struct gbm_bo *bo, struct vm_header *v_hdr, struct vm_buffer_info *vb);
int unexport_bo(struct ias_backend *bk, struct vm_buffer_info *vb);
void unpin_bo(struct gl_renderer *gr, void *buf);
void vm_destroy(struct gl_renderer *gr);

#endif // USE_VM
#endif // __VM_H__
