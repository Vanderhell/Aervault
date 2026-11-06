#ifndef AERVAULT_WAL_H
#define AERVAULT_WAL_H
#include "aervault_journal.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*av_wal_apply_fn)(void *, uint32_t, const void *, size_t);
typedef int (*av_wal_read_fn)(void *, uint32_t, void *, size_t, size_t *);
typedef struct { uint32_t key; const void *old_value; size_t old_length; const void *new_value; size_t new_length; } av_wal_change;
typedef struct { av_journal *journal; uint8_t *work; size_t work_size; av_wal_apply_fn apply; void *apply_ctx; uint8_t recovered; } av_wal;
av_journal_status av_wal_init(av_wal *, av_journal *, uint8_t *, size_t, av_wal_apply_fn, void *);
av_journal_status av_wal_update(av_wal *, uint32_t, const void *, size_t, const void *, size_t);
av_journal_status av_wal_update_many(av_wal *, const av_wal_change *, size_t);
av_journal_status av_wal_read(av_wal *, av_wal_read_fn, uint32_t, void *, size_t, size_t *);
int av_wal_is_ready(const av_wal *);
av_journal_status av_wal_recover(av_wal *);
#ifdef __cplusplus
}
#endif
#endif
