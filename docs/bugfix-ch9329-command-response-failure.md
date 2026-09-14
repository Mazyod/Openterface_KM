# CH9329 命令响应失败问题分析报告

**日期**: 2026-09-11  
**固件版本**: openterface-km-firmware-kvmgo  
**问题**: 设备无法响应 CH9329 命令（Get Info 0x01 等）

## 问题描述

设备烧录固件后，能够正常枚举为 USB CDC ACM 设备，但无法响应任何 CH9329 命令。鼠标键盘数据正常工作，说明 USB 连接本身是正常的。

## 错误修复过程

### 第一次尝试（失败）

**假设**: 端点状态切换冲突

认为 `SetEPTxStatus` 的 toggle 行为导致端点状态混乱，修改了以下代码：

1. **usb_endp.c** - 使用直接寄存器写入替代 `SetEPTxStatus`
   ```c
   // 修改前
   SetEPTxStatus(ENDP3, EP_TX_VALID);
   
   // 修改后
   USBFSD->UEP3_TX_CTRL = (USBFSD->UEP3_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
   ```

2. **usb_prop.c** - 在 HID_ACM 模式下立即启用 EP3
   ```c
   if (USB_ModeConfig_ActiveModeHasHostCdcAcm() && !USB_ModeConfig_ActiveModeBridgeAcm())
   {
       SetEPTxStatus(ENDP3, EP_TX_NAK);
   }
   ```

3. **km_ring.c** - 添加双重忙标志检查
   ```c
   if (USBFS_Endp_Busy[DEF_UEP3] == 0 && 
       USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0)
   ```

**结果**: 设备仍然无法响应 CH9329 命令

### 第二次尝试（失败）

**假设**: 条件编译错误

认为 `EP5_OUT_Callback` 的条件编译没有包含 HID_ACM 模式，添加了 `USB_WORK_MODE_HAS_HOST_CDC_ACM` 条件。

**结果**: 设备仍然无法响应

### 根本原因分析

用户指出 merge commit `bf0a4b5` 的版本是可以正常工作的。通过比较发现：

**关键差异**:

1. **USB Endpoint Busy Timeout Recovery 机制**
   - 当前代码（c9f1cb8 commit）添加了端点忙超时恢复机制
   - merge commit 版本没有这个机制
   - 这个机制可能在某些情况下干扰端点状态

2. **EP3 初始化时机**
   - merge commit 版本：EP3 在 USBD_Reset 中初始化为 `EP_TX_DIS`
   - 然后在 SET_LINE_CODING 命令处理时才启用为 `EP_TX_NAK`
   - 修改版本：在 USBD_Reset 中直接启用 EP3

3. **数据流路径**
   - HID_ACM 模式下，EP5_OUT_Callback 走 `#else` 分支
   - 数据被放入 `UART2_Tx_Buf`
   - 通过 `USB_DataRx_To_KMHandle()` 处理
   - 调用 `CH9329_RxBuffer_Add()` 解析命令
   - 响应通过 EP3 发送

## 根本原因

**USB Endpoint Busy Timeout Recovery 机制干扰了正常的端点状态管理**

这个机制在 `ch32v20x_usbfs_device.c` 中添加了：
- `USBFS_Endp_Busy_StartTime[]` - 记录端点忙开始时间
- `USBFS_CheckEndpointBusyTimeout()` - 检查并恢复超时的端点

在主循环中调用 `USBFS_CheckEndpointBusyTimeout()`，可能会在不合适的时候清除忙标志，导致端点状态混乱。

## 解决方案

恢复到 merge commit `bf0a4b5` 的版本，移除 USB Endpoint Busy Timeout Recovery 机制：

1. 恢复 `usb_endp.c` 到原始版本
2. 恢复 `usb_prop.c` 到原始版本
3. 移除 `USBFS_CheckEndpointBusyTimeout()` 相关代码

## 验证结果

恢复到 merge commit 版本后，设备能够正常响应 CH9329 命令。

## 经验教训

1. **不要过度优化**: 原有的端点状态管理已经足够稳定，不需要添加额外的超时恢复机制

2. **理解现有代码**: 在修改之前，需要充分理解现有代码的工作流程和数据流路径

3. **增量修改**: 应该一次只修改一个方面，而不是同时修改多个地方

4. **参考工作版本**: 当遇到问题时，应该先找到一个已知的工作版本作为参考

## 相关文件

- `APP/USBLIB/CONFIG/usb_endp.c` - 端点处理
- `APP/USBLIB/CONFIG/usb_prop.c` - USB 属性配置
- `APP/ch32v20x_usbfs_device.c` - USB 设备驱动
- `APP/usbd_compostie_km.c` - CH9329 命令处理
- `APP/km_ring.c` - 键盘鼠标数据队列管理

## Commit 信息

- **问题引入**: `c9f1cb8` - feat: implement USB endpoint busy timeout recovery mechanism
- **工作版本**: `bf0a4b5` - Merge branch 'fix/p0-stability-issues'
