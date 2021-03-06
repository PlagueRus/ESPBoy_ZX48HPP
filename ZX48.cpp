#pragma GCC optimize ("-Ofast")
#pragma GCC push_options
//v1.3 12.03.2020 core rewritten to c++, memory and speed optimizations
//v1.2 06.01.2020 bug fixes, onscreen keyboard added, keyboard module support
//v1.1 23.12.2019  z80 format v3 support, improved frameskip, screen and controls config files
//v1.0 20.12.2019 initial version, with sound
//by Shiru
//shiru@mail.ru
//https://www.patreon.com/shiru8bit
//uses Z80 core by Ketmar
#include "User_Setup.h"
#define USER_SETUP_LOADED

#include <arduino.h>

#include <Wire.h>
#include <SPI.h>

#include <Adafruit_MCP23017.h>
#include <Adafruit_MCP4725.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>
#include <sigma_delta.h>

/*
   IMPORTANT: the project consumes a lot of RAM, to allow enough set
	SSL Support to Basic
	IwIP to Lower memory (no features)

   Use 160 MHz for much better performance.

   Make sure all the display driver and pin comnenctions are correct by
   editting the User_Setup.h file in the TFT_eSPI library folder.

   Set SPI_FREQUENCY to 39000000 for best performance.

   Games should be uploaded into SPIFFS as 48K .z80 snapshots (v1,v2,v3)
   You can create such snapshots using ZXSPIN emulator

   You can also put a 6912 byte screen with the exact same name to be displayed before game

   You can provide an optional controls configuration file to provide convinient way to
   control a game. Such file has extension of .cfg, it is a plain text file that contains
   a string 8 characters long. The characters may represent a key A-Z, 0-9, _ for space,
   $ for Enter, @ for CS, # for SS
   Warning! This file overrides manual controls selection in file manager

   The order of characters is UP,DOWN,LEFT,RIGHT,ACT,ESC,LFT,RGT
   for example: QAOPM_0$

   I.e. a game file set may look like:

   game.z80 - snapshot
   game.scr - splash screen in native ZX Spectrum format
*/

#include "zymosis.hpp"

#include "glcdfont.c"
#include "gfx/espboy.h"
#include "gfx/keyboard.h"
#include "rom/rom.h"

#define MCP23017address 0 // actually it's 0x20 but in <Adafruit_MCP23017.h> lib there is (x|0x20) :)
#define MCP4725address 0x60
#define LEDPIN         D4
#define SOUNDPIN       D3
#define csTFTMCP23017pin 8 //SPI for LCD

Adafruit_MCP23017 mcp;
Adafruit_MCP23017 mcpKeyboard;
Adafruit_MCP4725 dac;

TFT_eSPI tft = TFT_eSPI();

uint8_t pad_state;
uint8_t pad_state_prev;
uint8_t pad_state_t;
uint8_t keybModuleExist;

#define PAD_LEFT        0x01
#define PAD_UP          0x02
#define PAD_DOWN        0x04
#define PAD_RIGHT       0x08
#define PAD_ACT         0x10
#define PAD_ESC         0x20
#define PAD_LFT         0x40
#define PAD_RGT         0x80
#define PAD_ANY         0xff

uint16_t line_buffer[128];

bool border_changed = false;

uint8_t line_change[24]; //bit mask to updating each line
char filename[32];

uint8_t port_fe;  //keyboard, tape, sound, border
uint8_t port_1f;  //kempston joystick

constexpr uint_fast32_t ZX_CLOCK_FREQ = 3500000;
constexpr uint_fast32_t ZX_FRAME_RATE = 50;
constexpr uint_fast32_t SAMPLE_RATE = 48000;   //more is better, but emulations gets slower
constexpr uint_fast32_t MAX_FRAMESKIP = 8;

#define RGB565Q(r,g,b)    ( ((((r)>>5)&0x1f)<<11) | ((((g)>>4)&0x3f)<<5) | (((b)>>5)&0x1f) )
inline uint16_t LHSWAP(uint16_t w) { return (w >> 8) | (w << 8); }

enum {
	K_CS = 0, K_Z, K_X, K_C, K_V,
	K_A, K_S, K_D, K_F, K_G,
	K_Q, K_W, K_E, K_R, K_T,
	K_1, K_2, K_3, K_4, K_5,
	K_0, K_9, K_8, K_7, K_6,
	K_P, K_O, K_I, K_U, K_Y,
	K_ENTER, K_L, K_K, K_J, K_H,
	K_SPACE, K_SS, K_M, K_N, K_B,
	K_DEL, K_LED, K_NULL = 255,
};

constexpr uint8_t keybCurrent[7][5] PROGMEM = {
	{K_Q, K_E, K_R, K_U, K_O},
	{K_W, K_S, K_G, K_H, K_L},
	{255, K_D, K_T, K_Y, K_I},
	{K_A, K_P, K_SS, K_ENTER, K_DEL},
	{K_SPACE, K_Z, K_C, K_N, K_M},
	{K_CS, K_X, K_V, K_B, K_6},
	{K_LED, K_SS, K_F, K_J, K_K}
};

constexpr uint8_t keybCurrent2[7][5] PROGMEM = {
	{K_Q, K_2, K_3, K_U, K_O},
	{K_1, K_4, K_G, K_H, K_L},
	{K_NULL, K_5, K_T, K_Y, K_I},
	{K_A, K_P, K_SS, K_ENTER, K_DEL},
	{K_SPACE, K_7, K_9, K_N, K_M},
	{K_CS, K_8, K_V, K_B, K_6},
	{K_0, K_SS, K_6, K_J, K_K}
};

constexpr uint8_t keybOnscrMatrix[2][20] PROGMEM = {
	{K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9, K_0, K_Q, K_W, K_E, K_R, K_T, K_Y, K_U, K_I, K_O, K_P},
	{K_A, K_S, K_D, K_F, K_G, K_H, K_J, K_K, K_L, K_ENTER, K_CS, K_Z, K_X, K_C, K_V, K_B, K_N, K_M, K_SS, K_SPACE},
};

constexpr uint8_t keybOnscr[2][21] PROGMEM = {
"1234567890QWERTYUIOP",
"ASDFGHJKLecZXCVBNMs_",
};

#undef min // defined in TFT_SPI but breaks the bitset library
#include <bitset>
std::bitset<41> key_matriz; // 8 bytes

uint8_t control_type;

uint8_t control_pad_l;
uint8_t control_pad_r;
uint8_t control_pad_u;
uint8_t control_pad_d;
uint8_t control_pad_act;
uint8_t control_pad_esc;
uint8_t control_pad_lft;
uint8_t control_pad_rgt;


enum {
	CONTROL_PAD_KEYBOARD,
	CONTROL_PAD_KEMPSTON
};

volatile uint8_t sound_dac;

constexpr size_t SOUND_BUFFER_SIZE = (SAMPLE_RATE / ZX_FRAME_RATE * 2);
constexpr size_t SOUND_MIN_GAP = SOUND_BUFFER_SIZE / 10;

volatile uint8_t* sound_buffer; // pointer to volatile array
volatile uint16_t sound_wr_ptr;
volatile uint16_t sound_rd_ptr;


class str_ext { // is constexpr file-ext string class
private:
	const char* const p_;
	const uint8_t sz_;
public:

	template<size_t N>
	constexpr str_ext(const char(&a)[N]) : // ctor
		p_(a), sz_(N - 1) {}
	constexpr char operator[](size_t n) const { // []
		return n < sz_ ? p_[n] : 0;
	}
	constexpr size_t size() const { return sz_; } // size()

	constexpr uint32_t toUint32() const
	{
#if BYTE_ORDER == LITTLE_ENDIAN
		return p_[0] + (p_[1] << 8) + (p_[2] << 16);
#else
		return (p_[0] << 24) + (p_[1] << 16) + (p_[2] << 8);
#endif
	}
};

enum {
	EXT_Z80 = str_ext("z80").toUint32(),
	EXT_SCR = str_ext("scr").toUint32(),
	EXT_CFG = str_ext("cfg").toUint32(),
};

constexpr size_t MEMORY_SIZE = 0xC000;
uint8_t* memory; //49152 bytes

class Z48_ESPBoy : protected zymosis::Z80CallBacks
{
protected:
	Z48_ESPBoy()
	{
	}

	ZYMOSIS_INLINE void reset()
	{
		memset(memory, 0, MEMORY_SIZE);
		memset(line_change, 0xff, sizeof(line_change));

		key_matriz.reset();

		port_fe = 0;
		port_1f = 0;
	}

	ZYMOSIS_INLINE void memWriteFn(uint16_t addr, uint8_t value, zymosis::Z80MemIOType mio)
	{
		uint16_t line;

		if (addr >= 0x4000)
		{
			addr -= 0x4000;
			if (addr < 0x1b00)
			{
				if (memory[addr] != value)
				{
					if (addr < 0x1800)
					{
						line = ((addr / 256) & 7) + ((addr / 32) & 7) * 8 + addr / 2048 * 64;
						line_change[line / 8] |= (1 << (line & 7));
					}
					else
					{
						line_change[(addr - 0x1800) / 32] = 255;
					}
				}
			}

			memory[addr] = value;
		}
	}

	ZYMOSIS_INLINE uint8_t memReadFn(uint16_t addr, zymosis::Z80MemIOType mio)
	{
		if (addr < 0x4000)
		{
			return pgm_read_byte(&rom[addr]);
		}
		else
		{
			return memory[addr - 0x4000];
		}
	}

	ZYMOSIS_INLINE uint8_t portInFn(uint16_t port, zymosis::Z80PIOType pio)
	{
		uint8_t val;
		uint16_t off;

		val = 0xff;

		if (!(port & 0x01)) //port #fe
		{
			off = 0;

			if (!(port & 0x0100)) off = 5 * 0;
			if (!(port & 0x0200)) off = 5 * 1;
			if (!(port & 0x0400)) off = 5 * 2;
			if (!(port & 0x0800)) off = 5 * 3;
			if (!(port & 0x1000)) off = 5 * 4;
			if (!(port & 0x2000)) off = 5 * 5;
			if (!(port & 0x4000)) off = 5 * 6;
			if (!(port & 0x8000)) off = 5 * 7;

			if (key_matriz[off + 0]) val &= ~0x01;
			if (key_matriz[off + 1]) val &= ~0x02;
			if (key_matriz[off + 2]) val &= ~0x04;
			if (key_matriz[off + 3]) val &= ~0x08;
			if (key_matriz[off + 4]) val &= ~0x10;
		}
		else
		{
			if ((port & 0xff) == 0x1f) val = port_1f;
		}

		return val;
	}

	ZYMOSIS_INLINE void portOutFn(uint16_t port, uint8_t value, zymosis::Z80PIOType pio)
	{
		if (!(port & 0x01))
		{
			if ((port_fe & 7) != (value & 7)) border_changed = 1; //update border

			port_fe = value;
		}
	}
public:
	ZYMOSIS_INLINE void emulateFrame()
	{
		uint_fast32_t n, ticks, sacc, sout;

		zymosis::Z80Cpu<Z48_ESPBoy>* zcpu = reinterpret_cast<zymosis::Z80Cpu<Z48_ESPBoy>*>(this);

		sacc = 0;
		sout = 0;
		ticks = zcpu->Z80_Interrupt();

		while (ticks < (ZX_CLOCK_FREQ / ZX_FRAME_RATE))
		{
			n = zcpu->Z80_ExecuteTS(8);

			sacc += n;

			if (port_fe & 0x10) sout += 127 * n;

			if (sacc >= (ZX_CLOCK_FREQ / SAMPLE_RATE))
			{
				sound_buffer[sound_wr_ptr] = sout / sacc;

				if (sound_wr_ptr != sound_rd_ptr)
				{
					++sound_wr_ptr;

					if (sound_wr_ptr >= SOUND_BUFFER_SIZE) sound_wr_ptr = 0;
				}

				sacc -= ZX_CLOCK_FREQ / SAMPLE_RATE;
				sout = 0;
			}

			ticks += n;
		}
	}

	ZYMOSIS_INLINE void renderFrame()
	{
		uint16_t ch, ln, px, row, aptr, optr, attr, pptr1, pptr2, bright;
		uint_fast16_t ink, pap;
		uint_fast16_t col = 0;
		uint8_t line1, line2;

		const uint_fast16_t palette[16] = {
		  RGB565Q(0, 0, 0),
		  RGB565Q(0, 29, 200),
		  RGB565Q(216, 36, 15),
		  RGB565Q(213, 48, 201),
		  RGB565Q(0, 199, 33),
		  RGB565Q(0, 201, 203),
		  RGB565Q(206, 202, 39),
		  RGB565Q(203, 203, 203),
		  RGB565Q(0, 0, 0),
		  RGB565Q(0, 39, 251),
		  RGB565Q(255, 48, 22),
		  RGB565Q(255, 63, 252),
		  RGB565Q(0, 249, 44),
		  RGB565Q(0, 252, 254),
		  RGB565Q(255, 253, 51),
		  RGB565Q(255, 255, 255),
		};


		if (border_changed)
		{
			tft.startWrite();
			border_changed = false;

			col = LHSWAP(palette[port_fe & 7] << 2);
			tft.setAddrWindow(0, 0, 128, 16);
			tft.writeColor(col, 2048);
			tft.setAddrWindow(0, 112, 128, 16);
			tft.writeColor(col, 2048);
			tft.endWrite();
		}

		row = 16;

		for (ln = 0; ln < 192; ln += 2)
		{
			if (!(line_change[ln / 8] & (3 << (ln & 7))))
			{
				++row;
				continue;
			}

			line_change[ln / 8] &= ~(3 << (ln & 7));

			pptr1 = (ln & 7) * 256 + ((ln / 8) & 7) * 32 + (ln / 64) * 2048;
			pptr2 = pptr1 + 256;
			aptr = 6144 + ln / 8 * 32;
			optr = 0;

			for (ch = 0; ch < 32; ++ch)
			{
				attr = memory[aptr++];
				bright = (attr & 0x40) ? 8 : 0;
				ink = palette[(attr & 7) + bright];
				pap = palette[((attr >> 3) & 7) + bright];

				line1 = memory[pptr1++];
				line2 = memory[pptr2++];
				px = 4;
				while (px--)
				{
					switch ((line1 >> 6) | ((line2 & 0xC0) >> 4))
					{
					case 0x00: col = pap * 4; break;
					case 0x01:
					case 0x02:
					case 0x04:
					case 0x08: col = ink + pap * 3; break;
					case 0x07:
					case 0x0B:
					case 0x0D:
					case 0x0E: col = ink * 3 + pap; break;
					case 0x0F: col = ink * 4; break;
					default: col = ink * 2 + pap * 2;
					}

					line_buffer[optr++] = col;

					line1 <<= 2;
					line2 <<= 2;
				}
			}

			tft.startWrite();
			tft.setAddrWindow(0, row++, 128, 1);
			tft.pushColors(line_buffer, 128, true);
			tft.endWrite();
		}
	}

	void unrle(uint8_t* mem, size_t sz)
	{
		int i, len;
		size_t ptr;
		uint8_t val;

		ptr = 0;

		while (ptr < sz - 4)
		{
			if (mem[ptr] == 0xed && mem[ptr + 1] == 0xed)
			{
				len = mem[ptr + 2];
				val = mem[ptr + 3];

				memmove(&mem[ptr + len], &mem[ptr + 4], sz - (ptr + len));  //not memcpy, because of the overlapping

				for (i = 0; i < len; ++i) mem[ptr++] = val;
			}
			else
			{
				++ptr;
			}
		}
	}

	uint8_t load_z80(const char* filename)
	{
		uint8_t header[30];
		int sz, len, ptr;
		uint8_t rle;

		fs::File f = SPIFFS.open(filename, "r");

		if (!f) return 0;

		sz = f.size();
		f.readBytes((char*)header, sizeof(header));
		sz -= sizeof(header);

		af.a = header[0];
		af.f = header[1];
		bc.c = header[2];
		bc.b = header[3];
		hl.l = header[4];
		hl.h = header[5];
		pc = header[6] + header[7] * 256;
		sp.l = header[8];
		sp.h = header[9];
		regI = header[10];
		regR = header[11];

		if (header[12] == 255) header[12] = 1;

		rle = header[12] & 0x20;
		port_fe = (header[12] >> 1) & 7;

		de.e = header[13];
		de.d = header[14];

		bcx.c = header[15];
		bcx.b = header[16];
		dex.e = header[17];
		dex.d = header[18];
		hlx.l = header[19];
		hlx.h = header[20];
		afx.a = header[21];
		afx.f = header[22];

		ix.l = header[23];
		ix.h = header[24];
		iy.l = header[25];
		iy.h = header[26];

		if (!(header[27] & 1)) //di
		{
			iff1 = 0;
		}
		else
		{
			iff1 = 1;
			prev_was_EIDDR = 1;
		}

		iff2 = header[28];
		im = (header[29] & 3);

		if (pc) //v1 format
		{
			f.readBytes((char*)memory, sz);

			if (rle) unrle(memory, 16384 * 3);
		}
		else  //v2 or v3 format, features an extra header
		{
			//read actual PC from the extra header, skip rest of the extra header

			f.readBytes((char*)header, 4);
			sz -= 4;

			len = header[0] + header[1] * 256 + 2 - 4;
			pc = header[2] + header[3] * 256;

			f.seek(len, fs::SeekCur);
			sz -= len;

			//unpack 16K pages

			while (sz > 0)
			{
				f.readBytes((char*)header, 3);
				sz -= 3;

				len = header[0] + header[1] * 256;

				switch (header[2])
				{
				case 4: ptr = 0x8000; break;
				case 5: ptr = 0xc000; break;
				case 8: ptr = 0x4000; break;
				default:
					ptr = 0;
				}

				if (ptr)
				{
					ptr -= 0x4000;

					f.readBytes((char*)&memory[ptr], len);
					sz -= len;

					if (len < 0xffff) unrle(&memory[ptr], 16384);
				}
				else
				{
					f.seek(len, fs::SeekCur);
					sz -= len;
				}
			}
		}

		f.close();

		return 1;
	}

	uint8_t load_scr(const char* filename)
	{
		fs::File f = SPIFFS.open(filename, "r");

		if (!f) return 0;

		f.readBytes((char*)memory, 6912);
		f.close();

		memset(line_change, 0xff, sizeof(line_change));

		return 1;
	}
};

zymosis::Z80Cpu<Z48_ESPBoy> cpu;

int check_key()
{
	pad_state_prev = pad_state;
	pad_state = ~mcp.readGPIOAB() & 255;
	pad_state_t = (pad_state ^ pad_state_prev) & pad_state;
	return pad_state;
}

//0 no timeout, otherwise timeout in ms

void wait_any_key(int timeout)
{
	timeout /= 100;

	while (1)
	{
		check_key();

		if (pad_state_t & PAD_ANY) break;

		if (timeout)
		{
			--timeout;

			if (timeout <= 0) break;
		}

		delay(100);
	}
}


//render part of a 8-bit uncompressed BMP file
//no clipping
//uses line buffer to draw it much faster than through writePixel

void drawBMP8Part(int16_t x, int16_t y, const uint8_t bitmap[], int16_t dx, int16_t dy, int16_t w, int16_t h)
{
	uint16_t i, j, col, c16;
	uint32_t bw, bh, wa, off, rgb;

	bw = pgm_read_dword(&bitmap[0x12]);
	bh = pgm_read_dword(&bitmap[0x16]);
	wa = (bw + 3) & ~3;

	if (w >= h)
	{
		for (i = 0; i < h; ++i)
		{
			off = 54 + 256 * 4 + (bh - 1 - (i + dy)) * wa + dx;

			for (j = 0; j < w; ++j)
			{
				col = pgm_read_byte(&bitmap[off++]);
				rgb = pgm_read_dword(&bitmap[54 + col * 4]);
				c16 = ((rgb & 0xf8) >> 3) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf80000) >> 8);
				line_buffer[j] = LHSWAP(c16);
			}

			tft.pushImage(x, y + i, w, 1, line_buffer);
		}
	}
	else
	{
		for (i = 0; i < w; ++i)
		{
			off = 54 + 256 * 4 + (bh - 1 - dy) * wa + i + dx;

			for (j = 0; j < h; ++j)
			{
				col = pgm_read_byte(&bitmap[off]);
				rgb = pgm_read_dword(&bitmap[54 + col * 4]);
				c16 = ((rgb & 0xf8) >> 3) | ((rgb & 0xfc00) >> 5) | ((rgb & 0xf80000) >> 8);
				line_buffer[j] = LHSWAP(c16);
				off -= wa;
			}

			tft.pushImage(x + i, y, 1, h, line_buffer);
		}
	}
}



void drawCharFast(uint16_t x, uint16_t y, uint8_t c, uint16_t color, uint16_t bg)
{
	uint16_t i, j, c16, line;

	for (i = 0; i < 5; ++i)
	{
		line = pgm_read_byte(&font[c * 5 + i]);

		for (j = 0; j < 8; ++j)
		{
			c16 = (line & 1) ? color : bg;
			line_buffer[j * 5 + i] = LHSWAP(c16);
			line >>= 1;
		}
	}

	tft.pushImage(x, y, 5, 8, line_buffer);
}



void printFast(int x, int y, char* str, int16_t color)
{
	char c;

	while (c = *str++)
	{
		drawCharFast(x, y, c, color, 0);
		x += 6;
	}
}

void printFast_P(int x, int y, PGM_P str, int16_t color)
{
	char c;

	while (c = pgm_read_byte(str++))
	{
		drawCharFast(x, y, c, color, 0);
		x += 6;
	}
}



bool espboy_logo_effect(int out)
{
	int16_t i, j, w, h, sx, sy, off, st, anim;

	sx = 32;
	sy = 28;
	w = 64;
	h = 72;
	st = 8;

	for (anim = 0; anim < st; ++anim)
	{
		if (check_key() & PAD_ANY) return false;

		//if (!out) set_speaker(200 + anim * 50, 5);

		for (i = 0; i < w / st; ++i)
		{
			for (j = 0; j < st; ++j)
			{
				off = anim - (7 - j);

				if (out) off += 8;

				if (off < 0 || off >= st) off = 0; else off += i * st;

				drawBMP8Part(sx + i * st + j, sy, g_espboy, off, 0, 1, h);
			}
		}

		delay(1000 / 30);
	}

	return true;
}



#define CONTROL_TYPES   5

const char layout_name[] PROGMEM = "KEMP\0QAOP\0ZXse\0SINC\0CURS\0";

constexpr int8_t layout_scheme[] PROGMEM = {
  -1, 0, 0, 0, 0, 0, 0, 0,
  K_O, K_P, K_Q, K_A, K_SPACE, K_M, K_0, K_1,
  K_Z, K_X, K_Q, K_A, K_SPACE, K_ENTER, K_0, K_1,
  K_6, K_7, K_9, K_8, K_0, K_ENTER, K_SPACE, K_1,
  K_5, K_8, K_8, K_7, K_0, K_ENTER, K_SPACE, K_1
};



#define FILE_HEIGHT    14
#define FILE_FILTER   F("z80")

int16_t file_cursor;

uint8_t file_browser_ext(const char* name)
{
	while (1) if (*name++ == '.') break;

	return (strcasecmp_P(name, (PGM_P)FILE_FILTER) == 0) ? 1 : 0;
}

void file_browser(const char* path, const __FlashStringHelper* header, char* fname, uint16_t fname_len)
{
	int16_t i, sy, pos, off, frame, file_count, control_type;
	uint16_t j;
	uint8_t change, filter;
	fs::Dir dir;
	fs::File entry;
	char name[19 + 1];
	const char* str;

	memset(fname, 0, fname_len);
	memset(name, 0, sizeof(name));

	tft.fillScreen(TFT_BLACK);

	dir = SPIFFS.openDir(path);

	file_count = 0;
	control_type = 0;

	while (dir.next())
	{
		entry = dir.openFile("r");

		filter = file_browser_ext(entry.name());

		entry.close();

		if (filter) ++file_count;
	}

	if (!file_count)
	{
		printFast_P(24, 60, PSTR("No files found"), TFT_RED);

		while (1) delay(1000);
	}

	printFast_P(4, 4, (PGM_P)header, TFT_GREEN);
	tft.fillRect(0, 12, 128, 1, TFT_WHITE);

	change = 1;
	frame = 0;

	while (1)
	{
		if (change)
		{
			printFast_P(100, 4, &layout_name[control_type * 5], TFT_WHITE);

			pos = file_cursor - FILE_HEIGHT / 2;

			if (pos > file_count - FILE_HEIGHT) pos = file_count - FILE_HEIGHT;
			if (pos < 0) pos = 0;

			dir = SPIFFS.openDir(path);
			i = pos;
			while (dir.next())
			{
				entry = dir.openFile("r");

				filter = file_browser_ext(entry.name());

				entry.close();

				if (!filter) continue;

				--i;
				if (i < 0) break;
			}

			sy = 14;
			i = 0;

			while (1)
			{
				entry = dir.openFile("r");

				filter = file_browser_ext(entry.name());

				if (filter)
				{
					str = entry.name() + 1;

					for (j = 0; j < sizeof(name) - 1; ++j)
					{
						if (*str != 0 && *str != '.') name[j] = *str++; else name[j] = ' ';
					}

					printFast(8, sy, name, TFT_WHITE);

					drawCharFast(2, sy, ' ', TFT_WHITE, TFT_BLACK);

					if (pos == file_cursor)
					{
						strncpy(fname, entry.name(), fname_len);

						if (!(frame & 128)) drawCharFast(2, sy, 0xda, TFT_WHITE, TFT_BLACK);
					}
				}

				entry.close();

				if (!dir.next()) break;

				if (filter)
				{
					sy += 8;
					++pos;
					++i;
					if (i >= FILE_HEIGHT) break;
				}
			}

			change = 0;
		}

		check_key();

		if (pad_state_t & PAD_UP)
		{
			--file_cursor;

			if (file_cursor < 0) file_cursor = file_count - 1;

			change = 1;
			frame = 0;

		}

		if (pad_state_t & PAD_DOWN)
		{
			++file_cursor;

			if (file_cursor >= file_count) file_cursor = 0;

			change = 1;
			frame = 0;
		}

		if (pad_state_t & PAD_ACT)
		{
			++control_type;
			if (control_type >= CONTROL_TYPES) control_type = 0;
			change = 1;
		}

		if (pad_state_t & PAD_ESC) break;

		if ((pad_state & PAD_LFT) || (pad_state & PAD_RGT)) {
			fname[0] = 0;
			break;
		}

		delay(1);
		++frame;

		if (!(frame & 127)) change = 1;
	}

	off = control_type * 8;

	if (pgm_read_byte(&layout_scheme[off + 0]) >= 0)
	{
		control_type = CONTROL_PAD_KEYBOARD;
		control_pad_l = pgm_read_byte(&layout_scheme[off + 0]);
		control_pad_r = pgm_read_byte(&layout_scheme[off + 1]);
		control_pad_u = pgm_read_byte(&layout_scheme[off + 2]);
		control_pad_d = pgm_read_byte(&layout_scheme[off + 3]);
		control_pad_act = pgm_read_byte(&layout_scheme[off + 4]);
		control_pad_esc = pgm_read_byte(&layout_scheme[off + 5]);
		control_pad_lft = pgm_read_byte(&layout_scheme[off + 6]);
		control_pad_rgt = pgm_read_byte(&layout_scheme[off + 7]);
	}
	else
	{
		control_type = CONTROL_PAD_KEMPSTON;
	}

	tft.fillScreen(TFT_BLACK);
}



void ICACHE_RAM_ATTR sound_ISR()
{
	size_t gap;

	sigmaDeltaWrite(0, sound_dac);

	sound_dac = sound_buffer[sound_rd_ptr];

	if (sound_rd_ptr < sound_wr_ptr) gap = sound_wr_ptr - sound_rd_ptr; else gap = SOUND_BUFFER_SIZE - sound_rd_ptr + sound_wr_ptr;

	if (gap < SOUND_MIN_GAP)
	{
		++sound_rd_ptr;

		if (sound_rd_ptr >= SOUND_BUFFER_SIZE) sound_rd_ptr = 0;
	}
}



void zx_setup() {

		WiFi.mode(WIFI_OFF); //disable wifi to save some battery power
		Wire.setClock(1000000); //I2C to 1mHz

		//DAC init, LCD backlit off
		dac.begin(MCP4725address);
		delay(100);
		dac.setVoltage(0, false);
		delay(100);

		//mcp23017 and buttons init, should preceed the TFT init
		mcp.begin(MCP23017address);
		delay(100);

		for (int i = 0; i < 8; ++i)
		{
			mcp.pinMode(i, INPUT);
			mcp.pullUp(i, HIGH);
		}

		pad_state = 0;
		pad_state_prev = 0;
		pad_state_t = 0;

		//TFT init

		mcp.pinMode(csTFTMCP23017pin, OUTPUT);
		mcp.digitalWrite(csTFTMCP23017pin, LOW);

		tft.begin();
		tft.setRotation(0);
		tft.fillScreen(TFT_BLACK);

		dac.setVoltage(4095, true);

		//keybModule init
		Wire.begin();
		Wire.beginTransmission(0x27); //check for MCP23017Keyboard at address 0x27
		if (!Wire.endTransmission()) {
			keybModuleExist = 1;
			mcpKeyboard.begin(7);
			for (uint8_t i = 0; i < 7; i++) {
				mcpKeyboard.pinMode(i, OUTPUT);
				mcpKeyboard.digitalWrite(i, HIGH);
			}
			for (uint8_t i = 0; i < 5; i++) {
				mcpKeyboard.pinMode(i + 8, INPUT);
				mcpKeyboard.pullUp(i + 8, HIGH);
			}
			mcpKeyboard.pinMode(7, OUTPUT);
			mcpKeyboard.digitalWrite(7, HIGH); //backlit on
		}
		else keybModuleExist = 0;

		//cpu = new zymosis::Z80Cpu<Z48_ESPBoy>;
		memory = (uint8_t*)malloc(MEMORY_SIZE);

		//filesystem init
		SPIFFS.begin();

}

void sound_init(void)
{
	uint16_t i;

	sound_buffer = (uint8_t*)malloc(SOUND_BUFFER_SIZE);

	for (i = 0; i < SOUND_BUFFER_SIZE; ++i) sound_buffer[i] = 0;

	sound_dac = 0;
	sound_rd_ptr = 0;
	sound_wr_ptr = 0;

	noInterrupts();
	sigmaDeltaSetup(0, F_CPU / 256);
	sigmaDeltaAttachPin(SOUNDPIN);
	sigmaDeltaEnable();
	timer1_attachInterrupt(sound_ISR);
	timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
	timer1_write(ESP.getCpuFreqMHz() * 1000000 / SAMPLE_RATE);
	interrupts();
}

void change_ext(char* fname, const uint32_t ext)
{
	while (1)
	{
		if (!*fname) break;
		if (*fname++ == '.')
		{
			fname[0] = (uint8_t)(ext & 0xFF);
			fname[1] = (uint8_t)((ext >> 8) & 0xFF);
			fname[2] = (uint8_t)((ext >> 16) & 0xFF);
			break;
		}
	}
}


uint8_t code2layout[] PROGMEM =
{
	K_SS,		//35
	K_ENTER,	//36
	0, 0, 0, 0, // 37-40
	0, 0, 0, 0, 0, 0, 0, // 41-47
	K_0, K_1, K_2, K_3,	K_4, K_5, K_6, K_7, K_8, K_9, // 48-57
	0, 0, 0, 0, 0, 0, // 
	K_CS, // 64
	K_A, K_B, K_C, K_D,	K_E, // 65-69
	K_F, K_G, K_H, K_I, K_J, // 70-74
	K_K, K_L, K_M, K_N, K_O, // 75-79
	K_P, K_Q, K_R, K_S, K_T, // 80-84
	K_U, K_V, K_W, K_X, K_Y, // 85-89
	K_Z, // 90
	0, 0, 0, 0, // 91-94
	K_SPACE, // 95
};

uint8_t zx_layout_code(uint8_t c)
{
	if (c >= 'a' && c <= 'z') c -= 32;
	if (c > 34 && c < 96)
	{
		return pgm_read_byte(&code2layout[c - 35]);
	}
	return 0;
}

void zx_load_layout(char* filename)
{
	char cfg[8];

	fs::File f = SPIFFS.open(filename, "r");

	if (!f) return;

	f.readBytes(cfg, 8);
	f.close();

	control_type = CONTROL_PAD_KEYBOARD;
	control_pad_u = zx_layout_code(cfg[0]);
	control_pad_d = zx_layout_code(cfg[1]);
	control_pad_l = zx_layout_code(cfg[2]);
	control_pad_r = zx_layout_code(cfg[3]);
	control_pad_act = zx_layout_code(cfg[4]);
	control_pad_esc = zx_layout_code(cfg[5]);
	control_pad_lft = zx_layout_code(cfg[6]);
	control_pad_rgt = zx_layout_code(cfg[7]);
}


void keybModule() {
	static uint8_t keysReaded[7];
	static uint8_t row, col;
	static uint8_t keykeyboardpressed;
	static uint8_t symkeyboardpressed;
	symkeyboardpressed = 0;
	for (row = 0; row < 7; row++) {
		mcpKeyboard.digitalWrite(row, LOW);
		keysReaded[row] = ((mcpKeyboard.readGPIOAB() >> 8) & 31);
		mcpKeyboard.digitalWrite(row, HIGH);
	}
	if (!(keysReaded[2] & 1)) symkeyboardpressed = 1; // if "sym" key is pressed
	for (row = 0; row < 7; row++)
		for (col = 0; col < 5; col++)
			if (!((keysReaded[row] >> col) & 1))
			{
				if (!symkeyboardpressed) keykeyboardpressed = pgm_read_byte(&keybCurrent[row][col]);
				else keykeyboardpressed = pgm_read_byte(&keybCurrent2[row][col]);
				if (keykeyboardpressed < 40) key_matriz.set(keykeyboardpressed);
				else {
					if (keykeyboardpressed == K_DEL) {
						key_matriz.set(K_0);
						key_matriz.set(K_CS);
					}
					if (keykeyboardpressed == K_LED) {
						mcpKeyboard.digitalWrite(7, !mcpKeyboard.digitalRead(7));
						delay(100);
					}
				}
			}
}


void redrawOnscreen(uint8_t slX, uint8_t slY, uint8_t shf) {
	tft.fillRect(0, 128 - 16, 128, 16, TFT_BLACK);
	for (uint8_t i = 0; i < 20; i++) drawCharFast(i * 6 + 4, 128 - 16, pgm_read_byte(&keybOnscr[0][i]), TFT_YELLOW, TFT_BLACK);
	for (uint8_t i = 0; i < 20; i++) drawCharFast(i * 6 + 4, 128 - 8, pgm_read_byte(&keybOnscr[1][i]), TFT_YELLOW, TFT_BLACK);
	drawCharFast(slX * 6 + 4, 128 - 16 + slY * 8, pgm_read_byte(&keybOnscr[slY][slX]), TFT_RED, TFT_BLACK);
	if (shf & 1) drawCharFast(10 * 6 + 4, 128 - 16 + 8, pgm_read_byte(&keybOnscr[1][10]), TFT_RED, TFT_BLACK);
	if (shf & 2) drawCharFast(18 * 6 + 4, 128 - 16 + 8, pgm_read_byte(&keybOnscr[1][18]), TFT_RED, TFT_BLACK);
}


void keybOnscreen() {
	uint8_t selX = 0, selY = 0, shifts = 0;
	redrawOnscreen(selX, selY, shifts);
	while (1) {
		check_key();
		delay(100);
		if ((pad_state & PAD_RIGHT) && selX < 19) selX++;
		if ((pad_state & PAD_LEFT) && selX > 0) selX--;
		if ((pad_state & PAD_DOWN) && selY < 1) selY++;
		if ((pad_state & PAD_UP) && selY > 0) selY--;
		if (((pad_state & PAD_ACT) || (pad_state & PAD_ESC)) && !(selX == 10 && selY == 1) && !(selX == 18 && selY == 1)) break;
		if ((pad_state & PAD_ACT) && (selX == 10) && (selY == 1)) { shifts |= (shifts ^ 1); delay(300); }
		if ((pad_state & PAD_ACT) && (selX == 18) && (selY == 1)) { shifts |= (shifts ^ 2); delay(300); }
		if ((pad_state & PAD_ACT) && (selX == 10) && (selY == 1) && (shifts & 2)) break;
		if ((pad_state & PAD_ACT) && (selX == 18) && (selY == 1) && (shifts & 1)) break;
		if (pad_state) redrawOnscreen(selX, selY, shifts);
	}

	if (pad_state & PAD_ACT) key_matriz.set(pgm_read_byte(&keybOnscrMatrix[selY][selX]));
	if (pad_state & PAD_ACT && (shifts & 1)) key_matriz.set(K_CS);
	if (pad_state & PAD_ACT && (shifts & 2)) key_matriz.set(K_SS);
	delay(300);
	check_key();
	tft.fillRect(0, 128 - 16, 128, 16, TFT_BLACK);
	memset(line_change, 0xff, sizeof(line_change));
}


void zx_loop()
{
		uint32_t t_prev, t_new;
		uint8_t frames;
		uint32_t avgt = 0;

		file_cursor = 0;

		control_type = CONTROL_PAD_KEYBOARD;
		control_pad_l = K_Z;
		control_pad_r = K_X;
		control_pad_u = K_Q;
		control_pad_d = K_A;
		control_pad_act = K_SPACE;
		control_pad_esc = K_ENTER;
		control_pad_lft = K_NULL;
		control_pad_rgt = K_NULL;

		if (espboy_logo_effect(0))
		{
			wait_any_key(1000);
			espboy_logo_effect(1);
		}

		file_browser("/", F("Load .Z80:"), filename, sizeof(filename));

		cpu.Z80_Reset();

		if (*filename) // filename is not ""
		{
			change_ext(filename, EXT_CFG);
			zx_load_layout(filename);

			change_ext(filename, EXT_SCR);
			if (cpu.load_scr(filename))
			{
				cpu.renderFrame();
				wait_any_key(3 * 1000);
			}

			change_ext(filename, EXT_Z80);
			cpu.Z80_Reset();
			cpu.load_z80(filename);
		}

		SPIFFS.end();

		memset(line_change, 0xff, sizeof(line_change));
		sound_init();

		//main loop

		t_prev = micros();
		while (1)
		{
			key_matriz.reset();
			check_key();

			//check onscreen keyboard
			if ((pad_state & PAD_LFT) && (pad_state & PAD_RGT)) keybOnscreen();

			//check keyboard module
			if (keybModuleExist) keybModule();

			switch (control_type)
			{
			case CONTROL_PAD_KEYBOARD:
				key_matriz.set(control_pad_l, pad_state & PAD_LEFT);
				key_matriz.set(control_pad_r, pad_state & PAD_RIGHT);
				key_matriz.set(control_pad_u, pad_state & PAD_UP);
				key_matriz.set(control_pad_d, pad_state & PAD_DOWN);
				key_matriz.set(control_pad_act, pad_state & PAD_ACT);
				key_matriz.set(control_pad_esc, pad_state & PAD_ESC);
				key_matriz.set(control_pad_lft, pad_state & PAD_LFT);
				key_matriz.set(control_pad_rgt, pad_state & PAD_RGT);
				break;

			case CONTROL_PAD_KEMPSTON:
				port_1f = 0;
				if (pad_state & PAD_LEFT) port_1f |= 0x02;
				if (pad_state & PAD_RIGHT) port_1f |= 0x01;
				if (pad_state & PAD_UP) port_1f |= 0x08;
				if (pad_state & PAD_DOWN) port_1f |= 0x04;
				if (pad_state & PAD_ACT) port_1f |= 0x10;
				key_matriz.set(K_SPACE, pad_state & PAD_ESC);
				key_matriz.set(K_0, pad_state & PAD_LFT);
				key_matriz.set(K_1, pad_state & PAD_RGT);
				break;
			}

			t_new = micros();
			frames = ((t_new - t_prev) / (1000000 / ZX_FRAME_RATE));
			if (frames < 1) frames = 1;
			t_prev = t_new;

			if (frames > MAX_FRAMESKIP) frames = MAX_FRAMESKIP;

			while (frames--) cpu.emulateFrame();

			uint32_t tp = t_prev; // micros();

			cpu.renderFrame();

			uint32_t tt = micros() - tp;
			uint32_t st = 1000000 / tt;

			avgt = ((avgt * 19) + tt) / 20;

			tft.fillRect(0, 0, 6 * 4, 10, TFT_BLACK);
			//tft.drawString(String(avgt), 0, 10);
			tft.drawString(String(st), 0, 0);

			delay(0);
		}
}
