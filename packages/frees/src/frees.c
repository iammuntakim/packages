#include "arena.h"
#include "frees.h"
#include "thread.h"
#include "sync.h"
#include "net.h"
#include "prof.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include <stdalign.h>
#include <string.h>
#include <sys/mman.h>
#include <sched.h>
#include <stdio.h>

#if defined(__i386__) || defined(__x86_64__)
    #include <immintrin.h>
    #define FREES_PAUSE() _mm_pause()
#elif defined(__arm__) || defined(__aarch64__)
    #define FREES_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define FREES_PAUSE() do {} while (0)
#endif

#define FREES_QUEUE_SIZE 65536
#define FREES_MASK (FREES_QUEUE_SIZE - 1)
#define CACHE_LINE 64
#define BATCH_SIZE 64
#define MAX_GROUPS 16

typedef void (*frees_task_t)(void*);

typedef struct {
    frees_task_t func;
    void* arg;
    uint64_t timestamp;
    uint32_t group_id;
    sync_waitgroup_t* wg;
    arena_t* task_arena;
    uint64_t prof_start;
} frees_job_t;

typedef struct {
    alignas(CACHE_LINE) _Atomic uint64_t head;
    alignas(CACHE_LINE) _Atomic uint64_t tail;
    alignas(CACHE_LINE) _Atomic bool running;
    alignas(CACHE_LINE) frees_job_t slots[FREES_QUEUE_SIZE];
    
    sync_mutex_t global_lock;
    sync_rwlock_t stats_lock;
    sync_event_t work_available;
    sync_sem_t worker_sem;
    
    _Atomic uint32_t active_workers;
    _Atomic uint64_t tasks_completed;
    _Atomic uint64_t group_stats[MAX_GROUPS];
    
    uint32_t sleep_us;
    _Atomic bool throttle_enabled;
    uint32_t throttle_threshold;
    thr_manager_t* internal_mgr;
    arena_t* global_context_arena;
    prof_ctx_t* profiler;
    net_ctx_t* network;
} frees_ctx_t;

static frees_ctx_t* ctx = NULL;

void frees_service_run(void);

static inline uint64_t get_nanos() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void* frees_worker_wrapper(void* arg) {
    (void)arg;
    frees_service_run();
    return NULL;
}

void frees_init_ext(uint32_t sleep_interval, uint32_t worker_count) {
    if (ctx) return;
    
    arena_t* bootstrap_arena = arena_create(sizeof(frees_ctx_t) + (2 * 1024 * 1024));
    if (!bootstrap_arena) return;

    ctx = (frees_ctx_t*)arena_alloc_aligned(bootstrap_arena, sizeof(frees_ctx_t), CACHE_LINE);
    if (!ctx) {
        arena_destroy(bootstrap_arena);
        return;
    }

    memset(ctx, 0, sizeof(frees_ctx_t));
    ctx->global_context_arena = bootstrap_arena;
    
    atomic_init(&ctx->head, 0);
    atomic_init(&ctx->tail, 0);
    atomic_init(&ctx->running, true);
    atomic_init(&ctx->active_workers, 0);
    atomic_init(&ctx->tasks_completed, 0);
    atomic_init(&ctx->throttle_enabled, false);
    
    sync_mutex_init(&ctx->global_lock);
    sync_rwlock_init(&ctx->stats_lock);
    sync_event_init(&ctx->work_available, true);
    sync_sem_init(&ctx->worker_sem, worker_count);
    
    ctx->throttle_threshold = (uint32_t)(FREES_QUEUE_SIZE * 0.9);
    ctx->sleep_us = sleep_interval ? sleep_interval : 1;

    ctx->profiler = prof_init();

    if (worker_count > 0) {
        ctx->internal_mgr = thr_manager_create(worker_count);
        for (uint32_t i = 0; i < worker_count; i++) {
            thr_unit_spawn(ctx->internal_mgr, frees_worker_wrapper, NULL, THR_PRIO_NORM, i);
        }
    }
}

void frees_init(uint32_t sleep_interval) {
    frees_init_ext(sleep_interval, 0);
}

bool frees_push_arena(frees_task_t func, void* arg, uint32_t group_id, sync_waitgroup_t* wg, arena_t* task_arena) {
    if (!ctx) return false;
    
    sync_mutex_lock(&ctx->global_lock);
    
    uint64_t t = atomic_load_explicit(&ctx->tail, memory_order_relaxed);
    uint64_t h = atomic_load_explicit(&ctx->head, memory_order_acquire);
    
    if ((t - h) >= FREES_QUEUE_SIZE) {
        sync_mutex_unlock(&ctx->global_lock);
        return false;
    }
    
    if (atomic_load_explicit(&ctx->throttle_enabled, memory_order_relaxed)) {
        if ((t - h) > ctx->throttle_threshold) {
            sync_mutex_unlock(&ctx->global_lock);
            return false;
        }
    }
    
    uint64_t idx = t & FREES_MASK;
    ctx->slots[idx].func = func;
    ctx->slots[idx].arg = arg;
    ctx->slots[idx].group_id = group_id < MAX_GROUPS ? group_id : 0;
    ctx->slots[idx].timestamp = get_nanos();
    ctx->slots[idx].wg = wg;
    ctx->slots[idx].task_arena = task_arena;
    ctx->slots[idx].prof_start = prof_begin(ctx->profiler);
    
    if (wg) sync_waitgroup_add(wg, 1);
    
    atomic_store_explicit(&ctx->tail, t + 1, memory_order_release);
    sync_event_set(&ctx->work_available);
    
    sync_mutex_unlock(&ctx->global_lock);
    return true;
}

bool frees_push_sync(frees_task_t func, void* arg, uint32_t group_id, sync_waitgroup_t* wg) {
    return frees_push_arena(func, arg, group_id, wg, NULL);
}

bool frees_push_ext(frees_task_t func, void* arg, uint32_t group_id) {
    return frees_push_sync(func, arg, group_id, NULL);
}

bool frees_push(frees_task_t func, void* arg) {
    return frees_push_sync(func, arg, 0, NULL);
}

bool frees_push_urgent(frees_task_t func, void* arg) {
    if (!ctx) return false;
    
    sync_mutex_lock(&ctx->global_lock);
    
    uint64_t h = atomic_load_explicit(&ctx->head, memory_order_acquire);
    uint64_t new_h = h - 1;
    uint64_t idx = new_h & FREES_MASK;
    
    ctx->slots[idx].func = func;
    ctx->slots[idx].arg = arg;
    ctx->slots[idx].group_id = 0;
    ctx->slots[idx].timestamp = get_nanos();
    ctx->slots[idx].wg = NULL;
    ctx->slots[idx].task_arena = NULL;
    ctx->slots[idx].prof_start = prof_begin(ctx->profiler);
    
    atomic_store_explicit(&ctx->head, new_h, memory_order_release);
    sync_event_set(&ctx->work_available);
    
    sync_mutex_unlock(&ctx->global_lock);
    return true;
}

static inline void frees_exec_batch() {
    uint32_t local_stats[MAX_GROUPS] = {0};
    uint32_t count = 0;
    
    for (int i = 0; i < BATCH_SIZE; i++) {
        uint64_t h = atomic_load_explicit(&ctx->head, memory_order_relaxed);
        uint64_t t = atomic_load_explicit(&ctx->tail, memory_order_acquire);
        
        if (h == t) {
            sync_event_reset(&ctx->work_available);
            break;
        }
        
        frees_job_t job = ctx->slots[h & FREES_MASK];
        if (job.func) {
            job.func(job.arg);
            
            prof_end(ctx->profiler, job.group_id, job.prof_start);
            
            if (job.task_arena) {
                arena_reset(job.task_arena);
            }
            
            if (job.wg) sync_waitgroup_done(job.wg);
            local_stats[job.group_id]++;
            count++;
        }
        atomic_store_explicit(&ctx->head, h + 1, memory_order_release);
    }
    
    if (count > 0) {
        atomic_fetch_add_explicit(&ctx->tasks_completed, count, memory_order_relaxed);
        sync_rwlock_wlock(&ctx->stats_lock);
        for (int i = 0; i < MAX_GROUPS; i++) {
            if (local_stats[i] > 0) {
                atomic_fetch_add_explicit(&ctx->group_stats[i], local_stats[i], memory_order_relaxed);
            }
        }
        sync_rwlock_wunlock(&ctx->stats_lock);
    }
}

void frees_service_run() {
    if (!ctx) return;
    
    sync_sem_wait(&ctx->worker_sem);
    atomic_fetch_add_explicit(&ctx->active_workers, 1, memory_order_relaxed);
    
    while (atomic_load_explicit(&ctx->running, memory_order_relaxed)) {
        sync_event_wait(&ctx->work_available);
        
        uint64_t h = atomic_load_explicit(&ctx->head, memory_order_relaxed);
        uint64_t t = atomic_load_explicit(&ctx->tail, memory_order_acquire);
        
        if (h != t) {
            frees_exec_batch();
        } else {
            if (ctx->sleep_us > 0) {
                sync_yield_cpu();
                usleep(ctx->sleep_us);
            }
        }
    }
    
    atomic_fetch_sub_explicit(&ctx->active_workers, 1, memory_order_relaxed);
    sync_sem_post(&ctx->worker_sem);
}

uint64_t frees_pending() {
    if (!ctx) return 0;
    return atomic_load_explicit(&ctx->tail, memory_order_acquire) - 
           atomic_load_explicit(&ctx->head, memory_order_acquire);
}

uint64_t frees_total_done() {
    return ctx ? atomic_load_explicit(&ctx->tasks_completed, memory_order_relaxed) : 0;
}

uint64_t frees_group_done(uint32_t group_id) {
    if (!ctx || group_id >= MAX_GROUPS) return 0;
    uint64_t val;
    sync_rwlock_rlock(&ctx->stats_lock);
    val = atomic_load_explicit(&ctx->group_stats[group_id], memory_order_relaxed);
    sync_rwlock_runlock(&ctx->stats_lock);
    return val;
}

void frees_set_throttle(bool enable, uint32_t threshold) {
    if (!ctx) return;
    atomic_store_explicit(&ctx->throttle_enabled, enable, memory_order_relaxed);
    if (threshold > 0 && threshold < FREES_QUEUE_SIZE) {
        ctx->throttle_threshold = threshold;
    }
}

void frees_stop() {
    if (ctx) {
        atomic_store_explicit(&ctx->running, false, memory_order_relaxed);
        sync_event_set(&ctx->work_available);
    }
}

void frees_shutdown() {
    if (!ctx) return;
    frees_stop();
    if (ctx->internal_mgr) {
        thr_manager_destroy(ctx->internal_mgr);
    }
    while (atomic_load_explicit(&ctx->active_workers, memory_order_relaxed) > 0) {
        sync_yield_cpu();
        usleep(100);
    }
    
    if (ctx->network) {
        net_destroy(ctx->network);
    }
    
    if (ctx->profiler) {
        prof_shutdown(ctx->profiler);
    }
    
    arena_t* root = ctx->global_context_arena;
    arena_destroy(root);
    ctx = NULL;
}

void frees_util_burst(frees_task_t func, void** args, uint32_t count) {
    sync_waitgroup_t wg;
    sync_waitgroup_init(&wg);
    for (uint32_t i = 0; i < count; i++) {
        while (!frees_push_sync(func, args[i], 0, &wg)) {
            FREES_PAUSE();
        }
    }
    sync_waitgroup_wait(&wg);
}

double frees_load_factor() {
    if (!ctx) return 0.0;
    return (double)frees_pending() / FREES_QUEUE_SIZE;
}

bool frees_is_healthy() {
    if (!ctx) return false;
    return atomic_load_explicit(&ctx->running, memory_order_relaxed);
}

void frees_balance_work(uint32_t target_pending) {
    if (!ctx) return;
    uint64_t current = frees_pending();
    if (current > target_pending) {
        uint32_t diff = (uint32_t)(current - target_pending);
        uint32_t batches = (diff / BATCH_SIZE) + 1;
        for(uint32_t i = 0; i < batches; i++) {
            frees_exec_batch();
        }
    }
}

void* frees_alloc(size_t size) {
    if (!ctx || !ctx->global_context_arena) return NULL;
    return arena_alloc(ctx->global_context_arena, size);
}

void* frees_calloc(size_t nmemb, size_t size) {
    if (!ctx || !ctx->global_context_arena) return NULL;
    return arena_calloc(ctx->global_context_arena, nmemb, size);
}

void frees_arena_snapshot(size_t* used, size_t* allocated) {
    if (!ctx || !ctx->global_context_arena) return;
    if (used) *used = arena_get_used(ctx->global_context_arena);
    if (allocated) *allocated = arena_get_allocated(ctx->global_context_arena);
}

void frees_clear_history() {
    if (!ctx || !ctx->global_context_arena) return;
    arena_reset(ctx->global_context_arena);
}

void frees_emergency_realloc() {
    if (!ctx || !ctx->global_context_arena) return;
    arena_defrag_hint(ctx->global_context_arena);
}

void frees_setup_net(const char* host, uint16_t port) {
    if (!ctx) return;
    net_config_t cfg = {
        .on_request = NULL,
        .conn_arena_size = 65536
    };
    ctx->network = net_create(cfg);
    net_listen(ctx->network, port);
}

void frees_net_poll() {
    if (ctx && ctx->network) {
        net_loop_run(ctx->network);
    }
}