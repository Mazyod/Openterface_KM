################################################################################
# MRS Version: 2.1.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../APP/USBLIB/CONFIG/hw_config.c \
../APP/USBLIB/CONFIG/usb_desc.c \
../APP/USBLIB/CONFIG/usb_endp.c \
../APP/USBLIB/CONFIG/usb_istr.c \
../APP/USBLIB/CONFIG/usb_prop.c \
../APP/USBLIB/CONFIG/usb_pwr.c 

C_DEPS += \
./APP/USBLIB/CONFIG/hw_config.d \
./APP/USBLIB/CONFIG/usb_desc.d \
./APP/USBLIB/CONFIG/usb_endp.d \
./APP/USBLIB/CONFIG/usb_istr.d \
./APP/USBLIB/CONFIG/usb_prop.d \
./APP/USBLIB/CONFIG/usb_pwr.d 

OBJS += \
./APP/USBLIB/CONFIG/hw_config.o \
./APP/USBLIB/CONFIG/usb_desc.o \
./APP/USBLIB/CONFIG/usb_endp.o \
./APP/USBLIB/CONFIG/usb_istr.o \
./APP/USBLIB/CONFIG/usb_prop.o \
./APP/USBLIB/CONFIG/usb_pwr.o 



# Each subdirectory must supply rules for building sources it contributes
APP/USBLIB/CONFIG/%.o: ../APP/USBLIB/CONFIG/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GNU RISC-V Cross C Compiler'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -g -DBLE_BUFF_MAX_LEN=516 -DCH32V20x_D8W -DBLE_MEMHEAP_SIZE=10240 -DBLE_TX_NUM_EVENT=5 -I"c:/Users/admin/projects/Openterface_KM/SRC/Core" -I"c:/Users/admin/projects/Openterface_KM/SRC/Startup" -I"c:/Users/admin/projects/Openterface_KM/SRC/Debug" -I"c:/Users/admin/projects/Openterface_KM/SRC/Peripheral/inc" -I"c:/Users/admin/projects/Openterface_KM/APP/include" -I"c:/Users/admin/projects/Openterface_KM/Profile/include" -I"c:/Users/admin/projects/Openterface_KM/HAL/include" -I"c:/Users/admin/projects/Openterface_KM/LIB" -I"c:/Users/admin/projects/Openterface_KM/APP/UART" -I"c:/Users/admin/projects/Openterface_KM/APP/USBLIB/CONFIG" -I"c:/Users/admin/projects/Openterface_KM/APP/USBLIB/USB-Driver/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@
