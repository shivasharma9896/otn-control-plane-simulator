# Architecture

The daemon is a single-threaded Linux event loop. `epoll` multiplexes the TCP
listener, UDP discovery socket, timerfd heartbeat scheduler, standard input,
and peer sockets. Deterministic ordering avoids lock coordination in the
control plane.

The bounded peer table is configured by `max_peers` (at most 64 compiled
entries). Runtime settings layer defaults, an optional key/value file, and
command-line overrides. TCP and discovery sockets accept explicit IPv4 bind
and destination addresses.

UDP discovery announces node identity and control port, and provides
best-effort PING/PONG liveness hints. TCP is authoritative: HELLO identifies a
peer and STATE exchanges a monotonic state generation. Each channel has bounded
receive/transmit buffers for partial I/O.

Timeouts and transport errors preserve a `FAILED` peer record. Reconnect delay
doubles per attempt and is capped by `reconnect_max_ms`, preventing reconnect
storms. A later announcement or PONG permits recovery. Structured logs contain
a timestamp, level, component, and message.

The portable test path compiles framing, discovery, reactor-independent code,
configuration, state, backoff, and logging tests on macOS. Daemon execution is
Linux-only because epoll and timerfd are Linux APIs.
