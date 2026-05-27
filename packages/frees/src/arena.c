#include "arena.h"
#include "sync.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ARENA_DEFAULT_CHUNK_SIZE (1024 * 1024 * 8)
#define ARENA_ALIGNMENT 16
#define ARENA_TAG_VALID 0xACE0BA5E
#define ARENA_MAX_HISTORY 32

typedef struct arena_chunk {
    uint8_t* base;
    size_t offset;
    size_t capacity;
    struct arena_chunk* next;
} arena_chunk_t;

struct arena_t {
    uint32_t tag;
    sync_mutex_t lock;
    arena_chunk_t* current;
    arena_chunk_t* chunks;
    size_t chunk_size;
    size_t total_allocated;
    size_t total_used;
    
    struct {
        void* ptrs[ARENA_MAX_HISTORY];
        size_t sizes[ARENA_MAX_HISTORY];
        uint32_t count;
    } history;

    void (*error_callback)(const char* msg);
};

static arena_chunk_t* arena_new_chunk(size_t size) {
    size_t page_size = sysconf(_SC_PAGESIZE);
    size = (size + page_size - 1) & ~(page_size - 1);

    arena_chunk_t* chunk = mmap(NULL, sizeof(arena_chunk_t), PROT_READ | PROT_WRITE, 
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (chunk == MAP_FAILED) return NULL;

    chunk->base = mmap(NULL, size, PROT_READ | PROT_WRITE, 
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (chunk->base == MAP_FAILED) {
        munmap(chunk, sizeof(arena_chunk_t));
        return NULL;
    }

    chunk->capacity = size;
    chunk->offset = 0;
    chunk->next = NULL;
    return chunk;
}

arena_t* arena_create(size_t chunk_size) {
    arena_t* arena = mmap(NULL, sizeof(arena_t), PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) return NULL;

    memset(arena, 0, sizeof(arena_t));
    arena->tag = ARENA_TAG_VALID;
    arena->chunk_size = chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE;
    
    sync_mutex_init(&arena->lock);
    
    arena->chunks = arena_new_chunk(arena->chunk_size);
    if (!arena->chunks) {
        munmap(arena, sizeof(arena_t));
        return NULL;
    }
    
    arena->current = arena->chunks;
    arena->total_allocated = arena->chunks->capacity;
    
    return arena;
}

static inline size_t align_up(size_t size, size_t align) {
    return (size + align - 1) & ~(align - 1);
}

void* arena_alloc(arena_t* arena, size_t size) {
    if (!arena || arena->tag != ARENA_TAG_VALID) return NULL;

    size = align_up(size, ARENA_ALIGNMENT);
    void* result = NULL;

    sync_mutex_lock(&arena->lock);

    if (arena->current->offset + size > arena->current->capacity) {
        size_t next_size = size > arena->chunk_size ? size : arena->chunk_size;
        arena_chunk_t* chunk = arena_new_chunk(next_size);
        
        if (!chunk) {
            if (arena->error_callback) arena->error_callback("ERR_OOM");
            sync_mutex_unlock(&arena->lock);
            return NULL;
        }

        chunk->next = arena->chunks;
        arena->chunks = chunk;
        arena->current = chunk;
        arena->total_allocated += chunk->capacity;
    }

    result = arena->current->base + arena->current->offset;
    arena->current->offset += size;
    arena->total_used += size;

    uint32_t idx = arena->history.count % ARENA_MAX_HISTORY;
    arena->history.ptrs[idx] = result;
    arena->history.sizes[idx] = size;
    arena->history.count++;

    sync_mutex_unlock(&arena->lock);
    return result;
}

void* arena_calloc(arena_t* arena, size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = arena_alloc(arena, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* arena_realloc(arena_t* arena, void* old_ptr, size_t old_size, size_t new_size) {
    if (new_size <= old_size) return old_ptr;
    
    void* new_ptr = arena_alloc(arena, new_size);
    if (new_ptr && old_ptr) {
        memcpy(new_ptr, old_ptr, old_size);
    }
    return new_ptr;
}

void arena_reset(arena_t* arena) {
    if (!arena || arena->tag != ARENA_TAG_VALID) return;

    sync_mutex_lock(&arena->lock);
    
    arena_chunk_t* curr = arena->chunks;
    while (curr) {
        curr->offset = 0;
        curr = curr->next;
    }
    
    arena->current = arena->chunks;
    arena->total_used = 0;
    arena->history.count = 0;
    
    sync_mutex_unlock(&arena->lock);
}

void arena_destroy(arena_t* arena) {
    if (!arena || arena->tag != ARENA_TAG_VALID) return;

    arena_chunk_t* curr = arena->chunks;
    while (curr) {
        arena_chunk_t* next = curr->next;
        munmap(curr->base, curr->capacity);
        munmap(curr, sizeof(arena_chunk_t));
        curr = next;
    }

    arena->tag = 0;
    munmap(arena, sizeof(arena_t));
}

void arena_set_error_handler(arena_t* arena, void (*callback)(const char*)) {
    if (arena) arena->error_callback = callback;
}

size_t arena_get_used(arena_t* arena) {
    return arena ? arena->total_used : 0;
}

size_t arena_get_allocated(arena_t* arena) {
    return arena ? arena->total_allocated : 0;
}

char* arena_strdup(arena_t* arena, const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* dest = arena_alloc(arena, len);
    if (dest) memcpy(dest, s, len);
    return dest;
}

void* arena_memdup(arena_t* arena, const void* data, size_t size) {
    void* dest = arena_alloc(arena, size);
    if (dest) memcpy(dest, data, size);
    return dest;
}

bool arena_owns(arena_t* arena, void* ptr) {
    if (!arena || !ptr) return false;
    
    sync_mutex_lock(&arena->lock);
    arena_chunk_t* curr = arena->chunks;
    while (curr) {
        if ((uint8_t*)ptr >= curr->base && (uint8_t*)ptr < (curr->base + curr->capacity)) {
            sync_mutex_unlock(&arena->lock);
            return true;
        }
        curr = curr->next;
    }
    sync_mutex_unlock(&arena->lock);
    return false;
}

void arena_trim(arena_t* arena) {
    if (!arena) return;
    sync_mutex_lock(&arena->lock);
    
    arena_chunk_t** prev = &arena->chunks;
    arena_chunk_t* curr = arena->chunks;
    
    while (curr) {
        if (curr != arena->current && curr->offset == 0) {
            *prev = curr->next;
            arena_chunk_t* to_free = curr;
            curr = curr->next;
            arena->total_allocated -= to_free->capacity;
            munmap(to_free->base, to_free->capacity);
            munmap(to_free, sizeof(arena_chunk_t));
        } else {
            prev = &curr->next;
            curr = curr->next;
        }
    }
    sync_mutex_unlock(&arena->lock);
}

void* arena_alloc_aligned(arena_t* arena, size_t size, size_t alignment) {
    if (!arena || arena->tag != ARENA_TAG_VALID) return NULL;
    
    sync_mutex_lock(&arena->lock);
    
    size_t current_addr = (size_t)(arena->current->base + arena->current->offset);
    size_t aligned_addr = (current_addr + alignment - 1) & ~(alignment - 1);
    size_t padding = aligned_addr - current_addr;
    
    if (arena->current->offset + size + padding > arena->current->capacity) {
        sync_mutex_unlock(&arena->lock);
        void* ptr = arena_alloc(arena, size + alignment);
        if (!ptr) return NULL;
        return (void*)(((size_t)ptr + alignment - 1) & ~(alignment - 1));
    }
    
    arena->current->offset += padding;
    void* result = arena->current->base + arena->current->offset;
    arena->current->offset += size;
    arena->total_used += (size + padding);
    
    sync_mutex_unlock(&arena->lock);
    return result;
}

void arena_summary(arena_t* arena, size_t* used, size_t* total, uint32_t* chunks) {
    if (!arena) return;
    sync_mutex_lock(&arena->lock);
    if (used) *used = arena->total_used;
    if (total) *total = arena->total_allocated;
    if (chunks) {
        uint32_t count = 0;
        arena_chunk_t* c = arena->chunks;
        while(c) { count++; c = c->next; }
        *chunks = count;
    }
    sync_mutex_unlock(&arena->lock);
}

void arena_defrag_hint(arena_t* arena) {
    if (!arena) return;
    sync_mutex_lock(&arena->lock);
    if (arena->total_used < arena->total_allocated / 4 && arena->total_allocated > arena->chunk_size * 2) {
        arena_chunk_t* new_root = arena_new_chunk(arena->chunk_size);
        if (new_root) {
            arena_chunk_t* old = arena->chunks;
            while (old) {
                arena_chunk_t* next = old->next;
                munmap(old->base, old->capacity);
                munmap(old, sizeof(arena_chunk_t));
                old = next;
            }
            arena->chunks = new_root;
            arena->current = new_root;
            arena->total_allocated = new_root->capacity;
            arena->total_used = 0;
        }
    }
    sync_mutex_unlock(&arena->lock);
}

void* arena_steal(arena_t* dest, arena_t* src, void* ptr, size_t size) {
    if (!dest || !src || !ptr) return NULL;
    void* new_ptr = arena_alloc(dest, size);
    if (new_ptr) memcpy(new_ptr, ptr, size);
    return new_ptr;
}

size_t arena_available_in_current(arena_t* arena) {
    if (!arena) return 0;
    sync_mutex_lock(&arena->lock);
    size_t res = arena->current->capacity - arena->current->offset;
    sync_mutex_unlock(&arena->lock);
    return res;
}