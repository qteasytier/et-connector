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
#include <QRegularExpression>

SystemTray::SystemTray(QObject *parent)
    : QObject(parent)
{
    std::clog << "SystemTray: 初始化开始" << std::endl;
    
    // 1. 初始化配置管理器
    m_configManager = new ConfigManager(this);
    if (!m_configManager->loadConfig()) {
        std::cerr << "SystemTray: 配置加载失败，使用默认配置" << std::endl;
    }
    
    // 1.5. 初始化 Casdoor 登录器（仅专业版）
#ifndef IS_COMMUNITY_VER
    m_casdoorLogin = new CasdoorLogin("d3ff87a9cd6317695066", this);
    connect(m_casdoorLogin, &CasdoorLogin::loginSuccess, this, [this](const QString &deviceKey, const QString &displayName, const QString &userId, const QString &userDisplayName, const QString &tenantName) {
        handleLoginSuccess(deviceKey, displayName, userId, userDisplayName, tenantName);
    });
    connect(m_casdoorLogin, &CasdoorLogin::loginFailed, this, [this](const QString &errorMessage) {
        closeProgressDialog();
        m_lastError = errorMessage;
        QMessageBox::warning(nullptr, "登录失败", errorMessage);
    });
    connect(m_casdoorLogin, &CasdoorLogin::proTenantsUpdated, this, &SystemTray::updateTenantMenu);
#endif
    
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
    
    // 8. 更新用户登录状态显示
#ifndef IS_COMMUNITY_VER
    updateUserStatus();
#else
    updateCommunityUserStatus();
#endif
    
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
#ifndef IS_COMMUNITY_VER
    maybeAutoConnectOnStartup();
#endif
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
    
#ifndef IS_COMMUNITY_VER
    // 专业版：用户登录状态 + 组织信息
    m_userStatusAction = new QAction(QIcon(":/assets/user.svg"), "用户：未登录", this);
    m_menu->addAction(m_userStatusAction);
    
    m_tenantStatusAction = new QAction(QIcon(":/assets/tenant.svg"), "", this);
    m_tenantStatusAction->setVisible(false);
    m_menu->addAction(m_tenantStatusAction);
#endif
    
    // 连接状态
    m_statusAction = new QAction(QIcon(":/assets/status-red.svg"), "状态：未启动", this);
    m_menu->addAction(m_statusAction);
    
    m_separator1 = m_menu->addSeparator();

#ifndef IS_COMMUNITY_VER
    m_devicesAction = new QAction(QIcon(":/assets/webconsole.svg"), "我的设备", this);
    connect(m_devicesAction, &QAction::triggered, this, &SystemTray::onOpenDevices);
    m_menu->addAction(m_devicesAction);

    m_loginEasyTierProAction = new QAction(QIcon(":/assets/login.svg"), "登录 EasyTier Pro", this);
    connect(m_loginEasyTierProAction, &QAction::triggered, this, &SystemTray::onLoginEasyTierPro);
    m_menu->addAction(m_loginEasyTierProAction);

    m_tenantMenu = new QMenu("切换组织", m_menu);
    m_tenantMenu->setIcon(QIcon(":/assets/tenant.svg"));
    m_menu->addMenu(m_tenantMenu);
#endif
    
    // 启动/停止连接
    m_toggleConnectionAction = new QAction(QIcon(":/assets/connect.svg"),
#ifdef IS_COMMUNITY_VER
                                           "启动连接",
#else
                                           "连接 EasyTier Pro",
#endif
                                           this);
    connect(m_toggleConnectionAction, &QAction::triggered, this, &SystemTray::onToggleConnection);
    m_menu->addAction(m_toggleConnectionAction);
    
    m_separator2 = m_menu->addSeparator();
    
#ifdef IS_COMMUNITY_VER
    // 社区版：设置连接地址与密钥
    m_settingsAction = new QAction(QIcon(":/assets/settings.svg"), "设置连接地址与密钥", this);
    connect(m_settingsAction, &QAction::triggered, this, &SystemTray::onSettings);
    m_menu->addAction(m_settingsAction);
#endif

    // 清空连接信息
#ifdef IS_COMMUNITY_VER
    const QString clearText = "清空连接信息";
    m_clearConnectionAction = new QAction(QIcon(":/assets/clear.svg"), clearText, this);
    connect(m_clearConnectionAction, &QAction::triggered, this, &SystemTray::onClearConnectionInfo);
#else
    const QString clearText = "退出登录";
    m_clearConnectionAction = new QAction(QIcon(":/assets/clear.svg"), clearText, this);
    connect(m_clearConnectionAction, &QAction::triggered, this, &SystemTray::onLogout);
#endif
    m_menu->addAction(m_clearConnectionAction);

    m_separator3 = m_menu->addSeparator();
    
    // 开机自启
    m_autoStartAction = new QAction("开机启动托盘程序", this);
    m_autoStartAction->setIcon(QIcon(":/assets/startup.svg"));
    m_autoStartAction->setCheckable(true);
    m_autoStartAction->setChecked(m_autoStart);
    m_autoStartAction->setToolTip("开机后启动托盘程序，EasyTier Pro 会自动连接");
    connect(m_autoStartAction, &QAction::toggled, this, &SystemTray::onAutoStart);
    m_menu->addAction(m_autoStartAction);

#ifndef IS_COMMUNITY_VER
    m_diagnosticsAction = new QAction(QIcon(":/assets/about.svg"), "诊断", this);
    connect(m_diagnosticsAction, &QAction::triggered, this, &SystemTray::onDiagnostics);
    m_menu->addAction(m_diagnosticsAction);
#endif
    
    m_separator4 = m_menu->addSeparator();
    
#ifdef IS_COMMUNITY_VER
    // 关于
    m_aboutAction = new QAction(QIcon(":/assets/about.svg"), "关于软件", this);
    connect(m_aboutAction, &QAction::triggered, this, &SystemTray::onAbout);
    m_menu->addAction(m_aboutAction);
#endif
    
    // 退出
    m_quitAction = new QAction(QIcon(":/assets/quit.svg"), "退出客户端", this);
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::onQuit);
    m_menu->addAction(m_quitAction);
}

#ifndef IS_COMMUNITY_VER
void SystemTray::updateUserStatus()
{
    // 判断是否使用EasyTier Pro官方控制台地址
    QRegularExpression proRe("tcp://et-web\\.console\\.easytier\\.net:22020/(.+)");
    QRegularExpressionMatch proMatch = proRe.match(m_connectionKey);
    
    std::clog << "updateUserStatus: hasProMatch=" << proMatch.hasMatch()
              << " isEmpty=" << m_connectionKey.isEmpty() << std::endl;
    
    if (proMatch.hasMatch()) {
        // 使用官方控制台，检查OAuth登录状态
        QString currentDeviceKey = proMatch.captured(1).trimmed();
        QString oauthDeviceKey = m_configManager->getOAuthDeviceKey();
        
        if (currentDeviceKey == oauthDeviceKey && !oauthDeviceKey.isEmpty()) {
            // 一致，显示用户名
            QString userDisplayName = m_configManager->getUserDisplayName();
            if (!userDisplayName.isEmpty()) {
                m_userStatusAction->setText(QString("用户：%1").arg(userDisplayName));
                m_userStatusAction->setIcon(QIcon(":/assets/user-loggedin.svg"));
                m_loginEasyTierProAction->setText("切换账号");
                m_clearConnectionAction->setEnabled(true);
                m_devicesAction->setEnabled(true);
                updateTenantMenu();
                
                // 显示组织信息
                QString tenantName = m_configManager->getTenantDisplayName();
                if (!tenantName.isEmpty()) {
                    m_tenantStatusAction->setText(QString("组织：%1").arg(tenantName));
                    m_tenantStatusAction->setVisible(true);
                } else {
                    m_tenantStatusAction->setVisible(false);
                }
                
                std::clog << "updateUserStatus: 显示已登录用户" << std::endl;
                return;
            }
        }
        
        // 不一致或未保存，显示未登录
        m_tenantStatusAction->setVisible(false);
        m_userStatusAction->setText("用户：未登录");
        m_userStatusAction->setIcon(QIcon(":/assets/user.svg"));
        m_loginEasyTierProAction->setText("登录 EasyTier Pro");
        m_clearConnectionAction->setEnabled(false);
        m_devicesAction->setEnabled(false);
        updateTenantMenu();
        std::clog << "updateUserStatus: 显示未登录" << std::endl;
    } else if (!m_connectionKey.isEmpty()) {
        // 使用自定义连接信息
        m_tenantStatusAction->setVisible(false);
        m_userStatusAction->setText("用户：未登录");
        m_userStatusAction->setIcon(QIcon(":/assets/user.svg"));
        m_loginEasyTierProAction->setText("登录 EasyTier Pro");
        m_clearConnectionAction->setEnabled(false);
        m_devicesAction->setEnabled(false);
        updateTenantMenu();
        std::clog << "updateUserStatus: 专业版忽略自定义连接" << std::endl;
    } else {
        // 未设置连接地址
        m_tenantStatusAction->setVisible(false);
        m_userStatusAction->setText("用户：未登录");
        m_userStatusAction->setIcon(QIcon(":/assets/user.svg"));
        m_loginEasyTierProAction->setText("登录 EasyTier Pro");
        m_clearConnectionAction->setEnabled(false);
        m_devicesAction->setEnabled(false);
        updateTenantMenu();
        std::clog << "updateUserStatus: 显示未登录(空密钥)" << std::endl;
    }
}

void SystemTray::updateTenantMenu()
{
    if (!m_tenantMenu) {
        return;
    }

    m_tenantMenu->clear();
    m_tenantActionIds.clear();

    if (!hasProCredentials()) {
        QAction *placeholder = m_tenantMenu->addAction("请先登录 EasyTier Pro");
        placeholder->setEnabled(false);
        m_tenantMenu->setEnabled(false);
        return;
    }

    QList<CasdoorLogin::ProTenantInfo> tenants = m_casdoorLogin->proTenants();
    if (tenants.isEmpty()) {
        QAction *refreshAction = m_tenantMenu->addAction("重新登录以刷新组织列表");
        connect(refreshAction, &QAction::triggered, this, &SystemTray::onLoginEasyTierPro);
        m_tenantMenu->setEnabled(true);
        return;
    }

    const QString currentTenantName = m_configManager->getTenantDisplayName();
    for (const CasdoorLogin::ProTenantInfo &tenant : tenants) {
        QAction *action = m_tenantMenu->addAction(tenant.name);
        action->setCheckable(true);
        action->setChecked(tenant.name == currentTenantName);
        m_tenantActionIds.insert(action, tenant.id);
        connect(action, &QAction::triggered, this, &SystemTray::onSwitchTenant);
    }

    m_tenantMenu->setEnabled(true);
}

bool SystemTray::hasProCredentials() const
{
    if (m_connectionKey.isEmpty() || !m_configManager) {
        return false;
    }

    QRegularExpression proRe("tcp://et-web\\.console\\.easytier\\.net:22020/(.+)");
    QRegularExpressionMatch proMatch = proRe.match(m_connectionKey);
    if (!proMatch.hasMatch()) {
        return false;
    }

    QString currentDeviceKey = proMatch.captured(1).trimmed();
    return !currentDeviceKey.isEmpty()
           && currentDeviceKey == m_configManager->getOAuthDeviceKey()
           && !m_configManager->getUserId().isEmpty();
}

void SystemTray::handleLoginSuccess(const QString &deviceKey,
                                    const QString &displayName,
                                    const QString &userId,
                                    const QString &userDisplayName,
                                    const QString &tenantName)
{
    Q_UNUSED(displayName);

    showProgressDialog("正在切换 EasyTier Pro 账号...");
    updateStatus(ConnectionState::Connecting);

    if (ETRunService::isServiceInstalled() || ETRunService::isRunning()) {
        CommandResult removeResult;
        if (!ETRunService::removeService(&removeResult)) {
            rememberLastError("切换账号前移除旧服务失败", removeResult);
            closeProgressDialog();
            updateStatus(ETRunService::isRunning() ? ConnectionState::Connected : ConnectionState::NotStarted);
            QMessageBox::warning(nullptr, "切换账号失败", m_lastError);
            return;
        }
        QThread::msleep(500);
    }

    m_connectionKey = QString("tcp://et-web.console.easytier.net:22020/%1").arg(deviceKey);
    m_configManager->setConnectionKey(m_connectionKey);
    m_configManager->setUserId(userId);
    m_configManager->setUserDisplayName(userDisplayName);
    m_configManager->setOAuthDeviceKey(deviceKey);
    m_configManager->setTenantDisplayName(tenantName);
    m_configManager->saveConfig();
    updateUserStatus();

    closeProgressDialog();
    startConnection(true);
}

void SystemTray::maybeAutoConnectOnStartup()
{
    if (!m_isAutoStartMode || !m_autoStart || !hasProCredentials() || ETRunService::isRunning()) {
        return;
    }

    startConnection(false);
}

void SystemTray::clearProCredentials()
{
    m_connectionKey.clear();
    m_configManager->setConnectionKey(QString());
    m_configManager->setUserId(QString());
    m_configManager->setUserDisplayName(QString());
    m_configManager->setOAuthDeviceKey(QString());
    m_configManager->setTenantDisplayName(QString());
    m_configManager->saveConfig();
}
#endif

#ifdef IS_COMMUNITY_VER
void SystemTray::updateCommunityUserStatus()
{
    if (!m_connectionKey.isEmpty()) {
        m_statusAction->setText("状态：已设置");
    } else {
        m_statusAction->setText("状态：未设置");
    }
}
#endif

void SystemTray::updateStatus(ConnectionState state)
{
    m_connectionState = state;
    
    QString statusText;
    QString tooltipText;
    QString statusIcon;
    
    switch (state) {
    case ConnectionState::NotStarted:
        statusText =
#ifdef IS_COMMUNITY_VER
            "状态：未启动";
        tooltipText = QString("%1 - 未启动").arg(APP_DISPLAY_NAME);
#else
            hasProCredentials() ? "状态：未连接" : "状态：未登录";
        tooltipText = QString("%1 - %2").arg(APP_DISPLAY_NAME, hasProCredentials() ? "未连接" : "未登录");
#endif
        statusIcon = ":/assets/status-red.svg";
        break;
    case ConnectionState::Connecting:
        statusText = "状态：连接中";
        tooltipText = QString("%1 - 连接中").arg(APP_DISPLAY_NAME);
        statusIcon = ":/assets/status-yellow.svg";
        break;
    case ConnectionState::Connected:
        statusText = "状态：已连接";
        tooltipText =
#ifdef IS_COMMUNITY_VER
            QString("%1 - 已连接").arg(APP_DISPLAY_NAME);
#else
            QString("%1 - 已连接\n组织：%2\n用户：%3")
                .arg(APP_DISPLAY_NAME,
                     m_configManager->getTenantDisplayName().isEmpty() ? "-" : m_configManager->getTenantDisplayName(),
                     m_configManager->getUserDisplayName().isEmpty() ? "-" : m_configManager->getUserDisplayName());
#endif
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
        m_toggleConnectionAction->setText(
#ifdef IS_COMMUNITY_VER
            "启动连接"
#else
            "连接 EasyTier Pro"
#endif
        );
        m_toggleConnectionAction->setIcon(QIcon(":/assets/connect.svg"));
        m_toggleConnectionAction->setEnabled(true);
        break;
    case ConnectionState::Connecting:
        m_toggleConnectionAction->setText("处理中...");
        m_toggleConnectionAction->setIcon(QIcon(":/assets/disconnect.svg"));
        m_toggleConnectionAction->setEnabled(false);
        break;
    case ConnectionState::Connected:
        m_toggleConnectionAction->setText(
#ifdef IS_COMMUNITY_VER
            "停止连接"
#else
            "暂停连接"
#endif
        );
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
#ifndef IS_COMMUNITY_VER
    if (!hasProCredentials()) {
        QMessageBox::warning(nullptr, "未登录", "请先登录 EasyTier Pro。");
        return false;
    }
#else
    if (m_connectionKey.isEmpty()) {
        QMessageBox::warning(nullptr, "提示", "请先设置连接地址与密钥");
        return false;
    }
#endif

    showProgressDialog("正在连接 EasyTier Pro...");
    updateStatus(ConnectionState::Connecting);

    CommandResult result;
    bool success = ETRunService::startOrInstall(m_connectionKey, &result);
    closeProgressDialog();

    if (success) {
        updateStatus(ConnectionState::Connected);
        m_lastError.clear();
        if (showNotification) {
            m_trayIcon->showMessage(APP_DISPLAY_NAME, "已连接 EasyTier Pro",
                                    QSystemTrayIcon::Information, 3000);
        }
        return true;
    }

    rememberLastError("连接 EasyTier Pro 失败", result);
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

#ifndef IS_COMMUNITY_VER
void SystemTray::onOpenDevices()
{
    if (m_devicesDialog.isNull()) {
        m_devicesDialog = new DevicesDialog(m_configManager);
    }

    m_devicesDialog->show();
    m_devicesDialog->raise();
    m_devicesDialog->activateWindow();
}

void SystemTray::onLoginEasyTierPro()
{
    if (hasProCredentials()) {
        QMessageBox::StandardButton reply = QMessageBox::question(nullptr,
            "切换账号",
            "切换账号会停止并卸载当前 EasyTier Core 服务，然后使用新账号重新连接。",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    // 启动 OAuth 登录流程
    m_casdoorLogin->startLogin();
}

void SystemTray::onSwitchTenant()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action || !m_tenantActionIds.contains(action)) {
        return;
    }

    const QString tenantName = action->text();
    if (tenantName == m_configManager->getTenantDisplayName()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(nullptr,
        "切换组织",
        QString("切换到“%1”会重建本机设备凭据并自动重新连接。").arg(tenantName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (reply != QMessageBox::Yes) {
        return;
    }

    showProgressDialog("正在切换组织...");
    if (ETRunService::isServiceInstalled() || ETRunService::isRunning()) {
        CommandResult removeResult;
        if (!ETRunService::removeService(&removeResult)) {
            rememberLastError("切换组织前移除旧服务失败", removeResult);
            closeProgressDialog();
            updateStatus(ETRunService::isRunning() ? ConnectionState::Connected : ConnectionState::NotStarted);
            QMessageBox::warning(nullptr, "切换组织失败", m_lastError);
            return;
        }
        QThread::msleep(500);
    }

    m_casdoorLogin->switchProTenant(m_tenantActionIds.value(action));
}

void SystemTray::onLogout()
{
    if (!hasProCredentials()) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(nullptr,
        "退出登录",
        "退出登录会暂停连接、卸载 EasyTier Core 服务，并清空本机账号与设备凭据。",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    showProgressDialog("正在退出登录...");
    CommandResult result;
    bool removed = true;
    if (ETRunService::isServiceInstalled() || ETRunService::isRunning()) {
        removed = ETRunService::removeService(&result);
    }

    clearProCredentials();
    closeProgressDialog();
    updateStatus(ConnectionState::NotStarted);
    updateUserStatus();

    if (removed) {
        m_lastError.clear();
        m_trayIcon->showMessage(APP_DISPLAY_NAME, "已退出登录并移除本机服务",
                                QSystemTrayIcon::Information, 3000);
    } else {
        rememberLastError("退出登录时移除服务失败", result);
        QMessageBox::warning(nullptr, "退出登录", "账号状态已清空，但本机服务可能仍存在。\n" + m_lastError);
    }
}

void SystemTray::onDiagnostics()
{
    if (m_diagnosticsDialog.isNull()) {
        m_diagnosticsDialog = new DiagnosticsDialog(m_lastError);
    } else {
        m_diagnosticsDialog->deleteLater();
        m_diagnosticsDialog = new DiagnosticsDialog(m_lastError);
    }

    m_diagnosticsDialog->show();
    m_diagnosticsDialog->raise();
    m_diagnosticsDialog->activateWindow();
}
#endif

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
#ifndef IS_COMMUNITY_VER
        updateUserStatus();
#else
        updateCommunityUserStatus();
#endif
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
#ifndef IS_COMMUNITY_VER
    m_configManager->setOAuthDeviceKey(QString());
    m_configManager->setTenantDisplayName(QString());
#endif
    m_configManager->saveConfig();
#ifndef IS_COMMUNITY_VER
    updateUserStatus();
#else
    updateCommunityUserStatus();
#endif

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
#ifndef IS_COMMUNITY_VER
    // 双击打开本地设备窗口（仅专业版）
    if (reason == QSystemTrayIcon::DoubleClick) {
        onOpenDevices();
    }
#else
    Q_UNUSED(reason);
#endif
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
    
    // 更新用户登录状态
#ifndef IS_COMMUNITY_VER
    updateUserStatus();
#else
    updateCommunityUserStatus();
#endif
    
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
