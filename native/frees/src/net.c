#include "net.h"
#include "frees.h"
#include "sync.h"
#include "arena.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define NET_MAX_EVENTS 4096
#define NET_LISTEN_BACKLOG 1024
#define NET_TAG_VALID 0xDEADBEEF
#define NET_IO_BATCH 32

typedef enum {
    NET_EV_READ,
    NET_EV_WRITE,
    NET_EV_ERROR
} net_ev_type_t;

typedef struct {
    int fd;
    void* user_data;
    net_handler_t handler;
    arena_t* conn_arena;
} net_conn_t;

struct net_ctx_t {
    uint32_t tag;
    int epoll_fd;
    int listen_fd;
    _Atomic bool running;
    sync_mutex_t conn_lock;
    net_conn_t* conns[NET_MAX_EVENTS];
    arena_t* net_arena;
    net_config_t config;
};

static int net_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int net_set_keepalive(int fd) {
    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    int keepcnt = 5;
    int keepidle = 30;
    int keepintvl = 5;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    return 0;
}

net_ctx_t* net_create(net_config_t config) {
    arena_t* arena = arena_create(1024 * 1024);
    if (!arena) return NULL;

    net_ctx_t* ctx = arena_alloc(arena, sizeof(net_ctx_t));
    memset(ctx, 0, sizeof(net_ctx_t));
    
    ctx->tag = NET_TAG_VALID;
    ctx->net_arena = arena;
    ctx->config = config;
    ctx->epoll_fd = epoll_create1(0);
    if (ctx->epoll_fd == -1) {
        arena_destroy(arena);
        return NULL;
    }

    sync_mutex_init(&ctx->conn_lock);
    atomic_init(&ctx->running, true);

    return ctx;
}

bool net_listen(net_ctx_t* ctx, uint16_t port) {
    if (!ctx || ctx->tag != NET_TAG_VALID) return false;

    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listen_fd == -1) return false;

    int opt = 1;
    setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(ctx->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(ctx->listen_fd);
        return false;
    }

    if (listen(ctx->listen_fd, NET_LISTEN_BACKLOG) < 0) {
        close(ctx->listen_fd);
        return false;
    }

    net_set_nonblocking(ctx->listen_fd);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = ctx->listen_fd;
    if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->listen_fd, &ev) == -1) {
        close(ctx->listen_fd);
        return false;
    }

    return true;
}

static void net_dispatch_task(void* arg) {
    net_conn_t* conn = (net_conn_t*)arg;
    if (conn && conn->handler) {
        conn->handler(conn->fd, conn->user_data, conn->conn_arena);
    }
}

void net_loop_run(net_ctx_t* ctx) {
    if (!ctx) return;

    struct epoll_event events[NET_MAX_EVENTS];
    while (atomic_load_explicit(&ctx->running, memory_order_relaxed)) {
        int nfds = epoll_wait(ctx->epoll_fd, events, NET_MAX_EVENTS, 100);
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == ctx->listen_fd) {
                while (true) {
                    struct sockaddr_in client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    int client_fd = accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addrlen);
                    
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        break;
                    }

                    net_set_nonblocking(client_fd);
                    net_set_keepalive(client_fd);

                    net_conn_t* conn = arena_alloc(ctx->net_arena, sizeof(net_conn_t));
                    conn->fd = client_fd;
                    conn->conn_arena = arena_create(ctx->config.conn_arena_size);
                    conn->handler = ctx->config.on_request;
                    conn->user_data = ctx->config.user_data;

                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    ev.data.ptr = conn;
                    epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            } else {
                net_conn_t* conn = (net_conn_t*)events[i].data.ptr;
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
                    close(conn->fd);
                    if (conn->conn_arena) arena_destroy(conn->conn_arena);
                } else if (events[i].events & EPOLLIN) {
                    frees_push(net_dispatch_task, conn);
                }
            }
        }
    }
}

ssize_t net_send_safe(int fd, const void* data, size_t len) {
    size_t total_sent = 0;
    const uint8_t* buf = (const uint8_t*)data;
    while (total_sent < len) {
        ssize_t sent = send(fd, buf + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        total_sent += sent;
    }
    return (ssize_t)total_sent;
}

ssize_t net_recv_all(int fd, void* buffer, size_t max_len) {
    ssize_t received = recv(fd, buffer, max_len, 0);
    if (received == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return received;
}

void net_stop(net_ctx_t* ctx) {
    if (ctx) atomic_store_explicit(&ctx->running, false, memory_order_release);
}

void net_destroy(net_ctx_t* ctx) {
    if (!ctx || ctx->tag != NET_TAG_VALID) return;
    
    net_stop(ctx);
    close(ctx->listen_fd);
    close(ctx->epoll_fd);
    
    arena_destroy(ctx->net_arena);
}

bool net_update_handler(net_ctx_t* ctx, int fd, net_handler_t new_handler) {
    if (!ctx) return false;
    sync_mutex_lock(&ctx->conn_lock);
    for (int i = 0; i < NET_MAX_EVENTS; i++) {
        if (ctx->conns[i] && ctx->conns[i]->fd == fd) {
            ctx->conns[i]->handler = new_handler;
            sync_mutex_unlock(&ctx->conn_lock);
            return true;
        }
    }
    sync_mutex_unlock(&ctx->conn_lock);
    return false;
}

void net_conn_close(net_ctx_t* ctx, net_conn_t* conn) {
    if (!ctx || !conn) return;
    epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    shutdown(conn->fd, SHUT_RDWR);
    close(conn->fd);
    if (conn->conn_arena) arena_destroy(conn->conn_arena);
}

int net_get_fd(net_ctx_t* ctx) {
    return ctx ? ctx->listen_fd : -1;
}

void net_set_timeout(int fd, uint32_t sec) {
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
}

bool net_is_active(net_ctx_t* ctx) {
    return ctx && atomic_load_explicit(&ctx->running, memory_order_acquire);
}

void net_broadcast(net_ctx_t* ctx, const void* data, size_t len) {
    if (!ctx) return;
    sync_mutex_lock(&ctx->conn_lock);
    for (int i = 0; i < NET_MAX_EVENTS; i++) {
        if (ctx->conns[i]) {
            net_send_safe(ctx->conns[i]->fd, data, len);
        }
    }
    sync_mutex_unlock(&ctx->conn_lock);
}

void net_stats(net_ctx_t* ctx, uint64_t* total_conns) {
    if (!ctx || !total_conns) return;
    uint64_t count = 0;
    sync_mutex_lock(&ctx->conn_lock);
    for (int i = 0; i < NET_MAX_EVENTS; i++) {
        if (ctx->conns[i]) count++;
    }
    sync_mutex_unlock(&ctx->conn_lock);
    *total_conns = count;
}