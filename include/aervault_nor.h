#ifndef AERVAULT_NOR_H
#define AERVAULT_NOR_H
#include "aervault_journal.h"
#include <mflash.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { mflash_t *flash; size_t partition; int (*sync)(void *); void *sync_ctx; } av_nor;
/* Binds two equal erase-aligned banks inside one already registered NOR partition. */
int av_nor_bind(av_nor *, mflash_t *, size_t, int (*)(void *), void *, av_journal_io *, av_journal_layout *);
int av_nor_write_counts(av_nor *, uint32_t *, uint32_t *);
#ifdef __cplusplus
}
#endif
#endif
