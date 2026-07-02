/**
 * @file ConnectionController.h
 * @brief 连接控制器 - 封装配置服务器连接/断开/重连的全部逻辑
 *
 * 将原 SystemTray 中的状态机、IPC 协调、配置同步、进度对话框、
 * 超时处理和重连逻辑抽离为独立类，SystemTray 仅保留 UI 协调职责。
 *
 * 设计要点：
 * - 后端 IPC 连接状态（DaemonState）和配置服务器连接状态（ServiceState）完全独立
 * - 各自通过独立信号通知 UI 层，互不干扰
 * - 后端断线时自动将 Service 状态重置为 Idle（原子操作）
 */

#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QProgressDialog>
#include <QPointer>

class IpcClient;
class ConfigManager;

// ============================================================================
// 后端守护进程 IPC 连接状态（2 值）
// ============================================================================
enum class DaemonState {
    Disconnected,   ///< 与后端守护进程断开连接
    Connected       ///< 已连接到后端守护进程
};

// ============================================================================
// 配置服务器连接状态（4 值）
// ============================================================================
enum class ServiceState {
    Idle,           ///< 配置服务器未启动，可发起连接
    Connecting,     ///< 正在连接配置服务器，等待心跳确认（最多 30s）
    Connected,      ///< 配置服务器已成功连接，业务正常运行
    Stopping        ///< 正在停止配置服务器，等待后端响应（最多 10s）
};

// ============================================================================
// 排队动作枚举 - 某些操作需要等当前操作完成后再执行
// ============================================================================
enum class PendingAction {
    None,               ///< 无排队动作
    RestartAfterStop,   ///< 停止完成后立即重启（用户修改密钥时触发）
    QuitAfterStop       ///< 停止完成后退出应用（用户退出时触发）
};

/**
 * @brief 连接控制器
 *
 * 独立管理两级状态：
 * - DaemonState: 后端 IPC 连接状态（断/连）
 * - ServiceState: 配置服务器运行状态（空闲/连接中/已连接/停止中）
 *
 * 两级信号独立发射，SystemTray 据此更新不同的 UI 行，互不混淆。
 */
class ConnectionController : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionController(ConfigManager *configManager,
                                  QObject *parent = nullptr);
    ~ConnectionController() override;

    // ========================================================================
    // 公共接口 - 供 SystemTray 调用
    // ========================================================================

    void startConnection();
    void stopConnection();
    void requestQuit();
    void restartAfterKeyChange();

    /** 查询后端 IPC 是否已连接 */
    DaemonState daemonState() const { return m_daemonState; }

    /** 查询配置服务器是否在运行（Connecting/Connected/Stopping） */
    bool isRunning() const;

    /** 获取最近一次错误消息，成功时为空 */
    QString lastError() const { return m_lastError; }

signals:
    // ========================================================================
    // 信号 - 分别通知后端状态和服务状态，UI 层据此独立更新
    // ========================================================================

    /** 后端守护进程连接状态变化 → 更新"守护进程"行 */
    void daemonStateChanged(DaemonState state);

    /** 配置服务器连接状态变化 → 更新"状态"行和"启动/停止"按钮 */
    void serviceStateChanged(ServiceState state);

    /** 连接成功，用于弹出托盘通知 */
    void connected();

    /** 连接失败，携带错误消息供弹窗显示 */
    void connectionFailed(const QString &error);

    /** 退出前的清理工作已完成，可以安全退出 */
    void quitRequested();

private slots:
    // ========================================================================
    // IPC 事件处理
    // ========================================================================

    void onIpcConnected();
    void onIpcDisconnected();
    void onIpcError(const QString &message);
    void onStartFinished(bool success, const QString &error);
    void onStopFinished(bool success, const QString &error);
    void onStateQueried(bool isConnected);
    void onEventsReceived(const QStringList &events);

    // ========================================================================
    // 定时器事件
    // ========================================================================

    void onHeartbeat();
    void onStartTimeout();
    void onStopTimeout();

private:
    // ========================================================================
    // 两级状态管理 helper
    // ========================================================================

    /** 后端连接成功 → 切 Service 为 Idle，立即查询当前状态 */
    void setDaemonConnected();

    /**
     * @brief 后端断开连接
     *
     * 强制重置所有 Service 相关状态：杀掉超时定时器、关闭进度对话框、
     * 重置重连计数、切 Service 为 Idle。
     * 这是原子操作——不会出现"后端已断但 Service 还显示 Connected"。
     */
    void setDaemonDisconnected();

    /** 设置配置服务器状态，emit serviceStateChanged */
    void setServiceState(ServiceState state);

    // ========================================================================
    // 连接逻辑
    // ========================================================================

    void doStartConnection();
    void doStopConnection();
    void processQueuedActions();
    void tryReconnect();

    void showProgress(const QString &text);
    void closeProgress();

    // ========================================================================
    // 成员变量
    // ========================================================================

    IpcClient *m_ipcClient;
    ConfigManager *m_configManager;

    DaemonState m_daemonState = DaemonState::Disconnected;
    ServiceState m_serviceState = ServiceState::Idle;

    QString m_lastError;
    PendingAction m_pendingAction = PendingAction::None;

    int m_reconnectCount = 0;
    static constexpr int MAX_RECONNECT = 3;
    static constexpr int HEARTBEAT_INTERVAL_MS = 2000;
    static constexpr int HEARTBEAT_INTERVAL_FAST_MS = 1000;
    static constexpr int START_TIMEOUT_MS = 30000;
    static constexpr int STOP_TIMEOUT_MS = 10000;
    static constexpr int RESTART_DELAY_MS = 500;

    QTimer *m_heartbeatTimer;
    QTimer *m_startTimeout;
    QTimer *m_stopTimeout;

    QPointer<QProgressDialog> m_progressDialog;
};

#endif // CONNECTIONCONTROLLER_H
