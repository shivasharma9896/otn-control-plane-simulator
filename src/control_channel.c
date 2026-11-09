#define _GNU_SOURCE
#include "control_channel.h"
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CONTROL_MAGIC 0x4f544e43U

uint32_t control_checksum(const uint8_t *data, size_t len) {
    uint32_t h = 2166136261U;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619U; }
    return h;
}

static void put32(uint8_t *p, uint32_t v) { uint32_t x = htonl(v); memcpy(p, &x, 4); }
static void put16(uint8_t *p, uint16_t v) { uint16_t x = htons(v); memcpy(p, &x, 2); }
static uint32_t get32(const uint8_t *p) { uint32_t x; memcpy(&x, p, 4); return ntohl(x); }
static uint16_t get16(const uint8_t *p) { uint16_t x; memcpy(&x, p, 2); return ntohs(x); }
static uint32_t frame_checksum(const uint8_t *frame, uint32_t length) {
    uint8_t bytes[12 + CONTROL_MAX_PAYLOAD];
    memcpy(bytes, frame, 12);
    if (length) memcpy(bytes + 12, frame + 16, length);
    return control_checksum(bytes, 12 + length);
}

int control_channel_init(control_channel_t *c, int fd, uint32_t peer_id) {
    if (!c || fd < 0) { errno = EINVAL; return -1; }
    memset(c, 0, sizeof(*c)); c->fd = fd; c->peer_id = peer_id; return 0;
}

int control_encode(uint16_t type, const void *payload, uint32_t length,
                   uint8_t *out, size_t out_size, size_t *encoded) {
    if (!out || !encoded || length > CONTROL_MAX_PAYLOAD ||
        out_size < CONTROL_HEADER_SIZE + length) { errno = EMSGSIZE; return -1; }
    put32(out, CONTROL_MAGIC); put16(out + 4, 1); put16(out + 6, type);
    put32(out + 8, length);
    if (length && payload) memcpy(out + CONTROL_HEADER_SIZE, payload, length);
    put32(out + 12, frame_checksum(out, length));
    *encoded = 16 + length; return 0;
}

int control_queue(control_channel_t *c, uint16_t type, const void *payload, uint32_t length) {
    if (!c || c->tx_len || c->tx_off) { errno = EBUSY; return -1; }
    size_t n; if (control_encode(type, payload, length, c->tx, sizeof(c->tx), &n) < 0) return -1;
    c->tx_len = n; c->tx_off = 0; return 0;
}

int control_flush(control_channel_t *c) {
    while (c && c->tx_off < c->tx_len) {
        ssize_t n = send(c->fd, c->tx + c->tx_off, c->tx_len - c->tx_off, MSG_NOSIGNAL);
        if (n > 0) c->tx_off += (size_t)n;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        else return -1;
    }
    if (c) c->tx_len = c->tx_off = 0;
    return 0;
}

int control_read(control_channel_t *c, control_header_t *header,
                 uint8_t *payload, size_t payload_size, int *message_ready) {
    if (!c || !header || !payload || !message_ready) { errno = EINVAL; return -1; }
    *message_ready = 0;
    for (;;) {
        if (c->rx_len == sizeof(c->rx)) break;
        ssize_t n = recv(c->fd, c->rx + c->rx_len, sizeof(c->rx) - c->rx_len, 0);
        if (n > 0) c->rx_len += (size_t)n;
        else if (n == 0) return -2;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else if (errno == EINTR) continue;
        else return -1;
    }
    if (c->rx_len < 16) return 0;
    uint32_t magic = get32(c->rx), length = get32(c->rx + 8);
    if (magic != CONTROL_MAGIC || get16(c->rx + 4) != 1 || length > CONTROL_MAX_PAYLOAD) {
        errno = EBADMSG; return -1;
    }
    size_t total = 16 + length; if (c->rx_len < total) return 0;
    if (payload_size < length) { errno = EMSGSIZE; return -1; }
    uint32_t expected = get32(c->rx + 12);
    uint32_t actual = frame_checksum(c->rx, length);
    if (expected != actual) { errno = EBADMSG; return -1; }
    header->magic = magic; header->version = get16(c->rx + 4); header->type = get16(c->rx + 6);
    header->length = length; header->checksum = expected;
    if (length) memcpy(payload, c->rx + 16, length);
    memmove(c->rx, c->rx + total, c->rx_len - total); c->rx_len -= total;
    *message_ready = 1; return 0;
}

int control_wants_write(const control_channel_t *c) { return c && c->tx_off < c->tx_len; }
void control_channel_close(control_channel_t *c) { if (c && c->fd >= 0) { close(c->fd); c->fd = -1; } }
