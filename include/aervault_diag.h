#ifndef AERVAULT_DIAG_H
#define AERVAULT_DIAG_H
#include "aervault_journal.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AV_DIAG_PAYLOAD_MAX 32u
typedef struct { uint64_t timestamp; uint32_t event; uint8_t severity; uint8_t length; uint8_t payload[AV_DIAG_PAYLOAD_MAX]; } av_diag_record;
typedef struct { av_journal *journal; size_t retain_count; } av_diag;
av_journal_status av_diag_init(av_diag *,av_journal *,size_t);
av_journal_status av_diag_write(av_diag *,const av_diag_record *,uint64_t *);
av_journal_status av_diag_read(av_diag *,uint64_t,av_diag_record *);
av_journal_status av_diag_compact(av_diag *);
#ifdef __cplusplus
}
#endif
#endif
