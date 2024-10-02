// Commander X16 Emulator
// Copyright (c) 2024 MooingLemur
// All rights reserved. License: 2-clause BSD

#pragma once

#ifndef __EMSCRIPTEN__
#include <fluidsynth.h>
#endif

#define MIDI_UART_OSC_RATE_MHZ 16.0f
#define MIDI_UART_PRIMARY_DIVIDER 16

#define NOTE_OFF 0x80
#define NOTE_ON 0x90
#define KEY_PRESSURE 0xa0
#define CONTROL_CHANGE 0xb0
#define PROGRAM_CHANGE 0xc0
#define CHANNEL_PRESSURE 0xd0
#define PITCH_BEND 0xe0
#define MIDI_SYSEX 0xf0
#define MIDI_TIME_CODE 0xf1
#define MIDI_SONG_POSITION 0xf2
#define MIDI_SONG_SELECT 0xf3
#define MIDI_TUNE_REQUEST 0xf6
#define MIDI_EOX 0xf7
#define MIDI_SYNC 0xf8
#define MIDI_TICK 0xf9
#define MIDI_START 0xfa
#define MIDI_CONTINUE 0xfb
#define MIDI_STOP 0xfc
#define MIDI_ACTIVE_SENSING 0xfe
#define MIDI_SYSTEM_RESET 0xff


void midi_init();
void midi_serial_init();
void midi_serial_step(int clocks);
uint8_t midi_serial_read(uint8_t reg, bool debugOn);
void midi_serial_write(uint8_t reg, uint8_t val);
void midi_load_sf2(uint8_t* filename);
void midi_synth_render(int16_t* buf, int len);
bool midi_serial_irq(void);
