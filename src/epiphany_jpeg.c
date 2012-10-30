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

#define INIT_DRIVER_DATA     struct epiphany_driver_data * const driver_data = (struct epiphany_driver_data *) ctx->pDriverData;

#define CONFIG(id)  ((object_config_p) object_heap_lookup( &driver_data->config_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))
#define SURFACE(id)	((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))

#define CONFIG_ID_OFFSET		0x01000000
#define CONTEXT_ID_OFFSET		0x02000000
#define SURFACE_ID_OFFSET		0x04000000
#define BUFFER_ID_OFFSET		0x08000000

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

const float pi_8 = M_PI / 8.0;

#define SIGMASK 0x8000
#define LEFTZERO 0xFFFE
#define MSBMASK 0x40

#define NODE 255

struct binary_tree *make_huffman_tree(unsigned char *num_codes,
				      unsigned char *codes)
{
    int i,j;
    int k = 11;
    int num_parents, num_children;
    for(i=15; i>=0 && num_codes[i] == 0; i--);

    struct binary_tree **children;
    struct binary_tree **parents = NULL;

    num_children = num_codes[i];
    num_parents = (num_children + (num_children % 2))/2;

    children = calloc(num_children + (num_children % 2), sizeof(struct binary_tree *));
    if ( (num_children%2) == 1)
	children[num_children] = NULL;

    for(i=i-1; i>=0; i--)
    {
	for(j=0; j<num_children && k>=0; j++)
	{
	    children[j] = malloc(sizeof(struct binary_tree));

	    children[j]->value = codes[k];
	    children[j]->left = NULL;
	    children[j]->right = NULL;
	    k--;
	}

	parents = calloc(num_parents, sizeof(struct binary_tree));

	for(j=0; j<num_parents; j++)
	{
	    parents[j]->left = children[2*j];
	    parents[j]->right = children[2*j+1];
	    parents[j]->value = NODE;
	}

	free(children);

	num_children = num_codes[i];
	children = calloc(num_parents+num_children, sizeof(struct binary_tree *));
	for(j=0; j<num_parents; j++)
	    children[j+num_children] = parents[j];

	num_parents = (num_parents+num_children)/2;
	
    }

    free(children);

    if(parents)
	return parents[0];
    else
	return NULL;
}

unsigned char walk_huffman(uint16_t sequence, struct binary_tree *huffman_tree, unsigned short int *counter);

unsigned char walk_huffman(uint16_t sequence,
			   struct binary_tree *huffman_tree,
			   unsigned short int *counter)
{
    if(!huffman_tree->left && !huffman_tree->right)
	return huffman_tree->value;
    else
    {
	if((sequence & SIGMASK) == 0)
	{
	    (*counter)++;
	    return walk_huffman( (sequence<<1) & LEFTZERO, huffman_tree->right, counter);
	}
	else
	{
	    (*counter)++;
	    return walk_huffman( (sequence<<1) & LEFTZERO, huffman_tree->left, counter);
	}
    }
}

void free_huffman(struct binary_tree *huffman_tree)
{
    if(huffman_tree->left)
	free_huffman(huffman_tree->left);
    if(huffman_tree->right)
	free_huffman(huffman_tree->right);
    free(huffman_tree);
}

/*this function is annoying, dealing with variable length bits
 * I brute forced it this time, using a "word" buffer (we should have no codes/add
 * bits over 16bits in length). and shifting the bits as we read the MSB.
 * The entropy encoding seems to use a psuedo-2's compliment signed number
 * (if the MSB is 0, then put an implied sign bit for a bitfield of size i+1.)
 * I'm sure there's a better way to do this, but as I'm not as confident in how C handles
 * signed shorts, sledge hammer it is!
 */
void epiphany_jpeg_decode_entropy(char *mcu_data_in,
				  char *mcu_data_out,
				  struct binary_tree *huff_dc_tree,
				  struct binary_tree *huff_ac_tree)
{
    int i,j = 0;
    int16_t value;
    union word buffer;
    uint16_t counter;
    unsigned char code;
    char temp;

    char *mcu_index = mcu_data_in;

    //DC lookup
    memcpy(&buffer.word, mcu_data_in, sizeof(uint16_t));
    mcu_index += 2;
    code = walk_huffman(buffer.word, huff_dc_tree, &counter);

    //see if we need to read another byte
    if(counter > sizeof(char))
    {
	buffer.bytes.a = buffer.bytes.b;
	counter -= 8;
	buffer.bytes.b = *mcu_index;
	mcu_index++;
    }

    //align to edge to check MSB
    buffer.word = buffer.word << counter;
    if (buffer.bytes.a & MSBMASK)
	value = 0;
    else
	value = -1 * pow(2, code);

    //move bit by bit adding the result to 'value', filling buffer as needed
    for(i=code-1; i>=0; i--)
    {
	if(buffer.bytes.a & MSBMASK)
	    value += pow(2, i);
	buffer.word = buffer.word << 1;
	counter++;
	if(counter == 8)
	{
	    buffer.bytes.b = *mcu_index;
	    mcu_index++;
	    counter = 0;
	}
    }

    mcu_data_out[0] = value;

    //AC lookups
    //loop through a similar approach as above
    for(j=1; j<BLKSZ*BLKSZ; j++)
    {
	//make sure we have a full buffer before sending this off
	temp = buffer.bytes.a;
	buffer.word = buffer.word << (8-counter);
	buffer.bytes.b = *mcu_index;
	buffer.word = buffer.word >> (8-counter);
	buffer.bytes.a = temp;

	code = walk_huffman(buffer.word, huff_ac_tree, &counter);

	if(counter > sizeof(char))
	{
	    buffer.bytes.a = buffer.bytes.b;
	    counter -= 8;
	    buffer.bytes.b = *mcu_index;
	    mcu_index++;
	}
	
	//align to edge to check MSB
	buffer.word = buffer.word << counter;
	if (buffer.bytes.a & MSBMASK)
	    value = 0;
	else
	    value = -1 * pow(2, code);
	
	//move bit by bit, filling buffer as needed
	for(i=code-1; i>=0; i--)
	{
	    if(buffer.bytes.a & MSBMASK)
		value += pow(2, i);
	    buffer.word = buffer.word << 1;
	    counter++;
	    if(counter == 8)
	    {
		buffer.bytes.b = *mcu_index;
		mcu_index++;
		counter = 0;
	    }
	}
	
	mcu_data_out[j] = value;
    }
}

// the resulting matrix needs to be an integer, and be cast accordingly
void epiphany_jpeg_decode_quant(int *quant,
				char *mcu,
				unsigned char *iq,
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
	    sum = 0;
	    for(v=0; v<BLKSZ; v++)
	    {
		for(u=0; u<BLKSZ; u++)
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
				      VAContextID context)
{
    INIT_DRIVER_DATA
    struct object_context *obj_context = CONTEXT(context);
    struct decode_state *decode_state = &obj_context->codec_state.decode;

    struct object_surface *obj_surface = SURFACE(obj_context->current_render_target);
    assert(obj_surface);

    unsigned int i, j, k, x, y;
    unsigned char r,g,b;
    char *slice, *slice_index, *slice_end;
    char *mcu_in, *mcu_out;
    int *quant;
    unsigned int h_scale, v_scale;

    unsigned char *dcts;
    
    VAIQMatrixBufferJPEGBaseline *iq = (VAIQMatrixBufferJPEGBaseline *) decode_state->iq_matrix->buffer;
    VAPictureParameterBufferJPEGBaseline *pic_params = (VAPictureParameterBufferJPEGBaseline *) decode_state->pic_param->buffer;
    VAHuffmanTableBufferJPEGBaseline *huffman_table = (VAHuffmanTableBufferJPEGBaseline *) decode_state->huffman_table->buffer;

    //recontruct all the huffman trees (simple binary tree implementation)
    struct binary_tree *huff_dc_lum, *huff_ac_lum, *huff_dc_chrom, *huff_ac_chrom;
    huff_dc_lum = make_huffman_tree(huffman_table->huffman_table[0].num_dc_codes, huffman_table->huffman_table[0].dc_values);
    huff_ac_lum = make_huffman_tree(huffman_table->huffman_table[0].num_ac_codes, huffman_table->huffman_table[0].ac_values);
    huff_dc_chrom = make_huffman_tree(huffman_table->huffman_table[1].num_dc_codes, huffman_table->huffman_table[1].dc_values);
    huff_ac_chrom = make_huffman_tree(huffman_table->huffman_table[1].num_ac_codes, huffman_table->huffman_table[1].ac_values);

    VASliceParameterBufferJPEGBaseline *slice_params;

    //allocate all our MCU temp variables
    mcu_in = malloc(BLKSZ * BLKSZ * sizeof(char));
    mcu_out = malloc(BLKSZ * BLKSZ * sizeof(char));
    quant = malloc(BLKSZ * BLKSZ * sizeof(int));
    dcts = malloc(BLKSZ * BLKSZ * sizeof(unsigned char));

    //the final image buffer
    //unsigned char *image = malloc(pic_params->width * pics_params->height * 3 * sizeof(unsigned char));
    unsigned char *image = obj_surface->image;

    h_scale = pic_params->components[1].h_sampling_factor;
    v_scale = pic_params->components[1].v_sampling_factor;

    //first, split each slice into separate MCUs, pass those to be huffman decompressed
    for (i=0; i < decode_state->num_slice_data; i++)
    {
	slice_params = (VASliceParameterBufferJPEGBaseline *) decode_state->slice_params[i]->buffer;

	slice = (char *) decode_state->slice_data[i];
	slice_end = slice + slice_params->slice_data_size - 1;
	slice_index = slice + slice_params->slice_data_offset;

	/* work at one block at a time (for epiphany, this would likely be dispatched in 
	 * to a series of cores on the epiphany chip, then read in after they signal completion
	 */
	
	for (j=0; j < slice_params->num_mcus; j++)
	{
	    //determine subsampling, to determine how many lumincance to grab. grab those first
	    for(k=0; k<(h_scale*v_scale); k++)
	    {
		//read bytes until EOB, if next byte is 0x00, MCU follows, otherwise break
		slice_index = ( (char *) memccpy(mcu_in, slice_index, EOB, (slice_end - slice_index)));
		if (slice_index[0] == PAD)
		    slice_index++;

		epiphany_jpeg_decode_entropy(mcu_in, mcu_out, huff_dc_lum, huff_ac_lum);

		epiphany_jpeg_decode_quant(quant, mcu_out, iq->quantiser_table[pic_params->components[0].quantiser_table_selector], pic_params, slice_params);
		epiphany_jpeg_decode_dct(dcts, quant);
		
		for(y=0; y<BLKSZ; y++)
		{
		    for(x=0; x<BLKSZ; x++)
		    {
			image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+x+(k%2)*BLKSZ) + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*y*pic_params->picture_width] = dcts[x+y*BLKSZ];
		    }
		
		}
	    }

	    //perform for each component
	    for(k=1; k<3; k++)
	    {
		//read bytes until EOB, if next byte is 0x00, MCU follows, otherwise break
		slice_index = ( (char *) memccpy(mcu_in, slice_index, EOB, (slice_end - slice_index)));
		if (slice_index[0] == PAD)
		    slice_index++;

		epiphany_jpeg_decode_entropy(mcu_in, mcu_out, huff_dc_chrom, huff_ac_chrom);

		epiphany_jpeg_decode_quant(quant, mcu_out, iq->quantiser_table[pic_params->components[k].quantiser_table_selector], pic_params, slice_params);
		epiphany_jpeg_decode_dct(dcts, quant);
		
		for(y=0; y<BLKSZ; y++)
		{
		    for(x=0; x<BLKSZ; x++)
		    {
			if(h_scale == 2)
			{
			    if(v_scale == 2)
			    {
				image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+(2*x)) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*(2*y)*pic_params->picture_width] = dcts[x+y*BLKSZ];
				image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+(2*x+1)) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*(2*y)*pic_params->picture_width] = dcts[x+y*BLKSZ];
				image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+(2*x)) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*(2*y+1)*pic_params->picture_width] = dcts[x+y*BLKSZ];
				image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+(2*x+1)) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*(2*y+1)*pic_params->picture_width] = dcts[x+y*BLKSZ];
			    }
			    else
			    {
				image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+(2*x)) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*y*pic_params->picture_width] = dcts[x+y*BLKSZ];
				image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+(2*x+1)) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*y*pic_params->picture_width] = dcts[x+y*BLKSZ];
			    }
			}
			else
			    image[(3*((j*BLKSZ*h_scale)%pic_params->picture_width)+x) +k + (3*((j*BLKSZ*h_scale)/pic_params->picture_width)*BLKSZ*v_scale) + 3*y*pic_params->picture_width] = dcts[x+y*BLKSZ];
		    }
		}    
	    }
	}

	assert(j == slice_params->num_mcus);
	if (j < slice_params->num_mcus)
	    fprintf(stderr, "epiphany_jpeg error: Slice MCU count less than stated");

	//convert YCbCr to RGB
	for(j=0; j < (pic_params->picture_width * pic_params->picture_height); j++)
	{
	    r = RCHAN(image[j*3], image[j*3+2]);
	    g = GCHAN(image[j*3], image[j*3+1], image[j*3+2]);
	    b = BCHAN(image[j*3], image[j*3+1]);

	    image[j*3] = r;
	    image[j*3+1] = g;
	    image[j*3+2] = b;
	}
    }

    //cleanup after ourselves
    free(mcu_in);
    free(mcu_out);
    free(quant);
    free(dcts);

    free_huffman(huff_dc_lum);
    free_huffman(huff_ac_lum);
    free_huffman(huff_dc_chrom);
    free_huffman(huff_ac_chrom);

    return VA_STATUS_SUCCESS;
}
