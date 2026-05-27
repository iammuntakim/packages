#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle for the network context */
typedef struct net_ctx_t net_ctx_t;

/* Callback signature for handling a single connection.
 * fd: The socket file descriptor.
 * user_data: Global context passed during init.
 * conn_arena: A dedicated arena for this specific connection's memory needs.
 */
typedef void (*net_handler_t)(int fd, void* user_data, arena_t* conn_arena);

typedef struct {
    net_handler_t on_request;   /* The function called when data arrives */
    void* user_data;            /* Global context pointer */
    size_t conn_arena_size;     /* Memory pre-allocated for every new connection */
} net_config_t;

/* --- Lifecycle --- */

/* Creates a network context but does not start listening yet */
net_ctx_t* net_create(net_config_t config);

/* Binds to a port and starts the internal epoll reactor */
bool net_listen(net_ctx_t* ctx, uint16_t port);

/* The main loop. Should usually be called in its own thread or main thread.
 * It monitors sockets and pushes ready events to the frees_service.
 */
void net_loop_run(net_ctx_t* ctx);

/* Signals the loop to stop and close all active descriptors */
void net_stop(net_ctx_t* ctx);

/* Fully destroys the context and releases all network-related memory */
void net_destroy(net_ctx_t* ctx);

/* --- Socket Utilities --- */

/* Non-blocking write that ensures all bytes are sent or error is returned */
ssize_t net_send_safe(int fd, const void* data, size_t len);

/* Non-blocking read into a buffer */
ssize_t net_recv_all(int fd, void* buffer, size_t max_len);

/* Sets read/write timeouts for a socket (safety mechanism) */
void net_set_timeout(int fd, uint32_t sec);

/* Sends data to every currently connected client (useful for pub/sub) */
void net_broadcast(net_ctx_t* ctx, const void* data, size_t len);

/* Returns the current number of active TCP connections */
void net_stats(net_ctx_t* ctx, uint64_t* total_conns);

#ifdef __cplusplus
}
#endif

#endif