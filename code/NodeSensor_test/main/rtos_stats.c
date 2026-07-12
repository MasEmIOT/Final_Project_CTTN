#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtos_stats.h"

static char state_char(eTaskState s)
{
    switch (s) {
        case eRunning:   return 'R';
        case eReady:     return 'r';
        case eBlocked:   return 'B';
        case eSuspended: return 'S';
        case eDeleted:   return 'D';
        default:         return '?';
    }
}

void rtos_stats_emit_json(uint8_t node_id)
{
#if (configUSE_TRACE_FACILITY == 1)
    UBaseType_t n = uxTaskGetNumberOfTasks();
    if (n == 0 || n > 40) return;
    TaskStatus_t *arr = malloc(n * sizeof(TaskStatus_t));
    if (!arr) return;

    uint32_t total_rt = 0;
    UBaseType_t got = uxTaskGetSystemState(arr, n, &total_rt);
    if (total_rt == 0) total_rt = 1;

    /* %CPU tung core qua idle task cua moi core (nhan dien theo TEN: IDLE0/IDLE1) */
    uint32_t idle_rt[2] = { 0, 0 };
    for (UBaseType_t i = 0; i < got; i++) {
        if (strncmp(arr[i].pcTaskName, "IDLE", 4) == 0) {
            int c = (arr[i].pcTaskName[4] == '1') ? 1 : 0;   /* IDLE1 -> core1, con lai core0 */
            idle_rt[c] += arr[i].ulRunTimeCounter;
        }
    }
    uint32_t per_core = total_rt / 2;
    if (per_core == 0) per_core = 1;
    int c0 = 100 - (int)((uint64_t)idle_rt[0] * 100 / per_core);
    int c1 = 100 - (int)((uint64_t)idle_rt[1] * 100 / per_core);
    if (c0 < 0) {
        c0 = 0;
    }
    if (c0 > 100) {
        c0 = 100;
    }
    if (c1 < 0) {
        c1 = 0;
    }
    if (c1 > 100) {
        c1 = 100;
    }

    printf("{\"type\":\"node_tasks\",\"node\":%u,\"c0_cpu\":%d,\"c1_cpu\":%d,\"tasks\":[",
           (unsigned)node_id, c0, c1);
    for (UBaseType_t i = 0; i < got; i++) {
        uint32_t pct = (uint32_t)((uint64_t)arr[i].ulRunTimeCounter * 100 / total_rt);
        int core = -1;
#if (configTASKLIST_INCLUDE_COREID == 1)
        core = ((unsigned)arr[i].xCoreID < 2) ? (int)arr[i].xCoreID : -1;
#endif
        printf("%s{\"n\":\"%s\",\"cpu\":%u,\"core\":%d,\"stk\":%u,\"prio\":%u,\"st\":\"%c\"}",
               (i == 0) ? "" : ",",
               arr[i].pcTaskName, (unsigned)pct, core,
               (unsigned)arr[i].usStackHighWaterMark,
               (unsigned)arr[i].uxCurrentPriority, state_char(arr[i].eCurrentState));
    }
    printf("]}\n");
    fflush(stdout);
    free(arr);
#else
    (void)node_id;
#endif
}
