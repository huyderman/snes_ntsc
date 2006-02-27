snes_ntsc 0.1.0: SNES NTSC Composite Video Emulator
---------------------------------------------------
This is an efficient Super Nintendo (SNES) NTSC composite video blitter for
displaying an authentic image in an emulator. It uses a highly optimized
algorithm to perform the same signal processing as an NTSC decoder in a TV,
giving very similar pixel artifacts. The usual TV picture controls can be
adjusted: hue (tint), saturation, contrast, brightness, sharpness, and gamma.
An optional even/odd field merging feature is included to help when the
emulator's refresh rate isn't a multiple of 60 Hz.

Performance when blitting a 256x223 15-bit BGR SNES source image to a 684x223
16-bit RGB memory buffer is 740 frames per second on an Athlon 3500+ at 2.0 GHz
and 150 frames per second on a 10-year-old 400 MHz G3 PowerMac. Changes to
image parameters take 1/16 second on the Athlon and 1/4 second on the G3. About
4.5 MB of RAM is needed to hold the data table.

Feedback about the interface and usability would be helpful.

Author : Shay Green <gblargg@gmail.com>
Website: http://www.slack.net/~ant/
Forum  : http://groups.google.com/group/blargg-sound-libs
License: BSD-style


Getting Started
---------------
Build a program consisting of demo.c, snes_ntsc.c, and the SDL multimedia
library (see http://libsdl.org/). Running the program with "snes.raw" in the
same directory should show the image as it would look on a TV, with mouse
control of two picture parameters (the raw image is currently little-endian, so
it'll appear funky-colored on a big-endian machine like the PowerPC).


Overview
--------
To use the emulator, allocate memory for an snes_ntsc_t object and call
snes_ntsc_init, then call snes_ntsc_blit repeatedly. You can call
snes_ntsc_init again to change parameters.

The burst_phase parameter to snes_ntsc_blit should toggle between two values
every frame (i.e. 0, 1, 0, 1), unless field merging is on (see below). If
you're blitting the frame using multiple calls to snes_ntsc_blit, the
burst_phase should be passed as (burst_phase + scanline) % 3.

If the monitor refresh rate isn't a multiple of 60 Hz, the image will flicker
due to the even/odd color artifacts not getting equal time on screen. The field
merging option does the equivalent of calling snes_ntsc_blit twice with
differing burst_phases and then mixing the outputs together, resulting in an
image similar to what you'd see on a monitor running at 60 Hz. If field merging
is on, burst_phase should always start with the same value every frame, rather
than toggling (i.e. always use 0).

The hires blit function snes_ntsc_blit_hires is the same as snes_ntsc_blit,
except it reads twice as many SNES input pixels per line (but still outputs the
same number as normal).

The image should be rescaled by 85.11% horizontally and 200% vertically when
displaying on screen; it should be possible to have the graphics card do this.
For example, if you are blitting a 256x223 SNES image to a 684x223 pixel
graphics buffer, it should be scaled to 582x446 pixels on screen for the proper
aspect ratio.

Use the image size constants in snes_ntsc.h for input and output widths. Use
snes_ntsc_min_out_width as a minimum output width that reads exactly 256 SNES
source pixels per scanline, or snes_ntsc_full_out_width to include some
overscan area; when using the latter, you should pad your SNES scanline with
black pixels on the left and right. 

Post to the discussion forum for assistance.


Configuration
-------------
If you are compiling for a big-endian processor, BIG_ENDIAN should be #defined
in the compiler command-line. Little-endian is assumed. The byte order is
checked at initialization using an assert. No other configuration is necessary.


Misc
----
If you're using C++, rename snes_ntsc.c to snes_ntsc.cpp.

Thanks to NewRisingSun for his original code, which was a starting point for me
learning about NTSC video and decoding. Thanks to byuu for testing this in
bsnes.
