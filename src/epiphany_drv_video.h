/*
 * Copyright (c) 2007-2009 Intel Corporation. All Rights Reserved.
 * Copyright (c) 2012 Scott Tincman <sctincman@gmail.com>. All Rights Reserved.
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

#ifndef _EPIPHANY_DRV_VIDEO_H_
#define _EPIPHANY_DRV_VIDEO_H_

#include <va/va.h>
#include "object_heap.h"

#define EPIPHANY_MAX_PROFILES			12
#define EPIPHANY_MAX_ENTRYPOINTS		5
#define EPIPHANY_MAX_CONFIG_ATTRIBUTES		10
#define EPIPHANY_MAX_IMAGE_FORMATS		10
#define EPIPHANY_MAX_SUBPIC_FORMATS		4
#define EPIPHANY_MAX_DISPLAY_ATTRIBUTES		4
#define EPIPHANY_STR_VENDOR			"Epiphany Driver 0.1"

//this was in the matmul_host example, need to reference docs why this offset is needed
unsigned int DRAM_BASE = 0x81000000;

struct epiphany_driver_data {
    struct object_heap	config_heap;
    struct object_heap	context_heap;
    struct object_heap	surface_heap;
    struct object_heap	buffer_heap;

    /*
      The necessary data to communicate with the coprocessor
     */
    char *servIP; // IP address, usually 127.0.0.1 for the local e-server
    unsigned short loaderPort; //The port to communicate with eserver
    
};

struct buffer_store
{
    void *buffer;
    void *bo; //update as needed for epiphany
    int ref_count;
    int num_elements;
};

#define NUM_SLICES     10

struct decode_state
{
    struct buffer_store *pic_param;
    struct buffer_store *iq_matrix;
    struct buffer_store **slice_params;
    struct buffer_store *bit_plane;
    struct buffer_store *huffman_table;
    struct buffer_store **slice_data;
    VASurfaceID current_render_target;
    int max_slice_params;
    int num_slice_params;
    int max_slice_data;
    int num_slice_data;
};

struct encode_state
{
    struct buffer_store *pic_param;
    struct buffer_store *iq_matrix;
    struct buffer_store **slice_params;
    struct buffer_store *seq_param;
    struct buffer_store *pic_control;
    struct buffer_store *q_matrix;
    VASurfaceID current_render_target;
    int max_slice_params;
    int num_slice_params;
};

#define CODEC_DEC       0
#define CODEC_ENC       1

union codec_state
{
    struct decode_state decode;
    struct encode_state encode;
};

struct object_config {
    struct object_base base;
    VAProfile profile;
    VAEntrypoint entrypoint;
    VAConfigAttrib attrib_list[EPIPHANY_MAX_CONFIG_ATTRIBUTES];
    int attrib_count;
};

struct object_context {
    struct object_base base;
    VAContextID context_id;
    VAConfigID config_id;
    VASurfaceID current_render_target;
    int picture_width;
    int picture_height;
    int num_render_targets;
    int flags;
    int codec_type;
    union codec_state codec_state;
    VASurfaceID *render_targets;
};

struct object_surface {
    struct object_base base;
    VASurfaceID surface_id;
    VASurfaceStatus status;
    VASubpictureID subpic;
    int width;
    int height;
    int size;
    int orig_width;
    int orig_height;
    int flags;
    unsigned int fourcc;    
    void *bo; //update as/if needed for epiphany
    VAImageID locked_image_id;
    unsigned int subsampling;
    int x_cb_offset;
    int y_cb_offset;
    int x_cr_offset;
    int y_cr_offset;
    int cb_cr_width;
    int cb_cr_height;
    int cb_cr_pitch;
};

struct object_buffer {
    struct object_base base;
    struct buffer_store *buffer_store;
    int max_num_elements;
    int num_elements;
    unsigned int element_size;
    VABufferType type;
};

typedef struct object_config *object_config_p;
typedef struct object_context *object_context_p;
typedef struct object_surface *object_surface_p;
typedef struct object_buffer *object_buffer_p;

#endif /* _EPIPHANY_DRV_VIDEO_H_ */
