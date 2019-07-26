/*
** Copyright 2017 Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/


#ifndef _IAS_HMI_REG_H_
#define _IAS_HMI_REG_H_

#include <display_wayland.h>

class ias_hmi_reg : public global_wl2 {
	protected:
		struct ias_hmi *hmi;
		struct wl_list surface_list;

	public:
		ias_hmi_reg() { }
		~ias_hmi_reg() { }
		virtual void add_reg();
		virtual void registry_handle_global(void *data,
				struct wl_registry *registry, uint32_t name,
				const char *interface, uint32_t version);
		virtual void registry_handle_global_remove(void *data,
				struct wl_registry *registry, uint32_t name);

		static void ias_hmi_surface_info(void *data,
				struct ias_hmi *hmi,
				uint32_t id,
				const char *name,
				uint32_t zorder,
				int32_t x, int32_t y,
				uint32_t width, uint32_t height,
				uint32_t alpha,
				uint32_t behavior_bits,
				uint32_t pid,
				const char *pname,
				uint32_t dispno,
				uint32_t flipped);
		static void ias_hmi_surface_destroyed(void *data,
				struct ias_hmi *hmi,
				uint32_t id,
				const char *name,
				uint32_t pid,
				const char *pname);
		static void ias_hmi_surface_sharing_info(void *data,
					     struct ias_hmi *ias_hmi,
					     uint32_t id,
					     const char *title,
					     uint32_t shareable,
					     uint32_t pid,
					     const char *pname);
		static void ias_hmi_raw_buffer_handle(void *data,
					  struct ias_hmi *ias_hmi,
					  int32_t handle,
					  uint32_t timestamp,
					  uint32_t frame_number,
					  uint32_t stride0,
					  uint32_t stride1,
					  uint32_t stride2,
					  uint32_t format,
					  uint32_t out_width,
					  uint32_t out_height,
					  uint32_t shm_surf_id,
					  uint32_t buf_id,
					  uint32_t image_id);
		static void ias_hmi_raw_buffer_fd(void *data,
				      struct ias_hmi *ias_hmi,
				      int32_t prime_fd,
				      uint32_t timestamp,
				      uint32_t frame_number,
				      uint32_t stride0,
				      uint32_t stride1,
				      uint32_t stride2,
				      uint32_t format,
				      uint32_t out_width,
				      uint32_t out_height);
		static void ias_hmi_capture_error(void *data,
				      struct ias_hmi *ias_hmi,
				      int32_t pid,
				      int32_t error);
};

#endif
