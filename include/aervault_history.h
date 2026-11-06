#ifndef AERVAULT_HISTORY_H
#define AERVAULT_HISTORY_H
#include "aervault.h"
#include "aervault_journal.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { av_journal *journal; uint8_t *work; size_t work_size; size_t retain_count; } av_history;
av_journal_status av_history_init(av_history *, av_journal *, uint8_t *, size_t, size_t);
/* Snapshots only a successfully recovered, authenticated AV01 object revision. */
av_journal_status av_history_checkpoint(av_history *, av_context *, uint32_t, uint64_t *);
av_journal_status av_history_latest(av_history *, uint32_t, uint64_t *, void *, size_t, size_t *);
av_journal_status av_history_compact(av_history *);
#ifdef __cplusplus
}
#endif
#endif
