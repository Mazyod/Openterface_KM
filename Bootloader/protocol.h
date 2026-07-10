#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Frame format: [0xAA] [0x55] [CMD] [LEN] [DATA...] [CS_LO] [CS_HI] [0x55] [0xAA]
 * 8-bit checksum = sum of all bytes mod 256
 * End markers: [0x55] [0xAA]
 */
#define FRAME_HEAD1        0xAA
#define FRAME_HEAD2        0x55
#define FRAME_END1         0x55
#define FRAME_END2         0xAA
#define FRAME_MIN_LEN      9   /* head(2) + cmd(1) + len(1) + checksum(2) + end(2) + cmd(1) */

/* Command codes */
#define CMD_HANDSHAKE      0x01
#define CMD_ERASE          0x02
#define CMD_WRITE          0x03
#define CMD_VERIFY         0x04
#define CMD_JUMP           0x05
#define CMD_ERROR          0xFF

/* Error codes */
#define ERR_CRC            0x04
#define ERR_PARAM          0x05
#define ERR_CMD            0x06

void protocol_process(const uint8_t *data, uint16_t len);
void protocol_send_handshake(uint8_t major, uint8_t minor, uint8_t patch);
void protocol_send_response(uint8_t cmd, const uint8_t *data, uint16_t len);
void protocol_send_error(uint8_t code);

#endif /* PROTOCOL_H */