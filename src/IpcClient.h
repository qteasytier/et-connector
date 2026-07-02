/**
 * @file IpcClient.h
 * @brief IPC 客户端 - 通过 QLocalSocket 与 qtet-daemon 后端守护进程通信
 *
 * 协议定义：
 *   帧格式：4 字节大端序长度前缀 + UTF-8 JSON 负载
 *   消息格式：JSON-RPC 风格
 *     请求：{ "id": N, "method": "xxx", "params": {...} }
 *     响应：{ "id": N, "method": "xxx", "result": {...} }  或
 *           { "id": N, "method": "xxx", "error": {"message": "..."} }
 *
 * 所有接口调用均为异步非阻塞，结果通过 Qt 信号返回。
 * 在 Linux/macOS 上使用 Unix Domain Socket，Windows 上使用命名管道。
 */

#ifndef IPCCLIENT_H
#define IPCCLIENT_H

#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>

/**
 * @brief IPC 客户端
 *
 * 负责与后端守护进程 qtet-daemon 建立本地套接字连接，
 * 并异步收发 JSON-RPC 消息。所有业务接口均是 fire-and-forget 风格，
 * 调用方通过连接对应的信号来获取异步结果。
 *
 * 线程安全性：所有操作必须在创建线程（主线程）中执行，不能跨线程使用。
 */
class IpcClient : public QObject
{
    Q_OBJECT

public:
    explicit IpcClient(QObject *parent = nullptr);
    ~IpcClient() override;

    // ========================================================================
    // 连接管理
    // ========================================================================

    /** 当前是否与守护进程处于已连接状态 */
    bool isSocketConnected() const;

    /** 当前是否正在尝试连接守护进程（ConnectingState） */
    bool isConnecting() const;

    /** 连接到守护进程。已在连接或正在连接中时不会重复发起 */
    void connectToDaemon();

    /** 断开与守护进程的连接 */
    void disconnectFromDaemon();

    // ========================================================================
    // 业务接口 - 异步调用，结果通过信号返回
    // ========================================================================

    /**
     * @brief 启动配置服务器客户端
     * @param url        配置服务器地址（即连接密钥）
     * @param hostname   本机主机名（QSystemInfo::machineHostName()）
     * @param machineId  机器唯一标识（当前与 hostname 相同）
     * @param secureMode 是否启用安全模式（加密通信）
     *
     * 所有字符串参数在发送时自动做 Base64 编码，后端会做相应解码。
     */
    void startConfigServerClient(const QString &url,
                                 const QString &hostname,
                                 const QString &machineId,
                                 bool secureMode = true);

    /** 停止配置服务器客户端 */
    void stopConfigServerClient();

    /** 查询配置服务器客户端当前是否已连接 */
    void isConfigServerClientConnected();

    /** 轮询后端推送的配置事件（peer 变更、状态更新等） */
    void pollConfigEvents();

signals:
    // ========================================================================
    // 连接状态信号
    // ========================================================================

    /** 已成功连接到后端守护进程 */
    void socketConnected();

    /** 与后端守护进程的连接已断开 */
    void socketDisconnected();

    /**
     * @brief IPC 通信错误
     * @param message 错误描述
     *
     * 可能的原因：socket 写失败、收到无效 JSON、方法调用失败等。
     */
    void ipcError(const QString &message);

    // ========================================================================
    // 业务结果信号
    // ========================================================================

    /**
     * @brief 启动配置服务器客户端的结果
     * @param success 是否成功
     * @param error   失败原因（成功时为空）
     */
    void startConfigServerClientFinished(bool success, const QString &error);

    /**
     * @brief 停止配置服务器客户端的结果
     * @param success 是否成功
     * @param error   失败原因
     */
    void stopConfigServerClientFinished(bool success, const QString &error);

    /**
     * @brief 配置服务器客户端连接状态查询结果
     * @param connected true=已连接，false=未连接
     */
    void isConfigServerClientConnectedFinished(bool connected);

    /**
     * @brief 配置事件轮询结果
     * @param events 事件列表，每个事件为 JSON 字符串
     */
    void pollConfigEventsFinished(const QStringList &events);

private slots:
    // ========================================================================
    // QLocalSocket 事件回调
    // ========================================================================

    /** socket 有可读数据 → 提取完整帧并分发到 processFrame */
    void onReadyRead();

    /** socket 发生错误 → 转发为 ipcError 信号 */
    void onSocketError(QLocalSocket::LocalSocketError socketError);

    /** socket 状态变化 → 转发为 socketConnected/socketDisconnected 信号 */
    void onStateChanged(QLocalSocket::LocalSocketState state);

private:
    // ========================================================================
    // 内部方法
    // ========================================================================

    /**
     * @brief 发送 JSON-RPC 请求
     * @param method 方法名（如 "start_config_server_client"）
     * @param params 参数字典
     *
     * 自动分配递增的请求 ID（m_nextRequestId），
     * 构造 JSON 消息后编码为帧格式发送。
     */
    void sendRequest(const QString &method, const QJsonObject &params);

    /**
     * @brief 处理接收到的帧负载
     * @param payload 解码后的 JSON 字符串
     *
     * 解析 JSON-RPC 响应，根据 method 字段分发到对应的业务结果信号。
     */
    void processFrame(const QByteArray &payload);

    /**
     * @brief 将 JSON 负载编码为帧格式
     * @param payload JSON 字符串（UTF-8）
     * @return 4 字节长度前缀 + payload 的完整帧
     *
     * 帧格式：| len (4 bytes, BE) | payload (len bytes) |
     */
    static QByteArray encodeFrame(const QByteArray &payload);

    /**
     * @brief Base64 编码（UTF-8）
     *
     * 后端要求部分参数以 Base64 编码传输，用于避免特殊字符问题。
     */
    static QString base64Encode(const QString &input);

    // ========================================================================
    // 成员变量
    // ========================================================================

    QLocalSocket *m_socket = nullptr;  ///< 本地套接字（Unix Domain Socket / 命名管道）
    QByteArray m_readBuffer;           ///< 接收缓冲区，处理粘包/半包问题
    qint64 m_nextRequestId = 1;        ///< 自增请求 ID，用于匹配请求和响应
};

#endif // IPCCLIENT_H
