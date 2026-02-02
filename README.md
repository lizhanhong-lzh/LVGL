# STM32F767 工业数据显示系统（LVGL）

本仓库包含 STM32F767 开发板上的工业数据显示系统，使用 LVGL 构建界面，串口接收 SQMWD_Tablet 协议数据并解析显示。

---

## 1. 项目目标

- 稳定接收串口数据并解析为业务指标
- 低负载刷新 UI，保证界面流畅
- 提供调试面板与容错机制，便于定位问题

---

## 2. 目录结构与职责

```
LVGL/
├─ LVGL1/
│  ├─ User/                 # 主逻辑与UI
│  │  ├─ main.c              # 初始化、解析、调度
│  │  └─ app/                # 业务模型/缓冲/UI
│  ├─ Drivers/               # 板级驱动
│  └─ Middlewares/LVGL/      # LVGL库与配置
├─ src/                      # PC模拟器/共用逻辑
├─ third_party/              # 外部依赖
└─ docs/                     # 说明文档
```

核心代码集中在 LVGL1/User 下，Drivers 为硬件驱动层，LVGL 库位于 Middlewares/LVGL。

---

## 3. 运行流程（板端）

1) 硬件初始化：时钟、串口、SDRAM、LCD、定时器
2) LVGL 初始化：显示适配与 UI 创建
3) 串口中断接收：字节写入环形缓冲
4) 主循环解析：从缓冲中解析 SQMWD_Tablet 帧
5) 数据分发：更新业务结构体并刷新 UI

---

## 4. 协议概述（0x09）

帧头固定 0x40 0x46，命令 0x09，长度为 payload 长度，校验为 XOR。

```
[0]  0x40
[1]  0x46
[2]  0x09
[3]  LEN
[4]  Sub_CMD
...  Payload
[N]  XOR
```

子命令：
- 0x01：泵压帧（两个 float）
- 0x02：参数数值帧（FID + float + 可选名称）
- 0x03：消息帧（自动关闭时间 + 文本）

FID（与 SQMWD_Tablet 一致）：
- 0x10 井斜
- 0x11 方位
- 0x12 工具面
- 0x13 重力工具面
- 0x14 磁性工具面

---

## 5. UI 刷新策略

- 主界面仅在数据变化时刷新
- 解码表采用固定行数循环刷新
- 解码表低频更新（避免高频重绘）

---

## 6. 构建与运行（PC 模拟器）

### 依赖
- CMake
- MinGW 或 Visual Studio
- SDL2

### 构建

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/dashboard_pc.exe
```

离线依赖模式请参考 [third_party/README.md](third_party/README.md)。

---

## 7. 常见问题

1) 小数显示异常：确保发送的是小端 float，且使用整数拼接显示小数。
2) 校验失败：确认 XOR 计算区间为帧头到 payload 末尾。
3) 卡顿：降低解码表刷新频率或减少重绘。

---

## 8. keilkill.bat 作用

用于结束 Keil/编译相关进程，解决工程被占用导致无法编译/下载的问题。

---

## 9. 进一步文档

- 板端交付说明：LVGL1/项目说明.md
