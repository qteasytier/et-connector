/**
 * @file QuitConfirmDialog.h
 * @brief 退出确认对话框 - 询问是否停止 EasyTier 并退出
 */

#ifndef QUITCONFIRMDIALOG_H
#define QUITCONFIRMDIALOG_H

#include <QDialog>

class QCheckBox;
class QPushButton;

/**
 * @brief 退出确认对话框
 *
 * 当 EasyTier 服务正在运行时，询问用户是否停止服务后退出。
 * 提供"记住我的选择"选项，后续可直接按记忆执行。
 */
class QuitConfirmDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuitConfirmDialog(QWidget *parent = nullptr);
    ~QuitConfirmDialog() override = default;

    /**
     * @brief 用户是否勾选了"记住我的选择"
     */
    bool isRememberChoice() const;

    /**
     * @brief 用户点击的是"是"还是"否"
     * @return true = 是（停止并退出），false = 否（仅退出前端）
     */
    bool choseStopAndQuit() const;

private:
    void setupUI();

    QCheckBox *m_rememberCheckBox = nullptr;
    QPushButton *m_yesButton = nullptr;
    QPushButton *m_noButton = nullptr;

    bool m_choseStopAndQuit = false;
};

#endif // QUITCONFIRMDIALOG_H
