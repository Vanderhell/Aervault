#include "aervault_journal.h"
#include <string.h>

#define AVJ_MAGIC UINT32_C(0x314A5641)
#define AVR_MAGIC UINT32_C(0x31525641)
#define AVJ_VERSION 1u
#define AVJ_BANK_HEADER 16u

static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { unsigned i; for(i=0;i<4;i++) p[i]=(uint8_t)(v>>(8u*i)); }
static void put64(uint8_t *p, uint64_t v) { unsigned i; for(i=0;i<8;i++) p[i]=(uint8_t)(v>>(8u*i)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t get64(const uint8_t *p) { uint64_t v=0; unsigned i; for(i=0;i<8;i++) v|=(uint64_t)p[i]<<(8u*i); return v; }
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n) {
    size_t i; unsigned b; c=~c;
    for(i=0;i<n;i++){ c^=p[i]; for(b=0;b<8;b++) c=(c>>1)^((c&1u)?UINT32_C(0xEDB88320):0u); }
    return ~c;
}
static int align_up(uint32_t n, uint32_t a, uint32_t *out) {
    uint64_t v; if(!a) return 0; v=((uint64_t)n+a-1u)/a*a; if(v>UINT32_MAX) return 0; *out=(uint32_t)v; return 1;
}
static int valid(const av_journal_io *io, const av_journal_layout *l, uint8_t *w, size_t wn) {
    uint32_t start, header_program, other_end;
    if(!io||!l||!w||!io->read||!io->program||!io->erase||!io->sync||!l->bank_size||
       !l->erase_size||!l->program_size||l->bank_size%l->erase_size||
       l->bank_offset[0]%l->erase_size||l->bank_offset[1]%l->erase_size||
       l->bank_offset[0]==l->bank_offset[1]||
       (uint64_t)l->bank_offset[0]+l->bank_size>UINT32_MAX||
       (uint64_t)l->bank_offset[1]+l->bank_size>UINT32_MAX) return 0;
    if(!((uint64_t)l->bank_offset[0]+l->bank_size<=l->bank_offset[1]||
         (uint64_t)l->bank_offset[1]+l->bank_size<=l->bank_offset[0])) return 0;
    if(!align_up(AVJ_BANK_HEADER,l->program_size,&header_program)||
       header_program>UINT32_MAX-l->program_size||
       !align_up(header_program+l->program_size,l->erase_size,&start)) return 0;
    other_end=start;
    return l->bank_size>other_end && wn>=AV_JOURNAL_HEADER_BYTES &&
           wn>=header_program && wn>=l->program_size && l->program_size<=l->erase_size;
}
static uint32_t rec_start(const av_journal_layout *l) {
    uint32_t n=0; (void)align_up(AVJ_BANK_HEADER,l->program_size,&n);
    n+=l->program_size; (void)align_up(n,l->erase_size,&n); return n;
}
static uint32_t commit_off(const av_journal_layout *l, uint32_t body) {
    uint32_t n=0; (void)align_up(body,l->program_size,&n); return n;
}
static int erased(const uint8_t *p,size_t n,uint8_t value) { size_t i; for(i=0;i<n;i++) if(p[i]!=value) return 0; return 1; }
static int erased_magic_byte(const uint8_t *p,uint8_t value) { size_t i; for(i=0;i<4;i++) if(p[i]==value)return 1; return 0; }
static av_journal_status write_bank_header(av_journal *j,uint32_t bank,uint32_t gen) {
    uint32_t c=commit_off(&j->layout,AVJ_BANK_HEADER), base=j->layout.bank_offset[bank];
    memset(j->work,j->layout.erased_value,c);
    put32(j->work,AVJ_MAGIC); put16(j->work+4,AVJ_VERSION); put16(j->work+6,0); put32(j->work+8,gen);
    put32(j->work+12,crc32_update(0,j->work,12));
    if(j->io.program(j->io.ctx,base,j->work,c)!=0 || j->io.sync(j->io.ctx)!=0) return AV_JOURNAL_IO;
    memset(j->work,j->layout.erased_value,j->layout.program_size); j->work[0]=0;
    if(j->io.program(j->io.ctx,base+c,j->work,j->layout.program_size)!=0 || j->io.sync(j->io.ctx)!=0) return AV_JOURNAL_IO;
    return AV_JOURNAL_OK;
}
static int read_bank_header(av_journal *j,uint32_t bank,uint32_t *gen) {
    uint32_t c=commit_off(&j->layout,AVJ_BANK_HEADER), base=j->layout.bank_offset[bank];
    uint8_t marker=0xffu;
    if(j->io.read(j->io.ctx,base,j->work,AVJ_BANK_HEADER)!=0 || j->io.read(j->io.ctx,base+c,&marker,1)!=0) return -1;
    if(get32(j->work)!=AVJ_MAGIC||get16(j->work+4)!=AVJ_VERSION||
       get32(j->work+12)!=crc32_update(0,j->work,12)||marker!=0) return 0;
    *gen=get32(j->work+8); return 1;
}
static av_journal_status write_record(av_journal *j,uint32_t bank,uint32_t *tail,
                                      uint16_t type,uint64_t seq,const uint8_t *payload,size_t len) {
    uint32_t body, co, total, base=j->layout.bank_offset[bank];
    if(len>UINT32_MAX || len>j->work_size-AV_JOURNAL_HEADER_BYTES) return AV_JOURNAL_BOUNDS;
    body=AV_JOURNAL_HEADER_BYTES+(uint32_t)len;
    co=commit_off(&j->layout,body);
    if(co>UINT32_MAX-j->layout.program_size || co>j->work_size) return AV_JOURNAL_BOUNDS;
    total=co+j->layout.program_size;
    if(*tail>j->layout.bank_size || total>j->layout.bank_size-*tail) return AV_JOURNAL_FULL;
    if(payload!=j->work+AV_JOURNAL_HEADER_BYTES && len) memmove(j->work+AV_JOURNAL_HEADER_BYTES,payload,len);
    put32(j->work,AVR_MAGIC); put16(j->work+4,AVJ_VERSION); put16(j->work+6,type);
    put64(j->work+8,seq); put32(j->work+16,(uint32_t)len); put32(j->work+20,0);
    put32(j->work+20,crc32_update(crc32_update(0,j->work,20),j->work+24,len));
    if(co>body) memset(j->work+body,j->layout.erased_value,co-body);
    if(j->io.program(j->io.ctx,base+*tail,j->work,co)!=0||j->io.sync(j->io.ctx)!=0) return AV_JOURNAL_IO;
    memset(j->work,j->layout.erased_value,j->layout.program_size); j->work[0]=0;
    if(j->io.program(j->io.ctx,base+*tail+co,j->work,j->layout.program_size)!=0||j->io.sync(j->io.ctx)!=0) return AV_JOURNAL_IO;
    *tail+=total; return AV_JOURNAL_OK;
}
static av_journal_status scan_one(av_journal *j,uint32_t bank,uint32_t *tail,uint64_t *next,
                                  av_journal_visit_fn visit,void *ctx) {
    uint32_t off=rec_start(&j->layout), c, total, len; uint64_t seq; uint16_t type; uint8_t marker;
    for(;;) {
        if(off>j->layout.bank_size || AV_JOURNAL_HEADER_BYTES>j->layout.bank_size-off) { *tail=off; return AV_JOURNAL_OK; }
        if(j->io.read(j->io.ctx,j->layout.bank_offset[bank]+off,j->work,AV_JOURNAL_HEADER_BYTES)!=0) return AV_JOURNAL_IO;
        if(erased(j->work,AV_JOURNAL_HEADER_BYTES,j->layout.erased_value)){ *tail=off; return AV_JOURNAL_OK; }
        if(get32(j->work)!=AVR_MAGIC) {
            *tail=off;
            if(erased_magic_byte(j->work,j->layout.erased_value)){j->incomplete_tail=1;return AV_JOURNAL_TORN;}
            return AV_JOURNAL_CORRUPT;
        }
        if(get16(j->work+4)!=AVJ_VERSION) { j->incomplete_tail=1; *tail=off; return AV_JOURNAL_TORN; }
        type=get16(j->work+6); seq=get64(j->work+8); len=get32(j->work+16);
        if((size_t)len>j->work_size-AV_JOURNAL_HEADER_BYTES) { j->incomplete_tail=1; *tail=off; return AV_JOURNAL_TORN; }
        c=commit_off(&j->layout,AV_JOURNAL_HEADER_BYTES+len);
        if(c>UINT32_MAX-j->layout.program_size || c>j->work_size) return AV_JOURNAL_BOUNDS;
        total=c+j->layout.program_size;
        if(total>j->layout.bank_size-off) { j->incomplete_tail=1; *tail=off; return AV_JOURNAL_TORN; }
        if(len&&j->io.read(j->io.ctx,j->layout.bank_offset[bank]+off+AV_JOURNAL_HEADER_BYTES,j->work+AV_JOURNAL_HEADER_BYTES,len)!=0) return AV_JOURNAL_IO;
        marker=0xffu;
        if(j->io.read(j->io.ctx,j->layout.bank_offset[bank]+off+c,&marker,1)!=0) return AV_JOURNAL_IO;
        if(marker!=0) { j->incomplete_tail=1; *tail=off; return AV_JOURNAL_TORN; }
        if(get32(j->work+20)!=crc32_update(crc32_update(0,j->work,20),j->work+24,len)) { *tail=off; return AV_JOURNAL_CORRUPT; }
        if(seq>=*next) { if(seq==UINT64_MAX) return AV_JOURNAL_CORRUPT; *next=seq+1; }
        if(visit) { av_journal_record r; r.type=type;r.sequence=seq;r.length=len;r.offset=off; if(visit(ctx,&r,j->work+24)!=0) return AV_JOURNAL_OK; }
        off+=total;
    }
}
static av_journal_status mount_ready(av_journal *j) {
    uint32_t g0=0,g1=0; int v0=read_bank_header(j,0,&g0), v1=read_bank_header(j,1,&g1); av_journal_status s;
    if(v0<0||v1<0) return AV_JOURNAL_IO;
    if(!v0&&!v1) return AV_JOURNAL_CORRUPT;
    j->active_bank=(uint32_t)(v1&&(!v0||(int32_t)(g1-g0)>0)?1:0);
    j->generation=j->active_bank?g1:g0; j->next_sequence=1; j->incomplete_tail=0;
    s=scan_one(j,j->active_bank,&j->tail,&j->next_sequence,NULL,NULL);
    if(s!=AV_JOURNAL_OK&&s!=AV_JOURNAL_TORN) return s;
    j->ready=1; return s;
}
av_journal_status av_journal_format(av_journal *j,const av_journal_io *io,const av_journal_layout *l,uint8_t *w,size_t wn) {
    unsigned b; av_journal_status s;
    if(!j||!valid(io,l,w,wn)) return AV_JOURNAL_INVALID;
    memset(j,0,sizeof(*j)); j->io=*io;j->layout=*l;j->work=w;j->work_size=wn;j->record_start=rec_start(l);
    for(b=0;b<2;b++) if(j->io.erase(j->io.ctx,l->bank_offset[b],l->bank_size)!=0) return AV_JOURNAL_IO;
    if(j->io.sync(j->io.ctx)!=0) return AV_JOURNAL_IO;
    s=write_bank_header(j,0,1); if(s!=AV_JOURNAL_OK) return s;
    return mount_ready(j);
}
av_journal_status av_journal_mount(av_journal *j,const av_journal_io *io,const av_journal_layout *l,uint8_t *w,size_t wn) {
    if(!j||!valid(io,l,w,wn)) return AV_JOURNAL_INVALID;
    memset(j,0,sizeof(*j));j->io=*io;j->layout=*l;j->work=w;j->work_size=wn;j->record_start=rec_start(l);
    return mount_ready(j);
}
av_journal_status av_journal_append(av_journal *j,uint16_t type,const void *data,size_t len,uint64_t *seq) {
    av_journal_status s; uint64_t n;
    if(!j||!j->ready) return AV_JOURNAL_NOT_READY;
    if(!type||(len&&!data)||len>j->work_size-AV_JOURNAL_HEADER_BYTES) return AV_JOURNAL_INVALID;
    if(j->incomplete_tail) return AV_JOURNAL_TORN;
    if(j->next_sequence==UINT64_MAX) return AV_JOURNAL_BOUNDS;
    n=j->next_sequence;
    s=write_record(j,j->active_bank,&j->tail,type,n,(const uint8_t *)data,len);
    if(s!=AV_JOURNAL_OK){ if(s==AV_JOURNAL_IO) j->incomplete_tail=1; return s; }
    j->next_sequence=n+1; if(seq)*seq=n; return AV_JOURNAL_OK;
}
av_journal_status av_journal_scan(av_journal *j,av_journal_visit_fn fn,void *ctx) {
    uint32_t tail;uint64_t next=1;av_journal_status s;
    if(!j||!j->ready||!fn)return AV_JOURNAL_INVALID;
    s=scan_one(j,j->active_bank,&tail,&next,fn,ctx);
    return s;
}
av_journal_status av_journal_compact(av_journal *j,av_journal_keep_fn keep,void *ctx) {
    uint32_t old,bank,off,tail;uint64_t next=1;av_journal_status s;
    if(!j||!j->ready||!keep)return AV_JOURNAL_INVALID;
    old=j->active_bank;bank=old^1u;
    if(j->io.erase(j->io.ctx,j->layout.bank_offset[bank],j->layout.bank_size)!=0||j->io.sync(j->io.ctx)!=0){j->ready=0;return AV_JOURNAL_IO;}
    off=rec_start(&j->layout); tail=off; j->incomplete_tail=0;
    for(;;){
        av_journal_record r;uint32_t c,total,len;uint64_t seq;uint16_t type;
        if(j->io.read(j->io.ctx,j->layout.bank_offset[old]+off,j->work,AV_JOURNAL_HEADER_BYTES)!=0){j->ready=0;return AV_JOURNAL_IO;}
        if(erased(j->work,AV_JOURNAL_HEADER_BYTES,j->layout.erased_value))break;
        if(get32(j->work)!=AVR_MAGIC){if(erased_magic_byte(j->work,j->layout.erased_value))break;j->ready=0;return AV_JOURNAL_CORRUPT;}
        if(get16(j->work+4)!=AVJ_VERSION){j->ready=0;return AV_JOURNAL_CORRUPT;}
        type=get16(j->work+6);seq=get64(j->work+8);len=get32(j->work+16);
        if((size_t)len>j->work_size-AV_JOURNAL_HEADER_BYTES){j->ready=0;return AV_JOURNAL_CORRUPT;}
        c=commit_off(&j->layout,AV_JOURNAL_HEADER_BYTES+len);if(c>j->work_size||c>UINT32_MAX-j->layout.program_size){j->ready=0;return AV_JOURNAL_CORRUPT;}total=c+j->layout.program_size;
        { uint8_t marker=0xffu;
          if(total>j->layout.bank_size-off){j->ready=0;return AV_JOURNAL_CORRUPT;}
          if(j->io.read(j->io.ctx,j->layout.bank_offset[old]+off+AV_JOURNAL_HEADER_BYTES,j->work+AV_JOURNAL_HEADER_BYTES,len)!=0||j->io.read(j->io.ctx,j->layout.bank_offset[old]+off+c,&marker,1)!=0){j->ready=0;return AV_JOURNAL_IO;}
          if(marker!=0)break;
          if(get32(j->work+20)!=crc32_update(crc32_update(0,j->work,20),j->work+24,len)){j->ready=0;return AV_JOURNAL_CORRUPT;} }
        r.type=type;r.sequence=seq;r.length=len;r.offset=off;
        if(keep(ctx,&r,j->work+24)){s=write_record(j,bank,&tail,type,seq,j->work+24,len);if(s!=AV_JOURNAL_OK){if(s==AV_JOURNAL_IO)j->ready=0;return s;}}
        off+=total;
    }
    if(j->generation==UINT32_MAX)return AV_JOURNAL_BOUNDS;
    s=write_bank_header(j,bank,j->generation+1u);if(s!=AV_JOURNAL_OK){j->ready=0;return s;}
    j->active_bank=bank;j->generation++;j->tail=tail;j->incomplete_tail=0;
    /* Keep global sequence monotonic even after dropping old records. */
    s=scan_one(j,bank,&j->tail,&next,NULL,NULL);if(s!=AV_JOURNAL_OK&&s!=AV_JOURNAL_TORN){j->ready=0;return s;}
    if(next<j->next_sequence) next=j->next_sequence;
    j->next_sequence=next;
    j->ready=1;
    return AV_JOURNAL_OK;
}
uint64_t av_journal_next_sequence(const av_journal *j){return (j&&j->ready)?j->next_sequence:0;}
uint32_t av_journal_free_bytes(const av_journal *j){return (j&&j->ready&&j->tail<j->layout.bank_size)?j->layout.bank_size-j->tail:0;}
