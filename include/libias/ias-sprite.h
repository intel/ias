/*
 *-----------------------------------------------------------------------------
 * Filename: ias-sprite.h
 *-----------------------------------------------------------------------------
 * Copyright 2013-2018 Intel Corporation
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
 *-----------------------------------------------------------------------------
 * Description:
 *   Sprite plane functions prototypes.
 *-----------------------------------------------------------------------------
 */

#ifndef __IAS_SPRITE_H__
#define __IAS_SPRITE_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#ifdef __cplusplus
extern "C" {
#endif


#define BASE DRM_COMMAND_BASE

#define DRM_IGD_MODE_GETPLANERESOURCES   0x41
#define DRM_IGD_MODE_GETPLANE            0x42
#define DRM_IGD_MODE_SETPLANE            0x43
#define DRM_IGD_MODE_ADDFB2              0x44
#define DRM_IGD_MODE_SET_PIPEBLEND       0x45
#define DRM_IGD_MODE_GET_PIPEBLEND       0x46


#define DRM_IOCTL_IGD_MODE_GETPLANERESOURCES     DRM_IOWR(BASE + DRM_IGD_MODE_GETPLANERESOURCES,\
                        struct drm_mode_get_plane_res)
#define DRM_IOCTL_IGD_MODE_GETPLANE              DRM_IOWR(BASE + DRM_IGD_MODE_GETPLANE,\
                        struct drm_mode_get_plane)
#define DRM_IOCTL_IGD_MODE_SETPLANE              DRM_IOWR(BASE + DRM_IGD_MODE_SETPLANE,\
                        struct drm_mode_set_plane)
#define DRM_IOCTL_IGD_MODE_ADDFB2                DRM_IOWR(BASE + DRM_IGD_MODE_ADDFB2,\
                        struct drm_mode_fb_cmd2)

#define DRM_IOCTL_IGD_SET_PIPEBLEND           	DRM_IOWR(BASE + DRM_IGD_MODE_SET_PIPEBLEND,\
                        struct drm_intel_sprite_pipeblend)
#define DRM_IOCTL_IGD_GET_PIPEBLEND            	DRM_IOWR(BASE + DRM_IGD_MODE_GET_PIPEBLEND,\
                        struct drm_intel_sprite_pipeblend)


/*
 *  *  * Ioctl to query kernel params (EMGD)
 *   *   * I915 definition currently occupies from value 0 up to 17
 *    *    */
#define I915_PARAM_HAS_MULTIPLANE_DRM   30

#define I915_SPRITEFLAG_PIPEBLEND_FBBLENDOVL    0x00000001
#define I915_SPRITEFLAG_PIPEBLEND_CONSTALPHA    0x00000002
#define I915_SPRITEFLAG_PIPEBLEND_ZORDER        0x00000004

/* Set or Get the current color correction info on a given sprite */
/* This MUST be an immediate update because of customer requirements */
struct drm_intel_sprite_pipeblend {
    __u32 plane_id;
    __u32 crtc_id;
        /* I915_SPRITEFLAG_PIPEBLEND_FBBLENDOVL = 0x00000001*/
        /* I915_SPRITEFLAG_PIPEBLEND_CONSTALPHA = 0x00000002*/
        /* I915_SPRITEFLAG_PIPEBLEND_ZORDER     = 0x00000004*/
    __u32 enable_flags;
    __u32 fb_blend_ovl;
    __u32 has_const_alpha;
    __u32 const_alpha; /* 8 LSBs is alpha channel*/
    __u32 zorder_value;
};


#define DRM_GET_PLANE_RESOURCES(ec, drm_fd) ec->private_multiplane_drm?   \
         intel_drm_get_plane_resources(drm_fd):drmModeGetPlaneResources(drm_fd)
#define DRM_GET_PLANE(ec, drm_fd, ovl) ec->private_multiplane_drm?   \
        intel_drm_get_plane(drm_fd, ovl):drmModeGetPlane(drm_fd, ovl)
#define DRM_ADD_FB2(ec, drm_fd, w, h, pf, bo, pitches, offsets, fb_id) ec->private_multiplane_drm?  \
        intel_drm_add_fb2(drm_fd, w, h, pf, bo, pitches, offsets, fb_id):   \
            drmModeAddFB2(drm_fd, w, h, pf, bo, pitches, offsets, fb_id, 0)
#define DRM_SET_PLANE(ec, drm_fd, ovl, crtc, fb, dx, dy, dw, dh, sx, sy, sw, sh) \
         ec->private_multiplane_drm?  \
            intel_drm_set_plane(drm_fd, ovl, crtc, fb, dx, dy, dw, dh, sx, sy, sw, sh):  \
                drmModeSetPlane(drm_fd, ovl, crtc, fb, 0, \
                    dx, dy, dw, dh, sx, sy, sw, sh)
#define DRM_FREE_PLANE(ec, ovl) ec->private_multiplane_drm?intel_drm_free_plane(ovl):drmModeFreePlane(ovl)


int intel_drm_add_fb2(int fd, uint32_t width, uint32_t height,
        uint32_t pixel_format, uint32_t bo_handles[4],
        uint32_t pitches[4], uint32_t offsets[4],
        uint32_t *buf_id);

drmModePlaneResPtr intel_drm_get_plane_resources(int fd);


drmModePlanePtr intel_drm_get_plane(int fd, uint32_t plane_id);


void intel_drm_free_plane(drmModePlanePtr ptr);

int intel_drm_set_plane(int fd, uint32_t plane_id, uint32_t crtc_id,
    uint32_t fb_id, uint32_t crtc_x, uint32_t crtc_y,
    uint32_t crtc_w, uint32_t crtc_h,
    uint32_t src_x, uint32_t src_y,
    uint32_t src_w, uint32_t src_h);

int drm_intel_gem_bo_get_userdata(int fd, int bo_handle, int offset, int bytes, void *data);
int drm_intel_gem_bo_set_userdata(int fd, int bo_handle, int offset, int bytes, void *data);

#ifdef __cplusplus
}
#endif

#endif /* __IAS_SPRITE_H__ */
