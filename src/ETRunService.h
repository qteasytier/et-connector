/**
 * @file ETRunService.h
 * @brief EasyTier Core 系统服务管理器
 * 
 * 通过 easytier-cli 管理 EasyTier Core 系统服务。
 * 服务安装后默认开机自启。
 * 
 * 设计说明：
 * - 由于所有方法都是静态的，不需要继承 QObject
 * - 使用 CommandResult 结构体封装命令执行结果
 */

#ifndef ETRUNSERVICE_H
#define ETRUNSERVICE_H

#include <QString>
#include <QStringList>

enum class ServiceError {
    None,
    CliMissing,
    CoreMissing,
    UacCancelled,
    InstallFailed,
    StartFailed,
    StopFailed,
    UninstallFailed,
    Timeout,
    Unknown
};

/**
 * @brief 命令执行结果结构体
 */
struct CommandResult {
    int exitCode = -1;          ///< 进程退出码，-1 表示执行失败
    QString output;             ///< 标准输出和标准错误的合并内容
    bool success = false;       ///< 是否执行成功（exitCode == 0）
    QString errorString;        ///< 错误描述（如果有）
    ServiceError error = ServiceError::None; ///< 结构化错误类型
    
    [[nodiscard]] bool outputContains(const QString &text) const {
        return output.contains(text, Qt::CaseInsensitive);
    }
};

/**
 * @brief EasyTier Core 系统服务管理器
 * 
 * 提供静态方法管理 Windows 系统服务。
 * 服务名称: "QtET Web Connector"
 */
class ETRunService
{
public:
    ETRunService() = default;
    ~ETRunService() = default;
    
    // 禁止拷贝
    ETRunService(const ETRunService&) = delete;
    ETRunService& operator=(const ETRunService&) = delete;
    
    /**
     * @brief 启动服务
     * @param connectionKey 连接密钥
     * @return 是否启动成功
     * 
     * 如果服务未安装，会合并安装+启动为一条命令执行，只需一次 UAC 授权。
     * 如果服务已安装，直接启动（一次 UAC 授权）。
     */
    static bool start(const QString &connectionKey);

    /**
     * @brief 未安装则安装并启动，已安装则直接启动
     * @param connectionKey 连接密钥
     * @param result 可选返回详细结果
     * @return 是否启动成功
     */
    static bool startOrInstall(const QString &connectionKey, CommandResult *result = nullptr);
    
    /**
     * @brief 停止并卸载服务
     * @return 是否操作成功
     * 
     * 合并停止+卸载为一条命令执行，只需一次 UAC 授权。
     */
    static bool stop();

    /**
     * @brief 暂停连接，仅停止服务，不卸载
     * @param result 可选返回详细结果
     * @return 是否停止成功
     */
    static bool pauseConnection(CommandResult *result = nullptr);

    /**
     * @brief 移除服务，停止并卸载
     * @param result 可选返回详细结果
     * @return 是否移除成功
     */
    static bool removeService(CommandResult *result = nullptr);
    
    /**
     * @brief 查询 easytier-core 进程是否正在运行
     * @return 进程是否存在
     */
    static bool isRunning();
    
    /**
     * @brief 检查服务是否已安装
     * @return 服务是否已安装
     */
    static bool isServiceInstalled();
    
    /**
     * @brief 获取 easytier-cli 路径
     * @return CLI 工具完整路径
     */
    static QString getCliPath();
    
    /**
     * @brief 获取工作目录
     * @return etcore 目录路径
     */
    static QString getWorkingDirectory();

    /**
     * @brief 获取 easytier-deamon 路径
     * @return Core 工具完整路径
     */
    static QString getCorePath();

    /**
     * @brief 查询节点信息 JSON
     */
    static CommandResult queryNodeInfoJson(int timeoutMs = 5000);

    /**
     * @brief 查询 peer 列表 JSON
     */
    static CommandResult queryPeerListJson(int timeoutMs = 5000);

    /**
     * @brief 查询路由列表 JSON
     */
    static CommandResult queryRouteListJson(int timeoutMs = 5000);

    /**
     * @brief 获取 Core 版本输出
     */
    static CommandResult queryCoreVersion(int timeoutMs = 5000);

    /**
     * @brief 错误类型转用户可读文本
     */
    static QString serviceErrorToString(ServiceError error);
    
    /**
     * @brief 执行命令并等待完成
     * @param command 命令路径
     * @param args 命令参数
     * @param timeoutMs 超时时间（毫秒），默认 30 秒
     * @return 命令执行结果
     */
    static CommandResult executeCommand(const QString &command, 
                                        const QStringList &args,
                                        int timeoutMs = 30000);

private:
    /// Windows 服务内部名称（不含空格）
    static constexpr const char* SERVICE_NAME = "QtETWebConnector";
};

#endif // ETRUNSERVICE_H
