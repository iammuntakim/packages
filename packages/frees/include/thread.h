#ifndef THREAD_H
#define THREAD_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defines how much a thread should be prioritized by the OS scheduler */
typedef enum {
    THR_PRIO_LOW,      
    THR_PRIO_NORM,     
    THR_PRIO_HIGH,     
    THR_PRIO_CRITICAL  
} thr_prio_t;

/* Represents the current lifecycle stage of an individual worker thread */
typedef enum {
    THR_STATE_INIT,    
    THR_STATE_RUNNING, 
    THR_STATE_IDLE,    
    THR_STATE_STOPPING,
    THR_STATE_DEAD     
} thr_state_t;

/* A function pointer type that all worker threads must follow */
typedef void* (*thr_worker_t)(void*);

/* The core structure containing all metadata for a single managed thread */
typedef struct {
    pthread_t handle;
    pthread_attr_t attr;
    thr_worker_t func;
    void* user_data;
    thr_prio_t priority;
    uint32_t cpu_affinity;
    _Atomic thr_state_t state;
    _Atomic uint64_t cycle_count;
    _Atomic uint64_t last_active_ns;
} thr_unit_t;

/* A high-level container that manages an array of thread units as a pool */
typedef struct {
    thr_unit_t** pool;
    uint32_t capacity;
    _Atomic uint32_t active_count;
    _Atomic bool global_shutdown;
} thr_manager_t;

/* Allocates and sets up a new manager capable of holding a fixed number of threads */
thr_manager_t* thr_manager_create(uint32_t capacity);

/* Creates a new thread, configures its priority/CPU core, and starts execution immediately */
thr_unit_t* thr_unit_spawn(thr_manager_t* mgr, thr_worker_t func, void* arg, thr_prio_t prio, int core);

/* Dynamically changes which CPU core a running thread is restricted to use */
void thr_set_affinity(thr_unit_t* unit, int core);

/* Marks a thread for stopping; the thread function must check its state to exit */
bool thr_unit_stop(thr_unit_t* unit);

/* Blocks the calling thread until the specified worker thread has finished its work */
void thr_unit_join(thr_unit_t* unit);

/* Calculates how many nanoseconds have passed since the thread was last active */
uint64_t thr_get_runtime_ns(thr_unit_t* unit);

/* Shuts down every thread in the pool and frees all associated manager memory */
void thr_manager_destroy(thr_manager_t* mgr);

/* Voluntarily gives up the CPU so other threads have a chance to run */
void thr_util_yield();

/* Pauses the calling thread for a specific number of milliseconds */
void thr_util_sleep_ms(uint32_t ms);

/* Queries the operating system to find out how many CPU cores are available */
uint32_t thr_get_core_count();

/* Detaches a thread so it cleans up after itself without needing a join call */
void thr_unit_detach(thr_unit_t* unit);

/* Checks if the thread is currently in a state where it is still performing work */
bool thr_is_alive(thr_unit_t* unit);

/* Assigns a human-readable string name to the thread for easier debugging in tools like GDB */
void thr_set_name(thr_unit_t* unit, const char* name);

/* Loops through the manager and waits for every single thread to finish execution */
void thr_manager_wait_all(thr_manager_t* mgr);

/* Counts how many threads in the pool are currently alive and kicking */
size_t thr_manager_get_active_count(thr_manager_t* mgr);

/* Forcefully sends a signal to a thread to stop it, though use with extreme caution */
void thr_unit_cancel(thr_unit_t* unit);

/* High-precision busy-wait loop that delays execution for a few nanoseconds */
void thr_util_micro_delay(uint64_t ns);

/* Signals every thread in the manager to transition into the stopping state */
void thr_manager_broadcast_stop(thr_manager_t* mgr);

/* Scans the pool for threads that have finished and cleans up their resources */
void thr_manager_purge_dead(thr_manager_t* mgr);

/* Retrieves the current atomic state of a specific thread unit */
thr_state_t thr_unit_get_state(thr_unit_t* unit);

/* Issues a full hardware memory barrier to ensure instruction ordering */
void thr_util_heavy_barrier();

/* Returns true if the thread is configured with high or critical priority */
bool thr_unit_is_prio_boosted(thr_unit_t* unit);

/* Re-distributes threads across all available CPU cores to balance system load */
void thr_manager_rebalance_affinity(thr_manager_t* mgr);

/* Returns the total number of work cycles or iterations completed by this thread */
uint64_t thr_unit_get_cycles(thr_unit_t* unit);

/* Increases the maximum number of threads the manager is allowed to hold */
void thr_manager_resize(thr_manager_t* mgr, uint32_t new_capacity);

/* Returns a percentage representing how many threads are active vs total capacity */
double thr_manager_get_load(thr_manager_t* mgr);

/* Dynamically changes the OS priority level for a thread that is already running */
void thr_unit_set_prio(thr_unit_t* unit, thr_prio_t prio);

/* Enters a high-frequency spin loop until a specific atomic boolean matches a target value */
void thr_util_spin_wait(_Atomic bool* condition, bool target);

/* Executes a provided function for every thread unit currently inside the manager */
void thr_manager_foreach(thr_manager_t* mgr, void (*func)(thr_unit_t*, void*), void* user_data);

/* Returns the total slot count of the manager, regardless of how many are active */
uint32_t thr_manager_get_capacity(thr_manager_t* mgr);

#ifdef __cplusplus
}
#endif

#endif