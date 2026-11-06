#include "aervault_queue.h"
#include <string.h>
#define Q_ENQUEUE 0x2001u
#define Q_ACK 0x2002u
static uint64_t get64q(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v|=(uint64_t)p[i]<<(8u*i);return v;}
static void put64q(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(8u*i));}
static size_t find_item(const av_queue*q,uint64_t id){size_t i;for(i=0;i<q->count;i++)if(q->items[i].id==id)return i;return q->count;}
static void remove_item(av_queue*q,size_t i){if(i<q->count){memmove(q->items+i,q->items+i+1,(q->count-i-1)*sizeof(*q->items));q->count--;}}
static int recover_visit(void*ctx,const av_journal_record*r,const uint8_t*p){av_queue*q=(av_queue*)ctx;size_t i;if(r->type==Q_ENQUEUE){if(q->count==q->capacity){q->invalid=1;return 0;}q->items[q->count].id=r->sequence;q->items[q->count].length=r->length;q->count++;}else if(r->type==Q_ACK){if(r->length!=8){q->invalid=1;return 0;}i=find_item(q,get64q(p));if(i==q->count){q->invalid=1;return 0;}remove_item(q,i);}return 0;}
av_journal_status av_queue_open(av_queue*q,av_journal*j,av_queue_item*items,size_t n){av_journal_status s;if(!q||!j||!items||!n)return AV_JOURNAL_INVALID;q->journal=j;q->items=items;q->capacity=n;q->count=0;q->ready=0;q->invalid=0;s=av_journal_scan(j,recover_visit,q);if(q->invalid)return AV_JOURNAL_CORRUPT;if(s!=AV_JOURNAL_OK&&s!=AV_JOURNAL_TORN)return s;q->ready=1;return s;}
av_journal_status av_queue_push(av_queue*q,const void*p,size_t n,uint64_t*id){uint64_t seq;av_journal_status s;if(!q||!q->ready||(n&&!p)||n>UINT32_MAX)return AV_JOURNAL_INVALID;if(q->count==q->capacity)return AV_JOURNAL_FULL;s=av_journal_append(q->journal,Q_ENQUEUE,p,n,&seq);if(s!=AV_JOURNAL_OK){if(s==AV_JOURNAL_IO||s==AV_JOURNAL_TORN)q->ready=0;return s;}q->items[q->count].id=seq;q->items[q->count].length=(uint32_t)n;q->count++;if(id)*id=seq;return AV_JOURNAL_OK;}
typedef struct{uint64_t id;void*out;size_t cap;size_t*len;int found;int small;} peek_state;
static int peek_visit(void*ctx,const av_journal_record*r,const uint8_t*p){peek_state*s=(peek_state*)ctx;if(r->type==Q_ENQUEUE&&r->sequence==s->id){if(r->length>s->cap){s->small=1;return 0;}if(r->length)memcpy(s->out,p,r->length);*s->len=r->length;s->found=1;}return 0;}
av_journal_status av_queue_peek(av_queue*q,uint64_t*id,void*out,size_t cap,size_t*len){peek_state s;av_journal_status rc;if(!q||!q->ready||!id||!len||(!out&&cap))return AV_JOURNAL_INVALID;if(!q->count)return AV_JOURNAL_FULL;*id=q->items[0].id;s.id=*id;s.out=out;s.cap=cap;s.len=len;s.found=0;s.small=0;rc=av_journal_scan(q->journal,peek_visit,&s);if(rc!=AV_JOURNAL_OK&&!(rc==AV_JOURNAL_TORN&&s.found))return rc;return s.small?AV_JOURNAL_BOUNDS:s.found?AV_JOURNAL_OK:rc==AV_JOURNAL_TORN?AV_JOURNAL_TORN:AV_JOURNAL_CORRUPT;}
av_journal_status av_queue_ack(av_queue*q,uint64_t id){uint8_t raw[8];size_t i;av_journal_status s;if(!q||!q->ready)return AV_JOURNAL_INVALID;i=find_item(q,id);if(i==q->count)return AV_JOURNAL_INVALID;put64q(raw,id);s=av_journal_append(q->journal,Q_ACK,raw,sizeof raw,NULL);if(s!=AV_JOURNAL_OK){if(s==AV_JOURNAL_IO||s==AV_JOURNAL_TORN)q->ready=0;return s;}remove_item(q,i);return AV_JOURNAL_OK;}
typedef struct{const av_queue*q;} keep_ctx;
static int keep_pending(void*ctx,const av_journal_record*r,const uint8_t*p){keep_ctx*k=(keep_ctx*)ctx;(void)p;if(r->type==Q_ENQUEUE)return find_item(k->q,r->sequence)<k->q->count;if(r->type==Q_ACK)return 0;return 1;}
av_journal_status av_queue_compact(av_queue*q){keep_ctx k;if(!q||!q->ready)return AV_JOURNAL_INVALID;k.q=q;return av_journal_compact(q->journal,keep_pending,&k);}
