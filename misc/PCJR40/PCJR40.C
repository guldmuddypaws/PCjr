/* 
 * This file is part of the PCjr 40th Anniversary display I wrote
 * which draws bitmaps from various PCjr games on the screen and
 * a IBM PCjr 40 text with the date November 1, 2023.
 * (https://github.com/guldmuddypaws/PCjr/misc/PCJR40).
 * Copyright (c) 2023 Jason R Neuhaus
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <dos.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "pcjrbmp.h"

/* Known Bugs/Issues:
   1) Does not work with PCJRCONFIG.SYS /v16 (even though we set the video
   memory to address 0x1800 below...(does work with /v32 and /v64). Works
   without PCJRCONFIG.SYS specified at all.
   2) 2 color bitmap needs to be checked for drawing off screen. 16 color
   bitmap code was tested/fixed for this, but not the 2 color bitmap code.
   3) General cleanp. Code is very inefficient in places.
 */

#define PCJR40_VERSION                "1.0"

#define VIDEO_SERVICES_INTERRUPT      0x10

#define VIDEO_SERVICES_SET_VIDEO_MODE 0x00
#define VIDEO_SERVICES_GET_VIDEO_MODE 0x0F

#define TIME_OF_DAY_SERVICES_INTERRUPT 0x1A
#define TIME_OF_DAY_SERVICES_READ_CURRENT_CLOCK_COUNT 0x00

#define VIDEO_MODE_320_200_16_PCJR    0x09
#define VIDEO_MEMORY_SEGMENT          0x1800
#define VIDEO_MEMORY_PAGE_SIZE        0x2000 /* 8192 bytes */
#define VIDEO_WIDTH_PIXELS            320
#define VIDEO_HEIGHT_PIXELS           200
#define VIDEO_WIDTH_BYTES             VIDEO_WIDTH_PIXELS / 2

#define MACHINE_ID_SEGMENT 0xF000
#define MACHINE_ID_OFFSET  0xFFFE

#define BLACK		0x00
#define BLUE		0x01
#define GREEN		0x02
#define CYAN		0x03
#define RED		0x04
#define MAGENTA		0x05
#define BROWN		0x06
#define LIGHT_GRAY	0x07
#define DARK_GRAY	0x08
#define BRIGHT_BLUE	0x09
#define BRIGHT_GREEN	0x0A
#define BRIGHT_CYAN	0x0B
#define BRIGHT_RED	0x0C
#define BRIGHT_MAGENTA	0x0D
#define BRIGHT_YELLOW	0x0E
#define WHITE		0x0F
/* Use "NO_COLOR" with the draw2ColorBitmap function to tell it to not color in 
   any bits that are not set. i.e. do not set their color to anything 
*/
#define NO_COLOR        0xFF
#define FALSE           0
#define TRUE            1

/** Draw a 2 color bitmap on the screen.
    @param bitmap Pointer to a character array containing the bitmap data
    @param width_bytes Number of bytes wide the bitmap is
    @param height_bytes Number of rows high the bitmap is (rows = bytes in this
    case.)
    @param color Color to set pixels to when the bit is '1' in the bitmap
    @param blank_color Color to set pixels to when the bit is '0' in the bitmap
    or leave the pixel alone if set to 'NO_COLOR' (i.e. like a transparency
    channel).
    @param x_pixel X pixel coordinate of the top left corner where the bitmap
    will be placed.
    @param y_pixel Y pixel coordinate of the top left corner where the bitmap
    will be placed.
    @param x_scale X scaling to apply to the bitmap. Bitmap will be scaled
    linearly based on this scaling.
    @param y_scale Y scaling to apply to the bitmap. Bitmap will be scaled
    linearly based on this scaling.
*/
extern void draw2ColorBitmap(const char*		bitmap,
			     const unsigned short	width_bytes,
			     const unsigned short	height_bytes,
			     const unsigned char	color,
			     const unsigned char	blank_color,
			     const unsigned short	x_pixel,
			     const unsigned short	y_pixel,
			     const unsigned short	x_scale,
			     const unsigned short	y_scale);


/** Draw a 16 color bitmap on the screen.
    @param bitmap Pointer to a character array containing the bitmap data
    @param width_bytes Number of bytes wide the bitmap is
    @param height_bytes Number of rows high the bitmap is (rows = bytes in this
    case.)
    @param blank_color Color to set pixels to when the bit is '0' in the bitmap
    or leave the pixel alone if set to 'NO_COLOR' (i.e. like a transparency
    channel).
    @param x_pixel X pixel coordinate of the top left corner where the bitmap
    will be placed.
    @param y_pixel Y pixel coordinate of the top left corner where the bitmap
    will be placed.
    @param x_scale X scaling to apply to the bitmap. Bitmap will be scaled
    linearly based on this scaling.
    @param y_scale Y scaling to apply to the bitmap. Bitmap will be scaled
    linearly based on this scaling.
    @param invert_x Flag indicating if the bitmap X axis should be inverted
    (horizontal flip). 0 = normal, 1 = flip horizontal.
*/
extern void draw16ColorBitmap(const char*		bitmap,
			      const unsigned short	width_bytes,
			      const unsigned short	height_bytes,
			      const unsigned char	blank_color,
			      const unsigned short 	x_pixel,
			      const unsigned short	y_pixel,
			      const unsigned short	x_scale,
			      const unsigned short	y_scale,
			      const unsigned short      invert_x);

/** This function enters a loop until the specified number of clock ticks
    have passed. The function takes the clock to count from so we can
    keep acurate count even if the program is doing something and also deal
    with midnight crossing.
    @param number_of_clock_ticks Number of clock ticks to wait until returning
    @param from_clock_tick Clock tick counter to count from.
    @return current number of clock ticks. (Typically passed back in the
    next time this function is called for the 'from_clock_ticks' argument.
*/
extern unsigned int waitClockTicks(const unsigned short number_of_clock_ticks,
				   const unsigned int   from_clock_tick);

static unsigned char far* video_memory = (unsigned char far*)
  (MK_FP(VIDEO_MEMORY_SEGMENT,0));

int main()
{
  union REGS registers;
  unsigned char crt_page_register;
  unsigned char original_video_mode;
  unsigned int counter = 0;
  unsigned int counter2 = 0;
  unsigned short animation_counter = 0;

  /* Detect machine and make sure it's a PCjr */
  /* @todo Need to implement */
  unsigned char far* machine_id = (unsigned char far*)
  (MK_FP(MACHINE_ID_SEGMENT,MACHINE_ID_OFFSET));

  if ( *machine_id == 0xFD )
    {
      printf("IBM PCjr detected. Happy 40th Birthday PCjr!\n"
	     "Once started, press any key to exit program.\n"
	     "Use /v32 option with JRCONFIG.SYS\n"
	     "PCJR40 v" PCJR40_VERSION);
    }
  else
    {
      printf("Sorry, this only works on the IBM PCjr\n");
      return -1;
    }

  /* Pause for the message above to be displayed briefly */
  registers.h.ah = TIME_OF_DAY_SERVICES_READ_CURRENT_CLOCK_COUNT;
  int86(TIME_OF_DAY_SERVICES_INTERRUPT,
	&registers,
	&registers);
  /* Wait 3 seconds (18.2 x 3) */
  waitClockTicks(55,
		 registers.x.dx);

  /* Get the current video mode so we can restore it on exit */
  registers.h.ah = VIDEO_SERVICES_GET_VIDEO_MODE;
  int86(VIDEO_SERVICES_INTERRUPT,
	&registers,
	&registers);
  original_video_mode = registers.h.al;

  /* Switch to 320x200 16 color mode */
  registers.h.ah = VIDEO_SERVICES_SET_VIDEO_MODE;
  registers.h.al = VIDEO_MODE_320_200_16_PCJR;

  int86(VIDEO_SERVICES_INTERRUPT,
	&registers,
	&registers);

  /*Set up video page to start at 1800 */
  /* Only works for PCjr, need to add code to check
     system type and display error message before this
     bit   | description
     7-6   | video address mode (11)
     5-3   | 16k video page address for B800 redirection (110)
     2-0   | video page being displayed (110)
     (11110110)
  */
  crt_page_register = 0xF6;
  outportb(0x3DF, crt_page_register); /* PCjr ONLY!! */

  /* Start drawing stuff */
  draw2ColorBitmap((const char*)&ibm_logo_bitmap,
		   IBM_LOGO_WIDTH,
		   IBM_LOGO_HEIGHT,
		   BLUE,
		   NO_COLOR,
		   0,
		   53,
		   2,
		   1);

  draw2ColorBitmap((const char*)&jr_logo_bitmap,
		   JR_LOGO_WIDTH,
		   JR_LOGO_HEIGHT,
		   BLUE,
		   NO_COLOR,
		   164,
		   53+53-35,
		   5,
		   5);

  draw2ColorBitmap((const char*)&char_40_bitmap,
		   CHAR_40_WIDTH,
		   CHAR_40_HEIGHT,
		   BLUE,
		   NO_COLOR,
		   164,
		   110,
		   5,
		   5);

  draw2ColorBitmap((const char*)&char_date_bitmap,
		   CHAR_DATE_WIDTH,
		   CHAR_DATE_HEIGHT,
		   BLUE,
		   NO_COLOR,
		   190,
		   2,
		   1,
		   1);


  /* Mineshaft */
  draw16ColorBitmap((const char*)&mineshaft_cart_bitmap,
		    MINESHAFT_CART_WIDTH,
		    MINESHAFT_CART_HEIGHT,
		    NO_COLOR,
		    218,
		    63,
		    1,
		    1,
		    FALSE);
  
  draw16ColorBitmap((const char*)&mineshaft_gem_bitmap,
		    MINESHAFT_GEM_WIDTH,
		    MINESHAFT_GEM_HEIGHT,
		    NO_COLOR,
		    218,
		    85,
		    1,
		    1,
		    FALSE);
  
  draw16ColorBitmap((const char*)&mineshaft_bug_bitmap,
		    MINESHAFT_BUG_WIDTH,
		    MINESHAFT_BUG_HEIGHT,
		    NO_COLOR,
		    20,
		    84,
		    1,
		    1,
		    FALSE);
  
  draw16ColorBitmap((const char*)&mineshaft_door_bitmap,
		    MINESHAFT_DOOR_WIDTH,
		    MINESHAFT_DOOR_HEIGHT,
		    NO_COLOR,
		    301,
		    69,
		    1,
		    1,
		    FALSE);
  

  /* King's Quest */
  draw16ColorBitmap((const char*)&kq_alligator_bitmap,
		    KQ_ALLIGATOR_WIDTH,
		    KQ_ALLIGATOR_HEIGHT,
		    NO_COLOR,
		    115,
		    82,
		    2,
		    1,
		    FALSE);

  draw16ColorBitmap((const char*)&kq_alligator_bitmap,
		    KQ_ALLIGATOR_WIDTH,
		    KQ_ALLIGATOR_HEIGHT,
		    NO_COLOR,
		    35,
		    102,
		    2,
		    1,
		    TRUE);

  draw16ColorBitmap((const char*)&kq_graham_bitmap,
		    KQ_GRAHAM_WIDTH,
		    KQ_GRAHAM_HEIGHT,
		    NO_COLOR,
		    100,
		    24,
		    2,
		    1,
		    FALSE);

  draw16ColorBitmap((const char*)&kq_goat_bitmap,
		    KQ_GOAT_WIDTH,
		    KQ_GOAT_HEIGHT,
		    NO_COLOR,
		    10,
		    163,
		    2,
		    1,
		    FALSE);

  draw16ColorBitmap((const char*)&kq_dragon1_bitmap,
		    KQ_DRAGON1_WIDTH,
		    KQ_DRAGON1_HEIGHT,
		    NO_COLOR,
		    10,
		    29,
		    2,
		    1,
		    FALSE);

  /* Draw the jumpman floor piece along the bottom */
  for ( counter2 = 0 ;
	counter2 < VIDEO_WIDTH_PIXELS ;
	counter2 += 2*JM_FLOOR_WIDTH*8 )
    {
      draw2ColorBitmap((const char*)&jm_floor_bitmap,
		       JM_FLOOR_WIDTH,
		       JM_FLOOR_HEIGHT,
		       GREEN,
		       NO_COLOR,
		       counter2,
		       192-JM_FLOOR_HEIGHT*2,
		       2,
		       2);
    }

  /* Draw another floor higher up on the right side of the screen */
  for ( counter2 = VIDEO_WIDTH_PIXELS - 4 * 2 * JM_FLOOR_WIDTH * 8;
	counter2 < VIDEO_WIDTH_PIXELS ;
	counter2 += 2*JM_FLOOR_WIDTH*8 )
    {
      draw2ColorBitmap((const char*)&jm_floor_bitmap,
		       JM_FLOOR_WIDTH,
		       JM_FLOOR_HEIGHT,
		       GREEN,
		       NO_COLOR,
		       counter2,
		       146,
		       2,
		       2);
    }

  /* Put shamus on the floating floor */
  draw2ColorBitmap((const char*)&shamus_bitmap,
		   SHAMUS_WIDTH,
		   SHAMUS_HEIGHT,
		   GREEN,
		   NO_COLOR,
		   270,
		   146-SHAMUS_HEIGHT,
		   1,
		   1);

  /* Pitfall Harry */
  draw16ColorBitmap((const char*)&pf2_harry_bitmap,
		    PF2_HARRY_WIDTH,
		    PF2_HARRY_HEIGHT,
		    NO_COLOR,
		    100,
		    192 - (JM_FLOOR_HEIGHT *2 + PF2_HARRY_HEIGHT),
		    2,
		    1,
		    FALSE);

  /* Pitfall Bat */
  draw16ColorBitmap((const char*)&pf2_bat_bitmap,
		    PF2_BAT_WIDTH,
		    PF2_BAT_HEIGHT,
		    NO_COLOR,
		    280,
		    30,
		    2,
		    1,
		    FALSE);

  /* Jump man */
  draw16ColorBitmap((const char*)&jm_jumpman_bitmap,
		    JM_JUMPMAN_WIDTH,
		    JM_JUMPMAN_HEIGHT,
		    NO_COLOR,
		    250,
		    192 - (JM_FLOOR_HEIGHT *2 + JM_JUMPMAN_HEIGHT),
		    2,
		    1,
		    FALSE);

  /* JM alien bomb 1 */
  draw2ColorBitmap((const char*)&jm_alien_bomb_bitmap,
		   JM_ALIEN_BOMB_WIDTH,
		   JM_ALIEN_BOMB_HEIGHT,
		   BROWN,
		   NO_COLOR,
		   270,
		   192 - (JM_FLOOR_HEIGHT + JM_ALIEN_BOMB_HEIGHT)*2,
		   2,
		   2);

  /* JM ladder on right side of screen - 6 rungs */
  for ( counter2 = 1 ; counter2 <= 6 ; counter2++ )
    {
      /* Want to end at JM_FLOOR_HEIGHT * 2 */
      const unsigned short jm_scale = 2;
      const unsigned short y = 192 - JM_FLOOR_HEIGHT * 2 -
	counter2 * JM_LADDER_HEIGHT * jm_scale;

      draw2ColorBitmap((const char*)&jm_ladder_bitmap,
		       JM_LADDER_WIDTH,
		       JM_LADDER_HEIGHT,
		       BRIGHT_BLUE,
		       NO_COLOR,
		       VIDEO_WIDTH_PIXELS - jm_scale * JM_LADDER_WIDTH * 8,
		       y,
		       jm_scale,
		       jm_scale);
    }

  /* JM vines from IBM logo down to ground */
  for ( counter2 = 0 ; counter2 <= 11 ; counter2++ )
    {
      /* Want to end at JM_FLOOR_HEIGHT * 2 */
      const unsigned short jm_scale = 2;
      const unsigned short y = VIDEO_HEIGHT_PIXELS - JM_FLOOR_HEIGHT * 2 -
	counter2 * JM_VINE_DOWN_HEIGHT * jm_scale;
      const unsigned short x_up = 50;
      const unsigned short x_down = x_up + 16;

      draw2ColorBitmap((const char*)&jm_vine_down_bitmap,
		       JM_VINE_DOWN_WIDTH,
		       JM_VINE_DOWN_HEIGHT,
		       MAGENTA,
		       NO_COLOR,
		       x_down,
		       y,
		       jm_scale,
		       jm_scale);

      /* Up vine is half the size of the down vine, so we have to draw it */
      /* twice */
      /* Also, do not draw the up fine on the first pass, let the down vine */
      /* go through the floor */
      if ( counter2 > 1 )
	{
	  draw2ColorBitmap((const char*)&jm_vine_up_bitmap,
			   JM_VINE_UP_WIDTH,
			   JM_VINE_UP_HEIGHT,
			   BRIGHT_CYAN,
			   NO_COLOR,
			   x_up,
			   y,
			   jm_scale,
			   jm_scale);

	  draw2ColorBitmap((const char*)&jm_vine_up_bitmap,
			   JM_VINE_UP_WIDTH,
			   JM_VINE_UP_HEIGHT,
			   BRIGHT_CYAN,
			   NO_COLOR,
			   x_up,
			   y+JM_VINE_UP_HEIGHT,
			   jm_scale,
			   jm_scale);
	}
    }

  /* Set up register data so we can pull it to determine when the
     next animation frame will be */
  registers.h.ah = TIME_OF_DAY_SERVICES_READ_CURRENT_CLOCK_COUNT;
  int86(TIME_OF_DAY_SERVICES_INTERRUPT,
	&registers,
	&registers);

  counter = registers.x.dx;
  for (;;)
    {
      unsigned char vga_status_register;

      /* Pitfall 2 silver bar goes through approximately 28 frames of
	 animation every second
	 the clock ticks approximately 18.2 times per second.
	 So:
	 1/28 (sec/frame) * 18.2 (clock/sec) = 0.65 clock/frame

	 So Pitfall 2 is likely not using the clock to determine
	 animation timing. Maybe just running wide open?

	 We'll try waiting for 1 clock tick between animations to
	 keep things smooth.
      */
      const unsigned int animation_clock = 1;

      counter = waitClockTicks(animation_clock,
			       counter);

      /* Look for vertical refresh before continuing */
      /* Vertical refresh is active when bit 3 at 3DA is 1 */
      do
	{
	  /* DX = 0x3DA 
	     in AL,DX
	     AL bit 3 = vertical refresh status ( 1 = refresh active )
	  */
	  vga_status_register = inportb(0x3DA);
	}
      while ( ( vga_status_register & 0x08 ) == 0 );

      /* Pitfall 2 - silver bar */
      if ( animation_counter == 0 )
	{
	  draw2ColorBitmap((const char*)&pf2_silver_bitmap1,
			   PF2_SILVER_WIDTH,
			   PF2_SILVER_HEIGHT,
			   WHITE,
			   BLACK,
			   160,
			   192 - 2*(JM_FLOOR_HEIGHT)-PF2_SILVER_HEIGHT,
			   2,
			   1);
	}
      else if ( animation_counter == 1 )
	{
	  draw2ColorBitmap((const char*)&pf2_silver_bitmap2,
			   PF2_SILVER_WIDTH,
			   PF2_SILVER_HEIGHT,
			   WHITE,
			   BLACK,
			   160,
			   192 - 2*(JM_FLOOR_HEIGHT)-PF2_SILVER_HEIGHT,
			   2,
			   1);
	}
      else
	{
	  draw2ColorBitmap((const char*)&pf2_silver_bitmap3,
			   PF2_SILVER_WIDTH,
			   PF2_SILVER_HEIGHT,
			   WHITE,
			   BLACK,
			   160,
			   192 - 2*(JM_FLOOR_HEIGHT)-PF2_SILVER_HEIGHT,
			   2,
			   1);
	}

      /* Exit if any key is presed */
      if ( bioskey(1) != 0 )
	{
	  break;
	}

      animation_counter++;
      if ( animation_counter >= 3 )
	{
	  animation_counter = 0;
	}
    }

  /* Restore original video mode */
  registers.h.ah = VIDEO_SERVICES_SET_VIDEO_MODE;
  registers.h.al = original_video_mode;

  int86(VIDEO_SERVICES_INTERRUPT,
	&registers,
	&registers);

  /* Repeat message from earlier so it stays visible 
     @todo Is there a way to keep the original message visible when we
     restore the old video mode? */
  printf("Happy 40th Birthday PCjr!\n"
	 "Full source code available at https://github.com/guldmuddypaws/PCjr\n");
  return 0;
}

#define draw2ColorBitmap_COLOR_LEFT  \
  *(current_screen_byte + x_current_screen_byte) = \
    (*(current_screen_byte + x_current_screen_byte) & 0x0F) | color_left
#define draw2ColorBitmap_COLOR_RIGHT \
  *(current_screen_byte + x_current_screen_byte) = \
    (*(current_screen_byte + x_current_screen_byte) & 0xF0) | color

#define draw2ColorBitmap_BLANK_LEFT  \
  *(current_screen_byte + x_current_screen_byte) = \
    (*(current_screen_byte + x_current_screen_byte) & 0x0F) | blank_color_left
#define draw2ColorBitmap_BLANK_RIGHT \
  *(current_screen_byte + x_current_screen_byte) = \
    (*(current_screen_byte + x_current_screen_byte) & 0xF0) | blank_color

/* Wish inline functions were avaialble */
/* Large chunk of code reused repeatedly in the draw2ColorBitmap function.
   It's likely this can be cleaned up/simplified */
#define draw2ColorBitmap_ADVANCE_TO_NEXT_BIT \
  /* Next pixel */ \
  if ( --x_scale_counter <= 0 ) \
    { \
  bitmap_bit_counter >>= 1; \
  x_scale_counter = x_scale; \
  \
  /* Check to see if we've gone through all 8 bits */ \
  if ( bitmap_bit_counter == 0 ) \
    { \
  /* Reset to the farthest left bit */ \
  bitmap_bit_counter   = 128; \
  /* Advance to the next byte in the bitmap */ \
  bitmap_byte += 1; \
    } \
  bitmap_pixel = *bitmap_byte & bitmap_bit_counter; \
    }

/* This second of code checks the next two pixels in the bitmap and fills
   in the video memory as appropriate
*/
#define draw2ColorBitmap_DRAW_NEXT_2_PIXELS \
  if ( bitmap_pixel != 0 ) \
    { \
  /* Color left pixel */ \
  draw2ColorBitmap_COLOR_LEFT; \
    } \
  else if ( blank_color != NO_COLOR ) \
    { \
  /* Blank left pixel */ \
  draw2ColorBitmap_BLANK_LEFT; \
    } \
  draw2ColorBitmap_ADVANCE_TO_NEXT_BIT;		\
  \
  if ( bitmap_pixel != 0 ) \
    { \
  /* Color right pixel */ \
  draw2ColorBitmap_COLOR_RIGHT; \
    } \
  else if ( blank_color != NO_COLOR ) \
    { \
  /* Blank right pixel */ \
  draw2ColorBitmap_BLANK_RIGHT; \
    } \
  \
  draw2ColorBitmap_ADVANCE_TO_NEXT_BIT;


/* @todo this could likely be improved, instead of directly modifying the
   video memory character directly, it might be better to make a copy, do all
   the manipulation locally, then copy it back to video memory afterwards
*/
void draw2ColorBitmap(const char*		bitmap,
		      const unsigned short	width_bytes,
		      const unsigned short	height_bytes,
		      const unsigned char	color,
		      const unsigned char	blank_color,
		      const unsigned short 	x_pixel,
		      const unsigned short	y_pixel,
		      const unsigned short	x_scale,
		      const unsigned short	y_scale)
{
  /* Since there are two pixels per byte in the video buffer, go ahead and set
     up a value for the full byte with the color specified */
  const unsigned char color_left = color << 4;
  /* 'blank_color' contains the color to use for blanking in the right hand
     nibble. 'blank_color_left' will contains the same color in the left
     nibble.
  */
  const unsigned char blank_color_left = blank_color << 4;

  unsigned short x_current_screen_byte;
  short x_pixels_remaining;
  unsigned short y_current_row;

  unsigned short x_scale_counter = x_scale;
  unsigned short y_scale_counter = y_scale;

  div_t quot_and_rem = div(y_pixel, 4);

  /* Take the quotient and find which page we are in */
  unsigned short current_page = quot_and_rem.rem;
  unsigned short page_start = current_page * VIDEO_MEMORY_PAGE_SIZE;
  /* Take the remainder to figure out which row in that page we are in */
  unsigned short page_row_byte = quot_and_rem.quot * VIDEO_WIDTH_BYTES;

  /* Calculate how many pixels wide the bitmap is */
  /* Each byte contains 8 pixels of data (1 pixel for each bit) */
  unsigned short width_pixels  = width_bytes * 8 * x_scale;
  unsigned short height_pixels = height_bytes * y_scale;
  /* Reduce the pixel width/height if it will go over the edge of the screen */
  if ( x_pixel + width_pixels > VIDEO_WIDTH_PIXELS )
    {
      width_pixels = VIDEO_WIDTH_PIXELS - x_pixel;
    }
  if ( y_pixel + height_pixels > VIDEO_HEIGHT_PIXELS )
    {
      height_pixels = VIDEO_HEIGHT_PIXELS - y_pixel;
    }

  /* Determine which half of the byte for each pixel we are on */
  quot_and_rem = div(x_pixel,2); /* 2 pixels per byte */
  {
  /* If we are an even column, then we will be starting on the left half of */
  /* the byte */
  const unsigned char column_start_left_byte = quot_and_rem.rem == 0;
  const unsigned short x_byte = quot_and_rem.quot;

  /* Calculate the first byte we are starting on for this row to reduce */
  /* calculations */
  unsigned char far* current_screen_byte = (video_memory +
					    page_start +
					    page_row_byte +
					    x_byte);
  /* Pointer to the current bitmap row */
  /* Set far left bit only - 1000 0000*/
  unsigned char  bitmap_bit_counter  = 128; 
  const char* bitmap_byte = bitmap;
  /* Keep track of the pointer to the beginning of the row in the bitmap
     so we can go back to it if we have a y_scale input, could likely also
     just subtract a fixed size but that might have issues with the code
     which attemps to cut the width down if we go past the end of the screen
     on the right
     @todo needs testing with bitmaps asked to be drawn off the edges of the
     screen
  */
  const char* bitmap_byte_start_of_row = bitmap;
  /* Extract the first bit from the bitmap */
  char bitmap_pixel = *bitmap_byte & bitmap_bit_counter;

  for ( y_current_row = 0 ; y_current_row < height_pixels ; ++y_current_row )
    {
      x_pixels_remaining = width_pixels;
      x_current_screen_byte = 0;

      if ( column_start_left_byte )
	{
	  if ( x_pixels_remaining > 1 )
	    {
	      draw2ColorBitmap_DRAW_NEXT_2_PIXELS;
	      
	      x_pixels_remaining -= 2;
	    }
	}
      else
	{
	  if ( bitmap_pixel != 0 )
	    {
	      /* Color right pixel */
	      draw2ColorBitmap_COLOR_RIGHT;
	    }
	  else if ( blank_color != NO_COLOR )
	    {
	      /* Blank right pixel */
	      draw2ColorBitmap_BLANK_RIGHT;
	    }

	  draw2ColorBitmap_ADVANCE_TO_NEXT_BIT;

	  x_pixels_remaining -= 1;
	}

      /* Now the middle section */
      for ( ;
	    x_pixels_remaining > 1 ;
	    x_pixels_remaining -= 2 )
	{
	  x_current_screen_byte += 1;

	  draw2ColorBitmap_DRAW_NEXT_2_PIXELS;
	}

      /* See if there is a single pixel remaining */
      if ( x_pixels_remaining == 1 )
	{
	  x_current_screen_byte += 1;

	  if ( bitmap_pixel != 0 )
	    {
	      /* Color left pixel */
	      draw2ColorBitmap_COLOR_LEFT;
	    }
	  else if ( blank_color != NO_COLOR )
	    {
	      /* Blank left pixel */
	      draw2ColorBitmap_BLANK_LEFT;
	    }
	  /* If is NOT the last row, advance to the next bit/byte of the */
	  /* bitmap */
	  /* This is to prevent reading past the end of the bitmap array */
	  if ( y_current_row < height_pixels - 1 )
	    {
	      draw2ColorBitmap_ADVANCE_TO_NEXT_BIT;
	}
	}

      /* Advance to next page and continue processing */
      current_page += 1;
      if ( current_page > 3 )
	{
	  current_page = 0;
	  page_row_byte += VIDEO_WIDTH_BYTES;
	}
      page_start = current_page * VIDEO_MEMORY_PAGE_SIZE;

      /* Adjust pointers if a y scale factor is in effect */
      if ( --y_scale_counter <= 0 )
	{
	  y_scale_counter = y_scale;
	  /* Make sure we advance to the next row of the bitmap, this is only
	     necessary in cases where the bitmap tries to go off the screen
	  */
	  bitmap_byte = bitmap_byte_start_of_row + width_bytes;
	  bitmap_byte_start_of_row = bitmap_byte;
	}
      else
	{
	  bitmap_byte = bitmap_byte_start_of_row;
	}

      /* Ensure the x scale counter is reset in cases we went past the edge 
	 of the screen
      */
      x_scale_counter = x_scale;
      bitmap_bit_counter = 128;

      bitmap_pixel = *bitmap_byte & bitmap_bit_counter;

      current_screen_byte = (video_memory +
			     page_start +
			     page_row_byte +
			     x_byte);
    }
  }
}

#undef draw2ColorBitmap_COLOR_LEFT
#undef draw2ColorBitmap_COLOR_RIGHT
#undef draw2ColorBitmap_BLANK_LEFT
#undef draw2ColorBitmap_BLANK_RIGHT
#undef draw2ColorBitmap_ADVANCE_TO_NEXT_BIT
#undef draw2ColorBitmap_DRAW_NEXT_2_PIXELS

/* This method draws a 16 color bitmap to the screen. Each nibble of the
   'bitmap' sets 1 pixels on the screen to the specified color.
*/
void draw16ColorBitmap(const char*		bitmap,
		       const unsigned short	width_bytes,
		       const unsigned short	height_bytes,
		       const unsigned char	blank_color,
		       const unsigned short 	x_pixel,
		       const unsigned short	y_pixel,
		       const unsigned short	x_scale,
		       const unsigned short	y_scale,
		       const unsigned short     invert_x)
{
  unsigned short x_current_screen_byte;
  short x_pixels_remaining;
  unsigned short y_current_row;

  unsigned short x_scale_counter = x_scale;
  unsigned short y_scale_counter = y_scale;

  div_t quot_and_rem = div(y_pixel, 4);

  /* Take the quotient and find which page we are in */
  unsigned short current_page = quot_and_rem.rem;
  unsigned short page_start = current_page * VIDEO_MEMORY_PAGE_SIZE;
  /* Take the remainder to figure out which row in that page we are in */
  unsigned short page_row_byte = quot_and_rem.quot * VIDEO_WIDTH_BYTES;

  /* Calculate how many pixels wide the bitmap is */
  /* Each byte contains 2 pixels of data (2 pixels for each byte) */
  unsigned short width_pixels  = width_bytes * 2 * x_scale;
  unsigned short height_pixels = height_bytes * y_scale;
  /* Reduce the pixel width/height if it will go over the edge of the screen */
  if ( x_pixel + width_pixels > VIDEO_WIDTH_PIXELS )
    {
      width_pixels = VIDEO_WIDTH_PIXELS - x_pixel;
    }
  if ( y_pixel + height_pixels > VIDEO_HEIGHT_PIXELS )
    {
      height_pixels = VIDEO_HEIGHT_PIXELS - y_pixel;
    }

  /* Determine which half of the byte for each pixel we are on */
  quot_and_rem = div(x_pixel,2); /* 2 pixels per byte */
  {
  /* If we are an even column, then we will be starting on the left half of */
  /* the byte */
  const unsigned char column_start_left_byte = quot_and_rem.rem == 0;
  const unsigned short x_byte = quot_and_rem.quot;

  /* Calculate the first byte we are starting on for this row to reduce */
  /* calculations */
  unsigned char far* current_screen_byte = (video_memory +
					    page_start +
					    page_row_byte +
					    x_byte);

  /* Start at the beginning of the bitmap, or at the end of the first
     row depending on the invert_x flag
  */
  const char* bitmap_byte =
    ( invert_x == FALSE ?
      bitmap :
      bitmap + width_bytes - 1 );

  /* Keep track of the pointer to the beginning of the row in the bitmap
     so we can go back to it if we have a y_scale input, could likely also
     just subtract a fixed size but that might have issues with the code
     which attemps to cut the width down if we go past the end of the screen
     on the right
     @todo needs testing with bitmaps asked to be drawn off the edges of the
     screen
  */
  const char* bitmap_byte_start_of_row = bitmap_byte;

  /* Bitmap mask used to alternate which nibble from each byte in the bitmap
     we are looking at at any given point in time. Start on the left nibble.
  */
  unsigned char bitmap_nibble_mask;

  /* Bitmap mask used to alternate which nibble from each byte in video memory
     we are looking at at any given point in time. Start on the left nibble.
  */
  unsigned char screen_nibble_mask;

  for ( y_current_row = 0 ; y_current_row < height_pixels ; ++y_current_row )
    {
      char current_bitmap_nibble;
      unsigned char new_screen_byte = 0x00;
      unsigned char screen_byte;

      /* At the start of a new row, the bitmap nibble should always be
	 reset. We reset it below in case the bitmap runs off the screen
	 and the mask has not flipped back to the original value yet. */
      /* The bitmap mask starts at 0xF0 if we are not inverting, otherwise
	 0x0F */
      if ( invert_x == FALSE )
	{
	  bitmap_nibble_mask = 0xF0;
	  current_bitmap_nibble = (*bitmap_byte & 0xF0 ) >> 4;
	}
      else
	{	
	  bitmap_nibble_mask = 0x0F;
	  current_bitmap_nibble = (*bitmap_byte & 0x0F );
	}

      /* If the pixel we want is on the left half of the byte we are looking at
	 then the process of drawing the bitmap is straightforward and we can
	 just copy the bitmap directly into screen memory.
	 Similar to above, we must reset the screen mask in case the bitmap
	 ran off the screen.
      */
      if ( !column_start_left_byte )
	{
	  screen_nibble_mask = 0x0F;
	}
      else
	{
	  screen_nibble_mask = 0xF0;
	}

      /* If we are starting on the right nibble of the screen, we need to
	 preload the left nibble of the byte that will eventually be written
	 to the screen with the current pixel in that location, otherwise
	 we will inadvertently wipe it */
      if ( screen_nibble_mask == 0x0F )
	{
	  new_screen_byte = *(current_screen_byte) & 0xF0;
	}

      x_pixels_remaining = width_pixels;
      x_current_screen_byte = 0;

      for ( ;
	    x_pixels_remaining > 0 ;
	    --x_pixels_remaining )
	{
	  char new_screen_nibble = 0x00;
	  screen_byte = *(current_screen_byte + x_current_screen_byte);

	  /* If the left nibble in the bitmap is 0, see if it should be colored a different
	     color or ignored (transparency)
	  */
	  if ( current_bitmap_nibble == 0x00 )
	    {
	      if ( blank_color != NO_COLOR )
		{
		  new_screen_nibble = blank_color;
		}
	      else
		{
		  new_screen_nibble = ( screen_byte & screen_nibble_mask );

		  /* If we are looking at the left nibble on the screen
		     shift it into the right hand side to be consistent */
		  if ( screen_nibble_mask == 0xF0 )
		    {
		      new_screen_nibble = new_screen_nibble >> 4;
		    }
		}
	    }
	  else
	    {
	      /* Set to the current nibble color in the bitmap */
	      new_screen_nibble = current_bitmap_nibble;
	    }

	  /* If we are on the right hand screen nibble, go ahead and write
	     it to video memory now before we advance */
	  if ( screen_nibble_mask == 0x0F )
	    {
	      new_screen_byte |= new_screen_nibble;
	      *(current_screen_byte + x_current_screen_byte) = new_screen_byte;
	      
	      x_current_screen_byte += 1;
	      new_screen_byte = 0x00;
	    }
	  else
	    {
	      new_screen_byte = new_screen_nibble << 4;
	    }
	  screen_nibble_mask ^= 0xFF;

	  if ( --x_scale_counter <= 0 )
	    {
	      x_scale_counter = x_scale;

	      /* Swap the mask between 0xF0 and 0x0F */
	      bitmap_nibble_mask ^= 0xFF;

	      if ( bitmap_nibble_mask == 0x0F )
		{
		  /* If we just changed to the trailing nibble in the bitmap
		     and we are inverting the X axis, go backwards one byte
		     in memory to the "next" bitmap nibble */
		  if ( invert_x != FALSE )
		    {
		      bitmap_byte -= 1;
		    }

		  current_bitmap_nibble = ( *bitmap_byte & 0x0F );
		}
	      else
		{
		  /* If we just changed to the leading nibble in the bitmap
		     and we are NOT inverting the X axis, go forward one byte
		     in memory to the next bitmap nibble */
		  if ( invert_x == FALSE )
		    {
		      bitmap_byte += 1;
		    }
		  current_bitmap_nibble = ( *bitmap_byte & 0xF0 ) >> 4;
		}
	    }
	}

      /* See if there is a single pixel remaining */
      /* This happens when we start on the right hand nibble on the screen.
	 Since the bitmap is ALWAYS an even number of pixels by the way it is
	 currently defined, we must write out one more pixel */
      if ( screen_nibble_mask == 0x0F )
	{
	  /* The color we want to right is already populated in the new
	     byte left hand nibble, so we just have to set the right
	     hand nibble appropriately */
	  new_screen_byte = new_screen_byte | ( screen_byte & 0x0F );
	  *(current_screen_byte + x_current_screen_byte) = new_screen_byte;
	}

      /* Advance to next page and continue processing */
      current_page += 1;
      if ( current_page > 3 )
	{
	  current_page = 0;
	  page_row_byte += VIDEO_WIDTH_BYTES;
	}
      page_start = current_page * VIDEO_MEMORY_PAGE_SIZE;

      /* Adjust pointers if a y scale factor is in effect */
      if ( --y_scale_counter <= 0 )
	{
	  y_scale_counter = y_scale;
	  /* Make sure we advance to the next row of the bitmap, this is only
	     necessary in cases where the bitmap tries to go off the screen
	  */
	  bitmap_byte = bitmap_byte_start_of_row + width_bytes;
	  bitmap_byte_start_of_row = bitmap_byte;
	}
      else
	{
	  bitmap_byte = bitmap_byte_start_of_row;
	}

      /* Ensure the x scale counter is reset in cases we went past the edge 
	 of the screen
      */
      x_scale_counter = x_scale;

      current_screen_byte = (video_memory +
			     page_start +
			     page_row_byte +
			     x_byte);
    }
  }
}

/* Burn clock cycles until the number of ticks have passed */
unsigned int waitClockTicks(const unsigned short number_of_clock_ticks,
			    const unsigned int   from_clock_tick)
{
  union REGS registers;
  unsigned short number_of_ticks_remaining;
  unsigned int   past_clock_tick;

  /* call the time of day services interrupt to get the current clock
     counters */
  registers.h.ah = TIME_OF_DAY_SERVICES_READ_CURRENT_CLOCK_COUNT;
  int86(TIME_OF_DAY_SERVICES_INTERRUPT,
	&registers,
	&registers);

  /* Figure out how many clock ticks have occured since the last time this
     function was called */
  if ( registers.x.dx >= from_clock_tick )
    {
      /* Temporarily store the number of clock ticks since last called */
      number_of_ticks_remaining = registers.x.dx - from_clock_tick;

      /* Make sure we haven't already passed the target time */
      if ( number_of_ticks_remaining < number_of_clock_ticks )
	{
	  number_of_ticks_remaining = number_of_clock_ticks -
	    number_of_ticks_remaining;
	}
      else
	{
	  /* number of ticks remaining would be negative, return
	     immediately */
	  return registers.x.dx;
	}
    }
  else
    {
      /* Either a midnight crossing or the low order count overflowed
	 from 65535 to 0. So we really don't know for sure how many clock
	 ticks have passed since the last time this function was called, but
	 is probably 1. We will assume 1 */
      number_of_ticks_remaining = number_of_clock_ticks - 1;
    }

  /* Now keep monitoring the low order clock variable until the number of
     ticks we want passes */
  past_clock_tick = registers.x.dx;

  do
    {
      int86(TIME_OF_DAY_SERVICES_INTERRUPT,
	    &registers,
	    &registers);

      /* We assume any change to the clock tick is 1 tick */
      if ( registers.x.dx != past_clock_tick )
	{
	  past_clock_tick = registers.x.dx;
	  --number_of_ticks_remaining;
	}
    }
  while ( number_of_ticks_remaining > 0 );

  return registers.x.dx;
}
