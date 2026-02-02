# LVGL 工业信息看板（PC 模拟器）

这个项目用于在 Windows + VS Code 上先把 LVGL 界面（工业信息显示前端）跑起来，再逐步迁移到 正点原子阿波罗 F4/F7/H7 + STM32F7B 开发板。

## 你将得到

- 一个可运行的 LVGL PC 模拟器（SDL2 显示）
- 一个“工业看板”示例界面（关键指标卡片 + 趋势图）
- 一个数据模拟器：周期刷新数值与趋势

## 依赖

- CMake（建议 >= 3.20）
- 编译器：
  - 推荐：MSYS2/MinGW-w64（或 Visual Studio 也可）
- SDL2（LVGL PC 端口使用）
- 网络（可选）：默认可以在线用 FetchContent 拉取依赖；如果网络无法访问 GitHub，可使用离线依赖模式（见下）

## Windows + MSYS2（推荐）安装依赖

1. 安装 MSYS2： https://www.msys2.org/
2. 打开 **MSYS2 UCRT64** 终端，执行：

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2
```

## 构建与运行

在 VS Code 终端进入项目根目录：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/dashboard_pc.exe
```

## 离线依赖模式（推荐：GitHub 访问受限时）

如果你配置时看到类似错误：`Failed to connect to github.com`，说明 FetchContent 拉取失败。

- 把依赖源码放到 `third_party/`（目录结构见 [third_party/README.md](third_party/README.md)）
- 然后在 VS Code 里运行任务：
  - `cmake: configure (local deps)`
  - `cmake: build`
  - `run: dashboard_pc`

### 如果你遇到“找不到 cmake/ninja/gcc”

有些 Windows 环境里，新安装的软件不会立刻进入 VS Code 的 PATH。

- 推荐方式：直接用 VS Code 的任务运行（已内置工具链路径）
  - 任务：`cmake: configure` / `cmake: build` / `run: dashboard_pc`
- 或者：重启 VS Code 让 PATH 刷新

## 项目运行逻辑（建议先看）

- 详细说明： [docs/项目运行逻辑与Lxb对接说明.md](docs/%E9%A1%B9%E7%9B%AE%E8%BF%90%E8%A1%8C%E9%80%BB%E8%BE%91%E4%B8%8ELxb%E5%AF%B9%E6%8E%A5%E8%AF%B4%E6%98%8E.md)

## 下一步（迁移到 STM32）

当你在 PC 上把界面结构和组件库确定后，再进入：

- LCD 驱动 / 触摸驱动
- LVGL 的 tick、刷新、DMA2D、LTDC（F7 常见）
- FreeRTOS（可选）

需要我下一步把界面做成更贴近你目标的“工业 HMI 信息显示”（比如：报警列表、设备状态矩阵、趋势缩略图、权限与页面导航）也可以直接说你的页面草图/字段。 
