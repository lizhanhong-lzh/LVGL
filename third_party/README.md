# third_party（离线依赖）

如果你的网络环境无法访问 GitHub（常见：公司网络/校园网/代理限制），本工程可以把依赖放到 `third_party/` 目录，CMake 会自动优先使用本地源码，不再走 `FetchContent`。

## 目录结构

把以下仓库源码放到对应目录（建议用解压 zip 的方式放入）：

- `third_party/lvgl/`（需要包含 `CMakeLists.txt`）
- `third_party/lv_drivers/`（需要包含 `CMakeLists.txt`）

SDL2 有两种方式：

- 推荐：系统已安装 SDL2（让 `find_package(SDL2)` 能找到）
- 或者：放源码到 `third_party/SDL/`（需要包含 `CMakeLists.txt`）

## VS Code 任务

- 离线/本地依赖：运行任务 `cmake: configure (local deps)` → `cmake: build` → `run: dashboard_pc`
- 在线拉取：运行任务 `cmake: configure (fetch deps)` → `cmake: build (after fetch)` → `run: dashboard_pc`

## 常见问题

1) **依赖目录放对了，但还是去拉 GitHub？**
- 确认目录名严格是：`third_party/lvgl`、`third_party/lv_drivers`、`third_party/SDL`
- 确认 `third_party/lvgl/CMakeLists.txt` 存在

2) **lv_drivers 没有 CMakeLists.txt**
- 说明你拿到的版本不带 CMake 支持；建议换用与 LVGL v8.3 配套的 `release/v8.3` 版本。

3) **SDL2 找不到**
- 如果你不想在线拉取 SDL2：把 SDL2 源码放到 `third_party/SDL`，再用 `cmake: configure (local deps)`。
