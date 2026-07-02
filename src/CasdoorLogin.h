/**
 * @file CasdoorLogin.h
 * @brief Casdoor OAuth 登录类
 * 
 * 使用 PKCE (Proof Key for Code Exchange) 流程进行 OAuth 2.0 授权
 * 支持通过本地回调服务器接收授权码并交换访问令牌
 */

#ifndef CASDOOR_LOGIN_H
#define CASDOOR_LOGIN_H

#include <QObject>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>

/**
 * @brief Casdoor OAuth 登录类
 * 
 * 负责处理与 Casdoor 认证服务器的交互，包括：
 * - 生成 PKCE code verifier 和 code challenge
 * - 打开浏览器进行 OAuth 授权
 * - 运行本地回调服务器接收授权码
 * - 使用授权码交换访问令牌
 */
class CasdoorLogin : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief EasyTier Pro 工作区信息
     */
    struct ProTenantInfo {
        QString id;
        QString name;
    };

    /**
     * @brief 构造函数
     * @param clientId Casdoor 应用的 Client ID
     * @param parent 父对象
     */
    explicit CasdoorLogin(const QString &clientId, QObject *parent = nullptr);

    /**
     * @brief 开始 OAuth 登录流程
     * 
     * 该方法会：
     * 1. 启动本地回调服务器
     * 2. 生成 PKCE 参数
     * 3. 构造授权 URL
     * 4. 打开默认浏览器进行授权
     */
    void startLogin();
    
    /**
     * @brief 停止登录流程（清理资源）
     */
    void stopLogin();

    /**
     * @brief 使用当前登录会话切换 EasyTier Pro 工作区
     */
    void switchProTenant(const QString &tenantId);

    /**
     * @brief 当前登录会话可见的 EasyTier Pro 工作区
     */
    QList<ProTenantInfo> proTenants() const { return m_proTenants; }

private slots:
    /**
     * @brief 登录超时处理
     */
    void onLoginTimeout();

signals:
    /**
     * @brief 登录成功信号
     * @param deviceKey 设备接入密钥（etk 开头）
     * @param displayName 密钥显示名称
     * @param userId 用户ID
     * @param userDisplayName 用户显示名称
     * @param tenantName 组织显示名称
     */
    void loginSuccess(const QString &deviceKey, const QString &displayName, const QString &userId, const QString &userDisplayName, const QString &tenantName);
    
    /**
     * @brief 登录失败信号
     * @param errorMessage 错误信息
     */
    void loginFailed(const QString &errorMessage);

    /**
     * @brief EasyTier Pro 工作区列表已更新
     */
    void proTenantsUpdated();

private:
    /**
     * @brief 用授权码交换访问令牌
     * @param code 授权码
     */
    void swapCodeForToken(const QString &code);
    
    /**
     * @brief 获取 EasyTier Pro 组织信息
     * @param accessToken 访问令牌
     */
    void fetchProTenants(const QString &accessToken);
    
    /**
     * @brief 创建新的设备接入密钥并获取连接密钥
     * @param accessToken 访问令牌
     * @param tenantId 组织 ID
     * @param tenantName 组织显示名称
     */
    void createDeviceEnrollmentKey(const QString &accessToken, const QString &tenantId, const QString &tenantName);
    

    
    /**
     * @brief 显示组织选择对话框
     * @param tenants 组织列表
     * @return 选中的组织索引，-1 表示取消
     */
    int showTenantSelectionDialog(const QList<ProTenantInfo> &tenants);
    

    
    /**
     * @brief 生成随机字符串（用于 state 和 code verifier）
     * @param length 字符串长度
     * @return URL 安全的 Base64 编码随机字符串
     */
    QString makeRandomString(int length) const;
    
    /**
     * @brief 生成 PKCE code challenge
     * @param verifier Code verifier
     * @return SHA256 哈希后的 Base64 URL 编码字符串
     */
    QString makeCodeChallenge(const QString &verifier) const;

private:
    QString m_clientId;                       ///< Client ID
    QTcpServer *m_server = nullptr;          ///< 本地回调服务器
    QNetworkAccessManager *m_networkManager = nullptr; ///< 网络管理器
    QTimer *m_timeoutTimer = nullptr;        ///< 超时定时器
    
    QString m_codeVerifier;                   ///< PKCE code verifier
    QString m_state;                          ///< OAuth state 参数
    QString m_accessToken;                    ///< 访问令牌
    QString m_userId;                         ///< 用户ID
    QString m_userDisplayName;                ///< 用户显示名称
    QList<ProTenantInfo> m_proTenants;        ///< EasyTier Pro 工作区列表
    int m_callbackPort = 0;                   ///< 实际使用的回调端口
    QString m_callbackUrl;                    ///< 动态生成的回调地址

    static constexpr int TIME_OUT = 60000;     ///< 登录超时时间（60秒）
};

#endif // CASDOOR_LOGIN_H
