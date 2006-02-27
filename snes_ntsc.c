
/* snes_ntsc 0.1.0. http://www.slack.net/~ant/ */

/* compilable in C or C++; just change the file extension */

#include "snes_ntsc.h"

#include <assert.h>
#include <string.h>
#include <math.h>

/* Based on algorithm by NewRisingSun */
/* Copyright (C) 2006 Shay Green. Permission is hereby granted, free of
charge, to any person obtaining a copy of this software module and associated
documentation files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and
to permit persons to whom the Software is furnished to do so, subject to the
following conditions: The above copyright notice and this permission notice
shall be included in all copies or substantial portions of the Software. THE
SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

/* support compilers without restrict keyword */
#ifndef restrict
	#define restrict
#endif

enum { burst_count = 3 }; /* different burst phases used */
enum { alignment_count = 3 }; /* different pixel alignments with respect to yiq quads */
enum { burst_entry_size = snes_ntsc_entry_size / burst_count };
enum { rgb_kernel_size = burst_entry_size / alignment_count };

/* important to use + and not | since values are signed */
#define MAKE_KRGB( r, g, b ) \
	( ((r + 16) >> 5 << 20) + ((g + 16) >> 5 << 10) + ((b + 16) >> 5) )
static float const rgb_unit = 0x1000;

#define MAKE_KMASK( x ) (((x) << 20) | ((x) << 10) | (x))

/* clamp each RGB component to 0 to 0x7F range (low two bits are trashed) */
#define CLAMP_RGB( io, adj ) {\
	ntsc_rgb_t sub = (io) >> (7 + adj) & MAKE_KMASK( 3 );\
	ntsc_rgb_t clamp = MAKE_KMASK( 0x202 ) - sub;\
	io = ((io) | clamp) & (clamp - sub);\
}

enum { composite_border = 6 };
enum { composite_size = composite_border + 8 + composite_border };
enum { ntsc_kernel_size = composite_size * 2 };

typedef struct ntsc_to_rgb_t
{
	float composite [composite_size];
	float to_rgb [6];
	float decoder_matrix [6];
	float brightness;
	float contrast;
	float sharpness;
	short rgb [composite_size + 1] [3]; /* extra space for sharpen */
	float kernel [ntsc_kernel_size];
} ntsc_to_rgb_t;

static float const pi = 3.14159265358979323846f;

static void rotate_matrix( float const* in, float s, float c, float* out )
{
	int n = 3;
	while ( n-- )
	{
		float i = *in++;
		float q = *in++;
		*out++ = i * c - q * s;
		*out++ = i * s + q * c;
	}
}

static void ntsc_to_rgb_init( ntsc_to_rgb_t* ntsc, snes_ntsc_setup_t const* setup )
{
	static float const to_rgb [6] = { 0.956, 0.621, -0.272, -0.647, -1.105, 1.702 };
	static float const gaussian_factor = 1.0; /* 1 = normal, > 1 reduces echoes of bright objects */
	int i;
	
	/* ranges need to be scaled a bit to avoid pixels overflowing at extremes */
	ntsc->brightness = setup->brightness * (0.4f * rgb_unit) + 1;
	ntsc->contrast = setup->contrast * 0.4f + 1;
	ntsc->sharpness = 1 + (setup->sharpness < 0 ? setup->sharpness * 0.5f : setup->sharpness);
	
	for ( i = 0; i < composite_size; i++ )
		ntsc->composite [i] = 0;
	
	/* Generate gaussian kernel, padded with zero */
	for ( i = 0; i < ntsc_kernel_size; i++ )
		ntsc->kernel [i] = 0;
	for ( i = -composite_border; i <= composite_border; i++ )
		ntsc->kernel [ntsc_kernel_size / 2 + i] = exp( i * i * (-0.03125f * gaussian_factor) );
	
	/* normalize kernel totals of every fourth sample (at all four phases) to 0.5, otherwise
	i/q low-pass will favor one of the four alignments and cause repeating spots */
	for ( i = 0; i < 4; i++ )
	{
		double sum = 0;
		float scale;
		int x;
		for ( x = i; x < ntsc_kernel_size; x += 4 )
			sum += ntsc->kernel [x];
		scale = 0.5 / sum;
		for ( x = i; x < ntsc_kernel_size; x += 4 )
			ntsc->kernel [x] *= scale;
	}
	
	/* adjust decoder matrix */
	{
		float hue = setup->hue * pi;
		float sat = setup->saturation + 1;
		rotate_matrix( to_rgb, sin( hue ) * sat, cos( hue ) * sat, ntsc->decoder_matrix );
	}
	
	memset( ntsc->rgb, 0, sizeof ntsc->rgb );
}

/* Convert NTSC composite signal to RGB, where composite signal contains only four
non-zero samples beginning at offset */
static void ntsc_to_rgb( ntsc_to_rgb_t const* ntsc, int offset, short* out )
{
	float const* kernel = &ntsc->kernel [ntsc_kernel_size / 2 - offset];
	float f0 = ntsc->composite [offset];
	float f1 = ntsc->composite [offset + 1];
	float f2 = ntsc->composite [offset + 2];
	float f3 = ntsc->composite [offset + 3];
	int x = 0;
	while ( x < composite_size )
	{
		#define PIXEL( get_y ) { \
			float i = kernel [ 0] * f0 + kernel [-2] * f2;\
			float q = kernel [-1] * f1 + kernel [-3] * f3;\
			float y = get_y;\
			float r = y + i * ntsc->to_rgb [0] + q * ntsc->to_rgb [1];\
			float g = y + i * ntsc->to_rgb [2] + q * ntsc->to_rgb [3];\
			float b = y + i * ntsc->to_rgb [4] + q * ntsc->to_rgb [5];\
			kernel++;\
			out [0] = (int) r;\
			out [1] = (int) g;\
			out [2] = (int) b;\
			out += 3;\
		}
		
		/* to do: these must be rearranged when changing kernel size (composite_border) */
		PIXEL( i - ntsc->composite [x + 0] )
		PIXEL( q - ntsc->composite [x + 1] )
		PIXEL( ntsc->composite [x + 2] - i )
		PIXEL( ntsc->composite [x + 3] - q )
		x += 4;
		
		#undef PIXEL
	}
}

/* sharpen image using (level-1)/2, level, (level-1)/2 convolution kernel */
static void sharpen( short const* in, float level, int count, short* out )
{
	/* to do: sharpen luma only? */
	int const accuracy = 16;
	int const middle = (int) (level * (1 << accuracy));
	int const side   = (middle - (1 << accuracy)) >> 1;
	
	*out++ = *in++;
	*out++ = *in++;
	*out++ = *in++;
	
	for ( count = (count - 2) * 3; count--; in++ )
		*out++ = (in [0] * middle - in [-3] * side - in [3] * side) >> accuracy;
	
	*out++ = *in++;
	*out++ = *in++;
	*out++ = *in++;
}

/* Generate pixel and capture into table */
static ntsc_rgb_t* gen_pixel( ntsc_to_rgb_t* ntsc, int ntsc_pos, int rgb_pos, ntsc_rgb_t* out )
{
	if ( ntsc->sharpness == 1.0f ) /* optimization only */
	{
		ntsc_to_rgb( ntsc, composite_border + ntsc_pos, ntsc->rgb [0] );
	}
	else
	{
		ntsc_to_rgb( ntsc, composite_border + ntsc_pos, ntsc->rgb [1] );
		sharpen( ntsc->rgb [1], ntsc->sharpness, composite_size, ntsc->rgb [0] );
	}
	
	{
		short const* in = ntsc->rgb [rgb_pos];
		int n = rgb_kernel_size;
		while ( n-- )
		{
			*out++ = MAKE_KRGB( in [0], in [1], in [2] );
			in += 3;
		}
	}
	return out;
}

/* Generate pixel at all burst phases and column alignments */
static void gen_pixel_phases( ntsc_to_rgb_t* ntsc, float y, float ci, float cq, ntsc_rgb_t* out )
{
	static float const burst_phases [burst_count] [2] = { /* 0 deg, -120 deg, -240 deg */
		{0.0, 1.0}, {-0.866025, -0.5}, {0.866025, -0.5}
	};
	
	/* generate for each scanline burst phase */
	int burst;
	for ( burst = 0; burst < burst_count; burst++ )
	{
		/* adjust i, q, and decoder matrix for burst phase */
		float sin_b = burst_phases [burst] [0];
		float cos_b = burst_phases [burst] [1];
		float fi = ci * cos_b - cq * sin_b;
		float fq = ci * sin_b + cq * cos_b;
		rotate_matrix( ntsc->decoder_matrix, sin_b, cos_b, ntsc->to_rgb );
		
		/* generate at the three different column alignments */
		ntsc->composite [composite_border + 0] = fi + y;
		ntsc->composite [composite_border + 1] = fq + y;
		ntsc->composite [composite_border + 2] = (fi - y) * (2 / 3.0f);
		out = gen_pixel( ntsc, 0, 0, out );
		
		ntsc->composite [composite_border + 0] = 0;
		ntsc->composite [composite_border + 1] = 0;
		ntsc->composite [composite_border + 2] = (fi - y) * (1 / 3.0f);
		ntsc->composite [composite_border + 3] = fq - y;
		ntsc->composite [composite_border + 4] = fi + y;
		ntsc->composite [composite_border + 5] = (fq + y) * (1 / 3.0f);
		out = gen_pixel( ntsc, 2, 2, out );
		
		ntsc->composite [composite_border + 2] = 0;
		ntsc->composite [composite_border + 3] = 0;
		ntsc->composite [composite_border + 4] = 0;
		ntsc->composite [composite_border + 5] = (fq + y) * (2 / 3.0f);
		ntsc->composite [composite_border + 6] = fi - y;
		ntsc->composite [composite_border + 7] = fq - y;
		out = gen_pixel( ntsc, 4, 5, out );
		
		/* keep composite clear for next time */
		ntsc->composite [composite_border + 5] = 0;
		ntsc->composite [composite_border + 6] = 0;
		ntsc->composite [composite_border + 7] = 0;
	}
}

/* correct kernel colors and merge burst phases */
static void adjust_kernel( ntsc_rgb_t color, int merge_fields, ntsc_rgb_t* out )
{
	ntsc_rgb_t const bias = MAKE_KMASK( 0x100 );
	
	if ( merge_fields )
	{
		/* convert to offset binary when doing shift to avoid bit leakage */
		ntsc_rgb_t const mask = MAKE_KMASK( 0x1FF );
		int i;
		for ( i = 0; i < burst_entry_size; i++ )
		{
			ntsc_rgb_t* p = &out [i];
			ntsc_rgb_t p0 = p [burst_entry_size * 0];
			ntsc_rgb_t p1 = p [burst_entry_size * 1];
			ntsc_rgb_t p2 = p [burst_entry_size * 2];
			p [burst_entry_size * 0] = ((p0 + p1 + bias) >> 1 & mask) - (bias >> 1);
			p [burst_entry_size * 1] = ((p1 + p2 + bias) >> 1 & mask) - (bias >> 1);
			p [burst_entry_size * 2] = ((p2 + p0 + bias) >> 1 & mask) - (bias >> 1);
		}
	}
	
	/* correct roundoff errors that would cause speckles in solid areas */
	{
		/* shifts preserve the entire 7 bits when clamping (clamp trashes
		low two bits, which doesn't normally matter) */
		int const shift = 2;
		int burst;
		
		/* clamp color as it would be when rendered */
		color = (color + bias) << shift;
		CLAMP_RGB( color, shift );
		color = color >> shift & MAKE_KMASK( 0x7F );
		
		for ( burst = 0; burst < burst_count; burst++ )
		{
			int i;
			for ( i = 0; i < rgb_kernel_size / 2; i++ )
			{
				ntsc_rgb_t first = out [i];
				ntsc_rgb_t raw = bias  + out [((i+14)&15)+16] + out [((i+11)&15)+32] +
				           out [i + 8] + out [  i+6      +16] + out [  i+ 3     +32];
				/* If desired color is close to clamping range and actual color is outside
				it before clamping, multiple error corrections will be necessary. Example:
				desired = 120, actual = 140. Actual is clamped to 127. Error correction
				first time is -7, but this is insufficient. Worst case, desired could be
				126, causing many loops. */
				while ( 1 )
				{
					ntsc_rgb_t clamped = (raw + first) << shift;
					ntsc_rgb_t error;
					CLAMP_RGB( clamped, shift );
					error = color - (clamped >> shift & MAKE_KMASK( 0x7F ));
					if ( !error )
						break;
					first += error;
				}
				
				out [i] = first + bias;
			}
			out += rgb_kernel_size * alignment_count;
		}
	}
}

void snes_ntsc_init( snes_ntsc_t* emu, snes_ntsc_setup_t const* setup )
{
	/* init pixel renderer */
	unsigned long const* bsnes_colortbl;
	float to_float [32];
	int entry;
	ntsc_to_rgb_t ntsc;
	ntsc_to_rgb_init( &ntsc, setup );
	
	/* generate gamma table */
	{
		float gamma = 1 - setup->gamma * (setup->gamma > 0 ? 0.5f : 1.5f);
		int i;
		for ( i = 0; i < 32; i++ )
			to_float [i] = pow( (1 / 31.0f) * i, gamma ) * rgb_unit;
	}
	
	/* generate entries */
	bsnes_colortbl = setup->bsnes_colortbl;
	for ( entry = 0; entry < snes_color_count; entry++ )
	{
		/* get rgb for entry */
		int ir = entry << 1 & 0x1E;
		int ig = entry >> 4 & 0x1F;
		int ib = entry >> 8 & 0x1E;
		/*
		if ( bsnes_colortbl )
		{
			int bgr15 = (entry << 2 & 0x7800) | (entry << 1 & 0x3FE);
			int rgb16 = bsnes_colortbl [bgr15];
			ir = rgb16 >> 11 & 0x1F;
			ig = rgb16 >>  6 & 0x1F;
			ib = rgb16       & 0x1F;
		}
		*/
		
		/* reduce number of significant bits of source color (changes to this
		must be reflectd in the ENTRY macro). I found that clearing the low
		bits of r and b were least notictable (setting them was moreso, and
		modifying green at all was quite noticeable) */
		float r = to_float [ir & ~1];
		float g = to_float [ig     ];
		float b = to_float [ib & ~1];
		
		/* convert to yiq color */
		float y = r * 0.299f + g * 0.587f + b * 0.114f;
		float i = r * 0.596f - g * 0.275f - b * 0.321f;
		float q = r * 0.212f - g * 0.523f + b * 0.311f;
		y = y * ntsc.contrast + ntsc.brightness;
		
		/* build table entries for pixel */
		gen_pixel_phases( &ntsc, y, i, q, emu->table [entry] );
		
		/* determine rgb that ntsc decoder should produce for a solid area of color */
		r = y + i * ntsc.decoder_matrix [0] + q * ntsc.decoder_matrix [1];
		g = y + i * ntsc.decoder_matrix [2] + q * ntsc.decoder_matrix [3];
		b = y + i * ntsc.decoder_matrix [4] + q * ntsc.decoder_matrix [5];
		adjust_kernel( MAKE_KRGB( (int) r, (int) g, (int) b ),
				setup->merge_fields, emu->table [entry] );
	}
	
	/* verify byte order */
	{
		volatile unsigned i = ~0xFF;
		#ifdef BIG_ENDIAN
			/* if this fails, BIG_ENDIAN needs to be #undef'd */
			assert( *(char*) &i != 0 );
		#else
			/* if this fails, BIG_ENDIAN needs to be #define'd */
			assert( *(char*) &i == 0 );
		#endif
	}
}

/* SNES 0BBBBbGG GGGRRRRr -> 00BBBBGG GGGRRRR0 index into table
hopefully compiler doesn't unnecessarily reload n, used twice here */
enum { snes_entry_factor = snes_ntsc_entry_size / 2 * sizeof (ntsc_rgb_t) };
#define ENTRY( n ) (ntsc_rgb_t*) \
	((char*) table + ((n & 0x03FE) | (n >> 1 & 0x3C00)) * snes_entry_factor)

/* final and adj are compile-time constants */

/* in : xxxxxRRR RRxxxxxG GGGGxxxx xBBBBBxx (for adj = 1, things are shifted left by 1 bit)
   out: RRRRRGGG GG0BBBBB RRRRRGGG GG0BBBBB */
#define LO_PIXEL( in, adj ) \
	(in>>(11+adj)&0x0000F800)|(in>>( 6+adj)& 0x000007C0)|(in>>( 2+adj)&0x0000001F)
#define HI_PIXEL( in, adj ) \
	(in<<( 5-adj)&0xF8000000)|(in<<(10-adj)& 0x07C00000)|(in<<(14-adj)&0x001F0000)

#ifdef BIG_ENDIAN
	#define RIGHT_PIXEL LO_PIXEL
	#define LEFT_PIXEL  HI_PIXEL
#else
	#define RIGHT_PIXEL HI_PIXEL
	#define LEFT_PIXEL  LO_PIXEL
#endif

/* Writing pixels singly halved performance when going to video memory on my machine */
#define MAKE_RGB( in, final, adj, rgb ) {\
	CLAMP_RGB( in, adj );\
	if ( final ) rgb |= RIGHT_PIXEL( in, adj );\
	else         rgb  = LEFT_PIXEL(  in, adj );\
}

/* would be easier for user if this took a boolean parameter for hires mode, but I'd
rather not stress the compiler's optimizer with a single huge function */

void snes_ntsc_blit( snes_ntsc_t const* emu, unsigned short const* in, long in_pitch,
		int burst, int width, int height, void* vout, long out_pitch )
{
	int const chunk_count = (unsigned) (width - 12) / 16;
	long const next_in_line = in_pitch - (chunk_count * 6 + 2) * sizeof *in;
	long const next_out_line = out_pitch - chunk_count * (16 * 2);
	ntsc_rgb_t* restrict out = (ntsc_rgb_t*) vout;
	while ( height-- )
	{
		ntsc_rgb_t const* table = &emu->table [0] [burst * burst_entry_size];
		ntsc_rgb_t const* k1 = table; /* black */
		ntsc_rgb_t const* k2 = k1;
		ntsc_rgb_t const* k3 = k1;
		ntsc_rgb_t const* k4 = ENTRY( in [0] );
		ntsc_rgb_t const* k5 = ENTRY( in [1] );
		int n;
		in += 2;
		burst = (burst + 1) % 3;
		
		#define PIXEL( x ) { \
			ntsc_rgb_t raw =\
				k0 [x       ] + k1 [(x+14)%16+16] + k2 [(x+11)%16+32] +\
				k3 [(x+8)%16] + k4 [(x+ 6)%16+16] + k5 [(x+ 3)%16+32];\
			if ( x && !(x & 1) ) out [x/2-1] = rgb;\
			MAKE_RGB( raw, x & 1, 0, rgb )\
		}
		
		for ( n = chunk_count; n; --n )
		{
			ntsc_rgb_t const* k0 = ENTRY( in [0] );
			ntsc_rgb_t rgb;
			PIXEL(  0 );
			PIXEL(  1 );
			k1 = ENTRY( in [1] );
			PIXEL(  2 );
			PIXEL(  3 );
			PIXEL(  4 );
			k2 = ENTRY( in [2] );
			PIXEL(  5 );
			PIXEL(  6 );
			PIXEL(  7 );
			k3 = ENTRY( in [3] );
			PIXEL(  8 );
			PIXEL(  9 );
			k4 = ENTRY( in [4] );
			PIXEL( 10 );
			PIXEL( 11 );
			PIXEL( 12 );
			k5 = ENTRY( in [5] );
			PIXEL( 13 );
			PIXEL( 14 );
			PIXEL( 15 );
			out [7] = rgb;
			in += 6;
			out += 8;
		}
		{
			ntsc_rgb_t const* k0 = ENTRY( in [0] );
			ntsc_rgb_t rgb;
			PIXEL(  0 );
			PIXEL(  1 );
			k1 = ENTRY( in [1] );
			PIXEL(  2 );
			PIXEL(  3 );
			PIXEL(  4 );
			k2 = table; /* black */
			PIXEL(  5 );
			PIXEL(  6 );
			PIXEL(  7 );
			k3 = table;
			PIXEL(  8 );
			PIXEL(  9 );
			k4 = table;
			PIXEL( 10 );
			PIXEL( 11 );
			out [5] = rgb;
		}
		#undef PIXEL
		
		in = (unsigned short*) ((char*) in + next_in_line);
		out = (ntsc_rgb_t*) ((char*) out + next_out_line);
	}
}

void snes_ntsc_blit_hires( snes_ntsc_t const* emu, unsigned short const* in, long in_pitch,
		int burst, int width, int height, void* vout, long out_pitch )
{
	int const chunk_count = (unsigned) (width - 12) / 16;
	long const next_in_line = in_pitch - (chunk_count * 12 + 4) * sizeof *in;
	long const next_out_line = out_pitch - chunk_count * (16 * 2);
	ntsc_rgb_t* restrict out = (ntsc_rgb_t*) vout;
	while ( height-- )
	{
		ntsc_rgb_t const* table = &emu->table [0] [burst * burst_entry_size];
		ntsc_rgb_t const* k1  = table; /* black */
		ntsc_rgb_t const* k2  = k1;
		ntsc_rgb_t const* k3  = k1;
		ntsc_rgb_t const* k4  = k1;
		ntsc_rgb_t const* k5  = k1;
		ntsc_rgb_t const* k6  = k1;
		ntsc_rgb_t const* k7  = k1;
		ntsc_rgb_t const* k8  = ENTRY( in [0] );
		ntsc_rgb_t const* k9  = ENTRY( in [1] );
		ntsc_rgb_t const* k10 = ENTRY( in [2] );
		ntsc_rgb_t const* k11 = ENTRY( in [3] );
		int n;
		in += 4;
		burst = (burst + 1) % 3;
		
		/* I pity the poor processor with less than 16 general-purpose registers */
		#define PIXEL( x ) { \
			ntsc_rgb_t raw =\
					k0 [(x+ 0)%16+ 0] + k2 [(x+14)%16+16] + k4  [(x+11)%16+32]+\
					k6 [(x+ 8)%16+ 0] + k8 [(x+ 6)%16+16] + k10 [(x+ 3)%16+32]+\
					k1 [(x+15)%16+32] + k3 [(x+12)%16+ 0] + k5  [(x+10)%16+16]+\
					k7 [(x+ 7)%16+32] + k9 [(x+ 4)%16+ 0] + k11 [(x+ 2)%16+16];\
			if ( x && !(x & 1) ) out [x/2-1] = rgb;\
			MAKE_RGB( raw, x & 1, 1, rgb )\
		}
		
		for ( n = chunk_count; n; --n )
		{
			ntsc_rgb_t const* k0 = ENTRY( in [0] );
			ntsc_rgb_t rgb;
			PIXEL(  0 );
			k1 = ENTRY( in [1] );
			PIXEL(  1 );
			k2 = ENTRY( in [2] );
			PIXEL(  2 );
			PIXEL(  3 );
			k3 = ENTRY( in [3] );
			PIXEL(  4 );
			k4 = ENTRY( in [4] );
			PIXEL(  5 );
			k5 = ENTRY( in [5] );
			PIXEL(  6 );
			PIXEL(  7 );
			k6 = ENTRY( in [6] );
			PIXEL(  8 );
			k7 = ENTRY( in [7] );
			PIXEL(  9 );
			k8 = ENTRY( in [8] );
			PIXEL( 10 );
			PIXEL( 11 );
			k9 = ENTRY( in [9] );
			PIXEL( 12 );
			k10= ENTRY( in [10] );
			PIXEL( 13 );
			k11= ENTRY( in [11] );
			PIXEL( 14 );
			PIXEL( 15 );
			out [7] = rgb;
			in += 12;
			out += 8;
		}
		{
			ntsc_rgb_t const* k0 = ENTRY( in [0] );
			ntsc_rgb_t rgb;
			PIXEL(  0 );
			k1 = ENTRY( in [1] );
			PIXEL(  1 );
			k2 = ENTRY( in [2] );
			PIXEL(  2 );
			PIXEL(  3 );
			k3 = ENTRY( in [3] );
			PIXEL(  4 );
			k4 = table; /* black */
			PIXEL(  5 );
			k5 = table;
			PIXEL(  6 );
			PIXEL(  7 );
			k6 = table;
			PIXEL(  8 );
			k7 = table;
			PIXEL(  9 );
			k8 = table;
			PIXEL( 10 );
			PIXEL( 11 );
			out [5] = rgb;
		}
		#undef PIXEL
		
		in = (unsigned short*) ((char*) in + next_in_line);
		out = (ntsc_rgb_t*) ((char*) out + next_out_line);
	}
}
