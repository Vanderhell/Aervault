#include <stdint.h>
#include <stddef.h>
#if AV_LINK_STORAGE_MODULE == 1
#include "aervault_journal.h"
int main(void){return av_journal_mount(NULL,NULL,NULL,NULL,0)==AV_JOURNAL_INVALID?0:1;}
#elif AV_LINK_STORAGE_MODULE == 2
#include "aervault_wal.h"
int main(void){return av_wal_init(NULL,NULL,NULL,0,NULL,NULL)==AV_JOURNAL_INVALID?0:1;}
#elif AV_LINK_STORAGE_MODULE == 3
#include "aervault_queue.h"
int main(void){return av_queue_open(NULL,NULL,NULL,0)==AV_JOURNAL_INVALID?0:1;}
#elif AV_LINK_STORAGE_MODULE == 4
#include "aervault_history.h"
int main(void){return av_history_init(NULL,NULL,NULL,0,0)==AV_JOURNAL_INVALID?0:1;}
#elif AV_LINK_STORAGE_MODULE == 5
#include "aervault_budget.h"
int main(void){return av_budget_check(NULL,0,0)==AV_BUDGET_INVALID?0:1;}
#elif AV_LINK_STORAGE_MODULE == 6
#include "aervault_diag.h"
int main(void){return av_diag_init(NULL,NULL,0)==AV_JOURNAL_INVALID?0:1;}
#elif AV_LINK_STORAGE_MODULE == 7
#include "aervault_nor.h"
int main(void){return av_nor_bind(NULL,NULL,0,NULL,NULL,NULL,NULL)!=0?0:1;}
#else
#error AV_LINK_STORAGE_MODULE must select one target
#endif
