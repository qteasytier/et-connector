/**
 * @file SystemTray.h
 * @brief 系统托盘主类 - 管理 UI 状态、菜单和信号槽协调
 * 
 * 核心职责：
 * - 系统托盘图标和右键菜单管理
 * - EasyTier 服务生命周期控制
 * - 配置持久化
 * - 开机自启管理
 */

#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QProgressDialog>
#include <QPointer>
#include <QTimer>
#include <QSettings>
#include "SettingsDialog.h"
#include "AboutDialog.h"
#include "QuitConfirmDialog.h"
#include "ETRunService.h"
#include "ConfigManager.h"

/**
 * @brief 连接状态枚举
 */
enum class ConnectionState {
    NotStarted,     ///< 未启动
    Connecting,     ///< 连接中
    Connected       ///< 已连接
};

/**
 * @brief 系统托盘主类
 * 
 * 负责整体协调，管理 UI 状态和信号槽连接。
 * 使用 QPointer 管理对话框生命周期，避免悬空指针问题。
 */
class SystemTray : public QObject
{
    Q_OBJECT

public:
    explicit SystemTray(QObject *parent = nullptr);
    ~SystemTray() override;

    void show() const;
    void setAutoStartMode(bool autoStart);

private slots:
    void onToggleConnection();
    void onSettings();
    void onAutoStart(bool checked);
    void onAbout();
    void onQuit();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    
    void onClearConnectionInfo();
    void onConnectionKeyChanged();
    void onHeartbeat();

private:
    void setupMenu();
    void updateStatus(ConnectionState state);
    void updateConnectionActions() const;
    void updateUserStatus();
    bool startConnection(bool showNotification);
    bool pauseConnection(bool showNotification);
    void rememberLastError(const QString &context, const CommandResult &result);
    void loadSettings();
    void saveSettings();
    
    void showProgressDialog(const QString &text);
    void closeProgressDialog();
    
    // 注册表自启管理（Windows 开机自启，无需管理员权限）
    static bool registerAutoStart();
    static bool unregisterAutoStart();
    static bool isAutoStartRegistered();

    // === 成员变量（按初始化顺序排列）===
    
    // 核心组件
    ConfigManager *m_configManager = nullptr;       ///< 配置管理器
    
    // 托盘 UI
    QSystemTrayIcon *m_trayIcon = nullptr;          ///< 系统托盘图标
    QMenu *m_menu = nullptr;                        ///< 右键菜单
    QProgressDialog *m_progressDialog = nullptr;    ///< 进度对话框
    
    // 菜单项
    QAction *m_titleAction = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_separator1 = nullptr;
    QAction *m_toggleConnectionAction = nullptr;
    QAction *m_separator2 = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_clearConnectionAction = nullptr;
    QAction *m_separator3 = nullptr;
    QAction *m_autoStartAction = nullptr;
    QAction *m_separator4 = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_quitAction = nullptr;
    
    // 对话框（使用 QPointer 自动管理，避免悬空指针）
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<AboutDialog> m_aboutDialog;
    QPointer<QuitConfirmDialog> m_quitConfirmDialog;
    
    // 状态变量
    ConnectionState m_connectionState = ConnectionState::NotStarted;
    bool m_autoStart = false;           ///< 是否开机启动托盘程序
    bool m_isAutoStartMode = false;     ///< 是否从开机自启启动
    QString m_connectionKey;            ///< 连接密钥
    QString m_lastError;                ///< 最近一次服务/登录错误
    
    // 心跳定时器
    QTimer *m_heartbeatTimer = nullptr; ///< 每2秒检测 easytier-core 进程状态
};

#endif // SYSTEMTRAY_H
