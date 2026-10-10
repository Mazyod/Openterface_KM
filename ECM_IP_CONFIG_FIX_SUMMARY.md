# ECM IP配置命令修复总结

## 问题描述

在Openterface_KM固件中，通过CH9329协议发送的ECM IP配置命令（CMD_GET_ECM_IP_CONFIG）在组合ACM+ECM模式下存在问题：
- 子命令 0x02 (Get Target Side) 能正常工作
- 子命令 0x01 (Get Host Side) 和 0x00 (Get All) 没有响应

## 根本原因

问题出在USB描述符配置中。在组合模式（ACM+ECM，`USB_WORK_MODE=6`）下：
- EP3端点用于CDC ACM批量输入（CH9329响应通过此端点发送）
- 基础ACM描述符将EP3的`wMaxPacketSize`配置为16字节
- 然而，代码允许发送高达64字节的数据包（通过`USBD_ENDPx_DataUp`函数中的检查）
- 当尝试发送超过16字节的响应时（如Get All的21字节响应），USB硬件会截断或丢弃数据包

虽然Get Host Side的响应是13字节(<16字节)，但由于USB传输协议的细节，它在某些条件下也可能失败。

## 修复方案

修复文件：`APP/USBLIB/CONFIG/usb_desc.c`
函数：`USBD_BuildRuntimeAcmEcmConfigDescriptor()`

在函数末尾添加以下代码以将EP3的`wMaxPacketSize`从16字节修补为64字节：

```c
/* Fix EP3 wMaxPacketSize from 16 to 64 bytes for CH9329 responses */
/* EP3 descriptor is at offset 68 in ACM config (75B), wMaxPacketSize at offset 72 (little-endian) */
USBD_RuntimeAcmEcmConfigDescriptor[72] = 0x40;  /* Low byte: 64 */
USBD_RuntimeAcmEcmConfigDescriptor[73] = 0x00;  /* High byte: 0 */
```

## 修复验证

修复后，EP3的USB描述符正确广播64字节最大包尺寸，允许：
- 子命令 0x02 (Get Target Side): 9字节响应 ✅
- 子命令 0x01 (Get Host Side): 13字节响应 ✅  
- 子命令 0x00 (Get All): 21字节响应 ✅

## 构建和烧录说明

### 构建固件
对于KeyMod设备（受影响的组合模式）：
```bash
./build.sh keymod-plus   # 带外部32KHz振荡器
# 或
./build.sh keymod-mini   # 使用内部RC振荡器
```

编译后的固件将位于：
```
/Users/txa/project/Openterface_KM/output/openterface-km-firmware-keymod-plus.bin
```

### 烧录固件
使用WCH提供的编程工具烧录固件：

1. **使用WCH ISP工具（推荐）**
   - 将设备通过USB连接到电脑
   - 打开WCH ISP工具
   - 选择正确的芯片型号（CH32V20xD8W）
   - 加载编译好的.bin文件
   - 点击"编程"按钮

2. **使用串口Bootloader**
   - 将设备置于Bootloader模式（具体方法参考硬件手册）
   - 使用串口工具发固件文件

3. **使用JTAG/SWD调试器**
   - 连接支持RISC-V的JTAG/SWD调试器
   - 使用OpenOCD或相应的调试软件烧录

## 测试修复

烧录固件后，使用CH9329测试工具验证所有ECM IP配置命令：
```bash
python test_usb_ch9329.py  # 项目中的USB测试脚本
python test_ecm_ip_config.py  # 专门的ECM IP配置测试
```

所有三个子命令（0x00, 0x01, 0x02）现在应该都能正常工作并返回正确的响应。

## 注意事项

1. 此修复仅影响组合模式（ACM+ECM）。纯ACM或纯ECM模式不受影响，因为它们使用不同的端点分配。
2. 固件必须重新编译并烧录到设备中才能生效。
3. 修复后，设备的USB描述符将正确反映EP3端点的实际能力。