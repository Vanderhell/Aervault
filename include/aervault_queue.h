#ifndef AERVAULT_QUEUE_H
#define AERVAULT_QUEUE_H
#include "aervault_journal.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t id; uint32_t length; } av_queue_item;
typedef struct { av_journal *journal; av_queue_item *items; size_t capacity,count; uint8_t ready,invalid; } av_queue;
av_journal_status av_queue_open(av_queue *, av_journal *, av_queue_item *, size_t);
av_journal_status av_queue_push(av_queue *, const void *, size_t, uint64_t *);
av_journal_status av_queue_peek(av_queue *, uint64_t *, void *, size_t, size_t *);
av_journal_status av_queue_ack(av_queue *, uint64_t);
av_journal_status av_queue_compact(av_queue *);
#ifdef __cplusplus
}
#endif
#endif
