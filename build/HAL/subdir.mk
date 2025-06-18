################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../HAL/MCU.c \
../HAL/RTC.c \
../HAL/SLEEP.c 

C_DEPS += \
./HAL/MCU.d \
./HAL/RTC.d \
./HAL/SLEEP.d 

OBJS += \
./HAL/MCU.o \
./HAL/RTC.o \
./HAL/SLEEP.o 



# Each subdirectory must supply rules for building sources it contributes
HAL/%.o: ../HAL/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GNU RISC-V Cross C Compiler'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DBLE_BUFF_MAX_LEN=516 -DCH32V20x_D8W -DBLE_MEMHEAP_SIZE=10240 -DBLE_TX_NUM_EVENT=5 -I"c:/Users/admin/projects/Openterface_KM/SRC/Core" -I"c:/Users/admin/projects/Openterface_KM/SRC/Startup" -I"c:/Users/admin/projects/Openterface_KM/SRC/Debug" -I"c:/Users/admin/projects/Openterface_KM/SRC/Peripheral/inc" -I"c:/Users/admin/projects/Openterface_KM/APP/include" -I"c:/Users/admin/projects/Openterface_KM/Profile/include" -I"c:/Users/admin/projects/Openterface_KM/HAL/include" -I"c:/Users/admin/projects/Openterface_KM/LIB" -I"c:/Users/admin/projects/Openterface_KM/APP/UART" -I"c:/Users/admin/projects/Openterface_KM/APP/USBLIB/CONFIG" -I"c:/Users/admin/projects/Openterface_KM/APP/USBLIB/USB-Driver/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@
