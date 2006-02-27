
/* Uses snes_ntsc to display a raw SNES image on screen with adjustment of
hue and sharpness using mouse. SDL has poor graphics performance, so blitter
will run must faster when using native graphics. Note that image displayed
here is too wide; it should be reduced horizontally to 85.11% of its width. */

#include "snes_ntsc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "SDL.h"

/* Change to 1 to have emulator merge even and odd fields internally,
eliminating flicker on monitors whose refresh isn't 60 Hz */
enum { merge_fields = 0 };

/* SNES pixel buffer size */
enum { snes_width = snes_ntsc_min_in_width };
enum { snes_height = 223 };

/* Output size */
enum { width = snes_ntsc_min_out_width };
enum { height = snes_height * 2 };

/* Utilities */
void fatal_error( const char* str );
void sdl_init( int width, int height, int depth );
void sdl_lock_pixels();
void sdl_display();
int sdl_button_pressed();
static unsigned char* sdl_pixels;
static long sdl_pitch;
static float mouse_x, mouse_y; /* 0.0 to 1.0 */

int main( int argc, char** argv )
{
	/* making setup static conveniently clears all fields to 0 */
	static snes_ntsc_setup_t setup;
	snes_ntsc_t* ntsc;
	int phase = 0;
	
	/* read raw image */
	unsigned short* image = (unsigned short*) malloc( (long) snes_height * snes_width * 2 );
	FILE* file = fopen( "snes.raw", "rb" );
	if ( !image )
		fatal_error( "Out of memory" );
	if ( !file )
		fatal_error( "Couldn't open image file" );
	fread( image, snes_width * 2, snes_height, file );
	fclose( file );
	
	/* allocate memory for snes_ntsc and initialize */
	ntsc = (snes_ntsc_t*) malloc( sizeof (snes_ntsc_t) );
	if ( !ntsc )
		fatal_error( "Out of memory" );
	setup.merge_fields = merge_fields;
	snes_ntsc_init( ntsc, &setup );
	
	/* keep displaying frames until mouse is clicked */
	sdl_init( width, height, 16 ); /* 16-bit RGB pixel buffer */
	while ( !sdl_button_pressed() )
	{
		int i;
		sdl_lock_pixels();
		
		/* toggle phase only when merge_fields is off */
		if ( !merge_fields )
			phase ^= 1;
		
		/* blit snes image, doubled vertically */
		snes_ntsc_blit( ntsc, image, snes_width * 2, phase, width, height / 2,
				(unsigned short*) sdl_pixels, sdl_pitch * 2 );
		
		/* fill in blank scanlines */
		for ( i = 0; i < height; i += 2 )
		{
			unsigned char* line = sdl_pixels + i * sdl_pitch;
			memcpy( line + sdl_pitch, line, width * 2 );
		}
		
		sdl_display();
		
		/* mouse controls saturation and sharpness */
		if ( mouse_x >= 0 )
		{
			setup.saturation = mouse_x * 2 - 1;
			setup.sharpness  = mouse_y * 2 - 1;
			
			snes_ntsc_init( ntsc, &setup );
			mouse_x = -1; /* only call snes_ntsc_init when mouse moves */
		}
	}
	
	free( ntsc );
	
	return 0;
}

/* Utilities */

static SDL_Rect rect;
static SDL_Surface* screen;
static SDL_Surface* surface;
static unsigned long next_time;

void fatal_error( const char* str )
{
	fprintf( stderr, "Error: %s\n", str );
	exit( EXIT_FAILURE );
}

void sdl_init( int width, int height, int depth )
{
	rect.w = width;
	rect.h = height;
	
	if ( SDL_Init( SDL_INIT_VIDEO ) < 0 )
		fatal_error( "SDL initialization failed" );
	atexit( SDL_Quit );
	
	screen = SDL_SetVideoMode( width, height, 0, 0 );
	surface = SDL_CreateRGBSurface( SDL_SWSURFACE, width, height, depth, 0, 0, 0, 0 );
	if ( !screen || !surface )
		fatal_error( "SDL initialization failed" );
}

int sdl_button_pressed()
{
	SDL_Event e;
	
	/* limit to 60 calls per second */
	unsigned long start = SDL_GetTicks();
	if ( start < next_time && next_time - start > 10 )
		SDL_Delay( next_time - start );
	while ( SDL_GetTicks() < next_time ) { }
	next_time = start + 1000 / 60;
	
	while ( SDL_PollEvent( &e ) )
	{
		if ( e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_QUIT )
			return 1;
		
		if ( e.type == SDL_MOUSEMOTION )
		{
			int x, y;
			SDL_GetMouseState( &x, &y );
			mouse_x = x / (float) (SDL_GetVideoSurface()->w - 1);
			mouse_y = 1 - y / (float) (SDL_GetVideoSurface()->h - 1);
		}
	}
	return 0;
}

void sdl_lock_pixels()
{
	if ( SDL_LockSurface( surface ) < 0 )
		fatal_error( "Couldn't lock surface" );
	sdl_pitch = surface->pitch;
	sdl_pixels = (unsigned char*) surface->pixels;
}

void sdl_display()
{
	SDL_UnlockSurface( surface );
	if ( SDL_BlitSurface( surface, &rect, screen, &rect ) < 0 || SDL_Flip( screen ) < 0 )
		fatal_error( "SDL blit failed" );
}

