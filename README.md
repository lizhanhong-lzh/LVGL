# STM32F767 工业数据显示系统（LVGL）

基于 STM32F767 的工业数据显示系统，使用 LVGL 构建界面，通过串口接收 SQMWD_Tablet 协议数据解析并显示。系统支持 NAND + FatFs 文件系统（字体/资源加载），提供 FILE/FRAME 模式切换与调试指令。

---

## 1. 总体结构概览

```
LVGL/                     主工程根目录
├─ LVGL1/                  STM32F767 板端工程（Keil MDK）
├─ src/                    PC 模拟主程序（SDL）
├─ third_party/            第三方库（PC 端）
├─ build/                  PC 端 CMake 构建输出
├─ config/                 PC 端 LVGL 配置
└─ docs/                   文档
```

---

## 2. 核心代码目录结构与职责

以下为与“协议解析 / UI 刷新 / 文件系统 / 串口接收”相关的核心文件与职责：

- LVGL1/User/main.c
  - 系统初始化（时钟、SDRAM、LCD、UART、定时器）
  - 串口模式（FILE/FRAME）管理与命令解析
  - SQMWD_Tablet 协议解析主流程与数据分发
  - NAND/FatFs 挂载与文件接收入口

- LVGL1/User/app/obuf.c / LVGL1/User/app/obuf.h
  - 串口接收环形缓冲（SPSC：ISR 生产者 + 主循环消费者）
  - 提供 `obuf_write/obuf_read/obuf_peek/obuf_find/obuf_drop`

- LVGL1/User/app/app.c / LVGL1/User/app/app.h
  - 应用层入口与 UI 创建封装（`app_init()`）

- LVGL1/User/app/screens/dashboard.c / dashboard.h
  - 工业看板 UI 创建与刷新
  - 解码表、泵压、工具面历史、通信状态、消息弹窗
  - NAND 字体加载（N:/font）与字体头校验

- LVGL1/Drivers/SYSTEM/usart/usart.c / usart.h
  - USART2/USART3 初始化与中断接收
  - 统一调用 `usart_rx_byte_hook()` 写入环形缓冲
  - 记录最后接收端口（用于 UI 显示 UART2/3）

- LVGL1/Drivers/BSP/NAND/nand.c / nand.h
  - NAND 底层驱动（初始化、读写、坏块检测）

- LVGL1/Drivers/BSP/NAND/ftl.c / ftl.h
  - FTL 层（坏块管理 / 逻辑块映射 / FTL 格式化）

- LVGL1/Middlewares/FATFS/src/ff.c / diskio.c
  - FatFs 文件系统核心

- LVGL1/Middlewares/LVGL/GUI/lvgl/src/extra/libs/fsdrv/lv_fs_fatfs.c
  - LVGL 文件系统桥接（让 LVGL 访问 N:）

---

## 3. OBUF 环形缓冲（串口接收缓冲）

在 main.c 中定义了 16KB 的 `g_rx_storage`，并初始化 `obuf_t g_rx_buf`。

设计要点：
- ISR 侧只写（`obuf_write`），主循环只读（`obuf_read`）
- 满时丢弃新字节，`dropped` 统计丢包
- 支持 `obuf_find`/`obuf_peek` 用于“找帧头/看长度/校验”

这样保证串口字节流高频输入时不阻塞 ISR，且主循环可以稳健解析。

---

## 4. NAND + FatFs 文件系统（N:）

系统使用 NAND + FTL + FatFs，逻辑盘注册为 N:：
- 启动时 `fatfs_mount_once()` 挂载 N:
- 通过 FILE 模式接收文件（字体/图片）写入 N:
- LVGL 通过 `lv_fs_fatfs` 访问 N:/font、N:/logo 等资源

字体加载流程（dashboard.c）：
- `font_has_lvgl_head()` 先检查 LVGL 字体头（head）
- 通过 `lv_font_load()` 从 N:/font 加载
- 失败回退到内置字体，避免崩溃

---

## 5. CMD 指令与模式切换

### 6.1 串口模式

系统支持两种模式：
- FILE：接收命令与文件（PUT/CMD），用于 NAND 文件管理
- FRAME：接收 SQMWD_Tablet 业务帧

当前默认值：`UART_MODE_FRAME`（可通过 CMD MODE 切换）

### 6.2 CMD 指令列表（FILE 模式）

- CMD FMT：格式化 FatFs（仅文件系统层）
- CMD MOUNT：挂载 N: 盘
- CMD MKDIR <path>：创建目录
- CMD STAT <path>：查询文件信息
- CMD LS <path>：列目录
- CMD DEL <path>：删除文件/目录
- CMD NANDSCAN：扫描坏块并统计
- CMD NANDFMT：FTL 格式化（逻辑层重建）
- CMD MODE FILE / CMD MODE FRAME：切换串口模式
- CMD FONTHEAD <path>：打印文件前 32 字节（用于字体头校验）
- CMD HELP：输出命令提示

### 6.3 PUT 文件写入

格式：
PUT <path> <size>
随后发送 size 个原始字节

示例：
PUT N:/font/my_font_70.bin 18388

---

## 6. 双串口输入与数据解析流程

### 7.1 串口接收（USART2 + USART3）

- USART2 与 USART3 都开启中断接收
- 每收到 1 字节，统一调用 `usart_rx_byte_hook()`
- 该钩子函数执行：
  1) 字节写入 `g_rx_buf`
  2) 更新“最近接收字节时间戳”
  3) 统计调试信息

### 7.2 主循环解析总流程

主循环核心逻辑：
1) `process_uart_commands()`：始终可用，优先解析 CMD/PUT
2) FILE 模式：`process_file_rx()` 持续写入文件
3) FRAME 模式：解析业务协议帧并更新 UI

数据流：
串口 ISR → obuf_write → g_rx_buf → sx_try_parse_one → 业务字段映射 → dashboard_update

### 7.3 SQMWD_Tablet 业务帧解析

帧结构：
Header(0x40 0x46) + CMD(0x09) + LEN + Sub_CMD + Payload + XOR

子命令：
- 0x01：泵压帧（两个 float）
- 0x02：参数数值帧（FID + float + 可选名称）
- 0x03：消息帧（autoCloseSec + 文本）

解析流程（sx_try_parse_one）：
1) 查帧头（0x40 0x46）
2) 校验长度与 CMD=0x09
3) XOR 校验
4) 解包为 `sx_frame_t`

字段映射优先级：
1) 先用 FID 映射（0x10~0x14）
2) 再用名称文本兜底（INC/AZI/GTF/MTF/TF）

解析结果最终写入 `g_metrics` 并触发 UI 刷新。

---

## 7. 硬件与显示配置

- HCLK = 216 MHz
- SDRAM 时钟 = 108 MHz
- LTDC 帧缓冲基址：0xC0000000
- 默认像素格式：RGB565
- 1280×800 显存占用：约 2.0MB（$1280\times800\times2$）

屏幕时序与像素时钟由 `ltdc_init()` 根据 LCD ID 分支设置。

---

## 8. UI 与字体

- 主界面：工具面历史圆环 + 核心参数 + 解码表
- 消息弹窗：Sub_CMD=0x03，支持常驻与自动关闭
- 字体从 N:/font 加载，头校验失败回退到内置字体

---

## 9. 调试功能启用位置

- 调试面板总开关（编译期）：`DASHBOARD_ENABLE_DEBUG`
- 调试面板创建与挂载：`#if DASHBOARD_ENABLE_DEBUG` 代码块
- 调试面板刷新函数：`dashboard_debug_update()`
- 调试信息周期刷新入口：主循环 1s 刷新逻辑

---

## 10. 构建与运行（PC 模拟器）

依赖：CMake + SDL2

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/dashboard_pc.exe
```

离线依赖模式参考 third_party/README.md。
