# OTN distributed control-plane simulator

OTN is a small C11 control-plane simulator with bounded peer state, Linux
`epoll`/`timerfd` scheduling, UDP discovery and PING/PONG liveness, nonblocking
TCP control channels, versioned binary framing, checksums, lifecycle tracking,
CLI status commands, structured logs, and bounded reconnect backoff.

## Build and validation

```sh
make test                 # portable tests and strict syntax checks
make                      # build bin/otn_node
```

The daemon runs on Linux. `make test` is also the macOS development path: it
builds the framing, checksum, state/config/backoff, and logging tests and checks
all sources for syntax. Full runtime testing requires native Linux, a Linux VM,
or a GitHub Codespace.

```sh
./bin/otn_node -i 1 -p 19091 -d 19090 -I 1000 -T 5000 -m 16 -l info
```

Commands on stdin are `status`, `peers`, `stats`, `help`, and `exit`. Run
`./scripts/simulate.sh` for two local processes; control ports must be unique.

## Configuration

Use `-c node.conf`; command-line options override file values. Supported keys
are `node_id`, `control_port`, `discovery_port`, `bind_address`,
`discovery_address`, `max_peers`, `heartbeat_interval_ms`,
`heartbeat_timeout_ms`, `reconnect_base_ms`, `reconnect_max_ms`, and `log_level`.
The matching flags are `-i -p -d -b -D -m -I -T -l`. Logs are timestamped and
structured as `timestamp level [component] message` on stderr.

## Linux without containers

In a GitHub Codespace, install `build-essential`, run `make test && make`, and
use two terminals for separate node processes. For reproducible isolation on a
Linux host or VM, create network namespaces and a veth pair/bridge, assign each
namespace an IPv4 address, and pass that address with `-b`. Set `-D` to the
bridge broadcast address (or a directed IPv4 address where broadcast is not
forwarded). Allow UDP discovery and TCP control traffic through the firewall.

## Protocol and design

UDP discovery carries fixed-size announce, PING, and PONG records. TCP carries a
16-byte network-order header and up to 1024 bytes of payload. Peer states are
`DISCOVERED`, `CONNECTING`, `ESTABLISHED`, and `FAILED`; failed records remain
visible and retry using capped exponential backoff.

See `docs/APPLICATION_GUIDE.md` for purpose, data flow, lifecycle, build/run,
testing, limitations, and interview talking points. Detailed design is in
`docs/ARCHITECTURE.md`, `docs/PROTOCOL.md`, and `docs/FAILURE_TESTING.md`.
