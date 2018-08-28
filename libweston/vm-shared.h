#ifndef __VM_SHARED_H__
#define __VM_SHARED_H__

#include <stdint.h>
#include "config.h"
#ifdef HYPER_DMABUF
#include <xen/hyper_dmabuf.h>
#else
typedef struct {
	int id;
	int rng_key[3];
} hyper_dmabuf_id_t;
#endif

#define SURFACE_NAME_LENGTH     64
#define BIT(a)                  (1<<a)
/*
 * If a buffer has been updated by a VM client app, the status field will have
 * this UPDATED bit set. Otherwise, it would have this bit cleared
 */
#define UPDATED                 BIT(0)
/*
 * Some fields may not have been filled by the compositor and stay unused.
 * Those would be marked as UNUSED_FIELD. For example, currently the
 * tile_format is not populated by the compositor and will be set to this.
 */
#define UNUSED_FIELD            (BIT(16) - 1)

struct vm_header {
	int32_t version;
	int32_t output;
	int32_t counter;
	int32_t n_buffers;
	int32_t disp_w;
	int32_t disp_h;
};

struct vm_buffer_info {
	int32_t surf_index;
	int32_t width, height;
	int32_t format;
	int32_t pitch[3];
	int32_t offset[3];
	int32_t tile_format;
	int32_t rotation;
	int32_t status;
	int32_t counter;
	hyper_dmabuf_id_t hyper_dmabuf_id;
	char surface_name[SURFACE_NAME_LENGTH];
	uint64_t surface_id;
	int32_t bbox[4];
};

/*
 * Metadata is being send as stream by compositor,
 * To be able to easily extract single frame from such stream,
 * special markers are used to mark beginning and end of single frame,
 * 0xF00D and 0xCAFE.
 */
#define METADATA_STREAM_START 0xF00D
#define METADATA_STREAM_END 0xCAFE

#define VM_MAX_OUTPUTS 12

/*
 * Depending on implementation of communication channel it may
 * be preallocating some memory space that will be used for communication.
 * In case when not enough memory will be allocated it may happen that metadata for
 * single frame won't fit in that memory. Due to performance reasons it's better
 * if whole metadata of single frame can be transmitted at once.
 * Below value is metadata size of frame containing ~80 surfaces, which can be used as hint
 * during initialization of communication channel
 */
#define METADATA_BUFFER_SIZE 12000

#endif
