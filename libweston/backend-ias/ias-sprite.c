/*
 *-----------------------------------------------------------------------------
 * Filename: ias-sprite.c
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
 *   Sprite plane functions.
 *-----------------------------------------------------------------------------
 */

#include <ias-common.h>
#include "ias-sprite.h"
#include <string.h>
#include <i915_drm.h>

#define U642VOID(x) ((void *)(unsigned long)(x))
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))




static void* drmAllocCpy(void *array, int count, int entry_size)
{
    char *r;
    int i;

    if (!count || !array || !entry_size)
        return 0;

    if (!(r = drmMalloc(count*entry_size)))
        return 0;

    for (i = 0; i < count; i++)
        memcpy(r+(entry_size*i), array+(entry_size*i), entry_size);

    return r;
}

int intel_drm_add_fb2(int fd, uint32_t width, uint32_t height,
        uint32_t pixel_format, uint32_t bo_handles[4],
        uint32_t pitches[4], uint32_t offsets[4],
        uint32_t *buf_id)
{
    struct drm_mode_fb_cmd2 f;
    int ret;


    f.width = width;
    f.height = height;
    f.pixel_format = pixel_format;
    memcpy(f.handles, bo_handles, 4 * sizeof(bo_handles[0]));
    memcpy(f.pitches, pitches, 4 * sizeof(pitches[0]));
    memcpy(f.offsets, offsets, 4 * sizeof(offsets[0]));

    /* Calling the private driver IOCTL instead of the DRM IOCTL */
    if ((ret = drmIoctl(fd, DRM_IOCTL_IGD_MODE_ADDFB2, &f))) {
        IAS_ERROR("add_fb2 returned non-zero failed!");
        return ret;
    }
    *buf_id = f.fb_id;

    return 0;
}


drmModePlaneResPtr intel_drm_get_plane_resources(int fd)
{
    struct drm_mode_get_plane_res res, counts;
    drmModePlaneResPtr r = 0;


retry:
    memset(&res, 0, sizeof(struct drm_mode_get_plane_res));
    if (drmIoctl(fd, DRM_IOCTL_IGD_MODE_GETPLANERESOURCES, &res))
    {
        return 0;
    }

    counts = res;

    if (res.count_planes) {
        res.plane_id_ptr = VOID2U64(drmMalloc(res.count_planes *
                    sizeof(uint32_t)));
        if (!res.plane_id_ptr)
            goto err_allocs;
    }

    if (drmIoctl(fd, DRM_IOCTL_IGD_MODE_GETPLANERESOURCES, &res))
        goto err_allocs;

    if (counts.count_planes < res.count_planes) {
        drmFree(U642VOID(res.plane_id_ptr));
        goto retry;
    }

    if (!(r = drmMalloc(sizeof(*r))))
        goto err_allocs;

    r->count_planes = res.count_planes;
    r->planes = drmAllocCpy(U642VOID(res.plane_id_ptr),
            res.count_planes, sizeof(uint32_t));
    if (res.count_planes && !r->planes) {
        drmFree(r->planes);
        r = 0;
    }

err_allocs:
    drmFree(U642VOID(res.plane_id_ptr));


    return r;
}

drmModePlanePtr intel_drm_get_plane(int fd, uint32_t plane_id)
{
    struct drm_mode_get_plane ovr, counts;
    drmModePlanePtr r = 0;

retry:
    memset(&ovr, 0, sizeof(struct drm_mode_get_plane));
    ovr.plane_id = plane_id;
    if (drmIoctl(fd, DRM_IOCTL_IGD_MODE_GETPLANE, &ovr))
        return 0;

    counts = ovr;

    if (ovr.count_format_types) {
        ovr.format_type_ptr = VOID2U64(drmMalloc(ovr.count_format_types *
                    sizeof(uint32_t)));
        if (!ovr.format_type_ptr)
            goto err_allocs;
    }

    if (drmIoctl(fd, DRM_IOCTL_IGD_MODE_GETPLANE, &ovr))
        goto err_allocs;

    if (counts.count_format_types < ovr.count_format_types) {
        drmFree(U642VOID(ovr.format_type_ptr));
        goto retry;
    }

    if (!(r = drmMalloc(sizeof(*r))))
        goto err_allocs;

    r->count_formats = ovr.count_format_types;
    r->plane_id = ovr.plane_id;
    r->crtc_id = ovr.crtc_id;
    r->fb_id = ovr.fb_id;
    r->possible_crtcs = ovr.possible_crtcs;
    r->gamma_size = ovr.gamma_size;
    r->formats = drmAllocCpy(U642VOID(ovr.format_type_ptr),
            ovr.count_format_types, sizeof(uint32_t));
    if (ovr.count_format_types && !r->formats) {
        drmFree(r->formats);
        r = 0;
    }

err_allocs:
    drmFree(U642VOID(ovr.format_type_ptr));

    return r;
}

void intel_drm_free_plane(drmModePlanePtr ptr)
{
    if (!ptr)
        return;

    free(ptr->formats);
    free(ptr);
}

int intel_drm_set_plane(int fd, uint32_t plane_id, uint32_t crtc_id,
    uint32_t fb_id, uint32_t crtc_x, uint32_t crtc_y,
    uint32_t crtc_w, uint32_t crtc_h,
    uint32_t src_x, uint32_t src_y,
    uint32_t src_w, uint32_t src_h)
{
    struct drm_mode_set_plane s;
    s.plane_id = plane_id;
    s.crtc_id = crtc_id;
    s.fb_id = fb_id;
    s.crtc_x = crtc_x;
    s.crtc_y = crtc_y;
    s.crtc_w = crtc_w;
    s.crtc_h = crtc_h;
    s.src_x = src_x;
    s.src_y = src_y;
    s.src_w = src_w;
    s.src_h = src_h;

    return drmIoctl(fd, DRM_IOCTL_IGD_MODE_SETPLANE, &s);
}

#ifdef DRM_IOCTL_I915_EXT_USERDATA
int
drm_intel_gem_bo_get_userdata(int fd, int bo_handle, int offset, int bytes, void *data)
{
        struct drm_i915_gem_userdata_blk userdata_blk;
        struct i915_ext_ioctl_data ext_ioctl;
        int ret;

        userdata_blk.op = I915_USERDATA_GET_OP;
        userdata_blk.handle = bo_handle;
        userdata_blk.offset = offset;
        userdata_blk.bytes = bytes;
        userdata_blk.flags = 0;
        userdata_blk.data_ptr = (intptr_t)data;

        ext_ioctl.sub_cmd = DRM_IOCTL_I915_EXT_USERDATA;
        ext_ioctl.table = 0;
        ext_ioctl.args_ptr = (intptr_t)&userdata_blk;

        ret = drmIoctl(fd, DRM_IOCTL_I915_EXT_IOCTL, &ext_ioctl);

        return ret;
}

int
drm_intel_gem_bo_set_userdata(int fd, int bo_handle, int offset, int bytes, void *data)
{
        struct drm_i915_gem_userdata_blk userdata_blk;
        struct i915_ext_ioctl_data ext_ioctl;
        int ret;

        userdata_blk.op = I915_USERDATA_SET_OP;
        userdata_blk.handle = bo_handle;
        userdata_blk.offset = offset;
        userdata_blk.bytes = bytes;
        userdata_blk.flags = 0;
        userdata_blk.data_ptr = (intptr_t)data;

        ext_ioctl.sub_cmd = DRM_IOCTL_I915_EXT_USERDATA;
        ext_ioctl.table = 0;
        ext_ioctl.args_ptr = (intptr_t)&userdata_blk;

        ret = drmIoctl(fd, DRM_IOCTL_I915_EXT_IOCTL, &ext_ioctl);

        return ret;
}
#else
int
drm_intel_gem_bo_get_userdata(int fd, int bo_handle, int offset, int bytes, void *data)
{
        return -1;
}

int
drm_intel_gem_bo_set_userdata(int fd, int bo_handle, int offset, int bytes, void *data)
{
        return -1;
}
#endif
