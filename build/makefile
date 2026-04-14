################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Define the source directory
SRC_DIR := $(abspath $(dir $(CURDIR)))

# Include other makefiles
-include ../makefile.init

RM := rm -rf

# Feature flags — define BEFORE includes so subdir.mk files can use them
# ETH support: build with 'make ENABLE_ETH=1' for CDC-ECM USB Ethernet on the
# legacy USBD controller (separate physical USB port from HID).
# UAC is forced off when ETH is enabled: ECM frame reassembly and the UAC
# ping-pong buffers together exceed available RAM (512B PMA is also full).
ENABLE_ETH ?= 0
ifeq ($(ENABLE_ETH),1)
CFLAGS += -DENABLE_ETH
override ENABLE_UAC := 0
endif

# BLE support: build with 'make ENABLE_BLE=0' to omit BLE (frees ~14 KB RAM)
ENABLE_BLE ?= 1

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
openterface-km-firmware.bin \

SECONDARY_LIST += \
openterface-km-firmware.lst \

SECONDARY_SIZE += \
openterface-km-firmware.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: openterface-km-firmware.elf secondary-outputs

CFLAGS += -include stdint.h -include stddef.h

# UAC support: build with 'make ENABLE_UAC=0' to omit USB Audio Class (frees EP5+EP6)
ENABLE_UAC ?= 1
ifeq ($(ENABLE_UAC),1)
CFLAGS += -DENABLE_UAC
endif

ifeq ($(ENABLE_BLE),1)
CFLAGS += -DENABLE_BLE
# BLE heap size in bytes — WCH minimum is 6144 (6 KB); default was 10240.
# Smaller values save static RAM. Increase only if BLE_LibInit fails at boot.
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

# Tool invocations
openterface-km-firmware.elf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GNU RISC-V Cross C Linker'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g $(CFLAGS) -T "../HAL/Link.ld" -nostartfiles -Xlinker --gc-sections -L"../" -L"../LIB" -Xlinker --print-memory-usage -Wl,-Map,"openterface-km-firmware.map" --specs=nano.specs --specs=nosys.specs -o "openterface-km-firmware.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@
openterface-km-firmware.bin: openterface-km-firmware.elf
	@echo 'Invoking: GNU RISC-V Cross Create Binary Image'
	@	riscv-none-embed-objcopy -O binary "openterface-km-firmware.elf" "openterface-km-firmware.bin"
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
	-$(RM) $(OBJS)$(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE)$(S_DEPS)$(S_UPPER_DEPS)$(ASM_DEPS)$(ASM_UPPER_DEPS)$(C_DEPS) openterface-km-firmware.elf openterface-km-firmware.bin openterface-km-firmware.hex openterface-km-firmware.ihex

secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets