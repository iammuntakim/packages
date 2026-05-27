#ifndef FREES_H
#define FREES_H

#include <stdint.h>
#include <stdbool.h>
#include "thread.h"
#include "sync.h"
#include "arena.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*frees_task_t)(void*);

/* --- Lifecycle Management --- */

void frees_init(uint32_t sleep_interval);
void frees_init_ext(uint32_t sleep_interval, uint32_t worker_count);
void frees_stop(void);
void frees_shutdown(void);

/* --- Task Injection (The Hot Path) --- */

bool frees_push(frees_task_t func, void* arg);

/* Pushes a task with an associated Arena. The service can reset/clean the arena after execution. */
bool frees_push_arena(frees_task_t func, void* arg, uint32_t group_id, sync_waitgroup_t* wg, arena_t* task_arena);

bool frees_push_sync(frees_task_t func, void* arg, uint32_t group_id, sync_waitgroup_t* wg);
bool frees_push_ext(frees_task_t func, void* arg, uint32_t group_id);
bool frees_push_urgent(frees_task_t func, void* arg);

/* --- Execution & Balancing --- */

void frees_service_run(void);
void frees_balance_work(uint32_t target_pending);

/* --- Integrated Arena Memory Management --- */

/* Allocates memory from the internal global service arena (Zero-lock path) */
void* frees_alloc(size_t size);
void* frees_calloc(size_t nmemb, size_t size);

/* Returns current memory pressure of the internal service arena */
void frees_arena_snapshot(size_t* used, size_t* allocated);

/* Clears the internal global arena history */
void frees_clear_history(void);

/* Triggers a defragmentation/trim of the service's memory chunks */
void frees_emergency_realloc(void);

/* --- Diagnostics & Telemetry --- */

uint64_t frees_pending(void);
uint64_t frees_total_done(void);
uint64_t frees_group_done(uint32_t group_id);
double frees_load_factor(void);
bool frees_is_healthy(void);

/* --- Configuration --- */

void frees_set_throttle(bool enable, uint32_t threshold);
void frees_set_sleep(uint32_t us);

/* --- High Volume Utilities --- */

void frees_util_burst(frees_task_t func, void** args, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif