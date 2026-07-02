/**
 * @file IpcClient.cpp
 * @brief IPC 客户端实现
 *
 * 通信流程：
 * 1. connectToDaemon() → QLocalSocket::connectToServer()
 * 2. onStateChanged → ConnectedState → emit socketConnected()
 * 3. sendRequest() → 构造 JSON → encodeFrame() → socket->write()
 * 4. onReadyRead() → 从缓冲区提取帧 → processFrame() → emit 业务信号
 *
 * 粘包/半包处理：
 *   m_readBuffer 持续累积数据，从开头读取 4 字节长度，校验完整性后提取帧。
 *   不完整的帧保留在缓冲区等待下次 readyRead。
 */

#include "IpcClient.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSysInfo>
#include <QTimer>
#include <iostream>

// 后端守护进程的本地套接字名称
// Linux/macOS: /tmp/qtet-connector-daemon.sock 或运行时目录
// Windows:     命名管道同名
static constexpr const char *DAEMON_SOCKET_NAME = "qtet-connector-daemon.sock";

// ============================================================================
// 构造 / 析构
// ============================================================================

IpcClient::IpcClient(QObject *parent)
    : QObject(parent)
{
    m_socket = new QLocalSocket(this);

    // 连接 socket 的三个核心信号
    connect(m_socket, &QLocalSocket::readyRead,
            this, &IpcClient::onReadyRead);
    // QOverload 用于选择正确的 errorOccurred 重载（Qt6 中该方法从 QAbstractSocket 继承）
    connect(m_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred),
            this, &IpcClient::onSocketError);
    connect(m_socket, &QLocalSocket::stateChanged,
            this, &IpcClient::onStateChanged);
}

IpcClient::~IpcClient()
{
    disconnectFromDaemon();
}

// ============================================================================
// 连接管理
// ============================================================================

bool IpcClient::isSocketConnected() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

bool IpcClient::isConnecting() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectingState;
}

void IpcClient::connectToDaemon()
{
    // 防止重复连接：已在连接或已连接时直接返回
    if (isSocketConnected() || isConnecting()) {
        return;
    }

    // 清空可能残留的接收缓冲区
    m_readBuffer.clear();
    m_socket->connectToServer(DAEMON_SOCKET_NAME);
}

void IpcClient::disconnectFromDaemon()
{
    if (m_socket) {
        m_socket->disconnectFromServer();
    }
}

// ============================================================================
// 业务接口 - 异步调用
// ============================================================================

void IpcClient::startConfigServerClient(const QString &url,
                                        const QString &hostname,
                                        const QString &machineId,
                                        bool secureMode)
{
    QJsonObject params;
    // 后端的协议要求这三个字符串参数做 Base64 编码
    params["config_server_url"] = base64Encode(url);
    params["hostname"] = base64Encode(hostname);
    params["machine_id"] = base64Encode(machineId);
    params["secure_mode"] = secureMode;

    sendRequest("start_config_server_client", params);
}

void IpcClient::stopConfigServerClient()
{
    sendRequest("stop_config_server_client", {});
}

void IpcClient::isConfigServerClientConnected()
{
    sendRequest("is_config_server_client_connected", {});
}

void IpcClient::pollConfigEvents()
{
    sendRequest("poll_config_events", {});
}

// ============================================================================
// 帧收发
// ============================================================================

void IpcClient::sendRequest(const QString &method, const QJsonObject &params)
{
    // 未连接时无法发送，直接报错
    if (!isSocketConnected()) {
        emit ipcError("未连接到后端守护进程");
        return;
    }

    // 构造 JSON-RPC 请求
    QJsonObject root;
    root["id"] = m_nextRequestId++;          // 自增 ID，每个请求唯一
    root["method"] = method;
    root["params"] = params;

    // 序列化为紧凑 JSON（无缩进，节省带宽）
    QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    // 编码为帧格式并写入 socket
    m_socket->write(encodeFrame(payload));
}

QByteArray IpcClient::encodeFrame(const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(4 + payload.size());       // 预分配，避免多次扩容

    // 写入 4 字节大端序长度前缀
    quint32 len = static_cast<quint32>(payload.size());
    frame.append(static_cast<char>((len >> 24) & 0xFF));
    frame.append(static_cast<char>((len >> 16) & 0xFF));
    frame.append(static_cast<char>((len >> 8) & 0xFF));
    frame.append(static_cast<char>(len & 0xFF));
    frame.append(payload);

    return frame;
}

void IpcClient::onReadyRead()
{
    // 将新数据追加到缓冲区（可能有粘包：多个帧在一次读取中到达）
    m_readBuffer.append(m_socket->readAll());

    // 循环提取完整帧，直到缓冲区不足一个帧
    while (true) {
        // 至少需要 4 字节来读取长度前缀
        if (m_readBuffer.size() < 4) {
            break;
        }

        // 解析 4 字节大端序长度
        quint32 len = (static_cast<quint8>(m_readBuffer[0]) << 24)
                  | (static_cast<quint8>(m_readBuffer[1]) << 16)
                  | (static_cast<quint8>(m_readBuffer[2]) << 8)
                  | static_cast<quint8>(m_readBuffer[3]);

        // 检查完整的帧是否已到达（长度前缀 + 负载）
        if (static_cast<quint32>(m_readBuffer.size()) < 4 + len) {
            break;  // 半包：等下次 readyRead
        }

        // 提取负载并处理
        QByteArray payload = m_readBuffer.mid(4, static_cast<int>(len));
        // 从缓冲区移除已处理的帧（包括 4 字节前缀）
        m_readBuffer.remove(0, static_cast<int>(4 + len));
        processFrame(payload);
    }
}

void IpcClient::processFrame(const QByteArray &payload)
{
    // 解析 JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit ipcError("收到无效的 IPC 响应");
        return;
    }

    QJsonObject obj = doc.object();
    QString method = obj.value("method").toString();

    // ---- 错误响应处理 ----
    if (obj.contains("error")) {
        QString msg = obj.value("error").toObject().value("message").toString();
        // start/stop 的错误各自分发，其他方法的错误作为通用 IPC 错误
        if (method == "start_config_server_client") {
            emit startConfigServerClientFinished(false, msg);
        } else if (method == "stop_config_server_client") {
            emit stopConfigServerClientFinished(false, msg);
        } else {
            emit ipcError(QString("%1 失败：%2").arg(method, msg));
        }
        return;
    }

    // ---- 成功响应处理 ----
    QJsonObject result = obj.value("result").toObject();

    // 根据 method 字段分发到对应的业务信号
    if (method == "start_config_server_client") {
        emit startConfigServerClientFinished(true, QString());
    } else if (method == "stop_config_server_client") {
        emit stopConfigServerClientFinished(true, QString());
    } else if (method == "is_config_server_client_connected") {
        emit isConfigServerClientConnectedFinished(result.value("connected").toBool());
    } else if (method == "poll_config_events") {
        // 事件以字符串数组形式返回，每项为 JSON 格式的事件数据
        QStringList events;
        QJsonArray arr = result.value("events").toArray();
        for (const auto &value : arr) {
            events.append(value.toString());
        }
        emit pollConfigEventsFinished(events);
    }
}

// ============================================================================
// Socket 事件回调
// ============================================================================

void IpcClient::onSocketError(QLocalSocket::LocalSocketError socketError)
{
    Q_UNUSED(socketError);
    // 将底层 socket 错误字符串转发给上层
    if (m_socket) {
        emit ipcError(m_socket->errorString());
    }
}

void IpcClient::onStateChanged(QLocalSocket::LocalSocketState state)
{
    if (state == QLocalSocket::ConnectedState) {
        emit socketConnected();
    } else if (state == QLocalSocket::UnconnectedState) {
        // 注意：UnconnectedState 也会在初始和连接失败时触发
        // 上层（ConnectionController）需要有状态机来过滤这些情况
        emit socketDisconnected();
    }
}

// ============================================================================
// 工具函数
// ============================================================================

QString IpcClient::base64Encode(const QString &input)
{
    // UTF-8 → Base64（Qt 内置，无需第三方库）
    return QString::fromUtf8(input.toUtf8().toBase64());
}
