#ifndef OTN_DISCOVERY_H
#define OTN_DISCOVERY_H

#include <stdint.h>
#include <stddef.h>

#define DISCOVERY_MAGIC 0x4f544e31U
#define DISCOVERY_PORT 19090
#define DISCOVERY_MAX_PACKET 128
#define DISCOVERY_ANNOUNCE 1U
#define DISCOVERY_PING 2U
#define DISCOVERY_PONG 3U

typedef struct {
    uint32_t node_id;
    uint16_t control_port;
    uint16_t reserved;
} discovery_peer_t;

typedef struct discovery discovery_t;
typedef void (*discovery_peer_callback)(const discovery_peer_t *, const char *, void *);
typedef void (*discovery_heartbeat_callback)(uint32_t, const char *, int, void *);

int discovery_open(discovery_t **out, uint32_t node_id, uint16_t control_port,
                   uint16_t port, discovery_peer_callback cb,
                   discovery_heartbeat_callback heartbeat_cb, void *arg);
int discovery_open_with_addresses(discovery_t **out, uint32_t node_id,
                                  uint16_t control_port, uint16_t port,
                                  const char *bind_address,
                                  const char *destination_address,
                                  discovery_peer_callback cb,
                                  discovery_heartbeat_callback heartbeat_cb,
                                  void *arg);
int discovery_fd(const discovery_t *d);
int discovery_send(discovery_t *d);
int discovery_send_ping(discovery_t *d);
int discovery_receive(discovery_t *d);
void discovery_close(discovery_t *d);

#endif
