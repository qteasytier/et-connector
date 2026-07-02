/**
 * @file ConfigManager.h
 * @brief 配置管理器 - 负责配置的 JSON 持久化存储
 *
 * 配置文件位置：
 *   Windows → %LOCALAPPDATA%/EasyTier/conf.json
 *   macOS   → ~/Library/Application Support/EasyTier/conf.json
 *   Linux   → ~/.local/share/EasyTier/conf.json
 *
 * 配置项说明：
 * - connectionKey      : 连接密钥（即配置服务器地址）
 * - autoStart           : 是否在系统启动时自动运行托盘程序
 * - secureMode          : 是否启用安全模式（加密通信）
 */

#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>

/**
 * @brief 配置管理器
 *
 * 使用 JSON 文件存储所有用户配置，支持：
 * - 配置的加载和保存（通过 QJsonDocument）
 * - JSON 格式有效性验证
 * - 配置文件不存在时自动创建目录并使用默认值
 *
 * 线程安全性：所有操作应在主线程中完成。
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager() override;
    
    // ========================================================================
    // 配置读取
    // ========================================================================
    QString getConnectionKey() const { return m_connectionKey; }
    bool getAutoStart() const { return m_autoStart; }
    QString getConfigFilePath() const { return m_configFilePath; }
    bool getSecureMode() const { return m_secureMode; }

    // ========================================================================
    // 配置写入（内存中，需调用 saveConfig() 持久化到磁盘）
    // ========================================================================
    void setConnectionKey(const QString &key) { m_connectionKey = key; }
    void setAutoStart(bool autoStart) { m_autoStart = autoStart; }
    void setSecureMode(bool secure) { m_secureMode = secure; }
    
    /** 将当前内存中的配置写入 JSON 文件 */
    bool saveConfig();
    
    /** 从 JSON 文件读取配置到内存 */
    bool loadConfig();
    
    /** 将所有配置项重置为默认值（不会自动保存到磁盘） */
    void resetToDefaults();

private:
    // ========================================================================
    // 配置数据
    // ========================================================================
    QString m_connectionKey;              ///< 连接密钥（配置服务器地址）
    bool m_autoStart = false;             ///< 是否开机启动托盘程序
    bool m_secureMode = true;             ///< 是否启用安全模式（默认启用）
    
    // ========================================================================
    // 文件路径
    // ========================================================================
    QString m_configFilePath;             ///< 配置文件的完整路径
    QString m_configDirPath;              ///< 配置文件所在目录的路径
    
    /**
     * @brief 初始化配置文件路径
     *
     * 按优先级尝试以下位置：
     * 1. QStandardPaths::AppLocalDataLocation（各平台标准数据目录）
     * 2. QStandardPaths::AppDataLocation
     * 3. QStandardPaths::GenericDataLocation
     * 4. 当前工作目录/EasyTier（最后备选）
     *
     * 通过写入测试文件验证目录可写性。
     */
    void initializeConfigPath();
    
    /** 确保配置目录存在，不存在则创建 */
    bool ensureConfigDirectory();
    
    /**
     * @brief 验证配置文件内容是否为合法 JSON 对象
     * @param jsonData 文件原始字节
     * @return 是否为合法 JSON 对象
     */
    static bool validateConfig(const QByteArray &jsonData);
};

#endif // CONFIGMANAGER_H
