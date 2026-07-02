/**
 * @file CasdoorLogin.cpp
 * @brief Casdoor OAuth 登录类实现
 */

#include "CasdoorLogin.h"
#include <QDesktopServices>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QSysInfo>

CasdoorLogin::CasdoorLogin(const QString &clientId, QObject *parent)
    : QObject(parent), m_clientId(clientId)
{
    m_server = new QTcpServer(this);
    m_networkManager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    
    // 连接超时信号
    connect(m_timeoutTimer, &QTimer::timeout, this, &CasdoorLogin::onLoginTimeout);
    m_timeoutTimer->setSingleShot(true);
}

void CasdoorLogin::startLogin()
{
    // 1. 如果之前有未完成的登录，先清理
    stopLogin();
    m_proTenants.clear();

    // 2. 在本地启动回调服务器（尝试随机端口，最多10000次）
    m_callbackPort = 0;
    for (int attempt = 0; attempt < 10000; ++attempt) {
        int randomPort = QRandomGenerator::global()->bounded(10000, 65001);
        if (m_server->listen(QHostAddress::LocalHost, randomPort)) {
            m_callbackPort = randomPort;
            break;
        }
    }
    
    if (m_callbackPort == 0) {
        emit loginFailed("无法启动本地服务，未找到可用端口");
        return;
    }
    
    m_callbackUrl = QString("http://127.0.0.1:%1/callback").arg(m_callbackPort);

    // 3. 生成防 CSRF 的随机 state
    m_state = makeRandomString(32);
    
    // 4. 生成 PKCE code verifier 和 challenge
    m_codeVerifier = makeRandomString(64);
    QString codeChallenge = makeCodeChallenge(m_codeVerifier);

    // 5. 构造 OAuth 授权 URL
    // Casdoor 地址: https://auth.console.easytier.net/
    // 应用名: EasyTier
    QString authUrl = "https://auth.console.easytier.net/login/oauth/authorize?"
                      "client_id=" + m_clientId +
                      "&redirect_uri=" + m_callbackUrl +
                      "&response_type=code"
                      "&scope=openid profile"
                      "&state=" + m_state +
                      "&code_challenge=" + codeChallenge +
                      "&code_challenge_method=S256";

    // 5. 打开默认浏览器进行授权
    QDesktopServices::openUrl(QUrl(authUrl));

    // 6. 启动超时定时器
    m_timeoutTimer->start(TIME_OUT);

    // 7. 等待浏览器回调
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        QTcpSocket *socket = m_server->nextPendingConnection();
        
        // 等待 HTTP 请求
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QString request = QString::fromUtf8(socket->readAll());

            // 从请求中提取授权码
            QRegularExpression re("code=([^&\\s]+)");
            QRegularExpressionMatch match = re.match(request);

            if (match.hasMatch()) {
                QString code = match.captured(1);
                
                // 返回登录成功页面
                QString response = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/html; charset=utf-8\r\n"
                                   "Connection: close\r\n\r\n"
                                   "<!DOCTYPE html>"
                                   "<html>"
                                   "<head>"
                                   "<meta charset='utf-8'>"
                                   "<title>登录成功</title>"
                                   "<link rel='icon' href='https://console.easytier.net/favicon.svg'>"
                                   "</head>"
                                   "<body style='font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #1a1a2e;'>"
                                   "<div style='background-color: #16213e; border-radius: 10px; padding: 40px; box-shadow: 0 2px 10px rgba(0,0,0,0.3); display: inline-block; min-width: 400px;'>"
                                   "<img src='https://console.easytier.net/favicon.svg' width='80' height='80' style='margin-bottom: 20px;'>"
                                   "<h2 style='color: #66ccff; margin: 0 0 20px 0;'>登录成功!</h2>"
                                   "<p style='color: #e0e0e0; font-size: 16px; margin: 0;'>程序可能会有数秒的延迟，您可以关闭此标签页。</p>"
                                   "</div>"
                                   "</body>"
                                   "</html>";
                socket->write(response.toUtf8());
                socket->flush();
                socket->disconnectFromHost();
                
                // 关闭本地服务器
                m_server->close();

                // 停止超时定时器
                m_timeoutTimer->stop();

                // 使用授权码交换访问令牌
                swapCodeForToken(code);
            } else {
                // 提取错误信息
                QRegularExpression errorRe("error=([^&\\s]+)");
                QRegularExpressionMatch errorMatch = errorRe.match(request);
                QString errorMsg = "授权失败";
                if (errorMatch.hasMatch()) {
                    errorMsg += ": " + errorMatch.captured(1);
                }
                
                QString response = "HTTP/1.1 400 Bad Request\r\n"
                                   "Content-Type: text/html; charset=utf-8\r\n"
                                   "Connection: close\r\n\r\n"
                                   "<!DOCTYPE html>"
                                   "<html>"
                                   "<head>"
                                   "<meta charset='utf-8'>"
                                   "<title>登录失败</title>"
                                   "<link rel='icon' href='https://console.easytier.net/favicon.svg'>"
                                   "</head>"
                                   "<body style='font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #1a1a2e;'>"
                                   "<div style='background-color: #16213e; border-radius: 10px; padding: 40px; box-shadow: 0 2px 10px rgba(0,0,0,0.3); display: inline-block; min-width: 400px;'>"
                                   "<img src='https://console.easytier.net/favicon.svg' width='80' height='80' style='margin-bottom: 20px;'>"
                                   "<h2 style='color: #ff6b6b; margin: 0 0 20px 0;'>登录失败</h2>"
                                   "<p style='color: #e0e0e0; font-size: 16px; margin: 0;'>" + errorMsg + "</p>"
                                   "</div>"
                                   "</body>"
                                   "</html>";
                socket->write(response.toUtf8());
                socket->flush();
                socket->disconnectFromHost();
                
                m_server->close();
                m_timeoutTimer->stop();
                emit loginFailed(errorMsg);
            }
        });
    });
}

void CasdoorLogin::swapCodeForToken(const QString &code)
{
    QUrl url("https://auth.console.easytier.net/api/login/oauth/access_token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    // 构造 POST 请求参数
    QUrlQuery params;
    params.addQueryItem("grant_type", "authorization_code");
    params.addQueryItem("client_id", m_clientId);
    params.addQueryItem("code", code);
    params.addQueryItem("redirect_uri", m_callbackUrl);
    params.addQueryItem("code_verifier", m_codeVerifier);

    // 发送 POST 请求
    m_networkManager->post(request, params.toString(QUrl::FullyEncoded).toUtf8());

    // 处理响应
    connect(m_networkManager, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            QString errorMsg = "网络请求失败: " + reply->errorString();
            emit loginFailed(errorMsg);
            return;
        }

        // 解析 JSON 响应
        QByteArray responseData = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument json = QJsonDocument::fromJson(responseData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            emit loginFailed("解析响应失败: " + parseError.errorString());
            return;
        }

        QJsonObject obj = json.object();

        // 检查错误
        if (obj.contains("error")) {
            QString errorMsg = obj["error"].toString();
            if (obj.contains("error_description")) {
                errorMsg += ": " + obj["error_description"].toString();
            }
            emit loginFailed(errorMsg);
            return;
        }

        // 提取令牌
        QString accessToken = obj["access_token"].toString();

        if (!accessToken.isEmpty()) {
            // 保存 access_token 供后续使用
            m_accessToken = accessToken;
            
            // 开始获取 EasyTier Pro 工作区信息
            fetchProTenants(accessToken);
        } else {
            emit loginFailed("未获取到访问令牌");
        }
    });
}

QString CasdoorLogin::makeRandomString(int length) const
{
    // 生成随机字节数组
    QByteArray randomBytes(length, 0);
    for (int i = 0; i < length; ++i) {
        randomBytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    // 转换为 Base64 URL 安全编码（去除填充字符）
    return QString::fromLatin1(randomBytes.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals
    ));
}

QString CasdoorLogin::makeCodeChallenge(const QString &verifier) const
{
    // SHA256 哈希
    QByteArray hash = QCryptographicHash::hash(
        verifier.toUtf8(),
        QCryptographicHash::Sha256
    );

    // 转换为 Base64 URL 安全编码（去除填充字符）
    return QString::fromLatin1(hash.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals
    ));
}

void CasdoorLogin::stopLogin()
{
    // 停止超时定时器
    if (m_timeoutTimer->isActive()) {
        m_timeoutTimer->stop();
    }

    // 关闭服务器
    if (m_server->isListening()) {
        m_server->close();
    }
    // 断开所有信号连接
    m_server->disconnect();
    m_networkManager->disconnect();
}

void CasdoorLogin::switchProTenant(const QString &tenantId)
{
    if (m_accessToken.isEmpty()) {
        emit loginFailed("登录会话已过期，请重新登录后切换组织");
        return;
    }

    for (const ProTenantInfo &tenant : m_proTenants) {
        if (tenant.id == tenantId) {
            createDeviceEnrollmentKey(m_accessToken, tenant.id, tenant.name);
            return;
        }
    }

    emit loginFailed("未找到要切换的组织，请重新登录后重试");
}



void CasdoorLogin::onLoginTimeout()
{
    // 超时，停止登录流程
    stopLogin();

    // 发出登录失败信号
    emit loginFailed("登录超时，请在60秒内完成登录");
}

void CasdoorLogin::fetchProTenants(const QString &accessToken)
{
    QUrl url("https://api.console.easytier.net/api/v1/auth/me");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Bearer %1").arg(accessToken).toUtf8());
    
    m_networkManager->get(request);
    
    // 断开之前的 finished 连接，避免多次触发
    disconnect(m_networkManager, &QNetworkAccessManager::finished, nullptr, nullptr);
    
    connect(m_networkManager, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            emit loginFailed("获取组织列表失败: " + reply->errorString());
            return;
        }
        
        QByteArray responseData = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument json = QJsonDocument::fromJson(responseData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            emit loginFailed("解析组织列表失败: " + parseError.errorString());
            return;
        }
        
        QJsonObject obj = json.object();
        
        // 提取用户信息
        QJsonObject userObj = obj["user"].toObject();
        m_userId = userObj["id"].toString();
        m_userDisplayName = userObj["display_name"].toString();
        
        QJsonArray tenantsArray = obj["tenants"].toArray();
        
        QList<ProTenantInfo> tenants;
        for (const QJsonValue &value : tenantsArray) {
            QJsonObject tenantObj = value.toObject();
            ProTenantInfo info;
            info.id = tenantObj["id"].toString();
            info.name = tenantObj["name"].toString();
            if (!info.id.isEmpty()) {
                tenants.append(info);
            }
        }
        m_proTenants = tenants;
        emit proTenantsUpdated();
        
        if (tenants.isEmpty()) {
            emit loginFailed("未找到任何组织，请确保您有访问权限");
            return;
        }
        
        if (tenants.size() == 1) {
            // 只有一个组织，自动选择
            createDeviceEnrollmentKey(m_accessToken, tenants[0].id, tenants[0].name);
            return;
        }
        
        // 显示组织选择对话框
        int selectedIndex = showTenantSelectionDialog(tenants);
        if (selectedIndex >= 0 && selectedIndex < tenants.size()) {
            // 用户选择了组织，创建新的设备接入密钥
            createDeviceEnrollmentKey(m_accessToken, tenants[selectedIndex].id, tenants[selectedIndex].name);
        } else {
            // 用户取消选择
            emit loginFailed("用户取消了选择");
        }
    });
}

void CasdoorLogin::createDeviceEnrollmentKey(const QString &accessToken, const QString &tenantId, const QString &tenantName)
{
    const QString hostname = QSysInfo::machineHostName();
    
    QUrl url(QString("https://api.console.easytier.net/api/v1/tenants/%1/device-enrollment-keys").arg(tenantId));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("Bearer %1").arg(accessToken).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject bodyObj;
    bodyObj["display_name"] = hostname;
    bodyObj["reusable"] = false;
    bodyObj["pre_approved"] = true;
    bodyObj["owner_user_id"] = m_userId;
    QJsonDocument bodyDoc(bodyObj);
    QByteArray postData = bodyDoc.toJson(QJsonDocument::Compact);
    
    m_networkManager->post(request, postData);
    
    // 断开之前的 finished 连接
    disconnect(m_networkManager, &QNetworkAccessManager::finished, nullptr, nullptr);
    
    connect(m_networkManager, &QNetworkAccessManager::finished, this, [this, hostname, tenantName](QNetworkReply *reply) {
        reply->deleteLater();
        
        if (reply->error() != QNetworkReply::NoError) {
            emit loginFailed("创建密钥失败: " + reply->errorString());
            return;
        }
        
        QByteArray responseData = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument json = QJsonDocument::fromJson(responseData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            emit loginFailed("解析创建密钥响应失败: " + parseError.errorString());
            return;
        }
        
        QJsonObject obj = json.object();
        QString bootstrapToken = obj["bootstrap_token"].toString();
        
        if (bootstrapToken.isEmpty()) {
            emit loginFailed("未获取到设备接入密钥");
            return;
        }
        
        // 登录成功，发送设备接入密钥
        emit loginSuccess(bootstrapToken, hostname, m_userId, m_userDisplayName, tenantName);
    });
}



int CasdoorLogin::showTenantSelectionDialog(const QList<ProTenantInfo> &tenants)
{
    QDialog dialog;
    dialog.setWindowTitle("请选择组织");
    dialog.setMinimumWidth(400);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    QLabel *label = new QLabel("请选择您要使用的组织：", &dialog);
    layout->addWidget(label);
    
    QListWidget *listWidget = new QListWidget(&dialog);
    for (const ProTenantInfo &tenant : tenants) {
        listWidget->addItem(tenant.name);
    }
    listWidget->setCurrentRow(0);
    layout->addWidget(listWidget);
    
    QPushButton *okButton = new QPushButton("确定", &dialog);
    QPushButton *cancelButton = new QPushButton("取消", &dialog);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);
    
    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        return listWidget->currentRow();
    }
    return -1;
}
