#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <gba_dma.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_sio.h>
#include <gba_timers.h>

#include "bios.h"
#include "common.h"

#include "main_screen.h"
#include "send_screen.h"
#include "edit_screen_1.h"
#include "edit_screen_2.h"
#include "edit_screen_3.h"
#include "edit_screen_4.h"
#include "highlight.h"
#include "font_kana.h"
#include "font_num.h"
#include "cursor.h"

#define ROM           ((int16_t *)0x08000000)
#define ROM_GPIODATA *((int16_t *)0x080000C4)
#define ROM_GPIODIR  *((int16_t *)0x080000C6)
#define ROM_GPIOCNT  *((int16_t *)0x080000C8)
#define SAVE_DATA    *((uint8_t *)0x0E000000)

//#define ANALOG

enum {
	CMD_ID = 0x00,
	CMD_STATUS = 0x40,
	CMD_DATA = 0x60,
	CMD_RESET = 0xFF
};

static struct {
	uint8_t type[2];

	struct {
		uint8_t mode   : 3;
		uint8_t motor  : 2;
		uint8_t origin : 1;
		uint8_t        : 2;
	} status;
} id;

struct
{
	u8 state;
	u32 x;
	u32 y;
} screen_cursor;

struct
{
	u8 state;
	u32 x;
	u32 y;
} highlight_cursor;

u8 program_state = 0;
u8 data_state = 0;
u8 page_limit[4][6];
int poll_length = 0;

static uint8_t buffer[128];
static uint8_t last_cmd_data[128];
static uint8_t reply_buffer[80];
static uint8_t status_buffer[8];
static uint8_t poll_buffer[80];

void SISetResponse(const void *buf, unsigned bits);
void SISetResponse8(uint8_t *buf, unsigned bits);
int SIGetCommand(void *buf, unsigned bits);

void main_screen_idle();
void send_data_idle();
void edit_data_idle();
void show_data_idle();

int main()
{
	for(u8 x = 0; x < 80; x++)
	{
		reply_buffer[x] = 0;
		poll_buffer[x] = 0;
	}

	//Do some initial setup
	setup();

	//Page up edit page cursor limits
	page_limit[0][0] = 5;
	page_limit[0][1] = 2;
	page_limit[0][2] = 2;
	page_limit[0][3] = 2;
	page_limit[0][4] = 0;
	page_limit[0][5] = 2;

	page_limit[1][0] = 5;
	page_limit[1][1] = 5;
	page_limit[1][2] = 4;
	page_limit[1][3] = 5;
	page_limit[1][4] = 5;
	page_limit[1][5] = 5;

	page_limit[2][0] = 5;
	page_limit[2][1] = 5;
	page_limit[2][2] = 5;
	page_limit[2][3] = 5;
	page_limit[2][4] = 5;
	page_limit[2][5] = 5;

	page_limit[3][0] = 5;
	page_limit[3][1] = 5;
	page_limit[3][2] = 5;
	page_limit[3][3] = 5;
	page_limit[3][4] = 5;
	page_limit[3][5] = 5;


	screen_cursor.state = 0;
	screen_cursor.x = 90;
	screen_cursor.y = 75;

	//Grab input, change some graphics and data in response, and grab and send JoyBus data
	while(true)
	{
		switch(program_state)
		{
			case 0: main_screen_idle(); break;
			case 1: edit_data_idle(); break;
			case 2: send_data_idle(); break;
			case 3: show_data_idle(); break;
			case 4: show_poll_idle(); break;
		}
	}
}

void IWRAM_CODE wait_for_signal()
{
	bool waiting = true;

	REG_IE = IRQ_SERIAL | IRQ_TIMER2 | IRQ_TIMER1 | IRQ_TIMER0 | IRQ_KEYPAD;
	REG_IF = REG_IF;

	REG_RCNT = R_GPIO | 0x100 | GPIO_SO_IO | GPIO_SO;

	REG_TM0CNT_L = -67;
	REG_TM1CNT_H = TIMER_START | TIMER_IRQ | TIMER_COUNT;
	REG_TM0CNT_H = TIMER_START;

	SoundBias(0);
	Halt();

	while(waiting)
	{
		int length = SIGetCommand(buffer, sizeof(buffer) * 8 + 1);

		if(length == -1) { waiting = false; }
		else if (length < 9) { continue; }

		bool ping_pattern = true;

		switch(buffer[0])
		{
			case CMD_RESET:
			case CMD_ID:
				if(length == 9)
				{
					id.type[0] = 0x08;
					id.type[1] = 0x02;
					SISetResponse(&id, sizeof(id) * 8);
				}

				break;

			case CMD_DATA:
				//Check for Ping Pattern
				for(u8 x = 1; x < 7; x++)
				{
					if(buffer[x] != 0xFF) { ping_pattern = false; }
				}

				if(buffer[0x4E] != 0x06) { ping_pattern = false; }
				if(buffer[0x4F] != 0x5A) { ping_pattern = false; }

				//Return current data when detecting the ping pattern
				if(ping_pattern)
				{
					SISetResponse8(reply_buffer, (sizeof(reply_buffer) * 8));
					for(u8 x = 0; x < 128; x++) { last_cmd_data[x] = buffer[x]; }
				}

				//Update data when ping pattern is not detected
				//Also return updated data
				else
				{
					for(u8 x = 1; x < 0x0C; x++) { reply_buffer[x] = buffer[x]; }
					SISetResponse8(reply_buffer, (sizeof(reply_buffer) * 8));
				}
					
				break;

			case CMD_STATUS:
				poll_length = length;
				for(u8 x = 0; x < poll_length; x++) { poll_buffer[x] = buffer[x]; }
				break;

			default:
				break;

		}
	}

	REG_IE = 0;
	program_state = 0;

	fade_out(4);

	screen_cursor.state = 0;
	screen_cursor.x = 90;
	screen_cursor.y = 75;

	wait_frames(1);
	draw_bitmap(main_screen, main_screen_size, 0, 0, 240);
	
	wait_frames(1);
	draw_bitmap_cc(cursor, cursor_size, screen_cursor.x, screen_cursor.y, 16, 0x7FFF);

	fade_in(4);
}

void main_screen_idle()
{
	//Move cursor up
	if(((IO_H[152] & 0x40) == 0) && (screen_cursor.state > 0))
	{
		screen_cursor.state--;
		screen_cursor.y -= 28;

		wait_frames(1);
		draw_bitmap(main_screen, main_screen_size, 0, 0, 240);
	
		wait_frames(1);
		draw_bitmap_cc(cursor, cursor_size, screen_cursor.x, screen_cursor.y, 16, 0x7FFF);
	}

	//Move cursor down
	else if(((IO_H[152] & 0x80) == 0) && (screen_cursor.state < 1))
	{
		screen_cursor.state++;
		screen_cursor.y += 28;

		wait_frames(1);
		draw_bitmap(main_screen, main_screen_size, 0, 0, 240);
	
		wait_frames(1);
		draw_bitmap_cc(cursor, cursor_size, screen_cursor.x, screen_cursor.y, 16, 0x7FFF);
	}

	//Edit data when pressing A
	else if(((IO_H[152] & 0x01) == 0) && (screen_cursor.state == 0))
	{
		program_state = 1;
	}

	//Send data when pressing A
	else if(((IO_H[152] & 0x01) == 0) && (screen_cursor.state == 1))
	{
		program_state = 2;
	}

	//Show last 0x40 data sent to GBA when pressing L
	else if((IO_H[152] & 0x200) == 0)
	{
		consoleDemoInit();
		printf("0x40 data sent to GBA:\n");
		printf("Length = %d\n", poll_length);

		for(u8 x = 0, y = 0; x < poll_length; x++)
		{
			printf("%02x ", poll_buffer[x]);
			y++;

			if(y == 8)
			{
				y = 0;
				printf("\n");
			}
		}

		program_state = 4;
	}

	//Show last CMD_DATA sent to GBA when pressing R
	else if((IO_H[152] & 0x100) == 0)
	{
		consoleDemoInit();
		printf("CMD_DATA sent to GBA:\n\n");

		for(u8 x = 0, y = 0; x < 128; x++)
		{
			printf("%02x ", last_cmd_data[x]);
			y++;

			if(y == 8)
			{
				y = 0;
				printf("\n");
			}
		}

		program_state = 3;
	}

	wait_frames(1);
}

void edit_data_idle()
{
	highlight_cursor.x = 128;
	highlight_cursor.y = 18;
	highlight_cursor.state = 0;

	u8 index = 0;
	u8 page_x = 0;
	u8 page_y = 0;
	u8 page = 0;

	//Draw edit data screen (Page 0)
	fade_out(4);

	wait_frames(1);
	draw_bitmap(edit_screen_1, edit_screen_1_size, 0, 0, 240);

	wait_frames(1);
	draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);

	fade_in(4);

	bool waiting = true;
	bool update = true;
	bool update_all = true;

	u32 last_x = 0;
	u32 last_y = 0;

	u32 temp24 = 0;

	while(waiting)
	{
		//Exit when pressing B
		if((IO_H[152] & 0x02) == 0) { waiting = false; }

		//Increase page when pressing R
		else if((IO_H[152] & 0x100) == 0)
		{
			page++;
			if(page > 4) { page = 0; }

			update = true;
			update_all = true;

			highlight_cursor.x = 128;
			highlight_cursor.y = 18;
			page_x = 0;
			page_y = 0;
			last_x = 0;
			last_y = 0;

			wait_frames(1);
			
			switch(page)
			{
				case 0: draw_bitmap(edit_screen_1, edit_screen_1_size, 0, 0, 240); break;
				case 1: draw_bitmap(edit_screen_2, edit_screen_2_size, 0, 0, 240); break;
				case 2: draw_bitmap(edit_screen_3, edit_screen_3_size, 0, 0, 240); break;
				case 3: draw_bitmap(edit_screen_4, edit_screen_4_size, 0, 0, 240); break;
			}

			wait_frames(1);
			draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);
		}

		//Decrease page when pressing L
		else if((IO_H[152] & 0x200) == 0)
		{
			page--;
			if(page > 4) { page = 3; }

			update = true;
			update_all = true;

			highlight_cursor.x = 128;
			highlight_cursor.y = 18;
			page_x = 0;
			page_y = 0;
			last_x = 0;
			last_y = 0;
			wait_frames(1);
			
			switch(page)
			{
				case 0: draw_bitmap(edit_screen_1, edit_screen_1_size, 0, 0, 240); break;
				case 1: draw_bitmap(edit_screen_2, edit_screen_2_size, 0, 0, 240); break;
				case 2: draw_bitmap(edit_screen_3, edit_screen_3_size, 0, 0, 240); break;
				case 3: draw_bitmap(edit_screen_4, edit_screen_4_size, 0, 0, 240); break;
			}

			wait_frames(1);
			draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);
		}

		//Change data value when pressing UP or DOWN
		else if(((IO_H[152] & 0x40) == 0) || ((IO_H[152] & 0x80) == 0))
		{
			update = true;

			//Page 0
			if(page == 0)
			{
				switch(page_y)
				{
					//Name
					case 0:
						if((IO_H[152] & 0x40) == 0) { reply_buffer[page_x + 1]++; }
						else { reply_buffer[page_x + 1]--; }
						break;

					//Age
					case 1:
						if((IO_H[152] & 0x40) == 0) { reply_buffer[0x07]++; }
						else { reply_buffer[0x07]--; }
						break;

					//Height
					case 2:
						if((IO_H[152] & 0x40) == 0) { reply_buffer[0x08]++; }
						else { reply_buffer[0x08]--; }
						break;

					//Weight
					case 3:
						if((IO_H[152] & 0x40) == 0) { reply_buffer[0x09]++; }
						else { reply_buffer[0x09]--; }
						break;

					//Sex
					case 4:
						if((IO_H[152] & 0x40) == 0) { reply_buffer[0x0A]++; }
						else { reply_buffer[0x0A]--; }
						if(reply_buffer[0x0A] > 2) { reply_buffer[0x0A] = 0; }
						break;

					//Step Size
					case 5:
						if((IO_H[152] & 0x40) == 0) { reply_buffer[0x0B]++; }
						else { reply_buffer[0x0B]--; }
						break;
						
				}
			}

			//Page 1
			else if(page == 1)
			{
				switch(page_y)
				{
					//Total Steps
					case 0:
						index = 12;
						break;

					//Total Meters
					case 1:
						index = 15;
						break;

					//Total Days
					case 2:
						temp24 = ((reply_buffer[21] << 8) | (reply_buffer[22]));

						if((IO_H[152] & 0x40) == 0) { temp24 += 5; }
						else { temp24 -= 5; }

						reply_buffer[21] = (temp24 >> 8);
						reply_buffer[22] = temp24;

						break;

					//Steps Today
					case 3:
						index = 23;
						break;

					//Kcal Burned Today
					case 4:
						index = 26;
						break;

					//Steps Yesterday
					case 5:
						index = 29;
						break;
				}

				if(page_y != 2)
				{
					temp24 = ((reply_buffer[index] << 16) | (reply_buffer[index + 1] << 8) | (reply_buffer[index + 2]));

					if((IO_H[152] & 0x40) == 0) { temp24 += 5; }
					else { temp24 -= 5; }

					if(temp24 == 1000000) { temp24 = 0; }
					else if(temp24 > 999995) { temp24 = 999995; }

					reply_buffer[index] = (temp24 >> 16);
					reply_buffer[index + 1] = (temp24 >> 8);
					reply_buffer[index + 2] = temp24;
				}
			}

			//Page 2
			else if(page == 2)
			{
				switch(page_y)
				{
					//Steps 2 Days Ago
					case 0:
						index = 32;
						break;

					//Steps 3 Days Ago
					case 1:
						index = 35;
						break;

					//Steps 4 Days Ago
					case 2:
						index = 38;
						break;

					//Steps 5 Days Ago
					case 3:
						index = 41;
						break;

					//Steps 6 Days Ago
					case 4:
						index = 44;
						break;

					//Steps 7 Days Ago
					case 5:
						index = 47;
						break;
				}

				temp24 = ((reply_buffer[index] << 16) | (reply_buffer[index + 1] << 8) | (reply_buffer[index + 2]));

				if((IO_H[152] & 0x40) == 0) { temp24 += 5; }
				else { temp24 -= 5; }

				if(temp24 == 1000000) { temp24 = 0; }
				else if(temp24 > 999995) { temp24 = 999995; }

				reply_buffer[index] = (temp24 >> 16);
				reply_buffer[index + 1] = (temp24 >> 8);
				reply_buffer[index + 2] = temp24;
			}

			//Page 3
			else if(page == 3)
			{
				switch(page_y)
				{
					//Kcal Burned 1 Day Ago
					case 0:
						index = 50;
						break;

					//Kcal Burned 2 Days Ago
					case 1:
						index = 53;
						break;

					//Kcal Burned 3 Days Ago
					case 2:
						index = 56;
						break;

					//Kcal Burned 4 Days Ago
					case 3:
						index = 59;
						break;

					//Kcal Burned 5 Days Ago
					case 4:
						index = 62;
						break;

					//Kcal Burned 6 Days Ago
					case 5:
						index = 65;
						break;
				}

				temp24 = ((reply_buffer[index] << 16) | (reply_buffer[index + 1] << 8) | (reply_buffer[index + 2]));

				if((IO_H[152] & 0x40) == 0) { temp24 += 5; }
				else { temp24 -= 5; }

				if(temp24 == 1000000) { temp24 = 0; }
				else if(temp24 > 999995) { temp24 = 999995; }

				reply_buffer[index] = (temp24 >> 16);
				reply_buffer[index + 1] = (temp24 >> 8);
				reply_buffer[index + 2] = temp24;
			}
		}

		//Change edit position of current item when pressing LEFT or RIGHT
		else if(((IO_H[152] & 0x10) == 0) || ((IO_H[152] & 0x20) == 0))
		{
			update = true;

			if((IO_H[152] & 0x10) == 0)
			{
				page_x++;
				highlight_cursor.x += 18;
			}
				
			else
			{
				page_x--;
				highlight_cursor.x -= 18;
			}

			if(page_x == 0xFF)
			{
				page_x = 0;
				highlight_cursor.x = 128;
			}

			else if(page_x > page_limit[page][page_y])
			{
				page_x = page_limit[page][page_y];
				highlight_cursor.x = 128 + (18 * page_limit[page][page_y]);
			}

			wait_frames(30);
		}

		//Cycle through edit items when pressing A
		else if((IO_H[152] & 0x01) == 0)
		{
			update = true;

			//Page 0, 1
			if((page == 0) || (page == 1) || (page == 2) || (page == 3))
			{
				page_y++;
				page_x = 0;

				highlight_cursor.y += 19;
				highlight_cursor.x = 128;

				if(page_y == 6)
				{
					page_y = 0;
					highlight_cursor.y = 18;
				}
			}

			wait_frames(20);
		}

		//Draw Pages
		if(update)
		{
			u8 update_id = (page_y * 6) + page_x;

			switch(page)
			{
				//PAGE 0
				case 0:
					//Clear highlight data and draw new one
					wait_frames(4);
					clear_highlight(edit_screen_1, last_x, last_y);
					draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);

					//Update all entries when switching pages
					if(update_all)
					{
						update_all = false;

						draw_font_cc(font_kana, reply_buffer[1], 129, 19, 0x7FFF);
						draw_font_cc(font_kana, reply_buffer[2], 147, 19, 0x7FFF);
						draw_font_cc(font_kana, reply_buffer[3], 165, 19, 0x7FFF);
						draw_font_cc(font_kana, reply_buffer[4], 183, 19, 0x7FFF);
						draw_font_cc(font_kana, reply_buffer[5], 201, 19, 0x7FFF);
						draw_font_cc(font_kana, reply_buffer[6], 219, 19, 0x7FFF);

						draw_font_cc(font_num, (reply_buffer[7] / 100), 129, 38, 0x7FFF);
						draw_font_cc(font_num, ((reply_buffer[7] / 10) % 10), 147, 38, 0x7FFF);
						draw_font_cc(font_num, (reply_buffer[7] % 10), 165, 38, 0x7FFF);

						draw_font_cc(font_num, (reply_buffer[8] / 100), 129, 57, 0x7FFF);
						draw_font_cc(font_num, ((reply_buffer[8] / 10) % 10), 147, 57, 0x7FFF);
						draw_font_cc(font_num, (reply_buffer[8] % 10), 165, 57, 0x7FFF);

						draw_font_cc(font_num, (reply_buffer[9] / 100), 129, 76, 0x7FFF);
						draw_font_cc(font_num, ((reply_buffer[9] / 10) % 10), 147, 76, 0x7FFF);
						draw_font_cc(font_num, (reply_buffer[9] % 10), 165, 76, 0x7FFF);

						if(!reply_buffer[10]) { draw_font_cc(font_kana, 0, 129, 95, 0x7FFF); }
						else if(reply_buffer[10] == 1) { draw_font_cc(font_kana, 0xCD, 129, 95, 0x7FFF); } 
						else { draw_font_cc(font_kana, 0xC6, 129, 95, 0x7FFF); }

						draw_font_cc(font_num, (reply_buffer[11] / 100), 129, 114, 0x7FFF);
						draw_font_cc(font_num, ((reply_buffer[11] / 10) % 10), 147, 114, 0x7FFF);
						draw_font_cc(font_num, (reply_buffer[11] % 10), 165, 114, 0x7FFF);

						break;
					}

					//Update specific entries
					switch(update_id)
					{
						case 0: clear_char(edit_screen_1, 129, 19); draw_font_cc(font_kana, reply_buffer[1], 129, 19, 0x7FFF); break;
						case 1: clear_char(edit_screen_1, 147, 19); draw_font_cc(font_kana, reply_buffer[2], 147, 19, 0x7FFF); break;
						case 2: clear_char(edit_screen_1, 165, 19); draw_font_cc(font_kana, reply_buffer[3], 165, 19, 0x7FFF); break;
						case 3: clear_char(edit_screen_1, 183, 19); draw_font_cc(font_kana, reply_buffer[4], 183, 19, 0x7FFF); break;
						case 4: clear_char(edit_screen_1, 201, 19); draw_font_cc(font_kana, reply_buffer[5], 201, 19, 0x7FFF); break;
						case 5: clear_char(edit_screen_1, 219, 19); draw_font_cc(font_kana, reply_buffer[6], 219, 19, 0x7FFF); break;

						case 6:
						case 7:
						case 8:
							clear_char(edit_screen_1, 129, 38); draw_font_cc(font_num, (reply_buffer[7] / 100), 129, 38, 0x7FFF);
							clear_char(edit_screen_1, 147, 38); draw_font_cc(font_num, ((reply_buffer[7] / 10) % 10), 147, 38, 0x7FFF);
							clear_char(edit_screen_1, 165, 38); draw_font_cc(font_num, (reply_buffer[7] % 10), 165, 38, 0x7FFF);
							break;

						case 12:
						case 13:
						case 14:
							clear_char(edit_screen_1, 129, 57); draw_font_cc(font_num, (reply_buffer[8] / 100), 129, 57, 0x7FFF);
							clear_char(edit_screen_1, 147, 57); draw_font_cc(font_num, ((reply_buffer[8] / 10) % 10), 147, 57, 0x7FFF);
							clear_char(edit_screen_1, 165, 57); draw_font_cc(font_num, (reply_buffer[8] % 10), 165, 57, 0x7FFF);
							break;

						case 18:
						case 19:
						case 20:
							clear_char(edit_screen_1, 129, 76); draw_font_cc(font_num, (reply_buffer[9] / 100), 129, 76, 0x7FFF);
							clear_char(edit_screen_1, 147, 76); draw_font_cc(font_num, ((reply_buffer[9] / 10) % 10), 147, 76, 0x7FFF);
							clear_char(edit_screen_1, 165, 76); draw_font_cc(font_num, (reply_buffer[9] % 10), 165, 76, 0x7FFF);
							break;

						case 24:
							clear_char(edit_screen_1, 129, 95);
							if(!reply_buffer[10]) { draw_font_cc(font_kana, 0, 129, 95, 0x7FFF); }
							else if(reply_buffer[10] == 1) { draw_font_cc(font_kana, 0xCD, 129, 95, 0x7FFF); } 
							else { draw_font_cc(font_kana, 0xC6, 129, 95, 0x7FFF); }
							break;

						case 30:
						case 31:
						case 32:
							clear_char(edit_screen_1, 129, 114); draw_font_cc(font_num, (reply_buffer[11] / 100), 129, 114, 0x7FFF);
							clear_char(edit_screen_1, 147, 114); draw_font_cc(font_num, ((reply_buffer[11] / 10) % 10), 147, 114, 0x7FFF);
							clear_char(edit_screen_1, 165, 114); draw_font_cc(font_num, (reply_buffer[11] % 10), 165, 114, 0x7FFF);
							break;
					}

					break;

				//PAGE 1
				case 1:
					//Clear highlight data and draw new one
					wait_frames(1);
					clear_highlight(edit_screen_2, last_x, last_y);
					draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);

					//Update all entries when switching pages
					if(update_all)
					{
						update_all = false;

						temp24 = ((reply_buffer[12] << 16) | (reply_buffer[13] << 8) | (reply_buffer[14]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 19, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 19, 0x7FFF);

						temp24 = ((reply_buffer[15] << 16) | (reply_buffer[16] << 8) | (reply_buffer[17]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 38, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 38, 0x7FFF);

						temp24 = ((reply_buffer[21] << 8) | (reply_buffer[22]));
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 129, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 147, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 165, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 183, 57, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 201, 57, 0x7FFF);

						temp24 = ((reply_buffer[23] << 16) | (reply_buffer[24] << 8) | (reply_buffer[25]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 76, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 76, 0x7FFF);

						temp24 = ((reply_buffer[26] << 16) | (reply_buffer[27] << 8) | (reply_buffer[28]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 95, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 95, 0x7FFF);

						temp24 = ((reply_buffer[29] << 16) | (reply_buffer[30] << 8) | (reply_buffer[31]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 114, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 114, 0x7FFF);

						break;
					}

					//Update specific entries
					switch(update_id)
					{
						case 0:
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
							temp24 = ((reply_buffer[12] << 16) | (reply_buffer[13] << 8) | (reply_buffer[14]));
							clear_char(edit_screen_2, 129, 19); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 19, 0x7FFF);
							clear_char(edit_screen_2, 147, 19); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 19, 0x7FFF);
							clear_char(edit_screen_2, 165, 19); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 19, 0x7FFF);
							clear_char(edit_screen_2, 183, 19); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 19, 0x7FFF);
							clear_char(edit_screen_2, 201, 19); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 19, 0x7FFF);
							clear_char(edit_screen_2, 219, 19); draw_font_cc(font_num, (temp24 % 10), 219, 19, 0x7FFF);

							break;

						case 6:
						case 7:
						case 8:
						case 9:
						case 10:
						case 11:
							temp24 = ((reply_buffer[15] << 16) | (reply_buffer[16] << 8) | (reply_buffer[17]));
							clear_char(edit_screen_2, 129, 38); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 38, 0x7FFF);
							clear_char(edit_screen_2, 147, 38); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 38, 0x7FFF);
							clear_char(edit_screen_2, 165, 38); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 38, 0x7FFF);
							clear_char(edit_screen_2, 183, 38); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 38, 0x7FFF);
							clear_char(edit_screen_2, 201, 38); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 38, 0x7FFF);
							clear_char(edit_screen_2, 219, 38); draw_font_cc(font_num, (temp24 % 10), 219, 38, 0x7FFF);

							break;

						case 12:
						case 13:
						case 14:
						case 15:
						case 16:
							temp24 = ((reply_buffer[21] << 8) | (reply_buffer[22]));
							clear_char(edit_screen_2, 129, 57); draw_font_cc(font_num, ((temp24 / 10000) % 10), 129, 57, 0x7FFF);
							clear_char(edit_screen_2, 147, 57); draw_font_cc(font_num, ((temp24 / 1000) % 10), 147, 57, 0x7FFF);
							clear_char(edit_screen_2, 165, 57); draw_font_cc(font_num, ((temp24 / 100) % 10), 165, 57, 0x7FFF);
							clear_char(edit_screen_2, 183, 57); draw_font_cc(font_num, ((temp24 / 10) % 10), 183, 57, 0x7FFF);
							clear_char(edit_screen_2, 201, 57); draw_font_cc(font_num, (temp24 % 10), 201, 57, 0x7FFF);

							break;

						case 18:
						case 19:
						case 20:
						case 21:
						case 22:
						case 23:
							temp24 = ((reply_buffer[23] << 16) | (reply_buffer[24] << 8) | (reply_buffer[25]));
							clear_char(edit_screen_2, 129, 76); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 76, 0x7FFF);
							clear_char(edit_screen_2, 147, 76); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 76, 0x7FFF);
							clear_char(edit_screen_2, 165, 76); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 76, 0x7FFF);
							clear_char(edit_screen_2, 183, 76); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 76, 0x7FFF);
							clear_char(edit_screen_2, 201, 76); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 76, 0x7FFF);
							clear_char(edit_screen_2, 219, 76); draw_font_cc(font_num, (temp24 % 10), 219, 76, 0x7FFF);

							break;

						case 24:
						case 25:
						case 26:
						case 27:
						case 28:
						case 29:
							temp24 = ((reply_buffer[26] << 16) | (reply_buffer[27] << 8) | (reply_buffer[28]));
							clear_char(edit_screen_2, 129, 95); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 95, 0x7FFF);
							clear_char(edit_screen_2, 147, 95); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 95, 0x7FFF);
							clear_char(edit_screen_2, 165, 95); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 95, 0x7FFF);
							clear_char(edit_screen_2, 183, 95); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 95, 0x7FFF);
							clear_char(edit_screen_2, 201, 95); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 95, 0x7FFF);
							clear_char(edit_screen_2, 219, 95); draw_font_cc(font_num, (temp24 % 10), 219, 95, 0x7FFF);

							break;

						case 30:
						case 31:
						case 32:
						case 33:
						case 34:
						case 35:
							temp24 = ((reply_buffer[29] << 16) | (reply_buffer[30] << 8) | (reply_buffer[31]));
							clear_char(edit_screen_2, 129, 114); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 114, 0x7FFF);
							clear_char(edit_screen_2, 147, 114); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 114, 0x7FFF);
							clear_char(edit_screen_2, 165, 114); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 114, 0x7FFF);
							clear_char(edit_screen_2, 183, 114); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 114, 0x7FFF);
							clear_char(edit_screen_2, 201, 114); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 114, 0x7FFF);
							clear_char(edit_screen_2, 219, 114); draw_font_cc(font_num, (temp24 % 10), 219, 114, 0x7FFF);

							break;
					}

					break;

				//PAGE 2
				case 2:
					//Clear highlight data and draw new one
					wait_frames(1);
					clear_highlight(edit_screen_3, last_x, last_y);
					draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);

					//Update all entries when switching pages
					if(update_all)
					{
						update_all = false;

						temp24 = ((reply_buffer[32] << 16) | (reply_buffer[33] << 8) | (reply_buffer[34]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 19, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 19, 0x7FFF);

						temp24 = ((reply_buffer[35] << 16) | (reply_buffer[36] << 8) | (reply_buffer[37]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 38, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 38, 0x7FFF);

						temp24 = ((reply_buffer[38] << 16) | (reply_buffer[39] << 8) | (reply_buffer[40]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 57, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 57, 0x7FFF);

						temp24 = ((reply_buffer[41] << 16) | (reply_buffer[42] << 8) | (reply_buffer[43]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 76, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 76, 0x7FFF);

						temp24 = ((reply_buffer[44] << 16) | (reply_buffer[45] << 8) | (reply_buffer[46]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 95, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 95, 0x7FFF);

						temp24 = ((reply_buffer[47] << 16) | (reply_buffer[48] << 8) | (reply_buffer[49]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 114, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 114, 0x7FFF);

						break;
					}

					//Update specific entries
					switch(update_id)
					{
						case 0:
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
							temp24 = ((reply_buffer[32] << 16) | (reply_buffer[33] << 8) | (reply_buffer[34]));
							clear_char(edit_screen_3, 129, 19); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 19, 0x7FFF);
							clear_char(edit_screen_3, 147, 19); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 19, 0x7FFF);
							clear_char(edit_screen_3, 165, 19); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 19, 0x7FFF);
							clear_char(edit_screen_3, 183, 19); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 19, 0x7FFF);
							clear_char(edit_screen_3, 201, 19); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 19, 0x7FFF);
							clear_char(edit_screen_3, 219, 19); draw_font_cc(font_num, (temp24 % 10), 219, 19, 0x7FFF);

							break;

						case 6:
						case 7:
						case 8:
						case 9:
						case 10:
						case 11:
							temp24 = ((reply_buffer[35] << 16) | (reply_buffer[36] << 8) | (reply_buffer[37]));
							clear_char(edit_screen_3, 129, 38); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 38, 0x7FFF);
							clear_char(edit_screen_3, 147, 38); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 38, 0x7FFF);
							clear_char(edit_screen_3, 165, 38); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 38, 0x7FFF);
							clear_char(edit_screen_3, 183, 38); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 38, 0x7FFF);
							clear_char(edit_screen_3, 201, 38); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 38, 0x7FFF);
							clear_char(edit_screen_3, 219, 38); draw_font_cc(font_num, (temp24 % 10), 219, 38, 0x7FFF);

							break;

						case 12:
						case 13:
						case 14:
						case 15:
						case 16:
							temp24 = ((reply_buffer[38] << 16) | (reply_buffer[39] << 8) | (reply_buffer[40]));
							clear_char(edit_screen_3, 129, 57); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 57, 0x7FFF);
							clear_char(edit_screen_3, 147, 57); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 57, 0x7FFF);
							clear_char(edit_screen_3, 165, 57); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 57, 0x7FFF);
							clear_char(edit_screen_3, 183, 57); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 57, 0x7FFF);
							clear_char(edit_screen_3, 201, 57); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 57, 0x7FFF);
							clear_char(edit_screen_3, 219, 57); draw_font_cc(font_num, (temp24 % 10), 219, 57, 0x7FFF);

							break;

						case 18:
						case 19:
						case 20:
						case 21:
						case 22:
						case 23:
							temp24 = ((reply_buffer[41] << 16) | (reply_buffer[42] << 8) | (reply_buffer[43]));
							clear_char(edit_screen_3, 129, 76); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 76, 0x7FFF);
							clear_char(edit_screen_3, 147, 76); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 76, 0x7FFF);
							clear_char(edit_screen_3, 165, 76); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 76, 0x7FFF);
							clear_char(edit_screen_3, 183, 76); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 76, 0x7FFF);
							clear_char(edit_screen_3, 201, 76); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 76, 0x7FFF);
							clear_char(edit_screen_3, 219, 76); draw_font_cc(font_num, (temp24 % 10), 219, 76, 0x7FFF);

							break;

						case 24:
						case 25:
						case 26:
						case 27:
						case 28:
						case 29:
							temp24 = ((reply_buffer[44] << 16) | (reply_buffer[45] << 8) | (reply_buffer[46]));
							clear_char(edit_screen_3, 129, 95); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 95, 0x7FFF);
							clear_char(edit_screen_3, 147, 95); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 95, 0x7FFF);
							clear_char(edit_screen_3, 165, 95); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 95, 0x7FFF);
							clear_char(edit_screen_3, 183, 95); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 95, 0x7FFF);
							clear_char(edit_screen_3, 201, 95); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 95, 0x7FFF);
							clear_char(edit_screen_3, 219, 95); draw_font_cc(font_num, (temp24 % 10), 219, 95, 0x7FFF);

							break;

						case 30:
						case 31:
						case 32:
						case 33:
						case 34:
						case 35:
							temp24 = ((reply_buffer[47] << 16) | (reply_buffer[48] << 8) | (reply_buffer[49]));
							clear_char(edit_screen_3, 129, 114); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 114, 0x7FFF);
							clear_char(edit_screen_3, 147, 114); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 114, 0x7FFF);
							clear_char(edit_screen_3, 165, 114); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 114, 0x7FFF);
							clear_char(edit_screen_3, 183, 114); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 114, 0x7FFF);
							clear_char(edit_screen_3, 201, 114); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 114, 0x7FFF);
							clear_char(edit_screen_3, 219, 114); draw_font_cc(font_num, (temp24 % 10), 219, 114, 0x7FFF);

							break;
					}

					break;

				//PAGE 3
				case 3:
					//Clear highlight data and draw new one
					wait_frames(1);
					clear_highlight(edit_screen_4, last_x, last_y);
					draw_bitmap_cc(highlight, highlight_size, highlight_cursor.x, highlight_cursor.y, 18, 0x7FFF);

					//Update all entries when switching pages
					if(update_all)
					{
						update_all = false;

						temp24 = ((reply_buffer[50] << 16) | (reply_buffer[51] << 8) | (reply_buffer[52]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 19, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 19, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 19, 0x7FFF);

						temp24 = ((reply_buffer[53] << 16) | (reply_buffer[54] << 8) | (reply_buffer[55]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 38, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 38, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 38, 0x7FFF);

						temp24 = ((reply_buffer[56] << 16) | (reply_buffer[57] << 8) | (reply_buffer[58]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 57, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 57, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 57, 0x7FFF);

						temp24 = ((reply_buffer[59] << 16) | (reply_buffer[60] << 8) | (reply_buffer[61]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 76, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 76, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 76, 0x7FFF);

						temp24 = ((reply_buffer[62] << 16) | (reply_buffer[63] << 8) | (reply_buffer[64]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 95, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 95, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 95, 0x7FFF);

						temp24 = ((reply_buffer[65] << 16) | (reply_buffer[67] << 8) | (reply_buffer[68]));
						draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 114, 0x7FFF);
						draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 114, 0x7FFF);
						draw_font_cc(font_num, (temp24 % 10), 219, 114, 0x7FFF);

						break;
					}

					//Update specific entries
					switch(update_id)
					{
						case 0:
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
							temp24 = ((reply_buffer[50] << 16) | (reply_buffer[51] << 8) | (reply_buffer[52]));
							clear_char(edit_screen_4, 129, 19); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 19, 0x7FFF);
							clear_char(edit_screen_4, 147, 19); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 19, 0x7FFF);
							clear_char(edit_screen_4, 165, 19); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 19, 0x7FFF);
							clear_char(edit_screen_4, 183, 19); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 19, 0x7FFF);
							clear_char(edit_screen_4, 201, 19); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 19, 0x7FFF);
							clear_char(edit_screen_4, 219, 19); draw_font_cc(font_num, (temp24 % 10), 219, 19, 0x7FFF);

							break;

						case 6:
						case 7:
						case 8:
						case 9:
						case 10:
						case 11:
							temp24 = ((reply_buffer[53] << 16) | (reply_buffer[54] << 8) | (reply_buffer[55]));
							clear_char(edit_screen_4, 129, 38); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 38, 0x7FFF);
							clear_char(edit_screen_4, 147, 38); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 38, 0x7FFF);
							clear_char(edit_screen_4, 165, 38); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 38, 0x7FFF);
							clear_char(edit_screen_4, 183, 38); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 38, 0x7FFF);
							clear_char(edit_screen_4, 201, 38); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 38, 0x7FFF);
							clear_char(edit_screen_4, 219, 38); draw_font_cc(font_num, (temp24 % 10), 219, 38, 0x7FFF);

							break;

						case 12:
						case 13:
						case 14:
						case 15:
						case 16:
							temp24 = ((reply_buffer[56] << 16) | (reply_buffer[57] << 8) | (reply_buffer[58]));
							clear_char(edit_screen_4, 129, 57); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 57, 0x7FFF);
							clear_char(edit_screen_4, 147, 57); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 57, 0x7FFF);
							clear_char(edit_screen_4, 165, 57); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 57, 0x7FFF);
							clear_char(edit_screen_4, 183, 57); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 57, 0x7FFF);
							clear_char(edit_screen_4, 201, 57); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 57, 0x7FFF);
							clear_char(edit_screen_4, 219, 57); draw_font_cc(font_num, (temp24 % 10), 219, 57, 0x7FFF);

							break;

						case 18:
						case 19:
						case 20:
						case 21:
						case 22:
						case 23:
							temp24 = ((reply_buffer[59] << 16) | (reply_buffer[60] << 8) | (reply_buffer[61]));
							clear_char(edit_screen_4, 129, 76); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 76, 0x7FFF);
							clear_char(edit_screen_4, 147, 76); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 76, 0x7FFF);
							clear_char(edit_screen_4, 165, 76); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 76, 0x7FFF);
							clear_char(edit_screen_4, 183, 76); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 76, 0x7FFF);
							clear_char(edit_screen_4, 201, 76); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 76, 0x7FFF);
							clear_char(edit_screen_4, 219, 76); draw_font_cc(font_num, (temp24 % 10), 219, 76, 0x7FFF);

							break;

						case 24:
						case 25:
						case 26:
						case 27:
						case 28:
						case 29:
							temp24 = ((reply_buffer[62] << 16) | (reply_buffer[63] << 8) | (reply_buffer[64]));
							clear_char(edit_screen_4, 129, 95); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 95, 0x7FFF);
							clear_char(edit_screen_4, 147, 95); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 95, 0x7FFF);
							clear_char(edit_screen_4, 165, 95); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 95, 0x7FFF);
							clear_char(edit_screen_4, 183, 95); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 95, 0x7FFF);
							clear_char(edit_screen_4, 201, 95); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 95, 0x7FFF);
							clear_char(edit_screen_4, 219, 95); draw_font_cc(font_num, (temp24 % 10), 219, 95, 0x7FFF);

							break;

						case 30:
						case 31:
						case 32:
						case 33:
						case 34:
						case 35:
							temp24 = ((reply_buffer[65] << 16) | (reply_buffer[66] << 8) | (reply_buffer[67]));
							clear_char(edit_screen_4, 129, 114); draw_font_cc(font_num, ((temp24 / 100000) % 10), 129, 114, 0x7FFF);
							clear_char(edit_screen_4, 147, 114); draw_font_cc(font_num, ((temp24 / 10000) % 10), 147, 114, 0x7FFF);
							clear_char(edit_screen_4, 165, 114); draw_font_cc(font_num, ((temp24 / 1000) % 10), 165, 114, 0x7FFF);
							clear_char(edit_screen_4, 183, 114); draw_font_cc(font_num, ((temp24 / 100) % 10), 183, 114, 0x7FFF);
							clear_char(edit_screen_4, 201, 114); draw_font_cc(font_num, ((temp24 / 10) % 10), 201, 114, 0x7FFF);
							clear_char(edit_screen_4, 219, 114); draw_font_cc(font_num, (temp24 % 10), 219, 114, 0x7FFF);

							break;
					}

					break;
			}

			update = false;
		}

		last_x = highlight_cursor.x;
		last_y = highlight_cursor.y;
	}

	program_state = 0;

	fade_out(4);

	screen_cursor.state = 0;
	screen_cursor.x = 90;
	screen_cursor.y = 75;

	wait_frames(1);
	draw_bitmap(main_screen, main_screen_size, 0, 0, 240);
	
	wait_frames(1);
	draw_bitmap_cc(cursor, cursor_size, screen_cursor.x, screen_cursor.y, 16, 0x7FFF);

	fade_in(4);
}

void send_data_idle()
{
	//Draw send data screen
	fade_out(4);

	wait_frames(1);
	draw_bitmap(send_screen, send_screen_size, 0, 0, 240);

	fade_in(4);

	//Wait for JoyBus commands and process them
	wait_for_signal();
}

void show_data_idle()
{
	//Return to main screen when pressing L
	if((IO_H[152] & 0x200) == 0)
	{
		setup();

		screen_cursor.state = 0;
		screen_cursor.x = 90;
		screen_cursor.y = 75;

		program_state = 0;
	}

	wait_frames(1);
}

void show_poll_idle()
{
	//Return to main screen when pressing R
	if((IO_H[152] & 0x100) == 0)
	{
		setup();

		screen_cursor.state = 0;
		screen_cursor.x = 90;
		screen_cursor.y = 75;

		program_state = 0;
	}

	wait_frames(1);
}
