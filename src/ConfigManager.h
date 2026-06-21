/**
 * @file ConfigManager.h
 * @brief 配置管理器 - 负责配置的持久化存储
 * 
 * 配置文件位置：%APPDATA%/EasyTier/conf.json (Windows)
 * 
 * 配置项：
 * - connectionKey: 连接密钥
 * - autoStart: 是否开机自启
 */

#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>

/**
 * @brief 配置管理器
 * 
 * 使用 JSON 文件存储配置，支持：
 * - 配置的加载和保存
 * - 配置文件有效性验证
 * - 自动创建配置目录
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() override;
    
    // 获取配置
    QString getConnectionKey() const { return m_connectionKey; }
    bool getAutoStart() const { return m_autoStart; }
    QString getConfigFilePath() const { return m_configFilePath; }
    bool getRememberQuitChoice() const { return m_rememberQuitChoice; }
    bool getStopOnQuit() const { return m_stopOnQuit; }

    // 设置配置
    void setConnectionKey(const QString &key) { m_connectionKey = key; }
    void setAutoStart(bool autoStart) { m_autoStart = autoStart; }
    void setRememberQuitChoice(bool remember) { m_rememberQuitChoice = remember; }
    void setStopOnQuit(bool stop) { m_stopOnQuit = stop; }
    
    /**
     * @brief 保存配置到文件
     * @return 是否保存成功
     */
    bool saveConfig();
    
    /**
     * @brief 从文件加载配置
     * @return 是否加载成功
     */
    bool loadConfig();
    
    /**
     * @brief 重置配置为默认值
     */
    void resetToDefaults();

private:
    // 配置数据
    QString m_connectionKey;       ///< 连接密钥
    bool m_autoStart = false;      ///< 是否开机启动托盘程序
    bool m_rememberQuitChoice = false; ///< 是否记住退出时是否停止EasyTier的选择
    bool m_stopOnQuit = true;          ///< 记住的选择：退出时是否停止EasyTier（默认停止）
    
    // 配置文件路径
    QString m_configFilePath;     ///< 配置文件完整路径
    QString m_configDirPath;      ///< 配置目录路径
    
    /**
     * @brief 初始化配置路径
     */
    void initializeConfigPath();
    
    /**
     * @brief 确保配置目录存在
     * @return 目录是否存在或创建成功
     */
    bool ensureConfigDirectory();
    
    /**
     * @brief 验证配置文件内容
     * @param jsonData JSON 数据
     * @return 配置是否有效
     */
    static bool validateConfig(const QByteArray &jsonData);
};

#endif // CONFIGMANAGER_H
