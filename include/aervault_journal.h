#ifndef AERVAULT_JOURNAL_H
#define AERVAULT_JOURNAL_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AV_JOURNAL_HEADER_BYTES 24u
#define AV_JOURNAL_COMMIT_BYTES 1u
typedef enum { AV_JOURNAL_OK=0, AV_JOURNAL_INVALID, AV_JOURNAL_IO,
               AV_JOURNAL_BOUNDS, AV_JOURNAL_FULL, AV_JOURNAL_TORN,
               AV_JOURNAL_CORRUPT, AV_JOURNAL_NOT_READY } av_journal_status;
typedef struct {
    void *ctx;
    int (*read)(void *, uint32_t, void *, size_t);
    int (*program)(void *, uint32_t, const void *, size_t);
    int (*erase)(void *, uint32_t, size_t);
    int (*sync)(void *);
} av_journal_io;
typedef struct {
    uint32_t bank_offset[2];
    uint32_t bank_size;
    uint32_t erase_size;
    uint32_t program_size;
    uint8_t erased_value;
} av_journal_layout;
typedef struct {
    av_journal_io io;
    av_journal_layout layout;
    uint8_t *work;
    size_t work_size;
    uint32_t tail;
    uint32_t record_start;
    uint32_t active_bank;
    uint32_t generation;
    uint64_t next_sequence;
    uint8_t ready;
    uint8_t incomplete_tail;
} av_journal;
typedef struct { uint16_t type; uint64_t sequence; uint32_t length; uint32_t offset; } av_journal_record;
typedef int (*av_journal_visit_fn)(void *, const av_journal_record *, const uint8_t *);
typedef int (*av_journal_keep_fn)(void *, const av_journal_record *, const uint8_t *);

av_journal_status av_journal_format(av_journal *, const av_journal_io *, const av_journal_layout *, uint8_t *, size_t);
av_journal_status av_journal_mount(av_journal *, const av_journal_io *, const av_journal_layout *, uint8_t *, size_t);
av_journal_status av_journal_append(av_journal *, uint16_t, const void *, size_t, uint64_t *);
av_journal_status av_journal_scan(av_journal *, av_journal_visit_fn, void *);
av_journal_status av_journal_compact(av_journal *, av_journal_keep_fn, void *);
uint64_t av_journal_next_sequence(const av_journal *);
uint32_t av_journal_free_bytes(const av_journal *);

#ifdef __cplusplus
}
#endif
#endif
