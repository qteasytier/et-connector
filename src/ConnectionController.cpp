/**
 * @file ConnectionController.cpp
 * @brief 连接控制器实现
 *
 * 两级独立状态机：
 * 1. DaemonState：IPC 连接/断开 → 驱动心跳重连
 * 2. ServiceState：配置服务器生命周期 → 驱动启动/停止/超时/重连
 *
 * 关键保证：后端断线时 setDaemonDisconnected() 原子性地重置所有 Service 状态。
 */

#include "ConnectionController.h"
#include "IpcClient.h"
#include "ConfigManager.h"
#include <QSysInfo>
#include <iostream>

// ============================================================================
// 构造 / 析构
// ============================================================================

ConnectionController::ConnectionController(ConfigManager *configManager, QObject *parent)
    : QObject(parent)
    , m_configManager(configManager)
{
    m_ipcClient = new IpcClient(this);
    connect(m_ipcClient, &IpcClient::socketConnected,
            this, &ConnectionController::onIpcConnected);
    connect(m_ipcClient, &IpcClient::socketDisconnected,
            this, &ConnectionController::onIpcDisconnected);
    connect(m_ipcClient, &IpcClient::ipcError,
            this, &ConnectionController::onIpcError);
    connect(m_ipcClient, &IpcClient::startConfigServerClientFinished,
            this, &ConnectionController::onStartFinished);
    connect(m_ipcClient, &IpcClient::stopConfigServerClientFinished,
            this, &ConnectionController::onStopFinished);
    connect(m_ipcClient, &IpcClient::isConfigServerClientConnectedFinished,
            this, &ConnectionController::onStateQueried);
    connect(m_ipcClient, &IpcClient::pollConfigEventsFinished,
            this, &ConnectionController::onEventsReceived);

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ConnectionController::onHeartbeat);
    m_heartbeatTimer->start(HEARTBEAT_INTERVAL_MS);

    m_startTimeout = new QTimer(this);
    m_startTimeout->setSingleShot(true);
    connect(m_startTimeout, &QTimer::timeout, this, &ConnectionController::onStartTimeout);

    m_stopTimeout = new QTimer(this);
    m_stopTimeout->setSingleShot(true);
    connect(m_stopTimeout, &QTimer::timeout, this, &ConnectionController::onStopTimeout);

    m_ipcClient->connectToDaemon();

    std::clog << "ConnectionController: 初始化完成" << std::endl;
}

ConnectionController::~ConnectionController()
{
    m_heartbeatTimer->stop();
    m_startTimeout->stop();
    m_stopTimeout->stop();
    closeProgress();
    m_ipcClient->disconnectFromDaemon();
    std::clog << "ConnectionController: 析构完成" << std::endl;
}

// ============================================================================
// 两级状态管理 helper
// ============================================================================

void ConnectionController::setDaemonConnected()
{
    if (m_daemonState == DaemonState::Connected) return;
    m_daemonState = DaemonState::Connected;
    emit daemonStateChanged(m_daemonState);

}

void ConnectionController::setDaemonDisconnected()
{
    if (m_daemonState == DaemonState::Disconnected) return;
    m_daemonState = DaemonState::Disconnected;
    emit daemonStateChanged(m_daemonState);

    // 后端断开 → 所有进行中的操作都被强制终止
    m_startTimeout->stop();
    m_stopTimeout->stop();
    closeProgress();
    m_reconnectCount = 0;

    // Service 强制回到 Unknown
    setServiceState(ServiceState::Unknown);
}

void ConnectionController::setServiceState(ServiceState state)
{
    if (m_serviceState == state) return;

    const ServiceState oldState = m_serviceState;
    m_serviceState = state;

    std::clog << "ConnectionController: Service 状态转换 "
              << static_cast<int>(oldState) << " -> "
              << static_cast<int>(state) << std::endl;

    switch (state) {
    case ServiceState::Idle:
        m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
        m_startTimeout->stop();
        m_stopTimeout->stop();
        m_reconnectCount = 0;
        closeProgress();
        break;
    case ServiceState::Connecting:
        m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_FAST_MS);
        // startTimeout 由 doStartConnection 启动
        break;
    case ServiceState::Connected:
        m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
        m_startTimeout->stop();
        m_stopTimeout->stop();
        m_reconnectCount = 0;
        closeProgress();
        break;
    case ServiceState::Stopping:
        m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
        // stopTimeout 由 doStopConnection 启动
        break;
    case ServiceState::Unknown:
        break;
    }

    emit serviceStateChanged(state);
}

// ============================================================================
// 公共接口：启动连接
// ============================================================================

void ConnectionController::startConnection()
{
    const QString key = m_configManager->getConnectionKey();
    if (key.isEmpty()) {
        m_lastError = "请先设置连接地址与密钥";
        emit connectionFailed(m_lastError);
        return;
    }

    if (!m_ipcClient->isSocketConnected()) {
        m_lastError = "未连接到后端守护进程，请确保 qtet-daemon 正在运行";
        emit connectionFailed(m_lastError);
        return;
    }

    m_pendingAction = PendingAction::None;
    doStartConnection();
}

void ConnectionController::doStartConnection()
{
    const QString key = m_configManager->getConnectionKey();
    if (key.isEmpty() || !m_ipcClient->isSocketConnected()) {
        return;
    }

    showProgress("正在连接配置服务器...");
    setServiceState(ServiceState::Connecting);

    const QString hostname = QSysInfo::machineHostName();
    m_ipcClient->startConfigServerClient(key, hostname, hostname,
                                         m_configManager->getSecureMode());
    m_startTimeout->start(START_TIMEOUT_MS);
}

// ============================================================================
// 公共接口：停止连接
// ============================================================================

void ConnectionController::stopConnection()
{
    if (m_serviceState != ServiceState::Idle && m_serviceState != ServiceState::Unknown) {
        doStopConnection();
    } else {
        processQueuedActions();
    }
}

void ConnectionController::doStopConnection()
{
    if (!m_ipcClient->isSocketConnected()) {
        setServiceState(ServiceState::Idle);
        processQueuedActions();
        return;
    }

    showProgress("正在断开配置服务器...");
    setServiceState(ServiceState::Stopping);

    m_stopTimeout->start(STOP_TIMEOUT_MS);
    m_ipcClient->stopConfigServerClient();
}

// ============================================================================
// 公共接口：退出 / 密钥变更重启
// ============================================================================

void ConnectionController::requestQuit()
{
    if (m_serviceState != ServiceState::Idle && m_serviceState != ServiceState::Unknown) {
        m_pendingAction = PendingAction::QuitAfterStop;
        stopConnection();
    } else {
        emit quitRequested();
    }
}

void ConnectionController::restartAfterKeyChange()
{
    const QString key = m_configManager->getConnectionKey();
    if (m_serviceState != ServiceState::Idle && m_serviceState != ServiceState::Unknown) {
        if (key.isEmpty()) {
            stopConnection();
        } else {
            m_pendingAction = PendingAction::RestartAfterStop;
            stopConnection();
        }
    }
}

// ============================================================================
// 自动重连
// ============================================================================

void ConnectionController::tryReconnect()
{
    if (m_reconnectCount >= MAX_RECONNECT) {
        m_lastError = "连接丢失，自动重连次数已达上限";
        setServiceState(ServiceState::Idle);
        emit connectionFailed(m_lastError);
        return;
    }

    m_reconnectCount++;
    std::clog << "ConnectionController: 自动重连 " << m_reconnectCount
              << "/" << MAX_RECONNECT << std::endl;

    doStartConnection();
}

// ============================================================================
// 排队动作处理
// ============================================================================

void ConnectionController::processQueuedActions()
{
    if (m_pendingAction == PendingAction::RestartAfterStop) {
        m_pendingAction = PendingAction::None;
        QTimer::singleShot(RESTART_DELAY_MS, this, [this]() {
            doStartConnection();
        });
        return;
    }

    if (m_pendingAction == PendingAction::QuitAfterStop) {
        m_pendingAction = PendingAction::None;
        emit quitRequested();
        return;
    }
}

// ============================================================================
// IPC 事件处理
// ============================================================================

void ConnectionController::onIpcConnected()
{
    std::clog << "ConnectionController: 已连接到后端守护进程" << std::endl;
    setDaemonConnected();

    m_ipcClient->isConfigServerClientConnected();
    m_ipcClient->pollConfigEvents();
}

void ConnectionController::onIpcDisconnected()
{
    std::clog << "ConnectionController: 与后端守护进程断开连接" << std::endl;
    setDaemonDisconnected();
}

void ConnectionController::onIpcError(const QString &message)
{
    std::cerr << "ConnectionController: IPC 错误 - "
              << message.toStdString() << std::endl;

    // 仅在 Connecting 状态时将 IPC 错误视为连接失败
    if (m_serviceState == ServiceState::Connecting && !message.isEmpty()) {
        m_startTimeout->stop();
        closeProgress();
        m_lastError = message;
        setServiceState(ServiceState::Idle);
        emit connectionFailed(message);
    }
}

void ConnectionController::onStartFinished(const bool success, const QString &error)
{
    if (success) {
        // IPC 返回成功只代表后端已受理请求，正在尝试连接配置服务器
        // 真正的连接成功由心跳轮询 onStateQueried 确认
        std::clog << "ConnectionController: 启动请求已受理，等待心跳确认连接..." << std::endl;
    } else {
        m_startTimeout->stop();
        closeProgress();
        m_lastError = error;
        setServiceState(ServiceState::Idle);
        emit connectionFailed(m_lastError.isEmpty() ? "未知错误" : m_lastError);
    }
}

void ConnectionController::onStopFinished(bool success, const QString &error)
{
    Q_UNUSED(success);
    m_stopTimeout->stop();
    closeProgress();

    if (error.isEmpty()) {
        m_lastError.clear();
    } else {
        m_lastError = error;
    }

    // 停止完成 → Service 回到 Idle
    // 若此时后端也断开了，setDaemonDisconnected 会幂等地 reset
    if (!m_ipcClient->isSocketConnected()) {
        setDaemonDisconnected();
    } else {
        setServiceState(ServiceState::Idle);
    }

    processQueuedActions();
}

void ConnectionController::onStateQueried(bool isConnected)
{
    if (m_serviceState == ServiceState::Unknown) {
        if (isConnected) {
            setServiceState(ServiceState::Connected);
        } else {
            setServiceState(ServiceState::Idle);
        }
        return;
    }

    // 后端报告已连接，但前端处于 Idle → 同步为 Connected（其他实例启动的）
    if (isConnected && m_serviceState == ServiceState::Idle) {
        std::clog << "ConnectionController: 心跳检测 - 配置服务器已连接" << std::endl;
        setServiceState(ServiceState::Connected);
    }
    // 正在 Connecting + 后端报告已连接 → 真正的连接建立成功
    else if (isConnected && m_serviceState == ServiceState::Connecting) {
        std::clog << "ConnectionController: 心跳检测 - 连接已建立" << std::endl;
        m_startTimeout->stop();
        closeProgress();
        m_lastError.clear();
        setServiceState(ServiceState::Connected);
        emit connected();
    }
    // 后端报告已断开，但前端认为 Connected → 触发自动重连
    else if (!isConnected && m_serviceState == ServiceState::Connected) {
        std::clog << "ConnectionController: 心跳检测 - 配置服务器已断开，尝试重连" << std::endl;
        tryReconnect();
    }
}

void ConnectionController::onEventsReceived(const QStringList &events)
{
    for (const QString &event : events) {
        std::clog << "ConfigEvent: " << event.toStdString() << std::endl;
    }
}

// ============================================================================
// 心跳
// ============================================================================

void ConnectionController::onHeartbeat()
{
    // Daemon 断线 → 持续尝试重连后端
    if (m_daemonState == DaemonState::Disconnected) {
        m_ipcClient->connectToDaemon();
        return;
    }

    // Daemon 已连接 → 根据 Service 状态执行不同策略
    switch (m_serviceState) {
    case ServiceState::Idle:
        // 空闲 → 轮询状态 + 收集事件
        m_ipcClient->isConfigServerClientConnected();
        m_ipcClient->pollConfigEvents();
        break;
    case ServiceState::Connecting:
        // 等待连接建立 → 只轮询状态
        m_ipcClient->isConfigServerClientConnected();
        break;
    case ServiceState::Connected:
        // 已连接 → 只查询状态（检测断线）
        m_ipcClient->isConfigServerClientConnected();
        break;
    case ServiceState::Stopping:
        // 等待停止完成 → 不操作，由超时兜底
        break;
    case ServiceState::Unknown:
        m_ipcClient->isConfigServerClientConnected();
        break;
    }
}

// ============================================================================
// 超时处理
// ============================================================================

void ConnectionController::onStartTimeout()
{
    std::clog << "ConnectionController: 连接超时，进入停止流程" << std::endl;
    closeProgress();

    if (m_ipcClient->isSocketConnected()) {
        m_ipcClient->stopConfigServerClient();
    }

    setServiceState(ServiceState::Stopping);
    m_stopTimeout->start(STOP_TIMEOUT_MS);
}

void ConnectionController::onStopTimeout()
{
    std::clog << "ConnectionController: 停止超时，强制切回 Idle" << std::endl;
    closeProgress();

    if (!m_ipcClient->isSocketConnected()) {
        setDaemonDisconnected();
    } else {
        setServiceState(ServiceState::Idle);
    }

    processQueuedActions();
}

// ============================================================================
// 进度对话框管理
// ============================================================================

void ConnectionController::showProgress(const QString &text)
{
    if (m_progressDialog.isNull()) {
        m_progressDialog = new QProgressDialog(text, QString(), 0, 0, nullptr);
        m_progressDialog->setWindowTitle("请稍候");
        m_progressDialog->setWindowModality(Qt::WindowModal);
        m_progressDialog->setCancelButton(nullptr);
        m_progressDialog->setMinimumDuration(0);
    }
    m_progressDialog->setLabelText(text);
    m_progressDialog->show();
}

void ConnectionController::closeProgress() const {
    if (!m_progressDialog.isNull()) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
    }
}
