# Protocol

## UDP discovery

Discovery packets are fixed-size network-order records containing a magic value,
node ID, TCP control port, and kind: announce, ping, or pong. Announcements go
to the configured IPv4 destination (broadcast by default). A node replies to a
PING directly to its sender. UDP is best-effort; TCP remains authoritative.

## TCP framing

Every frame has a 16-byte header followed by at most 1024 bytes:

| Offset | Size | Field |
|---|---:|---|
| 0 | 4 | magic |
| 4 | 2 | version |
| 6 | 2 | message type |
| 8 | 4 | payload length |
| 12 | 4 | FNV-1a-style checksum |

Fields are network byte order. Reads and writes are buffered for partial socket
operations. Invalid magic, version, length, or checksum closes the peer.
Current message types are HELLO, HEARTBEAT, STATE, ACK, and GOODBYE. HELLO
identifies the node; STATE exchanges a monotonic generation.
