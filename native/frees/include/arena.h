#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arena_t arena_t;

/* Creates a massive memory playground; chunk_size is the size of each land plot (use 0 for default) */
arena_t* arena_create(size_t chunk_size);

/* Grabs a slice of memory from the arena; faster than a lightning strike because it just moves a pointer */
void* arena_alloc(arena_t* arena, size_t size);

/* Same as alloc, but paints the memory with zeros so you don't find old ghosts in your data */
void* arena_calloc(arena_t* arena, size_t nmemb, size_t size);

/* Stretches or shrinks an existing memory slice; usually just gives you a new slice and moves your data */
void* arena_realloc(arena_t* arena, void* old_ptr, size_t old_size, size_t new_size);

/* Aligns your memory slice to a specific power-of-two boundary; keeps the CPU hardware happy */
void* arena_alloc_aligned(arena_t* arena, size_t size, size_t alignment);

/* Teleports a string into the arena so it stays there as long as the arena lives */
char* arena_strdup(arena_t* arena, const char* s);

/* Copies a raw block of data into the arena's safe embrace */
void* arena_memdup(arena_t* arena, const void* data, size_t size);

/* Resets the pointer to the start; effectively "deletes" everything at once without actually working hard */
void arena_reset(arena_t* arena);

/* Destroys the entire arena and returns the memory back to the Operating System's cold hands */
void arena_destroy(arena_t* arena);

/* Asks the arena how much memory is currently being used by your greedy tasks */
size_t arena_get_used(arena_t* arena);

/* Asks the arena how much total memory it has reserved from the system */
size_t arena_get_allocated(arena_t* arena);

/* Scans the arena to see if it actually owns a specific pointer address */
bool arena_owns(arena_t* arena, void* ptr);

/* Sets up a custom panic function for when the arena finally runs out of space */
void arena_set_error_handler(arena_t* arena, void (*callback)(const char* msg));

/* Throws away empty memory plots that aren't being used to save system resources */
void arena_trim(arena_t* arena);

/* Provides a quick summary of used bytes, total bytes, and how many chunks are active */
void arena_summary(arena_t* arena, size_t* used, size_t* total, uint32_t* chunks);

/* Performs a deep reset if the arena has become too fragmented over time */
void arena_defrag_hint(arena_t* arena);

/* Checks how many bytes are left in the current active plot before a new one is needed */
size_t arena_available_in_current(arena_t* arena);

#ifdef __cplusplus
}
#endif

#endif