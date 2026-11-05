#ifndef AERVAULT_RAM_BACKEND_H
#define AERVAULT_RAM_BACKEND_H
#include "aervault.h"
typedef struct {
    uint8_t *bytes[2]; size_t capacity, length[2];
    unsigned writes, fail_at; int mode, read_error;
} av_ram_backend;
/* mode: 1 fail before write, 2 fail after full write, 3 fail after half write */
void av_ram_init(av_ram_backend *, uint8_t *, uint8_t *, size_t);
int av_ram_read(void *, unsigned, uint8_t *, size_t, size_t *);
int av_ram_replace(void *, unsigned, const uint8_t *, size_t);
#endif
