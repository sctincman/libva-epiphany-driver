/*
 * Copyright (c) 2011 Intel Corporation
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
#include <va/va_dec_jpeg.h>

#include "epiphany_drv_video.h"
#include "epiphany_jpeg.h"

#include "assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define INIT_DRIVER_DATA     struct epiphany_driver_data * const driver_data = (struct epiphany_driver_data *) ctx->pDriverData

//JPEG EOB marker
#define EOB 0xff
#define PAD 0x00

//8 is the magic number!
#define BLKSZ 8
#define A(u) ((u) ? sqrt(0.125) : 0.5)

#define ROFF 0
#define GOFF 1
#define BOFF 2

#define RCHAN(y, cr)     (y + 1.402*(cr - 128))
#define GCHAN(y, cb, cr) (y - 0.3441*(cb-128) - 0.71414*(cr-128))
#define BCHAN(y, cb)     (y + 1.772*(cb-128))

const float pi_8 = PI / 8.0;

void epiphnay_jpeg_decode_entropy(VAHuffmanTableBufferJPEGBaseline *huffman_table,
				  char *mcu_data_in,
				  char *mcu_data_out)
{
    
}

// the resulting matrix needs to be an integer, and be cast accordingly
void epiphany_jpeg_decode_quant(int *quant,
				char *mcu,
				VAIQMatrixBufferJPEGBaseline *iq,
				VAPictureParameterBufferJPEGBaseline *pic_params,
				VASliceParameterBufferJPEGBaseline *slice_params)
{
    //entry-by-entry product
    int i,j;

    for(i=0; i<BLKSZ; i++)
    {
	for(j=0; j<BLKSZ; j++)
	{
	    quant[j*BLKSZ+i] = ((int) mcu[j*BLKSZ+i]) * ((int) iq[j*BLKSZ+i]);
	}
    }
}

void epiphany_jpeg_decode_dct(unsigned char *out,
			      int *in)
{
    unsigned int x,y,u,v;
    int sum;

    //add 1024 to DC coefficent to avoid 64 additions later on
    //in[0] += 1024;

    for(y=0; y<BLKSZ; y++)
    {
	for(x=0; x<BLKSZ; x++)
	{
	    sum = 0; //the last part is to add 128 to shift to unsigned, so why not just acc here first?
	    for(v=0, v<BLKSZ, v++)
	    {
		for(u=0, u<BLKSZ, u++)
		{
		    sum += (int) roundf(A(u) * A(v) * in[v*u + u] * cos(pi_8 * (x + 0.5) * u) * cos(pi_8 * (y + 0.5) * v));
		}
	    }
	    sum += 128;

	    if (sum < 0 )
		sum = 0;
	    else if (sum > 255 )
		sum = 255;

	    out[x*y + x] = (unsigned char) sum;
	}
    }
}
				      

VAStatus epiphany_jpeg_decode_picture(VADriverContextP ctx,
				      struct decode_state *decode_state)
{

    unsigned int i, j, k, x, y;
    unsigned char r,g,b;
    char *slice, *slice_index, *slice_end;
    char *mcu_in, **mcus, *mcu_index;
    int *quant;

    unsigned char *dcts;
    
    VAIQMatrixBufferJPEGBaseline *iq = (VAIQMatrixBufferJPEGBaseline *) decode_state->iq_matrix->buffer;
    VAPictureParameterBufferJPEGBaseline *pic_params = (VAPictureParameterBufferJPEGBaseline *) decode_state->pic_param->buffer;
    VAHuffmanTableBufferJPEGBaseline *huffman_table = (VAHuffmanTableBufferJPEGBaseline *) decode_state->huffman_table->buffer;

    VASliceParameterBufferJPEGBaseline *slice_params;

    mcu_in = malloc(BLKSZ * BLKSZ * sizeof(char));
    mcu_out = malloc(BLKSZ * BLKSZ * sizeof(char));
    quant = malloc(BLKSZ * BLKSZ * sizeof(int));
    dcts = malloc(BLKSZ * BLKSZ * sizeof(unsigned char));

    unsigned char *image = malloc(pic_params->width * pics_params->height * 3 * sizeof(unsigned char));

    //first, split each slice into separate MCUs, pass those to be huffman decompressed
    for (i=0; i < decode_state->num_slices; i++)
    {
	slice_params = (VASliceParameterBufferJPEGBaseline *) decode_state->slice_params[i]->buffer;

	slice = (char *) decode_state->slice_data[i];
	slice_end = slice + slice_params->slice_data_size - 1;
	slice_index = slice + slice_params->slice_data_offset;

	for (j=0; j < slice_params->num_mcus; j++)
	{
	    //read bytes until EOB, if next byte is 0x00, MCU follows, otherwise break
	    slice_index = ( (char *) memccpy(mcu_in, slice_index, EOB, (slice_end - slice_index)));
	    epiphany_jpeg_decode_entropy(huffman_table, mcu_in, mcu_out);

	    if (slice_index[0] == PAD)
		slice_index++;
	    else
		break;

	    epiphany_jpeg_decode_quant(quant, mcu_out, iq, pic_params, slice_params);
	    epiphany_jpeg_decode_dct(dcts, quant);

	    for(y=0; y<BLKSZ; y++)
	    {
		for(x=0; x<BLKSZ; x++)
		{
		    if(pic_params->components[(j%3)]->h_scaling_factor == 2)
		    {
			if(pic_params->components[(j%3)]->v_scaling_factor == 0)
			{
			    image[3*(j*BLKSZ+2*x) + (j%3) + 3*pic_param->width*2*y] = dcts[x+y*BLKSZ];
			    image[3*(j*BLKSZ+2*x+1) + (j%3) + 3*pic_param->width*2*y] = dcts[x+y*BLKSZ];

			    image[3*(j*BLKSZ+2*x) + (j%3) + 3*pic_param->width*2*(y+1)] = dcts[x+y*BLKSZ];
			    image[3*(j*BLKSZ+2*x+1) + (j%3) + 3*pic_param->width*2*(y+1)] = dcts[x+y*BLKSZ];
			}
			else
			{
			    image[3*(j*BLKSZ+2*x) + (j%3) + 3*pic_param->width*y] = dcts[x+y*BLKSZ];
			    image[3*(j*BLKSZ+2*x+1) + (j%3) + 3*pic_param->width*y] = dcts[x+y*BLKSZ];
			}
		    }
		    else
			image[3*(j*BLKSZ+x) + (j%3) + 3*pic_param->width*y] = dcts[x+y*BLKSZ];
		}
	    }    
	}

	assert(j == slice_params->num_mcus);
	if (j < slice_params->num_mcus)
	    epiphany__error_message("JPEG decode: Slice MCU content less than stated");

	//convert YCbCr to RGB
	for(j=0; j < (pic_params->width * pics_params->height); j++)
	{
	    r = RCHAN(image[j*3], image[j*3+2]);
	    g = GCHAN(image[j*3], image[j*3+1], image[j*3+2]);
	    b = BCHAN(image[j*3], image[j*3+1]);

	    image[j*3] = r;
	    image[j*3+1] = g;
	    image[j*3+2] = b;
	}
    }

}
