/**
 * @file main.cpp
 * @brief EasyTier 控制台连接器程序入口
 * 
 * 功能：
 * - 单实例检测（基于本地套接字）
 * - 命令行参数解析
 * - 系统托盘初始化
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
 * @brief 检查是否已有实例运行
 * @param serverName 本地套接字名称
 * @return true 如果已有实例运行
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
 * @param app QApplication 实例
 * @param serverName 本地套接字名称
 * @return 创建成功返回服务器指针，失败返回 nullptr
 */
static QLocalServer* createSingletonServer(QApplication &app, const QString &serverName)
{
    auto *server = new QLocalServer(&app);
    // 移除可能残留的服务器文件（Linux/macOS 需要，Windows 无害）
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
    
    // 设置应用程序元信息
    // 保持内部 applicationName 为 EasyTier，避免配置路径迁移到 EasyTier Pro/conf.json。
    app.setApplicationName("EasyTier");
    app.setApplicationVersion(QString(APP_VERSION)+"(et" + ET_VERSION + ")");
    app.setQuitOnLastWindowClosed(false);
    
    // 设置全局默认字体大小
    QFont defaultFont = app.font();
    defaultFont.setPointSize(10);
    app.setFont(defaultFont);
    
    // 单实例检测
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
        // 继续运行，因为可能是权限问题
    }
    
    // 解析命令行参数
    QCommandLineParser parser;
    parser.setApplicationDescription(QString("%1 - EasyTier 控制台连接器").arg(APP_DISPLAY_NAME));
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption autoStartOption(
        QStringList() << "auto-start",
        "开机自启模式启动（不显示启动通知）"
    );
    parser.addOption(autoStartOption);
    parser.process(app);
    
    // 创建系统托盘
    SystemTray tray;
    tray.setAutoStartMode(parser.isSet(autoStartOption));
    tray.show();

    return app.exec();
}
