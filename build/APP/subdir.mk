################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/RingMem.c \
../APP/RGB.c \
../APP/SD_SWITCH.c \
../APP/DS18B20.c \
../APP/CH32_TEMP.c \
../APP/ch32v20x_it.c \
../APP/ch32v20x_usbfs_device.c \
../APP/keyboard_handler.c \
../APP/mouse_handler.c \
../APP/peripheral.c \
../APP/peripheral_main.c \
../APP/system_ch32v20x.c \
../APP/usbd_compostie_km.c \
../APP/usbd_desc.c 

C_DEPS += \
./APP/RingMem.d \
./APP/RGB.d \
./APP/SD_SWITCH.d \
./APP/DS18B20.d \
./APP/CH32_TEMP.d \
./APP/ch32v20x_it.d \
./APP/ch32v20x_usbfs_device.d \
./APP/keyboard_handler.d \
./APP/mouse_handler.d \
./APP/peripheral.d \
./APP/peripheral_main.d \
./APP/system_ch32v20x.d \
./APP/usbd_compostie_km.d \
./APP/usbd_desc.d

OBJS += \
./APP/RingMem.o \
./APP/RGB.o \
./APP/SD_SWITCH.o \
./APP/DS18B20.o \
./APP/CH32_TEMP.o \
./APP/ch32v20x_it.o \
./APP/ch32v20x_usbfs_device.o \
./APP/keyboard_handler.o \
./APP/mouse_handler.o \
./APP/peripheral.o \
./APP/peripheral_main.o \
./APP/system_ch32v20x.o \
./APP/usbd_compostie_km.o \
./APP/usbd_desc.o

# Each subdirectory must supply rules for building sources it contributes
APP/%.o: ../APP/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GNU RISC-V Cross C Compiler'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DBLE_BUFF_MAX_LEN=516 -DCH32V20x_D8W -DBLE_MEMHEAP_SIZE=10240 -DBLE_TX_NUM_EVENT=5 -I"$(SRC_DIR)/SRC/Core" -I"$(SRC_DIR)/SRC/Startup" -I"$(SRC_DIR)/SRC/Debug" -I"$(SRC_DIR)/SRC/Peripheral/inc" -I"$(SRC_DIR)/APP/include" -I"$(SRC_DIR)/Profile/include" -I"$(SRC_DIR)/HAL/include" -I"$(SRC_DIR)/LIB" -I"$(SRC_DIR)/APP/UART" -I"$(SRC_DIR)/APP/USBLIB/CONFIG" -I"$(SRC_DIR)/APP/USBLIB/USB-Driver/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@
