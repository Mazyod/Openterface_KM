# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Define the source directory
SRC_DIR := $(abspath $(dir $(CURDIR)))

# Include other makefiles
-include ../makefile.init

RM := rm -rf

# Feature flags — define BEFORE includes so subdir.mk files can use them
# BLE support: build with 'make ENABLE_BLE=0' to omit BLE (frees ~14 KB RAM)
ENABLE_BLE ?= 1
ENABLE_UAC_ORIGIN := $(origin ENABLE_UAC)
ENABLE_UAC ?= 1

# USB work modes:
#   hid_acm        = target HID only, host CDC ACM
#   hid_uac_acm    = target HID+UAC, host CDC ACM+UAC bridge (default)
#   hid_acm_bridge = target CDC ACM enumeration scaffold + host CDC ACM
#   hid_ecm_bridge = target/host CDC ECM enumeration scaffold
USB_WORK_MODE ?= $(if $(filter 0,$(ENABLE_UAC)),hid_acm,hid_uac_acm)

ifeq ($(USB_WORK_MODE),hid_acm)
USB_WORK_MODE_ID := 1
USB_WORK_MODE_ENABLE_UAC := 0
else ifeq ($(USB_WORK_MODE),hid_uac_acm)
USB_WORK_MODE_ID := 2
USB_WORK_MODE_ENABLE_UAC := 1
else ifeq ($(USB_WORK_MODE),hid_acm_bridge)
USB_WORK_MODE_ID := 3
USB_WORK_MODE_ENABLE_UAC := 0
else ifeq ($(USB_WORK_MODE),hid_ecm_bridge)
USB_WORK_MODE_ID := 4
USB_WORK_MODE_ENABLE_UAC := 0
else
$(error Unsupported USB_WORK_MODE '$(USB_WORK_MODE)')
endif

ifeq ($(ENABLE_UAC_ORIGIN),undefined)
ENABLE_UAC := $(USB_WORK_MODE_ENABLE_UAC)
else ifneq ($(ENABLE_UAC),$(USB_WORK_MODE_ENABLE_UAC))
$(error ENABLE_UAC=$(ENABLE_UAC) conflicts with USB_WORK_MODE=$(USB_WORK_MODE))
endif

HOST_CC ?= cc
PKG_CONFIG ?= pkg-config
WCHISP ?= ../tools/wchisp_c
BUILD_CONFIG_STAMP := .build-config.stamp

BOARD_VERSION ?= 1
BLE_NAME ?= KVM-GO

LIBUSB_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS ?= $(shell $(PKG_CONFIG) --libs libusb-1.0 2>/dev/null)

ifeq ($(strip $(LIBUSB_LIBS)),)
ifneq ($(wildcard /opt/homebrew/include/libusb-1.0/libusb.h),)
LIBUSB_CFLAGS := -I/opt/homebrew/include/libusb-1.0
LIBUSB_LIBS := -L/opt/homebrew/lib -lusb-1.0
else ifneq ($(wildcard /usr/local/include/libusb-1.0/libusb.h),)
LIBUSB_CFLAGS := -I/usr/local/include/libusb-1.0
LIBUSB_LIBS := -L/usr/local/lib -lusb-1.0
endif
endif

# All of the sources participating in the build are defined here
-include sources.mk
-include SRC/Startup/subdir.mk
-include SRC/Peripheral/src/subdir.mk
-include SRC/Debug/subdir.mk
-include SRC/Core/subdir.mk
-include Profile/subdir.mk
-include LIB/subdir.mk
-include HAL/subdir.mk
-include APP/USBLIB/USB-Driver/src/subdir.mk
-include APP/USBLIB/CONFIG/subdir.mk
-include APP/UART/subdir.mk
-include APP/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
openterface-km-firmware.hex \

SECONDARY_LIST += \
openterface-km-firmware.lst \

SECONDARY_SIZE += \
openterface-km-firmware.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: openterface-km-firmware.elf secondary-outputs

bin: openterface-km-firmware.bin

flash: openterface-km-firmware.bin $(WCHISP)
	@echo 'Flashing...'
	$(WCHISP) flash openterface-km-firmware.bin

probe: $(WCHISP)
	$(WCHISP) probe

openterface-km-firmware.bin: openterface-km-firmware.elf
	@echo 'Generating BIN file for flashing...'
	riscv-none-embed-objcopy -O binary "openterface-km-firmware.elf" "openterface-km-firmware.bin"

ifeq ($(strip $(LIBUSB_LIBS)),)
$(WCHISP):
	@echo 'libusb-1.0 development files not found. Install with: brew install libusb pkg-config' >&2
	@false
else
$(WCHISP): ../tools/wchisp_c.c
	@echo 'Building host WCH ISP tool: $@'
	$(HOST_CC) -O2 -Wall -Wextra -o "$@" "$<" $(LIBUSB_CFLAGS) $(LIBUSB_LIBS)
endif

CFLAGS += -include stdint.h -include stddef.h
CFLAGS += -Dversion=$(BOARD_VERSION)
CFLAGS += -DUSB_WORK_MODE=$(USB_WORK_MODE_ID)

# UAC support is derived from USB_WORK_MODE. Legacy ENABLE_UAC=0 maps to hid_acm
# when USB_WORK_MODE is not specified.
ifeq ($(ENABLE_UAC),1)
CFLAGS += -DENABLE_UAC
endif

ifeq ($(ENABLE_BLE),1)
CFLAGS += -DENABLE_BLE
CFLAGS += -DBLE_DEVICE_NAME="\"$(BLE_NAME)\""
# BLE heap size in bytes — WCH minimum is 6144 (6 KB); default was 10240.
# The working baseline used 6 KB. Keep that default unless a specific board
# needs more heap for BLE bring-up.
BLE_MEMHEAP_SIZE ?= 6144
CFLAGS += -DBLE_MEMHEAP_SIZE=$(BLE_MEMHEAP_SIZE)
# Max BLE packet length (ATT_MTU = BLE_BUFF_MAX_LEN - 4).
# 64 is the WCH default; fully adequate for HID keyboard/mouse reports.
BLE_BUFF_MAX_LEN ?= 64
CFLAGS += -DBLE_BUFF_MAX_LEN=$(BLE_BUFF_MAX_LEN)
# Max TX packets per connection event (1 is sufficient for HID traffic).
BLE_TX_NUM_EVENT ?= 1
CFLAGS += -DBLE_TX_NUM_EVENT=$(BLE_TX_NUM_EVENT)
# We only use peripheral role, no central connections needed.
CFLAGS += -DCENTRAL_MAX_CONNECTION=0
else
# Strip -lwchble from LIBS (defined in objects.mk) when BLE is disabled
LIBS := -lm
endif

$(BUILD_CONFIG_STAMP): FORCE
	@{ \
		printf 'BOARD_VERSION=%s\n' "$(BOARD_VERSION)"; \
		printf 'USB_WORK_MODE=%s\n' "$(USB_WORK_MODE)"; \
		printf 'ENABLE_UAC=%s\n' "$(ENABLE_UAC)"; \
		printf 'ENABLE_BLE=%s\n' "$(ENABLE_BLE)"; \
		printf 'BLE_NAME=%s\n' "$(BLE_NAME)"; \
		printf 'BLE_MEMHEAP_SIZE=%s\n' "$(BLE_MEMHEAP_SIZE)"; \
		printf 'BLE_BUFF_MAX_LEN=%s\n' "$(BLE_BUFF_MAX_LEN)"; \
		printf 'BLE_TX_NUM_EVENT=%s\n' "$(BLE_TX_NUM_EVENT)"; \
	} > "$@.tmp"
	@if test ! -f "$@" || test "$$(cat "$@.tmp")" != "$$(cat "$@")"; then \
		mv "$@.tmp" "$@"; \
	else \
		rm -f "$@.tmp"; \
	fi

$(OBJS): $(BUILD_CONFIG_STAMP)

# Tool invocations
openterface-km-firmware.elf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GNU RISC-V Cross C Linker'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g $(CFLAGS) -T "../HAL/Link.ld" -nostartfiles -Xlinker --gc-sections -L"../" -L"../LIB" -Xlinker --print-memory-usage -Wl,-Map,"openterface-km-firmware.map" --specs=nano.specs --specs=nosys.specs -o "openterface-km-firmware.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@
openterface-km-firmware.hex: openterface-km-firmware.elf
	@echo 'Invoking: GNU RISC-V Cross Create Binary Image'
	@	riscv-none-embed-objcopy -O ihex "openterface-km-firmware.elf" "openterface-km-firmware.hex"
	@echo 'Finished building $@'
	@
openterface-km-firmware.lst: openterface-km-firmware.elf
	@echo 'Invoking: GNU RISC-V Cross Create Listing'
	@	riscv-none-embed-objdump --source --all-headers --demangle -M xw --line-numbers --wide "openterface-km-firmware.elf" > "openterface-km-firmware.lst"
	@echo 'Finished building $@'
	@
openterface-km-firmware.siz: openterface-km-firmware.elf
	@echo 'Invoking: GNU RISC-V Cross Print Size'
	riscv-none-embed-size --format=berkeley "openterface-km-firmware.elf"
	@echo 'Finished building $@'
	@

# Other Targets
clean:
	-$(RM) $(OBJS)$(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE)$(S_DEPS)$(S_UPPER_DEPS)$(ASM_DEPS)$(ASM_UPPER_DEPS)$(C_DEPS) openterface-km-firmware.elf openterface-km-firmware.hex openterface-km-firmware.bin $(BUILD_CONFIG_STAMP)

secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents bin flash probe FORCE

FORCE:

-include ../makefile.targets