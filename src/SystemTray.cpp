/**
 * @file SystemTray.cpp
 * @brief 系统托盘主类实现
 *
 * 架构说明（重构后）：
 * SystemTray 是纯 UI 协调层，不直接操作 IPC 或状态机。
 * 所有连接逻辑已委托给 ConnectionController，通过信号驱动 UI 更新。
 *
 * 两级信号设计：
 * - daemonStateChanged  → 只更新"守护进程：已连接/未连接"行
 * - serviceStateChanged → 只更新"状态：xxx"行 + "启动/停止"按钮
 *
 * 交互模式：
 *   SystemTray ──调用──→ ConnectionController ──IPC──→ qtet-daemon
 *       ↑                      │
 *       └─── 信号通知 ─────────┘
 */

#include "SystemTray.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QSysInfo>
#include <iostream>

// ============================================================================
// 构造 / 析构
// ============================================================================

SystemTray::SystemTray(QObject *parent)
    : QObject(parent)
{
    std::clog << "SystemTray: 初始化开始" << std::endl;

    m_configManager = new ConfigManager(this);
    if (!m_configManager->loadConfig()) {
        std::cerr << "SystemTray: 配置加载失败，使用默认配置" << std::endl;
    }

    loadSettings();

    // --- 3. 系统托盘图标 ---
    m_trayIcon = new QSystemTrayIcon(this);
    QIcon icon(FAVICON_SVG);
    if (icon.isNull()) {
        std::cerr << "SystemTray: 无法加载托盘图标" << std::endl;
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(APP_DISPLAY_NAME);

    // --- 4. 右键菜单（必须在 ConnectionController 之前创建，updateStatus 依赖菜单项）---
    m_menu = new QMenu();
    setupMenu();
    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &SystemTray::onTrayActivated);

    // --- 更新密钥设置状态文字 ---
    updateUserStatus();

    // --- 5. 连接控制器 ---
    m_connectionController = new ConnectionController(m_configManager, this);
    initController();
    updateStatus(m_connectionController->daemonState(), ServiceState::Idle);

    // --- 6. 自动连接（仅首次后端连上时触发一次）---
    if (m_autoConnect && !m_configManager->getConnectionKey().isEmpty()) {
        if (m_connectionController->daemonState() == DaemonState::Connected) {
            m_connectionController->startConnection();
        } else {
            m_autoConnectConn = connect(
                m_connectionController, &ConnectionController::daemonStateChanged,
                this, [this](DaemonState ds) {
                    if (ds == DaemonState::Connected) {
                        QObject::disconnect(m_autoConnectConn);
                        m_connectionController->startConnection();
                    }
                });
        }
    }

    std::clog << "SystemTray: 初始化完成" << std::endl;
}

SystemTray::~SystemTray()
{
    std::clog << "SystemTray: 析构开始" << std::endl;
    if (m_configManager) {
        m_configManager->saveConfig();
    }
    std::clog << "SystemTray: 析构完成" << std::endl;
}

// ============================================================================
// ConnectionController 信号初始化
// ============================================================================
// 两级信号独立连接，各自更新各自的 UI 行，互不干扰。

void SystemTray::initController()
{
    // --- 后端 IPC 状态变化 → 更新"守护进程"行 ---
    connect(m_connectionController, &ConnectionController::daemonStateChanged,
            this, [this](const DaemonState ds) {
        updateStatus(ds, ServiceState::Idle);
    });

    // --- 配置服务器状态变化 → 更新"状态"行和按钮 ---
    connect(m_connectionController, &ConnectionController::serviceStateChanged,
            this, [this](const ServiceState ss) {
        updateStatus(m_connectionController->daemonState(), ss);
    });

    // --- 连接成功 → 托盘通知 ---
    connect(m_connectionController, &ConnectionController::connected,
            this, [this]() {
        m_trayIcon->showMessage(APP_DISPLAY_NAME, "已连接配置服务器",
                                QSystemTrayIcon::Information, 3000);
    });

    // --- 连接失败 → 弹出错误对话框 ---
    connect(m_connectionController, &ConnectionController::connectionFailed,
            this, [this](const QString &error) {
        QMessageBox::warning(nullptr, "连接失败",
                             error.isEmpty() ? "未知错误" : error);
    });

    // --- 退出前的清理完成 → 保存配置并退出 ---
    connect(m_connectionController, &ConnectionController::quitRequested,
            this, [this]() {
        saveSettings();
        QApplication::quit();
    });
}

// ============================================================================
// 托盘显示
// ============================================================================

void SystemTray::show() const
{
    QTimer::singleShot(100, [this]() {
        m_trayIcon->show();
        if (!m_isAutoStartMode) {
            m_trayIcon->showMessage(
                APP_DISPLAY_NAME,
                QString("%1 已启动").arg(APP_DISPLAY_NAME),
                QSystemTrayIcon::Information,
                3000
            );
        }
    });
}

void SystemTray::setAutoStartMode(bool autoStart)
{
    m_isAutoStartMode = autoStart;
}

// ============================================================================
// 菜单构建
// ============================================================================

void SystemTray::setupMenu()
{
    m_titleAction = new QAction(QIcon(FAVICON_SVG), APP_DISPLAY_NAME, this);
    QFont titleFont = m_titleAction->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_titleAction->setFont(titleFont);
    m_menu->addAction(m_titleAction);

    m_separator1 = m_menu->addSeparator();

    m_userAction = new QAction(QIcon(":/assets/user.svg"), "用户：未设置", this);
    m_menu->addAction(m_userAction);

    m_statusAction = new QAction(QIcon(":/assets/status-red.svg"), "状态：未连接", this);
    m_menu->addAction(m_statusAction);


    m_separator2 = m_menu->addSeparator();

    m_toggleConnectionAction = new QAction(QIcon(":/assets/connect.svg"),
                                           "启动连接",
                                           this);
    m_toggleConnectionAction->setEnabled(false);
    connect(m_toggleConnectionAction, &QAction::triggered, this, &SystemTray::onToggleConnection);
    m_menu->addAction(m_toggleConnectionAction);

    m_separator3 = m_menu->addSeparator();

    m_settingsAction = new QAction(QIcon(":/assets/settings.svg"), "设置连接地址与密钥", this);
    connect(m_settingsAction, &QAction::triggered, this, &SystemTray::onSettings);
    m_menu->addAction(m_settingsAction);

    m_clearConnectionAction = new QAction(QIcon(":/assets/clear.svg"), "清空连接信息", this);
    connect(m_clearConnectionAction, &QAction::triggered, this, &SystemTray::onClearConnectionInfo);
    m_menu->addAction(m_clearConnectionAction);

    m_separator4 = m_menu->addSeparator();

    m_autoStartAction = new QAction("开机启动托盘程序", this);
    m_autoStartAction->setIcon(QIcon(":/assets/startup.svg"));
    m_autoStartAction->setCheckable(true);
    m_autoStartAction->setChecked(m_autoStart);
    m_autoStartAction->setToolTip("开机后启动托盘程序，ET Connector 会自动连接");
    connect(m_autoStartAction, &QAction::toggled, this, &SystemTray::onAutoStart);
    m_menu->addAction(m_autoStartAction);

    m_autoConnectAction = new QAction("自动连接", this);
    m_autoConnectAction->setCheckable(true);
    m_autoConnectAction->setChecked(m_autoConnect);
    m_autoConnectAction->setToolTip("启动程序后自动尝试连接配置服务器");
    connect(m_autoConnectAction, &QAction::toggled, this, &SystemTray::onAutoConnect);
    m_menu->addAction(m_autoConnectAction);

    m_separator5 = m_menu->addSeparator();

    m_aboutAction = new QAction(QIcon(":/assets/about.svg"), "关于软件", this);
    connect(m_aboutAction, &QAction::triggered, this, &SystemTray::onAbout);
    m_menu->addAction(m_aboutAction);

    m_quitAction = new QAction(QIcon(":/assets/quit.svg"), "退出客户端", this);
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::onQuit);
    m_menu->addAction(m_quitAction);

    m_backendStatusAction = new QAction(QIcon(":/assets/status-red.svg"), "守护进程：未连接", this);
    m_menu->addAction(m_backendStatusAction);
}

// ============================================================================
// UI 更新
// ============================================================================

void SystemTray::updateUserStatus()
{
    if (!m_configManager->getConnectionKey().isEmpty()) {
        m_userAction->setText("用户：已设置");
        m_userAction->setIcon(QIcon(":/assets/user-loggedin.svg"));
    } else {
        m_userAction->setText("用户：未设置");
        m_userAction->setIcon(QIcon(":/assets/user.svg"));
    }
}

void SystemTray::updateStatus(DaemonState daemon, ServiceState service)
{
    // ---- 后端守护进程行 ----
    if (daemon == DaemonState::Connected) {
        m_backendStatusAction->setText("守护进程：已连接");
        m_backendStatusAction->setIcon(QIcon(":/assets/status-green.svg"));
    } else {
        m_backendStatusAction->setText("守护进程：未连接");
        m_backendStatusAction->setIcon(QIcon(":/assets/status-red.svg"));
    }

    // ---- 配置服务器状态行 ----
    QString statusText;
    QString statusIcon;
    QString tooltipSuffix;

    if (daemon == DaemonState::Disconnected) {
        statusText = "状态：后端未连接";
        statusIcon = ":/assets/status-red.svg";
        tooltipSuffix = "后端未连接";
    } else {
        switch (service) {
        case ServiceState::Idle:
            statusText = "状态：未连接";
            statusIcon = ":/assets/status-red.svg";
            tooltipSuffix = "未连接";
            break;
        case ServiceState::Connecting:
            statusText = "状态：连接中";
            statusIcon = ":/assets/status-yellow.svg";
            tooltipSuffix = "连接中";
            break;
        case ServiceState::Connected:
            statusText = "状态：已连接";
            statusIcon = ":/assets/status-green.svg";
            tooltipSuffix = "已连接";
            break;
        case ServiceState::Stopping:
            statusText = "状态：停止中";
            statusIcon = ":/assets/status-yellow.svg";
            tooltipSuffix = "停止中";
            break;
        case ServiceState::Unknown:
            statusText = "状态：Unknown";
            statusIcon = ":/assets/status-red.svg";
            tooltipSuffix = "Unknown";
            break;
        }
    }

    m_statusAction->setText(statusText);
    m_statusAction->setIcon(QIcon(statusIcon));
    m_trayIcon->setToolTip(QString("%1 - %2").arg(APP_DISPLAY_NAME, tooltipSuffix));

    // ---- "启动/停止连接"按钮 ----
    updateConnectionActions(service);
}

void SystemTray::updateConnectionActions(ServiceState service) const
{
    const DaemonState daemon = m_connectionController->daemonState();

    if (daemon == DaemonState::Disconnected) {
        m_toggleConnectionAction->setText("启动连接");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/connect.svg"));
        m_toggleConnectionAction->setEnabled(false);
        return;
    }

    switch (service) {
    case ServiceState::Idle:
        m_toggleConnectionAction->setText("启动连接");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/connect.svg"));
        m_toggleConnectionAction->setEnabled(true);
        break;
    case ServiceState::Connecting:
        m_toggleConnectionAction->setText("处理中...");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/disconnect.svg"));
        m_toggleConnectionAction->setEnabled(false);
        break;
    case ServiceState::Connected:
        m_toggleConnectionAction->setText("停止连接");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/disconnect.svg"));
        m_toggleConnectionAction->setEnabled(true);
        break;
    case ServiceState::Stopping:
        m_toggleConnectionAction->setText("处理中...");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/disconnect.svg"));
        m_toggleConnectionAction->setEnabled(false);
        break;
    case ServiceState::Unknown:
        m_toggleConnectionAction->setText("启动连接");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/connect.svg"));
        m_toggleConnectionAction->setEnabled(true);
        break;
    }
}

// ============================================================================
// 配置读写
// ============================================================================

void SystemTray::loadSettings()
{
    m_autoStart = m_configManager->getAutoStart();
    m_autoConnect = m_configManager->getAutoConnect();

    if (const bool registered = isAutoStartRegistered(); m_autoStart != registered) {
        m_autoStart = registered;
        m_configManager->setAutoStart(m_autoStart);
        m_configManager->saveConfig();
    }
}

void SystemTray::saveSettings() const {
    m_configManager->setAutoStart(m_autoStart);
    m_configManager->setAutoConnect(m_autoConnect);
    m_configManager->saveConfig();
}

// ============================================================================
// 用户操作响应
// ============================================================================

void SystemTray::onToggleConnection() const {
    const auto ss = m_connectionController->serviceState();
    if (ss != ServiceState::Idle && ss != ServiceState::Unknown) {
        m_connectionController->stopConnection();
    } else {
        m_connectionController->startConnection();
    }
}

void SystemTray::onSettings()
{
    if (m_settingsDialog.isNull()) {
        m_settingsDialog = new SettingsDialog(m_configManager->getConnectionKey());
    }

    m_settingsDialog->setConnectionKey(m_configManager->getConnectionKey());
    m_settingsDialog->setSecureMode(m_configManager->getSecureMode());

    const QString oldKey = m_configManager->getConnectionKey();

    if (m_settingsDialog->exec() == QDialog::Accepted) {
        const QString newKey = m_settingsDialog->getConnectionKey();
        const bool newSecure = m_settingsDialog->getSecureMode();

        m_configManager->setConnectionKey(newKey);
        m_configManager->setSecureMode(newSecure);
        m_configManager->saveConfig();

        updateUserStatus();

        const auto ss = m_connectionController->serviceState();
        if (ss != ServiceState::Idle && ss != ServiceState::Unknown && newKey != oldKey) {
            m_connectionController->restartAfterKeyChange();
        }
    }

    if (!m_settingsDialog.isNull()) {
        m_settingsDialog->deleteLater();
    }
}

void SystemTray::onClearConnectionInfo()
{
    QMessageBox::StandardButton reply = QMessageBox::question(nullptr,
        "确认清空",
        "确定要清空连接地址与密钥吗？\n清空后需要重新设置才能连接。",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    m_configManager->setConnectionKey(QString());
    m_configManager->saveConfig();
    updateUserStatus();

    const auto ss = m_connectionController->serviceState();
    if (ss != ServiceState::Idle && ss != ServiceState::Unknown) {
        m_connectionController->stopConnection();
    } else {
        m_trayIcon->showMessage("EasyTier", "连接信息已清空",
                                QSystemTrayIcon::Information, 3000);
    }
}

void SystemTray::onAutoStart(bool checked)
{
    m_autoStart = checked;
    m_configManager->setAutoStart(m_autoStart);
    m_configManager->saveConfig();

    bool success;
    if (checked) {
        success = registerAutoStart();
    } else {
        success = unregisterAutoStart();
    }

    if (!success) {
        m_autoStartAction->blockSignals(true);
        m_autoStartAction->setChecked(!checked);
        m_autoStartAction->blockSignals(false);

        QMessageBox::warning(nullptr, "提示",
            checked ? "设置开机自启失败" : "取消开机自启失败");
    }
}

void SystemTray::onAutoConnect(bool checked)
{
    m_autoConnect = checked;
    m_configManager->setAutoConnect(m_autoConnect);
    m_configManager->saveConfig();
}

// ============================================================================
// 退出流程 - 必须停止连接后才能退出
// ============================================================================

void SystemTray::onQuit()
{
    const auto ss = m_connectionController->serviceState();
    if (ss != ServiceState::Idle && ss != ServiceState::Unknown) {
        m_connectionController->requestQuit();
    } else {
        saveSettings();
        QApplication::quit();
    }
}

void SystemTray::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    Q_UNUSED(reason);
}

// ============================================================================
// 开机自启管理 - 平台特定实现
// ============================================================================

bool SystemTray::registerAutoStart()
{
    const QString appPath = QApplication::applicationFilePath();

#ifdef Q_OS_WIN32
    QString value = QString("\"%1\" --auto-start").arg(appPath);
    value.replace('/', '\\');

    QSettings settings(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)",
                       QSettings::NativeFormat);
    settings.setValue("EasyTierConnector", value);
    settings.sync();

    bool success = (settings.status() == QSettings::NoError);
    std::clog << "注册开机自启: " << (success ? "成功" : "失败") << std::endl;
    return success;
#elif defined(Q_OS_MACOS)
    QString launchAgentsDir = QDir::homePath() + "/Library/LaunchAgents";
    QDir dir(launchAgentsDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString plistPath = launchAgentsDir + "/com.easytier.connector.plist";
    QString plistContent = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "    <key>Label</key>\n"
        "    <string>com.easytier.connector</string>\n"
        "    <key>ProgramArguments</key>\n"
        "    <array>\n"
        "        <string>%1</string>\n"
        "        <string>--auto-start</string>\n"
        "    </array>\n"
        "    <key>RunAtLoad</key>\n"
        "    <true/>\n"
        "    <key>KeepAlive</key>\n"
        "    <false/>\n"
        "</dict>\n"
        "</plist>\n"
    ).arg(appPath);

    QFile plistFile(plistPath);
    if (!plistFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "注册开机自启: 失败，无法写入 plist 文件" << std::endl;
        return false;
    }

    plistFile.write(plistContent.toUtf8());
    plistFile.close();

    std::clog << "注册开机自启: 成功" << std::endl;
    return true;
#else
    QString desktopFilePath = QDir::homePath() + "/.config/autostart/EasyTierConnector.desktop";
    QDir autostartDir(QDir::homePath() + "/.config/autostart");
    if (!autostartDir.exists()) {
        autostartDir.mkpath(".");
    }

    QString desktopEntry = QString("[Desktop Entry]\n"
                                   "Type=Application\n"
                                   "Name=EasyTierConnector\n"
                                   "Exec=%1 --auto-start\n"
                                   "Hidden=false\n"
                                   "NoDisplay=false\n"
                                   "X-GNOME-Autostart-enabled=true\n")
                               .arg(appPath);

    QFile desktopFile(desktopFilePath);
    if (!desktopFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "注册开机自启: 失败，无法写入 .desktop 文件" << std::endl;
        return false;
    }

    desktopFile.write(desktopEntry.toUtf8());
    desktopFile.close();

    std::clog << "注册开机自启: 成功" << std::endl;
    return true;
#endif
}

bool SystemTray::unregisterAutoStart()
{
#ifdef Q_OS_WIN32
    QSettings settings(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)",
                       QSettings::NativeFormat);
    settings.remove("EasyTierConnector");
    settings.sync();

    bool success = (settings.status() == QSettings::NoError);
    std::clog << "取消开机自启: " << (success ? "成功" : "失败") << std::endl;
    return success;
#elif defined(Q_OS_MACOS)
    QString plistPath = QDir::homePath() + "/Library/LaunchAgents/com.easytier.connector.plist";
    bool success = QFile::remove(plistPath);
    std::clog << "取消开机自启: " << (success ? "成功" : "失败") << std::endl;
    return success;
#else
    QString desktopFilePath = QDir::homePath() + "/.config/autostart/EasyTierConnector.desktop";
    bool success = QFile::remove(desktopFilePath);
    std::clog << "取消开机自启: " << (success ? "成功" : "失败") << std::endl;
    return success;
#endif
}

bool SystemTray::isAutoStartRegistered()
{
#ifdef Q_OS_WIN32
    QSettings settings(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)",
                       QSettings::NativeFormat);
    return settings.contains("EasyTierConnector");
#elif defined(Q_OS_MACOS)
    QString plistPath = QDir::homePath() + "/Library/LaunchAgents/com.easytier.connector.plist";
    return QFile::exists(plistPath);
#else
    QString desktopFilePath = QDir::homePath() + "/.config/autostart/EasyTierConnector.desktop";
    return QFile::exists(desktopFilePath);
#endif
}

void SystemTray::onAbout()
{
    if (m_aboutDialog.isNull()) {
        m_aboutDialog = new AboutDialog();
    }
    m_aboutDialog->exec();

    if (!m_aboutDialog.isNull()) {
        m_aboutDialog->deleteLater();
    }
}
