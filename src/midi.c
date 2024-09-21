// Commander X16 Emulator
// Copyright (c) 2024 MooingLemur
// All rights reserved. License: 2-clause BSD

#include "glue.h"
#include "midi.h"

#ifdef _WIN32
    #include <windows.h>
    #define LIBRARY_TYPE HMODULE
    #define LOAD_LIBRARY(name) LoadLibrary(name)
    #define GET_FUNCTION(lib, name) GetProcAddress(lib, name)
    #define CLOSE_LIBRARY(lib) FreeLibrary(lib)
#else
    #include <dlfcn.h>
    #define LIBRARY_TYPE void*
    #define LOAD_LIBRARY(name) dlopen(name, RTLD_LAZY)
    #define GET_FUNCTION(lib, name) dlsym(lib, name)
    #define CLOSE_LIBRARY(lib) dlclose(lib)
#endif

#define ASSIGN_FUNCTION(lib, var, name) {\
    var = (void *)GET_FUNCTION(lib, name);\
    if (!var) { fprintf(stderr, "Unable to find symbol for '%s'\n", name); CLOSE_LIBRARY(handle); return; }\
}

enum MIDI_states {
    NORMAL,
    PARAM,
    SYSEX,
};

static bool serial_dlab = false;
static uint8_t serial_dll, serial_dlm, serial_spr;

#ifndef __EMSCRIPTEN__

static uint8_t sysex_buffer[1024];
static int sysex_bufptr;

static enum MIDI_states midi_state = NORMAL;
static uint8_t midi_last_command = 0;
static uint8_t midi_first_param;

static bool midi_initialized = false;

static fluid_settings_t* fl_settings;
//static fluid_midi_driver_t* fl_mdriver;
static fluid_audio_driver_t* fl_adriver;
static fluid_synth_t* fl_synth;
static int fl_sf2id;

void midi_init()
{
    fl_settings = new_fluid_settings();
    fl_synth = new_fluid_synth(fl_settings);
    fl_adriver = new_fluid_audio_driver(fl_settings, fl_synth);

    midi_initialized = true;
    printf("Initialized MIDI synth.\n");
}

void midi_load_sf2(uint8_t* filename)
{
    if (!midi_initialized) return;
    fl_sf2id = fluid_synth_sfload(fl_synth, (const char *)filename, true);
    if (fl_sf2id == FLUID_FAILED) {
        printf("Unable to load soundfont.\n");
    }
}

// Receive a byte from client
// Store state, or dispatch event
void midi_byte(uint8_t b)
{
    if (!midi_initialized) return;

    switch (midi_state) {
        case NORMAL:
            if (b < 0x80) {
                if ((midi_last_command & 0xf0) == 0xc0) { // patch change
                    fluid_synth_program_change(fl_synth, midi_last_command & 0xf, b);
                } else if ((midi_last_command & 0xf0) == 0xd0) { // channel pressure
                    fluid_synth_channel_pressure(fl_synth, midi_last_command & 0xf, b);
                } else if (midi_last_command >= 0x80) { // two-param command
                    midi_first_param = b;
                    midi_state = PARAM;
                }
            } else {
                if (b < 0xf0) {
                    midi_last_command = b;
                } else if (b == 0xf0) {
                    sysex_bufptr = 0;
                    midi_state = SYSEX;
                } else if (b == 0xff) {
                    fluid_synth_system_reset(fl_synth);
                    midi_last_command = 0;
                }
            }
            break;
        case PARAM:
            switch (midi_last_command & 0xf0) {
                case 0x80: // note off
                    fluid_synth_noteoff(fl_synth, midi_last_command & 0xf, midi_first_param); // no release velocity
                    break;
                case 0x90: // note on
                    if (b == 0) {
                        fluid_synth_noteoff(fl_synth, midi_last_command & 0xf, midi_first_param);
                    } else {
                        fluid_synth_noteon(fl_synth, midi_last_command & 0xf, midi_first_param, b);
                    }
                    break;
                case 0xa0: // aftertouch
                    fluid_synth_key_pressure(fl_synth, midi_last_command & 0xf, midi_first_param, b);
                    break;
                case 0xb0: // controller
                    fluid_synth_cc(fl_synth, midi_last_command & 0xf, midi_first_param, b);
                    break;
                case 0xe0: // pitch bend
                    fluid_synth_pitch_bend(fl_synth, midi_last_command & 0xf, ((uint16_t)midi_first_param) | (uint16_t)b << 7);
                    break;
            }
            midi_state = NORMAL;
            break;
        case SYSEX:
            if (b == 0xf7) {
                sysex_buffer[sysex_bufptr] = 0;
                fluid_synth_sysex(fl_synth, (const char *)sysex_buffer, sysex_bufptr, NULL, NULL, NULL, 0);
                midi_state = NORMAL;
            } else {
                sysex_buffer[sysex_bufptr++] = b;
            }
            break;
    }
}

#else
void midi_load_sf2(uint8_t* filename)
{
    // no-op
}

void midi_init()
{
    printf("No fluidsynth support on WebAssembly.\n");
}

void midi_byte(uint8_t b) {
    // no-op
}

#endif


uint8_t midi_serial_read(uint8_t reg, bool debugOn) {
    //printf("midi_serial_read %d\n", reg);
    switch (reg) {
        case 0x0:
            if (serial_dlab) {
                return serial_dll;
            } else {
                // TODO: RHR
            }
            break;
        case 0x1:
            if (serial_dlab) {
                return serial_dlm;
            }
            return 0x00;
            // TODO: IER
            break;
        case 0x2:
            return 0x00;
            // TODO: ISR
            break;
        case 0x3:
            return 0x03 | ((int)serial_dlab << 7);
            // TODO: rest of LCR
            break;
        case 0x4:
            return 0x03;
            // TODO: MCR
            break;
        case 0x5:
            return 0x20;
            // TODO: LSR
            break;
        case 0x6:
            return 0x00;
            // TODO: MSR
            break;
        case 0x7:
            return serial_spr;
            break;
    }
    return 0x00;
}

void midi_serial_write(uint8_t reg, uint8_t val) {
    //printf("midi_serial_write %d %d\n", reg, val);
    switch (reg) {
        case 0x0:
            if (serial_dlab) {
                serial_dll = val;
            } else {
                midi_byte((uint8_t)val);
            }
            break;
        case 0x1:
            if (serial_dlab) {
                serial_dlm = val;
            }
            // TODO: IER
            break;
        case 0x2:
            // TODO: FCR
            break;
        case 0x3:
            serial_dlab = (bool)(val >> 7);
            // TODO: rest of LCR
            break;
        case 0x4:
            // TODO: MCR
            break;
        case 0x5:
            if (serial_dlab) {
                // TODO: PSD
            }
            break;
        case 0x7:
            serial_spr = val;
            break;
    }
}

