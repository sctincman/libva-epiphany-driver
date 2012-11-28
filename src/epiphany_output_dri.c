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
#include <va/va_drmcommon.h>
#include <va/va_dri2.h>

#include "epiphany_drv_video.h"
#include "epiphany_output_dri.h"
#include <X11/Xlib.h>

#include "assert.h"

#define ASSERT assert

#define INIT_DRIVER_DATA	struct epiphany_driver_data * const driver_data = (struct epiphany_driver_data *) ctx->pDriverData;

#define CONFIG(id)  ((object_config_p) object_heap_lookup( &driver_data->config_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))
#define SURFACE(id)	((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))

#define CONFIG_ID_OFFSET		0x01000000
#define CONTEXT_ID_OFFSET		0x02000000
#define SURFACE_ID_OFFSET		0x04000000
#define BUFFER_ID_OFFSET		0x08000000

VAStatus epiphany_put_surface_dri(VADriverContextP ctx,
				  VASurfaceID surface,
				  void               *draw,
				  const VARectangle  *src_rect,
				  const VARectangle  *dst_rect,
				  const VARectangle  *cliprects,
				  unsigned int        num_cliprects,
				  unsigned int        flags
)
{
    INIT_DRIVER_DATA

    struct dri_state *dri_state = (struct dri_state *) ctx->drm_state;
    struct dri_drawable *dri_drawable;
    union dri_buffer *buffer;
    struct object_surface *obj_surface; 

    /* Currently don't support DRI1 */
    char *driver_name = NULL;
    //there's a good chance this does not belong here (isDRI2Connected)
    if (dri_state->base.auth_type != VA_DRM_AUTH_DRI2 && !isDRI2Connected(ctx, &driver_name))
    {
        fprintf(stderr, "epiphany_output_dri error: Not DRI2 (%s)\n", driver_name);
        return VA_STATUS_ERROR_UNIMPLEMENTED;
    }
    fprintf(stderr, "epiphany_output_dri: auth_type %d\n", dri_state->base.auth_type);
    fprintf(stderr, "epiphany_output_dri: fd %d\n", dri_state->base.fd);
    fprintf(stderr, "epiphany_output_dri: driver %s\n", driver_name);

    /* Some broken sources such as H.264 conformance case FM2_SVA_C
     * will get here
     */
    obj_surface = SURFACE(surface);
    if (!obj_surface)
        return VA_STATUS_SUCCESS;

    fprintf(stderr, "epiphany_output_dri: trying to do DRI stuff\n");

    dri_drawable = dri_get_drawable(ctx, (Drawable)draw);
    assert(dri_drawable);

    fprintf(stderr, "epiphany_output_dri: trying to do DRI rendering stuff\n");

    buffer = dri_get_rendering_buffer(ctx, dri_drawable);
    assert(buffer);

    //create the Pixmap
    //this is likely something that should be moved to CreateSurface, possibly? would need to be freed later

    Pixmap source = XCreatePixmapFromBitmapData(ctx->native_dpy, dri_drawable->x_drawable,  (char *) obj_surface->image, obj_surface->width, obj_surface->height, 0, 0, 24);

    fprintf(stderr, "epiphany_output_dri: Made Pixamp (%d)\n", source);
    fwrite(obj_surface->image, sizeof(char), obj_surface->size, stdout);

    //ugh... I hope this is the right way to move the image data
    /*if(drmMap(dri_state->base.fd, buffer->dri2.attachment, obj_surface->size, map))
    {
	fprintf(stderr, "epiphany_output_dri: trying to copy drmMap");
	memcpy(map, obj_surface->image, obj_surface->size);
    }
    else
	fprintf(stderr, "epiphany_output_dri error: Could not map drm buffer %d", buffer->dri2.attachment);
    */

    dri_swap_buffer(ctx, dri_drawable);

    return VA_STATUS_SUCCESS;
}
