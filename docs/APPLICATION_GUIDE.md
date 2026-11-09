# OTN application guide

## Purpose

OTN is a small distributed control-plane simulator. Nodes discover peers over
UDP, establish nonblocking TCP control channels, exchange framed messages, and
track peer liveness and state generations. It is intentionally small enough to
explain in an interview while exercising partial I/O, timeouts, bounded
resources, failure recovery, and observability.

## Architecture and data flow

Each Linux node has one event-loop thread:

1. `timerfd` wakes the loop at the heartbeat interval.
2. The node sends an announce and PING over UDP.
3. Discovery announcements invoke peer connection attempts.
4. TCP sockets are nonblocking and registered with `epoll`.
5. HELLO identifies the remote node; STATE exchanges the monotonic generation.
6. Reads accumulate bytes until a complete, validated frame exists.
7. Timeout or transport failure moves the peer to `FAILED`; capped exponential
   backoff prevents reconnect storms.
8. A later announce/PONG permits recovery to `DISCOVERED` and reconnection.

TCP is authoritative for control data. UDP is deliberately best-effort and is
used for discovery and liveness hints.

## Lifecycle and configuration

Peer states are `DISCOVERED`, `CONNECTING`, `ESTABLISHED`, and `FAILED`.
Defaults are compiled into `node_daemon_config_defaults`. A config file uses
`key=value` lines; command-line flags override it:

```ini
node_id=1001
control_port=19101
discovery_port=19100
bind_address=0.0.0.0
discovery_address=255.255.255.255
max_peers=16
heartbeat_interval_ms=1000
heartbeat_timeout_ms=5000
reconnect_base_ms=500
reconnect_max_ms=30000
log_level=info
```

Run `./bin/otn_node -h` for options. stdin commands are `status`, `peers`,
`stats`, `help`, and `exit`. Logs include wall-clock timestamp, severity,
component, and message.

## Build and run

Requirements are a C11 compiler, make, and libc development headers.

```sh
make test
make
./bin/otn_node -i 1001 -p 19101 -d 19100
```

The daemon uses Linux `epoll` and `timerfd`. macOS supports `make test`,
framing, configuration/state helpers, and syntax checks, but cannot run the
Linux daemon. Use native Linux, a Linux VM, or a GitHub Codespace for runtime
testing. Network namespaces and a bridge provide reproducible multi-node
isolation without containers.

## Testing and limitations

`make test` runs control framing, checksum, state/config/backoff, and logging
tests, then strict syntax checks. GitHub Actions runs the same suite on Linux
plus AddressSanitizer/UndefinedBehaviorSanitizer and cppcheck.

Known limitations: IPv4 only; UDP discovery is unauthenticated and best-effort;
there is no encryption, authorization, persistent storage, or dynamic
reconfiguration; the daemon is single-threaded and Linux-specific; the peer
table is bounded at 64 entries; and state synchronization is a generation
counter rather than a replicated data model.

## Interview talking points

- One event loop avoids lock coordination and scales many idle sockets.
- UDP makes discovery cheap and lossy; TCP provides ordered control frames.
- Bounded TX/RX buffers explicitly handle partial writes and reads.
- Failed peers remain observable while capped exponential backoff avoids storms.
- Magic/version/length/checksum validation rejects malformed input early.
- Next steps: TLS/authentication, IPv6, a poll/kqueue backend, persistent state,
  jittered backoff, metrics, and fuzz/property tests.
