#include <errno.h>
#include <libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    status = libusb_claim_interface(handle, INTERFACE_NUMBER);
    if(status != 0) {
        fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(status));
        libusb_close(handle);
        libusb_exit(context);
        free(firmware);
        return 1;
    }
    release_claimed_interface = 1;

    cmd[0] = 0xa1;
    cmd[1] = 0x12;
    memcpy(&cmd[5], "MCU ISP & WCH.CN", sizeof("MCU ISP & WCH.CN"));
    if(bulk_out(handle, cmd, 21, "identify") != 0) {
        goto fail;
    }
    response_size = bulk_in(handle, resp, sizeof(resp), "identify");
    if(response_size < 5) {
        fprintf(stderr, "identify read failed: short response\n");
        goto fail;
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
