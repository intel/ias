/*
 * Copyright © 2018 Intel Corporation
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <dlfcn.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>

#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <va/va_enc_h264.h>
#include <libdrm/intel_bufmgr.h>
#include <wayland-client.h>

//#include "compositor.h"
#include "encoder.h"
#include "ias-shell-client-protocol.h"
#include "../../shared/timespec-util.h"
#include "../../shared/zalloc.h"

#define NAL_REF_IDC_NONE        0
#define NAL_REF_IDC_LOW         1
#define NAL_REF_IDC_MEDIUM      2
#define NAL_REF_IDC_HIGH        3

#define NAL_NON_IDR             1
#define NAL_IDR                 5
#define NAL_SPS                 7
#define NAL_PPS                 8
#define NAL_SEI                 6

#define SLICE_TYPE_P            0
#define SLICE_TYPE_B            1
#define SLICE_TYPE_I            2

#define ENTROPY_MODE_CAVLC      0
#define ENTROPY_MODE_CABAC      1

#define PROFILE_IDC_BASELINE    66
#define PROFILE_IDC_MAIN        77
#define PROFILE_IDC_HIGH        100

#define MAX_FRAMES              5
#define BUFFER_STATUS_FREE      0
#define BUFFER_STATUS_IN_USE    1

#define DRM_BUF_MGR_SIZE        4096

/* Buffer types used in encoder */
typedef enum {
	EncoderBufferSequence,
	EncoderBufferPicture,
	EncoderBufferSlice,
	EncoderBufferHRD,
	EncoderBufferQualityLevel,
	EncoderBufferSPSHeader,
	EncoderBufferSPSData,
	EncoderBufferPPSHeader,
	EncoderBufferPPSData,
	num_encoder_buffers
} EncoderBufferType;

/* Arbitrary value for size of window used for moving window average. */
#define MV_AV_WIN_SIZE         5
#define US_IN_SEC              1000000
#define DEFAULT_FPS            60

struct rd_encoder {
	int drm_fd;
	int width, height;
	int verbose;
	uint32_t surfid;
	struct ias_hmi *hmi;
	struct wl_display *display;
	uint32_t output_number;
	struct {
		int x;
		int y;
		int w;
		int h;
	} region;
	int profile_level;
	int encoder_tu;

	uint16_t frame_count;
	int num_vsyncs;
	int first_frame;

	int error;
	int destroying_transport;
	int destroying_encoder;

	/* Encoder thread */
	pthread_t encoder_thread;
	pthread_mutex_t encoder_mutex;
	pthread_cond_t encoder_cond;

	struct {
		int valid;
		int prime_fd;
		int stride;
		int frame_number;
		int32_t va_buffer_handle;
		enum rd_encoder_format format;
		uint32_t timestamp;
		uint32_t shm_surf_id;
		uint32_t buf_id;
		uint32_t image_id;
	} current_encode, next_encode;

	/* Transportation thread */
	pthread_t transport_thread;
	pthread_mutex_t transport_mutex;
	pthread_cond_t transport_cond;

	struct {
		int valid;
		int frame_number;
		int32_t handle;
		int32_t stream_size;
		uint32_t timestamp;
		VABufferID output_buf;
	} current_transport, next_transport;

	VADisplay va_dpy;

	/* Video post processing is used for colorspace conversion */
	struct {
		VAConfigID cfg;
		VAContextID ctx;
		VABufferID pipeline_buf;
		VASurfaceID output;
	} vpp;

	struct {
		VAConfigID cfg;
		VAContextID ctx;
		VASurfaceID reference_picture[3];

		int intra_period;
		int output_size;
		int constraint_set_flag;

		struct {
			VABufferID buffers[num_encoder_buffers];
			int seq_changed;
			uint32_t last_timestamp;
			unsigned int time_scale;
			unsigned int num_units_in_tick;
			uint32_t delta_t[MV_AV_WIN_SIZE];
			uint32_t delta_t_total;
			int circ_buffer_head;
		} param;
	} encoder;

	struct {
		VABufferID bufferID;
		int bufferStatus;
	} out_buf[MAX_FRAMES];

	/* Transport plugin */
	void *transport_handle;
	void *transport_private_data;
	int (*transport_send_fptr)(void *transport_private_data,
			drm_intel_bo *drm_bo,
			int32_t stream_size,
			uint32_t timestamp);

	drm_intel_bufmgr *drm_bufmgr;
};

static void *
encoder_thread_function(void * const data);

static void *
transport_thread_function(void * const data);

/* bitstream code used for writing the packed headers */

#define BITSTREAM_ALLOCATE_STEPPING	 4096

struct bitstream {
	unsigned int *buffer;
	int bit_offset;
	int max_size_in_dword;
};

static unsigned int
va_swap32(unsigned int val)
{
	unsigned char *pval = (unsigned char *)&val;

	return ((pval[0] << 24) |
		(pval[1] << 16) |
		(pval[2] << 8)  |
		(pval[3] << 0));
}

static void
bitstream_start(struct bitstream * const bs)
{
	bs->max_size_in_dword = BITSTREAM_ALLOCATE_STEPPING;
	bs->buffer = calloc(bs->max_size_in_dword * sizeof(unsigned int), 1);
	bs->bit_offset = 0;
}

static void
bitstream_end(struct bitstream * const bs)
{
	int pos = (bs->bit_offset >> 5);
	int bit_offset = (bs->bit_offset & 0x1f);
	int bit_left = 32 - bit_offset;

	if (!bs->buffer) {
		fprintf(stderr, "ERROR - No valid bitstream buffer.\n");
		return;
	}

	if (bit_offset) {
		bs->buffer[pos] = va_swap32((bs->buffer[pos] << bit_left));
	}
}

static void
bitstream_put_ui(struct bitstream * const bs, const unsigned int val,
			int size_in_bits)
{
	int pos = (bs->bit_offset >> 5);
	int bit_offset = (bs->bit_offset & 0x1f);
	int bit_left = 32 - bit_offset;

	if (!size_in_bits)
		return;

	if (!bs->buffer) {
		fprintf(stderr, "ERROR - No valid bitstream buffer.\n");
		return;
	}

	bs->bit_offset += size_in_bits;

	if (bit_left > size_in_bits) {
		bs->buffer[pos] = (bs->buffer[pos] << size_in_bits | val);
		return;
	}

	size_in_bits -= bit_left;
	bs->buffer[pos] =
		(bs->buffer[pos] << bit_left) | (val >> size_in_bits);
	bs->buffer[pos] = va_swap32(bs->buffer[pos]);

	if (pos + 1 == bs->max_size_in_dword) {
		unsigned int *temp_buffer;
		bs->max_size_in_dword += BITSTREAM_ALLOCATE_STEPPING;
		/* Assign realloc pointer to a temp_buffer so that if realloc
		 * fails then we can use the original bs->buffer to handle the
		 * error, otherwise it would be lost. */
		temp_buffer =
			realloc(bs->buffer,
				bs->max_size_in_dword * sizeof(unsigned int));
		if (!temp_buffer) {
			/* Handle error by discarding the buffer. Another option
			 * might be to just drop the value that we're trying to add
			 * to the buffer. */
			free(bs->buffer);
			bs->buffer = NULL;
			bs->max_size_in_dword = 0;
			bs->bit_offset = 0;
			return;
		} else {
			bs->buffer = temp_buffer;
		}
	}

	bs->buffer[pos + 1] = val;
}

static void
bitstream_put_ue(struct bitstream * const bs, unsigned int val)
{
	int size_in_bits = 0;
	int tmp_val = ++val;

	while (tmp_val) {
		tmp_val >>= 1;
		size_in_bits++;
	}

	bitstream_put_ui(bs, 0, size_in_bits - 1); /* leading zero */
	bitstream_put_ui(bs, val, size_in_bits);
}

static void
bitstream_put_se(struct bitstream * const bs, const int val)
{
	unsigned int new_val;

	if (val <= 0)
		new_val = -2 * val;
	else
		new_val = 2 * val - 1;

	bitstream_put_ue(bs, new_val);
}

static void
bitstream_byte_aligning(struct bitstream * const bs, const int bit)
{
	int bit_offset = (bs->bit_offset & 0x7);
	int bit_left = 8 - bit_offset;
	int new_val;

	if (!bit_offset)
		return;

	if (bit)
		new_val = (1 << bit_left) - 1;
	else
		new_val = 0;

	bitstream_put_ui(bs, new_val, bit_left);
}

static VAStatus
encoder_create_config(struct rd_encoder * const encoder)
{
	VAConfigAttrib attrib[2];
	VAStatus status;
	VASurfaceID encode_surfaces[4];

	VAEntrypoint *entrypoints =
		malloc(sizeof(VAEntrypoint) * vaMaxNumEntrypoints(encoder->va_dpy));
	int num_supported = 0;
	int i;
	bool lp_support = false;

	if (entrypoints == NULL) {
		fprintf(stderr, "encoder_create_config : failed to allocate entrypoints.\n");
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}

	vaQueryConfigEntrypoints(encoder->va_dpy,
				VAProfileH264ConstrainedBaseline,
				entrypoints,
				&num_supported);
	for (i = 0; i < num_supported; i++) {
		if (entrypoints[i] == VAEntrypointEncSliceLP) {
			lp_support = true;
			break;
		}
	}

	free(entrypoints);

	if (lp_support == false) {
		/* No VAEntrypointEncSliceLP support */
		return VA_STATUS_ERROR_INVALID_CONFIG;
	}

	encode_surfaces[0] = encoder->vpp.output;
	encode_surfaces[1] = encoder->encoder.reference_picture[0];
	encode_surfaces[2] = encoder->encoder.reference_picture[1];
	encode_surfaces[3] = encoder->encoder.reference_picture[2];

	/* FIXME: should check if specified attributes are supported */

	attrib[0].type = VAConfigAttribRTFormat;
	attrib[0].value = VA_RT_FORMAT_YUV420;

	attrib[1].type = VAConfigAttribRateControl;
	attrib[1].value = VA_RC_CQP;

	status = vaCreateConfig(encoder->va_dpy, VAProfileH264ConstrainedBaseline,
				VAEntrypointEncSliceLP, attrib, 2,
				&encoder->encoder.cfg);
	if (status != VA_STATUS_SUCCESS)
		return status;

	/* For encoding, width and height should be aligned to 16. */
	status = vaCreateContext(encoder->va_dpy, encoder->encoder.cfg,
				 (encoder->region.w + 0xF) & ~0xF, (encoder->region.h + 0xF) & ~0xF,
				 VA_PROGRESSIVE, &encode_surfaces[0], 4,
				 &encoder->encoder.ctx);
	if (status != VA_STATUS_SUCCESS) {
		vaDestroyConfig(encoder->va_dpy, encoder->encoder.cfg);
		return status;
	}

	return VA_STATUS_SUCCESS;
}

static void
encoder_destroy_config(struct rd_encoder * const encoder)
{
	vaDestroyContext(encoder->va_dpy, encoder->encoder.ctx);
	vaDestroyConfig(encoder->va_dpy, encoder->encoder.cfg);
}

static void
encoder_init_seq_parameters(struct rd_encoder * const encoder)
{
	int width_in_mbs, height_in_mbs;
	int frame_cropping_flag = 0;
	int frame_crop_bottom_offset = 0;
	VAStatus status;
	VABufferID seq_buf;
	VAEncSequenceParameterBufferH264 *seq_param;
	int i;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_init_seq_parameters : No encoder.\n");
		return;
	}

	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncSequenceParameterBufferType,
				sizeof(VAEncSequenceParameterBufferH264),
				1, NULL, &seq_buf);

	if (status == VA_STATUS_SUCCESS) {
		encoder->encoder.param.buffers[EncoderBufferSequence] = seq_buf;
	} else {
		printf("ERROR - failed to create encoder parameter sequence buffer.\n");
		return;
	}

	/* Some values in the sequence parameter buffer structure stay
	 * constant between frames. */
	status = vaMapBuffer(encoder->va_dpy, seq_buf, (void **) &seq_param);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to map sequence parameter buffer %d for init.\n",
				seq_buf);
		return;
	}

	width_in_mbs = (encoder->region.w + 15) / 16;
	height_in_mbs = (encoder->region.h + 15) / 16;

	seq_param->level_idc = 51;
	seq_param->intra_period = encoder->encoder.intra_period;
	seq_param->ip_period = 1;
	seq_param->max_num_ref_frames = 1;
	seq_param->picture_width_in_mbs = width_in_mbs;
	seq_param->picture_height_in_mbs = height_in_mbs;
	seq_param->seq_fields.bits.frame_mbs_only_flag = 1;

	/* H264 assumes two fields per frame. */
	seq_param->time_scale = 1800;
	seq_param->num_units_in_tick = seq_param->time_scale / (2 * DEFAULT_FPS);
	for (i = 0; i < MV_AV_WIN_SIZE; i++) {
		encoder->encoder.param.delta_t[i] = US_IN_SEC / DEFAULT_FPS;
	}
	encoder->encoder.param.delta_t_total = MV_AV_WIN_SIZE * encoder->encoder.param.delta_t[0];

	if (height_in_mbs * 16 - encoder->height > 0) {
		frame_cropping_flag = 1;
		frame_crop_bottom_offset = (height_in_mbs * 16 - encoder->height) / 2;
	}

	seq_param->frame_cropping_flag = frame_cropping_flag;
	seq_param->frame_crop_bottom_offset = frame_crop_bottom_offset;

	seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = 2;

	encoder->encoder.param.seq_changed = 1;
	encoder->encoder.param.time_scale = seq_param->time_scale;
	encoder->encoder.param.num_units_in_tick = seq_param->num_units_in_tick;

	vaUnmapBuffer(encoder->va_dpy, seq_buf);
}

static VABufferID
encoder_update_seq_parameters(struct rd_encoder * const encoder)
{
	unsigned int num_units_in_tick = 0;
	uint32_t delta_t;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_update_seq_parameters : No encoder.\n");
		return VA_INVALID_ID;
	}

		/* Workaround for mfx gstreamer not handling change in SPS. */
		return encoder->encoder.param.buffers[EncoderBufferSequence];

	/* We will need to update the sequence parameters if the frame
	 * rate does not match. */
	 if (encoder->frame_count == 0) {
		encoder->encoder.param.last_timestamp = encoder->current_encode.timestamp;
		return encoder->encoder.param.buffers[EncoderBufferSequence];
	 }

	/* We must handle the case where the timestamp has wrapped around. We
	 * ignore the possibility of a long enough gap between frames for the
	 * timestamp to have wrapped twice. */
	if (encoder->current_encode.timestamp > encoder->encoder.param.last_timestamp) {
		delta_t = encoder->current_encode.timestamp - encoder->encoder.param.last_timestamp;
	} else {
		delta_t = ~0;
		delta_t -= encoder->encoder.param.last_timestamp;
		delta_t += 1 + encoder->current_encode.timestamp;
	}

	/* Timestamps are in units of 1/90000 of a second. Convert to us. */
	/* Use 100/9 to avoid overflow or rounding errors with
	 * US_IN_SEC / 90000 or (US_IN_SEC / 90000). */
	delta_t = delta_t * 100 / 9;

	/* Using a moving window average. Update the total here and save
	 * the new value. */
	encoder->encoder.param.delta_t_total -=
			encoder->encoder.param.delta_t[encoder->encoder.param.circ_buffer_head];
	encoder->encoder.param.delta_t_total += delta_t;
	encoder->encoder.param.delta_t[encoder->encoder.param.circ_buffer_head] = delta_t;
	encoder->encoder.param.circ_buffer_head =
			(encoder->encoder.param.circ_buffer_head + 1) % MV_AV_WIN_SIZE;
	delta_t = encoder->encoder.param.delta_t_total / MV_AV_WIN_SIZE;

	/* H264 assumes two fields per frame. Round value. */
	num_units_in_tick = (encoder->encoder.param.time_scale * delta_t + US_IN_SEC);
	num_units_in_tick /= (2 * US_IN_SEC);

	if (encoder->encoder.param.num_units_in_tick != num_units_in_tick) {
		VAStatus status;
		VAEncSequenceParameterBufferH264 *seq_param;
		VABufferID seq_buf = encoder->encoder.param.buffers[EncoderBufferSequence];

		status = vaMapBuffer(encoder->va_dpy, seq_buf, (void **) &seq_param);
		if (status != VA_STATUS_SUCCESS) {
			printf("WARNING - failed to map sequence parameter buffer %d for update.\n",
					seq_buf);
		} else {
			seq_param->num_units_in_tick = num_units_in_tick;
			vaUnmapBuffer(encoder->va_dpy, seq_buf);
		}
		encoder->encoder.param.num_units_in_tick = num_units_in_tick;

		encoder->encoder.param.seq_changed = 1;
	}

	encoder->encoder.param.last_timestamp = encoder->current_encode.timestamp;

	return encoder->encoder.param.buffers[EncoderBufferSequence];
}


static void
encoder_init_pic_parameters(struct rd_encoder * const encoder)
{
	VAStatus status;
	VABufferID pic_param_buf;
	VAEncPictureParameterBufferH264 *pic_param;
	int i;
	/* The number of reference frames in a VAEncSequenceParameterBufferH264
	 * really ought to be defined as a constant in va_enc_h264.h but it is
	 * not. */
	const int num_ref_frames = 16;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_init_pic_parameters : No encoder.\n");
		return;
	}

	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncPictureParameterBufferType,
				sizeof(VAEncPictureParameterBufferH264),
				1, NULL, &pic_param_buf);

	if (status == VA_STATUS_SUCCESS) {
		encoder->encoder.param.buffers[EncoderBufferPicture] = pic_param_buf;
	} else {
		printf("ERROR - failed to create encoder picture parameter buffer.\n");
		return;
	}

	/* Some values in the picture parameter buffer structure stay
	 * constant between frames. */
	status = vaMapBuffer(encoder->va_dpy, pic_param_buf, (void **) &pic_param);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to map picture parameter buffer %d for init.\n",
				pic_param_buf);
		return;
	}

	pic_param->pic_init_qp = 0;

	/* Entropy mode is either CAVLC (0) or CABAC */
	pic_param->pic_fields.bits.entropy_coding_mode_flag = 1;

	pic_param->pic_fields.bits.deblocking_filter_control_present_flag = 1;

	pic_param->pic_fields.bits.idr_pic_flag = 1; /* Always true on init */
	pic_param->pic_fields.bits.reference_pic_flag = 1;

	for (i = 1; i < num_ref_frames; i++) {
		pic_param->ReferenceFrames[i].picture_id = VA_INVALID_ID;
		pic_param->ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
	}

	vaUnmapBuffer(encoder->va_dpy, pic_param_buf);
}

static VABufferID
encoder_update_pic_parameters(struct rd_encoder * const encoder,
				const VABufferID output_buf, const int slice_type)
{
	VAEncPictureParameterBufferH264 *pic_param;
	VAStatus status;
	VABufferID buffer = VA_INVALID_ID;;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_init_pic_parameters : No encoder.\n");
		return VA_INVALID_ID;
	}

	buffer = encoder->encoder.param.buffers[EncoderBufferPicture];
	status = vaMapBuffer(encoder->va_dpy, buffer, (void **) &pic_param);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to map picture parameter buffer %d.\n",
				buffer);
		return VA_INVALID_ID;
	}

	pic_param->CurrPic.picture_id = encoder->encoder.reference_picture[encoder->frame_count % 2];
	pic_param->CurrPic.flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
	pic_param->CurrPic.TopFieldOrderCnt = encoder->frame_count * 2;
	pic_param->CurrPic.BottomFieldOrderCnt = encoder->frame_count * 2 + 1;
	if (slice_type == SLICE_TYPE_I) {
		pic_param->ReferenceFrames[0].picture_id = VA_INVALID_ID;
		pic_param->ReferenceFrames[0].flags = VA_PICTURE_H264_INVALID;
	} else {
		pic_param->ReferenceFrames[0].picture_id =
				encoder->encoder.reference_picture[(encoder->frame_count + 1) % 2];
		pic_param->ReferenceFrames[0].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
	}

	pic_param->coded_buf = output_buf;
	pic_param->frame_num = encoder->frame_count;

	pic_param->pic_fields.bits.idr_pic_flag =
		((encoder->frame_count % encoder->encoder.intra_period) == 0);

	vaUnmapBuffer(encoder->va_dpy, buffer);

	return buffer;
}

static void
encoder_init_slice_parameter(struct rd_encoder * const encoder)
{
	VABufferID slice_param_buf;
	VAStatus status;
	VAEncSliceParameterBufferH264 *slice;
	int width_in_mbs;
	int height_in_mbs;
	int j;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_init_slice_parameter : No encoder.\n");
		return;
	}

	width_in_mbs = (encoder->region.w + 15) / 16;
	height_in_mbs = (encoder->region.h + 15) / 16;

	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncSliceParameterBufferType,
				sizeof(VAEncSliceParameterBufferH264), 1,
				NULL, &slice_param_buf);
	if (status == VA_STATUS_SUCCESS) {
		encoder->encoder.param.buffers[EncoderBufferSlice] = slice_param_buf;
	} else {
		printf("ERROR - failed to create encoder slice parameter buffer.\n");
		return;
	}

	status = vaMapBuffer(encoder->va_dpy, slice_param_buf, (void **) &slice);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to map slice parameter buffer %d for init.\n",
				slice_param_buf);
		return;
	}

	memset(slice, 0, sizeof(VAEncSliceParameterBufferH264));
	/* Most values in the slice parameter buffer structure stay
	 * constant between frames. */
	slice->macroblock_address = 0;
	slice->num_macroblocks = height_in_mbs * width_in_mbs;
	slice->pic_parameter_set_id = 0;
	slice->direct_spatial_mv_pred_flag = 0;
	slice->num_ref_idx_l0_active_minus1 = 0;
	slice->num_ref_idx_l1_active_minus1 = 0;
	slice->cabac_init_idc = 0;
	slice->slice_qp_delta = 0;
	slice->disable_deblocking_filter_idc = 0;
	slice->slice_alpha_c0_offset_div2 = 2;
	slice->slice_beta_offset_div2 = 2;
	slice->idr_pic_id = 0;

	for (j = 1; j < 32; j++) {
		slice->RefPicList0[j].picture_id = VA_INVALID_ID;
		slice->RefPicList0[j].flags = VA_PICTURE_H264_INVALID;
	}
	for (j = 0; j < 32; j++) {
		slice->RefPicList1[j].picture_id = VA_INVALID_ID;
		slice->RefPicList1[j].flags = VA_PICTURE_H264_INVALID;
	}

	vaUnmapBuffer(encoder->va_dpy, slice_param_buf);
}

static VABufferID
encoder_update_slice_parameter(struct rd_encoder * const encoder,
		const int slice_type)
{
	VAStatus status;
	VAEncSliceParameterBufferH264 *slice;
	VABufferID slice_param_buf = VA_INVALID_ID;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_update_slice_parameter : No encoder.\n");
		return VA_INVALID_ID;
	}

	slice_param_buf = encoder->encoder.param.buffers[EncoderBufferSlice];
	status = vaMapBuffer(encoder->va_dpy, slice_param_buf, (void **) &slice);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to map slice parameter buffer %d for update.\n",
				slice_param_buf);
		return VA_INVALID_ID;
	}

	slice->slice_type = slice_type;
	slice->pic_order_cnt_lsb = encoder->frame_count * 2;

	if (slice_type == SLICE_TYPE_I) {
		slice->RefPicList0[0].picture_id = VA_INVALID_ID;
		slice->RefPicList0[0].flags = VA_PICTURE_H264_INVALID;
	} else {
		slice->RefPicList0[0].picture_id =
				encoder->encoder.reference_picture[(encoder->frame_count + 1) % 2];
		slice->RefPicList0[0].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
	}

	vaUnmapBuffer(encoder->va_dpy, slice_param_buf);
	return slice_param_buf;
}

static void
encoder_init_misc_parameters(struct rd_encoder * const encoder)
{
	VABufferID buffer = VA_INVALID_ID;
	VAStatus status = VA_STATUS_SUCCESS;
	VAEncMiscParameterBuffer *quality_param_buf;
	VAEncMiscParameterBufferQualityLevel *quality_param;



	if (encoder == NULL) {
		fprintf(stderr, "%s : No encoder.\n", __func__);
		return;
	}

	int total_size =
		sizeof(VAEncMiscParameterBuffer) +
		sizeof(VAEncMiscParameterHRD);

	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncMiscParameterBufferType, total_size,
				1, NULL, &buffer);
	if (status == VA_STATUS_SUCCESS) {
		encoder->encoder.param.buffers[EncoderBufferHRD] = buffer;
	} else {
		printf("ERROR - failed to create encoder HRD parameter buffer.\n");
	}
	buffer = VA_INVALID_ID;

	total_size =
		sizeof(VAEncMiscParameterBuffer) +
		sizeof(VAEncMiscParameterBufferQualityLevel);
	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
			VAEncMiscParameterBufferType, total_size,
			1, NULL, &buffer);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to create encoder quality level parameter buffer.\n");
	} else {
		encoder->encoder.param.buffers[EncoderBufferQualityLevel] = buffer;
		status = vaMapBuffer(encoder->va_dpy, buffer,(void **)&quality_param_buf);
		if (status != VA_STATUS_SUCCESS) {
			printf("ERROR - failed to map quality level parameter buffer %d for update.\n",
					buffer);
		} else {
			quality_param_buf->type = VAEncMiscParameterTypeQualityLevel;
			quality_param = (VAEncMiscParameterBufferQualityLevel *)quality_param_buf->data;
			quality_param->quality_level = encoder->encoder_tu;
			vaUnmapBuffer(encoder->va_dpy, buffer);
		}
	}
}

/* Can probably remove this, since values do not change. */
static VABufferID
encoder_update_HRD_parameters(const struct rd_encoder * const encoder)
{
	VAEncMiscParameterBuffer *misc_param;
	VAEncMiscParameterHRD *hrd;
	VABufferID buffer = VA_INVALID_ID;
	VAStatus status;

	if (encoder == NULL) {
		fprintf(stderr, "%s : No encoder.\n", __func__);
		return VA_INVALID_ID;
	}

	buffer = encoder->encoder.param.buffers[EncoderBufferHRD];
	status = vaMapBuffer(encoder->va_dpy, buffer, (void **) &misc_param);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - failed to map HRD parameter buffer %d for update.\n",
				buffer);
		return VA_INVALID_ID;
	}

	misc_param->type = VAEncMiscParameterTypeHRD;
	hrd = (VAEncMiscParameterHRD *) misc_param->data;

	hrd->initial_buffer_fullness = 0;
	hrd->buffer_size = 0;

	vaUnmapBuffer(encoder->va_dpy, buffer);

	return buffer;
}

static int
setup_encoder(struct rd_encoder * const encoder)
{
	VAStatus status;
	int i;

	if (encoder == NULL) {
		fprintf(stderr, "setup_encoder : No encoder.\n");
		return -1;
	}

	status = vaCreateSurfaces(encoder->va_dpy, VA_RT_FORMAT_YUV420,
				  encoder->region.w, encoder->region.h,
				  encoder->encoder.reference_picture, 3,
				  NULL, 0);
	if (status != VA_STATUS_SUCCESS) {
		return -1;
	}

	status = encoder_create_config(encoder);
	if (status != VA_STATUS_SUCCESS) {
		return -1;
	}

	/* VAProfileH264Main */
	encoder->encoder.constraint_set_flag |= (1 << 1); /* Annex A.2.2 */

	encoder->encoder.output_size = encoder->region.w * encoder->region.h;

	encoder->encoder.intra_period = 1;

	for (i = 0; i < num_encoder_buffers; i++) {
		encoder->encoder.param.buffers[i] = VA_INVALID_ID;
	}

	encoder_init_seq_parameters(encoder);
	encoder_init_pic_parameters(encoder);
	encoder_init_slice_parameter(encoder);
	encoder_init_misc_parameters(encoder);
	return 0;
}

static void
encoder_destroy_encode_session(struct rd_encoder * const encoder)
{
	int i;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_destroy_encode_session : No encoder.\n");
		return;
	}

	for (i = 0; i < num_encoder_buffers; i++) {
		if (encoder->encoder.param.buffers[i] != VA_INVALID_ID) {
			vaDestroyBuffer(encoder->va_dpy, encoder->encoder.param.buffers[i]);
			encoder->encoder.param.buffers[i] = VA_INVALID_ID;
		}
	}

	vaDestroySurfaces(encoder->va_dpy, encoder->encoder.reference_picture, 3);
	encoder_destroy_config(encoder);
}

static void
nal_start_code_prefix(struct bitstream *bs)
{
	bitstream_put_ui(bs, 0x00000001, 32);
}

static void
nal_header(struct bitstream *bs, const int nal_ref_idc, const int nal_unit_type)
{
	/* forbidden_zero_bit: 0 */
	bitstream_put_ui(bs, 0, 1);

	bitstream_put_ui(bs, nal_ref_idc, 2);
	bitstream_put_ui(bs, nal_unit_type, 5);
}

static void
rbsp_trailing_bits(struct bitstream *bs)
{
	bitstream_put_ui(bs, 1, 1);
	bitstream_byte_aligning(bs, 0);
}

/* If none of the inputs to this change after init then we can just do
 * this at init. */
static void sps_rbsp(struct bitstream *bs,
			const VAEncSequenceParameterBufferH264 * const seq,
			const int constraint_set_flag)
{
	int i;

	bitstream_put_ui(bs, PROFILE_IDC_BASELINE, 8);

	/* constraint_set[0-3] flag */
	for (i = 0; i < 4; i++) {
		int set = (constraint_set_flag & (1 << i)) ? 1 : 0;
		bitstream_put_ui(bs, set, 1);
	}

	/* reserved_zero_4bits */
	bitstream_put_ui(bs, 0, 4);
	bitstream_put_ui(bs, seq->level_idc, 8);
	bitstream_put_ue(bs, seq->seq_parameter_set_id);

	bitstream_put_ue(bs, seq->seq_fields.bits.log2_max_frame_num_minus4);
	bitstream_put_ue(bs, seq->seq_fields.bits.pic_order_cnt_type);
	bitstream_put_ue(bs,
			 seq->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);

	bitstream_put_ue(bs, seq->max_num_ref_frames);

	/* gaps_in_frame_num_value_allowed_flag */
	bitstream_put_ui(bs, 0, 1);

	/* pic_width_in_mbs_minus1, pic_height_in_map_units_minus1 */
	bitstream_put_ue(bs, seq->picture_width_in_mbs - 1);
	bitstream_put_ue(bs, seq->picture_height_in_mbs - 1);

	bitstream_put_ui(bs, seq->seq_fields.bits.frame_mbs_only_flag, 1);
	bitstream_put_ui(bs, seq->seq_fields.bits.direct_8x8_inference_flag, 1);

	bitstream_put_ui(bs, seq->frame_cropping_flag, 1);

	if (seq->frame_cropping_flag) {
		bitstream_put_ue(bs, seq->frame_crop_left_offset);
		bitstream_put_ue(bs, seq->frame_crop_right_offset);
		bitstream_put_ue(bs, seq->frame_crop_top_offset);
		bitstream_put_ue(bs, seq->frame_crop_bottom_offset);
	}

	/* vui_parameters_present_flag */
	bitstream_put_ui(bs, 1, 1);

	/* aspect_ratio_info_present_flag */
	bitstream_put_ui(bs, 0, 1);
	/* overscan_info_present_flag */
	bitstream_put_ui(bs, 0, 1);

	/* video_signal_type_present_flag */
	bitstream_put_ui(bs, 0, 1);
	/* chroma_loc_info_present_flag */
	bitstream_put_ui(bs, 0, 1);

	/* timing_info_present_flag */
	bitstream_put_ui(bs, 1, 1);
	bitstream_put_ui(bs, seq->num_units_in_tick, 32);
	bitstream_put_ui(bs, seq->time_scale, 32);

	/* fixed_frame_rate_flag */
	bitstream_put_ui(bs, 0, 1);

	/* nal_hrd_parameters_present_flag */
	bitstream_put_ui(bs, 0, 1);

	/* vcl_hrd_parameters_present_flag */
	bitstream_put_ui(bs, 0, 1);

	/* low_delay_hrd_flag */
	bitstream_put_ui(bs, 0, 1);

	/* pic_struct_present_flag */
	bitstream_put_ui(bs, 0, 1);
	/* bitstream_restriction_flag */
	bitstream_put_ui(bs, 0, 1);

	rbsp_trailing_bits(bs);
}

static void pps_rbsp(struct bitstream *bs,
			const struct rd_encoder * const encoder)
{
	VABufferID buffer = encoder->encoder.param.buffers[EncoderBufferPicture];
	VAEncPictureParameterBufferH264 *pic;
	VAStatus status;

	status = vaMapBuffer(encoder->va_dpy, buffer, (void **) &pic);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - pps_rbsp failed to map picture parameter buffer %d.\n",
				buffer);
	} else {
		/* pic_parameter_set_id, seq_parameter_set_id */
		bitstream_put_ue(bs, pic->pic_parameter_set_id);
		bitstream_put_ue(bs, pic->seq_parameter_set_id);

		bitstream_put_ui(bs, pic->pic_fields.bits.entropy_coding_mode_flag, 1);

		/* pic_order_present_flag: 0 */
		bitstream_put_ui(bs, 0, 1);

		/* num_slice_groups_minus1 */
		bitstream_put_ue(bs, 0);

		bitstream_put_ue(bs, pic->num_ref_idx_l0_active_minus1);
		bitstream_put_ue(bs, pic->num_ref_idx_l1_active_minus1);

		bitstream_put_ui(bs, pic->pic_fields.bits.weighted_pred_flag, 1);
		bitstream_put_ui(bs, pic->pic_fields.bits.weighted_bipred_idc, 2);

		/* pic_init_qp_minus26, pic_init_qs_minus26, chroma_qp_index_offset */
		bitstream_put_se(bs, pic->pic_init_qp - 26);
		bitstream_put_se(bs, 0);
		bitstream_put_se(bs, 0);

		bitstream_put_ui(bs, pic->pic_fields.bits.deblocking_filter_control_present_flag, 1);

		/* constrained_intra_pred_flag, redundant_pic_cnt_present_flag */
		bitstream_put_ui(bs, 0, 1);
		bitstream_put_ui(bs, 0, 1);

		bitstream_put_ui(bs, pic->pic_fields.bits.transform_8x8_mode_flag, 1);

		/* pic_scaling_matrix_present_flag */
		bitstream_put_ui(bs, 0, 1);
		bitstream_put_se(bs, pic->second_chroma_qp_index_offset);

		vaUnmapBuffer(encoder->va_dpy, buffer);
	}

	rbsp_trailing_bits(bs);
}

static int
build_packed_pic_buffer(const struct rd_encoder * const encoder,
			void ** const header_buffer)
{
	struct bitstream bs;

	bitstream_start(&bs);
	nal_start_code_prefix(&bs);
	nal_header(&bs, NAL_REF_IDC_HIGH, NAL_PPS);
	pps_rbsp(&bs, encoder);
	bitstream_end(&bs);

	*header_buffer = bs.buffer;
	return bs.bit_offset;
}

static int
build_packed_seq_buffer(const struct rd_encoder * const encoder,
			void ** const header_buffer)
{
	struct bitstream bs;
	VAEncSequenceParameterBufferH264 *seq;
	VAStatus status;
	VABufferID seq_buf;

	bitstream_start(&bs);
	nal_start_code_prefix(&bs);
	nal_header(&bs, NAL_REF_IDC_HIGH, NAL_SPS);

	seq_buf = encoder->encoder.param.buffers[EncoderBufferSequence];
	status = vaMapBuffer(encoder->va_dpy, seq_buf, (void **) &seq);
	if (status != VA_STATUS_SUCCESS) {
		printf("ERROR - build_packed_seq_buffer failed to map sequence parameter buffer %d.\n",
				seq_buf);
	} else {
		sps_rbsp(&bs, seq, encoder->encoder.constraint_set_flag);
		vaUnmapBuffer(encoder->va_dpy, seq_buf);
	}

	bitstream_end(&bs);

	*header_buffer = bs.buffer;
	return bs.bit_offset;
}

static int
create_packed_header_buffers(const struct rd_encoder * const encoder,
				VABufferID * const buffers,
				VAEncPackedHeaderType type,
				void * const data, const int bit_length)
{
	VAEncPackedHeaderParameterBuffer packed_header;
	VAStatus status;

	packed_header.type = type;
	packed_header.bit_length = bit_length;
	packed_header.has_emulation_bytes = 0;

	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncPackedHeaderParameterBufferType,
				sizeof packed_header, 1, &packed_header,
				&buffers[0]);
	if (status != VA_STATUS_SUCCESS) {
		printf("WARNING - failed to create PackedHeaderParameterBuffer.\n");
		buffers[0] = VA_INVALID_ID;
		return 0;
	}

	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncPackedHeaderDataBufferType,
				(bit_length + 7) / 8, 1, data, &buffers[1]);
	if (status != VA_STATUS_SUCCESS) {
		printf("WARNING - failed to create PackedHeaderDataBuffer.\n");
		vaDestroyBuffer(encoder->va_dpy, buffers[0]);
		buffers[0] = VA_INVALID_ID;
		buffers[1] = VA_INVALID_ID;
		return 0;
	}

	return 2;
}

static int
encoder_prepare_headers(struct rd_encoder * const encoder,
		VABufferID * const buffers)
{
	VABufferID *p = buffers;
	int bit_length;
	void *data = NULL;
	VAStatus status;

	if (encoder->encoder.param.seq_changed) {
		if (encoder->encoder.param.buffers[EncoderBufferSPSHeader] != VA_INVALID_ID) {
			status = vaDestroyBuffer(encoder->va_dpy,
					encoder->encoder.param.buffers[EncoderBufferSPSHeader]);
			if (status != VA_STATUS_SUCCESS) {
				printf("Failed to destroy SPSHeader buffer %d.\n",
					encoder->encoder.param.buffers[EncoderBufferSPSHeader]);
			}
		}
		if (encoder->encoder.param.buffers[EncoderBufferSPSData] != VA_INVALID_ID) {
			status = vaDestroyBuffer(encoder->va_dpy,
					encoder->encoder.param.buffers[EncoderBufferSPSData]);
			if (status != VA_STATUS_SUCCESS) {
				printf("Failed to destroy SPSData buffer %d.\n",
					encoder->encoder.param.buffers[EncoderBufferSPSData]);
			}
		}
		bit_length = build_packed_seq_buffer(encoder, &data);
		p += create_packed_header_buffers(encoder, p, VAEncPackedHeaderSequence,
					data, bit_length);
		free(data);
		data = NULL;
		encoder->encoder.param.buffers[EncoderBufferSPSHeader] = *(p-2);
		encoder->encoder.param.buffers[EncoderBufferSPSData] = *(p-1);
		encoder->encoder.param.seq_changed = 0;
	} else {
		p += 2;
		*(p-2) = encoder->encoder.param.buffers[EncoderBufferSPSHeader];
		*(p-1) = encoder->encoder.param.buffers[EncoderBufferSPSData];
	}

	if (encoder->encoder.param.buffers[EncoderBufferPPSHeader] != VA_INVALID_ID) {
		status = vaDestroyBuffer(encoder->va_dpy,
				encoder->encoder.param.buffers[EncoderBufferPPSHeader]);
		if (status != VA_STATUS_SUCCESS) {
			printf("Failed to destroy PPSHeader buffer %d.\n",
				encoder->encoder.param.buffers[EncoderBufferPPSHeader]);
		}
	}
	if (encoder->encoder.param.buffers[EncoderBufferPPSData] != VA_INVALID_ID) {
		status = vaDestroyBuffer(encoder->va_dpy,
				encoder->encoder.param.buffers[EncoderBufferPPSData]);
		if (status != VA_STATUS_SUCCESS) {
			printf("Failed to destroy PPSData buffer %d.\n",
				encoder->encoder.param.buffers[EncoderBufferPPSData]);
		}
	}
	bit_length = build_packed_pic_buffer(encoder, &data);
	p += create_packed_header_buffers(encoder, p, VAEncPackedHeaderPicture,
					data, bit_length);
	free(data);
	encoder->encoder.param.buffers[EncoderBufferPPSHeader] = *(p-2);
	encoder->encoder.param.buffers[EncoderBufferPPSData] = *(p-1);

	return p - buffers;
}

static VAStatus
encoder_render_picture(const struct rd_encoder * const encoder,
		const VASurfaceID input,
		VABufferID * const buffers, const int num_buffers)
{
	VAStatus status;

	status = vaBeginPicture(encoder->va_dpy, encoder->encoder.ctx, input);
	if (status != VA_STATUS_SUCCESS) {
		printf("WARNING - vaBeginPicture failed.\n");
		return status;
	}

	status = vaRenderPicture(encoder->va_dpy, encoder->encoder.ctx,
			buffers, num_buffers);
	if (status != VA_STATUS_SUCCESS) {
		printf("WARNING - vaRenderPicture failed.\n");
		return status;
	}

	status = vaEndPicture(encoder->va_dpy, encoder->encoder.ctx);
	if (status != VA_STATUS_SUCCESS) {
		printf("WARNING - vaEndPicture failed.\n");
	}
	return status;
}

static VABufferID
encoder_get_output_buffer(struct rd_encoder * const encoder)
{
	VAStatus status;
	int i;

	if (encoder == NULL) {
		fprintf(stderr, "encoder_get_output_buffer : No encoder.\n");
		return VA_INVALID_ID;
	}

	/* Use first free buffer ID... */
	for (i = 0; i < MAX_FRAMES; i++) {
		if (encoder->out_buf[i].bufferStatus == BUFFER_STATUS_FREE) {
			break;
		}
	}
	if (i == MAX_FRAMES) {
		printf("WARNING - no output buffer available.\n");
		return VA_INVALID_ID;
	}

	/* Use existing buffer if possible... */
	if (encoder->out_buf[i].bufferID != VA_INVALID_ID) {
		encoder->out_buf[i].bufferStatus = BUFFER_STATUS_IN_USE;
		return encoder->out_buf[i].bufferID;
	}

	/* Create new buffer if necessary... */
	status = vaCreateBuffer(encoder->va_dpy, encoder->encoder.ctx,
				VAEncCodedBufferType, encoder->encoder.output_size,
				1, NULL, &(encoder->out_buf[i].bufferID));
	if (status == VA_STATUS_SUCCESS) {
		encoder->out_buf[i].bufferStatus = BUFFER_STATUS_IN_USE;
	} else {
		encoder->out_buf[i].bufferID = VA_INVALID_ID;
		encoder->out_buf[i].bufferStatus = BUFFER_STATUS_FREE;
	}
	return encoder->out_buf[i].bufferID;
}

static int
rd_encoder_release_buffer(struct rd_encoder *encoder, int buf_id)
{
	int status = 0;
	int i;

	if (encoder == NULL) {
		fprintf(stderr, "rd_encoder_release_buffer : No encoder.\n");
		return status;
	}

	for (i=0; i < MAX_FRAMES; i++) {
		if ((VABufferID)buf_id == encoder->out_buf[i].bufferID) {
			status = vaReleaseBufferHandle(encoder->va_dpy, buf_id);
			if (status != VA_STATUS_SUCCESS) {
				fprintf(stderr, "Failed to release handle for buffer %d.\n", buf_id);
				return status;
			}
			encoder->out_buf[i].bufferStatus = BUFFER_STATUS_FREE;
			break;
		}
	}
	if (i == MAX_FRAMES) {
		fprintf(stderr, "WARNING - can't release: no match for buffer ID.\n");
	}

	return status;
}

enum output_write_status {
	OUTPUT_WRITE_SUCCESS,
	OUTPUT_WRITE_OVERFLOW,
	OUTPUT_WRITE_FATAL
};

static enum output_write_status
encoder_write_output(struct rd_encoder * const encoder,
		const VABufferID output_buf)
{
	VACodedBufferSegment *segment;
	VAStatus status;
	VABufferInfo buf_info;
	unsigned int stream_size = 0;
	int frame_number;
#ifdef PROFILE_REMOTE_DISPLAY
	struct timespec start_spec, end_spec;
	int64_t duration;
#endif

	if (encoder == NULL) {
		fprintf(stderr, "encoder_write_output : No encoder.\n");
		return OUTPUT_WRITE_FATAL;
	}

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_spec);
	}
#endif

	vaSyncSurface(encoder->va_dpy, encoder->vpp.output);
	frame_number = encoder->current_encode.frame_number;

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = ( timespec_to_nsec(&end_spec) -
			timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] Send synced in %ld us.\n",
					frame_number, duration);
		start_spec = end_spec;
	}
#endif

	/* We need to map the buffer so that we can get the segment size. */
	status = vaMapBuffer(encoder->va_dpy, output_buf, (void **) &segment);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "encoder_write_output : vaMapBuffer failed for frame %d.\n",
			frame_number);
		return OUTPUT_WRITE_FATAL;
	}
#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = ( timespec_to_nsec(&end_spec) -
			timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] Buffer mapped in %ld us.\n",
					frame_number, duration);
		start_spec = end_spec;
	}
#endif

	if (segment->status & VA_CODED_BUF_STATUS_SLICE_OVERFLOW_MASK) {
		encoder->encoder.output_size *= 2;
		vaUnmapBuffer(encoder->va_dpy, output_buf);
		return OUTPUT_WRITE_OVERFLOW;
	}

	stream_size = segment->size;

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = ( timespec_to_nsec(&end_spec) -
			timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] Buffer modified in %ld us.\n",
					frame_number, duration);
		start_spec = end_spec;
	}
#endif

	vaUnmapBuffer(encoder->va_dpy, output_buf);
#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = ( timespec_to_nsec(&end_spec) -
			timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] Buffer unmapped in %ld us.\n",
					frame_number, duration);
		start_spec = end_spec;
	}
#endif

	memset(&buf_info, 0, sizeof(buf_info));
	buf_info.mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM;

	/* The output buffer is released in rd_encoder_release_buffer(). */
	status = vaAcquireBufferHandle(encoder->va_dpy, output_buf, &buf_info);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "Failed to acquire buffer handle for frame %d.\n",
			frame_number);
		return OUTPUT_WRITE_FATAL;
	}

	pthread_mutex_lock(&encoder->transport_mutex);
	if (encoder->next_transport.valid == 1) {
		fprintf(stderr, "WARNING: transport dropping frame %d.\n",
			encoder->next_transport.frame_number);
			rd_encoder_release_buffer(encoder, encoder->next_transport.output_buf);
	}
	encoder->next_transport.handle = buf_info.handle;
	encoder->next_transport.stream_size = stream_size;
	encoder->next_transport.timestamp = encoder->current_encode.timestamp;
	encoder->next_transport.output_buf = output_buf;
	encoder->next_transport.frame_number = encoder->current_encode.frame_number;
	encoder->next_transport.valid = 1;
	pthread_cond_signal(&encoder->transport_cond);
	pthread_mutex_unlock(&encoder->transport_mutex);

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		int64_t finish;

		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		finish = timespec_to_nsec(&end_spec);
		duration = ( finish - timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] New frame sent to transport in %ld us, "
					"submitted: %ld ns\n",
					frame_number, duration, finish);
	}
#endif
	return OUTPUT_WRITE_SUCCESS;
}

static void
encoder_encode(struct rd_encoder * const encoder, const VASurfaceID input)
{
	VABufferID output_buf = VA_INVALID_ID;
	VABufferID buffers[8];
	int bufferCount = 0;
	int numParamBuffers = 0;
	int i, slice_type;
	int frame_number;
	enum output_write_status ret = 0;
#ifdef PROFILE_REMOTE_DISPLAY
	struct timespec start_spec, end_spec;
	int64_t duration;
#endif

	if (encoder == NULL) {
		fprintf(stderr, "encoder_encode : No encoder.\n");
		return;
	}

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_spec);
	}
#endif

	frame_number = encoder->current_encode.frame_number;
	if (encoder->verbose > 2) {
		printf("Encoding frame %d.\n", frame_number);
	}

	if ((encoder->frame_count % encoder->encoder.intra_period) == 0)
		slice_type = SLICE_TYPE_I;
	else
		slice_type = SLICE_TYPE_P;

	buffers[bufferCount++] = encoder_update_seq_parameters(encoder);
	buffers[bufferCount++] = encoder_update_HRD_parameters(encoder);
	buffers[bufferCount++] = encoder->encoder.param.buffers[EncoderBufferQualityLevel];
	numParamBuffers = bufferCount;

	for (i = 0; i < numParamBuffers; i++)
		if (buffers[i] == VA_INVALID_ID) {
			printf("Invalid parameter buffer.\n");
			return;
		}

	/* Send SPS/PPS before every I frame or after a frame rate change. */
	if ((slice_type == SLICE_TYPE_I) || (encoder->encoder.param.seq_changed)) {
		int numHeaderBuffers;

		/* encoder_prepare_headers returns a value <= 4 */
		numHeaderBuffers = encoder_prepare_headers(encoder,
				buffers + bufferCount);
		bufferCount += numHeaderBuffers;
	}

	do {
		/* Keep retrying with larger buffer sizes until we have success. */
		output_buf = encoder_get_output_buffer(encoder);
		if (output_buf == VA_INVALID_ID) {
			printf("Invalid output buffer.\n");
			return;
		}

		buffers[bufferCount++] =
			encoder_update_pic_parameters(encoder, output_buf, slice_type);
		if (buffers[bufferCount - 1] == VA_INVALID_ID) {
			printf("Invalid pic parameters buffer.\n");
			return;
		}

		buffers[bufferCount++] = encoder_update_slice_parameter(encoder,
				slice_type);
		if (buffers[bufferCount - 1] == VA_INVALID_ID) {
			printf("Invalid image data buffer.\n");
			return;
		}

		encoder_render_picture(encoder, input, buffers, bufferCount);

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = ( timespec_to_nsec(&end_spec) -
			timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] Buffer %d encoded in %ld us.\n",
					frame_number, output_buf, duration);
		start_spec = end_spec;
	}
#endif

		ret = encoder_write_output(encoder, output_buf);

		/* The output buffer is to be destroyed on encoder destruction
		 * in the normal case but we need to destroy it before creating
		 * a larger one. */
		if (ret == OUTPUT_WRITE_OVERFLOW) {
			printf("\n !!! Buffer too small, so re-try needed!!!\n\n");
			vaDestroyBuffer(encoder->va_dpy, output_buf);
			output_buf = VA_INVALID_ID;
		}
	} while (ret == OUTPUT_WRITE_OVERFLOW);

	if (ret == OUTPUT_WRITE_FATAL) {
		printf("Fatal output write error.\n");
		encoder->error = errno;
	}

	encoder->frame_count++;
#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = ( timespec_to_nsec(&end_spec) -
			timespec_to_nsec(&start_spec) ) / NS_IN_US;
		printf("RD-ENCODER:\tFrame[%d] New frame sent to be written out in %ld us.\n",
					frame_number, duration);
	}
#endif
	return;
}


static int
setup_vpp(struct rd_encoder * const encoder)
{
	VAStatus status;

	if (encoder == NULL) {
		fprintf(stderr, "setup_vpp : No encoder.\n");
		return -1;
	}

	status = vaCreateConfig(encoder->va_dpy, VAProfileNone,
				VAEntrypointVideoProc, NULL, 0,
				&encoder->vpp.cfg);
	if (status != VA_STATUS_SUCCESS) {
		printf("encoder: failed to create VPP config\n");
		return -1;
	}

	status = vaCreateContext(encoder->va_dpy, encoder->vpp.cfg, encoder->width, encoder->height,
				 0, NULL, 0, &encoder->vpp.ctx);
	if (status != VA_STATUS_SUCCESS) {
		printf("encoder: failed to create VPP context\n");
		goto err_cfg;
	}

	status = vaCreateBuffer(encoder->va_dpy, encoder->vpp.ctx,
				VAProcPipelineParameterBufferType,
				sizeof(VAProcPipelineParameterBuffer),
				1, NULL, &encoder->vpp.pipeline_buf);
	if (status != VA_STATUS_SUCCESS) {
		printf("encoder: failed to create VPP pipeline buffer\n");
		goto err_ctx;
	}

	status = vaCreateSurfaces(encoder->va_dpy, VA_RT_FORMAT_YUV420,
				  encoder->region.w, encoder->region.h, &encoder->vpp.output, 1,
				  NULL, 0);
	if (status != VA_STATUS_SUCCESS) {
		printf("encoder: failed to create YUV surface\n");
		goto err_buf;
	}

	return 0;

err_buf:
	vaDestroyBuffer(encoder->va_dpy, encoder->vpp.pipeline_buf);
err_ctx:
	vaDestroyContext(encoder->va_dpy, encoder->vpp.ctx);
err_cfg:
	vaDestroyConfig(encoder->va_dpy, encoder->vpp.cfg);

	return -1;
}

static void
vpp_destroy(struct rd_encoder * const encoder)
{
	if (encoder) {
		if (encoder->vpp.output) {
			vaDestroySurfaces(encoder->va_dpy, &encoder->vpp.output, 1);
		}
		if (encoder->vpp.pipeline_buf) {
			vaDestroyBuffer(encoder->va_dpy, encoder->vpp.pipeline_buf);
		}
		if (encoder->vpp.ctx) {
			vaDestroyContext(encoder->va_dpy, encoder->vpp.ctx);
		}
		if (encoder->vpp.cfg) {
			vaDestroyConfig(encoder->va_dpy, encoder->vpp.cfg);
		}
	}
}

static int
setup_encoder_thread(struct rd_encoder * const encoder)
{
	int err;

	if (encoder == NULL) {
		fprintf(stderr, "setup_encoder_thread : No encoder.\n");
		return -1;
	}

	err = pthread_mutex_init(&encoder->encoder_mutex, NULL);
	if (err != 0) {
		fprintf(stderr, "Encoder mutex init failure: %d\n", err);
		return err;
	}
	err = pthread_cond_init(&encoder->encoder_cond, NULL);
	if (err != 0) {
		fprintf(stderr, "Encoder condition init failure: %d\n", err);
		return err;
	}
	err = pthread_create(&encoder->encoder_thread, NULL, encoder_thread_function, encoder);
	if (err != 0) {
		fprintf(stderr, "Encoder thread creation failure: %d\n", err);
		return err;
	}

	return 0;
}

static int
setup_transport_thread(struct rd_encoder * const encoder)
{
	int err;

	if (encoder == NULL) {
		fprintf(stderr, "setup_transport_thread : No encoder.\n");
		return -1;
	}

	err = pthread_mutex_init(&encoder->transport_mutex, NULL);
	if (err != 0) {
		fprintf(stderr, "Transport mutex init failure: %d\n", err);
		return err;
	}
	err = pthread_cond_init(&encoder->transport_cond, NULL);
	if (err != 0) {
		fprintf(stderr, "Transport condition init failure: %d\n", err);
		return err;
	}
	err = pthread_create(&encoder->transport_thread, NULL, transport_thread_function, encoder);
	if (err != 0) {
		fprintf(stderr, "Transport thread creation failure: %d\n", err);
		return err;
	}

	return 0;
}

static void
destroy_encoder_thread(struct rd_encoder * const encoder)
{
	if (encoder->encoder_thread) {
		/* Make sure the encoder thread finishes... */
		if (encoder->verbose > 1) {
			printf("Waiting for encoder thread mutex...\n");
		}
		pthread_mutex_lock(&encoder->encoder_mutex);
		encoder->destroying_encoder = 1;
		pthread_cond_signal(&encoder->encoder_cond);
		pthread_mutex_unlock(&encoder->encoder_mutex);

		if (encoder->verbose > 1) {
			printf("Waiting for encoder thread to finish...\n");
		}
		pthread_join(encoder->encoder_thread, NULL);
		pthread_mutex_destroy(&encoder->encoder_mutex);
		pthread_cond_destroy(&encoder->encoder_cond);
	}
}

static void
destroy_transport_thread(struct rd_encoder * const encoder)
{
	if (encoder->transport_thread) {
		/* Make sure the transport thread finishes... */
		if (encoder->verbose > 1) {
			printf("Waiting for transport thread mutex...\n");
		}
		pthread_mutex_lock(&encoder->transport_mutex);
		encoder->destroying_transport = 1;
		pthread_cond_signal(&encoder->transport_cond);
		pthread_mutex_unlock(&encoder->transport_mutex);

		if (encoder->verbose > 1) {
			printf("Waiting for transport thread to finish...\n");
		}
		pthread_join(encoder->transport_thread, NULL);
		pthread_mutex_destroy(&encoder->transport_mutex);
		pthread_cond_destroy(&encoder->transport_cond);
	}
}

static int
load_transport_plugin(const char *plugin, struct rd_encoder *encoder,
		int *argc, char **argv)
{
	int (*plugin_init_fptr)(int *argc, char **argv,
			void **plugin_private_data, int verbose);

	if (plugin == NULL) {
		fprintf(stderr, "load_transport_plugin : no plugin name provided\n");
		return -1;
	}

	if (encoder == NULL) {
		fprintf(stderr, "load_transport_plugin : No encoder.\n");
		return -1;
	}

	encoder->transport_handle = dlopen(plugin, RTLD_LAZY | RTLD_LOCAL);
	if (encoder->transport_handle == NULL) {
			fprintf(stderr, "Failed to load transport plugin at %s.\n",
				plugin);
		return -1;
	}
	if (encoder->verbose) {
		printf("Loaded transport plugin at %s...\n",
						plugin);
	}

	plugin_init_fptr = dlsym(encoder->transport_handle, "init");
	if (plugin_init_fptr) {
		int ret = (*plugin_init_fptr)(argc, argv,
			&(encoder->transport_private_data), encoder->verbose);

		if (ret) {
			fprintf(stderr, "Init function in %s transport plugin "
				"failed with %d.\n", plugin, ret);
			return -1;
		}
	} else {
		fprintf(stderr, "No init function found in %s transport plugin.\n",
				plugin);
		return -1;
	}
	encoder->transport_send_fptr = dlsym(encoder->transport_handle,
			"send_frame");
	if (encoder->transport_send_fptr == NULL) {
		fprintf(stderr, "No send function found in %s transport plugin.\n",
				plugin);
		return -1;
	}

	return 0;
}

static int
destroy_transport_plugin(struct rd_encoder *encoder)
{
	if (encoder->verbose) {
		printf("Destroy transport plugin...\n");
	}

	if (encoder->transport_handle) {
		void (*transport_destroy_fptr)(void **transport_private_data);

		transport_destroy_fptr = dlsym(encoder->transport_handle, "destroy");
		if (transport_destroy_fptr) {
			(*transport_destroy_fptr)(&(encoder->transport_private_data));
		} else {
			fprintf(stderr, "No destroy function found in transport plugin.\n");
		}

		if (encoder->verbose) {
			printf("Closing DLL...\n");
		}
		dlclose(encoder->transport_handle);
	}
	return 0;
}

struct rd_encoder *
rd_encoder_create(const int verbose, char *plugin, int *argc, char **argv)
{
	struct rd_encoder *encoder;
	VAStatus status;
	int major, minor;
	int i;
	int err;

	encoder = zalloc(sizeof(*encoder));
	if (encoder == NULL) {
		return NULL;
	}

	encoder->drm_fd = -1;
	encoder->verbose = verbose;

	encoder->drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if(encoder->drm_fd < 0) {
		fprintf(stderr, "Failed to open card0.\n");
		goto err_encoder;
	}

	encoder->drm_bufmgr = drm_intel_bufmgr_gem_init(encoder->drm_fd, DRM_BUF_MGR_SIZE);

	if (encoder->drm_bufmgr == NULL) {
		goto err_encoder;
	}

	err = load_transport_plugin(plugin, encoder, argc, argv);
	if (err != 0) {
		goto err_encoder;
	}

	/* Buffers will be created on request... */
	for (i = 0; i < MAX_FRAMES; i++) {
		encoder->out_buf[i].bufferID = VA_INVALID_ID;
		encoder->out_buf[i].bufferStatus = BUFFER_STATUS_FREE;
	}

	encoder->vpp.output = VA_INVALID_ID;

	encoder->va_dpy = vaGetDisplayDRM(encoder->drm_fd);
	if (!encoder->va_dpy) {
		fprintf(stderr, "encoder: Failed to create VA display.\n");
		goto err_encoder;
	}

	status = vaInitialize(encoder->va_dpy, &major, &minor);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "encoder: Failed to initialize display.\n");
		goto err_encoder;
	}

	return encoder;

err_encoder:
	if (encoder) {
		rd_encoder_destroy(encoder);
	}
	return NULL;
}

int
rd_encoder_init(struct rd_encoder * const encoder,
				const int width, const int height,
				const int x, const int y,
				const int w, const int h,
				const int encoder_tu,
				uint32_t surfid, struct ias_hmi * const hmi,
				struct wl_display *display, uint32_t output_number)
{
	int err;

	if (encoder == NULL) {
		fprintf(stderr, "rd_encoder_init : No encoder.\n");
		return -1;
	}

	encoder->width = width;
	encoder->height = height;

	encoder->region.x = x;
	encoder->region.y = y;
	encoder->region.w = w;
	encoder->region.h = h;
	encoder->encoder_tu = encoder_tu;
	encoder->surfid = surfid;
	encoder->hmi = hmi;
	encoder->display = display;
	encoder->output_number = output_number;
	if (setup_vpp(encoder) < 0) {
		fprintf(stderr, "encoder: Failed to initialize VPP pipeline.\n");
		goto err_va_dpy;
	}

	if (setup_encoder(encoder) < 0) {
		goto err_vpp;
	}

	err = setup_encoder_thread(encoder);
	if (err != 0) {
		goto err_vpp;
	}

	err = setup_transport_thread(encoder);
	if (err != 0) {
		goto err_vpp;
	}

	if (encoder->verbose) {
		printf("Recorder created...\n");
	}

	return 0;

err_vpp:
	vpp_destroy(encoder);
err_va_dpy:
	vaTerminate(encoder->va_dpy);
	encoder->va_dpy = 0;

	return -1;
}

void
rd_encoder_destroy(struct rd_encoder *encoder)
{
	int i;
	int status;

	destroy_encoder_thread(encoder);
	destroy_transport_thread(encoder);
	if (encoder->verbose) {
		printf("Worker threads destroyed...\n");
	}

	destroy_transport_plugin(encoder);
	if (encoder->verbose) {
		printf("Transport plugin destroyed...\n");
	}

	encoder_destroy_encode_session(encoder);
	vpp_destroy(encoder);
	for (i = 0; i < MAX_FRAMES; i++) {
		if (encoder->out_buf[i].bufferID != VA_INVALID_ID) {
			status = vaDestroyBuffer(encoder->va_dpy, encoder->out_buf[i].bufferID);
			if (status != VA_STATUS_SUCCESS) {
				fprintf(stderr, "Failed to destroy buffer %d.\n",
						encoder->out_buf[i].bufferID);
			} else {
				encoder->out_buf[i].bufferID = VA_INVALID_ID;
				encoder->out_buf[i].bufferStatus = BUFFER_STATUS_FREE;
			}
		}
	}
	vaTerminate(encoder->va_dpy);
	if (encoder->verbose) {
		printf("libva context destroyed...\n");
	}

	close(encoder->drm_fd);

	free(encoder);
	if (encoder->verbose) {
		printf("Recorder destroyed...\n");
	}
}

static VAStatus
create_surface_from_handle(struct rd_encoder * const encoder,
	VASurfaceID * const surface)
{
	/* Wrap up the existing buffer as a VA surface... */
	VASurfaceAttrib va_attribs[2];
	VASurfaceAttribExternalBuffers va_attrib_extbuf;
	VAStatus status = VA_STATUS_SUCCESS;

	unsigned long buffer_handle = encoder->current_encode.va_buffer_handle;

	va_attrib_extbuf.width = encoder->width;
	va_attrib_extbuf.height = encoder->height;
	va_attrib_extbuf.pitches[0] = encoder->current_encode.stride;
	va_attrib_extbuf.offsets[0] = 0;
	va_attrib_extbuf.buffers = &buffer_handle;
	va_attrib_extbuf.num_buffers = 1;
	va_attrib_extbuf.flags = 0;
	va_attrib_extbuf.private_data = NULL;

	va_attribs[0].type = VASurfaceAttribMemoryType;
	va_attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
	va_attribs[0].value.type = VAGenericValueTypeInteger;
	va_attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM;

	va_attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
	va_attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
	va_attribs[1].value.type = VAGenericValueTypePointer;
	va_attribs[1].value.value.p = &va_attrib_extbuf;

	if (encoder->current_encode.format == RD_FORMAT_RGB) {
		va_attrib_extbuf.pixel_format = VA_FOURCC_BGRX;
		va_attrib_extbuf.data_size = encoder->height * encoder->current_encode.stride;
		va_attrib_extbuf.num_planes = 1;

		status = vaCreateSurfaces(encoder->va_dpy, VA_RT_FORMAT_RGB32,
					encoder->width, encoder->height, surface, 1,
					va_attribs, 2);
	} else {
		printf("Unsupported colour format for shared memory buffer.\n");
	}

	return status;
}

static VAStatus
create_surface_from_fd(struct rd_encoder * const encoder,
	VASurfaceID * const surface)
{
	/* Wrap up the existing buffer as a VA surface... */
	VASurfaceAttrib va_attribs[2];
	VASurfaceAttribExternalBuffers va_attrib_extbuf;
	VAStatus status = VA_STATUS_SUCCESS;

	unsigned long buffer_fd = encoder->current_encode.prime_fd;

	va_attrib_extbuf.width = encoder->width;
	va_attrib_extbuf.height = encoder->height;
	va_attrib_extbuf.pitches[0] = encoder->current_encode.stride;
	va_attrib_extbuf.offsets[0] = 0;
	va_attrib_extbuf.buffers = &buffer_fd;
	va_attrib_extbuf.num_buffers = 1;
	va_attrib_extbuf.flags = 0;
	va_attrib_extbuf.private_data = NULL;

	va_attribs[0].type = VASurfaceAttribMemoryType;
	va_attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
	va_attribs[0].value.type = VAGenericValueTypeInteger;
	va_attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;

	va_attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
	va_attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
	va_attribs[1].value.type = VAGenericValueTypePointer;
	va_attribs[1].value.value.p = &va_attrib_extbuf;

	if (encoder->current_encode.format == RD_FORMAT_RGB) {
		va_attrib_extbuf.pixel_format = VA_FOURCC_BGRX;
		va_attrib_extbuf.data_size = encoder->height * encoder->current_encode.stride;
		va_attrib_extbuf.num_planes = 1;

		status = vaCreateSurfaces(encoder->va_dpy, VA_RT_FORMAT_RGB32,
					encoder->width, encoder->height, surface, 1,
					va_attribs, 2);
	} else { /* NV12 */
		int aligned_height = (encoder->height + 0x7F) & ~0x7F;

		va_attrib_extbuf.pixel_format = VA_FOURCC_NV12;
		va_attrib_extbuf.data_size = aligned_height *
				encoder->current_encode.stride * 3 / 2;
		va_attrib_extbuf.num_planes = 2;
		va_attrib_extbuf.pitches[1] = encoder->current_encode.stride;
		va_attrib_extbuf.offsets[1] = 0;

		status = vaCreateSurfaces(encoder->va_dpy, VA_RT_FORMAT_YUV420,
					encoder->width, encoder->height, surface, 1,
					va_attribs, 2);
	}

	return status;
}

static VAStatus
convert_rgb_to_yuv(struct rd_encoder * const encoder, const VASurfaceID src_surface)
{
	VAProcPipelineParameterBuffer *pipeline_param;
	VAStatus status;
	VARectangle surf_region, output_region;

	status = vaMapBuffer(encoder->va_dpy, encoder->vpp.pipeline_buf,
				(void **) &pipeline_param);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaMapBuffer failed\n");
		return status;
	}

	memset(pipeline_param, 0, sizeof *pipeline_param);

	surf_region.x =  encoder->region.x;
	surf_region.y =  encoder->region.y;
	surf_region.width = encoder->region.w;
	surf_region.height = encoder->region.h;

	output_region.x = 0;
	output_region.y = 0;
	output_region.width = encoder->region.w;
	output_region.height = encoder->region.h;

	pipeline_param->surface_region = &surf_region;
	pipeline_param->output_region = &output_region;

	pipeline_param->surface = src_surface;
	if (encoder->current_encode.format == RD_FORMAT_RGB) {
		pipeline_param->surface_color_standard  = VAProcColorStandardNone;
	} else { /* NV12 */
		pipeline_param->surface_color_standard  = VAProcColorStandardBT601;
	}

	pipeline_param->output_background_color = 0xff000000;
	pipeline_param->output_color_standard   = VAProcColorStandardBT601;

	status = vaUnmapBuffer(encoder->va_dpy, encoder->vpp.pipeline_buf);
	if (status != VA_STATUS_SUCCESS){
		fprintf(stderr, "convert_rgb_to_yuv() vaUnmapBuffer failed\n");
		return status;
	}

	vaSyncSurface(encoder->va_dpy, src_surface);

	status = vaBeginPicture(encoder->va_dpy, encoder->vpp.ctx, encoder->vpp.output);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaBeginPicture failed\n");
		return status;
	}

	status = vaRenderPicture(encoder->va_dpy, encoder->vpp.ctx,
				 &encoder->vpp.pipeline_buf, 1);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaRenderPicture failed\n");
		return status;
	}

	status = vaEndPicture(encoder->va_dpy, encoder->vpp.ctx);
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaEndPicture failed\n");
		return status;
	}

	return status;
}

static void
encoder_frame(struct rd_encoder * const encoder)
{
	VASurfaceID src_surface = VA_INVALID_ID;
	VAStatus status, conv_status;
	int64_t finish = 0;
	struct timespec end_spec;
	int frame_number;
#ifdef PROFILE_REMOTE_DISPLAY
	struct timespec start_spec;
	int64_t duration, start;

	if (encoder->profile_level) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_spec);
		start = timespec_to_nsec(&start_spec);
	}
#endif

	frame_number = encoder->current_encode.frame_number;

	if (encoder->current_encode.va_buffer_handle) {
		/* We assume that all shm buffers contain RGB data. */
		status = create_surface_from_handle(encoder, &src_surface);
		if (status != VA_STATUS_SUCCESS) {
			fprintf(stderr, "[libva encoder] failed to create surface from handle for frame %d.\n",
				frame_number);
			return;
		}
	} else {
		/* Not a shared memory buffer... */
		status = create_surface_from_fd(encoder, &src_surface);
		if (status != VA_STATUS_SUCCESS) {
			fprintf(stderr, "[libva encoder] failed to create surface from fd for frame %d.\n",
				frame_number);
			return;
		}
	}
	if (encoder->verbose > 2) {
		printf("Surface created for frame %d.\n", frame_number);
	}

	conv_status = convert_rgb_to_yuv(encoder, src_surface);

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		duration = timespec_to_nsec(&end_spec) - start;
		printf("RD-ENCODER:\tFrame[%d] New frame converted in %ld us.\n",
					frame_number, duration / NS_IN_US);
	}
#endif

	if (conv_status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "[libva encoder] color space conversion failed for frame %d.\n",
			frame_number);
		return;
	}

	encoder_encode(encoder, encoder->vpp.output);
	if (encoder->profile_level > 1) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		finish = timespec_to_nsec(&end_spec);
		printf("RD-ENCODER:\tFrame[%d] Encoder completed at: %ld ns.\n",
			frame_number, finish);
	}

	vaDestroySurfaces(encoder->va_dpy, &src_surface, 1);
	close(encoder->current_encode.prime_fd);

	if (encoder->verbose > 2) {
		printf("Releasing buffer for frame %d...\n", frame_number);
	}
	if (encoder->current_encode.va_buffer_handle) {
		/* Shared memory surface. */
		ias_hmi_release_buffer_handle(encoder->hmi,
			encoder->current_encode.shm_surf_id,
			encoder->current_encode.buf_id,
			encoder->current_encode.image_id,
			encoder->surfid, 0);
	} else if (encoder->surfid) {
		/* Wayland buffer surface. */
		ias_hmi_release_buffer_handle(encoder->hmi, 0, 0, 0,
				encoder->surfid, 0);
	} else {
		/* Full framebuffer. */
		ias_hmi_release_buffer_handle(encoder->hmi, 0, 0, 0, 0,
				encoder->output_number);
	}
	wl_display_flush(encoder->display);

#ifdef PROFILE_REMOTE_DISPLAY
	if (encoder->profile_level) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
		finish = timespec_to_nsec(&end_spec);
		duration = finish - start;
		printf("RD-ENCODER:\tFrame[%d] processed - start: %ld ns, "
					"finish: %ld ns, duration: %ld ns.\n",
					frame_number, start, finish, duration);
	}
#endif
}

static void *
encoder_thread_function(void * const data)
{
	struct rd_encoder *encoder = data;

	while (!encoder->destroying_encoder) {
		pthread_mutex_lock(&encoder->encoder_mutex);

		if (!encoder->next_encode.valid) {
			if (encoder->verbose > 1) {
				printf("Waiting on encoder condition...\n");
			}
			pthread_cond_wait(&encoder->encoder_cond, &encoder->encoder_mutex);
		}
		if (encoder->verbose > 1) {
			printf("Encoder thread running...\n");
		}

		/* If the thread is woken by destroy_encoder_thread()
		 * then there might not be valid input. */
		if (!encoder->next_encode.valid) {
			if (encoder->verbose > 1) {
				printf("No encode in queue.\n");
			}
			pthread_mutex_unlock(&encoder->encoder_mutex);
			continue;
		}

		if (!encoder->destroying_encoder) {
			encoder->current_encode.prime_fd = encoder->next_encode.prime_fd;
			encoder->current_encode.stride = encoder->next_encode.stride;
			encoder->current_encode.va_buffer_handle = encoder->next_encode.va_buffer_handle;
			encoder->current_encode.format = encoder->next_encode.format;
			encoder->current_encode.timestamp = encoder->next_encode.timestamp;
			encoder->current_encode.frame_number = encoder->next_encode.frame_number;
			encoder->current_encode.shm_surf_id = encoder->next_encode.shm_surf_id;
			encoder->current_encode.buf_id = encoder->next_encode.buf_id;
			encoder->current_encode.image_id = encoder->next_encode.image_id;
			encoder->current_encode.valid =  encoder->next_encode.valid;
			encoder->next_encode.valid = 0;
			pthread_mutex_unlock(&encoder->encoder_mutex);

			if (encoder->verbose > 2) {
				printf("RD-ENCODER:\tFrame[%d] encode starting.\n",
					encoder->current_encode.frame_number);
			}
			encoder_frame(encoder);
			if (encoder->verbose > 2) {
				printf("RD-ENCODER:\tFrame[%d] encode completed.\n",
					encoder->current_encode.frame_number);
			}

		} else {
			pthread_mutex_unlock(&encoder->encoder_mutex);
			if (encoder->verbose) {
				printf("encoder_thread_function skipping frame since encoder is being destroyed...\n");
			}
		}
	}

	return NULL;
}

static void *
transport_thread_function(void * const data)
{
	struct rd_encoder *encoder = data;

#ifdef PROFILE_REMOTE_DISPLAY
	struct timespec end_spec;
	int64_t finish;
#endif

	while (!encoder->destroying_transport) {
		pthread_mutex_lock(&encoder->transport_mutex);

		if (!encoder->next_transport.valid) {
			pthread_cond_wait(&encoder->transport_cond, &encoder->transport_mutex);
		}

		/* If the thread is woken by destroy_encoder_thread()
		 * then there might not be valid input. */
		if (!encoder->next_transport.valid) {
			if (encoder->verbose > 1) {
				printf("No transport in queue.\n");
			}
			pthread_mutex_unlock(&encoder->transport_mutex);
			continue;
		}

		if (!encoder->destroying_transport) {
			drm_intel_bo *drm_bo = NULL;

			encoder->current_transport.handle = encoder->next_transport.handle;
			encoder->current_transport.stream_size = encoder->next_transport.stream_size;
			encoder->current_transport.timestamp = encoder->next_transport.timestamp;
			encoder->current_transport.valid = encoder->next_transport.valid;
			encoder->current_transport.output_buf = encoder->next_transport.output_buf;
			encoder->current_transport.frame_number = encoder->next_transport.frame_number;
			encoder->next_transport.valid = 0;

			pthread_mutex_unlock(&encoder->transport_mutex);

			drm_bo = drm_intel_bo_gem_create_from_name(
									encoder->drm_bufmgr,
									"temp1",
									encoder->current_transport.handle);

			if (drm_bo == NULL) {
				fprintf(stderr, "Failed to create drm buffer.\n");
				rd_encoder_release_buffer(encoder, encoder->current_transport.output_buf);
				return NULL;
			}

			drm_intel_bo_map(drm_bo, 1);

			(*encoder->transport_send_fptr)(
				encoder->transport_private_data,
				drm_bo,
				encoder->current_transport.stream_size,
				encoder->current_transport.timestamp);

			drm_intel_bo_unmap(drm_bo);
			drm_intel_bo_unreference(drm_bo);
		} else {
			pthread_mutex_unlock(&encoder->transport_mutex);
			if (encoder->verbose) {
				printf("transport_thread_function skipping since encoder is being destroyed...\n");
			}
		}

		rd_encoder_release_buffer(encoder, encoder->current_transport.output_buf);

#ifdef PROFILE_REMOTE_DISPLAY
		if (encoder->profile_level) {
			clock_gettime(CLOCK_MONOTONIC_RAW, &end_spec);
			finish = timespec_to_nsec(&end_spec);
			printf("RD-ENCODER:\tFrame[%d] transport_thread_function - "
						"finish: %ld ns\n",
						encoder->current_transport.frame_number, finish);
		}
#endif
	}

	return NULL;
}

int
rd_encoder_frame(struct rd_encoder * const encoder,
		int32_t va_buffer_handle, int32_t prime_fd,
		int32_t stride0, int32_t stride1, int32_t stride2,
		uint32_t timestamp, enum rd_encoder_format format,
		int32_t frame_number, uint32_t shm_surf_id,
		uint32_t buf_id, uint32_t image_id)
{
	/* TODO: Added additional strides, need to use them */
	if (encoder->verbose > 1) {
		printf("Frame %d received...\n", frame_number);
	}

	if (encoder->error) {
		printf("WARNING: Dropping frame, owing to previous error...\n");
		ias_hmi_release_buffer_handle(encoder->hmi,
			shm_surf_id, buf_id, image_id, encoder->surfid,
					encoder->output_number);
		return -1;
	}

	if (encoder->first_frame == 0) {
		encoder->first_frame = 1;
		/* First notification is with a stale buffer */
		if (encoder->verbose || encoder->profile_level) {
			printf("RD-ENCODER:\tFrame[%d] dropped.\n",
						encoder->current_encode.frame_number);
		}

		ias_hmi_release_buffer_handle(encoder->hmi,
			shm_surf_id, buf_id, image_id, encoder->surfid,
					encoder->output_number);
		return 0;
	}

	pthread_mutex_lock(&encoder->encoder_mutex);

	/* The mutex is never released while encoding, so this point should
	 * never be reached if next_encode.valid is true. However, if the system
	 * is under heavy load, it can happen that the encoder does not get
	 * scheduled between frames being submitted. In this case, we
	 * drop the older frame. Dropping the current frame as well is too
	 * aggressive. */
	if (encoder->next_encode.valid) {
		/* Drop queued frame... */
		printf("WARNING: Dropping frame %d, since a newer frame is available to encode.\n",
				encoder->next_encode.frame_number);
		encoder->next_encode.valid = 0;
		if (encoder->next_encode.va_buffer_handle) {
			/* Shared memory surface. */
			ias_hmi_release_buffer_handle(encoder->hmi,
				encoder->next_encode.shm_surf_id,
				encoder->next_encode.buf_id,
				encoder->next_encode.image_id,
				encoder->surfid, 0);
		} else {
			close(encoder->next_encode.prime_fd);
			encoder->next_encode.prime_fd = -1;
			if (encoder->surfid) {
				/* Wayland buffer surface. */
				ias_hmi_release_buffer_handle(encoder->hmi, 0, 0, 0,
						encoder->surfid, 0);
			} else {
				/* Full framebuffer. */
				ias_hmi_release_buffer_handle(encoder->hmi, 0, 0, 0, 0,
						encoder->output_number);
			}
		}
	}

	/* Add current frame to queue... */
	if (encoder->verbose > 2) {
		printf("Updating queued buffer...\n");
	}
	encoder->next_encode.prime_fd = prime_fd;
	/* TODO - Once we have a version of mesa that supports
	 * gbm_bo_get_stride_for_plane(), we should send an array of
	 * strides and offsets. */
	encoder->next_encode.stride = stride0;
	encoder->next_encode.va_buffer_handle = va_buffer_handle;
	encoder->next_encode.format = format;
	encoder->next_encode.timestamp = timestamp;
	encoder->next_encode.frame_number = frame_number;
	encoder->next_encode.shm_surf_id = shm_surf_id;
	encoder->next_encode.buf_id = buf_id;
	encoder->next_encode.image_id = image_id;
	encoder->next_encode.valid = 1;
	pthread_cond_signal(&encoder->encoder_cond);
	pthread_mutex_unlock(&encoder->encoder_mutex);
	return 0;
}


void
rd_encoder_enable_profiling(struct rd_encoder *encoder, int profile_level)
{
	if (encoder) {
		encoder->profile_level = profile_level;
		if (encoder->verbose) {
			printf("Using profile level of %d.\n",
				encoder->profile_level);
		}
	} else {
		fprintf(stderr, "rd_encoder_enable_profiling : No encoder.\n");
	}
}

int
vsync_received(struct rd_encoder *encoder)
{
	if (encoder) {
		return encoder->num_vsyncs;
	} else {
		return 0;
	}
}

void
vsync_notify(struct rd_encoder *encoder)
{
	if (encoder) {
		encoder->num_vsyncs++;
	}
}

void
clear_vsyncs(struct rd_encoder *encoder)
{
	if (encoder) {
		encoder->num_vsyncs = 0;
	}
}
