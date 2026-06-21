/**
 * @file AboutDialog.h
 * @brief 关于对话框 - 显示应用程序信息
 */

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

/**
 * @brief 关于对话框
 * 
 * 显示应用程序图标、名称、版本、描述和版权信息。
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override = default;

private:
    void setupUI();
};

#endif // ABOUTDIALOG_H
