/*
 * Copyright (c) 2007-2009 Intel Corporation. All Rights Reserved.
 * Copyright (c) 2012 Scott Tincman <scintman@gmail.com>. All Rights Reserved.
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

#include "config.h"
#include <va/va_backend.h>
#include "sysdeps.h"

#include "epiphany_drv_video.h"

#include "assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define ASSERT	assert

#define INIT_DRIVER_DATA	struct epiphany_driver_data * const driver_data = (struct epiphany_driver_data *) ctx->pDriverData;

/* Check whether we are rendering to X11 (VA/X11 or VA/GLX API) */
#define IS_VA_X11(ctx) \
    (((ctx)->display_type & VA_DISPLAY_MAJOR_MASK) == VA_DISPLAY_X11)

#define CONFIG(id)  ((object_config_p) object_heap_lookup( &driver_data->config_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))
#define SURFACE(id)	((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))

#define CONFIG_ID_OFFSET		0x01000000
#define CONTEXT_ID_OFFSET		0x02000000
#define SURFACE_ID_OFFSET		0x04000000
#define BUFFER_ID_OFFSET		0x08000000

static void epiphany__error_message(const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "epiphany_drv_video error: ");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

static void epiphany__information_message(const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "epiphany_drv_video: ");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

/* buffer_store management adapted from the intel-driver.
 * This is used to store buffers before sending to the chip
 * Just simple reference counting management
 */

void release_buffer_store(struct buffer_store **ptr)
{
    struct buffer_store *buffer_store = *ptr;

    if (buffer_store == NULL)
        return;

    /* It seesm the buffer_store could track where the data was
     * I should implement something similar
     */ 
    assert(buffer_store->bo || buffer_store->buffer);
    assert(!(buffer_store->bo && buffer_store->buffer));
    buffer_store->ref_count--;
    
    if (buffer_store->ref_count == 0)
    {
        //dri_bo_unreference(buffer_store->bo); //replace w/ equivalent Epiphany proc
        free(buffer_store->buffer);
        buffer_store->bo = NULL; //may be needed for epiphany trans
        buffer_store->buffer = NULL;
        free(buffer_store);
    }

    *ptr = NULL;
}

void reference_buffer_store(struct buffer_store **ptr, 
                            struct buffer_store *buffer_store)
{
    //assert(*ptr == NULL);
    // I saw this too many times, if ptr is already a buffer_store, release first
    if (*ptr)
	release_buffer_store(ptr);

    if (buffer_store) {
        buffer_store->ref_count++;
        *ptr = buffer_store;
    }
}

VAStatus epiphany_QueryConfigProfiles(
		VADriverContextP ctx,
		VAProfile *profile_list,	/* out */
		int *num_profiles			/* out */
	)
{
    int i = 0;

    profile_list[i++] = VAProfileMPEG2Simple;
    profile_list[i++] = VAProfileMPEG2Main;
    profile_list[i++] = VAProfileMPEG4Simple;
    profile_list[i++] = VAProfileMPEG4AdvancedSimple;
    profile_list[i++] = VAProfileMPEG4Main;
    profile_list[i++] = VAProfileH264Baseline;
    profile_list[i++] = VAProfileH264Main;
    profile_list[i++] = VAProfileH264High;
    profile_list[i++] = VAProfileVC1Simple;
    profile_list[i++] = VAProfileVC1Main;
    profile_list[i++] = VAProfileVC1Advanced;
    profile_list[i++] = VAProfileJPEGBaseline;

    /* If the assert fails then EPIPHANY_MAX_PROFILES needs to be bigger */
    ASSERT(i <= EPIPHANY_MAX_PROFILES);
    *num_profiles = i;

    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_QueryConfigEntrypoints(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint  *entrypoint_list,	/* out */
		int *num_entrypoints		/* out */
	)
{
    switch (profile) {
        case VAProfileMPEG2Simple:
        case VAProfileMPEG2Main:
                *num_entrypoints = 2;
                entrypoint_list[0] = VAEntrypointVLD;
                entrypoint_list[1] = VAEntrypointMoComp;
                break;

        case VAProfileMPEG4Simple:
        case VAProfileMPEG4AdvancedSimple:
        case VAProfileMPEG4Main:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        case VAProfileH264Baseline:
        case VAProfileH264Main:
        case VAProfileH264High:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        case VAProfileVC1Simple:
        case VAProfileVC1Main:
        case VAProfileVC1Advanced:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        case VAProfileJPEGBaseline:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        default:
                *num_entrypoints = 0;
                break;
    }

    /* If the assert fails then EPIPHANY_MAX_ENTRYPOINTS needs to be bigger */
    ASSERT(*num_entrypoints <= EPIPHANY_MAX_ENTRYPOINTS);
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_GetConfigAttributes(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint entrypoint,
		VAConfigAttrib *attrib_list,	/* in/out */
		int num_attribs
	)
{
    int i;

    /* Other attributes don't seem to be defined */
    /* What to do if we don't know the attribute? */
    for (i = 0; i < num_attribs; i++)
    {
        switch (attrib_list[i].type)
        {
          case VAConfigAttribRTFormat:
              attrib_list[i].value = VA_RT_FORMAT_YUV420;
              break;

          default:
              /* Do nothing */
              attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
              break;
        }
    }

    return VA_STATUS_SUCCESS;
}

static VAStatus epiphany__update_attribute(object_config_p obj_config, VAConfigAttrib *attrib)
{
    int i;
    /* Check existing attrbiutes */
    for(i = 0; obj_config->attrib_count < i; i++)
    {
        if (obj_config->attrib_list[i].type == attrib->type)
        {
            /* Update existing attribute */
            obj_config->attrib_list[i].value = attrib->value;
            return VA_STATUS_SUCCESS;
        }
    }
    if (obj_config->attrib_count < EPIPHANY_MAX_CONFIG_ATTRIBUTES)
    {
        i = obj_config->attrib_count;
        obj_config->attrib_list[i].type = attrib->type;
        obj_config->attrib_list[i].value = attrib->value;
        obj_config->attrib_count++;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus epiphany_CreateConfig(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint entrypoint,
		VAConfigAttrib *attrib_list,
		int num_attribs,
		VAConfigID *config_id		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus;
    int configID;
    object_config_p obj_config;
    int i;

    /* Validate profile & entrypoint */
    switch (profile) {
        case VAProfileMPEG2Simple:
        case VAProfileMPEG2Main:
                if ((VAEntrypointVLD == entrypoint) ||
                    (VAEntrypointMoComp == entrypoint))
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileMPEG4Simple:
        case VAProfileMPEG4AdvancedSimple:
        case VAProfileMPEG4Main:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileH264Baseline:
        case VAProfileH264Main:
        case VAProfileH264High:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileVC1Simple:
        case VAProfileVC1Main:
        case VAProfileVC1Advanced:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileJPEGBaseline:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        default:
                vaStatus = VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
                break;
    }

    if (VA_STATUS_SUCCESS != vaStatus)
    {
        return vaStatus;
    }

    configID = object_heap_allocate( &driver_data->config_heap );
    obj_config = CONFIG(configID);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }

    obj_config->profile = profile;
    obj_config->entrypoint = entrypoint;
    obj_config->attrib_list[0].type = VAConfigAttribRTFormat;
    obj_config->attrib_list[0].value = VA_RT_FORMAT_YUV420;
    obj_config->attrib_count = 1;

    for(i = 0; i < num_attribs; i++)
    {
        vaStatus = epiphany__update_attribute(obj_config, &(attrib_list[i]));
        if (VA_STATUS_SUCCESS != vaStatus)
        {
            break;
        }
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        object_heap_free( &driver_data->config_heap, (object_base_p) obj_config);
    }
    else
    {
        *config_id = configID;
    }

    return vaStatus;
}

VAStatus epiphany_DestroyConfig(
		VADriverContextP ctx,
		VAConfigID config_id
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus;
    object_config_p obj_config;

    obj_config = CONFIG(config_id);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        return vaStatus;
    }

    object_heap_free( &driver_data->config_heap, (object_base_p) obj_config);
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_QueryConfigAttributes(
		VADriverContextP ctx,
		VAConfigID config_id,
		VAProfile *profile,		/* out */
		VAEntrypoint *entrypoint, 	/* out */
		VAConfigAttrib *attrib_list,	/* out */
		int *num_attribs		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int i;

    obj_config = CONFIG(config_id);
    ASSERT(obj_config);

    *profile = obj_config->profile;
    *entrypoint = obj_config->entrypoint;
    *num_attribs =  obj_config->attrib_count;
    for(i = 0; i < obj_config->attrib_count; i++)
    {
        attrib_list[i] = obj_config->attrib_list[i];
    }

    return vaStatus;
}

VAStatus epiphany_CreateSurfaces(
		VADriverContextP ctx,
		int width,
		int height,
		int format,
		int num_surfaces,
		VASurfaceID *surfaces		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int i;

    /* We only support one format */
    if (VA_RT_FORMAT_YUV420 != format)
    {
        return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
    }

    for (i = 0; i < num_surfaces; i++)
    {
        int surfaceID = object_heap_allocate( &driver_data->surface_heap );
        object_surface_p obj_surface = SURFACE(surfaceID);
        if (NULL == obj_surface)
        {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            break;
        }
        obj_surface->surface_id = surfaceID;
        surfaces[i] = surfaceID;

	/* From the intel-driver, may be necessary */

        obj_surface->status = VASurfaceReady;
        obj_surface->subpic = VA_INVALID_ID;
        obj_surface->orig_width = width;
        obj_surface->orig_height = height;
	/* The intel driver padded for some reason, I can likely get rid of "orig_"*/
	obj_surface->width = width;
	obj_surface->height = height;

        obj_surface->flags = 0;
        obj_surface->fourcc = 0;
        obj_surface->bo = NULL;
        obj_surface->locked_image_id = VA_INVALID_ID;
        obj_surface->subsampling = 0;
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        /* surfaces[i-1] was the last successful allocation */
        for(; i--; )
        {
            object_surface_p obj_surface = SURFACE(surfaces[i]);
            surfaces[i] = VA_INVALID_SURFACE;
            ASSERT(obj_surface);
            object_heap_free( &driver_data->surface_heap, (object_base_p) obj_surface);
        }
    }

    return vaStatus;
}

VAStatus epiphany_DestroySurfaces(
		VADriverContextP ctx,
		VASurfaceID *surface_list,
		int num_surfaces
	)
{
    INIT_DRIVER_DATA
    int i;
    for(i = num_surfaces; i--; )
    {
        object_surface_p obj_surface = SURFACE(surface_list[i]);
        ASSERT(obj_surface);
        object_heap_free( &driver_data->surface_heap, (object_base_p) obj_surface);
    }
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_QueryImageFormats(
	VADriverContextP ctx,
	VAImageFormat *format_list,        /* out */
	int *num_formats           /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_CreateImage(
	VADriverContextP ctx,
	VAImageFormat *format,
	int width,
	int height,
	VAImage *image     /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_DeriveImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	VAImage *image     /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_DestroyImage(
	VADriverContextP ctx,
	VAImageID image
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_SetImagePalette(
	VADriverContextP ctx,
	VAImageID image,
	unsigned char *palette
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_GetImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	int x,     /* coordinates of the upper left source pixel */
	int y,
	unsigned int width, /* width and height of the region */
	unsigned int height,
	VAImageID image
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}


VAStatus epiphany_PutImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	VAImageID image,
	int src_x,
	int src_y,
	unsigned int src_width,
	unsigned int src_height,
	int dest_x,
	int dest_y,
	unsigned int dest_width,
	unsigned int dest_height
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_QuerySubpictureFormats(
	VADriverContextP ctx,
	VAImageFormat *format_list,        /* out */
	unsigned int *flags,       /* out */
	unsigned int *num_formats  /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_CreateSubpicture(
	VADriverContextP ctx,
	VAImageID image,
	VASubpictureID *subpicture   /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_DestroySubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_SetSubpictureImage(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        VAImageID image
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_SetSubpicturePalette(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	/*
	 * pointer to an array holding the palette data.  The size of the array is
	 * num_palette_entries * entry_bytes in size.  The order of the components
	 * in the palette is described by the component_order in VASubpicture struct
	 */
	unsigned char *palette
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_SetSubpictureChromakey(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	unsigned int chromakey_min,
	unsigned int chromakey_max,
	unsigned int chromakey_mask
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_SetSubpictureGlobalAlpha(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	float global_alpha 
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}


VAStatus epiphany_AssociateSubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	VASurfaceID *target_surfaces,
	int num_surfaces,
	short src_x, /* upper left offset in subpicture */
	short src_y,
	unsigned short src_width,
	unsigned short src_height,
	short dest_x, /* upper left offset in surface */
	short dest_y,
	unsigned short dest_width,
	unsigned short dest_height,
	/*
	 * whether to enable chroma-keying or global-alpha
	 * see VA_SUBPICTURE_XXX values
	 */
	unsigned int flags
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_DeassociateSubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	VASurfaceID *target_surfaces,
	int num_surfaces
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_CreateContext(
		VADriverContextP ctx,
		VAConfigID config_id,
		int picture_width,
		int picture_height,
		int flag,
		VASurfaceID *render_targets,
		int num_render_targets,
		VAContextID *context		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int i;

    obj_config = CONFIG(config_id);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        return vaStatus;
    }

    /* Validate flag */
    /* Validate picture dimensions */

    int contextID = object_heap_allocate( &driver_data->context_heap );
    object_context_p obj_context = CONTEXT(contextID);
    if (NULL == obj_context)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }

    obj_context->context_id  = contextID;
    *context = contextID;
    obj_context->current_render_target = -1;
    obj_context->config_id = config_id;
    obj_context->picture_width = picture_width;
    obj_context->picture_height = picture_height;
    obj_context->num_render_targets = num_render_targets;
    obj_context->render_targets = (VASurfaceID *) malloc(num_render_targets * sizeof(VASurfaceID));
    if (obj_context->render_targets == NULL)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }
    
    for(i = 0; i < num_render_targets; i++)
    {
        if (NULL == SURFACE(render_targets[i]))
        {
            vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
            break;
        }
        obj_context->render_targets[i] = render_targets[i];
    }

    if (vaStatus == VA_STATUS_SUCCESS)
    {
        if (VAEntrypointEncSlice == obj_config->entrypoint ) /*encode routin only*/
	{
            obj_context->codec_type = CODEC_ENC;
            memset(&obj_context->codec_state.encode, 0, sizeof(obj_context->codec_state.encode));
            obj_context->codec_state.encode.current_render_target = VA_INVALID_ID;
            obj_context->codec_state.encode.max_slice_params = NUM_SLICES;
            obj_context->codec_state.encode.slice_params = calloc(obj_context->codec_state.encode.max_slice_params,
                                                               sizeof(*obj_context->codec_state.encode.slice_params));
        }
	else
	{
            obj_context->codec_type = CODEC_DEC;
            memset(&obj_context->codec_state.decode, 0, sizeof(obj_context->codec_state.decode));
            obj_context->codec_state.decode.current_render_target = -1;
            obj_context->codec_state.decode.max_slice_params = NUM_SLICES;
            obj_context->codec_state.decode.max_slice_data = NUM_SLICES;
            obj_context->codec_state.decode.slice_params = calloc(obj_context->codec_state.decode.max_slice_params,
                                                               sizeof(*obj_context->codec_state.decode.slice_params));
            obj_context->codec_state.decode.slice_data = calloc(obj_context->codec_state.decode.max_slice_data,
                                                              sizeof(*obj_context->codec_state.decode.slice_data));
        }
    }

    obj_context->flags = flag;

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        obj_context->context_id = -1;
        obj_context->config_id = -1;
        free(obj_context->render_targets);
        obj_context->render_targets = NULL;
        obj_context->num_render_targets = 0;
        obj_context->flags = 0;
        object_heap_free( &driver_data->context_heap, (object_base_p) obj_context);
    }

    return vaStatus;
}

VAStatus epiphany_destroy_context(struct object_heap *heap, struct object_base *obj)
{
    struct object_context *obj_context = (struct object_context *)obj;
    int i;

    obj_context->context_id = -1;
    obj_context->config_id = -1;
    obj_context->picture_width = 0;
    obj_context->picture_height = 0;
    if (obj_context->render_targets)
    {
        free(obj_context->render_targets);
    }
    obj_context->render_targets = NULL;
    obj_context->num_render_targets = 0;
    obj_context->flags = 0;

    obj_context->current_render_target = -1;

    if (obj_context->codec_type == CODEC_ENC)
    {
        assert(obj_context->codec_state.encode.num_slice_params <= obj_context->codec_state.encode.max_slice_params);
        release_buffer_store(&obj_context->codec_state.encode.pic_param);
        release_buffer_store(&obj_context->codec_state.encode.seq_param);

	/* Why didn't the intel driver free these? */
        release_buffer_store(&obj_context->codec_state.encode.pic_control);
        release_buffer_store(&obj_context->codec_state.encode.iq_matrix);
        release_buffer_store(&obj_context->codec_state.encode.q_matrix);

        for (i = 0; i < obj_context->codec_state.encode.num_slice_params; i++)
            release_buffer_store(&obj_context->codec_state.encode.slice_params[i]);

        free(obj_context->codec_state.encode.slice_params);
    }
    else
    {
        assert(obj_context->codec_state.decode.num_slice_params <= obj_context->codec_state.decode.max_slice_params);
        assert(obj_context->codec_state.decode.num_slice_data <= obj_context->codec_state.decode.max_slice_data);

        release_buffer_store(&obj_context->codec_state.decode.pic_param);
        release_buffer_store(&obj_context->codec_state.decode.iq_matrix);
        release_buffer_store(&obj_context->codec_state.decode.bit_plane);

	/* and again here, why not free this? */
	release_buffer_store(&obj_context->codec_state.decode.huffman_table);

        for (i = 0; i < obj_context->codec_state.decode.num_slice_params; i++)
            release_buffer_store(&obj_context->codec_state.decode.slice_params[i]);

        for (i = 0; i < obj_context->codec_state.decode.num_slice_data; i++)
            release_buffer_store(&obj_context->codec_state.decode.slice_data[i]);

        free(obj_context->codec_state.decode.slice_params);
        free(obj_context->codec_state.decode.slice_data);
    }

    object_heap_free(heap, (object_base_p) obj_context);

    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_DestroyContext(VADriverContextP ctx,
				 VAContextID context)
{
    INIT_DRIVER_DATA
    object_context_p obj_context = CONTEXT(context);
    ASSERT(obj_context);
    return epiphany_destroy_context(&driver_data->context_heap, (struct object_base *)obj_context);
}

VAStatus epiphany_CreateBuffer(
		VADriverContextP ctx,
                VAContextID context,	/* in */
                VABufferType type,	/* in */
                unsigned int size,		/* in */
                unsigned int num_elements,	/* in */
                void *data,			/* in */
                VABufferID *buf_id		/* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int bufferID;
    object_buffer_p obj_buffer;

    /* Validate type */
    switch (type)
    {
        case VAPictureParameterBufferType:
        case VAIQMatrixBufferType:
        case VABitPlaneBufferType:
        case VASliceGroupMapBufferType:
        case VASliceParameterBufferType:
        case VASliceDataBufferType:
        case VAMacroblockParameterBufferType:
        case VAResidualDataBufferType:
        case VADeblockingParameterBufferType:
        case VAImageBufferType:
        case VAProtectedSliceDataBufferType:
        case VAQMatrixBufferType:
        case VAHuffmanTableBufferType:
            /* Ok */
            break;
        default:
            vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
            return vaStatus;
    }

    bufferID = object_heap_allocate( &driver_data->buffer_heap );
    obj_buffer = BUFFER(bufferID);
    if (NULL == obj_buffer)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }

    /* Allocate the buffer_store (setting ref count to 1)
     * As this seems the only time this happens, no need for a separate func, but may be split off if necessary
     */
    obj_buffer->buffer_store = malloc(sizeof(struct buffer_store));
    assert(obj_buffer->buffer_store);

    if (NULL == obj_buffer->buffer_store)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    else
    {
	obj_buffer->buffer_store->buffer = calloc(num_elements, size);
	obj_buffer->buffer_store->ref_count = 1;
	obj_buffer->buffer_store->bo = NULL;
	/* this is in the parent object_buffer... needed? and if needed, why no size_t? */
	obj_buffer->buffer_store->num_elements = num_elements;

	assert(obj_buffer->buffer_store->buffer);
	if(obj_buffer->buffer_store->buffer && data)
	    memcpy(obj_buffer->buffer_store->buffer, data, size * num_elements);
    }

    /* End buffer_store allocation */

    if (VA_STATUS_SUCCESS == vaStatus)
    {
        obj_buffer->max_num_elements = num_elements;
        obj_buffer->num_elements = num_elements;
	obj_buffer->element_size = size;
	obj_buffer->type = type;

        *buf_id = bufferID;
    }

    return vaStatus;
}


VAStatus epiphany_BufferSetNumElements(
		VADriverContextP ctx,
		VABufferID buf_id,	/* in */
        unsigned int num_elements	/* in */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    ASSERT(obj_buffer);

    if ((num_elements < 0) || (num_elements > obj_buffer->max_num_elements))
    {
        vaStatus = VA_STATUS_ERROR_UNKNOWN;
    }
    if (VA_STATUS_SUCCESS == vaStatus)
    {
        obj_buffer->num_elements = num_elements;
	if (obj_buffer->buffer_store)
	    obj_buffer->buffer_store->num_elements = num_elements;
    }

    return vaStatus;
}

/* This needs to be updated when I work out how to map to the chip
 * For now just do "soft" mapping
 */
VAStatus epiphany_MapBuffer(
		VADriverContextP ctx,
		VABufferID buf_id,	/* in */
		void **pbuf         /* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    ASSERT(obj_buffer);
    if (NULL == obj_buffer)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        return vaStatus;
    }

    if (obj_buffer->buffer_store && obj_buffer->buffer_store->buffer)
    {
	*pbuf = obj_buffer->buffer_store->buffer;
	vaStatus = VA_STATUS_SUCCESS;
    }
    else
    {
	return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    return vaStatus;
}

VAStatus epiphany_UnmapBuffer(
		VADriverContextP ctx,
		VABufferID buf_id	/* in */
	)
{
    /* Do nothing for now...*/
    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_DestroyBuffer(
		VADriverContextP ctx,
		VABufferID buffer_id
	)
{
    INIT_DRIVER_DATA
    object_buffer_p obj_buffer = BUFFER(buffer_id);
    ASSERT(obj_buffer);

    assert(obj_buffer->buffer_store);
    release_buffer_store(&obj_buffer->buffer_store);
    object_heap_free(&driver_data->buffer_heap, (object_base_p) obj_buffer);

    return VA_STATUS_SUCCESS;
}

VAStatus epiphany_BeginPicture(
		VADriverContextP ctx,
		VAContextID context,
		VASurfaceID render_target
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;

    /* Intel checks profile here, but do we need to hold people's hands?
     * If so, make a proc to do so... since they do it multiple times.
     */

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    obj_context->current_render_target = obj_surface->base.id;

    /* Intel releases the codec_state here manually...
     * BeginPicture starts a new codec_state, so 
     */

    return vaStatus;
}

VAStatus epiphany_RenderPicture(
		VADriverContextP ctx,
		VAContextID context,
		VABufferID *buffers,
		int num_buffers
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;
    object_config_p obj_config;
    object_buffer_p obj_buffer;
    int i;

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    VAConfigID config = obj_context->config_id;
    obj_config = CONFIG(config);
    assert(obj_config);

    obj_surface = SURFACE(obj_context->current_render_target);
    ASSERT(obj_surface);

    /*
    if (obj_context->codec_type == CODEC_ENC)
    {
	struct encode_state *codec_state = &obj_context->codec_state.encode;
    }
    else
    {
	struct decode_state *codec_state = &obj_context->codec_state.decode;
    }
    */

    union codec_state *codec_state = &obj_context->codec_state;

    for(i = 0; i < num_buffers; i++)
    {
        obj_buffer = BUFFER(buffers[i]);
        assert(obj_buffer);
	assert(obj_buffer->buffer_store);
	assert(obj_buffer->buffer_store->buffer);
        if (NULL == obj_buffer)
        {
            vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
            break;
        }

	switch(obj_buffer->type)
	{
	case VAEncSequenceParameterBufferType:
	    reference_buffer_store(&codec_state->encode.seq_param, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
	    break;
	    
        case VAEncPictureParameterBufferType:
	    reference_buffer_store(&codec_state->encode.pic_param, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
	    break;

        case VAPictureParameterBufferType:
	    /* Ugh, this means different things for enc/decode... */
	    if (obj_context->codec_type == CODEC_ENC)
		reference_buffer_store(&codec_state->encode.pic_control, obj_buffer->buffer_store);
	    else
		reference_buffer_store(&codec_state->decode.pic_param, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
            break;		
	    
        case VAEncSliceParameterBufferType:
        case VASliceParameterBufferType:
	    if (codec_state->encode.num_slice_params == codec_state->encode.max_slice_params)
	    {
		codec_state->encode.slice_params = realloc(codec_state->encode.slice_params, (codec_state->encode.max_slice_params + NUM_SLICES) * sizeof(*codec_state->encode.slice_params));
		memset(codec_state->encode.slice_params + codec_state->encode.max_slice_params, 0, NUM_SLICES * sizeof(*codec_state->encode.slice_params));
		codec_state->encode.max_slice_params += NUM_SLICES;
	    }
	    reference_buffer_store(&codec_state->encode.slice_params[codec_state->encode.num_slice_params], obj_buffer->buffer_store);
	    codec_state->encode.num_slice_params++;
	    vaStatus = VA_STATUS_SUCCESS;
            break;
	    
        case VAQMatrixBufferType:
            reference_buffer_store(&codec_state->encode.q_matrix, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
            break;
	    
        case VAIQMatrixBufferType:
	    reference_buffer_store(&codec_state->encode.iq_matrix, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
            break;

        case VABitPlaneBufferType:
	    reference_buffer_store(&codec_state->decode.bit_plane, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
            break;

        case VASliceDataBufferType:
	    if (codec_state->decode.num_slice_data == codec_state->decode.max_slice_data)
	    {
		codec_state->decode.slice_data = realloc(codec_state->decode.slice_data, (codec_state->decode.max_slice_data + NUM_SLICES) * sizeof(*codec_state->decode.slice_data));
		memset(codec_state->decode.slice_data + codec_state->decode.max_slice_data, 0, NUM_SLICES * sizeof(*codec_state->decode.slice_data));
		codec_state->decode.max_slice_data += NUM_SLICES;
	    }
	    reference_buffer_store(&codec_state->decode.slice_data[codec_state->decode.num_slice_data], obj_buffer->buffer_store);
	    codec_state->decode.num_slice_data++;
	    vaStatus = VA_STATUS_SUCCESS;
            break;

        case VAHuffmanTableBufferType:
	    reference_buffer_store(&codec_state->decode.huffman_table, obj_buffer->buffer_store);
	    vaStatus = VA_STATUS_SUCCESS;
            break;

        default:
            vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
            break;
	}

	/* Release client side buffers (the data is our responsibility now, buffer_store) */
	if (vaStatus == VA_STATUS_SUCCESS)
	    epiphany_DestroyBuffer(ctx, buffers[i]);
    }

    return vaStatus;
}

VAStatus epiphany_EndPicture(
		VADriverContextP ctx,
		VAContextID context
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_surface = SURFACE(obj_context->current_render_target);
    ASSERT(obj_surface);

    // This is where we'd decode the picture, using the info stored in the codec_state
    obj_context->current_render_target = -1;

    return vaStatus;
}


VAStatus epiphany_SyncSurface(
		VADriverContextP ctx,
		VASurfaceID render_target
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    return vaStatus;
}

VAStatus epiphany_QuerySurfaceStatus(
		VADriverContextP ctx,
		VASurfaceID render_target,
		VASurfaceStatus *status	/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    *status = VASurfaceReady;

    return vaStatus;
}

VAStatus epiphany_PutSurface(
   		VADriverContextP ctx,
		VASurfaceID surface,
		void *draw, /* X Drawable */
		short srcx,
		short srcy,
		unsigned short srcw,
		unsigned short srch,
		short destx,
		short desty,
		unsigned short destw,
		unsigned short desth,
		VARectangle *cliprects, /* client supplied clip list */
		unsigned int number_cliprects, /* number of clip rects in the clip list */
		unsigned int flags /* de-interlacing flags */
	)
{
#ifdef HAVE_VA_X11
    if (IS_VA_X11(ctx)) {
        VARectangle src_rect, dst_rect;


        src_rect.x      = srcx;
        src_rect.y      = srcy;
        src_rect.width  = srcw;
        src_rect.height = srch;

        dst_rect.x      = destx;
        dst_rect.y      = desty;
        dst_rect.width  = destw;
        dst_rect.height = desth;

        return epiphany_put_surface_dri(ctx, surface, draw, &src_rect, &dst_rect,
                                    cliprects, number_cliprects, flags);
    }
#endif
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

/* 
 * Query display attributes 
 * The caller must provide a "attr_list" array that can hold at
 * least vaMaxNumDisplayAttributes() entries. The actual number of attributes
 * returned in "attr_list" is returned in "num_attributes".
 */
VAStatus epiphany_QueryDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,	/* out */
		int *num_attributes		/* out */
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

/* 
 * Get display attributes 
 * This function returns the current attribute values in "attr_list".
 * Only attributes returned with VA_DISPLAY_ATTRIB_GETTABLE set in the "flags" field
 * from vaQueryDisplayAttributes() can have their values retrieved.  
 */
VAStatus epiphany_GetDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,	/* in/out */
		int num_attributes
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

/* 
 * Set display attributes 
 * Only attributes returned with VA_DISPLAY_ATTRIB_SETTABLE set in the "flags" field
 * from vaQueryDisplayAttributes() can be set.  If the attribute is not settable or 
 * the value is out of range, the function returns VA_STATUS_ERROR_ATTR_NOT_SUPPORTED
 */
VAStatus epiphany_SetDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,
		int num_attributes
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}


VAStatus epiphany_BufferInfo(
        VADriverContextP ctx,
        VABufferID buf_id,	/* in */
        VABufferType *type,	/* out */
        unsigned int *size,    	/* out */
        unsigned int *num_elements /* out */
    )
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

    

VAStatus epiphany_LockSurface(
		VADriverContextP ctx,
		VASurfaceID surface,
                unsigned int *fourcc, /* following are output argument */
                unsigned int *luma_stride,
                unsigned int *chroma_u_stride,
                unsigned int *chroma_v_stride,
                unsigned int *luma_offset,
                unsigned int *chroma_u_offset,
                unsigned int *chroma_v_offset,
                unsigned int *buffer_name,
		void **buffer
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus epiphany_UnlockSurface(
		VADriverContextP ctx,
		VASurfaceID surface
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus epiphany_Terminate( VADriverContextP ctx )
{
    INIT_DRIVER_DATA
    object_buffer_p obj_buffer;
    object_context_p obj_context;
    object_config_p obj_config;
    object_heap_iterator iter;

    /* Clean up left over buffers */
    obj_buffer = (object_buffer_p) object_heap_first( &driver_data->buffer_heap, &iter);
    while (obj_buffer)
    {
        epiphany__information_message("vaTerminate: bufferID %08x still allocated, destroying\n", obj_buffer->base.id);
        release_buffer_store(&obj_buffer->buffer_store);
        obj_buffer = (object_buffer_p) object_heap_next( &driver_data->buffer_heap, &iter);
    }
    object_heap_destroy( &driver_data->buffer_heap );

    object_heap_destroy( &driver_data->surface_heap );

    obj_context = (object_context_p) object_heap_first( &driver_data->context_heap, &iter);
    while (obj_context)
    {
        epiphany__information_message("vaTerminate: contextID %08x still allocated, destroying\n", obj_context->base.id);
	epiphany_destroy_context(&driver_data->context_heap, (struct object_base *)obj_context);
        obj_context = (object_context_p) object_heap_next( &driver_data->context_heap, &iter);
    }
    object_heap_destroy( &driver_data->context_heap );

    /* Clean up configIDs */
    obj_config = (object_config_p) object_heap_first( &driver_data->config_heap, &iter);
    while (obj_config)
    {
        object_heap_free(&driver_data->config_heap, (object_base_p) obj_config);
        obj_config = (object_config_p) object_heap_next( &driver_data->config_heap, &iter);
    }
    object_heap_destroy(&driver_data->config_heap );

    free(ctx->pDriverData);
    ctx->pDriverData = NULL;

    return VA_STATUS_SUCCESS;
}

VAStatus DLL_EXPORT VA_DRIVER_INIT_FUNC(VADriverContextP ctx);

VAStatus VA_DRIVER_INIT_FUNC(  VADriverContextP ctx )
{
    struct VADriverVTable * const vtable = ctx->vtable;
    int result;
    struct epiphany_driver_data *driver_data;

    ctx->version_major = VA_MAJOR_VERSION;
    ctx->version_minor = VA_MINOR_VERSION;
    ctx->max_profiles = EPIPHANY_MAX_PROFILES;
    ctx->max_entrypoints = EPIPHANY_MAX_ENTRYPOINTS;
    ctx->max_attributes = EPIPHANY_MAX_CONFIG_ATTRIBUTES;
    ctx->max_image_formats = EPIPHANY_MAX_IMAGE_FORMATS;
    ctx->max_subpic_formats = EPIPHANY_MAX_SUBPIC_FORMATS;
    ctx->max_display_attributes = EPIPHANY_MAX_DISPLAY_ATTRIBUTES;
    ctx->str_vendor = EPIPHANY_STR_VENDOR;

    vtable->vaTerminate = epiphany_Terminate;
    vtable->vaQueryConfigEntrypoints = epiphany_QueryConfigEntrypoints;
    vtable->vaQueryConfigProfiles = epiphany_QueryConfigProfiles;
    vtable->vaQueryConfigEntrypoints = epiphany_QueryConfigEntrypoints;
    vtable->vaQueryConfigAttributes = epiphany_QueryConfigAttributes;
    vtable->vaCreateConfig = epiphany_CreateConfig;
    vtable->vaDestroyConfig = epiphany_DestroyConfig;
    vtable->vaGetConfigAttributes = epiphany_GetConfigAttributes;
    vtable->vaCreateSurfaces = epiphany_CreateSurfaces;
    vtable->vaDestroySurfaces = epiphany_DestroySurfaces;
    vtable->vaCreateContext = epiphany_CreateContext;
    vtable->vaDestroyContext = epiphany_DestroyContext;
    vtable->vaCreateBuffer = epiphany_CreateBuffer;
    vtable->vaBufferSetNumElements = epiphany_BufferSetNumElements;
    vtable->vaMapBuffer = epiphany_MapBuffer;
    vtable->vaUnmapBuffer = epiphany_UnmapBuffer;
    vtable->vaDestroyBuffer = epiphany_DestroyBuffer;
    vtable->vaBeginPicture = epiphany_BeginPicture;
    vtable->vaRenderPicture = epiphany_RenderPicture;
    vtable->vaEndPicture = epiphany_EndPicture;
    vtable->vaSyncSurface = epiphany_SyncSurface;
    vtable->vaQuerySurfaceStatus = epiphany_QuerySurfaceStatus;
    vtable->vaPutSurface = epiphany_PutSurface;
    vtable->vaQueryImageFormats = epiphany_QueryImageFormats;
    vtable->vaCreateImage = epiphany_CreateImage;
    vtable->vaDeriveImage = epiphany_DeriveImage;
    vtable->vaDestroyImage = epiphany_DestroyImage;
    vtable->vaSetImagePalette = epiphany_SetImagePalette;
    vtable->vaGetImage = epiphany_GetImage;
    vtable->vaPutImage = epiphany_PutImage;
    vtable->vaQuerySubpictureFormats = epiphany_QuerySubpictureFormats;
    vtable->vaCreateSubpicture = epiphany_CreateSubpicture;
    vtable->vaDestroySubpicture = epiphany_DestroySubpicture;
    vtable->vaSetSubpictureImage = epiphany_SetSubpictureImage;
    vtable->vaSetSubpictureChromakey = epiphany_SetSubpictureChromakey;
    vtable->vaSetSubpictureGlobalAlpha = epiphany_SetSubpictureGlobalAlpha;
    vtable->vaAssociateSubpicture = epiphany_AssociateSubpicture;
    vtable->vaDeassociateSubpicture = epiphany_DeassociateSubpicture;
    vtable->vaQueryDisplayAttributes = epiphany_QueryDisplayAttributes;
    vtable->vaGetDisplayAttributes = epiphany_GetDisplayAttributes;
    vtable->vaSetDisplayAttributes = epiphany_SetDisplayAttributes;
    vtable->vaLockSurface = epiphany_LockSurface;
    vtable->vaUnlockSurface = epiphany_UnlockSurface;
    vtable->vaBufferInfo = epiphany_BufferInfo;

    driver_data = (struct epiphany_driver_data *) malloc( sizeof(*driver_data) );
    ctx->pDriverData = (void *) driver_data;

    result = object_heap_init( &driver_data->config_heap, sizeof(struct object_config), CONFIG_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->context_heap, sizeof(struct object_context), CONTEXT_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->surface_heap, sizeof(struct object_surface), SURFACE_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->buffer_heap, sizeof(struct object_buffer), BUFFER_ID_OFFSET );
    ASSERT( result == 0 );

    driver_data->servIP = "127.0.0.1";
    driver_data->loaderPort = 50999;

    return VA_STATUS_SUCCESS;
}

