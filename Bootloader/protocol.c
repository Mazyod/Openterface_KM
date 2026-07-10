/* Binary protocol handler for bootloader firmware update.
 *
 * Frame: [0xAA][0x55][CMD][LEN][DATA...][CS_LO][CS_HI][0x55][0xAA]
 * 8-bit checksum = (sum of all bytes in CMD+LEN+DATA) mod 256
 * CS_LO = checksum, CS_HI = checksum (repeated for alignment with WCH format)
 *
 * Response: [0xAA][0x55][0x00][STATUS][0x55][0xAA] (6 bytes)
 * STATUS: 0x00 = OK, 0x01 = ERROR
 */
#include "ch32v20x.h"
#include "protocol.h"
#include "usb.h"
#include "bootloader.h"
#include "flash.h"

/* Accumulating buffer for flash writes (256-byte chunks) */
static uint8_t program_buf[256];
static uint16_t program_len = 0;
static uint32_t program_addr = FLASH_PROTECT_START;

/* Compute 8-bit checksum: sum of all bytes mod 256 */
static uint8_t checksum8(const uint8_t *data, uint16_t len) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/* Send a response frame via USB */
static void send_response(uint8_t status) {
    uint8_t resp[6];
    resp[0] = FRAME_HEAD1;    /* 0xAA */
    resp[1] = FRAME_HEAD2;    /* 0x55 */
    resp[2] = 0x00;           /* reserved */
    resp[3] = status;         /* 0x00 = OK, 0x01 = ERROR */
    resp[4] = FRAME_END1;     /* 0x55 */
    resp[5] = FRAME_END2;     /* 0xAA */
    usb_send(resp, 6);
}

/* Send a response with a data byte (used by CMD_VERIFY to return checksum) */
static void send_response_with_data(uint8_t value) {
    uint8_t resp[6];
    resp[0] = FRAME_HEAD1;
    resp[1] = FRAME_HEAD2;
    resp[2] = value;          /* checksum byte */
    resp[3] = 0x00;           /* status: OK */
    resp[4] = FRAME_END1;
    resp[5] = FRAME_END2;
    usb_send(resp, 6);
}

/* Flush pending program buffer (write 256-byte page) */
static void flush_program(void) {
    if (program_len == 0) return;

    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(program_addr);
    FLASH_ProgramPage_Fast(program_addr, (uint32_t *)program_buf);
    FLASH_Lock();

    program_addr += 256;
    program_len = 0;
}

void protocol_process(const uint8_t *data, uint16_t len) {
    uint16_t i;

    /* We expect complete frames in the USB bulk packet (max 64 bytes).
     * No byte-level framing needed — USB guarantees packet integrity.
     * Frame: [AA][55][CMD][LEN][DATA...][CS_LO][CS_HI][55][AA]
     */
    if (len < FRAME_MIN_LEN) return;

    /* Verify header */
    if (data[0] != FRAME_HEAD1 || data[1] != FRAME_HEAD2) {
        send_response(0x01);  /* error */
        return;
    }

    uint8_t cmd = data[2];
    uint8_t payload_len = data[3];
    uint16_t expected_len = 4 + payload_len + 4;  /* header(4) + payload + checksum(2) + end(2) */

    if (len < expected_len) {
        send_response(0x01);
        return;
    }

    /* Verify end markers */
    if (data[expected_len - 2] != FRAME_END1 ||
        data[expected_len - 1] != FRAME_END2) {
        send_response(0x01);
        return;
    }

    /* Verify checksum (8-bit sum of CMD + LEN + DATA) */
    uint8_t calc_cs = checksum8(&data[2], 1 + 1 + payload_len);  /* cmd(1) + len(1) + data */
    uint8_t recv_cs_lo = data[expected_len - 4];
    uint8_t recv_cs_hi = data[expected_len - 3];

    if (calc_cs != recv_cs_lo || calc_cs != recv_cs_hi) {
        send_response(0x01);
        return;
    }

    const uint8_t *payload = &data[4];

    switch (cmd) {
        case CMD_HANDSHAKE:
            /* Send handshake with version info via USB */
            send_response(0x00);
            break;

        case CMD_ERASE:
            /* Unlock fast flash for subsequent programming */
            FLASH_Unlock_Fast();
            program_len = 0;
            program_addr = FLASH_PROTECT_START;
            send_response(0x00);
            break;

        case CMD_WRITE:
            /* Accumulate data into buffer, flush in 256-byte chunks */
            for (i = 0; i < payload_len; i++) {
                program_buf[program_len++] = payload[i];
                if (program_len >= 256) {
                    flush_program();
                }
            }
            send_response(0x00);
            break;

        case CMD_VERIFY:
            /* Verify flash: payload = [ADDR(4)][LEN(2)], response includes 8-bit checksum */
            if (payload_len != 6) {
                send_response(0x01);
                break;
            }
            {
                uint32_t v_addr = (uint32_t)payload[0]
                                | ((uint32_t)payload[1] << 8)
                                | ((uint32_t)payload[2] << 16)
                                | ((uint32_t)payload[3] << 24);
                uint16_t v_len = (uint16_t)payload[4]
                               | ((uint16_t)payload[5] << 8);
                uint8_t cs = 0;
                for (i = 0; i < v_len; i++) {
                    cs += *(volatile uint8_t *)(v_addr + i);
                }
                /* Send checksum in response: [AA][55][cs][0x00][55][AA] */
                send_response_with_data(cs);
            }
            break;

        case CMD_JUMP:
            send_response(0x00);
            /* Small delay to ensure response is sent */
            for (volatile uint32_t i = 0; i < 10000; i++);
            bootloader_jump();
            break;

        default:
            send_response(0x01);  /* unknown command */
            break;
    }
}

/* Legacy API — kept for compatibility */
void protocol_send_handshake(uint8_t major, uint8_t minor, uint8_t patch) {
    (void)major;
    (void)minor;
    (void)patch;
    send_response(0x00);
}

void protocol_send_response(uint8_t cmd, const uint8_t *data, uint16_t len) {
    (void)cmd;
    (void)data;
    (void)len;
    send_response(0x00);
}

void protocol_send_error(uint8_t code) {
    (void)code;
    send_response(0x01);
}