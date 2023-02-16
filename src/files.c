#include "files.h"

#include <string.h>

bool 
file_is_compressed_type(char const *path)
{
	int len = (int)strlen(path);

	if (strcmp(path + len - 3, ".gz") == 0 || strcmp(path + len - 3, "-gz") == 0) {
		return true;
	} else if (strcmp(path + len - 2, ".z") == 0 || strcmp(path + len - 2, "-z") == 0 || strcmp(path + len - 2, "_z") == 0 || strcmp(path + len - 2, ".Z") == 0) {
		return true;
	}

	return false;
}

int 
gzsize(gzFile f)
{
	int oldseek = gztell(f);
	gzseek(f, 0, SEEK_SET);

	uint8_t read_buffer[64 * 1024];
	int  total_bytes = 0;
	int  bytes_read  = 0;

	do {
		bytes_read = gzread(f, read_buffer, 64 * 1024);
		total_bytes += bytes_read;
	} while (bytes_read == 64 * 1024);

	gzseek(f, oldseek, SEEK_SET);
	return total_bytes;
}

int 
gzwrite8(gzFile f, uint8_t val)
{
	return gzwrite(f, &val, 1);
}

int 
gzread8(gzFile f)
{
	uint8_t value;
	gzread(f, &value, 1);
	return value;
}