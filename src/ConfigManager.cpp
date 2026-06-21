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

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    initializeConfigPath();
    ensureConfigDirectory();
}

ConfigManager::~ConfigManager()
{
    // 不在析构函数中自动保存，由调用方决定是否保存
}

void ConfigManager::initializeConfigPath()
{
    // 优先使用标准配置目录
    QStringList candidatePaths;
    
    // Windows: C:/Users/<user>/AppData/Local/<AppName>
    candidatePaths << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    // 备选: AppDataLocation
    candidatePaths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // 备选: GenericDataLocation
    candidatePaths << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    // 最后备选: 当前目录
    candidatePaths << QDir::currentPath();
    
    for (const QString &basePath : candidatePaths) {
        if (basePath.isEmpty()) continue;
        
        QString configDir = basePath;

        // 检查目录是否可写
        if (QDir dir(configDir); dir.exists() || dir.mkpath(".")) {
            QFile testFile(configDir + "/.test");
            if (testFile.open(QIODevice::WriteOnly)) {
                testFile.close();
                testFile.remove();
                
                m_configDirPath = configDir;
                m_configFilePath = configDir + "/conf.json";
                std::clog << "ConfigManager: 配置路径: " << m_configFilePath.toStdString() << std::endl;
                return;
            }
        }
    }
    
    // 如果所有路径都不可用，使用当前目录
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

bool ConfigManager::validateConfig(const QByteArray &jsonData)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        std::cerr << "ConfigManager: JSON 解析错误: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    
    if (!doc.isObject()) {
        std::cerr << "ConfigManager: 配置文件格式错误，应为 JSON 对象" << std::endl;
        return false;
    }
    
    return true;
}

bool ConfigManager::saveConfig()
{
    if (!ensureConfigDirectory()) {
        return false;
    }
    
    QJsonObject configObj;
    configObj["connectionKey"] = m_connectionKey;
    configObj["autoStart"] = m_autoStart;
    configObj["rememberQuitChoice"] = m_rememberQuitChoice;
    configObj["stopOnQuit"] = m_stopOnQuit;
    
    QJsonDocument doc(configObj);
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
    
    if (!validateConfig(jsonData)) {
        std::cerr << "ConfigManager: 配置文件验证失败，使用默认配置" << std::endl;
        resetToDefaults();
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    QJsonObject configObj = doc.object();
    
    // 安全读取配置项
    m_connectionKey = configObj["connectionKey"].toString("");
    m_autoStart = configObj["autoStart"].toBool(false);
    m_rememberQuitChoice = configObj["rememberQuitChoice"].toBool(false);
    m_stopOnQuit = configObj["stopOnQuit"].toBool(true);
    
    std::clog << "ConfigManager: 配置已加载" << std::endl;
    return true;
}

void ConfigManager::resetToDefaults()
{
    m_connectionKey.clear();
    m_autoStart = false;
    m_rememberQuitChoice = false;
    m_stopOnQuit = true;
    std::clog << "ConfigManager: 配置已重置为默认值" << std::endl;
}
