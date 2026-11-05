#include "ram_backend.h"
#include <string.h>
void av_ram_init(av_ram_backend *m, uint8_t *a, uint8_t *b, size_t cap)
{ memset(m, 0, sizeof *m); m->bytes[0]=a; m->bytes[1]=b; m->capacity=cap; }
int av_ram_read(void *ctx, unsigned id, uint8_t *out, size_t cap, size_t *n)
{
    av_ram_backend *m=(av_ram_backend *)ctx;
    if (id>1 || m->read_error) return m->read_error ? m->read_error : -1;
    *n=m->length[id]; if (!*n) return 1;
    if (*n>cap) return -2;
    memcpy(out,m->bytes[id],*n); return 0;
}
int av_ram_replace(void *ctx, unsigned id, const uint8_t *in, size_t n)
{
    av_ram_backend *m=(av_ram_backend *)ctx; int mode;
    if (id>1 || n>m->capacity) return -3;
    mode=++m->writes==m->fail_at ? m->mode : 0;
    if (mode==1) return -33;
    if (mode==3) { size_t k=n/2; memcpy(m->bytes[id],in,k); m->length[id]=k; return -33; }
    memcpy(m->bytes[id],in,n); m->length[id]=n;
    return mode==2 ? -33 : 0;
}
