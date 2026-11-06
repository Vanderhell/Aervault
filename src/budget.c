#include "aervault_budget.h"
#include <stddef.h>
void av_budget_init(av_budget*b,uint64_t pl,uint64_t el,uint64_t pu,uint64_t eu){if(!b)return;b->program_limit=pl;b->erase_limit=el;b->program_used=pu;b->erase_used=eu;}
av_budget_reason av_budget_check(const av_budget*b,uint32_t p,uint32_t e){if(!b)return AV_BUDGET_INVALID;if(b->program_used>b->program_limit)return AV_BUDGET_LIMIT_PROGRAM;if(b->erase_used>b->erase_limit)return AV_BUDGET_LIMIT_ERASE;if((uint64_t)p>b->program_limit-b->program_used)return AV_BUDGET_LIMIT_PROGRAM;if((uint64_t)e>b->erase_limit-b->erase_used)return AV_BUDGET_LIMIT_ERASE;return AV_BUDGET_ALLOW;}
av_budget_reason av_budget_charge(av_budget*b,uint32_t p,uint32_t e){av_budget_reason r=av_budget_check(b,p,e);if(r!=AV_BUDGET_ALLOW)return r;b->program_used+=p;b->erase_used+=e;return AV_BUDGET_ALLOW;}
