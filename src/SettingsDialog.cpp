/**
 * @file SettingsDialog.cpp
 * @brief 设置对话框实现
 */

#include "SettingsDialog.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QIcon>

SettingsDialog::SettingsDialog(const QString &connectionKey, QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setConnectionKey(connectionKey);
}

void SettingsDialog::setupUI()
{
    setWindowTitle("设置连接地址与密钥");

    setWindowIcon(QIcon(FAVICON_SVG));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(400, 180);
    
    auto *mainLayout = new QVBoxLayout(this);
    
    // 密钥输入组
    auto *keyGroup = new QGroupBox("连接地址与密钥", this);
    auto *keyLayout = new QHBoxLayout(keyGroup);
    keyGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *keyLabel = new QLabel("地址与密钥:", this);
    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setMinimumHeight(28);
    updatePlaceholderText();

    keyLayout->addWidget(keyLabel);
    keyLayout->addWidget(m_keyEdit);

    mainLayout->addWidget(keyGroup);

    // 提示文本
    auto *hintLabel = new QLabel("请前往 EasyTier Pro 控制台获取连接地址与密钥\n说明：如果使用开源控制台，您需要输入连接地址与用户名", this);
    hintLabel->setStyleSheet("color: #66ccff;");
    hintLabel->setMaximumHeight(30);
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(hintLabel);
    
    // 按钮
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_okButton = new QPushButton("确定", this);
    m_okButton->setDefault(true);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    
    m_cancelButton = new QPushButton("取消", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    
    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::updatePlaceholderText()
{
    if (m_keyEdit) {
        m_keyEdit->setPlaceholderText("例: tcp://et.example.cn:666/etk_xxxxx");
    }
}

QString SettingsDialog::getConnectionKey() const
{
    return m_keyEdit ? m_keyEdit->text().trimmed() : QString();
}

void SettingsDialog::setConnectionKey(const QString &key)
{
    if (m_keyEdit) {
        m_keyEdit->setText(key);
    }
}

void SettingsDialog::accept()
{
    emit connectionKeyChanged();
    QDialog::accept();
}
