#define _GNU_SOURCE
#include "epoll_reactor.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/epoll.h>
#endif

typedef struct reactor_item {
    int fd;
    uint32_t events;
    reactor_callback cb;
    void *arg;
} reactor_item_t;

struct reactor {
    int fd;
    int stopped;
    reactor_item_t *items;
    size_t count;
    size_t capacity;
};

static reactor_item_t *find_item(reactor_t *r, int fd) {
    for (size_t i = 0; i < r->count; i++)
        if (r->items[i].fd == fd) return &r->items[i];
    return NULL;
}

reactor_t *reactor_create(void) {
    reactor_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
#ifdef __linux__
    r->fd = epoll_create1(EPOLL_CLOEXEC);
#else
    r->fd = -1;
#endif
    if (r->fd < 0) { free(r); return NULL; }
    return r;
}

void reactor_destroy(reactor_t *r) {
    if (!r) return;
    if (r->fd >= 0) close(r->fd);
    free(r->items);
    free(r);
}

int reactor_add(reactor_t *r, int fd, uint32_t events, reactor_callback cb, void *arg) {
    if (!r || fd < 0 || !cb || find_item(r, fd)) { errno = EINVAL; return -1; }
#ifdef __linux__
    if (r->count == r->capacity) {
        size_t cap = r->capacity ? r->capacity * 2U : 8U;
        reactor_item_t *p = realloc(r->items, cap * sizeof(*p));
        if (!p) return -1;
        r->items = p; r->capacity = cap;
    }
    struct epoll_event ev = {.events = events, .data.fd = fd};
    if (epoll_ctl(r->fd, EPOLL_CTL_ADD, fd, &ev) < 0) return -1;
    r->items[r->count++] = (reactor_item_t){fd, events, cb, arg};
    return 0;
#else
    (void)events; (void)arg; errno = ENOSYS; return -1;
#endif
}

int reactor_mod(reactor_t *r, int fd, uint32_t events) {
    reactor_item_t *item = r ? find_item(r, fd) : NULL;
    if (!item) { errno = ENOENT; return -1; }
#ifdef __linux__
    struct epoll_event ev = {.events = events, .data.fd = fd};
    if (epoll_ctl(r->fd, EPOLL_CTL_MOD, fd, &ev) < 0) return -1;
    item->events = events;
    return 0;
#else
    (void)events; errno = ENOSYS; return -1;
#endif
}

int reactor_del(reactor_t *r, int fd) {
    if (!r) { errno = EINVAL; return -1; }
#ifdef __linux__
    if (epoll_ctl(r->fd, EPOLL_CTL_DEL, fd, NULL) < 0 && errno != ENOENT) return -1;
    for (size_t i = 0; i < r->count; i++) if (r->items[i].fd == fd) {
        r->items[i] = r->items[--r->count]; return 0;
    }
    return 0;
#else
    (void)fd; errno = ENOSYS; return -1;
#endif
}

int reactor_run_once(reactor_t *r, int timeout_ms) {
#ifdef __linux__
    struct epoll_event events[32];
    int count = epoll_wait(r->fd, events, 32, timeout_ms);
    if (count < 0) return errno == EINTR ? -1 : -1;
    for (int i = 0; i < count; i++) {
        reactor_item_t *item = find_item(r, events[i].data.fd);
        if (item) item->cb(r, item->fd, events[i].events, item->arg);
    }
    return count;
#else
    (void)r; (void)timeout_ms; errno = ENOSYS; return -1;
#endif
}

void reactor_stop(reactor_t *r) { if (r) r->stopped = 1; }
int reactor_fd(const reactor_t *r) { return r ? r->fd : -1; }
