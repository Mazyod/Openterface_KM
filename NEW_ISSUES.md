# CH9329 待修复问题

## 问题 1: 串口多波特率不工作

### 现象
- 串口在 9600 波特率下可以正常收发 CH9329 命令和响应
- 在 19200、38400、57600、115200 等其他波特率下无响应

### 根本原因分析
当主机改变串口波特率时：
1. 主机发送 `CDC_SET_LINE_CODING` USB 控制请求
2. 设备在 `USBD_Status_In()` 中收到此请求后调用 `UART2_USB_Init()`
3. `UART2_USB_Init()` 重新配置 UART2 硬件

可能的问题：
- `UART2_USB_Init()` 重新初始化 UART2 时可能中断了正在进行的 CH9329 命令处理
- 波特率切换时存在时序问题，主机在设备准备好之前就发送了数据
- `CDC_SET_LINE_CODING` 处理可能影响了 EP5 OUT 的接收状态

### 测试验证
```python
# 测试代码: test_comprehensive.py - Test 3
# 结果:
# Serial 9600: ✓ received response
# Serial 19200: ✗ no response
# Serial 38400: ✗ no response
# Serial 57600: ✗ no response
# Serial 115200: ✗ no response
```

### 修复建议
1. 检查 `UART2_USB_Init()` 的实现，确保重新初始化不会破坏正在进行的通信
2. 在波特率切换后添加适当的延迟或同步机制
3. 确保 EP5 OUT 端点在波特率切换后仍然处于正确的接收状态
4. 考虑在 `CDC_SET_LINE_CODING` 处理中添加调试输出，跟踪实际执行情况

### 相关文件
- `APP/USBLIB/CONFIG/usb_prop.c` - CDC_SET_LINE_CODING 处理
- `APP/Uart.c` 或类似文件 - UART2_USB_Init() 实现
- `APP/usbd_compostie_km.c` - USB_DataRx_To_KMHandle()

---

## 问题 2: 大包响应失败（Get Para Cfg 命令）

### 现象
- 发送 Get Para Cfg (0x08) 命令后没有响应
- Get Info (0x01) 等小响应命令正常工作

### 根本原因
- Get Para Cfg 响应数据为 50 字节
- 加上 CH9329 帧头（6 字节），总包长 56 字节
- EP3 IN 端点的最大包长为 16 字节（`DEF_USBD_MAX_PACK_SIZE = 16`）
- `USBD_ENDPx_DataUp()` 函数检查 `len > max_pack` 时返回 `USB_ERROR`
- 响应被静默丢弃，没有任何错误提示

### 代码路径
```c
// usbd_compostie_km.c - CH9329_SendResponse
// 创建 56 字节的响应包

// km_ring.c - USB_SendFromQueues
// 调用 USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len)

// usb_endp.c - USBD_ENDPx_DataUp
uint16_t max_pack = DEF_USBD_MAX_PACK_SIZE;  // = 16
if( len > max_pack ) return USB_ERROR;        // 56 > 16 → 失败
```

### 测试验证
```python
# 测试代码: test_comprehensive.py - Test 4
# 结果:
# Get Info (0x01): ✓ response 14 bytes
# Get Para Cfg (0x08): ✗ no response
```

### 修复建议

**方案 1: 实现响应分包（推荐）**
在 `USB_SendFromQueues()` 或 `CH9329_SendResponse()` 中将大包拆分为多个小包：
```c
// 伪代码
if (pkt->len > max_packet_size) {
    // 发送第一个包
    USBD_ENDPx_DataUp(ENDP3, pkt->data, max_packet_size);
    // 等待完成
    while (USBD_Endp3_Busy) { /* wait */ }
    
    // 发送剩余数据
    uint16_t offset = max_packet_size;
    while (offset < pkt->len) {
        uint16_t chunk_size = min(max_packet_size, pkt->len - offset);
        USBD_ENDPx_DataUp(ENDP3, pkt->data + offset, chunk_size);
        while (USBD_Endp3_Busy) { /* wait */ }
        offset += chunk_size;
    }
}
```

**方案 2: 增大 EP3 最大包长**
修改 USB 描述符中 EP3 IN 的 wMaxPacketSize：
```c
// usb_desc.c
/* EP3 in endpoint descriptor */
0x07,                       /* bLength */
USB_ENDPOINT_DESCRIPTOR_TYPE, /* bDescriptorType */
0x83,                       /* bEndpointAddress */
0x02,                       /* bmAttributes: Bulk */
LOBYTE(64), HIBYTE(64),     /* wMaxPacketSize: 改为 64 */
0x00,                       /* bInterval */
```

**注意：** 方案 2 需要确认 CH32V20x 硬件是否支持更大的包长，并且需要确保主机端驱动也支持。

### 相关文件
- `APP/USBLIB/CONFIG/usb_endp.c` - USBD_ENDPx_DataUp()
- `APP/usbd_compostie_km.c` - CH9329_SendResponse()
- `APP/km_ring.c` - USB_SendFromQueues()
- `APP/USBLIB/CONFIG/usb_desc.c` - USB 描述符

---

## 优先级建议

1. **高优先级**: 问题 2（大包响应）- 影响多个重要命令（Get Para Cfg、Get Serial Status 等）
2. **中优先级**: 问题 1（多波特率）- 影响串口兼容性，但 pyusb 直接访问可以绕过

---

## 调试建议

### 问题 1 调试
在 `CDC_SET_LINE_CODING` 处理中添加调试输出：
```c
USB_DEBUG("CDC_SET_LINE_CODING: baud=%d\n", LineCoding.dwDTERate);
```

检查 UART2 配置：
```c
USB_DEBUG("UART2_Init: baud=%d stop=%d parity=%d\n", 
    LineCoding.dwDTERate, 
    LineCoding.bCharFormat,
    LineCoding.bParityType);
```

### 问题 2 调试
在 `USBD_ENDPx_DataUp()` 中添加错误输出：
```c
if( len > max_pack ) {
    USB_DEBUG("USBD_ENDPx_DataUp: len=%d > max=%d\n", len, max_pack);
    return USB_ERROR;
}
```

---

*文档创建时间: 2026-09-14*
*基于: COMPREHENSIVE_TEST_REPORT.md*
