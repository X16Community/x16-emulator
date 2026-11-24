// Commander X16 Emulator -- Accessibility (screen reader / SAPI) speech output
// See speech.h for an overview.
//
// The Tolk library is loaded dynamically at runtime so the emulator builds and
// runs even when Tolk.dll is absent (speech simply stays unavailable). Tolk
// auto-detects the running screen reader and, with Tolk_TrySAPI enabled, falls
// back to Microsoft SAPI 5 when none is present.

#include "speech.h"

#include <stdio.h>
#include <string.h>

#include <SDL.h> // SDL_GetTicks() for idle-based flushing

bool speech_enabled = false;
bool speech_interrupt = false;
bool speech_force_off = false;

#ifdef _WIN32

#include <windows.h>

// --- Dynamically resolved Tolk entry points ------------------------------------
typedef void (__cdecl *tolk_load_t)(void);
typedef void (__cdecl *tolk_unload_t)(void);
typedef const wchar_t *(__cdecl *tolk_detect_t)(void);
typedef bool (__cdecl *tolk_output_t)(const wchar_t *, bool);

static HMODULE        tolk_dll      = NULL;
static tolk_load_t    p_Tolk_Load   = NULL;
static tolk_unload_t  p_Tolk_Unload = NULL;
static tolk_detect_t  p_Tolk_Detect = NULL;
static tolk_output_t  p_Tolk_Output = NULL;

static bool speech_ready = false;

// The X16 emits screen text as a fast burst of CHROUT characters: a line, a
// carriage return, the next line, and so on. Speaking each line as its own
// utterance makes them interrupt one another (e.g. "HI" then "READY." would
// clobber each other). Instead we collect completed lines into `pending` and,
// once output has been quiet for IDLE_MS, speak the whole block at once.
#define LINE_MAX    256   // one logical line being assembled from CHROUT
#define PENDING_MAX 4096  // accumulated lines awaiting a flush
#define IDLE_MS     150   // quiet period after which pending speech is spoken

static char     line_buf[LINE_MAX];
static int      line_len = 0;
static char     pending[PENDING_MAX];
static int      pending_len = 0;
static uint32_t last_activity_ms = 0; // SDL_GetTicks() of the last char seen
static bool     have_pending = false;

// Scrollback of completed lines for history review (Ctrl+arrows). Stored as a
// ring buffer so it never grows unbounded.
#define HIST_MAX 2000
static char hist[HIST_MAX][LINE_MAX];
static int  hist_count = 0;  // number of valid entries (<= HIST_MAX)
static int  hist_start = 0;  // ring index of the oldest entry
static int  review_pos = -1; // logical index being reviewed; -1 = live (not reviewing)

// Append a completed line to the scrollback ring.
static void
history_record(const char *line)
{
	int slot = (hist_start + hist_count) % HIST_MAX;
	if (hist_count < HIST_MAX) {
		hist_count++;
	} else {
		hist_start = (hist_start + 1) % HIST_MAX; // overwrite oldest
	}
	strncpy(hist[slot], line, LINE_MAX - 1);
	hist[slot][LINE_MAX - 1] = '\0';
	review_pos = -1; // fresh output returns us to "live"
}

// Speak the scrollback line at the current review position.
static void
speak_review_line(void)
{
	if (review_pos < 0 || review_pos >= hist_count) {
		return;
	}
	int slot = (hist_start + review_pos) % HIST_MAX;
	speech_say(hist[slot], true);
}

// Move the current line into the pending block (skipping blank lines), and
// record that output is still arriving so the idle timer keeps resetting.
static void
end_line(void)
{
	line_buf[line_len] = '\0';
	int len = line_len;
	line_len = 0;

	const char *p = line_buf;
	while (*p == ' ') p++;
	if (!*p) {
		return; // whitespace-only line: nothing worth speaking
	}

	history_record(line_buf); // add to scrollback for review

	if (pending_len + len + 2 >= PENDING_MAX) {
		speech_flush(); // pending block is full; speak it now and start over
	}
	if (pending_len > 0 && pending_len < PENDING_MAX - 1) {
		pending[pending_len++] = '\n'; // line break -> screen reader pause
	}
	int n = (int)strlen(line_buf);
	if (pending_len + n < PENDING_MAX) {
		memcpy(pending + pending_len, line_buf, (size_t)n);
		pending_len += n;
	}
	pending[pending_len] = '\0';
	have_pending = true;
}

// Convert a UTF-8 string to a freshly allocated wide string. Caller frees.
static wchar_t *
utf8_to_wide(const char *utf8)
{
	int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (needed <= 0) {
		return NULL;
	}
	wchar_t *w = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
	if (!w) {
		return NULL;
	}
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, needed);
	return w;
}

bool
speech_init(void)
{
	if (speech_ready) {
		return true;
	}
	if (speech_force_off) {
		speech_enabled = false;
		return false; // user passed -nospeech
	}

	// Load Tolk if it is present. If it is missing, this is simply a normal
	// (silent) emulator -- e.g. a sighted user's build without Tolk.dll.
	tolk_dll = LoadLibraryW(L"Tolk.dll");
	if (!tolk_dll) {
		speech_enabled = false;
		return false;
	}

	p_Tolk_Load   = (tolk_load_t)(void *)GetProcAddress(tolk_dll, "Tolk_Load");
	p_Tolk_Unload = (tolk_unload_t)(void *)GetProcAddress(tolk_dll, "Tolk_Unload");
	p_Tolk_Detect = (tolk_detect_t)(void *)GetProcAddress(tolk_dll, "Tolk_DetectScreenReader");
	p_Tolk_Output = (tolk_output_t)(void *)GetProcAddress(tolk_dll, "Tolk_Output");

	if (!p_Tolk_Load || !p_Tolk_Output || !p_Tolk_Detect) {
		FreeLibrary(tolk_dll);
		tolk_dll = NULL;
		speech_enabled = false;
		return false;
	}

	// Initialize Tolk WITHOUT SAPI: we only want to talk if a real screen reader
	// is running. (Tolk excludes SAPI from auto-detection by default.)
	p_Tolk_Load();

	const wchar_t *name = p_Tolk_Detect();
	if (!name) {
		// No screen reader active -> run as a normal, silent emulator.
		p_Tolk_Unload();
		FreeLibrary(tolk_dll);
		tolk_dll = NULL;
		speech_enabled = false;
		return false;
	}

	// A screen reader is present: turn on screen-reader mode.
	speech_ready   = true;
	speech_enabled = true;

	char nbuf[128];
	WideCharToMultiByte(CP_UTF8, 0, name, -1, nbuf, sizeof(nbuf), NULL, NULL);
	printf("[speech] Screen reader detected (%s); screen reader mode on.\n", nbuf);
	speech_say("Commander X16 screen reader mode.", true);
	return true;
}

void
speech_shutdown(void)
{
	if (!speech_ready) {
		return;
	}
	speech_flush();
	if (p_Tolk_Unload) {
		p_Tolk_Unload();
	}
	if (tolk_dll) {
		FreeLibrary(tolk_dll);
		tolk_dll = NULL;
	}
	speech_ready = false;
}

void
speech_say(const char *utf8, bool interrupt)
{
	if (!speech_ready || !p_Tolk_Output || !utf8 || !*utf8) {
		return;
	}
	if (getenv("SPEECH_DEBUG")) {
		FILE *dbg = fopen("speech_debug.log", "a");
		if (dbg) {
			fprintf(dbg, "[speak%s] |%s|\n", interrupt ? " int" : "", utf8);
			fclose(dbg);
		}
	}
	wchar_t *w = utf8_to_wide(utf8);
	if (!w) {
		return;
	}
	p_Tolk_Output(w, interrupt);
	free(w);
}

void
speech_flush(void)
{
	// Finish any in-progress line, then speak the accumulated block as one
	// utterance so consecutive lines don't interrupt each other.
	if (line_len > 0) {
		end_line();
	}
	if (pending_len > 0) {
		speech_say(pending, speech_interrupt);
	}
	pending_len = 0;
	pending[0] = '\0';
	have_pending = false;
}

void
speech_output_char(uint8_t c)
{
	if (!speech_ready) {
		return;
	}
	last_activity_ms = SDL_GetTicks();
	switch (c) {
		case 0x0d: // CR -- end of the current logical line
			end_line();
			break;
		case 0x0a: // LF -- ignore (X16 uses CR)
			break;
		case 0x14: // PETSCII DEL/backspace -- erase last buffered char
			if (line_len > 0) {
				line_len--;
			}
			break;
		default:
			// Buffer the printable ASCII subset; ignore control and high codes.
			if (c >= 0x20 && c < 0x7f) {
				if (line_len >= LINE_MAX - 1) {
					end_line(); // overlong line: wrap it into pending
				}
				line_buf[line_len++] = (char)c;
			}
			break;
	}
}

void
speech_tick(void)
{
	// Called frequently from the emulator loop. Once output has been quiet for
	// IDLE_MS, speak whatever has accumulated.
	if (!speech_ready || !have_pending) {
		return;
	}
	if (SDL_GetTicks() - last_activity_ms >= IDLE_MS) {
		speech_flush();
	}
}

void
speech_key_echo(const char *key_name)
{
	if (!speech_ready || !key_name || !*key_name) {
		return;
	}
	// Key echo feels immediate, so it always interrupts. We deliberately do not
	// flush the output buffer here: the line currently in line_buf may be the
	// command the user is typing (the editor echoes keystrokes through CHROUT),
	// and speaking it on every keystroke would double up with the echo. Buffered
	// program output is delivered by the idle timer in speech_tick() instead.
	speech_say(key_name, true);
}

void
speech_say_char(char c)
{
	if (!speech_ready) {
		return;
	}
	if (c == 0) {
		return; // blank / off the edge of the screen: stay silent
	}
	char buf[8];
	if (c == ' ') {
		strcpy(buf, "space");
	} else {
		buf[0] = c;
		buf[1] = '\0';
	}
	speech_say(buf, true);
}

void
speech_history_prev(void)
{
	if (!speech_ready) {
		return;
	}
	if (hist_count == 0) {
		speech_say("No history.", true);
		return;
	}
	if (review_pos < 0) {
		review_pos = hist_count - 1; // from live, start at the most recent line
	} else if (review_pos > 0) {
		review_pos--;
	} else {
		speech_say("Top.", true); // already at the oldest line
		return;
	}
	speak_review_line();
}

void
speech_history_next(void)
{
	if (!speech_ready) {
		return;
	}
	if (hist_count == 0 || review_pos < 0) {
		speech_say("Live.", true); // not reviewing / nothing newer
		return;
	}
	if (review_pos < hist_count - 1) {
		review_pos++;
		speak_review_line();
	} else {
		review_pos = -1;
		speech_say("Live.", true); // moved past the newest line, back to live
	}
}

void
speech_history_oldest(void)
{
	if (!speech_ready || hist_count == 0) {
		return;
	}
	review_pos = 0;
	speech_say("Top.", true);
	speak_review_line();
}

void
speech_history_newest(void)
{
	if (!speech_ready || hist_count == 0) {
		return;
	}
	review_pos = hist_count - 1;
	speak_review_line();
}

void
speech_history_repeat(void)
{
	if (!speech_ready || hist_count == 0) {
		return;
	}
	if (review_pos < 0) {
		review_pos = hist_count - 1; // repeat the most recent line
	}
	speak_review_line();
}

#else // !_WIN32 -- speech is a Windows-only feature for now.

bool speech_init(void) { speech_enabled = false; return false; }
void speech_shutdown(void) {}
void speech_output_char(uint8_t c) { (void)c; }
void speech_flush(void) {}
void speech_tick(void) {}
void speech_key_echo(const char *key_name) { (void)key_name; }
void speech_say(const char *utf8, bool interrupt) { (void)utf8; (void)interrupt; }
void speech_say_char(char c) { (void)c; }
void speech_history_prev(void) {}
void speech_history_next(void) {}
void speech_history_oldest(void) {}
void speech_history_newest(void) {}
void speech_history_repeat(void) {}

#endif // _WIN32
