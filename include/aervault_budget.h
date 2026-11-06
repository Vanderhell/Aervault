#ifndef AERVAULT_BUDGET_H
#define AERVAULT_BUDGET_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { AV_BUDGET_ALLOW=0, AV_BUDGET_LIMIT_PROGRAM=1, AV_BUDGET_LIMIT_ERASE=2, AV_BUDGET_INVALID=3 } av_budget_reason;
typedef struct { uint64_t program_limit,erase_limit,program_used,erase_used; } av_budget;
void av_budget_init(av_budget *,uint64_t,uint64_t,uint64_t,uint64_t);
av_budget_reason av_budget_check(const av_budget *,uint32_t,uint32_t);
/* Account actual operations; callers conservatively charge uncertain writes. */
av_budget_reason av_budget_charge(av_budget *,uint32_t,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
