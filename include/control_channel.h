#ifndef OTN_CONTROL_CHANNEL_H
#define OTN_CONTROL_CHANNEL_H

#include <stdint.h>
#include <stddef.h>

#define CONTROL_MAX_PAYLOAD 1024U
#define CONTROL_HEADER_SIZE 16U

typedef enum {
    CONTROL_HELLO = 1,
    CONTROL_HEARTBEAT = 2,
    CONTROL_STATE = 3,
    CONTROL_ACK = 4,
    CONTROL_GOODBYE = 5
} control_type_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;
    uint32_t checksum;
} control_header_t;

typedef struct {
    int fd;
    uint32_t peer_id;
    uint8_t rx[CONTROL_HEADER_SIZE + CONTROL_MAX_PAYLOAD];
    size_t rx_len;
    uint8_t tx[CONTROL_HEADER_SIZE + CONTROL_MAX_PAYLOAD];
    size_t tx_len;
    size_t tx_off;
} control_channel_t;

uint32_t control_checksum(const uint8_t *data, size_t len);
int control_channel_init(control_channel_t *c, int fd, uint32_t peer_id);
int control_encode(uint16_t type, const void *payload, uint32_t length,
                   uint8_t *out, size_t out_size, size_t *encoded);
int control_queue(control_channel_t *c, uint16_t type, const void *payload, uint32_t length);
int control_flush(control_channel_t *c);
int control_read(control_channel_t *c, control_header_t *header,
                 uint8_t *payload, size_t payload_size, int *message_ready);
int control_wants_write(const control_channel_t *c);
void control_channel_close(control_channel_t *c);

#endif
