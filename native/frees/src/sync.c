#define _GNU_SOURCE
#include "sync.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#if defined(__i386__) || defined(__x86_64__)
    #include <immintrin.h>
    #define SYNC_PAUSE() _mm_pause()
#elif defined(__arm__) || defined(__aarch64__)
    #define SYNC_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define SYNC_PAUSE() do {} while (0)
#endif

void sync_mutex_init(sync_mutex_t* mutex) {
    atomic_init(&mutex->state, 0);
    mutex->recursion = 0;
    mutex->owner = 0;
}

bool sync_mutex_lock(sync_mutex_t* mutex) {
    pid_t tid = gettid();
    if (atomic_load_explicit(&mutex->owner, memory_order_relaxed) == tid) {
        mutex->recursion++;
        return true;
    }

    uint32_t expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&mutex->state, &expected, 1, 
           memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        SYNC_PAUSE();
    }
    
    atomic_store_explicit(&mutex->owner, tid, memory_order_relaxed);
    mutex->recursion = 1;
    return true;
}

bool sync_mutex_unlock(sync_mutex_t* mutex) {
    if (atomic_load_explicit(&mutex->owner, memory_order_relaxed) != gettid()) {
        return false;
    }

    if (--mutex->recursion == 0) {
        atomic_store_explicit(&mutex->owner, 0, memory_order_relaxed);
        atomic_store_explicit(&mutex->state, 0, memory_order_release);
    }
    return true;
}

void sync_barrier_init(sync_barrier_t* barrier, uint32_t count) {
    barrier->threshold = count;
    atomic_init(&barrier->count, count);
    atomic_init(&barrier->generation, 0);
}

bool sync_barrier_wait(sync_barrier_t* barrier) {
    uint32_t gen = atomic_load_explicit(&barrier->generation, memory_order_acquire);
    
    if (atomic_fetch_sub_explicit(&barrier->count, 1, memory_order_acq_rel) == 1) {
        atomic_store_explicit(&barrier->count, barrier->threshold, memory_order_relaxed);
        atomic_fetch_add_explicit(&barrier->generation, 1, memory_order_release);
        return true;
    }

    while (atomic_load_explicit(&barrier->generation, memory_order_acquire) == gen) {
        SYNC_PAUSE();
    }
    return false;
}

void sync_rwlock_init(sync_rwlock_t* rw) {
    atomic_init(&rw->readers, 0);
    atomic_init(&rw->writer, 0);
}

void sync_rwlock_rlock(sync_rwlock_t* rw) {
    while (1) {
        while (atomic_load_explicit(&rw->writer, memory_order_acquire)) {
            SYNC_PAUSE();
        }
        atomic_fetch_add_explicit(&rw->readers, 1, memory_order_acquire);
        if (!atomic_load_explicit(&rw->writer, memory_order_acquire)) {
            break;
        }
        atomic_fetch_sub_explicit(&rw->readers, 1, memory_order_relaxed);
    }
}

void sync_rwlock_runlock(sync_rwlock_t* rw) {
    atomic_fetch_sub_explicit(&rw->readers, 1, memory_order_release);
}

void sync_rwlock_wlock(sync_rwlock_t* rw) {
    uint32_t expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&rw->writer, &expected, 1,
           memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        SYNC_PAUSE();
    }
    while (atomic_load_explicit(&rw->readers, memory_order_acquire) > 0) {
        SYNC_PAUSE();
    }
}

void sync_rwlock_wunlock(sync_rwlock_t* rw) {
    atomic_store_explicit(&rw->writer, 0, memory_order_release);
}

void sync_event_init(sync_event_t* ev, bool manual) {
    atomic_init(&ev->state, 0);
    ev->manual_reset = manual;
}

void sync_event_set(sync_event_t* ev) {
    atomic_store_explicit(&ev->state, 1, memory_order_release);
}

void sync_event_reset(sync_event_t* ev) {
    atomic_store_explicit(&ev->state, 0, memory_order_release);
}

void sync_event_wait(sync_event_t* ev) {
    while (1) {
        if (atomic_load_explicit(&ev->state, memory_order_acquire)) {
            if (!ev->manual_reset) {
                uint32_t expected = 1;
                if (atomic_compare_exchange_strong_explicit(&ev->state, &expected, 0,
                    memory_order_acquire, memory_order_relaxed)) {
                    break;
                }
                continue;
            }
            break;
        }
        SYNC_PAUSE();
    }
}

void sync_sem_init(sync_sem_t* sem, uint32_t initial) {
    atomic_init(&sem->value, initial);
}

void sync_sem_post(sync_sem_t* sem) {
    atomic_fetch_add_explicit(&sem->value, 1, memory_order_release);
}

void sync_sem_wait(sync_sem_t* sem) {
    while (1) {
        uint32_t val = atomic_load_explicit(&sem->value, memory_order_acquire);
        if (val > 0) {
            if (atomic_compare_exchange_weak_explicit(&sem->value, &val, val - 1,
                memory_order_acquire, memory_order_relaxed)) {
                break;
            }
        }
        SYNC_PAUSE();
    }
}

void sync_waitgroup_init(sync_waitgroup_t* wg) {
    atomic_init(&wg->counter, 0);
}

void sync_waitgroup_add(sync_waitgroup_t* wg, int32_t delta) {
    atomic_fetch_add_explicit(&wg->counter, delta, memory_order_acq_rel);
}

void sync_waitgroup_done(sync_waitgroup_t* wg) {
    atomic_fetch_sub_explicit(&wg->counter, 1, memory_order_release);
}

void sync_waitgroup_wait(sync_waitgroup_t* wg) {
    while (atomic_load_explicit(&wg->counter, memory_order_acquire) > 0) {
        SYNC_PAUSE();
    }
}

bool sync_try_lock(sync_mutex_t* mutex) {
    uint32_t expected = 0;
    if (atomic_compare_exchange_strong_explicit(&mutex->state, &expected, 1,
        memory_order_acquire, memory_order_relaxed)) {
        atomic_store_explicit(&mutex->owner, gettid(), memory_order_relaxed);
        mutex->recursion = 1;
        return true;
    }
    return false;
}

uint32_t sync_sem_getvalue(sync_sem_t* sem) {
    return atomic_load_explicit(&sem->value, memory_order_relaxed);
}

void sync_spin_lock(_Atomic uint32_t* lock) {
    uint32_t expected = 0;
    while (!atomic_compare_exchange_weak_explicit(lock, &expected, 1,
           memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        SYNC_PAUSE();
    }
}

void sync_spin_unlock(_Atomic uint32_t* lock) {
    atomic_store_explicit(lock, 0, memory_order_release);
}

bool sync_event_is_set(sync_event_t* ev) {
    return atomic_load_explicit(&ev->state, memory_order_acquire) != 0;
}

void sync_barrier_destroy(sync_barrier_t* barrier) {
    memset(barrier, 0, sizeof(sync_barrier_t));
}

void sync_waitgroup_reset(sync_waitgroup_t* wg) {
    atomic_store_explicit(&wg->counter, 0, memory_order_release);
}

bool sync_mutex_is_locked(sync_mutex_t* mutex) {
    return atomic_load_explicit(&mutex->state, memory_order_relaxed) != 0;
}

void sync_yield_cpu() {
    SYNC_PAUSE();
    sched_yield();
}