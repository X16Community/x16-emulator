// Commander X16 Emulator -- Accessibility (screen reader / SAPI) speech output
//
// Pipes the X16's text output to the active Windows screen reader (NVDA, JAWS,
// System Access, SuperNova, Window-Eyes, ZoomText) via the Tolk library.
//
// Speech turns on automatically when a screen reader is detected at startup;
// otherwise the emulator runs as a normal, silent emulator (there is no SAPI
// fallback, so a sighted user's session is unaffected).
//
// Output speech is line-buffered: characters sent to KERNAL CHROUT ($FFD2) are
// collected and spoken when output goes briefly quiet. Typed keys are echoed
// immediately ("key echo").

#ifndef _SPEECH_H_
#define _SPEECH_H_

#include <stdbool.h>
#include <stdint.h>

extern bool speech_enabled;    // set by speech_init() when a screen reader is found
extern bool speech_interrupt;  // true: new output interrupts current speech; false: queue (-speech-interrupt)
extern bool speech_force_off;  // set by -nospeech to skip detection entirely

// Detect a screen reader and, if one is running, turn on speech. If none is
// running (or Tolk.dll is absent, or -nospeech was given), the emulator stays
// silent and this returns false. Sets speech_enabled accordingly.
bool speech_init(void);

// Tear down the speech backend.
void speech_shutdown(void);

// Feed one character coming out of KERNAL CHROUT ($FFD2). Buffers printable
// characters and speaks the accumulated line on carriage return.
void speech_output_char(uint8_t c);

// Speak any pending buffered output immediately as a single utterance.
void speech_flush(void);

// Call frequently from the emulator loop. Speaks accumulated output once the
// X16 has stopped printing for a short idle period, so a burst of lines becomes
// one utterance instead of many that interrupt each other.
void speech_tick(void);

// Echo a typed key. `key_name` is a human-readable UTF-8 name such as the result
// of SDL_GetKeyName() ("A", "Return", "Backspace", "Space", ...).
void speech_key_echo(const char *key_name);

// Speak a single screen character (used for arrow-key navigation and Backspace).
// ' ' is spoken as "space"; 0 (blank/off-screen) is silent.
void speech_say_char(char c);

// --- Screen history review --------------------------------------------------
// The emulator records every line the X16 prints into a scrollback buffer.
// These let the user move through it and have each line spoken. Bound to
// Ctrl+Up / Ctrl+Down / Ctrl+Home / Ctrl+End / Ctrl+Shift+Up in video.c.
void speech_history_prev(void);   // move to and speak the previous (older) line
void speech_history_next(void);   // move to and speak the next (newer) line
void speech_history_oldest(void); // jump to and speak the oldest line
void speech_history_newest(void); // jump to and speak the most recent line
void speech_history_repeat(void); // re-speak the current line

// Speak an arbitrary UTF-8 string immediately (used internally and for
// announcements). `interrupt` cancels current speech first.
void speech_say(const char *utf8, bool interrupt);

#endif // _SPEECH_H_
