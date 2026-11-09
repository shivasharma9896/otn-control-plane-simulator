# Failure testing

Run the portable checks with:

```sh
make test
```

They cover fragmented frames, malformed checksums, oversized payloads,
configuration defaults, log-level parsing, lifecycle transitions, and capped
backoff. On Linux, run:

```sh
make linux-integration
```

For a multi-process smoke test:

```sh
./bin/otn_node -i 1001 -p 19101 -d 19100 &
./bin/otn_node -i 1002 -p 19102 -d 19100 &
```

Stopping a process exercises UDP heartbeat loss, `FAILED` retention, and
bounded reconnect delay. Restarting it exercises discovery-based recovery.
For stronger isolation use Linux network namespaces, veth pairs, and a bridge;
set `bind_address` and `discovery_address` to namespace/bridge addresses.

CI runs the suite on Linux with AddressSanitizer/UndefinedBehaviorSanitizer and
cppcheck. Known limitations include IPv4-only unauthenticated discovery,
single-threaded Linux runtime, no persistence/encryption, and a 64-peer bound.
