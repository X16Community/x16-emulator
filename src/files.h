#include <stdbool.h>
#include <stdint.h>
#include <zlib.h>

bool file_is_compressed_type(char const *path);

// SDL-like functions to ease substitution of gz for SDL_RW
int gzsize(gzFile f);
int gzwrite8(gzFile f, uint8_t val);
int gzread8(gzFile f);