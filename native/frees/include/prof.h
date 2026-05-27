#ifndef PROF_H
#define PROF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for the profiling context */
typedef struct prof_ctx_t prof_ctx_t;

/* Holds a snapshot of performance data for a specific category */
typedef struct {
    uint32_t bucket_id;    /* The ID of the event being measured */
    uint64_t count;        /* Total number of times this event occurred */
    double avg_cycles;     /* Average CPU cycles per execution */
    uint64_t min_cycles;   /* The fastest execution recorded */
    uint64_t max_cycles;   /* The slowest execution recorded (outlier) */
} prof_snapshot_t;

/* --- Lifecycle --- */

/* Initializes the profiling system using anonymous mmap memory */
prof_ctx_t* prof_init(void);

/* Stops the profiler and releases all resources */
void prof_shutdown(prof_ctx_t* ctx);

/* Clears all current metrics to start a fresh measurement session */
void prof_reset(prof_ctx_t* ctx);

/* --- Instrumentation (The Hot Path) --- */

/* Captures the current CPU TSC value. Call this at the very start of a task. */
uint64_t prof_begin(prof_ctx_t* ctx);

/* Records the duration. bucket_id allows you to categorize different tasks.
 * Use bucket 0 for network, 1 for logic, 2 for DB, etc.
 */
void prof_end(prof_ctx_t* ctx, uint32_t bucket_id, uint64_t start_tsc);

/* --- Analytics & Reporting --- */

/* Globally enable or disable the profiler at runtime */
void prof_enable(prof_ctx_t* ctx, bool state);

/* Calculates the average cycles for a specific bucket */
double prof_get_avg(prof_ctx_t* ctx, uint32_t bucket_id);

/* Fills the out structure with all relevant data for a bucket */
void prof_get_snapshot(prof_ctx_t* ctx, uint32_t bucket_id, prof_snapshot_t* out);

/* Measures the "cost" of the profiler itself to subtract it from results */
void prof_calibrate(prof_ctx_t* ctx);

/* Checks if a specific task category is exceeding a cycle threshold */
bool prof_is_hot(prof_ctx_t* ctx, uint32_t bucket_id, uint64_t threshold_cycles);

/* Exports all recorded metrics into a CSV-formatted string for external logging */
size_t prof_export_csv(prof_ctx_t* ctx, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif