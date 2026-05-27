#define _GNU_SOURCE
#include "thread.h"
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#if defined(__i386__) || defined(__x86_64__)
    #include <immintrin.h>
    #define THR_PAUSE() _mm_pause()
#elif defined(__arm__) || defined(__aarch64__)
    #define THR_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define THR_PAUSE() do {} while (0)
#endif

static inline uint64_t thr_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void* thr_internal_wrapper(void* arg) {
    thr_unit_t* unit = (thr_unit_t*)arg;
    
    atomic_store_explicit(&unit->state, THR_STATE_RUNNING, memory_order_release);
    atomic_store_explicit(&unit->last_active_ns, thr_now_ns(), memory_order_relaxed);

    if (unit->cpu_affinity != (uint32_t)-1) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(unit->cpu_affinity, &cpuset);
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    }

    struct sched_param param;
    int policy = SCHED_OTHER;
    switch(unit->priority) {
        case THR_PRIO_LOW:      
            param.sched_priority = 0; 
            policy = SCHED_BATCH; 
            break;
        case THR_PRIO_NORM:     
            param.sched_priority = 0; 
            policy = SCHED_OTHER; 
            break;
        case THR_PRIO_HIGH:     
            param.sched_priority = 10; 
            policy = SCHED_RR; 
            break;
        case THR_PRIO_CRITICAL: 
            param.sched_priority = 50; 
            policy = SCHED_FIFO; 
            break;
    }
    
    if (unit->priority > THR_PRIO_NORM) {
        pthread_setschedparam(pthread_self(), policy, &param);
    }

    void* result = unit->func(unit->user_data);

    atomic_fetch_add_explicit(&unit->cycle_count, 1, memory_order_relaxed);
    atomic_store_explicit(&unit->state, THR_STATE_DEAD, memory_order_release);
    return result;
}

thr_manager_t* thr_manager_create(uint32_t capacity) {
    thr_manager_t* mgr = (thr_manager_t*)malloc(sizeof(thr_manager_t));
    if (!mgr) return NULL;

    mgr->pool = (thr_unit_t**)calloc(capacity, sizeof(thr_unit_t*));
    if (!mgr->pool) {
        free(mgr);
        return NULL;
    }
    mgr->capacity = capacity;
    atomic_init(&mgr->active_count, 0);
    atomic_init(&mgr->global_shutdown, false);

    return mgr;
}

thr_unit_t* thr_unit_spawn(thr_manager_t* mgr, thr_worker_t func, void* arg, thr_prio_t prio, int core) {
    if (!mgr || atomic_load_explicit(&mgr->global_shutdown, memory_order_relaxed)) return NULL;

    uint32_t idx = atomic_fetch_add_explicit(&mgr->active_count, 1, memory_order_acq_rel);
    if (idx >= mgr->capacity) {
        atomic_fetch_sub_explicit(&mgr->active_count, 1, memory_order_acq_rel);
        return NULL;
    }

    thr_unit_t* unit = (thr_unit_t*)malloc(sizeof(thr_unit_t));
    if (!unit) return NULL;
    memset(unit, 0, sizeof(thr_unit_t));

    unit->func = func;
    unit->user_data = arg;
    unit->priority = prio;
    unit->cpu_affinity = (core >= 0) ? (uint32_t)core : (uint32_t)-1;
    atomic_init(&unit->state, THR_STATE_INIT);
    atomic_init(&unit->cycle_count, 0);
    atomic_init(&unit->last_active_ns, 0);

    pthread_attr_init(&unit->attr);
    if (pthread_create(&unit->handle, &unit->attr, thr_internal_wrapper, unit) != 0) {
        pthread_attr_destroy(&unit->attr);
        free(unit);
        return NULL;
    }

    mgr->pool[idx] = unit;
    return unit;
}

void thr_set_affinity(thr_unit_t* unit, int core) {
    if (!unit) return;
    unit->cpu_affinity = core;
    if (atomic_load_explicit(&unit->state, memory_order_relaxed) == THR_STATE_RUNNING) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        sched_setaffinity(pthread_gettid_np(unit->handle), sizeof(cpu_set_t), &cpuset);
    }
}

bool thr_unit_stop(thr_unit_t* unit) {
    if (!unit) return false;
    thr_state_t expected = THR_STATE_RUNNING;
    return atomic_compare_exchange_strong(&unit->state, &expected, THR_STATE_STOPPING);
}

void thr_unit_join(thr_unit_t* unit) {
    if (!unit) return;
    pthread_join(unit->handle, NULL);
    pthread_attr_destroy(&unit->attr);
}

uint64_t thr_get_runtime_ns(thr_unit_t* unit) {
    if (!unit) return 0;
    uint64_t start = atomic_load_explicit(&unit->last_active_ns, memory_order_relaxed);
    if (start == 0) return 0;
    return thr_now_ns() - start;
}

void thr_manager_destroy(thr_manager_t* mgr) {
    if (!mgr) return;

    atomic_store_explicit(&mgr->global_shutdown, true, memory_order_release);
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);

    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i]) {
            thr_unit_stop(mgr->pool[i]);
            thr_unit_join(mgr->pool[i]);
            free(mgr->pool[i]);
        }
    }

    free(mgr->pool);
    free(mgr);
}

void thr_util_yield() {
    sched_yield();
}

void thr_util_sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

uint32_t thr_get_core_count() {
    return (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
}

void thr_unit_detach(thr_unit_t* unit) {
    if (!unit) return;
    pthread_detach(unit->handle);
}

bool thr_is_alive(thr_unit_t* unit) {
    if (!unit) return false;
    thr_state_t s = atomic_load_explicit(&unit->state, memory_order_acquire);
    return (s == THR_STATE_RUNNING || s == THR_STATE_IDLE);
}

void thr_set_name(thr_unit_t* unit, const char* name) {
    if (!unit || !name) return;
    pthread_setname_np(unit->handle, name);
}

void thr_manager_wait_all(thr_manager_t* mgr) {
    if (!mgr) return;
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);
    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i]) {
            thr_unit_join(mgr->pool[i]);
        }
    }
}

size_t thr_manager_get_active_count(thr_manager_t* mgr) {
    if (!mgr) return 0;
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);
    size_t live = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i] && thr_is_alive(mgr->pool[i])) {
            live++;
        }
    }
    return live;
}

void thr_unit_cancel(thr_unit_t* unit) {
    if (!unit) return;
    pthread_kill(unit->handle, SIGUSR1);
    atomic_store_explicit(&unit->state, THR_STATE_DEAD, memory_order_release);
}

void thr_util_micro_delay(uint64_t ns) {
    uint64_t start = thr_now_ns();
    while ((thr_now_ns() - start) < ns) {
        THR_PAUSE();
    }
}

void thr_manager_broadcast_stop(thr_manager_t* mgr) {
    if (!mgr) return;
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);
    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i]) thr_unit_stop(mgr->pool[i]);
    }
}

void thr_manager_purge_dead(thr_manager_t* mgr) {
    if (!mgr) return;
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);
    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i] && atomic_load_explicit(&mgr->pool[i]->state, memory_order_acquire) == THR_STATE_DEAD) {
            thr_unit_join(mgr->pool[i]);
            free(mgr->pool[i]);
            mgr->pool[i] = NULL;
        }
    }
}

thr_state_t thr_unit_get_state(thr_unit_t* unit) {
    if (!unit) return THR_STATE_DEAD;
    return atomic_load_explicit(&unit->state, memory_order_acquire);
}

void thr_util_heavy_barrier() {
    __sync_synchronize();
}

bool thr_unit_is_prio_boosted(thr_unit_t* unit) {
    if (!unit) return false;
    return unit->priority >= THR_PRIO_HIGH;
}

void thr_manager_rebalance_affinity(thr_manager_t* mgr) {
    if (!mgr) return;
    uint32_t cores = thr_get_core_count();
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);
    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i]) {
            thr_set_affinity(mgr->pool[i], i % cores);
        }
    }
}

uint64_t thr_unit_get_cycles(thr_unit_t* unit) {
    if (!unit) return 0;
    return atomic_load_explicit(&unit->cycle_count, memory_order_relaxed);
}

void thr_manager_resize(thr_manager_t* mgr, uint32_t new_capacity) {
    if (!mgr || new_capacity <= mgr->capacity) return;
    thr_unit_t** new_pool = (thr_unit_t**)realloc(mgr->pool, new_capacity * sizeof(thr_unit_t*));
    if (new_pool) {
        memset(new_pool + mgr->capacity, 0, (new_capacity - mgr->capacity) * sizeof(thr_unit_t*));
        mgr->pool = new_pool;
        mgr->capacity = new_capacity;
    }
}

double thr_manager_get_load(thr_manager_t* mgr) {
    if (!mgr || mgr->capacity == 0) return 0.0;
    return (double)thr_manager_get_active_count(mgr) / mgr->capacity;
}

void thr_unit_set_prio(thr_unit_t* unit, thr_prio_t prio) {
    if (!unit) return;
    unit->priority = prio;
    if (atomic_load_explicit(&unit->state, memory_order_relaxed) == THR_STATE_RUNNING) {
        struct sched_param param;
        int policy = SCHED_OTHER;
        switch(prio) {
            case THR_PRIO_LOW: param.sched_priority = 0; policy = SCHED_BATCH; break;
            case THR_PRIO_NORM: param.sched_priority = 0; policy = SCHED_OTHER; break;
            case THR_PRIO_HIGH: param.sched_priority = 10; policy = SCHED_RR; break;
            case THR_PRIO_CRITICAL: param.sched_priority = 50; policy = SCHED_FIFO; break;
        }
        pthread_setschedparam(unit->handle, policy, &param);
    }
}

void thr_util_spin_wait(_Atomic bool* condition, bool target) {
    while (atomic_load_explicit(condition, memory_order_acquire) != target) {
        THR_PAUSE();
    }
}

void thr_manager_foreach(thr_manager_t* mgr, void (*func)(thr_unit_t*, void*), void* user_data) {
    if (!mgr || !func) return;
    uint32_t count = atomic_load_explicit(&mgr->active_count, memory_order_acquire);
    for (uint32_t i = 0; i < count; i++) {
        if (mgr->pool[i]) func(mgr->pool[i], user_data);
    }
}

uint32_t thr_manager_get_capacity(thr_manager_t* mgr) {
    return mgr ? mgr->capacity : 0;
}