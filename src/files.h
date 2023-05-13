#include <stdbool.h>
#include <stdint.h>

struct x16file;

#define XSEEK_SET	0
#define XSEEK_END	1
#define XSEEK_CUR	2

bool file_is_compressed_type(char const *path);
const char *file_find_extension(const char *path, const char *mark);

void files_shutdown();

struct x16file *x16open(const char *path, const char *attribs);
void x16close(struct x16file *f);

int64_t x16size(struct x16file *f);
int x16seek(struct x16file *f, int64_t pos, int origin);
int64_t x16tell(struct x16file *f);


int x16write8(struct x16file *f, uint8_t val);
uint8_t x16read8(struct x16file *f);

uint64_t x16write(struct x16file *f, const uint8_t *data, uint64_t data_size, uint64_t data_count);
uint64_t x16read(struct x16file *f, uint8_t *data, uint64_t data_size, uint64_t data_count);