/**
 * @file AboutDialog.cpp
 * @brief 关于对话框实现
 */

#include "AboutDialog.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QDate>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
}

void AboutDialog::setupUI()
{
    setWindowTitle(QString("关于 %1").arg(APP_DISPLAY_NAME));
    setWindowIcon(QIcon(FAVICON_SVG));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(400, 280);
    
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    
    // ---------- 应用图标 ----------
    auto *iconLabel = new QLabel(this);
    QPixmap iconPixmap(FAVICON_SVG);
    if (!iconPixmap.isNull()) {
        // SVG 缩放到 64x64，保持宽高比，平滑渲染
        iconLabel->setPixmap(iconPixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(iconLabel);
    
    // ---------- 应用名称 ----------
    auto *titleLabel = new QLabel(APP_DISPLAY_NAME, this);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);
    
    // ---------- 版本号 ----------
    // applicationVersion 在 main.cpp 中设置，格式为 "1.0.0(et2.6.3)"
    auto *versionLabel = new QLabel(QString("版本: %1").arg(qApp->applicationVersion()), this);
    versionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(versionLabel);
    mainLayout->addStretch();
    
    // ---------- 描述 ----------
    auto *descriptionLabel = new QLabel(
        "极简设计，简单易用的 EasyTier Web 控制台连接器",
        this
    );
    descriptionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(descriptionLabel);
    mainLayout->addStretch();

    // ---------- 版权 ----------
    auto *copyrightLabel = new QLabel(
        QString("Copyright © %1 Myqfeng. All rights reserved.").arg(QDate::currentDate().year()),
        this
    );
    QFont copyrightFont = copyrightLabel->font();
    copyrightFont.setPointSize(8);  // 版权信息用小号字
    copyrightLabel->setFont(copyrightFont);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(copyrightLabel);
    
    // ---------- 按钮 ----------
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    // 赞助作者按钮 → 打开默认浏览器跳转赞助页面
    auto *sponsorButton = new QPushButton("赞助作者", this);
    connect(sponsorButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://qtet.myqfeng.top/other/donate/"));
    });
    buttonLayout->addWidget(sponsorButton);
    
    auto *okButton = new QPushButton("确定", this);
    okButton->setDefault(true);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    
    buttonLayout->addWidget(okButton);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);
}
