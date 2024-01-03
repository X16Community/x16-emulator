// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// Copyright (c) 2020 Frank van den Hoef
// All rights reserved. License: 2-clause BSD

#include "video.h"
#include "memory.h"
#include "glue.h"
#include "debugger.h"
#include "keyboard.h"
#include "gif.h"
#include "joystick.h"
#include "vera_spi.h"
#include "vera_psg.h"
#include "vera_pcm.h"
#include "icon.h"
#include "sdcard.h"
#include "i2c.h"
#include "audio.h"

#include <limits.h>
#include <stdint.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

#ifndef __EMSCRIPTEN__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

#define APPROX_TITLEBAR_HEIGHT 30

#define VERA_VERSION_MAJOR  0x00
#define VERA_VERSION_MINOR  0x03
#define VERA_VERSION_PATCH  0x02

#define ADDR_VRAM_START     0x00000
#define ADDR_VRAM_END       0x20000
#define ADDR_PSG_START      0x1F9C0
#define ADDR_PSG_END        0x1FA00
#define ADDR_PALETTE_START  0x1FA00
#define ADDR_PALETTE_END    0x1FC00
#define ADDR_SPRDATA_START  0x1FC00
#define ADDR_SPRDATA_END    0x20000

#define NUM_SPRITES 128

// both VGA and NTSC
#define SCAN_HEIGHT 525
#define PIXEL_FREQ 25.0

// VGA
#define VGA_SCAN_WIDTH 800
#define VGA_Y_OFFSET 0

// NTSC: 262.5 lines per frame, lower field first
#define NTSC_HALF_SCAN_WIDTH 794
#define NTSC_X_OFFSET 270
#define NTSC_Y_OFFSET_LOW 42
#define NTSC_Y_OFFSET_HIGH 568
#define TITLE_SAFE_X 0.067
#define TITLE_SAFE_Y 0.05

// visible area we're drawing
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define SCREEN_RAM_OFFSET 0x00000

#ifdef __APPLE__
#define LSHORTCUT_KEY SDL_SCANCODE_LGUI
#define RSHORTCUT_KEY SDL_SCANCODE_RGUI
#else
#define LSHORTCUT_KEY SDL_SCANCODE_LCTRL
#define RSHORTCUT_KEY SDL_SCANCODE_RCTRL
#endif

// When rendering a layer line, we can amortize some of the cost by calculating multiple pixels at a time.
#define LAYER_PIXELS_PER_ITERATION 8

#define MAX(a,b) ((a) > (b) ? a : b)

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *sdlTexture;
static bool is_fullscreen = false;
bool mouse_grabbed = false;
bool no_keyboard_capture = false;
bool kernal_mouse_enabled = false;

static uint8_t video_ram[0x20000];
static uint8_t palette[256 * 2];
static uint8_t sprite_data[128][8];

// I/O registers
static uint32_t io_addr[2];
static uint8_t io_rddata[2];
static uint8_t io_inc[2];
static uint8_t io_addrsel;
static uint8_t io_dcsel;

static uint8_t ien;
static uint8_t isr;

static uint16_t irq_line;

static uint8_t reg_layer[2][7];

#define COMPOSER_SLOTS 4*64
static uint8_t reg_composer[COMPOSER_SLOTS];
static uint8_t prev_reg_composer[2][COMPOSER_SLOTS];

static uint8_t layer_line[2][SCREEN_WIDTH];
static uint8_t sprite_line_col[SCREEN_WIDTH];
static uint8_t sprite_line_z[SCREEN_WIDTH];
static uint8_t sprite_line_mask[SCREEN_WIDTH];
static uint8_t sprite_line_collisions;
static bool layer_line_enable[2];
static bool old_layer_line_enable[2];
static bool old_sprite_line_enable;
static bool sprite_line_enable;

////////////////////////////////////////////////////////////
// FX registers
////////////////////////////////////////////////////////////
static uint8_t fx_addr1_mode;

// These are all 16.16 fixed point in the emulator
// even though the VERA uses smaller bit widths
// for the whole and fractional parts.
//
// Sign extension is done manually when assigning negative numbers
//
// Native VERA bit widths are shown below.
static uint32_t fx_x_pixel_increment;  // 11.9 fixed point (6.9 without 32x multiplier, 11.4 with 32x multiplier on)
static uint32_t fx_y_pixel_increment;  // 11.9 fixed point (6.9 without 32x multiplier, 11.4 with 32x multiplier on)
static uint32_t fx_x_pixel_position;   // 11.9 fixed point
static uint32_t fx_y_pixel_position;   // 11.9 fixed point

static uint16_t fx_poly_fill_length;      // 10 bits

static uint32_t fx_affine_tile_base;
static uint32_t fx_affine_map_base;

static uint8_t fx_affine_map_size;

static bool fx_4bit_mode;
static bool fx_16bit_hop;
static bool fx_cache_byte_cycling;
static bool fx_cache_fill;
static bool fx_cache_write;
static bool fx_trans_writes;

static bool fx_2bit_poly;
static bool fx_2bit_poking;

static bool fx_cache_increment_mode;
static bool fx_cache_nibble_index;
static uint8_t fx_cache_byte_index;
static bool fx_multiplier;
static bool fx_subtract;

static bool fx_affine_clip;

static uint8_t fx_16bit_hop_align;

static bool fx_nibble_bit[2];
static bool fx_nibble_incr[2];

static uint8_t fx_cache[4];

static int32_t fx_mult_accumulator;

static const uint8_t vera_version_string[] = {'V',
	VERA_VERSION_MAJOR,
	VERA_VERSION_MINOR,
	VERA_VERSION_PATCH
};

float vga_scan_pos_x;
uint16_t vga_scan_pos_y;
float ntsc_half_cnt;
uint16_t ntsc_scan_pos_y;
int frame_count = 0;

static uint8_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT * 4];
#ifndef __EMSCRIPTEN__
static uint8_t png_buffer[SCREEN_WIDTH * SCREEN_HEIGHT * 3];
#endif

static GifWriter gif_writer;

static const uint16_t default_palette[] = {
0x000,0xfff,0x800,0xafe,0xc4c,0x0c5,0x00a,0xee7,0xd85,0x640,0xf77,0x333,0x777,0xaf6,0x08f,0xbbb,0x000,0x111,0x222,0x333,0x444,0x555,0x666,0x777,0x888,0x999,0xaaa,0xbbb,0xccc,0xddd,0xeee,0xfff,0x211,0x433,0x644,0x866,0xa88,0xc99,0xfbb,0x211,0x422,0x633,0x844,0xa55,0xc66,0xf77,0x200,0x411,0x611,0x822,0xa22,0xc33,0xf33,0x200,0x400,0x600,0x800,0xa00,0xc00,0xf00,0x221,0x443,0x664,0x886,0xaa8,0xcc9,0xfeb,0x211,0x432,0x653,0x874,0xa95,0xcb6,0xfd7,0x210,0x431,0x651,0x862,0xa82,0xca3,0xfc3,0x210,0x430,0x640,0x860,0xa80,0xc90,0xfb0,0x121,0x343,0x564,0x786,0x9a8,0xbc9,0xdfb,0x121,0x342,0x463,0x684,0x8a5,0x9c6,0xbf7,0x120,0x241,0x461,0x582,0x6a2,0x8c3,0x9f3,0x120,0x240,0x360,0x480,0x5a0,0x6c0,0x7f0,0x121,0x343,0x465,0x686,0x8a8,0x9ca,0xbfc,0x121,0x242,0x364,0x485,0x5a6,0x6c8,0x7f9,0x020,0x141,0x162,0x283,0x2a4,0x3c5,0x3f6,0x020,0x041,0x061,0x082,0x0a2,0x0c3,0x0f3,0x122,0x344,0x466,0x688,0x8aa,0x9cc,0xbff,0x122,0x244,0x366,0x488,0x5aa,0x6cc,0x7ff,0x022,0x144,0x166,0x288,0x2aa,0x3cc,0x3ff,0x022,0x044,0x066,0x088,0x0aa,0x0cc,0x0ff,0x112,0x334,0x456,0x668,0x88a,0x9ac,0xbcf,0x112,0x224,0x346,0x458,0x56a,0x68c,0x79f,0x002,0x114,0x126,0x238,0x24a,0x35c,0x36f,0x002,0x014,0x016,0x028,0x02a,0x03c,0x03f,0x112,0x334,0x546,0x768,0x98a,0xb9c,0xdbf,0x112,0x324,0x436,0x648,0x85a,0x96c,0xb7f,0x102,0x214,0x416,0x528,0x62a,0x83c,0x93f,0x102,0x204,0x306,0x408,0x50a,0x60c,0x70f,0x212,0x434,0x646,0x868,0xa8a,0xc9c,0xfbe,0x211,0x423,0x635,0x847,0xa59,0xc6b,0xf7d,0x201,0x413,0x615,0x826,0xa28,0xc3a,0xf3c,0x201,0x403,0x604,0x806,0xa08,0xc09,0xf0b
};

uint8_t video_space_read(uint32_t address);
static void video_space_read_range(uint8_t* dest, uint32_t address, uint32_t size);

static void refresh_palette();

void
mousegrab_toggle() {
	mouse_grabbed = !mouse_grabbed;
	SDL_SetWindowGrab(window, mouse_grabbed && !no_keyboard_capture);
	SDL_SetRelativeMouseMode(mouse_grabbed);
	SDL_ShowCursor((mouse_grabbed || kernal_mouse_enabled) ? SDL_DISABLE : SDL_ENABLE);
	sprintf(window_title, WINDOW_TITLE "%s", mouse_grabbed ? MOUSE_GRAB_MSG : "");
	video_update_title(window_title);
}

void
video_reset()
{
	// init I/O registers
	memset(io_addr, 0, sizeof(io_addr));
	memset(io_inc, 0, sizeof(io_inc));
	io_addrsel = 0;
	io_dcsel = 0;
	io_rddata[0] = 0;
	io_rddata[1] = 0;

	ien = 0;
	isr = 0;
	irq_line = 0;

	// init Layer registers
	memset(reg_layer, 0, sizeof(reg_layer));

	// init composer registers
	memset(reg_composer, 0, sizeof(reg_composer));
	reg_composer[1] = 128; // hscale = 1.0
	reg_composer[2] = 128; // vscale = 1.0
	reg_composer[5] = 640 >> 2;
	reg_composer[7] = 480 >> 1;

	// Initialize FX registers
	fx_addr1_mode = 0;
	fx_x_pixel_position = 0x8000;
	fx_y_pixel_position = 0x8000;
	fx_x_pixel_increment = 0;
	fx_y_pixel_increment = 0;

	fx_cache_write = false;
	fx_cache_fill = false;
	fx_4bit_mode = false;
	fx_16bit_hop = false;
	fx_subtract = false;
	fx_cache_byte_cycling = false;
	fx_trans_writes = false;
	fx_multiplier = false;

	fx_mult_accumulator = 0;

	fx_2bit_poly = false;
	fx_2bit_poking = false;

	fx_cache_nibble_index = 0;
	fx_cache_byte_index = 0;
	fx_cache_increment_mode = 0;

	fx_cache[0] = 0;
	fx_cache[1] = 0;
	fx_cache[2] = 0;
	fx_cache[3] = 0;

	fx_16bit_hop_align = 0;

	fx_nibble_bit[0] = false;
	fx_nibble_bit[1] = false;
	fx_nibble_incr[0] = false;
	fx_nibble_incr[1] = false;

	fx_poly_fill_length = 0;
	fx_affine_tile_base = 0;
	fx_affine_map_base = 0;
	fx_affine_map_size = 2;
	fx_affine_clip = false;

	// init sprite data
	memset(sprite_data, 0, sizeof(sprite_data));

	// copy palette
	memcpy(palette, default_palette, sizeof(palette));
	for (int i = 0; i < 256; i++) {
		palette[i * 2 + 0] = default_palette[i] & 0xff;
		palette[i * 2 + 1] = default_palette[i] >> 8;
	}

	refresh_palette();

	// fill video RAM with random data
	for (int i = 0; i < 128 * 1024; i++) {
		video_ram[i] = rand();
	}

	sprite_line_collisions = 0;

	vga_scan_pos_x = 0;
	vga_scan_pos_y = 0;
	ntsc_half_cnt = 0;
	ntsc_scan_pos_y = 0;

	psg_reset();
	pcm_reset();
}

bool
video_init(int window_scale, float screen_x_scale, char *quality, bool fullscreen, float opacity)
{
	uint32_t window_flags = SDL_WINDOW_ALLOW_HIGHDPI;

#ifdef __EMSCRIPTEN__
	// Setting this flag would render the web canvas outside of its bounds on high dpi screens
	window_flags &= ~SDL_WINDOW_ALLOW_HIGHDPI;
#endif

	video_reset();

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, quality);
	SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1"); // Grabs keyboard shortcuts from the system during window grab
	SDL_CreateWindowAndRenderer(SCREEN_WIDTH * window_scale * screen_x_scale, SCREEN_HEIGHT * window_scale, window_flags, &window, &renderer);
#ifndef __MORPHOS__
	SDL_SetWindowResizable(window, true);
#endif
	SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH * screen_x_scale, SCREEN_HEIGHT);

	sdlTexture = SDL_CreateTexture(renderer,
									SDL_PIXELFORMAT_RGB888,
									SDL_TEXTUREACCESS_STREAMING,
									SCREEN_WIDTH, SCREEN_HEIGHT);

	SDL_SetWindowTitle(window, WINDOW_TITLE);
	SDL_SetWindowIcon(window, CommanderX16Icon());
	if (fullscreen) {
		is_fullscreen = true;
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
	} else {
		int winX, winY;
		SDL_GetWindowPosition(window, &winX, &winY);
		if (winX < 0 || winY < APPROX_TITLEBAR_HEIGHT) {
			winX = winX < 0 ? 0 : winX;
			winY = winY < APPROX_TITLEBAR_HEIGHT ? APPROX_TITLEBAR_HEIGHT : winY;
			SDL_SetWindowPosition(window, winX, winY);
		}
	}

	SDL_SetWindowOpacity(window, opacity);

	if (record_gif != RECORD_GIF_DISABLED) {
		if (!strcmp(gif_path+strlen(gif_path)-5, ",wait")) {
			// wait for POKE
			record_gif = RECORD_GIF_PAUSED;
			// move the string terminator to remove the ",wait"
			gif_path[strlen(gif_path)-5] = 0;
		} else {
			// start now
			record_gif = RECORD_GIF_ACTIVE;
		}
		if (!GifBegin(&gif_writer, gif_path, SCREEN_WIDTH, SCREEN_HEIGHT, 1, 8, false)) {
			record_gif = RECORD_GIF_DISABLED;
		}
	}

	if (debugger_enabled) {
		DEBUGInitUI(renderer);
	}

	if (grab_mouse && !mouse_grabbed)
		mousegrab_toggle();

	return true;
}

struct video_layer_properties
{
	uint8_t color_depth;
	uint32_t map_base;
	uint32_t tile_base;

	bool text_mode;
	bool text_mode_256c;
	bool tile_mode;
	bool bitmap_mode;

	uint16_t hscroll;
	uint16_t vscroll;

	uint8_t  mapw_log2;
	uint8_t  maph_log2;
	uint16_t tilew;
	uint16_t tileh;
	uint8_t  tilew_log2;
	uint8_t  tileh_log2;

	uint16_t mapw_max;
	uint16_t maph_max;
	uint16_t tilew_max;
	uint16_t tileh_max;
	uint16_t layerw_max;
	uint16_t layerh_max;

	uint8_t tile_size_log2;

	int min_eff_x;
	int max_eff_x;

	uint8_t bits_per_pixel;
	uint8_t first_color_pos;
	uint8_t color_mask;
	uint8_t color_fields_max;
};

#define NUM_LAYERS 2
struct video_layer_properties layer_properties[NUM_LAYERS];
struct video_layer_properties prev_layer_properties[2][NUM_LAYERS];

static int
calc_layer_eff_x(const struct video_layer_properties *props, const int x)
{
	return (x + props->hscroll) & (props->layerw_max);
}

static int
calc_layer_eff_y(const struct video_layer_properties *props, const int y)
{
	return (y + props->vscroll) & (props->layerh_max);
}

static uint32_t
calc_layer_map_addr_base2(const struct video_layer_properties *props, const int eff_x, const int eff_y)
{
	// Slightly faster on some platforms because we know that tilew and tileh are powers of 2.
	return props->map_base + ((((eff_y >> props->tileh_log2) << props->mapw_log2) + (eff_x >> props->tilew_log2)) << 1);
}

//TODO: Unused in all current cases. Delete? Or leave commented as a reminder?
//static uint32_t
//calc_layer_map_addr(struct video_layer_properties *props, int eff_x, int eff_y)
//{
//	return props->map_base + ((eff_y / props->tileh) * props->mapw + (eff_x / props->tilew)) * 2;
//}
static void
refresh_layer_properties(const uint8_t layer)
{
	struct video_layer_properties* props = &layer_properties[layer];

	uint16_t prev_layerw_max = props->layerw_max;
	uint16_t prev_hscroll = props->hscroll;

	props->color_depth    = reg_layer[layer][0] & 0x3;
	props->map_base       = reg_layer[layer][1] << 9;
	props->tile_base      = (reg_layer[layer][2] & 0xFC) << 9;
	props->bitmap_mode    = (reg_layer[layer][0] & 0x4) != 0;
	props->text_mode      = (props->color_depth == 0) && !props->bitmap_mode;
	props->text_mode_256c = (reg_layer[layer][0] & 8) != 0;
	props->tile_mode      = !props->bitmap_mode && !props->text_mode;

	if (!props->bitmap_mode) {
		props->hscroll = reg_layer[layer][3] | (reg_layer[layer][4] & 0xf) << 8;
		props->vscroll = reg_layer[layer][5] | (reg_layer[layer][6] & 0xf) << 8;
	} else {
		props->hscroll = 0;
		props->vscroll = 0;
	}

	uint16_t mapw = 0;
	uint16_t maph = 0;
	props->tilew = 0;
	props->tileh = 0;

	if (props->tile_mode || props->text_mode) {
		props->mapw_log2 = 5 + ((reg_layer[layer][0] >> 4) & 3);
		props->maph_log2 = 5 + ((reg_layer[layer][0] >> 6) & 3);
		mapw      = 1 << props->mapw_log2;
		maph      = 1 << props->maph_log2;

		// Scale the tiles or text characters according to TILEW and TILEH.
		props->tilew_log2 = 3 + (reg_layer[layer][2] & 1);
		props->tileh_log2 = 3 + ((reg_layer[layer][2] >> 1) & 1);
		props->tilew      = 1 << props->tilew_log2;
		props->tileh      = 1 << props->tileh_log2;
	} else if (props->bitmap_mode) {
		// bitmap mode is basically tiled mode with a single huge tile
		props->tilew = (reg_layer[layer][2] & 1) ? 640 : 320;
		props->tileh = SCREEN_HEIGHT;
	}

	// We know mapw, maph, tilew, and tileh are powers of two in all cases except bitmap modes, and any products of that set will be powers of two,
	// so there's no need to modulo against them if we have bitmasks we can bitwise-and against.

	props->mapw_max = mapw - 1;
	props->maph_max = maph - 1;
	props->tilew_max = props->tilew - 1;
	props->tileh_max = props->tileh - 1;
	props->layerw_max = (mapw * props->tilew) - 1;
	props->layerh_max = (maph * props->tileh) - 1;

	// Find min/max eff_x for bulk reading in tile data during draw.
	if (prev_layerw_max != props->layerw_max || prev_hscroll != props->hscroll) {
		int min_eff_x = INT_MAX;
		int max_eff_x = INT_MIN;
		for (int x = 0; x < SCREEN_WIDTH; ++x) {
			int eff_x = calc_layer_eff_x(props, x);
			if (eff_x < min_eff_x) {
				min_eff_x = eff_x;
			}
			if (eff_x > max_eff_x) {
				max_eff_x = eff_x;
			}
		}
		props->min_eff_x = min_eff_x;
		props->max_eff_x = max_eff_x;
	}

	props->bits_per_pixel = 1 << props->color_depth;
	props->tile_size_log2 = props->tilew_log2 + props->tileh_log2 + props->color_depth - 3;

	props->first_color_pos  = 8 - props->bits_per_pixel;
	props->color_mask       = (1 << props->bits_per_pixel) - 1;
	props->color_fields_max = (8 >> props->color_depth) - 1;
}

struct video_sprite_properties
{
	int8_t sprite_zdepth;
	uint8_t sprite_collision_mask;

	int16_t sprite_x;
	int16_t sprite_y;
	uint8_t sprite_width_log2;
	uint8_t sprite_height_log2;
	uint8_t sprite_width;
	uint8_t sprite_height;

	bool hflip;
	bool vflip;

	uint8_t color_mode;
	uint32_t sprite_address;

	uint16_t palette_offset;
};

#ifndef __EMSCRIPTEN__
static void
screenshot(void)
{
	char path[PATH_MAX];
	const time_t now = time(NULL);
	strftime(path, PATH_MAX, "x16emu-%Y-%m-%d-%H-%M-%S.png", localtime(&now));

	memset(png_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 3);

	// The framebuffer stores pixels in BRGA but we want RGB:
	for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
		png_buffer[(i*3)+0] = framebuffer[(i*4)+2];
		png_buffer[(i*3)+1] = framebuffer[(i*4)+1];
		png_buffer[(i*3)+2] = framebuffer[(i*4)+0];
	}

	if (stbi_write_png(path, SCREEN_WIDTH, SCREEN_HEIGHT, 3, png_buffer, SCREEN_WIDTH*3)) {
		printf("Wrote screenshot to %s\n", path);
	} else {
		printf("WARNING: Couldn't write screenshot to %s\n", path);
	}
}
#endif

struct video_sprite_properties sprite_properties[128];

static void
refresh_sprite_properties(const uint16_t sprite)
{
	struct video_sprite_properties* props = &sprite_properties[sprite];

	props->sprite_zdepth = (sprite_data[sprite][6] >> 2) & 3;
	props->sprite_collision_mask = sprite_data[sprite][6] & 0xf0;

	props->sprite_x = sprite_data[sprite][2] | (sprite_data[sprite][3] & 3) << 8;
	props->sprite_y = sprite_data[sprite][4] | (sprite_data[sprite][5] & 3) << 8;
	props->sprite_width_log2  = (((sprite_data[sprite][7] >> 4) & 3) + 3);
	props->sprite_height_log2 = ((sprite_data[sprite][7] >> 6) + 3);
	props->sprite_width       = 1 << props->sprite_width_log2;
	props->sprite_height      = 1 << props->sprite_height_log2;

	// fix up negative coordinates
	if (props->sprite_x >= 0x400 - props->sprite_width) {
		props->sprite_x -= 0x400;
	}
	if (props->sprite_y >= 0x400 - props->sprite_height) {
		props->sprite_y -= 0x400;
	}

	props->hflip = sprite_data[sprite][6] & 1;
	props->vflip = (sprite_data[sprite][6] >> 1) & 1;

	props->color_mode     = (sprite_data[sprite][1] >> 7) & 1;
	props->sprite_address = sprite_data[sprite][0] << 5 | (sprite_data[sprite][1] & 0xf) << 13;

	props->palette_offset = (sprite_data[sprite][7] & 0x0f) << 4;
}

struct video_palette
{
	uint32_t entries[256];
	bool dirty;
};

struct video_palette video_palette;

static void
refresh_palette() {
	const uint8_t out_mode = reg_composer[0] & 3;
	const bool chroma_disable = ((reg_composer[0] & 0x07) == 6);
	for (int i = 0; i < 256; ++i) {
		uint8_t r;
		uint8_t g;
		uint8_t b;
		if (out_mode == 0) {
			// video generation off
			// -> show blue screen
			r = 0;
			g = 0;
			b = 255;
		} else {
			uint16_t entry = palette[i * 2] | palette[i * 2 + 1] << 8;
			r = ((entry >> 8) & 0xf) << 4 | ((entry >> 8) & 0xf);
			g = ((entry >> 4) & 0xf) << 4 | ((entry >> 4) & 0xf);
			b = (entry & 0xf) << 4 | (entry & 0xf);
			if (chroma_disable) {
				r = g = b = (r + b + g) / 3;
			}
		}

		video_palette.entries[i] = (uint32_t)(r << 16) | ((uint32_t)g << 8) | ((uint32_t)b);
	}
	video_palette.dirty = false;
}

static void
expand_4bpp_data(uint8_t *dst, const uint8_t *src, int dst_size)
{
	while (dst_size >= 2) {
		*dst = (*src) >> 4;
		++dst;
		*dst = (*src) & 0xf;
		++dst;

		++src;
		dst_size -= 2;
	}
}

static void
render_sprite_line(const uint16_t y)
{
	memset(sprite_line_col, 0, SCREEN_WIDTH);
	memset(sprite_line_z, 0, SCREEN_WIDTH);
	memset(sprite_line_mask, 0, SCREEN_WIDTH);

	uint16_t sprite_budget = 800 + 1;
	for (int i = 0; i < NUM_SPRITES; i++) {
		// one clock per lookup
		sprite_budget--; if (sprite_budget == 0) break;
		const struct video_sprite_properties *props = &sprite_properties[i];

		if (props->sprite_zdepth == 0) {
			continue;
		}

		// check whether this line falls within the sprite
		if (y < props->sprite_y || y >= props->sprite_y + props->sprite_height) {
			continue;
		}

		const uint16_t eff_sy = props->vflip ? ((props->sprite_height - 1) - (y - props->sprite_y)) : (y - props->sprite_y);

		int16_t       eff_sx      = (props->hflip ? (props->sprite_width - 1) : 0);
		const int16_t eff_sx_incr = props->hflip ? -1 : 1;

		const uint8_t *bitmap_data = video_ram + props->sprite_address + (eff_sy << (props->sprite_width_log2 - (1 - props->color_mode)));

		uint8_t unpacked_sprite_line[64];
		const uint16_t width = (props->sprite_width<64? props->sprite_width : 64);
		if (props->color_mode == 0) {
			// 4bpp
			expand_4bpp_data(unpacked_sprite_line, bitmap_data, width);
		} else {
			// 8bpp
			memcpy(unpacked_sprite_line, bitmap_data, width);
		}

		for (uint16_t sx = 0; sx < props->sprite_width; ++sx) {
			const uint16_t line_x = props->sprite_x + sx;
			if (line_x >= SCREEN_WIDTH) {
				eff_sx += eff_sx_incr;
				continue;
			}

			// one clock per fetched 32 bits
			if (!(sx & 3)) {
				sprite_budget--; if (sprite_budget == 0) break;
			}

			// one clock per rendered pixel
			sprite_budget--; if (sprite_budget == 0) break;

			const uint8_t col_index = unpacked_sprite_line[eff_sx];
			eff_sx += eff_sx_incr;

			// palette offset
			if (col_index > 0) {
				sprite_line_collisions |= sprite_line_mask[line_x] & props->sprite_collision_mask;
				sprite_line_mask[line_x] |= props->sprite_collision_mask;

			if (props->sprite_zdepth > sprite_line_z[line_x]) {
					sprite_line_col[line_x] = col_index + props->palette_offset;
					sprite_line_z[line_x] = props->sprite_zdepth;
				}
			}
		}
	}
}

static void
render_layer_line_text(uint8_t layer, uint16_t y)
{
	const struct video_layer_properties *props = &prev_layer_properties[1][layer];
	const struct video_layer_properties *props0 = &prev_layer_properties[0][layer];

	const uint8_t max_pixels_per_byte = (8 >> props->color_depth) - 1;
	const int     eff_y               = calc_layer_eff_y(props0, y);
	const int     yy                  = eff_y & props->tileh_max;

	// additional bytes to reach the correct line of the tile
	const uint32_t y_add = (yy << props->tilew_log2) >> 3;

	const uint32_t map_addr_begin = calc_layer_map_addr_base2(props, props->min_eff_x, eff_y);
	const uint32_t map_addr_end   = calc_layer_map_addr_base2(props, props->max_eff_x, eff_y);
	const int      size           = (map_addr_end - map_addr_begin) + 2;

	uint8_t tile_bytes[512]; // max 256 tiles, 2 bytes each.
	video_space_read_range(tile_bytes, map_addr_begin, size);

	uint32_t tile_start;

	uint8_t  fg_color;
	uint8_t  bg_color;
	uint8_t  s;
	uint8_t  color_shift;

	{
		const int eff_x = calc_layer_eff_x(props, 0);
		const int xx    = eff_x & props->tilew_max;

		// extract all information from the map
		const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

		const uint8_t tile_index = tile_bytes[map_addr];
		const uint8_t byte1      = tile_bytes[map_addr + 1];

		if (!props->text_mode_256c) {
			fg_color = byte1 & 15;
			bg_color = byte1 >> 4;
		} else {
			fg_color = byte1;
			bg_color = 0;
		}

		// offset within tilemap of the current tile
		tile_start = tile_index << props->tile_size_log2;

		// additional bytes to reach the correct column of the tile
		const uint16_t x_add       = xx >> 3;
		const uint32_t tile_offset = tile_start + y_add + x_add;

		s           = video_space_read(props->tile_base + tile_offset);
		color_shift = max_pixels_per_byte - (xx & 0x7);
	}

	// Render tile line.
	for (int x = 0; x < SCREEN_WIDTH; x++) {
		// Scrolling
		const int eff_x = calc_layer_eff_x(props, x);
		const int xx = eff_x & props->tilew_max;

		if ((eff_x & 0x7) == 0) {
			if ((eff_x & props->tilew_max) == 0) {
				// extract all information from the map
				const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

				const uint8_t tile_index = tile_bytes[map_addr];
				const uint8_t byte1      = tile_bytes[map_addr + 1];

				if (!props->text_mode_256c) {
					fg_color = byte1 & 15;
					bg_color = byte1 >> 4;
				} else {
					fg_color = byte1;
					bg_color = 0;
				}

				// offset within tilemap of the current tile
				tile_start = tile_index << props->tile_size_log2;
			}

			// additional bytes to reach the correct column of the tile
			const uint16_t x_add       = xx >> 3;
			const uint32_t tile_offset = tile_start + y_add + x_add;

			s           = video_space_read(props->tile_base + tile_offset);
			color_shift = max_pixels_per_byte;
		}

		// convert tile byte to indexed color
		const uint8_t col_index = (s >> color_shift) & 1;
		--color_shift;
		layer_line[layer][x] = col_index ? fg_color : bg_color;
	}
}

static void
render_layer_line_tile(uint8_t layer, uint16_t y)
{
	const struct video_layer_properties *props = &prev_layer_properties[1][layer];
	const struct video_layer_properties *props0 = &prev_layer_properties[0][layer];

	const uint8_t max_pixels_per_byte = (8 >> props->color_depth) - 1;
	const int     eff_y               = calc_layer_eff_y(props0, y);
	const uint8_t yy                  = eff_y & props->tileh_max;
	const uint8_t yy_flip             = yy ^ props->tileh_max;
	const uint32_t y_add              = (yy << (props->tilew_log2 + props->color_depth - 3));
	const uint32_t y_add_flip         = (yy_flip << (props->tilew_log2 + props->color_depth - 3));

	const uint32_t map_addr_begin = calc_layer_map_addr_base2(props, props->min_eff_x, eff_y);
	const uint32_t map_addr_end   = calc_layer_map_addr_base2(props, props->max_eff_x, eff_y);
	const int      size           = (map_addr_end - map_addr_begin) + 2;

	uint8_t tile_bytes[512]; // max 256 tiles, 2 bytes each.
	video_space_read_range(tile_bytes, map_addr_begin, size);

	uint8_t  palette_offset;
	bool     vflip;
	bool     hflip;
	uint32_t tile_start;
	uint8_t  s;
	uint8_t  color_shift;
	int8_t   color_shift_incr;

	{
		const int eff_x = calc_layer_eff_x(props, 0);

		// extract all information from the map
		const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

		const uint8_t byte0 = tile_bytes[map_addr];
		const uint8_t byte1 = tile_bytes[map_addr + 1];

		// Tile Flipping
		vflip = (byte1 >> 3) & 1;
		hflip = (byte1 >> 2) & 1;

		palette_offset = byte1 & 0xf0;

		// offset within tilemap of the current tile
		const uint16_t tile_index = byte0 | ((byte1 & 3) << 8);
		tile_start                = tile_index << props->tile_size_log2;

		color_shift_incr = hflip ? props->bits_per_pixel : -props->bits_per_pixel;

		int xx = eff_x & props->tilew_max;
		if (hflip) {
			xx          = xx ^ (props->tilew_max);
			color_shift = 0;
		} else {
			color_shift = props->first_color_pos;
		}

		// additional bytes to reach the correct column of the tile
		uint16_t x_add       = (xx << props->color_depth) >> 3;
		uint32_t tile_offset = tile_start + (vflip ? y_add_flip : y_add) + x_add;

		s = video_space_read(props->tile_base + tile_offset);
	}


	// Render tile line.
	for (int x = 0; x < SCREEN_WIDTH; x++) {
		const int eff_x = calc_layer_eff_x(props, x);

		if ((eff_x & max_pixels_per_byte) == 0) {
			if ((eff_x & props->tilew_max) == 0) {
				// extract all information from the map
				const uint32_t map_addr = calc_layer_map_addr_base2(props, eff_x, eff_y) - map_addr_begin;

				const uint8_t byte0 = tile_bytes[map_addr];
				const uint8_t byte1 = tile_bytes[map_addr + 1];

				// Tile Flipping
				vflip = (byte1 >> 3) & 1;
				hflip = (byte1 >> 2) & 1;

				palette_offset = byte1 & 0xf0;

				// offset within tilemap of the current tile
				const uint16_t tile_index = byte0 | ((byte1 & 3) << 8);
				tile_start                = tile_index << props->tile_size_log2;

				color_shift_incr = hflip ? props->bits_per_pixel : -props->bits_per_pixel;
			}

			int xx = eff_x & props->tilew_max;
			if (hflip) {
				xx = xx ^ (props->tilew_max);
				color_shift = 0;
			} else {
				color_shift = props->first_color_pos;
			}

			// additional bytes to reach the correct column of the tile
			const uint16_t x_add       = (xx << props->color_depth) >> 3;
			const uint32_t tile_offset = tile_start + (vflip ? y_add_flip : y_add) + x_add;

			s = video_space_read(props->tile_base + tile_offset);
		}

		// convert tile byte to indexed color
		uint8_t col_index = (s >> color_shift) & props->color_mask;
		color_shift += color_shift_incr;

		// Apply Palette Offset
		if (col_index > 0 && col_index < 16) {
			col_index += palette_offset;
			if (props->text_mode_256c) {
				col_index |= 0x80;
			}
		}
		layer_line[layer][x] = col_index;
	}
}


static void
render_layer_line_bitmap(uint8_t layer, uint16_t y)
{
	const struct video_layer_properties *props = &prev_layer_properties[1][layer];
//	const struct video_layer_properties *props0 = &prev_layer_properties[0][layer];

	int yy = y % props->tileh;
	// additional bytes to reach the correct line of the tile
	uint32_t y_add = (yy * props->tilew * props->bits_per_pixel) >> 3;

	// Render tile line.
	for (int x = 0; x < SCREEN_WIDTH; x++) {
		int xx = x % props->tilew;

		// extract all information from the map
		uint8_t palette_offset = reg_layer[layer][4] & 0xf;

		// additional bytes to reach the correct column of the tile
		uint16_t x_add = (xx * props->bits_per_pixel) >> 3;
		uint32_t tile_offset = y_add + x_add;
		uint8_t s = video_space_read(props->tile_base + tile_offset);

		// convert tile byte to indexed color
		uint8_t col_index = (s >> (props->first_color_pos - ((xx & props->color_fields_max) << props->color_depth))) & props->color_mask;

		// Apply Palette Offset
		if (col_index > 0 && col_index < 16) {
			col_index += palette_offset << 4;
			if (props->text_mode_256c) {
				col_index |= 0x80;
			}			
		}
		layer_line[layer][x] = col_index;
	}
}

static uint8_t calculate_line_col_index(uint8_t spr_zindex, uint8_t spr_col_index, uint8_t l1_col_index, uint8_t l2_col_index)
{
	uint8_t col_index = 0;
	switch (spr_zindex) {
		case 3:
			col_index = spr_col_index ? spr_col_index : (l2_col_index ? l2_col_index : l1_col_index);
			break;
		case 2:
			col_index = l2_col_index ? l2_col_index : (spr_col_index ? spr_col_index : l1_col_index);
			break;
		case 1:
			col_index = l2_col_index ? l2_col_index : (l1_col_index ? l1_col_index : spr_col_index);
			break;
		case 0:
			col_index = l2_col_index ? l2_col_index : l1_col_index;
			break;
	}
	return col_index;
}

static void
render_line(uint16_t y, float scan_pos_x)
{
	static uint16_t y_prev;
	static uint16_t s_pos_x_p;
	static uint32_t eff_y_fp; // 16.16 fixed point
	static uint32_t eff_x_fp; // 16.16 fixed point

	static uint8_t col_line[SCREEN_WIDTH];

	uint8_t dc_video = reg_composer[0];
	uint16_t vstart = reg_composer[6] << 1;

	if (y != y_prev) {
		y_prev = y;
		s_pos_x_p = 0;

		// Copy the composer array to 2-line history buffer
		// so that the raster effects that happen on a delay take effect
		// at exactly the right time

		// This simulates different effects happening at render,
		// render but delayed until the next line, or applied mid-line
		// at scan-out

		memcpy(prev_reg_composer[1], prev_reg_composer[0], sizeof(*reg_composer) * COMPOSER_SLOTS);
		memcpy(prev_reg_composer[0], reg_composer, sizeof(*reg_composer) * COMPOSER_SLOTS);

		// Same with the layer properties

		memcpy(prev_layer_properties[1], prev_layer_properties[0], sizeof(*layer_properties) * NUM_LAYERS);
		memcpy(prev_layer_properties[0], layer_properties, sizeof(*layer_properties) * NUM_LAYERS);

		if ((dc_video & 3) > 1) { // 480i or 240p
			if ((y >> 1) == 0) {
				eff_y_fp = y*(prev_reg_composer[1][2] << 9);
			} else if ((y & 0xfffe) > vstart) {
				eff_y_fp += (prev_reg_composer[1][2] << 10);
			}
		} else {
			if (y == 0) {
				eff_y_fp = 0;
			} else if (y > vstart) {
				eff_y_fp += (prev_reg_composer[1][2] << 9);
			}
		}
	}

	if ((dc_video & 8) && (dc_video & 3) > 1) { // progressive NTSC/RGB mode
		y &= 0xfffe;
	}

	// refresh palette for next entry
	if (video_palette.dirty) {
		refresh_palette();
	}

	if (y >= SCREEN_HEIGHT) {
		return;
	}

	uint16_t s_pos_x = round(scan_pos_x);
	if (s_pos_x > SCREEN_WIDTH) {
		s_pos_x = SCREEN_WIDTH;
	}

	if (s_pos_x_p == 0) {
		eff_x_fp = 0;
	}

	uint8_t out_mode = reg_composer[0] & 3;

	uint8_t border_color = reg_composer[3];
	uint16_t hstart = reg_composer[4] << 2;
	uint16_t hstop = reg_composer[5] << 2;
	uint16_t vstop = reg_composer[7] << 1;

	uint16_t eff_y = (eff_y_fp >> 16);

	layer_line_enable[0] = dc_video & 0x10;
	layer_line_enable[1] = dc_video & 0x20;
	sprite_line_enable   = dc_video & 0x40;

	// clear layer_line if layer gets disabled
	for (uint8_t layer = 0; layer < 2; layer++) {
		if (!layer_line_enable[layer] && old_layer_line_enable[layer]) {
			for (uint16_t i = s_pos_x_p; i < SCREEN_WIDTH; i++) {
				layer_line[layer][i] = 0;
			}
		}
		if (s_pos_x_p == 0)
			old_layer_line_enable[layer] = layer_line_enable[layer];
	}

	// clear sprite_line if sprites get disabled
	if (!sprite_line_enable && old_sprite_line_enable) {
		for (uint16_t i = s_pos_x_p; i < SCREEN_WIDTH; i++) {
			sprite_line_col[i] = 0;
			sprite_line_z[i] = 0;
			sprite_line_mask[i] = 0;
		}
	}

	if (s_pos_x_p == 0)
		old_sprite_line_enable = sprite_line_enable;



	if (sprite_line_enable) {
		render_sprite_line(eff_y);
	}

	if (warp_mode && (frame_count & 63)) {
		// sprites were needed for the collision IRQ, but we can skip
		// everything else if we're in warp mode, most of the time
		return;
	}

	if (layer_line_enable[0]) {
		if (prev_layer_properties[1][0].text_mode) {
			render_layer_line_text(0, eff_y);
		} else if (prev_layer_properties[1][0].bitmap_mode) {
			render_layer_line_bitmap(0, eff_y);
		} else {
			render_layer_line_tile(0, eff_y);
		}
	}
	if (layer_line_enable[1]) {
		if (prev_layer_properties[1][1].text_mode) {
			render_layer_line_text(1, eff_y);
		} else if (prev_layer_properties[1][1].bitmap_mode) {
			render_layer_line_bitmap(1, eff_y);
		} else {
			render_layer_line_tile(1, eff_y);
		}
	}

	// If video output is enabled, calculate color indices for line.
	if (out_mode != 0) {
		// Add border after if required.
		if (y < vstart || y > vstop) {
			uint32_t border_fill = border_color;
			border_fill = border_fill | (border_fill << 8);
			border_fill = border_fill | (border_fill << 16);
			memset(col_line, border_fill, SCREEN_WIDTH);
		} else {
			hstart = hstart < 640 ? hstart : 640;
			hstop = hstop < 640 ? hstop : 640;

			for (uint16_t x = s_pos_x_p; x < hstart && x < s_pos_x; ++x) {
				col_line[x] = border_color;
			}

			const uint32_t scale = reg_composer[1];
			for (uint16_t x = MAX(hstart, s_pos_x_p); x < hstop && x < s_pos_x; ++x) {
				uint16_t eff_x = eff_x_fp >> 16;
				col_line[x] = calculate_line_col_index(sprite_line_z[eff_x], sprite_line_col[eff_x], layer_line[0][eff_x], layer_line[1][eff_x]);
				eff_x_fp += (scale << 9);
			}
			for (uint16_t x = hstop; x < s_pos_x; ++x) {
				col_line[x] = border_color;
			}
		}
	}

	// Look up all color indices.
	uint32_t* framebuffer4_begin = ((uint32_t*)framebuffer) + (y * SCREEN_WIDTH) + s_pos_x_p;
	{
		uint32_t* framebuffer4 = framebuffer4_begin;
		for (uint16_t x = s_pos_x_p; x < s_pos_x; x++) {
			*framebuffer4++ = video_palette.entries[col_line[x]];
		}
	}

	// NTSC overscan
	if (out_mode == 2) {
		uint32_t* framebuffer4 = framebuffer4_begin;
		for (uint16_t x = s_pos_x_p; x < s_pos_x; x++)
		{
			if (x < SCREEN_WIDTH * TITLE_SAFE_X ||
				x > SCREEN_WIDTH * (1 - TITLE_SAFE_X) ||
				y < SCREEN_HEIGHT * TITLE_SAFE_Y ||
				y > SCREEN_HEIGHT * (1 - TITLE_SAFE_Y)) {

				// Divide RGB elements by 4.
				*framebuffer4 &= 0x00fcfcfc;
				*framebuffer4 >>= 2;
			}
			framebuffer4++;
		}
	}

	s_pos_x_p = s_pos_x;
}

static void
update_isr_and_coll(uint16_t y, uint16_t compare)
{
	if (y == SCREEN_HEIGHT) {
		if (sprite_line_collisions != 0) {
			isr |= 4;
		}
		isr = (isr & 0xf) | sprite_line_collisions;
		sprite_line_collisions = 0;
		isr |= 1; // VSYNC IRQ
	}
	if (y == compare) { // LINE IRQ
		isr |= 2;
	}
}

bool
video_step(float mhz, float steps, bool midline)
{
	uint16_t y = 0;
	bool ntsc_mode = reg_composer[0] & 2;
	bool new_frame = false;
	vga_scan_pos_x += PIXEL_FREQ * steps / mhz;
	if (vga_scan_pos_x > VGA_SCAN_WIDTH) {
		vga_scan_pos_x -= VGA_SCAN_WIDTH;
		if (!ntsc_mode) {
			render_line(vga_scan_pos_y - VGA_Y_OFFSET, VGA_SCAN_WIDTH);
		}
		vga_scan_pos_y++;
		if (vga_scan_pos_y == SCAN_HEIGHT) {
			vga_scan_pos_y = 0;
			if (!ntsc_mode) {
				new_frame = true;
				frame_count++;
			}
		}
		if (!ntsc_mode) {
			update_isr_and_coll(vga_scan_pos_y - VGA_Y_OFFSET, irq_line);
		}
	} else if (midline) {
		if (!ntsc_mode) {
			render_line(vga_scan_pos_y - VGA_Y_OFFSET, vga_scan_pos_x);
		}
	}
	ntsc_half_cnt += PIXEL_FREQ * steps / mhz;
	if (ntsc_half_cnt > NTSC_HALF_SCAN_WIDTH) {
		ntsc_half_cnt -= NTSC_HALF_SCAN_WIDTH;
		if (ntsc_mode) {
			if (ntsc_scan_pos_y < SCAN_HEIGHT) {
				y = ntsc_scan_pos_y - NTSC_Y_OFFSET_LOW;
				if ((y & 1) == 0) {
					render_line(y, NTSC_HALF_SCAN_WIDTH);
				}
			} else {
				y = ntsc_scan_pos_y - NTSC_Y_OFFSET_HIGH;
				if ((y & 1) == 0) {
					render_line(y | 1, NTSC_HALF_SCAN_WIDTH);
				}
			}
		}
		ntsc_scan_pos_y++;
		if (ntsc_scan_pos_y == SCAN_HEIGHT) {
			reg_composer[0] |= 0x80;
			if (ntsc_mode) {
				new_frame = true;
				frame_count++;
			}
		}
		if (ntsc_scan_pos_y == SCAN_HEIGHT*2) {
			reg_composer[0] &= ~0x80;
			ntsc_scan_pos_y = 0;
			if (ntsc_mode) {
				new_frame = true;
				frame_count++;
			}
		}
		if (ntsc_mode) {
			// this is correct enough for even screen heights
			if (ntsc_scan_pos_y < SCAN_HEIGHT) {
				update_isr_and_coll(ntsc_scan_pos_y - NTSC_Y_OFFSET_LOW, irq_line & ~1);
			} else {
				update_isr_and_coll(ntsc_scan_pos_y - NTSC_Y_OFFSET_HIGH, irq_line & ~1);
			}
		}
	} else if (midline) {
		if (ntsc_mode) {
			if (ntsc_scan_pos_y < SCAN_HEIGHT) {
				y = ntsc_scan_pos_y - NTSC_Y_OFFSET_LOW;
				if ((y & 1) == 0) {
					render_line(y, ntsc_half_cnt);
				}
			} else {
				y = ntsc_scan_pos_y - NTSC_Y_OFFSET_HIGH;
				if ((y & 1) == 0) {
					render_line(y | 1, ntsc_half_cnt);
				}
			}
		}
	}

	return new_frame;
}

bool
video_get_irq_out()
{
	uint8_t tmp_isr = isr | (pcm_is_fifo_almost_empty() ? 8 : 0);
	return (tmp_isr & ien) != 0;
}

//
// saves the video memory and register content into a file
//

void
video_save(SDL_RWops *f)
{
	SDL_RWwrite(f, &video_ram[0], sizeof(uint8_t), sizeof(video_ram));
	SDL_RWwrite(f, &reg_composer[0], sizeof(uint8_t), sizeof(reg_composer));
	SDL_RWwrite(f, &palette[0], sizeof(uint8_t), sizeof(palette));
	SDL_RWwrite(f, &reg_layer[0][0], sizeof(uint8_t), sizeof(reg_layer));
	SDL_RWwrite(f, &sprite_data[0], sizeof(uint8_t), sizeof(sprite_data));
}

bool
video_update()
{
	static bool cmd_down = false;
	bool mouse_changed = false;

	// for activity LED, overlay red 8x4 square into top right of framebuffer
	// for progressive modes, draw LED only on even scanlines
	for (int y = 0; y < 4; y+=1+!!((reg_composer[0] & 0x0b) > 0x09)) {
		for (int x = SCREEN_WIDTH - 8; x < SCREEN_WIDTH; x++) {
			uint8_t b = framebuffer[(y * SCREEN_WIDTH + x) * 4 + 0];
			uint8_t g = framebuffer[(y * SCREEN_WIDTH + x) * 4 + 1];
			uint8_t r = framebuffer[(y * SCREEN_WIDTH + x) * 4 + 2];
			r = (uint32_t)r * (255 - activity_led) / 255 + activity_led;
			g = (uint32_t)g * (255 - activity_led) / 255;
			b = (uint32_t)b * (255 - activity_led) / 255;
			framebuffer[(y * SCREEN_WIDTH + x) * 4 + 0] = b;
			framebuffer[(y * SCREEN_WIDTH + x) * 4 + 1] = g;
			framebuffer[(y * SCREEN_WIDTH + x) * 4 + 2] = r;
			framebuffer[(y * SCREEN_WIDTH + x) * 4 + 3] = 0x00;
		}
	}

	SDL_UpdateTexture(sdlTexture, NULL, framebuffer, SCREEN_WIDTH * 4);

	if (record_gif > RECORD_GIF_PAUSED) {
		if(!GifWriteFrame(&gif_writer, framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, 2, 8, false)) {
			// if that failed, stop recording
			GifEnd(&gif_writer);
			record_gif = RECORD_GIF_DISABLED;
			printf("Unexpected end of recording.\n");
		}
		if (record_gif == RECORD_GIF_SINGLE) { // if single-shot stop recording
			record_gif = RECORD_GIF_PAUSED;  // need to close in video_end()
		}
	}

	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);

	if (debugger_enabled && showDebugOnRender != 0) {
		DEBUGRenderDisplay(SCREEN_WIDTH, SCREEN_HEIGHT);
		SDL_RenderPresent(renderer);
		return true;
	}

	SDL_RenderPresent(renderer);

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			return false;
		}
		if (event.type == SDL_KEYDOWN) {
			bool consumed = false;
			if (cmd_down && !(disable_emu_cmd_keys || mouse_grabbed)) {
				if (event.key.keysym.sym == SDLK_s) {
					machine_dump("user keyboard request");
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_r) {
					machine_reset();
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_BACKSPACE) {
					machine_nmi();
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_v) {
					machine_paste(SDL_GetClipboardText());
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_f || event.key.keysym.sym == SDLK_RETURN) {
					is_fullscreen = !is_fullscreen;
					SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_PLUS || event.key.keysym.sym == SDLK_EQUALS) {
					machine_toggle_warp();
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_a) {
					sdcard_attach();
					consumed = true;
				} else if (event.key.keysym.sym == SDLK_d) {
					sdcard_detach();
					consumed = true;
#ifndef __EMSCRIPTEN__
				} else if (event.key.keysym.sym == SDLK_p) {
					screenshot();
					consumed = true;
#endif
				}
			}
			if (cmd_down) {
				if (event.key.keysym.sym == SDLK_m) {
					mousegrab_toggle();
					consumed = true;
				}
			}
			if (!consumed) {
				if (event.key.keysym.scancode == LSHORTCUT_KEY || event.key.keysym.scancode == RSHORTCUT_KEY) {
					cmd_down = true;
				}
				handle_keyboard(true, event.key.keysym.sym, event.key.keysym.scancode);
			}
			continue;
		}
		if (event.type == SDL_KEYUP) {
			if (event.key.keysym.scancode == LSHORTCUT_KEY || event.key.keysym.scancode == RSHORTCUT_KEY) {
				cmd_down = false;
			}
			handle_keyboard(false, event.key.keysym.sym, event.key.keysym.scancode);
			continue;
		}
		if (event.type == SDL_MOUSEBUTTONDOWN) {
			switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					mouse_button_down(0);
					mouse_changed = true;
					break;
				case SDL_BUTTON_RIGHT:
					mouse_button_down(1);
					mouse_changed = true;
					break;
				case SDL_BUTTON_MIDDLE:
					mouse_button_down(2);
					mouse_changed = true;
					break;
			}
		}
		if (event.type == SDL_MOUSEBUTTONUP) {
			switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					mouse_button_up(0);
					mouse_changed = true;
					break;
				case SDL_BUTTON_RIGHT:
					mouse_button_up(1);
					mouse_changed = true;
					break;
				case SDL_BUTTON_MIDDLE:
					mouse_button_up(2);
					mouse_changed = true;
					break;
			}
		}
		if (event.type == SDL_MOUSEMOTION) {
			static int mouse_x;
			static int mouse_y;
			if (mouse_grabbed) {
				mouse_move(event.motion.xrel, event.motion.yrel);
			} else {
				mouse_move(event.motion.x - mouse_x, event.motion.y - mouse_y);
			}
			mouse_x = event.motion.x;
			mouse_y = event.motion.y;
			mouse_changed = true;
		}
		if (event.type == SDL_MOUSEWHEEL) {
			mouse_set_wheel(event.wheel.y);
			mouse_changed = true;
		}

		if (event.type == SDL_JOYDEVICEADDED) {
			joystick_add(event.jdevice.which);
		}
	    if (event.type == SDL_JOYDEVICEREMOVED) {
		    joystick_remove(event.jdevice.which);
	    }
	    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
		    joystick_button_down(event.cbutton.which, event.cbutton.button);
	    }
		if (event.type == SDL_CONTROLLERBUTTONUP) {
		    joystick_button_up(event.cbutton.which, event.cbutton.button);
	    }

	}
	if (mouse_changed) {
		mouse_send_state();
	}
	return true;
}

void
video_end()
{
	if (debugger_enabled) {
		DEBUGFreeUI();
	}

	if (record_gif != RECORD_GIF_DISABLED) {
		GifEnd(&gif_writer);
		record_gif = RECORD_GIF_DISABLED;
	}

	is_fullscreen = false;
	SDL_SetWindowFullscreen(window, 0);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}


static const int increments[32] = {
	0,   0,
	1,   -1,
	2,   -2,
	4,   -4,
	8,   -8,
	16,  -16,
	32,  -32,
	64,  -64,
	128, -128,
	256, -256,
	512, -512,
	40,  -40,
	80,  -80,
	160, -160,
	320, -320,
	640, -640,
};

uint32_t
video_get_address(uint8_t sel)
{
	uint32_t address = io_addr[sel];
	return address;
}

uint32_t
get_and_inc_address(uint8_t sel, bool write)
{
	uint32_t address = io_addr[sel];
	int16_t incr = increments[io_inc[sel]];

	if (fx_4bit_mode && fx_nibble_incr[sel] && !incr) {
		if (fx_nibble_bit[sel]) {
			if ((io_inc[sel] & 1) == 0) io_addr[sel] += 1;
			fx_nibble_bit[sel] = 0;
		} else {
			if (io_inc[sel] & 1) io_addr[sel] -= 1;
			fx_nibble_bit[sel] = 1;
		}
	}

	if (sel == 1 && fx_16bit_hop) {
		if (incr == 4) {
			if (fx_16bit_hop_align == (address & 0x3))
				incr = 1;
			else
				incr = 3;
		} else if (incr == 320) {
			if (fx_16bit_hop_align == (address & 0x3))
				incr = 1;
			else
				incr = 319;
		}
	}

	io_addr[sel] += incr;

	if (sel == 1 && fx_addr1_mode == 1) { // FX line draw mode
		fx_x_pixel_position += fx_x_pixel_increment;
		if (fx_x_pixel_position & 0x10000) {
			fx_x_pixel_position &= ~0x10000;
			if (fx_4bit_mode && fx_nibble_incr[0]) {
				if (fx_nibble_bit[1]) {
					if ((io_inc[0] & 1) == 0) io_addr[1] += 1;
					fx_nibble_bit[1] = 0;
				} else {
					if (io_inc[0] & 1) io_addr[1] -= 1;
					fx_nibble_bit[1] = 1;
				}
			}
			io_addr[1] += increments[io_inc[0]];
		}
	} else if (fx_addr1_mode == 2 && write == false) { // FX polygon fill mode
		fx_x_pixel_position += fx_x_pixel_increment;
		fx_y_pixel_position += fx_y_pixel_increment;
		fx_poly_fill_length = ((int32_t) fx_y_pixel_position >> 16) - ((int32_t) fx_x_pixel_position >> 16);
		if (sel == 0 && fx_cache_byte_cycling && !fx_cache_fill) {
			fx_cache_byte_index = (fx_cache_byte_index + 1) & 3;
		}
		if (sel == 1) {
			if (fx_4bit_mode) {
				io_addr[1] = io_addr[0] + (fx_x_pixel_position >> 17);
				fx_nibble_bit[1] = (fx_x_pixel_position >> 16) & 1;
			} else {
				io_addr[1] = io_addr[0] + (fx_x_pixel_position >> 16);
			}
		}
	} else if (sel == 1 && fx_addr1_mode == 3 && write == false) { // FX affine mode
		fx_x_pixel_position += fx_x_pixel_increment;
		fx_y_pixel_position += fx_y_pixel_increment;
	}
	return address;
}

void
fx_affine_prefetch(void)
{
	if (fx_addr1_mode != 3) return; // only if affine mode is selected

	uint32_t address;
	uint8_t affine_x_tile = (fx_x_pixel_position >> 19) & 0xff;
	uint8_t affine_y_tile = (fx_y_pixel_position >> 19) & 0xff;
	uint8_t affine_x_sub_tile = (fx_x_pixel_position >> 16) & 0x07;
	uint8_t affine_y_sub_tile = (fx_y_pixel_position >> 16) & 0x07;

	if (!fx_affine_clip) { // wrap
		affine_x_tile &= fx_affine_map_size - 1;
		affine_y_tile &= fx_affine_map_size - 1;
	}

	if (affine_x_tile >= fx_affine_map_size || affine_y_tile >= fx_affine_map_size) {
		// We clipped, return value for tile 0
		address = fx_affine_tile_base + (affine_y_sub_tile << (3 - fx_4bit_mode)) + (affine_x_sub_tile >> (uint8_t)fx_4bit_mode);
		if (fx_4bit_mode) fx_nibble_bit[1] = 0;
	} else {
		// Get the address within the tile map
		address = fx_affine_map_base + (affine_y_tile * fx_affine_map_size) + affine_x_tile;
		// Now translate that to the tile base address
		uint8_t affine_tile_idx = video_space_read(address);
		address = fx_affine_tile_base + (affine_tile_idx << (6 - fx_4bit_mode));
		// Now add the sub-tile address
		address += (affine_y_sub_tile << (3 - fx_4bit_mode)) + (affine_x_sub_tile >> (uint8_t)fx_4bit_mode);
		if (fx_4bit_mode) fx_nibble_bit[1] = affine_x_sub_tile & 1;
	}
	io_addr[1] = address;
	io_rddata[1] = video_space_read(address);
}

//
// Vera: Internal Video Address Space
//

uint8_t
video_space_read(uint32_t address)
{
	return video_ram[address & 0x1FFFF];
}

static void
video_space_read_range(uint8_t* dest, uint32_t address, uint32_t size)
{
	if (address >= ADDR_VRAM_START && (address+size) <= ADDR_VRAM_END) {
		memcpy(dest, &video_ram[address], size);
	} else {
		for(int i = 0; i < size; ++i) {
			*dest++ = video_space_read(address + i);
		}
	}
}

void
video_space_write(uint32_t address, uint8_t value)
{
	video_ram[address & 0x1FFFF] = value;

	if (address >= ADDR_PSG_START && address < ADDR_PSG_END) {
		audio_render();
		psg_writereg(address & 0x3f, value);
	} else if (address >= ADDR_PALETTE_START && address < ADDR_PALETTE_END) {
		palette[address & 0x1ff] = value;
		video_palette.dirty = true;
	} else if (address >= ADDR_SPRDATA_START && address < ADDR_SPRDATA_END) {
		sprite_data[(address >> 3) & 0x7f][address & 0x7] = value;
		refresh_sprite_properties((address >> 3) & 0x7f);
	}
}


void
fx_video_space_write(uint32_t address, bool nibble, uint8_t value)
{
	if (fx_4bit_mode) {
		if (nibble) {
			if (!fx_trans_writes || (value & 0x0f) > 0) {
				video_ram[address & 0x1FFFF] = (video_ram[address & 0x1FFFF] & 0xf0) | (value & 0x0f);
			}
		} else {
			if (!fx_trans_writes || (value & 0xf0) > 0) {
				video_ram[address & 0x1FFFF] = (video_ram[address & 0x1FFFF] & 0x0f) | (value & 0xf0);
			}
		}
	} else {
		if (!fx_trans_writes || value > 0) video_ram[address & 0x1FFFF] = value;
	}
	if (address >= ADDR_PSG_START && address < ADDR_PSG_END) {
		audio_render();
		psg_writereg(address & 0x3f, value);
	} else if (address >= ADDR_PALETTE_START && address < ADDR_PALETTE_END) {
		palette[address & 0x1ff] = value;
		video_palette.dirty = true;
	} else if (address >= ADDR_SPRDATA_START && address < ADDR_SPRDATA_END) {
		sprite_data[(address >> 3) & 0x7f][address & 0x7] = value;
		refresh_sprite_properties((address >> 3) & 0x7f);
	}
}

void
fx_vram_cache_write(uint32_t address, uint8_t value, uint8_t mask)
{
	if (!fx_trans_writes || value > 0) {
		switch (mask) {
			case 0:
				video_ram[address & 0x1FFFF] = value;
				break;
			case 1:
				video_ram[address & 0x1FFFF] = (video_ram[address & 0x1FFFF] & 0x0f) | (value & 0xf0);
				break;
			case 2:
				video_ram[address & 0x1FFFF] = (video_ram[address & 0x1FFFF] & 0xf0) | (value & 0x0f);
				break;
			case 3:
				// Do nothing
				break;
		}
	}
}


//
// Vera: 6502 I/O Interface
//
// if debugOn, read without any side effects (registers & memory unchanged)

uint8_t video_read(uint8_t reg, bool debugOn) {
	bool ntsc_mode = reg_composer[0] & 2;
	uint16_t scanline = ntsc_mode ? ntsc_scan_pos_y % SCAN_HEIGHT : vga_scan_pos_y;
	if (scanline >= 512) scanline=511;

	switch (reg & 0x1F) {
		case 0x00: return io_addr[io_addrsel] & 0xff;
		case 0x01: return (io_addr[io_addrsel] >> 8) & 0xff;
		case 0x02: return (io_addr[io_addrsel] >> 16) | (fx_nibble_bit[io_addrsel] << 1) | (fx_nibble_incr[io_addrsel] << 2) | (io_inc[io_addrsel] << 3);
		case 0x03:
		case 0x04: {
			if (debugOn) {
				return io_rddata[reg - 3];
			}

			//bool nibble = fx_nibble_bit[reg - 3];
			uint32_t address = get_and_inc_address(reg - 3, false);

			uint8_t value = io_rddata[reg - 3];

			if (reg == 4 && fx_addr1_mode == 3)
				fx_affine_prefetch();
			else
				io_rddata[reg - 3] = video_space_read(io_addr[reg - 3]);

			if (fx_cache_fill) {
				if (fx_4bit_mode) {
					if (fx_cache_nibble_index) {
						fx_cache[fx_cache_byte_index] = (fx_cache[fx_cache_byte_index] & 0xf0) | (value & 0x0f);
						fx_cache_nibble_index = 0;
						fx_cache_byte_index = ((fx_cache_byte_index + 1) & 0x3);
					} else {
						fx_cache[fx_cache_byte_index] = (fx_cache[fx_cache_byte_index] & 0x0f) | (value & 0xf0);
						fx_cache_nibble_index = 1;
					}
				} else {
					fx_cache[fx_cache_byte_index] = value;
					if (fx_cache_increment_mode)
						fx_cache_byte_index = (fx_cache_byte_index & 0x2) | ((fx_cache_byte_index + 1) & 0x1);
					else
						fx_cache_byte_index = ((fx_cache_byte_index + 1) & 0x3);
				}
			}

			if (log_video) {
				printf("READ  video_space[$%X] = $%02X\n", address, value);
			}
			return value;
		}
		case 0x05: return (io_dcsel << 1) | io_addrsel;
		case 0x06: return ((irq_line & 0x100) >> 1) | ((scanline & 0x100) >> 2) | (ien & 0xF);
		case 0x07: return isr | (pcm_is_fifo_almost_empty() ? 8 : 0);
		case 0x08: return scanline & 0xFF;

		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C: {
			int i = reg - 0x09 + (io_dcsel << 2);
			switch (i) {
				case 0x00:
				case 0x01:
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
				case 0x08:
					// DCSEL = [0,1] with any composer register, or [2] at $9f29
					return reg_composer[i];
					break;
				case 0x16: // DCSEL=5, 0x9F2B
					if (fx_poly_fill_length >= 768) {
						return ((fx_2bit_poly && fx_addr1_mode == 2) ? 0x00 : 0x80);
					}
					if (fx_4bit_mode) {
						if (fx_2bit_poly && fx_addr1_mode == 2) {
							return ((fx_y_pixel_position & 0x00008000) >> 8) |
								((fx_x_pixel_position >> 11) & 0x60) |
								((fx_x_pixel_position >> 14) & 0x10) |
								((fx_poly_fill_length & 0x0007) << 1) |
								((fx_x_pixel_position & 0x00008000) >> 15);
						} else {
							return ((!!(fx_poly_fill_length & 0xfff8)) << 7) |
								((fx_x_pixel_position >> 11) & 0x60) |
								((fx_x_pixel_position >> 14) & 0x10) |
								((fx_poly_fill_length & 0x0007) << 1);
						}
					} else {
						return ((!!(fx_poly_fill_length & 0xfff0)) << 7) |
							((fx_x_pixel_position >> 11) & 0x60) |
							((fx_poly_fill_length & 0x000f) << 1);
					}
					break;
				case 0x17: // DCSEL=5, 0x9F2C
					return ((fx_poly_fill_length & 0x03f8) >> 2);
					break;
				case 0x18: // DCSEL=6, 0x9F29
					fx_mult_accumulator = 0;
					// fall out of the switch
					break;
				case 0x19: // DCSEL=6, 0x9F2A
					; // <- avoids the error in some compilers about a declaration after a label
					int32_t m_result = (int16_t)((fx_cache[1] << 8) | fx_cache[0]) * (int16_t)((fx_cache[3] << 8) | fx_cache[2]);
					if (fx_subtract)
						fx_mult_accumulator -= m_result;
					else
						fx_mult_accumulator += m_result;
					// fall out of the switch
					break;
				default:
					// The rest of the space is write-only,
					// so reading the values out instead returns the version string.
					// fall out of the switch
					break;
			}
			return vera_version_string[i % 4];
			break;
		}
		case 0x0D:
		case 0x0E:
		case 0x0F:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13: return reg_layer[0][reg - 0x0D];

		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A: return reg_layer[1][reg - 0x14];

		case 0x1B: audio_render(); return pcm_read_ctrl();
		case 0x1C: return pcm_read_rate();
		case 0x1D: return 0;

		case 0x1E:
		case 0x1F: return vera_spi_read(reg & 1);
	}
	return 0;
}

void video_write(uint8_t reg, uint8_t value) {
	// if (reg > 4) {
	// 	printf("ioregisters[0x%02X] = 0x%02X\n", reg, value);
	// }
	//	printf("ioregisters[%d] = $%02X\n", reg, value);
	switch (reg & 0x1F) {
		case 0x00:
			if (fx_2bit_poly && fx_4bit_mode && fx_addr1_mode == 2 && io_addrsel == 1) {
				fx_2bit_poking = true;
				io_addr[1] = (io_addr[1] & 0x1fffc) | (value & 0x3);
			} else {
				io_addr[io_addrsel] = (io_addr[io_addrsel] & 0x1ff00) | value;
				if (fx_16bit_hop && io_addrsel == 1)
					fx_16bit_hop_align = value & 3;
			}
			io_rddata[io_addrsel] = video_space_read(io_addr[io_addrsel]);
			break;
		case 0x01:
			io_addr[io_addrsel] = (io_addr[io_addrsel] & 0x100ff) | (value << 8);
			io_rddata[io_addrsel] = video_space_read(io_addr[io_addrsel]);
			break;
		case 0x02:
			io_addr[io_addrsel] = (io_addr[io_addrsel] & 0x0ffff) | ((value & 0x1) << 16);
			fx_nibble_bit[io_addrsel] = (value >> 1) & 0x1;
			fx_nibble_incr[io_addrsel] = (value >> 2) & 0x1;
			io_inc[io_addrsel]  = value >> 3;
			io_rddata[io_addrsel] = video_space_read(io_addr[io_addrsel]);
			break;
		case 0x03:
		case 0x04: {
			if (fx_2bit_poking && fx_addr1_mode) {
				fx_2bit_poking = false;
				uint8_t mask = value >> 6;
				switch (mask) {
					case 0x00:
						video_ram[io_addr[1] & 0x1FFFF] = (fx_cache[fx_cache_byte_index] & 0xc0) | (io_rddata[1] & 0x3f);
						break;
					case 0x01:
						video_ram[io_addr[1] & 0x1FFFF] = (fx_cache[fx_cache_byte_index] & 0x30) | (io_rddata[1] & 0xcf);
						break;
					case 0x02:
						video_ram[io_addr[1] & 0x1FFFF] = (fx_cache[fx_cache_byte_index] & 0x0c) | (io_rddata[1] & 0xf3);
						break;
					case 0x03:
						video_ram[io_addr[1] & 0x1FFFF] = (fx_cache[fx_cache_byte_index] & 0x03) | (io_rddata[1] & 0xfc);
						break;
				}
				break; // break out of the enclosing switch statement early, too
			}

			if (enable_midline)
				video_step(MHZ, 0, true); // potential midline raster effect
			bool nibble = fx_nibble_bit[reg - 3];
			uint32_t address = get_and_inc_address(reg - 3, true);
			if (log_video) {
				printf("WRITE video_space[$%X] = $%02X\n", address, value);
			}

			if (fx_cache_write) {
				address &= 0x1fffc;
				if (fx_cache_byte_cycling) {
					fx_vram_cache_write(address+0, fx_cache[fx_cache_byte_index], value & 0x03);
					fx_vram_cache_write(address+1, fx_cache[fx_cache_byte_index], (value >> 2) & 0x03);
					fx_vram_cache_write(address+2, fx_cache[fx_cache_byte_index], (value >> 4) & 0x03);
					fx_vram_cache_write(address+3, fx_cache[fx_cache_byte_index], value >> 6);
				} else {
					if (fx_multiplier) {
						int32_t m_result = (int16_t)((fx_cache[1] << 8) | fx_cache[0]) * (int16_t)((fx_cache[3] << 8) | fx_cache[2]);
						if (fx_subtract)
							m_result = fx_mult_accumulator - m_result;
						else
							m_result = fx_mult_accumulator + m_result;
						fx_vram_cache_write(address+0, (m_result) & 0xff, value & 0x03);
						fx_vram_cache_write(address+1, (m_result >> 8) & 0xff, (value >> 2) & 0x03);
						fx_vram_cache_write(address+2, (m_result >> 16) & 0xff, (value >> 4) & 0x03);
						fx_vram_cache_write(address+3, (m_result >> 24) & 0xff, value >> 6);
					} else {
						fx_vram_cache_write(address+0, fx_cache[0], value & 0x03);
						fx_vram_cache_write(address+1, fx_cache[1], (value >> 2) & 0x03);
						fx_vram_cache_write(address+2, fx_cache[2], (value >> 4) & 0x03);
						fx_vram_cache_write(address+3, fx_cache[3], value >> 6);
					}
				}
			} else {
				if (fx_cache_byte_cycling) {
					if (fx_4bit_mode) {
						fx_vram_cache_write(address, fx_cache[fx_cache_byte_index], nibble+1);
					} else {
						fx_vram_cache_write(address, fx_cache[fx_cache_byte_index], 0);
					}
				} else {
					fx_video_space_write(address, nibble, value); // Normal write
				}
			}

			io_rddata[reg - 3] = video_space_read(io_addr[reg - 3]);
			break;
		}
		case 0x05:
			if (value & 0x80) {
				video_reset();
			}
			io_dcsel = (value >> 1) & 0x3f;
			io_addrsel = value & 1;
			break;
		case 0x06:
			irq_line = (irq_line & 0xFF) | ((value >> 7) << 8);
			ien = value & 0xF;
			break;
		case 0x07:
			isr &= value ^ 0xff;
			break;
		case 0x08:
			irq_line = (irq_line & 0x100) | value;
			break;

		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C: {
			video_step(MHZ, 0, true); // potential midline raster effect
			int i = reg - 0x09 + (io_dcsel << 2);
			if (i == 0) {
				// if progressive mode field goes from 0 to 1
				// or if mode goes from vga to something else with
				// progressive mode on, clear the framebuffer
				if (((reg_composer[0] & 0x8) == 0 && (value & 0x8)) ||
					((reg_composer[0] & 0x3) == 1 && (value & 0x3) > 1 && (value & 0x8))) {
					memset(framebuffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT * 4);
				}

				// interlace field bit is read-only
				reg_composer[0] = (reg_composer[0] & ~0x7f) | (value & 0x7f);
				video_palette.dirty = true;
			} else {
				reg_composer[i] = value;
			}

			switch (i) {
				case 0x08: // DCSEL=2, $9F29
					fx_addr1_mode = value & 0x03;
					fx_4bit_mode = (value & 0x04) >> 2;
					fx_16bit_hop = (value & 0x08) >> 3;
					fx_cache_byte_cycling = (value & 0x10) >> 4;
					fx_cache_fill = (value & 0x20) >> 5;
					fx_cache_write = (value & 0x40) >> 6;
					fx_trans_writes = (value & 0x80) >> 7;
					break;
				case 0x09: // DCSEL=2, $9F2A
					fx_affine_tile_base = (value & 0xfc) << 9;
					fx_affine_clip = (value & 0x02) >> 1;
					fx_2bit_poly = (value & 0x01);
					break;
				case 0x0a: // DCSEL=2, $9F2B
					fx_affine_map_base = (value & 0xfc) << 9;
					fx_affine_map_size = 2 << ((value & 0x03) << 1);
					break;
				case 0x0b: // DCSEL=2, $9F2C
					fx_cache_increment_mode = value & 0x01;
					fx_cache_nibble_index = (value & 0x02) >> 1;
					fx_cache_byte_index = (value & 0x0c) >> 2;
					fx_multiplier = (value & 0x10) >> 4;
					fx_subtract = (value & 0x20) >> 5;
					if (value & 0x40) { // accumulate
						int32_t m_result = (int16_t)((fx_cache[1] << 8) | fx_cache[0]) * (int16_t)((fx_cache[3] << 8) | fx_cache[2]);
						if (fx_subtract)
							fx_mult_accumulator -= m_result;
						else
							fx_mult_accumulator += m_result;
					}
					if (value & 0x80) { // reset accumulator
						fx_mult_accumulator = 0;
					}
					break;
				case 0x0c: // DCSEL=3, $9F29
					fx_x_pixel_increment = ((((reg_composer[0x0d] & 0x7f) << 15) + (reg_composer[0x0c] << 7)) // base value
						| ((reg_composer[0x0d] & 0x40) ? 0xffc00000 : 0)) // sign extend if negative
						<< 5*(!!(reg_composer[0x0d] & 0x80)); // multiply by 32 if flag set
					break;
				case 0x0d: // DCSEL=3, $9F2A
					fx_x_pixel_increment = ((((reg_composer[0x0d] & 0x7f) << 15) + (reg_composer[0x0c] << 7)) // base value
						| ((reg_composer[0x0d] & 0x40) ? 0xffc00000 : 0)) // sign extend if negative
						<< 5*(!!(reg_composer[0x0d] & 0x80)); // multiply by 32 if flag set
					// Reset subpixel to 0.5
					fx_x_pixel_position = (fx_x_pixel_position & 0x07ff0000) | 0x00008000;
					break;
				case 0x0e: // DCSEL=3, $9F2B
					fx_y_pixel_increment = ((((reg_composer[0x0f] & 0x7f) << 15) + (reg_composer[0x0e] << 7)) // base value
						| ((reg_composer[0x0f] & 0x40) ? 0xffc00000 : 0)) // sign extend if negative
						<< 5*(!!(reg_composer[0x0f] & 0x80)); // multiply by 32 if flag set
					break;
				case 0x0f: // DCSEL=3, $9F2C
					fx_y_pixel_increment = ((((reg_composer[0x0f] & 0x7f) << 15) + (reg_composer[0x0e] << 7)) // base value
						| ((reg_composer[0x0f] & 0x40) ? 0xffc00000 : 0)) // sign extend if negative
						<< 5*(!!(reg_composer[0x0f] & 0x80)); // multiply by 32 if flag set
					// Reset subpixel to 0.5
					fx_y_pixel_position = (fx_y_pixel_position & 0x07ff0000) | 0x00008000;
					break;
				case 0x10: // DCSEL=4, $9F29
					fx_x_pixel_position = (fx_x_pixel_position & 0x0700ff80) | (value << 16);
					fx_affine_prefetch();
					break;
				case 0x11: // DCSEL=4, $9F2A
					fx_x_pixel_position = (fx_x_pixel_position & 0x00ffff00) | ((value & 0x7) << 24) | (value & 0x80);
					fx_affine_prefetch();
					break;
				case 0x12: // DCSEL=4, $9F2B
					fx_y_pixel_position = (fx_y_pixel_position & 0x0700ff80) | (value << 16);
					fx_affine_prefetch();
					break;
				case 0x13: // DCSEL=4, $9F2C
					fx_y_pixel_position = (fx_y_pixel_position & 0x00ffff00) | ((value & 0x7) << 24) | (value & 0x80);
					fx_affine_prefetch();
					break;
				case 0x14: // DCSEL=5, $9F29
					fx_x_pixel_position = (fx_x_pixel_position & 0x07ff0080) | (value << 8);
					break;
				case 0x15: // DCSEL=5, $9F2A
					fx_y_pixel_position = (fx_y_pixel_position & 0x07ff0080) | (value << 8);
					break;
				case 0x18: // DCSEL=6, $9F29
					fx_cache[0] = value;
					break;
				case 0x19: // DCSEL=6, $9F2A
					fx_cache[1] = value;
					break;
				case 0x1a: // DCSEL=6, $9F2B
					fx_cache[2] = value;
					break;
				case 0x1b: // DCSEL=6, $9F2C
					fx_cache[3] = value;
					break;
			}
			break;
		}

		case 0x0D:
		case 0x0E:
		case 0x0F:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			video_step(MHZ, 0, true); // potential midline raster effect
			reg_layer[0][reg - 0x0D] = value;
			refresh_layer_properties(0);
			break;

		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:
			video_step(MHZ, 0, true); // potential midline raster effect
			reg_layer[1][reg - 0x14] = value;
			refresh_layer_properties(1);
			break;

		case 0x1B: audio_render(); pcm_write_ctrl(value); break;
		case 0x1C: audio_render(); pcm_write_rate(value); break;
		case 0x1D: audio_render(); pcm_write_fifo(value); break;

		case 0x1E:
		case 0x1F:
			vera_spi_write(reg & 1, value);
			break;
	}
}

void
video_update_title(const char* window_title)
{
	SDL_SetWindowTitle(window, window_title);
}

bool video_is_tilemap_address(int addr)
{
	for (int l = 0; l < 2; ++l) {
		struct video_layer_properties *props = &layer_properties[l];
		if (addr < props->map_base) {
			continue;
		}
		if (addr >= props->map_base + (2 << (props->mapw_log2 + props->maph_log2))) {
			continue;
		}

		return true;
	}
	return false;
}

bool video_is_tiledata_address(int addr)
{
	for (int l = 0; l < 2; ++l) {
		struct video_layer_properties *props = &layer_properties[l];
		if (addr < props->tile_base) {
			continue;
		}
		int tile_size = props->tilew * props->tileh * props->bits_per_pixel / 8;
		if (addr >= props->tile_base + tile_size * (props->bits_per_pixel == 1 ? 256 : 1024)) {
			continue;
		}

		return true;
	}
	return false;
}

bool video_is_special_address(int addr)
{
	return addr >= 0x1F9C0;
}

void
stop6502(uint16_t address) {
	if (debugger_enabled) {
		DEBUGBreakToDebugger();
	} else if (testbench) {
		printf("STP\n");
        fflush(stdout);
	} else {
		int return_btn;
		char error_message[80];
		const SDL_MessageBoxButtonData btns[2] = {
			{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Reset Machine"},
			{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, -1, "Ignore"}
		};
		const SDL_MessageBoxData msg_box = {
			SDL_MESSAGEBOX_ERROR, window, "Error", error_message,
			2, btns, NULL
		};

		sprintf(error_message, "Encountered stop instruction at address $%04X. CPU cannot continue.", address);
		if (SDL_ShowMessageBox(&msg_box, &return_btn) == 0 && return_btn == 0) {
			machine_reset();
		};
	}
}
