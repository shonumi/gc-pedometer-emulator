#include <gba_types.h>

#define IO_B ((vu8*)0x04000000)
#define IO_H ((vu16*)0x04000000)
#define IO_W ((vu32*)0x04000000)

#define VRAM_H ((vu16*)0x06000000)

void wait_vblank();
void wait_frames(u32 frames);
void setup();
void fade_in(u32 frames);
void fade_out(u32 frames);
void draw_bitmap(const unsigned char* bmp_src, const int bmp_size, u32 sx, u32 sy, u32 sw);
void draw_bitmap_cc(const unsigned char* bmp_src, const int bmp_size, u32 sx, u32 sy, u32 sw, u16 clear_color);
void draw_font_cc(const unsigned char* bmp_src, u8 index, u32 sx, u32 sy, u16 clear_color);
void clear_bitmap();
void clear_highlight();
void clear_char();
