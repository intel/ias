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
/*
 * This file contains a transport plugin for the remote display wayland
 * client. This plugin simply captures the stream of H264 frames to either
 * a single file or a file per frame, according to the options selected
 * by the user.
 */
#include <stdio.h>
#include <wayland-util.h>
#include <libdrm/intel_bufmgr.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/time.h>
#include <signal.h>

#include "../shared/config-parser.h"
#include "../shared/helpers.h"

#include "transport_plugin.h"
#include "debug.h"

int debug_level = DBG_OFF;

#define BENCHMARK_INTERVAL 1
#define TO_Mb(bytes) ((bytes)/1024/1024*8)


struct private_data {
	int verbose;
	int to_file;
	int dump_frames;
	FILE *fp;
	int file_flush;
	int file_mode;
	char *file_path;
	char *frame_path;
	uint32_t benchmark_time, frames, total_stream_size;
	uint32_t frames_cnt;
	uint32_t max_frames;
};

struct private_data *private_data = NULL;


WL_EXPORT int init(int *argc, char **argv, int verbose)
{
	debug_level = verbose;
	private_data = calloc(1, sizeof(*private_data));

	if (!private_data) {
		return(-ENOMEM);
	}

	INFO("Using file remote display transport plugin...\n");

	const struct weston_option options[] = {
		{ WESTON_OPTION_STRING,  "file_path", 0, &private_data->file_path},
		{ WESTON_OPTION_INTEGER, "file", 0, &private_data->to_file},
		{ WESTON_OPTION_INTEGER, "file_mode", 0, &private_data->file_mode},
		{ WESTON_OPTION_INTEGER, "file_flush", 0, &private_data->file_flush},
		{ WESTON_OPTION_STRING,  "frame_path", 0, &private_data->frame_path},
		{ WESTON_OPTION_INTEGER, "frame_files", 0, &private_data->dump_frames},
		{ WESTON_OPTION_UNSIGNED_INTEGER, "max_frames", 0, &private_data->max_frames},
	};

	parse_options(options, ARRAY_LENGTH(options), argc, argv);
	return 0;
}


WL_EXPORT void help(void)
{
	PRINT("\tThe file plugin uses the following parameters:\n");
	PRINT("\t--file_path=<file_path>\t\tset path for saving the captured image stream to a file\n");
	PRINT("\t--file=1\t\t\tappend video frames to <file_path>\n");
	PRINT("\t--file_flush=<0/1>\t\tflush after each frame\n");
	PRINT("\t--file_mode=<mode>\t\tfile mode: 0: rewrite 1: append\n");
	PRINT("\t--frame_path=<frame_path>\tset path to a folder for capture of frames into separate files\n");
	PRINT("\t--frame_files=1\t\t\tdump each frame to a separate numbered file in <frame_path>\n");
	PRINT("\t--max_frames=<max frames>\tStop recording after <max frames>\n");
	PRINT("\n\tNote that if file_path does not include a filename then it will default to 'capture.mp4'.\n");
	PRINT("\n\tFile can be played back using (for example):\n");
	PRINT("\t\"gst-launch-1.0 filesrc location=/var/cap.h264 ! h264parse ! mfxdecode ! mfxsink\"\n");
}


WL_EXPORT int send_frame(drm_intel_bo *drm_bo, int32_t stream_size, uint32_t timestamp)
{
	if (private_data == NULL) {
		ERROR("Invalid pointer to file plugin private data.\n");
		return (-EFAULT);
	}
	if (private_data->verbose) {
		struct timeval tv;
		uint32_t time;
		gettimeofday(&tv, NULL);
		time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
		if (private_data->frames == 0)
			private_data->benchmark_time = time;
		if (time - private_data->benchmark_time >= (BENCHMARK_INTERVAL * 1000)) {
			INFO("%d frames in %d seconds: %f fps, %f Mb written\n",
					private_data->frames,
					BENCHMARK_INTERVAL,
					(float) private_data->frames / BENCHMARK_INTERVAL,
					TO_Mb((float)(private_data->total_stream_size / BENCHMARK_INTERVAL)));
			private_data->benchmark_time = time;
			private_data->frames = 0;
			private_data->total_stream_size = 0;
		}
		private_data->frames++;
		private_data->total_stream_size += stream_size;
	}

	if (private_data->to_file) {
		int count;
		char filepath[PATH_MAX] = {0};
		char last_char;
		if (!private_data->fp) {
			DBG("Processing frame in file plugin...\n");

			if ((private_data->file_path == 0) || (private_data->file_path[0] == 0)) {
				ERROR("No file path provided.\n");
				return (-EINVAL);
			}

			last_char = private_data->file_path[(int)strlen(private_data->file_path)-1];

			strncpy(filepath, private_data->file_path, PATH_MAX);

			if (last_char != '/') {
				/* Filename included in path */
			} else {
				/* Append default filename */
				const unsigned int max_write = PATH_MAX - strlen(filepath) - 1;

				strncat(filepath, "capture.mp4", max_write);
			}
			private_data->fp = fopen(filepath, private_data->file_mode?"ab":"wb");
			if (!private_data->fp) {
				int err = errno;
				ERROR("Failed to open video output file: %s.\n", filepath);
				return err;
			}
			INFO("Writing to %s (mode:%s / flush:%s)\n",
					filepath,
					private_data->file_mode?"ab":"wb",
					private_data->file_flush?"on":"off");
		}

		count = fwrite(drm_bo->virtual, 1, stream_size, private_data->fp);
		if (count != stream_size) {
			ERROR("dumping frame to file. Tried to write "
					"%d bytes, %d bytes actually written.\n", stream_size, count);
		}
		if (private_data->file_flush) {
			fflush(private_data->fp);
		}
	}

	if (private_data->dump_frames) {
		FILE *fp;
		int count;
		static int frame_num;
		char filename[256] = {0};

		if ((private_data->frame_path == 0) || (private_data->frame_path[0] == 0)) {
			ERROR("No frame path provided.\n");
			return (-EINVAL);
		}

		sprintf(filename, "%s/%05d.frame", private_data->frame_path, frame_num++);
		fp = fopen(filename, "wb");
		if (!fp) {
			int err = errno;
			ERROR("Failed to open frames output file: %s\n", filename);
			return err;
		}

		count = fwrite(drm_bo->virtual, 1, stream_size, fp);
		if (count != stream_size) {
			ERROR("dumping single frame to file. Tried to "
					"write %d bytes, %d bytes actually written.\n",
					stream_size, count);
		}
		fclose(fp);
	}
	if (private_data->max_frames) {
		private_data->frames_cnt++;
		if (private_data->frames_cnt >= private_data->max_frames) {
			PRINT("force stop after %u frames\n", private_data->frames_cnt);
			raise(SIGTERM);
		}
	}
	return 0;
}

WL_EXPORT void destroy()
{
	if (private_data) {
		DBG("Freeing file plugin private data...\n");
	}
	if (private_data->fp) {
		fclose(private_data->fp);
	}
	free(private_data);
}
