################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Define the source directory
SRC_DIR := $(abspath $(dir $(CURDIR)))

# Include other makefiles
-include ../makefile.init

RM := rm -rf

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
KeyMod.lst \

SECONDARY_SIZE += \
KeyMod.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: KeyMod.elf secondary-outputs

CFLAGS += -include stdint.h -include stddef.h

# Tool invocations
KeyMod.elf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GNU RISC-V Cross C Linker'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g $(CFLAGS) -T "../HAL/Link.ld" -nostartfiles -Xlinker --gc-sections -L"../" -L"../LIB" -Xlinker --print-memory-usage -Wl,-Map,"KeyMod.map" --specs=nano.specs --specs=nosys.specs -o "KeyMod.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@
openterface-km-firmware.bin: KeyMod.elf
	@echo 'Invoking: GNU RISC-V Cross Create Binary Image'
	@	riscv-none-embed-objcopy -O binary "KeyMod.elf" "openterface-km-firmware.bin"
	@echo 'Finished building $@'
	@
KeyMod.lst: KeyMod.elf
	@echo 'Invoking: GNU RISC-V Cross Create Listing'
	@	riscv-none-embed-objdump --source --all-headers --demangle -M xw --line-numbers --wide "KeyMod.elf" > "KeyMod.lst"
	@echo 'Finished building $@'
	@
KeyMod.siz: KeyMod.elf
	@echo 'Invoking: GNU RISC-V Cross Print Size'
	riscv-none-embed-size --format=berkeley "KeyMod.elf"
	@echo 'Finished building $@'
	@

# Other Targets
clean:
	-$(RM) $(OBJS)$(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE)$(S_DEPS)$(S_UPPER_DEPS)$(ASM_DEPS)$(ASM_UPPER_DEPS)$(C_DEPS) KeyMod.elf openterface-km-firmware.bin

secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets