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
};

#endif
