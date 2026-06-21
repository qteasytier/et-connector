/**
 * @file QuitConfirmDialog.cpp
 * @brief 退出确认对话框实现
 */

#include "QuitConfirmDialog.h"
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>

QuitConfirmDialog::QuitConfirmDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
}

void QuitConfirmDialog::setupUI()
{
    setWindowTitle("退出确认");
    setWindowIcon(QIcon(FAVICON_SVG));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(380, 180);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // 提示文本
    auto *messageLabel = new QLabel(
        "EasyTier 服务正在运行中，是否停止连接并退出？",
        this
    );
    messageLabel->setWordWrap(true);
    messageLabel->setAlignment(Qt::AlignCenter);
    QFont msgFont = messageLabel->font();
    msgFont.setPointSize(msgFont.pointSize() + 1);
    messageLabel->setFont(msgFont);
    mainLayout->addWidget(messageLabel);

    mainLayout->addStretch();

    // "记住我的选择" 复选框
    m_rememberCheckBox = new QCheckBox("记住我的选择", this);
    mainLayout->addWidget(m_rememberCheckBox, 0, Qt::AlignCenter);

    // 提示：选择将被保存到配置文件
    auto *hintLabel = new QLabel("你的选择将被保存到配置文件", this);
    hintLabel->setStyleSheet("color: gray; font-size: 11px;");
    hintLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(hintLabel);

    mainLayout->addStretch();

    // 按钮
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_yesButton = new QPushButton("是", this);
    m_yesButton->setDefault(true);
    connect(m_yesButton, &QPushButton::clicked, this, [this]() {
        m_choseStopAndQuit = true;
        accept();
    });

    m_noButton = new QPushButton("否", this);
    connect(m_noButton, &QPushButton::clicked, this, [this]() {
        m_choseStopAndQuit = false;
        accept();
    });

    buttonLayout->addWidget(m_yesButton);
    buttonLayout->addWidget(m_noButton);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);
}

bool QuitConfirmDialog::isRememberChoice() const
{
    return m_rememberCheckBox && m_rememberCheckBox->isChecked();
}

bool QuitConfirmDialog::choseStopAndQuit() const
{
    return m_choseStopAndQuit;
}
