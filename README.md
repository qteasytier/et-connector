> **说明：目前为重构初期的版本，仅适配了 Arch Linux 系统，可通过 AUR 安装**

# EasyTier 控制台连接器

基于 Qt6 的系统托盘应用程序，用于连接 EasyTier Web 控制台（配置服务器）。

## 安装说明
平台支持：Windows（Win 10 1809以上）、Linux（Ubuntu 24.04+，Debian 12+，Arch/Manjaro）

### Windows
Windows 用户请直接前往 Releases 页面下载 exe 安装包，双击运行即可安装。

### Linux
对于 Ubuntu/Debian 用户，请前往 Releases 页面下载 Deb 包，使用如下命令安装
```bash
sudo dpkg -i /path/to/your/packge.deb

# 若提示依赖缺失请执行以下命令后再次安装
sudo apt install -f
```

对于 Arch/Manjaro 用户，您可以从 AUR 进行安装，以 `yay` 管理器为例
```bash
sudo pacman -Syu yay       # 安装 yay (从 archlinuxcn 源)
yay -S easytier-connector  # 安装 EasyTier Connector
```

### macOS
前往 Releases 页面下载 dmg 安装包，双击挂载后拖拽到应用程序目录即可。

## 功能特性

### 核心特性

- 连接状态持久化保存
- 系统托盘常驻运行
- 通过 IPC 后端管理配置服务器连接
- 修改密钥后自动重连
- 启动/停止进度提示
- 开机自启托盘程序

## 项目结构

```
et-connector/
├── src/                      # 源代码
│   ├── main.cpp              # 程序入口，命令行解析，单实例检测
│   ├── SystemTray.h/cpp      # 系统托盘主类
│   ├── IpcClient.h/cpp       # IPC 客户端，与 qtet-daemon 通信
│   ├── ConfigManager.h/cpp   # JSON 配置文件管理
│   ├── SettingsDialog.h/cpp  # 连接密钥设置对话框
│   ├── AboutDialog.h/cpp     # 关于软件对话框
│   ├── CasdoorLogin.h/cpp    # Casdoor 登录认证（未接入主流程）
│   └── QuitConfirmDialog.h/cpp # 退出确认对话框
├── assets/                   # 图标资源 (SVG/ICO)
├── docs/                     # 使用文档
├── package/                  # 打包脚本
│   ├── windows/              # Windows 安装包
│   ├── linux/                # Linux 安装包 (DEB/AUR)
│   └── mac/                  # macOS 安装包
├── resources.qrc             # Qt 资源文件定义
├── app.manifest              # Windows 应用清单
└── CMakeLists.txt            # CMake 构建配置
```

## 构建说明

### 环境要求

- Qt 6.11 或更高版本
- CMake 3.16+
- Windows：llvm-mingw 或 MSVC 编译器
- Linux：GCC 或 Clang
- macOS：Xcode 或 Clang

### 构建步骤

```bash
# 1. 创建构建目录
mkdir build && cd build

# 2. 配置 CMake（指定 Qt 路径）
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt6.11

# 3. 编译
cmake --build . --config Release

# 4. 安装（输出到 Install/bin/ 目录，自动部署 Qt 运行时）
cmake --install .
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `--auto-start` | 开机自启模式启动（不显示启动通知） |
| `--help` | 显示帮助信息 |
| `--version` | 显示版本号 |

## 配置说明

配置文件位于 `%LOCALAPPDATA%/EasyTier/conf.json`：

```json
{
  "connectionKey": "your-connection-key",
  "autoStart": false,
  "rememberQuitChoice": false,
  "stopOnQuit": true
}
```

| 配置项 | 说明 |
|--------|------|
| `connectionKey` | EasyTier 配置服务器连接地址/密钥 |
| `autoStart` | 开机自启托盘程序开关 |
| `rememberQuitChoice` | 是否记住退出时的选择 |
| `stopOnQuit` | 记住的选择：退出时是否停止连接 |

## 运行原理

### IPC 通信

应用程序通过本地套接字 `qtet-connector-daemon.sock` 与 `qtet-daemon` 后端守护进程通信：

```
ET Connector (前端)
        │ QLocalSocket
        │ qtet-connector-daemon.sock
        ▼
   qtet-daemon (后端)
        │ EasyTier FFI
        ▼
   EasyTier Core
```

前端使用的后端接口：

| 方法 | 说明 |
|------|------|
| `start_config_server_client` | 连接配置服务器 |
| `stop_config_server_client` | 断开配置服务器 |
| `is_config_server_client_connected` | 查询连接状态 |
| `poll_config_events` | 获取配置服务器事件 |

帧协议采用 4 字节大端长度前缀 + UTF-8 JSON payload，消息格式为 JSON-RPC 风格。

### 依赖后端

本程序本身不直接运行 EasyTier Core，需要配合 `qtet-daemon` 使用。请确保后端守护进程已启动并监听 `qtet-connector-daemon.sock`。
