# EasyTier 控制台连接器 - 智能体备忘

智能体在此仓库工作时容易遗漏的高价值信息。

## 构建与运行

- **环境要求**: Qt 6.11+、CMake 3.16+、Windows 专用（llvm‑mingw 或 MSVC）。
- **构建步骤**（优先使用手动创建的 `build/` 目录，而非 CLion 生成的 `cmake‑build‑*`）：
  ```bash
  mkdir build && cd build
  cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt6.11
  cmake --build . --config Release
  cmake --install .
  ```
- **安装输出** 位于项目根目录的 `Install/bin/`（由 `CMAKE_INSTALL_PREFIX` 设定）。
- **构建后处理**: `etcore/` 文件夹会自动复制到输出目录。  
  安装过程中会调用 `windeployqt`（排除 Quick、D3D、翻译、软件渲染）。
- **图标警告**: `assets/favicon.ico` 必须存在；CMake 会在缺失时警告（需将 `favicon.svg` 转换）。
- **无 lint/类型检查命令**；依赖编译器警告。

## 配置与数据

- **配置文件**: `%LOCALAPPDATA%/EasyTier/conf.json`（非 README 中提到的 `config.json`）。
- **旧设置迁移**: 一次性从 `HKEY_CURRENT_USER\Software\EasyTier\Connector`（QSettings）迁移。
- **服务名称**: `QtETWebConnector`（通过 `easytier‑cli.exe` 安装的 Windows 服务）。
- **Casdoor OAuth**:
  - 客户端 ID: `d3ff87a9cd6317695066`（硬编码于 `SystemTray.cpp:29`）。
  - 回调 URL: `http://127.0.0.1:54321/callback`（端口 54321）。
  - 认证服务器: `https://auth.console.easytier.net/`。
- **单实例锁**: `QLocalServer`，名称为 `QtETWebConnector‑By‑Myqfeng`。

## 代码库说明

- **已移除 `IS_NOT_ET_PRO`**: 统一采用 "EasyTier Pro" 系列表述。
- **服务管理** 由静态类 `ETRunService` 处理；所有操作都需要 UAC 提权（`ShellExecuteExW` 配合 `"runas"`）。  
  命令通过 `easytier‑cli.exe`（位于 `etcore/` 目录）执行。
- **连接状态**: `NotStarted`、`Connecting`、`Connected`（枚举定义于 `SystemTray.h`）。
- **成员变量前缀**: `m_`。
- **信号‑槽语法**: 使用 Qt5 风格函数指针连接。
- **日志输出**: `std::clog`/`std::cerr` 用于调试输出。

## 注意事项

- **UAC 提示** 会在服务启动/停止时出现；UI 必须等待最多 2 分钟以获取提权。
- **修改连接密钥** 会自动停止服务（若正在运行）并在 500 ms 后重新启动。
- **`Install/` 目录已 git 忽略**；`cmake‑build‑debug/` 和 `cmake‑build‑release/` 未忽略（避免提交）。
- **若服务启动失败**，检查 `etcore/easytier‑cli.exe` 及其 DLL 依赖是否存在。
- **Casdoor 登录流程** 使用 PKCE 和本地 TCP 服务器；确保端口 54321 空闲。