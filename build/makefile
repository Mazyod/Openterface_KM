################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################
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
BLE_USB.hex \

SECONDARY_LIST += \
BLE_USB.lst \

SECONDARY_SIZE += \
BLE_USB.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: BLE_USB.elf secondary-outputs

# Tool invocations
BLE_USB.elf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GNU RISC-V Cross C Linker'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -T "c:/Users/admin/projects/Openterface_KM/HAL/Link.ld" -nostartfiles -Xlinker --gc-sections -L"c:\Users\admin\projects\Openterface_KM" -L"c:/Users/admin/projects/Openterface_KM/LIB" -Xlinker --print-memory-usage -Wl,-Map,"BLE_USB.map" --specs=nano.specs --specs=nosys.specs -o "BLE_USB.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@
BLE_USB.hex: BLE_USB.elf
	@echo 'Invoking: GNU RISC-V Cross Create Flash Image'
	@	riscv-none-embed-objcopy -O ihex "BLE_USB.elf" "BLE_USB.hex"
	@echo 'Finished building $@'
	@
BLE_USB.lst: BLE_USB.elf
	@echo 'Invoking: GNU RISC-V Cross Create Listing'
	@	riscv-none-embed-objdump --source --all-headers --demangle -M xw --line-numbers --wide "BLE_USB.elf" > "BLE_USB.lst"
	@echo 'Finished building $@'
	@
BLE_USB.siz: BLE_USB.elf
	@echo 'Invoking: GNU RISC-V Cross Print Size'
	riscv-none-embed-size --format=berkeley "BLE_USB.elf"
	@echo 'Finished building $@'
	@

# Other Targets
clean:
	-$(RM) $(OBJS)$(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE)$(S_DEPS)$(S_UPPER_DEPS)$(ASM_DEPS)$(ASM_UPPER_DEPS)$(C_DEPS) BLE_USB.elf

secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets