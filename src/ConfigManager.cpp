/**
 * @file ConfigManager.cpp
 * @brief 配置管理器实现
 */

#include "ConfigManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

// ============================================================================
// 构造 / 析构
// ============================================================================

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    initializeConfigPath();
    ensureConfigDirectory();
}

ConfigManager::~ConfigManager()
{
    // 不在析构函数中自动保存 —— 调用方应在合适时机显式调用 saveConfig()
}

// ============================================================================
// 配置路径初始化
// ============================================================================

void ConfigManager::initializeConfigPath()
{
    // 候选路径列表，按优先级从高到低排列
    QStringList candidatePaths;
    
    // 第一优先级：AppLocalDataLocation
    //   Windows: C:/Users/<user>/AppData/Local/<AppName>
    //   Linux:   ~/.local/share/<AppName>
    //   macOS:   ~/Library/Application Support/<AppName>
    candidatePaths << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    candidatePaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    candidatePaths << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    // 最后备选：当前工作目录
    candidatePaths << QDir::currentPath();
    
    for (const QString &basePath : candidatePaths) {
        if (basePath.isEmpty()) continue;
        
        QString configDir = basePath;

        // 通过创建测试文件验证目录是否可写
        if (QDir dir(configDir); dir.exists() || dir.mkpath(".")) {
            QFile testFile(configDir + "/.test");
            if (testFile.open(QIODevice::WriteOnly)) {
                testFile.close();
                testFile.remove();  // 立即删除测试文件
                
                m_configDirPath = configDir;
                m_configFilePath = configDir + "/conf.json";
                std::clog << "ConfigManager: 配置路径: " << m_configFilePath.toStdString() << std::endl;
                return;
            }
        }
    }
    
    // 所有备选路径都不可写，使用当前目录作为最终兜底
    m_configDirPath = QDir::currentPath() + "/EasyTier";
    m_configFilePath = m_configDirPath + "/conf.json";
    std::cerr << "ConfigManager: 无法找到可写配置目录，使用: " << m_configFilePath.toStdString() << std::endl;
}

bool ConfigManager::ensureConfigDirectory()
{
    if (m_configDirPath.isEmpty()) {
        return false;
    }
    
    QDir dir(m_configDirPath);
    if (dir.exists()) {
        return true;
    }
    
    if (dir.mkpath(".")) {
        std::clog << "ConfigManager: 创建配置目录: " << m_configDirPath.toStdString() << std::endl;
        return true;
    }
    
    std::cerr << "ConfigManager: 无法创建配置目录: " << m_configDirPath.toStdString() << std::endl;
    return false;
}

// ============================================================================
// 配置验证
// ============================================================================

bool ConfigManager::validateConfig(const QByteArray &jsonData)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        std::cerr << "ConfigManager: JSON 解析错误: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    
    // 仅验证是 JSON 对象即可，不强制要求包含特定字段
    // 缺失的字段在 loadConfig() 中会使用默认值
    if (!doc.isObject()) {
        std::cerr << "ConfigManager: 配置文件格式错误，应为 JSON 对象" << std::endl;
        return false;
    }
    
    return true;
}

// ============================================================================
// 配置持久化
// ============================================================================

bool ConfigManager::saveConfig()
{
    if (!ensureConfigDirectory()) {
        return false;
    }
    
    // 将所有配置项序列化为 JSON 对象
    QJsonObject configObj;
    configObj["connectionKey"] = m_connectionKey;
    configObj["autoStart"] = m_autoStart;
    configObj["secureMode"] = m_secureMode;
    
    QJsonDocument doc(configObj);
    // Indented 格式方便用户手动查看/编辑
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    
    QFile configFile(m_configFilePath);
    if (!configFile.open(QIODevice::WriteOnly)) {
        std::cerr << "ConfigManager: 无法打开配置文件写入: " << configFile.errorString().toStdString() << std::endl;
        return false;
    }
    
    qint64 written = configFile.write(jsonData);
    configFile.close();
    
    if (written != jsonData.size()) {
        std::cerr << "ConfigManager: 配置文件写入不完整" << std::endl;
        return false;
    }
    
    std::clog << "ConfigManager: 配置已保存" << std::endl;
    return true;
}

bool ConfigManager::loadConfig()
{
    QFile configFile(m_configFilePath);
    
    // 配置文件不存在 → 使用默认值，不是错误
    if (!configFile.exists()) {
        std::clog << "ConfigManager: 配置文件不存在，使用默认配置" << std::endl;
        resetToDefaults();
        return true;
    }
    
    if (!configFile.open(QIODevice::ReadOnly)) {
        std::cerr << "ConfigManager: 无法打开配置文件: " << configFile.errorString().toStdString() << std::endl;
        return false;
    }
    
    QByteArray jsonData = configFile.readAll();
    configFile.close();
    
    // JSON 格式非法 → 使用默认值，避免程序崩溃
    if (!validateConfig(jsonData)) {
        std::cerr << "ConfigManager: 配置文件验证失败，使用默认配置" << std::endl;
        resetToDefaults();
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    QJsonObject configObj = doc.object();
    
    // 安全读取：每个字段都提供默认值，兼容旧版本配置中缺失的字段
    // toString("") 和 toBool(false) 会处理类型不匹配的情况
    m_connectionKey = configObj["connectionKey"].toString("");
    m_autoStart = configObj["autoStart"].toBool(false);
    m_secureMode = configObj["secureMode"].toBool(true);
    
    std::clog << "ConfigManager: 配置已加载" << std::endl;
    return true;
}

void ConfigManager::resetToDefaults()
{
    m_connectionKey.clear();
    m_autoStart = false;
    m_secureMode = true;
    std::clog << "ConfigManager: 配置已重置为默认值" << std::endl;
}
