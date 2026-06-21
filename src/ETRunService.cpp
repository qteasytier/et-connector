/**
 * @file ETRunService.cpp
 * @brief EasyTier Core 系统服务管理器实现
 */

#include "ETRunService.h"
#include <QCoreApplication>
#include <QDir>
#include <QSysInfo>
#include <QSettings>
#include <QProcess>
#include <QFile>
#include <iostream>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

/**
 * @brief 以管理员权限执行命令（通过 UAC/pkexec 提权）
 *
 * Windows: 使用 ShellExecuteExW 配合 "runas" 动词触发 UAC 提示框。
 * Linux: 使用 pkexec 提权执行命令。
 *
 * @param program 程序路径
 * @param arguments 参数列表
 * @param workingDir 工作目录
 * @param result 可选详细结果
 * @return 是否成功启动提权进程
 */
static bool executeElevated(const QString &program,
                            const QStringList &arguments,
                            const QString &workingDir,
                            CommandResult *result)
{
#ifdef Q_OS_WIN32
    // 转换为 Windows 原生路径格式
    QString nativeProgram = QDir::toNativeSeparators(program);
    QString nativeWorkingDir = QDir::toNativeSeparators(workingDir);
    QString args = arguments.join(' ');

    std::clog << "ETRunService: 执行 UAC 提权命令: " << nativeProgram.toStdString() << " " << args.toStdString() << std::endl;

    // 初始化 SHELLEXECUTEINFOW 结构体
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = nullptr;
    sei.lpVerb = L"runas";  // UAC 提权动词
    sei.lpFile = reinterpret_cast<LPCWSTR>(nativeProgram.utf16());
    sei.lpParameters = args.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(args.utf16());
    sei.lpDirectory = nativeWorkingDir.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(nativeWorkingDir.utf16());
    sei.nShow = SW_HIDE;  // 隐藏窗口

    // 执行 ShellExecuteExW
    if (!ShellExecuteExW(&sei)) {
        DWORD error = GetLastError();
        std::cerr << "ETRunService: ShellExecuteExW 失败, 错误码: " << error << std::endl;
        if (result) {
            result->exitCode = static_cast<int>(error);
            result->success = false;
            result->error = (error == ERROR_CANCELLED) ? ServiceError::UacCancelled : ServiceError::Unknown;
            result->errorString = (error == ERROR_CANCELLED)
                                      ? "用户取消了 UAC 授权"
                                      : QString("提权命令启动失败，错误码: %1").arg(error);
        }
        return false;
    }

    // 等待进程完成
    if (sei.hProcess) {
        DWORD waitResult = WaitForSingleObject(sei.hProcess, 120000);  // 2分钟超时

        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(sei.hProcess, 1);
            CloseHandle(sei.hProcess);
            std::cerr << "ETRunService: UAC 提权命令执行超时" << std::endl;
            if (result) {
                result->success = false;
                result->error = ServiceError::Timeout;
                result->errorString = "提权命令执行超时";
            }
            return false;
        }

        // 获取退出码
        DWORD exitCode = 0;
        if (GetExitCodeProcess(sei.hProcess, &exitCode)) {
            std::clog << "ETRunService: UAC 提权命令退出码: " << exitCode << std::endl;
        }

        CloseHandle(sei.hProcess);

        // 用户取消 UAC 时，exitCode 通常为 1223 (ERROR_CANCELLED)
        if (exitCode == 1223) {
            std::cerr << "ETRunService: 用户取消了 UAC 授权" << std::endl;
            if (result) {
                result->exitCode = static_cast<int>(exitCode);
                result->success = false;
                result->error = ServiceError::UacCancelled;
                result->errorString = "用户取消了 UAC 授权";
            }
            return false;
        }

        if (result) {
            result->exitCode = static_cast<int>(exitCode);
            result->success = (exitCode == 0);
            if (!result->success) {
                result->error = ServiceError::Unknown;
                result->errorString = QString("提权命令退出码: %1").arg(exitCode);
            }
        }
        return exitCode == 0;
    }

    if (result) {
        result->success = true;
    }
    return true;
#elif defined(Q_OS_MACOS)
    // macOS: 使用 osascript 提权，弹出原生认证对话框
    QString cmd = QString("\"%1\" %2").arg(program, arguments.join(" "));
    QString appleScript = QString("do shell script \"%1\" with administrator privileges").arg(QString(cmd).replace("\"", "\\\""));

    std::clog << "ETRunService: 执行 osascript 提权命令: " << cmd.toStdString() << std::endl;

    QProcess process;
    process.setWorkingDirectory(workingDir);
    process.start("osascript", QStringList() << "-e" << appleScript);

    if (!process.waitForStarted(5000)) {
        std::cerr << "ETRunService: osascript 启动失败: " << process.errorString().toStdString() << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::Unknown;
            result->errorString = "提权命令启动失败: " + process.errorString();
        }
        return false;
    }

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        std::cerr << "ETRunService: osascript 命令执行超时" << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::Timeout;
            result->errorString = "提权命令执行超时";
        }
        return false;
    }

    int exitCode = process.exitCode();
    QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    std::clog << "ETRunService: osascript 命令退出码: " << exitCode << std::endl;

    if (exitCode != 0) {
        QString errOutput = output;
        if (errOutput.contains("User canceled", Qt::CaseInsensitive)) {
            std::cerr << "ETRunService: 用户取消了授权" << std::endl;
        } else if (!errOutput.isEmpty()) {
            std::cerr << "ETRunService: osascript 错误输出: " << errOutput.toStdString() << std::endl;
        }
    }

    if (result) {
        result->exitCode = exitCode;
        result->output = output;
        result->success = (exitCode == 0);
        if (!result->success) {
            result->error = output.contains("User canceled", Qt::CaseInsensitive) ? ServiceError::UacCancelled : ServiceError::Unknown;
            result->errorString = (result->error == ServiceError::UacCancelled) ? "用户取消了授权" : output.trimmed();
        }
    }
    return exitCode == 0;
#else
    // Linux: 使用 pkexec 提权
    QStringList pkexecArgs;
    pkexecArgs << program << arguments;

    std::clog << "ETRunService: 执行 pkexec 提权命令: pkexec " << program.toStdString() << " " << arguments.join(' ').toStdString() << std::endl;

    QProcess process;
    process.setWorkingDirectory(workingDir);
    process.start("pkexec", pkexecArgs);

    if (!process.waitForStarted(5000)) {
        std::cerr << "ETRunService: pkexec 启动失败: " << process.errorString().toStdString() << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::Unknown;
            result->errorString = "提权命令启动失败: " + process.errorString();
        }
        return false;
    }

    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        std::cerr << "ETRunService: pkexec 命令执行超时" << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::Timeout;
            result->errorString = "提权命令执行超时";
        }
        return false;
    }

    int exitCode = process.exitCode();
    QString output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    std::clog << "ETRunService: pkexec 命令退出码: " << exitCode << std::endl;

    // pkexec 用户取消时退出码通常为 126
    if (exitCode == 126) {
        std::cerr << "ETRunService: 用户取消了 pkexec 授权" << std::endl;
        if (result) {
            result->exitCode = exitCode;
            result->output = output;
            result->success = false;
            result->error = ServiceError::UacCancelled;
            result->errorString = "用户取消了授权";
        }
        return false;
    }

    if (exitCode != 0) {
        QString errOutput = output;
        if (!errOutput.isEmpty()) {
            std::cerr << "ETRunService: pkexec 错误输出: " << errOutput.toStdString() << std::endl;
        }
    }

    if (result) {
        result->exitCode = exitCode;
        result->output = output;
        result->success = (exitCode == 0);
        if (!result->success) {
            result->error = ServiceError::Unknown;
            result->errorString = output.trimmed();
        }
    }
    return exitCode == 0;
#endif
}

QString ETRunService::getCliPath()
{
    QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN32
    return QDir::toNativeSeparators(appDir + "/etcore/easytier-cli.exe");
#else
    return appDir + "/etcore/easytier-cli";
#endif
}

QString ETRunService::getWorkingDirectory()
{
    QString appDir = QCoreApplication::applicationDirPath();
    return appDir + "/etcore";
}

QString ETRunService::getCorePath()
{
    QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN32
    return QDir::toNativeSeparators(appDir + "/etcore/easytier-deamon.exe");
#else
    return appDir + "/etcore/easytier-deamon";
#endif
}

CommandResult ETRunService::executeCommand(const QString &command, 
                                           const QStringList &args,
                                           const int timeoutMs)
{
    CommandResult result;
    
    QProcess process;
    process.setWorkingDirectory(getWorkingDirectory());
    process.start(command, args);
    
    if (!process.waitForStarted(5000)) {
        result.errorString = QString("无法启动进程: %1").arg(process.errorString());
        result.error = ServiceError::Unknown;
        std::cerr << "ETRunService: " << result.errorString.toStdString() << std::endl;
        return result;
    }
    
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        result.errorString = "命令执行超时";
        result.error = ServiceError::Timeout;
        std::cerr << "ETRunService: " << result.errorString.toStdString() << std::endl;
        return result;
    }
    
    result.exitCode = process.exitCode();
    result.output = QString::fromUtf8(
        process.readAllStandardOutput() + process.readAllStandardError()
    );
    result.success = (result.exitCode == 0);
    if (!result.success) {
        result.error = ServiceError::Unknown;
        result.errorString = result.output.trimmed();
    }
    
    return result;
}

CommandResult ETRunService::queryNodeInfoJson(const int timeoutMs)
{
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        CommandResult result;
        result.error = ServiceError::CliMissing;
        result.errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        return result;
    }

    return executeCommand(cliPath, QStringList() << "-o" << "json" << "node" << "info", timeoutMs);
}

CommandResult ETRunService::queryPeerListJson(const int timeoutMs)
{
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        CommandResult result;
        result.error = ServiceError::CliMissing;
        result.errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        return result;
    }

    return executeCommand(cliPath, QStringList() << "-o" << "json" << "peer" << "list", timeoutMs);
}

CommandResult ETRunService::queryRouteListJson(const int timeoutMs)
{
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        CommandResult result;
        result.error = ServiceError::CliMissing;
        result.errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        return result;
    }

    return executeCommand(cliPath, QStringList() << "-o" << "json" << "route" << "list", timeoutMs);
}

CommandResult ETRunService::queryCoreVersion(const int timeoutMs)
{
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        CommandResult result;
        result.error = ServiceError::CliMissing;
        result.errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        return result;
    }

    return executeCommand(cliPath, QStringList() << "--version", timeoutMs);
}

QString ETRunService::serviceErrorToString(const ServiceError error)
{
    switch (error) {
    case ServiceError::None:
        return "无错误";
    case ServiceError::CliMissing:
        return "找不到 easytier-cli.exe，请检查 etcore 目录是否完整";
    case ServiceError::CoreMissing:
        return "找不到 easytier-deamon.exe，请检查 etcore 目录是否完整";
    case ServiceError::UacCancelled:
        return "用户取消了管理员权限授权";
    case ServiceError::InstallFailed:
        return "EasyTier Core 服务安装失败";
    case ServiceError::StartFailed:
        return "EasyTier Core 服务启动失败";
    case ServiceError::StopFailed:
        return "EasyTier Core 服务停止失败";
    case ServiceError::UninstallFailed:
        return "EasyTier Core 服务卸载失败";
    case ServiceError::Timeout:
        return "操作超时";
    case ServiceError::Unknown:
        return "未知错误";
    }

    return "未知错误";
}

bool ETRunService::isServiceInstalled()
{
#ifdef Q_OS_WIN32
    QSettings settings(
        R"(HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services)",
        QSettings::NativeFormat
    );

    return settings.childGroups().contains(SERVICE_NAME);
#elif defined(Q_OS_MACOS)
    // macOS: 检查 launchd plist 是否存在
    QString plistPath = QString("/Library/LaunchDaemons/%1.plist").arg(SERVICE_NAME);
    return QFile::exists(plistPath);
#else
    // Linux: 检查 systemd 服务单元文件是否存在
    QString serviceFilePath = QString("/etc/systemd/system/%1.service").arg(SERVICE_NAME);
    return QFile::exists(serviceFilePath);
#endif
}

bool ETRunService::start(const QString &connectionKey)
{
    return startOrInstall(connectionKey);
}

bool ETRunService::startOrInstall(const QString &connectionKey, CommandResult *result)
{
    if (connectionKey.isEmpty()) {
        std::cerr << "ETRunService::start: 连接密钥为空" << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::Unknown;
            result->errorString = "连接凭据为空";
        }
        return false;
    }
    
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        std::cerr << "ETRunService::start: 找不到 easytier-cli: " << cliPath.toStdString() << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::CliMissing;
            result->errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        }
        return false;
    }

    const QString corePath = getCorePath();
    if (!QFile::exists(corePath)) {
        std::cerr << "ETRunService::start: 找不到 easytier-deamon: " << corePath.toStdString() << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::CoreMissing;
            result->errorString = QString("找不到 easytier-deamon: %1").arg(corePath);
        }
        return false;
    }
    
    QString workDir = getWorkingDirectory();
    
    // 检查服务是否已在运行
    if (isRunning()) {
        std::clog << "ETRunService: 服务已在运行中" << std::endl;
        if (result) {
            result->success = true;
            result->error = ServiceError::None;
        }
        return true;
    }
    
    // 如果服务未安装，合并安装+启动
    if (!isServiceInstalled()) {
        const QString hostname = QSysInfo::machineHostName();
        
#ifdef Q_OS_WIN32
        // Windows: 使用 cmd /c 串联安装和启动命令
        const QString workPath = corePath;
        QString installCmd = QString("\"%1\" service install --core-path \"%2\" --display-name \"%3\" -- -w \"%4\" --hostname \"%5\" --secure-mode true")
                                 .arg(cliPath, workPath, SERVICE_NAME, connectionKey, hostname);
        QString startCmd = QString("\"%1\" service start").arg(cliPath);
        
        QStringList args;
        args << "/c" << QString("\"%1 && %2\"").arg(installCmd, startCmd);
        
        std::clog << "ETRunService: 安装并启动服务 (需要UAC授权)" << std::endl;
        
        if (!executeElevated("cmd.exe", args, workDir, result)) {
            std::cerr << "ETRunService: 安装并启动服务失败" << std::endl;
            if (result && result->error == ServiceError::None) {
                result->error = ServiceError::InstallFailed;
                result->errorString = "服务安装或启动失败";
            }
            return false;
        }
        
        std::clog << "ETRunService: 安装并启动服务成功" << std::endl;
        if (result) {
            result->success = true;
            result->error = ServiceError::None;
        }
        return true;
#else
        // Linux: 构造复合命令，用 && 连接安装和启动
        QStringList installArgs;
        installArgs << "service" << "-n" << SERVICE_NAME << "install" << "--core-path";
        installArgs << corePath;
        installArgs << "--display-name" << SERVICE_NAME;
        installArgs << "--" << "-w" << connectionKey << "--hostname" << hostname << "--secure-mode" << "true";
        
        QStringList startArgs;
        startArgs << "service" << "-n" << SERVICE_NAME << "start";
        
        // 构造复合命令: install && start
        QString combinedCmd = QString("\"%1\" %2 && \"%1\" %3")
                                 .arg(cliPath)
                                 .arg(installArgs.join(" "))
                                 .arg(startArgs.join(" "));
        
        std::clog << "ETRunService: 安装并启动服务 (需要pkexec授权)" << std::endl;
        std::clog << "复合命令: " << combinedCmd.toStdString() << std::endl;
        
        // 使用 bash -c 执行复合命令
        QStringList bashArgs;
        bashArgs << "-c" << combinedCmd;
        
        if (!executeElevated("bash", bashArgs, workDir, result)) {
            std::cerr << "ETRunService: 安装并启动服务失败" << std::endl;
            if (result && result->error == ServiceError::None) {
                result->error = ServiceError::InstallFailed;
                result->errorString = "服务安装或启动失败";
            }
            return false;
        }
        
        std::clog << "ETRunService: 安装并启动服务成功" << std::endl;
        if (result) {
            result->success = true;
            result->error = ServiceError::None;
        }
        return true;
#endif
    } else {
        // 服务已安装：直接启动（只需一次提权授权）
        QStringList startArgs;
        startArgs << "service";
#ifndef Q_OS_WIN32
        startArgs << "-n" << SERVICE_NAME;
#endif
        startArgs << "start";

        std::clog << "ETRunService: 启动服务 (需要提权授权), 参数: " << startArgs.join(" ").toStdString() << std::endl;

        if (!executeElevated(cliPath, startArgs, workDir, result)) {
            std::cerr << "ETRunService: 服务启动失败" << std::endl;
            if (result && result->error == ServiceError::None) {
                result->error = ServiceError::StartFailed;
                result->errorString = "服务启动失败";
            }
            return false;
        }

        std::clog << "ETRunService: 服务启动成功" << std::endl;
        if (result) {
            result->success = true;
            result->error = ServiceError::None;
        }
        return true;
    }
}

bool ETRunService::stop()
{
    return removeService();
}

bool ETRunService::pauseConnection(CommandResult *result)
{
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        std::cerr << "ETRunService::pauseConnection: 找不到 easytier-cli: " << cliPath.toStdString() << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::CliMissing;
            result->errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        }
        return false;
    }

    QString workDir = getWorkingDirectory();
    QStringList stopArgs;
    stopArgs << "service";
#ifndef Q_OS_WIN32
    stopArgs << "-n" << SERVICE_NAME;
#endif
    stopArgs << "stop";

    std::clog << "ETRunService: 暂停连接，仅停止服务 (需要提权授权)" << std::endl;

    if (!executeElevated(cliPath, stopArgs, workDir, result)) {
        std::cerr << "ETRunService: 服务停止失败" << std::endl;
        if (result && result->error == ServiceError::None) {
            result->error = ServiceError::StopFailed;
            result->errorString = "服务停止失败";
        }
        return false;
    }

    if (result) {
        result->success = true;
        result->error = ServiceError::None;
    }
    std::clog << "ETRunService: 服务已停止，未卸载" << std::endl;
    return true;
}

bool ETRunService::removeService(CommandResult *result)
{
    QString cliPath = getCliPath();
    if (!QFile::exists(cliPath)) {
        std::cerr << "ETRunService::stop: 找不到 easytier-cli: " << cliPath.toStdString() << std::endl;
        if (result) {
            result->success = false;
            result->error = ServiceError::CliMissing;
            result->errorString = QString("找不到 easytier-cli: %1").arg(cliPath);
        }
        return false;
    }
    
    QString workDir = getWorkingDirectory();
    
#ifdef Q_OS_WIN32
    // Windows: 使用 cmd /c 串联停止和卸载命令
    QString stopCmd = QString("\"%1\" service stop").arg(cliPath);
    QString uninstallCmd = QString("\"%1\" service uninstall").arg(cliPath);
    
    QStringList args;
    args << "/c" << QString("\"%1 & %2\"").arg(stopCmd, uninstallCmd);
    
    std::clog << "ETRunService: 停止并卸载服务 (需要UAC授权)" << std::endl;
    
    if (!executeElevated("cmd.exe", args, workDir, result)) {
        std::cerr << "ETRunService: 停止并卸载服务失败" << std::endl;
        if (result && result->error == ServiceError::None) {
            result->error = ServiceError::UninstallFailed;
            result->errorString = "服务停止或卸载失败";
        }
        return false;
    }
    
    std::clog << "ETRunService: 停止并卸载服务成功" << std::endl;
    if (result) {
        result->success = true;
        result->error = ServiceError::None;
    }
    return true;
#else
    // Linux: 构造复合命令，用 ; 连接停止和卸载（无论停止是否成功都尝试卸载）
    QStringList stopArgs;
    stopArgs << "service" << "-n" << SERVICE_NAME << "stop";
    
    QStringList uninstallArgs;
    uninstallArgs << "service" << "-n" << SERVICE_NAME << "uninstall";
    
    // 构造复合命令: stop ; uninstall
    QString combinedCmd = QString("\"%1\" %2 ; \"%1\" %3")
                             .arg(cliPath)
                             .arg(stopArgs.join(" "))
                             .arg(uninstallArgs.join(" "));
    
    std::clog << "ETRunService: 停止并卸载服务 (需要pkexec授权)" << std::endl;
    std::clog << "复合命令: " << combinedCmd.toStdString() << std::endl;
    
    // 使用 bash -c 执行复合命令
    QStringList bashArgs;
    bashArgs << "-c" << combinedCmd;
    
    if (!executeElevated("bash", bashArgs, workDir, result)) {
        std::cerr << "ETRunService: 停止并卸载服务失败" << std::endl;
        if (result && result->error == ServiceError::None) {
            result->error = ServiceError::UninstallFailed;
            result->errorString = "服务停止或卸载失败";
        }
        return false;
    }
    
    std::clog << "ETRunService: 停止并卸载服务成功" << std::endl;
    if (result) {
        result->success = true;
        result->error = ServiceError::None;
    }
    return true;
#endif
}

bool ETRunService::isRunning()
{
#ifdef Q_OS_WIN32
    // 创建系统进程快照，TH32CS_SNAPPROCESS 表示包含所有进程信息
    // 参数 dwProcessID=0 表示当前进程，对 TH32CS_SNAPPROCESS 无实际影响
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "ETRunService::isRunning: CreateToolhelp32Snapshot 失败" << std::endl;
        return false;
    }

    // PROCESSENTRY32W 是进程信息的宽字符版本结构体
    // 必须在调用 Process32FirstW 之前设置 dwSize 为结构体大小，否则调用失败
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    bool found = false;
    // Process32FirstW 获取快照中的第一个进程信息
    if (Process32FirstW(snapshot, &entry)) {
        do {
            // _wcsicmp 宽字符不区分大小写比较，匹配进程名
            if (_wcsicmp(entry.szExeFile, L"easytier-deamon.exe") == 0) {
                found = true;
                break;
            }
            // Process32NextW 遍历快照中的下一个进程
        } while (Process32NextW(snapshot, &entry));
    }

    // 关闭快照句柄，释放系统资源
    CloseHandle(snapshot);
    return found;
#elif defined(Q_OS_MACOS)
    // macOS: 直接检测 easytier-deamon 进程（pgrep 无需提权）
    QProcess process;
    process.start("pgrep", QStringList() << "-x" << "easytier-deamon");
    process.waitForFinished(5000);
    return process.exitCode() == 0;
#else
    // Linux: 使用 easytier-cli service status 检查服务状态
    QString cliPath = getCliPath();
    QStringList args;
    args << "service" << "-n" << SERVICE_NAME << "status";

    CommandResult result = executeCommand(cliPath, args, 10000);

    if (result.success) {
        if (result.outputContains("running")) {
            return true;
        }
        if (result.outputContains("stopped")) {
            return false;
        }
    }

    // 备用方案：检查 easytier-deamon 进程是否存在
    QProcess process;
    process.start("pgrep", QStringList() << "-x" << "easytier-deamon");
    process.waitForFinished(5000);
    return process.exitCode() == 0;
#endif
}
