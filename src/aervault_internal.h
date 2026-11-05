#ifndef AERVAULT_INTERNAL_H
#define AERVAULT_INTERNAL_H
#include "aervault.h"
typedef struct { uint8_t *bytes; uint64_t revision; size_t length; av_status status; } av_slot;
typedef struct { const av_context *context; uint32_t id; } av_context_binding;
size_t av_internal_setup(const av_object *, uint8_t *, size_t, av_error *);
av_status av_internal_scan(const av_object *, uint8_t *, size_t, av_slot [2], unsigned *, av_error *);
av_status av_internal_write(const av_object *, uint8_t *, uint64_t, const void *, size_t, av_error *);
av_status av_internal_context_object(const av_context *, uint32_t,
                                     av_context_binding *, av_object *);
uint64_t av_internal_get(const uint8_t *, unsigned);
void av_internal_put(uint8_t *, uint64_t, unsigned);
#endif
