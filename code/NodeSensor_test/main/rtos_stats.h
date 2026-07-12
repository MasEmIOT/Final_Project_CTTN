/*
 * rtos_stats.h - In ra danh sach task FreeRTOS + %CPU tung core (de check RTOS).
 * Yeu cau: CONFIG_FREERTOS_USE_TRACE_FACILITY + CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS.
 */
#pragma once
#include <stdint.h>

/* In 1 dong JSON: {"type":"node_tasks","node":id,"c0_cpu":..,"c1_cpu":..,
 *                  "tasks":[{"n":name,"cpu":pct,"core":id,"stk":free,"prio":p,"st":state}, ...]} */
void rtos_stats_emit_json(uint8_t node_id);
