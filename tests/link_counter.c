#include "aervault.h"
#include <string.h>
int main(void)
{
    av_context context; uint64_t next; memset(&context,0,sizeof context);
    (void)av_counter_next(&context, 1, &next, 0);
    return av_reserve(0, 0, 0, 1, &next, 0) == AV_OK;
}
