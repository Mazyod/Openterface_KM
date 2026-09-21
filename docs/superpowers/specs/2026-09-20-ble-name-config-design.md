# BLE 名称动态配置设计文档

> 支持硬件型号编译时区分（KVM-GO 启用 / MiniKVM 完全禁用）

---

## 1. 概述

本文档描述在 Openterface_KM 固件中实现通过 CH9329 指令动态修改蓝牙设备名称的功能，支持：

- **持久化存储**：名称写入 Flash，掉电重启后保持
- **恢复出厂设置**：一条指令将名称恢复为默认值 "KVM-GO"
- **硬件型号裁剪**：通过 `VERSION_SELECTION` 宏在编译期完全移除 MiniKVM 固件中的本特性代码

**核心原则**：零运行时开销、零死代码、单一代码库、最小化新增代码量。

---

## 2. 硬件型号与特性矩阵

| 特性 | KVM-GO | MiniKVM |
|------|--------|---------|
| 蓝牙 (BLE) | ✅ 支持 | ❌ 不支持 |
| USB 复合设备 (HID/CDC/ECM等) | ✅ 支持 | ❌ 不支持 |
| 串口模式 (CDC ACM) | ✅ 支持 | ✅ 支持 |
| CH9329 完整指令集 | ✅ 支持 | ❌ 精简版（仅串口相关） |
| **BLE 名称动态配置** | **✅ 启用** | **❌ 完全禁用** |

**关键决策**：在编译期通过 `VERSION_SELECTION` 宏彻底移除 MiniKVM 固件中的所有 BLE 名称配置相关代码，实现：
- 零 Flash 空间占用（代码 + 数据）
- 无无效的 CH9329 命令处理分支
- 无额外 Flash 存储依赖
- 维护单一代码库

---

## 3. 编译时裁剪架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    Version_selection.h                           │
│  #define VERSION_SELECTION  VERSION_KVM_GO   // 或 VERSION_MINIKVM│
└────────────────────────────┬────────────────────────────────────┘
                             │
              ┌──────────────┴──────────────┐
              ▼                             ▼
    ┌─────────────────────┐       ┌─────────────────────┐
    │   KVM-GO 编译路径    │       │  MiniKVM 编译路径    │
    │  (VERSION_KVM_GO=1) │       │  (VERSION_MINIKVM=2)│
    ├─────────────────────┤       ├─────────────────────┤
    │ ENABLE_BLE          = 1     │ ENABLE_BLE          = 0 │
    │ ENABLE_USB_COMPOSITE= 1     │ ENABLE_USB_COMPOSITE= 0 │
    │ ENABLE_BLE_NAME_CONFIG=1    │ ENABLE_BLE_NAME_CONFIG=0│
    └──────────┬──────────┘       └──────────┬──────────┘
               │                             │
    ┌──────────┴──────────┐       ┌─────────┴─────────┐
    ▼                     ▼       ▼                   ▼
ble_name_config.c    usbd_composite_km.c   (文件不参与编译)
(完整实现)            (包含 CMD_BLE_SET_NAME)  (无任何 BLE 相关代码)
    │                     │
    └──────────┬──────────┘
               ▼
    ┌─────────────────────┐
    │   最终固件镜像       │
    │  - KVM-GO: 含 BLE   │
    │    名称配置功能      │
    │  - MiniKVM: 纯串口   │
    │    无任何 BLE 代码   │
    └─────────────────────┘
```

---

## 4. 核心实现

### 4.1 数据结构设计

**Flash 存储记录**

```c
typedef struct {
    uint32_t magic;           // 魔数: 0x424C454E ("BLEN" in ASCII)
    uint8_t  record_version;  // 记录版本号 (当前固定为 1)
    uint8_t  name[21];        // 名称缓冲区 (最多 20 字符 + null 终止符)
    uint8_t  name_length;     // 实际名称长度 (不包括 null 终止符)
    uint32_t sequence;        // 序列号 (防止旧配置覆盖新配置)
    uint32_t crc;             // 配置数据的 CRC32 校验值 (不包括 crc 字段本身)
} __attribute__((packed)) BLE_NameConfig_Record_t;
```

**运行时状态**

```c
typedef struct {
    char     name[21];        // 当前生效的名称 (null 终止字符串)
    bool     is_custom;       // 标记是否使用自定义名称 (false=出厂默认)
} BLE_NameConfig_t;

extern BLE_NameConfig_t g_ble_name;
```

### 4.2 Flash 地址分配

```c
#define BLE_NAME_CONFIG_MAGIC             0x424C454Eu  // "BLEN"
#define BLE_NAME_CONFIG_VERSION           1u
#define BLE_NAME_CONFIG_FLASH_BASE        0x08000000u
#define BLE_NAME_CONFIG_FLASH_SIZE        (448u * 1024u)  // 448KB
#define BLE_NAME_CONFIG_FLASH_PAGE_SIZE   4096u           // 4KB 页

// USB Mode Config 占用最后 4KB       (0x0806F000-0x0806FFFF)
// Default Target Config 占用倒数第二 4KB (0x0806E000-0x0806EFFF)
// BLE Name Config 占用倒数第三 4KB   (0x0806D000-0x0806DFFF)
#define BLE_NAME_CONFIG_FLASH_ADDR      (BLE_NAME_CONFIG_FLASH_BASE + \
                                         BLE_NAME_CONFIG_FLASH_SIZE - \
                                         3 * BLE_NAME_CONFIG_FLASH_PAGE_SIZE)
```

> **注意**：此地址 (0x0806D000) 在默认目标配置 (0x0806E000) 之前 4KB，确保不覆盖现有配置。

### 4.3 接口声明（专业级，带完整注释）

```c
// APP/include/ble_name_config.h
#ifndef _BLE_NAME_CONFIG_H
#define _BLE_NAME_CONFIG_H

#if (ENABLE_BLE_NAME_CONFIG == 1)

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化 BLE 名称配置模块
 * @details
 *   - 从 Flash 加载已保存的自定义名称（如果存在且有效）
 *   - 如果 Flash 中无有效配置，则使用出厂默认名称 "KVM-GO"
 *   - 初始化运行时状态结构 @ref g_ble_name
 *   - 此函数必须在 BLE 协议栈初始化之前被调用（即在 Peripheral_Init() 早期）
 *   - 【编译期保证】仅在 ENABLE_BLE_NAME_CONFIG=1 (KVM-GO) 时存在
 * @return  none
 * @note    MiniKVM 固件编译时，此函数声明不存在，调用代码被预处理移除
 */
void BLE_NameConfig_Init(void);

/**
 * @brief 设置新的蓝牙设备名称并持久化存储
 * @details
 *   - 验证输入名称：长度必须在 1-20 字节之间（不包括 null 终止符）
 *   - 如果名称有效，则擦除 Flash 页面并写入新配置记录
 *   - 写入成功后，自动更新运行时状态、广播数据和 GAP 特性
 *   - 名称更改将在下次广播周期中生效（通过重启广播实现即时生效）
 *   - 如果名称无效或 Flash 写入失败，则保持当前名称不变
 * @param   new_name  指向 null 终止字符串的指针，包含要设置的新名称
 * @return  true  如果名称成功设置并持久化
 * @return  false 如果名称无效（长度超限或空指针）或 Flash 操作失败
 * @note    此函数不可中断；调用者应确保在非中断上下文中执行
 * @note    【编译期保证】仅在 ENABLE_BLE_NAME_CONFIG=1 (KVM-GO) 时存在
 */
bool BLE_NameConfig_Set(const char *new_name);

/**
 * @brief 获取当前生效的蓝牙设备名称
 * @details
 *   - 返回指向内部名称缓冲区的只读指针
 *   - 返回的字符串始终是 null 终止的
 *   - 不得修改返回的缓冲区内容
 * @return  指向当前名称的 const char* 指针
 * @note    返回值在调用 BLE_NameConfig_Set() 或 BLE_NameConfig_Reset() 后可能变化
 * @note    【编译期保证】仅在 ENABLE_BLE_NAME_CONFIG=1 (KVM-GO) 时存在
 */
const char *BLE_NameConfig_Get(void);

/**
 * @brief 恢复蓝牙设备名称为出厂默认值
 * @details
 *   - 擦除 Flash 页面中的 BLE 名称配置记录
 *   - 重新初始化运行时状态为出厂默认名称 "KVM-GO"
 *   - 更新广播数据和 GAP 特性以反映默认名称
 *   - 重启广播使更改立即生效
 *   - 此操作是幂等的：多次调用效果同上
 * @return  true  如果 Flash 擦除成功
 * @return  false 如果 Flash 擦除失败（罕见，仅在硬件故障时发生）
 * @note    此函数不可中断；调用者应确保在非中断上下文中执行
 * @note    【编译期保证】仅在 ENABLE_BLE_NAME_CONFIG=1 (KVM-GO) 时存在
 */
bool BLE_NameConfig_Reset(void);

/**
 * @brief 检查当前名称是否为自定义名称（非出厂默认）
 * @details
 *   - 方便上层逻辑判断是否需要显示"已修改"状态等
 * @return  true  如果当前名称是通过 BLE_NameConfig_Set() 设置的自定义名称
 * @return  false 如果当前名称是出厂默认名称 "KVM-GO"
 * @note    依赖于 BLE_NameConfig_Init() 已被正确调用
 * @note    【编译期保证】仅在 ENABLE_BLE_NAME_CONFIG=1 (KVM-GO) 时存在
 */
bool BLE_NameConfig_IsCustom(void);

#endif /* ENABLE_BLE_NAME_CONFIG */

/**
 * @brief 空实现宏（MiniKVM 编译时用于消除调用站点代码）
 * @details
 *   当 ENABLE_BLE_NAME_CONFIG=0 时，提供空定义使调用代码编译通过但不产生指令
 *   避免在 peripheral.c 等文件中需要 #ifdef 包裹调用
 */
#if (ENABLE_BLE_NAME_CONFIG == 0)
    #define BLE_NameConfig_Init()           do {} while(0)
    #define BLE_NameConfig_Set(x)           (false)
    #define BLE_NameConfig_Get()            ("KVM-GO")
    #define BLE_NameConfig_Reset()          (false)
    #define BLE_NameConfig_IsCustom()       (false)
    typedef struct { char name[21]; bool is_custom; } BLE_NameConfig_t;
    extern BLE_NameConfig_t g_ble_name;  // 仍需声明避免编译错误，但不可用
#endif

#endif /* _BLE_NAME_CONFIG_H */
```

---

## 5. 集成修改清单

### 5.1 文件变更总览

| 文件路径 | 修改类型 | 说明 |
|----------|----------|------|
| `APP/include/ble_name_config.h` | **新增** | 接口声明文件（含编译期防护和空宏定义） |
| `APP/ble_name_config.c` | **新增** | 核心实现文件（整体被 `#if (ENABLE_BLE_NAME_CONFIG)` 包裹） |
| `APP/peripheral.c` | **修改** | 添加包含和初始化调用 |
| `APP/usbd_compostie_km.c` | **修改** | 添加命令处理函数 |
| `APP/include/usbd_composite_km.h` | **修改** | 添加命令定义 |

**总计**：2 个新文件 + 3 个文件微改，核心逻辑 < 150 行

### 5.2 Version_selection.h 特性宏映射

```c
// 在现有 VERSION_SELECTION 定义之后添加：
#if (VERSION_SELECTION == VERSION_KVM_GO)
    #define ENABLE_BLE               1
    #define ENABLE_USB_COMPOSITE     1
    #define ENABLE_BLE_NAME_CONFIG   1   // 本特性：仅 KVM-GO 启用
#elif (VERSION_SELECTION == VERSION_MINIKVM)
    #define ENABLE_BLE               0
    #define ENABLE_USB_COMPOSITE     0
    #define ENABLE_BLE_NAME_CONFIG   0   // 本特性：MiniKVM 完全禁用
#else
    #error "未知的 VERSION_SELECTION 值"
#endif
```

### 5.3 peripheral.c 集成（统一代码，无 #ifdef）

```c
// APP/peripheral.c 开头添加包含
#include "ble_name_config.h"  // MiniKVM 时自动展开为空宏

void Peripheral_Init(void)
{
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

    // 【统一代码】KVM-GO: 真实初始化；MiniKVM: 空宏展开为 do{}while(0)
    BLE_NameConfig_Init();

    // 使用运行时状态初始化 BLE 数据（替代原有硬编码逻辑）
    // 注意：此代码在 MiniKVM 编译时会被 #ifdef ENABLE_BLE 整体包裹的外层块移除
    // 因此无需在此处额外 #ifdef
    
    uint8_t nameLen = strlen(g_ble_name.name);  // MiniKVM: g_ble_name 仍声明但不可用
    if (nameLen > 20) nameLen = 20;

    tmos_memset(attDeviceName, 0, sizeof(attDeviceName));
    tmos_memcpy(attDeviceName, g_ble_name.name, nameLen);

    // ... 后续广播数据初始化保持不变，使用 g_ble_name.name ...
    
    // 其余初始化保持不变...
}
```

> **关键点**：`peripheral.c` 本身在 MiniKVM 编译时通常**不参与编译**（或被 `#ifdef ENABLE_BLE` 整体包裹）。若项目结构要求该文件通用编译，则上述统一代码配合 `ble_name_config.h` 的空宏定义即可实现零侵入。

### 5.4 usbd_compostie_km.c 集成（复用现有 ENABLE_BLE 保护）

```c
// APP/usbd_compostie_km.c - 在 CH9329_DispatchCommand 的 switch 中
#ifdef ENABLE_BLE
    // ... 现有 BLE 相关 case ...
    
    // 【新增】BLE 名称设置命令
    case CMD_BLE_SET_NAME:
        CH9329_Cmd_BLE_SetName(addr, cmd_code, pdata, data_len);
        break;
#endif

// 命令处理函数（放在 #ifdef ENABLE_BLE 块内）
#ifdef ENABLE_BLE
static void CH9329_Cmd_BLE_SetName(uint8_t addr, uint8_t cmd_code,
                                   uint8_t *pdata, uint8_t data_len)
{
    uint8_t status = STATUS_SUCCESS;

    if (data_len == 0) {
        status = STATUS_ERR_PARAM;
    }
    else if (pdata[0] == 0x00) {  // 特殊值：恢复出厂设置
        if (!BLE_NameConfig_Reset()) {
            status = STATUS_ERR_FRAME;
        }
    }
    else {  // 设置新名称
        // 安全复制名称（最多 20 字节 + null 终止符）
        char name[21];
        uint8_t copy_len = (data_len > 20) ? 20 : data_len;
        memcpy(name, pdata, copy_len);
        name[copy_len] = '\0';  // 确保 null 终止

        if (!BLE_NameConfig_Set(name)) {
            status = STATUS_ERR_PARAM;
        }
    }

    CH9329_SendResponse(addr, cmd_code, &status, 1, 1);
}
#endif
```

### 5.5 usbd_composite_km.h 命令定义

```c
// APP/include/usbd_composite_km.h
// ... 现有命令定义 ...
#define CMD_BLE_SET_NAME 0x23  // 添加新命令（需确认当前未使用）
// ... 其余命令定义 ...
```

---

## 6. 编译产物差异对比

| 项目 | KVM-GO 固件 | MiniKVM 固件 |
|------|-------------|--------------|
| `ble_name_config.o` | 存在 (~1.8KB 代码 + 0.2KB 数据) | **不存在** (文件编译为空目标文件) |
| `usbd_compostie_km.o` | 含 `CMD_BLE_SET_NAME` 处理逻辑 | 无 BLE 相关 case (被 `#ifdef ENABLE_BLE` 整体移除) |
| `peripheral.o` | 含 BLE 初始化逻辑 | **通常不编译此文件** (或被 `#ifdef ENABLE_BLE` 整体移除) |
| Flash 存储占用 | +4KB (独立页面，地址 0x0806D000) | 0 |
| CH9329 命令表 | 含 0x23 (BLE_SET_NAME) 响应码 0xA3 | 完全无此命令码相关代码 |
| 固件体积增量 | ~2KB | 0 |

---

## 7. 使用示例（仅 KVM-GO 生效）

### 7.1 设置 BLE 名称

```python
import serial
import time

def calculate_checksum(data):
    return sum(data) & 0xFF

def make_frame(addr, cmd, payload=b""):
    """构建 CH9329 命令帧: 57 AB addr cmd len payload checksum"""
    frame = bytes([0x57, 0xAB, addr, cmd, len(payload)]) + payload
    return frame + bytes([calculate_checksum(frame)])

def set_ble_name(port, new_name):
    """通过 CH9329 指令设置 BLE 名称"""
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.1)  # 等待端口稳定

    # 构建命令帧: 57 AB 00 23 <len> <name_bytes> <checksum>
    name_bytes = new_name.encode('utf-8')[:20]  # 强制限制为20字节
    payload = bytes([len(name_bytes)]) + name_bytes
    frame = make_frame(0x00, 0x23, payload)

    ser.write(frame)
    ser.flush()

    # 读取响应 (应返回 57 AB 00 A3 <len> <status> <checksum>)
    time.sleep(0.5)
    if ser.in_waiting > 0:
        response = ser.read(ser.in_waiting)
        print(f"响应: {response.hex().upper()}")
        
        # 解析响应：状态字节在数据部分
        if (len(response) >= 7 and 
            response[0] == 0x57 and response[1] == 0xAB and
            response[3] == 0xA3):  # 0x23 | 0x80 = 0xA3 (响应位置位)
            status = response[5]
            if status == 0x00:
                print("✓ BLE 名称设置成功")
            else:
                print(f"✗ 设置失败，状态码: 0x{status:02X}")
        else:
            print("✗ 响应格式错误或意外命令")
    else:
        print("✗ 未收到响应（超时或设备无此命令）")

    ser.close()

# 使用示例
set_ble_name('/dev/cu.usbmodem*', 'MyKVM-Device')
```

### 7.2 恢复出厂设置

```python
def reset_ble_name(port):
    """通过 CH9329 指令恢复 BLE 名称为出厂默认值"""
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.1)

    # 特殊载荷：pdata[0] = 0x00 表示恢复出厂设置
    # len=1, pdata[0]=0x00
    frame = make_frame(0x00, 0x23, bytes([0x01, 0x00]))

    ser.write(frame)
    ser.flush()
    
    # 读取响应（逻辑同上）
    time.sleep(0.5)
    if ser.in_waiting > 0:
        response = ser.read(ser.in_waiting)
        print(f"响应: {response.hex().upper()}")
        # ... 解析逻辑同 set_ble_name ...
    
    ser.close()
```

### 7.3 版本查询建议（上位机软件）

上位机软件应先查询设备版本信息（`CMD_GET_INFO`），再决定是否提供"修改 BLE 名称"UI：

```python
# CMD_GET_INFO 响应中 data[0] = version
# version = 1: KVM-GO
# version = 2: MiniKVM
# 仅当 version == 1 时显示 "修改蓝牙名称" 功能入口
```

---

## 8. 设计亮点总结

| 问题 | 解决方案 |
|------|----------|
| **MiniKVM 无 BLE 却编入 BLE 代码** | `ENABLE_BLE_NAME_CONFIG=0` 导致实现文件为空，头文件仅提供空宏，**零代码入镜** |
| **运行时判断浪费资源** | 编译期完全消除，无 `if (has_ble)` 分支，无 Flash 占用 |
| **命令表污染** | `CMD_BLE_SET_NAME` 仅在 `#ifdef ENABLE_BLE` 块中定义，MiniKVM 固件无此命令码 |
| **维护两套代码** | **单一代码库**，通过版本选择宏自动适配，无需分支维护 |
| **接口在 MiniKVM 编译报错** | 头文件提供空宏定义（`#define BLE_NameConfig_Init() do{}while(0)`），调用站点无需修改 |
| **代码量控制** | 复用现有 Flash 配置机制（`usb_mode_config.c` 模式），核心逻辑 < 150 行 |

---

## 9. 验证清单

### 9.1 KVM-GO 编译验证

- [ ] 编译通过，无警告
- [ ] `ble_name_config.o` 存在且大小合理
- [ ] `CMD_BLE_SET_NAME` (0x23) 在命令分发表中
- [ ] Flash 地址 `0x0806D000` 未与其他配置冲突
- [ ] 功能测试：设置名称 → 重启 → 名称保持

### 9.2 MiniKVM 编译验证

- [ ] 编译通过，无警告
- [ ] `ble_name_config.o` 为空或不存在
- [ ] `usbd_compostie_km.o` 中无 `0x23` 相关代码
- [ ] 固件体积未因 BLE 名称配置增加
- [ ] 串口模式功能正常无回归

### 9.3 交叉验证

- [ ] 同一源码树，仅修改 `VERSION_SELECTION` 即可产出两种固件
- [ ] 无需修改任何 `.c` 文件中的 `#ifdef` 即可切换

---

## 10. 后续实施步骤

1. **验证命令码**：确认 `0x23` 在 `usbd_compostie_km.c` 中当前未被使用（通过搜索 `CMD_*` 定义）
2. **创建新文件**：
   - `APP/include/ble_name_config.h`（参考 4.3 节）
   - `APP/ble_name_config.c`（参考 `usb_mode_config.c` 实现，但简化为仅处理名称）
3. **应用修改**：
   - `APP/peripheral.c`（参考 5.3 节）
   - `APP/usbd_compostie_km.c` 和 `APP/include/usbd_composite_km.h`（参考 5.4、5.5 节）
4. **编译和测试**：
   - 验证无编译警告/错误
   - 使用上位机脚本测试名称设置和恢复出厂设置
   - 用蓝牙扫描工具确认名称在广播数据中正确更新
   - 测试掉电重启后名称持久化
   - 测试 CRC 错误容忍性（通过故意破坏 Flash 内容验证回退到默认名称）

---

**文档版本**: v1.0  
**编写日期**: 2026-09-20  
**适用硬件**: Openterface KVM-GO / MiniKVM  
**作者**: Claude Code + 用户协作
