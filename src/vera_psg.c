// Commander X16 Emulator
// Copyright (c) 2020 Frank van den Hoef
// All rights reserved. License: 2-clause BSD

#include "vera_psg.h"

#include <stdbool.h>
#include <string.h>

enum waveform {
	WF_PULSE = 0,
	WF_SAWTOOTH,
	WF_TRIANGLE,
	WF_NOISE,
};

struct channel {
	uint16_t freq;
	uint8_t  volume;
	bool     left, right;
	uint8_t  pw;
	uint8_t  waveform;

	uint16_t noiseval;
	uint32_t phase;
};

static struct channel channels[16];

static uint8_t volume_lut[64] = {
	 0,                                  1,  1,  1,
	 2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,
	 4,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,
	 8,  8,  9,  9, 10, 11, 11, 12, 13, 14, 14, 15,
	16, 17, 18, 19, 21, 22, 23, 25, 26, 28, 29, 31,
	33, 35, 37, 39, 42, 44, 47, 50, 52, 56, 59, 63
};

static uint16_t noise_state;

void
psg_reset(void)
{
	memset(channels, 0, sizeof(channels));
	noise_state = 1;
}

void
psg_writereg(uint8_t reg, uint8_t val)
{
	reg &= 0x3f;

	int ch  = reg / 4;
	int idx = reg & 3;

	switch (idx) {
		case 0: channels[ch].freq = (channels[ch].freq & 0xFF00) | val; break;
		case 1: channels[ch].freq = (channels[ch].freq & 0x00FF) | (val << 8); break;
		case 2: {
			channels[ch].right  = (val & 0x80) != 0;
			channels[ch].left   = (val & 0x40) != 0;
			channels[ch].volume = volume_lut[val & 0x3F];
			break;
		}
		case 3: {
			channels[ch].pw       = val & 0x3F;
			channels[ch].waveform = val >> 6;
			break;
		}
	}
}

static void
render(int16_t *left, int16_t *right)
{
	int16_t l = 0;
	int16_t r = 0;

	for (int i = 0; i < 16; i++) {
		// In FPGA implementation, noise values are generated every system clock and
		// the channel update is run sequentially. So, even if both two channels are
		// fetching a noise value in the same sample, they should have different values
		noise_state = (noise_state << 1) | (((noise_state >> 1) ^ (noise_state >> 2) ^ (noise_state >> 4) ^ (noise_state >> 15)) & 1);

		struct channel *ch = &channels[i];

		uint32_t new_phase = (ch->left || ch->right) ? ((ch->phase + ch->freq) & 0x1FFFF) : 0;
		if ((ch->phase & 0x10000) != (new_phase & 0x10000)) {
			ch->noiseval = noise_state & 0x3F;
		}
		ch->phase = new_phase;

		uint32_t v = 0;
		switch (ch->waveform) {
			case WF_PULSE: v = ((ch->phase >> 10) > ch->pw) ? 0 : 0x3F; break;
			case WF_SAWTOOTH: v = ch->phase >> 11; break;
			case WF_TRIANGLE: v = (ch->phase & 0x10000) ? (~(ch->phase >> 10) & 0x3F) : ((ch->phase >> 10) & 0x3F); break;
			case WF_NOISE: v = ch->noiseval; break;
		}
		int16_t sv = (v ^ 0x20);
		if (sv & 0x20) {
			sv |= 0xFFC0;
		}

		int16_t val = sv * ch->volume;

		if (ch->left) {
			l += val;
		}
		if (ch->right) {
			r += val;
		}
	}

	*left  = l;
	*right = r;
}

void
psg_render(int16_t *buf, unsigned num_samples)
{
	while (num_samples--) {
		render(&buf[0], &buf[1]);
		buf += 2;
	}
}
