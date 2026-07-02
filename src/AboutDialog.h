/**
 * @file AboutDialog.h
 * @brief 关于对话框 - 显示应用程序的基本信息
 *
 * 展示：应用图标、名称、版本号、描述、版权信息和赞助链接。
 */

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

/**
 * @brief 关于对话框
 *
 * 一个固定大小的模态窗口，居中显示应用相关信息。
 * 包含"赞助作者"按钮，点击后打开默认浏览器跳转到赞助页面。
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override = default;

private:
    /** 构建 UI 布局：图标 + 标题 + 版本 + 描述 + 版权 + 按钮 */
    void setupUI();
};

#endif // ABOUTDIALOG_H
