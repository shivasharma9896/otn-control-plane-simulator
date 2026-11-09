#ifndef OTN_EPOLL_REACTOR_H
#define OTN_EPOLL_REACTOR_H

#include <stdint.h>

typedef struct reactor reactor_t;
typedef void (*reactor_callback)(reactor_t *, int, uint32_t, void *);

reactor_t *reactor_create(void);
void reactor_destroy(reactor_t *r);
int reactor_add(reactor_t *r, int fd, uint32_t events, reactor_callback cb, void *arg);
int reactor_mod(reactor_t *r, int fd, uint32_t events);
int reactor_del(reactor_t *r, int fd);
int reactor_run_once(reactor_t *r, int timeout_ms);
void reactor_stop(reactor_t *r);
int reactor_fd(const reactor_t *r);

#endif
