#define _GNU_SOURCE
#include "node_daemon.h"
#include "control_channel.h"
#include "discovery.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#else
#define EPOLLIN 0x001U
#define EPOLLOUT 0x004U
#define EPOLLERR 0x008U
#define EPOLLHUP 0x010U
#define EPOLLRDHUP 0x2000U
#endif
#include <time.h>
#include <unistd.h>

#define MAX_PEERS 64
#define DEFAULT_HEARTBEAT_MS 1000U
#define DEFAULT_TIMEOUT_MS 5000U
#define DEFAULT_RECONNECT_BASE_MS 500U
#define DEFAULT_RECONNECT_MAX_MS 30000U
#define DEFAULT_MAX_PEERS 64U

typedef struct {
    node_daemon_t *owner;
    node_peer_info_t info;
    control_channel_t channel;
    int fd;
    char address[INET_ADDRSTRLEN];
} peer_t;

struct node_daemon {
    uint32_t id; uint16_t port, discovery_port;
    int listen_fd, timer_fd, running;
    reactor_t *reactor; discovery_t *discovery;
    peer_t peers[MAX_PEERS]; size_t peer_count;
    uint64_t heartbeats, messages, state_generation;
    node_daemon_config_t config;
};

static uint64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}
void node_daemon_config_defaults(node_daemon_config_t *config) {
    if (!config) return;
    *config = (node_daemon_config_t){
        .node_id = 0, .control_port = 19091, .discovery_port = 19090,
        .heartbeat_interval_ms = DEFAULT_HEARTBEAT_MS,
        .heartbeat_timeout_ms = DEFAULT_TIMEOUT_MS,
        .reconnect_base_ms = DEFAULT_RECONNECT_BASE_MS,
        .reconnect_max_ms = DEFAULT_RECONNECT_MAX_MS,
        .max_peers = DEFAULT_MAX_PEERS,
        .bind_address = "0.0.0.0",
        .discovery_address = "255.255.255.255",
        .log_level = LOG_INFO
    };
}
static int nonblock(int fd) { int f = fcntl(fd, F_GETFL, 0); return f < 0 ? -1 : fcntl(fd, F_SETFL, f | O_NONBLOCK); }
static peer_t *peer_by_id(node_daemon_t *n, uint32_t id) { for (size_t i=0;i<n->peer_count;i++) if(n->peers[i].info.id==id)return &n->peers[i]; return NULL; }
int node_peer_transition(node_peer_info_t *peer, peer_state_t state) {
    if (!peer || state < PEER_STATE_DISCOVERED || state > PEER_STATE_FAILED) {
        errno = EINVAL;
        return -1;
    }
    peer->state = state;
    peer->connected = state == PEER_STATE_CONNECTING ||
                      state == PEER_STATE_ESTABLISHED;
    return 0;
}
unsigned node_reconnect_delay_ms(unsigned base_ms, unsigned max_ms,
                                 unsigned attempt) {
    if (base_ms == 0U || max_ms == 0U) return 0U;
    unsigned delay = base_ms;
    for (unsigned i = 1U; i < attempt && delay < max_ms; i++) {
        if (delay > max_ms / 2U) return max_ms;
        delay *= 2U;
    }
    return delay > max_ms ? max_ms : delay;
}
static void peer_mark_failed(node_daemon_t *n, peer_t *p) {
    if (p->fd >= 0) {
        reactor_del(n->reactor, p->fd);
        control_channel_close(&p->channel);
        p->fd = -1;
    }
    (void)node_peer_transition(&p->info, PEER_STATE_FAILED);
    if (p->info.reconnect_attempt < 31U) p->info.reconnect_attempt++;
    uint64_t delay = node_reconnect_delay_ms(n->config.reconnect_base_ms,
                                             n->config.reconnect_max_ms,
                                             p->info.reconnect_attempt);
    p->info.next_retry_ms = now_ms() + delay;
    LOGW("peer", "node=%u state=FAILED retry_in_ms=%llu attempt=%u",
         p->info.id, (unsigned long long)delay, p->info.reconnect_attempt);
}
static void peer_remove(node_daemon_t *n, peer_t *p) {
    peer_mark_failed(n, p);
    *p = n->peers[--n->peer_count];
}
static void peer_event(reactor_t *r, int fd, uint32_t events, void *arg);
static void command_event(reactor_t *r, int fd, uint32_t events, void *arg);
static void heartbeat_event(uint32_t peer_id, const char *ip, int pong, void *arg);

static int connect_peer(node_daemon_t *n, const discovery_peer_t *d, const char *ip) {
    if (d->node_id == n->id) return 0;
    peer_t *p = peer_by_id(n, d->node_id);
    if (p && p->info.state != PEER_STATE_FAILED &&
        p->info.state != PEER_STATE_DISCOVERED) return 0;
    if (p && p->info.state == PEER_STATE_FAILED && now_ms() < p->info.next_retry_ms) return 0;
    if (!p && n->peer_count >= n->config.max_peers) {
        LOGW("peer", "peer capacity reached max_peers=%u", n->config.max_peers);
        return 0;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return -1; if (nonblock(fd) < 0) { close(fd); return -1; }
    struct sockaddr_in a = {.sin_family=AF_INET,.sin_port=htons(d->control_port)};
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) { close(fd); return -1; }
    int rc = connect(fd, (struct sockaddr *)&a, sizeof(a));
    if (rc < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (!p) {
        p = &n->peers[n->peer_count++];
        memset(p, 0, sizeof(*p));
        p->owner = n;
        p->info.id = d->node_id;
    }
    p->fd = fd;
    p->info.port = d->control_port;
    p->info.last_seen_ms = now_ms();
    (void)node_peer_transition(&p->info, PEER_STATE_CONNECTING);
    strncpy(p->address,ip,sizeof(p->address)-1);
    control_channel_init(&p->channel, fd, d->node_id);
    uint32_t wire_id = htonl(n->id);
    control_queue(&p->channel, CONTROL_HELLO, &wire_id, sizeof(wire_id));
    if (reactor_add(n->reactor,fd,EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR,peer_event,p)<0) {
        peer_mark_failed(n, p);
        return -1;
    }
    return 0;
}
static void discovery_event(reactor_t *r, int fd, uint32_t ev, void *arg) { (void)r;(void)fd;(void)ev; node_daemon_t *n=arg; discovery_receive(n->discovery); }
static void discovery_peer(const discovery_peer_t *p, const char *ip, void *arg) { node_daemon_t *n=arg; connect_peer(n,p,ip); }
static void heartbeat_event(uint32_t peer_id, const char *ip, int pong, void *arg) {
    (void)ip;
    node_daemon_t *n = arg;
    peer_t *p = peer_by_id(n, peer_id);
    if (p && pong) {
        p->info.last_seen_ms = now_ms();
        if (p->info.state == PEER_STATE_FAILED) {
            (void)node_peer_transition(&p->info, PEER_STATE_DISCOVERED);
            p->info.next_retry_ms = 0;
            LOGI("peer", "node=%u recovered via UDP PONG", peer_id);
        }
    }
}
static void listener_event(reactor_t *r, int fd, uint32_t ev, void *arg) {
    (void)ev; node_daemon_t *n=arg;
    for (;;) {
        struct sockaddr_in a; socklen_t l=sizeof(a); int c=accept(fd,(struct sockaddr*)&a,&l);
        if(c<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return; if(errno==EINTR)continue;return;}
        if(nonblock(c)<0 || n->peer_count>=n->config.max_peers){close(c);continue;}
        peer_t *p=&n->peers[n->peer_count++]; memset(p,0,sizeof(*p)); p->owner=n; p->fd=c;p->info.port=ntohs(a.sin_port);p->info.last_seen_ms=now_ms();(void)node_peer_transition(&p->info, PEER_STATE_CONNECTING);inet_ntop(AF_INET,&a.sin_addr,p->address,sizeof(p->address));control_channel_init(&p->channel,c,0);if(reactor_add(r,c,EPOLLIN|EPOLLRDHUP|EPOLLERR,peer_event,p)<0){peer_mark_failed(n,p);}
    }
}
static void timer_event(reactor_t *r, int fd, uint32_t ev, void *arg) {
    (void)r;(void)ev; node_daemon_t*n=arg; uint64_t x; if(read(fd,&x,sizeof(x))<0)return; n->heartbeats++;
    discovery_send(n->discovery); discovery_send_ping(n->discovery); n->state_generation++; uint64_t t=now_ms();
    for(size_t i=0;i<n->peer_count;) {
        peer_t*p=&n->peers[i];
        if(p->info.state != PEER_STATE_FAILED &&
           t-p->info.last_seen_ms > n->config.heartbeat_timeout_ms) peer_mark_failed(n,p);
        i++;
    }
}
static void peer_event(reactor_t *r,int fd,uint32_t events,void*arg) {
    peer_t*p=arg; node_daemon_t*n=p->owner;
    if(events&(EPOLLERR|EPOLLRDHUP|EPOLLHUP)){ peer_mark_failed(n,p); return; }
    uint8_t payload[CONTROL_MAX_PAYLOAD]; control_header_t h; int ready;
    if(events&EPOLLIN) {
        for(;;){int rc=control_read(&p->channel,&h,payload,sizeof(payload),&ready);if(rc==-2||rc<0){peer_mark_failed(n,p);return;}if(!ready)break;p->info.last_seen_ms=now_ms();p->info.received++;n->messages++;if(h.type==CONTROL_HELLO&&h.length==4){uint32_t id;memcpy(&id,payload,4);p->info.id=ntohl(id);(void)node_peer_transition(&p->info, PEER_STATE_ESTABLISHED);peer_t *other=peer_by_id(n,p->info.id);if(other&&other!=p){if(other->fd<p->fd){peer_remove(n,p);return;}peer_remove(n,other);}uint32_t generation=htonl((uint32_t)n->state_generation);if(control_queue(&p->channel,CONTROL_STATE,&generation,sizeof(generation))==0)reactor_mod(r,p->fd,EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLERR);}else if(h.type==CONTROL_STATE&&h.length==4){uint32_t generation;memcpy(&generation,payload,4);if(ntohl(generation)>n->state_generation)n->state_generation=ntohl(generation);} }
    }
    if(events&EPOLLOUT){int rc=control_flush(&p->channel);if(rc<0){peer_mark_failed(n,p);return;}p->info.sent++;}
    if(p->fd>=0) reactor_mod(r,fd,EPOLLIN|(control_wants_write(&p->channel)?EPOLLOUT:0)|EPOLLRDHUP|EPOLLERR);
}
static void command_event(reactor_t *r, int fd, uint32_t events, void *arg) {
    (void)r; (void)fd; (void)events; node_daemon_t *n=arg; char line[64];
    while (fgets(line,sizeof(line),stdin)) {
        line[strcspn(line,"\n")]=0;
        if (!strcmp(line,"status")) node_daemon_print_status(n);
        else if (!strcmp(line,"peers")) node_daemon_print_peers(n);
        else if (!strcmp(line,"stats")) node_daemon_print_stats(n);
        else if (!strcmp(line,"help")) puts("status peers stats help exit");
        else if (!strcmp(line,"exit")) { node_daemon_stop(n); break; }
        else if (*line) puts("unknown command");
    }
    if (feof(stdin)) clearerr(stdin);
}

node_daemon_t *node_daemon_create_with_config(const node_daemon_config_t *config){if(!config)return NULL;node_daemon_t*n=calloc(1,sizeof(*n));if(n){n->config=*config;n->id=config->node_id;n->port=config->control_port;n->discovery_port=config->discovery_port;n->listen_fd=n->timer_fd=-1;log_set_level(config->log_level);}return n;}
node_daemon_t *node_daemon_create(uint32_t id,uint16_t port,uint16_t discovery_port){node_daemon_config_t c;node_daemon_config_defaults(&c);c.node_id=id;c.control_port=port;c.discovery_port=discovery_port;return node_daemon_create_with_config(&c);}
int node_daemon_start(node_daemon_t*n){
    if(!n)return -1;
#ifndef __linux__
    errno = ENOSYS;
    return -1;
#else
    n->reactor=reactor_create();if(!n->reactor)goto fail;
    n->listen_fd=socket(AF_INET,SOCK_STREAM,0);if(n->listen_fd<0)goto fail;int one=1;setsockopt(n->listen_fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(n->port)};
    if (inet_pton(AF_INET, n->config.bind_address, &a.sin_addr) != 1 ||
        bind(n->listen_fd,(struct sockaddr*)&a,sizeof(a))<0 ||
        listen(n->listen_fd,32)<0 || nonblock(n->listen_fd)<0) goto fail;
    if(discovery_open_with_addresses(&n->discovery,n->id,n->port,n->discovery_port,
                                     n->config.bind_address,
                                     n->config.discovery_address,
                                     discovery_peer,heartbeat_event,n)<0)goto fail;
    n->timer_fd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);if(n->timer_fd<0)goto fail;struct itimerspec ts={{n->config.heartbeat_interval_ms/1000U,(long)(n->config.heartbeat_interval_ms%1000U)*1000000L},{n->config.heartbeat_interval_ms/1000U,(long)(n->config.heartbeat_interval_ms%1000U)*1000000L}};if(timerfd_settime(n->timer_fd,0,&ts,NULL)<0)goto fail;
    if (reactor_add(n->reactor,n->listen_fd,EPOLLIN,listener_event,n)<0 ||
        reactor_add(n->reactor,discovery_fd(n->discovery),EPOLLIN,discovery_event,n)<0 ||
        reactor_add(n->reactor,n->timer_fd,EPOLLIN,timer_event,n)<0 ||
        reactor_add(n->reactor,STDIN_FILENO,EPOLLIN|EPOLLRDHUP,command_event,n)<0) goto fail;
    n->running=1;discovery_send(n->discovery);LOGI("daemon", "node=%u control_port=%u discovery_port=%u bind=%s discovery=%s max_peers=%u heartbeat_interval_ms=%u timeout_ms=%u", n->id, n->port, n->discovery_port, n->config.bind_address, n->config.discovery_address, n->config.max_peers, n->config.heartbeat_interval_ms, n->config.heartbeat_timeout_ms);return 0;
fail:
    node_daemon_destroy(n);
    errno = EIO;
    return -1;
#endif
}
int node_daemon_run(node_daemon_t*n){while(n&&n->running){if(reactor_run_once(n->reactor,1000)<0)return -1;}return 0;}
void node_daemon_stop(node_daemon_t*n){if(n)n->running=0;if(n&&n->reactor)reactor_stop(n->reactor);}
void node_daemon_destroy(node_daemon_t*n){if(!n)return;for(size_t i=0;i<n->peer_count;i++)control_channel_close(&n->peers[i].channel);if(n->discovery)discovery_close(n->discovery);if(n->timer_fd>=0)close(n->timer_fd);if(n->listen_fd>=0)close(n->listen_fd);reactor_destroy(n->reactor);free(n);}
void node_daemon_print_status(const node_daemon_t*n){printf("node=%u control_port=%u discovery_port=%u bind=%s discovery=%s peers=%zu/%u heartbeats=%llu heartbeat_interval_ms=%u heartbeat_timeout_ms=%u\n",n->id,n->port,n->discovery_port,n->config.bind_address,n->config.discovery_address,n->peer_count,n->config.max_peers,(unsigned long long)n->heartbeats,n->config.heartbeat_interval_ms,n->config.heartbeat_timeout_ms);}
void node_daemon_print_peers(const node_daemon_t*n){for(size_t i=0;i<n->peer_count;i++)printf("%u %s state=%d %s last_seen=%llums rx=%llu tx=%llu\n",n->peers[i].info.id,n->peers[i].address,n->peers[i].info.state,n->peers[i].info.connected?"connected":"down",(unsigned long long)n->peers[i].info.last_seen_ms,(unsigned long long)n->peers[i].info.received,(unsigned long long)n->peers[i].info.sent);}
void node_daemon_print_stats(const node_daemon_t*n){printf("heartbeats=%llu messages=%llu state_generation=%llu peers=%zu\n",(unsigned long long)n->heartbeats,(unsigned long long)n->messages,(unsigned long long)n->state_generation,n->peer_count);}
int node_daemon_peer_count(const node_daemon_t*n){return n?(int)n->peer_count:-1;}
int node_daemon_get_peer(const node_daemon_t*n,size_t i,node_peer_info_t*out){if(!n||!out||i>=n->peer_count)return -1;*out=n->peers[i].info;return 0;}
