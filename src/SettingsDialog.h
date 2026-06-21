/**
 * @file SettingsDialog.h
 * @brief 设置对话框 - 用于配置连接密钥
 */

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QLineEdit;
class QPushButton;

/**
 * @brief 设置对话框
 * 
 * 用于输入和修改 EasyTier 连接密钥。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const QString &connectionKey = QString(), 
                            QWidget *parent = nullptr);
    ~SettingsDialog() override = default;
    
    QString getConnectionKey() const;
    void setConnectionKey(const QString &key);

signals:
    void connectionKeyChanged();

protected:
    void accept() override;

private:
    void setupUI();
    void updatePlaceholderText();
    
    QLineEdit *m_keyEdit = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

#endif // SETTINGSDIALOG_H
