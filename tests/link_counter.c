#include "aervault.h"
int main(void)
{
    (void)&av_reserve; /* legacy API remains linkable */
    (void)&av_counter_next;
    return 0;
}
