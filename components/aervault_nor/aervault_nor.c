#include "aervault_nor.h"

static int nor_read(void *ctx,uint32_t off,void *buf,size_t n){av_nor *a=(av_nor *)ctx;if(n>UINT32_MAX)return -1;return (int)mflash_read(a->flash,a->partition,off,buf,(uint32_t)n);}
static int nor_program(void *ctx,uint32_t off,const void *buf,size_t n){av_nor *a=(av_nor *)ctx;if(n>UINT32_MAX)return -1;return (int)mflash_write(a->flash,a->partition,off,buf,(uint32_t)n);}
static int nor_erase(void *ctx,uint32_t off,size_t n){av_nor *a=(av_nor *)ctx;if(n>UINT32_MAX)return -1;return (int)mflash_erase(a->flash,a->partition,off,(uint32_t)n);}
static int nor_sync(void *ctx){av_nor *a=(av_nor *)ctx;return a->sync?a->sync(a->sync_ctx):0;}
int av_nor_bind(av_nor *out,mflash_t *flash,size_t part,int (*sync)(void *),void *sync_ctx,av_journal_io *io,av_journal_layout *layout){
    const mflash_partition_t *p;
    uint32_t bank;
    if(!out||!flash||!io||!layout||!sync)return -1;
    p=mflash_partition_at(flash,part);
    if(!p||p->type!=MFLASH_TYPE_NOR||p->read_only||p->erase_size==0u||p->program_size==0u)return -1;
    bank=(p->size/2u/p->erase_size)*p->erase_size;
    if(bank<p->erase_size*2u||bank>UINT32_MAX-p->base)return -1;
    out->flash=flash;out->partition=part;out->sync=sync;out->sync_ctx=sync_ctx;
    io->ctx=out;io->read=nor_read;io->program=nor_program;io->erase=nor_erase;io->sync=nor_sync;
    layout->bank_offset[0]=0u;layout->bank_offset[1]=bank;layout->bank_size=bank;
    layout->erase_size=p->erase_size;layout->program_size=p->program_size;layout->erased_value=0xffu;
    return 0;
}
int av_nor_write_counts(av_nor*a,uint32_t*programs,uint32_t*erases){mflash_stats_t s;if(!a||!programs||!erases)return -1;if(mflash_get_stats(a->flash,a->partition,&s)!=MFLASH_OK)return -1;*programs=s.writes;*erases=s.erases;return 0;}
