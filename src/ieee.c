// Commander X16 Emulator
// Copyright (c) 2022, 2023 Michael Steil, et al
// All rights reserved. License: 2-clause BSD

// Commodore Bus emulation
// * L2: TALK/LISTEN layer: https://www.pagetable.com/?p=1031
// * L3: Commodore DOS: https://www.pagetable.com/?p=1038
// This is used from
// * serial.c: L1: Serial Bus emulation (low level)
// * main.c: IEEE KERNAL call hooks (high level)

#ifndef __APPLE__
#define _XOPEN_SOURCE   600
#define _POSIX_C_SOURCE 1
#endif
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <SDL.h>
#include <errno.h>
#include <time.h>
#include "memory.h"
#include "ieee.h"
#include "glue.h"
#include "utf8_encode.h"
#include "utf8.h"
#include "iso_8859_15.h"
#ifdef __MINGW32__
#include <direct.h>
// Windows just has to be different
#define localtime_r(S,D) !localtime_s(D,S)
#include <io.h>
#define F_OK 0
#define access _access
#endif

extern SDL_RWops *prg_file;

#define UNIT_NO 8

#define WILDCARD_ALL 0
#define WILDCARD_PRG 1
#define WILDCARD_DIR 2

// Globals

//bool log_ieee = true;
bool log_ieee = false;

bool ieee_initialized_once = false;

uint8_t error[80];
int error_len = 0;
int error_pos = 0;
uint8_t cmd[80];
int cmdlen = 0;
int namelen = 0;
int channel = 0;
bool listening = false;
bool talking = false;
bool opening = false;
bool overwrite = false;
bool path_exists = false;
bool prg_consumed = false;

uint8_t *hostfscwd = NULL;

uint8_t dirlist[1024]; // Plenty large to hold a single entry
int dirlist_len = 0;
int dirlist_pos = 0;
bool dirlist_cwd = false; // whether we're doing a cwd dirlist or a normal one
bool dirlist_eof = true;
bool dirlist_timestmaps = false;
bool dirlist_long = false;
DIR *dirlist_dirp;
uint8_t dirlist_wildcard[256];
uint8_t dirlist_type_filter;

uint16_t cbdos_flags = 0;

const char *blocks_free = "BLOCKS FREE.";

typedef struct {
	uint8_t name[80];
	bool read;
	bool write;
	SDL_RWops *f;
} channel_t;

channel_t channels[16];

#ifdef __MINGW32__
// realpath doesn't exist on Windows. This function implements its behavior.
static char *
realpath(const char *path, char *resolved_path) {
	char *ret = _fullpath(resolved_path, path, PATH_MAX);

	if (ret && _access(ret,0) && errno == ENOENT) {
		if (resolved_path == NULL)
			free(ret);
		return NULL;
	}

	return ret;
}
#endif

#define u8strchr(A,B) (uint8_t *)strchr((char *)A,B)
#define u8strrchr(A,B) (uint8_t *)strrchr((char *)A,B)
#define u8strcpy(A,B) strcpy((char *)A,(char *)B)
#define u8strcmp(A,B) strcmp((char *)A,(char *)B)
#define u8strncmp(A,B,C) strncmp((char *)A,(char *)B,C)
#define u8strncpy(A,B,C) strncpy((char *)A,(char *)B,C)
#define u8strlen(A) strlen((char *)A)
#define u8stat(A,B) stat((char *)A,B)
#define u8realpath(A,B) (uint8_t *)realpath((char *)A,(char *)B)
#define u8getcwd(A,B) (uint8_t *)getcwd((char *)A,B)

// Prototypes for some of the static functions

static void clear_error();
static void set_error(int e, int t, int s);
static void cchdir(uint8_t *dir);
static int cgetcwd(uint8_t *buf, size_t len);
static void cseek(int channel, uint32_t pos);
static void cmkdir(uint8_t *dir);
static void crmdir(uint8_t *dir);
static void cunlink(uint8_t *f);
static void crename(uint8_t *f);

// Functions

static void
set_kernal_cbdos_flags(uint8_t flags)
{
	if (cbdos_flags)
		write6502(cbdos_flags, flags);
}

static uint8_t
get_kernal_cbdos_flags(void)
{
	if (cbdos_flags) {
		return read6502(cbdos_flags);
	} else {
		return 0;
	}
}


static void
utf8_to_iso_string(uint8_t *dst, const uint8_t *src)
{
	int i;
	int e;
	uint32_t cp;

	// utf8_decode requires the source string be at least 4 bytes
	// longer than the string (IOW, 4 terminating nulls)
	uint8_t *s = malloc(u8strlen(src)+4);
	memset(s, 0, u8strlen(src)+4);
	u8strcpy(s, src);

	uint8_t *so = s;

	for (i = 0; *so; i++) {
		so = utf8_decode(so, &cp, &e);
		if (e) {
			dst[i] = '?';
		} else {
			switch (cp) {
				case ',':
				case '"':
				case '*':
				case ':':
				case '\\':
				case '<':
				case '>':
				case '|':
					// these are not valid in filenames on FAT32 but might be encountered on hostfs
					dst[i] = '?';
					break;
				default:
					dst[i] = iso8859_15_from_unicode(cp);
					break;
			}
		}
	}

	dst[i] = 0;
	free(s);
}

// Puts the emulated cwd in buf, up to the maximum length specified by len
// Turn null termination into a space
// This is for displaying in the directory header
static int
cgetcwd(uint8_t *buf, size_t len)
{
	int o = 0;
	uint8_t l = fsroot_path[u8strlen(fsroot_path)-1];
	if (l == '/' || l == '\\')
		o--;
	if (u8strlen(fsroot_path) == u8strlen(hostfscwd))
		u8strncpy(buf, "/", len);
	else 
		u8strncpy(buf, hostfscwd+u8strlen(fsroot_path)+o, len);
	// Turn backslashes into slashes
	for (o = 0; o < len; o++) {
		if (buf[o] == 0) buf[o] = ' ';
		if (buf[o] == '\\') buf[o] = '/';
	}

	return 0;
}

static uint32_t
case_fold_unicode(uint32_t cp)
{
	// ASCII letters, and most of the ISO letters
	if ((cp >= 0x41 && cp <= 0x5a) || (cp >= 0xc0 && cp <= 0xde)) cp += 0x20;
	// fold the Š
	else if (cp == 0x0160) cp = 0x0161;
	// fold the Ž
	else if (cp == 0x017d) cp = 0x017e;
	// fold the Œ
	else if (cp == 0x0152) cp = 0x0153;
	// fold the Ÿ
	else if (cp == 0x0178) cp = 0xff;

	return cp;
}

static uint8_t
case_fold_iso(uint8_t c) {
	uint8_t f = c;
	// ASCII letters, and most of the ISO letters
	if ((f >= 0x41 && f <= 0x5a) || (f >= 0xc0 && f <= 0xde)) f += 0x20;
	// fold the Š
	else if (f == 0xa6) f = 0xa8;
	// fold the Ž
	else if (f == 0xb4) f = 0xb8;
	// fold the Œ
	else if (f == 0xbc) f = 0xbd;
	// fold the Ÿ
	else if (f == 0xbe) f = 0xff;

	return f;
}


static uint32_t
utf8_to_codepoint(uint8_t *str, int *off)
{
	uint32_t cp;
	int e;

	// utf8_decode requires the source string be at least 4 bytes
	// longer than the string (IOW, 4 terminating nulls)
	uint8_t *s = malloc(u8strlen(str+(*off))+4);
	memset(s, 0, u8strlen(str+(*off))+4);
	u8strcpy(s, str+(*off));
	
	uint8_t *so = utf8_decode(s, &cp, &e);

	*off += (so - s) - 1;
	free(s);

	if (e) {
		cp = '?';
	}

	return cp;
}


// compare two characters in a UTF-8 string
// in a case-insensitive manner,
// return value is cp2 - cp1 after case folding
// hence will be zero on match
static int
u8compare_utf8_char_i(uint8_t *str1, uint8_t *str2, int *off1, int *off2)
{
	uint32_t cp1, cp2;

	// these functions will increment *off1/*off2 to the
	// byte before the next character if the UTF-8
	// strings are multi-byte
	cp1 = utf8_to_codepoint(str1, off1);
	cp2 = utf8_to_codepoint(str2, off2);

	return case_fold_unicode(cp2) - case_fold_unicode(cp1);
}


static uint8_t *
parse_dos_filename(const uint8_t *name, bool dirhandling)
{
	// in case the name starts with something with special meaning,
	// such as @0:
	uint8_t *name_ptr = NULL;
	uint8_t *newname = malloc(u8strlen(name)+1);
	int i, j;

	newname[u8strlen(name)] = 0;
	
	overwrite = false;

	// [[@][<media 0-9>][</relative_path/> | <//absolute_path/>]:]<file_path>[*]
	// Examples of valid dos filenames
	//   ":FILE.PRG"  (same as "FILE.PRG")
	//   "@:FILE.PRG"  (same as "FILE.PRG" but overwrite okay)
	//   "@0:FILE.PRG"  (same as above)
	//   "//DIR/:FILE.PRG"  (same as "/DIR/FILE.PRG")
	//   "/DIR/:FILE.PRG"  (same as "./DIR/FILE.PRG")
	//   "FILE*" (matches the first file in the directory which starts with "FILE")

	// This routine only parses the bits before the ':'
	// and normalizes directory parts by attaching them to the name part

	// resolve_path*() is responsible for resolving absolute and relative
	// paths, and for processing the wildcard option

	name_ptr = u8strchr(name,':');
	if (dirhandling && !name_ptr)
		name_ptr = (uint8_t *)(name+u8strlen(name));

	if (name_ptr) {
		name_ptr++;
		i = 0;
		j = 0;


		if (!dirhandling) {
			// @ is overwrite flag
			if (name[i] == '@') {
				overwrite = true;
				i++;
			}

			// Medium, we don't care what this number is
			while (name[i] >= '0' && name[i] <= '9')
				i++;
		}

		// Directory name
		if (name[i] == '/') {
			i++;
			for (; name+i+1 < name_ptr; i++, j++) {
				newname[j] = name[i];
			}

			// mangled directory name (single slash?)
			if (j == 0) {
				free(newname);
				return NULL;
			}

			// Directory portion must end with /
			if (newname[j-1] != '/') {
				free(newname);
				return NULL;
			}
		}

		u8strcpy(newname+j,name_ptr);

	} else {
		u8strcpy(newname,name);
	}

	return newname;
}

// Returns a ptr to malloc()ed space which must be free()d later, or NULL
static uint8_t *
resolve_path_utf8(const uint8_t *name, bool must_exist, int wildcard_filetype)
{
	path_exists = false;
	clear_error();
	// Resolve the filename in the context of the emulated cwd
	// allocate plenty of string space
	uint8_t *tmp = malloc(u8strlen(name)+u8strlen(hostfscwd)+2);
	uint8_t *tmp2 = malloc(u8strlen(name)+u8strlen(hostfscwd)+2);
	uint8_t *c;
	uint8_t *d;
	uint8_t *ret;
	DIR *dirp;
	struct dirent *dp;
	struct stat st;
	int i;
	int j;
	bool has_wildcard_chars = false;

	if (tmp == NULL || tmp2 == NULL) {
		if (tmp) free(tmp);
		if (tmp2) free(tmp2);
		set_error(0x70, 0, 0);
		return NULL;
	}

	// If the filename begins with /, simply append it to the fsroot_path,
	// slash(es) and all, otherwise append it to the cwd, but with a /
	// in between
	if (name[0] == '/' || name[0] == '\\') { // absolute
		u8strcpy(tmp, fsroot_path);
		u8strcpy(tmp+u8strlen(fsroot_path), name);
	} else { // relative
		u8strcpy(tmp, hostfscwd);
		tmp[u8strlen(hostfscwd)] = '/';
		u8strcpy(tmp+u8strlen(hostfscwd)+1, name);
	}

	// keep the original parsed name
	u8strcpy(tmp2, tmp);

	if (u8strchr(tmp,'*') || u8strchr(tmp,'?')) { // oh goodie, a wildcard
		has_wildcard_chars = true;
	}
	// We have to search the directory for the first occurrence
	// in directory order.

	c = u8strrchr(tmp,'\\');
	d = u8strrchr(tmp,'/');

	if (c == NULL && d == NULL) { // This should never happen
		free(tmp);
		free(tmp2);
		set_error(0x62, 0, 0);
		return NULL;
	}

	// Chop off ret at the last path separator
	// and set c to point at the element with the wildcard
	// pattern
	if (c > d) {
		*c = 0;
		c++;
	} else {
		*d = 0;
		c = d+1;
	}

	// XXX
	//
	// tmp could have been populated with path parts that have not been checked
	// this will fail on case-sensitive filesystems
	// but if the user did not specify a path part in their filename
	// we should be able to resolve the new part of the name
	// we'll probably want to fix this at some point
	// but it requires some thought

	if (!(dirp = opendir((char *)tmp))) { // Directory couldn't be opened
		free(tmp);
		free(tmp2);
		set_error(0x62, 0, 0);
		return NULL;
	}

	ret = NULL;

	bool found = false;

	while ((dp = readdir(dirp))) {
		// in a wildcard match that starts at first position, leading dot filenames are not considered
		if ((*c == '*' || *c == '?') && *(dp->d_name) == '.')
			continue;
		for (i = 0, j = 0; j < u8strlen(c) && i < u8strlen(dp->d_name); i++, j++) {
			if (c[j] == '*') {
				found = true;
				break;
			} else if (c[j] == '?') {
				// '?' needs to eat the extra UTF-8 bytes if applicable
				if (((dp->d_name)[i] & 0xe0) == 0xc0) i++;
				else if (((dp->d_name)[i] & 0xf0) == 0xe0) i+=2;
				else if (((dp->d_name)[i] & 0xf8) == 0xf0) i+=3;
				continue;
			} else if (u8compare_utf8_char_i(c, (uint8_t *)(dp->d_name), &j, &i)) {
				break;
			}
		}

		// If we reach the end of both strings, it's a match
		if (i == u8strlen(dp->d_name) && j == u8strlen(c))
			found = true;

		// If we reach the end of the filename, but the next char
		// in the search string is *, then it's also a match
		else if (i == u8strlen(dp->d_name) && c[j] == '*')
			found = true;

		if (found) { // simple wildcard match
			ret = malloc(u8strlen(tmp)+u8strlen(dp->d_name)+2);
			if (ret == NULL) { // memory allocation error
				free(tmp);
				free(tmp2);
				closedir(dirp);
				set_error(0x70, 0, 0);
				return NULL;
			}
			u8strcpy(ret, tmp);
			ret[u8strlen(tmp)] = '/';
			u8strcpy(ret+u8strlen(tmp)+1, dp->d_name);
			if (wildcard_filetype) {
				u8stat(ret, &st);
				// in a wildcard match where the filetype is wrong, mark as not found
				// and continue
				if (wildcard_filetype == WILDCARD_DIR && !S_ISDIR(st.st_mode)) {
					free(ret);
					ret = NULL;
					found = false;
					continue;
				} else if (wildcard_filetype == WILDCARD_PRG && !S_ISREG(st.st_mode)) {
					free(ret);
					ret = NULL;
					found = false;
					continue;
				}
			}
			break;
		}
	}
	closedir(dirp);

	free(tmp);

	if (ret) { // We had a match
		free(tmp2);
		tmp = ret;
	} else if (has_wildcard_chars) {
		// our original query had * or ? and we didn't find it
		free(tmp2);
		set_error(0x62, 0, 0);
		return NULL;
	} else { // reset tmp to the original name
		tmp = tmp2;
	}

	// now resolve the path using OS routines
	ret = u8realpath(tmp, NULL);
	free(tmp);

	if (ret == NULL) {
		if (must_exist) {
			// path wasn't found or had another error in construction
			set_error(0x62, 0, 0);
		} else {
			// path does not exist, but as long as everything but the final
			// path element exists, we're still okay.
			tmp = malloc(u8strlen(name)+1);
			if (tmp == NULL) {
				set_error(0x70, 0, 0);
				return NULL;
			}
			u8strcpy(tmp, name);
			c = u8strrchr(tmp, '/');
			if (c == NULL)
				c = u8strrchr(tmp, '\\');
			if (c != NULL)
				*c = 0; // truncate string here

			// assemble a path with what we have left
			ret = malloc(u8strlen(tmp)+u8strlen(hostfscwd)+2);
			if (ret == NULL) {
				free(tmp);
				set_error(0x70, 0, 0);
				return NULL;
			}

			if (name[0] == '/' || name[0] == '\\') { // absolute
				u8strcpy(ret, fsroot_path);
				u8strcpy(ret+u8strlen(fsroot_path), tmp);
			} else { // relative
				u8strcpy(ret, hostfscwd);
				*(ret+u8strlen(hostfscwd)) = '/';
				u8strcpy(ret+u8strlen(hostfscwd)+1, tmp);
			}

			free(tmp);

			// if we found a path separator in the name string
			// we check everything up to that final separator
			if (c != NULL) {
				tmp = u8realpath(ret, NULL);
				free(ret);
				if (tmp == NULL) {
					// missing parent path element too
					set_error(0x62, 0, 0);
					ret = NULL;
				} else {
					free(tmp);
					// found everything up to the parent path element
					// restore ret to original case
					ret = malloc(u8strlen(name)+u8strlen(hostfscwd)+2);
					if (ret == NULL) {
						set_error(0x70, 0, 0);
						return NULL;
					}
					u8strcpy(ret, hostfscwd);
					ret[u8strlen(hostfscwd)] = '/';
					u8strcpy(ret+u8strlen(hostfscwd)+1, name);
				}
			}
		}
	} else {
		path_exists = true;
	}

	if (ret == NULL)
		return ret;


	// Prevent resolving outside the fsroot_path
	if (u8strlen(fsroot_path) > u8strlen(ret)) {
		free(ret);
		set_error(0x62, 0, 0);
		return NULL;
	} else if (u8strncmp(fsroot_path, ret, u8strlen(fsroot_path))) {
		free(ret);
		set_error(0x62, 0, 0);
		return NULL;
	} else if (u8strlen(fsroot_path) < u8strlen(ret) &&
	           fsroot_path[u8strlen(fsroot_path)-1] != '/' &&
			   fsroot_path[u8strlen(fsroot_path)-1] != '\\' &&
	           ret[u8strlen(fsroot_path)] != '/' &&
	           ret[u8strlen(fsroot_path)] != '\\')
	{
		// ret matches beginning of fsroot_path,
		// end of fsroot_path is not a path-separator,
		// and next char in ret is not a path-separator
		// This could happen if
		//   fsroot_path == "/home/user/ba" and
		//   ret == "/home/user/bah".
		// This condition should be considered a jailbreak and we fail out
		free(ret);
		set_error(0x62, 0, 0);
		return NULL;
	}

	return ret;
}


static uint8_t *
resolve_path_iso(const uint8_t *name, bool must_exist, int wildcard_filetype)
{
	uint8_t *ret;
	uint8_t *buf;
	int i, j;
	uint32_t cp;

	buf = malloc(u8strlen(name)*3+1);

	for (i = 0, j = 0; name[i]; i++) {
		cp = unicode_from_iso8859_15(name[i]);
		j += utf8_encode((char *)buf+j, cp);
	}

	ret = resolve_path_utf8(buf, must_exist, wildcard_filetype);
	free(buf);
	return ret;
}

static int
create_directory_listing(uint8_t *data, uint8_t *dirstring)
{
	uint8_t *data_start = data;

	dirlist_eof = true;
	dirlist_cwd = false;
	int i = 0;
	int j;

	
	dirlist_timestmaps = false;
	dirlist_long = false;
	dirlist_type_filter = 0;
	dirlist_wildcard[0] = 0;

	// Here's where we parse out directory listing options
	// Such as "$=T:MATCH*=P"

	while (i < u8strlen(dirstring)) {
		if (dirstring[i] == ':' || i == 0) {
			i++;
			j = 0;
			while (i < u8strlen(dirstring)) {
				if (i == 1 && dirstring[i] == ':')
					i++;
				if (dirstring[i] == '=' || dirstring[i] == 0) {
					if (dirstring[++i] == 'D')
						dirlist_type_filter = 'D';
					else if (dirstring[i] == 'P' && i != 2)
						dirlist_type_filter = 'P';
					else if (dirstring[i] == 'T' && i == 2)
						dirlist_timestmaps = true;
					else if (dirstring[i] == 'L' && i == 2) {
						dirlist_timestmaps = true;
						dirlist_long = true;
					}
					break;
				} else {
					dirlist_wildcard[j++] = dirstring[i++];
					dirlist_wildcard[j] = 0;
				}
			}
		}
		i++;
	}

	// load address
	*data++ = 1;
	*data++ = 8;
	// link
	*data++ = 1;
	*data++ = 1;
	// line number
	*data++ = 0;
	*data++ = 0;
	*data++ = 0x12; // REVERSE ON
	*data++ = '"';
	for (int i = 0; i < 16; i++) {
		*data++ = ' ';
	}
	if (cgetcwd(data - 16, 16)) {
		return false;
	}
	*data++ = '"';
	*data++ = ' ';
	*data++ = 'H';
	*data++ = 'O';
	*data++ = 'S';
	*data++ = 'T';
	*data++ = ' ';
	*data++ = 0;

	if (!(dirlist_dirp = opendir((char *)hostfscwd))) {
		return 0;
	}
	dirlist_eof = false;
	return data - data_start;
}

static int
continue_directory_listing(uint8_t *data)
{
	uint8_t *data_start = data;
	struct stat st;
	struct dirent *dp;
	size_t file_size;
	uint8_t *tmpnam;
	bool found;
	int i;

	while ((dp = readdir(dirlist_dirp))) {
		// Type match
		if (dirlist_type_filter) {
			// Because resolving the path within the hostfs
			// logic is expensive, and could keep us here for multiple seconds
			// when directories are very large, it is much faster and gets us
			// past the negative matches of the filter ASAP if we do a quick
			// stat().
			//
			// The later logic can validate if the path resolves to something
			// within the hostfs root or not.
			size_t nl = u8strlen(dp->d_name);
			size_t pl = u8strlen(hostfscwd);
			uint8_t *tn = malloc(nl+pl+3);
			u8strcpy(tn, hostfscwd);
			*(tn+pl) = '/';
			u8strcpy(tn+pl+1, dp->d_name);
			u8stat(tn, &st);
			free(tn);

			switch (dirlist_type_filter) {
				case 'D':
					if (!S_ISDIR(st.st_mode))
						continue;
					break;
				case 'P':
					if (!S_ISREG(st.st_mode))
						continue;
					break;
			}
		}

		size_t namlen = u8strlen(dp->d_name);
		tmpnam = resolve_path_utf8((uint8_t *)dp->d_name, true, WILDCARD_ALL);
		if (tmpnam == NULL) continue;
		u8stat(tmpnam, &st);
		free(tmpnam);

		// don't show the . or .. in the root directory
		// this behaves like SD card/FAT32
		if (!u8strcmp("..",dp->d_name) || !u8strcmp(".",dp->d_name)) {
			if (!u8strcmp(hostfscwd,fsroot_path)) {
				continue;
			}
		}

		tmpnam = malloc(namlen+1);
		utf8_to_iso_string(tmpnam, (uint8_t *)dp->d_name);

		if (dirlist_wildcard[0]) { // wildcard match selected
			// in a wildcard match that starts at first position, leading dot filenames are not considered
			if ((dirlist_wildcard[0] == '*' || dirlist_wildcard[0] == '?') && *(tmpnam) == '.')
				continue;

			found = false;
			for (i = 0; i < u8strlen(dirlist_wildcard) && i < u8strlen(tmpnam); i++) {
				if (dirlist_wildcard[i] == '*') {
					found = true;
					break;
				} else if (dirlist_wildcard[i] == '?') {
					continue;
				} else if (case_fold_iso(dirlist_wildcard[i]) != case_fold_iso((tmpnam)[i])) {
					break;
				}
			}

			// If we reach the end of both strings, it's a match
			if (i == u8strlen(tmpnam) && i == u8strlen(dirlist_wildcard))
				found = true;

			// If we reach the end of the filename and the following character in
			// the wildcard string is *, it's also a match
			else if (i == u8strlen(tmpnam) && dirlist_wildcard[i] == '*')
				found = true;

			if (!found) continue;
		}

		// link
		*data++ = 1;
		*data++ = 1;

		if (dirlist_long) {
			unsigned char unit = 'K';
			file_size = (st.st_size + 1023)/1024;
			if (file_size > 0xFFFF) {
				file_size /= 1024;
				unit = 'M';
			}
			if (file_size > 0xFFFF) {
				file_size /= 1024;
				unit = 'G';
			}
			if (file_size > 0xFFFF) {
				file_size = 0xFFFF;
			}

			*data++ = file_size & 0xFF;
			*data++ = file_size >> 8;
			*data++ = unit;
			*data++ = 'B';
		} else {
			file_size = (st.st_size + 255)/256;
			if (file_size > 0xFFFF) {
				file_size = 0xFFFF;
			}
			*data++ = file_size & 0xFF;
			*data++ = file_size >> 8;
		}

		if (file_size < 1000) {
			*data++ = ' ';
			if (file_size < 100) {
				*data++ = ' ';
				if (file_size < 10) {
					*data++ = ' ';
				}
			}
		}
		*data++ = '"';
		//if (namlen > 16) {
		//	namlen = 16; // TODO hack
		//}
		namlen = u8strlen(tmpnam);
		memcpy(data, tmpnam, namlen);
		data += namlen;
		*data++ = '"';
		for (int i = namlen; i < 16; i++) {
			*data++ = ' ';
		}
		*data++ = ' ';
		if (S_ISDIR(st.st_mode)) {
			*data++ = 'D';
			*data++ = 'I';
			*data++ = 'R';
		} else {
			*data++ = 'P';
			*data++ = 'R';
			*data++ = 'G';
		}
		// This would be a '<' if file were protected, but it's a space instead
		*data++ = ' ';

		if (dirlist_timestmaps) {
			// ISO-8601 date+time
			struct tm mtime;
			if (localtime_r(&st.st_mtime, &mtime)) {
				*data++ = ' '; // space before the date
				data += strftime((char *)data, 20, "%Y-%m-%d %H:%M:%S", &mtime);
				*data++ = ' '; // space after the date
			}
		}

		if (dirlist_long) {
			// attribute and size go at the end if dirlist_long is active
			uint8_t attrbyte = 0;
			if (S_ISDIR(st.st_mode)) attrbyte |= 0x10;
			if (S_ISREG(st.st_mode)) attrbyte |= 0x20;
			if (!(st.st_mode & 0200)) attrbyte |= 0x01; // read-only
			size_t fullsize = st.st_size;
			if (fullsize > 0xffffffff) {
				fullsize = 0xffffffff;
			}

			data += sprintf((char *)data, "%02X %08X ", attrbyte, (unsigned int)fullsize);
		}
		
		free(tmpnam);

		*data++ = 0;
		return data - data_start;
	}

	// link
	*data++ = 1;
	*data++ = 1;

	*data++ = 255; // "65535"
	*data++ = 255;

	memcpy(data, blocks_free, u8strlen(blocks_free));
	data += u8strlen(blocks_free);
	*data++ = 0;

	// link
	*data++ = 0;
	*data++ = 0;
	(void)closedir(dirlist_dirp);
	dirlist_eof = true;
	return data - data_start;
}


static int
create_cwd_listing(uint8_t *data)
{
	uint8_t *data_start = data;
	int file_size;

	// load address
	*data++ = 1;
	*data++ = 8;
	// link
	*data++ = 1;
	*data++ = 1;
	// line number
	*data++ = 0;
	*data++ = 0;
	*data++ = 0x12; // REVERSE ON
	*data++ = '"';
	for (int i = 0; i < 16; i++) {
		*data++ = ' ';
	}
	if (cgetcwd(data - 16, 16)) {
		dirlist_eof = true;
		return 0;
	}
	*data++ = '"';
	*data++ = ' ';
	*data++ = 'H';
	*data++ = 'O';
	*data++ = 'S';
	*data++ = 'T';
	*data++ = ' ';
	*data++ = 0;

	uint8_t *tmp = malloc(u8strlen(hostfscwd)+1);
	if (tmp == NULL) {
		set_error(0x70, 0, 0);
		return 0;
	}
	int i = u8strlen(hostfscwd);
	int j = u8strlen(fsroot_path);
	u8strcpy(tmp,hostfscwd);

	for(; i>= j-1; --i) {
		// find the beginning of a path element
		if (i >= j && tmp[i-1] != '/' && tmp[i-1] != '\\')
			continue;
		
		tmp[i-1]=0;

		if (i < j) {
			u8strcpy(tmp+i,"/");
		}

		file_size = 0;
		size_t namlen = u8strlen(tmp+i);

		if (!namlen) continue; // there was a doubled path separator

		// link
		*data++ = 1;
		*data++ = 1;

		*data++ = file_size & 0xFF;
		*data++ = file_size >> 8;
		if (file_size < 1000) {
			*data++ = ' ';
			if (file_size < 100) {
				*data++ = ' ';
				if (file_size < 10) {
					*data++ = ' ';
				}
			}
		}
		*data++ = '"';

		memcpy(data, tmp+i, namlen);
		data += namlen;
		*data++ = '"';
		for (int i = namlen; i < 16; i++) {
			*data++ = ' ';
		}
		*data++ = ' ';
		*data++ = 'D';
		*data++ = 'I';
		*data++ = 'R';
		*data++ = 0;
	}

	free(tmp);

	// link
	*data++ = 1;
	*data++ = 1;

	*data++ = 255; // "65535"
	*data++ = 255;

	memcpy(data, blocks_free, u8strlen(blocks_free));
	data += u8strlen(blocks_free);
	*data++ = 0;

	// link
	*data++ = 0;
	*data++ = 0;

	dirlist_eof = true;
	dirlist_cwd = true;

	return data - data_start;
}



static char*
error_string(int e)
{
	switch(e) {
		case 0x00:
			return " OK";
		case 0x01:
			return " FILES SCRATCHED";
		case 0x02:
			return "PARTITION SELECTED";
		// 0x2x: Physical disk error
		case 0x20:
			return "READ ERROR"; // generic read error
		case 0x25:
			return "WRITE ERROR"; // generic write error
		case 0x26:
			return "WRITE PROTECT ON";
		// 0x3x: Error parsing the command
		case 0x30: // generic
		case 0x31: // invalid command
		case 0x32: // command buffer overflow
			return "SYNTAX ERROR";
		case 0x33: // illegal filename
			return "ILLEGAL FILENAME";
		case 0x34: // empty file name
			return "EMPTY FILENAME";
		case 0x39: // subdirectory not found
			return "SUBDIRECTORY NOT FOUND";
		// 0x4x: Controller error (CMD addition)
		case 0x49:
			return "INVALID FORMAT"; // partition present, but not FAT32
		// 0x5x: Relative file related error
		// unsupported
		// 0x6x: File error
		case 0x62:
			return " FILE NOT FOUND";
		case 0x63:
			return "FILE EXISTS";
		// 0x7x: Generic disk or device error
		case 0x70:
			return "NO CHANNEL"; // error allocating context
		case 0x71:
			return "DIRECTORY ERROR"; // FAT error
		case 0x72:
			return "PARTITION FULL"; // filesystem full
		case 0x73:
			return "HOST FS V1.0 X16";
		case 0x74:
			return "DRIVE NOT READY"; // illegal partition for any command but "CP"
		case 0x75:
			return "FORMAT ERROR";
		case 0x77:
			return "SELECTED PARTITION ILLEGAL";
		default:
			return "";
	}
}

static void
set_activity(bool active)
{
	uint8_t cbdos_flags = get_kernal_cbdos_flags();
	if (active) {
		cbdos_flags |= 0x10; // set activity flag
	} else {
		cbdos_flags &= ~0x10; // clear activity flag
	}
	set_kernal_cbdos_flags(cbdos_flags);
}

static void
set_error(int e, int t, int s)
{
	snprintf((char *)error, sizeof(error), "%02x,%s,%02d,%02d\r", e, error_string(e), t, s);
	error_len = u8strlen(error);
	error_pos = 0;
	uint8_t cbdos_flags = get_kernal_cbdos_flags();
	if (e < 0x10 || e == 0x73) {
		cbdos_flags &= ~0x20; // clear error
	} else {
		cbdos_flags |= 0x20; // set error flag
	}
	set_kernal_cbdos_flags(cbdos_flags);
}

static void
clear_error()
{
	set_error(0, 0, 0);
}


static void
command(uint8_t *cmd)
{
	if (!cmd[0]) {
		return;
	}
	if (log_ieee) {
		if (cmd[0] == 'P') {
			printf("  COMMAND \"%c\" [binary parameters suppressed]\n", cmd[0]);
		} else {
			printf("  COMMAND \"%s\"\n", cmd);
		}
	}
	switch(cmd[0]) {
		case 'C': // C (copy), CD (change directory), CP (change partition)
			switch(cmd[1]) {
				case 'D': // Change directory
						cchdir(cmd+2);
						return;
				case 'P': // Change partition
					set_error(0x02, 0, 0);
					return;
				default: // Copy
					// NYI
					set_error(0x30, 0, 0);
					return;
			}
		case 'I': // Initialize
			clear_error();
			return;
		case 'M': // MD
			switch(cmd[1]) {
				case 'D': // Make directory
					cmkdir(cmd+2);
					return;
				default: // Memory (not implemented)
					set_error(0x31, 0, 0);
					return;
			}
		case 'P': // Seek
			cseek(cmd[1],
				((uint8_t)cmd[2])
				| ((uint8_t)cmd[3] << 8)
				| ((uint8_t)cmd[4] << 16)
				| ((uint8_t)cmd[5] << 24));
			return;
		case 'R': // RD
			switch(cmd[1]) {
				case 'D': // Remove directory
					crmdir(cmd+2);
					return;
				default: // Rename 
					crename(cmd); // Need to parse out the arg in this function
					return;
			}
		case 'S':
			switch(cmd[1]) {
				case '-': // Swap
					set_error(0x31, 0, 0);
					return;
				default: // Scratch
					cunlink(cmd); // Need to parse out the arg in this function
					return;
			}	
		case 'U':
			switch(cmd[1]) {
				case 'I': // UI: Reset
					set_error(0x73, 0, 0);
					return;
			}
		default:
			if (log_ieee) {
				printf("    (unsupported command ignored)\n");
			}
	}
	set_error(0x30, 0, 0);
}

static void
cchdir(uint8_t *dir)
{
	// The directory name is in dir, coming from the command channel
	// with the CD portion stripped off
	uint8_t *parsed;
	uint8_t *resolved;
	struct stat st;

	if (!u8strcmp(dir,":_")) { // turn CD:← into CD:..
		parsed = parse_dos_filename((uint8_t *)":..", true);
	} else {
		parsed = parse_dos_filename(dir, true);
	}

	if (parsed == NULL) {
		set_error(0x32, 0, 0);
		return;
	}
	
	if ((resolved = resolve_path_iso(parsed, true, WILDCARD_DIR)) == NULL) {
		free(parsed);
		// error already set
		return;
	}

	free(parsed);

	// Is it a directory?
	if (u8stat(resolved, &st)) {
		// FNF
		free(resolved);
		set_error(0x62, 0, 0);
	} else if (!S_ISDIR(st.st_mode)) {
		// Not a directory
		free(resolved);
		set_error(0x39, 0, 0);
	} else {
		// cwd has been changed
		free(hostfscwd);
		hostfscwd = resolved;
	}

	return;
}

static void
cmkdir(uint8_t *dir)
{
	// The directory name is in dir, coming from the command channel
	// with the MD portion stripped off
	uint8_t *resolved;
	uint8_t *parsed;

	parsed = parse_dos_filename(dir, true);

	if (parsed == NULL) {
		set_error(0x32, 0, 0);
		return;
	}

	clear_error();
	if ((resolved = resolve_path_iso(parsed, false, WILDCARD_DIR)) == NULL) {
		free(parsed);
		// error already set
		return;
	}

	free(parsed);
#ifdef __MINGW32__
	if (_mkdir((char *)resolved))
#else
	if (mkdir((char *)resolved,0777))
#endif
	{
		if (errno == EEXIST) {
			set_error(0x63, 0, 0);
		} else {
			set_error(0x62, 0, 0);
		}
	}

	free(resolved);

	return;
}

static void
crename(uint8_t *f)
{
	// This function receives the whole R command, which could be
	// "R:NEW=OLD" or "RENAME:NEW=OLD" or anything in between
	// let's simply find the first colon and chop it there
	uint8_t *tmp = malloc(u8strlen(f)+1);
	if (tmp == NULL) {
		set_error(0x70, 0, 0);
		return;
	}
	u8strcpy(tmp,f);
	uint8_t *d = u8strchr(tmp,':');

	if (d == NULL) {
		// No colon, not a valid rename command
		free(tmp);
		set_error(0x34, 0, 0);
		return;
	}

	d++; // one character after the colon

	// Now split on the = sign to find
	uint8_t *s = u8strchr(d,'=');

	if (s == NULL) {
		// No equals sign, not a valid rename command
		free(tmp);
		set_error(0x34, 0, 0);
		return;
	}

	*(s++) = 0; // null-terminate d and advance s
	
	uint8_t *src;
	uint8_t *dst;

	clear_error();
	if ((src = resolve_path_iso(s, true, WILDCARD_ALL)) == NULL) {
		// source not found
		free(tmp);
		set_error(0x62, 0, 0);
		return;
	}

	if ((dst = resolve_path_iso(d, false, WILDCARD_ALL)) == NULL) {
		// dest not found
		free(tmp);
		free(src);
		set_error(0x39, 0, 0);
		return;
	}

	free(tmp); // we're now done with d and s (part of tmp)

	if (rename((char *)src, (char *)dst)) {
		if (errno == EACCES) {
			set_error(0x63, 0, 0);
		} else if (errno == EINVAL) {
			set_error(0x33, 0, 0);
		} else {
			set_error(0x62, 0, 0);
		}
	}

	free(src);
	free(dst);

	return;
}


static void
crmdir(uint8_t *dir)
{
	// The directory name is in dir, coming from the command channel
	// with the RD: portion stripped off
	uint8_t *resolved;
	uint8_t *parsed;

	parsed = parse_dos_filename(dir, true);

	if (parsed == NULL) {
		set_error(0x32, 0, 0);
		return;
	}

	clear_error();
	if ((resolved = resolve_path_iso(parsed, true, WILDCARD_DIR)) == NULL) {
		free(parsed);
		set_error(0x39, 0, 0);
		return;
	}

	free(parsed);

	if (rmdir((char *)resolved)) {
		if (errno == ENOTEMPTY || errno == EACCES) {
			set_error(0x63, 0, 0);
		} else {
			set_error(0x62, 0, 0);
		}
	}

	free(resolved);

	return;
}


static void
cunlink(uint8_t *f)
{
	// This function receives the whole S command, which could be
	// "S:FILENAME" or "SCRATCH:FILENAME" or anything in between
	// let's simply find the first colon and chop it there
	// TODO path syntax and multiple files
	uint8_t *tmp = malloc(u8strlen(f)+1);
	if (tmp == NULL) {
		set_error(0x70, 0, 0);
		return;
	}
	u8strcpy(tmp,f);
	uint8_t *fn = u8strchr(tmp,':');

	if (fn == NULL) {
		// No colon, not a valid scratch command
		free(tmp);
		set_error(0x34, 0, 0);
		return;
	}

	fn++; // one character after the colon
	uint8_t *resolved;

	clear_error();
	if ((resolved = resolve_path_iso(fn, true, WILDCARD_PRG)) == NULL) {
		free(tmp);
		set_error(0x62, 0, 0);
		return;
	}

	free(tmp); // we're now done with fn (part of tmp)

	if (unlink((char *)resolved)) {
		if (errno == EACCES) {
			set_error(0x63, 0, 0);
		} else {
			set_error(0x62, 0, 0);
		}
	} else {
		set_error(0x01, 0, 0); // 1 file scratched
	}

	free(resolved);

	return;
}


static int
copen(int channel)
{
	if (channel == 15) {
		command(channels[channel].name);
		return -1;
	}

	uint8_t *resolved_filename = NULL;
	uint8_t *parsed_filename = NULL;
	int ret = -1;

	// decode ",P,W"-like suffix to know whether we're writing
	bool append = false;
	channels[channel].read = true;
	channels[channel].write = false;
	uint8_t *first = u8strchr(channels[channel].name, ',');
	if (first) {
		*first = 0; // truncate name here
		uint8_t *second = u8strchr(first+1, ',');
		if (second) {
			switch(second[1]) {
				case 'A':
					append = true;
					// fallthrough
				case 'W':
					channels[channel].read = false;
					channels[channel].write = true;
					break;
				case 'M':
					channels[channel].read = true;
					channels[channel].write = true;
					break;
			}
		}
	}
	if (channel <= 1) {
		// channels 0 and 1 are magic
		channels[channel].write = channel;
		channels[channel].read = !channel;
	}
	if (log_ieee) {
		printf("  OPEN \"%s\",%d (%s%s)\n", channels[channel].name, channel,
			channels[channel].read ? "R" : "",
			channels[channel].write ? "W" : "");
	}

	if (!channels[channel].write && channels[channel].name[0] == '$') {	
		dirlist_pos = 0;
		if (!u8strncmp(channels[channel].name,"$=C",3)) {
			// This emulates the behavior in the ROM code in
			// https://github.com/X16Community/x16-rom/pull/5
			dirlist_len = create_cwd_listing(dirlist);
		} else {
			dirlist_len = create_directory_listing(dirlist, channels[channel].name);
		}
	} else {
		if (!u8strcmp(channels[channel].name, ":*") && prg_file && !prg_consumed) {
			channels[channel].f = prg_file; // special case
			prg_consumed = true;
		} else {
			if ((parsed_filename = parse_dos_filename(channels[channel].name, false)) == NULL) {
				set_error(0x32, 0, 0); // Didn't parse out properly
				return -2; 
			}

			resolved_filename = resolve_path_iso(parsed_filename, false, WILDCARD_PRG);
			free(parsed_filename);

			if (resolved_filename == NULL) {
				// Resolve the path, if we get a null ptr back, error is already set.
				return -2;
			}

			if (path_exists && !overwrite && !append && !channels[channel].read) {
				free(resolved_filename);
				set_error(0x63, 0, 0); // forbid overwrite unless requested
				return -1;
			}
		
			if (append) {
				channels[channel].f = SDL_RWFromFile((char *)resolved_filename, "ab+");
			} else if (channels[channel].read && channels[channel].write) {
				if (access((char *)resolved_filename, F_OK) == 0) {
					channels[channel].f = SDL_RWFromFile((char *)resolved_filename, "rb+");
				} else {
					channels[channel].f = SDL_RWFromFile((char *)resolved_filename, "wb+");
				}
			} else {
				channels[channel].f = SDL_RWFromFile((char *)resolved_filename, channels[channel].write ? "wb" : "rb");
			}

			free(resolved_filename);
		}

		if (!channels[channel].f) {
			if (log_ieee) {
				printf("  FILE NOT FOUND\n");
			}
			set_error(0x62, 0, 0);
			ret = -2; // FNF
		} else {
			clear_error();
		}
	}
	return ret;
}

static void
cclose(int channel)
{
	if (log_ieee) {
		printf("  CLOSE %d\n", channel);
	}
	channels[channel].name[0] = 0;
	if (channels[channel].f) {
		SDL_RWclose(channels[channel].f);
		channels[channel].f = NULL;
	}
}

static void
cseek(int channel, uint32_t pos)
{
	if (channel == 15) {
		set_error(0x30, 0, 0);
		return;
	}

	if (channels[channel].f) {
		SDL_RWseek(channels[channel].f, pos, RW_SEEK_SET);
	}
}

void
ieee_init()
{

	int ch;

	if (!ieee_initialized_once) {
		// Init the hostfs "jail" and cwd
		if (fsroot_path == NULL) { // if null, default to cwd
			// We hold this for the lifetime of the program, and we don't have
			// any sort of destructor, so we rely on the OS teardown to free() it.
			fsroot_path = u8getcwd(NULL, 0);
		} else {
			// Normalize it
			fsroot_path = u8realpath(fsroot_path, NULL);
		}

		if (startin_path == NULL) {
			// same as above
			startin_path = u8getcwd(NULL, 0);
		} else {
			// Normalize it
			startin_path = u8realpath(startin_path, NULL);
		}
		// Quick error checks
		if (fsroot_path == NULL) {
			fprintf(stderr, "Failed to resolve argument to -fsroot\n");
			exit(1);
		}

		if (startin_path == NULL) {
			fprintf(stderr, "Failed to resolve argument to -startin\n");
			exit(1);
		}

		// Now we verify that startin_path is within fsroot_path
		// In other words, if fsroot_path is a left-justified substring of startin_path

		// If startin_path is not reachable, we instead default to setting it
		// back to fsroot_path
		if (u8strncmp(fsroot_path, startin_path, u8strlen(fsroot_path))) { // not equal
			free(startin_path);
			startin_path = fsroot_path;
		}

		for (ch = 0; ch < 16; ch++) {
			channels[ch].f = NULL;
			channels[ch].name[0] = 0;
		}

		ieee_initialized_once = true;
	} else {
		for (ch = 0; ch < 16; ch++) {
			cclose(ch);
		}

		listening = false;
		talking = false;
		opening = false;

		free(hostfscwd);
	}

	// Now initialize our emulated cwd.
	hostfscwd = malloc(u8strlen(startin_path)+1);
	if (hostfscwd == NULL) {
		fprintf(stderr, "Failed to allocate memory for hostfscwd\n");
		exit(1);
	}
	u8strcpy(hostfscwd, startin_path);

	// Locate and remember cbdos_flags variable address in KERNAL vars
	{
		// check JMP instruction at ACPTR API
		if (real_read6502(0xffa5, true, 0) != 0x4c) goto fail;

		// get address of ACPTR routine
		uint16_t kacptr = real_read6502(0xffa6, true, 0) | real_read6502(0xffa7, true, 0) << 8;
		if (kacptr < 0xc000) goto fail;

		// first instruction is BIT cbdos_flags
		if (real_read6502(kacptr, true, 0) != 0x2c) goto fail;

		// get the address of cbdos_flags
		cbdos_flags = real_read6502(kacptr+1, true, 0) | real_read6502(kacptr+2, true, 0) << 8;

		if (cbdos_flags < 0x0200 || cbdos_flags >= 0x0400) goto fail;
		goto success;
fail:
		printf("Unable to find KERNAL cbdos_flags\n");
		cbdos_flags = 0;

	}
success:

	set_error(0x73, 0, 0);
}

int
SECOND(uint8_t a)
{
	int ret = -1;
	if (log_ieee) {
		printf("%s $%02x\n", __func__, a);
	}
	if (listening) {
		channel = a & 0xf;
		opening = false;
		if (channel == 15)
			ret = 0;
		switch(a & 0xf0) {
			case 0x60:
				if (log_ieee) {
					printf("  WRITE %d...\n", channel);
				}
				break;
			case 0xe0:
				cclose(channel);
				break;
			case 0xf0:
				if (log_ieee) {
					printf("  OPEN %d...\n", channel);
				}
				opening = true;
				namelen = 0;
				break;
		}
	}
	return ret;
}

void
TKSA(uint8_t a)
{
	if (log_ieee) {
		printf("%s $%02x\n", __func__, a);
	}
	if (talking) {
		channel = a & 0xf;
	}
}


int
ACPTR(uint8_t *a)
{
	int ret = 0;
	if (channel == 15) {
		*a = error[error_pos++];
		if (error_pos >= error_len) {
			clear_error();
			ret = 0x40; // EOI
		}
	} else if (channels[channel].read) {
		if (channels[channel].name[0] == '$') {
			if (dirlist_pos < dirlist_len) {
				*a = dirlist[dirlist_pos++];
			} else {
				*a = 0;
			}
			if (dirlist_pos == dirlist_len) {
				if (dirlist_eof) {
					ret = 0x40;
				} else {
					dirlist_pos = 0;
					dirlist_len = continue_directory_listing(dirlist);
				}
			}
		} else if (channels[channel].f) {
			if (SDL_RWread(channels[channel].f, a, 1, 1) != 1) {
				ret = 0x42;
				*a = 0;
			} else {
				// We need to send EOI on the last byte of the file.
				// We have to check every time since CMDR-DOS
				// supports random access R/W mode
				
				Sint64 curpos = SDL_RWtell(channels[channel].f);
				if (curpos == SDL_RWseek(channels[channel].f, 0, RW_SEEK_END)) {
					ret = 0x40;
					channels[channel].read = false;
					cclose(channel);
				} else {
					SDL_RWseek(channels[channel].f, curpos, RW_SEEK_SET);
				}
			}
		} else {
			ret = 0x42;
		}
	} else {
		ret = 0x42; // FNF
	}
	if (log_ieee) {
		printf("%s-> $%02x\n", __func__, *a);
	}
	return ret;
}

int
CIOUT(uint8_t a)
{
	int ret = 0;
	if (log_ieee) {
		printf("%s $%02x\n", __func__, a);
	}
	if (listening) {
		if (opening) {
			if (namelen < sizeof(channels[channel].name) - 1) {
				channels[channel].name[namelen++] = a;
			}
		} else {
			if (channel == 15) {
				// P command takes binary parameters, so we can't terminate
				// the command on CR.
				if ((a == 13) && (cmd[0] != 'P')) {
					cmd[cmdlen] = 0;
					command(cmd);
					cmdlen = 0;
				} else {
					if (cmdlen < sizeof(cmd) - 1) {
						cmd[cmdlen++] = a;
					}
				}
			} else if (channels[channel].write && channels[channel].f) {
				if (!SDL_WriteU8(channels[channel].f, a))
					ret = 0x40;
			} else {
				ret = 2; // FNF
			}
		}
	}
	return ret;
}

void
UNTLK() {
	if (log_ieee) {
		printf("%s\n", __func__);
	}
	talking = false;
	set_activity(false);
}

int
UNLSN() {
	int ret = -1;
	if (log_ieee) {
		printf("%s\n", __func__);
	}
	listening = false;
	set_activity(false);
	if (opening) {
		channels[channel].name[namelen] = 0; // term
		opening = false;
		copen(channel);
	} else if (channel == 15) {
		cmd[cmdlen] = 0;
		command(cmd);
		cmdlen = 0;
	}
	return ret;
}

void
LISTEN(uint8_t a)
{
	if (log_ieee) {
		printf("%s $%02x\n", __func__, a);
	}
	if ((a & 0x1f) == UNIT_NO) {
		listening = true;
		set_activity(true);
	}
}

void
TALK(uint8_t a)
{
	if (log_ieee) {
		printf("%s $%02x\n", __func__, a);
	}
	if ((a & 0x1f) == UNIT_NO) {
		talking = true;
		set_activity(true);
	}
}

int
MACPTR(uint16_t addr, uint16_t *c, uint8_t stream_mode)
{
	int ret = 0;
	int count = *c ?: 256;
	uint8_t ram_bank = read6502(0);
	int i = 0;
	if (channels[channel].f) {
		do {
			uint8_t byte = 0;
			ret = ACPTR(&byte);
			write6502(addr, byte);
			i++;
			if (!stream_mode) {
				addr++;
				if (addr == 0xc000) {
					addr = 0xa000;
					ram_bank++;
					write6502(0, ram_bank);
				}
			}
			if (ret > 0) {
				break;
			}
		} while(i < count);
	} else {
		ret = -2;
	}
	*c = i;
	return ret;
}

int
MCIOUT(uint16_t addr, uint16_t *c, uint8_t stream_mode)
{
	int ret = 0;
	int count = *c ?: 256;
	uint8_t ram_bank = read6502(0);
	int i = 0;
	if (channels[channel].f && channels[channel].write) {
		do {
			uint8_t byte;
			byte = read6502(addr);
			i++;
			if (!stream_mode) {
				addr++;
				if (addr == 0xc000) {
					addr = 0xa000;
					ram_bank++;
					write6502(0, ram_bank);
				}
			}
			ret = CIOUT(byte);
			if (ret) {
				break;
			}
		} while(i < count);
	} else {
		ret = -2;
	}
	*c = i;
	return ret;
}
