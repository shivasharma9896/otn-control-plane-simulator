#define _GNU_SOURCE
#include "discovery.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct discovery {
    int fd;
    uint32_t node_id;
    uint16_t control_port;
    uint16_t port;
    discovery_peer_callback cb;
    discovery_heartbeat_callback heartbeat_cb;
    void *arg;
    struct sockaddr_in broadcast;
};

typedef struct {
    uint32_t magic;
    uint32_t node_id;
    uint16_t control_port;
    uint16_t reserved;
    uint8_t kind;
    uint8_t padding[3];
} discovery_wire_t;

int discovery_open_with_addresses(discovery_t **out, uint32_t node_id,
                                  uint16_t control_port, uint16_t port,
                                  const char *bind_address,
                                  const char *destination_address,
                                  discovery_peer_callback cb,
                                  discovery_heartbeat_callback heartbeat_cb,
                                  void *arg) {
    if (!out || !cb || !port) { errno = EINVAL; return -1; }
    discovery_t *d = calloc(1, sizeof(*d));
    if (!d) return -1;
    d->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (d->fd < 0) { free(d); return -1; }
    int one = 1;
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
    struct sockaddr_in destination = {
        .sin_family = AF_INET, .sin_port = htons(port)
    };
    const char *bind_text = bind_address && bind_address[0] ? bind_address : "0.0.0.0";
    const char *destination_text = destination_address && destination_address[0]
        ? destination_address : "255.255.255.255";
    if (inet_pton(AF_INET, bind_text, &addr.sin_addr) != 1 ||
        inet_pton(AF_INET, destination_text, &destination.sin_addr) != 1) {
        close(d->fd);
        free(d);
        errno = EINVAL;
        return -1;
    }
    if (setsockopt(d->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
        setsockopt(d->fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0 ||
        bind(d->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(d->fd); free(d); return -1;
    }
    int flags = fcntl(d->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(d->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(d->fd); free(d); return -1;
    }
    d->node_id = node_id; d->control_port = control_port; d->port = port;
    d->cb = cb; d->heartbeat_cb = heartbeat_cb; d->arg = arg;
    d->broadcast = destination;
    *out = d; return 0;
}

int discovery_open(discovery_t **out, uint32_t node_id, uint16_t control_port,
                   uint16_t port, discovery_peer_callback cb,
                   discovery_heartbeat_callback heartbeat_cb, void *arg) {
    return discovery_open_with_addresses(out, node_id, control_port, port,
                                          "0.0.0.0", "255.255.255.255",
                                          cb, heartbeat_cb, arg);
}

int discovery_fd(const discovery_t *d) { return d ? d->fd : -1; }

int discovery_send(discovery_t *d) {
    discovery_wire_t w = {htonl(DISCOVERY_MAGIC), htonl(d->node_id),
                          htons(d->control_port), 0, DISCOVERY_ANNOUNCE, {0, 0, 0}};
    ssize_t n = sendto(d->fd, &w, sizeof(w), 0, (struct sockaddr *)&d->broadcast,
                       sizeof(d->broadcast));
    return n == (ssize_t)sizeof(w) ? 0 : -1;
}

int discovery_send_ping(discovery_t *d) {
    discovery_wire_t w = {htonl(DISCOVERY_MAGIC), htonl(d->node_id),
                          htons(d->control_port), 0, DISCOVERY_PING, {0, 0, 0}};
    ssize_t n = sendto(d->fd, &w, sizeof(w), 0, (struct sockaddr *)&d->broadcast,
                       sizeof(d->broadcast));
    return n == (ssize_t)sizeof(w) ? 0 : -1;
}

int discovery_receive(discovery_t *d) {
    for (;;) {
        uint8_t buf[DISCOVERY_MAX_PACKET]; struct sockaddr_in from; socklen_t len = sizeof(from);
        ssize_t n = recvfrom(d->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
        if ((size_t)n != sizeof(discovery_wire_t)) continue;
        discovery_wire_t w; memcpy(&w, buf, sizeof(w));
        if (ntohl(w.magic) != DISCOVERY_MAGIC || ntohl(w.node_id) == d->node_id) continue;
        if (w.kind == DISCOVERY_PING) {
            w.kind = DISCOVERY_PONG;
            (void)sendto(d->fd, &w, sizeof(w), 0, (struct sockaddr *)&from, len);
            continue;
        }
        if (w.kind == DISCOVERY_PONG) {
            if (d->heartbeat_cb) {
                char ip[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip)))
                    d->heartbeat_cb(ntohl(w.node_id), ip, 1, d->arg);
            }
            continue;
        }
        if (w.kind != DISCOVERY_ANNOUNCE) continue;
        discovery_peer_t p = {ntohl(w.node_id), ntohs(w.control_port), 0};
        char ip[INET_ADDRSTRLEN]; if (!inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip))) continue;
        d->cb(&p, ip, d->arg);
    }
}

void discovery_close(discovery_t *d) { if (d) { close(d->fd); free(d); } }
