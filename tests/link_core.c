#include "aervault.h"
int main(void)
{
    (void)&av_open;
    (void)&av_provision_id;
    (void)&av_put;
    (void)&av_get;
    (void)&av_load; /* legacy API remains linkable */
    return 0;
}
