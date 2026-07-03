# EasyTier 控制台连接器 - 智能体备忘

智能体在此仓库工作时容易遗漏的高价值信息。

## 构建与运行

- **环境要求**: Qt 6.11+、CMake 3.16+、git（构建会自动克隆并编译 `qtet-daemon` 子项目）。
- **构建步骤**（优先使用手动创建的 `build/` 目录，而非 CLION 生成的 `cmake-build-*`）：
  ```bash
  mkdir build && cd build
  cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt6.11
  cmake --build . --config Release
  cmake --install .
  ```
- **可用 cmake-build-debug 快速编译**：`cmake --build cmake-build-debug --config Debug`
- **安装输出** 位于项目根目录的 `Install/bin/`（由 `CMAKE_INSTALL_PREFIX` 设定）。
- **构建后处理**: 安装过程中 Windows 下会调用 `windeployqt`（排除 Quick、D3D、翻译、软件渲染）。
- **无 lint/类型检查命令**；依赖编译器警告。

## 架构分层

```
main.cpp          → 入口：单实例检测 + 命令行解析 + 启动 SystemTray
SystemTray        → UI 协调层：托盘图标 + 右键菜单 + 信号槽桥接
ConnectionController → 业务逻辑层：两级状态机 + 心跳 + 超时 + 重连
IpcClient         → 通信层：QLocalSocket + JSON-RPC over length-prefixed frame
ConfigManager     → 持久化层：JSON 配置读写（conf.json）
```

## 配置与数据

- **配置文件**: `conf.json`，各平台路径：
  - Windows: `%LOCALAPPDATA%/EasyTier/conf.json`
  - macOS: `~/Library/Application Support/EasyTier/conf.json`
  - Linux: `~/.local/share/EasyTier/conf.json`
- **配置字段**: `connectionKey`、`autoStart`、`secureMode`、`autoConnect`（都是 JSON 顶层字段）。
- **配置安全策略**: 文件不存在或 JSON 非法时自动使用默认值，不崩溃。`resetToDefaults()` 将所有字段恢复默认。
- **IPC 后端**: 前端通过 `QLocalSocket` 连接到 `qtet-connector-daemon.sock`，与 `qtet-daemon` 守护进程通信。
- **单实例锁**: `QLocalServer`，名称为 `QtETWebConnector‑By‑Myqfeng`。

## 连接状态管理

- **两级状态枚举**，均定义于 `ConnectionController.h`：
  - `DaemonState`：`Disconnected` / `Connected`（后端 IPC 连接）
  - `ServiceState`：`Idle` / `Connecting` / `Connected` / `Stopping` / `Unknown`（配置服务器连接）
- **后端断线时**，`setDaemonDisconnected()` 原子性重置所有 Service 状态。
- **启动超时** 30s，**停止超时** 10s。
- **断线自动重连** 最多 3 次。
- **修改连接密钥** 会停止当前连接（若正在运行），500ms 后自动重启。
- **自动连接功能**: 勾选后程序启动时后端连上后自动调用 `startConnection()`，仅触发一次（由 `m_hasAutoConnected` 控制）。

## 代码约定

- **成员变量前缀**: `m_`。
- **信号‑槽语法**: 使用 Qt5 风格函数指针连接（`connect(sender, &Sender::signal, receiver, &Receiver::slot)`）。
- **日志输出**: `std::clog`（调试信息）/ `std::cerr`（错误信息）。
- **源码目录**: `src/` 下 15 个文件（7个 `.h`，7个 `.cpp`，1个 `main.cpp`）。
- **CasdoorLogin.cpp** 存在于 `src/` 但未加入 `CMakeLists.txt` 编译，属于历史遗留代码，切勿修改或引入引用。

## 注意事项

- **`Install/` 目录已 git 忽略**；`cmake-build-debug/` 和 `cmake-build-release/` 未忽略（避免提交）。
- **若连接失败**，检查后端守护进程 `qtet-daemon` 是否已启动并监听 `qtet-connector-daemon.sock`。
- **编译时自动构建 qtet-daemon**：CMake 通过 `add_custom_target(qtet_daemon ALL)` 从 git 克隆并编译，主程序依赖该目标。
- **`--auto-start` 命令行参数**：开机自启模式下启动，抑制托盘启动通知。
