/**
 * @file SystemTray.cpp
 * @brief 系统托盘主类实现
 */

#include "SystemTray.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QTimer>
#include <QThread>
#include <QProcess>
#include <QFile>
#include <iostream>
#include <QDir>

SystemTray::SystemTray(QObject *parent)
    : QObject(parent)
{
    std::clog << "SystemTray: 初始化开始" << std::endl;
    
    // 1. 初始化配置管理器
    m_configManager = new ConfigManager(this);
    if (!m_configManager->loadConfig()) {
        std::cerr << "SystemTray: 配置加载失败，使用默认配置" << std::endl;
    }
    
    // 2. 加载配置
    loadSettings();
    
    // 3. 创建系统托盘图标
    m_trayIcon = new QSystemTrayIcon(this);
    QIcon icon(FAVICON_SVG);
    if (icon.isNull()) {
        std::cerr << "SystemTray: 无法加载托盘图标" << std::endl;
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(APP_DISPLAY_NAME);
    
    // 4. 创建菜单
    m_menu = new QMenu();
    setupMenu();
    m_trayIcon->setContextMenu(m_menu);
    
    // 5. 连接托盘激活信号
    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &SystemTray::onTrayActivated);

    // 6. 检查服务是否已在运行
    if (ETRunService::isRunning()) {
        updateStatus(ConnectionState::Connected);
        std::clog << "SystemTray: EasyTier 服务已在运行中" << std::endl;
    }
    
    // 7. 启动心跳定时器，每2秒检测 easytier-core 进程状态
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &SystemTray::onHeartbeat);
    m_heartbeatTimer->start(2000);
    
    // 8. 更新用户状态显示
    updateUserStatus();
    
    std::clog << "SystemTray: 初始化完成" << std::endl;
}

SystemTray::~SystemTray()
{
    std::clog << "SystemTray: 析构开始" << std::endl;
    
    // 保存配置
    if (m_configManager) {
        m_configManager->saveConfig();
    }
    
    // 对话框由 QPointer 自动管理，无需手动删除
    
    std::clog << "SystemTray: 析构完成" << std::endl;
}

void SystemTray::show() const
{
    // 延迟显示，确保 UI 完全初始化
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

void SystemTray::setupMenu()
{
    // 标题
    m_titleAction = new QAction(QIcon(FAVICON_SVG), APP_DISPLAY_NAME, this);
    QFont titleFont = m_titleAction->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_titleAction->setFont(titleFont);
    m_menu->addAction(m_titleAction);
    
    // 连接状态
    m_statusAction = new QAction(QIcon(":/assets/status-red.svg"), "状态：未启动", this);
    m_menu->addAction(m_statusAction);
    
    m_separator1 = m_menu->addSeparator();

    // 启动/停止连接
    m_toggleConnectionAction = new QAction(QIcon(":/assets/connect.svg"),
                                           "启动连接",
                                           this);
    connect(m_toggleConnectionAction, &QAction::triggered, this, &SystemTray::onToggleConnection);
    m_menu->addAction(m_toggleConnectionAction);
    
    m_separator2 = m_menu->addSeparator();
    
    // 设置连接地址与密钥
    m_settingsAction = new QAction(QIcon(":/assets/settings.svg"), "设置连接地址与密钥", this);
    connect(m_settingsAction, &QAction::triggered, this, &SystemTray::onSettings);
    m_menu->addAction(m_settingsAction);

    // 清空连接信息
    m_clearConnectionAction = new QAction(QIcon(":/assets/clear.svg"), "清空连接信息", this);
    connect(m_clearConnectionAction, &QAction::triggered, this, &SystemTray::onClearConnectionInfo);
    m_menu->addAction(m_clearConnectionAction);

    m_separator3 = m_menu->addSeparator();
    
    // 开机自启
    m_autoStartAction = new QAction("开机启动托盘程序", this);
    m_autoStartAction->setIcon(QIcon(":/assets/startup.svg"));
    m_autoStartAction->setCheckable(true);
    m_autoStartAction->setChecked(m_autoStart);
    m_autoStartAction->setToolTip("开机后启动托盘程序，ET Connector 会自动连接");
    connect(m_autoStartAction, &QAction::toggled, this, &SystemTray::onAutoStart);
    m_menu->addAction(m_autoStartAction);
    
    m_separator4 = m_menu->addSeparator();
    
    // 关于
    m_aboutAction = new QAction(QIcon(":/assets/about.svg"), "关于软件", this);
    connect(m_aboutAction, &QAction::triggered, this, &SystemTray::onAbout);
    m_menu->addAction(m_aboutAction);
    
    // 退出
    m_quitAction = new QAction(QIcon(":/assets/quit.svg"), "退出客户端", this);
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::onQuit);
    m_menu->addAction(m_quitAction);
}

void SystemTray::updateUserStatus()
{
    if (!m_connectionKey.isEmpty()) {
        m_statusAction->setText("状态：已设置");
    } else {
        m_statusAction->setText("状态：未设置");
    }
}

void SystemTray::updateStatus(ConnectionState state)
{
    m_connectionState = state;
    
    QString statusText;
    QString tooltipText;
    QString statusIcon;
    
    switch (state) {
    case ConnectionState::NotStarted:
        statusText = "状态：未启动";
        tooltipText = QString("%1 - 未启动").arg(APP_DISPLAY_NAME);
        statusIcon = ":/assets/status-red.svg";
        break;
    case ConnectionState::Connecting:
        statusText = "状态：连接中";
        tooltipText = QString("%1 - 连接中").arg(APP_DISPLAY_NAME);
        statusIcon = ":/assets/status-yellow.svg";
        break;
    case ConnectionState::Connected:
        statusText = "状态：已连接";
        tooltipText = QString("%1 - 已连接").arg(APP_DISPLAY_NAME);
        statusIcon = ":/assets/status-green.svg";
        break;
    }
    
    m_statusAction->setText(statusText);
    m_statusAction->setIcon(QIcon(statusIcon));
    m_trayIcon->setToolTip(tooltipText);
    updateConnectionActions();
}

void SystemTray::updateConnectionActions() const
{
    switch (m_connectionState) {
    case ConnectionState::NotStarted:
        m_toggleConnectionAction->setText("启动连接");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/connect.svg"));
        m_toggleConnectionAction->setEnabled(true);
        break;
    case ConnectionState::Connecting:
        m_toggleConnectionAction->setText("处理中...");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/disconnect.svg"));
        m_toggleConnectionAction->setEnabled(false);
        break;
    case ConnectionState::Connected:
        m_toggleConnectionAction->setText("停止连接");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/disconnect.svg"));
        m_toggleConnectionAction->setEnabled(true);
        break;
    }
}

void SystemTray::loadSettings()
{
    m_autoStart = m_configManager->getAutoStart();
    m_connectionKey = m_configManager->getConnectionKey();
    
    // 同步注册表实际状态到 UI
    bool registered = isAutoStartRegistered();
    if (m_autoStart != registered) {
        m_autoStart = registered;
        m_configManager->setAutoStart(m_autoStart);
        m_configManager->saveConfig();
    }
}

void SystemTray::saveSettings()
{
    m_configManager->setAutoStart(m_autoStart);
    m_configManager->setConnectionKey(m_connectionKey);
    m_configManager->saveConfig();
}

bool SystemTray::startConnection(bool showNotification)
{
    if (m_connectionKey.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先设置连接地址与密钥");
        return false;
    }

    showProgressDialog("正在连接 EasyTier...");
    updateStatus(ConnectionState::Connecting);

    CommandResult result;
    bool success = ETRunService::startOrInstall(m_connectionKey, &result);
    closeProgressDialog();

    if (success) {
        updateStatus(ConnectionState::Connected);
        m_lastError.clear();
        if (showNotification) {
            m_trayIcon->showMessage(APP_DISPLAY_NAME, "已连接 EasyTier",
                                    QSystemTrayIcon::Information, 3000);
        }
        return true;
    }

    rememberLastError("连接 EasyTier 失败", result);
    updateStatus(ConnectionState::NotStarted);
    if (showNotification) {
        m_trayIcon->showMessage("连接失败", m_lastError, QSystemTrayIcon::Warning, 5000);
    }
    return false;
}

bool SystemTray::pauseConnection(bool showNotification)
{
    showProgressDialog("正在暂停连接...");
    updateStatus(ConnectionState::Connecting);

    CommandResult result;
    bool success = ETRunService::pauseConnection(&result);
    closeProgressDialog();

    if (success) {
        updateStatus(ConnectionState::NotStarted);
        m_lastError.clear();
        if (showNotification) {
            m_trayIcon->showMessage(APP_DISPLAY_NAME, "连接已暂停",
                                    QSystemTrayIcon::Information, 3000);
        }
        return true;
    }

    rememberLastError("暂停连接失败", result);
    updateStatus(ETRunService::isRunning() ? ConnectionState::Connected : ConnectionState::NotStarted);
    if (showNotification) {
        m_trayIcon->showMessage("暂停失败", m_lastError, QSystemTrayIcon::Warning, 5000);
    }
    return false;
}

void SystemTray::rememberLastError(const QString &context, const CommandResult &result)
{
    QString detail = result.errorString.trimmed();
    if (detail.isEmpty()) {
        detail = ETRunService::serviceErrorToString(result.error);
    }
    m_lastError = QString("%1：%2").arg(context, detail);
}

void SystemTray::onToggleConnection()
{
    if (m_connectionState == ConnectionState::NotStarted) {
        startConnection(true);
    } else if (m_connectionState == ConnectionState::Connected) {
        pauseConnection(true);
    }
}

void SystemTray::onSettings()
{
    // 创建或复用对话框
    if (m_settingsDialog.isNull()) {
        m_settingsDialog = new SettingsDialog(m_connectionKey);
        connect(m_settingsDialog.data(), &SettingsDialog::connectionKeyChanged,
                this, &SystemTray::onConnectionKeyChanged);
    }
    
    m_settingsDialog->setConnectionKey(m_connectionKey);
    
    if (m_settingsDialog->exec() == QDialog::Accepted) {
        m_connectionKey = m_settingsDialog->getConnectionKey();
        m_configManager->setConnectionKey(m_connectionKey);
        m_configManager->saveConfig();
        updateUserStatus();
    }
     
    // QPointer 会在对象删除后自动变为 nullptr
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

    // 清空连接信息
    m_connectionKey.clear();
    m_configManager->setConnectionKey(QString());
    m_configManager->saveConfig();
    updateUserStatus();

    // 如果服务正在运行，停止服务
    if (ETRunService::isRunning()) {
        showProgressDialog("正在停止 EasyTier 服务...");
        CommandResult result;
        bool stopped = ETRunService::pauseConnection(&result);
        closeProgressDialog();

        if (stopped) {
            updateStatus(ConnectionState::NotStarted);
            m_trayIcon->showMessage("EasyTier", "连接信息已清空，服务已停止",
                                    QSystemTrayIcon::Information, 3000);
        } else {
            rememberLastError("停止服务失败", result);
            m_trayIcon->showMessage("错误", "服务停止失败",
                                    QSystemTrayIcon::Warning, 5000);
        }
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
        // 操作失败，恢复 UI 状态
        m_autoStartAction->blockSignals(true);
        m_autoStartAction->setChecked(!checked);
        m_autoStartAction->blockSignals(false);
        
        QMessageBox::warning(nullptr, "提示", 
            checked ? "设置开机自启失败" : "取消开机自启失败");
    }
}

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
    // macOS: 使用 ~/Library/LaunchAgents/ 目录下的 plist 文件
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
    // Linux: 使用 ~/.config/autostart/ 目录下的 .desktop 文件
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

void SystemTray::onQuit()
{
    // 如果服务未运行，直接退出
    if (!ETRunService::isRunning()) {
        saveSettings();
        QApplication::quit();
        return;
    }

    // 服务正在运行，检查是否有记住的选择
    bool rememberQuitChoice = m_configManager->getRememberQuitChoice();
    bool stopOnQuit = m_configManager->getStopOnQuit();

    if (rememberQuitChoice) {
        // 已记住选择，按记忆执行
        if (stopOnQuit) {
            // 记住的选择是"停止并退出"
            showProgressDialog("正在停止 EasyTier 服务...");
            CommandResult result;
            bool stopped = ETRunService::pauseConnection(&result);
            closeProgressDialog();

            if (stopped) {
                saveSettings();
                QApplication::quit();
            } else {
                rememberLastError("退出前停止服务失败", result);
                m_trayIcon->showMessage("错误", "EasyTier 服务停止失败，程序保持运行",
                                        QSystemTrayIcon::Warning, 5000);
            }
        } else {
            // 记住的选择是"仅退出前端"
            saveSettings();
            QApplication::quit();
        }
        return;
    }

    // 未记住选择，弹出确认对话框
    if (m_quitConfirmDialog.isNull()) {
        m_quitConfirmDialog = new QuitConfirmDialog();
    }

    if (m_quitConfirmDialog->exec() == QDialog::Accepted) {
        bool choseStopAndQuit = m_quitConfirmDialog->choseStopAndQuit();
        bool rememberChoice = m_quitConfirmDialog->isRememberChoice();

        // 如果用户勾选了"记住我的选择"，保存到配置
        if (rememberChoice) {
            m_configManager->setRememberQuitChoice(true);
            m_configManager->setStopOnQuit(choseStopAndQuit);
            m_configManager->saveConfig();
        }

        if (choseStopAndQuit) {
            // 用户选择"是"：停止服务后退出
            showProgressDialog("正在停止 EasyTier 服务...");
            CommandResult result;
            bool stopped = ETRunService::pauseConnection(&result);
            closeProgressDialog();

            if (stopped) {
                saveSettings();
                QApplication::quit();
            } else {
                rememberLastError("退出前停止服务失败", result);
                m_trayIcon->showMessage("错误", "EasyTier 服务停止失败，程序保持运行",
                                        QSystemTrayIcon::Warning, 5000);
            }
        } else {
            // 用户选择"否"：仅退出前端
            saveSettings();
            QApplication::quit();
        }
    }

    // 清理对话框
    if (!m_quitConfirmDialog.isNull()) {
        m_quitConfirmDialog->deleteLater();
    }
}

void SystemTray::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    Q_UNUSED(reason);
}

void SystemTray::onHeartbeat()
{
    bool running = ETRunService::isRunning();
    
    // 只在状态发生变化时更新 UI，避免频繁刷新
    if (running && m_connectionState != ConnectionState::Connected) {
        updateStatus(ConnectionState::Connected);
        std::clog << "SystemTray: 心跳检测 - 服务已上线" << std::endl;
    } else if (!running && m_connectionState == ConnectionState::Connected) {
        updateStatus(ConnectionState::NotStarted);
        std::clog << "SystemTray: 心跳检测 - 服务已离线" << std::endl;
    }
}

void SystemTray::onConnectionKeyChanged()
{
    if (m_settingsDialog.isNull()) {
        return;
    }
    
    m_connectionKey = m_settingsDialog->getConnectionKey();
    m_configManager->setConnectionKey(m_connectionKey);
    m_configManager->saveConfig();
    
    // 更新用户状态
    updateUserStatus();
    
    // 如果服务已运行
    if (ETRunService::isRunning()) {
        if (m_connectionKey.isEmpty()) {
            // 密钥被清空，停止服务
            showProgressDialog("正在停止 EasyTier 服务...");
            CommandResult result;
            bool stopped = ETRunService::pauseConnection(&result);
            closeProgressDialog();

            if (stopped) {
                updateStatus(ConnectionState::NotStarted);
                m_trayIcon->showMessage("EasyTier", "连接密钥已清空，服务已停止",
                                        QSystemTrayIcon::Information, 3000);
            } else {
                rememberLastError("连接密钥清空后停止服务失败", result);
                m_trayIcon->showMessage("错误", "服务停止失败",
                                        QSystemTrayIcon::Warning, 5000);
            }
        } else {
            // 密钥变更，重启服务
            showProgressDialog("正在重启 EasyTier 服务...");

            CommandResult stopResult;
            bool stopped = ETRunService::removeService(&stopResult);
            bool started = false;
            CommandResult startResult;

            if (stopped) {
                // 等待资源释放
                QThread::msleep(500);
                started = ETRunService::startOrInstall(m_connectionKey, &startResult);
            }

            closeProgressDialog();

            if (started) {
                updateStatus(ConnectionState::Connected);
                m_trayIcon->showMessage("EasyTier", "服务已重启",
                                        QSystemTrayIcon::Information, 3000);
            } else {
                rememberLastError("服务重启失败", stopped ? startResult : stopResult);
                updateStatus(ConnectionState::NotStarted);
                m_trayIcon->showMessage("错误", "服务重启失败",
                                        QSystemTrayIcon::Warning, 5000);
            }
        }
    }
}

void SystemTray::showProgressDialog(const QString &text)
{
    if (m_progressDialog == nullptr) {
        m_progressDialog = new QProgressDialog(text, QString(), 0, 0, nullptr);
        m_progressDialog->setWindowTitle("请稍候");
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setCancelButton(nullptr);
        m_progressDialog->setMinimumDuration(0);
    }
    m_progressDialog->setLabelText(text);
    m_progressDialog->show();
    QApplication::processEvents();
}

void SystemTray::closeProgressDialog()
{
    if (m_progressDialog != nullptr) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
}
