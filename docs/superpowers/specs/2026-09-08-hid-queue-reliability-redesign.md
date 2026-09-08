# HID 队列可靠性与响应路径重新设计

> **状态：设计已批准，待实施**
>
> 本文档替代原有的 `queue-performance-optimization-plan.md`，修正了该文档中关于 CH32V20x USBFS "DMA 模式"的误解，并引入系统性改进以解决双向通信链路的丢事件问题。
>
> 设计基于对 `km_ring.c`、`ch32v20x_usbfs_device.c`、`usbd_compostie_km.c`、`peripheral_main.c` 的完整代码审查，以及用户反馈的"暴力测试下主机串口仅收到约 60% 回复"的实际现象。

---

## 1. 背景与问题陈述

Openterface_KM 固件在 CH32V20x 上运行，作为 USB/BLE 设备接收来自主机（KVM 控制器）的 CH9329 指令，将其转换为 HID 键盘/鼠标报告发送到目标机，并将命令应答回传给主机。

**已观察到的问题**：

| 症状 | 表现 | 影响 |
|------|------|------|
| 快速操作漏键/丢鼠标 | 打字快时漏键、鼠标快速移动轨迹丢段 | 用户体验差 |
| 回复丢失率高 | 暴力测试下主机仅收到 ~60% 的 CH9329 命令回复 | 主机误以为设备无响应，触发重试或报错 |
| 绝对鼠标拖动画质差 | 拖动窗口或画图时轨迹跳跃、不连续 | 远程桌面场景不可用 |

**原有优化计划的问题**：
- 将 USBFS 的 `DEF_UEP_DMA_LOAD` 误解为"零拷贝 DMA"，实际只是指向内部 RAM 的偏移寄存器
- 未识别跨上下文队列竞争（BLE 中断 vs 主循环）
- 未识别响应队列未被主循环服务的问题
- SoA 布局在 CH32V20x（无数据缓存）上收益存疑

---

## 2. 根因分析

### 2.1 跨上下文队列竞争（主因）

**证据**：`Queue_Push_*` 在两个上下文中被调用：
- BLE 回调（`ble_usb_ServiceEvt` → `CH9329_DataParser` → 直接 push）——不在主循环里
- 主循环（`USB_DataRx_To_KMHandle` → UART2 路径 → push）

而 `Queue_Pop_*` 只发生在主循环 `USB_SendFromQueues()`。push 的"写数据→更新 tail"和 pop 的"读 head/tail→取数据→推进 head"都没有临界区保护。

**后果**：快速操作时（BLE 高频推送 + 主循环高频 drain），可能发生：
- pop 在 push 更新 tail 前读到空队列 → **漏事件**
- 两者同时推进 head/tail → **乱序/覆盖**
- memcpy 竞争 → **脏报告发出**（目标机看到错误按键）

### 2.2 响应队列未被服务（主因）

**证据**：`RESP_QUEUE` 被 `CH9329_SendResponse()` push，但在 `peripheral_main.c` 的 `Main_Circulation()` 主循环中**从未调用 `USBD_SendFromRespQueue()`** 来排空队列。

**后果**：
- 响应堆积直到队列满（28 槽）
- 队列满时 `head = (head+1) % SIZE` 丢弃最旧响应
- 暴力测试下大量响应被丢弃 → **主机收到 ~60% 回复**

### 2.3 绝对鼠标队列语义错误

**证据**：绝对鼠标报告是状态型（x,y 绝对坐标），但当前按 FIFO 排队。

**后果**：
- 远程桌面拖动时 host 以几百 Hz 灌坐标帧，而目标机轮询 EP3 可能 4–8ms（drain 率 125–250 帧/s）
- 队列满时丢弃最旧帧 = **丢中间位置**
- 绝对坐标只需最新一帧，排队所有中间状态是纯浪费

### 2.4 CH9329 接收缓冲反复 memmove

**证据**：`CH9329_RxBuffer_Add()` 在每次追加数据和每次解析完一帧后都 `memmove` 剩余字节。

**后果**：
- 高频鼠标帧时白白消耗主循环周期
- 间接加剧积压

---

## 3. 设计目标

1. **消除跨上下文竞争** → 所有队列操作加短临界区保护
2. **绝对鼠标永不积压** → 单槽邮箱模型，仅保存最新状态
3. **响应队列及时服务** → 主循环定时排空 RESP_QUEUE
4. **接收解析零 memmove** → 环形缓冲 + 读写指针
5. **在不改变硬件约束下实现** → 不增 RAM，不改变 USB 端点分配

---

## 4. 队列语义分层

| 队列 | 数据语义 | 新模型 | 溢出策略 | 内存变化 |
|------|---------|--------|----------|----------|
| **KB** (键盘) | 状态型（全键状态） | FIFO + 临界区 | 丢最旧 | 不变 |
| **MS_ABS** (绝对鼠标) | **状态型**（绝对坐标） | **单槽邮箱** | 覆盖旧数据 | 168B → 7B |
| **MS_REL** (相对鼠标) | 事件型（增量移动） | FIFO 加深 + 临界区 | 丢最旧 | +144B (28→64 槽) |
| **MEDIA** (媒体键) | 状态型（位图） | FIFO + 临界区 | 丢最旧 | 不变 |
| **RESP** (响应包) | 状态型（应答帧） | FIFO + 临界区 + **主循环排空** | 丢最旧 | 不变 |

**关键 insight**：
- 状态型报告（绝对鼠标、媒体键）只需最新一帧 → 邮箱或 FIFO（深度足够即可）
- 事件型报告（相对鼠标）每帧都是增量 → 必须 FIFO，但可加深防突发
- 键盘是状态型但去重已做 → FIFO 512 深度足够，无需邮箱化（风险 > 收益）

---

## 5. 详细设计

### 5.1 绝对鼠标 → 单槽"邮箱"模型

```c
/* 绝对鼠标邮箱：只保存"最新未发送"的一帧 */
typedef struct {
    uint8_t data[ABS_MS_DATA_LEN];   // 6 or 5 bytes
    volatile uint8_t valid;          // 0 = 空, 1 = 有待发送数据
} MS_Abs_Mailbox_t;

/* 全局实例 */
static MS_Abs_Mailbox_t ms_abs_mbox;
```

**Push（无条件覆盖最新）**：
```c
void Queue_Push_MouseAbs(uint8_t *data) {
    uint8_t sr = __disable_interrupts();
    memcpy(ms_abs_mbox.data, data, ABS_MS_DATA_LEN);
    ms_abs_mbox.valid = 1;
    __set_interrupts(sr);
}
```

**Pop（取走后清空）**：
```c
bool Queue_Pop_MouseAbs(uint8_t *out) {
    if (!ms_abs_mbox.valid) return false;
    uint8_t sr = __disable_interrupts();
    memcpy(out, ms_abs_mbox.data, ABS_MS_DATA_LEN);
    ms_abs_mbox.valid = 0;
    __set_interrupts(sr);
    return true;
}
```

**收益**：
- 内存 168B → 7B
- 拖画轨迹完全连续（端点每次空闲发最新位置）
- 消除"队列满丢中间帧"问题
- 发送延迟最多 1 帧（1ms 轮询下不可感知）

### 5.2 所有队列加临界区防护

所有 `Queue_Push_*` 和 `Queue_Pop_*` 操作入口加最短临界区：

```c
void Queue_Push_Keyboard(uint8_t *data) {
    uint8_t sr = __disable_interrupts();
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[0].data));
    kb_queue.tail = next;
    __set_interrupts(sr);
}
```

**为什么足够**：
- 临界区仅覆盖"读尾指针→检查满→写数据→更新尾指针"这一原子序列
- 持续时间 < 100ns（144MHz 主频下），远短于 BLE 中断或 USB 帧间隔
- 不会导致中断丢失或任务调度延迟

### 5.3 主循环添加响应队列服务

在 `peripheral_main.c` 的 `Main_Circulation()` 中，`USB_SendFromQueues()` 之后添加：

```c
// Keyboard and mouse control code sending loop
USB_SendFromQueues();

// ★ 新增：服务响应队列（发送回复到主机）
if (USB_ModeConfig_ActiveModeBridgeAcm() || 
    USB_ModeConfig_ActiveModeBridgeEcm() ||
    !USB_ModeConfig_ActiveModeHasHostUac()) {
    USBD_SendFromRespQueue();  // 将 RESP_QUEUE 中的帧发出 via UART2
}
```

**条件判断理由**：
- 当 BLE CH9329 响应路由已启用（`Peripheral_GetBleCh9329Response()`）时，响应直接通过 BLE 通知发出
- 此条件确保仅在需要 UART2 路径时才排空队列，避免重复发送

**`USBD_SendFromRespQueue()` 实现**（参考 `USB_SendFromQueues`）：
```c
void USBD_SendFromRespQueue(void) {
    while (resp_queue.head != resp_queue.tail) {
        Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];
        if (USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0) {
            uint8_t sr = __disable_interrupts();
            resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
            __set_interrupts(sr);
        } else {
            break;  // 端点忙，下轮再试
        }
    }
}
```

### 5.4 CH9329 接收缓冲优化（消费偏移 + 按需 compact）

**现状问题**：`CH9329_RxBuffer_Add()` 在**每解析完一帧后**都 `memmove` 剩余字节。一次批量到达 N 帧时，产生 O(N²) 搬移。

**设计**：引入"已消费偏移"游标，逐帧解析只推进游标、不搬移；仅在尾部空间不足时 compact 一次（每次 Add 至多一次 memmove，摊还 O(1)）。保持缓冲**连续**，`CH9329_DataParser` 原样复用（无需处理环回绕写）：

```c
#define RX_BUF_SIZE 512
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_start = 0;  /* 第一个未解析字节的偏移 */
static uint16_t rx_len = 0;    /* 自 rx_start 起的有效字节数 */

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    /* 尾部空间不足时 compact（每次 Add 至多一次 memmove） */
    if (rx_start + rx_len + len > RX_BUF_SIZE) {
        if (rx_len + len > RX_BUF_SIZE) {
            /* 仍然放不下：丢最旧，为最新数据腾位 */
            uint16_t drop = (uint16_t)(rx_len + len - RX_BUF_SIZE);
            rx_start += drop; rx_len -= drop;
        }
        memmove(rx_buf, rx_buf + rx_start, rx_len);
        rx_start = 0;
    }
    memcpy(rx_buf + rx_start + rx_len, data, len);
    rx_len += len;

    /* 逐帧解析：只推进 rx_start，不搬移 */
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf + rx_start, rx_len);
        if (consumed < 0) { rx_start = rx_len = 0; break; }  /* 坏帧：清空 */
        else if (consumed == 0) break;                        /* 帧不完整：等更多数据 */
        else { rx_start += (uint16_t)consumed; rx_len -= (uint16_t)consumed; }
    }
}
```

**收益**：
- 逐帧解析**零 memmove**（从 O(N²) 降为摊还 O(1)）
- `CH9329_DataParser` 不改动，仍操作连续内存
- 溢出语义清晰（丢最旧字节，保最新数据）

---

## 6. 实现计划

### 6.1 文件修改清单

| 文件 | 改动类型 | 改动量 | 说明 |
|------|---------|--------|------|
| `APP/include/km_ring.h` | 重构 | 中 | 重定义队列结构（绝对鼠标邮箱化），添加临界区宏，声明 `USBD_SendFromRespQueue()` |
| `APP/km_ring.c` | 重构 | 大 | 重写所有 Push/Pop 函数（加临界区），实现邮箱模型，优化接收缓冲 |
| `APP/peripheral_main.c` | 添加 | 小 | 在主循环中添加 `USBD_SendFromRespQueue()` 调用（带条件判断） |
| `APP/usbd_compostie_km.c` | 添加 | 小 | 实现 `USBD_SendFromRespQueue()`（若缺失） |

### 6.2 实施顺序

1. **Phase 1（止血）**：
   - 给所有 Queue_Push/Pop 加临界区
   - 主循环添加 `USBD_SendFromRespQueue()` 调用
   - 验证：暴力测试下回复率应接近 100%

2. **Phase 2（核心优化）**：
   - 绝对鼠标改为邮箱模型
   - 相对鼠标队列 28→64
   - 验证：快速拖画轨迹连续

3. **Phase 3（性能优化）**：
   - CH9329 接收缓冲改为环形
   - 验证：主循环负载降低 15-25%

---

## 7. 验证与基准测试

### 7.1 功能验证

| 场景 | 验证方法 | 通过标准 |
|------|---------|----------|
| 暴力测试回复率 | 主机连续发送 100 条 CH9329 命令 | 回复率 ≥ 99% |
| 快速打字 | 连续快速按键（>20 键/秒） | 无漏键 |
| 快速鼠标移动 | 快速拖动窗口/画图 | 轨迹连续，无跳跃 |
| BLE 与 UART2 并发 | 同时从 BLE 和 UART2 发送命令 | 无死锁、无丢包 |

### 7.2 性能基准

| 指标 | 测量方法 | 预期值 |
|------|---------|--------|
| 主循环平均负载 | GPIO 翻转 + 示波器 | 降低 15-25% |
| 队列操作延迟 | 临界区时长（逻辑分析仪） | < 100ns |
| 绝对鼠标发送延迟 | USB 抓包 | ≤ 1 帧（1-2ms） |

---

## 8. 预期收益

| 指标 | 现状 | 新设计 | 改善幅度 |
|------|------|--------|----------|
| 输入事件丢失率（快速操作） | 高（竞争+溢出） | 极低（临界区+状态分层） | >90% 降低 |
| 响应丢失率（暴力测试） | ~40% | ~0%（主循环定时服务） | 消失 |
| 绝对鼠标轨迹完整性 | 有丢段 | 完全连续 | 100% 可用 |
| 主循环平均负载 | 中等 | 降低（减少 memmove+事件驱动） | 15-25% 降低 |
| RAM 占用 | ~1.2KB | 净变化 ~−20B（MS_ABS 邮箱省 161B，MS_REL 加深 +144B） | 基本持平 |

---

## 9. 风险与缓解

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| 临界区过长导致中断丢失 | 低 | 中 | 仅在关键指令序列加临界区（< 100ns） |
| 邮箱模型导致旧坐标丢失 | 中 | 低 | 这是预期行为（绝对鼠标只需最新状态） |
| 环形缓冲溢出 | 低 | 低 | 丢最旧字节，保证最新数据优先 |
| 响应队列排空不及时 | 低 | 中 | 主循环每轮都检查，响应帧小（< 64B），可快速发出 |

---

## 10. 与原有计划的对比

| 项目 | 原计划 | 新设计 |
|------|--------|--------|
| DMA 零拷贝 | ❌ 误解 USBFS DMA 模式 | ✅ 识别为指向内部 RAM 偏移，非真正零拷贝 |
| SoA 布局 | ❌ 假设 cache 收益（实际无 cache） | ✅ 移除，改为语义分层 |
| 队列竞争 | ❌ 未识别 | ✅ 所有操作加临界区 |
| 响应路径 | ❌ 未涉及 | ✅ 主循环添加排空调度 |
| 绝对鼠标 | ❌ 保持 FIFO | ✅ 改为单槽邮箱 |
| 预期收益 | 40% 性能提升（未验证） | 解决丢事件问题（已验证根因） |

---

## 11. 开放问题

- [ ] `USBD_SendFromRespQueue()` 是否已实现？若未实现需补写
- [ ] 临界区宏使用 `__disable_interrupts()`/`__set_interrupts()` 还是 `__get_MSTATUS()`/`__restore_mstatus()`？需确认 CH32V20x RISC-V 最佳实践
- [ ] 是否需要为调试目的添加队列深度监控（如通过 `USB_DEBUG` 输出当前各队列长度）？

---

## 12. 实施检查清单

- [ ] Phase 1：临界区 + 响应队列服务
  - [ ] 修改 `km_ring.c` 所有 Push/Pop 函数
  - [ ] 修改 `peripheral_main.c` 主循环
  - [ ] 实现 `USBD_SendFromRespQueue()`（若缺失）
  - [ ] 功能测试：暴力测试回复率
- [ ] Phase 2：队列模型优化
  - [ ] 修改 `km_ring.h` 重定义 `MS_Abs_Queue_t`
  - [ ] 实现绝对鼠标邮箱模型
  - [ ] 修改相对鼠标队列深度 28→64
  - [ ] 功能测试：拖画轨迹、快速打字
- [ ] Phase 3：接收缓冲优化
  - [ ] 修改 `CH9329_RxBuffer_Add` 为环形缓冲
  - [ ] 性能测试：主循环负载
- [ ] 最终验证
  - [ ] 全场景功能测试
  - [ ] 性能基准测试
  - [ ] 代码审查
  - [ ] 提交 PR

---

## 13. 状态跟踪

| 阶段 | 状态 | 完成日期 |
|------|------|----------|
| 设计 | ✅ 已完成 | 2026-09-08 |
| Phase 1 实施 | ⬜ 待开始 | — |
| Phase 2 实施 | ⬜ 待开始 | — |
| Phase 3 实施 | ⬜ 待开始 | — |
| 验证 | ⬜ 待开始 | — |

---

**文档维护**：本文档随实施进展更新。重大设计变更需重新审批。
