#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Recursive spin-mutex: Allows the same thread to lock multiple times without deadlocking */
typedef struct {
    _Atomic uint32_t state;
    _Atomic pid_t owner;
    uint32_t recursion;
} sync_mutex_t;

/* Reusable Barrier: Blocks a group of threads until a specific number have arrived */
typedef struct {
    uint32_t threshold;
    _Atomic uint32_t count;
    _Atomic uint32_t generation;
} sync_barrier_t;

/* Read-Write Lock: Allows many readers simultaneously, but only one exclusive writer */
typedef struct {
    _Atomic uint32_t readers;
    _Atomic uint32_t writer;
} sync_rwlock_t;

/* Event: A signal flag that can be set or reset; threads wait for it to become 'set' */
typedef struct {
    _Atomic uint32_t state;
    bool manual_reset;
} sync_event_t;

/* Lightweight Semaphore: A counter-based lock used to control access to limited resources */
typedef struct {
    _Atomic uint32_t value;
} sync_sem_t;

/* Wait Group: Used to wait for a collection of asynchronous tasks to finish */
typedef struct {
    _Atomic int32_t counter;
} sync_waitgroup_t;

/* Mutex Functions */
void sync_mutex_init(sync_mutex_t* mutex);
bool sync_mutex_lock(sync_mutex_t* mutex);
bool sync_try_lock(sync_mutex_t* mutex);
bool sync_mutex_unlock(sync_mutex_t* mutex);
bool sync_mutex_is_locked(sync_mutex_t* mutex);

/* Barrier Functions */
void sync_barrier_init(sync_barrier_t* barrier, uint32_t count);
bool sync_barrier_wait(sync_barrier_t* barrier);
void sync_barrier_destroy(sync_barrier_t* barrier);

/* RW Lock Functions */
void sync_rwlock_init(sync_rwlock_t* rw);
void sync_rwlock_rlock(sync_rwlock_t* rw);
void sync_rwlock_runlock(sync_rwlock_t* rw);
void sync_rwlock_wlock(sync_rwlock_t* rw);
void sync_rwlock_wunlock(sync_rwlock_t* rw);

/* Event Functions */
void sync_event_init(sync_event_t* ev, bool manual);
void sync_event_set(sync_event_t* ev);
void sync_event_reset(sync_event_t* ev);
void sync_event_wait(sync_event_t* ev);
bool sync_event_is_set(sync_event_t* ev);

/* Semaphore Functions */
void sync_sem_init(sync_sem_t* sem, uint32_t initial);
void sync_sem_post(sync_sem_t* sem);
void sync_sem_wait(sync_sem_t* sem);
uint32_t sync_sem_getvalue(sync_sem_t* sem);

/* WaitGroup Functions */
void sync_waitgroup_init(sync_waitgroup_t* wg);
void sync_waitgroup_add(sync_waitgroup_t* wg, int32_t delta);
void sync_waitgroup_done(sync_waitgroup_t* wg);
void sync_waitgroup_wait(sync_waitgroup_t* wg);
void sync_waitgroup_reset(sync_waitgroup_t* wg);

/* General Utilities */
void sync_spin_lock(_Atomic uint32_t* lock);
void sync_spin_unlock(_Atomic uint32_t* lock);
void sync_yield_cpu();

#ifdef __cplusplus
}
#endif

#endif