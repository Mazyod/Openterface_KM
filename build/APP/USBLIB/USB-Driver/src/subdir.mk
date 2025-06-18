################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/USBLIB/USB-Driver/src/usb_core.c \
../APP/USBLIB/USB-Driver/src/usb_init.c \
../APP/USBLIB/USB-Driver/src/usb_int.c \
../APP/USBLIB/USB-Driver/src/usb_mem.c \
../APP/USBLIB/USB-Driver/src/usb_regs.c \
../APP/USBLIB/USB-Driver/src/usb_sil.c 

C_DEPS += \
./APP/USBLIB/USB-Driver/src/usb_core.d \
./APP/USBLIB/USB-Driver/src/usb_init.d \
./APP/USBLIB/USB-Driver/src/usb_int.d \
./APP/USBLIB/USB-Driver/src/usb_mem.d \
./APP/USBLIB/USB-Driver/src/usb_regs.d \
./APP/USBLIB/USB-Driver/src/usb_sil.d 

OBJS += \
./APP/USBLIB/USB-Driver/src/usb_core.o \
./APP/USBLIB/USB-Driver/src/usb_init.o \
./APP/USBLIB/USB-Driver/src/usb_int.o \
./APP/USBLIB/USB-Driver/src/usb_mem.o \
./APP/USBLIB/USB-Driver/src/usb_regs.o \
./APP/USBLIB/USB-Driver/src/usb_sil.o 



# Each subdirectory must supply rules for building sources it contributes
APP/USBLIB/USB-Driver/src/%.o: ../APP/USBLIB/USB-Driver/src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GNU RISC-V Cross C Compiler'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DBLE_BUFF_MAX_LEN=516 -DCH32V20x_D8W -DBLE_MEMHEAP_SIZE=10240 -DBLE_TX_NUM_EVENT=5 -I"c:/Users/admin/projects/Openterface_KM/SRC/Core" -I"c:/Users/admin/projects/Openterface_KM/SRC/Startup" -I"c:/Users/admin/projects/Openterface_KM/SRC/Debug" -I"c:/Users/admin/projects/Openterface_KM/SRC/Peripheral/inc" -I"c:/Users/admin/projects/Openterface_KM/APP/include" -I"c:/Users/admin/projects/Openterface_KM/Profile/include" -I"c:/Users/admin/projects/Openterface_KM/HAL/include" -I"c:/Users/admin/projects/Openterface_KM/LIB" -I"c:/Users/admin/projects/Openterface_KM/APP/UART" -I"c:/Users/admin/projects/Openterface_KM/APP/USBLIB/CONFIG" -I"c:/Users/admin/projects/Openterface_KM/APP/USBLIB/USB-Driver/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@
