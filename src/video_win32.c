// All rights reserved. License: 2-clause BSD

#include <SDL.h>
#include <SDL_syswm.h>

#include <windows.h>
#include <dwmapi.h>

void video_win32_set_rounded_corners(SDL_Window *window)
{
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);

	HWND hwnd = wmInfo.info.win.window;
	DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUNDSMALL;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}
