/**
 * @file SystemTray.h
 * @brief 系统托盘主类 - 管理 UI 状态、菜单和信号槽协调
 *
 * 核心职责（重构后）：
 * - 系统托盘图标和右键菜单的创建与管理
 * - 通过 ConnectionController 委托所有连接/断开逻辑
 * - 配置持久化的读写（通过 ConfigManager）
 * - 开机自启管理（注册表/LaunchAgents/.desktop）
 *
 * 不再承担的职责（已移交给 ConnectionController）：
 * - 连接状态机管理
 * - IPC 通信协调
 * - 超时处理与自动重连
 * - 进度对话框管理
 */

#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include "SettingsDialog.h"
#include "AboutDialog.h"
#include "ConfigManager.h"
#include "ConnectionController.h"

/**
 * @brief 系统托盘主类
 *
 * 作为整个应用的 UI 协调层，连接 ConnectionController 的信号到菜单更新。
 * 使用 QPointer 管理对话框生命周期，避免悬空指针导致的崩溃。
 */
class SystemTray : public QObject
{
    Q_OBJECT

public:
    explicit SystemTray(QObject *parent = nullptr);
    ~SystemTray() override;

    /** 显示托盘图标，延迟 100ms 后再弹出启动通知 */
    void show() const;

    /** 设置是否以开机自启模式启动（抑制启动通知） */
    void setAutoStartMode(bool autoStart);

private slots:
    // ========================================================================
    // 用户操作响应
    // ========================================================================

    /** 点击"启动连接"/"停止连接"菜单项 */
    void onToggleConnection();

    /** 点击"设置连接地址与密钥" → 弹出设置对话框 */
    void onSettings();

    /** 点击"开机启动托盘程序"复选框切换 */
    void onAutoStart(bool checked);

    /** 点击"关于软件" */
    void onAbout();

    /** 点击"退出客户端"，根据运行状态决定是否弹确认框 */
    void onQuit();

    /** 托盘图标被双击/单击（当前不做处理） */
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

    /** 点击"清空连接信息" → 确认后清空密钥并停止连接 */
    void onClearConnectionInfo();

private:
    // ========================================================================
    // UI 构建
    // ========================================================================

    /** 创建系统托盘右键菜单的所有项 */
    void setupMenu();

    /** 根据两级状态更新所有 UI 元素 */
    void updateStatus(DaemonState daemon, ServiceState service);

    /** 根据 Service 状态更新"启动/停止连接"按钮的文字和启用状态 */
    void updateConnectionActions(ServiceState service) const;

    /** 根据密钥是否为空更新"已设置/未设置"状态文字 */
    void updateUserStatus();

    // ========================================================================
    // ConnectionController 信号连接
    // ========================================================================

    /**
     * @brief 初始化 ConnectionController 的信号连接
     *
     * 在此集中连接所有需要 UI 更新的信号：
     * - daemonStateChanged  → 更新"守护进程"行
     * - serviceStateChanged → 更新"状态"行和按钮
     * - connected           → 弹出托盘通知
     * - connectionFailed    → 弹出错误对话框
     * - quitRequested       → 保存配置并退出
     */
    void initController();

    // ========================================================================
    // 配置读写
    // ========================================================================

    /** 从 ConfigManager 加载配置到本地变量 */
    void loadSettings();

    /** 将开机自启设置同步到 ConfigManager 并保存 */
    void saveSettings();

    // ========================================================================
    // 开机自启管理（平台特定实现）
    // ========================================================================

    /**
     * @brief 注册开机自启
     * Windows → HKCU\...\Run 注册表
     * macOS   → ~/Library/LaunchAgents 的 launchd plist
     * Linux   → ~/.config/autostart 的 .desktop 文件
     */
    static bool registerAutoStart();

    /** 取消开机自启注册 */
    static bool unregisterAutoStart();

    /** 查询当前是否已注册开机自启 */
    static bool isAutoStartRegistered();

    // ========================================================================
    // 成员变量（按初始化顺序排列）
    // ========================================================================

    ConfigManager *m_configManager = nullptr;           ///< 配置管理器（拥有所有权）
    ConnectionController *m_connectionController = nullptr; ///< 连接控制器（拥有所有权）

    // 托盘 UI
    QSystemTrayIcon *m_trayIcon = nullptr;              ///< 系统托盘图标
    QMenu *m_menu = nullptr;                            ///< 右键弹出菜单

    // 菜单项 - 每个 action 对应一个菜单项，通过 separator 分组
    QAction *m_titleAction = nullptr;                   ///< 标题栏（应用名称）
    QAction *m_statusAction = nullptr;                  ///< 连接状态行
    QAction *m_backendStatusAction = nullptr;           ///< 后端守护进程连接状态行
    QAction *m_separator1 = nullptr;
    QAction *m_toggleConnectionAction = nullptr;        ///< "启动连接"/"停止连接"
    QAction *m_separator2 = nullptr;
    QAction *m_settingsAction = nullptr;                ///< "设置连接地址与密钥"
    QAction *m_clearConnectionAction = nullptr;         ///< "清空连接信息"
    QAction *m_separator3 = nullptr;
    QAction *m_autoStartAction = nullptr;               ///< "开机启动托盘程序"（复选框）
    QAction *m_separator4 = nullptr;
    QAction *m_separator5 = nullptr;
    QAction *m_aboutAction = nullptr;                   ///< "关于软件"
    QAction *m_quitAction = nullptr;                    ///< "退出客户端"

    // 对话框 - QPointer 自动跟踪生命周期，对象被删除后自动置 null
    QPointer<SettingsDialog> m_settingsDialog;
    QPointer<AboutDialog> m_aboutDialog;

    // 状态
    bool m_autoStart = false;           ///< 是否开机启动托盘程序
    bool m_isAutoStartMode = false;     ///< 是否从开机自启启动（影响启动通知）
};

#endif // SYSTEMTRAY_H
