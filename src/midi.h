// Commander X16 Emulator
// Copyright (c) 2024 MooingLemur
// All rights reserved. License: 2-clause BSD

#pragma once

#ifdef HAS_FLUIDSYNTH
#include <fluidsynth.h>
#endif

#define MIDI_UART_OSC_RATE_MHZ 16.0f
#define MIDI_UART_PRIMARY_DIVIDER 16

#define FS_MIDI_TEXT 0x01
#define FS_NOTE_OFF 0x80
#define FS_NOTE_ON 0x90
#define FS_KEY_PRESSURE 0xa0
#define FS_CONTROL_CHANGE 0xb0
#define FS_PROGRAM_CHANGE 0xc0
#define FS_CHANNEL_PRESSURE 0xd0
#define FS_PITCH_BEND 0xe0
#define FS_MIDI_SYSEX 0xf0
#define FS_MIDI_TIME_CODE 0xf1
#define FS_MIDI_SONG_POSITION 0xf2
#define FS_MIDI_SONG_SELECT 0xf3
#define FS_MIDI_TUNE_REQUEST 0xf6
#define FS_MIDI_EOX 0xf7
#define FS_MIDI_SYNC 0xf8
#define FS_MIDI_TICK 0xf9
#define FS_MIDI_START 0xfa
#define FS_MIDI_CONTINUE 0xfb
#define FS_MIDI_STOP 0xfc
#define FS_MIDI_ACTIVE_SENSING 0xfe
#define FS_MIDI_SYSTEM_RESET 0xff

#define FL_DEFAULT_GAIN 0.2f

void midi_init(void);
void midi_serial_init(void);
void midi_serial_step(int clocks);
uint8_t midi_serial_read(uint8_t reg, bool debugOn);
void midi_serial_write(uint8_t reg, uint8_t val);
void midi_load_sf2(uint8_t* filename);
void midi_synth_render(int16_t* buf, int len);
bool midi_serial_irq(void);

extern bool fs_midi_in_connect;
