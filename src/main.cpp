/**
 * @file main.cpp
 * @brief EasyTier 控制台连接器程序入口
 *
 * 启动流程：
 * 1. 设置 Qt 应用元信息（名称、版本）
 * 2. 全局字体大小设为 10pt
 * 3. 单实例检测（基于 QLocalServer/QLocalSocket）
 * 4. 解析命令行参数（--auto-start 用于开机自启模式）
 * 5. 创建 SystemTray（系统托盘 + 连接控制器 + IPC 客户端）
 * 6. 进入 Qt 事件循环
 */

#include <QApplication>
#include <QSystemTrayIcon>
#include <QFont>
#include <QCommandLineParser>
#include <QLocalSocket>
#include <QLocalServer>
#include <QMessageBox>
#include <iostream>
#include "SystemTray.h"

/**
 * @brief 检查是否已有实例在运行
 * @param serverName 本地套接字名称，用于单实例锁定
 * @return true = 已有实例运行
 *
 * 原理：尝试连接另一个实例的 QLocalServer。
 * 如果能在 500ms 内连接成功，说明已有实例在监听该名称。
 */
static bool isInstanceRunning(const QString &serverName)
{
    QLocalSocket checkSocket;
    checkSocket.connectToServer(serverName, QIODevice::WriteOnly);
    
    if (checkSocket.waitForConnected(500)) {
        checkSocket.disconnectFromServer();
        return true;
    }
    return false;
}

/**
 * @brief 创建单实例锁定的本地服务器
 * @param app        QApplication 实例（作为 parent）
 * @param serverName 本地套接字名称
 * @return 创建成功返回服务器指针，失败返回 nullptr
 *
 * 创建成功后，其他实例的 isInstanceRunning() 会检测到该服务器，
 * 从而实现单实例检测。removeServer() 用于清理前一次异常退出遗留的文件。
 */
static QLocalServer* createSingletonServer(QApplication &app, const QString &serverName)
{
    auto *server = new QLocalServer(&app);
    // Linux/macOS 上需要手动移除残留的 socket 文件
    // Windows 上此为无害操作
    QLocalServer::removeServer(serverName);
    
    if (!server->listen(serverName)) {
        std::cerr << "无法创建单实例服务器: " << server->errorString().toStdString() << std::endl;
        delete server;
        return nullptr;
    }
    
    return server;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // ---------- 设置应用元信息 ----------
    // 保持内部 applicationName 为 "EasyTier"，
    // 避免配置路径迁移到 "EasyTier Pro/conf.json"。
    // 用户看到的是 APP_DISPLAY_NAME = "ET Connector"。
    app.setApplicationName("EasyTier");
    // 版本格式: "1.0.0(et2.6.3)" - 前端版本(后端协议版本)
    app.setApplicationVersion(QString(APP_VERSION)+"(et" + ET_VERSION + ")");
    // 托盘应用：关闭最后一个窗口后不退出，继续在后台运行
    app.setQuitOnLastWindowClosed(false);
    
    // ---------- 设置全局字体 ----------
    QFont defaultFont = app.font();
    defaultFont.setPointSize(10);
    app.setFont(defaultFont);
    
    // ---------- 单实例检测 ----------
    // 套接字名称在 AGENTS.md 中记录，不可随意修改
    const QString serverName = "QtETWebConnector-By-Myqfeng";
    if (isInstanceRunning(serverName)) {
        QMessageBox::information(nullptr, APP_DISPLAY_NAME, "程序已在运行！");
        return 0;
    }
    
    // 创建单实例锁定服务器
    QLocalServer *localServer = createSingletonServer(app, serverName);
    if (!localServer) {
        QMessageBox::warning(nullptr, APP_DISPLAY_NAME,
                             "无法初始化单实例锁定，程序可能已在运行。");
        // 即使锁定失败也不阻止启动 —— 可能是权限问题
    }
    
    // ---------- 解析命令行参数 ----------
    QCommandLineParser parser;
    parser.setApplicationDescription(QString("%1 - EasyTier 控制台连接器").arg(APP_DISPLAY_NAME));
    parser.addHelpOption();
    parser.addVersionOption();
    
    // --auto-start: 开机自启模式，抑制启动通知
    QCommandLineOption autoStartOption(
        QStringList() << "auto-start",
        "开机自启模式启动（不显示启动通知）"
    );
    parser.addOption(autoStartOption);
    parser.process(app);
    
    // ---------- 启动系统托盘 ----------
    // SystemTray 的构造函数会自动：
    //   - 加载配置
    //   - 创建 ConnectionController → IpcClient
    //   - 连接后端守护进程
    //   - 启动心跳定时器
    SystemTray tray;
    tray.setAutoStartMode(parser.isSet(autoStartOption));
    tray.show();

    // 进入 Qt 事件循环，阻塞直到 QApplication::quit() 被调用
    return app.exec();
}
