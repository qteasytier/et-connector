/**
 * @file SettingsDialog.h
 * @brief 设置对话框 - 用于配置连接地址与密钥
 *
 * 提供一个简洁的输入界面，包含：
 * - 连接地址与密钥输入框
 * - 安全模式开关（加密通信）
 * - 提示用户如何获取密钥的文字
 */

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

// 前向声明，减少头文件依赖
class QLineEdit;
class QCheckBox;
class QPushButton;

/**
 * @brief 设置对话框
 *
 * 模态对话框，用户在此输入连接密钥和配置安全模式选项。
 * 确定时直接通过 getConnectionKey() / getSecureMode() 获取值，
 * SystemTray::onSettings() 负责保存到配置文件和通知控制器。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param connectionKey 当前已保存的密钥，用于初始化输入框
     * @param parent        父窗口
     */
    explicit SettingsDialog(const QString &connectionKey = QString(), 
                            QWidget *parent = nullptr);
    ~SettingsDialog() override = default;
    
    /** 获取用户输入的密钥（已去除首尾空白） */
    QString getConnectionKey() const;

    /** 设置输入框中的密钥文本 */
    void setConnectionKey(const QString &key);

    /** 获取安全模式复选框的勾选状态 */
    bool getSecureMode() const;

    /** 设置安全模式复选框的勾选状态 */
    void setSecureMode(bool enabled);

protected:
    /** 重写确定按钮行为，直接接受对话框 */
    void accept() override;

private:
    /** 构建 UI 布局 */
    void setupUI();

    /** 更新输入框的占位提示文字 */
    void updatePlaceholderText();
    
    QLineEdit *m_keyEdit = nullptr;          ///< 密钥输入框
    QCheckBox *m_secureModeCheck = nullptr;  ///< 安全模式复选框
    QPushButton *m_okButton = nullptr;       ///< 确定按钮
    QPushButton *m_cancelButton = nullptr;   ///< 取消按钮
};

#endif // SETTINGSDIALOG_H
