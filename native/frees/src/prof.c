#include "prof.h"
#include "sync.h"
#include <time.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>

#define PROF_MAGIC 0x50524F46
#define PROF_BUCKETS 64
#define PROF_HISTORY_MAX 1024

typedef struct {
    _Atomic uint64_t count;
    _Atomic uint64_t total_cycles;
    _Atomic uint64_t min_cycles;
    _Atomic uint64_t max_cycles;
} prof_metric_t;

typedef struct {
    uint64_t start_tsc;
    uint64_t end_tsc;
    uint32_t event_id;
    uint32_t thread_id;
} prof_sample_t;

struct prof_ctx_t {
    uint32_t magic;
    _Atomic bool enabled;
    prof_metric_t metrics[PROF_BUCKETS];
    
    struct {
        prof_sample_t buffer[PROF_HISTORY_MAX];
        _Atomic uint64_t cursor;
    } history;

    sync_mutex_t lock;
    uint64_t start_wall_time;
};

static inline uint64_t prof_rdtsc(void) {
#if defined(__i386__) || defined(__x86_64__)
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r" (val));
    return val;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

prof_ctx_t* prof_init(void) {
    prof_ctx_t* ctx = mmap(NULL, sizeof(prof_ctx_t), PROT_READ | PROT_WRITE, 
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ctx == MAP_FAILED) return NULL;

    memset(ctx, 0, sizeof(prof_ctx_t));
    ctx->magic = PROF_MAGIC;
    atomic_init(&ctx->enabled, true);
    
    for (int i = 0; i < PROF_BUCKETS; i++) {
        atomic_init(&ctx->metrics[i].min_cycles, UINT64_MAX);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ctx->start_wall_time = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    
    sync_mutex_init(&ctx->lock);
    return ctx;
}

uint64_t prof_begin(prof_ctx_t* ctx) {
    if (!ctx || !atomic_load_explicit(&ctx->enabled, memory_order_relaxed)) return 0;
    return prof_rdtsc();
}

void prof_end(prof_ctx_t* ctx, uint32_t bucket_id, uint64_t start_tsc) {
    if (!ctx || bucket_id >= PROF_BUCKETS || start_tsc == 0) return;
    if (!atomic_load_explicit(&ctx->enabled, memory_order_relaxed)) return;

    uint64_t end_tsc = prof_rdtsc();
    uint64_t diff = (end_tsc > start_tsc) ? (end_tsc - start_tsc) : 1;

    prof_metric_t* m = &ctx->metrics[bucket_id];
    atomic_fetch_add_explicit(&m->count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&m->total_cycles, diff, memory_order_relaxed);

    uint64_t current_min = atomic_load_explicit(&m->min_cycles, memory_order_relaxed);
    while (diff < current_min && !atomic_compare_exchange_weak(&m->min_cycles, &current_min, diff));

    uint64_t current_max = atomic_load_explicit(&m->max_cycles, memory_order_relaxed);
    while (diff > current_max && !atomic_compare_exchange_weak(&m->max_cycles, &current_max, diff));

    uint64_t hist_idx = atomic_fetch_add_explicit(&ctx->history.cursor, 1, memory_order_relaxed) % PROF_HISTORY_MAX;
    ctx->history.buffer[hist_idx].start_tsc = start_tsc;
    ctx->history.buffer[hist_idx].end_tsc = end_tsc;
    ctx->history.buffer[hist_idx].event_id = bucket_id;
    ctx->history.buffer[hist_idx].thread_id = (uint32_t)pthread_self();
}

void prof_reset(prof_ctx_t* ctx) {
    if (!ctx) return;
    sync_mutex_lock(&ctx->lock);
    for (int i = 0; i < PROF_BUCKETS; i++) {
        atomic_store(&ctx->metrics[i].count, 0);
        atomic_store(&ctx->metrics[i].total_cycles, 0);
        atomic_store(&ctx->metrics[i].min_cycles, UINT64_MAX);
        atomic_store(&ctx->metrics[i].max_cycles, 0);
    }
    atomic_store(&ctx->history.cursor, 0);
    sync_mutex_unlock(&ctx->lock);
}

void prof_enable(prof_ctx_t* ctx, bool state) {
    if (ctx) atomic_store_explicit(&ctx->enabled, state, memory_order_release);
}

double prof_get_avg(prof_ctx_t* ctx, uint32_t bucket_id) {
    if (!ctx || bucket_id >= PROF_BUCKETS) return 0.0;
    uint64_t count = atomic_load_explicit(&ctx->metrics[bucket_id].count, memory_order_relaxed);
    if (count == 0) return 0.0;
    uint64_t total = atomic_load_explicit(&ctx->metrics[bucket_id].total_cycles, memory_order_relaxed);
    return (double)total / count;
}

uint64_t prof_get_count(prof_ctx_t* ctx, uint32_t bucket_id) {
    return (ctx && bucket_id < PROF_BUCKETS) ? 
        atomic_load_explicit(&ctx->metrics[bucket_id].count, memory_order_relaxed) : 0;
}

uint64_t prof_get_min(prof_ctx_t* ctx, uint32_t bucket_id) {
    if (!ctx || bucket_id >= PROF_BUCKETS) return 0;
    uint64_t val = atomic_load_explicit(&ctx->metrics[bucket_id].min_cycles, memory_order_relaxed);
    return (val == UINT64_MAX) ? 0 : val;
}

uint64_t prof_get_max(prof_ctx_t* ctx, uint32_t bucket_id) {
    return (ctx && bucket_id < PROF_BUCKETS) ? 
        atomic_load_explicit(&ctx->metrics[bucket_id].max_cycles, memory_order_relaxed) : 0;
}

void prof_shutdown(prof_ctx_t* ctx) {
    if (!ctx || ctx->magic != PROF_MAGIC) return;
    munmap(ctx, sizeof(prof_ctx_t));
}

void prof_get_snapshot(prof_ctx_t* ctx, uint32_t bucket_id, prof_snapshot_t* out) {
    if (!ctx || !out || bucket_id >= PROF_BUCKETS) return;
    out->bucket_id = bucket_id;
    out->count = prof_get_count(ctx, bucket_id);
    out->avg_cycles = prof_get_avg(ctx, bucket_id);
    out->min_cycles = prof_get_min(ctx, bucket_id);
    out->max_cycles = prof_get_max(ctx, bucket_id);
}

void prof_calibrate(prof_ctx_t* ctx) {
    if (!ctx) return;
    for (int i = 0; i < 100; i++) {
        uint64_t s = prof_begin(ctx);
        prof_end(ctx, 0, s);
    }
}

uint64_t prof_get_wall_ns(prof_ctx_t* ctx) {
    if (!ctx) return 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    return now - ctx->start_wall_time;
}

bool prof_is_hot(prof_ctx_t* ctx, uint32_t bucket_id, uint64_t threshold_cycles) {
    if (!ctx || bucket_id >= PROF_BUCKETS) return false;
    return prof_get_avg(ctx, bucket_id) > (double)threshold_cycles;
}

size_t prof_export_csv(prof_ctx_t* ctx, char* buffer, size_t size) {
    if (!ctx || !buffer || size < 256) return 0;
    size_t offset = 0;
    for (uint32_t i = 0; i < PROF_BUCKETS; i++) {
        uint64_t c = prof_get_count(ctx, i);
        if (c > 0) {
            int written = snprintf(buffer + offset, size - offset, 
                                   "%u,%lu,%.2f,%lu,%lu\n", 
                                   i, c, prof_get_avg(ctx, i), 
                                   prof_get_min(ctx, i), prof_get_max(ctx, i));
            if (written < 0 || (size_t)written >= size - offset) break;
            offset += written;
        }
    }
    return offset;
}