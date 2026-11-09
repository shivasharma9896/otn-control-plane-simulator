#ifndef OTN_NODE_DAEMON_H
#define OTN_NODE_DAEMON_H

#include <stdint.h>
#include <stddef.h>
#include "epoll_reactor.h"
#include "log.h"

#define NODE_ADDRESS_MAX 64

typedef struct node_daemon node_daemon_t;

typedef enum {
    PEER_STATE_DISCOVERED = 0,
    PEER_STATE_CONNECTING = 1,
    PEER_STATE_ESTABLISHED = 2,
    PEER_STATE_FAILED = 3
} peer_state_t;

typedef struct {
    uint32_t id;
    uint16_t port;
    int connected;
    peer_state_t state;
    uint64_t last_seen_ms;
    uint64_t sent;
    uint64_t received;
    unsigned reconnect_attempt;
    uint64_t next_retry_ms;
} node_peer_info_t;

typedef struct {
    uint32_t node_id;
    uint16_t control_port;
    uint16_t discovery_port;
    unsigned heartbeat_interval_ms;
    unsigned heartbeat_timeout_ms;
    unsigned reconnect_base_ms;
    unsigned reconnect_max_ms;
    unsigned max_peers;
    char bind_address[NODE_ADDRESS_MAX];
    char discovery_address[NODE_ADDRESS_MAX];
    log_level_t log_level;
} node_daemon_config_t;

void node_daemon_config_defaults(node_daemon_config_t *config);
node_daemon_t *node_daemon_create_with_config(const node_daemon_config_t *config);

int node_peer_transition(node_peer_info_t *peer, peer_state_t state);
unsigned node_reconnect_delay_ms(unsigned base_ms, unsigned max_ms,
                                 unsigned attempt);

node_daemon_t *node_daemon_create(uint32_t node_id, uint16_t control_port,
                                  uint16_t discovery_port);
int node_daemon_start(node_daemon_t *n);
int node_daemon_run(node_daemon_t *n);
void node_daemon_stop(node_daemon_t *n);
void node_daemon_destroy(node_daemon_t *n);
void node_daemon_print_status(const node_daemon_t *n);
void node_daemon_print_peers(const node_daemon_t *n);
void node_daemon_print_stats(const node_daemon_t *n);
int node_daemon_peer_count(const node_daemon_t *n);
int node_daemon_get_peer(const node_daemon_t *n, size_t index, node_peer_info_t *out);

#endif
