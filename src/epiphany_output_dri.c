/*
 * Copyright (C) 2012 Intel Corporation. All Rights Reserved.
 * Copyright (C) 2012 Scott Tincman <sctincman@gmail.com>. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sysdeps.h"
#include <va/va_dricommon.h>
#include "epiphany_drv_video.h"
#include "epiphany_output_dri.h"

#include "assert.h"

#define ASSERT assert

#define INIT_DRIVER_DATA	struct epiphany_driver_data * const driver_data = (struct epiphany_driver_data *) ctx->pDriverData;

VAStatus
epiphany_put_surface_dri(
    VADriverContextP    ctx,
    VASurfaceID         surface,
    void               *draw,
    const VARectangle  *src_rect,
    const VARectangle  *dst_rect,
    const VARectangle  *cliprects,
    unsigned int        num_cliprects,
    unsigned int        flags
)
{
    struct epiphany_driver_data * const epiphany = epiphany_driver_data(ctx); 
    struct epiphany_render_state * const render_state = &driver_data->render_state;
    struct dri_drawable *dri_drawable;
    union dri_buffer *buffer;
    struct intel_region *dest_region;
    struct object_surface *obj_surface; 
    unsigned int pp_flag = 0;
    bool new_region = false;
    uint32_t name;
    int ret;

    /* Currently don't support DRI1 */
    if (!ctx->drm_state->base->auth_type, VA_DRM_AUTH_DRI2)
        return VA_STATUS_ERROR_UNIMPLEMENTED;

    /* Some broken sources such as H.264 conformance case FM2_SVA_C
     * will get here
     */
    obj_surface = SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_SUCCESS;

    dri_drawable = dri_get_drawable(ctx, (Drawable)draw);
    assert(dri_drawable);

    buffer = dri_get_rendering_buffer(ctx, dri_drawable);
    assert(buffer);

    intel_render_put_surface(ctx, surface, src_rect, dst_rect, flags);

    if(obj_surface->subpic != VA_INVALID_ID) {
        intel_render_put_subpicture(ctx, surface, src_rect, dst_rect);
    }

    dri_swap_buffer(ctx, dri_drawable);
    dri_drawable->has_backbuffer = 0;

    return VA_STATUS_SUCCESS;
}
