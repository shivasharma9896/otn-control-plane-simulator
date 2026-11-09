#include "node_daemon.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    node_daemon_config_t config;
    node_daemon_config_defaults(&config);
    assert(config.heartbeat_interval_ms > 0);
    assert(config.heartbeat_timeout_ms > config.heartbeat_interval_ms);
    assert(config.max_peers > 0);
    assert(node_reconnect_delay_ms(100, 1000, 1) == 100);
    assert(node_reconnect_delay_ms(100, 1000, 4) == 800);
    assert(node_reconnect_delay_ms(100, 1000, 5) == 1000);

    node_peer_info_t peer = {
        .id = 42,
        .port = 19142,
        .state = PEER_STATE_DISCOVERED
    };

    assert(node_peer_transition(&peer, PEER_STATE_CONNECTING) == 0);
    assert(peer.state == PEER_STATE_CONNECTING && peer.connected);
    assert(node_peer_transition(&peer, PEER_STATE_ESTABLISHED) == 0);
    assert(peer.state == PEER_STATE_ESTABLISHED && peer.connected);
    assert(node_peer_transition(&peer, PEER_STATE_FAILED) == 0);
    assert(peer.state == PEER_STATE_FAILED && !peer.connected);
    assert(node_peer_transition(&peer, PEER_STATE_DISCOVERED) == 0);
    assert(peer.state == PEER_STATE_DISCOVERED && !peer.connected);
    assert(node_peer_transition(&peer, (peer_state_t)99) < 0);

    puts("state transition tests passed");
    return 0;
}
