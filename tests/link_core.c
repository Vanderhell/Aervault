#include "aervault.h"
int main(void)
{
    av_context context;
    (void)av_open(&context, 0);
    return av_put(&context, 1, 0, 0, 0) == AV_OK;
}
