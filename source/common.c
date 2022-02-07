#include "common.h"
#include "main_screen.h"
#include "cursor.h"

void wait_vblank()
{
	while(IO_H[3] != 0xA0) { }
}

void wait_frames(u32 frames)
{
	while(IO_H[3] != 0xA0) { }

	u32 counter = 0;

	while(counter != frames)
	{
		while(IO_H[3] != 0x00) { }
		while(IO_H[3] != 0xA0) { }
		counter++;
	}
}

void setup()
{
	//Disable IRQs
	IO_H[256] = 0x00;
	IO_H[257] = 0xFFFF;
	IO_H[260] = 0x00;
	
	//Force blank
	IO_H[0] = 0x80;

	//Draw main screen
	draw_bitmap(main_screen, main_screen_size, 0, 0, 240);

	//Draw cursor
	draw_bitmap_cc(cursor, cursor_size, 90, 75, 16, 0x7FFF);

	//Enable BG2 in Mode 3
	IO_H[0] = 0x403;

	fade_in(4);
}

void fade_in(u32 frames)
{
	//Fade in gradually (from black)
	u8 fade_val = 16;
	IO_H[40] = 0x4C4;

	while(fade_val != 0)
	{
		IO_H[42] = fade_val;
		wait_frames(frames);
		fade_val--;
	}
}

void fade_out(u32 frames)
{
	//Fade out gradually (to black)
	u8 fade_val = 0;
	IO_H[40] = 0x4C4;

	while(fade_val != 16)
	{
		IO_H[42] = fade_val;
		wait_frames(frames);
		fade_val++;
	}
}

void draw_bitmap(const unsigned char* bmp_src, const int bmp_size, u32 sx, u32 sy, u32 sw)
{
	u32 origin = (sy * 240) + sx;
	u32 buffer_pos = 0;
	u32 width_counter = 0;
	u32 final_offset = 0;

	for(u32 x = 0; x < bmp_size;)
	{
		if(width_counter == sw)
		{
			width_counter = 0;
			final_offset -= sw;
			final_offset += 240;
		}

		u16 val = (bmp_src[x + 1] << 8) | (bmp_src[x]);
		buffer_pos = origin + final_offset;

		if(buffer_pos < 0x9600)
		{
			VRAM_H[buffer_pos] = val;
		}

		final_offset++;
		width_counter++;
		x += 2;
	}
}

void draw_bitmap_cc(const unsigned char* bmp_src, const int bmp_size, u32 sx, u32 sy, u32 sw, u16 clear_color)
{
	u32 origin = (sy * 240) + sx;
	u32 buffer_pos = 0;
	u32 width_counter = 0;
	u32 final_offset = 0;

	for(u32 x = 0; x < bmp_size;)
	{
		if(width_counter == sw)
		{
			width_counter = 0;
			final_offset -= sw;
			final_offset += 240;
		}

		u16 val = (bmp_src[x + 1] << 8) | (bmp_src[x]);
		buffer_pos = origin + final_offset;

		if((buffer_pos < 0x9600) && (val != clear_color))
		{
			VRAM_H[buffer_pos] = val;
		}

		final_offset++;
		width_counter++;
		x += 2;
	}
}

void draw_font_cc(const unsigned char* bmp_src, u8 index, u32 sx, u32 sy, u16 clear_color)
{
	u8 font_data[512];
	u8 width_count = 0;
	u32 buffer_pos = ((index / 20) * 0x2800) + ((index % 20) * 32);

	for(u32 x = 0; x < 512; x++)
	{
		font_data[x] = bmp_src[buffer_pos++];
		width_count++;

		if(width_count == 32)
		{
			width_count = 0;
			buffer_pos -= 32;
			buffer_pos += 640;
		}
	}

	draw_bitmap_cc(font_data, 512, sx, sy, 16, clear_color);
}

void clear_bitmap()
{
	for(u32 x = 0; x < 0x9600; x++) { VRAM_H[x] = 0; }
}

void clear_highlight(const unsigned char* bmp_src, u32 sx, u32 sy)
{
	u8 width_count = 0;
	u16 val = 0;
	u32 buffer_pos = ((sy * 240) + sx);
	u32 src_pos = 0;
	u32 y_pos = 0;

	for(u32 x = 0; x < 648;)
	{

		src_pos = (buffer_pos << 1);
		val = (bmp_src[src_pos + 1] << 8) | (bmp_src[src_pos]);

		if((width_count == 0) || (width_count == 17) || (y_pos == 0) || (y_pos == 17)) { VRAM_H[buffer_pos] = val; }

		width_count++;
		buffer_pos++;

		if(width_count == 18)
		{
			width_count = 0;
			buffer_pos -= 18;
			buffer_pos += 240;
			y_pos++;
		}

		x += 2;
	}
}

void clear_char(const unsigned char* bmp_src, u32 sx, u32 sy)
{
	u8 width_count = 0;
	u16 val = 0;
	u32 buffer_pos = ((sy * 240) + sx);
	u32 src_pos = 0;

	for(u32 x = 0; x < 512;)
	{
		src_pos = (buffer_pos << 1);
		val = (bmp_src[src_pos + 1] << 8) | (bmp_src[src_pos]);
		VRAM_H[buffer_pos++] = val;
		width_count++;

		if(width_count == 16)
		{
			width_count = 0;
			buffer_pos -= 16;
			buffer_pos += 240;
		}

		x += 2;
	}
}

		
		