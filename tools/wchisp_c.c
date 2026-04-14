#include <errno.h>
#include <libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VENDOR_ID 0x4348
#define VENDOR_ID_ALT 0x1a86
#define PRODUCT_ID 0x55e0

#define EP_OUT 0x02
#define EP_IN 0x82
#define INTERFACE_NUMBER 0
#define PROGRAM_CHUNK_SIZE 56
#define TIMEOUT_MS 5000

static uint8_t xor_key[8];

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s probe\n"
            "  %s flash <firmware.bin>\n"
            "  %s <firmware.bin>\n",
            program, program, program);
}

static void print_status(const char *message)
{
    printf("%s\n", message);
    fflush(stdout);
}

static void xor_encrypt(uint8_t *data, size_t length)
{
    size_t index;

    for(index = 0; index < length; index++) {
        data[index] ^= xor_key[index % sizeof(xor_key)];
    }
}

static int bulk_out(libusb_device_handle *handle, uint8_t *buffer, int length, const char *step)
{
    int transferred = 0;
    int status = libusb_bulk_transfer(handle, EP_OUT, buffer, length, &transferred, TIMEOUT_MS);

    if(status != 0 || transferred != length) {
        fprintf(stderr, "%s write failed: %s\n", step, libusb_error_name(status));
        return -1;
    }

    return 0;
}

static int bulk_in(libusb_device_handle *handle, uint8_t *buffer, int capacity, const char *step)
{
    int transferred = 0;
    int status = libusb_bulk_transfer(handle, EP_IN, buffer, capacity, &transferred, TIMEOUT_MS);

    if(status != 0) {
        fprintf(stderr, "%s read failed: %s\n", step, libusb_error_name(status));
        return -1;
    }

    if(transferred <= 0) {
        fprintf(stderr, "%s read failed: empty response\n", step);
        return -1;
    }

    return transferred;
}

static int load_firmware(const char *path, uint8_t **firmware_out, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *firmware;

    if(file == NULL) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if(fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek %s: %s\n", path, strerror(errno));
        fclose(file);
        return -1;
    }

    length = ftell(file);
    if(length <= 0) {
        fprintf(stderr, "Firmware image is empty: %s\n", path);
        fclose(file);
        return -1;
    }

    if(fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to rewind %s: %s\n", path, strerror(errno));
        fclose(file);
        return -1;
    }

    firmware = malloc((size_t)length);
    if(firmware == NULL) {
        fprintf(stderr, "Failed to allocate %ld bytes for firmware image\n", length);
        fclose(file);
        return -1;
    }

    if(fread(firmware, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Failed to read %s\n", path);
        free(firmware);
        fclose(file);
        return -1;
    }

    fclose(file);
    *firmware_out = firmware;
    *size_out = (size_t)length;
    return 0;
}

static int probe_devices(void)
{
    libusb_context *context = NULL;
    libusb_device **devices = NULL;
    ssize_t count;
    int found = 0;
    ssize_t index;
    int status = libusb_init(&context);

    if(status != 0) {
        fprintf(stderr, "Failed to initialize libusb: %s\n", libusb_error_name(status));
        return 1;
    }

    count = libusb_get_device_list(context, &devices);
    if(count < 0) {
        fprintf(stderr, "Failed to enumerate USB devices: %s\n", libusb_error_name((int)count));
        libusb_exit(context);
        return 1;
    }

    for(index = 0; index < count; index++) {
        struct libusb_device_descriptor descriptor;

        if(libusb_get_device_descriptor(devices[index], &descriptor) != 0) {
            continue;
        }

        if((descriptor.idVendor == VENDOR_ID || descriptor.idVendor == VENDOR_ID_ALT) &&
           descriptor.idProduct == PRODUCT_ID) {
            printf("Found WCH ISP device: vid=0x%04x pid=0x%04x bus=%u address=%u\n",
                   descriptor.idVendor,
                   descriptor.idProduct,
                   libusb_get_bus_number(devices[index]),
                   libusb_get_device_address(devices[index]));
            found = 1;
        }
    }

    if(!found) {
        fprintf(stderr, "No WCH ISP device found. Put the board in bootloader mode and retry.\n");
    }

    libusb_free_device_list(devices, 1);
    libusb_exit(context);
    return found ? 0 : 1;
}

static int flash_firmware(const char *path)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    uint8_t *firmware = NULL;
    size_t firmware_size = 0;
    uint8_t cmd[128] = {0};
    uint8_t resp[128] = {0};
    uint8_t chip_id;
    uint8_t checksum = 0;
    uint32_t sectors;
    uint32_t address = 0;
    int response_size;
    int status;
    int release_claimed_interface = 0;

    if(load_firmware(path, &firmware, &firmware_size) != 0) {
        return 1;
    }

    status = libusb_init(&context);
    if(status != 0) {
        fprintf(stderr, "Failed to initialize libusb: %s\n", libusb_error_name(status));
        free(firmware);
        return 1;
    }

    handle = libusb_open_device_with_vid_pid(context, VENDOR_ID, PRODUCT_ID);
    if(handle == NULL) {
        handle = libusb_open_device_with_vid_pid(context, VENDOR_ID_ALT, PRODUCT_ID);
    }

    if(handle == NULL) {
        fprintf(stderr, "WCH ISP device not found. Did you connect it in bootloader mode?\n");
        libusb_exit(context);
        free(firmware);
        return 1;
    }

    if(libusb_kernel_driver_active(handle, INTERFACE_NUMBER) == 1) {
        status = libusb_detach_kernel_driver(handle, INTERFACE_NUMBER);
        if(status != 0 && status != LIBUSB_ERROR_NOT_SUPPORTED) {
            fprintf(stderr, "Failed to detach kernel driver: %s\n", libusb_error_name(status));
            libusb_close(handle);
            libusb_exit(context);
            free(firmware);
            return 1;
        }
    }

    /* On macOS, set configuration explicitly to reset endpoint state */
    libusb_set_configuration(handle, 1);

    status = libusb_claim_interface(handle, INTERFACE_NUMBER);
    if(status != 0) {
        fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(status));
        libusb_close(handle);
        libusb_exit(context);
        free(firmware);
        return 1;
    }
    release_claimed_interface = 1;

    /* Give the device a moment to settle after being opened on macOS */
    usleep(200000);

    /* Identify with retry (device may need a moment after enumeration) */
    {
        int identify_ok = 0;
        int attempt;
        for(attempt = 0; attempt < 3; attempt++) {
            memset(cmd, 0, 128);
            cmd[0] = 0xa1;
            cmd[1] = 0x12;
            memcpy(&cmd[5], "MCU ISP & WCH.CN", sizeof("MCU ISP & WCH.CN"));
            if(bulk_out(handle, cmd, 21, "identify") != 0) {
                usleep(300000);
                continue;
            }
            response_size = bulk_in(handle, resp, sizeof(resp), "identify");
            if(response_size >= 5) {
                identify_ok = 1;
                break;
            }
            fprintf(stderr, "identify attempt %d failed, retrying...\n", attempt + 1);
            usleep(300000);
        }
        if(!identify_ok) {
            fprintf(stderr, "identify failed after 3 attempts\n");
            goto fail;
        }
    }
    chip_id = resp[4];
    printf("Connected to WCH ISP bootloader, chip_id=0x%02x\n", chip_id);
    fflush(stdout);

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0xa7;
    cmd[1] = 0x02;
    cmd[3] = 0x1f;
    if(bulk_out(handle, cmd, 5, "read config") != 0) {
        goto fail;
    }
    response_size = bulk_in(handle, resp, sizeof(resp), "read config");
    if(response_size < 30) {
        fprintf(stderr, "read config failed: short response\n");
        goto fail;
    }

    for(size_t index = 0; index < sizeof(xor_key); index++) {
        checksum += resp[22 + index];
    }
    for(size_t index = 0; index < sizeof(xor_key); index++) {
        xor_key[index] = checksum;
    }
    xor_key[7] = (uint8_t)(xor_key[7] + chip_id);
    printf("XOR key checksum: 0x%02x\n", checksum);
    fflush(stdout);

    /* ---- Check and remove code flash protection (RDPR + WRP) ---- */
    {
        uint8_t cfg_resp[64] = {0};

        /* Read RDPR/UserData/WPR config (bitMask = 0x07) */
        memset(cmd, 0, sizeof(cmd));
        cmd[0] = 0xa7;  /* readConfig */
        cmd[1] = 0x02;
        cmd[3] = 0x07;  /* cfgMaskRDPRUserDataWPR */
        if(bulk_out(handle, cmd, 5, "read config RDPR") != 0) {
            goto fail;
        }
        response_size = bulk_in(handle, cfg_resp, sizeof(cfg_resp), "read config RDPR");
        if(response_size < 18) {
            fprintf(stderr, "read config RDPR failed: short response (%d bytes)\n", response_size);
            goto fail;
        }

        /* cfg_resp layout: [0]=cmd, [1]=?, [2..3]=payload_len,
         * [4]=payload[0] (unknown), [5]=payload[1] (unknown),
         * [6]=RDPR, [7]=nRDPR, [8]=USER, [9]=nUSER,
         * [10]=DATA0, [11]=nDATA0, [12]=DATA1, [13]=nDATA1,
         * [14]=WRP0, [15]=WRP1, [16]=WRP2, [17]=WRP3 */
        uint8_t rdpr_val  = cfg_resp[6];
        uint8_t nrdpr_val = cfg_resp[7];
        uint8_t user_val  = cfg_resp[8];
        uint8_t nuser_val = cfg_resp[9];
        uint8_t data0_val = cfg_resp[10];
        uint8_t ndata0_val = cfg_resp[11];
        uint8_t data1_val = cfg_resp[12];
        uint8_t ndata1_val = cfg_resp[13];
        printf("Option bytes dump:\n");
        printf("  RDPR=0x%02x nRDPR=0x%02x  (valid=%s)\n",
               rdpr_val, nrdpr_val, ((rdpr_val ^ nrdpr_val) == 0xff) ? "YES" : "NO");
        printf("  USER=0x%02x nUSER=0x%02x  (valid=%s)\n",
               user_val, nuser_val, ((user_val ^ nuser_val) == 0xff) ? "YES" : "NO");
        printf("  DATA0=0x%02x nDATA0=0x%02x (valid=%s)\n",
               data0_val, ndata0_val, ((data0_val ^ ndata0_val) == 0xff) ? "YES" : "NO");
        printf("  DATA1=0x%02x nDATA1=0x%02x (valid=%s)\n",
               data1_val, ndata1_val, ((data1_val ^ ndata1_val) == 0xff) ? "YES" : "NO");
        printf("  WRP=%02x %02x %02x %02x\n",
               cfg_resp[14], cfg_resp[15], cfg_resp[16], cfg_resp[17]);
        fflush(stdout);

        if(rdpr_val != 0xa5 || (rdpr_val ^ nrdpr_val) != 0xff ||
           user_val != 0xff || (user_val ^ nuser_val) != 0xff ||
           (data0_val ^ ndata0_val) != 0xff || (data1_val ^ ndata1_val) != 0xff ||
           cfg_resp[14] != 0xff || cfg_resp[15] != 0xff ||
           cfg_resp[16] != 0xff || cfg_resp[17] != 0xff) {
            printf("Flash is protected, unprotecting...\n");
            fflush(stdout);

            /* Build 12-byte config payload (payload[2..13]) with safe defaults */
            uint8_t cfg_data[12];
            memcpy(cfg_data, &cfg_resp[6], 12);  /* copy from payload[2..13] */
            cfg_data[0] = 0xa5;  /* RDPR: unprotected */
            cfg_data[1] = 0x5a;  /* nRDPR: complement */
            cfg_data[2] = 0xff;  /* USER: software WDT, normal resets (factory default) */
            cfg_data[3] = 0x00;  /* nUSER: complement */
            cfg_data[4] = 0xff;  /* DATA0: factory default */
            cfg_data[5] = 0x00;  /* nDATA0: complement */
            cfg_data[6] = 0xff;  /* DATA1: factory default */
            cfg_data[7] = 0x00;  /* nDATA1: complement */
            cfg_data[8]  = 0xff; /* WRP0: all unprotected */
            cfg_data[9]  = 0xff; /* WRP1 */
            cfg_data[10] = 0xff; /* WRP2 */
            cfg_data[11] = 0xff; /* WRP3 */

            /* writeConfig command: 0xa8, len=14 (2+12), bitMask=0x07, pad=0x00, then 12 data bytes */
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = 0xa8;  /* writeConfig */
            uint16_t wcfg_len = 2 + 12;
            memcpy(&cmd[1], &wcfg_len, 2);
            cmd[3] = 0x07;  /* cfgMaskRDPRUserDataWPR */
            cmd[4] = 0x00;  /* padding */
            memcpy(&cmd[5], cfg_data, 12);

            if(bulk_out(handle, cmd, 17, "write config unprotect") != 0) {
                goto fail;
            }
            response_size = bulk_in(handle, cfg_resp, sizeof(cfg_resp), "write config unprotect");
            if(response_size < 4) {
                fprintf(stderr, "Failed to unprotect flash: short response (%d bytes)\n", response_size);
                goto fail;
            }
            /* Check that payload length field matches actual payload bytes received */
            {
                uint16_t expected_len;
                memcpy(&expected_len, &cfg_resp[2], 2);
                int actual_payload = response_size - 4;
                if(actual_payload != (int)expected_len) {
                    fprintf(stderr, "Failed to unprotect flash: length mismatch "
                            "(expected=%u actual=%d)\n", expected_len, actual_payload);
                    goto fail;
                }
            }
            printf("Flash unprotected. Chip will mass-erase on next reset.\n");
            printf("Resetting for unprotect to take effect...\n");
            fflush(stdout);

            /* Reset the chip so the protection change and mass-erase take effect */
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = 0xa2;  /* ispEnd */
            cmd[1] = 0x01;
            cmd[3] = 0x01;
            bulk_out(handle, cmd, 4, "reset after unprotect");
            /* Give time for reset + mass erase */
            sleep(5);

            /* Reconnect to bootloader */
            libusb_release_interface(handle, INTERFACE_NUMBER);
            libusb_close(handle);

            handle = libusb_open_device_with_vid_pid(context, VENDOR_ID, PRODUCT_ID);
            if(handle == NULL) {
                handle = libusb_open_device_with_vid_pid(context, VENDOR_ID_ALT, PRODUCT_ID);
            }
            if(handle == NULL) {
                fprintf(stderr, "Failed to reconnect after unprotect. Please re-enter bootloader mode.\n");
                libusb_exit(context);
                free(firmware);
                return 1;
            }

            if(libusb_kernel_driver_active(handle, INTERFACE_NUMBER) == 1) {
                libusb_detach_kernel_driver(handle, INTERFACE_NUMBER);
            }
            status = libusb_claim_interface(handle, INTERFACE_NUMBER);
            if(status != 0) {
                fprintf(stderr, "Failed to claim interface after reconnect: %s\n", libusb_error_name(status));
                libusb_close(handle);
                libusb_exit(context);
                free(firmware);
                return 1;
            }
            release_claimed_interface = 1;

            /* Re-identify */
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = 0xa1;
            cmd[1] = 0x12;
            memcpy(&cmd[5], "MCU ISP & WCH.CN", sizeof("MCU ISP & WCH.CN"));
            if(bulk_out(handle, cmd, 21, "re-identify") != 0) {
                goto fail;
            }
            if(bulk_in(handle, resp, sizeof(resp), "re-identify") < 5) {
                fprintf(stderr, "re-identify failed\n");
                goto fail;
            }
            printf("Reconnected after unprotect, chip_id=0x%02x\n", resp[4]);
            fflush(stdout);

            /* Re-read config for XOR key */
            memset(cmd, 0, sizeof(cmd));
            cmd[0] = 0xa7;
            cmd[1] = 0x02;
            cmd[3] = 0x1f;
            if(bulk_out(handle, cmd, 5, "re-read config") != 0) {
                goto fail;
            }
            response_size = bulk_in(handle, resp, sizeof(resp), "re-read config");
            if(response_size < 30) {
                fprintf(stderr, "re-read config failed\n");
                goto fail;
            }

            checksum = 0;
            for(size_t index = 0; index < sizeof(xor_key); index++) {
                checksum += resp[22 + index];
            }
            for(size_t index = 0; index < sizeof(xor_key); index++) {
                xor_key[index] = checksum;
            }
            xor_key[7] = (uint8_t)(xor_key[7] + chip_id);
            printf("New XOR key checksum: 0x%02x\n", checksum);
            fflush(stdout);
        } else {
            printf("Flash is not protected, proceeding...\n");
            fflush(stdout);
        }
    }

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0xa3;
    cmd[1] = 0x1e;
    if(bulk_out(handle, cmd, 33, "isp key") != 0) {
        goto fail;
    }
    if(bulk_in(handle, resp, sizeof(resp), "isp key") < 0) {
        goto fail;
    }

    sectors = (uint32_t)((firmware_size + 1023U) / 1024U);
    if(sectors < 8U) {
        sectors = 8U;
    }

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0xa4;
    cmd[1] = 0x04;
    memcpy(&cmd[3], &sectors, sizeof(sectors));
    if(bulk_out(handle, cmd, 9, "erase") != 0) {
        goto fail;
    }
    if(bulk_in(handle, resp, sizeof(resp), "erase") < 0) {
        goto fail;
    }
    printf("Erased %u sectors\n", sectors);
    fflush(stdout);

    printf("Programming %zu bytes...\n", firmware_size);
    fflush(stdout);
    while(address < firmware_size) {
        size_t remaining = firmware_size - address;
        size_t chunk_size = remaining > PROGRAM_CHUNK_SIZE ? PROGRAM_CHUNK_SIZE : remaining;
        uint16_t payload_length = (uint16_t)(chunk_size + 5U);

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = 0xa5;
        memcpy(&cmd[1], &payload_length, sizeof(payload_length));
        memcpy(&cmd[3], &address, sizeof(address));
        cmd[7] = 0x88;
        memcpy(&cmd[8], &firmware[address], chunk_size);
        xor_encrypt(&cmd[8], chunk_size);

        if(bulk_out(handle, cmd, (int)(8U + chunk_size), "program") != 0) {
            goto fail;
        }
        if(bulk_in(handle, resp, sizeof(resp), "program") < 0) {
            goto fail;
        }

        address += (uint32_t)chunk_size;
        if(address % 1024U == 0U || address == firmware_size) {
            printf("  Programmed %u bytes\n", address);
            fflush(stdout);
        }
    }

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0xa5;
    cmd[1] = 5;
    memcpy(&cmd[3], &address, sizeof(address));
    if(bulk_out(handle, cmd, 8, "program finalize") != 0) {
        goto fail;
    }
    if(bulk_in(handle, resp, sizeof(resp), "program finalize") < 0) {
        goto fail;
    }

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0xa3;
    cmd[1] = 0x1e;
    if(bulk_out(handle, cmd, 33, "isp key verify") != 0) {
        goto fail;
    }
    if(bulk_in(handle, resp, sizeof(resp), "isp key verify") < 0) {
        goto fail;
    }

    print_status("Verifying...");
    address = 0;
    while(address < firmware_size) {
        size_t remaining = firmware_size - address;
        size_t chunk_size = remaining > PROGRAM_CHUNK_SIZE ? PROGRAM_CHUNK_SIZE : remaining;
        uint16_t payload_length = (uint16_t)(chunk_size + 5U);

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = 0xa6;
        memcpy(&cmd[1], &payload_length, sizeof(payload_length));
        memcpy(&cmd[3], &address, sizeof(address));
        cmd[7] = 0x88;
        memcpy(&cmd[8], &firmware[address], chunk_size);
        xor_encrypt(&cmd[8], chunk_size);

        if(bulk_out(handle, cmd, (int)(8U + chunk_size), "verify") != 0) {
            goto fail;
        }
        if(bulk_in(handle, resp, sizeof(resp), "verify") < 0) {
            goto fail;
        }

        address += (uint32_t)chunk_size;
    }
    print_status("Verify OK.");

    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0xa2;
    cmd[1] = 0x01;
    cmd[3] = 0x01;
    print_status("Resetting target...");
    if(bulk_out(handle, cmd, 4, "reset") != 0) {
        goto fail;
    }
    /*
     * Many WCH bootloaders drop off USB immediately after accepting the reset
     * command, so waiting for a reply can hang until timeout or forever if the
     * user interrupts first. A successful OUT transfer is sufficient here.
     */

    libusb_release_interface(handle, INTERFACE_NUMBER);
    libusb_close(handle);
    libusb_exit(context);
    free(firmware);
    return 0;

fail:
    if(release_claimed_interface) {
        libusb_release_interface(handle, INTERFACE_NUMBER);
    }
    if(handle != NULL) {
        libusb_close(handle);
    }
    if(context != NULL) {
        libusb_exit(context);
    }
    free(firmware);
    return 1;
}

int main(int argc, char **argv)
{
    if(argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    if(strcmp(argv[1], "probe") == 0) {
        return probe_devices();
    }

    if(strcmp(argv[1], "flash") == 0) {
        if(argc < 3) {
            print_usage(argv[0]);
            return 1;
        }
        return flash_firmware(argv[2]);
    }

    return flash_firmware(argv[1]);
}
