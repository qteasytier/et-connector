/**
 * @file SettingsDialog.cpp
 * @brief 设置对话框实现
 *
 * 布局结构：
 * ┌──────────────────────────────┐
 * │     连接地址与密钥            │
 * │  [地址与密钥: _____________]  │
 * │  [x] 启用安全模式             │
 * │  ℹ 请前往 EasyTier Pro ...   │
 * │          [确定] [取消]        │
 * └──────────────────────────────┘
 */

#include "SettingsDialog.h"
#include <QLineEdit>
#include <QCheckBox>
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
    // 移除 Windows 标题栏上的"?"帮助按钮
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(400, 230);
    
    auto *mainLayout = new QVBoxLayout(this);
    
    // ---------- 密钥输入区域 ----------
    auto *keyGroup = new QGroupBox("连接地址与密钥", this);
    auto *keyLayout = new QHBoxLayout(keyGroup);
    keyGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *keyLabel = new QLabel("地址与密钥:", this);
    m_keyEdit = new QLineEdit(this);
    m_keyEdit->setMinimumHeight(28);  // 稍微高一点方便用户点选
    updatePlaceholderText();

    keyLayout->addWidget(keyLabel);
    keyLayout->addWidget(m_keyEdit);

    mainLayout->addWidget(keyGroup);

    // ---------- 安全模式开关 ----------
    m_secureModeCheck = new QCheckBox("启用安全模式", this);
    m_secureModeCheck->setToolTip("安全模式下会加密所有通信数据");
    mainLayout->addWidget(m_secureModeCheck);

    // ---------- 提示文字 ----------
    auto *hintLabel = new QLabel("请前往 EasyTier Pro 控制台获取连接地址与密钥\n说明：如果使用开源控制台，您需要输入连接地址与用户名", this);
    hintLabel->setStyleSheet("color: #66ccff;");
    hintLabel->setMaximumHeight(30);
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(hintLabel);
    
    // ---------- 按钮栏 ----------
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();      // 弹性空间，把按钮推到右边
    
    m_okButton = new QPushButton("确定", this);
    m_okButton->setDefault(true);    // 回车键默认触发确定
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
    // trimmed() 去除首尾空白，防止用户不小心多输了空格
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
    // 直接接受对话框，不再 emit 连接密钥变更信号
    // （重构后将所有配置保存逻辑统一放在了 SystemTray::onSettings() 中）
    QDialog::accept();
}

bool SettingsDialog::getSecureMode() const
{
    return m_secureModeCheck ? m_secureModeCheck->isChecked() : true;
}

void SettingsDialog::setSecureMode(bool enabled)
{
    if (m_secureModeCheck) {
        m_secureModeCheck->setChecked(enabled);
    }
}
